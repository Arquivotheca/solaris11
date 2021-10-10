/*
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * IPMI: Next Generation IPMI driver.
 */


#include <sys/types.h>
#include <sys/list.h>
#include <sys/note.h>
#include <sys/stat.h>
#include <sys/kstat.h>
#include <sys/devops.h>
#include <sys/dditypes.h>
#include <sys/modctl.h>
#include <sys/varargs.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/policy.h>
#include <sys/sysmacros.h>
#include <sys/smbios.h>
#include <sys/atomic.h>
#include <sys/conf.h>

#include "ipmi_sol_int.h"
#include "ipmi_cmd.h"

/*
 * Module linkage routines for the kernel
 */

/* opaque state structure head */
static void	*ipmi_sol_state_p;	/* One per plugin interface instance */
static int	ipmi_sol_instance;
static int	ipmi_sol_opened;	/* Overall number opened */
static int	ipmi_sol_attaching;
krwlock_t	ipmi_sol_inst_lock;

extern struct ipmi_plugin ipmi_kcs;
extern struct ipmi_plugin ipmi_vc;

/*
 * Note that this is only going to be here until we add dynamically loaded
 * backend plugins. Once that is done a call to ipmi_sol_piregister() will
 * take its place and will change the attach and detach code
 */
struct ipmi_plugin *ipmi_pi[] = {
#if !defined(__sparc)
	&ipmi_kcs,
#else
	&ipmi_vc,
#endif
	NULL
};

struct ipmi_wd_periodic ipmi_sol_wdog;
kmutex_t	ipmi_sol_wd_lock;

static dev_info_t *ipmi_dip;		/* Single DIP for driver */
int ipmi_watchd_timo;			/* Watchdog time-out time in secs. */
int ipmi_watchd_update;			/* Watchdog system update in secs. */
int ipmi_poll_time;			/* Async poll time in milliseconds */

static int ipmi_sol_attach(dev_info_t *, ddi_attach_cmd_t);
static int ipmi_sol_detach(dev_info_t *, ddi_detach_cmd_t);
static int ipmi_sol_quiesce(dev_info_t *);
static int ipmi_sol_resume(dev_info_t *dip);
static int ipmi_sol_suspend(dev_info_t *dip);
static void ipmi_kstat_delete(ipmi_state_t *statep);
static int ipmi_kstat_create(ipmi_state_t *statep);
static int ipmi_sol_findclone(dev_t dev, ipmi_state_t **statepp,
    ipmi_clone_t **clonepp, int rmit);
static void ipmi_sol_startpoll(ipmi_state_t *statep);
static void ipmi_sol_stoppoll(ipmi_state_t *statep);
boolean_t ipmi_command_requires_privilege(uint8_t cmnd, uint8_t netFn);



/*
 * When this gets called a bad thing happened. We are detaching but still
 * have clone resources allocated.
 */
void
ipmi_purge_clones(ipmi_state_t *statep)
{
	ipmi_clone_t		*clonep;

	rw_enter(&statep->is_clone_lock, RW_WRITER);
	while ((clonep = list_head(&statep->is_open_clones)) != NULL) {
		list_remove(&statep->is_open_clones, clonep);
		if (clonep->ic_bsd_dev) {
			list_destroy(
			    &clonep->ic_bsd_dev->ipmi_completed_requests);
			cv_destroy(&clonep->ic_bsd_dev->ipmi_close_cv);
			kmem_free(clonep->ic_bsd_dev,
			    sizeof (struct ipmi_device));
		}
		kmem_free(clonep, sizeof (ipmi_clone_t));
	}
	rw_exit(&statep->is_clone_lock);
}

/*
 * Detach a state instance. Note this assumes that the ipmi_sol_inst_lock
 * is locked for writing.
 */
static void
ipmi_sol_freestate(ipmi_state_t *statep)
{
	int			instance = statep->is_instance;

	ddi_remove_minor_node(statep->is_dip, statep->is_name);

	if (ipmi_poll_time)
		ipmi_sol_stoppoll(statep);

	(void) ipmi_detach(statep);

	/*
	 * Don't do this until after ipmi_detach() since
	 * that does transactions to the BMC to stop the
	 * watchdog. Once abort is set no more transactions!
	 */
	ipmi_kstat_delete(statep);
	statep->is_task_abort = B_TRUE;
	statep->is_pi->ipmi_pi_detach(statep);

	if (list_head(&statep->is_open_clones)) {
		/* This should never happen */
		cmn_err(CE_WARN, "%s: detaching while still open [%d] times",
		    statep->is_name, statep->is_bsd_softc.ipmi_opened);
		ipmi_purge_clones(statep);
	}

	mutex_destroy(&statep->is_bsd_softc.ipmi_lock);
	cv_destroy(&statep->is_bsd_softc.ipmi_request_added);
	cv_destroy(&statep->is_poll_cv);
	rw_destroy(&statep->is_clone_lock);
	list_destroy(&statep->is_bsd_softc.ipmi_pending_requests);
	list_destroy(&statep->is_open_clones);

	ddi_soft_state_free(ipmi_sol_state_p, instance);
}

/*
 * Fake startup routine, to keep BSD code happy
 */
/* ARGSUSED */
static int
ipmi_sol_fakestart(struct ipmi_softc *arg)
{
	return (0);
}

