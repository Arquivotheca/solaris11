/*
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 *
 * This file may contain confidential information of Marvell
 * Semiconductor, and should not be distributed in source form
 * without approval from Sun Legal.
 */

/*
 * This driver has been written from scratch by Sun Microsystems, Inc.
 * The hardware specification used to write this driver contains
 * confidential information and requires a NDA.
 */

/*
 * Driver for 88SX608X, 88SX508X and 88SX504X chip sets
 */

#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/modctl.h>
#include <sys/pci.h>
#include <sys/note.h>
#include <sys/sata/sata_hba.h>
#include <sys/sata/sata_defs.h>
#include <sys/sata/adapters/marvell88sx/marvell88sx.h>
#include <sys/sysmacros.h>

/* Absolute minimum and maximum length of scatter/gather lists */
#define	MV_MIN_DMA_NSEGS	4	/* allow read of an EFI label */
#define	MV_MAX_DMA_NSEGS	4096

/* Largest size in bytes of a single scatter/gather list entry (ePRD) */
#define	MV_MAX_DMA_REGION	(64 * 1024)

/*
 * This allows the driver to power manage the device if the device
 * supports PM.
 */
int mv_pm_enabled = 0;

/* Additional debug logging */
int mv_debug_logging = B_FALSE;
int mv_verbose = B_FALSE;


/*
 * setup the device attribute structure for little-endian,
 * strict ordering access.
 */
static ddi_device_acc_attr_t dev_attr = {
	DDI_DEVICE_ATTR_V0,
	DDI_STRUCTURE_LE_ACC,
	DDI_STRICTORDER_ACC
};

static const char *const detect_msgs[] = {
	"no device detected",			/* 0 */
	"device present but no phy",		/* 1 */
	"",					/* 2 */
	"device present and communicating",	/* 3 */
	"device present but offline"		/* 4 */
};
static const char *const speed_msgs[] = {
	"speed has not been negotiated",	/* 0 */
	"1.5Gb speed has been negotiated",	/* 1 */
	"3.0Gb speed has been negotiated"	/* 2 */
};
static const char *const power_msgs[] = {
	"device not present or no communication", /* 0 */
	"interface is active",			  /* 1 */
	"interface is in partial power state",	  /* 2 */
	"",					  /* 3 */
	"",					  /* 4 */
	"",					  /* 5 */
	"interface is in slumber power state"	  /* 6 */
};

static int mv_attach(dev_info_t *dip, ddi_attach_cmd_t cmd);
static int mv_getinfo(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
			void **result);
static int mv_detach(dev_info_t *dip, ddi_detach_cmd_t cmd);
static int mv_power(dev_info_t *dip, int component, int level);
static int mv_quiesce(dev_info_t *dip);
static uint_t mv_intr(caddr_t, caddr_t);

/*
 * SATA Framework required functions
 */
static int mv_probe_port(dev_info_t *, sata_device_t *);
static int mv_reset_port(dev_info_t *, sata_device_t *);
static int mv_start(dev_info_t *, sata_pkt_t *);
static int mv_abort(dev_info_t *, sata_pkt_t *, int);
static int mv_activate(dev_info_t *, sata_device_t *);
static int mv_deactivate(dev_info_t *, sata_device_t *);
#if defined(__lock_lint)
static int mv_selftest(dev_info_t *, sata_device_t *);
#endif


/*
 * Local Functions
 */
static boolean_t mv_map_registers(mv_ctl_t *);
static void mv_unmap_port_registers(struct mv_port_state *);
static void mv_init_ctrl_regs(mv_ctl_t *mv_ctlp);
static boolean_t mv_init_ctrl(mv_ctl_t *);
static boolean_t mv_init_subctrl(dev_info_t *, struct mv_subctlr *);
static boolean_t mv_init_port(dev_info_t *, struct mv_port_state *);
static boolean_t mv_check_link(struct mv_port_state *, sata_device_t *);
static boolean_t mv_reset_link(struct mv_port_state *, sata_device_t *,
				boolean_t);
static int mv_probe_pm(mv_ctl_t *, sata_device_t *);
static void mv_enable_dma(struct mv_port_state *);
static void mv_disable_dma(struct mv_port_state *);
static void mv_wait_for_dma(struct mv_port_state *);
static void mv_enable_port_intr(mv_ctl_t *, int);
static void mv_disable_port_intr(mv_ctl_t *, int);
static void free_edma_resources(struct mv_port_state *, unsigned int);
static void mv_pkt_timeout(void *);
static void mv_free_per_port_resource(struct mv_port_state *);
static void mv_dev_reset(struct mv_port_state *, boolean_t, boolean_t, char *);
static void mv_abort_pkt(struct mv_port_state *, sata_pkt_t **);
static void mv_abort_all_pkts(struct mv_port_state *, int);
static void mv_flush_pending_io(struct mv_port_state *, sata_pkt_t **, int);
static void mv_port_err_intr(struct mv_port_state *);
static boolean_t mv_stepping(dev_info_t *, mv_ctl_t *);
static void mv_pm_setup(dev_info_t *, mv_ctl_t *);
static int mv_add_legacy_intrs(mv_ctl_t *);
static int mv_add_msi_intrs(mv_ctl_t *);
static void mv_rem_intrs(mv_ctl_t *);
static int round_up_power_of_2(int);
static void mv_enqueue_pkt(mv_ctl_t *, sata_pkt_t *);
static sata_pkt_t *mv_dequeue_pkt(mv_ctl_t *);
static uint_t mv_do_completed_pkts(caddr_t);
static void mv_copy_out_ata_regs(sata_cmd_t *, struct mv_port_state *);
static void mv_save_phy_params(struct mv_port_state *);
static void mv_restore_phy_params(struct mv_port_state *);
static sata_pkt_t *mv_ncq_error_recovery(struct mv_port_state *);
static void mv_stuck_intr(struct mv_ctl *, uint32_t);
static void mv_log(dev_info_t *dip, int level, const char *fmt, ...);
#if defined(MV_DEBUG)
static void mv_trace_debug(dev_info_t *dip, const char *fmt, ...);
#endif

static enum mv_models mv_get_model(dev_info_t *);

/*
 * The 60xx can, but the 50xx device cannot support 512 byte PCI bursts,
 * but we control the actual burst size by programming the EDMA configuration
 * register and we will never program (or be able to program) a 50xx to
 * do 512 byte bursts.
 */
/*
 * DMA attributes for the data buffer
 */
static ddi_dma_attr_t buffer_dma_attr = {
	DMA_ATTR_V0,		/* version number */
	0ull,			/* low DMA address range */
	0xFFFFFFFFFFFFFFFFull,	/* high DMA address range */
	0xFFFFull,		/* max DMA byte */
	32,			/* DMA address alignment - worst case */
	0x3FF,			/* DMA burstsizes */
	1,			/* min effective DMA size */
	0xFFFFFFFFull,		/* max DMA xfer size */
	0xFFFFFFFFull,		/* segment boundary */
	SATA_DMA_NSEGS,		/* s/g length */
	512,			/* granularity of device */
	0,			/* bus specific DMA flags */
};

/*
 * Request queue, needs one contiguous buffer
 * The dma_attr_count_max, dma_attr_maxxfer & dma_attr_seg fields
 * are all meaningless as the command request queue is 1Kbytes
 * in length aligned on a 1Kbyte boundary and is read in 32 byte
 * chunks.  There is no hardware limitation mentioned w.r.t.
 * the dma counter register, etc.  We use an arbitrary large
 * value here as using values of 1K (or 1K-1) are unacceptable
 * to the i86pc root nexus driver.
 */
static ddi_dma_attr_t dma_attr_crqq = {
	DMA_ATTR_V0,			/* dma_attr version */
	0ull,				/* dma_attr_addr_lo */
	(uint64_t)0xFFFFFFFFFFFFFFFF,	/* dma_attr_addr_hi 64 bit address */
	(uint64_t)MV_SEG_SIZE,		/* dma_attr_count_max */
	1024,				/* dma_attr_align. */
	0x3FF,				/* dma_attr_burstsizes. */
	32,				/* dma_attr_minxfer */
	(uint64_t)MV_MAX_SIZE,		/* dma_attr_maxxfer 64 bit address */
	(uint64_t)MV_SEG_SIZE,		/* dma_attr_seg 64 bit */
	0x1,				/* dma_attr_sgllen. */
	1,				/* dma_attr_granular */
	0,				/* dma_attr_flags */
};

/*
 * For ePRD we need one contiguous buffer
 * The dma_attr_seg has no limitation for Marvell but
 * the i86pc root nexus driver needs to see MMU_PAGEOFFSET at least.
 */
static ddi_dma_attr_t dma_attr_eprd = {
	DMA_ATTR_V0,			/* dma_attr version */
	0,				/* dma_attr_addr_lo */
	(uint64_t)0xFFFFFFFFFFFFFFFF,	/* dma_attr_addr_hi */
	(uint64_t)0xFFFFFFFF,		/* dma_attr_count_max. */
	32,				/* dma_attr_align */
	0x3FF,				/* dma_attr_burstsizes */
	16,				/* dma_attr_minxfer */
	(uint64_t)0xFFFFFFFFFFFFFFFF,	/* dma_attr_maxxfer */
	(uint64_t)0xFFFFFFFFFFFFFFFF,	/* dma_attr_seg */
	1,				/* dma_attr_sgllen */
	1,				/* dma_attr_granular */
	0,				/* dma_attr_flags */
};


/*
 * Response queue, needs one contiguous buffer
 * The maximum transfer the hardware will ever try to do
 * is 256 (the whole of the command response queue), but
 * the i86pc root nexus driver needs to see at least MMU_PAGESIZE or
 * MMU_PAGEOFFSET values.
 */

static ddi_dma_attr_t dma_attr_crsq = {
	DMA_ATTR_V0,			/* dma_attr version */
	0,				/* dma_attr_addr_lo */
	(uint64_t)0xFFFFFFFFFFFFFFFF,	/* dma_attr_addr_hi 64 bit address */
	(uint64_t)0xFFFFFFFF,		/* dma_attr_count_max */
	256,				/* dma_attr_align. */
	0xFF,				/* dma_attr_burstsizes. */
	8,				/* dma_attr_minxfer */
	(uint64_t)MV_MAX_SIZE,		/* dma_attr_maxxfer 64 bit address */
	(uint64_t)MV_SEG_SIZE,		/* dma_attr_seg 64 bit */
	1,				/* dma_attr_sgllen. */
	1,				/* dma_attr_granular */
	0,				/* dma_attr_flags */
};


static struct dev_ops mv_dev_ops = {
	DEVO_REV,	/* devo_rev */
	0,		/* refcnt  */
	ddi_no_info,	/* info */
	nulldev,	/* identify */
	nulldev,	/* probe */
	mv_attach,	/* attach */
	mv_detach,	/* detach */
	nodev,		/* reset */
	NULL,		/* driver operations */
	NULL,		/* bus operations */
	mv_power,	/* power */
	mv_quiesce	/* quiesce */
};

static sata_tran_hotplug_ops_t mv_hotplug_ops = {
	SATA_TRAN_HOTPLUG_OPS_REV_1,
	mv_activate,
	mv_deactivate
};


extern struct mod_ops mod_driverops;


static  struct modldrv modldrv = {
	&mod_driverops,	/* driverops */
	"marvell88sx HBA Driver",
	&mv_dev_ops,	/* driver ops */
};

static  struct modlinkage modlinkage = {
	MODREV_1,
	&modldrv,
	NULL
};

static void *mv_state; /* soft state ptr */

int mv_dma_nsegs = SATA_DMA_NSEGS;

/*
 * Erratum FEr SATA#10. We give up after 25 link resets.
 * Tests indicated that links were online after 2 to 3 resets.
 * This will be same for all Marvell chip sets for driver hardening purpose.
 */

static int max_link_resets = 25;

/*
 * _init() initializes a loadable module. (marvell88sx)
 */
int
_init(void)
{
	int	ret;

	ret = ddi_soft_state_init(&mv_state, sizeof (mv_ctl_t), 0);

	if (ret != 0) {
		return (ret);
	}

	if ((ret = sata_hba_init(&modlinkage)) != 0) {
		ddi_soft_state_fini(&mv_state);
		return (ret);
	}

	ret = mod_install(&modlinkage);

	if (ret != 0) {
		sata_hba_fini(&modlinkage);

		/* Release soft state */
		ddi_soft_state_fini(&mv_state);

	}

	return (ret);
}
/*
 * _finit() prepares a loadable module for unloading. (marvell88sx)
 */

int
_fini(void)
{
	int	ret;

	ret = mod_remove(&modlinkage);

	if (ret != 0) {
		return (ret);
	}

	sata_hba_fini(&modlinkage);
	ddi_soft_state_fini(&mv_state);

	return (ret);
}
/*
 * _info()  retruns information about a loadable module.
 */

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

/*
 * mv_port_state_change() reports the state of the port to the
 * sata framework by calling sata_hba_event_notify().  This
 * funtion is called any time the state of the port is changed
 */

static void
mv_port_state_change(
	struct mv_port_state *portp,
	int event,
	int addr_type,
	int state)
{
	sata_device_t sdevice;
	uint32_t bridge_port_status;
	uint32_t bridge_port_serror;
	uint32_t bridge_port_control;

	if (! (portp->mv_ctlp->mv_sataframework_flags & MV_FRAMEWORK_ATTACHED))
		return;

	bridge_port_status = ddi_get32(portp->bridge_handle,
	    portp->bridge_regs + SATA_STATUS_BRIDGE_OFFSET);

	bridge_port_control = ddi_get32(portp->bridge_handle,
	    portp->bridge_regs + SATA_SCONTROL_BRIDGE_OFFSET);

	bridge_port_serror = ddi_get32(portp->bridge_handle,
	    portp->bridge_regs + SATA_SERROR_BRIDGE_OFFSET);

	ASSERT((portp->mv_ctlp->mv_model == MV_MODEL_60XX ||
	    portp->mv_ctlp->mv_model == MV_MODEL_50XX));

	switch (portp->mv_ctlp->mv_model) {
	case MV_MODEL_60XX:
		ddi_put32(portp->bridge_handle, portp->bridge_regs +
		    SATA_SERROR_BRIDGE_OFFSET, bridge_port_serror);
		break;
	case MV_MODEL_50XX:
		break;
	}

	sdevice.satadev_rev = SATA_DEVICE_REV;
	sdevice.satadev_scr.sstatus = bridge_port_status;
	sdevice.satadev_scr.serror = bridge_port_serror;
	sdevice.satadev_scr.scontrol = bridge_port_control;
	/*
	 * When NCQ and PM is implemented in phase 2 sactive,
	 * snotific and pmport fields need to be updated. for 60X1.
	 */
	sdevice.satadev_scr.sactive = 0;
	sdevice.satadev_scr.snotific = 0;
	sdevice.satadev_addr.cport = portp->port_num;
	sdevice.satadev_addr.pmport = 0;
	sdevice.satadev_addr.qual = addr_type;
	sdevice.satadev_state = state;
	/*
	 * If we notify the sata framework about a reset (maybe other things)
	 * during a panic then the framework will reject any further transport
	 * requests until the event has been processed.  However the event
	 * will never get processed since that processing thread will not
	 * get run due to our being in a panic and not scheduling other
	 * threads.  For now, do not send notifications if we are in
	 * the middle of a panic.
	 */
	if (! ddi_in_panic())
		sata_hba_event_notify(portp->mv_ctlp->mv_dip, &sdevice, event);
}

/*
 * mv_port_connect() is called when EDMA interrupt error cause register
 * eDevCon is set. (EDMA device connected).  This function calls
 * mv_port_state_chagne() in order to notify the sata framework of
 * the state change of the port.
 */

static void
mv_port_connect(struct mv_port_state *portp)
{
	int event;

	event = SATA_EVNT_LINK_ESTABLISHED;

#if defined(ADVANCED_HOT_PLUG)
	/*
	 * Framework does not support this right now, at phase II the
	 * support will be added to the framework
	 */
	uint32_t sata_inter_status;

	if (portp->mv_ctlp->mv_model == MV_MODEL_60XX) {
		sata_inter_status = ddi_get32(portp->bridge_handle,
		    portp->bridge_regs + SATA_INTERFACE_STATUS_OFFSET);
		event = (sata_inter_status & SATA_PLUG_IN) ?
		    SATA_EVNT_DEVICE_ATTACHED : 0;
		event |= (sata_inter_status & SATA_LINK_DOWN) ?
		    0 : SATA_EVNT_LINK_ESTABLISHED;
		MV_DEBUG_MSG(portp->mv_ctlp->mv_dip, MV_DBG_GEN,
		    ("mv_port_connect: port %d: cable is %s plugged in\n"),
		    portp->port_num,
		    sata_inter_status & SATA_PLUG_IN ? "" : "not ");
	}
#endif

	mv_port_state_change(portp, event, SATA_ADDR_CPORT, 0);
}

/*
 * mv_port_disconnect() is called when EDMA interrupt error cause register
 * eDevDis is set. (EDMA device disconnected).  This function calls
 * mv_port_state_chagne() in order to notify the sata framework of
 * the state change of the port.
 */

static void
mv_port_disconnect(struct mv_port_state *portp)
{
	int event;

	event = SATA_EVNT_LINK_LOST;
#if defined(ADVANCED_HOT_PLUG)
	uint32_t sata_inter_status;
	if (portp->mv_ctlp->mv_model == MV_MODEL_60XX) {
		sata_inter_status = ddi_get32(portp->bridge_handle,
		    portp->bridge_regs + SATA_INTERFACE_STATUS_OFFSET);
		event = (sata_inter_status & SATA_PLUG_IN) ?
		    0 : SATA_EVNT_DEVICE_DETACHED;
		event |= (sata_inter_status & SATA_LINK_DOWN) ?
		    SATA_EVNT_LINK_LOST : 0;
		MV_DEBUG_MSG(portp->mv_ctlp->mv_dip, MV_DBG_GEN,
		    ("mv_port_disconnect: port %d: cable is %splugged in\n"),
		    portp->port_num,
		    sata_inter_status & SATA_PLUG_IN ? "" : "not ");
	}
#endif
	mv_port_state_change(portp, event, SATA_ADDR_CPORT, 0);
}

/*
 * mv_stat_io_trans_intr() is called from mv_intr() (main interrupt
 * routine). This routine it only called for 60XX model.
 */

static void
mv_sata_io_trans_intr(mv_ctl_t *mv_ctlp, enum sata_io_trans trans_state)
{
	uint32_t trans_done;
	uint32_t offset;


	MV_DEBUG_MSG(mv_ctlp->mv_dip, MV_DBG_DEV_INTR,
	    ("mv_sata_io_trans_intr(%p, %d)\n"),
	    (void *)mv_ctlp, trans_state);

	offset = (trans_state == trans_low) ?
	    SATA_IO_TRANS_INTR_LOW_CAUSE : SATA_IO_TRANS_INTR_HIGH_CAUSE;

	trans_done = ddi_get32(mv_ctlp->mv_intr_coal_ext_handle,
	    mv_ctlp->mv_intr_coal_ext_regs + offset);

	MV_DEBUG_MSG(mv_ctlp->mv_dip, MV_DBG_DEV_INTR,
	    ("mv_sata_io_trans_intr:"
	    "trans_done 0x%x\n"), trans_done);

	ddi_put32(mv_ctlp->mv_intr_coal_ext_handle,
	    mv_ctlp->mv_intr_coal_ext_regs + offset, ~trans_done);

	MV_DEBUG_MSG(mv_ctlp->mv_dip, MV_DBG_DEV_INTR,
	    ("mv_sata_io_trans_intr: exiting\n"), NULL);
}

/*
 * mv_stat_io_trans_intr() is called from mv_intr() (main interrupt
 * routine). This routine it only called for 60XX model.
 */

static void
mv_coals_intr(mv_ctl_t *mv_ctlp)
{
	uint32_t intr_cause;

	MV_DEBUG_MSG(mv_ctlp->mv_dip, MV_DBG_DEV_INTR,
	    ("mv_coals_intr(%p)\n"), (void *)mv_ctlp);

	intr_cause = ddi_get32(mv_ctlp->mv_intr_coal_ext_handle,
	    mv_ctlp->mv_intr_coal_ext_regs + SATA_PORTS_INTR_CAUSE_OFFSET);

	MV_DEBUG_MSG(mv_ctlp->mv_dip, MV_DBG_DEV_INTR,
	    ("mv_coals_intr: intr_cause 0x%x\n"),
	    intr_cause);

	ddi_put32(mv_ctlp->mv_intr_coal_ext_handle,
	    mv_ctlp->mv_intr_coal_ext_regs + SATA_PORTS_INTR_CAUSE_OFFSET,
	    ~intr_cause);

	MV_DEBUG_MSG(mv_ctlp->mv_dip, MV_DBG_DEV_INTR,
	    ("mv_coals_intr: exiting\n"), NULL);
}

/*
 * mv_pci_err_dump() routine translates the PCI interrupt cause regsiter
 * bits to English and prints them.  This routine is called from
 * mv_pci_err().
 */
static void
mv_pci_err_dump(
	dev_info_t *dip,
	uint32_t pci_err,
	uint32_t err_cmd,
	uint32_t err_attr,
	uint32_t addr_low,
	uint32_t addr_high)
{
	uint32_t bits;
	register int i;

	static const char *mv_pci_err_strs[] = {
		"Reserved",	/* 0 */
		"SWrPerr",	/* 1 */
		"SRdPerr",	/* 2 */
		"Reserved",	/* 3 */
		"Reserved",	/* 4 */
		"MWrPerr",	/* 5 */
		"MRdPerr",	/* 6 */
		"MCTabort",	/* 7 */
		"MMabort",	/* 8 */
		"MTabort",	/* 9 */
		"Reserved",	/* 10 */
		"MRetry",	/* 11 */
		"Reserved",	/* 12 */
		"MUnExp",	/* 13 */
		"MErrMsg",	/* 14 */
		"Reserved",	/* 15 */
		"SCMabort",	/* 16 */
		"STabort",	/* 17 */
		"SCTabort",	/* 18 */
		"Reserved",	/* 19 */
		"Reserved",	/* 20 */
		"Reserved",	/* 21 */
		"Reserved",	/* 22 */
		"Reserved",	/* 23 */
		"Reserved",	/* 24 */
		"Reserved",	/* 25 */
		"Reserved",	/* 26 */
		"Reserved",	/* 27 */
		"Reserved",	/* 28 */
		"Reserved",	/* 29 */
		"Reserved",	/* 30 */
		"Reserved"	/* 31 */
	};

	cmn_err(CE_WARN, "marvell88sx%d: PCI error address 0x%x:%x",
	    ddi_get_instance(dip), addr_high, addr_low);
	cmn_err(CE_CONT, "\tPCI command 0x%x DAC %s attribute 0x%x\n",
	    err_cmd & 0xf,
	    err_cmd & 0x10 ? "true" : "false",
	    err_attr);
	bits = pci_err;
	for (i = 0; bits != 0; i++, bits >>= 1) {
		if (bits & 1)
			cmn_err(CE_CONT, "\t%s\n", mv_pci_err_strs[i]);
	}
}

/*
 * mv_pci_err() routine reads PCI error command register, PCI error attribute
 * register, PCI error address high and PCI error address low and then
 * calls mv_pci_err_dump() to translate the PCI interrupt cause register bits to
 * English.  Note: PCI error command register, PCI error error atrtribute
 * register, PCI error address (low) Register need to be read. (For example in
 * PCI error address (low) register, PCI address bits[31:0] are latched as a
 * result of an error lateched in * interrupt cause register.  Upon address
 * latched, no new address can be registered (due to another error) until the
 * register is read.).
 */

static void
mv_pci_err(mv_ctl_t *mv_ctlp)
{
	uint32_t pci_err;
	uint32_t err_cmd;
	uint32_t addr_high;
	uint32_t addr_low;
	uint32_t err_attr;

	pci_err = ddi_get32(mv_ctlp->mv_pci_intr_err_handle,
	    mv_ctlp->mv_pci_intr_err_regs + PCI_INTERRUPT_CAUSE_OFFSET);

	err_cmd = ddi_get32(mv_ctlp->mv_pci_intr_err_handle,
	    mv_ctlp->mv_pci_intr_err_regs + PCI_ERROR_COMMAND_OFFSET);

	err_attr = ddi_get32(mv_ctlp->mv_pci_intr_err_handle,
	    mv_ctlp->mv_pci_intr_err_regs + PCI_ERROR_ATTRIBUTE_OFFSET);

	addr_high = ddi_get32(mv_ctlp->mv_pci_intr_err_handle,
	    mv_ctlp->mv_pci_intr_err_regs + PCI_ERROR_ADDRESS_HIGH_OFFSET);

	addr_low = ddi_get32(mv_ctlp->mv_pci_intr_err_handle,
	    mv_ctlp->mv_pci_intr_err_regs + PCI_ERROR_ADDRESS_LOW_OFFSET);

	mv_pci_err_dump(mv_ctlp->mv_dip, pci_err, err_cmd, err_attr, addr_low,
	    addr_high);
}

/*
 * mv_crpb_done_intr() is called from mv_subctrl_intr when the EDMA is done
 */

static void
mv_crpb_done_intr(struct mv_port_state *portp, uint32_t q_ack_limit)
{
	sata_pkt_t *spkt;
	sata_cmd_t *scmd;
	uint32_t qentry;
	uint32_t q_out_ptr;
	uint32_t req_id;
#if defined(MV_DEBUG)
	uint32_t q_fill_entry;
#endif

	mutex_enter(&portp->mv_port_mutex);

	do {
		/*
		 * We avoid reading the Q Out Pointer Register for two
		 * reasons here.  First, it is slightly faster and
		 * second, on the Sun Fire x4500 every once in a long
		 * while (2 - 36 hours of stress testing) a bad
		 * read of "0" would occur which would cause a hang.
		 *
		 * The read of "0" was caused by an AMD chip issue.
		 * See CR 6490454.
		 * This issue is now documented in the product notes
		 * for X and F, 819-1162-27 and 819-5038-13.
		 * I am leaving the changes since we
		 * get a slight performance boost as a result of the
		 * change.
		 */
		q_out_ptr = portp->resp_q_out_register;
		qentry = q_out_ptr & EDMA_RESPONSE_Q_OUT_PTR_MASK;
		qentry >>= EDMA_RESPONSE_Q_OUT_PTR_SHIFT;

		(void) ddi_dma_sync(portp->crsq_dma_handle,
		    qentry * sizeof (struct cmd_resp_queue_entry),
		    sizeof (struct cmd_resp_queue_entry),
		    DDI_DMA_SYNC_FORKERNEL);

		req_id = portp->crsq_addr[qentry].id_reg & ID_REG_MASK;

#if defined(MV_DEBUG)
		q_fill_entry = ddi_get32(portp->edma_handle,
		    portp->edma_regs + EDMA_RESPONSE_Q_IN_PTR_OFFSET);
		q_fill_entry &= EDMA_RESPONSE_Q_IN_PTR_MASK;
		q_fill_entry >>= EDMA_RESPONSE_Q_IN_PTR_SHIFT;

		MV_DEBUG_MSG(portp->mv_ctlp->mv_dip, MV_DBG_RESPONSE,
		    ("mv_crpb_done_intr: response entry %d q_fill_entry "
		    "%d request id %d num_dmas %d\n"),
		    qentry, q_fill_entry, req_id, portp->num_dmas);
#endif

		/* Deal with the SATA packet that was just completed */
		spkt = portp->mv_dma_pkts[req_id];
		/* Check to see if timeouts already removed this packet */
		if (spkt != NULL) {
#if defined(MV_DEBUG)
			uint32_t sactive;

			ASSERT((portp->dma_in_use_map & (1 << req_id)) != 0);

			sactive = ddi_get32(portp->bridge_handle,
			    portp->bridge_regs +
			    SATA_INTERFACE_SACTIVE_OFFSET);

			MV_DEBUG_MSG(portp->mv_ctlp->mv_dip, MV_DBG_RESPONSE,
			    ("mv_crpb_done_intr: sactive = 0x%x "
			    "dma_in_use_map = 0x%x\n"),
			    sactive, portp->dma_in_use_map);

			ASSERT((sactive & (1 << req_id)) == 0);
#endif

			scmd = &spkt->satapkt_cmd;
			portp->mv_dma_pkts[req_id] = NULL;
			--portp->num_dmas;
			portp->dma_in_use_map &= ~(1 << req_id);

			ASSERT(portp->num_dmas >= 0 &&
			    (portp->num_dmas < MV_QDEPTH));

			scmd->satacmd_status_reg = portp->crsq_addr[qentry].
			    response_status.device_status;

			if (scmd->satacmd_status_reg & SATA_STATUS_ERR) {
				if (portp->queuing_type != MV_QUEUING_NCQ) {
					struct sata_cmd_flags *fp;

					fp = &scmd->satacmd_flags;

					/*
					 * make sure block address and sector
					 * count are copied out
					 */
					if (scmd->satacmd_addr_type ==
					    ATA_ADDR_LBA48) {
						fp->sata_copy_out_sec_count_msb
						    = B_TRUE;
						fp->sata_copy_out_lba_low_msb
						    = B_TRUE;
						fp->sata_copy_out_lba_mid_msb
						    = B_TRUE;
						fp->sata_copy_out_lba_high_msb
						    = B_TRUE;
					}
					fp->sata_copy_out_sec_count_lsb
					    = fp->sata_copy_out_lba_low_lsb
					    = fp->sata_copy_out_lba_mid_lsb
					    = fp->sata_copy_out_lba_high_lsb
					    = fp->sata_copy_out_device_reg
					    = fp->sata_copy_out_error_reg
					    = B_TRUE;
					mv_copy_out_ata_regs(scmd, portp);
				} else {
					(void) mv_ncq_error_recovery(portp);
				}
				spkt->satapkt_reason = SATA_PKT_DEV_ERROR;
			}
			else
				spkt->satapkt_reason = SATA_PKT_COMPLETED;

			if (spkt->satapkt_op_mode & SATA_OPMODE_SYNCH)
				cv_signal(&portp->mv_port_cv);
			else {
				mv_enqueue_pkt(portp->mv_ctlp, spkt);
				ddi_trigger_softintr(
				    portp->mv_ctlp->mv_softintr_id);
			}
		} else {
			MV_MSG((portp->mv_ctlp->mv_dip,
			    CE_WARN, "marvell88sx%d: port %d: DMA "
			    "completed after timed out",
			    ddi_get_instance(portp->mv_ctlp->mv_dip),
			    portp->port_num));
			ASSERT((portp->dma_in_use_map & (1 << req_id)) == 0);
		}

#if defined(MV_DEBUG)

		if (mv_debug_logging) {
			portp->mv_debug_info[portp->mv_debug_index].
			    start_or_finish = MV_DONE;
			portp->mv_debug_info[portp->mv_debug_index].req_id =
			    req_id;
			portp->mv_debug_info[portp->mv_debug_index].fill_index =
			    q_fill_entry;
			portp->mv_debug_info[portp->mv_debug_index].empty_index
			    = qentry;
			portp->mv_debug_info[portp->mv_debug_index].num_dmas =
			    portp->num_dmas;
			if (++portp->mv_debug_index == MV_DEBUG_INFO_SIZE)
				portp->mv_debug_index = 0;
		}

#endif
		/* Bump the empty response pointer */
		if (++qentry == MV_QDEPTH)
			qentry = 0;

		/* Write out the empty response pointer */
		q_out_ptr &= ~EDMA_RESPONSE_Q_OUT_PTR_MASK;
		q_out_ptr |= qentry << EDMA_RESPONSE_Q_OUT_PTR_SHIFT;
		portp->resp_q_out_register = q_out_ptr;
		ddi_put32(portp->edma_handle,
		    portp->edma_regs + EDMA_RESPONSE_Q_OUT_PTR_OFFSET,
		    q_out_ptr);

	} while (qentry != q_ack_limit);

	MV_DEBUG_MSG(portp->mv_ctlp->mv_dip, MV_DBG_RESPONSE,
	    ("mv_crpb_done_intr: leaving with dma_in_use 0x%x\n"),
	    portp->dma_in_use_map);

	if (portp->dma_frozen)
		cv_broadcast(&portp->mv_empty_cv);

	mutex_exit(&portp->mv_port_mutex);
}

