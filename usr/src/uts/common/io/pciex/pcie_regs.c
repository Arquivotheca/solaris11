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

#include <sys/sysmacros.h>
#include <sys/kmem.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/sunndi.h>
#include <sys/pci_cap.h>
#include <sys/pcie_impl.h>
#include <sys/pci_cfgacc.h>

/* PCI-Express Device Control Register */
uint16_t pcie_devctl_default = PCIE_DEVCTL_RO_EN |
    PCIE_DEVCTL_MAX_READ_REQ_512 | PCIE_DEVCTL_EXT_TAG_FIELD_EN;
int pcie_max_mps = PCIE_DEVCTL_MAX_PAYLOAD_4096 >> 5;
int pcie_max_mrrs = -1;
int pcie_disable_mps_mf = 0;		/* MPS Multi-function routine */
int pcie_disable_equalization = 1;	/* Disable equalization by default */
int pcie_disable_ido = 0;		/* ID based Ordering */
int pcie_disable_cto = 0;		/* Completion Timeout */
int pcie_disable_acs = 0;		/* Accss Control Service */
int pcie_link_intr_time = 200000;	/* 200ms */
int pcie_link_poll_time = 50000;	/* poll per 50ms */

extern uint32_t pcie_aer_ce_mask;
extern uint32_t	pcie_aer_uce_mask;
extern uint32_t pcie_ecrc_value;
#define	PCIE_LINK_UCE_ERROR_MASK	(PCIE_AER_UCE_DLP | \
    PCIE_AER_UCE_SD | PCIE_AER_UCE_FCP | PCIE_AER_UCE_RO | \
    PCIE_AER_UCE_MTLP | PCIE_AER_UCE_ECRC)

#define	PCIE_LINK_CE_ERROR_MASK	(PCIE_AER_CE_RECEIVER_ERR | \
    PCIE_AER_CE_BAD_TLP | PCIE_AER_CE_BAD_DLLP | \
    PCIE_AER_CE_REPLAY_ROLLOVER | PCIE_AER_CE_REPLAY_TO)

#ifdef	DEBUG
#define	PCIE_DBG_UCAP(dip, bus_p, name, sz, off, org) \
	PCIE_DBG("%s:%d:(0x%x) %s(0x%x) 0x%x -> 0x%x\n", ddi_node_name(dip), \
	    ddi_get_instance(dip), bus_p->bus_bdf, name, off, org, \
	    PCIE_CAP_UGET(sz, bus_p, off))
#else
#define	PCIE_DBG_UCAP 0 &&
#endif

#define	PCIE_CAP_UGET(sz, bus_p, off) \
	(bus_p->bus_cfg_hdl ? PCI_CAP_GET ## sz(bus_p->bus_cfg_hdl, NULL, \
	    bus_p->bus_pcie_off, off) : \
	    pci_cfgacc_get ## sz(bus_p->bus_rc_dip, \
	    bus_p->bus_bdf, bus_p->bus_pcie_off + off))

#define	PCIE_CAP_UPUT(sz, bus_p, off, val) \
	{if (bus_p->bus_cfg_hdl != NULL) \
	    PCI_CAP_PUT ## sz(bus_p->bus_cfg_hdl, NULL, \
	    bus_p->bus_pcie_off, off, val); \
	else \
	    pci_cfgacc_put ## sz(bus_p->bus_rc_dip, \
	    bus_p->bus_bdf, bus_p->bus_pcie_off + off, val); \
	}

#define	PCIE_LINK_GET_WIDTH_INDEX(width) \
	((width) & 0x3 ? (width) & 0x3 : ((width) >> 2) & 0x3 ? \
	    ((width) >> 2 & 0x3) + 2 : (((width) >> 4) & 0x3) + 5)

#define	PCIE_LINK_GET_L0S_EXIT_LATENCY(bus_p, value) \
	{int index = (PCIE_CAP_UGET(32, (bus_p), PCIE_LINKCAP) & \
	    PCIE_LINKCAP_L0S_EXIT_LAT_MASK) >> \
	    PCIE_LINKCAP_L0S_EXIT_LAT_SHIFT; \
	*(value) = (index < PCIE_NUM_L0S_EXIT_LAT && index > 0) ? \
	    pcie_l0s_exit_latency_table[index]: 0; \
	}

#define	PCIE_EP_GET_L0S_EXIT_LATENCY(bus_p, value) \
	{int index = (PCIE_CAP_UGET(32, (bus_p), PCIE_DEVCAP) & \
	    PCIE_DEVCAP_EP_L0S_LAT_MASK) >> \
	    PCIE_DEVCAP_EP_L0S_LAT_SHIFT; \
	    *(value) = (index < PCIE_NUM_L0S_EXIT_LAT && index > 0) ? \
	    pcie_l0s_exit_latency_table[index]: 0; \
	}

#define	PCIE_LINK_SAVE_BW(bus_p) \
	{((bus_p)->bus_linkbw).speed = \
	    (uint8_t)(PCIE_CAP_UGET(16, (bus_p), PCIE_LINKSTS) & \
	    PCIE_LINKSTS_SPEED_MASK); \
	((bus_p)->bus_linkbw).width = \
	    (uint8_t)((PCIE_CAP_UGET(16, (bus_p), PCIE_LINKSTS) & \
	    PCIE_LINKSTS_NEG_WIDTH_MASK) >> 4); \
	}

#define	PCIE_CTO_DISABLE_SUPPORTED(bus_p) \
	((bus_p) && (bus_p->bus_pcie_ver > 1) && \
	    PCIE_CAP_UGET(32, (bus_p), PCIE_DEVCAP2) & \
	    PCIE_DEVCAP2_COM_TO_DISABLE)

#define	PCIE_CTO_SUPPORTED(bus_p) \
	((bus_p) && (bus_p->bus_pcie_ver > 1) && (PCIE_IS_RP(bus_p) || \
	    PCIE_IS_PCIE_LEAF(bus_p) || \
	    PCIE_IS_PCIE_BDG(bus_p)) && \
	    (PCIE_CAP_UGET(32, (bus_p), PCIE_DEVCAP2) & \
	    PCIE_DEVCAP2_COM_TO_RANGE_MASK))

/*
 * Init Link_replay_timer_table
 * The formula is : REPLAY_TIMER * REPLAY_NUM - ACK/NAK_TIMER
 * ACK/NAK_TIMER = REPLAY_TIMER / 3
 * And then transfer the symbol times to ns:
 * Gen1: 1 symbol time = 4ns
 * Gen2: 1 symbol time = 2ns
 * Gen3: 1 symbol time = 1ns
 */

#define	Gen1(a)	((a) * 4 * 8 / 3)
#define	Gen2(a)	((a) * 2 * 8 / 3)
#define	Gen3(a)	((a) * 8 / 3)

/* PCIe 3.0 Spec Table 3-4, 3-5, 3-6 REPLAY_TIMER (symbol time) */
static int pcie_link_replay_timer_table
	[PCIE_NUM_LINK_SPEED][PCIE_NUM_MPS][PCIE_NUM_LINK_WIDTH] = {
	/*
	 * X1	X2	X4	X8	X12	X16	X32
	 * Gen1 2.5GT/S Link Speed
	 */
	{{Gen1(711), Gen1(384), Gen1(219), Gen1(201),
	    Gen1(174), Gen1(144), Gen1(99)},		/* mps: 128 */
	{Gen1(1248), Gen1(651), Gen1(354), Gen1(321),
	    Gen1(270), Gen1(216), Gen1(135)},		/* mps: 256 */
	{Gen1(1677), Gen1(867), Gen1(462), Gen1(258),
	    Gen1(327), Gen1(258), Gen1(156)},		/* mps: 512 */
	{Gen1(3213), Gen1(1635), Gen1(846), Gen1(450),
	    Gen1(582), Gen1(450), Gen1(252)},		/* mps:1024 */
	{Gen1(6285), Gen1(3171), Gen1(1614), Gen1(834),
	    Gen1(1095), Gen1(834), Gen1(6444)},		/* mps:2048 */
	{Gen1(612429), Gen1(66243), Gen1(63150), Gen1(61602),
	    Gen1(62118), Gen1(6160), Gen1(6828)}},	/* mps:4096 */
	/* Gen2 5.0GT/S Link Speed */
	{{Gen2(864), Gen2(8537), Gen2(8372), Gen2(8354),
	    Gen2(8327), Gen2(8297), Gen2(8252)},
	{Gen2(81401), Gen2(8804), Gen2(8507), Gen2(8474),
	    Gen2(8423), Gen2(8369), Gen2(8288)},
	{Gen2(81830), Gen2(81220), Gen2(8615), Gen2(8411),
	    Gen2(8480), Gen2(8411), Gen2(8309)},
	{Gen2(83366), Gen2(81788), Gen2(8999), Gen2(8603),
	    Gen2(8735), Gen2(8603), Gen2(8405)},
	{Gen2(86438), Gen2(83324), Gen2(81767), Gen2(8987),
	    Gen2(81248), Gen2(8987), Gen2(8597)},
	{Gen2(812582), Gen2(86396), Gen2(83303), Gen2(81755),
	    Gen2(82271), Gen2(81755), Gen2(8981)}},
	/* Gen3 8.0GT/S Link Speed */
	{{Gen3(999), Gen3(9672), Gen3(9507), Gen3(9489),
	    Gen3(9462), Gen3(9432), Gen3(9387)},
	{Gen3(91536), Gen3(9939), Gen3(9642), Gen3(9609),
	    Gen3(9558), Gen3(9504), Gen3(9423)},
	{Gen3(91965), Gen3(91155), Gen3(9750), Gen3(9546),
	    Gen3(9615), Gen3(9546), Gen3(9444)},
	{Gen3(93501), Gen3(91923), Gen3(91134), Gen3(9738),
	    Gen3(9870), Gen3(9738), Gen3(9540)},
	{Gen3(96573), Gen3(93459), Gen3(91902), Gen3(91122),
	    Gen3(91383), Gen3(91122), Gen3(9732)},
	{Gen3(912717), Gen3(96531), Gen3(93438), Gen3(91890),
	    Gen3(92406), Gen3(91890), Gen3(91116)}}
};

