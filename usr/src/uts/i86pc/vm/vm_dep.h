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
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * Copyright (c) 2010, Intel Corporation.
 * All rights reserved.
 */

/*
 * UNIX machine dependent virtual memory support.
 */

#ifndef	_VM_DEP_H
#define	_VM_DEP_H


#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/clock.h>
#include <vm/hat_pte.h>
#include <sys/param.h>
#include <sys/memnode.h>

/*
 * WARNING: vm_dep.h is included by files in common.
 */

#define	GETTICK()	tsc_read()
/*
 * Do not use this function for obtaining clock tick.  This
 * is called by callers who do not need to have a guarenteed
 * correct tick value.  The proper routine to use is tsc_read().
 */

extern u_longlong_t	randtick();
extern uint_t page_create_update_flags_x86(uint_t);
extern int kernel_page_update_flags_x86(uint_t *, int);

extern size_t plcnt_sz(size_t);
#define	PLCNT_SZ(ctrs_sz) (ctrs_sz = plcnt_sz(ctrs_sz))

extern caddr_t plcnt_init(caddr_t);
#define	PLCNT_INIT(addr) (addr = plcnt_init(addr))

extern void plcnt_inc_dec(page_t *, int, int, long, int);
#define	PLCNT_INCR(pp, mnode, mtype, szc, flags)			\
	plcnt_inc_dec(pp, mtype, szc, 1l << PAGE_BSZS_SHIFT(szc), flags)
#define	PLCNT_DECR(pp, mnode, mtype, szc, flags)			\
	plcnt_inc_dec(pp, mtype, szc, -1l << PAGE_BSZS_SHIFT(szc), flags)

/*
 * macro to update page list max counts.  no-op on x86.
 */
#define	PLCNT_XFER_NORELOC(pp)

/*
 * macro to modify the page list max counts when memory is added to
 * the page lists during startup (add_physmem) or during a DR operation
 * when memory is added (kphysm_add_memory_dynamic) or deleted
 * (kphysm_del_cleanup).
 */
#define	PLCNT_MODIFY_MAX(pfn, cnt)	mtype_modify_max(pfn, cnt)

extern int memrange_num(pfn_t);
extern int pfn_2_mtype(pfn_t);
extern int mtype_func(int, int, uint_t);
extern void mtype_modify_max(pfn_t, long);
extern int mnode_pgcnt(int);
extern int mnode_range_cnt(int);

/*
 * candidate counters in vm_pagelist.c are indexed by color and range
 */
#define	NUM_MEM_RANGES		4		/* memory range types */
#define	MAX_MNODE_MRANGES	NUM_MEM_RANGES
#define	MNODE_RANGE_CNT(mnode)	mnode_range_cnt(mnode)
#define	MNODE_MAX_MRANGE(mnode)	memrange_num(mem_node_config[mnode].physbase)

/*
 * combined memory ranges from mnode and memranges[] to manage single
 * mnode/mtype dimension in the page lists.
 *
 * Use two different data structures (one for mostly read fields and one for
 * most written fields) to avoid false sharing and be more cache friendly
 * because these data structures can be very hot when many processes all do
 * munmap(2) in parallel at the same time (see CR#7003253 for more details).
 */

/*
 * Mostly read fields of combined memory ranges from mnode and memranges[]
 */
typedef struct {
	pfn_t	mnr_pfnlo;
	pfn_t	mnr_pfnhi;
	int	mnr_mnode;
	int	mnr_memrange;		/* index into memranges[] */
	int	mnr_next;		/* next lower PA mnoderange */
	int	mnr_exists;
} mnoderange_r_t;

/*
 * Mostly written fields of combined memory ranges from mnode and memranges[]
 */
typedef struct {
	/* maintain page list stats */
	pgcnt_t	mnr_mt_clpgcnt;		/* cache list cnt */
	pgcnt_t	mnr_mt_flpgcnt[MMU_PAGE_SIZES];	/* free list cnt per szc */
	pgcnt_t	mnr_mt_totcnt;		/* sum of cache and free lists */
#ifdef DEBUG
	struct mnr_mts {		/* mnode/mtype szc stats */
		pgcnt_t	mnr_mts_pgcnt;
		int	mnr_mts_colors;
		pgcnt_t *mnr_mtsc_pgcnt;
	} 	*mnr_mts;
#endif
} mnoderange_rw_t;

#define	MEMRANGEHI(mtype)						\
	(((mtype) > 0) ? memranges[(mtype) - 1] - 1: physmax)
#define	MEMRANGELO(mtype)	(memranges[(mtype)])

#define	MTYPE_FREEMEM(mt)	(mnoderanges_rw[(mt)].mnr_mt_totcnt)

/*
 * This was really badly defined, it implicitly uses mnode_maxmrange[]
 * which is a static in vm_pagelist.c
 */
