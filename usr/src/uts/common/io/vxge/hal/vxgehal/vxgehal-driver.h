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
 * FileName :   vxgehal-driver.h
 *
 * Description:  HAL driver object functionality
 *
 * Created:       27 December 2006
 */


#ifndef	VXGE_HAL_DRIVER_H
#define	VXGE_HAL_DRIVER_H

__EXTERN_BEGIN_DECLS

/* maximum number of events consumed in a syncle poll() cycle */
#define	VXGE_HAL_DRIVER_QUEUE_CONSUME_MAX	5

/*
 * struct __vxge_hal_driver_t - Represents HAL object for driver.
 * @config: HAL configuration.
 * @devices: List of all PCI-enumerated X3100 devices in the system.
 * A single vxge_hal_driver_t instance contains zero or more
 * X3100 devices.
 * @devices_lock: Lock to protect %devices when inserting/removing.
 * @is_initialized: True if HAL is initialized; false otherwise.
 * @uld_callbacks: Upper-layer driver callbacks. See vxge_hal_uld_cbs_t {}.
 * @debug_module_mask: 32bit mask that defines which components of the
 * driver are to be traced. The trace-able components are listed in
 * xgehal_debug.h:
 * @debug_level: See vxge_debug_level_e {}.
 *
 * HAL (driver) object. There is a single instance of this structure per HAL.
 */
typedef struct __vxge_hal_driver_t {
	vxge_hal_driver_config_t		config;
	int				is_initialized;
	vxge_hal_uld_cbs_t		  uld_callbacks;
	u32				debug_level;
} __vxge_hal_driver_t;

extern __vxge_hal_driver_t *g_vxge_hal_driver;

__EXTERN_END_DECLS

#endif /* VXGE_HAL_DRIVER_H */
