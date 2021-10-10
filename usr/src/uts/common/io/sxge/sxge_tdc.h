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

#ifndef	_SYS_SXGE_TDC_H
#define	_SYS_SXGE_TDC_H

#ifdef	__cplusplus
extern "C" {
#endif

#if !defined(_BIG_ENDIAN)
#define	SWAP(X)	(X)
#else
#define	SWAP(X)   \
	(((X >> 32) & 0x00000000ffffffff) | ((X << 32) & 0xffffffff00000000))
#endif

#if !defined(PBASE)
#define	PBASE (0)
#endif

#if 0
#define	EPS_TXVNI_COMMON_BASE	(0xB5000)

#define	HOST_VNI_BASE		(PBASE+0x00000)
#define	TXDMA_BASE		(PBASE+0x00000)
#define	RXDMA_BASE		(PBASE+0x00400)
#define	RXVMAC_BASE		(PBASE+0x08000)
#define	TXVMAC_BASE		(PBASE+0x08200)
#define	INTR_BASE		(PBASE+0x08400)
#define	SHARE_RESOURCE_BASE	(0xC0000)
#define	STAND_RESOURCE_BASE	(0xF0000)

#define	VNI_STEP		(0x10000)
#define	TXDMA_STEP		(0x02000)
#else
#if !defined(SXGE_TXDMA_BASE)
#define	SXGE_TXDMA_BASE		(0x00000)
#endif
#define	TXDMA_BASE		(PBASE+(SXGE_TXDMA_BASE))
#define	TXDMA_STEP		(0x02000)
#define	TXDMA_VNI_STEP		(0x10000)
#endif


#define	EPS_TXC_BASE(vni)	(vni * 0x100)
#define	TDC_BASE(vni, tdc)	((vni * TXDMA_VNI_STEP) + (tdc * TXDMA_STEP))

#define	TDC_PG_HDL_REG		(TXDMA_BASE + 0x0008)
#define	TDC_RNG_CFG_REG		(TXDMA_BASE + 0x0000)
#define	TDC_RNG_HDL_REG		(TXDMA_BASE + 0x0010)
#define	TDC_RNG_KICK_REG	(TXDMA_BASE + 0x0018)
#define	TDC_DMA_ENT_MSK_REG	(TXDMA_BASE + 0x0020)
#define	TDC_CS_REG		(TXDMA_BASE + 0x0028)
#define	TDC_INTR_DBG_REG	(TXDMA_BASE + 0x0060)
#define	TDC_CS_DBG_REG		(TXDMA_BASE + 0x0068)
#define	TDC_MBH_REG		(TXDMA_BASE + 0x0030)
#define	TDC_MBL_REG		(TXDMA_BASE + 0x0038)
#define	TDC_PRE_ST_REG		(TXDMA_BASE + 0x0040)
#define	TDC_ERR_LOGH_REG	(TXDMA_BASE + 0x0048)
#define	TDC_ERR_LOGL_REG	(TXDMA_BASE + 0x0050)

#define	TXC_DMA_MAX		(TXDMA_BASE + 0x0200)

#define	TXC_PORT_DMA		(EPS_TXVNI_COMMON_BASE + 0x0010)
#define	TXC_CONTROL		(EPS_TXVNI_COMMON_BASE + 0x0018)

/* TDC_PG_HDL_REG : Transmit DMA Page Handle */

#define	TDC_PG_HDL_CTL_SH	0			/* bits 19:0 */
#define	TDC_PG_HDL_CTL_MK	0x00000000000FFFFFULL
#define	TDC_PG_HDL_DATA_SH	32			/* bits 51:32 */
#define	TDC_PG_HDL_DATA_MK	0x00000000000FFFFFULL

typedef union _tdc_pg_hdl_t {
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
} tdc_pg_hdl_t, *tdc_pg_hdl_pt;

/* TDC_RNG_CFG_REG : Transmit Ring Configuration */
#define	TDC_RNG_CFG_STADDR_SH	6			/* bits 18:6 */
#define	TDC_RNG_CFG_STADDR_MK		0x000000000007FFC0ULL
#define	TDC_RNG_CFG_ADDR_MK		0x00000FFFFFFFFFC0ULL
#define	TDC_RNG_CFG_STADDR_BASE_SH	19			/* bits 43:19 */
#define	TDC_RNG_CFG_STADDR_BASE_MK	0x00000FFFFFF80000ULL
#define	TDC_RNG_CFG_LEN_SH		48			/* bits 60:48 */
#define	TDC_RNG_CFG_LEN_MK		0xFFF8000000000000ULL

#define	TDC_RNG_HEAD_TAIL_SH		3
#define	TDC_RNG_HEAD_TAIL_WRAP_SH	19

typedef union _tdc_rng_cfg_t {
	uint64_t value;
	struct {
#if defined(_BIG_ENDIAN)
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t res2:3;
			uint32_t len:13;
			uint32_t res1:4;
			uint32_t staddr_base:12;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t staddr_base:12;
			uint32_t res1:4;
			uint32_t len:13;
			uint32_t res2:3;
#endif
		} hdw;
#endif
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t staddr_base:13;
			uint32_t staddr:13;
			uint32_t res2:6;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t res2:6;
			uint32_t staddr:13;
			uint32_t staddr_base:13;
#endif
		} ldw;
#ifndef _BIG_ENDIAN
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t res2:3;
			uint32_t len:13;
			uint32_t res1:4;
			uint32_t staddr_base:12;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t staddr_base:12;
			uint32_t res1:4;
			uint32_t len:13;
			uint32_t res2:3;
#endif
		} hdw;
