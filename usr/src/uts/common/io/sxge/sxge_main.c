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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Sun/Oracle SOL 10/40G Ethernet GLD Device Driver
 */

#include "sxge.h"
#include "sxge_mbx.h"
#include "sxge_mbx_proto.h"

static char SXGE_DESC_VER[]	= "SOL 10/40G Ethernet";

#define	SXGE_INTR_MSIX		2
#define	SXGE_INTR_MSI		1
#define	SXGE_INTR_FIXED		0

uint_t	sxge_intr_en		= 1;
uint_t	sxge_intr_msi_nreq 	= 0;
uint_t	sxge_intr_msi_en 	= SXGE_INTR_MSIX;
uint_t	sxge_intr_nf		= 0;
uint_t	sxge_intr_timer		= SXGE_INTR_TIMER;
uint_t	sxge_intr_timer_en	= 1;
uint_t	sxge_intr_dbg_en	= 0;

uint_t	sxge_gld_poll_en	= 1;
uint_t	sxge_soft_poll_en	= 0;
int	sxge_soft_intr_pri	= DDI_SOFTINT_MED;
uint_t	sxge_intr_poll_en	= 0;

uint_t	sxge_soft_poll_timer	= 100;
uint_t	sxge_soft_poll_max	= 64;
uint_t	sxge_soft_poll_outer	= 1;

uint_t	sxge_loop_fwd_en	= 0;

uint_t	sxge_vni_auto_en	= 1;
uint_t	sxge_vni_num		= 0;
uint_t	sxge_vmac_num		= 0;

uint_t	sxge_init_en		= 1;
uint_t	sxge_reg_num		= 2;

uint_t	sxge_tx_prsr_en		= 1;

uint_t	sxge_rx_rings		= 8;
uint_t	sxge_tx_rings		= 8;

uint_t	sxge_grps_en		= 1;
uint_t	sxge_rx_grps		= 1;
uint_t	sxge_tx_grps		= 1;

uint32_t sxge_tx_csum_flags	= HCKSUM_INET_FULL_V4;
				/* | HCKSUM_INET_FULL_V6; */
uint_t	sxge_tx_csum_en		= 1;
uint_t	sxge_rx_csum_en		= 1;

uint_t	sxge_tx_retries		= 10;

uint_t	sxge_rx_prom_en		= 0;
uint_t	sxge_rx_dvect		= 0xF;
uint_t	sxge_rx_opcode		= 3;

uint_t	sxge_mbx_en		= 1;
uint_t	sxge_mbx_intr_en	= 0;

uint32_t sxge_dbg_en		= 0;

uint_t	sxge_accept_jumbo	= 0;
uint_t	sxge_jumbo_frame_sz	= 9212;
uint_t	sxge_max_frame_sz	= 1518;

uint_t	sxge_link_chk_en	= 1;
uint_t	sxge_dbg_link		= 1;
uint_t	sxge_dbg_arm_cnt	= 1000;

sxge_udelay_t sxge_delay_busy	= busy;

/*
 * Local function protoypes
 */
static int sxge_register_mac(sxge_t *);
static int sxge_get_resources(sxge_t *);
static void sxge_get_config_properties(sxge_t *);
static void sxge_unconfig_properties(sxge_t *);
static void sxge_init_mutexes(sxge_t *);
static void sxge_uninit_mutexes(sxge_t *);
static int sxge_init(sxge_t *);
static void sxge_uninit(sxge_t *);
/* static int sxge_reset(sxge_t *); */
static int sxge_alloc_rings(sxge_t *);
static void sxge_free_rings(sxge_t *);
static int sxge_add_intrs(sxge_t *);
static int sxge_alloc_intrs(sxge_t *);
static int sxge_remove_intrs(sxge_t *);
static int sxge_enable_intrs(sxge_t *);
static int sxge_disable_intrs(sxge_t *);
static uint_t sxge_intr(void *, void *);
static uint_t sxge_rx_intr(void *, void *);
static uint_t sxge_tx_intr(void *, void *);
static uint_t sxge_rxvmac_intr(void *, void *);
static uint_t sxge_txvmac_intr(void *, void *);
static uint_t sxge_mbx_intr(void *, void *);
static uint_t sxge_vnierr_intr(void *, void *);
static int sxge_get_nlds(sxge_t *);
static int sxge_intr_hw_init(sxge_t *);
/* static void sxge_intr_mask_ld(sxge_t *, sxge_ldv_t *, int); */
static void sxge_intr_arm_ldg(sxge_t *, sxge_ldg_t *, boolean_t);
static void sxge_intr_arm(sxge_t *, boolean_t);
static int sxge_ldgv_init(sxge_t *, int, int);
static int sxge_ldgv_uninit(sxge_t *);
static int sxge_regs_map(sxge_t *);
static int sxge_regs_unmap(sxge_t *);

static int sxge_hosteps_mbx_reset(sxge_t *);
static int sxge_hosteps_mbx_init(sxge_t *);

static int sxge_m_start(void *);
static void sxge_m_stop(void *);
static int sxge_m_promisc(void *, boolean_t);
static int sxge_m_multicst(void *, boolean_t, const uint8_t *);
static void sxge_m_ioctl(void *, queue_t *, mblk_t *);
static boolean_t sxge_m_getcapab(void *, mac_capab_t, void *);

static int sxge_m_setprop(void *, const char *, mac_prop_id_t, uint_t,
			const void *);
static int sxge_m_getprop(void *, const char *, mac_prop_id_t,
			uint_t, void *);

static void sxge_m_propinfo(void *, const char *, mac_prop_id_t,
			mac_prop_info_handle_t);
/* static void sxge_priv_propinfo(const char *, mac_prop_info_handle_t); */

static int sxge_attach(dev_info_t *, ddi_attach_cmd_t);
static int sxge_detach(dev_info_t *, ddi_detach_cmd_t);
static int sxge_resume(sxge_t *);
static int sxge_suspend(sxge_t *);
static void sxge_unconfigure(sxge_t *);
static void sxge_unattach(sxge_t *);
static int sxge_quiesce(dev_info_t *);

static int sxge_rx_ring_start(mac_ring_driver_t, uint64_t);
static void sxge_rx_ring_stop(mac_ring_driver_t);
/* static int sxge_tx_ring_start(mac_ring_driver_t, uint64_t); */
/* static void sxge_tx_ring_stop(mac_ring_driver_t); */

static int sxge_rx_group_start(mac_group_driver_t);
static void sxge_rx_group_stop(mac_group_driver_t);
static int sxge_rx_group_add_mac(void *, const uint8_t *, uint64_t);
static int sxge_rx_group_rem_mac(void *, const uint8_t *);
/* static int sxge_vmac_init(sxge_t *); */
static int sxge_rx_vmac_init(sxge_t *, int, int);
static int sxge_rx_vmac_fini(sxge_t *, int, int);
static int sxge_tx_vmac_init(sxge_t *, int, int);
static int sxge_tx_vmac_fini(sxge_t *, int, int);

static uint_t sxge_rx_soft_poll(char *arg);
static uint_t sxge_rx_soft_poll_idx(char *arg, uint_t idx);
static void sxge_rx_soft_tmr(void *arg);
static void sxge_link_update(sxge_t *sxgep, link_state_t state);

static void sxge_err_inject(sxge_t *, queue_t *, mblk_t *);

static void sxge_loopback_ioctl(sxge_t *sxge, queue_t *wq, mblk_t *mp,
    struct iocblk *iocp);
static boolean_t sxge_set_lb(sxge_t *sxge, queue_t *wq, mblk_t *mp);

uint32_t sxge_dbg_code		= 0;
sxge_os_mutex_t	sxge_dbg_lock;
char sxge_desc_str[128];


char *sxge_priv_props[] = {
	"_adv_10gfdx_cap",
	"_adv_pause_cap",
	"_function_number",
	"_fw_version",
	"_port_mode",
	"_hot_swap_phy",
	"_rxdma_intr_time",
	"_rxdma_intr_pkts",
	"_class_opt_ipv4_tcp",
	"_class_opt_ipv4_udp",
	"_class_opt_ipv4_ah",
	"_class_opt_ipv4_sctp",
	"_class_opt_ipv6_tcp",
	"_class_opt_ipv6_udp",
	"_class_opt_ipv6_ah",
	"_class_opt_ipv6_sctp",
	"_soft_lso_enable",
	NULL
};

#define	SXGE_MAX_PRIV_PROPS	\
	(sizeof (sxge_priv_props)/sizeof (mac_priv_prop_t))

static struct cb_ops sxge_cb_ops = {
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

static struct dev_ops sxge_dev_ops = {
	DEVO_REV,		/* devo_rev */
	0,			/* devo_refcnt */
	NULL,			/* devo_getinfo */
	nulldev,		/* devo_identify */
	nulldev,		/* devo_probe */
	sxge_attach,		/* devo_attach */
	sxge_detach,		/* devo_detach */
	nodev,			/* devo_reset */
	&sxge_cb_ops,		/* devo_cb_ops */
	NULL,			/* devo_bus_ops */
	nulldev,		/* devo_power */
	sxge_quiesce		/* devo_quiesce */
};

static struct modldrv sxge_modldrv = {
	&mod_driverops,		/* Type of module.This one is a driver */
	SXGE_DESC_VER,		/* Description string */
	&sxge_dev_ops		/* driver ops */
};

static struct modlinkage sxge_modlinkage = {
	MODREV_1, &sxge_modldrv, NULL
};

/*
 * Access attributes for register mapping
 */
ddi_device_acc_attr_t sxge_regs_acc_attr = {
	DDI_DEVICE_ATTR_V0, /* V1 */
	DDI_STRUCTURE_LE_ACC,
	DDI_STRICTORDER_ACC,
	DDI_DEFAULT_ACC
};

/*
 * Device register access attributes for PIO.
 */
static ddi_device_acc_attr_t sxge_dev_reg_acc_attr = {
	DDI_DEVICE_ATTR_V1,
	DDI_STRUCTURE_LE_ACC,
	DDI_STRICTORDER_ACC,
	DDI_DEFAULT_ACC
};

/*
 * rcr, rbr and tx descriptor alignment fields
 */
ddi_dma_attr_t sxge_desc_dma_attr = {
	DMA_ATTR_V0,		/* version number. */
	0,			/* low address */
	0xffffffffffffffff,	/* high address */
	0xffffffffffffffff,	/* address counter max */
	0x100000,		/* alignment */
	0xfc00fc,		/* dlim_burstsizes */
	0x1,			/* minimum transfer size */
	0xffffffffffffffff,	/* maximum transfer size */
	0xffffffffffffffff,	/* maximum segment size */
	1,			/* scatter/gather list length */
	(unsigned int) 1,	/* granularity */
	0			/* attribute flags */
};

ddi_dma_attr_t sxge_tx_dma_attr = {
	DMA_ATTR_V0,		/* version number. */
	0,			/* low address */
	0xffffffffffffffff,	/* high address */
	0xffffffffffffffff,	/* address counter max */
#if defined(_BIG_ENDIAN)
	0x2000,			/* alignment */
#else
	0x1000,			/* alignment */
#endif
	0xfc00fc,		/* dlim_burstsizes */
	0x1,			/* minimum transfer size */
	0xffffffffffffffff,	/* maximum transfer size */
	0xffffffffffffffff,	/* maximum segment size */
	5,			/* scatter/gather list length */
	(unsigned int) 1,	/* granularity */
	0			/* attribute flags */
};

ddi_dma_attr_t sxge_rx_dma_attr = {
	DMA_ATTR_V0,		/* version number. */
	0,			/* low address */
	0xffffffffffffffff,	/* high address */
	0xffffffffffffffff,	/* address counter max */
	0x2000,			/* alignment */
	0xfc00fc,		/* dlim_burstsizes */
	0x1,			/* minimum transfer size */
	0xffffffffffffffff,	/* maximum transfer size */
	0xffffffffffffffff,	/* maximum segment size */
	1,			/* scatter/gather list length */
	(unsigned int) 1,	/* granularity */
	DDI_DMA_RELAXED_ORDERING /* attribute flags */
};

ddi_dma_lim_t sxge_dma_limits = {
	(uint_t)0,		/* dlim_addr_lo */
	(uint_t)0xffffffff,	/* dlim_addr_hi */
	(uint_t)0xffffffff,	/* dlim_cntr_max */
	(uint_t)0xfc00fc,	/* dlim_burstsizes for 32 and 64 bit xfers */
	0x1,			/* dlim_minxfer */
	1024			/* dlim_speed */
};

/*
 * Loopback property
 */
static lb_property_t lb_normal = {
	normal,	"normal", sxge_lb_normal
};

static lb_property_t lb_internal = {
	internal, "internal", sxge_lb_internal
};

#define	SXGE_M_CALLBACK_FLAGS	\
	(MC_IOCTL | MC_GETCAPAB | MC_SETPROP | MC_GETPROP | MC_PROPINFO)

static mac_callbacks_t sxge_m_callbacks = {
	SXGE_M_CALLBACK_FLAGS,
	sxge_m_stat,
	sxge_m_start,
	sxge_m_stop,
	sxge_m_promisc,
	sxge_m_multicst,
	NULL, /* sxge_m_unicst, */
	NULL, /* sxge_m_tx, */
	NULL, /* sxge_m_resources, */
	sxge_m_ioctl,
	sxge_m_getcapab,
	NULL,
	NULL,
	sxge_m_setprop,
	sxge_m_getprop,
	sxge_m_propinfo
};

/*
 * Global array of changable parameters.
 */
static sxge_param_t sxge_param_arr[] = {
	/* min	max	value	old	fcodename	name */
	{ 0,	999,	1000,	0,	"instance",	"instance"},
	{ 0,	0,	0,	0,	"rx-intr-pkts", "rx_intr_pkts"},
	{ 0,	32768,	0,	0,	"rx-intr-time", "rx_intr_time"},
	{ 0,	1,	0,	0,	"accept-jumbo", "accept_jumbo"}
};

/*
 * Module Initialization Functions.
 */

int
_init(void)
{
	int status;

	SXGE_DBG((NULL, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "==>_init"));

	MUTEX_INIT(&sxge_dbg_lock, NULL, MUTEX_DRIVER, NULL);

	mac_init_ops(&sxge_dev_ops, MODULE_NAME);

	(void) snprintf(sxge_desc_str, sizeof (sxge_desc_str), "%s",
	    SXGE_DESC_VER);

	status = mod_install(&sxge_modlinkage);

	if (status != SXGE_SUCCESS) {
		mac_fini_ops(&sxge_dev_ops);
		MUTEX_DESTROY(&sxge_dbg_lock);
	}
	SXGE_DBG((NULL, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "<==_init status = 0x%lx", status));

	return (status);
}

int
_fini(void)
{
	int status;

	SXGE_DBG((NULL, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "==>_fini"));

	status = mod_remove(&sxge_modlinkage);

	if (status != SXGE_SUCCESS) {
/* 		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI), */
/* 		"Failed to remove module")); */
		return (status);
	}

	mac_fini_ops(&sxge_dev_ops);
	MUTEX_DESTROY(&sxge_dbg_lock);

	return (status);
}

int
_info(struct modinfo *modinfop)
{
	int status;

	SXGE_DBG((NULL, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "==>_info"));
	status = mod_info(&sxge_modlinkage, modinfop);

	return (status);
}

static int
sxge_get_params(sxge_t *sxge)
{
	sxge_param_t *param_arr;
	uchar_t *prop_val;
	uint_t prop_len;
	int i;
	int status = SXGE_SUCCESS;
	uint_t iommu_pgsz;

	SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI), "==> sxge_get_params"));
	/*
	 * Read the parameters
	 */
	param_arr = sxge->param_arr;
	for (i = 0; i < sizeof (sxge_param_arr)/sizeof (sxge_param_t); i++) {
		if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, sxge->dip, 0,
		    sxge_param_arr[i].fcode_name,
		    (int **)&prop_val, &prop_len) == DDI_PROP_SUCCESS) {
			if ((*(uint32_t *)prop_val >=
			    sxge_param_arr[i].minimum) &&
			    (*(uint32_t *)prop_val <=
			    sxge_param_arr[i].maximum))
				param_arr[i].val = *(uint32_t *)prop_val;
			else {
				cmn_err(CE_CONT, "sxge%d: 'conf' file"
				    " parameter error\n", sxge->instance);
				cmn_err(CE_CONT, "Parameter keyword '%s'"
				    "is outside valid range\n",
				    param_arr[i].name);
			}
			ddi_prop_free(prop_val);
		}

		if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, sxge->dip, 0,
		    sxge_param_arr[i].name, (int **)&prop_val, &prop_len) ==
		    DDI_PROP_SUCCESS) {
			if ((*(uint32_t *)prop_val >=
			    sxge_param_arr[i].minimum) &&
			    (*(uint32_t *)prop_val <=
			    sxge_param_arr[i].maximum))
				param_arr[i].val = *(uint32_t *)prop_val;
			else {
				cmn_err(CE_CONT, "sxge%d: 'conf' file"
				    " parameter error\n", sxge->instance);
				cmn_err(CE_CONT, "Parameter keyword '%s'"
				    "is outside valid range\n",
				    param_arr[i].name);
			}
			ddi_prop_free(prop_val);
		}

		if ((i == sxge_param_accept_jumbo) && (param_arr[i].val == 1))
			sxge->accept_jumbo = 1;
	}

	/*
	 * Check if it is an adapter with its own local MAC address.
	 * If it is present, overide the system MAC address.
	 */
	if (ddi_prop_lookup_byte_array(DDI_DEV_T_ANY, sxge->dip, 0,
	    "local-mac-address", &prop_val, &prop_len) == DDI_PROP_SUCCESS) {
		if (prop_len == ETHERADDRL) {
			bcopy((uint8_t *)prop_val, sxge->factaddr, ETHERADDRL);
			SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
			    "local-mac-addr %02x:%02x:%02x:%02x:%02x:%02x",
			    prop_val[0], prop_val[1], prop_val[2],
			    prop_val[3], prop_val[4], prop_val[5]));
		}
		ddi_prop_free(prop_val);
	}

	if (ddi_prop_lookup_byte_array(DDI_DEV_T_ANY, sxge->dip, 0,
	    "local-mac-address?", &prop_val, &prop_len) == DDI_PROP_SUCCESS) {
		if (strncmp("true", (caddr_t)prop_val, (size_t)prop_len) == 0) {
			bcopy(sxge->factaddr, sxge->ouraddr, ETHERADDRL);
			SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
			    "Using local-mac address"));
		}
		ddi_prop_free(prop_val);
	}

	/*
	 * Get the system page size.
	 */

	sxge->sys_pgsz = ddi_ptob(sxge->dip, (ulong_t)1);
	iommu_pgsz = dvma_pagesize(sxge->dip);
	if (iommu_pgsz != 0) {
		if (sxge->sys_pgsz == iommu_pgsz) {
			if (iommu_pgsz > 0x4000)
				sxge->sys_pgsz = 0x4000;
		} else {
			if (sxge->sys_pgsz > iommu_pgsz)
				sxge->sys_pgsz = iommu_pgsz;
		}
	}
	sxge->sys_pgmsk = ~(sxge->sys_pgsz - 1);

	/*
	 * Store the page size in the receive memory
	 * module.
	 */
	/* sxge_rx_dma_attr.dma_attr_align = sxge->sys_pgsz; */

sxge_get_params_exit:
	SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "<== sxge_get_params status = 0x%x", status));
	return (status);
}

/* ARGSUSED */
int
sxge_attach_work(sxge_t *sxge)
{

	/* sxge_unconfigure(sxge->dip, sxge); */
	return (DDI_SUCCESS);
}

/* ARGSUSED */
int
sxge_pf_cb_handler(dev_info_t *dip, ddi_cb_action_t action,
			void *cbarg, void *arg1, void *arg2)
{
	sxge_t			 *sxge;
	pciv_config_vf_t	 *vf_config;
	/* uint32_t		sriov_bit = (1 << 18); */

	vf_config = (pciv_config_vf_t *)cbarg;
	sxge = (sxge_t *)arg1; /* arg2 unused */

	/* only one action is valid */
	if (action != DDI_CB_PCIV_CONFIG_VF) {
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"sxge_pf_cb_handler: Invalid action 0x%x", action));
		return (DDI_FAILURE);
	}

	/*
	 * Do necessary action based on cmd.
	 */
	switch (vf_config->cmd) {
	case PCIV_EVT_VFENABLE_PRE:
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"sxge_pf_cb_handler: PCIV_EVT_VFENABLE_PRE"));
		break;

	case PCIV_EVT_VFENABLE_POST:
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"sxge_pf_cb_handler: PCIV_EVT_VFENABLE_POST"));

		/* finish driver attach processing */
		if (sxge_attach_work(sxge) != DDI_SUCCESS) {
			SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
			    "sxge_attach_work return failure"));
			return (DDI_FAILURE);
		}

		/*
		 * enable virtual functions
		 */
		/* sxge_enable_vf(sxge); */ /* No need to do anything */

		break;

	case PCIV_EVT_VFDISABLE_PRE:
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"sxge_pf_cb_handler: PCIV_EVT_VFDISABLE_PRE"));
		break;

	case PCIV_EVT_VFDISABLE_POST:
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"sxge_pf_cb_handler: PCIV_EVT_VFDISABLE_POST"));
		break;

	default:
		break;
	}

	return (DDI_SUCCESS);
}

/*
 * sxge_attach - Driver attach.
 */
