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

/*
 * Update driver private statistics.
 */
/* ARGSUSED */
int
igbvf_update_stats(kstat_t *ks, int rw)
{
	igbvf_t *igbvf;
	struct e1000_hw *hw;
	igbvf_stat_t *igbvf_ks;
#ifdef IGBVF_DEBUG
	int i;
#endif

	if (rw == KSTAT_WRITE)
		return (EACCES);

	igbvf = (igbvf_t *)ks->ks_private;
	igbvf_ks = (igbvf_stat_t *)ks->ks_data;
	hw = &igbvf->hw;

	mutex_enter(&igbvf->gen_lock);

	if (IGBVF_IS_SUSPENDED_PIO(igbvf)) {
		mutex_exit(&igbvf->gen_lock);
		return (EBUSY);
	}

	/*
	 * Basic information.
	 */
	igbvf_ks->link_speed.value.ui32 = igbvf->link_speed;
	igbvf_ks->reset_count.value.ui32 = igbvf->reset_count;

#ifdef IGBVF_DEBUG
	igbvf_ks->rx_frame_error.value.ui32 = 0;
	igbvf_ks->rx_cksum_error.value.ui32 = 0;
	igbvf_ks->rx_exceed_pkt.value.ui32 = 0;
	for (i = 0; i < igbvf->num_rx_rings; i++) {
		igbvf_ks->rx_frame_error.value.ui32 +=
		    igbvf->rx_rings[i].stat_frame_error;
		igbvf_ks->rx_cksum_error.value.ui32 +=
		    igbvf->rx_rings[i].stat_cksum_error;
		igbvf_ks->rx_exceed_pkt.value.ui32 +=
		    igbvf->rx_rings[i].stat_exceed_pkt;
	}

	igbvf_ks->tx_overload.value.ui32 = 0;
	igbvf_ks->tx_fail_no_tbd.value.ui32 = 0;
	igbvf_ks->tx_fail_no_tcb.value.ui32 = 0;
	igbvf_ks->tx_fail_dma_bind.value.ui32 = 0;
	igbvf_ks->tx_reschedule.value.ui32 = 0;
	for (i = 0; i < igbvf->num_tx_rings; i++) {
		igbvf_ks->tx_overload.value.ui32 +=
		    igbvf->tx_rings[i].stat_overload;
		igbvf_ks->tx_fail_no_tbd.value.ui32 +=
		    igbvf->tx_rings[i].stat_fail_no_tbd;
		igbvf_ks->tx_fail_no_tcb.value.ui32 +=
		    igbvf->tx_rings[i].stat_fail_no_tcb;
		igbvf_ks->tx_fail_dma_bind.value.ui32 +=
		    igbvf->tx_rings[i].stat_fail_dma_bind;
		igbvf_ks->tx_reschedule.value.ui32 +=
		    igbvf->tx_rings[i].stat_reschedule;
	}
#endif

	/*
	 * Hardware calculated statistics.
	 * The per-VF counters are not cleared on read.
	 */
	igbvf_get_vf_stat(hw, E1000_VFGPRC, &igbvf_ks->gprc.value.ui64);

	igbvf_get_vf_stat(hw, E1000_VFGPTC, &igbvf_ks->gptc.value.ui64);

	igbvf_get_vf_stat(hw, E1000_VFGORC, &igbvf_ks->gorc.value.ui64);

	igbvf_get_vf_stat(hw, E1000_VFGOTC, &igbvf_ks->gotc.value.ui64);

	igbvf_get_vf_stat(hw, E1000_VFMPRC, &igbvf_ks->mprc.value.ui64);

	igbvf_get_vf_stat(hw, E1000_VFGPRLBC, &igbvf_ks->gprlbc.value.ui64);

	igbvf_get_vf_stat(hw, E1000_VFGPTLBC, &igbvf_ks->gptlbc.value.ui64);

	igbvf_get_vf_stat(hw, E1000_VFGORLBC, &igbvf_ks->gorlbc.value.ui64);

	igbvf_get_vf_stat(hw, E1000_VFGOTLBC, &igbvf_ks->gotlbc.value.ui64);

	mutex_exit(&igbvf->gen_lock);

	if (igbvf_check_acc_handle(igbvf->osdep.reg_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(igbvf->dip, DDI_SERVICE_DEGRADED);
		return (EIO);
	}

	return (0);
}

/*
 * igbvf_get_vf_stat - get the current value of one statistic.
 * Statistics are 32-bit and are not clear-on-read.  This detects and handles
 * the situation where the statistic rolls over from 32 bits to 0, and properly
 * updates a 64-bit quantity.
 */
void
igbvf_get_vf_stat(struct e1000_hw *hw, uint32_t offset, uint64_t *statistic)
{
	uint32_t prev_value;
	uint32_t curr_value;

	prev_value = (uint32_t)*statistic;

	/* current value of 32-bit counter */
	curr_value = E1000_READ_REG(hw, offset);

	/* handle rollover situation */
	if (curr_value < prev_value) {
		*statistic += 0x100000000LL;
	}

	/* update 64-bit counter */
	*statistic &= 0xFFFFFFFF00000000ULL;
	*statistic |= curr_value;
}

/*
 * igbvf_update_stat_regs - udpate the statistic registers with saved values
 *
 * VF statistics registers are not clear-on-read. When a FLR happens, all
 * registers are cleared. We need to restore the registers with save values
 * to avoid statistics lost through reset.
 */
void
igbvf_update_stat_regs(igbvf_t *igbvf)
{
	struct e1000_hw *hw = &igbvf->hw;
	igbvf_stat_t *igbvf_ks;

	ASSERT(mutex_owned(&igbvf->gen_lock));

	if (igbvf->igbvf_ks == NULL)
		return;

	igbvf_ks = (igbvf_stat_t *)igbvf->igbvf_ks->ks_data;

	E1000_WRITE_REG(hw, E1000_VFGPRC,
	    (uint32_t)igbvf_ks->gprc.value.ui64);
	E1000_WRITE_REG(hw, E1000_VFGPTC,
	    (uint32_t)igbvf_ks->gptc.value.ui64);
	E1000_WRITE_REG(hw, E1000_VFGORC,
	    (uint32_t)igbvf_ks->gorc.value.ui64);
	E1000_WRITE_REG(hw, E1000_VFGOTC,
	    (uint32_t)igbvf_ks->gotc.value.ui64);
	E1000_WRITE_REG(hw, E1000_VFMPRC,
	    (uint32_t)igbvf_ks->mprc.value.ui64);
	E1000_WRITE_REG(hw, E1000_VFGPRLBC,
	    (uint32_t)igbvf_ks->gprlbc.value.ui64);
	E1000_WRITE_REG(hw, E1000_VFGPTLBC,
	    (uint32_t)igbvf_ks->gptlbc.value.ui64);
	E1000_WRITE_REG(hw, E1000_VFGORLBC,
	    (uint32_t)igbvf_ks->gorlbc.value.ui64);
	E1000_WRITE_REG(hw, E1000_VFGOTLBC,
	    (uint32_t)igbvf_ks->gotlbc.value.ui64);
}

/*
 * Create and initialize the driver private statistics.
 */
int
igbvf_init_stats(igbvf_t *igbvf)
{
	kstat_t *ks;
	igbvf_stat_t *igbvf_ks;

	/*
	 * Create and init kstat
	 */
	ks = kstat_create(MODULE_NAME, ddi_get_instance(igbvf->dip),
	    "statistics", "net", KSTAT_TYPE_NAMED,
	    sizeof (igbvf_stat_t) / sizeof (kstat_named_t), 0);

	if (ks == NULL) {
		igbvf_error(igbvf,
		    "Could not create kernel statistics");
		return (IGBVF_FAILURE);
	}

	igbvf->igbvf_ks = ks;

	igbvf_ks = (igbvf_stat_t *)ks->ks_data;

	/*
	 * Initialize all the statistics.
	 */
	kstat_named_init(&igbvf_ks->link_speed, "link_speed",
	    KSTAT_DATA_UINT32);
	kstat_named_init(&igbvf_ks->reset_count, "reset_count",
	    KSTAT_DATA_UINT32);

#ifdef IGBVF_DEBUG
	kstat_named_init(&igbvf_ks->rx_frame_error, "rx_frame_error",
	    KSTAT_DATA_UINT32);
	kstat_named_init(&igbvf_ks->rx_cksum_error, "rx_cksum_error",
	    KSTAT_DATA_UINT32);
	kstat_named_init(&igbvf_ks->rx_exceed_pkt, "rx_exceed_pkt",
	    KSTAT_DATA_UINT32);
	kstat_named_init(&igbvf_ks->tx_overload, "tx_overload",
	    KSTAT_DATA_UINT32);
	kstat_named_init(&igbvf_ks->tx_fail_no_tbd, "tx_fail_no_tbd",
	    KSTAT_DATA_UINT32);
	kstat_named_init(&igbvf_ks->tx_fail_no_tcb, "tx_fail_no_tcb",
	    KSTAT_DATA_UINT32);
	kstat_named_init(&igbvf_ks->tx_fail_dma_bind, "tx_fail_dma_bind",
	    KSTAT_DATA_UINT32);
	kstat_named_init(&igbvf_ks->tx_reschedule, "tx_reschedule",
	    KSTAT_DATA_UINT32);
#endif

	kstat_named_init(&igbvf_ks->gprc, "good_packets_rx",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&igbvf_ks->gptc, "good_packets_tx",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&igbvf_ks->gorc, "good_octets_rx",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&igbvf_ks->gotc, "good_octets_tx",
	    KSTAT_DATA_UINT64);

	kstat_named_init(&igbvf_ks->gprlbc, "good_packets_rx_lb",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&igbvf_ks->gptlbc, "good_packets_tx_lb",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&igbvf_ks->gorlbc, "good_octets_rx_lb",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&igbvf_ks->gotlbc, "good_octets_tx_lb",
	    KSTAT_DATA_UINT64);

	/*
	 * Function to provide kernel stat update on demand
	 */
	ks->ks_update = igbvf_update_stats;

	ks->ks_private = (void *)igbvf;

	/*
	 * Add kstat to systems kstat chain
	 */
	kstat_install(ks);

	return (IGBVF_SUCCESS);
}

/*
 * Retrieve a value for one of the statistics for a particular rx ring
 */
int
igbvf_rx_ring_stat(mac_ring_driver_t rh, uint_t stat, uint64_t *val)
{
	igbvf_rx_ring_t *rx_ring = (igbvf_rx_ring_t *)rh;

	switch (stat) {
	case MAC_STAT_RBYTES:
		*val = rx_ring->rx_bytes;
		break;

	case MAC_STAT_IPACKETS:
		*val = rx_ring->rx_pkts;
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
igbvf_tx_ring_stat(mac_ring_driver_t rh, uint_t stat, uint64_t *val)
{
	igbvf_tx_ring_t *tx_ring = (igbvf_tx_ring_t *)rh;

	switch (stat) {
	case MAC_STAT_OBYTES:
		*val = tx_ring->tx_bytes;
		break;

	case MAC_STAT_OPACKETS:
		*val = tx_ring->tx_pkts;
		break;

	default:
		*val = 0;
		return (ENOTSUP);
	}

	return (0);
}