/* Switch internal latency: full-through + congestion */
static int pcie_switch_internal_latency_table[PCIE_NUM_LINK_SPEED] = {
	1100,   /* Gen1 300ns + 800ns */
	650,    /* Gen2 150ns + 500ns */
	630	/* Gen3 130ns + 500ns */
};

/* PCIe 3.0 Spec Device Capability & Link Capability */
static int pcie_l0s_exit_latency_table[PCIE_NUM_L0S_EXIT_LAT] = {
	64,	/* less than 64ns */
	128,	/* 64ns to less than 128ns */
	256,	/* 128ns to less than 256ns */
	512,	/* 256ns to less than 512ns */
	1000,	/* 512ns to less than 1us */
	2000,	/* 1us to less than 2us */
	4000,	/* 2us to less than 4us */
	-1	/* More than 4us */
};

/* PCIe Spec 3.0 7.8.15 Device Capability 2 Register */
static int pcie_cto_ranges_table[PCIE_NUM_CTO_RANGE][PCIE_NUM_CTO_VALUE] = {
	/* Range A: 50us to 10ms */
	{0,	10},
	/* Range B: 10ms to 250ms */
	{55,	250},
	/* Range C: 250ms to 4s */
	{900,	4000},
	/* Range D: 4s to 64s */
	{13000,	64000}
};

static void pcie_init_mps(dev_info_t *dip);
static void pcie_scan_mps(dev_info_t *rc_dip, dev_info_t *dip,
	pcie_max_supported_t *mps_supported);
static void pcie_get_max_supported(dev_info_t *dip, pcie_max_supported_t *arg);
static int pcie_map_phys(dev_info_t *dip, pci_regspec_t *phys_spec,
    caddr_t *addrp, ddi_acc_handle_t *handlep);
static void pcie_unmap_phys(ddi_acc_handle_t *handlep,	pci_regspec_t *ph);

static void pcie_link_config(dev_info_t *dip);
static int pcie_link_check_state(dev_info_t *dip, boolean_t equalization,
    boolean_t management);
static void pcie_cto_update(dev_info_t *dip);

static void
pcie_init_mps_mf(dev_info_t *cdip, uint16_t *dev_ctrl)
{
	dev_info_t *sdip, *pdip;
	uint16_t mask_mrrs_mps = PCIE_DEVCTL_MAX_READ_REQ_MASK |
	    PCIE_DEVCTL_MAX_PAYLOAD_MASK;
	pcie_bus_t *sdip_bus_p, *cdip_bus_p = PCIE_DIP2BUS(cdip);
	uint8_t fn, mfb;

	/*
	 * Applicable to only: non-ARI Multi-Function Devices (MFDs) that are
	 * PCIe Endpoints or PCIe2PCI bridges.
	 *
	 * fn	mfb	MFD
	 * --	---	---
	 *  0	 0	NO
	 *  0	 1	YES
	 * !0	 x	YES
	 */

	ASSERT(cdip_bus_p);
	fn = (cdip_bus_p->bus_bdf & PCIE_REQ_ID_FUNC_MASK) >>
	    PCIE_REQ_ID_FUNC_SHIFT;
	mfb = (pci_cfgacc_get8(cdip_bus_p->bus_rc_dip,
	    cdip_bus_p->bus_bdf, PCI_CONF_HEADER) & PCI_HEADER_MULTI) >> 7;
	PCIE_DBG("pcie_init_mps_mf: fn = %x, mfb =%x\n", fn, mfb);

	if ((fn == 0 && mfb == 0) ||
	    ((cdip_bus_p->bus_dev_type != PCIE_PCIECAP_DEV_TYPE_PCIE_DEV) &&
	    (cdip_bus_p->bus_dev_type != PCIE_PCIECAP_DEV_TYPE_PCIE2PCI)))
		return;

	/* Walk cdip's siblings and update their MPS/MRRS if necessary */
	pdip = ddi_get_parent(cdip);
	ASSERT(DEVI_BUSY_OWNED(pdip));
	for (sdip = ddi_get_child(pdip); sdip;
	    sdip = ddi_get_next_sibling(sdip)) {
		if ((sdip_bus_p = PCIE_DIP2BUS(sdip)) == NULL)
			continue;

		if ((sdip_bus_p->bus_bdf & (~PCIE_REQ_ID_FUNC_MASK)) !=
		    (cdip_bus_p->bus_bdf & (~PCIE_REQ_ID_FUNC_MASK)))
			continue;

		if (sdip_bus_p->bus_mps >= 0)
			break;
	}

	/* found one already setup sibling function */
	if (sdip != NULL) {
		*dev_ctrl = (*dev_ctrl & ~mask_mrrs_mps) |
		    (PCIE_CAP_UGET(16, sdip_bus_p, PCIE_DEVCTL) &
		    mask_mrrs_mps);
	}
}

/*
 * Initialize the MPS for a root port.
 *
 * dip - dip of root port device.
 */
static void
pcie_init_root_port_mps(dev_info_t *dip)
{
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);
	int		max_supported = pcie_max_mps;
	uint16_t	dev_ctrl;


	dev_ctrl = PCIE_CAP_UGET(16, bus_p, PCIE_DEVCTL);
	dev_ctrl = (dev_ctrl & ~(PCIE_DEVCTL_MAX_READ_REQ_MASK |
	    PCIE_DEVCTL_MAX_PAYLOAD_MASK)) |
	    (pcie_devctl_default &
	    (PCIE_DEVCTL_MAX_READ_REQ_MASK |
	    PCIE_DEVCTL_MAX_PAYLOAD_MASK));

	PCIE_CAP_UPUT(16, bus_p, PCIE_DEVCTL, dev_ctrl);

	(void) pcie_get_fabric_mps(ddi_get_parent(dip),
	    dip, &max_supported);

	bus_p->bus_mps = max_supported;
	pcie_init_mps(dip);
}

/*
 * Initialize the Maximum Payload Size of a device.
 *
 * cdip - dip of device.
 *
 * returns - DDI_SUCCESS or DDI_FAILURE
 */
static void
pcie_init_mps(dev_info_t *cdip)
{
	pcie_bus_t	*bus_p;
	dev_info_t	*pdip = ddi_get_parent(cdip);
	boolean_t	is_ari = B_FALSE;

	bus_p = PCIE_DIP2BUS(cdip);
	if (bus_p == NULL) {
		PCIE_DBG("%s: BUS not found.\n",
		    ddi_driver_name(cdip));
		return;
	}

	/*
	 * For ARI Devices, only function zero's MPS needs to be set.
	 */
	if (PCIE_IS_PCIE(bus_p) &&
	    (pcie_ari_is_enabled(pdip) == PCIE_ARI_FORW_ENABLED)) {
		if ((bus_p->bus_bdf & PCIE_REQ_ID_ARI_FUNC_MASK) != 0)
			return;
		is_ari = B_TRUE;
	}

	if (PCIE_IS_PCIE(bus_p)) {
		int suggested_mrrs, fabric_mps;
		uint16_t device_mps, device_mps_cap, device_mrrs, dev_ctrl;

		dev_ctrl = PCIE_CAP_UGET(16, bus_p, PCIE_DEVCTL);
		if ((fabric_mps = (PCIE_IS_RP(bus_p) ? bus_p :
		    PCIE_DIP2BUS(pdip))->bus_mps) < 0) {
			dev_ctrl = (dev_ctrl & ~(PCIE_DEVCTL_MAX_READ_REQ_MASK |
			    PCIE_DEVCTL_MAX_PAYLOAD_MASK)) |
			    (pcie_devctl_default &
			    (PCIE_DEVCTL_MAX_READ_REQ_MASK |
			    PCIE_DEVCTL_MAX_PAYLOAD_MASK));

			PCIE_CAP_UPUT(16, bus_p, PCIE_DEVCTL, dev_ctrl);
			bus_p->bus_mps = (dev_ctrl &
			    PCIE_DEVCTL_MAX_PAYLOAD_MASK) >>
			    PCIE_DEVCTL_MAX_PAYLOAD_SHIFT;
			return;
		}

		device_mps_cap = PCIE_CAP_UGET(16, bus_p, PCIE_DEVCAP) &
		    PCIE_DEVCAP_MAX_PAYLOAD_MASK;

		device_mrrs = (dev_ctrl & PCIE_DEVCTL_MAX_READ_REQ_MASK) >>
		    PCIE_DEVCTL_MAX_READ_REQ_SHIFT;

		if (device_mps_cap < fabric_mps)
			device_mrrs = device_mps = device_mps_cap;
		else
			device_mps = (uint16_t)fabric_mps;

		suggested_mrrs = (uint32_t)ddi_prop_get_int(DDI_DEV_T_ANY,
		    cdip, DDI_PROP_DONTPASS, "suggested-mrrs", device_mrrs);

		if ((device_mps == fabric_mps) ||
		    (suggested_mrrs < device_mrrs))
			device_mrrs = (uint16_t)suggested_mrrs;
		/*
		 * If pcie_max_mrrs is within the valid MRRS range (e.g.
		 * /etc/system setting) then use it to override the computed
		 * device_mrrs; otherwise, ignore it.
		 */
		if ((pcie_max_mrrs >= (PCIE_DEVCTL_MAX_READ_REQ_128 >>
		    PCIE_DEVCTL_MAX_READ_REQ_SHIFT)) &&
		    (pcie_max_mrrs <= (PCIE_DEVCTL_MAX_READ_REQ_4096 >>
		    PCIE_DEVCTL_MAX_READ_REQ_SHIFT)))
			device_mrrs = (device_mps == fabric_mps) ?
			    (uint16_t)pcie_max_mrrs : MIN(device_mps_cap,
			    (uint16_t)pcie_max_mrrs);
		/*
		 * Replace MPS and MRRS settings.
		 */
		dev_ctrl &= ~(PCIE_DEVCTL_MAX_READ_REQ_MASK |
		    PCIE_DEVCTL_MAX_PAYLOAD_MASK);

		dev_ctrl |= ((device_mrrs << PCIE_DEVCTL_MAX_READ_REQ_SHIFT) |
		    device_mps << PCIE_DEVCTL_MAX_PAYLOAD_SHIFT);

		if (pcie_disable_mps_mf == 0 && is_ari == B_FALSE)
			pcie_init_mps_mf(cdip, &dev_ctrl);

		PCIE_CAP_UPUT(16, bus_p, PCIE_DEVCTL, dev_ctrl);
		bus_p->bus_mps = device_mps;
	}
}

