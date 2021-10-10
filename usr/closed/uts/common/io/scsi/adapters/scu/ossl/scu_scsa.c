/*
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 *
 * This file may contain confidential information of Intel Corporation
 * and should not be distributed in source form without approval
 * from Oracle Legal.
 */

#include <sys/scsi/adapters/scu/scu_var.h>

/*
 * Function prototypes for scsi_hba_tran
 */
static	int scu_scsa_tran_tgt_init(dev_info_t *, dev_info_t *,
    scsi_hba_tran_t *, struct scsi_device *);
static	void scu_scsa_tran_tgt_free(dev_info_t *, dev_info_t *,
    scsi_hba_tran_t *, struct scsi_device *);
static	int scu_scsa_start(struct scsi_address *, struct scsi_pkt *);
static	int scu_scsa_abort(struct scsi_address *, struct scsi_pkt *);
static	int scu_scsa_reset(struct scsi_address *, int);
static	int scu_scsa_reset_notify(struct scsi_address *, int,
    void (*)(caddr_t), caddr_t);
static	int scu_scsa_getcap(struct scsi_address *, char *, int);
static	int scu_scsa_setcap(struct scsi_address *, char *, int, int);
static	int scu_scsa_setup_pkt(struct scsi_pkt *, int (*)(caddr_t), caddr_t);
static	void scu_scsa_teardown_pkt(struct scsi_pkt *);
static	int scu_scsa_quiesce(dev_info_t *);
static	int scu_scsa_unquiesce(dev_info_t *);

/*
 * Function prototypes for smp_hba_tran
 */
static	int scu_smp_init(dev_info_t *, dev_info_t *, smp_hba_tran_t *,
    smp_device_t *);
static	void scu_smp_free(dev_info_t *, dev_info_t *, smp_hba_tran_t *,
    smp_device_t *);
static	int scu_smp_start(struct smp_pkt *);

/*
 * Local functions
 */
static	scu_tgt_t *scu_find_target(scu_iport_t *, char *);
static	scu_tgt_t *scu_allocate_target(scu_iport_t *, char *);
static	void scu_wwnstr_to_scic_sas_address(char *, SCI_SAS_ADDRESS_T *);
static	scu_tgt_t *scu_addr_to_tgt(struct scsi_address *, uint64_t *,
    scu_cmd_t *);

static	SCI_STATUS scu_prepare_cmd(scu_subctl_t *, scu_cmd_t *);
static	int scu_do_start_cmd(scu_subctl_t *, scu_cmd_t *);
static	int scu_poll_cmd(scu_subctl_t *, scu_cmd_t *);
static	int scu_lib_task_management(scu_subctl_t *, scu_tgt_t *, int,
    uint64_t, uint16_t, uint8_t *, uint32_t, int);

static	void scu_draining(void *);

static clock_t scu_quiesce_tick = 1;
static clock_t scu_quiesce_interval;

extern int do_polled_io;


/*
 * Setup scsi_hba_tran and smp_hba_tran
 *
 * Return Value:
 *	DDI_SUCCESS
 *	DDI_FAILURE
 */
int
scu_scsa_setup(scu_ctl_t *scu_ctlp)
{
	scsi_hba_tran_t	*tran;
	int		flags;

	SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INIT, SCUDBG_TRACE,
	    "%s enter", __func__);

	/*
	 * Allocate a transport structure
	 */
	tran = scsi_hba_tran_alloc(scu_ctlp->scu_dip, SCSI_HBA_CANSLEEP);
	if (tran == NULL) {
		SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INIT,
		    SCUDBG_ERROR|SCUDBG_TRACE,
		    "%s: scsi_hba_tran_alloc failed", __func__);
		return (DDI_FAILURE);
	}

	tran->tran_hba_private	= scu_ctlp;
	tran->tran_tgt_init	= scu_scsa_tran_tgt_init;
	tran->tran_tgt_free	= scu_scsa_tran_tgt_free;
	tran->tran_start	= scu_scsa_start;
	tran->tran_abort	= scu_scsa_abort;
	tran->tran_reset	= scu_scsa_reset;
	tran->tran_reset_notify	= scu_scsa_reset_notify;
	tran->tran_getcap	= scu_scsa_getcap;
	tran->tran_setcap	= scu_scsa_setcap;
	tran->tran_setup_pkt	= scu_scsa_setup_pkt;
	tran->tran_teardown_pkt	= scu_scsa_teardown_pkt;
	tran->tran_quiesce	= scu_scsa_quiesce;
	tran->tran_unquiesce	= scu_scsa_unquiesce;
	tran->tran_interconnect_type	= INTERCONNECT_SAS;
	tran->tran_hba_len	= sizeof (scu_cmd_t);

	/*
	 * Attach this instance of the hba
	 */
	flags = SCSI_HBA_TRAN_SCB | SCSI_HBA_TRAN_CDB | SCSI_HBA_ADDR_COMPLEX |
	    SCSI_HBA_TRAN_PHCI | SCSI_HBA_HBA;

	if (scsi_hba_attach_setup(scu_ctlp->scu_dip,
	    &scu_ctlp->scu_data_dma_attr, tran, flags) != DDI_SUCCESS) {
		scsi_hba_tran_free(tran);
		SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INIT,
		    SCUDBG_ERROR|SCUDBG_TRACE,
		    "%s: scsi_hba_attach_setup failed", __func__);
		return (DDI_FAILURE);
	}

	scu_ctlp->scu_scsa_tran = tran;

	/*
	 * Attach the SMP part of this hba
	 */
	scu_ctlp->scu_smp_tran = smp_hba_tran_alloc(scu_ctlp->scu_dip);
	ASSERT(scu_ctlp->scu_smp_tran != NULL);
	scu_ctlp->scu_smp_tran->smp_tran_hba_private	= scu_ctlp;
	scu_ctlp->scu_smp_tran->smp_tran_init		= scu_smp_init;
	scu_ctlp->scu_smp_tran->smp_tran_free		= scu_smp_free;
	scu_ctlp->scu_smp_tran->smp_tran_start		= scu_smp_start;

	if (smp_hba_attach_setup(scu_ctlp->scu_dip, scu_ctlp->scu_smp_tran)
	    != DDI_SUCCESS) {
		smp_hba_tran_free(scu_ctlp->scu_smp_tran);
		scu_ctlp->scu_smp_tran = NULL;
		(void) scsi_hba_detach(scu_ctlp->scu_dip);
		scsi_hba_tran_free(tran);
		SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INIT,
		    SCUDBG_ERROR|SCUDBG_TRACE,
		    "%s: smp_hba_attach_setup failed", __func__);
		return (DDI_FAILURE);
	}

	SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INIT, SCUDBG_TRACE,
	    "%s: return with successful value", __func__);

	return (DDI_SUCCESS);
}

/*
 * Free scsi_hba_tran and smp_hba_tran
 */
void
scu_scsa_teardown(scu_ctl_t *scu_ctlp)
{
	(void) smp_hba_detach(scu_ctlp->scu_dip);
	if (scu_ctlp->scu_smp_tran != NULL) {
		smp_hba_tran_free(scu_ctlp->scu_smp_tran);
		scu_ctlp->scu_smp_tran = NULL;
	}

	(void) scsi_hba_detach(scu_ctlp->scu_dip);
	if (scu_ctlp->scu_scsa_tran != NULL) {
		scsi_hba_tran_free(scu_ctlp->scu_scsa_tran);
		scu_ctlp->scu_scsa_tran = NULL;
	}
}

/*
 * Look for the target
 *
 * note that scu_lock is held
 */
static scu_tgt_t *
scu_find_target(scu_iport_t *scu_iportp, char *tgt_port)
{
	scu_ctl_t	*scu_ctlp = scu_iportp->scui_ctlp;
	scu_tgt_t	*scu_tgtp;

	SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_INIT, SCUDBG_TRACE,
	    "%s: enter iport 0x%p for tgt_port %s",
	    __func__, (void *)scu_iportp, tgt_port);

	scu_tgtp = ddi_soft_state_bystr_get(scu_iportp->scui_tgt_sstate,
	    tgt_port);

	if (scu_tgtp == NULL) {
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_INIT, SCUDBG_TRACE,
		    "%s: cannot find target on tgt_port %s",
		    __func__, tgt_port);
		return (NULL);
	}

	/*
	 * Now make sure the state is valid for the target
	 */
	mutex_enter(&scu_tgtp->scut_lock);
	if (scu_tgtp->scut_iportp != scu_iportp) {
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_INIT, SCUDBG_ERROR,
		    "%s: scu_tgt 0x%p on tgt_port %s is in scu_iport 0x%p "
		    "not the supplied scu_iport 0x%p", __func__,
		    (void *)scu_tgtp, tgt_port, (void *)scu_tgtp->scut_iportp,
		    (void *)scu_iportp);
		mutex_exit(&scu_tgtp->scut_lock);
		return (NULL);
	}

	SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_INIT, SCUDBG_TRACE,
	    "%s: enter iport 0x%p for tgt_port %s",
	    __func__, (void *)scu_iportp, tgt_port);

	mutex_exit(&scu_tgtp->scut_lock);

	return (scu_tgtp);
}

static void
scu_wwnstr_to_scic_sas_address(char *wwnstr, SCI_SAS_ADDRESS_T *sas_address)
{
	uint64_t	wwn;

	sas_address->high = sas_address->low = 0;
	if (scsi_wwnstr_to_wwn(wwnstr, &wwn) == DDI_SUCCESS) {
		sas_address->high = BE_32(wwn >> 32);
		sas_address->low = BE_32((uint32_t)wwn);
	}
}

/*
 * Allocate the target
 */
static scu_tgt_t *
scu_allocate_target(scu_iport_t *scu_iportp, char *tgt_port)
{
	scu_ctl_t			*scu_ctlp = scu_iportp->scui_ctlp;
	scu_tgt_t			*scu_tgtp;
	SCI_REMOTE_DEVICE_HANDLE_T	scif_device;
	SCIF_SAS_REMOTE_DEVICE_T	*fw_device;
	SCI_DOMAIN_HANDLE_T		scif_domain;
	SCI_SAS_ADDRESS_T		sas_address;
	SMP_DISCOVER_RESPONSE_PROTOCOLS_T	protocols;

	SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_INIT, SCUDBG_TRACE,
	    "%s: enter iport 0x%p tgt_port %s",
	    __func__, (void *)scu_iportp, tgt_port);

	/*
	 * Allocate the new softstate
	 */
	if (ddi_soft_state_bystr_zalloc(scu_iportp->scui_tgt_sstate,
	    tgt_port) != DDI_SUCCESS) {
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_INIT,
		    SCUDBG_TRACE|SCUDBG_ERROR,
		    "%s: cannot allocate target softstate for device "
		    "@ %s", __func__, tgt_port);
		return (NULL);
	}

	scu_tgtp = ddi_soft_state_bystr_get(scu_iportp->scui_tgt_sstate,
	    tgt_port);
	ASSERT(scu_tgtp != NULL);

	/*
	 * Initialize the state lock for the target
	 */
	mutex_init(&scu_tgtp->scut_lock, NULL, MUTEX_DRIVER,
	    DDI_INTR_PRI(scu_iportp->scui_ctlp->scu_intr_pri));

	mutex_init(&scu_tgtp->scut_wq_lock, NULL, MUTEX_DRIVER,
	    DDI_INTR_PRI(scu_iportp->scui_ctlp->scu_intr_pri));

	cv_init(&scu_tgtp->scut_reset_cv, NULL, CV_DRIVER, NULL);
	cv_init(&scu_tgtp->scut_reset_complete_cv, NULL, CV_DRIVER, NULL);

	/*
	 * Initialize the waiting queue
	 */
	STAILQ_INIT(&scu_tgtp->scut_wq);

	list_create(&scu_tgtp->scut_luns, sizeof (scu_lun_t),
	    offsetof(scu_lun_t, list_node));

	scu_tgtp->scut_tgt_num = SCU_INVALID_TARGET_NUM;

	(void) strncpy(scu_tgtp->scut_unit_address, tgt_port,
	    SCU_MAX_UA_SIZE - 1);

	scu_tgtp->scut_ctlp = scu_iportp->scui_ctlp;
	scu_tgtp->scut_subctlp = scu_iportp->scui_subctlp;
	scu_tgtp->scut_iportp = scu_iportp;

	scu_tgtp->scut_iport_ua = strdup(scu_iportp->scui_ua);

	scu_wwnstr_to_scic_sas_address(tgt_port, &sas_address);

	scif_domain = scu_iportp->scui_lib_domain;
	if (scif_domain == SCI_INVALID_HANDLE) {
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_INIT, SCUDBG_WARNING,
		    "%s: cannot find SCIL domain for iport 0x%p",
		    __func__, (void *)scu_iportp);

		/*
		 * Sometimes, this branch will be jumped into when the domain
		 * is still in discover process or just get discover time-out,
		 * but since this phymap was created after controller is
		 * started, so tgt_init will be called though tgtmap is
		 * not created by scu_iport_report_tgts routine yet.
		 */
		scif_domain = scu_get_domain_by_iport(scu_ctlp, scu_iportp);
		if (scif_domain == SCI_INVALID_HANDLE) {
			SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_INIT,
			    SCUDBG_WARNING,
			    "%s: scu_get_domain_by_iport cannot find out "
			    "domain for iport 0x%p",
			    __func__, (void *)scu_iportp);
			if (scu_tgtp->scut_iport_ua) {
				strfree(scu_tgtp->scut_iport_ua);
			}
			ddi_soft_state_bystr_free(scu_iportp->scui_tgt_sstate,
			    tgt_port);
			return (NULL);
		}
	}

	SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_INIT, SCUDBG_INFO,
	    "%s: SCIL domain 0x%p associated with iport 0x%p",
	    __func__, (void *)scif_domain, (void *)scu_iportp);

	scif_device = scif_domain_get_device_by_sas_address(scif_domain,
	    &sas_address);
	if (scif_device == SCI_INVALID_HANDLE) {
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_INIT, SCUDBG_WARNING,
		    "%s: cannot find SCIL remote device @ %s",
		    __func__, tgt_port);

		if (scu_tgtp->scut_iport_ua) {
			strfree(scu_tgtp->scut_iport_ua);
		}
		ddi_soft_state_bystr_free(scu_iportp->scui_tgt_sstate,
		    tgt_port);
		return (NULL);
	}

	scu_tgtp->scut_lib_remote_device = scif_device;
	scu_tgtp->scut_lib_tgt_valid = 1;
	scu_tgtp->scut_lib_tgt_ready = 1;

	/* Set the associate between driver target and SCIF target */
	(void) sci_object_set_association(scif_device, scu_tgtp);

	fw_device = (SCIF_SAS_REMOTE_DEVICE_T *)scif_device;

	/* Now we need to get phynum for the target */
	if (fw_device->containing_device == NULL) {
		scu_tgtp->scut_is_da = 1;
		scu_tgtp->scut_phyp = scu_find_phy_by_sas_address(scu_ctlp,
		    scu_iportp, tgt_port);
		if (scu_tgtp->scut_phyp != NULL) {
			scu_tgtp->scut_hba_phynum =
			    scu_tgtp->scut_phyp->scup_hba_index;
			scu_tgtp->scut_phyp->scup_tgtp = scu_tgtp;
		}
	} else {
		scu_tgtp->scut_hba_phynum = fw_device->expander_phy_identifier;
		scu_tgtp->scut_phyp = NULL;
	}

	scic_remote_device_get_protocols(
	    scif_remote_device_get_scic_handle(scif_device), &protocols);

	/* Don't allocate LUN softstate for SMP targets */
	if (protocols.u.bits.attached_smp_target) {
		scu_tgtp->scut_dtype = SCU_DTYPE_EXPANDER;
		scu_tgtp->scut_qdepth = 1;
	} else if (protocols.u.bits.attached_ssp_target) {
		scu_tgtp->scut_dtype = SCU_DTYPE_SAS;
		scu_tgtp->scut_qdepth = SCU_MAX_SAS_QUEUE_DEPTH;
	} else if (protocols.u.bits.attached_stp_target ||
	    protocols.u.bits.attached_sata_device) {
		scu_tgtp->scut_dtype = SCU_DTYPE_SATA;
		scu_tgtp->scut_protocol_sata = 1;
		scu_tgtp->scut_qdepth =
		    scif_remote_device_get_max_queue_depth(scif_device);
	}

	if (scu_tgtp->scut_phyp != NULL)
		scu_tgtp->scut_phyp->scup_dtype = scu_tgtp->scut_dtype;

	if (scu_tgtp->scut_dtype == SCU_DTYPE_EXPANDER) {
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_INIT,
		    SCUDBG_INFO,
		    "%s: expander device 0x%p is created tgt_port %s",
		    __func__, (void *)scu_tgtp, tgt_port);
		return (scu_tgtp);
	}

	if (ddi_soft_state_bystr_init(&scu_tgtp->scut_lun_sstate,
	    sizeof (scu_lun_t), SCU_LUN_SSTATE_SIZE) != 0) {
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_INIT,
		    SCUDBG_TRACE|SCUDBG_ERROR,
		    "%s: LUN soft_state_bystr_init failed", __func__);
		if (scu_tgtp->scut_iport_ua) {
			strfree(scu_tgtp->scut_iport_ua);
		}
		ddi_soft_state_bystr_free(scu_iportp->scui_tgt_sstate,
		    tgt_port);
		return (NULL);
	}

	SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_INIT, SCUDBG_TRACE,
	    "%s: exit successfully target 0x%p tgt_port %s",
	    __func__, (void *)scu_tgtp, tgt_port);

	return (scu_tgtp);
}


