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

#ifndef _SXGE_INTR_H
#define	_SXGE_INTR_H

#ifdef	__cplusplus
extern "C" {
#endif

#define	SXGE_MAX_VNI_CNT		12
#define	SXGE_MAX_NF_PER_VNI_CNT		4
#define	SXGE_MAX_LD_PER_NF_CNT		12
#define	SXGE_MAX_LD_GRP_PER_NF_CNT	16

/* Logical devices numbers */
#define	LD_RXDMA(chan)			(0 + (chan))
#define	LD_TXDMA(chan)			(4 + (chan))
#define	LD_RSV				8
#define	LD_MAILBOX			9
#define	LD_RXVMAC			10
#define	LD_TXVMAC			11
#define	LD_VNI_ERROR			12
#define	LD_MAX				LD_VNI_ERROR

/*
 * Host Domain Interrupts
 */

/*
 * Logical Device Mask and Group Number
 * vni 0 to 11, nf 0 to 3, ld 0 to 12
 */
#define	INTR_LD_MSK_GNUM_OFF		(SXGE_HOST_VNI_BASE + 0x8400)
#define	INTR_LD_MSK_GNUM_VNI_STP	0x10000
#define	INTR_LD_MSK_GNUM_NF_STP		0x2000
#define	INTR_LD_MSK_GNUM_LD_STP		0x0008
#define	INTR_LD_MSK_GNUM(vni, nf, ld)	\
				((INTR_LD_MSK_GNUM_OFF) +	\
				((vni) * INTR_LD_MSK_GNUM_VNI_STP) +	\
				((nf) * INTR_LD_MSK_GNUM_NF_STP) +	\
				((ld) * INTR_LD_MSK_GNUM_LD_STP))

#define	LD_MSK_GNUM_LDG_NUM_SH		0
#define	LD_MSK_GNUM_LDG_NUM_MSK		0x000f
#define	LD_MSK_GNUM_LDF_MASK_SH		4
#define	LD_MSK_GNUM_LDF_MASK_MSK	0x0003
#define	LD_MSK_GNUM_EN_LDG_WR_SH	6
#define	LD_MSK_GNUM_EN_LDG_WR_MSK	0x01
#define	LD_MSK_GNUM_EN_MASK_WR_SH	7
#define	LD_MSK_GNUM_EN_MASK_WR_MSK	0x01

typedef union _intr_ld_msk_gnum_t {
	uint64_t value;
	struct {
#if defined(_BIG_ENDIAN)
		uint32_t hdw;
#endif
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t rsvd:24;
			uint32_t en_mask_wr:1;
			uint32_t en_ldg_wr:1;
			uint32_t ldf_mask:2;
			uint32_t ldg_num:4;

#elif defined(_BIT_FIELDS_LTOH)
			uint32_t ldg_num:4;
			uint32_t ldf_mask:2;
			uint32_t en_ldg_wr:1;
			uint32_t en_mask_wr:1;
			uint32_t rsvd:24;
#endif
		} ldw;
#if !defined(_BIG_ENDIAN)
		uint32_t hdw;
#endif
	} bits;
} intr_ld_msk_gnum_t, *p_intr_ld_msk_gnum_t;

/*
 * Logical Device State Vector LDSV
 * vni 0 to 11, nf 0 to 3, grp 0 to 15
 */
#define	INTR_LDSV_OFF			(SXGE_HOST_VNI_BASE + 0x8480)
#define	INTR_LDSV_VNI_STP		0x10000
#define	INTR_LDSV_NF_STP		0x2000
#define	INTR_LDSV_GRP_STP		0x0008
#define	INTR_LDSV(vni, nf, grp)		((INTR_LDSV_OFF) +	\
				((vni) * INTR_LDSV_VNI_STP) +	\
				((nf) * INTR_LDSV_NF_STP) +	\
				((grp) * INTR_LDSV_GRP_STP))

