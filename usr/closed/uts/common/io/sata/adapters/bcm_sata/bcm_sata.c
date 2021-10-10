/*
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This driver has been written from scratch by Sun Microsystems, Inc.
 * The hardware specification used to write this driver contains
 * confidential information and requires a NDA.
 */

/*
 *
 * bcm_sata is a SATA HBA driver for Broadcom ht1000 based chipset.
 *
 * Power Management Support
 * ------------------------
 *
 * At the moment, the bcm_sata driver only implements suspend/resume to
 * support Suspend to RAM on X86 feature. Device power management isn't
 * implemented and hot plug isn't allowed during the period from suspend
 * to resume.
 *
 * For s/r support, the bcm_sata driver only need to implement DDI_SUSPEND
 * and DDI_RESUME entries, and don't need to take care of new requests
 * sent down after suspend because the target driver (sd) has already
 * handled these conditions, and blocked these requests. For the detailed
 * information, please check with sdopen, sdclose and sdioctl routines.
 *
 * NCQ
 * ---
 *
 * Currently NCQ is not supported and is likely to be supported in the
 * future.
 *
 */

#include <sys/sdt.h>
#include <sys/scsi/scsi.h>
#include <sys/scsi/generic/commands.h>
#include <sys/pci.h>
#include <sys/sata/sata_hba.h>
#include <sys/sata/adapters/bcm_sata/bcm_satareg.h>
#include <sys/sata/adapters/bcm_sata/bcm_satavar.h>
#include <sys/disp.h>
#include <sys/note.h>

/*
 * Function prototypes for driver entry points
 */
static	int bcm_attach(dev_info_t *, ddi_attach_cmd_t);
static	int bcm_detach(dev_info_t *, ddi_detach_cmd_t);
static	int bcm_getinfo(dev_info_t *, ddi_info_cmd_t, void *, void **);
static	int bcm_quiesce(dev_info_t *);

/*
 * Function prototypes for SATA Framework interfaces
 */
static	int bcm_sata_probe(dev_info_t *, sata_device_t *);
static	int bcm_sata_start(dev_info_t *, sata_pkt_t *spkt);
static	int bcm_sata_abort(dev_info_t *, sata_pkt_t *, int);
static	int bcm_sata_reset(dev_info_t *, sata_device_t *);
static	int bcm_sata_activate(dev_info_t *, sata_device_t *);
static	int bcm_sata_deactivate(dev_info_t *, sata_device_t *);

static	int bcm_register_sata_hba_tran(bcm_ctl_t *);
static	int bcm_unregister_sata_hba_tran(bcm_ctl_t *);

/*
 * Local function prototypes
 */
static	int bcm_alloc_ports_state(bcm_ctl_t *);
static	void bcm_dealloc_ports_state(bcm_ctl_t *);
static	int bcm_alloc_port_state(bcm_ctl_t *, uint8_t);
static	void bcm_dealloc_port_state(bcm_ctl_t *, uint8_t);
static	int bcm_alloc_cmd_descriptor_queue(bcm_ctl_t *, bcm_port_t *);
static	int bcm_alloc_prdts_qdma(bcm_ctl_t *, bcm_port_t *);
static	int bcm_alloc_atapi_cdb(bcm_ctl_t *, bcm_port_t *);
static	void bcm_dealloc_cmd_descriptor_queue(bcm_port_t *);
static	void bcm_dealloc_prdts_qdma(bcm_port_t *);
static	void bcm_dealloc_atapi_cdb(bcm_port_t *);
static	int bcm_init_ctl(bcm_ctl_t *);
static	void bcm_uninit_ctl(bcm_ctl_t *);
static	int bcm_init_port(bcm_ctl_t *, bcm_port_t *, uint8_t);
static	int bcm_config_space_init(bcm_ctl_t *);
static	int bcm_start_port(bcm_ctl_t *, bcm_port_t *, uint8_t);
static	void bcm_find_dev_signature(bcm_ctl_t *, bcm_port_t *, uint8_t);
static	int bcm_config_port_registers(bcm_ctl_t *, bcm_port_t *, uint8_t);
static	void bcm_update_sata_registers(bcm_ctl_t *, uint8_t, sata_device_t *);
static	int bcm_do_sync_start(bcm_ctl_t *, bcm_port_t *, uint8_t, sata_pkt_t *);
static	int bcm_do_async_start(bcm_ctl_t *, bcm_port_t *, uint8_t,
    sata_pkt_t *);
static	int bcm_deliver_sync_satapkt(bcm_ctl_t *, bcm_port_t *,
    uint8_t, sata_pkt_t *);
static	int bcm_deliver_async_satapkt(bcm_ctl_t *, bcm_port_t *,
    uint8_t, sata_pkt_t *);
static	int bcm_start_pio_in(bcm_ctl_t *, bcm_port_t *, uint8_t, sata_pkt_t *);
static	int bcm_start_pio_out(bcm_ctl_t *, bcm_port_t *, uint8_t, sata_pkt_t *);
static	int bcm_start_nodata(bcm_ctl_t *, bcm_port_t *, uint8_t, sata_pkt_t *);
static	int bcm_start_qdma_pkt_cmd(bcm_ctl_t *, bcm_port_t *, uint8_t,
    sata_pkt_t *);
static	int bcm_start_non_qdma_pkt_cmd(bcm_ctl_t *, bcm_port_t *, uint8_t,
    sata_pkt_t *);
static	int bcm_check_atapi_qdma(bcm_port_t *, sata_pkt_t *);
static	int bcm_start_rqsense_pkt_cmd(bcm_ctl_t *, bcm_port_t *, uint8_t,
    sata_pkt_t *);
static	int bcm_start_qdma_cmd(bcm_ctl_t *, bcm_port_t *, uint8_t,
    sata_pkt_t *);
static	void bcm_program_taskfile_regs(bcm_ctl_t *, int, sata_pkt_t *);
static	int bcm_claim_free_slot(bcm_ctl_t *, bcm_port_t *, int);
static	int bcm_port_reset(bcm_ctl_t *, bcm_port_t *, uint8_t);
static	void bcm_reject_all_abort_pkts(bcm_ctl_t *, bcm_port_t *, uint8_t);
static	int bcm_reset_port_reject_pkts(bcm_ctl_t *, bcm_port_t *, uint8_t);
static	int bcm_reset_hba_reject_pkts(bcm_ctl_t *);
static	int bcm_disable_port_QDMA_engine(bcm_ctl_t *, bcm_port_t *, uint8_t);
static	int bcm_enable_port_QDMA_engine(bcm_ctl_t *, bcm_port_t *, uint8_t);
static	int bcm_pause_port_QDMA_engine(bcm_ctl_t *, bcm_port_t *, uint8_t);
static	int bcm_unpause_port_QDMA_engine(bcm_ctl_t *, bcm_port_t *, uint8_t);
static	int bcm_restart_port_wait_till_ready(bcm_ctl_t *, bcm_port_t *,
    uint8_t, int, int *);
static	void bcm_mop_commands(bcm_ctl_t *, bcm_port_t *, int, int, int,
    int, int);
static	void bcm_fatal_error_recovery_handler(bcm_ctl_t *, bcm_port_t *,
    uint8_t, uint32_t);
static	void bcm_timeout_pkts(bcm_ctl_t *, bcm_port_t *, uint8_t, int);
static	void bcm_events_handler(void *);
static	void bcm_hotplug_events_handler(void *);
static	void bcm_watchdog_handler(bcm_ctl_t *);

static	uint_t bcm_intr(caddr_t, caddr_t);
static	void bcm_port_intr(bcm_ctl_t *, bcm_port_t *, uint8_t);
static	void bcm_intr_non_qdma_cmd(bcm_ctl_t *, bcm_port_t *, uint8_t);
static	int bcm_intr_qdma_cmd_cmplt(bcm_ctl_t *, bcm_port_t *, uint8_t);
static	int bcm_intr_qdma_pkt_cmd_cmplt(bcm_ctl_t *, bcm_port_t *, uint8_t);
static	void bcm_intr_non_qdma_pkt_cmd(bcm_ctl_t *, bcm_port_t *, uint8_t);
static	void bcm_intr_pio_in(bcm_ctl_t *, bcm_port_t *, uint8_t);
static	void bcm_intr_pio_out(bcm_ctl_t *, bcm_port_t *, uint8_t);
static	void bcm_intr_nodata(bcm_ctl_t *, bcm_port_t *, uint8_t);
static	int bcm_add_legacy_intrs(bcm_ctl_t *);
static	int bcm_add_msi_intrs(bcm_ctl_t *);
static	void bcm_rem_intrs(bcm_ctl_t *);
static	void bcm_enable_all_intrs(bcm_ctl_t *);
static	void bcm_disable_all_intrs(bcm_ctl_t *);
static	void bcm_enable_port_intrs(bcm_ctl_t *, uint8_t);
static	void bcm_disable_port_intrs(bcm_ctl_t *, uint8_t);
static	int bcm_intr_phyrdy_change(bcm_ctl_t *, bcm_port_t *, uint8_t);
static  int bcm_intr_qsr_cmd_error(bcm_ctl_t *, bcm_port_t *, uint8_t,
    uint32_t);
static  int bcm_intr_qsr_pkt_cmd_error(bcm_ctl_t *, bcm_port_t *, uint8_t,
    uint32_t);
static  int bcm_intr_qsr_pci_error(bcm_ctl_t *, bcm_port_t *, uint8_t,
    uint32_t);
static	int bcm_intr_ser_error(bcm_ctl_t *, bcm_port_t *, uint8_t,
    uint32_t, uint32_t);
static  void bcm_log_cmd_error_message(bcm_ctl_t *, uint8_t port, uint32_t);
static  void bcm_log_pci_error_message(bcm_ctl_t *, uint8_t port, uint32_t);
static	void bcm_copy_err_cnxt(bcm_ctl_t *, bcm_port_t *, uint8_t);
static	int bcm_wait(bcm_ctl_t *, uint8_t, uchar_t, uchar_t, uint_t, int);
static	int bcm_wait3(bcm_ctl_t *, uint8_t, uchar_t, uchar_t,
    uchar_t, uchar_t, uchar_t, uchar_t, uint_t, int);
static	void bcm_log_serror_message(bcm_ctl_t *, uint8_t, uint32_t, int);

#if BCM_DEBUG
static	void bcm_log(bcm_ctl_t *, uint_t, char *, ...);
static	void bcm_dump_port_registers(bcm_ctl_t *, uint8_t);
static	void bcm_dump_global_registers(bcm_ctl_t *);
static	void bcm_dump_port_tf_registers(bcm_ctl_t *, uint8_t);
#endif

/*
 * DMA attributes for the data buffer for x86. HBA supports 47 bit addressing
 */
static ddi_dma_attr_t buffer_dma_attr = {
	DMA_ATTR_V0,		/* dma_attr_version */
	0,			/* dma_attr_addr_lo: lowest bus address */
	0x7fffffffffffull,	/* dma_attr_addr_hi: 47 bit addresses */
	BCM_BM_64K_BOUNDARY,	/* dma_attr_count_max: 64K */
	4,			/* dma_attr_align: 4 bytes */
	1,			/* dma_attr_burstsizes. */
	1,			/* dma_attr_minxfer */
	0x7fffffffffffull,	/* dma_attr_max xfer including all cookies */
	BCM_BM_64K_BOUNDARY,	/* dma_attr_seg */
	BCM_PRDT_NUMBER,	/* dma_attr_sgllen */
	512,			/* dma_attr_granular */
	0			/* dma_attr_flags */
};

/*
 * DMA attributes for command descriptor queue
 */
ddi_dma_attr_t cmd_queue_dma_attr = {
	DMA_ATTR_V0,			/* dma_attr_version */
	0,				/* dma_attr_addr_lo */
	0x7fffffffffffull,		/* dma_attr_addr_hi:47 bit addresses  */
	BCM_CMDQ_BUFFER_SIZE,		/* dma_attr_count_max: (16 * 32) */
	4,				/* dma_attr_align */
	1,				/* dma_attr_burstsizes */
	1,				/* dma_attr_minxfer */
	BCM_CMDQ_BUFFER_SIZE,		/* dma_attr_maxxfer: (16 * 32) */
	BCM_CMDQ_BUFFER_SIZE,		/* dma_attr_seg */
	1,				/* dma_attr_sgllen */
	1,				/* dma_attr_granular */
	0				/* dma_attr_flags */
};

/*
 * DMA attributes for PRD tables using QDMA
 */
ddi_dma_attr_t prd_qdma_dma_attr = {
	DMA_ATTR_V0,		/* dma_attr_version */
	0,			/* dma_attr_addr_lo */
	0xffffffffffffull,	/* dma_attr_addr_hi:48 bits */
	BCM_BM_64K_BOUNDARY,	/* dma_attr_count_max */
	4,			/* dma_attr_align */
	1,			/* dma_attr_burstsizes */
	1,			/* dma_attr_minxfer */
	BCM_BM_64K_BOUNDARY,	/* dma_attr_maxxfer */
	BCM_BM_64K_BOUNDARY,	/* dma_attr_seg */
	1,			/* dma_attr_sgllen */
	1,			/* dma_attr_granular */
	0			/* dma_attr_flags */
};

/*
 * DMA attributes for CDB blocks using QDMA
 */
ddi_dma_attr_t atapi_cdb_dma_attr = {
	DMA_ATTR_V0,		/* dma_attr_version */
	0,			/* dma_attr_addr_lo */
	0xffffffffull,		/* dma_attr_addr_hi:32 bits */
	SATA_ATAPI_MAX_CDB_LEN,	/* dma_attr_count_max */
	4,			/* dma_attr_align */
	1,			/* dma_attr_burstsizes */
	1,			/* dma_attr_minxfer */
	SATA_ATAPI_MAX_CDB_LEN,	/* dma_attr_maxxfer */
	SATA_ATAPI_MAX_CDB_LEN,	/* dma_attr_seg */
	1,			/* dma_attr_sgllen */
	1,			/* dma_attr_granular */
	0			/* dma_attr_flags */
};

/*
 * Device access attributes
 */
static ddi_device_acc_attr_t accattr = {
	DDI_DEVICE_ATTR_V0,
	DDI_STRUCTURE_LE_ACC,
	DDI_STRICTORDER_ACC
};


static struct dev_ops bcm_dev_ops = {
	DEVO_REV,		/* devo_rev */
	0,			/* refcnt  */
	bcm_getinfo,		/* info */
	nulldev,		/* identify */
	nulldev,		/* probe */
	bcm_attach,		/* attach */
	bcm_detach,		/* detach */
	nodev,			/* no reset */
	(struct cb_ops *)0,	/* driver operations */
	NULL,			/* bus operations */
	NULL,			/* power */
	bcm_quiesce		/* quiesce */
};

/*
 * Request Sense CDB for ATAPI
 */
static const uint8_t bcm_rqsense_cdb[16] = {
	SCMD_REQUEST_SENSE,
	0,
	0,
	0,
	SATA_ATAPI_MIN_RQSENSE_LEN,
	0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0	/* pad out to max CDB length */
};

static sata_tran_hotplug_ops_t bcm_hotplug_ops = {
	SATA_TRAN_HOTPLUG_OPS_REV_1,
	bcm_sata_activate,
	bcm_sata_deactivate
};

extern struct mod_ops mod_driverops;

static  struct modldrv modldrv = {
	&mod_driverops,		/* driverops */
	"Broadcom ht1000 HBA",
	&bcm_dev_ops,	/* driver ops */
};

static  struct modlinkage modlinkage = {
	MODREV_1,
	&modldrv,
	NULL
};

static int bcm_watchdog_tick;
int bcm_usec_delay = BCM_WAIT_REG_CHECK;

/*
 * BC tunables and can be changed via /etc/system file.
 */
/*
 * The number of Physical Region Descriptor Table(PRDT) in Command
 * Descriptor
 */
int bcm_dma_prdt_number = BCM_PRDT_NUMBER;

/*
 * MSI will be enabled in phase 2.
 */
boolean_t bcm_msi_enabled = B_FALSE;

#if BCM_DEBUG
/* uint32_t bcm_debug_flags = 0xffff; */
uint32_t bcm_debug_flags = 0x0;

/* The following is needed for bcm_log() */
static kmutex_t bcm_log_mutex;
static char bcm_log_buf[512];
#endif

/* Opaque state pointer initialized by ddi_soft_state_init() */
static void *bcm_statep = NULL;

/*
 *  bcm_sata module initialization.
 */
int
_init(void)
{
	int	ret;

	ret = ddi_soft_state_init(&bcm_statep, sizeof (bcm_ctl_t), 0);
	if (ret != 0) {
		goto err_out;
	}

#if BCM_DEBUG
	mutex_init(&bcm_log_mutex, NULL, MUTEX_DRIVER, NULL);
#endif

	if ((ret = sata_hba_init(&modlinkage)) != 0) {
#if BCM_DEBUG
		mutex_destroy(&bcm_log_mutex);
#endif
		ddi_soft_state_fini(&bcm_statep);
		goto err_out;
	}

	ret = mod_install(&modlinkage);
	if (ret != 0) {
		sata_hba_fini(&modlinkage);
#if BCM_DEBUG
		mutex_destroy(&bcm_log_mutex);
#endif
		ddi_soft_state_fini(&bcm_statep);
		goto err_out;
	}

	/* watchdog tick */
	bcm_watchdog_tick = drv_usectohz(
	    (clock_t)BCM_WATCHDOG_TIMEOUT * 1000000);
	return (ret);

err_out:
	cmn_err(CE_WARN, "!bcm: Module init failed");
	return (ret);
}

/*
 * bcm_sata module uninitialize.
 */
int
_fini(void)
{
	int	ret;

	ret = mod_remove(&modlinkage);
	if (ret != 0) {
		return (ret);
	}

	/* Remove the resources allocated in _init(). */
#if BCM_DEBUG
	mutex_destroy(&bcm_log_mutex);
#endif
	sata_hba_fini(&modlinkage);
	ddi_soft_state_fini(&bcm_statep);

	return (ret);
}

/*
 * bcm_sata _info entry point
 */
int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

/*
 * The attach entry point for dev_ops.
 */
static int
bcm_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	bcm_ctl_t *bcm_ctlp;
	int instance = ddi_get_instance(dip);
	int status;
	int attach_state;
	int intr_types;
	pci_regspec_t *regs;
	int regs_length;
	int rnumber;

	BCMDBG0(BCMDBG_INIT|BCMDBG_ENTRY, NULL, "bcm_attach enter");

	switch (cmd) {
	case DDI_ATTACH:
		break;

	case DDI_RESUME:

		/*
		 * During DDI_RESUME, the hardware state of the device
		 * (power may have been removed from the device) must be
		 * restored, allow pending requests to continue, and
		 * service new requests.
		 */
		bcm_ctlp = ddi_get_soft_state(bcm_statep, instance);
		mutex_enter(&bcm_ctlp->bcmc_mutex);

		/* Restart watch thread */
		if (bcm_ctlp->bcmc_timeout_id == 0)
			bcm_ctlp->bcmc_timeout_id = timeout(
			    (void (*)(void *))bcm_watchdog_handler,
			    (caddr_t)bcm_ctlp, bcm_watchdog_tick);

		mutex_exit(&bcm_ctlp->bcmc_mutex);

		/*
		 * Re-initialize the controller and enable the interrupts and
		 * restart all the ports.
		 *
		 * Note that so far we don't support hot-plug during
		 * suspend/resume.
		 */
		if (bcm_init_ctl(bcm_ctlp) != BCM_SUCCESS) {
			BCMDBG0(BCMDBG_ERRS|BCMDBG_PM, bcm_ctlp,
			    "Failed to initialize the controller "
			    "during DDI_RESUME");
			return (DDI_FAILURE);
		}

		mutex_enter(&bcm_ctlp->bcmc_mutex);
		bcm_ctlp->bcmc_flags &= ~ BCM_SUSPEND;
		mutex_exit(&bcm_ctlp->bcmc_mutex);

		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);
	}

	attach_state = BCM_ATTACH_STATE_NONE;

	/* Allocate soft state */
	status = ddi_soft_state_zalloc(bcm_statep, instance);
	if (status != DDI_SUCCESS) {
		cmn_err(CE_WARN, "!bcm%d: Cannot allocate soft state",
		    instance);
		goto err_out;
	}

	bcm_ctlp = ddi_get_soft_state(bcm_statep, instance);
	bcm_ctlp->bcmc_flags |= BCM_ATTACH;
	bcm_ctlp->bcmc_dip = dip;

	attach_state |= BCM_ATTACH_STATE_STATEP_ALLOC;

	/*
	 * Now map the ht1000 base address; which includes global
	 * registers and port control registers
	 *
	 * According to the spec, the ht1000 Base Address is BAR5,
	 * but BAR0-BAR4 are optional, so we need to check which
	 * rnumber is used for BAR5.
	 */

	/*
	 * search through DDI "reg" property for the ht1000 register set
	 */
	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, "reg", (int **)&regs,
	    (uint_t *)&regs_length) != DDI_PROP_SUCCESS) {
		cmn_err(CE_WARN, "!bcm%d: Cannot lookup reg property",
		    instance);
		goto err_out;
	}

	/* ht1000 Base Address is located at 0x24 offset */
	for (rnumber = 0; rnumber < regs_length; ++rnumber) {
		if ((regs[rnumber].pci_phys_hi & PCI_REG_REG_M)
		    == BCM_PCI_RNUM)
			break;
	}

	ddi_prop_free(regs);

	if (rnumber == regs_length) {
		cmn_err(CE_WARN, "!bcm%d: Cannot find ht1000 register set",
		    instance);
		goto err_out;
	}

	BCMDBG1(BCMDBG_INIT, bcm_ctlp, "rnumber = %d", rnumber);

	status = ddi_regs_map_setup(dip,
	    rnumber,
	    (caddr_t *)&bcm_ctlp->bcmc_bar_addr,
	    0,
	    0,
	    &accattr,
	    &bcm_ctlp->bcmc_bar_handle);
	if (status != DDI_SUCCESS) {
		cmn_err(CE_WARN, "!bcm%d: Cannot map register space",
		    instance);
		goto err_out;
	}

	attach_state |= BCM_ATTACH_STATE_REG_MAP;

	/*
	 * According to the spec, ht1000 supports a max of 4 ports.
	 */
	bcm_ctlp->bcmc_num_ports = BCM_NUM_CPORTS;

	BCMDBG1(BCMDBG_INIT, bcm_ctlp, "hba number of ports: %d",
	    bcm_ctlp->bcmc_num_ports);

	if (pci_config_setup(dip, &bcm_ctlp->bcmc_pci_conf_handle)
	    != DDI_SUCCESS) {
		cmn_err(CE_WARN, "!bcm%d: Cannot set up pci configure space",
		    instance);
		goto err_out;
	}

	/* Check the pci configuration space for right settings */
	if (bcm_config_space_init(bcm_ctlp) == BCM_FAILURE) {
		cmn_err(CE_WARN, "!bcm%d: bcm_config_space_init failed",
		    instance);
		goto err_out;
	}

	attach_state |= BCM_ATTACH_STATE_PCICFG_SETUP;

	/*
	 * Disable the whole controller interrupts before adding
	 * interrupt handlers(s).
	 */
	bcm_disable_all_intrs(bcm_ctlp);

	/* Get supported interrupt types */
	if (ddi_intr_get_supported_types(dip, &intr_types) != DDI_SUCCESS) {
		cmn_err(CE_WARN, "!bcm%d: ddi_intr_get_supported_types failed",
		    instance);
		goto err_out;
	}

	BCMDBG1(BCMDBG_INIT|BCMDBG_INTR, bcm_ctlp,
	    "ddi_intr_get_supported_types() returned: 0x%x",
	    intr_types);

	if (bcm_msi_enabled && (intr_types & DDI_INTR_TYPE_MSI)) {
		/*
		 * Try MSI first, but fall back to FIXED if failed
		 */
		if (bcm_add_msi_intrs(bcm_ctlp) == DDI_SUCCESS) {
			bcm_ctlp->bcmc_intr_type = DDI_INTR_TYPE_MSI;
			BCMDBG0(BCMDBG_INIT|BCMDBG_INTR, bcm_ctlp,
			    "Using MSI interrupt type");
			goto intr_done;
		}

		BCMDBG0(BCMDBG_INIT|BCMDBG_INTR, bcm_ctlp,
		    "MSI registration failed, "
		    "trying FIXED interrupts");
	}

	if (intr_types & DDI_INTR_TYPE_FIXED) {
		if (bcm_add_legacy_intrs(bcm_ctlp) == DDI_SUCCESS) {
			bcm_ctlp->bcmc_intr_type = DDI_INTR_TYPE_FIXED;
			BCMDBG0(BCMDBG_INIT|BCMDBG_INTR, bcm_ctlp,
			    "Using FIXED interrupt type");
			goto intr_done;
		}

		BCMDBG0(BCMDBG_INIT|BCMDBG_INTR, bcm_ctlp,
		    "FIXED interrupt registration failed");
	}

	cmn_err(CE_WARN, "!bcm%d: Interrupt registration failed", instance);

	goto err_out;

intr_done:

	attach_state |= BCM_ATTACH_STATE_INTR_ADDED;

	/* Initialize the controller mutex */
	mutex_init(&bcm_ctlp->bcmc_mutex, NULL, MUTEX_DRIVER,
	    (void *)(uintptr_t)bcm_ctlp->bcmc_intr_pri);

	attach_state |= BCM_ATTACH_STATE_MUTEX_INIT;

	bcm_ctlp->bcmc_buffer_dma_attr = buffer_dma_attr;
	bcm_ctlp->bcmc_cmd_queue_dma_attr = cmd_queue_dma_attr;
	bcm_ctlp->bcmc_prdt_qdma_dma_attr = prd_qdma_dma_attr;
	bcm_ctlp->bcmc_atapi_cdb_dma_attr = atapi_cdb_dma_attr;

	/* Allocate the ports structure */
	status = bcm_alloc_ports_state(bcm_ctlp);
	if (status != BCM_SUCCESS) {
		cmn_err(CE_WARN, "!bcm%d: Cannot allocate ports structure",
		    instance);
		goto err_out;
	}

	attach_state |= BCM_ATTACH_STATE_PORT_ALLOC;

	/*
	 * A taskq is created for dealing with events
	 */
	if ((bcm_ctlp->bcmc_event_taskq = ddi_taskq_create(dip,
	    "bcm_event_handle_taskq", 1, TASKQ_DEFAULTPRI, 0)) == NULL) {
		cmn_err(CE_WARN, "!bcm%d: ddi_taskq_create failed for event "
		    "handle", instance);
		goto err_out;
	}

	attach_state |= BCM_ATTACH_STATE_ERR_RECV_TASKQ;

	/*
	 * Initialize the controller and ports.
	 */
	status = bcm_init_ctl(bcm_ctlp);
	if (status != BCM_SUCCESS) {
		cmn_err(CE_WARN, "!bcm%d: HBA initialization failed",
		    instance);
		goto err_out;
	}

	attach_state |= BCM_ATTACH_STATE_HW_INIT;

	/* Start one thread to check packet timeouts */
	bcm_ctlp->bcmc_timeout_id = timeout(
	    (void (*)(void *))bcm_watchdog_handler,
	    (caddr_t)bcm_ctlp, bcm_watchdog_tick);

	attach_state |= BCM_ATTACH_STATE_TIMEOUT_ENABLED;

	if (bcm_register_sata_hba_tran(bcm_ctlp)) {
		cmn_err(CE_WARN, "!bcm%d: sata hba tran registration failed",
		    instance);
		goto err_out;
	}

	bcm_ctlp->bcmc_flags &= ~BCM_ATTACH;

	BCMDBG0(BCMDBG_INIT, bcm_ctlp, "bcm_attach success!");

	return (DDI_SUCCESS);

err_out:
	if (attach_state & BCM_ATTACH_STATE_TIMEOUT_ENABLED) {
		mutex_enter(&bcm_ctlp->bcmc_mutex);
		(void) untimeout(bcm_ctlp->bcmc_timeout_id);
		bcm_ctlp->bcmc_timeout_id = 0;
		mutex_exit(&bcm_ctlp->bcmc_mutex);
	}

	if (attach_state & BCM_ATTACH_STATE_HW_INIT) {
		bcm_uninit_ctl(bcm_ctlp);
	}

	if (attach_state & BCM_ATTACH_STATE_ERR_RECV_TASKQ) {
		ddi_taskq_destroy(bcm_ctlp->bcmc_event_taskq);
	}

	if (attach_state & BCM_ATTACH_STATE_PORT_ALLOC) {
		bcm_dealloc_ports_state(bcm_ctlp);
	}

	if (attach_state & BCM_ATTACH_STATE_MUTEX_INIT) {
		mutex_destroy(&bcm_ctlp->bcmc_mutex);
	}

	if (attach_state & BCM_ATTACH_STATE_INTR_ADDED) {
		bcm_rem_intrs(bcm_ctlp);
	}

	if (attach_state & BCM_ATTACH_STATE_PCICFG_SETUP) {
		pci_config_teardown(&bcm_ctlp->bcmc_pci_conf_handle);
	}

	if (attach_state & BCM_ATTACH_STATE_REG_MAP) {
		ddi_regs_map_free(&bcm_ctlp->bcmc_bar_handle);
	}

	if (attach_state & BCM_ATTACH_STATE_STATEP_ALLOC) {
		ddi_soft_state_free(bcm_statep, instance);
	}

	return (DDI_FAILURE);
}

/*
 * The detach entry point for dev_ops.
 */
static int
bcm_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	bcm_ctl_t *bcm_ctlp;
	int instance;
	int ret;

	instance = ddi_get_instance(dip);
	bcm_ctlp = ddi_get_soft_state(bcm_statep, instance);

	BCMDBG0(BCMDBG_ENTRY, bcm_ctlp, "bcm_detach enter");

	switch (cmd) {
	case DDI_DETACH:

		/* disable the interrupts for an uninterrupted detach */
		mutex_enter(&bcm_ctlp->bcmc_mutex);
		bcm_disable_all_intrs(bcm_ctlp);
		mutex_exit(&bcm_ctlp->bcmc_mutex);

		/* unregister from the sata framework. */
		ret = bcm_unregister_sata_hba_tran(bcm_ctlp);
		if (ret != BCM_SUCCESS) {
			mutex_enter(&bcm_ctlp->bcmc_mutex);
			bcm_enable_all_intrs(bcm_ctlp);
			mutex_exit(&bcm_ctlp->bcmc_mutex);
			return (DDI_FAILURE);
		}

		mutex_enter(&bcm_ctlp->bcmc_mutex);

		/* stop the watchdog handler */
		(void) untimeout(bcm_ctlp->bcmc_timeout_id);
		bcm_ctlp->bcmc_timeout_id = 0;

		mutex_exit(&bcm_ctlp->bcmc_mutex);

		/* uninitialize the controller */
		bcm_uninit_ctl(bcm_ctlp);

		/* remove the interrupts */
		bcm_rem_intrs(bcm_ctlp);

		/* destroy the taskq */
		ddi_taskq_destroy(bcm_ctlp->bcmc_event_taskq);

		/* deallocate the ports structures */
		bcm_dealloc_ports_state(bcm_ctlp);

		/* destroy mutex */
		mutex_destroy(&bcm_ctlp->bcmc_mutex);

		/* teardown the pci config */
		pci_config_teardown(&bcm_ctlp->bcmc_pci_conf_handle);

		/* remove the reg maps. */
		ddi_regs_map_free(&bcm_ctlp->bcmc_bar_handle);

		/* free the soft state. */
		ddi_soft_state_free(bcm_statep, instance);

		return (DDI_SUCCESS);

	case DDI_SUSPEND:

		/*
		 * The steps associated with suspension must include putting
		 * the underlying device into a quiescent state so that it
		 * will not generate interrupts or modify or access memory.
		 */
		mutex_enter(&bcm_ctlp->bcmc_mutex);
		if (bcm_ctlp->bcmc_flags & BCM_SUSPEND) {
			mutex_exit(&bcm_ctlp->bcmc_mutex);
			return (DDI_SUCCESS);
		}

		bcm_ctlp->bcmc_flags |= BCM_SUSPEND;

		/* stop the watchdog handler */
		if (bcm_ctlp->bcmc_timeout_id) {
			(void) untimeout(bcm_ctlp->bcmc_timeout_id);
			bcm_ctlp->bcmc_timeout_id = 0;
		}

		mutex_exit(&bcm_ctlp->bcmc_mutex);

		/*
		 * drain the taskq
		 */
		ddi_taskq_wait(bcm_ctlp->bcmc_event_taskq);

		/*
		 * Disable the interrupts and stop all the ports.
		 */
		bcm_uninit_ctl(bcm_ctlp);

		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);
	}
}

/*
 * The info entry point for dev_ops.
 *
 */
static int
bcm_getinfo(dev_info_t *dip, ddi_info_cmd_t infocmd,
		    void *arg, void **result)
{
#ifndef __lock_lint
	_NOTE(ARGUNUSED(dip))
#endif /* __lock_lint */

	bcm_ctl_t *bcm_ctlp;
	int instance;
	dev_t dev;

	dev = (dev_t)arg;
	instance = getminor(dev);

	switch (infocmd) {
		case DDI_INFO_DEVT2DEVINFO:
			bcm_ctlp = ddi_get_soft_state(bcm_statep,  instance);
			if (bcm_ctlp != NULL) {
				*result = bcm_ctlp->bcmc_dip;
				return (DDI_SUCCESS);
			} else {
				*result = NULL;
				return (DDI_FAILURE);
			}
		case DDI_INFO_DEVT2INSTANCE:
			*(int *)result = instance;
			break;
		default:
			break;
	}

	return (DDI_SUCCESS);
}

/*
 * Registers the bcm_sata with sata framework.
 */
