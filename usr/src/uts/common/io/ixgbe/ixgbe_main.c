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
 * Copyright(c) 2007-2011 Intel Corporation. All rights reserved.
 */

/*
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include "ixgbe_sw.h"

static char ixgbe_ident[] = "Intel 10Gb Ethernet";
static char ixgbe_version[] = "ixgbe 1.1.16";

/*
 * Local function protoypes
 */
static int ixgbe_register_mac(ixgbe_t *);
static int ixgbe_identify_hardware(ixgbe_t *);
static int ixgbe_regs_map(ixgbe_t *);
static void ixgbe_init_properties(ixgbe_t *);
static int ixgbe_init_driver_settings(ixgbe_t *);
static void ixgbe_init_bcm8706_leds(ixgbe_t *);
static void ixgbe_init_dcb_settings(ixgbe_t *);
static void ixgbe_init_locks(ixgbe_t *);
static void ixgbe_destroy_locks(ixgbe_t *);
static int ixgbe_init(ixgbe_t *);
static int ixgbe_chip_start(ixgbe_t *);
static void ixgbe_chip_stop(ixgbe_t *);
static int ixgbe_reset(ixgbe_t *);
static void ixgbe_tx_clean(ixgbe_t *);
static boolean_t ixgbe_tx_drain(ixgbe_t *);
static boolean_t ixgbe_rx_drain(ixgbe_t *);
static int ixgbe_alloc_rings(ixgbe_t *);
static void ixgbe_free_rings(ixgbe_t *);
static int ixgbe_alloc_rx_data(ixgbe_t *);
static void ixgbe_free_rx_data(ixgbe_t *);
static void ixgbe_setup_rx_group(ixgbe_t *);
static void ixgbe_setup_rx_global(ixgbe_t *);
static void ixgbe_setup_tx_group(ixgbe_t *);
static void ixgbe_setup_tx_global(ixgbe_t *);
static void ixgbe_setup_rx_ring(ixgbe_rx_ring_t *);
static void ixgbe_setup_tx_ring(ixgbe_tx_ring_t *);
static void ixgbe_disable_rx_ring(ixgbe_rx_ring_t *);
static void ixgbe_disable_tx_ring(ixgbe_tx_ring_t *);
static void ixgbe_setup_vlan(struct ixgbe_hw *);
static void ixgbe_setup_dcb(ixgbe_t *);
static void ixgbe_setup_rss_dcb(ixgbe_t *);
static void ixgbe_init_unicst(ixgbe_t *);
static void ixgbe_mta_set_vf(ixgbe_t *, uint32_t);
static void ixgbe_get_hw_state(ixgbe_t *);
static void ixgbe_setup_vt_rss_conf(ixgbe_t *ixgbe);
static void ixgbe_setup_rx_classify_82598eb(ixgbe_t *);
static void ixgbe_setup_rx_classify_generic(ixgbe_t *);
static void ixgbe_setup_rx_vt_82598eb(ixgbe_t *);
static void ixgbe_setup_rx_vt_generic(ixgbe_t *);
static void ixgbe_setup_rx_vt_rss_82598eb(ixgbe_t *);
static void ixgbe_setup_rx_vt_rss_generic(ixgbe_t *);
static void ixgbe_setup_tx_classify_generic(ixgbe_t *);
static void ixgbe_enable_hw_interrupts_82598eb(ixgbe_t *);
static void ixgbe_enable_hw_interrupts_generic(ixgbe_t *);
static void ixgbe_get_conf(ixgbe_t *);
static void ixgbe_init_params(ixgbe_t *);
static int ixgbe_get_prop(ixgbe_t *, char *, int, int, int);
static void ixgbe_driver_link_check(ixgbe_t *);
static void ixgbe_sfp_check(void *);
static void ixgbe_overtemp_check(void *);
static void ixgbe_link_timer(void *);
static void ixgbe_local_timer(void *);
static void ixgbe_arm_watchdog_timer(ixgbe_t *);
static void ixgbe_restart_watchdog_timer(ixgbe_t *);
static void ixgbe_disable_adapter_interrupts(ixgbe_t *);
static boolean_t ixgbe_stall_check(ixgbe_t *);
static boolean_t ixgbe_set_loopback_mode(ixgbe_t *, uint32_t);
static void ixgbe_set_internal_mac_loopback(ixgbe_t *);
static boolean_t ixgbe_find_mac_address(ixgbe_t *);
static int ixgbe_alloc_intrs(ixgbe_t *);
static int ixgbe_alloc_intr_handles(ixgbe_t *, int);
static int ixgbe_add_intr_handlers(ixgbe_t *);
static void ixgbe_map_rxring_to_vector(ixgbe_t *, int, int);
static void ixgbe_map_txring_to_vector(ixgbe_t *, int, int);
static void ixgbe_setup_ivar(ixgbe_t *, uint16_t, uint8_t, int8_t);
static void ixgbe_enable_ivar(ixgbe_t *, uint16_t, int8_t);
static void ixgbe_disable_ivar(ixgbe_t *, uint16_t, int8_t);
static void ixgbe_get_hw_rx_index(ixgbe_t *, ixgbe_rx_ring_t *, uint32_t);
static void ixgbe_get_hw_tx_index(ixgbe_t *, ixgbe_tx_ring_t *, uint32_t);
static int ixgbe_map_intrs_to_vectors(ixgbe_t *);
static void ixgbe_setup_adapter_vector(ixgbe_t *);
static void ixgbe_rem_intr_handlers(ixgbe_t *);
static void ixgbe_rem_intrs(ixgbe_t *);
static int ixgbe_enable_intrs(ixgbe_t *);
static int ixgbe_disable_intrs(ixgbe_t *);
static uint_t ixgbe_intr_legacy(void *, void *);
static uint_t ixgbe_intr_msi(void *, void *);
static uint_t ixgbe_intr_msix(void *, void *);
static void ixgbe_intr_rx_work(ixgbe_rx_ring_t *);
static void ixgbe_intr_tx_work(ixgbe_tx_ring_t *);
static void ixgbe_intr_other_work(ixgbe_t *, uint32_t);
static void ixgbe_get_driver_control(struct ixgbe_hw *);
static int ixgbe_add_mac(void *, const uint8_t *, uint64_t);
static int ixgbe_rem_mac(void *, const uint8_t *);
static int ixgbe_add_vlanfilter(void *, uint16_t, uint32_t);
static int ixgbe_rem_vlanfilter(void *, uint16_t);
static int ixgbe_add_vf_vlan(ixgbe_t *, uint16_t, uint32_t);
static int ixgbe_rem_vf_vlan(ixgbe_t *, uint16_t, uint32_t);
static int ixgbe_enable_vf_port_vlan(ixgbe_t *, uint16_t, uint32_t);
static int ixgbe_disable_vf_port_vlan(ixgbe_t *, uint16_t, uint32_t);
static int ixgbe_set_mtu(void *, uint32_t);
static int ixgbe_get_sriov_info(void *, mac_sriov_info_t *);
static void ixgbe_release_driver_control(struct ixgbe_hw *);
static void ixgbe_fcoe_enable_rxcrc(ixgbe_t *);
static void ixgbe_fcoe_enable_lro(ixgbe_t *);
static void ixgbe_fcoe_enable_rxmr(ixgbe_t *);

static int ixgbe_attach(dev_info_t *, ddi_attach_cmd_t);
static int ixgbe_detach(dev_info_t *, ddi_detach_cmd_t);
static int ixgbe_resume(dev_info_t *);
static int ixgbe_suspend(dev_info_t *);
static int ixgbe_quiesce(dev_info_t *);
static int ixgbe_unconfigure(dev_info_t *, ixgbe_t *);
static uint8_t *ixgbe_mc_table_itr(struct ixgbe_hw *, uint8_t **, uint32_t *);
static int ixgbe_cb_func(dev_info_t *, ddi_cb_action_t, void *, void *, void *);
static int ixgbe_intr_adjust(ixgbe_t *, ddi_cb_action_t, int);

static int ixgbe_fm_error_cb(dev_info_t *dip, ddi_fm_error_t *err,
    const void *impl_data);
static void ixgbe_fm_init(ixgbe_t *);
static void ixgbe_fm_fini(ixgbe_t *);

static void ixgbe_config_dcb(ixgbe_t *);
static int ixgbe_get_iov_params(ixgbe_t *);

#ifdef	DEBUG_INTEL
static int ixgbe_devmap(dev_t, devmap_cookie_t, offset_t, size_t, size_t *,
    uint_t);
#endif	/* DEBUG_INTEL */

char *ixgbe_priv_props[] = {
	"_tx_copy_thresh",
	"_tx_recycle_thresh",
	"_tx_overload_thresh",
	"_tx_resched_thresh",
	"_rx_copy_thresh",
	"_rx_limit_per_intr",
	"_rx_pfc_low",
	"_rx_pfc_high",
	"_intr_throttling",
	"_adv_pause_cap",
	"_adv_asym_pause_cap",
	NULL
};

#define	IXGBE_MAX_PRIV_PROPS \
	(sizeof (ixgbe_priv_props) / sizeof (mac_priv_prop_t))

static struct cb_ops ixgbe_cb_ops = {
	nulldev,		/* cb_open */
	nulldev,		/* cb_close */
	nodev,			/* cb_strategy */
	nodev,			/* cb_print */
	nodev,			/* cb_dump */
	nodev,			/* cb_read */
	nodev,			/* cb_write */
	nodev,			/* cb_ioctl */
#ifdef DEBUG_INTEL
	ixgbe_devmap,		/* cb_devmap */
#else
	nodev,			/* cb_devmap */
#endif	/* DEBUG_INTEL */
	nodev,			/* cb_mmap */
	nodev,			/* cb_segmap */
	nochpoll,		/* cb_chpoll */
	ddi_prop_op,		/* cb_prop_op */
	NULL,			/* cb_stream */
#ifdef DEBUG_INTEL
	D_MP | D_HOTPLUG | D_DEVMAP,	/* cb_flag */
#else
	D_MP | D_HOTPLUG,	/* cb_flag */
#endif	/* DEBUG_INTEL */
	CB_REV,			/* cb_rev */
	nodev,			/* cb_aread */
	nodev			/* cb_awrite */
};

static struct dev_ops ixgbe_dev_ops = {
	DEVO_REV,		/* devo_rev */
	0,			/* devo_refcnt */
	NULL,			/* devo_getinfo */
	nulldev,		/* devo_identify */
	nulldev,		/* devo_probe */
	ixgbe_attach,		/* devo_attach */
	ixgbe_detach,		/* devo_detach */
	nodev,			/* devo_reset */
	&ixgbe_cb_ops,		/* devo_cb_ops */
	NULL,			/* devo_bus_ops */
	nulldev,		/* devo_power */
	ixgbe_quiesce,		/* devo_quiesce */
};

static struct modldrv ixgbe_modldrv = {
	&mod_driverops,		/* Type of module.  This one is a driver */
	ixgbe_ident,		/* Discription string */
	&ixgbe_dev_ops		/* driver ops */
};

static struct modlinkage ixgbe_modlinkage = {
	MODREV_1, &ixgbe_modldrv, NULL
};

/*
 * Access attributes for register mapping
 */
ddi_device_acc_attr_t ixgbe_regs_acc_attr = {
	DDI_DEVICE_ATTR_V1,
	DDI_STRUCTURE_LE_ACC,
	DDI_STRICTORDER_ACC,
	DDI_FLAGERR_ACC
};

/*
 * Loopback property
 */
static lb_property_t lb_normal = {
	normal,	"normal", IXGBE_LB_NONE
};

static lb_property_t lb_mac = {
	internal, "MAC", IXGBE_LB_INTERNAL_MAC
};

static lb_property_t lb_external = {
	external, "External", IXGBE_LB_EXTERNAL
};

#define	IXGBE_M_CALLBACK_FLAGS \
	(MC_IOCTL | MC_GETCAPAB | MC_SETPROP | MC_GETPROP | MC_PROPINFO)

static mac_callbacks_t ixgbe_m_callbacks = {
	IXGBE_M_CALLBACK_FLAGS,
	ixgbe_m_stat,
	ixgbe_m_start,
	ixgbe_m_stop,
	ixgbe_m_promisc,
	ixgbe_m_multicst,
	NULL,
	NULL,
	NULL,
	ixgbe_m_ioctl,
	ixgbe_m_getcapab,
	NULL,
	NULL,
	ixgbe_m_setprop,
	ixgbe_m_getprop,
	ixgbe_m_propinfo
};

/*
 * Initialize capabilities of each supported adapter type
 */
static adapter_info_t ixgbe_82598eb_cap = {
	64,		/* maximum number of rx queues */
	1,		/* minimum number of rx queues */
	64,		/* default number of rx queues */

	32,		/* maximum number of tx queues */
	1,		/* minimum number of tx queues */
	8,		/* default number of tx queues */

	16,		/* maximum number of groups */
	1,		/* minimum number of groups */
	1,		/* default number of groups */

	16366,		/* maximum MTU size */

	0xFFFF,		/* maximum interrupt throttle rate */
	0,		/* minimum interrupt throttle rate */
	200,		/* default interrupt throttle rate */

	0,		/* maximum VFs supported */

	18,		/* maximum total msix vectors */
	16,		/* maximum number of ring vectors */
	2,		/* maximum number of other vectors */
	IXGBE_EICR_LSC,	/* "other" interrupt types handled */
	0,		/* "other" interrupt types enable mask */

	/* function pointers */
	ixgbe_enable_hw_interrupts_82598eb,
	ixgbe_setup_rx_classify_82598eb,
	NULL,

	(IXGBE_FLAG_DCA_CAPABLE	/* capability flags */
	| IXGBE_FLAG_RSS_CAPABLE
	| IXGBE_FLAG_VMDQ_CAPABLE)
};

/* capability for 82599 */
static adapter_info_t ixgbe_82599eb_cap = {
	128,		/* maximum number of rx queues */
	1,		/* minimum number of rx queues */
	128,		/* default number of rx queues */

	128,		/* maximum number of tx queues */
	1,		/* minimum number of tx queues */
	8,		/* default number of tx queues */

	64,		/* maximum number of groups */
	1,		/* minimum number of groups */
	1,		/* default number of groups */

	15500,		/* maximum MTU size */

	0xFF8,		/* maximum interrupt throttle rate */
	0,		/* minimum interrupt throttle rate */
	200,		/* default interrupt throttle rate */

	63,		/* maximum VFs supported */

	64,		/* maximum total msix vectors */
	16,		/* maximum number of ring vectors */
	2,		/* maximum number of other vectors */
	(IXGBE_EICR_LSC
	| IXGBE_EICR_GPI_SDP1
	| IXGBE_EICR_GPI_SDP2
	| IXGBE_EICR_MAILBOX),	/* other interrupt types */

	(IXGBE_SDP1_GPIEN
	| IXGBE_SDP2_GPIEN), /* "other" interrupt types enable mask */

	/* function pointers */
	ixgbe_enable_hw_interrupts_generic,
	ixgbe_setup_rx_classify_generic,
	ixgbe_setup_tx_classify_generic,

	(IXGBE_FLAG_DCA_CAPABLE
	| IXGBE_FLAG_RSS_CAPABLE
	| IXGBE_FLAG_VMDQ_CAPABLE
	| IXGBE_FLAG_RSC_CAPABLE
	| IXGBE_FLAG_SFP_PLUG_CAPABLE) /* capability flags */
};

/* capability for X540 */
static adapter_info_t ixgbe_X540_cap = {
	128,		/* maximum number of rx queues */
	1,		/* minimum number of rx queues */
	128,		/* default number of rx queues */

	128,		/* maximum number of tx queues */
	1,		/* minimum number of tx queues */
	8,		/* default number of tx queues */

	64,		/* maximum number of groups */
	1,		/* minimum number of groups */
	1,		/* default number of groups */

	15500,		/* maximum MTU size */

	0xFF8,		/* maximum interrupt throttle rate */
	0,		/* minimum interrupt throttle rate */
	200,		/* default interrupt throttle rate */

	0,		/* maximum VFs supported */

	64,		/* maximum total msix vectors */
	16,		/* maximum number of ring vectors */
	2,		/* maximum number of other vectors */
	(IXGBE_EICR_LSC
	| IXGBE_EICR_GPI_SDP1
	| IXGBE_EICR_GPI_SDP2), /* "other" interrupt types handled */

	(IXGBE_SDP1_GPIEN
	| IXGBE_SDP2_GPIEN), /* "other" interrupt types enable mask */

	/* function pointers */
	ixgbe_enable_hw_interrupts_generic,
	ixgbe_setup_rx_classify_generic,
	ixgbe_setup_tx_classify_generic,

	(IXGBE_FLAG_DCA_CAPABLE
	| IXGBE_FLAG_RSS_CAPABLE
	| IXGBE_FLAG_VMDQ_CAPABLE
	| IXGBE_FLAG_RSC_CAPABLE
	| IXGBE_FLAG_SFP_PLUG_CAPABLE) /* capability flags */
};

/*
 * IOV parameter table
 */
iov_param_desc_t ixgbe_iov_param_list[IXGBE_NUM_IOV_PARAMS] = {
	{
	"max-config-vfs",		/* Name of the param */
	"Max number of configurable VFs",	/* Description */
	(PCIV_DEV_PF | PCIV_READ_ONLY),	/* PF and Read-only */
	PCI_PARAM_DATA_TYPE_UINT32,	/* Data type */
	IXGBE_MAX_CONFIG_VF,		/* Default value */
	IXGBE_MAX_CONFIG_VF,		/* Minimum value */
	IXGBE_MAX_CONFIG_VF,		/* Maximum value */
	""				/* Default string */
	},
	{
	"max-vf-mtu",			/* Name of the param */
	"Max MTU supported for a VF",	/* Description */
	(PCIV_DEV_VF | PCIV_READ_ONLY),	/* VF and Read-only */
	PCI_PARAM_DATA_TYPE_UINT32,	/* Data type */
	IXGBE_MAX_VF_MTU_82599,		/* Default value */
	IXGBE_MAX_VF_MTU_82599,		/* Minimum value */
	IXGBE_MAX_VF_MTU_82599,		/* Maximum value */
	""				/* Default string */
	},
	{
	"max-vlans",			/* Name of the param */
	"Max number of VLAN filters supported",	/* Description */
	(PCIV_DEV_VF | PCIV_READ_ONLY),	/* VF and Read-only */
	PCI_PARAM_DATA_TYPE_UINT32,	/* Data type */
	IXGBE_VLVF_ENTRIES,		/* Default value */
	IXGBE_VLVF_ENTRIES,		/* Minimum value */
	IXGBE_VLVF_ENTRIES,		/* Maximum value */
	""				/* Default string */
	},
	{
	"unicast-slots",		/* Name of the param */
	"Number of unicast mac-address slots",	/* Description */
	(PCIV_DEV_PF | PCIV_DEV_VF),	/* Applicable for PF/VF */
	PCI_PARAM_DATA_TYPE_UINT32,	/* Data type */
	0,				/* Default value */
	0,				/* Minimum value */
	MAX_NUM_UNICAST_ADDRESSES,	/* Maximum value */
	""				/* Default string */
	},
	{
	"pvid-exclusive",	/* Name of the param */
	"Exclusive configuration of pvid required", /* Description */
	(PCIV_DEV_VF | PCIV_READ_ONLY),	/* VF and Read-only */
	PCI_PARAM_DATA_TYPE_UINT32,	/* Data type */
	1,				/* Default value */
	1,				/* Minimum value */
	1,				/* Maximum value */
	""				/* Default string */
	}
};

#ifdef DEBUG_INTEL
/*
 * hack for ethregs: save all dev_info_t's in a global array.  This would not
 * be necessary if I could get gld_getinfo() to work, but I can't.
 */
#define	DIP_MAX 32
dev_info_t *GlobalDip;
dev_info_t *GlobalDipArray[DIP_MAX];
#endif  /* DEBUG_INTEL */

/*
 * Module Initialization Functions.
 */

int
_init(void)
{
	int status;

	mac_init_ops(&ixgbe_dev_ops, MODULE_NAME);

	status = mod_install(&ixgbe_modlinkage);

	if (status != DDI_SUCCESS) {
		mac_fini_ops(&ixgbe_dev_ops);
	}

	return (status);
}

int
_fini(void)
{
	int status;

	status = mod_remove(&ixgbe_modlinkage);

	if (status == DDI_SUCCESS) {
		mac_fini_ops(&ixgbe_dev_ops);
	}

	return (status);
}

int
_info(struct modinfo *modinfop)
{
	int status;

	status = mod_info(&ixgbe_modlinkage, modinfop);

	return (status);
}

/*
 * ixgbe_attach - Driver attach.
 *
 * This function is the device specific initialization entry
 * point. This entry point is required and must be written.
 * The DDI_ATTACH command must be provided in the attach entry
 * point. When attach() is called with cmd set to DDI_ATTACH,
 * all normal kernel services (such as kmem_alloc(9F)) are
 * available for use by the driver.
 *
 * The attach() function will be called once for each instance
 * of  the  device  on  the  system with cmd set to DDI_ATTACH.
 * Until attach() succeeds, the only driver entry points which
 * may be called are open(9E) and getinfo(9E).
 */
static int
ixgbe_attach(dev_info_t *devinfo, ddi_attach_cmd_t cmd)
{
	ixgbe_t *ixgbe;
	struct ixgbe_osdep *osdep;
	struct ixgbe_hw *hw;
	pciv_config_vf_t vfcfg;
	int instance;
	char taskqname[32];
	int ret;

	/*
	 * Check the command and perform corresponding operations
	 */
	switch (cmd) {
	default:
		return (DDI_FAILURE);

	case DDI_RESUME:
		return (ixgbe_resume(devinfo));

	case DDI_ATTACH:
		break;
	}

	/* Get the device instance */
	instance = ddi_get_instance(devinfo);

	/* Allocate memory for the instance data structure */
	ixgbe = kmem_zalloc(sizeof (ixgbe_t), KM_SLEEP);

	ixgbe->dip = devinfo;
	ixgbe->instance = instance;

	hw = &ixgbe->hw;
	osdep = &ixgbe->osdep;
	hw->back = osdep;
	osdep->ixgbe = ixgbe;

	/* Attach the instance pointer to the dev_info data structure */
	ddi_set_driver_private(devinfo, ixgbe);

#ifdef DEBUG_INTEL
	/*
	 * hack for ethregs: save all dev_info_t's in a global array
	 */
	if (instance < DIP_MAX) {
		GlobalDipArray[instance] = devinfo;
		ixgbe_log(ixgbe, "instance %d dev_info_t 0x%p\n",
		    instance, devinfo);
	} else {
		ixgbe_log(ixgbe, "instance %d greater than %d\n",
		    instance, DIP_MAX);
	}
#endif  /* DEBUG_INTEL */

	/*
	 * Initialize for fma support
	 */
	ixgbe->fm_capabilities = ixgbe_get_prop(ixgbe, PROP_FM_CAPABLE,
	    0, 0x0f, DDI_FM_EREPORT_CAPABLE | DDI_FM_ACCCHK_CAPABLE |
	    DDI_FM_DMACHK_CAPABLE | DDI_FM_ERRCB_CAPABLE);
	ixgbe_fm_init(ixgbe);
	ixgbe->attach_progress |= ATTACH_PROGRESS_FM_INIT;

	/*
	 * Map PCI config space registers
	 */
	if (pci_config_setup(devinfo, &osdep->cfg_handle) != DDI_SUCCESS) {
		ixgbe_error(ixgbe, "Failed to map PCI configurations");
		goto attach_fail;
	}
	ixgbe->attach_progress |= ATTACH_PROGRESS_PCI_CONFIG;

	/*
	 * Identify the chipset family
	 */
	if (ixgbe_identify_hardware(ixgbe) != IXGBE_SUCCESS) {
		ixgbe_error(ixgbe, "Failed to identify hardware");
		goto attach_fail;
	}

	vfcfg.cmd = PCIV_VFCFG_PARAM;
	if (pciv_vf_config(ixgbe->dip, &vfcfg) == DDI_SUCCESS) {
		ixgbe->num_vfs = 0;
		if (vfcfg.num_vf > ixgbe->capab->max_vf) {
			ixgbe_log(ixgbe,
			    "Invalid num_vf = %d, max_vf = %d",
			    vfcfg.num_vf, ixgbe->capab->max_vf);
		} else if (vfcfg.num_vf > 0) {
			ixgbe->num_vfs = vfcfg.num_vf;
			ixgbe->sriov_pf = ixgbe->num_vfs > 0 ? B_TRUE : B_FALSE;
			ixgbe->pf_grp = ixgbe->num_vfs;
		}
	} else {
		IXGBE_DEBUGLOG_0(ixgbe, "Failed to get VF config parameters");
	}

	/*
	 * Map device registers
	 */
	if (ixgbe_regs_map(ixgbe) != IXGBE_SUCCESS) {
		ixgbe_error(ixgbe, "Failed to map device registers");
		goto attach_fail;
	}
	ixgbe->attach_progress |= ATTACH_PROGRESS_REGS_MAP;

	/*
	 * Initialize driver parameters
	 */
	ixgbe_init_properties(ixgbe);
	ixgbe->attach_progress |= ATTACH_PROGRESS_PROPS;

	if (ixgbe->sriov_pf) {
		vfcfg.cmd = PCIV_VF_ENABLE;
		if ((ret = pciv_vf_config(ixgbe->dip, &vfcfg)) != DDI_SUCCESS) {
			IXGBE_DEBUGLOG_6(ixgbe, "sriov: pciv_vf_config \
			    vfcfg.cmd %d, \
			    vfcfg.num_vf %d, \
			    vfcfg.first_vf_offset %d, \
			    vfcfg.vf_stride %d, \
			    vfcfg.ari_cap %d, \
			    vfcfg.page_size %d",
			    vfcfg.cmd,
			    vfcfg.num_vf,
			    vfcfg.first_vf_offset,
			    vfcfg.vf_stride,
			    vfcfg.ari_cap,
			    vfcfg.page_size);

			ixgbe_error(ixgbe,
			    "Failed to enable VF");
			goto attach_fail;
		}
		IXGBE_DEBUGLOG_1(ixgbe, "%d VF enabled", vfcfg.num_vf);
	}

	/* Set the SR-IOV callback flag unconditionally */
	ixgbe->cb_flags |= DDI_CB_FLAG_SRIOV;

	/* Set the IRM callback flag */
	ixgbe->cb_flags |= DDI_CB_FLAG_INTR;

	/*
	 * Register the callback function
	 */
	ret = ddi_cb_register(ixgbe->dip,
	    ixgbe->cb_flags,
	    ixgbe_cb_func,
	    ixgbe,
	    NULL,
	    &ixgbe->cb_hdl);
	if (ret != DDI_SUCCESS) {
		ixgbe_error(ixgbe,
		    "Failed to register callback, "
		    "error status: %d", ret);
		goto attach_fail;
	}
	ixgbe->attach_progress |= ATTACH_PROGRESS_CB;

	/*
	 * Allocate interrupts
	 */
	if (ixgbe_alloc_intrs(ixgbe) != IXGBE_SUCCESS) {
		ixgbe_error(ixgbe, "Failed to allocate interrupts");
		goto attach_fail;
	}
	ixgbe->attach_progress |= ATTACH_PROGRESS_ALLOC_INTR;

	/*
	 * Allocate rx/tx rings based on the ring numbers.
	 * The actual numbers of rx/tx rings are decided by the number of
	 * allocated interrupt vectors, so we should allocate the rings after
	 * interrupts are allocated.
	 */
	if (ixgbe_alloc_rings(ixgbe) != IXGBE_SUCCESS) {
		ixgbe_error(ixgbe, "Failed to allocate rx and tx rings");
		goto attach_fail;
	}
	ixgbe->attach_progress |= ATTACH_PROGRESS_ALLOC_RINGS;

	/*
	 * Map rings to interrupt vectors
	 */
	if (ixgbe_map_intrs_to_vectors(ixgbe) != IXGBE_SUCCESS) {
		ixgbe_error(ixgbe, "Failed to map interrupts to vectors");
		goto attach_fail;
	}

	/*
	 * Add interrupt handlers
	 */
	if (ixgbe_add_intr_handlers(ixgbe) != IXGBE_SUCCESS) {
		ixgbe_error(ixgbe, "Failed to add interrupt handlers");
		goto attach_fail;
	}
	ixgbe->attach_progress |= ATTACH_PROGRESS_ADD_INTR;

	/*
	 * Create a taskq for sfp-change
	 */
	(void) sprintf(taskqname, "ixgbe%d_sfp_taskq", instance);
	if ((ixgbe->sfp_taskq = ddi_taskq_create(devinfo, taskqname,
	    1, TASKQ_DEFAULTPRI, 0)) == NULL) {
		ixgbe_error(ixgbe, "sfp_taskq create failed");
		goto attach_fail;
	}
	ixgbe->attach_progress |= ATTACH_PROGRESS_SFP_TASKQ;

	/*
	 * Create a taskq for over-temp
	 */
	(void) sprintf(taskqname, "ixgbe%d_overtemp_taskq", instance);
	if ((ixgbe->overtemp_taskq = ddi_taskq_create(devinfo, taskqname,
	    1, TASKQ_DEFAULTPRI, 0)) == NULL) {
		ixgbe_error(ixgbe, "overtemp_taskq create failed");
		goto attach_fail;
	}
	ixgbe->attach_progress |= ATTACH_PROGRESS_OVERTEMP_TASKQ;

	/*
	 * Initialize driver parameters
	 */
	if (ixgbe_init_driver_settings(ixgbe) != IXGBE_SUCCESS) {
		ixgbe_error(ixgbe, "Failed to initialize driver settings");
		goto attach_fail;
	}

	/*
	 * Initialize mutexes for this device.
	 * Do this before enabling the interrupt handler and
	 * register the softint to avoid the condition where
	 * interrupt handler can try using uninitialized mutex.
	 */
	ixgbe_init_locks(ixgbe);
	ixgbe->attach_progress |= ATTACH_PROGRESS_LOCKS;

	ixgbe->link_check_complete = B_FALSE;
	ixgbe->link_check_hrtime = gethrtime() +
	    (IXGBE_LINK_UP_TIME * 100000000ULL);

	/*
	 * Initialize statistics
	 */
	if (ixgbe_init_stats(ixgbe) != IXGBE_SUCCESS) {
		ixgbe_error(ixgbe, "Failed to initialize statistics");
		goto attach_fail;
	}
	ixgbe->attach_progress |= ATTACH_PROGRESS_STATS;

	/*
	 * Initialize chipset specific hardware function pointers
	 */
	if (ixgbe_init_shared_code(hw) != IXGBE_SUCCESS) {
		ixgbe_error(ixgbe, "Failed to initialize hw settings");
		goto attach_fail;
	}

	ixgbe->periodic_id = ddi_periodic_add(ixgbe_link_timer, ixgbe,
	    IXGBE_CYCLIC_PERIOD, DDI_IPL_0);
	if (ixgbe->periodic_id == 0) {
		ixgbe_error(ixgbe, "Failed to add the link check timer");
		goto attach_fail;
	}
	ixgbe->attach_progress |= ATTACH_PROGRESS_LINK_TIMER;

	/*
	 * Initialize the adapter
	 */
	if (ixgbe_init(ixgbe) != IXGBE_SUCCESS) {
		ixgbe_error(ixgbe, "Failed to initialize adapter");
		goto attach_fail;
	}
	ixgbe->attach_progress |= ATTACH_PROGRESS_INIT;

	if (ixgbe_check_acc_handle(ixgbe->osdep.cfg_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(ixgbe->dip, DDI_SERVICE_LOST);
		goto attach_fail;
	}

	/*
	 * Power off optics
	 */
	if (hw->mac.type == ixgbe_mac_82599EB &&
	    ixgbe_get_media_type(hw) == ixgbe_media_type_fiber)
		ixgbe_disable_tx_laser(hw);

	/*
	 * Register the driver to the MAC
	 */
	if (ixgbe_register_mac(ixgbe) != IXGBE_SUCCESS) {
		ixgbe_error(ixgbe, "Failed to register MAC");
		goto attach_fail;
	}
	mac_link_update(ixgbe->mac_hdl, LINK_STATE_UNKNOWN);
	ixgbe->attach_progress |= ATTACH_PROGRESS_MAC;

	/*
	 * Now that mutex locks are initialized, and the chip is also
	 * initialized, enable interrupts.
	 */
	if (ixgbe_enable_intrs(ixgbe) != IXGBE_SUCCESS) {
		ixgbe_error(ixgbe, "Failed to enable DDI interrupts");
		goto attach_fail;
	}
	ixgbe->attach_progress |= ATTACH_PROGRESS_ENABLE_INTR;

	if (ixgbe->sriov_pf) {
		/*
		 * enable virtual functions
		 */
		ixgbe_enable_vf(ixgbe);

		/*
		 * In SRIOV mode, the tx/rx unit needs to be enabled
		 * before each VF setup.
		 */
		mutex_enter(&ixgbe->gen_lock);

		ixgbe_setup_rx_global(ixgbe);
		ixgbe_setup_tx_global(ixgbe);

		mutex_exit(&ixgbe->gen_lock);
	}

	ixgbe_log(ixgbe, "%s, %s", ixgbe_ident, ixgbe_version);
	atomic_or_32(&ixgbe->ixgbe_state, IXGBE_INITIALIZED);

	return (DDI_SUCCESS);

attach_fail:
	(void) ixgbe_unconfigure(ixgbe->dip, ixgbe);
	return (DDI_FAILURE);
}

/*
 * ixgbe_detach - Driver detach.
 *
 * The detach() function is the complement of the attach routine.
 * If cmd is set to DDI_DETACH, detach() is used to remove  the
 * state  associated  with  a  given  instance of a device node
 * prior to the removal of that instance from the system.
 *
 * The detach() function will be called once for each  instance
 * of the device for which there has been a successful attach()
 * once there are no longer  any  opens  on  the  device.
 *
 * Interrupts routine are disabled, All memory allocated by this
 * driver are freed.
 */
static int
ixgbe_detach(dev_info_t *devinfo, ddi_detach_cmd_t cmd)
{
	ixgbe_t *ixgbe;

	/*
	 * Check detach command
	 */
	switch (cmd) {
	default:
		return (DDI_FAILURE);

	case DDI_SUSPEND:
		return (ixgbe_suspend(devinfo));

	case DDI_DETACH:
		break;
	}

	/*
	 * Get the pointer to the driver private data structure
	 */
	ixgbe = (ixgbe_t *)ddi_get_driver_private(devinfo);
	if (ixgbe == NULL)
		return (DDI_FAILURE);

	/*
	 * The DDI framework may initiate a detach any time. Proceed further
	 * only if the call to mac_disable() succeeds.
	 */
	if (mac_disable(ixgbe->mac_hdl) != 0)
		return (DDI_FAILURE);

	/*
	 * If the device is still running, it needs to be stopped first.
	 * This check is necessary because under some specific circumstances,
	 * the detach routine can be called without stopping the interface
	 * first.
	 */
	mutex_enter(&ixgbe->gen_lock);
	if (ixgbe->ixgbe_state & IXGBE_STARTED) {
		atomic_and_32(&ixgbe->ixgbe_state, ~IXGBE_STARTED);
		ixgbe_stop(ixgbe, B_TRUE);
		mutex_exit(&ixgbe->gen_lock);
		/* Disable and stop the watchdog timer */
		ixgbe_disable_watchdog_timer(ixgbe);
	} else
		mutex_exit(&ixgbe->gen_lock);

	/*
	 * Check if there are still rx ddp buffers held by the upper layer.
	 * If so, fail the detach.
	 */
	if (!ixgbe_rx_drain(ixgbe))
		return (DDI_FAILURE);

	/*
	 * Do the remaining unconfigure routines
	 */
	if (ixgbe_unconfigure(devinfo, ixgbe) != IXGBE_SUCCESS)
		return (DDI_FAILURE);

	return (DDI_SUCCESS);
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
static int
ixgbe_quiesce(dev_info_t *devinfo)
{
	ixgbe_t *ixgbe;

	ixgbe = (ixgbe_t *)ddi_get_driver_private(devinfo);
	struct ixgbe_hw *hw = &ixgbe->hw;

	if (ixgbe == NULL)
		return (DDI_FAILURE);

	/*
	 * Disable the adapter interrupts
	 */
	ixgbe_disable_adapter_interrupts(ixgbe);

	/*
	 * Disable Rx/Tx units
	 */
	(void) ixgbe_stop_adapter(hw);

	/*
	 * Tell firmware driver is no longer in control
	 */
	ixgbe_release_driver_control(hw);

	/*
	 * Reset the chipset
	 */
	(void) ixgbe_reset_hw(hw);

	/*
	 * Reset PHY
	 */
	(void) ixgbe_reset_phy(hw);

	return (DDI_SUCCESS);
}

static int
ixgbe_unconfigure(dev_info_t *devinfo, ixgbe_t *ixgbe)
{
	/*
	 * Disable interrupt
	 */
	if (ixgbe->attach_progress & ATTACH_PROGRESS_ENABLE_INTR) {
		if (ixgbe_disable_intrs(ixgbe) != IXGBE_SUCCESS) {
			ixgbe_error(ixgbe, "Failed to disable interrupts");
			return (IXGBE_FAILURE);
		}
	}
	ixgbe->attach_progress &= ~ATTACH_PROGRESS_ENABLE_INTR;

	/*
	 * remove the link check timer
	 */
	if (ixgbe->attach_progress & ATTACH_PROGRESS_LINK_TIMER) {
		if (ixgbe->periodic_id != NULL) {
			ddi_periodic_delete(ixgbe->periodic_id);
			ixgbe->periodic_id = NULL;
		}
	}
	ixgbe->attach_progress &= ~ATTACH_PROGRESS_LINK_TIMER;

	/*
	 * Unregister MAC
	 */
	if (ixgbe->attach_progress & ATTACH_PROGRESS_MAC) {
		mutex_enter(&ixgbe->gen_lock);
		if (mac_unregister(ixgbe->mac_hdl) != 0) {
			ixgbe->mac_hdl = 0;
			mutex_exit(&ixgbe->gen_lock);
			ixgbe_error(ixgbe, "Failed to unregister mac handler");
			return (IXGBE_FAILURE);
		}
		mutex_exit(&ixgbe->gen_lock);
	}
	ixgbe->attach_progress &= ~ATTACH_PROGRESS_MAC;

	/*
	 * Free statistics
	 */
	if (ixgbe->attach_progress & ATTACH_PROGRESS_STATS) {
		kstat_delete((kstat_t *)ixgbe->ixgbe_ks);
	}
	ixgbe->attach_progress &= ~ATTACH_PROGRESS_STATS;

	/*
	 * Remove interrupt handlers
	 */
	if (ixgbe->attach_progress & ATTACH_PROGRESS_ADD_INTR) {
		ixgbe_rem_intr_handlers(ixgbe);
	}
	ixgbe->attach_progress &= ~ATTACH_PROGRESS_ADD_INTR;

	/*
	 * Remove taskq for sfp-status-change
	 */
	if (ixgbe->attach_progress & ATTACH_PROGRESS_SFP_TASKQ) {
		ddi_taskq_destroy(ixgbe->sfp_taskq);
	}
	ixgbe->attach_progress &= ~ATTACH_PROGRESS_SFP_TASKQ;

	/*
	 * Remove taskq for over-temp
	 */
	if (ixgbe->attach_progress & ATTACH_PROGRESS_OVERTEMP_TASKQ) {
		ddi_taskq_destroy(ixgbe->overtemp_taskq);
	}
	ixgbe->attach_progress &= ~ATTACH_PROGRESS_OVERTEMP_TASKQ;

	/*
	 * Remove interrupts
	 */
	if (ixgbe->attach_progress & ATTACH_PROGRESS_ALLOC_INTR) {
		ixgbe_rem_intrs(ixgbe);
	}
	ixgbe->attach_progress &= ~ATTACH_PROGRESS_ALLOC_INTR;

	/*
	 * Unregister the callback handler
	 */
	if (ixgbe->attach_progress & ATTACH_PROGRESS_CB) {
		if (ddi_cb_unregister(ixgbe->cb_hdl) != DDI_SUCCESS) {
			ixgbe_error(ixgbe, "Failed to unregister callback");
			return (IXGBE_FAILURE);
		}
	}
	ixgbe->attach_progress &= ~ATTACH_PROGRESS_CB;

	/*
	 * Remove driver properties
	 */
	if (ixgbe->attach_progress & ATTACH_PROGRESS_PROPS) {
		ddi_prop_remove_all(devinfo);
	}
	ixgbe->attach_progress &= ~ATTACH_PROGRESS_PROPS;

	/*
	 * Stop the chipset
	 */
	if (ixgbe->attach_progress & ATTACH_PROGRESS_INIT) {
		mutex_enter(&ixgbe->gen_lock);
		ixgbe_chip_stop(ixgbe);
		mutex_exit(&ixgbe->gen_lock);
	}
	ixgbe->attach_progress &= ~ATTACH_PROGRESS_INIT;

	/*
	 * Free register handle
	 */
	if (ixgbe->attach_progress & ATTACH_PROGRESS_REGS_MAP) {
		if (ixgbe->osdep.reg_handle != NULL)
			ddi_regs_map_free(&ixgbe->osdep.reg_handle);
	}
	ixgbe->attach_progress &= ~ATTACH_PROGRESS_REGS_MAP;

	/*
	 * Free PCI config handle
	 */
	if (ixgbe->attach_progress & ATTACH_PROGRESS_PCI_CONFIG) {
		if (ixgbe->osdep.cfg_handle != NULL)
			pci_config_teardown(&ixgbe->osdep.cfg_handle);
	}
	ixgbe->attach_progress &= ~ATTACH_PROGRESS_PCI_CONFIG;

	/*
	 * Free locks
	 */
	if (ixgbe->attach_progress & ATTACH_PROGRESS_LOCKS) {
		ixgbe_destroy_locks(ixgbe);
	}
	ixgbe->attach_progress &= ~ATTACH_PROGRESS_LOCKS;

	/*
	 * Free the rx/tx rings
	 */
	if (ixgbe->attach_progress & ATTACH_PROGRESS_ALLOC_RINGS) {
		ixgbe_free_rings(ixgbe);
	}
	ixgbe->attach_progress &= ~ATTACH_PROGRESS_ALLOC_RINGS;

	/*
	 * Unregister FMA capabilities
	 */
	if (ixgbe->attach_progress & ATTACH_PROGRESS_FM_INIT) {
		ixgbe_fm_fini(ixgbe);
	}
	ixgbe->attach_progress &= ~ATTACH_PROGRESS_FM_INIT;

	/*
	 * Free the driver data structure
	 */
	kmem_free(ixgbe, sizeof (ixgbe_t));

	ddi_set_driver_private(devinfo, NULL);

	return (IXGBE_SUCCESS);
}

/*
 * ixgbe_register_mac - Register the driver and its function pointers with
 * the GLD interface.
 */
static int
ixgbe_register_mac(ixgbe_t *ixgbe)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	mac_register_t *mac;
	int status;

	if ((mac = mac_alloc(MAC_VERSION)) == NULL)
		return (IXGBE_FAILURE);

	mac->m_type_ident = MAC_PLUGIN_IDENT_ETHER;
	mac->m_driver = ixgbe;
	mac->m_dip = ixgbe->dip;
	mac->m_src_addr = hw->mac.addr;
	mac->m_callbacks = &ixgbe_m_callbacks;
	mac->m_min_sdu = 0;
	mac->m_max_sdu = ixgbe->default_mtu;
	mac->m_margin = VLAN_TAGSZ;
	mac->m_priv_props = ixgbe_priv_props;

	status = mac_register(mac, &ixgbe->mac_hdl);

	mac_free(mac);

	return ((status == 0) ? IXGBE_SUCCESS : IXGBE_FAILURE);
}

/*
 * ixgbe_identify_hardware - Identify the type of the chipset.
 */
static int
ixgbe_identify_hardware(ixgbe_t *ixgbe)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	struct ixgbe_osdep *osdep = &ixgbe->osdep;

	/*
	 * Get the device id
	 */
	hw->vendor_id =
	    pci_config_get16(osdep->cfg_handle, PCI_CONF_VENID);
	hw->device_id =
	    pci_config_get16(osdep->cfg_handle, PCI_CONF_DEVID);
	hw->revision_id =
	    pci_config_get8(osdep->cfg_handle, PCI_CONF_REVID);
	hw->subsystem_device_id =
	    pci_config_get16(osdep->cfg_handle, PCI_CONF_SUBSYSID);
	hw->subsystem_vendor_id =
	    pci_config_get16(osdep->cfg_handle, PCI_CONF_SUBVENID);

	/*
	 * Set the mac type of the adapter based on the device id
	 */
	if (ixgbe_set_mac_type(hw) != IXGBE_SUCCESS) {
		return (IXGBE_FAILURE);
	}

	/*
	 * Install adapter capabilities
	 */
	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		IXGBE_DEBUGLOG_0(ixgbe, "identify 82598 adapter\n");
		ixgbe->capab = &ixgbe_82598eb_cap;

		if (ixgbe_get_media_type(hw) == ixgbe_media_type_copper) {
			ixgbe->capab->flags |= IXGBE_FLAG_FAN_FAIL_CAPABLE;
			ixgbe->capab->other_intr |= IXGBE_EICR_GPI_SDP1;
			ixgbe->capab->other_gpie |= IXGBE_SDP1_GPIEN;
		}
		break;

	case ixgbe_mac_82599EB:
		IXGBE_DEBUGLOG_0(ixgbe, "identify 82599 adapter\n");
		ixgbe->capab = &ixgbe_82599eb_cap;

		if (hw->device_id == IXGBE_DEV_ID_82599_T3_LOM) {
			ixgbe->capab->flags |= IXGBE_FLAG_TEMP_SENSOR_CAPABLE;
			ixgbe->capab->other_intr |= IXGBE_EICR_GPI_SDP0;
			ixgbe->capab->other_gpie |= IXGBE_SDP0_GPIEN;
		}
		break;

	case ixgbe_mac_X540:
		IXGBE_DEBUGLOG_0(ixgbe, "identify X540 adapter\n");
		ixgbe->capab = &ixgbe_X540_cap;
		break;

	default:
		IXGBE_DEBUGLOG_1(ixgbe,
		    "adapter not supported in ixgbe_identify_hardware(): %d\n",
		    hw->mac.type);
		return (IXGBE_FAILURE);
	}

	return (IXGBE_SUCCESS);
}