/*
 * mv_dev_intr() is called from mv subctrl_intr() when non EDMA operation
 * is done
 */

static void
mv_dev_intr(struct mv_port_state *portp)
{
	sata_pkt_t *spkt;
	uint8_t status;
	sata_cmd_t *scmd;
	char *bufp;
	struct buf *bp;
	int count, resid;
	int retries;
#define	STATUS_RETRIES 10000


	MV_DEBUG_MSG(portp->mv_ctlp->mv_dip, MV_DBG_DEV_INTR,
	    ("mv_dev_intr(%p)\n"), (void *)portp);

	mutex_enter(&portp->mv_port_mutex);

	mv_disable_dma(portp);

	/* Read the status (clear pending interrupt) */
	status = (uint8_t)ddi_get32(portp->task_file1_handle,
	    (uint32_t *)(portp->task_file1_regs + AT_STATUS));

	retries = STATUS_RETRIES;
	while (status & SATA_STATUS_BSY) {
		drv_usecwait(10);
		status = (uint8_t)ddi_get32(portp->task_file1_handle,
		    (uint32_t *)(portp->task_file1_regs + AT_STATUS));
		if (--retries == 0) {
			MV_DEBUG_MSG(portp->mv_ctlp->mv_dip, MV_DBG_DEV_INTR,
			    ("mv_dev_intr: exiting with busy\n"), NULL);
			mutex_exit(&portp->mv_port_mutex);
			return;
		}
	}


	if ((spkt = portp->mv_pio_pkt) == NULL) {
		/* Was this related to a DMA packet? */
		if (portp->num_dmas > 0) {
			int tag;
			int queuing_type;

			queuing_type = portp->queuing_type;

			MV_DEBUG_MSG(portp->mv_ctlp->mv_dip, MV_DBG_DEV_INTR,
			    ("mv_dev_intr: DMA with queuing type %d\n"),
			    queuing_type);

			/* Now figure out which DMA packet failed */
			switch (queuing_type) {
			case MV_QUEUING_NCQ:
				spkt = mv_ncq_error_recovery(portp);
				mv_abort_all_pkts(portp, SATA_PKT_ABORTED);
				mv_dev_reset(portp, B_TRUE, B_FALSE,
				    "Device error for NCQ command");
				if (spkt == NULL) {
					mutex_exit(&portp->mv_port_mutex);
					return;
				}
				goto done;

			case MV_QUEUING_TCQ:
				ddi_put32(portp->task_file2_handle,
				    (uint32_t *)(portp->task_file2_regs +
				    AT_DEVCTL), 0);
				tag = (uint8_t)ddi_get32(
				    portp->task_file1_handle,
				    (uint32_t *)(portp->task_file1_regs +
				    AT_ERROR));
				tag >>= SATA_TAG_QUEUING_SHIFT;
				tag &= SATA_TAG_QUEUING_MASK;
				spkt = portp->mv_dma_pkts[tag];
				break;

			case MV_QUEUING_NONE:
				tag = ddi_ffs(portp->dma_in_use_map);
				--tag;
				spkt = portp->mv_dma_pkts[tag];
				break;
			}
		}
	}

	if (spkt == NULL) {
		MV_DEBUG_MSG(portp->mv_ctlp->mv_dip, MV_DBG_DEV_INTR,
		    ("mv_dev_intr: NULL spkt?\n"), NULL);

		mutex_exit(&portp->mv_port_mutex);
		return;
	}

	scmd = &spkt->satapkt_cmd;

	if (spkt->satapkt_reason != SATA_PKT_BUSY) {
		MV_DEBUG_MSG(portp->mv_ctlp->mv_dip, MV_DBG_DEV_INTR,
		    ("mv_dev_intr: spkt not busy - command 0x%x\n"),
		    spkt->satapkt_cmd.satacmd_cmd_reg);

		mutex_exit(&portp->mv_port_mutex);
		return;
	}

	scmd->satacmd_status_reg = status;

	if (status & SATA_STATUS_ERR) {

		/* make sure block address and sector count are copied out */
		if (scmd->satacmd_addr_type == ATA_ADDR_LBA48) {
			scmd->satacmd_flags.sata_copy_out_sec_count_msb
			    = scmd->satacmd_flags.sata_copy_out_lba_low_msb
			    = scmd->satacmd_flags.sata_copy_out_lba_mid_msb
			    = scmd->satacmd_flags.sata_copy_out_lba_high_msb
			    = B_TRUE;
		}
		scmd->satacmd_flags.sata_copy_out_sec_count_lsb
		    = scmd->satacmd_flags.sata_copy_out_lba_low_lsb
		    = scmd->satacmd_flags.sata_copy_out_lba_mid_lsb
		    = scmd->satacmd_flags.sata_copy_out_lba_high_lsb
		    = scmd->satacmd_flags.sata_copy_out_device_reg
		    = scmd->satacmd_flags.sata_copy_out_error_reg
		    = B_TRUE;
		mv_copy_out_ata_regs(scmd, portp);
		spkt->satapkt_reason = SATA_PKT_DEV_ERROR;

		/* FMA - add an event here or possible in the sata module */
		MV_DEBUG_MSG(portp->mv_ctlp->mv_dip, MV_DBG_DEV_INTR,
		    ("marvell88sx%d: port %d: error in command 0x%x: "
		    "status 0x%x error 0x%x"),
		    ddi_get_instance(portp->mv_ctlp->mv_dip),
		    portp->port_num,
		    scmd->satacmd_cmd_reg, status, scmd->satacmd_error_reg);
	} else {
		MV_DEBUG_MSG(portp->mv_ctlp->mv_dip, MV_DBG_DEV_INTR,
		    ("mv_dev_intr: status 0x%x\n"), status);

		bp = scmd->satacmd_bp;
		bufp = bp ? bp->b_un.b_addr : NULL;
		count = bp ? bp->b_bcount : 0;
		resid = bp ? bp->b_resid : 0;

		bufp += (count - resid);
		count = MIN(SATA_DISK_SECTOR_SIZE, count);

		MV_DEBUG_MSG(portp->mv_ctlp->mv_dip, MV_DBG_DEV_INTR,
		    ("mv_dev_intr: bufp 0x%p count 0x%x resid 0x%x\n"),
		    (void *)bufp, count, resid);

		switch (scmd->satacmd_flags.sata_data_direction) {
		case SATA_DIR_NODATA_XFER:
			break;
		case SATA_DIR_READ:
			/* Now read the data */
			ddi_rep_get16(portp->task_file1_handle,
			    (uint16_t *)bufp,
			    (uint16_t *)(portp->task_file1_regs + AT_DATA),
			    count >> 1, DDI_DEV_NO_AUTOINCR);
			break;
		case SATA_DIR_WRITE:
			retries = STATUS_RETRIES;
			while ((status & SATA_STATUS_DRQ) == 0) {
				status = (uint8_t)ddi_get32(
				    portp->task_file1_handle,
				    (uint32_t *)(portp->task_file1_regs +
				    AT_STATUS));
				if (--retries == 0) {
					MV_DEBUG_MSG(portp->mv_ctlp->mv_dip,
					    MV_DBG_DEV_INTR,
					    ("mv_dev_intr:"
					    "DRQ never came on\n"), NULL);

					mutex_exit(&portp->mv_port_mutex);
					return;
				}
			}

			/* Now write the data */
			ddi_rep_put16(portp->task_file1_handle,
			    (uint16_t *)bufp,
			    (uint16_t *)(portp->task_file1_regs + AT_DATA),
			    count >> 1, DDI_DEV_NO_AUTOINCR);
			break;
		default:
			MV_MSG((portp->mv_ctlp->mv_dip, CE_WARN,
			    "marvell88sx%d: port %d: unknown direction "
			    "- 0x%x\n",
			    ddi_get_instance(portp->mv_ctlp->mv_dip),
			    portp->port_num,
			    scmd->satacmd_flags.sata_data_direction));
			break;
		}
		if (bp != NULL) {
			if ((bp->b_resid -= count) != 0) {
				mutex_exit(&portp->mv_port_mutex);

				MV_DEBUG_MSG(portp->mv_ctlp->mv_dip,
				    MV_DBG_DEV_INTR,
				    ("mv_dev_intr: partial PIO exiting "
				    "resid = 0x%lx\n"), bp->b_resid);

				return;
			}
		}
		spkt->satapkt_reason = SATA_PKT_COMPLETED;

		if (scmd->satacmd_flags.sata_special_regs)
			mv_copy_out_ata_regs(scmd, portp);

	}

	portp->mv_pio_pkt = NULL;
done:
	if (spkt->satapkt_op_mode & SATA_OPMODE_SYNCH)
		cv_signal(&portp->mv_port_cv);
	else { 	/* Asynch call */
		mv_enqueue_pkt(portp->mv_ctlp, spkt);
		ddi_trigger_softintr(portp->mv_ctlp->mv_softintr_id);
	}

	mutex_exit(&portp->mv_port_mutex);

	MV_DEBUG_MSG(portp->mv_ctlp->mv_dip, MV_DBG_DEV_INTR,
	    ("mv_dev_intr: exiting\n"), NULL);
}

/*
 * mv_port_err_intr() is called from mv_subctrl_intr().  If an error bit
 * is set act accordingly
 */
static void
mv_port_err_intr(struct mv_port_state *portp)
{
	uint32_t error_cause, bits, serror = 0;
	int i;
	static const char *edma_errors_50xx[] = {
		"ATA UDMA data parity error",			/* 0 */
		"ATA UDMA PRD parity error",			/* 1 */
		"device error",					/* 2 */
		"device disconnected",				/* 3 */
		"device connected",				/* 4 */
		"DMA data overrun",				/* 5 */
		"DMA data underrun",				/* 6 */
		"reserved bit 7",				/* 7 */
		"EDMA self disabled",				/* 8 */
		"command request queue parity error",		/* 9 */
		"command response queue parity error",		/* 10 */
		"internal memory parity error",			/* 11 */
		"I/O ready time-out"				/* 12 */
	};
	static const char *edma_errors_60xx[] = {
		"ATA UDMA data parity error",			/* 0 */
		"ATA UDMA PRD parity error",			/* 1 */
		"device error",					/* 2 */
		"device disconnected",				/* 3 */
		"device connected",				/* 4 */
		"SError interrupt",				/* 5 */
		"reserved bit 6",				/* 6 */
		"EDMA self disabled",				/* 7 */
		"BIST FIS or asynchronous notification",	/* 8 */
		"command request queue parity error",		/* 9 */
		"command response queue parity error",		/* 10 */
		"internal memory parity error",			/* 11 */
		"I/O ready time-out",				/* 12 */
		"link control receive error - crc",		/* 13 */
		"link control receive error - fifo",		/* 14 */
		"link control receive error - reset",		/* 15 */
		"link control receive error - state",		/* 16 */
		"link data receive error - crc",		/* 17 */
		"link data receive error - fifo",		/* 18 */
		"link data receive error - reset",		/* 19 */
		"link data receive error - state",		/* 20 */
		"link control transmit error - crc",		/* 21 */
		"link control transmit error - fifo",		/* 22 */
		"link control transmit error - reset",		/* 23 */
		"link control transmit error - DMAT",		/* 24 */
		"link control transmit error - collision",	/* 25 */
		"link data transmit error - crc",		/* 26 */
		"link data transmit error - fifo",		/* 27 */
		"link data transmit error - reset",		/* 28 */
		"link data transmit error - DMAT",		/* 29 */
		"link data transmit error - collision",		/* 30 */
		"transport protocol error"			/* 31 */
	};
	const char **edma_errors;
	static const char *serror_bits[] = {
		"Recovered data integrity error",			/* 0 */
		"Recovered communication error",			/* 1 */
		"reserved bit 2",					/* 2 */
		"reserved bit 3",					/* 3 */
		"reserved bit 4",					/* 4 */
		"reserved bit 5",					/* 5 */
		"reserved bit 6",					/* 6 */
		"reserved bit 7",					/* 7 */
		"Non-recoverable transient data integrity error",	/* 8 */
		"Non-recoverable persistent error",			/* 9 */
		"Protocol error",					/* 10 */
		"Internal error",					/* 11 */
		"reserved bit 12",					/* 12 */
		"reserved bit 13",					/* 13 */
		"reserved bit 14",					/* 14 */
		"reserved bit 15",					/* 15 */
		"PHY ready change",					/* 16 */
		"PHY internal error",					/* 17 */
		"Communication wake",					/* 18 */
		"10-bit to 8-bit decode error",				/* 19 */
		"Disparity error",					/* 20 */
		"CRC error",						/* 21 */
		"Handshake error",					/* 22 */
		"Link sequence error",					/* 23 */
		"Transport state transition error",			/* 24 */
		"Unrecognized FIS type",				/* 25 */
		"Device exchanged",					/* 26 */
		"reserved bit 27",					/* 27 */
		"reserved bit 28",					/* 28 */
		"reserved bit 29",					/* 29 */
		"reserved bit 30",					/* 30 */
		"reserved bit 31"					/* 31 */
	};
	/* Should this EDMA error be logged or just kstat counted? */
	static const uint32_t edma_error_log_mask =
	    (1U << 0) |
	    (1U << 1) |
	    (1U << 2) |
	    (1U << 3) |
	    (1U << 4) |
	    (1U << 5) |
	    (1U << 6) |
	    (1U << 7) |
	    (1U << 8) |
	    (1U << 9) |
	    (1U << 10) |
	    (1U << 11) |
	    (1U << 12) |
	    (1U << 13) |
	    (1U << 14) |
	    (1U << 15) |
	    (1U << 16) |
	    (1U << 17) |
	    (1U << 18) |
	    (1U << 19) |
	    (1U << 20) |
	    (1U << 21) |
	    (1U << 22) |
	    (1U << 23) |
	    (1U << 24) |
	    (0U << 25) |
	    (1U << 26) |
	    (1U << 27) |
	    (1U << 28) |
	    (1U << 29) |
	    (1U << 30) |
	    (1U << 31);

	mutex_enter(&portp->mv_port_mutex);

#if defined(MV_DEBUG)
	if (portp->mv_ctlp->mv_model == MV_MODEL_60XX) {
		uint32_t sata_inter_status;

		sata_inter_status = ddi_get32(portp->bridge_handle,
		    portp->bridge_regs + SATA_INTERFACE_STATUS_OFFSET);
		MV_DEBUG_MSG(portp->mv_ctlp->mv_dip, MV_DBG_DEV_INTR,
		    ("mv_port_error_intr: interface status is 0x%x\n"),
		    sata_inter_status);
	}
#endif

	/*
	 * Look at the response status and if there is an error.
	 * Note that and clear the EDMA Interrupt Cause Register bits
	 */
	error_cause = ddi_get32(portp->edma_handle,
	    portp->edma_regs + EDMA_INTERRUPT_ERROR_CAUSE_OFFSET);

	ASSERT((portp->mv_ctlp->mv_model == MV_MODEL_60XX ||
	    portp->mv_ctlp->mv_model == MV_MODEL_50XX));


	switch (portp->mv_ctlp->mv_model) {
	case MV_MODEL_60XX:
		if (error_cause & EDMA_INTR_ERROR_CAUSE_SERROR) {
			serror = ddi_get32(portp->bridge_handle,
			    portp->bridge_regs +
			    SATA_SERROR_BRIDGE_OFFSET);
			ddi_put32(portp->bridge_handle,
			    portp->bridge_regs +
			    SATA_SERROR_BRIDGE_OFFSET, serror);
		}
		break;
	case MV_MODEL_50XX:
		break;
	}

	if (error_cause & EDMA_INTR_ERROR_CAUSE_DEVICE_ERR) {
		MV_DEBUG_MSG(portp->mv_ctlp->mv_dip, MV_DBG_DEV_INTR,
		    ("mv_port_error_intr: device error\n"), NULL);

		/* Was this related to a DMA packet? */
		if (portp->num_dmas > 0) {
			sata_pkt_t *spkt;
			sata_cmd_t *scmd;
			struct sata_cmd_flags *fp;
			int tag;
			int queuing_type;

			MV_DEBUG_MSG(portp->mv_ctlp->mv_dip,
			    MV_DBG_DEV_INTR, ("mv_port_error_intr: "
			    "error during DMA\n"), NULL);

			queuing_type = portp->queuing_type;

			/* Now figure out which DMA packet failed */
			switch (queuing_type) {
			case MV_QUEUING_NCQ:
				spkt = mv_ncq_error_recovery(portp);
				if (spkt != NULL) {
					mv_flush_pending_io(portp, &spkt,
					    SATA_PKT_DEV_ERROR);
				}
				tag = -1;	/* Indicate no useful tag */
				break;

			case MV_QUEUING_TCQ:
				ddi_put32(portp->task_file2_handle,
				    (uint32_t *)(portp->task_file2_regs +
				    AT_DEVCTL), 0);
				tag = (uint8_t)ddi_get32(
				    portp->task_file1_handle,
				    (uint32_t *)(portp->task_file1_regs +
				    AT_ERROR));
				tag >>= SATA_TAG_QUEUING_SHIFT;
				tag &= SATA_TAG_QUEUING_MASK;
				spkt = portp->mv_dma_pkts[tag];
				break;

			case MV_QUEUING_NONE:
				tag = ddi_ffs(portp->dma_in_use_map);
				spkt = portp->mv_dma_pkts[--tag];
				break;
			}

			MV_DEBUG_MSG(portp->mv_ctlp->mv_dip, MV_DBG_DEV_INTR,
			    ("mv_port_error_intr: tag = %d spkt = 0x%p\n"),
			    tag, (void *)spkt);

			if ((spkt != NULL) && (tag != -1)) {
				scmd = &spkt->satapkt_cmd;
				/* copy out block address & sector count */
				if (scmd->satacmd_addr_type == ATA_ADDR_LBA48) {
					fp = &scmd->satacmd_flags;

					fp->sata_copy_out_sec_count_msb
					    = fp->sata_copy_out_lba_low_msb
					    = fp->sata_copy_out_lba_mid_msb
					    = fp->sata_copy_out_lba_high_msb
					    = B_TRUE;
				}
				scmd->satacmd_flags.sata_copy_out_sec_count_lsb
				    = fp->sata_copy_out_lba_low_lsb
				    = fp->sata_copy_out_lba_mid_lsb
				    = fp->sata_copy_out_lba_high_lsb
				    = fp->sata_copy_out_device_reg
				    = fp->sata_copy_out_error_reg
				    = B_TRUE;
				mv_copy_out_ata_regs(scmd, portp);
			}
		}
		mv_abort_all_pkts(portp, SATA_PKT_ABORTED);
	}

	if (error_cause & EDMA_INTR_ERROR_CAUSE_DEVICE_DISCONNECT) {
		mv_dev_reset(portp, B_TRUE, B_TRUE, "device disconnected");
		mv_abort_all_pkts(portp, SATA_PKT_RESET);
	} else if (error_cause & EDMA_INTR_ERROR_CAUSE_DEVICE_ERR) {
		switch (portp->mv_ctlp->mv_model) {
		case MV_MODEL_60XX:
			/*
			 * Reset is necessary here (even when there is
			 * a bad block since the sactive register does not
			 * get cleared by the hardware as it suppose to
			 * according to the SATA specification when the
			 * command is aborted.
			 */
			mv_dev_reset(portp, B_TRUE, B_TRUE, "device error");
			mv_abort_all_pkts(portp, SATA_PKT_ABORTED);
			break;
		case MV_MODEL_50XX:
			/* This really shouldn't ever occur */
			mv_dev_reset(portp, B_TRUE, B_TRUE, "device error");
			mv_abort_all_pkts(portp, SATA_PKT_RESET);
			break;
		}
	} else if (error_cause & EDMA_INTR_ERROR_CAUSE_DEVICE_CONNECTED)
		mv_dev_reset(portp, B_FALSE, B_TRUE, "device connected");

	error_cause = ddi_get32(portp->edma_handle,
	    portp->edma_regs + EDMA_INTERRUPT_ERROR_CAUSE_OFFSET);

	if (error_cause & EDMA_INTR_ERROR_CAUSE_DEVICE_DISCONNECT)
		mv_port_disconnect(portp);
	if (error_cause & EDMA_INTR_ERROR_CAUSE_DEVICE_CONNECTED)
		mv_port_connect(portp);
	if (error_cause != 0) {
		if ((serror != 0) || (error_cause & edma_error_log_mask)) {
			MV_MSG((portp->mv_ctlp->mv_dip, CE_WARN,
			    "!marvell88sx%d: error on port %d:",
			    ddi_get_instance(portp->mv_ctlp->mv_dip),
			    portp->port_num));
		}

		bits = error_cause;

		ASSERT((portp->mv_ctlp->mv_model == MV_MODEL_60XX ||
		    portp->mv_ctlp->mv_model == MV_MODEL_50XX));

		switch (portp->mv_ctlp->mv_model) {
		case MV_MODEL_60XX:
			edma_errors = edma_errors_60xx;
			break;
		case MV_MODEL_50XX:
			edma_errors = edma_errors_50xx;
			/*
			 * Make sure we don't look at any bits
			 * we don't know about
			 */
			bits &= EDMA_INTR_ERROR_MASK_50XX;
			break;
		}

		/* Log the EDMA errors */
		for (i = 0; i < sizeof (uint32_t) * NBBY; i++) {
			/*
			 * If this bit is set in the error cause register and
			 * is supposed to be logged, log it appropriately.
			 */
			if ((1 << i) & bits & edma_error_log_mask)
				MV_MSG((portp->mv_ctlp->mv_dip,
				    CE_CONT, "!\t%s\n", edma_errors[i]));
		}
		/* Log any SError errors */
		if (serror != 0) {
			bits = serror;
			MV_MSG((portp->mv_ctlp->mv_dip, CE_CONT,
			    "!\tSErrors:\n"));
			for (i = 0; bits != 0; i++, bits >>= 1) {
				if (bits & 1) {
					MV_MSG((portp->mv_ctlp->mv_dip,
					    CE_CONT, "!\t\t%s\n",
					    serror_bits[i]));
				}
			}
		}
		/* Clear the interrupt error cause */
		ddi_put32(portp->edma_handle,
		    portp->edma_regs + EDMA_INTERRUPT_ERROR_CAUSE_OFFSET,
		    ~error_cause);

		/* If DMA was self disabled, re-enable DMA */
		switch (portp->mv_ctlp->mv_model) {
		case MV_MODEL_60XX:
			if (error_cause &
			    EDMA_INTR_ERROR_CAUSE_SELF_DISABLE_60XX) {
				mv_enable_dma(portp);
			}
			break;
		case MV_MODEL_50XX:
			if (error_cause &
			    EDMA_INTR_ERROR_CAUSE_SELF_DISABLE_50XX) {
				mv_enable_dma(portp);
			}
			break;
		}
	}

	mutex_exit(&portp->mv_port_mutex);
}

/*
 * mv_subctrl_intr() is called from mv_intr() the main interrupt
 * routine to service an interrupt
 */

static void
mv_subctrl_intr(struct mv_subctlr *ctrlp, uint32_t main_intr)
{
	uint32_t ctrl_intr;
	uint32_t q_in_ptr, q_in_ptr_acked, q_limit;
	register int port;

	MV_DEBUG_MSG(NULL, MV_DBG_DEV_INTR,
	    ("mv_subctrl_intr(%p, 0x%x) entered\n"), (void *)ctrlp, main_intr);

	mutex_enter(&ctrlp->mv_subctrl_mutex);

	ctrl_intr = ddi_get32(ctrlp->mv_ctrl_handle,
	    ctrlp->mv_ctrl_regs + SATAHC_INTR_CAUSE_OFFSET);

	MV_DEBUG_MSG(NULL, MV_DBG_DEV_INTR,
	    ("mv_subctrl_intr: ctrl_intr = 0x%x\n"), ctrl_intr);

	do {
		/*
		 * Make sure we know where the queue fill pointer was when
		 * we acknowledged the interrupt.
		 */
		do {
			q_in_ptr_acked = ddi_get32(ctrlp->mv_ctrl_handle,
			    ctrlp->mv_ctrl_regs +
			    SATAHC_RESPONSE_Q_IN_PTR_OFFSET);

			/* Clear the interrupt bits for this subcontroller */
			ddi_put32(ctrlp->mv_ctrl_handle,
			    ctrlp->mv_ctrl_regs + SATAHC_INTR_CAUSE_OFFSET,
			    ~ctrl_intr);

			q_in_ptr = ddi_get32(ctrlp->mv_ctrl_handle,
			    ctrlp->mv_ctrl_regs +
			    SATAHC_RESPONSE_Q_IN_PTR_OFFSET);
		} while (q_in_ptr != q_in_ptr_acked);

		/* Now loop through all the ports on this host controller */
		for (port = 0; port < MV_NUM_PORTS_PER_SUBCTRLR; ++port) {

			/* Check for completed non-DMA operations */
			if (ctrl_intr &
			    ((1 << SATAHC_DEV_INTR_SHIFT) << port))
				mv_dev_intr(ctrlp->mv_port_state[port]);

			/* Check for completed DMA operations */
			if (ctrl_intr &
			    ((1 << SATAHC_CRPB_DONE_SHIFT) << port)) {
				q_limit = q_in_ptr >>
				    (port * SATAHC_RESP_Q_PORT_SHIFT);
				q_limit &= SATAHC_RESP_Q_PORT_MASK;
				mv_crpb_done_intr(ctrlp->mv_port_state[port],
				    q_limit);
			}

			/* Check for SATA port errors */
			if (main_intr &
			    (MAIN_SATAERR << (port * MAIN_PORT_SHIFT)))
				mv_port_err_intr(ctrlp->mv_port_state[port]);
		}

		/* Re-read the interrupt cause register */
		ctrl_intr = ddi_get32(ctrlp->mv_ctrl_handle,
		    ctrlp->mv_ctrl_regs + SATAHC_INTR_CAUSE_OFFSET);

		MV_DEBUG_MSG(NULL, MV_DBG_DEV_INTR, ("mv_subctrl_intr:"
		    " re-read ctrl_intr = 0x%x\n"), ctrl_intr);

	} while (ctrl_intr);


	MV_DEBUG_MSG(NULL, MV_DBG_DEV_INTR, ("mv_subctrl_intr: exiting\n"),
	    NULL);

	mutex_exit(&ctrlp->mv_subctrl_mutex);
}


/*
 * mv_intr() is the chip main interrupt routine
 */

/* ARGSUSED1 */
static uint_t
mv_intr(caddr_t arg1, caddr_t arg2)
{
	mv_ctl_t *mv_ctlp = (mv_ctl_t *)arg1;
	uint32_t intr_cause;
	int loopcnt = 0;

	MV_DEBUG_MSG(mv_ctlp->mv_dip, MV_DBG_DEV_INTR,
	    ("mv_intr(%p) entered\n"), (void *)arg1);

	mutex_enter(&mv_ctlp->mv_ctl_mutex);

	/*
	 * If power level is not on, then cannot access any
	 * non-config space registers.
	 * might be better to check some config space to
	 * see if there is any intr pending.  consider
	 * the case where pm-components is not declared so
	 * should unknown be ok?  or just check for pmflag first?
	 */
	if (mv_ctlp->mv_power_level == PM_LEVEL_D3) {
		if (pci_config_setup(mv_ctlp->mv_dip,
		    &mv_ctlp->mv_config_handle) != DDI_SUCCESS) {
			/*
			 * No need to run these instructions if
			 * mv_debug_flags is not set.
			 */
			if (mv_debug_flags & MV_DBG_GEN) {
				int instance;
				instance = ddi_get_instance(mv_ctlp->mv_dip);

				MV_DEBUG_MSG(mv_ctlp->mv_dip, MV_DBG_GEN,
				    ("marvell88sx%d: pci_config_setup "
				    "failed\n"), instance);
			}
			mutex_exit(&mv_ctlp->mv_ctl_mutex);
			return (DDI_INTR_UNCLAIMED);
		}

		/*
		 * No need to run these instructions if
		 * mv_debug_flags is not set.
		 */
		if (mv_debug_flags & MV_DBG_GEN) {
			uint32_t lvl;
			lvl = pci_config_get16(mv_ctlp->mv_config_handle,
			    MV_PM_CSR);

			MV_DEBUG_MSG(mv_ctlp->mv_dip, MV_DBG_GEN,
			    ("mv_intr: power level is %d,CSR=%X\n"),
			    mv_ctlp->mv_power_level, lvl);
		}

		pci_config_teardown(&mv_ctlp->mv_config_handle);
		mutex_exit(&mv_ctlp->mv_ctl_mutex);

		return (DDI_INTR_UNCLAIMED);
	}

	intr_cause = ddi_get32(mv_ctlp->mv_device_main_handle,
	    mv_ctlp->mv_device_main_regs + DEVICE_MAIN_INTR_CAUSE_OFFSET);


	if (intr_cause == 0) {
		mutex_exit(&mv_ctlp->mv_ctl_mutex);

		MV_DEBUG_MSG(mv_ctlp->mv_dip, MV_DBG_DEV_INTR,
		    ("mv_intr: returning unclaimed\n"), NULL);

		return (DDI_INTR_UNCLAIMED);
	}

	MV_DEBUG_MSG(mv_ctlp->mv_dip, MV_DBG_DEV_INTR,
	    ("mv_intr: claiming - cause 0x%x\n"), intr_cause);


	/* Service our interrupt(s) */
	do {
		/* Subcontroller 0 */
		if (intr_cause & DEVICE_MAIN_INTR_CAUSE_SATA03_INTR) {
			mv_subctrl_intr(mv_ctlp->mv_subctl[MV_FIRST_SUBCTRLR],
			    intr_cause &
			    DEVICE_MAIN_INTR_CAUSE_SATA03_INTR);
		}

		/* Subcontroller 1 */
		if (intr_cause & DEVICE_MAIN_INTR_CAUSE_SATA47_INTR) {
			mv_subctrl_intr(mv_ctlp->mv_subctl[MV_SECOND_SUBCTRLR],
			    (intr_cause &
			    DEVICE_MAIN_INTR_CAUSE_SATA47_INTR) >>
			    MAIN_SATAHC_BASE_SHIFT);
		}

		if (intr_cause & DEVICE_MAIN_INTR_CAUSE_PCI_ERR)
			mv_pci_err(mv_ctlp);

		if (mv_ctlp->mv_model == MV_MODEL_60XX) {
			if (intr_cause & DEVICE_MAIN_INTR_CAUSE_SATA07COALSDONE)
				mv_coals_intr(mv_ctlp);
			if (intr_cause & DEVICE_MAIN_INTR_CAUSE_TRAN_LOW_DONE)
				mv_sata_io_trans_intr(mv_ctlp, trans_low);
			if (intr_cause & DEVICE_MAIN_INTR_CAUSE_TRAN_HIGH_DONE)
				mv_sata_io_trans_intr(mv_ctlp, trans_high);
		}

		intr_cause = ddi_get32(mv_ctlp->mv_device_main_handle,
		    mv_ctlp->mv_device_main_regs +
		    DEVICE_MAIN_INTR_CAUSE_OFFSET);

		MV_DEBUG_MSG(mv_ctlp->mv_dip, MV_DBG_DEV_INTR,
		    ("mv_intr: after re-read 0x%x\n"), intr_cause);

		/*
		 * On rare occasions, the main interrupt register
		 * sticks indicating a subcontroller interrupt when
		 * there is no such interrupt.  Because new
		 * commands are not put on port queues in this loop,
		 * all commands should either be completed or aborted
		 * in a known number of loop iterations.  Rather than
		 * checking for a stuck interrupt on each iteration, it
		 * is sufficient to check the iteration count to
		 * determine if the interrupt is stuck.
		 */
		if (loopcnt++ >=
		    ((MV_MAX_NUM_SUBCTRLR * MV_NUM_PORTS_PER_SUBCTRLR *
		    MV_QDEPTH) + 1)) {
			mv_stuck_intr(mv_ctlp, intr_cause);
			break;
		}

	} while (intr_cause != 0);
	mutex_exit(&mv_ctlp->mv_ctl_mutex);

	MV_DEBUG_MSG(mv_ctlp->mv_dip, MV_DBG_DEV_INTR,
	    ("mv_intr: returning claimed\n"), NULL);

	return (DDI_INTR_CLAIMED);
}

