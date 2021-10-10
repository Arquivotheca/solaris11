/*
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 *
 */

/*
 * Copyright 2009, 2011, Hewlett-Packard Development Company, L.P.
 */

/*
 *
 * Module Name:
 *   CPQARY3_UTIL.C
 * Abstract:
 *   This module contains utility routines used by other modules in the
 *	driver.
 */

#include	"cpqary3.h"

#define	HBA_SETGEOM(hd, sec) (((hd) << 16) | (sec))

/*
 * Local Functions Definitions
 */

int	cpqary3_target_geometry(struct scsi_address *);
/* PROBE PERFORMANCE */
int8_t	cpqary3_detect_target_geometry(uint32_t, uint32_t, cpqary3_t *);



/*
 *	Function         : 	cpqary3_read_conf_file
 *	Description      : 	This routine reads the driver
 *				configuration file.
 *	Called By        : 	cpqary3_attach()
 *	Parameters       : 	device-information pointer, per_controller
 *	Calls            : 	None
 *	Return Values    : 	None
 */
void
cpqary3_read_conf_file(dev_info_t *dip, cpqary3_t *cpqary3p)
{

	char	*ptr;

	/*
	 * Plugin the code necessary to read from driver's conf file.
	 * As of now, we are not interested in reading the onf file
	 * for any purpose.
	 *
	 * eg. :
	 *
	 * retvalue = ddi_getprop( DDI_DEV_T_NONE, dip, DDI_PROP_DONTPASS,
	 * "cpqary3_online_debug", -1);
	 */

	/*
	 *  We are calling ddi_prop_lookup_string
	 *  which gets the property value, which is passed at
	 *  the grub menu. If the user wants to use the older
	 *  target mapping algorithm,(prior to 1.80)at the grub menu
	 *  "cpqary3_tgtmap=off" should be entered. if this
	 *  string is entered, then we will set the
	 *  value of the variable legacy_mapping to one, which
	 *  will be used in
	 *  cpqary3_detect_target_geometry()
	 *  and cpqary3_probe4LVs(), to decide on the
	 *  mapping algorithm
	 */

	if (ddi_prop_lookup_string(DDI_DEV_T_ANY, dip, 0,
	    "cpqary3_tgtmap", &ptr) == DDI_PROP_SUCCESS) {
		if (strcmp("off", ptr) == 0) {
			cpqary3p->legacy_mapping = 1;
		}
		ddi_prop_free(ptr);
	}

	return;

}	/* End of cpqary3_read_conf_file() */


/*
 *	Function         : 	cpqary3_tick_hdlr
 *	Description      : 	This routine is called once in 60 seconds
 *				to detect any command that is pending with
 *				the controller and hastimed out.
 *				Once invoked, it re-initializes itself such
 *				that it is invoked after an interval of
 *				60 seconds.
 *	Called By        : 	kernel
 *	Parameters       : 	per_controller
 *	Calls            : 	None
 *	Return Values    : 	None
 */
