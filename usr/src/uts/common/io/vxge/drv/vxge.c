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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 *  Copyright Exar 2011. Copyright (c) 2002-2011 Neterion, Inc.
 *  All right Reserved.
 *
 *  FileName :    vxge.c
 *
 *  Description:  vxge main Solaris specific initialization & routines
 *		  for upper layer driver
 *
 */


#include "vxge.h"
#include "vxgehal.h"

#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <sys/types.h>
#include <sys/kstat.h>
#include "vxge_firmware.h"

#define	VXGE_M_CALLBACK_FLAGS	\
	(MC_IOCTL | MC_GETCAPAB | MC_SETPROP | MC_GETPROP | MC_PROPINFO)

typedef struct vxge_dev {
	int bd;
	u8 num_func;
}vxge_dev_t;

typedef struct total_vxge_dev {
	int max_devs;
	int cur_devs;
	vxge_dev_t *vxge_dev;
}total_vxge_dev_t;

static total_vxge_dev_t total_vxge = {0, 0, NULL};

/* DMA attributes used for Tx side */
static struct ddi_dma_attr vxge_tx_dma_attr = {
	DMA_ATTR_V0,			/* dma_attr_version */
	0x0ULL,				/* dma_attr_addr_lo */
	0xFFFFFFFFFFFFFFFFULL,		/* dma_attr_addr_hi */
	0xFFFFFFFFFFFFFFFFULL,		/* dma_attr_count_max */
#if defined(_BIG_ENDIAN)
	0x2000,				/* alignment */
#else
	0x1000,				/* alignment */
#endif
	0xFC00FC,			/* dma_attr_burstsizes */
	0x1,				/* dma_attr_minxfer */
	0xFFFFFFFFFFFFFFFFULL,		/* dma_attr_maxxfer */
	0xFFFFFFFFFFFFFFFFULL,		/* dma_attr_seg */
	18,				/* dma_attr_sgllen */
	(unsigned int)1,		/* dma_attr_granular */
	0				/* dma_attr_flags */
};

/*
 * DMA attributes used when using ddi_dma_mem_alloc to
 * allocat HAL descriptors and Rx buffers during replenish
 */
static struct ddi_dma_attr vxge_hal_dma_attr = {
	DMA_ATTR_V0,			/* dma_attr_version */
	0x0ULL,				/* dma_attr_addr_lo */
	0xFFFFFFFFFFFFFFFFULL,		/* dma_attr_addr_hi */
	0xFFFFFFFFFFFFFFFFULL,		/* dma_attr_count_max */
#if defined(_BIG_ENDIAN)
	0x2000,				/* alignment */
#else
	0x1000,				/* alignment */
#endif
	0xFC00FC,			/* dma_attr_burstsizes */
	0x1,				/* dma_attr_minxfer */
	0xFFFFFFFFFFFFFFFFULL,		/* dma_attr_maxxfer */
	0xFFFFFFFFFFFFFFFFULL,		/* dma_attr_seg */
	1,				/* dma_attr_sgllen */
	(unsigned int)1,		/* dma_attr_granular */
	DDI_DMA_RELAXED_ORDERING	/* dma_attr_flags */
};

/* function modes strings */
static char *vxge_func_mode_names[] = {
	"Single Function - 1 func, 17 vpath",
	"Multi Function - 8 func, 2 vpath per func",
	"SRIOV 17 - 17 VF, 1 vpath per VF",
	"WLPEX/SharedIO 17 - 17 VH, 1 vpath/func/hierarchy",
	"WLPEX/SharedIO 8 - 8 VH, 2 vpath/func/hierarchy",
	"Multi Function 17 - 17 func, 1 vpath per func",
	"SRIOV 8 - 1 PF, 7 VF, 2 vpath per VF",
	"SRIOV 4 - 1 PF, 3 VF, 4 vpath per VF",
	"Multi Function  - 2 func, 8 vpath per func",
	"Multi Function  - 4 func, 4 vpath per func",
	"WLPEX/SharedIO 4 - 17 func, 1 vpath per func (PCIe ARI)"
};

static int vxge_max_devices = 1;

struct ddi_dma_attr *p_vxge_hal_dma_attr = &vxge_hal_dma_attr;

static int		vxge_m_stat(void *, uint_t, uint64_t *);
static int		vxge_m_start(void *);
static void		vxge_m_stop(void *);
static int		vxge_m_promisc(void *, boolean_t);
static int		vxge_m_multicst(void *, boolean_t, const uint8_t *);
static int		vxge_m_unicst(void *, const uint8_t *);
static void		vxge_m_ioctl(void *, queue_t *, mblk_t *);
static mblk_t		*vxge_m_tx(void *, mblk_t *);
static boolean_t	vxge_m_getcapab(void *, mac_capab_t, void *);
static int		vxge_m_setprop(void *, const char *, mac_prop_id_t,
    uint_t, const void *);
static int vxge_m_getprop(void *, const char *, mac_prop_id_t,
    uint_t, void *);
static void vxge_m_propinfo(void *, const char *, mac_prop_id_t,
    mac_prop_info_handle_t);
static int vxge_get_priv_prop(vxgedev_t *, const char *, uint_t, void *);
static int vxge_set_priv_prop(vxgedev_t *, const char *, uint_t,
    const void *);

char *vxge_priv_props[] = {
	" pciconf",
	" about",
	" vpath_stats",
	" driver_stats",
	" mrpcim_stats",
	" identify",
	" bar0",
	" debug_level",
	" flow_control_gen",
	" flow_control_rcv",
	" debug_module_mask",
	" devconfig",
	NULL
};

static mac_callbacks_t vxge_m_callbacks = {
	VXGE_M_CALLBACK_FLAGS,
	vxge_m_stat,
	vxge_m_start,
	vxge_m_stop,
	vxge_m_promisc,
	vxge_m_multicst,
	vxge_m_unicst,
	vxge_m_tx,
	NULL,
	vxge_m_ioctl,
	vxge_m_getcapab,
	NULL,
	NULL,
	vxge_m_setprop,
	vxge_m_getprop,
	vxge_m_propinfo
};

extern int vxge_enable_intrs(vxgedev_t *vdev);
extern void vxge_disable_intrs(vxgedev_t *vdev);
static int vxge_attach(dev_info_t *dev_info, ddi_attach_cmd_t cmd);
static int vxge_detach(dev_info_t *dev_info, ddi_detach_cmd_t cmd);

static int vxge_quiesce(dev_info_t *dev_info);
DDI_DEFINE_STREAM_OPS(vxge_ops, nulldev, nulldev, vxge_attach, vxge_detach,
	nodev, NULL, D_MP, NULL, vxge_quiesce);

/* Standard Module linkage initialization for a Streams driver */
extern struct mod_ops mod_driverops;

static struct modldrv modldrv = {
	&mod_driverops,	/* Type of module.  This one is a driver */
	VXGE_DRIVER_NAME,	/* short description */
	&vxge_ops	/* driver specific ops */
};

static struct modlinkage modlinkage = {
	MODREV_1, {(void *)&modldrv, NULL}
};

/* Xge device attributes */
static ddi_device_acc_attr_t vxge_dev_attr = {
	DDI_DEVICE_ATTR_V1,
	DDI_NEVERSWAP_ACC,
	DDI_STRICTORDER_ACC,
	DDI_FLAGERR_ACC
};
ddi_device_acc_attr_t *p_vxge_dev_attr = &vxge_dev_attr;

static int vpath_selector[VXGE_MAX_VPATHS] = {0, 1, 3, 3, 7, 7, 7, 7, 15, 15,
			15, 15, 15, 15, 15, 15, 31};
/* LINTED: static unused */
static unsigned int trace_mask;

u32 vxge_get_bd(dev_info_t *dev_info) {
	int val, bd;
	int *props;
	unsigned int numProps;

	val = ddi_prop_lookup_int_array(
	    DDI_DEV_T_ANY, dev_info, 0, "reg", &props, &numProps);

	if ((val == DDI_PROP_SUCCESS) && (numProps > 0)) {
		bd = (PCI_REG_BUS_G(props[0]) << 8) |
		    (PCI_REG_DEV_G(props[0]) << 3);
		ddi_prop_free(props);
	}
	return (bd);
}

int vxge_get_dev_no(u32 bd) {
	int vxge_dev_no;

	if (total_vxge.vxge_dev == NULL)
		return (VXGE_HAL_FAIL);

	for (vxge_dev_no = 0; vxge_dev_no < total_vxge.max_devs;
	    vxge_dev_no++) {
		if (total_vxge.vxge_dev[vxge_dev_no].bd == -1) {
			total_vxge.vxge_dev[vxge_dev_no].bd = bd;
			total_vxge.vxge_dev[vxge_dev_no].num_func = 0;
			break;
		} else {
			if (bd == total_vxge.vxge_dev[vxge_dev_no].bd) {
				break;
			}
		}
	}
	return (vxge_dev_no);
}

/*
 * vxge_callback_crit_err
 *
 * This function called by HAL on Serious Error event. VXGE_HAL_EVENT_SERR.
 * Upper layer must analyze it based on %type.
 */
/*ARGSUSED*/
void
vxge_callback_crit_err(vxge_hal_device_h devh, void *userdata,
	    vxge_hal_event_e type, u64 vp_id)
{
	vxgedev_t *vdev = (vxgedev_t *)userdata;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_CALLBACK_CRIT_ERR ENTRY ",
	    VXGE_IFNAME, vdev->instance);
	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_ENTER SOFT_LOCK_ALARM",
	    VXGE_IFNAME, vdev->instance);
	mutex_enter(&vdev->soft_lock_alarm);

	vdev->cric_err_event.type = type;
	vdev->cric_err_event.vp_id = vp_id;

	if (!vdev->soft_running_alarm) {
		vdev->soft_running_alarm = 1;
		(void) ddi_intr_trigger_softint(vdev->soft_hdl_alarm, vdev);
	}

	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_EXIT SOFT_LOCK_ALARM",
	    VXGE_IFNAME, vdev->instance);
	mutex_exit(&vdev->soft_lock_alarm);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_CALLBACK_CRIT_ERR EXIT ",
	    VXGE_IFNAME, vdev->instance);
}

/*
 * vxge_xpak_alarm_log
 *
 * This function called by HAL on XPAK alarms. Upper layer must log the msg
 * based on the xpak alarm type
 */
/*ARGSUSED*/
static void
vxge_xpak_alarm_log(void *userdata, u32 port, vxge_hal_xpak_alarm_type_e type)
{
	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s: VXGE_EVENT ENTER ",
	    VXGE_IFNAME);

	switch (type) {
	case VXGE_HAL_XPAK_ALARM_EXCESS_TEMP:
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
	    "%s", "Take X3100 NIC out of \
	    service. Excessive temperatures may  \
	    result in premature transceiver  \
	    failure \n");

	break;
	case VXGE_HAL_XPAK_ALARM_EXCESS_BIAS_CURRENT:
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
	    "%s", "Take X3100 NIC out of " \
	    "service Excessive bias currents may " \
	    "indicate imminent laser diode " \
	    "failure \n");

	break;
	case VXGE_HAL_XPAK_ALARM_EXCESS_LASER_OUTPUT:
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
	    "%s", "Take X3100 NIC out of " \
	    "service Excessive laser output " \
	    "power may saturate far-end " \
	    "receiver\n");

	break;
	default:
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
	    "%s", "Undefined Xpak Alarm");

	break;
	}
	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s: VXGE_EVENT EXIT ",
	    VXGE_IFNAME);

}

/*
 * vxge_driver_init_hal
 *
 * To initialize HAL portion of driver.
 */
static vxge_hal_status_e
vxge_driver_init_hal(void)
{
	static vxge_hal_driver_config_t driver_config;
	vxge_hal_uld_cbs_t uld_callbacks;

	uld_callbacks.link_up	= vxge_callback_link_up;
	uld_callbacks.link_down	 = vxge_callback_link_down;
	uld_callbacks.crit_err	= vxge_callback_crit_err;
	uld_callbacks.sched_timer	= NULL;
	uld_callbacks.xpak_alarm_log    = vxge_xpak_alarm_log;

	vxge_debug_init(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s", "vxge: Initializing driver\n");

	return (vxge_hal_driver_initialize(&driver_config, &uld_callbacks));
}

/*
 * _init
 *
 * Solaris standard _init function for a device driver
 */
int
_init(void)
{
	int ret = 0;
	vxge_hal_status_e status = VXGE_HAL_OK;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s", "vxge: _INIT ENTER ");

	status = vxge_driver_init_hal();
	if (status != VXGE_HAL_OK) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "vxge: can't initialize the driver (%d)",
		    status);
		return (EINVAL);
	}

	vxge_hal_driver_debug_set(VXGE_ERR);

	vxge_debug_driver(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s", "vxge: VXGE_INIT");

	mac_init_ops(&vxge_ops, VXGE_IFNAME);
	ret = mod_install(&modlinkage);
	if (ret != 0) {
		vxge_hal_driver_terminate();
		mac_fini_ops(&vxge_ops);
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
	    "%s", "vxge: Unable to install the driver");
		return (ret);
	}

	vxge_debug_init(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s", "vxge: mod_install done\n");

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s", "vxge: _INIT EXIT ");

	return (0);
}

/*
 * _fini
 *
 * Solaris standard _fini function for device driver
 */
int
_fini(void)
{
	int ret;

	vxge_debug_driver(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s", "vxge: VXGE_FINI");

	ret = mod_remove(&modlinkage);
	if (ret == 0) {
		vxge_hal_driver_terminate();
		mac_fini_ops(&vxge_ops);
	}

	vxge_debug_init(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s", "vxge: mod_remove done\n");

	return (ret);
}

/*
 * _info
 *
 * Solaris standard _info function for device driver
 */
int
_info(struct modinfo *pModinfo)
{
	return (mod_info(&modlinkage, pModinfo));
}

/*
 * vxge_isr
 *
 * This is the ISR scheduled by the OS to indicate to the
 * driver that the receive/transmit operation is completed.
 */
/*ARGSUSED*/
static uint_t
vxge_isr(caddr_t arg, caddr_t arg2)
{
	vxge_hal_status_e status = VXGE_HAL_OK;
	vxge_hal_device_t *hldev = (vxge_hal_device_t *)(void *)arg;
	vxgedev_t *vdev = vxge_hal_device_private_get(hldev);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_ISR ENTRY",
	    VXGE_IFNAME, vdev->instance);

	if (!vdev->is_initialized) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: vdev not initialized\n", VXGE_IFNAME,
		    vdev->instance);
		return (DDI_INTR_UNCLAIMED);
	}

	vxge_debug_intr(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: vxge_hal_device_handle_irq>>\n",
	    VXGE_IFNAME, vdev->instance);

	status = vxge_hal_device_handle_irq(hldev, 0);

	if (status != VXGE_HAL_OK)
		VXGE_STATS_DRV_INC(spurious_intr_cnt);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_ISR EXIT",
	    VXGE_IFNAME, vdev->instance);

	return ((status == VXGE_HAL_ERR_WRONG_IRQ) ?
	    DDI_INTR_UNCLAIMED : DDI_INTR_CLAIMED);
}

/*
 * vxge_alarm_msix_isr
 *
 * Hardware alarms handler (through MSI-X)
 */
/*ARGSUSED*/
static uint_t
vxge_alarm_msix_isr(caddr_t arg, caddr_t arg2)
{
	int i;
	vxge_hal_status_e status = VXGE_HAL_OK;
	vxgedev_t *vdev = (void *)arg2;
	vxge_vpath_t    *vpath = NULL;

	vxge_assert(vdev);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_ALARM_MSIX_ISR ENTRY",
	    VXGE_IFNAME, vdev->instance);

	VXGE_HAL_DEVICE_STATS_SW_INFO_NOT_TRAFFIC_INTR(vdev->devh);

	if (!vdev->is_initialized) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: vdev not initialized\n", VXGE_IFNAME,
		    vdev->instance);
		return (DDI_INTR_UNCLAIMED);
	}

	/* Process alarms in each vpath */
	for (i = 0; i < vdev->no_of_vpath; i++) {
		vxge_assert(vdev->vpaths[i].handle);
		vpath = &(vdev->vpaths[i]);

		vxge_hal_vpath_msix_mask(vpath->handle, vpath->alarm_msix_vec);

		status = vxge_hal_vpath_alarm_process(vpath->handle, 0);

		if ((status == VXGE_HAL_ERR_EVENT_SLOT_FREEZE) ||
		    (status == VXGE_HAL_ERR_EVENT_SERR)) {
			/*
			 * Leave interrupts masked
			 * Device disabeled DMA and masked the interrupts
			 */
			vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
			    "%s%d: %s Processing Alarms" \
			    "urecoverable error %x",
			    VXGE_IFNAME, vdev->instance,
			    VXGE_DRIVER_NAME, status);

			/* Stop the driver */
			vdev->is_initialized = 0;
			break;

		}
		vxge_hal_vpath_msix_unmask(vpath->handle,
		    vpath->alarm_msix_vec);
	}

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_ALARM_MSIX_ISR EXIT",
	    VXGE_IFNAME, vdev->instance);

	return (DDI_INTR_CLAIMED);
}

static uint_t
vxge_ring_fifo_msix_isr(caddr_t arg, caddr_t arg2)
{
	unsigned int	got_tx = 0;
	unsigned int	got_rx = 0;
	vxge_vpath_t *vpath = (void *)arg;
	vxgedev_t *vdev = (void *)arg2;
	vxge_hal_vpath_h vpath_handle = vpath->handle;

	vxge_assert(vpath_handle);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_RING_FIFO_MSIX_ISR ENTRY",
	    VXGE_IFNAME, vdev->instance);

	if (!vdev->is_initialized) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: vdev not initialized\n", VXGE_IFNAME,
		    vdev->instance);
		return (DDI_INTR_UNCLAIMED);
	}

	vxge_hal_vpath_msix_mask(vpath->handle, vpath->rx_tx_msix_vec);

	VXGE_HAL_DEVICE_STATS_SW_INFO_TRAFFIC_INTR(vdev->devh);

	/* Processing Rx */
	vxge_debug_rx(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: Polling Rx with vxge_hal_vpath_poll_rx\n",
	    VXGE_IFNAME, vdev->instance);

	vpath->ring.ring_intr_cnt++;

	(void) vxge_hal_vpath_poll_rx(vpath_handle, &got_rx);

	/* Processing Tx */
	vxge_debug_tx(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: Polling Tx with vxge_hal_vpath_poll_tx\n",
	    VXGE_IFNAME, vdev->instance);

	if (vxge_hal_isfifoenable(vpath_handle))
		(void) vxge_hal_vpath_poll_tx(vpath_handle, &got_tx);

	if (vpath->handle)
		vxge_hal_vpath_msix_unmask(vpath->handle,
		    vpath->rx_tx_msix_vec);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_FIFO_MSIX_ISR EXIT",
	    VXGE_IFNAME, vdev->instance);

	return (DDI_INTR_CLAIMED);
}
/*
 * vxge_config_init_vpath
 *
 * Initialize Virtual Path parameters
 */
static void
vxge_config_init_vpath(dev_info_t *dev_info,
	vxge_hal_device_config_t *device_config, vxge_config_t *ll_config)
{
#define	VXGE_CONFIG_VPATH(i)    (device_config->vp_config[i])
#define	VXGE_CONFIG_FIFO(i)    (device_config->vp_config[i].fifo)
#define	VXGE_CONFIG_TTI(i)    (device_config->vp_config[i].tti)
#define	VXGE_CONFIG_RING(i)    (device_config->vp_config[i].ring)
#define	VXGE_CONFIG_RTI(i)    (device_config->vp_config[i].rti)
#define	VXGE_TPA_IPV6_HDRS \
VXGE_HAL_VPATH_TPA_SUPPORT_MOBILE_IPV6_HDRS_USE_FLASH_DEFAULT

	int index = 0, no_of_vpaths = 0, temp_val = 0;
	int fifo_cnt = 0;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s: VXGE_CONFIG_INIT_VPATH ENTER",
	    VXGE_DRIVER_NAME);

	ll_config->mtu = ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
	    DDI_PROP_DONTPASS, "default_mtu", VXGE_HAL_DEFAULT_MTU);

	if ((!ll_config->tx_steering_type && !ll_config->rth_enable) &&
	    ll_config->max_config_vpath > 1) {
		ll_config->max_config_vpath = 1;
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s: Both tx and rx steering are disabled. "\
		    "Resetting vpath count to One \n",
		    VXGE_DRIVER_NAME);
	}

	if (ll_config->max_fifo_cnt > ll_config->max_config_vpath) {
		ll_config->max_fifo_cnt = ll_config->max_config_vpath;
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s: fifo_cnt is greater than vpath_cnt. "\
		    "Resetting fifo count to configured vpath cnt\n",
		    VXGE_DRIVER_NAME);
	}

	fifo_cnt = ll_config->max_fifo_cnt;

	for (index = 0; index < VXGE_HAL_MAX_VIRTUAL_PATHS; index++) {
		VXGE_CONFIG_VPATH(index).vp_id = index;
		VXGE_CONFIG_VPATH(index).mtu = ll_config->mtu;
		if (no_of_vpaths < ll_config->max_config_vpath) {
			if (!bVAL1(ll_config->device_hw_info.vpath_mask, index))
				continue;
			else
				no_of_vpaths++;
		} else
			break;

		/*
		 * VLAN stripping
		 */
		VXGE_CONFIG_VPATH(index).rpa_strip_vlan_tag =
		    VXGE_DO_NOT_STRIP_VLAN_TAG;

		ll_config->strip_vlan_tag =
		    VXGE_CONFIG_VPATH(index).rpa_strip_vlan_tag;

		/*
		 * FIFO Configuration
		 */
		VXGE_CONFIG_FIFO(index).fifo_length =
		    ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
		    DDI_PROP_DONTPASS, "fifo_length",
		    VXGE_HAL_DEF_FIFO_LENGTH);

		VXGE_CONFIG_FIFO(index).max_frags =
		    ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
		    DDI_PROP_DONTPASS, "fifo_max_frags",
		    VXGE_DEFAULT_FIFO_FRAGS);

		VXGE_CONFIG_FIFO(index).max_aligned_frags =
		    ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
		    DDI_PROP_DONTPASS, "fifo_copied_max_frags",
		    VXGE_DEFAULT_FIFO_MAX_ALIGNED_FRAGS);

		VXGE_CONFIG_FIFO(index).intr = VXGE_HAL_FIFO_QUEUE_INTR_DEFAULT;

		if (fifo_cnt) {
			VXGE_CONFIG_FIFO(index).enable = VXGE_HAL_FIFO_ENABLE;
			fifo_cnt--;
		} else
			VXGE_CONFIG_FIFO(index).enable = VXGE_HAL_FIFO_DISABLE;


#if defined(__sparc)
		VXGE_CONFIG_FIFO(index).no_snoop_bits =
		    ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
		    DDI_PROP_DONTPASS, "fifo_no_snoop_bits",
		    VXGE_HAL_FIFO_NO_SNOOP_DEFAULT);
#endif

		/*
		 * TTI Configuration
		 */
		VXGE_CONFIG_TTI(index).intr_enable =
		    ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
		    DDI_PROP_DONTPASS, "tti_intr_enable",
		    VXGE_HAL_TIM_INTR_ENABLE);

		temp_val = ddi_prop_get_int(DDI_DEV_T_ANY,
		    dev_info, DDI_PROP_DONTPASS, "tti_btimer_val",
		    VXGE_DEFAULT_TTI_BTIMER_VAL);

		VXGE_CONFIG_TTI(index).btimer_val = (temp_val * 1000) / 272;

		VXGE_CONFIG_TTI(index).timer_ac_en =
		    ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
		    DDI_PROP_DONTPASS, "tti_timer_ac_en",
		    VXGE_HAL_TIM_TIMER_AC_USE_FLASH_DEFAULT);

		VXGE_CONFIG_TTI(index).timer_ci_en =
		    ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
		    DDI_PROP_DONTPASS, "tti_timer_ci_en",
		    VXGE_HAL_TIM_TIMER_CI_USE_FLASH_DEFAULT);

		VXGE_CONFIG_TTI(index).timer_ri_en =
		    ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
		    DDI_PROP_DONTPASS, "tti_timer_ri_en",
		    VXGE_HAL_TIM_TIMER_RI_USE_FLASH_DEFAULT);

		VXGE_CONFIG_TTI(index).rtimer_event_sf =
		    ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
		    DDI_PROP_DONTPASS, "tti_rtimer_event_sf",
		    VXGE_HAL_USE_FLASH_DEFAULT_TIM_RTIMER_EVENT_SF);

		VXGE_CONFIG_TTI(index).util_sel =
		    ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
		    DDI_PROP_DONTPASS, "tti_util_sel",
		    VXGE_HAL_TIM_UTIL_SEL_LEGACY_TX_NET_UTIL);

		temp_val = ddi_prop_get_int(DDI_DEV_T_ANY,
		    dev_info, DDI_PROP_DONTPASS, "tti_ltimer_val",
		    VXGE_DEFAULT_TTI_LTIMER_VAL);

		VXGE_CONFIG_TTI(index).ltimer_val = (temp_val * 1000) / 272;

		temp_val = ddi_prop_get_int(DDI_DEV_T_ANY,
		    dev_info, DDI_PROP_DONTPASS, "tti_rtimer_val",
		    VXGE_DEFAULT_TTI_RTIMER_VAL);

		VXGE_CONFIG_TTI(index).rtimer_val = (temp_val * 1000) / 272;

		VXGE_CONFIG_TTI(index).urange_a =
		    ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
		    DDI_PROP_DONTPASS, "tti_urange_a",
		    VXGE_DEFAULT_TX_URANGE_A);

		VXGE_CONFIG_TTI(index).urange_b =
		    ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
		    DDI_PROP_DONTPASS, "tti_urange_b",
		    VXGE_DEFAULT_TX_URANGE_B);

		VXGE_CONFIG_TTI(index).urange_c =
		    ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
		    DDI_PROP_DONTPASS, "tti_urange_c",
		    VXGE_DEFAULT_TX_URANGE_C);

		VXGE_CONFIG_TTI(index).uec_a =
		    ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
		    DDI_PROP_DONTPASS, "tti_uec_a", VXGE_DEFAULT_TX_UFC_A);

		VXGE_CONFIG_TTI(index).uec_b = ddi_prop_get_int(DDI_DEV_T_ANY,
		    dev_info, DDI_PROP_DONTPASS, "tti_uec_b",
		    VXGE_DEFAULT_TX_UFC_B);

		VXGE_CONFIG_TTI(index).uec_c = ddi_prop_get_int(DDI_DEV_T_ANY,
		    dev_info, DDI_PROP_DONTPASS, "tti_uec_c",
		    VXGE_DEFAULT_TX_UFC_C);

		VXGE_CONFIG_TTI(index).uec_d = ddi_prop_get_int(DDI_DEV_T_ANY,
		    dev_info, DDI_PROP_DONTPASS, "tti_uec_d",
		    VXGE_DEFAULT_TX_UFC_D);

		VXGE_CONFIG_TTI(index).txfrm_cnt_en =
		    ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
		    DDI_PROP_DONTPASS, "tti_txfrm_cnt_enable",
		    VXGE_HAL_TXFRM_CNT_EN_USE_FLASH_DEFAULT);

		VXGE_CONFIG_TTI(index).txd_cnt_en =
		    ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
		    DDI_PROP_DONTPASS, "tti_txd_cnt_en",
		    VXGE_HAL_TXD_CNT_EN_USE_FLASH_DEFAULT);

		/*
		 * Ring Configuration
		 */
		VXGE_CONFIG_RING(index).ring_length =
		    ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
		    DDI_PROP_DONTPASS, "ring_length",
		    VXGE_HAL_DEF_RING_LENGTH);

		VXGE_CONFIG_RING(index).buffer_mode =
		    VXGE_HAL_RING_RXD_BUFFER_MODE_1;

		VXGE_CONFIG_RING(index).max_frm_len =
		    ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
		    DDI_PROP_DONTPASS, "ring_max_frm_len",
		    VXGE_HAL_DEFAULT_MTU + 22);

		VXGE_CONFIG_RING(index).enable  = VXGE_HAL_RING_ENABLE;

		VXGE_CONFIG_RING(index).no_snoop_bits =
		    ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
		    DDI_PROP_DONTPASS, "ring_no_snoop_bits",
		    VXGE_HAL_RING_NO_SNOOP_USE_FLASH_DEFAULT);

		VXGE_CONFIG_RING(index).backoff_interval_us = ddi_prop_get_int(
		    DDI_DEV_T_ANY, dev_info, DDI_PROP_DONTPASS,
		    "ring_backoff_interval_us",
		    VXGE_DEFAULT_BACKOFF_INTERVAL_US);

		VXGE_CONFIG_RING(index).rx_timer_val =
		    VXGE_HAL_RING_USE_FLASH_DEFAULT_RX_TIMER_VAL;

		VXGE_CONFIG_RING(index).indicate_max_pkts =
		    ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
		    DDI_PROP_DONTPASS, "ring_indicate_max_pkts",
		    VXGE_HAL_DEF_RING_INDICATE_MAX_PKTS);

		if (VXGE_CONFIG_RING(index).indicate_max_pkts >
		    VXGE_INDICATE_PACKET_ARRAY_SIZE) {
			VXGE_CONFIG_RING(index).indicate_max_pkts =
			    VXGE_INDICATE_PACKET_ARRAY_SIZE;
			vxge_debug_driver(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
			    "%s: indicate_max_pkts = %d",
			    VXGE_DRIVER_NAME,
			    VXGE_CONFIG_RING(index).indicate_max_pkts);
		}

		VXGE_CONFIG_RING(index).post_mode =
		    VXGE_HAL_RING_POST_MODE_DOORBELL;

		/*
		 * RTI Configuration
		 */
		VXGE_CONFIG_RTI(index).intr_enable =
		    ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
		    DDI_PROP_DONTPASS, "rti_intr_enable",
		    VXGE_HAL_TIM_INTR_ENABLE);

		temp_val = ddi_prop_get_int(DDI_DEV_T_ANY,
		    dev_info, DDI_PROP_DONTPASS, "rti_btimer_val",
		    VXGE_DEFAULT_RTI_BTIMER_VAL);

		VXGE_CONFIG_RTI(index).btimer_val = (temp_val * 1000) / 272;

		VXGE_CONFIG_RTI(index).timer_ac_en =
		    ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
		    DDI_PROP_DONTPASS, "rti_timer_ac_en",
		    VXGE_HAL_TIM_TIMER_AC_USE_FLASH_DEFAULT);

		VXGE_CONFIG_RTI(index).timer_ci_en =
		    ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
		    DDI_PROP_DONTPASS, "rti_timer_ci_en",
		    VXGE_HAL_TIM_TIMER_CI_USE_FLASH_DEFAULT);

		VXGE_CONFIG_RTI(index).timer_ri_en =
		    ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
		    DDI_PROP_DONTPASS, "rti_timer_ri_en",
		    VXGE_HAL_TIM_TIMER_RI_USE_FLASH_DEFAULT);

		VXGE_CONFIG_RTI(index).rtimer_event_sf =
		    ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
		    DDI_PROP_DONTPASS, "rti_rtimer_event_sf",
		    VXGE_HAL_USE_FLASH_DEFAULT_TIM_RTIMER_EVENT_SF);

		VXGE_CONFIG_RTI(index).util_sel =
		    ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
		    DDI_PROP_DONTPASS, "rti_util_sel",
		    VXGE_HAL_TIM_UTIL_SEL_LEGACY_RX_NET_UTIL);

		VXGE_CONFIG_RTI(index).urange_a =
		    ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
		    DDI_PROP_DONTPASS, "rti_urange_a",
		    VXGE_DEFAULT_RX_URANGE_A);

		VXGE_CONFIG_RTI(index).urange_b =
		    ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
		    DDI_PROP_DONTPASS, "rti_urange_b",
		    VXGE_DEFAULT_RX_URANGE_B);

		VXGE_CONFIG_RTI(index).urange_c =
		    ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
		    DDI_PROP_DONTPASS, "rti_urange_c",
		    VXGE_DEFAULT_RX_URANGE_C);

		VXGE_CONFIG_RTI(index).uec_a = ddi_prop_get_int(DDI_DEV_T_ANY,
		    dev_info, DDI_PROP_DONTPASS, "rti_uec_a",
		    VXGE_DEFAULT_RX_UFC_A);

		VXGE_CONFIG_RTI(index).uec_b = ddi_prop_get_int(DDI_DEV_T_ANY,
		    dev_info, DDI_PROP_DONTPASS, "rti_uec_b",
		    VXGE_DEFAULT_RX_UFC_B_N);

		VXGE_CONFIG_RTI(index).uec_c = ddi_prop_get_int(DDI_DEV_T_ANY,
		    dev_info, DDI_PROP_DONTPASS, "rti_uec_c",
		    VXGE_DEFAULT_RX_UFC_C_N);

		VXGE_CONFIG_RTI(index).uec_d = ddi_prop_get_int(DDI_DEV_T_ANY,
		    dev_info, DDI_PROP_DONTPASS, "rti_uec_d",
		    VXGE_DEFAULT_RX_UFC_D);

		temp_val = ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
		    DDI_PROP_DONTPASS, "rti_rtimer_val",
		    VXGE_DEFAULT_RTI_RTIMER_VAL);

		VXGE_CONFIG_RTI(index).rtimer_val = (temp_val * 1000) / 272;

		temp_val = ddi_prop_get_int(DDI_DEV_T_ANY,
		    dev_info, DDI_PROP_DONTPASS, "rti_ltimer_val",
		    VXGE_DEFAULT_RTI_LTIMER_VAL);

		VXGE_CONFIG_RTI(index).ltimer_val = (temp_val * 1000) / 272;

		VXGE_CONFIG_RTI(index).txfrm_cnt_en =
		    ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
		    DDI_PROP_DONTPASS, "rti_txfrm_cnt_enable",
		    VXGE_HAL_TXFRM_CNT_EN_ENABLE);

		VXGE_CONFIG_RTI(index).txd_cnt_en =
		    ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
		    DDI_PROP_DONTPASS, "rti_txd_cnt_en",
		    VXGE_HAL_TXD_CNT_EN_USE_FLASH_DEFAULT);

		VXGE_CONFIG_VPATH(index).tpa_lsov2_en =
		    VXGE_HAL_VPATH_TPA_LSOV2_EN_DISABLE;

		VXGE_CONFIG_VPATH(index).tpa_ignore_frame_error =
		    VXGE_HAL_VPATH_TPA_IGNORE_FRAME_ERROR_USE_FLASH_DEFAULT;

		VXGE_CONFIG_VPATH(index).tpa_ipv6_keep_searching =
		    VXGE_HAL_VPATH_TPA_IPV6_KEEP_SEARCHING_USE_FLASH_DEFAULT;

		VXGE_CONFIG_VPATH(index).tpa_l4_pshdr_present =
		    VXGE_HAL_VPATH_TPA_L4_PSHDR_PRESENT_USE_FLASH_DEFAULT;

		VXGE_CONFIG_VPATH(index).tpa_support_mobile_ipv6_hdrs =
		    VXGE_TPA_IPV6_HDRS;

		VXGE_CONFIG_VPATH(index).rpa_ipv4_tcp_incl_ph =
		    VXGE_HAL_VPATH_RPA_IPV4_TCP_INCL_PH_USE_FLASH_DEFAULT;

		VXGE_CONFIG_VPATH(index).rpa_ipv6_tcp_incl_ph =
		    VXGE_HAL_VPATH_RPA_IPV6_TCP_INCL_PH_USE_FLASH_DEFAULT;

		VXGE_CONFIG_VPATH(index).rpa_ipv4_udp_incl_ph =
		    VXGE_HAL_VPATH_RPA_IPV4_UDP_INCL_PH_USE_FLASH_DEFAULT;

		VXGE_CONFIG_VPATH(index).rpa_ipv6_udp_incl_ph =
		    VXGE_HAL_VPATH_RPA_IPV6_UDP_INCL_PH_USE_FLASH_DEFAULT;

		VXGE_CONFIG_VPATH(index).rpa_l4_incl_cf =
		    VXGE_HAL_VPATH_RPA_L4_INCL_CF_USE_FLASH_DEFAULT;

		VXGE_CONFIG_VPATH(index).rpa_l4_comp_csum =
		    VXGE_HAL_VPATH_RPA_L4_COMP_CSUM_USE_FLASH_DEFAULT;

		VXGE_CONFIG_VPATH(index).rpa_l3_incl_cf =
		    VXGE_HAL_VPATH_RPA_L3_INCL_CF_USE_FLASH_DEFAULT;

		VXGE_CONFIG_VPATH(index).rpa_l3_comp_csum =
		    VXGE_HAL_VPATH_RPA_L3_COMP_CSUM_USE_FLASH_DEFAULT;

		VXGE_CONFIG_VPATH(index).rpa_ucast_all_addr_en =
		    VXGE_HAL_VPATH_RPA_UCAST_ALL_ADDR_USE_FLASH_DEFAULT;

		VXGE_CONFIG_VPATH(index).rpa_mcast_all_addr_en =
		    VXGE_HAL_VPATH_RPA_MCAST_ALL_ADDR_USE_FLASH_DEFAULT;

		VXGE_CONFIG_VPATH(index).rpa_bcast_en  =
		    VXGE_HAL_VPATH_RPA_BCAST_USE_FLASH_DEFAULT;

		ll_config->vlan_promisc_enable = 1;

		VXGE_CONFIG_VPATH(index).vp_queue_l2_flow =
		    VXGE_HAL_VPATH_VP_Q_L2_FLOW_ENABLE;

		vxge_debug_driver(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
		    "vxge: vpath_%d configured", index);
	}
	ll_config->vpath_count = no_of_vpaths;
	vxge_debug_init(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s: Configured vpaths\n", VXGE_DRIVER_NAME);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s: VXGE_CONFIG_INIT_VPATH EXIT", VXGE_DRIVER_NAME);
}

static int vxge_get_max_device(u64 func_mode)
{
	switch (func_mode) {
	case VXGE_HAL_PCIE_FUNC_MODE_SF1_VP17:
		func_mode = VXGE_MAX_DEVICE_SF1_VP17;
		break;
	case VXGE_HAL_PCIE_FUNC_MODE_MF2_VP8:
		func_mode = VXGE_MAX_DEVICE_MF2_VP8;
		break;
	case VXGE_HAL_PCIE_FUNC_MODE_MF4_VP4:
		func_mode = VXGE_MAX_DEVICE_MF4_VP4;
		break;
	case VXGE_HAL_PCIE_FUNC_MODE_MF8_VP2:
		func_mode = VXGE_MAX_DEVICE_MF8_VP2;
		break;
	default:
		func_mode = VXGE_MAX_DEVICE_SF1_VP17;
	}
	return (int)(func_mode & 0xFF);
}

/*
 * vxge_configuration_init
 * @device_config: pointer to vxge_hal_device_config_t
 *
 * This function will lookup properties from .conf file to init
 * the configuration data structure. If a property is not in .conf
 * file, the default value should be set.
 */
static int
vxge_configuration_init(dev_info_t *dev_info, u8 *bar0, pci_reg_h regh0,
	vxge_hal_device_config_t *device_config, vxge_config_t *ll_config)
{
	vxge_hal_status_e status;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s", "vxge: VXGE_CONFIGURATION_INIT ENTER");

	status = vxge_hal_device_hw_info_get(dev_info, regh0, bar0,
	    &ll_config->device_hw_info);

	if (status != VXGE_HAL_OK) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s", "vxge: reading of mask and mac address failed");
		return (DDI_FAILURE);
	}

	if (!ll_config->device_hw_info.func_id) {
		ll_config->dev_func_mode =
		    ll_config->device_hw_info.function_mode;
		vxge_max_devices = vxge_get_max_device
		    (ll_config->device_hw_info.function_mode);
	}

	if (ll_config->device_hw_info.mac_addr_masks == 0) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s", "vxge: no VPATHs available");
	return (DDI_FAILURE);
	}

	ll_config->max_config_dev = ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
	    DDI_PROP_DONTPASS, "max_config_dev", VXGE_DEFAULT_CONF_DEV);

	if (ll_config->max_config_dev == VXGE_DEFAULT_CONF_DEV)
		ll_config->max_config_dev = vxge_max_devices;

	if (!ll_config->device_hw_info.func_id) {
		if (ll_config->max_config_dev < vxge_max_devices &&
		    ll_config->max_config_dev > 0)
			vxge_max_devices = ll_config->max_config_dev;
	}

	/*
	 * MSI-X switch
	 */
	ll_config->msix_enable = ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
	    DDI_PROP_DONTPASS, "msix_enable", VXGE_CONF_ENABLE_BY_DEFAULT);
	if (ll_config->msix_enable)
		device_config->intr_mode = VXGE_HAL_INTR_MODE_MSIX;

	ll_config->max_config_vpath = ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
	    DDI_PROP_DONTPASS, "max_config_vpath",
	    VXGE_DEFAULT_CONF_VPATH);

	ll_config->lso_enable = ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
	    DDI_PROP_DONTPASS, "lso_enable", VXGE_DEFAULT_CONF_LSO);

	ll_config->rx_dma_lowat = ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
	    DDI_PROP_DONTPASS, "ring_dma_lowat", VXGE_RX_DMA_LOWAT);

	ll_config->tx_dma_lowat = ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
	    DDI_PROP_DONTPASS, "fifo_dma_lowat", VXGE_TX_DMA_LOWAT);

	ll_config->rth_enable = ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
	    DDI_PROP_DONTPASS, "rth_enable", VXGE_DEFAULT_CONF_RTH);

	/* Tx Steering - Multi-FIFO Support */
	ll_config->tx_steering_type = ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
	    DDI_PROP_DONTPASS, "tx_steering_type",
	    VXGE_DEFAULT_TX_STEERING);

	/* Firmware Upgrade Support */
	ll_config->fw_upgrade = ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
	    DDI_PROP_DONTPASS, "fw_upgrade", VXGELL_DEFAULT_UPGRADE);

	/* Function Mode change Support */
	ll_config->func_mode = ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
	    DDI_PROP_DONTPASS, "func_mode", VXGELL_DEFAULT_FUNC_MODE);

	/* Port Mode change Support */
	ll_config->port_mode = ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
	    DDI_PROP_DONTPASS, "port_mode", VXGELL_DEFAULT_PORT_MODE);

	/* Port Mode change Support */
	ll_config->port_failure = ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
	    DDI_PROP_DONTPASS, "port_failure", VXGELL_DEFAULT_PORT_FAILURE);

	/* Number of FIFOs requested */
	ll_config->max_fifo_cnt = ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
	    DDI_PROP_DONTPASS, "max_fifo_cnt", VXGE_DEFAULT_CONF_FIFO);

	/* Using no. of FIFOs as number of vpaths if Tx Steering is enabled */
	if ((ll_config->tx_steering_type) && (ll_config->max_fifo_cnt < 2))
		ll_config->max_fifo_cnt = ll_config->max_config_vpath;

	/* Disabling Flow control on device configuration init by default */

	ll_config->flow_control_gen = ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
	    DDI_PROP_DONTPASS, "flow_control_gen",
	    VXGE_DEFAULT_RMAC_PAUSE_GEN_EN);
	ll_config->flow_control_rcv = ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
	    DDI_PROP_DONTPASS, "flow_control_rcv",
	    VXGE_DEFAULT_RMAC_PAUSE_RCV_EN);

	/* debug tracing */
	ll_config->debug_module_mask = ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
	    DDI_PROP_DONTPASS, "debug_module_mask",
	    VXGE_DEFAULT_DEBUG_MODULE_MASK);
	ll_config->debug_module_level = ddi_prop_get_int(DDI_DEV_T_ANY,
	    dev_info, DDI_PROP_DONTPASS, "debug_module_level",
	    VXGE_DEFAULT_DEBUG_MODULE_LEVEL);

	/* Initialize link layer configuration */
	ll_config->rx_pkt_burst = ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
	    DDI_PROP_DONTPASS, "rx_pkt_burst", VXGE_DEFAULT_RX_PKT_BURST);

	/* Initialize for fma support configuration */
	ll_config->fm_capabilities = ddi_prop_get_int(DDI_DEV_T_ANY,
	    dev_info, DDI_PROP_DONTPASS, "fm-capable",
	    DDI_FM_EREPORT_CAPABLE | DDI_FM_ACCCHK_CAPABLE |
	    DDI_FM_DMACHK_CAPABLE | DDI_FM_ERRCB_CAPABLE);

	device_config->debug_level = VXGE_ERR;
	device_config->debug_mask = VXGE_COMPONENT_LL;

	/*
	 * CQRQ/SRQ
	 */
	device_config->dma_blockpool_initial =
	    VXGE_HAL_INITIAL_DMA_BLOCK_POOL_SIZE;
	device_config->dma_blockpool_min = VXGE_HAL_MIN_DMA_BLOCK_POOL_SIZE;
	device_config->dma_blockpool_max = VXGE_HAL_MAX_DMA_BLOCK_POOL_SIZE;
	device_config->dma_blockpool_incr = VXGE_HAL_INCR_DMA_BLOCK_POOL_SIZE;

	device_config->stats_refresh_time_sec = ddi_prop_get_int(DDI_DEV_T_ANY,
	    dev_info, DDI_PROP_DONTPASS, "stats_refresh_time",
	    VXGE_HAL_USE_FLASH_DEFAULT_STATS_REFRESH_TIME);

	device_config->isr_polling_cnt = ddi_prop_get_int(DDI_DEV_T_ANY,
	    dev_info, DDI_PROP_DONTPASS, "isr_polling_cnt",
	    VXGE_DEFAULT_ISR_POLLING_CNT);

	device_config->device_poll_millis = VXGE_DEFAULT_DEVICE_POLL_TIME_MS;

	/*
	 * MAC based steering
	 */
	device_config->rts_mac_en = ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
	    DDI_PROP_DONTPASS, "rts_mac_en", VXGE_HAL_RTS_MAC_DEFAULT);

	/*
	 * Error Handling
	 */
	device_config->dump_on_serr = VXGE_HAL_DUMP_ON_SERR_DEFAULT;
	device_config->dump_on_eccerr = VXGE_HAL_DUMP_ON_ECCERR_DEFAULT;

	device_config->dump_on_unknown  = VXGE_HAL_DUMP_ON_UNKNOWN_DEFAULT;
	device_config->dump_on_critical  = VXGE_HAL_DUMP_ON_CRITICAL_DEFAULT;

	vxge_config_init_vpath(dev_info, device_config, ll_config);

	ll_config->rx_buffer_total_per_rxd = ddi_prop_get_int(DDI_DEV_T_ANY,
	    dev_info, DDI_PROP_DONTPASS, "rx_buffer_total_per_rxd",
	    VXGE_RX_BUFFER_TOTAL);
	ll_config->rx_buffer_total =
	    ((device_config->vp_config[0].ring.ring_length /
	    vxge_hal_ring_rxds_per_block_get(
	    device_config->vp_config[0].ring.buffer_mode)) *
	    ll_config->rx_buffer_total_per_rxd);

	ll_config->rx_buffer_post_hiwat_per_rxd =
	    ddi_prop_get_int(DDI_DEV_T_ANY, dev_info, DDI_PROP_DONTPASS,
	    "rx_buffer_post_hiwat_per_rxd", VXGE_RX_BUFFER_POST_HIWAT);
	ll_config->rx_buffer_post_hiwat =
	    ((device_config->vp_config[0].ring.ring_length /
	    vxge_hal_ring_rxds_per_block_get(
	    device_config->vp_config[0].ring.buffer_mode))    *
	    ll_config->rx_buffer_post_hiwat_per_rxd);

	device_config->rts_qos_en = VXGE_HAL_RTS_QOS_DISABLE;
	if (ll_config->rth_enable)
		device_config->rth_en = VXGE_HAL_RTH_ENABLE;
	else
		device_config->rth_en = VXGE_HAL_RTH_DISABLE;

	device_config->max_cqe_groups = VXGE_HAL_DEF_MAX_CQE_GROUPS;
	device_config->max_num_wqe_od_groups = VXGE_HAL_DEF_MAX_NUM_OD_GROUPS;
	device_config->no_wqe_threshold = VXGE_HAL_DEF_NO_WQE_THRESHOLD;
	device_config->refill_threshold_high =
	    VXGE_HAL_DEF_REFILL_THRESHOLD_HIGH;
	device_config->refill_threshold_low  =
	    VXGE_HAL_DEF_REFILL_THRESHOLD_LOW;
	device_config->ack_blk_limit = VXGE_HAL_DEF_ACK_BLOCK_LIMIT;
	device_config->poll_or_doorbell  = VXGE_HAL_POLL_OR_DOORBELL_DEFAULT;
	device_config->stats_read_method = VXGE_HAL_STATS_READ_METHOD_PIO;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s", "vxge: VXGE_CONFIGURATION_INIT EXIT");

	return (DDI_SUCCESS);
}

/*
 * vxge_alloc_intrs:
 *
 * Allocate FIXED or MSIX interrupts.
 */
static int
vxge_alloc_intrs(vxgedev_t *vdev)
{
	dev_info_t    *dip = vdev->dev_info;
	int avail = 0, actual = 0;
	int i, flag, ret;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d; VXGE_ALLOC_INTRS ENTRY ",
	    VXGE_IFNAME, vdev->instance);
	/* Get number of available interrupts */
	ret = ddi_intr_get_navail(dip, vdev->intr_type, &avail);
	if ((ret != DDI_SUCCESS) || (avail == 0)) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: ddi_intr_get_navail() failure"
		    "ret: %d, avail: %d",
		    VXGE_IFNAME, vdev->instance, ret, avail);
		return (DDI_FAILURE);
	}

	if (avail < vdev->intr_cnt) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: Not enough resources available",
		    VXGE_IFNAME, vdev->instance);
		goto _err_exit0;
	}

	if (vdev->intr_type == DDI_INTR_TYPE_MSIX)
		flag = DDI_INTR_ALLOC_STRICT;
	else
		flag = DDI_INTR_ALLOC_NORMAL;

	/* Allocate an array of interrupt handles */
	vdev->intr_size = vdev->intr_cnt * sizeof (ddi_intr_handle_t);

	vdev->htable = kmem_zalloc(vdev->intr_size, KM_NOSLEEP);
		if (vdev->htable == NULL) {
			VXGE_STATS_DRV_INC(kmem_zalloc_fail);
			goto _err_exit0;
		}
	VXGE_STATS_DRV_ADD(kmem_alloc, vdev->intr_size);

	/* Call ddi_intr_alloc() */
	ret = ddi_intr_alloc(dip, vdev->htable, vdev->intr_type, 0,
	    vdev->intr_cnt, &actual, flag);
	if ((ret != DDI_SUCCESS) && (actual > 0)) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s: Not enough interrupt vectors",
		    VXGE_IFNAME, vdev->instance);
		goto _err_exit2;
	} else if ((ret != DDI_SUCCESS) || (actual == 0)) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: ddi_intr_alloc() failed %d",
		    VXGE_IFNAME, vdev->instance, ret);
		goto _err_exit1;
	}

	vxge_debug_intr(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: Interrupts allocated\n",
	    VXGE_IFNAME, vdev->instance);

	vxge_os_printf("%s%d: %s: Requested: %d, Granted: %d\n",
	    VXGE_IFNAME, vdev->instance,
	    vdev->intr_type == DDI_INTR_TYPE_MSIX ? "MSI-X" : "IRQA",
	    vdev->intr_cnt, actual);

	if (vdev->intr_cnt != actual) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: Not enough resources granted",
		    VXGE_IFNAME, vdev->instance);
		goto _err_exit2;
	}

	/*
	 * Get priority for first msi, assume remaining are all the same
	 */
	ret = ddi_intr_get_pri(vdev->htable[0], &vdev->intr_pri);
	if (ret != DDI_SUCCESS) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: ddi_intr_get_pri() failed %d",
		    VXGE_IFNAME, vdev->instance, ret);
		goto _err_exit2;
	}

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_ALLOC_INTRS EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (DDI_SUCCESS);

_err_exit2:
	/* Free already allocated intr */
	for (i = 0; i < actual; i++)
		(void) ddi_intr_free(vdev->htable[i]);
_err_exit1:
	kmem_free(vdev->htable, vdev->intr_size);
	VXGE_STATS_DRV_ADD(kmem_free, vdev->intr_size);

_err_exit0:
	return (DDI_FAILURE);
}

/*
 * vxge_free_intrs:
 *
 * Free previously allocated interrupts.
 */
static void
vxge_free_intrs(vxgedev_t *vdev)
{
	int i;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_FREE_INTRS ENTRY ",
	    VXGE_IFNAME, vdev->instance);
	/* Free already allocated intr */
	for (i = 0; i < vdev->intr_cnt; i++)
		(void) ddi_intr_free(vdev->htable[i]);
	vxge_debug_intr(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: Freed interrupts\n",
	    VXGE_IFNAME, vdev->instance);

	kmem_free(vdev->htable, vdev->intr_size);
	VXGE_STATS_DRV_ADD(kmem_free, vdev->intr_size);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_FREE_INTRS EXIT ",
	    VXGE_IFNAME, vdev->instance);
}

/*
 * vxge_add_intrs:
 *
 * Register FIXED or MSI interrupts.
 */
static int
vxge_add_intrs(vxgedev_t *vdev)
{
	int j, ret;
	int traffic_intr_cnt = 0;
	vxge_hal_device_t *hldev = vdev->devh;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_ADD_INTRS ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	traffic_intr_cnt = vdev->no_of_vpath;

	/* Call ddi_intr_add_handler() */
	for (j = 0; j < vdev->intr_cnt; j++) {
		caddr_t isr_arg;
		uint_t (*isr_p)(caddr_t, caddr_t);

		if (vdev->intr_type == DDI_INTR_TYPE_MSIX) {
			/* Point to next Vpath object */

			if (j < traffic_intr_cnt) {
				isr_p = vxge_ring_fifo_msix_isr;
				isr_arg = (caddr_t)&(vdev->vpaths[j]);
			} else {
				isr_p = vxge_alarm_msix_isr;
				isr_arg = (caddr_t)&(vdev);
			}

		} else {
			isr_p = vxge_isr;
			isr_arg = (caddr_t)hldev;
		}
		ret = ddi_intr_add_handler(vdev->htable[j], isr_p,
		    (caddr_t)isr_arg, (caddr_t)vdev);
		if (ret != DDI_SUCCESS) {
			int k;
			vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
			    "%s%d: ddi_intr_add_handler() failed %d",
			    VXGE_IFNAME, vdev->instance, ret);

			/* unwind */
			for (k = j; k < 0; k--)
				(void) ddi_intr_remove_handler
				    (vdev->htable[k]);
				return (DDI_FAILURE);
		}
	}

	ret = ddi_intr_get_cap(vdev->htable[0], &vdev->intr_cap);
	if (ret != DDI_SUCCESS) {
		int i;
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: ddi_intr_get_cap() failed %d",
		    VXGE_IFNAME, vdev->instance, ret);

		for (i = 0; i < vdev->intr_cnt; i++)
			(void) ddi_intr_remove_handler(vdev->htable[i]);
			return (DDI_FAILURE);
	}

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_ADD_INTRS EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (DDI_SUCCESS);
}

/*
 * vxge_enable_intrs:
 *
 * Enable FIXED or MSI interrupts
 */
int
vxge_enable_intrs(vxgedev_t *vdev)
{
	int ret, i;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_ENABLE_INTRS ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	if (vdev->intr_cap & DDI_INTR_FLAG_BLOCK) {
		/* Call ddi_intr_block_enable() for MSI(X) interrupts */
		ret = ddi_intr_block_enable(vdev->htable, vdev->intr_cnt);
		if (ret != DDI_SUCCESS) {
			vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
			    "%s%d: ddi_intr_enable() failed, ret 0x%x",
			    VXGE_IFNAME, vdev->instance, ret);
			return (DDI_FAILURE);
		}
	} else {
		/* Call ddi_intr_enable for MSI(X) or FIXED interrupts */
		for (i = 0; i < vdev->intr_cnt; i++) {
			ret = ddi_intr_enable(vdev->htable[i]);
				if (ret != DDI_SUCCESS) {
					int j;
					vxge_debug_driver(VXGE_ERR, NULL_HLDEV,
					    NULL_VPID,
					    "%s%d: ddi_intr_enable() failed, "
					    "ret 0x%x",
					    VXGE_IFNAME, vdev->instance,
					    ret);

					/* unwind */
					for (j = 0; j < i; j++) {
						(void) ddi_intr_disable(
						    vdev->htable[j]);
					}

					return (DDI_FAILURE);
				}
		}
	}

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_ENABLE_INTRS EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (DDI_SUCCESS);
}

/*
 * vxge_disable_intrs:
 *
 * Disable FIXED or MSI interrupts
 */
void
vxge_disable_intrs(vxgedev_t *vdev)
{
	int i;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_DISABLE_INTRS ENTRY ",
	    VXGE_IFNAME, vdev->instance);
	if (vdev->intr_cap & DDI_INTR_FLAG_BLOCK) {
		/* Call ddi_intr_block_disable() */
		(void) ddi_intr_block_disable(vdev->htable, vdev->intr_cnt);
	} else {
		for (i = 0; i < vdev->intr_cnt; i++)
			(void) ddi_intr_disable(vdev->htable[i]);
	}

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_DISABLE_INTRS EXIT ",
	    VXGE_IFNAME, vdev->instance);
}

/*
 * vxge_rem_intrs:
 *
 * Unregister FIXED or MSI interrupts
 */
static void
vxge_rem_intrs(vxgedev_t *vdev)
{
	int i;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_REM_INTRS ENTRY ",
	    VXGE_IFNAME, vdev->instance);
	/* Call ddi_intr_remove_handler() */
	for (i = 0; i < vdev->intr_cnt; i++)
		(void) ddi_intr_remove_handler(vdev->htable[i]);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_REM_INTRS EXIT ",
	    VXGE_IFNAME, vdev->instance);
}

/*
 * resume device from suspend mode
 */
static int
vxge_resume(dev_info_t *dev_info)
{
	vxgedev_t *vdev;
	vxge_hal_device_t *hldev;
	int ret;

	hldev = (vxge_hal_device_t *)ddi_get_driver_private(dev_info);
	if (hldev == NULL) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: unable to resume: unknown error",
		    VXGE_IFNAME, vdev->instance);
		return (DDI_FAILURE);
	}

	if (hldev->pdev != dev_info) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: Resume failed: "
		    "Data structures aren't consistent",
		    VXGE_IFNAME, vdev->instance);
		return (DDI_FAILURE);
	}

	vdev = vxge_hal_device_private_get(hldev);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_RESUME ENTRY ",
	    VXGE_IFNAME, vdev->instance);


	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_ENTER GENLOCK",
	    VXGE_IFNAME, vdev->instance);
	mutex_enter(&vdev->genlock);
	vdev->in_reset = 0;
	if (vdev->need_start) {
		vdev->need_start = 0;
		ret = vxge_initiate_start(vdev);
		if (ret != DDI_SUCCESS) {
			vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
			    "%s%d: vxge_initiate_start failed\n",
			    VXGE_IFNAME, vdev->instance);
			mutex_exit(&vdev->genlock);
			return (ret);
		}
	}

	if ((vdev->vdev_state & VXGE_STARTED) &&
	    (vdev->resched_retry & VXGE_RESHED_RETRY)) {
		atomic_and_32(&vdev->resched_retry, ~VXGE_RESHED_RETRY);
		mac_tx_update(vdev->mh);
		VXGE_STATS_DRV_INC(xmit_update_cnt);
	}
	mutex_exit(&vdev->genlock);

	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_EXIT GENLOCK",
	    VXGE_IFNAME, vdev->instance);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_RESUME EXIT ",
	    VXGE_IFNAME, vdev->instance);

	return (DDI_SUCCESS);
}

/* ARGSUSED */
int
vxge_check_acc_handle(pci_reg_h handle)
{
	ddi_fm_error_t de;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_CHECK_ACC_HANDLE ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	ddi_fm_acc_err_get(handle, &de, DDI_FME_VERSION);
	ddi_fm_acc_err_clear(handle, DDI_FME_VERSION);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_CHECK_ACC_HANDLE EXIT ",
	    VXGE_IFNAME, vdev->instance);

	return (de.fme_status);
}

/*
 * The IO fault service error handling callback function
 */
/*ARGSUSED*/
static int
vxge_fm_error_cb(dev_info_t *dev_info, ddi_fm_error_t *err,
	    const void *impl_data)
{
	/*
	 * as the driver can always deal with an error in any dma or
	 * access handle, we can just return the fme_status value.
	 */
	pci_ereport_post(dev_info, err, NULL);

	return (err->fme_status);
}

static void
vxge_fm_init(vxgedev_t *vdev)
{
	ddi_iblock_cookie_t iblk;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_FM_INIT ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	/* Only register with IO Fault Services if we have some capability */
	vxge_dev_attr.devacc_attr_access =
	    (vdev->config.fm_capabilities & DDI_FM_ACCCHK_CAPABLE) ?
	    DDI_FLAGERR_ACC : DDI_DEFAULT_ACC;

	(void) vxge_set_fma_flags(vdev,
	    vdev->config.fm_capabilities & DDI_FM_DMACHK_CAPABLE);

	if (vdev->config.fm_capabilities) {

		/* Register capabilities with IO Fault Services */
		ddi_fm_init(vdev->dev_info, &vdev->config.fm_capabilities,
		    &iblk);

		/*
		 * Initialize pci ereport capabilities if ereport capable
		 */
		if (DDI_FM_EREPORT_CAP(vdev->config.fm_capabilities) ||
		    DDI_FM_ERRCB_CAP(vdev->config.fm_capabilities))
			pci_ereport_setup(vdev->dev_info);

		/*
		 * Register error callback if error callback capable
		 */
		if (DDI_FM_ERRCB_CAP(vdev->config.fm_capabilities))
			ddi_fm_handler_register(vdev->dev_info,
			    vxge_fm_error_cb, (void *) vdev);

		vxge_os_printf("%s%d: FMA capabilities: 0x%x\n",
		    VXGE_IFNAME, vdev->instance,
		    ddi_fm_capable(vdev->dev_info));
	}

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_FM_INIT EXIT ",
	    VXGE_IFNAME, vdev->instance);
}

static void
vxge_fm_fini(vxgedev_t *vdev)
{
	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_FM_FINI ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	/* Only unregister FMA capabilities if we registered some */
	if (vdev->config.fm_capabilities) {
		/*
		 * Release any resources allocated by pci_ereport_setup()
		 */
		if (DDI_FM_EREPORT_CAP(vdev->config.fm_capabilities) ||
		    DDI_FM_ERRCB_CAP(vdev->config.fm_capabilities))
			pci_ereport_teardown(vdev->dev_info);

		/*
		 * Un-register error callback if error callback capable
		 */
		if (DDI_FM_ERRCB_CAP(vdev->config.fm_capabilities))
			ddi_fm_handler_unregister(vdev->dev_info);

		/* Unregister from IO Fault Services */
		ddi_fm_fini(vdev->dev_info);
	}

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_FM_FINI EXIT ",
	    VXGE_IFNAME, vdev->instance);
}

static int
vxge_firmware_read_file(dev_info_t *dev_info, u8 *bar0, pci_reg_h regh0)
{
	char	*file_buf = NULL;
	int	file_size;

	vxge_hal_status_e status = VXGE_HAL_OK;

	file_buf = (char *)VXGE_FW_ARRAY_NAME;
	file_size = sizeof (VXGE_FW_ARRAY_NAME);

	/* Call HAL API to upgrade firmware */
	status = vxge_hal_mrpcim_fw_upgrade(dev_info, regh0, bar0,
	    (u8 *)file_buf, file_size);

	if (status != VXGE_HAL_OK) {
		goto _err_exit0;
	}

	return (VXGE_HAL_OK);

_err_exit0:
	return (VXGE_HAL_FAIL);
}

/*
 * vxge_firmware_version_verify
 *
 * Continue with driver attach only if adapter has allowed firmware
 */

static int
vxge_firmware_version_verify(dev_info_t *dev_info, u8 *bar0, pci_reg_h regh0,
    vxge_config_t *ll_config, vxgedev_t *vdev)
{

	vxge_hal_device_version_t *fw_version;
	u32 fw_version_current = 0;
	u32 fw_version_cert = 0;
	u8 *mac_addr;
	boolean_t func_mode_change = B_FALSE;
	boolean_t fw_change = B_FALSE;
	boolean_t port_change = B_FALSE;
	boolean_t port_failure_change = B_FALSE;
	boolean_t reboot_req = B_FALSE;
	vxge_hal_status_e status = VXGE_HAL_FAIL;
	u64 active_config = VXGELL_DEFAULT_PORT_MODE;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s", "vxge: VXGE_FIRMWARE_VERSION_VERIFY ENTER");

	fw_version = &ll_config->device_hw_info.fw_version;
	mac_addr = (u8 *)&ll_config->device_hw_info.mac_addrs[0];

	fw_version_current = VXGE_FW_VER(fw_version->major, fw_version->minor,
	    fw_version->build);
	fw_version_cert = VXGE_FW_VER(VXGE_FW_MAJOR_VERSION,
	    VXGE_FW_MINOR_VERSION, VXGE_FW_BUILD_NUMBER);

	if (ll_config->func_mode != VXGELL_DEFAULT_FUNC_MODE &&
	    ll_config->func_mode != vdev->dev_func_mode)
		func_mode_change = B_TRUE;

	if (fw_version_current < fw_version_cert)
		fw_change = B_TRUE;

	if (ll_config->port_mode != VXGELL_DEFAULT_PORT_MODE &&
	    ll_config->device_hw_info.ports == VXGE_DUAL_PORT)
		port_change = B_TRUE;

	if (ll_config->port_failure != VXGELL_DEFAULT_PORT_FAILURE &&
	    ll_config->device_hw_info.ports == VXGE_DUAL_PORT)
		port_failure_change = B_TRUE;

	if (!fw_change && !func_mode_change && !port_change &&
	    !port_failure_change) {
		status = VXGE_HAL_OK;
			goto _exit;
	}

	if (fw_change) {
		vxge_os_printf("%s%d: Firmware Revision %02d.%02d.%02d\n",
		    VXGE_IFNAME, vdev->instance,
		    fw_version->major, fw_version->minor, fw_version->build);

		vxge_os_printf("%s%d: Serial number: %s\n", VXGE_IFNAME,
		    vdev->instance, ll_config->device_hw_info.serial_number);

		vxge_os_printf("vxge%d: Ethernet Address: "
		    "%02X:%02X:%02X:%02X:%02X:%02X\n", vdev->instance,
		    mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3],
		    mac_addr[4], mac_addr[5]);

		switch (ll_config->fw_upgrade) {
		case 0:
		vxge_os_printf("%s%d: Incompatible Driver and Firmwares\n",
		    VXGE_IFNAME, vdev->instance);
		vxge_os_printf("%s%d: Please use Firmware %02d.%02d.%02d\n",
		    VXGE_IFNAME, vdev->instance,
		    VXGE_FW_MAJOR_VERSION, VXGE_FW_MINOR_VERSION,
		    VXGE_FW_BUILD_NUMBER);
		break;

		case 1:
		if (fw_version_current < VXGE_BASE_FW_VER) {
			vxge_os_printf("%s%d: Found incompatible firmware. \
			    Cannot  upgrade \n",
			    VXGE_IFNAME, vdev->instance);
			goto _exit;
		}

		vxge_os_printf("%s%d: Firmware upgrade in progress...\n",
		    VXGE_IFNAME, vdev->instance);
		status = vxge_firmware_read_file(dev_info, bar0, regh0);
		if (status != VXGE_HAL_OK) {
			vxge_os_printf("%s%d: FIRMWARE UPGRADE FAILED !!\n",
			    VXGE_IFNAME, vdev->instance);
			goto _exit;
		}

		vxge_os_printf("%s%d: FIRMWARE UPGRADED SUCCESSFULLY !!\n",
		    VXGE_IFNAME, vdev->instance);
		reboot_req = TRUE;
		status = VXGE_HAL_FAIL;
		break;

		default:
		vxge_os_printf("%s%d: Wrong Firmware upgrade option.\n",
		    VXGE_IFNAME, vdev->instance);
		}
	}

	if (func_mode_change) {

		if (!(ll_config->func_mode == VXGE_HAL_PCIE_FUNC_MODE_MF8_VP2 ||
		    ll_config->func_mode == VXGE_HAL_PCIE_FUNC_MODE_SF1_VP17 ||
		    ll_config->func_mode == VXGE_HAL_PCIE_FUNC_MODE_MF2_VP8 ||
		    ll_config->func_mode == VXGE_HAL_PCIE_FUNC_MODE_MF4_VP4)) {
			vxge_os_printf("%s%d: Wrong Function mode...\n",
			    VXGE_IFNAME, vdev->instance);
			goto _exit;
		}
		vxge_os_printf("%s%d: Function mode change in progress...\n",
		    VXGE_IFNAME, vdev->instance);
		status = vxge_hal_mrpcim_pcie_func_mode_set(vdev->devh,
		    ll_config->func_mode);

		if (status != VXGE_HAL_OK) {
			vxge_os_printf("%s%d: FUNCTION MODE CHANGE FAILED!!\n",
			    VXGE_IFNAME, vdev->instance);
			goto _exit;
		}
		reboot_req = TRUE;
		status = VXGE_HAL_FAIL;
		vxge_os_printf("%s%d: FUNCTION MODE CHANGED SUCCESSFULLY !!\n",
		    VXGE_IFNAME, vdev->instance);
	}

	if (port_change) {

		status = vxge_hal_get_active_config(vdev->devh,
		    VXGE_HAL_XMAC_NWIF_ActConfig_NWPortMode,
		    &active_config);
		if ((status != VXGE_HAL_OK) ||
		    (ll_config->port_mode == active_config)) {
			goto _exit;
		}

		vxge_os_printf("%s%d: Port mode change in progress...\n",
		    VXGE_IFNAME, vdev->instance);
		status = vxge_hal_set_port_mode(vdev->devh,
		    ll_config->port_mode);
		if (status == VXGE_HAL_OK) {
			(void) vxge_hal_set_fw_api(vdev->devh, 0,
			    VXGE_HAL_FW_API_FUNC_MODE_COMMIT,
			    0, 0, 0);

			reboot_req = TRUE;
			status = VXGE_HAL_FAIL;
			vxge_os_printf
			    ("%s%d: PORT MODE CHANGED SUCCESSFULLY !!\n",
			    VXGE_IFNAME, vdev->instance);
		} else {
			vxge_os_printf("%s%d: PORT MODE CHANGE FAILED!!\n",
			    VXGE_IFNAME, vdev->instance);
		}
	}

	if (port_failure_change) {
		active_config = VXGELL_DEFAULT_PORT_FAILURE;
		status = vxge_hal_get_active_config(vdev->devh,
		    VXGE_HAL_XMAC_NWIF_ActConfig_BehaviourOnFail,
		    &active_config);

		if ((status != VXGE_HAL_OK) ||
		    (ll_config->port_failure == active_config)) {
			goto _exit;
		}

		vxge_os_printf("%s%d: Port behaviour change in progress...\n",
		    VXGE_IFNAME, vdev->instance);
		status = vxge_hal_set_behavior_on_failure(vdev->devh,
		    ll_config->port_failure);
		if (status == VXGE_HAL_OK) {
			(void) vxge_hal_set_fw_api(vdev->devh, 0,
			    VXGE_HAL_FW_API_FUNC_MODE_COMMIT,
			    0, 0, 0);

			reboot_req = TRUE;
			status = VXGE_HAL_FAIL;
			vxge_os_printf
			    ("%s%d: PORT BEHAVIOUR CHANGED SUCCESSFULLY !!\n",
			    VXGE_IFNAME, vdev->instance);
		} else {
			vxge_os_printf
			    ("%s%d: PORT BEHAVIOUR CHANGE FAILED!!\n",
			    VXGE_IFNAME, vdev->instance);
		}
	}
_exit:

	/* Device configuration successful. Need a powercycle */
	if (reboot_req)
		vxge_os_printf("vxge: PLEASE POWERCYCLE THE SYSTEM !!\n");

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s", "vxge: VXGE_FIRMWARE_VERSION_VERIFY ENTER");

	return (status);
}

/*
 * vxge_attach
 *
 * This is a solaris standard attach function. This function initializes the
 * X3100 identified by the dev_info_t structure and setup the driver data
 * structures corresponding to the X3100 adapter. This function also registers
 * the X3100 device instance with the MAC Layer. If this function returns
 * success then the OS will attach the HBA controller to this driver.
 */
static int
vxge_attach(dev_info_t *dev_info, ddi_attach_cmd_t cmd)
{
	vxgedev_t *vdev;
	vxge_hal_device_config_t *device_config;
	vxge_hal_device_h hldev;
	vxge_hal_device_attr_t attr;
	vxge_hal_status_e status;
	vxge_config_t ll_config;
	int ret, intr_types, vxge_max_phys_dev;
	u16 ss_id = 0, cap_ptr = PCI_CAP_NEXT_PTR_NULL, cap_id = 0;
	u32 link_capabilities, link_status, neg_link_width;
	int i, j;
	vxge_hal_device_t *hdev;
	int bd, vxge_dev_no;

	trace_mask = ddi_prop_get_int(DDI_DEV_T_ANY, dev_info,
	    DDI_PROP_DONTPASS, "trace_mask",
	    VXGE_CONF_DISABLE_BY_DEFAULT);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "VXGE_ATTACH ENTRY cmd %d", cmd);

	switch (cmd) {
	case DDI_ATTACH:

		if (total_vxge.max_devs == 0) {
			vxge_max_phys_dev = ddi_prop_get_int(DDI_DEV_T_ANY,
			    dev_info, DDI_PROP_DONTPASS, "vxge_max_phys_dev",
			    VXGE_MAX_PHYS_DEV_DEF);

			if (vxge_max_phys_dev > VXGE_MAX_PHYS_DEV_DEF ||
			    vxge_max_phys_dev < VXGE_MIN_PHYS_DEV_DEF)
				vxge_max_phys_dev = VXGE_MAX_PHYS_DEV_DEF;

			if (total_vxge.vxge_dev == NULL)
				total_vxge.vxge_dev = (vxge_dev_t *)kmem_alloc
				    ((vxge_max_phys_dev * sizeof (vxge_dev_t)),
				    KM_NOSLEEP);

			if (total_vxge.vxge_dev == NULL)
				return (DDI_FAILURE);

			total_vxge.max_devs = vxge_max_phys_dev;
			(void) memset((void *)total_vxge.vxge_dev,
			    -1, vxge_max_phys_dev * sizeof (vxge_dev_t));
		}

		bd = vxge_get_bd(dev_info);
		vxge_dev_no =  vxge_get_dev_no(bd);

		if (vxge_dev_no != VXGE_HAL_FAIL) {
			if (total_vxge.vxge_dev[vxge_dev_no].num_func >=
			    vxge_max_devices) {
				vxge_debug_driver(VXGE_ERR, NULL_HLDEV,
				    NULL_VPID, "%s",
				    "vxge device not configured");
				ret = DDI_FAILURE;
				goto _exit0;
			}
		}
		break;

	case DDI_RESUME:
		return (vxge_resume(dev_info));

	default:
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "cmd 0x%x unrecognized", cmd);
		return (DDI_FAILURE);
	}

	device_config = kmem_zalloc(sizeof (vxge_hal_device_config_t),
	    KM_NOSLEEP);
	if (device_config == NULL) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s", "could not allocate configuration structure");
		ret = DDI_FAILURE;
		goto _exit0;
	}

	/* map BAR0 */
	ret = ddi_regs_map_setup(dev_info, 1, (caddr_t *)&attr.bar0,
	    (offset_t)0, (offset_t)0, &vxge_dev_attr, &attr.regh0);
	if (ret != DDI_SUCCESS) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "unable to map bar0: [%d]", ret);
		goto _exit0a;
	}

	/* get the default configuration parameters */
	(void) vxge_hal_device_config_default_get(device_config);

	/* Init device_config by lookup up properties from .conf file */
	ret = vxge_configuration_init(dev_info, attr.bar0, attr.regh0,
	    device_config, &ll_config);
	if (ret != DDI_SUCCESS) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s", "vxge_configuration_init failed\n");
		goto _exit0c;
	}

	ret = vxge_device_alloc(dev_info, &ll_config, &vdev);
	if (ret != DDI_SUCCESS) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s", "unable to allocate new LL device");
		goto _exit0c;
	}
	vxge_fm_init(vdev);

	/* Determine which types of interrupts supported */
	ret = ddi_intr_get_supported_types(dev_info, &intr_types);
	if ((ret != DDI_SUCCESS) || (!(intr_types & DDI_INTR_TYPE_FIXED))) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s", "fixed type interrupt is not supported");
		goto _exit1;
	}

	/* Get the PCI Configuartion space handle */
	ret = pci_config_setup(dev_info, &attr.cfgh);
	if (ret != DDI_SUCCESS) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s", "can not setup config space");
		goto _exit1;
	}

	attr.pdev = dev_info;
	vdev->no_of_vpath = ll_config.vpath_count;
	vdev->no_of_ring = vdev->no_of_vpath;
	vdev->no_of_fifo = ll_config.max_fifo_cnt;
	vdev->rth_enable = ll_config.rth_enable;
	vdev->vlan_promisc_enable = ll_config.vlan_promisc_enable;
	vdev->strip_vlan_tag = ll_config.strip_vlan_tag;
	vdev->tx_steering_type = ll_config.tx_steering_type;
	vdev->debug_module_mask = ll_config.debug_module_mask;
	vdev->debug_module_level = ll_config.debug_module_level;
	vdev->dev_func_mode = ll_config.dev_func_mode;

	if (ll_config.msix_enable && intr_types & DDI_INTR_TYPE_MSIX) {
		vdev->intr_type = DDI_INTR_TYPE_MSIX;
		device_config->intr_mode = VXGE_HAL_INTR_MODE_MSIX;
		vdev->intr_cnt = vdev->no_of_vpath + 1;
	} else {
		if (ll_config.msix_enable) {
			vxge_os_printf(
			    "%s%d: Running system does not support MSI-X"
			    " - defaulting to IRQA\n",
			    VXGE_IFNAME, vdev->instance);
		}
		vdev->config.msix_enable = 0;
		vdev->intr_cnt = 1;
		vdev->intr_type = DDI_INTR_TYPE_FIXED;
		device_config->intr_mode = VXGE_HAL_INTR_MODE_IRQLINE;
	}

	/* allocate an interrupt handler(s) */
	while ((ret = vxge_alloc_intrs(vdev)) != DDI_SUCCESS) {
		if (vdev->intr_type == DDI_INTR_TYPE_MSIX) {
			vdev->config.msix_enable = 0;
			vdev->intr_cnt = 1;
			vdev->intr_type = DDI_INTR_TYPE_FIXED;
			device_config->intr_mode = VXGE_HAL_INTR_MODE_IRQLINE;
			vxge_os_printf(
			    "%s%d: Unable to allocate MSI-X handlers"
			    " - defaulting to IRQA\n",
			    VXGE_IFNAME, vdev->instance);
			if (total_vxge.vxge_dev[vxge_dev_no].num_func > 1) {
				/* Don't load device in mix mode */
				total_vxge.vxge_dev[vxge_dev_no].num_func +=
				    vxge_max_devices;
				goto _exit2;
			}
			continue;
		}
		goto _exit2;
	}
	attr.irqh = (void *)((uintptr_t)vdev->intr_pri);

	/* initialize HW */
	status = vxge_hal_device_initialize(&hldev, &attr, device_config);
	if (status != VXGE_HAL_OK) {
		switch (status) {
		case VXGE_HAL_ERR_DRIVER_NOT_INITIALIZED:
			vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
			    "%s", "driver is not initialized");
			ret = DDI_FAILURE;
			goto _exit3;
		case VXGE_HAL_ERR_DEVICE_IS_NOT_QUIESCENT:
			vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
			    "%s", "Device is not quiescent");
			ret = DDI_EBUSY;
			goto _exit3;
		case VXGE_HAL_ERR_OUT_OF_MEMORY:
			vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
			    "%s", "unable to allocate memory");
			ret = DDI_ENOMEM;
			goto _exit3;
		default:
			vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
			    "can't initialize the device: %d",
			    status);
			ret = DDI_FAILURE;
			goto _exit3;
		}
	}
	vxge_debug_init(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d:", "HAL Device initialized\n",
	    VXGE_IFNAME, vdev->instance);

	vdev->devh = (vxge_hal_device_t *)hldev;
	vdev->mtu  = ll_config.mtu;
	vdev->bar0 = attr.bar0;

	hdev = (vxge_hal_device_t *)hldev;
	hdev->pdev = attr.pdev;
	ddi_set_driver_private(dev_info, (caddr_t)hldev);

	/* Check firmware version and verify its compatibility with driver */
	if (!ll_config.device_hw_info.func_id) {
		ret = vxge_firmware_version_verify(dev_info, attr.bar0,
		    attr.regh0, &ll_config, vdev);
		if (ret != DDI_SUCCESS) {
			/* Make sure you dont configure any more function */
			total_vxge.vxge_dev[vxge_dev_no].num_func++;
			vxge_max_devices = 1;
			/* Don't free vxge_dev. Will be freed in next reboot */
			total_vxge.cur_devs = 1;
			goto _exit4;
		}
	}

	vxge_hal_device_intrmode_set(vdev->devh, device_config->intr_mode);

	vxge_debug_init(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d", "Interrupts allocated\n",
	    VXGE_IFNAME, vdev->instance);

	if ((vxge_check_acc_handle(hdev->regh0) != DDI_FM_OK) ||
	    (vxge_check_acc_handle(hdev->cfgh) != DDI_FM_OK)) {
		ret = DDI_FAILURE;
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: can't verify FMA acc handle",
		    VXGE_IFNAME, vdev->instance);

		ddi_fm_service_impact(dev_info, DDI_SERVICE_LOST);
		goto _exit4;
	}

	/* store ll as a HAL private part */
	vxge_hal_device_private_set(hldev, vdev);

	for (i = 0, j = 0; i < VXGE_HAL_MAX_VIRTUAL_PATHS; i++) {
		if (!bVAL1(ll_config.device_hw_info.vpath_mask, i))
			continue;
		vdev->vpaths[j].is_configured = 1;
		vdev->vpaths[j].id = i;
		vdev->vpaths[j].vdev = vdev;
		vdev->vpaths[j].ring.vdev = vdev;
		vdev->vpaths[j].fifo.vdev = vdev;

		(void) memcpy((u8 *)vdev->vpaths[j].macaddr,
		    (u8 *)(ll_config.device_hw_info.mac_addrs[i]), 6);
		j++;
	}

	/* map the hashing selector table to the configured vpaths */
	vxge_os_memzero(&vdev->vpath_selector, sizeof (vdev->vpath_selector));

	vxge_os_memcpy(vdev->vpath_selector, vpath_selector,
	    (sizeof (int) * vdev->no_of_vpath));

	(void) vxge_hal_mrpcim_setpause_data(hldev, 0,
	    ll_config.flow_control_gen, ll_config.flow_control_rcv);

	ret = ddi_intr_add_softint(dev_info, &vdev->soft_hdl,
	    DDI_INTR_SOFTPRI_DEFAULT, vxge_soft_intr_link_handler, vdev);
	if (ret != DDI_SUCCESS) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: Soft interrupt for link handler\
		    initialisation failed. \n",
		    VXGE_IFNAME, vdev->instance);
		goto _exit4;
	}

	ret = ddi_intr_add_softint(dev_info, &vdev->soft_hdl_alarm,
	    DDI_INTR_SOFTPRI_DEFAULT, vxge_soft_intr_reset_handler, vdev);
	if (ret != DDI_SUCCESS) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: Soft interrupt for reset handler\
		    initialisation failed. \n",
		    VXGE_IFNAME, vdev->instance);
		goto _exit4a;
	}

	ret = ddi_intr_add_softint(dev_info, &vdev->soft_hdl_status,
	    DDI_INTR_SOFTPRI_DEFAULT, vxge_soft_state_check_handler, vdev);
	if (ret != DDI_SUCCESS) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: Soft interrupt for status check  handler\
		    initialisation failed. \n",
		    VXGE_IFNAME, vdev->instance);
		goto _exit4b;
	}

	vdev->link_state_update = LINK_STATE_UNKNOWN;
	ret = vxge_add_intrs(vdev);
	if (ret != DDI_SUCCESS) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: vxge_add_intrs Failed \n",
		    VXGE_IFNAME, vdev->instance);
		goto _exit5;
	}

	/* allocate and register Link Layer */
	ret = vxge_device_register(vdev);
	if (ret != DDI_SUCCESS)
		goto _exit6;

	/*
	 * Workaround for MSI-X to INT-A transition issue:
	 * If not using MSI-X, disable MSI-X enable bit in
	 * MSI-X control register
	 */
	if (vdev->intr_type == DDI_INTR_TYPE_FIXED) {
		/* Get capability pointer */
		vxge_os_pci_read8(hdev->pdev, hdev->cfgh,
		    vxge_offsetof(vxge_hal_pci_config_le_t,
		    capabilities_pointer), &cap_ptr);

		/* Search for MSI-X capability */
		while (cap_ptr != PCI_CAP_NEXT_PTR_NULL) {
			vxge_os_pci_read8(hdev->pdev, hdev->cfgh,
			    (cap_ptr + PCI_CAP_ID), &cap_id);
			if (cap_id == VXGE_HAL_PCI_CAP_ID_MSIX) {
				/* Found MSI-X capability */
				u16 msix_control;

				vxge_os_pci_read16(hdev->pdev, hdev->cfgh,
				    (cap_ptr + 2), &msix_control);

				/* MSI-X enable bit = 0 */
				msix_control &= ~VXGE_HAL_PCI_MSIX_FLAGS_ENABLE;

				vxge_os_pci_write16(hdev->pdev, hdev->cfgh,
				    (cap_ptr + 2), msix_control);
				break;
			} else {
				/* Get next capability */
				vxge_os_pci_read8(hdev->pdev, hdev->cfgh,
				    (cap_ptr + PCI_CAP_NEXT_PTR), &cap_ptr);
				continue;
			}
		}
	}

	/* Print type of adapter */
	vxge_os_pci_read16(hdev->pdev, hdev->cfgh,
	    vxge_offsetof(vxge_hal_pci_config_le_t, subsystem_id),
	    &ss_id);
	ss_id &= 0x1C00;
	ss_id = ss_id >> 10;

	/* Is Titan or Titan1a */
	vdev->dev_revision = vxge_hal_device_check_id(hdev);

	vxge_os_printf("vxge%d: Neterion %s Server Adapter\n", vdev->instance,
	    ll_config.device_hw_info.product_description);

	vxge_os_printf("vxge%d: Driver   v%d.%d.%d\n",
	    vdev->instance, VXGE_VERSION_MAJOR, VXGE_VERSION_MINOR,
	    GENERATED_BUILD_VERSION);

	vxge_os_printf("vxge%d: Serial number: %s\n", vdev->instance,
	    ll_config.device_hw_info.serial_number);

	vxge_os_printf("vxge%d: Part number: %s\n", vdev->instance,
	    ll_config.device_hw_info.part_number);

	vxge_os_printf("vxge%d: Ethernet Address: "
	    "%02X:%02X:%02X:%02X:%02X:%02X\n",
	    vdev->instance, vdev->vpaths[0].macaddr[0],
	    vdev->vpaths[0].macaddr[1], vdev->vpaths[0].macaddr[2],
	    vdev->vpaths[0].macaddr[3], vdev->vpaths[0].macaddr[4],
	    vdev->vpaths[0].macaddr[5]);

	vxge_os_printf("vxge%d: Firmware %s, Date: %s\n",
	    vdev->instance, ll_config.device_hw_info.fw_version.version,
	    ll_config.device_hw_info.fw_date.date);

	vxge_os_printf("vxge%d: Adapter Revision %s\n",
	    vdev->instance,
	    (vdev->dev_revision == VXGE_HAL_CARD_TITAN_1A) ?
	    "1A" : (vdev->dev_revision == VXGE_HAL_CARD_TITAN_1) ?
	    "1" : "Unknown");

	if (!ll_config.device_hw_info.func_id) {
		vxge_os_printf("vxge%d: %s Enabled\n", vdev->instance,
		    vxge_func_mode_names[vdev->dev_func_mode]);
	}

	vxge_os_printf("vxge%d: Interrupt type: %s\n", vdev->instance,
	    (vdev->intr_type == DDI_INTR_TYPE_MSIX) ? "MSI-X" : "INTA");

	vxge_os_printf("vxge%d: Large send offload - %s\n", vdev->instance,
	    ll_config.lso_enable ? "Enabled" : "Disabled");

	vxge_os_printf("vxge%d: RTH steering - %s\n", vdev->instance,
	    device_config->rth_en ? "Enabled" : "Disabled");

	/* Detect and print negotiated bus width */
	/* Get capability pointer */
	vxge_os_pci_read8(hdev->pdev, hdev->cfgh,
	    vxge_offsetof(vxge_hal_pci_config_le_t,
	    capabilities_pointer), &cap_ptr);

	/* Search for PCI Express capability */
	while (cap_ptr != PCI_CAP_NEXT_PTR_NULL) {
		vxge_os_pci_read8(hdev->pdev, hdev->cfgh,
		    (cap_ptr + PCI_CAP_ID), &cap_id);
		if (cap_id == PCI_CAP_ID_PCI_E) {
			/* Link Capabilities Register (0x0C) */
			vxge_os_pci_read16(hdev->pdev, hdev->cfgh,
			    (cap_ptr + 0x0C), &link_capabilities);

			/* Link Status Register (0x12) */
			vxge_os_pci_read16(hdev->pdev, hdev->cfgh,
			    (cap_ptr + 0x12), &link_status);

			neg_link_width = ((link_status >> 4) & 0x1F);

			vxge_os_printf("vxge%d: Link Width: X%d",
			    vdev->instance, neg_link_width);
			break;
		} else {
			/* Get next capability */
			vxge_os_pci_read8(hdev->pdev, hdev->cfgh,
			    (cap_ptr + PCI_CAP_NEXT_PTR), &cap_ptr);
			continue;
		}
	}

	/* set device trace mask */
	vxge_hal_device_debug_set(vdev->devh, vdev->debug_module_level,
	    vdev->debug_module_mask);

	kmem_free(device_config, sizeof (vxge_hal_device_config_t));
	total_vxge.vxge_dev[vxge_dev_no].num_func++;

	if (!ll_config.device_hw_info.func_id) {
		total_vxge.cur_devs++;
	}

	vdev->periodic_id = ddi_periodic_add(vxge_check_status, vdev,
	    VXGE_CYCLIC_PERIOD, DDI_IPL_0);

	vxge_os_printf("vxge%d: DDI_ATTACH successful\n", vdev->instance);
	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_ATTACH EXIT cmd %d",
	    VXGE_IFNAME, vdev->instance, cmd);

	return (DDI_SUCCESS);

_exit6:
	vxge_rem_intrs(vdev);
_exit5:
	(void) ddi_intr_remove_softint(vdev->soft_hdl_status);
_exit4b:
	(void) ddi_intr_remove_softint(vdev->soft_hdl_alarm);
_exit4a:
	(void) ddi_intr_remove_softint(vdev->soft_hdl);
_exit4:
	vxge_hal_device_terminate(hldev);
_exit3:
	vxge_fm_ereport(vdev, DDI_FM_DEVICE_INVAL_STATE);
	ddi_fm_service_impact(dev_info, DDI_SERVICE_LOST);
	vxge_free_intrs(vdev);
_exit2:
	pci_config_teardown(&attr.cfgh);
_exit1:
	vxge_fm_fini(vdev);
	vxge_device_free(vdev);
_exit0c:
	ddi_regs_map_free(&attr.regh0);
_exit0a:
	kmem_free(device_config, sizeof (vxge_hal_device_config_t));
_exit0:
	vxge_free_total_dev();
	vxge_os_printf("vxge DDI_ATTACH failed\n");
	return (ret);
}

/*
 * suspend transmit/receive for powerdown
 */
static int
vxge_suspend(vxgedev_t *vdev)
{
	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_SUSPEND ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_ENTER GENLOCK",
	    VXGE_IFNAME, vdev->instance);
	mutex_enter(&vdev->genlock);

	vdev->in_reset = 1;
	if (vdev->is_initialized) {
		(void) vxge_initiate_stop(vdev);
		vdev->need_start = 1;
	}

	mutex_exit(&vdev->genlock);
	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_EXIT GENLOCK",
	    VXGE_IFNAME, vdev->instance);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_SUSPEND EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (DDI_SUCCESS);
}

/*
 * quiesce entry point
 */
/*ARGSUSED*/
static int vxge_quiesce(dev_info_t *dev_info)
{
	vxgedev_t *vdev;
	vxge_hal_device_t *hldev;

	hldev = (vxge_hal_device_t *)ddi_get_driver_private(dev_info);

	if (hldev == NULL) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: unable to resume: unknown error",
		    VXGE_IFNAME, vdev->instance);
		return (DDI_FAILURE);
	}

	if (hldev->pdev != dev_info) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: Resume failed: "
		    "Data structures aren't consistent",
		    VXGE_IFNAME, vdev->instance);
		return (DDI_FAILURE);
	}

	vdev = vxge_hal_device_private_get(hldev);

	vxge_hal_device_intr_disable(vdev->devh);
	vxge_disable_intrs(vdev);
	return (DDI_SUCCESS);
}

/*
 * vxge_detach
 *
 * This function is called by OS when the system is about
 * to shutdown or when the super user tries to unload
 * the driver. This function frees all the memory allocated
 * during vxge_attch() and also unregisters the X3100
 * device instance from the GLD framework.
 */
static int
vxge_detach(dev_info_t *dev_info, ddi_detach_cmd_t cmd)
{
	vxge_hal_device_t *hldev;
	vxge_hal_device_attr_t attr;
	vxgedev_t *vdev;
	vxge_hal_status_e status = DDI_SUCCESS;
	vxge_ring_t *ring = NULL;
	int i;

	hldev = (vxge_hal_device_t *)ddi_get_driver_private(dev_info);
	vdev = vxge_hal_device_private_get(hldev);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_DETACH ENTRY cmd %d",
	    VXGE_IFNAME, vdev->instance, cmd);

	attr.cfgh  = hldev->cfgh;
	attr.regh0 = hldev->regh0;

	vxge_debug_driver(VXGE_TRACE, hldev, NULL_VPID,
	    "%s%d: VXGE_DETACH cmd %d HAL magic %d",
	    VXGE_IFNAME, vdev->instance, cmd, hldev->magic);

	switch (cmd) {
	case DDI_DETACH:
		break;

	case DDI_SUSPEND:
		return (vxge_suspend(vdev));

	default:
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: cmd 0x%x unrecognized",
		    VXGE_IFNAME, vdev->instance, cmd);
		return (DDI_FAILURE);
	}

	/*
	 * Removing watchdog for stall check
	 */
	if (vdev->periodic_id != NULL) {
		ddi_periodic_delete(vdev->periodic_id);
		vdev->periodic_id = NULL;
	}

	/*
	 * Removing stale parts.
	 */
	mutex_enter(&vdev->genlock);

	/*
	 * Make sure that buffer pool is freed and vpaths are closed
	 * during detach even if interface was not plumbed and unplumbed
	 */
	for (i = 0; i < vdev->no_of_vpath; i++) {
		ring = (vxge_ring_t *)&(vdev->vpaths[i].ring);
		if (ring->bf_pool.post != 0) {
			status = DDI_FAILURE;
		}
	}

	mutex_exit(&vdev->genlock);

	if (status != DDI_SUCCESS) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: Unable to stop device\n",
		    VXGE_IFNAME, vdev->instance);
		goto _exit0;
	}

	(void) ddi_intr_remove_softint(vdev->soft_hdl);
	(void) ddi_intr_remove_softint(vdev->soft_hdl_alarm);
	(void) ddi_intr_remove_softint(vdev->soft_hdl_status);

	vxge_hal_device_terminating(hldev);
	if (vxge_device_unregister(vdev) != DDI_SUCCESS) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: Unable to unregister device\n",
		    VXGE_IFNAME, vdev->instance);
		goto _exit0;
	}

	if (vxge_check_acc_handle(hldev->regh0) != DDI_FM_OK)
		ddi_fm_service_impact(dev_info, DDI_SERVICE_UNAFFECTED);

	vxge_hal_device_terminate(hldev);
	vxge_debug_init(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: Device terminated\n",
	    VXGE_IFNAME, vdev->instance);

	vxge_rem_intrs(vdev);
	vxge_debug_init(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d Removed interrupts\n", VXGE_IFNAME,
	    vdev->instance);

	vxge_free_intrs(vdev);
	vxge_debug_init(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: Interrupts freed\n",
	    VXGE_IFNAME, vdev->instance);

	if (!vdev->config.device_hw_info.func_id) {
		total_vxge.cur_devs--;
		vxge_free_total_dev();
	}
	vxge_fm_fini(vdev);
	vxge_device_free(vdev);
	vxge_debug_init(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s", "vxge Device freed\n");

	if (attr.cfgh != NULL)
		pci_config_teardown(&attr.cfgh);

	if (attr.regh0 != NULL)
		ddi_regs_map_free(&attr.regh0);
	ddi_remove_minor_node(dev_info, NULL);

	vxge_os_printf("vxge : DDI_DETACH successful\n");
	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "VXGE_DETACH EXIT cmd %d", cmd);
	return (DDI_SUCCESS);
_exit0:
	vxge_os_printf(" vxge DDI_DETACH failed\n");
	return (DDI_FAILURE);
}

/*ARGSUSED*/
uint_t
vxge_soft_intr_reset_handler(char *arg1, char *arg2)
{
	vxgedev_t *vdev = (void *)arg1;
	int reset_flag = 0;
	/*LINTED*/
	int inst_cnt;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_SOFT_INTR_RESET_HANDLER ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_ENTER SOFT_LOCK_ALARM",
	    VXGE_IFNAME, vdev->instance);

	mutex_enter(&vdev->soft_lock_alarm);
	if (vdev->soft_running_alarm) {
		vdev->soft_running_alarm = 0;
		reset_flag = 1;
	}

	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_EXIT SOFT_LOCK_ALARM",
	    VXGE_IFNAME, vdev->instance);

	mutex_exit(&vdev->soft_lock_alarm);

	if (reset_flag)
		vxge_onerr_reset(vdev);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_SOFT_INTR_RESET_HANDLER EXIT",
	    VXGE_IFNAME, vdev->instance);
	return (DDI_INTR_CLAIMED);
}

/*ARGSUSED*/
uint_t
vxge_soft_state_check_handler(char *arg1, char *arg2)
{
	vxgedev_t *vdev = (void *)arg1;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_SOFT_STATUS_CHECK_HANDLER ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	if (vdev->link_state != LINK_STATE_UP)
		return (DDI_INTR_CLAIMED);

	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_ENTER SOFT_LOCK_STATUS",
	    VXGE_IFNAME, vdev->instance);

	mutex_enter(&vdev->soft_lock_status);
	if (vdev->soft_check_status_running == 0) {
		mutex_exit(&vdev->soft_lock_status);
		return (DDI_INTR_UNCLAIMED);
	}
	vdev->soft_check_status_running = 0;
	if ((vdev->vdev_state & VXGE_STARTED) &&
	    (vdev->resched_retry & VXGE_RESHED_RETRY)) {
		atomic_and_32(&vdev->resched_retry, ~VXGE_RESHED_RETRY);
		mac_tx_update(vdev->mh);
		VXGE_STATS_DRV_INC(xmit_update_cnt);
	}

	if (vdev->vdev_state & VXGE_ERROR) {
		vxge_onerr_reset(vdev);
		VXGE_STATS_DRV_INC(reset_cnt);
		ddi_fm_service_impact(vdev->dev_info, DDI_SERVICE_RESTORED);
	}

	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_EXIT SOFT_LOCK_STATUS",
	    VXGE_IFNAME, vdev->instance);

	mutex_exit(&vdev->soft_lock_status);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_SOFT_STATUS_CHECK_HANDLER EXIT",
	    VXGE_IFNAME, vdev->instance);
	return (DDI_INTR_CLAIMED);
}

/*ARGSUSED*/
uint_t
vxge_soft_intr_link_handler(char *arg1, char *arg2)
{
	vxgedev_t *vdev = (void *)arg1;
	int	 l_state = LINK_STATE_UNKNOWN;
	/*LINTED*/
	int inst_cnt;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_SOFT_INTR_LINK_HANDLER ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: NUTEX_ENTER SOFT_LOCK",
	    VXGE_IFNAME, vdev->instance);

	mutex_enter(&vdev->soft_lock);
	if (vdev->soft_running) {
		vdev->soft_running = 0;
		if ((vdev->link_state !=
		    vdev->link_state_update))
			l_state = vdev->link_state;
	}

	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_EXIT SOFT_LOCK",
	    VXGE_IFNAME, vdev->instance);

	mutex_exit(&vdev->soft_lock);

	/* indicate up only if link status is changed */
	if (l_state != LINK_STATE_UNKNOWN) {
		mac_link_update(vdev->mh, l_state);
		/* Report link state whenver it changes */
		if (l_state == LINK_STATE_UP) {
			VXGE_STATS_DRV_INC(link_up);
			vxge_os_printf("%s%d: Link is up" \
			    "[10 Gbps Full Duplex]\n",
			    VXGE_IFNAME, vdev->instance);
		} else {
			VXGE_STATS_DRV_INC(link_down);
			vxge_os_printf("%s%d: Link is Down\n",
			    VXGE_IFNAME, vdev->instance);
		}
		vdev->link_state_update = l_state;
	}

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_SOFT_INTR_LINK_HANDLER EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (DDI_INTR_CLAIMED);
}
/*
 * vxge_callback_link_up
 *
 * This function called by HAL to notify HW link up state change.
 */
/*ARGSUSED*/
void
vxge_callback_link_up(vxge_hal_device_h devh, void *userdata)
{
	vxgedev_t *vdev = (vxgedev_t *)userdata;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_CALLBACK_LINK_UP ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	vdev->link_state = LINK_STATE_UP;

	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_ENTER SOFT_LOCK",
	    VXGE_IFNAME, vdev->instance);

	mutex_enter(&vdev->soft_lock);
	if (!vdev->soft_running) {
		vdev->soft_running = 1;
		(void) ddi_intr_trigger_softint(vdev->soft_hdl, vdev);
	}

	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_EXIT SOFT_LOCK",
	    VXGE_IFNAME, vdev->instance);

	mutex_exit(&vdev->soft_lock);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_CALLBACK_LINK_UP EXIT ",
	    VXGE_IFNAME, vdev->instance);
}

/*
 * vxge_callback_link_down
 *
 * This function called by HAL to notify HW link down state change.
 */
/*ARGSUSED*/
void
vxge_callback_link_down(vxge_hal_device_h devh, void *userdata)
{
	vxgedev_t *vdev = (vxgedev_t *)userdata;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_CALLBACK_LINK_DOWN ENTRY ",
	    VXGE_IFNAME, vdev->instance);
	vdev->link_state = LINK_STATE_DOWN;

	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_ENTER SOFT_LOCK",
	    VXGE_IFNAME, vdev->instance);

	mutex_enter(&vdev->soft_lock);
	if (!vdev->soft_running) {
		vdev->soft_running = 1;
		(void) ddi_intr_trigger_softint(vdev->soft_hdl, vdev);
	}

	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_EXIT SOFT_LOCK",
	    VXGE_IFNAME, vdev->instance);

	mutex_exit(&vdev->soft_lock);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_CALLBACK_LINK_DOWN EXIT ",
	    VXGE_IFNAME, vdev->instance);
}

/*
 * vxge_rx_buffer_replenish_all
 *
 * To replenish all freed dtr(s) with buffers in free pool. It's called by
 * vxge_rx_buffer_recycle() or vxge_rx_1b_compl().
 * Must be called with pool_lock held.
 */
static void
vxge_rx_buffer_replenish_all(vxge_ring_t *ring)
{
	vxge_hal_rxd_h dtr;
	vxge_rx_buffer_t *rx_buffer;
	vxge_rxd_priv_t *rxd_priv;
	vxgedev_t *vdev = ring->vdev;

	u32 rxd_post_count = 0;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_RX_BUFFER_REPLENISH_ALL ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	vxge_assert(ring);

	vxge_assert(mutex_owned(&ring->bf_pool.pool_lock));

	if (ring->bf_pool.free == 0)
		VXGE_STATS_DRV_INC(ring_buff_pool_free_cnt);

	while (ring->bf_pool.free > 0) {
		if (vxge_hal_ring_rxd_reserve(ring->channelh, &dtr,
		    (void **)&rxd_priv) == VXGE_HAL_OK) {
			rx_buffer = ring->bf_pool.head;

			vxge_assert(rx_buffer);

			ring->bf_pool.head = rx_buffer->next;
			ring->bf_pool.free--;

			vxge_assert(rx_buffer->dma_addr);

			vxge_hal_ring_rxd_1b_set(dtr, rx_buffer->dma_addr,
			    ring->bf_pool.size);
			vxge_debug_rx(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
			    "%s%d: Prepared 1-buffer mode Rx Desc\
			    with vxge_hal_ring_rxd_1b_set\n",
			    VXGE_IFNAME, vdev->instance);

			rxd_priv->rx_buffer = rx_buffer;
			(void) vxge_hal_ring_rxd_post(ring->channelh, dtr);
			vxge_debug_rx(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
			    "%s%d: Posted RXD with" \
			    "vxge_hal_ring_rxd_post\n",
			    VXGE_IFNAME, vdev->instance);

				rxd_post_count++;
		} else {
			VXGE_STATS_DRV_INC(rxd_full_cnt);
			break;
		}
	}

	if (rxd_post_count)
		vxge_hal_ring_rxd_post_post_db(ring->channelh);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_RX_BUFFER_REPLENISH_ALL EXIT ",
	    VXGE_IFNAME, vdev->instance);
}

/*
 * vxge_rx_buffer_release
 *
 * The only thing done here is to put the buffer back to the pool.
 * Calling this function need be protected by mutex, bf_pool.pool_lock.
 */
static void
vxge_rx_buffer_release(vxge_rx_buffer_t *rx_buffer)
{
	vxge_ring_t *ring = rx_buffer->ring;
	/*LINTED*/
	vxgedev_t *vdev = ring->vdev;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_RX_BUFFER_RELEASE ENTRY",
	    VXGE_IFNAME, vdev->instance);

	vxge_assert(mutex_owned(&ring->bf_pool.pool_lock));

	/* Put the buffer back to pool */
	rx_buffer->next = ring->bf_pool.head;
	ring->bf_pool.head = rx_buffer;

	ring->bf_pool.free++;
	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_RX_BUFFER_RELEASE EXIT",
	    VXGE_IFNAME, vdev->instance);
}

/*
 * vxge_rx_buffer_recycle
 *
 * Called by desballoc() to "free" the resource.
 * We will try to replenish all descripters.
 */
static void
vxge_rx_buffer_recycle(char *arg)
{
	vxge_rx_buffer_t *rx_buffer = (void *)arg;
	vxge_ring_t *ring = rx_buffer->ring;

	vxgedev_t *vdev = ring->vdev;
	vxge_rx_buffer_pool_t	*bf_pool = &ring->bf_pool;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_RX_BUFFER_RECYCLE ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	if (bf_pool->live & VXGE_POOL_LIVE) {
		vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
		    "%s%d: MUTEX_ENTER RECYCLE_LOCK",
		    VXGE_IFNAME, vdev->instance);

		mutex_enter(&bf_pool->recycle_lock);

		rx_buffer->next = bf_pool->recycle_head;
		bf_pool->recycle_head = rx_buffer;
		if (bf_pool->recycle_tail == NULL)
			bf_pool->recycle_tail = rx_buffer;
		bf_pool->recycle++;

		/*
		 * Before finding a good way to set this hiwat, just always
		 * call to replenish_all.
		 */
		if (bf_pool->recycle >= VXGE_RX_BUFFER_RECYCLE_CACHE) {
			vxge_debug_rx(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
			    "%s%d: vdev->is_initialized != 0.\
			    VXGE_RX_BUFFER_REPLENISH_ALL",
			    VXGE_IFNAME, vdev->instance);

			vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
			    "%s%d: MUTEX_TRYENTER BF_POOL.POOL_LOCK",
			    VXGE_IFNAME, vdev->instance);

			if (mutex_tryenter(&bf_pool->pool_lock)) {
				bf_pool->recycle_tail->next = bf_pool->head;
				bf_pool->head = bf_pool->recycle_head;
				bf_pool->recycle_head = NULL;
				bf_pool->recycle_tail = NULL;
				bf_pool->post -= bf_pool->recycle;
				bf_pool->free += bf_pool->recycle;
				bf_pool->recycle = 0;

				vxge_rx_buffer_replenish_all(ring);

				vxge_debug_lock(VXGE_TRACE, NULL_HLDEV,
				    NULL_VPID,
				    "%s%d: MUTEX_EXIT BF_POOL.POOL_LOCK",
				    VXGE_IFNAME, vdev->instance);

				mutex_exit(&bf_pool->pool_lock);
			}
		}
		vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
		    "%s%d: MUTEX_EXIT RECYCLE_LOCK",
		    VXGE_IFNAME, vdev->instance);

		mutex_exit(&bf_pool->recycle_lock);
		/*
		 * If the rx_buffer returned after the m_stop, try
		 * destroying the buffer pool.
		 */
		if ((!vdev->is_initialized) && (bf_pool->post > 0)) {
			(void) vxge_rx_destroy_buffer_pool(ring);
		}
	}

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_RX_BUFFER_RECYCLE EXIT ",
	    VXGE_IFNAME, vdev->instance);
}

/*
 * vxge_rx_buffer_alloc
 *
 * Allocate one rx buffer and return with the pointer to the buffer.
 * Return NULL if failed.
 */
static vxge_rx_buffer_t *
vxge_rx_buffer_alloc(vxge_ring_t *ring)
{
	vxge_hal_device_t *hldev;
	void *vaddr;
	ddi_dma_handle_t dma_handle;
	ddi_acc_handle_t dma_acch;
	dma_addr_t dma_addr;
	uint_t ncookies;
	ddi_dma_cookie_t dma_cookie;
	size_t real_size;
	extern ddi_device_acc_attr_t *p_vxge_dev_attr;
	vxge_rx_buffer_t *rx_buffer;
	vxgedev_t *vdev = ring->vdev;
	uint_t buffer_size, block_size, buffers_per_block, bind_size, index,
	    ip_align_pad, block_mtu_limit;

	hldev = (vxge_hal_device_t *)vdev->devh;

	ip_align_pad = (VXGE_ALIGN_16B -
	    ((HEADROOM + ring->bf_pool.size) % VXGE_ALIGN_16B));
	block_mtu_limit = VXGE_BLOCK_SIZE - HEADROOM - ip_align_pad -
	    sizeof (vxge_rx_buffer_t) - VXGE_HAL_MAC_HEADER_MAX_SIZE;
	buffer_size = HEADROOM + ring->bf_pool.size + ip_align_pad +
	    sizeof (vxge_rx_buffer_t);
	block_size =  (vdev->mtu <= block_mtu_limit) ?
	    VXGE_BLOCK_SIZE : buffer_size;
	buffers_per_block = (int)(block_size / buffer_size);
	bind_size = (buffers_per_block * (HEADROOM + ring->bf_pool.size +
	    ip_align_pad)) - HEADROOM;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_RX_BUFFER_ALLOC ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	if (ddi_dma_alloc_handle(hldev->pdev, p_vxge_hal_dma_attr,
	    DDI_DMA_SLEEP, 0, &dma_handle) != DDI_SUCCESS) {
		VXGE_STATS_DRV_INC(ddi_dma_alloc_handle_fail);
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: can not allocate DMA handle",
		    VXGE_IFNAME, vdev->instance);
		goto handle_failed;
	}

	/* reserve some space at the end of the buffer for recycling */
	if (ddi_dma_mem_alloc(dma_handle, block_size, p_vxge_dev_attr,
	    DDI_DMA_STREAMING, DDI_DMA_SLEEP, 0, (caddr_t *)&vaddr, &real_size,
	    &dma_acch) != DDI_SUCCESS) {
		VXGE_STATS_DRV_INC(ddi_dma_mem_alloc_fail);
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: can not allocate DMA-able memory",
		    VXGE_IFNAME, vdev->instance);
		goto mem_failed;
	}
	if ((buffer_size * buffers_per_block) > real_size) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: can not allocate DMA-able memory",
		    VXGE_IFNAME, vdev->instance);
		goto bind_failed;
	}

	if (ddi_dma_addr_bind_handle(dma_handle, NULL, (char *)vaddr + HEADROOM,
	    bind_size, DDI_DMA_READ | DDI_DMA_STREAMING,
	    DDI_DMA_SLEEP, 0, &dma_cookie, &ncookies) != DDI_SUCCESS) {
		VXGE_STATS_DRV_INC(ddi_dma_addr_bind_handle_fail);
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: out of mapping for mblk",
		    VXGE_IFNAME, vdev->instance);
		goto bind_failed;
	}

	if (ncookies != 1 || dma_cookie.dmac_size < bind_size) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: can not handle partial DMA",
		    VXGE_IFNAME, vdev->instance);
		goto check_failed;
	}

	dma_addr = dma_cookie.dmac_laddress;

	for (index = 0; index < buffers_per_block; index++) {
		rx_buffer = (void *)((char *)vaddr +
		    real_size - ((index + 1) * sizeof (vxge_rx_buffer_t)));

		rx_buffer->vaddr = (char *)vaddr +
		    (index * (HEADROOM + ring->bf_pool.size));

		rx_buffer->dma_addr = dma_addr +
		    (index * (HEADROOM + ring->bf_pool.size));

		if (index) {
			rx_buffer->vaddr = (char *)rx_buffer->vaddr +
			    ip_align_pad;
			rx_buffer->dma_addr += ip_align_pad;
		}

		rx_buffer->dma_handle = dma_handle;
		rx_buffer->dma_acch = dma_acch;
		rx_buffer->ring = ring;
		rx_buffer->frtn.free_func = vxge_rx_buffer_recycle;
		rx_buffer->frtn.free_arg  = (void *)rx_buffer;

		rx_buffer->next = ring->bf_pool.head;
		ring->bf_pool.head = rx_buffer;

		if (index == 0) {
			rx_buffer->blocknext = ring->block_pool;
			ring->block_pool = rx_buffer;
			ring->bf_pool.total++;
		} else {
			rx_buffer->blocknext = NULL;
		}

		ring->bf_pool.free++;
	}

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_RX_BUFFER_ALLOC EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (rx_buffer);

check_failed:
	(void) ddi_dma_unbind_handle(dma_handle);
bind_failed:
	ddi_dma_mem_free(&dma_acch);
mem_failed:
	ddi_dma_free_handle(&dma_handle);
handle_failed:

	return (NULL);
}

/*
 * vxge_rx_destroy_buffer_pool
 *
 * Destroy buffer pool. If there is still any buffer hold by upper layer,
 * recorded by bf_pool.post, return DDI_FAILURE to reject to be unloaded.
 */
static int
vxge_rx_destroy_buffer_pool(vxge_ring_t *ring)
{
	vxge_rx_buffer_t *rx_buffer;
	ddi_dma_handle_t  dma_handle;
	ddi_acc_handle_t  dma_acch;
	vxgedev_t *vdev = ring->vdev;
	vxge_rx_buffer_pool_t *bf_pool = &ring->bf_pool;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_RX_DESTROY_BUFFER_POOL ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	if (!(bf_pool->live & VXGE_POOL_LIVE))
		return (DDI_FAILURE);

	mutex_enter(&ring->bf_pool.recycle_lock);
	mutex_enter(&ring->bf_pool.pool_lock);
	if (ring->bf_pool.recycle > 0) {
		ring->bf_pool.recycle_tail->next = ring->bf_pool.head;
		ring->bf_pool.head = ring->bf_pool.recycle_head;
		ring->bf_pool.recycle_tail =
		    ring->bf_pool.recycle_head = NULL;
		ring->bf_pool.post -= ring->bf_pool.recycle;
		ring->bf_pool.free += ring->bf_pool.recycle;
		ring->bf_pool.recycle = 0;
	}
	mutex_exit(&ring->bf_pool.pool_lock);
	mutex_exit(&ring->bf_pool.recycle_lock);

	/*
	 * If there is any posted buffer, the driver should reject to be
	 * detached. Need notice upper layer to release them.
	 */
	if (ring->bf_pool.post != 0) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: has some buffers not be recycled," \
		    "try later!",
		    VXGE_IFNAME, vdev->instance);
		return (DDI_FAILURE);
	}

	/*
	 * Relase buffers one by one.
	 */
	mutex_enter(&ring->bf_pool.pool_lock);
	while (ring->block_pool) {
		rx_buffer = ring->block_pool;
		ring->block_pool = rx_buffer->blocknext;

		vxge_assert(rx_buffer != NULL);

		dma_handle = rx_buffer->dma_handle;
		dma_acch = rx_buffer->dma_acch;

		if (ddi_dma_unbind_handle(dma_handle) != DDI_SUCCESS) {
			VXGE_STATS_DRV_INC(ddi_dma_addr_unbind_handle_fail);
			vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
			    "%s%d: failed to unbind DMA handle!",
			    VXGE_IFNAME, vdev->instance);
			continue;
		}
		ddi_dma_mem_free(&dma_acch);
		ddi_dma_free_handle(&dma_handle);

		ring->bf_pool.total--;
	}

	ring->bf_pool.free = 0;
	atomic_and_32(&ring->bf_pool.live, ~VXGE_POOL_LIVE);
	mutex_exit(&ring->bf_pool.pool_lock);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_RX_DESTROY_BUFFER_POOL EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (DDI_SUCCESS);
}

/*
 * vxge_rx_create_buffer_pool
 *
 * Initialize RX buffer pool for all RX rings. Refer to rx_buffer_pool_t.
 */
static int
vxge_rx_create_buffer_pool(vxge_ring_t *ring)
{
	vxge_hal_device_t *hldev;
	vxge_rx_buffer_t *rx_buffer;
	vxgedev_t *vdev = ring->vdev;
	int i;
	uint_t buffer_size, block_size, buffers_per_block,
	    ip_align_pad, block_mtu_limit;
	hldev = (vxge_hal_device_t *)vdev->devh;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_RX_CREATE_BUFFER_POOL ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	if (ring->bf_pool.live & VXGE_POOL_LIVE)
		return (DDI_FAILURE);

	ring->bf_pool.total = 0;
	ring->bf_pool.size = vdev->mtu + VXGE_HAL_MAC_HEADER_MAX_SIZE;
	ring->bf_pool.head = NULL;
	ring->bf_pool.free = 0;
	ring->bf_pool.post = 0;
	ring->bf_pool.post_hiwat = vdev->config.rx_buffer_post_hiwat;

	ring->bf_pool.recycle = 0;
	ring->bf_pool.recycle_head = NULL;
	ring->bf_pool.recycle_tail = NULL;

	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_INIT RING->BF_POOL.POOL_LOCK",
	    VXGE_IFNAME, vdev->instance);
	mutex_init(&ring->bf_pool.pool_lock, NULL, MUTEX_DRIVER,
	    hldev->irqh);

	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_INIT RECYCLE_LOCK",
	    VXGE_IFNAME, vdev->instance);

	mutex_init(&ring->bf_pool.recycle_lock, NULL, MUTEX_DRIVER,
	    hldev->irqh);

	atomic_or_32(&ring->bf_pool.live, VXGE_POOL_LIVE);
	/*
	 * Allocate buffers one by one. If failed, destroy whole pool by
	 * call to vxge_rx_destroy_buffer_pool().
	 */

	ip_align_pad = (VXGE_ALIGN_16B -
	    ((HEADROOM + ring->bf_pool.size) % VXGE_ALIGN_16B));
	block_mtu_limit = VXGE_BLOCK_SIZE - HEADROOM - ip_align_pad
	    - sizeof (vxge_rx_buffer_t) - VXGE_HAL_MAC_HEADER_MAX_SIZE;
	buffer_size = HEADROOM + ring->bf_pool.size + ip_align_pad +
	    sizeof (vxge_rx_buffer_t);
	block_size  = (vdev->mtu <= block_mtu_limit) ?
	    VXGE_BLOCK_SIZE : buffer_size;
	buffers_per_block = (int)(block_size / buffer_size);

	ring->block_pool = NULL;

	for (i = 0; i < (vdev->config.rx_buffer_total / buffers_per_block);
	    i++) {
		rx_buffer = vxge_rx_buffer_alloc(ring);
		if (rx_buffer == NULL) {
			vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
			    "%s%d: rx_buffer from\
			    vxge_rx_buffer_alloc = NULL.\
			    VXGE_RX_DESTROY_BUFFER_POOL",
			    VXGE_IFNAME, vdev->instance);
			(void) vxge_rx_destroy_buffer_pool(ring);
			return (DDI_FAILURE);
		}
	}

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_RX_CREATE_BUFFER_POOL EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (DDI_SUCCESS);
}

/*
 * vxge_rx_dtr_replenish
 *
 * Replenish descriptor with rx_buffer in RX buffer pool.
 * The dtr should be post right away.
 */
/*ARGSUSED*/
static vxge_hal_status_e
vxge_rx_dtr_replenish(vxge_hal_vpath_h channelh, vxge_hal_rxd_h dtr,
	void *dtr_priv, u32 index, void *userdata, vxge_hal_reopen_e reopen)
{
	vxge_vpath_t *vpath = (vxge_vpath_t *)userdata;
	/*LINTED*/
	vxgedev_t *vdev = vpath->vdev;
	vxge_rx_buffer_t *rx_buffer;
	vxge_rxd_priv_t *rxd_priv = dtr_priv;
	vxge_ring_t *ring = (vxge_ring_t *)&vpath->ring;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_RX_DTR_REPLENISH ENTRY ",
	    VXGE_IFNAME, vdev->instance);
	if (ring->bf_pool.head == NULL) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: no more available rx DMA buffer!",
		    VXGE_IFNAME, vdev->instance);
		return (VXGE_HAL_FAIL);
	}
	rx_buffer = ring->bf_pool.head;
	ring->bf_pool.head = rx_buffer->next;
	ring->bf_pool.free--;

	vxge_assert(rx_buffer);
	vxge_assert(rx_buffer->dma_addr);

	vxge_hal_ring_rxd_1b_set(dtr, rx_buffer->dma_addr, ring->bf_pool.size);
	vxge_debug_rx(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: Prepared 1-buffer mode Rx Desc\
	    with vxge_hal_ring_rxd_1b_set\n",
	    VXGE_IFNAME, vdev->instance);

	rxd_priv->rx_buffer = rx_buffer;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_RX_DTR_REPLENISH ENTRY ",
	    VXGE_IFNAME, vdev->instance);
	return (VXGE_HAL_OK);
}

/*
 * vxge_get_ip_offset
 *
 * Calculate the offset to IP header.
 */
static inline int
vxge_get_ip_offset(vxge_hal_ring_rxd_info_t *ext_info)
{
	int ip_off;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s: VXGE_GET_IP_OFFSET ENTRY ", VXGE_IFNAME);

	/* get IP-header offset */
	switch (ext_info->frame) {
	case VXGE_HAL_FRAME_TYPE_DIX:
		ip_off = VXGE_HAL_HEADER_ETHERNET_II_802_3_SIZE;
		break;
	case VXGE_HAL_FRAME_TYPE_IPX:
		ip_off = (VXGE_HAL_HEADER_ETHERNET_II_802_3_SIZE +
		    VXGE_HAL_HEADER_802_2_SIZE + VXGE_HAL_HEADER_SNAP_SIZE);
		break;
	case VXGE_HAL_FRAME_TYPE_LLC:
		ip_off = (VXGE_HAL_HEADER_ETHERNET_II_802_3_SIZE +
		    VXGE_HAL_HEADER_802_2_SIZE);
		break;
	case VXGE_HAL_FRAME_TYPE_SNAP:
		ip_off = (VXGE_HAL_HEADER_ETHERNET_II_802_3_SIZE +
		    VXGE_HAL_HEADER_SNAP_SIZE);
		break;
	default:
		ip_off = 0;
		break;
	}

	if ((ext_info->proto & VXGE_HAL_FRAME_PROTO_IPV4 ||
	    ext_info->proto & VXGE_HAL_FRAME_PROTO_IPV6) &&
	    (ext_info->is_vlan)) {
		ip_off += VXGE_HAL_HEADER_VLAN_SIZE;
	}

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s: VXGE_GET_IP_OFFSET EXIT ", VXGE_IFNAME);

	return (ip_off);
}

/*
 * vxge_rx_hcksum_assoc
 *
 * Judge the packet type and then call to hcksum_assoc() to associate
 * h/w checksum information.
 */
static inline void
vxge_rx_hcksum_assoc(mblk_t *mp, char *vaddr, int pkt_length,
	vxge_hal_ring_rxd_info_t *ext_info)
{
	int cksum_flags = 0;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s: VXGE_RX_HCKSUM_ASSOC ENTRY ", VXGE_IFNAME);

	if (!(ext_info->proto & VXGE_HAL_FRAME_PROTO_IP_FRAG)) {
		if (ext_info->proto & VXGE_HAL_FRAME_PROTO_TCP_OR_UDP) {
			if (ext_info->l3_cksum_valid)
				cksum_flags |= HCK_IPV4_HDRCKSUM;
			if (ext_info->l4_cksum_valid)
				cksum_flags |= HCK_FULLCKSUM_OK;
			if (cksum_flags) {
				cksum_flags |= HCK_FULLCKSUM;
				(void) hcksum_assoc(mp, NULL, NULL, 0,
				    0, 0, 0, cksum_flags, 0);
			}
		}
	} else if (ext_info->proto &
	    (VXGE_HAL_FRAME_PROTO_IPV4 | VXGE_HAL_FRAME_PROTO_IPV6)) {
		/*
		 * Just pass the partial cksum up to IP.
		 */
		int ip_off = vxge_get_ip_offset(ext_info);
		int start, end = pkt_length - ip_off;

		if (ext_info->proto & VXGE_HAL_FRAME_PROTO_IPV4) {
			struct ip *ip = (void *)(vaddr + ip_off);
			start = ip->ip_hl * 4 + ip_off;
		} else {
			start = ip_off + 40;
		}
		cksum_flags |= HCK_PARTIALCKSUM;

		(void) hcksum_assoc(mp, NULL, NULL, start, 0, end,
		    ntohs(ext_info->l4_cksum), cksum_flags, 0);
	}
	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s: VXGE_RX_HCKSUM_ASSOC EXIT ", VXGE_IFNAME);
}

/*
 * vxge_rx_1b_msg_alloc
 *
 * Allocate message header for data buffer, and decide if copy the packet to
 * new data buffer to release big rx_buffer to save memory.
 *
 * If the pkt_length <= VXGE_RX_DMA_LOWAT, call allocb() to allocate
 * new message and copy the payload in.
 */
/*ARGSUSED*/
static mblk_t *
vxge_rx_1b_msg_alloc(vxgedev_t *vdev, vxge_ring_t *ring,
		vxge_rx_buffer_t *rx_buffer, int pkt_length,
		vxge_hal_ring_rxd_info_t *ext_info, boolean_t *copyit)
{
	mblk_t *mp;
	char *vaddr;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_RX_IB_MSG_ALLOC ENTRY ",
	    VXGE_IFNAME, vdev->instance);
	vaddr = (char *)rx_buffer->vaddr + HEADROOM;
	/*
	 * Copy packet into new allocated message buffer, if pkt_length
	 * is less than VXGE_RX_DMA_LOWAT
	 */
	if ((vdev->config.rx_dma_lowat &&
	    (*copyit || pkt_length <= vdev->config.rx_dma_lowat))) {
		mp = allocb(pkt_length + HEADROOM, 0);
		if (mp == NULL) {
			VXGE_STATS_DRV_INC(allocb_fail);
			vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
			    "%s%d: allocb failed", VXGE_IFNAME,
			    vdev->instance);
			return (NULL);
		}
		mp->b_rptr += HEADROOM;
		bcopy(vaddr, mp->b_rptr, pkt_length);
		mp->b_wptr = mp->b_rptr + pkt_length;
		*copyit = B_TRUE;
		return (mp);
	}

	/*
	 * Just allocate mblk for current data buffer
	 */
	mp = (mblk_t *)desballoc((unsigned char *)vaddr, pkt_length, 0,
	    &rx_buffer->frtn);
	if (mp == NULL) {
		VXGE_STATS_DRV_INC(desballoc_fail);
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: desballoc failed",
		    VXGE_IFNAME, vdev->instance);
		/* Drop it */
		return (NULL);
	}
	/*
	 * Adjust the b_rptr/b_wptr in the mblk_t structure.
	 */
	mp->b_wptr += pkt_length;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_RX_IB_MSG_ALLOC EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (mp);
}

/* LINTED: static unused */
static inline void vxge_rx_1b_hcksum_ok(mblk_t *mp)
{
	(void) hcksum_assoc(mp, NULL, NULL, 0, 0, 0, 0,
	    HCK_IPV4_HDRCKSUM | HCK_FULLCKSUM_OK | HCK_FULLCKSUM, 0);
}

/*
 * vxge_rx_1b_compl
 *
 * If the interrupt is because of a received frame or if the receive ring
 * contains fresh as yet un-processed frames, this function is called.
 */
static vxge_hal_status_e
vxge_rx_1b_compl(vxge_hal_vpath_h vph, vxge_hal_rxd_h dtr,
	void *dtr_priv, u8 t_code, void *userdata)
{
	vxge_vpath_t *vpath = (vxge_vpath_t *)userdata;
	vxgedev_t *vdev = vpath->vdev;
	vxge_hal_device_t *hldev = vdev->devh;
	vxge_rx_buffer_t *rx_buffer;
	mblk_t *mp_head = NULL;
	mblk_t *mp_end  = NULL;
	int pkt_burst = 0;
	vxge_ring_t *ring = (vxge_ring_t *)&vpath->ring;
	unsigned int pkt_length;
	dma_addr_t dma_data;
	mblk_t *mp;
	boolean_t copyit;
	vxge_hal_ring_rxd_info_t ext_info;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_RX_1B_COMPL ENTRY ",
	    VXGE_IFNAME, vdev->instance);
	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_ENTER RING->BF_POOL.POOL_LOCK",
	    VXGE_IFNAME, vdev->instance);
	mutex_enter(&ring->bf_pool.pool_lock);

	do {
		copyit = B_FALSE;

		vxge_rxd_priv_t *rxd_priv = dtr_priv;

		rx_buffer = rxd_priv->rx_buffer;

		vxge_hal_ring_rxd_1b_get(vph, dtr, &dma_data, &pkt_length);

		pkt_length -= VXGE_FCS_STRIP_SIZE;

		vxge_hal_ring_rxd_1b_info_get(vph, dtr, &ext_info);

		/* vxge_assert(dma_data == rx_buffer->dma_addr); */

		VXGE_STATS_DRV_INC(rx_frms);

		if (t_code != 0) {
			vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
			    "%s%d: rx: dtr 0x%"PRIx64
			    " completed due to error t_code %01x",
			    VXGE_IFNAME, vdev->instance,
			    (uint64_t)(uintptr_t)dtr, t_code);

			(void) vxge_hal_ring_handle_tcode(vph, dtr, t_code);
			vxge_hal_ring_rxd_free(vph, dtr); /* drop it */
			vxge_rx_buffer_release(rx_buffer);
			VXGE_STATS_DRV_INC(rx_tcode_cnt);
			continue;
		}

		/*
		 * Sync the DMA memory
		 */
		(void) ddi_dma_sync(rx_buffer->dma_handle, 0, pkt_length,
		    DDI_DMA_SYNC_FORKERNEL);

		if (vxge_check_dma_handle(hldev->pdev, rx_buffer->dma_handle) !=
		    VXGE_HAL_OK) {
			vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
			    "%s%d: rx: can not do DMA sync",
			    VXGE_IFNAME, vdev->instance);
			vxge_hal_ring_rxd_free(vph, dtr); /* drop it */
			vxge_rx_buffer_release(rx_buffer);
			VXGE_STATS_DRV_INC(dma_sync_fail_cnt);
			continue;
		}

		/*
		 * Allocate message for the packet.
		 */
		if (vdev->config.rx_dma_lowat &&
		    ring->bf_pool.post > ring->bf_pool.post_hiwat) {
			VXGE_STATS_DRV_INC(copyit_mblk_buff_cnt);
			copyit = B_TRUE;
		} else {
			copyit = B_FALSE;
		}

		mp = vxge_rx_1b_msg_alloc(vdev, ring, rx_buffer, pkt_length,
		    &ext_info, &copyit);

		vxge_hal_ring_rxd_free(vph, dtr);

		/*
		 * Release the buffer and recycle it later
		 */
		if ((mp == NULL) || copyit) {
			vxge_rx_buffer_release(rx_buffer);
		} else {
			/*
			 * Count it since the buffer should be loaned up.
			 */
			ring->bf_pool.post++;
		}
		if (mp == NULL) {
			vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
			    "%s%d: rx: can not allocate mp mblk",
			    VXGE_IFNAME, vdev->instance);
			continue;
		}

		/*
		 * Associate cksum_flags per packet type and h/w
		 * cksum flags.
		 */
		vxge_rx_hcksum_assoc(mp, (char *)rx_buffer->vaddr +
		    HEADROOM, pkt_length, &ext_info);

		VXGE_RX_1B_LINK(mp, mp_head, mp_end);

		ring->ring_rx_cmpl_cnt++;

		if (++pkt_burst < vdev->config.rx_pkt_burst)
			continue;

		vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
		    "%s%d: MUTEX_EXIT BF_POOL.POOL_LOCK",
		    VXGE_IFNAME, vdev->instance);

		mutex_exit(&ring->bf_pool.pool_lock);

		if (mp_head) {
			mac_rx(vdev->mh, ring->handle, mp_head);
			mp_head = mp_end = NULL;
		}

		pkt_burst = 0;

		vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
		    "%s%d: MUTEX_ENTER BF_POOL.POOL_LOCK",
		    VXGE_IFNAME, vdev->instance);

		mutex_enter(&ring->bf_pool.pool_lock);

		if (!vdev->is_initialized)
			break;
	} while (vxge_hal_ring_rxd_next_completed(vph, &dtr, &dtr_priv,
	    &t_code) == VXGE_HAL_OK);

	/*
	 * Always call replenish_all to recycle rx_buffers.
	 */
	vxge_rx_buffer_replenish_all(ring);

	if (vxge_check_acc_handle(hldev->regh0) != DDI_FM_OK) {
		ddi_fm_service_impact(vdev->dev_info, DDI_SERVICE_DEGRADED);
		atomic_or_32(&vdev->vdev_state, VXGE_ERROR);
	}

	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_EXIT BF_POOL.POOL_LOCK",
	    VXGE_IFNAME, vdev->instance);

	mutex_exit(&ring->bf_pool.pool_lock);
	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_EXIT RING->BF_POOL.POOL_LOCK",
	    VXGE_IFNAME, vdev->instance);

	if (mp_head) {
		mac_rx(vdev->mh, ring->handle, mp_head);
		mp_head = mp_end = NULL;
	}

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_RX_1B_COMPL EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (VXGE_HAL_OK);
}


/*
 * vxge_xmit_compl
 *
 * If an interrupt was raised to indicate DMA complete of the Tx packet,
 * this function is called. It identifies the last TxD whose buffer was
 * freed and frees all skbs whose data have already DMA'ed into the NICs
 * internal memory.
 */
static vxge_hal_status_e
vxge_xmit_compl(vxge_hal_vpath_h vph, vxge_hal_txdl_h dtr,
	void *dtr_priv, vxge_hal_fifo_tcode_e t_code, void *userdata)
{
	vxge_vpath_t *vpath = (vxge_vpath_t *)userdata;
	vxgedev_t *vdev = vpath->vdev;
	vxge_fifo_t *ring = (vxge_fifo_t *)&vpath->fifo;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_XMIT_COMPL ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	do {
		vxge_txd_priv_t *txd_priv = dtr_priv;
		mblk_t *mp = txd_priv->mblk;
		int i;

		if (t_code) {
			vxge_debug_driver(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
			    "%s%d: tx: dtr 0x%"PRIx64
			    " completed due to error t_code %01x",
			    VXGE_IFNAME, vdev->instance,
			    (uint64_t)(uintptr_t)dtr, t_code);

			(void) vxge_hal_fifo_handle_tcode(vph, dtr, t_code);
			VXGE_STATS_DRV_INC(tx_tcode_cnt);
		}

		for (i = 0; i < txd_priv->handle_cnt; i++) {
			vxge_assert(txd_priv->dma_handles[i]);
			(void) ddi_dma_unbind_handle(txd_priv->dma_handles[i]);
			ddi_dma_free_handle(&txd_priv->dma_handles[i]);
			txd_priv->dma_handles[i] = 0;
		}

		vxge_hal_fifo_txdl_free(vph, dtr);
		vxge_debug_tx(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
		    "%s%d: Freed Tx Descriptor\n",
		    VXGE_IFNAME, vdev->instance);

		freemsg(mp);
		vdev->resched_avail++;
		VXGE_STATS_DRV_INC(xmit_compl_cnt);

		ring->fifo_tx_cmpl_cnt++;

		if (!vdev->is_initialized)
			break;

	} while (vxge_hal_fifo_txdl_next_completed(vph, &dtr, &dtr_priv,
	    &t_code) == VXGE_HAL_OK);

	if ((vdev->resched_retry & VXGE_RESHED_RETRY) &&
	    (vxge_hal_fifo_free_txdl_count_get(vpath->handle))) {
		atomic_and_32(&vdev->resched_retry, ~VXGE_RESHED_RETRY);
		mac_tx_update(vdev->mh);
		VXGE_STATS_DRV_INC(xmit_update_cnt);
	}

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_XMIT_COMPL EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (VXGE_HAL_OK);
}

/* select a vpath to trasmit the packet */
u32
vxge_get_vpath_no(vxgedev_t *vdev, mblk_t *mp)
{
	u16 queue_len, counter = 0;
	struct ether_header *eth = (void *)mp->b_rptr;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_GET_VPATH_NO ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	if (eth->ether_type == htons(ETHERTYPE_IP)) {
		struct ip *ip = (void *)
		    (mp->b_rptr + sizeof (struct ether_header));
		if ((ip->ip_p == IPPROTO_TCP) || (ip->ip_p == IPPROTO_UDP)) {
			if ((ip->ip_off & htons(IPH_OFFSET|IP_MF)) == 0) {
				struct tcphdr *th;
				th = (void *)
				    (((unsigned char *)ip) + ip->ip_hl*4);
				queue_len = vdev->no_of_vpath;
				counter = (ntohs(th->th_sport) +
				    ntohs(th->th_dport)) &
				    vdev->vpath_selector[queue_len - 1];
				if (counter >= queue_len)
					counter = queue_len - 1;
			}
		}
	}

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_GET_VPATH_NO EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (counter);
}

/*
 * vxge_rx_dtr_term
 *
 * Function will be called by HAL to terminate all DTRs for
 * Ring(s) type of channels.
 */
/*ARGSUSED*/
static void
vxge_rx_dtr_term(vxge_hal_vpath_h vph, vxge_hal_rxd_h dtrh,
	void *dtr_priv, vxge_hal_rxd_state_e state, void *userdata,
	vxge_hal_reopen_e reopen)
{
	vxge_vpath_t *vpath = (vxge_vpath_t *)userdata;
	vxge_ring_t *ring = (vxge_ring_t *)&vpath->ring;
	/*LINTED*/
	vxgedev_t *vdev = vpath->vdev;
	vxge_rxd_priv_t *rxd_priv = dtr_priv;
	vxge_rx_buffer_t *rx_buffer = rxd_priv->rx_buffer;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_RX_DTR_TERM ENTRY ",
	    VXGE_IFNAME, vdev->instance);
	if (ring->bf_pool.live & VXGE_POOL_LIVE) {
		if (state == VXGE_HAL_RXD_STATE_POSTED) {
			vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
			    "%s%d: MUTEX_ENTER RING->BF_POOL.POOL_LOCK",
			    VXGE_IFNAME, vdev->instance);
			mutex_enter(&ring->bf_pool.pool_lock);
			if (rx_buffer)
				vxge_rx_buffer_release(rx_buffer);
			vxge_debug_tx(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
			    "%s%d: Rx buffer released \n",
			    VXGE_IFNAME, vdev->instance);
			mutex_exit(&ring->bf_pool.pool_lock);
			vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
			    "%s%d: MUTEX_EXIT RING->BF_POOL.POOL_LOCK",
			    VXGE_IFNAME, vdev->instance);
		}
	}

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_RX_DTR_TERM EXIT ",
	    VXGE_IFNAME, vdev->instance);
}

/*
 * vxge_tx_term
 *
 * Function will be called by HAL to terminate all DTRs for
 * Fifo(s) type of channels.
 */
/*ARGSUSED*/
static void
vxge_tx_term(vxge_hal_vpath_h vph, vxge_hal_txdl_h dtrh,
	void *dtr_priv, vxge_hal_txdl_state_e state, void *userdata,
	vxge_hal_reopen_e reopen)
{
	vxge_txd_priv_t *txd_priv = dtr_priv;
	mblk_t *mp = txd_priv->mblk;
	int i;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_TX_TERM ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	/*
	 * for Tx we must clean up the DTR *only* if it has been
	 * posted!
	 */
	if (state != VXGE_HAL_TXDL_STATE_POSTED)
		return;

	for (i = 0; i < txd_priv->handle_cnt; i++) {
		vxge_assert(txd_priv->dma_handles[i]);
		(void) ddi_dma_unbind_handle(txd_priv->dma_handles[i]);
		ddi_dma_free_handle(&txd_priv->dma_handles[i]);
		txd_priv->dma_handles[i] = 0;
	}

	freemsg(mp);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_TX_TERM EXIT ",
	    VXGE_IFNAME, vdev->instance);
}

/*
 * vpath_cb_fn
 * Virtual path Callback functio
 */
/*ARGSUSED*/
static vxge_hal_status_e
vpath_cb_fn(vxge_hal_client_h client_handle, vxge_hal_up_msg_h msgh,
	vxge_hal_message_type_e msg_type, vxge_hal_obj_id_t obj_id,
	vxge_hal_result_e result, vxge_hal_opaque_handle_t *opaque_handle)
{
	return (VXGE_HAL_OK);
}

/* RTH configuration */
static vxge_hal_status_e
vxge_rth_configure(vxgedev_t *vdev)
{
	vxge_hal_status_e status = VXGE_HAL_OK;
	u8 mtable[1<<VXGE_RTH_BUCKET_SIZE] = {0}; /* CPU to vpath mapping */

	vxge_hal_rth_hash_types_t hash_types;
	int index;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_RTH_CONFIGURE ENTRY ", VXGE_IFNAME,
	    vdev->instance);

	/*
	 * Filling
	 * 	- matable with bucket-to-vpath mapping
	 */
	for (index = 0; index < (1 << VXGE_RTH_BUCKET_SIZE); index++) {
		mtable[index] = index % vdev->no_of_vpath;
	}

	/* Fill RTH hash types */
	hash_types.hash_type_tcpipv4_en   = VXGE_HAL_RING_HASH_TYPE_TCP_IPV4;
	hash_types.hash_type_ipv4_en	  = VXGE_HAL_RING_HASH_TYPE_IPV4;
	hash_types.hash_type_tcpipv6_en   = VXGE_HAL_RING_HASH_TYPE_TCP_IPV6;
	hash_types.hash_type_ipv6_en	  = VXGE_HAL_RING_HASH_TYPE_IPV6;
	hash_types.hash_type_tcpipv6ex_en = VXGE_HAL_RING_HASH_TYPE_TCP_IPV6_EX;
	hash_types.hash_type_ipv6ex_en	= VXGE_HAL_RING_HASH_TYPE_IPV6_EX;

	/* set indirection table, bucket-to-vpath mapping */
	status = vxge_hal_vpath_rts_rth_itable_set(vdev->vp_handles,
	    vdev->no_of_vpath, mtable, 1 << VXGE_RTH_BUCKET_SIZE);

	if (status != VXGE_HAL_OK) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: RTH indirection table configuration\
		    failed for vpath:%d:\n", VXGE_IFNAME, vdev->instance,
		    vdev->vpaths[0].id);
		return (status);
	}

	vxge_debug_rx(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: Configured Indirection table using \
	    vxge_hal_vpath_rts_rth_itable_set \n",
	    VXGE_IFNAME, vdev->instance);
	/*
	 * Because the itable_set() method uses the active_table field
	 * for the target virtual path the RTH config should be updated
	 * for all VPATHs. The h/w only uses the lowest numbered VPATH
	 * when steering frames.
	 */
	for (index = 0; index < vdev->no_of_vpath; index++) {
		status = vxge_hal_vpath_rts_rth_set(vdev->vpaths[index].handle,
		    RTH_ALG_JENKINS, &hash_types, VXGE_RTH_BUCKET_SIZE, TRUE);

		if (status != VXGE_HAL_OK) {
			vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
			    "%s%d: RTH configuration failed for vpath:%d",
			    VXGE_IFNAME, vdev->instance,
			    vdev->vpaths[0].id);
				return (status);
		}
	}

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_RTH_CONFIGURE EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (status);
}

static int
vxge_open_vpaths(vxgedev_t *vdev)
{
	vxge_hal_status_e status;
	int i = 0, j = 0;
	vxge_hal_vpath_attr_t attr;
	/*LINTED*/
	u64 val64 = 0;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_OPEN_VPATHS ENTRY ",
	    VXGE_IFNAME, vdev->instance);
	for (i = 0; i < vdev->no_of_vpath; i++) {
		vxge_assert(vdev->vpaths[i].is_configured);
		vdev->vpaths_deployed |= VXGE_mBIT(vdev->vpaths[i].id);
		attr.vp_id = vdev->vpaths[i].id;

		/* Virtual Path attr: FIFO */
		attr.fifo_attr.callback = vxge_xmit_compl;
		attr.fifo_attr.per_txdl_space = sizeof (vxge_txd_priv_t);
		attr.fifo_attr.txdl_init = NULL;
		attr.fifo_attr.txdl_term = vxge_tx_term;
		attr.fifo_attr.userdata = &(vdev->vpaths[i]);

		/* Virtual Path attr: Ring */
		attr.ring_attr.callback	  = vxge_rx_1b_compl;
		attr.ring_attr.rxd_init	  = vxge_rx_dtr_replenish;
		attr.ring_attr.rxd_term	  = vxge_rx_dtr_term;
		attr.ring_attr.per_rxd_space = sizeof (vxge_rxd_priv_t);
		attr.ring_attr.userdata = &(vdev->vpaths[i]);
		vxge_ring_t *ring = &(vdev->vpaths[i].ring);
		vxge_fifo_t *fifo = &(vdev->vpaths[i].fifo);

		if (vxge_rx_create_buffer_pool(ring) != DDI_SUCCESS) {
			vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
			    "%s%d: unable to create Ring%d buffer pool",
			    VXGE_IFNAME, vdev->instance, i);
			/* unwind */
			for (j = (i - 1); j >= 0; j--) {
				ring = &(vdev->vpaths[j].ring);
				(void) vxge_hal_vpath_close(
				    vdev->vpaths[j].handle);
				vdev->vpaths[j].is_open = 0;
				vdev->vpaths[j].handle = NULL;
				(void) vxge_rx_destroy_buffer_pool(ring);

				if (!(ring->bf_pool.live & VXGE_POOL_LIVE) &&
				    !(ring->bf_pool.live & VXGE_POOL_DESTROY))
					vxge_vpath_rx_buff_mutex_destroy(ring);
			}
			return (-EPERM);
		}
		ring->opened = 1;
		vxge_debug_init(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
		    "%s%d: Rx buffer pool created\n",
		    VXGE_IFNAME, vdev->instance);
		status = vxge_hal_vpath_open(vdev->devh, &attr,
		    (vxge_hal_vpath_callback_f)vpath_cb_fn, NULL,
		    &(vdev->vpaths[i].handle));

		if (status == VXGE_HAL_OK) {
			vdev->vpaths[i].is_open = 1;
			vdev->vp_handles[i] = vdev->vpaths[i].handle;
			VXGE_STATS_DRV_INC(vpaths_open);
			ring->channelh = vdev->vpaths[i].handle;
			fifo->channelh = vdev->vpaths[i].handle;

			ring->index = i;
			fifo->index = i;

#ifdef VXGE_SET_PRC
			/* Applying PRC_CFG6 (offset 0x00a70) settings */
			status = vxge_hal_mgmt_reg_read(vdev->devh,
			    vxge_hal_mgmt_reg_type_vpath, i, 0x00a70, &val64);
			if (status == VXGE_HAL_OK) {
				val64 &= ~VXGE_HAL_PRC_CFG6_RXD_CRXDT(0x1ff);
				val64 |= VXGE_HAL_PRC_CFG6_RXD_CRXDT(0x1f);

				val64 &= ~VXGE_HAL_PRC_CFG6_RXD_SPAT(0x1ff);
				val64 |= VXGE_HAL_PRC_CFG6_RXD_SPAT(0);

				status = vxge_hal_mgmt_reg_write(
				    vdev->devh, vxge_hal_mgmt_reg_type_vpath,
				    i, 0x00a70, val64);
				if (status != VXGE_HAL_OK) {
					vxge_debug_init(VXGE_ERR, NULL_HLDEV,
					    NULL_VPID,
					    "%s%d: vpath %d: " \
					    "PRC_CFG6 settings" \
					    "failed",
					    VXGE_IFNAME,
					    vdev->instance,
					    i);
				}
			} else {
				vxge_debug_init(VXGE_ERR, NULL_HLDEV, NULL_VPID,
				    "%s%d: vpath%d: Reading PRC_CFG6 failed",
				    VXGE_IFNAME, vdev->instance, i);
			}
#endif
		} else {
			(void) vxge_close_vpaths(vdev);
			VXGE_STATS_DRV_INC(vpath_open_fail);

			vxge_os_printf("vxge%d: vxge_hal_vpath_open FAILED" \
			    "status=%d\n", vdev->instance, status);

			return (-EPERM);
		}
		vxge_debug_init(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
		    "%s%d: Vpath %d opened\n", VXGE_IFNAME,
		    vdev->instance, i);
	}

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_OPEN_VPATHS EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (VXGE_HAL_OK);
}

/* close vpaths */
static int
vxge_close_vpaths(vxgedev_t *vdev)
{
	int i;
	int vpath_status;
	int status = DDI_SUCCESS;
	vxge_ring_t *ring = NULL;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_CLOSE_VPATHS ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	for (i = 0; i < vdev->no_of_vpath; i++) {
		if (!(vdev->vpaths[i].handle && vdev->vpaths[i].is_open))
			continue;

		(void) vxge_hal_vpath_close(vdev->vpaths[i].handle);

		vxge_debug_init(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
		    "%s%d: Vpath %d closed\n", VXGE_IFNAME,
		    vdev->instance, i);

		vdev->vpaths[i].is_open = 0;
		vdev->vpaths[i].handle  = NULL;

		ring = (vxge_ring_t *)&(vdev->vpaths[i].ring);
		if (ring->bf_pool.live & VXGE_POOL_LIVE) {
			vpath_status = vxge_rx_destroy_buffer_pool(
			    &(vdev->vpaths[i].ring));

			if (vpath_status != DDI_SUCCESS) {
				vxge_debug_driver(VXGE_ERR, NULL_HLDEV,
				    NULL_VPID,
				    "%s%d: Cannot free Rx buffers %d",
				    VXGE_IFNAME, vdev->instance, status);
				status = vpath_status;
			}

			if (!(ring->bf_pool.live & VXGE_POOL_LIVE) &&
			    !(ring->bf_pool.live & VXGE_POOL_DESTROY))
				vxge_vpath_rx_buff_mutex_destroy(ring);
		}

		vxge_debug_init(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
		    "%s%d: Rx buffer pool destroyed\n",
		    VXGE_IFNAME, vdev->instance);
	}

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_CLOSE_VPATHS EXIT ",
	    VXGE_IFNAME, vdev->instance);

	return (status);
}

/* enable vlan promisc mode */
static vxge_hal_status_e
vxge_enable_vlan_promisc_mode(vxgedev_t *vdev)
{
	int i;
	vxge_hal_status_e status;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_ENABLE_VLAN_PROMISC_MODE ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	/* set RXMAC_AUTHORIZE_ALL_VID to all 1's */
	if (vxge_hal_mgmt_reg_write(vdev->devh,
	    vxge_hal_mgmt_reg_type_mrpcim, 0, 0x01670, 0xffffffffffffffff)
	    != VXGE_HAL_OK)
		return (VXGE_HAL_FAIL);

	for (i = 0; i < vdev->no_of_vpath; i++) {
		status = vxge_hal_vpath_all_vid_enable(vdev->vpaths[i].handle);
		if (status != VXGE_HAL_OK)
			break;
	}

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_ENABLE_VLAN_PROMISC_MODE EXIT ",
	    VXGE_IFNAME, vdev->instance);

	return (status);
}

static void vxge_restore_promiscuous(vxgedev_t *vdev)
{
	int i;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_RESTORE_PROMISCUOUS ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	for (i = 0; i < VXGE_HAL_MAX_VIRTUAL_PATHS; i++) {
		vxge_hal_vpath_h channelh = vxge_get_vpath(vdev, i);
		struct vxge_vpath *vpath;
		if (!channelh)
			continue;
		vpath = &vdev->vpaths[i];
		if (vpath->promiscuous_mode == VXGE_PROMISC_MODE_ENABLE)
			(void) vxge_hal_vpath_promisc_enable(channelh);
		else
			(void) vxge_hal_vpath_promisc_disable(channelh);
	}

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_RESTORE_PROMISCUOUS EXIT ",
	    VXGE_IFNAME, vdev->instance);
}

/* Enable MSI-X */
static vxge_hal_status_e
vxge_enable_msix(vxgedev_t *vdev)
{
	vxge_hal_status_e status;
	int i;

	/* Unmasking and Setting MSIX vectors before enabling interrupts */
	if (vdev->intr_type == DDI_INTR_TYPE_MSIX) {
		int alarm_vector = vdev->no_of_vpath;
		vxge_vpath_t *curr_vpath;

		/* tim[] : 0 - Tx ## 1 - Rx ## 2 - UMQ-DMQ ## 3 - BITMAP */
		int tim[4] = {0, 1, 0, 0};
		int msix_idx = 0;

		for (i = 0, msix_idx = 0; i < vdev->no_of_vpath; i++) {

			curr_vpath = vdev->vpaths + i;

			tim[0] = msix_idx;
			tim[1] = msix_idx;
			curr_vpath->rx_tx_msix_vec = tim[0];

			vxge_debug_init(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
			    "%s%d: --## tim[0] tim[1] tim[2] tim[3] is:" \
			    " %d %d %d %d", VXGE_IFNAME, vdev->instance,
			    tim[0], tim[1], tim[2], tim[3]);

			status = vxge_hal_vpath_msix_set(curr_vpath->handle,
			    tim, alarm_vector);

			if (status != VXGE_HAL_OK) {
				vxge_debug_driver(VXGE_ERR, NULL_HLDEV,
				    NULL_VPID,
				    "%s%d: vxge_hal_vpath_msix_set,status %d",
				    VXGE_IFNAME, vdev->instance, status);
				return (status);
			}

			curr_vpath->alarm_msix_vec = alarm_vector;

			vxge_hal_vpath_msix_unmask(curr_vpath->handle,
			    curr_vpath->rx_tx_msix_vec);

			vxge_hal_vpath_msix_unmask(curr_vpath->handle,
			    curr_vpath->alarm_msix_vec);

			vxge_debug_init(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
			    "%s%d: --## tim[0] is:%d and\
			    tim[1] is:%d and i is :%d, %d, %d",
			    VXGE_IFNAME, vdev->instance,
			    curr_vpath->rx_tx_msix_vec,
			    curr_vpath->rx_tx_msix_vec, i,
			    vdev->intr_cnt, vdev->no_of_vpath);

			msix_idx++;
		}
	}
	return (VXGE_HAL_OK);
}

static vxge_hal_status_e
vxge_device_enable(vxgedev_t *vdev)
{
	vxge_hal_status_e status = VXGE_HAL_OK;
	vxge_hal_device_t *hldev = vdev->devh;
	int i, ret;
	int maxpkt = vdev->mtu;

	/* Enable RTH */
	if (vdev->rth_enable) {
		status = vxge_rth_configure(vdev);
		if (status != VXGE_HAL_OK) {
			vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
			    "%s%d: rth_configure Failed \n",
			    VXGE_IFNAME, vdev->instance);
			goto exit0;
		}
	}

	/* set initial mtu before enabling the device */
	for (i = 0; i < VXGE_HAL_MAX_VIRTUAL_PATHS; i++) {
		vxge_hal_vpath_h channelh = vxge_get_vpath(vdev, i);
		if (!channelh)
			continue;
		/* check initial mtu before enabling the device */
		status = vxge_hal_device_mtu_check(channelh, maxpkt);
		if (status != VXGE_HAL_OK) {
			vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
			    "%s%d: MTU size %d is invalid",
			    VXGE_IFNAME, vdev->instance, maxpkt);
			goto exit0;
		}
		status = vxge_hal_vpath_mtu_set(channelh, maxpkt);
		if (status != VXGE_HAL_OK) {
			vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
			    "%s%d: can not set new MTU %d",
			    VXGE_IFNAME, vdev->instance, maxpkt);
			goto exit0;
		}

		/* tune jumbo/normal frame UFC counters */
		hldev->config.vp_config[i].rti.uec_b =
		    maxpkt > VXGE_HAL_DEFAULT_MTU ?
		    VXGE_DEFAULT_RX_UFC_B_J :
		    VXGE_DEFAULT_RX_UFC_B_N;

		hldev->config.vp_config[i].rti.uec_c =
		    maxpkt > VXGE_HAL_DEFAULT_MTU ?
		    VXGE_DEFAULT_RX_UFC_C_J :
		    VXGE_DEFAULT_RX_UFC_C_N;
	}

	if (vdev->vlan_promisc_enable) {
		if (vxge_enable_vlan_promisc_mode(vdev) != VXGE_HAL_OK) {
			/*EMPTY*/
			vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
			    "%s%d: VLAN promisc mode set failed",
			    VXGE_IFNAME, vdev->instance);
		}
	}

	/* now, enable the device */
	status = vxge_hal_device_enable(vdev->devh);
	if (status != VXGE_HAL_OK) {
		(void) vxge_close_vpaths(vdev);
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: can not enable the device",
		    VXGE_IFNAME, vdev->instance);
		goto exit0;
	}
	vxge_debug_init(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: Device enabled\n", VXGE_IFNAME, vdev->instance);

	/* Enable MSI-X */
	if (vxge_enable_msix(vdev) != VXGE_HAL_OK)
		goto exit1;

	ret = vxge_enable_intrs(vdev);
	if (ret != DDI_SUCCESS) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: vxge_enable_intrs Failed \n",
		    VXGE_IFNAME, vdev->instance);
		goto exit1;
	}
	vxge_debug_init(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: Enabled interrupts\n", VXGE_IFNAME,
	    vdev->instance);

	/* set link state */
	if (vxge_hal_device_link_state_get(hldev) == VXGE_HAL_LINK_UP)
		vdev->link_state = LINK_STATE_UP;
	else
		vdev->link_state = LINK_STATE_DOWN;

	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_ENTER SOFT_LOCK",
	    VXGE_IFNAME, vdev->instance);

	mutex_enter(&vdev->soft_lock);
	if (!vdev->soft_running) {
		vdev->soft_running = 1;
		(void) ddi_intr_trigger_softint(vdev->soft_hdl, vdev);
	}

	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_EXIT SOFT_LOCK",
	    VXGE_IFNAME, vdev->instance);

	mutex_exit(&vdev->soft_lock);

	/* restore mac/multicast addresses */
	(void) vxge_restore_mac_addr(vdev);

	/* restore promisc mode state */
	vxge_restore_promiscuous(vdev);

	vdev->is_initialized = 1;

	/* time to enable interrupts */
	vxge_hal_device_intr_enable(vdev->devh);

	/* reset stats */
	vxge_reset_stats(vdev);

	for (i = 0; i < vdev->no_of_vpath; i++) {
		status = vxge_hal_vpath_enable((vdev->vpaths[i].handle));
		if (status != VXGE_HAL_OK)
			break;
	}

	if (status == VXGE_HAL_OK)
		goto exit0;

exit2:
	vxge_hal_device_intr_disable(vdev->devh);
	vxge_disable_intrs(vdev);

exit1:
	(void) vxge_hal_device_disable(vdev->devh);
	vxge_fm_ereport(vdev, DDI_FM_DEVICE_INVAL_STATE);
	ddi_fm_service_impact(vdev->dev_info, DDI_SERVICE_LOST);

exit0:
	return (status);
}

int
vxge_initiate_start(vxgedev_t *vdev)
{
	vxge_hal_status_e status;
	vxge_hal_device_t *hldev = vdev->devh;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_INITIATE_START ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	status = vxge_open_vpaths(vdev);
	if (status != VXGE_HAL_OK) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: vxge_open_vpaths failed \n",
		    VXGE_IFNAME, vdev->instance);

		goto exit0;
	}

	vxge_debug_init(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: Vpaths opened", VXGE_IFNAME, vdev->instance);

	if (vxge_device_enable(vdev) != VXGE_HAL_OK)
		goto exit1;

	vxge_kstat_init(vdev);

	vxge_debug_init(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: Device initialized\n", VXGE_IFNAME,
	    vdev->instance);

	if ((vxge_check_acc_handle(hldev->regh0) != DDI_FM_OK) ||
	    (vxge_check_acc_handle(hldev->cfgh) != DDI_FM_OK)) {
		ddi_fm_service_impact(vdev->dev_info, DDI_SERVICE_LOST);
		atomic_or_32(&vdev->vdev_state, VXGE_ERROR);
	} else {
		/* Flush vdev_state */
		atomic_and_32(&vdev->vdev_state, 0x0);
		atomic_or_32(&vdev->vdev_state, VXGE_STARTED);
	}

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_INITIATE_START EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (0);

exit1:
	(void) vxge_close_vpaths(vdev);
exit0:
	return (-EPERM);
}

static void
vxge_device_disable(vxgedev_t *vdev)
{
	vxge_hal_status_e status;

	vdev->is_initialized = 0;
	vdev->link_state_update = LINK_STATE_UNKNOWN;
	vdev->link_state = LINK_STATE_DOWN;
	((vxge_hal_device_t *)vdev->devh)->link_state = VXGE_HAL_LINK_NONE;

	status = vxge_hal_device_disable(vdev->devh);
	if (status != VXGE_HAL_OK) {
		u64 adapter_status;
		(void) vxge_hal_device_status(vdev->devh, &adapter_status);
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: can not safely disable the device.  \
		    Adaper status 0x%"PRIx64" returned status %d",
		    VXGE_IFNAME, vdev->instance,
		    (uint64_t)adapter_status, status);
			ddi_fm_service_impact(vdev->dev_info,
			    DDI_SERVICE_DEGRADED);
	}
	vxge_debug_init(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: Device disabled\n",
	    VXGE_IFNAME, vdev->instance);

	/* disable device interrupts */
	vxge_hal_device_intr_disable(vdev->devh);

	vxge_debug_init(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: Interrupts disabled\n",
	    VXGE_IFNAME, vdev->instance);

	vxge_debug_driver(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: Waiting for device irq to become quiescent",
	    VXGE_IFNAME, vdev->instance);

	vxge_disable_intrs(vdev);

	vxge_debug_init(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: Disabled interrupts\n", VXGE_IFNAME,
	    vdev->instance);

	/* set link state */
	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_ENTER SOFT_LOCK",
	    VXGE_IFNAME, vdev->instance);

	mutex_enter(&vdev->soft_lock);

	if (!vdev->soft_running) {
		vdev->soft_running = 1;
		(void) ddi_intr_trigger_softint(vdev->soft_hdl, vdev);
	}

	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_EXIT SOFT_LOCK",
	    VXGE_IFNAME, vdev->instance);

	mutex_exit(&vdev->soft_lock);

	/* store mac/multicast addresses */
	(void) vxge_store_mac_addr(vdev);
}

static int
vxge_initiate_stop(vxgedev_t *vdev)
{
	int status, i;
	vxge_ring_t *ring = NULL;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_INITIATE_STOP ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	vxge_device_disable(vdev);

	vxge_tx_drain(vdev);

	/* reset vpaths */
	(void) vxge_reset_vpaths(vdev);

	status = vxge_close_vpaths(vdev);

	/*
	 * Wait for all buffers pools to be destroyed.
	 * Otherwise subsequent initiate_start crashes.
	 */
	if ((status != DDI_SUCCESS) && !vdev->in_reset) {
		for (i = 0; i < vdev->no_of_vpath; ) {
			ring = (vxge_ring_t *)&(vdev->vpaths[i].ring);
			if (ring->bf_pool.live & VXGE_POOL_LIVE) {
				vxge_os_udelay(10);
			} else
				i++;
		}
	}

	/*
	 * Destroy buffer pool mutex at last.
	 */
	for (i = 0; i < vdev->no_of_vpath; i++) {
		ring = (vxge_ring_t *)&(vdev->vpaths[i].ring);

		if (!(ring->bf_pool.live & VXGE_POOL_LIVE) &&
		    !(ring->bf_pool.live & VXGE_POOL_DESTROY))
			vxge_vpath_rx_buff_mutex_destroy(ring);
	}

	vxge_debug_init(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: Vpaths closed\n", VXGE_IFNAME, vdev->instance);

	vxge_kstat_destroy(vdev);
	atomic_and_32(&vdev->vdev_state, ~VXGE_STARTED);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_INITIATE_STOP EXIT ",
	    VXGE_IFNAME, vdev->instance);

	return (status);
}

static void vxge_reset_stats(vxgedev_t *vdev)
{
	int i;
	vxge_hal_vpath_h channelh;
	vxge_hal_status_e status;
	vxge_hal_device_t *hldev = (vxge_hal_device_t *)vdev->devh;
	u64 vpaths_open, vpaths_open_fail, link_up, link_down;

	/* Store */
	vpaths_open	  = vdev->stats.vpaths_open;
	vpaths_open_fail = vdev->stats.vpath_open_fail;
	link_up		  = vdev->stats.link_up;
	link_down		= vdev->stats.link_down;

	/* reset driver stats */
	(void) memset((void *)&vdev->stats, 0,
	    sizeof (struct vxge_sw_stats_t));

	/* Restore */
	vdev->stats.vpaths_open	  = vpaths_open;
	vdev->stats.vpath_open_fail  = vpaths_open_fail;
	vdev->stats.link_up		  = link_up;
	vdev->stats.link_down		= link_down;

	/* clear VPATH stats */
	for (i = 0; i < VXGE_HAL_MAX_VIRTUAL_PATHS; i++) {
		channelh = vxge_get_vpath(vdev, i);
		if (!channelh)
			continue;
		status = vxge_hal_vpath_stats_clear(channelh);
		if (status == VXGE_HAL_ERR_PRIVILAGED_OPEARATION) {
			/*EMPTY*/
			vxge_debug_driver(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
			    "%s%d: vxge_hal_vpath_stats_clear returns"\
			    "VXGE_HAL_ERR_PRIVILAGED_OPEARATION \n",
			    VXGE_IFNAME, vdev->instance);
		}
	}

	/* clear MRPCIM stats */
	status = vxge_hal_mrpcim_stats_clear(hldev);
	if (status == VXGE_HAL_ERR_PRIVILAGED_OPEARATION) {
		/*EMPTY*/
		vxge_debug_driver(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
		    "%s%d: vxge_hal_vpath_stats_clear returns"\
		    "VXGE_HAL_ERR_PRIVILAGED_OPEARATION \n",
		    VXGE_IFNAME, vdev->instance);
	}
}
static void vxge_kstat_init(vxgedev_t *vdev)
{
	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_KSTAT_INIT ENTER ", VXGE_IFNAME, vdev->instance);

	vxge_kstat_vpath_init(vdev);
	vxge_kstat_driver_init(vdev);
	vxge_kstat_port_init(vdev);
	vxge_kstat_aggr_init(vdev);
	vxge_kstat_mrpcim_init(vdev);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_KSTAT_INIT EXIT ", VXGE_IFNAME, vdev->instance);

}

static void vxge_kstat_destroy(vxgedev_t *vdev)
{
	vxge_kstat_vpath_destroy(vdev);
	vxge_kstat_driver_destroy(vdev);
	vxge_kstat_port_destroy(vdev);
	vxge_kstat_aggr_destroy(vdev);
	vxge_kstat_mrpcim_destroy(vdev);
}


/*
 * vxge_m_start
 * @arg: pointer to device private strucutre(hldev)
 *
 * This function is called by MAC Layer to enable the XFRAME
 * firmware to generate interrupts and also prepare the
 * driver to call mac_rx for delivering receive packets
 * to MAC Layer.
 */
static int
vxge_m_start(void *arg)
{
	vxgedev_t *vdev = arg;
	vxge_hal_device_t *hldev = vdev->devh;
	int ret;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_M_START ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_ENTER GENLOCK",
	    VXGE_IFNAME, vdev->instance);
	mutex_enter(&vdev->genlock);

	if (vdev->is_initialized) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: device is already initialized",
		    VXGE_IFNAME, vdev->instance);
		vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
		    "%s%d: MUTEX_EXIT GENLOCK",
		    VXGE_IFNAME, vdev->instance);
		mutex_exit(&vdev->genlock);
		return (EINVAL);
	}

	hldev->terminating = 0;
	ret = vxge_initiate_start(vdev);
	if (ret) {
		mutex_exit(&vdev->genlock);
		vxge_os_printf(
		    "%s%d: vxge_initiate_start failed ret = %d\n",
		    VXGE_IFNAME, vdev->instance, ret);
		/* return (ret); */
		return (EINVAL);
	}
	vxge_debug_init(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: vxge_initiate_start done\n",
	    VXGE_IFNAME, vdev->instance);

	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_EXIT GENLOCK",
	    VXGE_IFNAME, vdev->instance);

	mutex_exit(&vdev->genlock);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_M_START EXIT ", VXGE_IFNAME, vdev->instance);
	return (0);
}

/*
 * vxge_m_stop
 * @arg: pointer to device private data (vdev)
 *
 * This function is called by the MAC Layer to disable
 * the XFRAME firmware for generating any interrupts and
 * also stop the driver from calling mac_rx() for
 * delivering data packets to the MAC Layer.
 */
static void
vxge_m_stop(void *arg)
{
	vxgedev_t *vdev = arg;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_M_STOP ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_ENTER GENLOCK",
	    VXGE_IFNAME, vdev->instance);
	mutex_enter(&vdev->genlock);
	if (!vdev->is_initialized) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: device is not initialized",
		    VXGE_IFNAME, vdev->instance);

		vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
		    "%s%d: MUTEX_EXIT GENLOCK",
		    VXGE_IFNAME, vdev->instance);
		mutex_exit(&vdev->genlock);
		return;
	}

	(void) vxge_initiate_stop(vdev);
	vxge_debug_init(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: vxge_initiate_stop done\n",
	    VXGE_IFNAME, vdev->instance);

	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_EXIT GENLOCK",
	    VXGE_IFNAME, vdev->instance);

	mutex_exit(&vdev->genlock);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_M_STOP EXIT ",
	    VXGE_IFNAME, vdev->instance);
}

/*
 * vxge_onerr_reset
 * @vdev: pointer to vxgedev_t structure
 *
 * This function is called by HAL Event framework to reset the HW
 * This function is must be called with genlock taken.
 */
static void
vxge_onerr_reset(vxgedev_t *vdev)
{
	vxge_hal_status_e status;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_ONERR_RESET ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	mutex_enter(&vdev->genlock);

	if (!vdev->is_initialized) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: can not reset", VXGE_IFNAME, vdev->instance);
		vxge_debug_driver(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
		    "%s%d: MUTEX_EXIT GENLOCK",
		    VXGE_IFNAME, vdev->instance);
		mutex_exit(&vdev->genlock);
		return;
	}

	vdev->in_reset = 1;

	(void) vxge_initiate_stop(vdev);

	switch (vdev->cric_err_event.type) {
	case VXGE_HAL_EVENT_ECCERR:
	case VXGE_HAL_EVENT_KDFCCTL:
	case VXGE_HAL_EVENT_CRITICAL:
		/* reset device */
		if (vxge_hal_device_reset(vdev->devh) == VXGE_HAL_OK)
			(void) vxge_hal_device_reset_poll(vdev->devh);
		vxge_debug_init(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
		    "%s%d: Reset device done\n",
		    VXGE_IFNAME, vdev->instance);
		break;
	case VXGE_HAL_EVENT_SRPCIM_CRITICAL:
	case VXGE_HAL_EVENT_MRPCIM_CRITICAL:
	case VXGE_HAL_EVENT_MRPCIM_ECCERR:
	case VXGE_HAL_EVENT_UNKNOWN:
		status = vxge_hal_mrpcim_reset(vdev->devh);
		switch (status) {
		case VXGE_HAL_PENDING:
			(void) vxge_hal_mrpcim_reset_poll(vdev->devh);
			break;
		case VXGE_HAL_ERR_PRIVILAGED_OPEARATION:
			/* Non function 0 */
			(void) vxge_hal_device_mrpcim_reset_poll(vdev->devh);
			break;
		default:
			break;
		}
		break;
	case VXGE_HAL_EVENT_SLOT_FREEZE:
	case VXGE_HAL_EVENT_SERR:
		break;
	default:
		break;
	}

	(void) vxge_initiate_start(vdev);
out:
	vdev->in_reset = 0;

	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_EXIT GENLOCK", VXGE_IFNAME, vdev->instance);

	mutex_exit(&vdev->genlock);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_ONERR_RESET EXIT ",
	    VXGE_IFNAME, vdev->instance);
}

/*
 * vxge_m_unicst
 * @arg: pointer to device private strucutre(hldev)
 * @mac_addr:
 *
 * This function is called by MAC Layer to set the physical address
 * of the XFRAME firmware.
 */
static int
vxge_m_unicst(void *arg, const uint8_t *macaddr)
{
	vxge_hal_status_e status;
	vxgedev_t *vdev = (vxgedev_t *)arg;
	/* vxge_hal_device_t *hldev = vdev->devh; */
	int i;
	macaddr_t mc_mask = {0};
	vxge_mac_info_t  mac;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_M_UNICST ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_ENTER GENLOCK",
	    VXGE_IFNAME, vdev->instance);
	mutex_enter(&vdev->genlock);

	vxge_debug_driver(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: setting macaddr: " \
	    "0x%02x-%02x-%02x-%02x-%02x-%02x", VXGE_IFNAME,
	    vdev->instance, macaddr[0], macaddr[1], macaddr[2],
	    macaddr[3], macaddr[4], macaddr[5]);

	for (i = 0; i < VXGE_HAL_MAX_VIRTUAL_PATHS; i++) {
		vxge_hal_vpath_h channelh = vxge_get_vpath(vdev, i);
		if (!channelh)
			continue;
		(void *) memcpy(mac.macaddr, (uchar_t *)macaddr,
		    VXGE_HAL_ETH_ALEN);
		(void *) memcpy(mac.macmask, (uchar_t *)mc_mask,
		    VXGE_HAL_ETH_ALEN);
		mac.vpath_no = (u16)i;
		status = vxge_add_mac_addr(vdev, &mac);
		if (status == VXGE_HAL_FAIL) {
			vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
			    "%s%d: can not set mac address",
			    VXGE_IFNAME, vdev->instance);

			vxge_debug_driver(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
			    "%s%d: MUTEX_EXIT GENLOCK", VXGE_IFNAME,
			    vdev->instance);

			mutex_exit(&vdev->genlock);
			return (EIO);
		}
	}

	vxge_debug_driver(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_EXIT GENLOCK", VXGE_IFNAME, vdev->instance);

	mutex_exit(&vdev->genlock);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_M_UNICST EXIT ", VXGE_IFNAME, vdev->instance);
	return (0);
}


/*
 * vxge_m_multicst
 * @arg: pointer to device private strucutre(vdev)
 * @add:
 * @mc_addr:
 *
 * This function is called by MAC Layer to enable or
 * disable device-level reception of specific multicast addresses.
 */
static int
vxge_m_multicst(void *arg, boolean_t add, const uint8_t *mc_addr)
{
	vxge_hal_status_e status = VXGE_HAL_OK;
	vxgedev_t *vdev = (vxgedev_t *)arg;
	u16 i;
	macaddr_t mc_mask = {0};
	vxge_mac_info_t  mac;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_M_MULTICST ENTRY ",
	    VXGE_IFNAME, vdev->instance);
	vxge_debug_init(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: M_MULTICAST add %d",
	    VXGE_IFNAME, vdev->instance, add);

	/*
	 * link_up context is handled with softirq.
	 * Taking genlock to handle multiple multicast request.
	 */

	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_ENTER GENLOCK",
	    VXGE_IFNAME, vdev->instance);

	mutex_enter(&vdev->genlock);

	if (!vdev->is_initialized) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: can not set multicast",
		    VXGE_IFNAME, vdev->instance);

		vxge_debug_driver(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
		    "%s%d: MUTEX_EXIT GENLOCK",
		    VXGE_IFNAME, vdev->instance);

		mutex_exit(&vdev->genlock);
		return (EIO);
	}

	vxge_debug_init(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: multicast macaddr: " \
	    "0x%02x-%02x-%02x-%02x-%02x-%02x", VXGE_IFNAME,
	    vdev->instance, mc_addr[0], mc_addr[1], mc_addr[2],
	    mc_addr[3], mc_addr[4], mc_addr[5]);

	for (i = 0; i < VXGE_HAL_MAX_VIRTUAL_PATHS; i++) {
		vxge_hal_vpath_h channelh = vxge_get_vpath(vdev, i);
		if (!channelh)
			continue;
		(void) memcpy(mac.macaddr, (uchar_t *)mc_addr,
		    VXGE_HAL_ETH_ALEN);
		(void) memcpy(mac.macmask, (uchar_t *)mc_mask,
		    VXGE_HAL_ETH_ALEN);
		mac.vpath_no = i;
		status = (add) ?
		    vxge_add_mac_addr(vdev, &mac) :
		    vxge_delete_mac_addr(vdev, &mac);
		if (status != VXGE_HAL_OK)
			break;
	}

	if (status == VXGE_HAL_FAIL) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: failed to %s multicast, status %d",
		    VXGE_IFNAME, vdev->instance,
		    add ? "add" : "delete", status);
		vxge_debug_driver(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
		    "%s%d: MUTEX_EXIT GENLOCK",
		    VXGE_IFNAME, vdev->instance);
		mutex_exit(&vdev->genlock);
		return (EIO);
	}

	vxge_debug_driver(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_EXIT GENLOCK",
	    VXGE_IFNAME, vdev->instance);
	mutex_exit(&vdev->genlock);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_M_MULTICST EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (0);
}


/*
 * vxge_m_promisc
 * @arg: pointer to device private strucutre(vdev)
 * @on:
 *
 * This function is called by MAC Layer to enable or
 * disable the reception of all the packets on the medium
 */
static int
vxge_m_promisc(void *arg, boolean_t on)
{
	vxgedev_t *vdev = (vxgedev_t *)arg;
	int i;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_M_PROMISC ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_ENTER GENLOCK",
	    VXGE_IFNAME, vdev->instance);
	mutex_enter(&vdev->genlock);

	if (!vdev->is_initialized) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: can not set promiscuous",
		    VXGE_IFNAME, vdev->instance);

		vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
		    "%s%d: MUTEX_EXIT, GENLOCK",
		    VXGE_IFNAME, vdev->instance);
		mutex_exit(&vdev->genlock);
		return (EIO);
	}

	for (i = 0; i < VXGE_HAL_MAX_VIRTUAL_PATHS; i++) {
		vxge_hal_vpath_h channelh = vxge_get_vpath(vdev, i);
		struct vxge_vpath *vpath;
		if (!channelh)
			continue;
		vpath = &vdev->vpaths[i];
		if (on) {
			(void) vxge_hal_vpath_promisc_enable(channelh);
			vpath->promiscuous_mode = VXGE_PROMISC_MODE_ENABLE;
		} else {
			(void) vxge_hal_vpath_promisc_disable(channelh);
			vpath->promiscuous_mode = VXGE_PROMISC_MODE_DISABLE;
		}
	}

	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_EXIT GENLOCK", VXGE_IFNAME, vdev->instance);
	mutex_exit(&vdev->genlock);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_M_PROMISC EXIT ", VXGE_IFNAME, vdev->instance);
	return (0);
}

/*
 * vxge_m_stat
 * @arg: pointer to device private strucutre(vdev)
 *
 * This function is called by MAC Layer to get network statistics
 * from the driver.
 */
static int
vxge_m_stat(void *arg, uint_t stat, uint64_t *val)
{
	int i;
	vxgedev_t *vdev = (vxgedev_t *)arg;
	vxge_hal_vpath_stats_hw_info_t hw_info;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_M_STAT ENTRY", VXGE_IFNAME, vdev->instance);

	*val = 0;

	switch (stat) {

	case MAC_STAT_IFSPEED:
		*val = 10000000000; /* 10G */
		return (0);

	case ETHER_STAT_LINK_DUPLEX:
		*val = LINK_DUPLEX_FULL;
		return (0);

	default:
		break;
	}

	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_TRYENTER GENLOCK", VXGE_IFNAME, vdev->instance);
	if (!mutex_tryenter(&vdev->genlock))
		return (EAGAIN);

	if (!vdev->is_initialized) {
		mutex_exit(&vdev->genlock);
		return (ECANCELED);
	}
	for (i = 0; i < vdev->no_of_vpath; i++) {
		if (vxge_hal_vpath_hw_stats_enable(vdev->vpaths[i].handle)
		    != VXGE_HAL_OK) {
			vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
			    "%s%d: vxge_hal_vpath_hw_stats_enable failed \n",
			    VXGE_IFNAME, vdev->instance);
			mutex_exit(&vdev->genlock);
			return (ECANCELED);
		}

		if (vxge_hal_vpath_hw_stats_get(vdev->vpaths[i].handle,
		    &hw_info) != VXGE_HAL_OK) {
			vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
			    "%s%d: %s", VXGE_IFNAME, vdev->instance,
			    "Getting Hardware Stats failed \n");
			mutex_exit(&vdev->genlock);
			return (ECANCELED);
		}

		switch (stat) {

		case MAC_STAT_MULTIRCV:
			*val += hw_info.rx_stats.rx_vld_mcast_frms;
			break;

		case MAC_STAT_BRDCSTRCV:
			*val += hw_info.rx_stats.rx_vld_bcast_frms;
			break;

		case MAC_STAT_MULTIXMT:
			*val += hw_info.tx_stats.tx_mcast_frms;
			break;

		case MAC_STAT_BRDCSTXMT:
			*val += hw_info.tx_stats.tx_bcast_frms;
			break;

		case MAC_STAT_RBYTES:
			*val += hw_info.rx_stats.rx_ttl_eth_octets;
			break;

		case MAC_STAT_NORCVBUF:
			*val += hw_info.rx_stats.rx_lost_frms;
			break;

		case MAC_STAT_IERRORS:
			*val += hw_info.rx_stats.rx_various_discard;
			break;

		case MAC_STAT_OBYTES:
			*val += hw_info.tx_stats.tx_ttl_eth_octets;
			break;

		case MAC_STAT_NOXMTBUF:
			*val += hw_info.tx_stats.tx_lost_ip;
			break;

		case MAC_STAT_OERRORS:
			*val += hw_info.tx_stats.tx_parse_error;
			break;

		case MAC_STAT_IPACKETS:
			*val += hw_info.rx_stats.rx_ttl_eth_frms;
			break;

		case MAC_STAT_OPACKETS:
			*val += hw_info.tx_stats.tx_ttl_eth_frms;
			break;

		case ETHER_STAT_TOOLONG_ERRORS:
			*val += hw_info.rx_stats.rx_long_frms;
			break;

		default:
			mutex_exit(&vdev->genlock);
			return (ENOTSUP);
		}
	}

	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_EXIT GENLOCK", VXGE_IFNAME, vdev->instance);
	mutex_exit(&vdev->genlock);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_M_STAT EXIT ", VXGE_IFNAME, vdev->instance);
	return (0);
}

/*
 * vxge_device_alloc - Allocate new LL device
 */
int
vxge_device_alloc(dev_info_t *dev_info, vxge_config_t *config,
	vxgedev_t **vdev_out)
{
	vxgedev_t *vdev;
	int instance = ddi_get_instance(dev_info);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s: VXGE_DEVICE_ALLOC ENTRY ", VXGE_IFNAME);
	*vdev_out = NULL;

	vxge_debug_driver(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s: Trying to register Ethernet device", VXGE_IFNAME);

	vdev = kmem_zalloc(sizeof (vxgedev_t), KM_NOSLEEP);

	if (vdev == NULL) {
		VXGE_STATS_DRV_INC(kmem_zalloc_fail);
		return (ENOSPC);
	}

	vdev->instance = instance;
	vdev->dev_info = dev_info;

	bcopy(config, &vdev->config, sizeof (vxge_config_t));
	*vdev_out = vdev;

	vxge_debug_init(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "s%d: Allocated a new device instance\n",
	    VXGE_IFNAME, vdev->instance);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_DEVICE_ALLOC EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (DDI_SUCCESS);
}

/*
 * vxge_device_free
 */
void
vxge_device_free(vxgedev_t *vdev)
{

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_DEVICE_FREE ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	kmem_free(vdev, sizeof (vxgedev_t));

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_DEVICE_FREE EXIT ",
	    VXGE_IFNAME, vdev->instance);
}

/*
 * vxge_total_dev
 */
void
vxge_free_total_dev()
{
	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s: VXGE_FREE_TOTAL_DEV ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	if (total_vxge.cur_devs == 0) {
		kmem_free((void *)total_vxge.vxge_dev,
		    (total_vxge.max_devs * sizeof (vxge_dev_t)));
		total_vxge.max_devs = 0;
	}

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s: VXGE_FREE_TOTAL_DEV EXIT ",
	    VXGE_IFNAME, vdev->instance);
}

/* helper function to print mac address */
/*ARGSUSED*/
static void
vxge_print_mac_addr(vxgedev_t *vdev, u8 *macaddr)
{
	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s: VXGE_PRINT_MAC_ADDR ENTRY ",
	    VXGE_IFNAME);

	vxge_debug_driver(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "MAC ADDR=0x%02x:0x%02x:0x%02x:0x%02x:0x%02x:0x%02x\n",
	    (u8)macaddr[0], (u8)macaddr[1], (u8)macaddr[2],
	    (u8)macaddr[3], (u8)macaddr[4], (u8)macaddr[5]);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s: VXGE_PRINT_MAC_ADDR EXIT ", VXGE_IFNAME);
}

/* restore all mac addresses from DA table */
static vxge_hal_status_e
vxge_restore_mac_addr(vxgedev_t *vdev)
{
	vxge_hal_status_e status = VXGE_HAL_OK;
	struct vxge_vpath *vpath;
	u16 i, j, temp_cnt = 0;
	vxge_mac_info_t mac;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_RESTORE_MAC_ADDR ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	for (i = 0; i < VXGE_HAL_MAX_VIRTUAL_PATHS; i++) {
		vxge_hal_vpath_h channelh = vxge_get_vpath(vdev, i);
		if (!channelh)
			continue;
		vpath = &vdev->vpaths[i];

		/*
		 * store vpath->mac_addr_cnt and initialize to 0,
		 * it is increamented in add_mac_addr
		 */
		temp_cnt = vpath->mac_addr_cnt;
		vpath->mac_addr_cnt = 0;
		for (j = 0; j < temp_cnt; j++) {
			(void) memcpy(mac.macaddr, vpath->mac_list[j].macaddr,
			    VXGE_HAL_ETH_ALEN);
			(void) memcpy(mac.macmask, vpath->mac_list[j].macmask,
			    VXGE_HAL_ETH_ALEN);
			mac.vpath_no = i;

			/* don't restore station mac addr */
			status = vxge_add_mac_addr(vdev, &mac);
			if (status == VXGE_HAL_FAIL) {
				vxge_debug_driver(VXGE_ERR, NULL_HLDEV,
				    NULL_VPID,
				    "%s%d: can not set mac address",
				    VXGE_IFNAME, vdev->instance);
				vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV,
				    NULL_VPID,
				    "%s%d: VXGE_RESTIORE_MAC_ADDR EXIT ",
				    VXGE_IFNAME, vdev->instance);
				return (status);
			}
		}
	}
	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_RESTORE_MAC_ADDR EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (status);
}

/* store all mac addresses from DA table */
static vxge_hal_status_e
vxge_store_mac_addr(vxgedev_t *vdev)
{
	vxge_hal_status_e status = VXGE_HAL_OK;
	struct vxge_vpath *vpath;
	u8 macaddr[ETHERADDRL], macmask[ETHERADDRL];
	int i;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_STORE_MAC_ADDR ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	/* get first entry */
	for (i = 0; i < VXGE_HAL_MAX_VIRTUAL_PATHS; i++) {
		vxge_hal_vpath_h channelh = vxge_get_vpath(vdev, i);
		if (!channelh)
			continue;
		vpath = &vdev->vpaths[i];
		vpath->mac_addr_cnt = 0;

		/* read first mac entry, don't store station mac addr */
		status =
		    vxge_hal_vpath_mac_addr_get(channelh, macaddr, macmask);
		if (status != VXGE_HAL_OK)
			return (status);
		(void) memcpy(vpath->mac_list[vpath->mac_addr_cnt].macaddr,
		    macaddr, VXGE_HAL_ETH_ALEN);
		(void) memcpy(vpath->mac_list[vpath->mac_addr_cnt].macmask,
		    macmask, VXGE_HAL_ETH_ALEN);
		vpath->mac_addr_cnt++;

		/* get next entries */
		while (status == VXGE_HAL_OK) {
			status = vxge_hal_vpath_mac_addr_get_next(channelh,
			    macaddr, macmask);
			if (status != VXGE_HAL_OK)
				break;

			(void) memcpy(
			    vpath->mac_list[vpath->mac_addr_cnt].macaddr,
			    macaddr, VXGE_HAL_ETH_ALEN);
			(void) memcpy(
			    vpath->mac_list[vpath->mac_addr_cnt].macmask,
			    macmask, VXGE_HAL_ETH_ALEN);
			vpath->mac_addr_cnt++;
		}
	}
	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_STORE_MAC_ADDR EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (VXGE_HAL_OK);
}


/* list all mac addresses from DA table */
static vxge_hal_status_e
vxge_list_mac_addr(vxgedev_t *vdev, mblk_t *mp)
{
	vxge_hal_status_e status = VXGE_HAL_OK;
	u8 macaddr[ETHERADDRL], macmask[ETHERADDRL];
	vxge_hal_vpath_h channelh;
	u16 i = 0;
	mblk_t *mp1;
	macList_t  *macL;
	mp1 = mp->b_cont;
	macL = (void *)(mp1->b_rptr);

	u16  vpath_no = macL->macL[0].vpath_no;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_LIST_MAC_ADDR ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	channelh = vxge_get_vpath(vdev, vpath_no);

	if (!channelh) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: vpath:%d not configured\n",
		    VXGE_IFNAME, vdev->instance, vpath_no);
		return (VXGE_HAL_FAIL);
	}

	vxge_debug_driver(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MAC address List\n",
	    VXGE_IFNAME, vdev->instance);

	/* get first entry */
	status = vxge_hal_vpath_mac_addr_get(channelh, macaddr, macmask);
	if (status != VXGE_HAL_OK)
		return (status);

	(void) memcpy(macL->macL[i].macaddr, macaddr, VXGE_HAL_ETH_ALEN);
	(void) memcpy(macL->macL[i].macmask, macmask, VXGE_HAL_ETH_ALEN);


	vxge_print_mac_addr(vdev, macaddr);

	/* get next entries */
	while (status == VXGE_HAL_OK) {
		/* get next entries */
		status = vxge_hal_vpath_mac_addr_get_next(channelh, macaddr,
		    macmask);
		if (status != VXGE_HAL_OK)
			return (status);
		i++;
		(void) memcpy(macL->macL[i].macaddr, macaddr,
		    VXGE_HAL_ETH_ALEN);
		(void) memcpy(macL->macL[i].macmask, macmask,
		    VXGE_HAL_ETH_ALEN);
		macL->mac_addr_cnt = i;
		vxge_print_mac_addr(vdev, macaddr);
	}
	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_LIST_MAC_ADDR EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (VXGE_HAL_OK);
}

/* compare mac addresses */
static u8
vxge_compare_ether_addr(const u8 *addr1, const u8 *addr2)
{
	const u16 *a = (void *) addr1;
	const u16 *b = (void *) addr2;
	return (((a[0] ^ b[0]) | (a[1] ^ b[1]) | (a[2] ^ b[2])) != 0);
}

/* search mac address in DA table */
static vxge_hal_status_e
vxge_search_mac_addr_in_da_table(vxgedev_t *vdev,
	vxge_mac_info_t *mac)
{
	vxge_hal_status_e status = VXGE_HAL_OK;
	u8 macaddr[ETHERADDRL], macmask[ETHERADDRL];
	vxge_hal_vpath_h channelh;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_SEARCH_MAC_ADDR ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	channelh = vxge_get_vpath(vdev, mac->vpath_no);

	/* get first entry */
	status = vxge_hal_vpath_mac_addr_get(channelh, macaddr, macmask);
	if (status != VXGE_HAL_OK)
		return (status);

	/* check if already present in DA table */
	if (!vxge_compare_ether_addr(mac->macaddr, macaddr))
		return (status);

	/* get next entries */
	while (status == VXGE_HAL_OK) {
		/* get next entries */
		status = vxge_hal_vpath_mac_addr_get_next(channelh, macaddr,
		    macmask);
		if (status != VXGE_HAL_OK)
			return (status);

		/* check if already present in DA table */
		if (!vxge_compare_ether_addr(mac->macaddr, macaddr))
			return (status);

	}
	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_SEARCH_MAC_ADDR EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (VXGE_HAL_OK);

}

/* delete mac address to DA table */
static vxge_hal_status_e
vxge_delete_mac_addr(vxgedev_t *vdev, vxge_mac_info_t *mac)
{
	vxge_hal_status_e status = VXGE_HAL_OK;
	vxge_vpath_t *vpath;
	vxge_hal_vpath_h channelh;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_DELETE_MAC_ADDR ENTRY ",
	    VXGE_IFNAME, vdev->instance);
	vpath = &vdev->vpaths[mac->vpath_no];

	/* delete mac address */
	channelh = vxge_get_vpath(vdev, mac->vpath_no);
	status = vxge_hal_vpath_mac_addr_delete(channelh, mac->macaddr,
	    mac->macmask);
	if (status != VXGE_HAL_OK) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: DA config delete entry failed for vpath:%d",
		    VXGE_IFNAME, vdev->instance, vpath->id);
		return (status);
	}

	if (vpath->mac_addr_cnt > 0)
		vpath->mac_addr_cnt--;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_DELETE_MAC_ADDR EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (status);
}

/* add mac address to DA table */
static vxge_hal_status_e
vxge_add_mac_addr(vxgedev_t *vdev, vxge_mac_info_t *mac)
{
	vxge_hal_status_e status = VXGE_HAL_OK;
	vxge_vpath_t *vpath;
	vxge_hal_vpath_h channelh;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_ADD_MAC_ADDR ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	vpath = &vdev->vpaths[mac->vpath_no];
	channelh = vxge_get_vpath(vdev, mac->vpath_no);

	if (!channelh) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: vpath:%d not configured\n",
		    VXGE_IFNAME, vdev->instance, mac->vpath_no);
		return (VXGE_HAL_FAIL);
	}

	if (!vpath->is_open)
		return (VXGE_HAL_FAIL);

	/* check for boundary condition */
	if (vpath->mac_addr_cnt >= VXGE_MAX_MAC_ENTRIES) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: add_mac_addr:No space in DA table for vpath:%d",
		    VXGE_IFNAME, vdev->instance, vpath->id);
		return (VXGE_HAL_FAIL);
	}

	/* check if already present in DA table */
	if ((vxge_search_mac_addr_in_da_table(vdev, mac) == VXGE_HAL_OK)) {
		vxge_debug_init(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
		    "%s%d: This MAC addr is present in DA table for vpath:%d",
		    VXGE_IFNAME, vdev->instance, vpath->id);
		return (VXGE_MAC_PRESENT);
	}

	/* add mac address */
	channelh = vxge_get_vpath(vdev, mac->vpath_no);

	vxge_print_mac_addr(vdev, mac->macaddr);

	status = vxge_hal_vpath_mac_addr_add(channelh, mac->macaddr,
	    mac->macmask, VXGE_HAL_VPATH_MAC_ADDR_ADD_DUPLICATE);
	if (status != VXGE_HAL_OK) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: DA config add entry failed for vpath:%d",
		    VXGE_IFNAME, vdev->instance, vpath->id);
		return (status);
	}

	vpath->mac_addr_cnt++;
	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_ADD_MAC_ADDR EXIT ", VXGE_IFNAME, vdev->instance);
	return (status);
}


/*
 * vxge_ioctl
 */
static void
vxge_m_ioctl(void *arg, queue_t *wq, mblk_t *mp)
{
	vxgedev_t *vdev = arg;
	struct iocblk *iocp;
	int err = 0;
	int cmd;
	int need_privilege = 1;
	int ret = 0;
	vxge_hal_status_e status = VXGE_HAL_OK;
	v_ioctl_t *p_ioctl;
	u64 *ptr, i = 0, size, val64, offset = 0;
	u16 *ptr_16;
	mblk_t *mp1;
	vxge_mac_info_t *mac;
	u16 event_type, count;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_M_IOCTL ENTRY ", VXGE_IFNAME, vdev->instance);

	iocp = (void *)mp->b_rptr;
	iocp->ioc_error = 0;
	cmd = iocp->ioc_cmd;

	switch (cmd) {

	case VXGE_REGISTER_GET:
	case VXGE_REGISTER_SET:
	case VXGE_REGISTER_BLOCK_GET:
	case VXGE_MAC_ADD:
	case VXGE_MAC_DEL:
	case VXGE_MAC_LIST:
	case VXGE_PROMISCUOUS_ENABLE:
	case VXGE_PROMISCUOUS_DISABLE:
	case VXGE_FLICK_LED:
	case VXGE_RESET:
		need_privilege = 0;
		break;
	default:
		miocnak(wq, mp, 0, EINVAL);
		return;
	}

	if (need_privilege) {
		err = secpolicy_net_config(iocp->ioc_cr, B_FALSE);
		if (err != 0) {
			vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
			    "%s%d: drv_priv(): rejected cmd 0x%x, err %d",
			    VXGE_IFNAME, vdev->instance, cmd, err);
			miocnak(wq, mp, 0, err);
			return;
		}
	}

	switch (cmd) {

	case VXGE_RESET:
		ret = B_TRUE;
		ptr_16 = (void *)((mp->b_cont)->b_rptr);
		event_type = *ptr_16;
		vxge_callback_crit_err(vdev->devh, vdev, event_type, 0);
		break;

	case VXGE_PROMISCUOUS_ENABLE:
		ret = B_TRUE;
		for (i = 0; i < VXGE_HAL_MAX_VIRTUAL_PATHS; i++) {
			vxge_hal_vpath_h channelh = vxge_get_vpath(vdev, i);
			struct vxge_vpath *vpath;
			if (!channelh)
				continue;
			vpath = &vdev->vpaths[i];
			(void) vxge_hal_vpath_promisc_enable(channelh);
			vpath->promiscuous_mode = VXGE_PROMISC_MODE_ENABLE;
		}
		break;

	case VXGE_PROMISCUOUS_DISABLE:
		ret = B_TRUE;
		for (i = 0; i < VXGE_HAL_MAX_VIRTUAL_PATHS; i++) {
			vxge_hal_vpath_h channelh = vxge_get_vpath(vdev, i);
			struct vxge_vpath *vpath;
			if (!channelh)
				continue;
			vpath = &vdev->vpaths[i];
			(void) vxge_hal_vpath_promisc_disable(channelh);
			vpath->promiscuous_mode = VXGE_PROMISC_MODE_DISABLE;
		}

		break;

	case VXGE_REGISTER_GET:
		p_ioctl = (void *)((mp->b_cont)->b_rptr);

		status = vxge_hal_mgmt_reg_read(vdev->devh, p_ioctl->regtype,
		    p_ioctl->index, p_ioctl->offset, &p_ioctl->value);
		if (status != VXGE_HAL_OK) {
			vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
			    "%s%d: Regster read failed - \
			    Offset : 0x%016llX : \
			    Index : 0x%016llX : Value : 0x%016llX\n",
			    VXGE_IFNAME, vdev->instance,
			    (u64)p_ioctl->offset, (u64)p_ioctl->index,
			    (u64)p_ioctl->value);
			ret = B_FALSE;
			break;
		}
		ret = B_TRUE;
		break;

	case VXGE_REGISTER_SET:
		p_ioctl = (void *)((mp->b_cont)->b_rptr);
		status = vxge_hal_mgmt_reg_write(vdev->devh, p_ioctl->regtype,
		    p_ioctl->index, p_ioctl->offset, p_ioctl->value);
		if (status != VXGE_HAL_OK) {
			vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
			    "%s%d: Register write failed - " \
			    "Offset : 0x%016llX : " \
			    "Index : 0x%016llX : " \
			    "Value : 0x%016llX\n",
			    VXGE_IFNAME, vdev->instance,
			    (u64)p_ioctl->offset,
			    (u64)p_ioctl->index,
			    (u64)p_ioctl->value);
			ret = B_FALSE;
			break;
		}
		ret = B_TRUE;
		break;

	case VXGE_REGISTER_BLOCK_GET:
		ret = B_TRUE;
		mp1 = mp->b_cont;
		p_ioctl = (void *)(mp1->b_rptr);
		ptr = (u64 *)p_ioctl->data;

		/*
		 * The data blk size passed from user includes structure
		 * members. To get data blk size substract structure size
		 * 40 bytes(5)
		 */
		size = (_PTRDIFF(mp1->b_wptr, mp1->b_rptr))/8 - 5;
		vxge_debug_driver(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
		    "%s%d: Register Type = %lld Size of\
		    structure = %d\n",
		    VXGE_IFNAME, vdev->instance, p_ioctl->regtype,
		    iocp->ioc_count);

		/*CONSTCOND*/
		while (1) {
			for (i = 0; i < size; i++) {
				status = vxge_hal_mgmt_reg_read(vdev->devh,
				    p_ioctl->regtype,
				    p_ioctl->index, offset, &val64);
				if (status != VXGE_HAL_OK) {
					vxge_debug_driver(VXGE_ERR,
					    NULL_HLDEV, NULL_VPID,
					    "%s%d: Register read failed - \
					    Offset : 0x%016llX : \
					    Index : 0x%016llX : \
					    Value : 0x%016llX\n",
					    VXGE_IFNAME, vdev->instance,
					    (u64)p_ioctl->index,
					    (u64)val64);
					ret = B_FALSE;
					break;
				}
				ptr[i] = val64;
				vxge_debug_driver(VXGE_TRACE, NULL_HLDEV,
				    NULL_VPID,
				    "%s%d: Register Offset : 0x%016llX : \
				    Value : 0x%016llX :0x%016llX\n",
				    VXGE_IFNAME, vdev->instance,
				    (u64)offset, (u64)ptr[i], val64);
				offset += 8;
			}
			if (status != VXGE_HAL_OK)
				break;
			/*
			 * Each data blk is of 1k user memory, the
			 * next data will be in b_cont
			 */
			mp1 = mp1->b_cont;
			if (mp1 == NULL)
				break;
			ptr = (void *)(mp1->b_rptr);
			size = (_PTRDIFF(mp1->b_wptr, mp1->b_rptr))/8;
		}
		break;

	case VXGE_MAC_ADD:
		mp1 = mp->b_cont;
		mac = (void *)(mp1->b_rptr);
		status = vxge_add_mac_addr(vdev, mac);
		if (status != VXGE_HAL_OK)
			vxge_os_printf("vxge_add_mac_addr - returned %d\n",
			    status);
		ret = B_TRUE;
		break;

	case VXGE_MAC_DEL:
		/* delete mac addr from DA table */
		mp1 = mp->b_cont;
		mac = (void *)(mp1->b_rptr);
		status = vxge_delete_mac_addr(vdev, mac);
		if (status != VXGE_HAL_OK)
			vxge_os_printf("vxge_delete_mac_addr - returned %d\n",
			    status);
		ret = B_TRUE;
		break;

	case VXGE_MAC_LIST:
		(void) vxge_list_mac_addr(vdev, mp);
		ret = B_TRUE;
		break;

	case VXGE_FLICK_LED:
		ptr_16 = (void *)((mp->b_cont)->b_rptr);
		count = *ptr_16;
		status = vxge_hal_device_flick_link_led(vdev->devh, 0, count);
		if (status == VXGE_HAL_OK)
			ret = B_TRUE;
		else
			vxge_os_printf("vxge_hal_device_flick_link_led " \
			    "returned status %d\n", status);
		break;

	default:
		break;
	}

	if (ret == B_FALSE) {
		vxge_debug_driver(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
		    "%s%d: nd_getset(): rejected cmd 0x%x, err %d",
		    VXGE_IFNAME, vdev->instance, cmd, err);
		miocnak(wq, mp, 0, EINVAL);
	} else {
		mp->b_datap->db_type = iocp->ioc_error == 0 ?
		    M_IOCACK : M_IOCNAK;
		qreply(wq, mp);
	}

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_M_IOCTL EXIT ",
	    VXGE_IFNAME, vdev->instance);
}

/* ARGSUSED */
static boolean_t
vxge_m_getcapab(void *arg, mac_capab_t cap, void *cap_data)
{
	vxgedev_t *vdev = arg;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_M_GETCAPAB ENTRY ",
	    VXGE_IFNAME, vdev->instance);
	switch (cap) {
	case MAC_CAPAB_HCKSUM: {
		uint32_t *hcksum_txflags = cap_data;
		*hcksum_txflags = HCKSUM_ENABLE | HCKSUM_INET_FULL_V4 |
		    HCKSUM_IPHDRCKSUM;
		break;
	}
	case MAC_CAPAB_LSO: {
		mac_capab_lso_t *cap_lso = cap_data;

		if (vdev->config.lso_enable) {
			cap_lso->lso_flags = LSO_TX_BASIC_TCP_IPV4;
			cap_lso->lso_basic_tcp_ipv4.lso_max = VXGE_LSO_MAXLEN;
			break;
		} else {
			return (B_FALSE);
		}
	}

	default:
		return (B_FALSE);
	}

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_M_GETCAPAB EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (B_TRUE);
}

/*
 * callback functions for set/get of properties
 */
/*ARGSUSED*/
static int
vxge_m_setprop(void *arg, const char *pr_name, mac_prop_id_t pr_num,
    uint_t pr_valsize, const void *pr_val)
{
	vxgedev_t	*vdev = arg;
	int		err = 0;
	uint32_t	cur_mtu, new_mtu;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_M_SETPROP ENTER ",
	    VXGE_IFNAME, vdev->instance);

	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_ENTER GENLOCK",
	    VXGE_IFNAME, vdev->instance);
	mutex_enter(&vdev->genlock);

	switch (pr_num) {
	case MAC_PROP_DUPLEX:
	case MAC_PROP_SPEED:
	case MAC_PROP_STATUS:
	case MAC_PROP_AUTONEG:
	case MAC_PROP_FLOWCTRL:
	case MAC_PROP_ADV_1000FDX_CAP:
	case MAC_PROP_EN_1000FDX_CAP:
	case MAC_PROP_ADV_100FDX_CAP:
	case MAC_PROP_EN_100FDX_CAP:
	case MAC_PROP_ADV_10FDX_CAP:
	case MAC_PROP_EN_10FDX_CAP:
	case MAC_PROP_EN_1000HDX_CAP:
	case MAC_PROP_EN_100HDX_CAP:
	case MAC_PROP_EN_10HDX_CAP:
	case MAC_PROP_ADV_1000HDX_CAP:
	case MAC_PROP_ADV_100HDX_CAP:
	case MAC_PROP_ADV_10HDX_CAP:
		err = EINVAL;
		break;

	case MAC_PROP_MTU:
		cur_mtu = vdev->mtu;
		bcopy(pr_val, &new_mtu, sizeof (new_mtu));

		if (new_mtu == cur_mtu) {
			err = 0;
			break;
		}

		if (new_mtu < VXGE_DEFAULT_MTU ||
		    new_mtu > VXGE_MAXIMUM_MTU) {
			err = EINVAL;
			break;
		}

		if (vdev->is_initialized) {
			err = EBUSY;
			break;
		}

		vdev->mtu = new_mtu;

		err = mac_maxsdu_update(vdev->mh, new_mtu);
		if (err) {
			vdev->mtu = cur_mtu;
			err = EINVAL;
		}
		break;

	case MAC_PROP_PRIVATE:
		err = vxge_set_priv_prop(vdev, pr_name, pr_valsize,
		    pr_val);
		break;

	default:
		err = ENOTSUP;
		break;
	}

	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_EXIT GENLOCK", VXGE_IFNAME, vdev->instance);
	mutex_exit(&vdev->genlock);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_M_SETPROP EXIT ",
	    VXGE_IFNAME, vdev->instance);

	return (err);
}

/*ARGSUSED*/
static int
vxge_m_getprop(void *arg, const char *pr_name, mac_prop_id_t pr_num,
    uint_t pr_valsize, void *pr_val)
{

	vxgedev_t *vdev = arg;
	int err = 0;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_M_GETPROP ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	if (pr_valsize == 0)
		return (EINVAL);

	bzero(pr_val, pr_valsize);

	switch (pr_num) {
	case MAC_PROP_DUPLEX:
	case MAC_PROP_SPEED:
	case MAC_PROP_STATUS:
	case MAC_PROP_AUTONEG:
	case MAC_PROP_FLOWCTRL:
	case MAC_PROP_ADV_1000FDX_CAP:
	case MAC_PROP_EN_1000FDX_CAP:
	case MAC_PROP_ADV_100FDX_CAP:
	case MAC_PROP_EN_100FDX_CAP:
	case MAC_PROP_ADV_10FDX_CAP:
	case MAC_PROP_EN_10FDX_CAP:
	case MAC_PROP_EN_1000HDX_CAP:
	case MAC_PROP_EN_100HDX_CAP:
	case MAC_PROP_EN_10HDX_CAP:
	case MAC_PROP_ADV_1000HDX_CAP:
	case MAC_PROP_ADV_100HDX_CAP:
	case MAC_PROP_ADV_10HDX_CAP:
		err = ENOTSUP;
		break;

	case MAC_PROP_PRIVATE:
		err = vxge_get_priv_prop(vdev, pr_name, pr_valsize,
		    pr_val);
		break;

	default:
		err = EINVAL;
		break;
	}

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_M_GETPROP EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (err);
}

/*ARGSUSED*/
static void
vxge_m_propinfo(void *arg, const char *pr_name, mac_prop_id_t pr_num,
    mac_prop_info_handle_t prh)
{
	vxgedev_t *vdev = arg;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_M_PROPINFO ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	switch (pr_num) {
	case MAC_PROP_DUPLEX:
	case MAC_PROP_SPEED:
	case MAC_PROP_STATUS:
	case MAC_PROP_EN_1000HDX_CAP:
	case MAC_PROP_EN_100HDX_CAP:
	case MAC_PROP_EN_10HDX_CAP:
	case MAC_PROP_ADV_1000FDX_CAP:
	case MAC_PROP_ADV_1000HDX_CAP:
	case MAC_PROP_ADV_100FDX_CAP:
	case MAC_PROP_ADV_100HDX_CAP:
	case MAC_PROP_ADV_10FDX_CAP:
	case MAC_PROP_ADV_10HDX_CAP:
		mac_prop_info_set_perm(prh, MAC_PROP_PERM_READ);
		break;

	case MAC_PROP_EN_1000FDX_CAP:
	case MAC_PROP_EN_100FDX_CAP:
	case MAC_PROP_EN_10FDX_CAP:
		mac_prop_info_set_default_uint8(prh, 1);
		break;

	case MAC_PROP_AUTONEG:
		mac_prop_info_set_default_uint8(prh, 1);
		break;

	case MAC_PROP_FLOWCTRL:
		mac_prop_info_set_default_link_flowctrl(prh,
		    LINK_FLOWCTRL_NONE);
		break;

	case MAC_PROP_MTU:
		mac_prop_info_set_range_uint32(prh,
		    VXGE_DEFAULT_MTU, VXGE_MAXIMUM_MTU);
		break;

	case MAC_PROP_PRIVATE: {
		char valstr[64];
		int value;

		bzero(valstr, sizeof (valstr));

		if (strcmp(pr_name, " pciconf") == 0 ||
		    strcmp(pr_name, " about") == 0 ||
		    strcmp(pr_name, " vpath_stats") == 0 ||
		    strcmp(pr_name, " driver_stats") == 0 ||
		    strcmp(pr_name, " mrpcim_stats") == 0 ||
		    strcmp(pr_name, " devconfig") == 0) {
			mac_prop_info_set_perm(prh, MAC_PROP_PERM_READ);
			return;
		}

		if (strcmp(pr_name, " flow_control_gen") == 0) {
			value = VXGE_DEFAULT_RMAC_PAUSE_GEN_EN;
		} else if (strcmp(pr_name, " flow_control_rcv") == 0) {
			value = VXGE_DEFAULT_RMAC_PAUSE_RCV_EN;
		} else if (strcmp(pr_name, " debug_level") == 0) {
			value = vdev->debug_module_level;
		} else if (strcmp(pr_name, " debug_module_mask") == 0) {
			value = vdev->debug_module_mask;
		} else {
			return;
		}
		(void) snprintf(valstr, sizeof (valstr), "%x", value);
	}
	}
	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_M_PROPINFO EXIT",
	    VXGE_IFNAME, vdev->instance);
}

/*ARGSUSED*/
static int
vxge_get_priv_prop(vxgedev_t *vdev, const char *pr_name, uint_t pr_valsize,
	void *pr_val)
{
	int		err = EINVAL;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_GET_PRIV_PROP ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	if (strcmp(pr_name, "_pciconf") == 0) {
		err = vxge_pciconf_get(vdev, pr_valsize, pr_val);
	}

	if (strcmp(pr_name, "_about") == 0) {
		err = vxge_about_get(vdev, pr_valsize, pr_val);
	}

	if (strcmp(pr_name, "_vpath_stats") == 0) {
		err = vxge_stats_vpath_get(vdev, pr_valsize, pr_val);
	}

	if (strcmp(pr_name, "_driver_stats") == 0) {
		err = vxge_stats_driver_get(vdev, pr_valsize, pr_val);
	}

	if (strcmp(pr_name, "_mrpcim_stats") == 0) {
		err = vxge_mrpcim_stats_get(vdev, pr_valsize, pr_val);
	}

	if (strcmp(pr_name, "_identify") == 0) {
		err = vxge_flick_led_get(vdev, pr_valsize, pr_val);
	}

	if (strcmp(pr_name, "_bar0") == 0) {
		err = vxge_bar0_get(vdev, pr_valsize, pr_val);
	}

	if (strcmp(pr_name, "_debug_level") == 0) {
		err = vxge_debug_ldevel_get(vdev, pr_valsize, pr_val);
	}

	if (strcmp(pr_name, "_flow_control_gen") == 0) {
		err = vxge_flow_control_gen_get(vdev, pr_valsize, pr_val);
	}

	if (strcmp(pr_name, "_flow_control_rcv") == 0) {
		err = vxge_flow_control_rcv_get(vdev, pr_valsize, pr_val);
	}

	if (strcmp(pr_name, "_debug_module_mask") == 0) {
		err = vxge_debug_module_mask_get(vdev, pr_valsize, pr_val);
	}

	if (strcmp(pr_name, "_devconfig") == 0) {
		err = vxge_devconfig_get(vdev, pr_valsize, pr_val);
	}


	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_GET_PRIV_PROP EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (err);
}

/*ARGSUSED*/
static int
vxge_stats_vpath_get(vxgedev_t *vdev, uint_t pr_valsize, void *pr_val)
{
	vxge_hal_status_e status;
	uint_t		strsize;
	int i, retsize = 0, leftsize = 0, ret = 0;
	char *buf, *ptr;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_STATS_VPATH_GET ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	buf = kmem_zalloc(VXGE_STATS_BUFSIZE * vdev->no_of_vpath,
	    KM_NOSLEEP);
	if (buf == NULL) {
		VXGE_STATS_DRV_INC(kmem_zalloc_fail);
		return (ENOSPC);
	}

	VXGE_STATS_DRV_ADD(kmem_alloc,
	    VXGE_STATS_BUFSIZE * vdev->no_of_vpath);

	leftsize = (VXGE_STATS_BUFSIZE * vdev->no_of_vpath);
	ptr = buf;

	ret = vxge_os_sprintf(buf + retsize, "##### VPATH STATS ##### \n");
	ptr += ret;

	status = vxge_hal_aux_stats_device_hw_read(
	    vdev->devh, leftsize, ptr, &retsize);
	retsize = retsize + ret;

	if (status != VXGE_HAL_OK) {
		kmem_free(buf, VXGE_STATS_BUFSIZE * vdev->no_of_vpath);
		VXGE_STATS_DRV_ADD(kmem_free,
		    VXGE_STATS_BUFSIZE * vdev->no_of_vpath);
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: Failure in getting HW Stats ",
		    VXGE_IFNAME, vdev->instance);
		return (EINVAL);
	}

	for (i = 0; i < vdev->no_of_fifo; i++) {
		ret = vxge_os_sprintf(buf + retsize,
		    "fifo%d_tx_cnt\t\t\t\t: %"PRId64" \n",
		    i, vdev->vpaths[i].fifo.fifo_tx_cnt);
		retsize = retsize + ret;

		ret = vxge_os_sprintf(buf + retsize,
		    "fifo%d_tx_cmpl_cnt\t\t\t: %"PRId64" \n",
		    i, vdev->vpaths[i].fifo.fifo_tx_cmpl_cnt);
		retsize = retsize + ret;
	}

	for (i = 0; i < vdev->no_of_ring; i++) {

		ret = vxge_os_sprintf(buf + retsize,
		    "ring%d_intr_cnt\t\t\t\t: %"PRId64" \n",
		    i, vdev->vpaths[i].ring.ring_intr_cnt);
		retsize = retsize + ret;

		ret = vxge_os_sprintf(buf + retsize,
		    "ring%d_rx_cmpl_cnt\t\t\t: %"PRId64" \n",
		    i, vdev->vpaths[i].ring.ring_rx_cmpl_cnt);
		retsize = retsize + ret;
	}

	*(buf + retsize - 1) = '\0'; /* remove last '\n' */

	strsize = (uint_t)strlen(buf);
	if (pr_valsize < strsize) {
		return (ENOBUFS);
	} else {
		(void) strlcpy(pr_val, buf, pr_valsize);
	}

	kmem_free(buf, VXGE_STATS_BUFSIZE * vdev->no_of_vpath);
	VXGE_STATS_DRV_ADD(kmem_free,
	    VXGE_STATS_BUFSIZE * vdev->no_of_vpath);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_STATS_VPATH_GET EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (0);
}

/*ARGSUSED*/
static int
vxge_stats_driver_get(vxgedev_t *vdev, uint_t pr_valsize, void *pr_val)
{
	vxge_hal_status_e status;
	uint_t		strsize;
	int retsize = 0, rsize = 0, leftsize = 0, ret = 0;
	char *buf, *ptr;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_STATS_DRIVER_GET ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	buf = kmem_zalloc(VXGE_STATS_BUFSIZE * vdev->no_of_vpath,
	    KM_NOSLEEP);
	if (buf == NULL) {
		VXGE_STATS_DRV_INC(kmem_zalloc_fail);
		return (ENOSPC);
	}

	VXGE_STATS_DRV_ADD(kmem_alloc,
	    VXGE_STATS_BUFSIZE * vdev->no_of_vpath);

	leftsize = (VXGE_STATS_BUFSIZE * vdev->no_of_vpath);
	ptr = buf;

	ret = vxge_os_sprintf(buf + retsize, "##### DRIVER STATS ##### \n");
	retsize += ret;
	ptr += retsize;
	leftsize -= ret;

	status = vxge_hal_aux_stats_device_sw_read(
	    vdev->devh, leftsize, ptr, &rsize);

	if (status != VXGE_HAL_OK) {
		kmem_free(buf, VXGE_STATS_BUFSIZE * vdev->no_of_vpath);
		VXGE_STATS_DRV_ADD(kmem_free,
		    VXGE_STATS_BUFSIZE * vdev->no_of_vpath);
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: Failure in getting SW Stats ",
		    VXGE_IFNAME, vdev->instance);
		return (EINVAL);
	}

	retsize += rsize;

	ret = vxge_os_sprintf(buf + retsize, "tx_frms\t\t\t\t: %"PRId64" \n",
	    vdev->stats.tx_frms);
	retsize = retsize + ret;

	ret = vxge_os_sprintf(buf + retsize, "rx_frms\t\t\t\t: %"PRId64" \n",
	    vdev->stats.rx_frms);
	retsize = retsize + ret;

	ret = vxge_os_sprintf(buf + retsize, "vpaths_open\t\t\t: %"PRId64" \n",
	    vdev->stats.vpaths_open);
	retsize = retsize + ret;

	ret = vxge_os_sprintf(buf + retsize,
	    "vpath_open_fail\t\t\t: %"PRId64" \n",
	    vdev->stats.vpath_open_fail);
	retsize = retsize + ret;

	ret = vxge_os_sprintf(buf + retsize, "link_up\t\t\t\t: %"PRId64" \n",
	    vdev->stats.link_up);
	retsize = retsize + ret;

	ret = vxge_os_sprintf(buf + retsize, "link_down	\t\t: %"PRId64" \n",
	    vdev->stats.link_down);
	retsize = retsize + ret;

	ret = vxge_os_sprintf(buf + retsize,
	    "kmem_zalloc_fail\t\t: %"PRId64" \n",
	    vdev->stats.kmem_zalloc_fail);
	retsize = retsize + ret;

	ret = vxge_os_sprintf(buf + retsize, "allocb_fail\t\t\t: %"PRId64" \n",
	    vdev->stats.allocb_fail);
	retsize = retsize + ret;

	ret = vxge_os_sprintf(buf + retsize,
	    "desballoc_fail\t\t\t: %"PRId64" \n",
	    vdev->stats.desballoc_fail);
	retsize = retsize + ret;

	ret = vxge_os_sprintf(buf + retsize,
	    "ddi_dma_alloc_handle_fail\t: %"PRId64" \n",
	    vdev->stats.ddi_dma_alloc_handle_fail);
	retsize = retsize + ret;

	ret = vxge_os_sprintf(buf + retsize,
	    "ddi_dma_mem_alloc_fail\t\t: %"PRId64" \n",
	    vdev->stats.ddi_dma_mem_alloc_fail);
	retsize = retsize + ret;

	ret = vxge_os_sprintf(buf + retsize,
	    "ddi_dma_addr_bind_handle_fail\t: %"PRId64" \n",
	    vdev->stats.ddi_dma_addr_bind_handle_fail);
	retsize = retsize + ret;

	ret = vxge_os_sprintf(buf + retsize,
	    "ddi_dma_addr_unbind_handle_fail\t: %"PRId64" \n",
	    vdev->stats.ddi_dma_addr_unbind_handle_fail);
	retsize = retsize + ret;

	ret = vxge_os_sprintf(buf + retsize, "kmem_alloc\t\t\t: %"PRId64" \n",
	    vdev->stats.kmem_alloc);
	retsize = retsize + ret;

	ret = vxge_os_sprintf(buf + retsize, "kmem_free\t\t\t: %"PRId64" \n",
	    vdev->stats.kmem_free);
	retsize = retsize + ret;

	ret = vxge_os_sprintf(buf + retsize,
	    "spurious_intr_cnt\t\t: %"PRId64" \n",
	    vdev->stats.spurious_intr_cnt);
	retsize = retsize + ret;

	ret = vxge_os_sprintf(buf + retsize,
	    "dma_sync_fail_cnt\t\t: %"PRId64" \n",
	    vdev->stats.dma_sync_fail_cnt);
	retsize = retsize + ret;

	ret = vxge_os_sprintf(buf + retsize,
	    "copyit_mblk_buff_cnt\t\t: %"PRId64" \n",
	    vdev->stats.copyit_mblk_buff_cnt);
	retsize = retsize + ret;

	ret = vxge_os_sprintf(buf + retsize, "rx_tcode_cnt\t\t\t: %"PRId64" \n",
	    vdev->stats.rx_tcode_cnt);
	retsize = retsize + ret;

	ret = vxge_os_sprintf(buf + retsize, "tx_tcode_cnt\t\t\t: %"PRId64" \n",
	    vdev->stats.tx_tcode_cnt);
	retsize = retsize + ret;

	ret = vxge_os_sprintf(buf + retsize,
	    "xmit_compl_cnt\t\t\t: %"PRId64" \n",
	    vdev->stats.xmit_compl_cnt);
	retsize = retsize + ret;

	ret = vxge_os_sprintf(buf + retsize,
	    "xmit_tot_resched_cnt\t\t: %"PRId64" \n",
	    vdev->stats.xmit_tot_resched_cnt);
	retsize = retsize + ret;

	ret = vxge_os_sprintf(buf + retsize,
	    "xmit_resched_cnt\t\t: %"PRId64" \n",
	    vdev->stats.xmit_resched_cnt);
	retsize = retsize + ret;

	ret = vxge_os_sprintf(buf + retsize,
	    "xmit_tot_update_cnt\t\t: %"PRId64" \n",
	    vdev->stats.xmit_tot_update_cnt);
	retsize = retsize + ret;

	ret = vxge_os_sprintf(buf + retsize,
	    "xmit_update_cnt\t\t\t: %"PRId64" \n",
	    vdev->stats.xmit_update_cnt);
	retsize = retsize + ret;

	ret = vxge_os_sprintf(buf + retsize, "low_dtr_cnt\t\t\t: %"PRId64" \n",
	    vdev->stats.low_dtr_cnt);
	retsize = retsize + ret;

	ret = vxge_os_sprintf(buf + retsize,
	    "fifo_txdl_reserve_fail_cnt\t: %"PRId64" \n",
	    vdev->stats.fifo_txdl_reserve_fail_cnt);
	retsize = retsize + ret;

	ret = vxge_os_sprintf(buf + retsize,
	    "ring_buff_pool_free_cnt\t\t: %"PRId64" \n",
	    vdev->stats.ring_buff_pool_free_cnt);
	retsize = retsize + ret;

	ret = vxge_os_sprintf(buf + retsize, "rxd_full_cnt\t\t\t: %"PRId64" \n",
	    vdev->stats.rxd_full_cnt);
	retsize = retsize + ret;

	ret = vxge_os_sprintf(buf + retsize,
	    "vxge_reset_cnt\t\t\t: %"PRId64" \n",
	    vdev->stats.reset_cnt);
	retsize = retsize + ret;

	*(buf + retsize - 1) = '\0'; /* remove last '\n' */

	strsize = (uint_t)strlen(buf);
	if (pr_valsize < strsize) {
		return (ENOBUFS);
	} else {
		(void) strlcpy(pr_val, buf, pr_valsize);
	}

	kmem_free(buf, VXGE_STATS_BUFSIZE * vdev->no_of_vpath);
	VXGE_STATS_DRV_ADD(kmem_free,
	    VXGE_STATS_BUFSIZE * vdev->no_of_vpath);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_STATS_DRIVER_GET EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (0);
}

/*ARGSUSED*/
static int
vxge_mrpcim_stats_get(vxgedev_t *vdev, uint_t pr_valsize, void *pr_val)
{
	vxge_hal_status_e status;
	uint_t		strsize;
	int retsize = 0;
	char *buf;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_MRPCIM_STATS_GET ENTRY ",
	    VXGE_IFNAME, vdev->instance);
	buf = kmem_zalloc(VXGE_MRPCIM_STATS_BUFSIZE * vdev->no_of_vpath,
	    KM_NOSLEEP);
	if (buf == NULL) {
		VXGE_STATS_DRV_INC(kmem_zalloc_fail);
		return (ENOSPC);
	}

	VXGE_STATS_DRV_ADD(kmem_alloc,
	    VXGE_MRPCIM_STATS_BUFSIZE * vdev->no_of_vpath);

	status = vxge_hal_aux_stats_mrpcim_read(vdev->devh,
	    VXGE_MRPCIM_STATS_BUFSIZE * vdev->no_of_vpath,
	    buf, &retsize);
	if (status !=   VXGE_HAL_OK) {
		kmem_free(buf,
		    VXGE_MRPCIM_STATS_BUFSIZE * vdev->no_of_vpath);
		VXGE_STATS_DRV_ADD(kmem_free,
		    VXGE_MRPCIM_STATS_BUFSIZE * vdev->no_of_vpath);
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: Failure in getting MRPCIM HW Stats ",
		    VXGE_IFNAME, vdev->instance);
		return (EINVAL);
	}
	*(buf + retsize - 1) = '\0'; /* remove last '\n' */

	strsize = (uint_t)strlen(buf);
	if (pr_valsize < strsize) {
		return (ENOBUFS);
	} else {
		(void) strlcpy(pr_val, buf, pr_valsize);
	}

	kmem_free(buf, VXGE_MRPCIM_STATS_BUFSIZE * vdev->no_of_vpath);
	VXGE_STATS_DRV_ADD(kmem_free,
	    VXGE_MRPCIM_STATS_BUFSIZE * vdev->no_of_vpath);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_MRPCIM_STATS_GET EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (0);
}

/*ARGSUSED*/
static int
vxge_pciconf_get(vxgedev_t *vdev, uint_t pr_valsize, void *pr_val)
{
	vxge_hal_status_e status;
	uint_t		strsize;
	int retsize;
	char *buf;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_PCICONF_GET ENTRY ",
	    VXGE_IFNAME, vdev->instance);
	buf = kmem_zalloc(VXGE_PCICONF_BUFSIZE, KM_NOSLEEP);
	if (buf == NULL) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: kmem_zalloc failed\n",
		    VXGE_IFNAME, vdev->instance);
		VXGE_STATS_DRV_INC(kmem_zalloc_fail);
		return (ENOSPC);
	}
	VXGE_STATS_DRV_ADD(kmem_alloc, VXGE_PCICONF_BUFSIZE);
	status = vxge_hal_aux_pci_config_read(vdev->devh,
	    VXGE_PCICONF_BUFSIZE, buf, &retsize);
	if (status != VXGE_HAL_OK) {
		kmem_free(buf, VXGE_PCICONF_BUFSIZE);
		VXGE_STATS_DRV_ADD(kmem_free, VXGE_PCICONF_BUFSIZE);
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: pci_config_read(): status %d",
		    VXGE_IFNAME, vdev->instance, status);
		return (EINVAL);
	}
	*(buf + retsize - 1) = '\0'; /* remove last '\n' */

	strsize = (uint_t)strlen(buf);
	if (pr_valsize < strsize) {
		return (ENOBUFS);
	} else {
		(void) strlcpy(pr_val, buf, pr_valsize);
	}

	kmem_free(buf, VXGE_PCICONF_BUFSIZE);
	VXGE_STATS_DRV_ADD(kmem_free, VXGE_PCICONF_BUFSIZE);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_PCICONF_GET ENTRY ",
	    VXGE_IFNAME, vdev->instance);
	return (0);
}

/*ARGSUSED*/
static int
vxge_about_get(vxgedev_t *vdev, uint_t pr_valsize, void *pr_val)
{
	vxge_hal_status_e status;
	uint_t		strsize;
	int retsize;
	char *buf;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_ABOUT_GET ENTRY ",
	    VXGE_IFNAME, vdev->instance);
	buf = kmem_zalloc(VXGE_ABOUT_BUFSIZE, KM_NOSLEEP);
	if (buf == NULL) {
		VXGE_STATS_DRV_INC(kmem_zalloc_fail);
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: kmem_zalloc failed\n",
		    VXGE_IFNAME, vdev->instance);
		return (ENOSPC);
	}
	VXGE_STATS_DRV_ADD(kmem_alloc, VXGE_ABOUT_BUFSIZE);
	status = vxge_hal_aux_about_read(vdev->devh, VXGE_ABOUT_BUFSIZE,
	    buf, &retsize);
	if (status != VXGE_HAL_OK) {
		kmem_free(buf, VXGE_ABOUT_BUFSIZE);
		VXGE_STATS_DRV_ADD(kmem_free, VXGE_ABOUT_BUFSIZE);
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: about_read(): status %d",
		    VXGE_IFNAME, vdev->instance, status);
		return (EINVAL);
	}
	(void) sprintf((buf + retsize), "Firmware %s",
	    vdev->config.device_hw_info.fw_version.version);
	/* Number of bytes for "Firmware " is 9 */
	retsize += sizeof (vdev->config.device_hw_info.fw_version.version) + 9;
	*(buf + retsize - 1) = '\0'; /* remove last '\n' */

	strsize = (uint_t)strlen(buf);
	if (pr_valsize < strsize) {
		return (ENOBUFS);
	} else {
		(void) strlcpy(pr_val, buf, pr_valsize);
	}

	kmem_free(buf, VXGE_ABOUT_BUFSIZE);
	VXGE_STATS_DRV_ADD(kmem_free, VXGE_ABOUT_BUFSIZE);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_ABOUT_GET EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (0);
}

static unsigned long bar0_offset = 0x110; /* adapter_control */

/*ARGSUSED*/
static int
vxge_bar0_get(vxgedev_t *vdev, uint_t pr_valsize, void *pr_val)
{
	vxge_hal_status_e status;
	uint_t		strsize;
	u64 value, ret;
	char *buf;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_BAR0_GET ENTRY ",
	    VXGE_IFNAME, vdev->instance);
	buf = kmem_zalloc(VXGE_IOCTL_BUFSIZE, KM_NOSLEEP);
	if (buf == NULL) {
		VXGE_STATS_DRV_INC(kmem_zalloc_fail);
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: kmem_zalloc failed\n", VXGE_IFNAME,
		    vdev->instance);
		return (ENOSPC);
	}
	VXGE_STATS_DRV_ADD(kmem_alloc, VXGE_IOCTL_BUFSIZE);
	status = vxge_hal_mgmt_bar0_read(vdev->devh, bar0_offset, &value);
	if (status != VXGE_HAL_OK) {
		kmem_free(buf, VXGE_IOCTL_BUFSIZE);
		VXGE_STATS_DRV_ADD(kmem_free, VXGE_IOCTL_BUFSIZE);
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: bar0_read(): status %d",
		    VXGE_IFNAME, vdev->instance, status);
		return (EINVAL);
	}
	ret = vxge_os_sprintf(buf, "%"PRIx64, value);
	*(buf + ret) = '\0'; /* remove last '\n' */

	strsize = (uint_t)strlen(buf);
	if (pr_valsize < strsize) {
		return (ENOBUFS);
	} else {
		(void) strlcpy(pr_val, buf, pr_valsize);
	}

	kmem_free(buf, VXGE_IOCTL_BUFSIZE);
	VXGE_STATS_DRV_ADD(kmem_free, VXGE_IOCTL_BUFSIZE);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_BAR0_GET EXIT ", VXGE_IFNAME, vdev->instance);
	return (0);
}

/*ARGSUSED*/
static int
vxge_bar0_set(vxgedev_t *vdev, uint_t val_size, char *value)
{
	unsigned long old_offset = bar0_offset;
	char *end;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s: VXGE_BAR0_SET ENTRY ", VXGE_IFNAME);

	if (value && *value == '0' &&
	    (*(value + 1) == 'x' || *(value + 1) == 'X')) {
		value += 2;
	}

	bar0_offset = mi_strtol(value, &end, 16);
	if (end == value) {
		bar0_offset = old_offset;
		return (EINVAL);
	}

	vxge_debug_driver(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "bar0: new value %s:%lX", value, bar0_offset);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s: VXGE_BAR0_SET EXIT ", VXGE_IFNAME);

	return (0);
}

/*ARGSUSED*/
static int
vxge_debug_ldevel_get(vxgedev_t *vdev, uint_t pr_valsize, void *pr_val)
{
	char *buf;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_DEBUG_LEVEL_GET ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	buf = kmem_zalloc(VXGE_IOCTL_BUFSIZE, KM_NOSLEEP);
	if (buf == NULL) {
		VXGE_STATS_DRV_INC(kmem_zalloc_fail);
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: kmem_zalloc failed\n", VXGE_IFNAME,
		    vdev->instance);
		return (ENOSPC);
	}
	VXGE_STATS_DRV_ADD(kmem_alloc, VXGE_IOCTL_BUFSIZE);

	(void) snprintf(buf, VXGE_IOCTL_BUFSIZE, "%d",
	    vdev->debug_module_level);
	(void) strlcpy(pr_val, buf, pr_valsize);

	kmem_free(buf, VXGE_IOCTL_BUFSIZE);
	VXGE_STATS_DRV_ADD(kmem_free, VXGE_IOCTL_BUFSIZE);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_DEBUG_LEVEL_GET EXIT ", VXGE_IFNAME,
	    vdev->instance);
	return (0);
}

/*ARGSUSED*/
static int
vxge_debug_ldevel_set(vxgedev_t *vdev, uint_t val_size, char *value)
{
	int level;
	char *end;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s: VXGE_DEBUG_LEVEL_SET ENTRY ", VXGE_IFNAME);
	level = mi_strtol(value, &end, 10);

	if (level < VXGE_NONE || level > VXGE_ERR || end == value) {
		return (EINVAL);
	}

	vxge_hal_device_debug_set(vdev->devh, level,
	    vdev->debug_module_level);
	vdev->debug_module_level = level;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s: VXGE_DEBUG_LEVEL_SET EXIT ", VXGE_IFNAME);
	return (0);
}

/*ARGSUSED*/
static int
vxge_identify_adapter(vxgedev_t *vdev, uint_t pr_valsize, char *value)
{
	u32 flick_led = 0;
	char *end;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_IDENTIFY_ADAPTER ENTRY ",
	    VXGE_IFNAME, vdev->instance);
	flick_led = mi_strtol(value, &end, 10);

	if (flick_led != 0 && flick_led != 1 || end == value)
		return (EINVAL);

	(void *) vxge_hal_device_flick_link_led(vdev->devh, 0, flick_led);

	vdev->flick_led = flick_led;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_IDENTIFY_ADAPTER EXIT ",
	    VXGE_IFNAME, vdev->instance);

	return (0);
}

/*ARGSUSED*/
static int
vxge_flick_led_get(vxgedev_t *vdev, uint_t pr_valsize, void *pr_val)
{
	char *buf;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_FILCK_LED_GET ENTRY ",
	    VXGE_IFNAME, vdev->instance);
	buf = kmem_zalloc(VXGE_IOCTL_BUFSIZE, KM_NOSLEEP);
	if (buf == NULL) {
		VXGE_STATS_DRV_INC(kmem_zalloc_fail);
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: kmem_zalloc failed\n", VXGE_IFNAME,
		    vdev->instance);
		return (ENOSPC);
	}
	VXGE_STATS_DRV_ADD(kmem_alloc, VXGE_IOCTL_BUFSIZE);

	(void) snprintf(buf, VXGE_IOCTL_BUFSIZE, "%d",
	    (vdev->flick_led) ? 1 : 0);
	(void) strlcpy(pr_val, buf, pr_valsize);

	kmem_free(buf, VXGE_IOCTL_BUFSIZE);
	VXGE_STATS_DRV_ADD(kmem_free, VXGE_IOCTL_BUFSIZE);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_FILCK_LED_GET EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (0);
}

/*ARGSUSED*/
static int
vxge_flow_control_gen_get(vxgedev_t *vdev, uint_t pr_valsize, void *pr_val)
{
	char *buf;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_FLOW_CONTROL_GEN_GET ENTRY ",
	    VXGE_IFNAME, vdev->instance);
	buf = kmem_zalloc(VXGE_IOCTL_BUFSIZE, KM_NOSLEEP);
	if (buf == NULL) {
		VXGE_STATS_DRV_INC(kmem_zalloc_fail);
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: kmem_zalloc failed\n", VXGE_IFNAME,
		    vdev->instance);
		return (ENOSPC);
	}
	VXGE_STATS_DRV_ADD(kmem_alloc, VXGE_IOCTL_BUFSIZE);

	(void) snprintf(buf, VXGE_IOCTL_BUFSIZE, "%d",
	    (vdev->config.flow_control_gen) ? 1 : 0);
	(void) strlcpy(pr_val, buf, pr_valsize);

	kmem_free(buf, VXGE_IOCTL_BUFSIZE);
	VXGE_STATS_DRV_ADD(kmem_free, VXGE_IOCTL_BUFSIZE);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_FLOW_CONTROL_GEN_GET EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (0);
}

/*ARGSUSED*/
static int
vxge_flow_control_gen_set(vxgedev_t *vdev, uint_t val_size, char *value)
{
	u32 flow_control_gen = 0;
	char *end;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_FLOW_CONTROL_GEN_SET ENTRY ", VXGE_IFNAME,
	    vdev->instance);

	flow_control_gen = mi_strtol(value, &end, 10);
	if (flow_control_gen != 0 && flow_control_gen != 1 || end == value)
		return (EINVAL);

	if (vdev->config.flow_control_gen != flow_control_gen) {
		vdev->config.flow_control_gen = flow_control_gen;
		(void) vxge_hal_mrpcim_setpause_data(vdev->devh, 0,
		    vdev->config.flow_control_gen,
		    vdev->config.flow_control_rcv);
	}

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_FLOW_CONTROL_GEN_SET EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (0);
}

/*ARGSUSED*/
static int
vxge_flow_control_rcv_get(vxgedev_t *vdev, uint_t pr_valsize, void *pr_val)
{
	char *buf;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_FLOW_CONTROL_RCV_GET ENTRY ",
	    VXGE_IFNAME, vdev->instance);
	buf = kmem_zalloc(VXGE_IOCTL_BUFSIZE, KM_NOSLEEP);
	if (buf == NULL) {
		VXGE_STATS_DRV_INC(kmem_zalloc_fail);
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: kmem_zalloc failed\n", VXGE_IFNAME,
		    vdev->instance);
		return (ENOSPC);
	}
	VXGE_STATS_DRV_ADD(kmem_alloc, VXGE_IOCTL_BUFSIZE);

	(void) snprintf(buf, VXGE_IOCTL_BUFSIZE, "%d",
	    (vdev->config.flow_control_rcv) ? 1 : 0);
	(void) strlcpy(pr_val, buf, pr_valsize);

	kmem_free(buf, VXGE_IOCTL_BUFSIZE);
	VXGE_STATS_DRV_ADD(kmem_free, VXGE_IOCTL_BUFSIZE);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_FLOW_CONTROL_RCV_GET EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (0);
}

/*ARGSUSED*/
static int
vxge_flow_control_rcv_set(vxgedev_t *vdev, uint_t val_size, char *value)
{
	u32 flow_control_rcv = 0;
	char *end;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_FLOW_CONTROL_RCV_SET ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	flow_control_rcv = mi_strtol(value, &end, 10);
	if (flow_control_rcv != 0 && flow_control_rcv != 1 || end == value)
		return (EINVAL);

	if (vdev->config.flow_control_rcv != flow_control_rcv) {
		vdev->config.flow_control_rcv = flow_control_rcv;
		(void) vxge_hal_mrpcim_setpause_data(vdev->devh, 0,
		    vdev->config.flow_control_gen,
		    vdev->config.flow_control_rcv);
	}

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_FLOW_CONTROL_RCV_SET EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (0);
}

/*ARGSUSED*/
static int
vxge_debug_module_mask_get(vxgedev_t *vdev, uint_t pr_valsize, void *pr_val)
{
	char *buf;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_DEBUG_MODULE_MASK_GET ENTRY ",
	    VXGE_IFNAME, vdev->instance);
	buf = kmem_zalloc(VXGE_IOCTL_BUFSIZE, KM_NOSLEEP);
	if (buf == NULL) {
		VXGE_STATS_DRV_INC(kmem_zalloc_fail);
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: kmem_zalloc failed\n",
		    VXGE_IFNAME, vdev->instance);
		return (ENOSPC);
	}
	VXGE_STATS_DRV_ADD(kmem_alloc, VXGE_IOCTL_BUFSIZE);

	(void) snprintf(buf, VXGE_IOCTL_BUFSIZE, "0x%08x",
	    vdev->debug_module_mask);
	(void) strlcpy(pr_val, buf, pr_valsize);

	kmem_free(buf, VXGE_IOCTL_BUFSIZE);
	VXGE_STATS_DRV_ADD(kmem_free, VXGE_IOCTL_BUFSIZE);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_DEBUG_MODULE_MASK_GET EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (0);
}

/*ARGSUSED*/
static int
vxge_debug_module_mask_set(vxgedev_t *vdev, uint_t val_size, char *value)
{
	u32 mask;
	char *end;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_DEBUG_MODULE_MASK_SET ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	if (value && *value == '0' &&
	    (*(value + 1) == 'x' || *(value + 1) == 'X')) {
		value += 2;
	}

	mask = mi_strtol(value, &end, 16);
	if (end == value)
		return (EINVAL);

	vxge_hal_device_debug_set(vdev->devh, vdev->debug_module_level, mask);
	vdev->debug_module_mask = mask;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_DEBUG_MODULE_MASK_SET EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (0);
}

/*ARGSUSED*/
static int
vxge_devconfig_get(vxgedev_t *vdev, uint_t pr_valsize, void *pr_val)
{
	vxge_hal_status_e	status;
	uint_t			strsize;
	int retsize, ret = 0;
	char *buf;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_DEVCONFIG_GET ENTRY ",
	    VXGE_IFNAME, vdev->instance);
	buf = kmem_zalloc(VXGE_DEVCONF_BUFSIZE, KM_NOSLEEP);
	if (buf == NULL) {
		VXGE_STATS_DRV_INC(kmem_zalloc_fail);
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: kmem_zalloc failed\n", VXGE_IFNAME,
		    vdev->instance);
		return (ENOSPC);
	}

	VXGE_STATS_DRV_ADD(kmem_alloc, VXGE_DEVCONF_BUFSIZE);
	status = vxge_hal_aux_device_config_read(vdev->devh,
	    VXGE_DEVCONF_BUFSIZE, buf, &retsize);
	if (status != VXGE_HAL_OK) {
		kmem_free(buf, VXGE_DEVCONF_BUFSIZE);
		VXGE_STATS_DRV_ADD(kmem_free, VXGE_DEVCONF_BUFSIZE);
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: device_config_read(): status %d",
		    VXGE_IFNAME, vdev->instance, status);
		return (EINVAL);
	}
	ret = vxge_os_sprintf(buf + retsize, "Large Send Offload : %s \n",
	    vdev->config.lso_enable ? "ON" : "OFF");
	retsize = retsize + ret;

	ret = vxge_os_sprintf(buf + retsize, "tx_steering_type : %d \n",
	    vdev->config.tx_steering_type);
	retsize = retsize + ret;

	ret = vxge_os_sprintf(buf + retsize, "flow_control_gen : %s \n",
	    vdev->config.flow_control_gen ? "ON" : "OFF");
	retsize = retsize + ret;

	ret = vxge_os_sprintf(buf + retsize, "flow_control_rcv : %s \n",
	    vdev->config.flow_control_rcv ? "ON" : "OFF");
	retsize = retsize + ret;

	ret = vxge_os_sprintf(buf + retsize, "fm-capable : %d \n",
	    vdev->config.fm_capabilities);
	retsize = retsize + ret;

	ret = vxge_os_sprintf(buf + retsize, "fifo_dma_lowat : %d \n",
	    vdev->config.tx_dma_lowat);
	retsize = retsize + ret;

	ret = vxge_os_sprintf(buf + retsize, "ring_dma_lowat : %d \n",
	    vdev->config.rx_dma_lowat);
	retsize = retsize + ret;

	ret = vxge_os_sprintf(buf + retsize, "rx_pkt_burst : %d \n",
	    vdev->config.rx_pkt_burst);
	retsize = retsize + ret;

	ret = vxge_os_sprintf(buf + retsize, "rx_buffer_total_per_rxd : %d \n",
	    vdev->config.rx_buffer_total_per_rxd);
	retsize = retsize + ret;

	ret = vxge_os_sprintf(buf + retsize,
	    "rx_buffer_post_hiwat_per_rxd : %d \n",
	    vdev->config.rx_buffer_post_hiwat_per_rxd);
	retsize = retsize + ret;

	ret = vxge_os_sprintf(buf + retsize, "strip_vlan_tag : %d \n",
	    vdev->config.strip_vlan_tag);
	retsize = retsize + ret;

	ret = vxge_os_sprintf(buf + retsize, "vlan_promisc_enable : %d \n",
	    vdev->config.vlan_promisc_enable);
	retsize = retsize + ret;

	*(buf + retsize - 1) = '\0'; /* remove last '\n' */

	strsize = (uint_t)strlen(buf);
	if (pr_valsize < strsize) {
		return (ENOBUFS);
	} else {
		(void) strlcpy(pr_val, buf, pr_valsize);
	}

	kmem_free(buf, VXGE_DEVCONF_BUFSIZE);
	VXGE_STATS_DRV_ADD(kmem_free, VXGE_DEVCONF_BUFSIZE);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_DEVCONFIG_GET EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (0);
}
static int
vxge_update_aggr_kstats(kstat_t *ksp, int rw)
{
	vxge_hal_status_e status = VXGE_HAL_OK;
	vxge_kstat_aggr_t  *vxge_ksp;
	vxge_hal_xmac_aggr_stats_t aggr_stats;
	int j;
	port_stats_t *port_info = (port_stats_t *)ksp->ks_private;
	vxgedev_t *vdev = (vxgedev_t *)port_info->vdev;
	vxge_hal_device_t *hldev;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_UPDATE_AGGR_KSTATS ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	if (!vdev->is_initialized) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: device is not initialized...",
		    VXGE_IFNAME, vdev->instance);
		return (status);
	}

	hldev = (vxge_hal_device_t *)vdev->devh;

	j = port_info->port_id;
	vxge_ksp = (vxge_kstat_aggr_t *)vdev->vxge_kstat_aggr_ksp[j]->ks_data;

	if (rw == KSTAT_WRITE) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: Write Support is not added\n",
		    VXGE_IFNAME, vdev->instance);
		return (EACCES);
	}

	status = vxge_hal_mrpcim_xmac_aggr_stats_get(hldev, j, &aggr_stats);
	if (status != VXGE_HAL_OK) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: Error vxge_hal_mrpcim_xmac_aggr_stats_get\n",
		    VXGE_IFNAME, vdev->instance);
		return (status);
	}

	vxge_ksp->rx_frms.value.ull	= aggr_stats.rx_frms;
	vxge_ksp->rx_data_octets.value.ull = aggr_stats.rx_data_octets;
	vxge_ksp->rx_mcast_frms.value.ull  = aggr_stats.rx_mcast_frms;
	vxge_ksp->rx_bcast_frms.value.ull  = aggr_stats.rx_bcast_frms;
	vxge_ksp->rx_discarded_frms.value.ull = aggr_stats.rx_discarded_frms;
	vxge_ksp->rx_errored_frms.value.ull   = aggr_stats.rx_errored_frms;
	vxge_ksp->rx_unknown_slow_proto_frms.value.ull =
	    aggr_stats.rx_unknown_slow_proto_frms;

	vxge_ksp->tx_frms.value.ull	= aggr_stats.tx_frms;
	vxge_ksp->tx_data_octets.value.ull = aggr_stats.tx_data_octets;
	vxge_ksp->tx_mcast_frms.value.ull  = aggr_stats.tx_mcast_frms;
	vxge_ksp->tx_bcast_frms.value.ull  = aggr_stats.tx_bcast_frms;
	vxge_ksp->tx_discarded_frms.value.ull = aggr_stats.tx_discarded_frms;
	vxge_ksp->tx_errored_frms.value.ull   = aggr_stats.tx_errored_frms;


	if (vxge_check_acc_handle(hldev->regh0) != DDI_FM_OK)
		ddi_fm_service_impact(vdev->dev_info, DDI_SERVICE_UNAFFECTED);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_UPDATE_AGGR_KSTATS EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (0);
}

static int
vxge_update_port_kstats(kstat_t *ksp, int rw)
{
	vxge_hal_status_e status = VXGE_HAL_OK;
	vxge_kstat_port_t  *vxge_ksp;
	vxge_hal_xmac_port_stats_t port_stats;
	int j;
	port_stats_t *port_info = (port_stats_t *)ksp->ks_private;
	vxgedev_t *vdev = (vxgedev_t *)port_info->vdev;
	vxge_hal_device_t *hldev;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_UPDATE_PORT_KSTATS ENTRY ",
	    VXGE_IFNAME, vdev->instance);
	if (!vdev->is_initialized) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: device is not initialized...",
		    VXGE_IFNAME, vdev->instance);
		return (status);
	}

	hldev = (vxge_hal_device_t *)vdev->devh;

	j = port_info->port_id;
	vxge_ksp = (vxge_kstat_port_t *)vdev->vxge_kstat_port_ksp[j]->ks_data;

	if (rw == KSTAT_WRITE) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: Write Support is not added\n",
		    VXGE_IFNAME, vdev->instance);
		return (EACCES);
	}

	status = vxge_hal_mrpcim_xmac_port_stats_get(hldev, j, &port_stats);

	if (status != VXGE_HAL_OK) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: Error vxge_hal_mrpcim_xmac_aggr_stats_get\n",
		    VXGE_IFNAME, vdev->instance);

		return (status);
	}

	vxge_ksp->rx_ttl_frms.value.ull	 = port_stats.rx_ttl_frms;
	vxge_ksp->rx_vld_frms.value.ull	 = port_stats.rx_vld_frms;
	vxge_ksp->rx_offload_frms.value.ull = port_stats.rx_offload_frms;
	vxge_ksp->rx_ttl_octets.value.ull   = port_stats.rx_ttl_octets;
	vxge_ksp->rx_data_octets.value.ull  = port_stats.rx_data_octets;
	vxge_ksp->rx_offload_octets.value.ull = port_stats.rx_offload_octets;
	vxge_ksp->rx_vld_mcast_frms.value.ull = port_stats.rx_vld_mcast_frms;
	vxge_ksp->rx_vld_bcast_frms.value.ull = port_stats.rx_vld_bcast_frms;
	vxge_ksp->rx_accepted_ucast_frms.value.ull =
	    port_stats.rx_accepted_ucast_frms;
	vxge_ksp->rx_accepted_nucast_frms.value.ull =
	    port_stats.rx_accepted_nucast_frms;
	vxge_ksp->rx_tagged_frms.value.ull = port_stats.rx_tagged_frms;
	vxge_ksp->rx_long_frms.value.ull   = port_stats.rx_long_frms;
	vxge_ksp->rx_usized_frms.value.ull = port_stats.rx_usized_frms;
	vxge_ksp->rx_osized_frms.value.ull = port_stats.rx_osized_frms;
	vxge_ksp->rx_frag_frms.value.ull   = port_stats.rx_frag_frms;
	vxge_ksp->rx_jabber_frms.value.ull = port_stats.rx_jabber_frms;
	vxge_ksp->rx_ttl_64_frms.value.ull = port_stats.rx_ttl_64_frms;
	vxge_ksp->rx_ttl_65_127_frms.value.ull = port_stats.rx_ttl_65_127_frms;
	vxge_ksp->rx_ttl_128_255_frms.value.ull =
	    port_stats.rx_ttl_128_255_frms;
	vxge_ksp->rx_ttl_256_511_frms.value.ull =
	    port_stats.rx_ttl_256_511_frms;
	vxge_ksp->rx_ttl_512_1023_frms.value.ull =
	    port_stats.rx_ttl_512_1023_frms;
	vxge_ksp->rx_ttl_1024_1518_frms.value.ull =
	    port_stats.rx_ttl_1024_1518_frms;
	vxge_ksp->rx_ttl_1519_4095_frms.value.ull =
	    port_stats.rx_ttl_1519_4095_frms;
	vxge_ksp->rx_ttl_4096_8191_frms.value.ull =
	    port_stats.rx_ttl_4096_8191_frms;
	vxge_ksp->rx_ttl_8192_max_frms.value.ull =
	    port_stats.rx_ttl_8192_max_frms;
	vxge_ksp->rx_ttl_gt_max_frms.value.ull = port_stats.rx_ttl_gt_max_frms;
	vxge_ksp->rx_ip.value.ull = port_stats.rx_ip;
	vxge_ksp->rx_accepted_ip.value.ull = port_stats.rx_accepted_ip;
	vxge_ksp->rx_ip_octets.value.ull = port_stats.rx_ip_octets;
	vxge_ksp->rx_err_ip.value.ull = port_stats.rx_err_ip;
	vxge_ksp->rx_icmp.value.ull = port_stats.rx_icmp;
	vxge_ksp->rx_tcp.value.ull = port_stats.rx_tcp;
	vxge_ksp->rx_udp.value.ull = port_stats.rx_udp;
	vxge_ksp->rx_err_tcp.value.ull = port_stats.rx_err_tcp;
	vxge_ksp->rx_pause_count.value.ull = port_stats.rx_pause_count;
	vxge_ksp->rx_pause_ctrl_frms.value.ull = port_stats.rx_pause_ctrl_frms;
	vxge_ksp->rx_unsup_ctrl_frms.value.ull = port_stats.rx_unsup_ctrl_frms;
	vxge_ksp->rx_fcs_err_frms.value.ull = port_stats.rx_fcs_err_frms;
	vxge_ksp->rx_in_rng_len_err_frms.value.ull =
	    port_stats.rx_in_rng_len_err_frms;
	vxge_ksp->rx_out_rng_len_err_frms.value.ull =
	    port_stats.rx_out_rng_len_err_frms;
	vxge_ksp->rx_drop_frms.value.ull = port_stats.rx_drop_frms;
	vxge_ksp->rx_discarded_frms.value.ull = port_stats.rx_discarded_frms;
	vxge_ksp->rx_drop_ip.value.ull = port_stats.rx_drop_ip;
	vxge_ksp->rx_drop_udp.value.ull = port_stats.rx_drop_udp;
	vxge_ksp->rx_lacpdu_frms.value.ull = port_stats.rx_lacpdu_frms;
	vxge_ksp->rx_marker_pdu_frms.value.ull = port_stats.rx_marker_pdu_frms;
	vxge_ksp->rx_marker_resp_pdu_frms.value.ull =
	    port_stats.rx_marker_resp_pdu_frms;
	vxge_ksp->rx_unknown_pdu_frms.value.ull =
	    port_stats.rx_unknown_pdu_frms;
	vxge_ksp->rx_illegal_pdu_frms.value.ull =
	    port_stats.rx_illegal_pdu_frms;
	vxge_ksp->rx_fcs_discard.value.ull	 = port_stats.rx_fcs_discard;
	vxge_ksp->rx_len_discard.value.ull	 = port_stats.rx_len_discard;
	vxge_ksp->rx_switch_discard.value.ull  = port_stats.rx_switch_discard;
	vxge_ksp->rx_l2_mgmt_discard.value.ull = port_stats.rx_l2_mgmt_discard;
	vxge_ksp->rx_rpa_discard.value.ull	 = port_stats.rx_rpa_discard;
	vxge_ksp->rx_trash_discard.value.ull   = port_stats.rx_trash_discard;
	vxge_ksp->rx_rts_discard.value.ull	 = port_stats.rx_rts_discard;
	vxge_ksp->rx_red_discard.value.ull	 = port_stats.rx_red_discard;
	vxge_ksp->rx_buff_full_discard.value.ull =
	    port_stats.rx_buff_full_discard;
	vxge_ksp->rx_xgmii_data_err_cnt.value.ull =
	    port_stats.rx_xgmii_data_err_cnt;
	vxge_ksp->rx_xgmii_ctrl_err_cnt.value.ull =
	    port_stats.rx_xgmii_ctrl_err_cnt;
	vxge_ksp->rx_xgmii_err_sym.value.ull = port_stats.rx_xgmii_err_sym;
	vxge_ksp->rx_xgmii_char1_match.value.ull =
	    port_stats.rx_xgmii_char1_match;
	vxge_ksp->rx_xgmii_char2_match.value.ull =
	    port_stats.rx_xgmii_char2_match;
	vxge_ksp->rx_xgmii_column1_match.value.ull =
	    port_stats.rx_xgmii_column1_match;
	vxge_ksp->rx_xgmii_column2_match.value.ull =
	    port_stats.rx_xgmii_column2_match;
	vxge_ksp->rx_local_fault.value.ull  = port_stats.rx_local_fault;
	vxge_ksp->rx_remote_fault.value.ull = port_stats.rx_remote_fault;
	vxge_ksp->rx_jettison.value.ull	 = port_stats.rx_jettison;

	vxge_ksp->tx_ttl_frms.value.ull	  = port_stats.tx_ttl_frms;
	vxge_ksp->tx_ttl_octets.value.ull	= port_stats.tx_ttl_octets;
	vxge_ksp->tx_data_octets.value.ull   = port_stats.tx_data_octets;
	vxge_ksp->tx_mcast_frms.value.ull	= port_stats.tx_mcast_frms;
	vxge_ksp->tx_bcast_frms.value.ull	= port_stats.tx_bcast_frms;
	vxge_ksp->tx_ucast_frms.value.ull	= port_stats.tx_ucast_frms;
	vxge_ksp->tx_tagged_frms.value.ull   = port_stats.tx_tagged_frms;
	vxge_ksp->tx_vld_ip.value.ull	= port_stats.tx_vld_ip;
	vxge_ksp->tx_vld_ip_octets.value.ull = port_stats.tx_vld_ip_octets;
	vxge_ksp->tx_icmp.value.ull	  = port_stats.tx_icmp;
	vxge_ksp->tx_tcp.value.ull	   = port_stats.tx_tcp;
	vxge_ksp->tx_rst_tcp.value.ull	   = port_stats.tx_rst_tcp;
	vxge_ksp->tx_udp.value.ull	   = port_stats.tx_udp;
	vxge_ksp->tx_unknown_protocol.value.ull =
	    port_stats.tx_unknown_protocol;
	vxge_ksp->tx_parse_error.value.ull	 = port_stats.tx_parse_error;
	vxge_ksp->tx_pause_ctrl_frms.value.ull = port_stats.tx_pause_ctrl_frms;
	vxge_ksp->tx_lacpdu_frms.value.ul	  = port_stats.tx_lacpdu_frms;
	vxge_ksp->tx_marker_pdu_frms.value.ul  = port_stats.tx_marker_pdu_frms;
	vxge_ksp->tx_marker_resp_pdu_frms.value.ul =
	    port_stats.tx_marker_resp_pdu_frms;
	vxge_ksp->tx_drop_ip.value.ul = port_stats.tx_drop_ip;
	vxge_ksp->tx_xgmii_char1_match.value.ul =
	    port_stats.tx_xgmii_char1_match;
	vxge_ksp->tx_xgmii_char2_match.value.ul =
	    port_stats.tx_xgmii_char2_match;
	vxge_ksp->tx_xgmii_column1_match.value.ul =
	    port_stats.tx_xgmii_column1_match;
	vxge_ksp->tx_xgmii_column2_match.value.ul =
	    port_stats.tx_xgmii_column2_match;
	vxge_ksp->tx_drop_frms.value.ul	= port_stats.tx_drop_frms;
	vxge_ksp->tx_any_err_frms.value.ul = port_stats.tx_any_err_frms;

	if (vxge_check_acc_handle(hldev->regh0) != DDI_FM_OK)
		ddi_fm_service_impact(vdev->dev_info, DDI_SERVICE_UNAFFECTED);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_UPDATE_PORT_KSTATS EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (0);
}

static int
vxge_update_vpath_kstats(kstat_t *ksp, int rw)
{
	vxge_hal_status_e status = VXGE_HAL_OK;
	vxge_kstat_vpath_t *vxge_ksp;
	vxge_hal_vpath_stats_hw_info_t vpath_hw_stats;
	vxge_hal_vpath_stats_sw_info_t vpath_sw_stats;
	u16  j;

	vxge_hal_device_t *hldev;
	vxge_vpath_t *vpath = (vxge_vpath_t *)ksp->ks_private;
	vxgedev_t *vdev = (vxgedev_t *)vpath->vdev;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_UPDATE_VPATH_KSTATS ENTRY ",
	    VXGE_IFNAME, vdev->instance);
	if (!vdev->is_initialized) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: device is not initialized...",
		    VXGE_IFNAME, vdev->instance);
		return (status);
	}

	hldev = (vxge_hal_device_t *)vdev->devh;
	j = vpath->kstat_id;

	vxge_ksp = (vxge_kstat_vpath_t *)ksp->ks_data;

	if (rw == KSTAT_WRITE) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: Write Support is not added\n",
		    VXGE_IFNAME, vdev->instance);
		return (EACCES);
	}

	if (vxge_hal_vpath_hw_stats_enable(vdev->vpaths[j].handle) !=
	    VXGE_HAL_OK) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: vxge_hal_vpath_hw_stats_enable failed \n",
		    VXGE_IFNAME, vdev->instance);
		return (EAGAIN);
	}

	status = vxge_hal_vpath_hw_stats_get(vdev->vpaths[j].handle,
	    &vpath_hw_stats);
	if (status != VXGE_HAL_OK) {
		/*EMPTY*/
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d : %s : %d Failure in getting Hardware Stats ",
		    VXGE_IFNAME, vdev->instance, __func__, __LINE__);
	}

	status = vxge_hal_vpath_sw_stats_get(vdev->vpaths[j].handle,
	    &vpath_sw_stats);
	if (status != VXGE_HAL_OK) {
		/*EMPTY*/
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: %s : %d Failure in getting Software Stats ",
		    VXGE_IFNAME, vdev->instance, __func__, __LINE__);
	}

	vxge_ksp->ini_num_mwr_sent.value.ul = vpath_hw_stats.ini_num_mwr_sent;
	vxge_ksp->ini_num_mrd_sent.value.ul = vpath_hw_stats.ini_num_mrd_sent;
	vxge_ksp->ini_num_cpl_rcvd.value.ul = vpath_hw_stats.ini_num_cpl_rcvd;
	vxge_ksp->ini_num_mwr_byte_sent.value.ull =
	    vpath_hw_stats.ini_num_mwr_byte_sent;
	vxge_ksp->ini_num_cpl_byte_rcvd.value.ull =
	    vpath_hw_stats.ini_num_cpl_byte_rcvd;
	vxge_ksp->wrcrdtarb_xoff.value.ul = vpath_hw_stats.wrcrdtarb_xoff;
	vxge_ksp->rdcrdtarb_xoff.value.ul = vpath_hw_stats.rdcrdtarb_xoff;
	vxge_ksp->vpath_genstats_count0.value.ul =
	    vpath_hw_stats.vpath_genstats_count0;
	vxge_ksp->vpath_genstats_count1.value.ul =
	    vpath_hw_stats.vpath_genstats_count1;
	vxge_ksp->vpath_genstats_count2.value.ul =
	    vpath_hw_stats.vpath_genstats_count2;
	vxge_ksp->vpath_genstats_count3.value.ul =
	    vpath_hw_stats.vpath_genstats_count3;
	vxge_ksp->vpath_genstats_count4.value.ul =
	    vpath_hw_stats.vpath_genstats_count4;
	vxge_ksp->vpath_gennstats_count5.value.ul =
	    vpath_hw_stats.vpath_genstats_count5;

	vxge_ksp->tx_ttl_eth_frms.value.ull =
	    vpath_hw_stats.tx_stats.tx_ttl_eth_frms;
	vxge_ksp->tx_ttl_eth_octets.value.ull =
	    vpath_hw_stats.tx_stats.tx_ttl_eth_octets;
	vxge_ksp->tx_data_octets.value.ull =
	    vpath_hw_stats.tx_stats.tx_data_octets;
	vxge_ksp->tx_mcast_frms.value.ull  =
	    vpath_hw_stats.tx_stats.tx_mcast_frms;
	vxge_ksp->tx_bcast_frms.value.ull  =
	    vpath_hw_stats.tx_stats.tx_bcast_frms;
	vxge_ksp->tx_ucast_frms.value.ull  =
	    vpath_hw_stats.tx_stats.tx_ucast_frms;
	vxge_ksp->tx_tagged_frms.value.ull =
	    vpath_hw_stats.tx_stats.tx_tagged_frms;
	vxge_ksp->tx_vld_ip.value.ull = vpath_hw_stats.tx_stats.tx_vld_ip;
	vxge_ksp->tx_vld_ip_octets.value.ull =
	    vpath_hw_stats.tx_stats.tx_vld_ip_octets;
	vxge_ksp->tx_icmp.value.ull = vpath_hw_stats.tx_stats.tx_icmp;
	vxge_ksp->tx_tcp.value.ull = vpath_hw_stats.tx_stats.tx_tcp;
	vxge_ksp->tx_rst_tcp.value.ull = vpath_hw_stats.tx_stats.tx_rst_tcp;
	vxge_ksp->tx_udp.value.ull = vpath_hw_stats.tx_stats.tx_udp;
	vxge_ksp->tx_lost_ip.value.ul = vpath_hw_stats.tx_stats.tx_lost_ip;
	vxge_ksp->tx_unknown_protocol.value.ul =
	    vpath_hw_stats.tx_stats.tx_unknown_protocol;
	vxge_ksp->tx_parse_error.value.ul  =
	    vpath_hw_stats.tx_stats.tx_parse_error;
	vxge_ksp->tx_tcp_offload.value.ull =
	    vpath_hw_stats.tx_stats.tx_tcp_offload;
	vxge_ksp->tx_retx_tcp_offload.value.ull =
	    vpath_hw_stats.tx_stats.tx_retx_tcp_offload;
	vxge_ksp->tx_lost_ip_offload.value.ull =
	    vpath_hw_stats.tx_stats.tx_lost_ip_offload;
	vxge_ksp->rx_ttl_eth_frms.value.ull =
	    vpath_hw_stats.rx_stats.rx_ttl_eth_frms;
	vxge_ksp->rx_vld_frms.value.ull =  vpath_hw_stats.rx_stats.rx_vld_frms;
	vxge_ksp->rx_offload_frms.value.ull =
	    vpath_hw_stats.rx_stats.rx_offload_frms;
	vxge_ksp->rx_ttl_eth_octets.value.ull =
	    vpath_hw_stats.rx_stats.rx_ttl_eth_octets;
	vxge_ksp->rx_data_octets.value.ull =
	    vpath_hw_stats.rx_stats.rx_data_octets;
	vxge_ksp->rx_offload_octets.value.ull =
	    vpath_hw_stats.rx_stats.rx_offload_octets;
	vxge_ksp->rx_vld_mcast_frms.value.ull =
	    vpath_hw_stats.rx_stats.rx_vld_mcast_frms;
	vxge_ksp->rx_vld_bcast_frms.value.ull =
	    vpath_hw_stats.rx_stats.rx_vld_bcast_frms;
	vxge_ksp->rx_accepted_ucast_frms.value.ull =
	    vpath_hw_stats.rx_stats.rx_accepted_ucast_frms;
	vxge_ksp->rx_accepted_nucast_frms.value.ull =
	    vpath_hw_stats.rx_stats.rx_accepted_nucast_frms;
	vxge_ksp->rx_tagged_frms.value.ull =
	    vpath_hw_stats.rx_stats.rx_tagged_frms;
	vxge_ksp->rx_long_frms.value.ull   =
	    vpath_hw_stats.rx_stats.rx_long_frms;
	vxge_ksp->rx_usized_frms.value.ull =
	    vpath_hw_stats.rx_stats.rx_usized_frms;
	vxge_ksp->rx_osized_frms.value.ull =
	    vpath_hw_stats.rx_stats.rx_osized_frms;
	vxge_ksp->rx_frag_frms.value.ull   =
	    vpath_hw_stats.rx_stats.rx_frag_frms;
	vxge_ksp->rx_jabber_frms.value.ull =
	    vpath_hw_stats.rx_stats.rx_jabber_frms;
	vxge_ksp->rx_ttl_64_frms.value.ull =
	    vpath_hw_stats.rx_stats.rx_ttl_64_frms;
	vxge_ksp->rx_ttl_65_127_frms.value.ull =
	    vpath_hw_stats.rx_stats.rx_ttl_65_127_frms;
	vxge_ksp->rx_ttl_128_255_frms.value.ull =
	    vpath_hw_stats.rx_stats.rx_ttl_128_255_frms;
	vxge_ksp->rx_ttl_256_511_frms.value.ull =
	    vpath_hw_stats.rx_stats.rx_ttl_256_511_frms;
	vxge_ksp->rx_ttl_512_1023_frms.value.ull =
	    vpath_hw_stats.rx_stats.rx_ttl_512_1023_frms;
	vxge_ksp->rx_ttl_1024_1518_frms.value.ull =
	    vpath_hw_stats.rx_stats.rx_ttl_1024_1518_frms;
	vxge_ksp->rx_ttl_1519_4095_frms.value.ull =
	    vpath_hw_stats.rx_stats.rx_ttl_1519_4095_frms;
	vxge_ksp->rx_ttl_4096_8191_frms.value.ull =
	    vpath_hw_stats.rx_stats.rx_ttl_4096_8191_frms;
	vxge_ksp->rx_ttl_8192_max_frms.value.ull =
	    vpath_hw_stats.rx_stats.rx_ttl_8192_max_frms;
	vxge_ksp->rx_ttl_gt_max_frms.value.ull =
	    vpath_hw_stats.rx_stats.rx_ttl_gt_max_frms;
	vxge_ksp->rx_ip.value.ull =  vpath_hw_stats.rx_stats.rx_ip;
	vxge_ksp->rx_accepted_ip.value.ull =
	    vpath_hw_stats.rx_stats.rx_accepted_ip;
	vxge_ksp->rx_ip_octets.value.ull = vpath_hw_stats.rx_stats.rx_ip_octets;
	vxge_ksp->rx_err_ip.value.ull	= vpath_hw_stats.rx_stats.rx_err_ip;
	vxge_ksp->rx_icmp.value.ull	  = vpath_hw_stats.rx_stats.rx_icmp;
	vxge_ksp->rx_tcp.value.ull	   = vpath_hw_stats.rx_stats.rx_tcp;
	vxge_ksp->rx_udp.value.ull	   = vpath_hw_stats.rx_stats.rx_udp;
	vxge_ksp->rx_err_tcp.value.ull   = vpath_hw_stats.rx_stats.rx_err_tcp;
	vxge_ksp->rx_lost_frms.value.ull = vpath_hw_stats.rx_stats.rx_lost_frms;
	vxge_ksp->rx_lost_ip.value.ull   = vpath_hw_stats.rx_stats.rx_lost_ip;
	vxge_ksp->rx_lost_ip_offload.value.ull =
	    vpath_hw_stats.rx_stats.rx_lost_ip_offload;
	vxge_ksp->rx_queue_full_discard.value.ull =
	    vpath_hw_stats.rx_stats.rx_queue_full_discard;
	vxge_ksp->rx_red_discard.value.ull   =
	    vpath_hw_stats.rx_stats.rx_red_discard;
	vxge_ksp->rx_sleep_discard.value.ull =
	    vpath_hw_stats.rx_stats.rx_sleep_discard;
	vxge_ksp->rx_mpa_ok_frms.value.ull   =
	    vpath_hw_stats.rx_stats.rx_mpa_ok_frms;

	vxge_ksp->prog_event_vnum1.value.ull = vpath_hw_stats.prog_event_vnum1;
	vxge_ksp->prog_event_vnum0.value.ull = vpath_hw_stats.prog_event_vnum0;
	vxge_ksp->prog_event_vnum3.value.ull = vpath_hw_stats.prog_event_vnum3;
	vxge_ksp->prog_event_vnum2.value.ull = vpath_hw_stats.prog_event_vnum2;
	vxge_ksp->rx_multi_cast_frame_discard.value.ull =
	    vpath_hw_stats.rx_multi_cast_frame_discard;
	vxge_ksp->rx_frm_transferred.value.ull =
	    vpath_hw_stats.rx_frm_transferred;
	vxge_ksp->rxd_returned.value.ull = vpath_hw_stats.rxd_returned;
	vxge_ksp->rx_mpa_len_fail_frms.value.ull =
	    vpath_hw_stats.rx_mpa_len_fail_frms;
	vxge_ksp->rx_mpa_mrk_fail_frms.value.ull =
	    vpath_hw_stats.rx_mpa_mrk_fail_frms;
	vxge_ksp->rx_mpa_crc_fail_frms.value.ull =
	    vpath_hw_stats.rx_mpa_crc_fail_frms;
	vxge_ksp->rx_permitted_frms.value.ull =
	    vpath_hw_stats.rx_permitted_frms;
	vxge_ksp->rx_vp_reset_discarded_frms.value.ull =
	    vpath_hw_stats.rx_vp_reset_discarded_frms;
	vxge_ksp->rx_wol_frms.value.ull =
	    vpath_hw_stats.rx_wol_frms;
	vxge_ksp->tx_vp_reset_discarded_frms.value.ull =
	    vpath_hw_stats.tx_vp_reset_discarded_frms;

	vxge_ksp->no_nces.value.ull	 = vpath_sw_stats.obj_counts.no_nces;
	vxge_ksp->no_sqs.value.ull	  = vpath_sw_stats.obj_counts.no_sqs;
	vxge_ksp->no_srqs.value.ull	 = vpath_sw_stats.obj_counts.no_srqs;
	vxge_ksp->no_cqrqs.value.ull	= vpath_sw_stats.obj_counts.no_cqrqs;
	vxge_ksp->no_sessions.value.ull = vpath_sw_stats.obj_counts.no_sessions;

	vxge_ksp->ring_full_cnt.value.ull =
	    vpath_sw_stats.ring_stats.common_stats.full_cnt;
	vxge_ksp->ring_usage_cnt.value.ull =
	    vpath_sw_stats.ring_stats.common_stats.usage_cnt;
	vxge_ksp->ring_usage_max.value.ull =
	    vpath_sw_stats.ring_stats.common_stats.usage_max;
	vxge_ksp->ring_avg_compl_per_intr_cnt.value.ull =
	    vpath_sw_stats.ring_stats.common_stats.avg_compl_per_intr_cnt;
	vxge_ksp->ring_total_compl_cnt.value.ull =
	    vpath_sw_stats.ring_stats.common_stats.total_compl_cnt;
	vxge_ksp->rxd_t_code_err_cnt_0.value.ull =
	    vpath_sw_stats.ring_stats.rxd_t_code_err_cnt[0];
	vxge_ksp->rxd_t_code_err_cnt_1.value.ull =
	    vpath_sw_stats.ring_stats.rxd_t_code_err_cnt[1];
	vxge_ksp->rxd_t_code_err_cnt_2.value.ull =
	    vpath_sw_stats.ring_stats.rxd_t_code_err_cnt[2];
	vxge_ksp->rxd_t_code_err_cnt_3.value.ull =
	    vpath_sw_stats.ring_stats.rxd_t_code_err_cnt[3];
	vxge_ksp->rxd_t_code_err_cnt_4.value.ull =
	    vpath_sw_stats.ring_stats.rxd_t_code_err_cnt[4];
	vxge_ksp->rxd_t_code_err_cnt_5.value.ull =
	    vpath_sw_stats.ring_stats.rxd_t_code_err_cnt[5];
	vxge_ksp->rxd_t_code_err_cnt_6.value.ull =
	    vpath_sw_stats.ring_stats.rxd_t_code_err_cnt[6];
	vxge_ksp->rxd_t_code_err_cnt_7.value.ull =
	    vpath_sw_stats.ring_stats.rxd_t_code_err_cnt[7];
	vxge_ksp->rxd_t_code_err_cnt_8.value.ull =
	    vpath_sw_stats.ring_stats.rxd_t_code_err_cnt[8];
	vxge_ksp->rxd_t_code_err_cnt_9.value.ull =
	    vpath_sw_stats.ring_stats.rxd_t_code_err_cnt[9];
	vxge_ksp->rxd_t_code_err_cnt_10.value.ull =
	    vpath_sw_stats.ring_stats.rxd_t_code_err_cnt[10];
	vxge_ksp->rxd_t_code_err_cnt_11.value.ull =
	    vpath_sw_stats.ring_stats.rxd_t_code_err_cnt[11];
	vxge_ksp->rxd_t_code_err_cnt_12.value.ull =
	    vpath_sw_stats.ring_stats.rxd_t_code_err_cnt[12];
	vxge_ksp->rxd_t_code_err_cnt_13.value.ull =
	    vpath_sw_stats.ring_stats.rxd_t_code_err_cnt[13];
	vxge_ksp->rxd_t_code_err_cnt_14.value.ull =
	    vpath_sw_stats.ring_stats.rxd_t_code_err_cnt[14];
	vxge_ksp->rxd_t_code_err_cnt_15.value.ull =
	    vpath_sw_stats.ring_stats.rxd_t_code_err_cnt[15];


	vxge_ksp->unknown_alarms.value.ull =
	    vpath_sw_stats.error_stats.unknown_alarms;
	vxge_ksp->network_sustained_fault.value.ull =
	    vpath_sw_stats.error_stats.network_sustained_fault;
	vxge_ksp->network_sustained_ok.value.ull =
	    vpath_sw_stats.error_stats.network_sustained_ok;
	vxge_ksp->kdfcctl_fifo0_overwrite.value.ull =
	    vpath_sw_stats.error_stats.kdfcctl_fifo0_overwrite;
	vxge_ksp->kdfcctl_fifo0_poison.value.ull =
	    vpath_sw_stats.error_stats.kdfcctl_fifo0_poison;
	vxge_ksp->kdfcctl_fifo0_dma_error.value.ull =
	    vpath_sw_stats.error_stats.kdfcctl_fifo0_dma_error;
	vxge_ksp->kdfcctl_fifo1_overwrite.value.ull =
	    vpath_sw_stats.error_stats.kdfcctl_fifo1_overwrite;
	vxge_ksp->kdfcctl_fifo1_poison.value.ull =
	    vpath_sw_stats.error_stats.kdfcctl_fifo1_poison;
	vxge_ksp->kdfcctl_fifo1_dma_error.value.ull =
	    vpath_sw_stats.error_stats.kdfcctl_fifo1_dma_error;
	vxge_ksp->kdfcctl_fifo2_overwrite.value.ull =
	    vpath_sw_stats.error_stats.kdfcctl_fifo2_overwrite;
	vxge_ksp->kdfcctl_fifo2_poison.value.ull =
	    vpath_sw_stats.error_stats.kdfcctl_fifo2_poison;
	vxge_ksp->kdfcctl_fifo2_dma_error.value.ull =
	    vpath_sw_stats.error_stats.kdfcctl_fifo2_dma_error;
	vxge_ksp->dblgen_fifo0_overflow.value.ull =
	    vpath_sw_stats.error_stats.dblgen_fifo0_overflow;
	vxge_ksp->dblgen_fifo1_overflow.value.ull =
	    vpath_sw_stats.error_stats.dblgen_fifo1_overflow;
	vxge_ksp->dblgen_fifo2_overflow.value.ull =
	    vpath_sw_stats.error_stats.dblgen_fifo2_overflow;
	vxge_ksp->statsb_pif_chain_error.value.ull =
	    vpath_sw_stats.error_stats.statsb_pif_chain_error;
	vxge_ksp->statsb_drop_timeout.value.ull =
	    vpath_sw_stats.error_stats.statsb_drop_timeout;
	vxge_ksp->target_illegal_access.value.ull =
	    vpath_sw_stats.error_stats.target_illegal_access;
	vxge_ksp->ini_serr_det.value.ull =
	    vpath_sw_stats.error_stats.ini_serr_det;
	vxge_ksp->pci_config_status_err.value.ull =
	    vpath_sw_stats.error_stats.pci_config_status_err;
	vxge_ksp->pci_config_uncor_err.value.ull =
	    vpath_sw_stats.error_stats.pci_config_uncor_err;
	vxge_ksp->pci_config_cor_err.value.ull =
	    vpath_sw_stats.error_stats.pci_config_cor_err;
	vxge_ksp->mrpcim_to_vpath_alarms.value.ull =
	    vpath_sw_stats.error_stats.mrpcim_to_vpath_alarms;
	vxge_ksp->srpcim_to_vpath_alarms.value.ull =
	    vpath_sw_stats.error_stats.srpcim_to_vpath_alarms;
	vxge_ksp->srpcim_msg_to_vpath.value.ull =
	    vpath_sw_stats.error_stats.srpcim_msg_to_vpath;

	vxge_ksp->prc_ring_bump_cnt.value.ull =
	    vpath_sw_stats.error_stats.prc_ring_bumps;
	vxge_ksp->prc_rxdcm_sc_err_cnt.value.ull =
	    vpath_sw_stats.error_stats.prc_rxdcm_sc_err;
	vxge_ksp->prc_rxdcm_sc_abort_cnt.value.ull =
	    vpath_sw_stats.error_stats.prc_rxdcm_sc_abort;
	vxge_ksp->prc_quanta_size_err_cnt.value.ull =
	    vpath_sw_stats.error_stats.prc_quanta_size_err;

	vxge_ksp->fifo_full_cnt.value.ull =
	    vpath_sw_stats.fifo_stats.common_stats.full_cnt;
	vxge_ksp->fifo_usage_cnt.value.ull =
	    vpath_sw_stats.fifo_stats.common_stats.usage_cnt;
	vxge_ksp->fifo_usage_max.value.ull =
	    vpath_sw_stats.fifo_stats.common_stats.usage_max;
	vxge_ksp->fifo_avg_compl_per_intr_cnt.value.ull =
	    vpath_sw_stats.fifo_stats.common_stats.avg_compl_per_intr_cnt;
	vxge_ksp->fifo_total_compl_cnt.value.ull =
	    vpath_sw_stats.fifo_stats.common_stats.total_compl_cnt;
	vxge_ksp->fifo_total_posts.value.ull =
	    vpath_sw_stats.fifo_stats.total_posts;
	vxge_ksp->fifo_total_buffers.value.ull =
	    vpath_sw_stats.fifo_stats.total_buffers;
	vxge_ksp->fifo_avg_buffers_per_post.value.ull =
	    vpath_sw_stats.fifo_stats.avg_buffers_per_post;

	vxge_ksp->fifo_copied_frags.value.ull =
	    vpath_sw_stats.fifo_stats.copied_frags;
	vxge_ksp->fifo_copied_buffers.value.ull =
	    vpath_sw_stats.fifo_stats.copied_buffers;
	vxge_ksp->fifo_avg_buffer_size.value.ull =
	    vpath_sw_stats.fifo_stats.avg_buffer_size;
	vxge_ksp->fifo_avg_post_size.value.ull =
	    vpath_sw_stats.fifo_stats.avg_post_size;
	vxge_ksp->fifo_total_posts_dang_dtrs.value.ull =
	    vpath_sw_stats.fifo_stats.total_posts_dang_dtrs;
	vxge_ksp->fifo_total_posts_dang_frags.value.ull =
	    vpath_sw_stats.fifo_stats.total_posts_dang_frags;
	vxge_ksp->txd_t_code_err_cnt_0.value.ull =
	    vpath_sw_stats.fifo_stats.txd_t_code_err_cnt[0];
	vxge_ksp->txd_t_code_err_cnt_1.value.ull =
	    vpath_sw_stats.fifo_stats.txd_t_code_err_cnt[1];
	vxge_ksp->txd_t_code_err_cnt_2.value.ull =
	    vpath_sw_stats.fifo_stats.txd_t_code_err_cnt[2];
	vxge_ksp->txd_t_code_err_cnt_3.value.ull =
	    vpath_sw_stats.fifo_stats.txd_t_code_err_cnt[3];
	vxge_ksp->txd_t_code_err_cnt_4.value.ull =
	    vpath_sw_stats.fifo_stats.txd_t_code_err_cnt[4];
	vxge_ksp->txd_t_code_err_cnt_5.value.ull =
	    vpath_sw_stats.fifo_stats.txd_t_code_err_cnt[5];
	vxge_ksp->txd_t_code_err_cnt_6.value.ull =
	    vpath_sw_stats.fifo_stats.txd_t_code_err_cnt[6];
	vxge_ksp->txd_t_code_err_cnt_7.value.ull =
	    vpath_sw_stats.fifo_stats.txd_t_code_err_cnt[7];
	vxge_ksp->txd_t_code_err_cnt_8.value.ull =
	    vpath_sw_stats.fifo_stats.txd_t_code_err_cnt[8];
	vxge_ksp->txd_t_code_err_cnt_9.value.ull =
	    vpath_sw_stats.fifo_stats.txd_t_code_err_cnt[9];
	vxge_ksp->txd_t_code_err_cnt_10.value.ull =
	    vpath_sw_stats.fifo_stats.txd_t_code_err_cnt[10];
	vxge_ksp->txd_t_code_err_cnt_11.value.ull =
	    vpath_sw_stats.fifo_stats.txd_t_code_err_cnt[11];
	vxge_ksp->txd_t_code_err_cnt_12.value.ull =
	    vpath_sw_stats.fifo_stats.txd_t_code_err_cnt[12];
	vxge_ksp->txd_t_code_err_cnt_13.value.ull =
	    vpath_sw_stats.fifo_stats.txd_t_code_err_cnt[13];
	vxge_ksp->txd_t_code_err_cnt_14.value.ull =
	    vpath_sw_stats.fifo_stats.txd_t_code_err_cnt[14];
	vxge_ksp->txd_t_code_err_cnt_15.value.ull =
	    vpath_sw_stats.fifo_stats.txd_t_code_err_cnt[15];
	vxge_ksp->total_frags.value.ull = vpath_sw_stats.fifo_stats.total_frags;
	vxge_ksp->copied_frags.value.ull =
	    vpath_sw_stats.fifo_stats.copied_frags;

	if (vxge_check_acc_handle(hldev->regh0) != DDI_FM_OK)
		ddi_fm_service_impact(vdev->dev_info, DDI_SERVICE_UNAFFECTED);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_UPDATE_VPATH_KSTATS EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (0);
}

static void
vxge_kstat_aggr_init(vxgedev_t *vdev)
{
	kstat_t *ksp[VXGE_HAL_WIRE_PORT_MAX_PORTS];
	vxge_kstat_aggr_t   *vxge_ksp;
	char vxge_str[24] = "vxge_kstat_aggr_port";
	char vxge_str1[25] = "";
	u16 j;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_KSTAT_AGGR_INIT ENTRY ",
	    VXGE_IFNAME, vdev->instance);
	for (j = 0; j < VXGE_HAL_WIRE_PORT_MAX_PORTS; j++) {
		vdev->aggr_port_no[j].port_id = j;
		(void) sprintf(vxge_str1, "%s %d", vxge_str, j);
		ksp[j] = kstat_create(VXGE_IFNAME,
		    ddi_get_instance(vdev->dev_info), vxge_str1,
		    "net", KSTAT_TYPE_NAMED,
		    (sizeof (vxge_kstat_aggr_t) / sizeof (kstat_named_t)), 0);
		if (ksp[j] == NULL) {
			vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
			    "%s%d: Failed to create kstat for aggr \n",
			    VXGE_IFNAME, vdev->instance);
			return;
		}

		vxge_ksp = (vxge_kstat_aggr_t *)ksp[j]->ks_data;

		vdev->vxge_kstat_aggr_ksp[j] = ksp[j];   /* Fill in the ksp */

		/* Function to provide kernel stat update on demand */
		ksp[j]->ks_update = vxge_update_aggr_kstats;

		/* Pointer into provider's raw statistics */
		vdev->aggr_port_no[j].vdev = vdev;
		ksp[j]->ks_private = (void *)&vdev->aggr_port_no[j];

		/* Initialize all the statistics */
		kstat_named_init(&vxge_ksp->rx_frms,
		    "rx_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_data_octets,
		    "rx_data_octets", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_mcast_frms,
		    "rx_mcast_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_bcast_frms,
		    "rx_bcast_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_discarded_frms,
		    "rx_discarded_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_errored_frms,
		    "rx_errored_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_unknown_slow_proto_frms,
		    "rx_unknown_slow_proto_frms", KSTAT_DATA_UINT64);

		kstat_named_init(&vxge_ksp->tx_frms,
		    "tx_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->tx_data_octets,
		    "tx_data_octets", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->tx_mcast_frms,
		    "tx_mcast_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->tx_bcast_frms,
		    "tx_bcast_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->tx_discarded_frms,
		    "tx_discarded_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->tx_errored_frms,
		    "tx_errored_frms", KSTAT_DATA_UINT64);
		kstat_install(ksp[j]);
		}

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_KSTAT_AGGR_INIT EXIT ",
	    VXGE_IFNAME, vdev->instance);
}

static void
vxge_kstat_port_init(vxgedev_t *vdev)
{
	kstat_t *ksp[VXGE_HAL_WIRE_PORT_MAX_PORTS];
	vxge_kstat_port_t   *vxge_ksp;
	char vxge_str[20] = "vxge_kstat_port";
	char vxge_str1[21] = "";
	u16 j;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_KSTAT_PORT_INIT ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	for (j = 0; j < VXGE_HAL_WIRE_PORT_MAX_PORTS; j++) {
		vdev->port_no[j].port_id = j;
		(void) sprintf(vxge_str1, "%s %d", vxge_str, j);
		ksp[j] = kstat_create(VXGE_IFNAME,
		    ddi_get_instance(vdev->dev_info), vxge_str1,
		    "net", KSTAT_TYPE_NAMED,
		    (sizeof (vxge_kstat_port_t) / sizeof (kstat_named_t)), 0);
		if (ksp[j] == NULL) {
			vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
			    "%s%d: Failed to create kstat for port \n",
			    VXGE_IFNAME, vdev->instance);
			return;
		}

		vxge_ksp = (vxge_kstat_port_t *)ksp[j]->ks_data;

		/* Fill in the ksp */
		vdev->vxge_kstat_port_ksp[j] = ksp[j];

		/* Function to provide kernel stat update on demand */
		ksp[j]->ks_update = vxge_update_port_kstats;

		/* Pointer into provider's raw statistics */
		vdev->port_no[j].vdev = vdev;
		ksp[j]->ks_private = (void *)&vdev->port_no[j];

		/* Initialize all the statistics */
		kstat_named_init(&vxge_ksp->rx_ttl_frms,
		    "rx_ttl_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_vld_frms,
		    "rx_vld_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_offload_frms,
		    "rx_offload_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_ttl_octets,
		    "rx_ttl_octets", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_data_octets,
		    "rx_data_octets", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_offload_octets,
		    "rx_offload_octets", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_vld_mcast_frms,
		    "rx_vld_mcast_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_vld_bcast_frms,
		    "rx_vld_bcast_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_accepted_ucast_frms,
		    "rx_accepted_ucast_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_accepted_nucast_frms,
		    "rx_accepted_nucast_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_tagged_frms,
		    "rx_tagged_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_long_frms,
		    "rx_long_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_usized_frms,
		    "rx_usized_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_osized_frms,
		    "rx_osized_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_frag_frms,
		    "rx_frag_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_jabber_frms,
		    "rx_jabber_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_ttl_64_frms,
		    "rx_ttl_64_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_ttl_65_127_frms,
		    "rx_ttl_65_127_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_ttl_128_255_frms,
		    "rx_ttl_128_255_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_ttl_256_511_frms,
		    "rx_ttl_256_511_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_ttl_512_1023_frms,
		    "rx_ttl_512_1023_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_ttl_1024_1518_frms,
		    "rx_ttl_1024_1518_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_ttl_1519_4095_frms,
		    "rx_ttl_1519_4095_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_ttl_4096_8191_frms,
		    "rx_ttl_4096_8191_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_ttl_8192_max_frms,
		    "rx_ttl_8192_max_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_ttl_gt_max_frms,
		    "rx_ttl_gt_max_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_ip,
		    "rx_ip", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_accepted_ip,
		    "rx_accepted_ip", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_ip_octets,
		    "rx_ip_octets", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_err_ip,
		    "rx_err_ip", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_icmp,
		    "rx_icmp", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_tcp,
		    "rx_tcp", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_udp,
		    "rx_udp", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_err_tcp,
		    "rx_err_tcp", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_pause_count,
		    "rx_pause_count", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_pause_ctrl_frms,
		    "rx_pause_ctrl_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_unsup_ctrl_frms,
		    "rx_unsup_ctrl_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_fcs_err_frms,
		    "rx_fcs_err_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_in_rng_len_err_frms,
		    "rx_in_rng_len_err_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_out_rng_len_err_frms,
		    "rx_out_rng_len_err_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_drop_frms,
		    "rx_drop_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_discarded_frms,
		    "rx_discarded_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_drop_ip,
		    "rx_drop_ip", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_drop_udp,
		    "rx_drop_udp", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_lacpdu_frms,
		    "rx_lacpdu_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_marker_pdu_frms,
		    "rx_marker_pdu_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_marker_resp_pdu_frms,
		    "rx_marker_resp_pdu_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_unknown_pdu_frms,
		    "rx_unknown_pdu_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_illegal_pdu_frms,
		    "rx_illegal_pdu_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_fcs_discard,
		    "rx_fcs_discard", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_len_discard,
		    "rx_len_discard", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_switch_discard,
		    "rx_switch_discard", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_l2_mgmt_discard,
		    "rx_l2_mgmt_discard", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_rpa_discard,
		    "rx_rpa_discard", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_trash_discard,
		    "rx_trash_discard", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_rts_discard,
		    "rx_rts_discard", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_red_discard,
		    "rx_red_discard", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_buff_full_discard,
		    "rx_buff_full_discard", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_xgmii_data_err_cnt,
		    "rx_xgmii_data_err_cnt", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_xgmii_ctrl_err_cnt,
		    "rx_xgmii_ctrl_err_cnt", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_xgmii_err_sym,
		    "rx_xgmii_err_sym", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_xgmii_char1_match,
		    "rx_xgmii_char1_match", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_xgmii_char2_match,
		    "rx_xgmii_char2_match", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_xgmii_column1_match,
		    "rx_xgmii_column1_match", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_xgmii_column2_match,
		    "rx_xgmii_column2_match", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_local_fault,
		    "rx_local_fault", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_remote_fault,
		    "rx_remote_fault", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_jettison,
		    "rx_jettison", KSTAT_DATA_UINT64);

		kstat_named_init(&vxge_ksp->tx_ttl_frms,
		    "tx_ttl_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->tx_ttl_octets,
		    "tx_ttl_octets", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->tx_data_octets,
		    "tx_data_octets", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->tx_mcast_frms,
		    "tx_mcast_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->tx_bcast_frms,
		    "tx_bcast_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->tx_ucast_frms,
		    "tx_ucast_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->tx_tagged_frms,
		    "tx_tagged_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->tx_vld_ip,
		    "tx_vld_ip", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->tx_vld_ip_octets,
		    "tx_vld_ip_octets", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->tx_icmp,
		    "tx_icmp", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->tx_tcp,
		    "tx_tcp", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->tx_rst_tcp,
		    "tx_rst_tcp", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->tx_udp,
		    "tx_udp", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->tx_unknown_protocol,
		    "tx_unknown_protocol", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->tx_parse_error,
		    "tx_parse_error", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->tx_pause_ctrl_frms,
		    "tx_pause_ctrl_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->tx_lacpdu_frms,
		    "tx_lacpdu_frms", KSTAT_DATA_UINT32);
		kstat_named_init(&vxge_ksp->tx_marker_pdu_frms,
		    "tx_marker_pdu_frms", KSTAT_DATA_UINT32);
		kstat_named_init(&vxge_ksp->tx_marker_resp_pdu_frms,
		    "tx_marker_resp_pdu_frms", KSTAT_DATA_UINT32);
		kstat_named_init(&vxge_ksp->tx_drop_ip,
		    "tx_drop_ip", KSTAT_DATA_UINT32);
		kstat_named_init(&vxge_ksp->tx_xgmii_char1_match,
		    "tx_xgmii_char1_match", KSTAT_DATA_UINT32);
		kstat_named_init(&vxge_ksp->tx_xgmii_char2_match,
		    "tx_xgmii_char2_match", KSTAT_DATA_UINT32);
		kstat_named_init(&vxge_ksp->tx_xgmii_column1_match,
		    "tx_xgmii_column1_match", KSTAT_DATA_UINT32);
		kstat_named_init(&vxge_ksp->tx_xgmii_column2_match,
		    "tx_xgmii_column2_match", KSTAT_DATA_UINT32);
		kstat_named_init(&vxge_ksp->tx_drop_frms,
		    "tx_drop_frms", KSTAT_DATA_UINT32);
		kstat_named_init(&vxge_ksp->tx_any_err_frms,
		    "tx_any_err_frms", KSTAT_DATA_UINT32);
		kstat_install(ksp[j]);
		}

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_KSTAT_PORT_INIT EXIT ",
	    VXGE_IFNAME, vdev->instance);
}

static void
vxge_kstat_vpath_init(vxgedev_t *vdev)
{
	kstat_t *ksp[VXGE_HAL_MAX_VIRTUAL_PATHS];
	vxge_kstat_vpath_t   *vxge_ksp;
	char vxge_str[20] = "vxge_kstat_vpath";
	char vxge_str1[21] = "";
	u16 j;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_KSTAT_VPATH_INIT ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	for (j = 0; j < vdev->no_of_vpath; j++) {
		vdev->vpaths[j].kstat_id = j;
		(void) sprintf(vxge_str1, "%s %d", vxge_str, j);
		ksp[j] = kstat_create(VXGE_IFNAME,
		    ddi_get_instance(vdev->dev_info), vxge_str1,
		    "net", KSTAT_TYPE_NAMED,
		    (sizeof (vxge_kstat_vpath_t) / sizeof (kstat_named_t)), 0);
		if (ksp[j] == NULL) {
			vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
			    "%s%d: Failed to create kstat for vpath \n",
			    VXGE_IFNAME, vdev->instance);
			return;
		}

		vxge_ksp = (vxge_kstat_vpath_t *)ksp[j]->ks_data;

		/* Fill in the ksp */
		vdev->vxge_kstat_vpath_ksp[j] = ksp[j];

		/* Function to provide kernel stat update on demand */
		ksp[j]->ks_update = vxge_update_vpath_kstats;

		/* Pointer into provider's raw statistics */
		ksp[j]->ks_private = (void *)&vdev->vpaths[j];

		/* Initialize all the statistics */
		kstat_named_init(&vxge_ksp->ini_num_mwr_sent,
		    "ini_num_mwr_sent", KSTAT_DATA_UINT32);
		kstat_named_init(&vxge_ksp->ini_num_mrd_sent,
		    "ini_num_mrd_sent", KSTAT_DATA_UINT32);
		kstat_named_init(&vxge_ksp->ini_num_cpl_rcvd,
		    "ini_num_cpl_rcvd", KSTAT_DATA_UINT32);
		kstat_named_init(&vxge_ksp->ini_num_mwr_byte_sent,
		    "ini_num_mwr_byte_sent", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->ini_num_cpl_byte_rcvd,
		    "ini_num_cpl_byte_rcvd", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->wrcrdtarb_xoff,
		    "wrcrdtarb_xoff", KSTAT_DATA_UINT32);
		kstat_named_init(&vxge_ksp->rdcrdtarb_xoff,
		    "rdcrdtarb_xoff", KSTAT_DATA_UINT32);
		kstat_named_init(&vxge_ksp->vpath_genstats_count0,
		    "vpath_genstats_count0", KSTAT_DATA_UINT32);
		kstat_named_init(&vxge_ksp->vpath_genstats_count1,
		    "vpath_genstats_count1", KSTAT_DATA_UINT32);
		kstat_named_init(&vxge_ksp->vpath_genstats_count2,
		    "vpath_genstats_count2", KSTAT_DATA_UINT32);
		kstat_named_init(&vxge_ksp->vpath_genstats_count3,
		    "vpath_genstats_count3", KSTAT_DATA_UINT32);
		kstat_named_init(&vxge_ksp->vpath_genstats_count4,
		    "vpath_genstats_count4", KSTAT_DATA_UINT32);
		kstat_named_init(&vxge_ksp->vpath_gennstats_count5,
		    "vpath_gennstats_count5", KSTAT_DATA_UINT32);


		kstat_named_init(&vxge_ksp->tx_ttl_eth_frms,
		    "tx_ttl_eth_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->tx_ttl_eth_octets,
		    "tx_ttl_eth_octets", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->tx_data_octets,
		    "tx_data_octets", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->tx_mcast_frms,
		    "tx_mcast_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->tx_bcast_frms,
		    "tx_bcast_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->tx_ucast_frms,
		    "tx_ucast_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->tx_tagged_frms,
		    "tx_tagged_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->tx_vld_ip,
		    "tx_vld_ip", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->tx_vld_ip_octets,
		    "tx_vld_ip_octets", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->tx_icmp,
		    "tx_icmp", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->tx_tcp,
		    "tx_tcp", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->tx_rst_tcp,
		    "tx_rst_tcp", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->tx_udp,
		    "tx_udp", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->tx_lost_ip,
		    "tx_lost_ip", KSTAT_DATA_UINT32);
		kstat_named_init(&vxge_ksp->tx_unknown_protocol,
		    "tx_unknown_protocol", KSTAT_DATA_UINT32);
		kstat_named_init(&vxge_ksp->tx_parse_error,
		    "tx_parse_error", KSTAT_DATA_UINT32);
		kstat_named_init(&vxge_ksp->tx_tcp_offload,
		    "tx_tcp_offload", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->tx_retx_tcp_offload,
		    "tx_retx_tcp_offload", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->tx_lost_ip_offload,
		    "tx_lost_ip_offload", KSTAT_DATA_UINT64);

		kstat_named_init(&vxge_ksp->rx_ttl_eth_frms,
		    "rx_ttl_eth_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_vld_frms,
		    "rx_vld_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_offload_frms,
		    "rx_offload_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_ttl_eth_octets,
		    "rx_ttl_eth_octets", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_data_octets,
		    "rx_data_octets", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_offload_octets,
		    "rx_offload_octets", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_vld_mcast_frms,
		    "rx_vld_mcast_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_vld_bcast_frms,
		    "rx_vld_bcast_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_accepted_ucast_frms,
		    "rx_accepted_ucast_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_accepted_nucast_frms,
		    "rx_accepted_nucast_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_tagged_frms,
		    "rx_tagged_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_long_frms,
		    "rx_long_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_usized_frms,
		    "rx_usized_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_osized_frms,
		    "rx_osized_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_frag_frms,
		    "rx_frag_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_jabber_frms,
		    "rx_jabber_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_ttl_64_frms,
		    "rx_ttl_64_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_ttl_65_127_frms,
		    "rx_ttl_65_127_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_ttl_128_255_frms,
		    "rx_ttl_128_255_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_ttl_256_511_frms,
		    "rx_ttl_256_511_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_ttl_512_1023_frms,
		    "rx_ttl_512_1023_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_ttl_1024_1518_frms,
		    "rx_ttl_1024_1518_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_ttl_1519_4095_frms,
		    "rx_ttl_1519_4095_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_ttl_4096_8191_frms,
		    "rx_ttl_4096_8191_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_ttl_8192_max_frms,
		    "rx_ttl_8192_max_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_ttl_gt_max_frms,
		    "rx_ttl_gt_max_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_ip, "rx_ip", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_accepted_ip,
		    "rx_accepted_ip", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_ip_octets,
		    "rx_ip_octets", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_err_ip,
		    "rx_err_ip", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_icmp,
		    "rx_icmp", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_tcp,
		    "rx_tcp", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_udp,
		    "rx_udp", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_err_tcp,
		    "rx_err_tcp", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_lost_frms,
		    "rx_lost_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_lost_ip,
		    "rx_lost_ip", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_lost_ip_offload,
		    "rx_lost_ip_offload", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_queue_full_discard,
		    "rx_queue_full_discard", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_red_discard,
		    "rx_red_discard", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_sleep_discard,
		    "rx_sleep_discard", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_mpa_ok_frms,
		    "rx_mpa_ok_frms", KSTAT_DATA_UINT64);

		kstat_named_init(&vxge_ksp->prog_event_vnum1,
		    "prog_event_vnum1", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->prog_event_vnum0,
		    "prog_event_vnum0", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->prog_event_vnum3,
		    "prog_event_vnum3", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->prog_event_vnum2,
		    "prog_event_vnum2", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_multi_cast_frame_discard,
		    "rx_multi_cast_frame_discard", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_frm_transferred,
		    "rx_frm_transferred", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rxd_returned, "rxd_returned",
		    KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_mpa_len_fail_frms,
		    "rx_mpa_len_fail_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_mpa_mrk_fail_frms,
		    "rx_mpa_mrk_fail_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_mpa_crc_fail_frms,
		    "rx_mpa_crc_fail_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_permitted_frms,
		    "rx_permitted_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_vp_reset_discarded_frms,
		    "rx_vp_reset_discarded_frms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rx_wol_frms, "rx_wol_frms",
		    KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->tx_vp_reset_discarded_frms,
		    "tx_vp_reset_discarded_frms", KSTAT_DATA_UINT64);

		kstat_named_init(&vxge_ksp->no_nces,
		    "no_nces", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->no_sqs,
		    "no_sqs", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->no_srqs,
		    "no_srqs", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->no_cqrqs,
		    "no_cqrqs", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->no_sessions,
		    "no_sessions", KSTAT_DATA_UINT64);


		kstat_named_init(&vxge_ksp->ring_full_cnt,
		    "ring_full_cnt", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->ring_usage_cnt,
		    "ring_usage_cnt", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->ring_usage_max,
		    "ring_usage_max", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->ring_avg_compl_per_intr_cnt,
		    "ring_avg_compl_per_intr_cnt", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->ring_total_compl_cnt,
		    "ring_total_compl_cnt", KSTAT_DATA_UINT64);

		kstat_named_init(&vxge_ksp->rxd_t_code_err_cnt_0,
		    "rxd_t_code_err_cnt[0]", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rxd_t_code_err_cnt_1,
		    "rxd_t_code_err_cnt[1]", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rxd_t_code_err_cnt_2,
		    "rxd_t_code_err_cnt[2]", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rxd_t_code_err_cnt_3,
		    "rxd_t_code_err_cnt[3]", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rxd_t_code_err_cnt_4,
		    "rxd_t_code_err_cnt[4]", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rxd_t_code_err_cnt_5,
		    "rxd_t_code_err_cnt[5]", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rxd_t_code_err_cnt_6,
		    "rxd_t_code_err_cnt[6]", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rxd_t_code_err_cnt_7,
		    "rxd_t_code_err_cnt[7]", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rxd_t_code_err_cnt_8,
		    "rxd_t_code_err_cnt[8]", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rxd_t_code_err_cnt_9,
		    "rxd_t_code_err_cnt[9]", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rxd_t_code_err_cnt_10,
		    "rxd_t_code_err_cnt[10]", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rxd_t_code_err_cnt_11,
		    "rxd_t_code_err_cnt[11]", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rxd_t_code_err_cnt_12,
		    "rxd_t_code_err_cnt[12]", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rxd_t_code_err_cnt_13,
		    "rxd_t_code_err_cnt[13]", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rxd_t_code_err_cnt_14,
		    "rxd_t_code_err_cnt[14]", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->rxd_t_code_err_cnt_15,
		    "rxd_t_code_err_cnt[15]", KSTAT_DATA_UINT64);


		kstat_named_init(&vxge_ksp->unknown_alarms, "unknown_alarms",
		    KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->network_sustained_fault,
		    "network_sustained_fault", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->network_sustained_ok,
		    "network_sustained_ok", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->kdfcctl_fifo0_overwrite,
		    "kdfcctl_fifo0_overwrite", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->kdfcctl_fifo0_poison,
		    "kdfcctl_fifo0_poison", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->kdfcctl_fifo0_dma_error,
		    "kdfcctl_fifo0_dma_error", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->kdfcctl_fifo1_overwrite,
		    "kdfcctl_fifo1_overwrite", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->kdfcctl_fifo1_poison,
		    "kdfcctl_fifo1_poison", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->kdfcctl_fifo1_dma_error,
		    "kdfcctl_fifo1_dma_error", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->kdfcctl_fifo2_overwrite,
		    "kdfcctl_fifo2_overwrite", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->kdfcctl_fifo2_poison,
		    "kdfcctl_fifo2_poison", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->kdfcctl_fifo2_dma_error,
		    "kdfcctl_fifo2_dma_error", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->dblgen_fifo0_overflow,
		    "dblgen_fifo0_overflow", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->dblgen_fifo1_overflow,
		    "dblgen_fifo1_overflow", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->dblgen_fifo2_overflow,
		    "dblgen_fifo2_overflow", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->statsb_pif_chain_error,
		    "statsb_pif_chain_error", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->statsb_drop_timeout,
		    "statsb_drop_timeout", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->target_illegal_access,
		    "target_illegal_access", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->ini_serr_det, "ini_serr_det",
		    KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->pci_config_status_err,
		    "pci_config_status_err", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->pci_config_uncor_err,
		    "pci_config_uncor_err", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->pci_config_cor_err,
		    "pci_config_cor_err", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->mrpcim_to_vpath_alarms,
		    "mrpcim_to_vpath_alarms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->srpcim_to_vpath_alarms,
		    "srpcim_to_vpath_alarms", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->srpcim_msg_to_vpath,
		    "srpcim_msg_to_vpath", KSTAT_DATA_UINT64);

		kstat_named_init(&vxge_ksp->prc_ring_bump_cnt,
		    "ring_bump_cnt", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->prc_rxdcm_sc_err_cnt,
		    "prc_rxdcm_sc_err_cnt", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->prc_rxdcm_sc_abort_cnt,
		    "prc_rxdcm_sc_abort_cnt", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->prc_quanta_size_err_cnt,
		    "prc_quanta_size_err_cnt", KSTAT_DATA_UINT64);

		kstat_named_init(&vxge_ksp->fifo_full_cnt,
		    "fifo_full_cnt", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->fifo_usage_cnt,
		    "fifo_usage_cnt", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->fifo_usage_max,
		    "fifo_usage_max", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->fifo_avg_compl_per_intr_cnt,
		    "fifo_avg_compl_per_intr_cnt", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->fifo_total_compl_cnt,
		    "fifo_total_compl_cnt", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->fifo_total_posts,
		    "fifo_total_posts", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->fifo_total_buffers,
		    "fifo_total_buffers", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->fifo_avg_buffers_per_post,
		    "fifo_avg_buffers_per_post", KSTAT_DATA_UINT64);

		kstat_named_init(&vxge_ksp->fifo_copied_frags,
		    "fifo_copied_frags", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->fifo_copied_buffers,
		    "fifo_copied_buffers", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->fifo_avg_buffer_size,
		    "fifo_avg_buffer_size", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->fifo_avg_post_size,
		    "fifo_avg_post_size", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->fifo_total_posts_dang_dtrs,
		    "fifo_total_posts_dang_dtrs", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->fifo_total_posts_dang_frags,
		    "fifo_total_posts_dang_frags", KSTAT_DATA_UINT64);

		kstat_named_init(&vxge_ksp->txd_t_code_err_cnt_0,
		    "txd_t_code_err_cnt[0]", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->txd_t_code_err_cnt_1,
		    "txd_t_code_err_cnt[1]", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->txd_t_code_err_cnt_2,
		    "txd_t_code_err_cnt[2]", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->txd_t_code_err_cnt_3,
		    "txd_t_code_err_cnt[3]", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->txd_t_code_err_cnt_4,
		    "txd_t_code_err_cnt[4]", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->txd_t_code_err_cnt_5,
		    "txd_t_code_err_cnt[5]", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->txd_t_code_err_cnt_6,
		    "txd_t_code_err_cnt[6]", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->txd_t_code_err_cnt_7,
		    "txd_t_code_err_cnt[7]", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->txd_t_code_err_cnt_8,
		    "txd_t_code_err_cnt[8]", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->txd_t_code_err_cnt_9,
		    "txd_t_code_err_cnt[9]", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->txd_t_code_err_cnt_10,
		    "txd_t_code_err_cnt[10]", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->txd_t_code_err_cnt_11,
		    "txd_t_code_err_cnt[11]", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->txd_t_code_err_cnt_12,
		    "txd_t_code_err_cnt[12]", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->txd_t_code_err_cnt_13,
		    "txd_t_code_err_cnt[13]", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->txd_t_code_err_cnt_14,
		    "txd_t_code_err_cnt[14]", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->txd_t_code_err_cnt_15,
		    "txd_t_code_err_cnt[15]", KSTAT_DATA_UINT64);

		kstat_named_init(&vxge_ksp->total_frags,
		    "total_frags", KSTAT_DATA_UINT64);
		kstat_named_init(&vxge_ksp->copied_frags,
		    "copied_frags", KSTAT_DATA_UINT64);


		/* Add kstat to systems kstat chain */
		kstat_install(ksp[j]);
	}

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_KSTAT_VPATH_INIT EXIT ",
	    VXGE_IFNAME, vdev->instance);

}
static void
vxge_kstat_vpath_destroy(vxgedev_t *vdev)
{
	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_KSTAT_VPATH_DESTROY ENTRY ",
	    VXGE_IFNAME, vdev->instance);
	u16 j;
	for (j = 0; j < vdev->no_of_vpath; j++) {
		if (vdev->vxge_kstat_vpath_ksp[j])
			kstat_delete(vdev->vxge_kstat_vpath_ksp[j]);
	}
	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_KSTAT_VPATH_DESTROY EXIT ",
	    VXGE_IFNAME, vdev->instance);
}

static void
vxge_kstat_port_destroy(vxgedev_t *vdev)
{
	u16 j;
	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_KSTAT_PORT_DESTROY ENTRY ",
	    VXGE_IFNAME, vdev->instance);
	for (j = 0; j < VXGE_HAL_WIRE_PORT_MAX_PORTS; j++) {
		if (vdev->vxge_kstat_port_ksp[j])
			kstat_delete(vdev->vxge_kstat_port_ksp[j]);
	}
	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_KSTAT_PORT_DESTROY EXIT ",
	    VXGE_IFNAME, vdev->instance);
}
static void
vxge_kstat_aggr_destroy(vxgedev_t *vdev)
{
	u16 j;
	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_KSTAT_AGGR_DESTROY ENTRY ",
	    VXGE_IFNAME, vdev->instance);
	for (j = 0; j < VXGE_HAL_WIRE_PORT_MAX_PORTS; j++) {
		if (vdev->vxge_kstat_aggr_ksp[j])
			kstat_delete(vdev->vxge_kstat_aggr_ksp[j]);
	}
	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_KSTAT_AGGR_DESTROY EXIT ",
	    VXGE_IFNAME, vdev->instance);
}

static void
vxge_kstat_mrpcim_destroy(vxgedev_t *vdev)
{
	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_KSTAT_MRPCIM_DESTROY ENTRY ",
	    VXGE_IFNAME, vdev->instance);

		if (vdev->vxge_kstat_mrpcim_ksp)
			kstat_delete(vdev->vxge_kstat_mrpcim_ksp);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_KSTAT_MRPCIM_DESTROY EXIT ",
	    VXGE_IFNAME, vdev->instance);
}


static int
vxge_update_driver_kstats(kstat_t *ksp, int rw)
{
	vxge_sw_kstats_t  *vxge_ksp;

	vxgedev_t *vdev = (vxgedev_t *)ksp->ks_private;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_UPDATE_DRIVER_KSTATS ENTRY ",
	    VXGE_IFNAME, vdev->instance);
	vxge_ksp = (vxge_sw_kstats_t *)ksp->ks_data;

	if (rw == KSTAT_WRITE) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: Write Support is not added\n",
		    VXGE_IFNAME, vdev->instance);
		return (EACCES);
	}

	vxge_ksp->tx_frms.value.ull = vdev->stats.tx_frms;
	vxge_ksp->txd_alloc_fail.value.ull = vdev->stats.txd_alloc_fail;
	vxge_ksp->vpaths_open.value.ull = vdev->stats.vpaths_open;
	vxge_ksp->vpath_open_fail.value.ull = vdev->stats.vpath_open_fail;
	vxge_ksp->rx_frms.value.ull = vdev->stats.rx_frms;
	vxge_ksp->rxd_alloc_fail.value.ull = vdev->stats.rxd_alloc_fail;
	vxge_ksp->link_up.value.ull = vdev->stats.link_up;
	vxge_ksp->link_down.value.ull = vdev->stats.link_down;
	vxge_ksp->kmem_zalloc_fail.value.ull = vdev->stats.kmem_zalloc_fail;
	vxge_ksp->allocb_fail.value.ull = vdev->stats.allocb_fail;
	vxge_ksp->desballoc_fail.value.ull = vdev->stats.desballoc_fail;
	vxge_ksp->ddi_dma_alloc_handle_fail.value.ull =
	    vdev->stats.ddi_dma_alloc_handle_fail;
	vxge_ksp->ddi_dma_mem_alloc_fail.value.ull =
	    vdev->stats.ddi_dma_mem_alloc_fail;
	vxge_ksp->ddi_dma_addr_bind_handle_fail.value.ull =
	    vdev->stats.ddi_dma_addr_bind_handle_fail;
	vxge_ksp->ddi_dma_addr_unbind_handle_fail.value.ull =
	    vdev->stats.ddi_dma_addr_unbind_handle_fail;
	vxge_ksp->kmem_alloc.value.ull = vdev->stats.kmem_alloc;
	vxge_ksp->kmem_free.value.ull = vdev->stats.kmem_free;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_UPDATE_DRIVER_KSTATS EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (0);
}

static int
vxge_update_mrpcim_kstats(kstat_t *ksp, int rw)
{
	vxge_hal_status_e status = VXGE_HAL_OK;
	vxge_kstat_mrpcim_t  *vxge_ksp;
	vxge_hal_mrpcim_stats_hw_info_t mrpcim_stats;

	vxgedev_t *vdev = (vxgedev_t *)ksp->ks_private;

	vxge_hal_device_t *hldev = (vxge_hal_device_t *)vdev->devh;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_UPDATE_MRPCIM_KSTATS ENTRY ",
	    VXGE_IFNAME, vdev->instance);
	vxge_ksp = (vxge_kstat_mrpcim_t *)ksp->ks_data;

	if (rw == KSTAT_WRITE) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: Write Support is not added\n",
		    VXGE_IFNAME, vdev->instance);
		return (EACCES);
	}

	status = vxge_hal_mrpcim_stats_enable(hldev);
	if (status != VXGE_HAL_OK) {
		/*EMPTY*/
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: %s : %d Failure in enabling\
		    mrpcim Stats ", VXGE_IFNAME, vdev->instance,
		    __func__, __LINE__);
	}
	status = vxge_hal_mrpcim_stats_get(hldev, &mrpcim_stats);
	if (status != VXGE_HAL_OK) {
		/*EMPTY*/
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: %s : %d Failure in getting mrpcim Stats ",
		    VXGE_IFNAME, vdev->instance, __func__, __LINE__);
	}
		vxge_ksp->pic_ini_rd_drop.value.ul =
		    mrpcim_stats.pic_ini_rd_drop;
		vxge_ksp->pic_ini_wr_drop.value.ul =
		    mrpcim_stats.pic_ini_wr_drop;


		vxge_ksp->pic_wrcrb_ph_crdt_depd_vpn0.value.ul =
		    mrpcim_stats.pic_wrcrdtarb_ph_crdt_depleted_vplane[0].\
		    pic_wrcrdtarb_ph_crdt_depleted;
		vxge_ksp->pic_wrcrb_ph_crdt_depd_vpn1.value.ul =
		    mrpcim_stats.pic_wrcrdtarb_ph_crdt_depleted_vplane[1].\
		    pic_wrcrdtarb_ph_crdt_depleted;
		vxge_ksp->pic_wrcrb_ph_crdt_depd_vpn2.value.ul =
		    mrpcim_stats.pic_wrcrdtarb_ph_crdt_depleted_vplane[2].\
		    pic_wrcrdtarb_ph_crdt_depleted;
		vxge_ksp->pic_wrcrb_ph_crdt_depd_vpn3.value.ul =
		    mrpcim_stats.pic_wrcrdtarb_ph_crdt_depleted_vplane[3].\
		    pic_wrcrdtarb_ph_crdt_depleted;
		vxge_ksp->pic_wrcrb_ph_crdt_depd_vpn4.value.ul =
		    mrpcim_stats.pic_wrcrdtarb_ph_crdt_depleted_vplane[4].\
		    pic_wrcrdtarb_ph_crdt_depleted;
		vxge_ksp->pic_wrcrb_ph_crdt_depd_vpn5.value.ul =
		    mrpcim_stats.pic_wrcrdtarb_ph_crdt_depleted_vplane[5].\
		    pic_wrcrdtarb_ph_crdt_depleted;
		vxge_ksp->pic_wrcrb_ph_crdt_depd_vpn6.value.ul =
		    mrpcim_stats.pic_wrcrdtarb_ph_crdt_depleted_vplane[6].\
		    pic_wrcrdtarb_ph_crdt_depleted;
		vxge_ksp->pic_wrcrb_ph_crdt_depd_vpn7.value.ul =
		    mrpcim_stats.pic_wrcrdtarb_ph_crdt_depleted_vplane[7].\
		    pic_wrcrdtarb_ph_crdt_depleted;
		vxge_ksp->pic_wrcrb_ph_crdt_depd_vpn8.value.ul =
		    mrpcim_stats.pic_wrcrdtarb_ph_crdt_depleted_vplane[8].\
		    pic_wrcrdtarb_ph_crdt_depleted;
		vxge_ksp->pic_wrcrb_ph_crdt_depd_vpn9.value.ul =
		    mrpcim_stats.pic_wrcrdtarb_ph_crdt_depleted_vplane[9].\
		    pic_wrcrdtarb_ph_crdt_depleted;
		vxge_ksp->pic_wrcrb_ph_crdt_depd_vpn10.value.ul =
		    mrpcim_stats.pic_wrcrdtarb_ph_crdt_depleted_vplane[10].\
		    pic_wrcrdtarb_ph_crdt_depleted;
		vxge_ksp->pic_wrcrb_ph_crdt_depd_vpn11.value.ul =
		    mrpcim_stats.pic_wrcrdtarb_ph_crdt_depleted_vplane[11].\
		    pic_wrcrdtarb_ph_crdt_depleted;
		vxge_ksp->pic_wrcrb_ph_crdt_depd_vpn12.value.ul =
		    mrpcim_stats.pic_wrcrdtarb_ph_crdt_depleted_vplane[12].\
		    pic_wrcrdtarb_ph_crdt_depleted;
		vxge_ksp->pic_wrcrb_ph_crdt_depd_vpn13.value.ul =
		    mrpcim_stats.pic_wrcrdtarb_ph_crdt_depleted_vplane[13].\
		    pic_wrcrdtarb_ph_crdt_depleted;
		vxge_ksp->pic_wrcrb_ph_crdt_depd_vpn14.value.ul =
		    mrpcim_stats.pic_wrcrdtarb_ph_crdt_depleted_vplane[14].\
		    pic_wrcrdtarb_ph_crdt_depleted;
		vxge_ksp->pic_wrcrb_ph_crdt_depd_vpn15.value.ul =
		    mrpcim_stats.pic_wrcrdtarb_ph_crdt_depleted_vplane[15].\
		    pic_wrcrdtarb_ph_crdt_depleted;
		vxge_ksp->pic_wrcrb_ph_crdt_depd_vpn16.value.ul =
		    mrpcim_stats.pic_wrcrdtarb_ph_crdt_depleted_vplane[16].\
		    pic_wrcrdtarb_ph_crdt_depleted;


		vxge_ksp->pic_wrcrb_pd_crdt_depd_vpn0.value.ul =
		    mrpcim_stats.pic_wrcrdtarb_pd_crdt_depleted_vplane[0].\
		    pic_wrcrdtarb_pd_crdt_depleted;
		vxge_ksp->pic_wrcrb_pd_crdt_depd_vpn1.value.ul =
		    mrpcim_stats.pic_wrcrdtarb_pd_crdt_depleted_vplane[1].\
		    pic_wrcrdtarb_pd_crdt_depleted;
		vxge_ksp->pic_wrcrb_pd_crdt_depd_vpn2.value.ul =
		    mrpcim_stats.pic_wrcrdtarb_pd_crdt_depleted_vplane[2].\
		    pic_wrcrdtarb_pd_crdt_depleted;
		vxge_ksp->pic_wrcrb_pd_crdt_depd_vpn3.value.ul =
		    mrpcim_stats.pic_wrcrdtarb_pd_crdt_depleted_vplane[3].\
		    pic_wrcrdtarb_pd_crdt_depleted;
		vxge_ksp->pic_wrcrb_pd_crdt_depd_vpn4.value.ul =
		    mrpcim_stats.pic_wrcrdtarb_pd_crdt_depleted_vplane[4].\
		    pic_wrcrdtarb_pd_crdt_depleted;
		vxge_ksp->pic_wrcrb_pd_crdt_depd_vpn5.value.ul =
		    mrpcim_stats.pic_wrcrdtarb_pd_crdt_depleted_vplane[5].\
		    pic_wrcrdtarb_pd_crdt_depleted;
		vxge_ksp->pic_wrcrb_pd_crdt_depd_vpn6.value.ul =
		    mrpcim_stats.pic_wrcrdtarb_pd_crdt_depleted_vplane[6].\
		    pic_wrcrdtarb_pd_crdt_depleted;
		vxge_ksp->pic_wrcrb_pd_crdt_depd_vpn7.value.ul =
		    mrpcim_stats.pic_wrcrdtarb_pd_crdt_depleted_vplane[7].\
		    pic_wrcrdtarb_pd_crdt_depleted;
		vxge_ksp->pic_wrcrb_pd_crdt_depd_vpn8.value.ul =
		    mrpcim_stats.pic_wrcrdtarb_pd_crdt_depleted_vplane[8].\
		    pic_wrcrdtarb_pd_crdt_depleted;
		vxge_ksp->pic_wrcrb_pd_crdt_depd_vpn9.value.ul =
		    mrpcim_stats.pic_wrcrdtarb_pd_crdt_depleted_vplane[9].\
		    pic_wrcrdtarb_pd_crdt_depleted;
		vxge_ksp->pic_wrcrb_pd_crdt_depd_vpn10.value.ul =
		    mrpcim_stats.pic_wrcrdtarb_pd_crdt_depleted_vplane[10].\
		    pic_wrcrdtarb_pd_crdt_depleted;
		vxge_ksp->pic_wrcrb_pd_crdt_depd_vpn11.value.ul =
		    mrpcim_stats.pic_wrcrdtarb_pd_crdt_depleted_vplane[11].\
		    pic_wrcrdtarb_pd_crdt_depleted;
		vxge_ksp->pic_wrcrb_pd_crdt_depd_vpn12.value.ul =
		    mrpcim_stats.pic_wrcrdtarb_pd_crdt_depleted_vplane[12].\
		    pic_wrcrdtarb_pd_crdt_depleted;
		vxge_ksp->pic_wrcrb_pd_crdt_depd_vpn13.value.ul =
		    mrpcim_stats.pic_wrcrdtarb_pd_crdt_depleted_vplane[13].\
		    pic_wrcrdtarb_pd_crdt_depleted;
		vxge_ksp->pic_wrcrb_pd_crdt_depd_vpn14.value.ul =
		    mrpcim_stats.pic_wrcrdtarb_pd_crdt_depleted_vplane[14].\
		    pic_wrcrdtarb_pd_crdt_depleted;
		vxge_ksp->pic_wrcrb_pd_crdt_depd_vpn15.value.ul =
		    mrpcim_stats.pic_wrcrdtarb_pd_crdt_depleted_vplane[15].\
		    pic_wrcrdtarb_pd_crdt_depleted;
		vxge_ksp->pic_wrcrb_pd_crdt_depd_vpn16.value.ul =
		    mrpcim_stats.pic_wrcrdtarb_pd_crdt_depleted_vplane[16].\
		    pic_wrcrdtarb_pd_crdt_depleted;

		vxge_ksp->pic_rdcrb_nph_crdt_depd_vpn0.value.ul =
		    mrpcim_stats.pic_rdcrdtarb_nph_crdt_depleted_vplane[0].\
		    pic_rdcrdtarb_nph_crdt_depleted;
		vxge_ksp->pic_rdcrb_nph_crdt_depd_vpn1.value.ul =
		    mrpcim_stats.pic_rdcrdtarb_nph_crdt_depleted_vplane[1].\
		    pic_rdcrdtarb_nph_crdt_depleted;
		vxge_ksp->pic_rdcrb_nph_crdt_depd_vpn2.value.ul =
		    mrpcim_stats.pic_rdcrdtarb_nph_crdt_depleted_vplane[2].\
		    pic_rdcrdtarb_nph_crdt_depleted;
		vxge_ksp->pic_rdcrb_nph_crdt_depd_vpn3.value.ul =
		    mrpcim_stats.pic_rdcrdtarb_nph_crdt_depleted_vplane[3].\
		    pic_rdcrdtarb_nph_crdt_depleted;
		vxge_ksp->pic_rdcrb_nph_crdt_depd_vpn4.value.ul =
		    mrpcim_stats.pic_rdcrdtarb_nph_crdt_depleted_vplane[4].\
		    pic_rdcrdtarb_nph_crdt_depleted;
		vxge_ksp->pic_rdcrb_nph_crdt_depd_vpn5.value.ul =
		    mrpcim_stats.pic_rdcrdtarb_nph_crdt_depleted_vplane[5].\
		    pic_rdcrdtarb_nph_crdt_depleted;
		vxge_ksp->pic_rdcrb_nph_crdt_depd_vpn6.value.ul =
		    mrpcim_stats.pic_rdcrdtarb_nph_crdt_depleted_vplane[6].\
		    pic_rdcrdtarb_nph_crdt_depleted;
		vxge_ksp->pic_rdcrb_nph_crdt_depd_vpn7.value.ul =
		    mrpcim_stats.pic_rdcrdtarb_nph_crdt_depleted_vplane[7].\
		    pic_rdcrdtarb_nph_crdt_depleted;
		vxge_ksp->pic_rdcrb_nph_crdt_depd_vpn8.value.ul =
		    mrpcim_stats.pic_rdcrdtarb_nph_crdt_depleted_vplane[8].\
		    pic_rdcrdtarb_nph_crdt_depleted;
		vxge_ksp->pic_rdcrb_nph_crdt_depd_vpn9.value.ul =
		    mrpcim_stats.pic_rdcrdtarb_nph_crdt_depleted_vplane[9].\
		    pic_rdcrdtarb_nph_crdt_depleted;
		vxge_ksp->pic_rdcrb_nph_crdt_depd_vpn10.value.ul =
		    mrpcim_stats.pic_rdcrdtarb_nph_crdt_depleted_vplane[10]\
		    .pic_rdcrdtarb_nph_crdt_depleted;
		vxge_ksp->pic_rdcrb_nph_crdt_depd_vpn11.value.ul =
		    mrpcim_stats.pic_rdcrdtarb_nph_crdt_depleted_vplane[11]\
		    .pic_rdcrdtarb_nph_crdt_depleted;
		vxge_ksp->pic_rdcrb_nph_crdt_depd_vpn12.value.ul =
		    mrpcim_stats.pic_rdcrdtarb_nph_crdt_depleted_vplane[12]\
		    .pic_rdcrdtarb_nph_crdt_depleted;
		vxge_ksp->pic_rdcrb_nph_crdt_depd_vpn13.value.ul =
		    mrpcim_stats.pic_rdcrdtarb_nph_crdt_depleted_vplane[13]\
		    .pic_rdcrdtarb_nph_crdt_depleted;
		vxge_ksp->pic_rdcrb_nph_crdt_depd_vpn14.value.ul =
		    mrpcim_stats.pic_rdcrdtarb_nph_crdt_depleted_vplane[14]\
		    .pic_rdcrdtarb_nph_crdt_depleted;
		vxge_ksp->pic_rdcrb_nph_crdt_depd_vpn15.value.ul =
		    mrpcim_stats.pic_rdcrdtarb_nph_crdt_depleted_vplane[15]\
		    .pic_rdcrdtarb_nph_crdt_depleted;
		vxge_ksp->pic_rdcrb_nph_crdt_depd_vpn16.value.ul =
		    mrpcim_stats.pic_rdcrdtarb_nph_crdt_depleted_vplane[16]\
		    .pic_rdcrdtarb_nph_crdt_depleted;


		vxge_ksp->pic_ini_rd_vpin_drop.value.ul =
		    mrpcim_stats.pic_ini_rd_vpin_drop;
		vxge_ksp->pic_ini_wr_vpin_drop.value.ul =
		    mrpcim_stats.pic_ini_wr_vpin_drop;

		vxge_ksp->pic_genstats_count0.value.ul =
		    mrpcim_stats.pic_genstats_count0;
		vxge_ksp->pic_genstats_count1.value.ul =
		    mrpcim_stats.pic_genstats_count1;
		vxge_ksp->pic_genstats_count2.value.ul =
		    mrpcim_stats.pic_genstats_count2;
		vxge_ksp->pic_genstats_count3.value.ul =
		    mrpcim_stats.pic_genstats_count3;
		vxge_ksp->pic_genstats_count4.value.ul =
		    mrpcim_stats.pic_genstats_count4;
		vxge_ksp->pic_genstats_count5.value.ul =
		    mrpcim_stats.pic_genstats_count5;
		vxge_ksp->pci_rstdrop_cpl.value.ul =
		    mrpcim_stats.pci_rstdrop_cpl;
		vxge_ksp->pci_rstdrop_msg.value.ul =
		    mrpcim_stats.pci_rstdrop_msg;
		vxge_ksp->pci_rstdrop_client1.value.ul =
		    mrpcim_stats.pci_rstdrop_client1;
		vxge_ksp->pci_rstdrop_client0.value.ul =
		    mrpcim_stats.pci_rstdrop_client0;
		vxge_ksp->pci_rstdrop_client2.value.ul =
		    mrpcim_stats.pci_rstdrop_client2;

		vxge_ksp->pci_depl_cplh_vplane0.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[0].pci_depl_cplh;
		vxge_ksp->pci_depl_nph_vplane0.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[0].pci_depl_nph;
		vxge_ksp->pci_depl_ph_vplane0.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[0].pci_depl_ph;
		vxge_ksp->pci_depl_cplh_vplane1.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[1].pci_depl_cplh;
		vxge_ksp->pci_depl_nph_vplane1.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[1].pci_depl_nph;
		vxge_ksp->pci_depl_ph_vplane1.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[1].pci_depl_ph;
		vxge_ksp->pci_depl_cplh_vplane2.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[2].pci_depl_cplh;
		vxge_ksp->pci_depl_nph_vplane2.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[2].pci_depl_nph;
		vxge_ksp->pci_depl_ph_vplane2.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[2].pci_depl_ph;
		vxge_ksp->pci_depl_cplh_vplane3.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[3].pci_depl_cplh;
		vxge_ksp->pci_depl_nph_vplane3.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[3].pci_depl_nph;
		vxge_ksp->pci_depl_ph_vplane3.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[3].pci_depl_ph;
		vxge_ksp->pci_depl_cplh_vplane4.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[4].pci_depl_cplh;
		vxge_ksp->pci_depl_nph_vplane4.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[4].pci_depl_nph;
		vxge_ksp->pci_depl_ph_vplane4.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[4].pci_depl_ph;
		vxge_ksp->pci_depl_cplh_vplane5.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[5].pci_depl_cplh;
		vxge_ksp->pci_depl_nph_vplane5.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[5].pci_depl_nph;
		vxge_ksp->pci_depl_ph_vplane5.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[5].pci_depl_ph;
		vxge_ksp->pci_depl_cplh_vplane6.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[6].pci_depl_cplh;
		vxge_ksp->pci_depl_nph_vplane6.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[6].pci_depl_nph;
		vxge_ksp->pci_depl_ph_vplane6.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[6].pci_depl_ph;
		vxge_ksp->pci_depl_cplh_vplane7.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[7].pci_depl_cplh;
		vxge_ksp->pci_depl_nph_vplane7.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[7].pci_depl_nph;
		vxge_ksp->pci_depl_ph_vplane7.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[7].pci_depl_ph;
		vxge_ksp->pci_depl_cplh_vplane8.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[8].pci_depl_cplh;
		vxge_ksp->pci_depl_nph_vplane8.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[8].pci_depl_nph;
		vxge_ksp->pci_depl_ph_vplane8.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[8].pci_depl_ph;
		vxge_ksp->pci_depl_cplh_vplane9.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[9].pci_depl_cplh;
		vxge_ksp->pci_depl_nph_vplane9.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[9].pci_depl_nph;
		vxge_ksp->pci_depl_ph_vplane9.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[9].pci_depl_ph;
		vxge_ksp->pci_depl_cplh_vplane10.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[10].pci_depl_cplh;
		vxge_ksp->pci_depl_nph_vplane10.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[10].pci_depl_nph;
		vxge_ksp->pci_depl_ph_vplane10.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[10].pci_depl_ph;
		vxge_ksp->pci_depl_cplh_vplane11.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[11].pci_depl_cplh;
		vxge_ksp->pci_depl_nph_vplane11.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[11].pci_depl_nph;
		vxge_ksp->pci_depl_ph_vplane11.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[11].pci_depl_ph;
		vxge_ksp->pci_depl_cplh_vplane12.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[12].pci_depl_cplh;
		vxge_ksp->pci_depl_nph_vplane12.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[12].pci_depl_nph;
		vxge_ksp->pci_depl_ph_vplane12.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[12].pci_depl_ph;
		vxge_ksp->pci_depl_cplh_vplane13.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[13].pci_depl_cplh;
		vxge_ksp->pci_depl_nph_vplane13.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[13].pci_depl_nph;
		vxge_ksp->pci_depl_ph_vplane13.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[13].pci_depl_ph;
		vxge_ksp->pci_depl_cplh_vplane14.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[14].pci_depl_cplh;
		vxge_ksp->pci_depl_nph_vplane14.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[14].pci_depl_nph;
		vxge_ksp->pci_depl_ph_vplane14.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[14].pci_depl_ph;
		vxge_ksp->pci_depl_cplh_vplane15.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[15].pci_depl_cplh;
		vxge_ksp->pci_depl_nph_vplane15.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[15].pci_depl_nph;
		vxge_ksp->pci_depl_ph_vplane15.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[15].pci_depl_ph;
		vxge_ksp->pci_depl_cplh_vplane16.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[16].pci_depl_cplh;
		vxge_ksp->pci_depl_nph_vplane16.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[16].pci_depl_nph;
		vxge_ksp->pci_depl_ph_vplane16.value.ul =
		    mrpcim_stats.pci_depl_h_vplane[16].pci_depl_ph;

		vxge_ksp->pci_depl_cpld_vplane0.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[0].pci_depl_cpld;
		vxge_ksp->pci_depl_npd_vplane0.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[0].pci_depl_npd;
		vxge_ksp->pci_depl_pd_vplane0.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[0].pci_depl_pd;
		vxge_ksp->pci_depl_cpld_vplane1.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[1].pci_depl_cpld;
		vxge_ksp->pci_depl_npd_vplane1.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[1].pci_depl_npd;
		vxge_ksp->pci_depl_pd_vplane1.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[1].pci_depl_pd;
		vxge_ksp->pci_depl_cpld_vplane2.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[2].pci_depl_cpld;
		vxge_ksp->pci_depl_npd_vplane2.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[2].pci_depl_npd;
		vxge_ksp->pci_depl_pd_vplane2.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[2].pci_depl_pd;
		vxge_ksp->pci_depl_cpld_vplane3.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[3].pci_depl_cpld;
		vxge_ksp->pci_depl_npd_vplane3.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[3].pci_depl_npd;
		vxge_ksp->pci_depl_pd_vplane3.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[3].pci_depl_pd;
		vxge_ksp->pci_depl_cpld_vplane4.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[4].pci_depl_cpld;
		vxge_ksp->pci_depl_npd_vplane4.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[4].pci_depl_npd;
		vxge_ksp->pci_depl_pd_vplane4.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[4].pci_depl_pd;
		vxge_ksp->pci_depl_cpld_vplane5.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[5].pci_depl_cpld;
		vxge_ksp->pci_depl_npd_vplane5.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[5].pci_depl_npd;
		vxge_ksp->pci_depl_pd_vplane5.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[5].pci_depl_pd;
		vxge_ksp->pci_depl_cpld_vplane6.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[6].pci_depl_cpld;
		vxge_ksp->pci_depl_npd_vplane6.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[6].pci_depl_npd;
		vxge_ksp->pci_depl_pd_vplane6.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[6].pci_depl_pd;
		vxge_ksp->pci_depl_cpld_vplane7.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[7].pci_depl_cpld;
		vxge_ksp->pci_depl_npd_vplane7.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[7].pci_depl_npd;
		vxge_ksp->pci_depl_pd_vplane7.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[7].pci_depl_pd;
		vxge_ksp->pci_depl_cpld_vplane8.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[8].pci_depl_cpld;
		vxge_ksp->pci_depl_npd_vplane8.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[8].pci_depl_npd;
		vxge_ksp->pci_depl_pd_vplane8.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[8].pci_depl_pd;
		vxge_ksp->pci_depl_cpld_vplane9.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[9].pci_depl_cpld;
		vxge_ksp->pci_depl_npd_vplane9.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[9].pci_depl_npd;
		vxge_ksp->pci_depl_pd_vplane9.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[9].pci_depl_pd;
		vxge_ksp->pci_depl_cpld_vplane10.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[10].pci_depl_cpld;
		vxge_ksp->pci_depl_npd_vplane10.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[10].pci_depl_npd;
		vxge_ksp->pci_depl_pd_vplane10.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[10].pci_depl_pd;
		vxge_ksp->pci_depl_cpld_vplane11.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[11].pci_depl_cpld;
		vxge_ksp->pci_depl_npd_vplane11.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[11].pci_depl_npd;
		vxge_ksp->pci_depl_pd_vplane11.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[11].pci_depl_pd;
		vxge_ksp->pci_depl_cpld_vplane12.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[12].pci_depl_cpld;
		vxge_ksp->pci_depl_npd_vplane12.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[12].pci_depl_npd;
		vxge_ksp->pci_depl_pd_vplane12.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[12].pci_depl_pd;
		vxge_ksp->pci_depl_cpld_vplane13.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[13].pci_depl_cpld;
		vxge_ksp->pci_depl_npd_vplane13.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[13].pci_depl_npd;
		vxge_ksp->pci_depl_pd_vplane13.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[13].pci_depl_pd;
		vxge_ksp->pci_depl_cpld_vplane14.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[14].pci_depl_cpld;
		vxge_ksp->pci_depl_npd_vplane14.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[14].pci_depl_npd;
		vxge_ksp->pci_depl_pd_vplane14.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[14].pci_depl_pd;
		vxge_ksp->pci_depl_cpld_vplane15.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[15].pci_depl_cpld;
		vxge_ksp->pci_depl_npd_vplane15.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[15].pci_depl_npd;
		vxge_ksp->pci_depl_pd_vplane15.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[15].pci_depl_pd;
		vxge_ksp->pci_depl_cpld_vplane16.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[16].pci_depl_cpld;
		vxge_ksp->pci_depl_npd_vplane16.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[16].pci_depl_npd;
		vxge_ksp->pci_depl_pd_vplane16.value.ul =
		    mrpcim_stats.pci_depl_d_vplane[16].pci_depl_pd;


		vxge_ksp->tx_ttl_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].tx_ttl_frms;
		vxge_ksp->tx_ttl_octets_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].tx_ttl_octets;
		vxge_ksp->tx_data_octets_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].tx_data_octets;
		vxge_ksp->tx_mcast_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].tx_mcast_frms;
		vxge_ksp->tx_bcast_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].tx_bcast_frms;
		vxge_ksp->tx_ucast_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].tx_ucast_frms;
		vxge_ksp->tx_tagged_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].tx_tagged_frms;
		vxge_ksp->tx_vld_ip_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].tx_vld_ip;
		vxge_ksp->tx_vld_ip_octets_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].tx_vld_ip_octets;
		vxge_ksp->tx_icmp_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].tx_icmp;
		vxge_ksp->tx_tcp_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].tx_tcp;
		vxge_ksp->tx_rst_tcp_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].tx_rst_tcp;
		vxge_ksp->tx_udp_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].tx_udp;
		vxge_ksp->tx_parse_error_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].tx_parse_error;
		vxge_ksp->tx_unknown_protocol_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].tx_unknown_protocol;
		vxge_ksp->tx_pause_ctrl_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].tx_pause_ctrl_frms;
		vxge_ksp->tx_marker_pdu_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].tx_marker_pdu_frms;
		vxge_ksp->tx_lacpdu_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].tx_lacpdu_frms;
		vxge_ksp->tx_drop_ip_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].tx_drop_ip;
		vxge_ksp->tx_marker_resp_pdu_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].tx_marker_resp_pdu_frms;
		vxge_ksp->tx_xgmii_char2_match_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].tx_xgmii_char2_match;
		vxge_ksp->tx_xgmii_char1_match_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].tx_xgmii_char1_match;
		vxge_ksp->tx_xgmii_column2_match_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].tx_xgmii_column2_match;
		vxge_ksp->tx_xgmii_column1_match_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].tx_xgmii_column1_match;
		vxge_ksp->tx_any_err_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].tx_any_err_frms;
		vxge_ksp->tx_drop_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].tx_drop_frms;
		vxge_ksp->rx_ttl_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_ttl_frms;
		vxge_ksp->rx_vld_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_vld_frms;
		vxge_ksp->rx_offload_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_offload_frms;
		vxge_ksp->rx_ttl_octets_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_ttl_octets;
		vxge_ksp->rx_data_octets_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_data_octets;
		vxge_ksp->rx_offload_octets_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_offload_octets;
		vxge_ksp->rx_vld_mcast_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_vld_mcast_frms;
		vxge_ksp->rx_vld_bcast_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_vld_bcast_frms;
		vxge_ksp->rx_accepted_ucast_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_accepted_ucast_frms;
		vxge_ksp->rx_accepted_nucast_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_accepted_nucast_frms;
		vxge_ksp->rx_tagged_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_tagged_frms;
		vxge_ksp->rx_long_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_long_frms;
		vxge_ksp->rx_usized_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_usized_frms;
		vxge_ksp->rx_osized_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_osized_frms;
		vxge_ksp->rx_frag_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_frag_frms;
		vxge_ksp->rx_jabber_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_jabber_frms;
		vxge_ksp->rx_ttl_64_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_ttl_64_frms;
		vxge_ksp->rx_ttl_65_127_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_ttl_65_127_frms;
		vxge_ksp->rx_ttl_128_255_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_ttl_128_255_frms;
		vxge_ksp->rx_ttl_256_511_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_ttl_256_511_frms;
		vxge_ksp->rx_ttl_512_1023_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_ttl_512_1023_frms;
		vxge_ksp->rx_ttl_1024_1518_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_ttl_1024_1518_frms;
		vxge_ksp->rx_ttl_1519_4095_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_ttl_1519_4095_frms;
		vxge_ksp->rx_ttl_4096_8191_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_ttl_4096_8191_frms;
		vxge_ksp->rx_ttl_8192_max_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_ttl_8192_max_frms;
		vxge_ksp->rx_ttl_gt_max_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_ttl_gt_max_frms;
		vxge_ksp->rx_ip_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_ip;
		vxge_ksp->rx_accepted_ip_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_accepted_ip;
		vxge_ksp->rx_ip_octets_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_ip_octets;
		vxge_ksp->rx_err_ip_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_err_ip;
		vxge_ksp->rx_icmp_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_icmp;
		vxge_ksp->rx_tcp_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_tcp;
		vxge_ksp->rx_udp_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_udp;
		vxge_ksp->rx_err_tcp_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_err_tcp;
		vxge_ksp->rx_pause_cnt_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_pause_count;
		vxge_ksp->rx_pause_ctrl_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_pause_ctrl_frms;
		vxge_ksp->rx_unsup_ctrl_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_unsup_ctrl_frms;
		vxge_ksp->rx_fcs_err_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_fcs_err_frms;
		vxge_ksp->rx_in_rng_len_err_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_in_rng_len_err_frms;
		vxge_ksp->rx_out_rng_len_err_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_out_rng_len_err_frms;
		vxge_ksp->rx_drop_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_drop_frms;
		vxge_ksp->rx_discarded_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_discarded_frms;
		vxge_ksp->rx_drop_ip_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_drop_ip;
		vxge_ksp->rx_drp_udp_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_drop_udp;
		vxge_ksp->rx_marker_pdu_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_marker_pdu_frms;
		vxge_ksp->rx_lacpdu_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_lacpdu_frms;
		vxge_ksp->rx_unknown_pdu_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_unknown_pdu_frms;
		vxge_ksp->rx_marker_resp_pdu_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_marker_resp_pdu_frms;
		vxge_ksp->rx_fcs_discard_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_fcs_discard;
		vxge_ksp->rx_illegal_pdu_frms_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_illegal_pdu_frms;
		vxge_ksp->rx_switch_discard_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_switch_discard;
		vxge_ksp->rx_len_discard_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_len_discard;
		vxge_ksp->rx_rpa_discard_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_rpa_discard;
		vxge_ksp->rx_l2_mgmt_discard_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_l2_mgmt_discard;
		vxge_ksp->rx_rts_discard_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_rts_discard;
		vxge_ksp->rx_trash_discard_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_trash_discard;
		vxge_ksp->rx_buff_full_discard_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_buff_full_discard;
		vxge_ksp->rx_red_discard_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_red_discard;
		vxge_ksp->rx_xgmii_ctrl_err_cnt_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_xgmii_ctrl_err_cnt;
		vxge_ksp->rx_xgmii_data_err_cnt_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_xgmii_data_err_cnt;
		vxge_ksp->rx_xgmii_char1_match_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_xgmii_char1_match;
		vxge_ksp->rx_xgmii_err_sym_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_xgmii_err_sym;
		vxge_ksp->rx_xgmii_column1_match_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_xgmii_column1_match;
		vxge_ksp->rx_xgmii_char2_match_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_xgmii_char2_match;
		vxge_ksp->rx_local_fault_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_local_fault;
		vxge_ksp->rx_xgmii_column2_match_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_xgmii_column2_match;
		vxge_ksp->rx_jettison_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_jettison;
		vxge_ksp->rx_remote_fault_PORT0.value.ull =
		    mrpcim_stats.xgmac_port[0].rx_remote_fault;

		vxge_ksp->tx_ttl_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].tx_ttl_frms;
		vxge_ksp->tx_ttl_octets_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].tx_ttl_octets;
		vxge_ksp->tx_data_octets_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].tx_data_octets;
		vxge_ksp->tx_mcast_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].tx_mcast_frms;
		vxge_ksp->tx_bcast_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].tx_bcast_frms;
		vxge_ksp->tx_ucast_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].tx_ucast_frms;
		vxge_ksp->tx_tagged_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].tx_tagged_frms;
		vxge_ksp->tx_vld_ip_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].tx_vld_ip;
		vxge_ksp->tx_vld_ip_octets_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].tx_vld_ip_octets;
		vxge_ksp->tx_icmp_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].tx_icmp;
		vxge_ksp->tx_tcp_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].tx_tcp;
		vxge_ksp->tx_rst_tcp_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].tx_rst_tcp;
		vxge_ksp->tx_udp_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].tx_udp;
		vxge_ksp->tx_parse_error_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].tx_parse_error;
		vxge_ksp->tx_unknown_protocol_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].tx_unknown_protocol;
		vxge_ksp->tx_pause_ctrl_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].tx_pause_ctrl_frms;
		vxge_ksp->tx_marker_pdu_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].tx_marker_pdu_frms;
		vxge_ksp->tx_lacpdu_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].tx_lacpdu_frms;
		vxge_ksp->tx_drop_ip_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].tx_drop_ip;
		vxge_ksp->tx_marker_resp_pdu_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].tx_marker_resp_pdu_frms;
		vxge_ksp->tx_xgmii_char2_match_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].tx_xgmii_char2_match;
		vxge_ksp->tx_xgmii_char1_match_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].tx_xgmii_char1_match;
		vxge_ksp->tx_xgmii_column2_match_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].tx_xgmii_column2_match;
		vxge_ksp->tx_xgmii_column1_match_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].tx_xgmii_column1_match;
		vxge_ksp->tx_any_err_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].tx_any_err_frms;
		vxge_ksp->tx_drop_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].tx_drop_frms;
		vxge_ksp->rx_ttl_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_ttl_frms;
		vxge_ksp->rx_vld_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_vld_frms;
		vxge_ksp->rx_offload_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_offload_frms;
		vxge_ksp->rx_ttl_octets_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_ttl_octets;
		vxge_ksp->rx_data_octets_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_data_octets;
		vxge_ksp->rx_offload_octets_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_offload_octets;
		vxge_ksp->rx_vld_mcast_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_vld_mcast_frms;
		vxge_ksp->rx_vld_bcast_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_vld_bcast_frms;
		vxge_ksp->rx_accepted_ucast_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_accepted_ucast_frms;
		vxge_ksp->rx_accepted_nucast_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_accepted_nucast_frms;
		vxge_ksp->rx_tagged_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_tagged_frms;
		vxge_ksp->rx_long_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_long_frms;
		vxge_ksp->rx_usized_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_usized_frms;
		vxge_ksp->rx_osized_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_osized_frms;
		vxge_ksp->rx_frag_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_frag_frms;
		vxge_ksp->rx_jabber_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_jabber_frms;
		vxge_ksp->rx_ttl_64_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_ttl_64_frms;
		vxge_ksp->rx_ttl_65_127_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_ttl_65_127_frms;
		vxge_ksp->rx_ttl_128_255_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_ttl_128_255_frms;
		vxge_ksp->rx_ttl_256_511_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_ttl_256_511_frms;
		vxge_ksp->rx_ttl_512_1023_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_ttl_512_1023_frms;
		vxge_ksp->rx_ttl_1024_1518_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_ttl_1024_1518_frms;
		vxge_ksp->rx_ttl_1519_4095_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_ttl_1519_4095_frms;
		vxge_ksp->rx_ttl_4096_8191_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_ttl_4096_8191_frms;
		vxge_ksp->rx_ttl_8192_max_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_ttl_8192_max_frms;
		vxge_ksp->rx_ttl_gt_max_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_ttl_gt_max_frms;
		vxge_ksp->rx_ip_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_ip;
		vxge_ksp->rx_accepted_ip_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_accepted_ip;
		vxge_ksp->rx_ip_octets_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_ip_octets;
		vxge_ksp->rx_err_ip_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_err_ip;
		vxge_ksp->rx_icmp_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_icmp;
		vxge_ksp->rx_tcp_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_tcp;
		vxge_ksp->rx_udp_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_udp;
		vxge_ksp->rx_err_tcp_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_err_tcp;
		vxge_ksp->rx_pause_count_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_pause_count;
		vxge_ksp->rx_pause_ctrl_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_pause_ctrl_frms;
		vxge_ksp->rx_unsup_ctrl_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_unsup_ctrl_frms;
		vxge_ksp->rx_fcs_err_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_fcs_err_frms;
		vxge_ksp->rx_in_rng_len_err_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_in_rng_len_err_frms;
		vxge_ksp->rx_out_rng_len_err_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_out_rng_len_err_frms;
		vxge_ksp->rx_drop_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_drop_frms;
		vxge_ksp->rx_discarded_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_discarded_frms;
		vxge_ksp->rx_drop_ip_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_drop_ip;
		vxge_ksp->rx_drop_udp_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_drop_udp;
		vxge_ksp->rx_marker_pdu_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_marker_pdu_frms;
		vxge_ksp->rx_lacpdu_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_lacpdu_frms;
		vxge_ksp->rx_unknown_pdu_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_unknown_pdu_frms;
		vxge_ksp->rx_marker_resp_pdu_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_marker_resp_pdu_frms;
		vxge_ksp->rx_fcs_discard_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_fcs_discard;
		vxge_ksp->rx_illegal_pdu_frms_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_illegal_pdu_frms;
		vxge_ksp->rx_switch_discard_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_switch_discard;
		vxge_ksp->rx_len_discard_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_len_discard;
		vxge_ksp->rx_rpa_discard_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_rpa_discard;
		vxge_ksp->rx_l2_mgmt_discard_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_l2_mgmt_discard;
		vxge_ksp->rx_rts_discard_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_rts_discard;
		vxge_ksp->rx_trash_discard_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_trash_discard;
		vxge_ksp->rx_buff_full_discard_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_buff_full_discard;
		vxge_ksp->rx_red_discard_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_red_discard;
		vxge_ksp->rx_xgmii_ctrl_err_cnt_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_xgmii_ctrl_err_cnt;
		vxge_ksp->rx_xgmii_data_err_cnt_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_xgmii_data_err_cnt;
		vxge_ksp->rx_xgmii_char1_match_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_xgmii_char1_match;
		vxge_ksp->rx_xgmii_err_sym_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_xgmii_err_sym;
		vxge_ksp->rx_xgmii_column1_match_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_xgmii_column1_match;
		vxge_ksp->rx_xgmii_char2_match_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_xgmii_char2_match;
		vxge_ksp->rx_local_fault_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_local_fault;
		vxge_ksp->rx_xgmii_column2_match_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_xgmii_column2_match;
		vxge_ksp->rx_jettison_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_jettison;
		vxge_ksp->rx_remote_fault_PORT1.value.ull =
		    mrpcim_stats.xgmac_port[1].rx_remote_fault;

		vxge_ksp->tx_ttl_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].tx_ttl_frms;
		vxge_ksp->tx_ttl_octets_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].tx_ttl_octets;
		vxge_ksp->tx_data_octets_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].tx_data_octets;
		vxge_ksp->tx_mcast_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].tx_mcast_frms;
		vxge_ksp->tx_bcast_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].tx_bcast_frms;
		vxge_ksp->tx_ucast_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].tx_ucast_frms;
		vxge_ksp->tx_tagged_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].tx_tagged_frms;
		vxge_ksp->tx_vld_ip_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].tx_vld_ip;
		vxge_ksp->tx_vld_ip_octets_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].tx_vld_ip_octets;
		vxge_ksp->tx_icmp_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].tx_icmp;
		vxge_ksp->tx_tcp_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].tx_tcp;
		vxge_ksp->tx_rst_tcp_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].tx_rst_tcp;
		vxge_ksp->tx_udp_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].tx_udp;
		vxge_ksp->tx_parse_error_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].tx_parse_error;
		vxge_ksp->tx_unknown_protocol_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].tx_unknown_protocol;
		vxge_ksp->tx_pause_ctrl_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].tx_pause_ctrl_frms;
		vxge_ksp->tx_marker_pdu_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].tx_marker_pdu_frms;
		vxge_ksp->tx_lacpdu_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].tx_lacpdu_frms;
		vxge_ksp->tx_drop_ip_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].tx_drop_ip;
		vxge_ksp->tx_marker_resp_pdu_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].tx_marker_resp_pdu_frms;
		vxge_ksp->tx_xgmii_char2_match_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].tx_xgmii_char2_match;
		vxge_ksp->tx_xgmii_char1_match_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].tx_xgmii_char1_match;
		vxge_ksp->tx_xgmii_column2_match_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].tx_xgmii_column2_match;
		vxge_ksp->tx_xgmii_column1_match_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].tx_xgmii_column1_match;
		vxge_ksp->tx_any_err_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].tx_any_err_frms;
		vxge_ksp->tx_drop_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].tx_drop_frms;
		vxge_ksp->rx_ttl_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_ttl_frms;
		vxge_ksp->rx_vld_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_vld_frms;
		vxge_ksp->rx_offload_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_offload_frms;
		vxge_ksp->rx_ttl_octets_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_ttl_octets;
		vxge_ksp->rx_data_octets_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_data_octets;
		vxge_ksp->rx_offload_octets_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_offload_octets;
		vxge_ksp->rx_vld_mcast_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_vld_mcast_frms;
		vxge_ksp->rx_vld_bcast_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_vld_bcast_frms;
		vxge_ksp->rx_accepted_ucast_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_accepted_ucast_frms;
		vxge_ksp->rx_accepted_nucast_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_accepted_nucast_frms;
		vxge_ksp->rx_tagged_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_tagged_frms;
		vxge_ksp->rx_long_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_long_frms;
		vxge_ksp->rx_usized_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_usized_frms;
		vxge_ksp->rx_osized_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_osized_frms;
		vxge_ksp->rx_frag_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_frag_frms;
		vxge_ksp->rx_jabber_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_jabber_frms;
		vxge_ksp->rx_ttl_64_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_ttl_64_frms;
		vxge_ksp->rx_ttl_65_127_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_ttl_65_127_frms;
		vxge_ksp->rx_ttl_128_255_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_ttl_128_255_frms;
		vxge_ksp->rx_ttl_256_511_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_ttl_256_511_frms;
		vxge_ksp->rx_ttl_512_1023_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_ttl_512_1023_frms;
		vxge_ksp->rx_ttl_1024_1518_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_ttl_1024_1518_frms;
		vxge_ksp->rx_ttl_1519_4095_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_ttl_1519_4095_frms;
		vxge_ksp->rx_ttl_4096_8191_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_ttl_4096_8191_frms;
		vxge_ksp->rx_ttl_8192_max_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_ttl_8192_max_frms;
		vxge_ksp->rx_ttl_gt_max_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_ttl_gt_max_frms;
		vxge_ksp->rx_ip_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_ip;
		vxge_ksp->rx_accepted_ip_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_accepted_ip;
		vxge_ksp->rx_ip_octets_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_ip_octets;
		vxge_ksp->rx_err_ip_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_err_ip;
		vxge_ksp->rx_icmp_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_icmp;
		vxge_ksp->rx_tcp_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_tcp;
		vxge_ksp->rx_udp_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_udp;
		vxge_ksp->rx_err_tcp_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_err_tcp;
		vxge_ksp->rx_pause_count_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_pause_count;
		vxge_ksp->rx_pause_ctrl_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_pause_ctrl_frms;
		vxge_ksp->rx_unsup_ctrl_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_unsup_ctrl_frms;
		vxge_ksp->rx_fcs_err_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_fcs_err_frms;
		vxge_ksp->rx_in_rng_len_err_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_in_rng_len_err_frms;
		vxge_ksp->rx_out_rng_len_err_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_out_rng_len_err_frms;
		vxge_ksp->rx_drop_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_drop_frms;
		vxge_ksp->rx_discarded_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_discarded_frms;
		vxge_ksp->rx_drop_ip_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_drop_ip;
		vxge_ksp->rx_drop_udp_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_drop_udp;
		vxge_ksp->rx_marker_pdu_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_marker_pdu_frms;
		vxge_ksp->rx_lacpdu_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_lacpdu_frms;
		vxge_ksp->rx_unknown_pdu_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_unknown_pdu_frms;
		vxge_ksp->rx_marker_resp_pdu_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_marker_resp_pdu_frms;
		vxge_ksp->rx_fcs_discard_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_fcs_discard;
		vxge_ksp->rx_illegal_pdu_frms_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_illegal_pdu_frms;
		vxge_ksp->rx_switch_discard_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_switch_discard;
		vxge_ksp->rx_len_discard_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_len_discard;
		vxge_ksp->rx_rpa_discard_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_rpa_discard;
		vxge_ksp->rx_l2_mgmt_discard_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_l2_mgmt_discard;
		vxge_ksp->rx_rts_discard_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_rts_discard;
		vxge_ksp->rx_trash_discard_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_trash_discard;
		vxge_ksp->rx_buff_full_discard_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_buff_full_discard;
		vxge_ksp->rx_red_discard_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_red_discard;
		vxge_ksp->rx_xgmii_ctrl_err_cnt_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_xgmii_ctrl_err_cnt;
		vxge_ksp->rx_xgmii_data_err_cnt_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_xgmii_data_err_cnt;
		vxge_ksp->rx_xgmii_char1_match_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_xgmii_char1_match;
		vxge_ksp->rx_xgmii_err_sym_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_xgmii_err_sym;
		vxge_ksp->rx_xgmii_column1_match_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_xgmii_column1_match;
		vxge_ksp->rx_xgmii_char2_match_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_xgmii_char2_match;
		vxge_ksp->rx_local_fault_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_local_fault;
		vxge_ksp->rx_xgmii_column2_match_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_xgmii_column2_match;
		vxge_ksp->rx_jettison_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_jettison;
		vxge_ksp->rx_remote_fault_PORT2.value.ull =
		    mrpcim_stats.xgmac_port[2].rx_remote_fault;

		vxge_ksp->tx_frms_AGGR0.value.ull =
		    mrpcim_stats.xgmac_aggr[0].tx_frms;
		vxge_ksp->tx_data_octets_AGGR0.value.ull =
		    mrpcim_stats.xgmac_aggr[0].tx_data_octets;
		vxge_ksp->tx_mcast_frms_AGGR0.value.ull =
		    mrpcim_stats.xgmac_aggr[0].tx_mcast_frms;
		vxge_ksp->tx_bcast_frms_AGGR0.value.ull =
		    mrpcim_stats.xgmac_aggr[0].tx_bcast_frms;
		vxge_ksp->tx_discarded_frms_AGGR0.value.ull =
		    mrpcim_stats.xgmac_aggr[0].tx_discarded_frms;
		vxge_ksp->tx_errored_frms_AGGR0.value.ull =
		    mrpcim_stats.xgmac_aggr[0].tx_errored_frms;
		vxge_ksp->rx_frms_AGGR0.value.ull =
		    mrpcim_stats.xgmac_aggr[0].rx_frms;
		vxge_ksp->rx_data_octets_AGGR0.value.ull =
		    mrpcim_stats.xgmac_aggr[0].rx_data_octets;
		vxge_ksp->rx_mcast_frms_AGGR0.value.ull =
		    mrpcim_stats.xgmac_aggr[0].rx_mcast_frms;
		vxge_ksp->rx_bcast_frms_AGGR0.value.ull =
		    mrpcim_stats.xgmac_aggr[0].rx_bcast_frms;
		vxge_ksp->rx_discarded_frms_AGGR0.value.ull =
		    mrpcim_stats.xgmac_aggr[0].rx_discarded_frms;
		vxge_ksp->rx_errored_frms_AGGR0.value.ull =
		    mrpcim_stats.xgmac_aggr[0].rx_errored_frms;
		vxge_ksp->rx_ukwn_slow_proto_frms_AGGR0.value.ull =
		    mrpcim_stats.xgmac_aggr[0].rx_unknown_slow_proto_frms;

		vxge_ksp->tx_frms_AGGR1.value.ull =
		    mrpcim_stats.xgmac_aggr[1].tx_frms;
		vxge_ksp->tx_data_octets_AGGR1.value.ull =
		    mrpcim_stats.xgmac_aggr[1].tx_data_octets;
		vxge_ksp->tx_mcast_frms_AGGR1.value.ull =
		    mrpcim_stats.xgmac_aggr[1].tx_mcast_frms;
		vxge_ksp->tx_bcast_frms_AGGR1.value.ull =
		    mrpcim_stats.xgmac_aggr[1].tx_bcast_frms;
		vxge_ksp->tx_discarded_frms_AGGR1.value.ull =
		    mrpcim_stats.xgmac_aggr[1].tx_discarded_frms;
		vxge_ksp->tx_errored_frms_AGGR1.value.ull =
		    mrpcim_stats.xgmac_aggr[1].tx_errored_frms;
		vxge_ksp->rx_frms_AGGR1.value.ull =
		    mrpcim_stats.xgmac_aggr[1].rx_frms;
		vxge_ksp->rx_data_octets_AGGR1.value.ull =
		    mrpcim_stats.xgmac_aggr[1].rx_data_octets;
		vxge_ksp->rx_mcast_frms_AGGR1.value.ull =
		    mrpcim_stats.xgmac_aggr[1].rx_mcast_frms;
		vxge_ksp->rx_bcast_frms_AGGR1.value.ull =
		    mrpcim_stats.xgmac_aggr[1].rx_bcast_frms;
		vxge_ksp->rx_discarded_frms_AGGR1.value.ull =
		    mrpcim_stats.xgmac_aggr[1].rx_discarded_frms;
		vxge_ksp->rx_errored_frms_AGGR1.value.ull =
		    mrpcim_stats.xgmac_aggr[1].rx_errored_frms;
		vxge_ksp->rx_ukwn_slow_proto_frms_AGGR1.value.ull =
		    mrpcim_stats.xgmac_aggr[1].rx_unknown_slow_proto_frms;

		vxge_ksp->xgmac_global_prog_event_gnum0.value.ull =
		    mrpcim_stats.xgmac_global_prog_event_gnum0;
		vxge_ksp->xgmac_global_prog_event_gnum1.value.ull =
		    mrpcim_stats.xgmac_global_prog_event_gnum1;

		vxge_ksp->xgmac_orp_lro_events.value.ull =
		    mrpcim_stats.xgmac_orp_lro_events;
		vxge_ksp->xgmac_orp_bs_events.value.ull =
		    mrpcim_stats.xgmac_orp_bs_events;
		vxge_ksp->xgmac_orp_iwarp_events.value.ull =
		    mrpcim_stats.xgmac_orp_iwarp_events;
		vxge_ksp->xgmac_tx_permitted_frms.value.ul =
		    mrpcim_stats.xgmac_tx_permitted_frms;

		vxge_ksp->xgmac_port2_tx_any_frms.value.ul =
		    mrpcim_stats.xgmac_port2_tx_any_frms;
		vxge_ksp->xgmac_port1_tx_any_frms.value.ul =
		    mrpcim_stats.xgmac_port1_tx_any_frms;
		vxge_ksp->xgmac_port0_tx_any_frms.value.ul =
		    mrpcim_stats.xgmac_port0_tx_any_frms;

		vxge_ksp->xgmac_port2_rx_any_frms.value.ul =
		    mrpcim_stats.xgmac_port2_rx_any_frms;
		vxge_ksp->xgmac_port1_rx_any_frms.value.ul =
		    mrpcim_stats.xgmac_port1_rx_any_frms;
		vxge_ksp->xgmac_port0_rx_any_frms.value.ul =
		    mrpcim_stats.xgmac_port0_rx_any_frms;

	if (vxge_check_acc_handle(hldev->regh0) != DDI_FM_OK)
		ddi_fm_service_impact(vdev->dev_info, DDI_SERVICE_UNAFFECTED);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_UPDATE_MRPCIM_KSTATS EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (0);
}


static void
vxge_kstat_mrpcim_init(vxgedev_t *vdev)
{
	kstat_t *ksp;
	vxge_kstat_mrpcim_t *vxge_ksp;
	char vxge_str[20] = "vxge_kstat_mrpcim";

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_KSTAT_MRPCIM_INIT ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	ksp = kstat_create(VXGE_IFNAME,
	    ddi_get_instance(vdev->dev_info), vxge_str,
	    "net", KSTAT_TYPE_NAMED,
	    (sizeof (vxge_kstat_mrpcim_t) / sizeof (kstat_named_t)), 0);
	if (ksp == NULL) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: Failed to create kstat for mrpcim\n",
		    VXGE_IFNAME, vdev->instance);
		return;
	}

	vxge_ksp = (vxge_kstat_mrpcim_t *)ksp->ks_data;

	/* Fill in the ksp */
	vdev->vxge_kstat_mrpcim_ksp = ksp;

	/* Function to provide kernel stat update on demand */
	ksp->ks_update = vxge_update_mrpcim_kstats;

	/* Pointer into provider's raw statistics */
	ksp->ks_private = (void *)vdev;

	/* Initialize all the statistics */
	kstat_named_init(&vxge_ksp->pic_ini_rd_drop, "pic_ini_rd_drop",
	    KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_ini_wr_drop, "pic_ini_wr_drop",
	    KSTAT_DATA_UINT32);


	kstat_named_init(&vxge_ksp->pic_wrcrb_ph_crdt_depd_vpn0,
	    "pic_wrcrb_ph_crdt_depd_vpn0", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_wrcrb_ph_crdt_depd_vpn1,
	    "pic_wrcrb_ph_crdt_depd_vpn1", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_wrcrb_ph_crdt_depd_vpn2,
	    "pic_wrcrb_ph_crdt_depd_vpn2", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_wrcrb_ph_crdt_depd_vpn3,
	    "pic_wrcrb_ph_crdt_depd_vpn3", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_wrcrb_ph_crdt_depd_vpn4,
	    "pic_wrcrb_ph_crdt_depd_vpn4", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_wrcrb_ph_crdt_depd_vpn5,
	    "pic_wrcrb_ph_crdt_depd_vpn5", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_wrcrb_ph_crdt_depd_vpn6,
	    "pic_wrcrb_ph_crdt_depd_vpn6", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_wrcrb_ph_crdt_depd_vpn7,
	    "pic_wrcrb_ph_crdt_depd_vpn7", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_wrcrb_ph_crdt_depd_vpn8,
	    "pic_wrcrb_ph_crdt_depd_vpn8", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_wrcrb_ph_crdt_depd_vpn9,
	    "pic_wrcrb_ph_crdt_depd_vpn9", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_wrcrb_ph_crdt_depd_vpn10,
	    "pic_wrcrb_ph_crdt_depd_vpn10", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_wrcrb_ph_crdt_depd_vpn11,
	    "pic_wrcrb_ph_crdt_depd_vpn11", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_wrcrb_ph_crdt_depd_vpn12,
	    "pic_wrcrb_ph_crdt_depd_vpn12", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_wrcrb_ph_crdt_depd_vpn13,
	    "pic_wrcrb_ph_crdt_depd_vpn13", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_wrcrb_ph_crdt_depd_vpn14,
	    "pic_wrcrb_ph_crdt_depd_vpn14", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_wrcrb_ph_crdt_depd_vpn15,
	    "pic_wrcrb_ph_crdt_depd_vpn15", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_wrcrb_ph_crdt_depd_vpn16,
	    "pic_wrcrb_ph_crdt_depd_vpn16", KSTAT_DATA_UINT32);

	kstat_named_init(&vxge_ksp->pic_wrcrb_pd_crdt_depd_vpn0,
	    "pic_wrcrb_pd_crdt_depd_vpn0", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_wrcrb_pd_crdt_depd_vpn1,
	    "pic_wrcrb_pd_crdt_depd_vpn1", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_wrcrb_pd_crdt_depd_vpn2,
	    "pic_wrcrb_pd_crdt_depd_vpn2", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_wrcrb_pd_crdt_depd_vpn3,
	    "pic_wrcrb_pd_crdt_depd_vpn3", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_wrcrb_pd_crdt_depd_vpn4,
	    "pic_wrcrb_pd_crdt_depd_vpn4", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_wrcrb_pd_crdt_depd_vpn5,
	    "pic_wrcrb_pd_crdt_depd_vpn5", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_wrcrb_pd_crdt_depd_vpn6,
	    "pic_wrcrb_pd_crdt_depd_vpn6", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_wrcrb_pd_crdt_depd_vpn7,
	    "pic_wrcrb_pd_crdt_depd_vpn7", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_wrcrb_pd_crdt_depd_vpn8,
	    "pic_wrcrb_pd_crdt_depd_vpn8", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_wrcrb_pd_crdt_depd_vpn9,
	    "pic_wrcrb_pd_crdt_depd_vpn9", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_wrcrb_pd_crdt_depd_vpn10,
	    "pic_wrcrb_pd_crdt_depd_vpn10", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_wrcrb_pd_crdt_depd_vpn11,
	    "pic_wrcrb_pd_crdt_depd_vpn11", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_wrcrb_pd_crdt_depd_vpn12,
	    "pic_wrcrb_pd_crdt_depd_vpn12", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_wrcrb_pd_crdt_depd_vpn13,
	    "pic_wrcrb_pd_crdt_depd_vpn13", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_wrcrb_pd_crdt_depd_vpn14,
	    "pic_wrcrb_pd_crdt_depd_vpn14", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_wrcrb_pd_crdt_depd_vpn15,
	    "pic_wrcrb_pd_crdt_depd_vpn15", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_wrcrb_pd_crdt_depd_vpn16,
	    "pic_wrcrb_pd_crdt_depd_vpn16", KSTAT_DATA_UINT32);

	kstat_named_init(&vxge_ksp->pic_rdcrb_nph_crdt_depd_vpn0,
	    "pic_rdcrb_nph_crdt_depd_vpn0", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_rdcrb_nph_crdt_depd_vpn1,
	    "pic_rdcrb_nph_crdt_depd_vpn1", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_rdcrb_nph_crdt_depd_vpn2,
	    "pic_rdcrb_nph_crdt_depd_vpn2", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_rdcrb_nph_crdt_depd_vpn3,
	    "pic_rdcrb_nph_crdt_depd_vpn3", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_rdcrb_nph_crdt_depd_vpn4,
	    "pic_rdcrb_nph_crdt_depd_vpn4", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_rdcrb_nph_crdt_depd_vpn5,
	    "pic_rdcrb_nph_crdt_depd_vpn5", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_rdcrb_nph_crdt_depd_vpn6,
	    "pic_rdcrb_nph_crdt_depd_vpn6", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_rdcrb_nph_crdt_depd_vpn7,
	    "pic_rdcrb_nph_crdt_depd_vpn7", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_rdcrb_nph_crdt_depd_vpn8,
	    "pic_rdcrb_nph_crdt_depd_vpn8", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_rdcrb_nph_crdt_depd_vpn9,
	    "pic_rdcrb_nph_crdt_depd_vpn9", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_rdcrb_nph_crdt_depd_vpn10,
	    "pic_rdcrb_nph_crdt_depd_vpn10", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_rdcrb_nph_crdt_depd_vpn11,
	    "pic_rdcrb_nph_crdt_depd_vpn11", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_rdcrb_nph_crdt_depd_vpn12,
	    "pic_rdcrb_nph_crdt_depd_vpn12", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_rdcrb_nph_crdt_depd_vpn13,
	    "pic_rdcrb_nph_crdt_depd_vpn13", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_rdcrb_nph_crdt_depd_vpn14,
	    "pic_rdcrb_nph_crdt_depd_vpn14", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_rdcrb_nph_crdt_depd_vpn15,
	    "pic_rdcrb_nph_crdt_depd_vpn15", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_rdcrb_nph_crdt_depd_vpn16,
	    "pic_rdcrb_nph_crdt_depd_vpn16", KSTAT_DATA_UINT32);


	kstat_named_init(&vxge_ksp->pic_ini_rd_vpin_drop,
	    "pic_ini_rd_vpin_drop", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_ini_wr_vpin_drop,
	    "pic_ini_wr_vpin_drop", KSTAT_DATA_UINT32);

	kstat_named_init(&vxge_ksp->pic_genstats_count0,
	    "pic_genstats_count0", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_genstats_count1,
	    "pic_genstats_count1", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_genstats_count2,
	    "pic_genstats_count2", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_genstats_count3,
	    "pic_genstats_count3", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_genstats_count4,
	    "pic_genstats_count4", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pic_genstats_count5,
	    "pic_genstats_count5", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_rstdrop_cpl, "pci_rstdrop_cpl",
	    KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_rstdrop_msg, "pci_rstdrop_msg",
	    KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_rstdrop_client1,
	    "pci_rstdrop_client1", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_rstdrop_client0,
	    "pci_rstdrop_client0", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_rstdrop_client2,
	    "pci_rstdrop_client2", KSTAT_DATA_UINT32);

	kstat_named_init(&vxge_ksp->pci_depl_cplh_vplane0,
	    "pci_depl_cplh_vplane0", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_nph_vplane0,
	    "pci_depl_nph_vplane0", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_ph_vplane0,
	    "pci_depl_ph_vplane0", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_cplh_vplane1,
	    "pci_depl_cplh_vplane1", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_nph_vplane1,
	    "pci_depl_nph_vplane1", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_ph_vplane1,
	    "pci_depl_ph_vplane1", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_cplh_vplane2,
	    "pci_depl_cplh_vplane2", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_nph_vplane2,
	    "pci_depl_nph_vplane2", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_ph_vplane2,
	    "pci_depl_ph_vplane2", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_cplh_vplane3,
	    "pci_depl_cplh_vplane3", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_nph_vplane3,
	    "pci_depl_nph_vplane3", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_ph_vplane3,
	    "pci_depl_ph_vplane3", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_cplh_vplane4,
	    "pci_depl_cplh_vplane4", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_nph_vplane4,
	    "pci_depl_nph_vplane4", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_ph_vplane4,
	    "pci_depl_ph_vplane4", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_cplh_vplane5,
	    "pci_depl_cplh_vplane5", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_nph_vplane5,
	    "pci_depl_nph_vplane5", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_ph_vplane5,
	    "pci_depl_ph_vplane5", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_cplh_vplane6,
	    "pci_depl_cplh_vplane6", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_nph_vplane6,
	    "pci_depl_nph_vplane6", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_ph_vplane6,
	    "pci_depl_ph_vplane6", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_cplh_vplane7,
	    "pci_depl_cplh_vplane7", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_nph_vplane7,
	    "pci_depl_nph_vplane7", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_ph_vplane7,
	    "pci_depl_ph_vplane7", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_cplh_vplane8,
	    "pci_depl_cplh_vplane8", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_nph_vplane8,
	    "pci_depl_nph_vplane8", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_ph_vplane8,
	    "pci_depl_ph_vplane8", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_cplh_vplane9,
	    "pci_depl_cplh_vplane9", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_nph_vplane9,
	    "pci_depl_nph_vplane9", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_ph_vplane9,
	    "pci_depl_ph_vplane9", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_cplh_vplane10,
	    "pci_depl_cplh_vplane10", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_nph_vplane10,
	    "pci_depl_nph_vplane10", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_ph_vplane10,
	    "pci_depl_ph_vplane10", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_cplh_vplane11,
	    "pci_depl_cplh_vplane11", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_nph_vplane11,
	    "pci_depl_nph_vplane11", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_ph_vplane11,
	    "pci_depl_ph_vplane11", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_cplh_vplane12,
	    "pci_depl_cplh_vplane12", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_nph_vplane12,
	    "pci_depl_nph_vplane12", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_ph_vplane12,
	    "pci_depl_ph_vplane12", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_cplh_vplane13,
	    "pci_depl_cplh_vplane13", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_nph_vplane13,
	    "pci_depl_nph_vplane13", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_ph_vplane13,
	    "pci_depl_ph_vplane13", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_cplh_vplane14,
	    "pci_depl_cplh_vplane14", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_nph_vplane14,
	    "pci_depl_nph_vplane14", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_ph_vplane14,
	    "pci_depl_ph_vplane14", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_cplh_vplane15,
	    "pci_depl_cplh_vplane15", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_nph_vplane15,
	    "pci_depl_nph_vplane15", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_ph_vplane15,
	    "pci_depl_ph_vplane15", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_cplh_vplane16,
	    "pci_depl_cplh_vplane16", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_nph_vplane16,
	    "pci_depl_nph_vplane16", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_ph_vplane16,
	    "pci_depl_ph_vplane16", KSTAT_DATA_UINT32);

	kstat_named_init(&vxge_ksp->pci_depl_cpld_vplane0,
	    "pci_depl_cpld_vplane0", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_npd_vplane0,
	    "pci_depl_npd_vplane0", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_pd_vplane0,
	    "pci_depl_pd_vplane0", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_cpld_vplane1,
	    "pci_depl_cpld_vplane1", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_npd_vplane1,
	    "pci_depl_npd_vplane1", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_pd_vplane1,
	    "pci_depl_pd_vplane1", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_cpld_vplane2,
	    "pci_depl_cpld_vplane2", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_npd_vplane2,
	    "pci_depl_npd_vplane2", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_pd_vplane2,
	    "pci_depl_pd_vplane2", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_cpld_vplane3,
	    "pci_depl_cpld_vplane3", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_npd_vplane3,
	    "pci_depl_npd_vplane3", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_pd_vplane3,
	    "pci_depl_pd_vplane3", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_cpld_vplane4,
	    "pci_depl_cpld_vplane4", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_npd_vplane4,
	    "pci_depl_npd_vplane4", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_pd_vplane4,
	    "pci_depl_pd_vplane4", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_cpld_vplane5,
	    "pci_depl_cpld_vplane5", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_npd_vplane5,
	    "pci_depl_npd_vplane5", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_pd_vplane5,
	    "pci_depl_pd_vplane5", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_cpld_vplane6,
	    "pci_depl_cpld_vplane6", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_npd_vplane6,
	    "pci_depl_npd_vplane6", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_pd_vplane6,
	    "pci_depl_pd_vplane6", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_cpld_vplane7,
	    "pci_depl_cpld_vplane7", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_npd_vplane7,
	    "pci_depl_npd_vplane7", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_pd_vplane7,
	    "pci_depl_pd_vplane7", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_cpld_vplane8,
	    "pci_depl_cpld_vplane8", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_npd_vplane8,
	    "pci_depl_npd_vplane8", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_pd_vplane8,
	    "pci_depl_pd_vplane8", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_cpld_vplane9,
	    "pci_depl_cpld_vplane9", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_npd_vplane9,
	    "pci_depl_npd_vplane9", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_pd_vplane9,
	    "pci_depl_pd_vplane9", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_cpld_vplane10,
	    "pci_depl_cpld_vplane10", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_npd_vplane10,
	    "pci_depl_npd_vplane10", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_pd_vplane10,
	    "pci_depl_pd_vplane10", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_cpld_vplane11,
	    "pci_depl_cpld_vplane11", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_npd_vplane11,
	    "pci_depl_npd_vplane11", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_pd_vplane11,
	    "pci_depl_pd_vplane11", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_cpld_vplane12,
	    "pci_depl_cpld_vplane12", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_npd_vplane12,
	    "pci_depl_npd_vplane12", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_pd_vplane12,
	    "pci_depl_pd_vplane12", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_cpld_vplane13,
	    "pci_depl_cpld_vplane13", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_npd_vplane13,
	    "pci_depl_npd_vplane13", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_pd_vplane13,
	    "pci_depl_pd_vplane13", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_cpld_vplane14,
	    "pci_depl_cpld_vplane14", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_npd_vplane14,
	    "pci_depl_npd_vplane14", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_pd_vplane14,
	    "pci_depl_pd_vplane14", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_cpld_vplane15,
	    "pci_depl_cpld_vplane15", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_npd_vplane15,
	    "pci_depl_npd_vplane15", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_pd_vplane15,
	    "pci_depl_pd_vplane15", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_cpld_vplane16,
	    "pci_depl_cpld_vplane16", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_npd_vplane16,
	    "pci_depl_npd_vplane16", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->pci_depl_pd_vplane16,
	    "pci_depl_pd_vplane16", KSTAT_DATA_UINT32);

	kstat_named_init(&vxge_ksp->tx_ttl_frms_PORT0, "tx_ttl_frms_PORT0",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_ttl_octets_PORT0,
	    "tx_ttl_octets_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_data_octets_PORT0,
	    "tx_data_octets_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_mcast_frms_PORT0,
	    "tx_mcast_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_bcast_frms_PORT0,
	    "tx_bcast_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_ucast_frms_PORT0,
	    "tx_ucast_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_tagged_frms_PORT0,
	    "tx_tagged_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_vld_ip_PORT0, "tx_vld_ip_PORT0",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_vld_ip_octets_PORT0,
	    "tx_vld_ip_octets_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_icmp_PORT0, "tx_icmp_PORT0",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_tcp_PORT0, "tx_tcp_PORT0",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_rst_tcp_PORT0, "tx_rst_tcp_PORT0",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_udp_PORT0, "tx_udp_PORT0",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_parse_error_PORT0,
	    "tx_parse_error_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_unknown_protocol_PORT0,
	    "tx_unknown_protocol_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_pause_ctrl_frms_PORT0,
	    "tx_pause_ctrl_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_marker_pdu_frms_PORT0,
	    "tx_marker_pdu_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_lacpdu_frms_PORT0,
	    "tx_lacpdu_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_drop_ip_PORT0, "tx_drop_ip_PORT0",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_marker_resp_pdu_frms_PORT0,
	    "tx_marker_resp_pdu_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_xgmii_char2_match_PORT0,
	    "tx_xgmii_char2_match_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_xgmii_char1_match_PORT0,
	    "tx_xgmii_char1_match_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_xgmii_column2_match_PORT0,
	    "tx_xgmii_column2_match_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_xgmii_column1_match_PORT0,
	    "tx_xgmii_column1_match_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_any_err_frms_PORT0,
	    "tx_any_err_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_drop_frms_PORT0, "tx_drop_frms_PORT0",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ttl_frms_PORT0, "rx_ttl_frms_PORT0",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_vld_frms_PORT0, "rx_vld_frms_PORT0",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_offload_frms_PORT0,
	    "rx_offload_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ttl_octets_PORT0,
	    "rx_ttl_octets_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_data_octets_PORT0,
	    "rx_data_octets_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_offload_octets_PORT0,
	    "rx_offload_octets_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_vld_mcast_frms_PORT0,
	    "rx_vld_mcast_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_vld_bcast_frms_PORT0,
	    "rx_vld_bcast_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_accepted_ucast_frms_PORT0,
	    "rx_accepted_ucast_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_accepted_nucast_frms_PORT0,
	    "rx_accepted_nucast_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_tagged_frms_PORT0,
	    "rx_tagged_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_long_frms_PORT0,
	    "rx_long_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_usized_frms_PORT0,
	    "rx_usized_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_osized_frms_PORT0,
	    "rx_osized_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_frag_frms_PORT0,
	    "rx_frag_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_jabber_frms_PORT0,
	    "rx_jabber_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ttl_64_frms_PORT0,
	    "rx_ttl_64_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ttl_65_127_frms_PORT0,
	    "rx_ttl_65_127_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ttl_128_255_frms_PORT0,
	    "rx_ttl_128_255_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ttl_256_511_frms_PORT0,
	    "rx_ttl_256_511_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ttl_512_1023_frms_PORT0,
	    "rx_ttl_512_1023_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ttl_1024_1518_frms_PORT0,
	    "rx_ttl_1024_1518_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ttl_1519_4095_frms_PORT0,
	    "rx_ttl_1519_4095_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ttl_4096_8191_frms_PORT0,
	    "rx_ttl_4096_8191_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ttl_8192_max_frms_PORT0,
	    "rx_ttl_8192_max_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ttl_gt_max_frms_PORT0,
	    "rx_ttl_gt_max_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ip_PORT0, "rx_ip_PORT0",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_accepted_ip_PORT0,
	    "rx_accepted_ip_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ip_octets_PORT0,
	    "rx_ip_octets_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_err_ip_PORT0, "rx_err_ip_PORT0",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_icmp_PORT0, "rx_icmp_PORT0",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_tcp_PORT0, "rx_tcp_PORT0",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_udp_PORT0, "rx_udp_PORT0",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_err_tcp_PORT0, "rx_err_tcp_PORT0",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_pause_cnt_PORT0,
	    "rx_pause_cnt_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_pause_ctrl_frms_PORT0,
	    "rx_pause_ctrl_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_unsup_ctrl_frms_PORT0,
	    "rx_unsup_ctrl_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_fcs_err_frms_PORT0,
	    "rx_fcs_err_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_in_rng_len_err_frms_PORT0,
	    "rx_in_rng_len_err_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_out_rng_len_err_frms_PORT0,
	    "rx_out_rng_len_err_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_drop_frms_PORT0, "rx_drop_frms_PORT0",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_discarded_frms_PORT0,
	    "rx_discarded_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_drop_ip_PORT0, "rx_drop_ip_PORT0",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_drp_udp_PORT0, "rx_drp_udp_PORT0",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_marker_pdu_frms_PORT0,
	    "rx_marker_pdu_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_lacpdu_frms_PORT0,
	    "rx_lacpdu_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_unknown_pdu_frms_PORT0,
	    "rx_unknown_pdu_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_marker_resp_pdu_frms_PORT0,
	    "rx_marker_resp_pdu_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_fcs_discard_PORT0,
	    "rx_fcs_discard_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_illegal_pdu_frms_PORT0,
	    "rx_illegal_pdu_frms_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_switch_discard_PORT0,
	    "rx_switch_discard_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_len_discard_PORT0,
	    "rx_len_discard_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_rpa_discard_PORT0,
	    "rx_rpa_discard_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_l2_mgmt_discard_PORT0,
	    "rx_l2_mgmt_discard_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_rts_discard_PORT0,
	    "rx_rts_discard_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_trash_discard_PORT0,
	    "rx_trash_discard_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_buff_full_discard_PORT0,
	    "rx_buff_full_discard_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_red_discard_PORT0,
	    "rx_red_discard_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_xgmii_ctrl_err_cnt_PORT0,
	    "rx_xgmii_ctrl_err_cnt_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_xgmii_data_err_cnt_PORT0,
	    "rx_xgmii_data_err_cnt_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_xgmii_char1_match_PORT0,
	    "rx_xgmii_char1_match_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_xgmii_err_sym_PORT0,
	    "rx_xgmii_err_sym_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_xgmii_column1_match_PORT0,
	    "rx_xgmii_column1_match_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_xgmii_char2_match_PORT0,
	    "rx_xgmii_char2_match_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_local_fault_PORT0,
	    "rx_local_fault_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_xgmii_column2_match_PORT0,
	    "rx_xgmii_column2_match_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_jettison_PORT0,
	    "rx_jettison_PORT0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_remote_fault_PORT0,
	    "rx_remote_fault_PORT0", KSTAT_DATA_UINT64);

	kstat_named_init(&vxge_ksp->tx_ttl_frms_PORT1,
	    "tx_ttl_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_ttl_octets_PORT1,
	    "tx_ttl_octets_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_data_octets_PORT1,
	    "tx_data_octets_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_mcast_frms_PORT1,
	    "tx_mcast_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_bcast_frms_PORT1,
	    "tx_bcast_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_ucast_frms_PORT1,
	    "tx_ucast_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_tagged_frms_PORT1,
	    "tx_tagged_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_vld_ip_PORT1,
	    "tx_vld_ip_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_vld_ip_octets_PORT1,
	    "tx_vld_ip_octets_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_icmp_PORT1, "tx_icmp_PORT1",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_tcp_PORT1, "tx_tcp_PORT1",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_rst_tcp_PORT1, "tx_rst_tcp_PORT1",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_udp_PORT1, "tx_udp_PORT1",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_parse_error_PORT1,
	    "tx_parse_error_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_unknown_protocol_PORT1,
	    "tx_unknown_protocol_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_pause_ctrl_frms_PORT1,
	    "tx_pause_ctrl_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_marker_pdu_frms_PORT1,
	    "tx_marker_pdu_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_lacpdu_frms_PORT1,
	    "tx_lacpdu_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_drop_ip_PORT1, "tx_drop_ip_PORT1",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_marker_resp_pdu_frms_PORT1,
	    "tx_marker_resp_pdu_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_xgmii_char2_match_PORT1,
	    "tx_xgmii_char2_match_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_xgmii_char1_match_PORT1,
	    "tx_xgmii_char1_match_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_xgmii_column2_match_PORT1,
	    "tx_xgmii_column2_match_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_xgmii_column1_match_PORT1,
	    "tx_xgmii_column1_match_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_any_err_frms_PORT1,
	    "tx_any_err_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_drop_frms_PORT1, "tx_drop_frms_PORT1",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ttl_frms_PORT1, "rx_ttl_frms_PORT1",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_vld_frms_PORT1, "rx_vld_frms_PORT1",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_offload_frms_PORT1,
	    "rx_offload_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ttl_octets_PORT1,
	    "rx_ttl_octets_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_data_octets_PORT1,
	    "rx_data_octets_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_offload_octets_PORT1,
	    "rx_offload_octets_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_vld_mcast_frms_PORT1,
	    "rx_vld_mcast_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_vld_bcast_frms_PORT1,
	    "rx_vld_bcast_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_accepted_ucast_frms_PORT1,
	    "rx_accepted_ucast_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_accepted_nucast_frms_PORT1,
	    "rx_accepted_nucast_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_tagged_frms_PORT1,
	    "rx_tagged_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_long_frms_PORT1,
	    "rx_long_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_usized_frms_PORT1,
	    "rx_usized_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_osized_frms_PORT1,
	    "rx_osized_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_frag_frms_PORT1,
	    "rx_frag_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_jabber_frms_PORT1,
	    "rx_jabber_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ttl_64_frms_PORT1,
	    "rx_ttl_64_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ttl_65_127_frms_PORT1,
	    "rx_ttl_65_127_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ttl_128_255_frms_PORT1,
	    "rx_ttl_128_255_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ttl_256_511_frms_PORT1,
	    "rx_ttl_256_511_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ttl_512_1023_frms_PORT1,
	    "rx_ttl_512_1023_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ttl_1024_1518_frms_PORT1,
	    "rx_ttl_1024_1518_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ttl_1519_4095_frms_PORT1,
	    "rx_ttl_1519_4095_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ttl_4096_8191_frms_PORT1,
	    "rx_ttl_4096_8191_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ttl_8192_max_frms_PORT1,
	    "rx_ttl_8192_max_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ttl_gt_max_frms_PORT1,
	    "rx_ttl_gt_max_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ip_PORT1, "rx_ip_PORT1",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_accepted_ip_PORT1,
	    "rx_accepted_ip_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ip_octets_PORT1, "rx_ip_octets_PORT1",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_err_ip_PORT1, "rx_err_ip_PORT1",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_icmp_PORT1, "rx_icmp_PORT1",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_tcp_PORT1, "rx_tcp_PORT1",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_udp_PORT1, "rx_udp_PORT1",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_err_tcp_PORT1, "rx_err_tcp_PORT1",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_pause_count_PORT1,
	    "rx_pause_count_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_pause_ctrl_frms_PORT1,
	    "rx_pause_ctrl_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_unsup_ctrl_frms_PORT1,
	    "rx_unsup_ctrl_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_fcs_err_frms_PORT1,
	    "rx_fcs_err_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_in_rng_len_err_frms_PORT1,
	    "rx_in_rng_len_err_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_out_rng_len_err_frms_PORT1,
	    "rx_out_rng_len_err_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_drop_frms_PORT1, "rx_drop_frms_PORT1",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_discarded_frms_PORT1,
	    "rx_discarded_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_drop_ip_PORT1, "rx_drop_ip_PORT1",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_drop_udp_PORT1, "rx_drop_udp_PORT1",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_marker_pdu_frms_PORT1,
	    "rx_marker_pdu_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_lacpdu_frms_PORT1,
	    "rx_lacpdu_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_unknown_pdu_frms_PORT1,
	    "rx_unknown_pdu_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_marker_resp_pdu_frms_PORT1,
	    "rx_marker_resp_pdu_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_fcs_discard_PORT1,
	    "rx_fcs_discard_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_illegal_pdu_frms_PORT1,
	    "rx_illegal_pdu_frms_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_switch_discard_PORT1,
	    "rx_switch_discard_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_len_discard_PORT1,
	    "rx_len_discard_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_rpa_discard_PORT1,
	    "rx_rpa_discard_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_l2_mgmt_discard_PORT1,
	    "rx_l2_mgmt_discard_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_rts_discard_PORT1,
	    "rx_rts_discard_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_trash_discard_PORT1,
	    "rx_trash_discard_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_buff_full_discard_PORT1,
	    "rx_buff_full_discard_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_red_discard_PORT1,
	    "rx_red_discard_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_xgmii_ctrl_err_cnt_PORT1,
	    "rx_xgmii_ctrl_err_cnt_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_xgmii_data_err_cnt_PORT1,
	    "rx_xgmii_data_err_cnt_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_xgmii_char1_match_PORT1,
	    "rx_xgmii_char1_match_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_xgmii_err_sym_PORT1,
	    "rx_xgmii_err_sym_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_xgmii_column1_match_PORT1,
	    "rx_xgmii_column1_match_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_xgmii_char2_match_PORT1,
	    "rx_xgmii_char2_match_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_local_fault_PORT1,
	    "rx_local_fault_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_xgmii_column2_match_PORT1,
	    "rx_xgmii_column2_match_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_jettison_PORT1, "rx_jettison_PORT1",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_remote_fault_PORT1,
	    "rx_remote_fault_PORT1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_ttl_frms_PORT2, "tx_ttl_frms_PORT2",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_ttl_octets_PORT2,
	    "tx_ttl_octets_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_data_octets_PORT2,
	    "tx_data_octets_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_mcast_frms_PORT2,
	    "tx_mcast_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_bcast_frms_PORT2,
	    "tx_bcast_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_ucast_frms_PORT2,
	    "tx_ucast_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_tagged_frms_PORT2,
	    "tx_tagged_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_vld_ip_PORT2, "tx_vld_ip_PORT2",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_vld_ip_octets_PORT2,
	    "tx_vld_ip_octets_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_icmp_PORT2, "tx_icmp_PORT2",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_tcp_PORT2, "tx_tcp_PORT2",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_rst_tcp_PORT2, "tx_rst_tcp_PORT2",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_udp_PORT2, "tx_udp_PORT2",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_parse_error_PORT2,
	    "tx_parse_error_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_unknown_protocol_PORT2,
	    "tx_unknown_protocol_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_pause_ctrl_frms_PORT2,
	    "tx_pause_ctrl_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_marker_pdu_frms_PORT2,
	    "tx_marker_pdu_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_lacpdu_frms_PORT2,
	    "tx_lacpdu_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_drop_ip_PORT2, "tx_drop_ip_PORT2",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_marker_resp_pdu_frms_PORT2,
	    "tx_marker_resp_pdu_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_xgmii_char2_match_PORT2,
	    "tx_xgmii_char2_match_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_xgmii_char1_match_PORT2,
	    "tx_xgmii_char1_match_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_xgmii_column2_match_PORT2,
	    "tx_xgmii_column2_match_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_xgmii_column1_match_PORT2,
	    "tx_xgmii_column1_match_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_any_err_frms_PORT2,
	    "tx_any_err_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_drop_frms_PORT2, "tx_drop_frms_PORT2",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ttl_frms_PORT2,
	    "rx_ttl_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_vld_frms_PORT2,
	    "rx_vld_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_offload_frms_PORT2,
	    "rx_offload_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ttl_octets_PORT2,
	    "rx_ttl_octets_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_data_octets_PORT2,
	    "rx_data_octets_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_offload_octets_PORT2,
	    "rx_offload_octets_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_vld_mcast_frms_PORT2,
	    "rx_vld_mcast_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_vld_bcast_frms_PORT2,
	    "rx_vld_bcast_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_accepted_ucast_frms_PORT2,
	    "rx_accepted_ucast_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_accepted_nucast_frms_PORT2,
	    "rx_accepted_nucast_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_tagged_frms_PORT2,
	    "rx_tagged_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_long_frms_PORT2,
	    "rx_long_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_usized_frms_PORT2,
	    "rx_usized_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_osized_frms_PORT2,
	    "rx_osized_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_frag_frms_PORT2,
	    "rx_frag_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_jabber_frms_PORT2,
	    "rx_jabber_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ttl_64_frms_PORT2,
	    "rx_ttl_64_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ttl_65_127_frms_PORT2,
	    "rx_ttl_65_127_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ttl_128_255_frms_PORT2,
	    "rx_ttl_128_255_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ttl_256_511_frms_PORT2,
	    "rx_ttl_256_511_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ttl_512_1023_frms_PORT2,
	    "rx_ttl_512_1023_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ttl_1024_1518_frms_PORT2,
	    "rx_ttl_1024_1518_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ttl_1519_4095_frms_PORT2,
	    "rx_ttl_1519_4095_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ttl_4096_8191_frms_PORT2,
	    "rx_ttl_4096_8191_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ttl_8192_max_frms_PORT2,
	    "rx_ttl_8192_max_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ttl_gt_max_frms_PORT2,
	    "rx_ttl_gt_max_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ip_PORT2, "rx_ip_PORT2",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_accepted_ip_PORT2,
	    "rx_accepted_ip_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ip_octets_PORT2, "rx_ip_octets_PORT2",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_err_ip_PORT2, "rx_err_ip_PORT2",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_icmp_PORT2, "rx_icmp_PORT2",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_tcp_PORT2, "rx_tcp_PORT2",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_udp_PORT2, "rx_udp_PORT2",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_err_tcp_PORT2, "rx_err_tcp_PORT2",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_pause_count_PORT2,
	    "rx_pause_count_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_pause_ctrl_frms_PORT2,
	    "rx_pause_ctrl_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_unsup_ctrl_frms_PORT2,
	    "rx_unsup_ctrl_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_fcs_err_frms_PORT2,
	    "rx_fcs_err_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_in_rng_len_err_frms_PORT2,
	    "rx_in_rng_len_err_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_out_rng_len_err_frms_PORT2,
	    "rx_out_rng_len_err_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_drop_frms_PORT2, "rx_drop_frms_PORT2",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_discarded_frms_PORT2,
	    "rx_discarded_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_drop_ip_PORT2, "rx_drop_ip_PORT2",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_drop_udp_PORT2, "rx_drop_udp_PORT2",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_marker_pdu_frms_PORT2,
	    "rx_marker_pdu_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_lacpdu_frms_PORT2,
	    "rx_lacpdu_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_unknown_pdu_frms_PORT2,
	    "rx_unknown_pdu_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_marker_resp_pdu_frms_PORT2,
	    "rx_marker_resp_pdu_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_fcs_discard_PORT2,
	    "rx_fcs_discard_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_illegal_pdu_frms_PORT2,
	    "rx_illegal_pdu_frms_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_switch_discard_PORT2,
	    "rx_switch_discard_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_len_discard_PORT2,
	    "rx_len_discard_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_rpa_discard_PORT2,
	    "rx_rpa_discard_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_l2_mgmt_discard_PORT2,
	    "rx_l2_mgmt_discard_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_rts_discard_PORT2,
	    "rx_rts_discard_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_trash_discard_PORT2,
	    "rx_trash_discard_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_buff_full_discard_PORT2,
	    "rx_buff_full_discard_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_red_discard_PORT2,
	    "rx_red_discard_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_xgmii_ctrl_err_cnt_PORT2,
	    "rx_xgmii_ctrl_err_cnt_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_xgmii_data_err_cnt_PORT2,
	    "rx_xgmii_data_err_cnt_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_xgmii_char1_match_PORT2,
	    "rx_xgmii_char1_match_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_xgmii_err_sym_PORT2,
	    "rx_xgmii_err_sym_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_xgmii_column1_match_PORT2,
	    "rx_xgmii_column1_match_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_xgmii_char2_match_PORT2,
	    "rx_xgmii_char2_match_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_local_fault_PORT2,
	    "rx_local_fault_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_xgmii_column2_match_PORT2,
	    "rx_xgmii_column2_match_PORT2", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_jettison_PORT2, "rx_jettison_PORT2",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_remote_fault_PORT2,
	    "rx_remote_fault_PORT2", KSTAT_DATA_UINT64);

	kstat_named_init(&vxge_ksp->tx_frms_AGGR0, "tx_frms_AGGR0",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_data_octets_AGGR0,
	    "tx_data_octets_AGGR0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_mcast_frms_AGGR0,
	    "tx_mcast_frms_AGGR0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_bcast_frms_AGGR0,
	    "tx_bcast_frms_AGGR0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_discarded_frms_AGGR0,
	    "tx_discarded_frms_AGGR0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_errored_frms_AGGR0,
	    "tx_errored_frms_AGGR0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_frms_AGGR0, "rx_frms_AGGR0",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_data_octets_AGGR0,
	    "rx_data_octets_AGGR0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_mcast_frms_AGGR0,
	    "rx_mcast_frms_AGGR0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_bcast_frms_AGGR0,
	    "rx_bcast_frms_AGGR0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_discarded_frms_AGGR0,
	    "rx_discarded_frms_AGGR0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_errored_frms_AGGR0,
	    "rx_errored_frms_AGGR0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ukwn_slow_proto_frms_AGGR0,
	    "rx_ukwn_slow_proto_frms_AGGR0", KSTAT_DATA_UINT64);

	kstat_named_init(&vxge_ksp->tx_frms_AGGR1, "tx_frms_AGGR1",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_data_octets_AGGR1,
	    "tx_data_octets_AGGR1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_mcast_frms_AGGR1,
	    "tx_mcast_frms_AGGR1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_bcast_frms_AGGR1,
	    "tx_bcast_frms_AGGR1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_discarded_frms_AGGR1,
	    "tx_discarded_frms_AGGR1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->tx_errored_frms_AGGR1,
	    "tx_errored_frms_AGGR1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_frms_AGGR1, "rx_frms_AGGR1",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_data_octets_AGGR1,
	    "rx_data_octets_AGGR1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_mcast_frms_AGGR1,
	    "rx_mcast_frms_AGGR1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_bcast_frms_AGGR1,
	    "rx_bcast_frms_AGGR1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_discarded_frms_AGGR1,
	    "rx_discarded_frms_AGGR1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_errored_frms_AGGR1,
	    "rx_errored_frms_AGGR1", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_ukwn_slow_proto_frms_AGGR1,
	    "rx_ukwn_slow_proto_frms_AGGR1", KSTAT_DATA_UINT64);

	kstat_named_init(&vxge_ksp->xgmac_global_prog_event_gnum0,
	    "xgmac_global_prog_event_gnum0", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->xgmac_global_prog_event_gnum1,
	    "xgmac_global_prog_event_gnum1", KSTAT_DATA_UINT64);

	kstat_named_init(&vxge_ksp->xgmac_orp_lro_events,
	    "xgmac_orp_lro_events", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->xgmac_orp_bs_events,
	    "xgmac_orp_bs_events", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->xgmac_orp_iwarp_events,
	    "xgmac_orp_iwarp_events", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->xgmac_tx_permitted_frms,
	    "xgmac_tx_permitted_frms", KSTAT_DATA_UINT32);

	kstat_named_init(&vxge_ksp->xgmac_port2_tx_any_frms,
	    "xgmac_port2_tx_any_frms", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->xgmac_port1_tx_any_frms,
	    "xgmac_port1_tx_any_frms", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->xgmac_port0_tx_any_frms,
	    "xgmac_port0_tx_any_frms", KSTAT_DATA_UINT32);

	kstat_named_init(&vxge_ksp->xgmac_port2_rx_any_frms,
	    "xgmac_port2_rx_any_frms", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->xgmac_port1_rx_any_frms,
	    "xgmac_port1_rx_any_frms", KSTAT_DATA_UINT32);
	kstat_named_init(&vxge_ksp->xgmac_port0_rx_any_frms,
	    "xgmac_port0_rx_any_frms", KSTAT_DATA_UINT32);

	kstat_install(ksp);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_KSTAT_MRPCIM_INIT EXIT ",
	    VXGE_IFNAME, vdev->instance);
}


static void
vxge_kstat_driver_init(vxgedev_t *vdev)
{
	kstat_t *ksp;
	vxge_sw_kstats_t   *vxge_ksp;
	char vxge_str[20] = "vxge_kstat_driver";

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_KSTAT_DRIVER_INIT ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	ksp = kstat_create(VXGE_IFNAME,
	    ddi_get_instance(vdev->dev_info), vxge_str,
	    "net", KSTAT_TYPE_NAMED,
	    (sizeof (vxge_sw_kstats_t) / sizeof (kstat_named_t)), 0);
	if (ksp == NULL) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: Failed to create kstat for driver\n",
		    VXGE_IFNAME, vdev->instance);
		return;
	}

	vxge_ksp = (vxge_sw_kstats_t *)ksp->ks_data;

	/* Fill in the ksp */
	vdev->vxge_kstat_driver_ksp = ksp;

	/* Function to provide kernel stat update on demand */
	ksp->ks_update = vxge_update_driver_kstats;

	/* Pointer into provider's raw statistics */
	ksp->ks_private = (void *)vdev;

	/* Initialize all the statistics */
	kstat_named_init(&vxge_ksp->tx_frms, "tx_frms", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->txd_alloc_fail, "txd_alloc_fail",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->vpaths_open, "vpaths_open",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->vpath_open_fail, "vpath_open_fail",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rx_frms, "rx_frms", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->rxd_alloc_fail, "rxd_alloc_fail",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->link_up, "link_up", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->link_down, "link_down", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->kmem_zalloc_fail, "kmem_zalloc_fail",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->allocb_fail, "allocb_fail",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->desballoc_fail, "desballoc_fail",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->ddi_dma_alloc_handle_fail,
	    "ddi_dma_alloc_handle_fail", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->ddi_dma_mem_alloc_fail,
	    "ddi_dma_mem_alloc_fail", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->ddi_dma_addr_bind_handle_fail,
	    "ddi_dma_addr_bind_handle_fail", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->ddi_dma_addr_unbind_handle_fail,
	    "ddi_dma_addr_unbind_handle_fail", KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->kmem_alloc, "kmem_alloc",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&vxge_ksp->kmem_free, "kmem_free", KSTAT_DATA_UINT64);

	/* Add kstat to systems kstat chain */
	kstat_install(ksp);
	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_KSTAT_DRIVER_INIT EXIT ",
	    VXGE_IFNAME, vdev->instance);
}

static void
vxge_kstat_driver_destroy(vxgedev_t *vdev)
{
	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_KSTAT_DRIVER_DESTROY ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	if (vdev->vxge_kstat_driver_ksp)
		kstat_delete(vdev->vxge_kstat_driver_ksp);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_KSTAT_DRIVER_DESTROY EXIT ",
	    VXGE_IFNAME, vdev->instance);
}

/*
 * vxge_device_register
 * @vdev: pointer to valid LL device.
 *
 * This function will allocate and register network device
 */

int
vxge_device_register(vxgedev_t *vdev)
{
	mac_register_t *macp = NULL;
	vxge_hal_device_t *hldev;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_DEVICE_REGISTER ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	hldev = (vxge_hal_device_t *)vdev->devh;

	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_INIT GENLOCK", VXGE_IFNAME, vdev->instance);

	mutex_init(&vdev->soft_lock, NULL, MUTEX_DRIVER, NULL);
	mutex_init(&vdev->genlock, NULL, MUTEX_DRIVER, hldev->irqh);
	mutex_init(&vdev->soft_lock_alarm, NULL, MUTEX_DRIVER, NULL);
	mutex_init(&vdev->soft_lock_status, NULL, MUTEX_DRIVER, NULL);

	cv_init(&vdev->cv_initiate_stop, NULL, CV_DRIVER, NULL);

	macp = mac_alloc(MAC_VERSION);
	if (macp == NULL) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: unable to allocate mac structures",
		    VXGE_IFNAME, vdev->instance);
		goto vxge_register_fail;
	}

	macp->m_type_ident	= MAC_PLUGIN_IDENT_ETHER;
	macp->m_driver		= vdev;
	macp->m_dip		= vdev->dev_info;
	macp->m_src_addr	= (uint8_t *)vdev->vpaths[0].macaddr;
	macp->m_callbacks	= &vxge_m_callbacks;
	macp->m_min_sdu		= 0;
	macp->m_max_sdu		= vdev->mtu;
	macp->m_margin		= VLAN_TAGSZ;
	macp->m_priv_props	= vxge_priv_props;

	/*
	 * Finally, we're ready to register ourselves with the Nemo
	 * interface; if this succeeds, we're all ready to start()
	 */
	if (mac_register(macp, &vdev->mh) != 0) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: unable to register mac structures",
		    VXGE_IFNAME, vdev->instance);
		goto vxge_register_fail;
	}

	vxge_debug_init(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: Device registered\n",
	    VXGE_IFNAME, vdev->instance);

	/* Always free the macp after register */
	mac_free(macp);

	/* Calculate tx_copied_max using first vpath */
	vdev->tx_copied_max =
	    hldev->config.vp_config[0].fifo.max_frags *
	    hldev->config.vp_config[0].fifo.alignment_size *
	    hldev->config.vp_config[0].fifo.max_aligned_frags;


	vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
	    "%s%d: Ethernet device registered",
	    VXGE_IFNAME, vdev->instance);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_DEVICE_REGISTER EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (DDI_SUCCESS);

vxge_register_fail:
	if (macp != NULL)
		mac_free(macp);
	mutex_destroy(&vdev->genlock);
	mutex_destroy(&vdev->soft_lock);
	mutex_destroy(&vdev->soft_lock_alarm);
	mutex_destroy(&vdev->soft_lock_status);
	cv_destroy(&vdev->cv_initiate_stop);

	vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
	    "%s%d: unable to register networking device",
	    VXGE_IFNAME, vdev->instance);
	return (DDI_FAILURE);
}

/*
 * vxge_device_unregister
 * @vdev: pointer to valid LL device.
 *
 * This function will unregister and free network device
 */
int
vxge_device_unregister(vxgedev_t *vdev)
{

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_DEVICE_UNREGISTER ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	if (mac_unregister(vdev->mh) != 0) {
		vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
		    "%s%d: unable to unregister device",
		    VXGE_IFNAME, vdev->instance);
		return (DDI_FAILURE);
	}

	vxge_debug_init(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: Device unregistered\n", VXGE_IFNAME, vdev->instance);

	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_DESTROY SOFT_LOCK_ALARM",
	    VXGE_IFNAME, vdev->instance);
	mutex_destroy(&vdev->soft_lock_alarm);

	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_DESTROY SOFT_LOCK_STATUS",
	    VXGE_IFNAME, vdev->instance);
	mutex_destroy(&vdev->soft_lock_status);

	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_DESTROY SOFT_LOCK", VXGE_IFNAME, vdev->instance);
	mutex_destroy(&vdev->soft_lock);

	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_DESTROY GENLOCK", VXGE_IFNAME, vdev->instance);
	mutex_destroy(&vdev->genlock);

	cv_destroy(&vdev->cv_initiate_stop);

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_DEVICE_UNREGISTER EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (DDI_SUCCESS);
}

/*ARGSUSED*/
void
vxge_set_fma_flags(vxgedev_t *vdev, int dma_flag)
{

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_SET_FMA_FLAGS ENTRY ",
	    VXGE_IFNAME, vdev->instance);
	if (dma_flag) {
		vxge_tx_dma_attr.dma_attr_flags  = DDI_DMA_FLAGERR;
		vxge_hal_dma_attr.dma_attr_flags = DDI_DMA_FLAGERR;
	} else {
		vxge_tx_dma_attr.dma_attr_flags  = 0;
		vxge_hal_dma_attr.dma_attr_flags = 0;
	}

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_SET_FMA_FLAGS EXIT ",
	    VXGE_IFNAME, vdev->instance);
}

/* reset vpaths */
static vxge_hal_status_e
vxge_reset_vpaths(vxgedev_t *vdev)
{
	int i;
	vxge_hal_status_e status = VXGE_HAL_OK;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_RESET_VPATHS ENTRY ",
	    VXGE_IFNAME, vdev->instance);
	for (i = 0; i < vdev->no_of_vpath; i++)
		if (vdev->vpaths[i].handle) {
			if (vxge_hal_vpath_reset(vdev->vpaths[i].handle)
			    != VXGE_HAL_OK) {
				/*EMPTY*/
				vxge_debug_driver(VXGE_ERR, NULL_HLDEV,
				    NULL_VPID, "%s%d: vxge_hal_vpath_reset \
				    failed for vpath:%d\n",
				    VXGE_IFNAME, vdev->instance, i);
			}
		}
	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_RESET_VPATHS EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (status);
}

/*ARGSUSED*/
static int
vxge_set_priv_prop(vxgedev_t *vdev, const char *pr_name, uint_t pr_valsize,
    const void *pr_val)
{
	int	err = 0;


	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_SET_PRIV_PROP ENTRY ",
	    VXGE_IFNAME, vdev->instance);

	if (strcmp(pr_name, "_bar0") == 0) {
		err = vxge_bar0_set(vdev, pr_valsize, (char *)pr_val);
		return (err);
	}

	if (strcmp(pr_name, "_debug_level") == 0) {
		err = vxge_debug_ldevel_set(vdev, pr_valsize,
		    (char *)pr_val);
		return (err);
	}

	if (strcmp(pr_name, "_flow_control_gen") == 0) {
		err = vxge_flow_control_gen_set(vdev, pr_valsize,
		    (char *)pr_val);
		return (err);
	}

	if (strcmp(pr_name, "_flow_control_rcv") == 0) {
		err = vxge_flow_control_rcv_set(vdev, pr_valsize,
		    (char *)pr_val);
		return (err);
	}

	if (strcmp(pr_name, "_debug_module_mask") == 0) {
		err = vxge_debug_module_mask_set(vdev, pr_valsize,
		    (char *)pr_val);
		return (err);
	}

	if (strcmp(pr_name, "_identify") == 0) {
		err = vxge_identify_adapter(vdev, pr_valsize,
		    (char *)pr_val);
		return (err);
	}

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_SET_PRIV_PROP EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (EINVAL);
}

/*
 * vxge_send
 * @vdev: pointer to valid LL device.
 * @mblk: pointer to network buffer, i.e. mblk_t structure
 *
 * Called by the vxge_m_tx to transmit the packet to the XFRAME firmware.
 * A pointer to an M_DATA message that contains the packet is passed to
 * this routine.
 */
static boolean_t
vxge_send(vxgedev_t *vdev, mblk_t *mp)
{
	vxge_fifo_t *fifo;
	mblk_t *bp;
	boolean_t retry;
	vxge_hal_device_t *hldev = vdev->devh;
	vxge_hal_status_e status;
	vxge_hal_txdl_h dtr;
	vxge_txd_priv_t *txd_priv;
	uint32_t lsoflags;
	uint32_t hckflags;
	uint32_t mss;

	int handle_cnt, frag_cnt, ret, i, copied;
	boolean_t used_copy;
	vxge_hal_vpath_h vph;
	int vp_id, vp_num = 0;

	u32 tagged = 0;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_SEND ENTRY ",
	    VXGE_IFNAME, vdev->instance);

_begin:
	retry = B_FALSE;
	handle_cnt = frag_cnt = 0;

	if (vdev->tx_steering_type)
		vp_num = vxge_get_vpath_no(vdev, mp);

	vph = vdev->vpaths[vp_num].handle;
	vp_id = vdev->vpaths[vp_num].id;
	fifo = &(vdev->vpaths[vp_num].fifo);
	fifo->fifo_tx_cnt++;

	/*
	 * If the free Tx dtrs count reaches the lower threshold,
	 * inform the gld to stop sending more packets till the free
	 * dtrs count exceeds higher threshold. Driver informs the
	 * gld through gld_sched call, when the free dtrs count exceeds
	 * the higher threshold.
	 */
	if (vxge_hal_fifo_free_txdl_count_get(vph) <= VXGE_TX_LEVEL_LOW) {
		vxge_debug_tx(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
		    "%s%d: vpath "VXGE_OS_STXFMT": err on xmit,"
		    "free descriptors count at low threshold %d",
		    VXGE_IFNAME, vdev->instance, vph,
		    VXGE_TX_LEVEL_LOW);
		retry = B_TRUE;
		VXGE_STATS_DRV_INC(low_dtr_cnt);
		goto _exit;
	}

	status = vxge_hal_fifo_txdl_reserve(vph, &dtr, (void **)&txd_priv);
	if (status != VXGE_HAL_OK) {
		VXGE_STATS_DRV_INC(fifo_txdl_reserve_fail_cnt);
		switch (status) {
		case VXGE_HAL_INF_QUEUE_IS_NOT_READY:
			vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
			    "%s%d: vpath "VXGE_OS_STXFMT" is" \
			    "not ready.",
			    VXGE_IFNAME, vdev->instance, vph);
			retry = B_TRUE;
			goto _exit;
		case VXGE_HAL_INF_OUT_OF_DESCRIPTORS:
			vxge_debug_driver(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
			    "%s%d: vpath "VXGE_OS_STXFMT":\
			    error in xmit,"
			    " out of descriptors.", VXGE_IFNAME,
			    vdev->instance, vph);
			retry = B_TRUE;
			goto _exit;
		default:
			return (B_FALSE);
		}
	}
	vxge_debug_tx(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: Reserved TxD with vxge_hal_fifo_txdl_reserve \n",
	    VXGE_IFNAME, vdev->instance);
	txd_priv->mblk = mp;

	copied = 0;
	used_copy = B_FALSE;
	for (bp = mp; bp != NULL; bp = bp->b_cont) {
		int mblen;
		uint_t ncookies;
		ddi_dma_cookie_t dma_cookie;
		ddi_dma_handle_t dma_handle;

		/* skip zero-length message blocks */
		mblen = MBLKL(bp);
		if (mblen == 0)
			continue;

		/*
		 * Check the message length to decide to DMA or bcopy() data
		 * to tx descriptor(s).
		 */
		if (mblen < vdev->config.tx_dma_lowat &&
		    (copied + mblen) < vdev->tx_copied_max) {
			vxge_hal_status_e rc;
			rc = vxge_hal_fifo_txdl_buffer_append(vph, dtr,
			    bp->b_rptr, mblen);
			if (rc == VXGE_HAL_OK) {
				used_copy = B_TRUE;
				copied += mblen;
				continue;
			} else if (used_copy) {
				(void) vxge_hal_fifo_txdl_buffer_finalize(vph,
				    dtr, frag_cnt++);
				used_copy = B_FALSE;
			}
		} else if (used_copy) {
			(void) vxge_hal_fifo_txdl_buffer_finalize(vph, dtr,
			    frag_cnt++);
			used_copy = B_FALSE;
		}

		ret = ddi_dma_alloc_handle(vdev->dev_info, &vxge_tx_dma_attr,
		    DDI_DMA_DONTWAIT, 0, &dma_handle);
		if (ret != DDI_SUCCESS) {
			VXGE_STATS_DRV_INC(ddi_dma_alloc_handle_fail);
			vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
			    "%s%d: can not allocate dma handle",
			    VXGE_IFNAME, vdev->instance);
			goto _exit_cleanup;
		}

		ret = ddi_dma_addr_bind_handle(dma_handle, NULL,
		    (caddr_t)bp->b_rptr, mblen,
		    DDI_DMA_WRITE | DDI_DMA_STREAMING, DDI_DMA_DONTWAIT, 0,
		    &dma_cookie, &ncookies);
		if (ret != DDI_SUCCESS) {
			VXGE_STATS_DRV_INC(ddi_dma_addr_bind_handle_fail);
			goto _exit_cleanup;
		}

		switch (ret) {
		case DDI_DMA_MAPPED:
			/* everything's fine */
			break;

		case DDI_DMA_NORESOURCES:
			vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
			    "%s%d: can not bind dma address",
			    VXGE_IFNAME, vdev->instance);
			ddi_dma_free_handle(&dma_handle);
			goto _exit_cleanup;

		case DDI_DMA_NOMAPPING:
		case DDI_DMA_INUSE:
		case DDI_DMA_TOOBIG:
		default:
			/* drop packet, don't retry */
			vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
			    "%s%d: can not map message buffer",
			    VXGE_IFNAME, vdev->instance);
			ddi_dma_free_handle(&dma_handle);
			goto _exit_cleanup;
		}

		if (ncookies + frag_cnt >
		    hldev->config.vp_config[vp_id].fifo.max_frags) {
			vxge_debug_driver(VXGE_ERR, NULL_HLDEV, NULL_VPID,
			    "%s%d: too many fragments, "
			    "requested c:%d+f:%d", VXGE_IFNAME,
			    vdev->instance, ncookies, frag_cnt);
			(void) ddi_dma_unbind_handle(dma_handle);
			ddi_dma_free_handle(&dma_handle);
			goto _exit_cleanup;
		}

		/* setup the descriptors for this data buffer */
		while (ncookies) {
			vxge_hal_fifo_txdl_buffer_set(vph, dtr, frag_cnt++,
			    dma_cookie.dmac_laddress, dma_cookie.dmac_size);
			if (--ncookies)
				ddi_dma_nextcookie(dma_handle, &dma_cookie);

		}

		txd_priv->dma_handles[handle_cnt++] = dma_handle;

		if (bp->b_cont &&
		    (frag_cnt + VXGE_DEFAULT_FIFO_FRAGS_THRESHOLD >=
		    hldev->config.vp_config[vp_id].fifo.max_frags)) {
			mblk_t *nmp;

			vxge_debug_tx(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
			    "%s%d: too many FRAGs [%d],pull up them",
			    VXGE_IFNAME, vdev->instance, frag_cnt);

			nmp = msgpullup(bp->b_cont, -1);
			if (nmp == NULL) {
				vxge_debug_driver(VXGE_ERR, NULL_HLDEV,
				    NULL_VPID,
				    "%s%d: can not pullup message buffer",
				    VXGE_IFNAME, vdev->instance);
				goto _exit_cleanup;
			}
			freemsg(bp->b_cont);
			bp->b_cont = nmp;
		}
	}

	/* finalize unfinished copies */
	if (used_copy)
		(void) vxge_hal_fifo_txdl_buffer_finalize(vph, dtr, frag_cnt++);

	txd_priv->handle_cnt = handle_cnt;

	/*
	 * If LSO is required, just call vxge_hal_fifo_txdl_lso_set(dtr, mss) to
	 * do all necessary work.
	 */

	mac_lso_get(mp, &mss, &lsoflags);

	if (lsoflags & HW_LSO) {
		vxge_assert((mss != 0) && (mss <= VXGE_HAL_DEFAULT_MTU));
		vxge_hal_fifo_txdl_lso_set(dtr,
		    VXGE_HAL_FIFO_LSO_FRM_ENCAP_AUTO, mss);
	}

	hcksum_retrieve(mp, NULL, NULL, NULL, NULL, NULL, &mss, &hckflags);

	if (hckflags & HCK_IPV4_HDRCKSUM) {
		vxge_hal_fifo_txdl_cksum_set_bits(dtr,
		    VXGE_HAL_FIFO_TXD_TX_CKO_IPV4_EN);
	}
	if (hckflags & HCK_FULLCKSUM) {
		vxge_hal_fifo_txdl_cksum_set_bits(dtr,
		    VXGE_HAL_FIFO_TXD_TX_CKO_TCP_EN |
		    VXGE_HAL_FIFO_TXD_TX_CKO_UDP_EN);
	}

	(void) vxge_hal_fifo_txdl_post(vph, dtr, tagged);

	if (vxge_check_acc_handle(hldev->regh0) != DDI_FM_OK) {
		ddi_fm_service_impact(vdev->dev_info, DDI_SERVICE_DEGRADED);
		atomic_or_32(&vdev->vdev_state, VXGE_ERROR);
	}

	vxge_debug_tx(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: Posted TxD with vxge_hal_fifo_txdl_post \n",
	    VXGE_IFNAME, vdev->instance);
	VXGE_STATS_DRV_INC(tx_frms);

	return (B_TRUE);

_exit_cleanup:

	for (i = 0; i < handle_cnt; i++) {
		(void) ddi_dma_unbind_handle(txd_priv->dma_handles[i]);
		ddi_dma_free_handle(&txd_priv->dma_handles[i]);
		txd_priv->dma_handles[i] = 0;
	}

	vxge_hal_fifo_txdl_free(vph, dtr);

	freemsg(mp);

	return (B_TRUE);

_exit:
	if (retry) {
		atomic_or_32(&vdev->resched_retry, VXGE_RESHED_RETRY);
		VXGE_STATS_DRV_INC(xmit_resched_cnt);
		return (B_FALSE);
	}

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: VXGE_SEND EXIT ",
	    VXGE_IFNAME, vdev->instance);
	return (B_TRUE);
}


/*
 * vxge_m_tx
 * @arg: pointer to the vxgedev_t structure
 * @resid: resource id
 * @mp: pointer to the message buffer
 *
 * Called by MAC Layer to send a chain of packets
 */
static mblk_t *
vxge_m_tx(void *arg, mblk_t *mp)
{
	vxgedev_t *vdev = arg;
	mblk_t *next;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s: VXGE_M_TX ENTRY ", VXGE_IFNAME);

	if (!vdev->is_initialized || vdev->in_reset) {
		freemsgchain(mp);
		mp = NULL;
	}

	while (mp != NULL) {
		next = mp->b_next;
		mp->b_next = NULL;

		if (!vxge_send(vdev, mp)) {
			mp->b_next = next;
			break;
		}
		mp = next;
	}

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s: VXGE_M_TX EXIT ", VXGE_IFNAME);
	return (mp);
}

static void
vxge_check_status(void *arg)
{
	vxgedev_t *vdev;

	vdev = (vxgedev_t *)arg;

	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s: VXGE_CHECK_STATUS ENTRY ", VXGE_IFNAME);
	if ((!vdev->is_initialized))
		return;
	mutex_enter(&vdev->soft_lock_status);

	if (!vdev->soft_check_status_running) {
		vdev->soft_check_status_running = 1;
		(void) ddi_intr_trigger_softint(vdev->soft_hdl_status, vdev);
	}
	mutex_exit(&vdev->soft_lock_status);
	vxge_debug_entryexit(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s: VXGE_CHECK_STATUS EXIT ", VXGE_IFNAME);
}

static void
vxge_fm_ereport(vxgedev_t *vdev, char *detail)
{
	uint64_t ena;
	char buf[FM_MAX_CLASS];

	(void) snprintf(buf, FM_MAX_CLASS, "%s.%s", DDI_FM_DEVICE, detail);
	ena = fm_ena_generate(0, FM_ENA_FMT1);
	if (DDI_FM_EREPORT_CAP(vdev->config.fm_capabilities)) {
		ddi_fm_ereport_post(vdev->dev_info, buf, ena, DDI_NOSLEEP,
		    FM_VERSION, DATA_TYPE_UINT8, FM_EREPORT_VERS0, NULL);
	}
}

static void
vxge_tx_drain(vxgedev_t *vdev)
{
	vxge_hal_vpath_stats_sw_info_t vpath_sw_stats;
	vxge_hal_status_e status = VXGE_HAL_OK;
	boolean_t drain;
	int i, j;

	for (i = 0; i < VXGE_TX_DRAIN_TIME; i++) {
		drain = B_TRUE;
		for (j = 0; j < vdev->no_of_fifo; j++) {
			status = vxge_hal_vpath_sw_stats_get
			    (vdev->vpaths[j].handle, &vpath_sw_stats);
			if (status != VXGE_HAL_OK) {
				vxge_os_printf(
				    "vxge_hal_vpath_sw_stats_get failure %d\n",
				    status);
				drain = B_FALSE;
				break;
			}

			drain = drain &&
			    (!vpath_sw_stats.fifo_stats.common_stats.usage_cnt);
		}
		if (drain)
			break;
		else {
			vxge_os_printf("vxge delay happened, usage_cnt is %d\n",
			    vpath_sw_stats.fifo_stats.common_stats.usage_cnt);
		}

		vxge_os_mdelay(1);
	}
}

static void
vxge_vpath_rx_buff_mutex_destroy(vxge_ring_t *ring)
{
	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_DESTROY RECYCLE_LOCK",
	    VXGE_IFNAME);
	(void) atomic_cas_32(&ring->bf_pool.live, 0, VXGE_POOL_DESTROY);

	mutex_destroy(&ring->bf_pool.recycle_lock);

	vxge_debug_lock(VXGE_TRACE, NULL_HLDEV, NULL_VPID,
	    "%s%d: MUTEX_DESTROY RING->BF_POOL.POOL_LOCK",
	    VXGE_IFNAME);

	mutex_destroy(&ring->bf_pool.pool_lock);
}
