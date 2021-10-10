/*
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 *
 * This file may contain confidential information of Intel Corporation
 * and should not be distributed in source form without approval
 * from Oracle Legal.
 */

/*
 * Solaris scu driver is based on the shared SCIL(Storage Controller Interface
 * Library) provided by Intel.
 */

#include <sys/scsi/adapters/scu/scu_var.h>
#include <sys/scsi/adapters/scu/scu_scsa.h>

#define	SCU_DRIVER_VERSION	"scu driver"

static	char *scu_driver_rev = SCU_DRIVER_VERSION;

/*
 * Function prototypes for driver entry points
 */
static	int scu_attach(dev_info_t *, ddi_attach_cmd_t);
static	int scu_detach(dev_info_t *, ddi_detach_cmd_t);
static	int scu_quiesce(dev_info_t *);

/*
 * FMA functions
 */
static	void scu_fm_init(scu_ctl_t *);
static	void scu_fm_fini(scu_ctl_t *);
static	int scu_fm_error_cb(dev_info_t *, ddi_fm_error_t *, const void*);

/*
 * Local functions
 */
static	int scu_iport_attach(dev_info_t *);
static	int scu_iport_detach(scu_iport_t *);
static	void scu_watchdog_handler(void *);

static	int scu_register_intrs(scu_ctl_t *);
static	int scu_alloc_intrs(scu_ctl_t *, int);
static	int scu_add_intrs(scu_ctl_t *);
static	void scu_rem_intrs(scu_ctl_t *);
static	uint_t scu_all_intr(caddr_t, caddr_t);
static	uint_t scu_complete_intr(caddr_t, caddr_t);
static	uint_t scu_error_intr(caddr_t, caddr_t);

static	void scu_scif_user_parms_get(dev_info_t *, SCIF_USER_PARAMETERS_T *);
static	void scu_scic_user_parms_get(dev_info_t *, SCIC_USER_PARAMETERS_T *);
static	int scu_bar_mapping_setup(scu_ctl_t *);
static	void scu_bar_mapping_teardown(scu_ctl_t *);
static	int scu_setup_subctls(scu_ctl_t *, int);
static	void scu_teardown_subctls(scu_ctl_t *);
static	int scu_alloc_memory_descriptor(scu_subctl_t *);
static	void scu_free_memory_descriptor(scu_subctl_t *);
static	int scu_alloc_io_request(scu_subctl_t *);
static	void scu_free_io_request(scu_subctl_t *);
static	int scu_alloc_dma_safe_memory(scu_subctl_t *, ddi_dma_attr_t,
    size_t, caddr_t *, uint64_t *, ddi_dma_handle_t *, ddi_acc_handle_t *);
static	void scu_free_dma_safe_memory(ddi_dma_handle_t *,
    ddi_acc_handle_t *);
static	void scu_stop_cq_threads(scu_ctl_t *);
static void scu_event_thread(scu_ctl_t *);
static void scu_event_thread_destroy(scu_ctl_t *);
static	int scu_resume(dev_info_t *);
static	int scu_suspend(dev_info_t *);
static	void scu_iport_offline_subdevice(scu_iport_t *);
static	void scu_offline_subiport(scu_ctl_t *);

/*
 * DMA attributes for data buffer
 */
static ddi_dma_attr_t scu_data_dma_attr = {
	DMA_ATTR_V0,		/* dma_attr_version */
	0x0ull,			/* dma_attr_addr_lo: lowest bus address */
	0xffffffffffffffffull,	/* dma_attr_addr_hi: highest bus address */
	0xffffffffull,		/* dma_attr_count_max i.e. for one cookie */
	0x4ull,			/* dma_attr_align: dword aligned */
	1,			/* dma_attr_burstsizes */
	1,			/* dma_attr_minxfer */
	0xffffffffull,		/* dma_attr_maxxfer i.e. includes all cookies */
	0xffffffffull,		/* dma_attr_seg */
	0,			/* dma_attr_sgllen */
	512,			/* dma_attr_granular */
	0,			/* dma_attr_flags */
};


/*
 * DMA attributes for memory descriptor list
 */
static ddi_dma_attr_t scu_memory_descriptor_dma_attr = {
	DMA_ATTR_V0,		/* dma_attr_version */
	0x0ull,			/* dma_attr_addr_lo: lowest bus address */
	0xffffffffull,		/* dma_attr_addr_hi: highest bus address */
	0xffffffffull,		/* dma_attr_count_max i.e. for one cookie */
	0x0ull,			/* dma_attr_align */
	1,			/* dma_attr_burstsizes */
	1,			/* dma_attr_minxfer */
	0xffffffffull,		/* dma_attr_maxxfer i.e. includes all cookies */
	0xffffffffull,		/* dma_attr_seg */
	1,			/* dma_attr_sgllen */
	1,			/* dma_attr_granular */
	0,			/* dma_attr_flags */
};

/*
 * DMA attributes for IO request
 */
static ddi_dma_attr_t scu_io_request_dma_attr = {
	DMA_ATTR_V0,		/* dma_attr_version */
	0x0ull,			/* dma_attr_addr_lo: lowest bus address */
	0xffffffffffffffffull,	/* dma_attr_addr_hi: highest bus address */
	0xffffffffull,		/* dma_attr_count_max i.e. for one cookie */
	0x4ull,			/* dma_attr_align: dword aligned */
	1,			/* dma_attr_burstsizes */
	1,			/* dma_attr_minxfer */
	0xffffffffull,		/* dma_attr_maxxfer i.e. includes all cookies */
	0xffffffffull,		/* dma_attr_seg */
	1,			/* dma_attr_sgllen */
	1,			/* dma_attr_granular */
	0,			/* dma_attr_flags */
};

static	struct dev_ops scu_dev_ops = {
	DEVO_REV,		/* devo_rev */
	0,			/* refcnt */
	ddi_no_info,		/* info */
	nulldev,		/* identify */
	nulldev,		/* probe */
	scu_attach,		/* attach */
	scu_detach,		/* detach */
	nodev,			/* reset */
	NULL,			/* driver operations */
	NULL,			/* bus operations */
	nulldev,		/* power management */
	scu_quiesce		/* quiesce */
};

extern	struct mod_ops mod_driverops;

static	struct modldrv modldrv = {
	&mod_driverops,		/* driverops */
	SCU_DRIVER_VERSION,	/* short description */
	&scu_dev_ops,		/* driver ops */
};

static	struct modlinkage modlinkage = {
	MODREV_1,
	&modldrv,
	NULL
};

static	ddi_device_acc_attr_t accattr = {
	DDI_DEVICE_ATTR_V1,
	DDI_STRUCTURE_LE_ACC,
	DDI_STRICTORDER_ACC,
	DDI_DEFAULT_ACC
};

/*
 * Global driver data
 */
void *scu_softc_state = NULL;
void *scu_iport_softstate = NULL;

/*
 * log related objects and verbosities for scu driver
 */
kmutex_t scu_log_lock;
kmutex_t scu_lib_log_lock;
char	scu_log_buf[256];
char	scu_lib_log_buf[256];
_NOTE(MUTEX_PROTECTS_DATA(scu_log_lock, scu_log_buf))
_NOTE(MUTEX_PROTECTS_DATA(scu_lib_log_lock, scu_lib_log_buf))

#ifdef	SCU_DEBUG
uint8_t scu_debug_level_mask = 0x0;
uint32_t scu_debug_object_mask = 0x0;
#else
uint8_t scu_debug_level_mask = SCUDBG_ERROR;
uint32_t scu_debug_object_mask = 0x0;
#endif


/*
 * log related objects and verbosities for SCIF and SCIC
 *
 * Note: set SCUDBG_SCIF and SCUDBG_SCIC for enabling
 */
#if 0
uint32_t scif_log_object_mask = 0;
uint8_t scif_verbosity_mask = 0;
uint32_t scic_log_object_mask = 0;
uint8_t scic_verbosity_mask = 0;
#endif

/*
 * for full objects and full level for SCIF and SCIC log
 */
uint32_t scif_log_object_mask = 0xffffffff;
uint8_t scif_verbosity_mask = 0x1f;
uint32_t scic_log_object_mask = 0xffffffff;
uint8_t scic_verbosity_mask = 0x1f;

/*
 * This value is used for damap_create, which means # of quiescent
 * microseconds before report/map is stable.
 */
int scu_iportmap_stable_usec = 2000000;	/* 2 sec */

/*
 * This value can be tuned at /etc/system, the default value is
 * the controller start time-out value multiplied by 1.5.
 */
int scu_iportmap_csync_usec = 0;

/*
 * This value is used for damap_create, which means # of quiescent
 * microseconds before report/map is stable.
 */
int scu_phymap_stable_usec = 3000000; /* 3 sec */

/*
 * Run watchdog handler every 10 seconds
 */
clock_t scu_watch_tick = 10;
static clock_t scu_watch_interval;

/*
 * SCU MSI-X interrupt is tunable
 */
boolean_t scu_msix_enabled = B_TRUE;

/*
 * times for SCIF domain discovery timeout value
 *
 * We found the suggested interval value got from SCIL interface sometimes
 * doesn't work for LSI expander, so introduce this global variable to
 * tune the interval value.
 */
int scu_domain_discover_timeout = 1;

/*
 * time interval for SCIF device discovery timeout value
 *
 * There is no default interval value in SCIL, so we add this variable
 * which is tunable at /etc/system.
 */
int scu_device_discover_timeout = 15000; /* 15 seconds */

extern int ncpus_online;

static SCIC_PASSTHRU_REQUEST_CALLBACKS_T passthru_cb = {
	scu_scic_cb_passthru_get_phy_identifier,
	scu_scic_cb_passthru_get_port_identifier,
	scu_scic_cb_passthru_get_connection_rate,
	scu_scic_cb_passthru_get_destination_sas_address,
	scu_scic_cb_passthru_get_transfer_length,
	scu_scic_cb_passthru_get_data_direction
};

SCIC_SMP_PASSTHRU_REQUEST_CALLBACKS_T smp_passthru_cb = {
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	scu_scic_cb_smp_passthru_get_request,
	scu_scic_cb_smp_passthru_get_frame_type,
	scu_scic_cb_smp_passthru_get_function,
	scu_scic_cb_smp_passthru_get_allocated_response_length
};


/*
 * scu module initialization
 */
int
_init(void)
{
	int rval;

	/* Initialize soft state for controller */
	rval = ddi_soft_state_init(&scu_softc_state, sizeof (scu_ctl_t), 0);
	if (rval != 0) {
		cmn_err(CE_WARN, "!softc state failed for scu");
		return (rval);
	}

	if ((rval = scsi_hba_init(&modlinkage)) != 0) {
		cmn_err(CE_WARN, "!scsi_hba_init failed for scu");
		ddi_soft_state_fini(&scu_softc_state);
		return (rval);
	}

	/* Initialize soft state for iport */
	rval = ddi_soft_state_init(&scu_iport_softstate,
	    sizeof (scu_iport_t), 0);
	if (rval != 0) {
		cmn_err(CE_WARN, "!iport soft state init failed for scu");
		scsi_hba_fini(&modlinkage);
		ddi_soft_state_fini(&scu_softc_state);
		return (rval);
	}

	rval = mod_install(&modlinkage);
	if (rval != 0) {
		cmn_err(CE_WARN, "!mod install failed for scu");
		ddi_soft_state_fini(&scu_iport_softstate);
		scsi_hba_fini(&modlinkage);
		ddi_soft_state_fini(&scu_softc_state);
		return (rval);
	}

	mutex_init(&scu_log_lock, NULL, MUTEX_DRIVER, NULL);
	mutex_init(&scu_lib_log_lock, NULL, MUTEX_DRIVER, NULL);

	return (0);
}

/*
 * scu module uninitialization
 */
int
_fini(void)
{
	int rval;

	rval = mod_remove(&modlinkage);
	if (rval != 0)
		return (rval);

	scsi_hba_fini(&modlinkage);
	ddi_soft_state_fini(&scu_iport_softstate);
	ddi_soft_state_fini(&scu_softc_state);
	mutex_destroy(&scu_log_lock);
	mutex_destroy(&scu_lib_log_lock);

	return (0);
}

/*
 *  _info entry point
 */
int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

static int
scu_iport_attach(dev_info_t *dip)
{
	scu_iport_t	*scu_iportp;
	scu_ctl_t	*scu_ctlp;
	int		instance, hba_instance;
	char		*iport_ua;
	char		*init_port;
	void		*ua_priv = NULL;
	scsi_hba_tran_t	*tran;
	int		attach_state = 0;

	hba_instance = ddi_get_instance(ddi_get_parent(dip));
	instance = ddi_get_instance(dip);

	scu_ctlp = ddi_get_soft_state(scu_softc_state, hba_instance);
	if (scu_ctlp == NULL) {
		cmn_err(CE_WARN, "!%s: No HBA softstate for instance %d",
		    __func__, hba_instance);
		return (DDI_FAILURE);
	}

	if ((iport_ua = scsi_hba_iport_unit_address(dip)) == NULL) {
		scu_log(scu_ctlp, CE_WARN, "!%s: invoked with NULL unit "
		    "address, instance %d", __func__, instance);
		return (DDI_FAILURE);
	}

	if (ddi_soft_state_zalloc(scu_iport_softstate, instance)
	    != DDI_SUCCESS) {
		scu_log(scu_ctlp, CE_WARN, "!failed to allocate soft state "
		    "for iport %d", instance);
		return (DDI_FAILURE);
	}

	attach_state |= SCU_IPORT_ATTACH_ALLOC_SOFT;

	scu_iportp = ddi_get_soft_state(scu_iport_softstate, instance);
	if (scu_iportp == NULL) {
		scu_log(scu_ctlp, CE_WARN, "!cannot get iport soft state");
		goto err_out;
	}

	mutex_init(&scu_iportp->scui_lock, NULL, MUTEX_DRIVER,
	    DDI_INTR_PRI(scu_ctlp->scu_intr_pri));

	attach_state |= SCU_IPORT_ATTACH_MUTEX_INIT;

	scu_iportp->scui_dip = dip;
	scu_iportp->scui_ctlp = scu_ctlp;

	/* Dup the UA into the iport struct */
	scu_iportp->scui_ua = strdup(iport_ua);

	tran = (scsi_hba_tran_t *)ddi_get_driver_private(dip);
	tran->tran_hba_private = scu_iportp;

	/* Create the list of the phys for the iport */
	list_create(&scu_iportp->scui_phys, sizeof (scu_phy_t),
	    offsetof(scu_phy_t, list_node));

	attach_state |= SCU_IPORT_ATTACH_PHY_LIST_CREATE;

	/*
	 * If the unit address is active in the phymap, configure the
	 * iport's phylist.
	 */
	mutex_enter(&scu_iportp->scui_lock);
	ua_priv = sas_phymap_lookup_uapriv(scu_ctlp->scu_phymap,
	    scu_iportp->scui_ua);
	if (ua_priv) {
		/* Non-NULL private data indicates the unit address is active */
		scu_iportp->scui_ua_state = SCU_UA_ACTIVE;
		if (scu_iport_configure_phys(scu_iportp) != SCU_SUCCESS) {
			scu_log(scu_ctlp, CE_WARN, "!%s: failed to configure "
			    "phys for iport  handle 0x%p, unit address [%s]",
			    __func__, (void *)scu_iportp, scu_iportp->scui_ua);
			mutex_exit(&scu_iportp->scui_lock);
			goto err_out;
		}
	} else {
		/*
		 * If the private data is NULL, the sasphy is not ready.
		 * We should exit the iport attach process.
		 */
		SCUDBG(scu_ctlp, SCUDBG_IPORT|SCUDBG_INIT, SCUDBG_INFO,
		    "%s: ua private date is NULL", __func__);
		mutex_exit(&scu_iportp->scui_lock);
		goto err_out;

	}
	mutex_exit(&scu_iportp->scui_lock);

	/* Allocate string-based soft state pool for targets */
	scu_iportp->scui_tgt_sstate = NULL;
	if (ddi_soft_state_bystr_init(&scu_iportp->scui_tgt_sstate,
	    sizeof (scu_tgt_t), SCU_TGT_SSTATE_SIZE) != 0) {
		scu_log(scu_ctlp, CE_WARN, "!cannot init iport tgt soft state");
		goto err_out;
	}

	attach_state |= SCU_IPORT_ATTACH_TGT_STATE_ALLOC;

	/* Create this iport's target map */
	if (scu_iport_tgtmap_create(scu_iportp) != SCU_SUCCESS) {
		scu_log(scu_ctlp, CE_WARN, "!failed to create tgtmap for "
		    "iport %d", instance);
		goto err_out;
	}

	/* Set up the 'initiator-port' DDI property on this iport */
	init_port = kmem_zalloc(SCU_MAX_UA_SIZE, KM_SLEEP);

	/* Set initiator-port value to the HBA's base WWN */
	(void) scsi_wwn_to_wwnstr(scu_ctlp->sas_wwns[0], 1,
	    init_port);


	/* Insert the iport to hba struct */
	mutex_enter(&scu_ctlp->scu_iport_lock);
	list_insert_tail(&scu_ctlp->scu_iports, scu_iportp);
	scu_ctlp->scu_iport_num++;

	mutex_exit(&scu_ctlp->scu_iport_lock);

	/* add the smhba props for this iport */
	mutex_enter(&scu_iportp->scui_lock);
	scu_smhba_add_iport_prop(scu_iportp, DATA_TYPE_STRING,
	    SCSI_ADDR_PROP_INITIATOR_PORT, init_port);
	kmem_free(init_port, SCU_MAX_UA_SIZE);
	scu_smhba_add_iport_prop(scu_iportp, DATA_TYPE_INT32, SCU_NUM_PHYS,
	    &scu_iportp->scui_phy_num);
	mutex_exit(&scu_iportp->scui_lock);

	/* create the phy stats for this iport */
	scu_smhba_create_all_phy_stats(scu_iportp);
	ddi_report_dev(dip);

	SCUDBG(scu_ctlp, SCUDBG_IPORT|SCUDBG_INIT, SCUDBG_TRACE,
	    "%s: exit successfully", __func__);

	return (DDI_SUCCESS);

err_out:
	if (attach_state & SCU_IPORT_ATTACH_TGT_STATE_ALLOC) {
		ddi_soft_state_bystr_fini(&scu_iportp->scui_tgt_sstate);
	}

	if (attach_state & SCU_IPORT_ATTACH_PHY_LIST_CREATE) {
		list_destroy(&scu_iportp->scui_phys);
		strfree(scu_iportp->scui_ua);
	}

	if (attach_state & SCU_IPORT_ATTACH_MUTEX_INIT) {
		mutex_destroy(&scu_iportp->scui_lock);
	}

	if (attach_state & SCU_IPORT_ATTACH_ALLOC_SOFT) {
		ddi_soft_state_free(scu_iport_softstate, instance);
	}

	SCUDBG(scu_ctlp, SCUDBG_IPORT|SCUDBG_INIT, SCUDBG_TRACE,
	    "%s: exit with failure value", __func__);

	return (DDI_FAILURE);
}

