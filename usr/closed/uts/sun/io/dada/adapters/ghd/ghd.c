/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 1996, 2011, Oracle and/or its affiliates. All rights reserved.
 */



#include <sys/types.h>
#include <sys/kmem.h>
#include <sys/debug.h>
#include <sys/note.h>
#include <sys/scsi/scsi.h>

#include <sys/dada/adapters/ghd/ghd.h>

/* ghd_poll() function codes: */
typedef enum {
	GHD_POLL_REQUEST,	/* wait for a specific request */
	GHD_POLL_DEVICE,	/* wait for a specific device to idle */
	GHD_POLL_ALL		/* wait for the whole bus to idle */
} gpoll_t;

/*
 * Local functions:
 */
static	gcmd_t	*ghd_doneq_get(ccc_t *cccp);
static	void	 ghd_doneq_pollmode_enter(ccc_t *cccp);
static	void	 ghd_doneq_pollmode_exit(ccc_t *cccp);
static	uint_t	ghd_doneq_process(caddr_t arg);

static	int	 ghd_poll(ccc_t *cccp, gpoll_t polltype, clock_t polltime,
				gcmd_t *poll_gcmdp, gtgt_t *gtgtp,
				void *intr_status);


/*
 * Local configuration variables
 */

#define	DEFAULT_GHD_TIMEOUT    50000    /* Amount of time to poll(50ms) */

clock_t ghd_tran_abort_timeout = DEFAULT_GHD_TIMEOUT;
clock_t ghd_tran_abort_lun_timeout = DEFAULT_GHD_TIMEOUT;
clock_t ghd_tran_reset_target_timeout = DEFAULT_GHD_TIMEOUT;
clock_t ghd_tran_reset_bus_timeout = DEFAULT_GHD_TIMEOUT;

/*
 * warlock directives
 */
_NOTE(READ_ONLY_DATA(ghd_tran_abort_timeout))
_NOTE(READ_ONLY_DATA(ghd_tran_abort_lun_timeout))
_NOTE(READ_ONLY_DATA(ghd_tran_reset_target_timeout))
_NOTE(READ_ONLY_DATA(ghd_tran_reset_bus_timeout))

_NOTE(SCHEME_PROTECTS_DATA("No Mutex Needed", tmr_t::t_ticks))
_NOTE(SCHEME_PROTECTS_DATA("No Mutex Needed", L1el L2el))
_NOTE(SCHEME_PROTECTS_DATA("No Mutex Needed", ghd_device::gd_waitq.Q_nactive))

static int
ghd_doneq_init(ccc_t *cccp)
{
	ddi_iblock_cookie_t iblock;

	L2_INIT(&cccp->ccc_doneq);
	cccp->ccc_hba_pollmode = TRUE;

	if (ddi_add_softintr(cccp->ccc_hba_dip, DDI_SOFTINT_LOW,
	    &cccp->ccc_doneq_softid, &iblock, NULL,
	    ghd_doneq_process, (caddr_t)cccp) != DDI_SUCCESS) {
		GDBG_ERROR(("ghd_doneq_init: add softintr failed cccp 0x%p\n",
		    (void *)cccp));
		return (FALSE);
	}

	mutex_init(&cccp->ccc_doneq_mutex, NULL, MUTEX_DRIVER, iblock);
	ghd_doneq_pollmode_exit(cccp);
	return (TRUE);
}

/*
 * ghd_complete():
 *
 *	The HBA driver calls this entry point when it's completely
 *	done processing a request.
 *
 *	See the GHD_COMPLETE_INLINE() macro in ghd.h for the actual code.
 */

void
ghd_complete(ccc_t *cccp, gcmd_t *gcmdp)
{
	GHD_COMPLETE_INLINE(cccp, gcmdp);
}

/*
 * ghd_async_complete():
 *
 */
void
ghd_async_complete(ccc_t *cccp, gcmd_t *gcmdp)
{
	mutex_enter(&cccp->ccc_hba_mutex);
	GHD_COMPLETE_INLINE(cccp, gcmdp);
	mutex_exit(&cccp->ccc_hba_mutex);
}


