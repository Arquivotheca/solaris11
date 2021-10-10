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
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SYS_MPO_H
#define	_SYS_MPO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/lgrp.h>

/*
 * mpo.h -  Sun4v MPO common header file
 *
 */

#define	PROP_LG_CPU_ID	"id"
#define	PROP_LG_MASK	"address-mask"
#define	PROP_LG_LATENCY "latency"
#define	PROP_LG_MATCH	"address-match"
#define	PROP_LG_MEM_LG	"memory-latency-group"
#define	PROP_LG_CPU	"cpu"
#define	PROP_LG_MBLOCK	"mblock"
#define	PROP_LG_BASE	"base"
#define	PROP_LG_SIZE	"size"
#define	PROP_LG_RA_PA_OFFSET	"address-congruence-offset"

/* Macro to set the correspending bit if an mem-lg homeid is a member */
#define	HOMESET_ADD(homeset, home)\
	homeset |= ((int)1 << (home))

/* Macro to check if an mem_lg homeid is a member of the homeset */
#define	MEM_LG_ISMEMBER(homeset, home)\
	((homeset) & ((uint64_t)1 << (home)))

/* Structure to store CPU information from the MD */

struct cpu_md {
	uint_t 	home;
	int	lgrp_index;
};

/* Structure to store mem-lg information from the MD */

struct lgrp_md {
	uint64_t	id;
	uint64_t	addr_mask;
	uint64_t	addr_match;
	uint64_t	latency;
	mde_cookie_t	node;
	int		ncpu;
};

/* Structure to store mblock information retrieved from the MD */

typedef struct mblock_md {
	uint64_t	base;
	uint64_t	size;
	uint64_t	ra_to_pa;
	pfn_t		base_pfn;
	pfn_t		end_pfn;
} mblock_md_t;

/* Structure for memnode information for use by plat_pfn_to_mem_node */

struct mnode_info {
	pfn_t		base_pfn;
	pfn_t		end_pfn;
};

/* A stripe defines the portion of a mem_node that falls in one mblock */
typedef struct {
	pfn_t physbase;	/* first page in mnode in the corresponding mblock */
	pfn_t physmax;	/* last valid page in mnode in mblock */
	pfn_t offset;   /* stripe starts at physbase - offset */
	int exists;	/* set to 1 if mblock has memory in this mnode stripe */
} mem_stripe_t;

/* Configuration including allocation state of mblocks and stripes */

typedef struct {
	mblock_md_t	*mc_mblocks;	/* mblock array */
	int 		mc_nmblocks;	/* number in array */
	mem_stripe_t 	*mc_stripes;	/* stripe array */
	int 		mc_nstripes;	/* number in array */
	int 		mc_alloc_sz;	/* size in bytes of mc_mblocks if */
					/* it was kmem_alloc'd, else 0 */
} mpo_config_t;

extern int mblock_boundary_check(uint64_t, size_t);

/* These are used when MPO requires preallocated kvseg32 space */
extern	caddr_t	mpo_heap32_buf;
extern	size_t	mpo_heap32_bufsz;
extern void mpo_cpu_add(md_t *md, int cpuid);
extern void mpo_cpu_remove(int cpuid);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_MPO_H */