#endif
	} bits;
} tdc_rng_cfg_t, *tdc_rng_cfg_pt;

/* TDC_RNG_HDL_REG : Transmit Ring Head Low */
#define	TDC_RNG_HDL_SH		3			/* bit 31:3 */
#define	TDC_RNG_HDL_MK		0x00000000FFFFFFF8ULL

typedef union _tdc_rng_hdl_t {
	uint64_t value;
	struct {
#if defined(_BIG_ENDIAN)
		uint32_t hdw;
#endif
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t res0:12;
			uint32_t wrap:1;
			uint32_t head:16;
			uint32_t res2:3;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t res2:3;
			uint32_t head:16;
			uint32_t wrap:1;
			uint32_t res0:12;
#endif
		} ldw;
#ifndef _BIG_ENDIAN
		uint32_t hdw;
#endif
	} bits;
} tdc_rng_hdl_t, *tdc_rng_hdl_pt;

/* TDC_RNG_KICK_REG : Transmit Ring Kick */
#define	TDC_RNG_KICK_TAIL_SH	3		/* bit 43:3 */
#define	TDC_RNG_KICK_TAIL_MK		0x000000FFFFFFFFFF8ULL

typedef union _tdc_rng_kick_t {
	uint64_t value;
	struct {
#ifdef	_BIG_ENDIAN
		uint32_t hdw;
#endif
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t res0:12;
			uint32_t wrap:1;
			uint32_t tail:16;
			uint32_t res2:3;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t res2:3;
			uint32_t tail:16;
			uint32_t wrap:1;
			uint32_t res0:12;
#endif
		} ldw;
#ifndef _BIG_ENDIAN
		uint32_t hdw;
#endif
	} bits;
} tdc_rng_kick_t, *tdc_rng_kick_pt;


/* TDC_DMA_ENT_MSK_REG	: Transmit Event Mask */
#define	TDC_ENT_MSK_PKT_PRT_ERR_SH		0	/* bit 0: 0 to flag */
#define	TDC_ENT_MSK_PKT_PRT_ERR_MK		0x0000000000000001ULL
#define	TDC_ENT_MSK_CONF_PART_ERR_SH		1	/* bit 1: 0 to flag */
#define	TDC_ENT_MSK_CONF_PART_ERR_MK		0x0000000000000002ULL
#define	TDC_ENT_MSK_NACK_PKT_RD_SH		2	/* bit 2: 0 to flag */
#define	TDC_ENT_MSK_NACK_PKT_RD_MK		0x0000000000000004ULL
#define	TDC_ENT_MSK_NACK_PREF_SH		3	/* bit 3: 0 to flag */
#define	TDC_ENT_MSK_NACK_PREF_MK		0x0000000000000008ULL
#define	TDC_ENT_MSK_PREF_BUF_ECC_ERR_SH	4	/* bit 4: 0 to flag */
#define	TDC_ENT_MSK_PREF_BUF_ECC_ERR_MK	0x0000000000000010ULL
#define	TDC_ENT_MSK_TX_RING_OFLOW_SH		5	/* bit 5: 0 to flag */
#define	TDC_ENT_MSK_TX_RING_OFLOW_MK		0x0000000000000020ULL
#define	TDC_ENT_MSK_PKT_SIZE_ERR_SH		6	/* bit 6: 0 to flag */
#define	TDC_ENT_MSK_PKT_SIZE_ERR_MK		0x0000000000000040ULL
#define	TDC_ENT_MSK_MBOX_ERR_SH		7	/* bit 7: 0 to flag */
#define	TDC_ENT_MSK_MBOX_ERR_MK		0x0000000000000080ULL
#define	TDC_ENT_MSK_MK_SH			15	/* bit 15: 0 to flag */
#define	TDC_ENT_MSK_MK_MK			0x0000000000008000ULL
#define	TDC_ENT_MSK_MK_ALL		(TDC_ENT_MSK_PKT_PRT_ERR_MK | \
					TDC_ENT_MSK_CONF_PART_ERR_MK | \
					TDC_ENT_MSK_NACK_PKT_RD_MK |	\
					TDC_ENT_MSK_NACK_PREF_MK |	\
					TDC_ENT_MSK_PREF_BUF_ECC_ERR_MK | \
					TDC_ENT_MSK_TX_RING_OFLOW_MK | \
					TDC_ENT_MSK_PKT_SIZE_ERR_MK | \
					TDC_ENT_MSK_MBOX_ERR_MK | \
					TDC_ENT_MSK_MK_MK)

