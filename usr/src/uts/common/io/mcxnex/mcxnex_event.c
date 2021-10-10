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
 *  Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * mcxnex_event.c
 *    Mcxnex Interrupt and Event Processing Routines
 *
 *    Implements all the routines necessary for allocating, freeing, and
 *    handling all of the various event types that the Mcxnex hardware can
 *    generate.
 *    These routines include the main Mcxnex interrupt service routine
 *    (mcxnex_isr()) as well as all the code necessary to setup and handle
 *    events from each of the many event queues used by the Mcxnex device.
 */

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/modctl.h>

#include "mcxnex.h"

static void mcxnex_eq_poll(mcxnex_state_t *state, mcxnex_eqhdl_t eq);
static void mcxnex_eq_catastrophic(mcxnex_state_t *state);
static int mcxnex_eq_alloc(mcxnex_state_t *state, uint32_t log_eq_size,
    uint_t intr, mcxnex_eqhdl_t *eqhdl);
static int mcxnex_eq_free(mcxnex_state_t *state, mcxnex_eqhdl_t *eqhdl);
static int mcxnex_eq_handler_init(mcxnex_state_t *state, mcxnex_eqhdl_t eq,
    uint_t evt_type_mask, int (*eqfunc)(mcxnex_state_t *state,
    mcxnex_eqhdl_t eq, mcxnex_hw_eqe_t *eqe));
static int mcxnex_eq_handler_fini(mcxnex_state_t *state, mcxnex_eqhdl_t eq);
static int mcxnex_port_state_change_handler(mcxnex_state_t *state,
    mcxnex_eqhdl_t eq, mcxnex_hw_eqe_t *eqe);
static int mcxnex_comm_estbl_handler(mcxnex_state_t *state,
    mcxnex_eqhdl_t eq, mcxnex_hw_eqe_t *eqe);
static int mcxnex_local_wq_cat_err_handler(mcxnex_state_t *state,
    mcxnex_eqhdl_t eq, mcxnex_hw_eqe_t *eqe);
static int mcxnex_invreq_local_wq_err_handler(mcxnex_state_t *state,
    mcxnex_eqhdl_t eq, mcxnex_hw_eqe_t *eqe);
static int mcxnex_local_acc_vio_wq_err_handler(mcxnex_state_t *state,
    mcxnex_eqhdl_t eq, mcxnex_hw_eqe_t *eqe);
static int mcxnex_sendq_drained_handler(mcxnex_state_t *state,
    mcxnex_eqhdl_t eq, mcxnex_hw_eqe_t *eqe);
static int mcxnex_path_mig_handler(mcxnex_state_t *state,
    mcxnex_eqhdl_t eq, mcxnex_hw_eqe_t *eqe);
static int mcxnex_path_mig_err_handler(mcxnex_state_t *state,
    mcxnex_eqhdl_t eq, mcxnex_hw_eqe_t *eqe);
static int mcxnex_catastrophic_handler(mcxnex_state_t *state,
    mcxnex_eqhdl_t eq, mcxnex_hw_eqe_t *eqe);
static int mcxnex_srq_last_wqe_reached_handler(mcxnex_state_t *state,
    mcxnex_eqhdl_t eq, mcxnex_hw_eqe_t *eqe);
static int mcxnex_ecc_detection_handler(mcxnex_state_t *state,
    mcxnex_eqhdl_t eq, mcxnex_hw_eqe_t *eqe);
static int mcxnex_no_eqhandler(mcxnex_state_t *state, mcxnex_eqhdl_t eq,
    mcxnex_hw_eqe_t *eqe);
static int mcxnex_eq_demux(mcxnex_state_t *state, mcxnex_eqhdl_t eq,
    mcxnex_hw_eqe_t *eqe);

extern int mcxnex_verbose;

/*
 * mcxnex_eq_init_all
 *    Context: Only called from attach() path context
 */
int
mcxnex_eq_init_all(mcxnex_state_t *state)
{
	uint_t		log_eq_size, intr_num;
	uint_t		num_eq, num_eq_init, num_eq_unmap, num_eq_rsvd;
	uint32_t	event_mask;	/* used for multiple event types */
	int		status, i, num_extra;
	uint64_t	offset;
	ddi_acc_handle_t uarhdl = mcxnex_get_uarhdl(state);

	/* initialize the FMA retry loop */
	mcxnex_pio_init(fm_loop_cnt, fm_status, fm_test);

	/*
	 * For now, all Event Queues default to the same size (pulled from
	 * the current configuration profile) and are all assigned to the
	 * same interrupt or MSI.  In the future we may support assigning
	 * EQs to specific interrupts or MSIs XXX
	 */
	log_eq_size = state->hs_cfg_profile->cp_log_eq_sz;

	/*
	 * Total number of supported EQs is fixed.  Mcxnex hardware
	 * supports up to 512 EQs, though in theory they will one day be
	 * alloc'd to virtual HCA's.  We are currently using only 47 of them
	 * - that is, in Arbel and Tavor, before MCXNEX, where
	 * we had set aside the first 32 for use with Completion Queues (CQ)
	 * and reserved a few of the other 32 for each specific class of event
	 *
	 * However, with the coming of vitualization, we'll have only 4 per
	 * potential guest - so, we'll try alloc'ing them differntly
	 * (see below for more details).
	 */
	num_eq = MCXNEX_NUM_EQ_USED;
	num_eq_rsvd = state->hs_devlim.num_rsvd_eq;

	/*
	 * If MSI is to be used, then set intr_num to the MSI number.
	 * Otherwise, for fixed (i.e. 'legacy') interrupts,
	 * it is what the card tells us in 'inta_pin'.
	 */
	if (state->hs_intr_type_chosen == DDI_INTR_TYPE_FIXED) {
		intr_num = state->hs_adapter.inta_pin;
		num_extra = 0;
	} else {
		/* If we have more than one MSI-X vector, init them. */
		for (i = 0; i + 1 < state->hs_intrmsi_allocd; i++) {
			status = mcxnex_eq_alloc(state, log_eq_size, i,
			    &state->hs_eqhdl[i + num_eq_rsvd]);
			if (status != DDI_SUCCESS) {
				while (--i >= 0) {
					(void) mcxnex_eq_handler_fini(state,
					    state->hs_eqhdl[i + num_eq_rsvd]);
					(void) mcxnex_eq_free(state,
					    &state->hs_eqhdl[i + num_eq_rsvd]);
				}
				return (DDI_FAILURE);
			}

			(void) mcxnex_eq_handler_init(state,
			    state->hs_eqhdl[i + num_eq_rsvd],
			    MCXNEX_EVT_NO_MASK, mcxnex_cq_handler);
		}
		intr_num = i;
		num_extra = i;
	}

	/*
	 * Allocate and initialize the rest of the Event Queues to be used.
	 * If any of these EQ allocations fail then jump to the end, cleanup
	 * what had been successfully initialized, and return an error.
	 */
	for (i = 0; i < num_eq; i++) {
		status = mcxnex_eq_alloc(state, log_eq_size, intr_num,
		    &state->hs_eqhdl[num_eq_rsvd + num_extra + i]);
		if (status != DDI_SUCCESS) {
			num_eq_init = i;
			goto all_eq_init_fail;
		}
	}
	num_eq_init = num_eq;
	/*
	 * The "num_eq_unmap" variable is used in any possible failure
	 * cleanup (below) to indicate which events queues might require
	 * possible event class unmapping.
	 */
	num_eq_unmap = 0;
	/*
	 * Setup EQ0 (first avail) for use with Completion Queues.  Note: We can
	 * cast the return value to void here because, when we use the
	 * MCXNEX_EVT_NO_MASK flag, it is not possible for
	 * mcxnex_eq_handler_init() to return an error.
	 */

	(void) mcxnex_eq_handler_init(state,
	    state->hs_eqhdl[num_eq_unmap + num_extra + num_eq_rsvd],
	    MCXNEX_EVT_NO_MASK, mcxnex_cq_handler);

	num_eq_unmap++;

	/*
	 * Setup EQ1 for handling Completion Queue Error Events.
	 *
	 * These events include things like CQ overflow or CQ access
	 * violation errors.  If this setup fails for any reason (which, in
	 * general, it really never should), then jump to the end, cleanup
	 * everything that has been successfully initialized, and return an
	 * error.
	 */
	status = mcxnex_eq_handler_init(state,
	    state->hs_eqhdl[num_eq_unmap + num_extra + num_eq_rsvd],
	    MCXNEX_EVT_MSK_CQ_ERRORS, mcxnex_cq_err_handler);
	if (status != DDI_SUCCESS) {
		goto all_eq_init_fail;
	}
	num_eq_unmap++;


	/*
	 * Setup EQ2 for handling most other things including:
	 *
	 * Port State Change Events
	 *   These events include things like Port Up and Port Down events.
	 *
	 * Communication Established Events
	 *   These events correspond to the IB affiliated asynchronous events
	 *   that are used for connection management
	 *
	 * Path Migration Succeeded Events
	 *   These evens corresponid to the IB affiliated asynchronous events
	 *   that are used to indicate successful completion of a
	 *   Path Migration.
	 *
	 * Command Completion Events
	 *   These events correspond to the Arbel generated events that are used
	 *   to indicate Arbel firmware command completion.
	 *
	 * Local WQ Catastrophic Error Events
	 * Invalid Req Local WQ Error Events
	 * Local Access Violation WQ Error Events
	 * SRQ Catastrophic Error Events
	 * SRQ Last WQE Reached Events
	 * ECC error detection events
	 *   These events also correspond to the similarly-named IB affiliated
	 *   asynchronous error type.
	 *
	 * Send Queue Drained Events
	 *   These events correspond to the IB affiliated asynchronous events
	 *   that are used to indicate completion of a Send Queue Drained QP
	 *   state transition.
	 *
	 * Path Migration Failed Events
	 *   These events correspond to the IB affiliated asynchronous events
	 *   that are used to indicate that path migration was not successful.
	 *
	 * NOTE: When an event fires on this EQ, it will demux the type and
	 * 	send it to the right specific handler routine
	 *
	 */


	event_mask =
	    MCXNEX_EVT_MSK_PORT_STATE_CHANGE |
	    MCXNEX_EVT_MSK_COMM_ESTABLISHED |
	    MCXNEX_EVT_MSK_COMMAND_INTF_COMP |
	    MCXNEX_EVT_MSK_LOCAL_WQ_CAT_ERROR |
	    MCXNEX_EVT_MSK_INV_REQ_LOCAL_WQ_ERROR |
	    MCXNEX_EVT_MSK_LOCAL_ACC_VIO_WQ_ERROR |
	    MCXNEX_EVT_MSK_SEND_QUEUE_DRAINED |
	    MCXNEX_EVT_MSK_PATH_MIGRATED |
	    MCXNEX_EVT_MSK_PATH_MIGRATE_FAILED |
	    MCXNEX_EVT_MSK_SRQ_CATASTROPHIC_ERROR |
	    MCXNEX_EVT_MSK_SRQ_LAST_WQE_REACHED |
	    MCXNEX_EVT_MSK_ECC_DETECTION;

	status = mcxnex_eq_handler_init(state,
	    state->hs_eqhdl[num_eq_unmap + num_extra + num_eq_rsvd],
	    event_mask, mcxnex_eq_demux);
	if (status != DDI_SUCCESS) {
		goto all_eq_init_fail;
	}
	num_eq_unmap++;

	/*
	 * Setup EQ3 to catch all other types of events.  Specifically, we
	 * do not catch the "Local EEC Catastrophic Error Event" because we
	 * should have no EEC (the Arbel driver does not support RD).  We also
	 * choose not to handle any of the address translation page fault
	 * event types.  Since we are not doing any page fault handling (and
	 * since the Arbel firmware does not currently support any such
	 * handling), we allow these events to go to the catch-all handler.
	 */
	status = mcxnex_eq_handler_init(state,
	    state->hs_eqhdl[num_eq_unmap + num_extra + num_eq_rsvd],
	    MCXNEX_EVT_CATCHALL_MASK, mcxnex_no_eqhandler);
	if (status != DDI_SUCCESS) {
		goto all_eq_init_fail;
	}
	num_eq_unmap++;

	/* the FMA retry loop starts. */
	mcxnex_pio_start(state, uarhdl, all_eq_init_fail, fm_loop_cnt,
	    fm_status, fm_test);

	/*
	 * Run through and initialize the Consumer Index for each EQC.
	 */
	for (i = 0; i < num_eq + num_extra; i++) {
		offset = ARM_EQ_INDEX(i + num_eq_rsvd);
		ddi_put32(uarhdl,
		    (uint32_t *)((uintptr_t)state->hs_reg_uar_baseaddr +
		    (uint32_t)offset), 0x0);
	}

	/* the FMA retry loop ends. */
	mcxnex_pio_end(state, uarhdl, all_eq_init_fail, fm_loop_cnt,
	    fm_status, fm_test);

	return (DDI_SUCCESS);

all_eq_init_fail:

	/* Unmap any of the partially mapped EQs from above */
	for (i = 0; i < num_eq_unmap + num_extra; i++) {
		(void) mcxnex_eq_handler_fini(state,
		    state->hs_eqhdl[i + num_eq_rsvd]);
	}

	/* Free up any of the partially allocated EQs from above */
	for (i = 0; i < num_eq_init + num_extra; i++) {
		(void) mcxnex_eq_free(state, &state->hs_eqhdl[i]);
	}

	/* If a HW error happen during ddi_pio, return DDI_FAILURE */
	if (fm_status == HCA_PIO_PERSISTENT) {
		mcxnex_fm_ereport(state, HCA_SYS_ERR, HCA_ERR_NON_FATAL);
		status = DDI_FAILURE;
	}

	return (status);
}