/*
 * mv_num_ports() returns number of ports per chip
 */

static int
mv_num_ports(dev_info_t *dip)
{
	int instance;
	int deviceid;
	int nports;

	instance = ddi_get_instance(dip);

	deviceid = ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "device-id", 0);

	if (deviceid == 0) {
		cmn_err(CE_WARN,
		    "marvell88sx%d: failed to get device id\n", instance);
		return (0);
	}

	switch (deviceid) {
	case MV_SATA_88SX5040:
	case MV_SATA_88SX5041:
	case MV_SATA_88SX6040:
	case MV_SATA_88SX6041:
		return (4);
	case MV_SATA_88SX5080:
	case MV_SATA_88SX5081:
	case MV_SATA_88SX6080:
	case MV_SATA_88SX6081:
		return (8);
	default:
		nports = (deviceid & N_PORT_MASK) >> N_PORT_SHIFT;
		cmn_err(CE_WARN,
		    "marvell88sx%d: Unrecognized device"
		    " - device id 0x%x\n"
		    "assuming %d ports\n", instance, deviceid, nports);
		return (nports);
	}
}

/*
 * mv_get_model() returns the model of the chip
 */

static
enum mv_models
mv_get_model(dev_info_t *dip)
{
	int instance;
	int deviceid;

	instance = ddi_get_instance(dip);

	deviceid = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, "device-id", 0);

	if (deviceid == 0) {
		cmn_err(CE_WARN,
		    "marvell88sx%d: failed to get device id\n",
		    instance);
		return (MV_MODEL_UNKNOWN);
	}

	switch (deviceid) {
	case MV_SATA_88SX5040:
	case MV_SATA_88SX5041:
	case MV_SATA_88SX5080:
	case MV_SATA_88SX5081:
		return (MV_MODEL_50XX);
	case MV_SATA_88SX6040:
	case MV_SATA_88SX6041:
	case MV_SATA_88SX6080:
	case MV_SATA_88SX6081:
		return (MV_MODEL_60XX);
	default:
		cmn_err(CE_WARN,
		    "marvell88sx%d: Unrecognized device"
		    " - device id 0x%x\n", instance, deviceid);
		return (MV_MODEL_UNKNOWN);
	}
}

/*
 * mv_attach() attaches the device to the system, or resume it
 */
static int
mv_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	int subctrl;
	mv_ctl_t *mv_ctlp = NULL;
	sata_hba_tran_t *sata_hba_tran = NULL;
	int instance = ddi_get_instance(dip);
	int status;
	int i;
	int ctrlr;
	int port;
	int subctlr;
	uint32_t qentry;
	sata_device_t sdevice;
	int intr_types;

	switch (cmd) {
	case DDI_RESUME:
		mv_ctlp = ddi_get_soft_state(mv_state, instance);

		mutex_enter(&mv_ctlp->mv_ctl_mutex);

		mv_init_ctrl_regs(mv_ctlp);
		mv_ctlp->mv_timeout_id = timeout(mv_pkt_timeout,
		    (void *)mv_ctlp,
		    drv_usectohz(MV_MICROSECONDS_PER_SD_PKT_TICK));
		(void) pm_power_has_changed(dip, MV_PM_COMPONENT_0,
		    PM_LEVEL_D0);

		mutex_exit(&mv_ctlp->mv_ctl_mutex);

		/* Notify SATA framework about RESUME */
		if (sata_hba_attach(dip, mv_ctlp->mv_sata_hba_tran,
		    DDI_RESUME) != DDI_SUCCESS) {
			return (DDI_FAILURE);
		}
		mv_ctlp->mv_sataframework_flags |= MV_FRAMEWORK_ATTACHED;

		/*
		 * When implemented in the SATA module, this will notify
		 * the "framework" that it should reprobe ports to see if
		 * any device changed while suspended.
		 *
		 * The power management is not supported by the SATA
		 * framework in phase I.  This need to be revisited
		 * to decide what needs to be passed to SATA framework
		 */
		sdevice.satadev_rev = SATA_DEVICE_REV;
		sdevice.satadev_addr.qual = SATA_ADDR_CNTRL;
		sata_hba_event_notify(dip, &sdevice,
		    SATA_EVNT_PWR_LEVEL_CHANGED);
		(void) pm_idle_component(mv_ctlp->mv_dip,
		    MV_PM_COMPONENT_0);

		return (DDI_SUCCESS);

	case DDI_ATTACH:
		break;

	default:
		return (DDI_FAILURE);
	}

	/* Allocate instance softstate */
	status = ddi_soft_state_zalloc(mv_state, instance);
	if (status != DDI_SUCCESS) {
		cmn_err(CE_WARN,
		    "!marvell88sx%d: Could not attach, Could not "
		    "allocate soft state\n", instance);

		return (status);
	}

	mv_ctlp = ddi_get_soft_state(mv_state, instance);

	mv_ctlp->mv_flags |= MV_ATTACH;
	mv_ctlp->mv_dip = dip;

	if ((mv_ctlp->mv_model = mv_get_model(dip)) == MV_MODEL_UNKNOWN) {
		/* Free this instance soft state */
		ddi_soft_state_free(mv_state, instance);
		cmn_err(CE_WARN,
		    "marvell88sx%d: Could not attach, unknown device model\n",
		    instance);

		return (DDI_FAILURE);
	}
	if (mv_stepping(dip, mv_ctlp) == B_FALSE) {
		/* Free this instance soft state */
		ddi_soft_state_free(mv_state, instance);
		cmn_err(CE_WARN,
		    "marvell88sx%d: Could not attach, unsupported chip "
		    "stepping or unable to get the chip stepping\n",
		    instance);

		return (DDI_FAILURE);
	}
	/* Set the total number of SATA ports */
	mv_ctlp->mv_num_ports = mv_num_ports(dip);

	ASSERT(mv_ctlp->mv_num_ports > 0);
	ASSERT((mv_ctlp->mv_num_ports % MV_NUM_PORTS_PER_SUBCTRLR) == 0);

	/* And the number of sub controllers */
	mv_ctlp->mv_num_subctrls = mv_ctlp->mv_num_ports /
	    MV_NUM_PORTS_PER_SUBCTRLR;

	/*
	 * Allocate space for the sub controllers and ports
	 */
	for (ctrlr = 0; ctrlr < mv_ctlp->mv_num_subctrls; ctrlr++) {
		mv_ctlp->mv_subctl[ctrlr] =
		    kmem_zalloc(sizeof (struct mv_subctlr), KM_SLEEP);
		for (port = 0; port < MV_NUM_PORTS_PER_SUBCTRLR; port++) {
			struct mv_port_state *portp;

			portp = kmem_zalloc(sizeof (struct mv_port_state),
			    KM_SLEEP);
			mv_ctlp->mv_subctl[ctrlr]->mv_port_state[port] =
			    portp;
			portp->port_num = port +
			    ctrlr * MV_NUM_PORTS_PER_SUBCTRLR;
		}
	}

	/*
	 * Store handles and addresses in soft state.
	 */
	if (! mv_map_registers(mv_ctlp)) {
		cmn_err(CE_WARN, "marvell88sx%d: Could not attach\n", instance);
		goto fail4;
	}

	if (ddi_add_softintr(dip, DDI_SOFTINT_MED, &mv_ctlp->mv_softintr_id,
	    NULL, NULL, mv_do_completed_pkts, (caddr_t)mv_ctlp)
	    != DDI_SUCCESS) {
		cmn_err(CE_WARN, "marvell88sx%d: ddi_add_softintr() "
		    "failed\n", instance);
		cmn_err(CE_WARN, "marvell88sx%d: Could not attach\n", instance);
		goto fail3a;
	}


	/* Get supported interrupt types */
	if (ddi_intr_get_supported_types(dip, &intr_types) != DDI_SUCCESS) {
		cmn_err(CE_WARN, "marvell88sx%d: ddi_intr_get_supported_types"
		    "failed\n", instance);
		cmn_err(CE_WARN, "marvell88sx%d: Could not attach\n", instance);
		goto fail3;
	}


	MV_DEBUG_MSG(mv_ctlp->mv_dip, MV_DBG_DEV_INTR, ("marvell88sx%d: "
	    "ddi_intr_get_supported_types() returned: 0x%x\n"),
	    instance, intr_types);

	if (intr_types & DDI_INTR_TYPE_MSI) {

		MV_DEBUG_MSG(mv_ctlp->mv_dip, MV_DBG_DEV_INTR,
		    ("marvell88sx%d: Using MSI interrupt type\n"), instance);

		/*
		 * Try MSI first, but fall back to legacy if MSI attach fails
		 */
		if (mv_add_msi_intrs(mv_ctlp) == DDI_SUCCESS) {
			mv_ctlp->mv_intr_type = DDI_INTR_TYPE_MSI;
			goto intr_done;
		}

		MV_DEBUG_MSG(mv_ctlp->mv_dip, MV_DBG_DEV_INTR,
		    ("marvell88sx%d: MSI registration failed, trying "
		    "legacy interrupts\n"), instance);

	}

	if (intr_types & DDI_INTR_TYPE_FIXED) {

		MV_DEBUG_MSG(mv_ctlp->mv_dip, MV_DBG_DEV_INTR,
		    ("marvell88sx%d: Using Legacy interrupt type\n"
		    "trying legacy interrupts\n"), instance);

		if (mv_add_legacy_intrs(mv_ctlp) == DDI_SUCCESS) {
			mv_ctlp->mv_intr_type = DDI_INTR_TYPE_FIXED;
			goto intr_done;
		}

		MV_DEBUG_MSG(mv_ctlp->mv_dip, MV_DBG_DEV_INTR,
		    ("marvell88sx%d: Legacy interrupt registration failed\n"),
		    instance);

	}
	cmn_err(CE_WARN, "marvell88sx%d:  Could not attach, failed "
	    "interrupt registration\n", instance);

	goto fail3;



intr_done:

	/*
	 * mutex needed early for mv_power(), which is called
	 * by pm_raise_power().
	 */
	mutex_init(&mv_ctlp->mv_ctl_mutex, NULL, MUTEX_DRIVER,
	    (void *)(uintptr_t)mv_ctlp->mv_intr_pri);

	mv_pm_setup(dip, mv_ctlp);

	/*
	 * Initialize controller soft state.  Programming the
	 * registers is done next.  Do not bother with any device
	 * probing or initialization.
	 */

	/* Fix up buffer_dma_attr scatter/gather list length if non-standard */
	if (mv_dma_nsegs < MV_MIN_DMA_NSEGS) {
		cmn_err(CE_WARN, "marvell88sx%d: mv_dma_nsegs too small %d - "
		    "adjusted to %d\n",
		    instance, mv_dma_nsegs, MV_MIN_DMA_NSEGS);
		mv_dma_nsegs = MV_MIN_DMA_NSEGS;
	} else if (mv_dma_nsegs > MV_MAX_DMA_NSEGS) {
		cmn_err(CE_WARN, "marvell88sx%d: mv_dma_nsegs too large %d - "
		    "adjusted to %d\n",
		    instance, mv_dma_nsegs, MV_MAX_DMA_NSEGS);
		mv_dma_nsegs = MV_MAX_DMA_NSEGS;
	}
	if (mv_dma_nsegs != SATA_DMA_NSEGS)
		buffer_dma_attr.dma_attr_sgllen = mv_dma_nsegs;

	if (! mv_init_ctrl(mv_ctlp)) {
		cmn_err(CE_WARN, "marvell88sx%d: Could not attach\n", instance);
		goto fail2;
	}

	/*
	 * Now program the hardware registers
	 */
	mv_init_ctrl_regs(mv_ctlp);

	if (mv_ctlp->mv_intr_cap & DDI_INTR_FLAG_BLOCK) {

		MV_DEBUG_MSG(mv_ctlp->mv_dip, MV_DBG_DEV_INTR,
		    ("marvell88sx%d: about to call ddi_intr_block_enable\n"),
		    instance);

		/* Call ddi_intr_block_enable() for MSI */
		(void) ddi_intr_block_enable(mv_ctlp->mv_htable,
		    mv_ctlp->mv_intr_cnt);
	} else {
		/*
		 * Call ddi_intr_enable() for MSI non block
		 * or legacy interrupts
		 */
		for (i = 0; i < mv_ctlp->mv_intr_cnt; i++) {

			MV_DEBUG_MSG(mv_ctlp->mv_dip, MV_DBG_DEV_INTR,
			    ("marvell88sx%d: about to call ddi_intr_enable "
			    "- interrupt %d\n"), instance, i);

			(void) ddi_intr_enable(mv_ctlp->mv_htable[i]);
		}
	}

	/*
	 * Now unmask all interrupts
	 * First PCI error interrupts, then main device interrupts.
	 */
	ddi_put32(mv_ctlp->mv_pci_intr_err_handle,
	    mv_ctlp->mv_pci_intr_err_regs + PCI_INTERRUPT_MASK_OFFSET,
	    PCI_INTERRUPT_ENABLED);

	ddi_put32(mv_ctlp->mv_device_main_handle,
	    mv_ctlp->mv_device_main_regs + DEVICE_MAIN_INTR_MASK_OFFSET, ~0);

	/* Set up the sata_hba_tran  */
	sata_hba_tran = kmem_zalloc(sizeof (sata_hba_tran_t), KM_SLEEP);

	sata_hba_tran->sata_tran_hba_rev = SATA_TRAN_HBA_REV;
	sata_hba_tran->sata_tran_hba_dip = dip;
	sata_hba_tran->sata_tran_hba_dma_attr = &buffer_dma_attr;
	sata_hba_tran->sata_tran_hba_num_cports = mv_ctlp->mv_num_ports;
	/*
	 * SATA_CTLF_HOTPLUG flag should be set when controller is capable
	 * of detecting physical presence of a device regardless of the
	 * LINK status
	 * 88sx50XX is not capable of this, 88sx60XX can do it.
	 */
	sata_hba_tran->sata_tran_hba_features_support =
	    SATA_CTLF_ASN | SATA_CTLF_QCMD;

	if (mv_ctlp->mv_model == MV_MODEL_60XX) {
		sata_hba_tran->sata_tran_hba_features_support |= SATA_CTLF_NCQ;
#if defined(ADVANCED_HOT_PLUG)
		sata_hba_tran->sata_tran_hba_features_support |=
		    SATA_CTLF_HOTPLUG;
#endif
	}
	sata_hba_tran->sata_tran_hba_qdepth = MV_QDEPTH;
	sata_hba_tran->sata_tran_probe_port = mv_probe_port;
	sata_hba_tran->sata_tran_start = mv_start;
	sata_hba_tran->sata_tran_abort = mv_abort;
	sata_hba_tran->sata_tran_reset_dport = mv_reset_port;
	sata_hba_tran->sata_tran_hotplug_ops = &mv_hotplug_ops;
#if defined(__lock_lint)
	sata_hba_tran->sata_tran_selftest = mv_selftest;
#endif
	/*
	 * When SATA framework adds support for pwrmgt the
	 * pwrmgt_ops needs to be updated
	 */
	sata_hba_tran->sata_tran_pwrmgt_ops = NULL;
	sata_hba_tran->sata_tran_ioctl = NULL;

	mv_ctlp->mv_sata_hba_tran = sata_hba_tran;

	mutex_enter(&mv_ctlp->mv_ctl_mutex);

	/* Set up for checking packet timeouts once a second */
	mv_ctlp->mv_timeout_id = timeout(mv_pkt_timeout,
	    (void *) mv_ctlp, drv_usectohz(MV_MICROSECONDS_PER_SD_PKT_TICK));

	mutex_exit(&mv_ctlp->mv_ctl_mutex);

	/*
	 * Attach to SATA framework
	 * Controller has to be fully operational at this point,
	 * because SATA framework will try to probe ports,
	 * enumerate and initialize devices.
	 */
	if (sata_hba_attach(dip, sata_hba_tran, DDI_ATTACH) !=
	    DDI_SUCCESS) {
		MV_DEBUG_MSG(mv_ctlp->mv_dip, MV_DBG_GEN, ("marvell88sx%d: "
		    "unable to attach to sata framework\n"), instance);
		cmn_err(CE_WARN, "marvell88sx%d: Could not attach\n", instance);
		goto fail1;
	}
	mv_ctlp->mv_sataframework_flags |= MV_FRAMEWORK_ATTACHED;

	mutex_enter(&mv_ctlp->mv_ctl_mutex);

	mv_ctlp->mv_flags &= ~MV_ATTACH;

	mutex_exit(&mv_ctlp->mv_ctl_mutex);

	return (DDI_SUCCESS);


fail1:
	/*
	 * Disable Device Main Interrupt Cause register (Mask out interrupts)
	 */
	ddi_put32(mv_ctlp->mv_device_main_handle,
	    mv_ctlp->mv_device_main_regs + DEVICE_MAIN_INTR_MASK_OFFSET, 0);

	/*
	 * Disable PCI interrupts (Mask out interrupts)
	 */
	ddi_put32(mv_ctlp->mv_pci_intr_err_handle,
	    mv_ctlp->mv_pci_intr_err_regs + PCI_INTERRUPT_MASK_OFFSET,
	    PCI_INTERRUPT_DISABLED);

	/*
	 * Free sata_hba_tran structure
	 */
	if (mv_ctlp->mv_sata_hba_tran)
		kmem_free((void *)sata_hba_tran, sizeof (sata_hba_tran_t));

	/*
	 * Free edma resources
	 */
	for (subctlr = 0; subctlr < mv_ctlp->mv_num_subctrls; ++subctlr) {
		for (port = 0; port < MV_NUM_PORTS_PER_SUBCTRLR; ++port) {
			for (qentry = 0; qentry < MV_QDEPTH; ++qentry) {
				free_edma_resources(
				    mv_ctlp->mv_subctl[subctlr]->
				    mv_port_state[port],
				    qentry);

			}

		}
	}

	/*
	 * destroy per controller and port mutex
	 */
	for (subctlr = 0; subctlr < mv_ctlp->mv_num_subctrls;
	    ++subctlr) {
		mutex_destroy(&mv_ctlp->mv_subctl[subctlr]->
		    mv_subctrl_mutex);
		for (port = 0;
		    port < MV_NUM_PORTS_PER_SUBCTRLR;
		    ++port) {
			mutex_destroy(&mv_ctlp->mv_subctl[subctlr]->
			    mv_port_state[port]->mv_port_mutex);
		}
	}

fail2:
	mv_rem_intrs(mv_ctlp);

	/* destroy mutex */
	mutex_destroy(&mv_ctlp->mv_ctl_mutex);
fail3:
	ddi_remove_softintr(mv_ctlp->mv_softintr_id);
fail3a:
	/*
	 * unmap all the registers
	 */
	for (subctrl = 0; subctrl < mv_ctlp->mv_num_subctrls; ++subctrl) {
		for (port = 0; port < MV_NUM_PORTS_PER_SUBCTRLR; ++port)
			mv_unmap_port_registers(
			    mv_ctlp->mv_subctl[subctrl]->mv_port_state[port]);
		ddi_regs_map_free(
		    &mv_ctlp->mv_subctl[subctrl]->mv_ctrl_handle);
	}

	ddi_regs_map_free(&mv_ctlp->mv_device_main_handle);
	ddi_regs_map_free(&mv_ctlp->mv_pci_intr_err_handle);
fail4:

	/*
	 * Free controller and port structures
	 */
	for (ctrlr = 0; ctrlr < mv_ctlp->mv_num_subctrls; ++ctrlr) {
		for (port = 0; port < MV_NUM_PORTS_PER_SUBCTRLR; ++port) {
			kmem_free(
			    mv_ctlp->mv_subctl[ctrlr]->mv_port_state[port],
			    (sizeof (struct mv_port_state)));
		}
		kmem_free(mv_ctlp->mv_subctl[ctrlr],
		    sizeof (struct mv_subctlr));
	}
	/* Free this instance soft state */
	ddi_soft_state_free(mv_state, instance);

	return (DDI_FAILURE);
}

/*
 * mv_detach() detachs instance of HBA
 */
static int
mv_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	mv_ctl_t		*mv_ctlp;
	int			instance;
	int 			subctlr;
	int			port;
	uint32_t 		qentry;
	struct mv_port_state	*portp;
	sata_pkt_t		*pkt;

	instance = ddi_get_instance(dip);
	mv_ctlp = ddi_get_soft_state(mv_state, instance);


	switch (cmd) {
	case DDI_DETACH:
		mutex_enter(&mv_ctlp->mv_ctl_mutex);

		/*
		 * Freeze the Q and wait for everything to flush
		 */
		mv_ctlp->mv_flags |= MV_DETACH | MV_NO_TIMEOUTS;

		for (subctlr = 0; subctlr < mv_ctlp->mv_num_subctrls;
		    ++subctlr) {
			for (port = 0;
			    port < MV_NUM_PORTS_PER_SUBCTRLR;
			    ++port) {
				portp = mv_ctlp->mv_subctl[subctlr]->
				    mv_port_state[port];
				mutex_exit(&mv_ctlp->mv_ctl_mutex);
				mutex_enter(&portp->mv_port_mutex);
				pkt = portp->mv_pio_pkt;
				if (pkt) {
					while (pkt->satapkt_reason
					    == SATA_PKT_BUSY) {
						cv_wait(&portp->mv_port_cv,
						    &portp->mv_port_mutex);
					}
				} else while (portp->num_dmas != 0) {
					cv_wait(&portp->mv_empty_cv,
					    &portp->mv_port_mutex);
				}
				mutex_exit(&portp->mv_port_mutex);
				mutex_enter(&mv_ctlp->mv_ctl_mutex);
			}
		}

		(void) untimeout(mv_ctlp->mv_timeout_id);


		/*
		 * Mask out main interrupts.
		 */
		ddi_put32(mv_ctlp->mv_device_main_handle,
		    mv_ctlp->mv_device_main_regs
		    + DEVICE_MAIN_INTR_MASK_OFFSET,
		    0);

		/*
		 * Mask out PCI interrupts.
		 */
		ddi_put32(mv_ctlp->mv_pci_intr_err_handle,
		    mv_ctlp->mv_pci_intr_err_regs + PCI_INTERRUPT_MASK_OFFSET,
		    PCI_INTERRUPT_DISABLED);


		mutex_exit(&mv_ctlp->mv_ctl_mutex);

		/* Unattach SATA framework */
		if (sata_hba_detach(dip, cmd) != DDI_SUCCESS) {
			cmn_err(CE_WARN,
			    "marvell88sx%d: unable to detach from "
			    "sata framework\n", instance);
			return (DDI_FAILURE);
		}
		mv_ctlp->mv_sataframework_flags &= ~MV_FRAMEWORK_ATTACHED;

		mutex_enter(&mv_ctlp->mv_ctl_mutex);

		mv_ctlp->mv_flags &= ~MV_ATTACH;

		mv_rem_intrs(mv_ctlp);

		ddi_remove_softintr(mv_ctlp->mv_softintr_id);
		/*
		 * Free edma resources
		 */
		for (subctlr = 0; subctlr < mv_ctlp->mv_num_subctrls;
		    ++subctlr) {
			for (port = 0;
			    port < MV_NUM_PORTS_PER_SUBCTRLR;
			    ++port) {
				for (qentry = 0; qentry < MV_QDEPTH; ++qentry) {
					free_edma_resources(
					    mv_ctlp->mv_subctl[subctlr]->
					    mv_port_state[port],
					    qentry);

				}

			}
		}

		/*
		 * Unmap registers
		 */
		for (subctlr = 0; subctlr < mv_ctlp->mv_num_subctrls;
		    ++subctlr) {
			for (port = 0;
			    port < MV_NUM_PORTS_PER_SUBCTRLR;
			    ++port) {
				mv_unmap_port_registers(
				    mv_ctlp->mv_subctl[subctlr]->
				    mv_port_state[port]);

			}
			ddi_regs_map_free(&mv_ctlp->mv_subctl[subctlr]->
			    mv_ctrl_handle);
		}

		ddi_regs_map_free(&mv_ctlp->mv_device_main_handle);
		ddi_regs_map_free(&mv_ctlp->mv_pci_intr_err_handle);
		if (mv_ctlp->mv_model == MV_MODEL_60XX)
			ddi_regs_map_free(&mv_ctlp->mv_intr_coal_ext_handle);

		/*
		 * destroy per controller and port mutex
		 */
		for (subctlr = 0; subctlr < mv_ctlp->mv_num_subctrls;
		    ++subctlr) {
			mutex_destroy(&mv_ctlp->mv_subctl[subctlr]->
			    mv_subctrl_mutex);
			for (port = 0;
			    port < MV_NUM_PORTS_PER_SUBCTRLR;
			    ++port) {
				mutex_destroy(&mv_ctlp->mv_subctl[subctlr]->
				    mv_port_state[port]->mv_port_mutex);
			}
		}

		/*
		 * Free controller and port structures
		 */
		for (subctlr = 0; subctlr < mv_ctlp->mv_num_subctrls;
		    ++subctlr) {
			for (port = 0; port < MV_NUM_PORTS_PER_SUBCTRLR;
			    ++port) {
				kmem_free(mv_ctlp->mv_subctl[subctlr]->
				    mv_port_state[port],
				    (sizeof (struct mv_port_state)));
			}
			kmem_free(mv_ctlp->mv_subctl[subctlr],
			    sizeof (struct mv_subctlr));
		}


		/* Deallocate sata_hba_tran */
		kmem_free((void *)mv_ctlp->mv_sata_hba_tran,
		    sizeof (sata_hba_tran_t));

		mutex_exit(&mv_ctlp->mv_ctl_mutex);

		(void) pm_lower_power(mv_ctlp->mv_dip, MV_PM_COMPONENT_0,
		    PM_LEVEL_D3);

		mutex_destroy(&mv_ctlp->mv_ctl_mutex);

		/* Free the soft state */
		ddi_soft_state_free(mv_state, instance);

		return (DDI_SUCCESS);

	case DDI_SUSPEND:
		/* Inform SATA framework */
		if (sata_hba_detach(dip, cmd) != DDI_SUCCESS) {
			cmn_err(CE_WARN,
			    "marvell88sx%d: unable to detach from"
			    "sata framework\n", instance);
			return (DDI_FAILURE);
		}
		mv_ctlp->mv_sataframework_flags &= ~MV_FRAMEWORK_ATTACHED;

		mutex_enter(&mv_ctlp->mv_ctl_mutex);

		/*
		 * Device needs to be at full power in case it is needed to
		 * handle dump(9e) to save CPR state after DDI_SUSPEND
		 * completes.  This is OK since presumably power will be
		 * removed anyways.  No outstanding transactions should be
		 * on the controller since the children are already quiesed.
		 *
		 * If any ioctls/cfgadm support is added that touches
		 * hardware, those entry points will need to check for
		 * suspend and then block or return errors until resume.
		 *
		 */
		if (pm_busy_component(mv_ctlp->mv_dip,
		    MV_PM_COMPONENT_0) == DDI_SUCCESS) {
			mutex_exit(&mv_ctlp->mv_ctl_mutex);
			(void) pm_raise_power(mv_ctlp->mv_dip,
			    MV_PM_COMPONENT_0, PM_LEVEL_D0);
			mutex_enter(&mv_ctlp->mv_ctl_mutex);
		}

		mv_ctlp->mv_flags |= MV_NO_TIMEOUTS;
		(void) untimeout(mv_ctlp->mv_timeout_id);
		mv_ctlp->mv_flags &= ~MV_NO_TIMEOUTS;

		mutex_exit(&mv_ctlp->mv_ctl_mutex);

		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);
	}
}


static int
mv_quiesce(dev_info_t *dip)
{
	mv_ctl_t		*mv_ctlp;
	int			instance, subctrlr;

	instance = ddi_get_instance(dip);
	mv_ctlp = ddi_get_soft_state(mv_state, instance);

	if (mv_ctlp == NULL)
		return (DDI_SUCCESS);

	/*
	 * Turn off debug printing
	 */
	mv_debug_logging = B_FALSE;
	mv_verbose = B_FALSE;

	/*
	 * Mask out main interrupts.
	 */
	ddi_put32(mv_ctlp->mv_device_main_handle,
	    mv_ctlp->mv_device_main_regs + DEVICE_MAIN_INTR_MASK_OFFSET, 0);

	/*
	 * Mask out PCI interrupts.
	 */
	ddi_put32(mv_ctlp->mv_pci_intr_err_handle,
	    mv_ctlp->mv_pci_intr_err_regs + PCI_INTERRUPT_MASK_OFFSET,
	    PCI_INTERRUPT_DISABLED);

	for (subctrlr = 0; subctrlr < mv_ctlp->mv_num_subctrls; subctrlr++) {
		int port;

		for (port = 0; port < MV_NUM_PORTS_PER_SUBCTRLR; port++) {
			struct mv_port_state *portp =
			    mv_ctlp->mv_subctl[subctrlr]->mv_port_state[port];

			mv_disable_dma(portp);
			mv_dev_reset(portp, B_TRUE, B_TRUE, "fast reboot");
		}
	}

	return (DDI_SUCCESS);
}