static int
bcm_register_sata_hba_tran(bcm_ctl_t *bcm_ctlp)
{
	struct 	sata_hba_tran	*sata_hba_tran;

	BCMDBG0(BCMDBG_INIT|BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_register_sata_hba_tran enter");

	mutex_enter(&bcm_ctlp->bcmc_mutex);

	/* Allocate memory for the sata_hba_tran  */
	sata_hba_tran = kmem_zalloc(sizeof (sata_hba_tran_t), KM_SLEEP);

	sata_hba_tran->sata_tran_hba_rev = SATA_TRAN_HBA_REV_2;
	sata_hba_tran->sata_tran_hba_dip = bcm_ctlp->bcmc_dip;
	sata_hba_tran->sata_tran_hba_dma_attr =
	    &bcm_ctlp->bcmc_buffer_dma_attr;
	sata_hba_tran->sata_tran_hba_num_cports =
	    bcm_ctlp->bcmc_num_ports;
	sata_hba_tran->sata_tran_hba_features_support =
	    SATA_CTLF_HOTPLUG | SATA_CTLF_ASN | SATA_CTLF_ATAPI;

	sata_hba_tran->sata_tran_hba_qdepth = BCM_CTL_QUEUE_DEPTH - 1;

	sata_hba_tran->sata_tran_probe_port = bcm_sata_probe;
	sata_hba_tran->sata_tran_start = bcm_sata_start;
	sata_hba_tran->sata_tran_abort = bcm_sata_abort;
	sata_hba_tran->sata_tran_reset_dport = bcm_sata_reset;
	sata_hba_tran->sata_tran_hotplug_ops = &bcm_hotplug_ops;

	/*
	 * When SATA framework adds support for pwrmgt the
	 * pwrmgt_ops needs to be updated
	 */
	sata_hba_tran->sata_tran_pwrmgt_ops = NULL;

	sata_hba_tran->sata_tran_ioctl = NULL;
	sata_hba_tran->sata_tran_selftest = NULL;

	bcm_ctlp->bcmc_sata_hba_tran = sata_hba_tran;

	mutex_exit(&bcm_ctlp->bcmc_mutex);

	/* Attach it to SATA framework */
	if (sata_hba_attach(bcm_ctlp->bcmc_dip, sata_hba_tran, DDI_ATTACH)
	    != DDI_SUCCESS) {
		kmem_free((void *)sata_hba_tran, sizeof (sata_hba_tran_t));
		mutex_enter(&bcm_ctlp->bcmc_mutex);
		bcm_ctlp->bcmc_sata_hba_tran = NULL;
		mutex_exit(&bcm_ctlp->bcmc_mutex);
		return (BCM_FAILURE);
	}

	return (BCM_SUCCESS);
}

/*
 * Unregisters the bcm_sata with sata framework.
 */
static int
bcm_unregister_sata_hba_tran(bcm_ctl_t *bcm_ctlp)
{
	BCMDBG0(BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_unregister_sata_hba_tran enter");

	/* Detach from the SATA framework. */
	if (sata_hba_detach(bcm_ctlp->bcmc_dip, DDI_DETACH) !=
	    DDI_SUCCESS) {
		return (BCM_FAILURE);
	}

	/* Deallocate sata_hba_tran. */
	kmem_free((void *)bcm_ctlp->bcmc_sata_hba_tran,
	    sizeof (sata_hba_tran_t));

	mutex_enter(&bcm_ctlp->bcmc_mutex);
	bcm_ctlp->bcmc_sata_hba_tran = NULL;
	mutex_exit(&bcm_ctlp->bcmc_mutex);

	return (BCM_SUCCESS);
}

/*
 * bcm_sata_probe is called by SATA framework. It returns port state,
 * port status registers and an attached device type via sata_device
 * structure.
 *
 * We return the cached information from a previous hardware probe. The
 * actual hardware probing itself was done either from within
 * bcm_init_ctl() during the driver attach or from a phy ready change
 * interrupt handler.
 */
static int
bcm_sata_probe(dev_info_t *dip, sata_device_t *sd)
{
	bcm_ctl_t *bcm_ctlp;
	bcm_port_t *bcmp;
	uint8_t	cport = sd->satadev_addr.cport;
	uint8_t pmport = sd->satadev_addr.pmport;
	uint8_t qual = sd->satadev_addr.qual;
	uint8_t	device_type;
	uint32_t port_state;

	bcm_ctlp = ddi_get_soft_state(bcm_statep, ddi_get_instance(dip));

	BCMDBG3(BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_sata_probe enter: cport: %d, "
	    "pmport: %d, qual: %d", cport, pmport, qual);

	bcmp = bcm_ctlp->bcmc_ports[cport];

	mutex_enter(&bcmp->bcmp_mutex);

	port_state = bcmp->bcmp_state;
	switch (port_state) {

	case SATA_PSTATE_FAILED:
		sd->satadev_state = SATA_PSTATE_FAILED;
		BCMDBG1(BCMDBG_ERRS, bcm_ctlp,
		    "bcm_sata_probe: port %d PORT FAILED", cport);
		goto out;

	case SATA_PSTATE_SHUTDOWN:
		sd->satadev_state = SATA_PSTATE_SHUTDOWN;
		BCMDBG1(BCMDBG_ERRS, bcm_ctlp,
		    "bcm_sata_probe: port %d PORT SHUTDOWN", cport);
		goto out;

	case SATA_PSTATE_PWROFF:
		sd->satadev_state = SATA_PSTATE_PWROFF;
		BCMDBG1(BCMDBG_ERRS, bcm_ctlp,
		    "bcm_sata_probe: port %d PORT PWROFF", cport);
		goto out;

	case SATA_PSTATE_PWRON:
		sd->satadev_state = SATA_PSTATE_PWRON;
		BCMDBG1(BCMDBG_INFO, bcm_ctlp,
		    "bcm_sata_probe: port %d PORT PWRON", cport);
		break;

	default:
		sd->satadev_state = port_state;
		BCMDBG2(BCMDBG_INFO, bcm_ctlp,
		    "bcm_sata_probe: port %d PORT NORMAL %x",
		    cport, port_state);
		break;
	}

	device_type = bcmp->bcmp_device_type;

	switch (device_type) {

	case SATA_DTYPE_ATADISK:
		sd->satadev_type = SATA_DTYPE_ATADISK;
		BCMDBG1(BCMDBG_INFO, bcm_ctlp,
		    "bcm_sata_probe: port %d DISK found", cport);
		break;

	case SATA_DTYPE_ATAPI:
		sd->satadev_type = SATA_DTYPE_ATAPI;
		BCMDBG1(BCMDBG_INFO, bcm_ctlp,
		    "bcm_sata_probe: port %d ATAPI found", cport);
		break;

	case SATA_DTYPE_PMULT:
		/* don't support port monitor yet */
	case SATA_DTYPE_UNKNOWN:
		sd->satadev_type = SATA_DTYPE_UNKNOWN;
		BCMDBG1(BCMDBG_INFO, bcm_ctlp,
		    "bcm_sata_probe: port %d Unknown device found", cport);
		break;

	default:
		/* we don't support any other device types */
		sd->satadev_type = SATA_DTYPE_NONE;
		BCMDBG1(BCMDBG_INFO, bcm_ctlp,
		    "bcm_sata_probe: port %d No device found", cport);
		break;
	}

out:
	bcm_update_sata_registers(bcm_ctlp, cport, sd);
	mutex_exit(&bcmp->bcmp_mutex);

	return (SATA_SUCCESS);
}

/*
 * There are four operation modes in sata framework:
 * SATA_OPMODE_INTERRUPTS
 * SATA_OPMODE_POLLING
 * SATA_OPMODE_ASYNCH
 * SATA_OPMODE_SYNCH
 *
 * Their combined meanings as following:
 *
 * SATA_OPMODE_SYNCH
 * The command has to be completed before sata_tran_start functions returns.
 * Either interrupts or polling could be used - it's up to the driver.
 * Mode used currently for internal, sata-module initiated operations.
 *
 * SATA_OPMODE_SYNCH | SATA_OPMODE_INTERRUPTS
 * It is the same as the one above.
 *
 * SATA_OPMODE_SYNCH | SATA_OPMODE_POLLING
 * The command has to be completed before sata_tran_start function returns.
 * No interrupt used, polling only. This should be the mode used for scsi
 * packets with FLAG_NOINTR.
 *
 * SATA_OPMODE_ASYNCH | SATA_OPMODE_INTERRUPTS
 * The command may be queued (callback function specified). Interrupts could
 * be used. It's normal operation mode.
 */
/*
 * Called by sata framework to transport a sata packet down stream.
 */
static int
bcm_sata_start(dev_info_t *dip, sata_pkt_t *spkt)
{
	int rval;
	bcm_ctl_t *bcm_ctlp;
	bcm_port_t *bcmp;
	uint8_t	port = spkt->satapkt_device.satadev_addr.cport;

	bcm_ctlp = ddi_get_soft_state(bcm_statep, ddi_get_instance(dip));

	BCMDBG2(BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_sata_start enter: cport %d satapkt 0x%p",
	    port, (void *)spkt);

	bcmp = bcm_ctlp->bcmc_ports[port];

	mutex_enter(&bcmp->bcmp_mutex);

	if (bcmp->bcmp_state & SATA_PSTATE_FAILED |
	    bcmp->bcmp_state & SATA_PSTATE_SHUTDOWN |
	    bcmp->bcmp_state & SATA_PSTATE_PWROFF) {
		/*
		 * In case the target driver would send the packet before
		 * sata framework can have the opportunity to process those
		 * event reports.
		 */
		spkt->satapkt_reason = SATA_PKT_PORT_ERROR;
		spkt->satapkt_device.satadev_state =
		    bcmp->bcmp_state;
		bcm_update_sata_registers(bcm_ctlp, port,
		    &spkt->satapkt_device);
		BCMDBG1(BCMDBG_ERRS, bcm_ctlp,
		    "bcm_sata_start returning PORT_ERROR while "
		    "port in FAILED/SHUTDOWN/PWROFF state: "
		    "port: %d", port);
		mutex_exit(&bcmp->bcmp_mutex);
		return (SATA_TRAN_PORT_ERROR);
	}

	if (bcmp->bcmp_device_type == SATA_DTYPE_NONE) {
		/*
		 * bcm_intr_phyrdy_change() may have rendered it to
		 * SATA_DTYPE_NONE.
		 */
		spkt->satapkt_reason = SATA_PKT_PORT_ERROR;
		spkt->satapkt_device.satadev_type = SATA_DTYPE_NONE;
		spkt->satapkt_device.satadev_state = bcmp->bcmp_state;
		bcm_update_sata_registers(bcm_ctlp, port,
		    &spkt->satapkt_device);
		BCMDBG1(BCMDBG_ERRS, bcm_ctlp,
		    "bcm_sata_start returning PORT_ERROR while "
		    "no device attached: port: %d", port);
		mutex_exit(&bcmp->bcmp_mutex);
		return (SATA_TRAN_PORT_ERROR);
	}

	if (spkt->satapkt_device.satadev_type == SATA_DTYPE_PMULT) {
		BCMDBG1(BCMDBG_ERRS, bcm_ctlp,
		    "port multipliers not supported by controller: "
		    "port: %d", port);
		spkt->satapkt_reason = SATA_PKT_CMD_UNSUPPORTED;
		mutex_exit(&bcmp->bcmp_mutex);

		return (SATA_TRAN_CMD_UNSUPPORTED);
	}

	/*
	 * SATA HBA driver should remember that a device was reset and it
	 * is supposed to reject any packets which do not specify either
	 * SATA_IGNORE_DEV_RESET_STATE or SATA_CLEAR_DEV_RESET_STATE.
	 *
	 * This is to prevent a race condition when a device was arbitrarily
	 * reset by the HBA driver (and lost it's setting) and a target
	 * driver sending some commands to a device before the sata framework
	 * has a chance to restore the device setting (such as cache enable/
	 * disable or other resettable stuff).
	 */
	if (spkt->satapkt_cmd.satacmd_flags.sata_clear_dev_reset) {
		bcmp->bcmp_reset_in_progress = 0;
		BCMDBG1(BCMDBG_ERRS, bcm_ctlp,
		    "bcm_sata_start clearing the "
		    "reset_in_progress for port: %d", port);
	}

	if (bcmp->bcmp_reset_in_progress &&
	    ! spkt->satapkt_cmd.satacmd_flags.sata_ignore_dev_reset &&
	    ! ddi_in_panic()) {
		spkt->satapkt_reason = SATA_PKT_BUSY;
		BCMDBG1(BCMDBG_ERRS, bcm_ctlp,
		    "bcm_sata_start returning BUSY while "
		    "reset in progress: port: %d", port);
		mutex_exit(&bcmp->bcmp_mutex);
		return (SATA_TRAN_BUSY);
	}

	if (bcmp->bcmp_flags & BCM_PORT_FLAG_MOPPING) {
		spkt->satapkt_reason = SATA_PKT_BUSY;
		BCMDBG1(BCMDBG_ERRS, bcm_ctlp,
		    "bcm_sata_start returning BUSY while "
		    "mopping in progress: port: %d", port);
		mutex_exit(&bcmp->bcmp_mutex);
		return (SATA_TRAN_BUSY);
	}

	if (bcmp->bcmp_device_type == SATA_DTYPE_ATAPI &&
	    (bcmp->bcmp_qdma_engine_flags & BCM_QDMA_QCR_PAUSE ||
	    bcmp->bcmp_qdma_pkt_cmd_running == 1)) {
		spkt->satapkt_reason = SATA_PKT_BUSY;
		BCMDBG1(BCMDBG_ERRS, bcm_ctlp,
		    "Device is busy executing: port: %d", port);
		mutex_exit(&bcmp->bcmp_mutex);
		return (SATA_TRAN_BUSY);
	}

	if (spkt->satapkt_op_mode &
	    (SATA_OPMODE_SYNCH | SATA_OPMODE_POLLING)) {
		/*
		 * If a SYNC command to be executed in interrupt context,
		 * bounce it back to sata module.
		 */
		if (!(spkt->satapkt_op_mode & SATA_OPMODE_POLLING) &&
		    servicing_interrupt()) {
			spkt->satapkt_reason = SATA_PKT_BUSY;
			BCMDBG1(BCMDBG_ERRS, bcm_ctlp,
			    "bcm_sata_start returning BUSY while "
			    "sending SYNC mode under interrupt context: "
			    "port : %d", port);
			mutex_exit(&bcmp->bcmp_mutex);
			return (SATA_TRAN_BUSY);
		}

		/* Sync start, polling or using interrupt */
		if ((rval = bcm_do_sync_start(bcm_ctlp, bcmp, port, spkt))
		    != BCM_SUCCESS) {
			BCMDBG1(BCMDBG_ERRS, bcm_ctlp, "bcm_do_sync_start "
			    "error: rval %d", rval);
			mutex_exit(&bcmp->bcmp_mutex);
			return (rval);
		}
	} else {
		/* Async start, using interrupt */
		if ((rval = bcm_do_async_start(bcm_ctlp, bcmp, port, spkt))
		    != BCM_SUCCESS) {
			BCMDBG1(BCMDBG_ERRS, bcm_ctlp, "bcm_do_async_start "
			    "errors: rval %d", rval);
			mutex_exit(&bcmp->bcmp_mutex);
			return (rval);
		}
	}

	BCMDBG1(BCMDBG_INFO, bcm_ctlp, "bcm_sata_start "
	    "sata tran accepted: port %d", port);

	mutex_exit(&bcmp->bcmp_mutex);
	return (SATA_TRAN_ACCEPTED);
}

/*
 * if SATA_OPMODE_ASYNCH | SATA_OPMODE_INTERRUPTS are set, the command may be
 * queued (callback function specified). Interrupts could  be used. It's
 * normal operation mode.
 *
 * WARNING!!! bcmp_mutex should be acquired before the function
 * is called.
 */
static int
bcm_do_async_start(bcm_ctl_t *bcm_ctlp, bcm_port_t *bcmp,
    uint8_t port, sata_pkt_t *spkt)
{
	int rval;

	BCMDBG2(BCMDBG_ENTRY, bcm_ctlp, "bcm_do_async_start enter: "
	    "port %d spkt 0x%p", port, spkt);

	/* SATA_OPMODE_ASYNCH or SATA_OPMODE_INTERRUPTS mode */
	rval = bcm_deliver_async_satapkt(bcm_ctlp, bcmp, port, spkt);
	return (rval);
}

/*
 * SATA_OPMODE_SYNCH flag is set
 *
 * If SATA_OPMODE_POLLING flag is set, then we must poll the command
 * without interrupt, otherwise we can still use the interrupt.
 *
 * WARNING!!! bcmp_mutex should be acquired before the function
 * is called.
 */
static int
bcm_do_sync_start(bcm_ctl_t *bcm_ctlp, bcm_port_t *bcmp,
    uint8_t port, sata_pkt_t *spkt)
{
	int pkt_timeout_ticks;
	int timeout_slot;
	int rval;

	BCMDBG2(BCMDBG_ENTRY, bcm_ctlp, "bcm_do_sync_start enter: "
	    "port %d spkt 0x%p", port, spkt);

	spkt->satapkt_reason = SATA_PKT_BUSY;

	if (spkt->satapkt_op_mode & SATA_OPMODE_POLLING) {
		BCMDBG0(BCMDBG_INFO, bcm_ctlp,
		    "bcm_deliver_sync_satapkt: polling mode");
		bcmp->bcmp_flags |= BCM_PORT_FLAG_POLLING;
		if ((rval = bcm_deliver_sync_satapkt(bcm_ctlp, bcmp,
		    port, spkt)) != BCM_SUCCESS) {
			bcmp->bcmp_flags &= ~ BCM_PORT_FLAG_POLLING;
			return (rval);
		}

		pkt_timeout_ticks =
		    drv_usectohz((clock_t)spkt->satapkt_time * 1000000);

		while (spkt->satapkt_reason == SATA_PKT_BUSY) {
			mutex_exit(&bcmp->bcmp_mutex);

			BCMDBG2(BCMDBG_INFO, bcm_ctlp,
			    "bcm_do_sync_start: simulate the interrupt "
			    "for port %d and spkt 0x%p", port, spkt);

			/* Simulate the interrupt */
			bcm_port_intr(bcm_ctlp, bcmp, port);

			drv_usecwait(BCM_1MS_USECS);

			pkt_timeout_ticks -= BCM_1MS_TICKS;
			if (pkt_timeout_ticks < 0) {
				timeout_slot = -2;
				bcm_timeout_pkts(bcm_ctlp, bcmp,
				    port, timeout_slot);
			}
			mutex_enter(&bcmp->bcmp_mutex);
		}
		bcmp->bcmp_flags &= ~BCM_PORT_FLAG_POLLING;
		/* enable port interrupts */
		bcm_enable_port_intrs(bcm_ctlp, port);
		return (BCM_SUCCESS);

	} else {
		BCMDBG2(BCMDBG_INFO, bcm_ctlp,
		    "bcm_deliver_sync_satapkt: Non polling mode "
		    "for port %d and spkt 0x%p", port, spkt);

		if ((rval = bcm_deliver_sync_satapkt(bcm_ctlp, bcmp,
		    port, spkt)) != BCM_SUCCESS)
			return (rval);
		BCMDBG4(BCMDBG_INFO, bcm_ctlp,
		    "bcm_deliver_sync_satapkt: Non polling mode "
		    "for port %d and spkt 0x%p rval %d "
		    "spkt->satapkt_reason %d", port, spkt,
		    rval, spkt->satapkt_reason);

		while (spkt->satapkt_reason == SATA_PKT_BUSY) {
			BCMDBG2(BCMDBG_INFO, bcm_ctlp,
			    "bcm_deliver_sync_satapkt: port %d Non "
			    "polling mode spkt->satapkt_reason %d", port,
			    spkt->satapkt_reason);
			cv_wait(&bcmp->bcmp_cv,
			    &bcmp->bcmp_mutex);
		}

		return (BCM_SUCCESS);
	}
}

#define	SENDUP_PACKET(bcmp, satapkt, reason)			\
	if (satapkt) {							\
		satapkt->satapkt_reason = reason;			\
		/*							\
		 * We set the satapkt_reason in both sync and		\
		 * non-sync cases.					\
		 */							\
	}								\
	if (satapkt &&							\
	    ! (satapkt->satapkt_op_mode & SATA_OPMODE_SYNCH) &&		\
	    satapkt->satapkt_comp) {					\
		mutex_exit(&bcmp->bcmp_mutex);		\
		(*satapkt->satapkt_comp)(satapkt);			\
		mutex_enter(&bcmp->bcmp_mutex);		\
	} else {							\
		if (satapkt &&						\
		    (satapkt->satapkt_op_mode & SATA_OPMODE_SYNCH) &&	\
		    ! (satapkt->satapkt_op_mode & SATA_OPMODE_POLLING))	\
			cv_signal(&bcmp->bcmp_cv);		\
	}

/*
 * Find a free command slot in QDMA mode. For ATAPI command two empty
 * slots need to be available.
 *
 * WARNING!!! bcmp_mutex should be acquired before the function
 * is called.
 */
static int
bcm_claim_free_slot(bcm_ctl_t *bcm_ctlp, bcm_port_t *bcmp, int cmd_type)
{
	uint32_t current_qci, current_qpi;
	uint8_t port = bcmp->bcmp_num;

	BCMDBG3(BCMDBG_ENTRY, bcm_ctlp, "bcm_claim_free_slot enter: port %d "
	    "command type %d bcmp->bcmp_qdma_pkt_cmd_running %d ",
	    port, cmd_type, bcmp->bcmp_qdma_pkt_cmd_running);

	/* get qci value from the register */
	current_qci = ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QCI(bcm_ctlp, port));
	current_qpi = bcmp->bcmp_qpi;

	/* return current qpi for ATAPI QDMA command */
	if (cmd_type == BCM_QDMA_CMD_ATAPI) {
		/* Commands need to be serialized for ATAPI devices */
		if (bcmp->bcmp_qdma_pkt_cmd_running == 1) {
			/* QDMA engine is busy */
			return (BCM_FAILURE);
		} else {
			bcmp->bcmp_qdma_pkt_cmd_running = 1;
			return (current_qpi);
		}
	}

	/*
	 * For disk access, when QCI equals QPI, the ring is empty. when
	 * the QPI is one behind the consumer, the ring is considered full.
	 */
	if ((current_qci == 0 && current_qpi == BCM_CTL_QUEUE_DEPTH - 1) ||
	    (current_qpi + 1 == current_qci)) {
		/* ring is full */
		return (BCM_FAILURE);
	} else {
		return (current_qpi);
	}
}

/*
 * Build a non qdma command and deliver it to the controller for execution.
 *
 * WARNING!!! bcmp_mutex should be acquired before the function is called.
 */
static int
bcm_deliver_sync_satapkt(bcm_ctl_t *bcm_ctlp, bcm_port_t *bcmp,
    uint8_t port, sata_pkt_t *spkt)
{
	int rval, command_type;
	sata_cmd_t *sata_cmdp = &spkt->satapkt_cmd;
	int direction = sata_cmdp->satacmd_flags.sata_data_direction;

	BCMDBG2(BCMDBG_ENTRY, bcm_ctlp, "bcm_deliver_sync_satapkt enter: "
	    "port %d spkt 0x%p", port, spkt);

	BCMDBG1(BCMDBG_INFO, bcm_ctlp, "bcm_deliver_sync_satapkt : "
	    "direction = %d", direction);

	if ((bcmp->bcmp_qdma_engine_flags & BCM_QDMA_QCR_PAUSE) &&
	    (bcmp->bcmp_nonqdma_cmd->bcm_spkt != NULL) &&
	    (bcmp->bcmp_nonqdma_cmd->bcm_cmd_flags
	    != BCM_NONQDMA_CMD_COMPLETE)) {
		BCMDBG0(BCMDBG_ERRS, bcm_ctlp, "Previous non-qdma command "
		    "still executing...");
		return (SATA_TRAN_QUEUE_FULL);
	}

	/*
	 * Check to see if ncq command. according to the spec, ncq command
	 * can only run when QDMA is running, but not supported yet in this
	 * phase.
	 */
	if ((spkt->satapkt_cmd.satacmd_cmd_reg == SATAC_WRITE_FPDMA_QUEUED) ||
	    (spkt->satapkt_cmd.satacmd_cmd_reg == SATAC_READ_FPDMA_QUEUED)) {
		command_type = BCM_NCQ_QDMA_CMD;
		BCMDBG2(BCMDBG_ERRS, bcm_ctlp, "bcm_deliver_sync_satapkt "
		    "enter: ncq command can't run when QDMA is enabled on "
		    "port %d spkt 0x%p", port, spkt);
		return (SATA_TRAN_CMD_UNSUPPORTED);
	}

	/*
	 * Disable interrupt generation if in the polled mode.
	 */
	if (spkt->satapkt_op_mode & SATA_OPMODE_POLLING)
		bcm_disable_port_intrs(bcm_ctlp, port);

	/*
	 * We are overloading satapkt_hba_driver_private with
	 * watched_cycle count.
	 */
	spkt->satapkt_hba_driver_private = (void *)(clock_t)ddi_get_lbolt();

	/* Select the command type */
	if (!sata_cmdp->satacmd_flags.sata_protocol_pio &&
	    (sata_cmdp->satacmd_flags.sata_data_direction !=
	    SATA_DIR_NODATA_XFER)) {
		command_type = BCM_QDMA_CMD;
	} else if (sata_cmdp->satacmd_cmd_reg == SATAC_PACKET) {
		command_type = BCM_QDMA_CMD_ATAPI;
	} else {
		command_type = BCM_NON_QDMA_CMD;
	}

	/* Start a disk dma command */
	if ((command_type & BCM_QDMA_CMD) &&
	    (sata_cmdp->satacmd_num_dma_cookies != 0)) {
		rval = bcm_start_qdma_cmd(bcm_ctlp, bcmp, port, spkt);
		return (rval);
	}

	/* Start an ATAPI QDMA command. */
	if (command_type & BCM_QDMA_CMD_ATAPI) {
		if (bcm_check_atapi_qdma(bcmp, spkt) &&
		    (sata_cmdp->satacmd_num_dma_cookies != 0)) {
			BCMDBG0(BCMDBG_INFO, bcm_ctlp,
			    "QDMA ATAPI command");
			rval = bcm_start_qdma_pkt_cmd(bcm_ctlp,
			    bcmp, port, spkt);
			return (rval);
		}
	}

	/* Non qdma commands start from here. */

	/*
	 * If QDMA engine is enabled, check to see if it is paused
	 * before proceeding.
	 */
	if (bcmp->bcmp_qdma_engine_flags & BCM_QDMA_QCR_ENABLE) {
		rval = bcm_pause_port_QDMA_engine(bcm_ctlp, bcmp, port);
		if (rval == BCM_FAILURE)
			return (SATA_TRAN_PORT_ERROR);
	}

	if (spkt->satapkt_cmd.satacmd_cmd_reg == SATAC_PACKET) {
		/* Start a non qdma ATAPI command */
		BCMDBG0(BCMDBG_INFO, bcm_ctlp, "Non QDMA ATAPI command");
		rval = bcm_start_non_qdma_pkt_cmd(bcm_ctlp, bcmp, port, spkt);
	} else if (direction == SATA_DIR_NODATA_XFER) {
		/* Start a non-data command */
		BCMDBG0(BCMDBG_INFO, bcm_ctlp, "non-data command");
		rval = bcm_start_nodata(bcm_ctlp, bcmp, port, spkt);
	} else if (direction == SATA_DIR_READ) {
		/* Start a pio in command */
		BCMDBG0(BCMDBG_INFO, bcm_ctlp, "pio in command");
		rval = bcm_start_pio_in(bcm_ctlp, bcmp, port, spkt);
		sata_free_dma_resources(spkt);
	} else if (direction == SATA_DIR_WRITE) {
		/* Start a pio out command */
		BCMDBG0(BCMDBG_INFO, bcm_ctlp, "pio out command");
		rval = bcm_start_pio_out(bcm_ctlp, bcmp, port, spkt);
	} else {
		/* Malformed command */
		BCMDBG3(BCMDBG_ERRS, bcm_ctlp, "malformed command: direction"
		    " %d cookies %d cmd %x",
		    sata_cmdp->satacmd_flags.sata_data_direction,
		    sata_cmdp->satacmd_num_dma_cookies,
		    sata_cmdp->satacmd_cmd_reg);
		spkt->satapkt_reason = SATA_PKT_CMD_UNSUPPORTED;
		rval = SATA_TRAN_CMD_UNSUPPORTED;
	}

	return (rval);
}

/*
 * Start an Async command execution. For DMA transfer build a command descriptor
 * and deliver it to QDMA engine for execution.
 *
 * WARNING!!! bcmp_mutex should be acquired before the function is called.
 */
static int
bcm_deliver_async_satapkt(bcm_ctl_t *bcm_ctlp, bcm_port_t *bcmp,
    uint8_t port, sata_pkt_t *spkt)
{
	int rval, command_type;
	sata_cmd_t *scmd = &spkt->satapkt_cmd;
	int direction = scmd->satacmd_flags.sata_data_direction;

	BCMDBG2(BCMDBG_ENTRY, bcm_ctlp, "bcm_deliver_async_satapkt enter: "
	    "port %d spkt 0x%p", port, spkt);

	spkt->satapkt_reason = SATA_PKT_BUSY;

	/*
	 * We are overloading satapkt_hba_driver_private with
	 * watched_cycle count.
	 */
	spkt->satapkt_hba_driver_private = (void *)(clock_t)ddi_get_lbolt();

	/* determine the command type: ncq or non ncq */
	BCMDBG2(BCMDBG_INFO, bcm_ctlp,
	    "bcm_deliver_async_satapkt: port %d "
	    "spkt->satapkt_cmd.satacmd_cmd_reg 0x%x",
	    port, spkt->satapkt_cmd.satacmd_cmd_reg);

	if ((spkt->satapkt_cmd.satacmd_cmd_reg == SATAC_READ_FPDMA_QUEUED) ||
	    (spkt->satapkt_cmd.satacmd_cmd_reg == SATAC_WRITE_FPDMA_QUEUED)) {
		BCMDBG0(BCMDBG_INFO, bcm_ctlp, "NCQ is not supported yet");
		return (SATA_PKT_CMD_UNSUPPORTED);
	}

	/* Select the command type */
	if (!spkt->satapkt_cmd.satacmd_flags.sata_protocol_pio &&
	    (spkt->satapkt_cmd.satacmd_flags.sata_data_direction !=
	    SATA_DIR_NODATA_XFER)) {
		command_type = BCM_QDMA_CMD;
	} else if (spkt->satapkt_cmd.satacmd_cmd_reg == SATAC_PACKET) {
		command_type = BCM_QDMA_CMD_ATAPI;
	} else {
		command_type = BCM_NON_QDMA_CMD;
	}

	BCMDBG3(BCMDBG_INFO, bcm_ctlp,
	    "bcm_deliver_async_satapkt: port %d "
	    "command_type 0x%x bcmp->bcmp_qdma_engine_flags 0x%x",
	    port, command_type, bcmp->bcmp_qdma_engine_flags);

	/* Start a disk dma command */
	if ((command_type & BCM_QDMA_CMD) &&
	    (scmd->satacmd_num_dma_cookies != 0)) {
		BCMDBG0(BCMDBG_INFO, bcm_ctlp, "Disk QDMA command");
		/* Start a disk qdma command */
		rval = bcm_start_qdma_cmd(bcm_ctlp, bcmp, port, spkt);
		return (rval);
	}

	/* Start an QDMA ATAPI command */
	if (command_type & BCM_QDMA_CMD_ATAPI) {
		if (bcm_check_atapi_qdma(bcmp, spkt) &&
		    (scmd->satacmd_num_dma_cookies != 0)) {
			rval = bcm_start_qdma_pkt_cmd
			    (bcm_ctlp, bcmp, port, spkt);
			return (rval);
		}
	}

	/* Non-qdma command in async mode */

	/*
	 * If QDMA engine is enabled, check to see if it is paused
	 * before proceeding.
	 */
	if (bcmp->bcmp_qdma_engine_flags & BCM_QDMA_QCR_ENABLE) {
		rval = bcm_pause_port_QDMA_engine(bcm_ctlp, bcmp, port);
		if (rval == BCM_FAILURE)
			return (SATA_TRAN_PORT_ERROR);
	}

	if (spkt->satapkt_cmd.satacmd_cmd_reg == SATAC_PACKET) {
		BCMDBG0(BCMDBG_INFO, bcm_ctlp, "non qdma ATAPI command");
		/* Start a non qdma ATAPI command */
		rval = bcm_start_non_qdma_pkt_cmd(bcm_ctlp, bcmp, port, spkt);
	} else if (direction == SATA_DIR_NODATA_XFER) {
		BCMDBG0(BCMDBG_ENTRY, bcm_ctlp, "non-data command");
		/* Start a no data command in async mode */
		rval = bcm_start_nodata(bcm_ctlp, bcmp, port, spkt);
	} else if (direction == SATA_DIR_READ) {
		BCMDBG3(BCMDBG_ENTRY, bcm_ctlp, "pio in command: "
		    "spkt 0x%p spkt->satapkt_cmd.satacmd_bp->b_bcount = %d "
		    "spkt->satapkt_cmd.satacmd_bp->b_un.b_addr = 0x%p",
		    spkt, spkt->satapkt_cmd.satacmd_bp->b_bcount,
		    spkt->satapkt_cmd.satacmd_bp->b_un.b_addr);
		/* Start a pio in command in async mode */
		rval = bcm_start_pio_in(bcm_ctlp, bcmp, port, spkt);
		sata_free_dma_resources(spkt);
	} else if (direction == SATA_DIR_WRITE) {
		BCMDBG0(BCMDBG_ENTRY, bcm_ctlp, "pio out command");
		/* Start a pio out command in async mode */
		rval = bcm_start_pio_out(bcm_ctlp, bcmp, port, spkt);
	} else {
		BCMDBG3(BCMDBG_ERRS, bcm_ctlp, "malformed command: direction"
		    " %d cookies %d cmd %x",
		    scmd->satacmd_flags.sata_data_direction,
		    scmd->satacmd_num_dma_cookies,
		    scmd->satacmd_cmd_reg);
		/* Malformed command */
		spkt->satapkt_reason = SATA_PKT_CMD_UNSUPPORTED;
		rval = SATA_TRAN_CMD_UNSUPPORTED;
	}

	return (rval);
}

/*
 * Create a command descriptor and deliver it to the command descriptor
 * queue for execution.
 *
 * WARNING!!! bcmp_mutex should be acquired before the function is called.
 */
static int
bcm_start_qdma_cmd(bcm_ctl_t *bcm_ctlp, bcm_port_t *bcmp, uint8_t port,
    sata_pkt_t *spkt)
{
	int cmd_slot, ncookies;
	int dbit = 0;
	uint8_t dtype = 0;
	uint8_t cflags = 0;
	bcm_prde_t *prdt_vaddr;
	bcm_cmd_descriptor_t *cmd_descriptor;
	ddi_dma_cookie_t prdt_cookie;
	sata_cmd_t *scmd = &spkt->satapkt_cmd;
	int direction = scmd->satacmd_flags.sata_data_direction;

#if BCM_DEBUG
	uint32_t qsr_status, qpi_status, qci_status;
#endif

	BCMDBG2(BCMDBG_ENTRY, bcm_ctlp, "bcm_start_qdma_cmd enter: "
	    "port %d spkt 0x%p", port, spkt);

	/* Check if there is an empty command slot */
	cmd_slot = bcm_claim_free_slot(bcm_ctlp, bcmp, BCM_QDMA_CMD);
	if (cmd_slot == BCM_FAILURE) {
		BCMDBG0(BCMDBG_ERRS, bcm_ctlp, "Command queue is full");
		return (SATA_TRAN_QUEUE_FULL);
	}

	/* Store this packet in the array */
	bcmp->bcmp_slot_pkts[cmd_slot] = spkt;

	BCMDBG4(BCMDBG_INFO, bcm_ctlp,
	    "bcm_start_qdma_cmd: port %d cmd_reg: 0x%x, cmd_slot: 0x%x, "
	    "satapkt: 0x%p", port, scmd->satacmd_cmd_reg,
	    cmd_slot, (void *)spkt);

	/*
	 * Start to assemble command descriptor for the QDMA command
	 */

	cmd_descriptor = &bcmp->bcmp_cmd_queue[cmd_slot];
	prdt_vaddr = (bcm_prde_t *)bcmp->bcmp_prdts_qdma_dma_vaddr[cmd_slot];
	prdt_cookie = bcmp->bcmp_prdts_qdma_dma_cookie[cmd_slot];

	bzero((void *)cmd_descriptor, BCM_CMD_DESCRIPTOR_SIZE);

	/*
	 * Set descriptor type on command descriptor. it currently take
	 * 0 as the only value
	 */
	SET_BCMCD_DTYPE(cmd_descriptor, dtype);

	/*
	 * Set control flag on command descriptor.
	 */
	if (direction == SATA_DIR_READ) {
		cflags |= BCM_CMD_FLAGS_DIR_READ;
	}

	/* Enable interrupts after current QDMA descriptor completion */
	cflags |= BCM_CMD_FLAGS_EIN;

	/* Set control flags */
	SET_BCMCD_CFLAGS(cmd_descriptor, cflags);

	/* set sata command */
	SET_BCMCD_SCMD(cmd_descriptor, scmd->satacmd_cmd_reg);

	/* set sector count */
	SET_BCMCD_SECTOR_COUNT(cmd_descriptor, scmd->satacmd_sec_count_lsb);

	/* set features */
	SET_BCMCD_FEATURES(cmd_descriptor, scmd->satacmd_features_reg);

	switch (scmd->satacmd_addr_type) {
	case 0:
		BCMDBG0(BCMDBG_INFO, bcm_ctlp, "case 0");
		/*
		 * satacmd_addr_type will be 0 for the commands below:
		 *	ATAPI command
		 *	SATAC_IDLE_IM
		 *	SATAC_STANDBY_IM
		 *	SATAC_DOWNLOAD_MICROCODE
		 *	SATAC_FLUSH_CACHE
		 *	SATAC_SET_FEATURES
		 *	SATAC_SMART
		 *	SATAC_ID_PACKET_DEVICE
		 *	SATAC_ID_DEVICE
		 */
		/* FALLTHRU */

	case ATA_ADDR_LBA:
		BCMDBG0(BCMDBG_INFO, bcm_ctlp, "ATA_ADDR_LBA");
		/* FALLTHRU */

	case ATA_ADDR_LBA28:
		BCMDBG0(BCMDBG_INFO, bcm_ctlp, "ATA_ADDR_LBA28");
		/* LBA[7:0] */
		SET_BCMCD_SECTOR(cmd_descriptor, scmd->satacmd_lba_low_lsb);

		/* LBA[15:8] */
		SET_BCMCD_CYLLOW(cmd_descriptor, scmd->satacmd_lba_mid_lsb);

		/* LBA[23:16] */
		SET_BCMCD_CYLHI(cmd_descriptor, scmd->satacmd_lba_high_lsb);

		/* LBA [27:24] (also called dev_head) */
		SET_BCMCD_DEVHEAD(cmd_descriptor, scmd->satacmd_device_reg);

		break;

	case ATA_ADDR_LBA48:
		BCMDBG0(BCMDBG_INFO, bcm_ctlp, "ATA_ADDR_LBA48");
		/* LBA[7:0] */
		SET_BCMCD_SECTOR(cmd_descriptor, scmd->satacmd_lba_low_lsb);

		/* LBA[15:8] */
		SET_BCMCD_CYLLOW(cmd_descriptor, scmd->satacmd_lba_mid_lsb);

		/* LBA[23:16] */
		SET_BCMCD_CYLHI(cmd_descriptor, scmd->satacmd_lba_high_lsb);

		/* LBA [31:24] */
		SET_BCMCD_SECTOREXT(cmd_descriptor,
		    scmd->satacmd_lba_low_msb);

		/* LBA [39:32] */
		SET_BCMCD_CYLLOWEXT(cmd_descriptor,
		    scmd->satacmd_lba_mid_msb);

		/* LBA [47:40] */
		SET_BCMCD_CYLHIEXT(cmd_descriptor,
		    scmd->satacmd_lba_high_msb);

		/* Set dev_head */
		SET_BCMCD_DEVHEAD(cmd_descriptor,
		    scmd->satacmd_device_reg);

		/* Set the extended sector count and features */
		SET_BCMCD_SECTOR_COUNTEXT(cmd_descriptor,
		    scmd->satacmd_sec_count_msb);

		SET_BCMCD_FEATURESEXT(cmd_descriptor,
		    scmd->satacmd_features_reg_ext);

		break;
	}

	ncookies = scmd->satacmd_num_dma_cookies;

	BCMDBG2(BCMDBG_INFO, bcm_ctlp,
	    "ncookies = 0x%x, bcm_dma_prdt_number = 0x%x",
	    ncookies, bcm_dma_prdt_number);

	ASSERT(ncookies <= bcm_dma_prdt_number);

	/* *** now fill the scatter gather list ******* */
	for (int i = 0; i < ncookies; i++) {
		int length, eot;
		SET_BCPRDE_BADDRLO(prdt_vaddr[i],
		    scmd->satacmd_dma_cookie_list[i]._dmu._dmac_la[0]);
		SET_BCPRDE_BADDRHI(prdt_vaddr[i],
		    scmd->satacmd_dma_cookie_list[i]._dmu._dmac_la[1]);

		BCMDBG4(BCMDBG_INFO, bcm_ctlp, "bcm_start_qdma_cmd: port %d i="
		    "%d scmd->satacmd_dma_cookie_list[i]._dmu._dmac_la[0] 0x%x"
		    " scmd->satacmd_dma_cookie_list[i]._dmu._dmac_la[1] 0x%x",
		    port, i, scmd->satacmd_dma_cookie_list[i]._dmu._dmac_la[0],
		    scmd->satacmd_dma_cookie_list[i]._dmu._dmac_la[1]);
		BCMDBG4(BCMDBG_INFO, bcm_ctlp, "bcm_start_qdma_cmd: port %d "
		    "i = %d prdt_vaddr[i].bcmprde_baddrlo 0x%x "
		    "prdt_vaddr[i].bcmprde_length_baddrhi_eot 0x%x",
		    port, i, prdt_vaddr[i].bcmprde_baddrlo,
		    prdt_vaddr[i].bcmprde_length_baddrhi_eot);

		if (i != (ncookies - 1)) {
			eot = 0;
		} else {
			eot = 1;
		}

		BCMDBG3(BCMDBG_INFO, bcm_ctlp, "bcm_start_qdma_cmd: port %d "
		    "i = %d scmd->satacmd_dma_cookie_list[i].dmac_size %d",
		    port, i, scmd->satacmd_dma_cookie_list[i].dmac_size);

		length = scmd->satacmd_dma_cookie_list[i].dmac_size;
		if (length == 65536) {
			length = 0;
		}

		BCMDBG3(BCMDBG_INFO, bcm_ctlp, "bcm_start_qdma_cmd: port %d "
		    "i = %d length %d", port, i, length);

		SET_BCPRDE_LENGTH(prdt_vaddr[i], length);
		SET_BCPRDE_EOT(prdt_vaddr[i], eot);

		BCMDBG4(BCMDBG_INFO, bcm_ctlp, "bcm_start_qdma_cmd: port %d "
		    "i = %d prdt_vaddr[i].bcmprde_baddrlo 0x%x "
		    "prdt_vaddr[i].bcmprde_length_baddrhi_eot 0x%x",
		    port, i, prdt_vaddr[i].bcmprde_baddrlo,
		    prdt_vaddr[i].bcmprde_length_baddrhi_eot);
	}

	/* set prd table base address */
	SET_BCMCD_PRDTLO(cmd_descriptor, prdt_cookie._dmu._dmac_la[0]);
	SET_BCMCD_PRDTHI(cmd_descriptor, prdt_cookie._dmu._dmac_la[1]);

	BCMDBG3(BCMDBG_INFO, bcm_ctlp, "bcm_start_qdma_cmd: port %d "
	    "prdt_cookie._dmu._dmac_la[0] 0x%x "
	    "prdt_cookie._dmu._dmac_la[1] 0x%x", port,
	    prdt_cookie._dmu._dmac_la[0], prdt_cookie._dmu._dmac_la[1]);

	/* set dbit */
	SET_BCMCD_DBIT(cmd_descriptor, dbit);

	BCMDBG3(BCMDBG_COMMAND, bcm_ctlp,
	    "Command descriptor for spkt 0x%p cmd_reg 0x%x port %d",
	    spkt, scmd->satacmd_cmd_reg, port);
	BCMDBG3(BCMDBG_INFO, bcm_ctlp, "bcm_start_qdma_cmd: port %d "
	    "cflags 0x%x scmd 0x%x", port, GET_BCMCD_CFLAGS(cmd_descriptor),
	    GET_BCMCD_SCMD(cmd_descriptor));

	BCMDBG5(BCMDBG_INFO, bcm_ctlp, "bcm_start_qdma_cmd: port %d "
	    "cmd_descriptor->bcmcd_dtype_cflags_pmp_rsvd 0x%x "
	    "cmd_descriptor->bcmcd_dbit_rbit_prdtlo 0x%x "
	    "cmd_descriptor->bcmcd_prdthi_rsvd 0x%x "
	    "cmd_descriptor->bcmcd_scmd_devhead_features 0x%x", port,
	    cmd_descriptor->bcmcd_dtype_cflags_pmp_rsvd,
	    cmd_descriptor->bcmcd_dbit_rbit_prdtlo,
	    cmd_descriptor->bcmcd_prdthi_rsvd,
	    cmd_descriptor->bcmcd_scmd_devhead_features);

	BCMDBG3(BCMDBG_INFO, bcm_ctlp, "bcm_start_qdma_cmd: port %d "
	    "cmd_descriptor->bcmcd_sector_cyllow_cylhi_sectorext 0x%x "
	    "cmd_descriptor->bcmcd_cyllowext_cylhiext_sectorcnt 0x%x",
	    port, cmd_descriptor->bcmcd_sector_cyllow_cylhi_sectorext,
	    cmd_descriptor->bcmcd_cyllowext_cylhiext_sectorcnt);

	(void) ddi_dma_sync(bcmp->bcmp_prdts_qdma_dma_hdl[cmd_slot],
	    0,
	    sizeof (bcm_prde_t) * BCM_PRDT_NUMBER,
	    DDI_DMA_SYNC_FORDEV);

	(void) ddi_dma_sync(bcmp->bcmp_cmd_queue_dma_handle,
	    cmd_slot * sizeof (bcm_cmd_descriptor_t),
	    BCM_CMD_DESCRIPTOR_SIZE,
	    DDI_DMA_SYNC_FORDEV);

	/* Increment Port's QPI to indicate that the command is active. */
	if (cmd_slot == BCM_CTL_QUEUE_DEPTH - 1) {
		cmd_slot = 0;
	} else {
		cmd_slot = cmd_slot + 1;
	}

	/* Set QPI */
	ddi_put32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QPI(bcm_ctlp, port),
	    (uint32_t)(cmd_slot & 0xff));

	/* update qpi cache value */
	bcmp->bcmp_qpi = (uint32_t)(cmd_slot & 0xff);

#if BCM_DEBUG
	qpi_status = ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QPI(bcm_ctlp, port));
	qci_status = ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QCI(bcm_ctlp, port));
	qsr_status = ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QSR(bcm_ctlp, port));

	BCMDBG4(BCMDBG_INFO, bcm_ctlp, "bcm_start_qdma_cmd: port %d "
	    "qpi 0x%x qci 0x%x qsr 0x%x", port, qpi_status, qci_status,
	    qsr_status);