/*
 * Probe and attach all hardware interface plugins
 *
 * This function will return success if at least one interface
 * is found. Else return failure and overall driver attach will
 * fail.
 *
 * Note that we currently go through all interfaces in the ipmi_pi
 * table (this will change when we make plugins separate modules).
 * We call the plugins probe routine and if that finds an interface
 * we do the following:
 *
 *  1. We create a new instance of the ipmi driver (new minor number).
 *  2. We create a new node in /dev and/or /devices (ipmi, ipmi1, ipmi2...).
 *  3. Attach the plugin instance by calling its attach routine.
 */
static int
ipmi_sol_probeattach(dev_info_t *dip)
{
	ipmi_state_t		*statep;
	struct ipmi_plugin	**pip;
	char			tmp_str[MAX_STR_LEN];
	int			instance = 0;
	int			error;

	rw_enter(&ipmi_sol_inst_lock, RW_WRITER);
	ipmi_sol_attaching++;
	for (pip = &ipmi_pi[0]; *pip; pip++) {

		if (!(*pip)->ipmi_pi_probe(dip, ipmi_sol_instance,
		    (*pip)->ipmi_pi_intfinst))
			continue;

		if (ddi_soft_state_zalloc(ipmi_sol_state_p, ipmi_sol_instance)
		    != DDI_SUCCESS) {
			ipmi_sol_attaching--;
			rw_exit(&ipmi_sol_inst_lock);
			return (DDI_FAILURE);
		}

		if ((statep = ddi_get_soft_state(ipmi_sol_state_p,
		    ipmi_sol_instance)) == NULL) {
			ddi_soft_state_free(ipmi_sol_state_p,
			    ipmi_sol_instance);
			ipmi_sol_attaching--;
			rw_exit(&ipmi_sol_inst_lock);
			return (DDI_FAILURE);
		}

		/*
		 * Initialize soft state
		 */
		statep->is_dip = dip;
		statep->is_instance = ipmi_sol_instance;
		statep->is_bsd_softc.ipmi_opened = 0;
		statep->is_nextclone = 1; /* 0 is base dev node not clone */
		statep->is_pi = *pip;
		statep->is_bsd_softc.ipmi_pi_flags = (*pip)->ipmi_pi_flags;
		statep->is_bsd_softc.ipmi_detaching = 0;
		statep->is_bsd_softc.ipmi_startup = &ipmi_sol_fakestart;
		statep->is_bsd_softc.ipmi_dev = statep;
		statep->is_bsd_cdev.si_parent = (void *)statep;
		statep->is_bsd_softc.ipmi_enqueue_request =
		    ipmi_polled_enqueue_request;
		statep->is_pi_taskq = NULL;
		statep->is_suspended = B_FALSE;
		statep->is_task_abort = B_FALSE;
		statep->is_detaching = B_FALSE;
		statep->is_pi->ipmi_pi_getreq = ipmi_dequeue_request;
		statep->is_pi->ipmi_pi_putresp = ipmi_complete_request;
		statep->is_pi->ipmi_pi_taskinit = ipmi_pitask_init;
		statep->is_pi->ipmi_pi_taskexit = ipmi_pitask_exit;

		/*
		 * This was moved from ipmi_startup (which is called
		 * from ipmi_attach). It was moved since the backend
		 * plugins are alread started when these get
		 * initialized. So we moved it before the plugin call.
		 */
		mutex_init(&statep->is_bsd_softc.ipmi_lock, NULL, MUTEX_DRIVER,
		    NULL);
		cv_init(&statep->is_bsd_softc.ipmi_request_added, "ipmireq",
		    CV_DRIVER, NULL);
		cv_init(&statep->is_poll_cv, "ipmipoll",
		    CV_DRIVER, NULL);
		rw_init(&statep->is_clone_lock, NULL, RW_DRIVER, NULL);

		list_create(&statep->is_bsd_softc.ipmi_pending_requests,
		    sizeof (struct ipmi_request),
		    offsetof(struct ipmi_request, ir_link));
		list_create(&statep->is_open_clones,
		    sizeof (ipmi_clone_t),
		    offsetof(struct ipmi_clone, ic_link));

		bzero(tmp_str, MAX_STR_LEN);
		(void) sprintf(tmp_str, "ipmi%d", ipmi_sol_instance);
		bcopy(tmp_str, statep->is_name, MAX_STR_LEN);

		error = ipmi_kstat_create(statep);
		if (error != DDI_SUCCESS) {
			ipmi_sol_freestate(statep);
			continue;
		}

		error = (*pip)->ipmi_pi_attach(statep);
		if (error != DDI_SUCCESS) {
			ipmi_sol_freestate(statep);
			continue;
		}

		error = ipmi_attach(statep);
		if (error) {
			ipmi_sol_freestate(statep);
			continue;
		}

		/*
		 * Create the base device node. This is used to open the
		 * device from application land. It will get cloned. The
		 * minor number of this base device should only contain
		 * the plugin interface instance.
		 */
		error = ddi_create_minor_node(dip, tmp_str, S_IFCHR,
		    IPMI_MKMINOR(0, ipmi_sol_instance), DDI_PSEUDO, 0);
		if (error == DDI_FAILURE) {
			ipmi_sol_freestate(statep);
			continue;
		}

		if (!(statep->is_bsd_softc.ipmi_pi_flags & IPMIPI_DELYATT) &&
		    ipmi_poll_time)
			ipmi_sol_startpoll(statep);

		ipmi_sol_instance++;
		instance++;
	}
	ipmi_sol_attaching--;
	rw_exit(&ipmi_sol_inst_lock);

	if (instance)
		return (DDI_SUCCESS);
	return (DDI_FAILURE);
}

/*
 * Interface plugin detach - shutdown and detach all hardware interfaces.
 */