typedef union _tdc_dma_ent_msk_t {
	uint64_t value;
	struct {
#ifdef	_BIG_ENDIAN
		uint32_t hdw;
#endif
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t res1_1:16;
			uint32_t mk:1;
			uint32_t res2:2;
			uint32_t rej_resp_err:1;
			uint32_t sop_bit_err:1;
			uint32_t prem_sop_err:1;
			uint32_t desc_len_err:1;
			uint32_t desc_nptr_err:1;
			uint32_t mbox_err:1;
			uint32_t pkt_size_err:1;
			uint32_t tx_ring_oflow:1;
			uint32_t pref_buf_ecc_err:1;
			uint32_t nack_pref:1;
			uint32_t nack_pkt_rd:1;
			uint32_t conf_part_err:1;
			uint32_t pkt_prt_err:1;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t pkt_prt_err:1;
			uint32_t conf_part_err:1;
			uint32_t nack_pkt_rd:1;
			uint32_t nack_pref:1;
			uint32_t pref_buf_ecc_err:1;
			uint32_t tx_ring_oflow:1;
			uint32_t pkt_size_err:1;
			uint32_t mbox_err:1;
			uint32_t desc_nptr_err:1;
			uint32_t desc_len_err:1;
			uint32_t prem_sop_err:1;
			uint32_t sop_bit_err:1;
			uint32_t rej_resp_err:1;
			uint32_t res2:2;
			uint32_t mk:1;
			uint32_t res1_1:16;
#endif
		} ldw;
#ifndef _BIG_ENDIAN
		uint32_t hdw;
#endif
	} bits;
} tdc_dma_ent_msk_t, *tdc_dma_ent_msk_pt;