/*
 * mcxnex_eq_fini_all
 *    Context: Only called from attach() and/or detach() path contexts
 */
int
mcxnex_eq_fini_all(mcxnex_state_t *state)
{
	uint_t		num_eq, num_eq_rsvd;
	int		status, i;

	/*
	 * Grab the total number of supported EQs again.  This is the same
	 * hardcoded value that was used above (during the event queue
	 * initialization.)
	 */
	num_eq = MCXNEX_NUM_EQ_USED + state->hs_intrmsi_allocd - 1;
	num_eq_rsvd = state->hs_devlim.num_rsvd_eq;

	/*
	 * For each of the event queues that we initialized and mapped
	 * earlier, attempt to unmap the events from the EQ.
	 */
	for (i = 0; i < num_eq; i++) {
		status = mcxnex_eq_handler_fini(state,
		    state->hs_eqhdl[i + num_eq_rsvd]);
		if (status != DDI_SUCCESS) {
			return (DDI_FAILURE);
		}
	}

	/*
	 * Teardown and free up all the Event Queues that were allocated
	 * earlier.
	 */
	for (i = 0; i < num_eq; i++) {
		status = mcxnex_eq_free(state,
		    &state->hs_eqhdl[i + num_eq_rsvd]);
		if (status != DDI_SUCCESS) {
			return (DDI_FAILURE);
		}
	}

	return (DDI_SUCCESS);
}

/*
 * mcxnex_eq_arm()
 *	Context:  called from interrupt
 *
 * Arms a single eq - eqn is the __logical__ eq number 0-based
 */
void
mcxnex_eq_arm(mcxnex_state_t *state, int eqn)
{
	uint64_t	offset;
	mcxnex_eqhdl_t	eq;
	uint32_t	eq_ci;
	ddi_acc_handle_t uarhdl = mcxnex_get_uarhdl(state);

	/* initialize the FMA retry loop */
	mcxnex_pio_init(fm_loop_cnt, fm_status, fm_test);

	offset = ARM_EQ_INDEX(eqn + state->hs_devlim.num_rsvd_eq);
	eq = state->hs_eqhdl[eqn + state->hs_devlim.num_rsvd_eq];
	eq_ci = (eq->eq_consindx & MCXNEX_EQ_CI_MASK) | EQ_ARM_BIT;

	/* the FMA retry loop starts. */
	mcxnex_pio_start(state, uarhdl, pio_error, fm_loop_cnt, fm_status,
	    fm_test);

	ddi_put32(uarhdl,
	    (uint32_t *)((uintptr_t)state->hs_reg_uar_baseaddr +
	    (uint32_t)offset), eq_ci);

	/* the FMA retry loop ends. */
	mcxnex_pio_end(state, uarhdl, pio_error, fm_loop_cnt, fm_status,
	    fm_test);

	return;

pio_error:
	mcxnex_fm_ereport(state, HCA_SYS_ERR, HCA_ERR_FATAL);
}


/*
 * mcxnex_eq_arm_all
 *    Context: Only called from attach() and/or detach() path contexts
 *    Arbel calls in interrupt, currently (initial impl) in Mcxnex as well
 */
int
mcxnex_eq_arm_all(mcxnex_state_t *state)
{
	uint_t		num_eq, num_eq_rsvd;
	uint64_t	offset;
	mcxnex_eqhdl_t	eq;
	uint32_t	eq_ci;
	int		i;
	ddi_acc_handle_t uarhdl = mcxnex_get_uarhdl(state);

	/* initialize the FMA retry loop */
	mcxnex_pio_init(fm_loop_cnt, fm_status, fm_test);

	num_eq = MCXNEX_NUM_EQ_USED;
	num_eq_rsvd = state->hs_devlim.num_rsvd_eq;

	/* the FMA retry loop starts. */
	mcxnex_pio_start(state, uarhdl, pio_error, fm_loop_cnt, fm_status,
	    fm_test);

	for (i = 0; i < num_eq; i++) {
		offset = ARM_EQ_INDEX(i + num_eq_rsvd);
		eq = state->hs_eqhdl[i + num_eq_rsvd];
		eq_ci = (eq->eq_consindx & MCXNEX_EQ_CI_MASK) | EQ_ARM_BIT;
		ddi_put32(uarhdl,
		    (uint32_t *)((uintptr_t)state->hs_reg_uar_baseaddr +
		    (uint32_t)offset), eq_ci);
	}

	/* the FMA retry loop ends. */
	mcxnex_pio_end(state, uarhdl, pio_error, fm_loop_cnt, fm_status,
	    fm_test);

	return (DDI_SUCCESS);

pio_error:
	mcxnex_fm_ereport(state, HCA_SYS_ERR, HCA_ERR_NON_FATAL);
	return (DDI_FAILURE);
}


/*
 * mcxnex_isr()
 *    Context: Only called from interrupt context (and during panic)
 */