/*
 * Scans a device tree/branch for a maximum payload size capabilities.
 *
 * rc_dip - dip of Root Complex.
 * dip - dip of device where scan will begin.
 * max_supported (IN) - maximum allowable MPS.
 * max_supported (OUT) - maximum payload size capability of fabric.
 */
void
pcie_get_fabric_mps(dev_info_t *rc_dip, dev_info_t *dip, int *max_supported)
{
	pcie_max_supported_t mps_supported;

	if (dip == NULL)
		return;

	mps_supported.dip = rc_dip;
	mps_supported.highest_common_mps = *max_supported;

	/*
	 * Perform a fabric scan to obtain Maximum Payload Capabilities
	 */
	pcie_scan_mps(rc_dip, dip, &mps_supported);
	*max_supported = mps_supported.highest_common_mps;

	PCIE_DBG("MPS: Highest Common MPS= %x\n", *max_supported);
}

/*
 * Scans fabric and determines Maximum Payload Size based on
 * highest common denominator alogorithm
 */
static void
pcie_scan_mps(dev_info_t *rc_dip, dev_info_t *dip,
    pcie_max_supported_t *mps_supported)
{
	dev_info_t *cdip;

	if (dip == NULL)
		return;

	if (dip != rc_dip)
		pcie_get_max_supported(dip, mps_supported);

	for (cdip = ddi_get_child(dip); cdip;
	    cdip = ddi_get_next_sibling(cdip))
		pcie_scan_mps(rc_dip, cdip, mps_supported);
}

/*
 * Called as part of the Maximum Payload Size scan.
 */
static void
pcie_get_max_supported(dev_info_t *dip, pcie_max_supported_t *arg)
{
	uint32_t max_supported;
	uint16_t cap_ptr;
	pcie_max_supported_t *current = arg;
	pci_regspec_t *reg;
	int rlen;
	caddr_t virt;
	ddi_acc_handle_t config_handle;

	if (ddi_get_child(current->dip) == NULL) {
		return;
	}

	if (pcie_dev(dip) == DDI_FAILURE) {
		PCIE_DBG("MPS: pcie_get_max_supported: %s:  "
		    "Not a PCIe dev\n", ddi_driver_name(dip));
		return;
	}

	/*
	 * If the suggested-mrrs property exists, then don't include this
	 * device in the MPS capabilities scan.
	 */
	if (ddi_prop_exists(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "suggested-mrrs") != 0)
		return;

	if (ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS, "reg",
	    (caddr_t)&reg, &rlen) != DDI_PROP_SUCCESS) {
		PCIE_DBG("MPS: pcie_get_max_supported: %s:  "
		    "Can not read reg\n", ddi_driver_name(dip));
		return;
	}

	if (pcie_map_phys(ddi_get_child(current->dip), reg, &virt,
	    &config_handle) != DDI_SUCCESS) {
		PCIE_DBG("MPS: pcie_get_max_supported: %s:  pcie_map_phys "
		    "failed\n", ddi_driver_name(dip));
		goto fail2;
	}

	if ((PCI_CAP_LOCATE(config_handle, PCI_CAP_ID_PCI_E, &cap_ptr)) ==
	    DDI_FAILURE) {
		goto fail3;
	}

	max_supported = PCI_CAP_GET16(config_handle, NULL, cap_ptr,
	    PCIE_DEVCAP) & PCIE_DEVCAP_MAX_PAYLOAD_MASK;

	PCIE_DBG("PCIE MPS: %s: MPS Capabilities %x\n", ddi_driver_name(dip),
	    max_supported);

	if (max_supported < current->highest_common_mps)
		current->highest_common_mps = max_supported;

fail3:
	pcie_unmap_phys(&config_handle, reg);
fail2:
	kmem_free(reg, rlen);
}

/*
 * Function to map in a device's memory space.
 */
static int
pcie_map_phys(dev_info_t *dip, pci_regspec_t *phys_spec,
    caddr_t *addrp, ddi_acc_handle_t *handlep)
{
	ddi_map_req_t mr;
	ddi_acc_hdl_t *hp;
	int result;
	ddi_device_acc_attr_t attr;

	attr.devacc_attr_version = DDI_DEVICE_ATTR_V0;
	attr.devacc_attr_endian_flags = DDI_STRUCTURE_LE_ACC;
	attr.devacc_attr_dataorder = DDI_STRICTORDER_ACC;
	attr.devacc_attr_access = DDI_CAUTIOUS_ACC;

	*handlep = impl_acc_hdl_alloc(KM_SLEEP, NULL);
	hp = impl_acc_hdl_get(*handlep);
	hp->ah_vers = VERS_ACCHDL;
	hp->ah_dip = dip;
	hp->ah_rnumber = 0;
	hp->ah_offset = 0;
	hp->ah_len = 0;
	hp->ah_acc = attr;

	mr.map_op = DDI_MO_MAP_LOCKED;
	mr.map_type = DDI_MT_REGSPEC;
	mr.map_obj.rp = (struct regspec *)phys_spec;
	mr.map_prot = PROT_READ | PROT_WRITE;
	mr.map_flags = DDI_MF_KERNEL_MAPPING;
	mr.map_handlep = hp;
	mr.map_vers = DDI_MAP_VERSION;

	result = ddi_map(dip, &mr, 0, 0, addrp);

	if (result != DDI_SUCCESS) {
		impl_acc_hdl_free(*handlep);
		*handlep = (ddi_acc_handle_t)NULL;
	} else {
		hp->ah_addr = *addrp;
	}

	return (result);
}

/*
 * Map out memory that was mapped in with pcie_map_phys();
 */
static void
pcie_unmap_phys(ddi_acc_handle_t *handlep,  pci_regspec_t *ph)
{
	ddi_map_req_t mr;
	ddi_acc_hdl_t *hp;

	hp = impl_acc_hdl_get(*handlep);
	ASSERT(hp);

	mr.map_op = DDI_MO_UNMAP;
	mr.map_type = DDI_MT_REGSPEC;
	mr.map_obj.rp = (struct regspec *)ph;
	mr.map_prot = PROT_READ | PROT_WRITE;
	mr.map_flags = DDI_MF_KERNEL_MAPPING;
	mr.map_handlep = hp;
	mr.map_vers = DDI_MAP_VERSION;

	(void) ddi_map(hp->ah_dip, &mr, hp->ah_offset,
	    hp->ah_len, &hp->ah_addr);

	impl_acc_hdl_free(*handlep);
	*handlep = (ddi_acc_handle_t)NULL;
}

/*
 * The function is for disable link related errors.
 * Link related errors will be mask, but other errors
 * should be still reported.
 * The function will mask AER UCE, CE, ECRC related registers,
 * will not touch ROOTCTL and AER SUCE register.
 */