/* TDC_CS_REG : Transmit Control and Status */
#define	TDC_CS_PKT_PRT_ERR_SH			0	/* RO, bit 0 */
#define	TDC_CS_PKT_PRT_ERR_MK			0x0000000000000001ULL
#define	TDC_CS_CONF_PART_ERR_SHIF		1	/* RO, bit 1 */
#define	TDC_CS_CONF_PART_ERR_MK		0x0000000000000002ULL
#define	TDC_CS_NACK_PKT_RD_SH			2	/* RO, bit 2 */
#define	TDC_CS_NACK_PKT_RD_MK			0x0000000000000004ULL
#define	TDC_CS_PREF_SH			3	/* RO, bit 3 */
#define	TDC_CS_PREF_MK				0x0000000000000008ULL
#define	TDC_CS_PREF_BUF_PAR_ERR_SH		4	/* RO, bit 4 */
#define	TDC_CS_PREF_BUF_PAR_ERR_MK		0x0000000000000010ULL
#define	TDC_CS_RING_OFLOW_SH			5	/* RO, bit 5 */
#define	TDC_CS_RING_OFLOW_MK			0x0000000000000020ULL
#define	TDC_CS_PKT_SIZE_ERR_SH		6	/* RW, bit 6 */
#define	TDC_CS_PKT_SIZE_ERR_MK			0x0000000000000040ULL
#define	TDC_CS_MMK_SH				14	/* RC, bit 14 */
#define	TDC_CS_MMK_MK				0x0000000000004000ULL
#define	TDC_CS_MK_SH				15	/* RCW1C, bit 15 */
#define	TDC_CS_MK_MK				0x0000000000008000ULL
#define	TDC_CS_SNG_SH				27	/* RO, bit 27 */
#define	TDC_CS_SNG_MK				0x0000000008000000ULL
#define	TDC_CS_STOP_N_GO_SH			28	/* RW, bit 28 */
#define	TDC_CS_STOP_N_GO_MK			0x0000000010000000ULL
#define	TDC_CS_MB_SH				29	/* RO, bit 29 */
#define	TDC_CS_MB_MK				0x0000000020000000ULL
#define	TDC_CS_RST_STATE_SH			30	/* Rw, bit 30 */
#define	TDC_CS_RST_STATE_MK			0x0000000040000000ULL
#define	TDC_CS_RST_SH				31	/* Rw, bit 31 */
#define	TDC_CS_RST_MK				0x0000000080000000ULL
#define	TDC_CS_LASTMK_SH			32	/* RW, bit 43:32 */
#define	TDC_CS_LASTMARK_MK			0x00000FFF00000000ULL
#define	TDC_CS_PKT_CNT_SH			48	/* RW, bit 59:48 */
#define	TDC_CS_PKT_CNT_MK			0x0FFF000000000000ULL

/* Trasnmit Control and Status */
typedef union _tdc_cs_t {
	uint64_t value;
	struct {
#ifdef	_BIG_ENDIAN
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t res1:4;
			uint32_t pkt_cnt:12;
			uint32_t res2:4;
			uint32_t lastmark:12;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t lastmark:12;
			uint32_t res2:4;
			uint32_t pkt_cnt:12;
			uint32_t res1:4;
#endif
		} hdw;

#endif
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t rst:1;
			uint32_t rst_state:1;
			uint32_t mb:1;
			uint32_t stop_n_go:1;
			uint32_t sng_state:1;
			uint32_t res1:11;
			uint32_t mk:1;
			uint32_t mmk:1;
			uint32_t res2:1;
			uint32_t rej_resp_err:1;
			uint32_t sop_bit_err:1;
			uint32_t prem_sop_err:1;
			uint32_t desc_len_err:1;
			uint32_t desc_nptr_err:1;
			uint32_t mbox_err:1;
			uint32_t pkt_size_err:1;
			uint32_t tx_ring_oflow:1;
			uint32_t pref_buf_par_err:1;
			uint32_t nack_pref:1;
			uint32_t nack_pkt_rd:1;
			uint32_t conf_part_err:1;
			uint32_t pkt_prt_err:1;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t pkt_prt_err:1;
			uint32_t conf_part_err:1;
			uint32_t nack_pkt_rd:1;
			uint32_t nack_pref:1;
			uint32_t pref_buf_par_err:1;
			uint32_t tx_ring_oflow:1;
			uint32_t pkt_size_err:1;
			uint32_t mbox_err:1;
			uint32_t desc_nptr_err:1;
			uint32_t desc_len_err:1;
			uint32_t prem_sop_err:1;
			uint32_t sop_bit_err:1;
			uint32_t rej_resp_err:1;
			uint32_t res2:1;
			uint32_t mmk:1;
			uint32_t mk:1;
			uint32_t res1:11;
			uint32_t sng_state:1;
			uint32_t stop_n_go:1;
			uint32_t mb:1;
			uint32_t rst_state:1;
			uint32_t rst:1;
#endif
		} ldw;
#ifndef _BIG_ENDIAN
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t res1:4;
			uint32_t pkt_cnt:12;
			uint32_t res2:4;
			uint32_t lastmark:12;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t lastmark:12;
			uint32_t res2:4;
			uint32_t pkt_cnt:12;
			uint32_t res1:4;
#endif
	} hdw;

#endif
	} bits;
} tdc_cs_t, *tdc_cs_pt;


/* TDC_INTR_DBG_REG */

