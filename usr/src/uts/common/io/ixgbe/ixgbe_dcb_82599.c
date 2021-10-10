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

/* IntelVersion: 1.46 scm_011511_003853 */


#include "ixgbe_type.h"
#include "ixgbe_dcb.h"
#include "ixgbe_dcb_82599.h"

/*
 * ixgbe_dcb_get_tc_stats_82599 - Returns status for each traffic class
 * @hw: pointer to hardware structure
 * @stats: pointer to statistics structure
 * @tc_count:  Number of elements in bwg_array.
 *
 * This function returns the status data for each of the Traffic Classes in use.
 */
s32
ixgbe_dcb_get_tc_stats_82599(struct ixgbe_hw *hw,
    struct ixgbe_hw_stats *stats, u8 tc_count)
{
	int tc;

	DEBUGFUNC("dcb_get_tc_stats");

	if (tc_count > MAX_TRAFFIC_CLASS)
		return (DCB_ERR_PARAM);

	/* Statistics pertaining to each traffic class */
	for (tc = 0; tc < tc_count; tc++) {
		/* Transmitted Packets */
		stats->qptc[tc] += IXGBE_READ_REG(hw, IXGBE_QPTC(tc));
		/* Transmitted Bytes (read low first to prevent missed carry) */
		stats->qbtc[tc] += IXGBE_READ_REG(hw, IXGBE_QBTC_L(tc));
		stats->qbtc[tc] +=
		    (((u64)(IXGBE_READ_REG(hw, IXGBE_QBTC_H(tc)))) << 32);
		/* Received Packets */
		stats->qprc[tc] += IXGBE_READ_REG(hw, IXGBE_QPRC(tc));
		/* Received Bytes (read low first to prevent missed carry) */
		stats->qbrc[tc] += IXGBE_READ_REG(hw, IXGBE_QBRC_L(tc));
		stats->qbrc[tc] +=
		    (((u64)(IXGBE_READ_REG(hw, IXGBE_QBRC_H(tc)))) << 32);

		/* Received Dropped Packet */
		stats->qprdc[tc] += IXGBE_READ_REG(hw, IXGBE_QPRDC(tc));
	}

	return (IXGBE_SUCCESS);
}

/*
 * ixgbe_dcb_get_pfc_stats_82599 - Return CBFC status data
 * @hw: pointer to hardware structure
 * @stats: pointer to statistics structure
 * @tc_count:  Number of elements in bwg_array.
 *
 * This function returns the CBFC status data for each of the Traffic Classes.
 */
s32
ixgbe_dcb_get_pfc_stats_82599(struct ixgbe_hw *hw,
    struct ixgbe_hw_stats *stats, u8 tc_count)
{
	int tc;

	DEBUGFUNC("dcb_get_pfc_stats");

	if (tc_count > MAX_TRAFFIC_CLASS)
		return (DCB_ERR_PARAM);

	for (tc = 0; tc < tc_count; tc++) {
		/* Priority XOFF Transmitted */
		stats->pxofftxc[tc] += IXGBE_READ_REG(hw, IXGBE_PXOFFTXC(tc));
		/* Priority XOFF Received */
		stats->pxoffrxc[tc] += IXGBE_READ_REG(hw, IXGBE_PXOFFRXCNT(tc));
	}

	return (IXGBE_SUCCESS);
}

/*
 * ixgbe_dcb_config_packet_buffers_82599 - Configure DCB packet buffers
 * @hw: pointer to hardware structure
 * @dcb_config: pointer to ixgbe_dcb_config structure
 *
 * Configure packet buffers for DCB mode.
 */
s32
ixgbe_dcb_config_packet_buffers_82599(struct ixgbe_hw *hw,
    struct ixgbe_dcb_config *dcb_config)
{
	s32 ret_val = IXGBE_SUCCESS;
	u32 rxpktsize;
	u32 maxtxpktsize = IXGBE_TXPBSIZE_MAX;
	u32 txpktsize;
	int num_tcs;
	u8  i = 0;