/*
 * ixgbe_regs_map - Map the device registers.
 *
 */
static int
ixgbe_regs_map(ixgbe_t *ixgbe)
{
	dev_info_t *devinfo = ixgbe->dip;
	struct ixgbe_hw *hw = &ixgbe->hw;
	struct ixgbe_osdep *osdep = &ixgbe->osdep;
	off_t mem_size;

	/*
	 * First get the size of device registers to be mapped.
	 */
	if (ddi_dev_regsize(devinfo, IXGBE_ADAPTER_REGSET, &mem_size)
	    != DDI_SUCCESS) {
		return (IXGBE_FAILURE);
	}

	/*
	 * Call ddi_regs_map_setup() to map registers
	 */
	if ((ddi_regs_map_setup(devinfo, IXGBE_ADAPTER_REGSET,
	    (caddr_t *)&hw->hw_addr, 0,
	    mem_size, &ixgbe_regs_acc_attr,
	    &osdep->reg_handle)) != DDI_SUCCESS) {
		return (IXGBE_FAILURE);
	}

	return (IXGBE_SUCCESS);
}

/*
 * ixgbe_init_properties - Initialize driver properties.
 */
static void
ixgbe_init_properties(ixgbe_t *ixgbe)
{
	/*
	 * Get conf file properties, including link settings
	 * jumbo frames, ring number, descriptor number, etc.
	 */
	ixgbe_get_conf(ixgbe);

	ixgbe_init_params(ixgbe);

	if (ixgbe->sriov_pf)
		(void) ixgbe_get_iov_params(ixgbe);
}

/*
 * ixgbe_init_bcm8706_leds - Initialize BCM8706 LED controls.
 */
static void
ixgbe_init_bcm8706_leds(ixgbe_t *ixgbe)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	uint16_t reg_val;
	int32_t status;
	ixgbe_log(ixgbe, "BCM8706 phy address %x", hw->phy.addr);
	status = ixgbe_reset_phy(hw);
	if (status != IXGBE_SUCCESS)
		ixgbe_log(ixgbe, "reset phy status %d", status);

	/*
	 * BCM8706 use two gpio pins to control the leds.
	 * PMD Digital Control Register(Address = 0xc808).
	 * bits 6:4 GPIO_SEL control the gpio mode.
	 * Use mode 111 on our platform.
	 */
	(void) ixgbe_read_phy_reg(hw, 0xc808, 0x3, &reg_val);
	reg_val |= 0x70;
	(void) ixgbe_write_phy_reg(hw, 0xc808, 0x3, reg_val);
}

/*
 * ixgbe_init_driver_settings - Initialize driver settings.
 *
 * The settings include hardware function pointers, bus information,
 * rx/tx rings settings, link state, and any other parameters that
 * need to be setup during driver initialization.
 */
static int
ixgbe_init_driver_settings(ixgbe_t *ixgbe)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	dev_info_t *devinfo = ixgbe->dip;
	ixgbe_rx_ring_t *rx_ring;
	ixgbe_group_t *rx_group, *tx_group;
	ixgbe_tx_ring_t *tx_ring;
	uint32_t rx_size;
	uint32_t tx_size;
	uint32_t ring_per_group;
	uint16_t i;

	/* Initializ the mailbox operations */
	ixgbe_init_function_pointer_pf(ixgbe);
	ixgbe->hw.mbx.ops.init_params(hw);

	/*
	 * Get the system page size
	 */
	ixgbe->sys_page_size = ddi_ptob(devinfo, (ulong_t)1);

	/*
	 * Set rx buffer size
	 *
	 * The IP header alignment room is counted in the calculation.
	 * The rx buffer size is in unit of 1K that is required by the
	 * chipset hardware.
	 */
	rx_size = ixgbe->max_frame_size + IPHDR_ALIGN_ROOM;
	ixgbe->rx_buf_size = ((rx_size >> 10) +
	    ((rx_size & (((uint32_t)1 << 10) - 1)) > 0 ? 1 : 0)) << 10;

	/*
	 * Set tx buffer size
	 */
	tx_size = ixgbe->max_frame_size;
	ixgbe->tx_buf_size = ((tx_size >> 10) +
	    ((tx_size & (((uint32_t)1 << 10) - 1)) > 0 ? 1 : 0)) << 10;

	/*
	 * Initialize rx/tx rings/groups parameters
	 */
	if (ixgbe->sriov_pf) {
		for (i = 0; i < ixgbe->num_rx_rings; i ++) {
			rx_ring = &ixgbe->rx_rings[i];
			rx_ring->ixgbe = ixgbe;
			rx_ring->index = i;
			rx_ring->group_index = ixgbe->pf_grp;
			rx_ring->hw_index =
			    (ixgbe->pf_grp *
			    ixgbe->capab->max_rx_que_num / ixgbe->iov_mode) + i;
		}

		for (i = 0; i < ixgbe->num_all_rx_groups; i++) {
			rx_group = &ixgbe->rx_groups[i];
			rx_group->index = i;
			rx_group->ixgbe = ixgbe;
		}

		for (i = 0; i < ixgbe->num_tx_rings; i++) {
			tx_ring = &ixgbe->tx_rings[i];
			tx_ring->ixgbe = ixgbe;
			tx_ring->index = i;
			tx_ring->group_index = ixgbe->pf_grp;
			tx_ring->hw_index =
			    (ixgbe->pf_grp *
			    ixgbe->capab->max_tx_que_num / ixgbe->iov_mode) + i;
			if (ixgbe->tx_head_wb_enable)
				tx_ring->tx_recycle = ixgbe_tx_recycle_head_wb;
			else
				tx_ring->tx_recycle = ixgbe_tx_recycle_legacy;

			tx_ring->ring_size = ixgbe->tx_ring_size;
			tx_ring->free_list_size = ixgbe->tx_ring_size +
			    (ixgbe->tx_ring_size >> 1);
		}

		for (i = 0; i < ixgbe->num_all_tx_groups; i++) {
			tx_group = &ixgbe->tx_groups[i];
			tx_group->index = i;
			tx_group->ixgbe = ixgbe;
		}
	} else {
		ring_per_group = ixgbe->num_rx_rings / ixgbe->num_pf_rx_groups;
		for (i = 0; i < ixgbe->num_rx_rings; i++) {
			rx_ring = &ixgbe->rx_rings[i];
			rx_ring->index = i;
			rx_ring->ixgbe = ixgbe;
			rx_ring->group_index = i / ring_per_group;
			ixgbe_get_hw_rx_index(ixgbe, rx_ring, i);
		}

		for (i = 0; i < ixgbe->num_pf_rx_groups; i++) {
			rx_group = &ixgbe->rx_groups[i];
			rx_group->index = i;
			rx_group->ixgbe = ixgbe;
		}

		for (i = 0; i < ixgbe->num_tx_rings; i++) {
			tx_ring = &ixgbe->tx_rings[i];
			tx_ring->index = i;
			tx_ring->ixgbe = ixgbe;
			if (ixgbe->tx_head_wb_enable)
				tx_ring->tx_recycle = ixgbe_tx_recycle_head_wb;
			else
				tx_ring->tx_recycle = ixgbe_tx_recycle_legacy;

			tx_ring->ring_size = ixgbe->tx_ring_size;
			tx_ring->free_list_size = ixgbe->tx_ring_size +
			    (ixgbe->tx_ring_size >> 1);
			ixgbe_get_hw_tx_index(ixgbe, tx_ring, i);
		}

		for (i = 0; i < ixgbe->num_pf_tx_groups; i++) {
			tx_group = &ixgbe->tx_groups[i];
			tx_group->index = i;
			tx_group->ixgbe = ixgbe;
		}
	}
#ifdef CONFIG_DCB
	ixgbe_init_dcb_settings(ixgbe);
#endif

	/*
	 * Initialize values of interrupt throttling rate
	 */
	for (i = 1; i < MAX_INTR_VECTOR; i++)
		ixgbe->intr_throttling[i] = ixgbe->intr_throttling[0];

	/*
	 * The initial link state should be "unknown"
	 */
	ixgbe->link_state = LINK_STATE_UNKNOWN;

	return (IXGBE_SUCCESS);
}

/*
 * Initialize values for DCB configurations.
 */
static void
ixgbe_init_dcb_settings(ixgbe_t *ixgbe)
{
	struct ixgbe_dcb_config *dcb_config;
	struct tc_configuration *tc_cfg;
	struct tc_bw_alloc *p;
	int i, j;
	int num_tcs;

	dcb_config = &ixgbe->dcb_config;
	num_tcs = dcb_config->num_tcs.pg_tcs;
	if (num_tcs == MAX_TRAFFIC_CLASS) {
		/* Make sure the sum of all bw is 100% */
		int bw[] = {16, 12, 12, 12, 12, 12, 12, 12};
		for (i = 0; i < MAX_TRAFFIC_CLASS; i++) {
			tc_cfg = &dcb_config->tc_config[i];
			tc_cfg->dcb_pfc = pfc_enabled_full;

			for (j = DCB_TX_CONFIG; j <= DCB_RX_CONFIG; j++) {
				p = &tc_cfg->path[j];
				p->up_to_tc_bitmap = 1 << i;
				p->bwg_id = IXGBE_DEFAULT_BWG;
				p->bwg_percent = bw[i];
				p->prio_type = prio_group;
			}
		}
	} else if (num_tcs == MAX_TRAFFIC_CLASS / 2) {
		for (i = 0; i < MAX_TRAFFIC_CLASS / 2; i++) {
			tc_cfg = &dcb_config->tc_config[i];
			tc_cfg->dcb_pfc = pfc_enabled_full;

			for (j = DCB_TX_CONFIG; j <= DCB_RX_CONFIG; j++) {
				p = &tc_cfg->path[j];
				p->up_to_tc_bitmap = 1 << (2 * i);
				p->up_to_tc_bitmap |= 1 << (2 * i + 1);
				p->bwg_id = IXGBE_DEFAULT_BWG;
				p->bwg_percent = BW_PERCENT / num_tcs;
				p->prio_type = prio_group;
			}
		}
	}

	/*
	 * Use BWG ID 0 by default.
	 */
	for (i = 0; i < MAX_BW_GROUP; i++) {
		dcb_config->bw_percentage[DCB_TX_CONFIG][i] = 0;
		dcb_config->bw_percentage[DCB_RX_CONFIG][i] = 0;
	}
	dcb_config->bw_percentage[DCB_TX_CONFIG][IXGBE_DEFAULT_BWG] = 100;
	dcb_config->bw_percentage[DCB_RX_CONFIG][IXGBE_DEFAULT_BWG] = 100;

	if (ixgbe->hw.fc.requested_mode == ixgbe_fc_pfc)
		dcb_config->pfc_mode_enable = B_TRUE;
	else
		dcb_config->pfc_mode_enable = B_FALSE;
	dcb_config->rx_pba_cfg = pba_equal;

	ixgbe->pfc_map = 0;
}

/*
 * ixgbe_init_locks - Initialize locks.
 */
static void
ixgbe_init_locks(ixgbe_t *ixgbe)
{
	ixgbe_rx_ring_t *rx_ring;
	ixgbe_tx_ring_t *tx_ring;
	int i;

	for (i = 0; i < ixgbe->num_rx_rings; i++) {
		rx_ring = &ixgbe->rx_rings[i];
		mutex_init(&rx_ring->rx_lock, NULL,
		    MUTEX_DRIVER, DDI_INTR_PRI(ixgbe->intr_pri));
	}

	mutex_init(&ixgbe->rx_fcoe_lock, NULL,
	    MUTEX_DRIVER, DDI_INTR_PRI(ixgbe->intr_pri));

	for (i = 0; i < ixgbe->num_tx_rings; i++) {
		tx_ring = &ixgbe->tx_rings[i];
		mutex_init(&tx_ring->tx_lock, NULL,
		    MUTEX_DRIVER, DDI_INTR_PRI(ixgbe->intr_pri));
		mutex_init(&tx_ring->recycle_lock, NULL,
		    MUTEX_DRIVER, DDI_INTR_PRI(ixgbe->intr_pri));
		mutex_init(&tx_ring->tcb_head_lock, NULL,
		    MUTEX_DRIVER, DDI_INTR_PRI(ixgbe->intr_pri));
		mutex_init(&tx_ring->tcb_tail_lock, NULL,
		    MUTEX_DRIVER, DDI_INTR_PRI(ixgbe->intr_pri));
	}

	mutex_init(&ixgbe->gen_lock, NULL,
	    MUTEX_DRIVER, DDI_INTR_PRI(ixgbe->intr_pri));
	mutex_init(&ixgbe->rx_pending_lock, NULL,
	    MUTEX_DRIVER, DDI_INTR_PRI(ixgbe->intr_pri));
	mutex_init(&ixgbe->watchdog_lock, NULL,
	    MUTEX_DRIVER, DDI_INTR_PRI(ixgbe->intr_pri));
}

/*
 * ixgbe_destroy_locks - Destroy locks.
 */
static void
ixgbe_destroy_locks(ixgbe_t *ixgbe)
{
	ixgbe_rx_ring_t *rx_ring;
	ixgbe_tx_ring_t *tx_ring;
	int i;

	for (i = 0; i < ixgbe->num_rx_rings; i++) {
		rx_ring = &ixgbe->rx_rings[i];
		mutex_destroy(&rx_ring->rx_lock);
	}

	mutex_destroy(&ixgbe->rx_fcoe_lock);

	for (i = 0; i < ixgbe->num_tx_rings; i++) {
		tx_ring = &ixgbe->tx_rings[i];
		mutex_destroy(&tx_ring->tx_lock);
		mutex_destroy(&tx_ring->recycle_lock);
		mutex_destroy(&tx_ring->tcb_head_lock);
		mutex_destroy(&tx_ring->tcb_tail_lock);
	}

	mutex_destroy(&ixgbe->gen_lock);
	mutex_destroy(&ixgbe->rx_pending_lock);
	mutex_destroy(&ixgbe->watchdog_lock);
}

static int
ixgbe_resume(dev_info_t *devinfo)
{
	ixgbe_t *ixgbe;
	int i;

	ixgbe = (ixgbe_t *)ddi_get_driver_private(devinfo);
	if (ixgbe == NULL)
		return (DDI_FAILURE);

	mutex_enter(&ixgbe->gen_lock);

	/*
	 * Enable interrupts
	 */
	if (ixgbe_enable_intrs(ixgbe) != IXGBE_SUCCESS) {
		ixgbe_error(ixgbe, "Failed to enable DDI interrupts");
		mutex_exit(&ixgbe->gen_lock);
		return (DDI_FAILURE);
	}
	ixgbe->attach_progress |= ATTACH_PROGRESS_ENABLE_INTR;

	if (ixgbe->ixgbe_state & IXGBE_STARTED) {
		if (ixgbe_start(ixgbe, B_FALSE) != IXGBE_SUCCESS) {
			mutex_exit(&ixgbe->gen_lock);
			return (DDI_FAILURE);
		}

		/*
		 * Enable and start the watchdog timer
		 */
		ixgbe_enable_watchdog_timer(ixgbe);
	}

	atomic_and_32(&ixgbe->ixgbe_state, ~IXGBE_SUSPENDED);

	if (ixgbe->ixgbe_state & IXGBE_STARTED) {
		for (i = 0; i < ixgbe->num_tx_rings; i++) {
			mac_tx_ring_update(ixgbe->mac_hdl,
			    ixgbe->tx_rings[i].ring_handle);
		}
	}

	mutex_exit(&ixgbe->gen_lock);

	return (DDI_SUCCESS);
}

static int
ixgbe_suspend(dev_info_t *devinfo)
{
	ixgbe_t *ixgbe;

	ixgbe = (ixgbe_t *)ddi_get_driver_private(devinfo);
	if (ixgbe == NULL)
		return (DDI_FAILURE);

	if (ixgbe->num_vfs > 0)
		return (DDI_FAILURE);

	mutex_enter(&ixgbe->gen_lock);

	atomic_or_32(&ixgbe->ixgbe_state, IXGBE_SUSPENDED);

	/*
	 * Disable interrupt
	 */
	if (ixgbe->attach_progress & ATTACH_PROGRESS_ENABLE_INTR) {
		(void) ixgbe_disable_intrs(ixgbe);
		ixgbe->attach_progress &= ~ATTACH_PROGRESS_ENABLE_INTR;
	}

	if (!(ixgbe->ixgbe_state & IXGBE_STARTED)) {
		mutex_exit(&ixgbe->gen_lock);
		return (DDI_SUCCESS);
	}
	ixgbe_stop(ixgbe, B_FALSE);

	mutex_exit(&ixgbe->gen_lock);

	/*
	 * Disable and stop the watchdog timer
	 */
	ixgbe_disable_watchdog_timer(ixgbe);

	return (DDI_SUCCESS);
}

/*
 * ixgbe_init - Initialize the device.
 */
static int
ixgbe_init(ixgbe_t *ixgbe)
{
	struct ixgbe_hw *hw = &ixgbe->hw;

	mutex_enter(&ixgbe->gen_lock);

	/*
	 * Reset chipset to put the hardware in a known state
	 * before we try to do anything with the eeprom.
	 */
	if (ixgbe_reset_hw(hw) != IXGBE_SUCCESS) {
		ixgbe_fm_ereport(ixgbe, DDI_FM_DEVICE_INVAL_STATE);
		goto init_fail;
	}

	/*
	 * Need to init eeprom before validating the checksum.
	 */
	if (ixgbe_init_eeprom_params(hw) < 0) {
		ixgbe_error(ixgbe,
		    "Unable to intitialize the eeprom interface.");
		ixgbe_fm_ereport(ixgbe, DDI_FM_DEVICE_INVAL_STATE);
		goto init_fail;
	}

	/*
	 * NVM validation
	 */
	if (ixgbe_validate_eeprom_checksum(hw, NULL) < 0) {
		/*
		 * Some PCI-E parts fail the first check due to
		 * the link being in sleep state.  Call it again,
		 * if it fails a second time it's a real issue.
		 */
		if (ixgbe_validate_eeprom_checksum(hw, NULL) < 0) {
			ixgbe_error(ixgbe,
			    "Invalid NVM checksum. Please contact "
			    "the vendor to update the NVM.");
			ixgbe_fm_ereport(ixgbe, DDI_FM_DEVICE_INVAL_STATE);
			goto init_fail;
		}
	}

	/*
	 * Setup default flow control thresholds - enable/disable
	 * & flow control type is controlled by ixgbe.conf
	 */
	hw->fc.high_water = FC_HIGH_WATER(ixgbe->max_frame_size);
	hw->fc.low_water = FC_LOW_WATER(ixgbe->max_frame_size);
	hw->fc.pause_time = DEFAULT_FCPAUSE;
	hw->fc.send_xon = B_TRUE;

	/*
	 * Operating as a PF driver, put VFs into hold state
	 */
	if (ixgbe->sriov_pf) {
		ixgbe_hold_vfs(ixgbe);
	}

	/*
	 * Initialize link settings
	 */
	(void) ixgbe_driver_setup_link(ixgbe, B_FALSE);

	/*
	 * Initialize the chipset hardware
	 */
	if (ixgbe_chip_start(ixgbe) != IXGBE_SUCCESS) {
		ixgbe_fm_ereport(ixgbe, DDI_FM_DEVICE_INVAL_STATE);
		goto init_fail;
	}

	if (ixgbe_check_acc_handle(ixgbe->osdep.reg_handle) != DDI_FM_OK) {
		goto init_fail;
	}

	mutex_exit(&ixgbe->gen_lock);
	return (IXGBE_SUCCESS);

init_fail:
	/*
	 * Reset PHY
	 */
	(void) ixgbe_reset_phy(hw);

	mutex_exit(&ixgbe->gen_lock);
	ddi_fm_service_impact(ixgbe->dip, DDI_SERVICE_LOST);
	return (IXGBE_FAILURE);
}

/*
 * ixgbe_chip_start - Initialize and start the chipset hardware.
 */
static int
ixgbe_chip_start(ixgbe_t *ixgbe)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	int ret_val, i;

	ASSERT(mutex_owned(&ixgbe->gen_lock));

	/*
	 * Get the mac address
	 * This function should handle SPARC case correctly.
	 */
	if (!ixgbe_find_mac_address(ixgbe)) {
		ixgbe_error(ixgbe, "Failed to get the mac address");
		return (IXGBE_FAILURE);
	}

	/*
	 * Validate the mac address
	 */
	(void) ixgbe_init_rx_addrs(hw);
	if (!ixgbe_is_valid_mac_addr(hw->mac.addr)) {
		ixgbe_error(ixgbe, "Invalid mac address");
		return (IXGBE_FAILURE);
	}

	/*
	 * Configure/Initialize hardware
	 */
	ret_val = ixgbe_init_hw(hw);
	if (ret_val != IXGBE_SUCCESS) {
		if (ret_val == IXGBE_ERR_EEPROM_VERSION) {
			ixgbe_error(ixgbe,
			    "This 82599 device is pre-release and contains"
			    " outdated firmware, please contact your hardware"
			    " vendor for a replacement.");
		} else {
			ixgbe_error(ixgbe, "Failed to initialize hardware");
			return (IXGBE_FAILURE);
		}
	}

	/*
	 * Re-enable relaxed ordering for performance.  It is disabled
	 * by default in the hardware init.
	 */
	if (ixgbe->relax_order_enable == B_TRUE)
		ixgbe_enable_relaxed_ordering(hw);

	/*
	 * If detect BCM8706, setup LEDs.
	 */
	if (hw->phy.id == BCM8706_PHYID)
		ixgbe_init_bcm8706_leds(ixgbe);

	/*
	 * Setup adapter interrupt vectors
	 */
	ixgbe_setup_adapter_vector(ixgbe);

	/*
	 * Initialize unicast addresses.
	 */
	ixgbe_init_unicst(ixgbe);

	/*
	 * Setup and initialize the mctable structures.
	 */
	ixgbe_setup_multicst(ixgbe);

	/*
	 * Set interrupt throttling rate
	 */
	for (i = 0; i < ixgbe->intr_cnt; i++) {
		IXGBE_WRITE_REG(hw, IXGBE_EITR(i), ixgbe->intr_throttling[i]);
	}

	/*
	 * Save the state of the phy
	 */
	ixgbe_get_hw_state(ixgbe);

	/*
	 * Make sure driver has control
	 */
	ixgbe_get_driver_control(hw);

	return (IXGBE_SUCCESS);
}

/*
 * ixgbe_chip_stop - Stop the chipset hardware
 */
static void
ixgbe_chip_stop(ixgbe_t *ixgbe)
{
	struct ixgbe_hw *hw = &ixgbe->hw;

	ASSERT(mutex_owned(&ixgbe->gen_lock));

	/*
	 * Tell firmware driver is no longer in control
	 */
	ixgbe_release_driver_control(hw);

	/*
	 * Reset the chipset
	 */
	(void) ixgbe_reset_hw(hw);

	/*
	 * Reset PHY
	 */
	(void) ixgbe_reset_phy(hw);

	/*
	 * Power off optics
	 */
	if (hw->mac.type == ixgbe_mac_82599EB &&
	    ixgbe_get_media_type(hw) == ixgbe_media_type_fiber)
		ixgbe_disable_tx_laser(hw);
}

/*
 * ixgbe_reset - Reset the chipset and re-start the driver.
 *
 * It involves stopping and re-starting the chipset,
 * and re-configuring the rx/tx rings.
 */
static int
ixgbe_reset(ixgbe_t *ixgbe)
{
	int i;

	/*
	 * Disable and stop the watchdog timer
	 */
	ixgbe_disable_watchdog_timer(ixgbe);

	mutex_enter(&ixgbe->gen_lock);

	if (!(ixgbe->ixgbe_state & IXGBE_STARTED) ||
	    (ixgbe->ixgbe_state & IXGBE_SUSPENDED)) {
		mutex_exit(&ixgbe->gen_lock);
		return (IXGBE_SUCCESS);
	}

	atomic_and_32(&ixgbe->ixgbe_state, ~IXGBE_STARTED);

	ixgbe_stop(ixgbe, B_FALSE);

	if (ixgbe_start(ixgbe, B_FALSE) != IXGBE_SUCCESS) {
		mutex_exit(&ixgbe->gen_lock);
		return (IXGBE_FAILURE);
	}

	/*
	 * After resetting, need to recheck the link status.
	 */
	ixgbe->link_check_complete = B_FALSE;
	ixgbe->link_check_hrtime = gethrtime() +
	    (IXGBE_LINK_UP_TIME * 100000000ULL);

	atomic_or_32(&ixgbe->ixgbe_state, IXGBE_STARTED);

	if (!(ixgbe->ixgbe_state & IXGBE_SUSPENDED)) {
		for (i = 0; i < ixgbe->num_tx_rings; i++) {
			mac_tx_ring_update(ixgbe->mac_hdl,
			    ixgbe->tx_rings[i].ring_handle);
		}
	}

	mutex_exit(&ixgbe->gen_lock);

	/*
	 * Enable and start the watchdog timer
	 */
	ixgbe_enable_watchdog_timer(ixgbe);

	return (IXGBE_SUCCESS);
}

/*
 * ixgbe_tx_clean - Clean the pending transmit packets and DMA resources.
 */
static void
ixgbe_tx_clean(ixgbe_t *ixgbe)
{
	ixgbe_tx_ring_t *tx_ring;
	tx_control_block_t *tcb;
	link_list_t pending_list;
	uint32_t desc_num;
	int i, j;

	for (i = 0; i < ixgbe->num_tx_rings; i++) {
		tx_ring = &ixgbe->tx_rings[i];

		mutex_enter(&tx_ring->recycle_lock);

		/*
		 * If the work_list is NULL, there are no pending
		 * transactions, continue to the next ring.
		 */
		if (tx_ring->work_list == NULL) {
			mutex_exit(&tx_ring->recycle_lock);
			continue;
		}

		/*
		 * Build a pending list for each ring.
		 */
		LINK_LIST_INIT(&pending_list);

		/*
		 * Clean the pending tx data - the pending packets in the
		 * work_list that have no chances to be transmitted again.
		 *
		 * We must ensure the chipset is stopped or the link is down
		 * before cleaning the transmit packets.
		 */
		desc_num = 0;
		for (j = 0; j < tx_ring->ring_size; j++) {
			tcb = tx_ring->work_list[j];
			if (tcb != NULL) {
				desc_num += tcb->desc_num;
				tx_ring->work_list[j] = NULL;
				ixgbe_free_tcb(tcb);
				LIST_PUSH_TAIL(&pending_list, &tcb->link);
			}
		}

		if (desc_num > 0) {
			atomic_add_32(&tx_ring->tbd_free, desc_num);
			ASSERT(tx_ring->tbd_free == tx_ring->ring_size);

			/*
			 * Reset the head and tail pointers of the tbd ring;
			 * Reset the writeback head if it's enable.
			 */
			tx_ring->tbd_head = 0;
			tx_ring->tbd_tail = 0;
			if (ixgbe->tx_head_wb_enable)
				*tx_ring->tbd_head_wb = 0;

			IXGBE_WRITE_REG(&ixgbe->hw,
			    IXGBE_TDH(tx_ring->hw_index), 0);
			IXGBE_WRITE_REG(&ixgbe->hw,
			    IXGBE_TDT(tx_ring->hw_index), 0);
		}

		mutex_exit(&tx_ring->recycle_lock);

		/*
		 * Add the tx control blocks in the pending list to
		 * the free list.
		 */
		ixgbe_put_free_list(tx_ring, &pending_list);
	}
}

/*
 * ixgbe_tx_drain - Drain the tx rings to allow pending packets to be
 * transmitted.
 */
static boolean_t
ixgbe_tx_drain(ixgbe_t *ixgbe)
{
	ixgbe_tx_ring_t *tx_ring;
	boolean_t done;
	int i, j;

	/*
	 * Wait for a specific time to allow pending tx packets
	 * to be transmitted.
	 *
	 * Check the counter tbd_free to see if transmission is done.
	 * No lock protection is needed here.
	 *
	 * Return B_TRUE if all pending packets have been transmitted;
	 * Otherwise return B_FALSE;
	 */
	for (i = 0; i < TX_DRAIN_TIME; i++) {

		done = B_TRUE;
		for (j = 0; j < ixgbe->num_tx_rings; j++) {
			tx_ring = &ixgbe->tx_rings[j];
			done = done &&
			    (tx_ring->tbd_free == tx_ring->ring_size);
		}

		if (done)
			break;

		msec_delay(1);
	}

	return (done);
}

/*
 * ixgbe_rx_drain - Wait for all rx ddp buffers to be released by upper layer.
 */
static boolean_t
ixgbe_rx_drain(ixgbe_t *ixgbe)
{
	boolean_t done = B_TRUE;
	int i;

	/*
	 * Polling the ddp buffer free list to check if those buffers held by
	 * the upper layer are released.
	 *
	 * Check the counter ddp_buf_free to see if all pending buffers are
	 * released. No lock protection is needed here.
	 *
	 * Return B_TRUE if all pending buffers have been released;
	 * Otherwise return B_FALSE;
	 */
	for (i = 0; i < RX_DRAIN_TIME; i++) {
		done = (ixgbe->ddp_buf_pending == 0);

		if (done)
			break;

		msec_delay(1);
	}

	return (done);
}

/*
 * ixgbe_start - Start the driver/chipset.
 */