#define	LDSV_0_SH		0
#define	LDSV_0_MSK		0x1fff
#define	LDSV_1_SH		16
#define	LDSV_1_MSK		0x1fff
#define	LDSV_0_RXDMA0_SH	0
#define	LDSV_0_RXDMA1_SH	1
#define	LDSV_0_RXDMA2_SH	2
#define	LDSV_0_RXDMA3_SH	3
#define	LDSV_0_TXDMA0_SH	4
#define	LDSV_0_TXDMA1_SH	5
#define	LDSV_0_TXDMA2_SH	6
#define	LDSV_0_TXDMA3_SH	7
#define	LDSV_0_MBOX_SH		9
#define	LDSV_0_RXVMAC_SH	10
#define	LDSV_0_TXVMAC_SH	11
#define	LDSV_0_VNIERR_SH	12
#define	LDSV_1_RXDMA0_SH	16
#define	LDSV_1_RXDMA1_SH	17
#define	LDSV_1_RXDMA2_SH	18
#define	LDSV_1_RXDMA3_SH	19
#define	LDSV_1_TXDMA0_SH	20
#define	LDSV_1_TXDMA1_SH	21
#define	LDSV_1_TXDMA2_SH	22
#define	LDSV_1_TXDMA3_SH	23
#define	LDSV_1_MBOX_SH		25
#define	LDSV_1_RXVMAC_SH	26
#define	LDSV_1_TXVMAC_SH	27
#define	LDSV_1_VNI_ERR_SH	28

typedef union _intr_ldsv_t {
	uint64_t value;
	struct {
#if defined(_BIG_ENDIAN)
		uint32_t hdw;
#endif
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t rsvd1:3;
			uint32_t ldsv1_vni_err:1;
			uint32_t ldsv1_tx_vmac:1;
			uint32_t ldsv1_rx_vmac:1;
			uint32_t ldsv1_mbox:1;
			uint32_t ldsv1_rsvd:1;
			uint32_t ldsv1_txdma_3:1;
			uint32_t ldsv1_txdma_2:1;
			uint32_t ldsv1_txdma_1:1;
			uint32_t ldsv1_txdma_0:1;
			uint32_t ldsv1_rxdma_3:1;
			uint32_t ldsv1_rxdma_2:1;
			uint32_t ldsv1_rxdma_1:1;
			uint32_t ldsv1_rxdma_0:1;
			uint32_t rsvd2:3;
			uint32_t ldsv0_vni_err:1;
			uint32_t ldsv0_tx_vmac:1;
			uint32_t ldsv0_rx_vmac:1;
			uint32_t ldsv0_mbox:1;
			uint32_t ldsv0_rsvd:1;
			uint32_t ldsv0_txdma_3:1;
			uint32_t ldsv0_txdma_2:1;
			uint32_t ldsv0_txdma_1:1;
			uint32_t ldsv0_txdma_0:1;
			uint32_t ldsv0_rxdma_3:1;
			uint32_t ldsv0_rxdma_2:1;
			uint32_t ldsv0_rxdma_1:1;
			uint32_t ldsv0_rxdma_0:1;

#elif defined(_BIT_FIELDS_LTOH)
			uint32_t ldsv0_rxdma_0:1;
			uint32_t ldsv0_rxdma_1:1;
			uint32_t ldsv0_rxdma_2:1;
			uint32_t ldsv0_rxdma_3:1;
			uint32_t ldsv0_txdma_0:1;
			uint32_t ldsv0_txdma_1:1;
			uint32_t ldsv0_txdma_2:1;
			uint32_t ldsv0_txdma_3:1;
			uint32_t ldsv0_rsvd:1;
			uint32_t ldsv0_mbox:1;
			uint32_t ldsv0_rx_vmac:1;
			uint32_t ldsv0_tx_vmac:1;
			uint32_t ldsv0_vni_err:1;
			uint32_t rsvd2:3;
			uint32_t ldsv1_rxdma_0:1;
			uint32_t ldsv1_rxdma_1:1;
			uint32_t ldsv1_rxdma_2:1;
			uint32_t ldsv1_rxdma_3:1;
			uint32_t ldsv1_txdma_0:1;
			uint32_t ldsv1_txdma_1:1;
			uint32_t ldsv1_txdma_2:1;
			uint32_t ldsv1_txdma_3:1;
			uint32_t ldsv1_rsvd:1;
			uint32_t ldsv1_mbox:1;
			uint32_t ldsv1_rx_vmac:1;
			uint32_t ldsv1_tx_vmac:1;
			uint32_t ldsv1_vni_err:1;
			uint32_t rsvd1:3;
#endif
		} ldw;
#if !defined(_BIG_ENDIAN)
		uint32_t hdw;
#endif
	} bits;
} intr_ldsv_t, *p_intr_ldsv_t;