	num_tcs = dcb_config->num_tcs.pg_tcs;
	/* Setup Rx packet buffer sizes */
	if (dcb_config->rx_pba_cfg == pba_80_48) {
		/*
		 * This really means configure the first half of the TCs
		 * (Traffic Classes) to use 5/8 of the Rx packet buffer
		 * space.  To determine the size of the buffer for each TC,
		 * multiply the size of the entire packet buffer by 5/8
		 * then divide by half of the number of TCs.
		 */
		rxpktsize = (hw->mac.rx_pb_size * 5 / 8) / (num_tcs / 2);
		for (i = 0; i < (num_tcs / 2); i++)
			IXGBE_WRITE_REG(hw, IXGBE_RXPBSIZE(i),
			    rxpktsize << IXGBE_RXPBSIZE_SHIFT);

		/*
		 * The second half of the TCs use the remaining 3/8
		 * of the Rx packet buffer space.
		 */
		rxpktsize = (hw->mac.rx_pb_size * 3 / 8) / (num_tcs / 2);
		for (; i < num_tcs; i++)
			IXGBE_WRITE_REG(hw, IXGBE_RXPBSIZE(i),
			    rxpktsize << IXGBE_RXPBSIZE_SHIFT);
	} else {
		/* Divide the Rx packet buffer evenly among the TCs */
		rxpktsize = hw->mac.rx_pb_size / num_tcs;
		for (i = 0; i < num_tcs; i++)
			IXGBE_WRITE_REG(hw, IXGBE_RXPBSIZE(i),
			    rxpktsize << IXGBE_RXPBSIZE_SHIFT);
	}
	/* Setup remainig TCs, if any, to zero buffer size */
	for (; i < MAX_TRAFFIC_CLASS; i++)
		IXGBE_WRITE_REG(hw, IXGBE_RXPBSIZE(i), 0);

	/* Setup Tx packet buffer and threshold equally for all TCs */
	txpktsize = maxtxpktsize/num_tcs;
	for (i = 0; i < num_tcs; i++) {
		IXGBE_WRITE_REG(hw, IXGBE_TXPBSIZE(i), txpktsize);
		IXGBE_WRITE_REG(hw, IXGBE_TXPBTHRESH(i),
		    ((txpktsize  / 1024) - IXGBE_TXPKT_SIZE_MAX));
	}

	/* Setup remainig TCs, if any, to zero buffer size */
	for (; i < MAX_TRAFFIC_CLASS; i++) {
		IXGBE_WRITE_REG(hw, IXGBE_TXPBSIZE(i), 0);
		IXGBE_WRITE_REG(hw, IXGBE_TXPBTHRESH(i), 0);
	}

	return (ret_val);
}

/*
 * ixgbe_dcb_config_rx_arbiter_82599 - Config Rx Data arbiter
 * @hw: pointer to hardware structure
 * @dcb_config: pointer to ixgbe_dcb_config structure
 *
 * Configure Rx Packet Arbiter and credits for each traffic class.
 */
s32
ixgbe_dcb_config_rx_arbiter_82599(struct ixgbe_hw *hw,
    struct ixgbe_dcb_config *dcb_config)
{
	struct tc_bw_alloc	*p;
	u32 reg			= 0;
	u32 credit_refill	= 0;
	u32 credit_max		= 0;
	u8 i			= 0;
	u8 j;

	/*
	 * Disable the arbiter before changing parameters
	 * (always enable recycle mode; WSP)
	 */
	reg = IXGBE_RTRPCS_RRM | IXGBE_RTRPCS_RAC | IXGBE_RTRPCS_ARBDIS;
	IXGBE_WRITE_REG(hw, IXGBE_RTRPCS, reg);

