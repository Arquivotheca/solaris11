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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include "ixgbevf_sw.h"

static char ixgbevf_ident[] = "Intel 10Gb Ethernet";
static char ixgbevf_version[] = "ixgbevf 1.0.2";

/*
 * Local function protoypes
 */
static int ixgbevf_register_mac(ixgbevf_t *);
static int ixgbevf_identify_hardware(ixgbevf_t *);
static int ixgbevf_regs_map(ixgbevf_t *);
static void ixgbevf_init_properties(ixgbevf_t *);
static int ixgbevf_init_driver_settings(ixgbevf_t *);
static void ixgbevf_init_locks(ixgbevf_t *);
static void ixgbevf_destroy_locks(ixgbevf_t *);
static int ixgbevf_init(ixgbevf_t *);
static int ixgbevf_chip_start(ixgbevf_t *);
static void ixgbevf_chip_stop(ixgbevf_t *);
static int ixgbevf_reset(ixgbevf_t *);
static void ixgbevf_tx_clean(ixgbevf_t *);
static boolean_t ixgbevf_tx_drain(ixgbevf_t *);
static boolean_t ixgbevf_rx_drain(ixgbevf_t *);
static int ixgbevf_alloc_rings(ixgbevf_t *);
static void ixgbevf_free_rings(ixgbevf_t *);
static int ixgbevf_alloc_rx_data(ixgbevf_t *);
static void ixgbevf_free_rx_data(ixgbevf_t *);
static void ixgbevf_setup_rings(ixgbevf_t *);
static void ixgbevf_setup_rx(ixgbevf_t *);
static void ixgbevf_setup_tx(ixgbevf_t *);
static void ixgbevf_setup_rx_ring(ixgbevf_rx_ring_t *);
static void ixgbevf_setup_tx_ring(ixgbevf_tx_ring_t *);
static int ixgbevf_init_unicst(ixgbevf_t *);
static int ixgbevf_unicst_find(ixgbevf_t *, const uint8_t *);
static void ixgbevf_setup_multicst(ixgbevf_t *);
static void ixgbevf_get_hw_state(ixgbevf_t *);
static void ixgbevf_get_conf(ixgbevf_t *);
static void ixgbevf_init_params(ixgbevf_t *);
static int ixgbevf_get_prop(ixgbevf_t *, char *, int, int, int);
static void ixgbevf_driver_link_check(ixgbevf_t *);
static void ixgbevf_link_timer(void *);
static void ixgbevf_local_timer(void *);
static void ixgbevf_arm_watchdog_timer(ixgbevf_t *);
static void ixgbevf_restart_watchdog_timer(ixgbevf_t *);
static void ixgbevf_disable_adapter_interrupts(ixgbevf_t *);
static void ixgbevf_enable_adapter_interrupts(ixgbevf_t *);
static void ixgbevf_enable_mailbox_interrupt(ixgbevf_t *);
static boolean_t is_valid_mac_addr(uint8_t *);
static boolean_t ixgbevf_stall_check(ixgbevf_t *);
static int ixgbevf_alloc_intrs(ixgbevf_t *);
static int ixgbevf_alloc_intr_handles(ixgbevf_t *, int);
static int ixgbevf_add_intr_handlers(ixgbevf_t *);
static void ixgbevf_map_rxring_to_vector(ixgbevf_t *, int, int);
static void ixgbevf_map_txring_to_vector(ixgbevf_t *, int, int);
static void ixgbevf_setup_ivar(ixgbevf_t *, uint16_t, uint8_t, int8_t);
static void ixgbevf_enable_ivar(ixgbevf_t *, uint16_t, int8_t);
static void ixgbevf_disable_ivar(ixgbevf_t *, uint16_t, int8_t);
static int ixgbevf_map_intrs_to_vectors(ixgbevf_t *);
static void ixgbevf_setup_adapter_vector(ixgbevf_t *);
static void ixgbevf_rem_intr_handlers(ixgbevf_t *);
static void ixgbevf_rem_intrs(ixgbevf_t *);
static int ixgbevf_enable_intrs(ixgbevf_t *);
static int ixgbevf_disable_intrs(ixgbevf_t *);
static uint_t ixgbevf_intr_msix(void *, void *);
static void ixgbevf_intr_rx_work(ixgbevf_rx_ring_t *);
static void ixgbevf_intr_tx_work(ixgbevf_tx_ring_t *);
static int ixgbevf_addmac(void *, const uint8_t *, uint64_t);
static int ixgbevf_remmac(void *, const uint8_t *);

static int ixgbevf_attach(dev_info_t *, ddi_attach_cmd_t);
static int ixgbevf_detach(dev_info_t *, ddi_detach_cmd_t);
static int ixgbevf_resume(dev_info_t *);
static int ixgbevf_suspend(dev_info_t *);
static int ixgbevf_unconfigure(dev_info_t *, ixgbevf_t *);
static uint8_t *ixgbevf_mc_table_itr(struct ixgbe_hw *, uint8_t **,
    uint32_t *);
static int ixgbevf_cbfunc(dev_info_t *, ddi_cb_action_t, void *, void *,
    void *);
static int ixgbevf_intr_cb_register(ixgbevf_t *);
static int ixgbevf_intr_adjust(ixgbevf_t *, ddi_cb_action_t, int);

static int ixgbevf_fm_error_cb(dev_info_t *dip, ddi_fm_error_t *err,
    const void *impl_data);
static void ixgbevf_fm_init(ixgbevf_t *);
static void ixgbevf_fm_fini(ixgbevf_t *);
#ifdef DEBUG_INTEL
static int ixgbevf_devmap(dev_t, devmap_cookie_t, offset_t, size_t, size_t *,
    uint_t);
#endif	/* DEBUG_INTEL */

char *ixgbevf_priv_props[] = {
	"_tx_copy_thresh",
	"_tx_recycle_thresh",
	"_tx_overload_thresh",
	"_tx_resched_thresh",
	"_rx_copy_thresh",
	"_rx_limit_per_intr",
	"_intr_throttling",
	"_adv_pause_cap",
	"_adv_asym_pause_cap",
	NULL
};

#define	IXGBE_MAX_PRIV_PROPS \
	(sizeof (ixgbevf_priv_props) / sizeof (mac_priv_prop_t))

static struct cb_ops ixgbevf_cb_ops = {
	nulldev,		/* cb_open */
	nulldev,		/* cb_close */
	nodev,			/* cb_strategy */
	nodev,			/* cb_print */
	nodev,			/* cb_dump */
	nodev,			/* cb_read */
	nodev,			/* cb_write */
	nodev,			/* cb_ioctl */
#ifdef DEBUG_INTEL
	ixgbevf_devmap,		/* cb_devmap */
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
#endif  /* DEBUG_INTEL */
	CB_REV,			/* cb_rev */
	nodev,			/* cb_aread */
	nodev			/* cb_awrite */
};

static struct dev_ops ixgbevf_dev_ops = {
	DEVO_REV,		/* devo_rev */
	0,			/* devo_refcnt */
	NULL,			/* devo_getinfo */
	nulldev,		/* devo_identify */
	nulldev,		/* devo_probe */
	ixgbevf_attach,		/* devo_attach */
	ixgbevf_detach,		/* devo_detach */
	nodev,			/* devo_reset */
	&ixgbevf_cb_ops,		/* devo_cb_ops */
	NULL,			/* devo_bus_ops */
	nulldev,		/* devo_power */
	ddi_quiesce_not_supported,	/* devo_quiesce */
};

static struct modldrv ixgbevf_modldrv = {
	&mod_driverops,		/* Type of module.  This one is a driver */
	ixgbevf_ident,		/* Discription string */
	&ixgbevf_dev_ops		/* driver ops */
};

static struct modlinkage ixgbevf_modlinkage = {
	MODREV_1, &ixgbevf_modldrv, NULL
};

/*
 * Access attributes for register mapping
 */
ddi_device_acc_attr_t ixgbevf_regs_acc_attr = {
	DDI_DEVICE_ATTR_V1,
	DDI_STRUCTURE_LE_ACC,
	DDI_STRICTORDER_ACC,
	DDI_FLAGERR_ACC
};

#define	IXGBE_M_CALLBACK_FLAGS \
	(MC_IOCTL | MC_GETCAPAB | MC_SETPROP | MC_GETPROP | MC_PROPINFO)

static mac_callbacks_t ixgbevf_m_callbacks = {
	IXGBE_M_CALLBACK_FLAGS,
	ixgbevf_m_stat,
	ixgbevf_m_start,
	ixgbevf_m_stop,
	ixgbevf_m_promisc,
	ixgbevf_m_multicst,
	NULL,
	NULL,
	NULL,
	ixgbevf_m_ioctl,
	ixgbevf_m_getcapab,
	NULL,
	NULL,
	ixgbevf_m_setprop,
	ixgbevf_m_getprop,
	ixgbevf_m_propinfo
};

/*
 * Initialize capabilities of each supported adapter type
 */
static adapter_info_t ixgbevf_82599vf_cap = {
	8,		/* maximum number of rx queues */
	1,		/* minimum number of rx queues */
	1,		/* default number of rx queues */

	8,		/* maximum number of tx queues */
	1,		/* minimum number of tx queues */
	1,		/* default number of tx queues */

	1,		/* maximum number of groups */
	1,		/* minimum number of groups */
	1,		/* default number of groups */

	ETHERMTU,	/* maximum MTU size */

	0xFF8,		/* maximum interrupt throttle rate */
	0,		/* minimum interrupt throttle rate */
	200,		/* default interrupt throttle rate */

	3,		/* maximum total msix vectors */
	2,		/* maximum number of ring vectors */
	1,		/* maximum number of other vectors */

	(MAX_NUM_UNICAST_ADDRESSES - 1),
			/* maximum number of unicast addresses */
};

#ifdef DEBUG_INTEL
/*
 * hack for ethregs: save all dev_info_t's in a global array.  This would not
 * be necessary if I could get gld_getinfo() to work, but I can't.
 */
#define	DIP_MAX	32
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

	mac_init_ops(&ixgbevf_dev_ops, MODULE_NAME);

	status = mod_install(&ixgbevf_modlinkage);

	if (status != DDI_SUCCESS) {
		mac_fini_ops(&ixgbevf_dev_ops);
	}

	return (status);
}

int
_fini(void)
{
	int status;

	status = mod_remove(&ixgbevf_modlinkage);

	if (status == DDI_SUCCESS) {
		mac_fini_ops(&ixgbevf_dev_ops);
	}

	return (status);
}

int
_info(struct modinfo *modinfop)
{
	int status;

	status = mod_info(&ixgbevf_modlinkage, modinfop);

	return (status);
}

/*
 * ixgbevf_attach - Driver attach.
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
ixgbevf_attach(dev_info_t *devinfo, ddi_attach_cmd_t cmd)
{
	ixgbevf_t *ixgbevf;
	struct ixgbevf_osdep *osdep;
	struct ixgbe_hw *hw;
	int instance;
	char taskqname[32];

	/*
	 * Check the command and perform corresponding operations
	 */
	switch (cmd) {
	default:
		return (DDI_FAILURE);

	case DDI_RESUME:
		return (ixgbevf_resume(devinfo));

	case DDI_ATTACH:
		break;
	}

	/* Get the device instance */
	instance = ddi_get_instance(devinfo);

	/* Allocate memory for the instance data structure */
	ixgbevf = kmem_zalloc(sizeof (ixgbevf_t), KM_SLEEP);

	ixgbevf->dip = devinfo;
	ixgbevf->instance = instance;

	hw = &ixgbevf->hw;
	osdep = &ixgbevf->osdep;
	hw->back = osdep;
	osdep->ixgbevf = ixgbevf;

	/* Attach the instance pointer to the dev_info data structure */
	ddi_set_driver_private(devinfo, ixgbevf);

#ifdef DEBUG_INTEL
	/*
	 * hack for ethregs: save all dev_info_t's in a global array
	 */
	if (instance < DIP_MAX) {
		GlobalDipArray[instance] = devinfo;
		ixgbevf_log(ixgbevf, "instance %d dev_info_t 0x%p\n",
		    instance, devinfo);
	} else {
		ixgbevf_log(ixgbevf, "instance %d greater than %d\n",
		    instance, DIP_MAX);
	}
#endif	/* DEBUG_INTEL */

	/*
	 * Initialize for fma support
	 */
	ixgbevf->fm_capabilities = ixgbevf_get_prop(ixgbevf, PROP_FM_CAPABLE,
	    0, 0x0f, DDI_FM_EREPORT_CAPABLE | DDI_FM_ACCCHK_CAPABLE |
	    DDI_FM_DMACHK_CAPABLE | DDI_FM_ERRCB_CAPABLE);
	ixgbevf_fm_init(ixgbevf);
	ixgbevf->attach_progress |= ATTACH_PROGRESS_FM_INIT;

	/*
	 * Map PCI config space registers
	 */
	if (pci_config_setup(devinfo, &osdep->cfg_handle) != DDI_SUCCESS) {
		ixgbevf_error(ixgbevf, "Failed to map PCI configurations");
		goto attach_fail;
	}
	ixgbevf->attach_progress |= ATTACH_PROGRESS_PCI_CONFIG;

	/*
	 * Identify the chipset family
	 */
	if (ixgbevf_identify_hardware(ixgbevf) != IXGBE_SUCCESS) {
		ixgbevf_error(ixgbevf, "Failed to identify hardware");
		goto attach_fail;
	}

	/*
	 * Map device registers
	 */
	if (ixgbevf_regs_map(ixgbevf) != IXGBE_SUCCESS) {
		ixgbevf_error(ixgbevf, "Failed to map device registers");
		goto attach_fail;
	}
	ixgbevf->attach_progress |= ATTACH_PROGRESS_REGS_MAP;

	/*
	 * Initialize chipset specific hardware function pointers
	 */
	ixgbe_init_function_pointer_vf(hw);

	ixgbevf->hw.mac.ops.init_params(hw);
	ixgbevf->hw.mbx.ops.init_params(hw);

	/*
	 * Function-level Reset to put the virtual function in a known state,
	 * get MAC address in a message from physical function.
	 */
	if (ixgbevf_init_msg_api(ixgbevf) != IXGBE_SUCCESS) {
		ixgbevf_fm_ereport(ixgbevf, DDI_FM_DEVICE_INVAL_STATE);
		goto attach_fail;
	}

	/*
	 * Initialize driver parameters
	 */
	ixgbevf_init_properties(ixgbevf);
	ixgbevf->attach_progress |= ATTACH_PROGRESS_PROPS;

	/*
	 * Register interrupt callback
	 */
	if (ixgbevf_intr_cb_register(ixgbevf) != IXGBE_SUCCESS) {
		ixgbevf_error(ixgbevf, "Failed to register interrupt callback");
		goto attach_fail;
	}

	/*
	 * Allocate interrupts
	 */
	if (ixgbevf_alloc_intrs(ixgbevf) != IXGBE_SUCCESS) {
		ixgbevf_error(ixgbevf, "Failed to allocate interrupts");
		goto attach_fail;
	}
	ixgbevf->attach_progress |= ATTACH_PROGRESS_ALLOC_INTR;

	/*
	 * Allocate rx/tx rings based on the ring numbers.
	 * The actual numbers of rx/tx rings are decided by the number of
	 * allocated interrupt vectors, so we should allocate the rings after
	 * interrupts are allocated.
	 */
	if (ixgbevf_alloc_rings(ixgbevf) != IXGBE_SUCCESS) {
		ixgbevf_error(ixgbevf, "Failed to allocate rx and tx rings");
		goto attach_fail;
	}
	ixgbevf->attach_progress |= ATTACH_PROGRESS_ALLOC_RINGS;

	/*
	 * Map rings to interrupt vectors
	 */
	if (ixgbevf_map_intrs_to_vectors(ixgbevf) != IXGBE_SUCCESS) {
		ixgbevf_error(ixgbevf, "Failed to map interrupts to vectors");
		goto attach_fail;
	}

	/*
	 * Add interrupt handlers
	 */
	if (ixgbevf_add_intr_handlers(ixgbevf) != IXGBE_SUCCESS) {
		ixgbevf_error(ixgbevf, "Failed to add interrupt handlers");
		goto attach_fail;
	}
	ixgbevf->attach_progress |= ATTACH_PROGRESS_ADD_INTR;

	/*
	 * Create a taskq for sfp-change
	 */
	(void) sprintf(taskqname, "ixgbevf%d_taskq", instance);
	if ((ixgbevf->sfp_taskq = ddi_taskq_create(devinfo, taskqname,
	    1, TASKQ_DEFAULTPRI, 0)) == NULL) {
		ixgbevf_error(ixgbevf, "taskq_create failed");
		goto attach_fail;
	}
	ixgbevf->attach_progress |= ATTACH_PROGRESS_SFP_TASKQ;

	/*
	 * Initialize driver parameters
	 */
	if (ixgbevf_init_driver_settings(ixgbevf) != IXGBE_SUCCESS) {
		ixgbevf_error(ixgbevf, "Failed to initialize driver settings");
		goto attach_fail;
	}

	/*
	 * Initialize mutexes for this device.
	 * Do this before enabling the interrupt handler and
	 * register the softint to avoid the condition where
	 * interrupt handler can try using uninitialized mutex.
	 */
	ixgbevf_init_locks(ixgbevf);
	ixgbevf->attach_progress |= ATTACH_PROGRESS_LOCKS;

	/*
	 * Initialize chipset hardware
	 */
	if (ixgbevf_init(ixgbevf) != IXGBE_SUCCESS) {
		ixgbevf_error(ixgbevf, "Failed to initialize adapter");
		goto attach_fail;
	}

	ixgbevf->link_check_complete = B_FALSE;
	ixgbevf->link_check_hrtime = gethrtime() +
	    (IXGBE_LINK_UP_TIME * 100000000ULL);
	ixgbevf->attach_progress |= ATTACH_PROGRESS_INIT;

	if (ixgbevf_check_acc_handle(ixgbevf->osdep.cfg_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(ixgbevf->dip, DDI_SERVICE_LOST);
		goto attach_fail;
	}

	/*
	 * Initialize statistics
	 */
	if (ixgbevf_init_stats(ixgbevf) != IXGBE_SUCCESS) {
		ixgbevf_error(ixgbevf, "Failed to initialize statistics");
		goto attach_fail;
	}
	ixgbevf->attach_progress |= ATTACH_PROGRESS_STATS;

	/*
	 * Register the driver to the MAC
	 */
	if (ixgbevf_register_mac(ixgbevf) != IXGBE_SUCCESS) {
		ixgbevf_error(ixgbevf, "Failed to register MAC");
		goto attach_fail;
	}
	mac_link_update(ixgbevf->mac_hdl, LINK_STATE_UNKNOWN);
	ixgbevf->attach_progress |= ATTACH_PROGRESS_MAC;

	ixgbevf->periodic_id = ddi_periodic_add(ixgbevf_link_timer, ixgbevf,
	    IXGBE_CYCLIC_PERIOD, DDI_IPL_0);
	if (ixgbevf->periodic_id == 0) {
		ixgbevf_error(ixgbevf, "Failed to add the link check timer");
		goto attach_fail;
	}
	ixgbevf->attach_progress |= ATTACH_PROGRESS_LINK_TIMER;

	/*
	 * Now that mutex locks are initialized, and the chip is also
	 * initialized, enable interrupts.
	 */
	if (ixgbevf_enable_intrs(ixgbevf) != IXGBE_SUCCESS) {
		ixgbevf_error(ixgbevf, "Failed to enable DDI interrupts");
		goto attach_fail;
	}
	ixgbevf->attach_progress |= ATTACH_PROGRESS_ENABLE_INTR;

	/* enable the mailbox interrupt */
	ixgbevf_enable_mailbox_interrupt(ixgbevf);

	ixgbevf_log(ixgbevf, "%s, %s", ixgbevf_ident, ixgbevf_version);
	atomic_or_32(&ixgbevf->ixgbevf_state, IXGBE_INITIALIZED);

	return (DDI_SUCCESS);

attach_fail:
	(void) ixgbevf_unconfigure(devinfo, ixgbevf);
	return (DDI_FAILURE);
}