static int
sxge_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	sxge_t *sxge;
	/* struct sxge_hw *hw; */	/* unused in function */
	int instance;
	uint16_t pci_devid = 0, pci_venid = 0;
	int status = SXGE_SUCCESS;
	pciv_config_vf_t vfcfg;

	SXGE_DBG((NULL, (SXGE_ERR_BLK | SXGE_ERR_PRI), "==>sxge_attach"));

	switch (cmd) {
	default:
		status = SXGE_FAILURE;
		goto sxge_attach_exit;

	case DDI_RESUME:
		sxge = (sxge_t *)ddi_get_driver_private(dip);
		if (sxge == NULL) {
			status = SXGE_FAILURE;
			break;
		}
		if (sxge->dip != dip) {
			status = SXGE_FAILURE;
			break;
		}
		status = sxge_resume(sxge);
		goto sxge_attach_exit;

	case DDI_ATTACH:
		break;
	}

	/* Get the device instance */
	instance = ddi_get_instance(dip);

	/* Allocate memory for the instance data structure */
	sxge = SXGE_ZALLOC(sizeof (sxge_t));
	if (sxge == NULL) {
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"sxge_attach: Failed to alloc sxge"));
		goto sxge_attach_fail;
	}

	sxge->dip = dip;
	sxge->instance = instance;

	/* Attach the instance pointer to the dev_info data structure */
	ddi_set_driver_private(dip, sxge);

	sxge->dbg_level = sxge_dbg_code & SXGE_DBG_LVL_MSK;
	sxge->dbg_blks = (sxge_dbg_code & SXGE_DBG_BLK_MSK) >> SXGE_DBG_BLK_SH;

	SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "%d sxge_attach sxgep = %llx",
	    sxge->instance, sxge));
	/*
	 * Initialize for fma support
	 */
	sxge_fm_init(sxge, &sxge_dev_reg_acc_attr, &sxge_rx_dma_attr);
	sxge->load_state |= A_FM_INIT_DONE;

	/*
	 * Map device registers
	 */
	if (sxge_regs_map(sxge) != 0) {
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"sxge_attach: Failed to map device registers"));
		goto sxge_attach_fail;
	}
	sxge->pio_hdl.sxge = (void *)sxge;
	sxge->load_state |= A_REGS_MAP_DONE;

	/*
	 * Map PCI config space registers
	 */
	if (pci_config_setup(dip, &sxge->pcicfg_hdl.regh) != SXGE_SUCCESS) {
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"sxge_attach: Failed to map PCI configurations"));
		goto sxge_attach_fail;
	}
	sxge->pcicfg_hdl.sxge = (void *)sxge;
	sxge->load_state |= A_PCI_SETUP_DONE;

	/*
	 * Check for the following device ids (CONFIG_VF callback if PF)
	 * Specify pci.conf: num-vf (hotplug list -l)
	 * Functions:    0x2078
	 * PFs:	0x207a
	 * VFs:	0x207b
	 */

	pci_devid = SXGE_CGET16(sxge->pcicfg_hdl, PCI_CONF_DEVID);
	pci_venid = SXGE_CGET16(sxge->pcicfg_hdl, PCI_CONF_VENID);
	SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "%d sxge_attach: pci_venid %x pci_devid %x",
	    sxge->instance, pci_venid, pci_devid));

	if (pci_devid == 0x207a) {

		/* int pf_inst; */	/* unused in function */

		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"%d: sxge_attach sriov_pf instance", sxge->instance));

		/*
		 * Register SRIOV callbacks
		 */

	sxge->sriov_pf = 0;
	sxge->cb_hdl = NULL;
	sxge->num_vfs = 0;

	vfcfg.cmd = PCIV_VFCFG_PARAM;
	if (pciv_vf_config(sxge->dip, &vfcfg) == DDI_SUCCESS) {
		if (vfcfg.num_vf > 0) {
			sxge->num_vfs = vfcfg.num_vf;
			sxge->sriov_pf = sxge->num_vfs > 0 ? B_TRUE : B_FALSE;
			sxge->pf_grp = sxge->num_vfs;
		}
	} else
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		    "Failed get VF config parameters"));

	/*
	 * When acting as SR-IOV PF, register a callback and return.
	 * The rest of attach processing will be completed in the callback
	 * after transition to SR-IOV mode.
	 */
	if (sxge->sriov_pf) {
		vfcfg.cmd = PCIV_VF_ENABLE;
		if (pciv_vf_config(sxge->dip, &vfcfg) != DDI_SUCCESS) {
			SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
			    "sriov_pf - Failed to enable VF"));
		}
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		    "%d: sriov_pf - total %d VFs enabled",
		    sxge->instance, vfcfg.num_vf));

		/*
		 * Register the callback function
		 */
		status = ddi_cb_register(sxge->dip, DDI_CB_FLAG_SRIOV,
		    &sxge_pf_cb_handler, (void *)sxge,
		    NULL, &sxge->cb_hdl);
		if (status != DDI_SUCCESS) {
			SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
			"sriov_pf - Failed to register callback for SRIOV, "
			"error status: %d", status));
		}
		sxge->load_state |= A_SRIOV_CB_DONE;
	}

	}

	/*
	 * Get the resources allocated to this function
	 */
	if (!sxge_vni_auto_en) {
		sxge->num_tx_rings = sxge_tx_rings;
		sxge->num_rx_rings = sxge_rx_rings;
		sxge->num_rx_groups = sxge_rx_grps;
		sxge->num_tx_groups = sxge_tx_grps;
	} else {
		if (sxge_get_resources(sxge) != 0) {
			SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
			    "sxge_attach: Could not get resources"));
			goto sxge_attach_fail;
		}
	}

	sxge->load_state |= A_GET_RES_DONE;

	/*
	 * Setup the parameters for this instance.
	 */

	(void) sxge_get_params(sxge);
	sxge->load_state |= A_PARAM_INIT_DONE;

	/* get config properties */
	sxge_get_config_properties(sxge);
	sxge->load_state |= A_CFG_PROPS_DONE;

	/*
	 * Setup the Kstats for the driver.
	 */
	(void) sxge_init_stats(sxge);
	sxge->load_state |= A_STATS_INIT_DONE;

	sxge_init_mutexes(sxge);
	sxge->load_state |= A_MUTEX_INIT_DONE;

	sxge->load_state |= A_LINK_INIT_DONE;

	if (SXGE_FM_CHECK_ACC_HANDLE(sxge, sxge->pio_hdl.regh) != DDI_FM_OK) {
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		    "%d Bad register acc handle", sxge->instance));

		goto sxge_attach_fail;
	}

	/*
	 * Allocate rx/tx rings.
	 */
	if (sxge_alloc_rings(sxge) != 0) {
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"sxge_attach: Failed to allocate rx and tx rings"));
		goto sxge_attach_fail;
	}
	sxge->load_state |= A_RINGS_INIT_DONE;

	/*
	 * Register the driver to the MAC
	 */
	if (sxge_register_mac(sxge) != 0) {
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"sxge_attach: Failed to register MAC"));
		goto sxge_attach_fail;
	}
	mac_link_update(sxge->mach, LINK_STATE_UNKNOWN);
	sxge->load_state |= A_MAC_REG_DONE;

sxge_attach_exit:
	SXGE_DBG((sxge, (SXGE_INIT_BLK | SXGE_INFO_PRI), "sxge_attach_exit"));
	SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI), "<==sxge_attach_exit"));
	return (status);

sxge_attach_fail:
	sxge_unconfigure(sxge);
	SXGE_DBG((NULL, (SXGE_ERR_BLK | SXGE_ERR_PRI), "sxge_attach_fail"));
	return (SXGE_FAILURE);
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
sxge_quiesce(dev_info_t *dip)
{
	sxge_t *sxge;
	/* struct sxge_hw *hw; */	/* set but not used */

	sxge = (sxge_t *)ddi_get_driver_private(dip);

	if (sxge == NULL)
		return (DDI_FAILURE);

	/* hw = &sxge->hw; */	/* set but not used */
	sxge_unattach(sxge);
	return (DDI_SUCCESS);
}


/*
 * sxge_detach - Driver detach.
 */
static int
sxge_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	int status = SXGE_SUCCESS;
	/* int instance; */	/* set but not used */
	sxge_t *sxge;

	/* instance = ddi_get_instance(dip); */	/* set but not used */
	sxge = (sxge_t *)ddi_get_driver_private(dip);
	SXGE_DBG((sxge, (SXGE_UNINIT_BLK | SXGE_INFO_PRI), "==>sxge_detach"));

	if (sxge == NULL)
		return (SXGE_FAILURE);

	/*
	 * Check detach command
	 */
	switch (cmd) {
	case DDI_SUSPEND:
		sxge->suspended = DDI_SUSPEND;
		return (sxge_suspend(sxge));

	case DDI_DETACH:
		sxge->suspended = DDI_DETACH;
		break;

	default:
		return (SXGE_FAILURE);

	}

	sxge_unattach(sxge);
	sxge = NULL;

	SXGE_DBG((sxge, (SXGE_UNINIT_BLK | SXGE_INFO_PRI),
	"<==sxge_detach: ddi_status[%d]", status));

	return (status);
}

static void
sxge_unattach(sxge_t *sxge)
{
	if (sxge == NULL) {
		return;
	}

	/*
	 * If the device is still running, it needs to be stopped first.
	 * This check is necessary because under some specific circumstances,
	 * the detach routine can be called without stopping the interface
	 * first.
	 */
	MUTEX_ENTER(&sxge->gen_lock);

	if (sxge->sxge_state & SXGE_STARTED) {
		sxge->sxge_state &= ~SXGE_STARTED;
		sxge_uninit(sxge);

		/* Disable and stop the watchdog timer */
		if (sxge->sxge_timerid) {
			sxge_stop_timer(sxge, sxge->sxge_timerid);
			sxge->sxge_timerid = 0;
		}
	}

	MUTEX_EXIT(&sxge->gen_lock);

	SXGE_DBG((sxge, (SXGE_UNINIT_BLK | SXGE_INFO_PRI), "<==sxge_unattach"));

	sxge_unconfigure(sxge);
}

static void
sxge_unconfigure(sxge_t *sxge)
{
	if (sxge->load_state & A_EN_INTR_DONE)
		(void) sxge_disable_intrs(sxge);

	if (sxge->load_state & A_SRIOV_CB_DONE) {
		if (sxge->sriov_pf && sxge->cb_hdl)
		if (ddi_cb_unregister(sxge->cb_hdl) != DDI_SUCCESS) {
			SXGE_DBG((sxge, (SXGE_UNINIT_BLK | SXGE_INFO_PRI),
			"Failed to unregister callback for SRIOV"));
		}
	}

	if (sxge->load_state & A_MAC_REG_DONE)
		(void) mac_unregister(sxge->mach);

	if (sxge->load_state & A_INTR_ADD_DONE)
		(void) sxge_remove_intrs(sxge);

	if (sxge->load_state & A_RINGS_INIT_DONE)
		(void) sxge_free_rings(sxge);

	if (sxge->load_state & A_MUTEX_INIT_DONE)
		sxge_uninit_mutexes(sxge);

	if (sxge->load_state & A_STATS_INIT_DONE)
		(void) sxge_uninit_stats(sxge);

	if (sxge->load_state & A_CFG_PROPS_DONE)
		(void) sxge_unconfig_properties(sxge);

	if (sxge->load_state & A_PARAM_INIT_DONE) {
	/*
	 * Remove the list of parameters which were setup during attach.
	 */
		if (sxge->dip)
			(void) ddi_prop_remove_all(sxge->dip);
	}

	if (sxge->load_state & A_REGS_MAP_DONE)
		(void) sxge_regs_unmap(sxge);

	if (sxge->load_state & A_FM_INIT_DONE)
		(void) sxge_fm_fini(sxge);

	SXGE_DBG((sxge, (SXGE_UNINIT_BLK | SXGE_INFO_PRI),
	    "<==sxge_unconfigure"));

	ddi_set_driver_private(sxge->dip, NULL);
	SXGE_FREE(sxge, sizeof (sxge_t));
}

/*
 * sxge_register_mac - Register the driver and its function pointers with
 * the GLD interface.
 */
static int
sxge_register_mac(sxge_t *sxge)
{
	/* struct sxge_hw *hw = &sxge->hw; */	/* set but not used */
	mac_register_t *mac;
	int status = 0;

	if ((mac = mac_alloc(MAC_VERSION)) == NULL)
		return (ENOMEM);

	mac->m_type_ident = MAC_PLUGIN_IDENT_ETHER;
	mac->m_driver = sxge;
	mac->m_dip = sxge->dip;

	{

		/*
		 * Read mac-id from the ombx[7] location
		 */
		uint64_t val = SXGE_GET64(sxge->pio_hdl, NIU_OMB_ENTRY(7));

		sxge->vmac.addr[0] = (uint8_t)((val >> 40) & 0xff);
		sxge->vmac.addr[1] = (uint8_t)((val >> 32) & 0xff);
		sxge->vmac.addr[2] = (uint8_t)((val >> 24) & 0xff);
		sxge->vmac.addr[3] = (uint8_t)((val >> 16) & 0xff);
		sxge->vmac.addr[4] = (uint8_t)((val >> 8) & 0xff);
		sxge->vmac.addr[5] = (uint8_t)(val & 0xff);

		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		    "MAC addr from NIU OMB 7 [%2x:%2x:%2x:%2x:%2x:%2x]",
		    sxge->vmac.addr[0], sxge->vmac.addr[1],
		    sxge->vmac.addr[2], sxge->vmac.addr[3],
		    sxge->vmac.addr[4], sxge->vmac.addr[5]));

		mac->m_src_addr = sxge->vmac.addr;

	}

	sxge->vmac.maxframesize = (sxge_accept_jumbo || sxge->accept_jumbo)?
	    (uint16_t)sxge_jumbo_frame_sz:(uint16_t)sxge_max_frame_sz;
	mac->m_callbacks = &sxge_m_callbacks;
	mac->m_min_sdu = 0;
	mac->m_max_sdu = sxge->vmac.maxframesize - SXGE_EHEADER_VLAN;
	mac->m_margin = VLAN_TAGSZ;
	mac->m_flags = 0;

	status = mac_register(mac, &sxge->mach);

	mac_free(mac);

	SXGE_DBG((sxge, (SXGE_GLD_BLK | SXGE_INFO_PRI),
	"sxge_register_mac status %d", status));

	return (status);
}

/*
 * sxge_init_locks - Initialize locks.
 */
static void
sxge_init_mutexes(sxge_t *sxge)
{
	cv_init(&sxge->nbw_cv, NULL, CV_DRIVER, NULL);
	MUTEX_INIT(&sxge->nbw_lock, NULL, MUTEX_DRIVER, NULL);
	MUTEX_INIT(&sxge->gen_lock, NULL, MUTEX_DRIVER, NULL);
	MUTEX_INIT(&sxge->mbox_lock, NULL, MUTEX_DRIVER, NULL);

/* do other HW related mutex init here */
}

/*
 * sxge_destroy_locks - Destroy locks.
 */
static void
sxge_uninit_mutexes(sxge_t *sxge)
{
	MUTEX_DESTROY(&sxge->mbox_lock);
	MUTEX_DESTROY(&sxge->gen_lock);
	MUTEX_DESTROY(&sxge->nbw_lock);
	cv_destroy(&sxge->nbw_cv);
/* do other HW related mutex uninit here */

}

/*
 * sxge_add_intrs - Add interrupts.
 */
static int
sxge_add_intrs(sxge_t *sxge)
{
	int		intr_types;
	int		type = 0;
	int		stat = SXGE_SUCCESS;

	SXGE_DBG((sxge, (SXGE_GLD_BLK | SXGE_INFO_PRI), "==>sxge_add_intrs"));

	sxge->intr.registered = B_FALSE;
	sxge->intr.enabled = B_FALSE;
	sxge->intr.msi_intx_cnt = 0;
	sxge->intr.intr_added = 0;
	sxge->intr.msi_enable = B_FALSE;
	sxge->intr.type = 0;

	if (sxge_intr_msi_en) {
		sxge->intr.msi_enable = B_TRUE;
	}

	/* Get the supported interrupt types */
	if ((stat = ddi_intr_get_supported_types(sxge->dip, &intr_types))
	    != SXGE_SUCCESS) {
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"<== sxge_add_intrs: ddi_intr_get_supported_types failed"
		" status 0x%08x", stat));
		stat = SXGE_FAILURE;
		return (stat);
	}
	sxge->intr.sup_types = intr_types;
	SXGE_DBG((sxge, (SXGE_GLD_BLK | SXGE_INFO_PRI),
	"sxge_add_intrs: supported types[%d]", intr_types));


	/*
	 * sxge_intr_msi_en:
	 *	1 - MSI		2 - MSI-X	others - FIXED
	 */
	switch (sxge_intr_msi_en) {
	default:
		type = DDI_INTR_TYPE_FIXED;
		SXGE_DBG((sxge, (SXGE_GLD_BLK | SXGE_INFO_PRI),
		"sxge_add_intrs: use fixed (intx emulation) type %08x",
		    type));
		break;

	case SXGE_INTR_MSIX:
		if (intr_types & DDI_INTR_TYPE_MSIX) {
			type = DDI_INTR_TYPE_MSIX;
		} else if (intr_types & DDI_INTR_TYPE_MSI) {
			type = DDI_INTR_TYPE_MSI;
		} else if (intr_types & DDI_INTR_TYPE_FIXED) {
			type = DDI_INTR_TYPE_FIXED;
		}
		break;

	case SXGE_INTR_MSI:
		if (intr_types & DDI_INTR_TYPE_MSI) {
			type = DDI_INTR_TYPE_MSI;
		} else if (intr_types & DDI_INTR_TYPE_MSIX) {
			type = DDI_INTR_TYPE_MSIX;
		} else if (intr_types & DDI_INTR_TYPE_FIXED) {
			type = DDI_INTR_TYPE_FIXED;
		}
	}

	sxge->intr.type = type;

	if ((stat = sxge_alloc_intrs(sxge)) != DDI_SUCCESS) {
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"<== sxge_add_intrs: failed to alloc interrupts"));
		stat = SXGE_FAILURE;
		return (stat);
	}

	sxge->intr.registered = B_TRUE;

	SXGE_DBG((sxge, (SXGE_GLD_BLK | SXGE_INFO_PRI), "<==sxge_add_intrs"));

	return (stat);
}

static int
sxge_get_nlds(sxge_t *sxge) {

	int i, nlds;

	nlds = 0;

	for (i = 0; i < SXGE_MAX_VNI_NUM; i++) {
		if (sxge->vni_arr[i].present == 1) {
			nlds += (2 * sxge->vni_arr[i].dma_cnt);
			nlds += (2 * sxge->vni_arr[i].vmac_cnt);
			nlds += 2; /* 1 for Mbox, 1 for VNI err */
		}
	}

	return (nlds);
}

/* ARGSUSED */
static int
sxge_alloc_intrs(sxge_t *sxge)
{
	dev_info_t		 *dip = sxge->dip;
	uint32_t		int_type;
	sxge_ldg_t		 *ldgp;
	sxge_intr_t		 *intrp;
	uint_t			 *inthandler;
	void			 *arg1, *arg2;
	int			behavior;
	int			nintrs, navail, nrequest;
	int			nactual;
	/* int			nrequired; */	/* set but not used */
	int			inum = 0;
	int			i, j;
	int			stat = SXGE_SUCCESS;

	SXGE_DBG((sxge, (SXGE_GLD_BLK | SXGE_INFO_PRI),
	"==> sxge_alloc_intrs"));

	intrp = (sxge_intr_t *)&sxge->intr;
	int_type = intrp->type;

	stat = ddi_intr_get_nintrs(dip, int_type, &nintrs);
	if ((stat != SXGE_SUCCESS) || (nintrs == 0)) {
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"<== sxge_alloc_intrs: ddi_intr_get_nintrs failed"
		"status: 0x%x, nintrs %d", stat, nintrs));
		return (stat);
	}

	stat = ddi_intr_get_navail(dip, int_type, &navail);
	if ((stat != SXGE_SUCCESS) || (navail == 0)) {
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"<== sxge_alloc_intrs: ddi_intr_get_navail failed"
		"status: 0x%x, navail %d", stat, navail));
		return (stat);
	}

	if (sxge_intr_msi_nreq)
		navail = sxge_intr_msi_nreq;

	nrequest = sxge_get_nlds(sxge);

	if (nrequest < navail)
		navail = nrequest;

	if (int_type == DDI_INTR_TYPE_MSI && !ISP2(navail)) {
		/* MSI must be power of 2 */
		if ((navail & 16) == 16) {
			navail = 16;
		} else if ((navail & 8) == 8) {
			navail = 8;
		} else if ((navail & 4) == 4) {
			navail = 4;
		} else if ((navail & 2) == 2) {
			navail = 2;
		} else {
			navail = 1;
		}

		SXGE_DBG((sxge, (SXGE_GLD_BLK | SXGE_INFO_PRI),
		    "sxge_alloc_intrs: nintrs %d, MSI navail %d",
		    nintrs, navail));
	}

	behavior = DDI_INTR_ALLOC_NORMAL;
	intrp->intr_size = navail * sizeof (ddi_intr_handle_t);
	intrp->htable = SXGE_ALLOC(intrp->intr_size);

	stat = ddi_intr_alloc(dip, intrp->htable, int_type, inum,
	    navail, &nactual, behavior);

	if (stat != SXGE_SUCCESS || nactual == 0) {
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"<== sxge_alloc_intrs: ddi_intr_alloc failed"
		"status: 0x%x", stat));
		SXGE_FREE(intrp->htable, intrp->intr_size);
		return (stat);
	}

	if ((stat = ddi_intr_get_pri(intrp->htable[0],
	    (uint_t *)&intrp->pri)) != SXGE_SUCCESS) {
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"<== sxge_alloc_intrs: ddi_intr_get_pri failed"
		"status: 0x%x", stat));
		/* Free already allocated interrupts */
		for (i = 0; i < nactual; i++) {
			(void) ddi_intr_free(intrp->htable[i]);
		}

		SXGE_FREE(intrp->htable, intrp->intr_size);
		return (stat);
	}

	/* nrequired = 0; */	/* set but not used */
	stat = sxge_ldgv_init(sxge, nactual, nrequest);
	if (stat != 0) {
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"<== sxge_alloc_intrs: sxge_ldgv_init failed"
		"status: 0x%x", stat));
		/* Free already allocated interrupts */
		for (i = 0; i < nactual; i++) {
			(void) ddi_intr_free(intrp->htable[i]);
		}

		SXGE_FREE(intrp->htable, intrp->intr_size);
		return (stat);
	}

	ldgp = sxge->ldgvp->ldgp;
	for (i = 0; i < sxge->ldgvp->ldg_intrs; i++, ldgp++) {

		arg1 = ldgp->ldvp;
		arg2 = sxge;
		if (ldgp->nldvs == 1) {
			inthandler = (uint_t *)ldgp->ldvp->intr_handler;
		} else if (ldgp->nldvs > 1) {
			inthandler = (uint_t *)ldgp->sys_intr_handler;
		}

		if ((stat = ddi_intr_add_handler(intrp->htable[i],
		    (ddi_intr_handler_t *)inthandler, arg1, arg2))
		    != SXGE_SUCCESS) {
			SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
			"<== sxge_alloc_intrs:ddi_intr_add_handler failed"
			"status: 0x%x", stat));
			for (j = 0; j < intrp->intr_added; j++) {
				(void) ddi_intr_remove_handler(
				    intrp->htable[j]);
			}
			/* Free already allocated intr */
			for (j = 0; j < nactual; j++) {
				(void) ddi_intr_free(intrp->htable[j]);
			}
			SXGE_FREE(intrp->htable, intrp->intr_size);

			(void) sxge_ldgv_uninit(sxge);

			return (stat);
		}
		intrp->intr_added++;
		SXGE_DBG((sxge, (SXGE_GLD_BLK | SXGE_INFO_PRI),
		    "sxge_alloc_intrs: ddi_intr_add_handler: \
		    i %d, nldvs %d, added %d",
		    i, ldgp->nldvs, intrp->intr_added));
	}

	intrp->msi_intx_cnt = nactual;

	SXGE_DBG((sxge, (SXGE_GLD_BLK | SXGE_INFO_PRI), "sxge_alloc_intrs:"
	    "Requested: %d, Allowed: %d msi_intx_cnt %d intr_added %d",
	    navail, nactual, intrp->msi_intx_cnt, intrp->intr_added));

	(void) ddi_intr_get_cap(intrp->htable[0], &intrp->intr_cap);

	(void) sxge_intr_hw_init(sxge);

	SXGE_DBG((sxge, (SXGE_GLD_BLK | SXGE_INFO_PRI),
	"<== sxge_alloc_intrs"));

	return (stat);
}

