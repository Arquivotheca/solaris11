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
 * FileName :    vxge-os-debug.h
 *
 * Description:  debug facilities for ULD
 *
 * Created:      27 December 2006
 */

#ifndef VXGE_OS_DEBUG_H
#define	VXGE_OS_DEBUG_H

__EXTERN_BEGIN_DECLS

#ifndef VXGE_DEBUG_INLINE_FUNCTIONS

#ifdef VXGE_TRACE_INTO_CIRCULAR_ARR
#define	vxge_trace_aux(hldev, vpid, fmt, ...)				\
		vxge_os_vasprintf(hldev, vpid, fmt, __VA_ARGS__)
#else
#define	vxge_trace_aux(hldev, vpid, fmt, ...)				\
		vxge_os_vaprintf(hldev, vpid, fmt, __VA_ARGS__)
#endif

#define	vxge_debug(module, level, hldev, vpid, fmt, ...)		\
{									\
	if (((u32)level <=						\
		((vxge_hal_device_t *)hldev)->debug_level) &&		\
	    ((u32)module &						\
		((vxge_hal_device_t *)hldev)->debug_module_mask))	\
			vxge_trace_aux((vxge_hal_device_h)hldev,	\
					vpid, fmt, __VA_ARGS__);	\
}

/*
 * vxge_debug_driver
 * @level: level of debug verbosity.
 * @hldev: HAL Device
 * @vpid: Vpath id
 * @fmt: printf like format string
 *
 * Provides logging facilities for OS Dependent functions. Can be customized
 * with debug levels. Input parameters, except level, are the same
 * as posix printf. This function may be compiled out if DEBUG macro
 * was never defined.
 * See also: vxge_debug_level_e{}.
 */
#if (VXGE_COMPONENT_HAL_DRIVER & VXGE_DEBUG_MODULE_MASK)
#define	vxge_debug_driver(level, hldev, vpid, fmt, ...)		    \
	if ((u32)level <= g_debug_level)			    \
		vxge_os_vaprintf((vxge_hal_device_h)hldev,	    \
				vpid, fmt, __VA_ARGS__);
#else
#define	vxge_debug_driver(level, hldev, vpid, fmt, ...)
#endif

/*
 * vxge_debug_osdep
 * @level: level of debug verbosity.
 * @hldev: HAL Device
 * @vpid: Vpath id
 * @fmt: printf like format string
 *
 * Provides logging facilities for OS Dependent functions. Can be customized
 * with debug levels. Input parameters, except level, are the same
 * as posix printf. This function may be compiled out if DEBUG macro
 * was never defined.
 * See also: vxge_debug_level_e{}.
 */
#if (VXGE_COMPONENT_OSDEP & VXGE_DEBUG_MODULE_MASK)
#define	vxge_debug_osdep(level, hldev, vpid, fmt, ...) \
	vxge_debug(VXGE_COMPONENT_OSDEP, level, hldev, vpid, fmt, __VA_ARGS__)
#else
#define	vxge_debug_osdep(level, hldev, vpid, fmt, ...)
#endif

/*
 * vxge_debug_ll
 * @level: level of debug verbosity.
 * @hldev: HAL Device
 * @vpid: Vpath id
 * @fmt: printf like format string
 *
 * Provides logging facilities for LL driver. Can be customized
 * with debug levels. Input parameters, except level, are the same
 * as posix printf. This function may be compiled out if DEBUG macro
 * was never defined.
 * See also: vxge_debug_level_e{}.
 */
#if (VXGE_COMPONENT_LL & VXGE_DEBUG_MODULE_MASK)
#define	vxge_debug_ll(level, hldev, vpid, fmt, ...) \
	vxge_debug(VXGE_COMPONENT_LL, level, hldev, vpid, fmt, __VA_ARGS__)
#else
#define	vxge_debug_ll(level, hldev, vpid, fmt, ...)
#endif

/*
 * vxge_debug_uld
 * @component: The Component mask
 * @level: level of debug verbosity.
 * @hldev: HAL Device
 * @vpid: Vpath id
 * @fmt: printf like format string
 *
 * Provides logging facilities for LL driver. Can be customized
 * with debug levels. Input parameters, except level, are the same
 * as posix printf. This function may be compiled out if DEBUG macro
 * was never defined.
 * See also: vxge_debug_level_e{}.
 */
#if (VXGE_COMPONENT_ULD & VXGE_DEBUG_MODULE_MASK)
#define	vxge_debug_uld(component, level, hldev, vpid, fmt, ...) \
	vxge_debug(component, level, hldev, vpid, fmt, __VA_ARGS__)