/*
 * SCSA entry point - tran_tgt_init
 *
 * Return Value:
 *	DDI_SUCCESS
 *	DDI_FAILURE
 */
/*ARGSUSED*/
static int
scu_scsa_tran_tgt_init(dev_info_t *hba_dip, dev_info_t *tgt_dip,
    scsi_hba_tran_t *tran, struct scsi_device *sd)
{
	scu_ctl_t	*scu_ctlp;
	scu_iport_t	*scu_iportp;
	scu_tgt_t	*scu_tgtp = NULL;
	scu_lun_t	*scu_lunp = NULL;
	char		*ua;
	char		*tgt_port = NULL;
	uint64_t	lun_num;
	int		rval;

	/* First make sure we're an iport */
	if (scsi_hba_iport_unit_address(hba_dip) == NULL) {
		SCUDBG(NULL, SCUDBG_TGT|SCUDBG_INIT, SCUDBG_ERROR|SCUDBG_TRACE,
		    "%s: cannot enumerate device on hba node", __func__);
		return (DDI_FAILURE);
	}

	scu_ctlp = SCU_TRAN_TO_HBA(tran);
	scu_iportp = SCU_TRAN_TO_IPORT(tran);

	/* Get the unit address */
	ua = scsi_device_unit_address(sd);
	if (ua == NULL) {
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_INIT,
		    SCUDBG_ERROR|SCUDBG_TRACE,
		    "%s: cannot get ua for sd 0x%p", __func__, (void *)sd);
		return (DDI_FAILURE);
	}

	SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_INIT, SCUDBG_INFO,
	    "%s: got ua %s for sd 0x%p", __func__, ua, (void *)sd);

	/* Get the target address */
	rval = scsi_device_prop_lookup_string(sd, SCSI_DEVICE_PROP_PATH,
	    SCSI_ADDR_PROP_TARGET_PORT, &tgt_port);
	if (rval != DDI_PROP_SUCCESS) {
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_INIT,
		    SCUDBG_ERROR|SCUDBG_TRACE,
		    "%s: cannot get target ua", __func__);
		return (DDI_FAILURE);
	}

	SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_INIT, SCUDBG_INFO,
	    "%s: got tgt_port %s", __func__, tgt_port);

	/* Validate it's an active iport */
	if (scu_iportp->scui_ua_state == SCU_UA_INACTIVE) {
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_INIT,
		    SCUDBG_ERROR|SCUDBG_TRACE,
		    "%s: get inactive iport 0x%p for tgt_port %s",
		    __func__, (void *)scu_iportp, tgt_port);
		scsi_device_prop_free(sd, SCSI_DEVICE_PROP_PATH, tgt_port);
		return (DDI_FAILURE);
	}

	mutex_enter(&scu_ctlp->scu_lock);

	/* Check the target softstate */
	scu_tgtp = scu_find_target(scu_iportp, tgt_port);

	if (scu_tgtp) {
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_INIT,
		    SCUDBG_INFO|SCUDBG_ERROR,
		    "%s: target 0x%p @ tgt_port %s already exists",
		    __func__, (void *)scu_tgtp, tgt_port);
	} else {
		scu_tgtp = scu_allocate_target(scu_iportp, tgt_port);
		if (scu_tgtp == NULL) {
			SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_INIT,
			    SCUDBG_ERROR|SCUDBG_TRACE,
			    "%s: cannot allocate target %s",
			    __func__, tgt_port);
			mutex_exit(&scu_ctlp->scu_lock);
			scsi_device_prop_free(sd, SCSI_DEVICE_PROP_PATH,
			    tgt_port);
			return (DDI_FAILURE);
		}
	}

	SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_INIT, SCUDBG_INFO,
	    "%s: @%s target 0x%p, dip 0x%p tgt_port %s",
	    __func__, ua, (void *)scu_tgtp, (void *)tgt_dip, tgt_port);

	mutex_enter(&scu_tgtp->scut_lock);

	/* Now get the lun */
	lun_num = scsi_device_prop_get_int64(sd, SCSI_DEVICE_PROP_PATH,
	    SCSI_ADDR_PROP_LUN64, SCSI_LUN64_ILLEGAL);
	if (lun_num == SCSI_LUN64_ILLEGAL) {
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_INIT,
		    SCUDBG_ERROR|SCUDBG_TRACE,
		    "%s: no LUN for target 0x%p",
		    __func__, (void *)scu_tgtp);
		goto err_out;
	}

	if (scu_tgtp->scut_dtype != SCU_DTYPE_SAS &&
	    scu_tgtp->scut_dtype != SCU_DTYPE_SATA) {
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_INIT,
		    SCUDBG_ERROR|SCUDBG_TRACE,
		    "%s: scu_tgtp 0x%p got wrong device type", __func__,
		    (void *)scu_tgtp);
		goto err_out;
	}

	/* SATA device only has LUN 0 */
	if ((scu_tgtp->scut_dtype == SCU_DTYPE_SATA) &&
	    (lun_num > 0)) {
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_INIT,
		    SCUDBG_ERROR|SCUDBG_TRACE,
		    "%s: only support SATA device at LUN 0, lun_num %"PRIx64,
		    __func__, lun_num);
		goto err_out;
	}

	/* Allocate lun soft state */
	if (ddi_soft_state_bystr_zalloc(scu_tgtp->scut_lun_sstate, ua) !=
	    DDI_SUCCESS) {
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_INIT,
		    SCUDBG_ERROR|SCUDBG_TRACE,
		    "%s: cannot allocate lun softstate", __func__);
		goto err_out;
	}

	scu_lunp = ddi_soft_state_bystr_get(scu_tgtp->scut_lun_sstate, ua);
	if (scu_lunp == NULL) {
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_INIT,
		    SCUDBG_ERROR|SCUDBG_TRACE,
		    "%s: cannot get lun softstate", __func__);
		goto err_out;
	}

	scsi_device_hba_private_set(sd, scu_lunp);
	scu_lunp->scul_lun_num = lun_num;

	/* Convert to SCSI standard form */
	scu_lunp->scul_scsi_lun = scsi_lun64_to_lun(lun_num);

	bcopy(ua, scu_lunp->scul_unit_address,
	    strnlen(ua, SCU_MAX_UA_SIZE - 1));

	scu_lunp->scul_tgtp = scu_tgtp;

	/* Add to the list for the first tran_tgt_init */
	if (scu_tgtp->scut_tgt_num == SCU_INVALID_TARGET_NUM) {
		/*
		 * Each subctl has a fixed area of SCI_MAX_REMOTE_DEVICES
		 * in scu_tgts[] in the scus_num order so we can easily
		 * traverse subctl's devices
		 */
		int	begin = scu_tgtp->scut_subctlp->scus_num * \
		    SCI_MAX_REMOTE_DEVICES;
		int	num;

		for (num = begin; num < begin + SCI_MAX_REMOTE_DEVICES; num++) {
			if (scu_ctlp->scu_tgts[num] != NULL)
				continue;

			scu_ctlp->scu_tgts[num] = scu_tgtp;
			scu_tgtp->scut_tgt_num = (uint16_t)num;
			break;
		}

		if (scu_tgtp->scut_tgt_num == SCU_INVALID_TARGET_NUM) {
			SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_INIT,
			    SCUDBG_ERROR|SCUDBG_TRACE,
			    "%s: too many targets", __func__);
			goto err_out;
		}
	}

	scu_tgtp->scut_dip = sd->sd_dev;
	scu_lunp->scul_sd = sd;
	list_insert_tail(&scu_tgtp->scut_luns, scu_lunp);

	scu_tgtp->scut_ref_count++;

	(void) scsi_device_prop_update_int(sd, SCSI_DEVICE_PROP_PATH,
	    SCSI_ADDR_PROP_TARGET, (uint32_t)scu_tgtp->scut_tgt_num);

	if (scu_tgtp->scut_dtype == SCU_DTYPE_SATA) {
		(void) scsi_device_prop_update_string(sd,
		    SCSI_DEVICE_PROP_PATH, "variant", "sata");
	}

	(void) scsi_device_prop_update_int(sd, SCSI_DEVICE_PROP_PATH,
	    "phy-num", (uint32_t)scu_tgtp->scut_hba_phynum);

	/* Set the smhba attached-port and target-port props */
	(void) scu_smhba_set_scsi_device_props(scu_ctlp, scu_tgtp, sd);

	/* Update the pm props for the devices */
	(void) scu_smhba_update_tgt_pm_props(scu_tgtp);

	mutex_exit(&scu_tgtp->scut_lock);
	mutex_exit(&scu_ctlp->scu_lock);

	scsi_device_prop_free(sd, SCSI_DEVICE_PROP_PATH, tgt_port);

	SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_INIT, SCUDBG_TRACE,
	    "%s: successful for sd 0x%p, ua %s", __func__, (void *)sd, ua);

	return (DDI_SUCCESS);

err_out:
	scsi_device_hba_private_set(sd, NULL);

	if (scu_lunp) {
		scu_lunp->scul_sd = NULL;
		list_remove(&scu_tgtp->scut_luns, scu_lunp);
		ddi_soft_state_bystr_free(scu_tgtp->scut_lun_sstate, ua);
	}

	mutex_exit(&scu_tgtp->scut_lock);

	if (scu_tgtp && scu_tgtp->scut_ref_count == 0) {
		if (scu_tgtp->scut_phyp != NULL)
			scu_tgtp->scut_phyp->scup_tgtp = NULL;
		scu_tgtp->scut_dip = NULL;
		if (scu_tgtp->scut_iport_ua) {
			strfree(scu_tgtp->scut_iport_ua);
		}
		ddi_soft_state_bystr_free(scu_iportp->scui_tgt_sstate,
		    tgt_port);
	}

	if (scu_ctlp) {
		mutex_exit(&scu_ctlp->scu_lock);
	}

	if (tgt_port) {
		scsi_device_prop_free(sd, SCSI_DEVICE_PROP_PATH, tgt_port);
	}

	SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_INIT, SCUDBG_TRACE,
	    "%s: failed for @%s target 0x%p", __func__, ua, (void *)scu_tgtp);

	return (DDI_FAILURE);
}

/*
 * SCSA entry point - tran_tgt_free
 */
/*ARGSUSED*/
static void
scu_scsa_tran_tgt_free(dev_info_t *hba_dip, dev_info_t *tgt_dip,
    scsi_hba_tran_t *tran, struct scsi_device *sd)
{
	scu_ctl_t			*scu_ctlp;
	scu_iport_t			*scu_iportp;
	scu_tgt_t			*scu_tgtp;
	scu_phy_t			*scu_phyp;
	scu_lun_t			*scu_lunp;
	char				*unit_address;
	SCI_REMOTE_DEVICE_HANDLE_T	remote_device;

	scu_ctlp = SCU_TRAN_TO_HBA(tran);
	SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_INIT, SCUDBG_TRACE,
	    "%s: enter sd 0x%p dip 0x%p",
	    __func__, (void *)sd, (void *)tgt_dip);

	if (scsi_hba_iport_unit_address(hba_dip) == NULL) {
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_INIT,
		    SCUDBG_ERROR|SCUDBG_TRACE,
		    "%s: don't enumerate device on HBA node",
		    __func__);
		return;
	}

	scu_lunp = (scu_lun_t *)scsi_device_hba_private_get(sd);
	ASSERT((scu_lunp != NULL) && (scu_lunp->scul_tgtp != NULL));
	ASSERT(scu_lunp->scul_tgtp->scut_ref_count > 0);

	scu_tgtp = scu_lunp->scul_tgtp;
	ASSERT(scu_tgtp != NULL);

	mutex_enter(&scu_ctlp->scu_lock);
	mutex_enter(&scu_tgtp->scut_lock);

	if (scu_tgtp->scut_resetting == 1) {
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_INIT|SCUDBG_RECOVER,
		    SCUDBG_TRACE,
		    "%s: target 0x%p in resetting, return directly",
		    __func__, (void *)scu_tgtp);
		mutex_exit(&scu_tgtp->scut_lock);
		mutex_exit(&scu_ctlp->scu_lock);
		return;
	}

	unit_address = scu_lunp->scul_unit_address;
	list_remove(&scu_tgtp->scut_luns, scu_lunp);

	SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_INIT, SCUDBG_INFO,
	    "%s: @%s removed from target 0x%p, sd 0x%p",
	    __func__, unit_address, (void *)scu_tgtp, (void *)sd);

	scu_lunp->scul_sd = NULL;
	ddi_soft_state_bystr_free(scu_tgtp->scut_lun_sstate, unit_address);

	scu_tgtp->scut_ref_count--;

	if (scu_tgtp->scut_ref_count == 0) {
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_INIT, SCUDBG_INFO,
		    "%s: free target 0x%p tgt_num %d",
		    __func__, (void *)scu_tgtp, scu_tgtp->scut_tgt_num);
		scu_ctlp->scu_tgts[scu_tgtp->scut_tgt_num] = NULL;
		scu_tgtp->scut_tgt_num = SCU_INVALID_TARGET_NUM;

		remote_device = scu_tgtp->scut_lib_remote_device;
		if (remote_device != SCI_INVALID_HANDLE)
			(void) sci_object_set_association(remote_device, NULL);

		scu_phyp = scu_tgtp->scut_phyp;
		if (scu_phyp && scu_phyp->scup_tgtp == scu_tgtp)
			scu_phyp->scup_tgtp = NULL;

		scu_tgtp->scut_phyp = NULL;
		scu_tgtp->scut_dip = NULL;

		scu_iportp = scu_get_iport_by_ua(scu_ctlp,
		    scu_tgtp->scut_iport_ua);
		if (scu_iportp == NULL) {
			SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_INIT, SCUDBG_ERROR,
			    "%s: cannot find associated iport for target 0x%p",
			    __func__, (void *)scu_tgtp);
			mutex_exit(&scu_tgtp->scut_lock);
			mutex_exit(&scu_ctlp->scu_lock);
			return;
		}

		if (scu_tgtp->scut_iport_ua) {
			strfree(scu_tgtp->scut_iport_ua);
		}

		cv_destroy(&scu_tgtp->scut_reset_cv);
		cv_destroy(&scu_tgtp->scut_reset_complete_cv);
		mutex_destroy(&scu_tgtp->scut_lock);
		mutex_destroy(&scu_tgtp->scut_wq_lock);
		ddi_soft_state_bystr_fini(&scu_tgtp->scut_lun_sstate);

		ddi_soft_state_bystr_free(scu_iportp->scui_tgt_sstate,
		    scu_tgtp->scut_unit_address);
	} else {
		mutex_exit(&scu_tgtp->scut_lock);
	}

	mutex_exit(&scu_ctlp->scu_lock);
}

/*
 * Set the appropriate tgt and lun for scu_cmd_t
 *
 * notes: scut_lock will be held if the target exists
 */
static scu_tgt_t *
scu_addr_to_tgt(struct scsi_address *ap, uint64_t *lp, scu_cmd_t *scu_cmdp)
{
	scu_tgt_t	*scu_tgtp;
	scu_lun_t	*scu_lunp;

	scu_lunp = (scu_lun_t *)
	    scsi_device_hba_private_get(scsi_address_device(ap));

	if (scu_lunp == NULL || scu_lunp->scul_tgtp == NULL) {
		return (NULL);
	}

	scu_tgtp = scu_lunp->scul_tgtp;

	mutex_enter(&scu_tgtp->scut_lock);
	if (scu_tgtp->scut_lib_remote_device == NULL) {
		if (scu_cmdp != NULL) {
			scu_cmdp->cmd_tgtp = NULL;
			scu_cmdp->cmd_lunp = NULL;
		}
		mutex_exit(&scu_tgtp->scut_lock);
		return (NULL);
	}

	if (scu_cmdp != NULL) {
		scu_cmdp->cmd_tgtp = scu_tgtp;
		scu_cmdp->cmd_lunp = scu_lunp;
	}

	if (lp != NULL) {
		*lp = scu_lunp->scul_lun_num;
	}

	return (scu_tgtp);
}