extern int mtype_2_mrange(int);
#define	MTYPE_2_MRANGE(mnode, mtype)	\
	(mnode_maxmrange[mnode] - mtype_2_mrange(mtype))

/*
 * this structure is used for walking free page lists, it
 * controls when to split large pages into smaller pages,
 * and when to coalesce smaller pages into larger pages
 */
typedef struct page_list_walker {
	uint_t	plw_colors;		/* num of colors for szc */
	uint_t  plw_color_mask;		/* colors-1 */
	uint_t	plw_bin_step;		/* next bin: 1 or 2 */
	uint_t  plw_count;		/* loop count */
	uint_t	plw_bin0;		/* starting bin */
	uint_t  plw_bin_marker;		/* bin after initial jump */
	uint_t  plw_bin_split_prev;	/* last bin we tried to split */
	uint_t  plw_do_split;		/* set if OK to split */
	uint_t  plw_split_next;		/* next bin to split */
	uint_t	plw_ceq_dif;		/* number of different color groups */
					/* to check */
	uint_t	plw_ceq_mask[MMU_PAGE_SIZES + 1]; /* color equiv mask */
	uint_t	plw_bins[MMU_PAGE_SIZES + 1];	/* num of bins */
} page_list_walker_t;

/*
 * Page freelists are organized as two freelist types user and kernel, with
 * their own policy and allocation routines. The definitions related to the
 * freelist type structure are grouped below.
 *
 * The page free lists themselves are allocated dynamically with
 * dimensions [mtype][mmu_page_sizes][colors]
 *
 * For now there is only a single size cache list. Allocated dynamically.
 * dimensions [mtype][colors]
 *
 * mtype specifies a physical memory range with a unique mnode.
 */

#define	MAX_PFLT_POLICIES 3
#define	MAX_PFLT_TYPE 2
enum freelist_types {PFLT_USER, PFLT_KMEM};

/*
 * The kernel only needs a small number of page colors, far fewer than user
 * programs.
 */
#define	KFLT_PAGE_COLORS 16

typedef struct page_freelist_type page_freelist_type_t;
extern page_freelist_type_t flt_user;
extern page_freelist_type_t flt_kern;
extern page_freelist_type_t *ufltp;	/* user page freelist */
extern page_freelist_type_t *kfltp; 	/* kernel page freelist */

extern page_freelist_type_t clt_user;
extern page_freelist_type_t clt_kern;
extern page_freelist_type_t *ucltp;	/* user cachelist */
extern page_freelist_type_t *kcltp;	/* kernel cachelist */


void page_flt_init(page_freelist_type_t *, page_freelist_type_t *,
    page_freelist_type_t *, page_freelist_type_t *);
page_t *page_get_uflt(struct vnode *, u_offset_t, struct seg *, caddr_t,
    size_t, uint_t, struct lgrp *);
page_t *page_get_kflt(struct vnode *, u_offset_t, struct seg *, caddr_t,
    size_t, uint_t, struct lgrp *);
page_t *page_get_uclt(struct vnode *, u_offset_t, struct seg *, caddr_t,
    size_t, uint_t, struct lgrp *);
page_t *page_get_kclt(struct vnode *, u_offset_t, struct seg *, caddr_t,
    size_t, uint_t, struct lgrp *);
void page_kflt_walk_init(uchar_t, uint_t, uint_t, int, int,
    page_list_walker_t *);
uint_t page_kflt_walk_next_bin(uchar_t, uint_t, page_list_walker_t *);
void page_import_kflt(page_freelist_type_t *, uint_t, int, uchar_t,
    uint_t, int *);
page_t *page_user_alloc_kflt(page_freelist_type_t *, int, uint_t, int, uchar_t,
	uint_t);
page_t *page_get_any_freelist(int, uint_t, int, uchar_t, uint_t);
void kflt_expand(void);
page_t *kflt_page_promote_size(pfn_t, pgcnt_t);
void page_kflt_lock(int);
void page_kflt_unlock(int);
page_t *page_get_any_cachelist(int, uint_t, int, uint_t);

typedef page_t *(*pflt_get_func_p) (struct vnode *, u_offset_t, struct seg *,
    caddr_t, size_t, uint_t, lgrp_t *);
typedef page_t *(*pflt_policy_func_p)(page_freelist_type_t *, int, uint_t, int,
    uchar_t, uint_t);
typedef void (*pflt_list_walk_init_func_p)(uchar_t, uint_t, uint_t, int, int,
    page_list_walker_t *);
typedef uint_t (*pflt_list_walk_next_func_p)(uchar_t, uint_t,
    page_list_walker_t *);

