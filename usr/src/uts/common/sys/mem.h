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
 * Copyright (c) 1987, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SYS_MEM_H
#define	_SYS_MEM_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <sys/thread.h>
#include <sys/uio.h>

/*
 * Memory Device Minor Numbers
 */
#define	M_MEM		0	/* /dev/mem - physical main memory */
#define	M_KMEM		1	/* /dev/kmem - virtual kernel memory */
#define	M_NULL		2	/* /dev/null - EOF & Rathole */
#define	M_ALLKMEM	3	/* /dev/allkmem - virtual kernel memory & I/O */
#define	M_ZERO		12	/* /dev/zero - source of private memory */

/*
 * Logging index numbers. These mostly mirror the device minor numbers
 * so, if they ever change these should be considered. Access to /dev/null
 * is not tracked so we split allkmem accesses and hence the departure from
 * strict minor number mirroring.
 */
#define	L_MEM		0
#define	L_KMEM		1
#define	L_ALLKMEMR	2
#define	L_ALLKMEMW	3

/*
 * Private ioctl for libkvm: translate virtual address to physical address.
 */
#define	MEM_VTOP		(('M' << 8) | 0x01)

typedef struct mem_vtop {
	struct as	*m_as;
	void		*m_va;
	pfn_t		m_pfn;
} mem_vtop_t;

#if defined(_SYSCALL32)
typedef struct mem_vtop32 {
	uint32_t	m_as;
	uint32_t	m_va;
	uint32_t	m_pfn;
} mem_vtop32_t;
#endif

/*
 * Private ioctls for fmd(1M).  These interfaces are Sun Private.  Applications
 * and drivers should not make use of these interfaces: they can change without
 * notice and programs that consume them will fail to run on future releases.
 */
#define	MEM_NAME		(('M' << 8) | 0x04)
#define	MEM_INFO		(('M' << 8) | 0x05)

#define	MEM_PAGE_RETIRE		(('M' << 8) | 0x02)
#define	MEM_PAGE_ISRETIRED	(('M' << 8) | 0x03)
#define	MEM_PAGE_UNRETIRE	(('M' << 8) | 0x06)
#define	MEM_PAGE_GETERRORS	(('M' << 8) | 0x07)
#define	MEM_PAGE_RETIRE_MCE	(('M' << 8) | 0x08)
#define	MEM_PAGE_RETIRE_UE	(('M' << 8) | 0x09)
#define	MEM_PAGE_RETIRE_TEST	(('M' << 8) | 0x0A)

#define	MEM_SID			(('M' << 8) | 0x0B)

/*
 * Bits returned from MEM_PAGE_GETERRORS ioctl for use by fmd(1M).
 */
#define	MEM_PAGE_ERR_NONE	0x0
#define	MEM_PAGE_ERR_MULTI_CE	0x1
#define	MEM_PAGE_ERR_UE		0x2
#define	MEM_PAGE_ERR_FMA_REQ	0x8

#define	MEM_FMRI_MAX_BUFSIZE	8192	/* maximum allowed packed FMRI size */

typedef struct mem_name {
	uint64_t	m_addr;		/* memory address */
	uint64_t	m_synd;		/* architecture-specific syndrome */
	uint64_t	m_type[2];	/* architecture-specific type */
	caddr_t		m_name;		/* memory name buffer */
	size_t		m_namelen;	/* memory name buffer length */
	caddr_t		m_sid;		/* memory serial id buffer */
	size_t		m_sidlen;	/* memory serial id buffer length */
} mem_name_t;

#if	defined(_SYSCALL32)
typedef struct mem_name32 {
	uint64_t	m_addr;
	uint64_t	m_synd;
	uint64_t	m_type[2];
	caddr32_t	m_name;
	size32_t	m_namelen;
	caddr32_t	m_sid;
	size32_t	m_sidlen;
} mem_name32_t;
#endif	/* _SYSCALL32 */

typedef struct mem_info {
	uint64_t	m_addr;		/* memory address */
	uint64_t	m_synd;		/* architecture-specific syndrome */
	uint64_t	m_mem_size;	/* total memory size */
	uint64_t	m_seg_size;	/* segment size */
	uint64_t	m_bank_size;	/* bank size */
	int		m_segments;	/* # of segments */
	int		m_banks;	/* # of banks in segment */
	int		m_mcid;		/* associated memory controller id */
} mem_info_t;

/*
 * Log structure for capturing write access through mem interfaces.
 */

#define	LOG_DEVS	4	/* mem, kmem, allkmemr, allkmemw */
#define	LOG_ENTS	25	/* arbitrary guess at useful value */

typedef struct mem_log {
	kthread_t	*m_who;
	hrtime_t	m_when;
	uintptr_t	m_where;
	size_t		m_sz;
	int		m_what;	/* read or write */
	uint_t		m_success;
} mem_log_t;

#ifdef	_KERNEL

extern pfn_t impl_obmem_pfnum(pfn_t);

extern int plat_mem_do_mmio(struct uio *, enum uio_rw);

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_MEM_H */
