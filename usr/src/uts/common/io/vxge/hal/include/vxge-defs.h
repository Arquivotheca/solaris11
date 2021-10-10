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
 * FileName :   vxge-defs.h
 *
 * Description:  global definitions
 *
 * Created:       27 December 2006
 */

#ifndef	VXGE_DEFS_H
#define	VXGE_DEFS_H

#define	VXGE_PCI_VENDOR_ID				0x17D5
#define	VXGE_PCI_DEVICE_ID_TITAN_1			0x5833
#define	VXGE_PCI_REVISION_TITAN_1			1
#define	VXGE_PCI_DEVICE_ID_TITAN_1A			0x5833
#define	VXGE_PCI_REVISION_TITAN_1A			2
#define	VXGE_PCI_DEVICE_ID_TITAN_2			0x5834
#define	VXGE_PCI_REVISION_TITAN_2			1

#define	VXGE_MIN_SP_FW_MAJOR_VERSION			1
#define	VXGE_MIN_SP_FW_MINOR_VERSION			4
#define	VXGE_MIN_SP_FW_BUILD_NUMBER			4

#define	VXGE_MIN_DP_FW_MAJOR_VERSION			1
#define	VXGE_MIN_DP_FW_MINOR_VERSION			5
#define	VXGE_MIN_DP_FW_BUILD_NUMBER			1

#define	VXGE_DRIVER_NAME			    \
		"Neterion X3100/3200 Server Adapter Driver"
#define	VXGE_DRIVER_VENDOR			    "Neterion, Inc"
#define	VXGE_CHIP_FAMILY			    "X3100"
#define	VXGE_SUPPORTED_MEDIA_0			    "Fiber"

/*
 * mBIT(loc) - set bit at offset
 */
#define	mBIT(loc)		(0x8000000000000000ULL >> (loc))

/*
 * vBIT(val, loc, sz) - set bits at offset
 */
#define	vBIT(val, loc, sz)	(((u64)(val)) << (64-(loc)-(sz)))
#define	vBIT32(val, loc, sz)	(((u32)(val)) << (32-(loc)-(sz)))

/*
 * vxge_bVALn(bits, loc, n) - Get the value of n bits at location
 */
#define	vxge_bVALn(bits, loc, n) \
	((((u64)bits) >> (64-(loc+n))) & ((0x1ULL << n) - 1))

/*
 * bVALx(bits, loc) - Get the value of x bits at location
 */
#define	bVAL1(bits, loc)  ((((u64)bits) >> (64-(loc+1))) & 0x1)
#define	bVAL2(bits, loc)  ((((u64)bits) >> (64-(loc+2))) & 0x3)
#define	bVAL3(bits, loc)  ((((u64)bits) >> (64-(loc+3))) & 0x7)
#define	bVAL4(bits, loc)  ((((u64)bits) >> (64-(loc+4))) & 0xF)
#define	bVAL5(bits, loc)  ((((u64)bits) >> (64-(loc+5))) & 0x1F)
#define	bVAL6(bits, loc)  ((((u64)bits) >> (64-(loc+6))) & 0x3F)
#define	bVAL7(bits, loc)  ((((u64)bits) >> (64-(loc+7))) & 0x7F)
#define	bVAL8(bits, loc)  ((((u64)bits) >> (64-(loc+8))) & 0xFF)
#define	bVAL9(bits, loc)  ((((u64)bits) >> (64-(loc+9))) & 0x1FF)
#define	bVAL11(bits, loc) ((((u64)bits) >> (64-(loc+11))) & 0x7FF)
#define	bVAL12(bits, loc) ((((u64)bits) >> (64-(loc+12))) & 0xFFF)
#define	bVAL14(bits, loc) ((((u64)bits) >> (64-(loc+14))) & 0x3FFF)
#define	bVAL15(bits, loc) ((((u64)bits) >> (64-(loc+15))) & 0x7FFF)
#define	bVAL16(bits, loc) ((((u64)bits) >> (64-(loc+16))) & 0xFFFF)
#define	bVAL17(bits, loc) ((((u64)bits) >> (64-(loc+17))) & 0x1FFFF)
#define	bVAL18(bits, loc) ((((u64)bits) >> (64-(loc+18))) & 0x3FFFF)
#define	bVAL20(bits, loc) ((((u64)bits) >> (64-(loc+20))) & 0xFFFFF)
#define	bVAL22(bits, loc) ((((u64)bits) >> (64-(loc+22))) & 0x3FFFFF)
#define	bVAL24(bits, loc) ((((u64)bits) >> (64-(loc+24))) & 0xFFFFFF)
#define	bVAL28(bits, loc) ((((u64)bits) >> (64-(loc+28))) & 0xFFFFFFF)
#define	bVAL32(bits, loc) ((((u64)bits) >> (64-(loc+32))) & 0xFFFFFFFF)
#define	bVAL36(bits, loc) ((((u64)bits) >> (64-(loc+36))) & 0xFFFFFFFFFULL)
#define	bVAL40(bits, loc) ((((u64)bits) >> (64-(loc+40))) & 0xFFFFFFFFFFULL)
#define	bVAL44(bits, loc) ((((u64)bits) >> (64-(loc+44))) & 0xFFFFFFFFFFFULL)
#define	bVAL48(bits, loc) ((((u64)bits) >> (64-(loc+48))) & 0xFFFFFFFFFFFFULL)
#define	bVAL52(bits, loc) ((((u64)bits) >> (64-(loc+52))) & 0xFFFFFFFFFFFFFULL)
#define	bVAL56(bits, loc) ((((u64)bits) >> (64-(loc+56))) & 0xFFFFFFFFFFFFFFULL)
#define	bVAL60(bits, loc)   \
		((((u64)bits) >> (64-(loc+60))) & 0xFFFFFFFFFFFFFFFULL)