struct page_freelist_type {
	int pflt_type;			/* type is user or kernel */
	pflt_get_func_p pflt_get_free;  /* top-level alloc routine */
	pflt_list_walk_init_func_p pflt_walk_init;  /* walker routines */
	pflt_list_walk_next_func_p pflt_walk_next;
	int	pflt_num_policies;	/* the number of policy routines */
	/*
	 * the policy routines are called by the allocator routine
	 * to implement the actual allocation policies.
	 */
	pflt_policy_func_p pflt_policy[MAX_PFLT_POLICIES];
	page_t ****pflt_freelists;	/* the page freelist arrays */
	page_t ***pflt_cachelists;	/* the page cachelist arrays */
};

#if defined(__amd64) && !defined(__xpv)
#define	PAGE_FREELISTP(is_kflt, mnode, szc, color, mtype)		\
	((is_kflt) ?							\
	(page_t **)(kfltp->pflt_freelists[(mtype)] + (color)) :   	\
	((ufltp->pflt_freelists[(mtype)][(szc)] + (color))))

#define	PAGE_GET_FREELISTS(pp, vp, off, seg, vaddr, size, flags, lgrp)	     \
	{								     \
		if (kflt_on && ((flags) & PG_KFLT)) {	     		     \
			pp = kfltp->pflt_get_free((vp), (off), (seg),	     \
			    (vaddr), (size), (flags), (lgrp));		     \
		} else {						     \
			pp = ufltp->pflt_get_free((vp), (off), (seg),	     \
			    (vaddr), (size), (flags), (lgrp));		     \
		}							     \
	}

#define	PAGE_GET_CACHELISTS(pp, vp, off, seg, vaddr, flags, lgrp)	     \
	{								     \
		if (kflt_on && ((flags) & PG_KFLT)) {			     \
			pp = kcltp->pflt_get_free((vp), (off), (seg),	     \
			    (vaddr), 0, (flags), (lgrp));		     \
		} else {						     \
			pp = ucltp->pflt_get_free((vp), (off), (seg),	     \
			    (vaddr), 0, (flags), (lgrp));		     \
		}							     \
	}

#define	PAGE_CACHELISTP(is_kflt, mnode, color, mtype) 		\
	((is_kflt) ?						\
	(kcltp->pflt_cachelists[(mtype)] + (color)) :		\
	(ucltp->pflt_cachelists[(mtype)] + (color)))


#define	PAGE_GET_ANY_FREELIST(mnode, color, mtype, szc, flags)		\
	page_get_any_freelist((mnode), (color), (mtype), (szc), 	\
	    (flags))

#define	PAGE_GET_ANY_CACHELIST(mnode, color, mtype, flags)		\
	page_get_any_cachelist((mnode), (color), (mtype), (flags))

#else /* __amd64 && ! __xpv */
#define	PAGE_FREELISTP(is_kflt, mnode, szc, color, mtype)		\
	((ufltp->pflt_freelists[(mtype)][(szc)] + (color)))

#define	PAGE_GET_FREELISTS(pp, vp, off, seg, vaddr, size, flags, lgrp)  \
	pp = ufltp->pflt_get_free((vp), (off), (seg), (vaddr), 		\
	    (size), (flags), (lgrp));

#define	PAGE_CACHELISTP(is_kflt, mnode, color, mtype) 			\
	(ucltp->pflt_cachelists[(mtype)] + (color))

#define	PAGE_GET_CACHELISTS(pp, vp, off, seg, vaddr, flags, lgrp)	\
	pp = ucltp->pflt_get_free((vp), (off), (seg),			\
	    (vaddr),  0, (flags), (lgrp));

#define	PAGE_GET_ANY_FREELIST(mnode, color, mtype, szc, flags)		\
	page_get_mnode_freelist((ufltp), (mnode), (color), (mtype), 	\
	    (szc), (flags))

#define	PAGE_GET_ANY_CACHELIST(mnode, color, mtype, flags)		\
	page_get_mnode_cachelist((ucltp), (mnode), (color), (mtype), 	\
	    (flags))

#endif /* __amd64 && ! __xpv */

#define	PAGE_FREELISTS(is_kflt, mnode, szc, color, mtype)		\
	(*(PAGE_FREELISTP((is_kflt), (mnode), (szc), (color), (mtype))))

#define	PAGE_CACHELISTS(is_kflt, mnode, color, mtype)			\
	(*(PAGE_CACHELISTP((is_kflt), (mnode), (color), (mtype))))

#define	PAGE_GET_FREELISTS_POLICY(fp, i) 				\
	(fp->pflt_policy[i])

#define	PAGE_LIST_WALK_INIT(fp, szc, flags, bin, can_split, use_ceq, plw) \
	fp->pflt_walk_init((szc), (flags), (bin), (can_split), (use_ceq), \
	    (plw))

#define	PAGE_LIST_WALK_NEXT(fp, szc, bin, plw) 				\
	fp->pflt_walk_next((szc), (bin), (plw))