int
scu_prepare_tag(scu_subctl_t *scu_subctlp, scu_cmd_t *scu_cmdp)
{
	uint16_t	lib_io_tag, io_tag;
	scu_io_slot_t	*io_slotp;

	ASSERT(mutex_owned(&scu_subctlp->scus_slot_lock));

	SCUDBG(scu_subctlp->scus_ctlp, SCUDBG_TGT|SCUDBG_IO, SCUDBG_TRACE,
	    "%s: cmd 0x%p", __func__, (void *)scu_cmdp);

	lib_io_tag = scic_controller_allocate_io_tag(
	    scu_subctlp->scus_scic_ctl_handle);
	if (lib_io_tag == SCI_CONTROLLER_INVALID_IO_TAG) {
		SCUDBG(scu_subctlp->scus_ctlp, SCUDBG_TGT|SCUDBG_IO,
		    SCUDBG_TRACE|SCUDBG_ERROR,
		    "%s: cannot allocate tag for cmd 0x%p",
		    __func__, (void *)scu_cmdp);
		return (SCU_FAILURE);
	}

	/*
	 * Note that the tag returned includes sequence value, while we only
	 * need the task context tag, so here a convertion is needed
	 */
	scu_subctlp->scus_tag_active_num++;
	io_tag = scic_sds_io_tag_get_index(lib_io_tag);
	io_slotp = &(scu_subctlp->scus_io_slots[io_tag]);

	scu_cmdp->cmd_prepared = 1;
	scu_cmdp->cmd_tag = io_tag;
	scu_cmdp->cmd_tag_assigned_by_ossl = 1;

	/* Attach cmd to slot */
	scu_cmdp->cmd_slot = io_slotp;
	io_slotp->scu_io_slot_cmdp = scu_cmdp;
	io_slotp->scu_io_lib_tag = lib_io_tag;
	return (SCU_SUCCESS);
}

scu_io_slot_t *
scu_detach_cmd(scu_cmd_t *scu_cmdp)
{
	scu_io_slot_t	*scu_slotp = scu_cmdp->cmd_slot;
#ifdef DEBUG
	scu_subctl_t *scu_subctlp = scu_cmdp->cmd_tgtp->scut_subctlp;
#endif

	ASSERT(mutex_owned(&scu_subctlp->scus_slot_lock));
	ASSERT(scu_cmdp->cmd_tag_assigned_by_ossl == 1);

	scu_cmdp->cmd_prepared = 0;
	scu_cmdp->cmd_tag_assigned_by_ossl = 0;
	scu_cmdp->cmd_lib_io_request = NULL;
	scu_cmdp->cmd_tag = 0;

	scu_cmdp->cmd_slot = NULL;
	scu_slotp->scu_io_slot_cmdp = NULL;
	return (scu_slotp);
}

void
scu_free_tag(scu_subctl_t *scu_subctlp, scu_io_slot_t *scu_slotp)
{
	ASSERT(mutex_owned(&scu_subctlp->scus_slot_lock));

	if (scic_controller_free_io_tag(
	    scu_subctlp->scus_scic_ctl_handle,
	    scu_slotp->scu_io_lib_tag) != SCI_SUCCESS) {
		SCUDBG(scu_subctlp->scus_ctlp, SCUDBG_IO, SCUDBG_ERROR,
		    "%s: scic_controller_free_io_tag failed "
		    "for cmd_lib_tag 0x%x",
		    __func__, scu_slotp->scu_io_lib_tag);
		ASSERT(0);
	}
	scu_slotp->scu_io_lib_tag = SCI_CONTROLLER_INVALID_IO_TAG;
	scu_slotp->scu_io_active_timeout = -1;
	scu_subctlp->scus_tag_active_num--;
}

/*
 * For SCIL, there are two methods to prepare the command - SCIF managed tag
 * allocation and OSSL managed tag allocation. Here we're going to use OSSL
 * managed method to avoid the temporary copy
 */
static SCI_STATUS
scu_prepare_cmd(scu_subctl_t *scu_subctlp, scu_cmd_t *scu_cmdp)
{
#ifdef DEBUG
	scu_ctl_t	*scu_ctlp = scu_subctlp->scus_ctlp;
#endif
	scu_tgt_t	*scu_tgtp = scu_cmdp->cmd_tgtp;
	scu_io_slot_t	*io_slotp;
	SCI_STATUS	sci_status;

	SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_IO, SCUDBG_TRACE,
	    "%s: cmd 0x%p", __func__, (void *)scu_cmdp);

	ASSERT(scu_cmdp->cmd_prepared == 0);

	scu_cmdp->cmd_started = 0;
	scu_cmdp->cmd_finished = 0;
	if (scu_cmdp->cmd_pkt->pkt_dma_len)
		scu_cmdp->cmd_dma = 1;

	/* Now get the tag for this io request */
	if (scu_prepare_tag(scu_subctlp, scu_cmdp) != SCU_SUCCESS) {
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_IO,
		    SCUDBG_TRACE|SCUDBG_ERROR,
		    "%s: cannot allocate slot for cmd 0x%p",
		    __func__, (void *)scu_cmdp);
		return (SCI_FAILURE);
	}

	io_slotp = scu_cmdp->cmd_slot;

	/* Then construct the SCIF io request */
	sci_status = scif_io_request_construct(
	    scu_subctlp->scus_scif_ctl_handle,
	    scu_tgtp->scut_lib_remote_device,
	    io_slotp->scu_io_lib_tag,
	    (void *)scu_cmdp, /* IO cmd is non-detachable */
	    io_slotp->scu_io_virtual_address,
	    &scu_cmdp->cmd_lib_io_request);

	switch (sci_status) {
	case SCI_FAILURE_IO_RESPONSE_VALID:
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_IO,
		    SCUDBG_TRACE,
		    "%s: cmd 0x%p cdbp 0x%p not supported by SATI",
		    __func__, (void *)scu_cmdp,
		    (void *)scu_cmdp->cmd_pkt->pkt_cdbp);
		/* FALLTHROUGH */
	case SCI_SUCCESS_IO_COMPLETE_BEFORE_START:
		/*
		 * Some scsi commands to SATA drive may complete in SATI,
		 * and here they are.
		 */
		scu_cmdp->cmd_finished = 1;
		/* FALLTHROUGH */
	case SCI_SUCCESS:
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_IO, SCUDBG_TRACE,
		    "%s: prepare slot %d successfully for cmd 0x%p",
		    __func__, scu_cmdp->cmd_tag, (void *)scu_cmdp);
		return (sci_status);

	case SCI_FAILURE_INVALID_REMOTE_DEVICE:
	case SCI_FAILURE_UNSUPPORTED_PROTOCOL:
	default:
		break;
	}

	/* Should never reach here */
	SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_IO,
	    SCUDBG_TRACE|SCUDBG_ERROR,
	    "%s: cannot construct scif io request for cmd 0x%p, "
	    "sci_status %d", __func__, (void *)scu_cmdp, sci_status);

	ASSERT(0);

	scu_cmdp->cmd_finished = 1;
	return (sci_status);
}

/*
 * First prepare the command if not, and then start
 *
 * For sata drive, won't even try to send when qdepth is full later
 */
static int
scu_do_start_cmd(scu_subctl_t *scu_subctlp, scu_cmd_t *scu_cmdp)
{
	scu_ctl_t			*scu_ctlp = scu_subctlp->scus_ctlp;
	scu_tgt_t			*scu_tgtp = scu_cmdp->cmd_tgtp;
	SCI_STATUS			sci_status;
	uint16_t			io_tag;
	scu_io_slot_t			*io_slotp;
	SCIF_SAS_REMOTE_DEVICE_T	*fw_device;

	SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_IO, SCUDBG_TRACE,
	    "%s: cmd 0x%p", __func__, (void *)scu_cmdp);

	ASSERT(mutex_owned(&scu_subctlp->scus_slot_lock));

	/* First check whether it's already prepared */
	if (scu_cmdp->cmd_prepared == 0) {
		sci_status = scu_prepare_cmd(scu_subctlp, scu_cmdp);
	} else {
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_IO,
		    SCUDBG_TRACE|SCUDBG_INFO,
		    "%s: already prepared slot %d for cmd 0x%p",
		    __func__, scu_cmdp->cmd_tag, (void *)scu_cmdp);

		sci_status = SCI_SUCCESS;
	}

	if (sci_status == SCI_SUCCESS) {
		/* Start the io request */
		ASSERT(scu_cmdp->cmd_finished == 0);

		io_tag = scu_cmdp->cmd_tag;
		ASSERT(io_tag < scu_subctlp->scus_slot_num);

		io_slotp = scu_cmdp->cmd_slot;
		ASSERT(io_slotp == &(scu_subctlp->scus_io_slots[io_tag]));
		ASSERT(io_slotp->scu_io_slot_cmdp == scu_cmdp);

		sci_status = (SCI_STATUS)scif_controller_start_io(
		    scu_subctlp->scus_scif_ctl_handle,
		    scu_tgtp->scut_lib_remote_device,
		    scu_cmdp->cmd_lib_io_request,
		    io_slotp->scu_io_lib_tag);

		if (sci_status == SCI_SUCCESS) {
			/* Polled cmd timeout is checked by scu_poll_cmd() */
			if (scu_cmdp->cmd_poll == 0) {
				io_slotp->scu_io_active_timeout =
				    scu_cmdp->cmd_pkt->pkt_time;
				if (io_slotp->scu_io_active_timeout <=
				    scu_watch_tick) {
					io_slotp->scu_io_active_timeout +=
					    scu_watch_tick;
				}
			}
			scu_cmdp->cmd_started = 1;
			scu_subctlp->scus_slot_active_num++;
			return (SCU_SUCCESS);
		}

		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_IO, SCUDBG_ERROR,
		    "%s: cannot start scif io request for cmd 0x%p, "
		    "sci_status %d", __func__, (void *)scu_cmdp, sci_status);

		if (sci_status == SCI_FAILURE_INVALID_STATE) {
			fw_device = (SCIF_SAS_REMOTE_DEVICE_T *)
			    scu_tgtp->scut_lib_remote_device;
			if (sci_base_state_machine_get_state(&fw_device->
			    ready_substate_machine) ==
			    SCIF_SAS_REMOTE_DEVICE_READY_SUBSTATE_SUSPENDED) {
				scu_log(scu_ctlp, CE_WARN,
				    "!%s: scic device of fw_device 0x%p is "
				    "not ready, so not I/O is allowed",
				    __func__, (void *)fw_device);
			}
		}

	} else if (scu_cmdp->cmd_finished) {
		/* For whatever reason, the io request is done */
		mutex_exit(&scu_subctlp->scus_slot_lock);
		scif_cb_io_request_complete(
		    scu_subctlp->scus_scif_ctl_handle,
		    scu_tgtp->scut_lib_remote_device,
		    scu_cmdp->cmd_lib_io_request,
		    (SCI_IO_STATUS)sci_status);
		mutex_enter(&scu_subctlp->scus_slot_lock);
		return (SCU_SUCCESS);
	}

	/*
	 * No tag available or start_io failed. Anyway retry
	 * the io request later
	 */
	return (SCU_FAILURE);
}

void
scu_start_wqs(scu_ctl_t *scu_ctlp)
{
	int	i;
	for (i = 0; i < scu_ctlp->scu_max_dev; i++) {
		if (scu_ctlp->scu_tgts[i])
			scu_start_wq(scu_ctlp, i);
	}
}

static void
scu_clear_inactive_cmd(scu_subctl_t *scu_subctlp, scu_cmd_t *scu_cmdp)
{
	scu_ctl_t	*scu_ctlp = scu_subctlp->scus_ctlp;

	ASSERT(mutex_owned(&scu_subctlp->scus_slot_lock));
	ASSERT(scu_cmdp->cmd_task == 0);
	ASSERT(scu_cmdp->cmd_prepared == 0);

	scu_set_pkt_reason(scu_ctlp, scu_cmdp, CMD_TIMEOUT,
	    0, STAT_TIMEOUT|STAT_TERMINATED);
	scu_cmdp->cmd_finished = 1;
}

void
scu_clear_active_task(scu_subctl_t *scu_subctlp, scu_cmd_t *scu_cmdp)
{
	_NOTE(ARGUNUSED(scu_subctlp));
	ASSERT(scu_cmdp->cmd_task);
	ASSERT(mutex_owned(&scu_subctlp->scus_slot_lock));

	SCUDBG(scu_subctlp->scus_ctlp,
	    SCUDBG_HBA|SCUDBG_TGT|SCUDBG_WATCH, SCUDBG_ERROR,
	    "%s: outstanding task 0x%p cleared on target 0x%p",
	    __func__, (void *)scu_cmdp, (void *)scu_cmdp->cmd_tgtp);

	(void) scu_detach_cmd(scu_cmdp);
	scu_cmdp->cmd_task_reason = SCU_TASK_TIMEOUT;
	scu_cmdp->cmd_timeout = 1;
	scu_cmdp->cmd_finished = 1;
}

static void
scu_clear_active_cmds(scu_subctl_t *scu_subctlp)
{
	scu_ctl_t	*scu_ctlp = scu_subctlp->scus_ctlp;
	scu_io_slot_t	*scu_slotp;
	scu_cmd_t	*scu_cmdp;
	scu_tgt_t	*scu_tgtp;
	int		i;

	mutex_enter(&scu_subctlp->scus_slot_lock);
	for (i = 0; i < scu_subctlp->scus_slot_num; i++) {
		scu_slotp = &scu_subctlp->scus_io_slots[i];

		/* Clear slot and tag */
		if (scu_slotp->scu_io_lib_tag == SCI_CONTROLLER_INVALID_IO_TAG)
			continue;
		scu_slotp->scu_io_lib_tag = SCI_CONTROLLER_INVALID_IO_TAG;
		scu_slotp->scu_io_active_timeout = -1;
		scu_subctlp->scus_tag_active_num--;
		scu_subctlp->scus_slot_active_num--;

		/* Clear cmd */
		if ((scu_cmdp = scu_slotp->scu_io_slot_cmdp) == NULL)
			continue;
		ASSERT(scu_cmdp->cmd_started);
		scu_tgtp = scu_cmdp->cmd_tgtp;
		SCUDBG(scu_ctlp,
		    SCUDBG_HBA|SCUDBG_TGT|SCUDBG_WATCH, SCUDBG_ERROR,
		    "%s: outstanding cmd 0x%p cleared on target 0x%p",
		    __func__, (void *)scu_cmdp, (void *)scu_tgtp);
		if (scu_cmdp->cmd_smp == 0 && scu_cmdp->cmd_task == 0) {
			scu_set_pkt_reason(scu_ctlp, scu_cmdp, CMD_ABORTED, 0,
			    (scu_cmdp->cmd_timeout ? STAT_TIMEOUT : 0) | \
			    STAT_ABORTED);
		}
		(void) scu_detach_cmd(scu_cmdp);
		scu_cmdp->cmd_finished = 1;
		if (scu_cmdp->cmd_poll)
			continue;

		/*
		 * Queue reseted cmd
		 * Do not queue task cmd to avoid blocking event handling
		 * in scu_cq_handler()
		 */
		if (scu_cmdp->cmd_sync && scu_cmdp->cmd_task) {
			cv_broadcast(&scu_ctlp->scu_cmd_complete_cv);
		} else {
			mutex_enter(&scu_ctlp->scu_cq_lock);
			STAILQ_INSERT_TAIL(&scu_ctlp->scu_cq, scu_cmdp,
			    cmd_next);
			mutex_exit(&scu_ctlp->scu_cq_lock);
		}
	}
	ASSERT(scu_subctlp->scus_slot_active_num == 0);
	ASSERT(scu_subctlp->scus_tag_active_num == 0);
	mutex_exit(&scu_subctlp->scus_slot_lock);
}

/*
 * Abort IO or task management command
 */
