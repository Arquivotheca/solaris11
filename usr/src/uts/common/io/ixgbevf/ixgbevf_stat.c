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

#include "ixgbevf_sw.h"

/*
 * Update driver private statistics.
 */
static int
ixgbevf_update_stats(kstat_t *ks, int rw)
{
	ixgbevf_t *ixgbevf;
	struct ixgbe_hw *hw;
	ixgbevf_stat_t *ixgbevf_ks;
	int i;

	if (rw == KSTAT_WRITE)
		return (EACCES);

	ixgbevf = (ixgbevf_t *)ks->ks_private;
	ixgbevf_ks = (ixgbevf_stat_t *)ks->ks_data;
	hw = &ixgbevf->hw;

	mutex_enter(&ixgbevf->gen_lock);

	/*
	 * Basic information
	 */
	ixgbevf_ks->link_speed.value.ui64 = ixgbevf->link_speed;
	ixgbevf_ks->reset_count.value.ui64 = ixgbevf->reset_count;
	ixgbevf_ks->lroc.value.ui64 = ixgbevf->lro_pkt_count;

#ifdef IXGBE_DEBUG
	ixgbevf_ks->rx_frame_error.value.ui64 = 0;
	ixgbevf_ks->rx_cksum_error.value.ui64 = 0;
	ixgbevf_ks->rx_exceed_pkt.value.ui64 = 0;
	for (i = 0; i < ixgbevf->num_rx_rings; i++) {
		ixgbevf_ks->rx_frame_error.value.ui64 +=
		    ixgbevf->rx_rings[i].stat_frame_error;
		ixgbevf_ks->rx_cksum_error.value.ui64 +=
		    ixgbevf->rx_rings[i].stat_cksum_error;
		ixgbevf_ks->rx_exceed_pkt.value.ui64 +=
		    ixgbevf->rx_rings[i].stat_exceed_pkt;
	}

	ixgbevf_ks->tx_overload.value.ui64 = 0;
	ixgbevf_ks->tx_fail_no_tbd.value.ui64 = 0;
	ixgbevf_ks->tx_fail_no_tcb.value.ui64 = 0;
	ixgbevf_ks->tx_fail_dma_bind.value.ui64 = 0;
	ixgbevf_ks->tx_reschedule.value.ui64 = 0;
	for (i = 0; i < ixgbevf->num_tx_rings; i++) {
		ixgbevf_ks->tx_overload.value.ui64 +=
		    ixgbevf->tx_rings[i].stat_overload;
		ixgbevf_ks->tx_fail_no_tbd.value.ui64 +=
		    ixgbevf->tx_rings[i].stat_fail_no_tbd;
		ixgbevf_ks->tx_fail_no_tcb.value.ui64 +=
		    ixgbevf->tx_rings[i].stat_fail_no_tcb;
		ixgbevf_ks->tx_fail_dma_bind.value.ui64 +=
		    ixgbevf->tx_rings[i].stat_fail_dma_bind;
		ixgbevf_ks->tx_reschedule.value.ui64 +=
		    ixgbevf->tx_rings[i].stat_reschedule;
	}
#endif

	/*
	 * Hardware calculated statistics.
	 */
	ixgbevf_ks->gprc.value.ui64 = IXGBE_READ_REG(hw, IXGBE_VFGPRC);
	ixgbevf_ks->gptc.value.ui64 = IXGBE_READ_REG(hw, IXGBE_VFGPTC);

	ixgbevf_ks->gor.value.ui64 = IXGBE_READ_REG(hw, IXGBE_VFGORC_LSB);
	ixgbevf_ks->gor.value.ui64 |=
	    ((uint64_t)(IXGBE_READ_REG(hw, IXGBE_VFGORC_MSB))) << 32;

	ixgbevf_ks->got.value.ui64 = IXGBE_READ_REG(hw, IXGBE_VFGOTC_LSB);
	ixgbevf_ks->got.value.ui64 |=
	    ((uint64_t)(IXGBE_READ_REG(hw, IXGBE_VFGOTC_MSB))) << 32;

	mutex_exit(&ixgbevf->gen_lock);

	if (ixgbevf_check_acc_handle(ixgbevf->osdep.reg_handle) != DDI_FM_OK)
		ddi_fm_service_impact(ixgbevf->dip, DDI_SERVICE_UNAFFECTED);

	return (0);
}

/*
 * Create and initialize the driver private statistics.
 */