/*
 * mv_add_msi_intrs() adding MSI interrupts
 */

static int
mv_add_msi_intrs(mv_ctl_t *mv_ctlp)
{
	dev_info_t	*devinfo = mv_ctlp->mv_dip;
	int		count, avail, actual;
	int		x, y, rc, inum = 0;
	int 		instance = ddi_get_instance(mv_ctlp->mv_dip);

	MV_DEBUG_MSG(mv_ctlp->mv_dip, MV_DBG_DEV_INTR, ("marvell88sx%d: "
	    "Adding MSI interrupts"), instance);


	/* get number of interrupts */
	rc = ddi_intr_get_nintrs(devinfo, DDI_INTR_TYPE_MSI, &count);
	if (rc != DDI_SUCCESS) {
		cmn_err(CE_WARN, "!marvell88sx%d: Can not get number "
		    "of interrupts, rc %d\n", instance, rc);

		return (DDI_FAILURE);
	}
	if (count == 0) {
		cmn_err(CE_WARN, "!marvell88sx%d: 0 is not a valid number "
		    "of interrupts\n", instance);

		return (DDI_FAILURE);
	}

	/* round up to a power of 2 */
	count = round_up_power_of_2(count);

	/* get number of available interrupts */
	rc = ddi_intr_get_navail(devinfo, DDI_INTR_TYPE_MSI, &avail);
	if (rc != DDI_SUCCESS) {
		cmn_err(CE_WARN, "!marvell88sx%d: Failed to get the "
		    "number of available interrupts, rc %d \n", instance, rc);

		return (DDI_FAILURE);
	}
	if (count == 0) {
		cmn_err(CE_WARN, "!marvell88sx%d: Number of available "
		    "interrupts is 0\n", instance);

		return (DDI_FAILURE);
	}

	if (avail < count) {
		cmn_err(CE_NOTE, "marvell88sx%d: "
		    "ddi_intr_get_nvail returned %d,"
		    "navail() returned %d\n", instance, count, avail);
	}

	/* Allocate an array of interrupt handles */
	mv_ctlp->mv_intr_size = count * sizeof (ddi_intr_handle_t);
	mv_ctlp->mv_htable = kmem_alloc(mv_ctlp->mv_intr_size, KM_SLEEP);

	/* call ddi_intr_alloc() */
	rc = ddi_intr_alloc(devinfo, mv_ctlp->mv_htable, DDI_INTR_TYPE_MSI,
	    inum, count, &actual, DDI_INTR_ALLOC_NORMAL);

	if ((rc != DDI_SUCCESS) || (actual == 0)) {
		cmn_err(CE_WARN, "!marvell88sx%d: could not allocate "
		    "interrupts, rc %d actual %d\n", instance, rc, actual);

		kmem_free(mv_ctlp->mv_htable, mv_ctlp->mv_intr_size);
		return (DDI_FAILURE);
	}

	/* use interrupt count returned or abort? */
	if (actual < count) {
		/* Return? Abort? What? */
		cmn_err(CE_NOTE, "marvell88sx%d: "
		    "Requested: %d, Received: %d",
		    instance, count, actual);
	}

	mv_ctlp->mv_intr_cnt = actual;

	/*
	 * Get priority for first msi, assume remaining are all the same
	 */
	if (ddi_intr_get_pri(mv_ctlp->mv_htable[0], &mv_ctlp->mv_intr_pri) !=
	    DDI_SUCCESS) {
		cmn_err(CE_WARN, "marvell88sx%d: could not get interrupt "
		    "priority\n ", instance);

		/* Free already allocated intr */
		for (y = 0; y < actual; y++) {
			(void) ddi_intr_free(mv_ctlp->mv_htable[y]);
		}

		kmem_free(mv_ctlp->mv_htable, mv_ctlp->mv_intr_size);
		return (DDI_FAILURE);
	}

	/* Test for high level mutex */
	if (mv_ctlp->mv_intr_pri >= ddi_intr_get_hilevel_pri()) {
		MV_DEBUG_MSG(mv_ctlp->mv_dip, MV_DBG_DEV_INTR,
		    ("!marvell88sx%d: mv_add_msi_intrs: High level interrupt "
		    "not supported\n"), instance);

		/* Free already allocated intr */
		for (y = 0; y < actual; y++) {
			(void) ddi_intr_free(mv_ctlp->mv_htable[y]);
		}

		kmem_free(mv_ctlp->mv_htable, sizeof (ddi_intr_handle_t));

		return (DDI_FAILURE);
	}

	/* Call ddi_intr_add_handler() */
	for (x = 0; x < actual; x++) {
		if (ddi_intr_add_handler(mv_ctlp->mv_htable[x], mv_intr,
		    (caddr_t)mv_ctlp, NULL) != DDI_SUCCESS) {
			cmn_err(CE_WARN, "!marvell88sx%d: Could not add "
			    "interrupt handler\n", instance);

			for (y = 0; y < x; y++) {
				(void) ddi_intr_remove_handler(
				    mv_ctlp->mv_htable[y]);
			}
			/* Free already allocated intr */
			for (y = 0; y < actual; y++) {
				(void) ddi_intr_free(mv_ctlp->mv_htable[y]);
			}

			kmem_free(mv_ctlp->mv_htable, mv_ctlp->mv_intr_size);
			return (DDI_FAILURE);
		}
	}

	(void) ddi_intr_get_cap(mv_ctlp->mv_htable[0], &mv_ctlp->mv_intr_cap);
	return (DDI_SUCCESS);
}

/*
 * round_up_power_of_2() rounds up to a power of 2
 */
static int
round_up_power_of_2(int val)
{
	register int i;

	ASSERT(val != 0);

	for (i = (sizeof (int) * NBBY) - 1; i >= 0; --i) {
		if ((1 << i) & val)
			break;
	}
	if (val == (1 << i))
		return (val);
	else
		return (1 << ++i);
}

/*
 * mv_rem_intrs() disables all the interrupts and then removes the
 * the interrupt handler
 */

static void
mv_rem_intrs(mv_ctl_t *mv_ctlp)
{
	int		x;


	/* Disable all interrupts */
	if (mv_ctlp->mv_intr_cap & DDI_INTR_FLAG_BLOCK) {
		/* Call ddi_intr_block_disable() */
		(void) ddi_intr_block_disable(mv_ctlp->mv_htable,
		    mv_ctlp->mv_intr_cnt);
	} else {
		for (x = 0; x < mv_ctlp->mv_intr_cnt; x++) {
			(void) ddi_intr_disable(mv_ctlp->mv_htable[x]);
		}
	}

	/* Call ddi_intr_remove_handler() */
	for (x = 0; x < mv_ctlp->mv_intr_cnt; x++) {
		(void) ddi_intr_remove_handler(mv_ctlp->mv_htable[x]);
		(void) ddi_intr_free(mv_ctlp->mv_htable[x]);
	}

	kmem_free(mv_ctlp->mv_htable, mv_ctlp->mv_intr_size);
}


/*
 * mv_add_legacy_intrs()  ddingINTx and legacy interrupts
 */

static int
mv_add_legacy_intrs(mv_ctl_t *mv_ctlp)
{
	dev_info_t	*devinfo = mv_ctlp->mv_dip;
	int		actual, count = 0;
	int		x, y, rc, inum = 0;
	int 		instance = ddi_get_instance(mv_ctlp->mv_dip);

	MV_DEBUG_MSG(mv_ctlp->mv_dip, MV_DBG_DEV_INTR, ("marvell88sx%d: "
	    "Adding legacy interrupts"), instance);


	/* get number of interrupts */
	rc = ddi_intr_get_nintrs(devinfo, DDI_INTR_TYPE_FIXED, &count);
	if (rc != DDI_SUCCESS) {
		cmn_err(CE_WARN, "!marvell88sx%d: Can not get number "
		    "of interrupts, rc %d\n", instance, rc);

		return (DDI_FAILURE);
	}
	if (count == 0) {
		cmn_err(CE_WARN, "!marvell88sx%d: 0 is not a valid number "
		    "of interrupts\n", instance);

		return (DDI_FAILURE);
	}

	/* Allocate an array of interrupt handles */
	mv_ctlp->mv_intr_size = count * sizeof (ddi_intr_handle_t);
	mv_ctlp->mv_htable = kmem_zalloc(mv_ctlp->mv_intr_size, KM_SLEEP);

	/* call ddi_intr_alloc() */
	rc = ddi_intr_alloc(devinfo, mv_ctlp->mv_htable, DDI_INTR_TYPE_FIXED,
	    inum, count, &actual, DDI_INTR_ALLOC_STRICT);

	if ((rc != DDI_SUCCESS) || (actual == 0)) {
		cmn_err(CE_WARN, "marvell88sx%d: ddi_intr_alloc() failed, "
		    "rc %d\n", instance, rc);
		kmem_free(mv_ctlp->mv_htable, mv_ctlp->mv_intr_size);

		return (DDI_FAILURE);
	}

	if (actual < count) {
		MV_DEBUG_MSG(mv_ctlp->mv_dip, MV_DBG_DEV_INTR,
		    ("marvell88sx%d: Requested: %d, Received: %d"),
		    instance, count, actual);

		for (x = 0; x < actual; x++) {
			(void) ddi_intr_free(mv_ctlp->mv_htable[x]);
		}

		kmem_free(mv_ctlp->mv_htable, mv_ctlp->mv_intr_size);
		return (DDI_FAILURE);
	}

	mv_ctlp->mv_intr_cnt = actual;

	/* Get intr priority */
	if (ddi_intr_get_pri(mv_ctlp->mv_htable[0], &mv_ctlp->mv_intr_pri) !=
	    DDI_SUCCESS) {
		cmn_err(CE_WARN, "!marvell88sx%d: ddi_get_pri() failed\n ",
		    instance);

		for (x = 0; x < actual; x++) {
			(void) ddi_intr_free(mv_ctlp->mv_htable[x]);
		}

		kmem_free(mv_ctlp->mv_htable, mv_ctlp->mv_intr_size);
		return (DDI_FAILURE);
	}

	/* Test for high level mutex */
	if (mv_ctlp->mv_intr_pri >= ddi_intr_get_hilevel_pri()) {
		cmn_err(CE_WARN, "!marvell88sx%d: mv_add_msi_intrs: "
		    "High level interrupt not supported\n", instance);

		for (x = 0; x < actual; x++) {
			(void) ddi_intr_free(mv_ctlp->mv_htable[x]);
		}

		kmem_free(mv_ctlp->mv_htable, sizeof (ddi_intr_handle_t));

		return (DDI_FAILURE);
	}

	/* Call ddi_intr_add_handler() */
	for (x = 0; x < actual; x++) {
		if (ddi_intr_add_handler(mv_ctlp->mv_htable[x], mv_intr,
		    (caddr_t)mv_ctlp, NULL) != DDI_SUCCESS) {
			cmn_err(CE_WARN, "!marvell88sx%d: "
			    "ddi_intr_add_handler() failed\n", instance);

			for (y = 0; y < actual; y++) {
				(void) ddi_intr_free(mv_ctlp->mv_htable[y]);
			}

			kmem_free(mv_ctlp->mv_htable, mv_ctlp->mv_intr_size);
			return (DDI_FAILURE);
		}
	}

	return (DDI_SUCCESS);
}



/*
 * free_edma_resources() frees EDMA resources
 */

static void
free_edma_resources(struct mv_port_state *mv_port, unsigned int queue_entry)
{
	if (mv_port->eprd_dma_handle[queue_entry] != NULL) {
		(void) ddi_dma_unbind_handle(
		    mv_port->eprd_dma_handle[queue_entry]);
		(void) ddi_dma_mem_free(&mv_port->eprd_acc_handle[queue_entry]);
		ddi_dma_free_handle(&mv_port->eprd_dma_handle[queue_entry]);
		mv_port->eprd_dma_handle[queue_entry] = NULL;
	}

	if (queue_entry == 0) {
		if (mv_port->crqq_dma_handle != NULL) {
			(void) ddi_dma_unbind_handle(
			    mv_port->crqq_dma_handle);
			(void) ddi_dma_mem_free(&mv_port->crqq_acc_handle);
			ddi_dma_free_handle(&mv_port->crqq_dma_handle);
			mv_port->crqq_dma_handle = NULL;
			mv_port->crqq_acc_handle = NULL;
		}
		if (mv_port->crsq_dma_handle != NULL) {
			(void) ddi_dma_unbind_handle(
			    mv_port->crsq_dma_handle);
			(void) ddi_dma_mem_free(&mv_port->crsq_acc_handle);
			ddi_dma_free_handle(&mv_port->crsq_dma_handle);
			mv_port->crsq_dma_handle = NULL;
			mv_port->crsq_acc_handle = NULL;
		}
	}
}

/*
 * mv_power() is the power management module
 */

static int
mv_power(dev_info_t *dip, int component, int level)
{
#if ! defined(__lock_lint)
	_NOTE(ARGUNUSED(component))
#endif

	mv_ctl_t *mv_ctlp;
	int instance = ddi_get_instance(dip);
	int rval = DDI_SUCCESS;
	sata_device_t sdevice;
	int old_level;

	mv_ctlp = ddi_get_soft_state(mv_state, instance);

	if (mv_ctlp == NULL)
		return (DDI_FAILURE);

	mutex_enter(&mv_ctlp->mv_ctl_mutex);

	if (pci_config_setup(dip, &mv_ctlp->mv_config_handle) != DDI_SUCCESS) {
		MV_DEBUG_MSG(dip, MV_DBG_GEN,
		    ("marvell88sx%d: pci_config_setup failed\n"), instance);
		mutex_exit(&mv_ctlp->mv_ctl_mutex);
		return (DDI_FAILURE);
	}

	switch (level) {
	case PM_LEVEL_D0:
		pci_config_put16(mv_ctlp->mv_config_handle, MV_PM_CSR,
		    PCI_PMCSR_D0);
		mv_ctlp->mv_power_level = PM_LEVEL_D0;
		drv_usecwait(10000);
		(void) pci_restore_config_regs(mv_ctlp->mv_dip);

		old_level = mv_ctlp->mv_power_level;
		MV_DEBUG_MSG(dip, MV_DBG_GEN, ("marvell88sx%d: "
		    "turning power ON. old level %d"), instance, old_level);

		/*
		 * If called from attach, just raise device power,
		 * restore config registers (if they were saved
		 * from a previous detach that lowered power),
		 * and exit.
		 */
		if (mv_ctlp->mv_flags & MV_ATTACH)
			break;

		mv_init_ctrl_regs(mv_ctlp);
		mv_ctlp->mv_timeout_id = timeout(mv_pkt_timeout,
		    (void *) mv_ctlp,
		    drv_usectohz(MV_MICROSECONDS_PER_SD_PKT_TICK));

		ddi_put32(mv_ctlp->mv_pci_intr_err_handle,
		    mv_ctlp->mv_pci_intr_err_regs +
		    PCI_INTERRUPT_MASK_OFFSET, PCI_INTERRUPT_ENABLED);

		/*
		 * When implemented in the SATA module, this will notify
		 * the "framework" that it should reprobe ports to see if
		 * any device changed while suspended.
		 *
		 * The power management is not supported by the SATA
		 * framework in phase I.  This need to be revisited
		 * to decide what needs to be passed to SATA framework
		 */
		sdevice.satadev_rev = SATA_DEVICE_REV;
		sdevice.satadev_addr.qual = SATA_ADDR_CNTRL;
		sata_hba_event_notify(dip, &sdevice,
		    SATA_EVNT_PWR_LEVEL_CHANGED);
		break;

	case PM_LEVEL_D3:
		if (! (mv_ctlp->mv_flags & MV_DETACH)) {
			mv_ctlp->mv_flags |= MV_NO_TIMEOUTS;
			(void) untimeout(mv_ctlp->mv_timeout_id);
			mv_ctlp->mv_flags &= ~MV_NO_TIMEOUTS;

			ddi_put32(mv_ctlp->mv_pci_intr_err_handle,
			    mv_ctlp->mv_pci_intr_err_regs +
			    PCI_INTERRUPT_MASK_OFFSET, PCI_INTERRUPT_DISABLED);

			ddi_put32(mv_ctlp->mv_device_main_handle,
			    mv_ctlp->mv_device_main_regs +
			    DEVICE_MAIN_INTR_MASK_OFFSET, 0);

			mv_ctlp->mv_power_level = PM_LEVEL_D3;
		}

		(void) pci_save_config_regs(mv_ctlp->mv_dip);

		pci_config_put16(mv_ctlp->mv_config_handle, MV_PM_CSR,
		    PCI_PMCSR_D3HOT);

		MV_DEBUG_MSG(dip, MV_DBG_GEN, ("marvell88sx%d: "
		"turning power OFF. old level %d"), instance, old_level);

		break;

	default:
		MV_DEBUG_MSG(dip, MV_DBG_GEN, ("marvell88sx%d: "
		    "unknown power level <%x>.\n"), instance, level);
		rval = DDI_FAILURE;
		break;
	}

	pci_config_teardown(&mv_ctlp->mv_config_handle);
	mutex_exit(&mv_ctlp->mv_ctl_mutex);

	return (rval);
}


/*
 * mv_probe_port() is called by SATA framework.  It checks the state of the port
 * and 	determines if there is any device attached to the port.  It returns
 * port state, port status registers and an attached device type
 * via sata_device structure.
 */

static int
mv_probe_port(dev_info_t *dip, sata_device_t *device)
{
	mv_ctl_t *mv_ctlp;
	int instance;
	ushort_t port;
	struct mv_subctlr *subctlr;
	struct mv_port_state *port_state;

	instance = ddi_get_instance(dip);
	mv_ctlp = ddi_get_soft_state(mv_state, instance);
	port = device->satadev_addr.cport;
	device->satadev_state = SATA_STATE_READY;
	device->satadev_type = SATA_DTYPE_UNKNOWN;

	if (port < MV_NUM_PORTS_PER_SUBCTRLR) {
		subctlr = mv_ctlp->mv_subctl[MV_FIRST_SUBCTRLR];
	} else {
		subctlr = mv_ctlp->mv_subctl[MV_SECOND_SUBCTRLR];
		port -= MV_NUM_PORTS_PER_SUBCTRLR;
	}
	port_state = subctlr->mv_port_state[port];

	/* Now we have access to the instance-specific data */
	/* For debugging purpose may verify sata address */

	mutex_enter(&port_state->mv_port_mutex);

	/*
	 * Check controller port status
	 */
	if (! mv_check_link(port_state, device)) {
		uint32_t det;

		det = (device->satadev_scr.sstatus & STATUS_BRIDGE_PORT_DET) >>
		    STATUS_BRIDGE_PORT_DET_SHIFT;
		switch (det) {
		case STATUS_BRIDGE_PORT_DET_NODEV:
			device->satadev_type = SATA_DTYPE_NONE;
			break;
		case STATUS_BRIDGE_PORT_DET_PHYOFFLINE:
			device->satadev_state = SATA_PSTATE_SHUTDOWN;
			device->satadev_type = SATA_DTYPE_NONE;
			break;
		}
	}

	mutex_exit(&port_state->mv_port_mutex);

	return (SATA_SUCCESS);
}

int mv_max_pending = MV_QDEPTH;
/*
 * mv_rw_dma_start() starts EDMA read or write operations
 */
static int
mv_rw_dma_start(struct mv_port_state *portp, sata_pkt_t *spkt, boolean_t lba48)
{
	sata_cmd_t *scmd = &spkt->satapkt_cmd;
	uint32_t qentry;
	uint32_t q_in_ptr;
	int reg_slot = 0;
	int i;
	int req_id;
	uint16_t dma_direction;
#if defined(MV_DEBUG)
	uint32_t qentry_out;
	uint64_t byte_count = 0;
	uint32_t sector_count = 0;
	uint32_t sactive;
	int j;
	uint64_t start_sector = 0;
#endif

	/* EDMA queue full? */
	/* Use value passed down by sata module rather than max value */
	/*	if (portp->num_dmas >= mv_max_pending) */
	if (portp->num_dmas >=
	    (spkt->satapkt_cmd.satacmd_flags.sata_max_queue_depth + 1))
		return (SATA_TRAN_QUEUE_FULL);

	if (! portp->dma_enabled)
		mv_enable_dma(portp);

	switch (scmd->satacmd_flags.sata_data_direction) {
	case SATA_DIR_READ:
		dma_direction = CDIR_DEV_TO_SYSTEM;
		break;
	case SATA_DIR_WRITE:
		dma_direction = CDIR_SYSTEM_TO_DEV;
		break;
	case SATA_DIR_NODATA_XFER:
	default:
		/* unsupported DMA command */
		return (SATA_TRAN_CMD_UNSUPPORTED);
	}

	/*
	 * We avoid reading the Q In Pointer register for two
	 * reasons.  1) It is slightly faster and 2) On a Sun Fire x4500
	 * under stress testing (running 2-36 hours) we have seen
	 * bad reads of "0".
	 */
	q_in_ptr = portp->req_q_in_register;

	qentry = q_in_ptr & EDMA_REQUEST_Q_IN_PTR_MASK;
	qentry >>= EDMA_REQUEST_Q_IN_PTR_SHIFT;

	req_id = ddi_ffs(~portp->dma_in_use_map);
	--req_id;

#if defined(MV_DEBUG)
	sactive = ddi_get32(portp->bridge_handle,
	    portp->bridge_regs +
	    SATA_INTERFACE_SACTIVE_OFFSET);
	ASSERT((sactive & (1 << req_id)) == 0);
#endif

	ASSERT(req_id >= 0 && req_id < MV_QDEPTH);
	ASSERT(portp->mv_dma_pkts[req_id] == NULL);


#if defined(MV_DEBUG)
	if (mv_debug_flags & MV_DBG_RW_START) {
		printf("mv_rw_dma_start: qentry %d req_id %d sactive 0x%x\n",
		    qentry, req_id, sactive);
	}
#endif

	/* Keep track of this DMA packet in flight */
	portp->mv_dma_pkts[req_id] = spkt;

	ASSERT(scmd->satacmd_num_dma_cookies > 0);


	/*
	 * Store ePRD address in corresponding crqq entry
	 */
	ddi_put32(portp->crqq_acc_handle,
	    &portp->crqq_addr[qentry].eprd_tbl_base_low_addr,
	    (uint32_t)portp->eprd_cookie[req_id].dmac_laddress);
#if ! defined(__lock_lint)
	ddi_put32(portp->crqq_acc_handle,
	    &portp->crqq_addr[qentry].eprd_tbl_base_high_addr,
	    (uint32_t)(portp->eprd_cookie[req_id].dmac_laddress
	    >> 32));
#endif

	/* Fill in the ePRD from the SATA cmd dma_cookie_list */
	for (i = 0; i < scmd->satacmd_num_dma_cookies; ++i) {
		portp->eprd_addr[req_id][i].phys_mem_low =
		    (uint32_t)scmd->satacmd_dma_cookie_list[i].dmac_laddress;
#if ! defined(__lock_lint)
		portp->eprd_addr[req_id][i].phys_mem_high =
		    (uint32_t)(scmd->satacmd_dma_cookie_list[i].dmac_laddress>>
		    32);
#endif
		ASSERT(scmd->satacmd_dma_cookie_list[i].dmac_size <=
		    MV_MAX_DMA_REGION);
		/* Set the number of bytes to transfer, 0 implies 64KB */
		portp->eprd_addr[req_id][i].byte_count_flags =
		    (uint16_t)scmd->satacmd_dma_cookie_list[i].dmac_size;
	}
	/* Mark the end of the ePRD table */
	portp->eprd_addr[req_id][i - 1].byte_count_flags |= EPRD_EOT;

#if defined(MV_DEBUG)
	for (j = 0; j < scmd->satacmd_num_dma_cookies; ++j) {
		if (mv_debug_flags & MV_DBG_RW_START) {
			printf("mv_rw_dma_start: ePRD[%d] = 0x%x bytes"
			    "@ %x:%x\n",
			    j,
			    portp->eprd_addr[req_id][j].byte_count_flags,
			    portp->eprd_addr[req_id][j].phys_mem_high,
			    portp->eprd_addr[req_id][j].phys_mem_low);
		}
		/*
		 * If the low 16 bits of byte_count_flags are 0, then a
		 * MV_MAX_DMA_REGION (64k) byte transfer is being requested.
		 * Account for that here in the total byte count.
		 */
		byte_count +=
		    (uint16_t)portp->eprd_addr[req_id][j].byte_count_flags != 0
		    ? (uint16_t)portp->eprd_addr[req_id][j].byte_count_flags
		    : MV_MAX_DMA_REGION;

		if (mv_debug_logging) {
			portp->mv_debug_info[portp->mv_debug_index].dma_addrs[j]
			    = ((uint64_t)portp->eprd_addr[req_id][j].
			    phys_mem_high << 32) |
			    portp->eprd_addr[req_id][j].phys_mem_low;
		}
	}
#endif

	/* Now synch with the Marvell chip */
	(void) ddi_dma_sync(portp->eprd_dma_handle[req_id],
	    0, i * sizeof (eprd_t), DDI_DMA_SYNC_FORDEV);

	/* Set the direction of I/O and the id tag */
	ddi_put16(portp->crqq_acc_handle, &portp->crqq_addr[qentry].ctl_flags,
	    (req_id << CRQB_ID_SHIFT) | dma_direction);

	/* Now fill in the ATA task file registers in the CRQ */
	/* If 48-bit mode, write the most significant bytes */
	if (lba48) {
		if (scmd->satacmd_flags.sata_queued) {
			ddi_put16(portp->crqq_acc_handle,
			    &portp->crqq_addr[qentry].ata_cmd_regs[reg_slot++],
			    CRQB_ATA_CMD_REG(AT_FEATURE,
			    scmd->satacmd_features_reg_ext,
			    B_FALSE));
#if defined(MV_DEBUG)
			sector_count = scmd->satacmd_features_reg_ext << 8;
#endif
		} else {
			ddi_put16(portp->crqq_acc_handle,
			    &portp->crqq_addr[qentry].ata_cmd_regs[reg_slot++],
			    CRQB_ATA_CMD_REG(AT_COUNT,
			    scmd->satacmd_sec_count_msb,
			    B_FALSE));
#if defined(MV_DEBUG)
			sector_count = scmd->satacmd_sec_count_msb << 8;
#endif
		}
		ddi_put16(portp->crqq_acc_handle,
		    &portp->crqq_addr[qentry].ata_cmd_regs[reg_slot++],
		    CRQB_ATA_CMD_REG(AT_SECT,
		    scmd->satacmd_lba_low_msb, B_FALSE));
		ddi_put16(portp->crqq_acc_handle,
		    &portp->crqq_addr[qentry].ata_cmd_regs[reg_slot++],
		    CRQB_ATA_CMD_REG(AT_LCYL, scmd->satacmd_lba_mid_msb,
		    B_FALSE));
		ddi_put16(portp->crqq_acc_handle,
		    &portp->crqq_addr[qentry].ata_cmd_regs[reg_slot++],
		    CRQB_ATA_CMD_REG(AT_HCYL, scmd->satacmd_lba_high_msb,
		    B_FALSE));
#if defined(MV_DEBUG)
		start_sector |= (uint64_t)scmd->satacmd_lba_high_msb << 40;
		start_sector |= (uint64_t)scmd->satacmd_lba_mid_msb << 32;
		start_sector |= (uint64_t)scmd->satacmd_lba_low_msb << 24;
#endif
	}
	if (scmd->satacmd_flags.sata_queued) {
		ddi_put16(portp->crqq_acc_handle,
		    &portp->crqq_addr[qentry].ata_cmd_regs[reg_slot++],
		    CRQB_ATA_CMD_REG(AT_FEATURE,
		    scmd->satacmd_features_reg,
		    B_FALSE));
		ddi_put16(portp->crqq_acc_handle,
		    &portp->crqq_addr[qentry].ata_cmd_regs[reg_slot++],
		    CRQB_ATA_CMD_REG(AT_COUNT,
		    req_id << SATA_TAG_QUEUING_SHIFT,
		    B_FALSE));
#if defined(MV_DEBUG)
		sector_count |= scmd->satacmd_features_reg;
#endif
	} else {
		ddi_put16(portp->crqq_acc_handle,
		    &portp->crqq_addr[qentry].ata_cmd_regs[reg_slot++],
		    CRQB_ATA_CMD_REG(AT_COUNT,
		    scmd->satacmd_sec_count_lsb,
		    B_FALSE));
#if defined(MV_DEBUG)
		sector_count |= scmd->satacmd_sec_count_lsb;
#endif
	}
	ddi_put16(portp->crqq_acc_handle,
	    &portp->crqq_addr[qentry].ata_cmd_regs[reg_slot++],
	    CRQB_ATA_CMD_REG(AT_SECT, scmd->satacmd_lba_low_lsb, B_FALSE));
	ddi_put16(portp->crqq_acc_handle,
	    &portp->crqq_addr[qentry].ata_cmd_regs[reg_slot++],
	    CRQB_ATA_CMD_REG(AT_LCYL, scmd->satacmd_lba_mid_lsb, B_FALSE));
	ddi_put16(portp->crqq_acc_handle,
	    &portp->crqq_addr[qentry].ata_cmd_regs[reg_slot++],
	    CRQB_ATA_CMD_REG(AT_HCYL, scmd->satacmd_lba_high_lsb, B_FALSE));
	ddi_put16(portp->crqq_acc_handle,
	    &portp->crqq_addr[qentry].ata_cmd_regs[reg_slot++],
	    CRQB_ATA_CMD_REG(AT_DRVHD, scmd->satacmd_device_reg, B_FALSE));
	ddi_put16(portp->crqq_acc_handle,
	    &portp->crqq_addr[qentry].ata_cmd_regs[reg_slot++],
	    CRQB_ATA_CMD_REG(AT_CMD, scmd->satacmd_cmd_reg, B_TRUE));

#if defined(MV_DEBUG)
	start_sector |= scmd->satacmd_lba_high_lsb << 16;
	start_sector |= scmd->satacmd_lba_mid_lsb << 8;
	start_sector |= scmd->satacmd_lba_low_lsb << 0;

	if (sector_count * SATA_DISK_SECTOR_SIZE != byte_count) {
#if defined(_LP64)
		printf("mv_rw_dma_start: byte_count 0x%lx sector_count 0x%x "
		    "queued %d\n",
		    byte_count, sector_count, scmd->satacmd_flags.sata_queued);
#else
		printf("mv_rw_dma_start: byte_count 0x%llx sector_count 0x%x "
		    "queued %d\n",
		    byte_count, sector_count, scmd->satacmd_flags.sata_queued);
#endif
		panic("mv_rw_dma_start: DMA mismatch");
	}
#endif

	/* Advance to the next queue entry */
	if (++qentry == MV_QDEPTH)
		qentry = 0;

#if defined(MV_DEBUG)
	/* Get the request queue empty index */
	qentry_out = ddi_get32(portp->edma_handle, portp->edma_regs
	    + EDMA_REQUEST_Q_OUT_PTR_OFFSET);
	qentry_out &= EDMA_REQUEST_Q_OUT_PTR_MASK;
	qentry_out >>= EDMA_REQUEST_Q_OUT_PTR_SHIFT;

	if (mv_debug_flags & MV_DBG_RW_START) {
		printf("mv_rw_start: fill index %d empty index %d\n",
		    qentry, qentry_out);
	}

	if (mv_debug_logging) {
		portp->mv_debug_info[portp->mv_debug_index].start_or_finish =
		    MV_START;
		portp->mv_debug_info[portp->mv_debug_index].req_id = req_id;
		portp->mv_debug_info[portp->mv_debug_index].num_dmas =
		    portp->num_dmas + 1;
		portp->mv_debug_info[portp->mv_debug_index].fill_index = qentry;
		portp->mv_debug_info[portp->mv_debug_index].empty_index =
		    qentry_out;
		portp->mv_debug_info[portp->mv_debug_index].num_sectors =
		    sector_count;
		portp->mv_debug_info[portp->mv_debug_index].start_sector =
		    start_sector;
		portp->mv_debug_info[portp->mv_debug_index].num_bytes =
		    byte_count;
		if (++portp->mv_debug_index == MV_DEBUG_INFO_SIZE)
			portp->mv_debug_index = 0;

	}
#endif

	q_in_ptr &= ~EDMA_REQUEST_Q_IN_PTR_MASK;
	q_in_ptr |= qentry << EDMA_REQUEST_Q_IN_PTR_SHIFT;
	portp->req_q_in_register = q_in_ptr;

	/* Make the command available */
	ddi_put32(portp->edma_handle,
	    portp->edma_regs + EDMA_REQUEST_Q_IN_PTR_OFFSET,
	    q_in_ptr);

	portp->dma_in_use_map |= 1 << req_id;
	++portp->num_dmas;
	ASSERT((portp->num_dmas > 0) && (portp->num_dmas <= MV_QDEPTH));

#if defined(MV_DEBUG)
	if (mv_debug_flags & MV_DBG_RW_START) {
		printf("mv_rw_start: num_dmas %d dma_in_use_map 0x%x\n",
		    portp->num_dmas, portp->dma_in_use_map);
	}
#endif

	/* If synchronous mode, wait */
	if (spkt->satapkt_op_mode & SATA_OPMODE_SYNCH) {
		if (spkt->satapkt_op_mode & SATA_OPMODE_POLLING) {
			/* - is ddi_get_time() acceptable?  Wrap? Mods? */
			time_t start_time = ddi_get_time();

			mutex_exit(&portp->mv_port_mutex);
			mv_disable_port_intr(portp->mv_ctlp, portp->port_num);
			mutex_enter(&portp->mv_port_mutex);

			/*
			 * Wait for packet to go not busy.
			 */
			while (spkt->satapkt_reason
			    == SATA_PKT_BUSY) {
				mutex_exit(&portp->mv_port_mutex);
				(void) mv_intr((caddr_t)portp->mv_ctlp,
				    (caddr_t)NULL);
				drv_usecwait(100);
				mutex_enter(&portp->mv_port_mutex);
				if (ddi_get_time() >
				    (start_time + spkt->satapkt_time)) {
					cmn_err(CE_WARN,
					    "!marvell88sx%d: "
					    "polled read/write request "
					    "never completed - port %d",
					    ddi_get_instance(
					    portp->mv_ctlp->mv_dip),
					    portp->port_num);
					spkt->satapkt_reason =
					    SATA_PKT_TIMEOUT;
				}
			}
			mutex_exit(&portp->mv_port_mutex);
			mv_enable_port_intr(portp->mv_ctlp, portp->port_num);
			mutex_enter(&portp->mv_port_mutex);
		} else while (spkt->satapkt_reason == SATA_PKT_BUSY) {
			cv_wait(&portp->mv_port_cv, &portp->mv_port_mutex);
		}
	}

	return (SATA_TRAN_ACCEPTED);
}



