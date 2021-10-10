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

#ifndef	_SYS_SXGE_SXGE_FMA_H
#define	_SYS_SXGE_SXGE_FMA_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/ddi.h>

#define	ERNAME_DETAILED_ERR_TYPE	"detailed error type"
#define	ERNAME_ERR_PORTN		"port number"
#define	ERNAME_ERR_DCHAN		"dma channel number"
#define	ERNAME_TCAM_ERR_LOG		"tcam error log"
#define	ERNAME_VLANTAB_ERR_LOG		"vlan table error log"
#define	ERNAME_HASHTAB_ERR_LOG		"hash table error log"
#define	ERNAME_HASHT_LOOKUP_ERR_LOG0	"hash table lookup error log0"
#define	ERNAME_HASHT_LOOKUP_ERR_LOG1	"hash table lookup error log1"
#define	ERNAME_RDC_PAR_ERR_LOG		"rdc parity error log"
#define	ERNAME_DFIFO_RD_PTR		"dfifo read pointer"
#define	ERNAME_DFIFO_ENTRY		"dfifo entry"
#define	ERNAME_DFIFO_SYNDROME		"dfifo syndrome"
#define	ERNAME_PFIFO_ENTRY		"pfifo entry"
#define	ERNAME_CFIFO_PORT_NUM		"cfifo port number"
#define	ERNAME_RDC_ERR_TYPE		"completion error type"
#define	ERNAME_TDC_ERR_LOG0		"tdc error log0"
#define	ERNAME_TDC_ERR_LOG1		"tdc error log1"

#define	EREPORT_FM_ID_SHIFT		16
#define	EREPORT_FM_ID_MASK		0xFF
#define	EREPORT_INDEX_MASK		0xFF
#define	SXGE_FM_EREPORT_UNKNOWN		0

#define	FM_SW_ID			0xFF
#define	FM_MAC_ID			0x1
#define	FM_TXVMAC_ID			0x2
#define	FM_RXVMAC_ID			0x3
#define	FM_TDC_ID			0x4
#define	FM_RDC_ID			0x5
#define	FM_PFC_ID			0x6
#define	FM_PCIE_ID			0x7

/* Error Injection Utility: #define */
#define	SW_BLK_ID			0xFF
#define	MAC_BLK_ID			0x1
#define	TXVMAC_BLK_ID			0x2
#define	RXVMAC_BLK_ID			0x3
#define	TDC_BLK_ID			0x4
#define	RDC_BLK_ID			0x5
#define	PFC_BLK_ID			0x6
#define	PCIE_BLK_ID			0x7

#define	SXGE_INJECT_ERR			(SXGE_IOC|40)
#define	STR_CTL				0x0000000000000800ULL

typedef	uint32_t sxge_fm_ereport_id_t;

typedef	struct _sxge_fm_ereport_attr {
	uint32_t		index;
	char			*str;
	char			*eclass;
	ddi_fault_impact_t	impact;
} sxge_fm_ereport_attr_t;

/* General MAC ereports */
typedef	enum {
	SXGE_FM_EREPORT_XMAC_LINK_DOWN = (FM_MAC_ID << EREPORT_FM_ID_SHIFT),
	SXGE_FM_EREPORT_XMAC_TX_LINK_FAULT,
	SXGE_FM_EREPORT_XMAC_RX_LINK_FAULT,
	SXGE_FM_EREPORT_MAC_LINK_DOWN,
	SXGE_FM_EREPORT_MAC_REMOTE_FAULT
} sxge_fm_ereport_pcs_t;

/* PFC ereports */
typedef	enum {
	SXGE_FM_EREPORT_PFC_TCAM_ERR = (FM_PFC_ID << EREPORT_FM_ID_SHIFT),
	SXGE_FM_EREPORT_PFC_VLAN_ERR,
	SXGE_FM_EREPORT_PFC_HASHT_LOOKUP_ERR,
	SXGE_FM_EREPORT_PFC_ACCESS_FAIL
} sxge_fm_ereport_pfc_t;