	/*
	 * map all UPs to TCs. up_to_tc_bitmap for each TC has corresponding
	 * bits sets for the UPs that needs to be mappped to that TC.
	 * e.g if priorities 6 and 7 are to be mapped to a TC then the
	 * up_to_tc_bitmap value for that TC will be 11000000 in binary.
	 */
	reg = 0;
	for (i = 0; i < MAX_TRAFFIC_CLASS; i++) {
		p = &dcb_config->tc_config[i].path[DCB_RX_CONFIG];
		for (j = 0; j < MAX_USER_PRIORITY; j++) {
			if (p->up_to_tc_bitmap & (1 << j))
				reg |= (i << (j * IXGBE_RTRUP2TC_UP_SHIFT));
		}
	}
	IXGBE_WRITE_REG(hw, IXGBE_RTRUP2TC, reg);

	/* Configure traffic class credits and priority */
	for (i = 0; i < dcb_config->num_tcs.pg_tcs; i++) {
		p = &dcb_config->tc_config[i].path[DCB_RX_CONFIG];

		credit_refill = p->data_credits_refill;
		credit_max    = p->data_credits_max;
		reg = credit_refill | (credit_max << IXGBE_RTRPT4C_MCL_SHIFT);

		reg |= (u32)(p->bwg_id) << IXGBE_RTRPT4C_BWG_SHIFT;

		if (p->prio_type == prio_link)
			reg |= IXGBE_RTRPT4C_LSP;

		IXGBE_WRITE_REG(hw, IXGBE_RTRPT4C(i), reg);
	}

	/*
	 * Configure Rx packet plane (recycle mode; WSP) and
	 * enable arbiter
	 */
	reg = IXGBE_RTRPCS_RRM | IXGBE_RTRPCS_RAC;
	IXGBE_WRITE_REG(hw, IXGBE_RTRPCS, reg);

	return (IXGBE_SUCCESS);
}

/*
 * ixgbe_dcb_config_tx_desc_arbiter_82599 - Config Tx Desc. arbiter
 * @hw: pointer to hardware structure
 * @dcb_config: pointer to ixgbe_dcb_config structure
 *
 * Configure Tx Descriptor Arbiter and credits for each traffic class.
 */
s32
ixgbe_dcb_config_tx_desc_arbiter_82599(struct ixgbe_hw *hw,
    struct ixgbe_dcb_config *dcb_config)
{
	struct tc_bw_alloc *p;
	u32 reg, max_credits;
	u8 i;

	/* Clear the per-Tx queue credits; we use per-TC instead */
	for (i = 0; i < 128; i++) {
		IXGBE_WRITE_REG(hw, IXGBE_RTTDQSEL, i);
		IXGBE_WRITE_REG(hw, IXGBE_RTTDT1C, 0);
	}

	/* Configure traffic class credits and priority */
	for (i = 0; i < dcb_config->num_tcs.pg_tcs; i++) {
		p = &dcb_config->tc_config[i].path[DCB_TX_CONFIG];
		max_credits = dcb_config->tc_config[i].desc_credits_max;
		reg = max_credits << IXGBE_RTTDT2C_MCL_SHIFT;
		reg |= p->data_credits_refill;
		reg |= (u32)(p->bwg_id) << IXGBE_RTTDT2C_BWG_SHIFT;

		if (p->prio_type == prio_group)
			reg |= IXGBE_RTTDT2C_GSP;

		if (p->prio_type == prio_link)
			reg |= IXGBE_RTTDT2C_LSP;

		IXGBE_WRITE_REG(hw, IXGBE_RTTDT2C(i), reg);
	}

	/*
	 * Configure Tx descriptor plane (recycle mode; WSP) and
	 * enable arbiter
	 */
	reg = IXGBE_RTTDCS_TDPAC | IXGBE_RTTDCS_TDRM;
	IXGBE_WRITE_REG(hw, IXGBE_RTTDCS, reg);

	return (IXGBE_SUCCESS);
}

/*
 * ixgbe_dcb_config_tx_data_arbiter_82599 - Config Tx Data arbiter
 * @hw: pointer to hardware structure
 * @dcb_config: pointer to ixgbe_dcb_config structure
 *
 * Configure Tx Packet Arbiter and credits for each traffic class.
 */
s32
ixgbe_dcb_config_tx_data_arbiter_82599(struct ixgbe_hw *hw,
    struct ixgbe_dcb_config *dcb_config)
{
	struct tc_bw_alloc *p;
	u32 reg;
	u8 i, j;

