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
 * FileName :   vxgehal-debug.h
 *
 * Description:  debug facilities
 *
 * Created:       27 December 2006
 */

#ifndef	VXGEHAL_DEBUG_H
#define	VXGEHAL_DEBUG_H

__EXTERN_BEGIN_DECLS

#define	D_ERR_MASK   ((__vxge_hal_device_t *)hldev)->d_err_mask
#define	D_INFO_MASK  ((__vxge_hal_device_t *)hldev)->d_info_mask
#define	D_TRACE_MASK ((__vxge_hal_device_t *)hldev)->d_trace_mask

#ifdef	VXGE_TRACE_INTO_CIRCULAR_ARR
#define	vxge_hal_debug_printf vxge_os_vasprintf
#else
#define	vxge_hal_debug_printf vxge_os_vaprintf
#endif

#ifndef	VXGE_DEBUG_INLINE_FUNCTIONS
#define	vxge_hal_debug_noop(hldev, vpid, fmt, ...)
#else
static inline void vxge_hal_debug_noop(
			vxge_hal_device_h hldev,
			u32 vpid,
			char *fmt, ...)
{

}
#endif

#if (VXGE_COMPONENT_HAL_DRIVER & VXGE_DEBUG_MODULE_MASK)
#define	vxge_hal_err_log_driver					\
	if (g_debug_level&VXGE_ERR) vxge_os_vaprintf
#define	vxge_hal_info_log_driver				\
	if (g_debug_level&VXGE_INFO) vxge_os_vaprintf
#define	vxge_hal_trace_log_driver				\
	if (g_debug_level&VXGE_TRACE) vxge_os_vaprintf
#else
#define	vxge_hal_err_log_driver   vxge_hal_debug_noop
#define	vxge_hal_info_log_driver  vxge_hal_debug_noop
#define	vxge_hal_trace_log_driver vxge_hal_debug_noop
#endif

#if (VXGE_COMPONENT_HAL_DEVICE & VXGE_DEBUG_MODULE_MASK)
#define	vxge_hal_err_log_device					\
	if (D_ERR_MASK & VXGE_COMPONENT_HAL_DEVICE) vxge_hal_debug_printf
#define	vxge_hal_info_log_device				\
	if (D_INFO_MASK & VXGE_COMPONENT_HAL_DEVICE) vxge_hal_debug_printf
#define	vxge_hal_trace_log_device				\
	if (D_TRACE_MASK & VXGE_COMPONENT_HAL_DEVICE) vxge_hal_debug_printf
#else
#define	vxge_hal_err_log_device   vxge_hal_debug_noop
#define	vxge_hal_info_log_device  vxge_hal_debug_noop
#define	vxge_hal_trace_log_device vxge_hal_debug_noop
#endif

#if (VXGE_COMPONENT_HAL_DEVICE_IRQ & VXGE_DEBUG_MODULE_MASK)
#define	vxge_hal_err_log_device_irq				\
	if (D_ERR_MASK & VXGE_COMPONENT_HAL_DEVICE_IRQ) vxge_hal_debug_printf
#define	vxge_hal_info_log_device_irq				\
	if (D_INFO_MASK & VXGE_COMPONENT_HAL_DEVICE_IRQ) vxge_hal_debug_printf
#define	vxge_hal_trace_log_device_irq				\
	if (D_TRACE_MASK & VXGE_COMPONENT_HAL_DEVICE_IRQ) vxge_hal_debug_printf
#else
#define	vxge_hal_err_log_device_irq   vxge_hal_debug_noop
#define	vxge_hal_info_log_device_irq  vxge_hal_debug_noop
#define	vxge_hal_trace_log_device_irq vxge_hal_debug_noop
#endif

#if (VXGE_COMPONENT_HAL_VPATH & VXGE_DEBUG_MODULE_MASK)
#define	vxge_hal_err_log_vpath					\
	if (D_ERR_MASK & VXGE_COMPONENT_HAL_VPATH) vxge_hal_debug_printf
#define	vxge_hal_info_log_vpath					\
	if (D_INFO_MASK & VXGE_COMPONENT_HAL_VPATH) vxge_hal_debug_printf
#define	vxge_hal_trace_log_vpath				\
	if (D_TRACE_MASK & VXGE_COMPONENT_HAL_VPATH) vxge_hal_debug_printf
#else
#define	vxge_hal_err_log_vpath   vxge_hal_debug_noop
#define	vxge_hal_info_log_vpath  vxge_hal_debug_noop
#define	vxge_hal_trace_log_vpath vxge_hal_debug_noop
#endif

