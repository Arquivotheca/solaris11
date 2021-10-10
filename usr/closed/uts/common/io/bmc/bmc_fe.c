/*
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * IPMI: front end to BMC access
 */

#include <sys/types.h>
#include <sys/list.h>
#include <sys/stropts.h>
#include <sys/note.h>
#include <sys/stat.h>
#include <sys/kstat.h>
#include <sys/devops.h>
#include <sys/dditypes.h>
#include <sys/stream.h>
#include <sys/strsun.h>
#include <sys/modctl.h>
#include <sys/varargs.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/policy.h>
#include <sys/sysmacros.h>
#include <sys/smbios.h>
#include <sys/atomic.h>
#include <sys/bmc_intf.h>
#include <sys/bmc_cmd.h>

#include "bmc_fe.h"

static boolean_t bmc_command_requires_privilege(uint8_t, uint8_t);
static void	bmc_process_msg(ipmi_state_t *ipmip, queue_t *q, mblk_t *mp);
static mblk_t *bmc_build_errmsg(uint32_t mid, uint8_t error);
static void bmc_inscribe_errmsg(mblk_t *mp, uint32_t mid, uint8_t error);
static mblk_t *bmc_alloc_response_shell(void);
static int bmc_kstat_create(ipmi_state_t *ipmip, dev_info_t *dip);

int ipmi_debug = 0;

/*
 * Total message size is (# of bytes before the msg field in the bmc_msg_t
 * field) + the full size of the bmc_rsp_t structure, including all
 * non-data members + size of the data array (variable).
 */
#define	BMC_MAX_RSP_MSGSZ (offsetof(bmc_msg_t, msg) + \
    offsetof(bmc_rsp_t, data) + BMC_MAX_RESPONSE_PAYLOAD_SIZE())

#define	BMC_NUM_CMDS	256

static void *ipmi_state;
static bmc_clone_t *bmc_clones;
static int bmc_nclones;


/*ARGSUSED*/
static void
bmc_mioctl(queue_t *q, mblk_t *mp)
{
	struct iocblk		*iocp = (struct iocblk *)(void *)mp->b_rptr;
	mblk_t			*mptr;
	ipmi_state_t *ipmip = BMC_CLONE(q->q_ptr)->ipmip;
	ipmi_dev_t *devp = &ipmip->ipmi_dev_ext;
	bmc_reqrsp_t *intfp;
	uint8_t *methodp;
	unsigned char ack_type = M_IOCNAK;
	uint8_t err = 0;

	dprintf(BMC_DEBUG_LEVEL_4, "IOCTL  enter");

	/* mptr points to the the data the user passed down */
	mptr = mp->b_cont;

	/* Consolidate multiple mblk_t's used to build this message */
	if (mptr) {
		dprintf(BMC_DEBUG_LEVEL_4, "mptr: %p", (void *)mptr);
		if (pullupmsg(mptr, -1) == 0) {
			dprintf(BMC_DEBUG_LEVEL_4, "pullupmsg failure");
			iocp->ioc_error = EINVAL;
			goto mioctl_exit;
		}

		intfp = (bmc_reqrsp_t *)mptr->b_rptr;
	}

	/* Make sure that the user passed in something */
	if (intfp == NULL) {
		dprintf(BMC_DEBUG_LEVEL_4, "No data passed with M_IOCTL");
		iocp->ioc_error = EINVAL;
		goto mioctl_exit;
	}

	/* Don't allow transparent ioctls */
	if (iocp->ioc_count == TRANSPARENT) {
		dprintf(BMC_DEBUG_LEVEL_4, "TRANSPARENT ioctls not allowed");
		iocp->ioc_error = EINVAL;
		goto mioctl_exit;
	}

	if (devp == NULL) {
		dprintf(BMC_DEBUG_LEVEL_4, "deviceExt is NULL");
		iocp->ioc_error = EINVAL;
		goto mioctl_exit;
	}


	dprintf(BMC_DEBUG_LEVEL_4, "IOCTL cmd 0x%x count 0x%lx",
	    iocp->ioc_cmd, (ulong_t)iocp->ioc_count);

	switch (iocp->ioc_cmd) {

	case IOCTL_IPMI_KCS_ACTION:
		/* Perform some sanity checks on the ioctl parameter */
		if (iocp->ioc_count < sizeof (bmc_reqrsp_t)) {
			dprintf(BMC_DEBUG_LEVEL_4, "ioctl data too small");
			iocp->ioc_error = EINVAL;
			break;
		}

		if (intfp->req.datalength > SEND_MAX_PAYLOAD_SIZE ||
		    intfp->rsp.datalength > RECV_MAX_PAYLOAD_SIZE) {
			dprintf(BMC_DEBUG_LEVEL_4, "ioctl datalength too big");
			intfp->rsp.ccode = BMC_IPMI_DATA_LENGTH_EXCEED;
			iocp->ioc_rval = 0;
			ack_type = M_IOCACK;
			break;
		}

		/* Was the command supplied valid? */
		if (intfp->req.cmd >= (BMC_NUM_CMDS - 1)) {
			dprintf(BMC_DEBUG_LEVEL_4, "ioctl invalid command");
			intfp->rsp.ccode = BMC_IPMI_INVALID_COMMAND;
			iocp->ioc_rval = 0;
			ack_type = M_IOCACK;
			break;
		}

		/* Is the user authorized to use this command? */
		if (bmc_command_requires_privilege(intfp->req.cmd,
		    intfp->req.fn) && secpolicy_sys_config(iocp->ioc_cr,
		    B_FALSE) != 0) {
			iocp->ioc_error = EACCES;
			break;
		}

		dprintf(BMC_DEBUG_LEVEL_2,
		    "IOCTL  cmd 0x%x subcmd 0x%x req_len 0x%x rsp_len 0x%x "
		    "code 0x%x",
		    iocp->ioc_cmd, intfp->req.cmd,
		    intfp->req.datalength,
		    intfp->rsp.datalength,
		    intfp->rsp.ccode);

#if !defined(__sparc)
		(void) do_kcs2bmc(ipmip, &intfp->req, &intfp->rsp, B_TRUE,
		    &err);
#else
		(void) do_vc2bmc(ipmip, &intfp->req, &intfp->rsp, B_TRUE,
		    &err);
#endif

		if (err == ENXIO) {
			intfp->rsp.ccode = BMC_IPMI_COMMAND_TIMEOUT;
			intfp->rsp.datalength = 0;
		} else if (err) {
			iocp->ioc_error = err;
			break;
		}

		dprintf(BMC_DEBUG_LEVEL_2,
		    "IOCTL DONE subcmd 0x%x req_len 0x%x rsp_len 0x%x code "
		    "0x%x",
		    intfp->req.cmd,
		    intfp->req.datalength,
		    intfp->rsp.datalength,
		    intfp->rsp.ccode);

		dprintf(BMC_DEBUG_LEVEL_2,
		    "IOCTL DONE2 subcmd 0x%x req_len 0x%x rsp_len 0x%x code "
		    "0x%x",
		    intfp->req.cmd,
		    intfp->req.datalength,
		    intfp->rsp.datalength,
		    intfp->rsp.ccode);

		/*
		 * We add 3 to datalength because the response previously
		 * published this field as being 3 bytes more than the
		 * data field length, so even though the interface was fixed
		 * to report the correct length of the data field, we must
		 * continue to publish this adjustment to ensure backward
		 * compatibility.
		 */
		intfp->rsp.datalength += 3;

		ack_type = M_IOCACK;
		iocp->ioc_rval = 0;
		break;

	case IOCTL_IPMI_INTERFACE_METHOD:
		/*
		 * If the user has provided at least enough space to hold
		 * the interface type, then return it.  Otherwise, bail
		 * out with an error.
		 */
		if (iocp->ioc_count >= sizeof (uint8_t)) {

			/* All future accesses should be via putmsg/getmsg */
			methodp = (uint8_t *)mptr->b_rptr;
			*methodp = BMC_PUTMSG_METHOD;
			ack_type = M_IOCACK;
			iocp->ioc_rval = 0;
			iocp->ioc_count = 1;
		} else {
			dprintf(BMC_DEBUG_LEVEL_3,
			    "IOCTL_IPMI_INTERFACE_METHOD: Not enough data"
			    " supplied to ioctl");
			iocp->ioc_error = ENOSPC;
		}

		break;

	default:
		iocp->ioc_error = EINVAL;
		break;
	}

mioctl_exit:
	mp->b_datap->db_type = ack_type;
	qreply(q, mp);
}

