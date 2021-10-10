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

#ifndef	_SYS_SXGE_RDC_H
#define	_SYS_SXGE_RDC_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * NIU: RDC/RxDMA HW Interfaces
 */
#if !defined(PBASE)
#define	PBASE (0)
#endif

#if 0
#define	HOST_VNI_BASE		(PBASE+0x00000)
#define	TXDMA_BASE		(PBASE+0x00000)
#define	RXDMA_BASE		(PBASE+0x00400)
#define	RXVMAC_BASE		(PBASE+0x08000)
#define	TXVMAC_BASE		(PBASE+0x08200)
#define	INTR_BASE		(PBASE+0x08400)
#define	SHARE_RESOURCE_BASE	(0xC0000)
#define	STAND_RESOURCE_BASE	(0xF0000)

#define	VNI_STEP		(0x10000)
#define	RXDMA_STEP		(0x02000)
#else
#if !defined(SXGE_RXDMA_BASE)
#define	SXGE_RXDMA_BASE		(0x00400)
#endif
#define	RXDMA_BASE		(PBASE+(SXGE_RXDMA_BASE))
#define	RXDMA_STEP		(0x02000)
#define	RXDMA_VNI_STEP		(0x10000)
#endif

#define	RDC_BASE(vni, rdc)	((vni * RXDMA_VNI_STEP) + (rdc * RXDMA_STEP))

#define	RDC_PG_HDL_REG		(RXDMA_BASE + 0x0000)
#define	RDC_CFG_REG		(RXDMA_BASE + 0x0008)
#define	RDC_RBR_CFG_REG		(RXDMA_BASE + 0x0010)
#define	RDC_RCR_CFG_REG		(RXDMA_BASE + 0x0018)
#define	RDC_MBX_CFG_REG		(RXDMA_BASE + 0x0020)
#define	RDC_RCR_TMR_REG		(RXDMA_BASE + 0x0028)
#define	RDC_MBX_UPD_REG		(RXDMA_BASE + 0x0030)
#define	RDC_KICK_REG		(RXDMA_BASE + 0x0038)
#define	RDC_ENT_MSK_REG		(RXDMA_BASE + 0x0040)
#define	RDC_PRE_ST_REG		(RXDMA_BASE + 0x0048)
#define	RDC_CTL_STAT_REG	(RXDMA_BASE + 0x0050)
#define	RDC_CTL_STAT_DBG_REG	(RXDMA_BASE + 0x0058)
#define	RDC_RCR_FLSH_REG	(RXDMA_BASE + 0x0060)
#define	RDC_PKT_CNT_DBG_REG	(RXDMA_BASE + 0x0068)
#define	RDC_DIS_CNT_DBG_REG	(RXDMA_BASE + 0x0070)
#define	RDC_PKT_CNT_REG		(RXDMA_BASE + 0x0078)
#define	RDC_DIS_CNT_REG		(RXDMA_BASE + 0x0080)
#define	RDC_ERR_LOG_REG		(RXDMA_BASE + 0x0088)

/* RDC_PG_HDL_REG : Receive DMA Page Handle */

#define	RDC_PG_HDL_CTL_SH	0			/* bits 19:0 */
#define	RDC_PG_HDL_CTL_MK	0x00000000000FFFFFULL
#define	RDC_PG_HDL_DATA_SH	32			/* bits 51:32 */
#define	RDC_PG_HDL_DATA_MK	0x00000000000FFFFFULL

typedef union _rdc_pg_hdl_t {
	uint64_t value;
	struct {
#if defined(_BIG_ENDIAN)
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t resv1:12;
			uint32_t data:20;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t data:20;
			uint32_t resv1:12;
#endif
		} hdw;
#endif
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t resv0:12;
			uint32_t ctl:20;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t ctl:20;
			uint32_t resv0:12;
#endif
		} ldw;
#if !defined(_BIG_ENDIAN)
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t resv1:12;
			uint32_t data:20;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t data:20;
			uint32_t resv1:12;
#endif
		} hdw;
#endif
	} bits;
} rdc_pg_hdl_t, *rdc_pg_hdl_pt;

/* RDC_CFG_REG : Receive DMA Config */