static int
scu_abort_cmd(scu_subctl_t *scu_subctlp, scu_cmd_t *scu_cmdp)
{
	scu_ctl_t	*scu_ctlp = scu_subctlp->scus_ctlp;
	scu_tgt_t	*scu_tgtp = scu_cmdp->cmd_tgtp;
	scu_lun_t	*scu_lunp = scu_cmdp->cmd_lunp;

	mutex_enter(&scu_subctlp->scus_slot_lock);
	if (scu_cmdp->cmd_finished) {
		mutex_exit(&scu_subctlp->scus_slot_lock);
		return (SCU_TASK_GOOD);
	}

	scu_cmdp->cmd_noretry = 1;
	if (scu_cmdp->cmd_started) {
		if (scu_cmdp->cmd_poll) {
			SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_IO,
			    SCUDBG_INFO|SCUDBG_ERROR,
			    "%s: %s scu_cmdp 0x%p got timeout on the HBA",
			    __func__, SCU_CMD_NAME(scu_cmdp), (void *)scu_cmdp);

			if (scu_cmdp->cmd_task) {
			/*
			 * Detach cmd
			 * The dangling cmd will finally be reseted or cleaned
			 */
				scu_clear_active_task(scu_subctlp, scu_cmdp);
				scu_device_error(scu_subctlp, scu_tgtp);
				mutex_exit(&scu_subctlp->scus_slot_lock);
				return (SCU_TASK_GOOD);
			}
			mutex_exit(&scu_subctlp->scus_slot_lock);

			SCUDBG(scu_ctlp, SCUDBG_TGT, SCUDBG_ERROR,
			    "%s: now do timeout recovery for target 0x%p",
			    __func__, (void *)scu_tgtp);

			if (scu_reset_target(scu_subctlp, scu_tgtp, 1) !=
			    SCU_TASK_GOOD) {
				(void) scu_hard_reset(scu_subctlp, scu_tgtp, 1);
			}

			/* Wait for cmd to be finished */
			mutex_enter(&scu_subctlp->scus_slot_lock);
			while (!scu_cmdp->cmd_finished) {
				mutex_exit(&scu_subctlp->scus_slot_lock);

				if (scu_poll_intr(scu_ctlp) != DDI_INTR_CLAIMED)
					drv_usecwait(10000);
				mutex_enter(&scu_subctlp->scus_slot_lock);
			}
			mutex_exit(&scu_subctlp->scus_slot_lock);
			return (SCU_TASK_GOOD);

		} else {
			/*
			 * Send task management to abort the command
			 * Here the tag is task context index, not SCIL tag
			 */
			scu_io_slot_t	*io_slot = scu_cmdp->cmd_slot;
			uint16_t	io_lib_tag = io_slot->scu_io_lib_tag;
			ASSERT(scu_subctlp->scus_io_slots[scu_cmdp->cmd_tag]. \
			    scu_io_slot_cmdp == scu_cmdp);
			mutex_exit(&scu_subctlp->scus_slot_lock);

			SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_TASK, SCUDBG_INFO,
			    "%s: call task management to abort cmd 0x%p",
			    __func__, (void *)scu_cmdp);
			return (scu_lib_task_management(scu_subctlp, scu_tgtp,
			    SCI_SAS_ABORT_TASK, scu_lunp->scul_lun_num,
			    io_lib_tag, NULL, 0, 0));
		}
	}
	mutex_exit(&scu_subctlp->scus_slot_lock);

	/* Cmd is either in wq or will be aborted soon */
	SCUDBG(scu_ctlp, SCUDBG_TGT, SCUDBG_TRACE|SCUDBG_INFO,
	    "%s: abort queued cmd 0x%p", __func__, (void *)scu_cmdp);
	while (scu_cmdp->cmd_finished == 0) {
		int	cmd_poll;
		mutex_enter(&scu_tgtp->scut_wq_lock);
		if (scu_cmdp->cmd_wq_queued) {
			/* First remove from wq */
			STAILQ_REMOVE(&scu_tgtp->scut_wq, scu_cmdp,
			    scu_cmd, cmd_next);
			mutex_exit(&scu_tgtp->scut_wq_lock);

			mutex_enter(&scu_subctlp->scus_slot_lock);
			scu_clear_inactive_cmd(scu_subctlp, scu_cmdp);
			cmd_poll = scu_cmdp->cmd_poll;
			mutex_exit(&scu_subctlp->scus_slot_lock);

			if (cmd_poll == 0)
				scu_flush_cmd(scu_ctlp, scu_cmdp);
			return (SCU_TASK_GOOD);
		}
		mutex_exit(&scu_tgtp->scut_wq_lock);

		if (scu_cmdp->cmd_poll)
			drv_usecwait(10000);	/* 10 msec. */
		else
			delay(drv_usectohz(10000));
	}
	return (SCU_TASK_GOOD);
}

/*
 * Poll for status of a command sent to HBA without interrupts
 * (FLAG_NOINTR IO command or task management command)
 */
static int
scu_poll_cmd(scu_subctl_t *scu_subctlp, scu_cmd_t *scu_cmdp)
{
	scu_ctl_t	*scu_ctlp = scu_subctlp->scus_ctlp;
	scu_tgt_t	*scu_tgtp;
	int		timeout, timeout_ticks; /* in usec */
	int		rval;

	SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_IO, SCUDBG_TRACE,
	    "%s: enter for command 0x%p", __func__, (void *)scu_cmdp);

	ASSERT(scu_cmdp->cmd_smp == 0);

	scu_tgtp = scu_cmdp->cmd_tgtp;

	timeout = (scu_cmdp->cmd_task ? 10 : scu_cmdp->cmd_pkt->pkt_time) *
	    1000000;
	timeout_ticks = timeout;
	mutex_enter(&scu_subctlp->scus_slot_lock);
	while (!scu_cmdp->cmd_finished && timeout_ticks >= 0) {
		mutex_exit(&scu_subctlp->scus_slot_lock);

		int err = scu_poll_intr(scu_ctlp);
		if (err == DDI_INTR_CLAIMED && scu_cmdp->cmd_wq_queued) {
			scu_start_wq(scu_ctlp, scu_tgtp->scut_tgt_num);
			timeout_ticks = timeout;
		} else {
			drv_usecwait(10000);
			timeout_ticks -= 10000;
		}

		mutex_enter(&scu_subctlp->scus_slot_lock);
	}
	mutex_exit(&scu_subctlp->scus_slot_lock);

	if (scu_cmdp->cmd_finished)
		rval = SCU_SUCCESS;
	else
		rval = scu_abort_cmd(scu_subctlp, scu_cmdp);

	SCUDBG(scu_ctlp, SCUDBG_IO, SCUDBG_TRACE|SCUDBG_INFO,
	    "%s: return with value %d for command 0x%p",
	    __func__, rval, (void *)scu_cmdp);

	return (rval);
}

/*
 * SCSA entry point - tran_start
 *
 * Return Value:
 *	- TRAN_ACCEPT
 *	- TRAN_FATAL_ERROR
 */
static int
scu_scsa_start(struct scsi_address *ap, struct scsi_pkt *pkt)
{
	scu_ctl_t	*scu_ctlp;
	scu_subctl_t	*scu_subctlp;
	scu_tgt_t	*scu_tgtp;
	scu_cmd_t	*scu_cmdp;
	boolean_t	hba_started;
	boolean_t	hba_quiesced;
	boolean_t	hba_resetting;
	int		cmd_poll;
	int		rval;

	scu_ctlp = SCU_ADDR_TO_HBA(ap);

	SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_IO, SCUDBG_TRACE,
	    "%s: pkt 0x%p, sd 0x%p, cdb0=0x%02x, dma_len=%lu",
	    __func__, (void *)pkt,
	    (void *)scsi_address_device(&pkt->pkt_address),
	    pkt->pkt_cdbp[0] & 0xff, pkt->pkt_dma_len);

	scu_cmdp = SCU_PKT_TO_CMD(pkt);
	ASSERT(scu_cmdp->cmd_pkt == pkt);
	ASSERT(scu_cmdp->cmd_tag == 0);

	/* Clear pkt reason */
	pkt->pkt_reason = CMD_INCOMPLETE;
	pkt->pkt_state = 0;
	pkt->pkt_statistics = 0;

	mutex_enter(&scu_ctlp->scu_lock);
	hba_started = scu_ctlp->scu_started;
	hba_quiesced = scu_ctlp->scu_quiesced;
	mutex_exit(&scu_ctlp->scu_lock);

	if (hba_started != TRUE) {
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_IO,
		    SCUDBG_TRACE|SCUDBG_ERROR,
		    "%s: HBA not started, pkt 0x%p", __func__, (void *)pkt);
		return (TRAN_FATAL_ERROR);
	}

	/* Find out the target */
	scu_tgtp = scu_addr_to_tgt(ap, NULL, scu_cmdp);

	scu_cmdp->cmd_poll = cmd_poll = (pkt->pkt_flags & FLAG_NOINTR) ? 1 : 0;

	if (scu_tgtp == NULL) {
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_IO,
		    SCUDBG_TRACE|SCUDBG_ERROR,
		    "%s: dropping due to NULL target, pkt 0x%p",
		    __func__, (void *)pkt);
		goto non_tgt;
	}

	scu_subctlp = scu_tgtp->scut_subctlp;
	hba_resetting = scu_subctlp->scus_resetting; /* XXX */

	/*
	 * Just queue and return when HBA is quiesced
	 */
	if (hba_quiesced == TRUE || hba_resetting) {
		scu_tgtp->scut_active_pkts++;
		mutex_exit(&scu_tgtp->scut_lock);

		/* Adding to waiting queue */
		mutex_enter(&scu_tgtp->scut_wq_lock);
		scu_cmdp->cmd_wq_queued = 1;
		STAILQ_INSERT_TAIL(&scu_tgtp->scut_wq, scu_cmdp, cmd_next);
		scu_tgtp->scut_wq_pkts++;
		mutex_exit(&scu_tgtp->scut_wq_lock);

		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_IO,
		    SCUDBG_TRACE|SCUDBG_WARNING,
		    "%s: hba %s, pkt 0x%p", __func__,
		    hba_quiesced ? "quiesced" : "resetting", (void *)pkt);

		return (TRAN_ACCEPT);
	}

	/*
	 * If the target in draining or resetting, queue and return
	 */
	if (scu_tgtp->scut_draining || scu_tgtp->scut_resetting) {
		scu_tgtp->scut_active_pkts++;
		mutex_exit(&scu_tgtp->scut_lock);

		/* Adding to waiting queue */
		mutex_enter(&scu_tgtp->scut_wq_lock);
		scu_cmdp->cmd_wq_queued = 1;
		STAILQ_INSERT_TAIL(&scu_tgtp->scut_wq, scu_cmdp, cmd_next);
		scu_tgtp->scut_wq_pkts++;
		mutex_exit(&scu_tgtp->scut_wq_lock);

		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_IO,
		    SCUDBG_TRACE|SCUDBG_WARNING,
		    "%s: target in draining/resetting", __func__);
		return (TRAN_ACCEPT);
	}

	/*
	 * Now for normal condition
	 */
	scu_tgtp->scut_active_pkts++;
	mutex_exit(&scu_tgtp->scut_lock);

	/*
	 * Try to start the command
	 */
	mutex_enter(&scu_subctlp->scus_slot_lock);
	rval = scu_do_start_cmd(scu_subctlp, scu_cmdp);
	/*
	 * Cmd fails to start either because no more tags or
	 * device is abnormal and need reset. Anyway should
	 * not hold tag.
	 */
	if (rval != SCU_SUCCESS && scu_cmdp->cmd_prepared) {
		scu_free_tag(scu_subctlp,
		    scu_detach_cmd(scu_cmdp));
	}
	mutex_exit(&scu_subctlp->scus_slot_lock);
	if (rval != SCU_SUCCESS) {
		mutex_enter(&scu_tgtp->scut_wq_lock);
		scu_cmdp->cmd_wq_queued = 1;
		STAILQ_INSERT_TAIL(&scu_tgtp->scut_wq, scu_cmdp, cmd_next);
		scu_tgtp->scut_wq_pkts++;
		mutex_exit(&scu_tgtp->scut_wq_lock);
	}

	/*
	 * For POLL mode
	 * Cmd may be completed and freed, so don't touch it anymore
	 * unless it's poll mode.
	 */
	if (cmd_poll) {
		SCUDBG(scu_ctlp, SCUDBG_IO, SCUDBG_INFO,
		    "%s: NOINTR pkt 0x%p", __func__, (void *)pkt);
		(void) scu_poll_cmd(scu_subctlp, scu_cmdp);
		scu_flush_cmd(scu_ctlp, NULL);
	}

	return (TRAN_ACCEPT);

non_tgt:
	/* For NULL target */
	scu_set_pkt_reason(scu_ctlp, scu_cmdp, CMD_DEV_GONE,
	    STATE_GOT_BUS, 0);

	if (cmd_poll == 0)
		scu_flush_cmd(scu_ctlp, scu_cmdp);

	return (TRAN_ACCEPT);
}

static void
scu_wait_cmd(scu_subctl_t *scu_subctlp, scu_cmd_t *scu_cmdp)
{
	scu_ctl_t	*scu_ctlp = scu_subctlp->scus_ctlp;

	mutex_enter(&scu_subctlp->scus_slot_lock);
	while (scu_cmdp->cmd_finished == 0) {
		cv_wait(&scu_ctlp->scu_cmd_complete_cv,
		    &scu_subctlp->scus_slot_lock);
	}
	mutex_exit(&scu_subctlp->scus_slot_lock);
}

/*
 * Issue a task management function
 * Return value:
 *	SCU_TASK_GOOD
 *	SCU_TASK_BUSY
 *	SCU_TASK_ERROR
 *	SCU_TASK_DEVICE_RESET
 *	SCU_TASK_TIMEOUT
 */