#define	bVAL61(bits, loc)   \
		((((u64)bits) >> (64-(loc+61))) & 0x1FFFFFFFFFFFFFFFULL)

#define	VXGE_HAL_ALL_FOXES		0xFFFFFFFFFFFFFFFFULL

#define	VXGE_HAL_INTR_MASK_ALL		0xFFFFFFFFFFFFFFFFULL

#define	VXGE_HAL_MAX_VIRTUAL_PATHS	17

#define	VXGE_HAL_MAX_FUNCTIONS		8

#define	VXGE_HAL_MAX_ITABLE_ENTRIES	256

#define	VXGE_HAL_MAX_RSS_KEY_SIZE	40

#define	VXGE_HAL_MAC_MAX_WIRE_PORTS	2

#define	VXGE_HAL_MAC_SWITCH_PORT	2

#define	VXGE_HAL_MAC_MAX_AGGR_PORTS	2

#define	VXGE_HAL_MAC_MAX_PORTS		3

#define	VXGE_HAL_INTR_ALARM		(1<<4)

#define	VXGE_HAL_INTR_TX		(1<<(3-VXGE_HAL_VPATH_INTR_TX))

#define	VXGE_HAL_INTR_RX		(1<<(3-VXGE_HAL_VPATH_INTR_RX))

#define	VXGE_HAL_INTR_EINTA		(1<<(3-VXGE_HAL_VPATH_INTR_EINTA))

#define	VXGE_HAL_INTR_BMAP		(1<<(3-VXGE_HAL_VPATH_INTR_BMAP))

#define	VXGE_HAL_PCI_CONFIG_SPACE_SIZE	VXGE_OS_PCI_CONFIG_SIZE

#define	VXGE_HAL_DEFAULT_32		0xffffffff

#define	VXGE_HAL_DEFAULT_64		0xffffffffffffffff

#define	VXGE_HAL_DUMP_BUF_SIZE		0x10000

#define	VXGE_HAL_VPD_BUFFER_SIZE	128

#define	VXGE_HAL_VPD_LENGTH		80


/* frames sizes */
#define	VXGE_HAL_HEADER_ETHERNET_II_802_3_SIZE		14
#define	VXGE_HAL_HEADER_802_2_SIZE			3
#define	VXGE_HAL_HEADER_SNAP_SIZE			5
#define	VXGE_HAL_HEADER_VLAN_SIZE			4
#define	VXGE_HAL_MAC_HEADER_MAX_SIZE \
			(VXGE_HAL_HEADER_ETHERNET_II_802_3_SIZE + \
			VXGE_HAL_HEADER_802_2_SIZE + \
			VXGE_HAL_HEADER_SNAP_SIZE)

#define	VXGE_HAL_TCPIP_HEADER_MAX_SIZE			(64 + 64)

/* 32bit alignments */
#define	VXGE_HAL_HEADER_ETHERNET_II_802_3_ALIGN		2
#define	VXGE_HAL_HEADER_802_2_SNAP_ALIGN		2
#define	VXGE_HAL_HEADER_802_2_ALIGN			3
#define	VXGE_HAL_HEADER_SNAP_ALIGN			1

#define	VXGE_HAL_MIN_MTU				46
#define	VXGE_HAL_MAX_MTU				9600
#define	VXGE_HAL_DEFAULT_MTU				1500

#define	VXGE_HAL_SEGEMENT_OFFLOAD_MAX_SIZE		81920

#if defined(__EXTERN_BEGIN_DECLS)
#undef __EXTERN_BEGIN_DECLS
#endif

#if defined(__EXTERN_END_DECLS)
#undef __EXTERN_END_DECLS
#endif

#if defined(__cplusplus)
#define	__EXTERN_BEGIN_DECLS		extern "C" {
#define	__EXTERN_END_DECLS			}
#else
#define	__EXTERN_BEGIN_DECLS
#define	__EXTERN_END_DECLS
#endif

__EXTERN_BEGIN_DECLS

/* ---------------------------- DMA attributes ------------------------------ */
/*	  Used in vxge_os_dma_malloc() and vxge_os_dma_map() */
/* ---------------------------- DMA attributes ------------------------------ */

/*
 * VXGE_OS_DMA_REQUIRES_SYNC  - should be defined or
 * NOT defined in the Makefile
 */
#define	VXGE_OS_DMA_CACHELINE_ALIGNED			0x1

/*
 * Either STREAMING or CONSISTENT should be used.
 * The combination of both or none is invalid
 */
#define	VXGE_OS_DMA_STREAMING				0x2
#define	VXGE_OS_DMA_CONSISTENT				0x4
#define	VXGE_OS_SPRINTF_STRLEN				64

/* ---------------------------- common stuffs ------------------------------- */
#ifndef	VXGE_OS_LLXFMT
#define	VXGE_OS_LLXFMT				    "%llx"
#endif

#ifndef	VXGE_OS_LLDFMT
#define	VXGE_OS_LLDFMT				    "%lld"
#endif

#ifndef	VXGE_OS_STXFMT
#define	VXGE_OS_STXFMT				    "%zx"
#endif

#ifndef	VXGE_OS_STDFMT
#define	VXGE_OS_STDFMT				    "%zd"
#endif

__EXTERN_END_DECLS

#endif /* VXGE_DEFS_H */