static void
pcie_link_disable_errors(dev_info_t *dip, uint32_t *aer_values_p)
{
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);
	uint16_t	reg16;
	uint32_t	reg32, tmp_pcie_aer_ce_mask;

	if (!PCIE_IS_PCIE(bus_p))
		return;

	if (bus_p->bus_cfg_hdl == NULL)
		return;

	if (!PCIE_HAS_AER(bus_p)) {
		/* Disable PCIe Baseline Error Handling */
		reg16 = PCIE_CAP_UGET(16, bus_p, PCIE_DEVCTL);
		reg16 &= ~(PCIE_DEVCTL_CE_REPORTING_EN |
		    PCIE_DEVCTL_NFE_REPORTING_EN | PCIE_DEVCTL_FE_REPORTING_EN);
		PCIE_CAP_UPUT(16, bus_p, PCIE_DEVCTL, reg16);
		PCIE_DBG_UCAP(dip, bus_p, "DEVCTL", 16, PCIE_DEVCTL, reg16);
	} else {
		/* Disable Uncorrectable errors */
		if ((reg32 = PCIE_AER_GET(32, bus_p, PCIE_AER_UCE_MASK)) !=
		    PCI_CAP_EINVAL32) {
			*aer_values_p = reg32;
			PCIE_AER_PUT(32, bus_p, PCIE_AER_UCE_MASK,
			    reg32 | PCIE_LINK_UCE_ERROR_MASK);
			PCIE_DBG_AER(dip, bus_p, "AER UCE MASK", 32,
			    PCIE_AER_UCE_MASK, reg32);
		}
		aer_values_p++;

		tmp_pcie_aer_ce_mask =
		    (uint32_t)ddi_prop_get_int(DDI_DEV_T_ANY, dip,
		    DDI_PROP_DONTPASS, "pcie_ce_mask", pcie_aer_ce_mask);

		/* CE reporting has not been disabled */
		if ((reg32 = PCIE_AER_GET(32, bus_p, PCIE_AER_CE_MASK)) !=
		    PCI_CAP_EINVAL32 && tmp_pcie_aer_ce_mask != (uint32_t)-1) {
			/* Disable Correctable errors */
			*aer_values_p = reg32;
			PCIE_AER_PUT(32, bus_p, PCIE_AER_CE_MASK,
			    reg32 | PCIE_LINK_CE_ERROR_MASK);
			PCIE_DBG_AER(dip, bus_p, "AER CE MASK", 32,
			    PCIE_AER_CE_MASK, reg32);
		}
		aer_values_p++;

		/* Disable ECRC generation and checking */
		if ((reg32 = PCIE_AER_GET(32, bus_p, PCIE_AER_CTL)) !=
		    PCI_CAP_EINVAL32) {
			*aer_values_p = reg32;
			PCIE_AER_PUT(32, bus_p, PCIE_AER_CTL,
			    reg32 & ~pcie_ecrc_value);
			PCIE_DBG_AER(dip, bus_p, "AER CTL", 32,
			    PCIE_AER_CTL, reg32);
		}
		aer_values_p++;
	}
}

static void
pcie_link_enable_errors(dev_info_t *dip, uint32_t *aer_values_p)
{
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);
	uint16_t	reg16, tmp16;
	uint32_t	reg32, tmp32, tmp_pcie_aer_ce_mask;

	if (!PCIE_IS_PCIE(bus_p))
		return;

	if (bus_p->bus_cfg_hdl == NULL)
		return;

	if (!PCIE_HAS_AER(bus_p)) {
		/* Enable Baseline Error Handling */
		if ((reg16 = PCIE_CAP_UGET(16, bus_p, PCIE_DEVCTL)) !=
		    PCI_CAP_EINVAL16) {
			tmp16 = reg16 |
			    (PCIE_DEVCTL_NFE_REPORTING_EN |
			    PCIE_DEVCTL_FE_REPORTING_EN);

			PCIE_CAP_UPUT(16, bus_p, PCIE_DEVCTL, tmp16);
			PCIE_DBG_UCAP(dip, bus_p, "DEVCTL", 16,
			    PCIE_DEVCTL, reg16);
		}
	} else {
		/* Enable Uncorrectable errors */
		if ((reg32 = PCIE_AER_GET(32, bus_p, PCIE_AER_UCE_MASK)) !=
		    PCI_CAP_EINVAL32) {
			tmp32 = *aer_values_p;
			PCIE_AER_PUT(32, bus_p, PCIE_AER_UCE_MASK, tmp32);
			PCIE_DBG_AER(dip, bus_p, "AER UCE MASK", 32,
			    PCIE_AER_UCE_MASK, reg32);
		}
		aer_values_p++;

		tmp_pcie_aer_ce_mask =
		    (uint32_t)ddi_prop_get_int(DDI_DEV_T_ANY, dip,
		    DDI_PROP_DONTPASS, "pcie_ce_mask", pcie_aer_ce_mask);

		/* CE reporting has not been disabled */
		if ((reg32 = PCIE_AER_GET(32, bus_p, PCIE_AER_CE_MASK)) !=
		    PCI_CAP_EINVAL32 && tmp_pcie_aer_ce_mask != (uint32_t)-1) {
			tmp32 = *aer_values_p;
			PCIE_AER_PUT(32, bus_p, PCIE_AER_CE_MASK, tmp32);
			PCIE_DBG_AER(dip, bus_p, "AER CE MASK", 32,
			    PCIE_AER_CE_MASK, reg32);
		}
		aer_values_p++;

		/* Enable ECRC generation and checking */
		if ((reg32 = PCIE_AER_GET(32, bus_p, PCIE_AER_CTL)) !=
		    PCI_CAP_EINVAL32) {
			tmp32 = *aer_values_p;
			PCIE_AER_PUT(32, bus_p, PCIE_AER_CTL, tmp32);
			PCIE_DBG_AER(dip, bus_p, "AER CTL", 32,
			    PCIE_AER_CTL, reg32);
		}
		aer_values_p++;
	}
}

static void
pcie_link_errors_handler(dev_info_t *dip, boolean_t disable,
    uint32_t *aer_value_p)
{
	dev_info_t	*cdip;
	void	(*pcie_link_handler_p)(dev_info_t *, uint32_t *);

	pcie_link_handler_p = disable ? pcie_link_disable_errors :
	    pcie_link_enable_errors;

	pcie_link_handler_p(dip, aer_value_p);

	if ((cdip = ddi_get_child(dip)) == NULL)
		return;

	if ((PCIE_DIP2BUS(cdip))->bus_ari) {
		/* For ARI device, just check function 0 */
		pcie_link_handler_p(pcie_func_to_dip(dip, 0), aer_value_p);
	} else {
		for (; cdip; cdip = ddi_get_next_sibling(cdip))
			pcie_link_handler_p(cdip, aer_value_p);
	}
}

static int
pcie_link_check_errors(dev_info_t *dip)
{
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);
	int		ret = DDI_FAILURE;

	if (dip == NULL || bus_p == NULL)
		return (ret);

	if (bus_p->bus_cfg_hdl == NULL)
		return (DDI_SUCCESS);

	/* Check Surprise Down Error Bit */
	if (PCIE_HAS_AER(bus_p) &&
	    PCIE_AER_GET(32, bus_p, PCIE_AER_UCE_STS) & PCIE_AER_UCE_SD) {
		/* return without clear the error */
		return (ret);
	}

	/* Check if link works via reading VENID */
	if (pci_config_get16(bus_p->bus_cfg_hdl, PCI_CONF_VENID) !=
	    PCIE_VENID(bus_p))
		return (ret);

	/* Clear any pending errors */
	pcie_clear_errors(dip);

	/* errors still report after clearing pending errors */
	if (PCIE_CAP_UGET(16, bus_p, PCIE_DEVSTS) & (PCIE_DEVSTS_CE_DETECTED |
	    PCIE_DEVSTS_NFE_DETECTED | PCIE_DEVSTS_FE_DETECTED |
	    PCIE_DEVSTS_UR_DETECTED | PCIE_DEVSTS_TRANS_PENDING)) {
		/* Treated as Link Down */
		return (ret);
	}

	return (DDI_SUCCESS);
}

static int
pcie_link_check_down(dev_info_t *dip)
{
	dev_info_t	*cdip;
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);
	pcie_req_id_t	bdf;
	int		ret;

	if (pcie_link_check_errors(dip) != DDI_SUCCESS)
		return (DDI_FAILURE);

	/* Child's dip was removed by hotplug/cfgadm command */
	if ((cdip = ddi_get_child(dip)) == NULL) {
		/* Check child with bdf b.0.0 */
		bdf = (PCI_GET_SEC_BUS(dip) & 0xff) << 8;

		/*
		 * Add a workaround:
		 * Disable ACS Source Validation before configuration
		 * read to prevent ACS Violation Error
		 */
		pcie_acs_control(dip, PCIE_ACS_DISABLE,
		    PCIE_ACS_CTL_SOURCE_VAL_EN);

		ret = (pci_cfgacc_get32(bus_p->bus_rc_dip, bdf,
		    PCI_CONF_VENID) != (uint32_t)-1) ?
		    DDI_SUCCESS : DDI_FAILURE;

		pcie_acs_control(dip, PCIE_ACS_ENABLE,
		    PCIE_ACS_CTL_SOURCE_VAL_EN);

		return (ret);
	}

	if ((PCIE_DIP2BUS(cdip))->bus_ari) {
		/* For ARI device, just check function 0 */
		ret = pcie_link_check_errors(pcie_func_to_dip(dip, 0));
	} else {
		for (; cdip; cdip = ddi_get_next_sibling(cdip))
			if ((ret = pcie_link_check_errors(cdip)) ==
			    DDI_FAILURE)
				break;
	}

	return (ret);
}

static void
pcie_link_update(dev_info_t *dip)
{
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);
	dev_info_t	*cdip;

	/* Save current link speed & width */
	PCIE_LINK_SAVE_BW(bus_p);

	for (cdip = ddi_get_child(dip); cdip;
	    cdip = ddi_get_next_sibling(cdip))
		PCIE_LINK_SAVE_BW(PCIE_DIP2BUS(cdip));

	/* Update completion timeout value */
	if (!pcie_disable_cto)
		pcie_cto_update(dip);
}