#endif

	return (BCM_SUCCESS);
}

/*
 * To start a QDMA ATAPI command. Two command descriptors are created
 * and delivered to the command descriptor queue for execution.
 *
 * WARNING!!! bcmp_mutex should be acquired before the function is called.
 */
static int
bcm_start_qdma_pkt_cmd(bcm_ctl_t *bcm_ctlp, bcm_port_t *bcmp, uint8_t port,
    sata_pkt_t *spkt)
{
	int first_slot, second_slot, ncookies;
	uint8_t dtype = 0;
	uint8_t first_cflags = 0;
	uint8_t second_cflags = 0;
	bcm_prde_t *prdt_vaddr;
	uchar_t *cdb_vaddr;
	bcm_cmd_descriptor_t *first_cmd_descriptor;
	bcm_cmd_descriptor_t *second_cmd_descriptor;
	ddi_dma_cookie_t prdt_cookie, cdb_cookie;
	sata_cmd_t *scmd = &spkt->satapkt_cmd;
	int direction = scmd->satacmd_flags.sata_data_direction;

	BCMDBG2(BCMDBG_ENTRY, bcm_ctlp, "bcm_start_qdma_pkt_cmd enter: "
	    "port %d spkt 0x%p", port, spkt);

	ASSERT(mutex_owned(&bcmp->bcmp_mutex));

	/* Get free command slot */
	first_slot = bcm_claim_free_slot(bcm_ctlp, bcmp, BCM_QDMA_CMD_ATAPI);
	if (first_slot == BCM_FAILURE) {
		BCMDBG0(BCMDBG_ERRS, bcm_ctlp, "QDMA engine is busy");
		return (SATA_TRAN_BUSY);
	}

	/* Calculate the second slot */
	second_slot = (first_slot + 1) % BCM_CTL_QUEUE_DEPTH;

	/* Initialize slots in the pkts array */
	bcmp->bcmp_slot_pkts[first_slot] = spkt;
	bcmp->bcmp_slot_pkts[second_slot] = NULL;
	BCMDBG5(BCMDBG_INFO, bcm_ctlp,
	    "bcm_start_qdma_pkt_cmd: port %d cmd_reg: 0x%x, first_slot: 0x%x, "
	    "second_slot: 0x%x, satapkt: 0x%p", port, scmd->satacmd_cmd_reg,
	    first_slot, second_slot, (void *)spkt);

	/*
	 * Start to assemble two command descriptors for the ATAPI command
	 */

	first_cmd_descriptor = &bcmp->bcmp_cmd_queue[first_slot];
	second_cmd_descriptor = &bcmp->bcmp_cmd_queue[second_slot];

	/* Zero out two descriptors first */
	bzero((void *)first_cmd_descriptor, BCM_CMD_DESCRIPTOR_SIZE);
	bzero((void *)second_cmd_descriptor, BCM_CMD_DESCRIPTOR_SIZE);

	/*
	 * First command descriptor is flagged as an ATAPI and as a PIO request.
	 * Second command descriptor is flagged as a DMA request because SATA
	 * framework always sets up for DMA.
	 */

	/*
	 * Set descriptor type on command descriptors to 0.
	 */
	SET_BCMCD_DTYPE(first_cmd_descriptor, dtype);
	SET_BCMCD_DTYPE(second_cmd_descriptor, dtype);

	/*
	 * Set control flag on command descriptors.
	 */

	/*
	 * Set control flag for first command descriptor
	 * BCM_CMD_FLAGS_ATAPI = 0x40
	 * BCM_CMD_FLAGS_PIO = 0x80
	 */
	first_cflags = (BCM_CMD_FLAGS_ATAPI | BCM_CMD_FLAGS_PIO);
	BCMDBG2(BCMDBG_INFO, bcm_ctlp, "bcm_start_qdma_pkt_cmd: "
	    "port %d first_cflags 0x%x", port, first_cflags);

	BCMDBG2(BCMDBG_INFO, bcm_ctlp, "bcm_start_qdma_pkt_cmd: "
	    "port %d read direction %d", port, direction);
	/*
	 * Set control flag for second command descriptor.
	 * Second descriptor is for DMA data transfer.
	 * EIN bit set to 1 for interrupt to be generated
	 * after command execution.
	 */
	if (direction == SATA_DIR_WRITE) {
		second_cflags = BCM_CMD_FLAGS_EIN;
	} else {
		second_cflags = (BCM_CMD_FLAGS_DIR_READ |
		    BCM_CMD_FLAGS_EIN);
	}
	BCMDBG2(BCMDBG_INFO, bcm_ctlp, "bcm_start_qdma_pkt_cmd: "
	    "port %d second_cflags 0x%x", port, second_cflags);

	/* Set control flags for two descriptors */
	SET_BCMCD_CFLAGS(first_cmd_descriptor, first_cflags);
	SET_BCMCD_CFLAGS(second_cmd_descriptor, second_cflags);

	BCMDBG2(BCMDBG_INFO, bcm_ctlp, "bcm_start_qdma_pkt_cmd: port %d "
	    "sata cmd: 0x%x", port, scmd->satacmd_cmd_reg);
	/* set sata command(0xa0) on two descriptors */
	SET_BCMCD_SCMD(first_cmd_descriptor, scmd->satacmd_cmd_reg);
	SET_BCMCD_SCMD(second_cmd_descriptor, scmd->satacmd_cmd_reg);

	/*
	 * Broadcom has suggested that we set dev/head register to 0xa0
	 * Normally it should be set to scmd->satacmd_device_reg.
	 */
	SET_BCMCD_DEVHEAD(first_cmd_descriptor, BCM_SATA_ATAPI_DEV_VAL);
	SET_BCMCD_DEVHEAD(second_cmd_descriptor, BCM_SATA_ATAPI_DEV_VAL);

	/*
	 * Set features to 0x1 for first descriptor.
	 * The second one does not need to be set(default to 0x0).
	 */
	SET_BCMCD_FEATURES(first_cmd_descriptor, BCM_SATA_ATAPI_FEAT_VAL);

	/*
	 * Copy cdb from the packet to a memory buffer and set it up in the
	 * first descriptor so that cdb can be transfered to device
	 */
	cdb_vaddr = (uchar_t *)bcmp->bcmp_qdma_atapi_cdb_vaddr[first_slot];
	cdb_cookie = bcmp->bcmp_qdma_cdb_dma_cookie[first_slot];
	/* zero out the buffer */
	bzero((void *)cdb_vaddr, SATA_ATAPI_MAX_CDB_LEN);

	BCMDBG2(BCMDBG_INFO, bcm_ctlp, "bcm_start_qdma_pkt_cmd: port %d "
	    "cdb length: 0x%x", port, scmd->satacmd_acdb_len);
	bcopy(scmd->satacmd_acdb, cdb_vaddr, scmd->satacmd_acdb_len);

	/*
	 * Set PRD_TABLE_BASE low address to the low address of
	 * the cdb buffer
	 */
	SET_BCMCD_PRDTLO(first_cmd_descriptor, cdb_cookie._dmu._dmac_la[0]);
	/* Set dbit to 0x1 for first descriptor for no prd table */
	SET_BCMCD_DBIT(first_cmd_descriptor, BCM_CMD_DBIT_NO_PRD);

	/*
	 * Set PRD_TABLE_BASE high address to the high address of cdb buffer
	 * In our case the high address is always 0x0 (32 bits).
	 */
	SET_BCMCD_PRDTHI(first_cmd_descriptor, cdb_cookie._dmu._dmac_la[1]);
	/* Set PRD_COUNT with cdb length for first descriptor */
	SET_BCMCD_PRDCOUNT(first_cmd_descriptor, scmd->satacmd_acdb_len);

	/*
	 * Determine if it is a non-data ATAPI command or a data transfer
	 * ATAPI command for the second descriptor
	 */
	ncookies = scmd->satacmd_num_dma_cookies;
	BCMDBG2(BCMDBG_INFO, bcm_ctlp,
	    "direction = 0x%x ncookies = 0x%x ",
	    direction, ncookies);

	/* In case of one cookie, no prd table is needed */
	if (ncookies == 1) {
		BCMDBG3(BCMDBG_INFO, bcm_ctlp,
		    "bcm_start_qdma_pkt_cmd: port %d "
		    "scmd->satacmd_dma_cookie_list[0]._dmu._dmac_la[0] 0x%x "
		    "scmd->satacmd_dma_cookie_list[0]._dmu._dmac_la[1] 0x%x",
		    port, scmd->satacmd_dma_cookie_list[0]._dmu._dmac_la[0],
		    scmd->satacmd_dma_cookie_list[0]._dmu._dmac_la[1]);
		/* set low address for second descriptor */
		SET_BCMCD_PRDTLO(second_cmd_descriptor,
		    scmd->satacmd_dma_cookie_list[0]._dmu._dmac_la[0]);
		/* set high address for second descriptor */
		SET_BCMCD_PRDTHI(second_cmd_descriptor,
		    scmd->satacmd_dma_cookie_list[0]._dmu._dmac_la[1]);
		/* Set dbit to 0x1 to indicate no prd table */
		SET_BCMCD_DBIT(second_cmd_descriptor, BCM_CMD_DBIT_NO_PRD);
		/* Set number of bytes to transfer */
		SET_BCMCD_PRDCOUNT(second_cmd_descriptor,
		    scmd->satacmd_dma_cookie_list[0].dmac_size);

		goto cm_out;
	}

	/*
	 * In case of cookies more than one, Set up prd table for DMA
	 * data transfer
	 */
	prdt_vaddr = (bcm_prde_t *)bcmp->bcmp_prdts_qdma_dma_vaddr[second_slot];
	prdt_cookie = bcmp->bcmp_prdts_qdma_dma_cookie[second_slot];

	ASSERT(ncookies <= bcm_dma_prdt_number);

	/* *** now fill the scatter gather list ******* */
	for (int i = 0; i < ncookies; i++) {
		int length, eot;

		/* Set low and high address in prd table entry */
		SET_BCPRDE_BADDRLO(prdt_vaddr[i],
		    scmd->satacmd_dma_cookie_list[i]._dmu._dmac_la[0]);
		SET_BCPRDE_BADDRHI(prdt_vaddr[i],
		    scmd->satacmd_dma_cookie_list[i]._dmu._dmac_la[1]);

		/* check to see if this is last cookie */
		if (i != (ncookies - 1)) {
			eot = 0;
		} else {
			eot = 1;
		}

		BCMDBG3(BCMDBG_INFO, bcm_ctlp, "bcm_start_qdma_pkt_cmd: port %d"
		    " i = %d scmd->satacmd_dma_cookie_list[i].dmac_size %d",
		    port, i, scmd->satacmd_dma_cookie_list[i].dmac_size);

		/* Determine the length field for the prd table entry */
		length = scmd->satacmd_dma_cookie_list[i].dmac_size;
		if (length == 65536) {
			length = 0;
		}
		BCMDBG3(BCMDBG_INFO, bcm_ctlp, "bcm_start_qdma_pkt_cmd: port %d"
		    " i = %d length %d", port, i, length);

		/* Set length field for prd table entry */
		SET_BCPRDE_LENGTH(prdt_vaddr[i], length);
		/* Set eot bit. 1 for last entry and 0 for not */
		SET_BCPRDE_EOT(prdt_vaddr[i], eot);

		BCMDBG4(BCMDBG_INFO, bcm_ctlp, "bcm_start_qdma_pkt_cmd: port %d"
		    " i = %d prdt_vaddr[i].bcmprde_baddrlo 0x%x "
		    "prdt_vaddr[i].bcmprde_length_baddrhi_eot 0x%x",
		    port, i, prdt_vaddr[i].bcmprde_baddrlo,
		    prdt_vaddr[i].bcmprde_length_baddrhi_eot);
	}

	BCMDBG3(BCMDBG_INFO, bcm_ctlp, "bcm_start_qdma_pkt_cmd: port %d "
	    "prdt_cookie._dmu._dmac_la[0] 0x%x "
	    "prdt_cookie._dmu._dmac_la[1] 0x%x", port,
	    prdt_cookie._dmu._dmac_la[0], prdt_cookie._dmu._dmac_la[1]);
	/*
	 * set prd table base address on second descriptor. The high address
	 * address will be 0x0 because prd table can't cross 4gb boundary.
	 */
	SET_BCMCD_PRDTLO(second_cmd_descriptor, prdt_cookie._dmu._dmac_la[0]);
	SET_BCMCD_PRDTHI(second_cmd_descriptor, prdt_cookie._dmu._dmac_la[1]);

	(void) ddi_dma_sync(bcmp->bcmp_prdts_qdma_dma_hdl[second_slot],
	    0,
	    sizeof (bcm_prde_t) * BCM_PRDT_NUMBER,
	    DDI_DMA_SYNC_FORDEV);

cm_out:

	(void) ddi_dma_sync(bcmp->bcmp_qdma_cdb_dma_hdl[first_slot],
	    0,
	    SATA_ATAPI_MAX_CDB_LEN,
	    DDI_DMA_SYNC_FORDEV);

	(void) ddi_dma_sync(bcmp->bcmp_cmd_queue_dma_handle,
	    first_slot * sizeof (bcm_cmd_descriptor_t),
	    BCM_CMD_DESCRIPTOR_SIZE * 2,
	    DDI_DMA_SYNC_FORDEV);

	BCMDBG3(BCMDBG_COMMAND, bcm_ctlp,
	    "ATAPI command descriptors for spkt 0x%p cmd_reg 0x%x port %d",
	    spkt, scmd->satacmd_cmd_reg, port);

	BCMDBG5(BCMDBG_INFO, bcm_ctlp, "bcm_start_qdma_pkt_cmd: port %d "
	    "first_cmd_descriptor->bcmcd_dtype_cflags_pmp_rsvd 0x%x "
	    "first_cmd_descriptor->bcmcd_dbit_rbit_prdtlo 0x%x "
	    "first_cmd_descriptor->bcmcd_prdthi_rsvd 0x%x "
	    "first-cmd_descriptor->bcmcd_scmd_devhead_features 0x%x", port,
	    first_cmd_descriptor->bcmcd_dtype_cflags_pmp_rsvd,
	    first_cmd_descriptor->bcmcd_dbit_rbit_prdtlo,
	    first_cmd_descriptor->bcmcd_prdthi_rsvd,
	    first_cmd_descriptor->bcmcd_scmd_devhead_features);

	BCMDBG3(BCMDBG_INFO, bcm_ctlp, "bcm_start_qdma_pkt_cmd: port %d "
	    "first_cmd_descriptor->bcmcd_sector_cyllow_cylhi_sectorext 0x%x "
	    "first_cmd_descriptor->bcmcd_cyllowext_cylhiext_sectorcnt 0x%x",
	    port, first_cmd_descriptor->bcmcd_sector_cyllow_cylhi_sectorext,
	    first_cmd_descriptor->bcmcd_cyllowext_cylhiext_sectorcnt);

	BCMDBG5(BCMDBG_INFO, bcm_ctlp, "bcm_start_qdma_pkt_cmd: port %d "
	    "second_cmd_descriptor->bcmcd_dtype_cflags_pmp_rsvd 0x%x "
	    "second_cmd_descriptor->bcmcd_dbit_rbit_prdtlo 0x%x "
	    "second_cmd_descriptor->bcmcd_prdthi_rsvd 0x%x "
	    "second_cmd_descriptor->bcmcd_scmd_devhead_features 0x%x", port,
	    second_cmd_descriptor->bcmcd_dtype_cflags_pmp_rsvd,
	    second_cmd_descriptor->bcmcd_dbit_rbit_prdtlo,
	    second_cmd_descriptor->bcmcd_prdthi_rsvd,
	    second_cmd_descriptor->bcmcd_scmd_devhead_features);

	BCMDBG3(BCMDBG_INFO, bcm_ctlp, "bcm_start_qdma_pkt_cmd: port %d "
	    "second_cmd_descriptor->bcmcd_sector_cyllow_cylhi_sectorext 0x%x "
	    "second_cmd_descriptor->bcmcd_cyllowext_cylhiext_sectorcnt 0x%x",
	    port, second_cmd_descriptor->bcmcd_sector_cyllow_cylhi_sectorext,
	    second_cmd_descriptor->bcmcd_cyllowext_cylhiext_sectorcnt);

	/* update qpi cache value */
	bcmp->bcmp_qpi = (uint32_t)((first_slot + 2) % BCM_CTL_QUEUE_DEPTH);

	/* Set new QPI */
	ddi_put32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QPI(bcm_ctlp, port),
	    bcmp->bcmp_qpi);

	return (BCM_SUCCESS);
}

/*
 * Called by the sata framework to abort the previously sent packet(s).
 *
 * Reset device to abort commands.
 */