static void
scu_scif_user_parms_get(dev_info_t *dip, SCIF_USER_PARAMETERS_T *scif_parms)
{
	scif_parms->sas.is_sata_ncq_enabled = ddi_prop_get_int(
	    DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS | DDI_PROP_NOTPROM,
	    "is-sata-ncq-enabled", 1);
	scif_parms->sas.is_sata_standby_timer_enabled = ddi_prop_get_int(
	    DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS | DDI_PROP_NOTPROM,
	    "is-sata-standby-timer-enabled", 0);
	scif_parms->sas.is_non_zero_buffer_offsets_enabled = ddi_prop_get_int(
	    DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS | DDI_PROP_NOTPROM,
	    "is-non-zero-buffer-offsets-enabled", 0);
	scif_parms->sas.clear_affiliation_during_controller_stop =
	    ddi_prop_get_int(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS | DDI_PROP_NOTPROM,
	    "clear-affiliation-during-controller-stop", 1);
	scif_parms->sas.max_ncq_depth = ddi_prop_get_int(
	    DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS | DDI_PROP_NOTPROM,
	    "max-ncq-depth", 32);
	scif_parms->sas.reset_type = ddi_prop_get_int(
	    DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS | DDI_PROP_NOTPROM,
	    "reset-type", SCI_SAS_LOGICAL_UNIT_RESET);
}

static void
scu_scic_user_parms_get(dev_info_t *dip,
    SCIC_USER_PARAMETERS_T *scic_user_parms)
{
	int	index;

	for (index = 0; index < SCI_MAX_PHYS; index++) {
		scic_user_parms->sds1.phys[index].	\
		    notify_enable_spin_up_insertion_frequency =
		    ddi_prop_get_int(DDI_DEV_T_ANY, dip,
		    DDI_PROP_DONTPASS | DDI_PROP_NOTPROM,
		    "notify-enable-spin-up-insertion-frequency", 0x33);
		scic_user_parms->sds1.phys[index].align_insertion_frequency =
		    ddi_prop_get_int(DDI_DEV_T_ANY, dip,
		    DDI_PROP_DONTPASS | DDI_PROP_NOTPROM,
		    "align-insertion-frequency", 0x7f);
		scic_user_parms->sds1.phys[index].	\
		    in_connection_align_insertion_frequency =
		    ddi_prop_get_int(DDI_DEV_T_ANY, dip,
		    DDI_PROP_DONTPASS | DDI_PROP_NOTPROM,
		    "in-connection-align-insertion-frequency", 0xff);
		scic_user_parms->sds1.phys[index].max_speed_generation =
		    ddi_prop_get_int(DDI_DEV_T_ANY, dip,
		    DDI_PROP_DONTPASS | DDI_PROP_NOTPROM,
		    "max-speed-generation", 3);
	}

	scic_user_parms->sds1.stp_inactivity_timeout = ddi_prop_get_int(
	    DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS | DDI_PROP_NOTPROM,
	    "stp-inactivity-timeout", 5);
	scic_user_parms->sds1.ssp_inactivity_timeout = ddi_prop_get_int(
	    DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS | DDI_PROP_NOTPROM,
	    "ssp-inactivity-timeout", 5);
	scic_user_parms->sds1.stp_max_occupancy_timeout = ddi_prop_get_int(
	    DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS | DDI_PROP_NOTPROM,
	    "stp-max-occupancy-timeout", 5);
	scic_user_parms->sds1.ssp_max_occupancy_timeout = ddi_prop_get_int(
	    DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS | DDI_PROP_NOTPROM,
	    "ssp-max-occupancy-timeout", 20);
	scic_user_parms->sds1.no_outbound_task_timeout = ddi_prop_get_int(
	    DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS | DDI_PROP_NOTPROM,
	    "no-outbound-task-timeout", 20);
}

/*
 *  attach entry point for dev_ops
 */
static int
scu_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	scu_ctl_t			*scu_ctlp;
	int				instance, i, j;
	int				attach_state = 0;
	int				sm_hba = 1;
	int				num_phys = 0;
	int				protocol = 0;
	uint32_t			non_dma_needed;
	struct sci_pci_common_header	pci_header;
	int				ctl_count;
	scu_phy_t			*scu_phyp;
	uint32_t			num_threads;
	uint32_t			build, major, minor;
	uint32_t			version, revision;
	char				taskq_name[64] = "";
	char				hw_ver[24], *hw_rev;
	SCIC_SDS_LIBRARY_T		*sds_lib;
	SCI_CONTROLLER_HANDLE_T		scic_ctlp;
	SCIC_SDS_PHY_T			*scic_phyp;
	SCI_SAS_ADDRESS_T		local_phy_sas_address;
	uint64_t			local_sas_address;
	int				adapter_started;

	switch (cmd) {
	case DDI_ATTACH:
		break;

	case DDI_RESUME:
		return (scu_resume(dip));

	default:
		return (DDI_FAILURE);
	}

	/*
	 * If this is an iport node, invoke iport attach.
	 */
	if (scsi_hba_iport_unit_address(dip) != NULL) {
		return (scu_iport_attach(dip));
	}

	/*
	 * From here is attach for the HBA node
	 */
	instance = ddi_get_instance(dip);

	/*
	 * Allocate softc information.
	 */
	if (ddi_soft_state_zalloc(scu_softc_state, instance) != DDI_SUCCESS) {
		cmn_err(CE_WARN, "!scu%d: cannot allocate soft state",
		    instance);
		goto err_out;
	}

	attach_state |= SCU_ATTACHSTATE_CSOFT_ALLOC;

	scu_ctlp = ddi_get_soft_state(scu_softc_state, instance);
	if (scu_ctlp == NULL) {
		cmn_err(CE_WARN, "!scu%d: cannot get soft state", instance);
		goto err_out;
	}

	scu_ctlp->scu_dip = dip;
	scu_ctlp->scu_instance = instance;

	/*
	 * Initialize FMA
	 */
	scu_ctlp->reg_acc_attr = accattr;
	scu_fm_init(scu_ctlp);
	attach_state |= SCU_ATTACHSTATE_FMA_SUPPORT;

	/*
	 * Set up PCI config space
	 */
	if (pci_config_setup(dip, &scu_ctlp->scu_pcicfg_handle)
	    != DDI_SUCCESS) {
		scu_log(scu_ctlp, CE_WARN,
		    "!failed to set up pci config space");
		goto err_out;
	}

	attach_state |= SCU_ATTACHSTATE_PCICFG_SETUP;

	/*
	 * Set up mapping address for BAR
	 */
	if (scu_bar_mapping_setup(scu_ctlp) != DDI_SUCCESS) {
		scu_log(scu_ctlp, CE_WARN,
		    "!failed to set up mapping space for register bar");
		goto err_out;
	}

	attach_state |= SCU_ATTACHSTATE_BAR_MAPPING_SETUP;

	scu_ctlp->scu_lib_max_ctl_num = SCI_MAX_CONTROLLERS;

	/*
	 * Calculate Non DMA Safe memory needed
	 */
	non_dma_needed =
	    scif_library_get_object_size(scu_ctlp->scu_lib_max_ctl_num);

	scu_ctlp->scu_lib_non_dma_needed = non_dma_needed;

	/*
	 * Allocate Non DMA Safe memory
	 */
	scu_ctlp->scu_lib_non_dma_memory = kmem_zalloc(
	    non_dma_needed, KM_SLEEP);

	attach_state |= SCU_ATTACHSTATE_LIB_NONDMA_ALLOC;

	/*
	 * Now construct the SCI library
	 */
	scu_ctlp->scu_scif_lib_handle = scif_library_construct(
	    scu_ctlp->scu_lib_non_dma_memory, scu_ctlp->scu_lib_max_ctl_num);

	if (scu_ctlp->scu_scif_lib_handle == SCI_INVALID_HANDLE) {
		cmn_err(CE_WARN, "!scu: cannot construct SCI library");
		goto err_out;
	}

	/*
	 * Set the association between SCIF library and scu_ctlp
	 */
	(void) sci_object_set_association(scu_ctlp->scu_scif_lib_handle,
	    (void *)scu_ctlp);

	scu_ctlp->scu_scic_lib_handle = scif_library_get_scic_handle(
	    scu_ctlp->scu_scif_lib_handle);

	/*
	 * Now enabling the SCIL logging - according to SCIL developer guide,
	 * framework and core have completely independent log objects and
	 * verbosities.
	 */
	sci_logger_enable(sci_object_get_logger(scu_ctlp->scu_scif_lib_handle),
	    scif_log_object_mask, scif_verbosity_mask);

	sci_logger_enable(sci_object_get_logger(scu_ctlp->scu_scic_lib_handle),
	    scic_log_object_mask, scic_verbosity_mask);

	/*
	 * Get SCIL version
	 */
	major = sci_library_get_major_version();
	minor = sci_library_get_minor_version();
	build = sci_library_get_build_version();

	scu_log(NULL, CE_NOTE, "!SCIL version %d.%d.%d is used",
	    major, minor, build);

	/*
	 * Get SATI compliant version - SCIL not supported yet
	 */
	version = scif_controller_get_sat_compliance_version();
	revision =
	    scif_controller_get_sat_compliance_version_revision();

	if (version != 0 || revision != 0)
		scu_log(NULL, CE_NOTE, "!SAT compliance version %d.%d",
		    version, revision);

	/*
	 * Initialize cb pointer for smp pass through
	 */
	smp_passthru_cb.common_callbacks = passthru_cb;

	/*
	 * Now construct sci_pci_common_header
	 */
	pci_header.vendor_id = pci_config_get16(scu_ctlp->scu_pcicfg_handle,
	    PCI_CONF_VENID);
	pci_header.device_id = pci_config_get16(scu_ctlp->scu_pcicfg_handle,
	    PCI_CONF_DEVID);
	pci_header.command = pci_config_get16(scu_ctlp->scu_pcicfg_handle,
	    PCI_CONF_COMM);
	pci_header.status = pci_config_get16(scu_ctlp->scu_pcicfg_handle,
	    PCI_CONF_STAT);
	pci_header.revision = pci_config_get8(scu_ctlp->scu_pcicfg_handle,
	    PCI_CONF_REVID);
	pci_header.program_interface = pci_config_get8(
	    scu_ctlp->scu_pcicfg_handle, PCI_CONF_PROGCLASS);
	pci_header.sub_class = pci_config_get8(scu_ctlp->scu_pcicfg_handle,
	    PCI_CONF_SUBCLASS);
	pci_header.base_class = pci_config_get8(scu_ctlp->scu_pcicfg_handle,
	    PCI_CONF_BASCLASS);

	/*
	 * Set pci info to SCIC
	 */
	scic_library_set_pci_info(scu_ctlp->scu_scic_lib_handle, &pci_header);

	/*
	 * Get the controller count for this device
	 */
	ctl_count = scic_library_get_pci_device_controller_count(
	    scu_ctlp->scu_scic_lib_handle);
	scu_ctlp->scu_lib_ctl_num = ctl_count;

	scu_ctlp->scu_event_ctls = kmem_zalloc((ctl_count + 1) *
	    sizeof (scu_event_t), KM_SLEEP);
	for (i = 0; i < ctl_count + 1; i++)
		scu_ctlp->scu_event_ctls[i].ev_index = i;

	/*
	 * Now allocate sub-controllers
	 */
	scu_ctlp->scu_subctls = kmem_zalloc(ctl_count *
	    sizeof (scu_subctl_t), KM_SLEEP);

	attach_state |= SCU_ATTACHSTATE_SUBCTL_ALLOC;

	/*
	 * Set up every sub-controller
	 */
	if (scu_setup_subctls(scu_ctlp, ctl_count) != SCU_SUCCESS) {
		scu_log(scu_ctlp, CE_WARN, "!cannot setup sub controllers");
		goto err_out;
	}

	attach_state |= SCU_ATTACHSTATE_SUBCTL_SETUP;

	/*
	 * Get SCIF USER PARAMETERS from scu.conf
	 */
	scu_scif_user_parms_get(dip, &scu_ctlp->scu_scif_user_parms);

	/*
	 * Now call scif_user_parameters_set to override default values
	 * utilized by SCI framework, for example, enable/disable ncq and
	 * queue depth. Users can change the values via scu.conf.
	 */
	for (i = 0; i < ctl_count; i++) {
		(void) scif_user_parameters_set(
		    scu_ctlp->scu_subctls[i].scus_scif_ctl_handle,
		    &scu_ctlp->scu_scif_user_parms);
	}

	/*
	 * Get SCIC USER PARAMETERS from scu.conf
	 */
	scu_scic_user_parms_get(dip, &scu_ctlp->scu_scic_user_parms);

	/*
	 * Here	scic_user_parameters_set can be called to override default
	 * values used by SCI core. For example, set max speed generation
	 * for phy.
	 */
	for (i = 0; i < ctl_count; i++) {
		(void) scic_user_parameters_set(
		    scu_ctlp->scu_subctls[i].scus_scic_ctl_handle,
		    &scu_ctlp->scu_scic_user_parms);
	}

	/*
	 * Initialize the SCIF controller
	 */
	for (i = 0; i < ctl_count; i++) {
		if (scif_controller_initialize(
		    scu_ctlp->scu_subctls[i].scus_scif_ctl_handle) !=
		    SCI_SUCCESS) {
			scu_log(scu_ctlp, CE_WARN,
			    "!cannot initialize SCIF controller index %d", i);
			goto err_out;
		}
	}

	/*
	 * Allocate DMA Safe memory for memory descriptor list,
	 * this should be done after sci_controller_initialize()
	 */
	for (i = 0; i < ctl_count; i++) {
		if (scu_alloc_memory_descriptor(&(scu_ctlp->scu_subctls[i]))
		    != SCU_SUCCESS) {
			scu_log(scu_ctlp, CE_WARN, "!failed to allocate DMA "
			    "safe memory for SCIL memory descriptors for "
			    "controller %d", i);
			for (i--; i >= 0; i--) {
				scu_free_memory_descriptor(
				    &(scu_ctlp->scu_subctls[i]));
			}
			goto err_out;
		}
	}

	attach_state |= SCU_ATTACHSTATE_LIB_MEMORY_DESCRIPTOR_ALLOC;

	/*
	 * Allocate IO slot resources
	 */
	for (i = 0; i < ctl_count; i++) {
		scu_ctlp->scu_subctls[i].scus_slot_num = SCI_MAX_IO_REQUESTS;
		scu_ctlp->scu_subctls[i].scus_io_slots =
		    kmem_zalloc(SCI_MAX_IO_REQUESTS * sizeof (scu_io_slot_t),
		    KM_SLEEP);
	}

	attach_state |= SCU_ATTACHSTATE_IO_SLOT_ALLOC;

	/*
	 * Allocate DMA Safe memory for per-IO request
	 */
	for (i = 0; i < ctl_count; i++) {
		if (scu_alloc_io_request(&(scu_ctlp->scu_subctls[i]))
		    != SCU_SUCCESS) {
			scu_log(scu_ctlp, CE_WARN, "!failed to allocate DMA "
			    "safe memory for IO request for controller %d", i);
			for (i--; i >= 0; i--) {
				scu_free_io_request(
				    &(scu_ctlp->scu_subctls[i]));
			}
			goto err_out;
		}
	}

	attach_state |= SCU_ATTACHSTATE_IO_REQUEST_ALLOC;

	/*
	 * Disable the controller interrupt before adding interrupt handlers
	 */
	for (i = 0; i < ctl_count; i++) {
		scic_controller_disable_interrupts(
		    scu_ctlp->scu_subctls[i].scus_scic_ctl_handle);
	}

	if (scu_register_intrs(scu_ctlp) != SCU_SUCCESS) {
		scu_log(scu_ctlp, CE_WARN, "!interrupt registration failed");
		goto err_out;
	}

	attach_state |= SCU_ATTACHSTATE_INTR_ADDED;

	/*
	 * Initialize the controller mutex
	 */
	mutex_init(&scu_ctlp->scu_lock, NULL, MUTEX_DRIVER,
	    DDI_INTR_PRI(scu_ctlp->scu_intr_pri));

	cv_init(&scu_ctlp->scu_cv, NULL, CV_DRIVER, NULL);

	for (i = 0; i < ctl_count; i++) {
		/*
		 * Initialize the mutex used for SCIL internal hprq
		 * (high priority request queue)
		 */
		mutex_init(&scu_ctlp->scu_subctls[i].scus_lib_hprq_lock,
		    NULL, MUTEX_DRIVER, DDI_INTR_PRI(scu_ctlp->scu_intr_pri));
		/*
		 * Initialize the mutex used for SCIL remote device
		 * construction/destruction
		 */
		mutex_init(&scu_ctlp->scu_subctls[i]. \
		    scus_lib_remote_device_lock, NULL, MUTEX_DRIVER,
		    DDI_INTR_PRI(scu_ctlp->scu_intr_pri));

		/*
		 * Initialize the mutex for command slot
		 */
		mutex_init(&scu_ctlp->scu_subctls[i].scus_slot_lock, NULL,
		    MUTEX_DRIVER, DDI_INTR_PRI(scu_ctlp->scu_intr_pri));

		cv_init(&scu_ctlp->scu_subctls[i]. \
		    scus_reset_cv, NULL, CV_DRIVER, NULL);
		cv_init(&scu_ctlp->scu_subctls[i]. \
		    scus_reset_complete_cv, NULL, CV_DRIVER, NULL);
	}

	/*
	 * Create the list and mutex for iport node
	 */
	list_create(&scu_ctlp->scu_iports, sizeof (scu_iport_t),
	    offsetof(scu_iport_t, list_node));

	mutex_init(&scu_ctlp->scu_iport_lock, NULL, MUTEX_DRIVER,
	    DDI_INTR_PRI(scu_ctlp->scu_intr_pri));

	/*
	 * Initialize the mutex for completed command queue
	 */
	mutex_init(&scu_ctlp->scu_cq_lock, NULL, MUTEX_DRIVER,
	    DDI_INTR_PRI(scu_ctlp->scu_intr_pri));

	cv_init(&scu_ctlp->scu_cmd_complete_cv, NULL, CV_DRIVER, NULL);

	mutex_init(&scu_ctlp->scu_event_lock, NULL, MUTEX_DRIVER,
	    DDI_INTR_PRI(scu_ctlp->scu_intr_pri));
	cv_init(&scu_ctlp->scu_event_disp_cv, NULL, CV_DRIVER, NULL);
	cv_init(&scu_ctlp->scu_event_quit_cv, NULL, CV_DRIVER, NULL);

	attach_state |= SCU_ATTACHSTATE_MUTEX_INIT;

	/*
	 * Allocate root phys for the controller
	 */
	scu_ctlp->scu_root_phy_num = SCI_MAX_PHYS * ctl_count;
	scu_ctlp->scu_root_phys = kmem_zalloc(scu_ctlp->scu_root_phy_num
	    * sizeof (scu_phy_t), KM_SLEEP);
	scu_phyp = scu_ctlp->scu_root_phys;

	/*
	 * Do the appopriate initialization operation for phys
	 */
	for (i = 0; i < scu_ctlp->scu_root_phy_num; i++) {
		mutex_init(&scu_phyp->scup_lock, NULL, MUTEX_DRIVER,
		    DDI_INTR_PRI(scu_ctlp->scu_intr_pri));
		scu_phyp->scup_hba_index = i & SAS2_PHYNUM_MASK;
		scu_phyp->scup_lib_index = i % SCI_MAX_PHYS;
		scu_phyp->scup_ctlp = scu_ctlp;
		scu_phyp->scup_subctlp =
		    &(scu_ctlp->scu_subctls[i / SCI_MAX_PHYS]);
		scu_phyp++;
	}

	attach_state |= SCU_ATTACHSTATE_PHY_ALLOC;

	/*
	 * Allocate targets for the controller
	 */
	scu_ctlp->scu_max_dev = SCI_MAX_REMOTE_DEVICES * ctl_count;
	scu_ctlp->scu_tgts = (scu_tgt_t **)kmem_zalloc(
	    scu_ctlp->scu_max_dev * sizeof (scu_tgt_t *), KM_SLEEP);

	scu_ctlp->scu_event_tgts = kmem_zalloc(
	    scu_ctlp->scu_max_dev * sizeof (scu_event_t), KM_SLEEP);
	for (i = 0; i < scu_ctlp->scu_max_dev; i++)
		scu_ctlp->scu_event_tgts[i].ev_index = i;

	attach_state |= SCU_ATTACHSTATE_TGT_ALLOC;

	/*
	 * Initialize the completed command and event queue
	 */
	STAILQ_INIT(&scu_ctlp->scu_cq);
	STAILQ_INIT(&scu_ctlp->scu_eventq);

	/*
	 * Allocate threads for handling completed commands queue
	 */
	num_threads = ncpus_online;
	if (num_threads > SCU_MAX_CQ_THREADS) {
		SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_IO, SCUDBG_INFO,
		    "maximum threads for cq is %d", SCU_MAX_CQ_THREADS);
		num_threads = SCU_MAX_CQ_THREADS;
	}

	scu_ctlp->scu_cq_thread_num = num_threads;
	scu_ctlp->scu_cq_thread_list = kmem_zalloc(
	    sizeof (scu_cq_thread_t) * scu_ctlp->scu_cq_thread_num, KM_SLEEP);
	scu_ctlp->scu_cq_next_thread = 0;
	scu_ctlp->scu_cq_stop = 0;

	for (i = 0; i < scu_ctlp->scu_cq_thread_num; i++) {
		kthread_t	*thr;
		cv_init(&scu_ctlp->scu_cq_thread_list[i].scu_cq_thread_cv,
		    NULL, CV_DRIVER, NULL);

		scu_ctlp->scu_cq_thread_list[i].scu_cq_ctlp = scu_ctlp;
		thr = thread_create(NULL, 0, scu_cq_handler,
		    &(scu_ctlp->scu_cq_thread_list[i]), 0, &p0,
		    TS_RUN, minclsyspri);
		scu_ctlp->scu_cq_thread_list[i].scu_cq_thread =  thr;
		scu_ctlp->scu_cq_thread_list[i].scu_cq_thread_id = thr->t_did;
	}

	attach_state |= SCU_ATTACHSTATE_CQ_ALLOC;

	/*
	 * Create taskq to handle discovery
	 */
	if ((scu_ctlp->scu_discover_taskq = ddi_taskq_create(dip,
	    "discovery_taskq", scu_ctlp->scu_root_phy_num,
	    TASKQ_DEFAULTPRI, 0)) == NULL) {
		scu_log(scu_ctlp, CE_WARN,
		    "!failed to create taskq for discovery");
		goto err_out;
	}

	attach_state |= SCU_ATTACHSTATE_DISCOVER_TASKQ;

	/*
	 * Create thread to handle event
	 */
	if ((scu_ctlp->scu_event_thread = thread_create(NULL, 0,
	    scu_event_thread, scu_ctlp, 0, &p0, TS_RUN, minclsyspri)) == NULL) {
		scu_log(scu_ctlp, CE_WARN,
		    "!failed to create event thread");
		goto err_out;
	}

	attach_state |= SCU_ATTACHSTATE_EVENT_THREAD;

	/*
	 * Create taskq for SCIL to handle internal task
	 */
	for (i = 0; i < ctl_count; i++) {
		(void) sprintf(taskq_name, "lib_internal_taskq_subctl%d", i);
		if ((scu_ctlp->scu_subctls[i].scus_lib_internal_taskq =
		    ddi_taskq_create(dip, taskq_name, SCI_MAX_PHYS,
		    TASKQ_DEFAULTPRI, 0)) == NULL) {
			scu_log(scu_ctlp, CE_WARN,
			    "!failed to create taskq for SCIL internal task "
			    "handling for sub-controller %d", i);
			for (i--; i >= 0; i--) {
				ddi_taskq_destroy(scu_ctlp->scu_subctls[i]. \
				    scus_lib_internal_taskq);
			}
			goto err_out;
		}
	}

	attach_state |= SCU_ATTACHSTATE_LIB_INTERNAL_TASKQ;

	/*
	 * Set the controller's phy local address array
	 */
	for (i = 0; i < ctl_count; i++) {
		scic_ctlp = scu_ctlp->scu_subctls[i].scus_scic_ctl_handle;
		for (j = 0; j < SCI_MAX_PHYS; j++) {
			if (scic_controller_get_phy_handle(scic_ctlp, j,
			    (SCI_PHY_HANDLE_T *)&scic_phyp) != SCI_SUCCESS) {
				SCUDBG(scu_ctlp, SCUDBG_HBA, SCUDBG_ERROR,
				    "%s: cannot get phy handle for phy %d",
				    __func__, j);
				continue;
			}

			/* Get local phy sas address and add to phy map */
			scic_sds_phy_get_sas_address(scic_phyp,
			    &local_phy_sas_address);
			local_sas_address = SCU_SAS_ADDRESS(
			    BE_32(local_phy_sas_address.high),
			    BE_32(local_phy_sas_address.low));
			scu_ctlp->sas_wwns[i * SCI_MAX_PHYS + j]
			    = local_sas_address;
			scu_phyp = scu_ctlp->scu_root_phys +
			    i * SCI_MAX_PHYS + j;

			ASSERT(scu_phyp != NULL);
			scu_phyp->scup_sas_address = local_sas_address;
		}
	}

	/*
	 * Get DMA attributes for data buffer
	 */

	/* maximum size an individual SGL element can address */
	scu_data_dma_attr.dma_attr_count_max = scic_library_get_max_sge_size(
	    scu_ctlp->scu_scic_lib_handle);

	/* maximum number of SGL elements for a single IO request */
	scu_data_dma_attr.dma_attr_sgllen = scic_library_get_max_sge_count(
	    scu_ctlp->scu_scic_lib_handle);

	/* maximum length for any IO request the controllers can handle */
	scu_data_dma_attr.dma_attr_maxxfer = scic_library_get_max_io_length(
	    scu_ctlp->scu_scic_lib_handle);

	scu_data_dma_attr.dma_attr_seg = scu_data_dma_attr.dma_attr_maxxfer;

	/* SCU hardware needs data buffer must be alignment */
	scu_data_dma_attr.dma_attr_flags |= DDI_DMA_ALIGNMENT_REQUIRED;

	scu_ctlp->scu_data_dma_attr = scu_data_dma_attr;

	/*
	 * Do the SCSI attachment code
	 */
	if (scu_scsa_setup(scu_ctlp) != DDI_SUCCESS) {
		scu_log(scu_ctlp, CE_WARN, "!failed to setup scsa attachment");
		goto err_out;
	}

	attach_state |= SCU_ATTACHSTATE_SCSI_ATTACH;

	/*
	 * Create the iportmap for this HBA instance
	 */

	/*
	 * Here we must be careful about the value of csync_usec.
	 *
	 * csync_usec value is selected based on how long it takes the HBA
	 * driver to get from map creation to initial observation for
	 * something already plugged in. Must estimate high, a low estimate
	 * can result in devices not showing up correctly on first reference.
	 *
	 * Therefore, we assign the value to the controller start timeout
	 * multiplied by 1.5.
	 */
	if (scu_iportmap_csync_usec == 0)
		scu_iportmap_csync_usec = (int) \
		    1.5 * 1000 * scif_controller_get_suggested_start_timeout(
		    scu_ctlp->scu_subctls[0].scus_scif_ctl_handle);
	if (scsi_hba_iportmap_create(dip, scu_iportmap_csync_usec,
	    scu_iportmap_stable_usec, &scu_ctlp->scu_iportmap) != DDI_SUCCESS) {
		scu_log(scu_ctlp, CE_WARN, "!failed to create iportmap");
		goto err_out;
	}
	ASSERT(scu_ctlp->scu_iportmap != NULL);

	attach_state |= SCU_ATTACHSTATE_IPORTMAP_CREATE;

	/*
	 * Create the phymap for this HBA instance
	 */
	if (sas_phymap_create(dip, scu_phymap_stable_usec, PHYMAP_MODE_SIMPLE,
	    NULL, scu_ctlp, scu_phymap_activate, scu_phymap_deactivate,
	    &scu_ctlp->scu_phymap) != DDI_SUCCESS) {
		scu_log(scu_ctlp, CE_WARN, "!failed to create phymap");
		goto err_out;
	}
	ASSERT(scu_ctlp->scu_phymap != NULL);

	attach_state |= SCU_ATTACHSTATE_PHYMAP_CREATE;

	/*
	 * Now start the SCIF controller - please note that the real
	 * completion status will be returned by SCIL callback -
	 * scif_cb_controller_start_complete.
	 */
	for (i = 0; i < ctl_count; i++) {
		if (scif_controller_start(
		    scu_ctlp->scu_subctls[i].scus_scif_ctl_handle,
		    scif_controller_get_suggested_start_timeout(
		    scu_ctlp->scu_subctls[i].scus_scif_ctl_handle))
		    != SCI_SUCCESS) {
			scu_log(scu_ctlp, CE_WARN,
			    "!failed to start SCIF controller for "
			    "sub-controller %d", i);
			for (i--; i >= 0; i--) {
				(void) scif_controller_stop(
				    scu_ctlp->scu_subctls[i]. \
				    scus_scif_ctl_handle,
				    scif_controller_get_suggested_start_timeout(
				    scu_ctlp->scu_subctls[i]. \
				    scus_scif_ctl_handle));
			}
			goto err_out;
		}
	}

	/* Cannot use busy wait */
	do {
		(void) scu_poll_intr(scu_ctlp);
		delay(drv_usectohz(1000)); /* 1 millisecond */

		adapter_started = 1;
		for (i = 0; i < ctl_count; i++) {
			if (scu_ctlp->
			    scu_subctls[i].scus_adapter_is_ready == 0)
			adapter_started = 0;
		}
	} while (adapter_started == 0);

	/* Now the controller(s) are started */
	scu_ctlp->scu_started = 1;

	attach_state |= SCU_ATTACHSTATE_CTL_STARTED;

	/*
	 * Enable the interrupt before topology discovery
	 */
	for (i = 0; i < ctl_count; i++) {
		scic_controller_enable_interrupts(
		    scu_ctlp->scu_subctls[i].scus_scic_ctl_handle);
	}

	attach_state |= SCU_ATTACHSTATE_INTR_ENABLED;

	/*
	 * Kick off watchdog handler
	 */
	scu_watch_interval = scu_watch_tick * drv_usectohz((clock_t)1000000);
	scu_ctlp->scu_watchdog_timeid = timeout(scu_watchdog_handler,
	    scu_ctlp, scu_watch_interval);

	attach_state |= SCU_ATTACHSTATE_WATCHDOG_HANDLER;

	/* Print message of HBA present */
	ddi_report_dev(dip);

	/* SM-HBA */
	scu_smhba_add_hba_prop(scu_ctlp, DATA_TYPE_INT32, SCU_SMHBA_SUPPORTED,
	    &sm_hba);

	/* SM-HBA */
	scu_smhba_add_hba_prop(scu_ctlp, DATA_TYPE_STRING, SCU_DRV_VERSION,
	    scu_driver_rev);

	/* SM-HBA */
	sds_lib = (SCIC_SDS_LIBRARY_T *)scu_ctlp->scu_scic_lib_handle;
	switch (sds_lib->pci_revision) {
	case SCIC_SDS_PCI_REVISION_A0:
		hw_rev = "A0";
		break;
	case SCIC_SDS_PCI_REVISION_A2:
		hw_rev = "A2";
		break;
	case SCIC_SDS_PCI_REVISION_B0:
		hw_rev = "B0";
		break;
	default:
		hw_rev = "  ";
		break;
	}
	(void) snprintf(hw_ver, sizeof (pci_header.device_id) + sizeof (hw_rev),
	    "%x_%s", pci_header.device_id, hw_rev);

	scu_smhba_add_hba_prop(scu_ctlp, DATA_TYPE_STRING, SCU_HWARE_VERSION,
	    hw_ver);

	/* SM-HBA */
	scu_smhba_add_hba_prop(scu_ctlp, DATA_TYPE_STRING, SCU_FWARE_VERSION,
	    "NONE");

	/* SM-HBA */
	num_phys = scu_ctlp->scu_root_phy_num;
	scu_smhba_add_hba_prop(scu_ctlp, DATA_TYPE_INT32, SCU_NUM_PHYS_HBA,
	    &num_phys);

	/* SM-HBA */
	protocol = SAS_SSP_SUPPORT | SAS_SATA_SUPPORT | SAS_SMP_SUPPORT;
	scu_smhba_add_hba_prop(scu_ctlp, DATA_TYPE_INT32,
	    SCU_SUPPORTED_PROTOCOL, &protocol);

	/* SM-HBA */
	scu_smhba_add_hba_prop(scu_ctlp, DATA_TYPE_STRING, SCU_MANUFACTURER,
	    "Intel Corporation");

	/* SM-HBA */
	scu_smhba_add_hba_prop(scu_ctlp, DATA_TYPE_STRING, SCU_MODEL_NAME,
	    "Storage Controller Unit");

	/* FMA check all handles */
	if (scu_check_all_handle(scu_ctlp) != DDI_SUCCESS) {
		SCUDBG(scu_ctlp, SCUDBG_FMA, SCUDBG_ERROR|SCUDBG_TRACE,
		    "%s: failed to check controller dma/acc handle",
		    __func__);
		goto err_out;
	}

	/* Receptacle properties (FMA) */
	scu_ctlp->recept_labels[0] = SCU_RECEPT_LABEL_0;
	scu_ctlp->recept_pm[0] = SCU_RECEPT_PM_0;
	scu_ctlp->recept_labels[1] = SCU_RECEPT_LABEL_1;
	scu_ctlp->recept_pm[1] = SCU_RECEPT_PM_1;
	if (ddi_prop_update_string_array(DDI_DEV_T_NONE, dip,
	    SCSI_HBA_PROP_RECEPTACLE_LABEL, &scu_ctlp->recept_labels[0],
	    scu_ctlp->scu_lib_ctl_num) != DDI_PROP_SUCCESS) {
		SCUDBG(scu_ctlp, SCUDBG_FMA, SCUDBG_ERROR|SCUDBG_TRACE,
		    "%s: failed to create %s property", __func__,
		    "receptacle-label");
	}
	if (ddi_prop_update_string_array(DDI_DEV_T_NONE, dip,
	    SCSI_HBA_PROP_RECEPTACLE_PM, &scu_ctlp->recept_pm[0],
	    scu_ctlp->scu_lib_ctl_num) != DDI_PROP_SUCCESS) {
		SCUDBG(scu_ctlp, SCUDBG_FMA, SCUDBG_ERROR|SCUDBG_TRACE,
		    "%s: failed to create %s property", __func__,
		    "receptacle-pm");
	}

	SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INIT, SCUDBG_TRACE,
	    "%s return with success value", __func__);

	return (DDI_SUCCESS);