#define	RDC_CFG_VLD0_SH		0
#define	RDC_CFG_VLD0_MK		0x0000000000000001ULL
#define	RDC_CFG_BUFSZ0_SH	1
#define	RDC_CFG_BUFSZ0_MK	0x0000000000000003ULL
#define	RDC_CFG_VLD1_SH		3
#define	RDC_CFG_VLD1_MK		0x0000000000000001ULL
#define	RDC_CFG_BUFSZ1_SH	4
#define	RDC_CFG_BUFSZ1_MK	0x0000000000000003ULL
#define	RDC_CFG_VLD2_SH		6
#define	RDC_CFG_VLD2_MK		0x0000000000000001ULL
#define	RDC_CFG_BUFSZ2_SH	7
#define	RDC_CFG_BUFSZ2_MK	0x0000000000000003ULL
#define	RDC_CFG_BLKSZ_SH	9
#define	RDC_CFG_BLKSZ_MK	0x0000000000000003ULL
#define	RDC_CFG_HDRSZ_SH	11
#define	RDC_CFG_HDRSZ_MK	0x0000000000000003ULL
#define	RDC_CFG_OFF_SH		13
#define	RDC_CFG_OFF_MK		0x0000000000000007ULL
#define	RDC_CFG_PAD_SH		16
#define	RDC_CFG_PAD_MK		0x0000000000000007ULL

typedef union _rdc_cfg_t {
	uint64_t value;
	struct {
#if defined(_BIG_ENDIAN)
		uint32_t hdw;
#endif
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t resv:13;
			uint32_t pad:3;
			uint32_t off:3;
			uint32_t hdrsz:2;
			uint32_t blksz:2;
			uint32_t bufsz2:2;
			uint32_t vld2:1;
			uint32_t bufsz1:2;
			uint32_t vld1:1;
			uint32_t bufsz0:2;
			uint32_t vld0:1;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t vld0:1;
			uint32_t bufsz0:2;
			uint32_t vld1:1;
			uint32_t bufsz1:2;
			uint32_t vld2:1;
			uint32_t bufsz2:2;
			uint32_t blksz:2;
			uint32_t hdrsz:2;
			uint32_t off:3;
			uint32_t pad:3;
			uint32_t resv:13;
#endif
		} ldw;
#if !defined(_BIG_ENDIAN)
		uint32_t hdw;
#endif
	} bits;
} rdc_cfg_t, *rdc_cfg_pt;


/* RDC_RBR_CFG_REG : Receive DMA RBR Config */

#define	RDC_RBR_CFG_START_SH	0
#define	RDC_RBR_CFG_START_MK	0x00000FFFFFFFFFFFULL
#define	RDC_RBR_CFG_LEN_SH	48
#define	RDC_RBR_CFG_LEN_MK	0x00000000000001FFULL

typedef union _rdc_rbr_cfg_t {
	uint64_t value;
	struct {
#if defined(_BIG_ENDIAN)
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t resv1:7;
			uint32_t len:9;
			uint32_t resv0:4;
			uint32_t start:12;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t start:12;
			uint32_t resv0:4;
			uint32_t len:9;
			uint32_t resv1:7;
#endif
		} hdw;
#endif
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t start:26;
			uint32_t resv0:6;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t resv0:6;
			uint32_t start:26;
#endif
		} ldw;
#if !defined(_BIG_ENDIAN)
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t resv1:7;
			uint32_t len:9;
			uint32_t resv0:4;
			uint32_t start:12;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t start:12;
			uint32_t resv0:4;
			uint32_t len:9;
			uint32_t resv1:7;
#endif
		} hdw;
#endif
	} bits;
} rdc_rbr_cfg_t, *rdc_rbr_cfg_pt;

/* RDC_RCR_CFG_REG : Receive DMA RCR Config */

#define	RDC_RCR_CFG_START_SH	0
#define	RDC_RCR_CFG_START_MK	0x00000FFFFFFFFFFFULL
#define	RDC_RCR_CFG_LEN_SH	48
#define	RDC_RCR_CFG_LEN_MK	0x000000000000FFFFULL

typedef union _rdc_rcr_cfg_t {
	uint64_t value;
	struct {
#if defined(_BIG_ENDIAN)
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t len:16;
			uint32_t resv0:4;
			uint32_t start:12;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t start:12;
			uint32_t resv0:4;
			uint32_t len:16;
#endif
		} hdw;
#endif
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t start:26;
			uint32_t resv0:6;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t resv0:6;
			uint32_t start:26;
#endif
		} ldw;
#if !defined(_BIG_ENDIAN)
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t len:16;
			uint32_t resv0:4;
			uint32_t start:12;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t start:12;
			uint32_t resv0:4;
			uint32_t len:16;