static int
bcm_sata_abort(dev_info_t *dip, sata_pkt_t *spkt, int flag)
{
	bcm_ctl_t *bcm_ctlp;
	bcm_port_t *bcmp;
	int aborted_slot = -1;
	uint8_t port;
	int tmp_slot;
	int mop_flag;
	int instance = ddi_get_instance(dip);

	bcm_ctlp = ddi_get_soft_state(bcm_statep, instance);
	port = spkt->satapkt_device.satadev_addr.cport;

	BCMDBG1(BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_sata_abort enter: port %d", port);

	bcmp = bcm_ctlp->bcmc_ports[port];

	if (flag == SATA_ABORT_PACKET) {
		mop_flag = BCM_SATA_ABORT_ONE;
	} else if (flag == SATA_ABORT_ALL_PACKETS) {
		mop_flag = BCM_SATA_ABORT_ALL;
	}

	mutex_enter(&bcmp->bcmp_mutex);

	/*
	 * If BCM_PORT_FLAG_MOPPING flag is set, it means all the pending
	 * commands are being mopped, therefore there is nothing else to do
	 */
	if (bcmp->bcmp_flags & BCM_PORT_FLAG_MOPPING) {
		BCMDBG1(BCMDBG_INFO, bcm_ctlp,
		    "bcm_sata_abort: port %d is in "
		    "mopping process, so just return directly ", port);
		mutex_exit(&bcmp->bcmp_mutex);
		return (SATA_SUCCESS);
	}

	if (bcmp->bcmp_state & SATA_PSTATE_FAILED |
	    bcmp->bcmp_state & SATA_PSTATE_SHUTDOWN |
	    bcmp->bcmp_state & SATA_PSTATE_PWROFF) {
		/*
		 * In case the targer driver would send the request before
		 * sata framework can have the opportunity to process those
		 * event reports.
		 */
		spkt->satapkt_reason = SATA_PKT_PORT_ERROR;
		spkt->satapkt_device.satadev_state =
		    bcmp->bcmp_state;
		bcm_update_sata_registers(bcm_ctlp, port,
		    &spkt->satapkt_device);
		BCMDBG1(BCMDBG_ERRS, bcm_ctlp,
		    "bcm_sata_abort returning SATA_FAILURE while "
		    "port in FAILED/SHUTDOWN/PWROFF state: "
		    "port: %d", port);
		mutex_exit(&bcmp->bcmp_mutex);
		return (SATA_FAILURE);
	}

	if (bcmp->bcmp_device_type == SATA_DTYPE_NONE) {
		/*
		 * bcm_intr_phyrdy_change() may have rendered it to
		 * BCM_PORT_TYPE_NODEV.
		 */
		spkt->satapkt_reason = SATA_PKT_PORT_ERROR;
		spkt->satapkt_device.satadev_type = SATA_DTYPE_NONE;
		spkt->satapkt_device.satadev_state = bcmp->bcmp_state;
		bcm_update_sata_registers(bcm_ctlp, port,
		    &spkt->satapkt_device);
		BCMDBG1(BCMDBG_ERRS, bcm_ctlp,
		    "bcm_sata_abort returning SATA_FAILURE while "
		    "no device attached: port: %d", port);
		mutex_exit(&bcmp->bcmp_mutex);
		return (SATA_FAILURE);
	}

	if (flag == SATA_ABORT_ALL_PACKETS) {
		cmn_err(CE_NOTE, "!bcm%d: bcm port %d abort all packets",
		    instance, port);
	} else {

		/*
		 * First check if command to be aborted is a non qdma
		 * command or not
		 */
		if (bcmp->bcmp_nonqdma_cmd->bcm_spkt != NULL &&
		    bcmp->bcmp_nonqdma_cmd->bcm_spkt == spkt) {
			/* Abort a non qdma cmd */
			aborted_slot = -2;
			goto abort_out;
		}

		/*
		 *  Second search the bcmp_slot_pkts[] list for matching spkt.
		 */
		for (tmp_slot = 0; tmp_slot < BCM_CTL_QUEUE_DEPTH;
		    tmp_slot++) {
			if (bcmp->bcmp_slot_pkts[tmp_slot] == spkt) {
				aborted_slot = tmp_slot;
				goto abort_out;
			}
		}

		cmn_err(CE_NOTE, "!bcm%d: bcm port %d abort satapkt 0x%p",
		    instance, port, (void *)spkt);
	}

abort_out:

	bcmp->bcmp_flags |= BCM_PORT_FLAG_MOPPING;
	bcmp->bcmp_mop_in_progress++;

	/*
	 * COMREST if needs to.
	 */
	(void) bcm_restart_port_wait_till_ready(bcm_ctlp,
	    bcmp, port, NULL, NULL);

	bcm_mop_commands(bcm_ctlp,
	    bcmp,
	    mop_flag,
	    BCM_NO_FAILED_SLOT, /* failed slot */
	    BCM_NO_TIMEOUT_SLOT, /* timeout slot */
	    aborted_slot,
	    BCM_NO_RESET_SLOT); /* reset slot */

	bcm_update_sata_registers(bcm_ctlp, port, &spkt->satapkt_device);
	mutex_exit(&bcmp->bcmp_mutex);

	return (SATA_SUCCESS);
}

/*
 * Used to do port reset and reject all the pending packets on a port during
 * the reset operation.
 *
 * WARNING!!! bcmp_mutex should be acquired before the function is called.
 */
static int
bcm_reset_port_reject_pkts(bcm_ctl_t *bcm_ctlp,
    bcm_port_t *bcmp, uint8_t port)
{
	BCMDBG1(BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_reset_port_reject_pkts on port: %d", port);

	/*
	 * If BCM_PORT_FLAG_MOPPING flag is set, it means all the pending
	 * commands are being mopped, therefore there is nothing else to do
	 */
	if (bcmp->bcmp_flags & BCM_PORT_FLAG_MOPPING) {
		BCMDBG1(BCMDBG_ERRS, bcm_ctlp,
		    "bcm_reset_port_reject_pkts: port %d is in "
		    "mopping process, so return directly ", port);
		return (SATA_SUCCESS);
	}

	bcmp->bcmp_flags |= BCM_PORT_FLAG_MOPPING;
	bcmp->bcmp_mop_in_progress++;

	if (bcm_restart_port_wait_till_ready(bcm_ctlp,
	    bcmp, port, BCM_PORT_RESET|BCM_RESET_NO_EVENTS_UP,
	    NULL) != BCM_SUCCESS)
		return (SATA_FAILURE);

	bcm_mop_commands(bcm_ctlp,
	    bcmp,
	    BCM_SATA_RESET_ALL,
	    BCM_NO_FAILED_SLOT, /* failed slot */
	    BCM_NO_TIMEOUT_SLOT, /* timeout slot */
	    BCM_NO_ABORTED_SLOT, /* aborted slot */
	    BCM_NO_RESET_SLOT); /* reset tags */

	return (SATA_SUCCESS);
}

/*
 * Called by sata framework to reset a port(s) or device.
 */
static int
bcm_sata_reset(dev_info_t *dip, sata_device_t *sd)
{
	bcm_ctl_t *bcm_ctlp;
	bcm_port_t *bcmp;
	uint8_t port;
	int ret = SATA_SUCCESS;
	int instance = ddi_get_instance(dip);

	bcm_ctlp = ddi_get_soft_state(bcm_statep, instance);
	port = sd->satadev_addr.cport;

	BCMDBG1(BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_sata_reset enter: port: %d", port);

	switch (sd->satadev_addr.qual) {
	case SATA_ADDR_CPORT:
		/*FALLTHROUGH*/
	case SATA_ADDR_DCPORT:
		/* Port/device reset */
		bcmp = bcm_ctlp->bcmc_ports[port];

		mutex_enter(&bcmp->bcmp_mutex);
		if (bcmp->bcmp_state & SATA_PSTATE_FAILED |
		    bcmp->bcmp_state & SATA_PSTATE_SHUTDOWN |
		    bcmp->bcmp_state & SATA_PSTATE_PWROFF) {
			/*
			 * In case the targer driver would send the request
			 * before sata framework can have the opportunity to
			 * process those event reports.
			 */
			sd->satadev_state = bcmp->bcmp_state;
			bcm_update_sata_registers(bcm_ctlp, port, sd);
			BCMDBG1(BCMDBG_ERRS, bcm_ctlp,
			    "bcm_sata_reset returning SATA_FAILURE "
			    "while port in FAILED/SHUTDOWN/PWROFF state: "
			    "port: %d", port);
			mutex_exit(&bcmp->bcmp_mutex);
			ret = SATA_FAILURE;
			break;
		}

		if (bcmp->bcmp_device_type == SATA_DTYPE_NONE) {
			/*
			 * bcm_intr_phyrdy_change() may have rendered it to
			 * BCM_PORT_TYPE_NODEV.
			 */
			sd->satadev_type = SATA_DTYPE_NONE;
			sd->satadev_state = bcmp->bcmp_state;
			bcm_update_sata_registers(bcm_ctlp, port, sd);
			BCMDBG1(BCMDBG_ERRS, bcm_ctlp,
			    "bcm_sata_reset returning SATA_FAILURE "
			    "while no device attached: port: %d", port);
			mutex_exit(&bcmp->bcmp_mutex);
			ret = SATA_FAILURE;
			break;
		}

		ret = bcm_reset_port_reject_pkts(bcm_ctlp, bcmp, port);
		mutex_exit(&bcmp->bcmp_mutex);

		break;

	case SATA_ADDR_CNTRL:
		/* Reset the whole controller */
		BCMDBG0(BCMDBG_INFO, bcm_ctlp,
		    "bcm_sata_reset: controller reset not supported");
		break;

	case SATA_ADDR_PMPORT:
	case SATA_ADDR_DPMPORT:
		BCMDBG0(BCMDBG_INFO, bcm_ctlp,
		    "bcm_sata_reset: port multiplier will be "
		    "supported later");
		/* FALLTHRU */
	default:
		ret = SATA_FAILURE;
	}

	return (ret);
}

/*
 * Called by sata framework to activate a port as part of hotplug.
 * (cfgadm -c connect satax/y)
 * Note: Not port-mult aware.
 */
static int
bcm_sata_activate(dev_info_t *dip, sata_device_t *satadev)
{
	bcm_ctl_t *bcm_ctlp;
	bcm_port_t *bcmp;
	uint8_t port;
	int instance = ddi_get_instance(dip);
	bcm_ctlp = ddi_get_soft_state(bcm_statep, instance);
	port = satadev->satadev_addr.cport;

	BCMDBG1(BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_sata_activate port %d enter", port);

	bcmp = bcm_ctlp->bcmc_ports[port];

	mutex_enter(&bcmp->bcmp_mutex);

	/* Reset the port and find the signature */
	bcm_find_dev_signature(bcm_ctlp, bcmp, port);

	/* Try to start the port */
	if (bcm_start_port(bcm_ctlp, bcmp, port)
	    != BCM_SUCCESS) {
		satadev->satadev_state |= SATA_PSTATE_FAILED;
		BCMDBG1(BCMDBG_ERRS, bcm_ctlp,
		    "bcm_sata_activate: port %d failed "
		    "at start port", port);
	}

	bcmp->bcmp_state |= SATA_PSTATE_PWRON;
	bcmp->bcmp_state &= ~SATA_PSTATE_PWROFF;
	bcmp->bcmp_state &= ~SATA_PSTATE_SHUTDOWN;

	satadev->satadev_state = bcmp->bcmp_state;

	bcm_update_sata_registers(bcm_ctlp, port, satadev);

	mutex_exit(&bcmp->bcmp_mutex);
	return (SATA_SUCCESS);
}

/*
 * Called by sata framework to deactivate a port as part of hotplug.
 * (cfgadm -c disconnect satax/y)
 * Note: Not port-mult aware.
 */
static int
bcm_sata_deactivate(dev_info_t *dip, sata_device_t *satadev)
{
	bcm_ctl_t *bcm_ctlp;
	bcm_port_t *bcmp;
	uint8_t port;
	uint32_t port_scontrol;
	int instance = ddi_get_instance(dip);

	bcm_ctlp = ddi_get_soft_state(bcm_statep, instance);
	port = satadev->satadev_addr.cport;

	BCMDBG1(BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_sata_deactivate port %d enter", port);

	bcmp = bcm_ctlp->bcmc_ports[port];

	mutex_enter(&bcmp->bcmp_mutex);

	if (bcmp->bcmp_device_type == SATA_DTYPE_NONE) {
		goto phy_offline;
	}

	/* First to stop the port QDMA engine */
	(void) bcm_disable_port_QDMA_engine(bcm_ctlp, bcmp, port);

	/* Second to disable the interrupts on the port */
	bcm_disable_port_intrs(bcm_ctlp, port);

	/* Then to abort all the pending commands */
	bcm_reject_all_abort_pkts(bcm_ctlp, bcmp, port);

	/* Next put the PHY offline */

phy_offline:
	port_scontrol = ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_SATA_CONTROL(bcm_ctlp, port));

	port_scontrol |= (SCONTROL_DET_DISABLE - 1);
	ddi_put32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_SATA_CONTROL(bcm_ctlp, port),
	    port_scontrol);

	/* update qpi value */
	ddi_put32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QPI(bcm_ctlp, port),
	    0x0);
	bcmp->bcmp_qpi = 0;
	bcmp->bcmp_intr_idx = 0;
	/* No ATAPI command running */
	bcmp->bcmp_qdma_pkt_cmd_running = 0;

	/* Update bcmp_state */
	bcmp->bcmp_state = SATA_PSTATE_SHUTDOWN;
	satadev->satadev_state = bcmp->bcmp_state;

	bcm_update_sata_registers(bcm_ctlp, port, satadev);

	mutex_exit(&bcmp->bcmp_mutex);
	return (SATA_SUCCESS);
}

/*
 * To be used to mark all the outstanding pkts with SATA_PKT_ABORTED
 * when a device is unplugged or a port is deactivated.
 *
 * WARNING!!! bcmp_mutex should be acquired before the function is called.
 */
static void
bcm_reject_all_abort_pkts(bcm_ctl_t *bcm_ctlp,
    bcm_port_t *bcmp, uint8_t port)
{
	BCMDBG1(BCMDBG_ENTRY|BCMDBG_INTR, bcm_ctlp,
	    "bcm_reject_all_abort_pkts on port: %d", port);

	/*
	 * check if BCM_PORT_FLAG_MOPPING flag is set or not. no need
	 * to do it if it is.
	 */
	if (bcmp->bcmp_flags & BCM_PORT_FLAG_MOPPING) {
		BCMDBG1(BCMDBG_ERRS, bcm_ctlp,
		    "bcm_reject_all_abort_pkts return directly "
		    "port %d no needs to reject any outstanding "
		    "commands", port);
		return;
	}

	bcmp->bcmp_flags |= BCM_PORT_FLAG_MOPPING;
	bcmp->bcmp_mop_in_progress++;

	bcm_mop_commands(bcm_ctlp,
	    bcmp,
	    BCM_SATA_ABORT_ALL,
	    BCM_NO_FAILED_SLOT, /* failed slot */
	    BCM_NO_TIMEOUT_SLOT, /* timeout slot */
	    BCM_NO_ABORTED_SLOT, /* aborting slot */
	    BCM_NO_RESET_SLOT); /* reset slot */
}

/*
 * Allocate the ports structure, only called by bcm_attach
 */
static int
bcm_alloc_ports_state(bcm_ctl_t *bcm_ctlp)
{
	int port;

	BCMDBG0(BCMDBG_INIT|BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_alloc_ports_state enter");

	mutex_enter(&bcm_ctlp->bcmc_mutex);

	/* Allocate structures for ports */
	for (port = 0; port < bcm_ctlp->bcmc_num_ports; port++) {
		if (bcm_alloc_port_state(bcm_ctlp, port) != BCM_SUCCESS) {
			goto err_out;
		}
	}

	mutex_exit(&bcm_ctlp->bcmc_mutex);
	return (BCM_SUCCESS);

err_out:
	for (port--; port >= 0; port--) {
		bcm_dealloc_port_state(bcm_ctlp, port);
	}

	mutex_exit(&bcm_ctlp->bcmc_mutex);
	return (BCM_FAILURE);
}

/*
 * Reverse of bcm_alloc_ports_state(), only called by bcm_detach
 */
static void
bcm_dealloc_ports_state(bcm_ctl_t *bcm_ctlp)
{
	int port;

	mutex_enter(&bcm_ctlp->bcmc_mutex);
	for (port = 0; port < bcm_ctlp->bcmc_num_ports; port++) {
		bcm_dealloc_port_state(bcm_ctlp, port);
	}
	mutex_exit(&bcm_ctlp->bcmc_mutex);
}

/*
 * Initialize the controller and all ports. And then try to start the ports
 * if there are devices attached.
 *
 * This routine can be called from three separate cases: DDI_ATTACH,
 * PM_LEVEL_D0 and DDI_RESUME. The DDI_ATTACH case is different from
 * other two cases; device signature probing are attempted only during
 * DDI_ATTACH case.
 *
 * WARNING!!! Disable the whole controller's interrupts before calling and
 * the interrupts will be enabled upon successfully return.
 */
static int
bcm_init_ctl(bcm_ctl_t *bcm_ctlp)
{
	bcm_port_t *bcmp;
	int port;

	BCMDBG0(BCMDBG_INIT|BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_init_ctl enter");

	/* Initialize ports and structures */
	for (port = 0; port < bcm_ctlp->bcmc_num_ports; port++) {

		bcmp = bcm_ctlp->bcmc_ports[port];
		mutex_enter(&bcmp->bcmp_mutex);

		/*
		 * Ensure that the controller is not in the running state
		 * by checking every port's QDMA control register(QCR)
		 */
		if (bcm_init_port(bcm_ctlp, bcmp, port)
		    != BCM_SUCCESS) {
			BCMDBG1(BCMDBG_ERRS, bcm_ctlp,
			    "bcm_init_ctl: failed to "
			    "initialize port %d", port);
			/*
			 * Set the port state to SATA_PSTATE_FAILED if
			 * failed to initialize it.
			 */
			bcmp->bcmp_state = SATA_PSTATE_FAILED;
		}

		mutex_exit(&bcmp->bcmp_mutex);
	}

	mutex_enter(&bcm_ctlp->bcmc_mutex);

	/* Enable the whole controller interrupts */
	bcm_enable_all_intrs(bcm_ctlp);

	mutex_exit(&bcm_ctlp->bcmc_mutex);

	return (BCM_SUCCESS);
}

/*
 * Reverse of bcm_init_ctl()
 *
 * We only need to stop the ports and disable the interrupt.
 */
static void
bcm_uninit_ctl(bcm_ctl_t *bcm_ctlp)
{
	bcm_port_t *bcmp;
	int port;

	BCMDBG0(BCMDBG_INIT, bcm_ctlp,
	    "bcm_uninit_ctl enter");

	mutex_enter(&bcm_ctlp->bcmc_mutex);

	/* Disable all the interrupts. */
	bcm_disable_all_intrs(bcm_ctlp);

	mutex_exit(&bcm_ctlp->bcmc_mutex);

	for (port = 0; port < bcm_ctlp->bcmc_num_ports; port++) {

		bcmp = bcm_ctlp->bcmc_ports[port];

		mutex_enter(&bcmp->bcmp_mutex);
		/* Disable port interrupts */
		bcm_disable_port_intrs(bcm_ctlp, port);
		/* Disable port QDMA engine */
		(void) bcm_disable_port_QDMA_engine(bcm_ctlp,
		    bcmp, port);

		mutex_exit(&bcmp->bcmp_mutex);
	}
}

/*
 * The routine is to initialize the port. First disable port's QDMA
 * engine, then enable port interrupt and clear Serror register. And under
 * BCM_ATTACH case, find device signature and then try to start the port.
 *
 * WARNING!!! bcmp_mutex should be acquired before the function is called.
 */
static int
bcm_init_port(bcm_ctl_t *bcm_ctlp,
    bcm_port_t *bcmp, uint8_t port)
{

	BCMDBG1(BCMDBG_INIT|BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_init_port enter: port %d ", port);

	/*
	 * Check if the port's QDMA is disabled, if not, disable it.
	 */
	if (!(bcmp->bcmp_qdma_engine_flags & BCM_QDMA_QCR_ENABLE)) {
		goto init_port_next;
	}

	if (bcm_restart_port_wait_till_ready(bcm_ctlp, bcmp,
	    port, BCM_RESET_NO_EVENTS_UP|BCM_PORT_INIT, NULL) != BCM_SUCCESS)
		return (BCM_FAILURE);

init_port_next:
	BCMDBG1(BCMDBG_INIT, bcm_ctlp,
	    "port %d's QDMA is disabled now", port);

	/*
	 * For the time being, only probe ports/devices and get the types of
	 * attached devices during DDI_ATTACH. In fact, the device can be
	 * changed during power state changes, but at this moment, we don't
	 * support the situation.
	 */
	if (bcm_ctlp->bcmc_flags & BCM_ATTACH) {
		/* Try to get the device signature */
		bcm_find_dev_signature(bcm_ctlp, bcmp, port);
	} else {

		BCMDBG1(BCMDBG_PM, bcm_ctlp,
		    "bcm_init_port: port %d "
		    "reset the port during resume", port);
		(void) bcm_port_reset(bcm_ctlp, bcmp, port);
	}

	/* config port registers for QDMA engine */
	if (bcm_config_port_registers(bcm_ctlp, bcmp, port)
	    != BCM_SUCCESS) {
		BCMDBG1(BCMDBG_INIT, bcm_ctlp,
		    "failed to configure registers for port %d", port);
		return (BCM_FAILURE);
	}

	/* Return directly if no device connected */
	if (bcmp->bcmp_device_type == SATA_DTYPE_NONE) {
		BCMDBG1(BCMDBG_INIT, bcm_ctlp,
		    "No device connected to port %d", port);
		/* Enable port interrupts */
		bcm_enable_port_intrs(bcm_ctlp, port);
		goto init_port_out;
	}

	/* Try to start the port */
	if (bcm_start_port(bcm_ctlp, bcmp, port)
	    != BCM_SUCCESS) {
		BCMDBG1(BCMDBG_INIT, bcm_ctlp,
		    "failed to start port %d", port);
		return (BCM_FAILURE);
	}

init_port_out:
	return (BCM_SUCCESS);
}

/*
 * Figure out which chip and what mode it is running; Also check
 * the capabilities such as power management capability and etc..
 */
static int
bcm_config_space_init(bcm_ctl_t *bcm_ctlp)
{
	ushort_t venid, devid;
	ushort_t caps_ptr, cap_count, cap;
#if BCM_DEBUG
	ushort_t pmcap, pmcsr;
#endif

	/* Get vendor id from configuration space */
	venid = pci_config_get16(bcm_ctlp->bcmc_pci_conf_handle,
	    PCI_CONF_VENID);

	/* Get device id from configuration space */
	devid = pci_config_get16(bcm_ctlp->bcmc_pci_conf_handle,
	    PCI_CONF_DEVID);

	/*
	 * Check if it is Broadcom ht1000 sata controller.
	 */
	if ((venid != BCM_VENDOR_ID) || (devid != BCM_DEVICE_ID)) {
		BCMDBG2(BCMDBG_ERRS, bcm_ctlp,
		    "vendor id = 0x%x device id = 0x%x", venid, devid);
		return (BCM_FAILURE);
	}

	/*
	 * Check if capabilities list is supported and if so,
	 * get initial capabilities pointer and clear bits 0,1.
	 */
	if (pci_config_get16(bcm_ctlp->bcmc_pci_conf_handle,
	    PCI_CONF_STAT) & PCI_STAT_CAP) {
		caps_ptr = P2ALIGN(pci_config_get8(
		    bcm_ctlp->bcmc_pci_conf_handle,
		    PCI_CONF_CAP_PTR), 4);
	} else {
		caps_ptr = PCI_CAP_NEXT_PTR_NULL;
	}

	/*
	 * Walk capabilities if supported.
	 */
	for (cap_count = 0; caps_ptr != PCI_CAP_NEXT_PTR_NULL; ) {

		/*
		 * Check that we haven't exceeded the maximum number of
		 * capabilities and that the pointer is in a valid range.
		 */
		if (++cap_count > PCI_CAP_MAX_PTR) {
			BCMDBG0(BCMDBG_ERRS, bcm_ctlp,
			    "too many device capabilities");
			return (BCM_FAILURE);
		}
		if (caps_ptr < PCI_CAP_PTR_OFF) {
			BCMDBG1(BCMDBG_ERRS, bcm_ctlp,
			    "capabilities pointer 0x%x out of range",
			    caps_ptr);
			return (BCM_FAILURE);
		}

		/*
		 * Get next capability and check that it is valid.
		 * For now, we only support power management.
		 */
		cap = pci_config_get8(bcm_ctlp->bcmc_pci_conf_handle,
		    caps_ptr);
		switch (cap) {
		case PCI_CAP_ID_PM:

			/* power management supported */
			bcm_ctlp->bcmc_cap |= BCM_CAP_PM;

			/* Save PMCSR offset */
			bcm_ctlp->bcmc_pmcsr_offset = caps_ptr + PCI_PMCSR;

#if BCM_DEBUG
			pmcap = pci_config_get16(
			    bcm_ctlp->bcmc_pci_conf_handle,
			    caps_ptr + PCI_PMCAP);
			pmcsr = pci_config_get16(
			    bcm_ctlp->bcmc_pci_conf_handle,
			    bcm_ctlp->bcmc_pmcsr_offset);
			BCMDBG2(BCMDBG_PM, bcm_ctlp,
			    "Power Management capability found PCI_PMCAP "
			    "= 0x%x PCI_PMCSR = 0x%x", pmcap, pmcsr);
			if ((pmcap & 0x3) == 0x3)
				BCMDBG0(BCMDBG_PM, bcm_ctlp,
				    "PCI Power Management Interface "
				    "spec 1.2 compliant");
#endif
			break;

		case PCI_CAP_ID_MSI:
			bcm_ctlp->bcmc_cap |= BCM_CAP_MSI;
			BCMDBG0(BCMDBG_PM, bcm_ctlp, "MSI capability found");
			break;

		case PCI_CAP_ID_PCIX:
			bcm_ctlp->bcmc_cap |= BCM_CAP_PCIX;
			BCMDBG0(BCMDBG_PM, bcm_ctlp,
			    "PCI-X capability found");
			break;

		case PCI_CAP_ID_MSI_X:
			bcm_ctlp->bcmc_cap |= BCM_CAP_MSIX;
			BCMDBG0(BCMDBG_PM, bcm_ctlp,
			    "MSI-X capability found");
			break;

		case PCI_CAP_ID_SATA:
			bcm_ctlp->bcmc_cap |= BCM_CAP_SATA;
			BCMDBG0(BCMDBG_PM, bcm_ctlp,
			    "SATA capability found");
			break;

		case PCI_CAP_ID_VS:
			bcm_ctlp->bcmc_cap |= BCM_CAP_VS;
			BCMDBG0(BCMDBG_PM, bcm_ctlp,
			    "Vendor Specific capability found");
			break;

		default:
			BCMDBG1(BCMDBG_PM, bcm_ctlp,
			    "unrecognized capability 0x%x", cap);
			break;
		}

		/*
		 * Get next capabilities pointer and clear bits 0,1.
		 */
		caps_ptr = P2ALIGN(pci_config_get8(
		    bcm_ctlp->bcmc_pci_conf_handle,
		    (caps_ptr + PCI_CAP_NEXT_PTR)), 4);
	}

	return (BCM_SUCCESS);
}

/*
 * ht1000 port reset ...; the physical communication between the HBA and device
 * on a port are disabled.
 *
 * WARNING!!! bcmp_mutex should be acquired before the function is called
 */
static int
bcm_port_reset(bcm_ctl_t *bcm_ctlp, bcm_port_t *bcmp, uint8_t port)
{
	uint32_t port_scontrol, port_sstatus;
	uint8_t port_task_file_error, port_task_file_status;
	int loop_count, reset_count = 0;
	int rval = BCM_SUCCESS;
	int instance = ddi_get_instance(bcm_ctlp->bcmc_dip);
	bcmp->bcmp_state = 0;

	BCMDBG1(BCMDBG_INFO, bcm_ctlp,
	    "bcm_port_reset: do normal port reset on port %d", port);

	/*
	 * Clear signature registers
	 */
	ddi_put8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_SECT(bcm_ctlp, port), 0);
	ddi_put8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_LCYL(bcm_ctlp, port), 0);
	ddi_put8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_HCYL(bcm_ctlp, port), 0);
	ddi_put8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_COUNT(bcm_ctlp, port), 0);

port_reset:
	/* Do COMRESET */
	port_scontrol = ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_SATA_CONTROL(bcm_ctlp, port));
	SCONTROL_SET_DET(port_scontrol, SCONTROL_DET_COMRESET);

	ddi_put32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_SATA_CONTROL(bcm_ctlp, port),
	    port_scontrol);

	/* Give time for COMRESET to percolate */
	drv_usecwait(BCM_1MS_USECS * 2);

	/* Fetch the SCONTROL again and rewrite the DET part with 0 */
	port_scontrol = ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_SATA_CONTROL(bcm_ctlp, port));
	SCONTROL_SET_DET(port_scontrol, SCONTROL_DET_NOACTION);
	ddi_put32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_SATA_CONTROL(bcm_ctlp, port),
	    port_scontrol);

	loop_count = 0;
	do {
		port_sstatus = ddi_get32(bcm_ctlp->bcmc_bar_handle,
		    (uint32_t *)BCM_SATA_STATUS(bcm_ctlp, port));

		if (SSTATUS_GET_IPM(port_sstatus) != SSTATUS_IPM_ACTIVE) {
			/*
			 * If the interface is not active, the DET field
			 * is considered not accurate. So we want to
			 * continue looping.
			 */
			SSTATUS_SET_DET(port_sstatus, SSTATUS_DET_NODEV);
		}

		if (loop_count++ > BCM_POLLRATE_PORT_SSTATUS) {
			/*
			 * We are effectively timing out after 0.1 sec.
			 */
			break;
		}

		/* Wait for 10 millisec */
		drv_usecwait(BCM_2MS_USECS * 5);
	} while (SSTATUS_GET_DET(port_sstatus) != SSTATUS_DET_DEVPRE_PHYCOM);

	BCMDBG3(BCMDBG_INIT|BCMDBG_POLL_LOOP, bcm_ctlp,
	    "bcm_port_reset: 1st loop count: %d, "
	    "port_sstatus = 0x%x port %d",
	    loop_count, port_sstatus, port);

	if ((SSTATUS_GET_IPM(port_sstatus) != SSTATUS_IPM_ACTIVE) ||
	    (SSTATUS_GET_DET(port_sstatus) != SSTATUS_DET_DEVPRE_PHYCOM)) {
		/*
		 * Either the port is not active or there
		 * is no device present.
		 */
		bcmp->bcmp_device_type = SATA_DTYPE_NONE;
		goto out;
	}

	/*
	 * Next check whether COMRESET is completed successfully
	 */
	loop_count = 0;
	do {
		port_task_file_error = ddi_get8(bcm_ctlp->bcmc_bar_handle,
		    (uint8_t *)BCM_ERROR(bcm_ctlp, port));

		/*
		 * The Error bit '1' means COMRESET is finished successfully
		 * The device hardware has been initialized and the power-up
		 * diagnostics successfully completed.
		 */
		if (port_task_file_error == 0x1) {
			goto out_check;
		}

		if (loop_count++ > BCM_POLLRATE_PORT_TFD_ERROR) {
			/*
			 * We are effectively timing out after 11 sec.
			 */
			break;
		}

		/* Wait for 10 millisec */
		drv_usecwait(BCM_2MS_USECS * 5);
	} while (port_task_file_error != 0x1);

	BCMDBG3(BCMDBG_ERRS, bcm_ctlp, "bcm_port_reset: 2nd loop "
	    "count: %d, port_task_file_error = 0x%x port %d",
	    loop_count, port_task_file_error, port);

	/* Try four more time resets if COMRESET is not finished successfully */
	if (reset_count < BCM_POLLRATE_PORT_RESET) {
		reset_count ++;
		BCMDBG1(BCMDBG_ERRS, bcm_ctlp, "bcm_port_reset: reset_count "
		    "%d", reset_count);
		goto port_reset;
	}

	/* Reset port failed */
	bcmp->bcmp_state |= SATA_PSTATE_FAILED;
	rval = BCM_FAILURE;

out:
	/* Clear port serror and qsr registers for the port */
	ddi_put8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_ERROR(bcm_ctlp, port), 0);
	ddi_put32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_SATA_ERROR(bcm_ctlp, port),
	    BCM_SER_CLEAR_ALL);
	ddi_put32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QSR(bcm_ctlp, port),
	    BCM_QSR_CLEAR_ALL);
	ddi_put32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QCI(bcm_ctlp, port),
	    BCM_IDX_INIT_VALUE);
	ddi_put32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QPI(bcm_ctlp, port),
	    BCM_IDX_INIT_VALUE);
	bcmp->bcmp_qpi = BCM_IDX_INIT_VALUE;
	bcmp->bcmp_intr_idx = BCM_IDX_INIT_VALUE;
	/* No ATAPI command running */
	bcmp->bcmp_qdma_pkt_cmd_running = 0;
	if (rval == BCM_SUCCESS)
		bcmp->bcmp_state |= SATA_PSTATE_PWRON;
	return (rval);

out_check:
	/*
	 * Check device status, if keep busy or COMRESET error
	 * do device reset to patch some SATA disks' issue
	 */
	port_task_file_status = ddi_get8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_STATUS(bcm_ctlp, port));
	BCMDBG2(BCMDBG_INFO, bcm_ctlp, "bcm_port_reset: port %d "
	    "port_task_file_status 0x%x", port, port_task_file_status);

	if (port_task_file_status & BCM_TFD_STS_BSY ||
	    port_task_file_status & BCM_TFD_STS_DRQ) {
		ddi_put8(bcm_ctlp->bcmc_bar_handle,
		    (uint8_t *)BCM_DEVCTL(bcm_ctlp, port),
		    BCM_TFD_CTL_SRST);
		BCM_DELAY_NSEC(5000)
		ddi_put8(bcm_ctlp->bcmc_bar_handle,
		    (uint8_t *)BCM_DEVCTL(bcm_ctlp, port), 0);
		BCM_DELAY_NSEC(1000)
		if (port_task_file_status & BCM_TFD_STS_BSY ||
		    port_task_file_status & BCM_TFD_STS_DRQ) {
			cmn_err(CE_WARN, "!bcm%d: bcm_port_reset: port %d "
			    "BSY/DRQ still set after device reset "
			    "port_task_file_status = 0x%x", instance,
			    port, port_task_file_status);
			bcmp->bcmp_state |= SATA_PSTATE_FAILED;
			rval = BCM_FAILURE;
		}
	}
	goto out;
}

/*
 * This routine is only called from BCM_ATTACH or phyrdy change
 * case. It first calls port reset to initialize port, probe port and probe
 * device, then try to read registers to find the type of device
 * attached to the port.
 *
 * WARNING!!! bcmp_mutex should be acquired before the function
 * is called. And the port interrupt is disabled.
 */
static void
bcm_find_dev_signature(bcm_ctl_t *bcm_ctlp,
    bcm_port_t *bcmp, uint8_t port)
{
	uint32_t signature;

	BCMDBG1(BCMDBG_INIT|BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_find_dev_signature enter: port %d", port);

	bcmp->bcmp_device_type = SATA_DTYPE_UNKNOWN;

	/* Reset the port so that the signature can be retrieved */
	(void) bcm_port_reset(bcm_ctlp, bcmp, port);

	if (bcmp->bcmp_device_type == SATA_DTYPE_NONE) {
		BCMDBG1(BCMDBG_INFO, bcm_ctlp,
		    "bcm_find_dev_signature: No device is found "
		    "at port %d", port);
		return;
	}

	/* Check the port state, return if it is in failed state */
	if (bcmp->bcmp_state & SATA_PSTATE_FAILED) {
		BCMDBG2(BCMDBG_ERRS, bcm_ctlp,
		    "bcm_find_dev_signature: port %d state 0x%x",
		    port, bcmp->bcmp_state);
		return;
	}

	/* Retrieve and assemble signature from port registers */
	signature = ddi_get8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_COUNT(bcm_ctlp, port));
	signature |= (ddi_get8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_SECT(bcm_ctlp, port)) << 8);
	signature |= (ddi_get8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_LCYL(bcm_ctlp, port)) << 16);
	signature |= (ddi_get8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_HCYL(bcm_ctlp, port)) << 24);

	BCMDBG2(BCMDBG_INIT|BCMDBG_INFO, bcm_ctlp,
	    "bcm_find_dev_signature: port %d signature = 0x%x",
	    port, signature);

	/* Determine what device is connected to the controller */
	switch (signature) {

	case BCM_SIGNATURE_DISK:
		bcmp->bcmp_device_type = SATA_DTYPE_ATADISK;
		BCMDBG1(BCMDBG_INFO, bcm_ctlp,
		    "Disk is found at port: %d", port);
		break;

	case BCM_SIGNATURE_ATAPI:
		bcmp->bcmp_device_type = SATA_DTYPE_ATAPI;
		BCMDBG1(BCMDBG_INFO, bcm_ctlp,
		    "ATAPI device is found at port: %d", port);
		break;

	default:
		bcmp->bcmp_device_type = SATA_DTYPE_UNKNOWN;
		BCMDBG1(BCMDBG_INFO, bcm_ctlp,
		    "Unknown device is found at port: %d", port);
	}
}

/*
 * initialize port registers for QDMA engine
 *
 * WARNING!!! bcmp_mutex should be acquired before the function
 * is called. And the port interrupt is disabled.
 */
static int
bcm_config_port_registers(bcm_ctl_t *bcm_ctlp,
    bcm_port_t *bcmp, uint8_t port)
{
	BCMDBG1(BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_config_port_registers enter: port %d", port);

	/* check if QDMA is disabled. */
	if (bcmp->bcmp_qdma_engine_flags & BCM_QDMA_QCR_ENABLE) {
		BCMDBG2(BCMDBG_ERRS, bcm_ctlp, "bcm_config_port_registers "
		    "failed the QDMA engine flags for port %d is %d",
		    port, bcmp->bcmp_qdma_engine_flags);
		return (BCM_FAILURE);
	}

	/* initialize port's QPI register */
	ddi_put32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QPI(bcm_ctlp, port),
	    BCM_IDX_INIT_VALUE);

	/* Initialize port's QCI register */
	ddi_put32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QCI(bcm_ctlp, port),
	    BCM_IDX_INIT_VALUE);

	/* initialize port's queue depth register(QDR) */
	ddi_put32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QDR(bcm_ctlp, port),
	    BCM_CTL_QUEUE_DEPTH - 1);

	BCMDBG2(BCMDBG_INFO, bcm_ctlp,
	    "bcmp->bcmp_cmd_queue_dma_cookie._dmu._dmac_la[0] = 0x%x "
	    "bcmp->bcmp_cmd_queue_dma_cookie._dmu._dmac_la[1] = 0x%x ",
	    bcmp->bcmp_cmd_queue_dma_cookie._dmu._dmac_la[0],
	    bcmp->bcmp_cmd_queue_dma_cookie._dmu._dmac_la[1]);

	/*
	 * Config Port Command Descriptor Queue Base Address, low 32 bits set
	 * to QAL, high 15 bits set to QAU
	 */
	ddi_put32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QAL(bcm_ctlp, port),
	    (uint32_t)(bcmp->bcmp_cmd_queue_dma_cookie._dmu._dmac_la[0]
	    & BCM_CMD_QUEUE_BASE_ADDR_MASK_LOW));

	ddi_put32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QAU(bcm_ctlp, port),
	    (uint32_t)(bcmp->bcmp_cmd_queue_dma_cookie._dmu._dmac_la[1]
	    & BCM_CMD_QUEUE_BASE_ADDR_MASK_HIGH));

	/* set simr to BCM_SER_PHY_RDY_CHG to allow hotplug */
	ddi_put32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_SATA_SIMR(bcm_ctlp, port),
	    BCM_SER_PHY_RDY_CHG);

	/* set qmr to 0x21 suggested by Broadcom */
	ddi_put32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QMR(bcm_ctlp, port),
	    BCM_QDMA_CMD_CPLT | BCM_QDMA_CMD_ERR);

	return (BCM_SUCCESS);
}

/*
 * Each port contains one QDMA engine. The QDMA engine walks through the
 * command descriptor queue to execute commands in the queue. QDMA engine
 * is controlled by QDMA controller register(QCR).
 *
 * WARNING!!! bcmp_mutex should be acquired before the function
 * is called.
 */
static int
bcm_start_port(bcm_ctl_t *bcm_ctlp, bcm_port_t *bcmp, uint8_t port)
{
	uint32_t qpi, qci;

	BCMDBG1(BCMDBG_ENTRY, bcm_ctlp, "bcm_start_port enter: port %d", port);

	if (bcmp->bcmp_state & SATA_PSTATE_FAILED) {
		BCMDBG2(BCMDBG_ERRS, bcm_ctlp, "bcm_start_port failed "
		    "the state for port %d is 0x%x",
		    port, bcmp->bcmp_state);
		return (BCM_FAILURE);
	}

	if (bcmp->bcmp_device_type == SATA_DTYPE_NONE) {
		BCMDBG1(BCMDBG_ERRS, bcm_ctlp, "bcm_start_port failed "
		    "no device is attached at port %d", port);
		return (BCM_FAILURE);
	}

	/* Reset counters if it needs to */
	qci = ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QCI(bcm_ctlp, port));
	if (qci != BCM_IDX_INIT_VALUE) {
		ddi_put32(bcm_ctlp->bcmc_bar_handle,
		    (uint32_t *)BCM_QDMA_QCI(bcm_ctlp, port),
		    BCM_IDX_INIT_VALUE);
	}

	qpi = ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QPI(bcm_ctlp, port));
	if (qpi != BCM_IDX_INIT_VALUE) {
		ddi_put32(bcm_ctlp->bcmc_bar_handle,
		    (uint32_t *)BCM_QDMA_QPI(bcm_ctlp, port),
		    BCM_IDX_INIT_VALUE);
	}
	bcmp->bcmp_qpi = BCM_IDX_INIT_VALUE;
	bcmp->bcmp_intr_idx = BCM_IDX_INIT_VALUE;
	/* No ATAPI command running */
	bcmp->bcmp_qdma_pkt_cmd_running = 0;

	/* Enable port interrupts */
	bcm_enable_port_intrs(bcm_ctlp, port);

	/* Enable QDMA engine */
	(void) bcm_enable_port_QDMA_engine(bcm_ctlp, bcmp, port);

	bcmp->bcmp_flags |= BCM_PORT_FLAG_STARTED;

	return (BCM_SUCCESS);
}

/*
 * Allocate the port state for a port and initialize it.
 *
 * WARNING!!! bcmc_mutex should be acquired before the function
 * is called.
 */
static int
bcm_alloc_port_state(bcm_ctl_t *bcm_ctlp, uint8_t port)
{
	bcm_port_t *bcmp;
	bcm_nonqdma_cmd_t *nonqdma_cmd;

	BCMDBG1(BCMDBG_INIT|BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_alloc_port_state enter for port %d", port);

	/* Allocate memory for port state */
	bcmp =
	    (bcm_port_t *)kmem_zalloc(sizeof (bcm_port_t), KM_SLEEP);
	bcm_ctlp->bcmc_ports[port] = bcmp;

	bcmp->bcmp_num = port;

	bcmp->bcmp_state = 0;

	bcmp->bcmp_flags = 0;

	/* Allocate memory for non qdma command */
	nonqdma_cmd =
	    (bcm_nonqdma_cmd_t *)kmem_zalloc(sizeof (bcm_nonqdma_cmd_t),
	    KM_SLEEP);
	bcmp->bcmp_nonqdma_cmd = nonqdma_cmd;
	bcmp->bcmp_nonqdma_cmd->bcm_spkt = NULL;

	for (int i = 0; i < BCM_CTL_QUEUE_DEPTH; i++) {
		bcmp->bcmp_slot_pkts[i] = NULL;
	}

	bcmp->bcmp_qpi = BCM_IDX_INIT_VALUE;

	bcmp->bcmp_intr_idx = BCM_IDX_INIT_VALUE;

	bcmp->bcmp_qdma_engine_flags = 0;

	/* ATAPI command running flag */
	bcmp->bcmp_qdma_pkt_cmd_running = 0;

	/* ATAPI request sense packet in case of error */
	bcmp->bcmp_err_retri_pkt = NULL;

	/* Initialize the port condition variable */
	cv_init(&bcmp->bcmp_cv, NULL, CV_DRIVER, NULL);

	/* Initialize the port mutex */
	mutex_init(&bcmp->bcmp_mutex, NULL, MUTEX_DRIVER,
	    (void *)(uintptr_t)bcm_ctlp->bcmc_intr_pri);

	mutex_enter(&bcmp->bcmp_mutex);

	/*
	 * Allocate memory for command descriptor queue for this port
	 */
	if (bcm_alloc_cmd_descriptor_queue(bcm_ctlp, bcmp) != BCM_SUCCESS) {
		goto err_case1;
	}

	/*
	 * Allocate memory for prd tables when QDMA is used
	 */
	if (bcm_alloc_prdts_qdma(bcm_ctlp, bcmp) != BCM_SUCCESS) {
		goto err_case2;
	}

	/*
	 * Allocate memory for CDB block for ATAPI commands
	 */
	if (bcm_alloc_atapi_cdb(bcm_ctlp, bcmp) != BCM_SUCCESS) {
		goto err_case3;
	}

	/* Allocate memory for event arguments */
	bcmp->bcmp_event_args =
	    kmem_zalloc(sizeof (bcm_event_arg_t), KM_SLEEP);

	if (bcmp->bcmp_event_args == NULL)
		goto err_case4;

	mutex_exit(&bcmp->bcmp_mutex);

	return (BCM_SUCCESS);

err_case4:
	/* For CDB in ATAPI commands */
	bcm_dealloc_atapi_cdb(bcmp);
err_case3:
	bcm_dealloc_prdts_qdma(bcmp);
err_case2:
	bcm_dealloc_cmd_descriptor_queue(bcmp);
err_case1:
	mutex_exit(&bcmp->bcmp_mutex);
	mutex_destroy(&bcmp->bcmp_mutex);
	cv_destroy(&bcmp->bcmp_cv);

	kmem_free(bcmp, sizeof (bcm_port_t));

	return (BCM_FAILURE);
}

/*
 * Reverse of bcm_dealloc_port_state().
 *
 * WARNING!!! bcmc_mutex should be acquired before the function
 * is called.
 */
static void
bcm_dealloc_port_state(bcm_ctl_t *bcm_ctlp, uint8_t port)
{
	bcm_port_t *bcmp = bcm_ctlp->bcmc_ports[port];

	ASSERT(bcmp != NULL);

	mutex_enter(&bcmp->bcmp_mutex);
	kmem_free(bcmp->bcmp_event_args, sizeof (bcm_event_arg_t));
	bcmp->bcmp_event_args = NULL;
	bcm_dealloc_atapi_cdb(bcmp);
	bcm_dealloc_prdts_qdma(bcmp);
	bcm_dealloc_cmd_descriptor_queue(bcmp);
	mutex_exit(&bcmp->bcmp_mutex);

	mutex_destroy(&bcmp->bcmp_mutex);
	cv_destroy(&bcmp->bcmp_cv);

	kmem_free(bcmp, sizeof (bcm_port_t));

	bcm_ctlp->bcmc_ports[port] = NULL;
}

/*
 * Allocates memory for the Command Descriptor Queue that contains 16 entries.
 * Future NCQ will require more than 32 entries in queue depth.
 *
 * WARNING!!! bcmp_mutex should be acquired before the function is called.
 */
static int
bcm_alloc_cmd_descriptor_queue(bcm_ctl_t *bcm_ctlp, bcm_port_t *bcmp)
{
	size_t queue_size;
	size_t ret_len;
	uint_t cookie_count;

	queue_size = BCM_CTL_QUEUE_DEPTH * BCM_CMD_DESCRIPTOR_SIZE;

	/* allocate cmd descriptor queue dma handle. */
	if (ddi_dma_alloc_handle(bcm_ctlp->bcmc_dip,
	    &bcm_ctlp->bcmc_cmd_queue_dma_attr,
	    DDI_DMA_SLEEP,
	    NULL,
	    &bcmp->bcmp_cmd_queue_dma_handle) != DDI_SUCCESS) {

		BCMDBG0(BCMDBG_INIT, bcm_ctlp,
		    "cmd queue dma handle alloc failed");
		return (BCM_FAILURE);
	}

	if (ddi_dma_mem_alloc(bcmp->bcmp_cmd_queue_dma_handle,
	    queue_size,
	    &accattr,
	    DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
	    DDI_DMA_SLEEP,
	    NULL,
	    (caddr_t *)&bcmp->bcmp_cmd_queue,
	    &ret_len,
	    &bcmp->bcmp_cmd_queue_acc_handle) != NULL) {

		BCMDBG0(BCMDBG_INIT, bcm_ctlp,
		    "cmd queue dma mem alloc fail");
		/* error.. free the dma handle. */
		ddi_dma_free_handle(&bcmp->bcmp_cmd_queue_dma_handle);
		return (BCM_FAILURE);
	}

	if (ddi_dma_addr_bind_handle(bcmp->bcmp_cmd_queue_dma_handle,
	    NULL,
	    (caddr_t)bcmp->bcmp_cmd_queue,
	    queue_size,
	    DDI_DMA_CONSISTENT,
	    DDI_DMA_SLEEP,
	    NULL,
	    &bcmp->bcmp_cmd_queue_dma_cookie,
	    &cookie_count) !=  DDI_DMA_MAPPED) {

		BCMDBG0(BCMDBG_INIT, bcm_ctlp,
		    "cmd queue dma handle bind fail");
		/*  error.. free the dma handle & free the memory. */
		ddi_dma_mem_free(&bcmp->bcmp_cmd_queue_acc_handle);
		ddi_dma_free_handle(&bcmp->bcmp_cmd_queue_dma_handle);
		return (BCM_FAILURE);
	}

	bzero((void *)bcmp->bcmp_cmd_queue, queue_size);

	return (BCM_SUCCESS);
}

/*
 * Allocates memory for the prd tables used in command descriptors in the
 * queue when qdma engine is used. At this moment it has 16 prd tables and
 * each table has 257 prd entries.
 *
 * WARNING!!! bcmp_mutex should be acquired before the function is called.
 */
static int
bcm_alloc_prdts_qdma(bcm_ctl_t *bcm_ctlp, bcm_port_t *bcmp)
{
	size_t table_size;
	size_t ret_len;
	uint_t cookie_count;
	int i;

	table_size = (BCM_PRDT_NUMBER) * (sizeof (bcm_prde_t));

	for (i = 0; i < BCM_CTL_QUEUE_DEPTH; i++) {

		/* allocate prt table dma handle. */
		if (ddi_dma_alloc_handle(bcm_ctlp->bcmc_dip,
		    &bcm_ctlp->bcmc_prdt_qdma_dma_attr,
		    DDI_DMA_SLEEP,
		    NULL,
		    &bcmp->bcmp_prdts_qdma_dma_hdl[i]) != DDI_SUCCESS) {

			BCMDBG0(BCMDBG_INIT, bcm_ctlp,
			    "prd table dma handle alloc failed");
			return (BCM_FAILURE);
		}

		if (ddi_dma_mem_alloc(bcmp->bcmp_prdts_qdma_dma_hdl[i],
		    table_size,
		    &accattr,
		    DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
		    DDI_DMA_SLEEP,
		    NULL,
		    (caddr_t *)&bcmp->bcmp_prdts_qdma_dma_vaddr[i],
		    &ret_len,
		    &bcmp->bcmp_prdts_qdma_acc_hdl[i]) != NULL) {

			BCMDBG0(BCMDBG_INIT, bcm_ctlp,
			    "prd table dma mem alloc fail");
			/* error.. free the dma handle. */
			ddi_dma_free_handle(&bcmp->bcmp_prdts_qdma_dma_hdl[i]);
			return (BCM_FAILURE);
		}

		if (ddi_dma_addr_bind_handle(bcmp->bcmp_prdts_qdma_dma_hdl[i],
		    NULL,
		    (caddr_t)bcmp->bcmp_prdts_qdma_dma_vaddr[i],
		    table_size,
		    DDI_DMA_CONSISTENT,
		    DDI_DMA_SLEEP,
		    NULL,
		    &bcmp->bcmp_prdts_qdma_dma_cookie[i],
		    &cookie_count) !=  DDI_DMA_MAPPED) {

			BCMDBG0(BCMDBG_INIT, bcm_ctlp,
			    "prd table dma handle bind fail");
			/*  error.. free the dma handle & free the memory. */
			ddi_dma_mem_free(&bcmp->bcmp_prdts_qdma_acc_hdl[i]);
			ddi_dma_free_handle(&bcmp->bcmp_prdts_qdma_dma_hdl[i]);
			return (BCM_FAILURE);
		}

		bzero((void *)bcmp->bcmp_prdts_qdma_dma_vaddr[i], table_size);
	}

	return (BCM_SUCCESS);
}

/*
 * Allocates memory for the CDB for used in command descriptors in the
 * queue when ATAPI command is issued. At this moment 16 CDB blocks was
 * allocated. One for each descriptor in the queue. Each CDB has 16 bytes.
 *
 * WARNING!!! bcmp_mutex should be acquired before the function is called.
 */
static int
bcm_alloc_atapi_cdb(bcm_ctl_t *bcm_ctlp, bcm_port_t *bcmp)
{
	size_t ret_len;
	uint_t cookie_count;
	int i;
	size_t cdb_size = SATA_ATAPI_MAX_CDB_LEN;

	for (i = 0; i < BCM_CTL_QUEUE_DEPTH; i++) {

		/* allocate ATAPI CDB block dma handle. */
		if (ddi_dma_alloc_handle(bcm_ctlp->bcmc_dip,
		    &bcm_ctlp->bcmc_atapi_cdb_dma_attr,
		    DDI_DMA_SLEEP,
		    NULL,
		    &bcmp->bcmp_qdma_cdb_dma_hdl[i]) != DDI_SUCCESS) {

			BCMDBG0(BCMDBG_INIT, bcm_ctlp,
			    "cdb block dma handle alloc failed");
			/* deallocate the cdbs already allocated */
			for (int j = 0; j < i; j++) {
				(void) ddi_dma_unbind_handle
				    (bcmp->bcmp_qdma_cdb_dma_hdl[j]);
				ddi_dma_mem_free
				    (&bcmp->bcmp_qdma_cdb_acc_hdl[j]);
				ddi_dma_free_handle
				    (&bcmp->bcmp_qdma_cdb_dma_hdl[j]);
			}
			return (BCM_FAILURE);
		}

		if (ddi_dma_mem_alloc(bcmp->bcmp_qdma_cdb_dma_hdl[i],
		    cdb_size,
		    &accattr,
		    DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
		    DDI_DMA_SLEEP,
		    NULL,
		    (caddr_t *)&bcmp->bcmp_qdma_atapi_cdb_vaddr[i],
		    &ret_len,
		    &bcmp->bcmp_qdma_cdb_acc_hdl[i]) != NULL) {

			BCMDBG0(BCMDBG_INIT, bcm_ctlp,
			    "cdb block dma mem alloc fail");
			/* error.. free the dma handle. */
			ddi_dma_free_handle
			    (&bcmp->bcmp_qdma_cdb_dma_hdl[i]);
			/* deallocate the cdbs already allocated */
			for (int j = 0; j < i; j++) {
				(void) ddi_dma_unbind_handle
				    (bcmp->bcmp_qdma_cdb_dma_hdl[j]);
				ddi_dma_mem_free
				    (&bcmp->bcmp_qdma_cdb_acc_hdl[j]);
				ddi_dma_free_handle
				    (&bcmp->bcmp_qdma_cdb_dma_hdl[j]);
			}
			return (BCM_FAILURE);
		}

		if (ddi_dma_addr_bind_handle(bcmp->bcmp_qdma_cdb_dma_hdl[i],
		    NULL,
		    (caddr_t)bcmp->bcmp_qdma_atapi_cdb_vaddr[i],
		    cdb_size,
		    DDI_DMA_CONSISTENT,
		    DDI_DMA_SLEEP,
		    NULL,
		    &bcmp->bcmp_qdma_cdb_dma_cookie[i],
		    &cookie_count) !=  DDI_DMA_MAPPED) {

			BCMDBG0(BCMDBG_INIT, bcm_ctlp,
			    "cdb block dma handle bind fail");
			/* error. free the dma handle & free the memory. */
			ddi_dma_mem_free(&bcmp->bcmp_qdma_cdb_acc_hdl[i]);
			ddi_dma_free_handle(&bcmp->bcmp_qdma_cdb_dma_hdl[i]);
			/* deallocate the cdbs already allocated */
			for (int j = 0; j < i; j++) {
				(void) ddi_dma_unbind_handle
				    (bcmp->bcmp_qdma_cdb_dma_hdl[j]);
				ddi_dma_mem_free
				    (&bcmp->bcmp_qdma_cdb_acc_hdl[j]);
				ddi_dma_free_handle
				    (&bcmp->bcmp_qdma_cdb_dma_hdl[j]);
			}
			return (BCM_FAILURE);
		}

		bzero((void *)bcmp->bcmp_qdma_atapi_cdb_vaddr[i], cdb_size);
	}

	return (BCM_SUCCESS);
}

/*
 * Deallocates the Command Descriptor Queue
 *
 * WARNING!!! bcmp_mutex should be acquired before the function
 * is called.
 */
static void
bcm_dealloc_cmd_descriptor_queue(bcm_port_t *bcmp)
{
	/* Unbind the cmd descriptor queue dma handle first. */
	(void) ddi_dma_unbind_handle(bcmp->bcmp_cmd_queue_dma_handle);

	/* Then free the underlying memory. */
	ddi_dma_mem_free(&bcmp->bcmp_cmd_queue_acc_handle);

	/* Now free the handle itself. */
	ddi_dma_free_handle(&bcmp->bcmp_cmd_queue_dma_handle);
}

/*
 * Deallocates the prd tables in case of qdma
 *
 * WARNING!!! bcmp_mutex should be acquired before the function
 * is called.
 */
static void
bcm_dealloc_prdts_qdma(bcm_port_t *bcmp)
{
	int i;

	for (i = 0; i < BCM_CTL_QUEUE_DEPTH; i++) {
		/* Unbind the prd table dma handle first. */
		(void) ddi_dma_unbind_handle
		    (bcmp->bcmp_prdts_qdma_dma_hdl[i]);

		/* Then free the underlying memory. */
		ddi_dma_mem_free(&bcmp->bcmp_prdts_qdma_acc_hdl[i]);

		/* Now free the handle itself. */
		ddi_dma_free_handle(&bcmp->bcmp_prdts_qdma_dma_hdl[i]);
	}
}

/*
 * Deallocates the CDB blocks for ATAPI commands in qdma mode.
 *
 * WARNING!!! bcmp_mutex should be acquired before the function
 * is called.
 */
static void
bcm_dealloc_atapi_cdb(bcm_port_t *bcmp)
{
	int i;
	for (i = 0; i < BCM_CTL_QUEUE_DEPTH; i++) {
		/* Unbind the cdb block dma handle first. */
		(void) ddi_dma_unbind_handle
		    (bcmp->bcmp_qdma_cdb_dma_hdl[i]);

		/* Then free the underlying memory. */
		ddi_dma_mem_free(&bcmp->bcmp_qdma_cdb_acc_hdl[i]);

		/* Now free the handle itself. */
		ddi_dma_free_handle(&bcmp->bcmp_qdma_cdb_dma_hdl[i]);
	}
}

/*
 * WARNING!!! bcmp_mutex should be acquired before the function
 * is called.
 */
static void
bcm_update_sata_registers(bcm_ctl_t *bcm_ctlp, uint8_t port,
    sata_device_t *sd)
{
	BCMDBG1(BCMDBG_INIT|BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_update_sata_registers enter: port %d", port);

	sd->satadev_scr.sstatus =
	    ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)(BCM_SATA_STATUS(bcm_ctlp, port)));
	sd->satadev_scr.serror =
	    ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)(BCM_SATA_ERROR(bcm_ctlp, port)));
	sd->satadev_scr.scontrol =
	    ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)(BCM_SATA_CONTROL(bcm_ctlp, port)));
	sd->satadev_scr.sactive =
	    ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)(BCM_SATA_ACTIVE(bcm_ctlp, port)));
}