int
pcie_lbn_intr(dev_info_t *dip)
{
	pcie_bus_t	*bus_p;
	pcie_link_t	*link_p;
	uint16_t	reg16;
	boolean_t	management = B_FALSE;
	boolean_t	equalization = B_FALSE;

	if ((bus_p = PCIE_DIP2BUS(dip)) == NULL)
		return (DDI_INTR_UNCLAIMED);

	reg16 = PCIE_CAP_UGET(16, bus_p, PCIE_LINKSTS);

	if (reg16 & PCIE_LINKSTS_BAND_MGT_STS)
		management = B_TRUE;
	if (bus_p->bus_pcie_ver > 1 &&
	    PCIE_CAP_UGET(16, bus_p, PCIE_LINKSTS2) & PCIE_LINKSTS2_EQU_REQ)
		equalization = B_TRUE;
	if (!(management || equalization || reg16 & PCIE_LINKSTS_AUTO_BAND_STS))
		return (DDI_INTR_UNCLAIMED);

	link_p = PCIE_DIP2LINK(dip);
	mutex_enter(&link_p->link_lock);

	/* manually Link Retrain or Link Equalization */
	if (link_p->link_flags == PCIE_LINK_OPERATION_PENDING)
		cv_signal(&link_p->link_cv);
	else
		/*
		 * Autonomous Link Bandwidth Change or link up/down
		 * during hotplug
		 */
		(void) pcie_link_check_state(dip, equalization, management);

	mutex_exit(&link_p->link_lock);

	return (DDI_INTR_CLAIMED);
}

static int
pcie_link_check_state(dev_info_t *dip, boolean_t equalization,
    boolean_t management)
{
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);
	pcie_link_t	*link_p = PCIE_DIP2LINK(dip);
	pcie_linkbw_t	bw;
	uint16_t	reg16;
	int		speed, target_speed, width, ret;

	/* Check link errors */
	if (pcie_link_check_down(dip) != DDI_SUCCESS) {
		link_p->link_flags = PCIE_LINK_UP;
		return (PCIE_ERR_LINK_DOWN);
	}

	speed = PCIE_CAP_UGET(16, bus_p, PCIE_LINKSTS) &
	    PCIE_LINKSTS_SPEED_MASK;
	width = PCIE_CAP_UGET(16, bus_p, PCIE_LINKSTS) &
	    PCIE_LINKSTS_NEG_WIDTH_MASK;
	target_speed = PCIE_CAP_UGET(16, bus_p, PCIE_LINKCTL2) &
	    PCIE_LINKCTL2_TARGET_LINK_SPEED_MASK;

	bw = PCIE_DIP2BW(dip);

	/* Link Equalization performed with Link Retrain */
	if (management && equalization) {
		uint16_t equ = (PCIE_LINKSTS2_EQU_PHASE1_SUC |
		    PCIE_LINKSTS2_EQU_PHASE2_SUC |
		    PCIE_LINKSTS2_EQU_PHASE3_SUC);

		reg16 = PCIE_CAP_UGET(16, bus_p, PCIE_LINKSTS2);

		link_p->link_flags = (reg16 & PCIE_LINKSTS2_EQU_COMPL &&
		    (reg16 & equ) == equ && speed == PCIE_LINKSTS_SPEED_8) ?
		    PCIE_LINK_UP : PCIE_LINK_OPERATION_FAIL;

		/* Clear status bit */
		if ((reg16 = PCIE_CAP_UGET(16, bus_p, PCIE_LINKSTS2)) &
		    PCIE_LINKSTS2_EQU_REQ) {
			PCIE_CAP_UPUT(16, bus_p, PCIE_LINKSTS2, reg16);
		}
		if ((reg16 = PCIE_CAP_UGET(16, bus_p, PCIE_LINKSTS)) &
		    PCIE_LINKSTS_BAND_MGT_STS) {
			PCIE_CAP_UPUT(16, bus_p, PCIE_LINKSTS, reg16);
		}
	/*
	 * Link Retrain performed manually or
	 * due to unreliable link operation.
	 */

	} else if (management) {
		link_p->link_flags = (speed == target_speed && (speed !=
		    bw.speed || width != bw.width)) ?
		    PCIE_LINK_UP : PCIE_LINK_OPERATION_FAIL;

		/* Clear status bits */
		if ((reg16 = PCIE_CAP_UGET(16, bus_p, PCIE_LINKSTS)) &
		    PCIE_LINKSTS_BAND_MGT_STS) {
			PCIE_CAP_UPUT(16, bus_p, PCIE_LINKSTS, reg16);
		}

	/* Autonomous link retrain due to other reasons */
	} else {
		link_p->link_flags = ((speed != bw.speed ||
		    width != bw.width) ?
		    PCIE_LINK_UP : PCIE_LINK_OPERATION_FAIL);

		/* Clear status bits */
		if ((reg16 = PCIE_CAP_UGET(16, bus_p, PCIE_LINKSTS)) &
		    PCIE_LINKSTS_AUTO_BAND_STS) {
			PCIE_CAP_UPUT(16, bus_p, PCIE_LINKSTS, reg16);
		}
	}

	/* Save current link speed & width, update cto if needed */
	pcie_link_update(dip);

	/* Link equalization success or the link works fine under Gen3 */
	if ((equalization && link_p->link_flags == PCIE_LINK_UP) ||
	    bw.speed == PCIE_LINKSTS_SPEED_8)
		(bus_p->bus_linkbw).gen3_ready = B_TRUE;

	ret = (link_p->link_flags == PCIE_LINK_OPERATION_FAIL ?
	    PCIE_ERR_LINK_OPERATION_FAIL : 0);

	if (link_p->link_flags == PCIE_LINK_OPERATION_FAIL) {
		PCIE_DBG("%s%d: Link Bandwidth Change interrupt received"
		    " without actual link bandwidth change.\n",
		    ddi_driver_name(dip), ddi_get_instance(dip));
		link_p->link_flags = PCIE_LINK_UP;
	}

	return (ret);
}

static void
pcie_link_enable_auto_bw_change(dev_info_t *dip)
{
	dev_info_t	*cdip = dip;
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);
	uint16_t	tmp16;

	for (int i = 0; cdip && i < 2; i++) {
		bus_p = PCIE_DIP2BUS(cdip);
		/* clear Hardware Autonomous Link Width Disable bit */
		tmp16 = PCIE_CAP_UGET(16, bus_p, PCIE_LINKCTL) &
		    ~PCIE_LINKCTL_HW_AUT_WIDTH_DIS;
		PCIE_CAP_UPUT(16, bus_p, PCIE_LINKCTL, tmp16);

		if (bus_p->bus_pcie_ver < 2)
			continue;

		/* clear Hardware Autonomous Link Speed Disable bit */
		tmp16 = PCIE_CAP_UGET(16, bus_p, PCIE_LINKCTL2) &
		    ~PCIE_LINKCTL2_HW_AUT_SPEED_DIS;
		PCIE_CAP_UPUT(16, bus_p, PCIE_LINKCTL2, tmp16);

		/* Get function 0 */
		cdip = pcie_func_to_dip(dip, 0);
	}
}

static void
pcie_link_disable_auto_bw_change(dev_info_t *dip)
{
	dev_info_t	*cdip = dip;
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);
	uint16_t	tmp16;

	for (int i = 0; cdip && i < 2; i++) {
		bus_p = PCIE_DIP2BUS(cdip);
		/* Set Hardware Autonomous Link Width Disable bit */
		tmp16 = PCIE_CAP_UGET(16, bus_p, PCIE_LINKCTL) |
		    PCIE_LINKCTL_HW_AUT_WIDTH_DIS;
		PCIE_CAP_UPUT(16, bus_p, PCIE_LINKCTL, tmp16);

		if (bus_p->bus_pcie_ver < 2)
			continue;

		/* Set Hardware Autonomous Link Speed Disable bit */
		tmp16 = PCIE_CAP_UGET(16, bus_p, PCIE_LINKCTL2) |
		    PCIE_LINKCTL2_HW_AUT_SPEED_DIS;
		PCIE_CAP_UPUT(16, bus_p, PCIE_LINKCTL2, tmp16);

		/* Get function 0 */
		cdip = pcie_func_to_dip(dip, 0);
	}
}