uint_t
mcxnex_isr(caddr_t arg1, caddr_t arg2)
{
	mcxnex_state_t	*state;
	int		i, r;
	int		intr;

	/*
	 * Grab the Mcxnex softstate pointer from the input parameter
	 */
	state	= (mcxnex_state_t *)(void *)arg1;

	/* Get the interrupt number */
	intr = (int)(uintptr_t)arg2;

	/*
	 * Clear the interrupt.  Note: This is only needed for
	 * fixed interrupts as the framework does what is needed for
	 * MSI-X interrupts.
	 */
	if (state->hs_intr_type_chosen == DDI_INTR_TYPE_FIXED) {
		ddi_acc_handle_t cmdhdl = mcxnex_get_cmdhdl(state);

		/* initialize the FMA retry loop */
		mcxnex_pio_init(fm_loop_cnt, fm_status, fm_test);

		/* the FMA retry loop starts. */
		mcxnex_pio_start(state, cmdhdl, pio_error, fm_loop_cnt,
		    fm_status, fm_test);

		ddi_put64(cmdhdl, state->hs_cmd_regs.clr_intr,
		    (uint64_t)1 << state->hs_adapter.inta_pin);

		/* the FMA retry loop ends. */
		mcxnex_pio_end(state, cmdhdl, pio_error, fm_loop_cnt, fm_status,
		    fm_test);
	}

	/*
	 * Loop through all the EQs looking for ones that have "fired".
	 * To determine if an EQ is fired, the ownership will be the SW
	 * (the HW will set the owner appropriately). Update the Consumer Index
	 * of the Event Queue Entry (EQE) and pass it to HW by writing it
	 * to the respective Set CI DB Register.
	 *
	 * The "else" case handles the extra EQs used only for completion
	 * events, whereas the "if" case deals with the required interrupt
	 * vector that is used for all classes of events.
	 */
	r = state->hs_devlim.num_rsvd_eq;

	if (intr + 1 == state->hs_intrmsi_allocd) {	/* last intr */
		r += state->hs_intrmsi_allocd - 1;
		for (i = 0; i < MCXNEX_NUM_EQ_USED; i++) {
			mcxnex_eq_poll(state, state->hs_eqhdl[i + r]);
		}
	} else {	/* only poll the one EQ */
		mcxnex_eq_poll(state, state->hs_eqhdl[intr + r]);
	}

	return (DDI_INTR_CLAIMED);

pio_error:
	mcxnex_fm_ereport(state, HCA_SYS_ERR, HCA_ERR_FATAL);
	return (DDI_INTR_UNCLAIMED);
}


/*
 * mcxnex_eq_poll
 *    Context: Only called from interrupt context (and during panic)
 */
static void
mcxnex_eq_poll(mcxnex_state_t *state, mcxnex_eqhdl_t eq)
{
	mcxnex_hw_eqe_t	*eqe;
	uint64_t	offset;
	uint32_t	cons_indx, wrap_around_mask;
	int (*eqfunction)(mcxnex_state_t *state, mcxnex_eqhdl_t eq,
	    mcxnex_hw_eqe_t *eqe);
	ddi_acc_handle_t uarhdl = mcxnex_get_uarhdl(state);

	/* initialize the FMA retry loop */
	mcxnex_pio_init(fm_loop_cnt, fm_status, fm_test);

	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*eq))

	/* Get the consumer pointer index */
	cons_indx = eq->eq_consindx;

	/*
	 * Calculate the wrap around mask.  Note: This operation only works
	 * because all Mcxnex event queues have power-of-2 sizes
	 */
	wrap_around_mask = (eq->eq_bufsz - 1);

	/* Calculate the pointer to the first EQ entry */
	eqe = &eq->eq_buf[(cons_indx & wrap_around_mask)];


	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*eqe))

	/*
	 * Pull the handler function for this EQ from the Mcxnex Event Queue
	 * handle
	 */
	eqfunction = eq->eq_func;

	while (MCXNEX_EQE_OWNER_IS_SW(eq, eqe)) {

		/*
		 * Call the EQ handler function.  But only call if we
		 * are not in polled I/O mode (i.e. not processing
		 * because of a system panic).  Note: We don't call
		 * the EQ handling functions from a system panic
		 * because we are primarily concerned only with
		 * ensuring that the event queues do not overflow (or,
		 * more specifically, the event queue associated with
		 * the CQ that is being used in the sync/dump process).
		 * Also, we don't want to make any upcalls (to the
		 * IBTF) because we can't guarantee when/if those
		 * calls would ever return.  And, if we're in panic,
		 * then we reached here through a PollCQ() call (from
		 * mcxnex_cq_poll()), and we need to ensure that we
		 * successfully return any work completions to the
		 * caller.
		 */
		if (ddi_in_panic() == 0) {
			eqfunction(state, eq, eqe);
		}

		/* Reset to hardware ownership is implicit */

		eq->eq_nexteqe++;	/* for next time through */

		/* Increment the consumer index */
		cons_indx++;

		/* Update the pointer to the next EQ entry */
		eqe = &eq->eq_buf[(cons_indx & wrap_around_mask)];
	}

	/*
	 * write consumer index via EQ set CI Doorbell, to keep overflow
	 * from occuring during poll
	 */

	eq->eq_consindx = cons_indx;

	offset = ARM_EQ_INDEX(eq->eq_eqnum);

	/* the FMA retry loop starts. */
	mcxnex_pio_start(state, uarhdl, pio_error, fm_loop_cnt,
	    fm_status, fm_test);

	ddi_put32(uarhdl,
	    (uint32_t *)((uintptr_t)state->hs_reg_uar_baseaddr +
	    (uint32_t)offset), (cons_indx & MCXNEX_EQ_CI_MASK) |
	    EQ_ARM_BIT);

	/* the FMA retry loop starts. */
	mcxnex_pio_end(state, uarhdl, pio_error, fm_loop_cnt,
	    fm_status, fm_test);

	return;

pio_error:
	mcxnex_fm_ereport(state, HCA_SYS_ERR, HCA_ERR_FATAL);
}


/*
 * mcxnex_eq_catastrophic
 *    Context: Only called from interrupt context (and during panic)
 */
static void
mcxnex_eq_catastrophic(mcxnex_state_t *state)
{
	ddi_acc_handle_t	cmdhdl = mcxnex_get_cmdhdl(state);
	ibt_async_code_t	type;
	ibc_async_event_t	event;
	uint32_t		*base_addr;
	uint32_t		buf_size;
	uint32_t		word;
	uint8_t			err_type;
	uint32_t		err_buf;
	int			i;

	/* initialize the FMA retry loop */
	mcxnex_pio_init(fm_loop_cnt, fm_status, fm_test);

	bzero(&event, sizeof (ibc_async_event_t));
	base_addr = state->hs_cmd_regs.fw_err_buf;

	buf_size = state->hs_fw.error_buf_sz;	/* in #dwords */

	/* the FMA retry loop starts. */
	mcxnex_pio_start(state, cmdhdl, pio_error, fm_loop_cnt, fm_status,
	    fm_test);

	word = ddi_get32(cmdhdl, base_addr);

	/* the FMA retry loop ends. */
	mcxnex_pio_end(state, cmdhdl, pio_error, fm_loop_cnt, fm_status,
	    fm_test);

	err_type = (word & 0xFF000000) >> 24;
	type	 = IBT_ERROR_LOCAL_CATASTROPHIC;

	switch (err_type) {
	case MCXNEX_CATASTROPHIC_INTERNAL_ERROR:
		cmn_err(CE_WARN, "Catastrophic Internal Error: 0x%02x",
		    err_type);

		break;

	case MCXNEX_CATASTROPHIC_UPLINK_BUS_ERROR:
		cmn_err(CE_WARN, "Catastrophic Uplink Bus Error: 0x%02x",
		    err_type);

		break;

	case MCXNEX_CATASTROPHIC_DDR_DATA_ERROR:
		cmn_err(CE_WARN, "Catastrophic DDR Data Error: 0x%02x",
		    err_type);

		break;

	case MCXNEX_CATASTROPHIC_INTERNAL_PARITY_ERROR:
		cmn_err(CE_WARN, "Catastrophic Internal Parity Error: 0x%02x",
		    err_type);

		break;

	default:
		/* Unknown type of Catastrophic error */
		cmn_err(CE_WARN, "Catastrophic Unknown Error: 0x%02x",
		    err_type);

		break;
	}

	/* the FMA retry loop starts. */
	mcxnex_pio_start(state, cmdhdl, pio_error, fm_loop_cnt, fm_status,
	    fm_test);

	/*
	 * Read in the catastrophic error buffer from the hardware.
	 */
	for (i = 0; i < buf_size; i++) {
		base_addr =
		    (state->hs_cmd_regs.fw_err_buf + i);
		err_buf = ddi_get32(cmdhdl, base_addr);
		cmn_err(CE_NOTE, "mcxnex%d: catastrophic_error[%02x]: %08X",
		    state->hs_instance, i, err_buf);
	}

	/* the FMA retry loop ends. */
	mcxnex_pio_end(state, cmdhdl, pio_error, fm_loop_cnt, fm_status,
	    fm_test);

	/*
	 * We also call the IBTF here to inform it of the catastrophic error.
	 * Note: Since no event information (i.e. QP handles, CQ handles,
	 * etc.) is necessary, we pass a NULL pointer instead of a pointer to
	 * an empty ibc_async_event_t struct.
	 *
	 * But we also check if "hs_ibtfpriv" is NULL.  If it is then it
	 * means that we've have either received this event before we
	 * finished attaching to the IBTF or we've received it while we
	 * are in the process of detaching.
	 */
	if (state->async_cb != NULL) {
		MCXNEX_DO_IBTF_ASYNC_CALLB(state, type, &event);
	}

pio_error:
	/* ignore these errors but log them because they're harmless. */
	mcxnex_fm_ereport(state, HCA_SYS_ERR, HCA_ERR_NON_FATAL);
}

