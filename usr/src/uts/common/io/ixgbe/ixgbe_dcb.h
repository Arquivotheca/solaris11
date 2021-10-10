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
/* IntelVersion: 1.29 scm_011511_003853 */

#ifndef _IXGBE_DCB_H
#define	_IXGBE_DCB_H

#include "ixgbe_type.h"

/* DCB data structures */

#define	IXGBE_MAX_PACKET_BUFFERS	8
#define	MAX_USER_PRIORITY	8
#define	MAX_TRAFFIC_CLASS	8
#define	MAX_BW_GROUP		8
#define	BW_PERCENT		100

#define	DCB_TX_CONFIG		0
#define	DCB_RX_CONFIG		1

/* DCB error Codes */
#define	DCB_SUCCESS		0
#define	DCB_ERR_CONFIG		-1
#define	DCB_ERR_PARAM		-2

/* Transmit and receive Errors */
/* Error in bandwidth group allocation */
#define	DCB_ERR_BW_GROUP	-3
/* Error in traffic class bandwidth allocation */
#define	DCB_ERR_TC_BW		-4
/* Traffic class has both link strict and group strict enabled */
#define	DCB_ERR_LS_GS		-5
/* Link strict traffic class has non zero bandwidth */
#define	DCB_ERR_LS_BW_NONZERO	-6
/* Link strict bandwidth group has non zero bandwidth */
#define	DCB_ERR_LS_BWG_NONZERO	-7
/*  Traffic class has zero bandwidth */
#define	DCB_ERR_TC_BW_ZERO	-8

#define	DCB_NOT_IMPLEMENTED	0x7FFFFFFF

struct dcb_pfc_tc_debug {
	u8  tc;
	u8  pause_status;
	u64 pause_quanta;
};

enum strict_prio_type {
	prio_none = 0,
	prio_group,
	prio_link
};

/* DCB capability definitions */
#define	IXGBE_DCB_PG_SUPPORT	0x00000001
#define	IXGBE_DCB_PFC_SUPPORT	0x00000002
#define	IXGBE_DCB_BCN_SUPPORT	0x00000004
#define	IXGBE_DCB_UP2TC_SUPPORT	0x00000008
#define	IXGBE_DCB_GSP_SUPPORT	0x00000010

#define	IXGBE_DCB_8_TC_SUPPORT	0x80

struct dcb_support {
	/* DCB capabilities */
	u32 capabilities;

	/*
	 * Each bit represents a number of TCs configurable in the hw.
	 * If 8 traffic classes can be configured, the value is 0x80.
	 */
	u8  traffic_classes;
	u8  pfc_traffic_classes;
};

/* Traffic class bandwidth allocation per direction */
struct tc_bw_alloc {
	u8 bwg_id;		/* Bandwidth Group (BWG) ID */
	u8 bwg_percent;		/* % of BWG's bandwidth */
	u8 link_percent;	/* % of link bandwidth */
	u8 up_to_tc_bitmap;	/* User Priority to Traffic Class mapping */
	u16 data_credits_refill; /* Credit refill amount in 64B granularity */
	u16 data_credits_max; /* Max credits for a configured packet buffer */
				/* in 64B granularity. */
	enum strict_prio_type prio_type; /* Link or Group Strict Priority */
};

enum dcb_pfc_type {
	pfc_disabled = 0,
	pfc_enabled_full,
	pfc_enabled_tx,
	pfc_enabled_rx
};

/* Traffic class configuration */
struct tc_configuration {
	struct tc_bw_alloc path[2]; /* One each for Tx/Rx */
	enum dcb_pfc_type  dcb_pfc; /* Class based flow control setting */

	u16 desc_credits_max; /* For Tx Descriptor arbitration */
	u8 tc; /* Traffic class (TC) */
};

enum dcb_rx_pba_cfg {
	pba_equal,    /* PBA[0-7] each use 64KB FIFO */
	pba_80_48    /* PBA[0-3] each use 80KB, PBA[4-7] each use 48KB */
};

struct dcb_num_tcs {
	u8 pg_tcs;
	u8 pfc_tcs;
};

struct ixgbe_dcb_config {
	struct tc_configuration tc_config[MAX_TRAFFIC_CLASS];
	struct dcb_support support;
	struct dcb_num_tcs num_tcs;
	u8    bw_percentage[2][MAX_BW_GROUP]; /* One each for Tx/Rx */
	bool pfc_mode_enable;
	bool  round_robin_enable;

	enum dcb_rx_pba_cfg rx_pba_cfg;

	u32  dcb_cfg_version; /* Not used...OS-specific? */
	u32  link_speed; /* For bandwidth allocation validation purpose */
};

/* DCB driver APIs */

/* DCB rule checking function. */
s32 ixgbe_dcb_check_config(struct ixgbe_dcb_config *config);

/* DCB credits calculation */
s32 ixgbe_dcb_calculate_tc_credits(struct ixgbe_hw *hw,
    struct ixgbe_dcb_config *config, u32 max_frame_size, u8 direction);

/* DCB PFC functions */
s32 ixgbe_dcb_config_pfc(struct ixgbe_hw *hw,
    struct ixgbe_dcb_config *dcb_config);
s32 ixgbe_dcb_get_pfc_stats(struct ixgbe_hw *hw, struct ixgbe_hw_stats *stats,
    u8 tc_count);

/* DCB traffic class stats */
s32 ixgbe_dcb_config_tc_stats(struct ixgbe_hw *);
s32 ixgbe_dcb_get_tc_stats(struct ixgbe_hw *hw, struct ixgbe_hw_stats *stats,
    u8 tc_count);

/* DCB config arbiters */
s32 ixgbe_dcb_config_tx_desc_arbiter(struct ixgbe_hw *hw,
    struct ixgbe_dcb_config *dcb_config);
s32 ixgbe_dcb_config_tx_data_arbiter(struct ixgbe_hw *hw,
    struct ixgbe_dcb_config *dcb_config);
s32 ixgbe_dcb_config_rx_arbiter(struct ixgbe_hw *hw,
    struct ixgbe_dcb_config *dcb_config);

/* DCB hw initialization */
s32 ixgbe_dcb_hw_config(struct ixgbe_hw *hw, struct ixgbe_dcb_config *config);


/* DCB definitions for credit calculation */
#define	DCB_CREDIT_QUANTUM	64
#define	MAX_CREDIT_REFILL	200   /* 200 * 64B = 12800B */
#define	DCB_MAX_TSO_SIZE	(32 * 1024)
				/* MAX TSO packet size supported in DCB mode */
/* 513 for 32KB TSO packet */
#define	MINIMUM_CREDIT_FOR_TSO  ((DCB_MAX_TSO_SIZE / DCB_CREDIT_QUANTUM) + 1)
#define	MAX_CREDIT		(2 * MAX_CREDIT_REFILL)

#endif /* _IXGBE_DCB_H */