int
ixgbe_start(ixgbe_t *ixgbe, boolean_t alloc_buffer)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	int i;

	ASSERT(mutex_owned(&ixgbe->gen_lock));

	if (alloc_buffer) {
		if (ixgbe_alloc_rx_data(ixgbe) != IXGBE_SUCCESS) {
			ixgbe_error(ixgbe,
			    "Failed to allocate software receive rings");
			return (IXGBE_FAILURE);
		}

		/* Allocate buffers for all the rx/tx rings */
		if (ixgbe_alloc_dma(ixgbe) != IXGBE_SUCCESS) {
			ixgbe_error(ixgbe, "Failed to allocate DMA resource");
			return (IXGBE_FAILURE);
		}

		ixgbe->tx_ring_init = B_TRUE;
	} else {
		ixgbe->tx_ring_init = B_FALSE;
	}

	for (i = 0; i < ixgbe->num_rx_rings; i++)
		mutex_enter(&ixgbe->rx_rings[i].rx_lock);
	for (i = 0; i < ixgbe->num_tx_rings; i++)
		mutex_enter(&ixgbe->tx_rings[i].tx_lock);

	/*
	 * Start the chipset hardware
	 */
	if (!ixgbe->sriov_pf) {
		if (ixgbe_chip_start(ixgbe) != IXGBE_SUCCESS) {
			ixgbe_fm_ereport(ixgbe, DDI_FM_DEVICE_INVAL_STATE);
			goto start_failure;
		}
	}

	if (ixgbe_check_acc_handle(ixgbe->osdep.reg_handle) != DDI_FM_OK) {
		goto start_failure;
	}

#ifdef CONFIG_DCB
	/*
	 * Configure DCB
	 */
	ixgbe_config_dcb(ixgbe);
#endif

	/*
	 * Setup the rx/tx rings
	 */
	if (!ixgbe->sriov_pf) {
		ixgbe_setup_rx_global(ixgbe);
		ixgbe_setup_tx_global(ixgbe);
	}
	ixgbe_setup_rx_group(ixgbe);
	ixgbe_setup_tx_group(ixgbe);

	/*
	 * ixgbe_start() will be called when resetting, however if reset
	 * happens, we need to clear the ERROR, STALL and OVERTEMP flags
	 * before enabling the interrupts.
	 */
	atomic_and_32(&ixgbe->ixgbe_state, ~(IXGBE_ERROR
	    | IXGBE_STALL | IXGBE_OVERTEMP));

	/*
	 * Power on optics
	 */
	if (hw->mac.type == ixgbe_mac_82599EB &&
	    ixgbe_get_media_type(hw) == ixgbe_media_type_fiber)
		ixgbe_enable_tx_laser(hw);

	/*
	 * Enable adapter interrupts
	 * The interrupts must be enabled after the driver state is START
	 */
	if (ixgbe->sriov_pf && ixgbe->start_times) {
		/* interrupt types to enable */
		ixgbe->eims = IXGBE_READ_REG(hw, IXGBE_EIMS);
		ixgbe->eims |= IXGBE_EIMS_ENABLE_MASK;
		ixgbe->eims &= ~IXGBE_EIMS_TCP_TIMER;
		ixgbe->eims |= ixgbe->capab->other_intr;

		/* write to interrupt control registers */
		IXGBE_WRITE_REG(hw, IXGBE_EIMS, ixgbe->eims);
		IXGBE_WRITE_FLUSH(hw);
	} else {
		ixgbe->capab->enable_hw_intr(ixgbe);
	}

	ixgbe->start_times++;

	for (i = ixgbe->num_tx_rings - 1; i >= 0; i--)
		mutex_exit(&ixgbe->tx_rings[i].tx_lock);
	for (i = ixgbe->num_rx_rings - 1; i >= 0; i--)
		mutex_exit(&ixgbe->rx_rings[i].rx_lock);

	return (IXGBE_SUCCESS);

start_failure:
	for (i = ixgbe->num_tx_rings - 1; i >= 0; i--)
		mutex_exit(&ixgbe->tx_rings[i].tx_lock);
	for (i = ixgbe->num_rx_rings - 1; i >= 0; i--)
		mutex_exit(&ixgbe->rx_rings[i].rx_lock);

	ddi_fm_service_impact(ixgbe->dip, DDI_SERVICE_LOST);

	return (IXGBE_FAILURE);
}

/*
 * ixgbe_stop - Stop the driver/chipset.
 */
void
ixgbe_stop(ixgbe_t *ixgbe, boolean_t free_buffer)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	uint32_t eimc;
	int i;

	ASSERT(mutex_owned(&ixgbe->gen_lock));

	if (ixgbe->sriov_pf) {
		/*
		 * Enable mailbox interrupt, disable rx/tx interrupts
		 */
		/* Clear any pending interrupts */
		(void) IXGBE_READ_REG(hw, IXGBE_EICR);

		eimc = ixgbe->eims & ~IXGBE_EIMS_MAILBOX;

		IXGBE_WRITE_REG(hw, IXGBE_EIMC, eimc);
		IXGBE_WRITE_FLUSH(hw);
	} else {
		/*
		 * Disable the adapter interrupts
		 */
		ixgbe_disable_adapter_interrupts(ixgbe);
	}

	/*
	 * Drain the pending tx packets
	 */
	(void) ixgbe_tx_drain(ixgbe);

	if (ixgbe->sriov_pf) {
		/*
		 * Stop the rx rings
		 */
		for (i = 0; i < ixgbe->num_rx_rings; i++) {
			mutex_enter(&ixgbe->rx_rings[i].rx_lock);
			ixgbe_disable_rx_ring(&ixgbe->rx_rings[i]);
			mutex_exit(&ixgbe->rx_rings[i].rx_lock);
		}

		/*
		 * Stop the tx rings
		 */
		for (i = 0; i < ixgbe->num_tx_rings; i++) {
			mutex_enter(&ixgbe->tx_rings[i].tx_lock);
			ixgbe_disable_tx_ring(&ixgbe->tx_rings[i]);
			mutex_exit(&ixgbe->tx_rings[i].tx_lock);
		}
	}


	for (i = 0; i < ixgbe->num_rx_rings; i++)
		mutex_enter(&ixgbe->rx_rings[i].rx_lock);
	for (i = 0; i < ixgbe->num_tx_rings; i++)
		mutex_enter(&ixgbe->tx_rings[i].tx_lock);

	/*
	 * Stop the chipset hardware
	 */
	if (!ixgbe->sriov_pf)
		ixgbe_chip_stop(ixgbe);

	if (ixgbe_check_acc_handle(ixgbe->osdep.reg_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(ixgbe->dip, DDI_SERVICE_LOST);
	}

	/*
	 * Clean the pending tx data/resources
	 */
	ixgbe_tx_clean(ixgbe);

	for (i = ixgbe->num_tx_rings - 1; i >= 0; i--)
		mutex_exit(&ixgbe->tx_rings[i].tx_lock);
	for (i = ixgbe->num_rx_rings - 1; i >= 0; i--)
		mutex_exit(&ixgbe->rx_rings[i].rx_lock);

	if (ixgbe->link_state == LINK_STATE_UP) {
		ixgbe->link_state = LINK_STATE_UNKNOWN;
		mac_link_update(ixgbe->mac_hdl, ixgbe->link_state);
	}

	if (free_buffer) {
		/*
		 * Release the DMA/memory resources of rx/tx rings
		 */
		ixgbe_free_dma(ixgbe);
		ixgbe_free_rx_data(ixgbe);
	}
}

/*
 * ixgbe_cb_func - Driver interface for generic DDI callbacks
 */
/* ARGSUSED */
static int
ixgbe_cb_func(dev_info_t *dip, ddi_cb_action_t cbaction, void *cbarg,
    void *arg1, void *arg2)
{
	ixgbe_t			*ixgbe;
	pciv_config_vf_t	*vf_config;

	/* identify incoming pointers */
	ixgbe = (ixgbe_t *)arg1;

	switch (cbaction) {
	/* IRM callback */
	int count;
	case DDI_CB_INTR_ADD:
	case DDI_CB_INTR_REMOVE:
		count = (int)(uintptr_t)cbarg;
		ASSERT(ixgbe->intr_type == DDI_INTR_TYPE_MSIX);
		DTRACE_PROBE2(ixgbe__irm__callback, int, count,
		    int, ixgbe->intr_cnt);
		if (ixgbe_intr_adjust(ixgbe, cbaction, count) !=
		    DDI_SUCCESS) {
			ixgbe_error(ixgbe,
			    "IRM CB: Failed to adjust interrupts");
			goto cb_fail;
		}
		break;

	case DDI_CB_PCIV_CONFIG_VF:
		vf_config = (pciv_config_vf_t *)cbarg;
		switch (vf_config->cmd) {
		case PCIV_EVT_VFENABLE_PRE:
			IXGBE_DEBUGLOG_0(ixgbe,
			    "ixgbe_vf_config_handler: PCIV_EVT_VFENABLE_PRE");
			return (PCIV_REQREATTACH);

		case PCIV_EVT_VFENABLE_POST:
			IXGBE_DEBUGLOG_0(ixgbe,
			    "ixgbe_vf_config_handler: PCIV_EVT_VFENABLE_POST");
			IXGBE_DEBUGLOG_1(ixgbe, "number of VFs: %d",
			    vf_config->num_vf);

			break;

		case PCIV_EVT_VFDISABLE_PRE:
			IXGBE_DEBUGLOG_0(ixgbe,
			    "ixgbe_vf_config_handler: PCIV_EVT_VFDISABLE_PRE");
			break;

		case PCIV_EVT_VFDISABLE_POST:
			IXGBE_DEBUGLOG_0(ixgbe,
			    "ixgbe_vf_config_handler: PCIV_EVT_VFDISABLE_POST");

			break;

		default:
			break;
		}
		break;

	default:
		IXGBE_DEBUGLOG_1(ixgbe, "DDI CB: action 0x%x NOT supported",
		    cbaction);
		return (DDI_ENOTSUP);
	}
	return (DDI_SUCCESS);
cb_fail:
	return (DDI_FAILURE);
}

/*
 * ixgbe_intr_adjust - Adjust interrupt to respond to IRM request.
 */
static int
ixgbe_intr_adjust(ixgbe_t *ixgbe, ddi_cb_action_t cbaction, int count)
{
	int i, rc, actual;
	uint32_t state_backup;

	if (!(ixgbe->ixgbe_state & IXGBE_INITIALIZED)) {
		return (DDI_FAILURE);
	}

	if (count == 0)
		return (DDI_SUCCESS);

	if ((cbaction == DDI_CB_INTR_ADD &&
	    ixgbe->intr_cnt + count > ixgbe->intr_cnt_max) ||
	    (cbaction == DDI_CB_INTR_REMOVE &&
	    (int)(ixgbe->intr_cnt - count) < (int)ixgbe->intr_cnt_min))
		return (DDI_FAILURE);

	for (i = 0; i < ixgbe->num_rx_rings; i++)
		mac_ring_intr_set(ixgbe->rx_rings[i].ring_handle, NULL);
	for (i = 0; i < ixgbe->num_tx_rings; i++)
		mac_ring_intr_set(ixgbe->tx_rings[i].ring_handle, NULL);

	mutex_enter(&ixgbe->gen_lock);

	if (ixgbe->ixgbe_state & IXGBE_STARTED) {
		atomic_and_32(&ixgbe->ixgbe_state, ~IXGBE_STARTED);
		atomic_or_32(&ixgbe->ixgbe_state, IXGBE_INTR_ADJUST);
		atomic_or_32(&ixgbe->ixgbe_state, IXGBE_SUSPENDED);
		mac_link_update(ixgbe->mac_hdl, LINK_STATE_UNKNOWN);

		ixgbe_stop(ixgbe, B_FALSE);
		/*
		 * Backup ixgbe->ixgbe_state when it won't be changed.
		 */
		state_backup = ixgbe->ixgbe_state;
		state_backup |= IXGBE_STARTED;
		state_backup &= ~IXGBE_INTR_ADJUST;
		state_backup &= ~IXGBE_SUSPENDED;
	}

	/*
	 * Disable interrupts
	 */
	if (ixgbe->attach_progress & ATTACH_PROGRESS_ENABLE_INTR) {
		rc = ixgbe_disable_intrs(ixgbe);
		ASSERT(rc == IXGBE_SUCCESS);
	}
	ixgbe->attach_progress &= ~ATTACH_PROGRESS_ENABLE_INTR;

	/*
	 * Remove interrupt handlers
	 */
	if (ixgbe->attach_progress & ATTACH_PROGRESS_ADD_INTR) {
		ixgbe_rem_intr_handlers(ixgbe);
	}
	ixgbe->attach_progress &= ~ATTACH_PROGRESS_ADD_INTR;

	/*
	 * Clear vect_map
	 */
	bzero(&ixgbe->vect_map, sizeof (ixgbe->vect_map));
	switch (cbaction) {
	case DDI_CB_INTR_ADD:
		rc = ddi_intr_alloc(ixgbe->dip, ixgbe->htable,
		    DDI_INTR_TYPE_MSIX, ixgbe->intr_cnt, count, &actual,
		    DDI_INTR_ALLOC_NORMAL);
		if (rc != DDI_SUCCESS || actual != count) {
			ixgbe_log(ixgbe, "Adjust interrupts failed."
			    "return: %d, irm cb size: %d, actual: %d",
			    rc, count, actual);
			goto intr_adjust_fail;
		}
		ixgbe->intr_cnt += count;
		break;

	case DDI_CB_INTR_REMOVE:
		for (i = ixgbe->intr_cnt - count;
		    i < ixgbe->intr_cnt; i ++) {
			rc = ddi_intr_free(ixgbe->htable[i]);
			ixgbe->htable[i] = NULL;
			if (rc != DDI_SUCCESS) {
				ixgbe_log(ixgbe, "Adjust interrupts failed."
				    "return: %d, irm cb size: %d, actual: %d",
				    rc, count, actual);
				goto intr_adjust_fail;
			}
		}
		ixgbe->intr_cnt -= count;
		break;
	}

	/*
	 * Get priority for first vector, assume remaining are all the same
	 */
	rc = ddi_intr_get_pri(ixgbe->htable[0], &ixgbe->intr_pri);
	if (rc != DDI_SUCCESS) {
		ixgbe_log(ixgbe,
		    "Get interrupt priority failed: %d", rc);
		goto intr_adjust_fail;
	}
	rc = ddi_intr_get_cap(ixgbe->htable[0], &ixgbe->intr_cap);
	if (rc != DDI_SUCCESS) {
		ixgbe_log(ixgbe, "Get interrupt cap failed: %d", rc);
		goto intr_adjust_fail;
	}
	ixgbe->attach_progress |= ATTACH_PROGRESS_ALLOC_INTR;

	/*
	 * Map rings to interrupt vectors
	 */
	if (ixgbe_map_intrs_to_vectors(ixgbe) != IXGBE_SUCCESS) {
		ixgbe_error(ixgbe,
		    "IRM CB: Failed to map interrupts to vectors");
		goto intr_adjust_fail;
	}

	/*
	 * Add interrupt handlers
	 */
	if (ixgbe_add_intr_handlers(ixgbe) != IXGBE_SUCCESS) {
		ixgbe_error(ixgbe, "IRM CB: Failed to add interrupt handlers");
		goto intr_adjust_fail;
	}
	ixgbe->attach_progress |= ATTACH_PROGRESS_ADD_INTR;

	/*
	 * Now that mutex locks are initialized, and the chip is also
	 * initialized, enable interrupts.
	 */
	if (ixgbe_enable_intrs(ixgbe) != IXGBE_SUCCESS) {
		ixgbe_error(ixgbe, "IRM CB: Failed to enable DDI interrupts");
		goto intr_adjust_fail;
	}
	ixgbe->attach_progress |= ATTACH_PROGRESS_ENABLE_INTR;

	if (state_backup & IXGBE_STARTED) {
		if (ixgbe_start(ixgbe, B_FALSE) != IXGBE_SUCCESS) {
			ixgbe_error(ixgbe, "IRM CB: Failed to start");
			goto intr_adjust_fail;
		}
		/* Restore the state */
		ixgbe->ixgbe_state = state_backup;
	}

	mutex_exit(&ixgbe->gen_lock);

	for (i = 0; i < ixgbe->num_rx_rings; i++) {
		mac_ring_intr_set(ixgbe->rx_rings[i].ring_handle,
		    ixgbe->htable[ixgbe->rx_rings[i].intr_vector]);
	}
	for (i = 0; i < ixgbe->num_tx_rings; i++) {
		mac_ring_intr_set(ixgbe->tx_rings[i].ring_handle,
		    ixgbe->htable[ixgbe->tx_rings[i].intr_vector]);
	}

	/* Wakeup all Tx rings */
	for (i = 0; i < ixgbe->num_tx_rings; i++) {
		mac_tx_ring_update(ixgbe->mac_hdl,
		    ixgbe->tx_rings[i].ring_handle);
	}

	IXGBE_DEBUGLOG_3(ixgbe,
	    "IRM CB: interrupts new value: 0x%x(0x%x:0x%x).",
	    ixgbe->intr_cnt, ixgbe->intr_cnt_min, ixgbe->intr_cnt_max);
	return (DDI_SUCCESS);

intr_adjust_fail:
	ddi_fm_service_impact(ixgbe->dip, DDI_SERVICE_LOST);
	mutex_exit(&ixgbe->gen_lock);
	return (DDI_FAILURE);
}

/*
 * ixgbe_alloc_rings - Allocate memory space for rx/tx rings.
 */
static int
ixgbe_alloc_rings(ixgbe_t *ixgbe)
{
	/*
	 * Allocate memory space for rx rings
	 */
	ixgbe->rx_rings = kmem_zalloc(
	    sizeof (ixgbe_rx_ring_t) * ixgbe->num_rx_rings,
	    KM_NOSLEEP);

	if (ixgbe->rx_rings == NULL) {
		return (IXGBE_FAILURE);
	}

	/*
	 * Allocate memory space for tx rings
	 */
	ixgbe->tx_rings = kmem_zalloc(
	    sizeof (ixgbe_tx_ring_t) * ixgbe->num_tx_rings,
	    KM_NOSLEEP);

	if (ixgbe->tx_rings == NULL) {
		kmem_free(ixgbe->rx_rings,
		    sizeof (ixgbe_rx_ring_t) * ixgbe->num_rx_rings);
		ixgbe->rx_rings = NULL;
		return (IXGBE_FAILURE);
	}

	/*
	 * Allocate memory space for ring groups
	 */
	ixgbe->rx_groups = kmem_zalloc(
	    sizeof (ixgbe_group_t) * ixgbe->num_all_rx_groups,
	    KM_NOSLEEP);

	if (ixgbe->rx_groups == NULL) {
		kmem_free(ixgbe->rx_rings,
		    sizeof (ixgbe_rx_ring_t) * ixgbe->num_rx_rings);
		kmem_free(ixgbe->tx_rings,
		    sizeof (ixgbe_tx_ring_t) * ixgbe->num_tx_rings);
		ixgbe->rx_rings = NULL;
		ixgbe->tx_rings = NULL;
		return (IXGBE_FAILURE);
	}

	ixgbe->tx_groups = kmem_zalloc(
	    sizeof (ixgbe_group_t) * ixgbe->num_all_tx_groups,
	    KM_NOSLEEP);

	if (ixgbe->tx_groups == NULL) {
		kmem_free(ixgbe->rx_rings,
		    sizeof (ixgbe_rx_ring_t) * ixgbe->num_rx_rings);
		kmem_free(ixgbe->tx_rings,
		    sizeof (ixgbe_tx_ring_t) * ixgbe->num_tx_rings);
		kmem_free(ixgbe->rx_groups,
		    sizeof (ixgbe_group_t) * ixgbe->num_all_rx_groups);
		ixgbe->rx_rings = NULL;
		ixgbe->tx_rings = NULL;
		ixgbe->rx_groups = NULL;
		return (IXGBE_FAILURE);
	}

	return (IXGBE_SUCCESS);
}

/*
 * ixgbe_free_rings - Free the memory space of rx/tx rings.
 */
static void
ixgbe_free_rings(ixgbe_t *ixgbe)
{
	if (ixgbe->rx_rings != NULL) {
		kmem_free(ixgbe->rx_rings,
		    sizeof (ixgbe_rx_ring_t) * ixgbe->num_rx_rings);
		ixgbe->rx_rings = NULL;
	}

	if (ixgbe->tx_rings != NULL) {
		kmem_free(ixgbe->tx_rings,
		    sizeof (ixgbe_tx_ring_t) * ixgbe->num_tx_rings);
		ixgbe->tx_rings = NULL;
	}

	if (ixgbe->rx_groups != NULL) {
		kmem_free(ixgbe->rx_groups,
		    sizeof (ixgbe_group_t) * ixgbe->num_all_rx_groups);
		ixgbe->rx_groups = NULL;
	}

	if (ixgbe->tx_groups != NULL) {
		kmem_free(ixgbe->tx_groups,
		    sizeof (ixgbe_group_t) * ixgbe->num_all_tx_groups);
		ixgbe->tx_groups = NULL;
	}
}

static int
ixgbe_alloc_rx_data(ixgbe_t *ixgbe)
{
	ixgbe_rx_ring_t *rx_ring;
	int i;

	for (i = 0; i < ixgbe->num_rx_rings; i++) {
		rx_ring = &ixgbe->rx_rings[i];
		if (ixgbe_alloc_rx_ring_data(rx_ring) != IXGBE_SUCCESS)
			goto alloc_rx_data_failure;
	}

	if (ixgbe->fcoe_lro_enable) {
		if (ixgbe_alloc_rx_fcoe_data(ixgbe) != IXGBE_SUCCESS)
			goto alloc_rx_data_failure;
	}

	return (IXGBE_SUCCESS);

alloc_rx_data_failure:
	ixgbe_free_rx_data(ixgbe);
	return (IXGBE_FAILURE);
}

static void
ixgbe_free_rx_data(ixgbe_t *ixgbe)
{
	ixgbe_rx_ring_t *rx_ring;
	ixgbe_rx_data_t *rx_data;
	ixgbe_rx_fcoe_t *rx_fcoe;
	int i;

	for (i = 0; i < ixgbe->num_rx_rings; i++) {
		rx_ring = &ixgbe->rx_rings[i];
		rx_data = rx_ring->rx_data;

		if (rx_data != NULL) {
			rx_data->flag |= IXGBE_RX_STOPPED;
			ixgbe_free_rx_ring_data(rx_data);
			rx_ring->rx_data = NULL;
		}
	}

	if (ixgbe->fcoe_lro_enable) {
		mutex_enter(&ixgbe->rx_pending_lock);
		rx_fcoe = ixgbe->rx_fcoe;
		if (rx_fcoe != NULL) {
			rx_fcoe->flag |= IXGBE_RX_STOPPED;

			if (rx_fcoe->ddp_buf_pending == 0) {
				ixgbe_free_rx_fcoe_data(rx_fcoe);
				ixgbe->rx_fcoe = NULL;
			}
		}
		mutex_exit(&ixgbe->rx_pending_lock);
	}
}

#ifdef CONFIG_DCB
static void
ixgbe_config_dcb(ixgbe_t *ixgbe)
{
	if (ixgbe->dcb_config.num_tcs.pg_tcs != 0) {
		(void) ixgbe_dcb_check_config(&ixgbe->dcb_config);

		(void) ixgbe_dcb_calculate_tc_credits(&ixgbe->hw,
		    &ixgbe->dcb_config, ETHERMAX, DCB_RX_CONFIG);
		(void) ixgbe_dcb_calculate_tc_credits(&ixgbe->hw,
		    &ixgbe->dcb_config, ETHERMAX, DCB_TX_CONFIG);
		(void) ixgbe_dcb_hw_config(&ixgbe->hw, &ixgbe->dcb_config);
		(void) ixgbe_dcb_config_tc_stats(&ixgbe->hw);

		ixgbe->hw.fc.current_mode = ixgbe_fc_pfc;
	}
}
#endif

static void
ixgbe_disable_rx_ring(ixgbe_rx_ring_t *rx_ring)
{
	struct ixgbe_hw *hw = &rx_ring->ixgbe->hw;
	uint32_t rxdctl;

	/*
	 * Setup the Receive Descriptor Control Register (RXDCTL)
	 */
	rxdctl = IXGBE_READ_REG(hw, IXGBE_RXDCTL(rx_ring->hw_index));
	rxdctl &= ~IXGBE_RXDCTL_ENABLE;
	IXGBE_WRITE_REG(hw, IXGBE_RXDCTL(rx_ring->hw_index), rxdctl);
}

static void
ixgbe_setup_rx_ring(ixgbe_rx_ring_t *rx_ring)
{
	ixgbe_t *ixgbe = rx_ring->ixgbe;
	ixgbe_rx_data_t *rx_data = rx_ring->rx_data;
	struct ixgbe_hw *hw = &ixgbe->hw;
	rx_control_block_t *rcb;
	union ixgbe_adv_rx_desc	*rbd;
	uint32_t size;
	uint32_t buf_low;
	uint32_t buf_high;
	uint32_t reg_val;
	int i;

	ASSERT(mutex_owned(&rx_ring->rx_lock));
	ASSERT(mutex_owned(&ixgbe->gen_lock));

	if (rx_ring->rx_data == NULL)
		return;

	for (i = 0; i < ixgbe->rx_ring_size; i++) {
		rcb = rx_data->work_list[i];
		rbd = &rx_data->rbd_ring[i];

		rbd->read.pkt_addr = rcb->rx_buf.dma_address;
		rbd->read.hdr_addr = NULL;
	}

	/*
	 * Initialize the length register
	 */
	size = rx_data->ring_size * sizeof (union ixgbe_adv_rx_desc);
	IXGBE_WRITE_REG(hw, IXGBE_RDLEN(rx_ring->hw_index), size);

	/*
	 * Initialize the base address registers
	 */
	buf_low = (uint32_t)rx_data->rbd_area.dma_address;
	buf_high = (uint32_t)(rx_data->rbd_area.dma_address >> 32);
	IXGBE_WRITE_REG(hw, IXGBE_RDBAH(rx_ring->hw_index), buf_high);
	IXGBE_WRITE_REG(hw, IXGBE_RDBAL(rx_ring->hw_index), buf_low);

	/*
	 * Setup head & tail pointers
	 */
	IXGBE_WRITE_REG(hw, IXGBE_RDT(rx_ring->hw_index),
	    rx_data->ring_size - 1);
	IXGBE_WRITE_REG(hw, IXGBE_RDH(rx_ring->hw_index), 0);

	rx_data->rbd_next = 0;
	rx_data->lro_first = 0;

	/*
	 * Setup the Receive Descriptor Control Register (RXDCTL)
	 * PTHRESH=32 descriptors (half the internal cache)
	 * HTHRESH=0 descriptors (to minimize latency on fetch)
	 * WTHRESH defaults to 1 (writeback each descriptor)
	 */
	reg_val = IXGBE_READ_REG(hw, IXGBE_RXDCTL(rx_ring->hw_index));
	reg_val |= IXGBE_RXDCTL_ENABLE;	/* enable queue */

	/* Not a valid value for 82599/X540 */
	if (hw->mac.type == ixgbe_mac_82598EB) {
		reg_val |= 0x100808;	/* pthresh */
	}
	IXGBE_WRITE_REG(hw, IXGBE_RXDCTL(rx_ring->hw_index), reg_val);

	if (hw->mac.type >= ixgbe_mac_82599EB) {
		reg_val = IXGBE_READ_REG(hw, IXGBE_RDRXCTL);
		reg_val |= (IXGBE_RDRXCTL_CRCSTRIP | IXGBE_RDRXCTL_AGGDIS);
		IXGBE_WRITE_REG(hw, IXGBE_RDRXCTL, reg_val);
	}

	/*
	 * Setup the Split and Replication Receive Control Register.
	 * Set the rx buffer size and the advanced descriptor type.
	 */
	reg_val = (ixgbe->rx_buf_size >> IXGBE_SRRCTL_BSIZEPKT_SHIFT) |
	    IXGBE_SRRCTL_DESCTYPE_ADV_ONEBUF;
	reg_val |= IXGBE_SRRCTL_DROP_EN;
	IXGBE_WRITE_REG(hw, IXGBE_SRRCTL(rx_ring->hw_index), reg_val);
}

static void
ixgbe_setup_rx_group(ixgbe_t *ixgbe)
{
	ixgbe_rx_ring_t *rx_ring;
	struct ixgbe_hw *hw = &ixgbe->hw;
	uint32_t ring_mapping = 0;
	uint32_t i, index;

	/*
	 * ixgbe_setup_rx_ring must be called after configuring RXCTRL
	 */
	for (i = 0; i < ixgbe->num_rx_rings; i++) {
		rx_ring = &ixgbe->rx_rings[i];

		/* make sure the rx ring is disabled */
		ixgbe_disable_rx_ring(rx_ring);

		ixgbe_setup_rx_ring(rx_ring);

		/*
		 * Setup the per-ring statistics mapping for non-DCB mode.
		 */
		if (ixgbe->dcb_config.num_tcs.pg_tcs == 0) {
			index = rx_ring->hw_index;
			ring_mapping = IXGBE_READ_REG(hw,
			    IXGBE_RQSMR(index >> 2));
			ring_mapping |= (i & 0xF) << (8 * (index & 0x3));
			IXGBE_WRITE_REG(hw, IXGBE_RQSMR(index >> 2),
			    ring_mapping);
		}
	}
}

static void
ixgbe_setup_rx_global(ixgbe_t *ixgbe)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	uint32_t reg_val;
	uint32_t i;
	uint32_t psrtype_rss_bit;

	/* PSRTYPE must be configured for 82599/X540 */
	if (ixgbe->classify_mode != IXGBE_CLASSIFY_VT &&
	    ixgbe->classify_mode != IXGBE_CLASSIFY_VT_RSS) {
		reg_val = IXGBE_PSRTYPE_TCPHDR | IXGBE_PSRTYPE_UDPHDR |
		    IXGBE_PSRTYPE_IPV4HDR | IXGBE_PSRTYPE_IPV6HDR;
		reg_val |= IXGBE_PSRTYPE_L2HDR;
		reg_val |= 0x80000000;
		IXGBE_WRITE_REG(hw, IXGBE_PSRTYPE(0), reg_val);
	} else {
		if (ixgbe->num_all_rx_groups > 32) {
			psrtype_rss_bit = 0x20000000;
		} else {
			psrtype_rss_bit = 0x40000000;
		}
		reg_val = IXGBE_PSRTYPE_TCPHDR | IXGBE_PSRTYPE_UDPHDR |
		    IXGBE_PSRTYPE_IPV4HDR | IXGBE_PSRTYPE_IPV6HDR;
		reg_val |= IXGBE_PSRTYPE_L2HDR;
		reg_val |= psrtype_rss_bit;

		if (ixgbe->sriov_pf)
			IXGBE_WRITE_REG(hw, IXGBE_PSRTYPE(ixgbe->pf_grp),
			    reg_val);
		else {
			for (i = 0; i < ixgbe->capab->max_grp_num; i++)
				IXGBE_WRITE_REG(hw, IXGBE_PSRTYPE(i), reg_val);
		}
	}

	/*
	 * Set filter control in FCTRL to accept broadcast packets and do
	 * not pass pause frames to host.  Flow control settings are already
	 * in this register, so preserve them.
	 */
	reg_val = IXGBE_READ_REG(hw, IXGBE_FCTRL);
	reg_val |= IXGBE_FCTRL_BAM;	/* broadcast accept mode */
	reg_val |= IXGBE_FCTRL_DPF;	/* discard pause frames */
	IXGBE_WRITE_REG(hw, IXGBE_FCTRL, reg_val);

	ixgbe_setup_vlan(hw);

	/*
	 * Hardware checksum settings
	 */
	if (ixgbe->rx_hcksum_enable) {
		reg_val = IXGBE_RXCSUM_IPPCSE;	/* IP checksum */
		IXGBE_WRITE_REG(hw, IXGBE_RXCSUM, reg_val);
	}

	/* Force drop enable for all queues in SR-IOV mode */
	if (ixgbe->sriov_pf) {
		for (i = 0; i < 128; i++) {
			reg_val = IXGBE_QDE_ENABLE | (i << IXGBE_QDE_IDX_SHIFT);
			/* Set Write Enable to write to queue context */
			reg_val |= (1 << 16);
			IXGBE_WRITE_REG(hw, IXGBE_QDE, reg_val);
		}
	}

	/*
	 * Setup Virtualization and RSS for multiple receive queues
	 */
	ixgbe->capab->setup_rx_classify(ixgbe);

	/*
	 * Enable the receive unit.  This must be done after filter
	 * control is set in FCTRL.
	 */
	reg_val = (IXGBE_RXCTRL_RXEN	/* Enable Receive Unit */
	    | IXGBE_RXCTRL_DMBYPS);	/* descriptor monitor bypass */
	IXGBE_WRITE_REG(hw, IXGBE_RXCTRL, reg_val);

	/*
	 * The Max Frame Size in MHADD/MAXFRS will be internally increased
	 * by four bytes if the packet has a VLAN field, so includes MTU,
	 * ethernet header and frame check sequence.
	 * Register is MAXFRS in 82599/X540.
	 */
	reg_val = (ixgbe->default_mtu + sizeof (struct ether_header)
	    + ETHERFCSL) << IXGBE_MHADD_MFS_SHIFT;
	IXGBE_WRITE_REG(hw, IXGBE_MHADD, reg_val);

	/*
	 * Setup Jumbo Frame enable bit
	 */
	if (ixgbe->default_mtu > ETHERMTU) {
		reg_val = IXGBE_READ_REG(hw, IXGBE_HLREG0);
		reg_val |= IXGBE_HLREG0_JUMBOEN;
		IXGBE_WRITE_REG(hw, IXGBE_HLREG0, reg_val);
	}

	/*
	 * Setup RSC for multiple receive queues.
	 */
	if (ixgbe->lro_enable) {
		for (i = 0; i < ixgbe->num_rx_rings; i++) {
			/*
			 * Make sure rx_buf_size * MAXDESC not greater
			 * than 65535.
			 * Intel recommends 4 for MAXDESC field value.
			 */
			reg_val = IXGBE_READ_REG(hw, IXGBE_RSCCTL(i));
			reg_val |= IXGBE_RSCCTL_RSCEN;
			if (ixgbe->rx_buf_size == IXGBE_PKG_BUF_16k)
				reg_val |= IXGBE_RSCCTL_MAXDESC_1;
			else
				reg_val |= IXGBE_RSCCTL_MAXDESC_4;
			IXGBE_WRITE_REG(hw,  IXGBE_RSCCTL(i), reg_val);
		}

		reg_val = IXGBE_READ_REG(hw, IXGBE_RSCDBU);
		reg_val |= IXGBE_RSCDBU_RSCACKDIS;
		IXGBE_WRITE_REG(hw, IXGBE_RSCDBU, reg_val);

		reg_val = IXGBE_READ_REG(hw, IXGBE_RDRXCTL);
		reg_val |= IXGBE_RDRXCTL_RSCACKC;
		reg_val |= IXGBE_RDRXCTL_FCOE_WRFIX;
		reg_val &= ~IXGBE_RDRXCTL_RSCFRSTSIZE;

		IXGBE_WRITE_REG(hw, IXGBE_RDRXCTL, reg_val);
	}

	/*
	 * Setup FCoE RX CRC offload
	 */
	if (ixgbe->fcoe_rxcrc_enable) {
		ixgbe_fcoe_enable_rxcrc(ixgbe);
	}

	/*
	 * Setup FCoE LRO
	 */
	if (ixgbe->fcoe_lro_enable) {
		ixgbe_fcoe_enable_lro(ixgbe);
	}

	/*
	 * Setup FCoE redirection table
	 */
	if (ixgbe->fcoe_rxmr_enable) {
		ixgbe_fcoe_enable_rxmr(ixgbe);
	}
}

static void
ixgbe_disable_tx_ring(ixgbe_tx_ring_t *tx_ring)
{
	struct ixgbe_hw *hw = &tx_ring->ixgbe->hw;
	uint32_t txdctl;

	/*
	 * Setup the Transmit Descriptor Control Register (TXDCTL)
	 */
	txdctl = IXGBE_READ_REG(hw, IXGBE_TXDCTL(tx_ring->hw_index));
	txdctl &= ~IXGBE_TXDCTL_ENABLE;
	IXGBE_WRITE_REG(hw, IXGBE_TXDCTL(tx_ring->hw_index), txdctl);
}

static void
ixgbe_setup_tx_ring(ixgbe_tx_ring_t *tx_ring)
{
	ixgbe_t *ixgbe = tx_ring->ixgbe;
	struct ixgbe_hw *hw = &ixgbe->hw;
	uint32_t size;
	uint32_t buf_low;
	uint32_t buf_high;
	uint32_t reg_val;

	ASSERT(mutex_owned(&tx_ring->tx_lock));
	ASSERT(mutex_owned(&ixgbe->gen_lock));

	if (tx_ring->free_list == NULL)
		return;

	/*
	 * Initialize the length register
	 */
	size = tx_ring->ring_size * sizeof (union ixgbe_adv_tx_desc);
	IXGBE_WRITE_REG(hw, IXGBE_TDLEN(tx_ring->hw_index), size);

	/*
	 * Initialize the base address registers
	 */
	buf_low = (uint32_t)tx_ring->tbd_area.dma_address;
	buf_high = (uint32_t)(tx_ring->tbd_area.dma_address >> 32);
	IXGBE_WRITE_REG(hw, IXGBE_TDBAL(tx_ring->hw_index), buf_low);
	IXGBE_WRITE_REG(hw, IXGBE_TDBAH(tx_ring->hw_index), buf_high);

	/*
	 * Setup head & tail pointers
	 */
	IXGBE_WRITE_REG(hw, IXGBE_TDH(tx_ring->hw_index), 0);
	IXGBE_WRITE_REG(hw, IXGBE_TDT(tx_ring->hw_index), 0);

	/*
	 * Setup head write-back
	 */
	if (ixgbe->tx_head_wb_enable) {
		/*
		 * The memory of the head write-back is allocated using
		 * the extra tbd beyond the tail of the tbd ring.
		 */
		tx_ring->tbd_head_wb = (uint32_t *)
		    ((uintptr_t)tx_ring->tbd_area.address + size);
		*tx_ring->tbd_head_wb = 0;

		buf_low = (uint32_t)
		    (tx_ring->tbd_area.dma_address + size);
		buf_high = (uint32_t)
		    ((tx_ring->tbd_area.dma_address + size) >> 32);

		/* Set the head write-back enable bit */
		buf_low |= IXGBE_TDWBAL_HEAD_WB_ENABLE;

		IXGBE_WRITE_REG(hw, IXGBE_TDWBAL(tx_ring->hw_index), buf_low);
		IXGBE_WRITE_REG(hw, IXGBE_TDWBAH(tx_ring->hw_index), buf_high);

		/*
		 * Turn off relaxed ordering for head write back or it will
		 * cause problems with the tx recycling
		 */
		reg_val = IXGBE_READ_REG(hw,
		    IXGBE_DCA_TXCTRL(tx_ring->hw_index));
		reg_val &= ~IXGBE_DCA_TXCTRL_TX_WB_RO_EN;
		IXGBE_WRITE_REG(hw,
		    IXGBE_DCA_TXCTRL(tx_ring->hw_index), reg_val);
	} else {
		tx_ring->tbd_head_wb = NULL;
	}

	tx_ring->tbd_head = 0;
	tx_ring->tbd_tail = 0;
	tx_ring->tbd_free = tx_ring->ring_size;

	if (ixgbe->tx_ring_init == B_TRUE) {
		tx_ring->tcb_head = 0;
		tx_ring->tcb_tail = 0;
		tx_ring->tcb_free = tx_ring->free_list_size;
	}

	/*
	 * Initialize the s/w context structure
	 */
	bzero(&tx_ring->tx_context, sizeof (ixgbe_tx_context_t));

	/*
	 * Enabling tx queues ..
	 * For 82599/X540 must be done after DMATXCTL.TE is set
	 */
	reg_val = IXGBE_READ_REG(hw, IXGBE_TXDCTL(tx_ring->hw_index));

	/*
	 * set WTHRESH for 82599/X540 if head write-back is disabled.
	 */
	if ((hw->mac.type >= ixgbe_mac_82599EB) &&
	    (!ixgbe->tx_head_wb_enable)) {
		/*
		 * TXDCTL[i] field values:
		 * WTHRESH (bits 22:16) = 4, HTHRESH (bits 14:8) = 0,
		 * PTHRESH (bits 6:0) = 0.
		 * WTHRESH is set to 4 to avoid partial cache
		 * line writes and minimize the PCIe overhead.
		 */
		reg_val &= ~0x7fffff;
		reg_val |= 0x40000;
		tx_ring->wthresh_nonzero = B_TRUE;
	}
	reg_val |= IXGBE_TXDCTL_ENABLE;
	IXGBE_WRITE_REG(hw, IXGBE_TXDCTL(tx_ring->hw_index), reg_val);
}