/*
 * mv_start() is called by sata framework to start a command.
 */

static int
mv_start(dev_info_t *dip, sata_pkt_t *spkt)
{
	mv_ctl_t *mv_ctlp;
	struct mv_port_state *portp;
	struct mv_subctlr *subctrlp;
	sata_cmd_t *scmd;
	int instance;
	int port_num;
	int rval = SATA_TRAN_ACCEPTED;
	uint32_t edma_config;
	uint8_t cmd_queuing_type;
	uint8_t status;

	MV_DEBUG_MSG(dip, MV_DBG_START, ("mv_start(%p, %p)\n"), (void *)dip,
	    (void *)spkt);

	instance = ddi_get_instance(dip);
	mv_ctlp = ddi_get_soft_state(mv_state, instance);

	port_num = spkt->satapkt_device.satadev_addr.cport;

	ASSERT(port_num < (MV_NUM_PORTS_PER_SUBCTRLR * MV_MAX_NUM_SUBCTRLR));

	mutex_enter(&mv_ctlp->mv_ctl_mutex);
	/* Get which subcontroller and which port on that subcontroller */
	if (port_num < MV_NUM_PORTS_PER_SUBCTRLR)
		subctrlp = mv_ctlp->mv_subctl[MV_FIRST_SUBCTRLR];
	else
		subctrlp = mv_ctlp->mv_subctl[MV_SECOND_SUBCTRLR];
	mutex_exit(&mv_ctlp->mv_ctl_mutex);

	mutex_enter(&subctrlp->mv_subctrl_mutex);
	portp = subctrlp->mv_port_state[port_num % MV_NUM_PORTS_PER_SUBCTRLR];

	mutex_exit(&subctrlp->mv_subctrl_mutex);

	scmd = &spkt->satapkt_cmd;
	spkt->satapkt_reason = SATA_PKT_BUSY;

	ASSERT((spkt->satapkt_op_mode &
	    (SATA_OPMODE_POLLING|SATA_OPMODE_SYNCH)) !=
	    SATA_OPMODE_POLLING);

	/*
	 * If we are going to poll for this command, clean up any pending
	 * interrupts in case that would prevent the command from executing.
	 */
	if (spkt->satapkt_op_mode & SATA_OPMODE_POLLING)
		(void) mv_intr((caddr_t)portp->mv_ctlp, (caddr_t)NULL);

	mutex_enter(&portp->mv_port_mutex);

	MV_DEBUG_MSG(dip, MV_DBG_START, ("mv_start: cmd = 0x%x\n"),
	    scmd->satacmd_cmd_reg);


	/*
	 * If the sata framework has said the restore of device parameters
	 * after a device reset is complete, note that fact.
	 */
	if (scmd->satacmd_flags.sata_clear_dev_reset)
		portp->dev_restore_needed = B_FALSE;


	/*
	 * If we are waiting for the sata framework to restore the
	 * device parameters and this command doesn't indicate
	 * that we are to ignore that fact (such as the case as when this
	 * is a command that will restore the device parameters),
	 * do not execute the command and return state "busy".
	 */
	if (portp->dev_restore_needed &&
	    ! scmd->satacmd_flags.sata_ignore_dev_reset) {
		mutex_exit(&portp->mv_port_mutex);
		return (SATA_TRAN_BUSY);
	}


	/*
	 * The satacmd structure distinguishes between DMA and PIO commands
	 * via the sata_protocol_pio flag.  To be safe, the sata_data_direction
	 * flag is also checked to confirm that the command actually expects to
	 * do a data transfer.
	 *
	 * The satacmd structure does not distinguish between the different
	 * types of DMA (such as queued), so the HBA has to look at the
	 * command code to determine the type of DMA.
	 */
	if ((scmd->satacmd_flags.sata_protocol_pio == 0) &&
	    (scmd->satacmd_flags.sata_data_direction != SATA_DIR_NODATA_XFER)) {
		MV_DEBUG_MSG(dip, MV_DBG_START, ("mv_start: DMA command\n"),
		    NULL);

		if ((portp->dma_frozen) || (portp->mv_pio_pkt != NULL)) {
			spkt->satapkt_reason = SATA_PKT_QUEUE_FULL;
			mutex_exit(&portp->mv_port_mutex);
			return (SATA_TRAN_QUEUE_FULL);
		}

		/* Check if we are switching queuing mode */
		switch (scmd->satacmd_cmd_reg) {
		case SATAC_READ_DMA:
		case SATAC_WRITE_DMA:
		case SATAC_READ_DMA_EXT:
		case SATAC_WRITE_DMA_EXT:
			cmd_queuing_type = MV_QUEUING_NONE;
			break;
		case SATAC_READ_DMA_QUEUED:
		case SATAC_WRITE_DMA_QUEUED:
		case SATAC_READ_DMA_QUEUED_EXT:
		case SATAC_WRITE_DMA_QUEUED_EXT:
			cmd_queuing_type = MV_QUEUING_TCQ;
			break;
		case SATAC_READ_FPDMA_QUEUED:
		case SATAC_WRITE_FPDMA_QUEUED:
			cmd_queuing_type = MV_QUEUING_NCQ;
			break;
		}

		MV_DEBUG_MSG(dip, MV_DBG_START,
		    ("mv_start: cmd_queuing_type %d\n"), cmd_queuing_type);

		if (portp->queuing_type != cmd_queuing_type) {
			MV_DEBUG_MSG(dip, MV_DBG_START,
			    ("mv_start: switch queuing type\n"), NULL);

			mv_wait_for_dma(portp);
			mv_disable_dma(portp);

			switch (cmd_queuing_type) {
			case MV_QUEUING_NONE:
				edma_config = ddi_get32(portp->edma_handle,
				    portp->edma_regs + EDMA_CONFIG_OFFSET);
				edma_config &=
				    ~(EDMA_CONFIG_Q_ENABLED_MASK |
				    EDMA_CONFIG_NCQ_ENABLED_MASK);

				ddi_put32(portp->edma_handle,
				    portp->edma_regs + EDMA_CONFIG_OFFSET,
				    edma_config);

				portp->queuing_type = MV_QUEUING_NONE;
				mv_enable_dma(portp);
				break;
			case MV_QUEUING_TCQ:
				edma_config = ddi_get32(portp->edma_handle,
				    portp->edma_regs + EDMA_CONFIG_OFFSET);
				edma_config &= EDMA_CONFIG_NCQ_ENABLED_MASK;
				edma_config |= EDMA_CONFIG_Q_ENABLED_MASK;

				ddi_put32(portp->edma_handle,
				    portp->edma_regs + EDMA_CONFIG_OFFSET,
				    edma_config);

				portp->queuing_type = MV_QUEUING_TCQ;
				mv_enable_dma(portp);
				break;
			case MV_QUEUING_NCQ:
				edma_config = ddi_get32(portp->edma_handle,
				    portp->edma_regs + EDMA_CONFIG_OFFSET);
				edma_config &= ~EDMA_CONFIG_Q_ENABLED_MASK;
				edma_config |= EDMA_CONFIG_NCQ_ENABLED_MASK;

				ddi_put32(portp->edma_handle,
				    portp->edma_regs + EDMA_CONFIG_OFFSET,
				    edma_config);

				portp->queuing_type = MV_QUEUING_NCQ;
				mv_enable_dma(portp);
				break;
			}
			portp->dma_frozen = B_FALSE;
		}

		MV_DEBUG_MSG(dip, MV_DBG_START,
		    ("mv_start: queuing type %d\n"), portp->queuing_type);

		rval = mv_rw_dma_start(portp, spkt,
		    scmd->satacmd_addr_type == ATA_ADDR_LBA48);
	} else {
		if (portp->dma_enabled) {
			if (portp->num_dmas != 0)
				portp->mv_waiting_pkt = spkt;
			mv_wait_for_dma(portp);
			mv_disable_dma(portp);
			portp->mv_waiting_pkt = NULL;
		}

		/* We can only have one PIO command pending */
		if (portp->mv_pio_pkt != NULL) {
			spkt->satapkt_reason = SATA_PKT_QUEUE_FULL;
			mutex_exit(&portp->mv_port_mutex);
			return (SATA_TRAN_QUEUE_FULL);
		}

		/*
		 * Make sure that the device is not busy before accepting
		 * a new cmd
		 */
		status = (uint8_t)ddi_get32(portp->task_file2_handle,
		    (uint32_t *)(portp->task_file2_regs
		    + AT_ALTSTATUS));
		if (status & SATA_STATUS_BSY) {
			portp->dma_frozen = B_FALSE;
			mutex_exit(&portp->mv_port_mutex);
			return (SATA_TRAN_BUSY);
		}

		portp->mv_pio_pkt = spkt;

		/* If this is an LBA48 command, write the high bytes */
		if (scmd->satacmd_addr_type == ATA_ADDR_LBA48) {
			ddi_put32(portp->task_file1_handle,
			    portp->task_file1_regs + AT_COUNT,
			    scmd->satacmd_sec_count_msb);

			ddi_put32(portp->task_file1_handle,
			    portp->task_file1_regs + AT_SECT,
			    scmd->satacmd_lba_low_msb);

			ddi_put32(portp->task_file1_handle,
			    portp->task_file1_regs + AT_LCYL,
			    scmd->satacmd_lba_mid_msb);

			ddi_put32(portp->task_file1_handle,
			    portp->task_file1_regs + AT_HCYL,
			    scmd->satacmd_lba_high_msb);
		}

		/* Now write all ATA registers ending with the command reg */
		ddi_put32(portp->task_file1_handle,
		    portp->task_file1_regs + AT_FEATURE,
		    scmd->satacmd_features_reg);

		ddi_put32(portp->task_file1_handle,
		    portp->task_file1_regs + AT_COUNT,
		    scmd->satacmd_sec_count_lsb);

		ddi_put32(portp->task_file1_handle,
		    portp->task_file1_regs + AT_SECT,
		    scmd->satacmd_lba_low_lsb);

		ddi_put32(portp->task_file1_handle,
		    portp->task_file1_regs + AT_LCYL,
		    scmd->satacmd_lba_mid_lsb);

		ddi_put32(portp->task_file1_handle,
		    portp->task_file1_regs + AT_HCYL,
		    scmd->satacmd_lba_high_lsb);

		ddi_put32(portp->task_file1_handle,
		    portp->task_file1_regs + AT_DRVHD,
		    scmd->satacmd_device_reg);

		/* Write the command */
		ddi_put32(portp->task_file1_handle,
		    portp->task_file1_regs + AT_CMD,
		    scmd->satacmd_cmd_reg);


		/* Start out with all bytes residual */
		if (scmd->satacmd_bp != NULL)
			scmd->satacmd_bp->b_resid = scmd->satacmd_bp->b_bcount;

		MV_DEBUG_MSG(dip, MV_DBG_START,
		    ("mv_start: bp 0x%p b_count 0x%lx\n"),
		    (void *)scmd->satacmd_bp,
		    scmd->satacmd_bp ? scmd->satacmd_bp->b_bcount : 0);

		/* We need to prime the pump for PIO writes */
		switch (scmd->satacmd_flags.sata_data_direction) {
		case SATA_DIR_WRITE:
			mutex_exit(&portp->mv_port_mutex);
			mutex_enter(&portp->mv_ctlp->mv_ctl_mutex);
			mv_dev_intr(portp);
			mutex_exit(&portp->mv_ctlp->mv_ctl_mutex);
			mutex_enter(&portp->mv_port_mutex);
			break;
		case SATA_DIR_NODATA_XFER:
		case SATA_DIR_READ:
			break;
		default:
			cmn_err(CE_WARN,
			    "marvell88sx%d: port %d: unknown direction "
			    "- 0x%x\n",
			    ddi_get_instance(portp->mv_ctlp->mv_dip),
			    portp->port_num,
			    scmd->satacmd_flags.sata_data_direction);
			break;
		}


		/* If synchronous mode, wait */
		if (spkt->satapkt_op_mode & SATA_OPMODE_SYNCH) {
			if (spkt->satapkt_op_mode & SATA_OPMODE_POLLING) {
				/* is ddi_get_time() acceptable? */
				time_t start_time = ddi_get_time();

				mutex_exit(&portp->mv_port_mutex);
				mv_disable_port_intr(mv_ctlp, port_num);
				mutex_enter(&portp->mv_port_mutex);

				/*
				 * Wait for packet to go not busy.
				 */
				while (spkt->satapkt_reason
				    == SATA_PKT_BUSY) {
					mutex_exit(&portp->mv_port_mutex);
					(void) mv_intr((caddr_t)mv_ctlp,
					    (caddr_t)NULL);
					drv_usecwait(100);
					mutex_enter(&portp->mv_port_mutex);
					if (ddi_get_time() >
					    (start_time + spkt->satapkt_time)) {
						cmn_err(CE_WARN,
						    "!marvell88sx%d: "
						    "polled request "
						    "never completed - "
						    "port %d",
						    ddi_get_instance(dip),
						    port_num);
						spkt->satapkt_reason =
						    SATA_PKT_TIMEOUT;
					}
				}
				mutex_exit(&portp->mv_port_mutex);
				mv_enable_port_intr(mv_ctlp, port_num);
				mutex_enter(&portp->mv_port_mutex);
			} else while (spkt->satapkt_reason ==
			    SATA_PKT_BUSY) {
				cv_wait(&portp->mv_port_cv,
				    &portp->mv_port_mutex);
			}
		}

		portp->dma_frozen = B_FALSE;
	}


	MV_DEBUG_MSG(dip, MV_DBG_START, ("mv_start: exiting\n"), NULL);

	mutex_exit(&portp->mv_port_mutex);

	return (rval);
}

/*
 * mv_enable_port_intr() enables the port interrupt.
 */

static void
mv_enable_port_intr(mv_ctl_t *mv_ctlp, int port_num)
{
	int shift_cnt;
	uint32_t mask;

	mutex_enter(&mv_ctlp->mv_ctl_mutex);

	mask = ddi_get32(mv_ctlp->mv_device_main_handle,
	    mv_ctlp->mv_device_main_regs + DEVICE_MAIN_INTR_MASK_OFFSET);

	shift_cnt = (MAIN_PORT_SHIFT * port_num) +
	    (port_num >= MV_NUM_PORTS_PER_SUBCTRLR);

	mask |= ((MAIN_SATAERR | MAIN_SATADONE) << shift_cnt);

	ddi_put32(mv_ctlp->mv_device_main_handle,
	    mv_ctlp->mv_device_main_regs + DEVICE_MAIN_INTR_MASK_OFFSET,
	    mask);

	mutex_exit(&mv_ctlp->mv_ctl_mutex);
}

/*
 * mv_disable_port_intr() disables the port interrupt.
 */

static void
mv_disable_port_intr(mv_ctl_t *mv_ctlp, int port_num)
{
	int shift_cnt;
	uint32_t mask;

	mutex_enter(&mv_ctlp->mv_ctl_mutex);

	mask = ddi_get32(mv_ctlp->mv_device_main_handle,
	    mv_ctlp->mv_device_main_regs + DEVICE_MAIN_INTR_MASK_OFFSET);

	shift_cnt = (MAIN_PORT_SHIFT * port_num) +
	    (port_num >= MV_NUM_PORTS_PER_SUBCTRLR);

	mask &= ~((MAIN_SATAERR | MAIN_SATADONE) << shift_cnt);

	ddi_put32(mv_ctlp->mv_device_main_handle,
	    mv_ctlp->mv_device_main_regs + DEVICE_MAIN_INTR_MASK_OFFSET,
	    mask);

	mutex_exit(&mv_ctlp->mv_ctl_mutex);
}

/*
 * mv_abort() is called by sata framework to abort packets
 */

static int
mv_abort(dev_info_t *dip, sata_pkt_t *spkt, int flag)
{
	mv_ctl_t *mv_ctlp;
	int instance;
	struct mv_port_state *portp;
	struct mv_subctlr *subctrlp;
	int port_num;

	instance = ddi_get_instance(dip);
	mv_ctlp = ddi_get_soft_state(mv_state, instance);
	port_num = spkt->satapkt_device.satadev_addr.cport;

	mutex_enter(&mv_ctlp->mv_ctl_mutex);

	subctrlp = mv_ctlp->mv_subctl[port_num >= MV_NUM_PORTS_PER_SUBCTRLR];

	mutex_enter(&subctrlp->mv_subctrl_mutex);

	portp = subctrlp->mv_port_state[port_num % MV_NUM_PORTS_PER_SUBCTRLR];

	mutex_enter(&portp->mv_port_mutex);

	if (flag == SATA_ABORT_PACKET) {
		mv_abort_pkt(portp, &spkt);
		mv_flush_pending_io(portp, &spkt, SATA_PKT_ABORTED);
	} else {
		mv_abort_all_pkts(portp, SATA_PKT_ABORTED);
	}

	mv_dev_reset(portp, B_TRUE, B_TRUE, "abort requested");

	mutex_exit(&portp->mv_port_mutex);
	mutex_exit(&subctrlp->mv_subctrl_mutex);
	mutex_exit(&mv_ctlp->mv_ctl_mutex);

	return (0);
}

/*
 * mv_abort_pkt disables the DMA if enabled and then reset the link and
 * the device, this will blow away all pending commands
 */

static void
mv_abort_pkt(struct mv_port_state *portp, sata_pkt_t **spkt)
{
	ASSERT(MUTEX_HELD(&portp->mv_port_mutex));

	/* Disable DMA for this port.  This aborts any current command(s) */
	mv_disable_dma(portp);

	/* If this is a dma packet, reduce the # of DMA in flight */
	if (*spkt != portp->mv_pio_pkt) {
		--portp->num_dmas;
		portp->dma_in_use_map &=
		    ~(1 << (spkt - &portp->mv_dma_pkts[0]));
	}

	ASSERT(portp->num_dmas >= 0 && (portp->num_dmas < MV_QDEPTH));

	if (portp->dma_frozen && (portp->num_dmas == 0))
		cv_broadcast(&portp->mv_empty_cv);
}

/*
 * mv_reset_port() is called by SATA framework to reset the device (either
 * a port or a device connected to a port) to a known state.
 */

static int
mv_reset_port(dev_info_t *dip, sata_device_t *device)
{
	mv_ctl_t *mv_ctlp;
	boolean_t link_active;
	int instance;
	ushort_t port;
	struct mv_subctlr *subctlr;
	struct mv_port_state *port_state;
	int ret = SATA_SUCCESS;

	instance = ddi_get_instance(dip);
	mv_ctlp = ddi_get_soft_state(mv_state, instance);
	port = device->satadev_addr.cport;

	mutex_enter(&mv_ctlp->mv_ctl_mutex);

	/* Get which subcontroller and which port on that subcontroller */
	if (port < MV_NUM_PORTS_PER_SUBCTRLR) {
		subctlr = mv_ctlp->mv_subctl[MV_FIRST_SUBCTRLR];
	} else {
		subctlr = mv_ctlp->mv_subctl[MV_SECOND_SUBCTRLR];
		port -= MV_NUM_PORTS_PER_SUBCTRLR;
	}
	mutex_enter(&subctlr->mv_subctrl_mutex);
	port_state = subctlr->mv_port_state[port];

	mutex_enter(&port_state->mv_port_mutex);

	switch (device->satadev_addr.qual) {
	case SATA_ADDR_CPORT:
		/*
		 * Reset the port - actually reset the link between the
		 * port and the device.
		 */
		link_active = mv_reset_link(port_state, device, B_FALSE);

		/* Port state should be returned here */
		device->satadev_state = SATA_STATE_READY;

		device->satadev_type = link_active ?
		    SATA_DTYPE_UNKNOWN :
		    SATA_DTYPE_NONE;
		/*
		 * Link changes should cause event notification from
		 * link interrupts
		 */
		break;
	case SATA_ADDR_DCPORT:
		/*
		 * Reset the device - actually the device and the link
		 */
		link_active =  mv_reset_link(port_state, device, B_TRUE);
		device->satadev_type = link_active ?
		    SATA_DTYPE_UNKNOWN :
		    SATA_DTYPE_NONE;
		break;
	case SATA_ADDR_CNTRL:
		MV_DEBUG_MSG(mv_ctlp->mv_dip, MV_DBG_GEN,
		    ("mv_reset_port: sata_reset_all not supported"), NULL);
		ret = SATA_FAILURE;
		break;
	case SATA_ADDR_PMPORT:
	case SATA_ADDR_DPMPORT:
		MV_DEBUG_MSG(mv_ctlp->mv_dip, MV_DBG_GEN,
		    ("mv_reset_port: port multipliers not yet supported"),
		    NULL);
		/* FALLSTHROUGH */
	default:
		/* Unsupported case */
		ret = SATA_FAILURE;
		break;
	}
	mutex_exit(&port_state->mv_port_mutex);
	mutex_exit(&subctlr->mv_subctrl_mutex);
	mutex_exit(&mv_ctlp->mv_ctl_mutex);

	return (ret);
}

/*
 * mv_activate() is called by sata framework to activate a port
 * (cfgadm -x connect satax/y)
 */

static int
mv_activate(dev_info_t *dip, sata_device_t *device)
{
	mv_ctl_t *mv_ctlp = NULL;
	int instance;
	boolean_t link_active;
	ushort_t port;
	struct mv_subctlr *subctlr;
	struct mv_port_state *port_state;
	int ret_val = SATA_SUCCESS;

	instance = ddi_get_instance(dip);
	mv_ctlp = ddi_get_soft_state(mv_state, instance);
	port = device->satadev_addr.cport;
	device->satadev_state = SATA_STATE_READY;

	mutex_enter(&mv_ctlp->mv_ctl_mutex);

	if (port < MV_NUM_PORTS_PER_SUBCTRLR) {
		subctlr = mv_ctlp->mv_subctl[MV_FIRST_SUBCTRLR];
	} else {
		subctlr = mv_ctlp->mv_subctl[MV_SECOND_SUBCTRLR];
		port -= MV_NUM_PORTS_PER_SUBCTRLR;
	}
	mutex_enter(&subctlr->mv_subctrl_mutex);
	port_state = subctlr->mv_port_state[port];

	mutex_enter(&port_state->mv_port_mutex);

	/*
	 * Check controller port status
	 */
	(void) mv_check_link(port_state, device);

	if (mv_ctlp->mv_model == MV_MODEL_60XX) {
		uint32_t config_reg;

		config_reg = ddi_get32(port_state->edma_handle,
		    port_state->edma_regs + SERIAL_ATA_INTER_CONFIG_OFFSET);

		config_reg &= ~PHY_SHUTDOWN;

		ddi_put32(port_state->edma_handle,
		    port_state->edma_regs + SERIAL_ATA_INTER_CONFIG_OFFSET,
		    config_reg);
	} else if (mv_ctlp->mv_model == MV_MODEL_50XX) {
		uint32_t test_ctlr_reg;

		test_ctlr_reg = ddi_get32(subctlr->mv_ctrl_handle,
		    subctlr->mv_ctrl_regs + SATAHC_BRIDGES_TEST_CTL_OFFSET);

		test_ctlr_reg &=
		    ~SATAHC_BRIDGES_TEST_CTL_PORT_PHY_SHUTDOWN_BIT(port);

		ddi_put32(subctlr->mv_ctrl_handle,
		    subctlr->mv_ctrl_regs + SATAHC_BRIDGES_TEST_CTL_OFFSET,
		    test_ctlr_reg);

	}

	/*
	 * Link not active, reset link
	 */
	link_active = mv_reset_link(port_state, device, B_TRUE);

	if (! link_active) {
		/*
		 * Link not active, indicate no device
		 */
		device->satadev_type = SATA_DTYPE_NONE;
		ret_val = SATA_SUCCESS;
	}
	else
		device->satadev_type = SATA_DTYPE_UNKNOWN;


	mutex_exit(&port_state->mv_port_mutex);
	mutex_exit(&subctlr->mv_subctrl_mutex);
	mutex_exit(&mv_ctlp->mv_ctl_mutex);

	return (ret_val);
}

/*
 * mv_deactivate() is called by sata framework to deactivate a port
 * (cfgadm -x disconnect satax/y)
 */

static int
mv_deactivate(dev_info_t *dip, sata_device_t *device)
{
	mv_ctl_t *mv_ctlp = NULL;
	int instance;
	ushort_t port;
	struct mv_subctlr *subctlr;
	struct mv_port_state *mv_port;
	uint32_t bridge_port_status;
	uint32_t bridge_port_serror;
	uint32_t bridge_port_control;

	instance = ddi_get_instance(dip);
	mv_ctlp = ddi_get_soft_state(mv_state, instance);

	port = device->satadev_addr.cport;

	if (port < MV_NUM_PORTS_PER_SUBCTRLR)
		subctlr = mv_ctlp->mv_subctl[MV_FIRST_SUBCTRLR];
	else {
		subctlr = mv_ctlp->mv_subctl[MV_SECOND_SUBCTRLR];
		port -= MV_NUM_PORTS_PER_SUBCTRLR;
	}
	mv_port = subctlr->mv_port_state[port];

	if (mv_ctlp->mv_model == MV_MODEL_60XX) {
		uint32_t config_reg;

		mutex_enter(&mv_port->mv_port_mutex);

		config_reg = ddi_get32(mv_port->edma_handle,
		    mv_port->edma_regs +
		    SERIAL_ATA_INTER_CONFIG_OFFSET);

		config_reg |= PHY_SHUTDOWN;

		ddi_put32(mv_port->edma_handle,
		    mv_port->edma_regs + SERIAL_ATA_INTER_CONFIG_OFFSET,
		    config_reg);

		mutex_exit(&mv_port->mv_port_mutex);
	} else if (mv_ctlp->mv_model == MV_MODEL_50XX) {
		uint32_t test_ctlr_reg;

		mutex_enter(&subctlr->mv_subctrl_mutex);

		test_ctlr_reg = ddi_get32(subctlr->mv_ctrl_handle,
		    subctlr->mv_ctrl_regs +
		    SATAHC_BRIDGES_TEST_CTL_OFFSET);

		test_ctlr_reg |=
		    SATAHC_BRIDGES_TEST_CTL_PORT_PHY_SHUTDOWN_BIT(port);

		ddi_put32(subctlr->mv_ctrl_handle,
		    subctlr->mv_ctrl_regs +
		    SATAHC_BRIDGES_TEST_CTL_OFFSET,
		    test_ctlr_reg);

		mutex_exit(&subctlr->mv_subctrl_mutex);
	}

	mutex_enter(&mv_port->mv_port_mutex);


	/* Read the control bridge port register */
	bridge_port_control = ddi_get32(mv_port->bridge_handle,
	    mv_port->bridge_regs +
	    SATA_SCONTROL_BRIDGE_OFFSET);

	bridge_port_control &= ~SCONTROL_BRIDGE_PORT_DET;
	bridge_port_control |= SCONTROL_BRIDGE_PORT_DET_DISABLE;


	/* Disable the port */
	ddi_put32(mv_port->bridge_handle,
	    mv_port->bridge_regs + SATA_SCONTROL_BRIDGE_OFFSET,
	    bridge_port_control);

	/* Read the status bridge port register */
	bridge_port_status = ddi_get32(mv_port->bridge_handle,
	    mv_port->bridge_regs + SATA_STATUS_BRIDGE_OFFSET);

	/* Read the serror bridge port register */
	bridge_port_serror = ddi_get32(mv_port->bridge_handle,
	    mv_port->bridge_regs + SATA_SERROR_BRIDGE_OFFSET);

	/* Read the control bridge port register */
	bridge_port_control = ddi_get32(mv_port->bridge_handle,
	    mv_port->bridge_regs + SATA_SCONTROL_BRIDGE_OFFSET);
	mutex_exit(&mv_port->mv_port_mutex);

	/*
	 * FEr SATA#14 SStatus register contains the Wrong value when PHY is
	 * in offline mode.  This claimed to be fixed in C0 of 60xx but
	 * we still see it in C0 stepping.
	 */
	device->satadev_type = SATA_DTYPE_NONE;
	device->satadev_scr.sstatus = bridge_port_status;
	device->satadev_scr.serror = bridge_port_serror;
	device->satadev_scr.scontrol = bridge_port_control;
	/*
	 * When NCQ and PM is implemented in phase 2 sactive field and
	 * snotific fields need to be updated. for 60X1.
	 */
	device->satadev_scr.sactive = 0;
	device->satadev_scr.snotific = 0;
	device->satadev_state = SATA_PSTATE_SHUTDOWN;

	return (SATA_SUCCESS);
}