/*
 * Interrupt service routine for non qdma commands. It will be called to emulate
 * the interrupt for poll mode too.
 */
static void
bcm_intr_non_qdma_cmd(bcm_ctl_t *bcm_ctlp, bcm_port_t *bcmp,
    uint8_t port)
{
	sata_pkt_t *spkt;
	sata_cmd_t *sata_cmdp;
	int direction;

	BCMDBG1(BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_intr_non_qdma_cmd enter: port %d", port);

	mutex_enter(&bcmp->bcmp_mutex);
	if (bcmp->bcmp_flags & BCM_PORT_FLAG_RQSENSE &&
	    bcmp->bcmp_err_retri_pkt != NULL) {
		spkt = bcmp->bcmp_err_retri_pkt;
	} else {
		spkt = bcmp->bcmp_nonqdma_cmd->bcm_spkt;
	}

	sata_cmdp = &spkt->satapkt_cmd;
	direction = sata_cmdp->satacmd_flags.sata_data_direction;

	BCMDBG3(BCMDBG_INFO, bcm_ctlp,
	    "bcm_intr_non_qdma_cmd: port %d spkt 0x%p, direction %d",
	    port, spkt, direction)

	if (spkt->satapkt_cmd.satacmd_cmd_reg == SATAC_PACKET ||
	    bcmp->bcmp_flags & BCM_PORT_FLAG_RQSENSE) {
		bcm_intr_non_qdma_pkt_cmd(bcm_ctlp, bcmp, port);
	} else if (direction == SATA_DIR_READ) {
		bcm_intr_pio_in(bcm_ctlp, bcmp, port);
	} else if (direction == SATA_DIR_WRITE) {
		bcm_intr_pio_out(bcm_ctlp, bcmp, port);
	} else if (direction == SATA_DIR_NODATA_XFER) {
		bcm_intr_nodata(bcm_ctlp, bcmp, port);
	}
	mutex_exit(&bcmp->bcmp_mutex);
}

/*
 * Interrupt service routine for both qdma and non-qdma commands.
 */
static void
bcm_port_intr(bcm_ctl_t *bcm_ctlp, bcm_port_t *bcmp, uint8_t port)
{
	uint32_t port_qsr, port_ser;

	BCMDBG1(BCMDBG_INTR|BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_port_intr enter: port %d", port);

	mutex_enter(&bcmp->bcmp_mutex);

	/* Non-QDMA command interrupt handling */
	if (bcmp->bcmp_qdma_engine_flags & BCM_QDMA_QCR_PAUSE) {
		BCMDBG0(BCMDBG_INFO, bcm_ctlp,
		    "non-queued command intr handling");
		mutex_exit(&bcmp->bcmp_mutex);
		(void) bcm_intr_non_qdma_cmd(bcm_ctlp,
		    bcmp, port);
		return;
	}

	/* read and clear QSR and SER */
	port_qsr = ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QSR(bcm_ctlp, port));
	port_ser = ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_SATA_ERROR(bcm_ctlp, port));
	ddi_put32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QSR(bcm_ctlp, port), port_qsr);
	ddi_put32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_SATA_ERROR(bcm_ctlp, port), port_ser);

	/* QDMA command interrupt handling */
	if (port_qsr == BCM_QDMA_CMD_CPLT) {
		BCMDBG0(BCMDBG_INFO, bcm_ctlp,
		    "qdma command intr handling");
		if (bcmp->bcmp_device_type != SATA_DTYPE_ATAPI) {
			mutex_exit(&bcmp->bcmp_mutex);
			(void) bcm_intr_qdma_cmd_cmplt(bcm_ctlp,
			    bcmp, port);
		} else {
			mutex_exit(&bcmp->bcmp_mutex);
			(void) bcm_intr_qdma_pkt_cmd_cmplt(bcm_ctlp,
			    bcmp, port);
		}
		return;
	}

	/* Sata error interrupts */
	if (port_qsr & BCM_QDMA_SATAIF_ERR) {
		BCMDBG0(BCMDBG_INFO, bcm_ctlp,
		    "bcm_port_intr: sata errors");
		/* Check the PhyRdy change status interrupt bit */
		if (port_ser & BCM_SER_PHY_RDY_CHG) {
			BCMDBG0(BCMDBG_INFO, bcm_ctlp,
			    "bcm_port_intr: hotplug");
			/* Handle hotplug event */
			mutex_exit(&bcmp->bcmp_mutex);
			(void) bcm_intr_phyrdy_change(bcm_ctlp, bcmp, port);
		} else {
			/*
			 * Check the interface error interrupt bits, there are
			 * eight kinds of interface errors for ht1000
			 * controllers:
			 *
			 * PHY internal error
			 * 10B-to-8B decode error
			 * disparity error
			 * CRC error
			 * Handshake error
			 * Link seq error
			 * Tran state error
			 * FIS type error
			 *
			 * Log the errors for now
			 */
			mutex_exit(&bcmp->bcmp_mutex);
			(void) bcm_intr_ser_error(bcm_ctlp, bcmp, port,
			    port_ser, port_qsr);
		}
		return;
	}

	/*
	 * Check PCI errors
	 *
	 * Pci master abort
	 * Pci bus master abort
	 */
	if (port_qsr & (BCM_QDMA_PCIBUS_ERR | BCM_QDMA_PCIMSABORT_ERR)) {
		BCMDBG0(BCMDBG_INFO, bcm_ctlp,
		    "bcm_port_intr: pci errors");
		mutex_exit(&bcmp->bcmp_mutex);
		(void) bcm_intr_qsr_pci_error(bcm_ctlp, bcmp,
		    port, port_qsr);
		return;
	}

	/*
	 * Check error bits reported by QSR, there are four kinds of
	 * command errors :
	 *
	 * Data CRC error (CMDERR)
	 * PRD excess length error (CMDERR)
	 * PRD deficient length error (CMDERR)
	 * ATA command error (CMDERR)
	 */
	if (port_qsr & BCM_QDMA_CMD_ERR) {
		BCMDBG0(BCMDBG_INFO, bcm_ctlp,
		    "bcm_port_intr: command errors");
		if (bcmp->bcmp_device_type == SATA_DTYPE_ATAPI) {
			/* QDMA ATAPI command errors */
			mutex_exit(&bcmp->bcmp_mutex);
			(void) bcm_intr_qsr_pkt_cmd_error(bcm_ctlp, bcmp,
			    port, port_qsr);
		} else {
			/* QDMA disk command errors */
			mutex_exit(&bcmp->bcmp_mutex);
			(void) bcm_intr_qsr_cmd_error(bcm_ctlp, bcmp,
			    port, port_qsr);
		}
		return;
	}

	mutex_exit(&bcmp->bcmp_mutex);
}

/*
 * Interrupt service handler
 */
static uint_t
bcm_intr(caddr_t arg1, caddr_t arg2)
{
#ifndef __lock_lint
	_NOTE(ARGUNUSED(arg2))
#endif
	/* LINTED */
	bcm_ctl_t *bcm_ctlp = (bcm_ctl_t *)arg1;
	bcm_port_t *bcmp;
	uint32_t gsr_status;
	int port;

	BCMDBG0(BCMDBG_ENTRY, bcm_ctlp, "bcm_intr enter:");

	/*
	 * global status register indicates that the corresponding port has
	 * an interrupt pending.
	 */
	gsr_status = ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_GSR(bcm_ctlp));

	if (!(~gsr_status & BCM_GIMR_ALL)) {
		return (DDI_INTR_UNCLAIMED);
	}

	/* Loop for all the ports */
	for (port = 0; port < bcm_ctlp->bcmc_num_ports; port++) {

		BCMDBG1(BCMDBG_INFO, bcm_ctlp,
		    "bcm_intr: interrupt handling for port %d", port);

		if ((0x1 << port) & gsr_status) {
			BCMDBG1(BCMDBG_INFO, bcm_ctlp,
			    "bcm_intr: no interrupt on port %d", port);
			continue;
		}

		BCMDBG1(BCMDBG_INFO, bcm_ctlp,
		    "bcm_intr: interrupt on port %d", port);

		bcmp = bcm_ctlp->bcmc_ports[port];

		/* Call bcm_port_intr */
		bcm_port_intr(bcm_ctlp, bcmp, port);

	}

	return (DDI_INTR_CLAIMED);
}

/*
 * Interrupt processing for a non-data ATA command.
 */
static void
bcm_intr_nodata(bcm_ctl_t *bcm_ctlp, bcm_port_t *bcmp, uint8_t port)
{
	uint8_t status;
	uint32_t port_qsr_status;
	sata_pkt_t *spkt = bcmp->bcmp_nonqdma_cmd->bcm_spkt;
	sata_cmd_t *sata_cmdp = &spkt->satapkt_cmd;

	BCMDBG1(BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_intr_nodata enter: port %d", port);

	status = ddi_get8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_STATUS(bcm_ctlp, port));

	port_qsr_status = ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QSR(bcm_ctlp, port));

	ddi_put32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QSR(bcm_ctlp, port),
	    port_qsr_status);

	bcmp->bcmp_qdma_engine_flags &= ~BCM_QDMA_QCR_PAUSE;

	/*
	 * check for errors
	 */
	if ((port_qsr_status & BCM_QDMA_ATACMD_ERR) || (status & (SATA_STATUS_DF
	    | SATA_STATUS_ERR))) {
		sata_cmdp->satacmd_status_reg = ddi_get8(
		    bcm_ctlp->bcmc_bar_handle,
		    (uint8_t *)BCM_ALTSTATUS(bcm_ctlp, port));
		sata_cmdp->satacmd_error_reg = ddi_get8(
		    bcm_ctlp->bcmc_bar_handle,
		    (uint8_t *)BCM_ERROR(bcm_ctlp, port));
		bcmp->bcmp_nonqdma_cmd->bcm_cmd_flags =
		    BCM_NONQDMA_CMD_COMPLETE;
		bcmp->bcmp_nonqdma_cmd->bcm_spkt = NULL;
		SENDUP_PACKET(bcmp, spkt, SATA_PKT_DEV_ERROR);
	} else {

		bcmp->bcmp_nonqdma_cmd->bcm_cmd_flags =
		    BCM_NONQDMA_CMD_COMPLETE;
		bcmp->bcmp_nonqdma_cmd->bcm_spkt = NULL;
		SENDUP_PACKET(bcmp, spkt, SATA_PKT_COMPLETED);
	}

	ddi_put32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QCR(bcm_ctlp, port),
	    BCM_QDMA_QCR_ENABLE);
}

/*
 * ATA command, PIO data in
 */
static void
bcm_intr_pio_in(bcm_ctl_t *bcm_ctlp, bcm_port_t *bcmp, uint8_t port)
{
	uint8_t status;
	uint32_t port_qsr_status;
	sata_pkt_t *spkt = bcmp->bcmp_nonqdma_cmd->bcm_spkt;
	sata_cmd_t *sata_cmdp = &spkt->satapkt_cmd;
	int count;
	bcm_event_arg_t *event_args;
	int instance = ddi_get_instance(bcm_ctlp->bcmc_dip);

	BCMDBG1(BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_intr_pio_in enter: port %d", port);

	status = ddi_get8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_STATUS(bcm_ctlp, port));

	port_qsr_status = ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QSR(bcm_ctlp, port));
	ddi_put32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QSR(bcm_ctlp, port),
	    port_qsr_status);
	bcmp->bcmp_qdma_engine_flags &= ~BCM_QDMA_QCR_PAUSE;

	BCMDBG3(BCMDBG_INFO, bcm_ctlp,
	    "bcm_intr_pio_in enter: port %d "
	    "spkt 0x%p status 0x%x", port, spkt, status);

	if (spkt->satapkt_op_mode & SATA_OPMODE_POLLING) {
		if (status & SATA_STATUS_BSY) {
			return;
		} else {
			goto check_error;
		}
	}

	if (status & SATA_STATUS_BSY) {
		BCMDBG1(BCMDBG_INFO, bcm_ctlp,
		    "bcm_intr_pio_in: busy bit not cleared "
		    "status = 0x%x", status);

		sata_cmdp->satacmd_status_reg = ddi_get8(
		    bcm_ctlp->bcmc_bar_handle,
		    (uint8_t *)BCM_ALTSTATUS(bcm_ctlp, port));
		sata_cmdp->satacmd_error_reg = ddi_get8(
		    bcm_ctlp->bcmc_bar_handle,
		    (uint8_t *)BCM_ERROR(bcm_ctlp, port));
		bcmp->bcmp_nonqdma_cmd->bcm_cmd_flags =
		    BCM_NONQDMA_CMD_COMPLETE;
		bcmp->bcmp_nonqdma_cmd->bcm_spkt = NULL;
		SENDUP_PACKET(bcmp, spkt, SATA_PKT_TIMEOUT);

		/* Prepare the argument for the taskq */
		event_args = bcmp->bcmp_event_args;
		event_args->bcmea_ctlp = (void *)bcm_ctlp;
		event_args->bcmea_event = BCM_PORT_RESET_EVENT;
		/* Start the taskq to handle error recovery */
		if ((ddi_taskq_dispatch(bcm_ctlp->bcmc_event_taskq,
		    bcm_events_handler,
		    (void *)event_args, DDI_NOSLEEP)) != DDI_SUCCESS) {
			cmn_err(CE_WARN, "!bcm%d: bcm start taskq for "
			    "event handler failed", instance);
		}
		return;
	}

check_error:
	if ((port_qsr_status & BCM_QDMA_ATACMD_ERR) ||
	    (status & (SATA_STATUS_DRQ |
	    SATA_STATUS_DF |SATA_STATUS_ERR)) != SATA_STATUS_DRQ) {
		BCMDBG1(BCMDBG_INFO, bcm_ctlp,
		    "bcm_intr_pio_in: error bits set in status = 0x%x", status);
		bcm_update_sata_registers(bcm_ctlp, port,
		    &spkt->satapkt_device);
		bcm_copy_err_cnxt(bcm_ctlp, bcmp, port);
		bcmp->bcmp_nonqdma_cmd->bcm_cmd_flags =
		    BCM_NONQDMA_CMD_COMPLETE;
		bcmp->bcmp_nonqdma_cmd->bcm_spkt = NULL;
		SENDUP_PACKET(bcmp, spkt, SATA_PKT_DEV_ERROR);

		goto pio_in_out;
	}

	/*
	 * read the next chunk of data (if any)
	 */
	count = min(bcmp->bcmp_nonqdma_cmd->bcm_byte_count, BCM_BYTES_PER_SEC);

	/*
	 * read count bytes
	 */
	ASSERT(count != 0);

	ddi_rep_get16(bcm_ctlp->bcmc_bar_handle,
	    (uint16_t *)bcmp->bcmp_nonqdma_cmd->bcm_v_addr,
	    (uint16_t *)BCM_DATA(bcm_ctlp, port),
	    (count >> 1), DDI_DEV_NO_AUTOINCR);

	bcmp->bcmp_nonqdma_cmd->bcm_v_addr += count;

	bcmp->bcmp_nonqdma_cmd->bcm_byte_count -= count;

	if (bcmp->bcmp_nonqdma_cmd->bcm_byte_count != 0) {
		/*
		 * more to transfer.  Wait for next interrupt.
		 */
		return;
	}

	/*
	 * transfer is complete. wait for the busy bit to settle.
	 */
	BCM_DELAY_NSEC(400);

	bcmp->bcmp_nonqdma_cmd->bcm_cmd_flags = BCM_NONQDMA_CMD_COMPLETE;
	bcmp->bcmp_nonqdma_cmd->bcm_spkt = NULL;
	SENDUP_PACKET(bcmp, spkt, SATA_PKT_COMPLETED);

pio_in_out:
	ddi_put32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QCR(bcm_ctlp, port),
	    BCM_QDMA_QCR_ENABLE);
}

/*
 * ATA command PIO data out
 */
static void
bcm_intr_pio_out(bcm_ctl_t *bcm_ctlp, bcm_port_t *bcmp, uint8_t port)
{
	sata_pkt_t *spkt = bcmp->bcmp_nonqdma_cmd->bcm_spkt;
	uint8_t status;
	uint32_t port_qsr_status;
	int count;

	BCMDBG1(BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_intr_pio_out enter: port %d", port);

	status = ddi_get8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_STATUS(bcm_ctlp, port));

	port_qsr_status = ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QSR(bcm_ctlp, port));
	ddi_put32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QSR(bcm_ctlp, port),
	    port_qsr_status);
	bcmp->bcmp_qdma_engine_flags &= ~BCM_QDMA_QCR_PAUSE;

	if (spkt->satapkt_op_mode & SATA_OPMODE_POLLING) {
		if (status & SATA_STATUS_BSY) {
			return;
		}
	}

	/*
	 * check for errors
	 */
	if ((port_qsr_status & BCM_QDMA_ATACMD_ERR) || status & (SATA_STATUS_DF
	    | SATA_STATUS_ERR)) {
		bcm_update_sata_registers(bcm_ctlp, port,
		    &spkt->satapkt_device);
		bcm_copy_err_cnxt(bcm_ctlp, bcmp, port);
		bcmp->bcmp_nonqdma_cmd->bcm_cmd_flags =
		    BCM_NONQDMA_CMD_COMPLETE;
		bcmp->bcmp_nonqdma_cmd->bcm_spkt = NULL;
		SENDUP_PACKET(bcmp, spkt, SATA_PKT_DEV_ERROR);

		ddi_put32(bcm_ctlp->bcmc_bar_handle,
		    (uint32_t *)BCM_QDMA_QCR(bcm_ctlp, port),
		    BCM_QDMA_QCR_ENABLE);

		return;
	}

	/*
	 * this is the condition which signals the drive is
	 * no longer ready to transfer.	 Likely that the transfer
	 * completed successfully, but check that byte_count is
	 * zero.
	 */
	if ((status & SATA_STATUS_DRQ) == 0) {

		if (spkt->satapkt_cmd.satacmd_bp->b_bcount == 0) {
			/*
			 * complete; successful transfer
			 */
			bcmp->bcmp_nonqdma_cmd->bcm_cmd_flags
			    = BCM_NONQDMA_CMD_COMPLETE;
			bcmp->bcmp_nonqdma_cmd->bcm_spkt = NULL;
			SENDUP_PACKET(bcmp, spkt, SATA_PKT_COMPLETED);
		} else {
			/*
			 * error condition, incomplete transfer
			 */
			bcm_update_sata_registers(bcm_ctlp, port,
			    &spkt->satapkt_device);
			bcm_copy_err_cnxt(bcm_ctlp, bcmp, port);
			bcmp->bcmp_nonqdma_cmd->bcm_cmd_flags
			    = BCM_NONQDMA_CMD_COMPLETE;
			bcmp->bcmp_nonqdma_cmd->bcm_spkt = NULL;
			SENDUP_PACKET(bcmp, spkt, SATA_PKT_DEV_ERROR);
		}

		ddi_put32(bcm_ctlp->bcmc_bar_handle,
		    (uint32_t *)BCM_QDMA_QCR(bcm_ctlp, port),
		    BCM_QDMA_QCR_ENABLE);

		return;
	}

	/*
	 * write the next chunk of data
	 */
	count = min(bcmp->bcmp_nonqdma_cmd->bcm_byte_count, BCM_BYTES_PER_SEC);

	/*
	 * read or write count bytes
	 */

	ASSERT(count != 0);

	ddi_rep_put16(bcm_ctlp->bcmc_bar_handle,
	    (uint16_t *)bcmp->bcmp_nonqdma_cmd->bcm_v_addr,
	    (uint16_t *)BCM_DATA(bcm_ctlp, port),
	    (count >> 1), DDI_DEV_NO_AUTOINCR);

	bcmp->bcmp_nonqdma_cmd->bcm_v_addr += count;
	bcmp->bcmp_nonqdma_cmd->bcm_byte_count -= count;
}

/*
 * Non QDMA ATAPI PACKET command interrupt handling.
 *
 * Under normal circumstances, one of four different interrupt scenarios
 * will result in this function being called:
 *
 * 1. Packet command data transfer
 * 2. Packet command completion
 * 3. Request sense data transfer
 * 4. Request sense command completion
 */
static void
bcm_intr_non_qdma_pkt_cmd(bcm_ctl_t *bcm_ctlp, bcm_port_t *bcmp, uint8_t port)
{
	uchar_t status, error;
	uint32_t qsr_status;
	sata_pkt_t *spkt;
	sata_cmd_t *sata_cmdp;
	int direction;
	uint16_t ctlr_count;
	int count, reason;
	bcm_event_arg_t *event_args;
	int instance = ddi_get_instance(bcm_ctlp->bcmc_dip);

	BCMDBG1(BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_intr_non_qdma_pkt_cmd enter: port %d", port);

	if ((bcmp->bcmp_nonqdma_cmd->bcm_spkt == NULL) &&
	    (bcmp->bcmp_err_retri_pkt != NULL)) {
		/* in case of request sense for qdma command error */
		spkt = bcmp->bcmp_err_retri_pkt;
	} else {
		spkt = bcmp->bcmp_nonqdma_cmd->bcm_spkt;
	}
	sata_cmdp = &spkt->satapkt_cmd;
	direction = sata_cmdp->satacmd_flags.sata_data_direction;

	/* ATAPI protocol state - HP2: Check_Status_B */

	status = ddi_get8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_STATUS(bcm_ctlp, port));

	error = ddi_get8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_ERROR(bcm_ctlp, port));

	BCMDBG2(BCMDBG_INFO, bcm_ctlp,
	    "bcm_intr_non_qdma_pkt_cmd: status 0x%x error 0x%x",
	    status, error);

	qsr_status = ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QSR(bcm_ctlp, port));
	ddi_put32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QSR(bcm_ctlp, port),
	    qsr_status);

	if (spkt->satapkt_op_mode & SATA_OPMODE_POLLING) {
		if (status & SATA_STATUS_BSY) {
			return;
		} else {
			goto check_df;
		}
	}

	if (status & SATA_STATUS_BSY) {
		if (bcmp->bcmp_flags & BCM_PORT_FLAG_RQSENSE) {
			bcmp->bcmp_flags &= ~BCM_PORT_FLAG_RQSENSE;
			reason = SATA_PKT_DEV_ERROR;
			if (bcmp->bcmp_err_retri_pkt != NULL) {
				bcmp->bcmp_err_retri_pkt = NULL;
				for (int i = 0; i < BCM_CTL_QUEUE_DEPTH; i++) {
					if (spkt == bcmp->bcmp_slot_pkts[i]) {
						bcmp->bcmp_slot_pkts[i] = NULL;
						break;
					} else {
						continue;
					}
				}
				SENDUP_PACKET(bcmp, spkt, reason);
				goto next;
			}
		} else {
			reason = SATA_PKT_TIMEOUT;
		}

		BCMDBG1(BCMDBG_INFO, bcm_ctlp,
		    "bcm_intr_non_qdma_pkt_cmd: busy - status 0x%x", status);

		bcmp->bcmp_nonqdma_cmd->bcm_cmd_flags =
		    BCM_NONQDMA_CMD_COMPLETE;
		bcmp->bcmp_nonqdma_cmd->bcm_spkt = NULL;
		SENDUP_PACKET(bcmp, spkt, reason);

next:
		/* Prepare the argument for the taskq */
		event_args = bcmp->bcmp_event_args;
		event_args->bcmea_ctlp = (void *)bcm_ctlp;
		event_args->bcmea_portp = (void *)bcmp;
		event_args->bcmea_event = BCM_PORT_RESET_EVENT;

		/* Start the taskq to handle error recovery */
		if ((ddi_taskq_dispatch(bcm_ctlp->bcmc_event_taskq,
		    bcm_events_handler,
		    (void *)event_args, DDI_NOSLEEP)) != DDI_SUCCESS) {
			cmn_err(CE_WARN, "!bcm%d: bcm start taskq for "
			    "event handler failed", instance);
		}
		return;
	}

check_df:

	if ((status & SATA_STATUS_DF) != 0) {
		/*
		 * On device fault, just clean up and bail.  Request sense
		 * will just default to its NO SENSE initialized value.
		 */
		reason = SATA_PKT_DEV_ERROR;
		if ((bcmp->bcmp_flags & BCM_PORT_FLAG_RQSENSE)) {
			bcmp->bcmp_flags &= ~BCM_PORT_FLAG_RQSENSE;
			if (bcmp->bcmp_err_retri_pkt != NULL) {
				bcmp->bcmp_err_retri_pkt = NULL;
				for (int i = 0; i < BCM_CTL_QUEUE_DEPTH; i++) {
					if (spkt == bcmp->bcmp_slot_pkts[i]) {
						bcmp->bcmp_slot_pkts[i] = NULL;
						break;
					} else {
						continue;
					}
				}
				SENDUP_PACKET(bcmp, spkt, reason);
				(void) bcm_unpause_port_QDMA_engine
				    (bcm_ctlp, bcmp, port);
				return;
			}
		} else {
			bcm_update_sata_registers(bcm_ctlp, port,
			    &spkt->satapkt_device);
			bcm_copy_err_cnxt(bcm_ctlp, bcmp, port);
		}

		bcmp->bcmp_nonqdma_cmd->bcm_cmd_flags =
		    BCM_NONQDMA_CMD_COMPLETE;
		bcmp->bcmp_nonqdma_cmd->bcm_spkt = NULL;
		BCMDBG0(BCMDBG_INFO, bcm_ctlp,
		    "bcm_intr_non_qdma_pkt_cmd: device fault");
		(void) bcm_unpause_port_QDMA_engine(bcm_ctlp, bcmp, port);
		SENDUP_PACKET(bcmp, spkt, reason);
		return;
	}

	if (status & SATA_STATUS_ERR) {
		/*
		 * On command error, figure out whether we are processing a
		 * request sense.  If so, clean up and bail.  Otherwise,
		 * do a REQUEST SENSE.
		 */
		if ((bcmp->bcmp_flags & BCM_PORT_FLAG_RQSENSE) == 0) {
			bcm_update_sata_registers(bcm_ctlp, port,
			    &spkt->satapkt_device);
			bcm_copy_err_cnxt(bcm_ctlp, bcmp, port);
			bcmp->bcmp_flags |= BCM_PORT_FLAG_RQSENSE;
			if (bcm_start_rqsense_pkt_cmd(bcm_ctlp,
			    bcmp, port, spkt) != BCM_SUCCESS) {
				bcmp->bcmp_nonqdma_cmd->bcm_cmd_flags
				    = BCM_NONQDMA_CMD_COMPLETE;
				bcmp->bcmp_nonqdma_cmd->bcm_spkt = NULL;
				bcmp->bcmp_flags &= ~BCM_PORT_FLAG_RQSENSE;
				reason = SATA_PKT_DEV_ERROR;
				(void) bcm_unpause_port_QDMA_engine
				    (bcm_ctlp, bcmp, port);
				SENDUP_PACKET(bcmp, spkt, reason);
			}
		} else {
			bcmp->bcmp_flags &= ~BCM_PORT_FLAG_RQSENSE;
			if (bcmp->bcmp_err_retri_pkt == NULL) {
				bcmp->bcmp_nonqdma_cmd->bcm_cmd_flags
				    = BCM_NONQDMA_CMD_COMPLETE;
				bcmp->bcmp_nonqdma_cmd->bcm_spkt = NULL;
				reason = SATA_PKT_DEV_ERROR;
				(void) bcm_unpause_port_QDMA_engine
				    (bcm_ctlp, bcmp, port);
				SENDUP_PACKET(bcmp, spkt, reason);
			} else {
				bcmp->bcmp_err_retri_pkt = NULL;
				(void) bcm_unpause_port_QDMA_engine
				    (bcm_ctlp, bcmp, port);
			}
		}
		return;
	}

	if (bcmp->bcmp_flags & BCM_PORT_FLAG_RQSENSE) {
		/*
		 * REQUEST SENSE command processing
		 */
		if ((status & (SATA_STATUS_DRQ)) != 0) {
			/* ATAPI state - HP4: Transfer_Data */

			/* read the byte count from the controller */
			ctlr_count =
			    (uint16_t)ddi_get8(bcm_ctlp->bcmc_bar_handle,
			    (uint8_t *)BCM_HCYL(bcm_ctlp, port)) << 8;
			ctlr_count |= ddi_get8(bcm_ctlp->bcmc_bar_handle,
			    (uint8_t *)BCM_LCYL(bcm_ctlp, port));

			BCMDBG1(BCMDBG_INFO, bcm_ctlp,
			    "bcm_intr_non_qdma_pkt_cmd: ctlr byte count - %d",
			    ctlr_count);

			if (ctlr_count == 0) {
				/* no data to transfer - some devices do this */
				BCMDBG0(BCMDBG_INFO, bcm_ctlp,
				    "bcm_intr_non_qdma_pkt_cmd: "
				    "done (no data)");
				bcmp->bcmp_flags &= ~BCM_PORT_FLAG_RQSENSE;
				reason = SATA_PKT_DEV_ERROR;
				if (bcmp->bcmp_err_retri_pkt != NULL) {
					bcmp->bcmp_err_retri_pkt = NULL;
					for (int i = 0; i < BCM_CTL_QUEUE_DEPTH;
					    i++) {
						if (spkt ==
						    bcmp->bcmp_slot_pkts[i]) {
							bcmp->bcmp_slot_pkts[i]
							    = NULL;
							break;
						} else {
							continue;
						}
					}
					SENDUP_PACKET(bcmp, spkt, reason);
					(void) bcm_unpause_port_QDMA_engine
					    (bcm_ctlp, bcmp, port);
					return;
				}

				bcmp->bcmp_nonqdma_cmd->bcm_cmd_flags
				    = BCM_NONQDMA_CMD_COMPLETE;
				bcmp->bcmp_nonqdma_cmd->bcm_spkt = NULL;
				(void) bcm_unpause_port_QDMA_engine(bcm_ctlp,
				    bcmp, port);
				SENDUP_PACKET(bcmp, spkt, reason);
				return;
			}

			count = min(ctlr_count, SATA_ATAPI_RQSENSE_LEN);

			/* transfer the data */
			ddi_rep_get16(bcm_ctlp->bcmc_bar_handle,
			    (ushort_t *)bcmp->bcmp_nonqdma_cmd->
			    bcm_rqsense_buff,
			    (ushort_t *)BCM_DATA(bcm_ctlp, port),
			    (count >> 1),
			    DDI_DEV_NO_AUTOINCR);

			/* consume residual bytes */
			ctlr_count -= count;

			if (ctlr_count > 0) {
				for (; ctlr_count > 0; ctlr_count -= 2)
					(void) ddi_get16
					    (bcm_ctlp->bcmc_bar_handle,
					    (ushort_t *)BCM_DATA
					    (bcm_ctlp, port));
			}

			BCMDBG0(BCMDBG_INFO, bcm_ctlp,
			    "bcm_intr_non_qdma_pkt_cmd: transition to HP2");
		} else {
			/* still in ATAPI state - HP2 */

			/*
			 * In order to avoid clobbering the rqsense data
			 * set by the SATA framework, the sense data read
			 * from the device is put in a separate buffer and
			 * copied into the packet after the request sense
			 * command successfully completes.
			 */
			bcopy(bcmp->bcmp_nonqdma_cmd->bcm_rqsense_buff,
			    spkt->satapkt_cmd.satacmd_rqsense,
			    SATA_ATAPI_RQSENSE_LEN);

			BCMDBG0(BCMDBG_INFO, bcm_ctlp,
			    "bcm_intr_non_qdma_pkt_cmd: request sense done");

			bcmp->bcmp_flags &= ~BCM_PORT_FLAG_RQSENSE;
			if (bcmp->bcmp_err_retri_pkt != NULL) {
				bcmp->bcmp_err_retri_pkt = NULL;
				for (int i = 0; i < BCM_CTL_QUEUE_DEPTH; i++) {
					if (spkt == bcmp->bcmp_slot_pkts[i]) {
						bcmp->bcmp_slot_pkts[i] = NULL;
						break;
					} else {
						continue;
					}
				}
				reason = SATA_PKT_DEV_ERROR;
				SENDUP_PACKET(bcmp, spkt, reason);
				(void) bcm_unpause_port_QDMA_engine(bcm_ctlp,
				    bcmp, port);
				bcmp->bcmp_qdma_pkt_cmd_running = 0;
			} else {
				bcmp->bcmp_nonqdma_cmd->bcm_cmd_flags
				    = BCM_NONQDMA_CMD_COMPLETE;
				bcmp->bcmp_nonqdma_cmd->bcm_spkt = NULL;
				reason = SATA_PKT_DEV_ERROR;
				(void) bcm_unpause_port_QDMA_engine(bcm_ctlp,
				    bcmp, port);
				SENDUP_PACKET(bcmp, spkt, reason);
			}
		}
		return;
	}

	/*
	 * Normal command processing
	 */
	if ((status & (SATA_STATUS_DRQ)) != 0) {
		/* ATAPI protocol state - HP4: Transfer_Data */

		/* read the byte count from the controller */
		ctlr_count = (uint16_t)ddi_get8(bcm_ctlp->bcmc_bar_handle,
		    (uint8_t *)BCM_HCYL(bcm_ctlp, port)) << 8;
		ctlr_count |= ddi_get8(bcm_ctlp->bcmc_bar_handle,
		    (uint8_t *)BCM_LCYL(bcm_ctlp, port));

		if (ctlr_count == 0) {
			/* no data to transfer - some devices do this */
			BCMDBG0(BCMDBG_INFO, bcm_ctlp,
			    "bcm_intr_non_qdma_pkt_cmd: done (no data)");

			reason = SATA_PKT_COMPLETED;
			bcmp->bcmp_nonqdma_cmd->bcm_cmd_flags
			    = BCM_NONQDMA_CMD_COMPLETE;
			bcmp->bcmp_nonqdma_cmd->bcm_spkt = NULL;
			(void) bcm_unpause_port_QDMA_engine(bcm_ctlp,
			    bcmp, port);
			SENDUP_PACKET(bcmp, spkt, reason);
			return;
		}

		count = min(ctlr_count, bcmp->bcmp_nonqdma_cmd->bcm_byte_count);

		BCMDBG1(BCMDBG_INFO, bcm_ctlp,
		    "bcm_intr_non_qdma_pkt_cmd: drive_bytes 0x%x", ctlr_count);

		BCMDBG1(BCMDBG_INFO, bcm_ctlp,
		    "bcm_intr_non_qdma_pkt_cmd: byte_count 0x%x",
		    bcmp->bcmp_nonqdma_cmd->bcm_byte_count);

		/* transfer the data */
		if (direction == SATA_DIR_READ) {
			ddi_rep_get16(bcm_ctlp->bcmc_bar_handle,
			    (ushort_t *)bcmp->bcmp_nonqdma_cmd->bcm_v_addr,
			    (ushort_t *)BCM_DATA(bcm_ctlp, port), (count >> 1),
			    DDI_DEV_NO_AUTOINCR);

			ctlr_count -= count;

			if (ctlr_count > 0) {
				/* consume remaining bytes */
				for (; ctlr_count > 0;
				    ctlr_count -= 2)
					(void) ddi_get16
					    (bcm_ctlp->bcmc_bar_handle,
					    (ushort_t *)BCM_DATA
					    (bcm_ctlp, port));

				BCMDBG0(BCMDBG_INFO, bcm_ctlp,
				    "bcm_intr_non_qdma_pkt_cmd: "
				    " bytes remained");
			}
		} else {
			ddi_rep_put16(bcm_ctlp->bcmc_bar_handle,
			    (ushort_t *)bcmp->bcmp_nonqdma_cmd->bcm_v_addr,
			    (ushort_t *)BCM_DATA(bcm_ctlp, port), (count >> 1),
			    DDI_DEV_NO_AUTOINCR);
		}

		bcmp->bcmp_nonqdma_cmd->bcm_v_addr += count;
		bcmp->bcmp_nonqdma_cmd->bcm_byte_count -= count;

		BCMDBG0(BCMDBG_INFO, bcm_ctlp,
		    "bcm_intr_non_qdma_pkt_cmd: transition to HP2");
	} else {
		/* still in ATAPI state - HP2 */

		BCMDBG0(BCMDBG_INFO, bcm_ctlp,
		    "bcm_intr_non_qdma_pkt_cmd: done");
		reason = SATA_PKT_COMPLETED;
		bcmp->bcmp_nonqdma_cmd->bcm_cmd_flags
		    = BCM_NONQDMA_CMD_COMPLETE;
		bcmp->bcmp_nonqdma_cmd->bcm_spkt = NULL;
		(void) bcm_unpause_port_QDMA_engine(bcm_ctlp,
		    bcmp, port);
		SENDUP_PACKET(bcmp, spkt, reason);
	}
}