static void
ixgbe_setup_tx_group(ixgbe_t *ixgbe)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	ixgbe_tx_ring_t *tx_ring;
	uint32_t ring_mapping;
	int i;

	for (i = 0; i < ixgbe->num_tx_rings; i++) {
		tx_ring = &ixgbe->tx_rings[i];

		/* make sure the tx ring is disabled */
		ixgbe_disable_tx_ring(tx_ring);

		ixgbe_setup_tx_ring(tx_ring);
	}

	/*
	 * Setup the per-ring statistics mapping for non-DCB mode.
	 */
	if (ixgbe->dcb_config.num_tcs.pg_tcs == 0) {
		ring_mapping = 0;
		for (i = 0; i < ixgbe->num_tx_rings; i++) {
			ring_mapping |= (i & 0xF) << (8 * (i & 0x3));
			if ((i & 0x3) == 0x3) {
				switch (hw->mac.type) {
				case ixgbe_mac_82598EB:
					IXGBE_WRITE_REG(hw, IXGBE_TQSMR(i >> 2),
					    ring_mapping);
					break;

				case ixgbe_mac_82599EB:
				case ixgbe_mac_X540:
					IXGBE_WRITE_REG(hw, IXGBE_TQSM(i >> 2),
					    ring_mapping);
					break;

				default:
					break;
				}

				ring_mapping = 0;
			}
		}
		if (i & 0x3) {
			switch (hw->mac.type) {
			case ixgbe_mac_82598EB:
				IXGBE_WRITE_REG(hw, IXGBE_TQSMR(i >> 2),
				    ring_mapping);
				break;

			case ixgbe_mac_82599EB:
			case ixgbe_mac_X540:
				IXGBE_WRITE_REG(hw, IXGBE_TQSM(i >> 2),
				    ring_mapping);
				break;

			default:
				break;
			}
		}
	}
}

static void
ixgbe_setup_tx_global(ixgbe_t *ixgbe)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	uint32_t reg_val;

	/*
	 * Enable CRC appending and TX padding (for short tx frames)
	 */
	reg_val = IXGBE_READ_REG(hw, IXGBE_HLREG0);
	reg_val |= IXGBE_HLREG0_TXCRCEN | IXGBE_HLREG0_TXPADEN;
	IXGBE_WRITE_REG(hw, IXGBE_HLREG0, reg_val);

	/*
	 * Setup multiple transmit queue mode
	 */
	if ((ixgbe->sriov_pf || ixgbe->dcb_config.num_tcs.pg_tcs) &&
	    ixgbe->capab->setup_tx_classify != NULL)
		ixgbe->capab->setup_tx_classify(ixgbe);

	/*
	 * enable DMA for 82599/X540 parts
	 */
	if (hw->mac.type >= ixgbe_mac_82599EB) {
	/* DMATXCTL.TE must be set after all Tx config is complete */
		reg_val = IXGBE_READ_REG(hw, IXGBE_DMATXCTL);
		reg_val |= IXGBE_DMATXCTL_TE;
		IXGBE_WRITE_REG(hw, IXGBE_DMATXCTL, reg_val);
	}
}

static void
ixgbe_setup_vlan(struct ixgbe_hw *hw)
{
	uint32_t vlnctrl, i;

	/* enable VLAN tag insert/strip */
	vlnctrl = IXGBE_READ_REG(hw, IXGBE_VLNCTRL);
	if (!(vlnctrl & IXGBE_VLNCTRL_VFE)) {
		vlnctrl |= IXGBE_VLNCTRL_VFE;
		IXGBE_WRITE_REG(hw, IXGBE_VLNCTRL, vlnctrl);
	}

	/* enable all the VFTA bits */
	for (i = 0; i < 128; i ++)
		IXGBE_WRITE_REG(hw, IXGBE_VFTA(i), 0xFFFFFFFF);
}

static void
ixgbe_setup_rx_classify_82598eb(ixgbe_t *ixgbe)
{
	switch (ixgbe->classify_mode) {
	case IXGBE_CLASSIFY_RSS:
	case IXGBE_CLASSIFY_RSS_DCB:
		/*
		 * One group, only RSS is needed when more than
		 * one ring enabled.
		 */
		ixgbe_setup_rss_dcb(ixgbe);
		break;

	case IXGBE_CLASSIFY_VT:
		/*
		 * Multiple groups, each group has one ring,
		 * only VMDq is needed.
		 * Setup the VMDq Control register, enable VMDq based on
		 * packet destination MAC address:
		 * VMDq Enable = 1;
		 * VMDq Filter = 0; MAC filtering
		 * Default VMDq output index = 0;
		 */
		ixgbe_setup_rx_vt_82598eb(ixgbe);
		break;

	case IXGBE_CLASSIFY_VT_RSS:
		/*
		 * Multiple groups and multiple rings, both
		 * VMDq and RSS are needed.
		 */
		ixgbe_setup_rx_vt_rss_82598eb(ixgbe);
		break;

	default:
		break;
	}
}

static void
ixgbe_setup_rx_classify_generic(ixgbe_t *ixgbe)
{
	switch (ixgbe->classify_mode) {
	case IXGBE_CLASSIFY_DCB:
		ixgbe_setup_dcb(ixgbe);
		break;
	case IXGBE_CLASSIFY_RSS:
	case IXGBE_CLASSIFY_RSS_DCB:
		/*
		 * One group, only RSS is needed when more than
		 * one ring enabled.
		 */
		ixgbe_setup_rss_dcb(ixgbe);
		break;
	case IXGBE_CLASSIFY_VT:
		/*
		 * Multiple groups, each group has one ring,
		 * only VT is needed.
		 * Setup the VT Control register, enable VT based on
		 * packet destination MAC address:
		 */
		ixgbe_setup_rx_vt_generic(ixgbe);
		break;
	case IXGBE_CLASSIFY_VT_RSS:
		/*
		 * Multiple groups and multiple rings, both
		 * VT and RSS are needed.
		 */
		ixgbe_setup_rx_vt_rss_generic(ixgbe);
		break;

	default:
		break;
	}
}

static void
ixgbe_setup_dcb(ixgbe_t *ixgbe)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	uint32_t mrqc;

	switch (ixgbe->dcb_config.num_tcs.pg_tcs) {
	case MAX_TRAFFIC_CLASS:
		mrqc = IXGBE_MRQC_RT8TCEN;
		break;
	case MAX_TRAFFIC_CLASS / 2:
		mrqc = IXGBE_MRQC_RT4TCEN;
		break;
	default:
		return;
	}

	IXGBE_WRITE_REG(hw, IXGBE_MRQC, mrqc);
}

/*
 * Used when rss_fixed_seed_enable is on. It enables RSS to use the
 * same ring for a packet with the same 5-tuple, across system reboots.
 * This is useful for peformance benchmarks to limit the amount of variation.
 */
uint32_t fixed_seed[] = {0xcd97c365, 0xc716c020, 0x40984f99, 0xca2bccd0,
    0xd181659c, 0x8eed1409, 0x1948052f, 0x367fed63, 0x6e6b768f, 0x14e21be2};

/*
 * ixgbe_setup_rss_dcb - Setup receive-side scaling and DCB feature.
 */
static void
ixgbe_setup_rss_dcb(ixgbe_t *ixgbe)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	uint32_t i, mrqc, rxcsum;
	uint32_t random;
	uint32_t reta;
	uint32_t ring_per_unit;
	int num_tcs = ixgbe->dcb_config.num_tcs.pg_tcs;

	/*
	 * Fill out redirection table
	 */
	reta = 0;
	if (ixgbe->classify_mode == IXGBE_CLASSIFY_RSS_DCB) {
		ASSERT(num_tcs != 0);
		ring_per_unit = ixgbe->num_rx_rings / num_tcs;
	} else {
		ring_per_unit = ixgbe->num_rx_rings / ixgbe->num_pf_rx_groups;
	}

	for (i = 0; i < 128; i++) {
		reta = (reta << 8) | (i % ring_per_unit) |
		    ((i % ring_per_unit) << 4);
		if ((i & 3) == 3)
			IXGBE_WRITE_REG(hw, IXGBE_RETA(i >> 2), reta);
	}

	/*
	 * Fill out hash function seeds with a random constant
	 */
	for (i = 0; i < 10; i++) {
		if (ixgbe->rss_fixed_seed_enable) {
			random = fixed_seed[i];
		} else {
			(void) random_get_pseudo_bytes((uint8_t *)&random,
			    sizeof (uint32_t));
		}
		IXGBE_WRITE_REG(hw, IXGBE_RSSRK(i), random);
	}

	/*
	 * Enable RSS & perform hash on these packet types
	 */
	switch (num_tcs) {
	case 0:
		mrqc = IXGBE_MRQC_RSSEN;
		break;
	case MAX_TRAFFIC_CLASS:
		ASSERT(hw->mac.type >= ixgbe_mac_82599EB);
		mrqc = IXGBE_MRQC_RTRSS8TCEN;
		break;
	case MAX_TRAFFIC_CLASS / 2:
		ASSERT(hw->mac.type >= ixgbe_mac_82599EB);
		mrqc = IXGBE_MRQC_RTRSS4TCEN;
		break;
	}
	mrqc |= IXGBE_MRQC_RSS_FIELD_IPV4 |
	    IXGBE_MRQC_RSS_FIELD_IPV4_TCP |
	    IXGBE_MRQC_RSS_FIELD_IPV6 |
	    IXGBE_MRQC_RSS_FIELD_IPV6_TCP;
	if (ixgbe->rss_udp_enable)
		mrqc |= (IXGBE_MRQC_RSS_FIELD_IPV4_UDP |
		    IXGBE_MRQC_RSS_FIELD_IPV6_UDP);
	IXGBE_WRITE_REG(hw, IXGBE_MRQC, mrqc);

	/*
	 * Disable Packet Checksum to enable RSS for multiple receive queues.
	 * It is an adapter hardware limitation that Packet Checksum is
	 * mutually exclusive with RSS.
	 */
	rxcsum = IXGBE_READ_REG(hw, IXGBE_RXCSUM);
	rxcsum |= IXGBE_RXCSUM_PCSD;
	rxcsum &= ~IXGBE_RXCSUM_IPPCSE;
	IXGBE_WRITE_REG(hw, IXGBE_RXCSUM, rxcsum);
}

static void
ixgbe_setup_rx_vt_82598eb(ixgbe_t *ixgbe)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	uint32_t vmdctl;

	/*
	 * VMDq Enable = 1;
	 * VMDq Filter = 0; MAC filtering
	 * Default VMDq output index = 0;
	 */
	vmdctl = IXGBE_VMD_CTL_VMDQ_EN;
	IXGBE_WRITE_REG(hw, IXGBE_VMD_CTL, vmdctl);
}

static void
ixgbe_setup_rx_vt_generic(ixgbe_t *ixgbe)
{
	uint32_t vmdctl, i, vtctl;
	struct ixgbe_hw *hw = &ixgbe->hw;
	uint32_t vt_reg_bits;

	/*
	 * Enable VT-only.
	 */
	vmdctl = IXGBE_MRQC_VMDQEN;
	IXGBE_WRITE_REG(hw, IXGBE_MRQC, vmdctl);

	for (i = 0; i < hw->mac.num_rar_entries; i++) {
		IXGBE_WRITE_REG(hw, IXGBE_MPSAR_LO(i), 0);
		IXGBE_WRITE_REG(hw, IXGBE_MPSAR_HI(i), 0);
	}

	/*
	 * Enable Virtualization and Replication.
	 */
	vtctl = IXGBE_VT_CTL_VT_ENABLE | IXGBE_VT_CTL_REPLEN;

	if (ixgbe->sriov_pf) {
		vt_reg_bits = (ixgbe->pf_grp << IXGBE_VT_CTL_POOL_SHIFT);
		vtctl |= vt_reg_bits;
	}
	IXGBE_WRITE_REG(hw, IXGBE_VT_CTL, vtctl);

	/*
	 * Enable receiving packets to all VFs
	 */
	IXGBE_WRITE_REG(hw, IXGBE_VFRE(0), IXGBE_VFRE_ENABLE_ALL);
	IXGBE_WRITE_REG(hw, IXGBE_VFRE(1), IXGBE_VFRE_ENABLE_ALL);

	if (ixgbe->sriov_pf) {
		IXGBE_WRITE_REG(hw, IXGBE_VFTE(0), IXGBE_VFRE_ENABLE_ALL);
		IXGBE_WRITE_REG(hw, IXGBE_VFTE(1), IXGBE_VFRE_ENABLE_ALL);
		IXGBE_WRITE_REG(hw, IXGBE_PFDTXGSWC, IXGBE_PFDTXGSWC_VT_LBEN);
		for (i = 0; i < ixgbe->iov_mode; i ++) {
			ixgbe_set_vmolr(ixgbe, i);
		}
	}
}

/*
 * ixgbe_setup_rx_vt_rss_82598eb - Setup both vt feature and rss feature
 * for 82598eb.
 */
static void
ixgbe_setup_rx_vt_rss_82598eb(ixgbe_t *ixgbe)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	uint32_t i, mrqc, vmdctl, rxcsum;
	uint32_t random;
	uint32_t reta;
	uint32_t ring_per_group;

	/*
	 * Fill out redirection table
	 */
	reta = 0;
	ring_per_group = ixgbe->num_rx_rings / ixgbe->num_pf_rx_groups;
	for (i = 0; i < 128; i++) {
		reta = (reta << 8) | (i % ring_per_group) |
		    ((i % ring_per_group) << 4);
		if ((i & 3) == 3)
			IXGBE_WRITE_REG(hw, IXGBE_RETA(i >> 2), reta);
	}

	/*
	 * Fill out hash function seeds with a random constant
	 */
	for (i = 0; i < 10; i++) {
		if (ixgbe->rss_fixed_seed_enable) {
			random = fixed_seed[i];
		} else {
			(void) random_get_pseudo_bytes((uint8_t *)&random,
			    sizeof (uint32_t));
		}
		IXGBE_WRITE_REG(hw, IXGBE_RSSRK(i), random);
	}

	/*
	 * Enable and Setup RSS Hash functions
	 */
	mrqc = IXGBE_MRQC_RSSEN |
	    IXGBE_MRQC_RSS_FIELD_IPV4 |
	    IXGBE_MRQC_RSS_FIELD_IPV4_TCP |
	    IXGBE_MRQC_RSS_FIELD_IPV6 |
	    IXGBE_MRQC_RSS_FIELD_IPV6_TCP;

	if (ixgbe->rss_udp_enable)
		mrqc |= (IXGBE_MRQC_RSS_FIELD_IPV4_UDP |
		    IXGBE_MRQC_RSS_FIELD_IPV6_UDP);

	IXGBE_WRITE_REG(hw, IXGBE_MRQC, mrqc);

	/*
	 * Enable and Setup VMDq
	 * VMDq Filter = 0; MAC filtering
	 * Default VMDq output index = 0;
	 */
	vmdctl = IXGBE_VMD_CTL_VMDQ_EN;
	IXGBE_WRITE_REG(hw, IXGBE_VMD_CTL, vmdctl);

	/*
	 * Disable Packet Checksum to enable RSS for multiple receive queues.
	 * It is an adapter hardware limitation that Packet Checksum is
	 * mutually exclusive with RSS.
	 */
	rxcsum = IXGBE_READ_REG(hw, IXGBE_RXCSUM);
	rxcsum |= IXGBE_RXCSUM_PCSD;
	rxcsum &= ~IXGBE_RXCSUM_IPPCSE;
	IXGBE_WRITE_REG(hw, IXGBE_RXCSUM, rxcsum);
}

/*
 * ixgbe_setup_rx_vt_rss_generic - Setup both vt feature and rss feature
 * for 82599eb.
 */
static void
ixgbe_setup_rx_vt_rss_generic(ixgbe_t *ixgbe)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	uint32_t i, mrqc, rxcsum;
	uint32_t vtctl, vt_reg_bits;
	uint32_t random;
	uint32_t reta;
	uint32_t ring_per_group;

	/*
	 * Fill out redirection table
	 */
	reta = 0;
	ring_per_group = ixgbe->num_rx_rings / ixgbe->num_pf_rx_groups;
	for (i = 0; i < 128; i++) {
		reta = (reta << 8) | (i % ring_per_group) |
		    ((i % ring_per_group) << 4);
		if ((i & 3) == 3)
			IXGBE_WRITE_REG(hw, IXGBE_RETA(i >> 2), reta);
	}

	/*
	 * Fill out hash function seeds with a random constant
	 */
	for (i = 0; i < 10; i++) {
		(void) random_get_pseudo_bytes((uint8_t *)&random,
		    sizeof (uint32_t));
		IXGBE_WRITE_REG(hw, IXGBE_RSSRK(i), random);
	}

	/*
	 * Enable RSS & Setup RSS Hash functions
	 */
	mrqc = IXGBE_MRQC_RSS_FIELD_IPV4 |
	    IXGBE_MRQC_RSS_FIELD_IPV4_TCP |
	    IXGBE_MRQC_RSS_FIELD_IPV6 |
	    IXGBE_MRQC_RSS_FIELD_IPV6_TCP;

	if (ixgbe->rss_udp_enable)
		mrqc |= (IXGBE_MRQC_RSS_FIELD_IPV4_UDP |
		    IXGBE_MRQC_RSS_FIELD_IPV6_UDP);

	/*
	 * Enable VT+RSS.
	 */
	if (ixgbe->sriov_pf) {
		switch (ixgbe->iov_mode) {
		case IOV_64VF:
			mrqc |= IXGBE_MRQC_VMDQRSS64EN;
			break;
		case IOV_32VF:
			mrqc |= IXGBE_MRQC_VMDQRSS32EN;
			break;
		default:
			break;
		}
	} else {
		if (ixgbe->num_all_rx_groups > 32) {
			mrqc |= IXGBE_MRQC_VMDQRSS64EN;
		} else {
			mrqc |= IXGBE_MRQC_VMDQRSS32EN;
		}
	}

	IXGBE_WRITE_REG(hw, IXGBE_MRQC, mrqc);

	for (i = 0; i < hw->mac.num_rar_entries; i++) {
		IXGBE_WRITE_REG(hw, IXGBE_MPSAR_LO(i), 0);
		IXGBE_WRITE_REG(hw, IXGBE_MPSAR_HI(i), 0);
	}

	/*
	 * Enable Virtualization and Replication.
	 */
	vtctl = IXGBE_VT_CTL_VT_ENABLE | IXGBE_VT_CTL_REPLEN;

	if (ixgbe->sriov_pf) {
		vt_reg_bits = (ixgbe->pf_grp << IXGBE_VT_CTL_POOL_SHIFT);
		vtctl |= vt_reg_bits;
	}
	IXGBE_WRITE_REG(hw, IXGBE_VT_CTL, vtctl);

	/*
	 * Disable Packet Checksum to enable RSS for multiple receive queues.
	 * It is an adapter hardware limitation that Packet Checksum is
	 * mutually exclusive with RSS.
	 */
	rxcsum = IXGBE_READ_REG(hw, IXGBE_RXCSUM);
	rxcsum |= IXGBE_RXCSUM_PCSD;
	rxcsum &= ~IXGBE_RXCSUM_IPPCSE;
	IXGBE_WRITE_REG(hw, IXGBE_RXCSUM, rxcsum);

	/*
	 * Enable receiving packets to all VFs
	 */
	IXGBE_WRITE_REG(hw, IXGBE_VFRE(0), IXGBE_VFRE_ENABLE_ALL);
	IXGBE_WRITE_REG(hw, IXGBE_VFRE(1), IXGBE_VFRE_ENABLE_ALL);
	if (ixgbe->sriov_pf) {
		IXGBE_WRITE_REG(hw, IXGBE_VFTE(0), IXGBE_VFRE_ENABLE_ALL);
		IXGBE_WRITE_REG(hw, IXGBE_VFTE(1), IXGBE_VFRE_ENABLE_ALL);
		IXGBE_WRITE_REG(hw, IXGBE_PFDTXGSWC, IXGBE_PFDTXGSWC_VT_LBEN);
		for (i = 0; i < ixgbe->iov_mode; i ++) {
			ixgbe_set_vmolr(ixgbe, i);
		}
	}
}

static void
ixgbe_setup_tx_classify_generic(ixgbe_t *ixgbe)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	uint32_t mtqc, rttdcs;

	rttdcs = IXGBE_READ_REG(hw, IXGBE_RTTDCS);
	rttdcs |= IXGBE_RTTDCS_ARBDIS;
	IXGBE_WRITE_REG(hw, IXGBE_RTTDCS, rttdcs);

	/* configuration SR-IOV TX mode */
	if (ixgbe->sriov_pf) {
		mtqc = IXGBE_MTQC_VT_ENA;

		switch (ixgbe->iov_mode) {
		case IOV_64VF:
			mtqc |= IXGBE_MTQC_64VF;
			break;
		case IOV_32VF:
			mtqc |= IXGBE_MTQC_32VF;
			break;
		default:
			break;
		}

		IXGBE_WRITE_REG(hw, IXGBE_MTQC, mtqc);
	}

	/* DCB TX configurations is unavailable yet */

	IXGBE_WRITE_REG(hw, IXGBE_RTTDCS, (rttdcs & ~IXGBE_RTTDCS_ARBDIS));
}

static void
ixgbe_reserve_unicast_slots(ixgbe_t *ixgbe)
{
	uint32_t slots;
	int group, i;

	/*
	 * In Non-SRIOV mode, each group has one reserved slot. The slots
	 * from 0 to (group number - 1) are reserved slots which are
	 * 1 to 1 mapped with group index directly. The other slots are
	 * shared between all groups.
	 *
	 * In SRIOV mode, if the PF/VF is configured with a "unicast_slots"
	 * number, it can only use the reserved number of slots. Otherwise,
	 * it has one reserved slot, and can share the available slots with
	 * other PF/VFs.
	 */

	i = 0;
	for (group = 0; group < ixgbe->num_all_rx_groups; group++) {
		if (ixgbe->sriov_pf) {
			slots = ixgbe->vf[group].unicast_slots;
			if (slots == 0)
				slots = 1;
		} else {
			slots = 1;
		}

		while (slots > 0) {
			ixgbe->unicst_addr[i].group_index = (uint8_t)group;
			ixgbe->unicst_addr[i].flags = 0;

			slots--;
			i++;
		}
	}
	ASSERT(i <= ixgbe->unicst_total);

	for (; i < ixgbe->unicst_total; i++)
		ixgbe->unicst_addr[i].flags = IXGBE_ADDRESS_SHARED;
}

/*
 * ixgbe_init_unicst - Initialize the unicast addresses.
 */
static void
ixgbe_init_unicst(ixgbe_t *ixgbe)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	uint8_t *mac_addr;
	int slot;
	/*
	 * Here we should consider two situations:
	 *
	 * 1. Chipset is initialized at the first time,
	 *    Clear all the multiple unicast addresses.
	 *
	 * 2. Chipset is reset
	 *    Recover the multiple unicast addresses from the
	 *    software data structure to the RAR registers.
	 */
	if (!ixgbe->unicst_init) {
		/*
		 * Initialize the multiple unicast addresses
		 */
		ixgbe->unicst_total = hw->mac.num_rar_entries;
		ixgbe->unicst_avail = ixgbe->unicst_total;
		for (slot = 0; slot < ixgbe->unicst_total; slot++) {
			mac_addr = ixgbe->unicst_addr[slot].addr;
			bzero(mac_addr, ETHERADDRL);
			(void) ixgbe_set_rar(hw, slot, mac_addr, NULL, NULL);
			ixgbe->unicst_addr[slot].flags = 0;
		}
		ixgbe_reserve_unicast_slots(ixgbe);
		ixgbe->unicst_init = B_TRUE;
	} else {
		/* Re-configure the RAR registers */
		for (slot = 0; slot < ixgbe->unicst_total; slot++) {
			mac_addr = ixgbe->unicst_addr[slot].addr;
			if (ixgbe->unicst_addr[slot].flags &
			    IXGBE_ADDRESS_ENABLED) {
				(void) ixgbe_set_rar(hw, slot, mac_addr,
				    ixgbe->unicst_addr[slot].group_index,
				    IXGBE_RAH_AV);
			} else {
				bzero(mac_addr, ETHERADDRL);
				(void) ixgbe_set_rar(hw, slot, mac_addr,
				    NULL, NULL);
			}
		}
	}
}

/*
 * ixgbe_unicst_find - Find the slot for the unicast address and group
 */
int
ixgbe_unicst_find(ixgbe_t *ixgbe, const uint8_t *mac_addr, uint32_t group)
{
	int slot;

	ASSERT(mutex_owned(&ixgbe->gen_lock));

	for (slot = 0; slot < ixgbe->unicst_total; slot++) {
		if ((ixgbe->unicst_addr[slot].flags & IXGBE_ADDRESS_SET) &&
		    (ixgbe->unicst_addr[slot].group_index == group) &&
		    (bcmp(ixgbe->unicst_addr[slot].addr,
		    mac_addr, ETHERADDRL) == 0)) {
			return (slot);
		}
	}

	return (-1);
}

/*
 * ixgbe_multicst_add - Add a multicst address.
 */
int
ixgbe_multicst_add(ixgbe_t *ixgbe, const uint8_t *multiaddr)
{
	ASSERT(mutex_owned(&ixgbe->gen_lock));

	if ((multiaddr[0] & 01) == 0) {
		return (EINVAL);
	}

	if (ixgbe->mcast_count >= MAX_NUM_MULTICAST_ADDRESSES) {
		return (ENOENT);
	}

	bcopy(multiaddr,
	    &ixgbe->mcast_table[ixgbe->mcast_count], ETHERADDRL);
	ixgbe->mcast_count++;

	/*
	 * Update the multicast table in the hardware
	 */
	ixgbe_setup_multicst(ixgbe);

	if (ixgbe->sriov_pf) {
		struct ixgbe_hw *hw = &ixgbe->hw;
		uint32_t vmolr;

		vmolr = IXGBE_READ_REG(hw, IXGBE_VMOLR(ixgbe->pf_grp));
		ASSERT(ixgbe->mcast_count > 0);
		if ((vmolr & IXGBE_VMOLR_ROMPE) == 0) {
			vmolr |= IXGBE_VMOLR_ROMPE;
			IXGBE_WRITE_REG(hw, IXGBE_VMOLR(ixgbe->pf_grp), vmolr);
		}
	}

	if (ixgbe_check_acc_handle(ixgbe->osdep.reg_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(ixgbe->dip, DDI_SERVICE_DEGRADED);
		return (EIO);
	}

	return (0);
}

/*
 * ixgbe_multicst_remove - Remove a multicst address.
 */
int
ixgbe_multicst_remove(ixgbe_t *ixgbe, const uint8_t *multiaddr)
{
	int i;

	ASSERT(mutex_owned(&ixgbe->gen_lock));

	for (i = 0; i < ixgbe->mcast_count; i++) {
		if (bcmp(multiaddr, &ixgbe->mcast_table[i],
		    ETHERADDRL) == 0) {
			for (i++; i < ixgbe->mcast_count; i++) {
				ixgbe->mcast_table[i - 1] =
				    ixgbe->mcast_table[i];
			}
			ixgbe->mcast_count--;
			break;
		}
	}

	/*
	 * Update the multicast table in the hardware
	 */
	ixgbe_setup_multicst(ixgbe);

	if (ixgbe->sriov_pf) {
		struct ixgbe_hw *hw = &ixgbe->hw;
		uint32_t vmolr;
		if (ixgbe->mcast_count == 0) {
			vmolr = IXGBE_READ_REG(hw, IXGBE_VMOLR(ixgbe->pf_grp));
			vmolr &= ~IXGBE_VMOLR_ROMPE;
			IXGBE_WRITE_REG(hw, IXGBE_VMOLR(ixgbe->pf_grp), vmolr);
		}
	}

	if (ixgbe_check_acc_handle(ixgbe->osdep.reg_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(ixgbe->dip, DDI_SERVICE_DEGRADED);
		return (EIO);
	}

	return (0);
}

/*
 * ixgbe_setup_multicast - Setup multicast data structures.
 *
 * This routine initializes all of the multicast related structures
 * and save them in the hardware registers.
 */
void
ixgbe_setup_multicst(ixgbe_t *ixgbe)
{
	uint8_t *mc_addr_list;
	uint32_t mc_addr_count, i, j;
	struct ixgbe_hw *hw = &ixgbe->hw;
	vf_data_t *vf;

	ASSERT(mutex_owned(&ixgbe->gen_lock));

	ASSERT(ixgbe->mcast_count <= MAX_NUM_MULTICAST_ADDRESSES);

	mc_addr_list = (uint8_t *)ixgbe->mcast_table;
	mc_addr_count = ixgbe->mcast_count;

	/*
	 * Update the multicast addresses to the MTA registers
	 */
	(void) ixgbe_update_mc_addr_list(hw, mc_addr_list, mc_addr_count,
	    ixgbe_mc_table_itr);

	/*
	 * When functioning as a PF driver, update the multicast lists of all
	 * VFs to the MTA registers
	 */
	if (ixgbe->sriov_pf) {
		for (i = 0; i < ixgbe->num_vfs; i++) {
			vf = &ixgbe->vf[i];
			for (j = 0; j < vf->num_vf_mc_hashes; j++) {
				ixgbe_mta_set_vf(ixgbe,
				    vf->vf_mc_hashes[j]);
			}
		}
		IXGBE_WRITE_FLUSH(hw);
	}

#ifdef IXGBE_DEBUG
	/* check mta */
	uint32_t reg;
	ixgbe_log(ixgbe, "ixgbe_setup_multicst -- mta --\n");
	for (i = 0; i < 128; i++) {
		if (reg = IXGBE_READ_REG(hw, IXGBE_MTA(i))) {
			ixgbe_log(ixgbe, "mta(%d): 0x%x\n", i, reg);
		}
	}
#endif
}

/*
 *  ixgbe_mta_set_vf - Set multicast filter table address for virtual functions
 *
 *  The multicast table address is a register array of 32-bit registers.
 *  The hash_value is used to determine what register the bit is in, the
 *  current value is read, the new bit is OR'd in and the new value is
 *  written back into the register.
 */
static void
ixgbe_mta_set_vf(ixgbe_t *ixgbe, uint32_t hash_value)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	uint32_t hash_bit, hash_reg, mta;

	/*
	 * The MTA is a register array of 32-bit registers. It is
	 * treated like an array of (32*mcft_size) bits.  We want to
	 * set bit BitArray[hash_value]. So we figure out what register
	 * the bit is in, read it, OR in the new bit, then write
	 * back the new value.  The (hw->mac.mcft_size - 1) serves as a
	 * mask to bits 31:5 of the hash value which gives us the
	 * register we're modifying.  The hash bit within that register
	 * is determined by the lower 5 bits of the hash value.
	 */
	hash_reg = (hash_value >> 5) & (hw->mac.mcft_size - 1);
	hash_bit = hash_value & 0x1F;

	mta = IXGBE_READ_REG(hw, IXGBE_MTA(hash_reg));

	mta |= (1 << hash_bit);

	IXGBE_WRITE_REG(hw, IXGBE_MTA(hash_reg), mta);
}

/*
 * ixgbe_setup_vt_rss_conf - Configure vmdq and rss/dcb (number and mode).
 *
 * Configure the rx classification mode (vt & rss) and vmdq & rss numbers.
 * Different chipsets may have different allowed configuration of vmdq and rss.
 */
static void
ixgbe_setup_vt_rss_conf(ixgbe_t *ixgbe)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	struct ixgbe_dcb_config *dcb_config = &ixgbe->dcb_config;
	uint32_t ring_per_group;
	uint32_t ring_per_tc;
	int num_tcs = dcb_config->num_tcs.pg_tcs;

	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		/*
		 * 82598 supports the following combination:
		 * vmdq no. x rss no.
		 * [5..16]  x 1
		 * [1..4]   x [1..16]
		 * However 8 rss queue per pool (vmdq) is sufficient for
		 * most cases.
		 */
		if (num_tcs != 0)
			dcb_config->num_tcs.pg_tcs = 0;
		ring_per_group = ixgbe->num_rx_rings / ixgbe->num_pf_rx_groups;
		if (ixgbe->num_pf_rx_groups > 4) {
			ixgbe->num_rx_rings = ixgbe->num_pf_rx_groups;
		} else {
			ixgbe->num_rx_rings = ixgbe->num_pf_rx_groups *
			    min((ixgbe->max_rings_enable ? 16: 8),
			    ring_per_group);
		}

		break;

	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
		/*
		 * 82599/X540 supports the following combination:
		 * vmdq no. x rss no.
		 * [33..64] x [1..2]
		 * [2..32]  x [1..4]
		 * 1 x [1..16]
		 * However 8 rss queue per pool (vmdq) is sufficient for
		 * most cases.
		 */
		if (num_tcs == 0) {
			ring_per_group =
			    ixgbe->num_rx_rings / ixgbe->num_pf_rx_groups;
			if (ixgbe->num_pf_rx_groups == 1) {
				ixgbe->num_rx_rings =
				    min((ixgbe->max_rings_enable ? 16: 8),
				    ring_per_group);
			} else if (ixgbe->num_pf_rx_groups <= 32) {
				ixgbe->num_rx_rings = ixgbe->num_pf_rx_groups *
				    min(4, ring_per_group);
			} else if (ixgbe->num_pf_rx_groups <= 64) {
				ixgbe->num_rx_rings = ixgbe->num_pf_rx_groups *
				    min(2, ring_per_group);
			}
		}
		break;

	default:
		break;
	}

	ring_per_group = ixgbe->num_rx_rings / ixgbe->num_pf_rx_groups;

	if ((num_tcs = dcb_config->num_tcs.pg_tcs) != 0) {
		ring_per_tc = ixgbe->num_rx_rings / num_tcs;
		ASSERT(ring_per_tc >= 1 && ixgbe->num_pf_rx_groups == 1);
		if (ring_per_tc == 1) {
			ixgbe->classify_mode = IXGBE_CLASSIFY_DCB;
		} else {
			ixgbe->classify_mode = IXGBE_CLASSIFY_RSS_DCB;
		}
	} else if (ixgbe->num_pf_rx_groups == 1 && ring_per_group == 1) {
		ixgbe->classify_mode = IXGBE_CLASSIFY_NONE;
	} else if (ixgbe->num_pf_rx_groups != 1 && ring_per_group == 1) {
		ixgbe->classify_mode = IXGBE_CLASSIFY_VT;
	} else if (ixgbe->num_pf_rx_groups != 1 && ring_per_group != 1) {
		ixgbe->classify_mode = IXGBE_CLASSIFY_VT_RSS;
	} else {
		ixgbe->classify_mode = IXGBE_CLASSIFY_RSS;
	}

	IXGBE_DEBUGLOG_2(ixgbe, "pf rx group number:%d, rx ring number:%d",
	    ixgbe->num_pf_rx_groups, ixgbe->num_rx_rings);
}

/*
 * ixgbe_get_conf - Get driver configurations set in driver.conf.
 *
 * This routine gets user-configured values out of the configuration
 * file ixgbe.conf.
 *
 * For each configurable value, there is a minimum, a maximum, and a
 * default.
 * If user does not configure a value, use the default.
 * If user configures below the minimum, use the minumum.
 * If user configures above the maximum, use the maxumum.
 */