/*
 * mv_map_port_registers() maps the per port registers
 */

static int
mv_map_port_registers(
	dev_info_t *dip,
	struct mv_port_state *mv_port_state,
	unsigned int bridge_offset,
	unsigned int edma_offset,
	unsigned int task_file_1_offset,
	unsigned int task_file_2_offset
)
{
	/* Map the bridge registers */
	if (ddi_regs_map_setup(dip, MARVELL_BASE_REG,
	    (caddr_t *)&mv_port_state->bridge_regs,
	    bridge_offset, SATA_BRIDGE_REGS_LEN,
	    &dev_attr,
	    &mv_port_state->bridge_handle) != DDI_SUCCESS) {
		cmn_err(CE_WARN, "mv_map_port_registers: could not map"
		    " Bridge registers for port %d\n",
		    mv_port_state->port_num);
		return (B_FALSE);
	}

	/* Map the EDMA registers */
	if (ddi_regs_map_setup(dip, MARVELL_BASE_REG,
	    (caddr_t *)&mv_port_state->edma_regs,
	    edma_offset, EDMA_REGS_LEN, &dev_attr,
	    &mv_port_state->edma_handle) != DDI_SUCCESS) {
		if (mv_port_state->bridge_handle != NULL) {
			ddi_regs_map_free(&mv_port_state->bridge_handle);
			mv_port_state->bridge_handle = NULL;
		cmn_err(CE_WARN, "mv_map_port_registers: could not map"
		    " EDMA registers for port %d\n",
		    mv_port_state->port_num);
		}
		return (B_FALSE);
	}

	/* Map the task file 1 registers (for PATA this would be IOADDR1) */
	if (ddi_regs_map_setup(dip, MARVELL_BASE_REG,
	    (caddr_t *)&mv_port_state->task_file1_regs,
	    task_file_1_offset, EDMA_TASK_FILE_LEN, &dev_attr,
	    &mv_port_state->task_file1_handle) != DDI_SUCCESS) {

		cmn_err(CE_WARN, "mv_map_port_registers: could not map"
		    " Task File 1  registers for port %d\n",
		    mv_port_state->port_num);

		if (mv_port_state->bridge_handle != NULL) {
			ddi_regs_map_free(&mv_port_state->bridge_handle);
			mv_port_state->bridge_handle = NULL;
		}
		if (mv_port_state->edma_handle != NULL) {
			ddi_regs_map_free(&mv_port_state->edma_handle);
			mv_port_state->edma_handle = NULL;
		}
		return (B_FALSE);
	}

	/* Map the task file 2 registers (for PATA this would be IOADDR2) */
	if (ddi_regs_map_setup(dip, MARVELL_BASE_REG,
	    (caddr_t *)&mv_port_state->task_file2_regs,
	    task_file_2_offset, EDMA_TASK_FILE_CONTROL_STATUS_LEN,
	    &dev_attr, &mv_port_state->task_file2_handle)
	    != DDI_SUCCESS) {
		cmn_err(CE_WARN, "mv_map_port_registers: could not map"
		    " Task File 2  registers for port %d\n",
		    mv_port_state->port_num);
		if (mv_port_state->bridge_handle != NULL) {
			ddi_regs_map_free(&mv_port_state->bridge_handle);
			mv_port_state->bridge_handle = NULL;
		}
		if (mv_port_state->edma_handle != NULL) {
			ddi_regs_map_free(&mv_port_state->edma_handle);
			mv_port_state->edma_handle = NULL;
		}
		if (mv_port_state->task_file1_handle != NULL) {
		ddi_regs_map_free(&mv_port_state->task_file1_handle);
		mv_port_state->task_file1_handle = NULL;
		}
		return (B_FALSE);
	}

	return (B_TRUE);

}


/*
 * mv_map_subctrl_registers() maps per controller registers and
 * calls mv_map_port_registers to map all the per por registers
 */

static boolean_t
mv_map_subctrl_registers(
	dev_info_t *dip,
	struct mv_subctlr *mv_subctl,
	unsigned int ctrl_offset)
{
	int port;
	int instance = ddi_get_instance(dip);
	mv_ctl_t *mv_ctlp = ddi_get_soft_state(mv_state, instance);

	/* First map all the per (sub) controller registers */
	if (ddi_regs_map_setup(dip, MARVELL_BASE_REG,
	    (caddr_t *)&mv_subctl->mv_ctrl_regs,
	    ctrl_offset, SATAHC_REGS_LEN, &dev_attr,
	    &mv_subctl->mv_ctrl_handle) != DDI_SUCCESS) {
		cmn_err(CE_WARN, "mv_map_subctrl_registers: could not map"
		    " Sata Host Controller registers\n");

		return (B_FALSE);
	}

	/* Now map per port registers */
	for (port = 1; port <= MV_NUM_PORTS_PER_SUBCTRLR; ++port) {
		unsigned int bridge_offset;

		bridge_offset = (mv_ctlp->mv_model == MV_MODEL_50XX) ?
		    (port * SATA_BRIDGE_PORT_OFFSET) :
		    (SATA_INTERFACE_BASE_OFFSET +
		    port * SATA_INTERFACE_PORT_MULTIPLIER);

		if (! mv_map_port_registers(dip,
		    mv_subctl->mv_port_state[port - 1],
		    ctrl_offset + bridge_offset,
		    ctrl_offset + (port * EDMA_BASE_OFFSET),
		    ctrl_offset +
		    port * EDMA_BASE_OFFSET + EDMA_TASK_FILE_OFFSET,
		    ctrl_offset + port * EDMA_BASE_OFFSET +
		    EDMA_TASK_FILE_CONTROL_STATUS_OFFSET)) {
			goto port_map_failed;
		}
	}

	return (B_TRUE);

port_map_failed:
	for (; port > 1; --port)
		mv_unmap_port_registers(mv_subctl->mv_port_state[port - 1]);

	ddi_regs_map_free(&mv_subctl->mv_ctrl_handle);

	return (B_FALSE);
}


/*
 * mv_unmap_port_register() un maps per port registers.
 */

static void
mv_unmap_port_registers(struct mv_port_state *mv_port_state_ptr)
{
	/* Unmap the bridge registers */
	if (mv_port_state_ptr->bridge_handle != NULL) {
		ddi_regs_map_free(&mv_port_state_ptr->bridge_handle);
		mv_port_state_ptr->bridge_handle = NULL;
	}

	/* Unmap the EDMA registers */
	if (mv_port_state_ptr->edma_handle != NULL) {
		ddi_regs_map_free(&mv_port_state_ptr->edma_handle);
		mv_port_state_ptr->edma_handle = NULL;
	}

	/* Unmap the task file 1 registers (for PATA this would be IOADDR1) */
	if (mv_port_state_ptr->task_file1_handle != NULL) {
		ddi_regs_map_free(&mv_port_state_ptr->task_file1_handle);
		mv_port_state_ptr->task_file1_handle = NULL;
	}

	/* Unmap the task file 2 registers (for PATA this would be IOADDR2) */
	if (mv_port_state_ptr->task_file2_handle != NULL) {
		ddi_regs_map_free(&mv_port_state_ptr->task_file2_handle);
		mv_port_state_ptr->task_file2_handle = NULL;
	}
}


/*
 * mv_map_registers() maps per chip registers and then calls
 * mv_map_subctrl_registers to map per sub controller registers
 */

static boolean_t
mv_map_registers(mv_ctl_t *mv_ctlp)
{
	int	subctrl;
	int	port;
	static unsigned int ctrl_offsets[MV_MAX_NUM_SUBCTRLR] = {
		SATAHC0_BASE_OFFSET,
		SATAHC1_BASE_OFFSET
	};

	/* Main Cause register */
	if (ddi_regs_map_setup(mv_ctlp->mv_dip, MARVELL_BASE_REG,
	    (caddr_t *)&mv_ctlp->mv_device_main_regs,
	    DEVICE_MAIN_INTR_OFFSET,
	    DEVICE_MAIN_INTR_REGS_LEN,
	    &dev_attr,
	    &mv_ctlp->mv_device_main_handle) !=
	    DDI_SUCCESS) {
		cmn_err(CE_WARN, "mv_map_registers: could not map"
		    " Main Cause register\n");
		goto out;
	}
	/* PCI Interrupt Error registers */
	if (ddi_regs_map_setup(mv_ctlp->mv_dip, MARVELL_BASE_REG,
	    (caddr_t *)&mv_ctlp->mv_pci_intr_err_regs,
	    PCI_INTERRUPT_ERROR_OFFSET,
	    PCI_INTERRUPT_ERROR_LEN,
	    &dev_attr,
	    &mv_ctlp->mv_pci_intr_err_handle) !=
	    DDI_SUCCESS) {
		cmn_err(CE_WARN, "mv_map_registers: could not map"
		    " PCI Interrupt Error registers\n");
		goto out1;
	}
	ASSERT(mv_ctlp->mv_num_subctrls <= MV_MAX_NUM_SUBCTRLR);

	if (mv_ctlp->mv_model == MV_MODEL_60XX) {
		if (ddi_regs_map_setup(mv_ctlp->mv_dip, MARVELL_BASE_REG,
		    (caddr_t *)&mv_ctlp->mv_intr_coal_ext_regs,
		    INTR_COAL_EXT_OFFSET,
		    INTR_COAL_EXT_LEN,
		    &dev_attr,
		    &mv_ctlp->mv_intr_coal_ext_handle) !=
		    DDI_SUCCESS) {
			cmn_err(CE_WARN, "mv_map_registers: could not map"
			    " Interrupt coalescing external registers\n");
			goto out2;
		}
	}

	/* Now map per sub controller registers */
	for (subctrl = 0; subctrl < mv_ctlp->mv_num_subctrls; ++subctrl) {
		if (!mv_map_subctrl_registers(mv_ctlp->mv_dip,
		    mv_ctlp->mv_subctl[subctrl],
		    ctrl_offsets[subctrl])) {
			goto subctrl_map_failed;
		}
	}

	return (B_TRUE);

subctrl_map_failed:
	for (; subctrl < mv_ctlp->mv_num_subctrls; --subctrl) {
		for (port = 1; port <= MV_NUM_PORTS_PER_SUBCTRLR; ++port)
			mv_unmap_port_registers(
			    mv_ctlp->mv_subctl[subctrl]->
			    mv_port_state[port - 1]);
		ddi_regs_map_free(
		    &mv_ctlp->mv_subctl[subctrl]->mv_ctrl_handle);
	}

	ddi_regs_map_free(&mv_ctlp->mv_intr_coal_ext_handle);
out2:
	ddi_regs_map_free(&mv_ctlp->mv_pci_intr_err_handle);
out1:
	ddi_regs_map_free(&mv_ctlp->mv_device_main_handle);
out:
	return (B_FALSE);
}


/*
 * mv_init_ctrl() initializes controller & sub controllers
 */

static boolean_t
mv_init_ctrl(mv_ctl_t *mv_ctlp)
{
	int subctlr;
	int i;
	boolean_t rc;
	int port;

	/* No DMAable memory is required on a per chip basis */

	/*
	 * Initialize software data structures for each (one or two)
	 * sub controller.
	 */
	/*
	 * All resources are allocated at boot time instead of
	 * probe time because of the following issues:
	 *	- Can not mask all the interrupts. (Mask only
	 *	  effects the assertion of the interrupt pin.
	 *	  it does not affect the setting of bits in the
	 *	  device main interrupt cause register.)
	 *	- At the time of probe the SATA framework requires
	 *	  the interrupt to be on.
	 *	- At the system boot up time there is a better chance of
	 *	  being able to allocate memory than after the boot.
	 *	  (easier to get contiguous memory.)
	 */
	for (subctlr = 0; subctlr < mv_ctlp->mv_num_subctrls; ++subctlr) {
		rc = mv_init_subctrl(mv_ctlp->mv_dip,
		    mv_ctlp->mv_subctl[subctlr]);
		if (!rc)
			goto failed;
	}


	return (B_TRUE);

failed:
	for (i = --subctlr; i >= 0; i--) {
		for (port = 0; port < MV_NUM_PORTS_PER_SUBCTRLR; ++port)
			mv_free_per_port_resource(
			    mv_ctlp->mv_subctl[subctlr]->mv_port_state[port]);

	}
	return (B_FALSE);
}

/*
 * mv_init_port_regs() initializes port registers
 */

static void
mv_init_port_regs(struct mv_port_state *mv_port)
{
	uint32_t burst_info;
	uint32_t bridge_port_status;
	uint32_t det;
	uint32_t spd;
	uint32_t ipm;

	mutex_enter(&mv_port->mv_port_mutex);

	mv_save_phy_params(mv_port);

	/*
	 * Initialize the EDMA configuration
	 */
	switch (mv_port->mv_ctlp->mv_model) {
	case MV_MODEL_50XX:
		burst_info = MV_DEFAULT_BURST_50XX;
		break;
	case MV_MODEL_60XX:
		burst_info = MV_DEFAULT_BURST_60XX;
		break;
	}
	ddi_put32(mv_port->edma_handle,
	    mv_port->edma_regs + EDMA_CONFIG_OFFSET,
	    ((MV_QDEPTH - 1) << EDMA_CONFIG_DEV_Q_DEPTH_SHIFT) |
	    burst_info |
	    (0 << EDMA_CONFIG_Q_ENABLED_SHIFT) |
	    (0 << EDMA_CONFIG_STOP_ON_ERROR_SHIFT));

	mv_port->queuing_type = MV_QUEUING_NONE;

	/*
	 * Reset the link for this port
	 * 60XX links start down, 50XX up
	 */
	mv_dev_reset(mv_port, B_FALSE, B_TRUE, "initialization");

	/*
	 * Clear all pending interrupt cause bits
	 */
	switch (mv_port->mv_ctlp->mv_model) {
	case MV_MODEL_60XX:
		ddi_put32(mv_port->bridge_handle,
		    mv_port->bridge_regs +
		    SATA_SERROR_BRIDGE_OFFSET, ~0);
		break;
	case MV_MODEL_50XX:
		break;
	}
	ddi_put32(mv_port->edma_handle, mv_port->edma_regs +
	    EDMA_INTERRUPT_ERROR_CAUSE_OFFSET, 0);

	ASSERT((mv_port->mv_ctlp->mv_model == MV_MODEL_50XX) ||
	    (mv_port->mv_ctlp->mv_model == MV_MODEL_60XX));

	/*
	 * Unmask all interrupt cause bits
	 */
	switch (mv_port->mv_ctlp->mv_model) {
	case MV_MODEL_50XX:
		ddi_put32(mv_port->edma_handle, mv_port->edma_regs +
		    EDMA_INTERRUPT_ERROR_MASK_OFFSET,
		    EDMA_INTR_ERROR_MASK_50XX);
		break;
	case MV_MODEL_60XX:
		ddi_put32(mv_port->edma_handle, mv_port->edma_regs +
		    EDMA_INTERRUPT_ERROR_MASK_OFFSET,
		    EDMA_INTR_ERROR_MASK_60XX);
		break;
	}


	/*
	 * Set the EDMA request queue base address high register
	 */
#if ! defined(__lock_lint)
	ddi_put32(mv_port->edma_handle, mv_port->edma_regs +
	    EDMA_REQUEST_Q_BASE_HIGH_OFFSET, mv_port->crqq_cookie.dmac_laddress
	    >> 32);
#endif

	/*
	 * Set the EDMA request queue base address low register
	 * This also sets the EDMA Request Queue In-Pointer to 0.
	 */
	ddi_put32(mv_port->edma_handle, mv_port->edma_regs +
	    EDMA_REQUEST_Q_IN_PTR_OFFSET,
	    (uint32_t)mv_port->crqq_cookie.dmac_laddress);
	mv_port->req_q_in_register =
	    (uint32_t)mv_port->crqq_cookie.dmac_laddress;

	/*
	 * Set the EDMA Request Queue Out-Pointer to 0.
	 */
	ddi_put32(mv_port->edma_handle, mv_port->edma_regs +
	    EDMA_REQUEST_Q_OUT_PTR_OFFSET, 0);

	/*
	 * Set the edma response queue base address high register
	 */
#if ! defined(__lock_lint)
	ddi_put32(mv_port->edma_handle, mv_port->edma_regs +
	    EDMA_RESPONSE_Q_BASE_HIGH_OFFSET,
	    mv_port->crsq_cookie.dmac_laddress >> 32);
#endif

	/*
	 * Set the EDMA Response Queue In-Pointer to 0.
	 */
	ddi_put32(mv_port->edma_handle, mv_port->edma_regs +
	    EDMA_RESPONSE_Q_IN_PTR_OFFSET, 0);

	/*
	 * Set the edma response q base address low register
	 * This also sets the EDMA Response Queue Out-Pointer to 0.
	 */
	ddi_put32(mv_port->edma_handle, mv_port->edma_regs +
	    EDMA_RESPONSE_Q_OUT_PTR_OFFSET,
	    (uint32_t)mv_port->crsq_cookie.dmac_laddress);
	mv_port->resp_q_out_register =
	    (uint32_t)mv_port->crsq_cookie.dmac_laddress;

	/*
	 * No need to run these instructions if
	 * mv_debug_flags is not set.
	 */
	if (mv_debug_flags & MV_DBG_GEN) {
		bridge_port_status = ddi_get32(mv_port->bridge_handle,
		    mv_port->bridge_regs + SATA_STATUS_BRIDGE_OFFSET);

		mutex_exit(&mv_port->mv_port_mutex);

		det = (bridge_port_status & STATUS_BRIDGE_PORT_DET) >>
		    STATUS_BRIDGE_PORT_DET_SHIFT;

		spd = (bridge_port_status & STATUS_BRIDGE_PORT_SPD) >>
		    STATUS_BRIDGE_PORT_SPD_SHIFT;

		ipm = (bridge_port_status & STATUS_BRIDGE_PORT_IPM) >>
		    STATUS_BRIDGE_PORT_IPM_SHIFT;

		MV_DEBUG_MSG(mv_port->mv_ctlp->mv_dip, MV_DBG_GEN,
		    ("marvell88sx%d: SATA port %d: %s -  %s - %s"),
		    ddi_get_instance(mv_port->mv_ctlp->mv_dip),
		    mv_port->port_num, detect_msgs[det], speed_msgs[spd],
		    power_msgs[ipm]);
	} else {
		mutex_exit(&mv_port->mv_port_mutex);
	}
}

/*
 * mv_init_subctrl_regs() initializes the sub controller
 * registers and then calls mv_init_port_registers() to
 * initializes per port registers
 */

static void
mv_init_subctrl_regs(struct mv_subctlr *mv_subctlr)
{
	int port;

	/*
	 * Do not coalesce interrupts for the time being.
	 * No coalescing based upon count.
	 */
	ddi_put32(mv_subctlr->mv_ctrl_handle, mv_subctlr->mv_ctrl_regs
	    + SATAHC_INTRC_THRESHOLD_OFFSET, 0);

	/*
	 * No coalescing based on time.
	 */
	ddi_put32(mv_subctlr->mv_ctrl_handle, mv_subctlr->mv_ctrl_regs
	    + SATAHC_INTRT_THRESHOLD_OFFSET, 0);

	/*
	 * Now clear any pending interrupt causes.
	 */
	ddi_put32(mv_subctlr->mv_ctrl_handle, mv_subctlr->mv_ctrl_regs +
	    SATAHC_INTR_CAUSE_OFFSET, 0);

	/* Initialize port registers */
	for (port = 0; port < MV_NUM_PORTS_PER_SUBCTRLR; ++port) {
		mv_init_port_regs(mv_subctlr->mv_port_state[port]);
	}
}

/*
 * mv_init_ctrl_regs() goes through the sub controllers and
 * initialized them by calling mv_init_subctrl_regs()
 */

static void
mv_init_ctrl_regs(mv_ctl_t *mv_ctlp)
{
	int subctrl;

	/*
	 * Mask all interrupts for the time being
	 */
	ddi_put32(mv_ctlp->mv_device_main_handle,
	    mv_ctlp->mv_device_main_regs +
	    DEVICE_MAIN_INTR_MASK_OFFSET, 0);

	/* Clear any leftover PCI error interrupts */
	(void) ddi_put32(mv_ctlp->mv_pci_intr_err_handle,
	    mv_ctlp->mv_pci_intr_err_regs + PCI_INTERRUPT_CAUSE_OFFSET, 0);

	/* Enable any future PCI error interrupts */
	(void) ddi_put32(mv_ctlp->mv_pci_intr_err_handle,
	    mv_ctlp->mv_pci_intr_err_regs + PCI_INTERRUPT_MASK_OFFSET,
	    MV_ENABLE_ALL_PCI_INTR_MASK);

	/* Unlatch any previous PCI command error */
	(void) ddi_get32(mv_ctlp->mv_pci_intr_err_handle,
	    mv_ctlp->mv_pci_intr_err_regs + PCI_ERROR_COMMAND_OFFSET);

	/* Unlatch any previous PCI error attributes */
	(void) ddi_get32(mv_ctlp->mv_pci_intr_err_handle,
	    mv_ctlp->mv_pci_intr_err_regs + PCI_ERROR_ATTRIBUTE_OFFSET);

	/* Unlatch any previous PCI error address */
	(void) ddi_get32(mv_ctlp->mv_pci_intr_err_handle,
	    mv_ctlp->mv_pci_intr_err_regs + PCI_ERROR_ADDRESS_HIGH_OFFSET);
	(void) ddi_get32(mv_ctlp->mv_pci_intr_err_handle,
	    mv_ctlp->mv_pci_intr_err_regs + PCI_ERROR_ADDRESS_LOW_OFFSET);

	/*
	 * Now initialize each, one or two, sub controller
	 */
	for (subctrl = 0; subctrl < mv_ctlp->mv_num_subctrls; subctrl++) {
		mv_init_subctrl_regs(mv_ctlp->mv_subctl[subctrl]);
	}

	/* This should clear chip level interrupts */
	if (mv_ctlp->mv_model == MV_MODEL_60XX) {
		mv_coals_intr(mv_ctlp);
		mv_sata_io_trans_intr(mv_ctlp, trans_low);
		mv_sata_io_trans_intr(mv_ctlp, trans_high);
	}
}


/*
 * mv_init_subctrl() initializes sub controller & ports
 */

static boolean_t
mv_init_subctrl(dev_info_t *dip, struct mv_subctlr *mv_subctlr)
{
	mv_ctl_t *mv_ctlp;
	int port;
	boolean_t rc;

	mv_ctlp = ddi_get_soft_state(mv_state, ddi_get_instance(dip));

	mutex_init(&mv_subctlr->mv_subctrl_mutex, NULL, MUTEX_DRIVER,
	    (void *)(uintptr_t)mv_ctlp->mv_intr_pri);

	/*
	 * Now initialize each port
	 */
	for (port = 0; port < MV_NUM_PORTS_PER_SUBCTRLR; ++port) {
		rc = mv_init_port(dip, mv_subctlr->mv_port_state[port]);
		if (!rc)
			goto failed;
	}

	return (B_TRUE);

failed:
	mutex_destroy(&mv_subctlr->mv_subctrl_mutex);

	while (--port >= 0) {
		mv_free_per_port_resource(mv_subctlr->mv_port_state[port]);
	}

	return (B_FALSE);
}

/*
 * mv_free_per_port_resource() frees up per port resources
 */

static void
mv_free_per_port_resource(struct mv_port_state	*mv_port)
{
	int queue_entry;

	for (queue_entry = 0; queue_entry < MV_QDEPTH; ++queue_entry) {
		(void) ddi_dma_unbind_handle(
		    mv_port->eprd_dma_handle[queue_entry]);
		(void) ddi_dma_mem_free(&mv_port->eprd_acc_handle[queue_entry]);
		ddi_dma_free_handle(&mv_port->eprd_dma_handle[queue_entry]);
	}

	(void) ddi_dma_mem_free(&mv_port->crsq_acc_handle);
	ddi_dma_free_handle(&mv_port->crsq_dma_handle);
	mv_port->crsq_dma_handle = NULL;
	(void) ddi_dma_mem_free(&mv_port->crqq_acc_handle);
	ddi_dma_free_handle(&mv_port->crqq_dma_handle);
	mv_port->crqq_dma_handle = NULL;
	mv_port->crqq_acc_handle = NULL;
	mv_port->crsq_acc_handle = NULL;
	mutex_destroy(&mv_port->mv_port_mutex);
}

static int mv_reset_completion_retries = MV_POLLING_RETRIES;

/*
 * mv_dev_reset() reset the device if the second argument pass to it
 * is B_TRUE and if the third argument pass to it is B_TRUE then
 * it would reset the link as well
 */