/* RDC ereports */
typedef	enum {
	SXGE_FM_EREPORT_RDC_RBR_RING_ERR = (FM_RDC_ID << EREPORT_FM_ID_SHIFT),
	SXGE_FM_EREPORT_RDC_RCR_RING_ERR,
	SXGE_FM_EREPORT_RDC_RBR_PRE_PAR,
	SXGE_FM_EREPORT_RDC_RCR_SHA_PAR,
	SXGE_FM_EREPORT_RDC_RCR_ACK_ERR,
	SXGE_FM_EREPORT_RDC_RSP_DAT_ERR,
	SXGE_FM_EREPORT_RDC_RBR_TMOUT,
	SXGE_FM_EREPORT_RDC_REQUEST_REJECT,
	SXGE_FM_EREPORT_RDC_SHADOW_FULL,
	SXGE_FM_EREPORT_RDC_FIFO_ERR,
	SXGE_FM_EREPORT_RDC_RESET_FAIL
} sxge_fm_ereport_rdc_t;

typedef enum {
	SXGE_FM_EREPORT_RXVMAC_LINKDOWN =
		(FM_RXVMAC_ID << EREPORT_FM_ID_SHIFT),
	SXGE_FM_EREPORT_RXVMAC_RESET_FAIL
} sxge_fm_ereport_rxvmac_t;

typedef	enum {
	SXGE_FM_EREPORT_TDC_PKT_PRT_ERR =
				(FM_TDC_ID << EREPORT_FM_ID_SHIFT),
	SXGE_FM_EREPORT_TDC_CONF_PART_ERR,
	SXGE_FM_EREPORT_TDC_NACK_PKT_RD,
	SXGE_FM_EREPORT_TDC_NACK_PREF,
	SXGE_FM_EREPORT_TDC_PREF_BUF_PAR_ERR,
	SXGE_FM_EREPORT_TDC_TX_RING_OFLOW,
	SXGE_FM_EREPORT_TDC_PKT_SIZE_ERR,
	SXGE_FM_EREPORT_TDC_MBOX_ERR,
	SXGE_FM_EREPORT_TDC_DESC_NUM_PTR_ERR,
	SXGE_FM_EREPORT_TDC_DESC_LENGTH_ERR,
	SXGE_FM_EREPORT_TDC_PREMATURE_SOP_ERR,
	SXGE_FM_EREPORT_TDC_SOP_BIT_ERR,
	SXGE_FM_EREPORT_TDC_REJECT_RESP_ERR,
	SXGE_FM_EREPORT_TDC_RESET_FAIL
} sxge_fm_ereport_attr_tdc_t;

typedef	enum {
	SXGE_FM_EREPORT_TXVMAC_UNDERFLOW =
				(FM_TXVMAC_ID << EREPORT_FM_ID_SHIFT),
	SXGE_FM_EREPORT_TXVMAC_OVERFLOW,
	SXGE_FM_EREPORT_TXVMAC_TXFIFO_XFR_ERR,
	SXGE_FM_EREPORT_TXVMAC_MAX_PKT_ERR,
	SXGE_FM_EREPORT_TXVMAC_RESET_FAIL
} sxge_fm_ereport_attr_txvmac_t;

typedef	enum {
	SXGE_FM_EREPORT_SW_INVALID_PORT_NUM = (FM_SW_ID << EREPORT_FM_ID_SHIFT),
	SXGE_FM_EREPORT_SW_INVALID_CHAN_NUM,
	SXGE_FM_EREPORT_SW_INVALID_PARAM
} sxge_fm_ereport_sw_t;

/* Error Injection Utility: typedef */
typedef	enum {loop, noloop} getmsgmode_t;

typedef struct _err_inject {
	uint8_t		blk_id;
	uint8_t		chan;
	uint32_t	err_id;
	uint32_t	control;
} err_inject_t;

typedef	struct _err_info {
	char		*str;
	uint32_t	id;
} err_info_t;

#define	SXGE_FM_EREPORT_UNKNOWN			0
#define	SXGE_FM_EREPORT_UNKNOWN_NAME		""

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SXGE_SXGE_FMA_H */