typedef union _tdc_intr_dbg_t {
	uint64_t value;
	struct {
#ifdef	_BIG_ENDIAN
		uint32_t hdw;
#endif
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t res:16;
			uint32_t mk:1;
			uint32_t rsvd:2;
			uint32_t rej_resp_err:1;
			uint32_t sop_bit_err:1;
			uint32_t prem_sop_err:1;
			uint32_t desc_len_err:1;
			uint32_t desc_nptr_err:1;
			uint32_t mbox_err:1;
			uint32_t pkt_size_err:1;
			uint32_t tx_ring_oflow:1;
			uint32_t pref_buf_par_err:1;
			uint32_t nack_pref:1;
			uint32_t nack_pkt_rd:1;
			uint32_t conf_part_err:1;
			uint32_t pkt_part_err:1;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t pkt_part_err:1;
			uint32_t conf_part_err:1;
			uint32_t nack_pkt_rd:1;
			uint32_t nack_pref:1;
			uint32_t pref_buf_par_err:1;
			uint32_t tx_ring_oflow:1;
			uint32_t pkt_size_err:1;
			uint32_t mbox_err:1;
			uint32_t desc_nptr_err:1;
			uint32_t desc_len_err:1;
			uint32_t prem_sop_err:1;
			uint32_t sop_bit_err:1;
			uint32_t rej_resp_err:1;
			uint32_t rsvd:2;
			uint32_t mk:1;
			uint32_t res:16;
#endif
		} ldw;
#ifndef _BIG_ENDIAN
		uint32_t hdw;
#endif
	} bits;
} tdc_intr_dbg_t, *tdc_intr_dbg_pt;

/* TDC_CS_DBG_REG */

typedef union _tdc_cs_dbg_t {
	uint64_t value;
	struct {
#ifdef	_BIG_ENDIAN
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t res1:4;
			uint32_t pkt_cnt:12;
			uint32_t res2:16;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t res2:16;
			uint32_t pkt_cnt:12;
			uint32_t res1:4;
#endif
		} hdw;

#endif
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t rsvd:32;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t rsvd:32;

#endif
		} ldw;

#ifndef _BIG_ENDIAN
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t res1:4;
			uint32_t pkt_cnt:12;
			uint32_t res2:16;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t res2:16;
			uint32_t pkt_cnt:12;
			uint32_t res1:4;
#endif
	} hdw;

#endif
	} bits;
} tdc_cs_dbg_t, *tdc_cs_dbg_pt;

/* TDC_MBH_REG : Trasnmit Mailbox High */
#define	TDC_MBH_SH			0	/* bit 11:0 */
#define	TDC_MBH_ADDR_SH		32	/* bit 43:32 */
#define	TDC_MBH_MK			0x0000000000000FFFULL

typedef union _tdc_mbh_t {
	uint64_t value;
	struct {
#ifdef	_BIG_ENDIAN
		uint32_t hdw;
#endif
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t res1_1:20;
			uint32_t mbaddr:12;

#elif defined(_BIT_FIELDS_LTOH)
			uint32_t mbaddr:12;
			uint32_t res1_1:20;
#endif
		} ldw;
#ifndef _BIG_ENDIAN
		uint32_t hdw;
#endif
	} bits;
} tdc_mbh_t, *tdc_mbh_pt;


/* TDC_MBL_REG : Trasnmit Mailbox Low */
#define	TDC_MBL_SH			6	/* bit 31:6 */
#define	TDC_MBL_MK			0x00000000FFFFFFC0ULL

typedef union _tdc_mbl_t {
	uint64_t value;
	struct {
#ifdef	_BIG_ENDIAN
		uint32_t hdw;
#endif
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t mbaddr:26;
			uint32_t res2:6;

#elif defined(_BIT_FIELDS_LTOH)
			uint32_t res2:6;
			uint32_t mbaddr:26;
#endif
		} ldw;
#ifndef _BIG_ENDIAN
		uint32_t hdw;
#endif
	} bits;
} tdc_mbl_t, *tdc_mbl_pt;

/* TDC_PRE_ST_REG : Trasnmit Prefetch State */
#define	TDC_PRE_ST_SH		0	/* bit 5:0 */
#define	TDC_PRE_ST_MK		0x000000000000003FULL

typedef union _tdc_pre_st_t {
	uint64_t value;
	struct {
#ifdef	_BIG_ENDIAN
		uint32_t hdw;
#endif
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t res1_1:13;
			uint32_t shadow_hd:19;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t shadow_hd:19;
			uint32_t res1_1:13;
#endif
		} ldw;
#ifndef _BIG_ENDIAN
		uint32_t hdw;
#endif
	} bits;
} tdc_pre_st_t, *tdc_pre_st_pt;