/*
 * ghd_doneq_put():
 *
 *	Mark the request done and append it to the doneq.
 *	See the GHD_DONEQ_PUT_INLINE() macro in ghd.h for
 *	the actual code.
 */
void
ghd_doneq_put(ccc_t *cccp, gcmd_t *gcmdp)
{
	GHD_DONEQ_PUT_INLINE(cccp, gcmdp)				\
}

static gcmd_t *
ghd_doneq_get(ccc_t *cccp)
{
	gcmd_t	 *gcmdp;

	mutex_enter(&cccp->ccc_doneq_mutex);
	if ((gcmdp = L2_next(&cccp->ccc_doneq)) != NULL)
		L2_delete(&gcmdp->cmd_q);
	mutex_exit(&cccp->ccc_doneq_mutex);
	return (gcmdp);
}


static void
ghd_doneq_pollmode_enter(ccc_t *cccp)
{
	mutex_enter(&cccp->ccc_doneq_mutex);
	cccp->ccc_hba_pollmode = TRUE;
	mutex_exit(&cccp->ccc_doneq_mutex);
}


static void
ghd_doneq_pollmode_exit(ccc_t *cccp)
{

	mutex_enter(&cccp->ccc_doneq_mutex);
	cccp->ccc_hba_pollmode = FALSE;
	mutex_exit(&cccp->ccc_doneq_mutex);

	/* trigger software interrupt for the completion callbacks */
	if (!L2_EMPTY(&cccp->ccc_doneq)) {
		/*
		 * If we are panicking we should just call the completion
		 * function directly as we can not use soft interrupts
		 * or timeouts during panic.
		 */
		if (!ddi_in_panic())
			ddi_trigger_softintr(cccp->ccc_doneq_softid);
		else
			(void) ghd_doneq_process((caddr_t)cccp);
	}
}


/* ***************************************************************** */

/*
 *
 * ghd_doneq_process()
 *
 *	This function is called directly from the software interrupt
 *	handler.
 *
 *	The doneq is protected by a separate mutex than the
 *	HBA mutex in order to avoid mutex contention on MP systems.
 *
 */

static uint_t
ghd_doneq_process(caddr_t arg)
{
	register ccc_t *cccp = (ccc_t *)arg;
	register gcmd_t		 *gcmdp;

	for (;;) {
		mutex_enter(&cccp->ccc_doneq_mutex);
		/* skip if FLAG_NOINTR request in progress */
		if (cccp->ccc_hba_pollmode)
			break;
		/* pop the first one from the done Q */
		if ((gcmdp = L2_next(&cccp->ccc_doneq)) == NULL)
			break;
		L2_delete(&gcmdp->cmd_q);

		/*
		 * drop the mutex since completion
		 * function can re-enter the top half via
		 * ghd_transport()
		 */
		mutex_exit(&cccp->ccc_doneq_mutex);
		gcmdp->cmd_state = GCMD_STATE_IDLE;
		(*cccp->ccc_hba_complete)(cccp->ccc_hba_handle, gcmdp, TRUE);
	}
	mutex_exit(&cccp->ccc_doneq_mutex);
	return (DDI_INTR_CLAIMED);
}

/* ***************************************************************** */


/*
 * ghd_register()
 *
 *	Do the usual interrupt handler setup stuff.
 *
 *	Also, set up three mutexes: the wait queue mutex, the HBA
 *	mutex, and the done queue mutex. The permitted locking
 *	orders are:
 *
 *		1. enter(waitq)
 *		2. enter(activel)
 *		3. enter(doneq)
 *		4. enter(HBA) then enter(activel)
 *		5. enter(HBA) then enter(doneq)
 *		6. enter(HBA) then enter(waitq)
 *		7. enter(waitq) then tryenter(HBA)
 *
 *	Note: cases 6 and 7 won't deadlock because case 7 is always
 *	mutex_tryenter() call.
 *
 */