void
mcxnex_priv_async_handler(mcxnex_state_t *state, ibt_async_code_t type,
    ibc_async_event_t *event)
{
	if (state->async_cb)
		state->async_cb(state, type, event);
	/* for IB driver, need to call ibc_async_handler() */
}

void
mcxnex_priv_async_cb_set(mcxnex_state_t *state, mcxnex_priv_async_cb_t cb)
{
	state->async_cb = cb;
}

/*
 * mcxnex_eq_alloc()
 *    Context: Only called from attach() path context
 */
static int
mcxnex_eq_alloc(mcxnex_state_t *state, uint32_t log_eq_size, uint_t intr,
    mcxnex_eqhdl_t *eqhdl)
{
	mcxnex_rsrc_t		*eqc, *rsrc;
	mcxnex_hw_eqc_t		eqc_entry;
	mcxnex_eqhdl_t		eq;
	ibt_mr_attr_t		mr_attr;
	mcxnex_mr_options_t	op;
	mcxnex_pdhdl_t		pd;
	mcxnex_mrhdl_t		mr;
	mcxnex_hw_eqe_t		*buf;
	int			status;

	/* Use the internal protection domain (PD) for setting up EQs */
	pd = state->hs_pdhdl_internal;

	/* Increment the reference count on the protection domain (PD) */
	mcxnex_pd_refcnt_inc(pd);

	/*
	 * Allocate an EQ context entry.  This will be filled in with all
	 * the necessary parameters to define the Event Queue.  And then
	 * ownership will be passed to the hardware in the final step
	 * below.  If we fail here, we must undo the protection domain
	 * reference count.
	 */
	status = mcxnex_rsrc_alloc(state, MCXNEX_EQC, 1, DDI_SLEEP, &eqc);
	if (status != DDI_SUCCESS) {
		status = DDI_FAILURE;
		goto eqalloc_fail1;
	}

	/*
	 * Allocate the software structure for tracking the event queue (i.e.
	 * the Mcxnex Event Queue handle).  If we fail here, we must undo the
	 * protection domain reference count and the previous resource
	 * allocation.
	 */
	status = mcxnex_rsrc_alloc(state, MCXNEX_EQHDL, 1, DDI_SLEEP, &rsrc);
	if (status != DDI_SUCCESS) {
		status = DDI_FAILURE;
		goto eqalloc_fail2;
	}

	eq = (mcxnex_eqhdl_t)rsrc->hr_addr;

	/*
	 * Allocate the memory for Event Queue.
	 */
	eq->eq_eqinfo.qa_size = (1 << log_eq_size) * sizeof (mcxnex_hw_eqe_t);
	eq->eq_eqinfo.qa_alloc_align = PAGESIZE;
	eq->eq_eqinfo.qa_bind_align  = PAGESIZE;

	eq->eq_eqinfo.qa_location = MCXNEX_QUEUE_LOCATION_NORMAL;
	status = mcxnex_queue_alloc(state, &eq->eq_eqinfo, DDI_SLEEP);
	if (status != DDI_SUCCESS) {
		status = DDI_FAILURE;
		goto eqalloc_fail3;
	}

	buf = (mcxnex_hw_eqe_t *)eq->eq_eqinfo.qa_buf_aligned;
	/*
	 * Initializing each of the Event Queue Entries (EQE) by setting their
	 * ownership to hardware ("owner" bit set to HW) is now done by HW
	 * when the transfer of ownership (below) of the
	 * EQ context itself is done.
	 */

	/*
	 * Register the memory for the EQ.
	 *
	 * Because we are in the attach path we use NOSLEEP here so that we
	 * SPIN in the HCR since the event queues are not setup yet, and we
	 * cannot NOSPIN at this point in time.
	 */

	mr_attr.mr_vaddr = (uint64_t)(uintptr_t)buf;
	mr_attr.mr_len	 = eq->eq_eqinfo.qa_size;
	mr_attr.mr_as	 = NULL;
	mr_attr.mr_flags = IBT_MR_NOSLEEP | IBT_MR_ENABLE_LOCAL_WRITE;
	op.mro_bind_type   = state->hs_cfg_profile->cp_iommu_bypass;
	op.mro_bind_dmahdl = eq->eq_eqinfo.qa_dmahdl;
	op.mro_bind_override_addr = 0;
	status = mcxnex_mr_register(state, pd, &mr_attr, &mr, &op,
	    MCXNEX_EQ_CMPT);
	if (status != DDI_SUCCESS) {
		status = DDI_FAILURE;
		goto eqalloc_fail4;
	}
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*mr))

	/* Sync entire EQ for use by the hardware */
	eq->eq_sync = 1;

	(void) ddi_dma_sync(mr->mr_bindinfo.bi_dmahdl, 0,
	    eq->eq_eqinfo.qa_size, DDI_DMA_SYNC_FORDEV);

	/*
	 * Fill in the EQC entry.  This is the final step before passing
	 * ownership of the EQC entry to the Mcxnex hardware.  We use all of
	 * the information collected/calculated above to fill in the
	 * requisite portions of the EQC.  Note:  We create all EQs in the
	 * "fired" state.  We will arm them later (after our interrupt
	 * routine had been registered.)
	 */
	bzero(&eqc_entry, sizeof (mcxnex_hw_eqc_t));
	eqc_entry.state		= MCXNEX_EQ_ARMED;
	eqc_entry.log_eq_sz	= log_eq_size;
	eqc_entry.intr		= intr;
	eqc_entry.log2_pgsz	= mr->mr_log2_pgsz;
	eqc_entry.pg_offs	= eq->eq_eqinfo.qa_pgoffs >> 5;
	eqc_entry.mtt_base_addrh = (uint32_t)((mr->mr_mttaddr >> 32) & 0xFF);
	eqc_entry.mtt_base_addrl =  mr->mr_mttaddr >> 3;
	eqc_entry.cons_indx	= 0x0;
	eqc_entry.prod_indx	= 0x0;

	/*
	 * Write the EQC entry to hardware.  Lastly, we pass ownership of
	 * the entry to the hardware (using the Mcxnex SW2HW_EQ firmware
	 * command).  Note: in general, this operation shouldn't fail.  But
	 * if it does, we have to undo everything we've done above before
	 * returning error.
	 */
	status = mcxnex_cmn_ownership_cmd_post(state, SW2HW_EQ, &eqc_entry,
	    sizeof (mcxnex_hw_eqc_t), eqc->hr_indx, DDI_NOSLEEP);
	if (status != MCXNEX_CMD_SUCCESS) {
		cmn_err(CE_NOTE, "mcxnex%d: SW2HW_EQ command failed: %08x",
		    state->hs_instance, status);
		if (status == MCXNEX_CMD_INVALID_STATUS) {
			mcxnex_fm_ereport(state, HCA_SYS_ERR, HCA_ERR_SRV_LOST);
		}
		status = DDI_FAILURE;
		goto eqalloc_fail5;
	}

	/*
	 * Fill in the rest of the Mcxnex Event Queue handle.  Having
	 * successfully transferred ownership of the EQC, we can update the
	 * following fields for use in further operations on the EQ.
	 */
	eq->eq_eqcrsrcp	 = eqc;
	eq->eq_rsrcp	 = rsrc;
	eq->eq_consindx	 = 0;
	eq->eq_eqnum	 = eqc->hr_indx;
	eq->eq_buf	 = buf;
	eq->eq_bufsz	 = (1 << log_eq_size);
	eq->eq_log_eqsz	 = log_eq_size;
	eq->eq_nexteqe	 = 0;
	eq->eq_mrhdl	 = mr;
	*eqhdl		 = eq;

	return (DDI_SUCCESS);

/*
 * The following is cleanup for all possible failure cases in this routine
 */
eqalloc_fail5:
	if (mcxnex_mr_deregister(state, &mr, MCXNEX_MR_DEREG_ALL,
	    DDI_NOSLEEP) != DDI_SUCCESS) {
		MCXNEX_WARNING(state, "failed to deregister EQ memory");
	}
eqalloc_fail4:
	mcxnex_queue_free(&eq->eq_eqinfo);
eqalloc_fail3:
	mcxnex_rsrc_free(state, &rsrc);
eqalloc_fail2:
	mcxnex_rsrc_free(state, &eqc);
eqalloc_fail1:
	mcxnex_pd_refcnt_dec(pd);
eqalloc_fail:
	return (status);
}


/*
 * mcxnex_eq_free()
 *    Context: Only called from attach() and/or detach() path contexts
 */