static int
sxge_intr_hw_init(sxge_t *sxge)
{
	int			i;
	intr_ld_msk_gnum_t	val;
	sxge_ldv_t		 *ldvp = sxge->ldgvp->ldvp;

	SXGE_DBG((sxge, (SXGE_GLD_BLK | SXGE_INFO_PRI),
	"==> sxge_intr_hw_init: maxldvs %d", sxge->ldgvp->maxldvs));

	for (i = 0; i < sxge->ldgvp->ldv_intrs; i++) {
		val.value = 0;
		if (ldvp->ldgp)
			val.bits.ldw.ldg_num = ldvp->ldgp->ldg;
		else
			SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
			"sxge_intr_hw_init: ldvp->ldgp NULL %d", i));
		val.bits.ldw.ldf_mask = ldvp->ldf_masks;
		val.bits.ldw.en_ldg_wr = 1;
		val.bits.ldw.en_mask_wr = 1;
		SXGE_PUT64(sxge->pio_hdl,
		    INTR_LD_MSK_GNUM(ldvp->vni_num, ldvp->nf, ldvp->ldv),
		    val.value);
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		    "sxge_intr_hw_init: addr %llx, msk_gnum %llx",
		    INTR_LD_MSK_GNUM(ldvp->vni_num, ldvp->nf, ldvp->ldv),
		    val.value));
		ldvp++;
	}

	/* sxge_intr_arm(sxge, B_TRUE); */ /* can cause px0 blocked */
	return (0);
}

static void
sxge_intr_arm_ldg(sxge_t *sxge, sxge_ldg_t *ldgp, boolean_t arm)
{
	intr_ldgimgn_t		val;

	if (arm && !ldgp->arm)
		ldgp->arm = B_TRUE;
	else if (!arm && ldgp->arm)
		ldgp->arm = B_FALSE;
	val.value = 0;
	val.bits.ldw.timer = ldgp->timer;
	val.bits.ldw.arm = ldgp->arm;
	SXGE_PUT64(sxge->pio_hdl, INTR_LDGIMGN(ldgp->ldg), val.value);

}

static void
sxge_intr_arm(sxge_t *sxge, boolean_t arm)
{
	int			i;
	intr_ldgimgn_t		val;
	sxge_ldg_t		 *ldgp = sxge->ldgvp->ldgp;

	for (i = 0; i < sxge->ldgvp->ldg_intrs; i++) {
		if (arm && !ldgp->arm)
			ldgp->arm = B_TRUE;
		else if (!arm && ldgp->arm)
			ldgp->arm = B_FALSE;
		val.value = 0;
		val.bits.ldw.timer = ldgp->timer;
		if (sxge_intr_timer_en) val.bits.ldw.timer = sxge_intr_timer;
		val.bits.ldw.arm = ldgp->arm;
		SXGE_PUT64(sxge->pio_hdl, INTR_LDGIMGN(ldgp->ldg), val.value);
		ldgp++;
	}
}

/*
 * sxge_remove_intrs - Remove interrupts.
 */
static int
sxge_remove_intrs(sxge_t *sxge)
{
	/* int		i; */	/* unused */
	int		inum;
	sxge_intr_t	 *intrp;

	SXGE_DBG((sxge, (SXGE_GLD_BLK | SXGE_INFO_PRI),
	"==>sxge_remove_intrs"));

	intrp = (sxge_intr_t *)&sxge->intr;
	if (!intrp->registered) {
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"<== sxge_remove_intrs: interrupts not registered"));
		return (-1);
	}

	for (inum = 0; inum < intrp->intr_added; inum++) {
		if (intrp->htable[inum]) {
			(void) ddi_intr_remove_handler(intrp->htable[inum]);
		}
	}

	for (inum = 0; inum < intrp->msi_intx_cnt; inum++) {
		if (intrp->htable[inum])
			(void) ddi_intr_free(intrp->htable[inum]);
	}

	SXGE_FREE(intrp->htable, intrp->intr_size);
	intrp->registered = B_FALSE;
	intrp->enabled = B_FALSE;
	intrp->msi_intx_cnt = 0;
	intrp->intr_added = 0;

	(void) sxge_ldgv_uninit(sxge);

	SXGE_DBG((sxge, (SXGE_GLD_BLK | SXGE_INFO_PRI),
	"<==sxge_remove_intrs"));

	return (0);
}
/*
 * sxge_enable_intrs - Enable interrupts.
 */
static int
sxge_enable_intrs(sxge_t *sxge)
{
	sxge_intr_t	 *intrp;
	int		i;
	int		status;

	SXGE_DBG((sxge, (SXGE_GLD_BLK | SXGE_INFO_PRI),
	"==>sxge_enable_intrs"));

	intrp = (sxge_intr_t *)&sxge->intr;

	if (!intrp->registered) {
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"<== sxge_enable_intrs: interrupts not registered"));
		return (-1);
	}

	if (intrp->enabled) {
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"<== sxge_enable_intrs: already enabled"));
		return (0);
	}

	if (intrp->intr_cap & DDI_INTR_FLAG_BLOCK) {
		status = ddi_intr_block_enable(intrp->htable,
		    intrp->intr_added);

		if (status == SXGE_SUCCESS) intrp->enabled = B_TRUE;
	} else {
		for (i = 0; i < intrp->intr_added; i++) {
			status = ddi_intr_enable(intrp->htable[i]);

			if (status == SXGE_SUCCESS) intrp->enabled = B_TRUE;
		}
	}

	sxge_intr_arm(sxge, B_TRUE);

	SXGE_DBG((sxge, (SXGE_GLD_BLK | SXGE_INFO_PRI),
	"<==sxge_enable_intrs"));
	return (0);
}

/*
 * sxge_disable_intrs - disable interrupts.
 */
static int
sxge_disable_intrs(sxge_t *sxge)
{
	sxge_intr_t	 *intrp;
	int		i;

	SXGE_DBG((sxge, (SXGE_GLD_BLK | SXGE_INFO_PRI),
	"==>sxge_disable_intrs"));

	intrp = (sxge_intr_t *)&sxge->intr;

	sxge_intr_arm(sxge, B_FALSE);

	if (!intrp->registered) {
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"<== sxge_disable_intrs: interrupts not registered"));
		return (-1);
	}

	if (intrp->intr_cap & DDI_INTR_FLAG_BLOCK) {
		(void) ddi_intr_block_disable(intrp->htable,
		    intrp->intr_added);
	} else {
		for (i = 0; i < intrp->intr_added; i++) {
			(void) ddi_intr_disable(intrp->htable[i]);
		}
	}

	intrp->enabled = B_FALSE;
	SXGE_DBG((sxge, (SXGE_GLD_BLK | SXGE_INFO_PRI),
	"<==sxge_disable_intrs"));
	return (0);
}

static int
sxge_ldgv_init(sxge_t *sxge, int intr_avail, int intr_req)
{
	int		i, j, k, intr_remaining;
	/* int		ngrps; */	/* set but not used */
	int		vni, vmac;
	sxge_ldg_t	 *ldgp, *ptr;
	sxge_ldv_t	 *ldvp;
	uint_t		rx_intr_pref = 0;
	uint_t		vni_intr_pref = 0;

	SXGE_DBG((sxge, (SXGE_GLD_BLK | SXGE_INFO_PRI),
	    "==>sxge_ldgv_init: maxldgs %d, maxldvs %d",
	    intr_avail, intr_req));

	if (intr_avail <= 0 || intr_req <= 0) {
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"<== sxge_ldgv_init: No intrs available"));
		return (-1);
	}

	/* ngrps = (intr_avail < intr_req) ? intr_avail : intr_req; */
	/* set but not used */

	if (sxge->ldgvp == NULL) {
		sxge->ldgvp = SXGE_ZALLOC(sizeof (sxge_ldgv_t));
		sxge->ldgvp->maxldgs = (uint8_t)intr_avail;
		sxge->ldgvp->maxldvs = (uint8_t)intr_req;
		sxge->ldgvp->ldg_intrs = 0;
		sxge->ldgvp->ldv_intrs = 0;
		sxge->ldgvp->ldgp = SXGE_ZALLOC(sizeof (sxge_ldg_t) *
		    sxge->ldgvp->maxldgs);
		sxge->ldgvp->ldvp = SXGE_ZALLOC(sizeof (sxge_ldv_t) *
		    sxge->ldgvp->maxldvs);
	}

	if (sxge->ldgvp == NULL || sxge->ldgvp->ldgp == NULL ||
	    sxge->ldgvp->ldvp == NULL) {
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"<== sxge_ldgv_init: Out of memory"));
		return (-1);
	}

	ptr = sxge->ldgvp->ldgp;
	for (i = 0; i < sxge->ldgvp->maxldgs; i++) {
		ptr->ldg = (uint8_t)i;
		ptr->arm = B_TRUE;
		ptr->nldvs = 0;
		ptr->timer = (uint16_t)0;
		if (sxge_intr_timer_en) ptr->timer = (uint16_t)sxge_intr_timer;
		ptr->sys_intr_handler = sxge_intr;
		ptr->sxge = sxge;
		ptr->ldvp = NULL;
		ptr->vni_cnt = 0;
		ptr->next = NULL;
		ptr++;
	}
	ldgp = sxge->ldgvp->ldgp;
	ldvp = sxge->ldgvp->ldvp;

	intr_remaining = intr_avail;

	if (intr_remaining == 0) {
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		    "sxge_ldgv_init: returning -1 intr_remaning = 0"));
		return (-1);
	}

	if (intr_remaining >= (sxge->dma_cnt + sxge->vni_cnt)) rx_intr_pref = 1;
	if (intr_remaining >= (sxge->vni_cnt)) vni_intr_pref = 1;

	SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "sxge_ldgv_init: avail %d rx_intr_pref %d vni_intr_ref %d",
	    intr_remaining, rx_intr_pref, vni_intr_pref));

	for (i = 0, k = 0; i < SXGE_MAX_VNI_NUM; i++) {
		if (sxge->vni_arr[i].present == 1) {
			vni = i;
			for (j = 0; j < SXGE_MAX_VNI_VMAC_NUM; j++)
				if (sxge->vni_arr[vni].vmac[j] == 1) {
					vmac = j;
					break;
				}

			/* rx dma interrupts */
			for (j = 0; j < SXGE_MAX_VNI_DMA_NUM; j++) {
				if (sxge->vni_arr[vni].dma[j] == 1) {
					ldgp = sxge->ldgvp->ldgp + k;
					if (rx_intr_pref) ldvp->next = NULL;
					else ldvp->next = ldgp->ldvp;
					ldgp->ldvp = ldvp;
					ldgp->vni_num = (uint8_t)vni;
					ldgp->nf = (uint8_t)vmac;
					ldgp->nldvs++;

					ldvp->vni_num = (uint8_t)vni;
					ldvp->nf = (uint8_t)vmac;
					ldvp->ldv = (uint8_t)LD_RXDMA(j);
					ldvp->use_timer = B_FALSE;
					ldvp->ldgp = ldgp;
					ldvp->ldf_masks = 0;
					ldvp->intr_handler = sxge_rx_intr;
					ldvp->sxge = sxge;
					ldvp->idx = sxge->vni_arr[vni].didx[j];
					ldvp++;
					sxge->ldgvp->ldv_intrs++;
					if (rx_intr_pref) {
					k++; intr_remaining--;
					sxge->ldgvp->ldg_intrs++;
					ldgp->vni_cnt++;
					}
				}
			}

			/* tx dma interrupts */
			for (j = 0; j < SXGE_MAX_VNI_DMA_NUM; j++) {
				if (sxge->vni_arr[vni].dma[j] == 1) {
					ldgp = sxge->ldgvp->ldgp + k;
					ldvp->next = ldgp->ldvp;
					ldgp->ldvp = ldvp;
					ldgp->vni_num = (uint8_t)vni;
					ldgp->nf = (uint8_t)vmac;
					ldgp->nldvs++;

					ldvp->vni_num = (uint8_t)vni;
					ldvp->nf = (uint8_t)vmac;
					ldvp->ldv = LD_TXDMA(j);
					ldvp->use_timer = B_FALSE;
					ldvp->ldgp = ldgp;
					ldvp->ldf_masks = 0;
					ldvp->intr_handler = sxge_tx_intr;
					ldvp->sxge = sxge;
					ldvp->idx = sxge->vni_arr[vni].didx[j];
					ldvp++;
					sxge->ldgvp->ldv_intrs++;
				}
			}

			/* rx vmac interrupts */
			{
					ldgp = sxge->ldgvp->ldgp + k;
					ldvp->next = ldgp->ldvp;
					ldgp->ldvp = ldvp;
					ldgp->vni_num = (uint8_t)vni;
					ldgp->nf = (uint8_t)vmac;
					ldgp->nldvs++;

					ldvp->vni_num = (uint8_t)vni;
					ldvp->nf = (uint8_t)vmac;
					ldvp->ldv = LD_RXVMAC;
					ldvp->use_timer = B_FALSE;
					ldvp->ldgp = ldgp;
					ldvp->ldf_masks = 0;
					ldvp->intr_handler = sxge_rxvmac_intr;
					ldvp->sxge = sxge;
					ldvp->idx = vmac;
					ldvp++;
					sxge->ldgvp->ldv_intrs++;
			}

			/* tx vmac interrupts */
			{
					ldgp = sxge->ldgvp->ldgp + k;
					ldvp->next = ldgp->ldvp;
					ldgp->ldvp = ldvp;
					ldgp->vni_num = (uint8_t)vni;
					ldgp->nf = (uint8_t)vmac;
					ldgp->nldvs++;

					ldvp->vni_num = (uint8_t)vni;
					ldvp->nf = (uint8_t)vmac;
					ldvp->ldv = LD_TXVMAC;
					ldvp->use_timer = B_FALSE;
					ldvp->ldgp = ldgp;
					ldvp->ldf_masks = 0;
					ldvp->intr_handler = sxge_txvmac_intr;
					ldvp->sxge = sxge;
					ldvp->idx = vmac;
					ldvp++;
					sxge->ldgvp->ldv_intrs++;
			}

			/* mailbox interrupts */
			if (sxge_mbx_intr_en) {
					ldgp = sxge->ldgvp->ldgp + k;
					ldvp->next = ldgp->ldvp;
					ldgp->ldvp = ldvp;
					ldgp->vni_num = (uint8_t)vni;
					ldgp->nf = (uint8_t)vmac;
					ldgp->nldvs++;

					ldvp->vni_num = (uint8_t)vni;
					ldvp->nf = (uint8_t)vmac;
					ldvp->ldv = LD_MAILBOX;
					ldvp->use_timer = B_FALSE;
					ldvp->ldgp = ldgp;
					ldvp->ldf_masks = 0;
					ldvp->intr_handler = sxge_mbx_intr;
					ldvp->sxge = sxge;
					ldvp->idx = vmac;
					ldvp++;
					sxge->ldgvp->ldv_intrs++;
			}

			/* vnierror interrupts */
			{
					ldgp = sxge->ldgvp->ldgp + k;
					ldvp->next = ldgp->ldvp;
					ldgp->ldvp = ldvp;
					ldgp->vni_num = (uint8_t)vni;
					ldgp->nf = (uint8_t)vmac;
					ldgp->nldvs++;

					ldvp->vni_num = (uint8_t)vni;
					ldvp->nf = (uint8_t)vmac;
					ldvp->ldv = LD_VNI_ERROR;
					ldvp->use_timer = B_FALSE;
					ldvp->ldgp = ldgp;
					ldvp->ldf_masks = 0;
					ldvp->intr_handler = sxge_vnierr_intr;
					ldvp->sxge = sxge;
					ldvp->idx = vmac;
					ldvp++;
					sxge->ldgvp->ldv_intrs++;
			}

			ldgp->vni_cnt++;
			if (vni_intr_pref) {
			k++; intr_remaining--;
			sxge->ldgvp->ldg_intrs++;
			}
		}
	} /* for */

	if (rx_intr_pref == 0 && vni_intr_pref == 0) sxge->ldgvp->ldg_intrs++;

/*	ldgvp->tmres = SXGE_TIMER_RESO; */ /* this is set in EPS */

sxge_ldgv_init_exit:

	SXGE_DBG((sxge, (SXGE_GLD_BLK | SXGE_INFO_PRI), "<==sxge_ldgv_init"));

	return (0);
}

static int
sxge_ldgv_uninit(sxge_t *sxge)
{
	/*
	 * uint_t			i;
	 * sxge_ldg_t		*ldgp, *ptr;
	 * sxge_ldv_t		*ldvp;
	 */
	/* unused */
	sxge_ldgv_t		*ldgvp = sxge->ldgvp;

	if (ldgvp == NULL) {
		SXGE_DBG((sxge, (SXGE_GLD_BLK | SXGE_INFO_PRI),
		"<== sxge_ldgv_uninit: no logical group configured."));
		return (-1);
	}

	if (ldgvp->ldgp) {
		SXGE_FREE(ldgvp->ldgp, sizeof (sxge_ldg_t) * ldgvp->maxldgs);
	}
	if (ldgvp->ldvp) {
		SXGE_FREE(ldgvp->ldvp, sizeof (sxge_ldv_t) * ldgvp->maxldvs);
	}
	SXGE_FREE(ldgvp, sizeof (sxge_ldgv_t));
	sxge->ldgvp = NULL;

	return (0);
}

static uint_t
sxge_intr(void *arg1, void *arg2)
{
	uint_t serviced = DDI_INTR_UNCLAIMED;
	sxge_ldv_t *ldvp = (sxge_ldv_t *)arg1;
	sxge_t *sxge = (sxge_t *)arg2;
	sxge_ldg_t *ldgp;
	uint64_t ldsv;
	sxge_intr_t *intrp = (sxge_intr_t *)&sxge->intr;
	int int_type = intrp->type;
	intr_ldgimgn_t	val;
	int i;

	SXGE_DBG0((sxge, (SXGE_INTR_BLK | SXGE_INFO_PRI),
	    "==>sxge_intr: %llx, %llx", arg1, arg2));

	if (arg1 == NULL) {
		SXGE_DBG((NULL, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"<==sxge_intr: arg1 LD NULL"));
		return (serviced);
	}

	if (arg2 == NULL || (void *)ldvp->sxge != arg2) {
		sxge = ldvp->sxge;
	}

	if (!(sxge->sxge_state & SXGE_INITIALIZED)) {
		SXGE_DBG((NULL, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"<==sxge_intr: not initialized"));
		return (serviced);
	}
	ldgp = ldvp->ldgp;

	if (ldgp == NULL) {
		SXGE_DBG((NULL, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"<==sxge_intr: ldgp NULL"));
		return (serviced);
	}

	if (int_type == DDI_INTR_TYPE_FIXED) {
		val.value = SXGE_GET64(sxge->pio_hdl, INTR_LDGIMGN(ldgp->ldg));
		if (val.bits.ldw.arm) {
			SXGE_DBG((NULL, (SXGE_ERR_BLK | SXGE_ERR_PRI),
			    "sxge_intr: intx ignored ldg %d", ldgp->ldg));
			/* serviced = DDI_INTR_CLAIMED; */
			return (serviced);
		}
	}

	/*
	 * This interrupt handler will have to go through all the logical
	 * devices to find out which logical device interrupted and then call
	 * its handler to process the events.
	 */
	serviced = DDI_INTR_CLAIMED;

	for (i = 0, ldvp = ldgp->ldvp; (ldvp != NULL) && (i < ldgp->nldvs);
	    i++, ldvp = ldvp->next) {
		ldsv = SXGE_GET64(sxge->pio_hdl,
		    INTR_LDSV(ldvp->vni_num, ldvp->nf, ldvp->ldgp->ldg));

		if (LDSV0_ON(ldvp->ldv, ldsv) || LDSV1_ON(ldvp->ldv, ldsv)) {
			(void) (ldvp->intr_handler)((void *)ldvp, (void *)sxge);
		}
	}

	/* Re-arm the group */
	sxge_intr_arm_ldg(sxge, ldgp, B_TRUE);

	return (serviced);
}

static uint_t
sxge_rx_intr(void *arg1, void *arg2)
{
	uint_t serviced = DDI_INTR_UNCLAIMED;
	sxge_ldv_t *ldvp = (sxge_ldv_t *)arg1;
	sxge_t *sxge = (sxge_t *)arg2;
	sxge_ldg_t *ldgp;
	uint64_t ldsv;
	/* uint_t i; */	/* unused */
	/* uint64_t addr; */ /* unused */
	rdc_state_t *ring;

	if (arg1 == NULL) {
		SXGE_DBG((NULL, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"<==sxge_rx_intr: arg1 LD NULL"));
		return (serviced);
	}

	if (arg2 == NULL || (void *)ldvp->sxge != arg2) {
		sxge = ldvp->sxge;
	}

	SXGE_DBG0((sxge, (SXGE_INTR_BLK | SXGE_INFO_PRI), "==>sxge_rx_intr"));

	if (sxge_intr_dbg_en)
		SXGE_DBG((NULL, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"%d: sxge_rx_intr..issued %d", sxge->instance, ldvp->ldv));

	if (!(sxge->sxge_state & SXGE_INITIALIZED)) {
		SXGE_DBG((NULL, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"<==sxge_rx_intr: not initialized"));
		return (serviced);
	}
	ldgp = ldvp->ldgp;

	if (ldgp == NULL) {
		SXGE_DBG((NULL, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"<==sxge_rx_intr: ldgp NULL"));
		return (serviced);
	}

	serviced = DDI_INTR_CLAIMED;

	ldsv = SXGE_GET64(sxge->pio_hdl,
	    INTR_LDSV(ldvp->vni_num, ldvp->nf, ldvp->ldgp->ldg));

	if (LDSV0_ON(ldvp->ldv, ldsv)) {
		/* Normal datapath interrupt - pkt received */

		if (sxge_intr_dbg_en)
		SXGE_DBG((NULL, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		    "%d: sxge_rx_intr ldg %d ldv %d ldsv x%lx idx %d",
		    sxge->instance, ldvp->ldgp->ldg,
		    ldvp->ldv, ldsv, ldvp->idx));

		(void) sxge_rx_soft_poll_idx(arg2, (uint_t)ldvp->idx); /* ldv */

		/* sxge_intr_mask_ld(sxge, ldvp, 0); */
	} else if (LDSV1_ON(ldvp->ldv, ldsv)) {
		ring = &sxge->rdc[(uint_t)ldvp->idx];
		MUTEX_ENTER(&sxge->rdc_lock[(uint_t)ldvp->idx]);
		(void) sxge_rdc_errs(ring);
		MUTEX_EXIT(&sxge->rdc_lock[(uint_t)ldvp->idx]);
	} else {
		SXGE_DBG((NULL, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"sxge_rx_intr: INTR received, but no flags set on LDSV"));
		if (ldgp->nldvs == 1 && sxge_intr_en && ldsv != 0) {
		SXGE_PUT64(sxge->pio_hdl,
		    INTR_LDSV(ldgp->vni_num, ldgp->nf, ldgp->ldg),
		    0xC0DEBADDULL);
		}
	}

	/* re-arm grp if this is the only device */
	if (ldgp->nldvs == 1 && sxge_intr_en) {
		sxge_intr_arm_ldg(sxge, ldgp, B_TRUE);
	}
	return (serviced);
}