/*
 * The completion of QDMA commands. Commands are regular disk DMA commands
 * and ATAPI commands.
 */
static int
bcm_intr_qdma_cmd_cmplt(bcm_ctl_t *bcm_ctlp, bcm_port_t *bcmp, uint8_t port)
{
	uint8_t finished_slot, qci_value;
	sata_pkt_t *satapkt;

	BCMDBG1(BCMDBG_ENTRY|BCMDBG_INTR, bcm_ctlp,
	    "bcm_intr_qdma_cmd_cmplt enter: port %d", port);

	mutex_enter(&bcmp->bcmp_mutex);

	/* Get the finished command slot */
	qci_value = (uint8_t)ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QCI(bcm_ctlp, port));

	BCMDBG2(BCMDBG_ENTRY|BCMDBG_INTR, bcm_ctlp,
	    "bcm_intr_qdma_cmd_cmplt enter: port %d"
	    " qci_value 0x%x", port, qci_value);

	if (qci_value == 0) {
		finished_slot = BCM_CTL_QUEUE_DEPTH -1;
	} else {
		finished_slot = qci_value -1;
	}

	/*
	 * Make sure that interrupt routine is
	 * servicing the right command
	 */
	if (bcmp->bcmp_intr_idx == finished_slot) {

		/* Handling disk DMA command completion */
		satapkt = bcmp->bcmp_slot_pkts[finished_slot];
		BCMDBG1(BCMDBG_INTR, bcm_ctlp,
		    "with SATA_PKT_COMPLETED", (void *)satapkt);
		ASSERT(satapkt != NULL);
		bcmp->bcmp_slot_pkts[finished_slot] = NULL;
		SENDUP_PACKET(bcmp, satapkt, SATA_PKT_COMPLETED);
		if (bcmp->bcmp_intr_idx == BCM_CTL_QUEUE_DEPTH -1) {
			bcmp->bcmp_intr_idx = 0;
		} else {
			bcmp->bcmp_intr_idx = bcmp->bcmp_intr_idx + 1;
		}
	} else {
		/* Make sure that buried interrupts are serviced too */
		do {
			satapkt = bcmp->bcmp_slot_pkts[bcmp->bcmp_intr_idx];
			if (satapkt != NULL) {
				bcmp->bcmp_slot_pkts
				    [bcmp->bcmp_intr_idx] = NULL;
				SENDUP_PACKET(bcmp, satapkt,
				    SATA_PKT_COMPLETED);
			}

			if (bcmp->bcmp_intr_idx == (BCM_CTL_QUEUE_DEPTH -1)) {
				bcmp->bcmp_intr_idx = 0;
			} else {
				bcmp->bcmp_intr_idx = bcmp->bcmp_intr_idx + 1;
			}
		} while (bcmp->bcmp_intr_idx == finished_slot);
	}
	mutex_exit(&bcmp->bcmp_mutex);
	return (BCM_SUCCESS);
}

/*
 * The completion of QDMA PKT commands.
 */
static int
bcm_intr_qdma_pkt_cmd_cmplt(bcm_ctl_t *bcm_ctlp, bcm_port_t *bcmp, uint8_t port)
{
	uint8_t finished_slot, qci_value;
	sata_pkt_t *atapi_spkt;

	BCMDBG1(BCMDBG_ENTRY|BCMDBG_INTR, bcm_ctlp,
	    "bcm_intr_qdma_pkt_cmd_cmplt enter: port %d", port);

	mutex_enter(&bcmp->bcmp_mutex);

	/* Get the finished command slot */
	qci_value = (uint8_t)ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QCI(bcm_ctlp, port));

	BCMDBG2(BCMDBG_ENTRY|BCMDBG_INTR, bcm_ctlp,
	    "bcm_intr_qdma_pkt_cmd_cmplt enter: port %d"
	    " qci_value 0x%x", port, qci_value);

	if (qci_value != bcmp->bcmp_qpi) {
		BCMDBG1(BCMDBG_ENTRY|BCMDBG_INTR, bcm_ctlp,
		    "bcm_intr_qdma_pkt_cmd_cmplt enter: port %d"
		    " stuck without error qsr", port);
		mutex_exit(&bcmp->bcmp_mutex);
		return (BCM_SUCCESS);
	}

	if (qci_value == 0) {
		finished_slot = BCM_CTL_QUEUE_DEPTH -2;
	} else {
		finished_slot = qci_value -2;
	}

	/* Normal ATAPI command completion */
	atapi_spkt = bcmp->bcmp_slot_pkts[finished_slot];
	bcmp->bcmp_slot_pkts[finished_slot] = NULL;

	BCMDBG1(BCMDBG_INTR, bcm_ctlp,
	    "with ATAPI command SATA_PKT_COMPLETED",
	    (void *)atapi_spkt);

	/* clear ATAPI command running flag */
	bcmp->bcmp_qdma_pkt_cmd_running = 0;
	SENDUP_PACKET(bcmp, atapi_spkt, SATA_PKT_COMPLETED);
	mutex_exit(&bcmp->bcmp_mutex);
	return (BCM_SUCCESS);
}

/*
 * SATA native hot plug support. It handles surprised hotplug cases too.
 *
 * When set, it indicates that the internal PHYRDY signal changed state.
 *
 * There are following kinds of conditions to generate this interrupt event:
 * 1. a device is inserted
 * 2. a device is disconnected
 *
 */
static int
bcm_intr_phyrdy_change(bcm_ctl_t *bcm_ctlp, bcm_port_t *bcmp, uint8_t port)
{
	uint32_t port_ssr = 0; /* No dev present & PHY not established. */
	sata_device_t sdevice;
	int dev_exists_now = 0;
	int dev_existed_previously = 0;
	bcm_event_arg_t *event_args;
	int instance = ddi_get_instance(bcm_ctlp->bcmc_dip);

	BCMDBG1(BCMDBG_INTR|BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_intr_phyrdy_change enter, port %d", port);

	mutex_enter(&bcm_ctlp->bcmc_mutex);
	if ((bcm_ctlp->bcmc_sata_hba_tran == NULL) ||
	    (bcmp == NULL)) {
		/* The whole controller setup is not yet done. */
		mutex_exit(&bcm_ctlp->bcmc_mutex);
		return (BCM_SUCCESS);
	}
	mutex_exit(&bcm_ctlp->bcmc_mutex);

	mutex_enter(&bcmp->bcmp_mutex);

	/* Sata status tells the presence of device. */
	port_ssr = ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_SATA_STATUS(bcm_ctlp, port));

	if (SSTATUS_GET_DET(port_ssr) == SSTATUS_DET_DEVPRE_PHYCOM) {
		dev_exists_now = 1;
	}

	if (bcmp->bcmp_device_type != SATA_DTYPE_NONE) {
		dev_existed_previously = 1;
	}

	bzero((void *)&sdevice, sizeof (sata_device_t));
	sdevice.satadev_addr.cport = port;
	sdevice.satadev_addr.qual = SATA_ADDR_CPORT;
	sdevice.satadev_addr.pmport = 0;
	sdevice.satadev_state = SATA_PSTATE_PWRON;
	bcmp->bcmp_state = SATA_PSTATE_PWRON;

	if (dev_exists_now) {
		if (dev_existed_previously) {
			/* Things are fine now. The loss was temporary. */
			BCMDBG1(BCMDBG_EVENT, bcm_ctlp,
			    "bcm_intr_phyrdy_change  port %d "
			    "device link lost/established", port);

			bcm_update_sata_registers(bcm_ctlp, port, &sdevice);
			mutex_exit(&bcmp->bcmp_mutex);
			sata_hba_event_notify(
			    bcm_ctlp->bcmc_sata_hba_tran->sata_tran_hba_dip,
			    &sdevice,
			    SATA_EVNT_LINK_LOST|SATA_EVNT_LINK_ESTABLISHED);
			mutex_enter(&bcmp->bcmp_mutex);

		} else {
			BCMDBG1(BCMDBG_EVENT, bcm_ctlp,
			    "bcm_intr_phyrdy_change: port %d "
			    "device link established", port);

			/* Prepare the argument for the taskq */
			event_args = bcmp->bcmp_event_args;
			event_args->bcmea_ctlp = (void *)bcm_ctlp;
			event_args->bcmea_portp = (void *)bcmp;
			event_args->bcmea_event = BCM_HOT_INSERT_EVENT;

			/* Start the taskq to handle hotplug event */
			if ((ddi_taskq_dispatch(bcm_ctlp->bcmc_event_taskq,
			    bcm_hotplug_events_handler,
			    (void *)event_args, DDI_NOSLEEP)) != DDI_SUCCESS) {
				cmn_err(CE_WARN, "!bcm%d: bcm start taskq for "
				    "hotplug event handler failed", instance);
			}
		}
	} else { /* No device exists now */
		if (dev_existed_previously) {
			BCMDBG1(BCMDBG_EVENT, bcm_ctlp,
			    "bcm_intr_phyrdy_change: port %d "
			    "device link lost", port);

			/* Prepare the argument for the taskq */
			event_args = bcmp->bcmp_event_args;
			event_args->bcmea_ctlp = (void *)bcm_ctlp;
			event_args->bcmea_portp = (void *)bcmp;
			event_args->bcmea_event = BCM_HOT_REMOVE_EVENT;

			/* Start the taskq to handle hot-unplug event */
			if ((ddi_taskq_dispatch(bcm_ctlp->bcmc_event_taskq,
			    bcm_hotplug_events_handler,
			    (void *)event_args, DDI_NOSLEEP)) != DDI_SUCCESS) {
				cmn_err(CE_WARN, "!bcm%d: bcm start taskq for "
				    "hotplug event handler failed", instance);
			}
		}
	}

	mutex_exit(&bcmp->bcmp_mutex);

	return (BCM_SUCCESS);
}

/*
 * interrupt handling for sata interface errors
 */
static int
bcm_intr_ser_error(bcm_ctl_t *bcm_ctlp, bcm_port_t *bcmp, uint8_t port,
    uint32_t port_ser, uint32_t port_qsr)
{
	bcm_event_arg_t *event_args;
	int instance = ddi_get_instance(bcm_ctlp->bcmc_dip);

	BCMDBG2(BCMDBG_INTR|BCMDBG_ENTRY|BCMDBG_ERRS, bcm_ctlp,
	    "bcm_intr_ser_error: port %d, "
	    "port_ser = 0x%x", port, port_ser);

	mutex_enter(&bcmp->bcmp_mutex);

	/* Log sata errors */
	bcm_log_serror_message(bcm_ctlp, port, port_ser, 1);

	/*
	 * bcm_intr_phyrdy_change() may have rendered it to
	 * SATA_DTYPE_NONE.
	 */
	if (bcmp->bcmp_device_type == SATA_DTYPE_NONE) {
		BCMDBG1(BCMDBG_ENTRY|BCMDBG_INTR, bcm_ctlp,
		    "bcm_intr_ser_error: port %d no device attached, "
		    "and just return without doing anything", port);
		goto out0;
	}

	/* Prepare the argument for the taskq */
	event_args = bcmp->bcmp_event_args;
	event_args->bcmea_ctlp = (void *)bcm_ctlp;
	event_args->bcmea_portp = (void *)bcmp;
	event_args->bcmea_event = port_qsr;

	/* Start the taskq to handle error recovery */
	if ((ddi_taskq_dispatch(bcm_ctlp->bcmc_event_taskq,
	    bcm_events_handler,
	    (void *)event_args, DDI_NOSLEEP)) != DDI_SUCCESS) {
		cmn_err(CE_WARN, "!bcm%d: bcm start taskq for "
		    "event handler failed", instance);
	}

out0:
	mutex_exit(&bcmp->bcmp_mutex);

	return (BCM_SUCCESS);
}

/* PCI Bus (Master) errors */
static int
bcm_intr_qsr_pci_error(bcm_ctl_t *bcm_ctlp, bcm_port_t *bcmp, uint8_t port,
    uint32_t port_qsr)
{
	bcm_event_arg_t *event_args;
	int instance = ddi_get_instance(bcm_ctlp->bcmc_dip);

	BCMDBG2(BCMDBG_INTR|BCMDBG_ENTRY|BCMDBG_ERRS, bcm_ctlp,
	    "bcm_intr_qsr_pci_error: port %d, "
	    "port_qsr = 0x%x", port, port_qsr);

	mutex_enter(&bcmp->bcmp_mutex);

	/* Log PCI errors */
	bcm_log_pci_error_message(bcm_ctlp, port, port_qsr);

	/*
	 * bcm_intr_phyrdy_change() may have rendered it to
	 * SATA_DTYPE_NONE.
	 */
	if (bcmp->bcmp_device_type == SATA_DTYPE_NONE) {
		BCMDBG1(BCMDBG_ENTRY|BCMDBG_INTR, bcm_ctlp,
		    "bcm_intr_qsr_pci_error: port %d no device attached, "
		    "and just return without doing anything", port);
		goto out0;
	}

	/* Prepare the argument for the taskq */
	event_args = bcmp->bcmp_event_args;
	event_args->bcmea_ctlp = (void *)bcm_ctlp;
	event_args->bcmea_portp = (void *)bcmp;
	event_args->bcmea_event = port_qsr;

	/* Start the taskq to handle error recovery */
	if ((ddi_taskq_dispatch(bcm_ctlp->bcmc_event_taskq,
	    bcm_events_handler,
	    (void *)event_args, DDI_NOSLEEP)) != DDI_SUCCESS) {
		cmn_err(CE_WARN, "!bcm%d: bcm start taskq for "
		    "event handler failed", instance);
	}
out0:
	mutex_exit(&bcmp->bcmp_mutex);

	return (BCM_SUCCESS);
}

/* Command errors */
static int
bcm_intr_qsr_cmd_error(bcm_ctl_t *bcm_ctlp, bcm_port_t *bcmp, uint8_t port,
    uint32_t port_qsr)
{
	bcm_event_arg_t *event_args;
	int instance = ddi_get_instance(bcm_ctlp->bcmc_dip);

	BCMDBG2(BCMDBG_INTR|BCMDBG_ENTRY|BCMDBG_ERRS, bcm_ctlp,
	    "bcm_intr_qsr_cmd_error: port %d, "
	    "port_qsr = 0x%x", port, port_qsr);

	mutex_enter(&bcmp->bcmp_mutex);

	/* Log command errors */
	bcm_log_cmd_error_message(bcm_ctlp, port, port_qsr);

	/*
	 * bcm_intr_phyrdy_change() may have rendered it to
	 * SATA_DTYPE_NONE.
	 */
	if (bcmp->bcmp_device_type == SATA_DTYPE_NONE) {
		BCMDBG1(BCMDBG_ENTRY|BCMDBG_INTR, bcm_ctlp,
		    "bcm_intr_qsr_cmd_error: port %d no device attached, "
		    "and just return without doing anything", port);
		goto out0;
	}

	/* Prepare the argument for the taskq */
	event_args = bcmp->bcmp_event_args;
	event_args->bcmea_ctlp = (void *)bcm_ctlp;
	event_args->bcmea_portp = (void *)bcmp;
	event_args->bcmea_event = port_qsr;

	/* Start the taskq to handle error recovery */
	if ((ddi_taskq_dispatch(bcm_ctlp->bcmc_event_taskq,
	    bcm_events_handler,
	    (void *)event_args, DDI_NOSLEEP)) != DDI_SUCCESS) {
		cmn_err(CE_WARN, "!bcm%d: bcm start taskq for "
		    "event handler failed", instance);
	}
out0:
	mutex_exit(&bcmp->bcmp_mutex);

	return (BCM_SUCCESS);
}

/* Command errors */
static int
bcm_intr_qsr_pkt_cmd_error(bcm_ctl_t *bcm_ctlp, bcm_port_t *bcmp, uint8_t port,
    uint32_t port_qsr)
{
	int		qci;
	int		failed_slot = BCM_NO_FAILED_SLOT;
	sata_pkt_t	*spkt;
	uint8_t		task_file_status;
	bcm_event_arg_t	*event_args;
	int instance = ddi_get_instance(bcm_ctlp->bcmc_dip);

	BCMDBG1(BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_intr_qsr_pkt_cmd_error enter: port %d", port);

	mutex_enter(&bcmp->bcmp_mutex);

	qci = (uint8_t)ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QCI(bcm_ctlp, port));
	if (qci == bcmp->bcmp_qpi) {
		if (qci == 0) {
			failed_slot = BCM_CTL_QUEUE_DEPTH -2;
		} else {
			failed_slot = qci - 2;
		}
	} else {
		failed_slot = qci;
	}

	spkt = bcmp->bcmp_slot_pkts[failed_slot];

	/* Update the sata registers, especially PxSERR register */
	bcm_update_sata_registers(bcm_ctlp, port,
	    &spkt->satapkt_device);

	/* Disable QDMA engine */
	(void) bcm_disable_port_QDMA_engine(bcm_ctlp, bcmp, port);

	/* Disable port interrupts */
	(void) bcm_disable_port_intrs(bcm_ctlp, port);

	task_file_status = ddi_get8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_STATUS(bcm_ctlp, port));

	if (!(task_file_status & (BCM_TFD_STS_BSY | BCM_TFD_STS_DRQ))) {
		/*
		 * Port still in stable state. Issue request sense.
		 */

		/* Reset the counters */
		ddi_put32(bcm_ctlp->bcmc_bar_handle,
		    (uint32_t *)BCM_QDMA_QCI(bcm_ctlp, port),
		    BCM_IDX_INIT_VALUE);

		ddi_put32(bcm_ctlp->bcmc_bar_handle,
		    (uint32_t *)BCM_QDMA_QPI(bcm_ctlp, port),
		    BCM_IDX_INIT_VALUE);

		bcmp->bcmp_qpi = BCM_IDX_INIT_VALUE;
		bcmp->bcmp_intr_idx = BCM_IDX_INIT_VALUE;

		/* Enable port interrupts back */
		(void) bcm_enable_port_intrs(bcm_ctlp, port);
		/* Enable the QDMA engine again */
		(void) bcm_enable_port_QDMA_engine(bcm_ctlp, bcmp, port);

		/*
		 * Request sense command will be executed in PIO mode. So
		 * pause QDMA engine before starting the request sense
		 */
		(void) bcm_pause_port_QDMA_engine(bcm_ctlp, bcmp, port);

		bcmp->bcmp_err_retri_pkt = spkt;
		bcmp->bcmp_flags |= BCM_PORT_FLAG_RQSENSE;
		if (bcm_start_rqsense_pkt_cmd(bcm_ctlp, bcmp, port, spkt)
		    != BCM_SUCCESS) {
			bcmp->bcmp_err_retri_pkt = NULL;
			bcmp->bcmp_flags &= ~BCM_PORT_FLAG_RQSENSE;
			goto out;
		}
		mutex_exit(&bcmp->bcmp_mutex);
		return (BCM_SUCCESS);
	}

out:
	/* Prepare the argument for the taskq */
	event_args = bcmp->bcmp_event_args;
	event_args->bcmea_ctlp = (void *)bcm_ctlp;
	event_args->bcmea_portp = (void *)bcmp;
	event_args->bcmea_event = port_qsr;

	/* Start the taskq to handle error recovery */
	if ((ddi_taskq_dispatch(bcm_ctlp->bcmc_event_taskq,
	    bcm_events_handler,
	    (void *)event_args, DDI_NOSLEEP)) != DDI_SUCCESS) {
		cmn_err(CE_WARN, "!bcm%d: bcm start taskq for "
		    "event handler failed", instance);
	}
	mutex_exit(&bcmp->bcmp_mutex);
	return (BCM_SUCCESS);
}

/*
 * Enable the interrupts for a particular port.
 *
 * WARNING!!! bcmp_mutex should be acquired before the function
 * is called.
 */
static void
bcm_enable_port_intrs(bcm_ctl_t *bcm_ctlp, uint8_t port)
{
	BCMDBG1(BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_enable_port_intrs enter: port %d", port);

	/*
	 * Clear port interrupt status before enabling interrupt
	 */
	ddi_put32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_SATA_ERROR(bcm_ctlp, port),
	    BCM_SER_CLEAR_ALL);

	ddi_put32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QSR(bcm_ctlp, port),
	    BCM_QSR_CLEAR_ALL);

	/*
	 * Enable port interrupt
	 */
	ddi_put32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QMR(bcm_ctlp, port),
	    BCM_QDMA_CMD_CPLT | BCM_QDMA_CMD_ERR);

	ddi_put32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_SATA_SIMR(bcm_ctlp, port),
	    BCM_SER_PHY_RDY_CHG);

}

/*
 * Enable interrupts for all the ports.
 *
 * WARNING!!! bcmc_mutex should be acquired before the function
 * is called.
 */
static void
bcm_enable_all_intrs(bcm_ctl_t *bcm_ctlp)
{
	uint32_t GIMR_masks;

	BCMDBG0(BCMDBG_ENTRY, bcm_ctlp, "bcm_enable_all_intrs enter");

	/* Enable all interrupts */
	GIMR_masks = ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_GIMR(bcm_ctlp));
	GIMR_masks &= ~BCM_GIMR_ALL;
	ddi_put32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_GIMR(bcm_ctlp), GIMR_masks);
}

/*
 * Disable interrupts for a particular port.
 *
 * WARNING!!! bcmp_mutex should be acquired before the function
 * is called.
 */
static void
bcm_disable_port_intrs(bcm_ctl_t *bcm_ctlp, uint8_t port)
{
	BCMDBG1(BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_disable_port_intrs enter, port %d", port);

	/* disable port interrupts */
	ddi_put32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QMR(bcm_ctlp, port),
	    BCM_QDMA_DISABLE_ALL);
	ddi_put32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_SATA_SIMR(bcm_ctlp, port),
	    BCM_SIMR_DISABLE_ALL);

}

/*
 * Disable interrupts for the whole HBA.
 *
 * The global bit is cleared, then all interrupt sources from all
 * ports are disabled.
 *
 * WARNING!!! bcmc_mutex should be acquired before the function
 * is called.
 */
static void
bcm_disable_all_intrs(bcm_ctl_t *bcm_ctlp)
{
	uint32_t GIMR_masks;
	BCMDBG0(BCMDBG_ENTRY, bcm_ctlp, "bcm_disable_all_intrs enter");

	GIMR_masks = ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_GIMR(bcm_ctlp));
	GIMR_masks |=  BCM_GIMR_ALL;
	ddi_put32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_GIMR(bcm_ctlp), GIMR_masks);
}

/*
 * Handle INTx and legacy interrupts.
 */
static int
bcm_add_legacy_intrs(bcm_ctl_t *bcm_ctlp)
{
	dev_info_t	*dip = bcm_ctlp->bcmc_dip;
	int		actual, count = 0;
	int		x, y, rc, inum = 0;

	BCMDBG0(BCMDBG_ENTRY|BCMDBG_INIT, bcm_ctlp,
	    "bcm_add_legacy_intrs enter");

	/* get number of interrupts. */
	rc = ddi_intr_get_nintrs(dip, DDI_INTR_TYPE_FIXED, &count);
	if ((rc != DDI_SUCCESS) || (count == 0)) {
		BCMDBG2(BCMDBG_INTR|BCMDBG_INIT, bcm_ctlp,
		    "ddi_intr_get_nintrs() failed, "
		    "rc %d count %d\n", rc, count);
		return (DDI_FAILURE);
	}

	/* Allocate an array of interrupt handles. */
	bcm_ctlp->bcmc_intr_size = count * sizeof (ddi_intr_handle_t);
	bcm_ctlp->bcmc_intr_htable =
	    kmem_zalloc(bcm_ctlp->bcmc_intr_size, KM_SLEEP);

	/* call ddi_intr_alloc(). */
	rc = ddi_intr_alloc(dip, bcm_ctlp->bcmc_intr_htable,
	    DDI_INTR_TYPE_FIXED,
	    inum, count, &actual, DDI_INTR_ALLOC_STRICT);

	if ((rc != DDI_SUCCESS) || (actual == 0)) {
		BCMDBG1(BCMDBG_INTR|BCMDBG_INIT, bcm_ctlp,
		    "ddi_intr_alloc() failed, rc %d\n", rc);
		kmem_free(bcm_ctlp->bcmc_intr_htable,
		    bcm_ctlp->bcmc_intr_size);
		return (DDI_FAILURE);
	}

	if (actual < count) {
		BCMDBG2(BCMDBG_INTR|BCMDBG_INIT, bcm_ctlp,
		    "Requested: %d, Received: %d", count, actual);

		for (x = 0; x < actual; x++) {
			(void) ddi_intr_free(bcm_ctlp->bcmc_intr_htable[x]);
		}

		kmem_free(bcm_ctlp->bcmc_intr_htable,
		    bcm_ctlp->bcmc_intr_size);
		return (DDI_FAILURE);
	}

	bcm_ctlp->bcmc_intr_cnt = actual;

	/* Get intr priority. */
	if (ddi_intr_get_pri(bcm_ctlp->bcmc_intr_htable[0],
	    &bcm_ctlp->bcmc_intr_pri) != DDI_SUCCESS) {
		BCMDBG0(BCMDBG_INTR|BCMDBG_INIT, bcm_ctlp,
		    "ddi_intr_get_pri() failed");

		for (x = 0; x < actual; x++) {
			(void) ddi_intr_free(bcm_ctlp->bcmc_intr_htable[x]);
		}

		kmem_free(bcm_ctlp->bcmc_intr_htable,
		    bcm_ctlp->bcmc_intr_size);
		return (DDI_FAILURE);
	}

	/* Test for high level interrupt. */
	if (bcm_ctlp->bcmc_intr_pri >= ddi_intr_get_hilevel_pri()) {
		BCMDBG0(BCMDBG_INTR|BCMDBG_INIT, bcm_ctlp,
		    "bcm_add_legacy_intrs: Hi level intr not supported");

		for (x = 0; x < actual; x++) {
			(void) ddi_intr_free(bcm_ctlp->bcmc_intr_htable[x]);
		}

		kmem_free(bcm_ctlp->bcmc_intr_htable,
		    sizeof (ddi_intr_handle_t));

		return (DDI_FAILURE);
	}

	/* Call ddi_intr_add_handler(). */
	for (x = 0; x < actual; x++) {
		if (ddi_intr_add_handler(bcm_ctlp->bcmc_intr_htable[x],
		    bcm_intr, (caddr_t)bcm_ctlp, NULL) != DDI_SUCCESS) {
			BCMDBG0(BCMDBG_INTR|BCMDBG_INIT, bcm_ctlp,
			    "ddi_intr_add_handler() failed");

			for (y = 0; y < actual; y++) {
				(void) ddi_intr_free(
				    bcm_ctlp->bcmc_intr_htable[y]);
			}

			kmem_free(bcm_ctlp->bcmc_intr_htable,
			    bcm_ctlp->bcmc_intr_size);
			return (DDI_FAILURE);
		}
	}

	/* Call ddi_intr_enable() for legacy interrupts. */
	for (x = 0; x < bcm_ctlp->bcmc_intr_cnt; x++) {
		(void) ddi_intr_enable(bcm_ctlp->bcmc_intr_htable[x]);
	}

	return (DDI_SUCCESS);
}

/*
 * Handle MSI interrupts.
 */
static int
bcm_add_msi_intrs(bcm_ctl_t *bcm_ctlp)
{
	dev_info_t *dip = bcm_ctlp->bcmc_dip;
	int		count, avail, actual;
	int		x, y, rc, inum = 0;

	BCMDBG0(BCMDBG_ENTRY|BCMDBG_INIT, bcm_ctlp,
	    "bcm_add_msi_intrs enter");

	/* get number of interrupts. */
	rc = ddi_intr_get_nintrs(dip, DDI_INTR_TYPE_MSI, &count);
	if ((rc != DDI_SUCCESS) || (count == 0)) {
		BCMDBG2(BCMDBG_INTR|BCMDBG_INIT, bcm_ctlp,
		    "ddi_intr_get_nintrs() failed, "
		    "rc %d count %d\n", rc, count);
		return (DDI_FAILURE);
	}

	/* get number of available interrupts. */
	rc = ddi_intr_get_navail(dip, DDI_INTR_TYPE_MSI, &avail);
	if ((rc != DDI_SUCCESS) || (avail == 0)) {
		BCMDBG2(BCMDBG_INTR|BCMDBG_INIT, bcm_ctlp,
		    "ddi_intr_get_navail() failed, "
		    "rc %d avail %d\n", rc, avail);
		return (DDI_FAILURE);
	}

#if BCM_DEBUG
	if (avail < count) {
		BCMDBG2(BCMDBG_INTR|BCMDBG_INIT, bcm_ctlp,
		    "ddi_intr_get_nvail returned %d, navail() returned %d",
		    count, avail);
	}
#endif

	/* Allocate an array of interrupt handles. */
	bcm_ctlp->bcmc_intr_size = count * sizeof (ddi_intr_handle_t);
	bcm_ctlp->bcmc_intr_htable =
	    kmem_alloc(bcm_ctlp->bcmc_intr_size, KM_SLEEP);

	/* call ddi_intr_alloc(). */
	rc = ddi_intr_alloc(dip, bcm_ctlp->bcmc_intr_htable,
	    DDI_INTR_TYPE_MSI, inum, count, &actual, DDI_INTR_ALLOC_NORMAL);

	if ((rc != DDI_SUCCESS) || (actual == 0)) {
		BCMDBG1(BCMDBG_INTR|BCMDBG_INIT, bcm_ctlp,
		    "ddi_intr_alloc() failed, rc %d\n", rc);
		kmem_free(bcm_ctlp->bcmc_intr_htable,
		    bcm_ctlp->bcmc_intr_size);
		return (DDI_FAILURE);
	}

	/* use interrupt count returned */
#if BCM_DEBUG
	if (actual < count) {
		BCMDBG2(BCMDBG_INTR|BCMDBG_INIT, bcm_ctlp,
		    "Requested: %d, Received: %d", count, actual);
	}
#endif

	bcm_ctlp->bcmc_intr_cnt = actual;

	/*
	 * Get priority for first msi, assume remaining are all the same.
	 */
	if (ddi_intr_get_pri(bcm_ctlp->bcmc_intr_htable[0],
	    &bcm_ctlp->bcmc_intr_pri) != DDI_SUCCESS) {
		BCMDBG0(BCMDBG_INTR|BCMDBG_INIT, bcm_ctlp,
		    "ddi_intr_get_pri() failed");

		/* Free already allocated intr. */
		for (y = 0; y < actual; y++) {
			(void) ddi_intr_free(bcm_ctlp->bcmc_intr_htable[y]);
		}

		kmem_free(bcm_ctlp->bcmc_intr_htable,
		    bcm_ctlp->bcmc_intr_size);
		return (DDI_FAILURE);
	}

	/* Test for high level interrupt. */
	if (bcm_ctlp->bcmc_intr_pri >= ddi_intr_get_hilevel_pri()) {
		BCMDBG0(BCMDBG_INTR|BCMDBG_INIT, bcm_ctlp,
		    "bcm_add_msi_intrs: Hi level intr not supported");

		/* Free already allocated intr. */
		for (y = 0; y < actual; y++) {
			(void) ddi_intr_free(bcm_ctlp->bcmc_intr_htable[y]);
		}

		kmem_free(bcm_ctlp->bcmc_intr_htable,
		    sizeof (ddi_intr_handle_t));

		return (DDI_FAILURE);
	}

	/* Call ddi_intr_add_handler(). */
	for (x = 0; x < actual; x++) {
		if (ddi_intr_add_handler(bcm_ctlp->bcmc_intr_htable[x],
		    bcm_intr, (caddr_t)bcm_ctlp, NULL) != DDI_SUCCESS) {
			BCMDBG0(BCMDBG_INTR|BCMDBG_INIT, bcm_ctlp,
			    "ddi_intr_add_handler() failed");

			/* Free already allocated intr. */
			for (y = 0; y < actual; y++) {
				(void) ddi_intr_free(
				    bcm_ctlp->bcmc_intr_htable[y]);
			}

			kmem_free(bcm_ctlp->bcmc_intr_htable,
			    bcm_ctlp->bcmc_intr_size);
			return (DDI_FAILURE);
		}
	}


	(void) ddi_intr_get_cap(bcm_ctlp->bcmc_intr_htable[0],
	    &bcm_ctlp->bcmc_intr_cap);

	if (bcm_ctlp->bcmc_intr_cap & DDI_INTR_FLAG_BLOCK) {
		/* Call ddi_intr_block_enable() for MSI. */
		(void) ddi_intr_block_enable(bcm_ctlp->bcmc_intr_htable,
		    bcm_ctlp->bcmc_intr_cnt);
	} else {
		/* Call ddi_intr_enable() for MSI non block enable. */
		for (x = 0; x < bcm_ctlp->bcmc_intr_cnt; x++) {
			(void) ddi_intr_enable(
			    bcm_ctlp->bcmc_intr_htable[x]);
		}
	}

	return (DDI_SUCCESS);
}