static int
bmc_wput(queue_t *q, mblk_t *mp)
{
	dprintf(BMC_DEBUG_LEVEL_4, "bmc_wput  enter");
	/* We're expecting a message with data here */
	ASSERT(mp != NULL);
	ASSERT(mp->b_datap != NULL);

	switch (mp->b_datap->db_type) {

	case M_DATA:
		/* Queue for later processing */
		if (!putq(q, mp)) {
			dprintf(BMC_DEBUG_LEVEL_2, "putq(M_DATA) failed!");
			freemsg(mp);
		}
		break;

	case M_IOCTL:
		/* Process the I_STR ioctl() from user land */
		bmc_mioctl(q, mp);
		break;

	case M_FLUSH:
		/*
		 * Flush processing is a requirement of streams drivers and
		 * modules.
		 *
		 * The bmc driver does not use the read queue, so M_FLUSH
		 * handling consists of passing a read flush message back
		 * up the read side of the queue to any modules that may
		 * be residing above it as well as clearing the write queue,
		 * if requested.
		 *
		 */
		if (*mp->b_rptr & FLUSHW) {
			dprintf(BMC_DEBUG_LEVEL_2, "Flush write queue");
			flushq(q, FLUSHALL);
			*mp->b_rptr &= ~FLUSHW;
		}
		if (*mp->b_rptr & FLUSHR) {
			dprintf(BMC_DEBUG_LEVEL_2, "Flush read queue");
			qreply(q, mp);
		} else
			/* No read processing required.  Throw away message */
			freemsg(mp);
		break;

	default:
		dprintf(BMC_DEBUG_LEVEL_2,
		    "Message not understood.  Ignoring. db_type = %d",
		    mp->b_datap->db_type);
		freemsg(mp);
		break;
	}

	return (0);

}

/*
 * Write-size queue processing
 *
 * Process data messages and perform BMC operations as directed.
 */