/* TDC_ERR_LOGH_REG : Trasnmit Ring Error Log High */
#define	TDC_ERR_LOGH_ERR_ADDR_SH		0	/* RO bit 11:0 */
#define	TDC_ERR_LOGH_ERR_ADDR_MK		0x0000000000000FFFULL
#define	TDC_ERR_LOGH_ADDR_SH		32
#define	TDC_ERR_LOGH_ERRCODE_SH		26	/* RO bit 29:26 */
#define	TDC_ERR_LOGH_ERRCODE_MK		0x000000003C000000ULL
#define	TDC_ERR_LOGH_MERR_SH		30	/* RO bit 30 */
#define	TDC_ERR_LOGH_MERR_MK		0x0000000040000000ULL
#define	TDC_ERR_LOGH_ERR_SH		31	/* RO bit 31 */
#define	TDC_ERR_LOGH_ERR_MK		0x0000000080000000ULL

/* Transmit Ring Error codes */
#define	TDC_ERR_PKT_PRT_ERR			0
#define	TDC_ERR_CONF_PART_ERR		0x01
#define	TDC_ERR_NACK_PKT_ERR			0x02
#define	TDC_ERR_NACK_PREF_ERR		0x03
#define	TDC_ERR_PREF_BUF_PAR_ERR		0x04
#define	TDC_ERR_TX_RING_OFLOW_ERR		0x05
#define	TDC_ERR_PKT_SIZE_ERR			0x06

typedef union _tdc_err_logh_t {
	uint64_t value;
	struct {
#ifdef	_BIG_ENDIAN
		uint32_t hdw;
#endif
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t err:1;
			uint32_t merr:1;
			uint32_t errcode:4;
			uint32_t res2:14;
			uint32_t err_addr:12;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t err_addr:12;
			uint32_t res2:14;
			uint32_t errcode:4;
			uint32_t merr:1;
			uint32_t err:1;

#endif
		} ldw;
#ifndef _BIG_ENDIAN
		uint32_t hdw;
#endif
	} bits;
} tdc_err_logh_t, *tdc_err_logh_pt;


/* TDC_ERR_LOGL_REG : Trasnmit Ring Error Log  */
#define	TDC_ERR_LOGL_ERR_ADDR_SH		0	/* RO bit 31:0 */
#define	TDC_ERR_LOGL_ERR_ADDR_MK		0x00000000FFFFFFFFULL

typedef union _tdc_err_logl_t {
	uint64_t value;
	struct {
#ifdef	_BIG_ENDIAN
		uint32_t hdw;
#endif
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t err_addr:32;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t err_addr:32;

#endif
		} ldw;
#ifndef _BIG_ENDIAN
		uint32_t hdw;
#endif
	} bits;
} tdc_err_logl_t, *tdc_err_logl_pt;


/* Register TXC_DMA_MAX */
#define	TXC_DMA_MAX_BURST_DEFAULT	10000

typedef union _txc_dma_max_burst_t {
	uint64_t	value;
	struct {
#if defined(_BIG_ENDIAN)
		uint32_t rsev;
		uint32_t rsev1:12;
		uint32_t dma_max_burst:20;
#else
		uint32_t dma_max_burst:20;
		uint32_t rsev1:12;
		uint32_t rsev;
#endif
	} bits;
} txc_dma_max_burst_t;

typedef union {
	uint64_t	value;
	struct {
#if defined(_BIG_ENDIAN)
		uint32_t rsev;
		uint32_t rsev1:28;
		uint32_t port_dma_list:4;
#else
		uint32_t port_dma_list:4;
		uint32_t rsev1:28;
		uint32_t rsev;
#endif
	} bits;
} txc_port_dma_t;

typedef union {
	uint64_t	value;
	struct {
#if defined(_BIG_ENDIAN)
		uint32_t rsev;
		uint32_t rsev1:10;
		uint32_t debug_select:6;
		uint32_t rsev2:3;
		uint32_t clr_all_stat:1;
		uint32_t rsev3:3;
		uint32_t port_resource:5;
		uint32_t rsev4:1;
		uint32_t txc_hdr_prsr_en:1;
		uint32_t port_enable:1;
		uint32_t txc_enable:1;
#else
		uint32_t txc_enable:1;
		uint32_t port_enable:1;
		uint32_t txc_hdr_prsr_en:1;
		uint32_t rsev4:1;
		uint32_t port_resource:5;
		uint32_t rsev3:3;
		uint32_t clr_all_stat:1;
		uint32_t rsev2:3;
		uint32_t debug_select:6;
		uint32_t rsev1:10;
		uint32_t rsev;
#endif
	} bits;
} txc_control_t;