/* ARGSUSED */
static int
ipmi_sol_pidetach(dev_info_t *dip)
{
	ipmi_state_t		*statep;
	int			instance = 0;


	if (ipmi_sol_opened || ipmi_sol_attaching) {
		return (DDI_FAILURE);
	}

	rw_enter(&ipmi_sol_inst_lock, RW_WRITER);
	for (instance = 0; instance < ipmi_sol_instance; instance++) {
		statep = ddi_get_soft_state(ipmi_sol_state_p, instance);
		if (statep && statep->is_detaching != B_TRUE) {
			statep->is_detaching = B_TRUE;
			ipmi_sol_freestate(statep);
		}
	}
	rw_exit(&ipmi_sol_inst_lock);
	return (DDI_SUCCESS);
}

/*
 * Handle any awaiting event messages
 */
static void
ipmi_sol_events(ipmi_state_t *statep)
{
	struct	ipmi_request	*req;
	struct	ipmi_request	*reqcpy;
	ipmi_clone_t		*clonep;
	struct ipmi_softc	*sc;
	int			error;

	sc = &statep->is_bsd_softc;

	if (!(statep->is_pi->ipmi_pi_pollstatus(statep) & IPMIFL_ATTN))
		return;

	req = ipmi_alloc_driver_request(IPMI_ADDR(IPMI_APP_REQUEST, 0),
	    IPMI_GET_MSG_FLAGS, 0, 1);
	error = ipmi_submit_driver_request(sc, req, MAX_TIMEOUT);

	if (error || req->ir_compcode)
		goto done;
	if (!(req->ir_reply[0] & IPMI_MSG_AVAILABLE))
		goto done;

	ipmi_free_request(req);

	/*
	 * First see if we can get any messages
	 */
	req = ipmi_alloc_driver_request(IPMI_ADDR(IPMI_APP_REQUEST, 0),
	    IPMI_READ_EVENT_MSG_BUFFER, 0, 15);
	error = ipmi_submit_driver_request(sc, req, MAX_TIMEOUT);

	if (error || req->ir_compcode)
		goto done;

	/*
	 * copy the message we just got to any interested open instances
	 * of the driver.
	 */
	rw_enter(&statep->is_clone_lock, RW_READER);
	clonep = list_head(&statep->is_open_clones);
	while (clonep && statep->is_task_end == B_FALSE) {
		/* Interested? */
		if (clonep->ic_getevents && clonep->ic_bsd_dev) {
			/* Yes! */
			reqcpy = ipmi_alloc_request(clonep->ic_bsd_dev,
			    req->ir_msgid, req->ir_addr, req->ir_command,
			    0, req->ir_replybuflen);
			bcopy(&req->ir_reply[0], &reqcpy->ir_reply[0],
			    req->ir_replybuflen);
			reqcpy->ir_replylen = req->ir_replylen;
			reqcpy->ir_error = req->ir_error;
			reqcpy->ir_compcode = req->ir_compcode;
			/* most likely not needed but just to be consistant */
			reqcpy->ir_retrys.retries =
			    clonep->ic_retrys.retries;
			reqcpy->ir_retrys.retry_time_ms =
			    clonep->ic_retrys.retry_time_ms;

			sc = &clonep->ic_statep->is_bsd_softc;
			mutex_enter(&statep->is_bsd_softc.ipmi_lock);
			/*
			 * They are orphan phantom requests but will go through
			 * the ioctl IPMICTL_RECEIVE_MSG so we make these
			 * look like the user application made a related
			 * request. That way everything will work correctly.
			 */
			clonep->ic_bsd_dev->ipmi_requests++;
			ipmi_complete_request(sc, reqcpy);
			mutex_exit(&statep->is_bsd_softc.ipmi_lock);
		}
		clonep = list_next(&statep->is_open_clones, clonep);
	}
	rw_exit(&statep->is_clone_lock);

done:
	ipmi_free_request(req);
}

/*
 * Main asynchronous poll task
 */
static void
ipmi_sol_polltask(void *arg)
{
	ipmi_state_t		*statep = arg;
	hrtime_t		poll_rate;

	/* convert from milliseconds to microseconds */
	poll_rate = (hrtime_t)ipmi_poll_time * MICROSEC/MILLISEC;

	while (statep->is_task_end == B_FALSE) {

		if (statep->is_suspended == B_FALSE)
			ipmi_sol_events(statep);

		mutex_enter(&statep->is_bsd_softc.ipmi_lock);
		(void) cv_reltimedwait(&statep->is_poll_cv,
		    &statep->is_bsd_softc.ipmi_lock, drv_usectohz(poll_rate),
		    TR_CLOCK_TICK);
		mutex_exit(&statep->is_bsd_softc.ipmi_lock);
	}
}

/*
 * Try to enable async poll thread
 */
