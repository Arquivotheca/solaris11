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
 * FileName :   vxgehal-swapper.h
 *
 * Description:  HAL Swapper appi
 *
 * Created:       27 July 2007
 */

#ifndef	VXGE_HAL_SWAPPER_H
#define	VXGE_HAL_SWAPPER_H

__EXTERN_BEGIN_DECLS

#define	VXGE_HAL_SWAPPER_INITIAL_VALUE			0x0123456789abcdefULL
#define	VXGE_HAL_SWAPPER_BYTE_SWAPPED			0xefcdab8967452301ULL
#define	VXGE_HAL_SWAPPER_BIT_FLIPPED			0x80c4a2e691d5b3f7ULL
#define	VXGE_HAL_SWAPPER_BYTE_SWAPPED_BIT_FLIPPED	0xf7b3d591e6a2c480ULL

#define	VXGE_HAL_SWAPPER_READ_BYTE_SWAP_ENABLE		0xFFFFFFFFFFFFFFFFULL
#define	VXGE_HAL_SWAPPER_READ_BYTE_SWAP_DISABLE		0x0000000000000000ULL

#define	VXGE_HAL_SWAPPER_READ_BIT_FLAP_ENABLE		0xFFFFFFFFFFFFFFFFULL
#define	VXGE_HAL_SWAPPER_READ_BIT_FLAP_DISABLE		0x0000000000000000ULL

#define	VXGE_HAL_SWAPPER_WRITE_BYTE_SWAP_ENABLE		0xFFFFFFFFFFFFFFFFULL
#define	VXGE_HAL_SWAPPER_WRITE_BYTE_SWAP_DISABLE	0x0000000000000000ULL

#define	VXGE_HAL_SWAPPER_WRITE_BIT_FLAP_ENABLE		0xFFFFFFFFFFFFFFFFULL
#define	VXGE_HAL_SWAPPER_WRITE_BIT_FLAP_DISABLE		0x0000000000000000ULL

vxge_hal_status_e
__vxge_hal_legacy_swapper_set(
			pci_dev_h		pdev,
			pci_reg_h		regh,
			vxge_hal_legacy_reg_t	*legacy_reg);
vxge_hal_status_e
__vxge_hal_vpath_swapper_set(
			vxge_hal_device_t *hldev,
			u32 vp_id);

vxge_hal_status_e
__vxge_hal_kdfc_swapper_set(
			vxge_hal_device_t *hldev,
			u32 vp_id);

__EXTERN_END_DECLS

#endif /* VXGE_HAL_SWAPPER_H */