/*
 * ixgbevf_detach - Driver detach.
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
ixgbevf_detach(dev_info_t *devinfo, ddi_detach_cmd_t cmd)
{
	ixgbevf_t *ixgbevf;

	/*
	 * Check detach command
	 */
	switch (cmd) {
	default:
		return (DDI_FAILURE);

	case DDI_SUSPEND:
		return (ixgbevf_suspend(devinfo));

	case DDI_DETACH:
		break;
	}

	/*
	 * Get the pointer to the driver private data structure
	 */
	ixgbevf = (ixgbevf_t *)ddi_get_driver_private(devinfo);
	if (ixgbevf == NULL)
		return (DDI_FAILURE);

	/*
	 * If the device is still running, it needs to be stopped first.
	 * This check is necessary because under some specific circumstances,
	 * the detach routine can be called without stopping the interface
	 * first.
	 */
	mutex_enter(&ixgbevf->gen_lock);
	if (ixgbevf->ixgbevf_state & IXGBE_STARTED) {
		atomic_and_32(&ixgbevf->ixgbevf_state, ~IXGBE_STARTED);
		ixgbevf_stop(ixgbevf, B_TRUE);
		mutex_exit(&ixgbevf->gen_lock);
		/* Disable and stop the watchdog timer */
		ixgbevf_disable_watchdog_timer(ixgbevf);
	} else
		mutex_exit(&ixgbevf->gen_lock);

	/*
	 * Check if there are still rx buffers held by the upper layer.
	 * If so, fail the detach.
	 */
	if (!ixgbevf_rx_drain(ixgbevf))
		return (DDI_FAILURE);

	/*
	 * Do the remaining unconfigure routines
	 */
	if (ixgbevf_unconfigure(devinfo, ixgbevf) != IXGBE_SUCCESS)
		return (DDI_FAILURE);

	return (DDI_SUCCESS);
}

static int
ixgbevf_unconfigure(dev_info_t *devinfo, ixgbevf_t *ixgbevf)
{
	/*
	 * Disable interrupt
	 */
	if (ixgbevf->attach_progress & ATTACH_PROGRESS_ENABLE_INTR) {
		if (ixgbevf_disable_intrs(ixgbevf) != IXGBE_SUCCESS) {
			ixgbevf_error(ixgbevf, "Failed to disable interrupts");
			return (IXGBE_FAILURE);
		}
	}

	/*
	 * remove the link check timer
	 */
	if (ixgbevf->attach_progress & ATTACH_PROGRESS_LINK_TIMER) {
		if (ixgbevf->periodic_id != NULL) {
			ddi_periodic_delete(ixgbevf->periodic_id);
			ixgbevf->periodic_id = NULL;
		}
	}

	/*
	 * Unregister MAC
	 */
	if (ixgbevf->attach_progress & ATTACH_PROGRESS_MAC) {
		mutex_enter(&ixgbevf->gen_lock);
		if (mac_unregister(ixgbevf->mac_hdl) != 0) {
			mutex_exit(&ixgbevf->gen_lock);
			ixgbevf_error(ixgbevf,
			    "Failed to unregister mac handler");
			return (IXGBE_FAILURE);
		}
		mutex_exit(&ixgbevf->gen_lock);
	}

	/*
	 * Free statistics
	 */
	if (ixgbevf->attach_progress & ATTACH_PROGRESS_STATS) {
		kstat_delete((kstat_t *)ixgbevf->ixgbevf_ks);
	}

	/*
	 * Remove interrupt handlers
	 */
	if (ixgbevf->attach_progress & ATTACH_PROGRESS_ADD_INTR) {
		ixgbevf_rem_intr_handlers(ixgbevf);
	}

	/*
	 * Remove taskq for sfp-status-change
	 */
	if (ixgbevf->attach_progress & ATTACH_PROGRESS_SFP_TASKQ) {
		ddi_taskq_destroy(ixgbevf->sfp_taskq);
	}

	/*
	 * Remove interrupts
	 */
	if (ixgbevf->attach_progress & ATTACH_PROGRESS_ALLOC_INTR) {
		ixgbevf_rem_intrs(ixgbevf);
	}

	/*
	 * Unregister interrupt callback handler
	 */
	if (ixgbevf->cb_hdl != NULL) {
		if (ddi_cb_unregister(ixgbevf->cb_hdl) != DDI_SUCCESS) {
			ixgbevf_error(ixgbevf, "Failed to unregister callback");
			return (IXGBE_FAILURE);
		}
	}

	/*
	 * Remove driver properties
	 */
	if (ixgbevf->attach_progress & ATTACH_PROGRESS_PROPS) {
		(void) ddi_prop_remove_all(devinfo);
	}

	/*
	 * Stop the chipset
	 */
	if (ixgbevf->attach_progress & ATTACH_PROGRESS_INIT) {
		mutex_enter(&ixgbevf->gen_lock);
		ixgbevf_chip_stop(ixgbevf);
		mutex_exit(&ixgbevf->gen_lock);
	}

	/*
	 * Free register handle
	 */
	if (ixgbevf->attach_progress & ATTACH_PROGRESS_REGS_MAP) {
		if (ixgbevf->osdep.reg_handle != NULL)
			ddi_regs_map_free(&ixgbevf->osdep.reg_handle);
	}

	/*
	 * Free PCI config handle
	 */
	if (ixgbevf->attach_progress & ATTACH_PROGRESS_PCI_CONFIG) {
		if (ixgbevf->osdep.cfg_handle != NULL)
			pci_config_teardown(&ixgbevf->osdep.cfg_handle);
	}

	/*
	 * Free locks
	 */
	if (ixgbevf->attach_progress & ATTACH_PROGRESS_LOCKS) {
		ixgbevf_destroy_locks(ixgbevf);
	}

	/*
	 * Free the rx/tx rings
	 */
	if (ixgbevf->attach_progress & ATTACH_PROGRESS_ALLOC_RINGS) {
		ixgbevf_free_rings(ixgbevf);
	}

	/*
	 * Unregister FMA capabilities
	 */
	if (ixgbevf->attach_progress & ATTACH_PROGRESS_FM_INIT) {
		ixgbevf_fm_fini(ixgbevf);
	}

	/*
	 * Free the driver data structure
	 */
	kmem_free(ixgbevf, sizeof (ixgbevf_t));

	ddi_set_driver_private(devinfo, NULL);

	return (IXGBE_SUCCESS);
}

/*
 * ixgbevf_register_mac - Register the driver and its function pointers with
 * the GLD interface.
 */
static int
ixgbevf_register_mac(ixgbevf_t *ixgbevf)
{
	struct ixgbe_hw *hw = &ixgbevf->hw;
	mac_register_t *mac;
	int status;

	if ((mac = mac_alloc(MAC_VERSION)) == NULL)
		return (IXGBE_FAILURE);

	mac->m_type_ident = MAC_PLUGIN_IDENT_ETHER;
	mac->m_driver = ixgbevf;
	mac->m_dip = ixgbevf->dip;
	mac->m_src_addr = hw->mac.addr;
	mac->m_callbacks = &ixgbevf_m_callbacks;
	mac->m_min_sdu = 0;
	mac->m_max_sdu = ixgbevf->default_mtu;
	mac->m_margin = VLAN_TAGSZ;
	mac->m_priv_props = ixgbevf_priv_props;
	mac->m_flags = MAC_FLAGS_PROMISCUOUS_MULTICAST;

	status = mac_register(mac, &ixgbevf->mac_hdl);

	mac_free(mac);

	return ((status == 0) ? IXGBE_SUCCESS : IXGBE_FAILURE);
}

/*
 * ixgbevf_identify_hardware - Identify the type of the chipset.
 */
static int
ixgbevf_identify_hardware(ixgbevf_t *ixgbevf)
{
	struct ixgbe_hw *hw = &ixgbevf->hw;
	struct ixgbevf_osdep *osdep = &ixgbevf->osdep;

	/*
	 * Solaris recommendation is to get vendorid/deviceid from system
	 * properties
	 */
	hw->vendor_id = (uint16_t)ddi_prop_get_int(DDI_DEV_T_ANY, ixgbevf->dip,
	    DDI_PROP_DONTPASS, "vendor-id", -1);
	hw->device_id = (uint16_t)ddi_prop_get_int(DDI_DEV_T_ANY, ixgbevf->dip,
	    DDI_PROP_DONTPASS, "device-id", -1);

	/*
	 * Intel shared code wants these also; get them from config space
	 */
	hw->revision_id =
	    pci_config_get8(osdep->cfg_handle, PCI_CONF_REVID);
	hw->subsystem_device_id =
	    pci_config_get16(osdep->cfg_handle, PCI_CONF_SUBSYSID);
	hw->subsystem_vendor_id =
	    pci_config_get16(osdep->cfg_handle, PCI_CONF_SUBVENID);

	ixgbevf_log(ixgbevf, "vendor id: 0x%x   device id: 0x%x\n",
	    hw->vendor_id, hw->device_id);
	ixgbevf_log(ixgbevf, "subvendor id: 0x%x   subdevice id: 0x%x\n",
	    hw->subsystem_vendor_id, hw->subsystem_device_id);
	ixgbevf_log(ixgbevf, "revision id: 0x%x\n", hw->revision_id);

	/*
	 * Install adapter capabilities
	 */
	switch (hw->device_id) {
	case IXGBE_DEV_ID_82599_VF:
		hw->mac.type = ixgbe_mac_82599_vf;
		IXGBE_DEBUGLOG_0(ixgbevf, "identify 82599VF adapter\n");
		ixgbevf->capab = &ixgbevf_82599vf_cap;
		break;
	default:
		IXGBE_DEBUGLOG_1(ixgbevf,
		    "The adapter %d is not supported",
		    hw->mac.type);
		return (IXGBE_FAILURE);
	}

	return (IXGBE_SUCCESS);
}

/*
 * ixgbevf_regs_map - Map the device registers.
 *
 */
static int
ixgbevf_regs_map(ixgbevf_t *ixgbevf)
{
	dev_info_t *devinfo = ixgbevf->dip;
	struct ixgbe_hw *hw = &ixgbevf->hw;
	struct ixgbevf_osdep *osdep = &ixgbevf->osdep;
	off_t mem_size;
	int ret;

	/*
	 * First get the size of device registers to be mapped.
	 */
	ret = ddi_dev_regsize(devinfo, IXGBE_ADAPTER_REGSET, &mem_size);
	if (ret != DDI_SUCCESS) {
		ixgbevf_log(ixgbevf,
		    "ddi_dev_regsize fails, the return value is %d.", ret);
		return (IXGBE_FAILURE);
	}

	/*
	 * Call ddi_regs_map_setup() to map registers
	 */
	ret = ddi_regs_map_setup(devinfo, IXGBE_ADAPTER_REGSET,
	    (caddr_t *)&hw->hw_addr, 0,
	    mem_size, &ixgbevf_regs_acc_attr,
	    &osdep->reg_handle);
	if (ret != DDI_SUCCESS) {
		ixgbevf_log(ixgbevf,
		    "ddi_regs_map_setup fails, the return value is %d.", ret);
		return (IXGBE_FAILURE);
	}

	return (IXGBE_SUCCESS);
}

/*
 * ixgbevf_init_properties - Initialize driver properties.
 */
static void
ixgbevf_init_properties(ixgbevf_t *ixgbevf)
{
	/* Get configured queue limits */
	(void) ixgbevf_get_que_limits(ixgbevf);

	/* Get configured mtu limits */
	(void) ixgbevf_get_mtu_limits(ixgbevf);

	/*
	 * Get conf file properties, including link settings
	 * jumbo frames, ring number, descriptor number, etc.
	 */
	ixgbevf_get_conf(ixgbevf);

	ixgbevf_init_params(ixgbevf);
}

/*
 * ixgbevf_init_driver_settings - Initialize driver settings.
 *
 * bus information, rx/tx rings settings, link state, and any
 * other parameters thatop need to be setup during driver
 * initialization.
 */
static int
ixgbevf_init_driver_settings(ixgbevf_t *ixgbevf)
{
	dev_info_t *devinfo = ixgbevf->dip;
	ixgbevf_rx_ring_t *rx_ring;
	ixgbevf_group_t *rx_group, *tx_group;
	ixgbevf_tx_ring_t *tx_ring;
	uint32_t rx_size;
	uint32_t tx_size;
	uint32_t ring_per_group;
	int i;

	/*
	 * Get the system page size
	 */
	ixgbevf->sys_page_size = ddi_ptob(devinfo, (ulong_t)1);

	/*
	 * Set rx buffer size
	 *
	 * The IP header alignment room is counted in the calculation.
	 * The rx buffer size is in unit of 1K that is required by the
	 * chipset hardware.
	 */
	rx_size = ixgbevf->max_frame_size + IPHDR_ALIGN_ROOM;
	ixgbevf->rx_buf_size = ((rx_size >> 10) +
	    ((rx_size & (((uint32_t)1 << 10) - 1)) > 0 ? 1 : 0)) << 10;

	/*
	 * Set tx buffer size
	 */
	tx_size = ixgbevf->max_frame_size;
	ixgbevf->tx_buf_size = ((tx_size >> 10) +
	    ((tx_size & (((uint32_t)1 << 10) - 1)) > 0 ? 1 : 0)) << 10;

	/*
	 * Initialize rx/tx rings/groups parameters
	 */
	ring_per_group = ixgbevf->num_rx_rings / ixgbevf->num_groups;
	for (i = 0; i < ixgbevf->num_rx_rings; i++) {
		rx_ring = &ixgbevf->rx_rings[i];
		rx_ring->index = i;
		rx_ring->ixgbevf = ixgbevf;
		rx_ring->group_index = i / ring_per_group;
		rx_ring->hw_index = i;
	}

	for (i = 0; i < ixgbevf->num_groups; i++) {
		rx_group = &ixgbevf->rx_groups[i];
		rx_group->index = i;
		rx_group->ixgbevf = ixgbevf;
	}

	for (i = 0; i < ixgbevf->num_tx_rings; i++) {
		tx_ring = &ixgbevf->tx_rings[i];
		tx_ring->index = i;
		tx_ring->ixgbevf = ixgbevf;
		if (ixgbevf->tx_head_wb_enable)
			tx_ring->tx_recycle = ixgbevf_tx_recycle_head_wb;
		else
			tx_ring->tx_recycle = ixgbevf_tx_recycle_legacy;

		tx_ring->ring_size = ixgbevf->tx_ring_size;
		tx_ring->free_list_size = ixgbevf->tx_ring_size +
		    (ixgbevf->tx_ring_size >> 1);
	}

	for (i = 0; i < ixgbevf->num_groups; i++) {
		tx_group = &ixgbevf->tx_groups[i];
		tx_group->index = i;
		tx_group->ixgbevf = ixgbevf;
	}

	/*
	 * Initialize values of interrupt throttling rate
	 */
	for (i = 1; i < MAX_INTR_VECTOR; i++)
		ixgbevf->intr_throttling[i] = ixgbevf->intr_throttling[0];

	/*
	 * The initial link state should be "unknown"
	 */
	ixgbevf->link_state = LINK_STATE_UNKNOWN;

	return (IXGBE_SUCCESS);
}

/*
 * ixgbevf_init_locks - Initialize locks.
 */
static void
ixgbevf_init_locks(ixgbevf_t *ixgbevf)
{
	ixgbevf_rx_ring_t *rx_ring;
	ixgbevf_tx_ring_t *tx_ring;
	int i;

	for (i = 0; i < ixgbevf->num_rx_rings; i++) {
		rx_ring = &ixgbevf->rx_rings[i];
		mutex_init(&rx_ring->rx_lock, NULL,
		    MUTEX_DRIVER, DDI_INTR_PRI(ixgbevf->intr_pri));
	}

	for (i = 0; i < ixgbevf->num_tx_rings; i++) {
		tx_ring = &ixgbevf->tx_rings[i];
		mutex_init(&tx_ring->tx_lock, NULL,
		    MUTEX_DRIVER, DDI_INTR_PRI(ixgbevf->intr_pri));
		mutex_init(&tx_ring->recycle_lock, NULL,
		    MUTEX_DRIVER, DDI_INTR_PRI(ixgbevf->intr_pri));
		mutex_init(&tx_ring->tcb_head_lock, NULL,
		    MUTEX_DRIVER, DDI_INTR_PRI(ixgbevf->intr_pri));
		mutex_init(&tx_ring->tcb_tail_lock, NULL,
		    MUTEX_DRIVER, DDI_INTR_PRI(ixgbevf->intr_pri));
	}

	mutex_init(&ixgbevf->gen_lock, NULL,
	    MUTEX_DRIVER, DDI_INTR_PRI(ixgbevf->intr_pri));

	mutex_init(&ixgbevf->watchdog_lock, NULL,
	    MUTEX_DRIVER, DDI_INTR_PRI(ixgbevf->intr_pri));
}