static int
mcxnex_eq_free(mcxnex_state_t *state, mcxnex_eqhdl_t *eqhdl)
{
	mcxnex_rsrc_t		*eqc, *rsrc;
	mcxnex_hw_eqc_t		eqc_entry;
	mcxnex_pdhdl_t		pd;
	mcxnex_mrhdl_t		mr;
	mcxnex_eqhdl_t		eq;
	uint32_t		eqnum;
	int			status;

	/*
	 * Pull all the necessary information from the Mcxnex Event Queue
	 * handle.  This is necessary here because the resource for the
	 * EQ handle is going to be freed up as part of this operation.
	 */
	eq	= *eqhdl;
	eqc	= eq->eq_eqcrsrcp;
	rsrc	= eq->eq_rsrcp;
	pd	= state->hs_pdhdl_internal;
	mr	= eq->eq_mrhdl;
	eqnum	= eq->eq_eqnum;

	/*
	 * Reclaim EQC entry from hardware (using the Mcxnex HW2SW_EQ
	 * firmware command).  If the ownership transfer fails for any reason,
	 * then it is an indication that something (either in HW or SW) has
	 * gone seriously wrong.
	 */
	status = mcxnex_cmn_ownership_cmd_post(state, HW2SW_EQ, &eqc_entry,
	    sizeof (mcxnex_hw_eqc_t), eqnum, DDI_NOSLEEP);
	if (status != MCXNEX_CMD_SUCCESS) {
		MCXNEX_WARNING(state, "failed to reclaim EQC ownership");
		cmn_err(CE_CONT, "Mcxnex: HW2SW_EQ command failed: %08x\n",
		    status);
		return (DDI_FAILURE);
	}

	/*
	 * Deregister the memory for the Event Queue.  If this fails
	 * for any reason, then it is an indication that something (either
	 * in HW or SW) has gone seriously wrong.  So we print a warning
	 * message and continue.
	 */
	status = mcxnex_mr_deregister(state, &mr, MCXNEX_MR_DEREG_ALL,
	    DDI_NOSLEEP);
	if (status != DDI_SUCCESS) {
		MCXNEX_WARNING(state, "failed to deregister EQ memory");
	}

	/* Free the memory for the EQ */
	mcxnex_queue_free(&eq->eq_eqinfo);

	/* Free the Mcxnex Event Queue handle */
	mcxnex_rsrc_free(state, &rsrc);

	/* Free up the EQC entry resource */
	mcxnex_rsrc_free(state, &eqc);

	/* Decrement the reference count on the protection domain (PD) */
	mcxnex_pd_refcnt_dec(pd);

	/* Set the eqhdl pointer to NULL and return success */
	*eqhdl = NULL;

	return (DDI_SUCCESS);
}


/*
 * mcxnex_eq_handler_init
 *    Context: Only called from attach() path context
 */
static int
mcxnex_eq_handler_init(mcxnex_state_t *state, mcxnex_eqhdl_t eq,
    uint_t evt_type_mask, int (*eq_func)(mcxnex_state_t *state,
    mcxnex_eqhdl_t eq, mcxnex_hw_eqe_t *eqe))
{
	int		status;

	/*
	 * Save away the EQ handler function and the event type mask.  These
	 * will be used later during interrupt and event queue processing.
	 */
	eq->eq_func	   = eq_func;
	eq->eq_evttypemask = evt_type_mask;

	/*
	 * Map the EQ to a specific class of event (or events) depending
	 * on the mask value passed in.  The MCXNEX_EVT_NO_MASK means not
	 * to attempt associating the EQ with any specific class of event.
	 * This is particularly useful when initializing the events queues
	 * used for CQ events.   The mapping is done using the Mcxnex MAP_EQ
	 * firmware command.  Note: This command should not, in general, fail.
	 * If it does, then something (probably HW related) has gone seriously
	 * wrong.
	 */
	if (evt_type_mask != MCXNEX_EVT_NO_MASK) {
		status = mcxnex_map_eq_cmd_post(state,
		    MCXNEX_CMD_MAP_EQ_EVT_MAP, eq->eq_eqnum, evt_type_mask,
		    DDI_NOSLEEP);
		if (status != MCXNEX_CMD_SUCCESS) {
			cmn_err(CE_NOTE, "mcxnex%d: MAP_EQ command failed: "
			    "%08x\n", state->hs_instance, status);
			return (DDI_FAILURE);
		}
	}

	return (DDI_SUCCESS);
}


/*
 * mcxnex_eq_handler_fini
 *    Context: Only called from attach() and/or detach() path contexts
 */
static int
mcxnex_eq_handler_fini(mcxnex_state_t *state, mcxnex_eqhdl_t eq)
{
	int			status;

	/*
	 * Unmap the EQ from the event class to which it had been previously
	 * mapped.  The unmapping is done using the Mcxnex MAP_EQ (in much
	 * the same way that the initial mapping was done).  The difference,
	 * however, is in the MCXNEX_EQ_EVT_UNMAP flag that is passed to the
	 * MAP_EQ firmware command.  The MCXNEX_EVT_NO_MASK (which may have
	 * been passed in at init time) still means that no association has
	 * been made between the EQ and any specific class of event (and,
	 * hence, no unmapping is necessary).  Note: This command should not,
	 * in general, fail.  If it does, then something (probably HW related)
	 * has gone seriously wrong.
	 */
	if (eq->eq_evttypemask != MCXNEX_EVT_NO_MASK) {
		status = mcxnex_map_eq_cmd_post(state,
		    MCXNEX_CMD_MAP_EQ_EVT_UNMAP, eq->eq_eqnum,
		    eq->eq_evttypemask, DDI_NOSLEEP);
		if (status != MCXNEX_CMD_SUCCESS) {
			cmn_err(CE_NOTE, "mcxnex%d: MAP_EQ command failed: "
			    "%08x\n", state->hs_instance, status);
			return (DDI_FAILURE);
		}
	}

	return (DDI_SUCCESS);
}


/*
 * mcxnex_eq_demux()
 *	Context: Called only from interrupt context
 *	Usage:  to demux the various type reported on one EQ
 */
static int
mcxnex_eq_demux(mcxnex_state_t *state, mcxnex_eqhdl_t eq,
    mcxnex_hw_eqe_t *eqe)
{
	uint_t			eqe_evttype;
	int			status = DDI_FAILURE;

	eqe_evttype = MCXNEX_EQE_EVTTYPE_GET(eq, eqe);

	switch (eqe_evttype) {

	case MCXNEX_EVT_PORT_STATE_CHANGE:
		status = mcxnex_port_state_change_handler(state, eq, eqe);
		break;

	case MCXNEX_EVT_COMM_ESTABLISHED:
		status = mcxnex_comm_estbl_handler(state, eq, eqe);
		break;

	case MCXNEX_EVT_COMMAND_INTF_COMP:
		status = mcxnex_cmd_complete_handler(state, eq, eqe);
		break;

	case MCXNEX_EVT_LOCAL_WQ_CAT_ERROR:
		MCXNEX_FMANOTE(state, MCXNEX_FMA_LOCCAT);
		status = mcxnex_local_wq_cat_err_handler(state, eq, eqe);
		break;

	case MCXNEX_EVT_INV_REQ_LOCAL_WQ_ERROR:
		MCXNEX_FMANOTE(state, MCXNEX_FMA_LOCINV);
		status = mcxnex_invreq_local_wq_err_handler(state, eq, eqe);
		break;

	case MCXNEX_EVT_LOCAL_ACC_VIO_WQ_ERROR:
		MCXNEX_FMANOTE(state, MCXNEX_FMA_LOCACEQ);
		status = mcxnex_local_acc_vio_wq_err_handler(state, eq, eqe);
		break;
	case MCXNEX_EVT_SEND_QUEUE_DRAINED:
		status = mcxnex_sendq_drained_handler(state, eq, eqe);
		break;

	case MCXNEX_EVT_PATH_MIGRATED:
		status = mcxnex_path_mig_handler(state, eq, eqe);
		break;

	case MCXNEX_EVT_PATH_MIGRATE_FAILED:
		MCXNEX_FMANOTE(state, MCXNEX_FMA_PATHMIG);
		status = mcxnex_path_mig_err_handler(state, eq, eqe);
		break;

	case MCXNEX_EVT_SRQ_CATASTROPHIC_ERROR:
		MCXNEX_FMANOTE(state, MCXNEX_FMA_SRQCAT);
		status = mcxnex_catastrophic_handler(state, eq, eqe);
		break;

	case MCXNEX_EVT_SRQ_LAST_WQE_REACHED:
		status = mcxnex_srq_last_wqe_reached_handler(state, eq, eqe);
		break;

	case MCXNEX_EVT_ECC_DETECTION:
		status = mcxnex_ecc_detection_handler(state, eq, eqe);
		break;

	default:
		break;
	}
	return (status);
}

/*
 * mcxnex_port_state_change_handler()
 *    Context: Only called from interrupt context
 */