void
cpqary3_tick_hdlr(void *arg)
{
	cpqary3_t		*ctlr;
	volatile CfgTable_t	*ctp;
	/*
	 * clock_t			cpqary3_lbolt;
	 * clock_t			cpqary3_ticks;
	 * cpqary3_pkt_t		*pktp;
	 * struct scsi_pkt		*scsi_pktp;
	 * cpqary3_cmdpvt_t	*local;
	 * uint32_t i;
	 * uint32_t no_cmds = 0;
	 */

	/*
	 * The per-controller shall be passed as argument.
	 * Read the HeartBeat of the controller.
	 * if the current heartbeat is the same as the one recorded earlier,
	 * the f/w has locked up!!!
	 */

	if (NULL == (ctlr = (cpqary3_t *)arg))
		return;

	ctp = (CfgTable_t *)ctlr->ct;

	/* CONTROLLER_LOCKUP */
	if (ctlr->heartbeat == DDI_GET32(ctlr, &ctp->HeartBeat)) {
		if (ctlr->lockup_logged == CPQARY3_FALSE) {
			cmn_err(CE_WARN, "CPQary3 : %s HBA firmware"
			    " Locked !!! \n", ctlr->hba_name);
			cmn_err(CE_WARN, "CPQary3 :Please reboot the system\n");
			cpqary3_intr_onoff(ctlr, CPQARY3_INTR_DISABLE);
			if (ctlr->host_support & 0x4)
				cpqary3_lockup_intr_onoff(ctlr,
				    CPQARY3_LOCKUP_INTR_DISABLE);
			ctlr->controller_lockup = CPQARY3_TRUE;
			ctlr->lockup_logged = CPQARY3_TRUE;
		}
	}
#if 0
	/*
	 * QXCR1000929251: Commenting the code which times out the commands
	 * pending at the controller. These changes are done for SS821 release.
	 * This will be revisited in the next release, where commands pending
	 * at the controller will be aborted
	 */

		/* CONTROLLER_LOCKUP */
	no_cmds  = (uint32_t)((ctlr->ctlr_maxcmds / 3)
	    * NO_OF_CMDLIST_IN_A_BLK);
	mutex_enter(&ctlr->sw_mutex);

	for (i = 0; i < no_cmds; i++) {
		local = &ctlr->cmdmemlistp->pool[i];
		ASSERT(local != NULL);
		pktp = MEM2PVTPKT(local);

		if (!pktp)
			continue;

		if ((local->cmdpvt_flag == CPQARY3_TIMEOUT) ||
		    (local->cmdpvt_flag == CPQARY3_RESET)) {
			continue;
		}

		if (local->occupied == CPQARY3_OCCUPIED) {
			scsi_pktp   = pktp->scsi_cmd_pkt;
			cpqary3_lbolt = ddi_get_lbolt();
			if ((scsi_pktp) && (scsi_pktp->pkt_time)) {
				cpqary3_ticks = cpqary3_lbolt
				    - pktp->cmd_start_time;

				if ((drv_hztousec(cpqary3_ticks)/1000000) >
				    scsi_pktp->pkt_time) {
					scsi_pktp->pkt_reason =	CMD_TIMEOUT;
					scsi_pktp->pkt_statistics =
					    STAT_TIMEOUT;
					scsi_pktp->pkt_state = STATE_GOT_BUS |
					    STATE_GOT_TARGET | STATE_SENT_CMD;
					local->cmdpvt_flag = CPQARY3_TIMEOUT;

					/* This should always be the case */
					if (scsi_pktp->pkt_comp) {
						mutex_exit(&ctlr->sw_mutex);
						(*scsi_pktp->pkt_comp)
						    (scsi_pktp);
						mutex_enter(&ctlr->sw_mutex);
						continue;
					}
				}
			}
		}
	}

#endif
	ctlr->heartbeat	= DDI_GET32(ctlr, &ctp->HeartBeat);
	/* QXCR1000929251 */
	ctlr->tick_tmout_id = timeout(cpqary3_tick_hdlr, (caddr_t)ctlr,
	    drv_usectohz(CPQARY3_TICKTMOUT_VALUE));
	return;

}	/* End of cpqary3_tick_hdlr() */


/*
 *	Function         : 	cpqary3_init_ctlr_resource
 *	Description      : 	This routine initializes the command list,
 *				initializes
 *				the controller, enables the interrupt.
 *	Called By        : 	cpqary3_attach()
 *	Parameters       : 	per_controller
 *	Calls            : 	cpqary3_init_ctlr(), cpqary3_meminit(),
 *				cpqary3_intr_onoff(),
 *	Return Values    : 	SUCCESS / FAILURE
 *				[ Shall return failure if any of the mandatory
 *				initializations / setup of resources fail ]
 */