err_out:
	if (attach_state & SCU_ATTACHSTATE_FMA_SUPPORT) {
		scu_fm_ereport(scu_ctlp, DDI_FM_DEVICE_INVAL_STATE);
		ddi_fm_service_impact(scu_ctlp->scu_dip,
		    DDI_SERVICE_LOST);
		scu_fm_fini(scu_ctlp);
	}

	if (attach_state & SCU_ATTACHSTATE_WATCHDOG_HANDLER) {
		(void) untimeout(scu_ctlp->scu_watchdog_timeid);
		scu_ctlp->scu_watchdog_timeid = 0;
	}

	if (attach_state & SCU_ATTACHSTATE_INTR_ENABLED) {
		for (i = 0; i < ctl_count; i++) {
			scic_controller_disable_interrupts(
			    scu_ctlp->scu_subctls[i].scus_scic_ctl_handle);
		}
	}

	if (attach_state & SCU_ATTACHSTATE_CTL_STARTED) {
		for (i = 0; i < ctl_count; i++) {
			(void) scif_controller_stop(
			    scu_ctlp->scu_subctls[i].scus_scif_ctl_handle,
			    scif_controller_get_suggested_start_timeout(
			    scu_ctlp->scu_subctls[i].scus_scif_ctl_handle));
		}
	}

	if (attach_state & SCU_ATTACHSTATE_PHYMAP_CREATE) {
		sas_phymap_destroy(scu_ctlp->scu_phymap);
	}

	if (attach_state & SCU_ATTACHSTATE_IPORTMAP_CREATE) {
		scsi_hba_iportmap_destroy(scu_ctlp->scu_iportmap);
	}

	if (attach_state & SCU_ATTACHSTATE_SCSI_ATTACH) {
		scu_scsa_teardown(scu_ctlp);
	}

	if (attach_state & SCU_ATTACHSTATE_LIB_INTERNAL_TASKQ) {
		for (i = 0; i < ctl_count; i++) {
			ddi_taskq_destroy(
			    scu_ctlp->scu_subctls[i].scus_lib_internal_taskq);
		}
	}

	if (attach_state & SCU_ATTACHSTATE_EVENT_THREAD) {
		scu_event_thread_destroy(scu_ctlp);
	}

	if (attach_state & SCU_ATTACHSTATE_DISCOVER_TASKQ) {
		ddi_taskq_destroy(scu_ctlp->scu_discover_taskq);
	}

	if (attach_state & SCU_ATTACHSTATE_CQ_ALLOC) {
		scu_stop_cq_threads(scu_ctlp);
	}

	if (attach_state & SCU_ATTACHSTATE_TGT_ALLOC) {
		kmem_free(scu_ctlp->scu_tgts,
		    sizeof (scu_tgt_t *) * scu_ctlp->scu_max_dev);
		kmem_free(scu_ctlp->scu_event_tgts,
		    sizeof (scu_event_t) * scu_ctlp->scu_max_dev);
	}

	if (attach_state & SCU_ATTACHSTATE_PHY_ALLOC) {
		scu_phyp = scu_ctlp->scu_root_phys;

		for (i = 0; i < scu_ctlp->scu_root_phy_num; i++) {
			mutex_destroy(&scu_phyp->scup_lock);
			scu_phyp++;
		}

		kmem_free(scu_ctlp->scu_root_phys,
		    scu_ctlp->scu_root_phy_num * sizeof (scu_phy_t));
		scu_ctlp->scu_root_phys = NULL;
	}

	if (attach_state & SCU_ATTACHSTATE_MUTEX_INIT) {
		mutex_destroy(&scu_ctlp->scu_lock);
		cv_destroy(&scu_ctlp->scu_cv);
		mutex_destroy(&scu_ctlp->scu_iport_lock);
		list_destroy(&scu_ctlp->scu_iports);
		mutex_destroy(&scu_ctlp->scu_cq_lock);
		for (i = 0; i < ctl_count; i++) {
			mutex_destroy(&scu_ctlp->scu_subctls[i]. \
			    scus_lib_hprq_lock);
			mutex_destroy(&scu_ctlp->scu_subctls[i]. \
			    scus_lib_remote_device_lock);
			mutex_destroy(&scu_ctlp->scu_subctls[i]. \
			    scus_slot_lock);
			cv_destroy(&scu_ctlp->scu_subctls[i].scus_reset_cv);
			cv_destroy(&scu_ctlp->scu_subctls[i]. \
			    scus_reset_complete_cv);
		}
		cv_destroy(&scu_ctlp->scu_cmd_complete_cv);
		mutex_destroy(&scu_ctlp->scu_event_lock);
		cv_destroy(&scu_ctlp->scu_event_disp_cv);
		cv_destroy(&scu_ctlp->scu_event_quit_cv);
	}

	if (attach_state & SCU_ATTACHSTATE_INTR_ADDED) {
		scu_rem_intrs(scu_ctlp);
	}

	if (attach_state & SCU_ATTACHSTATE_IO_REQUEST_ALLOC) {
		for (i = 0; i < ctl_count; i++) {
			scu_free_io_request(&(scu_ctlp->scu_subctls[i]));
		}
	}

	if (attach_state & SCU_ATTACHSTATE_IO_SLOT_ALLOC) {
		for (i = 0; i < ctl_count; i++) {
			kmem_free(scu_ctlp->scu_subctls[i].scus_io_slots,
			    SCI_MAX_IO_REQUESTS * sizeof (scu_io_slot_t));
		}
	}

	if (attach_state & SCU_ATTACHSTATE_LIB_MEMORY_DESCRIPTOR_ALLOC) {
		for (i = 0; i < ctl_count; i++) {
			scu_free_memory_descriptor(&(scu_ctlp->scu_subctls[i]));
		}
	}

	if (attach_state & SCU_ATTACHSTATE_SUBCTL_SETUP) {
		scu_teardown_subctls(scu_ctlp);
	}

	if (attach_state & SCU_ATTACHSTATE_SUBCTL_ALLOC) {
		kmem_free(scu_ctlp->scu_subctls,
		    ctl_count * sizeof (scu_subctl_t));
		kmem_free(scu_ctlp->scu_event_ctls,
		    (ctl_count + 1) * sizeof (scu_event_t));
	}

	if (attach_state & SCU_ATTACHSTATE_LIB_NONDMA_ALLOC) {
		kmem_free(scu_ctlp->scu_lib_non_dma_memory,
		    non_dma_needed);
	}

	if (attach_state & SCU_ATTACHSTATE_BAR_MAPPING_SETUP) {
		scu_bar_mapping_teardown(scu_ctlp);
	}

	if (attach_state & SCU_ATTACHSTATE_PCICFG_SETUP) {
		pci_config_teardown(&scu_ctlp->scu_pcicfg_handle);
	}

	if (attach_state & SCU_ATTACHSTATE_CSOFT_ALLOC) {
		ddi_soft_state_free(scu_softc_state, instance);
	}

	SCUDBG(NULL, SCUDBG_HBA|SCUDBG_INIT, SCUDBG_TRACE,
	    "%s return with failure value", __func__);

	return (DDI_FAILURE);
}