/*
 * Removes the registered interrupts irrespective of whether they
 * were legacy or MSI.
 *
 * WARNING!!! The controller interrupts must be disabled before calling
 * this routine.
 */
static void
bcm_rem_intrs(bcm_ctl_t *bcm_ctlp)
{
	int x;

	BCMDBG0(BCMDBG_ENTRY, bcm_ctlp, "bcm_rem_intrs entered");

	/* Disable all interrupts. */
	if ((bcm_ctlp->bcmc_intr_type == DDI_INTR_TYPE_MSI) &&
	    (bcm_ctlp->bcmc_intr_cap & DDI_INTR_FLAG_BLOCK)) {
		/* Call ddi_intr_block_disable(). */
		(void) ddi_intr_block_disable(bcm_ctlp->bcmc_intr_htable,
		    bcm_ctlp->bcmc_intr_cnt);
	} else {
		for (x = 0; x < bcm_ctlp->bcmc_intr_cnt; x++) {
			(void) ddi_intr_disable(
			    bcm_ctlp->bcmc_intr_htable[x]);
		}
	}

	/* Call ddi_intr_remove_handler(). */
	for (x = 0; x < bcm_ctlp->bcmc_intr_cnt; x++) {
		(void) ddi_intr_remove_handler(
		    bcm_ctlp->bcmc_intr_htable[x]);
		(void) ddi_intr_free(bcm_ctlp->bcmc_intr_htable[x]);
	}

	kmem_free(bcm_ctlp->bcmc_intr_htable, bcm_ctlp->bcmc_intr_size);
}

/*
 * This routine will pause QDMA engine by setting port QDMA
 * control register's PAUSE bit.
 *
 * WARNING!!! bcmp_mutex should be acquired before the function
 * is called.
 */
static int
bcm_pause_port_QDMA_engine(bcm_ctl_t *bcm_ctlp,
    bcm_port_t *bcmp, uint8_t port)
{
	int i = 0;
	uint32_t port_QSR_status;

	BCMDBG1(BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_pause_port_QDMA_engine enter: port %d", port);

	if (!(bcmp->bcmp_qdma_engine_flags & BCM_QDMA_QCR_ENABLE) ||
	    bcmp->bcmp_qdma_engine_flags & BCM_QDMA_QCR_PAUSE) {
		BCMDBG1(BCMDBG_INIT, bcm_ctlp,
		    "bcm_pause_port_QDMA_engine: PAUSE bit set "
		    "bcmp->bcmp_qdma_engine_flags 0x%x",
		    bcmp->bcmp_qdma_engine_flags);
		return (BCM_SUCCESS);
	}

	/* Set PAUSE bit in QCR */
	ddi_put32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QCR(bcm_ctlp, port),
	    bcmp->bcmp_qdma_engine_flags | BCM_QDMA_QCR_PAUSE);

	/*
	 * Read QSR to see if QDMA engine is paused or not
	 */
	while (i < BCM_NUMBER_FOR_LOOPS) {

		port_QSR_status = ddi_get32(bcm_ctlp->bcmc_bar_handle,
		    (uint32_t *)BCM_QDMA_QSR(bcm_ctlp, port));
		if (port_QSR_status & BCM_QDMA_PAUSE_ACK) {
			break;
		}
		i++;
		drv_usecwait(BCM_100_USECS);
	}

	if (i == BCM_NUMBER_FOR_LOOPS) {
		BCMDBG1(BCMDBG_INIT, bcm_ctlp,
		    "bcm_pause_port_QDMA_engine: failed to set "
		    "QDMA QCR's PAUSE bit: "
		    "port_QSR_status = 0x%x", port_QSR_status);
		return (BCM_FAILURE);
	} else {
		bcmp->bcmp_qdma_engine_flags |= BCM_QDMA_QCR_PAUSE;
		BCMDBG2(BCMDBG_INIT, bcm_ctlp,
		    "bcm_pause_port_QDMA_engine: succeeded to set "
		    "QDMA QCR's PAUSE bit: "
		    "port_QSR_status = 0x%x "
		    "bcmp->bcmp_qdma_engine_flags = 0x%x",
		    port_QSR_status, bcmp->bcmp_qdma_engine_flags);
		return (BCM_SUCCESS);
	}
}

/*
 * This routine will unpause QDMA engine by unsetting port QDMA
 * control register's PAUSE bit.
 *
 * WARNING!!! bcmp_mutex should be acquired before the function
 * is called.
 */
static int
bcm_unpause_port_QDMA_engine(bcm_ctl_t *bcm_ctlp,
    bcm_port_t *bcmp, uint8_t port)
{
	int i = 0;
	uint32_t port_QSR_status;

	BCMDBG1(BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_unpause_port_QDMA_engine enter: port %d", port);

	if (!(bcmp->bcmp_qdma_engine_flags & BCM_QDMA_QCR_PAUSE)) {
		BCMDBG1(BCMDBG_INIT, bcm_ctlp,
		    "bcm_unpause_port_QDMA_engine: PAUSE bit unset "
		    "bcmp->bcmp_qdma_engine_flags 0x%x",
		    bcmp->bcmp_qdma_engine_flags);
		return (BCM_SUCCESS);
	}

	/*
	 * Unset PAUSE bit in QCR
	 */
	ddi_put32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QCR(bcm_ctlp, port),
	    bcmp->bcmp_qdma_engine_flags & (~BCM_QDMA_QCR_PAUSE));

	/*
	 * Read QSR to see if QDMA engine is unpaused or not
	 */
	while (i < BCM_NUMBER_FOR_LOOPS) {
		port_QSR_status = ddi_get32(bcm_ctlp->bcmc_bar_handle,
		    (uint32_t *)BCM_QDMA_QSR(bcm_ctlp, port));

		if (!(port_QSR_status & BCM_QDMA_PAUSE_ACK)) {
			break;
		}
		i++;
		drv_usecwait(BCM_100_USECS);
	}

	BCMDBG2(BCMDBG_INIT, bcm_ctlp, "bcm_unpause_port_QDMA_engine: "
	    "i = %d port_QSR_status = 0x%x ", i, port_QSR_status);

	if (i == BCM_NUMBER_FOR_LOOPS) {
		BCMDBG1(BCMDBG_INIT, bcm_ctlp,
		    "bcm_unpause_port_QDMA_engine: failed to unset "
		    "QDMA QCR's PAUSE bit: "
		    "port_QSR_status = 0x%x", port_QSR_status);
		return (BCM_FAILURE);
	} else {
		bcmp->bcmp_qdma_engine_flags &= ~BCM_QDMA_QCR_PAUSE;
		BCMDBG2(BCMDBG_INIT, bcm_ctlp,
		    "bcm_unpause_port_QDMA_engine: succeeded to unset "
		    "QDMA QCR's PAUSE bit: "
		    "port_QSR_status = 0x%x bcmp->bcmp_qdma_engine_flags"
		    " = 0x%x",
		    port_QSR_status, bcmp->bcmp_qdma_engine_flags);
		return (BCM_SUCCESS);
	}
}

/*
 * This routine will disable QDMA engine by clearing port QDMA
 * control register's ENABLE bit.
 *
 * WARNING!!! bcmp_mutex should be acquired before the function
 * is called.
 */
static int
bcm_disable_port_QDMA_engine(bcm_ctl_t *bcm_ctlp,
    bcm_port_t *bcmp, uint8_t port)
{
	uint32_t port_QCR_status;

	BCMDBG1(BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_disable_port_QDMA_engine enter: port %d", port);

	/*
	 * Clear ENABLE bit in QCR
	 */
	ddi_put32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QCR(bcm_ctlp, port), 0);

	/* Wait for 1 millisec */
	drv_usecwait(BCM_1MS_USECS);

	/* read back QCR to verify if the QCR ENABLE bit has been cleared. */
	port_QCR_status = ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QCR(bcm_ctlp, port));
	if (port_QCR_status & BCM_QDMA_QCR_ENABLE) {
		BCMDBG1(BCMDBG_INIT, bcm_ctlp,
		    "bcm_disable_port_QDMA_engine: failed to clear "
		    "QDMA QCR's ENABLE bit: "
		    "port_QCR_status = 0x%x", port_QCR_status);
		return (BCM_FAILURE);
	} else {
		bcmp->bcmp_qpi = 0;
		bcmp->bcmp_intr_idx = 0;
		/* ATAPI command not running */
		bcmp->bcmp_qdma_pkt_cmd_running = 0;
		bcmp->bcmp_qdma_engine_flags = 0;
		ddi_put32(bcm_ctlp->bcmc_bar_handle,
		    (uint32_t *)BCM_QDMA_QCI(bcm_ctlp, port),
		    BCM_IDX_INIT_VALUE);
		ddi_put32(bcm_ctlp->bcmc_bar_handle,
		    (uint32_t *)BCM_QDMA_QPI(bcm_ctlp, port),
		    BCM_IDX_INIT_VALUE);
		ddi_put32(bcm_ctlp->bcmc_bar_handle,
		    (uint32_t *)BCM_QDMA_QSR(bcm_ctlp, port),
		    BCM_QSR_CLEAR_ALL);
		bcmp->bcmp_flags &= ~BCM_PORT_FLAG_STARTED;
		BCMDBG1(BCMDBG_INIT, bcm_ctlp,
		    "bcm_disable_port_QDMA_engine: succeeded to clear "
		    "QDMA QCR's ENABLE bit: "
		    "port_QCR_status = 0x%x", port_QCR_status);
		return (BCM_SUCCESS);
	}
}

/*
 * This routine will enable QDMA engine by setting port QDMA
 * control register's ENABLE bit.
 *
 * WARNING!!! bcmp_mutex should be acquired before the function
 * is called.
 */
static int
bcm_enable_port_QDMA_engine(bcm_ctl_t *bcm_ctlp,
    bcm_port_t *bcmp, uint8_t port)
{
	uint32_t port_QCR_status;

	BCMDBG1(BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_enable_port_QDMA_engine enter: port %d", port);

	/*
	 * Set ENABLE bit in QCR
	 */
	ddi_put32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QCR(bcm_ctlp, port),
	    BCM_QDMA_QCR_ENABLE);

	/* Wait for 1 millisec */
	drv_usecwait(BCM_1MS_USECS);

	/* read back QCR to verify if the QCR ENABLE bit has been set. */
	port_QCR_status = ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QCR(bcm_ctlp, port));
	if (port_QCR_status & BCM_QDMA_QCR_ENABLE) {
		BCMDBG1(BCMDBG_INIT, bcm_ctlp,
		    "bcm_enable_port_QDMA_engine: succeeded to set "
		    "QDMA QCR's ENABLE bit: "
		    "port_QCR_status = 0x%x", port_QCR_status);
		bcmp->bcmp_qdma_engine_flags |= BCM_QDMA_QCR_ENABLE;
		return (BCM_SUCCESS);
	} else {
		BCMDBG1(BCMDBG_INIT, bcm_ctlp,
		    "bcm_enable_port_QDMA_engine: failed to set "
		    "QDMA QCR's ENABLE bit: "
		    "port_QCR_status = 0x%x", port_QCR_status);
		return (BCM_FAILURE);
	}
}

/*
 * First disable QDMA, and then check task file status register. If both
 * BSY bit and DRQ cleared to '0', it means the device is in a stable
 * state, then enable QDMA engine to start the port directly. if BSY or
 * DRQ is set to '1', then issue a COMRESET to the device to put it in
 * an idle state.
 *
 * The fifth argument returns whether the port reset is involved during
 * the process.
 *
 * The routine will be called under six scenarios:
 *	1. Initialize the port
 *	2. To abort the packet(s)
 *	3. To reset the port
 *	4. Fatal error recovery
 *	5. To abort the timeout packet(s)
 *
 * WARNING!!! bcmp_mutex should be acquired before the function
 * is called. And bcmp_mutex will be released before the reset
 * event is reported to sata module by calling sata_hba_event_notify,
 * and then be acquired again later.
 *
 * NOTES!!! During this procedure, port sata error register will be
 * cleared.
 *
 */
static int
bcm_restart_port_wait_till_ready(bcm_ctl_t *bcm_ctlp,
    bcm_port_t *bcmp, uint8_t port, int flag, int *reset_flag)
{
	uint8_t task_file_status;
	sata_device_t sdevice;
	int rval;

	BCMDBG1(BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_restart_port_wait_till_ready: port %d enter", port);

	/* First clear ENABLE bit in port's QCR */
	if ((rval = bcm_disable_port_QDMA_engine(bcm_ctlp, bcmp, port))
	    != BCM_SUCCESS)
		goto reset;
	/* Disable port interrupts */
	(void) bcm_disable_port_intrs(bcm_ctlp, port);

	/* Then clear port sata error register */
	ddi_put32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_SATA_ERROR(bcm_ctlp, port),
	    BCM_SER_CLEAR_ALL);

	/* Then get port task file status register */
	task_file_status = ddi_get8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_STATUS(bcm_ctlp, port));

	/*
	 * Check whether the device is in a stable status, if yes,
	 * then start the port directly. However for bcm_tran_dport_reset,
	 * we may have to perform a port reset.
	 */
	if (!(task_file_status & (BCM_TFD_STS_BSY | BCM_TFD_STS_DRQ)) &&
	    !(flag & BCM_PORT_RESET)) {
		goto out;
	}

	BCMDBG2(BCMDBG_INFO, bcm_ctlp, "bcm_restart_port_wait_till_ready: "
	    "port %d task_file_status 0x%x", port, task_file_status);
reset:
	/*
	 * If task file status register's BSY bit or DRQ bit is set to '1', then
	 * issue a COMRESET to the device
	 */
	rval = bcm_port_reset(bcm_ctlp, bcmp, port);

	if (reset_flag != NULL)
		*reset_flag = 1;

	/* Indicate to the framework that a reset has happened. */
	if ((bcmp->bcmp_device_type != SATA_DTYPE_NONE) &&
	    !(flag & BCM_RESET_NO_EVENTS_UP)) {
		/* Set the reset in progress flag */
		bcmp->bcmp_reset_in_progress = 1;

		bzero((void *)&sdevice, sizeof (sata_device_t));
		sdevice.satadev_addr.cport = port;
		sdevice.satadev_addr.pmport = 0;
		sdevice.satadev_addr.qual = SATA_ADDR_DCPORT;

		sdevice.satadev_state = SATA_DSTATE_RESET |
		    SATA_DSTATE_PWR_ACTIVE;
		if (bcm_ctlp->bcmc_sata_hba_tran) {
			mutex_exit(&bcmp->bcmp_mutex);
			sata_hba_event_notify(
			    bcm_ctlp->bcmc_sata_hba_tran->sata_tran_hba_dip,
			    &sdevice,
			    SATA_EVNT_DEVICE_RESET);
			mutex_enter(&bcmp->bcmp_mutex);
		}

		BCMDBG1(BCMDBG_EVENT, bcm_ctlp,
		    "port %d sending event up: SATA_EVNT_RESET", port);
	} else {
		bcmp->bcmp_reset_in_progress = 0;
	}

out:
	/* Start the port if not in initialization phase */
	if (flag & BCM_PORT_INIT)
		return (rval);

	rval = bcm_start_port(bcm_ctlp, bcmp, port);

	return (rval);
}

/*
 * This routine may be called under four scenarios:
 *	a) do the recovery from fatal error
 *	b) or we need to timeout some commands
 *	c) or we need to abort some commands
 *	d) or we need reset device/port
 *
 * In all these scenarios, we need to send any pending unfinished
 * commands up to sata framework.
 *
 * WARNING!!! bcmp_mutex should be acquired before the function is called.
 */
static void
bcm_mop_commands(bcm_ctl_t *bcm_ctlp,
    bcm_port_t *bcmp,
    int flag,
    int failed_slot,
    int timeout_slot,
    int aborted_slot,
    int reset_slot)
{
	int slot;
	sata_pkt_t *satapkt;
	/* uint32_t qci; */

	BCMDBG1(BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_mop_commands entered: port: %d", bcmp->bcmp_num);

	BCMDBG5(BCMDBG_INFO, bcm_ctlp,
	    "bcm_mop_commands: flag: %d, "
	    "failed_slot: %d, "
	    "timeout_slot: %d aborted_slot: %d, "
	    "reset_slot: %d", flag, failed_slot,
	    timeout_slot, aborted_slot, reset_slot);

	if (flag == SATA_ABORT_ALL_PACKETS ||
	    flag == BCM_SATA_RESET_ALL)
		goto all;

	/* Send up failed packet with SATA_PKT_DEV_ERROR. */
	if (failed_slot != BCM_NO_FAILED_SLOT) {
		/* there is a failed command */
		if (failed_slot == BCM_NON_QDMA_SLOT_NO) {
			satapkt = bcmp->bcmp_nonqdma_cmd->bcm_spkt;
			SENDUP_PACKET(bcmp, satapkt,
			    SATA_PKT_DEV_ERROR);
			bcmp->bcmp_nonqdma_cmd->bcm_spkt = NULL;
		} else {
			satapkt = bcmp->bcmp_slot_pkts[failed_slot];
			SENDUP_PACKET(bcmp, satapkt,
			    SATA_PKT_DEV_ERROR);
			bcmp->bcmp_slot_pkts[failed_slot] = NULL;
		}
	}

	/* Send up timeout packet with SATA_PKT_TIMEOUT. */
	if (timeout_slot != BCM_NO_TIMEOUT_SLOT) {
		/* there is a timeout_slot */
		if (timeout_slot == BCM_NON_QDMA_SLOT_NO) {
			satapkt = bcmp->bcmp_nonqdma_cmd->bcm_spkt;
			SENDUP_PACKET(bcmp, satapkt,
			    SATA_PKT_TIMEOUT);
			bcmp->bcmp_nonqdma_cmd->bcm_spkt = NULL;
		} else {
			satapkt = bcmp->bcmp_slot_pkts[timeout_slot];
			SENDUP_PACKET(bcmp, satapkt,
			    SATA_PKT_TIMEOUT);
			bcmp->bcmp_slot_pkts[timeout_slot] = NULL;
		}
	}

	/* Send up aborted packet with SATA_PKT_ABORTED */
	if (aborted_slot != BCM_NO_ABORTED_SLOT) {
		/* there is a aborted_slot */
		if (aborted_slot == BCM_NON_QDMA_SLOT_NO) {
			satapkt = bcmp->bcmp_nonqdma_cmd->bcm_spkt;
			SENDUP_PACKET(bcmp, satapkt,
			    SATA_PKT_ABORTED);
			bcmp->bcmp_nonqdma_cmd->bcm_spkt = NULL;
		} else {
			satapkt = bcmp->bcmp_slot_pkts[aborted_slot];
			SENDUP_PACKET(bcmp, satapkt,
			    SATA_PKT_ABORTED);
			bcmp->bcmp_slot_pkts[aborted_slot] = NULL;
		}
	}

	/* Send up reset packet with SATA_PKT_RESET. */
	if (reset_slot != BCM_NO_RESET_SLOT) { /* there is a reset_slot */
		if (reset_slot == BCM_NON_QDMA_SLOT_NO) {
			satapkt = bcmp->bcmp_nonqdma_cmd->bcm_spkt;
			SENDUP_PACKET(bcmp, satapkt,
			    SATA_PKT_RESET);
			bcmp->bcmp_nonqdma_cmd->bcm_spkt = NULL;
		} else {
			satapkt = bcmp->bcmp_slot_pkts[reset_slot];
			SENDUP_PACKET(bcmp, satapkt,
			    SATA_PKT_RESET);
			bcmp->bcmp_slot_pkts[reset_slot] = NULL;
		}
	}

all:
	/* Send up unfinished packets in QDMA with SATA_PKT_RESET */
	for (slot = 0; slot < BCM_CTL_QUEUE_DEPTH; slot++) {

		satapkt = bcmp->bcmp_slot_pkts[slot];
		if (satapkt == NULL)
			continue;

		BCMDBG1(BCMDBG_ERRS, bcm_ctlp, "bcm_mop_commands: "
		    "sending up pkt 0x%p with SATA_PKT_RESET",
		    (void *)satapkt);

		if (flag == SATA_ABORT_ALL_PACKETS) {
			SENDUP_PACKET(bcmp, satapkt, SATA_PKT_ABORTED);
		} else {
			SENDUP_PACKET(bcmp, satapkt, SATA_PKT_RESET);
		}
		bcmp->bcmp_slot_pkts[slot] = NULL;
	}

	/* Send up unfinished PIO packet */
	satapkt = bcmp->bcmp_nonqdma_cmd->bcm_spkt;
	if (satapkt != NULL) {
		if (flag == SATA_ABORT_ALL_PACKETS) {
			SENDUP_PACKET(bcmp, satapkt, SATA_PKT_ABORTED);
		} else {
			SENDUP_PACKET(bcmp, satapkt, SATA_PKT_RESET);
		}
		bcmp->bcmp_nonqdma_cmd->bcm_spkt = NULL;
	}

	bcmp->bcmp_mop_in_progress--;
	ASSERT(bcmp->bcmp_mop_in_progress >= 0);

	if (bcmp->bcmp_mop_in_progress == 0)
		bcmp->bcmp_flags &= ~BCM_PORT_FLAG_MOPPING;
}

/*
 * Start a PIO data-in ATA command
 */
static int
bcm_start_pio_in(bcm_ctl_t *bcm_ctlp, bcm_port_t *bcmp, uint8_t port,
    sata_pkt_t *spkt)
{
	BCMDBG1(BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_start_pio_in enter: port %d", port);

	bcmp->bcmp_nonqdma_cmd->bcm_v_addr =
	    spkt->satapkt_cmd.satacmd_bp->b_un.b_addr;
	bcmp->bcmp_nonqdma_cmd->bcm_byte_count =
	    spkt->satapkt_cmd.satacmd_bp->b_bcount;
	bcmp->bcmp_nonqdma_cmd->bcm_spkt = spkt;
	bcmp->bcmp_nonqdma_cmd->bcm_cmd_flags = BCM_NONQDMA_CMD_BUSY;

	bcm_program_taskfile_regs(bcm_ctlp, port, spkt);

	BCMDBG2(BCMDBG_INFO, bcm_ctlp, "bcm_start_pio_in: "
	    "port %d spkt->satapkt_cmd.satacmd_cmd_reg 0x%x",
	    port, spkt->satapkt_cmd.satacmd_cmd_reg);
	/*
	 * This next one sets the drive in motion
	 */
	ddi_put8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_CMD(bcm_ctlp, port),
	    spkt->satapkt_cmd.satacmd_cmd_reg);

	return (BCM_SUCCESS);
}

/*
 * Start a PIO data-out ATA command
 */
static int
bcm_start_pio_out(bcm_ctl_t *bcm_ctlp, bcm_port_t *bcmp, uint8_t port,
    sata_pkt_t *spkt)
{

	BCMDBG1(BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_start_pio_out enter: port %d", port);

	bcmp->bcmp_nonqdma_cmd->bcm_v_addr =
	    spkt->satapkt_cmd.satacmd_bp->b_un.b_addr;
	bcmp->bcmp_nonqdma_cmd->bcm_byte_count =
	    spkt->satapkt_cmd.satacmd_bp->b_bcount;
	bcmp->bcmp_nonqdma_cmd->bcm_spkt = spkt;
	bcmp->bcmp_nonqdma_cmd->bcm_cmd_flags = BCM_NONQDMA_CMD_BUSY;

	bcm_program_taskfile_regs(bcm_ctlp, port, spkt);

	/*
	 * this next one sets the drive in motion
	 */
	ddi_put8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_CMD(bcm_ctlp, port),
	    spkt->satapkt_cmd.satacmd_cmd_reg);

	/*
	 * wait for the busy bit to settle
	 */
	BCM_DELAY_NSEC(400);

	/*
	 * wait for the drive to assert DRQ to send the first chunk
	 * of data. Have to busy wait because there's no interrupt for
	 * the first chunk. This is bad... uses a lot of cycles if the
	 * drive responds too slowly or if the wait loop granularity
	 * is too large. It's even worse if the drive is defective and
	 * the loop times out.
	 */
	if (bcm_wait3(bcm_ctlp, port, SATA_STATUS_DRQ,
	    SATA_STATUS_BSY, /* okay */
	    SATA_STATUS_ERR, SATA_STATUS_BSY, /* cmd failed */
	    SATA_STATUS_DF, SATA_STATUS_BSY, /* drive failed */
	    4000000, 0) == B_FALSE) {
		bcmp->bcmp_nonqdma_cmd->bcm_spkt = NULL;
		bcmp->bcmp_nonqdma_cmd->bcm_cmd_flags =
		    BCM_NONQDMA_CMD_COMPLETE;
		spkt->satapkt_reason = SATA_PKT_TIMEOUT;
		goto error;
	}

	/*
	 * send the first block.
	 */
	bcm_intr_pio_out(bcm_ctlp, bcmp, port);

	if (bcmp->bcmp_nonqdma_cmd->bcm_cmd_flags !=
	    BCM_NONQDMA_CMD_COMPLETE) {
		return (BCM_SUCCESS);
	}

	error:
	/*
	 * there was an error so reset the device and complete the packet.
	 */
	bcm_update_sata_registers(bcm_ctlp, port,
	    &spkt->satapkt_device);
	bcm_copy_err_cnxt(bcm_ctlp, bcmp, port);
	bcmp->bcmp_flags |= BCM_PORT_FLAG_MOPPING;
	bcmp->bcmp_mop_in_progress++;
	(void) bcm_restart_port_wait_till_ready(bcm_ctlp,
	    bcmp, port, BCM_PORT_RESET, NULL);
	bcm_mop_commands(bcm_ctlp,
	    bcmp,
	    BCM_SATA_RESET_ALL,
	    BCM_NO_FAILED_SLOT,
	    BCM_NO_TIMEOUT_SLOT,
	    BCM_NO_ABORTED_SLOT,
	    BCM_NO_RESET_SLOT);

	return (SATA_TRAN_PORT_ERROR);
}

/*
 * start a non QDMA ATAPI command
 */
static int
bcm_start_non_qdma_pkt_cmd(bcm_ctl_t *bcm_ctlp, bcm_port_t *bcmp, uint8_t port,
    sata_pkt_t *spkt)
{
	int direction;
	sata_cmd_t *satacmd = &spkt->satapkt_cmd;

	BCMDBG1(BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_start_non_qdma_pkt_cmd enter: port %d", port);

	direction = satacmd->satacmd_flags.sata_data_direction;
	if ((direction == SATA_DIR_READ) ||
	    (direction == SATA_DIR_WRITE)) {
		bcmp->bcmp_nonqdma_cmd->bcm_v_addr =
		    spkt->satapkt_cmd.satacmd_bp->b_un.b_addr;
		bcmp->bcmp_nonqdma_cmd->bcm_byte_count =
		    spkt->satapkt_cmd.satacmd_bp->b_bcount;
	}

	bcmp->bcmp_nonqdma_cmd->bcm_spkt = spkt;
	bcmp->bcmp_nonqdma_cmd->bcm_cmd_flags = BCM_NONQDMA_CMD_BUSY;

	/*
	 * Write the PACKET command to the command register.  Normally
	 * this would be done through bcm_program_taskfile_regs().  It
	 * is done here because some values need to be overridden.
	 */

	/* select the drive */
	ddi_put8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_DRVHD(bcm_ctlp, port),
	    satacmd->satacmd_device_reg);

	/* make certain the drive selected */
	if (bcm_wait(bcm_ctlp, port, SATA_STATUS_DRDY, SATA_STATUS_BSY,
	    BCM_SEC2USEC(5), 0) == B_FALSE) {
		BCMDBG0(BCMDBG_INFO, bcm_ctlp,
		    "bcm_start_non_qdma_pkt_cmd: drive select failed");
		spkt->satapkt_reason = SATA_PKT_DEV_ERROR;
		bcmp->bcmp_nonqdma_cmd->bcm_spkt = NULL;
		bcmp->bcmp_nonqdma_cmd->bcm_cmd_flags =
		    BCM_NONQDMA_CMD_COMPLETE;

		bcmp->bcmp_flags |= BCM_PORT_FLAG_MOPPING;
		bcmp->bcmp_mop_in_progress++;
		/* reset the port */
		(void) bcm_restart_port_wait_till_ready(bcm_ctlp, bcmp,
		    port, BCM_PORT_RESET, NULL);
		bcm_mop_commands(bcm_ctlp, bcmp,
		    BCM_SATA_RESET_ALL,
		    BCM_NO_FAILED_SLOT,
		    BCM_NO_TIMEOUT_SLOT,
		    BCM_NO_ABORTED_SLOT,
		    BCM_NO_RESET_SLOT);

		return (SATA_PKT_DEV_ERROR);
	}

	/*
	 * Despite whatever the SATA framework sets in the command. Overwrite
	 * the DMA bit so that the command can be done by PIO. Also, overwrite
	 * the overlay bit to be safe (it shouldn't be set).
	 */
	/* deassert DMA and OVL */
	ddi_put8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_FEATURE(bcm_ctlp, port), 0);

	/* set appropriately by the sata framework */
	ddi_put8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_HCYL(bcm_ctlp, port),
	    satacmd->satacmd_lba_high_lsb);
	ddi_put8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_LCYL(bcm_ctlp, port),
	    satacmd->satacmd_lba_mid_lsb);
	ddi_put8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_SECT(bcm_ctlp, port),
	    satacmd->satacmd_lba_low_lsb);
	ddi_put8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_COUNT(bcm_ctlp, port),
	    satacmd->satacmd_sec_count_lsb);

	/* initiate the command by writing the command register last */
	ddi_put8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_CMD(bcm_ctlp, port),
	    spkt->satapkt_cmd.satacmd_cmd_reg);

	/* Give the host controller time to do its thing */
	BCM_DELAY_NSEC(400);

	/*
	 * Wait for the device to indicate that it is ready for the command
	 * ATAPI protocol state - HP0: Check_Status_A
	 */
	if (bcm_wait3(bcm_ctlp, port, SATA_STATUS_DRQ, SATA_STATUS_BSY,
	    SATA_STATUS_ERR, SATA_STATUS_BSY, /* cmd failed */
	    SATA_STATUS_DF, SATA_STATUS_BSY, /* drive failed */
	    4000000, 0) == B_FALSE) {
		/*
		 * Either an error or device fault occurred or the wait
		 * timed out.  According to the ATAPI protocol, command
		 * completion is also possible.	 Other implementations of
		 * this protocol don't handle this last case, so neither
		 * does this code.
		 */
		if (ddi_get8(bcm_ctlp->bcmc_bar_handle,
		    (uint8_t *)BCM_STATUS(bcm_ctlp, port)) &
		    (SATA_STATUS_ERR | SATA_STATUS_DF)) {
			spkt->satapkt_reason = SATA_PKT_DEV_ERROR;
			BCMDBG0(BCMDBG_ERRS, bcm_ctlp,
			    "bcm_start_non_qdma_pkt_cmd: device error (HP0)");
		} else {
			spkt->satapkt_reason = SATA_PKT_TIMEOUT;
			BCMDBG0(BCMDBG_INFO, bcm_ctlp,
			    "bcm_start_non_qdma_pkt_cmd: timeout (HP0)");
		}

		bcm_update_sata_registers(bcm_ctlp, port,
		    &spkt->satapkt_device);
		bcm_copy_err_cnxt(bcm_ctlp, bcmp, port);
		bcmp->bcmp_nonqdma_cmd->bcm_spkt = NULL;
		bcmp->bcmp_nonqdma_cmd->bcm_cmd_flags =
		    BCM_NONQDMA_CMD_COMPLETE;
		bcmp->bcmp_flags |= BCM_PORT_FLAG_MOPPING;
		bcmp->bcmp_mop_in_progress++;
		(void) bcm_restart_port_wait_till_ready(bcm_ctlp, bcmp,
		    port, BCM_PORT_RESET, NULL);
		bcm_mop_commands(bcm_ctlp, bcmp,
		    BCM_SATA_RESET_ALL,
		    BCM_NO_FAILED_SLOT,
		    BCM_NO_TIMEOUT_SLOT,
		    BCM_NO_ABORTED_SLOT,
		    BCM_NO_RESET_SLOT);
		return (SATA_TRAN_PORT_ERROR);
	}

	/*
	 * Put the ATAPI command in the data register
	 * ATAPI protocol state - HP1: Send_Packet
	 */
	ddi_rep_put16(bcm_ctlp->bcmc_bar_handle,
	    (ushort_t *)spkt->satapkt_cmd.satacmd_acdb,
	    (ushort_t *)BCM_DATA(bcm_ctlp, port),
	    (spkt->satapkt_cmd.satacmd_acdb_len >> 1), DDI_DEV_NO_AUTOINCR);

	/*
	 * See you in bcm_intr_non_qdma_pkt_cmd.
	 * ATAPI protocol state - HP3: INTRQ_wait
	 */
	BCMDBG0(BCMDBG_INFO, bcm_ctlp,
	    "bcm_start_non_qdma_pkt_cmd: exiting into HP3");

	return (BCM_SUCCESS);
}

/*
 * Get request sense data and stuff it the command's sense buffer.
 * Start a request sense command in order to get sense data to insert
 * in the sata packet's rqsense buffer.  The command completion
 * processing is in bcm_intr_non_qdma_pkt_cmd.
 *
 * For Broadcom HT1000 SATA controller, request sense packet command can't
 * be executed via QDMA. It can only be executed via PIO.
 *
 * The sata framework provides a function to allocate and set-up a
 * request sense packet command. The reasons it is not being used here is:
 * a) it cannot be called in an interrupt context and this function is
 *    called in an interrupt context.
 * b) it allocates DMA resources that are not used here because this is
 *    implemented using PIO.
 *
 * If, in the future, this is changed to use DMA, the sata framework should
 * be used to allocate and set-up the error retrieval (request sense)
 * command.
 */
static int
bcm_start_rqsense_pkt_cmd(bcm_ctl_t *bcm_ctlp, bcm_port_t *bcmp, uint8_t port,
    sata_pkt_t *spkt)
{
	sata_cmd_t *satacmd = &spkt->satapkt_cmd;
	int cdb_len = spkt->satapkt_cmd.satacmd_acdb_len;

	BCMDBG1(BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_start_rqsense_pkt_cmd enter: port %d", port);

	/* clear the local request sense buffer before starting the command */
	bzero(bcmp->bcmp_nonqdma_cmd->bcm_rqsense_buff, SATA_ATAPI_RQSENSE_LEN);

	/* Write the request sense PACKET command */

	/* select the drive */
	ddi_put8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_DRVHD(bcm_ctlp, port),
	    satacmd->satacmd_device_reg);

	/* make certain the drive selected */
	if (bcm_wait(bcm_ctlp, port, SATA_STATUS_DRDY, SATA_STATUS_BSY,
	    BCM_SEC2USEC(5), 0) == B_FALSE) {
		BCMDBG0(BCMDBG_ERRS, bcm_ctlp,
		    "bcm_start_rqsense_pkt_cmd: drive select failed");
		return (SATA_TRAN_PORT_ERROR);
	}

	/* set up the command */

	/* deassert DMA and OVL */
	ddi_put8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_FEATURE(bcm_ctlp, port), 0);

	ddi_put8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_HCYL(bcm_ctlp, port),
	    SATA_ATAPI_MAX_BYTES_PER_DRQ >> 8);
	ddi_put8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_LCYL(bcm_ctlp, port),
	    SATA_ATAPI_MAX_BYTES_PER_DRQ & 0xff);
	ddi_put8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_SECT(bcm_ctlp, port), 0);
	ddi_put8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_COUNT(bcm_ctlp, port), 0); /* no tag */

	/* initiate the command by writing the command register last */
	ddi_put8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_CMD(bcm_ctlp, port),
	    SATAC_PACKET);

	/* Give the host ctlr time to do its thing, according to ATA/ATAPI */
	BCM_DELAY_NSEC(400);

	/*
	 * Wait for the device to indicate that it is ready for the command
	 * ATAPI protocol state - HP0: Check_Status_A
	 */
	if (bcm_wait3(bcm_ctlp, port, SATA_STATUS_DRQ, SATA_STATUS_BSY,
	    SATA_STATUS_ERR, SATA_STATUS_BSY, /* cmd failed */
	    SATA_STATUS_DF, SATA_STATUS_BSY, /* drive failed */
	    4000000, 0) == B_FALSE) {
		if (ddi_get8(bcm_ctlp->bcmc_bar_handle,
		    (uint8_t *)BCM_STATUS(bcm_ctlp, port)) &
		    (SATA_STATUS_ERR | SATA_STATUS_DF)) {
			BCMDBG0(BCMDBG_ERRS, bcm_ctlp,
			    "bcm_start_rqsense_pkt_cmd: "
			    "rqsense dev error (HP0)");
		} else {
			BCMDBG0(BCMDBG_INFO, bcm_ctlp,
			    "bcm_start_rqsense_pkt_cmd: rqsense timeout (HP0)");
		}
		return (SATA_TRAN_PORT_ERROR);
	}

	/*
	 * Put the ATAPI command in the data register
	 * ATAPI protocol state - HP1: Send_Packet
	 */
	ddi_rep_put16(bcm_ctlp->bcmc_bar_handle, (ushort_t *)bcm_rqsense_cdb,
	    (ushort_t *)BCM_DATA(bcm_ctlp, port),
	    (cdb_len >> 1), DDI_DEV_NO_AUTOINCR);

	BCMDBG0(BCMDBG_INFO, bcm_ctlp,
	    "bcm_start_rqsense_pkt_cmd: exiting into HP3");

	return (BCM_SUCCESS);
}