int
pcie_link_retrain(dev_info_t *cdip, uint16_t speed, boolean_t equalization)
{
	dev_info_t	*dip, *cdip_func0;
	pcie_bus_t	*bus_p, *child_bus_p;
	pcie_link_t	*link_p;
	pcie_linkbw_t	bw;
	uint16_t	reg16;
	int		ret;
	boolean_t	management = B_TRUE;
	uint32_t	*aer_values_p;
	int		circular_count;

	if (!cdip || !PCIE_IS_PCIE(PCIE_DIP2BUS(cdip)))
		return (PCIE_ERR_OPERATION_NOTSUP);

	child_bus_p = PCIE_DIP2BUS(cdip);
	speed &= 0xf;
	bw = PCIE_DIP2BW(cdip);

	if (bw.sup_speeds == PCIE_LINKCAP2_LINK_SPEED_2_5 ||
	    bw.width == 0 ||
	    (equalization && !(bw.sup_speeds & PCIE_LINKCAP2_LINK_SPEED_8)))
		return (PCIE_ERR_OPERATION_NOTSUP);

	/* check parameters */
	if ((!(bw.sup_speeds & (1 << speed))) ||
	    (bw.speed == speed && !equalization))
		return (PCIE_ERR_INVALID_ARG);

	/* Get Downstream Port device */
	dip = (PCIE_IS_RP(child_bus_p) || PCIE_IS_SWD(child_bus_p)) ? cdip :
	    ddi_get_parent(cdip);
	bus_p = PCIE_DIP2BUS(dip);

	/* No RP exists  */
	if (PCIE_IS_RC(bus_p))
		return (PCIE_ERR_OPERATION_NOTSUP);

	/* Device has not ready for retrain to Gen3, start Link Equalization */
	if (equalization || (speed == PCIE_LINKSTS_SPEED_8 &&
	    !bw.gen3_ready)) {
		/* Check if manually link equalization support */
		if (bus_p->bus_ext2_off == 0 || pcie_disable_equalization) {
			return (PCIE_ERR_OPERATION_NOTSUP);
		}
		/* Link Equalization is companied by Link Retrain */
		equalization = B_TRUE;
		management = B_TRUE;
		speed = PCIE_LINKSTS_SPEED_8;
	}

	/* Prevent the device tree change when waiting for the interrupt */
	ndi_devi_enter(ddi_get_parent(dip), &circular_count);

	link_p = PCIE_DIP2LINK(dip);
	mutex_enter(&link_p->link_lock);

	/* Check if the link is in retrain or equalization */
	if (link_p->link_flags != PCIE_LINK_UP) {
		mutex_exit(&link_p->link_lock);
		ndi_devi_exit(ddi_get_parent(dip), circular_count);
		return (PCIE_ERR_DEV_BUSY);
	}
	link_p->link_flags = PCIE_LINK_OPERATION_PENDING;

	/* Disable Link Autonomous Bandwidth Change */
	pcie_link_disable_auto_bw_change(dip);

	/*
	 * Allocated memory for dip and its child
	 * The child device has 8 functions at max
	 * so we need allocate (8+1)*3=27 uint32_t
	 * for saving DEVCTL/AER related registers.
	 */
	aer_values_p = (uint32_t *)kmem_zalloc(
	    sizeof (uint32_t) * (8 + 1) * 3, KM_SLEEP);
	/* Disable Link related errors report */
	pcie_link_errors_handler(dip, B_TRUE, aer_values_p);

	/* Perform Link Equalization */
	if (equalization) {
		uint32_t	reg32;
		reg32 = pci_cfgacc_get32(bus_p->bus_rc_dip, bus_p->bus_bdf,
		    bus_p->bus_ext2_off + PCIE_EXT2_LINKCTL3) |
		    PCIE_EXT2_LINKCTL3_PERF_EQU;
		pci_cfgacc_put32(bus_p->bus_rc_dip, bus_p->bus_bdf,
		    bus_p->bus_ext2_off + PCIE_EXT2_LINKCTL3, reg32);
	}

	/* Set target speed */
	reg16 = PCIE_CAP_UGET(16, bus_p, PCIE_LINKCTL2) &
	    ~PCIE_LINKCTL2_TARGET_LINK_SPEED_MASK | speed;
	PCIE_CAP_UPUT(16, bus_p, PCIE_LINKCTL2, reg16);

	/* Set function 0's target speed */
	if ((cdip_func0 = pcie_func_to_dip(dip, 0)) != NULL) {
		reg16 = PCIE_CAP_UGET(16, PCIE_DIP2BUS(cdip_func0),
		    PCIE_LINKCTL2) & ~PCIE_LINKCTL2_TARGET_LINK_SPEED_MASK |
		    speed;
		PCIE_CAP_UPUT(16, PCIE_DIP2BUS(cdip_func0),
		    PCIE_LINKCTL2, reg16);
	}

	/* Start link retrain */
	reg16 = PCIE_CAP_UGET(16, bus_p, PCIE_LINKCTL) |
	    PCIE_LINKCTL_RETRAIN_LINK;
	PCIE_CAP_UPUT(16, bus_p, PCIE_LINKCTL, reg16);

	if (bw.intr_sup) {
		(void) cv_timedwait(&link_p->link_cv, &link_p->link_lock,
		    ddi_get_lbolt() + drv_usectohz(pcie_link_intr_time));
	} else {
		/* polling mode */
		for (int i = 0; i < 5; i++) {
			(void) cv_timedwait(&link_p->link_cv,
			    &link_p->link_lock, ddi_get_lbolt() +
			    drv_usectohz(pcie_link_poll_time));

			if (!equalization && (PCIE_CAP_UGET(16, bus_p,
			    PCIE_LINKSTS) & PCIE_LINKSTS_SPEED_MASK) == speed)
				break;
			if (equalization && (PCIE_CAP_UGET(16, bus_p,
			    PCIE_LINKSTS2) & PCIE_LINKSTS2_EQU_COMPL))
				break;
		}
	}

	ret = pcie_link_check_state(dip, equalization, management);
	mutex_exit(&link_p->link_lock);

	pcie_link_enable_auto_bw_change(dip);
	pcie_link_errors_handler(dip, B_FALSE, aer_values_p);

	ndi_devi_exit(ddi_get_parent(dip), circular_count);

	kmem_free(aer_values_p, sizeof (uint32_t) * (8 + 1) * 3);

	/*
	 * According to PCIe Spec 3.0 6.11, a 200ms interval is needed
	 * for a failed attempt
	 */
	if (ret == PCIE_LINK_OPERATION_FAIL)
		delay(200000);

	return (ret);
}

static int
get_supported_link_speeds(pcie_bus_t *bus_p)
{
	uint32_t	cap;
	int		speed_vec = -1;

	if (!bus_p || PCIE_IS_RC(bus_p))
		return (-1);

	if (bus_p->bus_pcie_ver > 1) {
		cap = PCIE_CAP_UGET(32, bus_p, PCIE_LINKCAP2);
		speed_vec = cap & PCIE_LINKCAP2_LINK_SPEED_VECTOR_MASK;
	}

	if (bus_p->bus_pcie_ver < 2 || !speed_vec) {
		speed_vec = ((PCIE_CAP_UGET(32, bus_p, PCIE_LINKCAP) &
		    PCIE_LINKCAP_MAX_SPEED_MASK) << 1) | 0x2;
	}

	return (speed_vec);
}

/*
 * Get supported Link Speed and Width.
 */
static int
pcie_link_get_supported_speeds(dev_info_t *dip)
{
	dev_info_t	*pdip;
	pcie_bus_t	*bus_p, *tmp_bus_p;
	int		speed_vec1 = -1, speed_vec2 = -1;

	bus_p = PCIE_DIP2BUS(dip);
	speed_vec1 = get_supported_link_speeds(bus_p);

	if ((pdip = ddi_get_parent(dip)) != NULL) {
		tmp_bus_p = PCIE_DIP2BUS(pdip);
		speed_vec2 = get_supported_link_speeds(tmp_bus_p);
	}

	return (speed_vec1 & speed_vec2);
}

/*
 * Calculate cto range according to timeout value.
 *
 * Input value:
 * timeout = 0 : get max supported range;
 *         > 0 : get range according to timeout value(ms);
 *
 * Return value:
 *  -1 : timeout value is not within supported ranges.
 *  > 0: success, return cto range.
 */
static int
pcie_cto_get_range(dev_info_t *dip, int timeout)
{
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);
	uint32_t	range;
	int		*ptr, index, size;

	range = PCIE_CAP_UGET(32, bus_p, PCIE_DEVCAP2) &
	    PCIE_DEVCAP2_COM_TO_RANGE_MASK;

	/*
	 * From PCIe 3.0 Spec, CTO is recommended not to expire in
	 * less than 10ms, Solaris doesn't support cto value more than 4s,
	 * so ignore Range A(50us - 10ms) and Range D (4s - 64s).
	 */
	range &= PCIE_DEVCAP2_COM_TO_RANGE_B_C;

	/* Get the supported max range */
	if (timeout == 0) {
		for (index = 0; range; range >>= 1, index++)
			;
		return (((--index) << 2) + PCIE_NUM_CTO_VALUE);
	}

	ptr = (int *)pcie_cto_ranges_table;
	size = PCIE_NUM_CTO_RANGE * PCIE_NUM_CTO_VALUE - 1;
	for (index = 1; index < size; index++)
		if (timeout >= *ptr && timeout < *++ptr) {
			/* If range doesn't support, continue searching */
			for (; !(range & (1 << index / PCIE_NUM_CTO_VALUE)) &&
			    index < size; index++)
				;
			break;
		}

	return (index == size ? -1 : (index / PCIE_NUM_CTO_VALUE << 2 |
	    index % PCIE_NUM_CTO_VALUE) + 1);
}

/*
 * Calculate cto value based on Memory Read Request
 *
 * rp_originated = B_TRUE: calculate RP's cto
 *               = B_FALSE: calculate endpoint's cto
 *
 * return value is timeout (ms)
 *               = 0: timeout has no limit
 *               > 0: get a valid value
 */