static void
ipmi_sol_startpoll(ipmi_state_t *statep)
{
	struct ipmi_request	*req;
	int			error;
	unsigned char		enables;
	struct ipmi_softc *sc = &statep->is_bsd_softc;
	char			tmp_str[MAX_STR_LEN];

	if (!ipmi_poll_time || statep->is_poll_taskq)
		return;

	req = ipmi_alloc_driver_request(IPMI_ADDR(IPMI_APP_REQUEST, 0),
	    IPMI_GET_GLOBAL_ENABLES, 0, 1);

	error = ipmi_submit_driver_request(sc, req, 0);
	if (error)
		goto done;

	enables = req->ir_reply[0];
	ipmi_free_request(req);

	req = ipmi_alloc_driver_request(IPMI_ADDR(IPMI_APP_REQUEST, 0),
	    IPMI_SET_GLOBAL_ENABLES, 1, 0);

	req->ir_request[0] = enables | IPMI_ENB_MSG_BUFF;

	error = ipmi_submit_driver_request(sc, req, 0);
	if (error)
		goto done;

	/* Setup event taskq. */
	(void) sprintf(tmp_str, "%s%d", "ipmipoll",
	    statep->is_instance);
	statep->is_poll_taskq = ddi_taskq_create(statep->is_dip, tmp_str,
	    1, TASKQ_DEFAULTPRI, 0);
	if (statep->is_poll_taskq == NULL)
		goto done;

	statep->is_task_end = B_FALSE;

	/* Dispatch the taskq */
	if (ddi_taskq_dispatch(statep->is_poll_taskq,  ipmi_sol_polltask,
	    statep, DDI_SLEEP) != DDI_SUCCESS) {
		ddi_taskq_destroy(statep->is_poll_taskq);
		statep->is_poll_taskq = NULL;
	}
done:
	ipmi_free_request(req);
}

/*
 * Disable async poll thread
 */
static void
ipmi_sol_stoppoll(ipmi_state_t *statep)
{
	if (statep->is_poll_taskq == NULL)
		return;

	statep->is_task_end = B_TRUE;

	mutex_enter(&statep->is_bsd_softc.ipmi_lock);
	cv_signal(&statep->is_poll_cv);
	mutex_exit(&statep->is_bsd_softc.ipmi_lock);

	ddi_taskq_wait(statep->is_poll_taskq);
	ddi_taskq_destroy(statep->is_poll_taskq);
	statep->is_poll_taskq = NULL;
}

/*
 * Enable Solaris kstates for an instance of this driver
 */
static int
ipmi_kstat_create(ipmi_state_t *statep)
{
	statep->is_ksp = kstat_create(IPMI_NODENAME, statep->is_instance,
	    "statistics", "controller", KSTAT_TYPE_NAMED,
	    sizeof (ipmi_kstat_t) / sizeof (kstat_named_t),
	    KSTAT_FLAG_VIRTUAL);

	if (statep->is_ksp == NULL)
		return (DDI_FAILURE);

	statep->is_ksp->ks_data = &statep->is_kstats;

	statep->is_kstats.ipmi_alloc_failures.value.ui64 = 0;
	kstat_named_init(&statep->is_kstats.ipmi_alloc_failures,
	    "allocation failures", KSTAT_DATA_UINT64);

	statep->is_kstats.ipmi_bytes_in.value.ui64 = 0;
	kstat_named_init(&statep->is_kstats.ipmi_bytes_in,
	    "bytes received", KSTAT_DATA_UINT64);

	statep->is_kstats.ipmi_bytes_out.value.ui64 = 0;
	kstat_named_init(&statep->is_kstats.ipmi_bytes_out,
	    "bytes sent", KSTAT_DATA_UINT64);

	kstat_install(statep->is_ksp);

	return (DDI_SUCCESS);
}

static void
ipmi_kstat_delete(ipmi_state_t *statep)
{
	if (statep->is_ksp) {
		kstat_delete(statep->is_ksp);
		statep->is_ksp = NULL;
	}
}

static int
ipmi_sol_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	int		driver_enabled;

	switch (cmd) {
	case DDI_ATTACH:
		ipmi_dip = dip;	/* Only one dip for this driver */

		driver_enabled = ddi_prop_get_int(DDI_DEV_T_ANY,
		    dip, 0, "ipmi-enable", 0);
		if (!driver_enabled) {
			return (DDI_FAILURE);
		}

		ipmi_watchd_timo = ddi_prop_get_int(DDI_DEV_T_ANY,
		    dip, 0, "ipmi-wdtime", IPMI_WDTIME);
		ipmi_watchd_update = ddi_prop_get_int(DDI_DEV_T_ANY,
		    dip, 0, "ipmi-wdupdate", IPMI_WDUPDATE);
		ipmi_poll_time = ddi_prop_get_int(DDI_DEV_T_ANY,
		    dip, 0, "ipmi-polltime", IPMI_POLLTIME);

		/*
		 * Probe for all kinds of possible hardware
		 * and attach all found instances.
		 */
		if (ipmi_sol_probeattach(dip) != DDI_SUCCESS) {
			(void) ipmi_sol_pidetach(dip);
			ipmi_dip = NULL;
			return (DDI_FAILURE);
		}

		ddi_report_dev(dip);

		return (DDI_SUCCESS);

	case DDI_RESUME:
		return (ipmi_sol_resume(dip));

	default:
		return (DDI_FAILURE);
	}

error:
	return (DDI_FAILURE);
}

static int
ipmi_sol_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	int		retval;

	switch (cmd) {
	case DDI_DETACH:
		retval = ipmi_sol_pidetach(dip);
		if (retval == DDI_SUCCESS)
			ipmi_dip = NULL;
		break;
	case DDI_SUSPEND:
		retval = ipmi_sol_suspend(dip);
		break;
	default:
		retval = DDI_FAILURE;
	}
	return (retval);
}

/*
 * quiesce(9E) entry point.
 *
 * This function is called when the system is single-threaded at high
 * PIL with preemption disabled. Therefore, this function must not be
 * blocked.
 *
 * This function returns DDI_SUCCESS on success, or DDI_FAILURE on failure.
 * DDI_FAILURE indicates an error condition and should almost never happen.
 */
