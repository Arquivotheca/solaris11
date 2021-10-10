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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include "igbvf_sw.h"

static char igbvf_ident[] = "Intel 1Gb VF Ethernet";
static char igbvf_version[] = "igbvf 1.1.1";

/*
 * Local function protoypes
 */
static int igbvf_register_mac(igbvf_t *);
static int igbvf_identify_hardware(igbvf_t *);
static int igbvf_regs_map(igbvf_t *);
static void igbvf_init_properties(igbvf_t *);
static int igbvf_init_driver_settings(igbvf_t *);
static void igbvf_init_locks(igbvf_t *);
static void igbvf_destroy_locks(igbvf_t *);
static int igbvf_init_default_mac_address(igbvf_t *);
static int igbvf_init_promisc(igbvf_t *);
static int igbvf_init(igbvf_t *);
static int igbvf_init_adapter(igbvf_t *);
static void igbvf_stop_adapter(igbvf_t *);
static void igbvf_reset(void *);
static void igbvf_tx_clean(igbvf_t *);
static boolean_t igbvf_tx_drain(igbvf_t *);
static boolean_t igbvf_rx_drain(igbvf_t *);
static int igbvf_alloc_ring_group(igbvf_t *);
static int igbvf_alloc_tx_mem(igbvf_t *);
static void igbvf_free_tx_mem(igbvf_t *);
static int igbvf_alloc_rx_data(igbvf_t *);
static void igbvf_free_rx_data(igbvf_t *);
static void igbvf_free_ring_group(igbvf_t *);
static void igbvf_init_ring_group(igbvf_t *);
static void igbvf_setup_rings(igbvf_t *);
static void igbvf_setup_rx(igbvf_t *);
static void igbvf_setup_tx(igbvf_t *);
static void igbvf_setup_rx_ring(igbvf_rx_ring_t *);
static void igbvf_setup_tx_ring(igbvf_tx_ring_t *);
static void igbvf_disable_rx_ring(igbvf_rx_ring_t *);
static void igbvf_disable_tx_ring(igbvf_tx_ring_t *);
static int igbvf_unicst_init(igbvf_t *);
static int igbvf_unicst_find(igbvf_t *, const uint8_t *);
static void igbvf_release_multicast(igbvf_t *);
static void igbvf_get_conf(igbvf_t *);
static int igbvf_get_prop(igbvf_t *, char *, int, int, int);
static boolean_t igbvf_is_link_up(igbvf_t *);
static void igbvf_link_check(void *);
static void igbvf_local_timer(void *);
static void igbvf_arm_watchdog_timer(igbvf_t *);
static void igbvf_restart_watchdog_timer(igbvf_t *);
static void igbvf_disable_adapter_interrupts(igbvf_t *);
static void igbvf_enable_adapter_interrupts_82576(igbvf_t *);
static void igbvf_enable_mailbox_interrupt(igbvf_t *);
static boolean_t is_valid_mac_addr(uint8_t *);
static boolean_t igbvf_stall_check(igbvf_t *);
static int igbvf_alloc_intrs(igbvf_t *);
static int igbvf_alloc_intr_handles(igbvf_t *, int);
static int igbvf_add_intr_handlers(igbvf_t *);
static void igbvf_rem_intr_handlers(igbvf_t *);
static void igbvf_rem_intrs(igbvf_t *);
static int igbvf_enable_intrs(igbvf_t *);
static int igbvf_disable_intrs(igbvf_t *);
static void igbvf_setup_msix_82576(igbvf_t *);
static uint_t igbvf_intr_rx(void *, void *);
static uint_t igbvf_intr_tx(void *, void *);
static uint_t igbvf_intr_other(void *, void *);
static uint_t igbvf_intr_tx_other(void *, void *);
static void igbvf_intr_rx_work(igbvf_rx_ring_t *);
static void igbvf_intr_tx_work(igbvf_tx_ring_t *);

static int igbvf_attach(dev_info_t *, ddi_attach_cmd_t);
static int igbvf_detach(dev_info_t *, ddi_detach_cmd_t);
static int igbvf_resume(igbvf_t *, uint_t, uint_t);
static int igbvf_suspend(igbvf_t *, uint_t, uint_t);
static int igbvf_cbfunc(dev_info_t *, ddi_cb_action_t, void *, void *, void *);
static int igbvf_quiesce(dev_info_t *);
static int igbvf_unconfigure(dev_info_t *, igbvf_t *);
static int igbvf_fm_error_cb(dev_info_t *, ddi_fm_error_t *,
    const void *);
static void igbvf_fm_init(igbvf_t *);
static void igbvf_fm_fini(igbvf_t *);

static int igbvf_init_msg_api(igbvf_t *);
static int igbvf_nego_msg_api(igbvf_t *);
static int igbvf_get_mac_addrs(igbvf_t *);
static int igbvf_get_que_limits(igbvf_t *);
static int igbvf_get_mtu_limits(igbvf_t *);
static int igbvf_set_mtu(igbvf_t *, uint32_t);
static int igbvf_enable_mac_addr(igbvf_t *, const uint8_t *);
static int igbvf_disable_mac_addr(igbvf_t *, const uint8_t *);
static int igbvf_set_multicast_addrs(igbvf_t *, const uint8_t *, uint32_t);
static void igbvf_rcv_msg(void *);

static int igbvf_reset_hw_vf(igbvf_t *);
static int igbvf_poll_for_msg(igbvf_t *);
static int igbvf_poll_for_ack(igbvf_t *);
static int igbvf_read_posted_mbx(igbvf_t *, uint32_t *, uint16_t);
static int igbvf_write_posted_mbx(igbvf_t *, uint32_t *, uint16_t);
static int igbvf_obtain_mbx_lock_vf(igbvf_t *);
static int igbvf_write_mbx_vf(igbvf_t *, uint32_t *, uint16_t);
static int igbvf_read_mbx_vf(igbvf_t *, uint32_t *, uint16_t);
static int igbvf_check_for_link_vf(igbvf_t *);

char *igbvf_priv_props[] = {
	"_tx_copy_thresh",
	"_tx_recycle_thresh",
	"_tx_overload_thresh",
	"_tx_resched_thresh",
	"_rx_copy_thresh",
	"_rx_limit_per_intr",
	"_intr_throttling",
	NULL
};

static struct cb_ops igbvf_cb_ops = {
	nulldev,		/* cb_open */
	nulldev,		/* cb_close */
	nodev,			/* cb_strategy */
	nodev,			/* cb_print */
	nodev,			/* cb_dump */
	nodev,			/* cb_read */
	nodev,			/* cb_write */
	nodev,			/* cb_ioctl */
	nodev,			/* cb_devmap */
	nodev,			/* cb_mmap */
	nodev,			/* cb_segmap */
	nochpoll,		/* cb_chpoll */
	ddi_prop_op,		/* cb_prop_op */
	NULL,			/* cb_stream */
	D_MP | D_HOTPLUG,	/* cb_flag */
	CB_REV,			/* cb_rev */
	nodev,			/* cb_aread */
	nodev			/* cb_awrite */
};

static struct dev_ops igbvf_dev_ops = {
	DEVO_REV,		/* devo_rev */
	0,			/* devo_refcnt */
	NULL,			/* devo_getinfo */
	nulldev,		/* devo_identify */
	nulldev,		/* devo_probe */
	igbvf_attach,		/* devo_attach */
	igbvf_detach,		/* devo_detach */
	nodev,			/* devo_reset */
	&igbvf_cb_ops,		/* devo_cb_ops */
	NULL,			/* devo_bus_ops */
	nulldev,		/* devo_power */
	igbvf_quiesce		/* devo_quiesce */
};

static struct modldrv igbvf_modldrv = {
	&mod_driverops,		/* Type of module.  This one is a driver */
	igbvf_ident,		/* Discription string */
	&igbvf_dev_ops,		/* driver ops */
};

static struct modlinkage igbvf_modlinkage = {
	MODREV_1, &igbvf_modldrv, NULL
};

/* Access attributes for register mapping */
ddi_device_acc_attr_t igbvf_regs_acc_attr = {
	DDI_DEVICE_ATTR_V1,
	DDI_STRUCTURE_LE_ACC,
	DDI_STRICTORDER_ACC,
	DDI_FLAGERR_ACC
};

#define	IGBVF_M_CALLBACK_FLAGS	\
	(MC_IOCTL | MC_GETCAPAB | MC_SETPROP | MC_GETPROP | MC_PROPINFO)

static mac_callbacks_t igbvf_m_callbacks = {
	IGBVF_M_CALLBACK_FLAGS,
	igbvf_m_stat,
	igbvf_m_start,
	igbvf_m_stop,
	igbvf_m_promisc,
	igbvf_m_multicst,
	NULL,
	NULL,
	NULL,
	igbvf_m_ioctl,
	igbvf_m_getcapab,
	NULL,
	NULL,
	igbvf_m_setprop,
	igbvf_m_getprop,
	igbvf_m_propinfo
};

/*
 * Initialize capabilities of each supported adapter type
 */
static adapter_info_t igbvf_82576vf_cap = {
	/* limits */
	1,		/* maximum number of rx queues */
	1,		/* minimum number of rx queues */
	1,		/* default number of rx queues */

	1,		/* maximum number of rx groups */
	1,		/* minimum number of rx groups */
	1,		/* default number of rx groups */

	1,		/* maximum number of tx queues */
	1,		/* minimum number of tx queues */
	1,		/* default number of tx queues */

	65535,		/* maximum interrupt throttle rate */
	0,		/* minimum interrupt throttle rate */
	200,		/* default interrupt throttle rate */

	(MAX_NUM_UNICAST_ADDRESSES - 1),
			/* size unicast receive address table */
	3,		/* size of interrupt vector table */
	0,		/* maximum VFs supported */

	/* function pointers */
	igbvf_enable_adapter_interrupts_82576,
	igbvf_setup_msix_82576,

	/* capabilities */
	0,

	0xfff00000		/* mask for RXDCTL register */
};

/*
 * Module Initialization Functions
 */

int
_init(void)
{
	int status;

	mac_init_ops(&igbvf_dev_ops, MODULE_NAME);

	status = mod_install(&igbvf_modlinkage);

	if (status != DDI_SUCCESS) {
		mac_fini_ops(&igbvf_dev_ops);
	}

	return (status);
}

int
_fini(void)
{
	int status;

	status = mod_remove(&igbvf_modlinkage);

	if (status == DDI_SUCCESS) {
		mac_fini_ops(&igbvf_dev_ops);
	}

	return (status);

}

int
_info(struct modinfo *modinfop)
{
	int status;

	status = mod_info(&igbvf_modlinkage, modinfop);

	return (status);
}

/*
 * igbvf_attach - driver attach
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
igbvf_attach(dev_info_t *devinfo, ddi_attach_cmd_t cmd)
{
	igbvf_t *igbvf;
	struct igbvf_osdep *osdep;
	struct e1000_hw *hw;
	char taskq_name[32];
	int instance;
	int ret;

	/*
	 * Check the command and perform corresponding operations
	 */
	switch (cmd) {
	default:
		return (DDI_FAILURE);

	case DDI_RESUME:
		igbvf = (igbvf_t *)ddi_get_driver_private(devinfo);
		if (igbvf == NULL)
			return (DDI_FAILURE);

		(void) igbvf_resume(igbvf, IGBVF_DDI_SR_ACTIVITIES,
		    IGBVF_DDI_SR_IMPACTS);

		/*
		 * Always return success, as cpr does not allow device
		 * resume failure
		 */
		return (DDI_SUCCESS);

	case DDI_ATTACH:
		break;
	}

	/* Get the device instance */
	instance = ddi_get_instance(devinfo);

	/* Allocate memory for the instance data structure */
	igbvf = kmem_zalloc(sizeof (igbvf_t), KM_SLEEP);

	igbvf->dip = devinfo;
	igbvf->instance = instance;

	hw = &igbvf->hw;
	osdep = &igbvf->osdep;
	hw->back = osdep;
	osdep->igbvf = igbvf;

	/* Attach the instance pointer to the dev_info data structure */
	ddi_set_driver_private(devinfo, igbvf);

	/* Initialize for fma support */
	igbvf->fm_capabilities = igbvf_get_prop(igbvf, "fm-capable",
	    0, 0x0f,
	    DDI_FM_EREPORT_CAPABLE | DDI_FM_ACCCHK_CAPABLE |
	    DDI_FM_DMACHK_CAPABLE | DDI_FM_ERRCB_CAPABLE);
	igbvf_fm_init(igbvf);
	igbvf->attach_progress |= ATTACH_PROGRESS_FMINIT;

	/*
	 * Map PCI config space registers
	 */
	if (pci_config_setup(devinfo, &osdep->cfg_handle) != DDI_SUCCESS) {
		igbvf_error(igbvf, "Failed to map PCI configurations");
		goto attach_fail;
	}
	igbvf->attach_progress |= ATTACH_PROGRESS_PCI_CONFIG;

	/*
	 * Identify the adapter family
	 */
	if (igbvf_identify_hardware(igbvf) != IGBVF_SUCCESS) {
		igbvf_error(igbvf, "Failed to identify hardware");
		goto attach_fail;
	}

	/*
	 * Map device registers
	 */
	if (igbvf_regs_map(igbvf) != IGBVF_SUCCESS) {
		igbvf_error(igbvf, "Failed to map device registers");
		goto attach_fail;
	}
	igbvf->attach_progress |= ATTACH_PROGRESS_REGS_MAP;

	/*
	 * Allocate interrupts
	 */
	if (igbvf_alloc_intrs(igbvf) != IGBVF_SUCCESS) {
		igbvf_error(igbvf, "Failed to allocate interrupts");
		goto attach_fail;
	}
	igbvf->attach_progress |= ATTACH_PROGRESS_ALLOC_INTR;

	/*
	 * Initialize mutexes for this device.
	 * Do this before enabling the interrupt handler and
	 * register the softint to avoid the condition where
	 * interrupt handler can try using uninitialized mutex
	 */
	igbvf_init_locks(igbvf);
	igbvf->attach_progress |= ATTACH_PROGRESS_LOCKS;

	/*
	 * Initialize the adapter and the mailbox message API
	 */
	if (igbvf_init_msg_api(igbvf) != IGBVF_SUCCESS) {
		igbvf_error(igbvf, "Failed to initialize message API");
		goto attach_fail;
	}

	/*
	 * Initialize driver parameters
	 */
	igbvf_init_properties(igbvf);
	igbvf->attach_progress |= ATTACH_PROGRESS_PROPS;

	/*
	 * Allocate rx/tx rings based on the ring numbers.
	 * The actual numbers of rx/tx rings are decided by the number of
	 * allocated interrupt vectors, so we should allocate the rings after
	 * interrupts are allocated.
	 */
	if (igbvf_alloc_ring_group(igbvf) != IGBVF_SUCCESS) {
		igbvf_error(igbvf, "Failed to allocate rx/tx rings or groups");
		goto attach_fail;
	}
	igbvf->attach_progress |= ATTACH_PROGRESS_ALLOC_RINGS;

	/*
	 * Add interrupt handlers
	 */
	if (igbvf_add_intr_handlers(igbvf) != IGBVF_SUCCESS) {
		igbvf_error(igbvf, "Failed to add interrupt handlers");
		goto attach_fail;
	}
	igbvf->attach_progress |= ATTACH_PROGRESS_ADD_INTR;

	/*
	 * Create a taskq for mailbox interrupt processing
	 */
	(void) sprintf(taskq_name,
	    "%s%d_mailbox_taskq", MODULE_NAME, instance);
	if ((igbvf->mbx_taskq = ddi_taskq_create(devinfo, taskq_name,
	    1, TASKQ_DEFAULTPRI, 0)) == NULL) {
		igbvf_error(igbvf, "Failed to create mailbox taskq");
		goto attach_fail;
	}
	igbvf->attach_progress |= ATTACH_PROGRESS_MBX_TASKQ;

	/*
	 * Create a taskq for local timer event
	 */
	(void) sprintf(taskq_name,
	    "%s%d_timer_taskq", MODULE_NAME, instance);
	if ((igbvf->timer_taskq = ddi_taskq_create(devinfo, taskq_name,
	    1, TASKQ_DEFAULTPRI, 0)) == NULL) {
		igbvf_error(igbvf, "Failed to create link check taskq");
		goto attach_fail;
	}
	igbvf->attach_progress |= ATTACH_PROGRESS_TIMER_TASKQ;

	/*
	 * Initialize driver parameters
	 */
	if (igbvf_init_driver_settings(igbvf) != IGBVF_SUCCESS) {
		igbvf_error(igbvf, "Failed to initialize driver settings");
		goto attach_fail;
	}

	if (igbvf_check_acc_handle(igbvf->osdep.cfg_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(igbvf->dip, DDI_SERVICE_LOST);
		goto attach_fail;
	}

	/*
	 * Initialize the adapter and setup the rx/tx rings
	 */
	if (igbvf_init(igbvf) != IGBVF_SUCCESS) {
		igbvf_error(igbvf, "Failed to initialize adapter");
		goto attach_fail;
	}
	igbvf->attach_progress |= ATTACH_PROGRESS_INIT_ADAPTER;

	/*
	 * Initialize statistics
	 */
	if (igbvf_init_stats(igbvf) != IGBVF_SUCCESS) {
		igbvf_error(igbvf, "Failed to initialize statistics");
		goto attach_fail;
	}
	igbvf->attach_progress |= ATTACH_PROGRESS_STATS;

	/*
	 * Register the callback function
	 */
	ret = ddi_cb_register(igbvf->dip, DDI_CB_FLAG_LSR,
	    &igbvf_cbfunc, (void *)igbvf, NULL, &igbvf->cb_hdl);

	if (ret != DDI_SUCCESS) {
		igbvf_error(igbvf,
		    "Failed to register callback for LSR, "
		    "error status: %d", ret);
		goto attach_fail;
	}
	igbvf->attach_progress |= ATTACH_PROGRESS_CB;

	/*
	 * Register the driver to the MAC
	 */
	if (igbvf_register_mac(igbvf) != IGBVF_SUCCESS) {
		igbvf_error(igbvf, "Failed to register MAC");
		goto attach_fail;
	}
	igbvf->attach_progress |= ATTACH_PROGRESS_MAC;

	/*
	 * Now that mutex locks are initialized, and the chip is also
	 * initialized, enable interrupts.
	 */
	if (igbvf_enable_intrs(igbvf) != IGBVF_SUCCESS) {
		igbvf_error(igbvf, "Failed to enable DDI interrupts");
		goto attach_fail;
	}
	igbvf->attach_progress |= ATTACH_PROGRESS_ENABLE_INTR;

	/*
	 * Enable and start the watchdog timer
	 */
	igbvf_enable_watchdog_timer(igbvf);

	igbvf_log(igbvf, "%s", igbvf_version);

	atomic_or_32(&igbvf->igbvf_state, IGBVF_INITIALIZED);

	return (DDI_SUCCESS);

attach_fail:
	(void) igbvf_unconfigure(devinfo, igbvf);
	return (DDI_FAILURE);
}

/*
 * igbvf_detach - driver detach
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
igbvf_detach(dev_info_t *devinfo, ddi_detach_cmd_t cmd)
{
	igbvf_t *igbvf;
	boolean_t is_suspended;

	/*
	 * Check detach command
	 */
	switch (cmd) {
	default:
		return (DDI_FAILURE);

	case DDI_SUSPEND:
		igbvf = (igbvf_t *)ddi_get_driver_private(devinfo);
		if (igbvf == NULL)
			return (DDI_FAILURE);
		return (igbvf_suspend(igbvf, IGBVF_DDI_SR_ACTIVITIES,
		    IGBVF_DDI_SR_IMPACTS));

	case DDI_DETACH:
		break;
	}

	/*
	 * Get the pointer to the driver private data structure
	 */
	igbvf = (igbvf_t *)ddi_get_driver_private(devinfo);
	if (igbvf == NULL)
		return (DDI_FAILURE);

	/*
	 * If the device is still running, it needs to be stopped first.
	 * This check is necessary because under some specific circumstances,
	 * the detach routine can be called without stopping the interface
	 * first.
	 */
	mutex_enter(&igbvf->gen_lock);

	/*
	 * End of the suspending period
	 */
	is_suspended = IGBVF_IS_SUSPENDED(igbvf);
	if (is_suspended)
		atomic_and_32(&igbvf->igbvf_state, ~IGBVF_SUSPENDED);

	if (IGBVF_IS_SUSPENDED_DMA_FREE(igbvf)) {
		igbvf_free_pending_rx_data(igbvf);
	}

	if (IGBVF_IS_STARTED(igbvf)) {
		atomic_and_32(&igbvf->igbvf_state, ~IGBVF_STARTED);
		igbvf_stop(igbvf);
	}

	mutex_exit(&igbvf->gen_lock);

	/* Disable and stop the watchdog timer */
	igbvf_disable_watchdog_timer(igbvf);

	/*
	 * Check if there are still rx buffers held by the upper layer.
	 * If so, fail the detach.
	 */
	if (!igbvf_rx_drain(igbvf)) {
		IGBVF_DEBUGLOG_1(igbvf, "detach failed, "
		    "%d packets still held in stack", igbvf->rcb_pending);
		if (is_suspended)
			atomic_or_32(&igbvf->igbvf_state, IGBVF_SUSPENDED);
		return (DDI_FAILURE);
	}

	/*
	 * Do the remaining unconfigure routines
	 */
	return (igbvf_unconfigure(devinfo, igbvf));
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
igbvf_quiesce(dev_info_t *devinfo)
{
	igbvf_t *igbvf;

	igbvf = (igbvf_t *)ddi_get_driver_private(devinfo);

	if (igbvf == NULL)
		return (DDI_FAILURE);

	if (igbvf->attach_progress & ATTACH_PROGRESS_ENABLE_INTR) {
		/*
		 * Disable the adapter interrupts
		 */
		igbvf_disable_adapter_interrupts(igbvf);
	}

	if (igbvf->attach_progress & ATTACH_PROGRESS_INIT_ADAPTER) {
		/*
		 * Reset the adapter
		 */
		mutex_enter(&igbvf->gen_lock);
		if (igbvf_reset_hw_vf(igbvf) != E1000_SUCCESS) {
			igbvf_log(igbvf,
			    "igbvf_quiesce: Adapter reset failed.");
			/*
			 * for now, still return success
			 */
		}
		mutex_exit(&igbvf->gen_lock);
	}

	return (DDI_SUCCESS);
}