/*
 * Interrupt state vector macros
 */
#define	LDSV0_ON(ld, v)	((v >> ld) & 0x1)
#define	LDSV1_ON(ld, v)	((v >> (ld + LDSV_1_SH)) & 0x1)

/*
 * Logical Device Group Interrupt Management
 * grp 0 to 15
 */
#define	INTR_LDGIMGN_OFF		(SXGE_STD_RES_BASE + 0x0000)
#define	INTR_LDGIMGN_GRP_STP	0x0008
#define	INTR_LDGIMGN(grp)	((INTR_LDGIMGN_OFF) +	\
			((grp) * (INTR_LDGIMGN_GRP_STP)))

#define	LDGIMGN_VNI_STAT_SH		0
#define	LDGIMGN_VNI_STAT_MSK		0x0fff
#define	LDGIMGN_VNI_STAT_SH_VNI(vni_id)	(vni_id)
#define	LDGIMGN_VNI_STAT_INT_AST	1
#define	LDGIMGN_VNI_STAT_INT_DEAST	0
#define	LDGIMGN_ARM_SH			12
#define	LDGIMGN_ARM			1
#define	LDGIMGN_TIMER_SH		16
#define	LDGIMGN_TIMER_MSK		0x003f

typedef union _intr_ldgimgn_t {
	uint64_t value;
	struct {
#if defined(_BIG_ENDIAN)
		uint32_t hdw;
#endif
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t rsvd1:10;
			uint32_t timer:6;
			uint32_t rsvd2:3;
			uint32_t arm:1;
			uint32_t vni_stat:12;

#elif defined(_BIT_FIELDS_LTOH)
			uint32_t vni_stat:12;
			uint32_t arm:1;
			uint32_t rsvd2:3;
			uint32_t timer:6;
			uint32_t rsvd1:10;
#endif
		} ldw;
#if !defined(_BIG_ENDIAN)
		uint32_t hdw;
#endif
	} bits;
} intr_ldgimgn_t, *p_intr_ldgimgn_t;

/*
 * Interrupt Command and Status Register.
 */
#define	INTR_CS_OFF		(SXGE_STD_RES_BASE + 0x200)

#define	CS_INTR_FN_RESET_SH		0
#define	CS_INTR_FN_RESET_MSK		0x0001
#define	CS_REJECT_ST_SH			1
#define	CS_REJECT_ST_MSK		0x0001
#define	CS_RETRY_ST_SH			2
#define	CS_RETRY_ST_MSK			0x0001

typedef union _intr_cs_t {
	uint64_t value;
	struct {
#if defined(_BIG_ENDIAN)
		uint32_t hdw;
#endif
		struct {
#if defined(_BIT_FIELDS_HTOL)
			uint32_t rsvd:29;
			uint32_t retry_st:1;
			uint32_t reject_st:1;
			uint32_t intr_fn_rst:1;

#elif defined(_BIT_FIELDS_LTOH)
			uint32_t intr_fn_rst:1;
			uint32_t reject_st:1;
			uint32_t retry_st:1;
			uint32_t rsvd:29;
#endif
		} ldw;
#if !defined(_BIG_ENDIAN)
		uint32_t hdw;
#endif
	} bits;
} intr_cs_t, *p_intr_cs_t;

#ifdef	__cplusplus
}
#endif

#endif /* _SXGE_INTR_H */