	/*
	 * Disable the arbiter before changing parameters
	 * (always enable recycle mode; SP; arb delay)
	 */
	reg = IXGBE_RTTPCS_TPPAC | IXGBE_RTTPCS_TPRM |
	    (IXGBE_RTTPCS_ARBD_DCB << IXGBE_RTTPCS_ARBD_SHIFT) |
	    IXGBE_RTTPCS_ARBDIS;
	IXGBE_WRITE_REG(hw, IXGBE_RTTPCS, reg);

	/*
	 * map all UPs to TCs. up_to_tc_bitmap for each TC has corresponding
	 * bits sets for the UPs that needs to be mappped to that TC.
	 * e.g if priorities 6 and 7 are to be mapped to a TC then the
	 * up_to_tc_bitmap value for that TC will be 11000000 in binary.
	 */
	reg = 0;
	for (i = 0; i < MAX_TRAFFIC_CLASS; i++) {
		p = &dcb_config->tc_config[i].path[DCB_TX_CONFIG];
		for (j = 0; j < MAX_USER_PRIORITY; j++)
			if (p->up_to_tc_bitmap & (1 << j))
				reg |= (i << (j * IXGBE_RTTUP2TC_UP_SHIFT));
	}
	IXGBE_WRITE_REG(hw, IXGBE_RTTUP2TC, reg);

	/* Configure traffic class credits and priority */
	for (i = 0; i < dcb_config->num_tcs.pg_tcs; i++) {
		p = &dcb_config->tc_config[i].path[DCB_TX_CONFIG];
		reg = p->data_credits_refill;
		reg |= (u32)(p->data_credits_max) << IXGBE_RTTPT2C_MCL_SHIFT;
		reg |= (u32)(p->bwg_id) << IXGBE_RTTPT2C_BWG_SHIFT;

		if (p->prio_type == prio_group)
			reg |= IXGBE_RTTPT2C_GSP;

		if (p->prio_type == prio_link)
			reg |= IXGBE_RTTPT2C_LSP;

		IXGBE_WRITE_REG(hw, IXGBE_RTTPT2C(i), reg);
	}

	/*
	 * Configure Tx packet plane (recycle mode; SP; arb delay) and
	 * enable arbiter
	 */
	reg = IXGBE_RTTPCS_TPPAC | IXGBE_RTTPCS_TPRM |
	    (IXGBE_RTTPCS_ARBD_DCB << IXGBE_RTTPCS_ARBD_SHIFT);
	IXGBE_WRITE_REG(hw, IXGBE_RTTPCS, reg);

	return (IXGBE_SUCCESS);
}

/*
 * ixgbe_dcb_config_pfc_82599 - Configure priority flow control
 * @hw: pointer to hardware structure
 * @dcb_config: pointer to ixgbe_dcb_config structure
 *
 * Configure Priority Flow Control (PFC) for each traffic class.
 */
s32
ixgbe_dcb_config_pfc_82599(struct ixgbe_hw *hw,
    struct ixgbe_dcb_config *dcb_config)
{
	u32 i, reg, rx_pba_size;

	/* If PFC is disabled globally then fall back to LFC. */
	if (!dcb_config->pfc_mode_enable) {
		for (i = 0; i < dcb_config->num_tcs.pg_tcs; i++)
			hw->mac.ops.fc_enable(hw, i);
		goto out;
	}

	/* Configure PFC Tx thresholds per TC */
	for (i = 0; i < dcb_config->num_tcs.pg_tcs; i++) {
		rx_pba_size = IXGBE_READ_REG(hw, IXGBE_RXPBSIZE(i));
		rx_pba_size >>= IXGBE_RXPBSIZE_SHIFT;

		reg = (rx_pba_size - hw->fc.low_water) << 10;

		if (dcb_config->tc_config[i].dcb_pfc == pfc_enabled_full ||
		    dcb_config->tc_config[i].dcb_pfc == pfc_enabled_tx)
			reg |= IXGBE_FCRTL_XONE;
		IXGBE_WRITE_REG(hw, IXGBE_FCRTL_82599(i), reg);

		reg = (rx_pba_size - hw->fc.high_water) << 10;

		if (dcb_config->tc_config[i].dcb_pfc == pfc_enabled_full ||
		    dcb_config->tc_config[i].dcb_pfc == pfc_enabled_tx)
			reg |= IXGBE_FCRTH_FCEN;
		IXGBE_WRITE_REG(hw, IXGBE_FCRTH_82599(i), reg);
	}