static uint_t
sxge_tx_intr(void *arg1, void *arg2)
{
	uint_t serviced = DDI_INTR_UNCLAIMED;
	sxge_ldv_t *ldvp = (sxge_ldv_t *)arg1;
	sxge_t *sxge = (sxge_t *)arg2;
	sxge_ldg_t *ldgp;
	tdc_state_t *ring;

	if (arg1 == NULL) {
		SXGE_DBG((NULL, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"<==sxge_tx_intr: arg1 LD NULL"));
		return (serviced);
	}

	if (arg2 == NULL || (void *)ldvp->sxge != arg2) {
		sxge = ldvp->sxge;
	}

	SXGE_DBG0((sxge, (SXGE_GLD_BLK | SXGE_INFO_PRI), "==>sxge_tx_intr"));

	if (!(sxge->sxge_state & SXGE_INITIALIZED)) {
		SXGE_DBG((NULL, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"<==sxge_tx_intr: not initialized"));
		return (serviced);
	}
	ldgp = ldvp->ldgp;

	if (ldgp == NULL) {
		SXGE_DBG((NULL, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"<==sxge_tx_intr: ldgp NULL"));
		return (serviced);
	}
	serviced = DDI_INTR_CLAIMED;

	ring = &sxge->tdc[(uint_t)ldvp->idx];
	MUTEX_ENTER(&sxge->tdc_lock[(uint_t)ldvp->idx]);
	(void) sxge_tdc_errs(ring);
	MUTEX_EXIT(&sxge->tdc_lock[(uint_t)ldvp->idx]);

	/* re-arm grp if this is the only device */
	if (ldgp->nldvs == 1) {
		sxge_intr_arm_ldg(sxge, ldgp, B_TRUE);
	}
	return (serviced);
}

static uint_t
sxge_rxvmac_intr(void *arg1, void *arg2)
{
	uint_t serviced = DDI_INTR_UNCLAIMED;
	sxge_ldv_t *ldvp = (sxge_ldv_t *)arg1;
	sxge_t *sxge = (sxge_t *)arg2;
	sxge_ldg_t *ldgp;
	uint64_t rxvmac_stat;
	int vni_num, nf;

	if (arg1 == NULL) {
		SXGE_DBG((NULL, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"<==sxge_rxvmac_intr: arg1 LD NULL"));
		return (serviced);
	}

	if (arg2 == NULL || (void *)ldvp->sxge != arg2) {
		sxge = ldvp->sxge;
	}

	SXGE_DBG0((sxge, (SXGE_GLD_BLK | SXGE_INFO_PRI),
	    "%d:sxge_rxvmac_intr vni %d vmac %d",
	    sxge->instance, ldvp->vni_num, ldvp->idx));

	if (!(sxge->sxge_state & SXGE_INITIALIZED)) {
		SXGE_DBG((NULL, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"<==sxge_rxvmac_intr: not initialized"));
		return (serviced);
	}
	ldgp = ldvp->ldgp;

	if (ldgp == NULL) {
		SXGE_DBG((NULL, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"<==sxge_rxvmac_intr: ldgp NULL"));
		return (serviced);
	}
	serviced = DDI_INTR_CLAIMED;
	vni_num = ldvp->vni_num;
	nf = ldvp->nf;

	/* Read rxVMAC stat register and change counters/link state */
	rxvmac_stat = SXGE_GET64(sxge->pio_hdl,
	    RXVMAC_INT_STAT(vni_num, nf));

	if (rxvmac_stat & RXVMAC_INTST_FMCNT_OVL_MSK) {
		sxge->statsp->vmac_stats.rx_frame_cnt +=
		    SXGE_GET64(sxge->pio_hdl, RXVMAC_FRM_CNT(vni_num, nf));
	}
	if (rxvmac_stat & RXVMAC_INTST_BTCNT_OVL_MSK) {
		sxge->statsp->vmac_stats.rx_byte_cnt +=
		    SXGE_GET64(sxge->pio_hdl, RXVMAC_BYTE_CNT(vni_num, nf));
	}
	if (rxvmac_stat & RXVMAC_INTST_DRPFMCNT_OVL_MSK) {
		sxge->statsp->vmac_stats.rx_drop_frame_cnt +=
		    SXGE_GET64(sxge->pio_hdl, RXVMAC_DROPFM_CNT(vni_num, nf));
	}
	if (rxvmac_stat & RXVMAC_INTST_DRPBTCNT_OVL_MSK) {
		sxge->statsp->vmac_stats.rx_drop_byte_cnt +=
		    SXGE_GET64(sxge->pio_hdl, RXVMAC_DROPBT_CNT(vni_num, nf));
	}
	if (rxvmac_stat & RXVMAC_INTST_MCASTFMCNT_OVL_MSK) {
		sxge->statsp->vmac_stats.rx_mcast_frame_cnt +=
		    SXGE_GET64(sxge->pio_hdl, RXVMAC_MCASTFM_CNT(vni_num, nf));
	}
	if (rxvmac_stat & RXVMAC_INTST_BCASTFMCNT_OVL_MSK) {
		sxge->statsp->vmac_stats.rx_bcast_frame_cnt +=
		    SXGE_GET64(sxge->pio_hdl, RXVMAC_BCASTFM_CNT(vni_num, nf));
	}
	if (rxvmac_stat & RXVMAC_INTST_LINKST_MSK) {
		sxge_link_update(sxge, LINK_STATE_UP);
	} else sxge_link_update(sxge, LINK_STATE_DOWN);

	/* clear the link up/down bits if set */
	if (rxvmac_stat & RXVMAC_INTST_LINKUP_MSK ||
	    rxvmac_stat & RXVMAC_INTST_LINKDN_MSK) {
		SXGE_PUT64(sxge->pio_hdl,
		    RXVMAC_INT_STAT(ldvp->vni_num, ldvp->nf),
		    (RXVMAC_INTST_LINKUP_MSK | RXVMAC_INTST_LINKDN_MSK));
	}

	/* re-arm grp if this is the only device */
	if (ldgp->nldvs == 1) {
		sxge_intr_arm_ldg(sxge, ldgp, B_TRUE);
	}
	return (serviced);
}

static uint_t
sxge_txvmac_intr(void *arg1, void *arg2)
{
	uint_t serviced = DDI_INTR_UNCLAIMED;
	sxge_ldv_t *ldvp = (sxge_ldv_t *)arg1;
	sxge_t *sxge = (sxge_t *)arg2;
	sxge_ldg_t *ldgp;
	uint64_t txvmac_stat;

	if (arg1 == NULL) {
		SXGE_DBG((NULL, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"<==sxge_txvmac_intr: arg1 LD NULL"));
		return (serviced);
	}

	if (arg2 == NULL || (void *)ldvp->sxge != arg2) {
		sxge = ldvp->sxge;
	}

	SXGE_DBG0((sxge, (SXGE_GLD_BLK | SXGE_INFO_PRI),
	"==>sxge_txvmac_intr"));

	if (!(sxge->sxge_state & SXGE_INITIALIZED)) {
		SXGE_DBG((NULL, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"<==sxge_txvmac_intr: not initialized"));
		return (serviced);
	}
	ldgp = ldvp->ldgp;

	if (ldgp == NULL) {
		SXGE_DBG((NULL, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"<==sxge_txvmac_intr: ldgp NULL"));
		return (serviced);
	}
	serviced = DDI_INTR_CLAIMED;

	/* Read txVMAC stat register and increment counters */
	txvmac_stat = SXGE_GET64(sxge->pio_hdl,
	    TXVMAC_STAT(ldvp->vni_num, ldvp->nf));
	if (txvmac_stat & TXVMAC_STAT_SW_RST_DONE_MSK) {
		/* should have this bit masked, not get this intr */
		SXGE_DBG((NULL, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"sxge_txvmac_intr: TXVMAC SW RST done"));
	}
	if (txvmac_stat & TXVMAC_STAT_SW_FRMCNT_OVL_MSK) {
		sxge->statsp->vmac_stats.tx_frame_cnt += TXVMAC_FRM_CNT_MSK;
	}
	if (txvmac_stat & TXVMAC_STAT_SW_BTCNT_OVL_MSK) {
		sxge->statsp->vmac_stats.tx_byte_cnt += TXVMAC_BYTE_CNT_MSK;
	}

	/* clear the status register W1C */
	SXGE_PUT64(sxge->pio_hdl, TXVMAC_STAT(ldvp->vni_num, ldvp->nf),
	    (TXVMAC_STAT_SW_RST_DONE_MSK | TXVMAC_STAT_SW_FRMCNT_OVL_MSK
	    | TXVMAC_STAT_SW_BTCNT_OVL_MSK));

	/* re-arm grp if this is the only device */
	if (ldgp->nldvs == 1) {
		sxge_intr_arm_ldg(sxge, ldgp, B_TRUE);
	}
	return (serviced);
}

static uint_t
sxge_mbx_intr(void *arg1, void *arg2)
{
	uint_t serviced = DDI_INTR_UNCLAIMED;
	sxge_ldv_t *ldvp = (sxge_ldv_t *)arg1;
	sxge_t *sxge = (sxge_t *)arg2;
	sxge_ldg_t *ldgp;
	niu_mb_status_t		cs;
	uint64_t		addr;

	if (arg1 == NULL) {
		SXGE_DBG((NULL, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"<==sxge_mbx_intr: arg1 LD NULL"));
		return (serviced);
	}

	if (arg2 == NULL || (void *)ldvp->sxge != arg2) {
		sxge = ldvp->sxge;
	}

	SXGE_DBG0((sxge, (SXGE_MBOX_BLK | SXGE_INFO_PRI), "==>sxge_mbx_intr"));

	if (!(sxge->sxge_state & SXGE_INITIALIZED)) {
		SXGE_DBG((NULL, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"<==sxge_mbx_intr: not initialized"));
		return (serviced);
	}
	ldgp = ldvp->ldgp;

	if (ldgp == NULL) {
		SXGE_DBG((NULL, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"<==sxge_mbx_intr: ldgp NULL"));
		return (serviced);
	}
	serviced = DDI_INTR_CLAIMED;

	/* process the intr */
	{
		addr = NIU_MB_STAT;
		cs.value = 0;
		cs.value = SXGE_GET64(sxge->pio_hdl, addr);

		SXGE_DBG((NULL, (SXGE_MBOX_BLK | SXGE_INFO_PRI),
		"sxge_mbx_intr: cs.value is 0x%llx\n", cs.value));

		MUTEX_ENTER(&sxge->mbox_lock);

		if ((cs.bits.omb_acked == 1) || (cs.bits.omb_failed == 1)) {
			sxge->mbox.acked = B_TRUE;
		}
		if (cs.bits.imb_full) {
			sxge->mbox.imb_full = B_TRUE;
		}

		SXGE_PUT64(sxge->pio_hdl, addr, cs.value);

		MUTEX_EXIT(&sxge->mbox_lock);
	}

	/* re-arm grp if this is the only device */
	if (ldgp->nldvs == 1) {
		sxge_intr_arm_ldg(sxge, ldgp, B_TRUE);
	}
	return (serviced);
}

static uint_t
sxge_vnierr_intr(void *arg1, void *arg2)
{
	uint_t serviced = DDI_INTR_UNCLAIMED;
	sxge_ldv_t *ldvp = (sxge_ldv_t *)arg1;
	sxge_t *sxge = (sxge_t *)arg2;
	sxge_ldg_t *ldgp;

	if (arg1 == NULL) {
		SXGE_DBG((NULL, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"<==sxge_vnierr_intr: arg1 LD NULL"));
		return (serviced);
	}

	if (arg2 == NULL || (void *)ldvp->sxge != arg2) {
		sxge = ldvp->sxge;
	}

	SXGE_DBG((sxge, (SXGE_GLD_BLK | SXGE_INFO_PRI),
	"==>sxge_vnierr_intr"));

	if (!(sxge->sxge_state & SXGE_INITIALIZED)) {
		SXGE_DBG((NULL, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"<==sxge_vnierr_intr: not initialized"));
		return (serviced);
	}
	ldgp = ldvp->ldgp;

	if (ldgp == NULL) {
		SXGE_DBG((NULL, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		"<==sxge_vnierr_intr: ldgp NULL"));
		return (serviced);
	}
	serviced = DDI_INTR_CLAIMED;

	/* re-arm grp if this is the only device */
	if (ldgp->nldvs == 1) {
		sxge_intr_arm_ldg(sxge, ldgp, B_TRUE);
	}
	return (serviced);
}

static int
sxge_resume(sxge_t *sxge)
{
	if (sxge == NULL)
		return (SXGE_FAILURE);

	MUTEX_ENTER(&sxge->gen_lock);

	if (sxge->sxge_state & SXGE_STARTED) {
		if (sxge_init(sxge) != 0) {
			MUTEX_EXIT(&sxge->gen_lock);
			return (SXGE_FAILURE);
		}

		/*
		 * Enable and start the watchdog timer
		 */
	}

	sxge->sxge_state &= ~SXGE_SUSPENDED;
	sxge->suspended = 0;

	MUTEX_EXIT(&sxge->gen_lock);

	return (SXGE_SUCCESS);
}

static int
sxge_suspend(sxge_t *sxge)
{
	if (sxge == NULL)
		return (SXGE_FAILURE);

	/*
	 * Stop the link status timer before sxge_intrs_disable() to avoid
	 * accessing the the MSIX table simultaneously. Note that the timer
	 * routine polls for MSIX parity errors.
	 */
	MUTEX_ENTER(&sxge->tmout.lock);
	if (sxge->tmout.id)
		(void) untimeout(sxge->tmout.id);
	MUTEX_EXIT(&sxge->tmout.lock);

	MUTEX_ENTER(&sxge->gen_lock);

	sxge->sxge_state |= SXGE_SUSPENDED;

	sxge_uninit(sxge);

	MUTEX_EXIT(&sxge->gen_lock);

	/*
	 * Disable and stop the watchdog timer
	 */

	return (SXGE_SUCCESS);
}

/*
 * sxge_init - Initialize the device.
 */
static int
sxge_init(sxge_t *sxge)
{
	int i, j, c, d, v;
	uint64_t val;
	uint16_t pci_devid = 0, pci_venid = 0;
	int stat = SXGE_SUCCESS;
	sxge_ring_handle_t	 *rhp;

	SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI), "==>sxge_init"));

	/* Init sequence */

	if (sxge->sxge_state & SXGE_INITIALIZED) {
		return (stat);
	}

	pci_devid = SXGE_CGET16(sxge->pcicfg_hdl, PCI_CONF_DEVID);
	pci_venid = SXGE_CGET16(sxge->pcicfg_hdl, PCI_CONF_VENID);
	SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "sxge_init: pci_venid %x pci_devid %x", pci_venid, pci_devid));

	val = SXGE_GET64(sxge->pio_hdl, 0x00000000000FE000);
	SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "sxge_init: RDATL[0x%lx]", val));
	val = SXGE_GET64(sxge->pio_hdl, 0x00000000000FE008);
	SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "sxge_init: RDATH[0x%lx]", val));

	/* read mac address */
	val = SXGE_GET64(sxge->pio_hdl, NIU_OMB_ENTRY(7));

	sxge->vmac.addr[0] = (uint8_t)((val >> 40) & 0xff);
	sxge->vmac.addr[1] = (uint8_t)((val >> 32) & 0xff);
	sxge->vmac.addr[2] = (uint8_t)((val >> 24) & 0xff);
	sxge->vmac.addr[3] = (uint8_t)((val >> 16) & 0xff);
	sxge->vmac.addr[4] = (uint8_t)((val >> 8) & 0xff);
	sxge->vmac.addr[5] = (uint8_t)(val & 0xff);
	SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "%d: NIU MAC addr - OMB7 %2x:%2x:%2x:%2x:%2x:%2x",
	    sxge->instance,
	    sxge->vmac.addr[0], sxge->vmac.addr[1],
	    sxge->vmac.addr[2], sxge->vmac.addr[3],
	    sxge->vmac.addr[4], sxge->vmac.addr[5]));

	/*
	 * Initialize Mailbox
	 */
	if (sxge_mbx_en) {
		if (sxge_hosteps_mbx_init(sxge) != 0) {
			SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
			    " Failed to init Host to EPS Mailbox"));
			goto sxge_init_fail2;
		}
	}
	/*
	 * Allocate system memory for the receive/transmit buffer blocks
	 * and receive/transmit descriptor rings.
	 * Here or in attach routine?
	 */

	if (sxge_vni_auto_en) {

		c = 0; d = 0; v = 0;
		sxge->tdc_prsr_en = sxge_tx_prsr_en;

		for (j = 0; (j < SXGE_MAX_VNI_NUM) && (v < sxge->vni_cnt);
		    j++) {
		if (sxge->vni_arr[j].present) {
		v++;
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		    "sxge_init: vni %d vnum %d",
		    j, sxge->vni_arr[j].vni_num));

		for (i = 0; (i < SXGE_MAX_VNI_VMAC_NUM); i++) {
			if (sxge->vni_arr[j].vmac[i]) {
				sxge->vni_arr[j].vidx[i] = (uint8_t)i;
				SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
				    "sxge_init: vni = %d vmac = %d", j, i));
				(void) sxge_tx_vmac_init(sxge, j, i);
				(void) sxge_rx_vmac_init(sxge, j, i);
				d++;
			}
		}

		for (i = 0; (i < SXGE_MAX_VNI_DMA_NUM) &&
		    (c < sxge_rx_rings); i++) {

			if (sxge->vni_arr[j].dma[i]) {
			SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
			    "sxge_init: vni = %d dma = %d", j, i));
			sxge->vni_arr[j].didx[i] = (uint8_t)c;
			sxge->tdc[c].sxgep = (void *)sxge;
			sxge->tdc[c].vnin = sxge->vni_arr[j].vni_num;
			sxge->tdc[c].tdcn = i;
			MUTEX_INIT(&sxge->tdc_lock[c], NULL,
			    MUTEX_DRIVER, NULL);
			MUTEX_ENTER(&sxge->tdc_lock[c]);
			(void) sxge_tdc_init(&sxge->tdc[c]);
			MUTEX_EXIT(&sxge->tdc_lock[c]);

			sxge->rdc[c].sxgep = (void *)sxge;
			sxge->rdc[c].vnin = sxge->vni_arr[j].vni_num;
			sxge->rdc[c].rdcn = i;
			MUTEX_INIT(&sxge->rdc_lock[c], NULL,
			    MUTEX_DRIVER, NULL);
			MUTEX_ENTER(&sxge->rdc_lock[c]);
			(void) sxge_rdc_init(&sxge->rdc[c]);
			MUTEX_EXIT(&sxge->rdc_lock[c]);
			c++;
			}
		}
		} /* if (sxge->vni_arr[j].present) */
		}

		if ((c != sxge->dma_cnt) || (v != sxge->vni_cnt))
			SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
			    "sxge_init: c != dma_cnt, v != vni_cnt"));
	} else {

		/*
		 * Initialize and enable VMACs
		 */
		(void) sxge_tx_vmac_init(sxge, sxge_vni_num, sxge_vmac_num);
		(void) sxge_rx_vmac_init(sxge, sxge_vni_num, sxge_vmac_num);

		/*
		 * Initialize and enable TXDMA channels.
		 */
		sxge->tdc_prsr_en = sxge_tx_prsr_en;
		for (i = 0; i < sxge->num_tx_rings; i++) {
		sxge->tdc[i].sxgep = (void *)sxge;
		sxge->tdc[i].vnin = sxge_vni_num;
		sxge->tdc[i].tdcn = i;
		(void) sxge_tdc_init(&sxge->tdc[i]);
		MUTEX_INIT(&sxge->tdc_lock[i], NULL, MUTEX_DRIVER, NULL);
		}

		/*
		 * Initialize and enable RXDMA channels.
		 */
		for (i = 0; i < sxge->num_rx_rings; i++) {
		sxge->rdc[i].sxgep = (void *)sxge;
		sxge->rdc[i].vnin = sxge_vni_num;
		sxge->rdc[i].rdcn = i;
		(void) sxge_rdc_init(&sxge->rdc[i]);
		MUTEX_INIT(&sxge->rdc_lock[i], NULL, MUTEX_DRIVER, NULL);
		}
	}

	for (i = 0; i < sxge->num_rx_rings; i++) {
	rhp = &sxge->tx_ring_handles[i];
	rhp->sxge = sxge;
	rhp->index = i;
	rhp->started = 1;

	rhp = &sxge->rx_ring_handles[i];
	rhp->sxge = sxge;
	rhp->index = i;
	rhp->started = 1;
	}

	/*
	 * interrupts enable
	 */

	if (sxge_intr_en) {
	/*
	 * Add interrupts
	 */
	if (sxge->msix_hdl.regh) {
		if (sxge_add_intrs(sxge) == SXGE_SUCCESS) {

		sxge->load_state |= A_INTR_ADD_DONE;

		/*
		 * Enable the interrrupts for DDI.
		 */
		if (sxge_enable_intrs(sxge) != 0) {
			SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
			    "sxge_init: Failed sxge_enable_intrs"));

			goto sxge_init_fail1;
		}
		else
			sxge->load_state |= A_EN_INTR_DONE;
		}
		else
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		    "sxge_init: Failed sxge_add_intrs"));
	}
	}

	/*
	 * soft interrupt fallback
	 */

	sxge->soft_poll_en = sxge_soft_poll_en;

	if (!(sxge->load_state & A_INTR_ADD_DONE))
		sxge->soft_poll_en = 1;

	sxge->idp = NULL; sxge->ibc = NULL;
	if (ddi_add_softintr(sxge->dip, sxge_soft_intr_pri,
	    &sxge->idp, &sxge->ibc, NULL, sxge_rx_soft_poll,
	    (char *)sxge) != SXGE_SUCCESS)
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		    "Failed to register soft intr!"));

	sxge_rx_soft_tmr((void *)sxge);

	/* Unmask xdc interrupts */
	for (i = 0; i < sxge->num_rx_rings; i++) {
		MUTEX_ENTER(&sxge->rdc_lock[i]);
		(void) sxge_rdc_mask(&sxge->rdc[i], B_FALSE);
		MUTEX_EXIT(&sxge->rdc_lock[i]);

		MUTEX_ENTER(&sxge->tdc_lock[i]);
		(void) sxge_tdc_mask(&sxge->tdc[i], B_FALSE);
		MUTEX_EXIT(&sxge->tdc_lock[i]);
	}

	sxge->sxge_state |= SXGE_INITIALIZED;

	goto sxge_init_exit;

