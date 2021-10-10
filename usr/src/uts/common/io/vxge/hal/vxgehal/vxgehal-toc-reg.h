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
 * FileName :   vxgehal-toc-reg.h
 *
 * Description:  Auto generated Titan register space
 *
 * Generation Information:
 *
 *       Source File(s):
 *
 *       C Template:       templates/c/location.st (version 1.10)
 *       Code Generation:  java/SWIF_Codegen.java (version 1.62)
 *       Frontend:      java/SWIF_Main.java (version 1.52)
 */

#ifndef	VXGE_HAL_TOC_REGS_H
#define	VXGE_HAL_TOC_REGS_H

__EXTERN_BEGIN_DECLS

typedef struct vxge_hal_toc_reg_t {

	u8	unused00050[0x00050];

/* 0x00050 */	u64	toc_common_pointer;
#define	VXGE_HAL_TOC_COMMON_POINTER_INITIAL_VAL(val)	    vBIT(val, 0, 64)
/* 0x00058 */	u64	toc_memrepair_pointer;
#define	VXGE_HAL_TOC_MEMREPAIR_POINTER_INITIAL_VAL(val)	    vBIT(val, 0, 64)
/* 0x00060 */	u64	toc_pcicfgmgmt_pointer[17];
#define	VXGE_HAL_TOC_PCICFGMGMT_POINTER_INITIAL_VAL(val)    vBIT(val, 0, 64)
	u8	unused001e0[0x001e0-0x000e8];

/* 0x001e0 */	u64	toc_mrpcim_pointer;
#define	VXGE_HAL_TOC_MRPCIM_POINTER_INITIAL_VAL(val)	    vBIT(val, 0, 64)
/* 0x001e8 */	u64	toc_srpcim_pointer[17];
#define	VXGE_HAL_TOC_SRPCIM_POINTER_INITIAL_VAL(val)	    vBIT(val, 0, 64)
	u8	unused00278[0x00278-0x00270];

/* 0x00278 */	u64	toc_vpmgmt_pointer[17];
#define	VXGE_HAL_TOC_VPMGMT_POINTER_INITIAL_VAL(val)	    vBIT(val, 0, 64)
	u8	unused00390[0x00390-0x00300];

/* 0x00390 */	u64	toc_vpath_pointer[17];
#define	VXGE_HAL_TOC_VPATH_POINTER_INITIAL_VAL(val)	    vBIT(val, 0, 64)
	u8	unused004a0[0x004a0-0x00418];

/* 0x004a0 */	u64	toc_kdfc;
#define	VXGE_HAL_TOC_KDFC_INITIAL_OFFSET(val)		    vBIT(val, 0, 61)
#define	VXGE_HAL_TOC_KDFC_INITIAL_BIR(val)		    vBIT(val, 61, 3)
/* 0x004a8 */	u64	toc_usdc;
#define	VXGE_HAL_TOC_USDC_INITIAL_OFFSET(val)		    vBIT(val, 0, 61)
#define	VXGE_HAL_TOC_USDC_INITIAL_BIR(val)		    vBIT(val, 61, 3)
/* 0x004b0 */	u64	toc_kdfc_vpath_stride;
#define	VXGE_HAL_TOC_KDFC_VPATH_STRIDE_INITIAL_TOC_KDFC_VPATH_STRIDE(val)\
							    vBIT(val, 0, 64)
/* 0x004b8 */	u64	toc_kdfc_fifo_stride;
#define	VXGE_HAL_TOC_KDFC_FIFO_STRIDE_INITIAL_TOC_KDFC_FIFO_STRIDE(val)\
							    vBIT(val, 0, 64)

} vxge_hal_toc_reg_t;

__EXTERN_END_DECLS

#endif /* VXGE_HAL_TOC_REGS_H */
