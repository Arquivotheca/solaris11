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
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/ksynch.h>
#include <sys/note.h>
#include <sys/scsi/conf/autoconf.h>

#include <sys/dada/adapters/ghd/ghd.h>
/*
 * Local functions
 */

static	gcmd_t	*ghd_timeout_get(ccc_t *cccp);
static	int	 ghd_timeout_loop(ccc_t *cccp);
static	uint_t	ghd_timeout_softintr(caddr_t arg);
static	void	 ghd_timeout(void *arg);
static	void	 ghd_timeout_disable(tmr_t *tmrp);
static	void	 ghd_timeout_enable(tmr_t *tmrp);

/*
 * Local data
 */
clock_t	ghd_HZ;

/* table of timeouts for abort processing steps */
cmdstate_t ghd_timeout_table[GCMD_NSTATES];

/* This table indirectly initializes the ghd_timeout_table */
struct {
	int		valid;
	cmdstate_t	state;
	clock_t		value;
} ghd_time_inits[] = {
	{ TRUE, GCMD_STATE_ABORTING_CMD, 3 },
	{ TRUE, GCMD_STATE_ABORTING_DEV, 3 },
	{ TRUE, GCMD_STATE_RESETTING_DEV, 8 },
	{ TRUE, GCMD_STATE_RESETTING_BUS, 10 },
	{ TRUE, GCMD_STATE_HUNG, 60},
	{ FALSE, 0, 0 },	/* spare entry */
	{ FALSE, 0, 0 },	/* spare entry */
	{ FALSE, 0, 0 },	/* spare entry */
	{ FALSE, 0, 0 },	/* spare entry */
	{ FALSE, 0, 0 }		/* spare entry */
};
int	ghd_ntime_inits = sizeof (ghd_time_inits)
				/ sizeof (ghd_time_inits[0]);
/*
 * warlock directives
 */
_NOTE(SCHEME_PROTECTS_DATA("No Mutex Needed", ghd_HZ))
_NOTE(SCHEME_PROTECTS_DATA("No Mutex Needed", ghd_timeout_table))
_NOTE(READ_ONLY_DATA(ghd_ntime_inits))

#ifdef ___notyet___

#include <sys/modctl.h>
extern struct mod_ops mod_miscops;
static struct modlmisc modlmisc = {
	&mod_miscops,	/* Type of module */
	"CCB Timeout Utility Routines"
};
static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modlmisc, NULL
};

/*
 * If this is a loadable module then there's a single CCB timer configure
 * structure for all HBA drivers (rather than one per HBA driver).
 */
static	tmr_t	tmr_conf;

_init()
{
	int	err;

	ghd_timer_init(&tmr_conf, 0);

	if ((err = mod_install(&modlinkage)) != 0)
		ghd_timer_fini(&tmr_conf);

	return (err);
}

_fini()
{
	int	err;

	if ((err = mod_remove(&modlinkage)) == 0)
		ghd_timer_fini(&tmr_conf);

	return (err);
}

_info(modinfop)
struct modinfo *modinfop;
{
	return (mod_info(&modlinkage, modinfop));
}

#endif /* ___notyet___ */



/*
 *
 * ghd_timeout_loop()
 *
 *	Check the CCB timer value for every active CCB for this
 * HBA driver instance.
 *
 *	This function is called both by the ghd_timeout() interrupt
 * handler when called via the timer callout, and by ghd_timer_poll()
 * while procesing "polled" (FLAG_NOINTR) requests.
 *
 * 	The ccc_activel_mutex is held while a CCB list is being scanned.
 * This prevents the HBA driver's transport or interrupt functions
 * from changing the active CCB list. But we wake up very infrequently
 * and do as little as possible so it shouldn't affect performance.
 *
 */

static int
ghd_timeout_loop(ccc_t *cccp)
{
	int	 got_any = FALSE;
	gcmd_t	*gcmdp;
	clock_t	lbolt;


	mutex_enter(&cccp->ccc_activel_mutex);
	lbolt = ddi_get_lbolt();
	gcmdp = (gcmd_t *)L2_next(&cccp->ccc_activel);
	while (gcmdp) {
		/*
		 * check to see if this one has timed out
		 */
		if ((gcmdp->cmd_timeout > 0L) &&
		    (lbolt - gcmdp->cmd_start_time >= gcmdp->cmd_timeout)) {
			got_any = TRUE;
		}
		gcmdp = (gcmd_t *)L2_next(&gcmdp->cmd_timer_link);
	}
	mutex_exit(&cccp->ccc_activel_mutex);
	return (got_any);
}