/*
 * Start a command that involves no media access
 */
static int
bcm_start_nodata(bcm_ctl_t *bcm_ctlp, bcm_port_t *bcmp, uint8_t port,
    sata_pkt_t *spkt)
{

	BCMDBG1(BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_start_nodata enter: port %d", port);

	bcmp->bcmp_nonqdma_cmd->bcm_spkt = spkt;
	bcmp->bcmp_nonqdma_cmd->bcm_cmd_flags = BCM_NONQDMA_CMD_BUSY;

	bcm_program_taskfile_regs(bcm_ctlp, port, spkt);


	/*
	 * This next one sets the controller in motion
	 */
	ddi_put8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_CMD(bcm_ctlp, port),
	    spkt->satapkt_cmd.satacmd_cmd_reg);

	return (BCM_SUCCESS);
}

/*
 * program taskfile registers to be used for command when QDMA is disabled
 * or paused.
 */
static void
bcm_program_taskfile_regs(bcm_ctl_t *bcm_ctlp, int port, sata_pkt_t *spkt)
{
	ddi_acc_handle_t cmdhdl = bcm_ctlp->bcmc_bar_handle;
	sata_cmd_t *scmd = &spkt->satapkt_cmd;

	BCMDBG1(BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_program_taskfile_regs enter: port %d", port);
	/*
	 * select the drive
	 */
	ddi_put8(cmdhdl,
	    (uint8_t *)BCM_DRVHD(bcm_ctlp, port),
	    scmd->satacmd_device_reg);

	/*
	 * make certain the drive selected
	 */
	if (bcm_wait(bcm_ctlp, port, SATA_STATUS_DRDY, SATA_STATUS_BSY,
	    BCM_SEC2USEC(5), 0) == B_FALSE) {
		BCMDBG0(BCMDBG_INFO, bcm_ctlp, "timeout on DRDY and BSY");
		return;
	}

	switch (spkt->satapkt_cmd.satacmd_addr_type) {

	case 0:
		BCMDBG0(BCMDBG_INFO, bcm_ctlp, "non-media access mode");
		/*
		 * non-media access commands such as identify and features
		 * take this path.
		 */
		/* Fall through */
	case ATA_ADDR_LBA:
	case ATA_ADDR_LBA28:
		BCMDBG0(BCMDBG_INFO, bcm_ctlp, "ATA_ADDR_LBA28 mode");
		ddi_put8(cmdhdl,
		    (uint8_t *)BCM_COUNT(bcm_ctlp, port),
		    scmd->satacmd_sec_count_lsb);
		ddi_put8(cmdhdl,
		    (uint8_t *)BCM_FEATURE(bcm_ctlp, port),
		    scmd->satacmd_features_reg);
		ddi_put8(cmdhdl,
		    (uint8_t *)BCM_HCYL(bcm_ctlp, port),
		    scmd->satacmd_lba_high_lsb);
		ddi_put8(cmdhdl,
		    (uint8_t *)BCM_LCYL(bcm_ctlp, port),
		    scmd->satacmd_lba_mid_lsb);
		ddi_put8(cmdhdl,
		    (uint8_t *)BCM_SECT(bcm_ctlp, port),
		    scmd->satacmd_lba_low_lsb);

		break;
	case ATA_ADDR_LBA48:

		BCMDBG0(BCMDBG_INFO, bcm_ctlp, "ATA_ADDR_LBA48 mode");

		BCMDBG1(BCMDBG_INFO, bcm_ctlp, "scmd->satacmd_features_reg_ext "
		    "%d", scmd->satacmd_features_reg_ext);
		ddi_put8(cmdhdl,
		    (uint8_t *)BCM_FEATURE(bcm_ctlp, port),
		    scmd->satacmd_features_reg_ext);
		BCMDBG1(BCMDBG_INFO, bcm_ctlp, "scmd->satacmd_features_reg "
		    "%d", scmd->satacmd_features_reg);
		ddi_put8(cmdhdl,
		    (uint8_t *)BCM_FEATURE(bcm_ctlp, port),
		    scmd->satacmd_features_reg);

		BCMDBG1(BCMDBG_INFO, bcm_ctlp, "scmd->satacmd_sec_count_msb "
		    "%d", scmd->satacmd_sec_count_msb);
		ddi_put8(cmdhdl,
		    (uint8_t *)BCM_COUNT(bcm_ctlp, port),
		    scmd->satacmd_sec_count_msb);
		BCMDBG1(BCMDBG_INFO, bcm_ctlp, "scmd->satacmd_sec_count_lsb "
		    "%d", scmd->satacmd_sec_count_lsb);
		ddi_put8(cmdhdl,
		    (uint8_t *)BCM_COUNT(bcm_ctlp, port),
		    scmd->satacmd_sec_count_lsb);

		BCMDBG1(BCMDBG_INFO, bcm_ctlp, "scmd->satacmd_lba_high_msb "
		    "%d", scmd->satacmd_lba_high_msb);
		ddi_put8(cmdhdl,
		    (uint8_t *)BCM_HCYL(bcm_ctlp, port),
		    scmd->satacmd_lba_high_msb);
		BCMDBG1(BCMDBG_INFO, bcm_ctlp, "scmd->satacmd_lba_high_lsb "
		    "%d", scmd->satacmd_lba_high_lsb);
		ddi_put8(cmdhdl,
		    (uint8_t *)BCM_HCYL(bcm_ctlp, port),
		    scmd->satacmd_lba_high_lsb);

		BCMDBG1(BCMDBG_INFO, bcm_ctlp, "scmd->satacmd_lba_mid_msb "
		    "%d", scmd->satacmd_lba_mid_msb);
		ddi_put8(cmdhdl,
		    (uint8_t *)BCM_LCYL(bcm_ctlp, port),
		    scmd->satacmd_lba_mid_msb);
		BCMDBG1(BCMDBG_INFO, bcm_ctlp, "scmd->satacmd_lba_mid_lsb "
		    "%d", scmd->satacmd_lba_mid_lsb);
		ddi_put8(cmdhdl,
		    (uint8_t *)BCM_LCYL(bcm_ctlp, port),
		    scmd->satacmd_lba_mid_lsb);

		BCMDBG1(BCMDBG_INFO, bcm_ctlp, "scmd->satacmd_lba_low_msb "
		    "%d", scmd->satacmd_lba_low_msb);
		ddi_put8(cmdhdl,
		    (uint8_t *)BCM_SECT(bcm_ctlp, port),
		    scmd->satacmd_lba_low_msb);
		BCMDBG1(BCMDBG_INFO, bcm_ctlp, "scmd->satacmd_lba_low_lsb "
		    "%d", scmd->satacmd_lba_low_lsb);
		ddi_put8(cmdhdl,
		    (uint8_t *)BCM_SECT(bcm_ctlp, port),
		    scmd->satacmd_lba_low_lsb);

		break;
	default:
		break;
	}
}

/*
 * Check to see if it is a QDMA PACKET command or a non QDMA one
 */
static int
bcm_check_atapi_qdma(bcm_port_t *bcmp, sata_pkt_t *spkt)
{
	sata_cmd_t *scmd = &spkt->satapkt_cmd;
	uint8_t cmd = scmd->satacmd_acdb[0];

	if (bcmp->bcmp_flags & BCM_PORT_FLAG_RQSENSE)
		return (B_FALSE);

	if ((cmd == SCMD_READ_G1) ||
	    (cmd == SCMD_READ_G5) ||
	    (cmd == SCMD_READ_G4) ||
	    (cmd == SCMD_WRITE_G1) ||
	    (cmd == SCMD_WRITE_G5) ||
	    (cmd == SCMD_WRITE_G4)) {
		return (B_TRUE);
	} else {
		return (B_FALSE);
	}
}

/*
 * Handle mainly disk command errors.
 *
 * WARNING!!! bcmp_mutex should be acquired before the function is called.
 */
static void
bcm_fatal_error_recovery_handler(bcm_ctl_t *bcm_ctlp,
    bcm_port_t *bcmp, uint8_t port, uint32_t status)
{
	int		failed_slot;
	sata_pkt_t	*spkt = NULL;

	BCMDBG1(BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_fatal_error_recovery_handler enter: port %d", port);

	if (status & (BCM_QDMA_PCIBUS_ERR | BCM_QDMA_PCIMSABORT_ERR |
	    BCM_QDMA_SATAIF_ERR) || status == BCM_PORT_RESET_EVENT) {
		goto reset;
	}

	failed_slot = (uint8_t)ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QCI(bcm_ctlp, port));

	spkt = bcmp->bcmp_slot_pkts[failed_slot];
	if (spkt == NULL) {
		/* May happen when PCI bus errors occur */
		goto reset;
	}

	/* Update the sata registers, especially PxSERR register */
	bcm_update_sata_registers(bcm_ctlp, port,
	    &spkt->satapkt_device);

	bcmp->bcmp_flags |= BCM_PORT_FLAG_MOPPING;
	bcmp->bcmp_mop_in_progress++;
	(void) bcm_restart_port_wait_till_ready(bcm_ctlp, bcmp,
	    port, NULL, NULL);
	bcm_mop_commands(bcm_ctlp, bcmp,
	    BCM_NO_ALL_OP,
	    failed_slot,
	    BCM_NO_TIMEOUT_SLOT,
	    BCM_NO_ABORTED_SLOT,
	    BCM_NO_RESET_SLOT);
	return;

reset:
	bcmp->bcmp_flags |= BCM_PORT_FLAG_MOPPING;
	bcmp->bcmp_mop_in_progress++;
	(void) bcm_restart_port_wait_till_ready(bcm_ctlp, bcmp,
	    port, BCM_PORT_RESET, NULL);
	bcm_mop_commands(bcm_ctlp, bcmp,
	    BCM_SATA_RESET_ALL,
	    BCM_NO_FAILED_SLOT,
	    BCM_NO_TIMEOUT_SLOT,
	    BCM_NO_ABORTED_SLOT,
	    BCM_NO_RESET_SLOT);
}

/*
 * Handle events - error recovery
 */
static void
bcm_events_handler(void *event_args)
{
	bcm_event_arg_t *bcm_event_arg;
	bcm_ctl_t *bcm_ctlp;
	bcm_port_t *bcmp;
	uint32_t event;
	uint8_t port;

	bcm_event_arg = (bcm_event_arg_t *)event_args;

	bcm_ctlp = bcm_event_arg->bcmea_ctlp;
	bcmp = bcm_event_arg->bcmea_portp;
	event = bcm_event_arg->bcmea_event;
	port = bcmp->bcmp_num;

	BCMDBG2(BCMDBG_ENTRY|BCMDBG_INTR, bcm_ctlp,
	    "bcm_events_handler enter: port %d port_qsr = 0x%x",
	    port, event);

	mutex_enter(&bcmp->bcmp_mutex);

	/*
	 * bcm_intr_phyrdy_change() may have rendered it to
	 * SATA_DTYPE_NONE.
	 */
	if (bcmp->bcmp_device_type == SATA_DTYPE_NONE) {
		BCMDBG1(BCMDBG_ENTRY|BCMDBG_INTR, bcm_ctlp,
		    "bcm_events_handler: port %d no device attached, "
		    "and just return without doing anything", port);
		goto out;
	}

	bcm_fatal_error_recovery_handler(bcm_ctlp, bcmp, port, event);
out:
	mutex_exit(&bcmp->bcmp_mutex);
}

/*
 * Handle events - hotplug
 */
static void
bcm_hotplug_events_handler(void *event_args)
{
	bcm_event_arg_t *bcm_event_arg;
	bcm_ctl_t *bcm_ctlp;
	bcm_port_t *bcmp;
	uint32_t event;
	uint8_t port;
	sata_device_t sdevice;

	bcm_event_arg = (bcm_event_arg_t *)event_args;

	bcm_ctlp = bcm_event_arg->bcmea_ctlp;
	bcmp = bcm_event_arg->bcmea_portp;
	event = bcm_event_arg->bcmea_event;
	port = bcmp->bcmp_num;

	BCMDBG2(BCMDBG_ENTRY|BCMDBG_INTR, bcm_ctlp,
	    "bcm_hotplug_events_handler enter: port %d event = 0x%x",
	    port, event);

	mutex_enter(&bcmp->bcmp_mutex);

	bzero((void *)&sdevice, sizeof (sata_device_t));
	sdevice.satadev_addr.cport = port;
	sdevice.satadev_addr.qual = SATA_ADDR_CPORT;
	sdevice.satadev_addr.pmport = 0;
	sdevice.satadev_state = SATA_PSTATE_PWRON;
	bcmp->bcmp_state = SATA_PSTATE_PWRON;

	if (event == BCM_HOT_INSERT_EVENT) {
		/* A new device has been detected. */

		/* Get the signauture of the device */
		bcm_find_dev_signature(bcm_ctlp, bcmp, port);

		/* Try to start the port */
		if (bcm_start_port(bcm_ctlp, bcmp, port)
		    != BCM_SUCCESS) {
			sdevice.satadev_state |= SATA_PSTATE_FAILED;
			BCMDBG1(BCMDBG_ERRS, bcm_ctlp,
			    "bcm_intr_phyrdy_change: port %d failed "
			    "at start port", port);
		}

		bcm_update_sata_registers(bcm_ctlp, port, &sdevice);

		mutex_exit(&bcmp->bcmp_mutex);
		sata_hba_event_notify(
		    bcm_ctlp->bcmc_sata_hba_tran->sata_tran_hba_dip,
		    &sdevice,
		    SATA_EVNT_DEVICE_ATTACHED);
	} else if (event == BCM_HOT_REMOVE_EVENT) {
		/* A device has been hot-unplugged. */

		/* First stop the port QDMA engine */
		(void) bcm_disable_port_QDMA_engine(bcm_ctlp,
		    bcmp, port);

		/* Second disable port interrupts */
		(void) bcm_disable_port_intrs(bcm_ctlp, port);

		/* Abort all the pending commands */
		bcm_reject_all_abort_pkts(bcm_ctlp, bcmp, port);

		/* Reset the port */
		(void) bcm_port_reset(bcm_ctlp, bcmp, port);

		/* An existing device is lost. */
		bcmp->bcmp_device_type = SATA_DTYPE_NONE;
		bcmp->bcmp_flags &= ~BCM_PORT_FLAG_STARTED;

		/* Enable port interrupts again */
		bcm_enable_port_intrs(bcm_ctlp, port);

		bcm_update_sata_registers(bcm_ctlp, port, &sdevice);

		mutex_exit(&bcmp->bcmp_mutex);
		sata_hba_event_notify(
		    bcm_ctlp->bcmc_sata_hba_tran->sata_tran_hba_dip,
		    &sdevice,
		    SATA_EVNT_DEVICE_DETACHED);
	}
}

/*
 * bcm_watchdog_handler() and bcm_do_sync_start will call us if they
 * detect there are some commands which are timed out.
 */
static void
bcm_timeout_pkts(bcm_ctl_t *bcm_ctlp, bcm_port_t *bcmp,
    uint8_t port, int timeout_slot)
{
	BCMDBG1(BCMDBG_TIMEOUT|BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_timeout_pkts enter: port %d", port);

	mutex_enter(&bcmp->bcmp_mutex);

	bcmp->bcmp_flags |= BCM_PORT_FLAG_MOPPING;
	bcmp->bcmp_mop_in_progress++;

	(void) bcm_restart_port_wait_till_ready(bcm_ctlp, bcmp,
	    port, NULL, NULL);

	bcm_mop_commands(bcm_ctlp,
	    bcmp,
	    BCM_NO_ALL_OP,
	    BCM_NO_FAILED_SLOT, /* failed slot */
	    timeout_slot, /* timeout slot */
	    BCM_NO_ABORTED_SLOT, /* aborted slot */
	    BCM_NO_RESET_SLOT); /* reset slot */

	mutex_exit(&bcmp->bcmp_mutex);
}

/*
 * Watchdog handler kicks in every 5 seconds to timeout any commands pending
 * for long time.
 */
static void
bcm_watchdog_handler(bcm_ctl_t *bcm_ctlp)
{
	bcm_port_t *bcmp;
	sata_pkt_t *spkt;
	int timeout_slot;
	uint8_t port;
	clock_t delivery_time;

	BCMDBG0(BCMDBG_TIMEOUT|BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_watchdog_handler entered");
	mutex_enter(&bcm_ctlp->bcmc_mutex);

	for (port = 0; port < bcm_ctlp->bcmc_num_ports; port++) {
		bcmp = bcm_ctlp->bcmc_ports[port];
		timeout_slot = -1;
		mutex_enter(&bcmp->bcmp_mutex);
		if (bcmp->bcmp_device_type == SATA_DTYPE_NONE) {
			mutex_exit(&bcmp->bcmp_mutex);
			continue;
		}

		/* Skip the check for those ports in error recovery */
		if (bcmp->bcmp_flags & BCM_PORT_FLAG_MOPPING) {
			mutex_exit(&bcmp->bcmp_mutex);
			continue;
		}

		if (bcmp->bcmp_qdma_engine_flags & BCM_QDMA_QCR_PAUSE) {
			spkt = bcmp->bcmp_nonqdma_cmd->bcm_spkt;

			if ((spkt != NULL) && spkt->satapkt_time &&
			    !(spkt->satapkt_op_mode & SATA_OPMODE_POLLING)) {
				/*
				 * We are overloading satapkt_hba_driver_private
				 * with watched_cycle count.
				 *
				 * If a packet has survived for more than it's
				 * max life cycles, it is a candidate for time
				 * out.
				 */
				delivery_time = (clock_t)
				    spkt->satapkt_hba_driver_private;
				if (TICK_TO_SEC(ddi_get_lbolt() - delivery_time)
				    > spkt->satapkt_time) {
					/* -2 represent non-qdma command slot */
					timeout_slot = -2;
					goto wdh;
				}
			}
		}

		for (int i = 0; i < BCM_CTL_QUEUE_DEPTH; i++) {
			if (bcmp->bcmp_slot_pkts[i] != NULL) {
				spkt = bcmp->bcmp_slot_pkts[i];

				delivery_time = (clock_t)
				    spkt->satapkt_hba_driver_private;
				if (TICK_TO_SEC(ddi_get_lbolt() - delivery_time)
				    > spkt->satapkt_time) {
					timeout_slot = i;
					break;
				} else {
					continue;
				}
			}
		}
wdh:
		mutex_exit(&bcmp->bcmp_mutex);
		if (timeout_slot == -1)
			continue;

		BCMDBG1(BCMDBG_INFO, bcm_ctlp,
		    "the timeout slot is %d", timeout_slot);

		mutex_exit(&bcm_ctlp->bcmc_mutex);
		bcm_timeout_pkts(bcm_ctlp, bcmp, port, timeout_slot);
		mutex_enter(&bcm_ctlp->bcmc_mutex);
	}

	/* Re-install the watchdog timeout handler */
	if (bcm_ctlp->bcmc_timeout_id != 0) {
		bcm_ctlp->bcmc_timeout_id =
		    timeout((void (*)(void *))bcm_watchdog_handler,
		    (caddr_t)bcm_ctlp, bcm_watchdog_tick);
	}

	mutex_exit(&bcm_ctlp->bcmc_mutex);
}

/*
 * Fill the error context into sata_cmd for non-queued command error.
 */
static void
bcm_copy_err_cnxt(bcm_ctl_t *bcm_ctlp, bcm_port_t *bcmp, uint8_t port)
{
	sata_pkt_t	*spkt = bcmp->bcmp_nonqdma_cmd->bcm_spkt;
	sata_cmd_t	*scmd = &spkt->satapkt_cmd;

	scmd->satacmd_status_reg = ddi_get8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_ALTSTATUS(bcm_ctlp, port));
	scmd->satacmd_error_reg = ddi_get8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_ERROR(bcm_ctlp, port));
	scmd->satacmd_device_reg = ddi_get8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_DRVHD(bcm_ctlp, port));

	if (scmd->satacmd_addr_type == ATA_ADDR_LBA48) {

		ddi_put8(bcm_ctlp->bcmc_bar_handle,
		    (uint8_t *)BCM_DEVCTL(bcm_ctlp, port),
		    BCM_TFD_DC_HOB | BCM_TFD_DC_D3);

		scmd->satacmd_sec_count_msb = ddi_get8
		    (bcm_ctlp->bcmc_bar_handle,
		    (uint8_t *)BCM_COUNT(bcm_ctlp, port));
		scmd->satacmd_lba_low_msb = ddi_get8
		    (bcm_ctlp->bcmc_bar_handle,
		    (uint8_t *)BCM_SECT(bcm_ctlp, port));
		scmd->satacmd_lba_mid_msb = ddi_get8
		    (bcm_ctlp->bcmc_bar_handle,
		    (uint8_t *)BCM_LCYL(bcm_ctlp, port));
		scmd->satacmd_lba_high_msb = ddi_get8
		    (bcm_ctlp->bcmc_bar_handle,
		    (uint8_t *)BCM_HCYL(bcm_ctlp, port));
	}

	/*
	 * disable HOB so that low byte is read
	 */
	ddi_put8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_DEVCTL(bcm_ctlp, port), BCM_TFD_DC_D3);

	scmd->satacmd_sec_count_lsb = ddi_get8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_COUNT(bcm_ctlp, port));
	scmd->satacmd_lba_low_lsb = ddi_get8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_SECT(bcm_ctlp, port));
	scmd->satacmd_lba_mid_lsb = ddi_get8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_LCYL(bcm_ctlp, port));
	scmd->satacmd_lba_high_lsb = ddi_get8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_HCYL(bcm_ctlp, port));
}

/*
 * Wait for a register of a controller to achieve a specific state.
 * To return normally, all the bits in the first sub-mask must be ON,
 * all the bits in the second sub-mask must be OFF.
 * If timeout_usec microseconds pass without the controller achieving
 * the desired bit configuration, return TRUE, else FALSE.
 *
 * hybrid waiting algorithm: if not in interrupt context, busy looping will
 * occur for the first 250 us, then switch over to a sleeping wait.
 *
 */
static int
bcm_wait(bcm_ctl_t *bcm_ctlp, uint8_t port, uchar_t onbits,
    uchar_t offbits, uint_t timeout_usec, int type_wait)
{
	hrtime_t end, cur, start_sleep, start;
	int first_time = B_TRUE;
	ushort_t val;

	for (;;) {
		val = ddi_get8(bcm_ctlp->bcmc_bar_handle,
		    (uint8_t *)BCM_ALTSTATUS(bcm_ctlp, port));

		if ((val & onbits) == onbits && (val & offbits) == 0) {

			return (B_TRUE);
		}

		cur = gethrtime();

		/*
		 * store the start time and calculate the end
		 * time.  also calculate "start_sleep" which is
		 * the point after which the driver will stop busy
		 * waiting and change to sleep waiting.
		 */
		if (first_time) {
			first_time = B_FALSE;
			/*
			 * start and end are in nanoseconds
			 */
			start = cur;
			end = start + timeout_usec * 1000;
			/*
			 * add 1 ms to start
			 */
			start_sleep =  start + 250000;

			if (servicing_interrupt()) {
				type_wait = 2;
			}
		}

		if (cur > end) {
			break;
		}

		if ((type_wait != 2) && (cur > start_sleep)) {
#if ! defined(__lock_lint)
			delay(1);
#endif
		} else {
			drv_usecwait(bcm_usec_delay);
		}
	}

	return (B_FALSE);
}

/*
 * This is a slightly more complicated version that checks
 * for error conditions and bails-out rather than looping
 * until the timeout is exceeded.
 *
 * hybrid waiting algorithm: if not in interrupt context, busy looping will
 * occur for the first 250 us, then switch over to a sleeping wait.
 */
static int
bcm_wait3(
	bcm_ctl_t	*bcm_ctlp,
	uint8_t		port,
	uchar_t		onbits1,
	uchar_t		offbits1,
	uchar_t		failure_onbits2,
	uchar_t		failure_offbits2,
	uchar_t		failure_onbits3,
	uchar_t		failure_offbits3,
	uint_t		timeout_usec,
	int		type_wait)
{
	hrtime_t end, cur, start_sleep, start;
	int first_time = B_TRUE;
	ushort_t val;

	for (;;) {
		val = ddi_get8(bcm_ctlp->bcmc_bar_handle,
		    (uint8_t *)BCM_ALTSTATUS(bcm_ctlp, port));

		/*
		 * Check for expected condition
		 */
		if ((val & onbits1) == onbits1 && (val & offbits1) == 0) {

			return (B_TRUE);
		}

		/* Check for error conditions */
		if ((val & failure_onbits2) == failure_onbits2 &&
		    (val & failure_offbits2) == 0) {

			return (B_FALSE);
		}

		if ((val & failure_onbits3) == failure_onbits3 &&
		    (val & failure_offbits3) == 0) {

			return (B_FALSE);
		}

		/*
		 * Store the start time and calculate the end
		 * time.  also calculate "start_sleep" which is
		 * the point after which the driver will stop busy
		 * waiting and change to sleep waiting.
		 */
		if (first_time) {
			first_time = B_FALSE;
			/*
			 * Start and end are in nanoseconds
			 */
			cur = start = gethrtime();
			end = start + timeout_usec * 1000;
			/*
			 * Add 1 ms to start
			 */
			start_sleep =  start + 250000;

			if (servicing_interrupt()) {
				type_wait = 2;
			}
		} else {
			cur = gethrtime();
		}

		if (cur > end) {

			break;
		}

		if ((type_wait != 2) && (cur > start_sleep)) {
#if ! defined(__lock_lint)
			delay(1);
#endif
		} else {
			drv_usecwait(bcm_usec_delay);
		}
	}

	return (B_FALSE);
}

/*
 * Dump pci bus error message in QDMA's QSR.
 */
static void
bcm_log_pci_error_message(bcm_ctl_t *bcm_ctlp, uint8_t port,
    uint32_t intr_status)
{
	int instance = ddi_get_instance(bcm_ctlp->bcmc_dip);

	if (intr_status & BCM_QDMA_PCIBUS_ERR)
		cmn_err(CE_WARN, "bcm%d: bcm port %d has PCI bus master error",
		    instance, port);

	if (intr_status & BCM_QDMA_PCIMSABORT_ERR)
		cmn_err(CE_WARN, "bcm%d: bcm port %d has PCI master "
		    "abort error", instance, port);

	cmn_err(CE_WARN, "bcm%d: bcm port %d is trying to do error "
	    "recovery from pci bus errors", instance, port);
}

/*
 * Dump command  error messages in QDMA's QSR.
 */
static void
bcm_log_cmd_error_message(bcm_ctl_t *bcm_ctlp, uint8_t port,
    uint32_t intr_status)
{
	int instance = ddi_get_instance(bcm_ctlp->bcmc_dip);

	if (intr_status & BCM_QDMA_ATACMD_ERR)
		cmn_err(CE_WARN, "bcm%d: bcm port %d has ATA command error",
		    instance, port);

	if (intr_status & BCM_QDMA_PRDDFL_ERR)
		cmn_err(CE_WARN, "bcm%d: bcm port %d has PRD deficient length "
		    "error", instance, port);

	if (intr_status & BCM_QDMA_PRDECL_ERR)
		cmn_err(CE_WARN, "bcm%d: bcm port %d has PRD excess length "
		    "error", instance, port);

	if (intr_status & BCM_QDMA_DATACRC_ERR)
		cmn_err(CE_WARN, "bcm%d: bcm port %d has data CRC error",
		    instance, port);

	cmn_err(CE_WARN, "bcm%d: bcm port %d is trying to do error "
	    "recovery from command errors\n", instance, port);
}

/*
 * Dump the serror message to the log.
 */
static void
bcm_log_serror_message(bcm_ctl_t *bcm_ctlp, uint8_t port,
    uint32_t port_serror, int debug_only)
{
	static char err_buf[512];
	static char err_msg_header[16];
	char *err_msg = err_buf;

	*err_buf = '\0';
	*err_msg_header = '\0';

	if (port_serror & SERROR_DATA_ERR_FIXED) {
		err_msg = strcat(err_msg,
		    "\tRecovered Data Integrity Error (I)\n");
	}

	if (port_serror & SERROR_COMM_ERR_FIXED) {
		err_msg = strcat(err_msg,
		    "\tRecovered Communication Error (M)\n");
	}

	if (port_serror & SERROR_DATA_ERR) {
		err_msg = strcat(err_msg,
		    "\tTransient Data Integrity Error (T)\n");
	}

	if (port_serror & SERROR_PERSISTENT_ERR) {
		err_msg = strcat(err_msg,
		    "\tPersistent Communication or Data Integrity Error (C)\n");
	}

	if (port_serror & SERROR_PROTOCOL_ERR) {
		err_msg = strcat(err_msg, "\tProtocol Error (P)\n");
	}

	if (port_serror & SERROR_INT_ERR) {
		err_msg = strcat(err_msg, "\tInternal Error (E)\n");
	}

	if (port_serror & SERROR_PHY_RDY_CHG) {
		err_msg = strcat(err_msg, "\tPhyRdy Change (N)\n");
	}

	if (port_serror & SERROR_PHY_INT_ERR) {
		err_msg = strcat(err_msg, "\tPhy Internal Error (I)\n");
	}

	if (port_serror & SERROR_COMM_WAKE) {
		err_msg = strcat(err_msg, "\tComm Wake (W)\n");
	}

	if (port_serror & SERROR_10B_TO_8B_ERR) {
		err_msg = strcat(err_msg, "\t10B to 8B Decode Error (B)\n");
	}

	if (port_serror & SERROR_DISPARITY_ERR) {
		err_msg = strcat(err_msg, "\tDisparity Error (D)\n");
	}

	if (port_serror & SERROR_CRC_ERR) {
		err_msg = strcat(err_msg, "\tCRC Error (C)\n");
	}

	if (port_serror & SERROR_HANDSHAKE_ERR) {
		err_msg = strcat(err_msg, "\tHandshake Error (H)\n");
	}

	if (port_serror & SERROR_LINK_SEQ_ERR) {
		err_msg = strcat(err_msg, "\tLink Sequence Error (S)\n");
	}

	if (port_serror & SERROR_TRANS_ERR) {
		err_msg = strcat(err_msg,
		    "\tTransport state transition error (T)\n");
	}

	if (port_serror & SERROR_FIS_TYPE) {
		err_msg = strcat(err_msg, "\tUnknown FIS Type (F)\n");
	}

	if (port_serror & SERROR_EXCHANGED_ERR) {
		err_msg = strcat(err_msg, "\tExchanged (X)\n");
	}

	if (err_msg == NULL)
		return;

	if (debug_only) {
		(void) sprintf(err_msg_header, "port %d", port);
		BCMDBG0(BCMDBG_ERRS, bcm_ctlp, err_msg_header);
		BCMDBG0(BCMDBG_ERRS, bcm_ctlp, err_msg);
	} else if (bcm_ctlp) {
		cmn_err(CE_WARN, "bcm%d: %s %s",
		    ddi_get_instance(bcm_ctlp->bcmc_dip),
		    err_msg_header, err_msg);
	} else {
		cmn_err(CE_WARN, "bc: %s %s", err_msg_header, err_msg);
	}
}

#if BCM_DEBUG
static void
bcm_dump_port_tf_registers(bcm_ctl_t *bcm_ctlp, uint8_t port)
{
	uint8_t altstatus, cylhi, cyllow, drvhead, error, scount, sector;
	uint16_t data;

	BCMDBG0(BCMDBG_INIT|BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_dump_port_tf_registers enter");

	altstatus = ddi_get8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_ALTSTATUS(bcm_ctlp, port));
	cylhi = ddi_get8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_HCYL(bcm_ctlp, port));
	cyllow = ddi_get8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_LCYL(bcm_ctlp, port));
	drvhead = ddi_get8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_DEVCTL(bcm_ctlp, port));
	error = ddi_get8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_ERROR(bcm_ctlp, port));
	scount = ddi_get8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_COUNT(bcm_ctlp, port));
	sector = ddi_get8(bcm_ctlp->bcmc_bar_handle,
	    (uint8_t *)BCM_SECT(bcm_ctlp, port));
	data = ddi_get16(bcm_ctlp->bcmc_bar_handle,
	    (uint16_t *)BCM_DATA(bcm_ctlp, port));

	BCMDBG4(BCMDBG_INFO, bcm_ctlp,
	    "bcm_dump_port_tf_registers: altstatus = 0x%x "
	    "cylhi = 0x%x cyllow = 0x%x "
	    "drvhead = 0x%x", altstatus, cylhi,
	    cyllow, drvhead);

	BCMDBG4(BCMDBG_INFO, bcm_ctlp,
	    "bcm_dump_port_tf_registers: error =  0x%x "
	    "scount = 0x%x sector = 0x%x data = 0x%x ",
	    error, scount, sector, data);
}
#endif

#if BCM_DEBUG
static void
bcm_dump_port_registers(bcm_ctl_t *bcm_ctlp, uint8_t port)
{
	uint32_t sstatus, serror, scontrol, sactive;
	uint32_t simr, qpi, qci, qcr, qsr, qmr;

	BCMDBG0(BCMDBG_INIT|BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_dump_port_registers enter");

	sstatus = ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_SATA_STATUS(bcm_ctlp, port));
	serror = ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_SATA_ERROR(bcm_ctlp, port));
	scontrol = ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_SATA_CONTROL(bcm_ctlp, port));
	sactive = ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_SATA_ACTIVE(bcm_ctlp, port));
	simr = ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_SATA_SIMR(bcm_ctlp, port));
	qpi = ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QPI(bcm_ctlp, port));
	qci = ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QCI(bcm_ctlp, port));
	qcr = ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QCR(bcm_ctlp, port));
	qsr = ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QSR(bcm_ctlp, port));
	qmr = ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_QMR(bcm_ctlp, port));

	BCMDBG5(BCMDBG_INFO, bcm_ctlp,
	    "bcm_dump_port_registers: port %d\n"
	    "sstatus = 0x%x serror = 0x%x\n"
	    "scontrol = 0x%x sactive = 0x%x\n",
	    port, sstatus, serror, scontrol, sactive);

	BCMDBG4(BCMDBG_INFO, bcm_ctlp,
	    "bcm_dump_port_registers: port %d\n"
	    "simr =  0x%x\n"
	    "qpi = 0x%x qci = 0x%x\n",
	    port, simr, qpi, qci);

	BCMDBG4(BCMDBG_INFO, bcm_ctlp,
	    "bcm_dump_port_registers: port %d\n"
	    "qcr = 0x%x qsr = 0x%x\n"
	    "qmr = 0x%x\n", port, qcr, qsr, qmr);
}
#endif

#if BCM_DEBUG
static void
bcm_dump_global_registers(bcm_ctl_t *bcm_ctlp)
{
	uint32_t gcr, gsr, gitr, gimr;

	BCMDBG0(BCMDBG_INIT|BCMDBG_ENTRY, bcm_ctlp,
	    "bcm_dump_global_registers enter");

	gcr = ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_GCR(bcm_ctlp));
	gsr = ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_GSR(bcm_ctlp));
	gitr = ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_GITR(bcm_ctlp));
	gimr = ddi_get32(bcm_ctlp->bcmc_bar_handle,
	    (uint32_t *)BCM_QDMA_GIMR(bcm_ctlp));

	BCMDBG4(BCMDBG_INFO, bcm_ctlp,
	    "bcm_dump_global_registers:\n"
	    "gcr = 0x%x gsr = 0x%x gitr = 0x%x gimr = 0x%x\n",
	    gcr, gsr, gitr, gimr);
}
#endif

#if BCM_DEBUG
static void
bcm_log(bcm_ctl_t *bcm_ctlp, uint_t level, char *fmt, ...)
{
	static char name[16];
	va_list ap;

	mutex_enter(&bcm_log_mutex);

	va_start(ap, fmt);
	if (bcm_ctlp) {
		(void) sprintf(name, "bcm%d: ",
		    ddi_get_instance(bcm_ctlp->bcmc_dip));
	} else {
		(void) sprintf(name, "bc: ");
	}

	(void) vsprintf(bcm_log_buf, fmt, ap);
	va_end(ap);

	cmn_err(level, "%s%s", name, bcm_log_buf);

	mutex_exit(&bcm_log_mutex);
}
#endif

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
static int
bcm_quiesce(dev_info_t *dip)
{
	bcm_ctl_t *bcm_ctlp;
	bcm_port_t *bcmp;
	int instance, port;

	instance = ddi_get_instance(dip);
	bcm_ctlp = ddi_get_soft_state(bcm_statep, instance);

	if (bcm_ctlp == NULL)
		return (DDI_FAILURE);

	/* disable all the interrupts. */
	bcm_disable_all_intrs(bcm_ctlp);

	for (port = 0; port < bcm_ctlp->bcmc_num_ports; port++) {

		bcmp = bcm_ctlp->bcmc_ports[port];

		/* Disable port interrupts */
		bcm_disable_port_intrs(bcm_ctlp, port);

		/* Disable port QDMA engine */
		(void) bcm_disable_port_QDMA_engine(bcm_ctlp,
		    bcmp, port);

		/* Abort all pending packets */
		bcm_reject_all_abort_pkts(bcm_ctlp,
		    bcmp, port);

		/* Reset the port */
		(void) bcm_port_reset(bcm_ctlp, bcmp, port);
	}

	return (DDI_SUCCESS);
}