static int
scu_iport_detach(scu_iport_t *scu_iportp)
{
	scu_ctl_t	*scu_ctlp;
	int		instance;
	int		polls = 0;
	int		i, ctl_count;

	scu_ctlp = scu_iportp->scui_ctlp;
	ctl_count = scu_ctlp->scu_lib_ctl_num;

	SCUDBG(scu_ctlp, SCUDBG_IPORT, SCUDBG_TRACE, "%s: enter", __func__);

	instance = ddi_get_instance(scu_iportp->scui_dip);

	/* drain taskq and set status as not ready */
	for (i = 0; i < ctl_count; i++) {
		mutex_enter(&scu_ctlp->scu_lock);
		if (scu_ctlp->scu_subctls[i].scus_lib_internal_taskq)
			ddi_taskq_wait(scu_ctlp->scu_subctls[i].
			    scus_lib_internal_taskq);
		scu_ctlp->scu_subctls[i].scus_adapter_is_ready = 0;
		mutex_exit(&scu_ctlp->scu_lock);
	}

	mutex_enter(&scu_ctlp->scu_lock);
	if (scu_ctlp->scu_discover_taskq) {
		ddi_taskq_wait(scu_ctlp->scu_discover_taskq);
	}
	scu_ctlp->scu_started = 0;
	mutex_exit(&scu_ctlp->scu_lock);

	/*
	 * Here is workaround for SCSAv3.
	 * The Iport start to detach process, the outstanding active/inactive
	 * tgt work should be cancelled, call this function flushs pending
	 * SCSA tgt work,the active/inactive process is not valid.
	 */
	mutex_enter(&scu_ctlp->scu_lock);
	(void) scsi_hba_tgtmap_set_flush(scu_iportp->scui_iss_tgtmap);
	mutex_exit(&scu_ctlp->scu_lock);

	/* Check whether there still is configured target */
	if (scu_iport_has_tgts(scu_ctlp, scu_iportp)) {
		SCUDBG(scu_ctlp, SCUDBG_IPORT, SCUDBG_ERROR|SCUDBG_TRACE,
		    "%s: failure since iport%d still has target",
		    __func__, instance);
		return (DDI_FAILURE);
	}

	/* Remove the iport from the list if it's inactive in the phymap */
	mutex_enter(&scu_ctlp->scu_iport_lock);
	mutex_enter(&scu_iportp->scui_lock);

	if (scu_iportp->scui_ua_state == SCU_UA_ACTIVE) {
		if (scu_ctlp->scu_phymap != NULL) {
			/*
			 * The followed code is workaround for SCSAv3,
			 * SCSAv3 contained problem that it detached
			 * iport firstly, the sasphys still active, so
			 * we destroy sasphy and wait iport inactive
			 */
			mutex_exit(&scu_iportp->scui_lock);
			mutex_exit(&scu_ctlp->scu_iport_lock);
			sas_phymap_destroy(scu_ctlp->scu_phymap);
			scu_ctlp->scu_phymap = NULL;

			do {
				drv_usecwait(1000);
				polls++;
			} while ((scu_iportp->scui_ua_state !=
			    SCU_UA_INACTIVE) && (polls <= 1000));

			mutex_enter(&scu_ctlp->scu_iport_lock);
			mutex_enter(&scu_iportp->scui_lock);
		}
	}

	scu_iport_offline_subdevice(scu_iportp);
	mutex_exit(&scu_iportp->scui_lock);

	scu_ctlp->scu_iport_num--;
	ASSERT(scu_ctlp->scu_iport_num >= 0);

	list_remove(&scu_ctlp->scu_iports, scu_iportp);
	mutex_exit(&scu_ctlp->scu_iport_lock);

	/* Destroy the iport target map */
	if (scu_iport_tgtmap_destroy(scu_iportp) == SCU_FAILURE) {
		return (DDI_FAILURE);
	}

	/* Free the tgt soft state */
	if (scu_iportp->scui_tgt_sstate != NULL) {
		ddi_soft_state_bystr_fini(&scu_iportp->scui_tgt_sstate);
	}

	/* Free the unit address string */
	strfree(scu_iportp->scui_ua);

	mutex_destroy(&scu_iportp->scui_lock);
	ddi_soft_state_free(scu_iport_softstate, instance);

	return (DDI_SUCCESS);
}

/*
 * detach entry point for dev_ops
 */
static int
scu_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	scu_ctl_t	*scu_ctlp = NULL;
	scu_iport_t	*scu_iportp = NULL;
	scu_phy_t	*scu_phyp = NULL;
	int		instance;
	int		i, ctl_count;
	int		polls = 0;

	instance = ddi_get_instance(dip);

	if (scsi_hba_iport_unit_address(dip) != NULL) {
		/* This is iport node */
		scu_iportp = ddi_get_soft_state(scu_iport_softstate, instance);
		if (scu_iportp == NULL)
			return (DDI_FAILURE);
	} else {
		/* It's HBA node */
		scu_ctlp = ddi_get_soft_state(scu_softc_state, instance);
		if (scu_ctlp == NULL)
			return (DDI_FAILURE);
	}

	switch (cmd) {
	case DDI_DETACH:
		break;

	case DDI_SUSPEND:
		return (scu_suspend(dip));

	default:
		return (DDI_FAILURE);
	}

	if (scu_iportp)
		return (scu_iport_detach(scu_iportp));

	ctl_count = scu_ctlp->scu_lib_ctl_num;

	/* Disable all interrupts */
	for (i = 0; i < ctl_count; i++) {
		scic_controller_disable_interrupts(
		    scu_ctlp->scu_subctls[i].scus_scic_ctl_handle);
	}

	/* Stop the watchdog handler */
	(void) untimeout(scu_ctlp->scu_watchdog_timeid);
	scu_ctlp->scu_watchdog_timeid = 0;

	/* Stop the controller */
	for (i = 0; i < ctl_count; i++) {
		(void) scif_controller_stop(
		    scu_ctlp->scu_subctls[i].scus_scif_ctl_handle,
		    scif_controller_get_suggested_start_timeout(
		    scu_ctlp->scu_subctls[i].scus_scif_ctl_handle));

		/* wait scif_controller_stop_cb */
		polls = 0;
		do {
			if (scu_poll_intr(scu_ctlp) != DDI_INTR_CLAIMED)
				drv_usecwait(1000);
			polls++;
		} while ((scu_ctlp->scu_subctls[i].scus_stopped != 1) &&
		    (polls <= 1000));
		SCUDBG(scu_ctlp, SCUDBG_HBA, SCUDBG_INFO,
		    "%s: scu_subctls[0x%d] status is 0x%d",
		    __func__, i, scu_ctlp->scu_subctls[i].
		    scus_stopped);
	}

	/* Destroy phymap and iportmap */
	if (scu_ctlp->scu_phymap != NULL) {
		sas_phymap_destroy(scu_ctlp->scu_phymap);
	}
	scsi_hba_iportmap_destroy(scu_ctlp->scu_iportmap);
	scu_offline_subiport(scu_ctlp);
	/* Teardown SCSA */
	scu_scsa_teardown(scu_ctlp);

	/* Destroy LIB internal taskq */
	for (i = 0; i < ctl_count; i++) {
		ddi_taskq_destroy(
		    scu_ctlp->scu_subctls[i].scus_lib_internal_taskq);
	}

	/* Destroy event thread */
	scu_event_thread_destroy(scu_ctlp);

	/* Destroy discover taskq */
	ddi_taskq_destroy(scu_ctlp->scu_discover_taskq);

	/* Stop the cq threads */
	scu_stop_cq_threads(scu_ctlp);

	/* Free the targets */
	kmem_free(scu_ctlp->scu_tgts,
	    sizeof (scu_tgt_t *) * scu_ctlp->scu_max_dev);

	kmem_free(scu_ctlp->scu_event_tgts,
	    sizeof (scu_event_t) * scu_ctlp->scu_max_dev);

	/* Free the phys */
	scu_phyp = scu_ctlp->scu_root_phys;

	for (i = 0; i < scu_ctlp->scu_root_phy_num; i++) {
		mutex_destroy(&scu_phyp->scup_lock);
		scu_phyp++;
	}

	kmem_free(scu_ctlp->scu_root_phys,
	    scu_ctlp->scu_root_phy_num * sizeof (scu_phy_t));
	scu_ctlp->scu_root_phys = NULL;
	scu_phyp = scu_ctlp->scu_root_phys;

	/* Destroy the mutex */
	mutex_destroy(&scu_ctlp->scu_lock);
	cv_destroy(&scu_ctlp->scu_cv);
	mutex_destroy(&scu_ctlp->scu_iport_lock);
	list_destroy(&scu_ctlp->scu_iports);
	mutex_destroy(&scu_ctlp->scu_cq_lock);
	for (i = 0; i < ctl_count; i++) {
		mutex_destroy(&scu_ctlp->scu_subctls[i]. \
		    scus_lib_hprq_lock);
		mutex_destroy(&scu_ctlp->scu_subctls[i]. \
		    scus_lib_remote_device_lock);
		mutex_destroy(&scu_ctlp->scu_subctls[i]. \
		    scus_slot_lock);
		cv_destroy(&scu_ctlp->scu_subctls[i].scus_reset_cv);
		cv_destroy(&scu_ctlp->scu_subctls[i].scus_reset_complete_cv);
	}
	cv_destroy(&scu_ctlp->scu_cmd_complete_cv);
	mutex_destroy(&scu_ctlp->scu_event_lock);
	cv_destroy(&scu_ctlp->scu_event_disp_cv);
	cv_destroy(&scu_ctlp->scu_event_quit_cv);

	/* Remove the interrupts */
	scu_rem_intrs(scu_ctlp);

	/* Free IO request DMA safe memory */
	for (i = 0; i < ctl_count; i++) {
		scu_free_io_request(&(scu_ctlp->scu_subctls[i]));
	}

	/* Free IO slot memory */
	for (i = 0; i < ctl_count; i++) {
		kmem_free(scu_ctlp->scu_subctls[i].scus_io_slots,
		    SCI_MAX_IO_REQUESTS * sizeof (scu_io_slot_t));
	}

	/* Free memory descriptor DMA safe memory */
	for (i = 0; i < ctl_count; i++) {
		scu_free_memory_descriptor(&(scu_ctlp->scu_subctls[i]));
	}

	/* Tear down sub-controllers */
	scu_teardown_subctls(scu_ctlp);

	/* Free sub-controllers */
	kmem_free(scu_ctlp->scu_subctls,
	    scu_ctlp->scu_lib_ctl_num * sizeof (scu_subctl_t));

	kmem_free(scu_ctlp->scu_event_ctls,
	    (scu_ctlp->scu_lib_ctl_num + 1) * sizeof (scu_event_t));

	/* Free LIB non dma memory */
	kmem_free(scu_ctlp->scu_lib_non_dma_memory,
	    scu_ctlp->scu_lib_non_dma_needed);

	/* Teardown mapping address for register BAR */
	scu_bar_mapping_teardown(scu_ctlp);

	/* Teardown PCI config space mapping */
	pci_config_teardown(&scu_ctlp->scu_pcicfg_handle);

	/* Disable FMA */
	scu_fm_fini(scu_ctlp);

	/* Free soft state */
	ddi_soft_state_free(scu_softc_state, instance);

	ddi_prop_remove_all(dip);

	return (DDI_SUCCESS);
}

/*
 *  queiesce entry point for dev_ops
 */
/*ARGSUSED*/
static int
scu_quiesce(dev_info_t *dip)
{
	scu_ctl_t	*scu_ctlp;
	scu_subctl_t	*scu_subctlp;
	int		instance;
	SCI_STATUS	sci_status;
	int		i;

	instance = ddi_get_instance(dip);
	scu_ctlp = ddi_get_soft_state(scu_softc_state, instance);
	if (scu_ctlp == NULL)
		return (DDI_SUCCESS);

	/* No quiesce necessary on a per-iport basis */
	if (scsi_hba_iport_unit_address(dip) != NULL) {
		return (DDI_SUCCESS);
	}

	scu_subctlp = scu_ctlp->scu_subctls;

	for (i = 0; i < scu_ctlp->scu_lib_ctl_num; i++) {

		/* Disable all interrupts */
		scic_controller_disable_interrupts(
		    scu_subctlp->scus_scic_ctl_handle);

		sci_status = scif_controller_reset(
		    scu_subctlp->scus_scif_ctl_handle);
		if ((sci_status != SCI_SUCCESS) &&
		    (sci_status != SCI_WARNING_ALREADY_IN_STATE))
			return (DDI_FAILURE);

		scu_subctlp++;
	}

	return (DDI_SUCCESS);
}

/*
 * Event Handling
 */

static int
scu_event_quit(scu_ctl_t *scu_ctlp, int index)
{
	_NOTE(ARGUNUSED(index));

	SCUDBG(scu_ctlp, SCUDBG_HBA, SCUDBG_INFO,
	    "%s: event index %d", __func__, index);
	mutex_enter(&scu_ctlp->scu_event_lock);
	scu_ctlp->scu_events = 0;
	cv_signal(&scu_ctlp->scu_event_quit_cv);
	mutex_exit(&scu_ctlp->scu_event_lock);
	return (-1);
}

/* Recover routine */
static int
scu_event_controller_error(scu_ctl_t *scu_ctlp, int index)
{
	scu_subctl_t	*scu_subctlp = &scu_ctlp->scu_subctls[index];

	SCUDBG(scu_ctlp, SCUDBG_HBA, SCUDBG_INFO,
	    "%s: event subctl 0x%p (%d)", __func__, (void *)scu_subctlp, index);

	/* Now call controller reset */
	(void) scu_reset_controller(scu_subctlp, 0);
	return (1);
}

void
scu_clear_tgt_timeout(scu_tgt_t *scu_tgtp)
{
	ASSERT(mutex_owned(&scu_tgtp->scut_lock));
	/* Clear luns' timeout state */
	if (scu_tgtp->scut_lun_timeouts) {
		scu_lun_t	*scu_lunp;
		for (scu_lunp = list_head(&scu_tgtp->scut_luns); scu_lunp;
		    scu_lunp = list_next(&scu_tgtp->scut_luns, scu_lunp)) {
			if (scu_lunp->scul_timeout) {
				scu_lunp->scul_timeout = 0;
				scu_tgtp->scut_lun_timeouts--;
			}
		}
	}
	ASSERT(scu_tgtp->scut_lun_timeouts == 0);
	if (scu_tgtp->scut_timeout)
		scu_tgtp->scut_timeout = 0;
}

void
scu_clear_tgt_timeouts(scu_subctl_t *scu_subctlp)
{
	scu_ctl_t	*scu_ctlp = scu_subctlp->scus_ctlp;
	scu_tgt_t	*scu_tgtp;
	int		i, begin;

	begin = scu_subctlp->scus_num * SCI_MAX_REMOTE_DEVICES;
	for (i = begin; i < begin + SCI_MAX_REMOTE_DEVICES; i++) {
		if ((scu_tgtp = scu_ctlp->scu_tgts[i]) == NULL)
			continue;
		mutex_enter(&scu_tgtp->scut_lock);
		scu_clear_tgt_timeout(scu_tgtp);
		mutex_exit(&scu_tgtp->scut_lock);
	}
}

static int
scu_event_controller_reset(scu_ctl_t *scu_ctlp, int index)
{
	_NOTE(ARGUNUSED(index));

	SCUDBG(scu_ctlp, SCUDBG_HBA, SCUDBG_INFO,
	    "%s: event index %d", __func__, index);
	/* XXX race condition */
	scu_flush_cmd(scu_ctlp, NULL);

	/* re-run start_wq as event_device_reset */
	scu_start_wqs(scu_ctlp);
	return (1);
}

static int
scu_event_device_error(scu_ctl_t *scu_ctlp, int index)
{
	scu_tgt_t	*scu_tgtp = scu_ctlp->scu_tgts[index];
	scu_subctl_t	*scu_subctlp = scu_tgtp->scut_subctlp;

	SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_RECOVER, SCUDBG_INFO,
	    "%s: event index %d", __func__, index);
	/* Call target reset */
	if (scu_hard_reset(scu_subctlp, scu_tgtp, 0) != SCU_TASK_GOOD)
		/* Dispatch controller error */
		scu_controller_error(scu_subctlp);
	return (1);
}