static int
scu_lib_task_management(scu_subctl_t *scu_subctlp, scu_tgt_t *scu_tgtp,
    int function, uint64_t lun, uint16_t io_tag, uint8_t *response_address,
    uint32_t response_length, int poll)
{
	scu_ctl_t	*scu_ctlp = scu_subctlp->scus_ctlp;
	scu_cmd_t	*scu_cmdp;
	scu_io_slot_t	*io_slot;
	SCI_STATUS	sci_status;
	int		rval = SCU_TASK_ERROR;

	SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_TASK, SCUDBG_TRACE,
	    "%s: scu_tgtp 0x%p, function %d, lun %"PRIx64", tag %u "
	    "resp_addr 0x%p resp_len %u", __func__, (void *)scu_tgtp,
	    function, lun, io_tag,
	    (void *)response_address, response_length);

	/* Allocate scu_cmd */
	scu_cmdp = (scu_cmd_t *)kmem_zalloc(sizeof (scu_cmd_t),
	    poll ? KM_NOSLEEP : KM_SLEEP);
	if (scu_cmdp == NULL)
		return (SCU_TASK_ERROR);
	scu_cmdp->cmd_task = 1;
	if (poll)
		scu_cmdp->cmd_poll = 1;
	else
		scu_cmdp->cmd_sync = 1;
	scu_cmdp->cmd_pkt = NULL;
	scu_cmdp->cmd_tgtp = scu_tgtp;

	/* Fill in task arguments */
	scu_cmdp->cmd_task_arg.scu_task_lun = lun;
	scu_cmdp->cmd_task_arg.scu_task_tag = io_tag;
	scu_cmdp->cmd_task_arg.scu_task_function = function;
	scu_cmdp->cmd_task_arg.scu_task_resp_addr = response_address;
	scu_cmdp->cmd_task_arg.scu_task_resp_len = response_length;

	/* If task has response data, it's not detachable anymore */
	ASSERT(response_address == NULL && response_length == 0);

	/* Allocate tag */
	mutex_enter(&scu_subctlp->scus_slot_lock);
	if (scu_prepare_tag(scu_subctlp, scu_cmdp) != SCU_SUCCESS) {
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_TASK,
		    SCUDBG_TRACE|SCUDBG_ERROR,
		    "%s: scu_allocate_tag failed for cmd 0x%p",
		    __func__, (void *)scu_cmdp);

		mutex_exit(&scu_subctlp->scus_slot_lock);
		rval = SCU_TASK_BUSY;
		goto exit;
	}

	io_slot = scu_cmdp->cmd_slot;

	/*
	 * The below code should be traversed again when hot-plug feature
	 * is implemented.
	 *
	 * Sometimes we will see panic if a device is hot removed here, the
	 * temporary fix is to first check whether the lib device is still
	 * existing.
	 *
	 * After hot-plug feature is added, a mutex should be used to protect
	 * such race condition between command execution and device hot-removal.
	 */
	mutex_enter(&scu_tgtp->scut_lock);
	if (scu_tgtp->scut_lib_remote_device == NULL) {
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_TASK|SCUDBG_HOTPLUG,
		    SCUDBG_TRACE,
		    "%s: no SCIF remote device attached to scu_tgt 0x%p",
		    __func__, (void *)scu_tgtp);
		mutex_exit(&scu_tgtp->scut_lock);
		mutex_exit(&scu_subctlp->scus_slot_lock);
		rval = SCU_TASK_GOOD;
		goto exit;
	}
	mutex_exit(&scu_tgtp->scut_lock);

	/* Now construct SCIF task request */
	sci_status = scif_task_request_construct(
	    scu_subctlp->scus_scif_ctl_handle,
	    scu_tgtp->scut_lib_remote_device,
	    io_slot->scu_io_lib_tag,
	    (void *)io_slot, /* task cmd is detachable */
	    io_slot->scu_io_virtual_address,
	    &scu_cmdp->cmd_lib_io_request);
	/*
	 * sci_status may be:
	 *	SCI_SUCCESS
	 *	SCI_FAILURE
	 *	SCI_FAILURE_UNSUPPORTED_PROTOCOL
	 */
	if (sci_status != SCI_SUCCESS) {
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_IO,
		    SCUDBG_TRACE|SCUDBG_ERROR,
		    "%s: cannot construct scif task request for cmd 0x%p, "
		    "sci_status %d", __func__, (void *)scu_cmdp, sci_status);
		mutex_exit(&scu_subctlp->scus_slot_lock);
		goto exit;
	}

	/* Then start the task request */
	scu_subctlp->scus_task_start_thread = curthread;
	sci_status = (SCI_STATUS)scif_controller_start_task(
	    scu_subctlp->scus_scif_ctl_handle,
	    scu_tgtp->scut_lib_remote_device,
	    scu_cmdp->cmd_lib_io_request,
	    io_slot->scu_io_lib_tag);
	scu_subctlp->scus_task_start_thread = NULL;
	/*
	 * sci_state may be:
	 *	SCI_SUCCESS
	 *	SCI_FAILURE_INVALID_PARAMETER_VALUE
	 *	SCI_FAILURE_INSUFFICIENT_RESOURCES
	 */
	if (sci_status != SCI_SUCCESS) {
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_IO,
		    SCUDBG_TRACE|SCUDBG_ERROR,
		    "%s: cannot start scif task request for cmd 0x%p, "
		    "sci_status %d", __func__, (void *)scu_cmdp, sci_status);

		if (sci_status == SCI_FAILURE_INSUFFICIENT_RESOURCES)
			rval = SCU_TASK_BUSY;
		(void) scu_detach_cmd(scu_cmdp);
		scu_free_tag(scu_subctlp, io_slot);
		mutex_exit(&scu_subctlp->scus_slot_lock);
		goto exit;
	}

	if (scu_cmdp->cmd_poll == 0)
		io_slot->scu_io_active_timeout = 60;
	scu_cmdp->cmd_started = 1;
	scu_subctlp->scus_slot_active_num++;
	mutex_exit(&scu_subctlp->scus_slot_lock);

	SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_TASK, SCUDBG_INFO,
	    "%s: successfully sent down task request cmd 0x%p for "
	    "scu_tgtp 0x%p, function %d, lun %"PRIx64", tag %u resp_addr 0x%p "
	    "resp_len %u", __func__, (void *)scu_cmdp,
	    (void *)scu_tgtp, function, lun, io_tag,
	    (void *)response_address, response_length);

	/* Now wait the command */
	if (scu_cmdp->cmd_poll)
		(void) scu_poll_cmd(scu_subctlp, scu_cmdp);
	else
		scu_wait_cmd(scu_subctlp, scu_cmdp);
	rval = scu_cmdp->cmd_task_reason;

	/* Check whether need to do remote device reset */
	if (rval == SCU_TASK_DEVICE_RESET) {
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_TASK,
		    SCUDBG_TRACE|SCUDBG_ERROR,
		    "%s: remote device reset is needed for target 0x%p",
		    __func__, (void *)scu_tgtp);

		scu_timeout(scu_subctlp, scu_tgtp, NULL);
	}

	/* Free the resources */
exit:
	kmem_free(scu_cmdp, sizeof (scu_cmd_t));

	SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_TASK, SCUDBG_TRACE,
	    "%s: task request return value %d", __func__, rval);
	return (rval);
}

/*
 * SCSA entry point - tran_abort
 *
 * Note:
 *	- if pkt is not NULL, abort just that command
 *	- if pkt is NULL, abort all outstanding commands for target
 *
 * Return Value:
 *	- TRUE
 *	- FALSE
 */
static int
scu_scsa_abort(struct scsi_address *ap, struct scsi_pkt *pkt)
{
	scu_ctl_t	*scu_ctlp;
	scu_subctl_t	*scu_subctlp;
	scu_tgt_t	*scu_tgtp;
	scu_lun_t	*scu_lunp;
	scu_cmd_t	*scu_cmdp = NULL;
	int		rval;

	scu_ctlp = SCU_ADDR_TO_HBA(ap);
	mutex_enter(&scu_ctlp->scu_lock);
	if (!scu_ctlp->scu_started) {
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_TASK,
		    SCUDBG_TRACE|SCUDBG_ERROR,
		    "%s: HBA dead, pkt 0x%p", __func__, (void *)pkt);
		mutex_exit(&scu_ctlp->scu_lock);
		return (FALSE);
	}
	mutex_exit(&scu_ctlp->scu_lock);

	scu_lunp =
	    (scu_lun_t *)scsi_device_hba_private_get(scsi_address_device(ap));

	if (scu_lunp == NULL || scu_lunp->scul_tgtp == NULL) {
		SCUDBG(scu_ctlp, SCUDBG_TGT, SCUDBG_TRACE|SCUDBG_ERROR,
		    "%s: NO target/lun to abort packet 0x%p",
		    __func__, (void *)pkt);
		return (FALSE);
	}

	scu_tgtp = scu_lunp->scul_tgtp;
	scu_subctlp = scu_tgtp->scut_subctlp;

	if (pkt != NULL) {
		/* Abort the specified packet */
		scu_cmdp = SCU_PKT_TO_CMD(pkt);
		ASSERT(scu_tgtp == scu_cmdp->cmd_tgtp);
		rval = scu_abort_cmd(scu_subctlp, scu_cmdp);
	} else {
		int	i;
		/* If pkt is NULL, then abort task set */
		for (i = 0; i < scu_ctlp->scu_lib_ctl_num; i++) {
			rval = scu_lib_task_management(scu_subctlp, scu_tgtp,
			    SCI_SAS_ABORT_TASK_SET, scu_lunp->scul_lun_num,
			    SCI_CONTROLLER_INVALID_IO_TAG, NULL, 0,
			    do_polled_io);
		}
	}

	if (rval != SCU_TASK_GOOD) {
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_TASK,
		    SCUDBG_TRACE|SCUDBG_ERROR, "%s: task management failed "
		    "to abort cmd 0x%p rval completion status %d",
		    __func__, (void *)scu_cmdp, rval);
		rval = FALSE;
	} else {
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_TASK,
		    SCUDBG_TRACE, "%s: task management succeed to abort "
		    "cmd 0x%p", __func__, (void *)scu_cmdp);
		rval = TRUE;

	}
	return (rval);
}

/*
 * All resets for a device go through scu_reset_device(). It determines the
 * proper reset level for device when multiple resets pending. Tiered reset
 * approach is taken: lun reset, target reset, hard reset, finally controller
 * reset.
 */
static int
scu_reset_device(scu_tgt_t *scu_tgtp, scu_lun_t *scu_lunp, int func, int force)
{
	scu_subctl_t	*scu_subctlp = scu_tgtp->scut_subctlp;
	scu_ctl_t	*scu_ctlp = scu_subctlp->scus_ctlp;
	int		func_tmp;
	uint64_t	lun_num;
	int		act_num, act_num_new, retry;
	int		rval;

	mutex_enter(&scu_tgtp->scut_lock);
	if (scu_tgtp->scut_lib_remote_device == NULL) {
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_TASK, SCUDBG_ERROR,
		    "%s: no associated SCIL remote device for scu_tgt 0x%p",
		    __func__, (void *)scu_tgtp);
		mutex_exit(&scu_tgtp->scut_lock);
		return (SCU_TASK_ERROR);
	}
	if (scu_tgtp->scut_resetting) {
		SCUDBG(scu_ctlp, SCUDBG_LUN|SCUDBG_TASK,
		    SCUDBG_TRACE|SCUDBG_INFO,
		    "%s: already in resetting func %d force %d",
		    __func__, func, force);
		if (ddi_in_panic()) {
			mutex_exit(&scu_tgtp->scut_lock);
			return (SCU_TASK_ERROR);
		}

		cv_wait(&scu_tgtp->scut_reset_cv, &scu_tgtp->scut_lock);
		func_tmp = scu_tgtp->scut_reset_func;
		mutex_exit(&scu_tgtp->scut_lock);
		return (func_tmp >= func ? SCU_TASK_CHECK : SCU_TASK_BUSY);
	}

	/* Decide reset level */
	if (scu_tgtp->scut_error) {
		func_tmp = SCI_SAS_HARD_RESET;
	} else if (scu_tgtp->scut_timeout) {
		func_tmp = SCI_SAS_I_T_NEXUS_RESET;
	} else if (scu_lunp && scu_lunp->scul_timeout) {
		func_tmp = SCI_SAS_LOGICAL_UNIT_RESET;

	/*
	 * If system is in panic but device is good, don't reset. If that
	 * makes device unavailable we have no way to get it back.
	 */
	} else if ((force == 0 || ddi_in_panic()) &&
	    scu_tgtp->scut_lib_tgt_ready) {
		mutex_exit(&scu_tgtp->scut_lock);
		return (SCU_TASK_GOOD);
	} else {
		func_tmp = func;
	}
	/*
	 * Higher reset level has bigger SCIL number. If this changed we
	 * should modify below funcs' comparison
	 */
#if (SCI_SAS_HARD_RESET < SCI_SAS_I_T_NEXUS_RESET) || \
	(SCI_SAS_I_T_NEXUS_RESET < SCI_SAS_LOGICAL_UNIT_RESET)
#error scu_scsa.c: task management function number usage error
#endif
	/* Don't lower force reset's level */
	if (force == 0 || func < func_tmp)
		func = func_tmp;
	scu_tgtp->scut_reset_func = func;
	scu_tgtp->scut_resetting = 1;
	mutex_exit(&scu_tgtp->scut_lock);

	/* First drain outstanding cmds to avoid sd reset storm */
	if (do_polled_io == 0) {
		mutex_enter(&scu_subctlp->scus_slot_lock);
		act_num = scu_subctlp->scus_slot_active_num;
		mutex_exit(&scu_subctlp->scus_slot_lock);
		for (retry = 0; act_num > 0; ) {
			delay(drv_usectohz(10000));	/* 10 msec. */

			mutex_enter(&scu_subctlp->scus_slot_lock);
			act_num_new = scu_subctlp->scus_slot_active_num;
			mutex_exit(&scu_subctlp->scus_slot_lock);
			if (act_num == act_num_new) {
				if (++retry > 10)
					break;
			} else {
				retry = 0;
				act_num = act_num_new;
			}
		}
	}

	/* Execute reset */
	lun_num = (func == SCI_SAS_LOGICAL_UNIT_RESET) ? \
	    scu_lunp->scul_lun_num : 0;
	rval = scu_lib_task_management(scu_subctlp, scu_tgtp, func, lun_num,
	    SCI_CONTROLLER_INVALID_IO_TAG, NULL, 0, do_polled_io);

	mutex_enter(&scu_tgtp->scut_lock);
	if (rval == SCU_TASK_GOOD) {
		/*
		 * Device in task management state doesn't accept IO requests.
		 * Wait for 2 seconds for device back to ready state.
		 */
		while (scu_tgtp->scut_lib_tgt_ready ==  0) {
			clock_t		lbolt, ret;
			lbolt = ddi_get_lbolt() + drv_usectohz(2000000);
			ret = cv_timedwait(&scu_tgtp->scut_reset_complete_cv,
			    &scu_tgtp->scut_lock, lbolt);
			if (ret == -1) {
				SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_TASK,
				    SCUDBG_ERROR,
				    "%s: !!! scu_tgt 0x%p ready timeout",
				    __func__, (void *)scu_tgtp);
				rval = SCU_TASK_ERROR;
				break;
			}
		}
	}
	if (rval == SCU_TASK_GOOD) {
		/* Clear timeouts accordingly */
		switch (func) {
		case SCI_SAS_HARD_RESET:
			scu_tgtp->scut_error = 0;
			/* FALLTHROUGH */
		case SCI_SAS_I_T_NEXUS_RESET:
			scu_clear_tgt_timeout(scu_tgtp);
			break;
		case SCI_SAS_LOGICAL_UNIT_RESET:
			if (scu_lunp->scul_timeout) {
				scu_lunp->scul_timeout = 0;
				scu_tgtp->scut_lun_timeouts--;
			}
			break;
		default:
			ASSERT(0);
		}
	}
	scu_tgtp->scut_resetting = 0;
	cv_broadcast(&scu_tgtp->scut_reset_cv);

	if (rval == SCU_TASK_GOOD) {
		mutex_enter(&scu_ctlp->scu_event_lock);
		scu_event_dispatch(scu_ctlp, SCU_EVENT_ID_DEVICE_RESET,
		    scu_tgtp->scut_tgt_num);
		mutex_exit(&scu_ctlp->scu_event_lock);
	}
	mutex_exit(&scu_tgtp->scut_lock);

	return (rval);
}

/*
 * Reset LUN
 *
 * return value:
 *	SCU_TASK_GOOD
 *	SCU_TASK_BUSY
 *	SCU_TASK_ERROR
 *	SCU_TASK_DEVICE_RESET
 *	SCU_TASK_TIMEOUT
 */
int
scu_reset_lun(scu_subctl_t *scu_subctlp, scu_lun_t *scu_lunp, int force)
{
	scu_ctl_t	*scu_ctlp = scu_subctlp->scus_ctlp;
	scu_tgt_t	*scu_tgtp = scu_lunp->scul_tgtp;
	int		rval;

	SCUDBG(scu_ctlp, SCUDBG_LUN|SCUDBG_TASK, SCUDBG_TRACE|SCUDBG_INFO,
	    "%s: enter for lun 0x%p lun num %"PRIx64" force %d",
	    __func__, (void *)scu_lunp, scu_lunp->scul_lun_num, force);
	rval = scu_reset_device(scu_tgtp, scu_lunp, SCI_SAS_LOGICAL_UNIT_RESET,
	    force);
	if (rval == SCU_TASK_CHECK) {
		mutex_enter(&scu_tgtp->scut_lock);
		if (scu_lunp->scul_timeout == 0) {
			rval = SCU_TASK_GOOD;
		} else if (scu_tgtp->scut_timeout == 0 ||
		    scu_tgtp->scut_error == 0) {
			rval = SCU_TASK_ERROR;
		} else {
			rval = SCU_TASK_TIMEOUT;
		}
		mutex_exit(&scu_tgtp->scut_lock);
	}
	SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_TASK, SCUDBG_TRACE|SCUDBG_INFO,
	    "%s: exit %d", __func__, rval);
	return (rval);
}

/*
 * Reset TARGET(device)
 *
 * return value:
 *	SCU_TASK_GOOD
 *	SCU_TASK_BUSY
 *	SCU_TASK_ERROR
 *	SCU_TASK_DEVICE_RESET
 *	SCU_TASK_TIMEOUT
 */
int
scu_reset_target(scu_subctl_t *scu_subctlp, scu_tgt_t *scu_tgtp, int force)
{
	scu_ctl_t	*scu_ctlp = scu_subctlp->scus_ctlp;
	int		rval;

	SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_TASK, SCUDBG_TRACE|SCUDBG_INFO,
	    "%s: enter for target 0x%p force %d", __func__,
	    (void *)scu_tgtp, force);
	do {
		rval = scu_reset_device(scu_tgtp, NULL,
		    SCI_SAS_I_T_NEXUS_RESET, force);
		if (rval == SCU_TASK_CHECK) {
			mutex_enter(&scu_tgtp->scut_lock);
			if (scu_tgtp->scut_timeout == 0)
				rval = SCU_TASK_GOOD;
			else if (scu_tgtp->scut_error)
				rval = SCU_TASK_TIMEOUT;
			else
				rval = SCU_TASK_ERROR;
			mutex_exit(&scu_tgtp->scut_lock);
		}
	} while (rval == SCU_TASK_BUSY);
	SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_TASK, SCUDBG_TRACE|SCUDBG_INFO,
	    "%s: exit %d", __func__, rval);
	return (rval);
}