#endif
		} hdw;
#endif
	} bits;
} rdc_rcr_cfg_t, *rdc_rcr_cfg_pt;

/* RDC_MBX_CFG_REG : Receive DMA Mailbox Address Config */

#define	RDC_MBX_CFG_START_SH	0
#define	RDC_MBX_CFG_START_MK	0x00000FFFFFFFFFFFULL

typedef union _rdc_mbx_cfg_t {
	uint64_t value;
	struct {
#if defined(_BIG_ENDIAN)
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t resv:20;
			uint32_t start:12;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t start:12;
			uint32_t resv:20;
#endif
		} hdw;
#endif
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t start:26;
			uint32_t resv:6;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t resv:6;
			uint32_t start:26;
#endif
		} ldw;
#if !defined(_BIG_ENDIAN)
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t resv:20;
			uint32_t start:12;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t start:12;
			uint32_t resv:20;
#endif
		} hdw;
#endif
	} bits;
} rdc_mbx_cfg_t, *rdc_mbx_cfg_pt;

/* RDC_RCR_TMR_REG : Receive DMA RCR Timer */

#define	RDC_RCR_TMR_TMOUT_SH	0
#define	RDC_RCR_TMR_TMOUT_MK	0x000000000000003FULL
#define	RDC_RCR_TMR_ENTMOUT_SH	6
#define	RDC_RCR_TMR_ENTMOUT_MK	0x0000000000000001ULL
#define	RDC_RCR_TMR_ENPTHRSH_SH	15
#define	RDC_RCR_TMR_ENPTHRSH_MK	0x0000000000000001ULL
#define	RDC_RCR_TMR_PTHRSH_SH	16
#define	RDC_RCR_TMR_PTHRSH_MK	0x000000000000FFFFULL

typedef union _rdc_rcr_tmr_t {
	uint64_t value;
	struct {
#if defined(_BIG_ENDIAN)
		uint32_t hdw;
#endif
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t pthres:16;
			uint32_t enpthres:1;
			uint32_t resv:8;
			uint32_t entimeout:1;
			uint32_t timeout:6;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t timeout:6;
			uint32_t entimeout:1;
			uint32_t resv:8;
			uint32_t enpthres:1;
			uint32_t pthres:16;
#endif
		} ldw;
#if !defined(_BIG_ENDIAN)
		uint32_t hdw;
#endif
	} bits;
} rdc_rcr_tmr_t, *rdc_rcr_tmr_pt;

/* RDC_MBX_UPD_REG : Receive Mailbox update config */

#define	RDC_MBX_PTHRES_SH	0
#define	RDC_MBX_PTHRES_MK	0x000000000000FFFFULL
#define	RDC_MBX_ENABLE_SH	16
#define	RDC_MBX_ENABLE_MK	0x0000000000000001ULL

typedef union _rdc_mbx_upd_t {
	uint64_t value;
	struct {
#if defined(_BIG_ENDIAN)
		uint32_t hdw;
#endif
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t resv:15;
			uint32_t enable:1;
			uint32_t pthres:16;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t pthres:16;
			uint32_t enable:1;
			uint32_t resv:15;
#endif
		} ldw;
#if !defined(_BIG_ENDIAN)
		uint32_t hdw;
#endif
	} bits;
} rdc_mbx_upd_t, *rdc_mbx_upd_pt;

/* RDC_KICK_REG : Receive Mailbox update config */

#define	RDC_KICK_RBRTAIL_SH	0
#define	RDC_KICK_RBRTAIL_MK	0x0000000000000FFFULL
#define	RDC_KICK_RBRTWRAP_SH	12
#define	RDC_KICK_RBRTWRAP_MK	0x0000000000000001ULL
#define	RDC_KICK_RBRTVLD_SH	13
#define	RDC_KICK_RBRTVLD_MK	0x0000000000000001ULL
#define	RDC_KICK_RCRTAIL_SH	14
#define	RDC_KICK_RCRTAIL_MK	0x000000000000FFFFULL
#define	RDC_KICK_RCRTWRAP_SH	30
#define	RDC_KICK_RCRTWRAP_MK	0x0000000000000001ULL
#define	RDC_KICK_RCRTVLD_SH	31
#define	RDC_KICK_RCRTVLD_MK	0x0000000000000001ULL