#if (VXGE_COMPONENT_HAL_VPATH_IRQ & VXGE_DEBUG_MODULE_MASK)
#define	vxge_hal_err_log_vpath_irq					\
	if (D_ERR_MASK & VXGE_COMPONENT_HAL_VPATH_IRQ) vxge_hal_debug_printf
#define	vxge_hal_info_log_vpath_irq					\
	if (D_INFO_MASK & VXGE_COMPONENT_HAL_VPATH_IRQ) vxge_hal_debug_printf
#define	vxge_hal_trace_log_vpath_irq					\
	if (D_TRACE_MASK & VXGE_COMPONENT_HAL_VPATH_IRQ) vxge_hal_debug_printf
#else
#define	vxge_hal_err_log_vpath_irq   vxge_hal_debug_noop
#define	vxge_hal_info_log_vpath_irq  vxge_hal_debug_noop
#define	vxge_hal_trace_log_vpath_irq vxge_hal_debug_noop
#endif

#if (VXGE_COMPONENT_HAL_CONFIG & VXGE_DEBUG_MODULE_MASK)
#define	vxge_hal_err_log_config						\
	if (D_ERR_MASK & VXGE_COMPONENT_HAL_CONFIG) vxge_hal_debug_printf
#define	vxge_hal_info_log_config					\
	if (D_INFO_MASK & VXGE_COMPONENT_HAL_CONFIG) vxge_hal_debug_printf
#define	vxge_hal_trace_log_config					\
	if (D_TRACE_MASK & VXGE_COMPONENT_HAL_CONFIG) vxge_hal_debug_printf
#else
#define	vxge_hal_err_log_config   vxge_hal_debug_noop
#define	vxge_hal_info_log_config  vxge_hal_debug_noop
#define	vxge_hal_trace_log_config vxge_hal_debug_noop
#endif

#if (VXGE_COMPONENT_HAL_MM & VXGE_DEBUG_MODULE_MASK)
#define	vxge_hal_err_log_mm						\
	if (D_ERR_MASK & VXGE_COMPONENT_HAL_MM) vxge_hal_debug_printf
#define	vxge_hal_info_log_mm						\
	if (D_INFO_MASK & VXGE_COMPONENT_HAL_MM) vxge_hal_debug_printf
#define	vxge_hal_trace_log_mm						\
	if (D_TRACE_MASK & VXGE_COMPONENT_HAL_MM) vxge_hal_debug_printf
#else
#define	vxge_hal_err_log_mm   vxge_hal_debug_noop
#define	vxge_hal_info_log_mm  vxge_hal_debug_noop
#define	vxge_hal_trace_log_mm vxge_hal_debug_noop
#endif

#if (VXGE_COMPONENT_HAL_POOL & VXGE_DEBUG_MODULE_MASK)
#define	vxge_hal_err_log_pool						\
	if (D_ERR_MASK & VXGE_COMPONENT_HAL_POOL) vxge_hal_debug_printf
#define	vxge_hal_info_log_pool						\
	if (D_INFO_MASK & VXGE_COMPONENT_HAL_POOL) vxge_hal_debug_printf
#define	vxge_hal_trace_log_pool						\
	if (D_TRACE_MASK & VXGE_COMPONENT_HAL_POOL) vxge_hal_debug_printf
#else
#define	vxge_hal_err_log_pool   vxge_hal_debug_noop
#define	vxge_hal_info_log_pool  vxge_hal_debug_noop
#define	vxge_hal_trace_log_pool vxge_hal_debug_noop
#endif

#if (VXGE_COMPONENT_HAL_QUEUE & VXGE_DEBUG_MODULE_MASK)
#define	vxge_hal_err_log_queue						\
	if (D_ERR_MASK & VXGE_COMPONENT_HAL_QUEUE) vxge_hal_debug_printf
#define	vxge_hal_info_log_queue						\
	if (D_INFO_MASK & VXGE_COMPONENT_HAL_QUEUE) vxge_hal_debug_printf
#define	vxge_hal_trace_log_queue					\
	if (D_TRACE_MASK & VXGE_COMPONENT_HAL_QUEUE) vxge_hal_debug_printf
#else
#define	vxge_hal_err_log_queue   vxge_hal_debug_noop
#define	vxge_hal_info_log_queue  vxge_hal_debug_noop
#define	vxge_hal_trace_log_queue vxge_hal_debug_noop
#endif