/*
 * ixgbevf_destroy_locks - Destroy locks.
 */
static void
ixgbevf_destroy_locks(ixgbevf_t *ixgbevf)
{
	ixgbevf_rx_ring_t *rx_ring;
	ixgbevf_tx_ring_t *tx_ring;
	int i;

	for (i = 0; i < ixgbevf->num_rx_rings; i++) {
		rx_ring = &ixgbevf->rx_rings[i];
		mutex_destroy(&rx_ring->rx_lock);
	}

	for (i = 0; i < ixgbevf->num_tx_rings; i++) {
		tx_ring = &ixgbevf->tx_rings[i];
		mutex_destroy(&tx_ring->tx_lock);
		mutex_destroy(&tx_ring->recycle_lock);
		mutex_destroy(&tx_ring->tcb_head_lock);
		mutex_destroy(&tx_ring->tcb_tail_lock);
	}

	mutex_destroy(&ixgbevf->gen_lock);
	mutex_destroy(&ixgbevf->watchdog_lock);
}

static int
ixgbevf_resume(dev_info_t *devinfo)
{
	ixgbevf_t *ixgbevf;
	int i;

	ixgbevf = (ixgbevf_t *)ddi_get_driver_private(devinfo);
	if (ixgbevf == NULL)
		return (DDI_FAILURE);

	mutex_enter(&ixgbevf->gen_lock);

	if (ixgbevf->ixgbevf_state & IXGBE_STARTED) {
		if (ixgbevf_start(ixgbevf, B_FALSE) != IXGBE_SUCCESS) {
			mutex_exit(&ixgbevf->gen_lock);
			return (DDI_FAILURE);
		}

		/*
		 * Enable and start the watchdog timer
		 */
		ixgbevf_enable_watchdog_timer(ixgbevf);
	}

	atomic_and_32(&ixgbevf->ixgbevf_state, ~IXGBE_SUSPENDED);

	if (ixgbevf->ixgbevf_state & IXGBE_STARTED) {
		for (i = 0; i < ixgbevf->num_tx_rings; i++) {
			mac_tx_ring_update(ixgbevf->mac_hdl,
			    ixgbevf->tx_rings[i].ring_handle);
		}
	}

	mutex_exit(&ixgbevf->gen_lock);

	return (DDI_SUCCESS);
}

static int
ixgbevf_suspend(dev_info_t *devinfo)
{
	ixgbevf_t *ixgbevf;

	ixgbevf = (ixgbevf_t *)ddi_get_driver_private(devinfo);
	if (ixgbevf == NULL)
		return (DDI_FAILURE);

	mutex_enter(&ixgbevf->gen_lock);

	atomic_or_32(&ixgbevf->ixgbevf_state, IXGBE_SUSPENDED);
	if (!(ixgbevf->ixgbevf_state & IXGBE_STARTED)) {
		mutex_exit(&ixgbevf->gen_lock);
		return (DDI_SUCCESS);
	}
	ixgbevf_stop(ixgbevf, B_FALSE);

	mutex_exit(&ixgbevf->gen_lock);

	/*
	 * Disable and stop the watchdog timer
	 */
	ixgbevf_disable_watchdog_timer(ixgbevf);

	return (DDI_SUCCESS);
}

/*
 * ixgbevf_init - Initialize the device.
 */
static int
ixgbevf_init(ixgbevf_t *ixgbevf)
{
	struct ixgbe_hw *hw = &ixgbevf->hw;

	mutex_enter(&ixgbevf->gen_lock);

	hw->mac.ops.get_mac_addr(hw, hw->mac.addr);

	/*
	 * Validate the mac address
	 */
	if (!is_valid_mac_addr(hw->mac.addr)) {
		ixgbevf_error(ixgbevf, "Invalid mac address");
		return (IXGBE_FAILURE);
	}

	ixgbevf->unicst_total ++;

	/*
	 * Initialize the chipset hardware
	 */
	if (ixgbevf_chip_start(ixgbevf) != IXGBE_SUCCESS) {
		ixgbevf_fm_ereport(ixgbevf, DDI_FM_DEVICE_INVAL_STATE);
		goto init_fail;
	}

	if (ixgbevf_check_acc_handle(ixgbevf->osdep.reg_handle) != DDI_FM_OK) {
		goto init_fail;
	}

	mutex_exit(&ixgbevf->gen_lock);
	return (IXGBE_SUCCESS);

init_fail:

	mutex_exit(&ixgbevf->gen_lock);
	ddi_fm_service_impact(ixgbevf->dip, DDI_SERVICE_LOST);
	return (IXGBE_FAILURE);
}

/*
 * ixgbevf_chip_start - Initialize and start the chipset hardware.
 */
static int
ixgbevf_chip_start(ixgbevf_t *ixgbevf)
{
	struct ixgbe_hw *hw = &ixgbevf->hw;
	int ret_val, i;

	ASSERT(mutex_owned(&ixgbevf->gen_lock));

	/*
	 * Configure/Initialize hardware
	 */
	ret_val = hw->mac.ops.init_hw(hw);
	if (ret_val != IXGBE_SUCCESS) {
		ixgbevf_error(ixgbevf, "Failed to initialize hardware");
		return (IXGBE_FAILURE);
	}

	/*
	 * Setup adapter interrupt vectors
	 */
	ixgbevf_setup_adapter_vector(ixgbevf);

	/*
	 * Initialize unicast addresses.
	 */
	if (ixgbevf_init_unicst(ixgbevf) != IXGBE_SUCCESS) {
		ixgbevf_error(ixgbevf,
		    "Failed to initialize unicast addresses");
		return (IXGBE_FAILURE);
	}

	/*
	 * Setup and initialize the mctable structures.
	 */
	ixgbevf_setup_multicst(ixgbevf);

	/*
	 * Set interrupt throttling rate
	 */
	for (i = 0; i < ixgbevf->intr_cnt; i++) {
		IXGBE_WRITE_REG(hw, IXGBE_VTEITR(i),
		    ixgbevf->intr_throttling[i]);
	}

	/*
	 * Save the state of the phy
	 */
	ixgbevf_get_hw_state(ixgbevf);

	return (IXGBE_SUCCESS);
}

/*
 * ixgbevf_chip_stop - Stop the chipset hardware
 */
static void
ixgbevf_chip_stop(ixgbevf_t *ixgbevf)
{
	struct ixgbe_hw *hw = &ixgbevf->hw;

	ASSERT(mutex_owned(&ixgbevf->gen_lock));

	/*
	 * Reset the chipset
	 */
	if (hw->mac.ops.reset_hw(hw) != IXGBE_SUCCESS) {
		ixgbevf_error(ixgbevf, "Failed to reset the VF");
	}
}

/*
 * ixgbevf_reset - Reset the chipset and re-start the driver.
 *
 * It involves stopping and re-starting the chipset,
 * and re-configuring the rx/tx rings.
 */
static int
ixgbevf_reset(ixgbevf_t *ixgbevf)
{
	int i;

	/*
	 * Disable and stop the watchdog timer
	 */
	ixgbevf_disable_watchdog_timer(ixgbevf);

	mutex_enter(&ixgbevf->gen_lock);

	if (!(ixgbevf->ixgbevf_state & IXGBE_STARTED) ||
	    (ixgbevf->ixgbevf_state & IXGBE_SUSPENDED)) {
		mutex_exit(&ixgbevf->gen_lock);
		return (IXGBE_SUCCESS);
	}

	atomic_and_32(&ixgbevf->ixgbevf_state, ~IXGBE_STARTED);

	ixgbevf_stop(ixgbevf, B_FALSE);

	if (ixgbevf_start(ixgbevf, B_FALSE) != IXGBE_SUCCESS) {
		mutex_exit(&ixgbevf->gen_lock);
		return (IXGBE_FAILURE);
	}

	/*
	 * After resetting, need to recheck the link status.
	 */
	ixgbevf->link_check_complete = B_FALSE;
	ixgbevf->link_check_hrtime = gethrtime() +
	    (IXGBE_LINK_UP_TIME * 100000000ULL);

	atomic_or_32(&ixgbevf->ixgbevf_state, IXGBE_STARTED);

	if (!(ixgbevf->ixgbevf_state & IXGBE_SUSPENDED)) {
		for (i = 0; i < ixgbevf->num_tx_rings; i++) {
			mac_tx_ring_update(ixgbevf->mac_hdl,
			    ixgbevf->tx_rings[i].ring_handle);
		}
	}

	mutex_exit(&ixgbevf->gen_lock);

	/*
	 * Enable and start the watchdog timer
	 */
	ixgbevf_enable_watchdog_timer(ixgbevf);

	return (IXGBE_SUCCESS);
}

/*
 * ixgbevf_tx_clean - Clean the pending transmit packets and DMA resources.
 */
static void
ixgbevf_tx_clean(ixgbevf_t *ixgbevf)
{
	ixgbevf_tx_ring_t *tx_ring;
	tx_control_block_t *tcb;
	link_list_t pending_list;
	uint32_t desc_num;
	int i, j;

	LINK_LIST_INIT(&pending_list);

	for (i = 0; i < ixgbevf->num_tx_rings; i++) {
		tx_ring = &ixgbevf->tx_rings[i];

		mutex_enter(&tx_ring->recycle_lock);

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

				ixgbevf_free_tcb(tcb);

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
			if (ixgbevf->tx_head_wb_enable)
				*tx_ring->tbd_head_wb = 0;

			IXGBE_WRITE_REG(&ixgbevf->hw,
			    IXGBE_VFTDH(tx_ring->index), 0);
			IXGBE_WRITE_REG(&ixgbevf->hw,
			    IXGBE_VFTDT(tx_ring->index), 0);
		}

		mutex_exit(&tx_ring->recycle_lock);

		/*
		 * Add the tx control blocks in the pending list to
		 * the free list.
		 */
		ixgbevf_put_free_list(tx_ring, &pending_list);
	}
}

/*
 * ixgbevf_tx_drain - Drain the tx rings to allow pending packets to be
 * transmitted.
 */