typedef union _rdc_kick_t {
	uint64_t value;
	struct {
#if defined(_BIG_ENDIAN)
		uint32_t hdw;
#endif
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t rcrhvld:1;
			uint32_t rcrhwrap:1;
			uint32_t rcrhead:16;
			uint32_t rbrtvld:1;
			uint32_t rbrtwrap:1;
			uint32_t rbrtail:12;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t rbrtail:12;
			uint32_t rbrtwrap:1;
			uint32_t rbrtvld:1;
			uint32_t rcrhead:16;
			uint32_t rcrhwrap:1;
			uint32_t rcrhvld:1;
#endif
		} ldw;
#if !defined(_BIG_ENDIAN)
		uint32_t hdw;
#endif
	} bits;
} rdc_kick_t, *rdc_kick_pt;

/* RDC_ENT_MSK_REG : Receive Event Mask */

#define	RDC_ENT_RBROVRFLW_SH	0
#define	RDC_ENT_RBROVRFLW_MK	0x0000000000000001ULL
#define	RDC_ENT_RCRUNDFLW_SH	1
#define	RDC_ENT_RCRUNDFLW_MK	0x0000000000000001ULL
#define	RDC_ENT_RBRPREPAR_SH	2
#define	RDC_ENT_RBRPREPAR_MK	0x0000000000000001ULL
#define	RDC_ENT_RCRSHAPAR_SH	3
#define	RDC_ENT_RCRSHAPAR_MK	0x0000000000000001ULL
#define	RDC_ENT_RCRACKERR_SH	4
#define	RDC_ENT_RCRACKERR_MK	0x0000000000000001ULL
#define	RDC_ENT_RSPDATERR_SH	5
#define	RDC_ENT_RSPDATERR_MK	0x0000000000000001ULL
#define	RDC_ENT_RBRTMOUT_SH	6
#define	RDC_ENT_RBRTMOUT_MK	0x0000000000000001ULL
#define	RDC_ENT_RBRREQREJ_SH	7
#define	RDC_ENT_RBRREQREJ_MK	0x0000000000000001ULL
#define	RDC_ENT_RCRSHAFULL_SH	8
#define	RDC_ENT_RCRSHAFULL_MK	0x0000000000000001ULL
#define	RDC_ENT_RDCFIFOERR_SH	9
#define	RDC_ENT_RDCFIFOERR_MK	0x0000000000000001ULL
#define	RDC_ENT_RCRTMOUT_SH	13
#define	RDC_ENT_RCRTMOUT_MK	0x0000000000000001ULL
#define	RDC_ENT_RCRTHRES_SH	14
#define	RDC_ENT_RCRTHRES_MK	0x0000000000000001ULL
#define	RDC_ENT_MBXTHRES_SH	15
#define	RDC_ENT_MBXTHRES_MK	0x0000000000000001ULL

typedef union _rdc_ent_msk_t {
	uint64_t value;
	struct {
#if defined(_BIG_ENDIAN)
		uint32_t hdw;
#endif
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t resv1:16;
			uint32_t mbxthres:1;
			uint32_t rcrthres:1;
			uint32_t rcrtmout:1;
			uint32_t resv0:3;
			uint32_t rdcfifoerr:1;
			uint32_t rcrshafull:1;
			uint32_t rbrreqrej:1;
			uint32_t rbrtmout:1;
			uint32_t rspdaterr:1;
			uint32_t rcrackerr:1;
			uint32_t rcrshapar:1;
			uint32_t rbrprepar:1;
			uint32_t rcrundflw:1;
			uint32_t rbrovrflw:1;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t rbrovrflw:1;
			uint32_t rcrundflw:1;
			uint32_t rbrprepar:1;
			uint32_t rcrshapar:1;
			uint32_t rcrackerr:1;
			uint32_t rspdaterr:1;
			uint32_t rbrtmout:1;
			uint32_t rbrreqrej:1;
			uint32_t rcrshafull:1;
			uint32_t rdcfifoerr:1;
			uint32_t resv0:3;
			uint32_t rcrtmout:1;
			uint32_t rcrthres:1;
			uint32_t mbxthres:1;
			uint32_t resv1:16;
#endif
		} ldw;
#if !defined(_BIG_ENDIAN)
		uint32_t hdw;
#endif
	} bits;
} rdc_ent_msk_t, *rdc_ent_msk_pt;

/* RDC_PRE_ST_REG : Rx Prefetch State */

#define	RDC_PRE_SHAHD_SH	0
#define	RDC_PRE_SHAHD_MK	0x0000000000003FFFULL