#if (VXGE_COMPONENT_HAL_BITMAP & VXGE_DEBUG_MODULE_MASK)
#define	vxge_hal_err_log_bitmap						\
	if (D_ERR_MASK & VXGE_COMPONENT_HAL_BITMAP) vxge_hal_debug_printf
#define	vxge_hal_info_log_bitmap					\
	if (D_INFO_MASK & VXGE_COMPONENT_HAL_BITMAP) vxge_hal_debug_printf
#define	vxge_hal_trace_log_bitmap					\
	if (D_TRACE_MASK & VXGE_COMPONENT_HAL_BITMAP) vxge_hal_debug_printf
#else
#define	vxge_hal_err_log_bitmap   vxge_hal_debug_noop
#define	vxge_hal_info_log_bitmap  vxge_hal_debug_noop
#define	vxge_hal_trace_log_bitmap vxge_hal_debug_noop
#endif

#if (VXGE_COMPONENT_HAL_CHANNEL & VXGE_DEBUG_MODULE_MASK)
#define	vxge_hal_err_log_channel					\
	if (D_ERR_MASK & VXGE_COMPONENT_HAL_CHANNEL) vxge_hal_debug_printf
#define	vxge_hal_info_log_channel					\
	if (D_INFO_MASK & VXGE_COMPONENT_HAL_CHANNEL) vxge_hal_debug_printf
#define	vxge_hal_trace_log_channel					\
	if (D_TRACE_MASK & VXGE_COMPONENT_HAL_CHANNEL) vxge_hal_debug_printf
#else
#define	vxge_hal_err_log_channel   vxge_hal_debug_noop
#define	vxge_hal_info_log_channel  vxge_hal_debug_noop
#define	vxge_hal_trace_log_channel vxge_hal_debug_noop
#endif

#if (VXGE_COMPONENT_HAL_FIFO & VXGE_DEBUG_MODULE_MASK)
#define	vxge_hal_err_log_fifo						\
	if (D_ERR_MASK & VXGE_COMPONENT_HAL_FIFO) vxge_hal_debug_printf
#define	vxge_hal_info_log_fifo						\
	if (D_INFO_MASK & VXGE_COMPONENT_HAL_FIFO) vxge_hal_debug_printf
#define	vxge_hal_trace_log_fifo						\
	if (D_TRACE_MASK & VXGE_COMPONENT_HAL_FIFO) vxge_hal_debug_printf
#else
#define	vxge_hal_err_log_fifo   vxge_hal_debug_noop
#define	vxge_hal_info_log_fifo  vxge_hal_debug_noop
#define	vxge_hal_trace_log_fifo vxge_hal_debug_noop
#endif

#if (VXGE_COMPONENT_HAL_RING & VXGE_DEBUG_MODULE_MASK)
#define	vxge_hal_err_log_ring						\
	if (D_ERR_MASK & VXGE_COMPONENT_HAL_RING) vxge_hal_debug_printf
#define	vxge_hal_info_log_ring						\
	if (D_INFO_MASK & VXGE_COMPONENT_HAL_RING) vxge_hal_debug_printf
#define	vxge_hal_trace_log_ring						\
	if (D_TRACE_MASK & VXGE_COMPONENT_HAL_RING) vxge_hal_debug_printf
#else
#define	vxge_hal_err_log_ring   vxge_hal_debug_noop
#define	vxge_hal_info_log_ring  vxge_hal_debug_noop
#define	vxge_hal_trace_log_ring vxge_hal_debug_noop
#endif

#if (VXGE_COMPONENT_HAL_DMQ & VXGE_DEBUG_MODULE_MASK)
#define	vxge_hal_err_log_dmq						\
	if (D_ERR_MASK & VXGE_COMPONENT_HAL_DMQ) vxge_hal_debug_printf
#define	vxge_hal_info_log_dmq						\
	if (D_INFO_MASK & VXGE_COMPONENT_HAL_DMQ) vxge_hal_debug_printf
#define	vxge_hal_trace_log_dmq						\
	if (D_TRACE_MASK & VXGE_COMPONENT_HAL_DMQ) vxge_hal_debug_printf
#else
#define	vxge_hal_err_log_dmq   vxge_hal_debug_noop
#define	vxge_hal_info_log_dmq  vxge_hal_debug_noop
#define	vxge_hal_trace_log_dmq vxge_hal_debug_noop
#endif