/*
 *
 * ghd_timeout()
 *
 *	Called every t_ticks ticks to scan the CCB timer lists
 *
 *	The t_mutex mutex is held the entire time this routine is active.
 *	It protects the list of ccc_t's.
 *
 *	The list of cmd_t's is protected by the ccc_activel_mutex mutex
 *	in the ghd_timeout_loop() routine.
 *
 *
 *	+------------+
 *	|   tmr_t    |----+
 *	+------------+    |
 *			  |
 *			  V
 *			  +---------+
 *			  |  ccc_t  |----+
 *			  +---------+    |
 *			  |		 V
 *			  |		 +--------+   +--------+
 *			  |		 | gcmd_t |-->| gcmd_t |--> ...
 *			  |		 +--------+   +--------+
 *			  V
 *			  +---------+
 *			  |  ccc_t  |----+
 *			  +---------+    |
 *			  |		 V
 *			  |		 +--------+
 *			  |		 | gcmd_t |
 *			  V		 +--------+
 *			  ...
 *
 *
 *
 */

static void
ghd_timeout(void *arg)
{
	tmr_t	*tmrp = (tmr_t *)arg;
	ccc_t	*cccp;

	/*
	 * Each HBA driver instance has a separate CCB timer list.
	 * Just exit if there are no more active timeout lists
	 * to process.
	 */
	mutex_enter(&tmrp->t_mutex);
	if ((cccp = tmrp->t_ccc_listp) == NULL) {
		mutex_exit(&tmrp->t_mutex);
		return;
	}

	do {
		/*
		 * If any active CCBs on this HBA have timed out then kick
		 * off the HBA driver's softintr handler to do the timeout
		 * processing
		 */
		if (ghd_timeout_loop(cccp))
			ddi_trigger_softintr(cccp->ccc_soft_id);
	} while ((cccp = cccp->ccc_nextp) != NULL);

	/* re-establish the timeout callback */
	if (tmrp->t_refs)
		tmrp->t_timeout_id = timeout(ghd_timeout, (caddr_t)tmrp,
		    tmrp->t_ticks);
	mutex_exit(&tmrp->t_mutex);
}


/*
 *
 * ghd_timer_newstate()
 *
 *	The HBA mutex is held by my caller.
 *
 */