/*
 * There are mutexes for the user and kernel page freelists, and the user
 * and kernel page cachelists.  We want enough locks to make contention
 * reasonable, but not too many -- otherwise page_freelist_lock() gets
 * so expensive that it becomes the bottleneck!
 */

#define	NPC_MUTEX	16

/*
 * The kflt_disable variable is used to determine whether the kernel freelist
 * is supported on this platform.
 */
extern int kflt_disable;

extern kmutex_t	*fpc_mutex[NPC_MUTEX];	/* user freelist mutexes */
extern kmutex_t	*kfpc_mutex[NPC_MUTEX]; /* kernel freelist mutexes */
extern kmutex_t	*cpc_mutex[NPC_MUTEX];  /* user cache list  mutexes */
extern kmutex_t	*kcpc_mutex[NPC_MUTEX]; /* kernel cache list  mutexes */

#define	PC_ISKFLT(fltp)	(fltp->pflt_type == PFLT_KMEM)
	/* flag used by the kflt_export function when calling page_promote */
#define	PC_KFLT_EXPORT 0x4

extern page_t *page_get_mnode_freelist(page_freelist_type_t *, int, uint_t,
    int, uchar_t, uint_t);
extern page_t *page_get_mnode_cachelist(page_freelist_type_t *, uint_t, uint_t,
    int, int);
extern page_t *page_get_contig_pages(page_freelist_type_t *, int, uint_t, int,
    uchar_t, uint_t);
extern void page_list_walk_init(uchar_t, uint_t, uint_t, int, int,
    page_list_walker_t *);
extern uint_t page_list_walk_next_bin(uchar_t, uint_t, page_list_walker_t *);

extern void kflt_evict_wakeup();
extern void kflt_freemem_add(pgcnt_t);
extern void kflt_freemem_sub(pgcnt_t);

/* mem node iterator is not used on x86 */
#define	MEM_NODE_ITERATOR_DECL(it)
#define	MEM_NODE_ITERATOR_INIT(pfn, mnode, szc, it)

/*
 * interleaved_mnodes mode is never set on x86, therefore,
 * simply return the limits of the given mnode, which then
 * determines the length of hpm_counters array for the mnode.
 */
#define	HPM_COUNTERS_LIMITS(mnode, physbase, physmax, first) 	\
	{							\
		(physbase) = mem_node_config[(mnode)].physbase;	\
		(physmax) = mem_node_config[(mnode)].physmax;	\
		(first) = (mnode);				\
	}

#define	PAGE_CTRS_WRITE_LOCK(mnode)				\
	{							\
		rw_enter(&page_ctrs_rwlock[(mnode)], RW_WRITER);\
		page_freelist_lock(mnode);			\
	}

#define	PAGE_CTRS_WRITE_UNLOCK(mnode)				\
	{							\
		page_freelist_unlock(mnode);			\
		rw_exit(&page_ctrs_rwlock[(mnode)]);		\
	}

/*
 * macro to call page_ctrs_adjust() when memory is added
 * during a DR operation.
 */
#define	PAGE_CTRS_ADJUST(pfn, cnt, rv) {				       \
	spgcnt_t _cnt = (spgcnt_t)(cnt);				       \
	int _mn;							       \
	pgcnt_t _np;							       \
	pfn_t _pfn = (pfn);						       \
	pfn_t _endpfn = _pfn + _cnt;					       \
	while (_pfn < _endpfn) {					       \
		_mn = PFN_2_MEM_NODE(_pfn);				       \
		_np = MIN(_endpfn, mem_node_config[_mn].physmax + 1) - _pfn;   \
		_pfn += _np;						       \
		if ((rv = page_ctrs_adjust(_mn)) != 0)			       \
			break;						       \
	}								       \
}

#define	PAGE_GET_COLOR_SHIFT(szc, nszc)				\
	    (hw_page_array[(nszc)].hp_shift - hw_page_array[(szc)].hp_shift)

#define	PAGE_CONVERT_COLOR(ncolor, szc, nszc)			\
	    ((ncolor) << PAGE_GET_COLOR_SHIFT((szc), (nszc)))

#define	PFN_2_COLOR(pfn, szc, it)					\
	(((pfn) & page_colors_mask) >>			                \
	(hw_page_array[szc].hp_shift - hw_page_array[0].hp_shift))

#define	PNUM_SIZE(szc)							\
	(hw_page_array[(szc)].hp_pgcnt)
#define	PNUM_SHIFT(szc)							\
	(hw_page_array[(szc)].hp_shift - hw_page_array[0].hp_shift)
#define	PAGE_GET_SHIFT(szc)						\
	(hw_page_array[(szc)].hp_shift)
#define	PAGE_GET_PAGECOLORS(szc)					\
	(hw_page_array[(szc)].hp_colors)

/*
 * This macro calculates the next sequential pfn with the specified
 * color using color equivalency mask
 */