static int
mcxnex_port_state_change_handler(mcxnex_state_t *state, mcxnex_eqhdl_t eq,
    mcxnex_hw_eqe_t *eqe)
{
	ibc_async_event_t	event;
	ibt_async_code_t	type;
	uint_t			subtype;
	uint8_t			port;
	uint_t			eqe_evttype;
	char			link_msg[24];

	eqe_evttype = MCXNEX_EQE_EVTTYPE_GET(eq, eqe);

	ASSERT(eqe_evttype == MCXNEX_EVT_PORT_STATE_CHANGE ||
	    eqe_evttype == MCXNEX_EVT_EQ_OVERFLOW);

	if (eqe_evttype == MCXNEX_EVT_EQ_OVERFLOW) {
		mcxnex_eq_overflow_handler(state, eq, eqe);
		return (DDI_FAILURE);
	}

	/*
	 * Depending on the type of Port State Change event, pass the
	 * appropriate asynch event to the IBTF.
	 */
	port = (uint8_t)MCXNEX_EQE_PORTNUM_GET(eq, eqe);

	/* Check for valid port number in event */
	if ((port == 0) || (port > state->hs_cfg_profile->cp_num_ports)) {
		MCXNEX_WARNING(state, "Unexpected port number in port state "
		    "change event");
		cmn_err(CE_CONT, "  Port number: %02x\n", port);
		return (DDI_FAILURE);
	}

	subtype = MCXNEX_EQE_EVTSUBTYPE_GET(eq, eqe);
	if (subtype == MCXNEX_PORT_LINK_ACTIVE) {
		event.ev_port 	= port;
		type		= IBT_EVENT_PORT_UP;
		if (mcxnex_verbose) {
			(void) snprintf(link_msg, 23, "port %d up", port);
			ddi_dev_report_fault(state->hs_dip,
			    DDI_SERVICE_RESTORED, DDI_EXTERNAL_FAULT, link_msg);
		}
	} else if (subtype == MCXNEX_PORT_LINK_DOWN) {
		event.ev_port	= port;
		type		= IBT_ERROR_PORT_DOWN;
		if (mcxnex_verbose) {
			(void) snprintf(link_msg, 23, "port %d down", port);
			ddi_dev_report_fault(state->hs_dip, DDI_SERVICE_LOST,
			    DDI_EXTERNAL_FAULT, link_msg);
		}
	} else {
		MCXNEX_WARNING(state, "Unexpected subtype in port state change "
		    "event");
		cmn_err(CE_CONT, "  Event type: %02x, subtype: %02x\n",
		    MCXNEX_EQE_EVTTYPE_GET(eq, eqe), subtype);
		return (DDI_FAILURE);
	}

	/*
	 * Deliver the event to the IBTF.  Note: If "hs_ibtfpriv" is NULL,
	 * then we have either received this event before we finished
	 * attaching to the IBTF or we've received it while we are in the
	 * process of detaching.
	 */
	if (state->hs_ibtfpriv != NULL) {
		MCXNEX_DO_IBTF_ASYNC_CALLB(state, type, &event);
	}

	return (DDI_SUCCESS);
}


/*
 * mcxnex_comm_estbl_handler()
 *    Context: Only called from interrupt context
 */
static int
mcxnex_comm_estbl_handler(mcxnex_state_t *state, mcxnex_eqhdl_t eq,
    mcxnex_hw_eqe_t *eqe)
{
	mcxnex_qphdl_t		qp;
	uint_t			qpnum;
	ibc_async_event_t	event;
	ibt_async_code_t	type;
	uint_t			eqe_evttype;

	eqe_evttype = MCXNEX_EQE_EVTTYPE_GET(eq, eqe);

	ASSERT(eqe_evttype == MCXNEX_EVT_COMM_ESTABLISHED ||
	    eqe_evttype == MCXNEX_EVT_EQ_OVERFLOW);

	if (eqe_evttype == MCXNEX_EVT_EQ_OVERFLOW) {
		mcxnex_eq_overflow_handler(state, eq, eqe);
		return (DDI_FAILURE);
	}

	/* Get the QP handle from QP number in event descriptor */
	qpnum = MCXNEX_EQE_QPNUM_GET(eq, eqe);
	qp = mcxnex_qphdl_from_qpnum(state, qpnum);

	/*
	 * If the QP handle is NULL, this is probably an indication
	 * that the QP has been freed already.  In which case, we
	 * should not deliver this event.
	 *
	 * We also check that the QP number in the handle is the
	 * same as the QP number in the event queue entry.  This
	 * extra check allows us to handle the case where a QP was
	 * freed and then allocated again in the time it took to
	 * handle the event queue processing.  By constantly incrementing
	 * the non-constrained portion of the QP number every time
	 * a new QP is allocated, we mitigate (somewhat) the chance
	 * that a stale event could be passed to the client's QP
	 * handler.
	 *
	 * Lastly, we check if "hs_ibtfpriv" is NULL.  If it is then it
	 * means that we've have either received this event before we
	 * finished attaching to the IBTF or we've received it while we
	 * are in the process of detaching.
	 */
	if ((qp != NULL) && (qp->qp_qpnum == qpnum) &&
	    (state->hs_ibtfpriv != NULL)) {
		event.ev_qp_hdl = (ibtl_qp_hdl_t)qp->qp_hdlrarg;
		type		= IBT_EVENT_COM_EST_QP;

		MCXNEX_DO_IBTF_ASYNC_CALLB(state, type, &event);
	}

	return (DDI_SUCCESS);
}


/*
 * mcxnex_local_wq_cat_err_handler()
 *    Context: Only called from interrupt context
 */
static int
mcxnex_local_wq_cat_err_handler(mcxnex_state_t *state, mcxnex_eqhdl_t eq,
    mcxnex_hw_eqe_t *eqe)
{
	mcxnex_qphdl_t		qp;
	uint_t			qpnum;
	ibc_async_event_t	event;
	ibt_async_code_t	type;
	uint_t			eqe_evttype;

	eqe_evttype = MCXNEX_EQE_EVTTYPE_GET(eq, eqe);

	ASSERT(eqe_evttype == MCXNEX_EVT_LOCAL_WQ_CAT_ERROR ||
	    eqe_evttype == MCXNEX_EVT_EQ_OVERFLOW);

	if (eqe_evttype == MCXNEX_EVT_EQ_OVERFLOW) {
		mcxnex_eq_overflow_handler(state, eq, eqe);
		return (DDI_FAILURE);
	}

	/* Get the QP handle from QP number in event descriptor */
	qpnum = MCXNEX_EQE_QPNUM_GET(eq, eqe);
	qp = mcxnex_qphdl_from_qpnum(state, qpnum);

	/*
	 * If the QP handle is NULL, this is probably an indication
	 * that the QP has been freed already.  In which case, we
	 * should not deliver this event.
	 *
	 * We also check that the QP number in the handle is the
	 * same as the QP number in the event queue entry.  This
	 * extra check allows us to handle the case where a QP was
	 * freed and then allocated again in the time it took to
	 * handle the event queue processing.  By constantly incrementing
	 * the non-constrained portion of the QP number every time
	 * a new QP is allocated, we mitigate (somewhat) the chance
	 * that a stale event could be passed to the client's QP
	 * handler.
	 *
	 * Lastly, we check if "hs_ibtfpriv" is NULL.  If it is then it
	 * means that we've have either received this event before we
	 * finished attaching to the IBTF or we've received it while we
	 * are in the process of detaching.
	 */
	if ((qp != NULL) && (qp->qp_qpnum == qpnum) &&
	    (state->hs_ibtfpriv != NULL)) {
		event.ev_qp_hdl = (ibtl_qp_hdl_t)qp->qp_hdlrarg;
		type		= IBT_ERROR_CATASTROPHIC_QP;

		MCXNEX_DO_IBTF_ASYNC_CALLB(state, type, &event);
	}

	return (DDI_SUCCESS);
}


/*
 * mcxnex_invreq_local_wq_err_handler()
 *    Context: Only called from interrupt context
 */
static int
mcxnex_invreq_local_wq_err_handler(mcxnex_state_t *state, mcxnex_eqhdl_t eq,
    mcxnex_hw_eqe_t *eqe)
{
	mcxnex_qphdl_t		qp;
	uint_t			qpnum;
	ibc_async_event_t	event;
	ibt_async_code_t	type;
	uint_t			eqe_evttype;

	eqe_evttype = MCXNEX_EQE_EVTTYPE_GET(eq, eqe);

	ASSERT(eqe_evttype == MCXNEX_EVT_INV_REQ_LOCAL_WQ_ERROR ||
	    eqe_evttype == MCXNEX_EVT_EQ_OVERFLOW);

	if (eqe_evttype == MCXNEX_EVT_EQ_OVERFLOW) {
		mcxnex_eq_overflow_handler(state, eq, eqe);
		return (DDI_FAILURE);
	}

	/* Get the QP handle from QP number in event descriptor */
	qpnum = MCXNEX_EQE_QPNUM_GET(eq, eqe);
	qp = mcxnex_qphdl_from_qpnum(state, qpnum);

	/*
	 * If the QP handle is NULL, this is probably an indication
	 * that the QP has been freed already.  In which case, we
	 * should not deliver this event.
	 *
	 * We also check that the QP number in the handle is the
	 * same as the QP number in the event queue entry.  This
	 * extra check allows us to handle the case where a QP was
	 * freed and then allocated again in the time it took to
	 * handle the event queue processing.  By constantly incrementing
	 * the non-constrained portion of the QP number every time
	 * a new QP is allocated, we mitigate (somewhat) the chance
	 * that a stale event could be passed to the client's QP
	 * handler.
	 *
	 * Lastly, we check if "hs_ibtfpriv" is NULL.  If it is then it
	 * means that we've have either received this event before we
	 * finished attaching to the IBTF or we've received it while we
	 * are in the process of detaching.
	 */
	if ((qp != NULL) && (qp->qp_qpnum == qpnum) &&
	    (state->hs_ibtfpriv != NULL)) {
		event.ev_qp_hdl = (ibtl_qp_hdl_t)qp->qp_hdlrarg;
		type		= IBT_ERROR_INVALID_REQUEST_QP;

		MCXNEX_DO_IBTF_ASYNC_CALLB(state, type, &event);
	}

	return (DDI_SUCCESS);
}


/*
 * mcxnex_local_acc_vio_wq_err_handler()
 *    Context: Only called from interrupt context
 */