typedef union _rdc_pre_st_t {
	uint64_t value;
	struct {
#if defined(_BIG_ENDIAN)
		uint32_t hdw;
#endif
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t resv:18;
			uint32_t shadowhd:14;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t shadowhd:14;
			uint32_t resv:18;
#endif
		} ldw;
#if !defined(_BIG_ENDIAN)
		uint32_t hdw;
#endif
	} bits;
} rdc_pre_st_t, *rdc_pre_st_pt;

/* RDC_CTL_STAT_REG : Rx Control Status */

#define	RDC_CTL_STAT_RBROVRFLW_SH	0
#define	RDC_CTL_STAT_RBROVRFLW_MK	0x0000000000000001ULL
#define	RDC_CTL_STAT_RCRUNDFLW_SH	1
#define	RDC_CTL_STAT_RCRUNDFLW_MK	0x0000000000000001ULL
#define	RDC_CTL_STAT_RBRPREPAR_SH	2
#define	RDC_CTL_STAT_RBRPREPAR_MK	0x0000000000000001ULL
#define	RDC_CTL_STAT_RCRSHAPAR_SH	3
#define	RDC_CTL_STAT_RCRSHAPAR_MK	0x0000000000000001ULL
#define	RDC_CTL_STAT_RCRACKERR_SH	4
#define	RDC_CTL_STAT_RCRACKERR_MK	0x0000000000000001ULL
#define	RDC_CTL_STAT_RSPDATERR_SH	5
#define	RDC_CTL_STAT_RSPDATERR_MK	0x0000000000000001ULL
#define	RDC_CTL_STAT_RBRTMOUT_SH	6
#define	RDC_CTL_STAT_RBRTMOUT_MK	0x0000000000000001ULL
#define	RDC_CTL_STAT_RBRREQREJ_SH	7
#define	RDC_CTL_STAT_RBRREQREJ_MK	0x0000000000000001ULL
#define	RDC_CTL_STAT_RCRSHAFULL_SH	8
#define	RDC_CTL_STAT_RCRSHAFULL_MK	0x0000000000000001ULL
#define	RDC_CTL_STAT_RDCFIFOERR_SH	9
#define	RDC_CTL_STAT_RDCFIFOERR_MK	0x0000000000000001ULL
#define	RDC_CTL_STAT_RCRTMOUT_SH	13
#define	RDC_CTL_STAT_RCRTMOUT_MK	0x0000000000000001ULL
#define	RDC_CTL_STAT_RCRTHRES_SH	14
#define	RDC_CTL_STAT_RCRTHRES_MK	0x0000000000000001ULL
#define	RDC_CTL_STAT_MBXTHRES_SH	15
#define	RDC_CTL_STAT_MBXTHRES_MK	0x0000000000000001ULL
#define	RDC_CTL_STAT_SNGSTATE_SH	27
#define	RDC_CTL_STAT_SNGSTATE_MK	0x0000000000000001ULL
#define	RDC_CTL_STAT_STOPNGO_SH		28
#define	RDC_CTL_STAT_STOPNGO_MK		0x0000000000000001ULL
#define	RDC_CTL_STAT_MBXEN_SH		29
#define	RDC_CTL_STAT_MBXEN_MK		0x0000000000000001ULL
#define	RDC_CTL_STAT_RSTSTATE_SH	30
#define	RDC_CTL_STAT_RSTSTATE_MK	0x0000000000000001ULL
#define	RDC_CTL_STAT_RST_SH		31
#define	RDC_CTL_STAT_RST_MK		0x0000000000000001ULL

#define	RDC_CTL_STAT_RBRHEAD_SH		32
#define	RDC_CTL_STAT_RBRHEAD_MK		0x0000000000000FFFULL
#define	RDC_CTL_STAT_RBRHWRAP_SH 	44
#define	RDC_CTL_STAT_RBRHWRAP_MK 	0x0000000000000001ULL
#define	RDC_CTL_STAT_RCRTAIL_SH		46
#define	RDC_CTL_STAT_RCRTAIL_MK		0x000000000000FFFFULL
#define	RDC_CTL_STAT_RCRTWRAP_SH 	62
#define	RDC_CTL_STAT_RCRTWRAP_MK 	0x0000000000000001ULL