#define	PAGE_NEXT_PFN_FOR_COLOR(pfn, szc, color, ceq_mask, color_mask, it)    \
	{								      \
		uint_t	pfn_shift = PAGE_BSZS_SHIFT(szc);                     \
		pfn_t	spfn = pfn >> pfn_shift;                              \
		pfn_t	stride = (ceq_mask) + 1;                              \
		ASSERT(((color) & ~(ceq_mask)) == 0);                         \
		ASSERT((((ceq_mask) + 1) & (ceq_mask)) == 0);                 \
		if (((spfn ^ (color)) & (ceq_mask)) == 0) {                   \
			pfn += stride << pfn_shift;                           \
		} else {                                                      \
			pfn = (spfn & ~(pfn_t)(ceq_mask)) | (color);          \
			pfn = (pfn > spfn ? pfn : pfn + stride) << pfn_shift; \
		}                                                             \
	}

#define	USER_2_KMEM_BIN(bin)	((bin) & (KFLT_PAGE_COLORS - 1))

/* get the color equivalency mask for the next szc */
#define	PAGE_GET_NSZ_MASK(szc, mask)                                         \
	((mask) >> (PAGE_GET_SHIFT((szc) + 1) - PAGE_GET_SHIFT(szc)))

/* get the color of the next szc */
#define	PAGE_GET_NSZ_COLOR(szc, color)                                       \
	((color) >> (PAGE_GET_SHIFT((szc) + 1) - PAGE_GET_SHIFT(szc)))

/* Find the bin for the given page if it was of size szc */
#define	PP_2_BIN_SZC(pp, szc)	(PFN_2_COLOR(pp->p_pagenum, szc, NULL))

#define	PP_2_BIN(pp)		((PP_ISKFLT(pp)) ?			\
	USER_2_KMEM_BIN(PP_2_BIN_SZC(pp, pp->p_szc)) :			\
	(PP_2_BIN_SZC(pp, pp->p_szc)))

#define	PP_2_MEM_NODE(pp)	(PFN_2_MEM_NODE(pp->p_pagenum))
#define	PP_2_MTYPE(pp)		(pfn_2_mtype(pp->p_pagenum))
#define	PP_2_SZC(pp)		(pp->p_szc)

#define	SZCPAGES(szc)		(1 << PAGE_BSZS_SHIFT(szc))
#define	PFN_BASE(pfnum, szc)	(pfnum & ~(SZCPAGES(szc) - 1))

void	page_list_walk_init(uchar_t szc, uint_t flags, uint_t bin,
    int can_split, int use_ceq, page_list_walker_t *plw);

uint_t	page_list_walk_next_bin(uchar_t szc, uint_t bin,
    page_list_walker_t *plw);

extern struct cpu	cpus[];
#define	CPU0		cpus

extern int mtype_init(vnode_t *, caddr_t, uint_t *, size_t);
#define	MTYPE_INIT(mtype, vp, vaddr, flags, pgsz)		\
	(mtype = mtype_init(vp, vaddr, &(flags), pgsz))

/*
 * macros to loop through the mtype range (page_get_mnode_{free,cache,any}list,
 * and page_get_contig_pages)
 *
 * MTYPE_START sets the initial mtype. -1 if the mtype range specified does
 * not contain mnode.
 *
 * MTYPE_NEXT sets the next mtype. -1 if there are no more valid
 * mtype in the range.
 */

#define	MTYPE_START(mnode, mtype, flags)				\
	(mtype = mtype_func(mnode, mtype, flags))

#define	MTYPE_NEXT(mnode, mtype, flags) {				\
	if (flags & PGI_MT_RANGE) {					\
		mtype = mtype_func(mnode, mtype, flags | PGI_MT_NEXT);	\
	} else {							\
		mtype = -1;						\
	}								\
}

extern int mtype_pgr_init(int *, page_t *, int, pgcnt_t);
#define	MTYPE_PGR_INIT(mtype, flags, pp, mnode, pgcnt)			\
	(mtype = mtype_pgr_init(&flags, pp, mnode, pgcnt))

#define	MNODE_PGCNT(mnode)		mnode_pgcnt(mnode)

extern void mnodetype_2_pfn(int, int, pfn_t *, pfn_t *);
#define	MNODETYPE_2_PFN(mnode, mtype, pfnlo, pfnhi)			\
	mnodetype_2_pfn(mnode, mtype, &pfnlo, &pfnhi)

#define	PC_FREELIST_BIN_MUTEX(is_kflt, mnode, bin, flags)		\
	((is_kflt) ? 							\
	(&kfpc_mutex[(bin) & (NPC_MUTEX - 1)][mnode]) : 		\
	(&fpc_mutex[(bin) & (NPC_MUTEX - 1)][mnode]))