/* Transmit Packet Descriptor Structure */

#define	TDC_DESC_SADDR_SH	0		/* bits 43:0 */
#define	TDC_DESC_SADDR_MK	0x00000FFFFFFFFFFFULL
#define	TDC_DESC_TRLEN_SH	44		/* bits 56:44 */
#define	TDC_DESC_TRLEN_MK	0x01FFF00000000000ULL
#define	TDC_DESC_CKSEN_SH	57		/* bit 57 */
#define	TDC_DESC_CKSEN_MK	0x0200000000000000ULL
#define	TDC_DESC_NUMPTR_SH	58		/* bits 61:58 */
#define	TDC_DESC_NUMPTR_MK	0x3C00000000000000ULL
#define	TDC_DESC_MARK_SH	62		/* bit 62 */
#define	TDC_DESC_MARK		0x4000000000000000ULL
#define	TDC_DESC_MARK_MK	0x4000000000000000ULL
#define	TDC_DESC_SOP_SH		63		/* bit 63 */
#define	TDC_DESC_SOP		0x8000000000000000ULL
#define	TDC_DESC_SOP_MK		0x8000000000000000ULL

typedef union _tdc_desc_t {
	uint64_t value;
	struct {
#if defined(_BIG_ENDIAN)
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t sop:1;
			uint32_t mark:1;
			uint32_t numptr:4;
			uint32_t cksen:1;
			uint32_t trlen:13;
			uint32_t saddr:12;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t saddr:12;
			uint32_t trlen:13;
			uint32_t cksen:1;
			uint32_t numptr:4;
			uint32_t mark:1;
			uint32_t sop:1;

#endif
		} hdw;
#endif
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t saddr:32;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t saddr:32;
#endif
		} ldw;
#if !defined(_BIG_ENDIAN)
		struct {

#if defined(_BIT_FIELDS_HTOL)
			uint32_t sop:1;
			uint32_t mark:1;
			uint32_t numptr:4;
			uint32_t cksen:1;
			uint32_t trlen:13;
			uint32_t saddr:12;
#elif defined(_BIT_FIELDS_LTOH)
			uint32_t saddr:12;
			uint32_t trlen:13;
			uint32_t cksen:1;
			uint32_t numptr:4;
			uint32_t mark:1;
			uint32_t sop:1;
#endif
		} hdw;
#endif
	} bits;
} tdc_desc_t, *tdc_desc_pt;


typedef struct _tdc_mbx_desc_t {
	tdc_pre_st_t		pre_st;		/* 8 bytes */
	tdc_cs_t 		cs;		/* 8 bytes */
	tdc_rng_kick_t		kick;		/* 8 bytes */
	tdc_rng_hdl_t		hdl;		/* 8 bytes */
	uint64_t		resv[4];
} tdc_mbx_desc_t, *tdc_mbx_desc_pt;



/*
 * Internal Transmit Packet Format (16 bytes)
 */
#define	TDC_PKT_HEADER_SIZE			16
#define	TDC_MAX_GATHER_POINTERS			15
#define	TDC_GATHER_POINTERS_THRESHOLD		8

#define	TDC_MAX_TRANSFER_LENGTH			4096
#define	TDC_JUMBO_MTU				9216

