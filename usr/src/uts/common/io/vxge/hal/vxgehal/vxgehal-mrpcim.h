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
 * FileName :   vxgehal-mrpcim.h
 *
 * Description:  API and data structures used for programming MRPCIM
 *
 * Created:       16 September 2008
 */

#ifndef	VXGE_HAL_MRPCIM_H
#define	VXGE_HAL_MRPCIM_H

__EXTERN_BEGIN_DECLS

/*
 * __vxge_hal_mrpcim_t
 *
 * HAL mrpcim object. Represents privileged mode device.
 */
typedef struct __vxge_hal_mrpcim_t {
	u32 mdio_phy_prtad0;
	u32 mdio_phy_prtad1;
	u32 mdio_dte_prtad0;
	u32 mdio_dte_prtad1;
	vxge_hal_vpd_data_t vpd_data;
	__vxge_hal_blockpool_entry_t *mrpcim_stats_block;
	vxge_hal_mrpcim_stats_hw_info_t *mrpcim_stats;
	vxge_hal_mrpcim_stats_hw_info_t mrpcim_stats_sav;
	vxge_hal_mrpcim_xpak_stats_t xpak_stats[VXGE_HAL_MAC_MAX_WIRE_PORTS];
} __vxge_hal_mrpcim_t;

#define	VXGE_HAL_MRPCIM_STATS_PIO_READ(loc, offset) {			\
	status = vxge_hal_mrpcim_stats_access(devh,			\
				VXGE_HAL_STATS_OP_READ,			\
				loc,					\
				offset,					\
				&val64);				\
									\
	if (status != VXGE_HAL_OK) {					\
		vxge_hal_trace_log_stats(devh, NULL_VPID,		\
		    "<==  %s:%s:%d Result = %d",			\
		    __FILE__, __func__, __LINE__, status);		\
		return (status);					\
	}								\
}

#define	VXGE_HAL_MRPCIM_ERROR_REG_CLEAR(reg)				\
	vxge_os_pio_mem_write64(					\
		hldev->header.pdev,					\
		hldev->header.regh0,					\
		VXGE_HAL_INTR_MASK_ALL,					\
		(reg));

#define	VXGE_HAL_MRPCIM_ERROR_REG_MASK(reg)				\
	vxge_os_pio_mem_write64(					\
		hldev->header.pdev,					\
		hldev->header.regh0,					\
		VXGE_HAL_INTR_MASK_ALL,					\
		(reg));

#define	VXGE_HAL_MRPCIM_ERROR_REG_UNMASK(mask, reg)			\
	vxge_os_pio_mem_write64(					\
		hldev->header.pdev,					\
		hldev->header.regh0,					\
		~mask,							\
		(reg));

vxge_hal_status_e
__vxge_hal_mrpcim_mdio_access(
	vxge_hal_device_h devh,
	u32 port,
	u32 operation,
	u32 device,
	u16 addr,
	u16 *data);

vxge_hal_status_e
__vxge_hal_mrpcim_initialize(__vxge_hal_device_t *hldev);

vxge_hal_status_e
__vxge_hal_mrpcim_terminate(__vxge_hal_device_t *hldev);

void
__vxge_hal_mrpcim_get_vpd_data(__vxge_hal_device_t *hldev);

void __vxge_hal_mrpcim_xpak_counter_check(__vxge_hal_device_t *hldev,
					u32 port, u32 type, u32 value);

vxge_hal_status_e
__vxge_hal_mrpcim_stats_get(
		__vxge_hal_device_t *hldev,
		vxge_hal_mrpcim_stats_hw_info_t *mrpcim_stats);

vxge_hal_status_e
__vxge_hal_mrpcim_mac_configure(__vxge_hal_device_t *hldev);

vxge_hal_status_e
__vxge_hal_mrpcim_lag_configure(__vxge_hal_device_t *hldev);

__EXTERN_END_DECLS

#endif /* VXGE_HAL_MRPCIM_H */