typedef union _rdc_ctl_stat_t {
	uint64_t value;
	struct {
#if defined(_BIG_ENDIAN)
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t resv0:1;
			uint32_t rcrtwrap:1;
			uint32_t rcrtail:16;
			uint32_t resv1:1;
			uint32_t rbrhwrap:1;
			uint32_t rbrhead:12;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t rbrhead:12;
			uint32_t rbrhwrap:1;
			uint32_t resv1:1;
			uint32_t rcrtail:16;
			uint32_t rcrtwrap:1;
			uint32_t resv0:1;
#endif
		} hdw;
#endif
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t rst:1;
			uint32_t rststate:1;
			uint32_t mbxen:1;
			uint32_t stopngo:1;
			uint32_t sngstate:1;
			uint32_t resv0:11;
			uint32_t mbxthres:1;
			uint32_t rcrthres:1;
			uint32_t rcrtmout:1;
			uint32_t pkt_count_oflow:1;
			uint32_t drop_count_oflow:1;
			uint32_t rbr_empty:1;
			uint32_t rdcfifoerr:1;
			uint32_t rcrshafull:1;
			uint32_t rbrreqrej:1;
			uint32_t rbrtmout:1;
			uint32_t rspdaterr:1;
			uint32_t rcrackerr:1;
			uint32_t rcrshapar:1;
			uint32_t rcrprepar:1;
			uint32_t rcrundflw:1;
			uint32_t rbrovrflw:1;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t rbrovrflw:1;
			uint32_t rcrundflw:1;
			uint32_t rcrprepar:1;
			uint32_t rcrshapar:1;
			uint32_t rcrackerr:1;
			uint32_t rspdaterr:1;
			uint32_t rbrtmout:1;
			uint32_t rbrreqrej:1;
			uint32_t rcrshafull:1;
			uint32_t rdcfifoerr:1;
			uint32_t rbr_empty:1;
			uint32_t drop_count_oflow:1;
			uint32_t pkt_count_oflow:1;
			uint32_t rcrtmout:1;
			uint32_t rcrthres:1;
			uint32_t mbxthres:1;
			uint32_t resv0:11;
			uint32_t sngstate:1;
			uint32_t stopngo:1;
			uint32_t mbxen:1;
			uint32_t rststate:1;
			uint32_t rst:1;
#endif
		} ldw;
#if !defined(_BIG_ENDIAN)
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t resv0:1;
			uint32_t rcrtwrap:1;
			uint32_t rcrtail:16;
			uint32_t resv1:1;
			uint32_t rbrhwrap:1;
			uint32_t rbrhead:12;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t rbrhead:12;
			uint32_t rbrhwrap:1;
			uint32_t resv1:1;
			uint32_t rcrtail:16;
			uint32_t rcrtwrap:1;
			uint32_t resv0:1;
#endif
		} hdw;
#endif
	} bits;
} rdc_ctl_stat_t, *rdc_ctl_stat_pt;


/* RDC_CTL_STAT_DBG_REG	: Rx Control Status Debug  */

#define	RDC_CTL_STAT_DBG_RBROVRFLW_SH	0
#define	RDC_CTL_STAT_DBG_RBROVRFLW_MK	0x0000000000000001ULL
#define	RDC_CTL_STAT_DBG_RCRUNDFLW_SH	1
#define	RDC_CTL_STAT_DBG_RCRUNDFLW_MK	0x0000000000000001ULL
#define	RDC_CTL_STAT_DBG_RBRPREPAR_SH	2
#define	RDC_CTL_STAT_DBG_RBRPREPAR_MK	0x0000000000000001ULL
#define	RDC_CTL_STAT_DBG_RCRSHAPAR_SH	3
#define	RDC_CTL_STAT_DBG_RCRSHAPAR_MK	0x0000000000000001ULL
#define	RDC_CTL_STAT_DBG_RCRACKERR_SH	4
#define	RDC_CTL_STAT_DBG_RCRACKERR_MK	0x0000000000000001ULL
#define	RDC_CTL_STAT_DBG_RSPDATERR_SH	5
#define	RDC_CTL_STAT_DBG_RSPDATERR_MK	0x0000000000000001ULL
#define	RDC_CTL_STAT_DBG_RBRTMOUT_SH	6
#define	RDC_CTL_STAT_DBG_RBRTMOUT_MK	0x0000000000000001ULL
#define	RDC_CTL_STAT_DBG_RBRREQREJ_SH	7
#define	RDC_CTL_STAT_DBG_RBRREQREJ_MK	0x0000000000000001ULL
#define	RDC_CTL_STAT_DBG_RCRSHAFULL_SH	8
#define	RDC_CTL_STAT_DBG_RCRSHAFULL_MK	0x0000000000000001ULL
#define	RDC_CTL_STAT_DBG_RDCFIFOERR_SH	9
#define	RDC_CTL_STAT_DBG_RDCFIFOERR_MK	0x0000000000000001ULL
#define	RDC_CTL_STAT_DBG_RCRTMOUT_SH	13
#define	RDC_CTL_STAT_DBG_RCRTMOUT_MK	0x0000000000000001ULL
#define	RDC_CTL_STAT_DBG_RCRTHRES_SH	14
#define	RDC_CTL_STAT_DBG_RCRTHRES_MK	0x0000000000000001ULL
#define	RDC_CTL_STAT_DBG_MBXTHRES_SH	15
#define	RDC_CTL_STAT_DBG_MBXTHRES_MK	0x0000000000000001ULL