static void
mv_dev_reset(
	struct mv_port_state *mv_port,
	boolean_t hard_reset,
	boolean_t reset_link,
	char *reason)
{
	uint32_t	edma_error_mask;
	uint32_t	bridge_port_status;
	uint32_t	det;
	int		i;
	int		number_of_link_resets = 0;

	mv_disable_dma(mv_port);

	MV_MSG((mv_port->mv_ctlp->mv_dip, CE_NOTE,
	    "!marvell88sx%d: device on port %d reset: %s\n",
	    ddi_get_instance(mv_port->mv_ctlp->mv_dip),
	    mv_port->port_num, reason));

	if (hard_reset) {
		/* Mask the edma interrupts */
		edma_error_mask = ddi_get32(mv_port->edma_handle,
		    mv_port->edma_regs + EDMA_INTERRUPT_ERROR_MASK_OFFSET);

		ASSERT((mv_port->mv_ctlp->mv_model == MV_MODEL_60XX ||
		    mv_port->mv_ctlp->mv_model == MV_MODEL_50XX));

		switch (mv_port->mv_ctlp->mv_model) {
		case MV_MODEL_50XX:
			edma_error_mask &= ~EDMA_INTR_ERROR_MASK_50XX;
			break;
		case MV_MODEL_60XX:
			edma_error_mask &= ~EDMA_INTR_ERROR_MASK_60XX;
			break;
		}

		ddi_put32(mv_port->edma_handle, mv_port->edma_regs +
		    EDMA_INTERRUPT_ERROR_MASK_OFFSET, edma_error_mask);

		/* ATA device hard Reset */
		ddi_put32(mv_port->edma_handle, mv_port->edma_regs +
		    EDMA_COMMAND_OFFSET, EDMA_COMMAND_ATA_DEVICE_RESET);

		/* Wait 25useconds */
		drv_usecwait((clock_t)25);

		/* Clear ATA reset bit */
		ddi_put32(mv_port->edma_handle, mv_port->edma_regs +
		    EDMA_COMMAND_OFFSET, 0);

		/* Unmask the edma interrupts */
		switch (mv_port->mv_ctlp->mv_model) {
		case MV_MODEL_50XX:
			ddi_put32(mv_port->edma_handle, mv_port->edma_regs +
			    EDMA_INTERRUPT_ERROR_MASK_OFFSET,
			    EDMA_INTR_ERROR_MASK_50XX);
			break;
		case MV_MODEL_60XX:
			ddi_put32(mv_port->edma_handle, mv_port->edma_regs +
			    EDMA_INTERRUPT_ERROR_MASK_OFFSET,
			    EDMA_INTR_ERROR_MASK_60XX);
			break;
		}

		if (! reset_link) {
			mv_port->dev_restore_needed = B_TRUE;
			mv_restore_phy_params(mv_port);
			return;
		}
	}

	/*
	 * Erratum SATA#10 -- Wrong Default Value to PHY Mode 4 Register
	 * Bits [1:0]
	 *
	 * Set bits 1:0 in PHY Mode 4 register to 2'b10 (bit 0 = 0, bit 1 = 1)
	 * before starting the channel.
	 */
reset_link_again:
	number_of_link_resets++;
	if (mv_port->mv_ctlp->mv_model == MV_MODEL_60XX) {
		uint32_t mode_4_reg;
		mode_4_reg = ddi_get32(mv_port->bridge_handle,
		    mv_port->bridge_regs +
		    SATA_INTERFACE_PHY_MODE_4_OFFSET);
		mode_4_reg  &= ~0x3;
		mode_4_reg  |= 0x2;
		ddi_put32(mv_port->bridge_handle,
		    mv_port->bridge_regs + SATA_INTERFACE_PHY_MODE_4_OFFSET,
		    mode_4_reg);
	}

	/* Reset the link, set no speed restrictions & pwr mgmt off */
	ddi_put32(mv_port->bridge_handle,
	    mv_port->bridge_regs + SATA_SCONTROL_BRIDGE_OFFSET,
	    SCONTROL_INITIAL_STATE);

	/* Wait 1 millisecond */
	drv_usecwait((clock_t)1000);

	/* Clear the reset bit */
	ddi_put32(mv_port->bridge_handle,
	    mv_port->bridge_regs + SATA_SCONTROL_BRIDGE_OFFSET,
	    SCONTROL_INITIAL_STATE &
	    ~SCONTROL_BRIDGE_PORT_DET_INIT);

	/* Wait 20 milliseconds */
	drv_usecwait((clock_t)20000);

	/*
	 * Check for completion up to 200 times wait 1 millisecond
	 * between tests
	 */
	for (i = 0; i < MAX_DETECT_READS; ++i) {
		uint8_t status;
		int retries = mv_reset_completion_retries;

		/* Read the status bridge port register */
		bridge_port_status = ddi_get32(mv_port->bridge_handle,
		    mv_port->bridge_regs +
		    SATA_STATUS_BRIDGE_OFFSET);

		det = (bridge_port_status & STATUS_BRIDGE_PORT_DET) >>
		    STATUS_BRIDGE_PORT_DET_SHIFT;
		if (det == STATUS_BRIDGE_PORT_DET_NODEV) {
			mv_restore_phy_params(mv_port);
			return;
		} else if (det == STATUS_BRIDGE_PORT_DET_DEVPRE_PHYCOM) {
			status = (uint8_t)ddi_get32(
			    mv_port->task_file2_handle,
			    (uint32_t *)(mv_port->task_file2_regs
			    + AT_ALTSTATUS));

			for (retries = mv_reset_completion_retries - 1;
			    ((status & SATA_STATUS_BSY)) && (retries > 0);
			    --retries) {
				/* Wait for the device to not be busy */
				drv_usecwait(1000);
				status = (uint8_t)ddi_get32(
				    mv_port->task_file2_handle,
				    (uint32_t *)(mv_port->task_file2_regs
				    + AT_ALTSTATUS));
			}
			if (status & SATA_STATUS_BSY) {
				if (number_of_link_resets < max_link_resets)
					goto reset_link_again;
				else
					MV_DEBUG_MSG(mv_port->mv_ctlp->mv_dip,
					    MV_DBG_RESET,
					    ("marvell88sx%d: device on port %d"
					    " still busy after reset"),
					    ddi_get_instance(
					    mv_port->mv_ctlp->mv_dip),
					    mv_port->port_num);
			} else {
				status = (uint8_t)ddi_get32(
				    mv_port->task_file2_handle,
				    (uint32_t *)
				    (mv_port->task_file2_regs
				    + AT_ALTSTATUS));
				for (retries = mv_reset_completion_retries - 1;
				    ! (status & SATA_STATUS_DRDY) &&
				    (retries > 0); --retries) {
					/* Wait for the device to be ready */
					drv_usecwait(1000);
					status = (uint8_t)ddi_get32(
					    mv_port->task_file2_handle,
					    (uint32_t *)
					    (mv_port->task_file2_regs
					    + AT_ALTSTATUS));
				}
				if (! (status & SATA_STATUS_DRDY)) {
					if (number_of_link_resets <
					    max_link_resets)
						goto reset_link_again;
					else
						MV_DEBUG_MSG(
						    mv_port->mv_ctlp->mv_dip,
						    MV_DBG_RESET,
						    ("marvell88sx%d: device on"
						    " port %d never came ready"
						    " after reset"),
						    ddi_get_instance(
						    mv_port->mv_ctlp->mv_dip),
						    mv_port->port_num);
				}
			}
			if ((status & SATA_STATUS_BSY) ||
			    (! (status & SATA_STATUS_DRDY))) {
				cmn_err(CE_WARN, "marvell88sx%d:device on port"
				    " %d failed to reset\n",
				    ddi_get_instance(mv_port->mv_ctlp->mv_dip),
				    mv_port->port_num);

				MV_DEBUG_MSG(mv_port->mv_ctlp->mv_dip,
				    MV_DBG_GEN, ("marvell88sx%d:"
				    " device on port %d - %s\n"),
				    ddi_get_instance(mv_port->mv_ctlp->mv_dip),
				    mv_port->port_num, detect_msgs[det]);
			}
			if (hard_reset) {
				mv_port->dev_restore_needed = B_TRUE;
				mv_port_state_change(mv_port,
				    SATA_EVNT_DEVICE_RESET,
				    SATA_ADDR_DCPORT,
				    SATA_DSTATE_RESET);
			}
			mv_restore_phy_params(mv_port);
			return;
		} else {
			drv_usecwait((clock_t)1000);
		}
	}
	if (number_of_link_resets < max_link_resets)
		goto reset_link_again;

	mv_restore_phy_params(mv_port);

	cmn_err(CE_WARN, "marvell88sx%d:device on port %d failed to reset\n",
	    ddi_get_instance(mv_port->mv_ctlp->mv_dip), mv_port->port_num);

	MV_DEBUG_MSG(mv_port->mv_ctlp->mv_dip, MV_DBG_GEN,
	    ("marvell88sx%d: device on port %d - %s\n"),
	    ddi_get_instance(mv_port->mv_ctlp->mv_dip), mv_port->port_num,
	    detect_msgs[det]);
}


/*
 * mv_init_port() initialized the port
 */

static boolean_t
mv_init_port(dev_info_t	*dip, struct mv_port_state *mv_port)
{
	int rc;
	size_t real_dma_length;
	unsigned int num_cookies;
	int queue_entry;
	int i = 0;
	mv_ctl_t *mv_ctlp;

	mv_ctlp = ddi_get_soft_state(mv_state, ddi_get_instance(dip));

	mv_port->mv_ctlp = mv_ctlp;

	/* Create a command request queue allocation handle */
	rc = ddi_dma_alloc_handle(dip, &dma_attr_crqq, DDI_DMA_SLEEP, 0,
	    &mv_port->crqq_dma_handle);

	if (rc != DDI_SUCCESS) {
		cmn_err(CE_WARN, "mv_init_port: unable to allocate dma handle"
		    " for command request queue for port %d\n",
		    mv_port->port_num);

		return (B_FALSE);
	}

	/* Allocate memory for the command request queue */
	rc = ddi_dma_mem_alloc(mv_port->crqq_dma_handle,
	    sizeof (cmd_req_queue_t), &dev_attr,
	    DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, (caddr_t *)&mv_port->crqq_addr,
	    &real_dma_length,
	    &mv_port->crqq_acc_handle);

	if (rc != DDI_SUCCESS) {
		cmn_err(CE_WARN, "mv_init_port: unable to allocate memory"
		    " for command request queue for port %d\n",
		    mv_port->port_num);
		ddi_dma_free_handle(&mv_port->crqq_dma_handle);
		mv_port->crqq_dma_handle = NULL;
		return (B_FALSE);
	}

	bzero((void *)mv_port->crqq_addr, sizeof (cmd_req_queue_t));

	/* Create a command response queue allocation handle */
	rc = ddi_dma_alloc_handle(dip, &dma_attr_crsq, DDI_DMA_SLEEP, 0,
	    &mv_port->crsq_dma_handle);
	if (rc != DDI_SUCCESS) {
		cmn_err(CE_WARN,
		    "mv_init_port: unable to allocate dma handle"
		    " for command response queue for port %d\n",
		    mv_port->port_num);
		(void) ddi_dma_mem_free(&mv_port->crqq_acc_handle);
		ddi_dma_free_handle(&mv_port->crqq_dma_handle);
		mv_port->crqq_dma_handle = NULL;
		mv_port->crqq_acc_handle = NULL;
		return (B_FALSE);
	}

	/* Allocate memory for the command response queue */
	rc = ddi_dma_mem_alloc(mv_port->crsq_dma_handle,
	    sizeof (cmd_resp_queue_t), &dev_attr,
	    DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    (caddr_t *)&mv_port->crsq_addr,
	    &real_dma_length,
	    &mv_port->crsq_acc_handle);

	if (rc != DDI_SUCCESS) {
		cmn_err(CE_WARN,
		    "mv_init_port: unable to allocate memory "
		    "for command response queue for port %d\n",
		    mv_port->port_num);
		(void) ddi_dma_mem_free(&mv_port->crqq_acc_handle);
		ddi_dma_free_handle(&mv_port->crqq_dma_handle);
		mv_port->crqq_dma_handle = NULL;
		mv_port->crqq_acc_handle = NULL;
		mv_port->crsq_dma_handle = NULL;
		return (B_FALSE);
	}

	bzero((void *)mv_port->crsq_addr, sizeof (cmd_resp_queue_t));

	/*
	 * Allocate the ePRDs.  mv_dma_nsegs entries per ePRD table.
	 */
	for (queue_entry = 0; queue_entry < MV_QDEPTH; ++queue_entry) {
		rc = ddi_dma_alloc_handle(dip, &dma_attr_eprd, DDI_DMA_SLEEP,
		    0,
		    &mv_port->eprd_dma_handle[queue_entry]);
		if (rc != DDI_SUCCESS) {

			cmn_err(CE_WARN, "mv_init_port: unable to allocate"
			    " handle for eprd %d port %d\n",
			    queue_entry, mv_port->port_num);
			for (i = --queue_entry; i >= 0; i--) {
				(void) ddi_dma_mem_free(
				    &mv_port->eprd_acc_handle[i]);
				ddi_dma_free_handle(
				    &mv_port->eprd_dma_handle[i]);
			}
			goto err2;
		}

		rc = ddi_dma_mem_alloc(mv_port->eprd_dma_handle[queue_entry],
		    sizeof (eprd_t) * mv_dma_nsegs, &dev_attr,
		    DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
		    (caddr_t *)&mv_port->eprd_addr[queue_entry],
		    &real_dma_length,
		    &mv_port->eprd_acc_handle[queue_entry]);
		if (rc != DDI_SUCCESS) {
			cmn_err(CE_WARN, "mv_init_port: unable to allocate"
			    " memory for eprd %d port %d\n",
			    queue_entry, mv_port->port_num);
			ddi_dma_free_handle(
			    &mv_port->eprd_dma_handle[queue_entry]);
			for (i = --queue_entry; i >= 0; i--) {
				(void) ddi_dma_mem_free(
				    &mv_port->eprd_acc_handle[i]);
				ddi_dma_free_handle(
				    &mv_port->eprd_dma_handle[i]);
			}
			goto err2;
		}
		bzero((void *)mv_port->eprd_addr[queue_entry],
		    sizeof (eprd_t) * mv_dma_nsegs);
	}
	/*
	 * Bind the ePRDs
	 */
	for (queue_entry = 0; queue_entry < MV_QDEPTH; ++queue_entry) {
		rc = ddi_dma_addr_bind_handle(
		    mv_port->eprd_dma_handle[queue_entry], NULL,
		    (caddr_t)mv_port->eprd_addr[queue_entry],
		    sizeof (eprd_t) * mv_dma_nsegs,
		    DDI_DMA_WRITE | DDI_DMA_CONSISTENT,
		    DDI_DMA_SLEEP, NULL, &mv_port->eprd_cookie[queue_entry],
		    &num_cookies);

		if (rc != DDI_DMA_MAPPED) {
			cmn_err(CE_WARN,
			    "mv_init_port: 0x%p eprd bind %d\n",
			    (void *) mv_port, rc);
			for (i = --queue_entry; i >= 0; i--) {
				(void) ddi_dma_unbind_handle(
				    mv_port->eprd_dma_handle[i]);
			}

			for (queue_entry = 0;
			    queue_entry < MV_QDEPTH;
			    ++queue_entry) {
				(void) ddi_dma_mem_free(
				    &mv_port->eprd_acc_handle[queue_entry]);
				ddi_dma_free_handle(
				    &mv_port->eprd_dma_handle[queue_entry]);
			}
			goto err2;
		}
		ASSERT(num_cookies == 1);
	}

	/* Bind the handle for the EDMA command request queue */
	rc = ddi_dma_addr_bind_handle(mv_port->crqq_dma_handle, NULL,
	    (caddr_t)mv_port->crqq_addr, sizeof (cmd_req_queue_t),
	    DDI_DMA_WRITE | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &mv_port->crqq_cookie, &num_cookies);

	if (rc != DDI_DMA_MAPPED) {
		cmn_err(CE_WARN,
		    "mv_init_port: 0x%p command request queue "
		    "bind failed: %d\n",
		    (void *) mv_port, rc);
		goto err1;
	}

	ASSERT(num_cookies == 1);
	ASSERT(((uint32_t)mv_port->crqq_cookie.dmac_laddress &
	    EDMA_REQUEST_Q_IN_PTR_MASK) == 0);

	/* Bind handle for EDMA command response queue */
	rc = ddi_dma_addr_bind_handle(
	    mv_port->crsq_dma_handle, NULL,
	    (caddr_t)mv_port->crsq_addr, sizeof (cmd_resp_queue_t),
	    DDI_DMA_WRITE | DDI_DMA_CONSISTENT,
	    DDI_DMA_SLEEP, NULL, &mv_port->crsq_cookie,
	    &num_cookies);

	if (rc != DDI_DMA_MAPPED) {
		cmn_err(CE_WARN, "mv_port_init: 0x%p "
		    "command response queue bind failed: %d\n",
		    (void *) mv_port, rc);
		(void) ddi_dma_unbind_handle(mv_port->crqq_dma_handle);
		goto err1;
	}

	ASSERT(num_cookies == 1);

	ASSERT(((uint32_t)mv_port->crsq_cookie.dmac_laddress &
	    EDMA_RESPONSE_Q_OUT_PTR_MASK) == 0);


	/* Initialize locking elements */
	mutex_init(&mv_port->mv_port_mutex, NULL, MUTEX_DRIVER,
	    (void *)(uintptr_t)mv_ctlp->mv_intr_pri);
	cv_init(&mv_port->mv_port_cv, NULL, CV_DRIVER, NULL);
	cv_init(&mv_port->mv_empty_cv, NULL, CV_DRIVER, NULL);

	mutex_enter(&mv_port->mv_port_mutex);
	mv_port->num_dmas = 0;
	mv_port->dma_in_use_map = 0;
	mv_port->dev_restore_needed = B_FALSE;

	/*
	 * Make sure the EDMA is disabled for now
	 */
	mv_disable_dma(mv_port);
	mutex_exit(&mv_port->mv_port_mutex);

	return (B_TRUE);

err1:
	for (queue_entry = 0; queue_entry < MV_QDEPTH; ++queue_entry) {
		(void) ddi_dma_unbind_handle(
		    mv_port->eprd_dma_handle[queue_entry]);
		(void) ddi_dma_mem_free(&mv_port->eprd_acc_handle[queue_entry]);
		ddi_dma_free_handle(&mv_port->eprd_dma_handle[queue_entry]);
	}
err2:
	(void) ddi_dma_mem_free(&mv_port->crsq_acc_handle);
	(void) ddi_dma_free_handle(&mv_port->crsq_dma_handle);
	mv_port->crsq_dma_handle = NULL;
	(void) ddi_dma_mem_free(&mv_port->crqq_acc_handle);
	ddi_dma_free_handle(&mv_port->crqq_dma_handle);
	mv_port->crqq_dma_handle = NULL;
	mv_port->crqq_acc_handle = NULL;
	mv_port->crsq_acc_handle = NULL;

	return (B_FALSE);
}


/*
 * mv_enable_dma() enables the EDMA and set the dma_enabled flag to B_TRUE
 */

static void
mv_enable_dma(struct mv_port_state *mv_port)
{
	ddi_put32(mv_port->edma_handle,
	    mv_port->edma_regs + EDMA_COMMAND_OFFSET,
	    EDMA_COMMAND_ENABLE);

	mv_port->dma_enabled = B_TRUE;
}

/*
 * mv_disable_dma disables DMA for a port then
 * checks to see if the dma is disabled if not it will wait and
 * retries 100 times.
 */
static void
mv_disable_dma(struct mv_port_state *mv_port)
{
	register int retries = 100;

	ddi_put32(mv_port->edma_handle,
	    mv_port->edma_regs + EDMA_COMMAND_OFFSET,
	    EDMA_COMMAND_DISABLE);

	do {
		if ((ddi_get32(mv_port->edma_handle, mv_port->edma_regs +
		    EDMA_COMMAND_OFFSET) &
		    EDMA_COMMAND_ENABLE) == 0)
			break;
		drv_usecwait(100);
	} while (retries-- > 0);
	/*
	 * We are checking to see if the hardware never disabled the EDMA.
	 * There is no way specified as to how to recover from this issue
	 * and no information has been forthcoming from Marvell.
	 * So, we log an error message and hope for the best.
	 */
	if (retries == 0) {
		cmn_err(CE_WARN, "!marvell88sx%d: EDMA never disabled",
		    ddi_get_instance(mv_port->mv_ctlp->mv_dip));
	}

	mv_port->dma_enabled = B_FALSE;

	/*
	 * Set the EDMA Response Queue In-Pointer to 0.
	 */
	ddi_put32(mv_port->edma_handle, mv_port->edma_regs +
	    EDMA_RESPONSE_Q_IN_PTR_OFFSET, 0);

	/*
	 * Set the edma response q base address low register
	 * This also sets the EDMA Response Queue Out-Pointer to 0.
	 */
	ddi_put32(mv_port->edma_handle, mv_port->edma_regs +
	    EDMA_RESPONSE_Q_OUT_PTR_OFFSET,
	    (uint32_t)mv_port->crsq_cookie.dmac_laddress);
	mv_port->resp_q_out_register =
	    (uint32_t)mv_port->crsq_cookie.dmac_laddress;

	/*
	 * Set the EDMA request queue base address low register
	 * This also sets the EDMA Request Queue In-Pointer to 0.
	 */
	ddi_put32(mv_port->edma_handle, mv_port->edma_regs +
	    EDMA_REQUEST_Q_IN_PTR_OFFSET,
	    (uint32_t)mv_port->crqq_cookie.dmac_laddress);
	mv_port->req_q_in_register =
	    (uint32_t)mv_port->crqq_cookie.dmac_laddress;

	/*
	 * Set the EDMA Request Queue Out-Pointer to 0.
	 */
	ddi_put32(mv_port->edma_handle, mv_port->edma_regs +
	    EDMA_REQUEST_Q_OUT_PTR_OFFSET, 0);

}

/*
 * mv_wait_for_io freezes any further DMA and waits for all pending
 * DMA to complete.
 */
static void
mv_wait_for_dma(struct mv_port_state *mv_port)
{
	while (mv_port->num_dmas != 0) {
		mv_port->dma_frozen = B_TRUE;
		cv_wait(&mv_port->mv_empty_cv, &mv_port->mv_port_mutex);
	}
}


/*
 * mv_reset_link resets the link and it will also reset the device if the
 * second argument passed to it is set to B_TRUE by calling mv_dev_reset()
 * it also fills out the sata_device_t structure that is passed to it as
 * a second argument.
 */

static boolean_t
mv_reset_link(
	struct mv_port_state *mv_port,
	sata_device_t *device,
	boolean_t reset_device)
{
	uint32_t bridge_port_status;
	uint32_t bridge_port_serror;
	uint32_t bridge_port_control;
	uint32_t det;
#if defined(MV_DEBUG)
	uint32_t spd, ipm;
#endif

	if (reset_device)
		mv_abort_all_pkts(mv_port, SATA_PKT_RESET);
	mv_dev_reset(mv_port, reset_device, B_TRUE, "link reset");

	/* Read the status bridge port register */
	bridge_port_status = ddi_get32(mv_port->bridge_handle,
	    mv_port->bridge_regs + SATA_STATUS_BRIDGE_OFFSET);

	/* Read the serror bridge port register */
	bridge_port_serror = ddi_get32(mv_port->bridge_handle,
	    mv_port->bridge_regs + SATA_SERROR_BRIDGE_OFFSET);
	/* Read the control bridge port register */
	bridge_port_control = ddi_get32(mv_port->bridge_handle,
	    mv_port->bridge_regs + SATA_SCONTROL_BRIDGE_OFFSET);

	device->satadev_scr.sstatus = bridge_port_status;
	device->satadev_scr.serror = bridge_port_serror;
	device->satadev_scr.scontrol = bridge_port_control;
	/*
	 * When NCQ and PM is implemented in phase 2 sactive field and
	 * snotific fields need to be updated. for 60X1.
	 */
	device->satadev_scr.sactive = 0;
	device->satadev_scr.snotific = 0;

	det = (bridge_port_status & STATUS_BRIDGE_PORT_DET)
	    >> STATUS_BRIDGE_PORT_DET_SHIFT;

#if defined(MV_DEBUG)
	spd = (bridge_port_status & STATUS_BRIDGE_PORT_SPD) >>
	    STATUS_BRIDGE_PORT_SPD_SHIFT;

	ipm = (bridge_port_status & STATUS_BRIDGE_PORT_IPM) >>
	    STATUS_BRIDGE_PORT_IPM_SHIFT;

	MV_DEBUG_MSG(mv_port->mv_ctlp->mv_dip, MV_DBG_GEN,
	    ("marvell88sx%d: (reset link) "
	    "port %d: %s -  %s - %s"),
	    ddi_get_instance(mv_port->mv_ctlp->mv_dip), mv_port->port_num,
	    detect_msgs[det], speed_msgs[spd], power_msgs[ipm]);
#endif

	return (det == STATUS_BRIDGE_PORT_DET_DEVPRE_PHYCOM);
}


/*
 * mv_check_link() checks if a specified link is active
 * (device present and communicating)
 * It sets port sstatus, serror and scontrol, and returns B_TRUE if link
 * is active, B_FALSE if it is not active.
 */

static boolean_t
mv_check_link(struct mv_port_state *mv_port, sata_device_t *device)
{
	uint32_t	bridge_port_status;
	uint32_t	bridge_port_serror;
	uint32_t	bridge_port_control;
	uint32_t	det;
#if defined(MV_DEBUG)
	uint32_t	spd;
	uint32_t	ipm;
#endif
	uint32_t	phy_port_control;
#if ! defined(NO_MAGIC)
	/*
	 * This register is mentioned in Marvell's
	 * Functional Errata and Restrictions
	 *	88SX5040, 88SX5041, 88SX5080, and 88SX5081 Devices
	 * dated July 8, 2004.
	 *
	 * FEr SATA#12 "ATA IDLE IMMEDIATE Command Does Not Work Properly
	 *
	 * This register is otherwise undocumented.
	 */
	uint32_t	magic_value;

	if (mv_port->mv_ctlp->mv_set_magic) {
		magic_value = ddi_get32(mv_port->bridge_handle,
		    mv_port->bridge_regs + SATA_MAGIC_BRIDGE_OFFSET);
		magic_value |= (1 << 19);	/* Set magic bit */
		ddi_put32(mv_port->bridge_handle,
		    mv_port->bridge_regs + SATA_MAGIC_BRIDGE_OFFSET,
		    magic_value);
		magic_value = ddi_get32(mv_port->bridge_handle,
		    mv_port->bridge_regs + SATA_MAGIC_BRIDGE_OFFSET);
	}
#endif

	if (mv_port->mv_ctlp->mv_model == MV_MODEL_50XX) {
		/* Set the squelch level appropriately */
		phy_port_control = ddi_get32(mv_port->bridge_handle,
		    mv_port->bridge_regs + SATA_PHY_CONTROL_BRIDGE_OFFSET);

		phy_port_control &= ~PHY_CONTROL_BRIDGE_PORT_SQ_MASK;
		if (mv_port->mv_ctlp->mv_set_squelch)
			phy_port_control |= PHY_CONTROL_BRIDGE_PORT_SQ_150MV;
		else
			phy_port_control |= PHY_CONTROL_BRIDGE_PORT_SQ_100MV;

		ddi_put32(mv_port->bridge_handle,
		    mv_port->bridge_regs + SATA_PHY_CONTROL_BRIDGE_OFFSET,
		    phy_port_control);
	}

	/* Read the status bridge port register */
	bridge_port_status = ddi_get32(mv_port->bridge_handle,
	    mv_port->bridge_regs + SATA_STATUS_BRIDGE_OFFSET);
	/* Read the serror bridge port register */
	bridge_port_serror = ddi_get32(mv_port->bridge_handle,
	    mv_port->bridge_regs + SATA_SERROR_BRIDGE_OFFSET);
	/* Read the control bridge port register */
	bridge_port_control = ddi_get32(mv_port->bridge_handle,
	    mv_port->bridge_regs + SATA_SCONTROL_BRIDGE_OFFSET);

	device->satadev_scr.sstatus = bridge_port_status;
	device->satadev_scr.serror = bridge_port_serror;
	device->satadev_scr.scontrol = bridge_port_control;
	/*
	 * When NCQ and PM is implemented in phase 2 sactive field and
	 * snotific fields need to be updated. for 60X1.
	 */
	device->satadev_scr.sactive = 0;
	device->satadev_scr.snotific = 0;

	det = (bridge_port_status & STATUS_BRIDGE_PORT_DET) >>
	    STATUS_BRIDGE_PORT_DET_SHIFT;

#if defined(MV_DEBUG)
	spd = (bridge_port_status & STATUS_BRIDGE_PORT_SPD) >>
	    STATUS_BRIDGE_PORT_SPD_SHIFT;

	ipm = (bridge_port_status & STATUS_BRIDGE_PORT_IPM) >>
	    STATUS_BRIDGE_PORT_IPM_SHIFT;

	MV_DEBUG_MSG(mv_port->mv_ctlp->mv_dip, MV_DBG_GEN,
	    ("marvell88sx%d: (check link) "
	    "port %d: %s -  %s - %s"),
	    ddi_get_instance(mv_port->mv_ctlp->mv_dip), mv_port->port_num,
	    detect_msgs[det], speed_msgs[spd], power_msgs[ipm]);
#endif

	return (det == STATUS_BRIDGE_PORT_DET_DEVPRE_PHYCOM);
}

/*
 * mv_flush_pending_io() removes the sata packet for the port's waiting
 * for completion queue and invokes the appropriate completion action for
 * the packet.
 */

static void
mv_flush_pending_io(
	struct mv_port_state *portp,
	sata_pkt_t **pkt_entry,
	int reason)
{
	sata_pkt_t *spkt = *pkt_entry;

	/* Deal with the SATA packet that was just completed */
	*pkt_entry = NULL;
	spkt->satapkt_reason = reason;

	if (spkt->satapkt_op_mode & SATA_OPMODE_SYNCH)
		cv_signal(&portp->mv_port_cv);
	else {
		mv_enqueue_pkt(portp->mv_ctlp, spkt);
		ddi_trigger_softintr(portp->mv_ctlp->mv_softintr_id);
	}
}


static long num_pio_cmd_timeouts = 0;
static long num_dma_cmd_timeouts = 0;

/*
 * mv_pkt_timeout() is per chip Time out function
 */

static void
mv_pkt_timeout(void *arg)
{
	mv_ctl_t *mv_ctlp = (mv_ctl_t *)arg;
	struct mv_subctlr *mv_subctl;
	struct mv_port_state *mv_portp;
	int subctlr;
	int port;
	unsigned int qentry;

	mutex_enter(&mv_ctlp->mv_ctl_mutex);

	for (subctlr = 0; subctlr < mv_ctlp->mv_num_subctrls; subctlr++) {
		mv_subctl = mv_ctlp->mv_subctl[subctlr];

		mutex_enter(&mv_subctl->mv_subctrl_mutex);
		for (port = 0; port < MV_NUM_PORTS_PER_SUBCTRLR; port++) {
			mv_portp = mv_subctl->mv_port_state[port];
			mutex_enter(&mv_portp->mv_port_mutex);
			if (mv_portp->mv_pio_pkt != NULL) {

				MV_DEBUG_MSG(mv_ctlp->mv_dip, MV_DBG_TIMEOUT,
				    ("mv_pkt_timeout: mv_pio_pkt = %p "
				    "mv_pio_pkt->satapkt_time = %d\n"),
				    (void *)mv_portp->mv_pio_pkt,
				    mv_portp->mv_pio_pkt->satapkt_time);

				if (--mv_portp->mv_pio_pkt->satapkt_time == 0) {
					++num_pio_cmd_timeouts;
					mv_abort_pkt(mv_portp,
					    &mv_portp->mv_pio_pkt);
					mv_flush_pending_io(mv_portp,
					    &mv_portp->mv_pio_pkt,
					    SATA_PKT_TIMEOUT);
					mv_dev_reset(mv_portp, B_TRUE, B_TRUE,
					    "PIO command timeout");
				}
			}
			if (mv_portp->mv_waiting_pkt != NULL &&
			    mv_portp->dma_frozen == FALSE &&
			    mv_portp->num_dmas == 0) {
				cv_broadcast(&mv_portp->mv_empty_cv);
				MV_DEBUG_MSG(mv_ctlp->mv_dip, MV_DBG_CVWAIT,
				    ("mv_pkt_timeout: excessive wait for cv "
				    "detected\n"), NULL);
			}
			for (qentry = 0; qentry < MV_QDEPTH; ++qentry) {
				if (mv_portp->mv_dma_pkts[qentry] != NULL) {

					MV_DEBUG_MSG(mv_ctlp->mv_dip,
					    MV_DBG_TIMEOUT,
					    ("mv_pkt_timeout: "
					    "mv_dma_pkts[%d]->satapkt_time = "
					    "%d\n"),
					    qentry,
					    mv_portp->mv_dma_pkts[qentry]->
					    satapkt_time);

					if (--mv_portp->mv_dma_pkts[qentry]->
					    satapkt_time == 0) {
						++num_dma_cmd_timeouts;
						mv_abort_pkt(mv_portp,
						    &mv_portp->
						    mv_dma_pkts[qentry]);
						mv_flush_pending_io(mv_portp,
						    &mv_portp->
						    mv_dma_pkts[qentry],
						    SATA_PKT_TIMEOUT);
						mv_dev_reset(mv_portp, B_TRUE,
						    B_TRUE,
						    "DMA command timeout");
					}
				}
			}

			mutex_exit(&mv_portp->mv_port_mutex);
		}
		mutex_exit(&mv_subctl->mv_subctrl_mutex);
	}
	/* Set up for checking packet timeouts once a second */
	if (!(mv_ctlp->mv_flags & MV_NO_TIMEOUTS)) {
		mv_ctlp->mv_timeout_id = timeout(mv_pkt_timeout,
		    (void *)mv_ctlp,
		    drv_usectohz(MV_MICROSECONDS_PER_SD_PKT_TICK));
	}
	else
		mv_ctlp->mv_timeout_id = 0;

	mutex_exit(&mv_ctlp->mv_ctl_mutex);
}

/*
 * mv_stepping() checks for supported stepping of the chips and
 * set flags whether the squelch or magic bit needs to be set.
 * (Erratas).
 */