/* ARGSUSED */
int
ghd_register(char	*labelp,
		ccc_t	*cccp,
		dev_info_t *dip,
		int	 inumber,
		void	*hba_handle,
		int	(*ccballoc)(gtgt_t *, gcmd_t *, int, int, int, int),
		void	(*ccbfree)(gcmd_t *),
		void	(*sg_func)(gcmd_t *, ddi_dma_cookie_t *, int, int),
		int	(*hba_start)(void *, gcmd_t *),
		void    (*hba_complete)(void *, gcmd_t *, int),
		uint_t	(*int_handler)(caddr_t),
		int	(*get_status)(void *, void *, int),
		void	(*process_intr)(void *, void *, int),
		int	(*timeout_func)(void *, gcmd_t *, gtgt_t *,
			    gact_t, int),
		tmr_t	*tmrp,
		ddi_iblock_cookie_t iblock,
		int	chno)
{

	cccp->ccc_label = labelp;
	cccp->ccc_hba_dip = dip;
	cccp->ccc_ccballoc = ccballoc;
	cccp->ccc_ccbfree = ccbfree;
	cccp->ccc_sg_func = sg_func;
	cccp->ccc_hba_start = hba_start;
	cccp->ccc_hba_complete = hba_complete;
	cccp->ccc_process_intr = process_intr;
	cccp->ccc_get_status = get_status;
	cccp->ccc_hba_handle = hba_handle;
	cccp->ccc_chno = chno;

	/* initialize the HBA's list headers */
	CCCP_INIT(cccp);

	mutex_init(&cccp->ccc_hba_mutex, NULL, MUTEX_DRIVER, iblock);

	mutex_init(&cccp->ccc_waitq_mutex, NULL, MUTEX_DRIVER, iblock);

	(void) ghd_timer_attach(cccp, tmrp, timeout_func);

	if (ghd_doneq_init(cccp)) {
		return (TRUE);
	}

	ghd_timer_detach(cccp);
	ddi_remove_intr(cccp->ccc_hba_dip, 0, cccp->ccc_iblock);
	mutex_destroy(&cccp->ccc_hba_mutex);
	mutex_destroy(&cccp->ccc_waitq_mutex);
	return (FALSE);

}


void
ghd_unregister(ccc_t *cccp)
{
	ghd_timer_detach(cccp);
	ddi_remove_intr(cccp->ccc_hba_dip, 0, cccp->ccc_iblock);
	ddi_remove_softintr(cccp->ccc_doneq_softid);
	mutex_destroy(&cccp->ccc_hba_mutex);
	mutex_destroy(&cccp->ccc_waitq_mutex);
	mutex_destroy(&cccp->ccc_doneq_mutex);
}



int
ghd_intr(ccc_t *cccp, void *intr_status, int chno)
{
	register int (*statfunc)(void *, void *, int) = cccp->ccc_get_status;
	register void (*processfunc)(void *, void *, int) =
	    cccp->ccc_process_intr;
	void		  *handle = cccp->ccc_hba_handle;
	int		   rc = DDI_INTR_UNCLAIMED;


	mutex_enter(&cccp->ccc_hba_mutex);
	if ((*statfunc)(handle, intr_status, chno)) {
		(*processfunc)(handle, intr_status, chno);
		rc = DDI_INTR_CLAIMED;
	}
	mutex_enter(&cccp->ccc_waitq_mutex);
	(void) ghd_waitq_process_and_mutex_hold(cccp);
	mutex_exit(&cccp->ccc_waitq_mutex);
	mutex_exit(&cccp->ccc_hba_mutex);
	return (rc);
}