/*
 * igbvf_unconfigure - release all resources held by this instance
 */
static int
igbvf_unconfigure(dev_info_t *devinfo, igbvf_t *igbvf)
{
	int ret;

	/*
	 * Disable interrupt
	 */
	if (igbvf->attach_progress & ATTACH_PROGRESS_ENABLE_INTR) {
		if ((ret = igbvf_disable_intrs(igbvf)) != IGBVF_SUCCESS) {
			igbvf_log(igbvf,
			    "Failed to disable DDI interrupts");
			return (DDI_FAILURE);
		}
	}

	/*
	 * Unregister callback handler
	 */
	if (igbvf->attach_progress & ATTACH_PROGRESS_CB) {
		if ((ret = ddi_cb_unregister(igbvf->cb_hdl)) != DDI_SUCCESS) {
			igbvf_log(igbvf,
			    "Failed to unregister callback: %d", ret);
			return (DDI_FAILURE);
		}
	}

	/*
	 * Unregister MAC
	 */
	if (igbvf->attach_progress & ATTACH_PROGRESS_MAC) {
		if ((ret = mac_unregister(igbvf->mac_hdl)) != 0) {
			igbvf_log(igbvf,
			    "Failed to unregister mac: %d", ret);
			return (DDI_FAILURE);
		}
	}

	/*
	 * Free statistics
	 */
	if (igbvf->attach_progress & ATTACH_PROGRESS_STATS) {
		kstat_delete((kstat_t *)igbvf->igbvf_ks);
	}

	/*
	 * Remove interrupt handlers
	 */
	if (igbvf->attach_progress & ATTACH_PROGRESS_ADD_INTR) {
		igbvf_rem_intr_handlers(igbvf);
	}

	/*
	 * Remove interrupts
	 */
	if (igbvf->attach_progress & ATTACH_PROGRESS_ALLOC_INTR) {
		igbvf_rem_intrs(igbvf);
	}

	/*
	 * Destroy mailbox taskq
	 */
	if (igbvf->attach_progress & ATTACH_PROGRESS_MBX_TASKQ) {
		ddi_taskq_destroy(igbvf->mbx_taskq);
	}

	/*
	 * Destroy link check taskq
	 */
	if (igbvf->attach_progress & ATTACH_PROGRESS_TIMER_TASKQ) {
		ddi_taskq_destroy(igbvf->timer_taskq);
	}

	/*
	 * Remove driver properties
	 */
	if (igbvf->attach_progress & ATTACH_PROGRESS_PROPS) {
		ddi_prop_remove_all(devinfo);
	}

	/*
	 * Stop the adapter
	 */
	if (igbvf->attach_progress & ATTACH_PROGRESS_INIT_ADAPTER) {
		mutex_enter(&igbvf->gen_lock);
		igbvf_stop_adapter(igbvf);
		mutex_exit(&igbvf->gen_lock);
		if (igbvf_check_acc_handle(igbvf->osdep.reg_handle) !=
		    DDI_FM_OK)
			ddi_fm_service_impact(igbvf->dip,
			    DDI_SERVICE_UNAFFECTED);
	}

	/*
	 * Free multicast table
	 */
	igbvf_release_multicast(igbvf);

	/*
	 * Free register handle
	 */
	if (igbvf->attach_progress & ATTACH_PROGRESS_REGS_MAP) {
		if (igbvf->osdep.reg_handle != NULL)
			ddi_regs_map_free(&igbvf->osdep.reg_handle);
	}

	/*
	 * Free PCI config handle
	 */
	if (igbvf->attach_progress & ATTACH_PROGRESS_PCI_CONFIG) {
		if (igbvf->osdep.cfg_handle != NULL)
			pci_config_teardown(&igbvf->osdep.cfg_handle);
	}

	/*
	 * Free locks
	 */
	if (igbvf->attach_progress & ATTACH_PROGRESS_LOCKS) {
		igbvf_destroy_locks(igbvf);
	}

	/*
	 * Free the rx/tx rings
	 */
	if (igbvf->attach_progress & ATTACH_PROGRESS_ALLOC_RINGS) {
		igbvf_free_ring_group(igbvf);
	}

	/*
	 * Remove FMA
	 */
	if (igbvf->attach_progress & ATTACH_PROGRESS_FMINIT) {
		igbvf_fm_fini(igbvf);
	}

	/*
	 * Free the driver data structure
	 */
	kmem_free(igbvf, sizeof (igbvf_t));

	ddi_set_driver_private(devinfo, NULL);

	return (DDI_SUCCESS);
}

/*
 * igbvf_register_mac - Register the driver and its function pointers with
 * the GLD interface
 */
static int
igbvf_register_mac(igbvf_t *igbvf)
{
	struct e1000_hw *hw = &igbvf->hw;
	mac_register_t *mac;
	int status;

	if ((mac = mac_alloc(MAC_VERSION)) == NULL)
		return (IGBVF_FAILURE);

	mac->m_type_ident = MAC_PLUGIN_IDENT_ETHER;
	mac->m_driver = igbvf;
	mac->m_dip = igbvf->dip;
	mac->m_src_addr = hw->mac.addr;
	mac->m_callbacks = &igbvf_m_callbacks;
	mac->m_min_sdu = 0;
	mac->m_max_sdu = igbvf->default_mtu;
	mac->m_margin = VLAN_TAGSZ;
	mac->m_priv_props = igbvf_priv_props;
	mac->m_flags = MAC_FLAGS_PROMISCUOUS_MULTICAST;

	status = mac_register(mac, &igbvf->mac_hdl);

	mac_free(mac);

	return ((status == 0) ? IGBVF_SUCCESS : IGBVF_FAILURE);
}

/*
 * igbvf_identify_hardware - Identify the type of the adapter
 */
