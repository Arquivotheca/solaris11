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
 * FileName :   vxgehal-legacy-reg.h
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

#ifndef	VXGE_HAL_LEGACY_REGS_H
#define	VXGE_HAL_LEGACY_REGS_H

__EXTERN_BEGIN_DECLS

typedef struct vxge_hal_legacy_reg_t {

	u8	unused00010[0x00010];

/* 0x00010 */	u64	toc_swapper_fb;
#define	VXGE_HAL_TOC_SWAPPER_FB_INITIAL_VAL(val)	vBIT(val, 0, 64)
/* 0x00018 */	u64	pifm_rd_swap_en;
#define	VXGE_HAL_PIFM_RD_SWAP_EN_PIFM_RD_SWAP_EN(val)	vBIT(val, 0, 64)
/* 0x00020 */	u64	pifm_rd_flip_en;
#define	VXGE_HAL_PIFM_RD_FLIP_EN_PIFM_RD_FLIP_EN(val)	vBIT(val, 0, 64)
/* 0x00028 */	u64	pifm_wr_swap_en;
#define	VXGE_HAL_PIFM_WR_SWAP_EN_PIFM_WR_SWAP_EN(val)	vBIT(val, 0, 64)
/* 0x00030 */	u64	pifm_wr_flip_en;
#define	VXGE_HAL_PIFM_WR_FLIP_EN_PIFM_WR_FLIP_EN(val)	vBIT(val, 0, 64)
/* 0x00038 */	u64	toc_first_pointer;
#define	VXGE_HAL_TOC_FIRST_POINTER_INITIAL_VAL(val)	vBIT(val, 0, 64)
/* 0x00040 */	u64	host_access_en;
#define	VXGE_HAL_HOST_ACCESS_EN_HOST_ACCESS_EN(val)	vBIT(val, 0, 64)

} vxge_hal_legacy_reg_t;

__EXTERN_END_DECLS

#endif /* VXGE_HAL_LEGACY_REGS_H */