static int
pcie_cto_calculate(dev_info_t *cdip, boolean_t rp_originated)
{
	pcie_bus_t	*bus_p, *cbus_p;
	dev_info_t	*dip;
	uint64_t	timeout = 0;
	uint16_t	mps, mrrs;
	int		size, factor, latency;

	/* Device L0s exit latency has no limit */
	PCIE_EP_GET_L0S_EXIT_LATENCY(PCIE_DIP2BUS(cdip), &latency);
	if (latency < 0)
		return (0);

	/*
	 * get device's mps & mrrs.
	 * For Root Port:
	 *	using rp's mps & mrrs;
	 * For Endpoint:
	 *	For ARI device: using function 0's mps & itself mrrs.
	 *	For Non-ARI device: using mps & mrrs itself.
	 */
	if (rp_originated) {
		dip = (PCIE_DIP2BUS(cdip))->bus_rp_dip;
		bus_p = cbus_p = PCIE_DIP2BUS(dip);
	} else {
		bus_p = cbus_p = PCIE_DIP2BUS(cdip);
		if ((pcie_ari_is_enabled(ddi_get_parent(cdip))
		    == PCIE_ARI_FORW_ENABLED) && (pcie_ari_device(cdip)
		    == PCIE_ARI_DEVICE)) {
			if ((dip = pcie_func_to_dip(ddi_get_parent(cdip),
			    0)) != NULL)
				bus_p = PCIE_DIP2BUS(dip);
		}
	}


	mps = (PCIE_CAP_UGET(16, bus_p, PCIE_DEVCTL) &
	    PCIE_DEVCTL_MAX_PAYLOAD_MASK) >> PCIE_DEVCTL_MAX_PAYLOAD_SHIFT;

	mrrs = (PCIE_CAP_UGET(16, cbus_p, PCIE_DEVCTL) &
	    PCIE_DEVCTL_MAX_READ_REQ_MASK) >> PCIE_DEVCTL_MAX_READ_REQ_SHIFT;

	cbus_p = PCIE_DIP2BUS(cdip);

	/* use the min value as packet payload size */
	size = MIN(mps, mrrs);

	timeout += PCIE_RC_INTERNAL_LATENCY + latency;

	for (dip = cdip, bus_p = PCIE_DIP2BUS(cdip);
	    bus_p && !PCIE_IS_RC(bus_p);
	    dip = ddi_get_parent(dip), bus_p = PCIE_DIP2BUS(dip)) {
		uint16_t speed, width;

		speed = PCIE_CAP_UGET(16, bus_p, PCIE_LINKSTS) &
		    PCIE_LINKSTS_SPEED_MASK;
		width = ((PCIE_CAP_UGET(16, bus_p, PCIE_LINKSTS) &
		    PCIE_LINKSTS_NEG_WIDTH_MASK) >> 4);

		/* Device works abnormal */
		if (speed == 0 || width == 0)
			return (0);
		if (PCIE_IS_SWD(bus_p) || PCIE_IS_RP(bus_p)) {
			/*
			 * Get link speed & width's index in
			 * link_replay_timer_table
			 */
			width = PCIE_LINK_GET_WIDTH_INDEX(width);
			timeout += pcie_link_replay_timer_table
			    [--speed][size][--width];

			PCIE_LINK_GET_L0S_EXIT_LATENCY(bus_p, &latency);
			/* link L0s exit latency has no limit */
			if (latency < 0)
				return (0);
			timeout += latency;
		}
		if (PCIE_IS_SWU(bus_p)) {
			timeout += pcie_switch_internal_latency_table[--speed];
		}
	}

	/* From ns to ms */
	timeout = timeout / 1000000 + 1;

	/* For multi-completion request */
	factor = (mrrs > mps) ? (mrrs - mps) : 0;
	factor = (1 << factor) + 1;
	timeout *= factor;

	return ((int)timeout);
}

/*
 * Set device's completion timeout range.
 *
 * Range == 0: first calculate timeout value, then set range;
 * Range >  0: set range directly without calculation;
 * Range == -1: timeout value is not within supported ranges,
 *              disable cto mechanism if supported,
 *              else set with max supported range;
 */
static void
pcie_cto_set(dev_info_t *dip, int range)
{
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);
	uint16_t	reg16;

	if ((range == 0) && !PCIE_IS_RP(bus_p)) {
		range = pcie_cto_get_range(dip,
		    pcie_cto_calculate(dip, B_FALSE));
	}

	/* timeout is not within supported ranges */
	if (range == -1) {
		if (PCIE_CTO_DISABLE_SUPPORTED(bus_p)) {
			/* Disable cto */
			reg16 = PCIE_CAP_UGET(16, bus_p, PCIE_DEVCTL2) |
			    PCIE_DEVCTL2_COM_TO_DISABLE;
			PCIE_CAP_UPUT(16, bus_p, PCIE_DEVCTL2, reg16);
			return;
		} else {
			/* Get the max supported range */
			range = pcie_cto_get_range(dip, 0);
		}
	}

	/* Clear cto disable bit */
	reg16 = PCIE_CAP_UGET(16, bus_p, PCIE_DEVCTL2) &
	    ~PCIE_DEVCTL2_COM_TO_DISABLE;
	PCIE_CAP_UPUT(16, bus_p, PCIE_DEVCTL2, reg16);

	/* Set cto range */
	if (((reg16 = PCIE_CAP_UGET(16, bus_p, PCIE_DEVCTL2))
	    & PCIE_DEVCTL2_COM_TO_RANGE_MASK) != range) {
		reg16 = reg16 & ~PCIE_DEVCTL2_COM_TO_RANGE_MASK | range;
		PCIE_CAP_UPUT(16, bus_p, PCIE_DEVCTL2, reg16);
	}
}

/* For RP, use the path from RP to the endpoint with max cto value. */
static void
pcie_cto_rp_update(dev_info_t *dip)
{
	pcie_bus_t	*rp_bus_p;
	dev_info_t	*rp_dip;
	int		range;
	uint16_t	timeout;

	rp_dip = (PCIE_DIP2BUS(dip))->bus_rp_dip;
	rp_bus_p = PCIE_DIP2BUS(rp_dip);

	if (!PCIE_CTO_SUPPORTED(rp_bus_p))
		return;

	/*
	 * Only update RP's cto when new value > default(50ms)
	 * and new range > old range.
	 */
	timeout = pcie_cto_calculate(dip, B_TRUE);
	if (!timeout || timeout > 50) {
		range = pcie_cto_get_range(rp_dip, timeout);
		if (range == -1 || (range >
		    (PCIE_CAP_UGET(16, rp_bus_p, PCIE_DEVCTL2)
		    & PCIE_DEVCTL2_COM_TO_RANGE_MASK)))
			pcie_cto_set(rp_dip, range);
	}
}

static void
pcie_cto_update(dev_info_t *dip)
{
	dev_info_t *cdip;

	cdip = dip;
	if (!cdip)
		return;

	if (PCIE_CTO_SUPPORTED(PCIE_DIP2BUS(cdip))) {
		/* Re-calculate cto value */
		pcie_cto_set(cdip, 0);
		/* Update rp's cto if needed */
		pcie_cto_rp_update(cdip);
	}

	for (cdip = ddi_get_child(cdip); cdip;
	    cdip = ddi_get_next_sibling(cdip))
		pcie_cto_update(cdip);
}

int
pcie_set_completion_timeout(dev_info_t *dip, uint16_t timeout)
{
	pcie_bus_t	 *bus_p = PCIE_DIP2BUS(dip);
	int		range;

	/* check parameters */
	if (!dip || !PCIE_IS_PCIE(bus_p) ||
	    !PCIE_CTO_SUPPORTED(bus_p))
		return (PCIE_ERR_OPERATION_NOTSUP);

	/* timeout values doesn't support */
	if (timeout < 10 || timeout > 4000 ||
	    (range = pcie_cto_get_range(dip, timeout)) == -1)
		return (PCIE_ERR_INVALID_ARG);

	pcie_cto_set(dip, range);

	return (0);
}

/* Enable/Disable ACS rules on a device */
void
pcie_acs_control(dev_info_t *cdip, boolean_t flag, uint16_t acs_ctrl)
{
	pcie_bus_t	*bus_p;
	dev_info_t	*rcdip, *dip;
	uint16_t	ctrl16;

	if (pcie_disable_acs || !cdip || !(bus_p = PCIE_DIP2BUS(cdip)))
		return;

	if (!PCIE_IS_PCIE(bus_p))
		return;

	rcdip = bus_p->bus_rc_dip;
	for (dip = cdip; dip && dip != rcdip; dip = ddi_get_parent(dip)) {
		bus_p = PCIE_DIP2BUS(dip);

		ASSERT(bus_p != NULL);

		if (bus_p->bus_acs_off == 0)
			continue;
		/* Device support ACS */
		ctrl16 = pci_cfgacc_get16(bus_p->bus_rc_dip, bus_p->bus_bdf,
		    bus_p->bus_acs_off + PCIE_ACS_CTL);

		ctrl16 = (flag == PCIE_ACS_ENABLE) ?
		    ctrl16 | (acs_ctrl & PCIE_ACS_CTL_MASK) :
		    ctrl16 & ~(acs_ctrl & PCIE_ACS_CTL_MASK);

		pci_cfgacc_put16(bus_p->bus_rc_dip, bus_p->bus_bdf,
		    bus_p->bus_acs_off + PCIE_ACS_CTL, ctrl16);
	}
}

