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
 * FileName :   vxgehal-pcicfgmgmt-reg.h
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

#ifndef	VXGE_HAL_PCICFGMGMT_REGS_H
#define	VXGE_HAL_PCICFGMGMT_REGS_H

__EXTERN_BEGIN_DECLS

typedef struct vxge_hal_pcicfgmgmt_reg_t {

/* 0x00000 */	u64	resource_no;
#define	VXGE_HAL_RESOURCE_NO_PFN_OR_VF	BIT(3)
/* 0x00008 */	u64	bargrp_pf_or_vf_bar0_mask;
#define	VXGE_HAL_BARGRP_PF_OR_VF_BAR0_MASK_BARGRP_PF_OR_VF_BAR0_MASK(val)\
							    vBIT(val, 2, 6)
/* 0x00010 */	u64	bargrp_pf_or_vf_bar1_mask;
#define	VXGE_HAL_BARGRP_PF_OR_VF_BAR1_MASK_BARGRP_PF_OR_VF_BAR1_MASK(val)\
							    vBIT(val, 2, 6)
/* 0x00018 */	u64	bargrp_pf_or_vf_bar2_mask;
#define	VXGE_HAL_BARGRP_PF_OR_VF_BAR2_MASK_BARGRP_PF_OR_VF_BAR2_MASK(val)\
							    vBIT(val, 2, 6)
/* 0x00020 */	u64	msixgrp_no;
#define	VXGE_HAL_MSIXGRP_NO_TABLE_SIZE(val)		    vBIT(val, 5, 11)

} vxge_hal_pcicfgmgmt_reg_t;

__EXTERN_END_DECLS

#endif /* VXGE_HAL_PCICFGMGMT_REGS_H */