/*
 * Reset port
 */
int
scu_hard_reset(scu_subctl_t *scu_subctlp, scu_tgt_t *scu_tgtp, int force)
{
	scu_ctl_t	*scu_ctlp = scu_subctlp->scus_ctlp;
	int		rval;

	SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_TASK, SCUDBG_TRACE|SCUDBG_INFO,
	    "%s: enter for target 0x%p force %d", __func__,
	    (void *)scu_tgtp, force);
	do {
		rval = scu_reset_device(scu_tgtp, NULL,
		    SCI_SAS_HARD_RESET, force);
		if (rval == SCU_TASK_CHECK) {
			mutex_enter(&scu_tgtp->scut_lock);
			rval = scu_tgtp->scut_error ? \
			    SCU_TASK_ERROR : SCU_TASK_GOOD;
			mutex_exit(&scu_tgtp->scut_lock);
		}
	} while (rval == SCU_TASK_BUSY);
	SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_TASK, SCUDBG_TRACE|SCUDBG_INFO,
	    "%s: exit %d", __func__, rval);
	return (rval);
}

int
scu_reset_all(scu_subctl_t *scu_subctlp)
{
	scu_ctl_t	*scu_ctlp = scu_subctlp->scus_ctlp;
	scu_tgt_t	*scu_tgtp;
	int		i, begin;
	int		rval;

	begin = scu_subctlp->scus_num * SCI_MAX_REMOTE_DEVICES;
	for (i = begin; i < begin + SCI_MAX_REMOTE_DEVICES; i++) {
		if ((scu_tgtp = scu_ctlp->scu_tgts[i]) == NULL)
			continue;
		rval = scu_hard_reset(scu_subctlp, scu_tgtp, 1);
		if (rval != SCU_TASK_GOOD)
			return (rval);
	}
	return (SCU_TASK_GOOD);
}

/*
 * Reset controller
 *
 * return value:
 *	SCU_TASK_GOOD
 *	SCU_TASK_ERROR
 */
int
scu_reset_controller(scu_subctl_t *scu_subctlp, int force)
{
	scu_ctl_t	*scu_ctlp = scu_subctlp->scus_ctlp;
	int		rval;
	int		i;
	scu_tgt_t	*scu_tgtp = NULL;
	SCI_REMOTE_DEVICE_HANDLE_T	scif_device;

	SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_TASK, SCUDBG_TRACE|SCUDBG_INFO,
	    "%s: enter", __func__);

	mutex_enter(&scu_ctlp->scu_lock);
	if (force == 0 && scu_subctlp->scus_error == 0) {
		mutex_exit(&scu_ctlp->scu_lock);
		return (SCU_TASK_GOOD);
	}
	if (scu_subctlp->scus_resetting == 1) {
		SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_TASK, SCUDBG_INFO,
		    "%s: already in resetting", __func__);
		if (do_polled_io) {
			scu_subctlp->scus_failed = 1;
		} else {
			cv_wait(&scu_subctlp->scus_reset_cv,
			    &scu_ctlp->scu_lock);
		}
		goto exit;
	}
	scu_subctlp->scus_resetting = 1;
	scu_subctlp->scus_adapter_is_ready = 0;
	mutex_exit(&scu_ctlp->scu_lock);

	/* Reset controller */
	rval = scif_controller_reset(scu_subctlp->scus_scif_ctl_handle);
	if (rval != SCI_SUCCESS) {
		SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_RECOVER, SCUDBG_ERROR,
		    "%s: controller reset failed for sub-controller 0x%p "
		    "rval %d, here a FMA ereport should be triggered",
		    __func__, (void *)scu_subctlp, rval);
		goto done;
	}
	scu_clear_active_cmds(scu_subctlp);

	/* Init controller */
	SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_RECOVER, SCUDBG_INFO,
	    "%s: controller reseted for sub-controller 0x%p",
	    __func__, (void *)scu_subctlp);
	rval = scif_controller_initialize(
	    scu_subctlp->scus_scif_ctl_handle);
	if (rval != SCI_SUCCESS) {
		SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_RECOVER,
		    SCUDBG_ERROR,
		    "%s: subctl 0x%p initialization failed rval %d, "
		    "here a FMA ereport should be triggered",
		    __func__, (void *)scu_subctlp, rval);
		goto done;
	}

	/*
	 * Start controller
	 * Controller startup process should not be interrupted by the
	 * hardware. SCIC will disable intrs, but ...
	 */
	rval = scu_disable_intrs(scu_ctlp);
	if (rval != DDI_SUCCESS) {
		SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_RECOVER,
		    SCUDBG_ERROR,
		    "%s: subctl 0x%p intr disable failed rval %d, "
		    "here a FMA ereport should be triggered",
		    __func__, (void *)scu_subctlp, rval);
		goto done;
	}
	rval = scif_controller_start(
	    scu_subctlp->scus_scif_ctl_handle,
	    scif_controller_get_suggested_start_timeout(
	    scu_subctlp-> scus_scif_ctl_handle));
#ifdef DEBUG
	if (rval != SCI_SUCCESS) {
		SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_RECOVER,
		    SCUDBG_ERROR,
		    "%s: subctl 0x%p start failed rval %d, "
		    "here a FMA ereport should be triggered",
		    __func__, (void *)scu_subctlp, rval);
	} else {
		SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_RECOVER, SCUDBG_INFO,
		    "%s: subctl 0x%p started", __func__, (void *)scu_subctlp);
	}
#endif
	/*
	 * This is not an endless loop. If scif_controller_start() return
	 * SCI_SUCCESS, adapter will be ready at most in the start_timeout
	 * time.
	 */
	do {
		(void) scu_poll_intr(scu_ctlp);
		drv_usecwait(1000);	/* 1 millisec. */
	} while (scu_subctlp->scus_adapter_is_ready == 0);
	rval = scu_enable_intrs(scu_ctlp);
#ifdef DEBUG
	if (rval != DDI_SUCCESS) {
		SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_RECOVER,
		    SCUDBG_ERROR,
		    "%s: subctl 0x%p intr enable failed rval %d, "
		    "here a FMA ereport should be triggered",
		    __func__, (void *)scu_subctlp, rval);
	}
#endif

done:
	mutex_enter(&scu_ctlp->scu_lock);
	if (rval != SCI_SUCCESS) {
		scu_subctlp->scus_failed = 1;
	} else {
		/* Wait controller ready */
		while (scu_subctlp->scus_adapter_is_ready == 0) {
			cv_wait(&scu_subctlp->scus_reset_complete_cv,
			    &scu_ctlp->scu_lock);
		}

		/*
		 * Recover the associate between driver target and
		 * SCIF target
		 */
		for (i = 0; i < scu_ctlp->scu_max_dev; i++) {
			scu_tgtp = scu_ctlp->scu_tgts[i];
			if (scu_tgtp == NULL)
				continue;
			scif_device = scu_tgtp->scut_lib_remote_device;
			ASSERT(scif_device != NULL);

			(void) sci_object_set_association(scif_device,
			    scu_tgtp);
		}
	}

	scu_clear_tgt_timeouts(scu_subctlp);
	scu_subctlp->scus_resetting = 0;
	if (scu_subctlp->scus_failed == 0)
		scu_subctlp->scus_error = 0;
	cv_broadcast(&scu_subctlp->scus_reset_cv);
exit:
	rval = scu_subctlp->scus_failed ? SCU_TASK_ERROR : SCU_TASK_GOOD;
	mutex_exit(&scu_ctlp->scu_lock);

	if (rval == SCU_TASK_ERROR) {
		SCUDBG(scu_ctlp, SCUDBG_FMA, SCUDBG_ERROR,
		    "%s: failed to reset controller",
		    __func__);
		scu_fm_ereport(scu_ctlp, DDI_FM_DEVICE_INTERN_CORR);
		ddi_fm_service_impact(scu_ctlp->scu_dip,
		    DDI_SERVICE_LOST);
	}

	return (rval);
}

/*
 * SCSA entry point - tran_reset
 *
 * Return Value:
 *	- TRUE
 *	- FALSE
 */
static int
scu_scsa_reset(struct scsi_address *ap, int level)
{
	scu_ctl_t	*scu_ctlp;
	scu_tgt_t	*scu_tgtp;
	scu_lun_t	*scu_lunp;
	int		rval = SCU_TASK_GOOD;

	scu_ctlp = SCU_ADDR_TO_HBA(ap);
	mutex_enter(&scu_ctlp->scu_lock);
	if (!scu_ctlp->scu_started) {
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_TASK,
		    SCUDBG_TRACE|SCUDBG_ERROR, "%s: HBA dead", __func__);
		mutex_exit(&scu_ctlp->scu_lock);
		return (FALSE);
	}
	mutex_exit(&scu_ctlp->scu_lock);

	scu_lunp =
	    (scu_lun_t *)scsi_device_hba_private_get(scsi_address_device(ap));
	if (scu_lunp == NULL || scu_lunp->scul_tgtp == NULL) {
		SCUDBG(scu_ctlp,
		    (level == RESET_LUN ? SCUDBG_LUN : \
		    (level == RESET_TARGET ? SCUDBG_TGT : SCUDBG_HBA)) | \
		    SCUDBG_TASK, SCUDBG_TRACE|SCUDBG_ERROR,
		    "%s: no such device for scsi address 0x%p",
		    __func__, (void *)ap);
		goto out;
	}
	scu_tgtp = scu_lunp->scul_tgtp;

	SCUDBG(scu_ctlp, SCUDBG_HBA, SCUDBG_INFO,
	    "%s: target 0x%p lun 0x%p level %d",
	    __func__, (void *)scu_tgtp, (void *)scu_lunp, level);

	switch (level) {
	case RESET_ALL:
		rval = scu_reset_all(scu_tgtp->scut_subctlp);
		break;

	case RESET_LUN:
		rval = scu_reset_lun(scu_tgtp->scut_subctlp, scu_lunp, 1);
		break;

	case RESET_TARGET:
		rval = scu_reset_target(scu_tgtp->scut_subctlp, scu_tgtp, 1);
		break;

	default:
		scu_log(scu_ctlp, CE_WARN, "!%s: got failure with "
		    "unrecognized %d level", __func__, level);
		rval = FALSE;
	}

out:
	if (rval == SCU_TASK_GOOD)
		rval = TRUE;
	else
		rval = FALSE;

	scu_flush_cmd(scu_ctlp, NULL);
	return (rval);
}

/*
 * SCSA entry point - tran_reset_notify
 */
static int
scu_scsa_reset_notify(struct scsi_address *ap, int flag,
    void (*callback)(caddr_t), caddr_t arg)
{
	scu_ctl_t	*scu_ctlp;

	scu_ctlp = SCU_ADDR_TO_HBA(ap);

	return (scsi_hba_reset_notify_setup(ap, flag, callback, arg,
	    &scu_ctlp->scu_lock, &scu_ctlp->scu_reset_notify_list));
}

/*
 * SCSA entry point - tran_getcap
 *
 * Return Value:
 *	1
 *	0
 *	-1
 */
/*ARGSUSED*/
static int
scu_scsa_getcap(struct scsi_address *ap, char *cap, int whom)
{
	scu_ctl_t *scu_ctlp = SCU_ADDR_TO_HBA(ap);
	scu_tgt_t *scu_tgtp;
	int rval = 0;
	int index;

	if (cap == NULL)
		return (-1);

	index = scsi_hba_lookup_capstr(cap);
	if (index == -1) {
		return (-1);
	}

	scu_tgtp = scu_addr_to_tgt(ap, NULL, NULL);
	if (scu_tgtp == NULL) {
		SCUDBG(scu_ctlp, SCUDBG_TGT, SCUDBG_WARNING,
		    "%s: cannot find target scsi_address 0x%p",
		    __func__, (void *)ap);
		return (-1);
	}

	switch (index) {
	case SCSI_CAP_DMA_MAX:
		rval = (int)scu_ctlp->scu_data_dma_attr.dma_attr_maxxfer;
		break;

	case SCSI_CAP_INITIATOR_ID:
		rval = INT_MAX;	/* argh */
		break;

	case SCSI_CAP_DISCONNECT:
	case SCSI_CAP_SYNCHRONOUS:
	case SCSI_CAP_WIDE_XFER:
	case SCSI_CAP_PARITY:
	case SCSI_CAP_ARQ:
	case SCSI_CAP_UNTAGGED_QING:
		rval = 1;
		break;

	case SCSI_CAP_TAGGED_QING:
		rval = 1;
		break;

	case SCSI_CAP_MSG_OUT:
	case SCSI_CAP_RESET_NOTIFICATION:
	case SCSI_CAP_QFULL_RETRIES:
	case SCSI_CAP_QFULL_RETRY_INTERVAL:
		break;

	case SCSI_CAP_SCSI_VERSION:
		rval = SCSI_VERSION_3;
		break;

	case SCSI_CAP_INTERCONNECT_TYPE:
		rval = INTERCONNECT_SAS;
		break;

	case SCSI_CAP_CDB_LEN:
		rval = CDB_GROUP4;
		break;

	case SCSI_CAP_LUN_RESET:
		if (scu_tgtp->scut_dtype == SCU_DTYPE_SATA) {
			rval = 0;
		} else {
			rval = 1;
		}
		break;

	default:
		rval = -1;
		break;
	}

	mutex_exit(&scu_tgtp->scut_lock);

	SCUDBG(scu_ctlp, SCUDBG_TGT, SCUDBG_TRACE,
	    "%s: cap %s rval %d", __func__, cap, rval);

	return (rval);
}

/*
 * SCSA entry point - tran_setcap
 *
 * Return Value:
 *	1
 *	0
 *	-1
 */
/*ARGSUSED*/
static int
scu_scsa_setcap(struct scsi_address *ap, char *cap, int value, int whom)
{
	scu_ctl_t *scu_ctlp = SCU_ADDR_TO_HBA(ap);
	int index;
	int rval = 0;

	if (cap == NULL) {
		return (-1);
	}

	index = scsi_hba_lookup_capstr(cap);
	if (index == -1) {
		return (-1);
	}

	switch (index) {
	case SCSI_CAP_DMA_MAX:
	case SCSI_CAP_INITIATOR_ID:
	case SCSI_CAP_DISCONNECT:
	case SCSI_CAP_SYNCHRONOUS:
	case SCSI_CAP_WIDE_XFER:
	case SCSI_CAP_PARITY:
	case SCSI_CAP_ARQ:
	case SCSI_CAP_UNTAGGED_QING:
		break;

	case SCSI_CAP_TAGGED_QING:
		rval = 1;
		break;

	case SCSI_CAP_MSG_OUT:
	case SCSI_CAP_RESET_NOTIFICATION:
	case SCSI_CAP_QFULL_RETRIES:
	case SCSI_CAP_QFULL_RETRY_INTERVAL:
		break;

	case SCSI_CAP_INTERCONNECT_TYPE:
	case SCSI_CAP_LUN_RESET:
		break;

	default:
		rval = -1;
		break;
	}

	SCUDBG(scu_ctlp, SCUDBG_TGT, SCUDBG_TRACE,
	    "%s: cap %s value %d rval %d",
	    __func__, cap, value, rval);

	return (rval);
}

/*
 * SCSA entry point - tran_setup_pkt
 */
/*ARGSUSED*/
static int
scu_scsa_setup_pkt(struct scsi_pkt *pkt, int (*callback)(caddr_t),
    caddr_t cbarg)
{
	scu_cmd_t	*scu_cmdp = pkt->pkt_ha_private;

	bzero(scu_cmdp, sizeof (scu_cmd_t));
	scu_cmdp->cmd_pkt = pkt;

	return (0);
}

/*
 * SCSA entry point - tran_teardown_pkt
 */
static void
scu_scsa_teardown_pkt(struct scsi_pkt *pkt)
{
	scu_cmd_t	*scu_cmdp = pkt->pkt_ha_private;

	ASSERT(scu_cmdp->cmd_pkt == pkt);

	scu_cmdp->cmd_tgtp = NULL;
	scu_cmdp->cmd_lunp = NULL;
}