/* Enable Access Control Services */
static void
pcie_acs_enable(dev_info_t *dip)
{
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);
	dev_info_t	*rcdip;
	uint16_t	bdf, base, cap16, ctrl16, size;
	uint32_t	vec32;
	int		i, j;

	if (!bus_p->bus_acs_off)
		return;

	rcdip = PCIE_GET_RC_DIP(bus_p);
	base = bus_p->bus_acs_off;
	bdf = bus_p->bus_bdf;

	cap16 = pci_cfgacc_get16(rcdip, bdf, base + PCIE_ACS_CAP);

	ctrl16 = PCIE_ACS_CTL_SOURCE_VAL_EN | PCIE_ACS_CTL_UP_FORW_EN;

	if (cap16 & PCIE_ACS_CAP_P2P_EGRESS_CTL) {
		ctrl16 |= PCIE_ACS_CTL_P2P_EGRESS_CTL_EN;
		size = ((cap16 & PCIE_ACS_CAP_EGRESS_VEC_SIZE) >>
		    PCIE_ACS_CAP_EGRESS_VEC_SHIFT);
		size = (size == 0) ? 256 : size;
		/* Set Egress Control Vector */
		for (i = 0; i < size / 32; i++) {
			pci_cfgacc_put32(rcdip, bdf,
			    base + PCIE_ACS_EGRESS_CTL_VEC + i, -1);
		}
		for (j = 0, vec32 = 0; j < size % 32; j++)
			vec32 |= 1 << j;
		pci_cfgacc_put32(rcdip, bdf,
		    base + PCIE_ACS_EGRESS_CTL_VEC + i, vec32);
	} else {
		/* Enable P2P request/completion Redirect */
		ctrl16 |= (PCIE_ACS_CTL_P2P_REQ_REDIRECT_EN |
		    PCIE_ACS_CTL_P2P_COM_REDIRECT_EN);
	}
	pci_cfgacc_put16(rcdip, bdf, base + PCIE_ACS_CTL, ctrl16);
}

static void
pcie_link_config(dev_info_t *dip)
{
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);
	pcie_linkbw_t	*bw_p;
	uint32_t	reg32;
	uint16_t	reg16;

	if (dip == NULL || bus_p == NULL || !PCIE_IS_PCIE(bus_p))
		return;

	reg16 = PCIE_CAP_UGET(16, bus_p, PCIE_LINKSTS);
	reg32 = PCIE_CAP_UGET(32, bus_p, PCIE_LINKCAP);

	bw_p = &bus_p->bus_linkbw;
	bw_p->speed = (uint8_t)(reg16 & PCIE_LINKSTS_SPEED_MASK);
	bw_p->width = (uint8_t)((reg16 & PCIE_LINKSTS_NEG_WIDTH_MASK) >> 4);
	/*
	 * It is marked TRUE when interrupt is initialized successfully
	 * during pcieb driver attach.
	 */
	bw_p->intr_sup = B_FALSE;
	bw_p->gen3_ready = bw_p->speed == 3 ? B_TRUE : B_FALSE;

	/* Clear Link Bandwidth related status bits */
	PCIE_CAP_UPUT(16, bus_p, PCIE_LINKSTS, reg16);
	PCIE_DBG_UCAP(dip, bus_p, "LINKSTS", 16, PCIE_LINKSTS, reg16);

	/*
	 * Enable Link Bandwidth Notification Interrupt
	 * Clear Hardware Autonomous Link Width Disable bit
	 */
	reg16 = PCIE_CAP_UGET(16, bus_p, PCIE_LINKCTL);
	PCIE_CAP_UPUT(16, bus_p, PCIE_LINKCTL,
	    (reg16 | PCIE_LINKCTL_BAND_MGT_INT_EN |
	    PCIE_LINKCTL_AUTO_BAND_INT_EN) & ~PCIE_LINKCTL_HW_AUT_WIDTH_DIS);
	PCIE_DBG_UCAP(dip, bus_p, "LINKCTL", 16, PCIE_LINKCTL, reg16);

	if (bus_p->bus_pcie_ver > 1) {
		/* clear Hardware Autonomous Link Speed Disable bit */
		reg16 = PCIE_CAP_UGET(16, bus_p, PCIE_LINKCTL2);
		PCIE_CAP_UPUT(16, bus_p, PCIE_LINKCTL2, reg16 &
		    ~PCIE_LINKCTL2_HW_AUT_SPEED_DIS);
		PCIE_DBG_UCAP(dip, bus_p, "LINKCTL2", 16, PCIE_LINKCTL2, reg16);
	}

	/* Clear Link Equalization Requst Status bit */
	if (bus_p->bus_ext2_off != 0) {
		reg16 = PCIE_CAP_UGET(16, bus_p, PCIE_LINKSTS2);
		PCIE_CAP_UPUT(16, bus_p, PCIE_LINKSTS2, reg16);
		PCIE_DBG_UCAP(dip, bus_p, "LINKSTS2", 16, PCIE_LINKSTS2, reg16);
	}

	/* check if device is working under max link width */
	reg32 = (reg32 & PCIE_LINKCAP_MAX_WIDTH_MASK) >> 4;
	if (bw_p->width != (uint8_t)reg32) {
		PCIE_DBG("%s device is not under max link width 0x%x->0x%x.\n",
		    ddi_driver_name(dip), bw_p->width, reg32);
		/* Gen3 device doesn't work under Max link width */
		bw_p->gen3_ready = B_FALSE;
	}

	if (PCIE_IS_PCIE_LEAF(bus_p) || PCIE_IS_PCIE_BDG(bus_p) ||
	    PCIE_IS_SWU(bus_p)) {
		pcie_bus_t *parent_bus_p;
		uint8_t	speed, optimal;

		/* get supported link speeds between two devices */
		parent_bus_p = PCIE_DIP2BUS(ddi_get_parent(dip));
		speed = (uint8_t)pcie_link_get_supported_speeds(dip);
		bw_p->sup_speeds = speed;
		if (!PCIE_IS_RC(parent_bus_p)) {
			(parent_bus_p->bus_linkbw).sup_speeds = speed;
			/* get the max link speeds between two devices */
			for (optimal = 0; speed; speed >>= 1, optimal++)
				;
			if (bw_p->speed != --optimal) {
				PCIE_DBG("%s device is not under max link speed"
				    "0x%x->0x%x\n", ddi_driver_name(dip),
				    bw_p->sup_speeds, optimal);
			}
		}
	}
}

/*
 * The following PCIe registers are initialized here:
 * Device Control Regsiter:
 * Device Control 2 Regsiter:
 * Link Control Register;
 * Link Control 2 Register;
 * Link Control 3 Register;
 * Link Status Register;
 * Link Status 2 Register;
 *
 * The following features are initialized/Enabled/Configured:
 * MPS/MRRS;
 * Completion Timeout;
 * Link Bandwidth Management;
 * ACS/IDO/RO/Extended Tag;
 */

void
pcie_init_regs(dev_info_t *dip)
{
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);
	uint16_t	reg16, tmp16;

	if (!PCIE_IS_PCIE(bus_p))
		return;

	/* Check if PCIe device is lower than version 2 */
	bus_p->bus_pcie_ver = PCIE_CAP_UGET(16, bus_p, PCIE_PCIECAP) &
	    PCIE_PCIECAP_VER_MASK;

	/* Setup PCIe device control register */
	reg16 = PCIE_CAP_UGET(16, bus_p, PCIE_DEVCTL);
	/* note: MPS/MRRS are initialized in pcie_init_mps() */
	tmp16 = (reg16 & (PCIE_DEVCTL_MAX_READ_REQ_MASK |
	    PCIE_DEVCTL_MAX_PAYLOAD_MASK)) |
	    (pcie_devctl_default & ~(PCIE_DEVCTL_MAX_READ_REQ_MASK |
	    PCIE_DEVCTL_MAX_PAYLOAD_MASK));
	PCIE_CAP_UPUT(16, bus_p, PCIE_DEVCTL, tmp16);
	PCIE_DBG_UCAP(dip, bus_p, "DEVCTL", 16, PCIE_DEVCTL, reg16);

	/* Setup PCIe device control 2 register */
	if (bus_p->bus_pcie_ver > 1 && !pcie_disable_ido) {
		uint16_t pcie_devctl2_default =
		    PCIE_DEVCTL2_IDO_REQ_EN | PCIE_DEVCTL2_IDO_COMPL_EN;
		reg16 = PCIE_CAP_UGET(16, bus_p, PCIE_DEVCTL2);
		tmp16 = reg16 | pcie_devctl2_default;
		PCIE_CAP_UPUT(16, bus_p, PCIE_DEVCTL2, tmp16);
		PCIE_DBG_UCAP(dip, bus_p, "DEVCTL2", 16, PCIE_DEVCTL2, reg16);
	}

	/* Init MPS/MRRS */
	PCIE_IS_RP(bus_p) ? pcie_init_root_port_mps(dip) :
	    pcie_init_mps(dip);

	/* Init Link related registers */
	pcie_link_config(dip);

	/* Setup Completion Timeout Value */
	if (PCIE_CTO_SUPPORTED(bus_p) && !pcie_disable_cto) {
		pcie_cto_set(dip, 0);
		pcie_cto_rp_update(dip);
	}

	/* Enable Access Control Service */
	if (!pcie_disable_acs)
		pcie_acs_enable(dip);
}

dev_info_t *
pcie_find_parent_by_busno(dev_info_t *rootp, uint8_t bus_no)
{
	dev_info_t	*dip;
	pcie_bus_t	*bus_p;

	for (dip = rootp; dip; dip = ddi_get_next_sibling(dip)) {
		if ((bus_p = PCIE_DIP2BUS(dip)) == NULL)
			continue;

		if (PCI_GET_SEC_BUS(dip) == bus_no)
			return (dip);

		if (PCIE_IS_BDG(bus_p) &&
		    bus_no >= bus_p->bus_bus_range.lo &&
		    bus_no <= bus_p->bus_bus_range.hi)
			return (pcie_find_parent_by_busno(
			    ddi_get_child(dip), bus_no));
	}

	return (NULL);
}