static int
bmc_wsrv(queue_t *q)
{
	mblk_t *mp, *errmp;
	queue_t *rq = RD(q);
	bmc_clone_t *clone = BMC_CLONE(q->q_ptr);
	bmc_kstat_t *ksp = &clone->ipmip->bmc_kstats;

	while (mp = getq(q)) {
		/* We only queued M_DATA messages */
		ASSERT(mp->b_datap->db_type == M_DATA);

		/*
		 * If we wouldn't be able to put a message upstream, hold
		 * off on processing this message and but it back on our
		 * write queue.  We'll get scheduled later and check the
		 * state of our upstream partner at that time.
		 */
		if (!canputnext(rq)) {
			/* If putbq fails, free the message */
			if (!putbq(q, mp))
				freemsg(mp);
			break;
		}

		/* The message must be at LEAST as large as a bmc_msg_t */
		if (MBLKSIZE(mp) < sizeof (bmc_msg_t)) {

			dprintf(BMC_DEBUG_LEVEL_4, "Message is smaller than "
			    "min msg size (size was %lu, must be at least %lu)",
			    MBLKSIZE(mp), (ulong_t)sizeof (bmc_msg_t));


			/*
			 * Invalid message -- try to create an error
			 * message and send it upstream.
			 */
			if ((errmp = bmc_build_errmsg(BMC_UNKNOWN_MSG_ID,
			    BMC_IPMI_INVALID_COMMAND)) != NULL) {
				freemsg(mp);
				qreply(q, errmp);
			} else {

				atomic_inc_64(&ksp->
				    bmc_alloc_failures.value.ui64);

				/*
				 * Couldn't even allocate an error reply, so
				 * try to reuse mp to send an error message
				 * upstream. If the message is smaller than 1
				 * byte, drop the message on the floor (this
				 * will hang the user process, but that's what
				 * they get for sending a malformed message).
				 */
				if (MBLKSIZE(mp) > 0)
					merror(q, mp, EINVAL);
			}
		} else {
			/* Process the message and send the reply upstream */
			bmc_process_msg(clone->ipmip, q, mp);
		}
	}
	return (0);
}


/*ARGSUSED*/
static int
bmc_open(queue_t *q, dev_t *devp, int flag, int sflag, cred_t *credp)
{
	int instance = getminor(*devp);
	ipmi_state_t *ipmip;
	int c;

	if (sflag) {
		/* Clone open NOT supported here */
		return (ENXIO);
	}

	if ((ipmip = ddi_get_soft_state(ipmi_state, instance)) == NULL) {
		return (ENXIO);
	}

	/*
	 * Note this is a per open clone state structure used to allocate
	 * unused minor numbers for new clone opens and as glue between the
	 * open clone and a physicial device state.
	 *
	 * Locate and reserve a clone structure.  We skip clone 0 as that is
	 * the real minor number, and we assign a new minor to each clone.
	 */
	for (c = 0; c < bmc_nclones; c++) {
		if (casptr(&bmc_clones[c].ipmip, NULL, ipmip) == NULL) {
			break;
		}
	}

	if (c >= bmc_nclones)
		return (EAGAIN);

	*devp = bmc_clones[c].dev = makedevice(getemajor(*devp), c + 1);
	bmc_clones[c].clone_closed = B_FALSE;

	/* Init q data pointers */
	q->q_ptr = WR(q)->q_ptr = &bmc_clones[c];

	qprocson(q);	/* Turn on the q */
	return (0);
}

/*ARGSUSED*/
static int
bmc_close(queue_t *q, int flag, cred_t *credp)
{
	bmc_clone_t  *clone = BMC_CLONE(q->q_ptr);

	qprocsoff(q);	/* Turn the q off */

	/*
	 * If there are no outstanding requests we can simply free up the clone
	 * slot. Otherwise we set our boolean flag to indicate that the last
	 * request should clean up after completion.
	 */
	mutex_enter(&clone->clone_mutex);
	if (clone->clone_req_cnt == 0)
		clone->ipmip = NULL;
	else
		clone->clone_closed = B_TRUE;
	mutex_exit(&clone->clone_mutex);

	return (0);
}

#if !defined(__sparc)
static int
bmc_getbmcversions(ipmi_state_t *ipmip, boolean_t can_intr, uint8_t *errp)
{
	bmc_req_t req;
	bmc_rsp_t rsp;

	bzero(&req, sizeof (bmc_req_t));
	bzero(&rsp, sizeof (bmc_rsp_t));

	req.fn = BMC_NETFN_APP;
	req.cmd = BMC_GET_DEVICE_ID;

	rsp.fn = BMC_NETFN_APP;
	rsp.cmd = BMC_GET_DEVICE_ID;
	rsp.datalength = RECV_MAX_PAYLOAD_SIZE;


	/*
	 * We specifically do not try to deal with *errp here, since this
	 * function is only called from bmc_attach() and we must ensure
	 * that the hardware is alive at attach time.
	 */
	if (do_kcs2bmc(ipmip, &req, &rsp, can_intr, errp) == BMC_SUCCESS) {

		/* check for the version */
		if (rsp.ccode != 0) {
			goto getbmcversions_error;
		}

		if (rsp.data[4] == BMC_IPMI_15_VER) {
			dprintf(BMC_DEBUG_LEVEL_3, "F/W Version: %x.%x",
			    (rsp.data[2] & 0x7F), rsp.data[3]);
		}

		return (BMC_SUCCESS);
	}

getbmcversions_error:

	if (*errp)
		dprintf(BMC_DEBUG_LEVEL_2, "getbmcversion interrupted");
	else
		dprintf(BMC_DEBUG_LEVEL_2, "getbmcversion failed");

	return (BMC_FAILURE);
}
#endif