static int
ghd_poll(ccc_t	*cccp,
	gpoll_t	 polltype,
	clock_t polltime,
	gcmd_t	*poll_gcmdp,
	gtgt_t	*gtgtp,
	void	*intr_status)
{
	gcmd_t	*gcmdp;
	L2el_t	 gcmd_hold_queue;
	int	 got_it = FALSE;
	clock_t  poll_lbolt;
	clock_t  start_lbolt;
	clock_t  current_lbolt;
	int	 chno;


	ASSERT(mutex_owned(&cccp->ccc_hba_mutex));

	chno = cccp->ccc_chno;
	L2_INIT(&gcmd_hold_queue);

	/* Que hora es? */
	poll_lbolt = drv_usectohz((clock_t)polltime);
	start_lbolt = ddi_get_lbolt();

	/* unqueue and save all CMD/CCBs until I find the right one */
	while (!got_it) {

		/* Give up yet? */
		current_lbolt = ddi_get_lbolt();
		if (poll_lbolt && (current_lbolt - start_lbolt >= poll_lbolt))
			break;

		/*
		 * delay 1 msec each time around the loop (this is an
		 * arbitrary delay value, any value should work) except
		 * zero because some devices don't like being polled too
		 * fast and it saturates the bus on an MP system.
		 */
		drv_usecwait(1000);

		/*
		 * check for any new device status
		 */
		if ((*cccp->ccc_get_status)(cccp->ccc_hba_handle,
		    intr_status, chno))
			(*cccp->ccc_process_intr)(cccp->ccc_hba_handle,
			    intr_status, chno);

		/*
		 * If something completed then try to start the
		 * next request from the wait queue. Don't release
		 * the HBA mutex because I don't know whether my
		 * request(s) is/are on the done queue yet.
		 */
		mutex_enter(&cccp->ccc_waitq_mutex);
		(void) ghd_waitq_process_and_mutex_hold(cccp);
		mutex_exit(&cccp->ccc_waitq_mutex);

		/*
		 * Process any timed-out requests.
		 */
		ghd_timer_poll(cccp);


		/*
		 * Unqueue all the completed requests, look for mine
		 */
		while (gcmdp = ghd_doneq_get(cccp)) {
			/*
			 * If we got one and it's my request, then
			 * we're done.
			 */
			if (gcmdp == poll_gcmdp) {
				poll_gcmdp->cmd_state = GCMD_STATE_IDLE;
				got_it = TRUE;
				continue;
			}
			/* fifo queue the other cmds on my local list */
			L2_add(&gcmd_hold_queue, &gcmdp->cmd_q, gcmdp);
		}


		/*
		 * Check whether we're done yet.
		 */
		switch (polltype) {
		case GHD_POLL_DEVICE:
			/*
			 * wait for everything queued on a specific device
			 */
			if (GDEV_NACTIVE(gtgtp->gt_gdevp) == 0)
				got_it = TRUE;
			break;

		case GHD_POLL_ALL:
			/*
			 * if waiting for all outstanding requests and
			 * if active list is now empty then exit
			 */
			mutex_enter(&cccp->ccc_waitq_mutex);
			if (GHBA_NACTIVE(cccp) == 0)
				got_it = TRUE;
			mutex_exit(&cccp->ccc_waitq_mutex);
			break;
		}
	}

	if (L2_EMPTY(&gcmd_hold_queue)) {
		ASSERT(!mutex_owned(&cccp->ccc_waitq_mutex));
		ASSERT(mutex_owned(&cccp->ccc_hba_mutex));
		return (got_it);
	}

	/*
	 * copy the local gcmd_hold_queue back to the doneq so
	 * that the order of completion callbacks is preserved
	 */
	while (gcmdp = L2_next(&gcmd_hold_queue)) {
		L2_delete(&gcmdp->cmd_q);
		GHD_DONEQ_PUT(cccp, gcmdp);
	}

	ASSERT(!mutex_owned(&cccp->ccc_waitq_mutex));
	ASSERT(mutex_owned(&cccp->ccc_hba_mutex));
	return (got_it);
}


/*
 * ghd_tran_abort()
 *
 *	Abort specific command on a target.
 *
 */

