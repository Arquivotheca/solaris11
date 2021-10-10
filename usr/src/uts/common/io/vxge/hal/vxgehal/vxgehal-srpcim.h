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
 * FileName :   vxgehal-srpcim.h
 *
 * Description:  API and data structures used for programming SRPCIM
 *
 * Created:       09 October 2008
 */

#ifndef	VXGE_HAL_SRPCIM_H
#define	VXGE_HAL_SRPCIM_H

__EXTERN_BEGIN_DECLS

/*
 * __vxge_hal_srpcim_vpath_t
 *
 * HAL srpcim vpath messaging state.
 */
typedef struct __vxge_hal_srpcim_vpath_t {
	u32 registered;
	u32 srpcim_id;
	u32 ifmsg_up_seqno;
} __vxge_hal_srpcim_vpath_t;

/*
 * __vxge_hal_srpcim_t
 *
 * HAL srpcim object. Represents privileged mode srpcim device.
 */
typedef struct __vxge_hal_srpcim_t {
	__vxge_hal_srpcim_vpath_t vpath_state[VXGE_HAL_MAX_VIRTUAL_PATHS];
} __vxge_hal_srpcim_t;


vxge_hal_status_e __vxge_hal_srpcim_alarm_process(
			__vxge_hal_device_t *hldev,
			u32 srpcim_id,
			u32 skip_alarms);

vxge_hal_status_e __vxge_hal_srpcim_intr_enable(
			__vxge_hal_device_t *hldev,
			u32 srpcim_id);

vxge_hal_status_e __vxge_hal_srpcim_intr_disable(
			__vxge_hal_device_t *hldev,
			u32 srpcim_id);

vxge_hal_status_e __vxge_hal_srpcim_initialize(
			__vxge_hal_device_t *hldev);

vxge_hal_status_e __vxge_hal_srpcim_terminate(
			__vxge_hal_device_t *hldev);

vxge_hal_status_e
__vxge_hal_srpcim_reset(__vxge_hal_device_t *hldev);

vxge_hal_status_e
__vxge_hal_srpcim_reset_poll(__vxge_hal_device_t *hldev);

__EXTERN_END_DECLS

#endif /* VXGE_HAL_SRPCIM_H */