/*ARGSUSED*/
void
ghd_timer_newstate(ccc_t	*cccp,
		gcmd_t	*gcmdp,
		gtgt_t	*gtgtp,
		gact_t	 action,
		int	 calltype)
{
	gact_t	next_action;
	cmdstate_t next_state;
#if defined(GHD_DEBUG) || defined(__lint)
	char	*msgp;
#endif
	clock_t	new_timeout;
	void	*hba_handle;

	ASSERT(mutex_owned(&cccp->ccc_hba_mutex));

#if defined(DEBUG) || defined(__lint)
	/* it shouldn't be on the timer active list */
	if (gcmdp != NULL) {
		L2el_t	*lp = &gcmdp->cmd_timer_link;
		ASSERT(lp->l2_nextp == lp);
		ASSERT(lp->l2_prevp == lp);
	}
#endif

	hba_handle = cccp->ccc_hba_handle;

	for (;;) {
		switch (action) {
		case GACTION_EARLY_ABORT:
			/* done before it started */
			ASSERT(gcmdp != NULL);
#if defined(GHD_DEBUG) || defined(__lint)
			msgp = "early abort";
#endif
			next_state = GCMD_STATE_DONEQ;
			next_action = GACTION_ABORT_CMD;
			break;

		case GACTION_EARLY_TIMEOUT:
			/* done before it started */
			ASSERT(gcmdp != NULL);
#if defined(GHD_DEBUG) || defined(__lint)
			msgp = "early timeout";
#endif
			next_state = GCMD_STATE_DONEQ;
			next_action = GACTION_ABORT_CMD;
			break;

		case GACTION_ABORT_CMD:
#if defined(GHD_DEBUG) || defined(__lint)
			msgp = "abort request";
#endif
			ASSERT(gcmdp != NULL);
			next_state = GCMD_STATE_ABORTING_CMD;
			next_action = GACTION_ABORT_DEV;
			break;

		case GACTION_ABORT_DEV:
#if defined(GHD_DEBUG) || defined(__lint)
			msgp = "abort device";
#endif
			next_state = GCMD_STATE_ABORTING_DEV;
			next_action = GACTION_RESET_TARGET;
			break;

		case GACTION_RESET_TARGET:
#if defined(GHD_DEBUG) || defined(__lint)
			msgp = "reset target";
#endif
			next_state = GCMD_STATE_RESETTING_DEV;
			next_action = GACTION_RESET_BUS;
			break;

		case GACTION_RESET_BUS:
#if defined(GHD_DEBUG) || defined(__lint)
			msgp = "reset bus";
#endif
			next_state = GCMD_STATE_RESETTING_BUS;
			next_action = GACTION_INCOMPLETE;
			break;

		case GACTION_INCOMPLETE:
		default:
			/* be verbose about HBA resets */
			GDBG_ERROR(("?ghd_timer_newstate: HBA reset failed "
			    "hba 0x%p gcmdp 0x%p gtgtp 0x%p\n",
			    (void *)hba_handle, (void *)gcmdp,
			    (void *)gtgtp));
			/*
			 * When all else fails, punt.
			 *
			 * We're in big trouble if we get to this point.
			 * Maybe we should try to re-initialize the HBA.
			 */
#if defined(GHD_DEBUG) || defined(__lint)
			msgp = "HBA reset";
#endif
			next_state = GCMD_STATE_HUNG;
			next_action = GACTION_INCOMPLETE;
			break;
		}


		GDBG_WARN(("?ghd_timer_newstate: %s, h=0x%p g=0x%p gtgtp=0x%p:"
		    " act %d nextact %d nstate %d\n",
		    msgp, (void *)hba_handle, (void *)gcmdp, (void *)gtgtp,
		    action, next_action, next_state));

		/*
		 * Before firing off the HBA action, restart the timer
		 * using the timeout value from ghd_timeout_table[].
		 *
		 * The table entries should never restart the timer
		 * for the GHD_STATE_IDLE and GHD_STATE_DONEQ states.
		 *
		 */
		if (gcmdp) {
			gcmdp->cmd_state = next_state;
			new_timeout = ghd_timeout_table[gcmdp->cmd_state];
			if (new_timeout != 0)
				ghd_timer_start(cccp, gcmdp, new_timeout);
		}

		/* invoke the HBA's action function */
		if ((*cccp->ccc_timeout_func)(hba_handle, gcmdp,
		    gtgtp, action, calltype)) {
			/* if it took wait for an interrupt or timeout */
			break;
		} else {
			/*
			 * For all of the actions listed here we have already
			 * cancelled the current timer and completed the pkt
			 * through a call to ata_ghd_complete_wrapper() in
			 * ccc_timeout_func (in this case ata_timeout_func()).
			 */
			if ((action == GACTION_RESET_BUS) ||
			    (action == GACTION_EARLY_TIMEOUT) ||
			    (action == GACTION_EARLY_ABORT) ||
			    (action == GACTION_INCOMPLETE)) {
				if (gcmdp != NULL && new_timeout != 0) {
					GHD_TIMER_STOP(cccp, gcmdp);
				}
				return;
			}
		}

		/* all other failures cause transition to next action */
		if (gcmdp != NULL && new_timeout != 0) {
			/*
			 * But stop the old timer prior to
			 * restarting a new timer because each step may
			 * have a different timeout value.
			 */
			GHD_TIMER_STOP(cccp, gcmdp);
		}
		action = next_action;
	}
}


/*
 *
 * ghd_timeout_softintr()
 *
 *	This interrupt is scheduled if a particular HBA instance's
 *	CCB timer list has a timed out CCB.
 *
 *	Find the timed out CCB and then call the HBA driver's timeout
 *	function.
 *
 *	In order to avoid race conditions all processing must be done
 *	while holding the HBA instance's mutex. If the mutex wasn't
 *	held the HBA driver's hardware interrupt routine could be
 *	triggered and it might try to remove a CCB from the list at
 *	same time as were trying to abort it.
 *
 */

static uint_t
ghd_timeout_softintr(caddr_t arg)
{
	ccc_t	*cccp = (ccc_t *)arg;

	/* grab this HBA instance's mutex */
	mutex_enter(&cccp->ccc_hba_mutex);

	/* timeout each expired CCB */
	ghd_timer_poll(cccp);

	mutex_enter(&cccp->ccc_waitq_mutex);
	ghd_waitq_process_and_mutex_exit(cccp);
	return (DDI_INTR_CLAIMED);
}


/*
 * ghd_timer_poll()
 *
 * This function steps a packet to the next action in the recovery
 * procedure.
 *
 * The caller must be  already holding the HBA mutex and take care of
 * running the pkt completion functions.
 *
 */