static int
scu_event_device_reset(scu_ctl_t *scu_ctlp, int index)
{
	_NOTE(ARGUNUSED(index));

	SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_RECOVER, SCUDBG_INFO,
	    "%s: event index %d", __func__, index);
	scu_flush_cmd(scu_ctlp, NULL);

	/*
	 * if event_device_ready is run before reset_device change
	 * scut_resetting flag, the start_wq will fail to work, so
	 * re-run start_wqs in here.
	 */
	scu_start_wqs(scu_ctlp);
	return (1);
}

/*
 * timeout error recovery handling
 *
 * This method will be triggered by below routines:
 *	scu_watchdog_handler
 *	scu_poll_cmd
 */
static int
scu_event_device_timeout(scu_ctl_t *scu_ctlp, int index)
{
	scu_tgt_t	*scu_tgtp = scu_ctlp->scu_tgts[index];
	scu_subctl_t	*scu_subctlp = scu_tgtp->scut_subctlp;
	int		rval;

	SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_RECOVER, SCUDBG_INFO,
	    "%s: enter, try to reset device 0x%p to recover timeout",
	    __func__, (void *)scu_tgtp);

	/* Call target reset */
	rval = scu_reset_target(scu_subctlp, scu_tgtp, 0);
	if (rval != SCU_TASK_GOOD && rval != SCU_TASK_TIMEOUT) {
		/* Dispatch device error */
		scu_device_error(scu_subctlp, scu_tgtp);
	}
	return (1);
}

static int
scu_event_device_lun_timeout(scu_ctl_t *scu_ctlp, int index)
{
	scu_tgt_t	*scu_tgtp = scu_ctlp->scu_tgts[index];
	scu_subctl_t	*scu_subctlp = scu_tgtp->scut_subctlp;
	scu_lun_t	*scu_lunp;
	int		rval;

	SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_RECOVER, SCUDBG_INFO,
	    "%s: enter, try to reset device 0x%p to recover timeout",
	    __func__, (void *)scu_tgtp);

	/* First try lun reset */
	for (scu_lunp = list_head(&scu_tgtp->scut_luns); scu_lunp;
	    scu_lunp = list_next(&scu_tgtp->scut_luns, scu_lunp)) {
		if (scu_lunp->scul_timeout)
			break;
	}
	if (scu_lunp == NULL)
		return (1);

	rval = scu_reset_lun(scu_subctlp, scu_lunp, 0);
	if (rval == SCU_TASK_GOOD) {
		/* Check if device still has lun timeouts pending */
		if (scu_tgtp->scut_lun_timeouts)
			return (0);
	} else if (rval != SCU_TASK_TIMEOUT) {
		/*
		 * If not recursive timeouts, escalate to device timeout;
		 * otherwise watchdog would've dispatched a device error.
		 */
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_RECOVER, SCUDBG_INFO,
		    "%s: tgt 0x%p reset escalate", __func__, (void *)scu_tgtp);
		scu_timeout(scu_subctlp, scu_tgtp, NULL);
	}
	return (1);
}

/*
 * XXX Clear the requests that were previously terminated by SCIL
 * but did not complete back to us.
 */
static void
scu_check_timeout(scu_subctl_t *scu_subctlp, scu_tgt_t *scu_tgtp)
{
	scu_ctl_t	*scu_ctlp = scu_subctlp->scus_ctlp;
	int		i;

	mutex_enter(&scu_subctlp->scus_slot_lock);
	for (i = 0; i < scu_subctlp->scus_slot_num; i++) {
		scu_io_slot_t	*scu_slotp;
		scu_cmd_t	*scu_cmdp;

		scu_slotp = &scu_subctlp->scus_io_slots[i];
		if (scu_slotp->scu_io_active_timeout > 0)
			continue;
		if ((scu_cmdp = scu_slotp->scu_io_slot_cmdp) == NULL)
			continue;
		if (scu_cmdp->cmd_tgtp != scu_tgtp)
			continue;

		SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_TGT|SCUDBG_WATCH,
		    SCUDBG_ERROR,
		    "%s: !!! cmd 0x%p %s not cleared on target 0x%p",
		    __func__, (void *)scu_cmdp, SCU_CMD_NAME(scu_cmdp),
		    (void *)scu_tgtp);
		ASSERT(scu_cmdp->cmd_timeout);
		ASSERT(scu_cmdp->cmd_task == 0);
		mutex_exit(&scu_subctlp->scus_slot_lock);

		scif_cb_io_request_complete(
		    scu_subctlp->scus_scif_ctl_handle,
		    scu_tgtp->scut_lib_remote_device,
		    scu_cmdp->cmd_lib_io_request,
		    SCI_IO_FAILURE_TERMINATED);

		mutex_enter(&scu_subctlp->scus_slot_lock);
	}
	mutex_exit(&scu_subctlp->scus_slot_lock);
}

static int
scu_event_device_ready(scu_ctl_t *scu_ctlp, int index)
{
	scu_tgt_t	*scu_tgtp = scu_ctlp->scu_tgts[index];

	SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_RECOVER, SCUDBG_INFO,
	    "%s: event index %d", __func__, index);
	scu_check_timeout(scu_tgtp->scut_subctlp, scu_tgtp);
	scu_start_wq(scu_ctlp, index);
	return (1);
}

static int (*scu_event_handlers[SCU_EVENT_ID_NUM])(scu_ctl_t *, int) = {
	scu_event_quit,
	scu_event_controller_error,
	scu_event_controller_reset,
	scu_event_device_error,
	scu_event_device_reset,
	scu_event_device_timeout,
	scu_event_device_lun_timeout,
	scu_event_device_ready
};

static void
scu_event_thread(scu_ctl_t *scu_ctlp)
{
	int		events;
	int		ev_id, ev_bit;
	scu_event_t	*evp;
	int		rval;

	mutex_enter(&scu_ctlp->scu_event_lock);
	for (;;) {
		/* Wait event */
		if ((events = scu_ctlp->scu_events) == 0) {
			cv_wait(&scu_ctlp->scu_event_disp_cv,
			    &scu_ctlp->scu_event_lock);
			continue;
		}
		/* Get event of the highest priority */
		evp = NULL;
		for (ev_id = 0; ev_id < SCU_EVENT_ID_NUM; ev_id++) {
			ev_bit = SCU_EVENT_ID2BIT(ev_id);
			if (events & ev_bit) {
				STAILQ_FOREACH(evp, &scu_ctlp->scu_eventq,
				    ev_next) {
					if (evp->ev_events & ev_bit)
						break;
				}
				ASSERT(evp);
				break;
			}
		}
		mutex_exit(&scu_ctlp->scu_event_lock);

		/* Process event */
		rval = scu_event_handlers[ev_id](scu_ctlp, evp->ev_index);
		if (rval == -1)
			break;

		mutex_enter(&scu_ctlp->scu_event_lock);
		/* Event still active, do not clear it */
		if (rval == 0)
			continue;
		/* Clear event */
		evp->ev_events &= ~ev_bit;
		if (evp->ev_events == 0) {
			STAILQ_REMOVE(&scu_ctlp->scu_eventq,
			    evp, scu_event, ev_next);
		}
		if (--scu_ctlp->scu_event_num[ev_id] == 0)
			scu_ctlp->scu_events &= ~ev_bit;
	}
}

void
scu_event_dispatch(scu_ctl_t *scu_ctlp, enum scu_event_id ev_id, int index)
{
	int		ev_bit = SCU_EVENT_ID2BIT(ev_id);
	scu_event_t	*evp;

	ASSERT(mutex_owned(&scu_ctlp->scu_event_lock));

	if (ev_id == SCU_EVENT_ID_QUIT)
		evp = &scu_ctlp->scu_event_ctls[scu_ctlp->scu_lib_ctl_num];
	else if (ev_id < SCU_EVENT_ID_DEVICE_ERROR)
		evp = &scu_ctlp->scu_event_ctls[index];
	else
		evp = &scu_ctlp->scu_event_tgts[index];

	/* Event already dispatched */
	if (evp->ev_events & ev_bit)
		return;

	/* First event for the device links in its evp */
	if (evp->ev_events == 0)
		STAILQ_INSERT_TAIL(&scu_ctlp->scu_eventq, evp, ev_next);
	evp->ev_events |= ev_bit;
	scu_ctlp->scu_event_num[ev_id]++;

	if ((scu_ctlp->scu_events & ev_bit) == 0) {
		scu_ctlp->scu_events |= ev_bit;
		cv_signal(&scu_ctlp->scu_event_disp_cv);
	}
}

static void
scu_event_thread_destroy(scu_ctl_t *scu_ctlp)
{
	mutex_enter(&scu_ctlp->scu_event_lock);
	scu_event_dispatch(scu_ctlp, SCU_EVENT_ID_QUIT, -1);
	cv_wait(&scu_ctlp->scu_event_quit_cv, &scu_ctlp->scu_event_lock);
	mutex_exit(&scu_ctlp->scu_event_lock);
}

void
scu_timeout(scu_subctl_t *scu_subctlp, scu_tgt_t *scu_tgtp, scu_cmd_t *scu_cmdp)
{
	scu_ctl_t	*scu_ctlp = scu_subctlp->scus_ctlp;
	scu_lun_t	*scu_lunp = NULL;

	/* Update cmd and tgt states */
	if (scu_cmdp) {
		ASSERT(mutex_owned(&scu_subctlp->scus_slot_lock));
		scu_cmdp->cmd_timeout = 1;
		scu_lunp = scu_cmdp->cmd_lunp;
	}

	mutex_enter(&scu_tgtp->scut_lock);
	if (scu_lunp) {
		if (scu_lunp->scul_timeout == 0) {
			scu_lunp->scul_timeout = 1;
			scu_tgtp->scut_lun_timeouts++;
		} else { /* timeout already in process */
			goto exit;
		}
	} else {
		if (scu_tgtp->scut_timeout == 0)
			scu_tgtp->scut_timeout = 1;
		else /* timeout already in process */
			goto exit;
	}

	/* Dispatch event */
	mutex_enter(&scu_ctlp->scu_event_lock);
	scu_event_dispatch(scu_ctlp,
	    (scu_lunp ? SCU_EVENT_ID_DEVICE_LUN_TIMEOUT :
	    SCU_EVENT_ID_DEVICE_TIMEOUT), scu_tgtp->scut_tgt_num);
	mutex_exit(&scu_ctlp->scu_event_lock);
exit:
	mutex_exit(&scu_tgtp->scut_lock);
}

void
scu_device_error(scu_subctl_t *scu_subctlp, scu_tgt_t *scu_tgtp)
{
	scu_ctl_t	*scu_ctlp = scu_subctlp->scus_ctlp;

	SCUDBG(scu_ctlp, SCUDBG_HBA, SCUDBG_INFO,
	    "%s: subctl 0x%p (%d) err",
	    __func__, (void *)scu_subctlp, scu_subctlp->scus_num);

	mutex_enter(&scu_ctlp->scu_event_lock);
	mutex_enter(&scu_tgtp->scut_lock);
	scu_tgtp->scut_error = 1;
	mutex_exit(&scu_tgtp->scut_lock);

	scu_event_dispatch(scu_ctlp, SCU_EVENT_ID_DEVICE_ERROR,
	    scu_tgtp->scut_tgt_num);
	mutex_exit(&scu_ctlp->scu_event_lock);
}

void
scu_controller_error(scu_subctl_t *scu_subctlp)
{
	scu_ctl_t	*scu_ctlp = scu_subctlp->scus_ctlp;
	int 		ctl_locked;

	SCUDBG(scu_ctlp, SCUDBG_HBA, SCUDBG_INFO,
	    "%s: subctl 0x%p (%d) err",
	    __func__, (void *)scu_subctlp, scu_subctlp->scus_num);

	/*
	 * XXX Conditional locking is ugly
	 */
	ctl_locked = mutex_owned(&scu_ctlp->scu_lock);
	if (ctl_locked == 0) {
		mutex_enter(&scu_ctlp->scu_lock);
	}
#ifdef DEBUG
	else {
		SCUDBG(scu_ctlp, SCUDBG_TASK|SCUDBG_INTR,
		    SCUDBG_TRACE|SCUDBG_INFO,
		    "%s: scu_lock already owned", __func__);
	}
#endif

	scu_subctlp->scus_error = 1;

	mutex_enter(&scu_ctlp->scu_event_lock);
	scu_event_dispatch(scu_ctlp, SCU_EVENT_ID_CONTROLLER_ERROR,
	    scu_subctlp->scus_num);
	mutex_exit(&scu_ctlp->scu_event_lock);
	if (ctl_locked == 0) {
		mutex_exit(&scu_ctlp->scu_lock);
	}
}

/*
 * timeout handling
 */
static void
scu_watchdog_handler(void *arg)
{
	scu_ctl_t	*scu_ctlp = (scu_ctl_t *)arg;
	scu_subctl_t	*scu_subctlp;
	int		i, j;

	/* Check whether active commands got timeout */
	for (j = 0, scu_subctlp = scu_ctlp->scu_subctls;
	    j < scu_ctlp->scu_lib_ctl_num;
	    j++, scu_subctlp++) {

		mutex_enter(&scu_subctlp->scus_slot_lock);
		for (i = 0; i < scu_subctlp->scus_slot_num; i++) {
			scu_io_slot_t	*scu_slotp;
			scu_cmd_t	*scu_cmdp;
			scu_tgt_t	*scu_tgtp;

			scu_slotp = &scu_subctlp->scus_io_slots[i];
			if (scu_slotp->scu_io_active_timeout <= 0)
				continue;
			scu_slotp->scu_io_active_timeout -= scu_watch_tick;
			if (scu_slotp->scu_io_active_timeout > 0)
				continue;
			if ((scu_cmdp = scu_slotp->scu_io_slot_cmdp) == NULL)
				continue;

			SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_TGT|SCUDBG_WATCH,
			    SCUDBG_ERROR,
			    "%s: cmd 0x%p got timeout on target 0x%p",
			    __func__, (void *)scu_cmdp,
			    (void *)scu_cmdp->cmd_tgtp);

			scu_tgtp = scu_cmdp->cmd_tgtp;
			if (scu_cmdp->cmd_task == 0) {
				scu_timeout(scu_subctlp, scu_tgtp, scu_cmdp);
			} else {
				/* Task management cmd timeout */
				scu_clear_active_task(scu_subctlp, scu_cmdp);
				if (scu_cmdp->cmd_sync) {
					cv_broadcast(
					    &scu_ctlp->scu_cmd_complete_cv);
				}
				scu_device_error(scu_subctlp, scu_tgtp);
			}
		}
		mutex_exit(&scu_subctlp->scus_slot_lock);
	}

	/* Re-install the watchdog timeout handler */
	mutex_enter(&scu_ctlp->scu_lock);
	if (scu_ctlp->scu_watchdog_timeid != 0) {
		scu_ctlp->scu_watchdog_timeid = timeout(scu_watchdog_handler,
		    scu_ctlp, scu_watch_interval);
	}
	mutex_exit(&scu_ctlp->scu_lock);
}

/*
 * Interrupt registration routine
 */
static int
scu_register_intrs(scu_ctl_t *scu_ctlp)
{
	dev_info_t	*dip = scu_ctlp->scu_dip;
	int		intr_types, i;
	int		rval = SCU_FAILURE;

	SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INTR|SCUDBG_INIT, SCUDBG_TRACE,
	    "%s enter", __func__);

	/*
	 * Get the supported interrupt types
	 */
	if (ddi_intr_get_supported_types(dip, &intr_types) != DDI_SUCCESS) {
		scu_log(scu_ctlp, CE_WARN, "!ddi_intr_get_supported_types "
		    "failed");
		rval = SCU_FAILURE;
		goto exit;
	}

	SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INTR, SCUDBG_INFO,
	    "0x%x intr_types are supported", intr_types);

	if (scu_msix_enabled && (intr_types & DDI_INTR_TYPE_MSIX)) {
		/*
		 * Try MSI-X first, but fall back to FIXED if failed
		 */
		if (scu_alloc_intrs(scu_ctlp, DDI_INTR_TYPE_MSIX) ==
		    DDI_SUCCESS) {
			scu_ctlp->scu_intr_type = DDI_INTR_TYPE_MSIX;
			SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INTR|SCUDBG_INIT,
			    SCUDBG_INFO, "%s: Using MSIX interrupt type",
			    __func__);
			rval = SCU_SUCCESS;
			goto next;
		}
	}

	if (intr_types & DDI_INTR_TYPE_FIXED) {
		if (scu_alloc_intrs(scu_ctlp, DDI_INTR_TYPE_FIXED) ==
		    DDI_SUCCESS) {
			scu_ctlp->scu_intr_type = DDI_INTR_TYPE_FIXED;
			SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INTR|SCUDBG_INIT,
			    SCUDBG_INFO, "%s: Using FIXed interrupt type",
			    __func__);
			rval = SCU_SUCCESS;
		} else {
			SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INTR|SCUDBG_INIT,
			    SCUDBG_ERROR, "%s: Alloc Intr failed",
			    __func__);
			rval = SCU_FAILURE;
			goto exit;
		}
	}

next:
	if (scu_add_intrs(scu_ctlp) != DDI_SUCCESS) {
		SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INTR|SCUDBG_INIT,
		    SCUDBG_ERROR, "%s: Intr registration got failure",
		    __func__);
		rval = SCU_FAILURE;
		goto exit;
	}

	/* Get no interrupt routine for polling mode */
	for (i = 0; i < scu_ctlp->scu_lib_ctl_num; i++) {
		if (scic_controller_get_handler_methods(SCIC_NO_INTERRUPTS, 0,
		    &scu_ctlp->scu_subctls[i].scus_lib_poll_handler)
		    != SCI_SUCCESS) {
			SCUDBG(scu_ctlp, SCUDBG_INTR,
			    SCUDBG_TRACE|SCUDBG_ERROR,
			    "%s: scic_controller_get_handler_methods "
			    "failed for no interrupt mode", __func__);
			rval = SCU_FAILURE;
			goto exit;
		}
	}

exit:
	SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INTR|SCUDBG_INIT, SCUDBG_TRACE,
	    "%s exit, rval = %d", __func__, rval);

	return (rval);
}