uint16_t
cpqary3_init_ctlr_resource(cpqary3_t *ctlr)
{


#ifdef CPQARY3_DEBUG_MEM
	cmn_err(CE_CONT, "CPQary3 : _init_ctlr_resource : Entering \n");
	int8_t	i = 0;
#endif

	/*
	 * Initialize the Controller
	 * Alocate Memory Pool for driver supported number of Commands
	 * return if not successful
	 * Allocate target structure for controller and initialize the same
	 * Detect all existing targets and allocate target structure for each
	 * Determine geometry for all existing targets
	 * Initialize the condition variables
	 */

	RETURN_FAILURE_IF_NULL(ctlr);

	if (CPQARY3_FAILURE == cpqary3_init_ctlr(ctlr))
		return (CPQARY3_FAILURE);

	if (CPQARY3_FAILURE == cpqary3_meminit(ctlr))
		return (CPQARY3_FAILURE);

	/*
	 * pre-allocate a command list for use during quiesce since we
	 * cannot use locks during quiesce itself.
	 *
	 * we pre-allocate some dma space as well.
	 */
	ctlr->quiescecmd = cpqary3_synccmd_alloc(ctlr, 4096);
	if (NULL == ctlr->quiescecmd) {
		cmn_err(CE_WARN, "CPQary3: Quiesce Initialization Failed\n");
		cpqary3_memfini(ctlr, CPQARY3_MEMLIST_DONE |
		    CPQARY3_PHYCTGS_DONE | CPQARY3_CMDMEM_DONE);
		return (CPQARY3_FAILURE);
	}
	ctlr->quiescecmddmapa = ctlr->quiescecmd->cmdlist_memaddr->SG[0].Addr;
	ctlr->quiescecmddmasz = ctlr->quiescecmd->cmdlist_memaddr->SG[0].Len;
	ctlr->quiescecmd->cmdlist_memaddr->SG[0].Addr = 0;
	ctlr->quiescecmd->cmdlist_memaddr->SG[0].Len = 0;
	ctlr->quiescecmd->cmdpvt_flag = CPQARY3_QUIESCECMD;



#ifdef CPQARY3_DEBUG_MEM
	/*
	 * This code is in place to test the memory management of this driver.
	 * This block of code allocates and de-allocates memory as many number
	 * of times as given in the for loop.
	 * After the for loop is executed, it returns a failure, which in turn
	 * would result in attach being failed.
	 */
	cmn_err(CE_CONT, "CPQary3 : _init_ctlr_resource : Testing memory \n");
	for (i = 0; i < 15; i++) {
		if (CPQARY3_SUCCESS != cpqary3_meminit(ctlr)) {
			cmn_err(CE_CONT, "CPQary3 : meminit failed :"
			    " attempt %d \n", i);
			return (CPQARY3_FAILURE);
		}
		cmn_err(CE_CONT, "CPQary3 :INIT successful : attempt %d \n", i);
		cpqary3_memfini(ctlr, CPQARY3_MEMLIST_DONE |
		    CPQARY3_PHYCTGS_DONE | CPQARY3_CMDMEM_DONE);
		cmn_err(CE_CONT, "CPQary3 : FINI successful :attempt %d \n", i);
	}
	return (CPQARY3_FAILURE);
#endif

	ctlr->cpqary3_tgtp[CTLR_SCSI_ID] = MEM_ZALLOC(sizeof (cpqary3_tgt_t));
	if (!(ctlr->cpqary3_tgtp[CTLR_SCSI_ID])) {
		cmn_err(CE_WARN, "CPQary3: Target Initialization Failed\n");
		cpqary3_synccmd_free(ctlr, ctlr->quiescecmd);
		ctlr->quiescecmd = NULL;
		cpqary3_memfini(ctlr, CPQARY3_MEMLIST_DONE |
		    CPQARY3_PHYCTGS_DONE | CPQARY3_CMDMEM_DONE);
		return (CPQARY3_FAILURE);
	}
	ctlr->cpqary3_tgtp[CTLR_SCSI_ID]->type = CPQARY3_TARGET_CTLR;

	cpqary3_intr_onoff(ctlr, CPQARY3_INTR_DISABLE);

	/*
	 * Initialize all condition variables :
	 * for the immediate call back
	 * for the disable noe
	 * for fulsh cache
	 * for probe device
	 */

	cv_init(&ctlr->cv_immediate_wait, NULL, CV_DRIVER, NULL);
	cv_init(&ctlr->cv_noe_wait, NULL, CV_DRIVER, NULL);
	cv_init(&ctlr->cv_flushcache_wait, NULL, CV_DRIVER, NULL);
	cv_init(&ctlr->cv_reset_wait, NULL, CV_DRIVER, NULL);
	cv_init(&ctlr->cv_abort_wait, NULL, CV_DRIVER, NULL);
	cv_init(&ctlr->cv_ioctl_wait, NULL, CV_DRIVER, NULL);

	return (CPQARY3_SUCCESS);

}	/* End of cpqary3_init_ctlr_resource() */

/*
 *	Function	: 	cpqary3_target_geometry
 *	Description 	: 	This function returns the geometry
 *				for the target.
 *	Called By 	: 	cpqary3_getcap()
 *	Parameters	:	Target SCSI address
 *	Calls		:	None
 *	Return Values	: 	Device Geometry
 */
int
cpqary3_target_geometry(struct scsi_address *sa)
{

	cpqary3_t	*ctlr	= SA2CTLR(sa);
	cpqary3_tgt_t	*tgtp	= ctlr->cpqary3_tgtp[SA2TGT(sa)];

	/*
	 * The target CHS are stored in the per-target structure
	 * during attach time. Use these values
	 *
	 * The macro HBA_SETGEOM has been defined in the path
	 * /usr/include/sys/dktp/hba.h as given below:
	 * #define HBA_SETGEOM(hd, sec) (((hd) << 16) | (sec))
	 */
	return (HBA_SETGEOM(tgtp->properties.drive.heads,
	    tgtp->properties.drive.sectors));

}	/* End of cpqary3_target_geometry() */

/*
 *	Function        :   	cpqary3_synccmd_alloc
 *	Description     :   	This function allocates the DMA buffer
 *				for the commands
 *	Called By       :   	cpqary3_ioctl_send_bmiccmd(),
 *				cpqary3_ioctl_send_scsicmd()
 *				cpqary3_send_abortcmd(), cpqary3_flush_cache(),
 *				cpqary3_probe4LVs(), cpqary3_probe4Tapes(),
 *				cpqary3_detect_target_geometry()
 *	Parameters      :   	per_controller, buffer size
 *	Calls           :   	cpqary3_alloc_phyctgs_mem(),
 *				cpqary3_cmdlist_occupy()
 *	Return Values   :   	memp
 */