int
ixgbevf_init_stats(ixgbevf_t *ixgbevf)
{
	kstat_t *ks;
	ixgbevf_stat_t *ixgbevf_ks;

	/*
	 * Create and init kstat
	 */
	ks = kstat_create(MODULE_NAME, ddi_get_instance(ixgbevf->dip),
	    "statistics", "net", KSTAT_TYPE_NAMED,
	    sizeof (ixgbevf_stat_t) / sizeof (kstat_named_t), 0);

	if (ks == NULL) {
		ixgbevf_error(ixgbevf,
		    "Could not create kernel statistics");
		return (IXGBE_FAILURE);
	}

	ixgbevf->ixgbevf_ks = ks;

	ixgbevf_ks = (ixgbevf_stat_t *)ks->ks_data;

	/*
	 * Initialize all the statistics.
	 */
	kstat_named_init(&ixgbevf_ks->link_speed, "link_speed",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&ixgbevf_ks->reset_count, "reset_count",
	    KSTAT_DATA_UINT64);

#ifdef IXGBE_DEBUG
	kstat_named_init(&ixgbevf_ks->rx_frame_error, "rx_frame_error",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&ixgbevf_ks->rx_cksum_error, "rx_cksum_error",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&ixgbevf_ks->rx_exceed_pkt, "rx_exceed_pkt",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&ixgbevf_ks->tx_overload, "tx_overload",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&ixgbevf_ks->tx_fail_no_tbd, "tx_fail_no_tbd",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&ixgbevf_ks->tx_fail_no_tcb, "tx_fail_no_tcb",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&ixgbevf_ks->tx_fail_dma_bind, "tx_fail_dma_bind",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&ixgbevf_ks->tx_reschedule, "tx_reschedule",
	    KSTAT_DATA_UINT64);
#endif

	kstat_named_init(&ixgbevf_ks->gprc, "good_pkts_recvd",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&ixgbevf_ks->gptc, "good_pkts_xmitd",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&ixgbevf_ks->gor, "good_octets_recvd",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&ixgbevf_ks->got, "good_octets_xmitd",
	    KSTAT_DATA_UINT64);

	kstat_named_init(&ixgbevf_ks->lroc, "lro_pkt_count",
	    KSTAT_DATA_UINT64);

	/*
	 * Function to provide kernel stat update on demand
	 */
	ks->ks_update = ixgbevf_update_stats;

	ks->ks_private = (void *)ixgbevf;

	/*
	 * Add kstat to systems kstat chain
	 */
	kstat_install(ks);

	return (IXGBE_SUCCESS);
}

/*
 * Retrieve a value for one of the statistics.
 */