static int
scu_alloc_intrs(scu_ctl_t *scu_ctlp, int intr_type)
{
	dev_info_t	*dip = scu_ctlp->scu_dip;
	int		count, avail, actual;
	int		rc;

	SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INTR, SCUDBG_TRACE,
	    "%s: enter", __func__);

	/* Get number of interrupts */
	rc = ddi_intr_get_nintrs(dip, intr_type, &count);
	if ((rc != DDI_SUCCESS) || (count == 0)) {
		SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INTR,
		    SCUDBG_TRACE|SCUDBG_ERROR,
		    "%s: ddi_intr_get_nintrs failed for intr_type %d "
		    "count %d",
		    __func__, intr_type, count);
		return (DDI_FAILURE);
	}

	SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INTR, SCUDBG_INFO,
	    "%s: ddi_intr_get_nintrs returned count %d", __func__, count);

	ASSERT(count <= scu_ctlp->scu_lib_ctl_num * SCI_MAX_MSIX_MESSAGES);

	/* Get number of available interrupts */
	rc = ddi_intr_get_navail(dip, intr_type, &avail);
	if ((rc != DDI_SUCCESS) || (avail == 0)) {
		SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INTR,
		    SCUDBG_TRACE|SCUDBG_ERROR,
		    "%s: ddi_intr_get_navail failed for intr_type %d "
		    "avail %d",
		    __func__, intr_type, avail);
		return (DDI_FAILURE);
	}

	SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INTR, SCUDBG_INFO,
	    "%s: ddi_intr_get_navail returned avail %d", __func__, avail);

	/* For MSI-X interrupt */
	if (intr_type == DDI_INTR_TYPE_MSIX) {
		if (avail < count) {
			SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INTR,
			    SCUDBG_WARNING,
			    "%s: return failure since avail %d less than "
			    "count %d",
			    __func__, avail, count);
			return (DDI_FAILURE);
		}

		if (scu_ctlp->scu_lib_ctl_num == SCI_MAX_CONTROLLERS) {
			if (count % SCI_MAX_CONTROLLERS) {
				SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INTR,
				    SCUDBG_WARNING,
				    "%s: invalid count %d, sub-controller "
				    "num %d", __func__, count,
				    scu_ctlp->scu_lib_ctl_num);
				return (DDI_FAILURE);
			}
		}
	}

	/* Allocate an array of interrupt handles */
	scu_ctlp->scu_intr_size = count * sizeof (ddi_intr_handle_t);
	scu_ctlp->scu_intr_htable =
	    kmem_zalloc(scu_ctlp->scu_intr_size, KM_SLEEP);

	/* Call ddi_intr_alloc */
	rc = ddi_intr_alloc(dip, scu_ctlp->scu_intr_htable,
	    intr_type, 0, count, &actual, DDI_INTR_ALLOC_NORMAL);
	if ((rc != DDI_SUCCESS) || (actual == 0)) {
		kmem_free(scu_ctlp->scu_intr_htable,
		    scu_ctlp->scu_intr_size);
		SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INTR,
		    SCUDBG_TRACE|SCUDBG_ERROR,
		    "%s: ddi_intr_alloc failed for intr_type %d",
		    __func__, intr_type);
		return (DDI_FAILURE);
	}

	/* Use interrupt count returned */
	scu_ctlp->scu_intr_count = actual;

	return (DDI_SUCCESS);
}

/*
 * Handle MSIX or FIXED interrupts
 */
static int
scu_add_intrs(scu_ctl_t *scu_ctlp)
{
	int	i, ctl_count;

	SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INTR, SCUDBG_TRACE,
	    "%s: enter", __func__);

	switch (scu_ctlp->scu_intr_count) {
	case 1:
		if (scu_ctlp->scu_intr_type == DDI_INTR_TYPE_FIXED) {
			/* Fixed mode interrupt */
			scu_ctlp->scu_intr_handler[0] = scu_all_intr;

			for (ctl_count = 0;
			    ctl_count < scu_ctlp->scu_lib_ctl_num;
			    ctl_count++) {
				if (scic_controller_get_handler_methods(
				    SCIC_LEGACY_LINE_INTERRUPT_TYPE,
				    0,
				    scu_ctlp->scu_subctls[ctl_count]. \
				    scus_lib_intr_handler) != SCI_SUCCESS) {
					SCUDBG(scu_ctlp,
					    SCUDBG_INTR,
					    SCUDBG_TRACE|SCUDBG_ERROR,
					    "%s: scic_controller_get_handler_"
					    "methods failed for FIXED "
					    "interrupt for sub-controller %d",
					    __func__, ctl_count);
					goto err_out;
				}
			}

			/* Add the intr handler */
			if (ddi_intr_add_handler(scu_ctlp->scu_intr_htable[0],
			    scu_ctlp->scu_intr_handler[0],
			    (caddr_t)scu_ctlp, NULL) != DDI_SUCCESS) {

				SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INTR,
				    SCUDBG_TRACE|SCUDBG_ERROR,
				    "%s: ddi_intr_add_handler failed",
				    __func__);
				goto err_out;
			}

		} else if (scu_ctlp->scu_intr_type == DDI_INTR_TYPE_MSIX) {
			/*
			 * MSI-X mode interrupt
			 *
			 * - 1 sub-controller, 1 message
			 */
			ASSERT(scu_ctlp->scu_lib_ctl_num == 1);

			scu_ctlp->scu_intr_handler[0] = scu_complete_intr;

			if (scic_controller_get_handler_methods(
			    SCIC_MSIX_INTERRUPT_TYPE,
			    1,
			    scu_ctlp->scu_subctls[0]. \
			    scus_lib_intr_handler) != SCI_SUCCESS) {
				SCUDBG(scu_ctlp,
				    SCUDBG_INTR,
				    SCUDBG_TRACE|SCUDBG_ERROR,
				    "%s: scic_controller_get_handler_"
				    "methods failed for 1 count MSIX "
				    "interrupt for sub-controller %d",
				    __func__, ctl_count);
				goto err_out;
			}

			/* Add the intr handler */
			if (ddi_intr_add_handler(scu_ctlp->scu_intr_htable[0],
			    scu_ctlp->scu_intr_handler[0],
			    (caddr_t)(&scu_ctlp->scu_subctls[0]),
			    NULL) != DDI_SUCCESS) {

				SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INTR,
				    SCUDBG_TRACE|SCUDBG_ERROR,
				    "%s: ddi_intr_add_handler failed",
				    __func__);
				goto err_out;
			}
		}

		break;

	case 2:
		/*
		 * MSI-X mode interrupt - two cases:
		 *
		 * - 1 sub-controller, 2 messages
		 * - 2 sub-controllers, 1 message for each
		 */
		ASSERT(scu_ctlp->scu_intr_type == DDI_INTR_TYPE_MSIX);

		if (scu_ctlp->scu_lib_ctl_num != SCI_MAX_CONTROLLERS) {
			/* 1 sub-controller, 2 messages */
			scu_ctlp->scu_intr_handler[0] = scu_complete_intr;
			scu_ctlp->scu_intr_handler[1] = scu_error_intr;

			if (scic_controller_get_handler_methods(
			    SCIC_MSIX_INTERRUPT_TYPE,
			    2,
			    scu_ctlp->scu_subctls[0]. \
			    scus_lib_intr_handler) != SCI_SUCCESS) {
				SCUDBG(scu_ctlp,
				    SCUDBG_INTR,
				    SCUDBG_TRACE|SCUDBG_ERROR,
				    "%s: scic_controller_get_handler_"
				    "methods failed for 2 count MSIX "
				    "interrupt for sub-controller 0", __func__);
				goto err_out;
			}

			/* Add the intr handler */
			for (i = 0; i < scu_ctlp->scu_intr_count; i++) {
				if (ddi_intr_add_handler(
				    scu_ctlp->scu_intr_htable[i],
				    scu_ctlp->scu_intr_handler[i],
				    (caddr_t)(&scu_ctlp->scu_subctls[0]),
				    NULL) != DDI_SUCCESS) {
					for (i--; i >= 0; i--) {
						(void) ddi_intr_remove_handler(
						    scu_ctlp-> \
						    scu_intr_htable[i]);
					}

					SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INTR,
					    SCUDBG_TRACE|SCUDBG_ERROR,
					    "%s: ddi_intr_add_handler failed",
					    __func__);
					goto err_out;
				}
			}

			break;
		}

		/* 2 sub-controllers, 1 message for each */
		scu_ctlp->scu_intr_handler[0] = scu_complete_intr;
		scu_ctlp->scu_intr_handler[1] = scu_complete_intr;

		for (ctl_count = 0;
		    ctl_count < scu_ctlp->scu_lib_ctl_num;
		    ctl_count++) {
			if (scic_controller_get_handler_methods(
			    SCIC_MSIX_INTERRUPT_TYPE,
			    1,
			    scu_ctlp->scu_subctls[ctl_count]. \
			    scus_lib_intr_handler) != SCI_SUCCESS) {
				SCUDBG(scu_ctlp,
				    SCUDBG_INTR,
				    SCUDBG_TRACE|SCUDBG_ERROR,
				    "%s: scic_controller_get_handler_"
				    "methods failed for 1 count MSIX "
				    "interrupt for sub-controller %d",
				    __func__, ctl_count);
				goto err_out;
			}

			/* Add the intr handler */
			if (ddi_intr_add_handler(
			    scu_ctlp->scu_intr_htable[ctl_count],
			    scu_ctlp->scu_intr_handler[ctl_count],
			    (caddr_t)(&scu_ctlp->scu_subctls[ \
			    ctl_count]),
			    NULL) != DDI_SUCCESS) {
				for (ctl_count--; ctl_count >= 0; ctl_count--) {
					(void) ddi_intr_remove_handler(
					    scu_ctlp-> \
					    scu_intr_htable[ctl_count]);
				}

				SCUDBG(scu_ctlp,
				    SCUDBG_HBA|SCUDBG_INTR,
				    SCUDBG_TRACE|SCUDBG_ERROR,
				    "%s: ddi_intr_add_handler "
				    "failed", __func__);
				goto err_out;
			}
		}
		break;

	case 4:
		/*
		 * MSI-X mode interrupt
		 *
		 * - 2 sub-controllers, 2 messages for each
		 */
		ASSERT(scu_ctlp->scu_intr_type == DDI_INTR_TYPE_MSIX);
		ASSERT(scu_ctlp->scu_lib_ctl_num == 2);

		scu_ctlp->scu_intr_handler[0] = scu_complete_intr;
		scu_ctlp->scu_intr_handler[1] = scu_error_intr;
		scu_ctlp->scu_intr_handler[2] = scu_complete_intr;
		scu_ctlp->scu_intr_handler[3] = scu_error_intr;

		for (ctl_count = 0;
		    ctl_count < scu_ctlp->scu_lib_ctl_num; ctl_count++) {
			if (scic_controller_get_handler_methods(
			    SCIC_MSIX_INTERRUPT_TYPE,
			    2,
			    scu_ctlp->scu_subctls[ctl_count]. \
			    scus_lib_intr_handler) != SCI_SUCCESS) {
				SCUDBG(scu_ctlp,
				    SCUDBG_INTR,
				    SCUDBG_TRACE|SCUDBG_ERROR,
				    "%s: scic_controller_get_handler_"
				    "methods failed for 2 count MSIX "
				    "interrupt for sub-controller %d",
				    __func__, ctl_count);
				goto err_out;
			}
		}

		/* Add the intr handler */
		for (i = 0; i < scu_ctlp->scu_intr_count; i++) {
			if (ddi_intr_add_handler(scu_ctlp->scu_intr_htable[i],
			    scu_ctlp->scu_intr_handler[i],
			    (caddr_t)(&scu_ctlp->scu_subctls[i / \
			    SCI_MAX_CONTROLLERS]),
			    NULL) != DDI_SUCCESS) {
				for (i--; i >= 0; i--) {
					(void) ddi_intr_remove_handler(
					    scu_ctlp->scu_intr_htable[i]);
				}

				SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INTR,
				    SCUDBG_TRACE|SCUDBG_ERROR,
				    "%s: ddi_intr_add_handler failed",
				    __func__);
				goto err_out;
			}
		}

		break;

	default:
		SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INTR,
		    SCUDBG_TRACE|SCUDBG_ERROR,
		    "%s: intr count %d is not valid",
		    __func__, scu_ctlp->scu_intr_count);
		goto err_out;
	}

	/*
	 * Get priority for first, assume remaining are all the same
	 */
	if (ddi_intr_get_pri(scu_ctlp->scu_intr_htable[0],
	    &scu_ctlp->scu_intr_pri) != DDI_SUCCESS) {
		SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INTR,
		    SCUDBG_TRACE|SCUDBG_ERROR,
		    "%s: ddi_intr_get_pri failed", __func__);
		goto err_out;
	}

	/* Test for high level interrupt */
	if (scu_ctlp->scu_intr_pri >= ddi_intr_get_hilevel_pri()) {
		SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INTR,
		    SCUDBG_TRACE|SCUDBG_ERROR,
		    "%s: pri %d is higher than ddi_intr_get_hilevel_pri",
		    __func__, scu_ctlp->scu_intr_pri);
		goto err_out;
	}

	if (ddi_intr_get_cap(scu_ctlp->scu_intr_htable[0],
	    &scu_ctlp->scu_intr_cap) != DDI_SUCCESS) {
		for (i = 0; i < scu_ctlp->scu_intr_count; i++) {
			(void) ddi_intr_remove_handler(
			    scu_ctlp->scu_intr_htable[i]);
		}
		SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INTR,
		    SCUDBG_TRACE|SCUDBG_ERROR,
		    "%s: ddi_intr_get_cap failed", __func__);
		goto err_out;
	}

	if (scu_ctlp->scu_intr_cap & DDI_INTR_FLAG_BLOCK) {
		/* Call ddi_intr_block_enable() for MSIX */
		(void) ddi_intr_block_enable(scu_ctlp->scu_intr_htable,
		    scu_ctlp->scu_intr_count);
	} else {
		/* call ddi_intr_enable() for FIXED or MSIX non block enable */
		for (i = 0; i < scu_ctlp->scu_intr_count; i++) {
			(void) ddi_intr_enable(scu_ctlp->scu_intr_htable[i]);
		}
	}

	SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INTR, SCUDBG_TRACE,
	    "%s: return successfully", __func__);
	return (DDI_SUCCESS);

err_out:

	/* Free already allocated intr */
	for (i = 0; i < scu_ctlp->scu_intr_count; i++) {
		(void) ddi_intr_free(scu_ctlp->scu_intr_htable[i]);
	}

	kmem_free(scu_ctlp->scu_intr_htable, scu_ctlp->scu_intr_size);

	return (DDI_FAILURE);
}

/*
 * Disable all interrupts for scu_ctlp
 *
 * Note: currently it's only called in scu_reset_controller
 */
int
scu_disable_intrs(scu_ctl_t *scu_ctlp)
{
	int		i;
	int		rval;

	/* Disable all interrupts */
	if ((scu_ctlp->scu_intr_type == DDI_INTR_TYPE_MSIX) &&
	    (scu_ctlp->scu_intr_cap & DDI_INTR_FLAG_BLOCK)) {
		/* Call ddi_intr_block_disable */
		rval = ddi_intr_block_disable(scu_ctlp->scu_intr_htable,
		    scu_ctlp->scu_intr_count);
	} else {
		for (i = 0; i < scu_ctlp->scu_intr_count; i++) {
			rval = ddi_intr_disable(
			    scu_ctlp->scu_intr_htable[i]);
			if (rval != DDI_SUCCESS)
				break;
		}
	}
	return (rval);
}

/*
 * Enable all interrupts for scu_ctlp
 *
 * Note: currently it's only called in scu_reset_controller
 */
int
scu_enable_intrs(scu_ctl_t *scu_ctlp)
{
	int		i;
	int		rval;

	/* ensable all interrupts */
	if ((scu_ctlp->scu_intr_type == DDI_INTR_TYPE_MSIX) &&
	    (scu_ctlp->scu_intr_cap & DDI_INTR_FLAG_BLOCK)) {
		/* Call ddi_intr_block_enable */
		rval = ddi_intr_block_enable(scu_ctlp->scu_intr_htable,
		    scu_ctlp->scu_intr_count);
	} else {
		for (i = 0; i < scu_ctlp->scu_intr_count; i++) {
			rval = ddi_intr_enable(
			    scu_ctlp->scu_intr_htable[i]);
			if (rval != DDI_SUCCESS)
				break;
		}
	}
	return (rval);
}

/*
 * Remove the registered interrrupts
 */
static void
scu_rem_intrs(scu_ctl_t *scu_ctlp)
{
	int i;

	SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INTR, SCUDBG_TRACE,
	    "%s: enter", __func__);

	/* Disable all interrupts */
	(void) scu_disable_intrs(scu_ctlp);

	/* Remove handlers & Free allocated intrs */
	for (i = 0; i < scu_ctlp->scu_intr_count; i++) {
		(void) ddi_intr_remove_handler(
		    scu_ctlp->scu_intr_htable[i]);
		(void) ddi_intr_free(scu_ctlp->scu_intr_htable[i]);
	}

	kmem_free(scu_ctlp->scu_intr_htable, scu_ctlp->scu_intr_size);

	SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INTR, SCUDBG_TRACE,
	    "%s: return", __func__);
}

/*
 * handler for poll mode
 */
uint_t
scu_poll_intr(scu_ctl_t *scu_ctlp)
{
	SCIC_CONTROLLER_HANDLER_METHODS_T handler;
	SCI_CONTROLLER_HANDLE_T	controller;
	uint_t	rval = DDI_INTR_UNCLAIMED;
	int	ctl_count;

	SCUDBG(scu_ctlp, SCUDBG_INTR, SCUDBG_TRACE,
	    "%s: enter", __func__);

	mutex_enter(&scu_ctlp->scu_lock);

	for (ctl_count = 0; ctl_count < scu_ctlp->scu_lib_ctl_num;
	    ctl_count++) {
		/* Get SCIC poll handler */
		handler = scu_ctlp->scu_subctls[ctl_count]. \
		    scus_lib_poll_handler;
		controller = scu_ctlp->scu_subctls[ctl_count]. \
		    scus_scic_ctl_handle;

		/* Check whether there is pending event */
		if (handler.interrupt_handler(controller) == TRUE) {
			SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INTR,
			    SCUDBG_TRACE,
			    "%s: call completion handler 0x%p",
			    __func__, (void *)&handler.completion_handler);

			/* Call the completion handler */
			handler.completion_handler(controller);
			rval = DDI_INTR_CLAIMED;
		}
	}

	SCUDBG(scu_ctlp, SCUDBG_INTR, SCUDBG_TRACE,
	    "%s: return with value %u", __func__, rval);
	mutex_exit(&scu_ctlp->scu_lock);

	return (rval);
}

/*
 * The interrupt handler for FIXED interrupt
 */
static uint_t
scu_all_intr(caddr_t arg1, caddr_t arg2)
{
	scu_ctl_t		*scu_ctlp = (scu_ctl_t *)(void *)arg1;
	SCIC_CONTROLLER_HANDLER_METHODS_T handler;
	SCI_CONTROLLER_HANDLE_T	controller;
	int			ctl_count;
	uint_t			rval = DDI_INTR_UNCLAIMED;

	_NOTE(ARGUNUSED(arg2));

	mutex_enter(&scu_ctlp->scu_lock);
	if (!scu_ctlp->scu_started) {
		mutex_exit(&scu_ctlp->scu_lock);
		return (DDI_INTR_UNCLAIMED);
	}

	for (ctl_count = 0; ctl_count < scu_ctlp->scu_lib_ctl_num;
	    ctl_count++) {
		/* Get SCIC intr handler */
		handler = scu_ctlp->scu_subctls[ctl_count]. \
		    scus_lib_intr_handler[0];
		controller = scu_ctlp->scu_subctls[ctl_count]. \
		    scus_scic_ctl_handle;

		/* Check whether there is pending interrupt */
		if (handler.interrupt_handler(controller) == TRUE) {

			/* Call the interrupt handler */
			SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INTR,
			    SCUDBG_TRACE, "%s: call completion handler 0x%p",
			    __func__, (void *)&handler.completion_handler);

			handler.completion_handler(controller);
			rval = DDI_INTR_CLAIMED;
		}
	}

	mutex_exit(&scu_ctlp->scu_lock);

	if (rval == DDI_INTR_CLAIMED) {
		/* If there are queued command, then start them now */
		scu_start_wqs(scu_ctlp);
	}

	SCUDBG(scu_ctlp, SCUDBG_INTR, SCUDBG_TRACE,
	    "%s: return with value %u", __func__, rval);

	return (rval);
}

/*
 * Completion handler for MSI-X support
 */