void
ghd_timer_poll(ccc_t *cccp)
{
	gcmd_t	*gcmdp;
	gact_t	 action;

	ASSERT(mutex_owned(&cccp->ccc_hba_mutex));

	/* abort each expired CCB */
	while (gcmdp = ghd_timeout_get(cccp)) {

		GDBG_INTR(("?ghd_timer_poll: cccp=0x%p gcmdp=0x%p\n",
		    (void *)cccp, (void *)gcmdp));

		switch (gcmdp->cmd_state) {
		case GCMD_STATE_IDLE:
		case GCMD_STATE_DONEQ:
		default:
			/* not supposed to happen */
			GDBG_ERROR(("ghd_timer_poll: invalid state %d\n",
			    gcmdp->cmd_state));
			return;

		case GCMD_STATE_WAITQ:
			action = GACTION_EARLY_TIMEOUT;
			break;

		case GCMD_STATE_ACTIVE:
			action = GACTION_ABORT_CMD;
			break;

		case GCMD_STATE_ABORTING_CMD:
			action = GACTION_ABORT_DEV;
			break;

		case GCMD_STATE_ABORTING_DEV:
			action = GACTION_RESET_TARGET;
			break;

		case GCMD_STATE_RESETTING_DEV:
			action = GACTION_RESET_BUS;
			break;

		case GCMD_STATE_RESETTING_BUS:
			action = GACTION_INCOMPLETE;
			break;

		case GCMD_STATE_HUNG:
			action = GACTION_INCOMPLETE;
			break;
		}

		ghd_timer_newstate(cccp, gcmdp, gcmdp->cmd_gtgtp, action,
		    GHD_NEWSTATE_TIMEOUT);
	}
}




/*
 *
 * ghd_timeout_get()
 *
 *	Remove the first expired CCB from a particular timer list.
 *
 */

static gcmd_t *
ghd_timeout_get(ccc_t *cccp)
{
	gcmd_t	*gcmdp;
	clock_t	lbolt;

	ASSERT(mutex_owned(&cccp->ccc_hba_mutex));


	mutex_enter(&cccp->ccc_activel_mutex);
	lbolt = ddi_get_lbolt();
	gcmdp = (gcmd_t *)L2_next(&cccp->ccc_activel);
	while (gcmdp != NULL) {
		if ((gcmdp->cmd_timeout > 0L) &&
		    (lbolt - gcmdp->cmd_start_time >= gcmdp->cmd_timeout))
			goto expired;
		gcmdp = (gcmd_t *)L2_next(&gcmdp->cmd_timer_link);
	}
	mutex_exit(&cccp->ccc_activel_mutex);
	return (NULL);

expired:
	/* unlink if from the CCB timer list */
	L2_delete(&gcmdp->cmd_timer_link);
	mutex_exit(&cccp->ccc_activel_mutex);
	return (gcmdp);
}


/*
 *
 * ghd_timeout_enable()
 *
 *	Only start a single timeout callback for each HBA driver
 *	regardless of the number of boards it supports.
 *
 */

static void
ghd_timeout_enable(tmr_t *tmrp)
{
	mutex_enter(&tmrp->t_mutex);
	if (tmrp->t_refs++ == 0)  {
		/* establish the timeout callback */
		tmrp->t_timeout_id = timeout(ghd_timeout, (caddr_t)tmrp,
		    tmrp->t_ticks);
	}
	mutex_exit(&tmrp->t_mutex);
}

static void
ghd_timeout_disable(tmr_t *tmrp)
{
	ASSERT(tmrp != NULL);

	mutex_enter(&tmrp->t_mutex);
	if (tmrp->t_refs-- <= 1) {
		_NOTE(DATA_READABLE_WITHOUT_LOCK(tmr_t::t_timeout_id));
		mutex_exit(&tmrp->t_mutex);
		(void) untimeout(tmrp->t_timeout_id);
	} else {
		mutex_exit(&tmrp->t_mutex);
	}
}

/* ************************************************************************ */

	/* these are the externally callable routines */

void
ghd_timer_init(tmr_t *tmrp, clock_t ticks)
{
	int	indx;

	mutex_init(&tmrp->t_mutex, NULL, MUTEX_DRIVER, NULL);

	/*
	 * determine default timeout value
	 */
	ghd_HZ = drv_usectohz(1000000);
	if (ticks == 0)
		ticks = scsi_watchdog_tick * ghd_HZ;
	tmrp->t_ticks = ticks;


	/*
	 * Initialize the table of abort timer values using an
	 * indirect lookup table so that this code isn't dependant
	 * on the cmdstate_t enum values or order.
	 */
	for (indx = 0; indx < ghd_ntime_inits; indx++) {
		int	state;
		clock_t	value;

		if (!ghd_time_inits[indx].valid)
			continue;
		state = ghd_time_inits[indx].state;
		value = ghd_time_inits[indx].value;
		ghd_timeout_table[state] = value;
	}
}