#define	PC_CACHELIST_BIN_MUTEX(is_kflt, mnode, bin, flags)		\
	((is_kflt) ? 							\
	(&kcpc_mutex[(bin) & (NPC_MUTEX - 1)][mnode]) : 		\
	(&cpc_mutex[(bin) & (NPC_MUTEX - 1)][mnode]))

#define	PC_BIN_MUTEX(is_kflt, mnode, bin, flags)			\
	((flags & PG_FREE_LIST) ?					\
	PC_FREELIST_BIN_MUTEX(is_kflt, mnode, bin, flags):		\
	PC_CACHELIST_BIN_MUTEX(is_kflt, mnode, bin, flags))

#define	FPC_MUTEX(mnode, i)	(&fpc_mutex[i][mnode])
#define	CPC_MUTEX(mnode, i)	(&cpc_mutex[i][mnode])
#define	KFPC_MUTEX(mnode, i)	(&kfpc_mutex[i][mnode])
#define	KCPC_MUTEX(mnode, i)	(&kcpc_mutex[i][mnode])

#ifdef DEBUG
#define	CHK_LPG(pp, szc)	chk_lpg(pp, szc)
extern void	chk_lpg(page_t *, uchar_t);
#else
#define	CHK_LPG(pp, szc)
#endif

#define	FULL_REGION_CNT(rg_szc)	\
	(LEVEL_SIZE(rg_szc) >> LEVEL_SHIFT(rg_szc - 1))

/* Return the leader for this mapping size */
#define	PP_GROUPLEADER(pp, szc) \
	(&(pp)[-(int)((pp)->p_pagenum & (SZCPAGES(szc)-1))])

/* Return the root page for this page based on p_szc */
#define	PP_PAGEROOT(pp) ((pp)->p_szc == 0 ? (pp) : \
	PP_GROUPLEADER((pp), (pp)->p_szc))

/*
 * The counter base must be per page_counter element to prevent
 * races when re-indexing, and the base page size element should
 * be aligned on a boundary of the given region size.
 *
 * We also round up the number of pages spanned by the counters
 * for a given region to PC_BASE_ALIGN in certain situations to simplify
 * the coding for some non-performance critical routines.
 */

#define	PC_BASE_ALIGN		((pfn_t)1 << PAGE_BSZS_SHIFT(MMU_PAGE_SIZES-1))
#define	PC_BASE_ALIGN_MASK	(PC_BASE_ALIGN - 1)

/*
 * cpu/mmu-dependent vm variables
 */
extern uint_t mmu_page_sizes;
extern uint_t mmu_exported_page_sizes;
/*
 * page sizes that legacy applications can see via getpagesizes(3c).
 * Used to prevent legacy applications from inadvertantly using the
 * 'new' large pagesizes (1g and above).
 */
extern uint_t mmu_legacy_page_sizes;

/* For x86, userszc is the same as the kernel's szc */
#define	USERSZC_2_SZC(userszc)	(userszc)
#define	SZC_2_USERSZC(szc)	(szc)

/*
 * for hw_page_map_t, sized to hold the ratio of large page to base
 * pagesize (1024 max)
 */
typedef	short	hpmctr_t;

/*
 * get the setsize of the current cpu - assume homogenous for x86
 */
extern int	l2cache_sz, l2cache_linesz, l2cache_assoc;

#define	L2CACHE_ALIGN		l2cache_linesz
#define	L2CACHE_ALIGN_MAX	64
#define	CPUSETSIZE()		\
	(l2cache_assoc ? (l2cache_sz / l2cache_assoc) : MMU_PAGESIZE)

/*
 * Return the log2(pagesize(szc) / MMU_PAGESIZE) --- or the shift count
 * for the number of base pages in this pagesize
 */
#define	PAGE_BSZS_SHIFT(szc) (LEVEL_SHIFT(szc) - MMU_PAGESHIFT)

/*
 * Internal PG_ flags.
 */
#define	PGI_RELOCONLY	0x010000	/* opposite of PG_NORELOC */
#define	PGI_NOCAGE	0x020000	/* cage is disabled */
#define	PGI_PGCPHIPRI	0x040000	/* page_get_contig_page pri alloc */
#define	PGI_PGCPSZC0	0x080000	/* relocate base pagesize page */

/*
 * PGI range flags - should not overlap PGI flags
 */
#define	PGI_MT_RANGE0	0x1000000	/* mtype range to 0 */
#define	PGI_MT_RANGE16M 0x2000000	/* mtype range to 16m */
#define	PGI_MT_RANGE4G	0x4000000	/* mtype range to 4g */
#define	PGI_MT_NEXT	0x8000000	/* get next mtype */
#define	PGI_MT_RANGE	(PGI_MT_RANGE0 | PGI_MT_RANGE16M | PGI_MT_RANGE4G)