/*ARGSUSED*/
static uint_t
scu_complete_intr(caddr_t arg1, caddr_t arg2)
{
	scu_subctl_t		*scu_subctlp = (scu_subctl_t *)(void *)arg1;
	scu_ctl_t		*scu_ctlp = scu_subctlp->scus_ctlp;
	SCIC_CONTROLLER_HANDLER_METHODS_T handler;
	SCI_CONTROLLER_HANDLE_T	controller;
	uint_t			rval = DDI_INTR_UNCLAIMED;

	SCUDBG(scu_ctlp, SCUDBG_INTR, SCUDBG_TRACE,
	    "%s: enter", __func__);

	mutex_enter(&scu_ctlp->scu_lock);

	/* Get SCIC complete intr handler */
	handler = scu_subctlp->scus_lib_intr_handler[0];
	controller = scu_subctlp->scus_scic_ctl_handle;

	/* Check whether there is pending interrupt */
	if (handler.interrupt_handler(controller) == TRUE) {

		/* Call the interrupt handler */
		SCUDBG(scu_ctlp, SCUDBG_INTR, SCUDBG_INFO,
		    "%s: call completion handler 0x%p",
		    __func__, (void *)&handler.completion_handler);

		handler.completion_handler(controller);
		rval = DDI_INTR_CLAIMED;
	}

	mutex_exit(&scu_ctlp->scu_lock);

	/*
	 * If there are queued command, then start them now
	 */
	scu_start_wqs(scu_ctlp);

	SCUDBG(scu_ctlp, SCUDBG_INTR, SCUDBG_TRACE,
	    "%s: return with value %u", __func__, rval);

	return (rval);
}

/*
 * Error handler for MSI-X support
 */
/*ARGSUSED*/
static uint_t
scu_error_intr(caddr_t arg1, caddr_t arg2)
{
	scu_subctl_t	*scu_subctlp = (scu_subctl_t *)(void *)arg1;
	scu_ctl_t	*scu_ctlp = scu_subctlp->scus_ctlp;
	SCIC_CONTROLLER_HANDLER_METHODS_T handler;
	SCI_CONTROLLER_HANDLE_T	controller;
	uint_t		rval = DDI_INTR_UNCLAIMED;

	SCUDBG(scu_ctlp, SCUDBG_INTR, SCUDBG_TRACE,
	    "%s: enter", __func__);

	mutex_enter(&scu_ctlp->scu_lock);

	/* Get SCIC error intr handler */
	handler = scu_subctlp->scus_lib_intr_handler[1];
	controller = scu_subctlp->scus_scic_ctl_handle;

	/* Check whether there is pending interrupt */
	if (handler.interrupt_handler(controller) == TRUE) {

		/* Call the interrupt handler */
		SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INTR,
		    SCUDBG_INFO,
		    "%s: call completion handler 0x%p",
		    __func__, (void *)&handler.completion_handler);

		handler.completion_handler(controller);
		rval = DDI_INTR_CLAIMED;
	}

	SCUDBG(scu_ctlp, SCUDBG_INTR, SCUDBG_TRACE,
	    "%s: return with value %u", __func__, rval);
	mutex_exit(&scu_ctlp->scu_lock);

	return (rval);
}

/*
 * Set up mapping address for register BAR
 */
static int
scu_bar_mapping_setup(scu_ctl_t	*scu_ctlp)
{
	int			i;
	int			rval = DDI_SUCCESS;

	for (i = 0; i < SCU_MAX_BAR; i++) {
		if (ddi_regs_map_setup(scu_ctlp->scu_dip,
		    (i + 1),
		    &scu_ctlp->scu_bar_addr[i],
		    0,
		    0,
		    &scu_ctlp->reg_acc_attr,
		    &scu_ctlp->scu_bar_map[i]) != DDI_SUCCESS) {
			rval = DDI_FAILURE;
			goto err_out;
		}
	}

	return (rval);

err_out:

	for (i--; i >= 0; i--) {
		ddi_regs_map_free(&scu_ctlp->scu_bar_map[i]);
		scu_ctlp->scu_bar_map[i] = NULL;
	}

	return (rval);
}

/*
 * Tear down mapping address for register BAR
 */
static void
scu_bar_mapping_teardown(scu_ctl_t *scu_ctlp)
{
	int	i;

	for (i = 0; i < SCU_MAX_BAR; i++) {
		if ((scu_ctlp->scu_bar_map[i]) != NULL) {
			ddi_regs_map_free(&scu_ctlp->scu_bar_map[i]);
			scu_ctlp->scu_bar_map[i] = NULL;
		}
	}
}

/*
 * Set up sub-controllers
 */
static int
scu_setup_subctls(scu_ctl_t *scu_ctlp, int ctl_count)
{
	scu_subctl_t *scu_subctlp;
	int	i, j;
	SCI_DOMAIN_HANDLE_T	domain_handle;

	scu_subctlp = scu_ctlp->scu_subctls;
	for (i = 0; i < ctl_count; i++) {
		scu_subctlp->scus_ctlp = scu_ctlp;
		scu_subctlp->scus_num = i;

		/*
		 * First allocate the SCIF controller
		 */
		if (scif_library_allocate_controller(
		    scu_ctlp->scu_scif_lib_handle,
		    &scu_subctlp->scus_scif_ctl_handle) != SCI_SUCCESS) {
			scu_log(scu_ctlp, CE_WARN,
			    "!cannot allocate SCIF controller");
			goto err_out;
		}

		/*
		 * Construct the SCIF controller
		 */
		if (scif_controller_construct(
		    scu_ctlp->scu_scif_lib_handle,
		    scu_subctlp->scus_scif_ctl_handle) != SCI_SUCCESS) {
			scu_log(scu_ctlp, CE_WARN,
			    "!cannot construct SCIF controller");
			(void) scif_library_free_controller(
			    scu_ctlp->scu_scif_lib_handle,
			    scu_subctlp->scus_scif_ctl_handle);
			goto err_out;
		}

		/*
		 * Set the association between SCIF controller and
		 * OSSL sub-controller
		 */
		(void) sci_object_set_association(
		    scu_subctlp->scus_scif_ctl_handle, (void *)scu_subctlp);

		/*
		 * Get the SCIC controller object
		 */
		scu_subctlp->scus_scic_ctl_handle =
		    scif_controller_get_scic_handle(
		    scu_subctlp->scus_scif_ctl_handle);

		/*
		 * Get handles for all valid domains
		 */
		for (j = 0; j < SCI_MAX_DOMAINS; j++) {
			if (scif_controller_get_domain_handle(
			    scu_subctlp->scus_scif_ctl_handle,
			    j,
			    &domain_handle) != SCI_SUCCESS) {
				scu_log(scu_ctlp, CE_WARN,
				    "!cannot get SCIF domain %d handle", j);
				(void) scif_library_free_controller(
				    scu_ctlp->scu_scif_lib_handle,
				    scu_subctlp->scus_scif_ctl_handle);
				goto err_out;
			}

			scu_subctlp->scus_domains[j].scu_lib_domain_handle =
			    domain_handle;
		}

		scu_subctlp++;
	}

	return (SCU_SUCCESS);

err_out:
	scu_subctlp--;
	for (i--; i >= 0; i--) {
		(void) scif_library_free_controller(
		    scu_ctlp->scu_scif_lib_handle,
		    scu_subctlp->scus_scif_ctl_handle);
		scu_subctlp--;
	}

	return (SCU_FAILURE);
}

/*
 * Tear down sub-controllers
 */
static void
scu_teardown_subctls(scu_ctl_t *scu_ctlp)
{
	scu_subctl_t *scu_subctlp;
	int	i, j;

	scu_subctlp = scu_ctlp->scu_subctls;
	for (i = 0; i < scu_ctlp->scu_lib_ctl_num; i++) {
		(void) scif_library_free_controller(
		    scu_ctlp->scu_scif_lib_handle,
		    scu_subctlp->scus_scif_ctl_handle);
		scu_subctlp->scus_scif_ctl_handle = NULL;
		scu_subctlp->scus_scic_ctl_handle = NULL;
		for (j = 0; j < SCI_MAX_DOMAINS; j++)
			scu_subctlp->scus_domains[j].scu_lib_domain_handle =
			    NULL;
		scu_subctlp++;
	}
}

/*
 * Allocate DMA Safe memory for memory descriptor list,
 */
static int
scu_alloc_memory_descriptor(scu_subctl_t *scu_subctlp)
{
	scu_ctl_t				*scu_ctlp;
	SCI_CONTROLLER_HANDLE_T			controller;
	SCI_MEMORY_DESCRIPTOR_LIST_HANDLE_T	mdl_handle;
	SCI_PHYSICAL_MEMORY_DESCRIPTOR_T	*current_mde;
	scu_lib_md_item_t			*scu_lib_md;
	ddi_dma_handle_t			*dma_handle;
	ddi_acc_handle_t			*acc_handle;
	size_t					size;
	int					count = 0;

	scu_ctlp = scu_subctlp->scus_ctlp;
	SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INIT, SCUDBG_TRACE,
	    "%s enter", __func__);

	controller = scu_subctlp->scus_scif_ctl_handle;

	mdl_handle = sci_controller_get_memory_descriptor_list_handle(
	    controller);

	/* Rewind to the first memory descriptor entry in the list */
	sci_mdl_first_entry(mdl_handle);

	/* Get the current memory descriptor entry */
	current_mde = sci_mdl_get_current_entry(mdl_handle);

	while (current_mde) {
		count++;

		/* Move pointer to next sequential memory descriptor */
		sci_mdl_next_entry(mdl_handle);
		current_mde = sci_mdl_get_current_entry(mdl_handle);
	}

	scu_subctlp->scus_lib_memory_descriptor_count = count;

	/* Now allocate memory for DMA handlers for future free */
	scu_subctlp->scus_lib_memory_descriptors = kmem_zalloc(
	    count * sizeof (scu_lib_md_item_t), KM_SLEEP);

	scu_lib_md = scu_subctlp->scus_lib_memory_descriptors;
	dma_handle = &scu_lib_md->scu_lib_md_dma_handle;
	acc_handle = &scu_lib_md->scu_lib_md_acc_handle;

	/* Rewind to the first memory descriptor entry in the list */
	sci_mdl_first_entry(mdl_handle);

	/* Get the current memory descriptor entry */
	current_mde = sci_mdl_get_current_entry(mdl_handle);

	count = 0;

	while (current_mde) {
		count++;

		/* Set the appropriate dma attribute */
		size = current_mde->constant_memory_size;
		scu_memory_descriptor_dma_attr.dma_attr_align =
		    current_mde->constant_memory_alignment;

		/* Allocate DMA Safe memory */
		if (scu_alloc_dma_safe_memory(scu_subctlp,
		    scu_memory_descriptor_dma_attr,
		    size,
		    (caddr_t *)&(current_mde->virtual_address),
		    &(current_mde->physical_address),
		    dma_handle,
		    acc_handle) != SCU_SUCCESS) {
			goto err_out;
		}

		/* Move pointer to next sequential memory descriptor */
		sci_mdl_next_entry(mdl_handle);
		current_mde = sci_mdl_get_current_entry(mdl_handle);
		scu_lib_md++;
		dma_handle = &scu_lib_md->scu_lib_md_dma_handle;
		acc_handle = &scu_lib_md->scu_lib_md_acc_handle;
	}

	ASSERT(scu_subctlp->scus_lib_memory_descriptor_count == count);
	SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INIT, SCUDBG_TRACE,
	    "%s exit with successful return value", __func__);

	return (SCU_SUCCESS);

err_out:
	scu_free_memory_descriptor(scu_subctlp);

	SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INIT, SCUDBG_TRACE|SCUDBG_ERROR,
	    "%s exit with failure return value", __func__);

	return (SCU_FAILURE);
}

/*
 * Free DMA Safe memory for memory descriptor list,
 */
static void
scu_free_memory_descriptor(scu_subctl_t *scu_subctlp)
{
	scu_ctl_t				*scu_ctlp;
	SCI_CONTROLLER_HANDLE_T			controller;
	SCI_MEMORY_DESCRIPTOR_LIST_HANDLE_T	mdl_handle;
	SCI_PHYSICAL_MEMORY_DESCRIPTOR_T	*current_mde;
	scu_lib_md_item_t			*scu_lib_md;
	ddi_dma_handle_t			*dma_handle;
	ddi_acc_handle_t			*acc_handle;

	scu_ctlp = scu_subctlp->scus_ctlp;
	SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INIT, SCUDBG_TRACE,
	    "%s enter", __func__);

	controller = scu_subctlp->scus_scif_ctl_handle;
	scu_lib_md = scu_subctlp->scus_lib_memory_descriptors;
	dma_handle = &scu_lib_md->scu_lib_md_dma_handle;
	acc_handle = &scu_lib_md->scu_lib_md_acc_handle;

	mdl_handle = sci_controller_get_memory_descriptor_list_handle(
	    controller);

	/* Rewind to the first memory descriptor entry in the list */
	sci_mdl_first_entry(mdl_handle);

	/* Get the current memory descriptor entry */
	current_mde = sci_mdl_get_current_entry(mdl_handle);

	while (current_mde) {
		/* No allocated resources */
		if (current_mde->physical_address == 0)
			break;
		/* Free the resources */
		scu_free_dma_safe_memory(dma_handle, acc_handle);

		/* Move pointer to next sequential memory descriptor */
		sci_mdl_next_entry(mdl_handle);
		current_mde = sci_mdl_get_current_entry(mdl_handle);
		scu_lib_md++;
		dma_handle = &scu_lib_md->scu_lib_md_dma_handle;
		acc_handle = &scu_lib_md->scu_lib_md_acc_handle;
	}

	kmem_free(scu_subctlp->scus_lib_memory_descriptors,
	    scu_subctlp->scus_lib_memory_descriptor_count *
	    sizeof (scu_lib_md_item_t));

	SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INIT, SCUDBG_TRACE,
	    "%s exit", __func__);
}

/*
 * Allocate DMA Safe memory for per-IO request
 */
static int
scu_alloc_io_request(scu_subctl_t *scu_subctlp)
{
	scu_ctl_t		*scu_ctlp;
	scu_io_slot_t		*io_slot;
	ddi_dma_handle_t	*dma_handle;
	ddi_acc_handle_t	*acc_handle;
	size_t			size;
	int			i;

	scu_ctlp = scu_subctlp->scus_ctlp;
	SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INIT, SCUDBG_TRACE,
	    "%s enter", __func__);

	size = scif_io_request_get_object_size();

	io_slot = scu_subctlp->scus_io_slots;
	dma_handle = &(io_slot->scu_io_dma_handle);
	acc_handle = &(io_slot->scu_io_acc_handle);

	for (i = 0; i < scu_subctlp->scus_slot_num; i++) {
		if (scu_alloc_dma_safe_memory(scu_subctlp,
		    scu_io_request_dma_attr,
		    size,
		    &(io_slot->scu_io_virtual_address),
		    &(io_slot->scu_io_physical_address),
		    dma_handle,
		    acc_handle) != SCU_SUCCESS) {
			goto err_out;
		}

		io_slot->scu_io_lib_tag = SCI_CONTROLLER_INVALID_IO_TAG;
		io_slot->scu_io_active_timeout = -1;
		io_slot->scu_io_subctlp = scu_subctlp;

		io_slot++;
		dma_handle = &(io_slot->scu_io_dma_handle);
		acc_handle = &(io_slot->scu_io_acc_handle);
	}

	SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INIT, SCUDBG_TRACE,
	    "%s exit with successful return value", __func__);

	return (SCU_SUCCESS);

err_out:
	io_slot--;
	dma_handle = &(io_slot->scu_io_dma_handle);
	acc_handle = &(io_slot->scu_io_acc_handle);

	for (i--; i >= 0; i--) {
		scu_free_dma_safe_memory(dma_handle, acc_handle);
		io_slot--;
		dma_handle = &(io_slot->scu_io_dma_handle);
		acc_handle = &(io_slot->scu_io_acc_handle);
	}

	SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INIT, SCUDBG_TRACE|SCUDBG_ERROR,
	    "%s exit with failure return value", __func__);

	return (SCU_FAILURE);
}

/*
 * Free DMA safe memory for per-IO request
 */
static void
scu_free_io_request(scu_subctl_t *scu_subctlp)
{
	scu_io_slot_t		*io_slot;
	ddi_dma_handle_t	*dma_handle;
	ddi_acc_handle_t	*acc_handle;
	int			i;

	io_slot = scu_subctlp->scus_io_slots;
	dma_handle = &(io_slot->scu_io_dma_handle);
	acc_handle = &(io_slot->scu_io_acc_handle);

	for (i = 0; i < scu_subctlp->scus_slot_num; i++) {
		scu_free_dma_safe_memory(dma_handle, acc_handle);
		io_slot++;
		dma_handle = &(io_slot->scu_io_dma_handle);
		acc_handle = &(io_slot->scu_io_acc_handle);
	}
}

/*
 * Allocate DMA Safe memory for task request, and also allocate
 * the corresponding scu_io_slot_t
 */
int
scu_alloc_task_request(scu_subctl_t *scu_subctlp, scu_io_slot_t **io_slot,
    size_t size)
{
	scu_ctl_t		*scu_ctlp;
	ddi_dma_handle_t	*dma_handle;
	ddi_acc_handle_t	*acc_handle;
	scu_io_slot_t		*scu_io_slot;

	scu_ctlp = scu_subctlp->scus_ctlp;
	SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INIT, SCUDBG_TRACE,
	    "%s enter", __func__);

	scu_io_slot =
	    (scu_io_slot_t *)kmem_zalloc(sizeof (scu_io_slot_t), KM_SLEEP);
	*io_slot = scu_io_slot;

	dma_handle = &(scu_io_slot->scu_io_dma_handle);
	acc_handle = &(scu_io_slot->scu_io_acc_handle);

	if (scu_alloc_dma_safe_memory(scu_subctlp,
	    scu_io_request_dma_attr,
	    size,
	    &(scu_io_slot->scu_io_virtual_address),
	    &(scu_io_slot->scu_io_physical_address),
	    dma_handle,
	    acc_handle) != SCU_SUCCESS) {
			goto err_out;
	}

	scu_io_slot->scu_io_active_timeout = -1;

	SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INIT, SCUDBG_TRACE,
	    "%s exit with successful return value", __func__);

	return (SCU_SUCCESS);

err_out:
	kmem_free(scu_io_slot, sizeof (scu_io_slot_t));

	SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_INIT, SCUDBG_TRACE|SCUDBG_ERROR,
	    "%s exit with failure return value", __func__);

	return (SCU_FAILURE);
}

/*
 * Free DMA safe memory for per-IO request
 */
/*ARGSUSED*/
void
scu_free_task_request(scu_subctl_t *scu_subctlp, scu_io_slot_t **io_slot)
{
	ddi_dma_handle_t	*dma_handle;
	ddi_acc_handle_t	*acc_handle;
	scu_io_slot_t		*scu_io_slot = *io_slot;

	dma_handle = &(scu_io_slot->scu_io_dma_handle);
	acc_handle = &(scu_io_slot->scu_io_acc_handle);

	scu_free_dma_safe_memory(dma_handle, acc_handle);
	kmem_free(scu_io_slot, sizeof (scu_io_slot_t));
}

/*
 * allocate DMA Safe memory
 */