/* ARGSUSED */
static int
ipmi_sol_quiesce(dev_info_t *dip)
{
	ipmi_state_t		*statep;
	int			instance;

	extern int ipmi_set_watchdog(struct ipmi_softc *, uint32_t);

	rw_enter(&ipmi_sol_inst_lock, RW_READER);
	for (instance = 0; instance < ipmi_sol_instance; instance++) {
		statep = ddi_get_soft_state(ipmi_sol_state_p, instance);
		if (statep) {
			/*
			 * This will stop new user-land requests and
			 * the watchdog timer updates.
			 */
			statep->is_suspended = B_TRUE;

			/*
			 * Since we stop updates, stop the timer to prevent
			 * cold resets from the timer.
			 */
			(void) ipmi_set_watchdog(&statep->is_bsd_softc, 0);
		}
	}
	rw_exit(&ipmi_sol_inst_lock);

	return (DDI_SUCCESS);
}

/*
 * ipmi_resume()
 */
/* ARGSUSED */
static int
ipmi_sol_resume(dev_info_t *dip)
{
	ipmi_state_t		*statep;
	int			instance;

	rw_enter(&ipmi_sol_inst_lock, RW_READER);
	for (instance = 0; instance < ipmi_sol_instance; instance++) {
		statep = ddi_get_soft_state(ipmi_sol_state_p, instance);
		if (statep) {
			mutex_enter(&statep->is_bsd_softc.ipmi_lock);
			statep->is_pi->ipmi_pi_resume(statep);

			/*
			 * This will start new user-land requests and
			 * the watchdog timer updates will start soon.
			 */
			statep->is_suspended = B_FALSE;
			mutex_exit(&statep->is_bsd_softc.ipmi_lock);
		}
	}
	rw_exit(&ipmi_sol_inst_lock);

	return (DDI_SUCCESS);
}	/* ipmi_resume */

/*
 * ipmi_suspend()
 */
/* ARGSUSED */
static int
ipmi_sol_suspend(dev_info_t *dip)
{
	ipmi_state_t		*statep;
	int			instance = 0;

	extern int ipmi_set_watchdog(struct ipmi_softc *, uint32_t);

	rw_enter(&ipmi_sol_inst_lock, RW_READER);
	for (instance = 0; instance < ipmi_sol_instance; instance++) {
		if ((statep = ddi_get_soft_state(ipmi_sol_state_p, instance))) {
			/*
			 * This will stop new user-land requests and
			 * the watchdog timer updates.
			 */
			statep->is_suspended = B_TRUE;

			/*
			 * Shutdown the BMC watchdog so it does not reset
			 * the system during suspend.
			 */
			(void) ipmi_set_watchdog(&statep->is_bsd_softc, 0);

			mutex_enter(&statep->is_bsd_softc.ipmi_lock);
			statep->is_pi->ipmi_pi_suspend(statep);
			mutex_exit(&statep->is_bsd_softc.ipmi_lock);
		}

	}
	rw_exit(&ipmi_sol_inst_lock);

	return (DDI_SUCCESS);
}	/* ipmi_suspend */

/*
 * Find a clone based on a dev_t
 */
static int
ipmi_sol_findclone(dev_t dev, ipmi_state_t **statepp, ipmi_clone_t **clonepp,
    int rmit)
{
	ipmi_clone_t		*clonep;
	ipmi_state_t		*statep;
	int			instance;

	instance = (int)IPMI_DEV2INTF(getminor(dev));

	rw_enter(&ipmi_sol_inst_lock, RW_READER);
	if ((statep = ddi_get_soft_state(ipmi_sol_state_p, instance)) == NULL) {
		rw_exit(&ipmi_sol_inst_lock);
		return (0);
	}
	rw_exit(&ipmi_sol_inst_lock);

	instance = (int)IPMI_DEV2OINST(getminor(dev));

	if (rmit)
		rw_enter(&statep->is_clone_lock, RW_WRITER);
	else
		rw_enter(&statep->is_clone_lock, RW_READER);
	clonep = list_head(&statep->is_open_clones);
	while (clonep) {
		if (clonep->ic_instance == instance) {
			if (rmit) {
				list_remove(&statep->is_open_clones, clonep);
				statep->is_bsd_softc.ipmi_opened--;
				ipmi_sol_opened--;
				rw_exit(&statep->is_clone_lock);
				kmem_free(clonep, sizeof (ipmi_clone_t));
				return (1);
			}
			if (statepp)
				*statepp = statep;
			if (clonepp)
				*clonepp = clonep;
			rw_exit(&statep->is_clone_lock);
			return (1);
		}
		clonep = list_next(&statep->is_open_clones, clonep);
	}
	rw_exit(&statep->is_clone_lock);
	return (0);
}