/*
 * Maximum and default values for user heap, stack, private and shared
 * anonymous memory, and user text and initialized data.
 * Used by map_pgsz*() routines.
 */
extern size_t max_uheap_lpsize;
extern size_t default_uheap_lpsize;
extern size_t max_ustack_lpsize;
extern size_t default_ustack_lpsize;
extern size_t max_privmap_lpsize;
extern size_t max_uidata_lpsize;
extern size_t max_utext_lpsize;
extern size_t max_shm_lpsize;
extern size_t mcntl0_lpsize;

/*
 * Sanity control. Don't use large pages regardless of user
 * settings if there's less than priv or shm_lpg_min_physmem memory installed.
 * The units for this variable are 8K pages.
 */
extern pgcnt_t privm_lpg_min_physmem;
extern pgcnt_t shm_lpg_min_physmem;

/*
 * hash as and addr to get a bin.
 */

#define	AS_2_USER_BIN(as, seg, vp, addr, bin, szc)			    \
	bin = (((((uintptr_t)(addr) >> PAGESHIFT) + ((uintptr_t)(as) >> 4)) \
	    & page_colors_mask) >>					    \
	    (hw_page_array[szc].hp_shift - hw_page_array[0].hp_shift))

#define	AS_2_BIN(is_kflt, as, seg, vp, addr, bin, szc) {		    \
	AS_2_USER_BIN(as, seg, vp, addr, bin, szc);			    \
	if (is_kflt) {							    \
		bin = USER_2_KMEM_BIN(bin);				    \
	}								    \
}
/*
 * cpu private vm data - accessed thru CPU->cpu_vm_data
 *	vc_pnum_memseg: tracks last memseg visited in page_numtopp_nolock()
 *	vc_pnext_memseg: tracks last memseg visited in page_nextn()
 *	vc_kmptr: orignal unaligned kmem pointer for this vm_cpu_data_t
 *	vc_kmsize: orignal kmem size for this vm_cpu_data_t
 */

typedef struct {
	struct memseg	*vc_pnum_memseg;
	struct memseg	*vc_pnext_memseg;
	void		*vc_kmptr;
	size_t		vc_kmsize;
} vm_cpu_data_t;

/* allocation size to ensure vm_cpu_data_t resides in its own cache line */
#define	VM_CPU_DATA_PADSIZE						\
	(P2ROUNDUP(sizeof (vm_cpu_data_t), L2CACHE_ALIGN_MAX))

/* for boot cpu before kmem is initialized */
extern char	vm_cpu_data0[];

/*
 * When a bin is empty, and we can't satisfy a color request correctly,
 * we scan.  If we assume that the programs have reasonable spatial
 * behavior, then it will not be a good idea to use the adjacent color.
 * Using the adjacent color would result in virtually adjacent addresses
 * mapping into the same spot in the cache.  So, if we stumble across
 * an empty bin, skip a bunch before looking.  After the first skip,
 * then just look one bin at a time so we don't miss our cache on
 * every look. Be sure to check every bin.  Page_create() will panic
 * if we miss a page.
 *
 * This also explains the `<=' in the for loops in both page_get_freelist()
 * and page_get_cachelist().  Since we checked the target bin, skipped
 * a bunch, then continued one a time, we wind up checking the target bin
 * twice to make sure we get all of them bins.
 */
#define	BIN_STEP	19