cpqary3_cmdpvt_t
*cpqary3_synccmd_alloc(cpqary3_t *cpqary3p, size_t bufsz)
{
	cpqary3_private_t	*cmddmah  = NULL;
	uint32_t		dmabufpa = 0;	/* XXX 32-bit pa? */
	cpqary3_cmdpvt_t	*memp = NULL;

	/* first, allocate any necessary dma buffers */
	if (bufsz > 0) {
		cpqary3_phyctg_t *dmah = NULL;
		caddr_t		dmabufva = NULL;

		/* first, allocate the command's dma handle */
		cmddmah = (cpqary3_private_t *)MEM_ZALLOC(sizeof (*cmddmah));
		if (cmddmah == NULL) {
			cmn_err(CE_WARN, "cpqary3_synccmd_alloc:"
			    " no memory for cmddmah\n");
			return (NULL);
		}

		/* next, allocate dma handle */
		dmah = (cpqary3_phyctg_t *)MEM_ZALLOC(sizeof (*dmah));
		if (dmah == NULL) {
			MEM_SFREE(cmddmah, sizeof (*cmddmah));
			cmn_err(CE_WARN, "cpqary3_synccmd_alloc:"
			    " no memory for dmah\n");
			return (NULL);
		}
		/* now, allocate dma buffer */
		dmabufva = cpqary3_alloc_phyctgs_mem(cpqary3p, bufsz,
		    &dmabufpa, dmah);
		if (dmabufva == NULL) {
			MEM_SFREE(cmddmah, sizeof (*cmddmah));
			DBG((CE_WARN, "cpqary3_synccmd_alloc:"
			    " no memory for dma buf\n"));
			return (NULL);
		}
		bzero(dmabufva, bufsz);

		/* attach dma buffer to command dma handle */
		cmddmah->sg = dmabufva;
		cmddmah->phyctgp = dmah;
	}

	/* next, allocate a command packet */
	memp = cpqary3_cmdlist_occupy(cpqary3p);
	if (memp == NULL) {
		if (cmddmah != NULL) {
			cpqary3_free_phyctgs_mem(cmddmah->phyctgp,
			    CPQARY3_FREE_PHYCTG_MEM);
			MEM_SFREE(cmddmah, sizeof (*cmddmah));
		}
		DBG((CE_WARN, "cpqary3_synccmd_alloc:"
		    " cannot get free command\n"));
		return (NULL);
	}
	memp->cmdpvt_flag = 0;
	memp->cmdlist_memaddr->Header.Tag.drvinfo_n_err =
	    CPQARY3_SYNCCMD_SUCCESS;

	/* attach dma resources to command */
	memp->driverdata = cmddmah;
	memp->cmdlist_memaddr->SG[0].Addr = dmabufpa;
	memp->cmdlist_memaddr->SG[0].Len  = (uint32_t)bufsz;

	/* done */
	return (memp);

}  /* cpqary3_synccmd_alloc() */

/*
 *	Function        :  cpqary3_synccmd_cleanup
 *	Description     :  This routine cleans up the command
 *	Called By       :  cpqary3_synccmd_complete(), * cpqary3_synccmd_free()
 *	Parameters      :  per_command_memory
 *	Calls           :  cpqary3_free_phyctgs_mem(), cpqary3_cmdlist_release()
 *	Return Values   :  none
 */
void
cpqary3_synccmd_cleanup(cpqary3_cmdpvt_t *memp)
{
	/*
	 * ordinary users should not call this routine
	 * (use cpqary3_synccmd_free() instead).  this is
	 * for use ONLY by cpqary3_synccmd_free() and
	 * cpqary3_process_pkt().
	 */

	if (memp->driverdata != NULL) {
		/* free dma resources */
		cpqary3_free_phyctgs_mem(memp->driverdata->phyctgp,
		    CPQARY3_FREE_PHYCTG_MEM);
		MEM_SFREE(memp->driverdata, sizeof (cpqary3_private_t));
		memp->driverdata = NULL;
	}
	/* release command */
	memp->cmdpvt_flag = 0;
	cpqary3_cmdlist_release(memp, CPQARY3_HOLD_SW_MUTEX);

	/* done */
	return;

}  /* cpqary3_synccmd_cleanup() */

/*
 *	Function        :   	cpqary3_synccmd_free
 *	Description     :   	This routine frees the command and the
 *				associated resources.
 *	Called By       :   	cpqary3_ioctl_send_bmiccmd(),
 *				cpqary3_ioctl_send_scsicmd()
 *				cpqary3_send_abortcmd(), cpqary3_flush_cache(),
 *				cpqary3_probe4LVs(), cpqary3_probe4Tapes(),
 *				cpqary3_detect_target_geometry()
 *	Parameters      :   	per_controller, per_command_memory
 *	Calls           :   	cpqary3_synccmd_cleanup()
 *	Return Values   :   	NONE
 */