/* ARGSUSED */
static int
ipmi_sol_open(dev_t *devp, int flag, int state, cred_t *cred)
{
	ipmi_state_t		*statep;
	ipmi_clone_t		*clonep;
	int			interface;

	if (state != OTYP_CHR)
		return (ENXIO);

	interface = (int)IPMI_DEV2INTF(getminor(*devp));

	rw_enter(&ipmi_sol_inst_lock, RW_READER);
	statep = ddi_get_soft_state(ipmi_sol_state_p, interface);
	if (statep == NULL) {
		rw_exit(&ipmi_sol_inst_lock);
		return (ENXIO);
	}
	if (statep->is_detaching == B_TRUE || ipmi_sol_attaching) {
		rw_exit(&ipmi_sol_inst_lock);
		return (ENXIO);
	}
	rw_exit(&ipmi_sol_inst_lock);

	if (statep->is_bsd_softc.ipmi_pi_flags & IPMIPI_DELYATT) {
		if (ipmi_poll_time && !(statep->is_bsd_softc.ipmi_pi_flags &
		    IPMIPI_NOASYNC))
			ipmi_sol_startpoll(statep);
		if (!ipmi_startup_io(&statep->is_bsd_softc)) {
			return (ENXIO);
		}
	}

	mutex_enter(&statep->is_bsd_softc.ipmi_lock);
	if (statep->is_bsd_softc.ipmi_opened >= IPMI_OINSTMAX) {
		cmn_err(CE_WARN, "open: too many opens %d",
		    statep->is_bsd_softc.ipmi_opened);
		mutex_exit(&statep->is_bsd_softc.ipmi_lock);
		return (EAGAIN);
	}

	/*
	 * We know the device is real, so add a clone to it.
	 */
	clonep = kmem_zalloc(sizeof (ipmi_clone_t), KM_SLEEP);

	/* Setup clone and its to state linkages */
	clonep->ic_statep = statep;
	clonep->ic_retrys.retries = DEFAULT_MSG_RETRY;
	clonep->ic_retrys.retry_time_ms = DEFAULT_MSG_TIMEOUT;

	/*
	 * Now we commit to open by calling the BSD driver open code.
	 */
	if (statep->is_bsd_cdev.si_devsw->d_open(clonep, flag, 0, NULL) != 0) {
		if (clonep->ic_bsd_dev) {
			list_destroy(
			    &clonep->ic_bsd_dev->ipmi_completed_requests);
			cv_destroy(&clonep->ic_bsd_dev->ipmi_close_cv);
			kmem_free(clonep->ic_bsd_dev,
			    sizeof (struct ipmi_device));
		}
		mutex_exit(&statep->is_bsd_softc.ipmi_lock);
		kmem_free(clonep, sizeof (ipmi_clone_t));
		return (EIO);
	}

	/* Create new minor number, Find an unused minor number */
	do {
		statep->is_nextclone++;
		if (statep->is_nextclone > IPMI_OINSTMAX) /* 8190 of them */
			statep->is_nextclone = 1;   /* 0 is base real dev */
		clonep->ic_instance = statep->is_nextclone;
		clonep->ic_mindev = makedevice(getemajor(*devp),
		    IPMI_MKMINOR(clonep->ic_instance, interface));
	} while (ipmi_sol_findclone(clonep->ic_mindev, NULL, NULL, 0));

	/* Put it on the instances clone list */
	rw_enter(&statep->is_clone_lock, RW_WRITER);
	/* Counts as open now */
	statep->is_bsd_softc.ipmi_opened++;
	list_insert_tail(&statep->is_open_clones, clonep);
	ipmi_sol_opened++;
	rw_exit(&statep->is_clone_lock);

	/* Tell the system the correct minor number */
	*devp = clonep->ic_mindev;

	mutex_exit(&statep->is_bsd_softc.ipmi_lock);

	return (0);
}

/* ARGSUSED */
static int
ipmi_sol_close(dev_t dev, int flag, int state, cred_t *cred)
{
	ipmi_state_t		*statep;
	ipmi_clone_t		*clonep;
	struct ipmi_device	*devp;

	if (state != OTYP_CHR)
		return (ENXIO);

	if (!ipmi_sol_findclone(dev, &statep, &clonep, 0)) {
		cmn_err(CE_WARN, "close: failed to find dev %x", (int)dev);
		return (ENXIO);
	}
	devp = clonep->ic_bsd_dev;
	clonep->ic_bsd_dev = NULL;
	statep->is_bsd_cdev.si_close(devp);
	(void) ipmi_sol_findclone(dev, NULL, NULL, 1);
	return (0);
}

/* ARGSUSED */
static int
ipmi_sol_ioctl(dev_t dev, int cmd, intptr_t arg, int flag,
    cred_t *cred, int *rvalp)
{
	ipmi_state_t		*statep;
	ipmi_clone_t		*clonep;
	int			error = 0;

	if (!ipmi_sol_findclone(dev, &statep, &clonep, 0)) {
		return (ENXIO);
	}
	if (statep->is_suspended == B_TRUE || statep->is_detaching == B_TRUE) {
		return (EAGAIN);
	}
	error = statep->is_bsd_cdev.si_devsw->d_ioctl(clonep, cmd, arg, flag,
	    cred);

	return (error);
}



static int
ipmi_sol_poll(dev_t dev, short events, int anyyet, short *reventsp,
    struct pollhead **phpp)
{
	ipmi_clone_t		*clonep;
	ipmi_state_t		*statep;

	if (!ipmi_sol_findclone(dev, &statep, &clonep, 0)) {
		return (ENXIO);
	}
	if ((events & (POLLIN | POLLRDNORM)) &&
	    list_head(&clonep->ic_bsd_dev->ipmi_completed_requests)) {
		*reventsp |= (POLLIN | POLLRDNORM);
	} else {
		*reventsp = 0;
		if (!anyyet) {
			*phpp = &clonep->ic_bsd_dev->ipmi_select;
		}
	}
	return (0);
}

/* ARGSUSED */
static int
ipmi_sol_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	dev_t dev;
	int error;

	switch (infocmd) {
	case DDI_INFO_DEVT2INSTANCE:
		/* The "instance" number is the minor number */
		dev = (dev_t)arg;
		*result = (void *)(unsigned long)IPMI_DEV2INTF(getminor(dev));
		error = DDI_SUCCESS;
		break;
	case DDI_INFO_DEVT2DEVINFO:
		*result = (void *)ipmi_dip;
		error = DDI_SUCCESS;
		break;
	default:
		error = DDI_FAILURE;
	}
	return (error);
}