static void
ixgbe_get_conf(ixgbe_t *ixgbe)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	uint32_t flow_control;
	uint_t num_tx_rings, num_rx_rings;
	int dcb_mode = ixgbe_dcb_disabled;

	/*
	 * ixgbe driver supports the following user configurations:
	 *
	 * Jumbo frame configuration:
	 *    default_mtu
	 *
	 * Ethernet flow control configuration:
	 *    flow_control
	 *
	 * DCB configuration:
	 *   dcb_mode
	 *
	 * Multiple rings configurations:
	 *    tx_queue_number
	 *    tx_ring_size
	 *    rx_queue_number
	 *    rx_ring_size
	 *
	 * Call ixgbe_get_prop() to get the value for a specific
	 * configuration parameter.
	 */

	/*
	 * Jumbo frame configuration - max_frame_size controls host buffer
	 * allocation, so includes MTU, ethernet header, vlan tag and
	 * frame check sequence.
	 */
	ixgbe->default_mtu = ixgbe_get_prop(ixgbe, PROP_DEFAULT_MTU,
	    MIN_MTU, ixgbe->capab->max_mtu, DEFAULT_MTU);

	ixgbe->max_frame_size = ixgbe->default_mtu +
	    sizeof (struct ether_vlan_header) + ETHERFCSL;

	/*
	 * Ethernet flow control configuration
	 * fc.requested mode is what the user requests.  After autoneg,
	 * fc.current_mode will be the flow_control mode that was negotiated.
	 */
	flow_control = ixgbe_get_prop(ixgbe, PROP_FLOW_CONTROL,
	    ixgbe_fc_none, ixgbe_fc_default, ixgbe_fc_none);
	if (hw->mac.type < ixgbe_mac_82599EB &&
	    (flow_control == ixgbe_fc_pfc || flow_control == ixgbe_fc_pfc_auto))
		hw->fc.requested_mode = ixgbe_fc_none;
	else
		hw->fc.requested_mode = flow_control;

	if (ixgbe->num_vfs >= 32)
		ixgbe->iov_mode = IOV_64VF;
	else if (ixgbe->num_vfs > 0) {
		ixgbe->iov_mode = ixgbe_get_prop(ixgbe, PROP_IOV_MODE,
		    IOV_32VF, IOV_64VF, IOV_32VF);
		ixgbe->iov_mode = (ixgbe->iov_mode <= 32) ? 32 : 64;
	} else
		ixgbe->iov_mode = IOV_NONE;

	/*
	 * Multiple rings configurations
	 */
	if (ixgbe->sriov_pf) {
		num_tx_rings = ixgbe->capab->max_tx_que_num / ixgbe->iov_mode;
		ixgbe->num_tx_rings = ixgbe_get_prop(ixgbe, PROP_TX_QUEUE_NUM,
		    1, num_tx_rings, num_tx_rings);
	} else {
		ixgbe->num_tx_rings = ixgbe_get_prop(ixgbe, PROP_TX_QUEUE_NUM,
		    ixgbe->capab->min_tx_que_num,
		    ixgbe->capab->max_tx_que_num,
		    ixgbe->capab->def_tx_que_num);
	}

	ixgbe->tx_ring_size = ixgbe_get_prop(ixgbe, PROP_TX_RING_SIZE,
	    MIN_TX_RING_SIZE, MAX_TX_RING_SIZE, DEFAULT_TX_RING_SIZE);

	if (ixgbe->sriov_pf) {
		num_rx_rings = ixgbe->capab->max_rx_que_num / ixgbe->iov_mode;
		ixgbe->num_rx_rings = ixgbe_get_prop(ixgbe, PROP_RX_QUEUE_NUM,
		    1, num_rx_rings, num_rx_rings);
	} else {
		ixgbe->num_rx_rings = ixgbe_get_prop(ixgbe, PROP_RX_QUEUE_NUM,
		    ixgbe->capab->min_rx_que_num,
		    ixgbe->capab->max_rx_que_num,
		    ixgbe->capab->def_rx_que_num);
	}

	ixgbe->rx_ring_size = ixgbe_get_prop(ixgbe, PROP_RX_RING_SIZE,
	    MIN_RX_RING_SIZE, MAX_RX_RING_SIZE, DEFAULT_RX_RING_SIZE);

	/*
	 * Multiple groups configuration
	 */
	if (ixgbe->sriov_pf) {
		ixgbe->num_pf_rx_groups = 1;
	} else {
		ixgbe->num_pf_rx_groups = ixgbe_get_prop(ixgbe,
		    PROP_RX_GROUP_NUM,
		    ixgbe->capab->min_grp_num,
		    ixgbe->capab->max_grp_num,
		    ixgbe->capab->def_grp_num);
	}

	/*
	 * DCB configuration
	 */
	if (hw->mac.type >= ixgbe_mac_82599EB) {
		dcb_mode = ixgbe_get_prop(ixgbe, PROP_DCB_MODE,
		    ixgbe_dcb_disabled, ixgbe_dcb_8tc_rss, ixgbe_dcb_disabled);
	}

	ixgbe->mr_enable = ixgbe_get_prop(ixgbe, PROP_MR_ENABLE,
	    0, 1, DEFAULT_MR_ENABLE);

	if (ixgbe->sriov_pf) {
		if (ixgbe->iov_mode == IOV_64VF) {
			ixgbe->classify_mode = (ixgbe->num_rx_rings > 1) ?
			    IXGBE_CLASSIFY_VT_RSS : IXGBE_CLASSIFY_VT;
		} else
			ixgbe->classify_mode = IXGBE_CLASSIFY_VT_RSS;
	} else if (ixgbe->mr_enable == B_FALSE) {
		ixgbe->num_tx_rings = 1;
		ixgbe->num_rx_rings = 1;
		ixgbe->num_pf_rx_groups = 1;

		ixgbe->dcb_config.num_tcs.pg_tcs = 0;
		ixgbe->classify_mode = IXGBE_CLASSIFY_NONE;
	} else {
		switch (dcb_mode) {
		case ixgbe_dcb_disabled:
			ixgbe->dcb_config.num_tcs.pg_tcs = 0;
			ixgbe->num_rx_rings = ixgbe->num_pf_rx_groups *
			    max(ixgbe->num_rx_rings /
			    ixgbe->num_pf_rx_groups, 1);
			ixgbe->max_rings_enable = ixgbe_get_prop(ixgbe,
			    PROP_MAX_RINGS_ENABLE, 0, 1,
			    DEFAULT_MAX_RINGS_ENABLE);
			break;
		case ixgbe_dcb_4tc:
			ixgbe->dcb_config.num_tcs.pg_tcs = 4;
			ixgbe->num_pf_rx_groups = 1;
			ixgbe->num_rx_rings = 4;
			break;
		case ixgbe_dcb_4tc_rss:
			ixgbe->dcb_config.num_tcs.pg_tcs = 4;
			ixgbe->num_pf_rx_groups = 1;
			ixgbe->num_rx_rings = 4 * 4;
			break;
		case ixgbe_dcb_8tc:
			ixgbe->dcb_config.num_tcs.pg_tcs = 8;
			ixgbe->num_pf_rx_groups = 1;
			ixgbe->num_rx_rings = 8;
			break;
		case ixgbe_dcb_8tc_rss:
			ixgbe->dcb_config.num_tcs.pg_tcs = 8;
			ixgbe->num_pf_rx_groups = 1;
			ixgbe->num_rx_rings = 8 * 4;
			break;
		default:
			break;
		}

		if (ixgbe->dcb_config.num_tcs.pg_tcs != 0)
			ixgbe->num_tx_rings = 4 *
			    ixgbe->dcb_config.num_tcs.pg_tcs;
		/*
		 * The combination of num_rx_rings and num_pf_rx_groups
		 * may be not supported by h/w. We need to adjust
		 * them to appropriate values.
		 */
		ixgbe_setup_vt_rss_conf(ixgbe);
	}

	ixgbe->num_all_rx_groups = ixgbe->num_vfs + ixgbe->num_pf_rx_groups;
	ixgbe->num_pf_tx_groups = 1;
	ixgbe->num_all_tx_groups = ixgbe->num_vfs + ixgbe->num_pf_tx_groups;

	/*
	 * Higher UDP RX performance can be achieved for non-fragmented
	 * UDP packets with rss_udp_enable enabled, while may introduce
	 * potential out-of-order issue for fragmented UDP packets.
	 */
	ixgbe->rss_udp_enable = ixgbe_get_prop(ixgbe, PROP_RSS_UDP_ENABLE,
	    0, 1, DEFAULT_RSS_UDP_ENABLE);

	/*
	 * Tunable used to force an interrupt type. The only use is
	 * for testing of the lesser interrupt types.
	 * 0 = don't force interrupt type
	 * 1 = force interrupt type MSI-X
	 * 2 = force interrupt type MSI
	 * 3 = force interrupt type Legacy
	 */
	ixgbe->intr_force = ixgbe_get_prop(ixgbe, PROP_INTR_FORCE,
	    IXGBE_INTR_NONE, IXGBE_INTR_LEGACY, IXGBE_INTR_NONE);

	ixgbe->tx_hcksum_enable = ixgbe_get_prop(ixgbe, PROP_TX_HCKSUM_ENABLE,
	    0, 1, DEFAULT_TX_HCKSUM_ENABLE);
	ixgbe->rx_hcksum_enable = ixgbe_get_prop(ixgbe, PROP_RX_HCKSUM_ENABLE,
	    0, 1, DEFAULT_RX_HCKSUM_ENABLE);
	ixgbe->lso_enable = ixgbe_get_prop(ixgbe, PROP_LSO_ENABLE,
	    0, 1, DEFAULT_LSO_ENABLE);
	ixgbe->lro_enable = ixgbe_get_prop(ixgbe, PROP_LRO_ENABLE,
	    0, 1, DEFAULT_LRO_ENABLE);
	ixgbe->tx_head_wb_enable = ixgbe_get_prop(ixgbe, PROP_TX_HEAD_WB_ENABLE,
	    0, 1, DEFAULT_TX_HEAD_WB_ENABLE);
	ixgbe->relax_order_enable = ixgbe_get_prop(ixgbe,
	    PROP_RELAX_ORDER_ENABLE, 0, 1, DEFAULT_RELAX_ORDER_ENABLE);
	ixgbe->rss_fixed_seed_enable = ixgbe_get_prop(ixgbe,
	    PROP_RSS_FIXED_SEED_ENABLE, 0, 1, DEFAULT_RSS_FIXED_SEED_ENABLE);
	ixgbe->fcoe_txcrc_enable = ixgbe_get_prop(ixgbe, PROP_FCOE_TXCRC_ENABLE,
	    0, 1, DEFAULT_FCOE_TXCRC_ENABLE);
	ixgbe->fcoe_rxcrc_enable = ixgbe_get_prop(ixgbe, PROP_FCOE_RXCRC_ENABLE,
	    0, 1, DEFAULT_FCOE_RXCRC_ENABLE);
	ixgbe->fcoe_lso_enable = ixgbe_get_prop(ixgbe, PROP_FCOE_LSO_ENABLE,
	    0, 1, DEFAULT_FCOE_LSO_ENABLE);
	ixgbe->fcoe_lro_enable = ixgbe_get_prop(ixgbe, PROP_FCOE_LRO_ENABLE,
	    0, 1, DEFAULT_FCOE_LRO_ENABLE);
	ixgbe->fcoe_rxmr_enable = ixgbe_get_prop(ixgbe, PROP_FCOE_RXMR_ENABLE,
	    0, 0, DEFAULT_FCOE_RXMR_ENABLE);

	/* Head Write Back not recommended for 82599/X540 */
	if (hw->mac.type >= ixgbe_mac_82599EB) {
		ixgbe->tx_head_wb_enable = B_FALSE;
	}

	/*
	 * ixgbe LSO needs the tx h/w checksum support.
	 * LSO will be disabled if tx h/w checksum is not
	 * enabled.
	 */
	if (ixgbe->tx_hcksum_enable == B_FALSE) {
		ixgbe->lso_enable = B_FALSE;
	}

	/*
	 * ixgbe LRO needs the rx h/w checksum support.
	 * LRO will be disabled if rx h/w checksum is not
	 * enabled.
	 */
	if (ixgbe->rx_hcksum_enable == B_FALSE) {
		ixgbe->lro_enable = B_FALSE;
	}

	/*
	 * ixgbe LRO not supported for 82598
	 */
	if (hw->mac.type == ixgbe_mac_82598EB) {
		ixgbe->lro_enable = B_FALSE;
	}

	/*
	 * ixgbe FCoE feature only available on 82599
	 */
	if (hw->mac.type != ixgbe_mac_82599EB) {
		ixgbe->fcoe_txcrc_enable = B_FALSE;
		ixgbe->fcoe_rxcrc_enable = B_FALSE;
		ixgbe->fcoe_lso_enable = B_FALSE;
		ixgbe->fcoe_lro_enable = B_FALSE;
		ixgbe->fcoe_rxmr_enable = B_FALSE;
	}

	/*
	 * ixgbe FCoE LSO needs the tx crc offload support.
	 * FCoE LSO will be disabled if tx crc offload is not
	 * enabled.
	 */
	if (ixgbe->fcoe_txcrc_enable == B_FALSE) {
		ixgbe->fcoe_lso_enable = B_FALSE;
	}

	/*
	 * ixgbe FCoE LRO needs the rx crc offload support.
	 * FCoE LRO will be disabled if rx crc offload is not
	 * enabled.
	 */
	if (ixgbe->fcoe_rxcrc_enable == B_FALSE) {
		ixgbe->fcoe_lro_enable = B_FALSE;
	}

	ixgbe->tx_copy_thresh = ixgbe_get_prop(ixgbe, PROP_TX_COPY_THRESHOLD,
	    MIN_TX_COPY_THRESHOLD, MAX_TX_COPY_THRESHOLD,
	    DEFAULT_TX_COPY_THRESHOLD);
	ixgbe->tx_recycle_thresh = ixgbe_get_prop(ixgbe,
	    PROP_TX_RECYCLE_THRESHOLD, MIN_TX_RECYCLE_THRESHOLD,
	    MAX_TX_RECYCLE_THRESHOLD, DEFAULT_TX_RECYCLE_THRESHOLD);
	ixgbe->tx_overload_thresh = ixgbe_get_prop(ixgbe,
	    PROP_TX_OVERLOAD_THRESHOLD, MIN_TX_OVERLOAD_THRESHOLD,
	    MAX_TX_OVERLOAD_THRESHOLD, DEFAULT_TX_OVERLOAD_THRESHOLD);
	ixgbe->tx_resched_thresh = ixgbe_get_prop(ixgbe,
	    PROP_TX_RESCHED_THRESHOLD, MIN_TX_RESCHED_THRESHOLD,
	    MAX_TX_RESCHED_THRESHOLD, DEFAULT_TX_RESCHED_THRESHOLD);

	ixgbe->rx_copy_thresh = ixgbe_get_prop(ixgbe, PROP_RX_COPY_THRESHOLD,
	    MIN_RX_COPY_THRESHOLD, MAX_RX_COPY_THRESHOLD,
	    DEFAULT_RX_COPY_THRESHOLD);
	ixgbe->rx_limit_per_intr = ixgbe_get_prop(ixgbe, PROP_RX_LIMIT_PER_INTR,
	    MIN_RX_LIMIT_PER_INTR, MAX_RX_LIMIT_PER_INTR,
	    DEFAULT_RX_LIMIT_PER_INTR);

	ixgbe->intr_throttling[0] = ixgbe_get_prop(ixgbe, PROP_INTR_THROTTLING,
	    ixgbe->capab->min_intr_throttle,
	    ixgbe->capab->max_intr_throttle,
	    ixgbe->capab->def_intr_throttle);
	/*
	 * 82599/X540 requires the interrupt throttling rate is
	 * a multiple of 8. This is enforced by the register
	 * definiton.
	 */
	if (hw->mac.type >= ixgbe_mac_82599EB)
		ixgbe->intr_throttling[0] = ixgbe->intr_throttling[0] & 0xFF8;
}

static void
ixgbe_init_params(ixgbe_t *ixgbe)
{
	ixgbe->param_en_10000fdx_cap = 1;
	ixgbe->param_en_1000fdx_cap = 1;
	ixgbe->param_en_100fdx_cap = 1;
	ixgbe->param_adv_10000fdx_cap = 1;
	ixgbe->param_adv_1000fdx_cap = 1;
	ixgbe->param_adv_100fdx_cap = 1;

	ixgbe->param_pause_cap = 1;
	ixgbe->param_asym_pause_cap = 1;
	ixgbe->param_rem_fault = 0;

	ixgbe->param_adv_autoneg_cap = 1;
	ixgbe->param_adv_pause_cap = 1;
	ixgbe->param_adv_asym_pause_cap = 1;
	ixgbe->param_adv_rem_fault = 0;

	ixgbe->param_lp_10000fdx_cap = 0;
	ixgbe->param_lp_1000fdx_cap = 0;
	ixgbe->param_lp_100fdx_cap = 0;
	ixgbe->param_lp_autoneg_cap = 0;
	ixgbe->param_lp_pause_cap = 0;
	ixgbe->param_lp_asym_pause_cap = 0;
	ixgbe->param_lp_rem_fault = 0;
}

/*
 * ixgbe_get_prop - Get a property value out of the configuration file
 * ixgbe.conf.
 *
 * Caller provides the name of the property, a default value, a minimum
 * value, and a maximum value.
 *
 * Return configured value of the property, with default, minimum and
 * maximum properly applied.
 */
static int
ixgbe_get_prop(ixgbe_t *ixgbe,
    char *propname,	/* name of the property */
    int minval,		/* minimum acceptable value */
    int maxval,		/* maximim acceptable value */
    int defval)		/* default value */
{
	int value;

	/*
	 * Call ddi_prop_get_int() to read the conf settings
	 */
	value = ddi_prop_get_int(DDI_DEV_T_ANY, ixgbe->dip,
	    DDI_PROP_DONTPASS, propname, defval);
	if (value > maxval)
		value = maxval;

	if (value < minval)
		value = minval;

	return (value);
}

/*
 * ixgbe_driver_setup_link - Using the link properties to setup the link.
 */
int
ixgbe_driver_setup_link(ixgbe_t *ixgbe, boolean_t setup_hw)
{
	u32 autoneg_advertised = 0;

	/*
	 * No half duplex support with 10Gb parts
	 */
	if (ixgbe->param_adv_10000fdx_cap == 1)
		autoneg_advertised |= IXGBE_LINK_SPEED_10GB_FULL;

	if (ixgbe->param_adv_1000fdx_cap == 1)
		autoneg_advertised |= IXGBE_LINK_SPEED_1GB_FULL;

	if (ixgbe->param_adv_100fdx_cap == 1)
		autoneg_advertised |= IXGBE_LINK_SPEED_100_FULL;

	if (ixgbe->param_adv_autoneg_cap == 1 && autoneg_advertised == 0) {
		ixgbe_notice(ixgbe, "Invalid link settings. Setup link "
		    "to autonegotiation with full link capabilities.");

		autoneg_advertised = IXGBE_LINK_SPEED_10GB_FULL |
		    IXGBE_LINK_SPEED_1GB_FULL |
		    IXGBE_LINK_SPEED_100_FULL;
	}

	if (setup_hw) {
		if (ixgbe_setup_link(&ixgbe->hw, autoneg_advertised,
		    ixgbe->param_adv_autoneg_cap, B_TRUE) != IXGBE_SUCCESS) {
			ixgbe_notice(ixgbe, "Setup link failed on this "
			    "device.");
			return (IXGBE_FAILURE);
		}
	}

	return (IXGBE_SUCCESS);
}

void
ixgbe_driver_setup_up2tc(ixgbe_t *ixgbe, uint_t dir, uint_t *map)
{
	uint32_t reg;
	struct ixgbe_hw *hw = &ixgbe->hw;
	struct ixgbe_dcb_config *dcb_config;
	struct tc_bw_alloc *p;
	int i;
	int tc;

	if (hw->mac.type < ixgbe_mac_82599EB)
		return;

	dcb_config = &ixgbe->dcb_config;

	for (i = 0; i < MAX_TRAFFIC_CLASS; i++) {
		p = &dcb_config->tc_config[i].path[dir];
		p->up_to_tc_bitmap = 0;
	}

	/*
	 * Set ARBDITS before making any changes
	 */
	reg = IXGBE_READ_REG(hw, IXGBE_RTTDCS);
	reg |= IXGBE_RTTDCS_ARBDIS;
	IXGBE_WRITE_REG(hw, IXGBE_RTTDCS, reg);

	reg = 0;
	switch (dir) {
	case DCB_TX_CONFIG:
		for (i = 0; i < MAX_USER_PRIORITY; i++) {
			tc = map[i];
			p = &dcb_config->tc_config[tc].path[dir];
			p->up_to_tc_bitmap |= 1 << i;
			reg |= (tc << (i * IXGBE_RTTUP2TC_UP_SHIFT));
		}
		IXGBE_WRITE_REG(hw, IXGBE_RTTUP2TC, reg);
		break;
	case DCB_RX_CONFIG:
		for (i = 0; i < MAX_USER_PRIORITY; i++) {
			tc = map[i];
			p = &dcb_config->tc_config[tc].path[dir];
			p->up_to_tc_bitmap |= 1 << i;
			reg |= (tc << (i * IXGBE_RTRUP2TC_UP_SHIFT));
		}
		IXGBE_WRITE_REG(hw, IXGBE_RTRUP2TC, reg);
		break;
	}

	reg = IXGBE_READ_REG(hw, IXGBE_RTTDCS);
	reg &= ~IXGBE_RTTDCS_ARBDIS;
	IXGBE_WRITE_REG(hw, IXGBE_RTTDCS, reg);
}

/*
 * ixgbe_driver_link_check - Link status processing.
 *
 * This function can be called in both kernel context and interrupt context
 */
static void
ixgbe_driver_link_check(ixgbe_t *ixgbe)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	ixgbe_link_speed speed = IXGBE_LINK_SPEED_UNKNOWN;
	boolean_t link_up = B_FALSE;
	boolean_t link_changed = B_FALSE;

	ASSERT(mutex_owned(&ixgbe->gen_lock));

	(void) ixgbe_check_link(hw, &speed, &link_up, false);
	if (link_up) {
		ixgbe->link_check_complete = B_TRUE;

		/* Link is up, enable flow control settings */
		(void) ixgbe_fc_enable(hw, 0);

		/*
		 * The Link is up, check whether it was marked as down earlier
		 */
		if (ixgbe->link_state != LINK_STATE_UP) {
			switch (speed) {
			case IXGBE_LINK_SPEED_10GB_FULL:
				ixgbe->link_speed = SPEED_10GB;
				break;
			case IXGBE_LINK_SPEED_1GB_FULL:
				ixgbe->link_speed = SPEED_1GB;
				break;
			case IXGBE_LINK_SPEED_100_FULL:
				ixgbe->link_speed = SPEED_100;
			}
			ixgbe->link_duplex = LINK_DUPLEX_FULL;
			ixgbe->link_state = LINK_STATE_UP;
			link_changed = B_TRUE;
		}
	} else {
		if (ixgbe->link_check_complete == B_TRUE ||
		    (ixgbe->link_check_complete == B_FALSE &&
		    gethrtime() >= ixgbe->link_check_hrtime)) {
			/*
			 * The link is really down
			 */
			ixgbe->link_check_complete = B_TRUE;

			if (ixgbe->link_state != LINK_STATE_DOWN) {
				ixgbe->link_speed = 0;
				ixgbe->link_duplex = LINK_DUPLEX_UNKNOWN;
				ixgbe->link_state = LINK_STATE_DOWN;
				link_changed = B_TRUE;
			}
		}
	}

	/*
	 * If we are in an interrupt context, need to re-enable the
	 * interrupt, which was automasked
	 */
	if (servicing_interrupt() != 0) {
		ixgbe->eims |= IXGBE_EICR_LSC;
		IXGBE_WRITE_REG(hw, IXGBE_EIMS, ixgbe->eims);
	}

	if (link_changed && ixgbe->mac_hdl) {
		mac_link_update(ixgbe->mac_hdl, ixgbe->link_state);
	}
}

/*
 * ixgbe_sfp_check - sfp module processing done in taskq only for 82599/X540.
 */
static void
ixgbe_sfp_check(void *arg)
{
	ixgbe_t *ixgbe = (ixgbe_t *)arg;
	uint32_t eicr = ixgbe->eicr;
	struct ixgbe_hw *hw = &ixgbe->hw;

	mutex_enter(&ixgbe->gen_lock);
	if (eicr & IXGBE_EICR_GPI_SDP1) {
		/* clear the interrupt */
		IXGBE_WRITE_REG(hw, IXGBE_EICR, IXGBE_EICR_GPI_SDP1);

		/* if link up, do multispeed fiber setup */
		(void) ixgbe_setup_link(hw, IXGBE_LINK_SPEED_82599_AUTONEG,
		    B_TRUE, B_TRUE);
		ixgbe_driver_link_check(ixgbe);
		ixgbe_get_hw_state(ixgbe);
	} else if (eicr & IXGBE_EICR_GPI_SDP2) {
		/* clear the interrupt */
		IXGBE_WRITE_REG(hw, IXGBE_EICR, IXGBE_EICR_GPI_SDP2);

		/* if link up, do sfp module setup */
		(void) hw->mac.ops.setup_sfp(hw);

		/* do multispeed fiber setup */
		(void) ixgbe_setup_link(hw, IXGBE_LINK_SPEED_82599_AUTONEG,
		    B_TRUE, B_TRUE);
		ixgbe_driver_link_check(ixgbe);
		ixgbe_get_hw_state(ixgbe);
	}
	mutex_exit(&ixgbe->gen_lock);

	/*
	 * We need to fully re-check the link later.
	 */
	ixgbe->link_check_complete = B_FALSE;
	ixgbe->link_check_hrtime = gethrtime() +
	    (IXGBE_LINK_UP_TIME * 100000000ULL);
}

/*
 * ixgbe_overtemp_check - overtemp module processing done in taskq
 *
 * This routine will only be called on adapters with temperature sensor.
 * The indication of over-temperature can be either SDP0 interrupt or the link
 * status change interrupt.
 */
static void
ixgbe_overtemp_check(void *arg)
{
	ixgbe_t *ixgbe = (ixgbe_t *)arg;
	struct ixgbe_hw *hw = &ixgbe->hw;
	uint32_t eicr = ixgbe->eicr;
	ixgbe_link_speed speed;
	boolean_t link_up;

	mutex_enter(&ixgbe->gen_lock);

	/* make sure we know current state of link */
	(void) ixgbe_check_link(hw, &speed, &link_up, false);

	/* check over-temp condition */
	if (((eicr & IXGBE_EICR_GPI_SDP0) && (!link_up)) ||
	    (eicr & IXGBE_EICR_LSC)) {
		if (hw->phy.ops.check_overtemp(hw) == IXGBE_ERR_OVERTEMP) {
			atomic_or_32(&ixgbe->ixgbe_state, IXGBE_OVERTEMP);

			/*
			 * Disable the adapter interrupts
			 */
			ixgbe_disable_adapter_interrupts(ixgbe);

			/*
			 * Disable Rx/Tx units
			 */
			(void) ixgbe_stop_adapter(hw);

			ddi_fm_service_impact(ixgbe->dip, DDI_SERVICE_LOST);
			ixgbe_error(ixgbe,
			    "Problem: Network adapter has been stopped "
			    "because it has overheated");
			ixgbe_error(ixgbe,
			    "Action: Restart the computer. "
			    "If the problem persists, power off the system "
			    "and replace the adapter");
		}
	}

	/* write to clear the interrupt */
	IXGBE_WRITE_REG(hw, IXGBE_EICR, eicr);

	mutex_exit(&ixgbe->gen_lock);
}

/*
 * ixgbe_link_timer - timer for link status detection
 */
static void
ixgbe_link_timer(void *arg)
{
	ixgbe_t *ixgbe = (ixgbe_t *)arg;

	mutex_enter(&ixgbe->gen_lock);
	ixgbe_driver_link_check(ixgbe);
	mutex_exit(&ixgbe->gen_lock);
}

/*
 * ixgbe_local_timer - Driver watchdog function.
 *
 * This function will handle the transmit stall check and other routines.
 */
static void
ixgbe_local_timer(void *arg)
{
	ixgbe_t *ixgbe = (ixgbe_t *)arg;
	ixgbe_rx_ring_t *rx_ring;
	int i, v_idx;

	if (ixgbe->ixgbe_state & IXGBE_OVERTEMP)
		goto out;

	if (ixgbe->ixgbe_state & IXGBE_ERROR) {
		ixgbe->reset_count++;
		if (ixgbe_reset(ixgbe) == IXGBE_SUCCESS) {
			/*
			 * We have already started the watchdog timer in
			 * ixgbe_reset().
			 */
			ddi_fm_service_impact(ixgbe->dip, DDI_SERVICE_RESTORED);
			return;
		}
	} else if (ixgbe_stall_check(ixgbe)) {
		atomic_or_32(&ixgbe->ixgbe_state, IXGBE_STALL);
		ddi_fm_service_impact(ixgbe->dip, DDI_SERVICE_DEGRADED);

		ixgbe->reset_count++;
		if (ixgbe_reset(ixgbe) == IXGBE_SUCCESS) {
			/*
			 * We have already started the watchdog timer in
			 * ixgbe_reset().
			 */
			ddi_fm_service_impact(ixgbe->dip, DDI_SERVICE_RESTORED);
			return;
		}
	}

	if (ixgbe->ixgbe_state & IXGBE_STARTED) {
		for (i = 0; i < ixgbe->num_rx_rings; i++) {
			rx_ring = &ixgbe->rx_rings[i];
			v_idx = rx_ring->intr_vector;

			mutex_enter(&ixgbe->gen_lock);

			IXGBE_WRITE_REG(&ixgbe->hw, IXGBE_EICS, (1 << v_idx));
			IXGBE_WRITE_FLUSH(&ixgbe->hw);

			mutex_exit(&ixgbe->gen_lock);
		}
	}

out:
	ixgbe_restart_watchdog_timer(ixgbe);
}

/*
 * ixgbe_stall_check - Check for transmit stall.
 *
 * This function checks if the adapter is stalled (in transmit).
 *
 * It is called each time the watchdog timeout is invoked.
 * If the transmit descriptor reclaim continuously fails,
 * the watchdog value will increment by 1. If the watchdog
 * value exceeds the threshold, the ixgbe is assumed to
 * have stalled and need to be reset.
 */
static boolean_t
ixgbe_stall_check(ixgbe_t *ixgbe)
{
	ixgbe_tx_ring_t *tx_ring;
	boolean_t result;
	int i;

	if (ixgbe->link_state != LINK_STATE_UP)
		return (B_FALSE);

	/*
	 * If any tx ring is stalled, we'll reset the chipset
	 */
	result = B_FALSE;
	for (i = 0; i < ixgbe->num_tx_rings; i++) {
		tx_ring = &ixgbe->tx_rings[i];
		if (tx_ring->tbd_free <= ixgbe->tx_recycle_thresh) {
			tx_ring->tx_recycle(tx_ring);
		}

		if (tx_ring->recycle_fail > 0)
			tx_ring->stall_watchdog++;
		else
			tx_ring->stall_watchdog = 0;

		if (tx_ring->stall_watchdog >= STALL_WATCHDOG_TIMEOUT) {
			result = B_TRUE;
			break;
		}
	}

	if (result) {
		tx_ring->stall_watchdog = 0;
		tx_ring->recycle_fail = 0;
	}

	return (result);
}


/*
 * ixgbe_is_valid_mac_addr - Check if the mac address is valid.
 */
boolean_t
ixgbe_is_valid_mac_addr(uint8_t *mac_addr)
{
	const uint8_t addr_test1[6] = { 0, 0, 0, 0, 0, 0 };
	const uint8_t addr_test2[6] =
	    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

	if (!(bcmp(addr_test1, mac_addr, ETHERADDRL)) ||
	    !(bcmp(addr_test2, mac_addr, ETHERADDRL)))
		return (B_FALSE);

	return (B_TRUE);
}

static boolean_t
ixgbe_find_mac_address(ixgbe_t *ixgbe)
{
#ifdef __sparc
	struct ixgbe_hw *hw = &ixgbe->hw;
	uchar_t *bytes;
	struct ether_addr sysaddr;
	uint_t nelts;
	int err;
	boolean_t found = B_FALSE;

	/*
	 * The "vendor's factory-set address" may already have
	 * been extracted from the chip, but if the property
	 * "local-mac-address" is set we use that instead.
	 *
	 * We check whether it looks like an array of 6
	 * bytes (which it should, if OBP set it).  If we can't
	 * make sense of it this way, we'll ignore it.
	 */
	err = ddi_prop_lookup_byte_array(DDI_DEV_T_ANY, ixgbe->dip,
	    DDI_PROP_DONTPASS, "local-mac-address", &bytes, &nelts);
	if (err == DDI_PROP_SUCCESS) {
		if (nelts == ETHERADDRL) {
			while (nelts--)
				hw->mac.addr[nelts] = bytes[nelts];
			found = B_TRUE;
		}
		ddi_prop_free(bytes);
	}

	/*
	 * Look up the OBP property "local-mac-address?". If the user has set
	 * 'local-mac-address? = false', use "the system address" instead.
	 */
	if (ddi_prop_lookup_byte_array(DDI_DEV_T_ANY, ixgbe->dip, 0,
	    "local-mac-address?", &bytes, &nelts) == DDI_PROP_SUCCESS) {
		if (strncmp("false", (caddr_t)bytes, (size_t)nelts) == 0) {
			if (localetheraddr(NULL, &sysaddr) != 0) {
				bcopy(&sysaddr, hw->mac.addr, ETHERADDRL);
				found = B_TRUE;
			}
		}
		ddi_prop_free(bytes);
	}

	/*
	 * Finally(!), if there's a valid "mac-address" property (created
	 * if we netbooted from this interface), we must use this instead
	 * of any of the above to ensure that the NFS/install server doesn't
	 * get confused by the address changing as Solaris takes over!
	 */
	err = ddi_prop_lookup_byte_array(DDI_DEV_T_ANY, ixgbe->dip,
	    DDI_PROP_DONTPASS, "mac-address", &bytes, &nelts);
	if (err == DDI_PROP_SUCCESS) {
		if (nelts == ETHERADDRL) {
			while (nelts--)
				hw->mac.addr[nelts] = bytes[nelts];
			found = B_TRUE;
		}
		ddi_prop_free(bytes);
	}

	if (found) {
		bcopy(hw->mac.addr, hw->mac.perm_addr, ETHERADDRL);
		return (B_TRUE);
	}
#else
	_NOTE(ARGUNUSED(ixgbe));
#endif

	return (B_TRUE);
}

#pragma inline(ixgbe_arm_watchdog_timer)
static void
ixgbe_arm_watchdog_timer(ixgbe_t *ixgbe)
{
	/*
	 * Fire a watchdog timer
	 */
	ixgbe->watchdog_tid =
	    timeout(ixgbe_local_timer,
	    (void *)ixgbe, 1 * drv_usectohz(1000000));

}

/*
 * ixgbe_enable_watchdog_timer - Enable and start the driver watchdog timer.
 */
void
ixgbe_enable_watchdog_timer(ixgbe_t *ixgbe)
{
	mutex_enter(&ixgbe->watchdog_lock);

	if (!ixgbe->watchdog_enable) {
		ixgbe->watchdog_enable = B_TRUE;
		ixgbe->watchdog_start = B_TRUE;
		ixgbe_arm_watchdog_timer(ixgbe);
	}

	mutex_exit(&ixgbe->watchdog_lock);
}

/*
 * ixgbe_disable_watchdog_timer - Disable and stop the driver watchdog timer.
 */
void
ixgbe_disable_watchdog_timer(ixgbe_t *ixgbe)
{
	timeout_id_t tid;

	mutex_enter(&ixgbe->watchdog_lock);

	ixgbe->watchdog_enable = B_FALSE;
	ixgbe->watchdog_start = B_FALSE;
	tid = ixgbe->watchdog_tid;
	ixgbe->watchdog_tid = 0;

	mutex_exit(&ixgbe->watchdog_lock);

	if (tid != 0)
		(void) untimeout(tid);
}

/*
 * ixgbe_start_watchdog_timer - Start the driver watchdog timer.
 */
void
ixgbe_start_watchdog_timer(ixgbe_t *ixgbe)
{
	mutex_enter(&ixgbe->watchdog_lock);

	if (ixgbe->watchdog_enable) {
		if (!ixgbe->watchdog_start) {
			ixgbe->watchdog_start = B_TRUE;
			ixgbe_arm_watchdog_timer(ixgbe);
		}
	}

	mutex_exit(&ixgbe->watchdog_lock);
}

/*
 * ixgbe_restart_watchdog_timer - Restart the driver watchdog timer.
 */
static void
ixgbe_restart_watchdog_timer(ixgbe_t *ixgbe)
{
	mutex_enter(&ixgbe->watchdog_lock);

	if (ixgbe->watchdog_start)
		ixgbe_arm_watchdog_timer(ixgbe);

	mutex_exit(&ixgbe->watchdog_lock);
}

/*
 * ixgbe_stop_watchdog_timer - Stop the driver watchdog timer.
 */
void
ixgbe_stop_watchdog_timer(ixgbe_t *ixgbe)
{
	timeout_id_t tid;

	mutex_enter(&ixgbe->watchdog_lock);

	ixgbe->watchdog_start = B_FALSE;
	tid = ixgbe->watchdog_tid;
	ixgbe->watchdog_tid = 0;

	mutex_exit(&ixgbe->watchdog_lock);

	if (tid != 0)
		(void) untimeout(tid);
}

/*
 * ixgbe_disable_adapter_interrupts - Disable all adapter interrupts.
 */
static void
ixgbe_disable_adapter_interrupts(ixgbe_t *ixgbe)
{
	struct ixgbe_hw *hw = &ixgbe->hw;

	/*
	 * mask all interrupts off
	 */
	IXGBE_WRITE_REG(hw, IXGBE_EIMC, 0xffffffff);

	/*
	 * for MSI-X, also disable autoclear
	 */
	if (ixgbe->intr_type == DDI_INTR_TYPE_MSIX) {
		IXGBE_WRITE_REG(hw, IXGBE_EIAC, 0x0);
	}

	IXGBE_WRITE_FLUSH(hw);
}