void
cpqary3_synccmd_free(cpqary3_t *cpqary3p, cpqary3_cmdpvt_t *memp)
{
	/*
	 * so, the user is done with this command packet.
	 * we have three possible scenarios here:
	 *
	 * 1) the command was never submitted to the controller
	 *
	 * or
	 *
	 * 2) the command has completed at the controller and has
	 *    been fully processed by the interrupt processing
	 *    mechanism and is no longer on the submitted or
	 *    retrieve queues.
	 *
	 * or
	 *
	 * 3) the command is not yet complete at the controller,
	 *    and/or hasn't made it through cpqary3_synccmd_complete()
	 *    yet.
	 *
	 * For cases (1) and (2), we can go ahead and free the
	 * command and the associated resources.  For case (3), we
	 * must mark the command as no longer needed, and let
	 * cpqary3_synccmd_complete() clean it up instead.
	 */

	mutex_enter(&(cpqary3p->sw_mutex));
	if (memp->cmdpvt_flag == CPQARY3_SYNC_SUBMITTED) {
		/*
		 * command is still pending (case #3 above).
		 * mark the command as abandoned and let
		 * cpqary3_synccmd_complete() clean it up.
		 */
		memp->cmdpvt_flag = CPQARY3_SYNC_TIMEOUT;
		mutex_exit(&(cpqary3p->sw_mutex));
		return;
	}
	memp->cmdpvt_flag = 0;
	mutex_exit(&(cpqary3p->sw_mutex));

	/*
	 * command was either never submitted or has completed
	 * (cases #1 and #2 above).  so, clean it up.
	 */
	cpqary3_synccmd_cleanup(memp);

	/* done */
	return;

}  /* cpqary3_synccmd_free() */

/*
 *	Function         :    cpqary3_synccmd_complete
 *	Description      :    This routine processes the completed commands
 *                            using the sync interface and
 *                            initiates any callback that is needed.
 *	Called By        :    cpqary3_transport
 *	Parameters       :    per-command
 *	Calls            :    cpqary3_cmdlist_release, cpqary3_synccmd_cleanup
 *	Return Values    :    None
 */
void
cpqary3_synccmd_complete(cpqary3_cmdpvt_t *cpqary3_cmdpvtp)
{

	cpqary3_t		*cpqary3p;
	ddi_dma_handle_t	dmahandle;
	ASSERT(cpqary3_cmdpvtp != NULL);

	if (CPQARY3_TIMEOUT == cpqary3_cmdpvtp->cmdpvt_flag) {
		cpqary3_cmdlist_release(cpqary3_cmdpvtp, CPQARY3_NO_MUTEX);
		return;
	}

	cpqary3p = cpqary3_cmdpvtp->ctlr;

	if (cpqary3_cmdpvtp->cmdpvt_flag == CPQARY3_SYNC_TIMEOUT) {
		/*
		 * The submitter has abandoned this command, so we
		 * have to free the resources here.
		 */
		mutex_exit(&(cpqary3p->sw_mutex));
		cpqary3_synccmd_cleanup(cpqary3_cmdpvtp);
		mutex_enter(&(cpqary3p->sw_mutex));
	} else {
		/* submitter is waiting; wake him up */
		cpqary3_cmdpvtp->cmdpvt_flag = 0;
		dmahandle = cpqary3_cmdpvtp->cpqary3_phyctgp->cpqary3_dmahandle;
		(void) ddi_dma_sync(dmahandle, 0, 0, DDI_DMA_SYNC_FORCPU);
		if (cpqary3_cmdpvtp->driverdata != NULL)
			(void) ddi_dma_sync(cpqary3_cmdpvtp->driverdata->
			    phyctgp->cpqary3_dmahandle, 0, 0,
			    DDI_DMA_SYNC_FORCPU);
		/* we use broadcast because the cv is shared */
		cv_broadcast(&(cpqary3p->cv_ioctl_wait));
	}

}

/*
 *	Function        :   	cpqary3_synccmd_send
 * 	Description     :   	This routine sends the command to the controller
 *	Called By       :   	cpqary3_ioctl_send_bmiccmd(),
 *				cpqary3_ioctl_send_scsicmd()
 *				cpqary3_send_abortcmd(), cpqary3_flush_cache(),
 *				cpqary3_probe4LVs(), cpqary3_probe4Tapes(),
 *				cpqary3_detect_target_geometry()
 *	Parameters      :   	per_controller, per_command_memory,
 *				timeout value, flag(wait for reply)
 *	Calls           :   	cpqary3_submit(), cpqary3_add2submitted_cmdq()
 *	Return Values   :   	SUCCESS / FAILURE
 */