boolean_t
ipmi_command_requires_privilege(uint8_t cmnd, uint8_t netFn)
{

	ipmi_command_priv_level_t *command_listp;
	int i;

	/*
	 * IPMI commands are grouped by function (netFn).
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

	case IPMI_NETFN_CHASSIS:
		command_listp = ipmi_netfn_chassis;
		break;

	case IPMI_NETFN_BRIDGE:
		command_listp = ipmi_netfn_bridge;
		break;

	case IPMI_NETFN_SE:
		command_listp = ipmi_netfn_se;
		break;

	case IPMI_NETFN_APP:
		command_listp = ipmi_netfn_app;
		break;

	case IPMI_NETFN_STORAGE:
		command_listp = ipmi_netfn_storage;
		break;

	case IPMI_NETFN_TRANSPORT:
		command_listp = ipmi_netfn_transport;
		break;
	/* XXX - BSH We need a case for both firmware and transport here */
	default:
		return (B_TRUE); /* Unknown function group */
	}

	for (i = 0; command_listp[i].req_level != IPMI_END_OF_LIST; i++) {
		if (command_listp[i].command == cmnd)
			return (command_listp[i].req_level == IPMI_REQ_PRIV);
	}

	return (B_TRUE); /* Unknown command */
}


struct ipmi_softc *
ipmi_get_softc(ipmi_state_t *dev)
{
	return (&dev->is_bsd_softc);
}

int
ipmi_device_get_unit(ipmi_state_t *statep)
{
	return (statep->is_instance);
}

struct cdev *
ipmi_make_dev(struct cdevsw *cdevsw, int instance)
{
	ipmi_state_t		*statep;

	/*
	 * Note this assumes that the read/write lock ipmi_sol_inst_lock
	 *  is held for writes
	 */
	if ((statep = ddi_get_soft_state(ipmi_sol_state_p, instance)) == NULL) {
		return (NULL);
	}

	statep->is_bsd_cdev.si_devsw = cdevsw;
	statep->is_bsd_cdev.si_parent = (void *)statep;
	statep->is_bsd_softc.ipmi_dev = statep;
	return (&statep->is_bsd_cdev);
}

/* ARGSUSED */
void
ipmi_destroy_dev(struct cdev *cdev)
{
}

int
ipmi_set_cdevpriv(ipmi_clone_t *clonep, void *arg, void (*closefunc)(void *))
{
	struct ipmi_device	*dev = (struct ipmi_device *)arg;

	clonep->ic_bsd_dev = dev;
	clonep->ic_statep->is_bsd_cdev.si_close = closefunc;
	return (0);
}

int
ipmi_get_cdevpriv(ipmi_clone_t *clonep, void **dev)
{
	*dev = clonep->ic_bsd_dev;
	/* Note if this is NUll it means we are no longer open */
	/* and this will prevent normal IO */
	if (clonep->ic_bsd_dev == NULL)
		return (ENOENT);
	return (0);
}

void
ipmi_cv_init(kcondvar_t *cv, char *name)
{
	cv_init(cv, name, CV_DRIVER, NULL);
}

static void
ipmi_tick(void *arg)
{
	struct ipmi_wd_periodic *wdp = arg;
	struct ipmi_softc	*sc = wdp->wd_arg;
	ipmi_state_t		*statep = sc->ipmi_dev;
	int			error;

	if (statep->is_suspended == B_FALSE)
		wdp->wd_callfunc(wdp->wd_arg, wdp->wd_timeo, &error);
}

eventhandler_tag
ipmi_eventhandler_register(void (*watchfunc)(void *, uint32_t, int *),
    void *arg)
{
	struct ipmi_wd_periodic *wdp;
	hrtime_t		wd_rate;

	wdp = kmem_zalloc(sizeof (struct ipmi_wd_periodic), KM_SLEEP);

	wdp->wd_next = NULL;
	wdp->wd_callfunc = watchfunc;
	wdp->wd_arg = arg;
	wdp->wd_timeo = ipmi_watchd_timo;

	mutex_enter(&ipmi_sol_wd_lock);
	wdp->wd_next = ipmi_sol_wdog.wd_next;
	ipmi_sol_wdog.wd_next = wdp;
	wd_rate = (hrtime_t)ipmi_watchd_update * NANOSEC;
	mutex_exit(&ipmi_sol_wd_lock);
	wdp->wd_periodic = ddi_periodic_add(ipmi_tick, wdp, wd_rate, 0);
	return (wdp);
}

void
ipmi_eventhandler_deregister(eventhandler_tag tag)
{
	struct ipmi_wd_periodic *wdp_targ = tag;
	struct ipmi_wd_periodic *wdp;

	mutex_enter(&ipmi_sol_wd_lock);
	for (wdp = &ipmi_sol_wdog; wdp; wdp = wdp->wd_next) {
		if (wdp->wd_next == wdp_targ) {
			wdp->wd_next = wdp_targ->wd_next;
			mutex_exit(&ipmi_sol_wd_lock);
			if (wdp_targ->wd_periodic)
				ddi_periodic_delete(wdp_targ->wd_periodic);
			kmem_free(wdp_targ, sizeof (struct ipmi_wd_periodic));
			return;
		}
	}
	mutex_exit(&ipmi_sol_wd_lock);
}