static int
mcxnex_local_acc_vio_wq_err_handler(mcxnex_state_t *state, mcxnex_eqhdl_t eq,
    mcxnex_hw_eqe_t *eqe)
{
	mcxnex_qphdl_t		qp;
	uint_t			qpnum;
	ibc_async_event_t	event;
	ibt_async_code_t	type;
	uint_t			eqe_evttype;

	eqe_evttype = MCXNEX_EQE_EVTTYPE_GET(eq, eqe);

	ASSERT(eqe_evttype == MCXNEX_EVT_LOCAL_ACC_VIO_WQ_ERROR ||
	    eqe_evttype == MCXNEX_EVT_EQ_OVERFLOW);

	if (eqe_evttype == MCXNEX_EVT_EQ_OVERFLOW) {
		mcxnex_eq_overflow_handler(state, eq, eqe);
		return (DDI_FAILURE);
	}

	/* Get the QP handle from QP number in event descriptor */
	qpnum = MCXNEX_EQE_QPNUM_GET(eq, eqe);
	qp = mcxnex_qphdl_from_qpnum(state, qpnum);

	/*
	 * If the QP handle is NULL, this is probably an indication
	 * that the QP has been freed already.  In which case, we
	 * should not deliver this event.
	 *
	 * We also check that the QP number in the handle is the
	 * same as the QP number in the event queue entry.  This
	 * extra check allows us to handle the case where a QP was
	 * freed and then allocated again in the time it took to
	 * handle the event queue processing.  By constantly incrementing
	 * the non-constrained portion of the QP number every time
	 * a new QP is allocated, we mitigate (somewhat) the chance
	 * that a stale event could be passed to the client's QP
	 * handler.
	 *
	 * Lastly, we check if "hs_ibtfpriv" is NULL.  If it is then it
	 * means that we've have either received this event before we
	 * finished attaching to the IBTF or we've received it while we
	 * are in the process of detaching.
	 */
	if ((qp != NULL) && (qp->qp_qpnum == qpnum) &&
	    (state->hs_ibtfpriv != NULL)) {
		event.ev_qp_hdl = (ibtl_qp_hdl_t)qp->qp_hdlrarg;
		type		= IBT_ERROR_ACCESS_VIOLATION_QP;

		MCXNEX_DO_IBTF_ASYNC_CALLB(state, type, &event);
	}

	return (DDI_SUCCESS);
}


/*
 * mcxnex_sendq_drained_handler()
 *    Context: Only called from interrupt context
 */
static int
mcxnex_sendq_drained_handler(mcxnex_state_t *state, mcxnex_eqhdl_t eq,
    mcxnex_hw_eqe_t *eqe)
{
	mcxnex_qphdl_t		qp;
	uint_t			qpnum;
	ibc_async_event_t	event;
	uint_t			forward_sqd_event;
	ibt_async_code_t	type;
	uint_t			eqe_evttype;

	eqe_evttype = MCXNEX_EQE_EVTTYPE_GET(eq, eqe);

	ASSERT(eqe_evttype == MCXNEX_EVT_SEND_QUEUE_DRAINED ||
	    eqe_evttype == MCXNEX_EVT_EQ_OVERFLOW);

	if (eqe_evttype == MCXNEX_EVT_EQ_OVERFLOW) {
		mcxnex_eq_overflow_handler(state, eq, eqe);
		return (DDI_FAILURE);
	}

	/* Get the QP handle from QP number in event descriptor */
	qpnum = MCXNEX_EQE_QPNUM_GET(eq, eqe);
	qp = mcxnex_qphdl_from_qpnum(state, qpnum);

	/*
	 * If the QP handle is NULL, this is probably an indication
	 * that the QP has been freed already.  In which case, we
	 * should not deliver this event.
	 *
	 * We also check that the QP number in the handle is the
	 * same as the QP number in the event queue entry.  This
	 * extra check allows us to handle the case where a QP was
	 * freed and then allocated again in the time it took to
	 * handle the event queue processing.  By constantly incrementing
	 * the non-constrained portion of the QP number every time
	 * a new QP is allocated, we mitigate (somewhat) the chance
	 * that a stale event could be passed to the client's QP
	 * handler.
	 *
	 * And then we check if "hs_ibtfpriv" is NULL.  If it is then it
	 * means that we've have either received this event before we
	 * finished attaching to the IBTF or we've received it while we
	 * are in the process of detaching.
	 */
	if ((qp != NULL) && (qp->qp_qpnum == qpnum) &&
	    (state->hs_ibtfpriv != NULL)) {
		event.ev_qp_hdl = (ibtl_qp_hdl_t)qp->qp_hdlrarg;
		type		= IBT_EVENT_SQD;

		/*
		 * Grab the QP lock and update the QP state to reflect that
		 * the Send Queue Drained event has arrived.  Also determine
		 * whether the event is intended to be forwarded on to the
		 * consumer or not.  This information is used below in
		 * determining whether or not to call the IBTF.
		 */
		mutex_enter(&qp->qp_lock);
		forward_sqd_event = qp->qp_forward_sqd_event;
		qp->qp_forward_sqd_event  = 0;
		qp->qp_sqd_still_draining = 0;
		mutex_exit(&qp->qp_lock);

		if (forward_sqd_event != 0) {
			MCXNEX_DO_IBTF_ASYNC_CALLB(state, type, &event);
		}
	}

	return (DDI_SUCCESS);
}


/*
 * mcxnex_path_mig_handler()
 *    Context: Only called from interrupt context
 */
static int
mcxnex_path_mig_handler(mcxnex_state_t *state, mcxnex_eqhdl_t eq,
    mcxnex_hw_eqe_t *eqe)
{
	mcxnex_qphdl_t		qp;
	uint_t			qpnum;
	ibc_async_event_t	event;
	ibt_async_code_t	type;
	uint_t			eqe_evttype;

	eqe_evttype = MCXNEX_EQE_EVTTYPE_GET(eq, eqe);

	ASSERT(eqe_evttype == MCXNEX_EVT_PATH_MIGRATED ||
	    eqe_evttype == MCXNEX_EVT_EQ_OVERFLOW);

	if (eqe_evttype == MCXNEX_EVT_EQ_OVERFLOW) {
		mcxnex_eq_overflow_handler(state, eq, eqe);
		return (DDI_FAILURE);
	}

	/* Get the QP handle from QP number in event descriptor */
	qpnum = MCXNEX_EQE_QPNUM_GET(eq, eqe);
	qp = mcxnex_qphdl_from_qpnum(state, qpnum);

	/*
	 * If the QP handle is NULL, this is probably an indication
	 * that the QP has been freed already.  In which case, we
	 * should not deliver this event.
	 *
	 * We also check that the QP number in the handle is the
	 * same as the QP number in the event queue entry.  This
	 * extra check allows us to handle the case where a QP was
	 * freed and then allocated again in the time it took to
	 * handle the event queue processing.  By constantly incrementing
	 * the non-constrained portion of the QP number every time
	 * a new QP is allocated, we mitigate (somewhat) the chance
	 * that a stale event could be passed to the client's QP
	 * handler.
	 *
	 * Lastly, we check if "hs_ibtfpriv" is NULL.  If it is then it
	 * means that we've have either received this event before we
	 * finished attaching to the IBTF or we've received it while we
	 * are in the process of detaching.
	 */
	if ((qp != NULL) && (qp->qp_qpnum == qpnum) &&
	    (state->hs_ibtfpriv != NULL)) {
		event.ev_qp_hdl = (ibtl_qp_hdl_t)qp->qp_hdlrarg;
		type		= IBT_EVENT_PATH_MIGRATED_QP;

		MCXNEX_DO_IBTF_ASYNC_CALLB(state, type, &event);
	}

	return (DDI_SUCCESS);
}


/*
 * mcxnex_path_mig_err_handler()
 *    Context: Only called from interrupt context
 */
static int
mcxnex_path_mig_err_handler(mcxnex_state_t *state, mcxnex_eqhdl_t eq,
    mcxnex_hw_eqe_t *eqe)
{
	mcxnex_qphdl_t		qp;
	uint_t			qpnum;
	ibc_async_event_t	event;
	ibt_async_code_t	type;
	uint_t			eqe_evttype;

	eqe_evttype = MCXNEX_EQE_EVTTYPE_GET(eq, eqe);

	ASSERT(eqe_evttype == MCXNEX_EVT_PATH_MIGRATE_FAILED ||
	    eqe_evttype == MCXNEX_EVT_EQ_OVERFLOW);

	if (eqe_evttype == MCXNEX_EVT_EQ_OVERFLOW) {
		mcxnex_eq_overflow_handler(state, eq, eqe);
		return (DDI_FAILURE);
	}

	/* Get the QP handle from QP number in event descriptor */
	qpnum = MCXNEX_EQE_QPNUM_GET(eq, eqe);
	qp = mcxnex_qphdl_from_qpnum(state, qpnum);

	/*
	 * If the QP handle is NULL, this is probably an indication
	 * that the QP has been freed already.  In which case, we
	 * should not deliver this event.
	 *
	 * We also check that the QP number in the handle is the
	 * same as the QP number in the event queue entry.  This
	 * extra check allows us to handle the case where a QP was
	 * freed and then allocated again in the time it took to
	 * handle the event queue processing.  By constantly incrementing
	 * the non-constrained portion of the QP number every time
	 * a new QP is allocated, we mitigate (somewhat) the chance
	 * that a stale event could be passed to the client's QP
	 * handler.
	 *
	 * Lastly, we check if "hs_ibtfpriv" is NULL.  If it is then it
	 * means that we've have either received this event before we
	 * finished attaching to the IBTF or we've received it while we
	 * are in the process of detaching.
	 */
	if ((qp != NULL) && (qp->qp_qpnum == qpnum) &&
	    (state->hs_ibtfpriv != NULL)) {
		event.ev_qp_hdl = (ibtl_qp_hdl_t)qp->qp_hdlrarg;
		type		= IBT_ERROR_PATH_MIGRATE_REQ_QP;

		MCXNEX_DO_IBTF_ASYNC_CALLB(state, type, &event);
	}

	return (DDI_SUCCESS);
}


