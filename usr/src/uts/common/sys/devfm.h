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
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_SYS_DEVFM_H
#define	_SYS_DEVFM_H

#include <sys/types.h>
#include <sys/nvpair.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	FM_IOC_MAXBUFSZ		32768
#define	FM_IOC_OUT_BUFSZ	32768
#define	FM_IOC_OUT_MAXBUFSZ	524288
#define	FM_DRV_VERSION		1

#define	FM_VERSIONS_VERSION	"fm-versions-version"
#define	FM_PAGE_OP_VERSION	"page-operation-version"
#define	FM_CPU_OP_VERSION	"cpu-operation-version"
#define	FM_CPU_INFO_VERSION	"cpu-info-version"
#define	FM_TOPO_LEGACY_VERSION	"topo-legacy-version"
#define	FM_CACHE_OP_VERSION	"cache-operation-version"

/*
 * FMA driver ioctl interfaces
 */
#define	FM_IOC			(0xFA << 16)
#define	FM_IOC_VERSIONS		(FM_IOC | 1)
#define	FM_IOC_PAGE_RETIRE	(FM_IOC | 2)
#define	FM_IOC_PAGE_STATUS	(FM_IOC | 3)
#define	FM_IOC_PAGE_UNRETIRE	(FM_IOC | 4)

#if defined(__x86)
#define	FM_IOC_PHYSCPU_INFO	(FM_IOC | 5)
#define	FM_IOC_CPU_RETIRE	(FM_IOC | 6)
#define	FM_IOC_CPU_STATUS	(FM_IOC | 7)
#define	FM_IOC_CPU_UNRETIRE	(FM_IOC | 8)
#define	FM_IOC_GENTOPO_LEGACY	(FM_IOC | 9)
#endif	/* __x86 */

#if defined(__sparc)

#define	FM_IOC_CACHE_RETIRE	(FM_IOC | 10)
#define	FM_IOC_CACHE_STATUS	(FM_IOC | 11)
#define	FM_IOC_CACHE_UNRETIRE	(FM_IOC | 12)

/* cache type */
#define	FM_CLR_DATA_CACHE		1
#define	FM_CLR_INSTR_CACHE		2
#define	FM_CLR_UNIFIED_CACHE		3

/* cache level */
#define	FM_CLR_L1_CACHE			1
#define	FM_CLR_L2_CACHE			2
#define	FM_CLR_L3_CACHE			3

/* cache line status */
#define	FM_CLR_STATUS_QUERY		0
#define	FM_CLR_STATUS_PENDING		1
#define	FM_CLR_STATUS_RETIRED		2
#define	FM_CLR_STATUS_ACTIVE		3
#define	FM_CLR_STATUS_RESERVED		4

#endif	/* __sparc */

/*
 * Types
 */
typedef struct fm_ioc_data {
	uint32_t	fid_version;	/* interface version */
	size_t		fid_insz;	/* size of packed input nvlist */
	caddr_t		fid_inbuf;	/* buf containing packed input nvl */
	size_t		fid_outsz;	/* size of packed output nvlist */
	caddr_t		fid_outbuf;	/* buf containing packed output nvl */
} fm_ioc_data_t;

#ifdef _KERNEL

typedef struct fm_ioc_data32 {
	uint32_t	fid_version;	/* interface version */
	size32_t	fid_insz;	/* size of packed input nvlist */
	caddr32_t	fid_inbuf;	/* buf containing packed input nvl */
	size32_t	fid_outsz;	/* size of packed output nvlist */
	caddr32_t	fid_outbuf;	/* buf containing packed output nvl */
} fm_ioc_data32_t;

/* debug flags */
#define	FM_DBG_ALL			0xffffffff
#define	FM_DBG_CLR			1

extern unsigned int fm_debug_flags;
extern void fm_debug(unsigned int, const char *, ...);

#endif	/* _KERNEL */

/*
 * Constants
 */
#define	FM_PAGE_RETIRE_FMRI		"fmri"
#define	FM_PHYSCPU_INFO_CPUS		"cpus"
#define	FM_CPU_RETIRE_CHIP_ID		"chip_id"
#define	FM_PHYSCPU_INFO_NPROCNODES	"procnodes_per_pkg"
#define	FM_PHYSCPU_INFO_PROCNODE_ID	"procnodeid"
#define	FM_CPU_RETIRE_CORE_ID		"core_id"
#define	FM_CPU_RETIRE_STRAND_ID		"strand_id"
#define	FM_CPU_RETIRE_OLDSTATUS		"oldstatus"
#define	FM_GENTOPO_LEGACY		"gentopolegacy"

/*
 * Properties set by FM_PHYSCPU_INFO
 */
#define	FM_PHYSCPU_INFO_VENDOR_ID	"vendor_id"
#define	FM_PHYSCPU_INFO_FAMILY		"family"
#define	FM_PHYSCPU_INFO_MODEL		"model"
#define	FM_PHYSCPU_INFO_STEPPING	"stepping"

/*
 * When Multi-Chip-Module(MCM) support is added
 * chip_id should map to the processor package
 * and not the die in the processor package.
 * This is for FMA; kernel's perception of
 * chip_id could differ for MCM.
 */
#define	FM_PHYSCPU_INFO_CHIP_ID		"chip_id"

#define	FM_PHYSCPU_INFO_CORE_ID		"core_id"
#define	FM_PHYSCPU_INFO_STRAND_ID	"strand_id"
#define	FM_PHYSCPU_INFO_STRAND_APICID	"strand_initial_apicid"
#define	FM_PHYSCPU_INFO_SMBIOS_ID	"smbios_id"
#define	FM_PHYSCPU_INFO_CHIP_ROOTS	"chip_roots"
#define	FM_PHYSCPU_INFO_CHIP_REV	"chip_rev"
#define	FM_PHYSCPU_INFO_SOCKET_TYPE	"socket_type"
#define	FM_PHYSCPU_INFO_CPU_ID		"cpuid"

#define	FM_CLR_STRAND_ID		"strand_id"
#define	FM_CLR_CACHE_LEVEL		"cache_level"
#define	FM_CLR_CACHE_TYPE		"cache_type"
#define	FM_CLR_CACHE_INDEX		"cache_index"
#define	FM_CLR_CACHE_WAY		"cache_way"
#define	FM_CLR_STATUS			"cache_status"

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DEVFM_H */