#if (VXGE_COMPONENT_HAL_UMQ & VXGE_DEBUG_MODULE_MASK)
#define	vxge_hal_err_log_umq						\
	if (D_ERR_MASK & VXGE_COMPONENT_HAL_UMQ) vxge_hal_debug_printf
#define	vxge_hal_info_log_umq						\
	if (D_INFO_MASK & VXGE_COMPONENT_HAL_UMQ) vxge_hal_debug_printf
#define	vxge_hal_trace_log_umq						\
	if (D_TRACE_MASK & VXGE_COMPONENT_HAL_UMQ) vxge_hal_debug_printf
#else
#define	vxge_hal_err_log_umq   vxge_hal_debug_noop
#define	vxge_hal_info_log_umq  vxge_hal_debug_noop
#define	vxge_hal_trace_log_umq vxge_hal_debug_noop
#endif

#if (VXGE_COMPONENT_HAL_SQ & VXGE_DEBUG_MODULE_MASK)
#define	vxge_hal_err_log_sq						\
	if (D_ERR_MASK & VXGE_COMPONENT_HAL_SQ) vxge_hal_debug_printf
#define	vxge_hal_info_log_sq						\
	if (D_INFO_MASK & VXGE_COMPONENT_HAL_SQ) vxge_hal_debug_printf
#define	vxge_hal_trace_log_sq						\
	if (D_TRACE_MASK & VXGE_COMPONENT_HAL_SQ) vxge_hal_debug_printf
#else
#define	vxge_hal_err_log_sq   vxge_hal_debug_noop
#define	vxge_hal_info_log_sq  vxge_hal_debug_noop
#define	vxge_hal_trace_log_sq vxge_hal_debug_noop
#endif

#if (VXGE_COMPONENT_HAL_SRQ & VXGE_DEBUG_MODULE_MASK)
#define	vxge_hal_err_log_srq						\
	if (D_ERR_MASK & VXGE_COMPONENT_HAL_SRQ) vxge_hal_debug_printf
#define	vxge_hal_info_log_srq						\
	if (D_INFO_MASK & VXGE_COMPONENT_HAL_SRQ) vxge_hal_debug_printf
#define	vxge_hal_trace_log_srq						\
	if (D_TRACE_MASK & VXGE_COMPONENT_HAL_SRQ) vxge_hal_debug_printf
#else
#define	vxge_hal_err_log_srq   vxge_hal_debug_noop
#define	vxge_hal_info_log_srq  vxge_hal_debug_noop
#define	vxge_hal_trace_log_srq vxge_hal_debug_noop
#endif

#if (VXGE_COMPONENT_HAL_CQRQ & VXGE_DEBUG_MODULE_MASK)
#define	vxge_hal_err_log_cqrq						\
	if (D_ERR_MASK & VXGE_COMPONENT_HAL_CQRQ) vxge_hal_debug_printf
#define	vxge_hal_info_log_cqrq						\
	if (D_INFO_MASK & VXGE_COMPONENT_HAL_CQRQ) vxge_hal_debug_printf
#define	vxge_hal_trace_log_cqrq						\
	if (D_TRACE_MASK & VXGE_COMPONENT_HAL_CQRQ) vxge_hal_debug_printf
#else
#define	vxge_hal_err_log_cqrq   vxge_hal_debug_noop
#define	vxge_hal_info_log_cqrq  vxge_hal_debug_noop
#define	vxge_hal_trace_log_cqrq vxge_hal_debug_noop
#endif

#if (VXGE_COMPONENT_HAL_NCE & VXGE_DEBUG_MODULE_MASK)
#define	vxge_hal_err_log_nce						\
	if (D_ERR_MASK & VXGE_COMPONENT_HAL_NCE) vxge_hal_debug_printf
#define	vxge_hal_info_log_nce						\
	if (D_INFO_MASK & VXGE_COMPONENT_HAL_NCE) vxge_hal_debug_printf
#define	vxge_hal_trace_log_nce						\
	if (D_TRACE_MASK & VXGE_COMPONENT_HAL_NCE) vxge_hal_debug_printf
#else
#define	vxge_hal_err_log_nce   vxge_hal_debug_noop
#define	vxge_hal_info_log_nce  vxge_hal_debug_noop
#define	vxge_hal_trace_log_nce vxge_hal_debug_noop
#endif

#if (VXGE_COMPONENT_HAL_STAG & VXGE_DEBUG_MODULE_MASK)
#define	vxge_hal_err_log_stag						\
	if (D_ERR_MASK & VXGE_COMPONENT_HAL_STAG) vxge_hal_debug_printf