int
ixgbevf_m_stat(void *arg, uint_t stat, uint64_t *val)
{
	ixgbevf_t *ixgbevf = (ixgbevf_t *)arg;
	struct ixgbe_hw *hw = &ixgbevf->hw;
	ixgbevf_stat_t *ixgbevf_ks;

	ixgbevf_ks = (ixgbevf_stat_t *)ixgbevf->ixgbevf_ks->ks_data;

	mutex_enter(&ixgbevf->gen_lock);

	if (ixgbevf->ixgbevf_state & IXGBE_SUSPENDED) {
		mutex_exit(&ixgbevf->gen_lock);
		return (ECANCELED);
	}

	switch (stat) {
	case MAC_STAT_IFSPEED:
		*val = ixgbevf->link_speed * 1000000ull;
		break;

	case MAC_STAT_MULTIRCV:
		ixgbevf_ks->mprc.value.ui64 +=
		    IXGBE_READ_REG(hw, IXGBE_VFMPRC);
		*val = ixgbevf_ks->mprc.value.ui64;
		break;

	case MAC_STAT_RBYTES:
		ixgbevf_ks->gor.value.ui64 = IXGBE_READ_REG(hw,
		    IXGBE_VFGORC_LSB);
		ixgbevf_ks->gor.value.ui64 |=
		    ((uint64_t)(IXGBE_READ_REG(hw, IXGBE_VFGORC_MSB))) << 32;
		*val = ixgbevf_ks->gor.value.ui64;
		break;

	case MAC_STAT_OBYTES:
		ixgbevf_ks->got.value.ui64 = IXGBE_READ_REG(hw,
		    IXGBE_VFGOTC_LSB);
		ixgbevf_ks->got.value.ui64 |=
		    ((uint64_t)(IXGBE_READ_REG(hw, IXGBE_VFGOTC_MSB))) << 32;
		*val = ixgbevf_ks->got.value.ui64;
		break;

	case MAC_STAT_IPACKETS:
		ixgbevf_ks->gprc.value.ui64 = IXGBE_READ_REG(hw, IXGBE_VFGPRC);
		*val = ixgbevf_ks->gprc.value.ui64;
		break;

	case MAC_STAT_OPACKETS:
		ixgbevf_ks->gptc.value.ui64 = IXGBE_READ_REG(hw, IXGBE_VFGPTC);
		*val = ixgbevf_ks->gptc.value.ui64;
		break;

	/* MII/GMII stats */
	case ETHER_STAT_CAP_10GFDX:
		*val = 1;
		break;

	case ETHER_STAT_CAP_1000FDX:
		*val = 1;
		break;

	case ETHER_STAT_CAP_100FDX:
		*val = 1;
		break;

	case ETHER_STAT_CAP_ASMPAUSE:
		*val = ixgbevf->param_asym_pause_cap;
		break;

	case ETHER_STAT_CAP_PAUSE:
		*val = ixgbevf->param_pause_cap;
		break;

	case ETHER_STAT_CAP_AUTONEG:
		*val = 1;
		break;

	case ETHER_STAT_ADV_CAP_10GFDX:
		*val = ixgbevf->param_adv_10000fdx_cap;
		break;

	case ETHER_STAT_ADV_CAP_1000FDX:
		*val = ixgbevf->param_adv_1000fdx_cap;
		break;

	case ETHER_STAT_ADV_CAP_100FDX:
		*val = ixgbevf->param_adv_100fdx_cap;
		break;

	case ETHER_STAT_ADV_CAP_ASMPAUSE:
		*val = ixgbevf->param_adv_asym_pause_cap;
		break;

	case ETHER_STAT_ADV_CAP_PAUSE:
		*val = ixgbevf->param_adv_pause_cap;
		break;

	case ETHER_STAT_ADV_CAP_AUTONEG:
		*val = ixgbevf->param_adv_autoneg_cap;
		break;

	case ETHER_STAT_LP_CAP_10GFDX:
		*val = ixgbevf->param_lp_10000fdx_cap;
		break;

	case ETHER_STAT_LP_CAP_1000FDX:
		*val = ixgbevf->param_lp_1000fdx_cap;
		break;

	case ETHER_STAT_LP_CAP_100FDX:
		*val = ixgbevf->param_lp_100fdx_cap;
		break;

	case ETHER_STAT_LP_CAP_ASMPAUSE:
		*val = ixgbevf->param_lp_asym_pause_cap;
		break;

	case ETHER_STAT_LP_CAP_PAUSE:
		*val = ixgbevf->param_lp_pause_cap;
		break;

	case ETHER_STAT_LP_CAP_AUTONEG:
		*val = ixgbevf->param_lp_autoneg_cap;
		break;

	case ETHER_STAT_LINK_ASMPAUSE:
		*val = ixgbevf->param_asym_pause_cap;
		break;

	case ETHER_STAT_LINK_PAUSE:
		*val = ixgbevf->param_pause_cap;
		break;

	case ETHER_STAT_LINK_AUTONEG:
		*val = ixgbevf->param_adv_autoneg_cap;
		break;

	case ETHER_STAT_LINK_DUPLEX:
		*val = ixgbevf->link_duplex;
		break;

	case ETHER_STAT_CAP_REMFAULT:
		*val = ixgbevf->param_rem_fault;
		break;

	case ETHER_STAT_ADV_REMFAULT:
		*val = ixgbevf->param_adv_rem_fault;
		break;

	case ETHER_STAT_LP_REMFAULT:
		*val = ixgbevf->param_lp_rem_fault;
		break;

	default:
		mutex_exit(&ixgbevf->gen_lock);
		return (ENOTSUP);
	}

	mutex_exit(&ixgbevf->gen_lock);

	if (ixgbevf_check_acc_handle(ixgbevf->osdep.reg_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(ixgbevf->dip, DDI_SERVICE_DEGRADED);
		return (EIO);
	}

	return (0);
}

/*
 * Retrieve a value for one of the statistics for a particular rx ring
 */
int
ixgbevf_rx_ring_stat(mac_ring_driver_t rh, uint_t stat, uint64_t *val)
{
	ixgbevf_rx_ring_t *rx_ring = (ixgbevf_rx_ring_t *)rh;
	ixgbevf_t *ixgbevf = rx_ring->ixgbevf;

	if (ixgbevf->ixgbevf_state & IXGBE_SUSPENDED) {
		return (ECANCELED);
	}

	switch (stat) {
	case MAC_STAT_RBYTES:
		*val = rx_ring->stat_rbytes;
		break;

	case MAC_STAT_IPACKETS:
		*val = rx_ring->stat_ipackets;
		break;

	default:
		*val = 0;
		return (ENOTSUP);
	}

	return (0);
}

/*
 * Retrieve a value for one of the statistics for a particular tx ring
 */
int
ixgbevf_tx_ring_stat(mac_ring_driver_t rh, uint_t stat, uint64_t *val)
{
	ixgbevf_tx_ring_t *tx_ring = (ixgbevf_tx_ring_t *)rh;
	ixgbevf_t *ixgbevf = tx_ring->ixgbevf;

	if (ixgbevf->ixgbevf_state & IXGBE_SUSPENDED) {
		return (ECANCELED);
	}

	switch (stat) {
	case MAC_STAT_OBYTES:
		*val = tx_ring->stat_obytes;
		break;

	case MAC_STAT_OPACKETS:
		*val = tx_ring->stat_opackets;
		break;

	default:
		*val = 0;
		return (ENOTSUP);
	}

	return (0);
}