static int
bmc_dispatch(ipmi_state_t *ipmip, int cmd, queue_t *q,
    mblk_t *reqmp, mblk_t *respmp,
    bmc_req_t *request, bmc_rsp_t *response)
{
	ipmi_task_t	*task_ent;
	bmc_clone_t *clone;

	task_ent = kmem_zalloc(sizeof (ipmi_task_t), KM_NOSLEEP);
	if (task_ent == NULL)
		return (0);

	task_ent->cmd = cmd;
	task_ent->q = q;
	task_ent->reqmp = reqmp;
	task_ent->respmp = respmp;
	task_ent->request = request;
	task_ent->response = response;

	/*
	 * Increment the count of outstanding requests to effect a hold on this
	 * clone.
	 */
	if (q != NULL) {
		clone = BMC_CLONE(q->q_ptr);
		task_ent->clone = clone;
		mutex_enter(&clone->clone_mutex);
		clone->clone_req_cnt++;
		mutex_exit(&clone->clone_mutex);
	}

	mutex_enter(&ipmip->task_mutex);

	/*
	 * If we are detaching do not allow any new work
	 * So task will exit as fast as possible.
	 */
	if (ipmip->task_abort == B_TRUE) {
		mutex_exit(&ipmip->task_mutex);
		kmem_free(task_ent, sizeof (ipmi_task_t));
		return (0);
	}

	/*
	 * We do this here so that if the task is already working an
	 * entry, it will end early and be ready for the exit faster.
	 */
	if (cmd == BMC_TASK_EXIT)
		ipmip->task_abort = B_TRUE;

	list_insert_tail(&ipmip->task_list, task_ent);
	cv_signal(&ipmip->task_cv);
	mutex_exit(&ipmip->task_mutex);

	return (1);
}

static void
bmc_task_ipmi(bmc_clone_t *clone, queue_t *q, mblk_t *reqmp, mblk_t *respmp,
    bmc_req_t *request, bmc_rsp_t *response)
{
	uint8_t err = 0;
	uint32_t mid = ((bmc_msg_t *)(void *)reqmp->b_rptr)->m_id;
	bmc_kstat_t *ksp = &clone->ipmip->bmc_kstats;

#if !defined(__sparc)
	(void) do_kcs2bmc(clone->ipmip, request, response, B_TRUE, &err);
#else
	(void) do_vc2bmc(clone->ipmip, request, response, B_TRUE, &err);
#endif
	switch (err) {
#if !defined(__sparc)
	case ENXIO:
		response->ccode = BMC_IPMI_COMMAND_TIMEOUT;
		response->datalength = 0;
		/*
		 * Since we're faking a timeout, clear the "error"
		 * condition -- this will cause the response to be
		 * properly returned to the process.
		 */
		err = 0;
		break;
#endif
	case 0:
		dprintf(BMC_DEBUG_LEVEL_2,
		    "MSG DONE subcmd 0x%x req_len 0x%x rsp_len "
		    "0x%x code 0x%x",
		    request->cmd,
		    request->datalength,
		    response->datalength,
		    response->ccode);
		break;

	default:
		break;
	}

	/* Always free the pulled up request msg: */
	freemsg(reqmp);

	if (err == 0) { /* Usual case */
		/*
		 * Adjust the response message's wptr to reflect its actual
		 * size
		 */
		respmp->b_wptr = respmp->b_rptr + (offsetof(bmc_msg_t, msg) +
		    offsetof(bmc_rsp_t, data) + response->datalength);

	} else {
		if (err == ENOMEM)
			atomic_inc_64(&ksp->bmc_alloc_failures.value.ui64);

		/*
		 * Build an error reply message for the user
		 * using the original message
		 */
		bmc_inscribe_errmsg(respmp, mid, err);
	}

	/*
	 * If the clone was closed during processing, just discard the message;
	 * otherwise send the reply.
	 */
	mutex_enter(&clone->clone_mutex);
	if (clone->clone_closed)
		freemsg(respmp);
	else
		qreply(q, respmp);
	mutex_exit(&clone->clone_mutex);
}

static void
bmc_task(void *arg)
{
	ipmi_task_t	*task_ent;
	ipmi_state_t	*ipmip = arg;
	bmc_clone_t	*clone;

	mutex_enter(&ipmip->task_mutex);
	while (ipmip->task_abort != B_TRUE) {
		while ((task_ent = list_head(&ipmip->task_list)) != NULL) {
			if (ipmip->task_abort == B_TRUE) {
				/*
				 * Once task_abort is set it prevents any
				 * new entries. If we were working an entry
				 * when task_abort was set it ended without
				 * completing, early. We will just return
				 * and let bmc_detach go through the list
				 * and free all allocated resources so we
				 * don't cause any memory leaks.
				 */
				mutex_exit(&ipmip->task_mutex);
				return;
			}
			list_remove(&ipmip->task_list, task_ent);
			clone = task_ent->clone;
			mutex_exit(&ipmip->task_mutex);

			switch (task_ent->cmd) {
			case BMC_TASK_IPMI:
				ASSERT(ipmip == clone->ipmip);
				bmc_task_ipmi(clone, task_ent->q,
				    task_ent->reqmp, task_ent->respmp,
				    task_ent->request, task_ent->response);
			default:
				break;
			}

			/*
			 * If this was the last entry queued for a clone and
			 * this clone was already closed then clear clone state.
			 */
			mutex_enter(&clone->clone_mutex);
			ASSERT(clone->clone_req_cnt != 0);
			clone->clone_req_cnt--;
			if (clone->clone_req_cnt == 0 && clone->clone_closed)
				clone->ipmip = NULL;
			mutex_exit(&clone->clone_mutex);

			kmem_free(task_ent, sizeof (ipmi_task_t));
			mutex_enter(&ipmip->task_mutex);
		}
		cv_wait(&ipmip->task_cv, &ipmip->task_mutex);
	}
	mutex_exit(&ipmip->task_mutex);
}