sxge_init_fail1:

sxge_init_fail2:

sxge_init_fail3:

sxge_init_exit:
	SXGE_DBG((sxge, (SXGE_GLD_BLK | SXGE_INFO_PRI), "<==sxge_init"));
	return (stat);
}

/*
 * sxge_uninit - Uninitialize the device.
 */
static void
sxge_uninit(sxge_t *sxge)
{
	int i = 0, j = 0;
	/* int k = 0, c = 0; */	/* unused */
	int d = 0, v = 0;
	sxge_ring_handle_t	 *rhp;

	if (!(sxge->sxge_state & SXGE_INITIALIZED))
		return;

	/* Mask xdc interrupts */
	for (i = 0; i < sxge->num_rx_rings; i++) {
		MUTEX_ENTER(&sxge->rdc_lock[i]);
		(void) sxge_rdc_mask(&sxge->rdc[i], B_TRUE);
		MUTEX_EXIT(&sxge->rdc_lock[i]);

		MUTEX_ENTER(&sxge->tdc_lock[i]);
		(void) sxge_tdc_mask(&sxge->tdc[i], B_TRUE);
		MUTEX_EXIT(&sxge->tdc_lock[i]);
	}

	if (sxge->soft_poll_en)
		sxge->soft_poll_en = 0;
	if (sxge->soft_tmr_id) {
		sxge_stop_timer(sxge, sxge->soft_tmr_id);
		sxge->soft_tmr_id = 0;
	}
	if (sxge->idp) {
		ddi_remove_softintr(sxge->idp);
		sxge->idp = NULL; sxge->ibc = NULL;
	}

	for (i = 0; i < sxge->num_rx_rings; i++) {
	rhp = &sxge->tx_ring_handles[i];
	rhp->started = 0;

	rhp = &sxge->rx_ring_handles[i];
	rhp->started = 0;
	}

	if (sxge_vni_auto_en) {

		/* c = 0; */	/* set but not used */
		d = 0; v = 0;
		for (j = 0; (j < SXGE_MAX_VNI_NUM) && (v < sxge->vni_cnt);
		    j++) {
		if (sxge->vni_arr[j].present) {
			v++;
			SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
			    "sxge_uninit: vni %d vnum %d",
			    j, sxge->vni_arr[j].vni_num));

			for (i = 0; (i < SXGE_MAX_VNI_VMAC_NUM); i++)
			if (sxge->vni_arr[j].vmac[i]) {
				(void) sxge_tx_vmac_fini(sxge, j, i);
				(void) sxge_rx_vmac_fini(sxge, j, i);
				d++;
			}
		}
		}
	}

	if (sxge_intr_en) {
		if (sxge->load_state & A_EN_INTR_DONE)
			(void) sxge_disable_intrs(sxge);
		sxge->load_state &= ~A_EN_INTR_DONE;

		if (sxge->load_state & A_INTR_ADD_DONE)
			(void) sxge_remove_intrs(sxge);
		sxge->load_state &= ~A_INTR_ADD_DONE;
	}

	for (i = 0; i < sxge->num_rx_rings; i++) {
		MUTEX_ENTER(&sxge->tdc_lock[i]);
		(void) sxge_tdc_fini(&sxge->tdc[i]);
		MUTEX_EXIT(&sxge->tdc_lock[i]);
		MUTEX_DESTROY(&sxge->tdc_lock[i]);

		MUTEX_ENTER(&sxge->rdc_lock[i]);
		(void) sxge_rdc_fini(&sxge->rdc[i]);
		MUTEX_EXIT(&sxge->rdc_lock[i]);
		MUTEX_DESTROY(&sxge->rdc_lock[i]);
	}

	sxge->sxge_state &= ~SXGE_INITIALIZED;
}

static int
sxge_tx_vmac_init(sxge_t *sxge, int vni_num, int vmac_num)
{
	uint64_t val;
	/* uint64_t data; */	/* unused */
	int count = 5;

	/* Reset TX VMAC */
	val = 0;
	val |= TXVMAC_CFG_SW_RST_MSK;
	SXGE_PUT64(sxge->pio_hdl, TXVMAC_CFG(vni_num, vmac_num), val);

	val = SXGE_GET64(sxge->pio_hdl, TXVMAC_STAT(vni_num, vmac_num));
	while ((count--) && ((val & TXVMAC_CFG_SW_RST_MSK) == 0)) {
		SXGE_UDELAY(sxge, 1, sxge_delay_busy);
		val = SXGE_GET64(sxge->pio_hdl, TXVMAC_STAT(vni_num, vmac_num));
	}

	SXGE_PUT64(sxge->pio_hdl, TXVMAC_CFG(vni_num, vmac_num), 0ULL);

	SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "sxge_tx_vmac_init: TXVMAC_CFG(%d, %d) = [0x%llx]",
	    vni_num, vmac_num, val));
	if (count <= 0) {
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		    "sxge_tx_vmac_init: TX VMAC reset failed"));
		SXGE_FM_REPORT_ERROR(sxge, sxge->instance, (int)vmac_num,
		    SXGE_FM_EREPORT_TXVMAC_RESET_FAIL);
		return (-1);
	}

	/* Unmask events for which we want to get interrupts */
	SXGE_PUT64(sxge->pio_hdl, TXVMAC_STMASK(vni_num, vmac_num),
	    TXVMAC_STMASK_SW_RST_DONE_MSK);

	return (0);
}

static int
sxge_tx_vmac_fini(sxge_t *sxge, int vni_num, int vmac_num)
{
	uint64_t val;
	/* uint64_t data; */	/* unused */
	int count = 5;

	/* Reset TX VMAC */
	val = 0;
	val |= TXVMAC_CFG_SW_RST_MSK;
	SXGE_PUT64(sxge->pio_hdl, TXVMAC_CFG(vni_num, vmac_num), val);

	val = SXGE_GET64(sxge->pio_hdl, TXVMAC_STAT(vni_num, vmac_num));
	while ((count--) && ((val & TXVMAC_CFG_SW_RST_MSK) == 0)) {
		SXGE_UDELAY(sxge, 1, sxge_delay_busy);
		val = SXGE_GET64(sxge->pio_hdl, TXVMAC_STAT(vni_num, vmac_num));
	}
	SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "sxge_tx_vmac_fini: TXVMAC_CFG(%d, %d) = [0x%llx]",
	    vni_num, vmac_num, val));
	if (count <= 0) {
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		    "sxge_tx_vmac_fini: TX VMAC reset failed"));
		SXGE_FM_REPORT_ERROR(sxge, sxge->instance, (int)vmac_num,
		    SXGE_FM_EREPORT_TXVMAC_RESET_FAIL);
		return (-1);
	}

	return (0);
}

static int
sxge_rx_vmac_init(sxge_t *sxge, int vni_num, int vmac_num)
{
	uint64_t	val, dma_vec;
	/* uint64_t	data; */	/* unused */
	int		i, count = 5;
	uint64_t 	rxvmac_stat;

	/* Reset RX VMAC */
	val = 0;
	val |= (RXVMAC_CFG_RST_MSK << RXVMAC_CFG_RST_SH);
	SXGE_PUT64(sxge->pio_hdl, RXVMAC_CFG(vni_num, vmac_num), val);

	val = SXGE_GET64(sxge->pio_hdl, RXVMAC_CFG(vni_num, vmac_num));
	while ((count--) &&
	    ((val & (RXVMAC_CFG_RST_ST_MSK << RXVMAC_CFG_RST_ST_SH)) == 0)) {
		SXGE_UDELAY(sxge, 1, sxge_delay_busy);
		val = SXGE_GET64(sxge->pio_hdl, RXVMAC_CFG(vni_num, vmac_num));
	}
	if (count <= 0) {
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		    "sxge_rx_vmac_init: RX VMAC reset failed"));
		SXGE_FM_REPORT_ERROR(sxge, sxge->instance, (int)vmac_num,
		    SXGE_FM_EREPORT_RXVMAC_RESET_FAIL);
		return (-1);
	}

	/* Set up DMA and enable the VMAC */
	dma_vec = val = 0;
	if (sxge_vni_auto_en) {
	for (i = 0; i < SXGE_MAX_VNI_DMA_NUM; i++) {
		if (sxge->vni_arr[vni_num].dma[i] & 0x1) {
			dma_vec |= (0x01ULL << (RXVMAC_CFG_DMA_VECT_SH + i));
		}
	}
	} else dma_vec = sxge_rx_dvect << 8;
	val |= dma_vec;

	if (sxge_vni_auto_en) {
		if (sxge->vni_arr[vni_num].dma_cnt == 4) val |= (0x3 << 12);
	} else val |= (sxge_rx_opcode << 12);

	if (sxge_rx_prom_en) { /* if (sxge->vmac.promisc) */
		val |= (0x01ULL << RXVMAC_CFG_PROMISCMD_SH);
	}

	SXGE_PUT64(sxge->pio_hdl, RXVMAC_CFG(vni_num, vmac_num), val);
	val = SXGE_GET64(sxge->pio_hdl, RXVMAC_CFG(vni_num, vmac_num));
	SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "sxge_rx_vmac_init: RXVMAC_CFG(%d, %d) = [0x%llx]",
	    vni_num, vmac_num, val));


	/* Clear all counters */
	SXGE_PUT64(sxge->pio_hdl, RXVMAC_CLR_CNT(vni_num, vmac_num), 1);

	/* Unmask events for which we want to get interrupts */
	if (sxge_dbg_link)
	SXGE_PUT64(sxge->pio_hdl, RXVMAC_INT_MSK(vni_num, vmac_num), 0);
	else
	SXGE_PUT64(sxge->pio_hdl, RXVMAC_INT_MSK(vni_num, vmac_num), 0x180);

	rxvmac_stat = SXGE_GET64(sxge->pio_hdl,
	    RXVMAC_INT_STAT(vni_num, vmac_num));
	if (rxvmac_stat & RXVMAC_INTST_LINKST_MSK) {
		sxge_link_update(sxge, LINK_STATE_UP);
	} else sxge_link_update(sxge, LINK_STATE_DOWN);

	return (0);
}


static int
sxge_rx_vmac_fini(sxge_t *sxge, int vni_num, int vmac_num)
{
	uint64_t	val;
	/*
	 * uint64_t	dma_vec, data;
	 * int		i;
	 */
	/* unused */
	int		count = 5;

	/* Reset RX VMAC */
	val = 0;
	val |= (RXVMAC_CFG_RST_MSK << RXVMAC_CFG_RST_SH);
	SXGE_PUT64(sxge->pio_hdl, RXVMAC_CFG(vni_num, vmac_num), val);

	val = SXGE_GET64(sxge->pio_hdl, RXVMAC_CFG(vni_num, vmac_num));
	while ((count--) &&
	    ((val & (RXVMAC_CFG_RST_ST_MSK << RXVMAC_CFG_RST_ST_SH)) == 0)) {
		SXGE_UDELAY(sxge, 1, sxge_delay_busy);
		val = SXGE_GET64(sxge->pio_hdl, RXVMAC_CFG(vni_num, vmac_num));
	}
	if (count <= 0) {
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		    "sxge_rx_vmac_fini: RX VMAC reset failed"));
		SXGE_FM_REPORT_ERROR(sxge, sxge->instance, (int)vmac_num,
		    SXGE_FM_EREPORT_RXVMAC_RESET_FAIL);
		return (-1);
	}

	return (0);
}

/*
 * sxge_alloc_rings - Allocate memory space for rx/tx rings.
 */
/* ARGSUSED */
static int
sxge_alloc_rings(sxge_t *sxge)
{
	return (0);
}

/*
 * sxge_free_rings - Free the memory space of rx/tx rings.
 */
/* ARGSUSED */
static void
sxge_free_rings(sxge_t *sxge)
{
}

/*
 * sxge_get_resource
 */
static int
sxge_get_resources(sxge_t *sxge)
{
	int i, j, vmac_cnt;
	uint64_t val, rdatl, rdath;
	uint8_t res;

	SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "==>sxge_get_resources"));

	sxge->num_tx_rings = 0; sxge->num_rx_rings = 0;
	sxge->vni_cnt = 0; sxge->dma_cnt = 0; sxge->vmac_cnt = 0;

	/* First check if the NIU is available */
	val = SXGE_GET64(sxge->pio_hdl, SXGE_NIU_AVAILABLE);
	SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "sxge_get_resources: SXGE_NIU_AVAILABLE %llx = %llx",
	    SXGE_NIU_AVAILABLE, val));

	SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "sxge_get_resources: After SXGE_NIU_AVAILABLE read"));

	/* Read RDAT */
	rdatl = SXGE_GET64(sxge->pio_hdl, SXGE_RDAT_LOW);
	SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "sxge_get_resources: RDATL %llx = 0x%lx",
	    SXGE_RDAT_LOW, rdatl));
	rdath = SXGE_GET64(sxge->pio_hdl, SXGE_RDAT_HIGH);

	SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "sxge_get_resources: RDATH %llx = 0x%lx",
	    SXGE_RDAT_HIGH, rdath));

	vmac_cnt = 0;

	/* First 8 VNI resources are in RDATL, next 4 are in RDATH */
	for (i = 0; i < SXGE_MAX_VNI_NUM; i++) {
		if (i < 8) {
			res = (uint8_t)(rdatl & SXGE_RDAT_VNI_MASK);
			rdatl >>= 8;
		} else {
			res = (uint8_t)(rdath & SXGE_RDAT_VNI_MASK);
			rdath >>= 8;
		}
		if (res != 0) {
			sxge->vni_cnt++;
			sxge->vni_arr[i].present = 1;
			sxge->vni_arr[i].vni_num = (uint8_t)i;
			SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
			    "%d: found vni %d", sxge->instance, i));

			for (j = 0; j < SXGE_MAX_VNI_DMA_NUM; j++) {
				sxge->vni_arr[i].dma[j] = res & 0x1;
				if (sxge->vni_arr[i].dma[j] == 1) {
					sxge->vni_arr[i].dma_cnt++;
					sxge->dma_cnt++;
				SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
				    "%d: found dma %d", sxge->instance, j));
				}
				res >>= 1;
			}
			for (j = 0; j < SXGE_MAX_VNI_VMAC_NUM; j++) {
				sxge->vni_arr[i].vmac[j] = res & 0x1;
				if (sxge->vni_arr[i].vmac[j] == 1) {
					sxge->vni_arr[i].vmac_cnt++;
					sxge->vmac_cnt++;
				SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
				    "%d: found vmac %d", sxge->instance, j));
				}
				res >>= 1;
			}
			if (sxge->vni_arr[i].dma_cnt == 0 ||
			    sxge->vni_arr[i].vmac_cnt == 0) {
				SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
				    "sxge_get_resources: dma_cnt/vmac_cnt 0 "
				    "for VNI %d", i));
			}

			sxge->num_rx_rings += sxge->vni_arr[i].dma_cnt;
			sxge->num_tx_rings = sxge->num_rx_rings;
			vmac_cnt += sxge->vni_arr[i].vmac_cnt;
		}
	}

	if (sxge->vni_cnt == 0 || sxge->dma_cnt == 0 || sxge->vmac_cnt == 0) {
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		    "sxge_get_resources: no resources vni %d vmac %d dma %d",
		    sxge->vni_cnt, sxge->vmac_cnt, sxge->dma_cnt));
	}

	SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "sxge_get_resources: num_rx_rings %d num_tx_rings %d",
	    sxge->num_rx_rings, sxge->num_tx_rings));

	sxge->num_rx_groups = sxge_rx_grps;
	sxge->num_tx_groups = sxge_tx_grps;

	return (0);
}

/*
 * sxge_get_config_properties
 */
/* ARGSUSED */
static void
sxge_get_config_properties(sxge_t *sxge)
{
}

/*
 * sxge_unconfig_properties
 */
/* ARGSUSED */
static void
sxge_unconfig_properties(sxge_t *sxge)
{
}

timeout_id_t
sxge_start_timer(sxge_t *sxge, fptrv_t func, int usec)
{
	if ((sxge->suspended == 0) || (sxge->suspended == DDI_RESUME)) {
		return (timeout(func, (caddr_t)sxge,
		    drv_usectohz(usec)));
	}
	return (NULL);
}

/* ARGSUSED */
void
sxge_stop_timer(sxge_t *sxge, timeout_id_t timerid)
{
	if (timerid) {
		(void) untimeout(timerid);
	}
}

/*
 * GLD interfaces
 */

/*
 * Get a statistics value.
 */
int
sxge_m_stat(void *arg, uint_t stat, uint64_t *val)
{
	/* uint64_t addr; */	/* unused */
	int i, j, vni, vmac;
	sxge_t *sxge = (sxge_t *)arg;

	for (i = 0; i < SXGE_MAX_VNI_NUM; i++) {
		if (sxge->vni_arr[i].present == 1 &&
		    sxge->vni_arr[i].vmac_cnt != 0) {
			for (j = 0; j < SXGE_MAX_VNI_VMAC_NUM; j++) {
				if (sxge->vni_arr[i].vmac[j] == 1) {
					vni = i;
					vmac = j;
					break;
				}
			}
		}
	}

	MUTEX_ENTER(&sxge->gen_lock);

	if (sxge->sxge_state & SXGE_SUSPENDED || sxge->statsp == NULL) {
		MUTEX_EXIT(&sxge->gen_lock);
		return (ECANCELED);
	}

	*val = 0;
	switch (stat) {
	case MAC_STAT_IFSPEED:
		if (sxge->link_speed == 0) {
			if (sxge->mbox.ready != B_TRUE)
				(void) sxge_hosteps_mbx_init(sxge);
			(void) sxge_mbx_link_speed_req(sxge);
		}
		*val = sxge->link_speed * 1000000ull;
		break;

	case MAC_STAT_MULTIRCV:
		*val = sxge->statsp->vmac_stats.rx_mcast_frame_cnt +=
		    SXGE_GET64(sxge->pio_hdl, RXVMAC_MCASTFM_CNT(vni, vmac));
		break;

	case MAC_STAT_BRDCSTRCV:
		*val = sxge->statsp->vmac_stats.rx_bcast_frame_cnt +=
		    SXGE_GET64(sxge->pio_hdl, RXVMAC_BCASTFM_CNT(vni, vmac));
		break;

	case MAC_STAT_MULTIXMT:
		break;

	case MAC_STAT_BRDCSTXMT:
		break;

	case MAC_STAT_NORCVBUF:
		break;

	case MAC_STAT_IERRORS:
		break;

	case MAC_STAT_RBYTES:
		*val = sxge->statsp->vmac_stats.rx_byte_cnt +=
		    SXGE_GET64(sxge->pio_hdl, RXVMAC_BYTE_CNT(vni, vmac));
		break;

	case MAC_STAT_OBYTES:
		*val = sxge->statsp->vmac_stats.tx_byte_cnt +=
		    SXGE_GET64(sxge->pio_hdl, TXVMAC_BYTE_CNT(vni, vmac));
		break;

	case MAC_STAT_IPACKETS:
		*val = sxge->statsp->vmac_stats.rx_frame_cnt +=
		    SXGE_GET64(sxge->pio_hdl, RXVMAC_FRM_CNT(vni, vmac));
		break;

	case MAC_STAT_OPACKETS:
		*val = sxge->statsp->vmac_stats.tx_frame_cnt +=
		    SXGE_GET64(sxge->pio_hdl, TXVMAC_FRM_CNT(vni, vmac));
		break;

	/* RFC 1643 stats */
	case ETHER_STAT_FCS_ERRORS:
		break;

	case ETHER_STAT_TOOLONG_ERRORS:
		break;

	case ETHER_STAT_MACRCV_ERRORS:
		break;

	/* MII/GMII stats */
	case ETHER_STAT_XCVR_ADDR:
		break;

	case ETHER_STAT_XCVR_ID:
		break;

	case ETHER_STAT_XCVR_INUSE:
		break;

	case ETHER_STAT_CAP_1000FDX:
		break;

	case ETHER_STAT_CAP_100FDX:
		break;

	case ETHER_STAT_CAP_ASMPAUSE:
		break;

	case ETHER_STAT_CAP_PAUSE:
		break;

	case ETHER_STAT_CAP_AUTONEG:
		break;

	case ETHER_STAT_ADV_CAP_1000FDX:
		break;

	case ETHER_STAT_ADV_CAP_100FDX:
		break;

	case ETHER_STAT_ADV_CAP_ASMPAUSE:
		break;

	case ETHER_STAT_ADV_CAP_PAUSE:
		break;

	case ETHER_STAT_ADV_CAP_AUTONEG:
		break;

	case ETHER_STAT_LP_CAP_1000FDX:
		break;

	case ETHER_STAT_LP_CAP_100FDX:
		break;

	case ETHER_STAT_LP_CAP_ASMPAUSE:
		break;

	case ETHER_STAT_LP_CAP_PAUSE:
		break;

	case ETHER_STAT_LP_CAP_AUTONEG:
		break;

	case ETHER_STAT_LINK_ASMPAUSE:
		*val = sxge->link_asmpause;
		break;

	case ETHER_STAT_LINK_PAUSE:
		*val = sxge->link_pause;
		break;

	case ETHER_STAT_LINK_AUTONEG:
		*val = sxge->link_autoneg;
		break;
	case ETHER_STAT_LINK_DUPLEX:
		*val = sxge->link_duplex;
		break;
	case ETHER_STAT_TOOSHORT_ERRORS:
		break;

	case ETHER_STAT_CAP_REMFAULT:
		break;

	case ETHER_STAT_ADV_REMFAULT:
		break;

	case ETHER_STAT_LP_REMFAULT:
		break;

	case ETHER_STAT_JABBER_ERRORS:
		break;

	default:
		MUTEX_EXIT(&sxge->gen_lock);
		return (ENOTSUP);
	}

	MUTEX_EXIT(&sxge->gen_lock);

	return (0);
}

/*
 * Bring the device out of the reset/quiesced state that it
 * was in when the interface was registered.
 */
static int
sxge_m_start(void *arg)
{
	sxge_t *sxge = (sxge_t *)arg;

	SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI), "==> sxge_m_start"));
	MUTEX_ENTER(&sxge->gen_lock);

	if (sxge->sxge_state & SXGE_SUSPENDED) {
		MUTEX_EXIT(&sxge->gen_lock);
		return (ECANCELED);
	}

	if (sxge_init_en)
	if (sxge_init(sxge) != 0) {
		MUTEX_EXIT(&sxge->gen_lock);
		return (EIO);
	}

	sxge->sxge_state |= SXGE_STARTED;

	if (sxge->sxge_mac_state != SXGE_MAC_STARTED) {
		/*
		 * Start timer to check the system error and tx hangs
		 */
		sxge->sxge_timerid = sxge_start_timer(sxge,
		    sxge_check_hw_state, SXGE_CHECK_TIMER);

		sxge->sxge_mac_state = SXGE_MAC_STARTED;

	}

	MUTEX_EXIT(&sxge->gen_lock);

	SXGE_DBG((sxge, (SXGE_GLD_BLK | SXGE_INFO_PRI), "<== sxge_m_start"));
	return (0);
}