#ifdef VM_STATS
struct vmm_vmstats_str {
			/* page_get_uflt and page_get_kflt */
	ulong_t pgf_alloc[MMU_PAGE_SIZES][MAX_PFLT_TYPE];
	ulong_t pgf_allocok[MMU_PAGE_SIZES][MAX_PFLT_TYPE];
	ulong_t pgf_allocokrem[MMU_PAGE_SIZES][MAX_PFLT_TYPE];
	ulong_t pgf_allocfailed[MMU_PAGE_SIZES][MAX_PFLT_TYPE];
	ulong_t	pgf_allocdeferred;
	ulong_t	pgf_allocretry[MMU_PAGE_SIZES][MAX_PFLT_TYPE];
	ulong_t pgik_allocok;			/* page_import_kflt */
	ulong_t pgik_allocfailed;
	ulong_t pgkx_allocok;			/* kflt_expand */
	ulong_t pgkx_allocfailed;
	ulong_t puak_allocok;			/* page_user_alloc_kflt */
	ulong_t puak_allocfailed;
	ulong_t puakc_allocok;			/* page_user_alloc_kclt */
	ulong_t puakc_allocfailed;
	ulong_t pgexportok;			/* kflt_export */
	ulong_t pgexportfail;
	ulong_t pgkflt_disable;			/* kflt_user_evict */
	ulong_t pgc_alloc[MAX_PFLT_TYPE];	/* page_get_cachelist */
	ulong_t pgc_allocok[MAX_PFLT_TYPE];
	ulong_t pgc_allocokrem[MAX_PFLT_TYPE];
	ulong_t pgc_allocokdeferred[MAX_PFLT_TYPE];
	ulong_t pgc_allocfailed[MAX_PFLT_TYPE];
	ulong_t	pgcp_alloc[MMU_PAGE_SIZES];	/* page_get_contig_pages */
	ulong_t	pgcp_allocfailed[MMU_PAGE_SIZES];
	ulong_t	pgcp_allocempty[MMU_PAGE_SIZES];
	ulong_t	pgcp_allocok[MMU_PAGE_SIZES];
	ulong_t	ptcp[MMU_PAGE_SIZES];		/* page_trylock_contig_pages */
	ulong_t	ptcpfreethresh[MMU_PAGE_SIZES];
	ulong_t	ptcpfailexcl[MMU_PAGE_SIZES];
	ulong_t	ptcpfailszc[MMU_PAGE_SIZES];
	ulong_t	ptcpfailcage[MMU_PAGE_SIZES];
	ulong_t	ptcpfailkflt[MMU_PAGE_SIZES];
	ulong_t	ptcpok[MMU_PAGE_SIZES];
	ulong_t	pgmf_alloc[MMU_PAGE_SIZES];	/* page_get_mnode_freelist */
	ulong_t	pgmf_allocfailed[MMU_PAGE_SIZES];
	ulong_t	pgmf_allocempty[MMU_PAGE_SIZES];
	ulong_t	pgmf_allocok[MMU_PAGE_SIZES];
	ulong_t	pgmc_alloc;			/* page_get_mnode_cachelist */
	ulong_t	pgmc_allocfailed;
	ulong_t	pgmc_allocempty;
	ulong_t	pgmc_allocok;
	ulong_t	pladd_free[MMU_PAGE_SIZES];	/* page_list_add/sub */
	ulong_t	plsub_free[MMU_PAGE_SIZES];
	ulong_t	pladd_cache;
	ulong_t	plsub_cache;
	ulong_t	plsubpages_szcbig;
	ulong_t	plsubpages_szc0;
	ulong_t	pfs_req[MMU_PAGE_SIZES];	/* page_freelist_split */
	ulong_t	pfs_demote[MMU_PAGE_SIZES];
	ulong_t	pfc_coalok[MMU_PAGE_SIZES][MAX_MNODE_MRANGES];
	ulong_t	ppr_reloc[MMU_PAGE_SIZES];	/* page_relocate */
	ulong_t ppr_relocnoroot[MMU_PAGE_SIZES];
	ulong_t ppr_reloc_replnoroot[MMU_PAGE_SIZES];
	ulong_t ppr_relocnolock[MMU_PAGE_SIZES];
	ulong_t ppr_relocnomem[MMU_PAGE_SIZES];
	ulong_t ppr_relocok[MMU_PAGE_SIZES];
	ulong_t ppr_copyfail;
	/* page coalesce counter */
	ulong_t page_ctrs_coalesce[MMU_PAGE_SIZES][MAX_MNODE_MRANGES];
	/* candidates useful */
	ulong_t page_ctrs_cands_skip[MMU_PAGE_SIZES][MAX_MNODE_MRANGES];
	/* ctrs changed after locking */
	ulong_t page_ctrs_changed[MMU_PAGE_SIZES][MAX_MNODE_MRANGES];
	/* page_freelist_coalesce failed */
	ulong_t page_ctrs_failed[MMU_PAGE_SIZES][MAX_MNODE_MRANGES];
	ulong_t page_ctrs_coalesce_all;	/* page coalesce all counter */
	ulong_t page_ctrs_cands_skip_all; /* candidates useful for all func */
	ulong_t	restrict4gcnt;
	ulong_t	unrestrict16mcnt;	/* non-DMA 16m allocs allowed */
	ulong_t	pgpanicalloc;		/* PG_PANIC allocation */
	ulong_t	pcf_deny[MMU_PAGE_SIZES];	/* page_chk_freelist */
	ulong_t	pcf_allow[MMU_PAGE_SIZES];
};
extern struct vmm_vmstats_str vmm_vmstats;
#endif	/* VM_STATS */

extern size_t page_ctrs_sz(void);
extern caddr_t page_ctrs_alloc(caddr_t);
extern void page_ctr_sub(int, int, page_t *, int);
extern page_t *page_freelist_split(uchar_t,
    uint_t, int, int, pfn_t, pfn_t, page_list_walker_t *);
extern page_t *page_freelist_coalesce(int, uchar_t, uint_t, uint_t, int,
    pfn_t);
extern void page_freelist_coalesce_all(int);
extern uint_t page_get_pagecolors(uint_t);
extern void pfnzero(pfn_t, uint_t, uint_t);

#ifdef	__cplusplus
}
#endif

#endif	/* _VM_DEP_H */
