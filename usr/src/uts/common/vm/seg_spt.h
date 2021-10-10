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
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_VM_SEG_SPT_H
#define	_VM_SEG_SPT_H

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef _ASM

#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/lgrp.h>

/*
 * Passed data when creating spt segment.
 */
struct  segspt_crargs {
	struct	seg	*seg_spt;
	struct anon_map *amp;
	uint_t		prot;
	uint_t		flags;
	uint_t		szc;
	uint_t		advice;
	size_t		granule_sz;
};

typedef struct spt_data {
	struct vnode	*spt_vp;
	struct anon_map	*spt_amp;
	size_t 		spt_realsize;
	struct page	**spt_ppa;
	ushort_t	*spt_ppa_lckcnt;
	uint_t		spt_prot;
	kmutex_t 	spt_lock;
	size_t		spt_pcachecnt;	/* # of times in pcache */
	uint_t		spt_flags;	/* Dynamic ISM or regular ISM */
	kcondvar_t	spt_cv;
	ushort_t	spt_gen;	/* only updated for DISM */
	/*
	 * Initial memory allocation policy
	 * used during pre-allocation done in shmat()
	 */
	lgrp_mem_policy_info_t	spt_policy_info;
	uint_t		spt_advice;	/* advice given by shmadv(2) */
	size_t		spt_granule_sz;	/* granule size for shmget_osm() */
	krwlock_t	spt_mcglock;	/* lock for MC_LOCK_GRANULE, */
					/* MC_UNLOCK_GRANULE */
} spt_data_t;

/*
 * Private data for spt_shm segment.
 */
typedef struct shm_data {
	struct as	*shm_sptas;
	struct anon_map *shm_amp;
	spgcnt_t	shm_softlockcnt; /* # outstanding lock operations */
	struct seg 	*shm_sptseg;	/* pointer to spt segment */
	char		*shm_vpage;	/* indicating locked pages */
	spgcnt_t	shm_lckpgs;	/* # of locked pages per attached seg */
	/*
	 * Memory allocation policy after shmat()
	 */
	lgrp_mem_policy_info_t	shm_policy_info;
	kmutex_t shm_segfree_syncmtx;	/* barrier lock for segspt_shmfree() */
} shm_data_t;

/*
 * Flags for shm_vpage.
 */
#define	DISM_PG_LOCKED		0x1	/* DISM page is locked */

/*
 * Flags used in spt_flags.
 */
#define	OSM_MEM_FREED 		0x1	/* Set when OSM pages are freed */
#define	DISM_PPA_CHANGED	0x2	/* DISM new lock, need to rebuild ppa */

#define	DISM_LOCK_MAX		0xfffe	/* max number of locks per DISM page */

	/* shmget_osm() shared memory. */
#define	SPT_OSM(sptd)	(sptd->spt_granule_sz)

#endif

#ifdef _KERNEL

#ifndef _ASM

/*
 * Functions used in shm.c to call ISM.
 */
int	sptcreate(size_t size, struct seg **sptseg, struct anon_map *amp,
	    uint_t prot, uint_t flags, uint_t szc, uint_t advice,
	    size_t granule_sz);
void	sptdestroy(struct as *, struct anon_map *);
int	segspt_shmattach(struct seg *, caddr_t *);

#define	isspt(sp)	((sp)->shm_sptinfo ? (sp)->shm_sptinfo->sptas : NULL)
#define	spt_locked(a)	((a) & SHM_SHARE_MMU)
#define	spt_pageable(a)	((a) & SHM_PAGEABLE)
#define	spt_invalid(a)	(spt_locked((a)) && spt_pageable((a)))

/*
 * This can be applied to a segment with seg->s_ops == &segspt_shmops
 * to determine the real size of the ISM segment.
 */
#define	spt_realsize(seg) (((struct spt_data *)(((struct shm_data *)\
			((seg)->s_data))->shm_sptseg->s_data))->spt_realsize)

/*
 * This can be applied to a segment with seg->s_ops == &segspt_ops
 * to determine the flags of the {D}ISM segment.
 */
#define	spt_flags(seg) (((struct spt_data *)((seg)->s_data))->spt_flags)

/*
 * For large page support
 */
extern int segvn_anypgsz;

/*
 * The minimum preferred pagesize for {D}ISM segment.
 * The size of {D}ISM segment is rounded up to this pagesize in shmat().
 */
extern size_t ism_min_pgsz;

#endif

/*
 * In a 64-bit address space, we'll try to put ISM segments between
 * PREDISM_BASE and PREDISM_BOUND.  The HAT may use these constants to
 * predict that a VA is contained by an ISM segment, which may optimize
 * translation.  The range must _only_ be treated as advisory; ISM segments
 * may fall outside of the range, and non-ISM segments may be contained
 * within the range.
 * In order to avoid collision between ISM/DISM addresses with e.g.
 * process heap addresses we will try to put ISM/DISM segments above
 * PREDISM_1T_BASESHIFT (1T).
 * The HAT is still expecting that any VA larger than PREDISM_BASESHIFT
 * may belong to ISM/DISM (so on tlb miss it will probe first for 4M
 * translation)
 */
#define	PREDISM_BASESHIFT	33
#define	PREDISM_1T_BASESHIFT	40
#define	PREDISM_BASE		((uintptr_t)1 << PREDISM_BASESHIFT)
#define	PREDISM_1T_BASE		((uintptr_t)1 << PREDISM_1T_BASESHIFT)
#define	PREDISM_BOUND		((uintptr_t)1 << 63)

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _VM_SEG_SPT_H */