/*
 * Stop the device and put it in a reset/quiesced state such
 * that the interface can be unregistered.
 */
static void
sxge_m_stop(void *arg)
{
	sxge_t *sxge = (sxge_t *)arg;

	SXGE_DBG((sxge, (SXGE_GLD_BLK | SXGE_INFO_PRI), "==> sxge_m_stop"));

	MUTEX_ENTER(&sxge->gen_lock);

	if (sxge->sxge_timerid) {
		sxge_stop_timer(sxge, sxge->sxge_timerid);
		sxge->sxge_timerid = 0;
	}

	if (sxge->sxge_state & SXGE_SUSPENDED) {
		MUTEX_EXIT(&sxge->gen_lock);
		return;
	}

	sxge->sxge_state &= ~SXGE_STARTED;

	if (sxge_init_en)
		sxge_uninit(sxge);

	sxge->sxge_mac_state = SXGE_MAC_STOPPED;

	MUTEX_EXIT(&sxge->gen_lock);
	SXGE_DBG((sxge, (SXGE_GLD_BLK | SXGE_INFO_PRI), "<== sxge_m_stop"));
}

/*
 * Set the promiscuity of the device.
 */
static int
sxge_m_promisc(void *arg, boolean_t on)
{
	int i, j;
	uint64_t val;
	sxge_t *sxge = (sxge_t *)arg;
	int status = 0;

	MUTEX_ENTER(&sxge->gen_lock);

	if (sxge->sxge_state & SXGE_SUSPENDED) {
		MUTEX_EXIT(&sxge->gen_lock);
		return (ECANCELED);
	}

	MUTEX_EXIT(&sxge->gen_lock);

	sxge->filter.all_phys_cnt = ((on) ? 1 : 0);

	MUTEX_ENTER(&sxge->gen_lock);

	for (i = 0; i < SXGE_MAX_VNI_NUM; i++) {
		if (sxge->vni_arr[i].present == 1 &&
		    sxge->vni_arr[i].vmac_cnt != 0) {
			for (j = 0; j < SXGE_MAX_VNI_VMAC_NUM; j++) {
				if (sxge->vni_arr[i].vmac[j] == 1) {
					val = SXGE_GET64(sxge->pio_hdl,
					    RXVMAC_CFG(i, j));
					if (on)
						val |= (0x01ULL <<
						    RXVMAC_CFG_PROMISCMD_SH);
					else
						val |= ~(0x01ULL <<
						    RXVMAC_CFG_PROMISCMD_SH);
					SXGE_PUT64(sxge->pio_hdl,
					    RXVMAC_CFG(i, j), val);
				}
			}
		}
	}

	MUTEX_EXIT(&sxge->gen_lock);

	if (on)
		sxge->vmac.promisc = B_TRUE;
	else
		sxge->vmac.promisc = B_FALSE;

	SXGE_DBG((sxge, (SXGE_GLD_BLK | SXGE_INFO_PRI), "<== sxge_m_promisc"));
	return (0);

fail:
	RW_EXIT(&sxge->filter_lock);
	SXGE_DBG((sxge, (SXGE_GLD_BLK | SXGE_INFO_PRI),
	    "<== sxge_m_promisc FAILED"));
	return (status);
}

/*
 * Add/remove the addresses to/from the set of multicast
 * addresses for which the device will receive packets.
 */
static int
sxge_m_multicst(void *arg, boolean_t add, const uint8_t *mcst_addr)
{
	sxge_t *sxge = (sxge_t *)arg;
	struct ether_addr addrp;
	int result;
	SXGE_DBG((sxge, (SXGE_GLD_BLK | SXGE_INFO_PRI), "==> sxge_m_multicst"));

	bcopy(mcst_addr, (uint8_t *)&addrp, ETHERADDRL);

	MUTEX_ENTER(&sxge->gen_lock);

	if (sxge->sxge_state & SXGE_SUSPENDED || sxge->mbox.ready != B_TRUE) {
		MUTEX_EXIT(&sxge->gen_lock);
		return (ECANCELED);
	}

	if (sxge->mbox.ready != B_TRUE) {
		result = sxge_hosteps_mbx_init(sxge);
		if (result != 0) {
			SXGE_DBG((sxge, (SXGE_MBOX_BLK | SXGE_ERR_PRI),
			    " Failed to init Host to EPS Mailbox"));
			MUTEX_EXIT(&sxge->gen_lock);
			return (ECANCELED);
		}
	}

	result = (add) ? sxge_add_mcast_addr(sxge, &addrp)
	    : sxge_del_mcast_addr(sxge, &addrp);

	MUTEX_EXIT(&sxge->gen_lock);

	if (result)
		return (EINVAL);
	else
		return (0);
}

/*
 * Pass on M_IOCTL messages passed to the DLD, and support
 * private IOCTLs for debugging
 */
static void
sxge_m_ioctl(void *arg, queue_t *q, mblk_t *mp)
{
	sxge_t *sxge = (sxge_t *)arg;
	struct iocblk *iocp;
	/* boolean_t need_privilege; */	/* set but not used */
	/*
	 * struct ether_addr addrp;
	 * int err;
	 */
	/* unused */
	int cmd;

	iocp = (struct iocblk *)(uintptr_t)mp->b_rptr;
	iocp->ioc_error = 0;
	/* need_privilege = B_TRUE; */	/* set but not used */
	cmd = iocp->ioc_cmd;

	SXGE_DBG((sxge, (SXGE_GLD_BLK | SXGE_INFO_PRI), "==> sxge_m_ioctl"
	    " cmd 0x%08x", cmd));

	switch (cmd) {
	default:
		miocnak(q, mp, 0, EINVAL);
		return;
	case LB_GET_INFO_SIZE:
	case LB_GET_INFO:
	case LB_GET_MODE:
		break;
	case ND_GET:
	case SXGE_EPS_BYPASS_RD:
	case SXGE_EPS_BYPASS_WR:
		/* need_privilege = B_FALSE; */	/* set but not used */
		break;
	case LB_SET_MODE:
	case ND_SET:
		break;
	/* Error Injection Utility: ioctl */
	case SXGE_INJECT_ERR:

		cmn_err(CE_NOTE, "sxge_m_ioctl: Inject error\n");
		sxge_err_inject(sxge, q, mp);

		break;
	}
	switch (cmd) {
	case ND_GET:
	case ND_SET:
/* 		sxge_param_ioctl(sxge, q, mp, iocp); */
		break;

	case LB_GET_MODE:
	case LB_SET_MODE:
	case LB_GET_INFO_SIZE:
	case LB_GET_INFO:
		sxge_loopback_ioctl(sxge, q, mp, iocp);
		break;
	case SXGE_EPS_BYPASS_RD:
		SXGE_DBG((sxge, (SXGE_GLD_BLK | SXGE_INFO_PRI),
		    "SXGE_EPS_BYPASS_RD cmd [%d]", cmd));
		if (sxge_eb_rd_ioctl(sxge, mp->b_cont) < 0)
			miocnak(q, mp, 0, -1);
		else
			miocack(q, mp, sizeof (sxge_eb_cmd_t), 0);
		break;
	case SXGE_EPS_BYPASS_WR:
		SXGE_DBG((sxge, (SXGE_GLD_BLK | SXGE_INFO_PRI),
		    "SXGE_EPS_BYPASS_WR cmd [%d]", cmd));
		sxge_eb_wr_ioctl(sxge, mp->b_cont);
		miocack(q, mp, 0, 0);
		break;
	default:
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		    "Unknown ioctl cmd [%d]"));
		break;
	}
}

/*
 * Obtain the MAC's capabilities and associated data from
 * the driver.
 */
static boolean_t
sxge_m_getcapab(void *arg, mac_capab_t cap, void *cap_data)
{
	sxge_t *sxge = (sxge_t *)arg;

	SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "==> sxge_m_getcapab"));

	switch (cap) {
	case MAC_CAPAB_HCKSUM: {
		uint32_t *tx_hcksum_flags = cap_data;
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		    "==> sxge_m_getcapab, MAC_CAPAB_HCKSUM"));

		/*
		 * We advertise our capabilities only if tx hcksum offload is
		 * enabled.On receive, the stack will accept checksummed
		 * packets anyway, even if we haven't said we can deliver
		 * them.
		 */
		if (!sxge_tx_csum_en)
			return (B_FALSE);
		*tx_hcksum_flags = sxge_tx_csum_flags;
		break;
	}
	case MAC_CAPAB_RINGS: {
		mac_capab_rings_t *cap_rings = cap_data;
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		    "==> sxge_m_getcapab, MAC_CAPAB_RINGS"));

		MUTEX_ENTER(&sxge->gen_lock);

		cap_rings->mr_version = MAC_RINGS_VERSION_1;
		cap_rings->mr_gget = sxge_group_get;
		cap_rings->mr_rget = sxge_fill_ring;
		cap_rings->mr_gaddring = NULL;
		cap_rings->mr_gremring = NULL;

		switch (cap_rings->mr_type) {
		case MAC_RING_TYPE_RX:
			cap_rings->mr_group_type = MAC_GROUP_TYPE_STATIC;
			cap_rings->mr_rnum = sxge->num_rx_rings;
			cap_rings->mr_gnum = sxge_grps_en?sxge->num_rx_groups:0;
			break;
		case MAC_RING_TYPE_TX:
			cap_rings->mr_group_type = MAC_GROUP_TYPE_STATIC;
			cap_rings->mr_rnum = sxge->num_tx_rings;
			cap_rings->mr_gnum = sxge->num_tx_groups;
			break;
		default:
			break;
		}

		MUTEX_EXIT(&sxge->gen_lock);
		break;
	}

	default:
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		    "<== sxge_m_getcapab, unkown [0x%x]", cap));
		return (B_FALSE);
	}
	return (B_TRUE);
}

static boolean_t
sxge_param_locked(mac_prop_id_t pr_num)
{
	/*
	 * All adv_* parameters are locked (read-only) while
	 * the device is in any sort of loopback mode ...
	 */
	switch (pr_num) {
		case MAC_PROP_ADV_1000FDX_CAP:
		case MAC_PROP_EN_1000FDX_CAP:
		case MAC_PROP_ADV_1000HDX_CAP:
		case MAC_PROP_EN_1000HDX_CAP:
		case MAC_PROP_ADV_100FDX_CAP:
		case MAC_PROP_EN_100FDX_CAP:
		case MAC_PROP_ADV_100HDX_CAP:
		case MAC_PROP_EN_100HDX_CAP:
		case MAC_PROP_ADV_10FDX_CAP:
		case MAC_PROP_EN_10FDX_CAP:
		case MAC_PROP_ADV_10HDX_CAP:
		case MAC_PROP_EN_10HDX_CAP:
		case MAC_PROP_AUTONEG:
		case MAC_PROP_FLOWCTRL:
		return (B_TRUE);
	}
	return (B_FALSE);
}

/*
 * callback functions for set/get of properties
 */
/* ARGSUSED */
static int
sxge_m_setprop(void *arg, const char *pr_name, mac_prop_id_t pr_num,
    uint_t pr_valsize, const void *pr_val)
{
	sxge_t		 *sxgep = arg;
	int		err = 0;

	SXGE_DBG((sxgep, (SXGE_GLD_BLK | SXGE_INFO_PRI),
	    "==> sxge_m_setprop"));
	MUTEX_ENTER(&sxgep->gen_lock);

	if (sxgep->lb_mode != sxge_lb_normal &&
	    sxge_param_locked(pr_num)) {
		/*
		 * All adv_* parameters are locked (read-only)
		 * while the device is in any sort of loopback mode.
		 */
		SXGE_DBG((sxgep, (SXGE_GLD_BLK | SXGE_INFO_PRI),
		    "==> sxge_m_setprop: loopback mode: read only"));
		MUTEX_EXIT(&sxgep->gen_lock);
		return (EBUSY);
	}

	switch (pr_num) {

	case MAC_PROP_MTU: {
		uint32_t cur_mtu, new_mtu; /* , old_framesize; */

		cur_mtu = sxgep->vmac.maxframesize;
		ASSERT(pr_valsize >= sizeof (new_mtu));
		bcopy(pr_val, &new_mtu, sizeof (new_mtu));

		SXGE_DBG((sxgep, (SXGE_GLD_BLK | SXGE_INFO_PRI),
		    "==> sxge_m_setprop: set MTU: %d is_jumbo %d",
		    new_mtu, sxgep->vmac.is_jumbo));

		if (new_mtu == cur_mtu) {
			err = 0;
			break;
		}

		if (sxgep->sxge_mac_state == SXGE_MAC_STARTED) {
			err = EBUSY;
			break;
		}

		if ((new_mtu < sxge_max_frame_sz - SXGE_EHEADER_VLAN) ||
		    (new_mtu > sxge_jumbo_frame_sz - SXGE_EHEADER_VLAN)) {
			err = EINVAL;
			break;
		}

		/* old_framesize = (uint32_t)sxgep->vmac.maxframesize; */
		sxgep->vmac.maxframesize = (uint16_t)
		    (new_mtu + SXGE_EHEADER_VLAN);
		sxgep->vmac.default_mtu = new_mtu;
		sxgep->vmac.is_jumbo = (new_mtu > sxge_max_frame_sz);

		SXGE_DBG((sxgep, (SXGE_GLD_BLK | SXGE_INFO_PRI),
		    "==> sxge_m_setprop: set MTU: %d maxframe %d",
		    new_mtu, sxgep->vmac.maxframesize));
		break;
	}

reprogram:
		err = EINVAL;
		break;
	default:
		err = ENOTSUP;
		break;
	}

	MUTEX_EXIT(&sxgep->gen_lock);

	SXGE_DBG((sxgep, (SXGE_GLD_BLK | SXGE_INFO_PRI),
	    "<== sxge_m_setprop (return %d)", err));
	return (err);
}

/* ARGSUSED */
static int
sxge_m_getprop(void *arg, const char *pr_name, mac_prop_id_t pr_num,
    uint_t pr_valsize, void *pr_val)
{
	sxge_t		*sxgep = arg;
	SXGE_DBG((sxgep, (SXGE_GLD_BLK | SXGE_INFO_PRI),
	    "==> sxge_m_getprop: pr_num %d", pr_num));

	switch (pr_num) {
	case MAC_PROP_DUPLEX:
		*(uint8_t *)pr_val = sxgep->link_duplex;
		break;

	case MAC_PROP_SPEED: {
		uint64_t val;

		val = sxgep->link_speed * 1000000ull;

		ASSERT(pr_valsize >= sizeof (val));
		bcopy(&val, pr_val, sizeof (val));
		break;
	}

	case MAC_PROP_STATUS: {
		link_state_t state = sxgep->link_state ?
		    LINK_STATE_UP : LINK_STATE_DOWN;

		ASSERT(pr_valsize >= sizeof (state));
		bcopy(&state, pr_val, sizeof (state));
		break;
	}

	case MAC_PROP_AUTONEG:
		*(uint8_t *)pr_val = (uint8_t)sxgep->link_autoneg;
		break;

	default:
		return (ENOTSUP);
	}

	return (0);
}

/* ARGSUSED */
static void
sxge_priv_propinfo(const char *pr_name, mac_prop_info_handle_t pr_hdl)
{

	cmn_err(CE_CONT, "sxge_priv_propinfo");

	SXGE_DBG((NULL, (SXGE_GLD_BLK|SXGE_INFO_PRI),
	    "==> sxge_priv_propinfo"));

}

/* ARGSUSED */
static void
sxge_m_propinfo(void *arg, const char *pr_name, mac_prop_id_t pr_num,
    mac_prop_info_handle_t prh)
{
	sxge_t		*sxge = (sxge_t *)arg;
	/* int		err = 0; */ /* unused */

	cmn_err(CE_CONT, "sxge_m_propinfo");

	SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "==> sxge_m_propinfo"));

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
		/*
		 * Note that read-only properties don't need to
		 * provide default values since they cannot be
		 * changed by the administrator.
		 */
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
		mac_prop_info_set_default_link_flowctrl(prh, LINK_FLOWCTRL_RX);
		break;

	case MAC_PROP_MTU:
		mac_prop_info_set_range_uint32(prh,
		    sxge_max_frame_sz - SXGE_EHEADER_VLAN,
		    sxge_jumbo_frame_sz - SXGE_EHEADER_VLAN);
		break;
	case MAC_PROP_PRIVATE:
		sxge_priv_propinfo(pr_name, prh);
		break;
	}

}

void
sxge_check_hw_state(sxge_t *sxge)
{
	/* Check for TX hang or any hardware hang */
	/* This function will be called when the watchdog timer expires */

	if (sxge->sxge_state & SXGE_STARTED) {

	if (SXGE_FM_CHECK_ACC_HANDLE(sxge, sxge->pio_hdl.regh) != DDI_FM_OK) {
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		    "%d Bad register acc handle", sxge->instance));
	}

	sxge->sxge_timerid = sxge_start_timer(sxge,
	    sxge_check_hw_state, SXGE_CHECK_TIMER);
	}
}

/*
 * Callback function for MAC layer to register all rings.
 */
/* ARGSUSED */
void
sxge_fill_ring(void *arg, mac_ring_type_t rtype, const int rg_index,
	const int idx, mac_ring_info_t *infop, mac_ring_handle_t rh)
{
	sxge_t *sxge = (sxge_t *)arg;
	/* mac_intr_t *mintr = &infop->mri_intr; */	/* set but not used */

	SXGE_DBG((NULL, (SXGE_INIT_BLK | SXGE_INFO_PRI),
	    "sxge_fill_ring: sxgep = %llx", sxge));

	ASSERT(sxge != NULL);
	ASSERT(infop != NULL);

	switch (rtype) {
	case MAC_RING_TYPE_RX: {
		sxge_ring_handle_t    *rhp;
		mac_intr_t		sxge_mac_intr;

		ASSERT((idx >= 0) && (idx < SXGE_MAX_RDCS));
		rhp = &sxge->rx_ring_handles[idx];
		rhp->ring_handle = rh;

		bzero(&sxge_mac_intr, sizeof (sxge_mac_intr));
		sxge_mac_intr.mi_enable =
		    (mac_intr_enable_t)sxge_disable_poll;
		sxge_mac_intr.mi_disable =
		    (mac_intr_disable_t)sxge_enable_poll;
		infop->mri_driver = (mac_ring_driver_t)rhp;
		infop->mri_start = sxge_rx_ring_start;
		infop->mri_stop = sxge_rx_ring_stop;
		infop->mri_intr = sxge_mac_intr;
		infop->mri_poll = sxge_rx_ring_poll_outer;
		infop->mri_stat = sxge_rx_ring_stat;
		infop->mri_flags = NULL;
		rhp->mr_valid = 1;
		break;
	}
	case MAC_RING_TYPE_TX: {
		sxge_ring_handle_t	 *rhp;
		mac_intr_t *mintr = &infop->mri_intr;

		ASSERT((idx >= 0) && (idx < SXGE_MAX_TDCS));
		rhp = &sxge->tx_ring_handles[idx];
		rhp->ring_handle = rh;

		mintr->mi_ddi_handle = NULL;

		infop->mri_driver = (mac_ring_driver_t)rhp;
		infop->mri_start = NULL; /* sxge_tx_ring_start; */
		infop->mri_stop = NULL; /* sxge_tx_ring_stop; */
		infop->mri_tx = sxge_tx_ring_send;
		infop->mri_stat = sxge_tx_ring_stat;
		infop->mri_flags = NULL;
		break;
	}
	default:
		break;
	}
}

void
sxge_group_get(void *arg, mac_ring_type_t type, int groupid,
	mac_group_info_t *infop, mac_group_handle_t gh)
{
	sxge_t		 *sxge = (sxge_t *)arg;
	sxge_ring_group_t	 *group;

	SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	"==> sxge_group_get"));

	switch (type) {
	case MAC_RING_TYPE_RX:
		group = &sxge->rx_groups[groupid];
		group->sxge = sxge;
		group->ghandle = gh;
		group->index = groupid;
		group->type = type;

		infop->mgi_driver = (mac_group_driver_t)group;
		infop->mgi_start = sxge_rx_group_start;
		infop->mgi_stop = sxge_rx_group_stop;
		infop->mgi_addmac = sxge_rx_group_add_mac;
		infop->mgi_remmac = sxge_rx_group_rem_mac;
		infop->mgi_count = sxge->num_rx_rings;
		break;

	case MAC_RING_TYPE_TX:
		group = &sxge->tx_groups[groupid];
		group->sxge = sxge;
		group->ghandle = gh;
		group->index = groupid;
		group->type = type;
		infop->mgi_driver = (mac_group_driver_t)group;
		infop->mgi_count = sxge->num_tx_rings;
		break;
	}
}

static int
sxge_regs_map(sxge_t *sxge)
{
	int		ddi_status = SXGE_SUCCESS;
	char		buf[MAXPATHLEN + 1];
	char		*devname;
	off_t		regsize;
	int		status = SXGE_SUCCESS;
	uint_t		bar = 0;

	devname = ddi_pathname(sxge->dip, buf);
	ASSERT(strlen(devname) > 0);
	SXGE_DBG((sxge, (SXGE_INIT_BLK | SXGE_INFO_PRI),
	    "sxge_regs_map: pathname devname %s", devname));

	(void) ddi_dev_regsize(sxge->dip, bar, &regsize);

	ddi_status = ddi_regs_map_setup(sxge->dip, bar,
	    (caddr_t *)&(sxge->pcicfg_hdl.regp), 0, 0,
	    &sxge_dev_reg_acc_attr, &sxge->pcicfg_hdl.regh);

	SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "sxge_regs_map: cfgp %llx size %lx",
	    sxge->pcicfg_hdl.regp, regsize));

	if (ddi_status != SXGE_SUCCESS) {
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		    "sxge_regs_map: "
		    "ddi_regs_map_setup, sxge bus config regs failed"));
		status = EIO;
		goto sxge_map_exit;
	}
	sxge->pcicfg_hdl.sxge = (void *)sxge;

	/* set up the msi/msi-x mapped register */
	bar++;
	(void) ddi_dev_regsize(sxge->dip, bar, &regsize);
	if (regsize == 0x100000)
	goto sxge_map_pio;

	ddi_status = ddi_regs_map_setup(sxge->dip, bar,
	    (caddr_t *)&(sxge->msix_hdl.regp), 0, 0,
	    &sxge_dev_reg_acc_attr, &sxge->msix_hdl.regh);

	if (ddi_status != SXGE_SUCCESS) {
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		    "sxge_regs_map: "
		    "ddi_regs_map_setup, MSIX regs failed"));
		status = EIO;
		ddi_regs_map_free(&sxge->pcicfg_hdl.regh);
	}
	sxge->msix_hdl.sxge = (void *)sxge;

	SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "sxge_regs_map: regp %llx size %lx",
	    sxge->msix_hdl.regp, regsize));

	bar++;
	(void) ddi_dev_regsize(sxge->dip, bar, &regsize);