#define	vxge_hal_info_log_stag						\
	if (D_INFO_MASK & VXGE_COMPONENT_HAL_STAG) vxge_hal_debug_printf
#define	vxge_hal_trace_log_stag						\
	if (D_TRACE_MASK & VXGE_COMPONENT_HAL_STAG) vxge_hal_debug_printf
#else
#define	vxge_hal_err_log_stag   vxge_hal_debug_noop
#define	vxge_hal_info_log_stag  vxge_hal_debug_noop
#define	vxge_hal_trace_log_stag vxge_hal_debug_noop
#endif

#if (VXGE_COMPONENT_HAL_TCP & VXGE_DEBUG_MODULE_MASK)
#define	vxge_hal_err_log_tcp						\
	if (D_ERR_MASK & VXGE_COMPONENT_HAL_TCP) vxge_hal_debug_printf
#define	vxge_hal_info_log_tcp						\
	if (D_INFO_MASK & VXGE_COMPONENT_HAL_TCP) vxge_hal_debug_printf
#define	vxge_hal_trace_log_tcp						\
	if (D_TRACE_MASK & VXGE_COMPONENT_HAL_TCP) vxge_hal_debug_printf
#else
#define	vxge_hal_err_log_tcp   vxge_hal_debug_noop
#define	vxge_hal_info_log_tcp  vxge_hal_debug_noop
#define	vxge_hal_trace_log_tcp vxge_hal_debug_noop
#endif

#if (VXGE_COMPONENT_HAL_LRO & VXGE_DEBUG_MODULE_MASK)
#define	vxge_hal_err_log_lro						\
	if (D_ERR_MASK & VXGE_COMPONENT_HAL_LRO) vxge_hal_debug_printf
#define	vxge_hal_info_log_lro						\
	if (D_INFO_MASK & VXGE_COMPONENT_HAL_LRO) vxge_hal_debug_printf
#define	vxge_hal_trace_log_lro						\
	if (D_TRACE_MASK & VXGE_COMPONENT_HAL_LRO) vxge_hal_debug_printf
#else
#define	vxge_hal_err_log_lro   vxge_hal_debug_noop
#define	vxge_hal_info_log_lro  vxge_hal_debug_noop
#define	vxge_hal_trace_log_lro vxge_hal_debug_noop
#endif

#if (VXGE_COMPONENT_HAL_SPDM & VXGE_DEBUG_MODULE_MASK)
#define	vxge_hal_err_log_spdm						\
	if (D_ERR_MASK & VXGE_COMPONENT_HAL_SPDM) vxge_hal_debug_printf
#define	vxge_hal_info_log_spdm						\
	if (D_INFO_MASK & VXGE_COMPONENT_HAL_SPDM) vxge_hal_debug_printf
#define	vxge_hal_trace_log_spdm						\
	if (D_TRACE_MASK & VXGE_COMPONENT_HAL_SPDM) vxge_hal_debug_printf
#else
#define	vxge_hal_err_log_spdm   vxge_hal_debug_noop
#define	vxge_hal_info_log_spdm  vxge_hal_debug_noop
#define	vxge_hal_trace_log_spdm vxge_hal_debug_noop
#endif

#if (VXGE_COMPONENT_HAL_SESSION & VXGE_DEBUG_MODULE_MASK)
#define	vxge_hal_err_log_session					\
	if (D_ERR_MASK & VXGE_COMPONENT_HAL_SESSION) vxge_hal_debug_printf
#define	vxge_hal_info_log_session					\
	if (D_INFO_MASK & VXGE_COMPONENT_HAL_SESSION) vxge_hal_debug_printf
#define	vxge_hal_trace_log_session					\
	if (D_TRACE_MASK & VXGE_COMPONENT_HAL_SESSION) vxge_hal_debug_printf
#else
#define	vxge_hal_err_log_session   vxge_hal_debug_noop
#define	vxge_hal_info_log_session  vxge_hal_debug_noop
#define	vxge_hal_trace_log_session vxge_hal_debug_noop
#endif

#if (VXGE_COMPONENT_HAL_STATS & VXGE_DEBUG_MODULE_MASK)
#define	vxge_hal_err_log_stats						\
	if (D_ERR_MASK & VXGE_COMPONENT_HAL_STATS) vxge_hal_debug_printf