static void
ixgbe_enable_hw_interrupts_82598eb(ixgbe_t *ixgbe)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	uint32_t eiac, eiam;
	uint32_t gpie = IXGBE_READ_REG(hw, IXGBE_GPIE);

	/* interrupt types to enable */
	ixgbe->eims = IXGBE_EIMS_ENABLE_MASK;	/* shared code default */
	ixgbe->eims &= ~IXGBE_EIMS_TCP_TIMER;	/* minus tcp timer */
	ixgbe->eims |= ixgbe->capab->other_intr; /* "other" interrupt types */

	/* enable automask on "other" causes that this adapter can generate */
	eiam = ixgbe->capab->other_intr;

	/*
	 * msi-x mode
	 */
	if (ixgbe->intr_type == DDI_INTR_TYPE_MSIX) {
		/* enable autoclear but not on bits 29:20 */
		eiac = (ixgbe->eims & ~IXGBE_OTHER_INTR);

		/* general purpose interrupt enable */
		gpie |= (IXGBE_GPIE_MSIX_MODE
		    | IXGBE_GPIE_PBA_SUPPORT
		    | IXGBE_GPIE_OCD
		    | IXGBE_GPIE_EIAME);
	/*
	 * non-msi-x mode
	 */
	} else {

		/* disable autoclear, leave gpie at default */
		eiac = 0;

		/*
		 * General purpose interrupt enable.
		 */
		gpie |= IXGBE_GPIE_EIAME;
	}

	/* Enable specific "other" interrupt types */
	gpie |= ixgbe->capab->other_gpie;

	/* write to interrupt control registers */
	IXGBE_WRITE_REG(hw, IXGBE_EIMS, ixgbe->eims);
	IXGBE_WRITE_REG(hw, IXGBE_EIAC, eiac);
	IXGBE_WRITE_REG(hw, IXGBE_EIAM, eiam);
	IXGBE_WRITE_REG(hw, IXGBE_GPIE, gpie);
	IXGBE_WRITE_FLUSH(hw);
}

static void
ixgbe_enable_hw_interrupts_generic(ixgbe_t *ixgbe)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	uint32_t eiac, eiam;
	uint32_t gpie = IXGBE_READ_REG(hw, IXGBE_GPIE);

	/* interrupt types to enable */
	ixgbe->eims = IXGBE_READ_REG(hw, IXGBE_EIMS);
	ixgbe->eims |= IXGBE_EIMS_ENABLE_MASK;	/* shared code default */
	ixgbe->eims &= ~IXGBE_EIMS_TCP_TIMER;	/* minus tcp timer */
	ixgbe->eims |= ixgbe->capab->other_intr; /* "other" interrupt types */

	/* enable automask on "other" causes except Mailbox Interrupt */
	if (ixgbe->sriov_pf)
		eiam = ixgbe->capab->other_intr & ~IXGBE_EIMS_MAILBOX;
	else
		eiam = ixgbe->capab->other_intr;

	/*
	 * msi-x mode
	 */
	if (ixgbe->intr_type == DDI_INTR_TYPE_MSIX) {
		/* enable autoclear but not on bits 29:20 */
		eiac = (ixgbe->eims & ~IXGBE_OTHER_INTR & ~IXGBE_EIMS_MAILBOX);

		/* general purpose interrupt enable */
		gpie |= (IXGBE_GPIE_MSIX_MODE
		    | IXGBE_GPIE_PBA_SUPPORT
		    | IXGBE_GPIE_OCD
		    | IXGBE_GPIE_EIAME);
	/*
	 * non-msi-x mode
	 */
	} else {

		/* disable autoclear, leave gpie at default */
		eiac = 0;

		/*
		 * General purpose interrupt enable.
		 * For 82599, extended interrupt automask enable
		 * only in MSI or MSI-X mode
		 */
		if (ixgbe->intr_type == DDI_INTR_TYPE_MSI)
			gpie |= IXGBE_GPIE_EIAME;
	}

	/* Enable specific "other" interrupt types */
	gpie |= ixgbe->capab->other_gpie;

	/* Enable RSC Delay 8us when LRO enabled  */
	if (ixgbe->lro_enable) {
		gpie |= (1 << IXGBE_GPIE_RSC_DELAY_SHIFT);
	}
	/* write to interrupt control registers */
	IXGBE_WRITE_REG(hw, IXGBE_EIMS, ixgbe->eims);
	IXGBE_WRITE_REG(hw, IXGBE_EIAC, eiac);
	IXGBE_WRITE_REG(hw, IXGBE_EIAM, eiam);
	if (!ixgbe->sriov_pf) {
		IXGBE_WRITE_REG(hw, IXGBE_GPIE, gpie);
	}
	IXGBE_WRITE_FLUSH(hw);
}

/*
 * ixgbe_enable_sriov_interrupt - configure SR-IOV related interrupt registers
 *
 * This should only be called for NIC acting as PF driver; therefore
 * assume MSI-X interrupts and VF mailboxes.
 */
void
ixgbe_enable_sriov_interrupt(ixgbe_t *ixgbe)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	uint32_t eiac, eimc, eitrsel;
	uint32_t gpie = IXGBE_READ_REG(hw, IXGBE_GPIE);
	uint32_t gcr_ext = IXGBE_READ_REG(hw, IXGBE_GCR_EXT);

	/* Clear any pending interrupts */
	(void) IXGBE_READ_REG(hw, IXGBE_EICR);

	ixgbe->eims |= IXGBE_EIMS_MAILBOX;

	/* enable autoclear but not on bits 29:20 */
	eiac = (ixgbe->eims & ~IXGBE_OTHER_INTR & ~IXGBE_EIMS_MAILBOX);

	/* disable rx/tx interrupts */
	eimc = ixgbe->eims & ~IXGBE_EIMS_MAILBOX;

	if (ixgbe->num_vfs > 32) {
		eitrsel = (1 << (ixgbe->num_vfs - 32)) - 1;
		IXGBE_WRITE_REG(&ixgbe->hw, IXGBE_EITRSEL, eitrsel);
	}

	/* clear previous SR-IOV settings */
	gcr_ext &= ~(IXGBE_GCR_EXT_SRIOV);

	gcr_ext |= IXGBE_GCR_EXT_MSIX_EN;
	if (ixgbe->iov_mode == IOV_64VF)
		gcr_ext |= IXGBE_GCR_EXT_VT_MODE_64;
	else if (ixgbe->iov_mode == IOV_32VF)
		gcr_ext |= IXGBE_GCR_EXT_VT_MODE_32;

	/* general purpose interrupt enable */
	gpie |= (IXGBE_GPIE_MSIX_MODE
	    | IXGBE_GPIE_PBA_SUPPORT
	    | IXGBE_GPIE_OCD
	    | IXGBE_GPIE_EIAME);

	gpie &= ~IXGBE_GPIE_VTMODE_MASK;
	if (ixgbe->iov_mode == IOV_64VF)
		gpie |= IXGBE_GPIE_VTMODE_64;
	else if (ixgbe->iov_mode == IOV_32VF)
		gpie |= IXGBE_GPIE_VTMODE_32;

	/* Enable specific "other" interrupt types */
	gpie |= ixgbe->capab->other_gpie;

	/* Enable RSC Delay 8us when LRO enabled  */
	if (ixgbe->lro_enable) {
		gpie |= (1 << IXGBE_GPIE_RSC_DELAY_SHIFT);
	}

	IXGBE_WRITE_REG(hw, IXGBE_EIMS, ixgbe->eims);
	/* Disable auto-mask for EICR interrupt bits */
	IXGBE_WRITE_REG(hw, IXGBE_EIAM, 0);
	IXGBE_WRITE_REG(hw, IXGBE_EIAC, eiac);
	IXGBE_WRITE_REG(hw, IXGBE_EIMC, eimc);
	IXGBE_WRITE_REG(hw, IXGBE_GCR_EXT, gcr_ext);
	IXGBE_WRITE_REG(hw, IXGBE_GPIE, gpie);

	IXGBE_WRITE_FLUSH(hw);
}

/*
 * ixgbe_loopback_ioctl - Loopback support.
 */
enum ioc_reply
ixgbe_loopback_ioctl(ixgbe_t *ixgbe, struct iocblk *iocp, mblk_t *mp)
{
	lb_info_sz_t *lbsp;
	lb_property_t *lbpp;
	uint32_t *lbmp;
	uint32_t size;
	uint32_t value;

	if (mp->b_cont == NULL)
		return (IOC_INVAL);

	switch (iocp->ioc_cmd) {
	default:
		return (IOC_INVAL);

	case LB_GET_INFO_SIZE:
		size = sizeof (lb_info_sz_t);
		if (iocp->ioc_count != size)
			return (IOC_INVAL);

		value = sizeof (lb_normal);
		value += sizeof (lb_mac);
		value += sizeof (lb_external);

		lbsp = (lb_info_sz_t *)(uintptr_t)mp->b_cont->b_rptr;
		*lbsp = value;
		break;

	case LB_GET_INFO:
		value = sizeof (lb_normal);
		value += sizeof (lb_mac);
		value += sizeof (lb_external);

		size = value;
		if (iocp->ioc_count != size)
			return (IOC_INVAL);

		value = 0;
		lbpp = (lb_property_t *)(uintptr_t)mp->b_cont->b_rptr;

		lbpp[value++] = lb_normal;
		lbpp[value++] = lb_mac;
		lbpp[value++] = lb_external;
		break;

	case LB_GET_MODE:
		size = sizeof (uint32_t);
		if (iocp->ioc_count != size)
			return (IOC_INVAL);

		lbmp = (uint32_t *)(uintptr_t)mp->b_cont->b_rptr;
		*lbmp = ixgbe->loopback_mode;
		break;

	case LB_SET_MODE:
		size = 0;
		if (iocp->ioc_count != sizeof (uint32_t))
			return (IOC_INVAL);

		lbmp = (uint32_t *)(uintptr_t)mp->b_cont->b_rptr;
		if (!ixgbe_set_loopback_mode(ixgbe, *lbmp))
			return (IOC_INVAL);
		break;
	}

	iocp->ioc_count = size;
	iocp->ioc_error = 0;

	if (ixgbe_check_acc_handle(ixgbe->osdep.reg_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(ixgbe->dip, DDI_SERVICE_DEGRADED);
		return (IOC_INVAL);
	}

	return (IOC_REPLY);
}

/*
 * ixgbe_set_loopback_mode - Setup loopback based on the loopback mode.
 */
static boolean_t
ixgbe_set_loopback_mode(ixgbe_t *ixgbe, uint32_t mode)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	uint8_t atlas;

	if (mode == ixgbe->loopback_mode)
		return (B_TRUE);

	ixgbe->loopback_mode = mode;

	if (mode == IXGBE_LB_NONE) {
		/*
		 * Clear the bits set in mac loopback.
		 */
		if (hw->mac.type == ixgbe_mac_82598EB) {
			(void) ixgbe_read_analog_reg8(hw, IXGBE_ATLAS_PDN_LPBK,
			    &atlas);
			atlas &= ~IXGBE_ATLAS_PDN_TX_REG_EN;
			(void) ixgbe_write_analog_reg8(hw, IXGBE_ATLAS_PDN_LPBK,
			    atlas);

			(void) ixgbe_read_analog_reg8(hw, IXGBE_ATLAS_PDN_10G,
			    &atlas);
			atlas &= ~IXGBE_ATLAS_PDN_TX_10G_QL_ALL;
			(void) ixgbe_write_analog_reg8(hw, IXGBE_ATLAS_PDN_10G,
			    atlas);

			(void) ixgbe_read_analog_reg8(hw, IXGBE_ATLAS_PDN_1G,
			    &atlas);
			atlas &= ~IXGBE_ATLAS_PDN_TX_1G_QL_ALL;
			(void) ixgbe_write_analog_reg8(hw, IXGBE_ATLAS_PDN_1G,
			    atlas);

			(void) ixgbe_read_analog_reg8(hw, IXGBE_ATLAS_PDN_AN,
			    &atlas);
			atlas &= ~IXGBE_ATLAS_PDN_TX_AN_QL_ALL;
			(void) ixgbe_write_analog_reg8(hw, IXGBE_ATLAS_PDN_AN,
			    atlas);
		}

		/*
		 * Reset the chip
		 */
		(void) ixgbe_reset(ixgbe);
		return (B_TRUE);
	}

	mutex_enter(&ixgbe->gen_lock);

	switch (mode) {
	default:
		mutex_exit(&ixgbe->gen_lock);
		return (B_FALSE);

	case IXGBE_LB_EXTERNAL:
		break;

	case IXGBE_LB_INTERNAL_MAC:
		ixgbe_set_internal_mac_loopback(ixgbe);
		break;
	}

	mutex_exit(&ixgbe->gen_lock);

	return (B_TRUE);
}

/*
 * ixgbe_set_internal_mac_loopback - Set the internal MAC loopback mode.
 */
static void
ixgbe_set_internal_mac_loopback(ixgbe_t *ixgbe)
{
	struct ixgbe_hw *hw;
	uint32_t reg;
	uint16_t autoneg_reg;
	uint8_t atlas;

	hw = &ixgbe->hw;

	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		reg = IXGBE_READ_REG(&ixgbe->hw, IXGBE_HLREG0);
		reg |= IXGBE_HLREG0_LPBK;
		IXGBE_WRITE_REG(&ixgbe->hw, IXGBE_HLREG0, reg);

		reg = IXGBE_READ_REG(&ixgbe->hw, IXGBE_AUTOC);
		reg &= ~IXGBE_AUTOC_LMS_MASK;
		reg |= (IXGBE_AUTOC_FLU |
		    IXGBE_AUTOC_10G_KX4 |
		    IXGBE_AUTOC_LMS_10G_LINK_NO_AN);
		IXGBE_WRITE_REG(&ixgbe->hw, IXGBE_AUTOC, reg);

		/*
		 * Disable Atlas Tx lanes to keep packets in
		 * loopback and not on wire
		 */
		(void) ixgbe_read_analog_reg8(&ixgbe->hw, IXGBE_ATLAS_PDN_LPBK,
		    &atlas);
		atlas |= IXGBE_ATLAS_PDN_TX_REG_EN;
		(void) ixgbe_write_analog_reg8(&ixgbe->hw, IXGBE_ATLAS_PDN_LPBK,
		    atlas);

		(void) ixgbe_read_analog_reg8(&ixgbe->hw, IXGBE_ATLAS_PDN_10G,
		    &atlas);
		atlas |= IXGBE_ATLAS_PDN_TX_10G_QL_ALL;
		(void) ixgbe_write_analog_reg8(&ixgbe->hw, IXGBE_ATLAS_PDN_10G,
		    atlas);

		(void) ixgbe_read_analog_reg8(&ixgbe->hw, IXGBE_ATLAS_PDN_1G,
		    &atlas);
		atlas |= IXGBE_ATLAS_PDN_TX_1G_QL_ALL;
		(void) ixgbe_write_analog_reg8(&ixgbe->hw, IXGBE_ATLAS_PDN_1G,
		    atlas);

		(void) ixgbe_read_analog_reg8(&ixgbe->hw, IXGBE_ATLAS_PDN_AN,
		    &atlas);
		atlas |= IXGBE_ATLAS_PDN_TX_AN_QL_ALL;
		(void) ixgbe_write_analog_reg8(&ixgbe->hw, IXGBE_ATLAS_PDN_AN,
		    atlas);
		break;
	case ixgbe_mac_82599EB:
		reg = IXGBE_READ_REG(&ixgbe->hw, IXGBE_HLREG0);
		reg |= IXGBE_HLREG0_LPBK;
		IXGBE_WRITE_REG(&ixgbe->hw, IXGBE_HLREG0, reg);

		reg = IXGBE_READ_REG(&ixgbe->hw, IXGBE_AUTOC);
		reg &= ~IXGBE_AUTOC_LMS_MASK;
		reg |= (IXGBE_AUTOC_FLU |
		    IXGBE_AUTOC_10G_KX4 |
		    IXGBE_AUTOC_LMS_10G_LINK_NO_AN);
		IXGBE_WRITE_REG(&ixgbe->hw, IXGBE_AUTOC, reg);
		break;
	case ixgbe_mac_X540:
		/*
		 * Disable auto negotiation in PHY
		 */
		hw->phy.ops.read_reg(hw, IXGBE_MDIO_AUTO_NEG_CONTROL,
		    IXGBE_MDIO_AUTO_NEG_DEV_TYPE, &autoneg_reg);
		autoneg_reg &= ~0x1000;
		hw->phy.ops.write_reg(hw, IXGBE_MDIO_AUTO_NEG_CONTROL,
		    IXGBE_MDIO_AUTO_NEG_DEV_TYPE, autoneg_reg);

		hw->phy.ops.write_reg(hw, IXGBE_MDIO_PMA_PMD_CONTROL_ADDR,
		    IXGBE_MDIO_PMA_PMD_DEV_TYPE, 0x2040);
		hw->phy.ops.write_reg(hw, 0, IXGBE_MDIO_PCS_DEV_TYPE, 0x2040);

		reg = IXGBE_READ_REG(&ixgbe->hw, IXGBE_MACC);
		reg |= IXGBE_MACC_FLU;
		IXGBE_WRITE_REG(&ixgbe->hw, IXGBE_MACC, reg);

		reg = IXGBE_READ_REG(&ixgbe->hw, IXGBE_HLREG0);
		reg |= IXGBE_HLREG0_LPBK;
		IXGBE_WRITE_REG(&ixgbe->hw, IXGBE_HLREG0, reg);
		break;
	default:
		break;
	}
}

#pragma inline(ixgbe_intr_rx_work)
/*
 * ixgbe_intr_rx_work - RX processing of ISR.
 */
static void
ixgbe_intr_rx_work(ixgbe_rx_ring_t *rx_ring)
{
	mblk_t *mp;

	mutex_enter(&rx_ring->rx_lock);
	mp = ixgbe_ring_rx(rx_ring, IXGBE_POLL_NULL, IXGBE_POLL_NULL);
	mutex_exit(&rx_ring->rx_lock);

	if (mp != NULL)
		mac_rx_ring(rx_ring->ixgbe->mac_hdl, rx_ring->ring_handle, mp,
		    rx_ring->ring_gen_num);
}

#pragma inline(ixgbe_intr_tx_work)
/*
 * ixgbe_intr_tx_work - TX processing of ISR.
 */
static void
ixgbe_intr_tx_work(ixgbe_tx_ring_t *tx_ring)
{
	ixgbe_t *ixgbe = tx_ring->ixgbe;

	/*
	 * Recycle the tx descriptors
	 */
	tx_ring->tx_recycle(tx_ring);

	/*
	 * Schedule the re-transmit
	 */
	if (tx_ring->reschedule &&
	    (tx_ring->tbd_free >= ixgbe->tx_resched_thresh)) {
		tx_ring->reschedule = B_FALSE;
		mac_tx_ring_update(tx_ring->ixgbe->mac_hdl,
		    tx_ring->ring_handle);
		IXGBE_DEBUG_STAT(tx_ring->stat_reschedule);
	}
}

#pragma inline(ixgbe_intr_other_work)
/*
 * ixgbe_intr_other_work - Process interrupt types other than tx/rx
 */
static void
ixgbe_intr_other_work(ixgbe_t *ixgbe, uint32_t eicr)
{
	ASSERT(mutex_owned(&ixgbe->gen_lock));

	/*
	 * handle link status change
	 */
	if (eicr & IXGBE_EICR_LSC) {
		ixgbe_driver_link_check(ixgbe);
		ixgbe_get_hw_state(ixgbe);
	}

	/* Check for mailbox event from VF driver */
	if (eicr & IXGBE_EICR_MAILBOX) {
		ixgbe_msg_task(ixgbe);
	}

	/*
	 * check for fan failure on adapters with fans
	 */
	if ((ixgbe->capab->flags & IXGBE_FLAG_FAN_FAIL_CAPABLE) &&
	    (eicr & IXGBE_EICR_GPI_SDP1)) {
		atomic_or_32(&ixgbe->ixgbe_state, IXGBE_OVERTEMP);

		/*
		 * Disable the adapter interrupts
		 */
		ixgbe_disable_adapter_interrupts(ixgbe);

		/*
		 * Disable Rx/Tx units
		 */
		(void) ixgbe_stop_adapter(&ixgbe->hw);

		ddi_fm_service_impact(ixgbe->dip, DDI_SERVICE_LOST);
		ixgbe_error(ixgbe,
		    "Problem: Network adapter has been stopped "
		    "because the fan has stopped.\n");
		ixgbe_error(ixgbe,
		    "Action: Replace the adapter.\n");

		/* re-enable the interrupt, which was automasked */
		ixgbe->eims |= IXGBE_EICR_GPI_SDP1;
	}

	/*
	 * Do SFP check for adapters with hot-plug capability
	 */
	if ((ixgbe->capab->flags & IXGBE_FLAG_SFP_PLUG_CAPABLE) &&
	    ((eicr & IXGBE_EICR_GPI_SDP1) || (eicr & IXGBE_EICR_GPI_SDP2))) {
		ixgbe->eicr = eicr;
		if ((ddi_taskq_dispatch(ixgbe->sfp_taskq,
		    ixgbe_sfp_check, (void *)ixgbe,
		    DDI_NOSLEEP)) != DDI_SUCCESS) {
			ixgbe_log(ixgbe, "No memory available to dispatch "
			    "taskq for SFP check");
		}
	}

	/*
	 * Do over-temperature check for adapters with temp sensor
	 */
	if ((ixgbe->capab->flags & IXGBE_FLAG_TEMP_SENSOR_CAPABLE) &&
	    ((eicr & IXGBE_EICR_GPI_SDP0) || (eicr & IXGBE_EICR_LSC))) {
		ixgbe->eicr = eicr;
		if ((ddi_taskq_dispatch(ixgbe->overtemp_taskq,
		    ixgbe_overtemp_check, (void *)ixgbe,
		    DDI_NOSLEEP)) != DDI_SUCCESS) {
			ixgbe_log(ixgbe, "No memory available to dispatch "
			    "taskq for overtemp check");
		}
	}
}

/*
 * ixgbe_intr_legacy - Interrupt handler for legacy interrupts.
 */
static uint_t
ixgbe_intr_legacy(void *arg1, void *arg2)
{
	ixgbe_t *ixgbe = (ixgbe_t *)arg1;
	struct ixgbe_hw *hw = &ixgbe->hw;
	ixgbe_tx_ring_t *tx_ring;
	ixgbe_rx_ring_t *rx_ring;
	uint32_t eicr;
	mblk_t *mp;
	boolean_t tx_reschedule;
	uint_t result;

	_NOTE(ARGUNUSED(arg2));

	mutex_enter(&ixgbe->gen_lock);
	if (ixgbe->ixgbe_state & IXGBE_SUSPENDED) {
		mutex_exit(&ixgbe->gen_lock);
		return (DDI_INTR_UNCLAIMED);
	}

	mp = NULL;
	tx_reschedule = B_FALSE;

	/*
	 * Any bit set in eicr: claim this interrupt
	 */
	eicr = IXGBE_READ_REG(hw, IXGBE_EICR);

	if (ixgbe_check_acc_handle(ixgbe->osdep.reg_handle) != DDI_FM_OK) {
		mutex_exit(&ixgbe->gen_lock);
		ddi_fm_service_impact(ixgbe->dip, DDI_SERVICE_DEGRADED);
		atomic_or_32(&ixgbe->ixgbe_state, IXGBE_ERROR);
		return (DDI_INTR_CLAIMED);
	}

	if (eicr) {
		/*
		 * For legacy interrupt, we have only one interrupt,
		 * so we have only one rx ring and one tx ring enabled.
		 */
		ASSERT(ixgbe->num_rx_rings == 1);
		ASSERT(ixgbe->num_tx_rings == 1);

		/*
		 * For legacy interrupt, rx rings[0] will use RTxQ[0].
		 */
		if (eicr & 0x1) {
			ixgbe->eimc |= IXGBE_EICR_RTX_QUEUE;
			IXGBE_WRITE_REG(hw, IXGBE_EIMC, ixgbe->eimc);
			ixgbe->eims |= IXGBE_EICR_RTX_QUEUE;
			/*
			 * Clean the rx descriptors
			 */
			rx_ring = &ixgbe->rx_rings[0];
			mutex_enter(&rx_ring->rx_lock);
			mp = ixgbe_ring_rx(rx_ring, IXGBE_POLL_NULL,
			    IXGBE_POLL_NULL);
			mutex_exit(&rx_ring->rx_lock);
		}

		/*
		 * For legacy interrupt, tx rings[0] will use RTxQ[1].
		 */
		if (eicr & 0x2) {
			/*
			 * Recycle the tx descriptors
			 */
			tx_ring = &ixgbe->tx_rings[0];
			tx_ring->tx_recycle(tx_ring);

			/*
			 * Schedule the re-transmit
			 */
			tx_reschedule = (tx_ring->reschedule &&
			    (tx_ring->tbd_free >= ixgbe->tx_resched_thresh));
		}

		/* any interrupt type other than tx/rx */
		if (eicr & ixgbe->capab->other_intr) {
			switch (hw->mac.type) {
			case ixgbe_mac_82598EB:
				ixgbe->eims &= ~(eicr & IXGBE_OTHER_INTR);
				break;

			case ixgbe_mac_82599EB:
			case ixgbe_mac_X540:
				ixgbe->eimc = IXGBE_82599_OTHER_INTR;
				IXGBE_WRITE_REG(hw, IXGBE_EIMC, ixgbe->eimc);
				break;

			default:
				break;
			}
			ixgbe_intr_other_work(ixgbe, eicr);
			ixgbe->eims &= ~(eicr & IXGBE_OTHER_INTR);
		}

		mutex_exit(&ixgbe->gen_lock);

		result = DDI_INTR_CLAIMED;
	} else {
		mutex_exit(&ixgbe->gen_lock);

		/*
		 * No interrupt cause bits set: don't claim this interrupt.
		 */
		result = DDI_INTR_UNCLAIMED;
	}

	/* re-enable the interrupts which were automasked */
	IXGBE_WRITE_REG(hw, IXGBE_EIMS, ixgbe->eims);

	/*
	 * Do the following work outside of the gen_lock
	 */
	if (mp != NULL) {
		mac_rx_ring(rx_ring->ixgbe->mac_hdl, rx_ring->ring_handle, mp,
		    rx_ring->ring_gen_num);
	}

	if (tx_reschedule)  {
		tx_ring->reschedule = B_FALSE;
		mac_tx_ring_update(ixgbe->mac_hdl, tx_ring->ring_handle);
		IXGBE_DEBUG_STAT(tx_ring->stat_reschedule);
	}

	return (result);
}

/*
 * ixgbe_intr_msi - Interrupt handler for MSI.
 */
static uint_t
ixgbe_intr_msi(void *arg1, void *arg2)
{
	ixgbe_t *ixgbe = (ixgbe_t *)arg1;
	struct ixgbe_hw *hw = &ixgbe->hw;
	uint32_t eicr;

	_NOTE(ARGUNUSED(arg2));

	eicr = IXGBE_READ_REG(hw, IXGBE_EICR);

	if (ixgbe_check_acc_handle(ixgbe->osdep.reg_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(ixgbe->dip, DDI_SERVICE_DEGRADED);
		atomic_or_32(&ixgbe->ixgbe_state, IXGBE_ERROR);
		return (DDI_INTR_CLAIMED);
	}

	/*
	 * For MSI interrupt, we have only one vector,
	 * so we have only one rx ring and one tx ring enabled.
	 */
	ASSERT(ixgbe->num_rx_rings == 1);
	ASSERT(ixgbe->num_tx_rings == 1);

	/*
	 * For MSI interrupt, rx rings[0] will use RTxQ[0].
	 */
	if (eicr & 0x1) {
		ixgbe_intr_rx_work(&ixgbe->rx_rings[0]);
	}

	/*
	 * For MSI interrupt, tx rings[0] will use RTxQ[1].
	 */
	if (eicr & 0x2) {
		ixgbe_intr_tx_work(&ixgbe->tx_rings[0]);
	}

	/* any interrupt type other than tx/rx */
	if (eicr & ixgbe->capab->other_intr) {
		mutex_enter(&ixgbe->gen_lock);
		switch (hw->mac.type) {
		case ixgbe_mac_82598EB:
			ixgbe->eims &= ~(eicr & IXGBE_OTHER_INTR);
			break;

		case ixgbe_mac_82599EB:
		case ixgbe_mac_X540:
			ixgbe->eimc = IXGBE_82599_OTHER_INTR;
			IXGBE_WRITE_REG(hw, IXGBE_EIMC, ixgbe->eimc);
			break;

		default:
			break;
		}
		ixgbe_intr_other_work(ixgbe, eicr);
		ixgbe->eims &= ~(eicr & IXGBE_OTHER_INTR);
		mutex_exit(&ixgbe->gen_lock);
	}

	/* re-enable the interrupts which were automasked */
	IXGBE_WRITE_REG(hw, IXGBE_EIMS, ixgbe->eims);

	return (DDI_INTR_CLAIMED);
}

/*
 * ixgbe_intr_msix - Interrupt handler for MSI-X.
 */
static uint_t
ixgbe_intr_msix(void *arg1, void *arg2)
{
	ixgbe_intr_vector_t *vect = (ixgbe_intr_vector_t *)arg1;
	ixgbe_t *ixgbe = vect->ixgbe;
	struct ixgbe_hw *hw = &ixgbe->hw;
	uint32_t eicr;
	int r_idx = 0;

	_NOTE(ARGUNUSED(arg2));

	if (ixgbe->ixgbe_state & IXGBE_STARTED) {
		/*
		 * Clean each rx ring that has its bit set in the map
		 */
		if (vect->rxr_cnt > 0) {
			r_idx = bt_getlowbit(vect->rx_map, 0,
			    (ixgbe->num_rx_rings - 1));
			while (r_idx >= 0) {
				ixgbe_intr_rx_work(&ixgbe->rx_rings[r_idx]);
				r_idx = bt_getlowbit(vect->rx_map, (r_idx + 1),
				    (ixgbe->num_rx_rings - 1));
			}
		}

		/*
		 * Clean each tx ring that has its bit set in the map
		 */
		if (vect->txr_cnt > 0) {
			r_idx = bt_getlowbit(vect->tx_map, 0,
			    (ixgbe->num_tx_rings - 1));
			while (r_idx >= 0) {
				ixgbe_intr_tx_work(&ixgbe->tx_rings[r_idx]);
				r_idx = bt_getlowbit(vect->tx_map, (r_idx + 1),
				    (ixgbe->num_tx_rings - 1));
			}
		}
	}

	/*
	 * Clean other interrupt that has its bit set in the map
	 */
	if (vect->other_cnt > 0 && BT_TEST(vect->other_map, 0) == 1) {
		eicr = IXGBE_READ_REG(hw, IXGBE_EICR);

		if (ixgbe_check_acc_handle(ixgbe->osdep.reg_handle) !=
		    DDI_FM_OK) {
			ddi_fm_service_impact(ixgbe->dip,
			    DDI_SERVICE_DEGRADED);
			atomic_or_32(&ixgbe->ixgbe_state, IXGBE_ERROR);
			return (DDI_INTR_CLAIMED);
		}

		/*
		 * Check "other" cause bits: any interrupt type other than tx/rx
		 */
		if (eicr & ixgbe->capab->other_intr) {
			mutex_enter(&ixgbe->gen_lock);
			switch (hw->mac.type) {
			case ixgbe_mac_82598EB:
				ixgbe->eims &= ~(eicr & IXGBE_OTHER_INTR);
				ixgbe_intr_other_work(ixgbe, eicr);
				break;

			case ixgbe_mac_82599EB:
			case ixgbe_mac_X540:
				ixgbe->eims |= IXGBE_EICR_RTX_QUEUE;
				ixgbe_intr_other_work(ixgbe, eicr);
				break;

			default:
				break;
			}
			mutex_exit(&ixgbe->gen_lock);
		}

		/* re-enable the interrupts which were automasked */
		IXGBE_WRITE_REG(hw, IXGBE_EIMS, ixgbe->eims);
	}

	return (DDI_INTR_CLAIMED);
}

/*
 * ixgbe_alloc_intrs - Allocate interrupts for the driver.
 *
 * Normal sequence is to try MSI-X; if not sucessful, try MSI;
 * if not successful, try Legacy.
 * ixgbe->intr_force can be used to force sequence to start with
 * any of the 3 types.
 * If MSI-X is not used, number of tx/rx rings is forced to 1.
 */
static int
ixgbe_alloc_intrs(ixgbe_t *ixgbe)
{
	dev_info_t *devinfo;
	int intr_types;
	int rc;

	devinfo = ixgbe->dip;

	/*
	 * Get supported interrupt types
	 */
	rc = ddi_intr_get_supported_types(devinfo, &intr_types);

	if (rc != DDI_SUCCESS) {
		ixgbe_log(ixgbe,
		    "Get supported interrupt types failed: %d", rc);
		return (IXGBE_FAILURE);
	}
	IXGBE_DEBUGLOG_1(ixgbe, "Supported interrupt types: %x", intr_types);

	ixgbe->intr_type = 0;

	/*
	 * Install MSI-X interrupts
	 */
	if ((intr_types & DDI_INTR_TYPE_MSIX) &&
	    (ixgbe->intr_force <= IXGBE_INTR_MSIX)) {
		ixgbe->intr_type = DDI_INTR_TYPE_MSIX;
		rc = ixgbe_alloc_intr_handles(ixgbe, DDI_INTR_TYPE_MSIX);
		if (rc == IXGBE_SUCCESS)
			return (IXGBE_SUCCESS);

		if (ixgbe->sriov_pf) {
			ixgbe_log(ixgbe,
			    "Allocate MSI-X failed, can't work on SRIOV mode");
			return (IXGBE_FAILURE);
		}

		ixgbe_log(ixgbe,
		    "Allocate MSI-X failed, trying MSI interrupts...");
	}

	/*
	 * MSI-X not used, force rings and groups to 1
	 */
	ixgbe->num_rx_rings = 1;
	ixgbe->num_pf_rx_groups = 1;
	ixgbe->num_tx_rings = 1;
	ixgbe->classify_mode = IXGBE_CLASSIFY_NONE;
	ixgbe_log(ixgbe,
	    "MSI-X not used, force rings and groups number to 1");

	/*
	 * Install MSI interrupts
	 */
	if ((intr_types & DDI_INTR_TYPE_MSI) &&
	    (ixgbe->intr_force <= IXGBE_INTR_MSI)) {
		ixgbe->intr_type = DDI_INTR_TYPE_MSI;
		rc = ixgbe_alloc_intr_handles(ixgbe, DDI_INTR_TYPE_MSI);
		if (rc == IXGBE_SUCCESS)
			return (IXGBE_SUCCESS);

		ixgbe_log(ixgbe,
		    "Allocate MSI failed, trying Legacy interrupts...");
	}

	/*
	 * Install legacy interrupts
	 */
	if (intr_types & DDI_INTR_TYPE_FIXED) {
		ixgbe->intr_type = DDI_INTR_TYPE_FIXED;
		rc = ixgbe_alloc_intr_handles(ixgbe, DDI_INTR_TYPE_FIXED);
		if (rc == IXGBE_SUCCESS)
			return (IXGBE_SUCCESS);

		ixgbe_log(ixgbe,
		    "Allocate Legacy interrupts failed");
	}

	/*
	 * If none of the 3 types succeeded, return failure
	 */
	return (IXGBE_FAILURE);
}

/*
 * ixgbe_alloc_intr_handles - Allocate interrupt handles.
 *
 * For legacy and MSI, only 1 handle is needed.  For MSI-X,
 * if fewer than 2 handles are available, return failure.
 * Upon success, this maps the vectors to rx and tx rings for
 * interrupts.
 */
static int
ixgbe_alloc_intr_handles(ixgbe_t *ixgbe, int intr_type)
{
	dev_info_t *devinfo;
	int request, count, actual;
	int minimum;
	int rc;

	devinfo = ixgbe->dip;

	switch (intr_type) {
	case DDI_INTR_TYPE_FIXED:
		request = 1;	/* Request 1 legacy interrupt handle */
		minimum = 1;
		IXGBE_DEBUGLOG_0(ixgbe, "interrupt type: legacy");
		break;

	case DDI_INTR_TYPE_MSI:
		request = 1;	/* Request 1 MSI interrupt handle */
		minimum = 1;
		IXGBE_DEBUGLOG_0(ixgbe, "interrupt type: MSI");
		break;

	case DDI_INTR_TYPE_MSIX:
		/*
		 * Best number of vectors for the adapter is
		 * (# rx rings + # tx rings), however we will
		 * limit the request number.
		 */
		request = min((ixgbe->max_rings_enable ? 32: 16),
		    ixgbe->num_rx_rings + ixgbe->num_tx_rings);
		if (request > ixgbe->capab->max_ring_vect)
			request = ixgbe->capab->max_ring_vect;
		minimum = 1;
		IXGBE_DEBUGLOG_0(ixgbe, "interrupt type: MSI-X");
		break;

	default:
		ixgbe_log(ixgbe,
		    "invalid call to ixgbe_alloc_intr_handles(): %d\n",
		    intr_type);
		return (IXGBE_FAILURE);
	}
	IXGBE_DEBUGLOG_2(ixgbe, "interrupt handles requested: %d  minimum: %d",
	    request, minimum);

	/*
	 * Get number of supported interrupts
	 */
	rc = ddi_intr_get_nintrs(devinfo, intr_type, &count);
	if ((rc != DDI_SUCCESS) || (count < minimum)) {
		ixgbe_log(ixgbe,
		    "Get interrupt number failed. Return: %d, count: %d",
		    rc, count);
		return (IXGBE_FAILURE);
	}
	IXGBE_DEBUGLOG_1(ixgbe, "interrupts supported: %d", count);

	actual = 0;
	ixgbe->intr_cnt = 0;
	ixgbe->intr_cnt_max = 0;
	ixgbe->intr_cnt_min = 0;

	/*
	 * Allocate an array of interrupt handles
	 */
	ixgbe->intr_size = request * sizeof (ddi_intr_handle_t);
	ixgbe->htable = kmem_alloc(ixgbe->intr_size, KM_SLEEP);

	rc = ddi_intr_alloc(devinfo, ixgbe->htable, intr_type, 0,
	    request, &actual, DDI_INTR_ALLOC_NORMAL);
	if (rc != DDI_SUCCESS) {
		ixgbe_log(ixgbe, "Allocate interrupts failed. "
		    "return: %d, request: %d, actual: %d",
		    rc, request, actual);
		goto alloc_handle_fail;
	}
	IXGBE_DEBUGLOG_1(ixgbe, "interrupts actually allocated: %d", actual);

	/*
	 * upper/lower limit of interrupts
	 */
	ixgbe->intr_cnt = actual;
	ixgbe->intr_cnt_max = request;
	ixgbe->intr_cnt_min = minimum;

	/*
	 * Now we know the actual number of vectors.  Here we map the vector
	 * to other, rx rings and tx ring.
	 */
	if (actual < minimum) {
		ixgbe_log(ixgbe, "Insufficient interrupt handles available: %d",
		    actual);
		goto alloc_handle_fail;
	}

	/*
	 * Get priority for first vector, assume remaining are all the same
	 */
	rc = ddi_intr_get_pri(ixgbe->htable[0], &ixgbe->intr_pri);
	if (rc != DDI_SUCCESS) {
		ixgbe_log(ixgbe,
		    "Get interrupt priority failed: %d", rc);
		goto alloc_handle_fail;
	}

	rc = ddi_intr_get_cap(ixgbe->htable[0], &ixgbe->intr_cap);
	if (rc != DDI_SUCCESS) {
		ixgbe_log(ixgbe,
		    "Get interrupt cap failed: %d", rc);
		goto alloc_handle_fail;
	}

	return (IXGBE_SUCCESS);

alloc_handle_fail:
	ixgbe_rem_intrs(ixgbe);

	return (IXGBE_FAILURE);
}

/*
 * ixgbe_add_intr_handlers - Add interrupt handlers based on the interrupt type.
 *
 * Before adding the interrupt handlers, the interrupt vectors have
 * been allocated, and the rx/tx rings have also been allocated.
 */
static int
ixgbe_add_intr_handlers(ixgbe_t *ixgbe)
{
	int vector = 0;
	int rc;

	switch (ixgbe->intr_type) {
	case DDI_INTR_TYPE_MSIX:
		/*
		 * Add interrupt handler for all vectors
		 */
		for (vector = 0; vector < ixgbe->intr_cnt; vector++) {
			/*
			 * install pointer to vect_map[vector]
			 */
			rc = ddi_intr_add_handler(ixgbe->htable[vector],
			    (ddi_intr_handler_t *)ixgbe_intr_msix,
			    (void *)&ixgbe->vect_map[vector], NULL);

			if (rc != DDI_SUCCESS) {
				ixgbe_log(ixgbe,
				    "Add interrupt handler failed. "
				    "return: %d, vector: %d", rc, vector);
				for (vector--; vector >= 0; vector--) {
					(void) ddi_intr_remove_handler(
					    ixgbe->htable[vector]);
				}
				return (IXGBE_FAILURE);
			}
		}

		break;

	case DDI_INTR_TYPE_MSI:
		/*
		 * Add interrupt handlers for the only vector
		 */
		rc = ddi_intr_add_handler(ixgbe->htable[vector],
		    (ddi_intr_handler_t *)ixgbe_intr_msi,
		    (void *)ixgbe, NULL);

		if (rc != DDI_SUCCESS) {
			ixgbe_log(ixgbe,
			    "Add MSI interrupt handler failed: %d", rc);
			return (IXGBE_FAILURE);
		}

		break;

	case DDI_INTR_TYPE_FIXED:
		/*
		 * Add interrupt handlers for the only vector
		 */
		rc = ddi_intr_add_handler(ixgbe->htable[vector],
		    (ddi_intr_handler_t *)ixgbe_intr_legacy,
		    (void *)ixgbe, NULL);

		if (rc != DDI_SUCCESS) {
			ixgbe_log(ixgbe,
			    "Add legacy interrupt handler failed: %d", rc);
			return (IXGBE_FAILURE);
		}

		break;

	default:
		return (IXGBE_FAILURE);
	}

	return (IXGBE_SUCCESS);
}

#pragma inline(ixgbe_map_rxring_to_vector)
/*
 * ixgbe_map_rxring_to_vector - Map given rx ring to given interrupt vector.
 */
static void
ixgbe_map_rxring_to_vector(ixgbe_t *ixgbe, int r_idx, int v_idx)
{
	/*
	 * Set bit in map
	 */
	BT_SET(ixgbe->vect_map[v_idx].rx_map, r_idx);

	/*
	 * Count bits set
	 */
	ixgbe->vect_map[v_idx].rxr_cnt++;

	/*
	 * Remember bit position
	 */
	ixgbe->rx_rings[r_idx].intr_vector = v_idx;
	ixgbe->rx_rings[r_idx].vect_bit = 1 << v_idx;
}

#pragma inline(ixgbe_map_txring_to_vector)
/*
 * ixgbe_map_txring_to_vector - Map given tx ring to given interrupt vector.
 */
static void
ixgbe_map_txring_to_vector(ixgbe_t *ixgbe, int t_idx, int v_idx)
{
	/*
	 * Set bit in map
	 */
	BT_SET(ixgbe->vect_map[v_idx].tx_map, t_idx);

	/*
	 * Count bits set
	 */
	ixgbe->vect_map[v_idx].txr_cnt++;

	/*
	 * Remember bit position
	 */
	ixgbe->tx_rings[t_idx].intr_vector = v_idx;
	ixgbe->tx_rings[t_idx].vect_bit = 1 << v_idx;
}

/*
 * ixgbe_setup_ivar - Set the given entry in the given interrupt vector
 * allocation register (IVAR).
 * cause:
 *   -1 : other cause
 *    0 : rx
 *    1 : tx
 */
static void
ixgbe_setup_ivar(ixgbe_t *ixgbe, uint16_t intr_alloc_entry, uint8_t msix_vector,
    int8_t cause)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	u32 ivar, index;

	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		msix_vector |= IXGBE_IVAR_ALLOC_VAL;
		if (cause == -1) {
			cause = 0;
		}
		index = (((cause * 64) + intr_alloc_entry) >> 2) & 0x1F;
		ivar = IXGBE_READ_REG(hw, IXGBE_IVAR(index));
		ivar &= ~(0xFF << (8 * (intr_alloc_entry & 0x3)));
		ivar |= (msix_vector << (8 * (intr_alloc_entry & 0x3)));
		IXGBE_WRITE_REG(hw, IXGBE_IVAR(index), ivar);
		break;

	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
		if (cause == -1) {
			/* other causes */
			msix_vector |= IXGBE_IVAR_ALLOC_VAL;
			index = (intr_alloc_entry & 1) * 8;
			ivar = IXGBE_READ_REG(hw, IXGBE_IVAR_MISC);
			ivar &= ~(0xFF << index);
			ivar |= (msix_vector << index);
			IXGBE_WRITE_REG(hw, IXGBE_IVAR_MISC, ivar);
		} else {
			/* tx or rx causes */
			msix_vector |= IXGBE_IVAR_ALLOC_VAL;
			index = ((16 * (intr_alloc_entry & 1)) + (8 * cause));
			ivar = IXGBE_READ_REG(hw,
			    IXGBE_IVAR(intr_alloc_entry >> 1));
			ivar &= ~(0xFF << index);
			ivar |= (msix_vector << index);
			IXGBE_WRITE_REG(hw, IXGBE_IVAR(intr_alloc_entry >> 1),
			    ivar);
		}
		break;

	default:
		break;
	}
}