sxge_map_pio:

	/* set up the device mapped register */
	ddi_status = ddi_regs_map_setup(sxge->dip, bar,
	    (caddr_t *)&(sxge->pio_hdl.regp), 0, 0,
	    &sxge_dev_reg_acc_attr, &sxge->pio_hdl.regh);

	if (ddi_status != SXGE_SUCCESS) {
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		    "sxge_regs_map: "
		    "ddi_regs_map_setup, PIO regs failed"));
		status = EIO;
		ddi_regs_map_free(&sxge->pcicfg_hdl.regh);
		if (sxge->msix_hdl.regh) {
			ddi_regs_map_free(&sxge->msix_hdl.regh);
			sxge->msix_hdl.regh = NULL;
		}
	}
	sxge->pio_hdl.sxge = (void *)sxge;

	if (SXGE_FM_CHECK_ACC_HANDLE(sxge, sxge->pio_hdl.regh) != DDI_FM_OK) {
		SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		    "%d Bad register acc handle", sxge->instance));
		status = SXGE_FAILURE;
	}
	SXGE_DBG((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "sxge_regs_map: regp %llx size %lx",
	    sxge->pio_hdl.regp, regsize));

sxge_map_exit:
	return (status);
}

static int
sxge_regs_unmap(sxge_t *sxge)
{
	ddi_regs_map_free(&sxge->pio_hdl.regh);
	ddi_regs_map_free(&sxge->msix_hdl.regh);
	ddi_regs_map_free(&sxge->pcicfg_hdl.regh);
	return (0);
}

static int
sxge_rx_ring_start(mac_ring_driver_t rdriver, uint64_t mr_gen_num)
{
	sxge_ring_handle_t	 *rhp = (sxge_ring_handle_t *)rdriver;
	sxge_t			 *sxgep = rhp->sxge;
	uint32_t		channel = rhp->index;
	rdc_state_t		 *ring;

	SXGE_DBG((sxgep, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "sxge_rx_ring_start: init rx ring %d\n", channel));

	MUTEX_ENTER(&sxgep->gen_lock);
	ring = &sxgep->rdc[channel];
	ring->rhdl = (void *)rhp->ring_handle;
	ring->sxgep = (void *)sxgep;
	rhp->mr_gen_num = mr_gen_num;
	rhp->started = 1;
	MUTEX_EXIT(&sxgep->gen_lock);

	return (0);
}

static void
sxge_rx_ring_stop(mac_ring_driver_t rdriver)
{
	sxge_ring_handle_t    *rhp = (sxge_ring_handle_t *)rdriver;
	sxge_t			 *sxgep = rhp->sxge;
	uint32_t		channel = rhp->index;
	rdc_state_t		 *ring;

	SXGE_DBG((sxgep, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "sxge_rx_ring_stop: stop rx ring %d\n", channel));

	MUTEX_ENTER(&sxgep->rdc_lock[channel]);
	ring = &sxgep->rdc[channel];
	ring->rhdl = (void *)NULL;
	/* ring->sxgep = (void *)NULL; */
	rhp->mr_gen_num = 0;
	rhp->started = 0;
	MUTEX_EXIT(&sxgep->rdc_lock[channel]);
}

/* ARGSUSED */
static int
sxge_rx_group_start(mac_group_driver_t gdriver)
{
	SXGE_DBG((NULL, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "==> sxge_rx_group_start"));
	return (0);
}

/* ARGSUSED */
static void
sxge_rx_group_stop(mac_group_driver_t gdriver)
{
	SXGE_DBG((NULL, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "==> sxge_rx_group_stop"));
}

/* ARGSUSED */
static int
sxge_rx_group_add_mac(void *arg, const uint8_t *mac_addr, uint64_t flags)
{
	sxge_ring_group_t	*group = arg;
	sxge_t			*sxge = group->sxge;
	struct			ether_addr addrp;
	int			result = SXGE_SUCCESS;

	SXGE_DBG((NULL, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "==> sxge_rx_group_add_mac"));

	bcopy(mac_addr, (uint8_t *)&addrp, ETHERADDRL);

	MUTEX_ENTER(&sxge->gen_lock);

	if (sxge->mbox.ready != B_TRUE) {
		result = sxge_hosteps_mbx_init(sxge);
		if (result != 0) {
			SXGE_DBG((sxge, (SXGE_MBOX_BLK | SXGE_ERR_PRI),
			    " Failed to init Host to EPS Mailbox"));
			MUTEX_EXIT(&sxge->gen_lock);
			return (ECANCELED);
		}
	}

	/* Filter out the factory MAC address. It can not be changed. */
	if (bcmp(sxge->vmac.addr, mac_addr, ETHERADDRL) != 0) {
		result = sxge_add_ucast_addr(sxge, &addrp);
	}

	MUTEX_EXIT(&sxge->gen_lock);
	return (result);
}

/* ARGSUSED */
static int
sxge_rx_group_rem_mac(void *arg, const uint8_t *mac_addr)
{
	sxge_ring_group_t	*group = arg;
	sxge_t			*sxge = group->sxge;
	struct			ether_addr addrp;
	int			result = SXGE_SUCCESS;

	SXGE_DBG((NULL, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "==> sxge_rx_group_rem_mac"));

	bcopy(mac_addr, (uint8_t *)&addrp, ETHERADDRL);

	MUTEX_ENTER(&sxge->gen_lock);

	if (sxge->mbox.ready != B_TRUE) {
		result = sxge_hosteps_mbx_init(sxge);
		if (result != 0) {
			SXGE_DBG((sxge, (SXGE_MBOX_BLK | SXGE_ERR_PRI),
			    " Failed to init Host to EPS Mailbox"));
			MUTEX_EXIT(&sxge->gen_lock);
			return (ECANCELED);
		}
	}

	/* Filter out the factory MAC address. It can not be changed. */
	if (bcmp(sxge->vmac.addr, mac_addr, ETHERADDRL) != 0) {
		result = sxge_del_ucast_addr(sxge, &addrp);
	}

	MUTEX_EXIT(&sxge->gen_lock);
	return (result);
}

/*
 * Disable polling for a ring and enable its interrupt.
 */
int
sxge_disable_poll(void *arg)
{
	sxge_ring_handle_t	 *rhp = (sxge_ring_handle_t *)arg;
	sxge_t			 *sxgep = rhp->sxge;
	uint_t			channel = rhp->index;
	rdc_state_t		 *ring;

	SXGE_DBG((sxgep, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "sxge_disable_poll: mask rx ring intrs %d\n", channel));

	if (!sxge_gld_poll_en)
		return (0);

	MUTEX_ENTER(&sxgep->rdc_lock[channel]);

	rhp->polled = 0;
	ring = &sxgep->rdc[channel];
	(void) sxge_rdc_mask(ring, B_FALSE);

	MUTEX_EXIT(&sxgep->rdc_lock[channel]);


	SXGE_DBG((sxgep, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "<==sxge_disable_poll\n"));
	return (0);
}

/*
 * Enable polling for a ring. Interrupt for the ring is disabled when
 * the sxge interrupt comes (see sxge_rx_intr).
 */
int
sxge_enable_poll(void *arg)
{
	sxge_ring_handle_t	 *rhp = (sxge_ring_handle_t *)arg;
	sxge_t			 *sxgep = rhp->sxge;
	uint_t			channel = rhp->index;
	rdc_state_t		 *ring;

	SXGE_DBG((sxgep, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "sxge_enable_poll: mask rx ring intrs %d\n", channel));

	if (!sxge_gld_poll_en)
		return (0);

	MUTEX_ENTER(&sxgep->rdc_lock[channel]);

	rhp->polled = 1;
	ring = &sxgep->rdc[channel];
	(void) sxge_rdc_mask(ring, B_TRUE);

	MUTEX_EXIT(&sxgep->rdc_lock[channel]);

	SXGE_DBG((sxgep, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "<==sxge_enable_poll\n"));
	return (0);
}

/*
 * Retrieve a value for one of the statistics for a particular rx ring
 */
/* ARGSUSED */
int
sxge_rx_ring_stat(mac_ring_driver_t rdriver, uint_t stat, uint64_t *val)
{
	/* sxge_ring_handle_t    *rhp = (sxge_ring_handle_t *)rdriver; */
	/* set but not used */
	/* sxge_t	*sxge = rhp->sxge; */	/* set but not used */



	*val = 0;

	return (0);

}

void
sxge_rx_soft_tmr(void *arg)
{
	sxge_t			 *sxge = (sxge_t *)arg;

	if (sxge->soft_poll_en)
		ddi_trigger_softintr(sxge->idp);
}


uint_t
sxge_rx_soft_poll(char *arg)
{
	sxge_t			 *sxge = (sxge_t *)arg;
	mblk_t			 *mp;
	sxge_ring_handle_t	 *rhp, *rhp_f;
	uint_t			idx = 0, cnt = 0, i = 0;

sxge_rx_soft_poll_start:

	for (i = 0; i < sxge_soft_poll_outer; i++)
	for (idx = 0; idx < sxge->num_rx_rings; idx++) {

	rhp = &sxge->rx_ring_handles[idx];

	if (rhp->polled) continue;

	SXGE_DBG0((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "sxge_rx_soft_poll: sxge = %llx ring = %d\n", arg, idx));

	cnt = 0;
	do {
	mp = sxge_rx_ring_poll((void *)rhp, sxge->vmac.maxframesize);

	if (mp) {

		if (sxge_loop_fwd_en) {

			SXGE_DBG0((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
			"sxge_rx_soft_poll: tx_ring_send idx = %d mp = %llx\n",
			    idx, mp));

			rhp_f = &sxge->tx_ring_handles[idx];
			(void) sxge_tx_ring_send(rhp_f, mp);

		} else {

			SXGE_DBG0((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
			"sxge_rx_soft_poll: mac_rx_ring idx = %d mp = %llx\n",
			    idx, mp));
			mac_rx_ring(sxge->mach, rhp->mr_valid ?
			    rhp->ring_handle:NULL, mp, rhp->mr_gen_num);
		}
	}

	} while ((mp != NULL) && (cnt++ < sxge_soft_poll_max));
	}

	if (sxge->soft_poll_en) {

		if (!sxge_soft_poll_timer) goto sxge_rx_soft_poll_start;

		sxge->soft_tmr_id = sxge_start_timer(sxge,
		    sxge_rx_soft_tmr, sxge_soft_poll_timer);
	}
	return (SXGE_INTR_CLAIMED);
}

mblk_t *
sxge_rx_ring_poll_outer(void *arg, int bytes, int pkts)
{
	uint_t bytes_cur = 0, pkts_cur = 0;
	mblk_t *mp = NULL, *mp_first = NULL, *mp_cur = NULL;

	if (!sxge_gld_poll_en)
		return (NULL);

	do {
		mp = sxge_rx_ring_poll(arg, bytes);
		if (mp != NULL) {
			if (mp_first == NULL) mp_first = mp;
			bytes_cur += msgsize(mp);
			pkts_cur++;
			if (mp_cur != NULL) mp_cur->b_next = mp;
			mp_cur = mp;
		}
	} while ((mp != NULL) && (bytes_cur <= bytes) && (pkts_cur <= pkts));

	return (mp_first);
}

uint_t
sxge_rx_soft_poll_idx(char *arg, uint_t idx)
{
	sxge_t			 *sxge = (sxge_t *)arg;
	mblk_t			 *mp;
	sxge_ring_handle_t	 *rhp, *rhp_f;
	uint_t			cnt = 0, i = 0;

	for (i = 0; i < sxge_soft_poll_outer; i++) {

	rhp = &sxge->rx_ring_handles[idx];

	if (sxge_intr_poll_en && rhp->polled)
		return (SXGE_INTR_CLAIMED);

	SXGE_DBG0((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "sxge_rx_soft_poll_idx: sxge = %llx ring = %d\n", arg, idx));
	cnt = 0;
	do {
	mp = sxge_rx_ring_poll((void *)rhp, sxge->vmac.maxframesize);

	if (mp) {

		if (sxge_loop_fwd_en) {

			SXGE_DBG0((sxge, (SXGE_ERR_BLK | SXGE_ERR_PRI),
			"sxge_rx_soft_poll_idx: tx_ring_send idx %d mp %llx\n",
			    idx, mp));

			rhp_f = &sxge->tx_ring_handles[idx];
			(void) sxge_tx_ring_send(rhp_f, mp);

		} else {
			mac_rx_ring(sxge->mach, rhp->mr_valid ?
			    rhp->ring_handle:NULL, mp, rhp->mr_gen_num);

		}
	}

	} while ((mp != NULL) && (cnt++ < sxge_soft_poll_max));

	/* sxge_soft_poll_outer */
	}

	return (SXGE_INTR_CLAIMED);
}

/*
 * Poll 'bytes_to_pickup' bytes of message from the rx ring.
 */
mblk_t *
sxge_rx_ring_poll(void *arg, int bytes_to_pickup)
{
	sxge_ring_handle_t	 *rhp = (sxge_ring_handle_t *)arg;
	sxge_t			 *sxgep = rhp->sxge;
	uint32_t		channel = rhp->index; /* 0 */
	rdc_state_t		 *ring;
	mblk_t			 *mblk = NULL;
	uint_t			flags = 0;
	/* int			status; */	/* unused */

	ASSERT(rhp != NULL);

	if (rhp->started == 0) {
		return (NULL);
	}

	SXGE_DBG0((sxgep, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "-->> sxge_rx_ring_poll: recv on rx ring %d\n", channel));

	/* channel = sxgep->pt_config.hw_config.tdc.start + rhp->index; */
	ring = &sxgep->rdc[channel];

	MUTEX_ENTER(&sxgep->rdc_lock[channel]);

	mblk = sxge_rdc_recv(ring, (uint_t)bytes_to_pickup, &flags);

	if (sxge_rx_csum_en && !flags) {
		if (mblk)
			mac_hcksum_set(mblk, 0, 0, 0, 0,
			    HCK_FULLCKSUM_OK | HCK_IPV4_HDRCKSUM_OK);
	}
	MUTEX_EXIT(&sxgep->rdc_lock[channel]);

	SXGE_DBG0((sxgep, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "<<-- sxge_rx_ring_poll: returning ring %d\n", channel));

	return (mblk);
}

/*
 * Retrieve a value for one of the statistics for a particular tx ring
 */
/* ARGSUSED */
int
sxge_tx_ring_stat(mac_ring_driver_t rdriver, uint_t stat, uint64_t *val)
{
	/* sxge_ring_handle_t    *rhp = (sxge_ring_handle_t *)rdriver; */
	/* set but not used */
	/* sxge_t	*sxge = rhp->sxge; */	/* set but not used */



	*val = 0;
	return (0);
}

mblk_t *
sxge_tx_ring_send(void *arg, mblk_t *mp)
{
	sxge_ring_handle_t	*rhp = (sxge_ring_handle_t *)arg;
	sxge_t			*sxgep = rhp->sxge;
	uint32_t		channel = rhp->index;
	tdc_state_t		*ring;
	uint_t			flags = 0;
	uint32_t		pflags = 0;
	int			status = -1, i = 0;
	mblk_t			*nmp = mp, *next = mp;

	SXGE_DBG0((sxgep, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "-->> sxge_tx_ring_send: send on tx ring %d\n", channel));

	while (nmp != NULL) {

		status = -1; i = 0;
		next = nmp->b_next;

		if ((rhp->started == 0) ||
		    (sxge_link_chk_en?(sxgep->link_state != 1):0)) {
			break;
		}

		ASSERT(rhp != NULL);
		ring = &sxgep->tdc[channel];

		MUTEX_ENTER(&sxgep->tdc_lock[channel]);

		/*
		 * Transmit the packet.
		 */

		SXGE_DBG0((sxgep, (SXGE_ERR_BLK | SXGE_ERR_PRI),
		    "sxge_tx_ring_send: sxge_tdc_send ring %d, msgsize %d\n",
		    channel, msgsize(nmp)));

		mac_hcksum_get(nmp, NULL, NULL, NULL, NULL, &pflags);
		if (pflags & HCK_FULLCKSUM) flags = SXGE_TX_CKENB;

		while ((status != 0) && (i++ < sxge_tx_retries))
		status = sxge_tdc_send(ring, nmp, &flags);
		if (status != 0) freemsg(nmp);

		MUTEX_EXIT(&sxgep->tdc_lock[channel]);

		nmp = next;
	}

	SXGE_DBG0((sxgep, (SXGE_ERR_BLK | SXGE_ERR_PRI),
	    "<<-- sxge_tx_ring_send"));
	return (nmp);
}

int
sxge_eb_rd_ioctl(sxge_t *sxge, mblk_t *mp)
{
	sxge_eb_cmd_t cmd;
	sxge_pio_handle_t hdl;

	if (sxge == NULL)
		return (-1);

	hdl = sxge->pio_hdl;

	bcopy((char *)mp->b_rptr, (char *)&cmd, sizeof (sxge_eb_cmd_t));
	/* hdl.sxge = sxge; */

	cmd.offset &= 0xfffffULL;
	cmd.data = SXGE_GET64(hdl, cmd.offset);

	bcopy((char *)&cmd, (char *)mp->b_rptr, sizeof (sxge_eb_cmd_t));

	return (0);
}

void
sxge_eb_wr_ioctl(sxge_t *sxge, mblk_t *mp)
{
	sxge_eb_cmd_t cmd;
	sxge_pio_handle_t hdl;

	if (sxge == NULL)
		return;

	hdl = sxge->pio_hdl;

	bcopy((char *)mp->b_rptr, (char *)&cmd, sizeof (sxge_eb_cmd_t));
	/* hdl.sxge = sxge; */

	cmd.offset &= 0xfffffULL;
	SXGE_PUT64(hdl, cmd.offset, cmd.data);
}

static int
sxge_hosteps_mbx_reset(sxge_t *sxge)
{
	niu_mb_status_t	cs;
	int count = 1000;

	cs.value = 0x0ULL;
	cs.bits.func_rst = 1;
	SXGE_PUT64(sxge->pio_hdl, NIU_MB_STAT, cs.value);

	/*
	 * Wait for reset to complete.
	 */
	while (count > 0) {
		cs.value = SXGE_GET64(sxge->pio_hdl, NIU_MB_STAT);

		if (cs.bits.func_rst_done == 1) {
			cs.value = 0x0ULL;
			cs.bits.func_rst_done = 1;
			SXGE_PUT64(sxge->pio_hdl, NIU_MB_STAT, cs.value);
			break;
		}
		SXGE_UDELAY(sxge, 1, sxge_delay_busy);
		count--;
	}

	if (count == 0) {
		SXGE_DBG((sxge, (SXGE_MBOX_BLK | SXGE_ERR_PRI),
		    "sxge_hosteps_mbx_reset: reset failed"));
		return (-1);
	}

	SXGE_DBG((sxge, (SXGE_MBOX_BLK | SXGE_INFO_PRI),
	    "mbox: reset complete\n"));

	return (0);
}

/*
 * Initialize host-eps mailbox
 */
static int
sxge_hosteps_mbx_init(sxge_t *sxge)
{
	int stat = DDI_SUCCESS;
	niu_mb_int_msk_t mask;

	/* MBX only reset once after attach */
	if (sxge->mbox.ready == B_TRUE)
		return (stat);

	if ((stat = sxge_hosteps_mbx_reset(sxge)) != 0)
		return (stat);

	/* Unmask all interrupts for now */
	mask.value = 0x0ULL;
	SXGE_PUT64(sxge->pio_hdl, NIU_MB_MSK, mask.value);

	bzero(&sxge->mac_pool, sizeof (sxge->mac_pool));
	sxge->mbox.ready = B_TRUE;
	return (stat);
}



/* ARGSUSED */
static kstat_t *
sxge_setup_local_kstat(sxge_t *sxge, int instance, char *name,
	const sxge_kstat_index_t *ksip, size_t count,
	int (*update) (kstat_t *, int))
{
	kstat_t *ksp;
	kstat_named_t *knp;
	int i;

	ksp = kstat_create(MODULE_NAME, instance, name, "net",
	    KSTAT_TYPE_NAMED, count, 0);
	if (ksp == NULL)
		return (NULL);

	ksp->ks_private = (void *)sxge;
	ksp->ks_update = update;
	knp = ksp->ks_data;

	for (i = 0; ksip[i].name != NULL; i++) {
		kstat_named_init(&knp[i], ksip[i].name, ksip[i].type);
	}

	kstat_install(ksp);
	return (ksp);
}

typedef enum {
	VMAC_STAT_TX_FRAME_CNT = 0,
	VMAC_STAT_TX_BYTE_CNT,
	VMAC_STAT_RX_FRAME_CNT_D,
	VMAC_STAT_RX_BYTE_CNT_D,
	VMAC_STAT_RX_DROP_FRAME_CNT_D,
	VMAC_STAT_RX_DROP_BYTE_CNT_D,
	VMAC_STAT_RX_MCAST_FRAME_CNT_D,
	VMAC_STAT_RX_BCAST_FRAME_CNT_D,
	VMAC_STAT_RX_FRAME_CNT,
	VMAC_STAT_RX_BYTE_CNT,
	VMAC_STAT_RX_DROP_FRAME_CNT,
	VMAC_STAT_RX_DROP_BYTE_CNT,
	VMAC_STAT_RX_MCAST_FRAME_CNT,
	VMAC_STAT_RX_BCAST_FRAME_CNT,
	VMAC_STAT_END
} sxge_vmac_stat_index_t;

sxge_kstat_index_t sxge_vmac_kstats[] = {
	{VMAC_STAT_TX_FRAME_CNT, KSTAT_DATA_ULONG, "txvmac_frame_cnt"},
	{VMAC_STAT_TX_BYTE_CNT, KSTAT_DATA_ULONG, "txvmac_byte_cnt"},
	{VMAC_STAT_RX_FRAME_CNT_D, KSTAT_DATA_ULONG, "rxvmac_frame_cnt_d"},
	{VMAC_STAT_RX_BYTE_CNT_D, KSTAT_DATA_ULONG, "rxvmac_byte_cnt_d"},
	{VMAC_STAT_RX_DROP_FRAME_CNT_D, KSTAT_DATA_ULONG,
		"rxvmac_drop_frame_cnt_d"},
	{VMAC_STAT_RX_DROP_BYTE_CNT_D, KSTAT_DATA_ULONG,
		"rxvmac_drop_byte_cnt_d"},
	{VMAC_STAT_RX_MCAST_FRAME_CNT_D, KSTAT_DATA_ULONG,
		"rxvmac_mcast_frame_cnt_d"},
	{VMAC_STAT_RX_BCAST_FRAME_CNT_D, KSTAT_DATA_ULONG,
		"rxvmac_bcast_frame_cnt_d"},
	{VMAC_STAT_RX_FRAME_CNT, KSTAT_DATA_ULONG,
		"rxvmac_frame_cnt"},
	{VMAC_STAT_RX_BYTE_CNT, KSTAT_DATA_ULONG,
		"rxvmac_byte_cnt"},
	{VMAC_STAT_RX_DROP_FRAME_CNT, KSTAT_DATA_ULONG,
		"rxvmac_drop_frame_cnt"},
	{VMAC_STAT_RX_DROP_BYTE_CNT, KSTAT_DATA_ULONG, "rxvmac_drop_byte_cnt"},
	{VMAC_STAT_RX_MCAST_FRAME_CNT, KSTAT_DATA_ULONG,
		"rxvmac_mcast_frame_cnt"},
	{VMAC_STAT_RX_BCAST_FRAME_CNT, KSTAT_DATA_ULONG,
		"rxvmac_bcast_frame_cnt"},
	{VMAC_STAT_END, NULL, NULL}
};