/*
 * mcxnex_catastrophic_handler()
 *    Context: Only called from interrupt context
 */
static int
mcxnex_catastrophic_handler(mcxnex_state_t *state, mcxnex_eqhdl_t eq,
    mcxnex_hw_eqe_t *eqe)
{
	mcxnex_qphdl_t		qp;
	uint_t			qpnum;
	ibc_async_event_t	event;
	ibt_async_code_t	type;
	uint_t			eqe_evttype;

	if (eq->eq_evttypemask == MCXNEX_EVT_MSK_LOCAL_CAT_ERROR) {
		MCXNEX_FMANOTE(state, MCXNEX_FMA_INTERNAL);
		mcxnex_eq_catastrophic(state);
		return (DDI_SUCCESS);
	}

	eqe_evttype = MCXNEX_EQE_EVTTYPE_GET(eq, eqe);

	ASSERT(eqe_evttype == MCXNEX_EVT_SRQ_CATASTROPHIC_ERROR ||
	    eqe_evttype == MCXNEX_EVT_EQ_OVERFLOW);

	if (eqe_evttype == MCXNEX_EVT_EQ_OVERFLOW) {
		mcxnex_eq_overflow_handler(state, eq, eqe);
		return (DDI_FAILURE);
	}

	/* Get the QP handle from QP number in event descriptor */
	qpnum = MCXNEX_EQE_QPNUM_GET(eq, eqe);
	qp = mcxnex_qphdl_from_qpnum(state, qpnum);

	/*
	 * If the QP handle is NULL, this is probably an indication
	 * that the QP has been freed already.  In which case, we
	 * should not deliver this event.
	 *
	 * We also check that the QP number in the handle is the
	 * same as the QP number in the event queue entry.  This
	 * extra check allows us to handle the case where a QP was
	 * freed and then allocated again in the time it took to
	 * handle the event queue processing.  By constantly incrementing
	 * the non-constrained portion of the QP number every time
	 * a new QP is allocated, we mitigate (somewhat) the chance
	 * that a stale event could be passed to the client's QP
	 * handler.
	 *
	 * Lastly, we check if "hs_ibtfpriv" is NULL.  If it is then it
	 * means that we've have either received this event before we
	 * finished attaching to the IBTF or we've received it while we
	 * are in the process of detaching.
	 */
	if ((qp != NULL) && (qp->qp_qpnum == qpnum) &&
	    (state->hs_ibtfpriv != NULL)) {
		event.ev_srq_hdl = (ibt_srq_hdl_t)qp->qp_srqhdl->srq_hdlrarg;
		type		= IBT_ERROR_CATASTROPHIC_SRQ;

		mutex_enter(&qp->qp_srqhdl->srq_lock);
		qp->qp_srqhdl->srq_state = MCXNEX_SRQ_STATE_ERROR;
		mutex_exit(&qp->qp_srqhdl->srq_lock);

		MCXNEX_DO_IBTF_ASYNC_CALLB(state, type, &event);
	}

	return (DDI_SUCCESS);
}


/*
 * mcxnex_srq_last_wqe_reached_handler()
 *    Context: Only called from interrupt context
 */
static int
mcxnex_srq_last_wqe_reached_handler(mcxnex_state_t *state, mcxnex_eqhdl_t eq,
    mcxnex_hw_eqe_t *eqe)
{
	mcxnex_qphdl_t		qp;
	uint_t			qpnum;
	ibc_async_event_t	event;
	ibt_async_code_t	type;
	uint_t			eqe_evttype;

	eqe_evttype = MCXNEX_EQE_EVTTYPE_GET(eq, eqe);

	ASSERT(eqe_evttype == MCXNEX_EVT_SRQ_LAST_WQE_REACHED ||
	    eqe_evttype == MCXNEX_EVT_EQ_OVERFLOW);

	if (eqe_evttype == MCXNEX_EVT_EQ_OVERFLOW) {
		mcxnex_eq_overflow_handler(state, eq, eqe);
		return (DDI_FAILURE);
	}

	/* Get the QP handle from QP number in event descriptor */
	qpnum = MCXNEX_EQE_QPNUM_GET(eq, eqe);
	qp = mcxnex_qphdl_from_qpnum(state, qpnum);

	/*
	 * If the QP handle is NULL, this is probably an indication
	 * that the QP has been freed already.  In which case, we
	 * should not deliver this event.
	 *
	 * We also check that the QP number in the handle is the
	 * same as the QP number in the event queue entry.  This
	 * extra check allows us to handle the case where a QP was
	 * freed and then allocated again in the time it took to
	 * handle the event queue processing.  By constantly incrementing
	 * the non-constrained portion of the QP number every time
	 * a new QP is allocated, we mitigate (somewhat) the chance
	 * that a stale event could be passed to the client's QP
	 * handler.
	 *
	 * Lastly, we check if "hs_ibtfpriv" is NULL.  If it is then it
	 * means that we've have either received this event before we
	 * finished attaching to the IBTF or we've received it while we
	 * are in the process of detaching.
	 */
	if ((qp != NULL) && (qp->qp_qpnum == qpnum) &&
	    (state->hs_ibtfpriv != NULL)) {
		event.ev_qp_hdl = (ibtl_qp_hdl_t)qp->qp_hdlrarg;
		type		= IBT_EVENT_EMPTY_CHAN;

		MCXNEX_DO_IBTF_ASYNC_CALLB(state, type, &event);
	}

	return (DDI_SUCCESS);
}


/*
 * mcxnex_ecc_detection_handler()
 *    Context: Only called from interrupt context
 */
static int
mcxnex_ecc_detection_handler(mcxnex_state_t *state, mcxnex_eqhdl_t eq,
    mcxnex_hw_eqe_t *eqe)
{
	uint_t			eqe_evttype;
	uint_t			data;
	int			i;

	eqe_evttype = MCXNEX_EQE_EVTTYPE_GET(eq, eqe);

	ASSERT(eqe_evttype == MCXNEX_EVT_ECC_DETECTION ||
	    eqe_evttype == MCXNEX_EVT_EQ_OVERFLOW);

	if (eqe_evttype == MCXNEX_EVT_EQ_OVERFLOW) {
		mcxnex_eq_overflow_handler(state, eq, eqe);
		return (DDI_FAILURE);
	}

	/*
	 * The "ECC Detection Event" indicates that a correctable single-bit
	 * has occurred with the attached DDR.  The EQE provides some
	 * additional information about the errored EQ.  So we print a warning
	 * message here along with that additional information.
	 */
	MCXNEX_WARNING(state, "ECC Correctable Error Event Detected");
	for (i = 0; i < sizeof (mcxnex_hw_eqe_t) >> 2; i++) {
		data = ((uint_t *)eqe)[i];
		cmn_err(CE_CONT, "!  EQE[%02x]: %08x\n", i, data);
	}

	return (DDI_SUCCESS);
}


/*
 * mcxnex_eq_overflow_handler()
 *    Context: Only called from interrupt context
 */
/* ARGSUSED */
void
mcxnex_eq_overflow_handler(mcxnex_state_t *state, mcxnex_eqhdl_t eq,
    mcxnex_hw_eqe_t *eqe)
{
	uint_t		error_type, data;

	ASSERT(MCXNEX_EQE_EVTTYPE_GET(eq, eqe) == MCXNEX_EVT_EQ_OVERFLOW);

	/*
	 * The "Event Queue Overflow Event" indicates that something has
	 * probably gone seriously wrong with some hardware (or, perhaps,
	 * with the software... though it's unlikely in this case).  The EQE
	 * provides some additional information about the errored EQ.  So we
	 * print a warning message here along with that additional information.
	 */
	error_type = MCXNEX_EQE_OPERRTYPE_GET(eq, eqe);
	data	   = MCXNEX_EQE_OPERRDATA_GET(eq, eqe);

	MCXNEX_WARNING(state, "Event Queue overflow");
	cmn_err(CE_CONT, "  Error type: %02x, data: %08x\n", error_type, data);
}


/*
 * mcxnex_no_eqhandler
 *    Context: Only called from interrupt context
 */
/* ARGSUSED */
static int
mcxnex_no_eqhandler(mcxnex_state_t *state, mcxnex_eqhdl_t eq,
    mcxnex_hw_eqe_t *eqe)
{
	uint_t		data;
	int		i;

	/*
	 * This "unexpected event" handler (or "catch-all" handler) will
	 * receive all events for which no other handler has been registered.
	 * If we end up here, then something has probably gone seriously wrong
	 * with the Mcxnex hardware (or, perhaps, with the software... though
	 * it's unlikely in this case).  The EQE provides all the information
	 * about the event.  So we print a warning message here along with
	 * the contents of the EQE.
	 */
	MCXNEX_WARNING(state, "Unexpected Event handler");
	cmn_err(CE_CONT, "  Event type: %02x, subtype: %02x\n",
	    MCXNEX_EQE_EVTTYPE_GET(eq, eqe),
	    MCXNEX_EQE_EVTSUBTYPE_GET(eq, eqe));
	for (i = 0; i < sizeof (mcxnex_hw_eqe_t) >> 2; i++) {
		data = ((uint_t *)eqe)[i];
		cmn_err(CE_CONT, "  EQE[%02x]: %08x\n", i, data);
	}

	return (DDI_SUCCESS);
}