	/* Configure pause time (2 TCs per register) */
	reg = hw->fc.pause_time | (hw->fc.pause_time << 16);
	for (i = 0; i < (MAX_TRAFFIC_CLASS / 2); i++)
		IXGBE_WRITE_REG(hw, IXGBE_FCTTV(i), reg);

	/* Configure flow control refresh threshold value */
	IXGBE_WRITE_REG(hw, IXGBE_FCRTV, hw->fc.pause_time / 2);

	/* Enable Transmit PFC */
	reg = IXGBE_FCCFG_TFCE_PRIORITY;
	IXGBE_WRITE_REG(hw, IXGBE_FCCFG, reg);

	/*
	 * Enable Receive PFC
	 * We will always honor XOFF frames we receive when
	 * we are in PFC mode.
	 */
	reg = IXGBE_READ_REG(hw, IXGBE_MFLCN);
	reg &= ~(IXGBE_MFLCN_RFCE | IXGBE_MFLCN_DPF);
	reg |= (IXGBE_MFLCN_PMCF | IXGBE_MFLCN_RPFCE);

	IXGBE_WRITE_REG(hw, IXGBE_MFLCN, reg);
out:
	return (IXGBE_SUCCESS);
}

/*
 * ixgbe_dcb_config_tc_stats_82599 - Config traffic class statistics
 * @hw: pointer to hardware structure
 *
 * Configure queue statistics registers, all queues belonging to same traffic
 * class uses a single set of queue statistics counters.
 */
s32
ixgbe_dcb_config_tc_stats_82599(struct ixgbe_hw *hw)
{
	u32 reg = 0;
	u8  i   = 0;

	/*
	 * Receive Queues stats setting
	 * 32 RQSMR registers, each configuring 4 queues.
	 * Set all 16 queues of each TC to the same stat
	 * with TC 'n' going to stat 'n'.
	 */
	for (i = 0; i < 32; i++) {
		reg = 0x01010101 * (i / 4);
		IXGBE_WRITE_REG(hw, IXGBE_RQSMR(i), reg);
	}
	/*
	 * Transmit Queues stats setting
	 * 32 TQSM registers, each controlling 4 queues.
	 * Set all queues of each TC to the same stat
	 * with TC 'n' going to stat 'n'.
	 * Tx queues are allocated non-uniformly to TCs:
	 * 32, 32, 16, 16, 8, 8, 8, 8.
	 */
	for (i = 0; i < 32; i++) {
		if (i < 8)
			reg = 0x00000000;
		else if (i < 16)
			reg = 0x01010101;
		else if (i < 20)
			reg = 0x02020202;
		else if (i < 24)
			reg = 0x03030303;
		else if (i < 26)
			reg = 0x04040404;
		else if (i < 28)
			reg = 0x05050505;
		else if (i < 30)
			reg = 0x06060606;
		else
			reg = 0x07070707;
		IXGBE_WRITE_REG(hw, IXGBE_TQSM(i), reg);
	}

	return (IXGBE_SUCCESS);
}

/*
 * ixgbe_dcb_config_82599 - Configure general DCB parameters
 * @hw: pointer to hardware structure
 * @dcb_config: pointer to ixgbe_dcb_config structure
 *
 * Configure general DCB parameters.
 */
s32
ixgbe_dcb_config_82599(struct ixgbe_hw *hw,
    struct ixgbe_dcb_config *dcb_config)
{
	u32 reg;
	u32 q;