/*
 * ixgbe_enable_ivar - Enable the given entry by setting the VAL bit of
 * given interrupt vector allocation register (IVAR).
 * cause:
 *   -1 : other cause
 *    0 : rx
 *    1 : tx
 */
static void
ixgbe_enable_ivar(ixgbe_t *ixgbe, uint16_t intr_alloc_entry, int8_t cause)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	u32 ivar, index;

	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		if (cause == -1) {
			cause = 0;
		}
		index = (((cause * 64) + intr_alloc_entry) >> 2) & 0x1F;
		ivar = IXGBE_READ_REG(hw, IXGBE_IVAR(index));
		ivar |= (IXGBE_IVAR_ALLOC_VAL << (8 *
		    (intr_alloc_entry & 0x3)));
		IXGBE_WRITE_REG(hw, IXGBE_IVAR(index), ivar);
		break;

	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
		if (cause == -1) {
			/* other causes */
			index = (intr_alloc_entry & 1) * 8;
			ivar = IXGBE_READ_REG(hw, IXGBE_IVAR_MISC);
			ivar |= (IXGBE_IVAR_ALLOC_VAL << index);
			IXGBE_WRITE_REG(hw, IXGBE_IVAR_MISC, ivar);
		} else {
			/* tx or rx causes */
			index = ((16 * (intr_alloc_entry & 1)) + (8 * cause));
			ivar = IXGBE_READ_REG(hw,
			    IXGBE_IVAR(intr_alloc_entry >> 1));
			ivar |= (IXGBE_IVAR_ALLOC_VAL << index);
			IXGBE_WRITE_REG(hw, IXGBE_IVAR(intr_alloc_entry >> 1),
			    ivar);
		}
		break;

	default:
		break;
	}
}

/*
 * ixgbe_disable_ivar - Disble the given entry by clearing the VAL bit of
 * given interrupt vector allocation register (IVAR).
 * cause:
 *   -1 : other cause
 *    0 : rx
 *    1 : tx
 */
static void
ixgbe_disable_ivar(ixgbe_t *ixgbe, uint16_t intr_alloc_entry, int8_t cause)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	u32 ivar, index;

	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		if (cause == -1) {
			cause = 0;
		}
		index = (((cause * 64) + intr_alloc_entry) >> 2) & 0x1F;
		ivar = IXGBE_READ_REG(hw, IXGBE_IVAR(index));
		ivar &= ~(IXGBE_IVAR_ALLOC_VAL<< (8 *
		    (intr_alloc_entry & 0x3)));
		IXGBE_WRITE_REG(hw, IXGBE_IVAR(index), ivar);
		break;

	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
		if (cause == -1) {
			/* other causes */
			index = (intr_alloc_entry & 1) * 8;
			ivar = IXGBE_READ_REG(hw, IXGBE_IVAR_MISC);
			ivar &= ~(IXGBE_IVAR_ALLOC_VAL << index);
			IXGBE_WRITE_REG(hw, IXGBE_IVAR_MISC, ivar);
		} else {
			/* tx or rx causes */
			index = ((16 * (intr_alloc_entry & 1)) + (8 * cause));
			ivar = IXGBE_READ_REG(hw,
			    IXGBE_IVAR(intr_alloc_entry >> 1));
			ivar &= ~(IXGBE_IVAR_ALLOC_VAL << index);
			IXGBE_WRITE_REG(hw, IXGBE_IVAR(intr_alloc_entry >> 1),
			    ivar);
		}
		break;

	default:
		break;
	}
}

/*
 * Convert the rx ring index driver maintained to the rx ring index
 * in h/w.
 */
static void
ixgbe_get_hw_rx_index(ixgbe_t *ixgbe, ixgbe_rx_ring_t *rx_ring,
    uint32_t sw_rx_index)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	uint32_t rx_ring_per_group, rx_ring_per_tc;

	if (ixgbe->classify_mode == IXGBE_CLASSIFY_RSS ||
	    ixgbe->classify_mode == IXGBE_CLASSIFY_NONE ||
	    ixgbe->classify_mode == IXGBE_CLASSIFY_DCB) {
		rx_ring->hw_index = sw_rx_index;
		rx_ring->tc_index = sw_rx_index;
	} else if (ixgbe->classify_mode == IXGBE_CLASSIFY_VT) {
		rx_ring->tc_index = 0;

		switch (hw->mac.type) {
		case ixgbe_mac_82598EB:
			rx_ring->hw_index = sw_rx_index;
			break;
		case ixgbe_mac_82599EB:
		case ixgbe_mac_X540:
			rx_ring->hw_index = sw_rx_index * 2;
			break;
		default:
			break;
		}
	} else if (ixgbe->classify_mode == IXGBE_CLASSIFY_VT_RSS) {
		rx_ring->tc_index = 0;
		rx_ring_per_group =
		    ixgbe->num_rx_rings / ixgbe->num_pf_rx_groups;

		switch (hw->mac.type) {
		case ixgbe_mac_82598EB:
			rx_ring->hw_index = (sw_rx_index / rx_ring_per_group) *
			    16 + (sw_rx_index % rx_ring_per_group);
			break;

		case ixgbe_mac_82599EB:
		case ixgbe_mac_X540:
			if (ixgbe->num_pf_rx_groups > 32) {
				rx_ring->hw_index = (sw_rx_index /
				    rx_ring_per_group) * 2 +
				    (sw_rx_index % rx_ring_per_group);
			} else {
				rx_ring->hw_index = (sw_rx_index /
				    rx_ring_per_group) * 4 +
				    (sw_rx_index % rx_ring_per_group);
			}
			break;

		default:
			break;
		}
	} else if (ixgbe->classify_mode == IXGBE_CLASSIFY_RSS_DCB) {
		rx_ring_per_tc = ixgbe->num_rx_rings /
		    ixgbe->dcb_config.num_tcs.pg_tcs;
		ASSERT(rx_ring_per_tc > 0);

		/* Equally allocate rings among all TCs */
		rx_ring->hw_index = (sw_rx_index / rx_ring_per_tc) * 16 +
		    (sw_rx_index % rx_ring_per_tc);
		rx_ring->tc_index = sw_rx_index / rx_ring_per_tc;
	}
}

static void
ixgbe_get_hw_tx_index(ixgbe_t *ixgbe, ixgbe_tx_ring_t *tx_ring, uint32_t index)
{
	uint32_t ring_per_tc;
	int ring_8tc[8] = { 0, 32, 64, 80, 96, 104, 112, 120 };
	int ring_4tc[4] = { 0, 64, 96, 112};
	int num_tcs = ixgbe->dcb_config.num_tcs.pg_tcs;

	if (ixgbe->classify_mode == IXGBE_CLASSIFY_RSS_DCB) {
		ring_per_tc = ixgbe->num_tx_rings / num_tcs;
		tx_ring->tc_index = index / ring_per_tc;

		if (num_tcs == MAX_TRAFFIC_CLASS) {
			tx_ring->hw_index = index % ring_per_tc +
			    ring_8tc[index / ring_per_tc];
		} else {
			tx_ring->hw_index = index % ring_per_tc +
			    ring_4tc[index / ring_per_tc];
		}
	} else {
		if (num_tcs != 0)
			tx_ring->tc_index = index % num_tcs;
		else
			tx_ring->tc_index = 0;
		tx_ring->hw_index = index;
	}
}

/*
 * ixgbe_map_intrs_to_vectors - Map different interrupts to MSI-X vectors.
 *
 * For MSI-X, here will map rx interrupt, tx interrupt and other interrupt
 * to vector[0 - (intr_cnt -1)].
 */
static int
ixgbe_map_intrs_to_vectors(ixgbe_t *ixgbe)
{
	int i, vector = 0;

	/* initialize vector map */
	bzero(&ixgbe->vect_map, sizeof (ixgbe->vect_map));
	for (i = 0; i < ixgbe->intr_cnt; i++) {
		ixgbe->vect_map[i].ixgbe = ixgbe;
	}

	/*
	 * non-MSI-X case is very simple: rx rings[0] on RTxQ[0],
	 * tx rings[0] on RTxQ[1].
	 */
	if (ixgbe->intr_type != DDI_INTR_TYPE_MSIX) {
		ixgbe_map_rxring_to_vector(ixgbe, 0, 0);
		ixgbe_map_txring_to_vector(ixgbe, 0, 1);
		return (IXGBE_SUCCESS);
	}

	/*
	 * Interrupts/vectors mapping for MSI-X
	 */

	/*
	 * Map other interrupt to vector 0,
	 * Set bit in map and count the bits set.
	 */
	BT_SET(ixgbe->vect_map[vector].other_map, 0);
	ixgbe->vect_map[vector].other_cnt++;

	/*
	 * Map rx ring interrupts to vectors
	 */
	for (i = 0; i < ixgbe->num_rx_rings; i++) {
		ixgbe_map_rxring_to_vector(ixgbe, i, vector);
		vector = (vector +1) % ixgbe->intr_cnt;
	}

	/*
	 * Map tx ring interrupts to vectors
	 */
	for (i = 0; i < ixgbe->num_tx_rings; i++) {
		ixgbe_map_txring_to_vector(ixgbe, i, vector);
		vector = (vector +1) % ixgbe->intr_cnt;
	}

	return (IXGBE_SUCCESS);
}

/*
 * ixgbe_setup_adapter_vector - Setup the adapter interrupt vector(s).
 *
 * This relies on ring/vector mapping already set up in the
 * vect_map[] structures
 */
static void
ixgbe_setup_adapter_vector(ixgbe_t *ixgbe)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	ixgbe_intr_vector_t *vect;	/* vector bitmap */
	int r_idx;	/* ring index */
	int v_idx;	/* vector index */
	uint32_t hw_index;

	/*
	 * Clear any previous entries
	 */
	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		for (v_idx = 0; v_idx < 25; v_idx++)
			IXGBE_WRITE_REG(hw, IXGBE_IVAR(v_idx), 0);
		break;

	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
		if (ixgbe->sriov_pf) {
			IXGBE_WRITE_REG(hw, IXGBE_IVAR(ixgbe->pf_grp), 0);
		} else {
			for (v_idx = 0; v_idx < 64; v_idx++)
				IXGBE_WRITE_REG(hw, IXGBE_IVAR(v_idx), 0);
		}
		IXGBE_WRITE_REG(hw, IXGBE_IVAR_MISC, 0);
		break;

	default:
		break;
	}

	/*
	 * For non MSI-X interrupt, rx rings[0] will use RTxQ[0], and
	 * tx rings[0] will use RTxQ[1].
	 */
	if (ixgbe->intr_type != DDI_INTR_TYPE_MSIX) {
		ixgbe_setup_ivar(ixgbe, 0, 0, 0);
		ixgbe_setup_ivar(ixgbe, 0, 1, 1);
		return;
	}

	/*
	 * For MSI-X interrupt, "Other" is always on vector[0].
	 */
	ixgbe_setup_ivar(ixgbe, IXGBE_IVAR_OTHER_CAUSES_INDEX, 0, -1);

	/*
	 * For each interrupt vector, populate the IVAR table
	 */
	for (v_idx = 0; v_idx < ixgbe->intr_cnt; v_idx++) {
		vect = &ixgbe->vect_map[v_idx];

		/*
		 * For each rx ring bit set
		 */
		r_idx = bt_getlowbit(vect->rx_map, 0,
		    (ixgbe->num_rx_rings - 1));

		while (r_idx >= 0) {
			hw_index = ixgbe->rx_rings[r_idx].hw_index;
			ixgbe_setup_ivar(ixgbe, hw_index, v_idx, 0);
			r_idx = bt_getlowbit(vect->rx_map, (r_idx + 1),
			    (ixgbe->num_rx_rings - 1));
		}

		/*
		 * For each tx ring bit set
		 */
		r_idx = bt_getlowbit(vect->tx_map, 0,
		    (ixgbe->num_tx_rings - 1));

		while (r_idx >= 0) {
			hw_index = ixgbe->tx_rings[r_idx].hw_index;
			ixgbe_setup_ivar(ixgbe, hw_index, v_idx, 1);
			r_idx = bt_getlowbit(vect->tx_map, (r_idx + 1),
			    (ixgbe->num_tx_rings - 1));
		}
	}
}

/*
 * ixgbe_rem_intr_handlers - Remove the interrupt handlers.
 */
static void
ixgbe_rem_intr_handlers(ixgbe_t *ixgbe)
{
	int i;
	int rc;

	for (i = 0; i < ixgbe->intr_cnt; i++) {
		rc = ddi_intr_remove_handler(ixgbe->htable[i]);
		if (rc != DDI_SUCCESS) {
			IXGBE_DEBUGLOG_1(ixgbe,
			    "Remove intr handler failed: %d", rc);
		}
	}
}

/*
 * ixgbe_rem_intrs - Remove the allocated interrupts.
 */
static void
ixgbe_rem_intrs(ixgbe_t *ixgbe)
{
	int i;
	int rc;

	for (i = 0; i < ixgbe->intr_cnt; i++) {
		rc = ddi_intr_free(ixgbe->htable[i]);
		if (rc != DDI_SUCCESS) {
			IXGBE_DEBUGLOG_1(ixgbe,
			    "Free intr failed: %d", rc);
		}
	}

	kmem_free(ixgbe->htable, ixgbe->intr_size);
	ixgbe->htable = NULL;
}

/*
 * ixgbe_enable_intrs - Enable all the ddi interrupts.
 */
static int
ixgbe_enable_intrs(ixgbe_t *ixgbe)
{
	int i;
	int rc;

	/*
	 * Enable interrupts
	 */
	if (ixgbe->intr_cap & DDI_INTR_FLAG_BLOCK) {
		/*
		 * Call ddi_intr_block_enable() for MSI
		 */
		rc = ddi_intr_block_enable(ixgbe->htable, ixgbe->intr_cnt);
		if (rc != DDI_SUCCESS) {
			ixgbe_log(ixgbe,
			    "Enable block intr failed: %d", rc);
			return (IXGBE_FAILURE);
		}
	} else {
		/*
		 * Call ddi_intr_enable() for Legacy/MSI non block enable
		 */
		for (i = 0; i < ixgbe->intr_cnt; i++) {
			rc = ddi_intr_enable(ixgbe->htable[i]);
			if (rc != DDI_SUCCESS) {
				ixgbe_log(ixgbe,
				    "Enable intr failed: %d", rc);
				return (IXGBE_FAILURE);
			}
		}
	}

	return (IXGBE_SUCCESS);
}

/*
 * ixgbe_disable_intrs - Disable all the interrupts.
 */
static int
ixgbe_disable_intrs(ixgbe_t *ixgbe)
{
	int i;
	int rc;

	/*
	 * Disable all interrupts
	 */
	if (ixgbe->intr_cap & DDI_INTR_FLAG_BLOCK) {
		rc = ddi_intr_block_disable(ixgbe->htable, ixgbe->intr_cnt);
		if (rc != DDI_SUCCESS) {
			ixgbe_log(ixgbe,
			    "Disable block intr failed: %d", rc);
			return (IXGBE_FAILURE);
		}
	} else {
		for (i = 0; i < ixgbe->intr_cnt; i++) {
			rc = ddi_intr_disable(ixgbe->htable[i]);
			if (rc != DDI_SUCCESS) {
				ixgbe_log(ixgbe,
				    "Disable intr failed: %d", rc);
				return (IXGBE_FAILURE);
			}
		}
	}

	return (IXGBE_SUCCESS);
}

/*
 * ixgbe_get_hw_state - Get and save parameters related to adapter hardware.
 */
static void
ixgbe_get_hw_state(ixgbe_t *ixgbe)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	ixgbe_link_speed speed = IXGBE_LINK_SPEED_UNKNOWN;
	boolean_t link_up = B_FALSE;
	uint32_t pcs1g_anlp = 0;
	uint32_t pcs1g_ana = 0;
	boolean_t autoneg = B_FALSE;

	ASSERT(mutex_owned(&ixgbe->gen_lock));
	ixgbe->param_lp_1000fdx_cap = 0;
	ixgbe->param_lp_100fdx_cap  = 0;

	/* check for link, don't wait */
	(void) ixgbe_check_link(hw, &speed, &link_up, false);
	pcs1g_ana = IXGBE_READ_REG(hw, IXGBE_PCS1GANA);

	if (link_up) {
		pcs1g_anlp = IXGBE_READ_REG(hw, IXGBE_PCS1GANLP);

		ixgbe->param_lp_1000fdx_cap =
		    (pcs1g_anlp & IXGBE_PCS1GANLP_LPFD) ? 1 : 0;
		ixgbe->param_lp_100fdx_cap =
		    (pcs1g_anlp & IXGBE_PCS1GANLP_LPFD) ? 1 : 0;
	}

	(void) ixgbe_get_link_capabilities(hw, &speed, &autoneg);

	ixgbe->param_adv_1000fdx_cap = ((pcs1g_ana & IXGBE_PCS1GANA_FDC) &&
	    (speed & IXGBE_LINK_SPEED_1GB_FULL)) ? 1 : 0;
	ixgbe->param_adv_100fdx_cap = ((pcs1g_ana & IXGBE_PCS1GANA_FDC) &&
	    (speed & IXGBE_LINK_SPEED_100_FULL)) ? 1 : 0;
}

/*
 * ixgbe_get_driver_control - Notify that driver is in control of device.
 */
static void
ixgbe_get_driver_control(struct ixgbe_hw *hw)
{
	uint32_t ctrl_ext;

	/*
	 * Notify firmware that driver is in control of device
	 */
	ctrl_ext = IXGBE_READ_REG(hw, IXGBE_CTRL_EXT);
	ctrl_ext |= IXGBE_CTRL_EXT_DRV_LOAD;

	/* Set PF Reset Done bit so PF/VF Mail Ops can work */
	ctrl_ext |= IXGBE_CTRL_EXT_PFRSTD;

	IXGBE_WRITE_REG(hw, IXGBE_CTRL_EXT, ctrl_ext);
}

/*
 * ixgbe_release_driver_control - Notify that driver is no longer in control
 * of device.
 */
static void
ixgbe_release_driver_control(struct ixgbe_hw *hw)
{
	uint32_t ctrl_ext;

	/*
	 * Notify firmware that driver is no longer in control of device
	 */
	ctrl_ext = IXGBE_READ_REG(hw, IXGBE_CTRL_EXT);
	ctrl_ext &= ~IXGBE_CTRL_EXT_DRV_LOAD;
	IXGBE_WRITE_REG(hw, IXGBE_CTRL_EXT, ctrl_ext);
}

/*
 * ixgbe_atomic_reserve - Atomic decrease operation.
 */
int
ixgbe_atomic_reserve(uint32_t *count_p, uint32_t n)
{
	uint32_t oldval;
	uint32_t newval;

	/*
	 * ATOMICALLY
	 */
	do {
		oldval = *count_p;
		if (oldval < n)
			return (-1);
		newval = oldval - n;
	} while (atomic_cas_32(count_p, oldval, newval) != oldval);

	return (newval);
}

/*
 * ixgbe_mc_table_itr - Traverse the entries in the multicast table.
 */
static uint8_t *
ixgbe_mc_table_itr(struct ixgbe_hw *hw, uint8_t **upd_ptr, uint32_t *vmdq)
{
	uint8_t *addr = *upd_ptr;
	uint8_t *new_ptr;

	_NOTE(ARGUNUSED(hw));
	_NOTE(ARGUNUSED(vmdq));

	new_ptr = addr + IXGBE_ETH_LENGTH_OF_ADDRESS;
	*upd_ptr = new_ptr;
	return (addr);
}

/*
 * FMA support
 */
int
ixgbe_check_acc_handle(ddi_acc_handle_t handle)
{
	ddi_fm_error_t de;

	ddi_fm_acc_err_get(handle, &de, DDI_FME_VERSION);
	ddi_fm_acc_err_clear(handle, DDI_FME_VERSION);
	return (de.fme_status);
}

int
ixgbe_check_dma_handle(ddi_dma_handle_t handle)
{
	ddi_fm_error_t de;

	ddi_fm_dma_err_get(handle, &de, DDI_FME_VERSION);
	return (de.fme_status);
}

/*
 * ixgbe_fm_error_cb - The IO fault service error handling callback function.
 */
static int
ixgbe_fm_error_cb(dev_info_t *dip, ddi_fm_error_t *err, const void *impl_data)
{
	_NOTE(ARGUNUSED(impl_data));
	/*
	 * as the driver can always deal with an error in any dma or
	 * access handle, we can just return the fme_status value.
	 */
	pci_ereport_post(dip, err, NULL);
	return (err->fme_status);
}

static void
ixgbe_fm_init(ixgbe_t *ixgbe)
{
	ddi_iblock_cookie_t iblk;
	int fma_dma_flag;

	/*
	 * Only register with IO Fault Services if we have some capability
	 */
	if (ixgbe->fm_capabilities & DDI_FM_ACCCHK_CAPABLE) {
		ixgbe_regs_acc_attr.devacc_attr_access = DDI_FLAGERR_ACC;
	} else {
		ixgbe_regs_acc_attr.devacc_attr_access = DDI_DEFAULT_ACC;
	}

	if (ixgbe->fm_capabilities & DDI_FM_DMACHK_CAPABLE) {
		fma_dma_flag = 1;
	} else {
		fma_dma_flag = 0;
	}

	ixgbe_set_fma_flags(fma_dma_flag);

	if (ixgbe->fm_capabilities) {

		/*
		 * Register capabilities with IO Fault Services
		 */
		ddi_fm_init(ixgbe->dip, &ixgbe->fm_capabilities, &iblk);

		/*
		 * Initialize pci ereport capabilities if ereport capable
		 */
		if (DDI_FM_EREPORT_CAP(ixgbe->fm_capabilities) ||
		    DDI_FM_ERRCB_CAP(ixgbe->fm_capabilities))
			pci_ereport_setup(ixgbe->dip);

		/*
		 * Register error callback if error callback capable
		 */
		if (DDI_FM_ERRCB_CAP(ixgbe->fm_capabilities))
			ddi_fm_handler_register(ixgbe->dip,
			    ixgbe_fm_error_cb, (void*) ixgbe);
	}
}

static void
ixgbe_fm_fini(ixgbe_t *ixgbe)
{
	/*
	 * Only unregister FMA capabilities if they are registered
	 */
	if (ixgbe->fm_capabilities) {

		/*
		 * Release any resources allocated by pci_ereport_setup()
		 */
		if (DDI_FM_EREPORT_CAP(ixgbe->fm_capabilities) ||
		    DDI_FM_ERRCB_CAP(ixgbe->fm_capabilities))
			pci_ereport_teardown(ixgbe->dip);

		/*
		 * Un-register error callback if error callback capable
		 */
		if (DDI_FM_ERRCB_CAP(ixgbe->fm_capabilities))
			ddi_fm_handler_unregister(ixgbe->dip);

		/*
		 * Unregister from IO Fault Service
		 */
		ddi_fm_fini(ixgbe->dip);
	}
}

void
ixgbe_fm_ereport(ixgbe_t *ixgbe, char *detail)
{
	uint64_t ena;
	char buf[FM_MAX_CLASS];

	(void) snprintf(buf, FM_MAX_CLASS, "%s.%s", DDI_FM_DEVICE, detail);
	ena = fm_ena_generate(0, FM_ENA_FMT1);
	if (DDI_FM_EREPORT_CAP(ixgbe->fm_capabilities)) {
		ddi_fm_ereport_post(ixgbe->dip, buf, ena, DDI_NOSLEEP,
		    FM_VERSION, DATA_TYPE_UINT8, FM_EREPORT_VERS0, NULL);
	}
}

static int
ixgbe_rx_ring_start(mac_ring_driver_t rh, uint64_t mr_gen_num)
{
	ixgbe_rx_ring_t *rx_ring = (ixgbe_rx_ring_t *)rh;

	mutex_enter(&rx_ring->rx_lock);
	rx_ring->ring_gen_num = mr_gen_num;
	rx_ring->started = B_TRUE;
	mutex_exit(&rx_ring->rx_lock);
	return (0);
}

static void
ixgbe_rx_ring_stop(mac_ring_driver_t rh)
{
	ixgbe_rx_ring_t *rx_ring = (ixgbe_rx_ring_t *)rh;

	mutex_enter(&rx_ring->rx_lock);
	rx_ring->started = B_FALSE;
	mutex_exit(&rx_ring->rx_lock);
}

/*
 * Get the global ring index by a ring index within a group.
 */
static int
ixgbe_get_rx_ring_index(ixgbe_t *ixgbe, int gindex, int rindex)
{
	ixgbe_rx_ring_t *rx_ring;
	int i;

	for (i = 0; i < ixgbe->num_rx_rings; i++) {
		rx_ring = &ixgbe->rx_rings[i];
		if (rx_ring->group_index == gindex)
			rindex--;
		if (rindex < 0)
			return (i);
	}

	return (-1);
}

/*ARGSUSED*/
static int
ixgbe_tx_ring_start(mac_ring_driver_t rh, uint64_t mr_gen_num)
{
	ixgbe_tx_ring_t *tx_ring = (ixgbe_tx_ring_t *)rh;

	mutex_enter(&tx_ring->tx_lock);
	tx_ring->started = B_TRUE;
	mutex_exit(&tx_ring->tx_lock);
	return (0);
}

static void
ixgbe_tx_ring_stop(mac_ring_driver_t rh)
{
	ixgbe_tx_ring_t *tx_ring = (ixgbe_tx_ring_t *)rh;

	mutex_enter(&tx_ring->tx_lock);
	tx_ring->started = B_FALSE;
	mutex_exit(&tx_ring->tx_lock);
}

/*
 * Callback funtion for MAC layer to register all rings.
 */
/* ARGSUSED */
void
ixgbe_fill_ring(void *arg, mac_ring_type_t rtype, const int group_index,
    const int ring_index, mac_ring_info_t *infop, mac_ring_handle_t rh)
{
	ixgbe_t *ixgbe = (ixgbe_t *)arg;
	mac_intr_t *mintr = &infop->mri_intr;

	/* If it's not a PF group, just return */
	if (ixgbe->sriov_pf && (group_index < ixgbe->pf_grp)) {
		ixgbe_log(ixgbe, "group_index = %d, ring_index = %d",
		    group_index, ring_index);
		return;
	}

	switch (rtype) {
	case MAC_RING_TYPE_RX: {
		/*
		 * 'index' is the ring index within the group.
		 * Need to get the global ring index by searching in groups.
		 */
		int global_ring_index = ixgbe_get_rx_ring_index(
		    ixgbe, group_index, ring_index);

		ASSERT(global_ring_index >= 0);

		ixgbe_rx_ring_t *rx_ring = &ixgbe->rx_rings[global_ring_index];
		rx_ring->ring_handle = rh;

		infop->mri_driver = (mac_ring_driver_t)rx_ring;
		infop->mri_start = ixgbe_rx_ring_start;
		infop->mri_stop = ixgbe_rx_ring_stop;
		infop->mri_poll = ixgbe_ring_rx_poll;
		infop->mri_stat = ixgbe_rx_ring_stat;

		mintr->mi_enable = ixgbe_rx_ring_intr_enable;
		mintr->mi_disable = ixgbe_rx_ring_intr_disable;
		if (ixgbe->intr_type &
		    (DDI_INTR_TYPE_MSIX | DDI_INTR_TYPE_MSI)) {
			mintr->mi_ddi_handle =
			    ixgbe->htable[rx_ring->intr_vector];
		}

		break;
	}
	case MAC_RING_TYPE_TX: {
		ASSERT(ring_index < ixgbe->num_tx_rings);

		ixgbe_tx_ring_t *tx_ring = &ixgbe->tx_rings[ring_index];
		tx_ring->ring_handle = rh;

		infop->mri_driver = (mac_ring_driver_t)tx_ring;
		infop->mri_start = ixgbe_tx_ring_start;
		infop->mri_stop = ixgbe_tx_ring_stop;
		infop->mri_tx = ixgbe_ring_tx;
		infop->mri_stat = ixgbe_tx_ring_stat;
		if (ixgbe->intr_type &
		    (DDI_INTR_TYPE_MSIX | DDI_INTR_TYPE_MSI)) {
			mintr->mi_ddi_handle =
			    ixgbe->htable[tx_ring->intr_vector];
		}
		break;
	}
	default:
		break;
	}
}

/*
 * Callback funtion for MAC layer to register all groups.
 */
void
ixgbe_fill_group(void *arg, mac_ring_type_t rtype, const int index,
    mac_group_info_t *infop, mac_group_handle_t gh)
{
	ixgbe_t *ixgbe = (ixgbe_t *)arg;

	ASSERT(ixgbe != NULL);

	switch (rtype) {
	case MAC_RING_TYPE_RX: {
		ixgbe_group_t *rx_group;

		rx_group = &ixgbe->rx_groups[index];
		rx_group->group_handle = gh;

		infop->mgi_driver = (mac_group_driver_t)rx_group;
		infop->mgi_start = NULL;
		infop->mgi_stop = NULL;
		infop->mgi_addvlan = NULL;
		infop->mgi_remvlan = NULL;
		infop->mgi_getsriov_info = NULL;
		infop->mgi_setmtu = NULL;
		infop->mgi_addmac = ixgbe_add_mac;
		infop->mgi_remmac = ixgbe_rem_mac;

		if (index < ixgbe->pf_grp) {
			/* The group is a VF rx group */
			infop->mgi_count = 0;
			infop->mgi_setmtu = ixgbe_set_mtu;
			infop->mgi_addvlan = ixgbe_add_vlanfilter;
			infop->mgi_remvlan = ixgbe_rem_vlanfilter;
			infop->mgi_getsriov_info = ixgbe_get_sriov_info;
			infop->mgi_flags = MAC_GROUP_VLAN_TRANSPARENT_ALL;
		} else {
			/* For SRIOV PF group or non-SRIOV groups */
			infop->mgi_count = ixgbe->num_rx_rings /
			    ixgbe->num_pf_rx_groups;
			if (index == ixgbe->pf_grp) {
				infop->mgi_flags = MAC_GROUP_DEFAULT |
				    MAC_GROUP_VLAN_TRANSPARENT_ALL;
			}
		}
		break;
	}

	case MAC_RING_TYPE_TX: {
		ixgbe_group_t *tx_group;

		ASSERT((index >= 0) && (index < ixgbe->num_all_tx_groups));

		tx_group = &ixgbe->tx_groups[index];
		tx_group->group_handle = gh;

		infop->mgi_driver = (mac_group_driver_t)tx_group;
		infop->mgi_start = NULL;
		infop->mgi_stop = NULL;
		infop->mgi_addmac = NULL;
		infop->mgi_remmac = NULL;
		infop->mgi_addvlan = NULL;
		infop->mgi_remvlan = NULL;
		infop->mgi_setmtu = NULL;
		infop->mgi_getsriov_info = NULL;

		if (index < ixgbe->pf_grp) {
			/* The group is a VF tx group */
			infop->mgi_count = 0;
			infop->mgi_getsriov_info = ixgbe_get_sriov_info;
			infop->mgi_flags = MAC_GROUP_VLAN_TRANSPARENT_ALL;
		} else {
			/* The group is a PF tx group */
			infop->mgi_count = ixgbe->num_tx_rings /
			    ixgbe->num_pf_tx_groups;
			if (index == ixgbe->pf_grp) {
				infop->mgi_flags = MAC_GROUP_DEFAULT |
				    MAC_GROUP_VLAN_TRANSPARENT_ALL;
			}
		}
		break;
	}
	default:
		break;
	}
}

/*
 * Callback function for MAC layer to get ring's TC infomation.
 */