#else
#define	vxge_debug_uld(level, hldev, vpid, fmt, ...)
#endif

#else  // #ifndef VXGE_DEBUG_INLINE_FUNCTIONS

#ifdef VXGE_TRACE_INTO_CIRCULAR_ARR
#define	vxge_trace_aux(hldev, vpid, fmt)				\
		vxge_os_vasprintf(hldev, vpid, fmt)
#else
#define	vxge_trace_aux(hldev, vpid, fmt)				\
		vxge_os_vaprintf(hldev, vpid, fmt)
#endif

#define	vxge_debug(module, level, hldev, vpid, fmt)			    \
{									    \
	if (((u32)level <= ((vxge_hal_device_t *)hldev)->debug_level) &&    \
	    ((u32)module & ((vxge_hal_device_t *)hldev)->debug_module_mask))\
		vxge_trace_aux((vxge_hal_device_h)hldev, vpid, fmt);	    \
}

/*
 * vxge_debug_driver
 * @level: level of debug verbosity.
 * @hldev: HAL Device
 * @vpid: Vpath id
 * @fmt: printf like format string
 *
 * Provides logging facilities for OS Dependent functions. Can be customized
 * with debug levels. Input parameters, except level, are the same
 * as posix printf. This function may be compiled out if DEBUG macro
 * was never defined.
 * See also: vxge_debug_level_e{}.
 */
static inline void vxge_debug_driver(
			vxge_debug_level_e level,
			vxge_hal_device_h hldev,
			u32 vpid,
			char *fmt, ...)
{
#if (VXGE_COMPONENT_HAL_DRIVER & VXGE_DEBUG_MODULE_MASK)
	if ((u32)level <= g_debug_level)
		vxge_os_vaprintf((vxge_hal_device_h)hldev, vpid, fmt);
#endif
}

/*
 * vxge_debug_osdep
 * @level: level of debug verbosity.
 * @hldev: HAL Device
 * @vpid: Vpath id
 * @fmt: printf like format string
 *
 * Provides logging facilities for OS Dependent functions. Can be customized
 * with debug levels. Input parameters, except level, are the same
 * as posix printf. This function may be compiled out if DEBUG macro
 * was never defined.
 * See also: vxge_debug_level_e{}.
 */
static inline void vxge_debug_osdep(
			vxge_debug_level_e level,
			vxge_hal_device_h hldev,
			u32 vpid,
			char *fmt, ...)
{
#if (VXGE_COMPONENT_OSDEP & VXGE_DEBUG_MODULE_MASK)
	vxge_debug(VXGE_COMPONENT_OSDEP, level, hldev, vpid, fmt)
#endif
}

/*
 * vxge_debug_ll
 * @level: level of debug verbosity.
 * @hldev: HAL Device
 * @vpid: Vpath id
 * @fmt: printf like format string
 *
 * Provides logging facilities for LL driver. Can be customized
 * with debug levels. Input parameters, except level, are the same
 * as posix printf. This function may be compiled out if DEBUG macro
 * was never defined.
 * See also: vxge_debug_level_e{}.
 */
static inline void vxge_debug_ll(
			vxge_debug_level_e level,
			vxge_hal_device_h hldev,
			u32 vpid,
			char *fmt, ...)
{
#if (VXGE_COMPONENT_LL & VXGE_DEBUG_MODULE_MASK)
	vxge_debug(VXGE_COMPONENT_LL, level, hldev, vpid, fmt)
#endif
}

/*
 * vxge_debug_uld
 * @component: The Component mask
 * @level: level of debug verbosity.
 * @hldev: HAL Device
 * @vpid: Vpath id
 * @fmt: printf like format string
 *
 * Provides logging facilities for LL driver. Can be customized
 * with debug levels. Input parameters, except level, are the same
 * as posix printf. This function may be compiled out if DEBUG macro
 * was never defined.
 * See also: vxge_debug_level_e{}.
 */
static inline void vxge_debug_uld(
			u32 component,
			vxge_debug_level_e level,
			vxge_hal_device_h hldev,
			u32 vpid,
			char *fmt, ...)
{
#if (VXGE_COMPONENT_ULD & VXGE_DEBUG_MODULE_MASK)
	vxge_debug(component, level, hldev, vpid, fmt)
#endif
}

#endif /* end of VXGE_DEBUG_INLINE_FUNCTIONS */

__EXTERN_END_DECLS

#endif /* VXGE_OS_DEBUG_H */
