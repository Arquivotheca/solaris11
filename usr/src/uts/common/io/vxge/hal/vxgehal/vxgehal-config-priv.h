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
 * FileName :   vxgehal-config-priv.h
 *
 * Description:  HAL config functionality
 *
 * Created:       3 August 2007
 */

#ifndef	XGEHAL_CONFIG_PRIV_H
#define	XGEHAL_CONFIG_PRIV_H

__EXTERN_BEGIN_DECLS

vxge_hal_status_e
__vxge_hal_driver_config_check(vxge_hal_driver_config_t *config);

vxge_hal_status_e
__vxge_hal_device_mac_config_check(vxge_hal_mac_config_t *mac_config);

vxge_hal_status_e
__vxge_hal_device_lag_config_check(vxge_hal_lag_config_t *lag_config);

vxge_hal_status_e
__vxge_hal_vpath_qos_config_check(vxge_hal_vpath_qos_config_t *config);

vxge_hal_status_e
__vxge_hal_mrpcim_config_check(vxge_hal_mrpcim_config_t *config);

vxge_hal_status_e
__vxge_hal_device_ring_config_check(vxge_hal_ring_config_t *ring_config);

vxge_hal_status_e
__vxge_hal_device_fifo_config_check(vxge_hal_fifo_config_t *fifo_config);


vxge_hal_status_e
__vxge_hal_device_tim_intr_config_check
	(vxge_hal_tim_intr_config_t *tim_intr_config);

vxge_hal_status_e
__vxge_hal_device_vpath_config_check(vxge_hal_vp_config_t *vp_config);

vxge_hal_status_e
__vxge_hal_device_config_check(vxge_hal_device_config_t *new_config);

__EXTERN_END_DECLS

#endif /* XGEHAL_CONFIG_PRIV_H */