	/* Disable the Tx desc arbiter so that MTQC can be changed */
	reg = IXGBE_READ_REG(hw, IXGBE_RTTDCS);
	reg |= IXGBE_RTTDCS_ARBDIS;
	IXGBE_WRITE_REG(hw, IXGBE_RTTDCS, reg);

	/* Enable DCB for Rx with 8 TCs */
	reg = IXGBE_READ_REG(hw, IXGBE_MRQC);
	switch (reg & IXGBE_MRQC_MRQE_MASK) {
	case 0:
	case IXGBE_MRQC_RT4TCEN:
		/* RSS disabled cases */
		reg = (reg & ~IXGBE_MRQC_MRQE_MASK) | IXGBE_MRQC_RT8TCEN;
		break;
	case IXGBE_MRQC_RSSEN:
	case IXGBE_MRQC_RTRSS4TCEN:
		/* RSS enabled cases */
		reg = (reg & ~IXGBE_MRQC_MRQE_MASK) | IXGBE_MRQC_RTRSS8TCEN;
		break;
	default:
		/* Unsupported value, assume stale data, overwrite no RSS */
		ASSERT(0);
		reg = (reg & ~IXGBE_MRQC_MRQE_MASK) | IXGBE_MRQC_RT8TCEN;
	}
	if (dcb_config->num_tcs.pg_tcs == 4) {
		/* Enable DCB for Rx with 4 TCs and VT Mode */
		reg = (reg & ~IXGBE_MRQC_MRQE_MASK) | IXGBE_MRQC_VMDQRT4TCEN;
	}
	IXGBE_WRITE_REG(hw, IXGBE_MRQC, reg);

	/* Enable DCB for Tx with 8 TCs */
	if (dcb_config->num_tcs.pg_tcs == 8)
		reg = IXGBE_MTQC_RT_ENA | IXGBE_MTQC_8TC_8TQ;
	else /* Enable DCB for Tx with 4 TCs and VT Mode */
		reg = IXGBE_MTQC_RT_ENA | IXGBE_MTQC_VT_ENA
		    | IXGBE_MTQC_4TC_4TQ;
	IXGBE_WRITE_REG(hw, IXGBE_MTQC, reg);

	/* Disable drop for all queues */
	for (q = 0; q < 128; q++) {
		IXGBE_WRITE_REG(hw, IXGBE_QDE, q << IXGBE_QDE_IDX_SHIFT);
	}

	/* Enable the Tx desc arbiter */
	reg = IXGBE_READ_REG(hw, IXGBE_RTTDCS);
	reg &= ~IXGBE_RTTDCS_ARBDIS;
	IXGBE_WRITE_REG(hw, IXGBE_RTTDCS, reg);

	/* Enable Security TX Buffer IFG for DCB */
	reg = IXGBE_READ_REG(hw, IXGBE_SECTXMINIFG);
	reg |= IXGBE_SECTX_DCB;
	IXGBE_WRITE_REG(hw, IXGBE_SECTXMINIFG, reg);

	return (IXGBE_SUCCESS);
}

/*
 * ixgbe_dcb_hw_config_82599 - Configure and enable DCB
 * @hw: pointer to hardware structure
 * @dcb_config: pointer to ixgbe_dcb_config structure
 *
 * Configure dcb settings and enable dcb mode.
 */
s32
ixgbe_dcb_hw_config_82599(struct ixgbe_hw *hw,
    struct ixgbe_dcb_config *dcb_config)
{

	(void) ixgbe_dcb_config_packet_buffers_82599(hw, dcb_config);
	(void) ixgbe_dcb_config_82599(hw, dcb_config);
	(void) ixgbe_dcb_config_rx_arbiter_82599(hw, dcb_config);
	(void) ixgbe_dcb_config_tx_desc_arbiter_82599(hw, dcb_config);
	(void) ixgbe_dcb_config_tx_data_arbiter_82599(hw, dcb_config);
	(void) ixgbe_dcb_config_pfc_82599(hw, dcb_config);
	(void) ixgbe_dcb_config_tc_stats_82599(hw);

	return (IXGBE_SUCCESS);
}
