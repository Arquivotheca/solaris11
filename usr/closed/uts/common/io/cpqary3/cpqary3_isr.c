/*
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copyright 2009, 2011, Hewlett-Packard Development Company, L.P.
 */

/*
 * Module Name:
 *    CPQARY3_ISR.C
 * Abstract:
 *	This file contains the hardware and software interrupt handler.
 *	It has routines which process the completed commands.
 */

#include	"cpqary3.h"

/*
 * Local Functions Definitions
 */



/*
 *	Function     : 	cpqary3_hw_isr
 *	Description  : 	This routine determines if this instance of the
 *			HBA interrupted and if positive triggers a software
 *			interrupt.
 *			For SAS controllers which operate in performant mode
 *			we clear the interrupt.
 *			For CISS controllers which operate in simple mode
 *			we get the tag value.
 *	Called By    :	kernel
 *	Parameters   : 	per-controller
 *	Calls        : 	cpqary3_check_ctlr_intr()
 *	Return Values: 	DDI_INTR_CLAIMED/UNCLAIMED
 *			[We either CLAIM the interrupt or Discard it]
 */
uint_t
cpqary3_hw_isr(caddr_t per_ctlr)
{
	uint8_t			need_swintr;
	cpqary3_t		*cpqary3p;
	cpqary3_drvr_replyq_t	*replyq_ptr;
	volatile CfgTable_t	*ctp;
	uint32_t		spr0;
	uint32_t		doorbell_status;
	uint32_t		tag;
	uint32_t ret = DDI_INTR_CLAIMED;

#ifdef ISR_DEBUG
	cmn_err(CE_CONT, "CPQary3: _hw_isr : Entering \n");
#endif

	cpqary3p = (cpqary3_t *)(void *)per_ctlr;

	if (!cpqary3p) {
		cmn_err(CE_PANIC, "CPQary3 : Interrupt Service Routine"
		    " invoked with NULL pointer argument \n");
	}

	ctp = (CfgTable_t *)cpqary3p->ct;
	replyq_ptr = (cpqary3_drvr_replyq_t *)cpqary3p->drvr_replyq;

	if (CPQARY3_FAILURE == cpqary3p->check_ctlr_intr(cpqary3p)) {
		if (cpqary3p->heartbeat == DDI_GET32(cpqary3p,
		    &ctp->HeartBeat)) {
			if (cpqary3_check_acc_handle(cpqary3p->ct_handle)
			    != DDI_SUCCESS) {
				ddi_fm_service_impact(cpqary3p->dip,
				    DDI_SERVICE_LOST);
			}

			if (0x2 & (ddi_get32(cpqary3p->odr_handle,
			    (uint32_t *)cpqary3p->odr))) {
				spr0 = ddi_get32(cpqary3p->spr0_handle,
				    (uint32_t *)cpqary3p->spr0);
				if (cpqary3_check_acc_handle(cpqary3p->
				    spr0_handle) != DDI_SUCCESS) {
					ddi_fm_service_impact(cpqary3p->dip,
					    DDI_SERVICE_LOST);
				}
				spr0 = spr0 >> 16;
				cmn_err(CE_WARN, "CPQary3 : %s HBA firmware"
				    " Locked !!!  Lockup Code: 0x%x\n",
				    cpqary3p->hba_name, spr0);
				cmn_err(CE_WARN, "CPQary3 : Please reboot"
				    " the system\n");
				ddi_put32(cpqary3p->odr_cl_handle,
				    (uint32_t *)cpqary3p->odr_cl, 0x2);
				cpqary3_intr_onoff(cpqary3p,
				    CPQARY3_INTR_DISABLE);
				if (cpqary3p->host_support & 0x4)
					cpqary3_lockup_intr_onoff(cpqary3p,
					    CPQARY3_LOCKUP_INTR_DISABLE);
				cpqary3p->controller_lockup = CPQARY3_TRUE;
			}
			return (DDI_INTR_CLAIMED);
		}
		if (cpqary3_check_acc_handle(cpqary3p->ct_handle)
		    != DDI_SUCCESS) {
			ddi_fm_service_impact(cpqary3p->dip,
			    DDI_SERVICE_LOST);
		}
		return (DDI_INTR_UNCLAIMED);
	}

	/* PERF */

	/*
	 * We decided that we will have only one retrieve function for
	 * both simple and performant mode. To achieve this we have to
	 * mimic what controller does for performant mode in simple mode.
	 * For simple mode we are making replq_simple_ptr and
	 * replq_headptr of performant
	 * mode point to the same location in the reply queue.
	 * For the performant mode, we clear the interrupt
	 */

	if (!(cpqary3p->bddef->bd_flags & SA_BD_SAS)) {
		while ((tag = ddi_get32(cpqary3p->opq_handle,
		    (uint32_t *)cpqary3p->opq)) != 0xFFFFFFFF) {
			replyq_ptr->replyq_simple_ptr[0] = tag;
			replyq_ptr->replyq_simple_ptr[0] |=
			    replyq_ptr->simple_cyclic_indicator;
			++replyq_ptr->simple_index;
			if (replyq_ptr->simple_index == replyq_ptr->max_index) {
				replyq_ptr->simple_index = 0;
				/* Toggle at wraparound */
				replyq_ptr->simple_cyclic_indicator =
				    (replyq_ptr->simple_cyclic_indicator
				    == 0) ? 1 : 0;
				replyq_ptr->replyq_simple_ptr =
				    (uint32_t *)(void *)
				    (replyq_ptr->replyq_start_addr);
				} else {
					replyq_ptr->replyq_simple_ptr += 2;
				}
		}
	} else {
		doorbell_status = ddi_get32(cpqary3p->odr_handle,
		    (uint32_t *)cpqary3p->odr);
		if (cpqary3_check_acc_handle(cpqary3p->odr_handle) !=
		    DDI_SUCCESS) {
			ddi_fm_service_impact(cpqary3p->dip, DDI_SERVICE_LOST);
			ret = DDI_INTR_CLAIMED;
		}
		if (doorbell_status & 0x1) {
			ddi_put32(cpqary3p->odr_cl_handle,
			    (uint32_t *)cpqary3p->odr_cl,
			    (ddi_get32(cpqary3p->odr_cl_handle,
			    (uint32_t *)cpqary3p->odr_cl) | 0x1));
			if (cpqary3_check_acc_handle(cpqary3p->odr_cl_handle) !=
			    DDI_SUCCESS) {
				ddi_fm_service_impact(cpqary3p->dip,
				    DDI_SERVICE_LOST);
				ret = DDI_INTR_CLAIMED;
			}

		}
	}

	/* PERF */

	/*
	 * If s/w interrupt handler is already running, do not trigger another
	 * since packets have already been transferred to Retrieved Q.
	 * Else, Set swintr_flag to state to the s/w interrupt handler
	 * that it has a job to do.
	 * trigger the s/w interrupt handler
	 * Claim the interrupt
	 */

	mutex_enter(&cpqary3p->hw_mutex);

	if (cpqary3p->swisr_running)
		need_swintr = 0;
	else
		need_swintr = 1;

	mutex_exit(&cpqary3p->hw_mutex);

	if (need_swintr)
		ddi_trigger_softintr(cpqary3p->cpqary3_softintr_id);

#ifdef	ISR_DEBUG
	cmn_err(CE_WARN, "CPQary3 : _hw_isr : Leaving \n");
#endif

	return (ret);

}	/*** End of cpqary3_hw_isr() ***/