static boolean_t
ixgbevf_tx_drain(ixgbevf_t *ixgbevf)
{
	ixgbevf_tx_ring_t *tx_ring;
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
		for (j = 0; j < ixgbevf->num_tx_rings; j++) {
			tx_ring = &ixgbevf->tx_rings[j];
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
 * ixgbevf_rx_drain - Wait for all rx buffers to be released by upper layer.
 */
static boolean_t
ixgbevf_rx_drain(ixgbevf_t *ixgbevf)
{
	boolean_t done = B_TRUE;
	int i;

	/*
	 * Polling the rx free list to check if those rx buffers held by
	 * the upper layer are released.
	 *
	 * Check the counter rcb_free to see if all pending buffers are
	 * released. No lock protection is needed here.
	 *
	 * Return B_TRUE if all pending buffers have been released;
	 * Otherwise return B_FALSE;
	 */
	for (i = 0; i < RX_DRAIN_TIME; i++) {
		done = (ixgbevf->rcb_pending == 0);

		if (done)
			break;

		msec_delay(1);
	}

	return (done);
}

/*
 * ixgbevf_start - Start the driver/chipset.
 */
int
ixgbevf_start(ixgbevf_t *ixgbevf, boolean_t alloc_buffer)
{
	int i;

	ASSERT(mutex_owned(&ixgbevf->gen_lock));

	if (alloc_buffer) {
		if (ixgbevf_alloc_rx_data(ixgbevf) != IXGBE_SUCCESS) {
			ixgbevf_error(ixgbevf,
			    "Failed to allocate software receive rings");
			return (IXGBE_FAILURE);
		}

		/* Allocate buffers for all the rx/tx rings */
		if (ixgbevf_alloc_dma(ixgbevf) != IXGBE_SUCCESS) {
			ixgbevf_error(ixgbevf,
			    "Failed to allocate DMA resource");
			return (IXGBE_FAILURE);
		}

		ixgbevf->tx_ring_init = B_TRUE;
	} else {
		ixgbevf->tx_ring_init = B_FALSE;
	}

	for (i = 0; i < ixgbevf->num_rx_rings; i++)
		mutex_enter(&ixgbevf->rx_rings[i].rx_lock);
	for (i = 0; i < ixgbevf->num_tx_rings; i++)
		mutex_enter(&ixgbevf->tx_rings[i].tx_lock);

	/*
	 * Start the chipset hardware
	 */
	if (ixgbevf_chip_start(ixgbevf) != IXGBE_SUCCESS) {
		ixgbevf_fm_ereport(ixgbevf, DDI_FM_DEVICE_INVAL_STATE);
		goto start_failure;
	}

	if (ixgbevf_check_acc_handle(ixgbevf->osdep.reg_handle) != DDI_FM_OK) {
		goto start_failure;
	}

	/*
	 * Setup the rx/tx rings
	 */
	ixgbevf_setup_rings(ixgbevf);

	/*
	 * ixgbevf_start() will be called when resetting, however if reset
	 * happens, we need to clear the ERROR and STALL flags before
	 * enabling the interrupts.
	 */
	atomic_and_32(&ixgbevf->ixgbevf_state, ~(IXGBE_ERROR | IXGBE_STALL));

	/*
	 * Enable adapter interrupts
	 * The interrupts must be enabled after the driver state is START
	 */
	ixgbevf_enable_adapter_interrupts(ixgbevf);

	for (i = ixgbevf->num_tx_rings - 1; i >= 0; i--)
		mutex_exit(&ixgbevf->tx_rings[i].tx_lock);
	for (i = ixgbevf->num_rx_rings - 1; i >= 0; i--)
		mutex_exit(&ixgbevf->rx_rings[i].rx_lock);

	return (IXGBE_SUCCESS);

start_failure:
	for (i = ixgbevf->num_tx_rings - 1; i >= 0; i--)
		mutex_exit(&ixgbevf->tx_rings[i].tx_lock);
	for (i = ixgbevf->num_rx_rings - 1; i >= 0; i--)
		mutex_exit(&ixgbevf->rx_rings[i].rx_lock);

	ddi_fm_service_impact(ixgbevf->dip, DDI_SERVICE_LOST);

	return (IXGBE_FAILURE);
}

/*
 * ixgbevf_stop - Stop the driver/chipset.
 */
void
ixgbevf_stop(ixgbevf_t *ixgbevf, boolean_t free_buffer)
{
	int i;

	ASSERT(mutex_owned(&ixgbevf->gen_lock));

	/*
	 * Disable the adapter interrupts
	 */
	ixgbevf_disable_adapter_interrupts(ixgbevf);

	/*
	 * Drain the pending tx packets
	 */
	(void) ixgbevf_tx_drain(ixgbevf);

	for (i = 0; i < ixgbevf->num_rx_rings; i++)
		mutex_enter(&ixgbevf->rx_rings[i].rx_lock);
	for (i = 0; i < ixgbevf->num_tx_rings; i++)
		mutex_enter(&ixgbevf->tx_rings[i].tx_lock);

	/*
	 * Stop the chipset hardware
	 */
	ixgbevf_chip_stop(ixgbevf);

	if (ixgbevf_check_acc_handle(ixgbevf->osdep.reg_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(ixgbevf->dip, DDI_SERVICE_LOST);
	}

	/*
	 * Clean the pending tx data/resources
	 */
	ixgbevf_tx_clean(ixgbevf);

	for (i = ixgbevf->num_tx_rings - 1; i >= 0; i--)
		mutex_exit(&ixgbevf->tx_rings[i].tx_lock);
	for (i = ixgbevf->num_rx_rings - 1; i >= 0; i--)
		mutex_exit(&ixgbevf->rx_rings[i].rx_lock);

	if (ixgbevf->link_state == LINK_STATE_UP) {
		ixgbevf->link_state = LINK_STATE_UNKNOWN;
		mac_link_update(ixgbevf->mac_hdl, ixgbevf->link_state);
	}

	if (free_buffer) {
		/*
		 * Release the DMA/memory resources of rx/tx rings
		 */
		ixgbevf_free_dma(ixgbevf);
		ixgbevf_free_rx_data(ixgbevf);
	}
}

/*
 * ixgbevf_cbfunc - Driver interface for generic DDI callbacks
 */
/* ARGSUSED */
static int
ixgbevf_cbfunc(dev_info_t *dip, ddi_cb_action_t cbaction, void *cbarg,
    void *arg1, void *arg2)
{
	ixgbevf_t *ixgbevf = (ixgbevf_t *)arg1;

	switch (cbaction) {
	/* IRM callback */
	int count;
	case DDI_CB_INTR_ADD:
	case DDI_CB_INTR_REMOVE:
		count = (int)(uintptr_t)cbarg;
		ASSERT(ixgbevf->intr_type == DDI_INTR_TYPE_MSIX);
		DTRACE_PROBE2(ixgbevf__irm__callback, int, count,
		    int, ixgbevf->intr_cnt);
		if (ixgbevf_intr_adjust(ixgbevf, cbaction, count) !=
		    DDI_SUCCESS) {
			ixgbevf_error(ixgbevf,
			    "IRM CB: Failed to adjust interrupts");
			goto cb_fail;
		}
		break;
	default:
		IXGBE_DEBUGLOG_1(ixgbevf, "DDI CB: action 0x%x NOT supported",
		    cbaction);
		return (DDI_ENOTSUP);
	}
	return (DDI_SUCCESS);
cb_fail:
	return (DDI_FAILURE);
}

/*
 * ixgbevf_intr_adjust - Adjust interrupt to respond to IRM request.
 */
static int
ixgbevf_intr_adjust(ixgbevf_t *ixgbevf, ddi_cb_action_t cbaction, int count)
{
	int i, rc, actual;
	uint32_t state_backup;

	if (!(ixgbevf->ixgbevf_state & IXGBE_INITIALIZED)) {
		return (DDI_FAILURE);
	}

	if (count == 0)
		return (DDI_SUCCESS);

	if ((cbaction == DDI_CB_INTR_ADD &&
	    ixgbevf->intr_cnt + count > ixgbevf->intr_cnt_max) ||
	    (cbaction == DDI_CB_INTR_REMOVE &&
	    ixgbevf->intr_cnt - count < ixgbevf->intr_cnt_min))
		return (DDI_FAILURE);

	for (i = 0; i < ixgbevf->num_rx_rings; i++)
		mac_ring_intr_set(ixgbevf->rx_rings[i].ring_handle, NULL);
	for (i = 0; i < ixgbevf->num_tx_rings; i++)
		mac_ring_intr_set(ixgbevf->tx_rings[i].ring_handle, NULL);

	mutex_enter(&ixgbevf->gen_lock);

	if (ixgbevf->ixgbevf_state & IXGBE_STARTED) {
		atomic_and_32(&ixgbevf->ixgbevf_state, ~IXGBE_STARTED);
		atomic_or_32(&ixgbevf->ixgbevf_state, IXGBE_INTR_ADJUST);
		atomic_or_32(&ixgbevf->ixgbevf_state, IXGBE_SUSPENDED);
		mac_link_update(ixgbevf->mac_hdl, LINK_STATE_UNKNOWN);

		ixgbevf_stop(ixgbevf, B_FALSE);
		/*
		 * Backup ixgbe->ixgbe_state when it won't be changed.
		 */
		state_backup = ixgbevf->ixgbevf_state;
		state_backup |= IXGBE_STARTED;
		state_backup &= ~IXGBE_INTR_ADJUST;
		state_backup &= ~IXGBE_SUSPENDED;
	}

	/*
	 * Disable interrupts
	 */
	if (ixgbevf->attach_progress & ATTACH_PROGRESS_ENABLE_INTR) {
		rc = ixgbevf_disable_intrs(ixgbevf);
		ASSERT(rc == IXGBE_SUCCESS);
	}
	ixgbevf->attach_progress &= ~ATTACH_PROGRESS_ENABLE_INTR;

	/*
	 * Remove interrupt handlers
	 */
	if (ixgbevf->attach_progress & ATTACH_PROGRESS_ADD_INTR) {
		ixgbevf_rem_intr_handlers(ixgbevf);
	}
	ixgbevf->attach_progress &= ~ATTACH_PROGRESS_ADD_INTR;

	/*
	 * Clear vect_map
	 */
	bzero(&ixgbevf->vect_map, sizeof (ixgbevf->vect_map));
	switch (cbaction) {
	case DDI_CB_INTR_ADD:
		rc = ddi_intr_alloc(ixgbevf->dip, ixgbevf->htable,
		    DDI_INTR_TYPE_MSIX, ixgbevf->intr_cnt, count, &actual,
		    DDI_INTR_ALLOC_NORMAL);
		if (rc != DDI_SUCCESS || actual != count) {
			ixgbevf_log(ixgbevf, "Adjust interrupts failed."
			    "return: %d, irm cb size: %d, actual: %d",
			    rc, count, actual);
			goto intr_adjust_fail;
		}
		ixgbevf->intr_cnt += count;
		break;

	case DDI_CB_INTR_REMOVE:
		for (i = ixgbevf->intr_cnt - count;
		    i < ixgbevf->intr_cnt; i ++) {
			rc = ddi_intr_free(ixgbevf->htable[i]);
			ixgbevf->htable[i] = NULL;
			if (rc != DDI_SUCCESS) {
				ixgbevf_log(ixgbevf, "Adjust interrupts failed."
				    "return: %d, irm cb size: %d, actual: %d",
				    rc, count, actual);
				goto intr_adjust_fail;
			}
		}
		ixgbevf->intr_cnt -= count;
		break;
	}

	/*
	 * Get priority for first vector, assume remaining are all the same
	 */
	rc = ddi_intr_get_pri(ixgbevf->htable[0], &ixgbevf->intr_pri);
	if (rc != DDI_SUCCESS) {
		ixgbevf_log(ixgbevf,
		    "Get interrupt priority failed: %d", rc);
		goto intr_adjust_fail;
	}
	rc = ddi_intr_get_cap(ixgbevf->htable[0], &ixgbevf->intr_cap);
	if (rc != DDI_SUCCESS) {
		ixgbevf_log(ixgbevf, "Get interrupt cap failed: %d", rc);
		goto intr_adjust_fail;
	}
	ixgbevf->attach_progress |= ATTACH_PROGRESS_ALLOC_INTR;

	/*
	 * Map rings to interrupt vectors
	 */
	if (ixgbevf_map_intrs_to_vectors(ixgbevf) != IXGBE_SUCCESS) {
		ixgbevf_error(ixgbevf,
		    "IRM CB: Failed to map interrupts to vectors");
		goto intr_adjust_fail;
	}

	/*
	 * Add interrupt handlers
	 */
	if (ixgbevf_add_intr_handlers(ixgbevf) != IXGBE_SUCCESS) {
		ixgbevf_error(ixgbevf,
		    "IRM CB: Failed to add interrupt handlers");
		goto intr_adjust_fail;
	}
	ixgbevf->attach_progress |= ATTACH_PROGRESS_ADD_INTR;

	/*
	 * Now that mutex locks are initialized, and the chip is also
	 * initialized, enable interrupts.
	 */
	if (ixgbevf_enable_intrs(ixgbevf) != IXGBE_SUCCESS) {
		ixgbevf_error(ixgbevf,
		    "IRM CB: Failed to enable DDI interrupts");
		goto intr_adjust_fail;
	}
	ixgbevf->attach_progress |= ATTACH_PROGRESS_ENABLE_INTR;

	if (state_backup & IXGBE_STARTED) {
		if (ixgbevf_start(ixgbevf, B_FALSE) != IXGBE_SUCCESS) {
			ixgbevf_error(ixgbevf, "IRM CB: Failed to start");
			goto intr_adjust_fail;
		}
		/* Restore the state */
		ixgbevf->ixgbevf_state = state_backup;
	}

	mutex_exit(&ixgbevf->gen_lock);

	for (i = 0; i < ixgbevf->num_rx_rings; i++) {
		mac_ring_intr_set(ixgbevf->rx_rings[i].ring_handle,
		    ixgbevf->htable[ixgbevf->rx_rings[i].intr_vector]);
	}
	for (i = 0; i < ixgbevf->num_tx_rings; i++) {
		mac_ring_intr_set(ixgbevf->tx_rings[i].ring_handle,
		    ixgbevf->htable[ixgbevf->tx_rings[i].intr_vector]);
	}

	/* Wakeup all Tx rings */
	for (i = 0; i < ixgbevf->num_tx_rings; i++) {
		mac_tx_ring_update(ixgbevf->mac_hdl,
		    ixgbevf->tx_rings[i].ring_handle);
	}

	IXGBE_DEBUGLOG_3(ixgbevf,
	    "IRM CB: interrupts new value: 0x%x(0x%x:0x%x).",
	    ixgbevf->intr_cnt, ixgbevf->intr_cnt_min, ixgbevf->intr_cnt_max);
	return (DDI_SUCCESS);

intr_adjust_fail:
	ddi_fm_service_impact(ixgbevf->dip, DDI_SERVICE_LOST);
	mutex_exit(&ixgbevf->gen_lock);
	return (DDI_FAILURE);
}

/*
 * ixgbevf_intr_cb_register - Register interrupt callback function.
 */
static int
ixgbevf_intr_cb_register(ixgbevf_t *ixgbevf)
{
	if (ddi_cb_register(ixgbevf->dip, DDI_CB_FLAG_INTR, ixgbevf_cbfunc,
	    ixgbevf, NULL, &ixgbevf->cb_hdl) != DDI_SUCCESS) {
		return (IXGBE_FAILURE);
	}
	IXGBE_DEBUGLOG_0(ixgbevf, "Interrupt callback function registered.");
	return (IXGBE_SUCCESS);
}

/*
 * ixgbevf_alloc_rings - Allocate memory space for rx/tx rings.
 */
static int
ixgbevf_alloc_rings(ixgbevf_t *ixgbevf)
{
	/*
	 * Allocate memory space for rx rings
	 */
	ixgbevf->rx_rings = kmem_zalloc(
	    sizeof (ixgbevf_rx_ring_t) * ixgbevf->num_rx_rings,
	    KM_NOSLEEP);

	if (ixgbevf->rx_rings == NULL) {
		return (IXGBE_FAILURE);
	}

	/*
	 * Allocate memory space for tx rings
	 */
	ixgbevf->tx_rings = kmem_zalloc(
	    sizeof (ixgbevf_tx_ring_t) * ixgbevf->num_tx_rings,
	    KM_NOSLEEP);

	if (ixgbevf->tx_rings == NULL) {
		kmem_free(ixgbevf->rx_rings,
		    sizeof (ixgbevf_rx_ring_t) * ixgbevf->num_rx_rings);
		ixgbevf->rx_rings = NULL;
		return (IXGBE_FAILURE);
	}

	/*
	 * Allocate memory space for rx ring groups
	 */
	ixgbevf->rx_groups = kmem_zalloc(
	    sizeof (ixgbevf_group_t) * ixgbevf->num_groups,
	    KM_NOSLEEP);

	if (ixgbevf->rx_groups == NULL) {
		kmem_free(ixgbevf->rx_rings,
		    sizeof (ixgbevf_rx_ring_t) * ixgbevf->num_rx_rings);
		kmem_free(ixgbevf->tx_rings,
		    sizeof (ixgbevf_tx_ring_t) * ixgbevf->num_tx_rings);
		ixgbevf->rx_rings = NULL;
		ixgbevf->tx_rings = NULL;
		return (IXGBE_FAILURE);
	}

	ixgbevf->tx_groups = kmem_zalloc(
	    sizeof (ixgbevf_group_t) * ixgbevf->num_groups,
	    KM_NOSLEEP);

	if (ixgbevf->tx_groups == NULL) {
		kmem_free(ixgbevf->rx_rings,
		    sizeof (ixgbevf_rx_ring_t) * ixgbevf->num_rx_rings);
		kmem_free(ixgbevf->tx_rings,
		    sizeof (ixgbevf_tx_ring_t) * ixgbevf->num_tx_rings);
		kmem_free(ixgbevf->rx_groups,
		    sizeof (ixgbevf_group_t) * ixgbevf->num_groups);
		ixgbevf->rx_rings = NULL;
		ixgbevf->tx_rings = NULL;
		return (IXGBE_FAILURE);
	}

	return (IXGBE_SUCCESS);
}

/*
 * ixgbevf_free_rings - Free the memory space of rx/tx rings.
 */
static void
ixgbevf_free_rings(ixgbevf_t *ixgbevf)
{
	if (ixgbevf->rx_rings != NULL) {
		kmem_free(ixgbevf->rx_rings,
		    sizeof (ixgbevf_rx_ring_t) * ixgbevf->num_rx_rings);
		ixgbevf->rx_rings = NULL;
	}

	if (ixgbevf->tx_rings != NULL) {
		kmem_free(ixgbevf->tx_rings,
		    sizeof (ixgbevf_tx_ring_t) * ixgbevf->num_tx_rings);
		ixgbevf->tx_rings = NULL;
	}

	if (ixgbevf->rx_groups != NULL) {
		kmem_free(ixgbevf->rx_groups,
		    sizeof (ixgbevf_group_t) * ixgbevf->num_groups);
		ixgbevf->rx_groups = NULL;
	}

	if (ixgbevf->tx_groups != NULL) {
		kmem_free(ixgbevf->tx_groups,
		    sizeof (ixgbevf_group_t) * ixgbevf->num_groups);
		ixgbevf->tx_groups = NULL;
	}
}

static int
ixgbevf_alloc_rx_data(ixgbevf_t *ixgbevf)
{
	ixgbevf_rx_ring_t *rx_ring;
	int i;

	for (i = 0; i < ixgbevf->num_rx_rings; i++) {
		rx_ring = &ixgbevf->rx_rings[i];
		if (ixgbevf_alloc_rx_ring_data(rx_ring) != IXGBE_SUCCESS)
			goto alloc_rx_rings_failure;
	}
	return (IXGBE_SUCCESS);

alloc_rx_rings_failure:
	ixgbevf_free_rx_data(ixgbevf);
	return (IXGBE_FAILURE);
}

static void
ixgbevf_free_rx_data(ixgbevf_t *ixgbevf)
{
	ixgbevf_rx_ring_t *rx_ring;
	ixgbevf_rx_data_t *rx_data;
	int i;

	for (i = 0; i < ixgbevf->num_rx_rings; i++) {
		rx_ring = &ixgbevf->rx_rings[i];

		mutex_enter(&ixgbevf->rx_pending_lock);
		rx_data = rx_ring->rx_data;

		if (rx_data != NULL) {
			rx_data->flag |= IXGBE_RX_STOPPED;

			if (rx_data->rcb_pending == 0) {
				ixgbevf_free_rx_ring_data(rx_data);
				rx_ring->rx_data = NULL;
			}
		}

		mutex_exit(&ixgbevf->rx_pending_lock);
	}
}

/*
 * ixgbevf_setup_rings - Setup rx/tx rings.
 */
static void
ixgbevf_setup_rings(ixgbevf_t *ixgbevf)
{
	/*
	 * Setup the rx/tx rings, including the following:
	 *
	 * 1. Setup the descriptor ring and the control block buffers;
	 * 2. Initialize necessary registers for receive/transmit;
	 * 3. Initialize software pointers/parameters for receive/transmit;
	 */
	ixgbevf_setup_rx(ixgbevf);

	ixgbevf_setup_tx(ixgbevf);
}

static void
ixgbevf_disable_rx_ring(ixgbevf_rx_ring_t *rx_ring)
{
	struct ixgbe_hw *hw = &rx_ring->ixgbevf->hw;
	uint32_t rxdctl;

	/*
	 * Setup the Receive Descriptor Control Register (VFRXDCTL)
	 */
	rxdctl = IXGBE_READ_REG(hw, IXGBE_VFRXDCTL(rx_ring->hw_index));
	rxdctl &= ~IXGBE_RXDCTL_ENABLE;
	IXGBE_WRITE_REG(hw, IXGBE_VFRXDCTL(rx_ring->hw_index), rxdctl);
}

/* Set VLAN stripping */
void
ixgbevf_set_vlan_stripping(ixgbevf_t *ixgbevf, boolean_t enable)
{
	ixgbevf_rx_ring_t *rx_ring;
	struct ixgbe_hw *hw = &ixgbevf->hw;
	uint32_t reg_val, i;

	ASSERT(mutex_owned(&ixgbevf->gen_lock));

	for (i = 0; i < ixgbevf->num_rx_rings; i++) {
		rx_ring = &ixgbevf->rx_rings[i];

		mutex_enter(&rx_ring->rx_lock);

		reg_val = IXGBE_READ_REG(hw, IXGBE_VFRXDCTL(rx_ring->hw_index));

		if (enable)
			reg_val |= IXGBE_RXDCTL_VME;
		else
			reg_val &= ~IXGBE_RXDCTL_VME;

		IXGBE_WRITE_REG(hw, IXGBE_VFRXDCTL(rx_ring->hw_index), reg_val);

		mutex_exit(&rx_ring->rx_lock);
	}
}

static void
ixgbevf_setup_rx_ring(ixgbevf_rx_ring_t *rx_ring)
{
	ixgbevf_t *ixgbevf = rx_ring->ixgbevf;
	ixgbevf_rx_data_t *rx_data = rx_ring->rx_data;
	struct ixgbe_hw *hw = &ixgbevf->hw;
	rx_control_block_t *rcb;
	union ixgbe_adv_rx_desc	*rbd;
	uint32_t size;
	uint32_t buf_low;
	uint32_t buf_high;
	uint32_t reg_val;
	int i;

	ASSERT(mutex_owned(&rx_ring->rx_lock));
	ASSERT(mutex_owned(&ixgbevf->gen_lock));

	for (i = 0; i < ixgbevf->rx_ring_size; i++) {
		rcb = rx_data->work_list[i];
		rbd = &rx_data->rbd_ring[i];

		rbd->read.pkt_addr = rcb->rx_buf.dma_address;
		rbd->read.hdr_addr = NULL;
	}

	/*
	 * Initialize the length register
	 */
	size = rx_data->ring_size * sizeof (union ixgbe_adv_rx_desc);
	IXGBE_WRITE_REG(hw, IXGBE_VFRDLEN(rx_ring->hw_index), size);

	/*
	 * Initialize the base address registers
	 */
	buf_low = (uint32_t)rx_data->rbd_area.dma_address;
	buf_high = (uint32_t)(rx_data->rbd_area.dma_address >> 32);
	IXGBE_WRITE_REG(hw, IXGBE_VFRDBAH(rx_ring->hw_index), buf_high);
	IXGBE_WRITE_REG(hw, IXGBE_VFRDBAL(rx_ring->hw_index), buf_low);

	/*
	 * Setup head & tail pointers
	 */
	IXGBE_WRITE_REG(hw, IXGBE_VFRDT(rx_ring->hw_index),
	    rx_data->ring_size - 1);
	IXGBE_WRITE_REG(hw, IXGBE_VFRDH(rx_ring->hw_index), 0);

	rx_data->rbd_next = 0;
	rx_data->lro_first = 0;

	/*
	 * Setup the Receive Descriptor Control Register (VFRXDCTL)
	 * PTHRESH=32 descriptors (half the internal cache)
	 * HTHRESH=0 descriptors (to minimize latency on fetch)
	 * WTHRESH defaults to 1 (writeback each descriptor)
	 */
	reg_val = IXGBE_READ_REG(hw, IXGBE_VFRXDCTL(rx_ring->hw_index));
	reg_val |= IXGBE_RXDCTL_ENABLE;	/* enable queue */
	/* set vlan stripping */
	if (ixgbevf->vlan_stripping)
		reg_val |= IXGBE_RXDCTL_VME;
	else
		reg_val &= ~IXGBE_RXDCTL_VME;

	IXGBE_WRITE_REG(hw, IXGBE_VFRXDCTL(rx_ring->hw_index), reg_val);

	/*
	 * Setup the Split and Replication Receive Control Register.
	 * Set the rx buffer size and the advanced descriptor type.
	 */
	reg_val = (ixgbevf->rx_buf_size >> IXGBE_SRRCTL_BSIZEPKT_SHIFT) |
	    IXGBE_SRRCTL_DESCTYPE_ADV_ONEBUF;
	reg_val |= IXGBE_SRRCTL_DROP_EN;
	IXGBE_WRITE_REG(hw, IXGBE_VFSRRCTL(rx_ring->hw_index), reg_val);
}

static void
ixgbevf_setup_rx(ixgbevf_t *ixgbevf)
{
	ixgbevf_rx_ring_t *rx_ring;
	struct ixgbe_hw *hw = &ixgbevf->hw;
	uint32_t reg_val, i;

	/* PSRTYPE must be configured for 82599 */

	reg_val = IXGBE_PSRTYPE_TCPHDR | IXGBE_PSRTYPE_UDPHDR |
	    IXGBE_PSRTYPE_IPV4HDR | IXGBE_PSRTYPE_IPV6HDR;
	reg_val |= IXGBE_PSRTYPE_L2HDR;

	ASSERT(ixgbevf->max_rx_rings);
	if (ixgbevf->max_rx_rings == 1)
		reg_val |= 0;
	else if (ixgbevf->max_rx_rings == 2)
		reg_val |= 0x20000000;
	else
		reg_val |= 0x40000000;

	IXGBE_WRITE_REG(hw, IXGBE_VFPSRTYPE, reg_val);

	for (i = 0; i < ixgbevf->num_rx_rings; i++) {
		rx_ring = &ixgbevf->rx_rings[i];

		/* make sure the rx ring is disabled */
		ixgbevf_disable_rx_ring(rx_ring);

		ixgbevf_setup_rx_ring(rx_ring);
	}
}

static void
ixgbevf_disable_tx_ring(ixgbevf_tx_ring_t *tx_ring)
{
	struct ixgbe_hw *hw = &tx_ring->ixgbevf->hw;
	uint32_t txdctl;

	/*
	 * Setup the Transmit Descriptor Control Register (VFTXDCTL)
	 */
	txdctl = IXGBE_READ_REG(hw, IXGBE_VFTXDCTL(tx_ring->index));
	txdctl &= ~IXGBE_TXDCTL_ENABLE;
	IXGBE_WRITE_REG(hw, IXGBE_VFTXDCTL(tx_ring->index), txdctl);
}

static void
ixgbevf_setup_tx_ring(ixgbevf_tx_ring_t *tx_ring)
{
	ixgbevf_t *ixgbevf = tx_ring->ixgbevf;
	struct ixgbe_hw *hw = &ixgbevf->hw;
	uint32_t size;
	uint32_t buf_low;
	uint32_t buf_high;
	uint32_t reg_val;

	ASSERT(mutex_owned(&tx_ring->tx_lock));
	ASSERT(mutex_owned(&ixgbevf->gen_lock));

	/*
	 * Initialize the length register
	 */
	size = tx_ring->ring_size * sizeof (union ixgbe_adv_tx_desc);
	IXGBE_WRITE_REG(hw, IXGBE_VFTDLEN(tx_ring->index), size);

	/*
	 * Initialize the base address registers
	 */
	buf_low = (uint32_t)tx_ring->tbd_area.dma_address;
	buf_high = (uint32_t)(tx_ring->tbd_area.dma_address >> 32);
	IXGBE_WRITE_REG(hw, IXGBE_VFTDBAL(tx_ring->index), buf_low);
	IXGBE_WRITE_REG(hw, IXGBE_VFTDBAH(tx_ring->index), buf_high);

	/*
	 * Setup head & tail pointers
	 */
	IXGBE_WRITE_REG(hw, IXGBE_VFTDH(tx_ring->index), 0);
	IXGBE_WRITE_REG(hw, IXGBE_VFTDT(tx_ring->index), 0);

	/*
	 * Setup head write-back
	 */
	if (ixgbevf->tx_head_wb_enable) {
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

		IXGBE_WRITE_REG(hw, IXGBE_VFTDWBAL(tx_ring->index), buf_low);
		IXGBE_WRITE_REG(hw, IXGBE_VFTDWBAH(tx_ring->index), buf_high);

		/*
		 * Turn off relaxed ordering for head write back or it will
		 * cause problems with the tx recycling
		 */
		reg_val = IXGBE_READ_REG(hw,
		    IXGBE_VFDCA_TXCTRL(tx_ring->index));
		reg_val &= ~IXGBE_DCA_TXCTRL_TX_WB_RO_EN;
		IXGBE_WRITE_REG(hw,
		    IXGBE_VFDCA_TXCTRL(tx_ring->index), reg_val);
	} else {
		tx_ring->tbd_head_wb = NULL;
	}

	tx_ring->tbd_head = 0;
	tx_ring->tbd_tail = 0;
	tx_ring->tbd_free = tx_ring->ring_size;

	if (ixgbevf->tx_ring_init == B_TRUE) {
		tx_ring->tcb_head = 0;
		tx_ring->tcb_tail = 0;
		tx_ring->tcb_free = tx_ring->free_list_size;
	}

	/*
	 * Initialize the s/w context structure
	 */
	bzero(&tx_ring->tx_context, sizeof (ixgbevf_tx_context_t));

	/*
	 * Enabling tx queues ..
	 */
	reg_val = IXGBE_READ_REG(hw, IXGBE_VFTXDCTL(tx_ring->index));
	reg_val |= IXGBE_TXDCTL_ENABLE;
	IXGBE_WRITE_REG(hw, IXGBE_VFTXDCTL(tx_ring->index), reg_val);
}

static void
ixgbevf_setup_tx(ixgbevf_t *ixgbevf)
{
	ixgbevf_tx_ring_t *tx_ring;
	int i;

	for (i = 0; i < ixgbevf->num_tx_rings; i++) {
		tx_ring = &ixgbevf->tx_rings[i];

		/* make sure the tx ring is disabled */
		ixgbevf_disable_tx_ring(tx_ring);

		ixgbevf_setup_tx_ring(tx_ring);
	}
}

/*
 * ixgbevf_init_unicst - Initialize the unicast addresses.
 */
static int
ixgbevf_init_unicst(ixgbevf_t *ixgbevf)
{
	uint8_t *mac_addr;
	int slot;
	int ret = IXGBE_SUCCESS;

	/*
	 * The mac addresses for this VF are maintained in a local list.
	 * If any mac address is enabled, request PF to configure the
	 * mac address in hardware.
	 */
	for (slot = 0; slot < ixgbevf->unicst_total; slot++) {
		mac_addr = ixgbevf->unicst_addr[slot].mac.addr;
		if (ixgbevf->unicst_addr[slot].mac.set == 1) {
			ret = ixgbevf_enable_mac_addr(ixgbevf,
			    mac_addr);
			if (ret != IXGBE_SUCCESS) {
				IXGBE_DEBUGLOG_0(ixgbevf,
				    "ixgbevf enable mac addr fail");
				break;
			}
		}
	}
	return (ret);
}

/*
 * ixgbevf_unicst_find - Find the slot for the specified unicast address
 */
int
ixgbevf_unicst_find(ixgbevf_t *ixgbevf, const uint8_t *mac_addr)
{
	int slot;

	ASSERT(mutex_owned(&ixgbevf->gen_lock));

	for (slot = 0; slot < ixgbevf->unicst_total; slot++) {
		if (bcmp(ixgbevf->unicst_addr[slot].mac.addr,
		    mac_addr, ETHERADDRL) == 0)
			return (slot);
	}

	return (-1);
}

/*
 * ixgbevf_multicst_add - Add a multicst address.
 */
int
ixgbevf_multicst_add(ixgbevf_t *ixgbevf, const uint8_t *multiaddr)
{
	ASSERT(mutex_owned(&ixgbevf->gen_lock));

	if ((multiaddr[0] & 01) == 0) {
		return (EINVAL);
	}

	if (ixgbevf->mcast_count >= MAX_NUM_MULTICAST_ADDRESSES) {
		return (ENOENT);
	}

	bcopy(multiaddr,
	    &ixgbevf->mcast_table[ixgbevf->mcast_count], ETHERADDRL);
	ixgbevf->mcast_count++;

	/*
	 * Update the multicast table in the hardware
	 */
	ixgbevf_setup_multicst(ixgbevf);

	if (ixgbevf_check_acc_handle(ixgbevf->osdep.reg_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(ixgbevf->dip, DDI_SERVICE_DEGRADED);
		return (EIO);
	}

	return (0);
}

/*
 * ixgbevf_multicst_remove - Remove a multicst address.
 */
int
ixgbevf_multicst_remove(ixgbevf_t *ixgbevf, const uint8_t *multiaddr)
{
	int i;

	ASSERT(mutex_owned(&ixgbevf->gen_lock));

	for (i = 0; i < ixgbevf->mcast_count; i++) {
		if (bcmp(multiaddr, &ixgbevf->mcast_table[i],
		    ETHERADDRL) == 0) {
			for (i++; i < ixgbevf->mcast_count; i++) {
				ixgbevf->mcast_table[i - 1] =
				    ixgbevf->mcast_table[i];
			}
			ixgbevf->mcast_count--;
			break;
		}
	}

	/*
	 * Update the multicast table in the hardware
	 */
	ixgbevf_setup_multicst(ixgbevf);

	if (ixgbevf_check_acc_handle(ixgbevf->osdep.reg_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(ixgbevf->dip, DDI_SERVICE_DEGRADED);
		return (EIO);
	}

	return (0);
}

/*
 * ixgbevf_setup_multicast - Setup multicast data structures.
 *
 * This routine initializes all of the multicast related structures
 * and save them in the hardware registers.
 */
static void
ixgbevf_setup_multicst(ixgbevf_t *ixgbevf)
{
	uint8_t *mc_addr_list;
	uint32_t mc_addr_count;
	struct ixgbe_hw *hw = &ixgbevf->hw;

	ASSERT(mutex_owned(&ixgbevf->gen_lock));

	ASSERT(ixgbevf->mcast_count <= MAX_NUM_MULTICAST_ADDRESSES);

	mc_addr_list = (uint8_t *)ixgbevf->mcast_table;
	mc_addr_count = ixgbevf->mcast_count;

	/*
	 * Update the multicast addresses to the MTA registers
	 */
	(void) hw->mac.ops.update_mc_addr_list(hw, mc_addr_list, mc_addr_count,
	    ixgbevf_mc_table_itr);
}

/*
 * ixgbevf_get_conf - Get driver configurations set in driver.conf.
 *
 * This routine gets user-configured values out of the configuration
 * file ixgbevf.conf.
 *
 * For each configurable value, there is a minimum, a maximum, and a
 * default.
 * If user does not configure a value, use the default.
 * If user configures below the minimum, use the minumum.
 * If user configures above the maximum, use the maxumum.
 */
static void
ixgbevf_get_conf(ixgbevf_t *ixgbevf)
{
	struct ixgbe_hw *hw = &ixgbevf->hw;

	/*
	 * ixgbevf driver supports the following user configurations:
	 *
	 * Jumbo frame configuration:
	 *    default_mtu
	 *
	 * Multiple rings configurations:
	 *    tx_queue_number
	 *    tx_ring_size
	 *    rx_queue_number
	 *    rx_ring_size
	 *
	 * Call ixgbevf_get_prop() to get the value for a specific
	 * configuration parameter.
	 */

	/*
	 * Jumbo frame configuration - max_frame_size controls host buffer
	 * allocation, so includes MTU, ethernet header, vlan tag and
	 * frame check sequence.
	 */
	ixgbevf->default_mtu = ixgbevf_get_prop(ixgbevf, PROP_DEFAULT_MTU,
	    MIN_MTU, ixgbevf->max_mtu, DEFAULT_MTU);

	ixgbevf->max_frame_size = ixgbevf->default_mtu +
	    sizeof (struct ether_vlan_header) + ETHERFCSL;

	/*
	 * Multiple rings configurations
	 */
	ixgbevf->num_tx_rings = ixgbevf_get_prop(ixgbevf, PROP_TX_QUEUE_NUM,
	    ixgbevf->capab->min_tx_que_num,
	    ixgbevf->max_tx_rings,
	    ixgbevf->max_tx_rings);
	ixgbevf->tx_ring_size = ixgbevf_get_prop(ixgbevf, PROP_TX_RING_SIZE,
	    MIN_TX_RING_SIZE, MAX_TX_RING_SIZE, DEFAULT_TX_RING_SIZE);

	ixgbevf->num_rx_rings = ixgbevf_get_prop(ixgbevf, PROP_RX_QUEUE_NUM,
	    ixgbevf->capab->min_rx_que_num,
	    ixgbevf->max_rx_rings,
	    ixgbevf->max_rx_rings);
	ixgbevf->rx_ring_size = ixgbevf_get_prop(ixgbevf, PROP_RX_RING_SIZE,
	    MIN_RX_RING_SIZE, MAX_RX_RING_SIZE, DEFAULT_RX_RING_SIZE);

	/*
	 * Multiple groups configuration
	 */
	ixgbevf->num_groups = ixgbevf_get_prop(ixgbevf, PROP_RX_GROUP_NUM,
	    ixgbevf->capab->min_grp_num, ixgbevf->capab->max_grp_num,
	    ixgbevf->capab->def_grp_num);

	ixgbevf->mr_enable = ixgbevf_get_prop(ixgbevf, PROP_MR_ENABLE,
	    0, 1, DEFAULT_MR_ENABLE);

	ixgbevf->tx_hcksum_enable = ixgbevf_get_prop(ixgbevf,
	    PROP_TX_HCKSUM_ENABLE, 0, 1, DEFAULT_TX_HCKSUM_ENABLE);
	ixgbevf->rx_hcksum_enable = ixgbevf_get_prop(ixgbevf,
	    PROP_RX_HCKSUM_ENABLE, 0, 1, DEFAULT_RX_HCKSUM_ENABLE);
	ixgbevf->lso_enable = ixgbevf_get_prop(ixgbevf, PROP_LSO_ENABLE,
	    0, 1, DEFAULT_LSO_ENABLE);
	ixgbevf->lro_enable = ixgbevf_get_prop(ixgbevf, PROP_LRO_ENABLE,
	    0, 1, DEFAULT_LRO_ENABLE);
	if (hw->mac.type == ixgbe_mac_82599_vf)
		ixgbevf->lro_enable = B_FALSE;

	ixgbevf->tx_head_wb_enable = ixgbevf_get_prop(ixgbevf,
	    PROP_TX_HEAD_WB_ENABLE, 0, 1, DEFAULT_TX_HEAD_WB_ENABLE);

	/* Head Write Back not recommended for 82599 */
	if (hw->mac.type >= ixgbe_mac_82599_vf) {
		ixgbevf->tx_head_wb_enable = B_FALSE;
	}

	/*
	 * ixgbevf LSO needs the tx h/w checksum support.
	 * LSO will be disabled if tx h/w checksum is not
	 * enabled.
	 */
	if (ixgbevf->tx_hcksum_enable == B_FALSE) {
		ixgbevf->lso_enable = B_FALSE;
	}

	/*
	 * ixgbevf LRO needs the rx h/w checksum support.
	 * LRO will be disabled if rx h/w checksum is not
	 * enabled.
	 */
	if (ixgbevf->rx_hcksum_enable == B_FALSE) {
		ixgbevf->lro_enable = B_FALSE;
	}

	/*
	 * ixgbevf LRO only been supported by 82599 now
	 */
	if (hw->mac.type != ixgbe_mac_82599_vf) {
		ixgbevf->lro_enable = B_FALSE;
	}
	ixgbevf->tx_copy_thresh = ixgbevf_get_prop(ixgbevf,
	    PROP_TX_COPY_THRESHOLD, MIN_TX_COPY_THRESHOLD,
	    MAX_TX_COPY_THRESHOLD, DEFAULT_TX_COPY_THRESHOLD);
	ixgbevf->tx_recycle_thresh = ixgbevf_get_prop(ixgbevf,
	    PROP_TX_RECYCLE_THRESHOLD, MIN_TX_RECYCLE_THRESHOLD,
	    MAX_TX_RECYCLE_THRESHOLD, DEFAULT_TX_RECYCLE_THRESHOLD);
	ixgbevf->tx_overload_thresh = ixgbevf_get_prop(ixgbevf,
	    PROP_TX_OVERLOAD_THRESHOLD, MIN_TX_OVERLOAD_THRESHOLD,
	    MAX_TX_OVERLOAD_THRESHOLD, DEFAULT_TX_OVERLOAD_THRESHOLD);
	ixgbevf->tx_resched_thresh = ixgbevf_get_prop(ixgbevf,
	    PROP_TX_RESCHED_THRESHOLD, MIN_TX_RESCHED_THRESHOLD,
	    MAX_TX_RESCHED_THRESHOLD, DEFAULT_TX_RESCHED_THRESHOLD);

	ixgbevf->rx_copy_thresh = ixgbevf_get_prop(ixgbevf,
	    PROP_RX_COPY_THRESHOLD, MIN_RX_COPY_THRESHOLD,
	    MAX_RX_COPY_THRESHOLD, DEFAULT_RX_COPY_THRESHOLD);
	ixgbevf->rx_limit_per_intr = ixgbevf_get_prop(ixgbevf,
	    PROP_RX_LIMIT_PER_INTR, MIN_RX_LIMIT_PER_INTR,
	    MAX_RX_LIMIT_PER_INTR, DEFAULT_RX_LIMIT_PER_INTR);

	ixgbevf->intr_throttling[0] = ixgbevf_get_prop(ixgbevf,
	    PROP_INTR_THROTTLING,
	    ixgbevf->capab->min_intr_throttle,
	    ixgbevf->capab->max_intr_throttle,
	    ixgbevf->capab->def_intr_throttle);
	/*
	 * 82599 requires the interupt throttling rate is
	 * a multiple of 8. This is enforced by the register
	 * definiton.
	 */
	if (hw->mac.type == ixgbe_mac_82599_vf)
		ixgbevf->intr_throttling[0] =
		    ixgbevf->intr_throttling[0] & 0xFF8;
}

static void
ixgbevf_init_params(ixgbevf_t *ixgbevf)
{
	ixgbevf->param_en_10000fdx_cap = 1;
	ixgbevf->param_en_1000fdx_cap = 1;
	ixgbevf->param_en_100fdx_cap = 1;
	ixgbevf->param_adv_10000fdx_cap = 1;
	ixgbevf->param_adv_1000fdx_cap = 1;
	ixgbevf->param_adv_100fdx_cap = 1;

	ixgbevf->param_pause_cap = 1;
	ixgbevf->param_asym_pause_cap = 1;
	ixgbevf->param_rem_fault = 0;

	ixgbevf->param_adv_autoneg_cap = 1;
	ixgbevf->param_adv_pause_cap = 1;
	ixgbevf->param_adv_asym_pause_cap = 1;
	ixgbevf->param_adv_rem_fault = 0;

	ixgbevf->param_lp_10000fdx_cap = 0;
	ixgbevf->param_lp_1000fdx_cap = 0;
	ixgbevf->param_lp_100fdx_cap = 0;
	ixgbevf->param_lp_autoneg_cap = 0;
	ixgbevf->param_lp_pause_cap = 0;
	ixgbevf->param_lp_asym_pause_cap = 0;
	ixgbevf->param_lp_rem_fault = 0;
}

/*
 * ixgbevf_get_prop - Get a property value out of the configuration file
 * ixgbevf.conf.
 *
 * Caller provides the name of the property, a default value, a minimum
 * value, and a maximum value.
 *
 * Return configured value of the property, with default, minimum and
 * maximum properly applied.
 */
static int
ixgbevf_get_prop(ixgbevf_t *ixgbevf,
    char *propname,	/* name of the property */
    int minval,		/* minimum acceptable value */
    int maxval,		/* maximim acceptable value */
    int defval)		/* default value */
{
	int value;

	/*
	 * Call ddi_prop_get_int() to read the conf settings
	 */
	value = ddi_prop_get_int(DDI_DEV_T_ANY, ixgbevf->dip,
	    DDI_PROP_DONTPASS, propname, defval);
	if (value > maxval)
		value = maxval;

	if (value < minval)
		value = minval;

	return (value);
}

/*
 * ixgbevf_driver_link_check - Link status processing.
 *
 * This function can be called in both kernel context and interrupt context
 */
static void
ixgbevf_driver_link_check(ixgbevf_t *ixgbevf)
{
	struct ixgbe_hw *hw = &ixgbevf->hw;
	ixgbe_link_speed speed = IXGBE_LINK_SPEED_UNKNOWN;
	boolean_t link_up = B_FALSE;
	boolean_t link_changed = B_FALSE;

	ASSERT(mutex_owned(&ixgbevf->gen_lock));

	(void) hw->mac.ops.check_link(hw, &speed, &link_up, false);
	if (link_up) {
		ixgbevf->link_check_complete = B_TRUE;

		/*
		 * The Link is up, check whether it was marked as down earlier
		 */
		if (ixgbevf->link_state != LINK_STATE_UP) {
			switch (speed) {
			case IXGBE_LINK_SPEED_10GB_FULL:
				ixgbevf->link_speed = SPEED_10GB;
				break;
			case IXGBE_LINK_SPEED_1GB_FULL:
				ixgbevf->link_speed = SPEED_1GB;
				break;
			case IXGBE_LINK_SPEED_100_FULL:
				ixgbevf->link_speed = SPEED_100;
			}
			ixgbevf->link_duplex = LINK_DUPLEX_FULL;
			ixgbevf->link_state = LINK_STATE_UP;
			link_changed = B_TRUE;

			if (ixgbevf->link_speed == SPEED_10GB)
				ixgbevf->stall_threshold = TX_STALL_TIME_2S;
			else
				ixgbevf->stall_threshold = TX_STALL_TIME_8S;
		}
	} else {
		if (ixgbevf->link_check_complete == B_TRUE ||
		    (ixgbevf->link_check_complete == B_FALSE &&
		    gethrtime() >= ixgbevf->link_check_hrtime)) {
			/*
			 * The link is really down
			 */
			ixgbevf->link_check_complete = B_TRUE;

			if (ixgbevf->link_state != LINK_STATE_DOWN) {
				ixgbevf->link_speed = 0;
				ixgbevf->link_duplex = LINK_DUPLEX_UNKNOWN;
				ixgbevf->link_state = LINK_STATE_DOWN;
				link_changed = B_TRUE;
			}
		}
	}

	/*
	 * this is only reached after a link-status-change interrupt
	 * so always get new phy state
	 */
	ixgbevf_get_hw_state(ixgbevf);

	if (link_changed && ixgbevf->mac_hdl) {
		if (!ixgbevf->reset_flag)
			mac_link_update(ixgbevf->mac_hdl, ixgbevf->link_state);
		if (ixgbevf->link_state == LINK_STATE_UP)
			ixgbevf->reset_flag = B_FALSE;
	}
}

/*
 * ixgbevf_link_timer - timer for link status detection
 */
static void
ixgbevf_link_timer(void *arg)
{
	ixgbevf_t *ixgbevf = (ixgbevf_t *)arg;

	mutex_enter(&ixgbevf->gen_lock);
	ixgbevf_driver_link_check(ixgbevf);
	mutex_exit(&ixgbevf->gen_lock);
}

/*
 * ixgbevf_local_timer - Driver watchdog function.
 *
 * This function will handle the transmit stall check and other routines.
 */
static void
ixgbevf_local_timer(void *arg)
{
	ixgbevf_t *ixgbevf = (ixgbevf_t *)arg;

	if (ixgbevf->ixgbevf_state & IXGBE_ERROR) {
		ixgbevf->reset_count++;
		if (ixgbevf_reset(ixgbevf) == IXGBE_SUCCESS)
			ddi_fm_service_impact(ixgbevf->dip,
			    DDI_SERVICE_RESTORED);
		ixgbevf_restart_watchdog_timer(ixgbevf);
		return;
	}

	if (ixgbevf_stall_check(ixgbevf)) {
		atomic_or_32(&ixgbevf->ixgbevf_state, IXGBE_STALL);
		ddi_fm_service_impact(ixgbevf->dip, DDI_SERVICE_DEGRADED);

		ixgbevf->reset_count++;
		if (ixgbevf_reset(ixgbevf) == IXGBE_SUCCESS)
			ddi_fm_service_impact(ixgbevf->dip,
			    DDI_SERVICE_RESTORED);
	}

	/* check the mailbox */
	mutex_enter(&ixgbevf->gen_lock);
	ixgbevf_rcv_msg_pf_api20(ixgbevf);
	mutex_exit(&ixgbevf->gen_lock);

	ixgbevf_restart_watchdog_timer(ixgbevf);
}

/*
 * ixgbevf_stall_check - Check for transmit stall.
 *
 * This function checks if the adapter is stalled (in transmit).
 *
 * It is called each time the watchdog timeout is invoked.
 * If the transmit descriptor reclaim continuously fails,
 * the watchdog value will increment by 1. If the watchdog
 * value exceeds the threshold, the ixgbevf is assumed to
 * have stalled and need to be reset.
 */
static boolean_t
ixgbevf_stall_check(ixgbevf_t *ixgbevf)
{
	ixgbevf_tx_ring_t *tx_ring;
	boolean_t result;
	int i;

	if (ixgbevf->link_state != LINK_STATE_UP)
		return (B_FALSE);

	/*
	 * If any tx ring is stalled, we'll reset the chipset
	 */
	result = B_FALSE;
	for (i = 0; i < ixgbevf->num_tx_rings; i++) {
		tx_ring = &ixgbevf->tx_rings[i];
		tx_ring->tx_recycle(tx_ring);

		if (ixgbevf->stall_flag) {
			result = B_TRUE;
			ixgbevf->stall_flag = B_FALSE;
			ixgbevf->reset_flag = B_TRUE;
			return (result);
		}
	}

	return (result);
}


/*
 * is_valid_mac_addr - Check if the mac address is valid.
 */
static boolean_t
is_valid_mac_addr(uint8_t *mac_addr)
{
	const uint8_t addr_test1[6] = { 0, 0, 0, 0, 0, 0 };
	const uint8_t addr_test2[6] =
	    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

	if (!(bcmp(addr_test1, mac_addr, ETHERADDRL)) ||
	    !(bcmp(addr_test2, mac_addr, ETHERADDRL)))
		return (B_FALSE);

	return (B_TRUE);
}

#pragma inline(ixgbevf_arm_watchdog_timer)
static void
ixgbevf_arm_watchdog_timer(ixgbevf_t *ixgbevf)
{
	/*
	 * Fire a watchdog timer
	 */
	ixgbevf->watchdog_tid =
	    timeout(ixgbevf_local_timer,
	    (void *)ixgbevf, 1 * drv_usectohz(1000000));

}

/*
 * ixgbevf_enable_watchdog_timer - Enable and start the driver watchdog timer.
 */
void
ixgbevf_enable_watchdog_timer(ixgbevf_t *ixgbevf)
{
	mutex_enter(&ixgbevf->watchdog_lock);

	if (!ixgbevf->watchdog_enable) {
		ixgbevf->watchdog_enable = B_TRUE;
		ixgbevf->watchdog_start = B_TRUE;
		ixgbevf_arm_watchdog_timer(ixgbevf);
	}

	mutex_exit(&ixgbevf->watchdog_lock);
}

/*
 * ixgbevf_disable_watchdog_timer - Disable and stop the driver watchdog timer.
 */
void
ixgbevf_disable_watchdog_timer(ixgbevf_t *ixgbevf)
{
	timeout_id_t tid;

	mutex_enter(&ixgbevf->watchdog_lock);

	ixgbevf->watchdog_enable = B_FALSE;
	ixgbevf->watchdog_start = B_FALSE;
	tid = ixgbevf->watchdog_tid;
	ixgbevf->watchdog_tid = 0;

	mutex_exit(&ixgbevf->watchdog_lock);

	if (tid != 0)
		(void) untimeout(tid);
}

/*
 * ixgbevf_start_watchdog_timer - Start the driver watchdog timer.
 */
void
ixgbevf_start_watchdog_timer(ixgbevf_t *ixgbevf)
{
	mutex_enter(&ixgbevf->watchdog_lock);

	if (ixgbevf->watchdog_enable) {
		if (!ixgbevf->watchdog_start) {
			ixgbevf->watchdog_start = B_TRUE;
			ixgbevf_arm_watchdog_timer(ixgbevf);
		}
	}

	mutex_exit(&ixgbevf->watchdog_lock);
}

/*
 * ixgbevf_restart_watchdog_timer - Restart the driver watchdog timer.
 */
static void
ixgbevf_restart_watchdog_timer(ixgbevf_t *ixgbevf)
{
	mutex_enter(&ixgbevf->watchdog_lock);

	if (ixgbevf->watchdog_start)
		ixgbevf_arm_watchdog_timer(ixgbevf);

	mutex_exit(&ixgbevf->watchdog_lock);
}

/*
 * ixgbevf_stop_watchdog_timer - Stop the driver watchdog timer.
 */
void
ixgbevf_stop_watchdog_timer(ixgbevf_t *ixgbevf)
{
	timeout_id_t tid;

	mutex_enter(&ixgbevf->watchdog_lock);

	ixgbevf->watchdog_start = B_FALSE;
	tid = ixgbevf->watchdog_tid;
	ixgbevf->watchdog_tid = 0;

	mutex_exit(&ixgbevf->watchdog_lock);

	if (tid != 0)
		(void) untimeout(tid);
}

/*
 * ixgbevf_disable_adapter_interrupts - Disable interrupts except mailbox.
 */
static void
ixgbevf_disable_adapter_interrupts(ixgbevf_t *ixgbevf)
{
	struct ixgbe_hw *hw = &ixgbevf->hw;

	/*
	 * mask all interrupts off except the mailbox one (vector 0)
	 */
	IXGBE_WRITE_REG(hw, IXGBE_VTEIMC, 0x6);

	/*
	 * for MSI-X, also disable autoclear
	 */
	ASSERT(ixgbevf->intr_type == DDI_INTR_TYPE_MSIX);
	IXGBE_WRITE_REG(hw, IXGBE_VTEIAC, 0x1);

	IXGBE_WRITE_FLUSH(hw);
}

/*
 * ixgbevf_enable_adapter_interrupts - Enable all hardware interrupts.
 */
static void
ixgbevf_enable_adapter_interrupts(ixgbevf_t *ixgbevf)
{
	struct ixgbe_hw *hw = &ixgbevf->hw;
	uint32_t eiac, eiam;

	/* interrupt types to enable */
	ixgbevf->eims = 0x7;

	/* enable autoclear */
	eiac = ixgbevf->eims;

	eiam = 0x0;

	/* write to interrupt control registers */
	IXGBE_WRITE_REG(hw, IXGBE_VTEIMS, ixgbevf->eims);
	IXGBE_WRITE_REG(hw, IXGBE_VTEIAC, eiac);
	IXGBE_WRITE_REG(hw, IXGBE_VTEIAM, eiam);

	IXGBE_WRITE_FLUSH(hw);
}

/*
 * ixgbevf_enable_mailbox_interrupt - Enable the vf mailbox interrupt.
 */
static void
ixgbevf_enable_mailbox_interrupt(ixgbevf_t *ixgbevf)
{
	struct ixgbe_hw *hw = &ixgbevf->hw;
	uint32_t eiac, eiam;

	/* enable the mailbox interrupt (vector 0) */
	ixgbevf->eims = 0x1;

	/* enable autoclear */
	eiac = ixgbevf->eims;

	eiam = 0x0;

	/* write to interrupt control registers */
	IXGBE_WRITE_REG(hw, IXGBE_VTEIMS, ixgbevf->eims);
	IXGBE_WRITE_REG(hw, IXGBE_VTEIAC, eiac);
	IXGBE_WRITE_REG(hw, IXGBE_VTEIAM, eiam);

	IXGBE_WRITE_FLUSH(hw);
}

#pragma inline(ixgbevf_intr_rx_work)
/*
 * ixgbevf_intr_rx_work - RX processing of ISR.
 */
static void
ixgbevf_intr_rx_work(ixgbevf_rx_ring_t *rx_ring)
{
	mblk_t *mp;

	mutex_enter(&rx_ring->rx_lock);

	mp = ixgbevf_ring_rx(rx_ring, IXGBE_POLL_NULL, IXGBE_POLL_NULL);
	mutex_exit(&rx_ring->rx_lock);

	if (mp != NULL)
		mac_rx_ring(rx_ring->ixgbevf->mac_hdl, rx_ring->ring_handle, mp,
		    rx_ring->ring_gen_num);
}

#pragma inline(ixgbevf_intr_tx_work)
/*
 * ixgbevf_intr_tx_work - TX processing of ISR.
 */
static void
ixgbevf_intr_tx_work(ixgbevf_tx_ring_t *tx_ring)
{
	ixgbevf_t *ixgbevf = tx_ring->ixgbevf;

	/*
	 * Recycle the tx descriptors
	 */
	tx_ring->tx_recycle(tx_ring);

	/*
	 * Schedule the re-transmit
	 */
	if (tx_ring->reschedule &&
	    (tx_ring->tbd_free >= ixgbevf->tx_resched_thresh)) {
		tx_ring->reschedule = B_FALSE;
		mac_tx_ring_update(tx_ring->ixgbevf->mac_hdl,
		    tx_ring->ring_handle);
		IXGBE_DEBUG_STAT(tx_ring->stat_reschedule);
	}
}

/*
 * ixgbevf_intr_msix - Interrupt handler for MSI-X.
 */
static uint_t
ixgbevf_intr_msix(void *arg1, void *arg2)
{
	ixgbevf_intr_vector_t *vect = (ixgbevf_intr_vector_t *)arg1;
	ixgbevf_t *ixgbevf = vect->ixgbevf;
	int r_idx = 0;

	_NOTE(ARGUNUSED(arg2));

	if (ixgbevf->ixgbevf_state & IXGBE_STARTED) {
		/*
		 * Clean each rx ring that has its bit set in the map
		 */
		r_idx = bt_getlowbit(vect->rx_map, 0,
		    (ixgbevf->num_rx_rings - 1));
		while (r_idx >= 0) {
			ixgbevf_intr_rx_work(&ixgbevf->rx_rings[r_idx]);
			r_idx = bt_getlowbit(vect->rx_map, (r_idx + 1),
			    (ixgbevf->num_rx_rings - 1));
		}

		/*
		 * Clean each tx ring that has its bit set in the map
		 */
		r_idx = bt_getlowbit(vect->tx_map, 0,
		    (ixgbevf->num_tx_rings - 1));
		while (r_idx >= 0) {
			ixgbevf_intr_tx_work(&ixgbevf->tx_rings[r_idx]);
			r_idx = bt_getlowbit(vect->tx_map, (r_idx + 1),
			    (ixgbevf->num_tx_rings - 1));
		}
	}

	return (DDI_INTR_CLAIMED);
}

/*
 * ixgbevf_alloc_intrs - Allocate interrupts for the driver.
 * MSI-X is the only supported interrupt type
 */
static int
ixgbevf_alloc_intrs(ixgbevf_t *ixgbevf)
{
	dev_info_t *devinfo;
	int intr_types;
	int rc;

	devinfo = ixgbevf->dip;

	/*
	 * Get supported interrupt types
	 */
	rc = ddi_intr_get_supported_types(devinfo, &intr_types);

	if (rc != DDI_SUCCESS) {
		ixgbevf_log(ixgbevf,
		    "Get supported interrupt types failed: %d", rc);
		return (IXGBE_FAILURE);
	}
	IXGBE_DEBUGLOG_1(ixgbevf, "Supported interrupt types: %x", intr_types);

	ixgbevf->intr_type = 0;

	/*
	 * Install MSI-X interrupts
	 */
	if (intr_types & DDI_INTR_TYPE_MSIX) {
		rc = ixgbevf_alloc_intr_handles(ixgbevf, DDI_INTR_TYPE_MSIX);
		if (rc == IXGBE_SUCCESS)
			return (IXGBE_SUCCESS);

		ixgbevf_log(ixgbevf,
		    "Allocate MSI-X failed");
	}

	return (IXGBE_FAILURE);
}

/*
 * ixgbevf_alloc_intr_handles - Allocate interrupt handles.
 *
 * For legacy and MSI, only 1 handle is needed.  For MSI-X,
 * if fewer than 2 handles are available, return failure.
 * Upon success, this maps the vectors to rx and tx rings for
 * interrupts.
 */
static int
ixgbevf_alloc_intr_handles(ixgbevf_t *ixgbevf, int intr_type)
{
	dev_info_t *devinfo;
	int request, count, actual;
	int minimum;
	int rc;

	devinfo = ixgbevf->dip;

	switch (intr_type) {
	case DDI_INTR_TYPE_MSIX:
		/*
		 * The maximum vectors for the adapter is 3, however the Tx
		 * and Rx interrupt can be mapped only to MSI-X 0 and MSI-X 1.
		 */
		request = 2;
		if (request > ixgbevf->capab->max_msix_vect)
			request = ixgbevf->capab->max_msix_vect;
		minimum = 1;
		IXGBE_DEBUGLOG_0(ixgbevf, "interrupt type: MSI-X");
		break;

	default:
		ixgbevf_log(ixgbevf,
		    "invalid call to ixgbevf_alloc_intr_handles(): %d\n",
		    intr_type);
		return (IXGBE_FAILURE);
	}
	IXGBE_DEBUGLOG_2(ixgbevf,
	    "interrupt handles requested: %d  minimum: %d",
	    request, minimum);

	/*
	 * Get number of supported interrupts
	 */
	rc = ddi_intr_get_nintrs(devinfo, intr_type, &count);
	if ((rc != DDI_SUCCESS) || (count < minimum)) {
		ixgbevf_log(ixgbevf,
		    "Get interrupt number failed. Return: %d, count: %d",
		    rc, count);
		return (IXGBE_FAILURE);
	}
	IXGBE_DEBUGLOG_1(ixgbevf, "interrupts supported: %d", count);

	actual = 0;
	ixgbevf->intr_cnt = 0;
	ixgbevf->intr_cnt_max = 0;
	ixgbevf->intr_cnt_min = 0;

	/*
	 * Allocate an array of interrupt handles
	 */
	ixgbevf->intr_size = request * sizeof (ddi_intr_handle_t);
	ixgbevf->htable = kmem_alloc(ixgbevf->intr_size, KM_SLEEP);

	rc = ddi_intr_alloc(devinfo, ixgbevf->htable, intr_type, 0,
	    request, &actual, DDI_INTR_ALLOC_NORMAL);
	if (rc != DDI_SUCCESS) {
		ixgbevf_log(ixgbevf, "Allocate interrupts failed. "
		    "return: %d, request: %d, actual: %d",
		    rc, request, actual);
		goto alloc_handle_fail;
	}
	IXGBE_DEBUGLOG_1(ixgbevf, "interrupts actually allocated: %d", actual);

	/*
	 * upper/lower limit of interrupts
	 */
	ixgbevf->intr_cnt = actual;
	ixgbevf->intr_cnt_max = request;
	ixgbevf->intr_cnt_min = minimum;

	/*
	 * Now we know the actual number of vectors.  Here we map the vector
	 * to other, rx rings and tx ring.
	 */
	if (actual < minimum) {
		ixgbevf_log(ixgbevf,
		    "Insufficient interrupt handles available: %d", actual);
		goto alloc_handle_fail;
	}

	/*
	 * Get priority for first vector, assume remaining are all the same
	 */
	rc = ddi_intr_get_pri(ixgbevf->htable[0], &ixgbevf->intr_pri);
	if (rc != DDI_SUCCESS) {
		ixgbevf_log(ixgbevf,
		    "Get interrupt priority failed: %d", rc);
		goto alloc_handle_fail;
	}

	rc = ddi_intr_get_cap(ixgbevf->htable[0], &ixgbevf->intr_cap);
	if (rc != DDI_SUCCESS) {
		ixgbevf_log(ixgbevf,
		    "Get interrupt cap failed: %d", rc);
		goto alloc_handle_fail;
	}

	ixgbevf->intr_type = intr_type;

	return (IXGBE_SUCCESS);

alloc_handle_fail:
	ixgbevf_rem_intrs(ixgbevf);

	return (IXGBE_FAILURE);
}

/*
 * ixgbevf_add_intr_handlers Add interrupt handlers based on the interrupt type.
 *
 * Before adding the interrupt handlers, the interrupt vectors have
 * been allocated, and the rx/tx rings have also been allocated.
 */
static int
ixgbevf_add_intr_handlers(ixgbevf_t *ixgbevf)
{
	int vector = 0;
	int rc;

	switch (ixgbevf->intr_type) {
	case DDI_INTR_TYPE_MSIX:
		/*
		 * Add interrupt handler for all vectors
		 */
		for (vector = 0; vector < ixgbevf->intr_cnt; vector++) {
			/*
			 * install pointer to vect_map[vector]
			 */
			rc = ddi_intr_add_handler(ixgbevf->htable[vector],
			    (ddi_intr_handler_t *)ixgbevf_intr_msix,
			    (void *)&ixgbevf->vect_map[vector], NULL);

			if (rc != DDI_SUCCESS) {
				ixgbevf_log(ixgbevf,
				    "Add interrupt handler failed. "
				    "return: %d, vector: %d", rc, vector);
				for (vector--; vector >= 0; vector--) {
					(void) ddi_intr_remove_handler(
					    ixgbevf->htable[vector]);
				}
				return (IXGBE_FAILURE);
			}
		}

		break;

	default:
		return (IXGBE_FAILURE);
	}

	return (IXGBE_SUCCESS);
}

#pragma inline(ixgbevf_map_rxring_to_vector)
/*
 * ixgbevf_map_rxring_to_vector - Map given rx ring to given interrupt vector.
 */
static void
ixgbevf_map_rxring_to_vector(ixgbevf_t *ixgbevf, int r_idx, int v_idx)
{
	/*
	 * Set bit in map
	 */
	BT_SET(ixgbevf->vect_map[v_idx].rx_map, r_idx);

	/*
	 * Count bits set
	 */
	ixgbevf->vect_map[v_idx].rxr_cnt++;

	/*
	 * Remember bit position
	 */
	ixgbevf->rx_rings[r_idx].intr_vector = v_idx;
	ixgbevf->rx_rings[r_idx].vect_bit = 1 << v_idx;
}

#pragma inline(ixgbevf_map_txring_to_vector)
/*
 * ixgbevf_map_txring_to_vector - Map given tx ring to given interrupt vector.
 */
static void
ixgbevf_map_txring_to_vector(ixgbevf_t *ixgbevf, int t_idx, int v_idx)
{
	/*
	 * Set bit in map
	 */
	BT_SET(ixgbevf->vect_map[v_idx].tx_map, t_idx);

	/*
	 * Count bits set
	 */
	ixgbevf->vect_map[v_idx].txr_cnt++;

	/*
	 * Remember bit position
	 */
	ixgbevf->tx_rings[t_idx].intr_vector = v_idx;
	ixgbevf->tx_rings[t_idx].vect_bit = 1 << v_idx;
}

/*
 * ixgbevf_setup_ivar - Set the given entry in the given interrupt vector
 * allocation register (IVAR).
 * cause:
 *   -1 : other cause
 *    0 : rx
 *    1 : tx
 */
static void
ixgbevf_setup_ivar(ixgbevf_t *ixgbevf, uint16_t intr_alloc_entry,
    uint8_t msix_vector, int8_t cause)
{
	struct ixgbe_hw *hw = &ixgbevf->hw;
	u32 ivar, index;

	switch (hw->mac.type) {
	case ixgbe_mac_82599_vf:
		if (cause == -1) {
			/* mailbox causes */
			msix_vector |= IXGBE_IVAR_ALLOC_VAL;
			index = (intr_alloc_entry & 1) * 8;
			ivar = IXGBE_READ_REG(hw, IXGBE_VTIVAR_MISC);
			ivar &= ~(0xFF << index);
			ivar |= (msix_vector << index);
			IXGBE_WRITE_REG(hw, IXGBE_VTIVAR_MISC, ivar);
		} else {
			/* tx or rx causes */
			msix_vector |= IXGBE_IVAR_ALLOC_VAL;
			index = ((16 * (intr_alloc_entry & 1)) + (8 * cause));
			ivar = IXGBE_READ_REG(hw,
			    IXGBE_VTIVAR(intr_alloc_entry >> 1));
			ivar &= ~(0xFF << index);
			ivar |= (msix_vector << index);
			IXGBE_WRITE_REG(hw, IXGBE_VTIVAR(intr_alloc_entry >> 1),
			    ivar);
		}
		break;
	default:
		break;
	}
}

/*
 * ixgbevf_enable_ivar - Enable the given entry by setting the VAL bit of
 * given interrupt vector allocation register (IVAR).
 * cause:
 *   -1 : other cause
 *    0 : rx
 *    1 : tx
 */
static void
ixgbevf_enable_ivar(ixgbevf_t *ixgbevf, uint16_t intr_alloc_entry, int8_t cause)
{
	struct ixgbe_hw *hw = &ixgbevf->hw;
	u32 ivar, index;

	switch (hw->mac.type) {
	case ixgbe_mac_82599_vf:
		if (cause == -1) {
			/* mailbox causes */
			index = (intr_alloc_entry & 1) * 8;
			ivar = IXGBE_READ_REG(hw, IXGBE_VTIVAR_MISC);
			ivar |= (IXGBE_IVAR_ALLOC_VAL << index);
			IXGBE_WRITE_REG(hw, IXGBE_VTIVAR_MISC, ivar);
		} else {
			/* tx or rx causes */
			index = ((16 * (intr_alloc_entry & 1)) + (8 * cause));
			ivar = IXGBE_READ_REG(hw,
			    IXGBE_VTIVAR(intr_alloc_entry >> 1));
			ivar |= (IXGBE_IVAR_ALLOC_VAL << index);
			IXGBE_WRITE_REG(hw, IXGBE_VTIVAR(intr_alloc_entry >> 1),
			    ivar);
		}
		break;
	default:
		break;
	}
}

/*
 * ixgbevf_disable_ivar - Disble the given entry by clearing the VAL bit of
 * given interrupt vector allocation register (IVAR).
 * cause:
 *   -1 : other cause
 *    0 : rx
 *    1 : tx
 */
static void
ixgbevf_disable_ivar(ixgbevf_t *ixgbevf, uint16_t intr_alloc_entry,
    int8_t cause)
{
	struct ixgbe_hw *hw = &ixgbevf->hw;
	u32 ivar, index;

	switch (hw->mac.type) {
	case ixgbe_mac_82599_vf:
		if (cause == -1) {
			/* mailbox causes */
			index = (intr_alloc_entry & 1) * 8;
			ivar = IXGBE_READ_REG(hw, IXGBE_VTIVAR_MISC);
			ivar &= ~(IXGBE_IVAR_ALLOC_VAL << index);
			IXGBE_WRITE_REG(hw, IXGBE_VTIVAR_MISC, ivar);
		} else {
			/* tx or rx causes */
			index = ((16 * (intr_alloc_entry & 1)) + (8 * cause));
			ivar = IXGBE_READ_REG(hw,
			    IXGBE_VTIVAR(intr_alloc_entry >> 1));
			ivar &= ~(IXGBE_IVAR_ALLOC_VAL << index);
			IXGBE_WRITE_REG(hw, IXGBE_VTIVAR(intr_alloc_entry >> 1),
			    ivar);
		}
		break;
	default:
		break;
	}
}

/*
 * ixgbevf_map_intrs_to_vectors - Map different interrupts to MSI-X vectors.
 *
 * For MSI-X, here will map rx interrupt, tx interrupt and other interrupt
 * to vector[0 - (intr_cnt-1)].
 */
static int
ixgbevf_map_intrs_to_vectors(ixgbevf_t *ixgbevf)
{
	int i, vector = 0;

	/* initialize vector map */
	bzero(&ixgbevf->vect_map, sizeof (ixgbevf->vect_map));
	for (i = 0; i < ixgbevf->intr_cnt; i++) {
		ixgbevf->vect_map[i].ixgbevf = ixgbevf;
	}

	/*
	 * Interrupts/vectors mapping for MSI-X
	 */

	/*
	 * Map mailbox interrupt to vector 0,
	 * Set bit in map and count the bits set.
	 */
	BT_SET(ixgbevf->vect_map[vector].other_map, 0);
	ixgbevf->vect_map[vector].other_cnt++;

	/*
	 * Map tx ring interrupts to vectors
	 */
	for (i = 0; i < ixgbevf->num_tx_rings; i++) {
		ixgbevf_map_txring_to_vector(ixgbevf, i, vector);
		vector = (vector +1) % ixgbevf->intr_cnt;
	}

	/*
	 * Map rx ring interrupts to vectors
	 */
	for (i = 0; i < ixgbevf->num_rx_rings; i++) {
		ixgbevf_map_rxring_to_vector(ixgbevf, i, vector);
		vector = (vector +1) % ixgbevf->intr_cnt;
	}

	return (IXGBE_SUCCESS);
}

/*
 * ixgbevf_setup_adapter_vector - Setup the adapter interrupt vector(s).
 *
 * This relies on ring/vector mapping already set up in the
 * vect_map[] structures
 */
static void
ixgbevf_setup_adapter_vector(ixgbevf_t *ixgbevf)
{
	struct ixgbe_hw *hw = &ixgbevf->hw;
	ixgbevf_intr_vector_t *vect;	/* vector bitmap */
	int r_idx;	/* ring index */
	int v_idx;	/* vector index */
	uint32_t hw_index;

	/*
	 * Clear any previous entries
	 */
	switch (hw->mac.type) {
	case ixgbe_mac_82599_vf:
		for (v_idx = 0; v_idx < 4; v_idx++)
			IXGBE_WRITE_REG(hw, IXGBE_VTIVAR(v_idx), 0);
		IXGBE_WRITE_REG(hw, IXGBE_VTIVAR_MISC, 0);

		break;
	default:
		break;
	}

	/*
	 * For MSI-X interrupt, "Other" maps to vector[intr_cnt-1].
	 */
	ixgbevf_setup_ivar(ixgbevf, IXGBE_IVAR_OTHER_CAUSES_INDEX,
	    ixgbevf->intr_cnt -1, -1);

	/*
	 * For each interrupt vector, populate the IVAR table
	 */
	for (v_idx = 0; v_idx < ixgbevf->intr_cnt; v_idx++) {
		vect = &ixgbevf->vect_map[v_idx];

		/*
		 * For each rx ring bit set
		 */
		r_idx = bt_getlowbit(vect->rx_map, 0,
		    (ixgbevf->num_rx_rings - 1));

		while (r_idx >= 0) {
			hw_index = ixgbevf->rx_rings[r_idx].hw_index;
			ixgbevf_setup_ivar(ixgbevf, hw_index, v_idx, 0);
			r_idx = bt_getlowbit(vect->rx_map, (r_idx + 1),
			    (ixgbevf->num_rx_rings - 1));
		}

		/*
		 * For each tx ring bit set
		 */
		r_idx = bt_getlowbit(vect->tx_map, 0,
		    (ixgbevf->num_tx_rings - 1));

		while (r_idx >= 0) {
			ixgbevf_setup_ivar(ixgbevf, r_idx, v_idx, 1);
			r_idx = bt_getlowbit(vect->tx_map, (r_idx + 1),
			    (ixgbevf->num_tx_rings - 1));
		}
	}
}

/*
 * ixgbevf_rem_intr_handlers - Remove the interrupt handlers.
 */
static void
ixgbevf_rem_intr_handlers(ixgbevf_t *ixgbevf)
{
	int i;
	int rc;

	for (i = 0; i < ixgbevf->intr_cnt; i++) {
		rc = ddi_intr_remove_handler(ixgbevf->htable[i]);
		if (rc != DDI_SUCCESS) {
			IXGBE_DEBUGLOG_1(ixgbevf,
			    "Remove intr handler failed: %d", rc);
		}
	}
}

/*
 * ixgbevf_rem_intrs - Remove the allocated interrupts.
 */
static void
ixgbevf_rem_intrs(ixgbevf_t *ixgbevf)
{
	int i;
	int rc;

	for (i = 0; i < ixgbevf->intr_cnt; i++) {
		rc = ddi_intr_free(ixgbevf->htable[i]);
		if (rc != DDI_SUCCESS) {
			IXGBE_DEBUGLOG_1(ixgbevf,
			    "Free intr failed: %d", rc);
		}
	}

	kmem_free(ixgbevf->htable, ixgbevf->intr_size);
	ixgbevf->htable = NULL;
}

/*
 * ixgbevf_enable_intrs - Enable all the ddi interrupts.
 */
static int
ixgbevf_enable_intrs(ixgbevf_t *ixgbevf)
{
	int i;
	int rc;

	/*
	 * Enable interrupts
	 */
	if (ixgbevf->intr_cap & DDI_INTR_FLAG_BLOCK) {
		/*
		 * Call ddi_intr_block_enable() for MSI
		 */
		rc = ddi_intr_block_enable(ixgbevf->htable, ixgbevf->intr_cnt);
		if (rc != DDI_SUCCESS) {
			ixgbevf_log(ixgbevf,
			    "Enable block intr failed: %d", rc);
			return (IXGBE_FAILURE);
		}
	} else {
		/*
		 * Call ddi_intr_enable() for Legacy/MSI non block enable
		 */
		for (i = 0; i < ixgbevf->intr_cnt; i++) {
			rc = ddi_intr_enable(ixgbevf->htable[i]);
			if (rc != DDI_SUCCESS) {
				ixgbevf_log(ixgbevf,
				    "Enable intr failed: %d", rc);
				return (IXGBE_FAILURE);
			}
		}
	}

	return (IXGBE_SUCCESS);
}

/*
 * ixgbevf_disable_intrs - Disable all the interrupts.
 */
static int
ixgbevf_disable_intrs(ixgbevf_t *ixgbevf)
{
	int i;
	int rc;

	/*
	 * Disable all interrupts
	 */
	if (ixgbevf->intr_cap & DDI_INTR_FLAG_BLOCK) {
		rc = ddi_intr_block_disable(ixgbevf->htable, ixgbevf->intr_cnt);
		if (rc != DDI_SUCCESS) {
			ixgbevf_log(ixgbevf,
			    "Disable block intr failed: %d", rc);
			return (IXGBE_FAILURE);
		}
	} else {
		for (i = 0; i < ixgbevf->intr_cnt; i++) {
			rc = ddi_intr_disable(ixgbevf->htable[i]);
			if (rc != DDI_SUCCESS) {
				ixgbevf_log(ixgbevf,
				    "Disable intr failed: %d", rc);
				return (IXGBE_FAILURE);
			}
		}
	}

	return (IXGBE_SUCCESS);
}

/*
 * ixgbevf_get_hw_state - Get and save parameters related to adapter hardware.
 */
static void
ixgbevf_get_hw_state(ixgbevf_t *ixgbevf)
{
	struct ixgbe_hw *hw = &ixgbevf->hw;
	ixgbe_link_speed speed = IXGBE_LINK_SPEED_UNKNOWN;
	boolean_t link_up = B_FALSE;

	ASSERT(mutex_owned(&ixgbevf->gen_lock));
	ixgbevf->param_lp_1000fdx_cap = 0;
	ixgbevf->param_lp_100fdx_cap  = 0;

	/* check for link, don't wait */
	(void) hw->mac.ops.check_link(hw, &speed, &link_up, false);
	if (link_up) {
		ixgbevf->param_lp_1000fdx_cap = 1;
		ixgbevf->param_lp_100fdx_cap = 0;
	}

	ixgbevf->param_adv_1000fdx_cap = 1;
	ixgbevf->param_adv_100fdx_cap = 0;
}

/*
 * ixgbevf_atomic_reserve - Atomic decrease operation.
 */
int
ixgbevf_atomic_reserve(uint32_t *count_p, uint32_t n)
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
 * ixgbevf_mc_table_itr - Traverse the entries in the multicast table.
 */
static uint8_t *
ixgbevf_mc_table_itr(struct ixgbe_hw *hw, uint8_t **upd_ptr, uint32_t *vmdq)
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
ixgbevf_check_acc_handle(ddi_acc_handle_t handle)
{
	ddi_fm_error_t de;

	ddi_fm_acc_err_get(handle, &de, DDI_FME_VERSION);
	ddi_fm_acc_err_clear(handle, DDI_FME_VERSION);
	return (de.fme_status);
}

int
ixgbevf_check_dma_handle(ddi_dma_handle_t handle)
{
	ddi_fm_error_t de;

	ddi_fm_dma_err_get(handle, &de, DDI_FME_VERSION);
	return (de.fme_status);
}

/*
 * ixgbevf_fm_error_cb - The IO fault service error handling callback function.
 */
static int
ixgbevf_fm_error_cb(dev_info_t *dip, ddi_fm_error_t *err, const void *impl_data)
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
ixgbevf_fm_init(ixgbevf_t *ixgbevf)
{
	ddi_iblock_cookie_t iblk;
	int fma_dma_flag;

	/*
	 * Only register with IO Fault Services if we have some capability
	 */
	if (ixgbevf->fm_capabilities & DDI_FM_ACCCHK_CAPABLE) {
		ixgbevf_regs_acc_attr.devacc_attr_access = DDI_FLAGERR_ACC;
	} else {
		ixgbevf_regs_acc_attr.devacc_attr_access = DDI_DEFAULT_ACC;
	}

	if (ixgbevf->fm_capabilities & DDI_FM_DMACHK_CAPABLE) {
		fma_dma_flag = 1;
	} else {
		fma_dma_flag = 0;
	}

	ixgbevf_set_fma_flags(fma_dma_flag);

	if (ixgbevf->fm_capabilities) {

		/*
		 * Register capabilities with IO Fault Services
		 */
		ddi_fm_init(ixgbevf->dip, &ixgbevf->fm_capabilities, &iblk);

		/*
		 * Initialize pci ereport capabilities if ereport capable
		 */
		if (DDI_FM_EREPORT_CAP(ixgbevf->fm_capabilities) ||
		    DDI_FM_ERRCB_CAP(ixgbevf->fm_capabilities))
			pci_ereport_setup(ixgbevf->dip);

		/*
		 * Register error callback if error callback capable
		 */
		if (DDI_FM_ERRCB_CAP(ixgbevf->fm_capabilities))
			ddi_fm_handler_register(ixgbevf->dip,
			    ixgbevf_fm_error_cb, (void*) ixgbevf);
	}
}

static void
ixgbevf_fm_fini(ixgbevf_t *ixgbevf)
{
	/*
	 * Only unregister FMA capabilities if they are registered
	 */
	if (ixgbevf->fm_capabilities) {

		/*
		 * Release any resources allocated by pci_ereport_setup()
		 */
		if (DDI_FM_EREPORT_CAP(ixgbevf->fm_capabilities) ||
		    DDI_FM_ERRCB_CAP(ixgbevf->fm_capabilities))
			pci_ereport_teardown(ixgbevf->dip);

		/*
		 * Un-register error callback if error callback capable
		 */
		if (DDI_FM_ERRCB_CAP(ixgbevf->fm_capabilities))
			ddi_fm_handler_unregister(ixgbevf->dip);

		/*
		 * Unregister from IO Fault Service
		 */
		ddi_fm_fini(ixgbevf->dip);
	}
}

void
ixgbevf_fm_ereport(ixgbevf_t *ixgbevf, char *detail)
{
	uint64_t ena;
	char buf[FM_MAX_CLASS];

	(void) snprintf(buf, FM_MAX_CLASS, "%s.%s", DDI_FM_DEVICE, detail);
	ena = fm_ena_generate(0, FM_ENA_FMT1);
	if (DDI_FM_EREPORT_CAP(ixgbevf->fm_capabilities)) {
		ddi_fm_ereport_post(ixgbevf->dip, buf, ena, DDI_NOSLEEP,
		    FM_VERSION, DATA_TYPE_UINT8, FM_EREPORT_VERS0, NULL);
	}
}

static int
ixgbevf_ring_start(mac_ring_driver_t rh, uint64_t mr_gen_num)
{
	ixgbevf_rx_ring_t *rx_ring = (ixgbevf_rx_ring_t *)rh;

	mutex_enter(&rx_ring->rx_lock);
	rx_ring->ring_gen_num = mr_gen_num;
	mutex_exit(&rx_ring->rx_lock);
	return (0);
}

/*
 * Get the global ring index by a ring index within a group.
 */
static int
ixgbevf_get_rx_ring_index(ixgbevf_t *ixgbevf, int gindex, int rindex)
{
	ixgbevf_rx_ring_t *rx_ring;
	int i;

	for (i = 0; i < ixgbevf->num_rx_rings; i++) {
		rx_ring = &ixgbevf->rx_rings[i];
		if (rx_ring->group_index == gindex)
			rindex--;
		if (rindex < 0)
			return (i);
	}

	return (-1);
}

/*
 * Callback funtion for MAC layer to register all rings.
 */
/* ARGSUSED */
void
ixgbevf_fill_ring(void *arg, mac_ring_type_t rtype, const int group_index,
    const int ring_index, mac_ring_info_t *infop, mac_ring_handle_t rh)
{
	ixgbevf_t *ixgbevf = (ixgbevf_t *)arg;
	mac_intr_t *mintr = &infop->mri_intr;

	switch (rtype) {
	case MAC_RING_TYPE_RX: {
		/*
		 * 'index' is the ring index within the group.
		 * Need to get the global ring index by searching in groups.
		 */
		int global_ring_index = ixgbevf_get_rx_ring_index(
		    ixgbevf, group_index, ring_index);

		ASSERT(global_ring_index >= 0);

		ixgbevf_rx_ring_t *rx_ring =
		    &ixgbevf->rx_rings[global_ring_index];
		rx_ring->ring_handle = rh;

		infop->mri_driver = (mac_ring_driver_t)rx_ring;
		infop->mri_start = ixgbevf_ring_start;
		infop->mri_stop = NULL;
		infop->mri_poll = ixgbevf_ring_rx_poll;
		infop->mri_stat = ixgbevf_rx_ring_stat;

		mintr->mi_enable = ixgbevf_rx_ring_intr_enable;
		mintr->mi_disable = ixgbevf_rx_ring_intr_disable;
		if (ixgbevf->intr_type &
		    (DDI_INTR_TYPE_MSIX | DDI_INTR_TYPE_MSI)) {
			mintr->mi_ddi_handle =
			    ixgbevf->htable[rx_ring->intr_vector];
		}

		break;
	}
	case MAC_RING_TYPE_TX: {
		ASSERT(group_index >= 0);
		ASSERT(ring_index < ixgbevf->num_tx_rings);

		ixgbevf_tx_ring_t *tx_ring = &ixgbevf->tx_rings[ring_index];
		tx_ring->ring_handle = rh;

		infop->mri_driver = (mac_ring_driver_t)tx_ring;
		infop->mri_start = NULL;
		infop->mri_stop = NULL;
		infop->mri_tx = ixgbevf_ring_tx;
		infop->mri_stat = ixgbevf_tx_ring_stat;
		if (ixgbevf->intr_type &
		    (DDI_INTR_TYPE_MSIX | DDI_INTR_TYPE_MSI)) {
			mintr->mi_ddi_handle =
			    ixgbevf->htable[tx_ring->intr_vector];
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
ixgbevf_fill_group(void *arg, mac_ring_type_t rtype, const int index,
    mac_group_info_t *infop, mac_group_handle_t gh)
{
	ixgbevf_t *ixgbevf = (ixgbevf_t *)arg;

	switch (rtype) {
	case MAC_RING_TYPE_RX: {
		ixgbevf_group_t *rx_group;

		rx_group = &ixgbevf->rx_groups[index];
		rx_group->group_handle = gh;

		infop->mgi_driver = (mac_group_driver_t)rx_group;
		infop->mgi_start = NULL;
		infop->mgi_stop = NULL;
		infop->mgi_addmac = ixgbevf_addmac;
		infop->mgi_remmac = ixgbevf_remmac;
		infop->mgi_count =
		    (ixgbevf->num_rx_rings / ixgbevf->num_groups);
		if (index == 0)
			infop->mgi_flags = MAC_GROUP_DEFAULT;

		break;
	}
	case MAC_RING_TYPE_TX: {
		ixgbevf_group_t *tx_group;

		tx_group = &ixgbevf->tx_groups[index];
		tx_group->group_handle = gh;

		infop->mgi_driver = (mac_group_driver_t)tx_group;
		infop->mgi_count =
		    (ixgbevf->num_tx_rings / ixgbevf->num_groups);
		if (index == 0)
			infop->mgi_flags = MAC_GROUP_DEFAULT;

		break;
	}
	default:
		break;
	}
}

/*
 * Enable interrupt on the specificed rx ring.
 */
int
ixgbevf_rx_ring_intr_enable(mac_ring_driver_t rh)
{
	ixgbevf_rx_ring_t *rx_ring = (ixgbevf_rx_ring_t *)rh;
	ixgbevf_t *ixgbevf = rx_ring->ixgbevf;
	int r_idx = rx_ring->index;
	int hw_r_idx = rx_ring->hw_index;
	int v_idx = rx_ring->intr_vector;

	mutex_enter(&ixgbevf->gen_lock);
	if (ixgbevf->ixgbevf_state & IXGBE_INTR_ADJUST) {
		mutex_exit(&ixgbevf->gen_lock);
		/*
		 * Simply return 0.
		 * Interrupts are being adjusted. ixgbevf_intr_adjust()
		 * will eventually re-enable the interrupt when it's
		 * done with the adjustment.
		 */
		return (0);
	}

	/*
	 * To enable interrupt by setting the VAL bit of given interrupt
	 * vector allocation register (IVAR).
	 */
	ixgbevf_enable_ivar(ixgbevf, hw_r_idx, 0);

	BT_SET(ixgbevf->vect_map[v_idx].rx_map, r_idx);

	/*
	 * Trigger a Rx interrupt on this ring
	 */
	IXGBE_WRITE_REG(&ixgbevf->hw, IXGBE_VTEICS, (1 << v_idx));
	IXGBE_WRITE_FLUSH(&ixgbevf->hw);

	mutex_exit(&ixgbevf->gen_lock);

	return (0);
}

/*
 * Disable interrupt on the specificed rx ring.
 */
int
ixgbevf_rx_ring_intr_disable(mac_ring_driver_t rh)
{
	ixgbevf_rx_ring_t *rx_ring = (ixgbevf_rx_ring_t *)rh;
	ixgbevf_t *ixgbevf = rx_ring->ixgbevf;
	int r_idx = rx_ring->index;
	int hw_r_idx = rx_ring->hw_index;
	int v_idx = rx_ring->intr_vector;

	mutex_enter(&ixgbevf->gen_lock);
	if (ixgbevf->ixgbevf_state & IXGBE_INTR_ADJUST) {
		mutex_exit(&ixgbevf->gen_lock);
		/*
		 * Simply return 0.
		 * In the rare case where an interrupt is being
		 * disabled while interrupts are being adjusted,
		 * we don't fail the operation. No interrupts will
		 * be generated while they are adjusted, and
		 * ixgbevf_intr_adjust() will cause the interrupts
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
	ixgbevf_disable_ivar(ixgbevf, hw_r_idx, 0);

	BT_CLEAR(ixgbevf->vect_map[v_idx].rx_map, r_idx);

	mutex_exit(&ixgbevf->gen_lock);

	return (0);
}

/*
 * Add a mac address.
 */
static int
ixgbevf_addmac(void *arg, const uint8_t *mac_addr, uint64_t flags)
{
	_NOTE(ARGUNUSED(flags))

	ixgbevf_group_t *rx_group = (ixgbevf_group_t *)arg;
	ixgbevf_t *ixgbevf = rx_group->ixgbevf;
	int slot, i;

	IXGBE_DEBUGLOG_0(ixgbevf, "ixgbevf_addmac: The MAC address in VF is:");
	for (int i = 0; i < 6; i ++)
		IXGBE_DEBUGLOG_2(ixgbevf, "%d: 0x%x", i, mac_addr[i]);

	mutex_enter(&ixgbevf->gen_lock);

	if (ixgbevf->ixgbevf_state & IXGBE_SUSPENDED) {
		mutex_exit(&ixgbevf->gen_lock);
		return (ECANCELED);
	}


	/*
	 * The first ixgbevf->num_groups slots are reserved for each
	 * respective group. The rest slots are shared by all groups. While
	 * adding a MAC address, reserved slots are firstly checked then the
	 * shared slots are searched.
	 */
	slot = -1;
	if (ixgbevf->unicst_addr[rx_group->index].mac.set == 1) {
		for (i = ixgbevf->num_groups; i < ixgbevf->unicst_total;
		    i++) {
			if (ixgbevf->unicst_addr[i].mac.set == 0) {
				slot = i;
				break;
			}
		}
	} else {
		slot = rx_group->index;
	}

	if (slot == -1) {
		/* no slots available */
		mutex_exit(&ixgbevf->gen_lock);
		return (ENOSPC);
	}

	bcopy(mac_addr, ixgbevf->unicst_addr[slot].mac.addr, ETHERADDRL);
	if (ixgbevf_enable_mac_addr(ixgbevf, mac_addr) != IXGBE_SUCCESS) {
		mutex_exit(&ixgbevf->gen_lock);
		IXGBE_DEBUGLOG_0(ixgbevf,
		    "ixgbevf_addmac: failed to enable address");
		return (EIO);
	}
	ixgbevf->unicst_addr[slot].mac.set = 1;
	ixgbevf->unicst_addr[slot].mac.group_index = rx_group->index;

	mutex_exit(&ixgbevf->gen_lock);

	return (0);
}

/*
 * Remove a mac address.
 */
static int
ixgbevf_remmac(void *arg, const uint8_t *mac_addr)
{
	ixgbevf_group_t *rx_group = (ixgbevf_group_t *)arg;
	ixgbevf_t *ixgbevf = rx_group->ixgbevf;
	int slot;

	mutex_enter(&ixgbevf->gen_lock);

	if (ixgbevf->ixgbevf_state & IXGBE_SUSPENDED) {
		mutex_exit(&ixgbevf->gen_lock);
		return (ECANCELED);
	}

	slot = ixgbevf_unicst_find(ixgbevf, mac_addr);
	if (slot == -1) {
		mutex_exit(&ixgbevf->gen_lock);
		return (EINVAL);
	}

	if (ixgbevf->unicst_addr[slot].mac.set == 0) {
		mutex_exit(&ixgbevf->gen_lock);
		return (EINVAL);
	}

	bzero(ixgbevf->unicst_addr[slot].mac.addr, ETHERADDRL);
	if (ixgbevf_disable_mac_addr(ixgbevf, mac_addr) != IXGBE_SUCCESS) {
		mutex_exit(&ixgbevf->gen_lock);
		IXGBE_DEBUGLOG_0(ixgbevf,
		    "ixgbevf_remmac: failed to disable the address");
		return (EIO);
	}
	ixgbevf->unicst_addr[slot].mac.set = 0;

	mutex_exit(&ixgbevf->gen_lock);

	return (0);
}

#ifdef DEBUG_INTEL
extern int gld_getinfo(dev_info_t *, ddi_info_cmd_t, void *, void **);

/*
 * export device registers to an mmap() call; used for ethregs support
 */
static
int ixgbevf_devmap(dev_t dev, devmap_cookie_t dhp, offset_t off,
    size_t len, size_t *maplen, uint_t model)
{
	dev_info_t *dip = GlobalDip;
	ixgbevf_t *ixgbevf = ddi_get_driver_private(dip);
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
		ixgbevf_log(ixgbevf, "regsize test fail\n");
		return (DDI_FAILURE);
	}

	/* set up the device mapping */
	status = devmap_devmem_setup(dhp, dip, NULL, regnum, off, lenpage,
	    PROT_ALL, DEVMAP_DEFAULTS, &ixgbevf_regs_acc_attr);
	if (status == DDI_SUCCESS) {
		/* acknowledge the entire range */
		*maplen = lenpage;
	} else {
		cmn_err(CE_WARN, "devmap_devmem_setup fail: 0x%x\n", status);
	}

	return (status);
}
#endif	/* DEBUG_INTEL */