int
ipmi_intr_establish(struct intr_config_hook *hook)
{
	/*
	 * Startup the this instance of the BSD driver
	 * After this we can take requests - this happens at
	 * the end of the BSD drivers attach code
	 */
	hook->ich_func(hook->ich_arg);

	return (0);
}

int
ipmi_pitask_init(ipmi_state_t *statep, void (*taskfunc)(void *), char *tname)
{

	/* Setup plugin taskq. */
	statep->is_pi_taskq = ddi_taskq_create(statep->is_dip, tname,
	    1, TASKQ_DEFAULTPRI, 0);
	if (statep->is_pi_taskq == NULL) {
		return (DDI_FAILURE);
	}
	statep->is_bsd_softc.ipmi_detaching = 0;

	/* Dispatch the taskq */
	if (ddi_taskq_dispatch(statep->is_pi_taskq,  taskfunc, statep,
	    DDI_SLEEP) != DDI_SUCCESS) {
		ddi_taskq_destroy(statep->is_pi_taskq);
		statep->is_pi_taskq = NULL;
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

void
ipmi_pitask_exit(ipmi_state_t *statep)
{
	if (statep->is_pi_taskq) {
		statep->is_bsd_softc.ipmi_detaching = 1;
		cv_signal(&statep->is_bsd_softc.ipmi_request_added);
		ddi_taskq_wait(statep->is_pi_taskq);
		ddi_taskq_destroy(statep->is_pi_taskq);
		statep->is_pi_taskq = NULL;
	}
}

void
ipmi_device_printf(void *arg, const char *fmt, ...)
{
	ipmi_state_t *statep = (ipmi_state_t *)arg;
	va_list ap;
	auto char buf[256];

	if (fmt == NULL)
		return;

	va_start(ap, fmt);
	(void) vsprintf(buf, fmt, ap);
	va_end(ap);
	if (statep)
		cmn_err(CE_NOTE, "%s: %s", statep->is_name, buf);
	else
		cmn_err(CE_NOTE, "IPMI: %s", buf);
}


int
ipmi_msleep(kcondvar_t *sleep_cv, kmutex_t *sleep_lock, int *flag, int tick)
{
	clock_t cvret = 0;

	ASSERT(MUTEX_HELD(sleep_lock));

	while (*flag && cvret >= 0) {
		if (tick <= 0) {
			cv_wait(sleep_cv, sleep_lock);
			cvret = 1;
		} else
			cvret = cv_reltimedwait(sleep_cv, sleep_lock, tick,
			    TR_CLOCK_TICK);
	}

	if (cvret > 0)
		return (0);

	return (-1);
}

/*ARGSUSED*/
void
ipmi_wakeup(kcondvar_t *sleep_cv, kmutex_t *sleep_lock, int *flag)
{
	ASSERT(MUTEX_HELD(sleep_lock));

	*flag = 0;
	cv_signal(sleep_cv);
}

/*
 * Solaris Driver & module interfaces
 */

struct cb_ops	ipmi_sol_cb_ops = {
	ipmi_sol_open,		/* open */
	ipmi_sol_close,		/* close */
	nodev,			/* strategy */
	nodev,			/* print */
	nodev,			/* dump */
	nodev,			/* read */
	nodev,			/* write */
	ipmi_sol_ioctl,		/* ioctl */
	nodev,			/* devmap */
	nodev,			/* mmap */
	nodev, 			/* segmap */
	ipmi_sol_poll,		/* poll */
	ddi_prop_op,		/* cb_prop_op */
	NULL,			/* streamtab  */
	D_MP,			/* Driver compatibility flag */
	CB_REV
};

static struct dev_ops ipmi_sol_dev_ops = {
	DEVO_REV,		/* rev */
	0,			/* refcnt */
	ipmi_sol_info,		/* getinfo */
	nulldev,		/* identify */
	nulldev,		/* probe */
	ipmi_sol_attach,	/* attach */
	ipmi_sol_detach,	/* detach */
	nodev,			/* reset */
	&ipmi_sol_cb_ops,	/* driver operations  */
	NULL,			/* bus operations */
	NULL,			/* power */
	ipmi_sol_quiesce,	/* quiesce */
};

static struct modldrv ipmi_sol_modldrv = {
	&mod_driverops,			/* drv_modops */
	"IPMI",				/* linkinfo */
	&ipmi_sol_dev_ops,		/* dev_ops */
};

static struct modlinkage modlinkage = {
	MODREV_1,
	{ &ipmi_sol_modldrv, NULL }
};

int
_init(void)
{
	int err;

	rw_init(&ipmi_sol_inst_lock, NULL, RW_DRIVER, NULL);
	mutex_init(&ipmi_sol_wd_lock, NULL, MUTEX_DRIVER, NULL);

	err = ddi_soft_state_init(&ipmi_sol_state_p, sizeof (ipmi_state_t), 0);
	if (err != DDI_SUCCESS) {
		return (err);
	}

	if (err = mod_install(&modlinkage)) {
		ddi_soft_state_fini(&ipmi_sol_state_p);
	}

	return (err);
}

int
_fini(void)
{
	int err;

	if (ipmi_sol_attaching)
		return (DDI_FAILURE);

	if ((err = mod_remove(&modlinkage)) != 0) {
		return (err);
	}

	rw_destroy(&ipmi_sol_inst_lock);
	mutex_destroy(&ipmi_sol_wd_lock);

	ddi_soft_state_fini(&ipmi_sol_state_p);

	return (err);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}