/*
 *	Function         : cpqary3_sw_isr
 *	Description      : This routine determines if this instance of the
 *			   software interrupt handler was triggered by its
 *			   respective h/w interrupt handler and if affermative
 *			   processes the completed commands.
 *	Called By        : kernel (Triggered by : cpqary3_hw_isr)
 *	Parameters       : per-controller
 *	Calls            : cpqary3_retrieve()
 *	Return Values    : DDI_INTR_CLAIMED/UNCLAIMED
 *			   [We either CLAIM the interrupr or DON'T]
 */
uint_t
cpqary3_sw_isr(caddr_t per_ctlr)
{
	cpqary3_t	*cpqary3p;
	uint_t		ret = DDI_INTR_UNCLAIMED;

#ifdef	ISR_DEBUG
	cmn_err(CE_CONT, "CPQary3 : _sw_isr : Entering \n");
#endif

	cpqary3p = (cpqary3_t *)(void *)per_ctlr;
	if (!cpqary3p) {
		cmn_err(CE_PANIC, "CPQary3 : Software Interrupt Service Routine"
		    " invoked with NULL pointer argument \n");
	}

	mutex_enter(&cpqary3p->sw_mutex);
	mutex_enter(&cpqary3p->hw_mutex);

	/*
	 * Are we already running?
	 */

	if (cpqary3p->swisr_running) {
		/* yes, already running; do not claim this interrupt */
		mutex_exit(&cpqary3p->hw_mutex);
		mutex_exit(&cpqary3p->sw_mutex);
		return (DDI_INTR_UNCLAIMED);
	}

	cpqary3p->swisr_running = 1;

	/*
	 * cpqary3_retrieve() requires both locks to be held, but may
	 * drop them during processing.  Both locks are always held on
	 * return.
	 */
	if (CPQARY3_SUCCESS == cpqary3_retrieve(cpqary3p)) {
		/* retrieve() processed a command, so claim the interrupt */
		ret = DDI_INTR_CLAIMED;
	}

	/*
	 * cpqary3_retrieve() may drop the hw_lock, but always returns
	 * with the lock held and the replyq empty (checked under the lock).
	 * In this way, the replyq-empty check in retrieve() and our clearing
	 * of the swisr_running flag below are atomic under the hw_lock.
	 *
	 * If we're racing with cpqary3_hw_isr() here, then:
	 *
	 * 1. If we won the race, then hw_isr() will find the swisr_running
	 *    flag clear and will trigger a new soft interrupt, so it is
	 *    safe for us to exit now.
	 *
	 * 2. If hw_isr won the race, then it found swisr_running still set
	 *    and did not trigger a new soft interrupt.  But, retrieve() has
	 *    verified that the replyq is empty so the interrupt handling is
	 *    complete and we can exit.
	 *
	 * See ddi_add_softintr(9F) for another example of this algorithm.
	 */
	cpqary3p->swisr_running = 0;

	mutex_exit(&cpqary3p->hw_mutex);
	mutex_exit(&cpqary3p->sw_mutex);

#ifdef	ISR_DEBUG
	cmn_err(CE_CONT, "CPQary3 : _sw_isr : Leaving \n");
#endif

	return (ret);


}	/* End of cpqary3_sw_isr() */