#define	vxge_hal_info_log_stats						\
	if (D_INFO_MASK & VXGE_COMPONENT_HAL_STATS) vxge_hal_debug_printf
#define	vxge_hal_trace_log_stats					\
	if (D_TRACE_MASK & VXGE_COMPONENT_HAL_STATS) vxge_hal_debug_printf
#else
#define	vxge_hal_err_log_stats   vxge_hal_debug_noop
#define	vxge_hal_info_log_stats  vxge_hal_debug_noop
#define	vxge_hal_trace_log_stats vxge_hal_debug_noop
#endif

#if (VXGE_COMPONENT_HAL_MRPCIM & VXGE_DEBUG_MODULE_MASK)
#define	vxge_hal_err_log_mrpcim						\
	if (D_ERR_MASK & VXGE_COMPONENT_HAL_MRPCIM) vxge_hal_debug_printf
#define	vxge_hal_info_log_mrpcim					\
	if (D_INFO_MASK & VXGE_COMPONENT_HAL_MRPCIM) vxge_hal_debug_printf
#define	vxge_hal_trace_log_mrpcim					\
	if (D_TRACE_MASK & VXGE_COMPONENT_HAL_MRPCIM) vxge_hal_debug_printf
#else
#define	vxge_hal_err_log_mrpcim   vxge_hal_debug_noop
#define	vxge_hal_info_log_mrpcim  vxge_hal_debug_noop
#define	vxge_hal_trace_log_mrpcim vxge_hal_debug_noop
#endif

#if (VXGE_COMPONENT_HAL_MRPCIM_IRQ & VXGE_DEBUG_MODULE_MASK)
#define	vxge_hal_err_log_mrpcim_irq					\
	if (D_ERR_MASK & VXGE_COMPONENT_HAL_MRPCIM_IRQ) vxge_hal_debug_printf
#define	vxge_hal_info_log_mrpcim_irq					\
	if (D_INFO_MASK & VXGE_COMPONENT_HAL_MRPCIM_IRQ) vxge_hal_debug_printf
#define	vxge_hal_trace_log_mrpcim_irq					\
	if (D_TRACE_MASK & VXGE_COMPONENT_HAL_MRPCIM_IRQ) vxge_hal_debug_printf
#else
#define	vxge_hal_err_log_mrpcim_irq   vxge_hal_debug_noop
#define	vxge_hal_info_log_mrpcim_irq  vxge_hal_debug_noop
#define	vxge_hal_trace_log_mrpcim_irq vxge_hal_debug_noop
#endif

#if (VXGE_COMPONENT_HAL_SRPCIM & VXGE_DEBUG_MODULE_MASK)
#define	vxge_hal_err_log_srpcim						\
	if (D_ERR_MASK & VXGE_COMPONENT_HAL_SRPCIM) vxge_hal_debug_printf
#define	vxge_hal_info_log_srpcim					\
	if (D_INFO_MASK & VXGE_COMPONENT_HAL_SRPCIM) vxge_hal_debug_printf
#define	vxge_hal_trace_log_srpcim					\
	if (D_TRACE_MASK & VXGE_COMPONENT_HAL_SRPCIM) vxge_hal_debug_printf
#else
#define	vxge_hal_err_log_srpcim   vxge_hal_debug_noop
#define	vxge_hal_info_log_srpcim  vxge_hal_debug_noop
#define	vxge_hal_trace_log_srpcim vxge_hal_debug_noop
#endif

#if (VXGE_COMPONENT_HAL_SRPCIM_IRQ & VXGE_DEBUG_MODULE_MASK)
#define	vxge_hal_err_log_srpcim_irq					\
	if (D_ERR_MASK & VXGE_COMPONENT_HAL_SRPCIM_IRQ) vxge_hal_debug_printf
#define	vxge_hal_info_log_srpcim_irq					\
	if (D_INFO_MASK & VXGE_COMPONENT_HAL_SRPCIM_IRQ) vxge_hal_debug_printf
#define	vxge_hal_trace_log_srpcim_irq					\
	if (D_TRACE_MASK & VXGE_COMPONENT_HAL_SRPCIM_IRQ) vxge_hal_debug_printf
#else
#define	vxge_hal_err_log_srpcim_irq   vxge_hal_debug_noop
#define	vxge_hal_info_log_srpcim_irq  vxge_hal_debug_noop
#define	vxge_hal_trace_log_srpcim_irq vxge_hal_debug_noop
#endif

__EXTERN_END_DECLS

#endif /* VXGEHAL_DEBUG_H */