static void
bmc_task_drain(ipmi_state_t *ipmip)
{
	ipmi_task_t	*task_ent, *task_nxt;

	/*
	 * Note that no locks are needed since this is only called
	 * from the detach routine after the task has already exited.
	 */
	for (task_ent = list_head(&ipmip->task_list); task_ent; ) {
		task_nxt = list_next(&ipmip->task_list, task_ent);
		list_remove(&ipmip->task_list, task_ent);
		freemsg(task_ent->reqmp);
		freemsg(task_ent->respmp);
		kmem_free(task_ent, sizeof (ipmi_task_t));
		task_ent = task_nxt;
	}
}

static int
bmc_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	int instance = ddi_get_instance(dip);
	ipmi_state_t *ipmip;
	int c;
	int driver_enabled;
#if !defined(__sparc)
	smbios_ipmi_t ip;
	uint8_t err = 0;
	int bmc_gone = 0;
#endif

	driver_enabled = ddi_prop_get_int(DDI_DEV_T_ANY, dip, 0, "bmc-enable",
	    1);
	if (!driver_enabled) {
		return (DDI_FAILURE);
	}

	if (cmd != DDI_ATTACH)
		return (DDI_FAILURE);

	if (ddi_soft_state_zalloc(ipmi_state, instance) != DDI_SUCCESS)
		return (DDI_FAILURE);

	if ((ipmip = ddi_get_soft_state(ipmi_state, instance)) == NULL) {
		ddi_soft_state_free(ipmi_state, instance);
		return (DDI_FAILURE);
	}

	ipmip->ipmi_dip = dip;

	/* Setup event taskq. */
	ipmip->task_q = ddi_taskq_create(dip, "bmc_tq", 1, TASKQ_DEFAULTPRI, 0);
	if (ipmip->task_q == NULL) {
		ddi_soft_state_free(ipmi_state, instance);
		return (DDI_FAILURE);
	}
	cv_init(&ipmip->task_cv, NULL, CV_DRIVER, NULL);
	mutex_init(&ipmip->task_mutex, NULL, MUTEX_DRIVER, NULL);
	list_create(&ipmip->task_list, sizeof (struct ipmi_task),
	    offsetof(struct ipmi_task, task_linkage));
	ipmip->task_abort = B_FALSE; /* Used to expedite detach/unload */

	mutex_init(&ipmip->ipmi_dev_ext.if_mutex, NULL, MUTEX_DRIVER, NULL);
	cv_init(&ipmip->ipmi_dev_ext.if_cv, NULL, CV_DEFAULT, NULL);
	ipmip->ipmi_dev_ext.if_busy = B_FALSE;

	for (c = 0; c < bmc_nclones; c++) {
		mutex_init(&bmc_clones[c].clone_mutex, NULL, MUTEX_DRIVER,
		    NULL);
		bmc_clones[c].clone_closed = B_FALSE;
		bmc_clones[c].clone_req_cnt = 0;
	}

#if !defined(__sparc)
	mutex_init(&ipmip->ipmi_dev_ext.timer_mutex, NULL, MUTEX_DRIVER, NULL);
	cv_init(&ipmip->ipmi_dev_ext.timer_cv, NULL, CV_DEFAULT, NULL);

	/*
	 * At present, the bmc driver requires SMBIOS to describe an IPMI
	 * device and requires that it support the KCS transport mechanism.
	 */
	if (ksmbios == NULL || smbios_info_ipmi(ksmbios, &ip) == SMB_ERR ||
	    ip.smbip_type != SMB_IPMI_T_KCS) {
		ddi_taskq_destroy(ipmip->task_q);
		ipmip->task_q = NULL;
		list_destroy(&ipmip->task_list);
		ddi_soft_state_free(ipmi_state, instance);
		return (DDI_FAILURE);
	}

	ipmip->ipmi_dev_ext.bmcaddress = (uint32_t)ip.smbip_addr;
	ipmip->ipmi_dev_ext.bmcregspacing = ip.smbip_regspacing;
#else
	/* LDI initialization */
	if (vc_init(dip) != BMC_SUCCESS) {
		ddi_taskq_destroy(ipmip->task_q);
		ipmip->task_q = NULL;
		list_destroy(&ipmip->task_list);
		ddi_soft_state_free(ipmi_state, instance);
		return (DDI_FAILURE);
	}
#endif

	/*
	 * Create this instance's kstats
	 */
	if (bmc_kstat_create(ipmip, dip) != DDI_SUCCESS) {
		ddi_taskq_destroy(ipmip->task_q);
		ipmip->task_q = NULL;
		list_destroy(&ipmip->task_list);
		ddi_soft_state_free(ipmi_state, instance);
		return (DDI_FAILURE);
	}

#if !defined(__sparc)
	mutex_enter(&ipmip->ipmi_dev_ext.if_mutex);
	kcs_initregs(ipmip->ipmi_dev_ext.bmcaddress,
	    ipmip->ipmi_dev_ext.bmcregspacing);
	mutex_exit(&ipmip->ipmi_dev_ext.if_mutex);

	/* Try twice to get the BMC version -- with a reset in between */
	if (bmc_getbmcversions(ipmip, B_FALSE, &err) == BMC_FAILURE) {
		kcs_reset_bmc(&ipmip->ipmi_dev_ext, &bmc_gone,
		    &ipmip->task_abort);

		/* Ignore bmc_gone in this case and just try again */
		if (bmc_getbmcversions(ipmip, B_FALSE, &err) == BMC_FAILURE) {
			ddi_taskq_destroy(ipmip->task_q);
			ipmip->task_q = NULL;
			list_destroy(&ipmip->task_list);
			kstat_delete(ipmip->ksp);
			ddi_soft_state_free(ipmi_state, instance);
			return (DDI_FAILURE);
		}
	}