void
ghd_timer_fini(tmr_t *tmrp)
{
	mutex_destroy(&tmrp->t_mutex);
}

int
ghd_timer_attach(ccc_t	*cccp,
		tmr_t	*tmrp,
		int	(*timeout_func)(void *handle, gcmd_t *,
				gtgt_t *gtgtp, gact_t, int calltype))
{
	ddi_iblock_cookie_t iblock;

	if (ddi_add_softintr(cccp->ccc_hba_dip, DDI_SOFTINT_LOW,
	    &cccp->ccc_soft_id, &iblock, NULL, ghd_timeout_softintr,
	    (caddr_t)cccp) !=  DDI_SUCCESS) {
		GDBG_ERROR(("ghd_timer_attach: add softintr failed cccp 0x%p\n",
		    (void *)cccp));
		return (FALSE);
	}

	/* init the per HBA-instance control fields */
	mutex_init(&cccp->ccc_activel_mutex, NULL, MUTEX_DRIVER, iblock);
	L2_INIT(&cccp->ccc_activel);
	cccp->ccc_timeout_func = timeout_func;

	/* stick this HBA's control structure on the master list */
	mutex_enter(&tmrp->t_mutex);

	cccp->ccc_nextp = tmrp->t_ccc_listp;
	tmrp->t_ccc_listp = cccp;
	cccp->ccc_tmrp = tmrp;
	mutex_exit(&tmrp->t_mutex);

	/*
	 * The enable and disable routines use a separate mutex than
	 * t_mutex which is used by the timeout callback function.
	 * This is to avoid a deadlock when calling untimeout() from
	 * the disable routine.
	 */
	ghd_timeout_enable(tmrp);

	return (TRUE);
}


/*
 *
 * ghd_timer_detach()
 *
 *	clean up for a detaching HBA instance
 *
 */

void
ghd_timer_detach(ccc_t *cccp)
{
	tmr_t	*tmrp = cccp->ccc_tmrp;
	ccc_t	**prevpp;

	/* make certain the CCB list is empty */
	ASSERT(cccp->ccc_activel.l2_nextp == &cccp->ccc_activel);
	ASSERT(cccp->ccc_activel.l2_nextp == cccp->ccc_activel.l2_prevp);

	mutex_enter(&tmrp->t_mutex);

	prevpp = &tmrp->t_ccc_listp;
	ASSERT(*prevpp != NULL);

	/* run down the linked list to find the entry that precedes this one */
	do {
		if (*prevpp == cccp)
			goto remove_it;
		prevpp = &(*prevpp)->ccc_nextp;
	} while (*prevpp != NULL);

	/* fell off the end of the list */
	GDBG_ERROR(("ghd_timer_detach: corrupt list, cccp=0x%p\n",
	    (void *)cccp));

remove_it:
	*prevpp = cccp->ccc_nextp;
	mutex_exit(&tmrp->t_mutex);
	mutex_destroy(&cccp->ccc_activel_mutex);

	ddi_remove_softintr(cccp->ccc_soft_id);

	ghd_timeout_disable(tmrp);
}

/*
 *
 * ghd_timer_start()
 *
 *	Add a CCB to the CCB timer list.
 */

void
ghd_timer_start(ccc_t	*cccp,
		gcmd_t	*gcmdp,
		uint32_t cmd_timeout)
{
	clock_t	lbolt;

	mutex_enter(&cccp->ccc_activel_mutex);
	lbolt = ddi_get_lbolt();

	/* initialize this CCB's timer */
	gcmdp->cmd_start_time = lbolt;
	gcmdp->cmd_timeout = (cmd_timeout * ghd_HZ);


	/* add it to the list */
	L2_add(&cccp->ccc_activel, &gcmdp->cmd_timer_link, gcmdp);
	mutex_exit(&cccp->ccc_activel_mutex);
}


/*
 *
 * ghd_timer_stop()
 *
 *	Remove a completed CCB from the CCB timer list.
 *
 *	See the GHD_TIMER_STOP_INLINE() macro in ghd.h for
 *	the actual code.
 */

void
ghd_timer_stop(ccc_t *cccp, gcmd_t *gcmdp)
{
	GHD_TIMER_STOP_INLINE(cccp, gcmdp);
}