int
ixgbe_get_ring_tc(mac_ring_driver_t rh, mac_ring_type_t rtype)
{
	switch (rtype) {
	case MAC_RING_TYPE_RX: {
		ixgbe_rx_ring_t *rx_ring = (ixgbe_rx_ring_t *)rh;

		return (rx_ring->tc_index);
	}
	case MAC_RING_TYPE_TX: {
		ixgbe_tx_ring_t *tx_ring = (ixgbe_tx_ring_t *)rh;

		return (tx_ring->tc_index);
	}
	default:
		break;
	}

	return (0);
}

/*
 * Enable interrupt on the specificed rx ring.
 */
int
ixgbe_rx_ring_intr_enable(mac_ring_driver_t rh)
{
	ixgbe_rx_ring_t *rx_ring = (ixgbe_rx_ring_t *)rh;
	ixgbe_t *ixgbe = rx_ring->ixgbe;
	int r_idx = rx_ring->index;
	int hw_r_idx = rx_ring->hw_index;
	int v_idx = rx_ring->intr_vector;

	mutex_enter(&ixgbe->gen_lock);
	if (ixgbe->ixgbe_state & IXGBE_INTR_ADJUST) {
		mutex_exit(&ixgbe->gen_lock);
		/*
		 * Simply return 0.
		 * Interrupts are being adjusted. ixgbe_intr_adjust()
		 * will eventually re-enable the interrupt when it's
		 * done with the adjustment.
		 */
		return (0);
	}

	/*
	 * To enable interrupt by setting the VAL bit of given interrupt
	 * vector allocation register (IVAR).
	 */
	ixgbe_enable_ivar(ixgbe, hw_r_idx, 0);

	BT_SET(ixgbe->vect_map[v_idx].rx_map, r_idx);

	/*
	 * Trigger a Rx interrupt on this ring
	 */
	IXGBE_WRITE_REG(&ixgbe->hw, IXGBE_EICS, (1 << v_idx));
	IXGBE_WRITE_FLUSH(&ixgbe->hw);

	mutex_exit(&ixgbe->gen_lock);

	return (0);
}

/*
 * Disable interrupt on the specificed rx ring.
 */
int
ixgbe_rx_ring_intr_disable(mac_ring_driver_t rh)
{
	ixgbe_rx_ring_t *rx_ring = (ixgbe_rx_ring_t *)rh;
	ixgbe_t *ixgbe = rx_ring->ixgbe;
	int r_idx = rx_ring->index;
	int hw_r_idx = rx_ring->hw_index;
	int v_idx = rx_ring->intr_vector;

	mutex_enter(&ixgbe->gen_lock);
	if (ixgbe->ixgbe_state & IXGBE_INTR_ADJUST) {
		mutex_exit(&ixgbe->gen_lock);
		/*
		 * Simply return 0.
		 * In the rare case where an interrupt is being
		 * disabled while interrupts are being adjusted,
		 * we don't fail the operation. No interrupts will
		 * be generated while they are adjusted, and
		 * ixgbe_intr_adjust() will cause the interrupts
		 * to be re-enabled once it completes. Note that
		 * in this case, packets may be delivered to the
		 * stack via interrupts before xgbe_rx_ring_intr_enable()
		 * is called again. This is acceptable since interrupt
		 * adjustment is infrequent, and the stack will be
		 * able to handle these packets.
		 */
		return (0);
	}

	/*
	 * To disable interrupt by clearing the VAL bit of given interrupt
	 * vector allocation register (IVAR).
	 */
	ixgbe_disable_ivar(ixgbe, hw_r_idx, 0);

	BT_CLEAR(ixgbe->vect_map[v_idx].rx_map, r_idx);

	mutex_exit(&ixgbe->gen_lock);

	return (0);
}

/*
 * ixgbe_unicst_add - Add given unicast MAC address to given group
 * Returns:
 * 	IXGBE_SUCCESS: success; address has been added, or it was already there
 * 	ENOSPC: no space to add a new entry
 * 	EIO: fma failure
 */
int
ixgbe_unicst_add(ixgbe_t *ixgbe, const uint8_t *mac_addr, uint32_t group_index)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	uint8_t addr[ETHERADDRL];
	int slot, i;
	boolean_t enable, share;

	ASSERT(mutex_owned(&ixgbe->gen_lock));

	/*
	 * Search for (address, group) in list; if it's already there, just
	 * return success
	 */
	slot = ixgbe_unicst_find(ixgbe, mac_addr, group_index);
	if (slot >= 0) {
		IXGBE_DEBUGLOG_0(ixgbe,
		    "ixgbe_unicst_add: address existed, return success");
		return (IXGBE_SUCCESS);
	}

	/* check for space available */
	if (ixgbe->unicst_avail == 0) {
		return (ENOSPC);
	}

	/*
	 * In Non-SRIOV mode, each group has one reserved slot. The slots
	 * from 0 to (group number - 1) are reserved slots which are
	 * 1 to 1 mapped with group index directly. The other slots are
	 * shared between all groups.
	 *
	 * In SRIOV mode, if the PF/VF is configured with a "unicast_slots"
	 * number, it can only use the reserved number of slots. Otherwise,
	 * it has one reserved slot, and can share the available slots with
	 * other PF/VFs.
	 */
	if (ixgbe->sriov_pf)
		share = (ixgbe->vf[group_index].unicast_slots == 0);
	else
		share = B_TRUE;

	/* Check and use the reserved slots first */
	for (i = 0; i < ixgbe->unicst_total; i++) {
		if (!(ixgbe->unicst_addr[i].flags & IXGBE_ADDRESS_SHARED) &&
		    (ixgbe->unicst_addr[i].group_index == group_index) &&
		    !(ixgbe->unicst_addr[i].flags & IXGBE_ADDRESS_SET)) {
			slot = i;
			break;
		}
	}

	/*
	 * If the reserved slots for current group are used, and
	 * sharing is allowed, find a free slot in the shared slots.
	 */
	if ((slot < 0) && share) {
		for (i = 0; i < ixgbe->unicst_total; i++) {
			if ((ixgbe->unicst_addr[i].flags &
			    IXGBE_ADDRESS_SHARED) &&
			    !(ixgbe->unicst_addr[i].flags &
			    IXGBE_ADDRESS_SET)) {
				slot = i;
				break;
			}
		}
	}

	if (slot < 0)
		return (ENOSPC);

	/*
	 * For SRIOV VF, save the address to the unicst_addr[]
	 * list without enabling it.
	 *
	 * For non-SRIOV mode or for SRIOV PF, save the address
	 * and enable it in the hardware.
	 */
	enable = !(ixgbe->sriov_pf && (group_index < ixgbe->pf_grp));

	if (enable) {
		/*
		 * Set VMDQ according to current mode and write to adapter
		 * receive address registers
		 */
		bcopy(mac_addr, addr, ETHERADDRL);
		(void) ixgbe_set_rar(hw, slot, addr, group_index, IXGBE_RAH_AV);

		if (ixgbe_check_acc_handle(ixgbe->osdep.reg_handle)
		    != DDI_FM_OK) {
			ddi_fm_service_impact(ixgbe->dip, DDI_SERVICE_DEGRADED);
			return (EIO);
		}
	}

	/* Remember this address in software state */
	bcopy(mac_addr, ixgbe->unicst_addr[slot].addr, ETHERADDRL);
	ixgbe->unicst_addr[slot].group_index = (uint8_t)group_index;
	ixgbe->unicst_addr[slot].flags |= IXGBE_ADDRESS_SET;

	if (enable)
		ixgbe->unicst_addr[slot].flags |= IXGBE_ADDRESS_ENABLED;

	if (ixgbe->sriov_pf) {
		ixgbe->vf[group_index].num_mac_addrs++;
		ixgbe->vf[group_index].mac_addr_chg = 1;
	}

	ixgbe->unicst_avail--;

#ifdef IXGBE_DEBUG
	/* check RAR registers */
	uint32_t reg;
	ixgbe_log(ixgbe, "ixgbe_unicst_add -- ral/rah --\n");
	for (int i = 0; i < 16; i++) {
		if (reg = IXGBE_READ_REG(hw, IXGBE_RAL(i))) {
			ixgbe_log(ixgbe, "ral(%d): 0x%x  rah(%d): 0x%x\n",
			    i, reg, i, IXGBE_READ_REG(hw, IXGBE_RAH(i)));
		}
	}
#endif
	return (IXGBE_SUCCESS);
}

/*
 * ixgbe_unicst_remove - Remove given unicast MAC address
 */
int
ixgbe_unicst_remove(ixgbe_t *ixgbe, const uint8_t *mac_addr,
    uint32_t group_index)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	int slot;

	ASSERT(mutex_owned(&ixgbe->gen_lock));

	/*
	 * Search for (address, group) in list; if it's not there,
	 * just return success.
	 */
	slot = ixgbe_unicst_find(ixgbe, mac_addr, group_index);
	if (slot == -1) {
		IXGBE_DEBUGLOG_0(ixgbe,
		    "ixgbe_unicst_remove: address not found, return success");
		return (IXGBE_FAILURE);
	}

	if (ixgbe->sriov_pf && (group_index < ixgbe->pf_grp) &&
	    (ixgbe->unicst_addr[slot].flags & IXGBE_ADDRESS_ENABLED)) {
		ixgbe_error(ixgbe,
		    "Address enabled for VF%d, can not be removed",
		    group_index);
		return (EBUSY);
	}

	/* Clear the MAC address from adapter registers */
	(void) ixgbe_clear_rar(hw, slot);

	if (ixgbe_check_acc_handle(ixgbe->osdep.reg_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(ixgbe->dip, DDI_SERVICE_DEGRADED);
		return (EIO);
	}

	/* Clear the MAC address from software state */
	if (ixgbe->unicst_addr[slot].flags & IXGBE_ADDRESS_SHARED)
		ixgbe->unicst_addr[slot].flags = IXGBE_ADDRESS_SHARED;
	else
		ixgbe->unicst_addr[slot].flags = 0;

	if (ixgbe->sriov_pf) {
		ixgbe->vf[group_index].num_mac_addrs--;
		ixgbe->vf[group_index].mac_addr_chg = 1;
	}

	ixgbe->unicst_avail++;
#ifdef IXGBE_DEBUG
	/* check RAR registers */
	uint32_t reg;
	ixgbe_log(ixgbe, "ixgbe_unicst_remove -- ral/rah --\n");
	for (int i = 0; i < 16; i++) {
		if (reg = IXGBE_READ_REG(hw, IXGBE_RAL(i))) {
			ixgbe_log(ixgbe, "ral(%d): 0x%x  rah(%d): 0x%x\n",
			    i, reg, i, IXGBE_READ_REG(hw, IXGBE_RAH(i)));
		}
	}
#endif

	return (IXGBE_SUCCESS);
}

/*
 * Add a MAC address to the specified Rx group.
 */
static int
ixgbe_add_mac(void *arg, const uint8_t *mac_addr, uint64_t flags)
{
	_NOTE(ARGUNUSED(flags))

	ixgbe_group_t *rx_group = (ixgbe_group_t *)arg;
	ixgbe_t *ixgbe = rx_group->ixgbe;
	int ret;

	mutex_enter(&ixgbe->gen_lock);

	if (ixgbe->ixgbe_state & IXGBE_SUSPENDED) {
		ret = ECANCELED;
	} else {
		ret = ixgbe_unicst_add(ixgbe, mac_addr, rx_group->index);
		if (ret) {
			IXGBE_DEBUGLOG_1(ixgbe,
			    "Can not add mac address, ret = %d", ret);
		}
	}

	mutex_exit(&ixgbe->gen_lock);

	return (ret);
}

/*
 * Remove a MAC address from the specified Rx group.
 */
static int
ixgbe_rem_mac(void *arg, const uint8_t *mac_addr)
{
	ixgbe_group_t *rx_group = (ixgbe_group_t *)arg;
	ixgbe_t *ixgbe = rx_group->ixgbe;
	int ret;

	mutex_enter(&ixgbe->gen_lock);

	if (ixgbe->ixgbe_state & IXGBE_SUSPENDED) {
		ret = ECANCELED;
	} else {
		ret = ixgbe_unicst_remove(ixgbe, mac_addr,
		    rx_group->index);
	}

	mutex_exit(&ixgbe->gen_lock);

	return (ret);
}

static int
ixgbe_add_vlanfilter(void *arg, uint16_t vlan_id, uint32_t flags)
{
	ixgbe_group_t	*group = (ixgbe_group_t *)arg;
	ixgbe_t		*ixgbe = group->ixgbe;
	vf_data_t	*vf_data = &ixgbe->vf[group->index];
	int		ret = 0;

	IXGBE_DEBUGLOG_3(ixgbe, "ixgbe_add_vlanfilter(group%d) enter, "
	    "vid = %d, flags = %x",
	    group->index, vlan_id, flags);

	ASSERT(group->index < ixgbe->pf_grp);
	ASSERT(vlan_id > 0);

	mutex_enter(&ixgbe->gen_lock);

	if (flags == 0) {
		if (vf_data->vlan_stripping) {
			mutex_exit(&ixgbe->gen_lock);
			cmn_err(CE_WARN,
			    "Please delete pvid before setting vid");
			return (EIO);
		}
		ret = ixgbe_add_vf_vlan(ixgbe, vlan_id, group->index);
	} else {
		ASSERT(flags == MAC_GROUP_VLAN_TRANSPARENT_ENABLE);
		if (vf_data->num_vlans) {
			mutex_exit(&ixgbe->gen_lock);
			cmn_err(CE_WARN,
			    "Please delete vid before setting pvid");
			return (EIO);
		}
		ret = ixgbe_enable_vf_port_vlan(ixgbe, vlan_id, group->index);
	}

	mutex_exit(&ixgbe->gen_lock);

	return (ret);
}

static int
ixgbe_rem_vlanfilter(void *arg, uint16_t vlan_id)
{
	ixgbe_group_t	*group = (ixgbe_group_t *)arg;
	ixgbe_t		*ixgbe = group->ixgbe;
	int		ret = 0;

	IXGBE_DEBUGLOG_2(ixgbe,
	    "ixgbe_rem_vlanfilter(group%d) enter, vid = %d",
	    group->index, vlan_id);

	ASSERT(group->index < ixgbe->pf_grp);
	ASSERT(vlan_id > 0);

	mutex_enter(&ixgbe->gen_lock);

	if (ixgbe->vf[group->index].port_vlan_id == vlan_id)
		ret = ixgbe_disable_vf_port_vlan(ixgbe, vlan_id, group->index);
	else
		ret = ixgbe_rem_vf_vlan(ixgbe, vlan_id, group->index);

	mutex_exit(&ixgbe->gen_lock);

	return (ret);
}

static int
ixgbe_add_vf_vlan(ixgbe_t *ixgbe, uint16_t vlan_id, uint32_t vf_index)
{
	vf_data_t	*vf = &ixgbe->vf[vf_index];
	int		ret = 0;
	uint_t		i;

	IXGBE_DEBUGLOG_2(ixgbe,
	    "ixgbe_add_vf_vlan(group%d) enter, vid = %d",
	    vf_index, vlan_id);

	ASSERT(mutex_owned(&ixgbe->gen_lock));

	/* Search all configured VLAN IDs */
	for (i = 0; i < vf->num_vlans; i++) {
		if (vf->vlan_ids[i] == vlan_id) {
			IXGBE_DEBUGLOG_3(ixgbe,
			    "Add VLAN %d existed at slot %d for VF%d",
			    vlan_id, i, vf_index);
			/* VLAN ID existed, just return success */
			return (0);
		}
	}

	if (ixgbe->ixgbe_state & IXGBE_STARTED) {
		ixgbe_stop(ixgbe, B_FALSE);
	}

	if (vf->num_vlans >= IXGBE_VLVF_ENTRIES) {
		IXGBE_DEBUGLOG_2(ixgbe,
		    "No space to set VLAN %d for VF%d", vlan_id, vf_index);
		return (ENOSPC);
	}

	if ((ret = ixgbe_vlvf_set(ixgbe, vlan_id, B_TRUE, vf_index)) == 0) {
		vf->vlan_ids[vf->num_vlans] = vlan_id;
		vf->num_vlans++;

		if (ixgbe->ixgbe_state & IXGBE_STARTED) {
			if (ixgbe_start(ixgbe, B_FALSE) != IXGBE_SUCCESS)
				return (EIO);
		}
	}

	return (ret);
}

static int
ixgbe_rem_vf_vlan(ixgbe_t *ixgbe, uint16_t vlan_id, uint32_t vf_index)
{
	vf_data_t *vf = &ixgbe->vf[vf_index];
	int		ret = 0;
	uint_t		i;

	IXGBE_DEBUGLOG_2(ixgbe,
	    "ixgbe_rem_vf_vlan(group%d) enter, vid = %d",
	    vf_index, vlan_id);

	ASSERT(mutex_owned(&ixgbe->gen_lock));

	/* Search all configured VLAN IDs */
	for (i = 0; i < vf->num_vlans; i++) {
		if (vf->vlan_ids[i] == vlan_id) {
			/* VLAN ID found */
			break;
		}
	}

	if (i == vf->num_vlans) {
		IXGBE_DEBUGLOG_2(ixgbe,
		    "VLAN %d not found for VF%d", vlan_id, vf_index);
		/* Address not found, just return success */
		return (0);
	}

	if (ixgbe->ixgbe_state & IXGBE_STARTED) {
		ixgbe_stop(ixgbe, B_FALSE);
	}

	if ((ret = ixgbe_vlvf_set(ixgbe, vlan_id, B_FALSE, vf_index)) == 0) {
		for (i++; i < vf->num_vlans; i++) {
			vf->vlan_ids[i - 1] = vf->vlan_ids[i];
		}
		vf->num_vlans--;
		vf->vlan_ids[vf->num_vlans] =
		    vf->default_vlan_id;

		if (ixgbe->ixgbe_state & IXGBE_STARTED) {
			if (ixgbe_start(ixgbe, B_FALSE) != IXGBE_SUCCESS)
				return (EIO);
		}
	}

	return (ret);
}

int
ixgbe_enable_vf_port_vlan(ixgbe_t *ixgbe, uint16_t vlan_id, uint32_t vf_index)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	vf_data_t *vf_data = &ixgbe->vf[vf_index];
	uint32_t vmvir;
	int ret;

	IXGBE_DEBUGLOG_2(ixgbe, "ixgbe_enable_vf_port_vlan(vf%d) enter: "
	    "pvid = %d", vf_index, vlan_id);

	ASSERT(mutex_owned(&ixgbe->gen_lock));

	/* Enable VLAN stripping */
	if ((ret = ixgbe_vlan_stripping(ixgbe, vf_index, B_TRUE)) != 0) {
		ixgbe_log(ixgbe, "fail to enable vlan stripping for vf%d",
		    vf_index);
	}

	/* Enable VLAN insert - always insert Default VLAN */
	vmvir = vlan_id;
	vmvir |= IXGBE_VMVIR_VLANA_DEFAULT;
	IXGBE_WRITE_REG(hw, IXGBE_VMVIR(vf_index), vmvir);

	/* Enable VLAN filter */
	if ((ret = ixgbe_vlvf_set(ixgbe, vlan_id, B_TRUE, vf_index)) == 0)
		vf_data->port_vlan_id = vlan_id;

	IXGBE_WRITE_FLUSH(hw);

	return (ret);
}

static int
ixgbe_disable_vf_port_vlan(ixgbe_t *ixgbe, uint16_t vlan_id, uint32_t vf_index)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	vf_data_t *vf_data = &ixgbe->vf[vf_index];
	uint32_t vmvir;
	int ret;

	IXGBE_DEBUGLOG_2(ixgbe, "ixgbe_disable_vf_port_vlan(vf%d) enter: "
	    "pvid = %d", vf_index, vlan_id);

	ASSERT(mutex_owned(&ixgbe->gen_lock));

	/* Disable VLAN stripping */
	if ((ret = ixgbe_vlan_stripping(ixgbe, vf_index, B_FALSE)) != 0) {
		ixgbe_log(ixgbe, "fail to disable vlan stripping for vf%d",
		    vf_index);
	}

	/* Disable VLAN insert */
	vmvir = IXGBE_VMVIR_VLANA_NEVER;
	IXGBE_WRITE_REG(hw, IXGBE_VMVIR(vf_index), vmvir);

	/* Disable VLAN filter */
	if ((ret = ixgbe_vlvf_set(ixgbe, vlan_id, B_FALSE, vf_index)) == 0)
		vf_data->port_vlan_id = 0;

	IXGBE_WRITE_FLUSH(hw);

	return (ret);
}

static int
ixgbe_set_mtu(void *arg, uint32_t mtu)
{
	UNREFERENCED_PARAMETER(arg);
	UNREFERENCED_PARAMETER(mtu);

	/* 82599 doesn't support VF MTU settings */

	return (ENOTSUP);
}

static int
ixgbe_get_sriov_info(void *arg, mac_sriov_info_t *infop)
{
	ixgbe_group_t *group = (ixgbe_group_t *)arg;

	/*
	 * the group index is the identical to
	 * the vf index
	 */
	infop->msi_vf_index = group->index;
	return (0);
}

int
ixgbe_get_param_unicast_slots(pci_plist_t *plist, uint32_t *unicast_slots,
    char *reason)
{
	iov_param_desc_t *pdesc;
	uint32_t value = 0;
	int rval;

	*unicast_slots = 0;
	pdesc = &ixgbe_iov_param_unicast_slots;

	rval = pci_plist_lookup_uint32(plist, pdesc->pd_name, &value);
	if (rval != DDI_SUCCESS) {
		IXGBE_DEBUGLOG_1(NULL, "param %s not found", pdesc->pd_name);
		return (DDI_SUCCESS);
	}

	/* This only validates individual assignment. */
	if ((value < pdesc->pd_min64) || (value > pdesc->pd_max64)) {
		if (reason) {
			(void) snprintf(reason, MAX_REASON_LEN,
			    "Param %s has invalid value: %d",
			    pdesc->pd_name, value);
		}
		return (DDI_FAILURE);
	}

	*unicast_slots = value;
	return (DDI_SUCCESS);
}

static int
ixgbe_get_iov_params(ixgbe_t *ixgbe)
{
	pci_param_t param;
	pci_plist_t plist;
	char reason[MAX_REASON_LEN + 1];
	int rval, i;

	IXGBE_DEBUGLOG_0(ixgbe, "ixgbe_get_iov_params() enter");

	rval = pci_param_get(ixgbe->dip, &param);
	if (rval != DDI_SUCCESS) {
		IXGBE_DEBUGLOG_0(ixgbe, "No params available");
		return (DDI_FAILURE);
	}

	rval = pci_plist_get(param, &plist);
	if (rval != DDI_SUCCESS) {
		IXGBE_DEBUGLOG_0(ixgbe, "No params for PF");
		goto get_iov_params_end;
	}

	/* Get the PF parameters */
	rval = ixgbe_get_param_unicast_slots(plist,
	    &ixgbe->vf[ixgbe->pf_grp].unicast_slots, reason);
	if (rval != DDI_SUCCESS) {
		IXGBE_DEBUGLOG_1(ixgbe, "PF %s", reason);
		goto get_iov_params_end;
	}

	/* Get the VF parameters */
	for (i = 0; i < ixgbe->num_vfs; i++) {
		rval = pci_plist_getvf(param, i, &plist);
		if (rval != DDI_SUCCESS) {
			IXGBE_DEBUGLOG_1(ixgbe, "No params for VF%d", i);
			goto get_iov_params_end;
		}

		rval = ixgbe_get_param_unicast_slots(plist,
		    &ixgbe->vf[i].unicast_slots, reason);
		if (rval != DDI_SUCCESS) {
			IXGBE_DEBUGLOG_1(ixgbe, "VF %s", reason);
			goto get_iov_params_end;
		}
	}

get_iov_params_end:
	(void) pci_param_free(param);

	return (rval);
}

/*
 * Enable FCoE RX CRC.
 */
static void
ixgbe_fcoe_enable_rxcrc(ixgbe_t *ixgbe)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	uint32_t reg_val;

	reg_val = IXGBE_READ_REG(hw, IXGBE_ETQF(IXGBE_ETQF_FILTER_FCOE));
	reg_val |= (ETHERTYPE_FCOE | IXGBE_ETQF_FCOE | IXGBE_ETQF_FILTER_EN);
	IXGBE_WRITE_REG(hw, IXGBE_ETQF(IXGBE_ETQF_FILTER_FCOE), reg_val);

	reg_val = IXGBE_READ_REG(hw, IXGBE_FCRXCTRL);
	reg_val |= (IXGBE_FCRXCTRL_SAVBAD |
	    IXGBE_FCRXCTRL_ICRC |
	    IXGBE_FCRXCTRL_FCCRCBO |
	    (IXGBE_FCOE_VER << IXGBE_FCRXCTRL_FCOEVER_SHIFT));
	IXGBE_WRITE_REG(hw, IXGBE_FCRXCTRL, reg_val);
}

/*
 * Enable FCoE LRO/DDP.
 */
static void
ixgbe_fcoe_enable_lro(ixgbe_t *ixgbe)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	uint32_t reg_val;

	reg_val = IXGBE_READ_REG(hw, IXGBE_ETQF(IXGBE_ETQF_FILTER_FCOE));
	reg_val |= (ETHERTYPE_FCOE | IXGBE_ETQF_FCOE | IXGBE_ETQF_FILTER_EN);
	IXGBE_WRITE_REG(hw, IXGBE_ETQF(IXGBE_ETQF_FILTER_FCOE), reg_val);

	reg_val = IXGBE_READ_REG(hw, IXGBE_FCRXCTRL);
	reg_val |= (IXGBE_FCRXCTRL_FCCRCBO |
	    IXGBE_FCRXCTRL_LASTSEQH |
	    (IXGBE_FCOE_VER << IXGBE_FCRXCTRL_FCOEVER_SHIFT));
	IXGBE_WRITE_REG(hw, IXGBE_FCRXCTRL, reg_val);
}

/*
 * Enable FCoE redirection table for FCoE multiple RX rings.
 */
static void
ixgbe_fcoe_enable_rxmr(ixgbe_t *ixgbe)
{
	int i, fcoe_q, fcoe_i;
	struct ixgbe_hw *hw = &ixgbe->hw;
	uint32_t reg_val;

	reg_val = IXGBE_READ_REG(hw, IXGBE_ETQF(IXGBE_ETQF_FILTER_FCOE));
	reg_val |= (ETHERTYPE_FCOE | IXGBE_ETQF_FCOE | IXGBE_ETQF_FILTER_EN);
	IXGBE_WRITE_REG(hw, IXGBE_ETQF(IXGBE_ETQF_FILTER_FCOE), reg_val);

	for (i = 0; i < IXGBE_FCRETA_SIZE; i++) {
		fcoe_i = i % ixgbe->num_rx_rings;
		fcoe_q = ixgbe->rx_rings[fcoe_i].hw_index;
		IXGBE_WRITE_REG(hw, IXGBE_FCRETA(i), fcoe_q);
	}
	reg_val = IXGBE_READ_REG(hw, IXGBE_FCRECTL);
	reg_val |=  IXGBE_FCRECTL_ENA;
	IXGBE_WRITE_REG(hw, IXGBE_FCRECTL, reg_val);
}

/*
 * Setup FCoE LRO (large receive offload) / DDP (direct data placement) for
 * specified exchange id.
 *
 * Return:
 *     -1: Fail
 *      0: Success
 */
int
ixgbe_fcoe_setup_lro(void *arg, mac_fcoe_lro_params_t *mflp)
{
	ixgbe_t *ixgbe = (ixgbe_t *)arg;
	struct ixgbe_hw *hw = &ixgbe->hw;
	ixgbe_rx_fcoe_t *rx_fcoe;
	ixgbe_fcoe_ddp_t *ddp;
	ixgbe_fcoe_ddp_buf_t *ddp_buf;
	uint16_t xchg_id;
	uint32_t buf_size;
	uint32_t fcbuf, fcdmarw, fcfltrw;
	uint32_t addr_low, addr_high;
	uint32_t free_index;
	uint32_t buf_count;
	uint32_t last_buf_size;

	if ((ixgbe->ixgbe_state & IXGBE_SUSPENDED) ||
	    (ixgbe->ixgbe_state & IXGBE_ERROR) ||
	    (ixgbe->ixgbe_state & IXGBE_OVERTEMP) ||
	    !(ixgbe->ixgbe_state & IXGBE_STARTED))
		return (-1);

	rx_fcoe = ixgbe->rx_fcoe;

	xchg_id = mflp->xchg_id;
	if (xchg_id > IXGBE_DDP_MAX_XCHGID) {
		IXGBE_DEBUGLOG_1(ixgbe,
		    "setup lro fail: xchg_id %d out of range\n", xchg_id);
		return (-1);
	}
	buf_size = mflp->buf_size;
	if (mflp->flags & MAC_FCOE_XCHG_ON_TARGET) {
		rx_fcoe->on_target = B_TRUE;
	} else {
		rx_fcoe->on_target = B_FALSE;
	}

	if (ixgbe_atomic_reserve(&rx_fcoe->ddp_buf_free, 1) < 0) {
		IXGBE_DEBUGLOG_0(ixgbe,
		    "setup lro fail: no available ddp");
		return (-1);
	}

	/*
	 * 4K buffer length is used for ddp.
	 *
	 * To workaround the 82599 hw limitation, one extra buffer with
	 * size of 1 is added if last buffer size is equal to 4K.
	 */
	buf_count = (buf_size >> IXGBE_DDP_BUF_SHIFT) + 1;
	if (buf_size & IXGBE_DDP_BUF_MASK)
		last_buf_size = (buf_size & IXGBE_DDP_BUF_MASK);
	else
		last_buf_size = 1;

	fcbuf = (IXGBE_FCBUFF_VALID) |
	    (IXGBE_FCOE_BUF_4KB << IXGBE_FCBUFF_BUFFSIZE_SHIFT) |
	    (buf_count << IXGBE_FCBUFF_BUFFCNT_SHIFT) |
	    (0 << IXGBE_FCBUFF_OFFSET_SHIFT);

	if (rx_fcoe->on_target) {
		fcbuf |= IXGBE_FCBUFF_WRCONTX;
	}

	fcdmarw = xchg_id | IXGBE_FCDMARW_WE |
	    (last_buf_size << IXGBE_FCDMARW_LASTSIZE_SHIFT);

	fcfltrw = xchg_id | IXGBE_FCFLTRW_WE;

	mutex_enter(&ixgbe->rx_fcoe_lock);
	ddp = &rx_fcoe->ddp_ring[xchg_id];
	if (ddp->ddp_buf != NULL) {
		IXGBE_DEBUGLOG_1(ixgbe,
		    "fcoe ddp[%d] is not available now\n", xchg_id);
		mutex_exit(&ixgbe->rx_fcoe_lock);
		atomic_inc_32(&rx_fcoe->ddp_buf_free);
		return (-1);
	}

	ddp->xchg_id = xchg_id;
	ddp_buf = rx_fcoe->work_list[xchg_id];
	ddp->ddp_buf = ddp_buf;
	if (buf_size & IXGBE_DDP_BUF_MASK) {
		ddp->ddp_buf->used_ubd_count = buf_count;
		ddp->ddp_buf->last_ub_len = (buf_size & IXGBE_DDP_BUF_MASK);
	} else {
		ddp->ddp_buf->used_ubd_count = buf_count - 1;
		ddp->ddp_buf->last_ub_len = IXGBE_DDP_BUF_SIZE;
	}
	ddp->ddp_buf->used_buf_size = buf_size;

	/*
	 * Set the ddp buffer descriptor list ptr
	 */
	addr_low = (uint32_t)(ddp_buf->ubd_area.dma_address);
	addr_high = (uint32_t)((ddp_buf->ubd_area.dma_address) >> 32);
	IXGBE_WRITE_REG(hw, IXGBE_FCPTRL, addr_low);
	IXGBE_WRITE_REG(hw, IXGBE_FCPTRH, addr_high);

	/* Set the DMA Context */
	IXGBE_WRITE_REG(hw, IXGBE_FCBUFF, fcbuf);
	IXGBE_WRITE_REG(hw, IXGBE_FCDMARW, fcdmarw);

	/* Set the Filter Context */
	IXGBE_WRITE_REG(hw, IXGBE_FCPARAM, 0);
	IXGBE_WRITE_REG(hw, IXGBE_FCFLT, IXGBE_FCFLT_VALID);
	IXGBE_WRITE_REG(hw, IXGBE_FCFLTRW, fcfltrw);

	/*
	 * Strip off one free ddp buf from the free list
	 */
	free_index = rx_fcoe->ddp_buf_head;
	ddp_buf = rx_fcoe->free_list[free_index];
	ASSERT(ddp_buf != NULL);
	rx_fcoe->work_list[xchg_id] = ddp_buf;
	rx_fcoe->free_list[free_index] = NULL;
	rx_fcoe->ddp_buf_head = NEXT_INDEX(free_index, 1,
	    rx_fcoe->free_list_size);

	mutex_exit(&ixgbe->rx_fcoe_lock);

	return (0);
}

void
ixgbe_fcoe_cancel_lro(void *arg, mac_fcoe_lro_params_t *mflp)
{
	ixgbe_t *ixgbe = (ixgbe_t *)arg;
	struct ixgbe_hw *hw = &ixgbe->hw;
	ixgbe_rx_fcoe_t *rx_fcoe = ixgbe->rx_fcoe;
	ixgbe_fcoe_ddp_t *ddp;
	ixgbe_fcoe_ddp_buf_t *ddp_buf;
	uint32_t fcbuf;
	uint32_t free_index;
	uint16_t xchg_id;

	xchg_id = mflp->xchg_id;
	if (xchg_id > IXGBE_DDP_MAX_XCHGID) {
		IXGBE_DEBUGLOG_1(ixgbe,
		    "cancel lro fail: xchg_id %d out of range\n", xchg_id);
		return;
	}
	ddp = &rx_fcoe->ddp_ring[xchg_id];

	mutex_enter(&ixgbe->rx_fcoe_lock);
	if ((ddp->ddp_buf == NULL) ||
	    (ddp->xchg_id != xchg_id)) {
		mutex_exit(&ixgbe->rx_fcoe_lock);
		IXGBE_DEBUGLOG_0(ixgbe,
		    "cancel lro fail: not valid ddp or xchid\n");
		return;
	}

	ddp_buf = ddp->ddp_buf;
	ddp->ddp_buf = NULL;
	ddp->xchg_id = 0;

	/* clear the ddp */
	IXGBE_WRITE_REG(hw, IXGBE_FCFLT, 0);
	IXGBE_WRITE_REG(hw, IXGBE_FCFLTRW,
	    (xchg_id | IXGBE_FCFLTRW_WE));
	IXGBE_WRITE_REG(hw, IXGBE_FCBUFF, 0);
	IXGBE_WRITE_REG(hw, IXGBE_FCDMARW,
	    (xchg_id | IXGBE_FCDMARW_WE));

	/*
	 * Wait ~100us to get it complete
	 */
	IXGBE_WRITE_REG(hw, IXGBE_FCDMARW,
	    (xchg_id | IXGBE_FCDMARW_RE));
	fcbuf = IXGBE_READ_REG(hw, IXGBE_FCBUFF);
	if (fcbuf & IXGBE_FCBUFF_VALID)
		usec_delay(100);

	mutex_exit(&ixgbe->rx_fcoe_lock);

	/*
	 * Put the recycled ddp buffer into free list
	 */
	mutex_enter(&rx_fcoe->recycle_lock);
	free_index = rx_fcoe->ddp_buf_tail;
	ASSERT(rx_fcoe->free_list[free_index] == NULL);
	rx_fcoe->free_list[free_index] = ddp_buf;
	rx_fcoe->ddp_buf_tail = NEXT_INDEX(free_index, 1,
	    rx_fcoe->free_list_size);
	mutex_exit(&rx_fcoe->recycle_lock);
	/*
	 * The atomic operation on the number of the available ddp
	 * in the free list is used to make the recycling mutual
	 * exclusive with the receiving.
	 */
	atomic_inc_32(&rx_fcoe->ddp_buf_free);
	ASSERT(rx_fcoe->ddp_buf_free <= rx_fcoe->free_list_size);
}

#ifdef DEBUG_INTEL
extern int gld_getinfo(dev_info_t *, ddi_info_cmd_t, void *, void **);

/*
 * export device registers to an mmap() call; used for ethregs support
 */
static int
ixgbe_devmap(dev_t dev, devmap_cookie_t dhp, offset_t off, size_t len,
    size_t *maplen, uint_t model)
{
	dev_info_t *dip = GlobalDip;
	ixgbe_t *ixgbe = ddi_get_driver_private(dip);
	int status, regnum = IXGBE_ADAPTER_REGSET;
	size_t lenpage;
	off_t regsize;

	UNREFERENCED_PARAMETER(dev);
	UNREFERENCED_PARAMETER(model);

	/* get size of register set */
	if (ddi_dev_regsize(dip, regnum, &regsize) == DDI_SUCCESS) {
		cmn_err(CE_WARN, "regsize: 0x%x\n", (uint32_t)regsize);
	} else {
		cmn_err(CE_WARN, "ddi_dev_regsize fail");
		return (DDI_FAILURE);
	}

	/* round up len to multiple of page size */
	cmn_err(CE_WARN, "len before: 0x%x", (uint32_t)len);
	lenpage = ptob(btopr(len));
	cmn_err(CE_WARN, "len _after: 0x%x", (uint32_t)lenpage);

	cmn_err(CE_WARN,
	    "off: 0x%x  lenpage: 0x%x  regsize: 0x%x\n",
	    (uint32_t)off, (uint32_t)lenpage, (uint32_t)regsize);
	if ((uint32_t)(off + lenpage) > (uint32_t)regsize) {
		ixgbe_log(ixgbe, "regsize test fail\n");
		return (DDI_FAILURE);
	}

	/* set up the device mapping */
	status = devmap_devmem_setup(dhp, dip, NULL, regnum, off, lenpage,
	    PROT_ALL, DEVMAP_DEFAULTS, &ixgbe_regs_acc_attr);
	if (status == DDI_SUCCESS) {
		/* acknowledge the entire range */
		*maplen = lenpage;
	} else {
		cmn_err(CE_WARN, "devmap_devmem_setup fail: 0x%x\n", status);
	}

	return (status);
}
#endif	/* DEBUG_INTEL */