static void
scu_draining(void *arg)
{
	scu_ctl_t	*scu_ctlp = arg;
	scu_subctl_t	*scu_subctlp;
	scu_tgt_t	*scu_tgtp;
	int		i, quiesce_needed = 0;

	scu_subctlp = scu_ctlp->scu_subctls;
	for (i = 0; i < scu_ctlp->scu_lib_ctl_num; i++) {
		mutex_enter(&scu_subctlp->scus_slot_lock);
		if (scu_subctlp->scus_slot_active_num != 0) {
			quiesce_needed = 1;
		}
		scu_subctlp++;
	}

	if (quiesce_needed == 1) {
		/*
		 * Set the target node flag again since a SCSI bus
		 * reset may have happened
		 */
		for (i = 0; i < scu_ctlp->scu_max_dev; i++) {
			scu_tgtp = scu_ctlp->scu_tgts[i];
			if (scu_tgtp == NULL)
				continue;

			mutex_enter(&scu_tgtp->scut_lock);
			scu_tgtp->scut_draining = 1;
			mutex_exit(&scu_tgtp->scut_lock);
		}

		/* Re-stall the handler */
		mutex_enter(&scu_ctlp->scu_lock);
		scu_ctlp->scu_quiesce_timeid = timeout(scu_draining,
		    scu_ctlp, scu_quiesce_interval);
		mutex_exit(&scu_ctlp->scu_lock);
	} else {
		/* No active commands */
		mutex_enter(&scu_ctlp->scu_lock);
		scu_ctlp->scu_quiesce_timeid = 0;
		cv_signal(&scu_ctlp->scu_cv);
		mutex_exit(&scu_ctlp->scu_lock);
	}
}

/*
 * SCSA entry point - tran_quiesce
 *
 * Return Value:
 *	- 0
 *	- -1
 */
static int
scu_scsa_quiesce(dev_info_t *dip)
{
	scu_ctl_t	*scu_ctlp;
	scu_subctl_t	*scu_subctlp;
	scu_tgt_t	*scu_tgtp;
	int		i, quiesce_needed = 0;

	/* Return directly for iport */
	if (scsi_hba_iport_unit_address(dip) != NULL) {
		return (0);
	}

	scu_ctlp = ddi_get_soft_state(scu_softc_state, ddi_get_instance(dip));
	if (scu_ctlp == NULL) {
		return (-1);
	}

	mutex_enter(&scu_ctlp->scu_lock);
	if (!scu_ctlp->scu_started) {
		SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_DRAIN,
		    SCUDBG_ERROR|SCUDBG_TRACE,
		    "%s: hba dead", __func__);
		mutex_exit(&scu_ctlp->scu_lock);
		return (-1);
	}

	SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_DRAIN, SCUDBG_TRACE,
	    "%s: enter", __func__);

	/* Set target node flag */
	for (i = 0; i < scu_ctlp->scu_max_dev; i++) {
		scu_tgtp = scu_ctlp->scu_tgts[i];
		if (scu_tgtp == NULL)
			continue;

		mutex_enter(&scu_tgtp->scut_lock);
		if (scu_tgtp->scut_active_pkts) {
			scu_tgtp->scut_draining = 1;
		}
		mutex_exit(&scu_tgtp->scut_lock);
	}

	scu_subctlp = scu_ctlp->scu_subctls;
	for (i = 0; i < scu_ctlp->scu_lib_ctl_num; i++) {
		mutex_enter(&scu_subctlp->scus_slot_lock);
		if (scu_subctlp->scus_slot_active_num != 0) {
			SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_DRAIN, SCUDBG_TRACE,
			    "%s: return directly since no active packets",
			    __func__);
			quiesce_needed = 1;
		}
		mutex_exit(&scu_subctlp->scus_slot_lock);

		scu_subctlp++;
	}

	if (quiesce_needed == 0) {
		scu_ctlp->scu_quiesced = 1;
		mutex_exit(&scu_ctlp->scu_lock);
		return (0);
	}

	scu_quiesce_interval =
	    scu_quiesce_tick * drv_usectohz((clock_t)1000000);

	/* Start the handler */
	scu_ctlp->scu_quiesce_timeid = timeout(scu_draining,
	    scu_ctlp, scu_quiesce_interval);

	if (cv_wait_sig(&scu_ctlp->scu_cv, &scu_ctlp->scu_lock) == 0) {
		/*
		 * Quiesce was interrupted
		 */
		SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_DRAIN, SCUDBG_ERROR,
		    "%s: quiesce was interrupted", __func__);

		for (i = 0; i < scu_ctlp->scu_max_dev; i++) {
			scu_tgtp = scu_ctlp->scu_tgts[i];
			if (scu_tgtp == NULL)
				continue;
			mutex_enter(&scu_tgtp->scut_lock);
			scu_tgtp->scut_draining = 0;
			mutex_exit(&scu_tgtp->scut_lock);
			scu_start_wq(scu_ctlp, i);
		}

		if (scu_ctlp->scu_quiesce_timeid != 0) {
			(void) untimeout(scu_ctlp->scu_quiesce_timeid);
			scu_ctlp->scu_quiesce_timeid = 0;
		}

	} else {
		/*
		 * Quiesce made successfully
		 */
		ASSERT(scu_ctlp->scu_quiesce_timeid == 0);

		/* Set HBA node flag */
		scu_ctlp->scu_quiesced = 1;
	}

	SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_DRAIN, SCUDBG_TRACE,
	    "%s: return successfully", __func__);
	mutex_exit(&scu_ctlp->scu_lock);

	return (0);
}

/*
 * SCSA entry point - tran_unquiesce
 *
 * Return Value:
 *	- 0
 *	- -1
 */
static int
scu_scsa_unquiesce(dev_info_t *dip)
{
	scu_ctl_t	*scu_ctlp;
	scu_tgt_t	*scu_tgtp;
	int		i;

	/* Return directly for iport */
	if (scsi_hba_iport_unit_address(dip) != NULL) {
		return (0);
	}

	scu_ctlp = ddi_get_soft_state(scu_softc_state, ddi_get_instance(dip));
	if (scu_ctlp == NULL) {
		return (-1);
	}

	SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_DRAIN, SCUDBG_TRACE,
	    "%s: enter", __func__);

	mutex_enter(&scu_ctlp->scu_lock);
	if (!scu_ctlp->scu_started) {
		SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_DRAIN,
		    SCUDBG_ERROR|SCUDBG_TRACE,
		    "%s: hba dead", __func__);
		mutex_exit(&scu_ctlp->scu_lock);
		return (-1);
	}

	scu_ctlp->scu_quiesced = 0;
	mutex_exit(&scu_ctlp->scu_lock);

	for (i = 0; i < scu_ctlp->scu_max_dev; i++) {
		scu_tgtp = scu_ctlp->scu_tgts[i];
		if (scu_tgtp == NULL)
			continue;

		mutex_enter(&scu_tgtp->scut_lock);
		scu_tgtp->scut_draining = 0;
		mutex_exit(&scu_tgtp->scut_lock);
		scu_start_wq(scu_ctlp, i);
	}

	SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_DRAIN, SCUDBG_TRACE,
	    "%s: return successfully", __func__);
	return (0);
}

/*
 * Return Value:
 *	DDI_SUCCESS
 *	DDI_FAILURE
 */
/*ARGSUSED*/
static int
scu_smp_init(dev_info_t *self, dev_info_t *child,
    smp_hba_tran_t *tran, smp_device_t *smp_sd)
{
	scu_ctl_t			*scu_ctlp;
	scu_iport_t			*scu_iportp;
	scu_tgt_t			*scu_tgtp;
	char				*tgt_port, *ua;
	SCI_REMOTE_DEVICE_HANDLE_T	fw_device;
	SCI_REMOTE_DEVICE_HANDLE_T	parent_device, parent_core_device;
	SCI_SAS_ADDRESS_T		sas_address;
	uint64_t			wwn;
	int				num;
	SCI_STATUS			sci_status;

	scu_iportp = ddi_get_soft_state(scu_iport_softstate,
	    ddi_get_instance(self));
	if (scu_iportp == NULL)
		return (DDI_FAILURE);

	scu_ctlp = scu_iportp->scui_ctlp;
	if (scu_ctlp == NULL)
		return (DDI_FAILURE);

	/* Get "target-port" prop from devinfo node */
	if (ddi_prop_lookup_string(DDI_DEV_T_ANY, child,
	    DDI_PROP_DONTPASS|DDI_PROP_NOTPROM,
	    SCSI_ADDR_PROP_TARGET_PORT, &tgt_port) != DDI_SUCCESS) {
		SCUDBG(scu_ctlp, SCUDBG_SMP, SCUDBG_TRACE,
		    "%s: failed to look up target-port prop",
		    __func__);
		return (DDI_SUCCESS);
	}

	SCUDBG(scu_ctlp, SCUDBG_SMP|SCUDBG_INIT, SCUDBG_INFO,
	    "%s: got tgt_port %s", __func__, tgt_port);

	/* Check the iport status */
	if (scu_iportp->scui_ua_state == SCU_UA_INACTIVE) {
		SCUDBG(scu_ctlp, SCUDBG_SMP, SCUDBG_ERROR,
		    "%s: iport 0x%p is not active",
		    __func__, (void *)scu_iportp);
		ddi_prop_free(tgt_port);
		return (DDI_FAILURE);
	}

	mutex_enter(&scu_ctlp->scu_lock);

	/* Check the target softstate */
	scu_tgtp = scu_find_target(scu_iportp, tgt_port);

	if (scu_tgtp) {
		SCUDBG(scu_ctlp, SCUDBG_SMP, SCUDBG_INFO|SCUDBG_ERROR,
		    "%s: target 0x%p @ tgt_port %s already exists",
		    __func__, (void *)scu_tgtp, tgt_port);
	} else {
		scu_tgtp = scu_allocate_target(scu_iportp, tgt_port);
		if (scu_tgtp == NULL) {
			SCUDBG(scu_ctlp, SCUDBG_SMP, SCUDBG_ERROR|SCUDBG_TRACE,
			    "%s: cannot allocate target %s",
			    __func__, tgt_port);
			ddi_prop_free(tgt_port);
			mutex_exit(&scu_ctlp->scu_lock);
			return (DDI_FAILURE);
		}
	}

	SCUDBG(scu_ctlp, SCUDBG_SMP|SCUDBG_INIT, SCUDBG_INFO,
	    "%s: %s (%s) target 0x%p, dip 0x%p",
	    __func__, ddi_get_name(child), tgt_port,
	    (void *)scu_tgtp, (void *)child);

	mutex_enter(&scu_tgtp->scut_lock);

	/* Add to the list for the first tran_tgt_init */
	if (scu_tgtp->scut_tgt_num == SCU_INVALID_TARGET_NUM) {
		for (num = 0; num < scu_ctlp->scu_max_dev; num++) {
			if (scu_ctlp->scu_tgts[num] != NULL) {
				continue;
			}

			scu_ctlp->scu_tgts[num] = scu_tgtp;
			scu_tgtp->scut_tgt_num = (uint16_t)num;
			break;
		}

		if (num == scu_ctlp->scu_max_dev) {
			SCUDBG(scu_ctlp, SCUDBG_SMP, SCUDBG_ERROR|SCUDBG_TRACE,
			    "%s: too many targets", __func__);
			goto err_out;
		}
	}

	scu_tgtp->scut_smp_sd = smp_sd;

	scu_tgtp->scut_ref_count++;

	fw_device = scu_tgtp->scut_lib_remote_device;
	sci_status = scif_remote_device_get_containing_device(fw_device,
	    &parent_device);

	if (sci_status != SCI_SUCCESS) {
		/* directed attached device */
		scic_sds_phy_get_sas_address(
		    scu_iportp->scui_primary_phy->scup_lib_phyp,
		    &sas_address);
	} else {
		/* expander behind attached device */
		parent_core_device =
		    scif_remote_device_get_scic_handle(parent_device);
		scic_remote_device_get_sas_address(parent_core_device,
		    &sas_address);
	}

	smp_sd->smp_sd_hba_private = scu_tgtp;

	/* Update the attached-port prop */
	wwn = SCU_SAS_ADDRESS(BE_32(sas_address.high),
	    BE_32(sas_address.low));
	ua = scsi_wwn_to_wwnstr(wwn, 1, NULL);
	if (smp_device_prop_update_string(smp_sd, SCSI_ADDR_PROP_ATTACHED_PORT,
	    ua) != DDI_SUCCESS) {
		SCUDBG(scu_ctlp, SCUDBG_SMP, SCUDBG_ERROR,
		    "%s: failed to set attached-port prop",
		    __func__);
	}

	/* Update the pm props for smp */
	(void) scu_smhba_update_tgt_pm_props(scu_tgtp);

	mutex_exit(&scu_tgtp->scut_lock);
	mutex_exit(&scu_ctlp->scu_lock);

	(void) scsi_free_wwnstr(ua);
	ddi_prop_free(tgt_port);
	return (DDI_SUCCESS);

err_out:
	scu_tgtp->scut_smp_sd = NULL;
	scu_tgtp->scut_tgt_num = SCU_INVALID_TARGET_NUM;
	mutex_exit(&scu_tgtp->scut_lock);
	mutex_exit(&scu_ctlp->scu_lock);
	ddi_soft_state_bystr_free(scu_iportp->scui_tgt_sstate,
	    scu_tgtp->scut_unit_address);
	ddi_prop_free(tgt_port);
	return (DDI_FAILURE);
}

/*ARGSUSED*/
static void
scu_smp_free(dev_info_t *self, dev_info_t *child,
    smp_hba_tran_t *tran, smp_device_t *smp_sd)
{
	scu_ctl_t	*scu_ctlp;
	scu_iport_t	*scu_iportp;
	scu_tgt_t	*scu_tgtp;
	scu_phy_t	*scu_phyp;
	char		*tgt_port;

	scu_iportp = ddi_get_soft_state(scu_iport_softstate,
	    ddi_get_instance(self));
	if (scu_iportp == NULL)
		return;

	scu_ctlp = scu_iportp->scui_ctlp;
	if (scu_ctlp == NULL)
		return;

	/* Get target-port prop from devinfo node */
	if (ddi_prop_lookup_string(DDI_DEV_T_ANY, child,
	    DDI_PROP_DONTPASS|DDI_PROP_NOTPROM,
	    SCSI_ADDR_PROP_TARGET_PORT, &tgt_port) != DDI_SUCCESS) {
		SCUDBG(scu_ctlp, SCUDBG_SMP, SCUDBG_TRACE,
		    "%s: failed to look up target-port prop",
		    __func__);
		return;
	}

	/* Retrieve softstate via unit-address */
	mutex_enter(&scu_ctlp->scu_lock);
	scu_tgtp = ddi_soft_state_bystr_get(scu_iportp->scui_tgt_sstate,
	    tgt_port);
	SCUDBG(scu_ctlp, SCUDBG_SMP, SCUDBG_INFO,
	    "%s: %s @ tgt_port %s",
	    __func__, ddi_get_name(child), tgt_port);
	ddi_prop_free(tgt_port);

	if (scu_tgtp == NULL) {
		SCUDBG(scu_ctlp, SCUDBG_SMP, SCUDBG_ERROR,
		    "%s: target softstate cannot be found",
		    __func__);
		mutex_exit(&scu_ctlp->scu_lock);
		return;
	}

	mutex_enter(&scu_tgtp->scut_lock);

	scu_tgtp->scut_ref_count--;

	if (scu_tgtp->scut_ref_count == 0) {
		SCUDBG(scu_ctlp, SCUDBG_SMP|SCUDBG_INIT, SCUDBG_INFO,
		    "%s: free target 0x%p tgt_num %d",
		    __func__, (void *)scu_tgtp, scu_tgtp->scut_tgt_num);
		scu_ctlp->scu_tgts[scu_tgtp->scut_tgt_num] = NULL;
		scu_tgtp->scut_tgt_num = SCU_INVALID_TARGET_NUM;

		scu_phyp = scu_tgtp->scut_phyp;
		if (scu_phyp && scu_phyp->scup_tgtp == scu_tgtp)
			scu_phyp->scup_tgtp = NULL;

		scu_tgtp->scut_phyp = NULL;
		scu_tgtp->scut_dip = NULL;

		scu_iportp = scu_get_iport_by_ua(scu_ctlp,
		    scu_tgtp->scut_iport_ua);
		if (scu_iportp == NULL) {
			SCUDBG(scu_ctlp, SCUDBG_SMP|SCUDBG_INIT, SCUDBG_ERROR,
			    "%s: cannot find associated iport for target 0x%p",
			    __func__, (void *)scu_tgtp);
			mutex_exit(&scu_tgtp->scut_lock);
			mutex_exit(&scu_ctlp->scu_lock);
			return;
		}

		if (scu_tgtp->scut_iport_ua) {
			strfree(scu_tgtp->scut_iport_ua);
		}

		mutex_destroy(&scu_tgtp->scut_lock);
		mutex_destroy(&scu_tgtp->scut_wq_lock);

		ddi_soft_state_bystr_free(scu_iportp->scui_tgt_sstate,
		    scu_tgtp->scut_unit_address);
	} else {
		mutex_exit(&scu_tgtp->scut_lock);
	}

	mutex_exit(&scu_ctlp->scu_lock);
}