static int
igbvf_identify_hardware(igbvf_t *igbvf)
{
	struct e1000_hw *hw = &igbvf->hw;
	struct igbvf_osdep *osdep = &igbvf->osdep;

	/*
	 * Solaris recommendation is to get vendorid/deviceid from system
	 * properties
	 */
	hw->vendor_id = (uint16_t)ddi_prop_get_int(DDI_DEV_T_ANY, igbvf->dip,
	    DDI_PROP_DONTPASS, "vendor-id", -1);
	hw->device_id = (uint16_t)ddi_prop_get_int(DDI_DEV_T_ANY, igbvf->dip,
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

	/*
	 * Support only Intel devices
	 */
	if (hw->vendor_id != 0x8086) {
		igbvf_error(igbvf, "Reject non-intel vendor id: 0x%x",
		    hw->vendor_id);
		return (IGBVF_FAILURE);
	}

	/*
	 * Set mac type & install adapter capabilities based on device id
	 */
	switch (hw->device_id) {
	case E1000_DEV_ID_82576_VF:
		hw->mac.type = e1000_vfadapt;
		igbvf->capab = &igbvf_82576vf_cap;
		break;
	default:
		return (IGBVF_FAILURE);
	}

	/*
	 * Initialize adapter specific hardware function pointers
	 */
	e1000_init_function_pointers_vf(hw);

	igbvf->hw.mac.ops.init_params(hw);

	igbvf->hw.mbx.ops.init_params(hw);

	return (IGBVF_SUCCESS);
}

/*
 * igbvf_regs_map - Map the device registers
 */
static int
igbvf_regs_map(igbvf_t *igbvf)
{
	dev_info_t *devinfo = igbvf->dip;
	struct e1000_hw *hw = &igbvf->hw;
	struct igbvf_osdep *osdep = &igbvf->osdep;
	off_t mem_size;

	/*
	 * First get the size of device registers to be mapped.
	 */
	if (ddi_dev_regsize(devinfo, IGBVF_ADAPTER_REGSET, &mem_size) !=
	    DDI_SUCCESS) {
		return (IGBVF_FAILURE);
	}

	/*
	 * Call ddi_regs_map_setup() to map registers
	 */
	if ((ddi_regs_map_setup(devinfo, IGBVF_ADAPTER_REGSET,
	    (caddr_t *)&hw->hw_addr, 0,
	    mem_size, &igbvf_regs_acc_attr,
	    &osdep->reg_handle)) != DDI_SUCCESS) {
		return (IGBVF_FAILURE);
	}

	return (IGBVF_SUCCESS);
}

/*
 * igbvf_init_properties - Initialize driver properties
 */
static void
igbvf_init_properties(igbvf_t *igbvf)
{
	/*
	 * Get conf file properties, including link settings
	 * jumbo frames, ring number, descriptor number, etc.
	 */
	igbvf_get_conf(igbvf);
}

/*
 * igbvf_init_driver_settings - Initialize driver settings
 *
 * The settings include hardware function pointers, bus information,
 * rx/tx rings settings, link state, and any other parameters that
 * need to be setup during driver initialization.
 */
static int
igbvf_init_driver_settings(igbvf_t *igbvf)
{
	uint32_t rx_size;
	uint32_t tx_size;
	int i;

	/*
	 * Get the system page size
	 */
	igbvf->page_size = ddi_ptob(igbvf->dip, (ulong_t)1);

	/*
	 * Set rx buffer size
	 * The IP header alignment room is counted in the calculation.
	 * The rx buffer size is in unit of 1K that is required by the
	 * adapter hardware.
	 */
	rx_size = igbvf->max_frame_size + IPHDR_ALIGN_ROOM;
	igbvf->rx_buf_size = ((rx_size >> 10) +
	    ((rx_size & (((uint32_t)1 << 10) - 1)) > 0 ? 1 : 0)) << 10;

	/*
	 * Set tx buffer size
	 */
	tx_size = igbvf->max_frame_size;
	igbvf->tx_buf_size = ((tx_size >> 10) +
	    ((tx_size & (((uint32_t)1 << 10) - 1)) > 0 ? 1 : 0)) << 10;

	/*
	 * Initialize values of interrupt throttling rates
	 */
	for (i = 1; i < igbvf->intr_cnt; i++)
		igbvf->intr_throttling[i] = igbvf->intr_throttling[0];

	/*
	 * The initial link state should be "unknown"
	 */
	igbvf->link_state = LINK_STATE_UNKNOWN;

	/*
	 * Initialize the software structures of rings and groups
	 */
	igbvf_init_ring_group(igbvf);

	return (IGBVF_SUCCESS);
}

/*
 * igbvf_init_locks - Initialize locks
 */
static void
igbvf_init_locks(igbvf_t *igbvf)
{
	mutex_init(&igbvf->gen_lock, NULL,
	    MUTEX_DRIVER, DDI_INTR_PRI(igbvf->intr_pri));
	mutex_init(&igbvf->watchdog_lock, NULL,
	    MUTEX_DRIVER, DDI_INTR_PRI(igbvf->intr_pri));

	cv_init(&igbvf->mbx_hold_cv, NULL, CV_DRIVER, NULL);
	cv_init(&igbvf->mbx_poll_cv, NULL, CV_DRIVER, NULL);
}

/*
 * igbvf_destroy_locks - Destroy locks
 */
static void
igbvf_destroy_locks(igbvf_t *igbvf)
{
	mutex_destroy(&igbvf->gen_lock);
	mutex_destroy(&igbvf->watchdog_lock);

	cv_destroy(&igbvf->mbx_hold_cv);
	cv_destroy(&igbvf->mbx_poll_cv);
}

static int
igbvf_resume(igbvf_t *igbvf, uint_t activities, uint_t impacts)
{
	mutex_enter(&igbvf->gen_lock);

	if (!IGBVF_IS_SUSPENDED(igbvf)) {
		mutex_exit(&igbvf->gen_lock);
		return (DDI_EALREADY);
	}

	if ((activities != igbvf->sr_activities) ||
	    (impacts != igbvf->sr_impacts)) {
		igbvf_error(igbvf,
		    "Resume with different activities and impacts");
		mutex_exit(&igbvf->gen_lock);
		return (DDI_EINVAL);
	}

	/*
	 * End of the suspending period
	 */
	atomic_and_32(&igbvf->igbvf_state, ~IGBVF_SUSPENDED);

	if (IGBVF_IS_SUSPENDED_DMA_FREE(igbvf)) {
		igbvf_free_pending_rx_data(igbvf);
	}

	if (IGBVF_IS_SUSPENDED_INTR(igbvf) &&
	    !(igbvf->attach_progress & ATTACH_PROGRESS_ENABLE_INTR)) {
		/*
		 * Enable interrupts
		 */
		if (igbvf_enable_intrs(igbvf) != IGBVF_SUCCESS) {
			igbvf_error(igbvf, "Failed to enable DDI interrupts");
			goto resume_fail;
		}
		igbvf->attach_progress |= ATTACH_PROGRESS_ENABLE_INTR;
	}

	if (IGBVF_IS_SUSPENDED_ADAPTER(igbvf)) {
		/* Perform a function level reset */
		igbvf_stop_adapter(igbvf);

		/* Re-negotiate the message API after the FLR */
		if (igbvf_nego_msg_api(igbvf) != IGBVF_SUCCESS) {
			igbvf_error(igbvf, "Compatible message API not found");
			goto resume_fail;
		}

		/*
		 * Initialize the adapter
		 */
		if (igbvf_init_adapter(igbvf) != IGBVF_SUCCESS) {
			igbvf_fm_ereport(igbvf, DDI_FM_DEVICE_INVAL_STATE);
			ddi_fm_service_impact(igbvf->dip, DDI_SERVICE_LOST);
			goto resume_fail;
		}
		igbvf->attach_progress |= ATTACH_PROGRESS_INIT_ADAPTER;
	}

	if (IGBVF_IS_STARTED(igbvf) &&
	    (igbvf->sr_reconfigure & IGBVF_SR_RC_STOP)) {

		atomic_and_32(&igbvf->igbvf_state, ~IGBVF_STARTED);
		igbvf_stop(igbvf);
	} else if (IGBVF_IS_STARTED(igbvf) ||
	    (igbvf->sr_reconfigure & IGBVF_SR_RC_START)) {

		if (igbvf_start(igbvf) != IGBVF_SUCCESS)
			goto resume_fail;
		atomic_or_32(&igbvf->igbvf_state, IGBVF_STARTED);
	}

	igbvf->sr_activities = 0;
	igbvf->sr_impacts = 0;
	igbvf->sr_reconfigure = 0;

	mutex_exit(&igbvf->gen_lock);

	/*
	 * Enable and start the watchdog timer
	 */
	igbvf_enable_watchdog_timer(igbvf);

	return (DDI_SUCCESS);

resume_fail:
	igbvf_error(igbvf, "Failed to resume the device");

	if (IGBVF_IS_SUSPENDED_ADAPTER(igbvf)) {
		if (igbvf->attach_progress & ATTACH_PROGRESS_INIT_ADAPTER) {
			igbvf_stop_adapter(igbvf);
			igbvf->attach_progress &= ~ATTACH_PROGRESS_INIT_ADAPTER;
		}
	}

	atomic_or_32(&igbvf->igbvf_state, IGBVF_SUSPENDED);

	mutex_exit(&igbvf->gen_lock);

	if (IGBVF_IS_SUSPENDED_INTR(igbvf)) {
		if (igbvf->attach_progress & ATTACH_PROGRESS_ENABLE_INTR) {
			(void) igbvf_disable_intrs(igbvf);
			igbvf->attach_progress &= ~ATTACH_PROGRESS_ENABLE_INTR;
		}
	}

	return (DDI_FAILURE);
}

static int
igbvf_suspend(igbvf_t *igbvf, uint_t activities, uint_t impacts)
{
	mutex_enter(&igbvf->gen_lock);

	if (IGBVF_IS_SUSPENDED(igbvf)) {
		mutex_exit(&igbvf->gen_lock);
		return (DDI_EALREADY);
	}

	igbvf->sr_activities = activities;
	igbvf->sr_impacts = impacts;
	igbvf->sr_reconfigure = 0;

	if (IGBVF_IS_STARTED(igbvf)) {
		igbvf_stop(igbvf);
	}

	if (IGBVF_IS_SUSPENDED_ADAPTER(igbvf)) {
		/*
		 * Stop the adapter
		 */
		igbvf_stop_adapter(igbvf);
		igbvf->attach_progress &= ~ATTACH_PROGRESS_INIT_ADAPTER;
	} else if (IGBVF_IS_SUSPENDED_INTR(igbvf)) {
		/*
		 * Stop mailbox interrupt, in case driver is not STARTED
		 */
		igbvf_disable_adapter_interrupts(igbvf);
	}

	/*
	 * Start of the suspending period
	 */
	atomic_or_32(&igbvf->igbvf_state, IGBVF_SUSPENDED);

	mutex_exit(&igbvf->gen_lock);

	/*
	 * Disable and stop the watchdog timer
	 */
	igbvf_disable_watchdog_timer(igbvf);

	if (IGBVF_IS_SUSPENDED_INTR(igbvf)) {
		/*
		 * Disable interrupts
		 */
		if (igbvf_disable_intrs(igbvf) != IGBVF_SUCCESS) {
			igbvf_error(igbvf, "Failed to disable DDI interrupts");
			goto suspend_fail;
		}
		igbvf->attach_progress &= ~ATTACH_PROGRESS_ENABLE_INTR;
	}

	return (DDI_SUCCESS);

suspend_fail:
	igbvf_error(igbvf, "Failed to suspend the device");

	(void) igbvf_resume(igbvf, activities, impacts);

	return (DDI_FAILURE);
}

static int
igbvf_init(igbvf_t *igbvf)
{
	mutex_enter(&igbvf->gen_lock);

	/*
	 * Initialize the adapter
	 */
	if (igbvf_init_adapter(igbvf) != IGBVF_SUCCESS) {
		mutex_exit(&igbvf->gen_lock);
		return (IGBVF_FAILURE);
	}

	igbvf_enable_mailbox_interrupt(igbvf);

	mutex_exit(&igbvf->gen_lock);

	if (igbvf_check_acc_handle(igbvf->osdep.reg_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(igbvf->dip, DDI_SERVICE_LOST);
		return (IGBVF_FAILURE);
	}

	return (IGBVF_SUCCESS);
}

/*
 * igbvf_init_default_mac_address - Initialize the default MAC address
 *
 * On success, the MAC address is entered in the igbvf->hw.mac.addr
 * and hw->mac.perm_addr fields and the adapter's RAR(0) receive
 * address register.
 *
 * Important side effects:
 * 1. adapter is reset - this is required to put it in a known state.
 * 2. all of non-volatile memory (NVM) is read & checksummed - NVM is where
 * MAC address and all default settings are stored, so a valid checksum
 * is required.
 */
static int
igbvf_init_default_mac_address(igbvf_t *igbvf)
{
	struct e1000_hw *hw = &igbvf->hw;

	ASSERT(mutex_owned(&igbvf->gen_lock));

	/*
	 * Function-level Reset to put the virtual function in a known state,
	 * get MAC address in a message from physical function.
	 */
	if (igbvf_reset_hw_vf(igbvf) != E1000_SUCCESS) {
		igbvf_log(igbvf, "Adapter reset failed.");
		igbvf_fm_ereport(igbvf, DDI_FM_DEVICE_INVAL_STATE);
		ddi_fm_service_impact(igbvf->dip, DDI_SERVICE_DEGRADED);
		return (IGBVF_FAILURE);
	}

	/* remember time of reset */
	igbvf->last_reset = ddi_get_lbolt();

	/* make sure mac address is valid */
	if (is_valid_mac_addr(igbvf->hw.mac.perm_addr)) {
		hw->mac.ops.read_mac_addr(hw);
	} else {
		igbvf_log(igbvf,
		    "Invalid MAC Address: %02x:%02x:%02x:%02x:%02x:%02x",
		    igbvf->hw.mac.addr[0], igbvf->hw.mac.addr[1],
		    igbvf->hw.mac.addr[2], igbvf->hw.mac.addr[3],
		    igbvf->hw.mac.addr[4], igbvf->hw.mac.addr[5]);
		return (IGBVF_FAILURE);
	}

	/* Save the default mac address at the first slot */
	bcopy(hw->mac.addr, igbvf->unicst_addr[0].addr, ETHERADDRL);
	igbvf->unicst_addr[0].set = 0;

	igbvf->unicst_total = 1;

	return (IGBVF_SUCCESS);
}

/*
 * igbvf_init_adapter - Initialize the adapter
 */
static int
igbvf_init_adapter(igbvf_t *igbvf)
{
	struct e1000_hw *hw = &igbvf->hw;
	int i;

	ASSERT(mutex_owned(&igbvf->gen_lock));

	/*
	 * Setup MSI-X interrupts
	 */
	igbvf->capab->setup_msix(igbvf);

	/*
	 * Initialize unicast addresses.
	 */
	if (igbvf_unicst_init(igbvf) != IGBVF_SUCCESS) {
		igbvf_error(igbvf, "Failed to initialize unicast addresses");
		return (IGBVF_FAILURE);
	}

	/*
	 * Setup and initialize the multicast table structures.
	 */
	if (igbvf_setup_multicst(igbvf) != IGBVF_SUCCESS) {
		igbvf_error(igbvf, "Failed to initialize multicast addresses");
		return (IGBVF_FAILURE);
	}

	/*
	 * Setup promisc mode
	 */
	(void) igbvf_init_promisc(igbvf);

	/*
	 * Update statistics registers with saved values
	 */
	igbvf_update_stat_regs(igbvf);

	/*
	 * Set interrupt throttling rate
	 */
	for (i = 0; i < igbvf->intr_cnt; i++)
		E1000_WRITE_REG(hw, E1000_EITR(i), igbvf->intr_throttling[i]);

	return (IGBVF_SUCCESS);
}

/*
 * igbvf_stop_adapter - Stop the adapter
 */
static void
igbvf_stop_adapter(igbvf_t *igbvf)
{
	ASSERT(mutex_owned(&igbvf->gen_lock));

	/*
	 * Reset the adapter
	 */
	if (igbvf_reset_hw_vf(igbvf) != E1000_SUCCESS) {
		igbvf_log(igbvf, "igbvf_stop_adapter: Adapter reset failed.");
		igbvf_fm_ereport(igbvf, DDI_FM_DEVICE_INVAL_STATE);
		ddi_fm_service_impact(igbvf->dip, DDI_SERVICE_DEGRADED);
	}
}

/*
 * igbvf_reset - Reset the adapter and restart the driver.
 *
 * It involves stopping and re-starting the adapter,
 * and re-configuring the rx/tx rings.
 */
static void
igbvf_reset(void *arg)
{
	igbvf_t *igbvf = (igbvf_t *)arg;
	boolean_t started;
	boolean_t ereport;
	int i;

	mutex_enter(&igbvf->gen_lock);

	if (igbvf->igbvf_state & IGBVF_SUSPENDED) {
		mutex_exit(&igbvf->gen_lock);
		return;
	}

	ereport = !(igbvf->igbvf_state & IGBVF_RESET);

	started = IGBVF_IS_STARTED(igbvf);
	if (started)
		atomic_and_32(&igbvf->igbvf_state, ~IGBVF_STARTED);

	/*
	 * Disable the adapter interrupts to stop any rx/tx activities
	 * before draining pending data and resetting hardware.
	 */
	igbvf_disable_adapter_interrupts(igbvf);

	/*
	 * Drain the pending transmit packets
	 */
	if (started)
		(void) igbvf_tx_drain(igbvf);

	for (i = 0; i < igbvf->num_rx_rings; i++)
		mutex_enter(&igbvf->rx_rings[i].rx_lock);
	for (i = 0; i < igbvf->num_tx_rings; i++)
		mutex_enter(&igbvf->tx_rings[i].tx_lock);

	/*
	 * Stop the adapter. This will do a function level reset.
	 */
	igbvf_stop_adapter(igbvf);

	/*
	 * Clean the pending tx data/resources
	 */
	if (started)
		igbvf_tx_clean(igbvf);

	/*
	 * Re-negotiate the message API after the FLR
	 */
	if (igbvf_nego_msg_api(igbvf) != IGBVF_SUCCESS) {
		igbvf_log(igbvf, "Re-negotiate message API failed");
	}

	/*
	 * Start the adapter
	 */
	if (igbvf_init_adapter(igbvf) != IGBVF_SUCCESS)
		goto reset_failure;

	/*
	 * Setup the rx/tx rings
	 */
	if (started) {
		igbvf->tx_ring_init = B_FALSE;
		igbvf_setup_rings(igbvf);
	}

	atomic_and_32(&igbvf->igbvf_state, ~(IGBVF_ERROR | IGBVF_RESET));

	/*
	 * Enable adapter interrupts
	 * The interrupts must be enabled after the driver state is START
	 */
	if (started)
		igbvf->capab->enable_intr(igbvf);
	else
		igbvf_enable_mailbox_interrupt(igbvf);

	if (igbvf_check_acc_handle(igbvf->osdep.reg_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(igbvf->dip, DDI_SERVICE_LOST);
		goto reset_failure;
	}

	for (i = igbvf->num_tx_rings - 1; i >= 0; i--)
		mutex_exit(&igbvf->tx_rings[i].tx_lock);
	for (i = igbvf->num_rx_rings - 1; i >= 0; i--)
		mutex_exit(&igbvf->rx_rings[i].rx_lock);

	if (started)
		atomic_or_32(&igbvf->igbvf_state, IGBVF_STARTED);

	mutex_exit(&igbvf->gen_lock);

	if (ereport)
		ddi_fm_service_impact(igbvf->dip, DDI_SERVICE_RESTORED);

	igbvf_restart_watchdog_timer(igbvf);

	return;

reset_failure:
	for (i = igbvf->num_tx_rings - 1; i >= 0; i--)
		mutex_exit(&igbvf->tx_rings[i].tx_lock);
	for (i = igbvf->num_rx_rings - 1; i >= 0; i--)
		mutex_exit(&igbvf->rx_rings[i].rx_lock);

	mutex_exit(&igbvf->gen_lock);
}

/*
 * igbvf_tx_clean - Clean the pending transmit packets and DMA resources
 */
static void
igbvf_tx_clean(igbvf_t *igbvf)
{
	igbvf_tx_ring_t *tx_ring;
	tx_control_block_t *tcb;
	link_list_t pending_list;
	uint32_t desc_num;
	int i, j;

	LINK_LIST_INIT(&pending_list);

	for (i = 0; i < igbvf->num_tx_rings; i++) {
		tx_ring = &igbvf->tx_rings[i];

		mutex_enter(&tx_ring->recycle_lock);

		/*
		 * Clean the pending tx data - the pending packets in the
		 * work_list that have no chances to be transmitted again.
		 *
		 * We must ensure the adapter is stopped or the link is down
		 * before cleaning the transmit packets.
		 */
		desc_num = 0;
		for (j = 0; j < tx_ring->ring_size; j++) {
			tcb = tx_ring->work_list[j];
			if (tcb != NULL) {
				desc_num += tcb->desc_num;

				tx_ring->work_list[j] = NULL;

				igbvf_free_tcb(tcb);

				LIST_PUSH_TAIL(&pending_list, &tcb->link);
			}
		}

		if (desc_num > 0) {
			atomic_add_32(&tx_ring->tbd_free, desc_num);
			ASSERT(tx_ring->tbd_free == tx_ring->ring_size);

			/*
			 * Reset the head and tail pointers of the tbd ring;
			 * Reset the head write-back if it is enabled.
			 */
			tx_ring->tbd_head = 0;
			tx_ring->tbd_tail = 0;
			if (igbvf->tx_head_wb_enable)
				*tx_ring->tbd_head_wb = 0;

			E1000_WRITE_REG(&igbvf->hw,
			    E1000_TDH(tx_ring->queue), 0);
			E1000_WRITE_REG(&igbvf->hw,
			    E1000_TDT(tx_ring->queue), 0);
		}

		mutex_exit(&tx_ring->recycle_lock);

		/*
		 * Add the tx control blocks in the pending list to
		 * the free list.
		 */
		igbvf_put_free_list(tx_ring, &pending_list);
	}
}

/*
 * igbvf_tx_drain - Drain tx rings to allow pending packets to be transmitted
 */
static boolean_t
igbvf_tx_drain(igbvf_t *igbvf)
{
	igbvf_tx_ring_t *tx_ring;
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
		for (j = 0; j < igbvf->num_tx_rings; j++) {
			tx_ring = &igbvf->tx_rings[j];
			tx_ring->tx_recycle(tx_ring);
			done = done &&
			    (tx_ring->tcb_free == tx_ring->free_list_size);
		}

		if (done)
			break;

		msec_delay(1);
	}

	IGBVF_DEBUGLOG_2(igbvf, "tx drain%s completed in %d ms",
	    ((done) ? "": " not"), i);

	return (done);
}

/*
 * igbvf_rx_drain - Wait for all rx buffers to be released by upper layer
 */
static boolean_t
igbvf_rx_drain(igbvf_t *igbvf)
{
	boolean_t done;
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
		done = (igbvf->rcb_pending == 0);

		if (done)
			break;

		msec_delay(1);
	}

	IGBVF_DEBUGLOG_2(igbvf, "rx drain%s completed in %d ms",
	    ((done) ? "": " not"), i);

	return (done);
}

/*
 * igbvf_start - Start the driver & adapter
 */
int
igbvf_start(igbvf_t *igbvf)
{
	int i;
	igbvf_tx_ring_t *tx_ring;

	ASSERT(mutex_owned(&igbvf->gen_lock));

	if (!IGBVF_IS_STARTED(igbvf) ||
	    IGBVF_IS_SUSPENDED_DMA_FREE(igbvf)) {
		if (igbvf_alloc_rx_data(igbvf) != IGBVF_SUCCESS) {
			igbvf_error(igbvf,
			    "Failed to allocate software receive rings");
			return (IGBVF_FAILURE);
		}
	}

	if (!IGBVF_IS_STARTED(igbvf)) {
		if (igbvf_alloc_tx_mem(igbvf) != IGBVF_SUCCESS) {
			igbvf_error(igbvf,
			    "Failed to allocate software transmit rings");
			igbvf_free_rx_data(igbvf);
			return (IGBVF_FAILURE);
		}
	}

	if (!IGBVF_IS_STARTED(igbvf) ||
	    IGBVF_IS_SUSPENDED_DMA_UNBIND(igbvf)) {

		/* Allocate buffers for all the rx/tx rings */
		if (igbvf_alloc_dma(igbvf) != IGBVF_SUCCESS) {
			igbvf_error(igbvf, "Failed to allocate DMA resource");
			igbvf_free_tx_mem(igbvf);
			igbvf_free_rx_data(igbvf);
			return (IGBVF_FAILURE);
		}
		igbvf->tx_ring_init = B_TRUE;
	}

	if (!IGBVF_IS_STARTED(igbvf) ||
	    IGBVF_IS_SUSPENDED_DMA(igbvf)) {

		/*
		 * Setup/Enable Tx & RX
		 */
		for (i = 0; i < igbvf->num_rx_rings; i++)
			mutex_enter(&igbvf->rx_rings[i].rx_lock);

		igbvf_setup_rx(igbvf);

		for (i = igbvf->num_rx_rings - 1; i >= 0; i--)
			mutex_exit(&igbvf->rx_rings[i].rx_lock);

		for (i = 0; i < igbvf->num_tx_rings; i++)
			mutex_enter(&igbvf->tx_rings[i].tx_lock);

		igbvf_setup_tx(igbvf);

		/*
		 * Now we must clear the flag to unblock tx
		 */
		atomic_and_32(&igbvf->igbvf_state, ~IGBVF_SUSPENDED_TX_RX);

		for (i = igbvf->num_tx_rings - 1; i >= 0; i--) {
			tx_ring = &igbvf->tx_rings[i];
			tx_ring->reschedule = B_FALSE;
			mutex_exit(&tx_ring->tx_lock);
			mac_tx_ring_update(igbvf->mac_hdl,
			    tx_ring->ring_handle);
			IGBVF_DEBUG_STAT(tx_ring->stat_reschedule);
		}

		if ((ddi_taskq_dispatch(igbvf->timer_taskq, igbvf_link_check,
		    (void *)igbvf, DDI_NOSLEEP)) != DDI_SUCCESS) {
			igbvf_log(igbvf,
			    "Failed to dispatch timer taskq for link check");
		}
	}

	if (!IGBVF_IS_STARTED(igbvf) ||
	    IGBVF_IS_SUSPENDED_INTR(igbvf)) {

		igbvf->sr_activities &= ~DDI_CB_LSR_ACT_INTR;

		/*
		 * Enable adapter interrupts
		 * The interrupts must be enabled after the driver state is
		 * START
		 */
		igbvf->capab->enable_intr(igbvf);
	}

	if (igbvf_check_acc_handle(igbvf->osdep.reg_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(igbvf->dip, DDI_SERVICE_LOST);
		goto start_fail;
	}

	return (IGBVF_SUCCESS);

start_fail:

	igbvf_stop(igbvf);

	return (IGBVF_FAILURE);
}

/*
 * igbvf_stop - Stop the driver & adapter
 */
void
igbvf_stop(igbvf_t *igbvf)
{
	int i;
	igbvf_rx_ring_t *rx_ring;

	ASSERT(mutex_owned(&igbvf->gen_lock));

	if (!(igbvf->attach_progress & ATTACH_PROGRESS_INIT_ADAPTER))
		goto free_dma_resource;

	if (!IGBVF_IS_SUSPENDED_INTR(igbvf)) {
		/*
		 * Disable the rx/tx interrupts and enable the mailbox interrupt
		 */
		igbvf_enable_mailbox_interrupt(igbvf);
	} else if (IGBVF_IS_STARTED(igbvf)) {
		/*
		 * Disable the adapter interrupts
		 */
		igbvf_disable_adapter_interrupts(igbvf);
	}

	if (IGBVF_IS_STARTED(igbvf) ==
	    IGBVF_IS_SUSPENDED_DMA(igbvf)) {

		/*
		 * Set the flag to prevent further tx and rx
		 */
		atomic_or_32(&igbvf->igbvf_state, IGBVF_SUSPENDED_TX_RX);

		/*
		 * Drain the pending tx packets
		 */
		(void) igbvf_tx_drain(igbvf);

		/*
		 * Stop the rx rings
		 */
		for (i = 0; i < igbvf->num_rx_rings; i++) {
			rx_ring = &igbvf->rx_rings[i];
			mutex_enter(&rx_ring->rx_lock);
			igbvf_disable_rx_ring(rx_ring);
			mutex_exit(&rx_ring->rx_lock);
		}

		/*
		 * Stop the tx rings
		 */
		for (i = 0; i < igbvf->num_tx_rings; i++)
			mutex_enter(&igbvf->tx_rings[i].tx_lock);

		for (i = 0; i < igbvf->num_tx_rings; i++)
			igbvf_disable_tx_ring(&igbvf->tx_rings[i]);

		/*
		 * Clean the pending tx data/resources
		 */
		igbvf_tx_clean(igbvf);

		for (i = igbvf->num_tx_rings - 1; i >= 0; i--)
			mutex_exit(&igbvf->tx_rings[i].tx_lock);

		if (igbvf_check_acc_handle(igbvf->osdep.reg_handle) !=
		    DDI_FM_OK)
			ddi_fm_service_impact(igbvf->dip, DDI_SERVICE_LOST);

		if (igbvf->link_state != LINK_STATE_UNKNOWN) {
			igbvf->link_state = LINK_STATE_UNKNOWN;
			mac_link_update(igbvf->mac_hdl, igbvf->link_state);
		}
	}

free_dma_resource:
	if ((IGBVF_IS_STARTED(igbvf) ==
	    IGBVF_IS_SUSPENDED_DMA_FREE(igbvf)) ||
	    (IGBVF_IS_STARTED(igbvf) ==
	    IGBVF_IS_SUSPENDED_DMA_UNBIND(igbvf))) {
		/*
		 * Release the DMA/memory resources of rx/tx rings
		 */
		igbvf_free_dma(igbvf);
	}

	if (!IGBVF_IS_STARTED(igbvf)) {
		igbvf_free_tx_mem(igbvf);
	}

	if (IGBVF_IS_STARTED(igbvf) ==
	    IGBVF_IS_SUSPENDED_DMA_FREE(igbvf)) {
		igbvf_free_rx_data(igbvf);
	}
}

/*
 * igbvf_alloc_ring_group - Allocate memory space for rx/tx rings
 */
static int
igbvf_alloc_ring_group(igbvf_t *igbvf)
{
	igbvf_rx_ring_t *rx_ring;
	igbvf_tx_ring_t *tx_ring;
	int i;

	/*
	 * Allocate memory space for rx rings
	 */
	igbvf->rx_rings = kmem_zalloc(
	    sizeof (igbvf_rx_ring_t) * igbvf->num_rx_rings,
	    KM_NOSLEEP);

	if (igbvf->rx_rings == NULL) {
		return (IGBVF_FAILURE);
	}

	/*
	 * Allocate memory space for tx rings
	 */
	igbvf->tx_rings = kmem_zalloc(
	    sizeof (igbvf_tx_ring_t) * igbvf->num_tx_rings,
	    KM_NOSLEEP);

	if (igbvf->tx_rings == NULL) {
		kmem_free(igbvf->rx_rings,
		    sizeof (igbvf_rx_ring_t) * igbvf->num_rx_rings);
		igbvf->rx_rings = NULL;
		return (IGBVF_FAILURE);
	}

	/*
	 * Allocate memory space for rx ring groups
	 */
	igbvf->rx_groups = kmem_zalloc(
	    sizeof (igbvf_rx_group_t) * igbvf->num_rx_groups,
	    KM_NOSLEEP);

	if (igbvf->rx_groups == NULL) {
		kmem_free(igbvf->rx_rings,
		    sizeof (igbvf_rx_ring_t) * igbvf->num_rx_rings);
		kmem_free(igbvf->tx_rings,
		    sizeof (igbvf_tx_ring_t) * igbvf->num_tx_rings);
		igbvf->rx_rings = NULL;
		igbvf->tx_rings = NULL;
		return (IGBVF_FAILURE);
	}

	/* Initialize per-ring mutexes */
	for (i = 0; i < igbvf->num_rx_rings; i++) {
		rx_ring = &igbvf->rx_rings[i];
		mutex_init(&rx_ring->rx_lock, NULL,
		    MUTEX_DRIVER, DDI_INTR_PRI(igbvf->intr_pri));
	}

	for (i = 0; i < igbvf->num_tx_rings; i++) {
		tx_ring = &igbvf->tx_rings[i];
		mutex_init(&tx_ring->tx_lock, NULL,
		    MUTEX_DRIVER, DDI_INTR_PRI(igbvf->intr_pri));
		mutex_init(&tx_ring->recycle_lock, NULL,
		    MUTEX_DRIVER, DDI_INTR_PRI(igbvf->intr_pri));
		mutex_init(&tx_ring->tcb_head_lock, NULL,
		    MUTEX_DRIVER, DDI_INTR_PRI(igbvf->intr_pri));
		mutex_init(&tx_ring->tcb_tail_lock, NULL,
		    MUTEX_DRIVER, DDI_INTR_PRI(igbvf->intr_pri));
	}

	return (IGBVF_SUCCESS);
}

/*
 * igbvf_free_ring_group - Free the memory space of rx/tx rings.
 */
static void
igbvf_free_ring_group(igbvf_t *igbvf)
{
	igbvf_rx_ring_t *rx_ring;
	igbvf_tx_ring_t *tx_ring;
	int i;

	for (i = 0; i < igbvf->num_rx_rings; i++) {
		rx_ring = &igbvf->rx_rings[i];
		mutex_destroy(&rx_ring->rx_lock);
	}

	for (i = 0; i < igbvf->num_tx_rings; i++) {
		tx_ring = &igbvf->tx_rings[i];
		mutex_destroy(&tx_ring->tx_lock);
		mutex_destroy(&tx_ring->recycle_lock);
		mutex_destroy(&tx_ring->tcb_head_lock);
		mutex_destroy(&tx_ring->tcb_tail_lock);
	}

	if (igbvf->rx_rings != NULL) {
		kmem_free(igbvf->rx_rings,
		    sizeof (igbvf_rx_ring_t) * igbvf->num_rx_rings);
		igbvf->rx_rings = NULL;
	}

	if (igbvf->tx_rings != NULL) {
		kmem_free(igbvf->tx_rings,
		    sizeof (igbvf_tx_ring_t) * igbvf->num_tx_rings);
		igbvf->tx_rings = NULL;
	}

	if (igbvf->rx_groups != NULL) {
		kmem_free(igbvf->rx_groups,
		    sizeof (igbvf_rx_group_t) * igbvf->num_rx_groups);
		igbvf->rx_groups = NULL;
	}
}

static int
igbvf_alloc_tx_mem(igbvf_t *igbvf)
{
	igbvf_tx_ring_t *tx_ring;
	int i;

	for (i = 0; i < igbvf->num_tx_rings; i++) {
		tx_ring = &igbvf->tx_rings[i];
		if (igbvf_alloc_tx_ring_mem(tx_ring) != IGBVF_SUCCESS)
			goto alloc_tx_mem_failure;
	}
	return (IGBVF_SUCCESS);

alloc_tx_mem_failure:
	igbvf_free_tx_mem(igbvf);
	return (IGBVF_FAILURE);
}

static void
igbvf_free_tx_mem(igbvf_t *igbvf)
{
	igbvf_tx_ring_t *tx_ring;
	int i;

	for (i = 0; i < igbvf->num_tx_rings; i++) {
		tx_ring = &igbvf->tx_rings[i];
		igbvf_free_tx_ring_mem(tx_ring);
	}
}

static int
igbvf_alloc_rx_data(igbvf_t *igbvf)
{
	igbvf_rx_ring_t *rx_ring;
	int i;

	for (i = 0; i < igbvf->num_rx_rings; i++) {
		rx_ring = &igbvf->rx_rings[i];
		if (igbvf_alloc_rx_ring_data(rx_ring) != IGBVF_SUCCESS)
			goto alloc_rx_rings_failure;
	}
	return (IGBVF_SUCCESS);

alloc_rx_rings_failure:
	igbvf_free_rx_data(igbvf);
	return (IGBVF_FAILURE);
}

static void
igbvf_free_rx_data(igbvf_t *igbvf)
{
	igbvf_rx_ring_t *rx_ring;
	igbvf_rx_data_t *rx_data;
	int i;

	for (i = 0; i < igbvf->num_rx_rings; i++) {
		rx_ring = &igbvf->rx_rings[i];

		mutex_enter(&igbvf->rx_pending_lock);
		rx_data = rx_ring->rx_data;

		if (rx_data != NULL) {
			rx_data->flag |= IGBVF_RX_STOPPED;

			if (rx_data->rcb_pending == 0) {
				igbvf_free_rx_ring_data(rx_data);
				rx_ring->rx_data = NULL;
			}
		}

		mutex_exit(&igbvf->rx_pending_lock);
	}
}

/*
 * igbvf_init_ring_group - Initialize software structures of rings and groups
 */
static void
igbvf_init_ring_group(igbvf_t *igbvf)
{
	igbvf_rx_ring_t *rx_ring;
	igbvf_tx_ring_t *tx_ring;
	igbvf_rx_group_t *rx_group;
	int i;

	/*
	 * Initialize rx/tx rings parameters
	 */
	for (i = 0; i < igbvf->num_rx_rings; i++) {
		rx_ring = &igbvf->rx_rings[i];
		rx_ring->index = i;
		rx_ring->queue = i;
		rx_ring->igbvf = igbvf;
	}

	for (i = 0; i < igbvf->num_tx_rings; i++) {
		tx_ring = &igbvf->tx_rings[i];
		tx_ring->index = i;
		tx_ring->queue = i;
		tx_ring->igbvf = igbvf;

		if (igbvf->tx_head_wb_enable)
			tx_ring->tx_recycle = igbvf_tx_recycle_head_wb;
		else
			tx_ring->tx_recycle = igbvf_tx_recycle_legacy;

		tx_ring->ring_size = igbvf->tx_ring_size;
		tx_ring->free_list_size = igbvf->tx_ring_size +
		    (igbvf->tx_ring_size >> 1);
	}

	/*
	 * Set up the receive ring group indexes
	 */
	for (i = 0; i < igbvf->num_rx_groups; i++) {
		rx_group = &igbvf->rx_groups[i];
		rx_group->index = i;
		rx_group->igbvf = igbvf;
	}

	/*
	 * Get the ring number per group.
	 */
	igbvf->ring_per_group = igbvf->num_rx_rings / igbvf->num_rx_groups;
}

/*
 * igbvf_setup_rings - Setup rx/tx rings
 */
static void
igbvf_setup_rings(igbvf_t *igbvf)
{
	ASSERT(mutex_owned(&igbvf->gen_lock));

	/*
	 * Setup the rx/tx rings, including the following:
	 *
	 * 1. Setup the descriptor ring and the control block buffers;
	 * 2. Initialize necessary registers for receive/transmit;
	 * 3. Initialize software pointers/parameters for receive/transmit;
	 */
	igbvf_setup_rx(igbvf);

	igbvf_setup_tx(igbvf);
}

static void
igbvf_setup_rx_ring(igbvf_rx_ring_t *rx_ring)
{
	igbvf_t *igbvf = rx_ring->igbvf;
	igbvf_rx_data_t *rx_data = rx_ring->rx_data;
	struct e1000_hw *hw = &igbvf->hw;
	rx_control_block_t *rcb;
	union e1000_adv_rx_desc	*rbd;
	uint32_t size;
	uint32_t buf_low;
	uint32_t buf_high;
	uint32_t rxdctl;
	int i;

	ASSERT(mutex_owned(&rx_ring->rx_lock));
	ASSERT(mutex_owned(&igbvf->gen_lock));

	/*
	 * Initialize descriptor ring with buffer addresses
	 */
	for (i = 0; i < igbvf->rx_ring_size; i++) {
		rcb = rx_data->work_list[i];
		rbd = &rx_data->rbd_ring[i];

		rbd->read.pkt_addr = rcb->rx_buf.dma_address;
		rbd->read.hdr_addr = NULL;
	}

	/*
	 * Initialize the base address registers
	 */
	buf_low = (uint32_t)rx_data->rbd_area.dma_address;
	buf_high = (uint32_t)(rx_data->rbd_area.dma_address >> 32);
	E1000_WRITE_REG(hw, E1000_RDBAH(rx_ring->queue), buf_high);
	E1000_WRITE_REG(hw, E1000_RDBAL(rx_ring->queue), buf_low);

	/*
	 * Initialize the length register
	 */
	size = rx_data->ring_size * sizeof (union e1000_adv_rx_desc);
	E1000_WRITE_REG(hw, E1000_RDLEN(rx_ring->queue), size);

	/*
	 * Initialize buffer size & descriptor type
	 */
	E1000_WRITE_REG(hw, E1000_SRRCTL(rx_ring->queue),
	    ((igbvf->rx_buf_size >> E1000_SRRCTL_BSIZEPKT_SHIFT) |
	    E1000_SRRCTL_DESCTYPE_ADV_ONEBUF));

	/*
	 * Setup the Receive Descriptor Control Register (RXDCTL)
	 */
	rxdctl = E1000_READ_REG(hw, E1000_RXDCTL(rx_ring->queue));
	rxdctl &= igbvf->capab->rxdctl_mask;
	rxdctl |= E1000_RXDCTL_QUEUE_ENABLE;
	rxdctl |= 16;		/* pthresh */
	rxdctl |= 8 << 8;	/* hthresh */
	rxdctl |= 1 << 16;	/* wthresh */
	E1000_WRITE_REG(hw, E1000_RXDCTL(rx_ring->queue), rxdctl);

	/*
	 * Initialize ring head & tail pointers.
	 * This must be done after the rx ring is enabled.
	 */
	E1000_WRITE_REG(hw, E1000_RDH(rx_ring->queue), 0);
	E1000_WRITE_REG(hw, E1000_RDT(rx_ring->queue), rx_data->ring_size - 1);

	rx_data->rbd_next = 0;
}

static void
igbvf_setup_rx(igbvf_t *igbvf)
{
	int i;

	ASSERT(mutex_owned(&igbvf->gen_lock));

	/*
	 * Set up all rx descriptor rings - must be called after indexes set up
	 * and before receive unit enabled.
	 */
	for (i = 0; i < igbvf->num_rx_rings; i++) {
		igbvf_setup_rx_ring(&igbvf->rx_rings[i]);
	}

	/*
	 * Request the PF to set mtu
	 *
	 * The possible failure of the function is ignored. When it fails,
	 * the default value (9k) of VMOLR.RLPML will still allow the VF
	 * working with the current mtu.
	 */
	(void) igbvf_set_mtu(igbvf, igbvf->default_mtu);
}

#define	E1000_TX_HEAD_WB_ENABLE		0x1 /* Tx Desc head writeback */
#define	E1000_DCA_TXCTRL_TX_WB_RO_EN	(1 << 11) /* Tx Desc writeback RO bit */

static void
igbvf_setup_tx_ring(igbvf_tx_ring_t *tx_ring)
{
	igbvf_t *igbvf = tx_ring->igbvf;
	struct e1000_hw *hw = &igbvf->hw;
	uint32_t size;
	uint32_t buf_low;
	uint32_t buf_high;
	uint32_t reg_val;

	ASSERT(mutex_owned(&tx_ring->tx_lock));
	ASSERT(mutex_owned(&igbvf->gen_lock));

	/*
	 * Initialize the length register
	 */
	size = tx_ring->ring_size * sizeof (union e1000_adv_tx_desc);
	E1000_WRITE_REG(hw, E1000_TDLEN(tx_ring->index), size);

	/*
	 * Initialize the base address registers
	 */
	buf_low = (uint32_t)tx_ring->tbd_area.dma_address;
	buf_high = (uint32_t)(tx_ring->tbd_area.dma_address >> 32);
	E1000_WRITE_REG(hw, E1000_TDBAL(tx_ring->index), buf_low);
	E1000_WRITE_REG(hw, E1000_TDBAH(tx_ring->index), buf_high);

	/*
	 * Setup head & tail pointers
	 */
	E1000_WRITE_REG(hw, E1000_TDH(tx_ring->index), 0);
	E1000_WRITE_REG(hw, E1000_TDT(tx_ring->index), 0);

	/*
	 * Setup head write-back
	 */
	if (igbvf->tx_head_wb_enable) {
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
		buf_low |= E1000_TX_HEAD_WB_ENABLE;

		E1000_WRITE_REG(hw, E1000_TDWBAL(tx_ring->index), buf_low);
		E1000_WRITE_REG(hw, E1000_TDWBAH(tx_ring->index), buf_high);

		/*
		 * Turn off relaxed ordering for head write back or it will
		 * cause problems with the tx recycling
		 */
		reg_val = E1000_READ_REG(hw,
		    E1000_DCA_TXCTRL(tx_ring->index));
		reg_val &= ~E1000_DCA_TXCTRL_TX_WB_RO_EN;
		E1000_WRITE_REG(hw,
		    E1000_DCA_TXCTRL(tx_ring->index), reg_val);
	} else {
		tx_ring->tbd_head_wb = NULL;
	}

	tx_ring->tbd_head = 0;
	tx_ring->tbd_tail = 0;
	tx_ring->tbd_free = tx_ring->ring_size;

	if (igbvf->tx_ring_init == B_TRUE) {
		tx_ring->tcb_head = 0;
		tx_ring->tcb_tail = 0;
		tx_ring->tcb_free = tx_ring->free_list_size;
	}

	/*
	 * Enable TXDCTL per queue
	 */
	reg_val = E1000_READ_REG(hw, E1000_TXDCTL(tx_ring->index));
	reg_val |= E1000_TXDCTL_QUEUE_ENABLE;
	E1000_WRITE_REG(hw, E1000_TXDCTL(tx_ring->index), reg_val);

	/*
	 * Initialize hardware checksum offload settings
	 */
	bzero(&tx_ring->tx_context, sizeof (tx_context_t));
}

static void
igbvf_setup_tx(igbvf_t *igbvf)
{
	igbvf_tx_ring_t *tx_ring;
	int i;

	ASSERT(mutex_owned(&igbvf->gen_lock));

	for (i = 0; i < igbvf->num_tx_rings; i++) {
		tx_ring = &igbvf->tx_rings[i];
		igbvf_setup_tx_ring(tx_ring);
	}
}

static void
igbvf_disable_rx_ring(igbvf_rx_ring_t *rx_ring)
{
	struct e1000_hw *hw = &rx_ring->igbvf->hw;
	uint32_t rxdctl;

	/*
	 * Setup the Receive Descriptor Control Register (RXDCTL)
	 */
	rxdctl = E1000_READ_REG(hw, E1000_RXDCTL(rx_ring->queue));
	rxdctl &= ~E1000_RXDCTL_QUEUE_ENABLE;
	E1000_WRITE_REG(hw, E1000_RXDCTL(rx_ring->queue), rxdctl);
}

static void
igbvf_disable_tx_ring(igbvf_tx_ring_t *tx_ring)
{
	struct e1000_hw *hw = &tx_ring->igbvf->hw;
	uint32_t txdctl;

	/*
	 * Setup the Transmit Descriptor Control Register (TXDCTL)
	 */
	txdctl = E1000_READ_REG(hw, E1000_TXDCTL(tx_ring->queue));
	txdctl &= ~E1000_TXDCTL_QUEUE_ENABLE;
	E1000_WRITE_REG(hw, E1000_TXDCTL(tx_ring->queue), txdctl);
}

/*
 * igbvf_unicst_init - Initialize the unicast MAC address table
 */
static int
igbvf_unicst_init(igbvf_t *igbvf)
{
	int slot;
	int ret = IGBVF_SUCCESS;

	ASSERT(mutex_owned(&igbvf->gen_lock));

	/*
	 * The mac addresses for this VF are maintained in a local list.
	 * If any mac address is enabled, request PF to configure the
	 * mac address in hardware.
	 */
	for (slot = 0; slot < igbvf->unicst_total; slot++) {
		if (igbvf->unicst_addr[slot].set == 1) {
			ret = igbvf_enable_mac_addr(igbvf,
			    igbvf->unicst_addr[slot].addr);
			if (ret != IGBVF_SUCCESS) {
				IGBVF_DEBUGLOG_0(igbvf, "igbvf_unicst_init: "
				    "failed to enable address");
				break;
			}
		}
	}

	return (ret);
}

/*
 * igbvf_unicst_add - Add given unicast MAC address to list
 */
int
igbvf_unicst_add(igbvf_t *igbvf, const uint8_t *mac_addr)
{
	int slot;

	ASSERT(mutex_owned(&igbvf->gen_lock));

	/*
	 * Validate the address: check if the address is already in the list.
	 */
	slot = igbvf_unicst_find(igbvf, mac_addr);

	if (slot == IGBVF_FAILURE) {
		/* The address is not valid */
		IGBVF_DEBUGLOG_0(igbvf,
		    "igbvf_unicst_add: invalid address (not configured)");
		return (EINVAL);
	}

	if (igbvf->unicst_addr[slot].set == 1) {
		IGBVF_DEBUGLOG_0(igbvf,
		    "igbvf_unicst_add: address already enabled");
		return (IGBVF_SUCCESS);
	}

	/* Enable this address in the list */
	igbvf->unicst_addr[slot].set = 1;

	if (IGBVF_IS_SUSPENDED_PIO(igbvf)) {
		/* We've saved the setting and it'll take effect in resume */
		return (IGBVF_SUCCESS);
	}

	/* Request PF to enable the mac address */
	if (igbvf_enable_mac_addr(igbvf, mac_addr) != IGBVF_SUCCESS) {
		IGBVF_DEBUGLOG_0(igbvf,
		    "igbvf_unicst_add: failed to enable address");
		igbvf->unicst_addr[slot].set = 0;
		return (EIO);
	}

	return (IGBVF_SUCCESS);
}

/*
 * igbvf_unicst_remove - Remove given unicast MAC address from list
 */
int
igbvf_unicst_remove(igbvf_t *igbvf, const uint8_t *mac_addr)
{
	int slot;

	ASSERT(mutex_owned(&igbvf->gen_lock));

	/*
	 * Validate the address: check if the address is already in the list.
	 */
	slot = igbvf_unicst_find(igbvf, mac_addr);

	/* no match found */
	if (slot == IGBVF_FAILURE) {
		/* The address is not valid */
		IGBVF_DEBUGLOG_0(igbvf,
		    "igbvf_unicst_remove: invalid address (not configured)");
		return (EINVAL);
	}

	if (igbvf->unicst_addr[slot].set == 0) {
		IGBVF_DEBUGLOG_0(igbvf,
		    "igbvf_unicst_remove: address already disabled");
		return (IGBVF_SUCCESS);
	}

	/* Disable the address in the list */
	igbvf->unicst_addr[slot].set = 0;

	if (IGBVF_IS_SUSPENDED_PIO(igbvf)) {
		/* We've saved the setting and it'll take effect in resume */
		return (IGBVF_SUCCESS);
	}

	/* Request PF to disable the address */
	if (igbvf_disable_mac_addr(igbvf, mac_addr) != IGBVF_SUCCESS) {
		IGBVF_DEBUGLOG_0(igbvf,
		    "igbvf_unicst_remove: failed to disable address");
		igbvf->unicst_addr[slot].set = 1;
		return (EIO);
	}

	return (IGBVF_SUCCESS);
}

/*
 * igbvf_unicst_find - Find given unicast MAC address in the list
 * Returns:
 *   found: slot number of unicast address list
 *   not found: IGBVF_FAILURE
 */
static int
igbvf_unicst_find(igbvf_t *igbvf, const uint8_t *mac_addr)
{
	int i, slot;

	ASSERT(mutex_owned(&igbvf->gen_lock));

	/*
	 * Search address list for a match.
	 * The set flag is not checked here.
	 */
	slot = IGBVF_FAILURE;
	for (i = 0; i < igbvf->unicst_total; i++) {
		if (bcmp(igbvf->unicst_addr[i].addr,
		    mac_addr, ETHERADDRL) == 0) {
			slot = i;
			break;
		}
	}

	return (slot);
}

/*
 * igbvf_multicst_add - Add a multicst address
 */
int
igbvf_multicst_add(igbvf_t *igbvf, const uint8_t *multiaddr)
{
	struct ether_addr *new_table;
	size_t new_len;
	size_t old_len;

	ASSERT(mutex_owned(&igbvf->gen_lock));

	if ((multiaddr[0] & 01) == 0) {
		igbvf_error(igbvf, "Illegal multicast address");
		return (EINVAL);
	}

	if (igbvf->mcast_count >= igbvf->mcast_max_num) {
		igbvf_error(igbvf,
		    "Adapter requested more than %d mcast addresses",
		    igbvf->mcast_max_num);
		return (ENOENT);
	}

	if (igbvf->mcast_count == igbvf->mcast_alloc_count) {
		old_len = igbvf->mcast_alloc_count *
		    sizeof (struct ether_addr);
		new_len = (igbvf->mcast_alloc_count + MCAST_ALLOC_COUNT) *
		    sizeof (struct ether_addr);

		new_table = kmem_zalloc(new_len, KM_NOSLEEP);
		if (new_table == NULL) {
			igbvf_error(igbvf,
			    "Not enough memory to alloc mcast table");
			return (ENOMEM);
		}

		if (igbvf->mcast_table != NULL) {
			bcopy(igbvf->mcast_table, new_table, old_len);
			kmem_free(igbvf->mcast_table, old_len);
		}
		igbvf->mcast_alloc_count += MCAST_ALLOC_COUNT;
		igbvf->mcast_table = new_table;
	}

	bcopy(multiaddr,
	    &igbvf->mcast_table[igbvf->mcast_count], ETHERADDRL);
	igbvf->mcast_count++;

	return (0);
}

/*
 * igbvf_multicst_remove - Remove a multicst address
 */
int
igbvf_multicst_remove(igbvf_t *igbvf, const uint8_t *multiaddr)
{
	struct ether_addr *new_table;
	size_t new_len;
	size_t old_len;
	int i;

	ASSERT(mutex_owned(&igbvf->gen_lock));

	for (i = 0; i < igbvf->mcast_count; i++) {
		if (bcmp(multiaddr, &igbvf->mcast_table[i],
		    ETHERADDRL) == 0) {
			for (i++; i < igbvf->mcast_count; i++) {
				igbvf->mcast_table[i - 1] =
				    igbvf->mcast_table[i];
			}
			igbvf->mcast_count--;
			break;
		}
	}

	if ((igbvf->mcast_alloc_count - igbvf->mcast_count) >
	    MCAST_ALLOC_COUNT) {
		old_len = igbvf->mcast_alloc_count *
		    sizeof (struct ether_addr);
		new_len = (igbvf->mcast_alloc_count - MCAST_ALLOC_COUNT) *
		    sizeof (struct ether_addr);

		new_table = kmem_alloc(new_len, KM_NOSLEEP);
		if (new_table != NULL) {
			bcopy(igbvf->mcast_table, new_table, new_len);
			kmem_free(igbvf->mcast_table, old_len);
			igbvf->mcast_alloc_count -= MCAST_ALLOC_COUNT;
			igbvf->mcast_table = new_table;
		}
	}

	return (0);
}

static void
igbvf_release_multicast(igbvf_t *igbvf)
{
	if (igbvf->mcast_table != NULL) {
		kmem_free(igbvf->mcast_table,
		    igbvf->mcast_alloc_count * sizeof (struct ether_addr));
		igbvf->mcast_table = NULL;
	}
}

/*
 * igbvf_setup_multicast - setup multicast data structures
 *
 * This routine initializes all of the multicast related structures
 * and save them in the hardware registers.
 */
int
igbvf_setup_multicst(igbvf_t *igbvf)
{
	uint8_t *mc_addr_list;
	uint32_t mc_addr_count;
	int ret;

	ASSERT(mutex_owned(&igbvf->gen_lock));
	ASSERT(igbvf->mcast_count <= igbvf->mcast_max_num);

	mc_addr_list = (uint8_t *)igbvf->mcast_table;
	mc_addr_count = igbvf->mcast_count;

	if (mc_addr_list == NULL)
		return (IGBVF_SUCCESS);

	/*
	 * Request PF to update the multicast addresses
	 */
	ret = igbvf_set_multicast_addrs(igbvf, mc_addr_list, mc_addr_count);
	if (ret != IGBVF_SUCCESS)
		IGBVF_DEBUGLOG_0(igbvf, "Failed to set multicast addresses");

	return (ret);
}

/*
 * igbvf_get_conf - Get driver configurations set in driver.conf
 *
 * This routine gets user-configured values out of the configuration
 * file igbvf.conf.
 *
 * For each configurable value, there is a minimum, a maximum, and a
 * default.
 * If user does not configure a value, use the default.
 * If user configures below the minimum, use the minumum.
 * If user configures above the maximum, use the maxumum.
 */
static void
igbvf_get_conf(igbvf_t *igbvf)
{
	int diff;
	int i, rc;

	/*
	 * igbvf driver supports the following user configurations:
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
	 * Call igbvf_get_prop() to get the value for a specific
	 * configuration parameter.
	 */

	/*
	 * Jumbo frame configurations
	 */
	igbvf->default_mtu = igbvf_get_prop(igbvf, PROP_DEFAULT_MTU,
	    igbvf->min_mtu, igbvf->max_mtu, DEFAULT_MTU);

	igbvf->max_frame_size = igbvf->default_mtu +
	    sizeof (struct ether_vlan_header) + ETHERFCSL;

	/*
	 * Multiple rings configurations
	 */
	igbvf->tx_ring_size = igbvf_get_prop(igbvf, PROP_TX_RING_SIZE,
	    MIN_TX_RING_SIZE, MAX_TX_RING_SIZE, DEFAULT_TX_RING_SIZE);
	igbvf->rx_ring_size = igbvf_get_prop(igbvf, PROP_RX_RING_SIZE,
	    MIN_RX_RING_SIZE, MAX_RX_RING_SIZE, DEFAULT_RX_RING_SIZE);

	igbvf->num_rx_groups = igbvf_get_prop(igbvf, PROP_RX_GROUP_NUM,
	    igbvf->capab->min_rx_group_num,
	    igbvf->capab->max_rx_group_num,
	    igbvf->capab->def_rx_group_num);

	igbvf->num_rx_rings = igbvf_get_prop(igbvf, PROP_RX_QUEUE_NUM,
	    igbvf->capab->min_rx_que_num,
	    igbvf->max_rx_rings,
	    igbvf->capab->def_rx_que_num);

	igbvf->num_tx_rings = igbvf_get_prop(igbvf, PROP_TX_QUEUE_NUM,
	    igbvf->capab->min_tx_que_num,
	    igbvf->max_tx_rings,
	    igbvf->capab->def_tx_que_num);

	/* Adjust tx/rx ring numbers based on MSI-X vector number */
	diff = (igbvf->num_rx_rings + igbvf->num_tx_rings + 1) -
	    igbvf->intr_cnt;

	if (diff < 0) {
		/*
		 * Extra MSI-X vectors are allocated. The unnecessary
		 * vectors need to be freed.
		 */
		for (i = (igbvf->intr_cnt + diff); i < igbvf->intr_cnt; i++) {
			rc = ddi_intr_free(igbvf->htable[i]);
			if (rc != DDI_SUCCESS) {
				IGBVF_DEBUGLOG_2(igbvf,
				    "Failed to free intr vector %d: %d", i, rc);
			}
		}
		igbvf->intr_cnt += diff;
	} else if (diff > 0) {
		if (diff == 1) {
			igbvf_log(igbvf,
			    "Tx queue 0 is forced to share MSI-X vector "
			    "with other interrupt");
		} else if (diff < igbvf->num_tx_rings) {
			igbvf_log(igbvf,
			    "MSI-X vectors force Tx queue number to %d",
			    igbvf->num_tx_rings - diff);
			igbvf->num_tx_rings -= diff;
		} else {
			/*
			 * In this case, assign one vector for one tx ring;
			 * all left vectors for rx rings; no separate vector
			 * for "other", which has to share the tx vector.
			 */
			igbvf_log(igbvf,
			    "MSI-X vectors force Tx queue number to 1");
			igbvf->num_tx_rings = 1;

			igbvf->num_rx_rings = igbvf->intr_cnt - 1;
			igbvf_log(igbvf,
			    "MSI-X vectors force Rx queue number to %d",
			    igbvf->num_rx_rings);
		}
	}

	igbvf->tx_hcksum_enable = igbvf_get_prop(igbvf, PROP_TX_HCKSUM_ENABLE,
	    0, 1, 1);
	igbvf->rx_hcksum_enable = igbvf_get_prop(igbvf, PROP_RX_HCKSUM_ENABLE,
	    0, 1, 1);
	igbvf->lso_enable = igbvf_get_prop(igbvf, PROP_LSO_ENABLE,
	    0, 1, 1);
	igbvf->tx_head_wb_enable = igbvf_get_prop(igbvf, PROP_TX_HEAD_WB_ENABLE,
	    0, 1, 1);

	/*
	 * LSO needs the tx h/w checksum support.
	 * Here LSO will be disabled if tx h/w checksum has been disabled.
	 */
	if (igbvf->tx_hcksum_enable == B_FALSE)
		igbvf->lso_enable = B_FALSE;

	igbvf->tx_copy_thresh = igbvf_get_prop(igbvf,
	    PROP_TX_COPY_THRESHOLD,
	    MIN_TX_COPY_THRESHOLD,
	    MAX_TX_COPY_THRESHOLD,
	    DEFAULT_TX_COPY_THRESHOLD);
	igbvf->tx_recycle_thresh = igbvf_get_prop(igbvf,
	    PROP_TX_RECYCLE_THRESHOLD,
	    MIN_TX_RECYCLE_THRESHOLD,
	    MAX_TX_RECYCLE_THRESHOLD,
	    DEFAULT_TX_RECYCLE_THRESHOLD);
	igbvf->tx_overload_thresh = igbvf_get_prop(igbvf,
	    PROP_TX_OVERLOAD_THRESHOLD,
	    MIN_TX_OVERLOAD_THRESHOLD,
	    MAX_TX_OVERLOAD_THRESHOLD,
	    DEFAULT_TX_OVERLOAD_THRESHOLD);
	igbvf->tx_resched_thresh = igbvf_get_prop(igbvf,
	    PROP_TX_RESCHED_THRESHOLD,
	    MIN_TX_RESCHED_THRESHOLD,
	    MIN(igbvf->tx_ring_size, MAX_TX_RESCHED_THRESHOLD),
	    igbvf->tx_ring_size > DEFAULT_TX_RESCHED_THRESHOLD ?
	    DEFAULT_TX_RESCHED_THRESHOLD : DEFAULT_TX_RESCHED_THRESHOLD_LOW);

	igbvf->rx_copy_thresh = igbvf_get_prop(igbvf,
	    PROP_RX_COPY_THRESHOLD,
	    MIN_RX_COPY_THRESHOLD,
	    MAX_RX_COPY_THRESHOLD,
	    DEFAULT_RX_COPY_THRESHOLD);
	igbvf->rx_limit_per_intr = igbvf_get_prop(igbvf,
	    PROP_RX_LIMIT_PER_INTR,
	    MIN_RX_LIMIT_PER_INTR,
	    MAX_RX_LIMIT_PER_INTR,
	    DEFAULT_RX_LIMIT_PER_INTR);

	igbvf->intr_throttling[0] = igbvf_get_prop(igbvf,
	    PROP_INTR_THROTTLING,
	    igbvf->capab->min_intr_throttle,
	    igbvf->capab->max_intr_throttle,
	    igbvf->capab->def_intr_throttle);

	/*
	 * Max number of multicast addresses
	 */
	igbvf->mcast_max_num =
	    igbvf_get_prop(igbvf, PROP_MCAST_MAX_NUM,
	    MIN_MCAST_NUM, MAX_MCAST_NUM, DEFAULT_MCAST_NUM);
}

/*
 * igbvf_get_prop - Get a property value out of the conf file igbvf.conf
 *
 * Caller provides the name of the property, a default value, a minimum
 * value, and a maximum value.
 *
 * Return configured value of the property, with default, minimum and
 * maximum properly applied.
 */
static int
igbvf_get_prop(igbvf_t *igbvf,
    char *propname,	/* name of the property */
    int minval,		/* minimum acceptable value */
    int maxval,		/* maximim acceptable value */
    int defval)		/* default value */
{
	int value;

	/*
	 * Call ddi_prop_get_int() to read the conf settings
	 */
	value = ddi_prop_get_int(DDI_DEV_T_ANY, igbvf->dip,
	    DDI_PROP_DONTPASS, propname, defval);

	if (value > maxval)
		value = maxval;

	if (value < minval)
		value = minval;

	return (value);
}

/*
 * igbvf_is_link_up - Check if the link is up
 */
static boolean_t
igbvf_is_link_up(igbvf_t *igbvf)
{
	struct e1000_hw *hw = &igbvf->hw;
	boolean_t link_up = B_TRUE;
	int ret;

	ASSERT(mutex_owned(&igbvf->gen_lock));

	hw->mac.get_link_status = B_TRUE;
	ret = igbvf_check_for_link_vf(igbvf);
	link_up = !hw->mac.get_link_status;

	/*
	 * Make sure that it is at least 10 milliseconds since last reset
	 * to avoid reset storm. The 10-millisecond value was determined
	 * experimentally.
	 */
	if (ret && ticks_later(igbvf->last_reset, drv_usectohz(10000))) {
		atomic_or_32(&igbvf->igbvf_state, IGBVF_RESET);
	}

	return (link_up);
}

/*
 * igbvf_link_check - Link status processing
 */
static void
igbvf_link_check(void *arg)
{
	igbvf_t *igbvf = (igbvf_t *)arg;
	struct e1000_hw *hw = &igbvf->hw;
	struct e1000_mac_info *mac = &igbvf->hw.mac;
	uint16_t speed = 0, duplex = 0;
	boolean_t link_changed = B_FALSE;
	boolean_t link_up;

	mutex_enter(&igbvf->gen_lock);

	if (IGBVF_IS_SUSPENDED(igbvf)) {
		mutex_exit(&igbvf->gen_lock);
		return;
	}

	link_up = igbvf_is_link_up(igbvf);

	if (IGBVF_IS_STARTED(igbvf)) {
		/*
		 * The link change is only checked after started
		 */
		if (link_up) {
			if (igbvf->link_state != LINK_STATE_UP) {
				(void) mac->ops.get_link_up_info(hw,
				    &speed, &duplex);
				igbvf->link_speed = speed;
				igbvf->link_duplex = duplex;
				igbvf->link_state = LINK_STATE_UP;
				link_changed = B_TRUE;
			}
		} else {
			if (igbvf->link_state != LINK_STATE_DOWN) {
				igbvf->link_speed = 0;
				igbvf->link_duplex = 0;
				igbvf->link_state = LINK_STATE_DOWN;
				link_changed = B_TRUE;
			}
		}
	}

	if (igbvf_check_acc_handle(igbvf->osdep.reg_handle) != DDI_FM_OK) {
		mutex_exit(&igbvf->gen_lock);
		ddi_fm_service_impact(igbvf->dip, DDI_SERVICE_DEGRADED);
		return;
	}

	mutex_exit(&igbvf->gen_lock);

	if (link_changed)
		mac_link_update(igbvf->mac_hdl, igbvf->link_state);
}

/*
 * igbvf_local_timer - driver watchdog function
 *
 * This function will handle the hardware stall check, link status
 * check and other routines.
 */
static void
igbvf_local_timer(void *arg)
{
	igbvf_t *igbvf = (igbvf_t *)arg;

	(void) igbvf_update_stats(igbvf->igbvf_ks, KSTAT_READ);

	if ((igbvf->igbvf_state & IGBVF_RESET) ||
	    (igbvf->igbvf_state & IGBVF_ERROR) ||
	    igbvf_stall_check(igbvf)) {
		if ((ddi_taskq_dispatch(igbvf->timer_taskq, igbvf_reset,
		    (void *)igbvf, DDI_NOSLEEP)) != DDI_SUCCESS) {
			igbvf_log(igbvf,
			    "Failed to dispatch timer taskq for reset");
			goto restart_timer;
		}
		igbvf->reset_count++;
		return;
	}

	if ((ddi_taskq_dispatch(igbvf->timer_taskq, igbvf_link_check,
	    (void *)igbvf, DDI_NOSLEEP)) != DDI_SUCCESS) {
		igbvf_log(igbvf,
		    "Failed to dispatch timer taskq for link check");
	}

restart_timer:
	igbvf_restart_watchdog_timer(igbvf);
}

/*
 * igbvf_stall_check - check for transmit stall
 *
 * This function checks if the adapter is stalled (in transmit).
 *
 * It is called each time the watchdog timeout is invoked.
 * If the transmit descriptor reclaim continuously fails,
 * the watchdog value will increment by 1. If the watchdog
 * value exceeds the threshold, the adapter is assumed to
 * have stalled and need to be reset.
 */
static boolean_t
igbvf_stall_check(igbvf_t *igbvf)
{
	igbvf_tx_ring_t *tx_ring;
	boolean_t result;
	int i;

	if (!IGBVF_IS_STARTED(igbvf) ||
	    (igbvf->link_state != LINK_STATE_UP))
		return (B_FALSE);

	/*
	 * If any tx ring is stalled, we'll reset the adapter
	 */
	result = B_FALSE;
	for (i = 0; i < igbvf->num_tx_rings; i++) {
		tx_ring = &igbvf->tx_rings[i];

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
		igbvf_fm_ereport(igbvf, DDI_FM_DEVICE_STALL);
		ddi_fm_service_impact(igbvf->dip, DDI_SERVICE_DEGRADED);
	}

	return (result);
}

/*
 * is_valid_mac_addr - Check if the mac address is valid
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

#pragma inline(igbvf_arm_watchdog_timer)

static void
igbvf_arm_watchdog_timer(igbvf_t *igbvf)
{
	/*
	 * Fire a watchdog timer
	 */
	igbvf->watchdog_tid =
	    timeout(igbvf_local_timer,
	    (void *)igbvf, 1 * drv_usectohz(1000000));

}

/*
 * igbvf_enable_watchdog_timer - Enable and start the driver watchdog timer
 */
void
igbvf_enable_watchdog_timer(igbvf_t *igbvf)
{
	mutex_enter(&igbvf->watchdog_lock);

	if (!igbvf->watchdog_enable) {
		igbvf->watchdog_enable = B_TRUE;
		igbvf_arm_watchdog_timer(igbvf);
	}

	mutex_exit(&igbvf->watchdog_lock);

}

/*
 * igbvf_disable_watchdog_timer - Disable and stop the driver watchdog timer
 */
void
igbvf_disable_watchdog_timer(igbvf_t *igbvf)
{
	timeout_id_t tid;

	mutex_enter(&igbvf->watchdog_lock);

	igbvf->watchdog_enable = B_FALSE;
	tid = igbvf->watchdog_tid;
	igbvf->watchdog_tid = 0;

	mutex_exit(&igbvf->watchdog_lock);

	if (tid != 0)
		(void) untimeout(tid);

}

/*
 * igbvf_restart_watchdog_timer - Restart the driver watchdog timer
 */
static void
igbvf_restart_watchdog_timer(igbvf_t *igbvf)
{
	mutex_enter(&igbvf->watchdog_lock);

	if (igbvf->watchdog_enable)
		igbvf_arm_watchdog_timer(igbvf);

	mutex_exit(&igbvf->watchdog_lock);
}

/*
 * igbvf_disable_adapter_interrupts - Clear/disable all hardware interrupts
 */
static void
igbvf_disable_adapter_interrupts(igbvf_t *igbvf)
{
	struct e1000_hw *hw = &igbvf->hw;

	/* mask all "other" interrupts */
	E1000_WRITE_REG(hw, E1000_IMC, ~0);
	E1000_WRITE_REG(hw, E1000_IAM, 0);

	/* mask all rx/tx interrupts */
	E1000_WRITE_REG(hw, E1000_EIMC, ~0);
	E1000_WRITE_REG(hw, E1000_EIAC, 0);
	E1000_WRITE_REG(hw, E1000_EIAM, 0);

	E1000_WRITE_FLUSH(hw);
}

/*
 * igbvf_enable_adapter_interrupts_82576 - Enable NIC interrupts for 82576
 * Only MSI-X interrupts are used.
 */
static void
igbvf_enable_adapter_interrupts_82576(igbvf_t *igbvf)
{
	struct e1000_hw *hw = &igbvf->hw;

	/* Enable interrupts and autoclear */
	E1000_WRITE_REG(hw, E1000_EIMS, igbvf->eims_mask);
	E1000_WRITE_REG(hw, E1000_EIAC, igbvf->eims_mask);

	/* Disable auto-mask */
	E1000_WRITE_REG(hw, E1000_EIAM, 0);

	E1000_WRITE_FLUSH(hw);
}

static void
igbvf_enable_mailbox_interrupt(igbvf_t *igbvf)
{
	struct e1000_hw *hw = &igbvf->hw;

	/* Disable all rx/tx vectors except vector 0 */
	E1000_WRITE_REG(hw, E1000_EIMC, ~0x1);
	E1000_WRITE_REG(hw, E1000_EIMS, 0x1);
	E1000_WRITE_REG(hw, E1000_EIAC, 0x1);

	/* Disable auto-mask */
	E1000_WRITE_REG(hw, E1000_EIAM, 0);

	E1000_WRITE_FLUSH(hw);
}

#pragma inline(igbvf_intr_rx_work)
/*
 * igbvf_intr_rx_work - rx processing of ISR
 */
static void
igbvf_intr_rx_work(igbvf_rx_ring_t *rx_ring)
{
	igbvf_t *igbvf = rx_ring->igbvf;
	mblk_t *mp;

	mutex_enter(&rx_ring->rx_lock);

	if ((igbvf->igbvf_state & (IGBVF_STARTED | IGBVF_SUSPENDED_TX_RX |
	    IGBVF_ERROR)) != IGBVF_STARTED) {
		mutex_exit(&rx_ring->rx_lock);
		return;
	}

	mp = igbvf_rx(rx_ring, IGBVF_NO_POLL, IGBVF_NO_POLL);

	mutex_exit(&rx_ring->rx_lock);

	if (mp != NULL)
		mac_rx_ring(igbvf->mac_hdl, rx_ring->ring_handle, mp,
		    rx_ring->ring_gen_num);
}

#pragma inline(igbvf_intr_tx_work)
/*
 * igbvf_intr_tx_work - tx processing of ISR
 */
static void
igbvf_intr_tx_work(igbvf_tx_ring_t *tx_ring)
{
	igbvf_t *igbvf = tx_ring->igbvf;

	if ((igbvf->igbvf_state & (IGBVF_STARTED | IGBVF_SUSPENDED_TX_RX)) !=
	    IGBVF_STARTED) {
		return;
	}

	/* Recycle the tx descriptors */
	tx_ring->tx_recycle(tx_ring);

	/* Schedule the re-transmit */
	if (tx_ring->reschedule &&
	    (tx_ring->tbd_free >= igbvf->tx_resched_thresh)) {
		tx_ring->reschedule = B_FALSE;
		mac_tx_ring_update(igbvf->mac_hdl,
		    tx_ring->ring_handle);
		IGBVF_DEBUG_STAT(tx_ring->stat_reschedule);
	}
}

/*
 * igbvf_intr_rx - Interrupt handler for rx
 */
static uint_t
igbvf_intr_rx(void *arg1, void *arg2)
{
	igbvf_rx_ring_t *rx_ring = (igbvf_rx_ring_t *)arg1;

	_NOTE(ARGUNUSED(arg2));

	/*
	 * Only used via MSI-X vector so don't check cause bits
	 * and only clean the given ring.
	 */
	if (!IGBVF_IS_SUSPENDED_INTR(rx_ring->igbvf))
		igbvf_intr_rx_work(rx_ring);

	return (DDI_INTR_CLAIMED);
}

/*
 * igbvf_intr_tx - Interrupt handler for tx
 */
static uint_t
igbvf_intr_tx(void *arg1, void *arg2)
{
	igbvf_tx_ring_t *tx_ring = (igbvf_tx_ring_t *)arg1;

	_NOTE(ARGUNUSED(arg2));

	/*
	 * Only used via MSI-X vector so don't check cause bits
	 * and only clean the given ring.
	 */
	if (!IGBVF_IS_SUSPENDED_INTR(tx_ring->igbvf))
		igbvf_intr_tx_work(tx_ring);

	return (DDI_INTR_CLAIMED);
}

/*
 * igbvf_intr_other - Interrupt handler for other (mailbox) interrupt
 *
 */
static uint_t
igbvf_intr_other(void *arg1, void *arg2)
{
	igbvf_t *igbvf = (igbvf_t *)arg1;

	_NOTE(ARGUNUSED(arg2));

	if (IGBVF_IS_SUSPENDED_INTR(igbvf))
		return (DDI_INTR_CLAIMED);

	if ((ddi_taskq_dispatch(igbvf->mbx_taskq,
	    igbvf_rcv_msg, (void *)igbvf, DDI_NOSLEEP)) != DDI_SUCCESS) {
		igbvf_log(igbvf, "Failed to dispatch mailbox taskq");
	}

	return (DDI_INTR_CLAIMED);
}

/*
 * igbvf_intr_tx_other - Interrupt handler for both tx and other
 *
 */
static uint_t
igbvf_intr_tx_other(void *arg1, void *arg2)
{
	igbvf_t *igbvf = (igbvf_t *)arg1;

	_NOTE(ARGUNUSED(arg2));

	if (IGBVF_IS_SUSPENDED_INTR(igbvf))
		return (DDI_INTR_CLAIMED);

	/*
	 * Look for tx reclaiming work first.
	 */
	igbvf_intr_tx_work(&igbvf->tx_rings[0]);

	/*
	 * Check for mailbox message
	 */
	if ((ddi_taskq_dispatch(igbvf->mbx_taskq,
	    igbvf_rcv_msg, (void *)igbvf, DDI_NOSLEEP)) != DDI_SUCCESS) {
		igbvf_log(igbvf, "Failed to dispatch mailbox taskq");
	}

	return (DDI_INTR_CLAIMED);
}

/*
 * igbvf_alloc_intrs - Allocate interrupts for the driver
 *
 * Only MSI-X is supported for igb VF
 */
static int
igbvf_alloc_intrs(igbvf_t *igbvf)
{
	dev_info_t *devinfo;
	int intr_types;
	int rc;

	devinfo = igbvf->dip;

	/* Get supported interrupt types */
	rc = ddi_intr_get_supported_types(devinfo, &intr_types);

	if (rc != DDI_SUCCESS) {
		igbvf_log(igbvf,
		    "Get supported interrupt types failed: %d", rc);
		return (IGBVF_FAILURE);
	}

	igbvf->intr_type = 0;

	/* Install MSI-X interrupts */
	if (intr_types & DDI_INTR_TYPE_MSIX)
		rc = igbvf_alloc_intr_handles(igbvf, DDI_INTR_TYPE_MSIX);

	return (rc);
}

/*
 * igbvf_alloc_intr_handles - Allocate interrupt handles.
 */
static int
igbvf_alloc_intr_handles(igbvf_t *igbvf, int intr_type)
{
	dev_info_t *devinfo;
	int request, count, avail, actual;
	int minimum;
	int rc;

	devinfo = igbvf->dip;

	switch (intr_type) {

	case DDI_INTR_TYPE_MSIX:
		/*
		 * Request max allowed number of vectors, which is 3 currently.
		 * 1 for rx ring + 1 for tx ring + 1 for "other"
		 */
		request = igbvf->capab->max_intr_vec;
		minimum = 2;
		break;

	default:
		igbvf_log(igbvf,
		    "invalid call to igbvf_alloc_intr_handles(): %d",
		    intr_type);
		return (IGBVF_FAILURE);
	}
	IGBVF_DEBUGLOG_2(igbvf, "interrupt handles requested: %d  minimum: %d",
	    request, minimum);

	/*
	 * Get number of supported interrupts
	 */
	rc = ddi_intr_get_nintrs(devinfo, intr_type, &count);
	if ((rc != DDI_SUCCESS) || (count < minimum)) {
		igbvf_log(igbvf,
		    "Get supported interrupt number failed. "
		    "Return: %d, count: %d", rc, count);
		return (IGBVF_FAILURE);
	}
	IGBVF_DEBUGLOG_1(igbvf,
	    "igbvf_alloc_intr_handles: interrupts supported: %d",
	    count);

	/*
	 * Get number of available interrupts
	 */
	rc = ddi_intr_get_navail(devinfo, intr_type, &avail);
	if ((rc != DDI_SUCCESS) || (avail < minimum)) {
		igbvf_log(igbvf,
		    "Get available interrupt number failed. "
		    "Return: %d, available: %d", rc, avail);
		return (IGBVF_FAILURE);
	}
	IGBVF_DEBUGLOG_1(igbvf, "interrupts available: %d", avail);

	if (avail < request) {
		igbvf_log(igbvf, "Request %d handles, %d available",
		    request, avail);
		request = avail;
	}

	actual = 0;
	igbvf->intr_cnt = 0;

	/*
	 * Allocate an array of interrupt handles
	 */
	igbvf->intr_size = request * sizeof (ddi_intr_handle_t);
	igbvf->htable = kmem_alloc(igbvf->intr_size, KM_SLEEP);

	rc = ddi_intr_alloc(devinfo, igbvf->htable, intr_type, 0,
	    request, &actual, DDI_INTR_ALLOC_NORMAL);
	if (rc != DDI_SUCCESS) {
		igbvf_log(igbvf, "Allocate interrupts failed. "
		    "return: %d, request: %d, actual: %d",
		    rc, request, actual);
		goto alloc_handle_fail;
	}
	IGBVF_DEBUGLOG_1(igbvf, "interrupts actually allocated: %d", actual);

	igbvf->intr_cnt = actual;

	if (actual < minimum) {
		igbvf_log(igbvf, "Insufficient interrupt handles allocated: %d",
		    actual);
		goto alloc_handle_fail;
	}

	/*
	 * Get priority for first vector, assume remaining are all the same
	 */
	rc = ddi_intr_get_pri(igbvf->htable[0], &igbvf->intr_pri);
	if (rc != DDI_SUCCESS) {
		igbvf_log(igbvf,
		    "Get interrupt priority failed: %d", rc);
		goto alloc_handle_fail;
	}

	rc = ddi_intr_get_cap(igbvf->htable[0], &igbvf->intr_cap);
	if (rc != DDI_SUCCESS) {
		igbvf_log(igbvf,
		    "Get interrupt cap failed: %d", rc);
		goto alloc_handle_fail;
	}

	igbvf->intr_type = intr_type;

	return (IGBVF_SUCCESS);

alloc_handle_fail:
	igbvf_rem_intrs(igbvf);

	return (IGBVF_FAILURE);
}

/*
 * igbvf_add_intr_handlers - Add interrupt handlers based on the interrupt type
 *
 * Before adding the interrupt handlers, the interrupt vectors have
 * been allocated, and the rx/tx rings have also been allocated.
 */
static int
igbvf_add_intr_handlers(igbvf_t *igbvf)
{
	igbvf_rx_ring_t *rx_ring;
	igbvf_tx_ring_t *tx_ring;
	ddi_intr_handler_t *handler;
	boolean_t shared;
	int vector;
	int rc;
	int i;

	/* reject non-MSI-X type */
	if (igbvf->intr_type != DDI_INTR_TYPE_MSIX) {
		return (IGBVF_FAILURE);
	}

	/*
	 * Check if the "other" interrupt and the first tx interrupt
	 * have to share one interrupt vector.
	 */
	shared = ((igbvf->num_rx_rings + igbvf->num_tx_rings) ==
	    igbvf->intr_cnt);
	if (shared)
		handler = (ddi_intr_handler_t *)igbvf_intr_tx_other;
	else
		handler = (ddi_intr_handler_t *)igbvf_intr_other;

	/* Add interrupt handler for other or tx + other on vector 0 */
	vector = 0;
	rc = ddi_intr_add_handler(igbvf->htable[vector],
	    handler, (void *)igbvf, NULL);

	if (rc != DDI_SUCCESS) {
		igbvf_log(igbvf,
		    "Add tx/other interrupt handler failed: %d", rc);
		return (IGBVF_FAILURE);
	}

	if (shared)
		igbvf->tx_rings[0].intr_vect = vector;
	vector++;

	/* Add interrupt handler for each rx ring */
	for (i = 0; i < igbvf->num_rx_rings; i++) {
		rx_ring = &igbvf->rx_rings[i];

		rc = ddi_intr_add_handler(igbvf->htable[vector],
		    (ddi_intr_handler_t *)igbvf_intr_rx,
		    (void *)rx_ring, NULL);

		if (rc != DDI_SUCCESS) {
			igbvf_log(igbvf,
			    "Add rx interrupt handler failed. "
			    "return: %d, rx ring: %d", rc, 0);
			for (vector--; vector >= 0; vector--) {
				(void) ddi_intr_remove_handler(
				    igbvf->htable[vector]);
			}
			return (IGBVF_FAILURE);
		}
		rx_ring->intr_vect = vector;
		vector++;
	}

	/*
	 * Add interrupt handler for each tx ring.
	 * Because the first tx ring shares interrupt with the "other"
	 * interrupt, start from the second ring.
	 */
	for (i = (shared ? 1 : 0); i < igbvf->num_tx_rings; i++) {
		tx_ring = &igbvf->tx_rings[i];

		rc = ddi_intr_add_handler(igbvf->htable[vector],
		    (ddi_intr_handler_t *)igbvf_intr_tx,
		    (void *)tx_ring, NULL);

		if (rc != DDI_SUCCESS) {
			igbvf_log(igbvf,
			    "Add tx interrupt handler failed. "
			    "return: %d, tx ring: %d", rc, i);
			for (vector--; vector >= 0; vector--) {
				(void) ddi_intr_remove_handler(
				    igbvf->htable[vector]);
			}
			return (IGBVF_FAILURE);
		}
		tx_ring->intr_vect = vector;
		vector++;
	}

	ASSERT(vector == igbvf->intr_cnt);

	return (IGBVF_SUCCESS);
}

/*
 * igbvf_setup_msix_82576 - setup 82576 adapter to use MSI-X interrupts
 *
 * 82576 uses a table based method for assigning vectors.  Each queue has a
 * single entry in the table to which we write a vector number along with a
 * "valid" bit.  The entry is a single byte in a 4-byte register.  Vectors
 * take a different position in the 4-byte register depending on whether
 * they are numbered above or below 8.
 */
static void
igbvf_setup_msix_82576(igbvf_t *igbvf)
{
	struct e1000_hw *hw = &igbvf->hw;
	uint32_t ivar, index, que, vector;
	int i;

	/*
	 * This is also interdependent with installation of interrupt service
	 * routines in igbvf_add_intr_handlers().
	 */

	/* assign "other" causes to vector 0 */
	vector = 0;
	ivar = (vector | E1000_IVAR_VALID);
	E1000_WRITE_REG(hw, E1000_IVAR_MISC, ivar);

	/* accumulate all interrupt-cause bits in the mask */
	igbvf->eims_mask = (1 << vector);

	for (i = 0; i < igbvf->num_rx_rings; i++) {
		/*
		 * Set vector for each rx ring
		 */
		vector = igbvf->rx_rings[i].intr_vect;
		que = igbvf->rx_rings[i].queue;
		index = (que / 2);
		ivar = E1000_READ_REG_ARRAY(hw, E1000_IVAR0, index);

		if (que & 0x1) {
			/* vector goes into third byte of register */
			ivar &= 0xFF00FFFF;
			ivar |= ((vector | E1000_IVAR_VALID) << 16);
		} else {
			/* vector goes into low byte of register */
			ivar &= 0xFFFFFF00;
			ivar |= (vector | E1000_IVAR_VALID);
		}
		E1000_WRITE_REG_ARRAY(hw, E1000_IVAR0, index, ivar);

		/* Accumulate interrupt-cause bits to enable */
		igbvf->eims_mask |= (1 << vector);
	}

	for (i = 0; i < igbvf->num_tx_rings; i++) {
		/*
		 * Set vector for each tx ring
		 */
		vector = igbvf->tx_rings[i].intr_vect;
		que = igbvf->tx_rings[i].queue;
		index = (que / 2);
		ivar = E1000_READ_REG_ARRAY(hw, E1000_IVAR0, index);

		if (que & 0x1) {
			/* vector goes into high byte of register */
			ivar &= 0x00FFFFFF;
			ivar |= ((vector | E1000_IVAR_VALID) << 24);
		} else {
			/* vector goes into second byte of register */
			ivar &= 0xFFFF00FF;
			ivar |= (vector | E1000_IVAR_VALID) << 8;
		}
		E1000_WRITE_REG_ARRAY(hw, E1000_IVAR0, index, ivar);

		/* Accumulate interrupt-cause bits to enable */
		igbvf->eims_mask |= (1 << vector);
	}
}

/*
 * igbvf_rem_intr_handlers - remove the interrupt handlers
 */
static void
igbvf_rem_intr_handlers(igbvf_t *igbvf)
{
	int i;
	int rc;

	for (i = 0; i < igbvf->intr_cnt; i++) {
		rc = ddi_intr_remove_handler(igbvf->htable[i]);
		if (rc != DDI_SUCCESS) {
			IGBVF_DEBUGLOG_1(igbvf,
			    "Remove intr handler failed: %d", rc);
		}
	}
}

/*
 * igbvf_rem_intrs - remove the allocated interrupts
 */
static void
igbvf_rem_intrs(igbvf_t *igbvf)
{
	int i;
	int rc;

	for (i = 0; i < igbvf->intr_cnt; i++) {
		rc = ddi_intr_free(igbvf->htable[i]);
		if (rc != DDI_SUCCESS) {
			IGBVF_DEBUGLOG_1(igbvf,
			    "Free intr failed: %d", rc);
		}
	}

	kmem_free(igbvf->htable, igbvf->intr_size);
	igbvf->htable = NULL;
}

/*
 * igbvf_enable_intrs - enable all the ddi interrupts
 */
static int
igbvf_enable_intrs(igbvf_t *igbvf)
{
	int i;
	int rc;

	/* Enable interrupts */
	if (igbvf->intr_cap & DDI_INTR_FLAG_BLOCK) {
		/* Call ddi_intr_block_enable() for MSI */
		rc = ddi_intr_block_enable(igbvf->htable, igbvf->intr_cnt);
		if (rc != DDI_SUCCESS) {
			igbvf_log(igbvf,
			    "Enable block intr failed: %d", rc);
			return (IGBVF_FAILURE);
		}
	} else {
		/* Call ddi_intr_enable() for Legacy/MSI non block enable */
		for (i = 0; i < igbvf->intr_cnt; i++) {
			rc = ddi_intr_enable(igbvf->htable[i]);
			if (rc != DDI_SUCCESS) {
				igbvf_log(igbvf,
				    "Enable intr vector %d failed: %d", i, rc);
				goto enable_intrs_fail;
			}
		}
	}

	return (IGBVF_SUCCESS);

enable_intrs_fail:
	for (i--; i >= 0; i--)
		(void) ddi_intr_disable(igbvf->htable[i]);

	return (IGBVF_FAILURE);
}

/*
 * igbvf_disable_intrs - disable all the ddi interrupts
 */
static int
igbvf_disable_intrs(igbvf_t *igbvf)
{
	int i;
	int rc;

	/* Disable all interrupts */
	if (igbvf->intr_cap & DDI_INTR_FLAG_BLOCK) {
		rc = ddi_intr_block_disable(igbvf->htable, igbvf->intr_cnt);
		if (rc != DDI_SUCCESS) {
			igbvf_log(igbvf,
			    "Disable block intr failed: %d", rc);
			return (IGBVF_FAILURE);
		}
	} else {
		for (i = 0; i < igbvf->intr_cnt; i++) {
			rc = ddi_intr_disable(igbvf->htable[i]);
			if (rc != DDI_SUCCESS) {
				igbvf_log(igbvf,
				    "Disable intr vector %d failed: %d", i, rc);
				goto disable_intrs_fail;
			}
		}
	}

	return (IGBVF_SUCCESS);

disable_intrs_fail:
	for (i--; i >= 0; i--)
		(void) ddi_intr_enable(igbvf->htable[i]);

	return (IGBVF_FAILURE);
}

/*
 * igbvf_atomic_reserve - Atomic decrease operation
 */
int
igbvf_atomic_reserve(uint32_t *count_p, uint32_t n)
{
	uint32_t oldval;
	uint32_t newval;

	/* ATOMICALLY */
	do {
		oldval = *count_p;
		if (oldval < n)
			return (-1);
		newval = oldval - n;
	} while (atomic_cas_32(count_p, oldval, newval) != oldval);

	return (newval);
}

/*
 * FMA support
 */

int
igbvf_check_acc_handle(ddi_acc_handle_t handle)
{
	ddi_fm_error_t de;

	ddi_fm_acc_err_get(handle, &de, DDI_FME_VERSION);
	ddi_fm_acc_err_clear(handle, DDI_FME_VERSION);
	return (de.fme_status);
}

int
igbvf_check_dma_handle(ddi_dma_handle_t handle)
{
	ddi_fm_error_t de;

	ddi_fm_dma_err_get(handle, &de, DDI_FME_VERSION);
	return (de.fme_status);
}

/*
 * The IO fault service error handling callback function
 */
/*ARGSUSED*/
static int
igbvf_fm_error_cb(dev_info_t *dip, ddi_fm_error_t *err, const void *impl_data)
{
	/*
	 * as the driver can always deal with an error in any dma or
	 * access handle, we can just return the fme_status value.
	 */
	pci_ereport_post(dip, err, NULL);
	return (err->fme_status);
}

static void
igbvf_fm_init(igbvf_t *igbvf)
{
	ddi_iblock_cookie_t iblk;
	int fma_dma_flag;

	/* Only register with IO Fault Services if we have some capability */
	if (igbvf->fm_capabilities & DDI_FM_ACCCHK_CAPABLE) {
		igbvf_regs_acc_attr.devacc_attr_access = DDI_FLAGERR_ACC;
	} else {
		igbvf_regs_acc_attr.devacc_attr_access = DDI_DEFAULT_ACC;
	}

	if (igbvf->fm_capabilities & DDI_FM_DMACHK_CAPABLE) {
		fma_dma_flag = 1;
	} else {
		fma_dma_flag = 0;
	}

	(void) igbvf_set_fma_flags(fma_dma_flag);

	if (igbvf->fm_capabilities) {

		/* Register capabilities with IO Fault Services */
		ddi_fm_init(igbvf->dip, &igbvf->fm_capabilities, &iblk);

		/*
		 * Initialize pci ereport capabilities if ereport capable
		 */
		if (DDI_FM_EREPORT_CAP(igbvf->fm_capabilities) ||
		    DDI_FM_ERRCB_CAP(igbvf->fm_capabilities))
			pci_ereport_setup(igbvf->dip);

		/*
		 * Register error callback if error callback capable
		 */
		if (DDI_FM_ERRCB_CAP(igbvf->fm_capabilities))
			ddi_fm_handler_register(igbvf->dip,
			    igbvf_fm_error_cb, (void*) igbvf);
	}
}

static void
igbvf_fm_fini(igbvf_t *igbvf)
{
	/* Only unregister FMA capabilities if we registered some */
	if (igbvf->fm_capabilities) {

		/*
		 * Release any resources allocated by pci_ereport_setup()
		 */
		if (DDI_FM_EREPORT_CAP(igbvf->fm_capabilities) ||
		    DDI_FM_ERRCB_CAP(igbvf->fm_capabilities))
			pci_ereport_teardown(igbvf->dip);

		/*
		 * Un-register error callback if error callback capable
		 */
		if (DDI_FM_ERRCB_CAP(igbvf->fm_capabilities))
			ddi_fm_handler_unregister(igbvf->dip);

		/* Unregister from IO Fault Services */
		ddi_fm_fini(igbvf->dip);
	}
}

void
igbvf_fm_ereport(igbvf_t *igbvf, char *detail)
{
	uint64_t ena;
	char buf[FM_MAX_CLASS];

	(void) snprintf(buf, FM_MAX_CLASS, "%s.%s", DDI_FM_DEVICE, detail);
	ena = fm_ena_generate(0, FM_ENA_FMT1);
	if (DDI_FM_EREPORT_CAP(igbvf->fm_capabilities)) {
		ddi_fm_ereport_post(igbvf->dip, buf, ena, DDI_NOSLEEP,
		    FM_VERSION, DATA_TYPE_UINT8, FM_EREPORT_VERS0, NULL);
	}
}

static int
igbvf_cb_suspend_handler(igbvf_t *igbvf, uint_t activities, uint_t impacts)
{
	if (activities & ~IGBVF_CB_LSR_ACTIVITIES) {
		cmn_err(CE_WARN, "Invalid cb_activities: 0x%x",
		    (int)activities);
		return (DDI_ENOTSUP);
	}

	if (impacts & ~IGBVF_CB_LSR_IMPACTS) {
		cmn_err(CE_WARN, "Invalid cb_impact: 0x%x",
		    (int)impacts);
		return (DDI_ENOTSUP);
	}

	if (activities & DDI_CB_LSR_ACT_PIO) {
		/*
		 * Suspending all is required
		 */
		impacts |= DDI_CB_LSR_IMP_DEVICE_RESET;
	}

	if (impacts & (DDI_CB_LSR_IMP_DEVICE_RESET |
	    DDI_CB_LSR_IMP_DEVICE_REPLACE |
	    DDI_CB_LSR_IMP_LOSE_POWER)) {
		/*
		 * Suspending all PIO, DMA and interrupt is required
		 */
		activities |= DDI_CB_LSR_ACT_PIO |
		    DDI_CB_LSR_ACT_DMA | DDI_CB_LSR_ACT_INTR;
	}

	if (impacts &
	    (DDI_CB_LSR_IMP_DMA_ADDR_CHANGE |
	    DDI_CB_LSR_IMP_DMA_PROP_CHANGE)) {
		/*
		 * Suspending DMA is required
		 */
		activities |= DDI_CB_LSR_ACT_DMA;
	}

	return (igbvf_suspend(igbvf, activities, impacts));
}


static int
igbvf_cb_resume_handler(igbvf_t *igbvf, uint_t activities, uint_t impacts)
{
	if (activities & ~IGBVF_CB_LSR_ACTIVITIES) {
		cmn_err(CE_WARN, "Invalid cb_activities: 0x%x",
		    (int)activities);
		return (DDI_ENOTSUP);
	}

	if (impacts & ~IGBVF_CB_LSR_IMPACTS) {
		cmn_err(CE_WARN, "Invalid cb_impact: 0x%x",
		    (int)impacts);
		return (DDI_ENOTSUP);
	}

	if (activities & DDI_CB_LSR_ACT_PIO) {
		/*
		 * Suspending all is required
		 */
		impacts |= DDI_CB_LSR_IMP_DEVICE_RESET;
	}

	if (impacts & (DDI_CB_LSR_IMP_DEVICE_RESET |
	    DDI_CB_LSR_IMP_DEVICE_REPLACE |
	    DDI_CB_LSR_IMP_LOSE_POWER)) {
		/*
		 * Suspending all PIO, DMA and interrupt is required
		 */
		activities |= DDI_CB_LSR_ACT_PIO |
		    DDI_CB_LSR_ACT_DMA | DDI_CB_LSR_ACT_INTR;
	}

	if (impacts &
	    (DDI_CB_LSR_IMP_DMA_ADDR_CHANGE |
	    DDI_CB_LSR_IMP_DMA_PROP_CHANGE)) {
		/*
		 * Suspending DMA is required
		 */
		activities |= DDI_CB_LSR_ACT_DMA;
	}

	return (igbvf_resume(igbvf, activities, impacts));
}

/*
 * Callback function
 */
static int
igbvf_cbfunc(dev_info_t *dip, ddi_cb_action_t cbaction, void *cbarg,
    void *arg1, void *arg2)
{
	igbvf_t *igbvf;
	ddi_cb_lsr_t *lsrp;
	int retval;

	 _NOTE(ARGUNUSED(arg1, arg2));

	igbvf = (igbvf_t *)ddi_get_driver_private(dip);
	if (igbvf == NULL)
		return (DDI_FAILURE);

	switch (cbaction) {
	case DDI_CB_LSR_QUERY_CAPABILITY:
		lsrp = (ddi_cb_lsr_t *)cbarg;
		lsrp->activities = IGBVF_CB_LSR_ACTIVITIES;
		lsrp->impacts = IGBVF_CB_LSR_IMPACTS;
		retval = DDI_SUCCESS;
		break;
	case DDI_CB_LSR_SUSPEND:
		lsrp = (ddi_cb_lsr_t *)cbarg;
		retval = igbvf_cb_suspend_handler(igbvf, lsrp->activities,
		    lsrp->impacts);
		break;
	case DDI_CB_LSR_RESUME:
		lsrp = (ddi_cb_lsr_t *)cbarg;
		retval = igbvf_cb_resume_handler(igbvf, lsrp->activities,
		    lsrp->impacts);
		break;
	default:
		return (DDI_ENOTSUP);
	}

	return (retval);
}

static int
igbvf_init_promisc(igbvf_t *igbvf)
{
	int ret;

	ASSERT(mutex_owned(&igbvf->gen_lock));

	/*
	 * Request only multicast promiscuous.
	 */
	ret = igbvf_set_mcast_promisc(igbvf, igbvf->promisc_mode);
	if (ret != IGBVF_SUCCESS) {
		IGBVF_DEBUGLOG_0(igbvf,
		    "Failed to set multicast promiscuous mode");
	}

	return (ret);
}

/*
 * igbvf_init_msg_api - initialize the API used for PF-VF messages
 *
 * This routine will:
 * - do a function-level reset of this virtual function (equivalent to
 *     resetting the adapter hardware)
 * - negotiate message API version
 * - get default MAC address and any additional MAC addresses
 * - get number of configured tx/rx rings
 * - get configured bounds on MTU
 */
static int
igbvf_init_msg_api(igbvf_t *igbvf)
{
	mutex_enter(&igbvf->gen_lock);

	/* this will do a function-level reset */
	if (igbvf_init_default_mac_address(igbvf) != IGBVF_SUCCESS) {
		mutex_exit(&igbvf->gen_lock);
		igbvf_error(igbvf, "Failed to initialize default MAC address");
		return (IGBVF_FAILURE);
	}

	/* negotiate message API version */
	if (igbvf_nego_msg_api(igbvf) != IGBVF_SUCCESS) {
		mutex_exit(&igbvf->gen_lock);
		igbvf_error(igbvf, "Compatible message API not found");
		return (IGBVF_FAILURE);
	}

	/* Get any additional MAC addresses */
	(void) igbvf_get_mac_addrs(igbvf);

	/* Get configured queue limits */
	(void) igbvf_get_que_limits(igbvf);

	/* Get configured mtu limits */
	(void) igbvf_get_mtu_limits(igbvf);

	mutex_exit(&igbvf->gen_lock);

	return (IGBVF_SUCCESS);
}

/*
 * igbvf_nego_msg_api - negotiate the version of the message API
 * This version of igbvf only supports message api 2.0, so only request
 * 2.0 and fail if response is negative.
 */
static int
igbvf_nego_msg_api(igbvf_t *igbvf)
{
	uint32_t msg[3];
	clock_t time_stop;
	int ret;

	ASSERT(mutex_owned(&igbvf->gen_lock));

	/* Wait for up to 1 second for other mailbox transactions to complete */
	time_stop = ddi_get_lbolt() + SEC_TO_TICK(1);
	while (igbvf->mbx_hold) {
		ret = cv_timedwait(&igbvf->mbx_hold_cv,
		    &igbvf->gen_lock, time_stop);
		if (ret == -1)
			break;
	}

	if (igbvf->mbx_hold) {
		IGBVF_DEBUGLOG_0(igbvf, "igbvf_nego_msg_api: "
		    "Time out waiting other mailbox transaction to complete");
		return (IGBVF_FAILURE);
	}

	igbvf->mbx_hold = B_TRUE;

	/* set up message to request api 2.0 */
	msg[0] = E1000_VF_API_NEGOTIATE;
	msg[1] = e1000_mbox_api_20;
	msg[2] = 0;

	/* send message */
	ret = igbvf_write_posted_mbx(igbvf, msg, 3);
	if (ret) {
		ret = IGBVF_FAILURE;
		goto nego_api_end;
	}

	/* read response */
	ret = igbvf_read_posted_mbx(igbvf, msg, 3);
	if (ret) {
		ret = IGBVF_FAILURE;
		goto nego_api_end;
	}

	/* fail if don't get both ACK and the requested api version */
	if (!(msg[0] & E1000_VT_MSGTYPE_ACK) || (msg[1] != e1000_mbox_api_20)) {
		ret = IGBVF_FAILURE;
		goto nego_api_end;
	}

	ret = IGBVF_SUCCESS;

nego_api_end:
	igbvf->mbx_hold = B_FALSE;
	cv_broadcast(&igbvf->mbx_hold_cv);

	return (ret);
}

/*
 * igbvf_get_mac_addrs - Get any MAC adresses in addition to the default
 *
 * This code should compare the number of addresses in whole list
 * against the number received in the first response and possibly generate
 * more requests. Also with multiple requests, there can be a NACK from
 * PF in which case this should start the whole sequence over again.  Address
 * list is not complete until the whole sequence has been completed with no
 * NAKs.
 */
static int
igbvf_get_mac_addrs(igbvf_t *igbvf)
{
	uint32_t msg[16];
	uint8_t *addr;
	uint32_t offset, total, start, number;
	clock_t time_stop;
	int i, ret;

	ASSERT(mutex_owned(&igbvf->gen_lock));

	/* Wait for up to 1 second for other mailbox transactions to complete */
	time_stop = ddi_get_lbolt() + SEC_TO_TICK(1);
	while (igbvf->mbx_hold) {
		ret = cv_timedwait(&igbvf->mbx_hold_cv,
		    &igbvf->gen_lock, time_stop);
		if (ret == -1)
			break;
	}

	if (igbvf->mbx_hold) {
		IGBVF_DEBUGLOG_0(igbvf, "igbvf_get_mac_addrs: "
		    "Time out waiting other mailbox transaction to complete");
		return (IGBVF_FAILURE);
	}

	igbvf->mbx_hold = B_TRUE;

	offset = 0;

get_mac_again:
	/* set up message to request additional MAC addresses */
	(void) memset(msg, 0, sizeof (msg));
	msg[0] = E1000_VF_GET_MACADDRS;
	msg[1] = offset;

	/* send message, 2 longwords */
	ret = igbvf_write_posted_mbx(igbvf, msg, 2);
	if (ret) {
		ret = IGBVF_FAILURE;
		goto get_mac_end;
	}

	/* read response, up to 16 longwords */
	ret = igbvf_read_posted_mbx(igbvf, msg, 16);
	if (ret) {
		ret = IGBVF_FAILURE;
		goto get_mac_end;
	}

	/* test for ACK of message */
	if (!(msg[0] & E1000_VT_MSGTYPE_ACK)) {
		IGBVF_DEBUGLOG_0(igbvf, "Addresses changed, retry...");
		igbvf->unicst_total -= offset;
		offset = 0;
		for (i = igbvf->unicst_total;
		    i < igbvf->capab->max_unicst_rar; i++)
			bzero(igbvf->unicst_addr[i].addr, ETHERADDRL);
		goto get_mac_again;
	}

	total = msg[1];
	start = msg[2];
	number = msg[3];
	addr = (uint8_t *)&msg[4];

	if (number == 0) {
		IGBVF_DEBUGLOG_0(igbvf, "No available alternative addresses");
		ret = IGBVF_SUCCESS;
		goto get_mac_end;
	}

	if ((total >= igbvf->capab->max_unicst_rar) ||
	    (start >= igbvf->capab->max_unicst_rar) ||
	    (number >= igbvf->capab->max_unicst_rar) ||
	    (start != offset)) {
		igbvf_error(igbvf, "invalid mac address info: "
		    "total = %d, start = %d, number = %d",
		    total, start, number);
		ret = IGBVF_FAILURE;
		goto get_mac_end;
	}

	/* advance the offset */
	offset += number;

	/*
	 * Save mac addresses to the unicst_addr[] array.
	 * The first slot is reserved for the default mac address.
	 */
	for (i = (start + 1); i <= offset; i++) {
		bcopy(addr, igbvf->unicst_addr[i].addr, ETHERADDRL);
		igbvf->unicst_addr[i].set = 0;

		addr += ETHERADDRL;
	}
	igbvf->unicst_total += number;

	/* If there are more addresses to get, send another request */
	if (offset < total)
		goto get_mac_again;

	ret = IGBVF_SUCCESS;

get_mac_end:
	igbvf->mbx_hold = B_FALSE;
	cv_broadcast(&igbvf->mbx_hold_cv);

	return (ret);
}

/*
 * igbvf_get_que_limits - Get the configured limit number of tx and rx queues
 */
static int
igbvf_get_que_limits(igbvf_t *igbvf)
{
	uint32_t msg[4];
	clock_t time_stop;
	int ret;

	ASSERT(mutex_owned(&igbvf->gen_lock));

	/* Wait for up to 1 second for other mailbox transactions to complete */
	time_stop = ddi_get_lbolt() + SEC_TO_TICK(1);
	while (igbvf->mbx_hold) {
		ret = cv_timedwait(&igbvf->mbx_hold_cv,
		    &igbvf->gen_lock, time_stop);
		if (ret == -1)
			break;
	}

	if (igbvf->mbx_hold) {
		IGBVF_DEBUGLOG_0(igbvf, "igbvf_get_que_limits: "
		    "Time out waiting other mailbox transaction to complete");
		return (IGBVF_FAILURE);
	}

	igbvf->mbx_hold = B_TRUE;

	/* Initialize queue limits */
	igbvf->max_tx_rings = igbvf->capab->max_tx_que_num;
	igbvf->max_rx_rings = igbvf->capab->max_rx_que_num;

	/* set up message to request queue limits */
	msg[0] = E1000_VF_GET_QUEUES;
	msg[1] = 0;
	msg[2] = 0;
	msg[3] = 0;

	/* send message */
	ret = igbvf_write_posted_mbx(igbvf, msg, 4);
	if (ret) {
		ret = IGBVF_FAILURE;
		goto get_que_end;
	}

	/* read response */
	ret = igbvf_read_posted_mbx(igbvf, msg, 4);
	if (ret) {
		ret = IGBVF_FAILURE;
		goto get_que_end;
	}

	/*
	 * for success require ACK of the message and at least one each
	 * transmit and receive queues
	 */
	if (!(msg[0] & E1000_VT_MSGTYPE_ACK)) {
		ret = IGBVF_FAILURE;
		goto get_que_end;
	}

	if ((msg[1] < igbvf->capab->min_tx_que_num) ||
	    (msg[1] > igbvf->capab->max_tx_que_num) ||
	    (msg[2] < igbvf->capab->min_rx_que_num) ||
	    (msg[2] > igbvf->capab->max_rx_que_num)) {
		igbvf_error(igbvf, "Invalid queue number limits: "
		    "max tx queues = %d, max rx queues = %d", msg[1], msg[2]);
		ret = IGBVF_FAILURE;
		goto get_que_end;

	}

	/* Save queue limits */
	igbvf->max_tx_rings = msg[1];
	igbvf->max_rx_rings = msg[2];
	/* Get transparent vlan indication */
	igbvf->transparent_vlan_enable = msg[3];

	ret = IGBVF_SUCCESS;

get_que_end:
	igbvf->mbx_hold = B_FALSE;
	cv_broadcast(&igbvf->mbx_hold_cv);

	return (ret);
}

/*
 * igbvf_get_mtu_limits - Get the configured limits on MTU
 */
static int
igbvf_get_mtu_limits(igbvf_t *igbvf)
{
	uint32_t msg[3];
	clock_t time_stop;
	int ret;

	ASSERT(mutex_owned(&igbvf->gen_lock));

	/* Wait for up to 1 second for other mailbox transactions to complete */
	time_stop = ddi_get_lbolt() + SEC_TO_TICK(1);
	while (igbvf->mbx_hold) {
		ret = cv_timedwait(&igbvf->mbx_hold_cv,
		    &igbvf->gen_lock, time_stop);
		if (ret == -1)
			break;
	}

	if (igbvf->mbx_hold) {
		IGBVF_DEBUGLOG_0(igbvf, "igbvf_get_mtu_limits: "
		    "Time out waiting other mailbox transaction to complete");
		return (IGBVF_FAILURE);
	}

	igbvf->mbx_hold = B_TRUE;

	/* Initialize mtu limits */
	igbvf->min_mtu = MIN_MTU;
	igbvf->max_mtu = MAX_MTU;

	/* set up message to request mtu limits */
	msg[0] = E1000_VF_GET_MTU;
	msg[1] = 0;
	msg[2] = 0;

	/* send message */
	ret = igbvf_write_posted_mbx(igbvf, msg, 3);
	if (ret) {
		ret = IGBVF_FAILURE;
		goto get_mtu_end;
	}

	/* read response */
	ret = igbvf_read_posted_mbx(igbvf, msg, 3);
	if (ret) {
		ret = IGBVF_FAILURE;
		goto get_mtu_end;
	}

	/*
	 * for success require ACK of the message and
	 * valid values for lower and upper bounds
	 */
	if (!(msg[0] & E1000_VT_MSGTYPE_ACK)) {
		ret = IGBVF_FAILURE;
		goto get_mtu_end;
	}

	if ((msg[1] < MIN_MTU) || (msg[1] > MAX_MTU) ||
	    (msg[2] < MIN_MTU) || (msg[2] > MAX_MTU)) {
		igbvf_error(igbvf, "Invalid MTU limits: "
		    "min_mtu = %d, max_mtu = %d", msg[1], msg[2]);
		ret = IGBVF_FAILURE;
		goto get_mtu_end;
	}

	/* Save mtu limits */
	igbvf->min_mtu = msg[1];
	igbvf->max_mtu = msg[2];

	ret = IGBVF_SUCCESS;

get_mtu_end:
	igbvf->mbx_hold = B_FALSE;
	cv_broadcast(&igbvf->mbx_hold_cv);

	return (ret);
}

/*
 * igbvf_set_mtu - Set the given MTU
 */
static int
igbvf_set_mtu(igbvf_t *igbvf, uint32_t mtu)
{
	uint32_t msg[2];
	clock_t time_stop;
	int ret;

	ASSERT(mutex_owned(&igbvf->gen_lock));

	/* Wait for up to 1 second for other mailbox transactions to complete */
	time_stop = ddi_get_lbolt() + SEC_TO_TICK(1);
	while (igbvf->mbx_hold) {
		ret = cv_timedwait(&igbvf->mbx_hold_cv,
		    &igbvf->gen_lock, time_stop);
		if (ret == -1)
			break;
	}

	if (igbvf->mbx_hold) {
		IGBVF_DEBUGLOG_0(igbvf, "igbvf_set_mtu: "
		    "Time out waiting other mailbox transaction to complete");
		return (IGBVF_FAILURE);
	}

	igbvf->mbx_hold = B_TRUE;

	/* set up message to request mtu */
	msg[0] = E1000_VF_SET_MTU;
	msg[1] = mtu;

	/* send message */
	ret = igbvf_write_posted_mbx(igbvf, msg, 2);
	if (ret) {
		ret = IGBVF_FAILURE;
		goto set_mtu_end;
	}

	/* read response */
	ret = igbvf_read_posted_mbx(igbvf, msg, 1);
	if (ret) {
		ret = IGBVF_FAILURE;
		goto set_mtu_end;
	}

	/*
	 * for success require ACK of the message
	 */
	if (!(msg[0] & E1000_VT_MSGTYPE_ACK)) {
		ret = IGBVF_FAILURE;
		goto set_mtu_end;
	}

	ret = IGBVF_SUCCESS;

set_mtu_end:
	igbvf->mbx_hold = B_FALSE;
	cv_broadcast(&igbvf->mbx_hold_cv);

	return (ret);
}

/*
 * igbvf_enable_mac_addr - Request PF to enable given MAC address
 */
static int
igbvf_enable_mac_addr(igbvf_t *igbvf, const uint8_t *addr)
{
	uint32_t msg[3];
	uint8_t *msg_addr = (uint8_t *)(&msg[1]);
	clock_t time_stop;
	int ret;

	ASSERT(mutex_owned(&igbvf->gen_lock));

	/* Wait for up to 1 second for other mailbox transactions to complete */
	time_stop = ddi_get_lbolt() + SEC_TO_TICK(1);
	while (igbvf->mbx_hold) {
		ret = cv_timedwait(&igbvf->mbx_hold_cv,
		    &igbvf->gen_lock, time_stop);
		if (ret == -1)
			break;
	}

	if (igbvf->mbx_hold) {
		IGBVF_DEBUGLOG_0(igbvf, "igbvf_enable_mac_addr: "
		    "Time out waiting other mailbox transaction to complete");
		return (IGBVF_FAILURE);
	}

	igbvf->mbx_hold = B_TRUE;

	/* set up message */
	(void) memset(msg, 0, sizeof (msg));
	msg[0] = E1000_VF_ENABLE_MACADDR;
	(void) memcpy(msg_addr, addr, ETHERADDRL);

	/* send message */
	ret = igbvf_write_posted_mbx(igbvf, msg, 3);
	if (ret) {
		ret = IGBVF_FAILURE;
		goto enable_mac_end;
	}

	/* read response */
	ret = igbvf_read_posted_mbx(igbvf, msg, 1);
	if (ret) {
		ret = IGBVF_FAILURE;
		goto enable_mac_end;
	}

	/* require ACK of the message */
	if (!(msg[0] & E1000_VT_MSGTYPE_ACK)) {
		ret = IGBVF_FAILURE;
		goto enable_mac_end;
	}

	ret = IGBVF_SUCCESS;

enable_mac_end:
	igbvf->mbx_hold = B_FALSE;
	cv_broadcast(&igbvf->mbx_hold_cv);

	return (ret);
}

/*
 * igbvf_disable_mac_addr - Request PF to disable given MAC address
 */
static int
igbvf_disable_mac_addr(igbvf_t *igbvf, const uint8_t *addr)
{
	uint32_t msg[3];
	uint8_t *msg_addr = (uint8_t *)(&msg[1]);
	clock_t time_stop;
	int ret;

	ASSERT(mutex_owned(&igbvf->gen_lock));

	/* Wait for up to 1 second for other mailbox transactions to complete */
	time_stop = ddi_get_lbolt() + SEC_TO_TICK(1);
	while (igbvf->mbx_hold) {
		ret = cv_timedwait(&igbvf->mbx_hold_cv,
		    &igbvf->gen_lock, time_stop);
		if (ret == -1)
			break;
	}

	if (igbvf->mbx_hold) {
		IGBVF_DEBUGLOG_0(igbvf, "igbvf_disable_mac_addr: "
		    "Time out waiting other mailbox transaction to complete");
		return (IGBVF_FAILURE);
	}

	igbvf->mbx_hold = B_TRUE;

	/* set up message */
	(void) memset(msg, 0, sizeof (msg));
	msg[0] = E1000_VF_DISABLE_MACADDR;
	(void) memcpy(msg_addr, addr, ETHERADDRL);

	/* send message */
	ret = igbvf_write_posted_mbx(igbvf, msg, 3);
	if (ret) {
		ret = IGBVF_FAILURE;
		goto disable_mac_end;
	}

	/* read response */
	ret = igbvf_read_posted_mbx(igbvf, msg, 1);
	if (ret) {
		ret = IGBVF_FAILURE;
		goto disable_mac_end;
	}

	/* require ACK of the message */
	if (!(msg[0] & E1000_VT_MSGTYPE_ACK)) {
		ret = IGBVF_FAILURE;
		goto disable_mac_end;
	}

	ret = IGBVF_SUCCESS;

disable_mac_end:
	igbvf->mbx_hold = B_FALSE;
	cv_broadcast(&igbvf->mbx_hold_cv);

	return (ret);
}

static int
igbvf_set_multicast_addrs(igbvf_t *igbvf,
    const uint8_t *mc_addr_list, uint32_t count)
{
	struct e1000_hw *hw = &igbvf->hw;
	uint32_t msg[E1000_VFMAILBOX_SIZE];
	uint16_t *hash_list = (uint16_t *)&msg[1];
	uint32_t hash_value;
	clock_t time_stop;
	int i, ret;

	ASSERT(mutex_owned(&igbvf->gen_lock));

	/* Wait for up to 1 second for other mailbox transactions to complete */
	time_stop = ddi_get_lbolt() + SEC_TO_TICK(1);
	while (igbvf->mbx_hold) {
		ret = cv_timedwait(&igbvf->mbx_hold_cv,
		    &igbvf->gen_lock, time_stop);
		if (ret == -1)
			break;
	}

	if (igbvf->mbx_hold) {
		IGBVF_DEBUGLOG_0(igbvf, "igbvf_set_multicast_addrs: "
		    "Time out waiting other mailbox transaction to complete");
		return (IGBVF_FAILURE);
	}

	igbvf->mbx_hold = B_TRUE;

	/*
	 * Each entry in the list uses 1 16 bit word.  We have 30
	 * 16 bit words available in our HW msg buffer (minus 1 for the
	 * msg type).  That's 30 hash values if we pack 'em right.  If
	 * there are more than 30 MC addresses to add then punt the
	 * extras for now and then add code to handle more than 30 later.
	 */
	msg[0] = E1000_VF_SET_MULTICAST;

	if (count > 30) {
		msg[0] |= E1000_VF_SET_MULTICAST_OVERFLOW;
		count = 30;
	}

	msg[0] |= count << E1000_VT_MSGINFO_SHIFT;

	for (i = 0; i < count; i++) {
		hash_value = e1000_hash_mc_addr_vf(hw, (u8 *)mc_addr_list);
		hash_list[i] = hash_value & 0x0FFF;
		mc_addr_list += ETHERADDRL;
	}

	ret = igbvf_write_posted_mbx(igbvf, msg, E1000_VFMAILBOX_SIZE);
	if (ret) {
		ret = IGBVF_FAILURE;
		goto multicast_end;
	}

	/* read response */
	ret = igbvf_read_posted_mbx(igbvf, msg, 1);
	if (ret) {
		ret = IGBVF_FAILURE;
		goto multicast_end;
	}

	/* require ACK of the message */
	if (!(msg[0] & E1000_VT_MSGTYPE_ACK)) {
		ret = IGBVF_FAILURE;
		goto multicast_end;
	}

	ret = IGBVF_SUCCESS;

multicast_end:
	igbvf->mbx_hold = B_FALSE;
	cv_broadcast(&igbvf->mbx_hold_cv);

	return (ret);
}

/*
 * igbvf_set_mcast_promisc - Request setting of multicast promiscuous
 */
int
igbvf_set_mcast_promisc(igbvf_t *igbvf, boolean_t enable)
{
	uint32_t msg[2];
	clock_t time_stop;
	int ret;

	ASSERT(mutex_owned(&igbvf->gen_lock));

	/* Wait for up to 1 second for other mailbox transactions to complete */
	time_stop = ddi_get_lbolt() + SEC_TO_TICK(1);
	while (igbvf->mbx_hold) {
		ret = cv_timedwait(&igbvf->mbx_hold_cv,
		    &igbvf->gen_lock, time_stop);
		if (ret == -1)
			break;
	}

	if (igbvf->mbx_hold) {
		IGBVF_DEBUGLOG_0(igbvf, "igbvf_set_mcast_promisc: "
		    "Time out waiting other mailbox transaction to complete");
		return (IGBVF_FAILURE);
	}

	igbvf->mbx_hold = B_TRUE;

	/* set up message to request enable or disable */
	msg[0] = E1000_VF_SET_MCAST_PROMISC;
	msg[1] = enable ? 1 : 0;

	/* send message */
	ret = igbvf_write_posted_mbx(igbvf, msg, 2);
	if (ret) {
		ret = IGBVF_FAILURE;
		goto promisc_end;
	}

	/* read response */
	ret = igbvf_read_posted_mbx(igbvf, msg, 1);
	if (ret) {
		ret = IGBVF_FAILURE;
		goto promisc_end;
	}

	/* require ACK of the message */
	if (!(msg[0] & E1000_VT_MSGTYPE_ACK)) {
		ret = IGBVF_FAILURE;
		goto promisc_end;
	}

	ret = IGBVF_SUCCESS;

promisc_end:
	igbvf->mbx_hold = B_FALSE;
	cv_broadcast(&igbvf->mbx_hold_cv);

	return (ret);
}

static void
igbvf_rcv_msg(void *arg)
{
	igbvf_t *igbvf = (igbvf_t *)arg;
	uint32_t msg[E1000_VFMAILBOX_SIZE];
	uint32_t response;
	clock_t time_stop;
	int ret;

	mutex_enter(&igbvf->gen_lock);

	/* Wait for up to 1 second */
	time_stop = ddi_get_lbolt() + SEC_TO_TICK(1);
	while (igbvf->mbx_hold) {
		ret = cv_timedwait(&igbvf->mbx_hold_cv,
		    &igbvf->gen_lock, time_stop);
		if (ret == -1)
			break;
	}

	if (igbvf->mbx_hold) {
		igbvf_log(igbvf,
		    "Time out waiting other mailbox transaction to complete");
		goto rcv_msg_end;
	}

	/* Try to read a message */
	ret = igbvf_read_mbx_vf(igbvf, msg, E1000_VFMAILBOX_SIZE);
	if (ret) {
		igbvf_log(igbvf, "Failed to receive mailbox message from PF");
		goto rcv_msg_end;
	}

	/* Ignore ACK and NACK */
	if (msg[0] & (E1000_VT_MSGTYPE_ACK | E1000_VT_MSGTYPE_NACK)) {
		goto rcv_msg_end;
	}

	response = E1000_VT_MSGTYPE_ACK;

	switch (msg[0] & 0xFFFF) {
	case E1000_PF_CONTROL_MSG:
		/* PF sent this notification to indicate a reset */
		atomic_or_32(&igbvf->igbvf_state, IGBVF_RESET);
		break;
	case E1000_PF_TRANSPARENT_VLAN:
		igbvf->transparent_vlan_enable = msg[1];
		break;
	default:
		igbvf_log(igbvf, "Unhandled msg %08x from PF", msg[0]);
		response = E1000_VT_MSGTYPE_NACK;
		break;
	}

	/* Notify the PF of the result */
	msg[0] |= response;

	if (igbvf_write_mbx_vf(igbvf, msg, 1)) {
		igbvf_log(igbvf, "Failed to reply mailbox message to PF");
	}

rcv_msg_end:
	mutex_exit(&igbvf->gen_lock);
}

/*
 * igbvf_reset_hw_vf - Resets the HW
 *
 * VF's provide a function level reset. This is done using bit 26 of ctrl_reg.
 * This is all the reset we can perform on a VF.
 */
static int
igbvf_reset_hw_vf(igbvf_t *igbvf)
{
	struct e1000_hw *hw = &igbvf->hw;
	struct e1000_mbx_info *mbx = &hw->mbx;
	uint32_t ctrl, msgbuf[3];
	uint8_t *addr = (uint8_t *)(&msgbuf[1]);
	uint32_t timeout = E1000_VF_INIT_TIMEOUT;
	clock_t time_stop;
	clock_t time_left;
	int ret = -E1000_ERR_MAC_INIT;

	ASSERT(mutex_owned(&igbvf->gen_lock));

	/* Wait for up to 1 second for other mailbox transactions to complete */
	time_stop = ddi_get_lbolt() + SEC_TO_TICK(1);
	while (igbvf->mbx_hold) {
		time_left = cv_timedwait(&igbvf->mbx_hold_cv,
		    &igbvf->gen_lock, time_stop);
		if (time_left == -1)
			break;
	}

	if (igbvf->mbx_hold) {
		IGBVF_DEBUGLOG_0(igbvf, "igbvf_reset_hw_vf: "
		    "Time out waiting other mailbox transaction to complete");
		return (ret);
	}

	igbvf->mbx_hold = B_TRUE;

	ctrl = E1000_READ_REG(hw, E1000_CTRL);
	E1000_WRITE_REG(hw, E1000_CTRL, ctrl | E1000_CTRL_RST);

	/* we cannot reset while the RSTI / RSTD bits are asserted */
	while (!mbx->ops.check_for_rst(hw, 0) && timeout) {
		timeout--;
		usec_delay(5);
	}

	if (timeout) {
		/* mailbox timeout can now become active */
		mbx->timeout = E1000_VF_MBX_INIT_TIMEOUT;

		msgbuf[0] = E1000_VF_RESET;
		ret = igbvf_write_posted_mbx(igbvf, msgbuf, 1);
		if (ret) {
			goto reset_hw_end;
		}

		/* set our "perm_addr" based on info provided by PF */
		ret = igbvf_read_posted_mbx(igbvf, msgbuf, 3);
		if (!ret) {
			if (msgbuf[0] ==
			    (E1000_VF_RESET | E1000_VT_MSGTYPE_ACK))
				(void) memcpy(hw->mac.perm_addr, addr, 6);
			else
				ret = -E1000_ERR_MAC_INIT;
		}
	}

reset_hw_end:
	igbvf->mbx_hold = B_FALSE;
	cv_broadcast(&igbvf->mbx_hold_cv);

	return (ret);
}

/*
 * igbvf_poll_for_msg - Wait for message notification
 */
static int
igbvf_poll_for_msg(igbvf_t *igbvf)
{
	struct e1000_hw *hw = &igbvf->hw;
	struct e1000_mbx_info *mbx = &hw->mbx;
	clock_t countdown = 100;	/* 100 ticks */
	clock_t time_left;
	clock_t time_stop;

	ASSERT(mutex_owned(&igbvf->gen_lock));

	if (!mbx->ops.check_for_msg)
		goto out;

	while (countdown && mbx->ops.check_for_msg(hw, 0)) {
		time_stop = ddi_get_lbolt() + 1;

		time_left = cv_timedwait(&igbvf->mbx_poll_cv,
		    &igbvf->gen_lock, time_stop);

		if (time_left < 0)
			countdown--;
	}

	/* if we failed, all future posted messages fail until reset */
	if (!countdown)
		mbx->timeout = 0;
out:
	return (countdown ? E1000_SUCCESS : -E1000_ERR_MBX);
}

/*
 * igbvf_poll_for_ack - Wait for message acknowledgement
 */
static int
igbvf_poll_for_ack(igbvf_t *igbvf)
{
	struct e1000_hw *hw = &igbvf->hw;
	struct e1000_mbx_info *mbx = &hw->mbx;
	clock_t countdown = 100;	/* 100 ticks */
	clock_t time_left;
	clock_t time_stop;

	ASSERT(mutex_owned(&igbvf->gen_lock));

	if (!mbx->ops.check_for_ack)
		goto out;

	while ((countdown > 0) && mbx->ops.check_for_ack(hw, 0)) {
		time_stop = ddi_get_lbolt() + 1;

		time_left = cv_timedwait(&igbvf->mbx_poll_cv,
		    &igbvf->gen_lock, time_stop);

		if (time_left < 0)
			countdown--;
	}

	/* if we failed, all future posted messages fail until reset */
	if (!countdown)
		mbx->timeout = 0;
out:
	return (countdown ? E1000_SUCCESS : -E1000_ERR_MBX);
}

/*
 * igbvf_read_posted_mbx - Wait for message notification and receive message
 */
static int
igbvf_read_posted_mbx(igbvf_t *igbvf, uint32_t *msg, uint16_t size)
{
	int ret_val = -E1000_ERR_MBX;

	ASSERT(mutex_owned(&igbvf->gen_lock));

	ret_val = igbvf_poll_for_msg(igbvf);

	/* if ack received read message, otherwise we timed out */
	if (!ret_val) {
		ret_val = igbvf_read_mbx_vf(igbvf, msg, size);
	}
out:
	return (ret_val);
}

/*
 * igbvf_write_posted_mbx - Write a message to the mailbox, wait for ack
 */
static int
igbvf_write_posted_mbx(igbvf_t *igbvf, uint32_t *msg, uint16_t size)
{
	int ret_val = -E1000_ERR_MBX;

	ASSERT(mutex_owned(&igbvf->gen_lock));

	/* exit if either we can't write or there isn't a defined timeout */
	if (!igbvf->hw.mbx.timeout)
		goto out;

	/* send msg */
	ret_val = igbvf_write_mbx_vf(igbvf, msg, size);

	/* if msg sent wait until we receive an ack */
	if (!ret_val) {
		ret_val = igbvf_poll_for_ack(igbvf);
	}
out:
	return (ret_val);
}

/*
 * igbvf_obtain_mbx_lock_vf - obtain mailbox lock
 *
 * return SUCCESS if we obtained the mailbox lock
 */
static int
igbvf_obtain_mbx_lock_vf(igbvf_t *igbvf)
{
	struct e1000_hw *hw = &igbvf->hw;
	uint32_t countdown = 200;	/* 1 msec */
	int ret_val = -E1000_ERR_MBX;

	ASSERT(mutex_owned(&igbvf->gen_lock));

	while (countdown) {
		/* Take ownership of the buffer */
		E1000_WRITE_REG(hw, E1000_V2PMAILBOX(0), E1000_V2PMAILBOX_VFU);

		/* reserve mailbox for vf use */
		if (e1000_read_v2p_mailbox(hw) & E1000_V2PMAILBOX_VFU)
			return (E1000_SUCCESS);

		usec_delay(5);
		countdown--;
	}

	return (ret_val);
}

/*
 * igbvf_write_mbx_vf - Write a message to the mailbox
 *
 * returns SUCCESS if it successfully copied message into the buffer
 */
static int
igbvf_write_mbx_vf(igbvf_t *igbvf, uint32_t *msg, uint16_t size)
{
	struct e1000_hw *hw = &igbvf->hw;
	int ret_val = E1000_SUCCESS;
	int i;

	ASSERT(mutex_owned(&igbvf->gen_lock));

	/* limit size to the size of mailbox */
	if (size > hw->mbx.size)
		size = hw->mbx.size;

	/* lock the mailbox to prevent pf/vf race condition */
	ret_val = igbvf_obtain_mbx_lock_vf(igbvf);
	if (ret_val)
		goto out_no_write;

	/* flush msg and acks as we are overwriting the message buffer */
	(void) hw->mbx.ops.check_for_msg(hw, 0);
	(void) hw->mbx.ops.check_for_ack(hw, 0);

	/* copy the caller specified message to the mailbox memory buffer */
	for (i = 0; i < size; i++)
		E1000_WRITE_REG_ARRAY(hw, E1000_VMBMEM(0), i, msg[i]);

	/* update stats */
	hw->mbx.stats.msgs_tx++;

	/* Drop VFU and interrupt the PF to tell it a message has been sent */
	E1000_WRITE_REG(hw, E1000_V2PMAILBOX(0), E1000_V2PMAILBOX_REQ);

out_no_write:
	return (ret_val);
}

/*
 * igbvf_read_mbx_vf - Reads a message from the inbox intended for vf
 *
 * returns SUCCESS if it successfuly read message from buffer
 */
static int
igbvf_read_mbx_vf(igbvf_t *igbvf, uint32_t *msg, uint16_t size)
{
	struct e1000_hw *hw = &igbvf->hw;
	int ret_val = E1000_SUCCESS;
	int i;

	ASSERT(mutex_owned(&igbvf->gen_lock));

	/* limit size to the size of mailbox */
	if (size > hw->mbx.size)
		size = hw->mbx.size;

	/* lock the mailbox to prevent pf/vf race condition */
	ret_val = igbvf_obtain_mbx_lock_vf(igbvf);
	if (ret_val)
		goto out_no_read;

	/* copy the message from the mailbox memory buffer */
	for (i = 0; i < size; i++)
		msg[i] = E1000_READ_REG_ARRAY(hw, E1000_VMBMEM(0), i);

	/* Acknowledge receipt and release mailbox, then we're done */
	E1000_WRITE_REG(hw, E1000_V2PMAILBOX(0), E1000_V2PMAILBOX_ACK);

	/* update stats */
	hw->mbx.stats.msgs_rx++;

out_no_read:
	return (ret_val);
}

/*
 * igbvf_check_for_link_vf - Check for link for a virtual interface
 *
 * Checks to see if the underlying PF is still talking to the VF and
 * if it is then it reports the link state to the hardware, otherwise
 * it reports link down and returns an error.
 */
static int
igbvf_check_for_link_vf(igbvf_t *igbvf)
{
	struct e1000_hw *hw = &igbvf->hw;
	struct e1000_mbx_info *mbx = &hw->mbx;
	struct e1000_mac_info *mac = &hw->mac;
	uint32_t in_msg = 0;
	clock_t time_stop;
	clock_t time_left;
	int ret_val = E1000_SUCCESS;

	ASSERT(mutex_owned(&igbvf->gen_lock));

	/* If we were hit with a reset drop the link */
	if (!mbx->ops.check_for_rst(hw, 0) || !mbx->timeout)
		mac->get_link_status = true;

	if (!mac->get_link_status)
		goto out;

	/* if link status is down no point in checking to see if pf is up */
	if (!(E1000_READ_REG(hw, E1000_STATUS) & E1000_STATUS_LU)) {
		goto out;
	}

	/* Wait for up to 1 second */
	time_stop = ddi_get_lbolt() + SEC_TO_TICK(1);
	while (igbvf->mbx_hold) {
		time_left = cv_timedwait(&igbvf->mbx_hold_cv,
		    &igbvf->gen_lock, time_stop);
		if (time_left == -1)
			break;
	}

	if (igbvf->mbx_hold) {
		IGBVF_DEBUGLOG_0(igbvf, "igbvf_check_for_link_vf: "
		    "Time out waiting other mailbox transaction to complete");
		mac->get_link_status = false;
		goto out;
	}

	/*
	 * if the read failed it could just be a mailbox collision, best wait
	 * until we are called again and don't report an error
	 */
	if (igbvf_read_mbx_vf(igbvf, &in_msg, 1)) {
		IGBVF_DEBUGLOG_0(igbvf, "igbvf_check_for_link_vf: "
		    "Failed to read mailbox");
		goto out;
	}

	/* if incoming message isn't clear to send we are waiting on response */
	if (!(in_msg & E1000_VT_MSGTYPE_CTS)) {
		/* message is not CTS and is NACK we have lost CTS status */
		if (in_msg & E1000_VT_MSGTYPE_NACK) {
			ret_val = -E1000_ERR_MAC_INIT;
		}
		goto out;
	}

	/*
	 * at this point we know the PF is talking to us, check and see if
	 * we are still accepting timeout or if we had a timeout failure.
	 * if we failed then we will need to reinit
	 */
	if (!mbx->timeout) {
		ret_val = -E1000_ERR_MAC_INIT;
		goto out;
	}

	/*
	 * if we passed all the tests above then the link is up and we no
	 * longer need to check for link
	 */
	mac->get_link_status = false;

out:
	return (ret_val);
}