static int
scu_alloc_dma_safe_memory(scu_subctl_t *scu_subctlp, ddi_dma_attr_t dma_attr,
    size_t size, caddr_t *virtual_address, uint64_t *physical_address,
    ddi_dma_handle_t *dma_handle, ddi_acc_handle_t *acc_handle)
{
	scu_ctl_t		*scu_ctlp = scu_subctlp->scus_ctlp;
	dev_info_t		*dip = scu_ctlp->scu_dip;
	size_t			ret_len;
	ddi_dma_cookie_t	dma_cookie;
	uint_t			cookie_count;

	/* Allocate DMA handle */
	if (ddi_dma_alloc_handle(dip, &dma_attr, DDI_DMA_SLEEP, NULL,
	    dma_handle) != DDI_SUCCESS) {
		SCUDBG(scu_ctlp, SCUDBG_INIT|SCUDBG_HBA,
		    SCUDBG_ERROR|SCUDBG_TRACE,
		    "%s: ddi_dma_alloc_handle failed", __func__);
		return (SCU_FAILURE);
	}

	if (ddi_dma_mem_alloc(*dma_handle, size, &scu_ctlp->reg_acc_attr,
	    DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL, virtual_address,
	    &ret_len, acc_handle) != DDI_SUCCESS) {
		SCUDBG(scu_ctlp, SCUDBG_INIT|SCUDBG_HBA,
		    SCUDBG_ERROR|SCUDBG_TRACE,
		    "%s: ddi_dma_mem_alloc failed", __func__);
		ddi_dma_free_handle(dma_handle);
		return (SCU_FAILURE);
	}

	if (ddi_dma_addr_bind_handle(*dma_handle, NULL,
	    *virtual_address,
	    size, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &dma_cookie, &cookie_count) != DDI_DMA_MAPPED) {
		SCUDBG(scu_ctlp, SCUDBG_INIT|SCUDBG_HBA,
		    SCUDBG_ERROR|SCUDBG_TRACE,
		    "%s: ddi_dma_addr_bind_handle failed", __func__);
		ddi_dma_mem_free(acc_handle);
		ddi_dma_free_handle(dma_handle);
		return (SCU_FAILURE);
	}

	if (cookie_count != 1) {
		SCUDBG(scu_ctlp, SCUDBG_INIT|SCUDBG_HBA,
		    SCUDBG_ERROR|SCUDBG_TRACE,
		    "%s: cookie_count %d not 1", __func__, cookie_count);
		(void) ddi_dma_unbind_handle(*dma_handle);
		ddi_dma_mem_free(acc_handle);
		ddi_dma_free_handle(dma_handle);
		return (SCU_FAILURE);
	}

	*physical_address = dma_cookie.dmac_laddress;

	return (SCU_SUCCESS);
}

/*
 * Free DMA safe memory
 */
static void
scu_free_dma_safe_memory(ddi_dma_handle_t *dma_handle,
    ddi_acc_handle_t *acc_handle)
{
	/* Unbind dma handle first */
	(void) ddi_dma_unbind_handle(*dma_handle);

	/* Then free the underlying memory */
	ddi_dma_mem_free(acc_handle);

	/* Now free the handle itself */
	ddi_dma_free_handle(dma_handle);
}

static void
scu_stop_cq_threads(scu_ctl_t *scu_ctlp)
{
	scu_cq_thread_t	*scu_cqt;
	int		i;

	mutex_enter(&scu_ctlp->scu_cq_lock);
	scu_ctlp->scu_cq_stop = 1;
	for (i = 0; i < scu_ctlp->scu_cq_thread_num; i++) {
		if (scu_ctlp->scu_cq_thread_list[i].scu_cq_thread) {
			scu_cqt = &scu_ctlp->scu_cq_thread_list[i];
			cv_signal(&scu_cqt->scu_cq_thread_cv);
			mutex_exit(&scu_ctlp->scu_cq_lock);
			thread_join(scu_cqt->scu_cq_thread_id);
			mutex_enter(&scu_ctlp->scu_cq_lock);
			cv_destroy(&scu_cqt->scu_cq_thread_cv);
		}
	}
	mutex_exit(&scu_ctlp->scu_cq_lock);
	kmem_free(scu_ctlp->scu_cq_thread_list,
	    sizeof (scu_cq_thread_t) * scu_ctlp->scu_cq_thread_num);
}

static void
scu_fm_init(scu_ctl_t *scu_ctlp)
{
	ddi_iblock_cookie_t	fm_ibc;

	scu_ctlp->scu_fm_cap = ddi_getprop(DDI_DEV_T_ANY,
	    scu_ctlp->scu_dip,
	    DDI_PROP_CANSLEEP | DDI_PROP_DONTPASS, "fm-capable",
	    DDI_FM_EREPORT_CAPABLE | DDI_FM_ACCCHK_CAPABLE |
	    DDI_FM_DMACHK_CAPABLE | DDI_FM_ERRCB_CAPABLE);

	/* Only register with IO Fault Services if we have some capability */
	if (scu_ctlp->scu_fm_cap) {
		scu_ctlp->reg_acc_attr.devacc_attr_access = DDI_FLAGERR_ACC;
		scu_data_dma_attr.dma_attr_flags |= DDI_DMA_FLAGERR;
		scu_memory_descriptor_dma_attr.
		    dma_attr_flags |= DDI_DMA_FLAGERR;
		scu_io_request_dma_attr.dma_attr_flags |= DDI_DMA_FLAGERR;

		/*
		 * Register capabilities with IO Fault Services.
		 */
		ddi_fm_init(scu_ctlp->scu_dip,
		    &scu_ctlp->scu_fm_cap, &fm_ibc);
		if (scu_ctlp->scu_fm_cap == DDI_FM_NOT_CAPABLE) {
			cmn_err(CE_WARN, "!scu%d: fma init failed.",
			    ddi_get_instance(scu_ctlp->scu_dip));
			return;
		}
		/*
		 * Initialize pci ereport capabilities if ereport
		 * capable (should always be.)
		 */
		if (DDI_FM_EREPORT_CAP(scu_ctlp->scu_fm_cap) ||
		    DDI_FM_ERRCB_CAP(scu_ctlp->scu_fm_cap)) {
			pci_ereport_setup(scu_ctlp->scu_dip);
		}
		/*
		 * Register error callback if error callback capable.
		 */
		if (DDI_FM_ERRCB_CAP(scu_ctlp->scu_fm_cap)) {
			ddi_fm_handler_register(scu_ctlp->scu_dip,
			    scu_fm_error_cb, (void *) scu_ctlp);
		}
	}
}

static void
scu_fm_fini(scu_ctl_t *scu_ctlp)
{
	/* Only unregister FMA capabilities if registered */
	if (scu_ctlp->scu_fm_cap) {
		/*
		 * Un-register error callback if error callback capable.
		 */
		if (DDI_FM_ERRCB_CAP(scu_ctlp->scu_fm_cap)) {
			ddi_fm_handler_unregister(scu_ctlp->scu_dip);
		}

		/*
		 * Release any resources allocated by pci_ereport_setup()
		 */
		if (DDI_FM_EREPORT_CAP(scu_ctlp->scu_fm_cap) ||
		    DDI_FM_ERRCB_CAP(scu_ctlp->scu_fm_cap)) {
			pci_ereport_teardown(scu_ctlp->scu_dip);
		}

		/* Unregister from IO Fault Services */
		ddi_fm_fini(scu_ctlp->scu_dip);

		scu_ctlp->reg_acc_attr.devacc_attr_access = DDI_DEFAULT_ACC;
		scu_data_dma_attr.dma_attr_flags &= ~DDI_DMA_FLAGERR;
		scu_memory_descriptor_dma_attr.
		    dma_attr_flags &= ~DDI_DMA_FLAGERR;
		scu_io_request_dma_attr.dma_attr_flags &= ~DDI_DMA_FLAGERR;
	}
}

/*ARGSUSED*/
static int
scu_fm_error_cb(dev_info_t *dip, ddi_fm_error_t *err, const void *impl_data)
{
	/*
	 * as the driver can always deal with an error in any dma or
	 * access handle, we can just return the fme_status value.
	 */
	pci_ereport_post(dip, err, NULL);
	return (err->fme_status);
}

/* PRINTFLIKE3 */
void
scu_log(scu_ctl_t *scu_ctlp, int level, char *fmt, ...)
{
	dev_info_t	*dip = NULL;
	va_list		ap;

	if (scu_ctlp) {
		dip = scu_ctlp->scu_dip;
	}

	mutex_enter(&scu_log_lock);

	va_start(ap, fmt);
	(void) vsprintf(scu_log_buf, fmt, ap);
	va_end(ap);

	if (level == CE_NOTE) {
		scsi_log(dip, "scu", level, "%s\n", scu_log_buf);
	} else {
		scsi_log(dip, "scu", level, "%s", scu_log_buf);
	}

	mutex_exit(&scu_log_lock);
}

#ifdef	SCU_DEBUG
/* PRINTFLIKE3 */
void
scu_prt(scu_ctl_t *scu_ctlp, uint8_t level, char *fmt, ...)
{
	dev_info_t	*dip = NULL;
	va_list		ap;
	int		system_level;

	if (scu_ctlp) {
		dip = scu_ctlp->scu_dip;
	}

	mutex_enter(&scu_log_lock);

	va_start(ap, fmt);
	(void) vsprintf(scu_log_buf, fmt, ap);
	va_end(ap);

	if (level & SCUDBG_ERROR) {
		system_level = CE_WARN;
	} else if (level & SCUDBG_WARNING) {
		system_level = CE_NOTE;
	} else {
		system_level = CE_CONT;
	}

	scsi_log(dip, "scu", system_level, "%s", scu_log_buf);

	mutex_exit(&scu_log_lock);
}
#endif

static int
scu_outstanding_cmds(scu_ctl_t *scu_ctlp)
{
	scu_subctl_t	*scu_subctlp;
	int		j;

	/* Check whether active commands got timeout */
	for (j = 0, scu_subctlp = scu_ctlp->scu_subctls;
	    j < scu_ctlp->scu_lib_ctl_num;
	    j++, scu_subctlp++) {

		mutex_enter(&scu_subctlp->scus_slot_lock);
		if (scu_subctlp->scus_slot_active_num) {
			mutex_exit(&scu_subctlp->scus_slot_lock);
			return (TRUE);
		}
		mutex_exit(&scu_subctlp->scus_slot_lock);
	}
	return (FALSE);
}

/*
 * resume routine, it will be run when get the command
 * DDI_RESUME at attach(9E) from system power management
 */
static int
scu_resume(dev_info_t *dip)
{
	scu_ctl_t	*scu_ctlp;
	int		instance, i, ctl_count;
	int		adapter_started;
	scu_tgt_t	*scu_tgtp = NULL;
	SCI_REMOTE_DEVICE_HANDLE_T	scif_device;

	/* No DDI_RESUME on iport nodes */
	if (scsi_hba_iport_unit_address(dip) != NULL) {
		return (DDI_SUCCESS);
	}

	instance = ddi_get_instance(dip);
	scu_ctlp = ddi_get_soft_state(scu_softc_state, instance);
	if (scu_ctlp == NULL) {
		cmn_err(CE_WARN, "!scu%d: cannot get soft state", instance);
		return (DDI_FAILURE);
	}

	ctl_count = scu_ctlp->scu_lib_ctl_num;

	/*
	 * Initialize the SCIF controller
	 */
	for (i = 0; i < ctl_count; i++) {
		if (scif_controller_initialize(
		    scu_ctlp->scu_subctls[i].scus_scif_ctl_handle) !=
		    SCI_SUCCESS) {
			scu_log(scu_ctlp, CE_WARN,
			    "!cannot initialize SCIF controller index %d "
			    "during DDI_RESUME", i);
			return (DDI_FAILURE);
		}
	}

	/* Disable all interrupts */
	mutex_enter(&scu_ctlp->scu_lock);
	for (i = 0; i < ctl_count; i++) {
		scic_controller_disable_interrupts(
		    scu_ctlp->scu_subctls[i].scus_scic_ctl_handle);
	}

	mutex_exit(&scu_ctlp->scu_lock);

	/*
	 * Start the SCIF controller
	 */
	for (i = 0; i < ctl_count; i++) {
		mutex_enter(&scu_ctlp->scu_lock);
		scu_ctlp->scu_subctls[i].scus_adapter_is_ready = 0;
		mutex_exit(&scu_ctlp->scu_lock);

		if (scif_controller_start(
		    scu_ctlp->scu_subctls[i].scus_scif_ctl_handle,
		    scif_controller_get_suggested_start_timeout(
		    scu_ctlp->scu_subctls[i].scus_scif_ctl_handle))
		    != SCI_SUCCESS) {
			scu_log(scu_ctlp, CE_WARN,
			    "!failed to start SCIF controller for "
			    "sub-controller %d during DDI_RESUME", i);

			return (DDI_FAILURE);
		}
	}

	/* Poll on interrupt then invoke controller_stop callback */
	do
	{
		(void) scu_poll_intr(scu_ctlp);
		drv_usecwait(1000); /* 1 millisecond */

		adapter_started = 1;
		for (i = 0; i < ctl_count; i++) {
			if (scu_ctlp->
			    scu_subctls[i].scus_adapter_is_ready == 0)
			adapter_started = 0;
		}
	} while (adapter_started == 0);

	/*
	 * Enable the interrupt
	 */
	mutex_enter(&scu_ctlp->scu_lock);
	for (i = 0; i < ctl_count; i++) {
		scic_controller_enable_interrupts(
		    scu_ctlp->scu_subctls[i].scus_scic_ctl_handle);
	}
	scu_ctlp->scu_started = 1;
	scu_ctlp->scu_is_suspended = 0;

	/* Recover the associate between driver target and SCIF target */
	for (i = 0; i < scu_ctlp->scu_max_dev; i++) {
		scu_tgtp = scu_ctlp->scu_tgts[i];
		if (scu_tgtp == NULL)
			continue;
		scif_device = scu_tgtp->scut_lib_remote_device;
		ASSERT(scif_device != NULL);

		(void) sci_object_set_association(scif_device, scu_tgtp);
	}

	mutex_exit(&scu_ctlp->scu_lock);

	/* Restart watch thread */
	mutex_enter(&scu_ctlp->scu_lock);
	if (scu_ctlp->scu_watchdog_timeid == 0) {
		scu_watch_interval =
		    scu_watch_tick * drv_usectohz((clock_t)1000000);
		scu_ctlp->scu_watchdog_timeid = timeout(scu_watchdog_handler,
		    scu_ctlp, scu_watch_interval);
	}
	mutex_exit(&scu_ctlp->scu_lock);
	return (DDI_SUCCESS);

}

/*
 * suspend routine, it will be run when get the command
 * DDI_SUSPEND at detach(9E) from system power management
 */
static int
scu_suspend(dev_info_t *dip)
{
	scu_ctl_t	*scu_ctlp;
	int		instance, i, ctl_count;
	SCI_STATUS	sci_status;

	instance = ddi_get_instance(dip);

	/* No DDI_RESUME on iport nodes */
	if (scsi_hba_iport_unit_address(dip) != NULL) {
		return (DDI_SUCCESS);
	} else {
		/* It's HBA node */
		scu_ctlp = ddi_get_soft_state(scu_softc_state, instance);
		if (scu_ctlp == NULL)
			return (DDI_FAILURE);
	}
	ctl_count = scu_ctlp->scu_lib_ctl_num;

	mutex_enter(&scu_ctlp->scu_lock);

	if (scu_ctlp->scu_is_suspended) {
		mutex_exit(&scu_ctlp->scu_lock);
		return (DDI_SUCCESS);
	}

	while (scu_outstanding_cmds(scu_ctlp)) {
		(void) drv_usecwait(1000000);
	}

	/* Disable all interrupts */
	for (i = 0; i < ctl_count; i++) {
		scic_controller_disable_interrupts(
		    scu_ctlp->scu_subctls[i].scus_scic_ctl_handle);
	}

	/* Stop timeout threads */
	if (scu_ctlp->scu_quiesce_timeid) {
		timeout_id_t tid = scu_ctlp->scu_quiesce_timeid;
		scu_ctlp->scu_quiesce_timeid = 0;
		mutex_exit(&scu_ctlp->scu_lock);
		(void) untimeout(tid);
		mutex_enter(&scu_ctlp->scu_lock);
	}

	if (scu_ctlp->scu_watchdog_timeid) {
		timeout_id_t tid = scu_ctlp->scu_watchdog_timeid;
		scu_ctlp->scu_watchdog_timeid = 0;
		mutex_exit(&scu_ctlp->scu_lock);
		(void) untimeout(tid);
		mutex_enter(&scu_ctlp->scu_lock);
	}

	/* Drain taskq */
	for (i = 0; i < ctl_count; i++) {
		if (scu_ctlp->scu_subctls[i].scus_lib_internal_taskq)
		ddi_taskq_wait(
		    scu_ctlp->scu_subctls[i].scus_lib_internal_taskq);
	}

	if (scu_ctlp->scu_discover_taskq) {
		ddi_taskq_wait(scu_ctlp->scu_discover_taskq);
	}
	mutex_exit(&scu_ctlp->scu_lock);

	/* Reset the controller */
	for (i = 0; i < ctl_count; i++) {
		sci_status = scif_controller_reset(
		    scu_ctlp->scu_subctls[i].scus_scif_ctl_handle);
		if ((sci_status != SCI_SUCCESS) &&
		    (sci_status != SCI_WARNING_ALREADY_IN_STATE)) {
			return (DDI_FAILURE);
		}
	}
	mutex_enter(&scu_ctlp->scu_lock);
	scu_ctlp->scu_started = 0;
	scu_ctlp->scu_is_suspended = 1;
	mutex_exit(&scu_ctlp->scu_lock);
	return (DDI_SUCCESS);
}

static void
scu_iport_offline_subdevice(scu_iport_t *scu_iportp)
{
	dev_info_t *child;
	scu_ctl_t	*scu_ctlp;

	scu_ctlp = scu_iportp->scui_ctlp;
	child = ddi_get_child(scu_iportp->scui_dip);
	while (child) {
		if (ndi_dev_is_persistent_node(child) == 0) {
			continue;
		}
		if (ndi_devi_offline(child,
		    NDI_DEVFS_CLEAN | NDI_DEVI_REMOVE) == DDI_SUCCESS) {
			SCUDBG(scu_ctlp, SCUDBG_IPORT,
			    SCUDBG_ERROR|SCUDBG_TRACE,
			    "%s: offline iport`subdevice successfully",
			    __func__);
		} else if (!ndi_devi_device_remove(child)) {
			SCUDBG(scu_ctlp, SCUDBG_IPORT,
			    SCUDBG_ERROR|SCUDBG_TRACE,
			    "%s: failed to offline iport`subdevice",
			    __func__);
		}
		child = ddi_get_next_sibling(child);
	}
}
static void
scu_offline_subiport(scu_ctl_t *scu_ctlp)
{
	dev_info_t *child;

	child = ddi_get_child(scu_ctlp->scu_dip);
	while (child) {
		/*
		 * the iport is persistent node, use device_remove
		 */
		if (!ndi_devi_device_remove(child)) {
			SCUDBG(scu_ctlp, SCUDBG_IPORT,
			    SCUDBG_ERROR|SCUDBG_TRACE,
			    "%s: failed to offline iport",
			    __func__);
		}

		ddi_prop_remove_all(child);
		child = ddi_get_next_sibling(child);
	}
}