#endif

	/* Dispatch the taskq */
	if (ddi_taskq_dispatch(ipmip->task_q, bmc_task, ipmip, DDI_SLEEP) !=
	    DDI_SUCCESS) {
		ddi_taskq_destroy(ipmip->task_q);
		ipmip->task_q = NULL;
		list_destroy(&ipmip->task_list);
		kstat_delete(ipmip->ksp);
		ddi_soft_state_free(ipmi_state, instance);
		return (DDI_FAILURE);
	}

	if ((ddi_create_minor_node(dip, BMC_NODENAME, S_IFCHR,
	    BMC_MINOR, DDI_PSEUDO, 0)) != DDI_SUCCESS) {
		(void) bmc_dispatch(ipmip, BMC_TASK_EXIT,
		    NULL, NULL, NULL, NULL, NULL);
		ddi_taskq_destroy(ipmip->task_q);
		ipmip->task_q = NULL;
		list_destroy(&ipmip->task_list);
		kstat_delete(ipmip->ksp);
		ddi_soft_state_free(ipmi_state, instance);
		return (DDI_FAILURE);
	}

	ddi_report_dev(dip);
	return (DDI_SUCCESS);
}

static int
bmc_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	ipmi_state_t *ipmip;
	int	c;

	if (cmd != DDI_DETACH)
		return (DDI_FAILURE);

	ddi_remove_minor_node(dip, NULL);

#if defined(__sparc)
	(void) vc_uninit();
#endif

	ipmip = ddi_get_soft_state(ipmi_state, ddi_get_instance(dip));
	if (ipmip == NULL)
		return (DDI_FAILURE);

	if (ipmip->task_q != NULL) {
		(void) bmc_dispatch(ipmip, BMC_TASK_EXIT, NULL, NULL, NULL,
		    NULL, NULL);
		ddi_taskq_destroy(ipmip->task_q);
		ipmip->task_q = NULL;
		bmc_task_drain(ipmip);
	}

	kstat_delete(ipmip->ksp);
	list_destroy(&ipmip->task_list);
	ddi_soft_state_free(ipmi_state, ddi_get_instance(dip));
	for (c = 0; c < bmc_nclones; c++) {
		bmc_clones[c].clone_closed = B_FALSE;
		bmc_clones[c].clone_req_cnt = 0;
	}
	return (DDI_SUCCESS);
}

/*ARGSUSED*/
static int
bmc_getinfo(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	int error, instance = getminor((dev_t)arg);
	ipmi_state_t *ipmip = ddi_get_soft_state(ipmi_state, instance);

	if (ipmip == NULL)
		return (DDI_FAILURE);

	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		if (ipmip->ipmi_dip == NULL) {
			error = DDI_FAILURE;
		} else {
			*result = (void *)ipmip->ipmi_dip;
			error = DDI_SUCCESS;
		}
		break;
	case DDI_INFO_DEVT2INSTANCE:
		*result = (void *)(intptr_t)instance;
		error = DDI_SUCCESS;
		break;
	default:
		error = DDI_FAILURE;
	}

	return (error);
}

/*ARGSUSED*/
/*PRINTFLIKE2*/
void
dprintf(int d, const char *format, ...)
{
#ifdef DEBUG
	if (d <= ipmi_debug) {
		va_list ap;
		va_start(ap, format);
		vcmn_err(d < BMC_DEBUG_LEVEL_2 ? CE_WARN : CE_CONT, format, ap);
		va_end(ap);
	}
#endif
}

static boolean_t
bmc_command_requires_privilege(uint8_t command, uint8_t netFn)
{

	bmc_command_priv_level_t *command_listp;
	int i;

	/*
	 * BMC commands are grouped by function (netFn).
	 * The commands implemented within each function
	 * group are tabulated, together with their associated
	 * privilege level in the bmc_netfn* arrays.
	 *
	 * Currently two privilege levels are defined:
	 *    BMC_REQ_NORM permits global access to this command
	 *    BMC_REQ_PRIV permits privileged (sys_admin) access
	 *    to this command.
	 *
	 * bmc_command_requires_privilege() returns B_FALSE in the case
	 * that global access is permitted and B_TRUE in the case
	 * that sys_admin privileges are required.
	 *
	 * Future IPMI implementations may add further function
	 * groups and further commands to existing function groups.
	 * In the case that an unknown function group is specified,
	 * and in the case that an unknown command within an existing
	 * function group is specified, B_TRUE is returned.
	 */

	switch (netFn) {

	case BMC_NETFN_CHASSIS:
		command_listp = bmc_netfn_chassis;
		break;

	case BMC_NETFN_BRIDGE:
		command_listp = bmc_netfn_bridge;
		break;

	case BMC_NETFN_SE:
		command_listp = bmc_netfn_se;
		break;

	case BMC_NETFN_APP:
		command_listp = bmc_netfn_app;
		break;

	case BMC_NETFN_STORAGE:
		command_listp = bmc_netfn_storage;
		break;

	case BMC_NETFN_TRANSPORT:
		command_listp = bmc_netfn_transport;
		break;

	default:
		return (B_TRUE); /* Unknown function group */
	}

	for (i = 0; command_listp[i].req_level != BMC_END_OF_LIST; i++) {
		if (command_listp[i].command == command)
			return (command_listp[i].req_level == BMC_REQ_PRIV);
	}

	return (B_TRUE); /* Unknown command */
}