int
cpqary3_synccmd_send(cpqary3_t *cpqary3p, cpqary3_cmdpvt_t *memp,
    clock_t timeoutms, int flags)
{
	clock_t   	absto   = 0;  /* absolute timeout */
	int		waitsig = 0;
	int		rc = 0;
	kcondvar_t	*cv = 0;

	/* compute absolute timeout, if necessary */
	if (timeoutms > 0)
		absto = ddi_get_lbolt() + drv_usectohz(timeoutms * 1000);

	/* heed signals during wait? */
	if (flags & CPQARY3_SYNCCMD_SEND_WAITSIG)
		waitsig = 1;
	memp->complete = cpqary3_synccmd_complete;

	/* acquire the sw mutex for our wait */
	mutex_enter(&(cpqary3p->sw_mutex));

	/* submit command to controller */
	mutex_enter(&(cpqary3p->hw_mutex));

	memp->cmdpvt_flag = CPQARY3_SYNC_SUBMITTED;
	memp->cmdlist_memaddr->Header.Tag.drvinfo_n_err =
	    CPQARY3_SYNCCMD_SUCCESS;

	if (EIO == cpqary3_submit(cpqary3p, memp)) {
		mutex_exit(&(cpqary3p->hw_mutex));
		mutex_exit(&(cpqary3p->sw_mutex));
		rc = (-1);
		return (rc);
	}
	mutex_exit(&(cpqary3p->hw_mutex));

	/* wait for command completion, timeout, or signal */
	while (memp->cmdpvt_flag == CPQARY3_SYNC_SUBMITTED) {
		kmutex_t *mt = &(cpqary3p->sw_mutex);

		cv = &(cpqary3p->cv_ioctl_wait);
		/* wait with the requested behavior */
		if (absto) {
			clock_t   crc;
			if (waitsig) {
				crc = cv_timedwait_sig(cv, mt, absto);
			} else {
				crc = cv_timedwait(cv, mt, absto);
			}
			if (crc > 0)
				rc = 0;
			else
				rc = (-1);
		} else {
			if (waitsig) {
				rc = cv_wait_sig(cv, mt);
				if (rc > 0)
					rc = 0;
				else
					rc = (-1);
			} else {
				cv_wait(cv, mt);
				rc = 0;
			}
		}


		/*
		 * if our wait was interrupted (timeout),
		 * then break here
		 */
		if (rc) {
			break;
		}
	}

	/* our wait is done, so release the sw mutex */
	mutex_exit(&(cpqary3p->sw_mutex));

	/* return the results */
	return (rc);

}  /* cpqary3_synccmd_send() */

int
cpqary3_synccmd_send_poll(cpqary3_t *cpqary3p, cpqary3_cmdpvt_t *memp,
    clock_t timeoutms)
{
	uint32_t   tag = memp->tag.tag_value;
	clock_t    tt;  /* total time spent waiting */
	int	   rc;

	/* turn off interrupts */
	cpqary3_intr_onoff(cpqary3p, CPQARY3_INTR_DISABLE);
	if (cpqary3p->host_support & 0x4)
		cpqary3_lockup_intr_onoff(cpqary3p,
		    CPQARY3_LOCKUP_INTR_DISABLE);

	memp->complete = cpqary3_synccmd_complete;

	mutex_enter(&cpqary3p->sw_mutex);
	mutex_enter(&cpqary3p->hw_mutex);

	memp->cmdpvt_flag = CPQARY3_SYNC_SUBMITTED;
	memp->cmdlist_memaddr->Header.Tag.drvinfo_n_err =
	    CPQARY3_SYNCCMD_SUCCESS;

	/*
	 * submit command to the controller
	 *
	 * note that unlike cpqary3_handle_flag_nointr(), we do not
	 * drain the controller.  it does not appear necessary because
	 * 1) flag_nointr() does not prevent new commands from being
	 * submitted, and 2) cpqary3_poll_retrieve() seems to handle
	 * multiple outstanding commands.
	 */
	if (EIO == cpqary3_submit(cpqary3p, memp)) {
		mutex_exit(&cpqary3p->hw_mutex);
		mutex_exit(&cpqary3p->sw_mutex);
		/* XXX leave interrupts disabled? */
		return (-1);
	}

	/* poll for completion */
	rc = CPQARY3_FAILURE;
	for (tt = 0; (timeoutms == 0) || (tt < timeoutms); tt++) {
		rc = cpqary3_poll_retrieve(cpqary3p, tag, 1);
		if (CPQARY3_SUCCESS == rc)
			break;
		drv_usecwait(1000);
	}

	mutex_exit(&cpqary3p->hw_mutex);
	mutex_exit(&cpqary3p->sw_mutex);

	/* re-enable interrupts */
	cpqary3_intr_onoff(cpqary3p, CPQARY3_INTR_ENABLE);
	if (cpqary3p->host_support & 0x4)
		cpqary3_lockup_intr_onoff(cpqary3p, CPQARY3_LOCKUP_INTR_ENABLE);

	/* done */
	return (rc == CPQARY3_SUCCESS) ? 0 : (-1);

}

