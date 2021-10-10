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
 * FileName :   vxge-debug.h
 *
 * Description:  debug facilities
 *
 * Created:       27 December 2006
 */

#ifndef	VXGE_DEBUG_H
#define	VXGE_DEBUG_H

__EXTERN_BEGIN_DECLS

/*
 * __FUNCTION__ is, together with __PRETTY_FUNCTION__ or something similar,
 * a gcc extension. we'll have to #ifdef	around that, and provide some
 * meaningful replacement for those, so to make some gcc versions happier
 */
#ifndef	__func__
#ifdef	__FUNCTION__
#define	__func__ __FUNCTION__
#else
#define	__func__ " "
#endif
#endif

#define	NULL_HLDEV					NULL
#define	NULL_VPID					0xFFFFFFFF

#define	VXGE_DEBUG_MODULE_MASK_DEF			0xFFFFFFFF
#define	VXGE_DEBUG_LEVEL_DEF				VXGE_TRACE

extern u32 g_debug_level;

#ifndef	VXGE_DEBUG_MODULE_MASK
#define	VXGE_DEBUG_MODULE_MASK				0
#endif

/*
 * enum vxge_debug_level_e
 * @VXGE_NONE: debug disabled
 * @VXGE_ERR: all errors going to be logged out
 * @VXGE_INFO: all errors plus all kind of info tracing print outs
 *		going to be logged out. noisy.
 * @VXGE_TRACE: all errors, all info plus all function entry and exit
 *		and parameters. Very noisy
 *
 * This enumeration going to be used to switch between different
 * debug levels during runtime if DEBUG macro defined during
 * compilation. If DEBUG macro not defined than code will be
 * compiled out.
 */
typedef enum vxge_debug_level_e {
	VXGE_NONE   = 0x0,
	VXGE_ERR	= 0x1,
	VXGE_INFO   = 0x2,
	VXGE_TRACE  = 0x4,
} vxge_debug_level_e;

/*
 * @VXGE_COMPONENT_HAL_DEVICE: do debug for vxge core device module
 * @VXGE_COMPONENT_HAL_DEVICE_IRQ: do debug for vxge core device module in ISR
 * @VXGE_COMPONENT_HAL_VAPTH: do debug for vxge core virtual path module
 * @VXGE_COMPONENT_HAL_VAPTH_ISR: do debug for vxge core virtual path module in
 *		ISR
 * @VXGE_COMPONENT_HAL_CONFIG: do debug for vxge core config module
 * @VXGE_COMPONENT_HAL_MM: do debug for vxge core memory module
 * @VXGE_COMPONENT_HAL_POOL: do debug for vxge core memory pool module
 * @VXGE_COMPONENT_HAL_QUEUE: do debug for vxge core queue module
 * @VXGE_COMPONENT_HAL_BITMAP: do debug for vxge core BITMAP module
 * @VXGE_COMPONENT_HAL_CHANNEL: do debug for vxge core channel module
 * @VXGE_COMPONENT_HAL_FIFO: do debug for vxge core fifo module
 * @VXGE_COMPONENT_HAL_RING: do debug for vxge core ring module
 * @VXGE_COMPONENT_HAL_DMQ: do debug for vxge core DMQ module
 * @VXGE_COMPONENT_HAL_UMQ: do debug for vxge core UMQ module
 * @VXGE_COMPONENT_HAL_SQ: do debug for vxge core SQ module
 * @VXGE_COMPONENT_HAL_SRQ: do debug for vxge core SRQ module
 * @VXGE_COMPONENT_HAL_CQRQ: do debug for vxge core CRQ module
 * @VXGE_COMPONENT_HAL_NCE: do debug for vxge core NCE module
 * @VXGE_COMPONENT_HAL_STAG: do debug for vxge core STAG module
 * @VXGE_COMPONENT_HAL_TCP: do debug for vxge core TCP module
 * @VXGE_COMPONENT_HAL_LRO: do debug for vxge core LRO module
 * @VXGE_COMPONENT_HAL_SPDM: do debug for vxge core SPDM module
 * @VXGE_COMPONENT_HAL_SESSION: do debug for vxge core SESSION module
 * @VXGE_COMPONENT_HAL_STATS: do debug for vxge core statistics module
 * @VXGE_COMPONENT_HAL_MRPCIM: do debug for vxge KMA core mrpcim module
 * @VXGE_COMPONENT_HAL_SRPCIM: do debug for vxge KMA core srpcim module
 * @VXGE_COMPONENT_OSDEP: do debug for vxge KMA os dependent parts
 * @VXGE_COMPONENT_LL: do debug for vxge link layer module
 * @VXGE_COMPONENT_ULD: do debug for vxge upper layer driver
 * @VXGE_COMPONENT_ALL: activate debug for all modules with no exceptions
 *
 * This enumeration going to be used to distinguish modules
 * or libraries during compilation and runtime.  Makefile must declare
 * VXGE_DEBUG_MODULE_MASK macro and set it to proper value.
 */
#define	VXGE_COMPONENT_HAL_DEVICE			0x00000001
#define	VXGE_COMPONENT_HAL_DEVICE_IRQ			0x00000002
#define	VXGE_COMPONENT_HAL_VPATH			0x00000004
#define	VXGE_COMPONENT_HAL_VPATH_IRQ			0x00000008
#define	VXGE_COMPONENT_HAL_CONFIG			0x00000010
#define	VXGE_COMPONENT_HAL_MM				0x00000020
#define	VXGE_COMPONENT_HAL_POOL				0x00000040
#define	VXGE_COMPONENT_HAL_QUEUE			0x00000080
#define	VXGE_COMPONENT_HAL_BITMAP			0x00000100
#define	VXGE_COMPONENT_HAL_CHANNEL			0x00000200
#define	VXGE_COMPONENT_HAL_FIFO				0x00000400
#define	VXGE_COMPONENT_HAL_RING				0x00000800
#define	VXGE_COMPONENT_HAL_DMQ				0x00001000
#define	VXGE_COMPONENT_HAL_UMQ				0x00002000
#define	VXGE_COMPONENT_HAL_SQ				0x00004000
#define	VXGE_COMPONENT_HAL_SRQ				0x00008000
#define	VXGE_COMPONENT_HAL_CQRQ				0x00010000
#define	VXGE_COMPONENT_HAL_NCE				0x00020000
#define	VXGE_COMPONENT_HAL_STAG				0x00040000
#define	VXGE_COMPONENT_HAL_TCP				0x00080000
#define	VXGE_COMPONENT_HAL_LRO				0x00100000
#define	VXGE_COMPONENT_HAL_SPDM				0x00200000
#define	VXGE_COMPONENT_HAL_SESSION			0x00400000
#define	VXGE_COMPONENT_HAL_STATS			0x00800000
#define	VXGE_COMPONENT_HAL_MRPCIM			0x01000000
#define	VXGE_COMPONENT_HAL_MRPCIM_IRQ			0x02000000
#define	VXGE_COMPONENT_HAL_SRPCIM			0x04000000
#define	VXGE_COMPONENT_HAL_SRPCIM_IRQ			0x08000000
#define	VXGE_COMPONENT_HAL_DRIVER			0x10000000

	/* space for CORE_XXX */
#define	VXGE_COMPONENT_OSDEP				0x20000000
#define	VXGE_COMPONENT_LL				0x40000000
#define	VXGE_COMPONENT_ULD				0x80000000
#define	VXGE_COMPONENT_ALL				0xffffffff

__EXTERN_END_DECLS

#endif /* VXGE_DEBUG_H */