static mblk_t *
bmc_alloc_response_shell(void)
{
	mblk_t *mp;
	bmc_rsp_t *response;

	mp = allocb(BMC_MAX_RSP_MSGSZ, BPRI_MED);
	if (mp != NULL) {
		bzero(mp->b_rptr, BMC_MAX_RSP_MSGSZ);
		response = (bmc_rsp_t *)
		    &((bmc_msg_t *)(void *)mp->b_rptr)->msg[0];
		response->datalength = BMC_MAX_RESPONSE_PAYLOAD_SIZE();
	}

	return (mp);
}

/*
 * Process a message sent from the user.
 *
 * q passed in is the WRITE side.
 */
static void
bmc_process_msg(ipmi_state_t *ipmip, queue_t *q, mblk_t *mp)
{
	bmc_msg_t *msg, *respmsg;
	bmc_req_t *request;
	bmc_rsp_t *response;
	int msgsize;
	uint8_t err = 0;
	uint32_t mid = ((bmc_msg_t *)(void *)mp->b_rptr)->m_id;
	mblk_t *reqmp = NULL;
	mblk_t *respmp;
	bmc_kstat_t *ksp = &ipmip->bmc_kstats;

	ASSERT(mp->b_datap->db_type == M_DATA);

	dprintf(BMC_DEBUG_LEVEL_4, "bmc_process_msg  enter");

	dprintf(BMC_DEBUG_LEVEL_4, "mp = %p", (void *)mp);

	/* Allocate enough space for the largest response possible */
	if ((respmp = bmc_alloc_response_shell()) == NULL) {
		dprintf(BMC_DEBUG_LEVEL_4, "response allocation failure");
		err = ENOMEM;
		respmp = mp;
		mp = NULL;
		goto bmc_pm_err_return;
	}

	/* Construct contiguous message so we can access its fields below */
	if ((reqmp = msgpullup(mp, -1)) == NULL) {
		dprintf(BMC_DEBUG_LEVEL_4, "msgpullup failure");
		err = ENOMEM;
		goto bmc_pm_err_return;
	}

	msgsize = MBLKSIZE(reqmp);
	msg = (bmc_msg_t *)(void *)reqmp->b_rptr;
	/* Write the message ID and type into the reponse message here: */
	respmsg = (bmc_msg_t *)(void *)respmp->b_rptr;
	respmsg->m_id = mid;
	respmsg->m_type = BMC_MSG_RESPONSE;
	response = (bmc_rsp_t *)&respmsg->msg[0];

	switch (msg->m_type) {

	case BMC_MSG_REQUEST:
		/*
		 * Calculate the payload size (the size of the request
		 * structure embedded in the bmc_msg_t request) by subtracting
		 * the size of all members of the bmc_msg_t except for the
		 * msg field (which is overlayed with the bmc_req_t).
		 */
		msgsize -= offsetof(bmc_msg_t, msg);

		request = (bmc_req_t *)&msg->msg[0];

		/* Perform some sanity checks on the size of the message */
		if (msgsize < sizeof (bmc_req_t) || msgsize <
		    (offsetof(bmc_req_t, data) + request->datalength)) {
			dprintf(BMC_DEBUG_LEVEL_4, "Malformed message, msg "
			    " size=%lu, payload size=%d, expected size=%lu",
			    (ulong_t)msgdsize(reqmp), msgsize,
			    (ulong_t)((msgsize < sizeof (bmc_req_t)) ?
			    sizeof (bmc_req_t) :
			    (offsetof(bmc_req_t, data) +
			    request->datalength)));

			/* Fake a response from the BMC with an error code */
			response->fn = request->fn;
			response->lun = request->lun;
			response->cmd = request->cmd;
			response->ccode = BMC_IPMI_INVALID_COMMAND;
			response->datalength = 0;
			freemsg(reqmp);
			qreply(q, respmp);
			break;
		}

		/* Does the command number look OK? */
		if (request->cmd >= (BMC_NUM_CMDS - 1)) {
			/* Fake a response from the BMC with an error code */
			response->fn = request->fn;
			response->lun = request->lun;
			response->cmd = request->cmd;
			response->ccode = BMC_IPMI_INVALID_COMMAND;
			response->datalength = 0;
			freemsg(reqmp);
			qreply(q, respmp);
			break;
		}

		/*
		 * Command number's good.  Does the messages have a NULL
		 * cred attached to its first data block, or does this
		 * command require privileges the user doesn't have?
		 *
		 * (This implies that should any STREAMS modules be pushed
		 * between the stream head and this driver, it must preserve
		 * the cred added to the original message so that this driver
		 * can do the appropriate permissions checks).
		 */
		if ((DB_CRED(reqmp) == NULL) ||
		    (bmc_command_requires_privilege(request->cmd,
		    request->fn) && secpolicy_sys_config(DB_CRED(reqmp),
		    B_FALSE) != 0)) {
			err = EACCES;
			break;
		}

		dprintf(BMC_DEBUG_LEVEL_2,
		    "MSG  type 0x%x subcmd 0x%x req_len 0x%x",
		    msg->m_type, request->cmd, request->datalength);


		if (!bmc_dispatch(ipmip, BMC_TASK_IPMI, q,
		    reqmp, respmp, request, response)) {
			err = ENOMEM;
		}

		break;

	default:
		err = EINVAL;
		break;
	}

bmc_pm_err_return:
	if (err != 0) {
		/*
		 * If we got here we failed to dispatch the
		 * request to the Hardware because of some
		 * error. So we no longer need the request.
		 * All we need is the mid to send an error
		 * response.
		 */
		if (reqmp != NULL)
			freemsg(reqmp);

		if (err == ENOMEM)
			atomic_inc_64(&ksp->bmc_alloc_failures.value.ui64);

		/*
		 * Build an error reply message for the user
		 * using the original message id.
		 */
		bmc_inscribe_errmsg(respmp, mid, err);
		qreply(q, respmp);
	}
	if (mp != NULL)
		freemsg(mp);
}