/*
 *	Function	: 	cpqary3_detect_target_geometry
 *	Description 	: 	This function determines the geometry for all
 *				the existing targets for the controller.
 *	Called By 	:  cpqary3_tgt_init()
 *	Parameters	:	per controller
 *	Calls		:  cpqary3_synccmd_alloc(), cpqary3_synccmd_send()
 *				cpqary3_synccmd_free()
 *	Return Values	: 	SUCCESS / FAILURE
 *				[ Shall return failure only if Memory
 *				constraints exist
 *				or controller does not respond ]
 */
int8_t
cpqary3_detect_target_geometry(uint32_t log_id, uint32_t log_index,
    cpqary3_t *ctlr)
{
	IdLogDrive		*idlogdrive;
	CommandList_t		*cmdlistp;
	cpqary3_cmdpvt_t	*cpqary3_cmdpvtp;

#ifdef CPQARY3_DEBUG
	cmn_err(CE_CONT, "CPQary3 : cpqary3_detect_target_geometry :"
	    " Entering \n");
#endif

	RETURN_FAILURE_IF_NULL(ctlr);

	/*
	 * Occupy a Command List
	 * Allocate Memory for return data
	 * If error, RETURN 0.
	 * get the Request Block from the CommandList
	 * Fill in the Request Packet with the corresponding values
	 * Submit the Command and Poll for its completion
	 * If success, continue else RETURN 0
	 */

	/* Sync Changes */
	cpqary3_cmdpvtp = cpqary3_synccmd_alloc(ctlr, sizeof (IdLogDrive));
	if (cpqary3_cmdpvtp == NULL) {
		DBG("CPQary3: cpqary3_detect_target_geometry:"
		    " unable to allocate cmd\n");
		return (CPQARY3_FAILURE);
	}

	cmdlistp = cpqary3_cmdpvtp->cmdlist_memaddr;
	idlogdrive = (IdLogDrive*) cpqary3_cmdpvtp->driverdata->sg;
	/* Sync Changes */


	/* Update Cmd Header */
	cmdlistp->Header.SGList		   = 1;
	cmdlistp->Header.SGTotal	   = 1;
	cmdlistp->Header.Tag.drvinfo_n_err = CPQARY3_SYNCCMD_SUCCESS;

	/* Cmd Request */
	cmdlistp->Request.CDBLen	   = CPQARY3_CDBLEN_16;
	cmdlistp->Request.CDB[0]	   = 0x26;
	cmdlistp->Request.CDB[6]	   = BMIC_IDENTIFY_LOGICAL_DRIVE;
	cmdlistp->Request.CDB[7]	   = (sizeof (IdLogDrive) >> 8) & 0xff;
	cmdlistp->Request.CDB[8]	   = sizeof (IdLogDrive) & 0xff;
	cmdlistp->Request.Type.Type	   = CISS_TYPE_CMD;
	cmdlistp->Request.Type.Attribute   = CISS_ATTR_HEADOFQUEUE;
	cmdlistp->Request.Type.Direction   = CISS_XFER_READ;

	/*
	 * For all the Targets that exist, issue an IDENTIFY LOGICAL DRIVE.
	 * That returns values which includes the dsired Geometry also.
	 * Update the Geometry in the per-target structure.
	 * NOTE :When the loop is executed for i=controller's SCSI ID, just
	 *	increament by one so that we are talking to the next logical
	 * 	drive in our per-target structure.
	 */

	/*
	 * Depending upon the value of the variable legacy_mapping
	 * set in cpqary3_attach(),
	 * the target mapping algorithm to be used by the driver is decided.
	 */

	if (ctlr->legacy_mapping == 1) {
		bzero(idlogdrive, sizeof (IdLogDrive));
		cmdlistp->Request.CDB[1] = (uint8_t)log_id;

		/* Always zero */
		cmdlistp->Header.LUN.PhysDev.TargetId	= 0;
		/*
		 * Logical volume Id numbering scheme is as follows
		 * 0x00000, 0x00001, ... - for Direct Attached
		 * 0x10000, 0x10001, ... - If 1st Port of HBA is
		 * 0x20000, 0x20001, ... - If 2nd Port of HBA is
		 * connected to MSA20 / MSA500
		 */
		cmdlistp->Header.LUN.PhysDev.Bus = log_id >> 16;
		cmdlistp->Header.LUN.PhysDev.Mode=
		    (cmdlistp->Header.LUN.PhysDev.Bus > 0) ?
		    MASK_PERIPHERIAL_DEV_ADDR : PERIPHERIAL_DEV_ADDR;

		/*
		 * Submit the command
		 * Poll for its completion
		 * If polling is not successful,
		 * something is wrong with the controler
		 * Return FAILURE (No point in continuing if
		 * h/w is faulty !!!)
		 */

		/* Sync Changes */
		if (cpqary3_synccmd_send(ctlr, cpqary3_cmdpvtp, 90000,
		    CPQARY3_SYNCCMD_SEND_WAITSIG) != 0) {
			/* Timed out */
			cpqary3_synccmd_free(ctlr, cpqary3_cmdpvtp);
			return (CPQARY3_FAILURE);
		}
		if ((cpqary3_cmdpvtp->cmdlist_memaddr->
		    Header.Tag.drvinfo_n_err ==
		    CPQARY3_SYNCCMD_FAILURE) && (
		    cpqary3_cmdpvtp->errorinfop->CommandStatus != 2)) {
			DBG("CPQary3 : _probe4targets :"
			    "Identify Logical drives Returned ERROR\n");
			DBG1("CPQary3 : _probe4targets :"
			    " Command Status = %d\n", cpqary3_cmdpvtp->
			    errorinfop->CommandStatus);
			cpqary3_synccmd_free(ctlr, cpqary3_cmdpvtp);

			return (CPQARY3_FAILURE);
		}
		/* Sync Changes */

		ctlr->cpqary3_tgtp[log_index]->properties.drive.heads =
		    idlogdrive->heads;
		ctlr->cpqary3_tgtp[log_index]->properties.drive.sectors
		    = idlogdrive->sectors;

		DBG1("CPQary3 : _detect_target_geometry :"
		    "target = %d \n", i);
		DBG1("CPQary3 : _detect_target_geometry :"
		    "heads = %d \n", idlogdrive->heads);
		DBG1("CPQary3:  _detect_target_geometry :"
		    " sectors = %d \n", idlogdrive->sectors);
	} /* End of if */
	else {

/*
 *	Fix for QXCR1000446657: Logical drives are re numbered
 *	after deleting a Logical drive.
 *	introduced, new variable ld_count, which gets
 *	incremented when the Target ID is found.
 *	And for i=controller's SCSI ID and LDs with holes are found,
 *	we continue talking to
 *	the next logical drive in the per-target structure
 */

		/* Go to the Next logical target */
		bzero(idlogdrive, sizeof (IdLogDrive));
		/* PROBE PERFORMANCE */
		cmdlistp->Request.CDB[1] = (uint8_t)log_id;
		/* Always zero */
		cmdlistp->Header.LUN.PhysDev.TargetId = 0;
		/*
		 * Logical volume Id numbering scheme is as follows
		 * 0x00000, 0x00001, ... - for Direct Attached
		 * 0x10000, 0x10001, ... - If 1st Port of HBA is
		 * connected to MSA20 / MSA500
		 * 0x20000, 0x20001, ... - If 2nd Port of HBA is
		 * connected to MSA20 / MSA500
		 */

		/* PROBE PERFORMANCE */
		cmdlistp->Header.LUN.PhysDev.Bus = log_id >> 16;
		cmdlistp->Header.LUN.PhysDev.Mode =
		    (cmdlistp->Header.LUN.PhysDev.Bus > 0) ?
		    MASK_PERIPHERIAL_DEV_ADDR : PERIPHERIAL_DEV_ADDR;

	/*
	 * Submit the command
	 * Poll for its completion
	 * If polling is not successful, something is wrong with the controler
	 * Return FAILURE (No point in continuing if h/w is faulty !!!)
	 */

		/* Sync Changes */
		if (cpqary3_synccmd_send(ctlr, cpqary3_cmdpvtp, 90000,
		    CPQARY3_SYNCCMD_SEND_WAITSIG) != 0) {
			/* Timed out */
			cpqary3_synccmd_free(ctlr, cpqary3_cmdpvtp);
			return (CPQARY3_FAILURE);
		}
		if ((cpqary3_cmdpvtp->cmdlist_memaddr->Header.Tag.
		    drvinfo_n_err == CPQARY3_SYNCCMD_FAILURE) &&
		    (cpqary3_cmdpvtp->errorinfop->CommandStatus != 2)) {
			DBG("CPQary3 : _probe4targets : Identify"
			    " Logical drives Returned ERROR \n");
			DBG1("CPQary3 : _probe4targets : Command"
			    " Status = %d\n", cpqary3_cmdpvtp->
			    errorinfop->CommandStatus);
			cpqary3_synccmd_free(ctlr, cpqary3_cmdpvtp);
			/* appropriate return value */
			return (CPQARY3_FAILURE);
		}
		/* Sync Changes */

		/* PROBE PERFORMANCE */
		ctlr->cpqary3_tgtp[log_index]->properties.drive.heads =
		    idlogdrive->heads;
		ctlr->cpqary3_tgtp[log_index]->properties.drive.sectors =
		    idlogdrive->sectors;

		DBG1("CPQary3 : _detect_target_geometry :"
		    " heads = %d \n", idlogdrive->heads);
		DBG1("CPQary3 : _detect_target_geometry :"
		    " sectors = %d \n", idlogdrive->sectors);

	} /* End else */

	/* Sync Changes */
	cpqary3_synccmd_free(ctlr, cpqary3_cmdpvtp);
	/* Sync Changes */

	return (CPQARY3_SUCCESS);

}	/* End of cpqary3_detect_target_geometry() */