/* ARGSUSED */
int
sxge_vmac_stat_update(kstat_t *ksp, int rw)
{
	sxge_t *sxge;
	sxge_vmac_kstat_t *kstatp;
	sxge_vmac_stats_t *statsp;
	int i, j, vni = sxge_vni_num, vmac = sxge_vmac_num;

	sxge = (sxge_t *)ksp->ks_private;
	if (sxge == NULL ||sxge->statsp == NULL)
		return (-1);

	kstatp = (sxge_vmac_kstat_t *)ksp->ks_data;
	statsp = (sxge_vmac_stats_t *)&sxge->statsp->vmac_stats;

	SXGE_DBG((sxge, SXGE_STATS_BLK, "==> sxge_vmac_stat_update"));

	if (rw == KSTAT_WRITE) {
		statsp->tx_frame_cnt = kstatp->tx_frame_cnt.value.ull;
		statsp->tx_byte_cnt = kstatp->tx_byte_cnt.value.ull;
		statsp->rx_frame_cnt = kstatp->rx_frame_cnt.value.ull;
		statsp->rx_byte_cnt = kstatp->rx_byte_cnt.value.ul;
		statsp->rx_drop_frame_cnt = kstatp->rx_drop_frame_cnt.value.ul;
		statsp->rx_drop_byte_cnt = kstatp->rx_drop_byte_cnt.value.ul;
		statsp->rx_mcast_frame_cnt =
		    kstatp->rx_mcast_frame_cnt.value.ul;
		statsp->rx_bcast_frame_cnt =
		    kstatp->rx_bcast_frame_cnt.value.ul;
	} else {

	if (sxge_vni_auto_en) {
	for (i = 0; i < SXGE_MAX_VNI_NUM; i++) {
		if (sxge->vni_arr[i].present == 1 &&
		    sxge->vni_arr[i].vmac_cnt != 0) {
			for (j = 0; j < SXGE_MAX_VNI_VMAC_NUM; j++) {
				if (sxge->vni_arr[i].vmac[j] == 1) {
					vni = i;
					vmac = j;
					break;
				}
			}
		}
	}
	}

	kstatp->tx_frame_cnt.value.ull = statsp->tx_frame_cnt +=
	    SXGE_GET64(sxge->pio_hdl, TXVMAC_FRM_CNT(vni, vmac));
	kstatp->tx_byte_cnt.value.ull = statsp->tx_byte_cnt +=
	    SXGE_GET64(sxge->pio_hdl, TXVMAC_BYTE_CNT(vni, vmac));
	kstatp->rx_frame_cnt_d.value.ull = SXGE_GET64(sxge->pio_hdl,
	    RXVMAC_FRM_CNT_DBG(vni, vmac));
	kstatp->rx_byte_cnt_d.value.ull = SXGE_GET64(sxge->pio_hdl,
	    RXVMAC_BYTE_CNT_DBG(vni, vmac));
	kstatp->rx_drop_frame_cnt_d.value.ull = SXGE_GET64(sxge->pio_hdl,
	    RXVMAC_DROPFM_CNT_DBG(vni, vmac));
	kstatp->rx_drop_byte_cnt_d.value.ull = SXGE_GET64(sxge->pio_hdl,
	    RXVMAC_DROPBT_CNT_DBG(vni, vmac));
	kstatp->rx_mcast_frame_cnt_d.value.ull = SXGE_GET64(sxge->pio_hdl,
	    RXVMAC_MCASTFM_CNT_DBG(vni, vmac));
	kstatp->rx_bcast_frame_cnt_d.value.ul = SXGE_GET64(sxge->pio_hdl,
	    RXVMAC_BCASTFM_CNT_DBG(vni, vmac));

	kstatp->rx_frame_cnt.value.ull = statsp->rx_frame_cnt +=
	    SXGE_GET64(sxge->pio_hdl, RXVMAC_FRM_CNT(vni, vmac));
	kstatp->rx_byte_cnt.value.ull = statsp->rx_byte_cnt +=
	    SXGE_GET64(sxge->pio_hdl, RXVMAC_BYTE_CNT(vni, vmac));
	kstatp->rx_drop_frame_cnt.value.ull = statsp->rx_drop_frame_cnt +=
	    SXGE_GET64(sxge->pio_hdl, RXVMAC_DROPFM_CNT(vni, vmac));
	kstatp->rx_drop_byte_cnt.value.ull = statsp->rx_drop_byte_cnt +=
	    SXGE_GET64(sxge->pio_hdl, RXVMAC_DROPBT_CNT(vni, vmac));
	kstatp->rx_mcast_frame_cnt.value.ull = statsp->rx_mcast_frame_cnt +=
	    SXGE_GET64(sxge->pio_hdl, RXVMAC_MCASTFM_CNT(vni, vmac));
	kstatp->rx_bcast_frame_cnt.value.ul = statsp->rx_bcast_frame_cnt +=
	    SXGE_GET64(sxge->pio_hdl, RXVMAC_BCASTFM_CNT(vni, vmac));
	} /* else */
	SXGE_DBG((sxge, SXGE_STATS_BLK, " <== sxge_vmac_stat_update"));
	return (0);
}

typedef enum {
	RDC_STAT_PACKETS = 0,
	RDC_STAT_BYTES,
	RDC_STAT_ERRORS,
	RDC_STAT_JUMBO_PKTS,
	RDC_STAT_RCR_UNKNOWN_ERR,
	RDC_STAT_RCR_SHA_PAR_ERR,
	RDC_STAT_RBR_PRE_PAR_ERR,
	RDC_STAT_RBR_PRE_EMTY,
	RDC_STAT_RCR_SHADOW_FULL,
	RDC_STAT_RBR_TMOUT,
	RDC_STAT_PEU_RESP_ERR,
	RDC_STAT_CTRL_FIFO_ECC_ERR,
	RDC_STAT_DATA_FIFO_ECC_ERR,
	RDC_STAT_RCRFULL,
	RDC_STAT_RBR_EMPTY,
	RDC_STAT_RBR_EMPTY_FAIL,
	RDC_STAT_RBR_EMPTY_RESTORE,
	RDC_STAT_RBR_FULL,
	RDC_STAT_RCR_INVALIDS,
	RDC_STAT_RCRTO,
	RDC_STAT_RCRTHRES,
	RDC_STAT_PKT_DROP,
	RDC_STAT_END
} sxge_rdc_stat_index_t;

sxge_kstat_index_t sxge_rdc_kstats[] = {
	{RDC_STAT_PACKETS, KSTAT_DATA_UINT64, "rdc_packets"},
	{RDC_STAT_BYTES, KSTAT_DATA_UINT64, "rdc_bytes"},
	{RDC_STAT_ERRORS, KSTAT_DATA_ULONG, "rdc_errors"},
	{RDC_STAT_JUMBO_PKTS, KSTAT_DATA_ULONG, "rdc_jumbo_pkts"},
	{RDC_STAT_RCR_UNKNOWN_ERR, KSTAT_DATA_ULONG, "rdc_rcr_unknown_err"},
	{RDC_STAT_RCR_SHA_PAR_ERR, KSTAT_DATA_ULONG, "rdc_rcr_sha_par_err"},
	{RDC_STAT_RBR_PRE_PAR_ERR, KSTAT_DATA_ULONG, "rdc_rbr_pre_par_err"},
	{RDC_STAT_RBR_PRE_EMTY, KSTAT_DATA_ULONG, "rdc_rbr_pre_empty"},
	{RDC_STAT_RCR_SHADOW_FULL, KSTAT_DATA_ULONG, "rdc_rcr_shadow_full"},
	{RDC_STAT_RBR_TMOUT, KSTAT_DATA_ULONG, "rdc_rbr_tmout"},
	{RDC_STAT_PEU_RESP_ERR, KSTAT_DATA_ULONG, "peu_resp_err"},
	{RDC_STAT_CTRL_FIFO_ECC_ERR, KSTAT_DATA_ULONG, "ctrl_fifo_ecc_err"},
	{RDC_STAT_DATA_FIFO_ECC_ERR, KSTAT_DATA_ULONG, "data_fifo_ecc_err"},
	{RDC_STAT_RCRFULL, KSTAT_DATA_ULONG, "rdc_rcrfull"},
	{RDC_STAT_RBR_EMPTY, KSTAT_DATA_ULONG, "rdc_rbr_empty"},
	{RDC_STAT_RBR_EMPTY_FAIL, KSTAT_DATA_ULONG, "rdc_rbr_empty_fail"},
	{RDC_STAT_RBR_EMPTY_FAIL, KSTAT_DATA_ULONG, "rdc_rbr_empty_restore"},
	{RDC_STAT_RBR_FULL, KSTAT_DATA_ULONG, "rdc_rbrfull"},
	{RDC_STAT_RCR_INVALIDS, KSTAT_DATA_ULONG, "rdc_rcr_invalids"},
	{RDC_STAT_RCRTO, KSTAT_DATA_ULONG, "rdc_rcrto"},
	{RDC_STAT_RCRTHRES, KSTAT_DATA_ULONG, "rdc_rcrthres"},
	{RDC_STAT_PKT_DROP, KSTAT_DATA_ULONG, "rdc_pkt_drop"},
	{RDC_STAT_END, NULL, NULL}
};

#define	RDC_NAME_FORMAT1 "RDC_"
#define	TDC_NAME_FORMAT1 "TDC_"
#define	CH_NAME_FORMAT "%d"

/* ARGSUSED */
int
sxge_rdc_stat_update(kstat_t *ksp, int rw)
{
	sxge_t *sxge;
	sxge_rdc_kstat_t *rdc_kstatp;
	sxge_rx_ring_stats_t *statsp;
	int channel;
	char *ch_name, *end;
	/*
	 * int i, j, vni = sxge_vni_num, vmac = sxge_vmac_num;
	 * uint64_t val;
	 */
	/* used or set but not used */
	uint64_t pbase;

	SXGE_DBG((NULL, SXGE_STATS_BLK, "==> sxge_rdc_stat_update"));

	sxge = (sxge_t *)ksp->ks_private;
	if (sxge == NULL ||sxge->statsp == NULL)
		return (-1);

	ch_name = ksp->ks_name;
	ch_name += strlen(RDC_NAME_FORMAT1);
	channel = mi_strtol(ch_name, &end, 10);

	if (channel > sxge->dma_cnt)
		return (0);

	rdc_kstatp = (sxge_rdc_kstat_t *)ksp->ks_data;
	statsp = &sxge->statsp->rdc_stats[channel];

	pbase = sxge->rdc[channel].pbase;

	rdc_kstatp->ipackets.value.ul = statsp->ipackets +=
	    SXGE_GET64(sxge->pio_hdl, RDC_PKT_CNT_REG);
	rdc_kstatp->pkt_drop.value.ul = statsp->pkt_drop +=
	    SXGE_GET64(sxge->pio_hdl, RDC_DIS_CNT_REG);

	SXGE_DBG((sxge, SXGE_STATS_BLK, " <== sxge_rdc_stat_update"));
	return (0);
}

int
sxge_init_stats(sxge_t *sxge)
{
	int i;
	char stat_name[64];

	if (sxge->statsp == NULL) {
		sxge->statsp = SXGE_ZALLOC(sizeof (sxge_stats_t));

		sxge->statsp->vmac_ksp = sxge_setup_local_kstat(sxge,
		    sxge->instance,
		    "VMAC Stats",
		    &sxge_vmac_kstats[0],
		    VMAC_STAT_END,
		    sxge_vmac_stat_update);

		for (i = 0; i < sxge->dma_cnt; i ++) {
			(void) sprintf(stat_name, "%s"CH_NAME_FORMAT,
			    RDC_NAME_FORMAT1, i);
			sxge->statsp->rdc_ksp[i] = sxge_setup_local_kstat(sxge,
			    sxge->instance,
			    stat_name,
			    &sxge_rdc_kstats[0],
			    RDC_STAT_END,
			    sxge_rdc_stat_update);
		}
	}
	return (0);
}

int
sxge_uninit_stats(sxge_t *sxge)
{
	int i;

	if (sxge->statsp != NULL) {
		if (sxge->statsp->vmac_ksp) {
			kstat_delete(sxge->statsp->vmac_ksp);
			for (i = 0; i < sxge->dma_cnt; i ++) {
				kstat_delete(sxge->statsp->rdc_ksp[i]);
			}
		}
		SXGE_FREE(sxge->statsp, sizeof (sxge_stats_t));
		sxge->statsp = NULL;
	}
	return (0);
}

static void
sxge_link_update(sxge_t *sxgep, link_state_t state)
{
	mac_link_update(sxgep->mach, state);
	if (state == LINK_STATE_UP) {
		if (sxge_dbg_link && !sxgep->vmac.link_up)
		if (sxge_dbg_en)
			cmn_err(CE_CONT, "%d: %p vmac link up!",
			    sxgep->instance, (void *)sxgep);
		else cmn_err(CE_CONT, "%d: vmac link up!", sxgep->instance);

		sxgep->vmac.link_up = B_TRUE;
		sxgep->link_state = 1;
		sxgep->link_duplex = 2;
		sxgep->link_speed = 0;
		sxgep->link_asmpause = 1;
		sxgep->link_autoneg = 1;
		sxgep->link_pause = 1;
	} else {
		if (sxge_dbg_link && sxgep->vmac.link_up)
		if (sxge_dbg_en)
			cmn_err(CE_CONT, "%d: %p vmac link down!",
			    sxgep->instance, (void *)sxgep);
		else cmn_err(CE_CONT, "%d: vmac link down!", sxgep->instance);

		sxgep->vmac.link_up = B_FALSE;
		sxgep->link_state = 0;
		sxgep->link_speed = 0;
		sxgep->link_duplex = 0;
		sxgep->link_asmpause = 0;
		sxgep->link_autoneg = 0;
		sxgep->link_pause = 0;
	}
}

void
sxge_dbg_msg(sxge_t *sxge, uint32_t code, char *fmt, ...)
{
	char msg_buffer[256];
	char prefix_buffer[32];
	int instance;
	uint32_t dbg_level, dbg_blks, cur_dbg_lvl, cur_dbg_blks;
	int cmn_level = CE_CONT;
	va_list ap;

	if (sxge == NULL) {
		dbg_level = sxge_dbg_code & SXGE_DBG_LVL_MSK;
		dbg_blks = (sxge_dbg_code & SXGE_DBG_BLK_MSK) >>
		    SXGE_DBG_BLK_SH;
	} else {
		dbg_level = sxge_dbg_code & SXGE_DBG_LVL_MSK;
		dbg_blks = (sxge_dbg_code & SXGE_DBG_BLK_MSK) >>
		    SXGE_DBG_BLK_SH;
	}

	cur_dbg_lvl = code & SXGE_DBG_LVL_MSK;
	cur_dbg_blks = (code & SXGE_DBG_BLK_MSK) >> SXGE_DBG_BLK_SH;

	if (sxge_dbg_en ||
	    ((cur_dbg_lvl <= dbg_level) && ((cur_dbg_blks & dbg_blks) != 0))) {
		MUTEX_ENTER(&sxge_dbg_lock);

		if ((cur_dbg_lvl == SXGE_ERR_PRI))
			cmn_level = CE_WARN;
		else
			cmn_level = CE_CONT;

		va_start(ap, fmt);
		(void) vsnprintf(msg_buffer, sizeof (msg_buffer), fmt, ap);
		va_end(ap);

		if (sxge == NULL) {
			instance = -1;
			(void) snprintf(prefix_buffer,
			    sizeof (prefix_buffer), "%s: ", "sxge");
		} else {
			instance = sxge->instance;
			(void) snprintf(prefix_buffer,
			    sizeof (prefix_buffer), "%s%d: ", "sxge", instance);
		}

		MUTEX_EXIT(&sxge_dbg_lock);

		cmn_err(cmn_level, "!%s %s\n", prefix_buffer, msg_buffer);
	}
}

/* Error Injection Utility: function body */
void
sxge_err_inject(sxge_t *sxge, queue_t *q, mblk_t *mp)
{
	ssize_t		size;
	mblk_t		*nmp;
	uint8_t		blk_id;
	uint8_t		chan;
	uint32_t 	err_id;
	err_inject_t 	*eip;

	SXGE_DBG((sxge, STR_CTL, "==> sxge_err_inject"));

	size = 1024;
	nmp = mp->b_cont;
	eip = (err_inject_t *)nmp->b_rptr;
	blk_id = eip->blk_id;
	err_id = eip->err_id;
	chan = eip->chan;
	cmn_err(CE_NOTE, "!blk_id = 0x%x\n", blk_id);
	cmn_err(CE_NOTE, "!err_id = 0x%x\n", err_id);
	cmn_err(CE_NOTE, "!chan = 0x%x\n", chan);
	switch (blk_id) {
	case MAC_BLK_ID:
		break;
	case TXVMAC_BLK_ID:
		break;
	case RXVMAC_BLK_ID:
		break;
	case TDC_BLK_ID:
		sxge_tdc_inject_err(sxge, err_id, chan);
		break;
	case RDC_BLK_ID:
		sxge_rdc_inject_err(sxge, err_id, chan);
		break;
	case PFC_BLK_ID:
		break;
	case SW_BLK_ID:
		break;
	}

	nmp->b_wptr = nmp->b_rptr + size;
	SXGE_DBG((sxge, STR_CTL, "<== sxge_err_inject"));

	miocack(q, mp, (int)size, 0);
}

static void
sxge_loopback_ioctl(sxge_t *sxge, queue_t *wq, mblk_t *mp,
    struct iocblk *iocp)
{
	p_lb_property_t lb_props;
	size_t		size;
	int		i;

	if (mp->b_cont == NULL) {
		miocnak(wq, mp, 0, EINVAL);
	}

	switch (iocp->ioc_cmd) {
	case LB_GET_MODE:
		SXGE_DBG((sxge, (SXGE_CFG_BLK | SXGE_INFO_PRI),
		    "SXGE_GET_LB_MODE command"));

		if (sxge != NULL) {
			*(lb_info_sz_t *)mp->b_cont->b_rptr =
			    sxge->lb_mode;
			miocack(wq, mp, sizeof (sxge_lb_t), 0);
		} else
			miocnak(wq, mp, 0, EINVAL);
		break;

	case LB_SET_MODE:
		SXGE_DBG((sxge, (SXGE_CFG_BLK | SXGE_INFO_PRI),
		    "SXGE_SET_LB_MODE command"));

		if (iocp->ioc_count != sizeof (uint32_t)) {
			miocack(wq, mp, 0, 0);
			break;
		}
		if ((sxge != NULL) && sxge_set_lb(sxge, wq, mp->b_cont)) {
			miocack(wq, mp, 0, 0);
		} else {
			miocnak(wq, mp, 0, EPROTO);
		}
		break;

	case LB_GET_INFO_SIZE:
		SXGE_DBG((sxge, (SXGE_CFG_BLK | SXGE_INFO_PRI),
		    "LB_GET_INFO_SIZE command"));

		if (sxge != NULL) {
			size = sizeof (lb_normal) + sizeof (lb_internal);

			*(lb_info_sz_t *)mp->b_cont->b_rptr = size;

			SXGE_DBG((sxge, (SXGE_CFG_BLK | SXGE_INFO_PRI),
			    "SXGE_GET_LB_INFO command: size %d", size));
			miocack(wq, mp, sizeof (lb_info_sz_t), 0);
		} else
			miocnak(wq, mp, 0, EINVAL);
		break;

	case LB_GET_INFO:
		SXGE_DBG((sxge, (SXGE_CFG_BLK | SXGE_INFO_PRI),
		    "LB_GET_INFO command"));

		if (sxge != NULL) {
			size = sizeof (lb_normal) + sizeof (lb_internal);
			SXGE_DBG((sxge, (SXGE_CFG_BLK | SXGE_INFO_PRI),
			    "SXGE_GET_LB_INFO command: size %d", size));
			if (size == iocp->ioc_count) {
				i = 0;
				lb_props = (p_lb_property_t)mp->b_cont->b_rptr;
				lb_props[i++] = lb_normal;
				lb_props[i++] = lb_internal;

				miocack(wq, mp, size, 0);
			} else
				miocnak(wq, mp, 0, EINVAL);
		} else {
			miocnak(wq, mp, 0, EINVAL);
			cmn_err(CE_NOTE,
			    "sxge_loopback_ioctl: invalid command 0x%x",
			    iocp->ioc_cmd);
		}

		break;
	}
}

/* ARGSUSED */
static boolean_t
sxge_set_lb(sxge_t *sxge, queue_t *wq, mblk_t *mp)
{
	boolean_t	status = B_TRUE;
	uint32_t	lb_mode;
	lb_property_t	*lb_info;

	SXGE_DBG((sxge, (SXGE_CFG_BLK | SXGE_INFO_PRI), "<== sxge_set_lb"));
	lb_mode = sxge->lb_mode;
	if (lb_mode == *(uint32_t *)mp->b_rptr) {
		cmn_err(CE_NOTE,
		    "sxge%d: Loopback mode already set (lb_mode %d).\n",
		    sxge->instance, lb_mode);
		status = B_FALSE;
		goto sxge_set_lb_exit;
	}

	lb_mode = *(uint32_t *)mp->b_rptr;
	lb_info = NULL;

	if (lb_mode == lb_normal.value)
		lb_info = &lb_normal;
	else if (lb_mode == lb_internal.value)
		lb_info = &lb_internal;
	else {
		cmn_err(CE_NOTE,
		    "sxge%d: Loopback mode not supported(mode %d).\n",
		    sxge->instance, lb_mode);
		status = B_FALSE;
		goto sxge_set_lb_exit;
	}

	if (lb_mode == sxge_lb_normal) {
		SXGE_DBG((sxge, (SXGE_CFG_BLK | SXGE_INFO_PRI),
		    "sxge_lb_normal"));
		sxge->lb_mode = sxge_lb_normal;
		(void) sxge_m_promisc(sxge, B_FALSE);

		goto sxge_set_lb_exit;
	}

	sxge->lb_mode = lb_mode;

	SXGE_DBG((sxge, (SXGE_CFG_BLK | SXGE_INFO_PRI), "sxge_lb_internal"));

	if (lb_info->lb_type == internal) {
		(void) sxge_m_promisc(sxge, B_TRUE);
	}

sxge_set_lb_exit:
	SXGE_DBG((sxge, (SXGE_CFG_BLK | SXGE_INFO_PRI),
	    "<== sxge_set_lb status = 0x%08x", status));

	return (status);
}