static void
bmc_inscribe_errmsg(mblk_t *mp, uint32_t mid, uint8_t error)
{
	bmc_msg_t *msg;

	ASSERT(mp != NULL && MBLKSIZE(mp) >= sizeof (bmc_msg_t));

	/*
	 * Build the error message.
	 */
	mp->b_rptr = mp->b_wptr = DB_BASE(mp);
	msg = (bmc_msg_t *)(void *)mp->b_wptr;
	msg->m_type = BMC_MSG_ERROR;
	msg->m_id = mid;
	/* First byte of msg is the error code */
	msg->msg[0] = error;
	/* sizeof (bmc_msg_t) includes the one-byte error code */
	mp->b_wptr += sizeof (bmc_msg_t);
}

static mblk_t *
bmc_build_errmsg(uint32_t mid, uint8_t error)
{
	mblk_t *mp;

	mp = allocb(sizeof (bmc_msg_t), BPRI_MED);

	if (mp != NULL)
		bmc_inscribe_errmsg(mp, mid, error);

	return (mp);
}

static int
bmc_kstat_create(ipmi_state_t *ipmip, dev_info_t *dip)
{
	ipmip->ksp = kstat_create("bmc", ddi_get_instance(dip), "statistics",
	    "streams", KSTAT_TYPE_NAMED, sizeof (bmc_kstat_t) /
	    sizeof (kstat_named_t), KSTAT_FLAG_VIRTUAL);

	if (ipmip->ksp == NULL)
		return (DDI_FAILURE);

	ipmip->ksp->ks_data = &ipmip->bmc_kstats;

	ipmip->bmc_kstats.bmc_alloc_failures.value.ui64 = 0;
	kstat_named_init(&ipmip->bmc_kstats.bmc_alloc_failures,
	    "allocation failures", KSTAT_DATA_UINT64);

	ipmip->bmc_kstats.bmc_bytes_in.value.ui64 = 0;
	kstat_named_init(&ipmip->bmc_kstats.bmc_bytes_in,
	    "bytes received", KSTAT_DATA_UINT64);

	ipmip->bmc_kstats.bmc_bytes_out.value.ui64 = 0;
	kstat_named_init(&ipmip->bmc_kstats.bmc_bytes_out,
	    "bytes sent", KSTAT_DATA_UINT64);

	kstat_install(ipmip->ksp);

	return (DDI_SUCCESS);
}

static struct module_info bmc_minfo = {
	0xabcd,				/* module id number */
	"IPMI bmc driver",		/* module name */
	0,				/* min packet size */
	INFPSZ,				/* max packet size */
	1024,				/* hi water mark */
	512				/* low water mark */
};

static struct qinit bmc_rinit = {
	NULL,				/* put procedure */
	NULL,				/* service procedure */
	bmc_open,			/* open() procedure */
	bmc_close,			/* close() procedure */
	NULL,				/* reserved */
	&bmc_minfo,			/* module information pointer */
	NULL				/* module stats pointer */
};

static struct qinit bmc_winit = {
	bmc_wput,			/* put procedure */
	bmc_wsrv,			/* service procedure */
	NULL,				/* open() not used on write side */
	NULL,				/* close() not used on write side */
	NULL,				/* reserved */
	&bmc_minfo,			/* module information pointer */
	NULL				/* module state pointer */
};

struct streamtab bmc_str_info = {
	&bmc_rinit,
	&bmc_winit,
	NULL,
	NULL
};

DDI_DEFINE_STREAM_OPS(				\
	bmc_ops,				\
	nulldev,				\
	nulldev,				\
	bmc_attach,				\
	bmc_detach,				\
	nodev,					\
	bmc_getinfo,				\
	D_MP | D_NEW,				\
	&bmc_str_info,				\
	ddi_quiesce_not_supported		\
);

static struct modldrv modldrv = {
	&mod_driverops, "BMC 1.13", &bmc_ops
};

static struct modlinkage modlinkage = {
	MODREV_1, &modldrv, NULL
};

int
_init(void)
{
	int error;

	if ((error = ddi_soft_state_init(&ipmi_state,
	    sizeof (ipmi_state_t), 0)) != 0)
		return (error);

	if (bmc_nclones <= 0)
		bmc_nclones = maxusers;

	bmc_clones = kmem_zalloc(sizeof (bmc_clone_t) * bmc_nclones, KM_SLEEP);

	if ((error = mod_install(&modlinkage)) != 0) {
		ddi_soft_state_fini(&ipmi_state);
		kmem_free(bmc_clones, sizeof (bmc_clone_t) * bmc_nclones);
	}

	return (error);
}

int
_fini(void)
{
	int error;

	if ((error = mod_remove(&modlinkage)) == 0) {
		ddi_soft_state_fini(&ipmi_state);
		kmem_free(bmc_clones, sizeof (bmc_clone_t) * bmc_nclones);
	}

	return (error);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}
