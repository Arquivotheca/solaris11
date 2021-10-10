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
/* IntelVersion: 1.6 scm_011511_003853 */

#ifndef _IXGBE_DCB_82598_H
#define	_IXGBE_DCB_82598_H

/* DCB register definitions */

#define	IXGBE_DPMCS_MTSOS_SHIFT	16
#define	IXGBE_DPMCS_TDPAC	0x00000001 /* 0 Round Robin */
					/* 1 DFP - Deficit Fixed Priority */
#define	IXGBE_DPMCS_TRM		0x00000010 /* Transmit Recycle Mode */
#define	IXGBE_DPMCS_ARBDIS	0x00000040 /* DCB arbiter disable */
#define	IXGBE_DPMCS_TSOEF	0x00080000 /* TSO Expand Factor: 0=x4, 1=x2 */

#define	IXGBE_RUPPBMR_MQA	0x80000000 /* Enable UP to queue mapping */

#define	IXGBE_RT2CR_MCL_SHIFT	12 /* Offset to Max Credit Limit setting */
#define	IXGBE_RT2CR_LSP		0x80000000 /* LSP enable bit */

#define	IXGBE_RDRXCTL_MPBEN    0x00000010 /* DMA config for multiple packet */
						/* buffers enable */
#define	IXGBE_RDRXCTL_MCEN    0x00000040 /* DMA config for multiple cores */
						/* (RSS) enable */

#define	IXGBE_TDTQ2TCCR_MCL_SHIFT	12
#define	IXGBE_TDTQ2TCCR_BWG_SHIFT	9
#define	IXGBE_TDTQ2TCCR_GSP	0x40000000
#define	IXGBE_TDTQ2TCCR_LSP	0x80000000

#define	IXGBE_TDPT2TCCR_MCL_SHIFT	12
#define	IXGBE_TDPT2TCCR_BWG_SHIFT	9
#define	IXGBE_TDPT2TCCR_GSP	0x40000000
#define	IXGBE_TDPT2TCCR_LSP	0x80000000

#define	IXGBE_PDPMCS_TPPAC	0x00000020 /* 0 Round Robin */
					/* 1 DFP - Deficit Fixed Priority */
#define	IXGBE_PDPMCS_ARBDIS	0x00000040 /* Arbiter disable */
#define	IXGBE_PDPMCS_TRM	0x00000100 /* Transmit Recycle Mode enable */

#define	IXGBE_DTXCTL_ENDBUBD    0x00000004 /* Enable DBU buffer division */

#define	IXGBE_TXPBSIZE_40KB	0x0000A000 /* 40KB Packet Buffer */
#define	IXGBE_RXPBSIZE_48KB	0x0000C000 /* 48KB Packet Buffer */
#define	IXGBE_RXPBSIZE_64KB	0x00010000 /* 64KB Packet Buffer */
#define	IXGBE_RXPBSIZE_80KB	0x00014000 /* 80KB Packet Buffer */

/* DCB hardware-specific driver APIs */

/* DCB PFC functions */
s32 ixgbe_dcb_config_pfc_82598(struct ixgbe_hw *hw,
    struct ixgbe_dcb_config *dcb_config);
s32 ixgbe_dcb_get_pfc_stats_82598(struct ixgbe_hw *hw,
    struct ixgbe_hw_stats *stats, u8 tc_count);

/* DCB traffic class stats */
s32 ixgbe_dcb_config_tc_stats_82598(struct ixgbe_hw *hw);
s32 ixgbe_dcb_get_tc_stats_82598(struct ixgbe_hw *hw,
    struct ixgbe_hw_stats *stats, u8 tc_count);

/* DCB config arbiters */
s32 ixgbe_dcb_config_tx_desc_arbiter_82598(struct ixgbe_hw *hw,
    struct ixgbe_dcb_config *dcb_config);
s32 ixgbe_dcb_config_tx_data_arbiter_82598(struct ixgbe_hw *hw,
    struct ixgbe_dcb_config *dcb_config);
s32 ixgbe_dcb_config_rx_arbiter_82598(struct ixgbe_hw *hw,
    struct ixgbe_dcb_config *dcb_config);

/* DCB hw initialization */
s32 ixgbe_dcb_hw_config_82598(struct ixgbe_hw *hw,
    struct ixgbe_dcb_config *config);

#endif /* _IXGBE_DCB_82598_H */