static boolean_t
mv_stepping(dev_info_t *dip, mv_ctl_t *mv_ctlp)
{
	uint8_t revision;
	int instance = ddi_get_instance(dip);
	boolean_t supported;

	struct stepping_info {
		const char *name;
		boolean_t supported;
	};
	const struct stepping_info mv_50xx_steppings[] = {
		{"B0",		B_TRUE},	/* 0 */
		{"unknown",	B_FALSE},	/* 1 */
		{"B1",		B_TRUE},	/* 2 */
		{"B2",		B_TRUE}		/* 3 */
	};
	const struct stepping_info mv_60xx_steppings[] = {
		{"A0",		B_FALSE},	/* 0 */
		{"A1",		B_FALSE},	/* 1 */
		{"B0",		B_FALSE},	/* 2 */
		{"B1",		B_FALSE},	/* 3 */
		{"unknown",	B_FALSE},	/* 4 */
		{"unknown",	B_FALSE},	/* 5 */
		{"unknown",	B_FALSE},	/* 6 */
		{"B2",		B_FALSE},	/* 7 */
		{"unknown",	B_FALSE},	/* 8 */
		{"C0",		B_TRUE}		/* 9 */
	};
	const struct stepping_info mv_unknown_steppings[] = {
		{"unknown",	B_FALSE}
	};
	uint8_t num_stepping;
	const struct stepping_info *steppings;

	const char *mv_models[] = {
		"unknown",	/* 0 */
		"88SX50xx",	/* 1 */
		"88SX60xx"	/* 2 */
	};

	if (pci_config_setup(dip, &mv_ctlp->mv_config_handle) != DDI_SUCCESS) {
		MV_DEBUG_MSG(dip, MV_DBG_GEN,
		    ("marvell88sx%d: pci_config_setup failed\n"), instance);

		return (B_FALSE);
	}

	revision = pci_config_get8(mv_ctlp->mv_config_handle, PCI_CONF_REVID);

	switch (mv_ctlp->mv_model) {
	case MV_MODEL_50XX:
		steppings = mv_50xx_steppings;
		num_stepping = sizeof (mv_50xx_steppings) /
		    sizeof (mv_50xx_steppings[0]);
		mv_ctlp->mv_set_squelch = (revision == 0);
		mv_ctlp->mv_set_magic = (revision == 0);
		break;
	case MV_MODEL_60XX:
		steppings = mv_60xx_steppings;
		num_stepping = sizeof (mv_60xx_steppings) /
		    sizeof (mv_60xx_steppings[0]);
		mv_ctlp->mv_set_squelch = B_FALSE;
		mv_ctlp->mv_set_magic = B_FALSE;
		break;
	case MV_MODEL_UNKNOWN:
		steppings = mv_unknown_steppings;
		num_stepping = sizeof (mv_unknown_steppings) /
		    sizeof (mv_unknown_steppings[0]);
		break;
	}

	/*
	 * If this is a stepping beyond which we know about we will
	 * issue a warning and say we support it and hope for the best.
	 */
	if (revision >= num_stepping) {
		cmn_err(CE_WARN, "marvell88sx%d: model %s unknown chip "
		    "stepping %d\n", instance, mv_models[mv_ctlp->mv_model],
		    revision);
		supported = B_TRUE;
	} else {
		/* This is a known stepping.  We support only the good ones */
		MV_DEBUG_MSG(dip, MV_DBG_GEN,
		    ("marvell88sx%d: model %s chip stepping %s\n"),
		    instance, mv_models[mv_ctlp->mv_model],
		    steppings[revision].name);
		supported = steppings[revision].supported;
	}


	pci_config_teardown(&mv_ctlp->mv_config_handle);

	return (supported);
}

/*
 * mv_pm_setup() sets up the power management.
 */

static void
mv_pm_setup(dev_info_t *dip, mv_ctl_t *mv_ctlp)
{
	ushort_t caps_ptr, cap, cap_count;
	int instance = ddi_get_instance(dip);

	char pmc_name[20];
	char *pmc[] = {
		NULL,
		"0=Off (PCI D3 State)",
		"3=On (PCI D0 State)",
		NULL
	};

	if (pci_config_setup(dip, &mv_ctlp->mv_config_handle) != DDI_SUCCESS) {
		MV_DEBUG_MSG(dip, MV_DBG_GEN,
		    ("marvell88sx%d: pci_config_setup failed\n"), instance);
		return;
	}

	/*
	 * Check if capabilities list is supported and if so,
	 * get initial capabilities pointer and clear bits 0,1.
	 */
	if (pci_config_get16(mv_ctlp->mv_config_handle, PCI_CONF_STAT)
	    & PCI_STAT_CAP) {
		caps_ptr = P2ALIGN(pci_config_get8(mv_ctlp->mv_config_handle,
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
			MV_DEBUG_MSG(dip, MV_DBG_GEN,
			    ("marvell88sx%d: too many device "
			    "capabilities.\n"), instance);
			pci_config_teardown(&mv_ctlp->mv_config_handle);
			return;
		}

		if (caps_ptr < PCI_CAP_PTR_OFF) {
			MV_DEBUG_MSG(dip, MV_DBG_GEN,
			    ("marvell88sx%d: capabilities pointer"
			    " 0x%x out of range.\n"), instance, caps_ptr);

			pci_config_teardown(&mv_ctlp->mv_config_handle);
			return;
		}

		/*
		 * Get next capability and check that it is valid.
		 * For now, we only support power management.
		 */
		cap = pci_config_get8(mv_ctlp->mv_config_handle, caps_ptr);
		switch (cap) {
			case PCI_CAP_ID_PM:
				/* power management supported */
				mv_ctlp->mv_flags |= MV_PM;
				break;

			/*
			 * Message signaled interrupts and PCI-X are both
			 * unsupported for now but supported by the chip so
			 * we don't need to keep printing out the notice.
			 */
			case PCI_CAP_ID_MSI:
			case PCI_CAP_ID_PCIX:
			case PCI_CAP_ID_MSI_X:
				break;
			default:
				MV_DEBUG_MSG(dip, MV_DBG_GEN,
				    ("marvell88sx%d: unrecognized "
				    "capability 0x%x.\n"), instance, cap);
			break;
		}

		/*
		 * Get next capabilities pointer and clear bits 0,1.
		 */
		caps_ptr = P2ALIGN(pci_config_get8(mv_ctlp->mv_config_handle,
		    (caps_ptr + PCI_CAP_NEXT_PTR)), 4);
	}


	/*
	 * Set initial power software state to ON for now, in case
	 * PM is not supported or is forced off.
	 */
	mv_ctlp->mv_power_level = PM_LEVEL_D0;

	if ((mv_ctlp->mv_flags & MV_PM) && ! mv_pm_enabled) {

		MV_DEBUG_MSG(dip, MV_DBG_GEN,
		    ("marvell88sx%d pm disabled by driver"), instance);

		mv_ctlp->mv_flags &= ~MV_PM;

		pci_config_teardown(&mv_ctlp->mv_config_handle);
		return;
	}

	/*
	 * If power management is support by this chip, create
	 * pm-components property for the power management framework
	 */
	if (mv_ctlp->mv_flags & MV_PM) {
		(void) sprintf(pmc_name, "NAME=marvell88sx%d", instance);
		pmc[0] = pmc_name;
		if (ddi_prop_update_string_array(DDI_DEV_T_NONE, dip,
		    "pm-components", pmc, 3) != DDI_PROP_SUCCESS) {
			mv_ctlp->mv_flags &= ~MV_PM;
			MV_DEBUG_MSG(dip, MV_DBG_GEN,
			    ("marvell88sx%d pm-component property "
			    "creation failed."), instance);
		}

		mv_ctlp->mv_power_level = PM_LEVEL_UNKNOWN;
		if (pm_raise_power(dip, MV_PM_COMPONENT_0,
		    PM_LEVEL_D0) == DDI_FAILURE) {
			cmn_err(CE_WARN,
			    "marvell88sx%d pm_raise_power failed.",
			    instance);
		}
	}

	pci_config_teardown(&mv_ctlp->mv_config_handle);
}

#if defined(__lock_lint)
/* ARGSUSED */
static int
mv_selftest(dev_info_t *dip, sata_device_t *device)
{
	return (SATA_SUCCESS);
}
#endif

/*
 * mv_enqueue_pkt() is called at interrupt time to create a list of
 * completed SATA packets to be processed later by a software interrupt
 * thread.
 */
static void
mv_enqueue_pkt(mv_ctl_t *mv_ctlp, sata_pkt_t *spkt)
{
	ASSERT(MUTEX_HELD(&mv_ctlp->mv_ctl_mutex));

	/* Is the queue empty? */
	if (mv_ctlp->mv_completed_head == NULL) {
		/* The new tail is the new packet */
		mv_ctlp->mv_completed_head = spkt;
	} else {
		/* Add onto the tail */
		mv_ctlp->mv_completed_tail->satapkt_hba_driver_private =
		    (void *)spkt;
	}
	/* The tail is now the new packet */
	mv_ctlp->mv_completed_tail = spkt;
	/* And this is the end of the queue */
	spkt->satapkt_hba_driver_private = NULL;
}

/*
 * mv_dequeue_pkt() is called by the software interrupt thread
 * to remove packets from the completed SATA packet queue.
 */
static sata_pkt_t *
mv_dequeue_pkt(mv_ctl_t *mv_ctlp)
{
	sata_pkt_t *spkt;

	ASSERT(MUTEX_HELD(&mv_ctlp->mv_ctl_mutex));

	/* Get the next packet */
	spkt = mv_ctlp->mv_completed_head;
	ASSERT(spkt != NULL);
	/* Move the head to the next packet in the list */
	mv_ctlp->mv_completed_head =
	    (sata_pkt_t *)spkt->satapkt_hba_driver_private;

	/* If the list is now empty make sure the tail is NULL */
	if (mv_ctlp->mv_completed_head == NULL)
		mv_ctlp->mv_completed_tail = NULL;

	/* The dequeued packet now points to nothing */
	spkt->satapkt_hba_driver_private = NULL;

	return (spkt);
}

/*
 * mv_do_completed_pkts() is a software interrupt service routine
 * that runs through a list of completed SATA packets and invokes
 * the packet completion routine outside of the main interrupt
 * threads context.
 */
static uint_t
mv_do_completed_pkts(caddr_t arg)
{
	int ret = DDI_INTR_UNCLAIMED;
	mv_ctl_t *mv_ctlp = (mv_ctl_t *)arg;
	sata_pkt_t *spkt;

	mutex_enter(&mv_ctlp->mv_ctl_mutex);
	while (mv_ctlp->mv_completed_head != NULL) {
		ret = DDI_INTR_CLAIMED;
		spkt = mv_dequeue_pkt(mv_ctlp);
		mutex_exit(&mv_ctlp->mv_ctl_mutex);
		(*spkt->satapkt_comp)(spkt);
		mutex_enter(&mv_ctlp->mv_ctl_mutex);
	}
	mutex_exit(&mv_ctlp->mv_ctl_mutex);

	return (ret);
}

/*
 * mv_copy_out_ata_regs() reads the hardware
 * ATA task file registers and puts a copy into
 * the sata_cmd_t structure where needed.
 */
static void
mv_copy_out_ata_regs(sata_cmd_t *scmd, struct mv_port_state *portp)
{
	/* Get all the needed high bytes */
	if (scmd->satacmd_flags.sata_copy_out_sec_count_msb) {
		ddi_put32(portp->task_file2_handle,
		    (uint32_t *)(portp->task_file2_regs + AT_DEVCTL),
		    SATA_DEVCTL_HOB);
		scmd->satacmd_sec_count_msb =
		    (uint8_t)ddi_get32(portp->task_file1_handle,
		    (uint32_t *)(portp->task_file1_regs + AT_COUNT));
	}
	if (scmd->satacmd_flags.sata_copy_out_lba_low_msb) {
		ddi_put32(portp->task_file2_handle,
		    (uint32_t *)(portp->task_file2_regs + AT_DEVCTL),
		    SATA_DEVCTL_HOB);
		scmd->satacmd_lba_low_msb =
		    (uint8_t)ddi_get32(portp->task_file1_handle,
		    (uint32_t *)(portp->task_file1_regs + AT_SECT));
	}
	if (scmd->satacmd_flags.sata_copy_out_lba_mid_msb) {
		ddi_put32(portp->task_file2_handle,
		    (uint32_t *)(portp->task_file2_regs + AT_DEVCTL),
		    SATA_DEVCTL_HOB);
		scmd->satacmd_lba_mid_msb =
		    (uint8_t)ddi_get32(portp->task_file1_handle,
		    (uint32_t *)(portp->task_file1_regs + AT_LCYL));
	}
	if (scmd->satacmd_flags.sata_copy_out_lba_high_msb) {
		ddi_put32(portp->task_file2_handle,
		    (uint32_t *)(portp->task_file2_regs + AT_DEVCTL),
		    SATA_DEVCTL_HOB);
		scmd->satacmd_lba_high_msb =
		    (uint8_t)ddi_get32(portp->task_file1_handle,
		    (uint32_t *)(portp->task_file1_regs + AT_HCYL));
	}

	/* Get all the needed low bytes */
	if (scmd->satacmd_flags.sata_copy_out_sec_count_lsb) {
		ddi_put32(portp->task_file2_handle,
		    (uint32_t *)(portp->task_file2_regs + AT_DEVCTL), 0);
		scmd->satacmd_sec_count_lsb =
		    (uint8_t)ddi_get32(portp->task_file1_handle,
		    (uint32_t *)(portp->task_file1_regs + AT_COUNT));
	}
	if (scmd->satacmd_flags.sata_copy_out_lba_low_lsb) {
		ddi_put32(portp->task_file2_handle,
		    (uint32_t *)(portp->task_file2_regs + AT_DEVCTL), 0);
		scmd->satacmd_lba_low_lsb =
		    (uint8_t)ddi_get32(portp->task_file1_handle,
		    (uint32_t *)(portp->task_file1_regs + AT_SECT));
	}
	if (scmd->satacmd_flags.sata_copy_out_lba_mid_lsb) {
		ddi_put32(portp->task_file2_handle,
		    (uint32_t *)(portp->task_file2_regs + AT_DEVCTL), 0);
		scmd->satacmd_lba_mid_lsb =
		    (uint8_t)ddi_get32(portp->task_file1_handle,
		    (uint32_t *)(portp->task_file1_regs + AT_LCYL));
	}
	if (scmd->satacmd_flags.sata_copy_out_lba_high_lsb) {
		ddi_put32(portp->task_file2_handle,
		    (uint32_t *)(portp->task_file2_regs + AT_DEVCTL), 0);
		scmd->satacmd_lba_high_lsb =
		    (uint8_t)ddi_get32(portp->task_file1_handle,
		    (uint32_t *)(portp->task_file1_regs + AT_HCYL));
	}

	/* Get the device register if needed */
	if (scmd->satacmd_flags.sata_copy_out_device_reg) {
		ddi_put32(portp->task_file2_handle,
		    (uint32_t *)(portp->task_file2_regs + AT_DEVCTL), 0);
		scmd->satacmd_device_reg =
		    (uint8_t)ddi_get32(portp->task_file1_handle,
		    (uint32_t *)(portp->task_file1_regs + AT_DRVHD));
	}
	if (scmd->satacmd_flags.sata_copy_out_error_reg) {
		ddi_put32(portp->task_file2_handle,
		    (uint32_t *)(portp->task_file2_regs + AT_DEVCTL), 0);
		scmd->satacmd_error_reg =
		    (uint8_t)ddi_get32(portp->task_file1_handle,
		    (uint32_t *)(portp->task_file1_regs + AT_ERROR));
	}
}

/*
 * mv_abort_all_pkts() stops the execution of all packets
 * in progress and flushes any pending I/O.
 */
static void
mv_abort_all_pkts(struct mv_port_state *mv_port, int reason)
{
	register unsigned int qentry;

	ASSERT(MUTEX_HELD(&mv_port->mv_port_mutex));

	/* No pending packets will complete, so blow them away */
	if (mv_port->mv_pio_pkt != NULL) {
		mv_abort_pkt(mv_port, &mv_port->mv_pio_pkt);
		mv_flush_pending_io(mv_port, &mv_port->mv_pio_pkt, reason);
	}
	for (qentry = 0; qentry < MV_QDEPTH; ++qentry) {
		if (mv_port->mv_dma_pkts[qentry] != NULL) {
			mv_abort_pkt(mv_port, &mv_port->mv_dma_pkts[qentry]);
			mv_flush_pending_io(mv_port,
			    &mv_port->mv_dma_pkts[qentry], reason);
		}
	}

	ASSERT(mv_port->num_dmas == 0);
	/*
	 * Make sure that even if there were no packets to be aborted, we
	 * wake up anyone waiting for dma to complete.
	 */
	if (mv_port->dma_frozen)
		cv_broadcast(&mv_port->mv_empty_cv);
}

/*
 * Used to save away into the port structure the PHY paramters to be
 * restored later by mv_restore_phy_params()
 */
static void
mv_save_phy_params(struct mv_port_state *portp)
{
	uint32_t phy_params;

	switch (portp->mv_ctlp->mv_model) {
	case MV_MODEL_50XX:
		phy_params = ddi_get32(portp->bridge_handle,
		    portp->bridge_regs + SATA_PHY_MODE_BRIDGE_OFFSET);
		portp->pre_emphasis = (phy_params & PHY_MODE_BRIDGE_PORT_PRE)
		    >> PHY_MODE_BRIDGE_PORT_PRE_SHIFT;
		portp->diff_amplitude =
		    (phy_params & PHY_MODE_BRIDGE_PORT_AMP)
		    >> PHY_MODE_BRIDGE_PORT_AMP_SHIFT;
		break;
	case MV_MODEL_60XX:
		phy_params = ddi_get32(portp->bridge_handle,
		    portp->bridge_regs + SATA_INTERFACE_PHY_MODE_2_OFFSET);
		portp->pre_emphasis = (phy_params & SATA_TXPRE)
		    >> SATA_TXPRE_SHIFT;
		portp->diff_amplitude = (phy_params & SATA_TXAMPL)
		    >> SATA_TXAMPL_SHIFT;
		break;
	}
}

/*
 * Used to restore the PHY paramters that were saved earlier
 * by mv_save_phy_params()
 */
static void
mv_restore_phy_params(struct mv_port_state *portp)
{
	uint32_t phy_params;

	switch (portp->mv_ctlp->mv_model) {
	case MV_MODEL_50XX:
		/* Get the current values */
		phy_params = ddi_get32(portp->bridge_handle,
		    portp->bridge_regs + SATA_PHY_MODE_BRIDGE_OFFSET);

		/* Replace the pre and amp values */
		phy_params &=
		    ~(PHY_MODE_BRIDGE_PORT_PRE | PHY_MODE_BRIDGE_PORT_AMP);
		phy_params |=
		    portp->pre_emphasis << PHY_MODE_BRIDGE_PORT_PRE_SHIFT;
		phy_params |=
		    portp->diff_amplitude << PHY_MODE_BRIDGE_PORT_AMP_SHIFT;

		/* Set the phy values */
		ddi_put32(portp->bridge_handle,
		    portp->bridge_regs + SATA_PHY_MODE_BRIDGE_OFFSET,
		    phy_params);
		break;
	case MV_MODEL_60XX:
		/* Get the current values */
		phy_params = ddi_get32(portp->bridge_handle,
		    portp->bridge_regs + SATA_INTERFACE_PHY_MODE_2_OFFSET);

		/* Replace the pre and amp values & clear magic bit 16 */
		phy_params &= ~(SATA_TXPRE | SATA_TXAMPL | SATA_MAGIC16);
		phy_params |= portp->pre_emphasis << SATA_TXPRE_SHIFT;
		phy_params |= portp->diff_amplitude << SATA_TXAMPL_SHIFT;

		/* Set the phy values */
		ddi_put32(portp->bridge_handle,
		    portp->bridge_regs + SATA_INTERFACE_PHY_MODE_2_OFFSET,
		    phy_params);
	}
}

/*
 * Issue a READ LOG EXT command with log addresss 0x10 for NCQ recovery
 *	When an I/O error occurs while doing FPDMA the actual command
 *	that failed is not directly available.  A READ LOG EXT (0x10)
 *	PIO command must be issued to get the tag of the failed command
 *	along with the contents of the shadow registers associated
 *	with the failing command.
 *
 *	mv_ncq_error_recovery() is used to issue the READ LOG EXT command
 *	at interrupt time and polling is used to get the results.
 */

static sata_pkt_t *
mv_ncq_error_recovery(struct mv_port_state *portp)
{
	sata_cmd_t *scmd;
	struct sata_ncq_error_recovery_page ncq_err_page;
	sata_pkt_t *spkt;	/* Which NCQ packet was in error */
	uint8_t status;
	int tag;
	uint32_t estate;
	uint32_t sata_interface_status;
	uint32_t sata_interface_ctrl;
	int num_retries;


	mv_disable_dma(portp);

	estate = ddi_get32(portp->edma_handle,
	    portp->edma_regs + EDMA_STATUS_OFFSET);

	estate &= EDMA_STATUS_STATE_MASK;
	estate >>= EDMA_STATUS_STATE_SHIFT;

	/*
	 * The error recovery scheme for when the estate is
	 * the magic number 0x70 is following the work around
	 * stated in FEr SATA#25
	 */
	if (estate != 0x70) {	/* Magic wedged internal state value */
		MV_DEBUG_MSG(portp->mv_ctlp->mv_dip, MV_DBG_DEV_INTR,
		    ("mv_ncq_error_recovery: estate 0x%x\n"), estate);

		ddi_put32(portp->task_file1_handle,
		    portp->task_file1_regs + AT_COUNT, 0);

		ddi_put32(portp->task_file1_handle,
		    portp->task_file1_regs + AT_COUNT, 1);

		ddi_put32(portp->task_file1_handle,
		    portp->task_file1_regs + AT_SECT, 0);

		ddi_put32(portp->task_file1_handle,
		    portp->task_file1_regs + AT_SECT,
		    READ_LOG_EXT_NCQ_ERROR_RECOVERY);

		ddi_put32(portp->task_file1_handle,
		    portp->task_file1_regs + AT_LCYL, 0);

		ddi_put32(portp->task_file1_handle,
		    portp->task_file1_regs + AT_LCYL, 0);

		ddi_put32(portp->task_file1_handle,
		    portp->task_file1_regs + AT_DRVHD, 0);

		ddi_put32(portp->task_file1_handle,
		    portp->task_file1_regs + AT_CMD, SATAC_READ_LOG_EXT);
	} else {
		uint32_t fis_data;
		uint8_t	fis_pmport = 0;	/* Update when PM supported */

		MV_DEBUG_MSG(portp->mv_ctlp->mv_dip, MV_DBG_DEV_INTR,
		    ("mv_ncq_error_recovery: estate 0x70\n"), NULL);

		sata_interface_status = ddi_get32(portp->bridge_handle,
		    portp->bridge_regs + SATA_INTERFACE_STATUS_OFFSET);
		if (sata_interface_status & SATA_TRANS_FSM_STS) {
			mv_dev_reset(portp, B_TRUE, B_TRUE,
			    "finite state machine wedged during NCQ error");
			return (NULL);
		}
		sata_interface_ctrl = ddi_get32(portp->bridge_handle,
		    portp->bridge_regs + SATA_INTERFACE_CONTROL_OFFSET);
		sata_interface_ctrl |= SATA_INTER_VENDOR_UNIQ_MODE;
		ddi_put32(portp->bridge_handle, portp->bridge_regs
		    + SATA_INTERFACE_CONTROL_OFFSET, sata_interface_ctrl);

		fis_data = FIS_TYPE_REG_H2D
		    | ((FIS_CMD_UPDATE | fis_pmport) << 8)
		    | (SATAC_READ_LOG_EXT << 16)
		    | (0 << 24);
		ddi_put32(portp->bridge_handle, portp->bridge_regs
		    + SATA_INTERFACE_VENDOR_UNIQUE_OFFSET, fis_data);

		fis_data = READ_LOG_EXT_NCQ_ERROR_RECOVERY;
		ddi_put32(portp->bridge_handle, portp->bridge_regs
		    + SATA_INTERFACE_VENDOR_UNIQUE_OFFSET, fis_data);

		fis_data = 0;
		ddi_put32(portp->bridge_handle, portp->bridge_regs
		    + SATA_INTERFACE_VENDOR_UNIQUE_OFFSET, fis_data);

		fis_data = 1;
		ddi_put32(portp->bridge_handle, portp->bridge_regs
		    + SATA_INTERFACE_VENDOR_UNIQUE_OFFSET, fis_data);

		sata_interface_ctrl |= SATA_INTER_VENDOR_UNIQ_SEND;
		ddi_put32(portp->bridge_handle, portp->bridge_regs
		    + SATA_INTERFACE_CONTROL_OFFSET, sata_interface_ctrl);
		fis_data = 0;
		ddi_put32(portp->bridge_handle, portp->bridge_regs
		    + SATA_INTERFACE_VENDOR_UNIQUE_OFFSET, fis_data);

		for (num_retries = 200; num_retries > 0; --num_retries) {
			sata_interface_status = ddi_get32(portp->bridge_handle,
			    portp->bridge_regs
			    + SATA_INTERFACE_STATUS_OFFSET);
			if (sata_interface_status &
			    (SATA_VENDOR_UQ_DN | SATA_VENDOR_UQ_ERR)) {
				break;
			}
			drv_usecwait(1);
		}
		if ((num_retries == 0) ||
		    (sata_interface_status & SATA_VENDOR_UQ_ERR)) {
			mv_dev_reset(portp, B_TRUE, B_TRUE,
			    "vendor unique error during NCQ error");
			ddi_put32(portp->bridge_handle,
			    portp->bridge_regs +
			    SATA_INTERFACE_CONTROL_OFFSET, 0);
				return (NULL);
		}
		ddi_put32(portp->bridge_handle, portp->bridge_regs +
		    SATA_INTERFACE_CONTROL_OFFSET, 0);
	}

	/* Read the status (clear pending interrupt) */

	num_retries = 500000;	/* Try for at least 1/2 a second */
	do {
		drv_usecwait(1);
		status = (uint8_t)ddi_get32(portp->task_file1_handle,
		    (uint32_t *)(portp->task_file1_regs + AT_STATUS));
	} while ((status & SATA_STATUS_BSY) && (num_retries-- > 0));

	if (num_retries == 0) {
		MV_DEBUG_MSG(portp->mv_ctlp->mv_dip, MV_DBG_DEV_INTR,
		    ("mv_ncq_error_recovery: never went unbusy\n"), NULL);
		return (NULL);
	}

	/* Now read the data */
	ddi_rep_get16(portp->task_file1_handle,
	    (uint16_t *)&ncq_err_page,
	    (uint16_t *)(portp->task_file1_regs + AT_DATA),
	    sizeof (ncq_err_page) >> 1, DDI_DEV_NO_AUTOINCR);

	/* Get the tag field to find the correct NCQ packet */
	tag = ncq_err_page.ncq_tag;
	if (tag & NQ) {
		MV_DEBUG_MSG(portp->mv_ctlp->mv_dip, MV_DBG_DEV_INTR,
		    ("mv_ncq_error_recovery: tag & NQ is set\n"), NULL);
		return (NULL);
	} else {
		tag &= NCQ_TAG_MASK;
	}

	spkt = portp->mv_dma_pkts[tag];
	if (spkt == NULL) {
		MV_DEBUG_MSG(portp->mv_ctlp->mv_dip, MV_DBG_DEV_INTR,
		    ("mv_ncq_error_recovery: mv_dma_pkts[%d] was NULL\n"),
		    tag);
		return (NULL);
	}
	portp->mv_dma_pkts[tag] = NULL;	/* Done with this packet */
	--portp->num_dmas;
	portp->dma_in_use_map &= ~(1 << tag);
	scmd = &spkt->satapkt_cmd;

	/* Extract all the error fields from the READ LOG EXT data */
	scmd->satacmd_sec_count_msb = ncq_err_page.ncq_sector_count_ext;
	scmd->satacmd_sec_count_lsb = ncq_err_page.ncq_sector_count;
	scmd->satacmd_lba_low_msb = ncq_err_page.ncq_sector_number_ext;
	scmd->satacmd_lba_low_lsb = ncq_err_page.ncq_sector_number;
	scmd->satacmd_lba_mid_msb = ncq_err_page.ncq_cyl_low_ext;
	scmd->satacmd_lba_mid_lsb = ncq_err_page.ncq_cyl_low;
	scmd->satacmd_lba_high_msb = ncq_err_page.ncq_cyl_high_ext;
	scmd->satacmd_lba_high_lsb = ncq_err_page.ncq_cyl_high;
	scmd->satacmd_device_reg = ncq_err_page.ncq_dev_head;
	scmd->satacmd_status_reg = ncq_err_page.ncq_status;
	scmd->satacmd_error_reg = ncq_err_page.ncq_error;

	spkt->satapkt_reason = SATA_PKT_DEV_ERROR;


	return (spkt);
}


/*
 * On rare occasions, the main interrupt register is stuck indicating
 * a subcontroller interrupt when there is no such interrupt.
 * For now, just emit a warning message, but later we may want to
 * consider resetting the port.
 */
static	void
mv_stuck_intr(struct mv_ctl *mv_ctlp, uint32_t intr_cause)
{
	uint32_t intr_mask;
	int subctl, port;

	/*
	 * Determine affected subcontroller
	 */
	if (intr_cause & DEVICE_MAIN_INTR_CAUSE_SATA03_INTR)
		subctl = 0;
	else if (mv_ctlp->mv_num_subctrls == 2 &&
	    intr_cause & DEVICE_MAIN_INTR_CAUSE_SATA47_INTR)
		subctl = 1;
	else
		return;

	/* Determine affected port */
	for (port = 0; port < MV_NUM_PORTS_PER_SUBCTRLR; ++port) {

		intr_mask = (((MAIN_SATAERR | MAIN_SATADONE) <<
		    ((port * MAIN_PORT_SHIFT))) <<
		    (subctl * MAIN_SATAHC_BASE_SHIFT));

		if ((intr_cause & intr_mask) != 0) {
			MV_MSG((mv_ctlp->mv_dip, CE_WARN, "marvell88sx%d: "
			    "stuck interrupt on port %d\n",
			    ddi_get_instance(mv_ctlp->mv_dip),
			    port + (subctl * MV_NUM_PORTS_PER_SUBCTRLR)));
			MV_DEBUG_MSG(mv_ctlp->mv_dip, MV_DBG_INTRSTK,
			    ("port %d, intr_cause 0x%x, intr_mask 0x%x\n"),
			    port + (subctl * MV_NUM_PORTS_PER_SUBCTRLR),
			    intr_cause, intr_mask);
			/*
			 * Recovery may involve aborting all commands on
			 * a port and resetting the port and device.
			 * This should produce a real interrupt that
			 * would hopefully unhinge the stuck bit.
			 * If the above would not clear the stuck interrupt
			 * bit, we may want to disable the port and mask
			 * its interrupts.  For now, a warning message is
			 * emitted.  The interrupt loop is broken in the
			 * main interrupt function.
			 */
		}
	}
}

static void
mv_log(dev_info_t *dip, int level, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (mv_verbose) {
		vcmn_err(level, fmt, ap);
	}

	sata_vtrace_debug(dip, fmt, ap);
	va_end(ap);
}

#if defined(MV_DEBUG)
static void
mv_trace_debug(dev_info_t *dip, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	sata_vtrace_debug(dip, fmt, ap);
	vprintf(fmt, ap);
	va_end(ap);
}
#endif