typedef union _rdc_ctl_stat_dbg_t {
	uint64_t value;
	struct {
#if defined(_BIG_ENDIAN)
		uint32_t hdw;
#endif
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t resv0:16;
			uint32_t mbxthres:1;
			uint32_t rcrthres:1;
			uint32_t rcrtmout:1;
			uint32_t resv1:3;
			uint32_t rdcfifoerr:1;
			uint32_t rcrshafull:1;
			uint32_t rbrreqrej:1;
			uint32_t rbrtmout:1;
			uint32_t rspdaterr:1;
			uint32_t rcrackerr:1;
			uint32_t rcrshapar:1;
			uint32_t rcrprepar:1;
			uint32_t rcrundflw:1;
			uint32_t rbrovrflw:1;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t rbrovrflw:1;
			uint32_t rcrundflw:1;
			uint32_t rcrprepar:1;
			uint32_t rcrshapar:1;
			uint32_t rcrackerr:1;
			uint32_t rspdaterr:1;
			uint32_t rbrtmout:1;
			uint32_t rbrreqrej:1;
			uint32_t rcrshafull:1;
			uint32_t rdcfifoerr:1;
			uint32_t resv1:3;
			uint32_t rcrtmout:1;
			uint32_t rcrthres:1;
			uint32_t mbxthres:1;
			uint32_t resv0:16;
#endif
		} ldw;
#if !defined(_BIG_ENDIAN)
		uint32_t hdw;
#endif
	} bits;
} rdc_ctl_stat_dbg_t, *rdc_ctl_stat_dbg_pt;

/* RDC_RCR_FLSH_REG	: Rx RCR Flush Debug  */

#define	RDC_RCR_FLSH_SH	0
#define	RDC_RCR_FLSH_MK	0x0000000000000001ULL

typedef union _rdc_rcr_flsh_t {
	uint64_t value;
	struct {
#if defined(_BIG_ENDIAN)
		uint32_t hdw;
#endif
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t resv:31;
			uint32_t flsh:1;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t flsh:1;
			uint32_t resv:31;
#endif
		} ldw;
#if !defined(_BIG_ENDIAN)
		uint32_t hdw;
#endif
	} bits;
} rdc_rcr_flsh_t, *rdc_rcr_flsh_pt;

/* RDC_PKT_CNT_REG : Rx Packet Count */

#define	RDC_PKT_CNT_SH	0
#define	RDC_PKT_CNT_MK	0x000000007FFFFFFFULL
#define	RDC_PKT_OFLW_SH	31
#define	RDC_PKT_OFLW_MK	0x0000000000000001ULL

typedef union _rdc_pkt_cnt_t {
	uint64_t value;
	struct {
#if defined(_BIG_ENDIAN)
		uint32_t hdw;
#endif
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t oflow:1;
			uint32_t count:31;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t count:31;
			uint32_t oflow:1;
#endif
		} ldw;
#if !defined(_BIG_ENDIAN)
		uint32_t hdw;
#endif
	} bits;
} rdc_pkt_cnt_t, *rdc_pkt_cnt_pt;

/* RDC_DIS_CNT_REG : Rx Discard Count */

#define	RDC_DIS_CNT_SH	0
#define	RDC_DIS_CNT_MK	0x000000007FFFFFFFULL
#define	RDC_DIS_OFLW_SH	31
#define	RDC_DIS_OFLW_MK	0x0000000000000001ULL