int
ghd_tran_abort(ccc_t	*cccp,
		gcmd_t	*gcmdp,
		gtgt_t	*gtgtp,
		void	*intr_status)
{
	gact_t	 action;
	int	 rc;

	/*
	 * call the driver's abort_cmd function
	 */

	mutex_enter(&cccp->ccc_hba_mutex);
	ghd_doneq_pollmode_enter(cccp);

	switch (gcmdp->cmd_state) {
	case GCMD_STATE_WAITQ:
		/* not yet started */
		action = GACTION_EARLY_ABORT;
		break;

	case GCMD_STATE_ACTIVE:
		/* in progress */
		action = GACTION_ABORT_CMD;
		break;

	default:
		/* everything else, probably already being aborted */
		rc = FALSE;
		goto exit;
	}

	/* stop the timer and remove it from the active list */
	GHD_TIMER_STOP(cccp, gcmdp);

	/* start a new timer and send out the abort command */
	ghd_timer_newstate(cccp, gcmdp, gtgtp, action, GHD_NEWSTATE_TGTREQ);

	/* wait for the abort to complete */
	if (rc = ghd_poll(cccp, GHD_POLL_REQUEST, ghd_tran_abort_timeout,
	    gcmdp, gtgtp, intr_status)) {
		gcmdp->cmd_state = GCMD_STATE_DONEQ;
		GHD_DONEQ_PUT(cccp, gcmdp);
	}

exit:
	ghd_doneq_pollmode_exit(cccp);

	mutex_enter(&cccp->ccc_waitq_mutex);
	(void) ghd_waitq_process_and_mutex_exit(cccp);

	return (rc);
}


/*
 * ghd_tran_abort_lun()
 *
 *	Abort all commands on a specific target.
 *
 */

int
ghd_tran_abort_lun(ccc_t	*cccp,
			gtgt_t	*gtgtp,
			void	*intr_status)
{
	int	 rc;

	/*
	 * call the HBA driver's abort_device function
	 */

	mutex_enter(&cccp->ccc_hba_mutex);
	ghd_doneq_pollmode_enter(cccp);

	/* send out the abort device request */
	ghd_timer_newstate(cccp, NULL, gtgtp, GACTION_ABORT_DEV,
	    GHD_NEWSTATE_TGTREQ);

	/* wait for the device to go idle */
	rc = ghd_poll(cccp, GHD_POLL_DEVICE, ghd_tran_abort_lun_timeout,
	    NULL, gtgtp, intr_status);

	ghd_doneq_pollmode_exit(cccp);

	mutex_enter(&cccp->ccc_waitq_mutex);
	(void) ghd_waitq_process_and_mutex_exit(cccp);

	return (rc);
}



/*
 * ghd_tran_reset_target()
 *
 *	reset the target device
 *
 *
 */

/* ARGSUSED */
int
ghd_tran_reset_target(ccc_t	*cccp,
			gtgt_t	*gtgtp,
			void	*intr_status)
{
	mutex_enter(&cccp->ccc_hba_mutex);
	ghd_doneq_pollmode_enter(cccp);

	/* send out the device reset request */
	ghd_timer_newstate(cccp, NULL, gtgtp, GACTION_RESET_TARGET,
	    GHD_NEWSTATE_TGTREQ);

	ghd_doneq_pollmode_exit(cccp);

	mutex_enter(&cccp->ccc_waitq_mutex);
	(void) ghd_waitq_process_and_mutex_exit(cccp);

	return (TRUE);
}



/*
 * ghd_tran_reset_bus()
 *
 *	reset the scsi bus
 *
 */

/* ARGSUSED */
int
ghd_tran_reset_bus(ccc_t	*cccp,
		gtgt_t	*gtgtp,
		void	*intr_status)
{
	mutex_enter(&cccp->ccc_hba_mutex);
	ghd_doneq_pollmode_enter(cccp);

	/* send out the bus reset request */
	ghd_timer_newstate(cccp, NULL, gtgtp, GACTION_RESET_BUS,
	    GHD_NEWSTATE_TGTREQ);

	ghd_doneq_pollmode_exit(cccp);

	mutex_enter(&cccp->ccc_waitq_mutex);
	(void) ghd_waitq_process_and_mutex_exit(cccp);

	return (TRUE);
}