#define	TDC_PKT_HEADER_PAD_SH			0	/* bit 2:0 */
#define	TDC_PKT_HEADER_PAD_MK			0x0000000000000007ULL
#define	TDC_PKT_HEADER_TOT_XFER_LEN_SH		16	/* bit 16:29 */
#define	TDC_PKT_HEADER_TOT_XFER_LEN_MK		0x000000000000FFF8ULL
#define	TDC_PKT_HEADER_L4STUFF_SH		32	/* bit 37:32 */
#define	TDC_PKT_HEADER_L4STUFF_MK		0x0000003F00000000ULL
#define	TDC_PKT_HEADER_L4START_SH		40	/* bit 45:40 */
#define	TDC_PKT_HEADER_L4START_MK		0x00003F0000000000ULL
#define	TDC_PKT_HEADER_L3START_SH		48	/* bit 45:40 */
#define	TDC_PKT_HEADER_IHL_SH			52	/* bit 52 */
#define	TDC_PKT_HEADER_VLAN_SH			56	/* bit 56 */
#define	TDC_PKT_HEADER_TCP_UDP_CRC32C_SH	57	/* bit 57 */
#define	TDC_PKT_HEADER_LLC_SH			57	/* bit 57 */
#define	TDC_PKT_HEADER_TCP_UDP_CRC32C_SET	0x0200000000000000ULL
#define	TDC_PKT_HEADER_TCP_UDP_CRC32C_MK	0x0200000000000000ULL
#define	TDC_PKT_HEADER_L4_PROTO_OP_SH		2	/* bit 59:58 */
#define	TDC_PKT_HEADER_L4_PROTO_OP_MK		0x0C00000000000000ULL
#define	TDC_PKT_HEADER_V4_HDR_CS_SH		60	/* bit 60 */
#define	TDC_PKT_HEADER_V4_HDR_CS_SET		0x1000000000000000ULL
#define	TDC_PKT_HEADER_V4_HDR_CS_MK		0x1000000000000000ULL
#define	TDC_PKT_HEADER_IP_VER_SH		61	/* bit 61 */
#define	TDC_PKT_HEADER_IP_VER_MK		0x2000000000000000ULL
#define	TDC_PKT_HEADER_PKT_TYPE_SH		62	/* bit 62 */
#define	TDC_PKT_HEADER_PKT_TYPE_MK		0x4000000000000000ULL

/* L4 Prototol Operations */
#define	TDC_PKT_L4_PROTO_OP_NOP			0x00
#define	TDC_PKT_L4_PROTO_OP_FULL_L4_CSUM	0x01
#define	TDC_PKT_L4_PROTO_OP_L4_PAYLOAD_CSUM	0x02
#define	TDC_PKT_L4_PROTO_OP_SCTP_CRC32		0x04

/* Transmit Packet Types */
#define	TDC_PKT_PKT_TYPE_NOP			0x00
#define	TDC_PKT_PKT_TYPE_TCP			0x01
#define	TDC_PKT_PKT_TYPE_UDP			0x02
#define	TDC_PKT_PKT_TYPE_SCTP			0x03

#define	TDC_CKSUM_EN_PKT_TYPE_TCP	(1ull << TDC_PKT_HEADER_PKT_TYPE_SH)
#define	TDC_CKSUM_EN_PKT_TYPE_UDP	(2ull << TDC_PKT_HEADER_PKT_TYPE_SH)
#define	TDC_CKSUM_EN_PKT_TYPE_FCOE	(3ull << TDC_PKT_HEADER_PKT_TYPE_SH)
#define	TDC_CKSUM_EN_PKT_TYPE_NOOP	(0ull << TDC_PKT_HEADER_PKT_TYPE_SH)

typedef union _tdc_pkt_hdr_t {
	uint64_t value;
	struct {
#if defined(_BIG_ENDIAN)
			uint32_t cksum_en_pkt_type:2;
			uint32_t ip_ver:1;
			uint32_t res1:2;
			uint32_t pkt_type:1;
			uint32_t llc:1;
			uint32_t vlan:1;
			uint32_t ihl:4;
			uint32_t l3start:4;
			uint32_t resv2:2;
			uint32_t l4start:6;
			uint32_t resv3:2;
			uint32_t l4stuff:6;
			uint32_t resv4:2;
			uint32_t tot_xfer_len:14;
			uint32_t fc_offset:8;
			uint32_t resv5:5;
			uint32_t pad:3;
#else
			uint32_t pad:3;
			uint32_t resv5:5;
			uint32_t fc_offset:8;
			uint32_t tot_xfer_len:14;
			uint32_t resv4:2;
			uint32_t l4stuff:6;
			uint32_t resv3:2;
			uint32_t l4start:6;
			uint32_t resv2:2;
			uint32_t l3start:4;
			uint32_t ihl:4;
			uint32_t vlan:1;
			uint32_t llc:1;
			uint32_t fcoe:1;
			uint32_t res1:2;
			uint32_t ip_ver:1;
			uint32_t cksum_en_pkt_type:2;
#endif
	} bits;
} tdc_pkt_hdr_t, *tdc_pkt_hdr_pt;

typedef struct _tdc_pkt_hdr_all_t {
	tdc_pkt_hdr_t	pkthdr;
	uint64_t	resv;
} tdc_pkt_hdr_all_t, *tdc_pkt_hdr_all_pt;


#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SXGE_TDC_H */