typedef union _rdc_disc_cnt_t {
	uint64_t value;
	struct {
#if defined(_BIG_ENDIAN)
		uint32_t hdw;
#endif
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t oflow:1;
			uint32_t count:31;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t count:31;
			uint32_t oflow:1;
#endif
		} ldw;
#if !defined(_BIG_ENDIAN)
		uint32_t hdw;
#endif
	} bits;
} rdc_disc_cnt_t, *rdc_disc_cnt_pt;

/* RDC_ERR_LOG_REG : Rx Error Log */

typedef union _rdc_err_log_t {
	uint64_t value;
	struct {
#if defined(_BIG_ENDIAN)
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t err:1;
			uint32_t merr:1;
			uint32_t errcode:3;
			uint32_t resv:15;
			uint32_t addr:12;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t addr:12;
			uint32_t resv:15;
			uint32_t errcode:3;
			uint32_t merr:1;
			uint32_t err:1;
#endif
		} hdw;
#endif
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t addr;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t addr;
#endif
		} ldw;
#if !defined(_BIG_ENDIAN)
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t err:1;
			uint32_t merr:1;
			uint32_t errcode:3;
			uint32_t resv:15;
			uint32_t addr:12;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t addr:12;
			uint32_t resv:15;
			uint32_t errcode:3;
			uint32_t merr:1;
			uint32_t err:1;
#endif
		} hdw;
#endif
	} bits;
} rdc_err_log_t, *rdc_err_log_pt;

#define	RBR_BKADDR_SHIFT		12

typedef union _rdc_rbr_desc_t {
	uint64_t value; /* blkaddr[43:12], index, rx-offset, flush */
	struct {
#if defined(_BIG_ENDIAN)
		uint32_t	rsvd1:4;
		uint32_t	index:16;
		uint32_t	rsvd:12;
		uint32_t	blkaddr:32;
#else
		uint32_t	blkaddr:32;
		uint32_t	rsvd:12;
		uint32_t	index:16;
		uint32_t	rsvd1:4;
#endif
	} bits;
} rdc_rbr_desc_t, *rdc_rbr_desc_pt;

typedef union _rdc_rcr_desc_t {
	uint64_t value;
	struct {
#if defined(_BIG_ENDIAN)
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t multi:1;
			uint32_t pkttype:5;
			uint32_t pkterrs:3;
			uint32_t rsvd:4;
			uint32_t ponum:1;
			uint32_t clscode:5;
			uint32_t promisc:1;
			uint32_t rsshash:1;
			uint32_t tcamhit:1;
			uint32_t bufsz:2;
			uint32_t len:8;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t len:8;
			uint32_t bufsz:2;
			uint32_t tcamhit:1;
			uint32_t rsshash:1;
			uint32_t promisc:1;
			uint32_t clscode:5;
			uint32_t ponum:1;
			uint32_t rsvd:4;
			uint32_t pkterrs:3;
			uint32_t pkttype:5;
			uint32_t multi:1;
#endif
		} hdw;
#endif
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t len:6;
			uint32_t last:1;
			uint32_t subidx:9;
			uint32_t index:16;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t index:16;
			uint32_t subidx:9;
			uint32_t last:1;
			uint32_t len:6;
#endif
		} ldw;
#if !defined(_BIG_ENDIAN)
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t multi:1;
			uint32_t pkttype:5;
			uint32_t pkterrs:3;
			uint32_t rsvd:4;
			uint32_t ponum:1;
			uint32_t clscode:5;
			uint32_t promisc:1;
			uint32_t rsshash:1;
			uint32_t tcamhit:1;
			uint32_t bufsz:2;
			uint32_t len:8;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t len:8;
			uint32_t bufsz:2;
			uint32_t tcamhit:1;
			uint32_t rsshash:1;
			uint32_t promisc:1;
			uint32_t clscode:5;
			uint32_t ponum:1;
			uint32_t rsvd:4;
			uint32_t pkterrs:3;
			uint32_t pkttype:5;
			uint32_t multi:1;
#endif
		} hdw;
#endif
	} bits;
} rdc_rcr_desc_t, *rdc_rcr_desc_pt;

typedef struct _rdc_mbx_desc_t {
	rdc_ctl_stat_t		ctl_stat;	/* 8 bytes */
	rdc_pre_st_t 		pre_st;		/* 8 bytes */
	rdc_kick_t		kick;		/* 4 of 8 bytes */
	uint64_t		resv[5];
} rdc_mbx_desc_t, *rdc_mbx_desc_pt;





#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SXGE_RDC_H */
