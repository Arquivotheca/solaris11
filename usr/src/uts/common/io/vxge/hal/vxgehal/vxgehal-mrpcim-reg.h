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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copyright Exar 2010. Copyright (c) 2002-2010 Neterion, Inc.
 * All right Reserved.
 *
 * FileName :	 vxgehal-mrpcim-reg.h
 *
 * Description:  Auto generated Titan register space
 *
 * Generation Information:
 *
 *	  Source File(s):
 *
 *	  C Template:	   templates/c/location.st (version 1.10)
 *	  Code Generation:  java/SWIF_Codegen.java (version 1.62)
 *	  Frontend:	java/SWIF_Main.java (version 1.52)
 */

#ifndef	VXGE_HAL_MRPCIM_REGS_H
#define	VXGE_HAL_MRPCIM_REGS_H

__EXTERN_BEGIN_DECLS

typedef struct vxge_hal_mrpcim_reg_t {

/* 0x00000 */	u64	g3fbct_int_status;
#define	VXGE_HAL_G3FBCT_INT_STATUS_ERR_G3IF_INT		    mBIT(0)
/* 0x00008 */	u64	g3fbct_int_mask;
/* 0x00010 */	u64	g3fbct_err_reg;
#define	VXGE_HAL_G3FBCT_ERR_REG_G3IF_SM_ERR		    mBIT(4)
#define	VXGE_HAL_G3FBCT_ERR_REG_G3IF_GDDR3_DECC		    mBIT(5)
#define	VXGE_HAL_G3FBCT_ERR_REG_G3IF_GDDR3_U_DECC	    mBIT(6)
#define	VXGE_HAL_G3FBCT_ERR_REG_G3IF_CTRL_FIFO_DECC	    mBIT(7)
#define	VXGE_HAL_G3FBCT_ERR_REG_G3IF_GDDR3_SECC		    mBIT(29)
#define	VXGE_HAL_G3FBCT_ERR_REG_G3IF_GDDR3_U_SECC	    mBIT(30)
#define	VXGE_HAL_G3FBCT_ERR_REG_G3IF_CTRL_FIFO_SECC	    mBIT(31)
/* 0x00018 */	u64	g3fbct_err_mask;
/* 0x00020 */	u64	g3fbct_err_alarm;
	u8	unused00a00[0x00a00-0x00028];

/* 0x00a00 */	u64	wrdma_int_status;
#define	VXGE_HAL_WRDMA_INT_STATUS_RC_ALARM_RC_INT	    mBIT(0)
#define	VXGE_HAL_WRDMA_INT_STATUS_RXDRM_SM_ERR_RXDRM_INT    mBIT(1)
#define	VXGE_HAL_WRDMA_INT_STATUS_RXDCM_SM_ERR_RXDCM_SM_INT mBIT(2)
#define	VXGE_HAL_WRDMA_INT_STATUS_RXDWM_SM_ERR_RXDWM_INT    mBIT(3)
#define	VXGE_HAL_WRDMA_INT_STATUS_RDA_ERR_RDA_INT	    mBIT(6)
#define	VXGE_HAL_WRDMA_INT_STATUS_RDA_ECC_DB_RDA_ECC_DB_INT mBIT(8)
#define	VXGE_HAL_WRDMA_INT_STATUS_RDA_ECC_SG_RDA_ECC_SG_INT mBIT(9)
#define	VXGE_HAL_WRDMA_INT_STATUS_FRF_ALARM_FRF_INT	    mBIT(12)
#define	VXGE_HAL_WRDMA_INT_STATUS_ROCRC_ALARM_ROCRC_INT	    mBIT(13)
#define	VXGE_HAL_WRDMA_INT_STATUS_WDE0_ALARM_WDE0_INT	    mBIT(14)
#define	VXGE_HAL_WRDMA_INT_STATUS_WDE1_ALARM_WDE1_INT	    mBIT(15)
#define	VXGE_HAL_WRDMA_INT_STATUS_WDE2_ALARM_WDE2_INT	    mBIT(16)
#define	VXGE_HAL_WRDMA_INT_STATUS_WDE3_ALARM_WDE3_INT	    mBIT(17)
/* 0x00a08 */	u64	wrdma_int_mask;
/* 0x00a10 */	u64	rc_alarm_reg;
#define	VXGE_HAL_RC_ALARM_REG_FTC_SM_ERR		    mBIT(0)
#define	VXGE_HAL_RC_ALARM_REG_FTC_SM_PHASE_ERR		    mBIT(1)
#define	VXGE_HAL_RC_ALARM_REG_BTDWM_SM_ERR		    mBIT(2)
#define	VXGE_HAL_RC_ALARM_REG_BTC_SM_ERR		    mBIT(3)
#define	VXGE_HAL_RC_ALARM_REG_BTDCM_SM_ERR		    mBIT(4)
#define	VXGE_HAL_RC_ALARM_REG_BTDRM_SM_ERR		    mBIT(5)
#define	VXGE_HAL_RC_ALARM_REG_RMM_RXD_RC_ECC_DB_ERR	    mBIT(6)
#define	VXGE_HAL_RC_ALARM_REG_RMM_RXD_RC_ECC_SG_ERR	    mBIT(7)
#define	VXGE_HAL_RC_ALARM_REG_RHS_RXD_RHS_ECC_DB_ERR	    mBIT(8)
#define	VXGE_HAL_RC_ALARM_REG_RHS_RXD_RHS_ECC_SG_ERR	    mBIT(9)
#define	VXGE_HAL_RC_ALARM_REG_RMM_SM_ERR		    mBIT(10)
#define	VXGE_HAL_RC_ALARM_REG_BTC_VPATH_MISMATCH_ERR	    mBIT(12)
/* 0x00a18 */	u64	rc_alarm_mask;
/* 0x00a20 */	u64	rc_alarm_alarm;
/* 0x00a28 */	u64	rxdrm_sm_err_reg;
#define	VXGE_HAL_RXDRM_SM_ERR_REG_PRC_VP(n)		    mBIT(n)
/* 0x00a30 */	u64	rxdrm_sm_err_mask;
/* 0x00a38 */	u64	rxdrm_sm_err_alarm;
/* 0x00a40 */	u64	rxdcm_sm_err_reg;
#define	VXGE_HAL_RXDCM_SM_ERR_REG_PRC_VP(n)		    mBIT(n)
/* 0x00a48 */	u64	rxdcm_sm_err_mask;
/* 0x00a50 */	u64	rxdcm_sm_err_alarm;
/* 0x00a58 */	u64	rxdwm_sm_err_reg;
#define	VXGE_HAL_RXDWM_SM_ERR_REG_PRC_VP(n)		    mBIT(n)
/* 0x00a60 */	u64	rxdwm_sm_err_mask;
/* 0x00a68 */	u64	rxdwm_sm_err_alarm;
/* 0x00a70 */	u64	rda_err_reg;
#define	VXGE_HAL_RDA_ERR_REG_RDA_SM0_ERR_ALARM		    mBIT(0)
#define	VXGE_HAL_RDA_ERR_REG_RDA_MISC_ERR		    mBIT(1)
#define	VXGE_HAL_RDA_ERR_REG_RDA_PCIX_ERR		    mBIT(2)
#define	VXGE_HAL_RDA_ERR_REG_RDA_RXD_ECC_DB_ERR		    mBIT(3)
#define	VXGE_HAL_RDA_ERR_REG_RDA_FRM_ECC_DB_ERR		    mBIT(4)
#define	VXGE_HAL_RDA_ERR_REG_RDA_UQM_ECC_DB_ERR		    mBIT(5)
#define	VXGE_HAL_RDA_ERR_REG_RDA_IMM_ECC_DB_ERR		    mBIT(6)
#define	VXGE_HAL_RDA_ERR_REG_RDA_TIM_ECC_DB_ERR		    mBIT(7)
/* 0x00a78 */	u64	rda_err_mask;
/* 0x00a80 */	u64	rda_err_alarm;
/* 0x00a88 */	u64	rda_ecc_db_reg;
#define	VXGE_HAL_RDA_ECC_DB_REG_RDA_RXD_ERR(n)		    mBIT(n)
/* 0x00a90 */	u64	rda_ecc_db_mask;
/* 0x00a98 */	u64	rda_ecc_db_alarm;
/* 0x00aa0 */	u64	rda_ecc_sg_reg;
#define	VXGE_HAL_RDA_ECC_SG_REG_RDA_RXD_ERR(n)		    mBIT(n)
/* 0x00aa8 */	u64	rda_ecc_sg_mask;
/* 0x00ab0 */	u64	rda_ecc_sg_alarm;
/* 0x00ab8 */	u64	rqa_err_reg;
#define	VXGE_HAL_RQA_ERR_REG_RQA_SM_ERR_ALARM		    mBIT(0)
/* 0x00ac0 */	u64	rqa_err_mask;
/* 0x00ac8 */	u64	rqa_err_alarm;
/* 0x00ad0 */	u64	frf_alarm_reg;
#define	VXGE_HAL_FRF_ALARM_REG_PRC_VP_FRF_SM_ERR(n)	    mBIT(n)
/* 0x00ad8 */	u64	frf_alarm_mask;
/* 0x00ae0 */	u64	frf_alarm_alarm;
/* 0x00ae8 */	u64	rocrc_alarm_reg;
#define	VXGE_HAL_ROCRC_ALARM_REG_QCQ_QCC_BYP_ECC_DB	    mBIT(0)
#define	VXGE_HAL_ROCRC_ALARM_REG_QCQ_QCC_BYP_ECC_SG	    mBIT(1)
#define	VXGE_HAL_ROCRC_ALARM_REG_NOA_NMA_SM_ERR		    mBIT(2)
#define	VXGE_HAL_ROCRC_ALARM_REG_NOA_IMMM_ECC_DB	    mBIT(3)
#define	VXGE_HAL_ROCRC_ALARM_REG_NOA_IMMM_ECC_SG	    mBIT(4)
#define	VXGE_HAL_ROCRC_ALARM_REG_UDQ_UMQM_ECC_DB	    mBIT(5)
#define	VXGE_HAL_ROCRC_ALARM_REG_UDQ_UMQM_ECC_SG	    mBIT(6)
#define	VXGE_HAL_ROCRC_ALARM_REG_NOA_RCBM_ECC_DB	    mBIT(11)
#define	VXGE_HAL_ROCRC_ALARM_REG_NOA_RCBM_ECC_SG	    mBIT(12)
#define	VXGE_HAL_ROCRC_ALARM_REG_QCQ_MULTI_EGB_RSVD_ERR	    mBIT(13)
#define	VXGE_HAL_ROCRC_ALARM_REG_QCQ_MULTI_EGB_OWN_ERR	    mBIT(14)
#define	VXGE_HAL_ROCRC_ALARM_REG_QCQ_MULTI_BYP_OWN_ERR	    mBIT(15)
#define	VXGE_HAL_ROCRC_ALARM_REG_QCQ_OWN_NOT_ASSIGNED_ERR   mBIT(16)
#define	VXGE_HAL_ROCRC_ALARM_REG_QCQ_OWN_RSVD_SYNC_ERR	    mBIT(17)
#define	VXGE_HAL_ROCRC_ALARM_REG_QCQ_LOST_EGB_ERR	    mBIT(18)
#define	VXGE_HAL_ROCRC_ALARM_REG_RCQ_BYPQ0_OVERFLOW	    mBIT(19)
#define	VXGE_HAL_ROCRC_ALARM_REG_RCQ_BYPQ1_OVERFLOW	    mBIT(20)
#define	VXGE_HAL_ROCRC_ALARM_REG_RCQ_BYPQ2_OVERFLOW	    mBIT(21)
#define	VXGE_HAL_ROCRC_ALARM_REG_NOA_WCT_CMD_FIFO_ERR	    mBIT(22)
/* 0x00af0 */	u64	rocrc_alarm_mask;
/* 0x00af8 */	u64	rocrc_alarm_alarm;
/* 0x00b00 */	u64	wde0_alarm_reg;
#define	VXGE_HAL_WDE0_ALARM_REG_WDE0_DCC_SM_ERR		    mBIT(0)
#define	VXGE_HAL_WDE0_ALARM_REG_WDE0_PRM_SM_ERR		    mBIT(1)
#define	VXGE_HAL_WDE0_ALARM_REG_WDE0_CP_SM_ERR		    mBIT(2)
#define	VXGE_HAL_WDE0_ALARM_REG_WDE0_CP_CMD_ERR		    mBIT(3)
#define	VXGE_HAL_WDE0_ALARM_REG_WDE0_PCR_SM_ERR		    mBIT(4)
/* 0x00b08 */	u64	wde0_alarm_mask;
/* 0x00b10 */	u64	wde0_alarm_alarm;
/* 0x00b18 */	u64	wde1_alarm_reg;
#define	VXGE_HAL_WDE1_ALARM_REG_WDE1_DCC_SM_ERR		    mBIT(0)
#define	VXGE_HAL_WDE1_ALARM_REG_WDE1_PRM_SM_ERR		    mBIT(1)
#define	VXGE_HAL_WDE1_ALARM_REG_WDE1_CP_SM_ERR		    mBIT(2)
#define	VXGE_HAL_WDE1_ALARM_REG_WDE1_CP_CMD_ERR		    mBIT(3)
#define	VXGE_HAL_WDE1_ALARM_REG_WDE1_PCR_SM_ERR		    mBIT(4)
/* 0x00b20 */	u64	wde1_alarm_mask;
/* 0x00b28 */	u64	wde1_alarm_alarm;
/* 0x00b30 */	u64	wde2_alarm_reg;
#define	VXGE_HAL_WDE2_ALARM_REG_WDE2_DCC_SM_ERR		    mBIT(0)
#define	VXGE_HAL_WDE2_ALARM_REG_WDE2_PRM_SM_ERR		    mBIT(1)
#define	VXGE_HAL_WDE2_ALARM_REG_WDE2_CP_SM_ERR		    mBIT(2)
#define	VXGE_HAL_WDE2_ALARM_REG_WDE2_CP_CMD_ERR		    mBIT(3)
#define	VXGE_HAL_WDE2_ALARM_REG_WDE2_PCR_SM_ERR		    mBIT(4)
/* 0x00b38 */	u64	wde2_alarm_mask;
/* 0x00b40 */	u64	wde2_alarm_alarm;
/* 0x00b48 */	u64	wde3_alarm_reg;
#define	VXGE_HAL_WDE3_ALARM_REG_WDE3_DCC_SM_ERR		    mBIT(0)
#define	VXGE_HAL_WDE3_ALARM_REG_WDE3_PRM_SM_ERR		    mBIT(1)
#define	VXGE_HAL_WDE3_ALARM_REG_WDE3_CP_SM_ERR		    mBIT(2)
#define	VXGE_HAL_WDE3_ALARM_REG_WDE3_CP_CMD_ERR		    mBIT(3)
#define	VXGE_HAL_WDE3_ALARM_REG_WDE3_PCR_SM_ERR		    mBIT(4)
/* 0x00b50 */	u64	wde3_alarm_mask;
/* 0x00b58 */	u64	wde3_alarm_alarm;
	u8	unused00be8[0x00be8-0x00b60];

/* 0x00be8 */	u64	rx_w_round_robin_0;
#define	VXGE_HAL_RX_W_ROUND_ROBIN_0_RX_W_PRIORITY_SS_0(val) vBIT(val, 3, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_0_RX_W_PRIORITY_SS_1(val) vBIT(val, 11, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_0_RX_W_PRIORITY_SS_2(val) vBIT(val, 19, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_0_RX_W_PRIORITY_SS_3(val) vBIT(val, 27, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_0_RX_W_PRIORITY_SS_4(val) vBIT(val, 35, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_0_RX_W_PRIORITY_SS_5(val) vBIT(val, 43, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_0_RX_W_PRIORITY_SS_6(val) vBIT(val, 51, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_0_RX_W_PRIORITY_SS_7(val) vBIT(val, 59, 5)
/* 0x00bf0 */	u64	rx_w_round_robin_1;
#define	VXGE_HAL_RX_W_ROUND_ROBIN_1_RX_W_PRIORITY_SS_8(val) vBIT(val, 3, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_1_RX_W_PRIORITY_SS_9(val) vBIT(val, 11, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_1_RX_W_PRIORITY_SS_10(val) vBIT(val, 19, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_1_RX_W_PRIORITY_SS_11(val) vBIT(val, 27, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_1_RX_W_PRIORITY_SS_12(val) vBIT(val, 35, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_1_RX_W_PRIORITY_SS_13(val) vBIT(val, 43, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_1_RX_W_PRIORITY_SS_14(val) vBIT(val, 51, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_1_RX_W_PRIORITY_SS_15(val) vBIT(val, 59, 5)
/* 0x00bf8 */	u64	rx_w_round_robin_2;
#define	VXGE_HAL_RX_W_ROUND_ROBIN_2_RX_W_PRIORITY_SS_16(val) vBIT(val, 3, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_2_RX_W_PRIORITY_SS_17(val) vBIT(val, 11, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_2_RX_W_PRIORITY_SS_18(val) vBIT(val, 19, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_2_RX_W_PRIORITY_SS_19(val) vBIT(val, 27, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_2_RX_W_PRIORITY_SS_20(val) vBIT(val, 35, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_2_RX_W_PRIORITY_SS_21(val) vBIT(val, 43, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_2_RX_W_PRIORITY_SS_22(val) vBIT(val, 51, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_2_RX_W_PRIORITY_SS_23(val) vBIT(val, 59, 5)
/* 0x00c00 */	u64	rx_w_round_robin_3;
#define	VXGE_HAL_RX_W_ROUND_ROBIN_3_RX_W_PRIORITY_SS_24(val) vBIT(val, 3, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_3_RX_W_PRIORITY_SS_25(val) vBIT(val, 11, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_3_RX_W_PRIORITY_SS_26(val) vBIT(val, 19, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_3_RX_W_PRIORITY_SS_27(val) vBIT(val, 27, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_3_RX_W_PRIORITY_SS_28(val) vBIT(val, 35, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_3_RX_W_PRIORITY_SS_29(val) vBIT(val, 43, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_3_RX_W_PRIORITY_SS_30(val) vBIT(val, 51, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_3_RX_W_PRIORITY_SS_31(val) vBIT(val, 59, 5)
/* 0x00c08 */	u64	rx_w_round_robin_4;
#define	VXGE_HAL_RX_W_ROUND_ROBIN_4_RX_W_PRIORITY_SS_32(val) vBIT(val, 3, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_4_RX_W_PRIORITY_SS_33(val) vBIT(val, 11, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_4_RX_W_PRIORITY_SS_34(val) vBIT(val, 19, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_4_RX_W_PRIORITY_SS_35(val) vBIT(val, 27, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_4_RX_W_PRIORITY_SS_36(val) vBIT(val, 35, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_4_RX_W_PRIORITY_SS_37(val) vBIT(val, 43, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_4_RX_W_PRIORITY_SS_38(val) vBIT(val, 51, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_4_RX_W_PRIORITY_SS_39(val) vBIT(val, 59, 5)
/* 0x00c10 */	u64	rx_w_round_robin_5;
#define	VXGE_HAL_RX_W_ROUND_ROBIN_5_RX_W_PRIORITY_SS_40(val) vBIT(val, 3, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_5_RX_W_PRIORITY_SS_41(val) vBIT(val, 11, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_5_RX_W_PRIORITY_SS_42(val) vBIT(val, 19, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_5_RX_W_PRIORITY_SS_43(val) vBIT(val, 27, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_5_RX_W_PRIORITY_SS_44(val) vBIT(val, 35, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_5_RX_W_PRIORITY_SS_45(val) vBIT(val, 43, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_5_RX_W_PRIORITY_SS_46(val) vBIT(val, 51, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_5_RX_W_PRIORITY_SS_47(val) vBIT(val, 59, 5)
/* 0x00c18 */	u64	rx_w_round_robin_6;
#define	VXGE_HAL_RX_W_ROUND_ROBIN_6_RX_W_PRIORITY_SS_48(val) vBIT(val, 3, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_6_RX_W_PRIORITY_SS_49(val) vBIT(val, 11, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_6_RX_W_PRIORITY_SS_50(val) vBIT(val, 19, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_6_RX_W_PRIORITY_SS_51(val) vBIT(val, 27, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_6_RX_W_PRIORITY_SS_52(val) vBIT(val, 35, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_6_RX_W_PRIORITY_SS_53(val) vBIT(val, 43, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_6_RX_W_PRIORITY_SS_54(val) vBIT(val, 51, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_6_RX_W_PRIORITY_SS_55(val) vBIT(val, 59, 5)
/* 0x00c20 */	u64	rx_w_round_robin_7;
#define	VXGE_HAL_RX_W_ROUND_ROBIN_7_RX_W_PRIORITY_SS_56(val) vBIT(val, 3, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_7_RX_W_PRIORITY_SS_57(val) vBIT(val, 11, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_7_RX_W_PRIORITY_SS_58(val) vBIT(val, 19, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_7_RX_W_PRIORITY_SS_59(val) vBIT(val, 27, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_7_RX_W_PRIORITY_SS_60(val) vBIT(val, 35, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_7_RX_W_PRIORITY_SS_61(val) vBIT(val, 43, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_7_RX_W_PRIORITY_SS_62(val) vBIT(val, 51, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_7_RX_W_PRIORITY_SS_63(val) vBIT(val, 59, 5)
/* 0x00c28 */	u64	rx_w_round_robin_8;
#define	VXGE_HAL_RX_W_ROUND_ROBIN_8_RX_W_PRIORITY_SS_64(val) vBIT(val, 3, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_8_RX_W_PRIORITY_SS_65(val) vBIT(val, 11, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_8_RX_W_PRIORITY_SS_66(val) vBIT(val, 19, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_8_RX_W_PRIORITY_SS_67(val) vBIT(val, 27, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_8_RX_W_PRIORITY_SS_68(val) vBIT(val, 35, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_8_RX_W_PRIORITY_SS_69(val) vBIT(val, 43, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_8_RX_W_PRIORITY_SS_70(val) vBIT(val, 51, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_8_RX_W_PRIORITY_SS_71(val) vBIT(val, 59, 5)
/* 0x00c30 */	u64	rx_w_round_robin_9;
#define	VXGE_HAL_RX_W_ROUND_ROBIN_9_RX_W_PRIORITY_SS_72(val) vBIT(val, 3, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_9_RX_W_PRIORITY_SS_73(val) vBIT(val, 11, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_9_RX_W_PRIORITY_SS_74(val) vBIT(val, 19, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_9_RX_W_PRIORITY_SS_75(val) vBIT(val, 27, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_9_RX_W_PRIORITY_SS_76(val) vBIT(val, 35, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_9_RX_W_PRIORITY_SS_77(val) vBIT(val, 43, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_9_RX_W_PRIORITY_SS_78(val) vBIT(val, 51, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_9_RX_W_PRIORITY_SS_79(val) vBIT(val, 59, 5)
/* 0x00c38 */	u64	rx_w_round_robin_10;
#define	VXGE_HAL_RX_W_ROUND_ROBIN_10_RX_W_PRIORITY_SS_80(val) vBIT(val, 3, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_10_RX_W_PRIORITY_SS_81(val) vBIT(val, 11, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_10_RX_W_PRIORITY_SS_82(val) vBIT(val, 19, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_10_RX_W_PRIORITY_SS_83(val) vBIT(val, 27, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_10_RX_W_PRIORITY_SS_84(val) vBIT(val, 35, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_10_RX_W_PRIORITY_SS_85(val) vBIT(val, 43, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_10_RX_W_PRIORITY_SS_86(val) vBIT(val, 51, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_10_RX_W_PRIORITY_SS_87(val) vBIT(val, 59, 5)
/* 0x00c40 */	u64	rx_w_round_robin_11;
#define	VXGE_HAL_RX_W_ROUND_ROBIN_11_RX_W_PRIORITY_SS_88(val) vBIT(val, 3, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_11_RX_W_PRIORITY_SS_89(val) vBIT(val, 11, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_11_RX_W_PRIORITY_SS_90(val) vBIT(val, 19, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_11_RX_W_PRIORITY_SS_91(val) vBIT(val, 27, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_11_RX_W_PRIORITY_SS_92(val) vBIT(val, 35, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_11_RX_W_PRIORITY_SS_93(val) vBIT(val, 43, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_11_RX_W_PRIORITY_SS_94(val) vBIT(val, 51, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_11_RX_W_PRIORITY_SS_95(val) vBIT(val, 59, 5)
/* 0x00c48 */	u64	rx_w_round_robin_12;
#define	VXGE_HAL_RX_W_ROUND_ROBIN_12_RX_W_PRIORITY_SS_96(val) vBIT(val, 3, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_12_RX_W_PRIORITY_SS_97(val) vBIT(val, 11, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_12_RX_W_PRIORITY_SS_98(val) vBIT(val, 19, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_12_RX_W_PRIORITY_SS_99(val) vBIT(val, 27, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_12_RX_W_PRIORITY_SS_100(val) vBIT(val, 35, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_12_RX_W_PRIORITY_SS_101(val) vBIT(val, 43, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_12_RX_W_PRIORITY_SS_102(val) vBIT(val, 51, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_12_RX_W_PRIORITY_SS_103(val) vBIT(val, 59, 5)
/* 0x00c50 */	u64	rx_w_round_robin_13;
#define	VXGE_HAL_RX_W_ROUND_ROBIN_13_RX_W_PRIORITY_SS_104(val) vBIT(val, 3, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_13_RX_W_PRIORITY_SS_105(val) vBIT(val, 11, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_13_RX_W_PRIORITY_SS_106(val) vBIT(val, 19, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_13_RX_W_PRIORITY_SS_107(val) vBIT(val, 27, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_13_RX_W_PRIORITY_SS_108(val) vBIT(val, 35, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_13_RX_W_PRIORITY_SS_109(val) vBIT(val, 43, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_13_RX_W_PRIORITY_SS_110(val) vBIT(val, 51, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_13_RX_W_PRIORITY_SS_111(val) vBIT(val, 59, 5)
/* 0x00c58 */	u64	rx_w_round_robin_14;
#define	VXGE_HAL_RX_W_ROUND_ROBIN_14_RX_W_PRIORITY_SS_112(val) vBIT(val, 3, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_14_RX_W_PRIORITY_SS_113(val) vBIT(val, 11, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_14_RX_W_PRIORITY_SS_114(val) vBIT(val, 19, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_14_RX_W_PRIORITY_SS_115(val) vBIT(val, 27, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_14_RX_W_PRIORITY_SS_116(val) vBIT(val, 35, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_14_RX_W_PRIORITY_SS_117(val) vBIT(val, 43, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_14_RX_W_PRIORITY_SS_118(val) vBIT(val, 51, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_14_RX_W_PRIORITY_SS_119(val) vBIT(val, 59, 5)
/* 0x00c60 */	u64	rx_w_round_robin_15;
#define	VXGE_HAL_RX_W_ROUND_ROBIN_15_RX_W_PRIORITY_SS_120(val) vBIT(val, 3, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_15_RX_W_PRIORITY_SS_121(val) vBIT(val, 11, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_15_RX_W_PRIORITY_SS_122(val) vBIT(val, 19, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_15_RX_W_PRIORITY_SS_123(val) vBIT(val, 27, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_15_RX_W_PRIORITY_SS_124(val) vBIT(val, 35, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_15_RX_W_PRIORITY_SS_125(val) vBIT(val, 43, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_15_RX_W_PRIORITY_SS_126(val) vBIT(val, 51, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_15_RX_W_PRIORITY_SS_127(val) vBIT(val, 59, 5)
/* 0x00c68 */	u64	rx_w_round_robin_16;
#define	VXGE_HAL_RX_W_ROUND_ROBIN_16_RX_W_PRIORITY_SS_128(val) vBIT(val, 3, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_16_RX_W_PRIORITY_SS_129(val) vBIT(val, 11, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_16_RX_W_PRIORITY_SS_130(val) vBIT(val, 19, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_16_RX_W_PRIORITY_SS_131(val) vBIT(val, 27, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_16_RX_W_PRIORITY_SS_132(val) vBIT(val, 35, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_16_RX_W_PRIORITY_SS_133(val) vBIT(val, 43, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_16_RX_W_PRIORITY_SS_134(val) vBIT(val, 51, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_16_RX_W_PRIORITY_SS_135(val) vBIT(val, 59, 5)
/* 0x00c70 */	u64	rx_w_round_robin_17;
#define	VXGE_HAL_RX_W_ROUND_ROBIN_17_RX_W_PRIORITY_SS_136(val) vBIT(val, 3, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_17_RX_W_PRIORITY_SS_137(val) vBIT(val, 11, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_17_RX_W_PRIORITY_SS_138(val) vBIT(val, 19, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_17_RX_W_PRIORITY_SS_139(val) vBIT(val, 27, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_17_RX_W_PRIORITY_SS_140(val) vBIT(val, 35, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_17_RX_W_PRIORITY_SS_141(val) vBIT(val, 43, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_17_RX_W_PRIORITY_SS_142(val) vBIT(val, 51, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_17_RX_W_PRIORITY_SS_143(val) vBIT(val, 59, 5)
/* 0x00c78 */	u64	rx_w_round_robin_18;
#define	VXGE_HAL_RX_W_ROUND_ROBIN_18_RX_W_PRIORITY_SS_144(val) vBIT(val, 3, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_18_RX_W_PRIORITY_SS_145(val) vBIT(val, 11, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_18_RX_W_PRIORITY_SS_146(val) vBIT(val, 19, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_18_RX_W_PRIORITY_SS_147(val) vBIT(val, 27, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_18_RX_W_PRIORITY_SS_148(val) vBIT(val, 35, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_18_RX_W_PRIORITY_SS_149(val) vBIT(val, 43, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_18_RX_W_PRIORITY_SS_150(val) vBIT(val, 51, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_18_RX_W_PRIORITY_SS_151(val) vBIT(val, 59, 5)
/* 0x00c80 */	u64	rx_w_round_robin_19;
#define	VXGE_HAL_RX_W_ROUND_ROBIN_19_RX_W_PRIORITY_SS_152(val) vBIT(val, 3, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_19_RX_W_PRIORITY_SS_153(val) vBIT(val, 11, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_19_RX_W_PRIORITY_SS_154(val) vBIT(val, 19, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_19_RX_W_PRIORITY_SS_155(val) vBIT(val, 27, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_19_RX_W_PRIORITY_SS_156(val) vBIT(val, 35, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_19_RX_W_PRIORITY_SS_157(val) vBIT(val, 43, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_19_RX_W_PRIORITY_SS_158(val) vBIT(val, 51, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_19_RX_W_PRIORITY_SS_159(val) vBIT(val, 59, 5)
/* 0x00c88 */	u64	rx_w_round_robin_20;
#define	VXGE_HAL_RX_W_ROUND_ROBIN_20_RX_W_PRIORITY_SS_160(val) vBIT(val, 3, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_20_RX_W_PRIORITY_SS_161(val) vBIT(val, 11, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_20_RX_W_PRIORITY_SS_162(val) vBIT(val, 19, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_20_RX_W_PRIORITY_SS_163(val) vBIT(val, 27, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_20_RX_W_PRIORITY_SS_164(val) vBIT(val, 35, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_20_RX_W_PRIORITY_SS_165(val) vBIT(val, 43, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_20_RX_W_PRIORITY_SS_166(val) vBIT(val, 51, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_20_RX_W_PRIORITY_SS_167(val) vBIT(val, 59, 5)
/* 0x00c90 */	u64	rx_w_round_robin_21;
#define	VXGE_HAL_RX_W_ROUND_ROBIN_21_RX_W_PRIORITY_SS_168(val) vBIT(val, 3, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_21_RX_W_PRIORITY_SS_169(val) vBIT(val, 11, 5)
#define	VXGE_HAL_RX_W_ROUND_ROBIN_21_RX_W_PRIORITY_SS_170(val) vBIT(val, 19, 5)
/* 0x00c98 */	u64	rx_queue_priority_0;
#define	VXGE_HAL_RX_QUEUE_PRIORITY_0_RX_Q_NUMBER_0(val)	    vBIT(val, 3, 5)
#define	VXGE_HAL_RX_QUEUE_PRIORITY_0_RX_Q_NUMBER_1(val)	    vBIT(val, 11, 5)
#define	VXGE_HAL_RX_QUEUE_PRIORITY_0_RX_Q_NUMBER_2(val)	    vBIT(val, 19, 5)
#define	VXGE_HAL_RX_QUEUE_PRIORITY_0_RX_Q_NUMBER_3(val)	    vBIT(val, 27, 5)
#define	VXGE_HAL_RX_QUEUE_PRIORITY_0_RX_Q_NUMBER_4(val)	    vBIT(val, 35, 5)
#define	VXGE_HAL_RX_QUEUE_PRIORITY_0_RX_Q_NUMBER_5(val)	    vBIT(val, 43, 5)
#define	VXGE_HAL_RX_QUEUE_PRIORITY_0_RX_Q_NUMBER_6(val)	    vBIT(val, 51, 5)
#define	VXGE_HAL_RX_QUEUE_PRIORITY_0_RX_Q_NUMBER_7(val)	    vBIT(val, 59, 5)
/* 0x00ca0 */	u64	rx_queue_priority_1;
#define	VXGE_HAL_RX_QUEUE_PRIORITY_1_RX_Q_NUMBER_8(val)	    vBIT(val, 3, 5)
#define	VXGE_HAL_RX_QUEUE_PRIORITY_1_RX_Q_NUMBER_9(val)	    vBIT(val, 11, 5)
#define	VXGE_HAL_RX_QUEUE_PRIORITY_1_RX_Q_NUMBER_10(val)    vBIT(val, 19, 5)
#define	VXGE_HAL_RX_QUEUE_PRIORITY_1_RX_Q_NUMBER_11(val)    vBIT(val, 27, 5)
#define	VXGE_HAL_RX_QUEUE_PRIORITY_1_RX_Q_NUMBER_12(val)    vBIT(val, 35, 5)
#define	VXGE_HAL_RX_QUEUE_PRIORITY_1_RX_Q_NUMBER_13(val)    vBIT(val, 43, 5)
#define	VXGE_HAL_RX_QUEUE_PRIORITY_1_RX_Q_NUMBER_14(val)    vBIT(val, 51, 5)
#define	VXGE_HAL_RX_QUEUE_PRIORITY_1_RX_Q_NUMBER_15(val)    vBIT(val, 59, 5)
/* 0x00ca8 */	u64	rx_queue_priority_2;
#define	VXGE_HAL_RX_QUEUE_PRIORITY_2_RX_Q_NUMBER_16(val)    vBIT(val, 3, 5)
	u8	unused00cc8[0x00cc8-0x00cb0];

/* 0x00cc8 */	u64	replication_queue_priority;
#define	VXGE_HAL_REPLICATION_QUEUE_PRIORITY_REPLICATION_QUEUE_PRIORITY(val)\
							    vBIT(val, 59, 5)
/* 0x00cd0 */	u64	rx_queue_select;
#define	VXGE_HAL_RX_QUEUE_SELECT_NUMBER(n)		    mBIT(n)
#define	VXGE_HAL_RX_QUEUE_SELECT_ENABLE_CODE		    mBIT(15)
#define	VXGE_HAL_RX_QUEUE_SELECT_ENABLE_HIERARCHICAL_PRTY   mBIT(23)
/* 0x00cd8 */	u64	rqa_vpbp_ctrl;
#define	VXGE_HAL_RQA_VPBP_CTRL_WR_XON_DIS		    mBIT(15)
#define	VXGE_HAL_RQA_VPBP_CTRL_ROCRC_DIS		    mBIT(23)
#define	VXGE_HAL_RQA_VPBP_CTRL_TXPE_DIS	mBIT(31)
/* 0x00ce0 */	u64	rx_multi_cast_ctrl;
#define	VXGE_HAL_RX_MULTI_CAST_CTRL_TIME_OUT_DIS	    mBIT(0)
#define	VXGE_HAL_RX_MULTI_CAST_CTRL_FRM_DROP_DIS	    mBIT(1)
#define	VXGE_HAL_RX_MULTI_CAST_CTRL_NO_RXD_TIME_OUT_CNT(val) vBIT(val, 2, 30)
#define	VXGE_HAL_RX_MULTI_CAST_CTRL_TIME_OUT_CNT(val)	    vBIT(val, 32, 32)
/* 0x00ce8 */	u64	wde_prm_ctrl;
#define	VXGE_HAL_WDE_PRM_CTRL_SPAV_THRESHOLD(val)	    vBIT(val, 2, 10)
#define	VXGE_HAL_WDE_PRM_CTRL_SPLIT_THRESHOLD(val)	    vBIT(val, 18, 14)
#define	VXGE_HAL_WDE_PRM_CTRL_SPLIT_ON_1ST_ROW		    mBIT(32)
#define	VXGE_HAL_WDE_PRM_CTRL_SPLIT_ON_ROW_BNDRY	    mBIT(33)
#define	VXGE_HAL_WDE_PRM_CTRL_FB_ROW_SIZE(val)		    vBIT(val, 46, 2)
/* 0x00cf0 */	u64	noa_ctrl;
#define	VXGE_HAL_NOA_CTRL_FRM_PRTY_QUOTA(val)		    vBIT(val, 3, 5)
#define	VXGE_HAL_NOA_CTRL_NON_FRM_PRTY_QUOTA(val)	    vBIT(val, 11, 5)
#define	VXGE_HAL_NOA_CTRL_IGNORE_KDFC_IF_STATUS		    mBIT(16)
#define	VXGE_HAL_NOA_CTRL_MAX_JOB_CNT_FOR_WDE0(val)	    vBIT(val, 37, 4)
#define	VXGE_HAL_NOA_CTRL_MAX_JOB_CNT_FOR_WDE1(val)	    vBIT(val, 45, 4)
#define	VXGE_HAL_NOA_CTRL_MAX_JOB_CNT_FOR_WDE2(val)	    vBIT(val, 53, 4)
#define	VXGE_HAL_NOA_CTRL_MAX_JOB_CNT_FOR_WDE3(val)	    vBIT(val, 60, 4)
/* 0x00cf8 */	u64	phase_cfg;
#define	VXGE_HAL_PHASE_CFG_QCC_WR_PHASE_EN		    mBIT(0)
#define	VXGE_HAL_PHASE_CFG_QCC_RD_PHASE_EN		    mBIT(3)
#define	VXGE_HAL_PHASE_CFG_IMMM_WR_PHASE_EN		    mBIT(7)
#define	VXGE_HAL_PHASE_CFG_IMMM_RD_PHASE_EN		    mBIT(11)
#define	VXGE_HAL_PHASE_CFG_UMQM_WR_PHASE_EN		    mBIT(15)
#define	VXGE_HAL_PHASE_CFG_UMQM_RD_PHASE_EN		    mBIT(19)
#define	VXGE_HAL_PHASE_CFG_RCBM_WR_PHASE_EN		    mBIT(23)
#define	VXGE_HAL_PHASE_CFG_RCBM_RD_PHASE_EN		    mBIT(27)
#define	VXGE_HAL_PHASE_CFG_RXD_RC_WR_PHASE_EN		    mBIT(31)
#define	VXGE_HAL_PHASE_CFG_RXD_RC_RD_PHASE_EN		    mBIT(35)
#define	VXGE_HAL_PHASE_CFG_RXD_RHS_WR_PHASE_EN		    mBIT(39)
#define	VXGE_HAL_PHASE_CFG_RXD_RHS_RD_PHASE_EN		    mBIT(43)
/* 0x00d00 */	u64	rcq_bypq_cfg;
#define	VXGE_HAL_RCQ_BYPQ_CFG_OVERFLOW_THRESHOLD(val)	    vBIT(val, 10, 22)
#define	VXGE_HAL_RCQ_BYPQ_CFG_BYP_ON_THRESHOLD(val)	    vBIT(val, 39, 9)
#define	VXGE_HAL_RCQ_BYPQ_CFG_BYP_OFF_THRESHOLD(val)	    vBIT(val, 55, 9)
	u8	unused00e00[0x00e00-0x00d08];

/* 0x00e00 */	u64	doorbell_int_status;
#define	VXGE_HAL_DOORBELL_INT_STATUS_KDFC_ERR_REG_TXDMA_KDFC_INT mBIT(7)
#define	VXGE_HAL_DOORBELL_INT_STATUS_USDC_ERR_REG_TXDMA_USDC_INT mBIT(15)
/* 0x00e08 */	u64	doorbell_int_mask;
/* 0x00e10 */	u64	kdfc_err_reg;
#define	VXGE_HAL_KDFC_ERR_REG_KDFC_KDFC_ECC_SG_ERR	    mBIT(7)
#define	VXGE_HAL_KDFC_ERR_REG_KDFC_KDFC_ECC_DB_ERR	    mBIT(15)
#define	VXGE_HAL_KDFC_ERR_REG_KDFC_KDFC_SM_ERR_ALARM	    mBIT(23)
#define	VXGE_HAL_KDFC_ERR_REG_KDFC_KDFC_MISC_ERR_1	    mBIT(32)
#define	VXGE_HAL_KDFC_ERR_REG_KDFC_KDFC_PCIX_ERR	    mBIT(39)
/* 0x00e18 */	u64	kdfc_err_mask;
/* 0x00e20 */	u64	kdfc_err_reg_alarm;
#define	VXGE_HAL_KDFC_ERR_REG_ALARM_KDFC_KDFC_ECC_SG_ERR    mBIT(7)
#define	VXGE_HAL_KDFC_ERR_REG_ALARM_KDFC_KDFC_ECC_DB_ERR    mBIT(15)
#define	VXGE_HAL_KDFC_ERR_REG_ALARM_KDFC_KDFC_SM_ERR_ALARM  mBIT(23)
#define	VXGE_HAL_KDFC_ERR_REG_ALARM_KDFC_KDFC_MISC_ERR_1    mBIT(32)
#define	VXGE_HAL_KDFC_ERR_REG_ALARM_KDFC_KDFC_PCIX_ERR	    mBIT(39)
	u8	unused00e40[0x00e40-0x00e28];

/* 0x00e40 */	u64	kdfc_vp_partition_0;
#define	VXGE_HAL_KDFC_VP_PARTITION_0_ENABLE		    mBIT(0)
#define	VXGE_HAL_KDFC_VP_PARTITION_0_NUMBER_0(val)	    vBIT(val, 5, 3)
#define	VXGE_HAL_KDFC_VP_PARTITION_0_LENGTH_0(val)	    vBIT(val, 17, 15)
#define	VXGE_HAL_KDFC_VP_PARTITION_0_NUMBER_1(val)	    vBIT(val, 37, 3)
#define	VXGE_HAL_KDFC_VP_PARTITION_0_LENGTH_1(val)	    vBIT(val, 49, 15)
/* 0x00e48 */	u64	kdfc_vp_partition_1;
#define	VXGE_HAL_KDFC_VP_PARTITION_1_NUMBER_2(val)	    vBIT(val, 5, 3)
#define	VXGE_HAL_KDFC_VP_PARTITION_1_LENGTH_2(val)	    vBIT(val, 17, 15)
#define	VXGE_HAL_KDFC_VP_PARTITION_1_NUMBER_3(val)	    vBIT(val, 37, 3)
#define	VXGE_HAL_KDFC_VP_PARTITION_1_LENGTH_3(val)	    vBIT(val, 49, 15)
/* 0x00e50 */	u64	kdfc_vp_partition_2;
#define	VXGE_HAL_KDFC_VP_PARTITION_2_NUMBER_4(val)	    vBIT(val, 5, 3)
#define	VXGE_HAL_KDFC_VP_PARTITION_2_LENGTH_4(val)	    vBIT(val, 17, 15)
#define	VXGE_HAL_KDFC_VP_PARTITION_2_NUMBER_5(val)	    vBIT(val, 37, 3)
#define	VXGE_HAL_KDFC_VP_PARTITION_2_LENGTH_5(val)	    vBIT(val, 49, 15)
/* 0x00e58 */	u64	kdfc_vp_partition_3;
#define	VXGE_HAL_KDFC_VP_PARTITION_3_NUMBER_6(val)	    vBIT(val, 5, 3)
#define	VXGE_HAL_KDFC_VP_PARTITION_3_LENGTH_6(val)	    vBIT(val, 17, 15)
#define	VXGE_HAL_KDFC_VP_PARTITION_3_NUMBER_7(val)	    vBIT(val, 37, 3)
#define	VXGE_HAL_KDFC_VP_PARTITION_3_LENGTH_7(val)	    vBIT(val, 49, 15)
/* 0x00e60 */	u64	kdfc_vp_partition_4;
#define	VXGE_HAL_KDFC_VP_PARTITION_4_LENGTH_8(val)	    vBIT(val, 17, 15)
#define	VXGE_HAL_KDFC_VP_PARTITION_4_LENGTH_9(val)	    vBIT(val, 49, 15)
/* 0x00e68 */	u64	kdfc_vp_partition_5;
#define	VXGE_HAL_KDFC_VP_PARTITION_5_LENGTH_10(val)	    vBIT(val, 17, 15)
#define	VXGE_HAL_KDFC_VP_PARTITION_5_LENGTH_11(val)	    vBIT(val, 49, 15)
/* 0x00e70 */	u64	kdfc_vp_partition_6;
#define	VXGE_HAL_KDFC_VP_PARTITION_6_LENGTH_12(val)	    vBIT(val, 17, 15)
#define	VXGE_HAL_KDFC_VP_PARTITION_6_LENGTH_13(val)	    vBIT(val, 49, 15)
/* 0x00e78 */	u64	kdfc_vp_partition_7;
#define	VXGE_HAL_KDFC_VP_PARTITION_7_LENGTH_14(val)	    vBIT(val, 17, 15)
#define	VXGE_HAL_KDFC_VP_PARTITION_7_LENGTH_15(val)	    vBIT(val, 49, 15)
/* 0x00e80 */	u64	kdfc_vp_partition_8;
#define	VXGE_HAL_KDFC_VP_PARTITION_8_LENGTH_16(val)	    vBIT(val, 17, 15)
/* 0x00e88 */	u64	kdfc_w_round_robin_0;
#define	VXGE_HAL_KDFC_W_ROUND_ROBIN_0_NUMBER_0(val)	    vBIT(val, 3, 5)
#define	VXGE_HAL_KDFC_W_ROUND_ROBIN_0_NUMBER_1(val)	    vBIT(val, 11, 5)
#define	VXGE_HAL_KDFC_W_ROUND_ROBIN_0_NUMBER_2(val)	    vBIT(val, 19, 5)
#define	VXGE_HAL_KDFC_W_ROUND_ROBIN_0_NUMBER_3(val)	    vBIT(val, 27, 5)
#define	VXGE_HAL_KDFC_W_ROUND_ROBIN_0_NUMBER_4(val)	    vBIT(val, 35, 5)
#define	VXGE_HAL_KDFC_W_ROUND_ROBIN_0_NUMBER_5(val)	    vBIT(val, 43, 5)
#define	VXGE_HAL_KDFC_W_ROUND_ROBIN_0_NUMBER_6(val)	    vBIT(val, 51, 5)
#define	VXGE_HAL_KDFC_W_ROUND_ROBIN_0_NUMBER_7(val)	    vBIT(val, 59, 5)
		u8	unused00f28[0x00f28-0x00e90];

/* 0x00f28 */	u64	kdfc_w_round_robin_20;
#define	VXGE_HAL_KDFC_W_ROUND_ROBIN_20_NUMBER_0(val)	    vBIT(val, 3, 5)
#define	VXGE_HAL_KDFC_W_ROUND_ROBIN_20_NUMBER_1(val)	    vBIT(val, 11, 5)
#define	VXGE_HAL_KDFC_W_ROUND_ROBIN_20_NUMBER_2(val)	    vBIT(val, 19, 5)
#define	VXGE_HAL_KDFC_W_ROUND_ROBIN_20_NUMBER_3(val)	    vBIT(val, 27, 5)
#define	VXGE_HAL_KDFC_W_ROUND_ROBIN_20_NUMBER_4(val)	    vBIT(val, 35, 5)
#define	VXGE_HAL_KDFC_W_ROUND_ROBIN_20_NUMBER_5(val)	    vBIT(val, 43, 5)
#define	VXGE_HAL_KDFC_W_ROUND_ROBIN_20_NUMBER_6(val)	    vBIT(val, 51, 5)
#define	VXGE_HAL_KDFC_W_ROUND_ROBIN_20_NUMBER_7(val)	    vBIT(val, 59, 5)
		u8	unused00fc8[0x00fc8-0x00f30];

/* 0x00fc8 */	u64	kdfc_w_round_robin_40;
#define	VXGE_HAL_KDFC_W_ROUND_ROBIN_40_NUMBER_0(val)	    vBIT(val, 3, 5)
#define	VXGE_HAL_KDFC_W_ROUND_ROBIN_40_NUMBER_1(val)	    vBIT(val, 11, 5)
#define	VXGE_HAL_KDFC_W_ROUND_ROBIN_40_NUMBER_2(val)	    vBIT(val, 19, 5)
#define	VXGE_HAL_KDFC_W_ROUND_ROBIN_40_NUMBER_3(val)	    vBIT(val, 27, 5)
#define	VXGE_HAL_KDFC_W_ROUND_ROBIN_40_NUMBER_4(val)	    vBIT(val, 35, 5)
#define	VXGE_HAL_KDFC_W_ROUND_ROBIN_40_NUMBER_5(val)	    vBIT(val, 43, 5)
#define	VXGE_HAL_KDFC_W_ROUND_ROBIN_40_NUMBER_6(val)	    vBIT(val, 51, 5)
#define	VXGE_HAL_KDFC_W_ROUND_ROBIN_40_NUMBER_7(val)	    vBIT(val, 59, 5)
		u8	unused01068[0x01068-0x0fd0];

/* 0x01068 */	u64	kdfc_entry_type_sel_0;
#define	VXGE_HAL_KDFC_ENTRY_TYPE_SEL_0_NUMBER_0(val)	    vBIT(val, 6, 2)
#define	VXGE_HAL_KDFC_ENTRY_TYPE_SEL_0_NUMBER_1(val)	    vBIT(val, 14, 2)
#define	VXGE_HAL_KDFC_ENTRY_TYPE_SEL_0_NUMBER_2(val)	    vBIT(val, 22, 2)
#define	VXGE_HAL_KDFC_ENTRY_TYPE_SEL_0_NUMBER_3(val)	    vBIT(val, 30, 2)
#define	VXGE_HAL_KDFC_ENTRY_TYPE_SEL_0_NUMBER_4(val)	    vBIT(val, 38, 2)
#define	VXGE_HAL_KDFC_ENTRY_TYPE_SEL_0_NUMBER_5(val)	    vBIT(val, 46, 2)
#define	VXGE_HAL_KDFC_ENTRY_TYPE_SEL_0_NUMBER_6(val)	    vBIT(val, 54, 2)
#define	VXGE_HAL_KDFC_ENTRY_TYPE_SEL_0_NUMBER_7(val)	    vBIT(val, 62, 2)
/* 0x01070 */	u64	kdfc_entry_type_sel_1;
#define	VXGE_HAL_KDFC_ENTRY_TYPE_SEL_1_NUMBER_8(val)	    vBIT(val, 6, 2)
/* 0x01078 */	u64	kdfc_fifo_0_ctrl;
#define	VXGE_HAL_KDFC_FIFO_0_CTRL_WRR_NUMBER(val)	    vBIT(val, 3, 5)
		u8	unused01100[0x01100-0x01080];

/* 0x01100 */	u64	kdfc_fifo_17_ctrl;
#define	VXGE_HAL_KDFC_FIFO_17_CTRL_WRR_NUMBER(val)	    vBIT(val, 3, 5)
	u8	unused01600[0x01600-0x01108];

/* 0x01600 */	u64	rxmac_int_status;
#define	VXGE_HAL_RXMAC_INT_STATUS_RXMAC_GEN_ERR_RXMAC_GEN_INT mBIT(3)
#define	VXGE_HAL_RXMAC_INT_STATUS_RXMAC_ECC_ERR_RXMAC_ECC_INT mBIT(7)
#define	VXGE_HAL_RXMAC_INT_STATUS_RXMAC_VARIOUS_ERR_RXMAC_VARIOUS_INT mBIT(11)
/* 0x01608 */	u64	rxmac_int_mask;
	u8	unused01618[0x01618-0x01610];

/* 0x01618 */	u64	rxmac_gen_err_reg;
/* 0x01620 */	u64	rxmac_gen_err_mask;
/* 0x01628 */	u64	rxmac_gen_err_alarm;
/* 0x01630 */	u64	rxmac_ecc_err_reg;
#define	VXGE_HAL_RXMAC_ECC_ERR_REG_RMAC_PORT0_RMAC_RTS_PART_SG_ERR(val)\
							    vBIT(val, 0, 4)
#define	VXGE_HAL_RXMAC_ECC_ERR_REG_RMAC_PORT0_RMAC_RTS_PART_DB_ERR(val)\
							    vBIT(val, 4, 4)
#define	VXGE_HAL_RXMAC_ECC_ERR_REG_RMAC_PORT1_RMAC_RTS_PART_SG_ERR(val)\
							    vBIT(val, 8, 4)
#define	VXGE_HAL_RXMAC_ECC_ERR_REG_RMAC_PORT1_RMAC_RTS_PART_DB_ERR(val)\
							    vBIT(val, 12, 4)
#define	VXGE_HAL_RXMAC_ECC_ERR_REG_RMAC_PORT2_RMAC_RTS_PART_SG_ERR(val)\
							    vBIT(val, 16, 4)
#define	VXGE_HAL_RXMAC_ECC_ERR_REG_RMAC_PORT2_RMAC_RTS_PART_DB_ERR(val)\
							    vBIT(val, 20, 4)
#define	VXGE_HAL_RXMAC_ECC_ERR_REG_RTSJ_RMAC_DA_LKP_PRT0_SG_ERR(val)\
							    vBIT(val, 24, 2)
#define	VXGE_HAL_RXMAC_ECC_ERR_REG_RTSJ_RMAC_DA_LKP_PRT0_DB_ERR(val)\
							    vBIT(val, 26, 2)
#define	VXGE_HAL_RXMAC_ECC_ERR_REG_RTSJ_RMAC_DA_LKP_PRT1_SG_ERR(val)\
							    vBIT(val, 28, 2)
#define	VXGE_HAL_RXMAC_ECC_ERR_REG_RTSJ_RMAC_DA_LKP_PRT1_DB_ERR(val)\
							    vBIT(val, 30, 2)
#define	VXGE_HAL_RXMAC_ECC_ERR_REG_RTSJ_RMAC_VID_LKP_SG_ERR	mBIT(32)
#define	VXGE_HAL_RXMAC_ECC_ERR_REG_RTSJ_RMAC_VID_LKP_DB_ERR	mBIT(33)
#define	VXGE_HAL_RXMAC_ECC_ERR_REG_RTSJ_RMAC_PN_LKP_PRT0_SG_ERR	mBIT(34)
#define	VXGE_HAL_RXMAC_ECC_ERR_REG_RTSJ_RMAC_PN_LKP_PRT0_DB_ERR	mBIT(35)
#define	VXGE_HAL_RXMAC_ECC_ERR_REG_RTSJ_RMAC_PN_LKP_PRT1_SG_ERR	mBIT(36)
#define	VXGE_HAL_RXMAC_ECC_ERR_REG_RTSJ_RMAC_PN_LKP_PRT1_DB_ERR	mBIT(37)
#define	VXGE_HAL_RXMAC_ECC_ERR_REG_RTSJ_RMAC_PN_LKP_PRT2_SG_ERR	mBIT(38)
#define	VXGE_HAL_RXMAC_ECC_ERR_REG_RTSJ_RMAC_PN_LKP_PRT2_DB_ERR	mBIT(39)
#define	VXGE_HAL_RXMAC_ECC_ERR_REG_RTSJ_RMAC_RTH_MASK_SG_ERR(val)\
							    vBIT(val, 40, 7)
#define	VXGE_HAL_RXMAC_ECC_ERR_REG_RTSJ_RMAC_RTH_MASK_DB_ERR(val)\
							    vBIT(val, 47, 7)
#define	VXGE_HAL_RXMAC_ECC_ERR_REG_RTSJ_RMAC_RTH_LKP_SG_ERR(val)\
							    vBIT(val, 54, 3)
#define	VXGE_HAL_RXMAC_ECC_ERR_REG_RTSJ_RMAC_RTH_LKP_DB_ERR(val)\
							    vBIT(val, 57, 3)
#define	VXGE_HAL_RXMAC_ECC_ERR_REG_RTSJ_RMAC_DS_LKP_SG_ERR  mBIT(60)
#define	VXGE_HAL_RXMAC_ECC_ERR_REG_RTSJ_RMAC_DS_LKP_DB_ERR  mBIT(61)
/* 0x01638 */	u64	rxmac_ecc_err_mask;
/* 0x01640 */	u64	rxmac_ecc_err_alarm;
/* 0x01648 */	u64	rxmac_various_err_reg;
#define	VXGE_HAL_RXMAC_VARIOUS_ERR_REG_RMAC_RMAC_PORT0_FSM_ERR mBIT(0)
#define	VXGE_HAL_RXMAC_VARIOUS_ERR_REG_RMAC_RMAC_PORT1_FSM_ERR mBIT(1)
#define	VXGE_HAL_RXMAC_VARIOUS_ERR_REG_RMAC_RMAC_PORT2_FSM_ERR mBIT(2)
#define	VXGE_HAL_RXMAC_VARIOUS_ERR_REG_RMACJ_RMACJ_FSM_ERR  mBIT(3)
/* 0x01650 */	u64	rxmac_various_err_mask;
/* 0x01658 */	u64	rxmac_various_err_alarm;
/* 0x01660 */	u64	rxmac_gen_cfg;
#define	VXGE_HAL_RXMAC_GEN_CFG_SCALE_RMAC_UTIL		    mBIT(11)
/* 0x01668 */	u64	rxmac_authorize_all_addr;
#define	VXGE_HAL_RXMAC_AUTHORIZE_ALL_ADDR_VP(n)		    mBIT(n)
/* 0x01670 */	u64	rxmac_authorize_all_vid;
#define	VXGE_HAL_RXMAC_AUTHORIZE_ALL_VID_VP(n)		    mBIT(n)
	u8	unused016c0[0x016c0-0x01678];

/* 0x016c0 */	u64	rxmac_red_rate_repl_queue;
#define	VXGE_HAL_RXMAC_RED_RATE_REPL_QUEUE_CRATE_THR0(val)  vBIT(val, 0, 4)
#define	VXGE_HAL_RXMAC_RED_RATE_REPL_QUEUE_CRATE_THR1(val)  vBIT(val, 4, 4)
#define	VXGE_HAL_RXMAC_RED_RATE_REPL_QUEUE_CRATE_THR2(val)  vBIT(val, 8, 4)
#define	VXGE_HAL_RXMAC_RED_RATE_REPL_QUEUE_CRATE_THR3(val)  vBIT(val, 12, 4)
#define	VXGE_HAL_RXMAC_RED_RATE_REPL_QUEUE_FRATE_THR0(val)  vBIT(val, 16, 4)
#define	VXGE_HAL_RXMAC_RED_RATE_REPL_QUEUE_FRATE_THR1(val)  vBIT(val, 20, 4)
#define	VXGE_HAL_RXMAC_RED_RATE_REPL_QUEUE_FRATE_THR2(val)  vBIT(val, 24, 4)
#define	VXGE_HAL_RXMAC_RED_RATE_REPL_QUEUE_FRATE_THR3(val)  vBIT(val, 28, 4)
#define	VXGE_HAL_RXMAC_RED_RATE_REPL_QUEUE_TRICKLE_EN	    mBIT(35)
	u8	unused016e0[0x016e0-0x016c8];

/* 0x016e0 */	u64	rxmac_cfg0_port[3];
#define	VXGE_HAL_RXMAC_CFG0_PORT_RMAC_EN		    mBIT(3)
#define	VXGE_HAL_RXMAC_CFG0_PORT_STRIP_FCS		    mBIT(7)
#define	VXGE_HAL_RXMAC_CFG0_PORT_DISCARD_PFRM		    mBIT(11)
#define	VXGE_HAL_RXMAC_CFG0_PORT_IGNORE_FCS_ERR		    mBIT(15)
#define	VXGE_HAL_RXMAC_CFG0_PORT_IGNORE_LONG_ERR	    mBIT(19)
#define	VXGE_HAL_RXMAC_CFG0_PORT_IGNORE_USIZED_ERR	    mBIT(23)
#define	VXGE_HAL_RXMAC_CFG0_PORT_IGNORE_LEN_MISMATCH	    mBIT(27)
#define	VXGE_HAL_RXMAC_CFG0_PORT_MAX_PYLD_LEN(val)	    vBIT(val, 50, 14)
	u8	unused01710[0x01710-0x016f8];

/* 0x01710 */	u64	rxmac_cfg2_port[3];
#define	VXGE_HAL_RXMAC_CFG2_PORT_PROM_EN		    mBIT(3)
/* 0x01728 */	u64	rxmac_pause_cfg_port[3];
#define	VXGE_HAL_RXMAC_PAUSE_CFG_PORT_GEN_EN		    mBIT(3)
#define	VXGE_HAL_RXMAC_PAUSE_CFG_PORT_RCV_EN		    mBIT(7)
#define	VXGE_HAL_RXMAC_PAUSE_CFG_PORT_ACCEL_SEND(val)	    vBIT(val, 9, 3)
#define	VXGE_HAL_RXMAC_PAUSE_CFG_PORT_DUAL_THR		    mBIT(15)
#define	VXGE_HAL_RXMAC_PAUSE_CFG_PORT_HIGH_PTIME(val)	    vBIT(val, 20, 16)
#define	VXGE_HAL_RXMAC_PAUSE_CFG_PORT_IGNORE_PF_FCS_ERR	    mBIT(39)
#define	VXGE_HAL_RXMAC_PAUSE_CFG_PORT_IGNORE_PF_LEN_ERR	    mBIT(43)
#define	VXGE_HAL_RXMAC_PAUSE_CFG_PORT_LIMITER_EN	    mBIT(47)
#define	VXGE_HAL_RXMAC_PAUSE_CFG_PORT_MAX_LIMIT(val)	    vBIT(val, 48, 8)
#define	VXGE_HAL_RXMAC_PAUSE_CFG_PORT_PERMIT_RATEMGMT_CTRL  mBIT(59)
	u8	unused01758[0x01758-0x01740];

/* 0x01758 */	u64	rxmac_red_cfg0_port[3];
#define	VXGE_HAL_RXMAC_RED_CFG0_PORT_RED_EN_VP(n)	    mBIT(n)
/* 0x01770 */	u64	rxmac_red_cfg1_port[3];
#define	VXGE_HAL_RXMAC_RED_CFG1_PORT_FINE_EN		    mBIT(3)
#define	VXGE_HAL_RXMAC_RED_CFG1_PORT_RED_EN_REPL_QUEUE	    mBIT(11)
/* 0x01788 */	u64	rxmac_red_cfg2_port[3];
#define	VXGE_HAL_RXMAC_RED_CFG2_PORT_TRICKLE_EN_VP(n)	    mBIT(n)
/* 0x017a0 */	u64	rxmac_link_util_port[3];
#define	VXGE_HAL_RXMAC_LINK_UTIL_PORT_RMAC_RMAC_UTILIZATION(val) vBIT(val, 1, 7)
#define	VXGE_HAL_RXMAC_LINK_UTIL_PORT_RMAC_UTIL_CFG(val)    vBIT(val, 8, 4)
#define	VXGE_HAL_RXMAC_LINK_UTIL_PORT_RMAC_RMAC_FRAC_UTIL(val) vBIT(val, 12, 4)
#define	VXGE_HAL_RXMAC_LINK_UTIL_PORT_RMAC_PKT_WEIGHT(val)  vBIT(val, 16, 4)
#define	VXGE_HAL_RXMAC_LINK_UTIL_PORT_RMAC_RMAC_SCALE_FACTOR mBIT(23)
	u8	unused017d0[0x017d0-0x017b8];

/* 0x017d0 */	u64	rxmac_status_port[3];
#define	VXGE_HAL_RXMAC_STATUS_PORT_RMAC_RX_FRM_RCVD	    mBIT(3)
	u8	unused01800[0x01800-0x017e8];

/* 0x01800 */	u64	rxmac_rx_pa_cfg0;
#define	VXGE_HAL_RXMAC_RX_PA_CFG0_IGNORE_FRAME_ERR	    mBIT(3)
#define	VXGE_HAL_RXMAC_RX_PA_CFG0_SUPPORT_SNAP_AB_N	    mBIT(7)
#define	VXGE_HAL_RXMAC_RX_PA_CFG0_SEARCH_FOR_HAO	    mBIT(18)
#define	VXGE_HAL_RXMAC_RX_PA_CFG0_SUPPORT_MOBILE_IPV6_HDRS  mBIT(19)
#define	VXGE_HAL_RXMAC_RX_PA_CFG0_IPV6_STOP_SEARCHING	    mBIT(23)
#define	VXGE_HAL_RXMAC_RX_PA_CFG0_NO_PS_IF_UNKNOWN	    mBIT(27)
#define	VXGE_HAL_RXMAC_RX_PA_CFG0_SEARCH_FOR_ETYPE	    mBIT(35)
#define	VXGE_HAL_RXMAC_RX_PA_CFG0_TOSS_ANY_FRM_IF_L3_CSUM_ERR mBIT(39)
#define	VXGE_HAL_RXMAC_RX_PA_CFG0_TOSS_OFFLD_FRM_IF_L3_CSUM_ERR mBIT(43)
#define	VXGE_HAL_RXMAC_RX_PA_CFG0_TOSS_ANY_FRM_IF_L4_CSUM_ERR	mBIT(47)
#define	VXGE_HAL_RXMAC_RX_PA_CFG0_TOSS_OFFLD_FRM_IF_L4_CSUM_ERR	mBIT(51)
#define	VXGE_HAL_RXMAC_RX_PA_CFG0_TOSS_ANY_FRM_IF_RPA_ERR	mBIT(55)
#define	VXGE_HAL_RXMAC_RX_PA_CFG0_TOSS_OFFLD_FRM_IF_RPA_ERR	mBIT(59)
#define	VXGE_HAL_RXMAC_RX_PA_CFG0_JUMBO_SNAP_EN		    mBIT(63)
/* 0x01808 */	u64	rxmac_rx_pa_cfg1;
#define	VXGE_HAL_RXMAC_RX_PA_CFG1_REPL_IPV4_TCP_INCL_PH	    mBIT(3)
#define	VXGE_HAL_RXMAC_RX_PA_CFG1_REPL_IPV6_TCP_INCL_PH	    mBIT(7)
#define	VXGE_HAL_RXMAC_RX_PA_CFG1_REPL_IPV4_UDP_INCL_PH	    mBIT(11)
#define	VXGE_HAL_RXMAC_RX_PA_CFG1_REPL_IPV6_UDP_INCL_PH	    mBIT(15)
#define	VXGE_HAL_RXMAC_RX_PA_CFG1_REPL_L4_INCL_CF	    mBIT(19)
#define	VXGE_HAL_RXMAC_RX_PA_CFG1_REPL_STRIP_VLAN_TAG	    mBIT(23)
	u8	unused01828[0x01828-0x01810];

/* 0x01828 */	u64	rts_mgr_cfg0;
#define	VXGE_HAL_RTS_MGR_CFG0_RTS_DP_SP_PRIORITY	    mBIT(3)
#define	VXGE_HAL_RTS_MGR_CFG0_FLEX_L4PRTCL_VALUE(val)	    vBIT(val, 24, 8)
#define	VXGE_HAL_RTS_MGR_CFG0_ICMP_TRASH		    mBIT(35)
#define	VXGE_HAL_RTS_MGR_CFG0_TCPSYN_TRASH		    mBIT(39)
#define	VXGE_HAL_RTS_MGR_CFG0_ZL4PYLD_TRASH		    mBIT(43)
#define	VXGE_HAL_RTS_MGR_CFG0_L4PRTCL_TCP_TRASH		    mBIT(47)
#define	VXGE_HAL_RTS_MGR_CFG0_L4PRTCL_UDP_TRASH		    mBIT(51)
#define	VXGE_HAL_RTS_MGR_CFG0_L4PRTCL_FLEX_TRASH	    mBIT(55)
#define	VXGE_HAL_RTS_MGR_CFG0_IPFRAG_TRASH		    mBIT(59)
/* 0x01830 */	u64	rts_mgr_cfg1;
#define	VXGE_HAL_RTS_MGR_CFG1_DA_ACTIVE_TABLE		    mBIT(3)
#define	VXGE_HAL_RTS_MGR_CFG1_PN_ACTIVE_TABLE		    mBIT(7)
/* 0x01838 */	u64	rts_mgr_criteria_priority;
#define	VXGE_HAL_RTS_MGR_CRITERIA_PRIORITY_ETYPE(val)	    vBIT(val, 5, 3)
#define	VXGE_HAL_RTS_MGR_CRITERIA_PRIORITY_ICMP_TCPSYN(val) vBIT(val, 9, 3)
#define	VXGE_HAL_RTS_MGR_CRITERIA_PRIORITY_L4PN(val)	    vBIT(val, 13, 3)
#define	VXGE_HAL_RTS_MGR_CRITERIA_PRIORITY_RANGE_L4PN(val)  vBIT(val, 17, 3)
#define	VXGE_HAL_RTS_MGR_CRITERIA_PRIORITY_RTH_IT(val)	    vBIT(val, 21, 3)
#define	VXGE_HAL_RTS_MGR_CRITERIA_PRIORITY_DS(val)	    vBIT(val, 25, 3)
#define	VXGE_HAL_RTS_MGR_CRITERIA_PRIORITY_QOS(val)	    vBIT(val, 29, 3)
#define	VXGE_HAL_RTS_MGR_CRITERIA_PRIORITY_ZL4PYLD(val)	    vBIT(val, 33, 3)
#define	VXGE_HAL_RTS_MGR_CRITERIA_PRIORITY_L4PRTCL(val)	    vBIT(val, 37, 3)
/* 0x01840 */	u64	rts_mgr_da_pause_cfg;
#define	VXGE_HAL_RTS_MGR_DA_PAUSE_CFG_VPATH_VECTOR(val)	    vBIT(val, 0, 17)
/* 0x01848 */	u64	rts_mgr_da_slow_proto_cfg;
#define	VXGE_HAL_RTS_MGR_DA_SLOW_PROTO_CFG_VPATH_VECTOR(val) vBIT(val, 0, 17)
	u8	unused01890[0x01890-0x01850];
/* 0x01890 */	u64	rts_mgr_cbasin_cfg;
	u8	unused01968[0x01968-0x01898];

/* 0x01968 */	u64	dbg_stat_rx_any_frms;
#define	VXGE_HAL_DBG_STAT_RX_ANY_FRMS_PORT0_RX_ANY_FRMS(val) vBIT(val, 0, 8)
#define	VXGE_HAL_DBG_STAT_RX_ANY_FRMS_PORT1_RX_ANY_FRMS(val) vBIT(val, 8, 8)
#define	VXGE_HAL_DBG_STAT_RX_ANY_FRMS_PORT2_RX_ANY_FRMS(val) vBIT(val, 16, 8)
	u8	unused01a00[0x01a00-0x01970];

/* 0x01a00 */	u64	rxmac_red_rate_vp[17];
#define	VXGE_HAL_RXMAC_RED_RATE_VP_CRATE_THR0(val)	    vBIT(val, 0, 4)
#define	VXGE_HAL_RXMAC_RED_RATE_VP_CRATE_THR1(val)	    vBIT(val, 4, 4)
#define	VXGE_HAL_RXMAC_RED_RATE_VP_CRATE_THR2(val)	    vBIT(val, 8, 4)
#define	VXGE_HAL_RXMAC_RED_RATE_VP_CRATE_THR3(val)	    vBIT(val, 12, 4)
#define	VXGE_HAL_RXMAC_RED_RATE_VP_FRATE_THR0(val)	    vBIT(val, 16, 4)
#define	VXGE_HAL_RXMAC_RED_RATE_VP_FRATE_THR1(val)	    vBIT(val, 20, 4)
#define	VXGE_HAL_RXMAC_RED_RATE_VP_FRATE_THR2(val)	    vBIT(val, 24, 4)
#define	VXGE_HAL_RXMAC_RED_RATE_VP_FRATE_THR3(val)	    vBIT(val, 28, 4)
	u8	unused01e00[0x01e00-0x01a88];

/* 0x01e00 */	u64	xgmac_int_status;
#define	VXGE_HAL_XGMAC_INT_STATUS_XMAC_GEN_ERR_XMAC_GEN_INT mBIT(3)
#define	VXGE_HAL_XGMAC_INT_STATUS_XMAC_LINK_ERR_PORT0_XMAC_LINK_INT_PORT0\
							    mBIT(7)
#define	VXGE_HAL_XGMAC_INT_STATUS_XMAC_LINK_ERR_PORT1_XMAC_LINK_INT_PORT1\
							    mBIT(11)
#define	VXGE_HAL_XGMAC_INT_STATUS_XGXS_GEN_ERR_XGXS_GEN_INT mBIT(15)
#define	VXGE_HAL_XGMAC_INT_STATUS_ASIC_NTWK_ERR_ASIC_NTWK_INT mBIT(19)
#define	VXGE_HAL_XGMAC_INT_STATUS_ASIC_GPIO_ERR_ASIC_GPIO_INT mBIT(23)
/* 0x01e08 */	u64	xgmac_int_mask;
/* 0x01e10 */	u64	xmac_gen_err_reg;
#define	VXGE_HAL_XMAC_GEN_ERR_REG_LAGC_LAG_PORT0_ACTOR_CHURN_DETECTED mBIT(7)
#define	VXGE_HAL_XMAC_GEN_ERR_REG_LAGC_LAG_PORT0_PARTNER_CHURN_DETECTED	mBIT(11)
#define	VXGE_HAL_XMAC_GEN_ERR_REG_LAGC_LAG_PORT0_RECEIVED_LACPDU    mBIT(15)
#define	VXGE_HAL_XMAC_GEN_ERR_REG_LAGC_LAG_PORT1_ACTOR_CHURN_DETECTED	mBIT(19)
#define	VXGE_HAL_XMAC_GEN_ERR_REG_LAGC_LAG_PORT1_PARTNER_CHURN_DETECTED	mBIT(23)
#define	VXGE_HAL_XMAC_GEN_ERR_REG_LAGC_LAG_PORT1_RECEIVED_LACPDU    mBIT(27)
#define	VXGE_HAL_XMAC_GEN_ERR_REG_XLCM_LAG_FAILOVER_DETECTED	mBIT(31)
#define	VXGE_HAL_XMAC_GEN_ERR_REG_XSTATS_RMAC_STATS_TILE0_SG_ERR(val)\
							    vBIT(val, 40, 2)
#define	VXGE_HAL_XMAC_GEN_ERR_REG_XSTATS_RMAC_STATS_TILE0_DB_ERR(val)\
							    vBIT(val, 42, 2)
#define	VXGE_HAL_XMAC_GEN_ERR_REG_XSTATS_RMAC_STATS_TILE1_SG_ERR(val)\
							    vBIT(val, 44, 2)
#define	VXGE_HAL_XMAC_GEN_ERR_REG_XSTATS_RMAC_STATS_TILE1_DB_ERR(val)\
							    vBIT(val, 46, 2)
#define	VXGE_HAL_XMAC_GEN_ERR_REG_XSTATS_RMAC_STATS_TILE2_SG_ERR(val)\
							    vBIT(val, 48, 2)
#define	VXGE_HAL_XMAC_GEN_ERR_REG_XSTATS_RMAC_STATS_TILE2_DB_ERR(val)\
							    vBIT(val, 50, 2)
#define	VXGE_HAL_XMAC_GEN_ERR_REG_XSTATS_RMAC_STATS_TILE3_SG_ERR(val)\
							    vBIT(val, 52, 2)
#define	VXGE_HAL_XMAC_GEN_ERR_REG_XSTATS_RMAC_STATS_TILE3_DB_ERR(val)\
							    vBIT(val, 54, 2)
#define	VXGE_HAL_XMAC_GEN_ERR_REG_XSTATS_RMAC_STATS_TILE4_SG_ERR(val)\
							    vBIT(val, 56, 2)
#define	VXGE_HAL_XMAC_GEN_ERR_REG_XSTATS_RMAC_STATS_TILE4_DB_ERR(val)\
							    vBIT(val, 58, 2)
#define	VXGE_HAL_XMAC_GEN_ERR_REG_XMACJ_XMAC_FSM_ERR	    mBIT(63)
/* 0x01e18 */	u64	xmac_gen_err_mask;
/* 0x01e20 */	u64	xmac_gen_err_alarm;
/* 0x01e28 */	u64	xmac_link_err_port0_reg;
#define	VXGE_HAL_XMAC_LINK_ERR_PORT_REG_XMACJ_PORT_DOWN	    mBIT(3)
#define	VXGE_HAL_XMAC_LINK_ERR_PORT_REG_XMACJ_PORT_UP	    mBIT(7)
#define	VXGE_HAL_XMAC_LINK_ERR_PORT_REG_XMACJ_PORT_WENT_DOWN mBIT(11)
#define	VXGE_HAL_XMAC_LINK_ERR_PORT_REG_XMACJ_PORT_WENT_UP  mBIT(15)
#define	VXGE_HAL_XMAC_LINK_ERR_PORT_REG_XMACJ_PORT_REAFFIRMED_FAULT mBIT(19)
#define	VXGE_HAL_XMAC_LINK_ERR_PORT_REG_XMACJ_PORT_REAFFIRMED_OK    mBIT(23)
#define	VXGE_HAL_XMAC_LINK_ERR_PORT_REG_XMACJ_LINK_DOWN	    mBIT(27)
#define	VXGE_HAL_XMAC_LINK_ERR_PORT_REG_XMACJ_LINK_UP	    mBIT(31)
#define	VXGE_HAL_XMAC_LINK_ERR_PORT_REG_RATEMGMT_RATE_CHANGE mBIT(35)
#define	VXGE_HAL_XMAC_LINK_ERR_PORT_REG_RATEMGMT_LASI_INV   mBIT(39)
#define	VXGE_HAL_XMAC_LINK_ERR_PORT_REG_XMDIO_MDIO_MGR_ACCESS_COMPLETE mBIT(47)
/* 0x01e30 */	u64	xmac_link_err_port0_mask;
/* 0x01e38 */	u64	xmac_link_err_port0_alarm;
/* 0x01e40 */	u64	xmac_link_err_port1_reg;
/* 0x01e48 */	u64	xmac_link_err_port1_mask;
/* 0x01e50 */	u64	xmac_link_err_port1_alarm;
/* 0x01e58 */	u64	xgxs_gen_err_reg;
#define	VXGE_HAL_XGXS_GEN_ERR_REG_XGXS_XGXS_FSM_ERR	    mBIT(63)
/* 0x01e60 */	u64	xgxs_gen_err_mask;
/* 0x01e68 */	u64	xgxs_gen_err_alarm;
/* 0x01e70 */	u64	asic_ntwk_err_reg;
#define	VXGE_HAL_ASIC_NTWK_ERR_REG_XMACJ_NTWK_DOWN	    mBIT(3)
#define	VXGE_HAL_ASIC_NTWK_ERR_REG_XMACJ_NTWK_UP	    mBIT(7)
#define	VXGE_HAL_ASIC_NTWK_ERR_REG_XMACJ_NTWK_WENT_DOWN	    mBIT(11)
#define	VXGE_HAL_ASIC_NTWK_ERR_REG_XMACJ_NTWK_WENT_UP	    mBIT(15)
#define	VXGE_HAL_ASIC_NTWK_ERR_REG_XMACJ_NTWK_REAFFIRMED_FAULT	mBIT(19)
#define	VXGE_HAL_ASIC_NTWK_ERR_REG_XMACJ_NTWK_REAFFIRMED_OK	mBIT(23)
/* 0x01e78 */	u64	asic_ntwk_err_mask;
/* 0x01e80 */	u64	asic_ntwk_err_alarm;
/* 0x01e88 */	u64	asic_gpio_err_reg;
#define	VXGE_HAL_ASIC_GPIO_ERR_REG_XMACJ_GPIO_INT(n)	    mBIT(n)
/* 0x01e90 */	u64	asic_gpio_err_mask;
/* 0x01e98 */	u64	asic_gpio_err_alarm;
/* 0x01ea0 */	u64	xgmac_gen_status;
#define	VXGE_HAL_XGMAC_GEN_STATUS_XMACJ_NTWK_OK		    mBIT(3)
#define	VXGE_HAL_XGMAC_GEN_STATUS_XMACJ_NTWK_DATA_RATE	    mBIT(11)
/* 0x01ea8 */	u64	xgmac_gen_fw_memo_status;
#define	VXGE_HAL_XGMAC_GEN_FW_MEMO_STATUS_XMACJ_EVENTS_PENDING(val)\
							    vBIT(val, 0, 17)
/* 0x01eb0 */	u64	xgmac_gen_fw_memo_mask;
#define	VXGE_HAL_XGMAC_GEN_FW_MEMO_MASK_MASK(val)	    vBIT(val, 0, 64)
/* 0x01eb8 */	u64	xgmac_gen_fw_vpath_to_vsport_status;
#define	VXGE_HAL_XGMAC_GEN_FW_VPATH_TO_VSPORT_STATUS_XMACJ_EVENTS_PENDING(val)\
							    vBIT(val, 0, 17)
/* 0x01ec0 */	u64	xgmac_main_cfg_port[2];
#define	VXGE_HAL_XGMAC_MAIN_CFG_PORT_PORT_EN		    mBIT(3)
	u8	unused01f40[0x01f40-0x01ed0];

/* 0x01f40 */	u64	xmac_gen_cfg;
#define	VXGE_HAL_XMAC_GEN_CFG_RATEMGMT_MAC_RATE_SEL(val)    vBIT(val, 2, 2)
#define	VXGE_HAL_XMAC_GEN_CFG_TX_HEAD_DROP_WHEN_FAULT	    mBIT(7)
#define	VXGE_HAL_XMAC_GEN_CFG_FAULT_BEHAVIOUR		    mBIT(27)
#define	VXGE_HAL_XMAC_GEN_CFG_PERIOD_NTWK_UP(val)	    vBIT(val, 28, 4)
#define	VXGE_HAL_XMAC_GEN_CFG_PERIOD_NTWK_DOWN(val)	    vBIT(val, 32, 4)
/* 0x01f48 */	u64	xmac_timestamp;
#define	VXGE_HAL_XMAC_TIMESTAMP_EN			    mBIT(3)
#define	VXGE_HAL_XMAC_TIMESTAMP_USE_LINK_ID(val)	    vBIT(val, 6, 2)
#define	VXGE_HAL_XMAC_TIMESTAMP_INTERVAL(val)		    vBIT(val, 12, 4)
#define	VXGE_HAL_XMAC_TIMESTAMP_TIMER_RESTART		    mBIT(19)
#define	VXGE_HAL_XMAC_TIMESTAMP_XMACJ_ROLLOVER_CNT(val)	    vBIT(val, 32, 16)
/* 0x01f50 */	u64	xmac_stats_gen_cfg;
#define	VXGE_HAL_XMAC_STATS_GEN_CFG_PRTAGGR_CUM_TIMER(val)  vBIT(val, 4, 4)
#define	VXGE_HAL_XMAC_STATS_GEN_CFG_VPATH_CUM_TIMER(val)    vBIT(val, 8, 4)
#define	VXGE_HAL_XMAC_STATS_GEN_CFG_VLAN_HANDLING	    mBIT(15)
/* 0x01f58 */	u64	xmac_stats_sys_cmd;
#define	VXGE_HAL_XMAC_STATS_SYS_CMD_OP(val)		    vBIT(val, 5, 3)
#define	VXGE_HAL_XMAC_STATS_SYS_CMD_STROBE		    mBIT(15)
#define	VXGE_HAL_XMAC_STATS_SYS_CMD_LOC_SEL(val)	    vBIT(val, 27, 5)
#define	VXGE_HAL_XMAC_STATS_SYS_CMD_OFFSET_SEL(val)	    vBIT(val, 32, 8)
/* 0x01f60 */	u64	xmac_stats_sys_data;
#define	VXGE_HAL_XMAC_STATS_SYS_DATA_XSMGR_DATA(val)	    vBIT(val, 0, 64)
	u8	unused01f80[0x01f80-0x01f68];

/* 0x01f80 */	u64	asic_ntwk_ctrl;
#define	VXGE_HAL_ASIC_NTWK_CTRL_REQ_TEST_NTWK		    mBIT(3)
#define	VXGE_HAL_ASIC_NTWK_CTRL_PORT0_REQ_TEST_PORT	    mBIT(11)
#define	VXGE_HAL_ASIC_NTWK_CTRL_PORT1_REQ_TEST_PORT	    mBIT(15)
/* 0x01f88 */	u64	asic_ntwk_cfg_show_port_info;
#define	VXGE_HAL_ASIC_NTWK_CFG_SHOW_PORT_INFO_VP(n)	    mBIT(n)
/* 0x01f90 */	u64	asic_ntwk_cfg_port_num;
#define	VXGE_HAL_ASIC_NTWK_CFG_PORT_NUM_VP(n)		    mBIT(n)
/* 0x01f98 */	u64	xmac_cfg_port[3];
#define	VXGE_HAL_XMAC_CFG_PORT_XGMII_LOOPBACK		    mBIT(3)
#define	VXGE_HAL_XMAC_CFG_PORT_XGMII_REVERSE_LOOPBACK	    mBIT(7)
#define	VXGE_HAL_XMAC_CFG_PORT_XGMII_TX_BEHAV		    mBIT(11)
#define	VXGE_HAL_XMAC_CFG_PORT_XGMII_RX_BEHAV		    mBIT(15)
/* 0x01fb0 */	u64	xmac_station_addr_port[2];
#define	VXGE_HAL_XMAC_STATION_ADDR_PORT_MAC_ADDR(val)	    vBIT(val, 0, 48)
	u8	unused02020[0x02020-0x01fc0];

/* 0x02020 */	u64	lag_cfg;
#define	VXGE_HAL_LAG_CFG_EN				    mBIT(3)
#define	VXGE_HAL_LAG_CFG_MODE(val)			    vBIT(val, 6, 2)
#define	VXGE_HAL_LAG_CFG_TX_DISCARD_BEHAV		    mBIT(11)
#define	VXGE_HAL_LAG_CFG_RX_DISCARD_BEHAV		    mBIT(15)
#define	VXGE_HAL_LAG_CFG_PREF_INDIV_PORT_NUM		    mBIT(19)
/* 0x02028 */	u64	lag_status;
#define	VXGE_HAL_LAG_STATUS_XLCM_WAITING_TO_FAILBACK	    mBIT(3)
#define	VXGE_HAL_LAG_STATUS_XLCM_TIMER_VAL_COLD_FAILOVER(val) vBIT(val, 8, 8)
/* 0x02030 */	u64	lag_active_passive_cfg;
#define	VXGE_HAL_LAG_ACTIVE_PASSIVE_CFG_HOT_STANDBY	    mBIT(3)
#define	VXGE_HAL_LAG_ACTIVE_PASSIVE_CFG_LACP_DECIDES	    mBIT(7)
#define	VXGE_HAL_LAG_ACTIVE_PASSIVE_CFG_PREF_ACTIVE_PORT_NUM mBIT(11)
#define	VXGE_HAL_LAG_ACTIVE_PASSIVE_CFG_AUTO_FAILBACK	    mBIT(15)
#define	VXGE_HAL_LAG_ACTIVE_PASSIVE_CFG_FAILBACK_EN	    mBIT(19)
#define	VXGE_HAL_LAG_ACTIVE_PASSIVE_CFG_COLD_FAILOVER_TIMEOUT(val)\
							    vBIT(val, 32, 16)
	u8	unused02040[0x02040-0x02038];

/* 0x02040 */	u64	lag_lacp_cfg;
#define	VXGE_HAL_LAG_LACP_CFG_EN			    mBIT(3)
#define	VXGE_HAL_LAG_LACP_CFG_LACP_BEGIN		    mBIT(7)
#define	VXGE_HAL_LAG_LACP_CFG_DISCARD_LACP		    mBIT(11)
#define	VXGE_HAL_LAG_LACP_CFG_LIBERAL_LEN_CHK		    mBIT(15)
/* 0x02048 */	u64	lag_timer_cfg_1;
#define	VXGE_HAL_LAG_TIMER_CFG_1_FAST_PER(val)		    vBIT(val, 0, 16)
#define	VXGE_HAL_LAG_TIMER_CFG_1_SLOW_PER(val)		    vBIT(val, 16, 16)
#define	VXGE_HAL_LAG_TIMER_CFG_1_SHORT_TIMEOUT(val)	    vBIT(val, 32, 16)
#define	VXGE_HAL_LAG_TIMER_CFG_1_LONG_TIMEOUT(val)	    vBIT(val, 48, 16)
/* 0x02050 */	u64	lag_timer_cfg_2;
#define	VXGE_HAL_LAG_TIMER_CFG_2_CHURN_DET(val)		    vBIT(val, 0, 16)
#define	VXGE_HAL_LAG_TIMER_CFG_2_AGGR_WAIT(val)		    vBIT(val, 16, 16)
#define	VXGE_HAL_LAG_TIMER_CFG_2_SHORT_TIMER_SCALE(val)	    vBIT(val, 32, 16)
#define	VXGE_HAL_LAG_TIMER_CFG_2_LONG_TIMER_SCALE(val)	    vBIT(val, 48, 16)
/* 0x02058 */	u64	lag_sys_id;
#define	VXGE_HAL_LAG_SYS_ID_ADDR(val)			    vBIT(val, 0, 48)
#define	VXGE_HAL_LAG_SYS_ID_USE_PORT_ADDR		    mBIT(51)
#define	VXGE_HAL_LAG_SYS_ID_ADDR_SEL			    mBIT(55)
/* 0x02060 */	u64	lag_sys_cfg;
#define	VXGE_HAL_LAG_SYS_CFG_SYS_PRI(val)		    vBIT(val, 0, 16)
	u8	unused02070[0x02070-0x02068];

/* 0x02070 */	u64	lag_aggr_addr_cfg[2];
#define	VXGE_HAL_LAG_AGGR_ADDR_CFG_ADDR(val)		    vBIT(val, 0, 48)
#define	VXGE_HAL_LAG_AGGR_ADDR_CFG_USE_PORT_ADDR	    mBIT(51)
#define	VXGE_HAL_LAG_AGGR_ADDR_CFG_ADDR_SEL		    mBIT(55)
/* 0x02080 */	u64	lag_aggr_id_cfg[2];
#define	VXGE_HAL_LAG_AGGR_ID_CFG_ID(val)		    vBIT(val, 0, 16)
/* 0x02090 */	u64	lag_aggr_admin_key[2];
#define	VXGE_HAL_LAG_AGGR_ADMIN_KEY_KEY(val)		    vBIT(val, 0, 16)
/* 0x020a0 */	u64	lag_aggr_alt_admin_key;
#define	VXGE_HAL_LAG_AGGR_ALT_ADMIN_KEY_KEY(val)	    vBIT(val, 0, 16)
#define	VXGE_HAL_LAG_AGGR_ALT_ADMIN_KEY_ALT_AGGR	    mBIT(19)
/* 0x020a8 */	u64	lag_aggr_oper_key[2];
#define	VXGE_HAL_LAG_AGGR_OPER_KEY_LAGC_KEY(val)	    vBIT(val, 0, 16)
/* 0x020b8 */	u64	lag_aggr_partner_sys_id[2];
#define	VXGE_HAL_LAG_AGGR_PARTNER_SYS_ID_LAGC_ADDR(val)	    vBIT(val, 0, 48)
/* 0x020c8 */	u64	lag_aggr_partner_info[2];
#define	VXGE_HAL_LAG_AGGR_PARTNER_INFO_LAGC_SYS_PRI(val)    vBIT(val, 0, 16)
#define	VXGE_HAL_LAG_AGGR_PARTNER_INFO_LAGC_OPER_KEY(val)   vBIT(val, 16, 16)
/* 0x020d8 */	u64	lag_aggr_state[2];
#define	VXGE_HAL_LAG_AGGR_STATE_LAGC_TX			    mBIT(3)
#define	VXGE_HAL_LAG_AGGR_STATE_LAGC_RX			    mBIT(7)
#define	VXGE_HAL_LAG_AGGR_STATE_LAGC_READY		    mBIT(11)
#define	VXGE_HAL_LAG_AGGR_STATE_LAGC_INDIVIDUAL		    mBIT(15)
	u8	unused020f0[0x020f0-0x020e8];

/* 0x020f0 */	u64	lag_port_cfg[2];
#define	VXGE_HAL_LAG_PORT_CFG_EN			    mBIT(3)
#define	VXGE_HAL_LAG_PORT_CFG_DISCARD_SLOW_PROTO	    mBIT(7)
#define	VXGE_HAL_LAG_PORT_CFG_HOST_CHOSEN_AGGR		    mBIT(11)
#define	VXGE_HAL_LAG_PORT_CFG_DISCARD_UNKNOWN_SLOW_PROTO    mBIT(15)
/* 0x02100 */	u64	lag_port_actor_admin_cfg[2];
#define	VXGE_HAL_LAG_PORT_ACTOR_ADMIN_CFG_PORT_NUM(val)	    vBIT(val, 0, 16)
#define	VXGE_HAL_LAG_PORT_ACTOR_ADMIN_CFG_PORT_PRI(val)	    vBIT(val, 16, 16)
#define	VXGE_HAL_LAG_PORT_ACTOR_ADMIN_CFG_KEY_10G(val)	    vBIT(val, 32, 16)
#define	VXGE_HAL_LAG_PORT_ACTOR_ADMIN_CFG_KEY_1G(val)	    vBIT(val, 48, 16)
/* 0x02110 */	u64	lag_port_actor_admin_state[2];
#define	VXGE_HAL_LAG_PORT_ACTOR_ADMIN_STATE_LACP_ACTIVITY   mBIT(3)
#define	VXGE_HAL_LAG_PORT_ACTOR_ADMIN_STATE_LACP_TIMEOUT    mBIT(7)
#define	VXGE_HAL_LAG_PORT_ACTOR_ADMIN_STATE_AGGREGATION	    mBIT(11)
#define	VXGE_HAL_LAG_PORT_ACTOR_ADMIN_STATE_SYNCHRONIZATION mBIT(15)
#define	VXGE_HAL_LAG_PORT_ACTOR_ADMIN_STATE_COLLECTING	    mBIT(19)
#define	VXGE_HAL_LAG_PORT_ACTOR_ADMIN_STATE_DISTRIBUTING    mBIT(23)
#define	VXGE_HAL_LAG_PORT_ACTOR_ADMIN_STATE_DEFAULTED	    mBIT(27)
#define	VXGE_HAL_LAG_PORT_ACTOR_ADMIN_STATE_EXPIRED	    mBIT(31)
/* 0x02120 */	u64	lag_port_partner_admin_sys_id[2];
#define	VXGE_HAL_LAG_PORT_PARTNER_ADMIN_SYS_ID_ADDR(val)    vBIT(val, 0, 48)
/* 0x02130 */	u64	lag_port_partner_admin_cfg[2];
#define	VXGE_HAL_LAG_PORT_PARTNER_ADMIN_CFG_SYS_PRI(val)    vBIT(val, 0, 16)
#define	VXGE_HAL_LAG_PORT_PARTNER_ADMIN_CFG_KEY(val)	    vBIT(val, 16, 16)
#define	VXGE_HAL_LAG_PORT_PARTNER_ADMIN_CFG_PORT_NUM(val)   vBIT(val, 32, 16)
#define	VXGE_HAL_LAG_PORT_PARTNER_ADMIN_CFG_PORT_PRI(val)   vBIT(val, 48, 16)
/* 0x02140 */	u64	lag_port_partner_admin_state[2];
#define	VXGE_HAL_LAG_PORT_PARTNER_ADMIN_STATE_LACP_ACTIVITY mBIT(3)
#define	VXGE_HAL_LAG_PORT_PARTNER_ADMIN_STATE_LACP_TIMEOUT  mBIT(7)
#define	VXGE_HAL_LAG_PORT_PARTNER_ADMIN_STATE_AGGREGATION   mBIT(11)
#define	VXGE_HAL_LAG_PORT_PARTNER_ADMIN_STATE_SYNCHRONIZATION mBIT(15)
#define	VXGE_HAL_LAG_PORT_PARTNER_ADMIN_STATE_COLLECTING    mBIT(19)
#define	VXGE_HAL_LAG_PORT_PARTNER_ADMIN_STATE_DISTRIBUTING  mBIT(23)
#define	VXGE_HAL_LAG_PORT_PARTNER_ADMIN_STATE_DEFAULTED	    mBIT(27)
#define	VXGE_HAL_LAG_PORT_PARTNER_ADMIN_STATE_EXPIRED	    mBIT(31)
/* 0x02150 */	u64	lag_port_to_aggr[2];
#define	VXGE_HAL_LAG_PORT_TO_AGGR_LAGC_AGGR_ID(val)	    vBIT(val, 0, 16)
#define	VXGE_HAL_LAG_PORT_TO_AGGR_LAGC_AGGR_VLD_ID	    mBIT(19)
/* 0x02160 */	u64	lag_port_actor_oper_key[2];
#define	VXGE_HAL_LAG_PORT_ACTOR_OPER_KEY_LAGC_KEY(val)	    vBIT(val, 0, 16)
/* 0x02170 */	u64	lag_port_actor_oper_state[2];
#define	VXGE_HAL_LAG_PORT_ACTOR_OPER_STATE_LAGC_LACP_ACTIVITY	mBIT(3)
#define	VXGE_HAL_LAG_PORT_ACTOR_OPER_STATE_LAGC_LACP_TIMEOUT	mBIT(7)
#define	VXGE_HAL_LAG_PORT_ACTOR_OPER_STATE_LAGC_AGGREGATION	mBIT(11)
#define	VXGE_HAL_LAG_PORT_ACTOR_OPER_STATE_LAGC_SYNCHRONIZATION	mBIT(15)
#define	VXGE_HAL_LAG_PORT_ACTOR_OPER_STATE_LAGC_COLLECTING	mBIT(19)
#define	VXGE_HAL_LAG_PORT_ACTOR_OPER_STATE_LAGC_DISTRIBUTING	mBIT(23)
#define	VXGE_HAL_LAG_PORT_ACTOR_OPER_STATE_LAGC_DEFAULTED	mBIT(27)
#define	VXGE_HAL_LAG_PORT_ACTOR_OPER_STATE_LAGC_EXPIRED		mBIT(31)
/* 0x02180 */	u64	lag_port_partner_oper_sys_id[2];
#define	VXGE_HAL_LAG_PORT_PARTNER_OPER_SYS_ID_LAGC_ADDR(val) vBIT(val, 0, 48)
/* 0x02190 */	u64	lag_port_partner_oper_info[2];
#define	VXGE_HAL_LAG_PORT_PARTNER_OPER_INFO_LAGC_SYS_PRI(val) vBIT(val, 0, 16)
#define	VXGE_HAL_LAG_PORT_PARTNER_OPER_INFO_LAGC_KEY(val)   vBIT(val, 16, 16)
#define	VXGE_HAL_LAG_PORT_PARTNER_OPER_INFO_LAGC_PORT_NUM(val) vBIT(val, 32, 16)
#define	VXGE_HAL_LAG_PORT_PARTNER_OPER_INFO_LAGC_PORT_PRI(val) vBIT(val, 48, 16)
/* 0x021a0 */	u64	lag_port_partner_oper_state[2];
#define	VXGE_HAL_LAG_PORT_PARTNER_OPER_STATE_LAGC_LACP_ACTIVITY	mBIT(3)
#define	VXGE_HAL_LAG_PORT_PARTNER_OPER_STATE_LAGC_LACP_TIMEOUT	mBIT(7)
#define	VXGE_HAL_LAG_PORT_PARTNER_OPER_STATE_LAGC_AGGREGATION	mBIT(11)
#define	VXGE_HAL_LAG_PORT_PARTNER_OPER_STATE_LAGC_SYNCHRONIZATION mBIT(15)
#define	VXGE_HAL_LAG_PORT_PARTNER_OPER_STATE_LAGC_COLLECTING	mBIT(19)
#define	VXGE_HAL_LAG_PORT_PARTNER_OPER_STATE_LAGC_DISTRIBUTING	mBIT(23)
#define	VXGE_HAL_LAG_PORT_PARTNER_OPER_STATE_LAGC_DEFAULTED	mBIT(27)
#define	VXGE_HAL_LAG_PORT_PARTNER_OPER_STATE_LAGC_EXPIRED	mBIT(31)
/* 0x021b0 */	u64	lag_port_state_vars[2];
#define	VXGE_HAL_LAG_PORT_STATE_VARS_LAGC_READY		    mBIT(3)
#define	VXGE_HAL_LAG_PORT_STATE_VARS_LAGC_SELECTED(val)	    vBIT(val, 6, 2)
#define	VXGE_HAL_LAG_PORT_STATE_VARS_LAGC_AGGR_NUM	    mBIT(11)
#define	VXGE_HAL_LAG_PORT_STATE_VARS_LAGC_PORT_MOVED	    mBIT(15)
#define	VXGE_HAL_LAG_PORT_STATE_VARS_LAGC_PORT_ENABLED	    mBIT(18)
#define	VXGE_HAL_LAG_PORT_STATE_VARS_LAGC_PORT_DISABLED	    mBIT(19)
#define	VXGE_HAL_LAG_PORT_STATE_VARS_LAGC_NTT		    mBIT(23)
#define	VXGE_HAL_LAG_PORT_STATE_VARS_LAGC_ACTOR_CHURN	    mBIT(27)
#define	VXGE_HAL_LAG_PORT_STATE_VARS_LAGC_PARTNER_CHURN	    mBIT(31)
#define	VXGE_HAL_LAG_PORT_STATE_VARS_LAGC_ACTOR_INFO_LEN_MISMATCH mBIT(32)
#define	VXGE_HAL_LAG_PORT_STATE_VARS_LAGC_PARTNER_INFO_LEN_MISMATCH mBIT(33)
#define	VXGE_HAL_LAG_PORT_STATE_VARS_LAGC_COLL_INFO_LEN_MISMATCH mBIT(34)
#define	VXGE_HAL_LAG_PORT_STATE_VARS_LAGC_TERM_INFO_LEN_MISMATCH mBIT(35)
#define	VXGE_HAL_LAG_PORT_STATE_VARS_LAGC_RX_FSM_STATE(val) vBIT(val, 37, 3)
#define	VXGE_HAL_LAG_PORT_STATE_VARS_LAGC_MUX_FSM_STATE(val) vBIT(val, 41, 3)
#define	VXGE_HAL_LAG_PORT_STATE_VARS_LAGC_MUX_REASON(val)   vBIT(val, 44, 4)
#define	VXGE_HAL_LAG_PORT_STATE_VARS_LAGC_ACTOR_CHURN_STATE mBIT(54)
#define	VXGE_HAL_LAG_PORT_STATE_VARS_LAGC_PARTNER_CHURN_STATE mBIT(55)
#define	VXGE_HAL_LAG_PORT_STATE_VARS_LAGC_ACTOR_CHURN_COUNT(val)\
							    vBIT(val, 56, 4)
#define	VXGE_HAL_LAG_PORT_STATE_VARS_LAGC_PARTNER_CHURN_COUNT(val)\
							    vBIT(val, 60, 4)
/* 0x021c0 */	u64	lag_port_timer_cntr[2];
#define	VXGE_HAL_LAG_PORT_TIMER_CNTR_LAGC_CURRENT_while (val) vBIT(val, 0, 8)
#define	VXGE_HAL_LAG_PORT_TIMER_CNTR_LAGC_PERIODIC_while (val) vBIT(val, 8, 8)
#define	VXGE_HAL_LAG_PORT_TIMER_CNTR_LAGC_WAIT_while (val)  vBIT(val, 16, 8)
#define	VXGE_HAL_LAG_PORT_TIMER_CNTR_LAGC_TX_LACP(val)	    vBIT(val, 24, 8)
#define	VXGE_HAL_LAG_PORT_TIMER_CNTR_LAGC_ACTOR_SYNC_TRANSITION_COUNT(val)\
							    vBIT(val, 32, 8)
#define	VXGE_HAL_LAG_PORT_TIMER_CNTR_LAGC_PARTNER_SYNC_TRANSITION_COUNT(val)\
							    vBIT(val, 40, 8)
#define	VXGE_HAL_LAG_PORT_TIMER_CNTR_LAGC_ACTOR_CHANGE_COUNT(val)\
							    vBIT(val, 48, 8)
#define	VXGE_HAL_LAG_PORT_TIMER_CNTR_LAGC_PARTNER_CHANGE_COUNT(val)\
							    vBIT(val, 56, 8)
	u8	unused02700[0x02700-0x021d0];

/* 0x02700 */	u64	rtdma_int_status;
#define	VXGE_HAL_RTDMA_INT_STATUS_PDA_ALARM_PDA_INT	    mBIT(1)
#define	VXGE_HAL_RTDMA_INT_STATUS_PCC_ERROR_PCC_INT	    mBIT(2)
#define	VXGE_HAL_RTDMA_INT_STATUS_LSO_ERROR_LSO_INT	    mBIT(4)
#define	VXGE_HAL_RTDMA_INT_STATUS_SM_ERROR_SM_INT	    mBIT(5)
/* 0x02708 */	u64	rtdma_int_mask;
/* 0x02710 */	u64	pda_alarm_reg;
#define	VXGE_HAL_PDA_ALARM_REG_PDA_HSC_FIFO_ERR		    mBIT(0)
#define	VXGE_HAL_PDA_ALARM_REG_PDA_SM_ERR		    mBIT(1)
/* 0x02718 */	u64	pda_alarm_mask;
/* 0x02720 */	u64	pda_alarm_alarm;
/* 0x02728 */	u64	pcc_error_reg;
#define	VXGE_HAL_PCC_ERROR_REG_PCC_PCC_FRM_BUF_SBE(n)	    mBIT(n)
#define	VXGE_HAL_PCC_ERROR_REG_PCC_PCC_TXDO_SBE(n)	    mBIT(n)
#define	VXGE_HAL_PCC_ERROR_REG_PCC_PCC_FRM_BUF_DBE(n)	    mBIT(n)
#define	VXGE_HAL_PCC_ERROR_REG_PCC_PCC_TXDO_DBE(n)	    mBIT(n)
#define	VXGE_HAL_PCC_ERROR_REG_PCC_PCC_FSM_ERR_ALARM(n)	    mBIT(n)
#define	VXGE_HAL_PCC_ERROR_REG_PCC_PCC_SERR(n)		    mBIT(n)
/* 0x02730 */	u64	pcc_error_mask;
/* 0x02738 */	u64	pcc_error_alarm;
/* 0x02740 */	u64	lso_error_reg;
#define	VXGE_HAL_LSO_ERROR_REG_PCC_LSO_ABORT(n)		    mBIT(n)
#define	VXGE_HAL_LSO_ERROR_REG_PCC_LSO_FSM_ERR_ALARM(n)	    mBIT(n)
/* 0x02748 */	u64	lso_error_mask;
/* 0x02750 */	u64	lso_error_alarm;
/* 0x02758 */	u64	sm_error_reg;
#define	VXGE_HAL_SM_ERROR_REG_SM_FSM_ERR_ALARM		    mBIT(15)
/* 0x02760 */	u64	sm_error_mask;
/* 0x02768 */	u64	sm_error_alarm;
	u8	unused027a8[0x027a8-0x02770];

/* 0x027a8 */	u64	txd_ownership_ctrl;
#define	VXGE_HAL_TXD_OWNERSHIP_CTRL_KEEP_OWNERSHIP	    mBIT(7)
/* 0x027b0 */	u64	pcc_cfg;
#define	VXGE_HAL_PCC_CFG_PCC_ENABLE(n)			    mBIT(n)
#define	VXGE_HAL_PCC_CFG_PCC_ECC_ENABLE_N(n)		    mBIT(n)
/* 0x027b8 */	u64	pcc_control;
#define	VXGE_HAL_PCC_CONTROL_FE_ENABLE(val)		    vBIT(val, 6, 2)
#define	VXGE_HAL_PCC_CONTROL_EARLY_ASSIGN_EN		    mBIT(15)
#define	VXGE_HAL_PCC_CONTROL_UNBLOCK_DB_ERR		    mBIT(31)
/* 0x027c0 */	u64	pda_status1;
#define	VXGE_HAL_PDA_STATUS1_PDA_WRAP_0_CTR(val)	    vBIT(val, 4, 4)
#define	VXGE_HAL_PDA_STATUS1_PDA_WRAP_1_CTR(val)	    vBIT(val, 12, 4)
#define	VXGE_HAL_PDA_STATUS1_PDA_WRAP_2_CTR(val)	    vBIT(val, 20, 4)
#define	VXGE_HAL_PDA_STATUS1_PDA_WRAP_3_CTR(val)	    vBIT(val, 28, 4)
#define	VXGE_HAL_PDA_STATUS1_PDA_WRAP_4_CTR(val)	    vBIT(val, 36, 4)
#define	VXGE_HAL_PDA_STATUS1_PDA_WRAP_5_CTR(val)	    vBIT(val, 44, 4)
#define	VXGE_HAL_PDA_STATUS1_PDA_WRAP_6_CTR(val)	    vBIT(val, 52, 4)
#define	VXGE_HAL_PDA_STATUS1_PDA_WRAP_7_CTR(val)	    vBIT(val, 60, 4)
/* 0x027c8 */	u64	rtdma_bw_timer;
#define	VXGE_HAL_RTDMA_BW_TIMER_TIMER_CTRL(val)		    vBIT(val, 12, 4)
	u8	unused02900[0x02900-0x027d0];

/* 0x02900 */	u64	g3cmct_int_status;
#define	VXGE_HAL_G3CMCT_INT_STATUS_ERR_G3IF_INT		    mBIT(0)
/* 0x02908 */	u64	g3cmct_int_mask;
/* 0x02910 */	u64	g3cmct_err_reg;
#define	VXGE_HAL_G3CMCT_ERR_REG_G3IF_SM_ERR		    mBIT(4)
#define	VXGE_HAL_G3CMCT_ERR_REG_G3IF_GDDR3_DECC		    mBIT(5)
#define	VXGE_HAL_G3CMCT_ERR_REG_G3IF_GDDR3_U_DECC	    mBIT(6)
#define	VXGE_HAL_G3CMCT_ERR_REG_G3IF_CTRL_FIFO_DECC	    mBIT(7)
#define	VXGE_HAL_G3CMCT_ERR_REG_G3IF_GDDR3_SECC		    mBIT(29)
#define	VXGE_HAL_G3CMCT_ERR_REG_G3IF_GDDR3_U_SECC	    mBIT(30)
#define	VXGE_HAL_G3CMCT_ERR_REG_G3IF_CTRL_FIFO_SECC	    mBIT(31)
/* 0x02918 */	u64	g3cmct_err_mask;
/* 0x02920 */	u64	g3cmct_err_alarm;
	u8	unused03000[0x03000-0x02928];

/* 0x03000 */	u64	mc_int_status;
#define	VXGE_HAL_MC_INT_STATUS_MC_ERR_MC_INT		    mBIT(3)
#define	VXGE_HAL_MC_INT_STATUS_GROCRC_ALARM_ROCRC_INT	    mBIT(7)
#define	VXGE_HAL_MC_INT_STATUS_FAU_GEN_ERR_FAU_GEN_INT	    mBIT(11)
#define	VXGE_HAL_MC_INT_STATUS_FAU_ECC_ERR_FAU_ECC_INT	    mBIT(15)
/* 0x03008 */	u64	mc_int_mask;
/* 0x03010 */	u64	mc_err_reg;
#define	VXGE_HAL_MC_ERR_REG_MC_XFMD_MEM_ECC_SG_ERR_A	    mBIT(3)
#define	VXGE_HAL_MC_ERR_REG_MC_XFMD_MEM_ECC_SG_ERR_B	    mBIT(4)
#define	VXGE_HAL_MC_ERR_REG_MC_G3IF_RD_FIFO_ECC_SG_ERR	    mBIT(5)
#define	VXGE_HAL_MC_ERR_REG_MC_MIRI_ECC_SG_ERR_0	    mBIT(6)
#define	VXGE_HAL_MC_ERR_REG_MC_MIRI_ECC_SG_ERR_1	    mBIT(7)
#define	VXGE_HAL_MC_ERR_REG_MC_XFMD_MEM_ECC_DB_ERR_A	    mBIT(10)
#define	VXGE_HAL_MC_ERR_REG_MC_XFMD_MEM_ECC_DB_ERR_B	    mBIT(11)
#define	VXGE_HAL_MC_ERR_REG_MC_G3IF_RD_FIFO_ECC_DB_ERR	    mBIT(12)
#define	VXGE_HAL_MC_ERR_REG_MC_MIRI_ECC_DB_ERR_0	    mBIT(13)
#define	VXGE_HAL_MC_ERR_REG_MC_MIRI_ECC_DB_ERR_1	    mBIT(14)
#define	VXGE_HAL_MC_ERR_REG_MC_SM_ERR			    mBIT(15)
/* 0x03018 */	u64	mc_err_mask;
/* 0x03020 */	u64	mc_err_alarm;
/* 0x03028 */	u64	grocrc_alarm_reg;
#define	VXGE_HAL_GROCRC_ALARM_REG_XFMD_WR_FIFO_ERR	    mBIT(3)
#define	VXGE_HAL_GROCRC_ALARM_REG_WDE2MSR_RD_FIFO_ERR	    mBIT(7)
/* 0x03030 */	u64	grocrc_alarm_mask;
/* 0x03038 */	u64	grocrc_alarm_alarm;
	u8	unused03100[0x03100-0x03040];

/* 0x03100 */	u64	rx_thresh_cfg_repl;
#define	VXGE_HAL_RX_THRESH_CFG_REPL_PAUSE_LOW_THR(val)	    vBIT(val, 0, 8)
#define	VXGE_HAL_RX_THRESH_CFG_REPL_PAUSE_HIGH_THR(val)	    vBIT(val, 8, 8)
#define	VXGE_HAL_RX_THRESH_CFG_REPL_RED_THR_0(val)	    vBIT(val, 16, 8)
#define	VXGE_HAL_RX_THRESH_CFG_REPL_RED_THR_1(val)	    vBIT(val, 24, 8)
#define	VXGE_HAL_RX_THRESH_CFG_REPL_RED_THR_2(val)	    vBIT(val, 32, 8)
#define	VXGE_HAL_RX_THRESH_CFG_REPL_RED_THR_3(val)	    vBIT(val, 40, 8)
#define	VXGE_HAL_RX_THRESH_CFG_REPL_GLOBAL_WOL_EN	    mBIT(62)
#define	VXGE_HAL_RX_THRESH_CFG_REPL_EXACT_VP_MATCH_REQ	    mBIT(63)
	u8	unused033b8[0x033b8-0x03108];

/* 0x033b8 */	u64	fbmc_ecc_cfg;
#define	VXGE_HAL_FBMC_ECC_CFG_ENABLE(val)		    vBIT(val, 3, 5)
	u8	unused03400[0x03400-0x033c0];

/* 0x03400 */	u64	pcipif_int_status;
#define	VXGE_HAL_PCIPIF_INT_STATUS_DBECC_ERR_DBECC_ERR_INT  mBIT(3)
#define	VXGE_HAL_PCIPIF_INT_STATUS_SBECC_ERR_SBECC_ERR_INT  mBIT(7)
#define	VXGE_HAL_PCIPIF_INT_STATUS_GENERAL_ERR_GENERAL_ERR_INT mBIT(11)
#define	VXGE_HAL_PCIPIF_INT_STATUS_SRPCIM_MSG_SRPCIM_MSG_INT mBIT(15)
#define	VXGE_HAL_PCIPIF_INT_STATUS_MRPCIM_SPARE_R1_MRPCIM_SPARE_R1_INT mBIT(19)
/* 0x03408 */	u64	pcipif_int_mask;
/* 0x03410 */	u64	dbecc_err_reg;
#define	VXGE_HAL_DBECC_ERR_REG_PCI_RETRY_BUF_DB_ERR	    mBIT(3)
#define	VXGE_HAL_DBECC_ERR_REG_PCI_RETRY_SOT_DB_ERR	    mBIT(7)
#define	VXGE_HAL_DBECC_ERR_REG_PCI_P_HDR_DB_ERR		    mBIT(11)
#define	VXGE_HAL_DBECC_ERR_REG_PCI_P_DATA_DB_ERR	    mBIT(15)
#define	VXGE_HAL_DBECC_ERR_REG_PCI_NP_HDR_DB_ERR	    mBIT(19)
#define	VXGE_HAL_DBECC_ERR_REG_PCI_NP_DATA_DB_ERR	    mBIT(23)
/* 0x03418 */	u64	dbecc_err_mask;
/* 0x03420 */	u64	dbecc_err_alarm;
/* 0x03428 */	u64	sbecc_err_reg;
#define	VXGE_HAL_SBECC_ERR_REG_PCI_RETRY_BUF_SG_ERR	    mBIT(3)
#define	VXGE_HAL_SBECC_ERR_REG_PCI_RETRY_SOT_SG_ERR	    mBIT(7)
#define	VXGE_HAL_SBECC_ERR_REG_PCI_P_HDR_SG_ERR		    mBIT(11)
#define	VXGE_HAL_SBECC_ERR_REG_PCI_P_DATA_SG_ERR	    mBIT(15)
#define	VXGE_HAL_SBECC_ERR_REG_PCI_NP_HDR_SG_ERR	    mBIT(19)
#define	VXGE_HAL_SBECC_ERR_REG_PCI_NP_DATA_SG_ERR	    mBIT(23)
/* 0x03430 */	u64	sbecc_err_mask;
/* 0x03438 */	u64	sbecc_err_alarm;
/* 0x03440 */	u64	general_err_reg;
#define	VXGE_HAL_GENERAL_ERR_REG_PCI_DROPPED_ILLEGAL_CFG    mBIT(3)
#define	VXGE_HAL_GENERAL_ERR_REG_PCI_ILLEGAL_MEM_MAP_PROG   mBIT(7)
#define	VXGE_HAL_GENERAL_ERR_REG_PCI_LINK_RST_FSM_ERR	    mBIT(11)
#define	VXGE_HAL_GENERAL_ERR_REG_PCI_RX_ILLEGAL_TLP_VPLANE  mBIT(15)
#define	VXGE_HAL_GENERAL_ERR_REG_PCI_TRAINING_RESET_DET	    mBIT(19)
#define	VXGE_HAL_GENERAL_ERR_REG_PCI_PCI_LINK_DOWN_DET	    mBIT(23)
#define	VXGE_HAL_GENERAL_ERR_REG_PCI_RESET_ACK_DLLP	    mBIT(27)
/* 0x03448 */	u64	general_err_mask;
/* 0x03450 */	u64	general_err_alarm;
/* 0x03458 */	u64	srpcim_msg_reg;
#define	VXGE_HAL_SRPCIM_MSG_REG_SWIF_SRPCIM_TO_MRPCIM_VPLANE0_RMSG_INT	mBIT(0)
#define	VXGE_HAL_SRPCIM_MSG_REG_SWIF_SRPCIM_TO_MRPCIM_VPLANE1_RMSG_INT	mBIT(1)
#define	VXGE_HAL_SRPCIM_MSG_REG_SWIF_SRPCIM_TO_MRPCIM_VPLANE2_RMSG_INT	mBIT(2)
#define	VXGE_HAL_SRPCIM_MSG_REG_SWIF_SRPCIM_TO_MRPCIM_VPLANE3_RMSG_INT	mBIT(3)
#define	VXGE_HAL_SRPCIM_MSG_REG_SWIF_SRPCIM_TO_MRPCIM_VPLANE4_RMSG_INT	mBIT(4)
#define	VXGE_HAL_SRPCIM_MSG_REG_SWIF_SRPCIM_TO_MRPCIM_VPLANE5_RMSG_INT	mBIT(5)
#define	VXGE_HAL_SRPCIM_MSG_REG_SWIF_SRPCIM_TO_MRPCIM_VPLANE6_RMSG_INT	mBIT(6)
#define	VXGE_HAL_SRPCIM_MSG_REG_SWIF_SRPCIM_TO_MRPCIM_VPLANE7_RMSG_INT	mBIT(7)
#define	VXGE_HAL_SRPCIM_MSG_REG_SWIF_SRPCIM_TO_MRPCIM_VPLANE8_RMSG_INT	mBIT(8)
#define	VXGE_HAL_SRPCIM_MSG_REG_SWIF_SRPCIM_TO_MRPCIM_VPLANE9_RMSG_INT	mBIT(9)
#define	VXGE_HAL_SRPCIM_MSG_REG_SWIF_SRPCIM_TO_MRPCIM_VPLANE10_RMSG_INT	mBIT(10)
#define	VXGE_HAL_SRPCIM_MSG_REG_SWIF_SRPCIM_TO_MRPCIM_VPLANE11_RMSG_INT	mBIT(11)
#define	VXGE_HAL_SRPCIM_MSG_REG_SWIF_SRPCIM_TO_MRPCIM_VPLANE12_RMSG_INT	mBIT(12)
#define	VXGE_HAL_SRPCIM_MSG_REG_SWIF_SRPCIM_TO_MRPCIM_VPLANE13_RMSG_INT	mBIT(13)
#define	VXGE_HAL_SRPCIM_MSG_REG_SWIF_SRPCIM_TO_MRPCIM_VPLANE14_RMSG_INT	mBIT(14)
#define	VXGE_HAL_SRPCIM_MSG_REG_SWIF_SRPCIM_TO_MRPCIM_VPLANE15_RMSG_INT	mBIT(15)
#define	VXGE_HAL_SRPCIM_MSG_REG_SWIF_SRPCIM_TO_MRPCIM_VPLANE16_RMSG_INT	mBIT(16)
/* 0x03460 */	u64	srpcim_msg_mask;
/* 0x03468 */	u64	srpcim_msg_alarm;
	u8	unused03600[0x03600-0x03470];

/* 0x03600 */	u64	gcmg1_int_status;
#define	VXGE_HAL_GCMG1_INT_STATUS_GSSCC_ERR_GSSCC_INT	    mBIT(0)
#define	VXGE_HAL_GCMG1_INT_STATUS_GSSC0_ERR0_GSSC0_0_INT    mBIT(1)
#define	VXGE_HAL_GCMG1_INT_STATUS_GSSC0_ERR1_GSSC0_1_INT    mBIT(2)
#define	VXGE_HAL_GCMG1_INT_STATUS_GSSC1_ERR0_GSSC1_0_INT    mBIT(3)
#define	VXGE_HAL_GCMG1_INT_STATUS_GSSC1_ERR1_GSSC1_1_INT    mBIT(4)
#define	VXGE_HAL_GCMG1_INT_STATUS_GSSC2_ERR0_GSSC2_0_INT    mBIT(5)
#define	VXGE_HAL_GCMG1_INT_STATUS_GSSC2_ERR1_GSSC2_1_INT    mBIT(6)
#define	VXGE_HAL_GCMG1_INT_STATUS_UQM_ERR_UQM_INT	    mBIT(7)
#define	VXGE_HAL_GCMG1_INT_STATUS_GQCC_ERR_GQCC_INT	    mBIT(8)
/* 0x03608 */	u64	gcmg1_int_mask;
	u8	unused03a00[0x03a00-0x03610];

/* 0x03a00 */	u64	pcmg1_int_status;
#define	VXGE_HAL_PCMG1_INT_STATUS_PSSCC_ERR_PSSCC_INT	    mBIT(0)
#define	VXGE_HAL_PCMG1_INT_STATUS_PQCC_ERR_PQCC_INT	    mBIT(1)
#define	VXGE_HAL_PCMG1_INT_STATUS_PQCC_CQM_ERR_PQCC_CQM_INT mBIT(2)
#define	VXGE_HAL_PCMG1_INT_STATUS_PQCC_SQM_ERR_PQCC_SQM_INT mBIT(3)
/* 0x03a08 */	u64	pcmg1_int_mask;
	u8	unused04000[0x04000-0x03a10];

/* 0x04000 */	u64	one_int_status;
#define	VXGE_HAL_ONE_INT_STATUS_RXPE_ERR_RXPE_INT	    mBIT(7)
#define	VXGE_HAL_ONE_INT_STATUS_TXPE_BCC_MEM_SG_ECC_ERR_TXPE_BCC_MEM_SG_ECC_INT\
							    mBIT(13)
#define	VXGE_HAL_ONE_INT_STATUS_TXPE_BCC_MEM_DB_ECC_ERR_TXPE_BCC_MEM_DB_ECC_INT\
							    mBIT(14)
#define	VXGE_HAL_ONE_INT_STATUS_TXPE_ERR_TXPE_INT	    mBIT(15)
#define	VXGE_HAL_ONE_INT_STATUS_DLM_ERR_DLM_INT		    mBIT(23)
#define	VXGE_HAL_ONE_INT_STATUS_PE_ERR_PE_INT		    mBIT(31)
#define	VXGE_HAL_ONE_INT_STATUS_RPE_ERR_RPE_INT		    mBIT(39)
#define	VXGE_HAL_ONE_INT_STATUS_RPE_FSM_ERR_RPE_FSM_INT	    mBIT(47)
#define	VXGE_HAL_ONE_INT_STATUS_OES_ERR_OES_INT		    mBIT(55)
/* 0x04008 */	u64	one_int_mask;
	u8	unused04818[0x04818-0x04010];

/* 0x04818 */	u64	noa_wct_ctrl;
#define	VXGE_HAL_NOA_WCT_CTRL_VP_INT_NUM		    mBIT(0)
/* 0x04820 */	u64	rc_cfg2;
#define	VXGE_HAL_RC_CFG2_BUFF1_SIZE(val)		    vBIT(val, 0, 16)
#define	VXGE_HAL_RC_CFG2_BUFF2_SIZE(val)		    vBIT(val, 16, 16)
#define	VXGE_HAL_RC_CFG2_BUFF3_SIZE(val)		    vBIT(val, 32, 16)
#define	VXGE_HAL_RC_CFG2_BUFF4_SIZE(val)		    vBIT(val, 48, 16)
/* 0x04828 */	u64	rc_cfg3;
#define	VXGE_HAL_RC_CFG3_BUFF5_SIZE(val)		    vBIT(val, 0, 16)
/* 0x04830 */	u64	rx_multi_cast_ctrl1;
#define	VXGE_HAL_RX_MULTI_CAST_CTRL1_ENABLE		    mBIT(7)
#define	VXGE_HAL_RX_MULTI_CAST_CTRL1_DELAY_COUNT(val)	    vBIT(val, 11, 5)
/* 0x04838 */	u64	rxdm_dbg_rd;
#define	VXGE_HAL_RXDM_DBG_RD_ADDR(val)			    vBIT(val, 0, 12)
#define	VXGE_HAL_RXDM_DBG_RD_ENABLE			    mBIT(31)
/* 0x04840 */	u64	rxdm_dbg_rd_data;
#define	VXGE_HAL_RXDM_DBG_RD_DATA_RMC_RXDM_DBG_RD_DATA(val) vBIT(val, 0, 64)
/* 0x04848 */	u64	rqa_top_prty_for_vh[17];
#define	VXGE_HAL_RQA_TOP_PRTY_FOR_VH_RQA_TOP_PRTY_FOR_VH(val) vBIT(val, 59, 5)
	u8	unused04900[0x04900-0x048d0];

/* 0x04900 */	u64	tim_status;
#define	VXGE_HAL_TIM_STATUS_TIM_RESET_IN_PROGRESS	    mBIT(0)
/* 0x04908 */	u64	tim_ecc_enable;
#define	VXGE_HAL_TIM_ECC_ENABLE_VBLS_N			    mBIT(7)
#define	VXGE_HAL_TIM_ECC_ENABLE_BMAP_N			    mBIT(15)
#define	VXGE_HAL_TIM_ECC_ENABLE_BMAP_MSG_N		    mBIT(23)
/* 0x04910 */	u64	tim_bp_ctrl;
#define	VXGE_HAL_TIM_BP_CTRL_RD_XON			    mBIT(7)
#define	VXGE_HAL_TIM_BP_CTRL_WR_XON			    mBIT(15)
#define	VXGE_HAL_TIM_BP_CTRL_ROCRC_BYP			    mBIT(23)
/* 0x04918 */	u64	tim_resource_assignment_vh[17];
#define	VXGE_HAL_TIM_RESOURCE_ASSIGNMENT_VH_BMAP_ROOT(val)  vBIT(val, 0, 32)
/* 0x049a0 */	u64	tim_bmap_mapping_vp_err[17];
#define	VXGE_HAL_TIM_BMAP_MAPPING_VP_ERR_TIM_DEST_VPATH(val) vBIT(val, 3, 5)
	u8	unused04b00[0x04b00-0x04a28];

/* 0x04b00 */	u64	gcmg2_int_status;
#define	VXGE_HAL_GCMG2_INT_STATUS_GXTMC_ERR_GXTMC_INT	    mBIT(7)
#define	VXGE_HAL_GCMG2_INT_STATUS_GCP_ERR_GCP_INT	    mBIT(15)
#define	VXGE_HAL_GCMG2_INT_STATUS_CMC_ERR_CMC_INT	    mBIT(23)
/* 0x04b08 */	u64	gcmg2_int_mask;
/* 0x04b10 */	u64	gxtmc_err_reg;
#define	VXGE_HAL_GXTMC_ERR_REG_XTMC_BDT_MEM_DB_ERR(val)	    vBIT(val, 0, 4)
#define	VXGE_HAL_GXTMC_ERR_REG_XTMC_BDT_MEM_SG_ERR(val)	    vBIT(val, 4, 4)
#define	VXGE_HAL_GXTMC_ERR_REG_XTMC_CMC_RD_DATA_DB_ERR	    mBIT(8)
#define	VXGE_HAL_GXTMC_ERR_REG_XTMC_REQ_FIFO_ERR	    mBIT(9)
#define	VXGE_HAL_GXTMC_ERR_REG_XTMC_REQ_DATA_FIFO_ERR	    mBIT(10)
#define	VXGE_HAL_GXTMC_ERR_REG_XTMC_WR_RSP_FIFO_ERR	    mBIT(11)
#define	VXGE_HAL_GXTMC_ERR_REG_XTMC_RD_RSP_FIFO_ERR	    mBIT(12)
#define	VXGE_HAL_GXTMC_ERR_REG_XTMC_CMI_WRP_FIFO_ERR	    mBIT(13)
#define	VXGE_HAL_GXTMC_ERR_REG_XTMC_CMI_WRP_ERR		    mBIT(14)
#define	VXGE_HAL_GXTMC_ERR_REG_XTMC_CMI_RRP_FIFO_ERR	    mBIT(15)
#define	VXGE_HAL_GXTMC_ERR_REG_XTMC_CMI_RRP_ERR		    mBIT(16)
#define	VXGE_HAL_GXTMC_ERR_REG_XTMC_CMI_DATA_SM_ERR	    mBIT(17)
#define	VXGE_HAL_GXTMC_ERR_REG_XTMC_CMI_CMC0_IF_ERR	    mBIT(18)
#define	VXGE_HAL_GXTMC_ERR_REG_XTMC_BDT_CMI_ARB_SM_ERR	    mBIT(19)
#define	VXGE_HAL_GXTMC_ERR_REG_XTMC_BDT_CMI_CFC_SM_ERR	    mBIT(20)
#define	VXGE_HAL_GXTMC_ERR_REG_XTMC_BDT_CMI_DFETCH_CREDIT_OVERFLOW mBIT(21)
#define	VXGE_HAL_GXTMC_ERR_REG_XTMC_BDT_CMI_DFETCH_CREDIT_UNDERFLOW mBIT(22)
#define	VXGE_HAL_GXTMC_ERR_REG_XTMC_BDT_CMI_DFETCH_SM_ERR   mBIT(23)
#define	VXGE_HAL_GXTMC_ERR_REG_XTMC_BDT_CMI_RCTRL_CREDIT_OVERFLOW mBIT(24)
#define	VXGE_HAL_GXTMC_ERR_REG_XTMC_BDT_CMI_RCTRL_CREDIT_UNDERFLOW mBIT(25)
#define	VXGE_HAL_GXTMC_ERR_REG_XTMC_BDT_CMI_RCTRL_SM_ERR    mBIT(26)
#define	VXGE_HAL_GXTMC_ERR_REG_XTMC_BDT_CMI_WCOMPL_SM_ERR   mBIT(27)
#define	VXGE_HAL_GXTMC_ERR_REG_XTMC_BDT_CMI_WCOMPL_TAG_ERR  mBIT(28)
#define	VXGE_HAL_GXTMC_ERR_REG_XTMC_BDT_CMI_WREQ_SM_ERR	    mBIT(29)
#define	VXGE_HAL_GXTMC_ERR_REG_XTMC_BDT_CMI_WREQ_FIFO_ERR   mBIT(30)
#define	VXGE_HAL_GXTMC_ERR_REG_XTMC_CP2BDT_RFIFO_POP_ERR    mBIT(31)
#define	VXGE_HAL_GXTMC_ERR_REG_XTMC_XTMC_BDT_CMI_OP_ERR	    mBIT(32)
#define	VXGE_HAL_GXTMC_ERR_REG_XTMC_XTMC_BDT_DFETCH_OP_ERR  mBIT(33)
#define	VXGE_HAL_GXTMC_ERR_REG_XTMC_XTMC_BDT_DFIFO_ERR	    mBIT(34)
#define	VXGE_HAL_GXTMC_ERR_REG_XTMC_CMI_ARB_SM_ERR	    mBIT(35)
/* 0x04b18 */	u64	gxtmc_err_mask;
/* 0x04b20 */	u64	gxtmc_err_alarm;
/* 0x04b28 */	u64	cmc_err_reg;
#define	VXGE_HAL_CMC_ERR_REG_CMC_CMC_SM_ERR		    mBIT(0)
/* 0x04b30 */	u64	cmc_err_mask;
/* 0x04b38 */	u64	cmc_err_alarm;
/* 0x04b40 */	u64	gcp_err_reg;
#define	VXGE_HAL_GCP_ERR_REG_CP_H2L2CP_FIFO_ERR		    mBIT(0)
#define	VXGE_HAL_GCP_ERR_REG_CP_STC2CP_FIFO_ERR		    mBIT(1)
#define	VXGE_HAL_GCP_ERR_REG_CP_STE2CP_FIFO_ERR		    mBIT(2)
#define	VXGE_HAL_GCP_ERR_REG_CP_TTE2CP_FIFO_ERR		    mBIT(3)
/* 0x04b48 */	u64	gcp_err_mask;
/* 0x04b50 */	u64	gcp_err_alarm;
	u8	unused04f00[0x04f00-0x04b58];

/* 0x04f00 */	u64	pcmg2_int_status;
#define	VXGE_HAL_PCMG2_INT_STATUS_PXTMC_ERR_PXTMC_INT	    mBIT(7)
#define	VXGE_HAL_PCMG2_INT_STATUS_CP_EXC_CP_XT_EXC_INT	    mBIT(15)
#define	VXGE_HAL_PCMG2_INT_STATUS_CP_ERR_CP_ERR_INT	    mBIT(23)
/* 0x04f08 */	u64	pcmg2_int_mask;
/* 0x04f10 */	u64	pxtmc_err_reg;
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_XT_PIF_SRAM_DB_ERR(val) vBIT(val, 0, 2)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_MPT_REQ_FIFO_ERR	    mBIT(2)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_MPT_PRSP_FIFO_ERR	    mBIT(3)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_MPT_WRSP_FIFO_ERR	    mBIT(4)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_UPT_REQ_FIFO_ERR	    mBIT(5)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_UPT_PRSP_FIFO_ERR	    mBIT(6)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_UPT_WRSP_FIFO_ERR	    mBIT(7)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_CPT_REQ_FIFO_ERR	    mBIT(8)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_CPT_PRSP_FIFO_ERR	    mBIT(9)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_CPT_WRSP_FIFO_ERR	    mBIT(10)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_REQ_FIFO_ERR	    mBIT(11)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_REQ_DATA_FIFO_ERR	    mBIT(12)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_WR_RSP_FIFO_ERR	    mBIT(13)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_RD_RSP_FIFO_ERR	    mBIT(14)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_MPT_REQ_SHADOW_ERR	    mBIT(15)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_MPT_RSP_SHADOW_ERR	    mBIT(16)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_UPT_REQ_SHADOW_ERR	    mBIT(17)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_UPT_RSP_SHADOW_ERR	    mBIT(18)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_CPT_REQ_SHADOW_ERR	    mBIT(19)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_CPT_RSP_SHADOW_ERR	    mBIT(20)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_XIL_SHADOW_ERR	    mBIT(21)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_ARB_SHADOW_ERR	    mBIT(22)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_RAM_SHADOW_ERR	    mBIT(23)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_CMW_SHADOW_ERR	    mBIT(24)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_CMR_SHADOW_ERR	    mBIT(25)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_MPT_REQ_FSM_ERR	    mBIT(26)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_MPT_RSP_FSM_ERR	    mBIT(27)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_UPT_REQ_FSM_ERR	    mBIT(28)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_UPT_RSP_FSM_ERR	    mBIT(29)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_CPT_REQ_FSM_ERR	    mBIT(30)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_CPT_RSP_FSM_ERR	    mBIT(31)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_XIL_FSM_ERR		    mBIT(32)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_ARB_FSM_ERR		    mBIT(33)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_CMW_FSM_ERR		    mBIT(34)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_CMR_FSM_ERR		    mBIT(35)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_MXP_RD_PROT_ERR	    mBIT(36)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_UXP_RD_PROT_ERR	    mBIT(37)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_CXP_RD_PROT_ERR	    mBIT(38)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_MXP_WR_PROT_ERR	    mBIT(39)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_UXP_WR_PROT_ERR	    mBIT(40)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_CXP_WR_PROT_ERR	    mBIT(41)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_MXP_INV_ADDR_ERR	    mBIT(42)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_UXP_INV_ADDR_ERR	    mBIT(43)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_CXP_INV_ADDR_ERR	    mBIT(44)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_MXP_RD_PROT_INFO_ERR    mBIT(45)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_UXP_RD_PROT_INFO_ERR    mBIT(46)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_CXP_RD_PROT_INFO_ERR    mBIT(47)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_MXP_WR_PROT_INFO_ERR    mBIT(48)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_UXP_WR_PROT_INFO_ERR    mBIT(49)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_CXP_WR_PROT_INFO_ERR    mBIT(50)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_MXP_INV_ADDR_INFO_ERR   mBIT(51)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_UXP_INV_ADDR_INFO_ERR   mBIT(52)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_CXP_INV_ADDR_INFO_ERR   mBIT(53)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_XT_PIF_SRAM_SG_ERR(val) vBIT(val, 54, 2)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_CP2BDT_DFIFO_PUSH_ERR   mBIT(56)
#define	VXGE_HAL_PXTMC_ERR_REG_XTMC_CP2BDT_RFIFO_PUSH_ERR   mBIT(57)
/* 0x04f18 */	u64	pxtmc_err_mask;
/* 0x04f20 */	u64	pxtmc_err_alarm;
/* 0x04f28 */	u64	cp_err_reg;
#define	VXGE_HAL_CP_ERR_REG_CP_CP_DCACHE_SG_ERR(val)	    vBIT(val, 0, 8)
#define	VXGE_HAL_CP_ERR_REG_CP_CP_ICACHE_SG_ERR(val)	    vBIT(val, 8, 2)
#define	VXGE_HAL_CP_ERR_REG_CP_CP_DTAG_SG_ERR		    mBIT(10)
#define	VXGE_HAL_CP_ERR_REG_CP_CP_ITAG_SG_ERR		    mBIT(11)
#define	VXGE_HAL_CP_ERR_REG_CP_CP_TRACE_SG_ERR		    mBIT(12)
#define	VXGE_HAL_CP_ERR_REG_CP_DMA2CP_SG_ERR		    mBIT(13)
#define	VXGE_HAL_CP_ERR_REG_CP_MP2CP_SG_ERR		    mBIT(14)
#define	VXGE_HAL_CP_ERR_REG_CP_QCC2CP_SG_ERR		    mBIT(15)
#define	VXGE_HAL_CP_ERR_REG_CP_STC2CP_SG_ERR(val)	    vBIT(val, 16, 2)
#define	VXGE_HAL_CP_ERR_REG_CP_CP_DCACHE_DB_ERR(val)	    vBIT(val, 24, 8)
#define	VXGE_HAL_CP_ERR_REG_CP_CP_ICACHE_DB_ERR(val)	    vBIT(val, 32, 2)
#define	VXGE_HAL_CP_ERR_REG_CP_CP_DTAG_DB_ERR		    mBIT(34)
#define	VXGE_HAL_CP_ERR_REG_CP_CP_ITAG_DB_ERR		    mBIT(35)
#define	VXGE_HAL_CP_ERR_REG_CP_CP_TRACE_DB_ERR		    mBIT(36)
#define	VXGE_HAL_CP_ERR_REG_CP_DMA2CP_DB_ERR		    mBIT(37)
#define	VXGE_HAL_CP_ERR_REG_CP_MP2CP_DB_ERR		    mBIT(38)
#define	VXGE_HAL_CP_ERR_REG_CP_QCC2CP_DB_ERR		    mBIT(39)
#define	VXGE_HAL_CP_ERR_REG_CP_STC2CP_DB_ERR(val)	    vBIT(val, 40, 2)
#define	VXGE_HAL_CP_ERR_REG_CP_H2L2CP_FIFO_ERR		    mBIT(48)
#define	VXGE_HAL_CP_ERR_REG_CP_STC2CP_FIFO_ERR		    mBIT(49)
#define	VXGE_HAL_CP_ERR_REG_CP_STE2CP_FIFO_ERR		    mBIT(50)
#define	VXGE_HAL_CP_ERR_REG_CP_TTE2CP_FIFO_ERR		    mBIT(51)
#define	VXGE_HAL_CP_ERR_REG_CP_SWIF2CP_FIFO_ERR		    mBIT(52)
#define	VXGE_HAL_CP_ERR_REG_CP_CP2DMA_FIFO_ERR		    mBIT(53)
#define	VXGE_HAL_CP_ERR_REG_CP_DAM2CP_FIFO_ERR		    mBIT(54)
#define	VXGE_HAL_CP_ERR_REG_CP_MP2CP_FIFO_ERR		    mBIT(55)
#define	VXGE_HAL_CP_ERR_REG_CP_QCC2CP_FIFO_ERR		    mBIT(56)
#define	VXGE_HAL_CP_ERR_REG_CP_DMA2CP_FIFO_ERR		    mBIT(57)
#define	VXGE_HAL_CP_ERR_REG_CP_CP_WAKE_FSM_INTEGRITY_ERR    mBIT(60)
#define	VXGE_HAL_CP_ERR_REG_CP_CP_PMON_FSM_INTEGRITY_ERR    mBIT(61)
#define	VXGE_HAL_CP_ERR_REG_CP_DMA_RD_SHADOW_ERR	    mBIT(62)
#define	VXGE_HAL_CP_ERR_REG_CP_PIFT_CREDIT_ERR		    mBIT(63)
/* 0x04f30 */	u64	cp_err_mask;
/* 0x04f38 */	u64	cp_err_alarm;
	u8	unused04f50[0x04f50-0x04f40];

/* 0x04f50 */	u64	cp_exc_reg;
#define	VXGE_HAL_CP_EXC_REG_CP_CP_CAUSE_INFO_INT	    mBIT(47)
#define	VXGE_HAL_CP_EXC_REG_CP_CP_CAUSE_CRIT_INT	    mBIT(55)
#define	VXGE_HAL_CP_EXC_REG_CP_CP_SERR			    mBIT(63)
/* 0x04f58 */	u64	cp_exc_mask;
/* 0x04f60 */	u64	cp_exc_alarm;
/* 0x04f68 */	u64	cp_exc_cause;
#define	VXGE_HAL_CP_EXC_CAUSE_CP_CP_CAUSE(val)		    vBIT(val, 32, 32)
	u8	unused05200[0x05200-0x04f70];

/* 0x05200 */	u64	msg_int_status;
#define	VXGE_HAL_MSG_INT_STATUS_TIM_ERR_TIM_INT		    mBIT(7)
#define	VXGE_HAL_MSG_INT_STATUS_MSG_EXC_MSG_XT_EXC_INT	    mBIT(60)
#define	VXGE_HAL_MSG_INT_STATUS_MSG_ERR3_MSG_ERR3_INT	    mBIT(61)
#define	VXGE_HAL_MSG_INT_STATUS_MSG_ERR2_MSG_ERR2_INT	    mBIT(62)
#define	VXGE_HAL_MSG_INT_STATUS_MSG_ERR_MSG_ERR_INT	    mBIT(63)
/* 0x05208 */	u64	msg_int_mask;
/* 0x05210 */	u64	tim_err_reg;
#define	VXGE_HAL_TIM_ERR_REG_TIM_VBLS_SG_ERR		    mBIT(4)
#define	VXGE_HAL_TIM_ERR_REG_TIM_BMAP_PA_SG_ERR		    mBIT(5)
#define	VXGE_HAL_TIM_ERR_REG_TIM_BMAP_PB_SG_ERR		    mBIT(6)
#define	VXGE_HAL_TIM_ERR_REG_TIM_BMAP_MSG_SG_ERR	    mBIT(7)
#define	VXGE_HAL_TIM_ERR_REG_TIM_VBLS_DB_ERR		    mBIT(12)
#define	VXGE_HAL_TIM_ERR_REG_TIM_BMAP_PA_DB_ERR		    mBIT(13)
#define	VXGE_HAL_TIM_ERR_REG_TIM_BMAP_PB_DB_ERR		    mBIT(14)
#define	VXGE_HAL_TIM_ERR_REG_TIM_BMAP_MSG_DB_ERR	    mBIT(15)
#define	VXGE_HAL_TIM_ERR_REG_TIM_BMAP_MEM_CNTRL_SM_ERR	    mBIT(18)
#define	VXGE_HAL_TIM_ERR_REG_TIM_BMAP_MSG_MEM_CNTRL_SM_ERR mBIT(19)
#define	VXGE_HAL_TIM_ERR_REG_TIM_MPIF_PCIWR_ERR		    mBIT(20)
#define	VXGE_HAL_TIM_ERR_REG_TIM_ROCRC_BMAP_UPDT_FIFO_ERR   mBIT(22)
#define	VXGE_HAL_TIM_ERR_REG_TIM_CREATE_BMAPMSG_FIFO_ERR    mBIT(23)
#define	VXGE_HAL_TIM_ERR_REG_TIM_ROCRCIF_MISMATCH	    mBIT(46)
#define	VXGE_HAL_TIM_ERR_REG_TIM_BMAP_MAPPING_VP_ERR(n)	    mBIT(n)
/* 0x05218 */	u64	tim_err_mask;
/* 0x05220 */	u64	tim_err_alarm;
/* 0x05228 */	u64	msg_err_reg;
#define	VXGE_HAL_MSG_ERR_REG_UP_UXP_WAKE_FSM_INTEGRITY_ERR  mBIT(0)
#define	VXGE_HAL_MSG_ERR_REG_MP_MXP_WAKE_FSM_INTEGRITY_ERR  mBIT(1)
#define	VXGE_HAL_MSG_ERR_REG_MSG_QUE_DMQ_DMA_READ_CMD_FSM_INTEGRITY_ERR mBIT(2)
#define	VXGE_HAL_MSG_ERR_REG_MSG_QUE_DMQ_DMA_RESP_FSM_INTEGRITY_ERR	mBIT(3)
#define	VXGE_HAL_MSG_ERR_REG_MSG_QUE_DMQ_OWN_FSM_INTEGRITY_ERR	mBIT(4)
#define	VXGE_HAL_MSG_ERR_REG_MSG_QUE_PDA_ACC_FSM_INTEGRITY_ERR	mBIT(5)
#define	VXGE_HAL_MSG_ERR_REG_MP_MXP_PMON_FSM_INTEGRITY_ERR  mBIT(6)
#define	VXGE_HAL_MSG_ERR_REG_UP_UXP_PMON_FSM_INTEGRITY_ERR  mBIT(7)
#define	VXGE_HAL_MSG_ERR_REG_UP_UXP_DTAG_SG_ERR		    mBIT(8)
#define	VXGE_HAL_MSG_ERR_REG_UP_UXP_ITAG_SG_ERR		    mBIT(10)
#define	VXGE_HAL_MSG_ERR_REG_MP_MXP_DTAG_SG_ERR		    mBIT(12)
#define	VXGE_HAL_MSG_ERR_REG_MP_MXP_ITAG_SG_ERR		    mBIT(14)
#define	VXGE_HAL_MSG_ERR_REG_UP_UXP_TRACE_SG_ERR	    mBIT(16)
#define	VXGE_HAL_MSG_ERR_REG_MP_MXP_TRACE_SG_ERR	    mBIT(17)
#define	VXGE_HAL_MSG_ERR_REG_MSG_QUE_CMG2MSG_SG_ERR	    mBIT(18)
#define	VXGE_HAL_MSG_ERR_REG_MSG_QUE_TXPE2MSG_SG_ERR	    mBIT(19)
#define	VXGE_HAL_MSG_ERR_REG_MSG_QUE_RXPE2MSG_SG_ERR	    mBIT(20)
#define	VXGE_HAL_MSG_ERR_REG_MSG_QUE_RPE2MSG_SG_ERR	    mBIT(21)
#define	VXGE_HAL_MSG_ERR_REG_MSG_QUE_UMQ_SG_ERR		    mBIT(26)
#define	VXGE_HAL_MSG_ERR_REG_MSG_QUE_BWR_PF_SG_ERR	    mBIT(27)
#define	VXGE_HAL_MSG_ERR_REG_MSG_QUE_DMQ_ECC_SG_ERR	    mBIT(29)
#define	VXGE_HAL_MSG_ERR_REG_MSG_QUE_DMA_RESP_ECC_SG_ERR    mBIT(31)
#define	VXGE_HAL_MSG_ERR_REG_MSG_XFMDQRY_FSM_INTEGRITY_ERR  mBIT(33)
#define	VXGE_HAL_MSG_ERR_REG_MSG_FRMQRY_FSM_INTEGRITY_ERR   mBIT(34)
#define	VXGE_HAL_MSG_ERR_REG_MSG_QUE_UMQ_WRITE_FSM_INTEGRITY_ERR mBIT(35)
#define	VXGE_HAL_MSG_ERR_REG_MSG_QUE_UMQ_BWR_PF_FSM_INTEGRITY_ERR mBIT(36)
#define	VXGE_HAL_MSG_ERR_REG_MSG_QUE_REG_RESP_FIFO_ERR	    mBIT(38)
#define	VXGE_HAL_MSG_ERR_REG_UP_UXP_DTAG_DB_ERR		    mBIT(39)
#define	VXGE_HAL_MSG_ERR_REG_UP_UXP_ITAG_DB_ERR		    mBIT(41)
#define	VXGE_HAL_MSG_ERR_REG_MP_MXP_DTAG_DB_ERR		    mBIT(43)
#define	VXGE_HAL_MSG_ERR_REG_MP_MXP_ITAG_DB_ERR		    mBIT(45)
#define	VXGE_HAL_MSG_ERR_REG_UP_UXP_TRACE_DB_ERR	    mBIT(47)
#define	VXGE_HAL_MSG_ERR_REG_MP_MXP_TRACE_DB_ERR	    mBIT(48)
#define	VXGE_HAL_MSG_ERR_REG_MSG_QUE_CMG2MSG_DB_ERR	    mBIT(49)
#define	VXGE_HAL_MSG_ERR_REG_MSG_QUE_TXPE2MSG_DB_ERR	    mBIT(50)
#define	VXGE_HAL_MSG_ERR_REG_MSG_QUE_RXPE2MSG_DB_ERR	    mBIT(51)
#define	VXGE_HAL_MSG_ERR_REG_MSG_QUE_RPE2MSG_DB_ERR	    mBIT(52)
#define	VXGE_HAL_MSG_ERR_REG_MSG_QUE_REG_READ_FIFO_ERR	    mBIT(53)
#define	VXGE_HAL_MSG_ERR_REG_MSG_QUE_MXP2UXP_FIFO_ERR	    mBIT(54)
#define	VXGE_HAL_MSG_ERR_REG_MSG_QUE_KDFC_SIF_FIFO_ERR	    mBIT(55)
#define	VXGE_HAL_MSG_ERR_REG_MSG_QUE_CXP2SWIF_FIFO_ERR	    mBIT(56)
#define	VXGE_HAL_MSG_ERR_REG_MSG_QUE_UMQ_DB_ERR		    mBIT(57)
#define	VXGE_HAL_MSG_ERR_REG_MSG_QUE_BWR_PF_DB_ERR	    mBIT(58)
#define	VXGE_HAL_MSG_ERR_REG_MSG_QUE_BWR_SIF_FIFO_ERR	    mBIT(59)
#define	VXGE_HAL_MSG_ERR_REG_MSG_QUE_DMQ_ECC_DB_ERR	    mBIT(60)
#define	VXGE_HAL_MSG_ERR_REG_MSG_QUE_DMA_READ_FIFO_ERR	    mBIT(61)
#define	VXGE_HAL_MSG_ERR_REG_MSG_QUE_DMA_RESP_ECC_DB_ERR    mBIT(62)
#define	VXGE_HAL_MSG_ERR_REG_MSG_QUE_UXP2MXP_FIFO_ERR	    mBIT(63)
/* 0x05230 */	u64	msg_err_mask;
/* 0x05238 */	u64	msg_err_alarm;
	u8	unused05340[0x05340-0x05240];

/* 0x05340 */	u64	msg_exc_reg;
#define	VXGE_HAL_MSG_EXC_REG_MP_MXP_CAUSE_INFO_INT	    mBIT(50)
#define	VXGE_HAL_MSG_EXC_REG_MP_MXP_CAUSE_CRIT_INT	    mBIT(51)
#define	VXGE_HAL_MSG_EXC_REG_UP_UXP_CAUSE_INFO_INT	    mBIT(54)
#define	VXGE_HAL_MSG_EXC_REG_UP_UXP_CAUSE_CRIT_INT	    mBIT(55)
#define	VXGE_HAL_MSG_EXC_REG_MP_MXP_SERR		    mBIT(62)
#define	VXGE_HAL_MSG_EXC_REG_UP_UXP_SERR		    mBIT(63)
/* 0x05348 */	u64	msg_exc_mask;
/* 0x05350 */	u64	msg_exc_alarm;
/* 0x05358 */	u64	msg_exc_cause;
#define	VXGE_HAL_MSG_EXC_CAUSE_MP_MXP(val)		    vBIT(val, 0, 32)
#define	VXGE_HAL_MSG_EXC_CAUSE_UP_UXP(val)		    vBIT(val, 32, 32)
	u8	unused05380[0x05380-0x05360];

/* 0x05380 */	u64	msg_err2_reg;
#define	VXGE_HAL_MSG_ERR2_REG_MSG_QUE_CMG2MSG_DISPATCH_FSM_INTEGRITY_ERR mBIT(0)
#define	VXGE_HAL_MSG_ERR2_REG_MSG_QUE_DMQ_DISPATCH_FSM_INTEGRITY_ERR	mBIT(1)
#define	VXGE_HAL_MSG_ERR2_REG_MSG_QUE_SWIF_DISPATCH_FSM_INTEGRITY_ERR	mBIT(2)
#define	VXGE_HAL_MSG_ERR2_REG_MSG_QUE_PIC_WRITE_FSM_INTEGRITY_ERR	mBIT(3)
#define	VXGE_HAL_MSG_ERR2_REG_MSG_QUE_SWIFREG_FSM_INTEGRITY_ERR		mBIT(4)
#define	VXGE_HAL_MSG_ERR2_REG_MSG_QUE_TIM_WRITE_FSM_INTEGRITY_ERR	mBIT(5)
#define	VXGE_HAL_MSG_ERR2_REG_MSG_QUE_UMQ_TA_FSM_INTEGRITY_ERR	mBIT(6)
#define	VXGE_HAL_MSG_ERR2_REG_MSG_QUE_TXPE_TA_FSM_INTEGRITY_ERR	mBIT(7)
#define	VXGE_HAL_MSG_ERR2_REG_MSG_QUE_RXPE_TA_FSM_INTEGRITY_ERR	mBIT(8)
#define	VXGE_HAL_MSG_ERR2_REG_MSG_QUE_SWIF_TA_FSM_INTEGRITY_ERR	mBIT(9)
#define	VXGE_HAL_MSG_ERR2_REG_MSG_QUE_DMA_TA_FSM_INTEGRITY_ERR	mBIT(10)
#define	VXGE_HAL_MSG_ERR2_REG_MSG_QUE_CP_TA_FSM_INTEGRITY_ERR	mBIT(11)
#define	VXGE_HAL_MSG_ERR2_REG_MSG_QUE_LONGTERMUMQ_TA16_FSM_INTEGRITY_ERR\
							    mBIT(12)
#define	VXGE_HAL_MSG_ERR2_REG_MSG_QUE_LONGTERMUMQ_TA15_FSM_INTEGRITY_ERR\
							    mBIT(13)
#define	VXGE_HAL_MSG_ERR2_REG_MSG_QUE_LONGTERMUMQ_TA14_FSM_INTEGRITY_ERR\
							    mBIT(14)
#define	VXGE_HAL_MSG_ERR2_REG_MSG_QUE_LONGTERMUMQ_TA13_FSM_INTEGRITY_ERR\
							    mBIT(15)
#define	VXGE_HAL_MSG_ERR2_REG_MSG_QUE_LONGTERMUMQ_TA12_FSM_INTEGRITY_ERR\
							    mBIT(16)
#define	VXGE_HAL_MSG_ERR2_REG_MSG_QUE_LONGTERMUMQ_TA11_FSM_INTEGRITY_ERR\
							    mBIT(17)
#define	VXGE_HAL_MSG_ERR2_REG_MSG_QUE_LONGTERMUMQ_TA10_FSM_INTEGRITY_ERR\
							    mBIT(18)
#define	VXGE_HAL_MSG_ERR2_REG_MSG_QUE_LONGTERMUMQ_TA9_FSM_INTEGRITY_ERR	mBIT(19)
#define	VXGE_HAL_MSG_ERR2_REG_MSG_QUE_LONGTERMUMQ_TA8_FSM_INTEGRITY_ERR	mBIT(20)
#define	VXGE_HAL_MSG_ERR2_REG_MSG_QUE_LONGTERMUMQ_TA7_FSM_INTEGRITY_ERR	mBIT(21)
#define	VXGE_HAL_MSG_ERR2_REG_MSG_QUE_LONGTERMUMQ_TA6_FSM_INTEGRITY_ERR	mBIT(22)
#define	VXGE_HAL_MSG_ERR2_REG_MSG_QUE_LONGTERMUMQ_TA5_FSM_INTEGRITY_ERR	mBIT(23)
#define	VXGE_HAL_MSG_ERR2_REG_MSG_QUE_LONGTERMUMQ_TA4_FSM_INTEGRITY_ERR	mBIT(24)
#define	VXGE_HAL_MSG_ERR2_REG_MSG_QUE_LONGTERMUMQ_TA3_FSM_INTEGRITY_ERR	mBIT(25)
#define	VXGE_HAL_MSG_ERR2_REG_MSG_QUE_LONGTERMUMQ_TA2_FSM_INTEGRITY_ERR	mBIT(26)
#define	VXGE_HAL_MSG_ERR2_REG_MSG_QUE_LONGTERMUMQ_TA1_FSM_INTEGRITY_ERR	mBIT(27)
#define	VXGE_HAL_MSG_ERR2_REG_MSG_QUE_LONGTERMUMQ_TA0_FSM_INTEGRITY_ERR	mBIT(28)
#define	VXGE_HAL_MSG_ERR2_REG_MSG_QUE_FBMC_OWN_FSM_INTEGRITY_ERR	mBIT(29)
#define	VXGE_HAL_MSG_ERR2_REG_MSG_QUE_TXPE2MSG_DISPATCH_FSM_INTEGRITY_ERR\
							    mBIT(30)
#define	VXGE_HAL_MSG_ERR2_REG_MSG_QUE_RXPE2MSG_DISPATCH_FSM_INTEGRITY_ERR\
							    mBIT(31)
#define	VXGE_HAL_MSG_ERR2_REG_MSG_QUE_RPE2MSG_DISPATCH_FSM_INTEGRITY_ERR\
							    mBIT(32)
#define	VXGE_HAL_MSG_ERR2_REG_MP_MP_PIFT_IF_CREDIT_CNT_ERR  mBIT(33)
#define	VXGE_HAL_MSG_ERR2_REG_UP_UP_PIFT_IF_CREDIT_CNT_ERR  mBIT(34)
#define	VXGE_HAL_MSG_ERR2_REG_MSG_QUE_UMQ2PIC_CMD_FIFO_ERR  mBIT(62)
#define	VXGE_HAL_MSG_ERR2_REG_TIM_TIM2MSG_CMD_FIFO_ERR	    mBIT(63)
/* 0x05388 */	u64	msg_err2_mask;
/* 0x05390 */	u64	msg_err2_alarm;
/* 0x05398 */	u64	msg_err3_reg;
#define	VXGE_HAL_MSG_ERR3_REG_UP_UXP_DCACHE_SG_ERR0	    mBIT(0)
#define	VXGE_HAL_MSG_ERR3_REG_UP_UXP_DCACHE_SG_ERR1	    mBIT(1)
#define	VXGE_HAL_MSG_ERR3_REG_UP_UXP_DCACHE_SG_ERR2	    mBIT(2)
#define	VXGE_HAL_MSG_ERR3_REG_UP_UXP_DCACHE_SG_ERR3	    mBIT(3)
#define	VXGE_HAL_MSG_ERR3_REG_UP_UXP_DCACHE_SG_ERR4	    mBIT(4)
#define	VXGE_HAL_MSG_ERR3_REG_UP_UXP_DCACHE_SG_ERR5	    mBIT(5)
#define	VXGE_HAL_MSG_ERR3_REG_UP_UXP_DCACHE_SG_ERR6	    mBIT(6)
#define	VXGE_HAL_MSG_ERR3_REG_UP_UXP_DCACHE_SG_ERR7	    mBIT(7)
#define	VXGE_HAL_MSG_ERR3_REG_UP_UXP_ICACHE_SG_ERR0	    mBIT(8)
#define	VXGE_HAL_MSG_ERR3_REG_UP_UXP_ICACHE_SG_ERR1	    mBIT(9)
#define	VXGE_HAL_MSG_ERR3_REG_MP_MXP_DCACHE_SG_ERR0	    mBIT(16)
#define	VXGE_HAL_MSG_ERR3_REG_MP_MXP_DCACHE_SG_ERR1	    mBIT(17)
#define	VXGE_HAL_MSG_ERR3_REG_MP_MXP_DCACHE_SG_ERR2	    mBIT(18)
#define	VXGE_HAL_MSG_ERR3_REG_MP_MXP_DCACHE_SG_ERR3	    mBIT(19)
#define	VXGE_HAL_MSG_ERR3_REG_MP_MXP_DCACHE_SG_ERR4	    mBIT(20)
#define	VXGE_HAL_MSG_ERR3_REG_MP_MXP_DCACHE_SG_ERR5	    mBIT(21)
#define	VXGE_HAL_MSG_ERR3_REG_MP_MXP_DCACHE_SG_ERR6	    mBIT(22)
#define	VXGE_HAL_MSG_ERR3_REG_MP_MXP_DCACHE_SG_ERR7	    mBIT(23)
#define	VXGE_HAL_MSG_ERR3_REG_MP_MXP_ICACHE_SG_ERR0	    mBIT(24)
#define	VXGE_HAL_MSG_ERR3_REG_MP_MXP_ICACHE_SG_ERR1	    mBIT(25)
#define	VXGE_HAL_MSG_ERR3_REG_UP_UXP_DCACHE_DB_ERR0	    mBIT(32)
#define	VXGE_HAL_MSG_ERR3_REG_UP_UXP_DCACHE_DB_ERR1	    mBIT(33)
#define	VXGE_HAL_MSG_ERR3_REG_UP_UXP_DCACHE_DB_ERR2	    mBIT(34)
#define	VXGE_HAL_MSG_ERR3_REG_UP_UXP_DCACHE_DB_ERR3	    mBIT(35)
#define	VXGE_HAL_MSG_ERR3_REG_UP_UXP_DCACHE_DB_ERR4	    mBIT(36)
#define	VXGE_HAL_MSG_ERR3_REG_UP_UXP_DCACHE_DB_ERR5	    mBIT(37)
#define	VXGE_HAL_MSG_ERR3_REG_UP_UXP_DCACHE_DB_ERR6	    mBIT(38)
#define	VXGE_HAL_MSG_ERR3_REG_UP_UXP_DCACHE_DB_ERR7	    mBIT(39)
#define	VXGE_HAL_MSG_ERR3_REG_UP_UXP_ICACHE_DB_ERR0	    mBIT(40)
#define	VXGE_HAL_MSG_ERR3_REG_UP_UXP_ICACHE_DB_ERR1	    mBIT(41)
#define	VXGE_HAL_MSG_ERR3_REG_MP_MXP_DCACHE_DB_ERR0	    mBIT(48)
#define	VXGE_HAL_MSG_ERR3_REG_MP_MXP_DCACHE_DB_ERR1	    mBIT(49)
#define	VXGE_HAL_MSG_ERR3_REG_MP_MXP_DCACHE_DB_ERR2	    mBIT(50)
#define	VXGE_HAL_MSG_ERR3_REG_MP_MXP_DCACHE_DB_ERR3	    mBIT(51)
#define	VXGE_HAL_MSG_ERR3_REG_MP_MXP_DCACHE_DB_ERR4	    mBIT(52)
#define	VXGE_HAL_MSG_ERR3_REG_MP_MXP_DCACHE_DB_ERR5	    mBIT(53)
#define	VXGE_HAL_MSG_ERR3_REG_MP_MXP_DCACHE_DB_ERR6	    mBIT(54)
#define	VXGE_HAL_MSG_ERR3_REG_MP_MXP_DCACHE_DB_ERR7	    mBIT(55)
#define	VXGE_HAL_MSG_ERR3_REG_MP_MXP_ICACHE_DB_ERR0	    mBIT(56)
#define	VXGE_HAL_MSG_ERR3_REG_MP_MXP_ICACHE_DB_ERR1	    mBIT(57)
/* 0x053a0 */	u64	msg_err3_mask;
/* 0x053a8 */	u64	msg_err3_alarm;
	u8	unused05600[0x05600-0x053b0];

/* 0x05600 */	u64	fau_gen_err_reg;
#define	VXGE_HAL_FAU_GEN_ERR_REG_FMPF_PORT0_PERMANENT_STOP  mBIT(3)
#define	VXGE_HAL_FAU_GEN_ERR_REG_FMPF_PORT1_PERMANENT_STOP  mBIT(7)
#define	VXGE_HAL_FAU_GEN_ERR_REG_FMPF_PORT2_PERMANENT_STOP  mBIT(11)
#define	VXGE_HAL_FAU_GEN_ERR_REG_FALR_AUTO_LRO_NOTIF mBIT(15)
/* 0x05608 */	u64	fau_gen_err_mask;
/* 0x05610 */	u64	fau_gen_err_alarm;
/* 0x05618 */	u64	fau_ecc_err_reg;
#define	VXGE_HAL_FAU_ECC_ERR_REG_FAU_PORT0_FAU_MAC2F_N_SG_ERR mBIT(0)
#define	VXGE_HAL_FAU_ECC_ERR_REG_FAU_PORT0_FAU_MAC2F_N_DB_ERR mBIT(1)
#define	VXGE_HAL_FAU_ECC_ERR_REG_FAU_PORT0_FAU_MAC2F_W_SG_ERR(val)\
							    vBIT(val, 2, 2)
#define	VXGE_HAL_FAU_ECC_ERR_REG_FAU_PORT0_FAU_MAC2F_W_DB_ERR(val)\
							    vBIT(val, 4, 2)
#define	VXGE_HAL_FAU_ECC_ERR_REG_FAU_PORT1_FAU_MAC2F_N_SG_ERR mBIT(6)
#define	VXGE_HAL_FAU_ECC_ERR_REG_FAU_PORT1_FAU_MAC2F_N_DB_ERR mBIT(7)
#define	VXGE_HAL_FAU_ECC_ERR_REG_FAU_PORT1_FAU_MAC2F_W_SG_ERR(val)\
							    vBIT(val, 8, 2)
#define	VXGE_HAL_FAU_ECC_ERR_REG_FAU_PORT1_FAU_MAC2F_W_DB_ERR(val)\
							    vBIT(val, 10, 2)
#define	VXGE_HAL_FAU_ECC_ERR_REG_FAU_PORT2_FAU_MAC2F_N_SG_ERR mBIT(12)
#define	VXGE_HAL_FAU_ECC_ERR_REG_FAU_PORT2_FAU_MAC2F_N_DB_ERR mBIT(13)
#define	VXGE_HAL_FAU_ECC_ERR_REG_FAU_PORT2_FAU_MAC2F_W_SG_ERR(val)\
							    vBIT(val, 14, 2)
#define	VXGE_HAL_FAU_ECC_ERR_REG_FAU_PORT2_FAU_MAC2F_W_DB_ERR(val)\
							    vBIT(val, 16, 2)
#define	VXGE_HAL_FAU_ECC_ERR_REG_FAU_FAU_XFMD_INS_SG_ERR(val) vBIT(val, 18, 2)
#define	VXGE_HAL_FAU_ECC_ERR_REG_FAU_FAU_XFMD_INS_DB_ERR(val) vBIT(val, 20, 2)
#define	VXGE_HAL_FAU_ECC_ERR_REG_FAUJ_FAU_FSM_ERR	    mBIT(31)
/* 0x05620 */	u64	fau_ecc_err_mask;
/* 0x05628 */	u64	fau_ecc_err_alarm;
	u8	unused05658[0x05658-0x05630];

/* 0x05658 */	u64	fau_pa_cfg;
#define	VXGE_HAL_FAU_PA_CFG_REPL_L4_COMP_CSUM		    mBIT(3)
#define	VXGE_HAL_FAU_PA_CFG_REPL_L3_INCL_CF		    mBIT(7)
#define	VXGE_HAL_FAU_PA_CFG_REPL_L3_COMP_CSUM		    mBIT(11)
	u8	unused05668[0x05668-0x05660];

/* 0x05668 */	u64	dbg_stats_fau_rx_path;
#define	VXGE_HAL_DBG_STATS_FAU_RX_PATH_RX_PERMITTED_FRMS(val) vBIT(val, 32, 32)
	u8	unused056c0[0x056c0-0x05670];

/* 0x056c0 */	u64	fau_lag_cfg;
#define	VXGE_HAL_FAU_LAG_CFG_COLL_ALG(val)		    vBIT(val, 2, 2)
#define	VXGE_HAL_FAU_LAG_CFG_INCR_RX_AGGR_STATS		    mBIT(7)
	u8	unused05800[0x05800-0x056c8];

/* 0x05800 */	u64	tpa_int_status;
#define	VXGE_HAL_TPA_INT_STATUS_ORP_ERR_ORP_INT		    mBIT(15)
#define	VXGE_HAL_TPA_INT_STATUS_PTM_ALARM_PTM_INT	    mBIT(23)
#define	VXGE_HAL_TPA_INT_STATUS_TPA_ERROR_TPA_INT	    mBIT(31)
/* 0x05808 */	u64	tpa_int_mask;
/* 0x05810 */	u64	orp_err_reg;
#define	VXGE_HAL_ORP_ERR_REG_ORP_FIFO_SG_ERR		    mBIT(3)
#define	VXGE_HAL_ORP_ERR_REG_ORP_FIFO_DB_ERR		    mBIT(7)
#define	VXGE_HAL_ORP_ERR_REG_ORP_XFMD_FIFO_UFLOW_ERR	    mBIT(11)
#define	VXGE_HAL_ORP_ERR_REG_ORP_FRM_FIFO_UFLOW_ERR	    mBIT(15)
#define	VXGE_HAL_ORP_ERR_REG_ORP_XFMD_RCV_FSM_ERR	    mBIT(19)
#define	VXGE_HAL_ORP_ERR_REG_ORP_OUTREAD_FSM_ERR	    mBIT(23)
#define	VXGE_HAL_ORP_ERR_REG_ORP_OUTQEM_FSM_ERR		    mBIT(27)
#define	VXGE_HAL_ORP_ERR_REG_ORP_XFMD_RCV_SHADOW_ERR	    mBIT(31)
#define	VXGE_HAL_ORP_ERR_REG_ORP_OUTREAD_SHADOW_ERR	    mBIT(35)
#define	VXGE_HAL_ORP_ERR_REG_ORP_OUTQEM_SHADOW_ERR	    mBIT(39)
#define	VXGE_HAL_ORP_ERR_REG_ORP_OUTFRM_SHADOW_ERR	    mBIT(43)
#define	VXGE_HAL_ORP_ERR_REG_ORP_OPTPRS_SHADOW_ERR	    mBIT(47)
/* 0x05818 */	u64	orp_err_mask;
/* 0x05820 */	u64	orp_err_alarm;
/* 0x05828 */	u64	ptm_alarm_reg;
#define	VXGE_HAL_PTM_ALARM_REG_PTM_RDCTRL_SYNC_ERR	    mBIT(3)
#define	VXGE_HAL_PTM_ALARM_REG_PTM_RDCTRL_FIFO_ERR	    mBIT(7)
#define	VXGE_HAL_PTM_ALARM_REG_XFMD_RD_FIFO_ERR		    mBIT(11)
#define	VXGE_HAL_PTM_ALARM_REG_WDE2MSR_WR_FIFO_ERR	    mBIT(15)
#define	VXGE_HAL_PTM_ALARM_REG_PTM_FRMM_ECC_DB_ERR(val)	    vBIT(val, 18, 2)
#define	VXGE_HAL_PTM_ALARM_REG_PTM_FRMM_ECC_SG_ERR(val)	    vBIT(val, 22, 2)
/* 0x05830 */	u64	ptm_alarm_mask;
/* 0x05838 */	u64	ptm_alarm_alarm;
/* 0x05840 */	u64	tpa_error_reg;
#define	VXGE_HAL_TPA_ERROR_REG_TPA_FSM_ERR_ALARM	    mBIT(3)
#define	VXGE_HAL_TPA_ERROR_REG_TPA_TPA_DA_LKUP_PRT0_DB_ERR  mBIT(7)
#define	VXGE_HAL_TPA_ERROR_REG_TPA_TPA_DA_LKUP_PRT0_SG_ERR  mBIT(11)
/* 0x05848 */	u64	tpa_error_mask;
/* 0x05850 */	u64	tpa_error_alarm;
/* 0x05858 */	u64	tpa_global_cfg;
#define	VXGE_HAL_TPA_GLOBAL_CFG_SUPPORT_SNAP_AB_N	    mBIT(7)
#define	VXGE_HAL_TPA_GLOBAL_CFG_ECC_ENABLE_N		    mBIT(35)
	u8	unused05870[0x05870-0x05860];

/* 0x05870 */	u64	ptm_ecc_cfg;
#define	VXGE_HAL_PTM_ECC_CFG_PTM_FRMM_ECC_EN_N		    mBIT(3)
/* 0x05878 */	u64	ptm_phase_cfg;
#define	VXGE_HAL_PTM_PHASE_CFG_FRMM_WR_PHASE_EN		    mBIT(3)
#define	VXGE_HAL_PTM_PHASE_CFG_FRMM_RD_PHASE_EN		    mBIT(7)
	u8	unused05898[0x05898-0x05880];

/* 0x05898 */	u64	dbg_stats_tpa_tx_path;
#define	VXGE_HAL_DBG_STATS_TPA_TX_PATH_TX_PERMITTED_FRMS(val) vBIT(val, 32, 32)
	u8	unused05900[0x05900-0x058a0];

/* 0x05900 */	u64	tmac_int_status;
#define	VXGE_HAL_TMAC_INT_STATUS_TXMAC_GEN_ERR_TXMAC_GEN_INT	mBIT(3)
#define	VXGE_HAL_TMAC_INT_STATUS_TXMAC_ECC_ERR_TXMAC_ECC_INT	mBIT(7)
/* 0x05908 */	u64	tmac_int_mask;
/* 0x05910 */	u64	txmac_gen_err_reg;
#define	VXGE_HAL_TXMAC_GEN_ERR_REG_TMACJ_PERMANENT_STOP	    mBIT(3)
#define	VXGE_HAL_TXMAC_GEN_ERR_REG_TMACJ_NO_VALID_VSPORT    mBIT(7)
/* 0x05918 */	u64	txmac_gen_err_mask;
/* 0x05920 */	u64	txmac_gen_err_alarm;
/* 0x05928 */	u64	txmac_ecc_err_reg;
#define	VXGE_HAL_TXMAC_ECC_ERR_REG_TMACJ_TMAC_TPA2MAC_SG_ERR	mBIT(3)
#define	VXGE_HAL_TXMAC_ECC_ERR_REG_TMACJ_TMAC_TPA2MAC_DB_ERR	mBIT(7)
#define	VXGE_HAL_TXMAC_ECC_ERR_REG_TMACJ_TMAC_TPA2M_SB_SG_ERR	mBIT(11)
#define	VXGE_HAL_TXMAC_ECC_ERR_REG_TMACJ_TMAC_TPA2M_SB_DB_ERR	mBIT(15)
#define	VXGE_HAL_TXMAC_ECC_ERR_REG_TMACJ_TMAC_TPA2M_DA_SG_ERR	mBIT(19)
#define	VXGE_HAL_TXMAC_ECC_ERR_REG_TMACJ_TMAC_TPA2M_DA_DB_ERR	mBIT(23)
#define	VXGE_HAL_TXMAC_ECC_ERR_REG_TMAC_TMAC_PORT0_FSM_ERR  mBIT(27)
#define	VXGE_HAL_TXMAC_ECC_ERR_REG_TMAC_TMAC_PORT1_FSM_ERR  mBIT(31)
#define	VXGE_HAL_TXMAC_ECC_ERR_REG_TMAC_TMAC_PORT2_FSM_ERR  mBIT(35)
#define	VXGE_HAL_TXMAC_ECC_ERR_REG_TMACJ_TMACJ_FSM_ERR	    mBIT(39)
/* 0x05930 */	u64	txmac_ecc_err_mask;
/* 0x05938 */	u64	txmac_ecc_err_alarm;
	u8	unused05978[0x05978-0x05940];

/* 0x05978 */	u64	dbg_stat_tx_any_frms;
#define	VXGE_HAL_DBG_STAT_TX_ANY_FRMS_PORT0_TX_ANY_FRMS(val) vBIT(val, 0, 8)
#define	VXGE_HAL_DBG_STAT_TX_ANY_FRMS_PORT1_TX_ANY_FRMS(val) vBIT(val, 8, 8)
#define	VXGE_HAL_DBG_STAT_TX_ANY_FRMS_PORT2_TX_ANY_FRMS(val) vBIT(val, 16, 8)
	u8	unused059a0[0x059a0-0x05980];

/* 0x059a0 */	u64	txmac_link_util_port[3];
#define	VXGE_HAL_TXMAC_LINK_UTIL_PORT_TMAC_TMAC_UTILIZATION(val) vBIT(val, 1, 7)
#define	VXGE_HAL_TXMAC_LINK_UTIL_PORT_TMAC_UTIL_CFG(val)    vBIT(val, 8, 4)
#define	VXGE_HAL_TXMAC_LINK_UTIL_PORT_TMAC_TMAC_FRAC_UTIL(val) vBIT(val, 12, 4)
#define	VXGE_HAL_TXMAC_LINK_UTIL_PORT_TMAC_PKT_WEIGHT(val)  vBIT(val, 16, 4)
#define	VXGE_HAL_TXMAC_LINK_UTIL_PORT_TMAC_TMAC_SCALE_FACTOR mBIT(23)
/* 0x059b8 */	u64	txmac_cfg0_port[3];
#define	VXGE_HAL_TXMAC_CFG0_PORT_TMAC_EN		    mBIT(3)
#define	VXGE_HAL_TXMAC_CFG0_PORT_APPEND_PAD		    mBIT(7)
#define	VXGE_HAL_TXMAC_CFG0_PORT_PAD_BYTE(val)		    vBIT(val, 8, 8)
/* 0x059d0 */	u64	txmac_cfg1_port[3];
#define	VXGE_HAL_TXMAC_CFG1_PORT_AVG_IPG(val)		    vBIT(val, 40, 8)
/* 0x059e8 */	u64	txmac_status_port[3];
#define	VXGE_HAL_TXMAC_STATUS_PORT_TMAC_TX_FRM_SENT	    mBIT(3)
	u8	unused05a20[0x05a20-0x05a00];

/* 0x05a20 */	u64	lag_distrib_dest;
#define	VXGE_HAL_LAG_DISTRIB_DEST_MAP_VPATH(n)		    mBIT(n)
/* 0x05a28 */	u64	lag_marker_cfg;
#define	VXGE_HAL_LAG_MARKER_CFG_GEN_RCVR_EN		    mBIT(3)
#define	VXGE_HAL_LAG_MARKER_CFG_RESP_EN			    mBIT(7)
#define	VXGE_HAL_LAG_MARKER_CFG_RESP_TIMEOUT(val)	    vBIT(val, 16, 16)
#define	VXGE_HAL_LAG_MARKER_CFG_SLOW_PROTO_MRKR_MIN_INTERVAL(val)\
							    vBIT(val, 32, 16)
#define	VXGE_HAL_LAG_MARKER_CFG_THROTTLE_MRKR_RESP	    mBIT(51)
/* 0x05a30 */	u64	lag_tx_cfg;
#define	VXGE_HAL_LAG_TX_CFG_INCR_TX_AGGR_STATS		    mBIT(3)
#define	VXGE_HAL_LAG_TX_CFG_DISTRIB_ALG_SEL(val)	    vBIT(val, 6, 2)
#define	VXGE_HAL_LAG_TX_CFG_DISTRIB_REMAP_IF_FAIL	    mBIT(11)
#define	VXGE_HAL_LAG_TX_CFG_COLL_MAX_DELAY(val)		    vBIT(val, 16, 16)
/* 0x05a38 */	u64	lag_tx_status;
#define	VXGE_HAL_LAG_TX_STATUS_TLAG_TIMER_VAL_EMPTIED_LINK(val) vBIT(val, 0, 8)
#define	VXGE_HAL_LAG_TX_STATUS_TLAG_TIMER_VAL_SLOW_PROTO_MRKR(val)\
							    vBIT(val, 8, 8)
#define	VXGE_HAL_LAG_TX_STATUS_TLAG_TIMER_VAL_SLOW_PROTO_MRKRRESP(val)\
							    vBIT(val, 16, 8)
	u8	unused05d48[0x05d48-0x05a40];

/* 0x05d48 */	u64	srpcim_to_mrpcim_vplane_rmsg[17];
#define	VXGE_HAL_SRPCIM_TO_MRPCIM_VPLANE_RMSG_RMSG(val)	    vBIT(val, 0, 64)
	u8	unused06420[0x06420-0x05dd0];

/* 0x06420 */	u64	mrpcim_to_srpcim_vplane_wmsg[17];
#define	VXGE_HAL_MRPCIM_TO_SRPCIM_VPLANE_WMSG_WMSG(val)	    vBIT(val, 0, 64)
/* 0x064a8 */	u64	mrpcim_to_srpcim_vplane_wmsg_trig[17];
#define	VXGE_HAL_MRPCIM_TO_SRPCIM_VPLANE_WMSG_TRIG_TRIG	    mBIT(0)
/* 0x06530 */	u64	debug_stats0;
#define	VXGE_HAL_DEBUG_STATS0_RSTDROP_MSG(val)		    vBIT(val, 0, 32)
#define	VXGE_HAL_DEBUG_STATS0_RSTDROP_CPL(val)		    vBIT(val, 32, 32)
/* 0x06538 */	u64	debug_stats1;
#define	VXGE_HAL_DEBUG_STATS1_RSTDROP_CLIENT0(val)	    vBIT(val, 0, 32)
#define	VXGE_HAL_DEBUG_STATS1_RSTDROP_CLIENT1(val)	    vBIT(val, 32, 32)
/* 0x06540 */	u64	debug_stats2;
#define	VXGE_HAL_DEBUG_STATS2_RSTDROP_CLIENT2(val)	    vBIT(val, 0, 32)
/* 0x06548 */	u64	debug_stats3_vplane[17];
#define	VXGE_HAL_DEBUG_STATS3_VPLANE_DEPL_PH(val)	    vBIT(val, 0, 16)
#define	VXGE_HAL_DEBUG_STATS3_VPLANE_DEPL_NPH(val)	    vBIT(val, 16, 16)
#define	VXGE_HAL_DEBUG_STATS3_VPLANE_DEPL_CPLH(val)	    vBIT(val, 32, 16)
/* 0x065d0 */	u64	debug_stats4_vplane[17];
#define	VXGE_HAL_DEBUG_STATS4_VPLANE_DEPL_PD(val)	    vBIT(val, 0, 16)
#define	VXGE_HAL_DEBUG_STATS4_VPLANE_DEPL_NPD(val)	    vBIT(val, 16, 16)
#define	VXGE_HAL_DEBUG_STATS4_VPLANE_DEPL_CPLD(val)	    vBIT(val, 32, 16)
	u8	unused07000[0x07000-0x06658];

/* 0x07000 */	u64	mrpcim_general_int_status;
#define	VXGE_HAL_MRPCIM_GENERAL_INT_STATUS_PIC_INT	    mBIT(0)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_STATUS_PCI_INT	    mBIT(1)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_STATUS_RTDMA_INT	    mBIT(2)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_STATUS_WRDMA_INT	    mBIT(3)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_STATUS_G3CMCT_INT	    mBIT(4)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_STATUS_GCMG1_INT	    mBIT(5)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_STATUS_GCMG2_INT	    mBIT(6)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_STATUS_GCMG3_INT	    mBIT(7)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_STATUS_G3CMIFL_INT	    mBIT(8)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_STATUS_G3CMIFU_INT	    mBIT(9)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_STATUS_PCMG1_INT	    mBIT(10)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_STATUS_PCMG2_INT	    mBIT(11)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_STATUS_PCMG3_INT	    mBIT(12)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_STATUS_XMAC_INT	    mBIT(13)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_STATUS_RXMAC_INT	    mBIT(14)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_STATUS_TMAC_INT	    mBIT(15)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_STATUS_G3FBIF_INT	    mBIT(16)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_STATUS_FBMC_INT	    mBIT(17)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_STATUS_G3FBCT_INT	    mBIT(18)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_STATUS_TPA_INT	    mBIT(19)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_STATUS_DRBELL_INT	    mBIT(20)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_STATUS_ONE_INT	    mBIT(21)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_STATUS_MSG_INT	    mBIT(22)
/* 0x07008 */	u64	mrpcim_general_int_mask;
#define	VXGE_HAL_MRPCIM_GENERAL_INT_MASK_PIC_INT	    mBIT(0)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_MASK_PCI_INT	    mBIT(1)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_MASK_RTDMA_INT	    mBIT(2)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_MASK_WRDMA_INT	    mBIT(3)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_MASK_G3CMCT_INT	    mBIT(4)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_MASK_GCMG1_INT	    mBIT(5)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_MASK_GCMG2_INT	    mBIT(6)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_MASK_GCMG3_INT	    mBIT(7)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_MASK_G3CMIFL_INT	    mBIT(8)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_MASK_G3CMIFU_INT	    mBIT(9)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_MASK_PCMG1_INT	    mBIT(10)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_MASK_PCMG2_INT	    mBIT(11)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_MASK_PCMG3_INT	    mBIT(12)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_MASK_XMAC_INT	    mBIT(13)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_MASK_RXMAC_INT	    mBIT(14)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_MASK_TMAC_INT	    mBIT(15)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_MASK_G3FBIF_INT	    mBIT(16)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_MASK_FBMC_INT	    mBIT(17)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_MASK_G3FBCT_INT	    mBIT(18)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_MASK_TPA_INT	    mBIT(19)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_MASK_DRBELL_INT	    mBIT(20)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_MASK_ONE_INT	    mBIT(21)
#define	VXGE_HAL_MRPCIM_GENERAL_INT_MASK_MSG_INT	    mBIT(22)
/* 0x07010 */	u64	mrpcim_ppif_int_status;
#define	VXGE_HAL_MRPCIM_PPIF_INT_STATUS_INI_ERRORS_INI_INT  mBIT(3)
#define	VXGE_HAL_MRPCIM_PPIF_INT_STATUS_DMA_ERRORS_DMA_INT  mBIT(7)
#define	VXGE_HAL_MRPCIM_PPIF_INT_STATUS_TGT_ERRORS_TGT_INT  mBIT(11)
#define	VXGE_HAL_MRPCIM_PPIF_INT_STATUS_CONFIG_ERRORS_CONFIG_INT mBIT(15)
#define	VXGE_HAL_MRPCIM_PPIF_INT_STATUS_CRDT_ERRORS_CRDT_INT mBIT(19)
#define	VXGE_HAL_MRPCIM_PPIF_INT_STATUS_MRPCIM_GENERAL_ERRORS_GENERAL_INT\
							    mBIT(23)
#define	VXGE_HAL_MRPCIM_PPIF_INT_STATUS_PLL_ERRORS_PLL_INT  mBIT(27)
#define	VXGE_HAL_MRPCIM_PPIF_INT_STATUS_CRDT_ERRORS_VPLANE0_CRD_INT_VPLANE0_INT\
							    mBIT(31)
#define	VXGE_HAL_MRPCIM_PPIF_INT_STATUS_CRDT_ERRORS_VPLANE1_CRD_INT_VPLANE1_INT\
							    mBIT(32)
#define	VXGE_HAL_MRPCIM_PPIF_INT_STATUS_CRDT_ERRORS_VPLANE2_CRD_INT_VPLANE2_INT\
							    mBIT(33)
#define	VXGE_HAL_MRPCIM_PPIF_INT_STATUS_CRDT_ERRORS_VPLANE3_CRD_INT_VPLANE3_INT\
							    mBIT(34)
#define	VXGE_HAL_MRPCIM_PPIF_INT_STATUS_CRDT_ERRORS_VPLANE4_CRD_INT_VPLANE4_INT\
							    mBIT(35)
#define	VXGE_HAL_MRPCIM_PPIF_INT_STATUS_CRDT_ERRORS_VPLANE5_CRD_INT_VPLANE5_INT\
							    mBIT(36)
#define	VXGE_HAL_MRPCIM_PPIF_INT_STATUS_CRDT_ERRORS_VPLANE6_CRD_INT_VPLANE6_INT\
							    mBIT(37)
#define	VXGE_HAL_MRPCIM_PPIF_INT_STATUS_CRDT_ERRORS_VPLANE7_CRD_INT_VPLANE7_INT\
							    mBIT(38)
#define	VXGE_HAL_MRPCIM_PPIF_INT_STATUS_CRDT_ERRORS_VPLANE8_CRD_INT_VPLANE8_INT\
							    mBIT(39)
#define	VXGE_HAL_MRPCIM_PPIF_INT_STATUS_CRDT_ERRORS_VPLANE9_CRD_INT_VPLANE9_INT\
							    mBIT(40)
#define	\
    VXGE_HAL_MRPCIM_PPIF_INT_STATUS_CRDT_ERRORS_VPLANE10_CRD_INT_VPLANE10_INT\
							    mBIT(41)
#define	\
    VXGE_HAL_MRPCIM_PPIF_INT_STATUS_CRDT_ERRORS_VPLANE11_CRD_INT_VPLANE11_INT\
							    mBIT(42)
#define	\
    VXGE_HAL_MRPCIM_PPIF_INT_STATUS_CRDT_ERRORS_VPLANE12_CRD_INT_VPLANE12_INT\
							    mBIT(43)
#define	\
    VXGE_HAL_MRPCIM_PPIF_INT_STATUS_CRDT_ERRORS_VPLANE13_CRD_INT_VPLANE13_INT\
							    mBIT(44)
#define	\
    VXGE_HAL_MRPCIM_PPIF_INT_STATUS_CRDT_ERRORS_VPLANE14_CRD_INT_VPLANE14_INT\
							    mBIT(45)
#define	\
    VXGE_HAL_MRPCIM_PPIF_INT_STATUS_CRDT_ERRORS_VPLANE15_CRD_INT_VPLANE15_INT\
							    mBIT(46)
#define	\
    VXGE_HAL_MRPCIM_PPIF_INT_STATUS_CRDT_ERRORS_VPLANE16_CRD_INT_VPLANE16_INT\
							    mBIT(47)
#define	VXGE_HAL_MRPCIM_PPIF_INT_STATUS_SRPCIM_TO_MRPCIM_ALARM_INT  mBIT(51)
#define	VXGE_HAL_MRPCIM_PPIF_INT_STATUS_VPATH_TO_MRPCIM_ALARM_INT   mBIT(55)
/* 0x07018 */	u64	mrpcim_ppif_int_mask;
	u8	unused07028[0x07028-0x07020];

/* 0x07028 */	u64	ini_errors_reg;
#define	VXGE_HAL_INI_ERRORS_REG_SCPL_CPL_TIMEOUT_UNUSED_TAG mBIT(3)
#define	VXGE_HAL_INI_ERRORS_REG_SCPL_CPL_TIMEOUT	    mBIT(7)
#define	VXGE_HAL_INI_ERRORS_REG_DCPL_FSM_ERR		    mBIT(11)
#define	VXGE_HAL_INI_ERRORS_REG_DCPL_POISON		    mBIT(12)
#define	VXGE_HAL_INI_ERRORS_REG_DCPL_UNSUPPORTED	    mBIT(15)
#define	VXGE_HAL_INI_ERRORS_REG_DCPL_ABORT		    mBIT(19)
#define	VXGE_HAL_INI_ERRORS_REG_INI_TLP_ABORT		    mBIT(23)
#define	VXGE_HAL_INI_ERRORS_REG_INI_DLLP_ABORT		    mBIT(27)
#define	VXGE_HAL_INI_ERRORS_REG_INI_ECRC_ERR		    mBIT(31)
#define	VXGE_HAL_INI_ERRORS_REG_INI_BUF_DB_ERR		    mBIT(35)
#define	VXGE_HAL_INI_ERRORS_REG_INI_BUF_SG_ERR		    mBIT(39)
#define	VXGE_HAL_INI_ERRORS_REG_INI_DATA_OVERFLOW	    mBIT(43)
#define	VXGE_HAL_INI_ERRORS_REG_INI_HDR_OVERFLOW	    mBIT(47)
#define	VXGE_HAL_INI_ERRORS_REG_INI_MRD_SYS_DROP	    mBIT(51)
#define	VXGE_HAL_INI_ERRORS_REG_INI_MWR_SYS_DROP	    mBIT(55)
#define	VXGE_HAL_INI_ERRORS_REG_INI_MRD_CLIENT_DROP	    mBIT(59)
#define	VXGE_HAL_INI_ERRORS_REG_INI_MWR_CLIENT_DROP	    mBIT(63)
/* 0x07030 */	u64	ini_errors_mask;
/* 0x07038 */	u64	ini_errors_alarm;
/* 0x07040 */	u64	dma_errors_reg;
#define	VXGE_HAL_DMA_ERRORS_REG_RDARB_FSM_ERR		    mBIT(3)
#define	VXGE_HAL_DMA_ERRORS_REG_WRARB_FSM_ERR		    mBIT(7)
#define	VXGE_HAL_DMA_ERRORS_REG_DMA_WRDMA_WR_HDR_OVERFLOW   mBIT(8)
#define	VXGE_HAL_DMA_ERRORS_REG_DMA_WRDMA_WR_HDR_UNDERFLOW  mBIT(9)
#define	VXGE_HAL_DMA_ERRORS_REG_DMA_WRDMA_WR_DATA_OVERFLOW  mBIT(10)
#define	VXGE_HAL_DMA_ERRORS_REG_DMA_WRDMA_WR_DATA_UNDERFLOW mBIT(11)
#define	VXGE_HAL_DMA_ERRORS_REG_DMA_MSG_WR_HDR_OVERFLOW	    mBIT(12)
#define	VXGE_HAL_DMA_ERRORS_REG_DMA_MSG_WR_HDR_UNDERFLOW    mBIT(13)
#define	VXGE_HAL_DMA_ERRORS_REG_DMA_MSG_WR_DATA_OVERFLOW    mBIT(14)
#define	VXGE_HAL_DMA_ERRORS_REG_DMA_MSG_WR_DATA_UNDERFLOW   mBIT(15)
#define	VXGE_HAL_DMA_ERRORS_REG_DMA_STATS_WR_HDR_OVERFLOW   mBIT(16)
#define	VXGE_HAL_DMA_ERRORS_REG_DMA_STATS_WR_HDR_UNDERFLOW  mBIT(17)
#define	VXGE_HAL_DMA_ERRORS_REG_DMA_STATS_WR_DATA_OVERFLOW  mBIT(18)
#define	VXGE_HAL_DMA_ERRORS_REG_DMA_STATS_WR_DATA_UNDERFLOW mBIT(19)
#define	VXGE_HAL_DMA_ERRORS_REG_DMA_RTDMA_WR_HDR_OVERFLOW   mBIT(20)
#define	VXGE_HAL_DMA_ERRORS_REG_DMA_RTDMA_WR_HDR_UNDERFLOW  mBIT(21)
#define	VXGE_HAL_DMA_ERRORS_REG_DMA_RTDMA_WR_DATA_OVERFLOW  mBIT(22)
#define	VXGE_HAL_DMA_ERRORS_REG_DMA_RTDMA_WR_DATA_UNDERFLOW mBIT(23)
#define	VXGE_HAL_DMA_ERRORS_REG_DMA_WRDMA_RD_HDR_OVERFLOW   mBIT(24)
#define	VXGE_HAL_DMA_ERRORS_REG_DMA_WRDMA_RD_HDR_UNDERFLOW  mBIT(25)
#define	VXGE_HAL_DMA_ERRORS_REG_DMA_RTDMA_RD_HDR_OVERFLOW   mBIT(28)
#define	VXGE_HAL_DMA_ERRORS_REG_DMA_RTDMA_RD_HDR_UNDERFLOW  mBIT(29)
#define	VXGE_HAL_DMA_ERRORS_REG_DBLGEN_FSM_ERR		    mBIT(32)
#define	VXGE_HAL_DMA_ERRORS_REG_DBLGEN_CREDIT_FSM_ERR	    mBIT(33)
#define	VXGE_HAL_DMA_ERRORS_REG_DBLGEN_DMA_WRR_SM_ERR	    mBIT(34)
/* 0x07048 */	u64	dma_errors_mask;
/* 0x07050 */	u64	dma_errors_alarm;
/* 0x07058 */	u64	tgt_errors_reg;
#define	VXGE_HAL_TGT_ERRORS_REG_TGT_VENDOR_MSG		    mBIT(0)
#define	VXGE_HAL_TGT_ERRORS_REG_TGT_MSG_UNLOCK		    mBIT(1)
#define	VXGE_HAL_TGT_ERRORS_REG_TGT_ILLEGAL_TLP_BE	    mBIT(2)
#define	VXGE_HAL_TGT_ERRORS_REG_TGT_BOOT_WRITE		    mBIT(3)
#define	VXGE_HAL_TGT_ERRORS_REG_TGT_PIF_WR_CROSS_QWRANGE    mBIT(4)
#define	VXGE_HAL_TGT_ERRORS_REG_TGT_PIF_READ_CROSS_QWRANGE  mBIT(5)
#define	VXGE_HAL_TGT_ERRORS_REG_TGT_KDFC_READ		    mBIT(6)
#define	VXGE_HAL_TGT_ERRORS_REG_TGT_USDC_READ		    mBIT(7)
#define	VXGE_HAL_TGT_ERRORS_REG_TGT_USDC_WR_CROSS_QWRANGE   mBIT(8)
#define	VXGE_HAL_TGT_ERRORS_REG_TGT_MSIX_BEYOND_RANGE	    mBIT(9)
#define	VXGE_HAL_TGT_ERRORS_REG_TGT_WR_TO_KDFC_POISON	    mBIT(10)
#define	VXGE_HAL_TGT_ERRORS_REG_TGT_WR_TO_USDC_POISON	    mBIT(11)
#define	VXGE_HAL_TGT_ERRORS_REG_TGT_WR_TO_PIF_POISON	    mBIT(12)
#define	VXGE_HAL_TGT_ERRORS_REG_TGT_WR_TO_MSIX_POISON	    mBIT(13)
#define	VXGE_HAL_TGT_ERRORS_REG_TGT_WR_TO_MRIOV_POISON	    mBIT(14)
#define	VXGE_HAL_TGT_ERRORS_REG_TGT_NOT_MEM_TLP		    mBIT(15)
#define	VXGE_HAL_TGT_ERRORS_REG_TGT_UNKNOWN_MEM_TLP	    mBIT(16)
#define	VXGE_HAL_TGT_ERRORS_REG_TGT_REQ_FSM_ERR		    mBIT(17)
#define	VXGE_HAL_TGT_ERRORS_REG_TGT_CPL_FSM_ERR		    mBIT(18)
#define	VXGE_HAL_TGT_ERRORS_REG_TGT_KDFC_PROT_ERR	    mBIT(19)
#define	VXGE_HAL_TGT_ERRORS_REG_TGT_SWIF_PROT_ERR	    mBIT(20)
#define	VXGE_HAL_TGT_ERRORS_REG_TGT_MRIOV_MEM_MAP_CFG_ERR   mBIT(21)
/* 0x07060 */	u64	tgt_errors_mask;
/* 0x07068 */	u64	tgt_errors_alarm;
/* 0x07070 */	u64	config_errors_reg;
#define	VXGE_HAL_CONFIG_ERRORS_REG_I2C_ILLEGAL_STOP_COND    mBIT(3)
#define	VXGE_HAL_CONFIG_ERRORS_REG_I2C_ILLEGAL_START_COND   mBIT(7)
#define	VXGE_HAL_CONFIG_ERRORS_REG_I2C_EXP_RD_CNT	    mBIT(11)
#define	VXGE_HAL_CONFIG_ERRORS_REG_I2C_EXTRA_CYCLE	    mBIT(15)
#define	VXGE_HAL_CONFIG_ERRORS_REG_I2C_MAIN_FSM_ERR	    mBIT(19)
#define	VXGE_HAL_CONFIG_ERRORS_REG_I2C_REQ_COLLISION	    mBIT(23)
#define	VXGE_HAL_CONFIG_ERRORS_REG_I2C_REG_FSM_ERR	    mBIT(27)
#define	VXGE_HAL_CONFIG_ERRORS_REG_CFGM_I2C_TIMEOUT	    mBIT(31)
#define	VXGE_HAL_CONFIG_ERRORS_REG_RIC_I2C_TIMEOUT	    mBIT(35)
#define	VXGE_HAL_CONFIG_ERRORS_REG_CFGM_FSM_ERR		    mBIT(39)
#define	VXGE_HAL_CONFIG_ERRORS_REG_RIC_FSM_ERR		    mBIT(43)
#define	VXGE_HAL_CONFIG_ERRORS_REG_PIFM_ILLEGAL_ACCESS	    mBIT(47)
#define	VXGE_HAL_CONFIG_ERRORS_REG_PIFM_TIMEOUT		    mBIT(51)
#define	VXGE_HAL_CONFIG_ERRORS_REG_PIFM_FSM_ERR		    mBIT(55)
#define	VXGE_HAL_CONFIG_ERRORS_REG_PIFM_TO_FSM_ERR	    mBIT(59)
#define	VXGE_HAL_CONFIG_ERRORS_REG_RIC_RIC_RD_TIMEOUT	    mBIT(63)
/* 0x07078 */	u64	config_errors_mask;
/* 0x07080 */	u64	config_errors_alarm;
	u8	unused07090[0x07090-0x07088];

/* 0x07090 */	u64	crdt_errors_reg;
#define	VXGE_HAL_CRDT_ERRORS_REG_WRCRDTARB_FSM_ERR	    mBIT(11)
#define	VXGE_HAL_CRDT_ERRORS_REG_WRCRDTARB_INTCTL_ILLEGAL_CRD_DEAL mBIT(15)
#define	VXGE_HAL_CRDT_ERRORS_REG_WRCRDTARB_PDA_ILLEGAL_CRD_DEAL	mBIT(19)
#define	VXGE_HAL_CRDT_ERRORS_REG_WRCRDTARB_PCI_MSG_ILLEGAL_CRD_DEAL mBIT(23)
#define	VXGE_HAL_CRDT_ERRORS_REG_RDCRDTARB_FSM_ERR	    mBIT(35)
#define	VXGE_HAL_CRDT_ERRORS_REG_RDCRDTARB_RDA_ILLEGAL_CRD_DEAL	mBIT(39)
#define	VXGE_HAL_CRDT_ERRORS_REG_RDCRDTARB_PDA_ILLEGAL_CRD_DEAL	mBIT(43)
#define	VXGE_HAL_CRDT_ERRORS_REG_RDCRDTARB_DBLGEN_ILLEGAL_CRD_DEAL mBIT(47)
/* 0x07098 */	u64	crdt_errors_mask;
/* 0x070a0 */	u64	crdt_errors_alarm;
	u8	unused070b0[0x070b0-0x070a8];

/* 0x070b0 */	u64	mrpcim_general_errors_reg;
#define	VXGE_HAL_MRPCIM_GENERAL_ERRORS_REG_STATSB_FSM_ERR   mBIT(3)
#define	VXGE_HAL_MRPCIM_GENERAL_ERRORS_REG_XGEN_FSM_ERR	    mBIT(7)
#define	VXGE_HAL_MRPCIM_GENERAL_ERRORS_REG_XMEM_FSM_ERR	    mBIT(11)
#define	VXGE_HAL_MRPCIM_GENERAL_ERRORS_REG_KDFCCTL_FSM_ERR  mBIT(15)
#define	VXGE_HAL_MRPCIM_GENERAL_ERRORS_REG_MRIOVCTL_FSM_ERR mBIT(19)
#define	VXGE_HAL_MRPCIM_GENERAL_ERRORS_REG_SPI_FLSH_ERR	    mBIT(23)
#define	VXGE_HAL_MRPCIM_GENERAL_ERRORS_REG_SPI_IIC_ACK_ERR  mBIT(27)
#define	VXGE_HAL_MRPCIM_GENERAL_ERRORS_REG_SPI_IIC_CHKSUM_ERR mBIT(31)
#define	VXGE_HAL_MRPCIM_GENERAL_ERRORS_REG_INI_SERR_DET	    mBIT(35)
#define	VXGE_HAL_MRPCIM_GENERAL_ERRORS_REG_INTCTL_MSIX_FSM_ERR mBIT(39)
#define	VXGE_HAL_MRPCIM_GENERAL_ERRORS_REG_INTCTL_MSI_OVERFLOW mBIT(43)
#define	VXGE_HAL_MRPCIM_GENERAL_ERRORS_REG_PPIF_PCI_NOT_FLUSH_SW_RESET\
							    mBIT(47)
#define	VXGE_HAL_MRPCIM_GENERAL_ERRORS_REG_PPIF_SW_RESET_FSM_ERR mBIT(51)
/* 0x070b8 */	u64	mrpcim_general_errors_mask;
/* 0x070c0 */	u64	mrpcim_general_errors_alarm;
	u8	unused070d0[0x070d0-0x070c8];

/* 0x070d0 */	u64	pll_errors_reg;
#define	VXGE_HAL_PLL_ERRORS_REG_CORE_CMG_PLL_OOL	    mBIT(3)
#define	VXGE_HAL_PLL_ERRORS_REG_CORE_FB_PLL_OOL		    mBIT(7)
#define	VXGE_HAL_PLL_ERRORS_REG_CORE_X_PLL_OOL		    mBIT(11)
/* 0x070d8 */	u64	pll_errors_mask;
/* 0x070e0 */	u64	pll_errors_alarm;
/* 0x070e8 */	u64	srpcim_to_mrpcim_alarm_reg;
#define	VXGE_HAL_SRPCIM_TO_MRPCIM_ALARM_REG_ALARM(val)	    vBIT(val, 0, 17)
/* 0x070f0 */	u64	srpcim_to_mrpcim_alarm_mask;
/* 0x070f8 */	u64	srpcim_to_mrpcim_alarm_alarm;
/* 0x07100 */	u64	vpath_to_mrpcim_alarm_reg;
#define	VXGE_HAL_VPATH_TO_MRPCIM_ALARM_REG_ALARM(val)	    vBIT(val, 0, 17)
/* 0x07108 */	u64	vpath_to_mrpcim_alarm_mask;
/* 0x07110 */	u64	vpath_to_mrpcim_alarm_alarm;
	u8	unused07128[0x07128-0x07118];

/* 0x07128 */	u64	crdt_errors_vplane_reg[17];
#define	VXGE_HAL_CRDT_ERRORS_VPLANE_REG_WRCRDTARB_P_H_CONSUME_CRDT_ERR	mBIT(3)
#define	VXGE_HAL_CRDT_ERRORS_VPLANE_REG_WRCRDTARB_P_D_CONSUME_CRDT_ERR	mBIT(7)
#define	VXGE_HAL_CRDT_ERRORS_VPLANE_REG_WRCRDTARB_P_H_RETURN_CRDT_ERR	mBIT(11)
#define	VXGE_HAL_CRDT_ERRORS_VPLANE_REG_WRCRDTARB_P_D_RETURN_CRDT_ERR	mBIT(15)
#define	VXGE_HAL_CRDT_ERRORS_VPLANE_REG_RDCRDTARB_NP_H_CONSUME_CRDT_ERR	mBIT(19)
#define	VXGE_HAL_CRDT_ERRORS_VPLANE_REG_RDCRDTARB_NP_H_RETURN_CRDT_ERR	mBIT(23)
#define	VXGE_HAL_CRDT_ERRORS_VPLANE_REG_RDCRDTARB_TAG_CONSUME_TAG_ERR	mBIT(27)
#define	VXGE_HAL_CRDT_ERRORS_VPLANE_REG_RDCRDTARB_TAG_RETURN_TAG_ERR	mBIT(31)
/* 0x07130 */	u64	crdt_errors_vplane_mask[17];
/* 0x07138 */	u64	crdt_errors_vplane_alarm[17];
	u8	unused072f0[0x072f0-0x072c0];

/* 0x072f0 */	u64	mrpcim_rst_in_prog;
#define	VXGE_HAL_MRPCIM_RST_IN_PROG_MRPCIM_RST_IN_PROG	    mBIT(7)
/* 0x072f8 */	u64	mrpcim_reg_modified;
#define	VXGE_HAL_MRPCIM_REG_MODIFIED_MRPCIM_REG_MODIFIED    mBIT(7)
	u8	unused07378[0x07378-0x07300];

/* 0x07378 */	u64	write_arb_pending;
#define	VXGE_HAL_WRITE_ARB_PENDING_WRARB_WRDMA		    mBIT(3)
#define	VXGE_HAL_WRITE_ARB_PENDING_WRARB_RTDMA		    mBIT(7)
#define	VXGE_HAL_WRITE_ARB_PENDING_WRARB_MSG		    mBIT(11)
#define	VXGE_HAL_WRITE_ARB_PENDING_WRARB_STATSB		    mBIT(15)
#define	VXGE_HAL_WRITE_ARB_PENDING_WRARB_INTCTL		    mBIT(19)
/* 0x07380 */	u64	read_arb_pending;
#define	VXGE_HAL_READ_ARB_PENDING_RDARB_WRDMA		    mBIT(3)
#define	VXGE_HAL_READ_ARB_PENDING_RDARB_RTDMA		    mBIT(7)
#define	VXGE_HAL_READ_ARB_PENDING_RDARB_DBLGEN		    mBIT(11)
/* 0x07388 */	u64	dmaif_dmadbl_pending;
#define	VXGE_HAL_DMAIF_DMADBL_PENDING_DMAIF_WRDMA_WR	    mBIT(0)
#define	VXGE_HAL_DMAIF_DMADBL_PENDING_DMAIF_WRDMA_RD	    mBIT(1)
#define	VXGE_HAL_DMAIF_DMADBL_PENDING_DMAIF_RTDMA_WR	    mBIT(2)
#define	VXGE_HAL_DMAIF_DMADBL_PENDING_DMAIF_RTDMA_RD	    mBIT(3)
#define	VXGE_HAL_DMAIF_DMADBL_PENDING_DMAIF_MSG_WR	    mBIT(4)
#define	VXGE_HAL_DMAIF_DMADBL_PENDING_DMAIF_STATS_WR	    mBIT(5)
#define	VXGE_HAL_DMAIF_DMADBL_PENDING_DBLGEN_IN_PROG(val)   vBIT(val, 13, 51)
/* 0x07390 */	u64	wrcrdtarb_status0_vplane[17];
#define	VXGE_HAL_WRCRDTARB_STATUS0_VPLANE_WRCRDTARB_ABS_AVAIL_P_H(val)\
							    vBIT(val, 0, 8)
/* 0x07418 */	u64	wrcrdtarb_status1_vplane[17];
#define	VXGE_HAL_WRCRDTARB_STATUS1_VPLANE_WRCRDTARB_ABS_AVAIL_P_D(val)\
							    vBIT(val, 4, 12)
	u8	unused07500[0x07500-0x074a0];

/* 0x07500 */	u64	mrpcim_general_cfg1;
#define	VXGE_HAL_MRPCIM_GENERAL_CFG1_CLEAR_SERR		    mBIT(7)
/* 0x07508 */	u64	mrpcim_general_cfg2;
#define	VXGE_HAL_MRPCIM_GENERAL_CFG2_INS_TX_WR_TD	    mBIT(3)
#define	VXGE_HAL_MRPCIM_GENERAL_CFG2_INS_TX_RD_TD	    mBIT(7)
#define	VXGE_HAL_MRPCIM_GENERAL_CFG2_INS_TX_CPL_TD	    mBIT(11)
#define	VXGE_HAL_MRPCIM_GENERAL_CFG2_INI_TIMEOUT_EN_MWR	    mBIT(15)
#define	VXGE_HAL_MRPCIM_GENERAL_CFG2_INI_TIMEOUT_EN_MRD	    mBIT(19)
#define	VXGE_HAL_MRPCIM_GENERAL_CFG2_IGNORE_VPATH_RST_FOR_MSIX mBIT(23)
#define	VXGE_HAL_MRPCIM_GENERAL_CFG2_FLASH_READ_MSB	    mBIT(27)
#define	VXGE_HAL_MRPCIM_GENERAL_CFG2_DIS_HOST_PIPELINE_WR   mBIT(31)
#define	VXGE_HAL_MRPCIM_GENERAL_CFG2_MRPCIM_STATS_ENABLE    mBIT(43)
#define	VXGE_HAL_MRPCIM_GENERAL_CFG2_MRPCIM_STATS_MAP_TO_VPATH(val)\
							    vBIT(val, 47, 5)
#define	VXGE_HAL_MRPCIM_GENERAL_CFG2_EN_BLOCK_MSIX_DUE_TO_SERR mBIT(55)
#define	VXGE_HAL_MRPCIM_GENERAL_CFG2_FORCE_SENDING_INTA	    mBIT(59)
#define	VXGE_HAL_MRPCIM_GENERAL_CFG2_DIS_SWIF_PROT_ON_RDS   mBIT(63)
/* 0x07510 */	u64	mrpcim_general_cfg3;
#define	VXGE_HAL_MRPCIM_GENERAL_CFG3_PROTECTION_CA_OR_UNSUPN mBIT(0)
#define	VXGE_HAL_MRPCIM_GENERAL_CFG3_ILLEGAL_RD_CA_OR_UNSUPN mBIT(3)
#define	VXGE_HAL_MRPCIM_GENERAL_CFG3_RD_BYTE_SWAPEN	    mBIT(7)
#define	VXGE_HAL_MRPCIM_GENERAL_CFG3_RD_BIT_FLIPEN	    mBIT(11)
#define	VXGE_HAL_MRPCIM_GENERAL_CFG3_WR_BYTE_SWAPEN	    mBIT(15)
#define	VXGE_HAL_MRPCIM_GENERAL_CFG3_WR_BIT_FLIPEN	    mBIT(19)
#define	VXGE_HAL_MRPCIM_GENERAL_CFG3_MR_MAX_MVFS(val)	    vBIT(val, 20, 16)
#define	VXGE_HAL_MRPCIM_GENERAL_CFG3_MR_MVF_TBL_SIZE(val)   vBIT(val, 36, 16)
#define	VXGE_HAL_MRPCIM_GENERAL_CFG3_PF0_SW_RESET_EN	    mBIT(55)
#define	VXGE_HAL_MRPCIM_GENERAL_CFG3_REG_MODIFIED_CFG(val)  vBIT(val, 56, 2)
#define	VXGE_HAL_MRPCIM_GENERAL_CFG3_CPL_ECC_ENABLE_N	    mBIT(59)
#define	VXGE_HAL_MRPCIM_GENERAL_CFG3_BYPASS_DAISY_CHAIN	    mBIT(63)
/* 0x07518 */	u64	mrpcim_stats_start_host_addr;
#define	VXGE_HAL_MRPCIM_STATS_START_HOST_ADDR_MRPCIM_STATS_START_HOST_ADDR(val)\
							    vBIT(val, 0, 57)
	u8	unused07950[0x07950-0x07520];

/* 0x07950 */	u64	rdcrdtarb_cfg0;
#define	VXGE_HAL_RDCRDTARB_CFG0_RDA_MAX_OUTSTANDING_RDS(val) vBIT(val, 18, 6)
#define	VXGE_HAL_RDCRDTARB_CFG0_PDA_MAX_OUTSTANDING_RDS(val) vBIT(val, 26, 6)
#define	VXGE_HAL_RDCRDTARB_CFG0_DBLGEN_MAX_OUTSTANDING_RDS(val) vBIT(val, 34, 6)
#define	VXGE_HAL_RDCRDTARB_CFG0_WAIT_CNT(val)		    vBIT(val, 48, 4)
#define	VXGE_HAL_RDCRDTARB_CFG0_MAX_OUTSTANDING_RDS(val)    vBIT(val, 54, 6)
#define	VXGE_HAL_RDCRDTARB_CFG0_EN_XON			    mBIT(63)

	u8	unused07be8[0x07be8-0x07958];

/* 0x07be8 */	u64	bf_sw_reset;
#define	VXGE_HAL_BF_SW_RESET_BF_SW_RESET(val)		    vBIT(val, 0, 8)
/* 0x07bf0 */	u64	sw_reset_status;
#define	VXGE_HAL_SW_RESET_STATUS_RESET_CMPLT		    mBIT(7)
#define	VXGE_HAL_SW_RESET_STATUS_INIT_CMPLT		    mBIT(15)
	u8	unused07c20[0x07c20-0x07bf8];

/* 0x07c20 */	u64	sw_reset_cfg1;
#define	VXGE_HAL_SW_RESET_CFG1_TYPE			    mBIT(0)
#define	VXGE_HAL_SW_RESET_CFG1_WAIT_TIME_FOR_FLUSH_PCI(val) vBIT(val, 7, 25)
#define	VXGE_HAL_SW_RESET_CFG1_SOPR_ASSERT_TIME(val)	    vBIT(val, 32, 4)
#define	VXGE_HAL_SW_RESET_CFG1_WAIT_TIME_AFTER_RESET(val)   vBIT(val, 38, 25)
	u8	unused07d30[0x07d30-0x07c28];

/* 0x07d30 */	u64	mrpcim_debug_stats0;
#define	VXGE_HAL_MRPCIM_DEBUG_STATS0_INI_WR_DROP(val)	    vBIT(val, 0, 32)
#define	VXGE_HAL_MRPCIM_DEBUG_STATS0_INI_RD_DROP(val)	    vBIT(val, 32, 32)
/* 0x07d38 */	u64	mrpcim_debug_stats1_vplane[17];
#define	VXGE_HAL_MRPCIM_DEBUG_STATS1_VPLANE_WRCRDTARB_PH_CRDT_DEPLETED(val)\
							    vBIT(val, 32, 32)
/* 0x07dc0 */	u64	mrpcim_debug_stats2_vplane[17];
#define	VXGE_HAL_MRPCIM_DEBUG_STATS2_VPLANE_WRCRDTARB_PD_CRDT_DEPLETED(val)\
							    vBIT(val, 32, 32)
/* 0x07e48 */	u64	mrpcim_debug_stats3_vplane[17];
#define	VXGE_HAL_MRPCIM_DEBUG_STATS3_VPLANE_RDCRDTARB_NPH_CRDT_DEPLETED(val)\
							    vBIT(val, 32, 32)
/* 0x07ed0 */	u64	mrpcim_debug_stats4;
#define	VXGE_HAL_MRPCIM_DEBUG_STATS4_INI_WR_VPIN_DROP(val)  vBIT(val, 0, 32)
#define	VXGE_HAL_MRPCIM_DEBUG_STATS4_INI_RD_VPIN_DROP(val)  vBIT(val, 32, 32)
/* 0x07ed8 */	u64	genstats_count01;
#define	VXGE_HAL_GENSTATS_COUNT01_GENSTATS_COUNT1(val)	    vBIT(val, 0, 32)
#define	VXGE_HAL_GENSTATS_COUNT01_GENSTATS_COUNT0(val)	    vBIT(val, 32, 32)
/* 0x07ee0 */	u64	genstats_count23;
#define	VXGE_HAL_GENSTATS_COUNT23_GENSTATS_COUNT3(val)	    vBIT(val, 0, 32)
#define	VXGE_HAL_GENSTATS_COUNT23_GENSTATS_COUNT2(val)	    vBIT(val, 32, 32)
/* 0x07ee8 */	u64	genstats_count4;
#define	VXGE_HAL_GENSTATS_COUNT4_GENSTATS_COUNT4(val)	    vBIT(val, 32, 32)
/* 0x07ef0 */	u64	genstats_count5;
#define	VXGE_HAL_GENSTATS_COUNT5_GENSTATS_COUNT5(val)	    vBIT(val, 32, 32)
	u8	unused07f08[0x07f08-0x07ef8];

/* 0x07f08 */	u64	genstats_cfg[6];
#define	VXGE_HAL_GENSTATS_CFG_DTYPE_SEL(val)		    vBIT(val, 3, 5)
#define	VXGE_HAL_GENSTATS_CFG_CLIENT_NO_SEL(val)	    vBIT(val, 9, 3)
#define	VXGE_HAL_GENSTATS_CFG_WR_RD_CPL_SEL(val)	    vBIT(val, 14, 2)
#define	VXGE_HAL_GENSTATS_CFG_VPATH_SEL(val)		    vBIT(val, 31, 17)
/* 0x07f38 */	u64	genstat_64bit_cfg;
#define	VXGE_HAL_GENSTAT_64BIT_CFG_EN_FOR_GENSTATS0	    mBIT(3)
#define	VXGE_HAL_GENSTAT_64BIT_CFG_EN_FOR_GENSTATS2	    mBIT(7)
	u8	unused08000[0x08000-0x07f40];

/* 0x08000 */	u64	gcmg3_int_status;
#define	VXGE_HAL_GCMG3_INT_STATUS_GSTC_ERR0_GSTC0_INT	    mBIT(0)
#define	VXGE_HAL_GCMG3_INT_STATUS_GSTC_ERR1_GSTC1_INT	    mBIT(1)
#define	VXGE_HAL_GCMG3_INT_STATUS_GH2L_ERR0_GH2L0_INT	    mBIT(2)
#define	VXGE_HAL_GCMG3_INT_STATUS_GHSQ_ERR_GH2L1_INT	    mBIT(3)
#define	VXGE_HAL_GCMG3_INT_STATUS_GHSQ_ERR2_GH2L2_INT	    mBIT(4)
#define	VXGE_HAL_GCMG3_INT_STATUS_GH2L_SMERR0_GH2L3_INT	    mBIT(5)
#define	VXGE_HAL_GCMG3_INT_STATUS_GHSQ_ERR3_GH2L4_INT	    mBIT(6)
/* 0x08008 */	u64	gcmg3_int_mask;
	u8	unused09000[0x09000-0x08010];

/* 0x09000 */	u64	g3ifcmd_fb_int_status;
#define	VXGE_HAL_G3IFCMD_FB_INT_STATUS_ERR_G3IF_INT	    mBIT(0)
/* 0x09008 */	u64	g3ifcmd_fb_int_mask;
/* 0x09010 */	u64	g3ifcmd_fb_err_reg;
#define	VXGE_HAL_G3IFCMD_FB_ERR_REG_G3IF_CK_DLL_LOCK	    mBIT(6)
#define	VXGE_HAL_G3IFCMD_FB_ERR_REG_G3IF_SM_ERR		    mBIT(7)
#define	VXGE_HAL_G3IFCMD_FB_ERR_REG_G3IF_RWDQS_DLL_LOCK(val) vBIT(val, 24, 8)
#define	VXGE_HAL_G3IFCMD_FB_ERR_REG_G3IF_IOCAL_FAULT	    mBIT(55)
/* 0x09018 */	u64	g3ifcmd_fb_err_mask;
/* 0x09020 */	u64	g3ifcmd_fb_err_alarm;
	u8	unused09400[0x09400-0x09028];

/* 0x09400 */	u64	g3ifcmd_cmu_int_status;
#define	VXGE_HAL_G3IFCMD_CMU_INT_STATUS_ERR_G3IF_INT	    mBIT(0)
/* 0x09408 */	u64	g3ifcmd_cmu_int_mask;
/* 0x09410 */	u64	g3ifcmd_cmu_err_reg;
#define	VXGE_HAL_G3IFCMD_CMU_ERR_REG_G3IF_CK_DLL_LOCK	    mBIT(6)
#define	VXGE_HAL_G3IFCMD_CMU_ERR_REG_G3IF_SM_ERR	    mBIT(7)
#define	VXGE_HAL_G3IFCMD_CMU_ERR_REG_G3IF_RWDQS_DLL_LOCK(val) vBIT(val, 24, 8)
#define	VXGE_HAL_G3IFCMD_CMU_ERR_REG_G3IF_IOCAL_FAULT	    mBIT(55)
/* 0x09418 */	u64	g3ifcmd_cmu_err_mask;
/* 0x09420 */	u64	g3ifcmd_cmu_err_alarm;
	u8	unused09800[0x09800-0x09428];

/* 0x09800 */	u64	g3ifcmd_cml_int_status;
#define	VXGE_HAL_G3IFCMD_CML_INT_STATUS_ERR_G3IF_INT	    mBIT(0)
/* 0x09808 */	u64	g3ifcmd_cml_int_mask;
/* 0x09810 */	u64	g3ifcmd_cml_err_reg;
#define	VXGE_HAL_G3IFCMD_CML_ERR_REG_G3IF_CK_DLL_LOCK	    mBIT(6)
#define	VXGE_HAL_G3IFCMD_CML_ERR_REG_G3IF_SM_ERR	    mBIT(7)
#define	VXGE_HAL_G3IFCMD_CML_ERR_REG_G3IF_RWDQS_DLL_LOCK(val)\
							    vBIT(val, 24, 8)
#define	VXGE_HAL_G3IFCMD_CML_ERR_REG_G3IF_IOCAL_FAULT	    mBIT(55)
/* 0x09818 */	u64	g3ifcmd_cml_err_mask;
/* 0x09820 */	u64	g3ifcmd_cml_err_alarm;
	u8	unused09b00[0x09b00-0x09828];

/* 0x09b00 */	u64	vpath_to_vplane_map[17];
#define	VXGE_HAL_VPATH_TO_VPLANE_MAP_VPATH_TO_VPLANE_MAP(val) vBIT(val, 3, 5)
	u8	unused09c30[0x09c30-0x09b88];

/* 0x09c30 */	u64	xgxs_cfg_port[2];
#define	VXGE_HAL_XGXS_CFG_PORT_SIG_DETECT_FORCE_LOS(val)    vBIT(val, 16, 4)
#define	VXGE_HAL_XGXS_CFG_PORT_SIG_DETECT_FORCE_VALID(val)  vBIT(val, 20, 4)
#define	VXGE_HAL_XGXS_CFG_PORT_SEL_INFO_0		    mBIT(27)
#define	VXGE_HAL_XGXS_CFG_PORT_SEL_INFO_1(val)		    vBIT(val, 29, 3)
#define	VXGE_HAL_XGXS_CFG_PORT_TX_LANE0_SKEW(val)	    vBIT(val, 32, 4)
#define	VXGE_HAL_XGXS_CFG_PORT_TX_LANE1_SKEW(val)	    vBIT(val, 36, 4)
#define	VXGE_HAL_XGXS_CFG_PORT_TX_LANE2_SKEW(val)	    vBIT(val, 40, 4)
#define	VXGE_HAL_XGXS_CFG_PORT_TX_LANE3_SKEW(val)	    vBIT(val, 44, 4)
/* 0x09c40 */	u64	xgxs_rxber_cfg_port[2];
#define	VXGE_HAL_XGXS_RXBER_CFG_PORT_INTERVAL_DUR(val)	    vBIT(val, 0, 4)
#define	VXGE_HAL_XGXS_RXBER_CFG_PORT_RXGXS_INTERVAL_CNT(val) vBIT(val, 16, 48)
/* 0x09c50 */	u64	xgxs_rxber_status_port[2];
#define	VXGE_HAL_XGXS_RXBER_STATUS_PORT_RXGXS_RXGXS_LANE_A_ERR_CNT(val)\
							    vBIT(val, 0, 16)
#define	VXGE_HAL_XGXS_RXBER_STATUS_PORT_RXGXS_RXGXS_LANE_B_ERR_CNT(val)\
							    vBIT(val, 16, 16)
#define	VXGE_HAL_XGXS_RXBER_STATUS_PORT_RXGXS_RXGXS_LANE_C_ERR_CNT(val)\
							    vBIT(val, 32, 16)
#define	VXGE_HAL_XGXS_RXBER_STATUS_PORT_RXGXS_RXGXS_LANE_D_ERR_CNT(val)\
							    vBIT(val, 48, 16)
/* 0x09c60 */	u64	xgxs_status_port[2];
#define	VXGE_HAL_XGXS_STATUS_PORT_XMACJ_PCS_TX_ACTIVITY(val) vBIT(val, 0, 4)
#define	VXGE_HAL_XGXS_STATUS_PORT_XMACJ_PCS_RX_ACTIVITY(val) vBIT(val, 4, 4)
#define	VXGE_HAL_XGXS_STATUS_PORT_XMACJ_PCS_CTC_FIFO_ERR    BIT(11)
#define	VXGE_HAL_XGXS_STATUS_PORT_XMACJ_PCS_BYTE_SYNC_LOST(val) vBIT(val, 12, 4)
#define	VXGE_HAL_XGXS_STATUS_PORT_XMACJ_PCS_CTC_ERR(val)    vBIT(val, 16, 4)
#define	VXGE_HAL_XGXS_STATUS_PORT_XMACJ_PCS_ALIGNMENT_ERR   mBIT(23)
#define	VXGE_HAL_XGXS_STATUS_PORT_XMACJ_PCS_DEC_ERR(val)    vBIT(val, 24, 8)
#define	VXGE_HAL_XGXS_STATUS_PORT_XMACJ_PCS_SKIP_INS_REQ(val) vBIT(val, 32, 4)
#define	VXGE_HAL_XGXS_STATUS_PORT_XMACJ_PCS_SKIP_DEL_REQ(val) vBIT(val, 36, 4)
/* 0x09c70 */	u64	xgxs_pma_reset_port[2];
#define	VXGE_HAL_XGXS_PMA_RESET_PORT_SERDES_RESET(val)	    vBIT(val, 0, 8)
	u8	unused09c90[0x09c90-0x09c80];

/* 0x09c90 */	u64	xgxs_static_cfg_port[2];
#define	VXGE_HAL_XGXS_STATIC_CFG_PORT_FW_CTRL_SERDES	    mBIT(3)
	u8	unused09d40[0x09d40-0x09ca0];

/* 0x09d40 */	u64	xgxs_info_port[2];
#define	VXGE_HAL_XGXS_INFO_PORT_XMACJ_INFO_0(val)	    vBIT(val, 0, 32)
#define	VXGE_HAL_XGXS_INFO_PORT_XMACJ_INFO_1(val)	    vBIT(val, 32, 32)
/* 0x09d50 */	u64	ratemgmt_cfg_port[2];
#define	VXGE_HAL_RATEMGMT_CFG_PORT_MODE(val)		    vBIT(val, 2, 2)
#define	VXGE_HAL_RATEMGMT_CFG_PORT_RATE			    mBIT(7)
#define	VXGE_HAL_RATEMGMT_CFG_PORT_FIXED_USE_FSM	    mBIT(11)
#define	VXGE_HAL_RATEMGMT_CFG_PORT_ANTP_USE_FSM		    mBIT(15)
#define	VXGE_HAL_RATEMGMT_CFG_PORT_ANBE_USE_FSM		    mBIT(19)
/* 0x09d60 */	u64	ratemgmt_status_port[2];
#define	VXGE_HAL_RATEMGMT_STATUS_PORT_RATEMGMT_COMPLETE	    mBIT(3)
#define	VXGE_HAL_RATEMGMT_STATUS_PORT_RATEMGMT_RATE	    mBIT(7)
#define	VXGE_HAL_RATEMGMT_STATUS_PORT_RATEMGMT_MAC_MATCHES_PHY mBIT(11)
	u8	unused09d80[0x09d80-0x09d70];

/* 0x09d80 */	u64	ratemgmt_fixed_cfg_port[2];
#define	VXGE_HAL_RATEMGMT_FIXED_CFG_PORT_RESTART	    mBIT(7)
/* 0x09d90 */	u64	ratemgmt_antp_cfg_port[2];
#define	VXGE_HAL_RATEMGMT_ANTP_CFG_PORT_RESTART		    mBIT(7)
#define	VXGE_HAL_RATEMGMT_ANTP_CFG_PORT_USE_PREAMBLE_EXT_PHY mBIT(11)
#define	VXGE_HAL_RATEMGMT_ANTP_CFG_PORT_USE_ACT_SEL	    mBIT(15)
#define	VXGE_HAL_RATEMGMT_ANTP_CFG_PORT_T_RETRY_PHY_QUERY(val)\
							    vBIT(val, 16, 4)
#define	VXGE_HAL_RATEMGMT_ANTP_CFG_PORT_T_WAIT_MDIO_RESP(val)\
							    vBIT(val, 20, 4)
#define	VXGE_HAL_RATEMGMT_ANTP_CFG_PORT_T_LDOWN_REAUTO_RESP(val)\
							    vBIT(val, 24, 4)
#define	VXGE_HAL_RATEMGMT_ANTP_CFG_PORT_ADVERTISE_10G	    mBIT(31)
#define	VXGE_HAL_RATEMGMT_ANTP_CFG_PORT_ADVERTISE_1G	    mBIT(35)
/* 0x09da0 */	u64	ratemgmt_anbe_cfg_port[2];
#define	VXGE_HAL_RATEMGMT_ANBE_CFG_PORT_RESTART	mBIT(7)
#define	VXGE_HAL_RATEMGMT_ANBE_CFG_PORT_PARALLEL_DETECT_10G_KX4_ENABLE mBIT(11)
#define	VXGE_HAL_RATEMGMT_ANBE_CFG_PORT_PARALLEL_DETECT_1G_KX_ENABLE mBIT(15)
#define	VXGE_HAL_RATEMGMT_ANBE_CFG_PORT_T_SYNC_10G_KX4(val) vBIT(val, 16, 4)
#define	VXGE_HAL_RATEMGMT_ANBE_CFG_PORT_T_SYNC_1G_KX(val)   vBIT(val, 20, 4)
#define	VXGE_HAL_RATEMGMT_ANBE_CFG_PORT_T_DME_EXCHANGE(val) vBIT(val, 24, 4)
#define	VXGE_HAL_RATEMGMT_ANBE_CFG_PORT_ADVERTISE_10G_KX4   mBIT(31)
#define	VXGE_HAL_RATEMGMT_ANBE_CFG_PORT_ADVERTISE_1G_KX	    mBIT(35)
/* 0x09db0 */	u64	anbe_cfg_port[2];
#define	VXGE_HAL_ANBE_CFG_PORT_RESET_CFG_REGS(val)	    vBIT(val, 0, 8)
#define	VXGE_HAL_ANBE_CFG_PORT_ALIGN_10G_KX4_OVERRIDE(val)  vBIT(val, 10, 2)
#define	VXGE_HAL_ANBE_CFG_PORT_SYNC_1G_KX_OVERRIDE(val)	    vBIT(val, 14, 2)
/* 0x09dc0 */	u64	anbe_mgr_ctrl_port[2];
#define	VXGE_HAL_ANBE_MGR_CTRL_PORT_WE	mBIT(3)
#define	VXGE_HAL_ANBE_MGR_CTRL_PORT_STROBE		    mBIT(7)
#define	VXGE_HAL_ANBE_MGR_CTRL_PORT_ADDR(val)		    vBIT(val, 15, 9)
#define	VXGE_HAL_ANBE_MGR_CTRL_PORT_DATA(val)		    vBIT(val, 32, 32)
	u8	unused09de0[0x09de0-0x09dd0];

/* 0x09de0 */	u64	anbe_fw_mstr_port[2];
#define	VXGE_HAL_ANBE_FW_MSTR_PORT_CONNECT_BEAN_TO_SERDES   mBIT(3)
#define	VXGE_HAL_ANBE_FW_MSTR_PORT_TX_ZEROES_TO_SERDES	    mBIT(7)
/* 0x09df0 */	u64	anbe_hwfsm_gen_status_port[2];
#define	VXGE_HAL_ANBE_HWFSM_GEN_STATUS_PORT_RATEMGMT_CHOSE_10G_KX4_USING_PD\
							    mBIT(3)
#define	VXGE_HAL_ANBE_HWFSM_GEN_STATUS_PORT_RATEMGMT_CHOSE_10G_KX4_USING_DME\
							    mBIT(7)
#define	VXGE_HAL_ANBE_HWFSM_GEN_STATUS_PORT_RATEMGMT_CHOSE_1G_KX_USING_PD\
							    mBIT(11)
#define	VXGE_HAL_ANBE_HWFSM_GEN_STATUS_PORT_RATEMGMT_CHOSE_1G_KX_USING_DME\
							    mBIT(15)
#define	VXGE_HAL_ANBE_HWFSM_GEN_STATUS_PORT_RATEMGMT_ANBEFSM_STATE(val)\
							    vBIT(val, 18, 6)
#define	VXGE_HAL_ANBE_HWFSM_GEN_STATUS_PORT_RATEMGMT_BEAN_NEXT_PAGE_RECEIVED\
							    mBIT(27)
#define	VXGE_HAL_ANBE_HWFSM_GEN_STATUS_PORT_RATEMGMT_BEAN_PARALLEL_DETECT_FAULT\
							    mBIT(31)
#define	VXGE_HAL_ANBE_HWFSM_GEN_STATUS_PORT_RATEMGMT_BEAN_BASE_PAGE_RECEIVED\
							    mBIT(35)
#define	VXGE_HAL_ANBE_HWFSM_GEN_STATUS_PORT_RATEMGMT_BEAN_AUTONEG_COMPLETE\
							    mBIT(39)
#define	VXGE_HAL_ANBE_HWFSM_GEN_STATUS_PORT_RATEMGMT_UNEXP_NP_BEFORE_BP\
							    mBIT(43)
#define	VXGE_HAL_ANBE_HWFSM_GEN_STATUS_PORT_RATEMGMT_UNEXP_AN_COMPL_BEFORE_BP\
							    mBIT(47)
#define	VXGE_HAL_ANBE_HWFSM_GEN_STATUS_PORT_RATEMGMT_UNEXP_AN_COMPL_BEFORE_NP\
							    mBIT(51)
#define	VXGE_HAL_ANBE_HWFSM_GEN_STATUS_PORT_RATEMGMT_UNEXP_MODE_WHEN_AN_COMPL\
							    mBIT(55)
#define	VXGE_HAL_ANBE_HWFSM_GEN_STATUS_PORT_RATEMGMT_COUNT_BP(val)\
							    vBIT(val, 56, 4)
#define	VXGE_HAL_ANBE_HWFSM_GEN_STATUS_PORT_RATEMGMT_COUNT_NP(val)\
							    vBIT(val, 60, 4)
/* 0x09e00 */	u64	anbe_hwfsm_bp_status_port[2];
#define	VXGE_HAL_ANBE_HWFSM_BP_STATUS_PORT_RATEMGMT_BP_FEC_ENABLE	mBIT(32)
#define	VXGE_HAL_ANBE_HWFSM_BP_STATUS_PORT_RATEMGMT_BP_FEC_ABILITY	mBIT(33)
#define	VXGE_HAL_ANBE_HWFSM_BP_STATUS_PORT_RATEMGMT_BP_10G_KR_CAPABLE	mBIT(40)
#define	VXGE_HAL_ANBE_HWFSM_BP_STATUS_PORT_RATEMGMT_BP_10G_KX4_CAPABLE	mBIT(41)
#define	VXGE_HAL_ANBE_HWFSM_BP_STATUS_PORT_RATEMGMT_BP_1G_KX_CAPABLE	mBIT(42)
#define	VXGE_HAL_ANBE_HWFSM_BP_STATUS_PORT_RATEMGMT_BP_TX_NONCE(val)\
							    vBIT(val, 43, 5)
#define	VXGE_HAL_ANBE_HWFSM_BP_STATUS_PORT_RATEMGMT_BP_NP   mBIT(48)
#define	VXGE_HAL_ANBE_HWFSM_BP_STATUS_PORT_RATEMGMT_BP_ACK  mBIT(49)
#define	VXGE_HAL_ANBE_HWFSM_BP_STATUS_PORT_RATEMGMT_BP_REMOTE_FAULT mBIT(50)
#define	VXGE_HAL_ANBE_HWFSM_BP_STATUS_PORT_RATEMGMT_BP_ASM_DIR mBIT(51)
#define	VXGE_HAL_ANBE_HWFSM_BP_STATUS_PORT_RATEMGMT_BP_PAUSE mBIT(53)
#define	VXGE_HAL_ANBE_HWFSM_BP_STATUS_PORT_RATEMGMT_BP_ECHOED_NONCE(val)\
							    vBIT(val, 54, 5)
#define	VXGE_HAL_ANBE_HWFSM_BP_STATUS_PORT_RATEMGMT_BP_SELECTOR_FIELD(val)\
							    vBIT(val, 59, 5)
/* 0x09e10 */	u64	anbe_hwfsm_np_status_port[2];
#define	VXGE_HAL_ANBE_HWFSM_NP_STATUS_PORT_RATEMGMT_NP_BITS_47_TO_32(val)\
							    vBIT(val, 16, 16)
#define	VXGE_HAL_ANBE_HWFSM_NP_STATUS_PORT_RATEMGMT_NP_BITS_31_TO_0(val)\
							    vBIT(val, 32, 32)
	u8	unused09e30[0x09e30-0x09e20];

/* 0x09e30 */	u64	antp_gen_cfg_port[2];
/* 0x09e40 */	u64	antp_hwfsm_gen_status_port[2];
#define	VXGE_HAL_ANTP_HWFSM_GEN_STATUS_PORT_RATEMGMT_CHOSE_10G	mBIT(3)
#define	VXGE_HAL_ANTP_HWFSM_GEN_STATUS_PORT_RATEMGMT_CHOSE_1G	mBIT(7)
#define	VXGE_HAL_ANTP_HWFSM_GEN_STATUS_PORT_RATEMGMT_ANTPFSM_STATE(val)\
							    vBIT(val, 10, 6)
#define	VXGE_HAL_ANTP_HWFSM_GEN_STATUS_PORT_RATEMGMT_UNEXPECTED_TIMEOUT	mBIT(19)
#define	VXGE_HAL_ANTP_HWFSM_GEN_STATUS_PORT_RATEMGMT_AUTONEG_COMPLETE	mBIT(23)
#define	VXGE_HAL_ANTP_HWFSM_GEN_STATUS_PORT_RATEMGMT_UNEXPECTED_NO_LP_XNP\
							    mBIT(27)
#define	VXGE_HAL_ANTP_HWFSM_GEN_STATUS_PORT_RATEMGMT_GOT_LP_XNP	mBIT(31)
#define	VXGE_HAL_ANTP_HWFSM_GEN_STATUS_PORT_RATEMGMT_UNEXPECTED_MESSAGE_CODE\
							    mBIT(35)
#define	VXGE_HAL_ANTP_HWFSM_GEN_STATUS_PORT_RATEMGMT_GOT_LP_MESSAGE_CODE_10G_1K\
							    mBIT(39)
#define	VXGE_HAL_ANTP_HWFSM_GEN_STATUS_PORT_RATEMGMT_UNEXPECTED_NO_HCD	mBIT(43)
#define	VXGE_HAL_ANTP_HWFSM_GEN_STATUS_PORT_RATEMGMT_FOUND_HCD	mBIT(47)
#define	VXGE_HAL_ANTP_HWFSM_GEN_STATUS_PORT_RATEMGMT_UNEXPECTED_INVALID_RATE\
							    mBIT(51)
#define	VXGE_HAL_ANTP_HWFSM_GEN_STATUS_PORT_RATEMGMT_VALID_RATE	mBIT(55)
#define	VXGE_HAL_ANTP_HWFSM_GEN_STATUS_PORT_RATEMGMT_PERSISTENT_LDOWN mBIT(59)
/* 0x09e50 */	u64	antp_hwfsm_bp_status_port[2];
#define	VXGE_HAL_ANTP_HWFSM_BP_STATUS_PORT_RATEMGMT_BP_NP	mBIT(0)
#define	VXGE_HAL_ANTP_HWFSM_BP_STATUS_PORT_RATEMGMT_BP_ACK	mBIT(1)
#define	VXGE_HAL_ANTP_HWFSM_BP_STATUS_PORT_RATEMGMT_BP_RF	mBIT(2)
#define	VXGE_HAL_ANTP_HWFSM_BP_STATUS_PORT_RATEMGMT_BP_XNP	mBIT(3)
#define	VXGE_HAL_ANTP_HWFSM_BP_STATUS_PORT_RATEMGMT_BP_ABILITY_FIELD(val)\
								vBIT(val, 4, 7)
#define	VXGE_HAL_ANTP_HWFSM_BP_STATUS_PORT_RATEMGMT_BP_SELECTOR_FIELD(val)\
								vBIT(val, 11, 5)
/* 0x09e60 */	u64	antp_hwfsm_xnp_status_port[2];
#define	VXGE_HAL_ANTP_HWFSM_XNP_STATUS_PORT_RATEMGMT_XNP_NP	mBIT(0)
#define	VXGE_HAL_ANTP_HWFSM_XNP_STATUS_PORT_RATEMGMT_XNP_ACK	mBIT(1)
#define	VXGE_HAL_ANTP_HWFSM_XNP_STATUS_PORT_RATEMGMT_XNP_MP	mBIT(2)
#define	VXGE_HAL_ANTP_HWFSM_XNP_STATUS_PORT_RATEMGMT_XNP_ACK2	mBIT(3)
#define	VXGE_HAL_ANTP_HWFSM_XNP_STATUS_PORT_RATEMGMT_XNP_TOGGLE	mBIT(4)
#define	VXGE_HAL_ANTP_HWFSM_XNP_STATUS_PORT_RATEMGMT_XNP_MESSAGE_CODE(val)\
							    vBIT(val, 5, 11)
#define	VXGE_HAL_ANTP_HWFSM_XNP_STATUS_PORT_RATEMGMT_XNP_UNF_CODE_FIELD1(val)\
							    vBIT(val, 16, 16)
#define	VXGE_HAL_ANTP_HWFSM_XNP_STATUS_PORT_RATEMGMT_XNP_UNF_CODE_FIELD2(val)\
							    vBIT(val, 32, 16)
/* 0x09e70 */	u64	mdio_mgr_access_port[2];
#define	VXGE_HAL_MDIO_MGR_ACCESS_PORT_STROBE_ONE	    mBIT(3)
#define	VXGE_HAL_MDIO_MGR_ACCESS_PORT_OP_TYPE(val)	    vBIT(val, 5, 3)
#define	VXGE_HAL_MDIO_MGR_ACCESS_PORT_DEVAD(val)	    vBIT(val, 11, 5)
#define	VXGE_HAL_MDIO_MGR_ACCESS_PORT_ADDR(val)		    vBIT(val, 16, 16)
#define	VXGE_HAL_MDIO_MGR_ACCESS_PORT_DATA(val)		    vBIT(val, 32, 16)
#define	VXGE_HAL_MDIO_MGR_ACCESS_PORT_ST_PATTERN(val)	    vBIT(val, 49, 2)
#define	VXGE_HAL_MDIO_MGR_ACCESS_PORT_PREAMBLE		    mBIT(51)
#define	VXGE_HAL_MDIO_MGR_ACCESS_PORT_PRTAD(val)	    vBIT(val, 55, 5)
#define	VXGE_HAL_MDIO_MGR_ACCESS_PORT_STROBE_TWO	    mBIT(63)
	u8	unused0a200[0x0a200-0x09e80];

/* 0x0a200 */	u64	xmac_vsport_choices_vh[17];
#define	VXGE_HAL_XMAC_VSPORT_CHOICES_VH_VSPORT_VECTOR(val)  vBIT(val, 0, 17)
	u8	unused0a400[0x0a400-0x0a288];

/* 0x0a400 */	u64	rx_thresh_cfg_vp[17];
#define	VXGE_HAL_RX_THRESH_CFG_VP_PAUSE_LOW_THR(val)	    vBIT(val, 0, 8)
#define	VXGE_HAL_RX_THRESH_CFG_VP_PAUSE_HIGH_THR(val)	    vBIT(val, 8, 8)
#define	VXGE_HAL_RX_THRESH_CFG_VP_RED_THR_0(val)	    vBIT(val, 16, 8)
#define	VXGE_HAL_RX_THRESH_CFG_VP_RED_THR_1(val)	    vBIT(val, 24, 8)
#define	VXGE_HAL_RX_THRESH_CFG_VP_RED_THR_2(val)	    vBIT(val, 32, 8)
#define	VXGE_HAL_RX_THRESH_CFG_VP_RED_THR_3(val)	    vBIT(val, 40, 8)
	u8	unused0ac90[0x0ac90-0x0a488];

} vxge_hal_mrpcim_reg_t;

__EXTERN_END_DECLS

#endif /* VXGE_HAL_MRPCIM_REGS_H */