static void
scu_smp_init_pkt_resplen(scu_cmd_t *scu_cmdp)
{
	struct smp_pkt		*smp_pkt;
	smp_request_frame_t	*srq;
	int			response_length, response_maxlen;

	ASSERT(scu_cmdp->cmd_smp);

	smp_pkt = scu_cmdp->cmd_smp_pkt;
	srq = (smp_request_frame_t *)smp_pkt->smp_pkt_req;
	response_maxlen = sizeof (SMP_RESPONSE_T) - SMP_RESP_MINLEN;
	response_length = srq->srf_allocated_response_len << 2;

	if (response_length == 0) {
		SCUDBG(NULL, SCUDBG_SMP, SCUDBG_INFO,
		    "%s: srf_allocated_response_len is zero for smp_pkt 0x%p",
		    __func__, (void *)smp_pkt);

		response_length = smp_pkt->smp_pkt_rspsize - SMP_RESP_MINLEN;
	}

	if (response_length > response_maxlen) {
		SCUDBG(NULL, SCUDBG_SMP, SCUDBG_INFO,
		    "%s: srf_allocated_response_len %d > %d for smp_pkt 0x%p",
		    __func__, response_length, response_maxlen,
		    (void *)smp_pkt);

		response_length = response_maxlen;
	}

	scu_cmdp->cmd_smp_resplen = response_length;
}

/*
 * SMP command will be sent down via SMP pass-through method provided by SCIL
 *
 * Return Value:
 *	DDI_SUCCESS
 *	DDI_FAILURE
 */
/*ARGSUSED*/
static int
scu_smp_start(struct smp_pkt *smp_pkt)
{
	scu_ctl_t			*scu_ctlp;
	scu_subctl_t			*scu_subctlp;
	scu_tgt_t			*scu_tgtp;
	uint64_t			wwn;
	scu_cmd_t			*scu_cmdp;
	scu_io_slot_t			*io_slot;
	SCI_STATUS			sci_status;
	int				rval = DDI_FAILURE;

	/* First got the controller softstate */
	scu_ctlp = (scu_ctl_t *)smp_pkt->smp_pkt_address->
	    smp_a_hba_tran->smp_tran_hba_private;

	/* Next find out the target device */
	bcopy(smp_pkt->smp_pkt_address->smp_a_wwn, &wwn, SAS_WWN_BYTE_SIZE);

	SCUDBG(scu_ctlp, SCUDBG_SMP, SCUDBG_TRACE,
	    "%s: start for wwn 0x%"PRIx64, __func__, wwn);

	/* Now find out the target */
	scu_tgtp = scu_get_tgt_by_wwn(scu_ctlp, wwn);
	if (scu_tgtp == NULL) {
		SCUDBG(scu_ctlp, SCUDBG_SMP, SCUDBG_TRACE|SCUDBG_ERROR,
		    "%s: cannot find smp device for wwn 0x%"PRIx64,
		    __func__, wwn);
		return (DDI_FAILURE);
	}

	scu_subctlp = scu_tgtp->scut_subctlp;

	if (scu_tgtp->scut_lib_remote_device == SCI_INVALID_HANDLE) {
		SCUDBG(scu_ctlp, SCUDBG_SMP, SCUDBG_TRACE|SCUDBG_ERROR,
		    "%s: cannot find valid SCIL smp device for wwn 0x%"PRIx64,
		    __func__, wwn);
		return (DDI_FAILURE);
	}

	SCUDBG(scu_ctlp, SCUDBG_SMP, SCUDBG_INFO,
	    "%s: smp_pkt 0x%p retry %d",
	    __func__, (void *)smp_pkt, smp_pkt->smp_pkt_will_retry);

	/* Allocate scu_cmd */
	scu_cmdp = (scu_cmd_t *)kmem_zalloc(sizeof (scu_cmd_t), KM_SLEEP);
	scu_cmdp->cmd_smp = 1;
	scu_cmdp->cmd_sync = 1;
	scu_cmdp->cmd_smp_pkt = smp_pkt;
	scu_cmdp->cmd_tgtp = scu_tgtp;
	scu_smp_init_pkt_resplen(scu_cmdp);

	/* Allocate tag */
	mutex_enter(&scu_subctlp->scus_slot_lock);
	if (scu_prepare_tag(scu_subctlp, scu_cmdp) != SCU_SUCCESS) {
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_TASK,
		    SCUDBG_TRACE|SCUDBG_ERROR,
		    "%s: scu_allocate_tag failed for cmd 0x%p",
		    __func__, (void *)scu_cmdp);
		mutex_exit(&scu_subctlp->scus_slot_lock);
		goto exit;
	}

	io_slot = scu_cmdp->cmd_slot;

	scu_smp_init_pkt_resplen(scu_cmdp);

	/* Now construct SCIF request */
	sci_status = scif_request_construct(
	    scu_subctlp->scus_scif_ctl_handle,
	    scu_tgtp->scut_lib_remote_device,
	    io_slot->scu_io_lib_tag,
	    scu_cmdp, /* smp cmd is non-detachable */
	    io_slot->scu_io_virtual_address,
	    &scu_cmdp->cmd_lib_io_request);

	SCUDBG(scu_ctlp, SCUDBG_SMP, SCUDBG_INFO,
	    "%s: cmd 0x%p smp_pkt 0x%p sci_status %d", __func__,
	    (void *)scu_cmdp, (void *)smp_pkt, sci_status);

	/* Now set the passthru_cb */
	if (sci_status == SCI_SUCCESS) {
		/* Create SMP passthrough part of the io request */
		sci_status =
		    (SCI_STATUS)scic_io_request_construct_smp_pass_through(
		    scif_io_request_get_scic_handle(
		    scu_cmdp->cmd_lib_io_request), &smp_passthru_cb);
	}

	if (sci_status != SCI_SUCCESS) {
		SCUDBG(scu_ctlp, SCUDBG_SMP, SCUDBG_ERROR,
		    "%s: cannot construct scif request for cmd 0x%p "
		    "smp_pkt 0x%p sci_status %d", __func__,
		    (void *)scu_cmdp, (void *)smp_pkt, sci_status);
		mutex_exit(&scu_subctlp->scus_slot_lock);
		goto exit;
	}

	/* Then start the request */
	sci_status = (SCI_STATUS)scif_controller_start_io(
	    scu_subctlp->scus_scif_ctl_handle,
	    scu_tgtp->scut_lib_remote_device,
	    scu_cmdp->cmd_lib_io_request,
	    io_slot->scu_io_lib_tag);
	if (sci_status != SCI_SUCCESS) {
		SCUDBG(scu_ctlp, SCUDBG_SMP, SCUDBG_INFO,
		    "%s: cannot sent down scif request for cmd 0x%p "
		    "scu_tgtp 0x%p, smp_pkt 0x%p", __func__,
		    (void *)scu_cmdp, (void *)scu_tgtp, (void *)smp_pkt);
		mutex_exit(&scu_subctlp->scus_slot_lock);
		goto exit;
	}

	io_slot->scu_io_active_timeout = smp_pkt->smp_pkt_timeout;
	scu_cmdp->cmd_started = 1;
	scu_subctlp->scus_slot_active_num++;
	mutex_exit(&scu_subctlp->scus_slot_lock);

	/* Now wait the command */
	scu_wait_cmd(scu_subctlp, scu_cmdp);
	if (scu_cmdp->cmd_smp_pkt->smp_pkt_reason == 0)
		rval = DDI_SUCCESS;

	/* Free the resources */
exit:
	kmem_free(scu_cmdp, sizeof (scu_cmd_t));

	SCUDBG(scu_ctlp, SCUDBG_SMP, SCUDBG_TRACE,
	    "%s: smp request 0x%p return value %d", __func__,
	    (void *)smp_pkt, rval);
	return (rval);
}

/*
 * The thread for handling completed command queue
 */
void
scu_cq_handler(void *arg)
{
	scu_cq_thread_t	*cq_threadp = (scu_cq_thread_t *)arg;
	scu_ctl_t	*scu_ctlp = cq_threadp->scu_cq_ctlp;
	scu_tgt_t	*scu_tgtp;
	scu_cmd_t	*scu_cmdp, *next;

	SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_IO, SCUDBG_TRACE,
	    "%s: enter", __func__);

	mutex_enter(&scu_ctlp->scu_cq_lock);

	while (!scu_ctlp->scu_cq_stop) {
		scu_cmdp = STAILQ_FIRST(&scu_ctlp->scu_cq);
		STAILQ_INIT(&scu_ctlp->scu_cq);
		mutex_exit(&scu_ctlp->scu_cq_lock);

		while (scu_cmdp) {
			next = STAILQ_NEXT(scu_cmdp, cmd_next);
			scu_tgtp = scu_cmdp->cmd_tgtp;
			SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_IO, SCUDBG_TRACE,
			    "%s: calling completion of 0x%p for target 0x%p",
			    __func__, (void *)scu_cmdp, (void *)scu_tgtp);
			if (scu_cmdp->cmd_sync == 0) {
				/* scu_tgtp will be NULL when dev gone */
				if (scu_tgtp) {
					mutex_enter(&scu_tgtp->scut_lock);
					ASSERT(scu_tgtp->scut_active_pkts);
					scu_tgtp->scut_active_pkts--;
					mutex_exit(&scu_tgtp->scut_lock);
				}
				scsi_hba_pkt_comp(scu_cmdp->cmd_pkt);
			} else {
				scu_subctl_t	*scu_subctlp = \
				    scu_tgtp->scut_subctlp;
				mutex_enter(&scu_subctlp->scus_slot_lock);
				cv_broadcast(&scu_ctlp->scu_cmd_complete_cv);
				mutex_exit(&scu_subctlp->scus_slot_lock);
			}
			scu_cmdp = next;
		}

		/* check whether there are more completions to do */
		mutex_enter(&scu_ctlp->scu_cq_lock);
		if (STAILQ_EMPTY(&scu_ctlp->scu_cq) &&
		    !scu_ctlp->scu_cq_stop) {
			cv_wait(&cq_threadp->scu_cq_thread_cv,
			    &scu_ctlp->scu_cq_lock);
		}
	}

	mutex_exit(&scu_ctlp->scu_cq_lock);
	SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_IO, SCUDBG_TRACE|SCUDBG_INFO,
	    "%s: cq thread 0x%p will exit", __func__, (void *)cq_threadp);
	thread_exit();
}

/*
 * Try to send down the commands on waiting queue
 */
void
scu_start_wq(scu_ctl_t *scu_ctlp, int num)
{
	scu_subctl_t	*scu_subctlp;
	scu_cmd_t	*scu_cmdp;
	scu_tgt_t	*scu_tgtp;
	int		rval;

	SCUDBG(scu_ctlp, SCUDBG_TGT, SCUDBG_TRACE,
	    "%s: enter for target %d", __func__, num);

	/*
	 * Avoid from accessing scu_tgts[] while scu_scsa_tran_tgt_free()
	 * is freeing its member.
	 */
	mutex_enter(&scu_ctlp->scu_lock);
	if ((scu_tgtp = scu_ctlp->scu_tgts[num]) != NULL) {
		mutex_enter(&scu_tgtp->scut_lock);
		if (scu_tgtp->scut_draining == 1) {
			SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_IO, SCUDBG_TRACE,
			    "%s: target 0x%p is draining, so return directly",
			    __func__, (void *)scu_tgtp);
			mutex_exit(&scu_tgtp->scut_lock);
			mutex_exit(&scu_ctlp->scu_lock);
			return;
		}
		mutex_exit(&scu_tgtp->scut_lock);

		mutex_enter(&scu_tgtp->scut_wq_lock);
	}
	mutex_exit(&scu_ctlp->scu_lock);

	scu_subctlp = scu_tgtp->scut_subctlp;
	while ((scu_cmdp = STAILQ_FIRST(&scu_tgtp->scut_wq)) != NULL) {
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_IO, SCUDBG_TRACE,
		    "%s: remove cmd 0x%p from wq for target 0x%p",
		    __func__, (void *)scu_cmdp, (void *)scu_tgtp);
		STAILQ_REMOVE(&scu_tgtp->scut_wq, scu_cmdp, scu_cmd, cmd_next);
		scu_cmdp->cmd_wq_queued = 0;
		mutex_exit(&scu_tgtp->scut_wq_lock);

		/* Try to start the command */
		mutex_enter(&scu_subctlp->scus_slot_lock);
		if (scu_subctlp->scus_adapter_is_ready == 0 ||
		    scu_subctlp->scus_resetting ||
		    scu_tgtp->scut_resetting ||
		    scu_tgtp->scut_lib_tgt_ready == 0) {
			mutex_exit(&scu_subctlp->scus_slot_lock);
			rval = SCU_FAILURE;
			goto out;
		}
		if (scu_cmdp->cmd_noretry == 0) {
			rval = scu_do_start_cmd(scu_tgtp->scut_subctlp,
			    scu_cmdp);
			/*
			 * Cmd fails to start either because no more tags or
			 * device is abnormal and need reset. Anyway should
			 * not hold tag.
			 */
			if (rval != SCU_SUCCESS && scu_cmdp->cmd_prepared) {
				scu_free_tag(scu_subctlp,
				    scu_detach_cmd(scu_cmdp));
			}
		} else {
			int	cmd_poll;
			scu_clear_inactive_cmd(scu_subctlp, scu_cmdp);
			cmd_poll = scu_cmdp->cmd_poll;
			mutex_exit(&scu_subctlp->scus_slot_lock);

			if (cmd_poll == 0)
				scu_flush_cmd(scu_ctlp, scu_cmdp);
			return;
		}
		mutex_exit(&scu_subctlp->scus_slot_lock);
out:
		mutex_enter(&scu_tgtp->scut_wq_lock);
		if (rval != SCU_SUCCESS) {
			scu_cmdp->cmd_wq_queued = 1;
			STAILQ_INSERT_HEAD(&scu_tgtp->scut_wq, scu_cmdp,
			    cmd_next);
			SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_IO, SCUDBG_INFO,
			    "%s: add cmd 0x%p to wq on target 0x%p again",
			    __func__, (void *)scu_cmdp, (void *)scu_tgtp);
			break;
		} else {
			SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_IO, SCUDBG_INFO,
			    "%s: cmd 0x%p on wq of target 0x%p is sent down",
			    __func__, (void *)scu_cmdp, (void *)scu_tgtp);
			scu_tgtp->scut_wq_pkts--;
		}
	}
	mutex_exit(&scu_tgtp->scut_wq_lock);

	SCUDBG(scu_ctlp, SCUDBG_TGT, SCUDBG_TRACE,
	    "%s: exit for target 0x%p", __func__, (void *)scu_tgtp);
}

void
scu_flush_cmd(scu_ctl_t *scu_ctlp, scu_cmd_t *scu_cmdp)
{
	mutex_enter(&scu_ctlp->scu_cq_lock);
	if (scu_cmdp) {
		ASSERT(scu_cmdp->cmd_poll == 0);
		STAILQ_INSERT_TAIL(&scu_ctlp->scu_cq, scu_cmdp, cmd_next);
	}
	/*
	 * Empty cq list
	 * Now we use a round-robin fashion to handle completed queue,
	 * later we may need to consider Intel's performance optimization
	 * algorithm (SBCS) - Software Based Completion Steering
	 */
	if (!STAILQ_EMPTY(&scu_ctlp->scu_cq)) {
		scu_cq_thread_t *thread = &scu_ctlp->scu_cq_thread_list \
		    [scu_ctlp->scu_cq_next_thread];
		scu_ctlp->scu_cq_next_thread++;
		if (scu_ctlp->scu_cq_next_thread ==
		    scu_ctlp->scu_cq_thread_num) {
			scu_ctlp->scu_cq_next_thread = 0;
		}
		cv_signal(&thread->scu_cq_thread_cv);
	}
	mutex_exit(&scu_ctlp->scu_cq_lock);
}