int
ghd_transport(ccc_t	*cccp,
		gcmd_t	*gcmdp,
		gtgt_t	*gtgtp,
		uint32_t timeout,
		int	 polled,
		void	*intr_status)
{
	gdev_t	*gdevp = gtgtp->gt_gdevp;



	/*
	 * Have  added redundand code in if statement for polled to make
	 * warlock happy. Because though there is no locking problem still
	 * warlock was  giving error.
	 */
	if (polled) {
		/*
		 * Grab the HBA mutex so no other requests are started
		 * until after this one completes.
		 */
		mutex_enter(&cccp->ccc_hba_mutex);

		GDBG_START(("ghd_transport: polled"
		    " cccp 0x%p gdevp 0x%p gtgtp 0x%p gcmdp 0x%p\n",
		    (void *)cccp, (void *)gdevp, (void *)gtgtp, (void *)gcmdp));

		/*
		 * Lock the doneq so no other thread flushes the Q.
		 */
		ghd_doneq_pollmode_enter(cccp);
		/*
		 * add this request to the tail of the waitq
		 */
		gcmdp->cmd_waitq_level = 1;
		mutex_enter(&cccp->ccc_waitq_mutex);
		L2_add(&GDEV_QHEAD(gdevp), &gcmdp->cmd_q, gcmdp);

		/*
		 * Add request to the packet timer active list and start its
		 * abort timer.
		 */
		gcmdp->cmd_state = GCMD_STATE_WAITQ;
		ghd_timer_start(cccp, gcmdp, timeout);


		/*
		 * Check the device wait queue throttle and perhaps move
		 * some requests to the end of the HBA wait queue.
		 */
		ghd_waitq_shuffle_up(cccp, gdevp);
	} else {
		/* EMPTY */
		GDBG_START(("ghd_transport: non-polled"
		    " cccp 0x%p gdevp 0x%p gtgtp 0x%p gcmdp 0x%p\n",
		    (void *)cccp, (void *)gdevp, (void *)gtgtp, (void *)gcmdp));
		/*
		 * add this request to the tail of the waitq
		 */
		gcmdp->cmd_waitq_level = 1;
		mutex_enter(&cccp->ccc_waitq_mutex);
		L2_add(&GDEV_QHEAD(gdevp), &gcmdp->cmd_q, gcmdp);

		/*
		 * Add request to the packet timer active list and start its
		 * abort timer.
		 */
		gcmdp->cmd_state = GCMD_STATE_WAITQ;
		ghd_timer_start(cccp, gcmdp, timeout);


		/*
		 * Check the device wait queue throttle and perhaps move
		 * some requests to the end of the HBA wait queue.
		 */
		ghd_waitq_shuffle_up(cccp, gdevp);
		/*
		 * See if the HBA mutex is available but use the
		 * tryenter so I don't deadlock.
		 */
		if (!mutex_tryenter(&cccp->ccc_hba_mutex)) {
			/* The HBA mutex isn't available */
			GDBG_START(("ghd_transport: !mutex cccp 0x%p\n",
			    (void *)cccp));
			mutex_exit(&cccp->ccc_waitq_mutex);
			return (TRAN_ACCEPT);
		}
		GDBG_START(("ghd_transport: got mutex cccp 0x%p\n",
		    (void *)cccp));

		/*
		 * start as many requests as possible from the head
		 * of the HBA wait queue
		 */

		(void) ghd_waitq_process_and_mutex_exit(cccp);

		ASSERT(!mutex_owned(&cccp->ccc_waitq_mutex));

		return (TRAN_ACCEPT);
	}

	(void) ghd_waitq_process_and_mutex_hold(cccp);

	/*
	 * If polled mode (FLAG_NOINTR specified in scsi_pkt flags),
	 * then ghd_poll() waits until the request completes or times out
	 * before returning.
	 */

	mutex_exit(&cccp->ccc_waitq_mutex);
	(void) ghd_poll(cccp, GHD_POLL_REQUEST, 0, gcmdp, gtgtp,
	    intr_status);
	ghd_doneq_pollmode_exit(cccp);

	mutex_enter(&cccp->ccc_waitq_mutex);
	(void) ghd_waitq_process_and_mutex_exit(cccp);

	/* call HBA's completion function but don't do callback to target */
	(*cccp->ccc_hba_complete)(cccp->ccc_hba_handle, gcmdp, FALSE);

	GDBG_START(("ghd_transport: polled done cccp 0x%p\n", (void *)cccp));
	return (TRAN_ACCEPT);
}
