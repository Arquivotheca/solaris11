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
 * Copyright (c) 1993, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/param.h>
#include <sys/user.h>
#include <sys/mman.h>
#include <sys/kmem.h>
#include <sys/sysmacros.h>
#include <sys/cmn_err.h>
#include <sys/systm.h>
#include <sys/tuneable.h>
#include <vm/hat.h>
#include <vm/seg.h>
#include <vm/as.h>
#include <vm/anon.h>
#include <vm/page.h>
#include <sys/buf.h>
#include <sys/swap.h>
#include <sys/atomic.h>
#include <vm/seg_spt.h>
#include <sys/debug.h>
#include <sys/vtrace.h>
#include <sys/shm.h>
#include <sys/shm_impl.h>
#include <sys/lgrp.h>
#include <sys/vmsystm.h>
#include <sys/policy.h>
#include <sys/project.h>
#include <sys/zone.h>
#include <vm/vmtask.h>

#define	SEGSPTADDR	(caddr_t)0x0

/*
 * # pages used for spt
 */
size_t	spt_used;

/*
 * The ISM/DISM segment size is always rounded up to this value in shmat().
 * This is the smallest large page size supported by hat_share(). This is
 * also the default page size supported for ISM/DISM.
 *
 * As a result of this, if the real size is not in multiple of preferred
 * large page size (which we choose for segment), ISM/DISM segment will
 * not have preferred page size at the tail end.
 */
size_t	ism_min_pgsz;

/*
 * DISM is suboptimal when memory in the DISM segment is locked without
 * regard to the size of pages used in the segment.  It also requires
 * additional privileges to lock down DISM pages; ISM does not require
 * that.  In addition, if the process locking down parts of a DISM
 * segment exits, its locks are released, which can lead to substantial
 * performance degradations.
 *
 * To address these deficiencies and pave the way for VM2 shared memory
 * segments, a new type of shared memory, "Optimised shared memory" (OSM) is
 * introduced.  OSM's design is centered around a particular "granule size",
 * which is a power-of-2 >= sysconf(_SC_OSM_PAGESIZE_MIN).  All operations
 * on an OSM segment are done in units of granule_size; the size must be
 * a granule_size multiple, the address an OSM segment is mapped at must
 * be a granule_size multiple, etc.
 *
 * Each "granule_size"-aligned-and-sized unit of the shared memory segment
 * is referred to as a "granule", and is either "locked" or "unlocked".  A
 * newly created OSM segment is entirely unlocked.  Only "locked" sections
 * of the OSM segment can be accessed; any access to "unlocked" sections
 * will fail with EFAULT or SIGSEGV.
 *
 * To create an OSM shm, the following interface is used:
 *
 * res = shmget_osm(id, size, perm, granule_size)
 *
 *  Like shmget(), but does not reserve any swap for the shared memory segment,
 *  and sets the granule size to granule_size.
 *
 * Mapping OSM is done using shmat(); the address (if specified) must be
 * aligned to the granule size.
 *
 * To lock/unlock memory, the new interfaces:
 *
 * memcntl(addr, len, MC_LOCK_GRANULE, ...)
 * memcntl(addr, len, MC_UNLOCK_GRANULE, ...)
 *
 * Take the granules covered by [addr, addr + len) (which must be
 * granule_size-aligned and contained within a shmat()ed OSM segment) and
 * either locks or unlocks them.
 *
 * Locking a granule consists of reserving locked-down memory for the
 * memory needed, then allocating the underlying pages.  Unlocking
 * frees the associated memory, then releases the locked-memory reservation.
 *
 * OSM segments cannot be locked using via the mlock() or shmctl() interfaces,
 * and the SHM_PAGEABLE and SHM_SHARE_MMU flags may not be used when shmat()ing
 * them.
 *
 * At the moment, the implementation of OSM is built on top of the existing
 * DISM code; as a result of this, they have SHM_PAGEABLE set in their kshmid
 * structure.
 */
/*
 * segspt_minfree is the memory left for system after ISM
 * locked its pages; it is set up to 5% of availrmem in
 * sptcreate when ISM is created.  ISM should not use more
 * than ~90% of availrmem; if it does, then the performance
 * of the system may decrease. Machines with large memories may
 * be able to use up more memory for ISM so we set the default
 * segspt_minfree to 5% (which gives ISM max 95% of availrmem.
 * If somebody wants even more memory for ISM (risking hanging
 * the system) they can patch the segspt_minfree to smaller number.
 */
pgcnt_t segspt_minfree = 0;

static int segspt_create(struct seg *seg, caddr_t argsp);
static int segspt_unmap(struct seg *seg, caddr_t raddr, size_t ssize);
static void segspt_free(struct seg *seg);
static void segspt_free_pages(struct seg *seg, caddr_t addr, size_t len);
static lgrp_mem_policy_info_t *segspt_getpolicy(struct seg *seg, caddr_t addr);

static void
segspt_badop()
{
	panic("segspt_badop called");
	/*NOTREACHED*/
}

#define	SEGSPT_BADOP(t)	(t(*)())segspt_badop

struct seg_ops segspt_ops = {
	SEGSPT_BADOP(int),		/* dup */
	segspt_unmap,
	segspt_free,
	SEGSPT_BADOP(int),		/* fault */
	SEGSPT_BADOP(faultcode_t),	/* faulta */
	SEGSPT_BADOP(int),		/* setprot */
	SEGSPT_BADOP(int),		/* checkprot */
	SEGSPT_BADOP(int),		/* kluster */
	SEGSPT_BADOP(size_t),		/* swapout */
	SEGSPT_BADOP(int),		/* sync */
	SEGSPT_BADOP(size_t),		/* incore */
	SEGSPT_BADOP(int),		/* lockop */
	SEGSPT_BADOP(int),		/* getprot */
	SEGSPT_BADOP(u_offset_t), 	/* getoffset */
	SEGSPT_BADOP(int),		/* gettype */
	SEGSPT_BADOP(int),		/* getvp */
	SEGSPT_BADOP(int),		/* advise */
	SEGSPT_BADOP(void),		/* dump */
	SEGSPT_BADOP(int),		/* pagelock */
	SEGSPT_BADOP(int),		/* setpgsz */
	SEGSPT_BADOP(int),		/* getmemid */
	segspt_getpolicy,		/* getpolicy */
	SEGSPT_BADOP(int),		/* capable */
};

static int segspt_shmdup(struct seg *seg, struct seg *newseg);
static int segspt_shmunmap(struct seg *seg, caddr_t raddr, size_t ssize);
static void segspt_shmfree(struct seg *seg);
static faultcode_t segspt_shmfault(struct hat *hat, struct seg *seg,
		caddr_t addr, size_t len, enum fault_type type, enum seg_rw rw);
static faultcode_t segspt_shmfaulta(struct seg *seg, caddr_t addr);
static int segspt_shmsetprot(register struct seg *seg, register caddr_t addr,
			register size_t len, register uint_t prot);
static int segspt_shmcheckprot(struct seg *seg, caddr_t addr, size_t size,
			uint_t prot);
static int	segspt_shmkluster(struct seg *seg, caddr_t addr, ssize_t delta);
static size_t	segspt_shmswapout(struct seg *seg);
static size_t segspt_shmincore(struct seg *seg, caddr_t addr, size_t len,
			register char *vec);
static int segspt_shmsync(struct seg *seg, register caddr_t addr, size_t len,
			int attr, uint_t flags);
static int segspt_shmlockop(struct seg *seg, caddr_t addr, size_t len,
			int attr, int op, ulong_t *lockmap, size_t pos);
static int segspt_shmgetprot(struct seg *seg, caddr_t addr, size_t len,
			uint_t *protv);
static u_offset_t segspt_shmgetoffset(struct seg *seg, caddr_t addr);
static int segspt_shmgettype(struct seg *seg, caddr_t addr);
static int segspt_shmgetvp(struct seg *seg, caddr_t addr, struct vnode **vpp);
static int segspt_shmadvise(struct seg *seg, caddr_t addr, size_t len,
			uint_t behav);
static void segspt_shmdump(struct seg *seg);
static int segspt_shmpagelock(struct seg *, caddr_t, size_t,
			struct page ***, enum lock_type, enum seg_rw);
static int segspt_shmsetpgsz(struct seg *, caddr_t, size_t, uint_t);
static int segspt_shmgetmemid(struct seg *, caddr_t, memid_t *);
static lgrp_mem_policy_info_t *segspt_shmgetpolicy(struct seg *, caddr_t);
static int segspt_shmcapable(struct seg *, segcapability_t);

struct seg_ops segspt_shmops = {
	segspt_shmdup,
	segspt_shmunmap,
	segspt_shmfree,
	segspt_shmfault,
	segspt_shmfaulta,
	segspt_shmsetprot,
	segspt_shmcheckprot,
	segspt_shmkluster,
	segspt_shmswapout,
	segspt_shmsync,
	segspt_shmincore,
	segspt_shmlockop,
	segspt_shmgetprot,
	segspt_shmgetoffset,
	segspt_shmgettype,
	segspt_shmgetvp,
	segspt_shmadvise,	/* advise */
	segspt_shmdump,
	segspt_shmpagelock,
	segspt_shmsetpgsz,
	segspt_shmgetmemid,
	segspt_shmgetpolicy,
	segspt_shmcapable,
};

static void segspt_purge(struct seg *seg);
static int segspt_reclaim(void *, caddr_t, size_t, struct page **,
		enum seg_rw, int);
static int spt_anon_getpages(struct seg *seg, caddr_t addr, size_t len,
		page_t **ppa, int reserve);
static rctl_qty_t spt_unlockedbytes(pgcnt_t npages, page_t **ppa);

/*
 * We need to wait for pending IO to complete to a DISM segment in order for
 * pages to get kicked out of the seg_pcache.  120 seconds should be more
 * than enough time to wait.
 */
static clock_t spt_pcache_wait = 120;

/*ARGSUSED*/
int
sptcreate(size_t size, struct seg **sptseg, struct anon_map *amp,
    uint_t prot, uint_t flags, uint_t share_szc, uint_t advice,
    size_t granule_sz)
{
	int 	err;
	struct  as	*newas;
	struct	segspt_crargs sptcargs;

	if (segspt_minfree == 0)	/* leave min 5% of availrmem for */
		segspt_minfree = availrmem/20;	/* for the system */

	if (!hat_supported(HAT_SHARED_PT, (void *)0))
		return (EINVAL);

	/*
	 * get a new as for this shared memory segment
	 */
	newas = as_alloc(NULL);
	sptcargs.amp = amp;
	sptcargs.prot = prot;
	sptcargs.flags = flags;
	sptcargs.szc = share_szc;
	sptcargs.advice = advice;
	sptcargs.granule_sz = granule_sz;

	/*
	 * create a shared page table (spt) segment
	 */
	if (err = as_map(newas, SEGSPTADDR, size, segspt_create, &sptcargs)) {
		as_free(newas);
		return (err);
	}
	*sptseg = sptcargs.seg_spt;
	return (0);
}

void
sptdestroy(struct as *as, struct anon_map *amp)
{
	(void) as_unmap(as, SEGSPTADDR, amp->size);
	as_free(as);
}

/*
 * called from seg_free().
 * free (i.e., unlock, unmap, return to free list)
 *  all the pages in the given seg.
 */
void
segspt_free(struct seg	*seg)
{
	struct spt_data *sptd = (struct spt_data *)seg->s_data;

	ASSERT(seg->s_as && AS_WRITE_HELD(seg->s_as, &seg->s_as->a_lock));

	if (sptd != NULL) {
		if (sptd->spt_realsize) {
			segspt_free_pages(seg, seg->s_base, sptd->spt_realsize);
		}

		if (sptd->spt_ppa_lckcnt) {
			kmem_free(sptd->spt_ppa_lckcnt,
			    sizeof (*sptd->spt_ppa_lckcnt)
			    * btopr(sptd->spt_amp->size));
		}
		kmem_free(sptd->spt_vp, sizeof (*sptd->spt_vp));
		cv_destroy(&sptd->spt_cv);
		mutex_destroy(&sptd->spt_lock);
		kmem_free(sptd, sizeof (*sptd));
	}
}

/*ARGSUSED*/
static int
segspt_shmsync(struct seg *seg, caddr_t addr, size_t len, int attr,
	uint_t flags)
{
	ASSERT(seg->s_as && AS_LOCK_HELD(seg->s_as, &seg->s_as->a_lock));

	return (0);
}

/*ARGSUSED*/
static size_t
segspt_shmincore(struct seg *seg, caddr_t addr, size_t len, char *vec)
{
	caddr_t	eo_seg;
	pgcnt_t	npages;
	struct shm_data *shmd = (struct shm_data *)seg->s_data;
	struct seg	*sptseg;
	struct spt_data *sptd;

	ASSERT(seg->s_as && AS_LOCK_HELD(seg->s_as, &seg->s_as->a_lock));
#ifdef lint
	seg = seg;
#endif
	sptseg = shmd->shm_sptseg;
	sptd = sptseg->s_data;

	if ((sptd->spt_flags & SHM_PAGEABLE) == 0) {
		eo_seg = addr + len;
		while (addr < eo_seg) {
			/* page exists, and it's locked. */
			*vec++ = SEG_PAGE_INCORE | SEG_PAGE_LOCKED |
			    SEG_PAGE_ANON;
			addr += PAGESIZE;
		}
		return (len);
	} else {
		struct  anon_map *amp = shmd->shm_amp;
		struct  anon	*ap;
		page_t		*pp;
		pgcnt_t 	anon_index;
		struct vnode 	*vp;
		u_offset_t 	off;
		ulong_t		i;
		int		ret;
		anon_sync_obj_t	cookie;

		addr = (caddr_t)((uintptr_t)addr & (uintptr_t)PAGEMASK);
		anon_index = seg_page(seg, addr);
		npages = btopr(len);
		if (anon_index + npages > btopr(shmd->shm_amp->size)) {
			return (EINVAL);
		}
		ANON_LOCK_ENTER(&amp->a_rwlock, RW_READER);
		for (i = 0; i < npages; i++, anon_index++) {
			ret = 0;
			anon_array_enter(amp, anon_index, &cookie);
			ap = anon_get_ptr(amp->ahp, anon_index);
			if (ap != NULL) {
				swap_xlate(ap, &vp, &off);
				anon_array_exit(&cookie);
				pp = page_lookup_nowait(vp, off, SE_SHARED);
				if (pp != NULL) {
					ret |= SEG_PAGE_INCORE | SEG_PAGE_ANON;
					page_unlock(pp);
				}
			} else {
				anon_array_exit(&cookie);
			}
			if (shmd->shm_vpage[anon_index] & DISM_PG_LOCKED) {
				ret |= SEG_PAGE_LOCKED;
			}
			*vec++ = (char)ret;
		}
		ANON_LOCK_EXIT(&amp->a_rwlock);
		return (len);
	}
}

static int
segspt_unmap(struct seg *seg, caddr_t raddr, size_t ssize)
{
	ASSERT(seg->s_as && AS_WRITE_HELD(seg->s_as, &seg->s_as->a_lock));

	/*
	 * seg.s_size may have been rounded up in shmat().
	 * XXX This should be cleanedup. sptdestroy should take a length
	 * argument which should be the same as sptcreate. Then
	 * this rounding would not be needed (or is done in shm.c)
	 * Only the check for full segment will be needed.
	 *
	 * XXX -- shouldn't raddr == 0 always? These tests don't seem
	 * to be useful at all.
	 */
	ssize = P2ROUNDUP(ssize, ism_min_pgsz);

	if (raddr == seg->s_base && ssize == seg->s_size) {
		seg_free(seg);
		return (0);
	} else
		return (EINVAL);
}

struct spt_unlock_npp {
	page_t		**sptunl_ppa;
	struct seg	*sptunl_seg;
	int		sptunl_free;	/* destroy the pages */
};

/*
 * Many of the following are run in parallel tasks. The absolute time
 * of long running loops on multi-CPU systems can be improved by splitting
 * them into several parallel tasks.
 *
 * The granularity is always considered to be 1. This is why these functions
 * need to make sure that they normalize the job so that it can be split
 * this way. The caller can use job_chunk_min argument in vmtask_run_job() to
 * advise the minimal recommended job size to give to each task.
 * VMTASK_SPGS_MINJOB chunk is used for splitting jobs with small page
 * granularity and VMTASK_LPGS_MINJOB for large pages.
 * When free is set pages are freed.
 */
static int
segspt_unlock_npages_task(ulong_t idx, ulong_t end, void *arg,
    ulong_t *ridx)
{
	struct spt_unlock_npp	*sptunl	= arg;
	page_t 			**ppa = sptunl->sptunl_ppa;
	struct seg		*seg = sptunl->sptunl_seg;
	int			free = sptunl->sptunl_free;
	struct spt_data 	*sptd = seg->s_data;
	pgcnt_t 		pgcnt;
	ulong_t 		i;
	const pgcnt_t 		npages = SPT_OSM(sptd) ?
	    page_get_pagecnt(seg->s_szc) : 1;

	ASSERT(IS_P2ALIGNED(idx, npages));

	while (idx < end) {
		page_t	*bpp = ppa[idx];

		if (bpp == NULL) {
			/*
			 * OSM always frees pages in min preferred page size
			 * chunks so we can skip to large page. ISM and DISM
			 * may not have preferred pages at the tail end
			 * depending if the real size is in multiple of large
			 * page size.
			 */
			idx += npages;
			continue;
		}
		if (!free) {
			page_unlock(bpp);
			idx++;
			continue;
		}

		/* Free up a single (possibly large) page */
		ASSERT(PAGE_LOCKED(ppa[idx]));
		pgcnt = page_get_pagecnt(bpp->p_szc);
		ASSERT(IS_P2ALIGNED(idx, pgcnt));
		for (i = 0; i < pgcnt; i++) {
			page_t	*pp = ppa[idx + i];

			if (PAGE_SHARED(pp) && !page_tryupgrade(pp)) {
				vnode_t		*vp = pp->p_vnode;
				u_offset_t	off = pp->p_offset;

				page_unlock(pp);
				pp = page_lookup(vp, off, SE_EXCL);
				if (pp == NULL) {
					panic("page <%p, %llx> not found",
					    (void *)vp, off);
				}
				/*
				 * There is an (extremely small) chance that the
				 * page was relocated behind our back.
				 */
				ppa[idx + i] = pp;
			}
			ASSERT(PAGE_EXCL(pp));
			ASSERT3U(pgcnt, ==, page_get_pagecnt(pp->p_szc));
			if (pp->p_lckcnt) {
				page_pp_unlock(pp, 0, 1);
			}
		}
		if (pgcnt > 1) {
			page_destroy_pages(bpp);
		} else {
			/* LINTED: constant in conditional context */
			VN_DISPOSE(bpp, B_INVAL, 0, kcred);
		}
		idx += pgcnt;
	}

	*ridx = idx;
	return (0);
}

/*
 * seg is segspt not shm seg.
 */
void
segspt_unlock_npages(struct seg *seg, pgcnt_t npages, page_t **ppa, int free)
{
	struct spt_unlock_npp	arg;

	arg.sptunl_free = free;
	arg.sptunl_ppa = ppa;
	arg.sptunl_seg = seg;
	ulong_t job_chunk_min;

	/*
	 * set job_chunk_min to large page count when freeing pages.
	 */
	job_chunk_min = (free == 1) ? page_get_pagecnt(seg->s_szc) :
	    VMTASK_SPGS_MINJOB;

	(void) vmtask_run_job(npages, job_chunk_min,
	    segspt_unlock_npages_task, (void *)&arg, NULL);
}

static int
segspt_pp_lock_npages_task(ulong_t idx, ulong_t end, void *arg, ulong_t *ridx)
{
	page_t	**ppa = (page_t **)arg;
	int	rv = 0;

	for (; idx < end; idx++) {
		if (page_pp_lock(ppa[idx], 0, 1) == 0) {
			rv = ENOMEM;
			break;
		}
	}
	*ridx = idx;

	return (rv);
}

static int
segspt_pp_lock_npages_undo(ulong_t idx, ulong_t end, void *arg, ulong_t
	*ridx)
{
	page_t	**ppa = (page_t **)arg;

	for (; idx < end; idx++)
		page_pp_unlock(ppa[idx], 0, 1);

	*ridx = idx;

	return (0);
}

static int
segspt_pp_lock_npages(pgcnt_t npages, page_t **ppa)
{
	int		rv;
	vmtask_undo_t	undo;

	rv = vmtask_run_job(npages, VMTASK_SPGS_MINJOB,
	    segspt_pp_lock_npages_task, (void *)ppa, &undo);

	if (rv != 0) {
		vmtask_undo(&undo, segspt_pp_lock_npages_undo,
		    (void *)ppa);
	}

	return (rv);
}

struct segspt_ldnp {
	uint_t		hat_flags;
	struct spt_data	*sptd;
	struct seg	*seg;
	pgcnt_t		npages;
	page_t		**ppa;
	pgcnt_t		pgcnt;
	size_t		pgsz;
};

static int
segspt_load_npages_task(ulong_t idx, ulong_t end, void *arg, ulong_t *ridx)
{
	struct segspt_ldnp *targp	= (struct segspt_ldnp *)arg;
	uint_t		hat_flags	= targp->hat_flags;
	struct spt_data	*sptd		= targp->sptd;
	struct seg	*seg		= targp->seg;
	pgcnt_t		npages		= targp->npages;
	page_t		**ppa		= targp->ppa;
	pgcnt_t		pgcnt		= targp->pgcnt;
	size_t		pgsz		= targp->pgsz;
	caddr_t		a		= seg->s_base + idx * pgsz;
	pgcnt_t		pidx		= idx * pgcnt;

	for (; idx < end; idx++, a += pgsz, pidx += pgcnt) {
		hat_memload_array(seg->s_as->a_hat, a, MIN(pgsz, ptob(npages -
		    pidx)), &ppa[pidx], sptd->spt_prot, hat_flags);
	}
	*ridx = idx;

	return (0);
}

static int
segspt_load_npages(pgcnt_t npages, page_t **ppa, struct seg *seg,
    struct spt_data *sptd, uint_t hat_flags)
{
	struct segspt_ldnp	targ;
	ulong_t			job_size;

	targ.pgsz	= page_get_pagesize(seg->s_szc);
	targ.pgcnt	= page_get_pagecnt(seg->s_szc);
	targ.npages	= npages;
	targ.hat_flags	= hat_flags;
	targ.sptd	= sptd;
	targ.seg	= seg;
	targ.ppa	= ppa;
	job_size	= (npages + targ.pgcnt - 1) / targ.pgcnt;

	(void) vmtask_run_job(job_size, VMTASK_LPGS_MINJOB,
	    segspt_load_npages_task, (void *)&targ, NULL);

	return (0);
}

int
segspt_create(struct seg *seg, caddr_t argsp)
{
	int			err;
	caddr_t			addr = seg->s_base;
	struct spt_data		*sptd;
	struct 	segspt_crargs	*sptcargs = (struct segspt_crargs *)argsp;
	struct anon_map 	*amp = sptcargs->amp;
	struct kshmid		*sp = amp->a_sp;
	struct	cred		*cred = CRED();
	ulong_t			anon_index = 0;
	pgcnt_t			npages = btopr(amp->size);
	struct vnode		*vp;
	page_t			**ppa;
	uint_t			hat_flags;
	proc_t			*procp = curproc;
	rctl_qty_t		unlockedbytes = 0;
	kproject_t		*proj;
	uint_t			advice;
	lgrp_mem_policy_t	mem_policy;

	/*
	 * We are holding the a_lock on the underlying dummy as,
	 * so we can make calls to the HAT layer.
	 */
	ASSERT(seg->s_as && AS_WRITE_HELD(seg->s_as, &seg->s_as->a_lock));
	ASSERT(sp != NULL);

	/*
	 * adjust swap only for ISM.
	 */
	if (((sptcargs->flags & SHM_PAGEABLE) == 0) &&
	    (sptcargs->granule_sz == 0)) {
		if (err = anon_swap_adjust(npages))
			return (err);
	}
	err = ENOMEM;

	if ((sptd = kmem_zalloc(sizeof (*sptd), KM_NOSLEEP)) == NULL)
		goto out1;

	if ((sptcargs->flags & SHM_PAGEABLE) == 0) {
		if ((ppa = kmem_zalloc(((sizeof (page_t *)) * npages),
		    KM_NOSLEEP)) == NULL)
			goto out2;
	}

	mutex_init(&sptd->spt_lock, NULL, MUTEX_DEFAULT, NULL);

	if ((vp = kmem_zalloc(sizeof (*vp), KM_NOSLEEP)) == NULL)
		goto out3;

	seg->s_ops = &segspt_ops;
	sptd->spt_vp = vp;
	sptd->spt_amp = amp;
	sptd->spt_prot = sptcargs->prot;
	sptd->spt_flags = sptcargs->flags;
	sptd->spt_granule_sz = sptcargs->granule_sz;
	seg->s_data = (caddr_t)sptd;
	sptd->spt_ppa = NULL;
	sptd->spt_ppa_lckcnt = NULL;
	seg->s_szc = sptcargs->szc;
	cv_init(&sptd->spt_cv, NULL, CV_DEFAULT, NULL);
	sptd->spt_gen = 0;
	advice = sptcargs->advice;
	sptd->spt_advice = advice;

	ANON_LOCK_ENTER(&amp->a_rwlock, RW_WRITER);
	if (seg->s_szc > amp->a_szc) {
		amp->a_szc = seg->s_szc;
	}
	ANON_LOCK_EXIT(&amp->a_rwlock);

	/*
	 * Set policy to affect initial allocation of pages in
	 * anon_map_createpages()
	 */
	mem_policy = lgrp_shmadv_to_policy(advice);
	(void) lgrp_shm_policy_set(mem_policy, amp, anon_index, NULL, 0,
	    ptob(npages));

	if (sptcargs->flags & SHM_PAGEABLE) {
		pgcnt_t new_npgs, more_pgs;
		struct anon_hdr *nahp;
		zone_t *zone;

		if (!IS_P2ALIGNED(amp->size, ism_min_pgsz)) {
			ASSERT(!SPT_OSM(sptd));

			/*
			 * We are rounding up the size of the anon array
			 * on 4 M boundary because we always create 4 M
			 * of page(s) when locking, faulting pages and we
			 * don't have to check for all corner cases e.g.
			 * if there is enough space to allocate 4 M
			 * page.
			 */
			new_npgs = btop(P2ROUNDUP(amp->size, ism_min_pgsz));
			more_pgs = new_npgs - npages;

			/*
			 * The zone will never be NULL, as a fully created
			 * shm always has an owning zone.
			 */
			zone = sp->shm_perm.ipc_zone_ref.zref_zone;
			ASSERT(zone != NULL);
			if (anon_resv_zone(ptob(more_pgs), zone) == 0) {
				err = ENOMEM;
				goto out4;
			}

			nahp = anon_create(new_npgs, ANON_SLEEP);
			ANON_LOCK_ENTER(&amp->a_rwlock, RW_WRITER);
			(void) anon_copy_ptr(amp->ahp, 0, nahp, 0, npages,
			    ANON_SLEEP);
			anon_release(amp->ahp, npages);
			amp->ahp = nahp;
			ASSERT(amp->swresv == ptob(npages));
			amp->swresv = amp->size = ptob(new_npgs);
			ANON_LOCK_EXIT(&amp->a_rwlock);
			npages = new_npgs;
		}

		sptd->spt_ppa_lckcnt = kmem_zalloc(npages *
		    sizeof (*sptd->spt_ppa_lckcnt), KM_SLEEP);
		sptd->spt_pcachecnt = 0;
		sptd->spt_realsize = ptob(npages);
		sptcargs->seg_spt = seg;
		return (0);
	}

	/*
	 * get array of pages for each anon slot in amp
	 */
	if ((err = anon_map_createpages(amp, anon_index, ptob(npages), ppa,
	    seg, addr, S_CREATE, cred)) != 0) {
		segspt_unlock_npages(seg, npages, ppa, 1);
		goto out4;
	}
	mutex_enter(&sp->shm_mlock);

	/* May be partially locked, so, count bytes to charge for locking */
	unlockedbytes = spt_unlockedbytes(npages, ppa);

	proj = sp->shm_perm.ipc_proj;

	if (unlockedbytes > 0) {
		mutex_enter(&procp->p_lock);
		if (rctl_incr_locked_mem(procp, proj, unlockedbytes, 0)) {
			mutex_exit(&procp->p_lock);
			mutex_exit(&sp->shm_mlock);
			segspt_unlock_npages(seg, npages, ppa, 1);
			err = ENOMEM;
			goto out4;
		}
		mutex_exit(&procp->p_lock);
	}

	/*
	 * addr is initial address corresponding to the first page on ppa list
	 */
	if ((err = segspt_pp_lock_npages(npages, ppa)) != 0) {
		segspt_unlock_npages(seg, npages, ppa, 1);
		rctl_decr_locked_mem(NULL, proj, unlockedbytes, 0);
		mutex_exit(&sp->shm_mlock);
		goto out4;
	}
	mutex_exit(&sp->shm_mlock);

	/*
	 * Some platforms assume that ISM mappings are HAT_LOAD_LOCK
	 * for the entire life of the segment. For example platforms
	 * that do not support Dynamic Reconfiguration.
	 */
	hat_flags = HAT_LOAD_SHARE;
	if (!hat_supported(HAT_DYNAMIC_ISM_UNMAP, NULL))
		hat_flags |= HAT_LOAD_LOCK;

	/*
	 * Load translations one lare page at a time
	 * to make sure we don't create mappings bigger than
	 * segment's size code in case underlying pages
	 * are shared with segvn's segment that uses bigger
	 * size code than we do.
	 */
	(void) segspt_load_npages(npages, ppa, seg, sptd, hat_flags);

	/*
	 * On platforms that do not support HAT_DYNAMIC_ISM_UNMAP,
	 * we will leave the pages locked SE_SHARED for the life
	 * of the ISM segment. This will prevent any calls to
	 * hat_pageunload() on this ISM segment for those platforms.
	 */
	if (!(hat_flags & HAT_LOAD_LOCK)) {
		/*
		 * On platforms that support HAT_DYNAMIC_ISM_UNMAP,
		 * we no longer need to hold the SE_SHARED lock on the pages,
		 * since L_PAGELOCK and F_SOFTLOCK calls will grab the
		 * SE_SHARED lock on the pages as necessary.
		 */
		segspt_unlock_npages(seg, npages, ppa, 0);
	}
	sptd->spt_pcachecnt = 0;
	kmem_free(ppa, ((sizeof (page_t *)) * npages));
	sptd->spt_realsize = ptob(npages);
	atomic_add_long(&spt_used, npages);
	sptcargs->seg_spt = seg;
	return (0);

out4:
	seg->s_data = NULL;
	kmem_free(vp, sizeof (*vp));
	cv_destroy(&sptd->spt_cv);
out3:
	mutex_destroy(&sptd->spt_lock);
	if ((sptcargs->flags & SHM_PAGEABLE) == 0)
		kmem_free(ppa, (sizeof (*ppa) * npages));
out2:
	kmem_free(sptd, sizeof (*sptd));
out1:
	if ((sptcargs->flags & SHM_PAGEABLE) == 0)
		anon_swap_restore(npages);
	return (err);
}

struct segspt_frp {
	uint64_t	unlocked_bytes;
	struct spt_data	*sptd;
	struct anon_map	*amp;
	uint_t		hat_flags;
};
static int segspt_free_pages_task(ulong_t, ulong_t, void *, ulong_t *);

/*ARGSUSED*/
void
segspt_free_pages(struct seg *seg, caddr_t addr, size_t len)
{
	struct spt_data	*sptd = (struct spt_data *)seg->s_data;
	pgcnt_t		npages;
	struct anon_map	*amp;
	uint_t		hat_flags;
	rctl_qty_t	unlocked_bytes = 0;
	kproject_t	*proj;
	kshmid_t	*sp;
	ulong_t		job_chunk_min;
	struct segspt_frp targ;

	ASSERT(seg->s_as && AS_WRITE_HELD(seg->s_as, &seg->s_as->a_lock));

	len = P2ROUNDUP(len, PAGESIZE);

	npages = btop(len);

	hat_flags = HAT_UNLOAD_UNLOCK | HAT_UNLOAD_UNMAP;
	if ((hat_supported(HAT_DYNAMIC_ISM_UNMAP, (void *)0)) ||
	    (sptd->spt_flags & SHM_PAGEABLE)) {
		hat_flags = HAT_UNLOAD_UNMAP;
	}

	hat_unload(seg->s_as->a_hat, addr, len, hat_flags);

	amp = sptd->spt_amp;
	if (sptd->spt_flags & SHM_PAGEABLE)
		npages = btop(amp->size);

	ASSERT(amp != NULL);

	if ((sptd->spt_flags & SHM_PAGEABLE) == 0) {
		sp = amp->a_sp;
		proj = sp->shm_perm.ipc_proj;
		mutex_enter(&sp->shm_mlock);
	}

	targ.unlocked_bytes	= 0;
	targ.sptd		= sptd;
	targ.amp		= amp;
	targ.hat_flags		= hat_flags;

	/*
	 * Use small page granularity with min chunk equal to the number of
	 * pages in the largest page.
	 */
	job_chunk_min = page_get_pagecnt(seg->s_szc);

	if (VMTASK_SPGS_MINJOB > job_chunk_min)
		job_chunk_min = VMTASK_SPGS_MINJOB;
	(void) vmtask_run_job(npages, job_chunk_min,
	    segspt_free_pages_task, (void *)&targ, NULL);
	unlocked_bytes = targ.unlocked_bytes;

	if ((sptd->spt_flags & SHM_PAGEABLE) == 0) {
		if (unlocked_bytes > 0)
			rctl_decr_locked_mem(NULL, proj, unlocked_bytes, 0);
		mutex_exit(&sp->shm_mlock);
	}

	/*
	 * mark that pages have been released
	 */
	sptd->spt_realsize = 0;

	if ((sptd->spt_flags & SHM_PAGEABLE) == 0) {
		atomic_add_long(&spt_used, -npages);
		anon_swap_restore(npages);
	}
}

static int
segspt_free_pages_task(ulong_t idx, ulong_t end, void *arg, ulong_t *ridx)
{
	struct segspt_frp *targp = (struct segspt_frp *)arg;
	struct page	*pp;
	struct spt_data	*sptd = targp->sptd;
	ulong_t		anon_idx = idx;
	struct anon_map	*amp = targp->amp;
	struct anon	*ap;
	struct vnode	*vp;
	u_offset_t	off;
	uint_t		hat_flags = targp->hat_flags;
	int		root = 0;
	pgcnt_t		pgs, curnpgs = 0;
	page_t		*rootpp;
	uint64_t	unlocked_bytes = 0;
	pgcnt_t		unlocked_pgs = 0;

	for (; anon_idx < end; anon_idx++) {
		if ((sptd->spt_flags & SHM_PAGEABLE) == 0) {
			if ((ap = anon_get_ptr(amp->ahp, anon_idx)) == NULL) {
				panic("segspt_free_pages: null app");
				/*NOTREACHED*/
			}
		} else {
			if ((ap = anon_get_next_ptr(amp->ahp, &anon_idx, end))
			    == NULL)
				continue;
		}
		ASSERT(ANON_ISBUSY(anon_get_slot(amp->ahp, anon_idx)) == 0);
		swap_xlate(ap, &vp, &off);

		/*
		 * If this platform supports HAT_DYNAMIC_ISM_UNMAP,
		 * the pages won't be having SE_SHARED lock at this
		 * point.
		 *
		 * On platforms that do not support HAT_DYNAMIC_ISM_UNMAP,
		 * the pages are still held SE_SHARED locked from the
		 * original segspt_create()
		 *
		 * Our goal is to get SE_EXCL lock on each page, remove
		 * permanent lock on it and invalidate the page.
		 */
		if ((sptd->spt_flags & SHM_PAGEABLE) == 0) {
			if (hat_flags == HAT_UNLOAD_UNMAP)
				pp = page_lookup(vp, off, SE_EXCL);
			else {
				if ((pp = page_find(vp, off)) == NULL) {
					panic("segspt_free_pages: "
					    "page not locked");
					/*NOTREACHED*/
				}
				if (!page_tryupgrade(pp)) {
					page_unlock(pp);
					pp = page_lookup(vp, off, SE_EXCL);
				}
			}
			if (pp == NULL) {
				panic("segspt_free_pages: "
				    "page not in the system");
				/*NOTREACHED*/
			}
			ASSERT(pp->p_lckcnt > 0);
			page_pp_unlock(pp, 0, 1);
			if (pp->p_lckcnt == 0)
				unlocked_bytes += PAGESIZE;
		} else {
			if ((pp = page_lookup(vp, off, SE_EXCL)) == NULL) {
				continue;
			}
			if (SPT_OSM(sptd)) {
				if (pp->p_lckcnt > 0) {
					pp->p_lckcnt = 0;
					unlocked_pgs++;
				}
			}
		}
		/*
		 * It's logical to invalidate the pages here as in most cases
		 * these were created by segspt.
		 */
		if (pp->p_szc != 0) {
			if (root == 0) {
				ASSERT(curnpgs == 0);
				root = 1;
				rootpp = pp;
				pgs = curnpgs = page_get_pagecnt(pp->p_szc);
				ASSERT(pgs > 1);
				ASSERT(IS_P2ALIGNED(pgs, pgs));
				ASSERT(!(page_pptonum(pp) & (pgs - 1)));
				curnpgs--;
			} else if ((page_pptonum(pp) & (pgs - 1)) == pgs - 1) {
				ASSERT(curnpgs == 1);
				ASSERT(page_pptonum(pp) ==
				    page_pptonum(rootpp) + (pgs - 1));
				page_destroy_pages(rootpp);
				root = 0;
				curnpgs = 0;
			} else {
				ASSERT(curnpgs > 1);
				ASSERT(page_pptonum(pp) ==
				    page_pptonum(rootpp) + (pgs - curnpgs));
				curnpgs--;
			}
		} else {
			if (root != 0 || curnpgs != 0) {
				panic("segspt_free_pages: bad large page");
				/*NOTREACHED*/
			}
			/*
			 * Before destroying the pages, we need to take care
			 * of the rctl locked memory accounting. For that
			 * we need to calculte the unlocked_bytes.
			 */
			if (pp->p_lckcnt > 0)
				unlocked_bytes += PAGESIZE;
			/*LINTED: constant in conditional context */
			VN_DISPOSE(pp, B_INVAL, 0, kcred);
		}
	}

	if (root != 0 || curnpgs != 0) {
		panic("segspt_free_pages: bad large page");
		/*NOTREACHED*/
	}

	if (unlocked_pgs) {
		mutex_enter(&freemem_lock);
		availrmem += unlocked_pgs;
		pages_locked -= unlocked_pgs;
		mutex_exit(&freemem_lock);
	}

	atomic_add_64(&targp->unlocked_bytes, unlocked_bytes);
	*ridx = end;

	return (0);
}

/*
 * Get memory allocation policy info for specified address in given segment
 */
static lgrp_mem_policy_info_t *
segspt_getpolicy(struct seg *seg, caddr_t addr)
{
	struct anon_map		*amp;
	ulong_t			anon_index;
	lgrp_mem_policy_info_t	*policy_info;
	struct spt_data		*spt_data;

	ASSERT(seg != NULL);

	/*
	 * Get anon_map from segspt
	 *
	 * Assume that no lock needs to be held on anon_map, since
	 * it should be protected by its reference count which must be
	 * nonzero for an existing segment
	 * Need to grab readers lock on policy tree though
	 */
	spt_data = (struct spt_data *)seg->s_data;
	if (spt_data == NULL)
		return (NULL);
	amp = spt_data->spt_amp;
	ASSERT(amp->refcnt != 0);

	/*
	 * Get policy info
	 *
	 * Assume starting anon index of 0
	 */
	anon_index = seg_page(seg, addr);
	policy_info = lgrp_shm_policy_get(amp, anon_index, NULL, 0);

	return (policy_info);
}

/*
 * DISM only.
 * Return locked pages over a given range.
 *
 * We will cache all DISM locked pages and save the pplist for the
 * entire segment in the ppa field of the underlying DISM segment structure.
 * Later, during a call to segspt_reclaim() we will use this ppa array
 * to page_unlock() all of the pages and then we will free this ppa list.
 */
/*ARGSUSED*/
static int
segspt_dismpagelock(struct seg *seg, caddr_t addr, size_t len,
    struct page ***ppp, enum lock_type type, enum seg_rw rw)
{
	struct  shm_data *shmd = (struct shm_data *)seg->s_data;
	struct  seg	*sptseg = shmd->shm_sptseg;
	struct  spt_data *sptd = sptseg->s_data;
	pgcnt_t pg_idx, npages, tot_npages, npgs;
	struct  page **pplist, **pl, **ppa, *pp;
	struct  anon_map *amp;
	spgcnt_t	an_idx;
	int 	ret = ENOTSUP;
	uint_t	pl_built = 0;
	struct  anon *ap;
	struct  vnode *vp;
	u_offset_t off;
	pgcnt_t claim_availrmem = 0;
	uint_t	szc;

	ASSERT(seg->s_as && AS_LOCK_HELD(seg->s_as, &seg->s_as->a_lock));
	ASSERT(type == L_PAGELOCK || type == L_PAGEUNLOCK);

	/*
	 * We want to lock/unlock the entire ISM segment. Therefore,
	 * we will be using the underlying sptseg and it's base address
	 * and length for the caching arguments.
	 */
	ASSERT(sptseg);
	ASSERT(sptd);

	pg_idx = seg_page(seg, addr);
	npages = btopr(len);


	/*
	 * check if the request is larger than number of pages covered
	 * by amp
	 */
	if (pg_idx + npages > btopr(sptd->spt_amp->size)) {
		*ppp = NULL;
		return (ENOTSUP);
	}

	if (type == L_PAGEUNLOCK) {
		ASSERT(sptd->spt_ppa != NULL);

		seg_pinactive(seg, NULL, seg->s_base, sptd->spt_amp->size,
		    sptd->spt_ppa, S_WRITE, SEGP_FORCE_WIRED, segspt_reclaim);

		/*
		 * If someone is blocked while unmapping, we purge
		 * segment page cache and thus reclaim pplist synchronously
		 * without waiting for seg_pasync_thread. This speeds up
		 * unmapping in cases where munmap(2) is called, while
		 * raw async i/o is still in progress or where a thread
		 * exits on data fault in a multithreaded application.
		 */
		if ((sptd->spt_flags & DISM_PPA_CHANGED) ||
		    (AS_ISUNMAPWAIT(seg->s_as) &&
		    shmd->shm_softlockcnt > 0)) {
			segspt_purge(seg);
		}
		return (0);
	}

	/* The L_PAGELOCK case ... */

	if (sptd->spt_flags & (DISM_PPA_CHANGED | OSM_MEM_FREED)) {
		segspt_purge(seg);
		/*
		 * for DISM ppa needs to be rebuild since
		 * number of locked pages could be changed
		 */
		*ppp = NULL;
		return (ENOTSUP);
	}

	/*
	 * First try to find pages in segment page cache, without
	 * holding the segment lock.
	 */
	pplist = seg_plookup(seg, NULL, seg->s_base, sptd->spt_amp->size,
	    S_WRITE, SEGP_FORCE_WIRED);
	if (pplist != NULL) {
		ASSERT(sptd->spt_ppa != NULL);
		ASSERT(sptd->spt_ppa == pplist);
		ppa = sptd->spt_ppa;
		for (an_idx = pg_idx; an_idx < pg_idx + npages; ) {
			if (ppa[an_idx] == NULL) {
				seg_pinactive(seg, NULL, seg->s_base,
				    sptd->spt_amp->size, ppa,
				    S_WRITE, SEGP_FORCE_WIRED, segspt_reclaim);
				*ppp = NULL;
				return (ENOTSUP);
			}
			if ((szc = ppa[an_idx]->p_szc) != 0) {
				npgs = page_get_pagecnt(szc);
				an_idx = P2ROUNDUP(an_idx + 1, npgs);
			} else {
				an_idx++;
			}
		}
		/*
		 * Since we cache the entire DISM segment, we want to
		 * set ppp to point to the first slot that corresponds
		 * to the requested addr, i.e. pg_idx.
		 */
		*ppp = &(sptd->spt_ppa[pg_idx]);
		return (0);
	}

	mutex_enter(&sptd->spt_lock);
	if (sptd->spt_flags & (DISM_PPA_CHANGED | OSM_MEM_FREED)) {
		mutex_exit(&sptd->spt_lock);
		segspt_purge(seg);
		*ppp = NULL;
		return (ENOTSUP);
	}

	/*
	 * try to find pages in segment page cache with mutex
	 */
	pplist = seg_plookup(seg, NULL, seg->s_base, sptd->spt_amp->size,
	    S_WRITE, SEGP_FORCE_WIRED);
	if (pplist != NULL) {
		ASSERT(sptd->spt_ppa != NULL);
		ASSERT(sptd->spt_ppa == pplist);
		ppa = sptd->spt_ppa;
		for (an_idx = pg_idx; an_idx < pg_idx + npages; ) {
			if (ppa[an_idx] == NULL) {
				mutex_exit(&sptd->spt_lock);
				seg_pinactive(seg, NULL, seg->s_base,
				    sptd->spt_amp->size, ppa,
				    S_WRITE, SEGP_FORCE_WIRED, segspt_reclaim);
				*ppp = NULL;
				return (ENOTSUP);
			}
			if ((szc = ppa[an_idx]->p_szc) != 0) {
				npgs = page_get_pagecnt(szc);
				an_idx = P2ROUNDUP(an_idx + 1, npgs);
			} else {
				an_idx++;
			}
		}
		/*
		 * Since we cache the entire DISM segment, we want to
		 * set ppp to point to the first slot that corresponds
		 * to the requested addr, i.e. pg_idx.
		 */
		mutex_exit(&sptd->spt_lock);
		*ppp = &(sptd->spt_ppa[pg_idx]);
		return (0);
	}
	if (seg_pinsert_check(seg, NULL, seg->s_base, sptd->spt_amp->size,
	    SEGP_FORCE_WIRED) == SEGP_FAIL) {
		mutex_exit(&sptd->spt_lock);
		*ppp = NULL;
		return (ENOTSUP);
	}

	/*
	 * No need to worry about protections because DISM pages are always rw.
	 */
	pl = pplist = NULL;
	amp = sptd->spt_amp;

	/*
	 * Do we need to build the ppa array?
	 */
	if (sptd->spt_ppa == NULL) {
		pgcnt_t lpg_cnt = 0;

		pl_built = 1;
		tot_npages = btopr(sptd->spt_amp->size);

		ASSERT(sptd->spt_pcachecnt == 0);
		pplist = kmem_zalloc(sizeof (page_t *) * tot_npages, KM_SLEEP);
		pl = pplist;

		ANON_LOCK_ENTER(&amp->a_rwlock, RW_WRITER);
		for (an_idx = 0; an_idx < tot_npages; ) {
			ap = anon_get_ptr(amp->ahp, an_idx);
			/*
			 * Cache only mlocked pages. For large pages
			 * if one (constituent) page is mlocked
			 * all pages for that large page
			 * are cached also. This is for quick
			 * lookups of ppa array;
			 */
			if ((ap != NULL) && (lpg_cnt != 0 ||
			    (sptd->spt_ppa_lckcnt[an_idx] != 0))) {

				swap_xlate(ap, &vp, &off);
				pp = page_lookup(vp, off, SE_SHARED);
				ASSERT(pp != NULL);
				if (lpg_cnt == 0) {
					lpg_cnt++;
					/*
					 * For a small page, we are done --
					 * lpg_cnt is reset to 0 below.
					 *
					 * For a large page, we are guaranteed
					 * to find the anon structures of all
					 * constituent pages and a non-zero
					 * lpg_cnt ensures that we don't test
					 * for mlock for these. We are done
					 * when lpg_cnt reaches (npgs + 1).
					 * If we are not the first constituent
					 * page, restart at the first one.
					 */
					npgs = page_get_pagecnt(pp->p_szc);
					if (!IS_P2ALIGNED(an_idx, npgs)) {
						an_idx = P2ALIGN(an_idx, npgs);
						page_unlock(pp);
						continue;
					}
				}
				if (++lpg_cnt > npgs)
					lpg_cnt = 0;

				/*
				 * availrmem is decremented only
				 * for unlocked pages
				 */
				if (sptd->spt_ppa_lckcnt[an_idx] == 0)
					claim_availrmem++;
				pplist[an_idx] = pp;
			}
			an_idx++;
		}
		ANON_LOCK_EXIT(&amp->a_rwlock);

		if (claim_availrmem) {
			mutex_enter(&freemem_lock);
			if (availrmem < tune.t_minarmem + claim_availrmem) {
				mutex_exit(&freemem_lock);
				ret = ENOTSUP;
				claim_availrmem = 0;
				goto insert_fail;
			} else {
				availrmem -= claim_availrmem;
			}
			mutex_exit(&freemem_lock);
		}

		sptd->spt_ppa = pl;
	} else {
		/*
		 * We already have a valid ppa[].
		 */
		pl = sptd->spt_ppa;
	}

	ASSERT(pl != NULL);

	ret = seg_pinsert(seg, NULL, seg->s_base, sptd->spt_amp->size,
	    sptd->spt_amp->size, pl, S_WRITE, SEGP_FORCE_WIRED,
	    segspt_reclaim);
	if (ret == SEGP_FAIL) {
		/*
		 * seg_pinsert failed. We return
		 * ENOTSUP, so that the as_pagelock() code will
		 * then try the slower F_SOFTLOCK path.
		 */
		if (pl_built) {
			/*
			 * No one else has referenced the ppa[].
			 * We created it and we need to destroy it.
			 */
			sptd->spt_ppa = NULL;
		}
		ret = ENOTSUP;
		goto insert_fail;
	}

	/*
	 * In either case, we increment softlockcnt on the 'real' segment.
	 */
	sptd->spt_pcachecnt++;
	atomic_add_long((ulong_t *)(&(shmd->shm_softlockcnt)), 1);

	ppa = sptd->spt_ppa;
	for (an_idx = pg_idx; an_idx < pg_idx + npages; ) {
		if (ppa[an_idx] == NULL) {
			mutex_exit(&sptd->spt_lock);
			seg_pinactive(seg, NULL, seg->s_base,
			    sptd->spt_amp->size,
			    pl, S_WRITE, SEGP_FORCE_WIRED, segspt_reclaim);
			*ppp = NULL;
			return (ENOTSUP);
		}
		if ((szc = ppa[an_idx]->p_szc) != 0) {
			npgs = page_get_pagecnt(szc);
			an_idx = P2ROUNDUP(an_idx + 1, npgs);
		} else {
			an_idx++;
		}
	}
	/*
	 * We can now drop the sptd->spt_lock since the ppa[]
	 * exists and he have incremented pacachecnt.
	 */
	mutex_exit(&sptd->spt_lock);

	/*
	 * Since we cache the entire segment, we want to
	 * set ppp to point to the first slot that corresponds
	 * to the requested addr, i.e. pg_idx.
	 */
	*ppp = &(sptd->spt_ppa[pg_idx]);
	return (0);

insert_fail:
	/*
	 * We will only reach this code if we tried and failed.
	 *
	 * And we can drop the lock on the dummy seg, once we've failed
	 * to set up a new ppa[].
	 */
	mutex_exit(&sptd->spt_lock);

	if (pl_built) {
		if (claim_availrmem) {
			mutex_enter(&freemem_lock);
			availrmem += claim_availrmem;
			mutex_exit(&freemem_lock);
		}

		/*
		 * We created pl and we need to destroy it.
		 */
		pplist = pl;
		for (an_idx = 0; an_idx < tot_npages; an_idx++) {
			if (pplist[an_idx] != NULL)
				page_unlock(pplist[an_idx]);
		}
		kmem_free(pl, sizeof (page_t *) * tot_npages);
	}

	if (shmd->shm_softlockcnt <= 0) {
		if (AS_ISUNMAPWAIT(seg->s_as)) {
			mutex_enter(&seg->s_as->a_contents);
			if (AS_ISUNMAPWAIT(seg->s_as)) {
				AS_CLRUNMAPWAIT(seg->s_as);
				cv_broadcast(&seg->s_as->a_cv);
			}
			mutex_exit(&seg->s_as->a_contents);
		}
	}
	*ppp = NULL;
	return (ret);
}



/*
 * return locked pages over a given range.
 *
 * We will cache the entire ISM segment and save the pplist for the
 * entire segment in the ppa field of the underlying ISM segment structure.
 * Later, during a call to segspt_reclaim() we will use this ppa array
 * to page_unlock() all of the pages and then we will free this ppa list.
 */
/*ARGSUSED*/
static int
segspt_shmpagelock(struct seg *seg, caddr_t addr, size_t len,
    struct page ***ppp, enum lock_type type, enum seg_rw rw)
{
	struct shm_data *shmd = (struct shm_data *)seg->s_data;
	struct seg	*sptseg = shmd->shm_sptseg;
	struct spt_data *sptd = sptseg->s_data;
	pgcnt_t np, page_index, npages;
	caddr_t a, spt_base;
	struct page **pplist, **pl, *pp;
	struct anon_map *amp;
	ulong_t anon_index;
	int ret = ENOTSUP;
	uint_t	pl_built = 0;
	struct anon *ap;
	struct vnode *vp;
	u_offset_t off;

	ASSERT(seg->s_as && AS_LOCK_HELD(seg->s_as, &seg->s_as->a_lock));
	ASSERT(type == L_PAGELOCK || type == L_PAGEUNLOCK);


	/*
	 * We want to lock/unlock the entire ISM segment. Therefore,
	 * we will be using the underlying sptseg and it's base address
	 * and length for the caching arguments.
	 */
	ASSERT(sptseg);
	ASSERT(sptd);

	if (sptd->spt_flags & SHM_PAGEABLE) {
		return (segspt_dismpagelock(seg, addr, len, ppp, type, rw));
	}

	page_index = seg_page(seg, addr);
	npages = btopr(len);

	/*
	 * check if the request is larger than number of pages covered
	 * by amp
	 */
	if (page_index + npages > btopr(sptd->spt_amp->size)) {
		*ppp = NULL;
		return (ENOTSUP);
	}

	if (type == L_PAGEUNLOCK) {

		ASSERT(sptd->spt_ppa != NULL);

		seg_pinactive(seg, NULL, seg->s_base, sptd->spt_amp->size,
		    sptd->spt_ppa, S_WRITE, SEGP_FORCE_WIRED, segspt_reclaim);

		/*
		 * If someone is blocked while unmapping, we purge
		 * segment page cache and thus reclaim pplist synchronously
		 * without waiting for seg_pasync_thread. This speeds up
		 * unmapping in cases where munmap(2) is called, while
		 * raw async i/o is still in progress or where a thread
		 * exits on data fault in a multithreaded application.
		 */
		if (AS_ISUNMAPWAIT(seg->s_as) && (shmd->shm_softlockcnt > 0)) {
			segspt_purge(seg);
		}
		return (0);
	}

	/* The L_PAGELOCK case... */

	/*
	 * First try to find pages in segment page cache, without
	 * holding the segment lock.
	 */
	pplist = seg_plookup(seg, NULL, seg->s_base, sptd->spt_amp->size,
	    S_WRITE, SEGP_FORCE_WIRED);
	if (pplist != NULL) {
		ASSERT(sptd->spt_ppa == pplist);
		ASSERT(sptd->spt_ppa[page_index]);
		/*
		 * Since we cache the entire ISM segment, we want to
		 * set ppp to point to the first slot that corresponds
		 * to the requested addr, i.e. page_index.
		 */
		*ppp = &(sptd->spt_ppa[page_index]);
		return (0);
	}

	mutex_enter(&sptd->spt_lock);

	/*
	 * try to find pages in segment page cache
	 */
	pplist = seg_plookup(seg, NULL, seg->s_base, sptd->spt_amp->size,
	    S_WRITE, SEGP_FORCE_WIRED);
	if (pplist != NULL) {
		ASSERT(sptd->spt_ppa == pplist);
		/*
		 * Since we cache the entire segment, we want to
		 * set ppp to point to the first slot that corresponds
		 * to the requested addr, i.e. page_index.
		 */
		mutex_exit(&sptd->spt_lock);
		*ppp = &(sptd->spt_ppa[page_index]);
		return (0);
	}

	if (seg_pinsert_check(seg, NULL, seg->s_base, sptd->spt_amp->size,
	    SEGP_FORCE_WIRED) == SEGP_FAIL) {
		mutex_exit(&sptd->spt_lock);
		*ppp = NULL;
		return (ENOTSUP);
	}

	/*
	 * No need to worry about protections because ISM pages
	 * are always rw.
	 */
	pl = pplist = NULL;

	/*
	 * Do we need to build the ppa array?
	 */
	if (sptd->spt_ppa == NULL) {
		ASSERT(sptd->spt_ppa == pplist);

		spt_base = sptseg->s_base;
		pl_built = 1;

		/*
		 * availrmem is decremented once during anon_swap_adjust()
		 * and is incremented during the anon_unresv(), which is
		 * called from shm_rm_amp() when the segment is destroyed.
		 */
		amp = sptd->spt_amp;
		ASSERT(amp != NULL);

		/* pcachecnt is protected by sptd->spt_lock */
		ASSERT(sptd->spt_pcachecnt == 0);
		pplist = kmem_zalloc(sizeof (page_t *)
		    * btopr(sptd->spt_amp->size), KM_SLEEP);
		pl = pplist;

		anon_index = seg_page(sptseg, spt_base);

		ANON_LOCK_ENTER(&amp->a_rwlock, RW_WRITER);
		for (a = spt_base; a < (spt_base + sptd->spt_amp->size);
		    a += PAGESIZE, anon_index++, pplist++) {
			ap = anon_get_ptr(amp->ahp, anon_index);
			ASSERT(ap != NULL);
			swap_xlate(ap, &vp, &off);
			pp = page_lookup(vp, off, SE_SHARED);
			ASSERT(pp != NULL);
			*pplist = pp;
		}
		ANON_LOCK_EXIT(&amp->a_rwlock);

		if (a < (spt_base + sptd->spt_amp->size)) {
			ret = ENOTSUP;
			goto insert_fail;
		}
		sptd->spt_ppa = pl;
	} else {
		/*
		 * We already have a valid ppa[].
		 */
		pl = sptd->spt_ppa;
	}

	ASSERT(pl != NULL);

	ret = seg_pinsert(seg, NULL, seg->s_base, sptd->spt_amp->size,
	    sptd->spt_amp->size, pl, S_WRITE, SEGP_FORCE_WIRED,
	    segspt_reclaim);
	if (ret == SEGP_FAIL) {
		/*
		 * seg_pinsert failed. We return
		 * ENOTSUP, so that the as_pagelock() code will
		 * then try the slower F_SOFTLOCK path.
		 */
		if (pl_built) {
			/*
			 * No one else has referenced the ppa[].
			 * We created it and we need to destroy it.
			 */
			sptd->spt_ppa = NULL;
		}
		ret = ENOTSUP;
		goto insert_fail;
	}

	/*
	 * In either case, we increment softlockcnt on the 'real' segment.
	 */
	sptd->spt_pcachecnt++;
	atomic_add_long((ulong_t *)(&(shmd->shm_softlockcnt)), 1);

	/*
	 * We can now drop the sptd->spt_lock since the ppa[]
	 * exists and he have incremented pacachecnt.
	 */
	mutex_exit(&sptd->spt_lock);

	/*
	 * Since we cache the entire segment, we want to
	 * set ppp to point to the first slot that corresponds
	 * to the requested addr, i.e. page_index.
	 */
	*ppp = &(sptd->spt_ppa[page_index]);
	return (0);

insert_fail:
	/*
	 * We will only reach this code if we tried and failed.
	 *
	 * And we can drop the lock on the dummy seg, once we've failed
	 * to set up a new ppa[].
	 */
	mutex_exit(&sptd->spt_lock);

	if (pl_built) {
		/*
		 * We created pl and we need to destroy it.
		 */
		pplist = pl;
		np = (((uintptr_t)(a - spt_base)) >> PAGESHIFT);
		while (np) {
			page_unlock(*pplist);
			np--;
			pplist++;
		}
		kmem_free(pl, sizeof (page_t *) * btopr(sptd->spt_amp->size));
	}
	if (shmd->shm_softlockcnt <= 0) {
		if (AS_ISUNMAPWAIT(seg->s_as)) {
			mutex_enter(&seg->s_as->a_contents);
			if (AS_ISUNMAPWAIT(seg->s_as)) {
				AS_CLRUNMAPWAIT(seg->s_as);
				cv_broadcast(&seg->s_as->a_cv);
			}
			mutex_exit(&seg->s_as->a_contents);
		}
	}
	*ppp = NULL;
	return (ret);
}

/*
 * purge any cached pages in the I/O page cache
 */
static void
segspt_purge(struct seg *seg)
{
	seg_ppurge(seg, NULL, SEGP_FORCE_WIRED);
}

static int
segspt_reclaim(void *ptag, caddr_t addr, size_t len, struct page **pplist,
	enum seg_rw rw, int async)
{
	struct seg *seg = (struct seg *)ptag;
	struct	shm_data *shmd = (struct shm_data *)seg->s_data;
	struct	seg	*sptseg;
	struct	spt_data *sptd;
	pgcnt_t npages, i, free_availrmem = 0;
	int	done = 0;

#ifdef lint
	addr = addr;
#endif
	sptseg = shmd->shm_sptseg;
	sptd = sptseg->s_data;
	npages = (len >> PAGESHIFT);
	ASSERT(npages);
	ASSERT(sptd->spt_pcachecnt != 0);
	ASSERT(sptd->spt_ppa == pplist);
	ASSERT(npages == btopr(sptd->spt_amp->size));
	ASSERT(async || AS_LOCK_HELD(seg->s_as, &seg->s_as->a_lock));

	/*
	 * Acquire the lock on the dummy seg and destroy the
	 * ppa array IF this is the last pcachecnt.
	 */
	mutex_enter(&sptd->spt_lock);
	if (--sptd->spt_pcachecnt == 0) {
		for (i = 0; i < npages; i++) {
			if (pplist[i] == NULL) {
				continue;
			}
			if (rw == S_WRITE) {
				hat_setrefmod(pplist[i]);
			} else {
				hat_setref(pplist[i]);
			}
			if ((sptd->spt_flags & SHM_PAGEABLE) &&
			    (sptd->spt_ppa_lckcnt[i] == 0)) {
				free_availrmem++;
			}
			page_unlock(pplist[i]);
		}
		if ((sptd->spt_flags & SHM_PAGEABLE) && free_availrmem) {
			mutex_enter(&freemem_lock);
			availrmem += free_availrmem;
			mutex_exit(&freemem_lock);
		}
		/*
		 * Since we want to cach/uncache the entire ISM segment,
		 * we will track the pplist in a segspt specific field
		 * ppa, that is initialized at the time we add an entry to
		 * the cache.
		 */
		ASSERT(sptd->spt_pcachecnt == 0);
		kmem_free(pplist, sizeof (page_t *) * npages);
		sptd->spt_ppa = NULL;
		sptd->spt_flags &= ~DISM_PPA_CHANGED;
		sptd->spt_gen++;
		cv_broadcast(&sptd->spt_cv);
		done = 1;
	}
	mutex_exit(&sptd->spt_lock);

	/*
	 * If we are pcache async thread or called via seg_ppurge_wiredpp() we
	 * may not hold AS lock (in this case async argument is not 0). This
	 * means if softlockcnt drops to 0 after the decrement below address
	 * space may get freed. We can't allow it since after softlock
	 * derement to 0 we still need to access as structure for possible
	 * wakeup of unmap waiters. To prevent the disappearance of as we take
	 * this segment's shm_segfree_syncmtx. segspt_shmfree() also takes
	 * this mutex as a barrier to make sure this routine completes before
	 * segment is freed.
	 *
	 * The second complication we have to deal with in async case is a
	 * possibility of missed wake up of unmap wait thread. When we don't
	 * hold as lock here we may take a_contents lock before unmap wait
	 * thread that was first to see softlockcnt was still not 0. As a
	 * result we'll fail to wake up an unmap wait thread. To avoid this
	 * race we set nounmapwait flag in as structure if we drop softlockcnt
	 * to 0 if async is not 0.  unmapwait thread
	 * will not block if this flag is set.
	 */
	if (async)
		mutex_enter(&shmd->shm_segfree_syncmtx);

	/*
	 * Now decrement softlockcnt.
	 */
	ASSERT(shmd->shm_softlockcnt > 0);
	atomic_add_long((ulong_t *)(&(shmd->shm_softlockcnt)), -1);

	if (shmd->shm_softlockcnt <= 0) {
		if (async || AS_ISUNMAPWAIT(seg->s_as)) {
			mutex_enter(&seg->s_as->a_contents);
			if (async)
				AS_SETNOUNMAPWAIT(seg->s_as);
			if (AS_ISUNMAPWAIT(seg->s_as)) {
				AS_CLRUNMAPWAIT(seg->s_as);
				cv_broadcast(&seg->s_as->a_cv);
			}
			mutex_exit(&seg->s_as->a_contents);
		}
	}

	if (async)
		mutex_exit(&shmd->shm_segfree_syncmtx);

	return (done);
}

/*
 * Do a F_SOFTUNLOCK call over the range requested.
 * The range must have already been F_SOFTLOCK'ed.
 *
 * The calls to acquire and release the anon map lock mutex were
 * removed in order to avoid a deadly embrace during a DR
 * memory delete operation.  (Eg. DR blocks while waiting for a
 * exclusive lock on a page that is being used for kaio; the
 * thread that will complete the kaio and call segspt_softunlock
 * blocks on the anon map lock; another thread holding the anon
 * map lock blocks on another page lock via the segspt_shmfault
 * -> page_lookup -> page_lookup_create -> page_lock_es code flow.)
 *
 * The appropriateness of the removal is based upon the following:
 * 1. If we are holding a segment's reader lock and the page is held
 * shared, then the corresponding element in anonmap which points to
 * anon struct cannot change and there is no need to acquire the
 * anonymous map lock.
 * 2. Threads in segspt_softunlock have a reader lock on the segment
 * and already have the shared page lock, so we are guaranteed that
 * the anon map slot cannot change and therefore can call anon_get_ptr()
 * without grabbing the anonymous map lock.
 * 3. Threads that softlock a shared page break copy-on-write, even if
 * its a read.  Thus cow faults can be ignored with respect to soft
 * unlocking, since the breaking of cow means that the anon slot(s) will
 * not be shared.
 */
static void
segspt_softunlock(struct seg *seg, caddr_t sptseg_addr,
	size_t len, enum seg_rw rw)
{
	struct shm_data *shmd = (struct shm_data *)seg->s_data;
	struct seg	*sptseg;
	struct spt_data *sptd;
	page_t *pp;
	caddr_t adr;
	struct vnode *vp;
	u_offset_t offset;
	ulong_t anon_index;
	struct anon_map *amp;		/* XXX - for locknest */
	struct anon *ap = NULL;
	pgcnt_t npages;

	ASSERT(seg->s_as && AS_LOCK_HELD(seg->s_as, &seg->s_as->a_lock));

	sptseg = shmd->shm_sptseg;
	sptd = sptseg->s_data;

	/*
	 * Some platforms assume that ISM mappings are HAT_LOAD_LOCK
	 * and therefore their pages are SE_SHARED locked
	 * for the entire life of the segment.
	 */
	if ((!hat_supported(HAT_DYNAMIC_ISM_UNMAP, (void *)0)) &&
	    ((sptd->spt_flags & SHM_PAGEABLE) == 0)) {
		goto softlock_decrement;
	}

	/*
	 * Any thread is free to do a page_find and
	 * page_unlock() on the pages within this seg.
	 *
	 * We are already holding the as->a_lock on the user's
	 * real segment, but we need to hold the a_lock on the
	 * underlying dummy as. This is mostly to satisfy the
	 * underlying HAT layer.
	 */
	AS_LOCK_ENTER(sptseg->s_as, &sptseg->s_as->a_lock, RW_READER);
	hat_unlock(sptseg->s_as->a_hat, sptseg_addr, len);
	AS_LOCK_EXIT(sptseg->s_as, &sptseg->s_as->a_lock);
	amp = sptd->spt_amp;
	ASSERT(amp != NULL);
	anon_index = seg_page(sptseg, sptseg_addr);

	for (adr = sptseg_addr; adr < sptseg_addr + len; adr += PAGESIZE) {
		ap = anon_get_ptr(amp->ahp, anon_index++);
		ASSERT(ap != NULL);
		swap_xlate(ap, &vp, &offset);

		/*
		 * Use page_find() instead of page_lookup() to
		 * find the page since we know that it has a
		 * "shared" lock.
		 */
		pp = page_find(vp, offset);
		ASSERT(ap == anon_get_ptr(amp->ahp, anon_index - 1));
		if (pp == NULL) {
			panic("segspt_softunlock: "
			    "addr %p, ap %p, vp %p, off %llx",
			    (void *)adr, (void *)ap, (void *)vp, offset);
			/*NOTREACHED*/
		}

		if (rw == S_WRITE) {
			hat_setrefmod(pp);
		} else if (rw != S_OTHER) {
			hat_setref(pp);
		}
		page_unlock(pp);
	}

softlock_decrement:
	npages = btopr(len);
	ASSERT(shmd->shm_softlockcnt >= npages);
	atomic_add_long((ulong_t *)(&(shmd->shm_softlockcnt)), -npages);
	if (shmd->shm_softlockcnt == 0) {

		/*
		 * All SOFTLOCKS are gone. Wakeup any waiting
		 * unmappers so they can try again to unmap.
		 * Check for waiters first without the mutex
		 * held so we don't always grab the mutex on
		 * softunlocks.
		 */
		if (AS_ISUNMAPWAIT(seg->s_as)) {
			mutex_enter(&seg->s_as->a_contents);
			if (AS_ISUNMAPWAIT(seg->s_as)) {
				AS_CLRUNMAPWAIT(seg->s_as);
				cv_broadcast(&seg->s_as->a_cv);
			}
			mutex_exit(&seg->s_as->a_contents);
		}
	}
}

int
segspt_shmattach(struct seg *seg, caddr_t *argsp)
{
	struct shm_data *shmd_arg = (struct shm_data *)argsp;
	struct shm_data *shmd;
	struct anon_map *shm_amp = shmd_arg->shm_amp;
	struct spt_data *sptd;
	int error = 0;
	lgrp_mem_policy_t mem_policy;

	ASSERT(seg->s_as && AS_WRITE_HELD(seg->s_as, &seg->s_as->a_lock));

	shmd = kmem_zalloc((sizeof (*shmd)), KM_NOSLEEP);
	if (shmd == NULL)
		return (ENOMEM);

	shmd->shm_sptas = shmd_arg->shm_sptas;
	shmd->shm_amp = shm_amp;
	shmd->shm_sptseg = shmd_arg->shm_sptseg;
	sptd = shmd->shm_sptseg->s_data;

	mem_policy = lgrp_shmadv_to_policy(sptd->spt_advice);
	(void) lgrp_shm_policy_set(mem_policy, shm_amp, 0, NULL, 0,
	    seg->s_size);

	mutex_init(&shmd->shm_segfree_syncmtx, NULL, MUTEX_DEFAULT, NULL);

	seg->s_data = (void *)shmd;
	seg->s_ops = &segspt_shmops;
	seg->s_szc = shmd->shm_sptseg->s_szc;

	if (sptd->spt_flags & SHM_PAGEABLE) {
		if ((shmd->shm_vpage = kmem_zalloc(btopr(shm_amp->size),
		    KM_NOSLEEP)) == NULL) {
			seg->s_data = (void *)NULL;
			kmem_free(shmd, (sizeof (*shmd)));
			return (ENOMEM);
		}
		shmd->shm_lckpgs = 0;
		if (hat_supported(HAT_DYNAMIC_ISM_UNMAP, (void *)0)) {
			if ((error = hat_share(seg->s_as->a_hat, seg->s_base,
			    shmd_arg->shm_sptas->a_hat, SEGSPTADDR,
			    seg->s_size, seg->s_szc)) != 0) {
				kmem_free(shmd->shm_vpage,
				    btopr(shm_amp->size));
			}
		}
	} else {
		error = hat_share(seg->s_as->a_hat, seg->s_base,
		    shmd_arg->shm_sptas->a_hat, SEGSPTADDR,
		    seg->s_size, seg->s_szc);
	}
	if (error) {
		seg->s_szc = 0;
		seg->s_data = (void *)NULL;
		kmem_free(shmd, (sizeof (*shmd)));
	} else {
		ANON_LOCK_ENTER(&shm_amp->a_rwlock, RW_WRITER);
		shm_amp->refcnt++;
		ANON_LOCK_EXIT(&shm_amp->a_rwlock);
	}
	return (error);
}

int
segspt_shmunmap(struct seg *seg, caddr_t raddr, size_t ssize)
{
	struct shm_data *shmd = (struct shm_data *)seg->s_data;
	int reclaim = 1;

	ASSERT(seg->s_as && AS_WRITE_HELD(seg->s_as, &seg->s_as->a_lock));
retry:
	if (shmd->shm_softlockcnt > 0) {
		if (reclaim == 1) {
			segspt_purge(seg);
			reclaim = 0;
			goto retry;
		}
		return (EAGAIN);
	}

	if (ssize != seg->s_size) {
#ifdef DEBUG
		cmn_err(CE_WARN, "Incompatible ssize %lx s_size %lx\n",
		    ssize, seg->s_size);
#endif
		return (EINVAL);
	}

	(void) segspt_shmlockop(seg, raddr, shmd->shm_amp->size, 0,
	    MC_UNLOCK, NULL, 0);

	hat_unshare(seg->s_as->a_hat, raddr, ssize, seg->s_szc);

	seg_free(seg);

	return (0);
}

void
segspt_shmfree(struct seg *seg)
{
	struct shm_data *shmd = (struct shm_data *)seg->s_data;
	struct anon_map *shm_amp = shmd->shm_amp;

	ASSERT(seg->s_as && AS_WRITE_HELD(seg->s_as, &seg->s_as->a_lock));

	(void) segspt_shmlockop(seg, seg->s_base, shm_amp->size, 0,
	    MC_UNLOCK, NULL, 0);

	/*
	 * Need to increment refcnt when attaching
	 * and decrement when detaching because of dup().
	 */
	ANON_LOCK_ENTER(&shm_amp->a_rwlock, RW_WRITER);
	shm_amp->refcnt--;
	ANON_LOCK_EXIT(&shm_amp->a_rwlock);

	if (shmd->shm_vpage) {	/* only for DISM and OSM */
		kmem_free(shmd->shm_vpage, btopr(shm_amp->size));
		shmd->shm_vpage = NULL;
	}

	/*
	 * Take shm_segfree_syncmtx lock to let segspt_reclaim() finish if it's
	 * still working with this segment without holding as lock.
	 */
	ASSERT(shmd->shm_softlockcnt == 0);
	mutex_enter(&shmd->shm_segfree_syncmtx);
	mutex_destroy(&shmd->shm_segfree_syncmtx);

	kmem_free(shmd, sizeof (*shmd));
}

/*ARGSUSED*/
int
segspt_shmsetprot(struct seg *seg, caddr_t addr, size_t len, uint_t prot)
{
	ASSERT(seg->s_as && AS_LOCK_HELD(seg->s_as, &seg->s_as->a_lock));

	/*
	 * Shared page table is more than shared mapping.
	 *  Individual process sharing page tables can't change prot
	 *  because there is only one set of page tables.
	 *  This will be allowed after private page table is
	 *  supported.
	 */
/* need to return correct status error? */
	return (0);
}

faultcode_t
segspt_dismfault(struct hat *hat, struct seg *seg, caddr_t addr,
    size_t len, enum fault_type type, enum seg_rw rw)
{
	struct  shm_data 	*shmd = (struct shm_data *)seg->s_data;
	struct  seg		*sptseg = shmd->shm_sptseg;
	struct  as		*curspt = shmd->shm_sptas;
	struct  spt_data 	*sptd = sptseg->s_data;
	pgcnt_t npages;
	size_t  size;
	caddr_t segspt_addr;
	caddr_t shm_addr;
	page_t  **ppa;
	ulong_t	i, j;
	ulong_t an_idx = 0;
	int	err = 0;
	int	dyn_ism_unmap = hat_supported(HAT_DYNAMIC_ISM_UNMAP, (void *)0);
	size_t	pgsz, sz;
	pgcnt_t	pgcnt;
	caddr_t	a;
	pgcnt_t	pidx;
	struct	anon_map	*amp = sptd->spt_amp;
	struct	anon		*ap;
	struct	vnode		*vp;
	u_offset_t		offset;

#ifdef lint
	hat = hat;
#endif
	ASSERT(seg->s_as && AS_LOCK_HELD(seg->s_as, &seg->s_as->a_lock));

	/*
	 * Because of the way spt is implemented
	 * the realsize of the segment does not have to be
	 * equal to the segment size itself. The segment size is
	 * often in multiples of a page size larger than PAGESIZE.
	 * The realsize is rounded up to the nearest PAGESIZE
	 * based on what the user requested. This is a bit of
	 * ungliness that is historical but not easily fixed
	 * without re-designing the higher levels of ISM.
	 */
	ASSERT(addr >= seg->s_base);
	if (((addr + len) - seg->s_base) > sptd->spt_realsize)
		return (FC_NOMAP);
	/*
	 * For all of the following cases except F_PROT, we need to
	 * make any necessary adjustments to addr and len
	 * and get all of the necessary page_t's into an array called ppa[].
	 *
	 * The code in shmat() forces base addr of ISM segment to be aligned
	 * to largest page size supported and len of ISM segment in multiple
	 * of smallest large page size supported by hat_share(). All mappings
	 * are done in large page size chunks except the tail end which may
	 * be done in smallest large page size supported by the hat_share().
	 * For OSM we also map pages in preferred page size chunks.
	 */
	pgsz = page_get_pagesize(sptseg->s_szc);
	pgcnt = page_get_pagecnt(sptseg->s_szc);
	shm_addr = (caddr_t)P2ALIGN((uintptr_t)(addr), pgsz);
	size = P2ROUNDUP((uintptr_t)(((addr + len) - shm_addr)), pgsz);
	npages = btopr(size);

	/*
	 * Now we need to convert from addr in segshm to addr in segspt.
	 */
	an_idx = seg_page(seg, shm_addr);
	segspt_addr = sptseg->s_base + ptob(an_idx);

	/*
	 * And now we may have to adjust npages downward if we have
	 * exceeded the realsize of the segment or initial anon
	 * allocations. Does not apply to osm because osm size
	 * has to be aligned on granule size.
	 */
	if ((segspt_addr + ptob(npages)) >
	    (sptseg->s_base + sptd->spt_realsize))
		size = (sptseg->s_base + sptd->spt_realsize) - segspt_addr;
	npages = btopr(size);

	ASSERT(segspt_addr < (sptseg->s_base + sptseg->s_size));

	switch (type) {

	case F_SOFTLOCK:

		atomic_add_long((ulong_t *)(&(shmd->shm_softlockcnt)), npages);
		/*
		 * Fall through to the F_INVAL case to load up the hat layer
		 * entries with the HAT_LOAD_LOCK flag.
		 */
		/* FALLTHRU */
	case F_INVAL:

		if ((rw == S_EXEC) && !(sptd->spt_prot & PROT_EXEC))
			return (FC_NOMAP);

		ppa = kmem_zalloc(npages * sizeof (page_t *), KM_SLEEP);

		if (SPT_OSM(sptd)) {

			rw_enter(&sptd->spt_mcglock, RW_READER);
			for (i = 0; i < npages; i++, an_idx++) {

				if (sptd->spt_ppa_lckcnt[an_idx] == 0) {
					break;
				}

				ap = anon_get_ptr(amp->ahp, an_idx);
				ASSERT(ap != NULL);
				swap_xlate(ap, &vp, &offset);
				ppa[i] = page_lookup(vp, offset, SE_SHARED);
				ASSERT(ppa[i] != NULL);
			}
			rw_exit(&sptd->spt_mcglock);

			if (i < npages) {
				for (j = 0; j < i; j++) {
					page_unlock(ppa[j]);
				}

				kmem_free(ppa, sizeof (page_t *) * npages);
				if (type == F_SOFTLOCK) {
					atomic_add_long((ulong_t *)
					    (&(shmd->shm_softlockcnt)),
					    -npages);
				}

				/*
				 * If OSM pages are not locked return error.
				 */
				return (EFAULT);
			}
		} else {

			/*
			 * For DISM to allocate/lock pages.
			 *
			 * For tail end page allocations, the size has
			 * to be rounded back to preferred page size
			 * (even though ppa size was scaled back to
			 * reflect spt_realsize). Because
			 * spt_anon_getpages() allocates pages in
			 * multiple of preferred page size chunks.
			 */
			err = spt_anon_getpages(sptseg, segspt_addr,
			    P2ROUNDUP(size, pgsz), ppa, 0);
			if (err != 0) {
				if (type == F_SOFTLOCK) {
					atomic_add_long((ulong_t *)(
					    &(shmd->shm_softlockcnt)), -npages);
				}
				goto dism_err;
			}
		}

		AS_LOCK_ENTER(sptseg->s_as, &sptseg->s_as->a_lock, RW_READER);
		a = segspt_addr;
		pidx = 0;
		if (type == F_SOFTLOCK) {

			/*
			 * Load up the translation keeping it
			 * locked and don't unlock the page.
			 */
			for (; pidx < npages; a += pgsz, pidx += pgcnt) {
				sz = MIN(pgsz, ptob(npages - pidx));
				hat_memload_array(sptseg->s_as->a_hat,
				    a, sz, &ppa[pidx], sptd->spt_prot,
				    HAT_LOAD_LOCK | HAT_LOAD_SHARE);
			}
		} else {
			ASSERT(hat == seg->s_as->a_hat);

			/*
			 * Migrate pages marked for migration
			 */
			if (lgrp_optimizations())
				page_migrate(seg, shm_addr, ppa, npages);

			for (; pidx < npages; a += pgsz, pidx += pgcnt) {
				sz = MIN(pgsz, ptob(npages - pidx));
				hat_memload_array(sptseg->s_as->a_hat, a,
				    sz, &ppa[pidx], sptd->spt_prot,
				    HAT_LOAD_SHARE);
			}

			/*
			 * And now drop the SE_SHARED lock(s).
			 */
			if (dyn_ism_unmap) {
				for (i = 0; i < npages; i++) {
					page_unlock(ppa[i]);
				}
			}
		}

		if (!dyn_ism_unmap) {
			if (hat_share(seg->s_as->a_hat, shm_addr,
			    curspt->a_hat, segspt_addr, ptob(npages),
			    seg->s_szc) != 0) {
				panic("hat_share err in DISM fault");
				/* NOTREACHED */
			}
			if (type == F_INVAL) {
				for (i = 0; i < npages; i++) {
					page_unlock(ppa[i]);
				}
			}
		}
		AS_LOCK_EXIT(sptseg->s_as, &sptseg->s_as->a_lock);
dism_err:
		kmem_free(ppa, npages * sizeof (page_t *));
		return (err);

	case F_SOFTUNLOCK:

		/*
		 * This is a bit ugly, we pass in the real seg pointer,
		 * but the segspt_addr is the virtual address within the
		 * dummy seg.
		 */
		segspt_softunlock(seg, segspt_addr, size, rw);
		return (0);

	case F_PROT:

		/*
		 * This takes care of the unusual case where a user
		 * allocates a stack in shared memory and a register
		 * window overflow is written to that stack page before
		 * it is otherwise modified.
		 *
		 * We can get away with this because ISM segments are
		 * always rw. Other than this unusual case, there
		 * should be no instances of protection violations.
		 */
		return (0);

	default:
#ifdef DEBUG
		panic("segspt_dismfault default type?");
#else
		return (FC_NOMAP);
#endif
	}
}


faultcode_t
segspt_shmfault(struct hat *hat, struct seg *seg, caddr_t addr,
    size_t len, enum fault_type type, enum seg_rw rw)
{
	struct shm_data 	*shmd = (struct shm_data *)seg->s_data;
	struct seg		*sptseg = shmd->shm_sptseg;
	struct as		*curspt = shmd->shm_sptas;
	struct spt_data 	*sptd   = sptseg->s_data;
	pgcnt_t npages;
	size_t size;
	caddr_t sptseg_addr, shm_addr;
	page_t *pp, **ppa;
	int	i;
	u_offset_t offset;
	ulong_t anon_index = 0;
	struct vnode *vp;
	struct anon_map *amp;		/* XXX - for locknest */
	struct anon *ap = NULL;
	size_t		pgsz;
	pgcnt_t		pgcnt;
	caddr_t		a;
	pgcnt_t		pidx;
	size_t		sz;

#ifdef lint
	hat = hat;
#endif

	ASSERT(seg->s_as && AS_LOCK_HELD(seg->s_as, &seg->s_as->a_lock));

	if (sptd->spt_flags & SHM_PAGEABLE) {
		return (segspt_dismfault(hat, seg, addr, len, type, rw));
	}

	/*
	 * Because of the way spt is implemented
	 * the realsize of the segment does not have to be
	 * equal to the segment size itself. The segment size is
	 * often in multiples of a page size larger than PAGESIZE.
	 * The realsize is rounded up to the nearest PAGESIZE
	 * based on what the user requested. This is a bit of
	 * ungliness that is historical but not easily fixed
	 * without re-designing the higher levels of ISM.
	 */
	ASSERT(addr >= seg->s_base);
	if (((addr + len) - seg->s_base) > sptd->spt_realsize)
		return (FC_NOMAP);
	/*
	 * For all of the following cases except F_PROT, we need to
	 * make any necessary adjustments to addr and len
	 * and get all of the necessary page_t's into an array called ppa[].
	 *
	 * The code in shmat() forces base addr of ISM segment to be aligned
	 * to largest page size supported and len of ISM segment in multiple
	 * of smallest large page size supported by hat_share().
	 * The F_SOFTLOCK and F_INVAL calls are handled in preferred page
	 * sizes except tail end which is done in smallest large page size
	 * supported by hat_share.
	 */
	pgsz = page_get_pagesize(sptseg->s_szc);
	pgcnt = page_get_pagecnt(sptseg->s_szc);
	shm_addr = (caddr_t)P2ALIGN((uintptr_t)(addr), pgsz);
	size = P2ROUNDUP((uintptr_t)(((addr + len) - shm_addr)), pgsz);
	npages = btopr(size);

	/*
	 * Now we need to convert from addr in segshm to addr in segspt.
	 */
	anon_index = seg_page(seg, shm_addr);
	sptseg_addr = sptseg->s_base + ptob(anon_index);

	/*
	 * And now we may have to adjust npages downward if we have
	 * exceeded the realsize of the segment or initial anon
	 * allocations.
	 */
	if ((sptseg_addr + ptob(npages)) >
	    (sptseg->s_base + sptd->spt_realsize))
		size = (sptseg->s_base + sptd->spt_realsize) - sptseg_addr;
	npages = btopr(size);

	ASSERT(sptseg_addr < (sptseg->s_base + sptseg->s_size));
	ASSERT((sptd->spt_flags & SHM_PAGEABLE) == 0);

	switch (type) {

	case F_SOFTLOCK:

		/*
		 * availrmem is decremented once during anon_swap_adjust()
		 * and is incremented during the anon_unresv(), which is
		 * called from shm_rm_amp() when the segment is destroyed.
		 */
		atomic_add_long((ulong_t *)(&(shmd->shm_softlockcnt)), npages);
		/*
		 * Some platforms assume that ISM pages are SE_SHARED
		 * locked for the entire life of the segment.
		 */
		if (!hat_supported(HAT_DYNAMIC_ISM_UNMAP, (void *)0))
			return (0);
		/*
		 * Fall through to the F_INVAL case to load up the hat layer
		 * entries with the HAT_LOAD_LOCK flag.
		 */

		/* FALLTHRU */
	case F_INVAL:

		if ((rw == S_EXEC) && !(sptd->spt_prot & PROT_EXEC))
			return (FC_NOMAP);

		/*
		 * Some platforms that do NOT support DYNAMIC_ISM_UNMAP
		 * may still rely on this call to hat_share(). That
		 * would imply that those hat's can fault on a
		 * HAT_LOAD_LOCK translation, which would seem
		 * contradictory.
		 */
		if (!hat_supported(HAT_DYNAMIC_ISM_UNMAP, (void *)0)) {
			if (hat_share(seg->s_as->a_hat, seg->s_base,
			    curspt->a_hat, sptseg->s_base,
			    sptseg->s_size, sptseg->s_szc) != 0) {
				panic("hat_share error in ISM fault");
				/*NOTREACHED*/
			}
			return (0);
		}
		ppa = kmem_zalloc(sizeof (page_t *) * npages, KM_SLEEP);

		/*
		 * I see no need to lock the real seg,
		 * here, because all of our work will be on the underlying
		 * dummy seg.
		 *
		 * sptseg_addr and npages now account for large pages.
		 */
		amp = sptd->spt_amp;
		ASSERT(amp != NULL);
		anon_index = seg_page(sptseg, sptseg_addr);

		ANON_LOCK_ENTER(&amp->a_rwlock, RW_READER);
		for (i = 0; i < npages; i++) {
			ap = anon_get_ptr(amp->ahp, anon_index++);
			ASSERT(ap != NULL);
			swap_xlate(ap, &vp, &offset);
			pp = page_lookup(vp, offset, SE_SHARED);
			ASSERT(pp != NULL);
			ppa[i] = pp;
		}
		ANON_LOCK_EXIT(&amp->a_rwlock);
		ASSERT(i == npages);

		/*
		 * We are already holding the as->a_lock on the user's
		 * real segment, but we need to hold the a_lock on the
		 * underlying dummy as. This is mostly to satisfy the
		 * underlying HAT layer.
		 */
		AS_LOCK_ENTER(sptseg->s_as, &sptseg->s_as->a_lock, RW_READER);
		a = sptseg_addr;
		pidx = 0;
		if (type == F_SOFTLOCK) {
			/*
			 * Load up the translation keeping it
			 * locked and don't unlock the page.
			 */
			for (; pidx < npages; a += pgsz, pidx += pgcnt) {
				sz = MIN(pgsz, ptob(npages - pidx));
				hat_memload_array(sptseg->s_as->a_hat, a,
				    sz, &ppa[pidx], sptd->spt_prot,
				    HAT_LOAD_LOCK | HAT_LOAD_SHARE);
			}
		} else {
			ASSERT(hat == seg->s_as->a_hat);

			/*
			 * Migrate pages marked for migration.
			 */
			if (lgrp_optimizations())
				page_migrate(seg, shm_addr, ppa, npages);

			for (; pidx < npages; a += pgsz, pidx += pgcnt) {
				sz = MIN(pgsz, ptob(npages - pidx));
				hat_memload_array(sptseg->s_as->a_hat, a, sz,
				    &ppa[pidx], sptd->spt_prot, HAT_LOAD_SHARE);
			}

			/*
			 * And now drop the SE_SHARED lock(s).
			 */
			for (i = 0; i < npages; i++)
				page_unlock(ppa[i]);
		}
		AS_LOCK_EXIT(sptseg->s_as, &sptseg->s_as->a_lock);

		kmem_free(ppa, sizeof (page_t *) * npages);
		return (0);
	case F_SOFTUNLOCK:

		/*
		 * This is a bit ugly, we pass in the real seg pointer,
		 * but the sptseg_addr is the virtual address within the
		 * dummy seg.
		 */
		segspt_softunlock(seg, sptseg_addr, ptob(npages), rw);
		return (0);

	case F_PROT:

		/*
		 * This takes care of the unusual case where a user
		 * allocates a stack in shared memory and a register
		 * window overflow is written to that stack page before
		 * it is otherwise modified.
		 *
		 * We can get away with this because ISM segments are
		 * always rw. Other than this unusual case, there
		 * should be no instances of protection violations.
		 */
		return (0);

	default:
#ifdef DEBUG
		cmn_err(CE_WARN, "segspt_shmfault default type?");
#endif
		return (FC_NOMAP);
	}
}

/*ARGSUSED*/
static faultcode_t
segspt_shmfaulta(struct seg *seg, caddr_t addr)
{
	return (0);
}

/*ARGSUSED*/
static int
segspt_shmkluster(struct seg *seg, caddr_t addr, ssize_t delta)
{
	return (0);
}

/*ARGSUSED*/
static size_t
segspt_shmswapout(struct seg *seg)
{
	return (0);
}

/*
 * duplicate the shared page tables
 */
int
segspt_shmdup(struct seg *seg, struct seg *newseg)
{
	struct shm_data		*shmd = (struct shm_data *)seg->s_data;
	struct anon_map 	*amp = shmd->shm_amp;
	struct shm_data 	*shmd_new;
	struct seg		*spt_seg = shmd->shm_sptseg;
	struct spt_data		*sptd = spt_seg->s_data;
	int			error = 0;

	ASSERT(seg->s_as && AS_WRITE_HELD(seg->s_as, &seg->s_as->a_lock));

	shmd_new = kmem_zalloc((sizeof (*shmd_new)), KM_SLEEP);
	newseg->s_data = (void *)shmd_new;
	shmd_new->shm_sptas = shmd->shm_sptas;
	shmd_new->shm_amp = amp;
	shmd_new->shm_sptseg = shmd->shm_sptseg;
	newseg->s_ops = &segspt_shmops;
	newseg->s_szc = seg->s_szc;
	ASSERT(seg->s_szc == shmd->shm_sptseg->s_szc);

	ANON_LOCK_ENTER(&amp->a_rwlock, RW_WRITER);
	amp->refcnt++;
	ANON_LOCK_EXIT(&amp->a_rwlock);

	if (sptd->spt_flags & SHM_PAGEABLE) {
		shmd_new->shm_vpage = kmem_zalloc(btopr(amp->size), KM_SLEEP);
		shmd_new->shm_lckpgs = 0;
		if (hat_supported(HAT_DYNAMIC_ISM_UNMAP, (void *)0)) {
			if ((error = hat_share(newseg->s_as->a_hat,
			    newseg->s_base, shmd->shm_sptas->a_hat, SEGSPTADDR,
			    seg->s_size, seg->s_szc)) != 0) {
				kmem_free(shmd_new->shm_vpage,
				    btopr(amp->size));
			}
		}
		return (error);
	} else {
		return (hat_share(newseg->s_as->a_hat, newseg->s_base,
		    shmd->shm_sptas->a_hat, SEGSPTADDR, seg->s_size,
		    seg->s_szc));

	}
}

/*ARGSUSED*/
int
segspt_shmcheckprot(struct seg *seg, caddr_t addr, size_t size, uint_t prot)
{
	struct shm_data *shmd = (struct shm_data *)seg->s_data;
	struct spt_data *sptd = (struct spt_data *)shmd->shm_sptseg->s_data;

	ASSERT(seg->s_as && AS_LOCK_HELD(seg->s_as, &seg->s_as->a_lock));

	/*
	 * ISM segment is always rw.
	 */
	return (((sptd->spt_prot & prot) != prot) ? EACCES : 0);
}

struct spt_agp {
	pgcnt_t	lp_npgs;
	size_t	pg_sz;
	caddr_t	lp_addr;
	pgcnt_t	an_idx;
	pgcnt_t	amp_pgs;
	uint_t	szc;
	struct	spt_data *sptd;
	struct	anon_map *amp;
	struct	seg *sptseg;
	page_t	**ppa;
	pgcnt_t	locked_pgs;	/* pages already locked */
};

static int spt_anon_getpages_task(ulong_t, ulong_t, void *, ulong_t *);

/*
 * Return an array of locked large pages, for empty slots allocate
 * private zero-filled anon pages.
 * len needs to be aligned to the preferred page size of the segment
 */
static int
spt_anon_getpages(
	struct seg *sptseg,
	caddr_t sptaddr,
	size_t len,
	page_t *ppa[],
	int	reserve)
{
	struct spt_agp	targ;
	struct spt_data	*sptd = sptseg->s_data;
	int		err;
	ulong_t		job_size;

	targ.szc	= sptseg->s_szc;
	targ.pg_sz	= page_get_pagesize(targ.szc);

	ASSERT(IS_P2ALIGNED(sptaddr, targ.pg_sz) &&
	    IS_P2ALIGNED(len, targ.pg_sz));
	ASSERT(len != 0);

	targ.sptd	= sptd;
	targ.amp	= targ.sptd->spt_amp;
	targ.lp_npgs	= btop(targ.pg_sz);
	targ.lp_addr	= sptaddr;
	targ.an_idx	= seg_page(sptseg, sptaddr);
	targ.amp_pgs	= page_get_pagecnt(targ.amp->a_szc);
	targ.sptseg	= sptseg;
	targ.ppa	= ppa;
	targ.locked_pgs = 0;
	job_size	= len / targ.pg_sz;

	ANON_LOCK_ENTER(&targ.amp->a_rwlock, RW_READER);

	err = vmtask_run_job(job_size, VMTASK_LPGS_MINJOB,
	    spt_anon_getpages_task, (void *)&targ, NULL);

	ANON_LOCK_EXIT(&targ.amp->a_rwlock);

	if (!err && reserve) {

		/*
		 * Reserve availrmem only for non locked pages.
		 */
		if (!page_reclaim_mem(btop(len) - targ.locked_pgs,
		    segspt_minfree, 1)) {
			err = EAGAIN;
		}
	}

	if (err) {

		/*
		 * not enough of memory:
		 * for OSM unlock and free allocated pages.
		 * for DISM unlock allocated pages.
		 *
		 * For DISM we may have to adjust len downward if we have
		 * exceeded the realsize of the segment or initial anon
		 * allocations. Since len is preferred page size aligned,
		 * but size of allocated ppa array may not be.
		 */
		if (!SPT_OSM(sptd) &&
		    ((sptaddr + len) > (sptseg->s_base + sptd->spt_realsize))) {
			len = (sptseg->s_base + sptd->spt_realsize) - sptaddr;
		}
		segspt_unlock_npages(sptseg, btop(len), ppa,
		    SPT_OSM(sptd) ? 1 : 0);
	}

	return (err);
}

static int
spt_anon_getpages_task(ulong_t idx, ulong_t end, void *arg, ulong_t *ridx)
{
	struct spt_agp *targp	= (struct spt_agp *)arg;
	size_t pg_sz		= targp->pg_sz;
	pgcnt_t lp_npgs		= targp->lp_npgs;
	pgcnt_t an_idx		= targp->an_idx + idx * lp_npgs;
	caddr_t lp_addr		= targp->lp_addr + idx * pg_sz;
	caddr_t e_sptaddr	= targp->lp_addr + end * pg_sz;
					/* end address (+1) of this request */
	pgcnt_t amp_pgs		= targp->amp_pgs;
	struct spt_data *sptd	= targp->sptd;
	struct anon_map *amp	= targp->amp;
	struct seg *sptseg	= targp->sptseg;
	page_t **ppa		= targp->ppa;
	uint_t szc		= targp->szc;
	enum seg_rw rw		= sptd->spt_prot;
	ulong_t ppa_idx		= idx * lp_npgs;
	caddr_t end_addr	= sptseg->s_base + sptd->spt_realsize;
					/* end address (+1) of the segment */
	int err			= 0;
	int anon_locked		= 0;
	int ierr		= 0;
	anon_sync_obj_t cookie;
	uint_t vpprot, ppa_szc;
	pgcnt_t	locked_pgs	= 0;
	pgcnt_t	locked_lp_pgs	= 0;
	caddr_t tail_start;

	/*
	 * We may have to adjust e_sptaddr downward, if
	 * we have exceeded the realsize of the segment
	 * or initial anon allocations.
	 */
	if (!SPT_OSM(sptd) && (e_sptaddr >  end_addr)) {
		e_sptaddr =  end_addr;
	}
	tail_start = (caddr_t)P2ALIGN((uintptr_t)(end_addr), pg_sz);

	while (lp_addr < e_sptaddr) {
		/*
		 * skip already locked OSM pages.
		 */
		if (SPT_OSM(sptd) && (sptd->spt_ppa_lckcnt[an_idx] != 0)) {
			lp_addr += pg_sz;
			an_idx += lp_npgs;
			ppa_idx += lp_npgs;
			locked_pgs += lp_npgs;
			continue;
		}

		/*
		 * Try to allocate ism_min_pgsz pages for the
		 * tail end of the segment, if the real size of
		 * segment (spt_realsize) is not aligned to the
		 * preferred page size of the segment.
		 *
		 * This may happen as a result of a "size up" or
		 * "size down" operation which was done after a call
		 * to anon_map_getpages(), below.  This operation may
		 * cause us to start allocating pages smaller than
		 * ism_min_pgsz, in which case the allocation would
		 * no longer be ism_min_pgsz aligned; we must not use
		 * ism_min_pgsz pages in that case.
		 */
		if (!SPT_OSM(sptd) && (lp_addr >= tail_start) &&
		    (pg_sz > ism_min_pgsz) && (ierr == 0)) {
			pg_sz = ism_min_pgsz;
			szc = page_szc(pg_sz);
			lp_npgs = btop(pg_sz);
			ASSERT(IS_P2ALIGNED(lp_addr, pg_sz));
		}

		/*
		 * Can't skip locked DISM pages because DISM locking
		 * semantics are differenet. If page is locked by other
		 * process this process still needs to lock it and
		 * increment it's p_lckcnt.
		 */
		if (!SPT_OSM(sptd) && IS_P2ALIGNED(lp_addr, pg_sz)) {
			pgcnt_t	i;
			pgcnt_t	idx = an_idx;

			for (i = 0; i < lp_npgs; i++, idx++) {
				if ((sptd->spt_ppa_lckcnt[idx] != 0) ||
				    (sptd->spt_ppa != NULL &&
				    sptd->spt_ppa[idx] != 0)) {
					locked_lp_pgs++;
				}
			}
		}

		/*
		 * If we're currently locked, and we get to a new
		 * page, unlock our current anon chunk.
		 */
		if (anon_locked && P2PHASE(an_idx, amp_pgs) == 0) {
			anon_array_exit(&cookie);
			anon_locked = 0;
		}
		if (!anon_locked) {
			anon_array_enter(amp, an_idx, &cookie);
			anon_locked = 1;
		}
		ppa_szc = (uint_t)-1;
		ierr = anon_map_getpages(amp, an_idx, szc, sptseg,
		    lp_addr, sptd->spt_prot, &vpprot, &ppa[ppa_idx],
		    &ppa_szc, NULL, rw, 0, segvn_anypgsz, 0, kcred);

		if (ierr > 0) {
			if (ierr == ENOMEM)
				err = ENOMEM;
			else
				err = FC_MAKE_ERR(ierr);
			break;
		} else if (ierr == -1 || ierr == -2) {
			/*
			 * ierr == -1 means we failed to allocate a large page.
			 * so do a size down operation.
			 *
			 * ierr == -2 means some other process that privately
			 * shares pages with this process has allocated a
			 * larger page and we need to retry with larger pages.
			 * So do a size up operation. This relies on the fact
			 * that large pages are never partially shared i.e. if
			 * we share any constituent page of a large page with
			 * another process we must share the entire large page.
			 * Note this cannot happen for SOFTLOCK case, unless
			 * current address (lpaddr) is at the beginning of the
			 * next page size boundary because the other process
			 * couldn't have relocated locked pages.
			 */
			ASSERT(ierr == -1 || ierr == -2);
			if (segvn_anypgsz) {
				ASSERT(ierr == -2 || szc != 0);
				ASSERT(ierr == -1 || szc < sptseg->s_szc);
				szc = (ierr == -1) ? szc - 1 : szc + 1;
			} else {
				/*
				 * For faults and segvn_anypgsz == 0
				 * we need to be careful not to loop forever
				 * if existing page is found with szc other
				 * than 0 or seg->s_szc. This could be due
				 * to page relocations on behalf of DR or
				 * more likely large page creation. For this
				 * case simply re-size to existing page's szc
				 * if returned by anon_map_getpages().
				 */
				if (ppa_szc == (uint_t)-1) {
					szc = (ierr == -1) ? 0 : sptseg->s_szc;
				} else {
					ASSERT(ppa_szc <= sptseg->s_szc);
					ASSERT(ierr == -2 || ppa_szc < szc);
					ASSERT(ierr == -1 || ppa_szc > szc);
					szc = ppa_szc;
				}
			}

			if (!SPT_OSM(sptd))
				locked_lp_pgs = 0;

			pg_sz = page_get_pagesize(szc);
			lp_npgs = btop(pg_sz);
			ASSERT(IS_P2ALIGNED(lp_addr, pg_sz));
			continue;
		}

		if (!SPT_OSM(sptd)) {
			locked_pgs += locked_lp_pgs;
			locked_lp_pgs = 0;
		}
		an_idx += lp_npgs;
		lp_addr += pg_sz;
		ppa_idx += lp_npgs;
	}

	if (anon_locked)
		anon_array_exit(&cookie);
	if (err == 0) {
		atomic_add_long(&targp->locked_pgs, locked_pgs);
	}
	*ridx = (an_idx - targp->an_idx)/lp_npgs;
	return (err);
}

struct spt_ulkb {
	uint64_t	unlockedbytes;
	page_t		**ppa;
};
static int spt_unlockedbytes_task(ulong_t, ulong_t, void *, ulong_t *);

/*
 * count the number of bytes in a set of spt pages that are currently not
 * locked
 */
static rctl_qty_t
spt_unlockedbytes(pgcnt_t npages, page_t **ppa)
{
	struct spt_ulkb targ;

	targ.unlockedbytes	= 0;
	targ.ppa		= ppa;

	(void) vmtask_run_job(npages, VMTASK_SPGS_MINJOB,
	    spt_unlockedbytes_task, (void *)&targ, NULL);

	return ((rctl_qty_t)targ.unlockedbytes);
}

static int
spt_unlockedbytes_task(ulong_t idx, ulong_t end, void *arg, ulong_t *ridx)
{
	struct spt_ulkb	*targp = (struct spt_ulkb *)arg;
	page_t		**ppa = targp->ppa;
	uint64_t	unlockedbytes = 0;

	for (; idx < end; idx++) {
		if (ppa[idx] != NULL && ppa[idx]->p_lckcnt == 0)
			unlockedbytes += PAGESIZE;
	}

	atomic_add_64(&targp->unlockedbytes, unlockedbytes);

	*ridx = idx;

	return (0);
}

struct spt_lkp {
	struct seg	*seg;
	pgcnt_t		anon_index;
	page_t		**ppa;
	ulong_t		*lockmap;
	size_t		pos;
	uint64_t	locked;
};
static int spt_lockpages_task(ulong_t, ulong_t, void *, ulong_t *);

int
spt_lockpages(struct seg *seg, pgcnt_t anon_index, pgcnt_t npages,
    page_t **ppa, ulong_t *lockmap, size_t pos,
    rctl_qty_t *locked)
{
	struct spt_lkp	targ;
	int		rv;
	ulong_t		job_chunk_min;
	ulong_t		job_size;
	struct shm_data	*shmd = seg->s_data;
	struct spt_data	*sptd = shmd->shm_sptseg->s_data;

	targ.seg	= seg;
	targ.anon_index	= anon_index;
	targ.ppa	= ppa;
	targ.lockmap	= lockmap;
	targ.pos	= pos;
	targ.locked	= 0;

	job_chunk_min = SPT_OSM(sptd) ? VMTASK_LPGS_MINJOB:
	    VMTASK_SPGS_MINJOB;
	job_size = SPT_OSM(sptd) ?
	    npages / page_get_pagecnt(seg->s_szc) :
	    npages;

	rv = vmtask_run_job(job_size, job_chunk_min, spt_lockpages_task,
	    (void *)&targ, NULL);

	/* return the number of bytes actually locked */
	*locked = (rctl_qty_t)targ.locked;

	return (rv);
}

static int
spt_lockpages_task(ulong_t idx, ulong_t end, void *arg, ulong_t *ridx)
{
	struct spt_lkp	*targp = (struct spt_lkp *)arg;
	struct seg	*seg = targp->seg;
	pgcnt_t		an_idx;
	page_t		**ppa = targp->ppa;
	ulong_t		*lockmap = targp->lockmap;
	size_t		pos = targp->pos + idx;
	struct shm_data	*shmd = seg->s_data;
	struct spt_data	*sptd = shmd->shm_sptseg->s_data;
	uint64_t	locked = 0;
	ulong_t		i;
	pgcnt_t		shm_lckpgs = 0;
	pgcnt_t		pgs_locked = 0;
	pgcnt_t		cache_idx_start;
	pgcnt_t		pgcnt = page_get_pagecnt(seg->s_szc);

	/*
	 * OSM pages are locked in multiple LP chunks so idx
	 * and end are multiple of large page sizes.
	 *
	 */
	if (SPT_OSM(sptd)) {
		idx = idx * pgcnt;
		an_idx = targp->anon_index + idx;
		end = end * pgcnt;
	} else {
		an_idx = targp->anon_index + idx;
	}

	cache_idx_start = an_idx;
	for (i = idx; i < end; an_idx++, pos++, i++) {

		/*
		 * Osm MC_LOCK_GRANULE operations do not nest, page is
		 * locked once for all processes.
		 * Skip already locked OSM pages.
		 */
		if (SPT_OSM(sptd) && sptd->spt_ppa_lckcnt[an_idx] != 0) {

			ASSERT(ppa[i] == NULL);

			cache_idx_start = an_idx + pgcnt;
			an_idx += pgcnt - 1;
			pos += pgcnt - 1;
			i += pgcnt - 1;
			continue;
		}

		/*
		 * For OSM this is always true since spt_ppa_lckcnt == 0.
		 */
		if (!(shmd->shm_vpage[an_idx] & DISM_PG_LOCKED)) {
			if (sptd->spt_ppa_lckcnt[an_idx] <
			    (ushort_t)DISM_LOCK_MAX) {
				if (++sptd->spt_ppa_lckcnt[an_idx] ==
				    (ushort_t)DISM_LOCK_MAX) {
					cmn_err(CE_WARN,
					    "DISM page lock limit "
					    "reached on DISM offset 0x%lx\n",
					    an_idx << PAGESHIFT);
				}

				(void) page_pp_lock(ppa[i], 0, 1);

				/* if this is a newly locked page, count it */
				if (ppa[i]->p_lckcnt == 1) {
					locked += PAGESIZE;
					pgs_locked++;
				}
				shm_lckpgs++;
				shmd->shm_vpage[an_idx] |= DISM_PG_LOCKED;

				/*
				 * Multiple threads simultaneously set bits in
				 * lockmap, this is why atomic operation is
				 * needed.
				 */
				if (lockmap != NULL)
					BT_ATOMIC_SET(lockmap, pos);
			}
			/*
			 * mlocked pages are not cached when no pages are in
			 * seg pcache so they need to be unlocked.
			 */
			if (sptd->spt_ppa == NULL && SPT_OSM(sptd)) {
				page_unlock(ppa[i]);
				ppa[i] = 0;
			}
		}

		/*
		 * Cache locked pages in spt_ppa[]. Start doing it from top of
		 * the large page to the bottom of the large page in spt_ppa[]
		 * to avoid race with segspt_dismpagelock().
		 * The segspt_dismpagelock() first looks at spt_ppa[] without
		 * holding spt_lock. And for large pages it only checks root
		 * page. So we have to make sure that all constituent pages are
		 * cached before root page is present in spt_ppa[].
		 */
		if (sptd->spt_ppa && SPT_OSM(sptd)) {
			if (an_idx - cache_idx_start == (pgcnt - 1)) {
				pgcnt_t from = i;
				pgcnt_t	to = an_idx;

				while (to > cache_idx_start) {
					if (PAGE_EXCL(ppa[from])) {
						page_downgrade(ppa[from]);
					}
					sptd->spt_ppa[to--] = ppa[from];
					ppa[from--] = 0;
				}

				if (PAGE_EXCL(ppa[from]))
					page_downgrade(ppa[from]);
				/*
				 * before root page is placed in spt_ppa
				 * make sure all constituent pages are present.
				 */
				membar_producer();
				sptd->spt_ppa[to] = ppa[from];
				cache_idx_start = an_idx + 1;
			}
		}
	}

	if (SPT_OSM(sptd)) {
		struct kshmid   *sp = sptd->spt_amp->a_sp;

		atomic_add_long((ulong_t *)&sp->shm_allocated,
		    (ulong_t)locked);
	}

	if (pgs_locked != 0) {
		mutex_enter(&freemem_lock);
		pages_locked += pgs_locked;
		mutex_exit(&freemem_lock);
	}

	atomic_add_long(((ulong_t *)&shmd->shm_lckpgs), (long)shm_lckpgs);
	atomic_add_64(&targp->locked, locked);
	*ridx = i;

	return (0);
}

struct spt_ukp {
	pgcnt_t		anon_index;
	struct seg	*seg;
	uint64_t	unlocked;
};
static int spt_unlockpages_task(ulong_t, ulong_t, void *, ulong_t *);

int
spt_unlockpages(struct seg *seg, pgcnt_t anon_index, pgcnt_t npages,
    rctl_qty_t *unlocked)
{
	struct spt_ukp	targ;
	int		rv;

	targ.anon_index	= anon_index;
	targ.seg	= seg;
	targ.unlocked	= 0;

	rv = vmtask_run_job(npages, VMTASK_SPGS_MINJOB, spt_unlockpages_task,
	    (void *)&targ, NULL);

	/* return the number of bytes unlocked */
	*unlocked = (rctl_qty_t)targ.unlocked;

	return (rv);
}

static int
spt_unlockpages_task(ulong_t idx, ulong_t end, void *arg, ulong_t *ridx)
{
	struct spt_ukp	*targp = (struct spt_ukp *)arg;
	pgcnt_t		anon_index = targp->anon_index + idx;
	struct shm_data	*shmd = targp->seg->s_data;
	struct spt_data	*sptd = shmd->shm_sptseg->s_data;
	struct anon_map	*amp = sptd->spt_amp;
	struct anon 	*ap;
	struct vnode 	*vp;
	u_offset_t 	off;
	struct page	*pp;
	int		kernel;
	anon_sync_obj_t	cookie;
	uint64_t	unlocked = 0;
	ulong_t		i;
	pgcnt_t		nlck = 0;
	pgcnt_t		shm_lckpgs = 0;

	ANON_LOCK_ENTER(&amp->a_rwlock, RW_READER);
	for (i = idx; i < end; i++, anon_index++) {
		if ((!SPT_OSM(sptd) &&		/* DISM mlock case */
		    shmd->shm_vpage[anon_index] & DISM_PG_LOCKED) ||

		    (SPT_OSM(sptd) &&		/* OSM case */
		    sptd->spt_ppa_lckcnt[anon_index])) {

			anon_array_enter(amp, anon_index, &cookie);
			ap = anon_get_ptr(amp->ahp, anon_index);
			ASSERT(ap);

			swap_xlate(ap, &vp, &off);
			anon_array_exit(&cookie);
			pp = page_lookup(vp, off, SE_SHARED);
			ASSERT(pp);
			/*
			 * availrmem is decremented only for pages which are not
			 * in seg pcache, for pages in seg pcache availrmem was
			 * decremented in _dismpagelock()
			 */
			kernel = (sptd->spt_ppa && sptd->spt_ppa[anon_index]);
			ASSERT(pp->p_lckcnt > 0);

			/*
			 * unlock page but do not change availrmem until
			 * we are done with unlocking all pages.
			 */
			page_pp_unlock(pp, 0, 1);
			if (pp->p_lckcnt == 0) {
				if (kernel == 0)
					nlck++;
				unlocked += PAGESIZE;
			}
			page_unlock(pp);
			shmd->shm_vpage[anon_index] &= ~DISM_PG_LOCKED;
			sptd->spt_ppa_lckcnt[anon_index]--;
			shm_lckpgs++;
		}
	}
	ANON_LOCK_EXIT(&amp->a_rwlock);

	if (nlck > 0) {
		mutex_enter(&freemem_lock);
		availrmem	+= nlck;
		mutex_exit(&freemem_lock);
	}

	if (unlocked) {
		mutex_enter(&freemem_lock);
		pages_locked    -= btop(unlocked);
		mutex_exit(&freemem_lock);
	}
	if (SPT_OSM(sptd) && unlocked) {
		struct kshmid   *sp = sptd->spt_amp->a_sp;

		atomic_add_long((ulong_t *)&sp->shm_allocated,
		    -((long)unlocked));
	}
	atomic_add_long(((ulong_t *)&shmd->shm_lckpgs), -((long)shm_lckpgs));
	atomic_add_64(&targp->unlocked, unlocked);
	*ridx = i;

	return (0);
}

/*
 * This implements MC_LOCK for DISM and MC_LOCK_GRANULE for OSM.
 * The MC_LOCK locks page _once_ per process. Any further mlocks (in the
 * same process) do not have any affect. If two processes lock the same page
 * then it has to be unlocked twice (per each process) be get unlocked.
 *
 * The MC_LOCK_GRANULE allocates and locks pages once per process. Any
 * subsequent locks on the same page in the same process or _other_ processes
 * have no effect. Locked pages are visible to all attached processes.
 * The OSM locked page needs to be unlocked once by _any_ process (not necessary
 * the one which originally locked it) to get unlocked and freed. Any
 * subsequent unlocks have no effect.
 *
 * After pages are locked for DISM the DISM_PPA_CHANGED is set, which causes
 * the entire spt_ppa[] to be discarded and regenerated in a future call to
 * segspt_dismpagelock().
 *
 * For OSM the above is true if spt_ppa[] does not exist. If spt_ppa[] does
 * exist then newly locked pages are added directly to spt_ppa[] to avoid
 * spt_ppa[] regeneration.
 *
 * After pages are unlocked for DISM set DISM_PPA_CHANGED and return
 * immediately. The spt_ppa[] is lazily deleted later.
 *
 * For OSM set DISM_PPA_CHANGED and wait for all IO to stop. Once IO is
 * stopped delete spt_ppa[] _and_ free all pages.
 */
/*ARGSUSED*/
static int
segspt_shmlockop(struct seg *seg, caddr_t addr, size_t len,
    int attr, int op, ulong_t *lockmap, size_t pos)
{
	struct shm_data	*shmd = seg->s_data;
	struct seg	*sptseg = shmd->shm_sptseg;
	struct spt_data	*sptd = sptseg->s_data;
	struct kshmid	*sp = sptd->spt_amp->a_sp;
	pgcnt_t		npages, a_npages;
	page_t		**ppa;
	pgcnt_t 	an_idx, a_an_idx, ppa_idx;
	caddr_t		spt_addr, a_addr;	/* spt and aligned address */
	size_t		a_len;			/* aligned len */
	size_t		share_sz;
	int		sts = 0;
	rctl_qty_t	unlocked = 0;
	rctl_qty_t	locked = 0;
	struct proc	*p = curproc;
	kproject_t	*proj;

	ASSERT(seg->s_as && AS_LOCK_HELD(seg->s_as, &seg->s_as->a_lock));
	ASSERT(sp != NULL);

	if ((sptd->spt_flags & SHM_PAGEABLE) == 0) {
		return (0);
	}

	/*
	 * For shmget_osm() mlock()/munlock() is not supported.
	 */
	if (SPT_OSM(sptd) && (op == MC_LOCK || op == MC_UNLOCK)) {
		return (0);
	}

	/*
	 * For shmget_osm() memory the addr and len for MC_LOCK_GRANULE/
	 * MC_UNLOCK_GRANULE has to be aligned on granule size.
	 */
	if (op == MC_LOCK_GRANULE || op == MC_UNLOCK_GRANULE) {
		if (!IS_P2ALIGNED(addr, sptd->spt_granule_sz) ||
		    !IS_P2ALIGNED(len, sptd->spt_granule_sz))
			return (EINVAL);
	}

	addr = (caddr_t)((uintptr_t)addr & (uintptr_t)PAGEMASK);
	an_idx = seg_page(seg, addr);
	npages = btopr(len);

	if (an_idx + npages > btopr(shmd->shm_amp->size)) {
		return (ENOMEM);
	}

	/*
	 * A shm's project never changes, so no lock needed.
	 * The shm has a hold on the project, so it will not go away.
	 * Since we have a mapping to shm within this zone, we know
	 * that the zone will not go away.
	 */
	proj = sp->shm_perm.ipc_proj;

	if ((op == MC_LOCK) || (op == MC_LOCK_GRANULE)) {

		/*
		 * Need to align addr and size request if they are not
		 * aligned so we can always allocate large page(s) however
		 * we only lock what was requested in initial request.
		 */
		share_sz = page_get_pagesize(sptseg->s_szc);
		a_addr = (caddr_t)P2ALIGN((uintptr_t)(addr), share_sz);
		a_len = P2ROUNDUP((uintptr_t)(((addr + len) - a_addr)),
		    share_sz);
		a_an_idx = seg_page(seg, a_addr);
		spt_addr = sptseg->s_base + ptob(a_an_idx);
		ppa_idx = an_idx - a_an_idx;

		/*
		 * And now we may have to adjust a_len downward if we have
		 * exceeded the realsize of the segment or initial anon
		 * allocations. Does not apply to osm because osm size
		 * has to be aligned on granule size.
		 */
		if ((spt_addr + a_len) >
		    (sptseg->s_base + sptd->spt_realsize)) {
			a_len = (sptseg->s_base + sptd->spt_realsize) -
			    spt_addr;
		}
		a_npages = btop(a_len);

		if ((ppa = kmem_zalloc(((sizeof (page_t *)) * a_npages),
		    KM_NOSLEEP)) == NULL) {
			return (ENOMEM);
		}
		if (op == MC_LOCK_GRANULE)
			rw_enter(&sptd->spt_mcglock, RW_WRITER);

		/*
		 * Don't cache any new pages for IO and
		 * flush any cached pages for DISM.
		 * For OSM we will cache new pages for IO
		 * when they are locked.
		 */
		mutex_enter(&sptd->spt_lock);
		if (sptd->spt_ppa != NULL && !SPT_OSM(sptd))
			sptd->spt_flags |= DISM_PPA_CHANGED;

		/*
		 * For tail end page allocations, the size has to be
		 * rounded back to preferred page size (even though
		 * ppa size was scaled back to reflect spt_realsize).
		 * Because, spt_anon_getpages() allocates pages in
		 * multiple of preferred page size chunks.
		 */
		sts = spt_anon_getpages(sptseg, spt_addr,
		    P2ROUNDUP(a_len, share_sz), ppa, 1);
		if (sts != 0) {
			mutex_exit(&sptd->spt_lock);
			if (op == MC_LOCK_GRANULE)
				rw_exit(&sptd->spt_mcglock);

			kmem_free(ppa, ((sizeof (page_t *)) * a_npages));
			return (sts);
		}

		mutex_enter(&sp->shm_mlock);
		/* enforce locked memory rctl */
		unlocked = spt_unlockedbytes(npages, &ppa[ppa_idx]);

		mutex_enter(&p->p_lock);
		if (rctl_incr_locked_mem(p, proj, unlocked, 0)) {
			mutex_exit(&p->p_lock);
			sts = EAGAIN;
		} else {
			mutex_exit(&p->p_lock);
			sts = spt_lockpages(seg, an_idx, npages,
			    &ppa[ppa_idx], lockmap, pos, &locked);

			/*
			 * correct locked count if not all pages could be
			 * locked
			 */
			if ((unlocked - locked) > 0) {
				rctl_decr_locked_mem(NULL, proj,
				    (unlocked - locked), 0);
			}
		}
		/*
		 * DISM mlocked pages are always unlocked here.
		 */
		if (!SPT_OSM(sptd)) {
			segspt_unlock_npages(sptseg, a_npages, ppa, 0);
		}

		if (sptd->spt_ppa != NULL && !SPT_OSM(sptd))
			sptd->spt_flags |= DISM_PPA_CHANGED;

		mutex_exit(&sp->shm_mlock);
		mutex_exit(&sptd->spt_lock);

		if (op == MC_LOCK_GRANULE)
			rw_exit(&sptd->spt_mcglock);

		kmem_free(ppa, ((sizeof (page_t *)) * a_npages));

	} else if (op == MC_UNLOCK || op == MC_UNLOCK_GRANULE) { /* unlock */
		page_t		**ppa;

		if (op == MC_UNLOCK_GRANULE) {
			rw_enter(&sptd->spt_mcglock, RW_WRITER);
		}

		mutex_enter(&sptd->spt_lock);
		if (op != MC_UNLOCK_GRANULE && shmd->shm_lckpgs == 0) {
			mutex_exit(&sptd->spt_lock);
			return (0);
		}

		/*
		 * ppa array needs to be rebuilt (if it is cached)
		 * because pages are unlocked.
		 */
		if (sptd->spt_ppa != NULL)
			sptd->spt_flags |= DISM_PPA_CHANGED;

		/*
		 * Don't cache any new pages for IO while OSM pages
		 * are munlocked/freed.
		 */
		if (SPT_OSM(sptd))
			sptd->spt_flags |= OSM_MEM_FREED;

		mutex_enter(&sp->shm_mlock);
		sts = spt_unlockpages(seg, an_idx, npages, &unlocked);
		ppa = sptd->spt_ppa;
		mutex_exit(&sptd->spt_lock);

		rctl_decr_locked_mem(NULL, proj, unlocked, 0);
		mutex_exit(&sp->shm_mlock);

		if (ppa != NULL)
			seg_ppurge_wiredpp(ppa);

		if (op == MC_UNLOCK_GRANULE) {
			struct anon_map *amp;
			pgcnt_t pg_idx;
			int	writer;
			ushort_t gen;
			clock_t end_lbolt;

			amp = sptd->spt_amp;
			pg_idx = seg_page(seg, addr);

			mutex_enter(&sptd->spt_lock);
			if ((ppa = sptd->spt_ppa) == NULL) {
				mutex_exit(&sptd->spt_lock);

				ANON_LOCK_ENTER(&amp->a_rwlock, RW_READER);
				anon_disclaim(amp, pg_idx, len, 1);
				ANON_LOCK_EXIT(&amp->a_rwlock);

				/*
				 * No need to grab spt_lock since all pages
				 * have been freed after that point the ppa
				 * can be rebuilt in segspt_dismpagelock().
				 */
				sptd->spt_flags &= ~OSM_MEM_FREED;
				rw_exit(&sptd->spt_mcglock);
				return (0);
			}

			gen = sptd->spt_gen;
			mutex_exit(&sptd->spt_lock);

			/*
			 * Purge all DISM cached pages
			 */
			seg_ppurge_wiredpp(ppa);

			/*
			 * Drop the AS_LOCK so that other threads can grab
			 * it in the as_pageunlock path and hopefully get
			 * the segment kicked out of the seg_pcache. We bump
			 * the shm_softlockcnt to keep this segment resident.
			 */
			writer = AS_WRITE_HELD(seg->s_as, &seg->s_as->a_lock);
			atomic_add_long((ulong_t *)
			    (&(shmd->shm_softlockcnt)), 1);
			AS_LOCK_EXIT(seg->s_as, &seg->s_as->a_lock);

			mutex_enter(&sptd->spt_lock);
			end_lbolt = ddi_get_lbolt() + (hz * spt_pcache_wait);

			/*
			 * Try to wait for pages to get kicked out of the
			 * seg_pcache.
			 */
			while (sptd->spt_gen == gen &&
			    (sptd->spt_flags & DISM_PPA_CHANGED) &&
			    ddi_get_lbolt() < end_lbolt) {
				if (!cv_timedwait_sig(&sptd->spt_cv,
				    &sptd->spt_lock, end_lbolt)) {
					break;
				}
			}
			mutex_exit(&sptd->spt_lock);

			/*
			 * Regrab the AS_LOCK and release our hold on the
			 * segment
			 */
			AS_LOCK_ENTER(seg->s_as, &seg->s_as->a_lock,
			    writer ? RW_WRITER : RW_READER);
			atomic_add_long((ulong_t *)
			    (&(shmd->shm_softlockcnt)), -1);
			if (shmd->shm_softlockcnt <= 0) {
				if (AS_ISUNMAPWAIT(seg->s_as)) {
					mutex_enter(&seg->s_as->a_contents);
					if (AS_ISUNMAPWAIT(seg->s_as)) {
						AS_CLRUNMAPWAIT(seg->s_as);
						cv_broadcast(&seg->s_as->a_cv);
					}
					mutex_exit(&seg->s_as->a_contents);
				}
			}

			ANON_LOCK_ENTER(&amp->a_rwlock, RW_READER);
			anon_disclaim(amp, pg_idx, len, 1);
			ANON_LOCK_EXIT(&amp->a_rwlock);

			/*
			 * No need to grab spt_lock since all pages
			 * have been freed after that point the ppa
			 * can be rebuilt in segspt_dismpagelock().
			 */
			sptd->spt_flags &= ~OSM_MEM_FREED;
			rw_exit(&sptd->spt_mcglock);
		}
	}
	return (sts);
}

/*ARGSUSED*/
int
segspt_shmgetprot(struct seg *seg, caddr_t addr, size_t len, uint_t *protv)
{
	struct shm_data *shmd = (struct shm_data *)seg->s_data;
	struct spt_data *sptd = (struct spt_data *)shmd->shm_sptseg->s_data;
	spgcnt_t pgno = seg_page(seg, addr+len) - seg_page(seg, addr) + 1;

	ASSERT(seg->s_as && AS_LOCK_HELD(seg->s_as, &seg->s_as->a_lock));

	/*
	 * ISM segment is always rw.
	 */
	while (--pgno >= 0)
		*protv++ = sptd->spt_prot;
	return (0);
}

/*ARGSUSED*/
u_offset_t
segspt_shmgetoffset(struct seg *seg, caddr_t addr)
{
	ASSERT(seg->s_as && AS_LOCK_HELD(seg->s_as, &seg->s_as->a_lock));

	/* Offset does not matter in ISM memory */

	return ((u_offset_t)0);
}

/* ARGSUSED */
int
segspt_shmgettype(struct seg *seg, caddr_t addr)
{
	struct shm_data *shmd = (struct shm_data *)seg->s_data;
	struct spt_data *sptd = (struct spt_data *)shmd->shm_sptseg->s_data;

	ASSERT(seg->s_as && AS_LOCK_HELD(seg->s_as, &seg->s_as->a_lock));

	/*
	 * The shared memory mapping is always MAP_SHARED, SWAP is only
	 * reserved for DISM
	 */
	return (MAP_SHARED |
	    ((sptd->spt_flags & SHM_PAGEABLE) ? 0 : MAP_NORESERVE));
}

/*ARGSUSED*/
int
segspt_shmgetvp(struct seg *seg, caddr_t addr, struct vnode **vpp)
{
	struct shm_data *shmd = (struct shm_data *)seg->s_data;
	struct spt_data *sptd = (struct spt_data *)shmd->shm_sptseg->s_data;

	ASSERT(seg->s_as && AS_LOCK_HELD(seg->s_as, &seg->s_as->a_lock));

	*vpp = sptd->spt_vp;
	return (0);
}

/*ARGSUSED*/
static int
segspt_shmadvise(struct seg *seg, caddr_t addr, size_t len, uint_t behav)
{
	struct shm_data	*shmd = (struct shm_data *)seg->s_data;
	struct spt_data	*sptd = (struct spt_data *)shmd->shm_sptseg->s_data;
	struct anon_map	*amp;
	pgcnt_t pg_idx;
	ushort_t gen;
	clock_t	end_lbolt;
	int writer;
	page_t **ppa;

	ASSERT(seg->s_as && AS_LOCK_HELD(seg->s_as, &seg->s_as->a_lock));

	if (behav == MADV_FREE) {
		if (((sptd->spt_flags & SHM_PAGEABLE) == 0) || SPT_OSM(sptd))
			return (0);

		amp = sptd->spt_amp;
		pg_idx = seg_page(seg, addr);

		mutex_enter(&sptd->spt_lock);
		if ((ppa = sptd->spt_ppa) == NULL) {
			mutex_exit(&sptd->spt_lock);
			ANON_LOCK_ENTER(&amp->a_rwlock, RW_READER);
			anon_disclaim(amp, pg_idx, len, 0);
			ANON_LOCK_EXIT(&amp->a_rwlock);
			return (0);
		}

		sptd->spt_flags |= DISM_PPA_CHANGED;
		gen = sptd->spt_gen;

		mutex_exit(&sptd->spt_lock);

		/*
		 * Purge all DISM cached pages
		 */
		seg_ppurge_wiredpp(ppa);

		/*
		 * Drop the AS_LOCK so that other threads can grab it
		 * in the as_pageunlock path and hopefully get the segment
		 * kicked out of the seg_pcache.  We bump the shm_softlockcnt
		 * to keep this segment resident.
		 */
		writer = AS_WRITE_HELD(seg->s_as, &seg->s_as->a_lock);
		atomic_add_long((ulong_t *)(&(shmd->shm_softlockcnt)), 1);
		AS_LOCK_EXIT(seg->s_as, &seg->s_as->a_lock);

		mutex_enter(&sptd->spt_lock);

		end_lbolt = ddi_get_lbolt() + (hz * spt_pcache_wait);

		/*
		 * Try to wait for pages to get kicked out of the seg_pcache.
		 */
		while (sptd->spt_gen == gen &&
		    (sptd->spt_flags & DISM_PPA_CHANGED) &&
		    ddi_get_lbolt() < end_lbolt) {
			if (!cv_timedwait_sig(&sptd->spt_cv,
			    &sptd->spt_lock, end_lbolt)) {
				break;
			}
		}

		mutex_exit(&sptd->spt_lock);

		/* Regrab the AS_LOCK and release our hold on the segment */
		AS_LOCK_ENTER(seg->s_as, &seg->s_as->a_lock,
		    writer ? RW_WRITER : RW_READER);
		atomic_add_long((ulong_t *)(&(shmd->shm_softlockcnt)), -1);
		if (shmd->shm_softlockcnt <= 0) {
			if (AS_ISUNMAPWAIT(seg->s_as)) {
				mutex_enter(&seg->s_as->a_contents);
				if (AS_ISUNMAPWAIT(seg->s_as)) {
					AS_CLRUNMAPWAIT(seg->s_as);
					cv_broadcast(&seg->s_as->a_cv);
				}
				mutex_exit(&seg->s_as->a_contents);
			}
		}

		ANON_LOCK_ENTER(&amp->a_rwlock, RW_READER);
		anon_disclaim(amp, pg_idx, len, 0);
		ANON_LOCK_EXIT(&amp->a_rwlock);
	} else if (lgrp_optimizations() && (behav == MADV_ACCESS_LWP ||
	    behav == MADV_ACCESS_MANY || behav == MADV_ACCESS_MANY_PSET ||
	    behav == MADV_ACCESS_DEFAULT)) {
		int			already_set;
		ulong_t			anon_index;
		lgrp_mem_policy_t	policy;
		caddr_t			shm_addr;
		size_t			share_size;
		size_t			size;
		struct seg		*sptseg = shmd->shm_sptseg;
		caddr_t			sptseg_addr;

		if (SPT_OSM(sptd)) {
			if (!IS_P2ALIGNED(addr, sptd->spt_granule_sz) ||
			    !IS_P2ALIGNED(len, sptd->spt_granule_sz))
				return (EINVAL);
		}

		/*
		 * Align address and length to page size of underlying segment
		 * For osm addr and len have to be aligned on granule size
		 * so nothing is done.
		 */
		share_size = page_get_pagesize(shmd->shm_sptseg->s_szc);
		shm_addr = (caddr_t)P2ALIGN((uintptr_t)(addr), share_size);
		size = P2ROUNDUP((uintptr_t)(((addr + len) - shm_addr)),
		    share_size);

		amp = shmd->shm_amp;
		anon_index = seg_page(seg, shm_addr);

		/*
		 * And now we may have to adjust size downward if we have
		 * exceeded the realsize of the segment or initial anon
		 * allocations. Does not apply to osm because osm size
		 * has to be aligned on granule size.
		 */
		sptseg_addr = sptseg->s_base + ptob(anon_index);
		if ((sptseg_addr + size) >
		    (sptseg->s_base + sptd->spt_realsize))
			size = (sptseg->s_base + sptd->spt_realsize) -
			    sptseg_addr;

		/*
		 * Set memory allocation policy for this segment
		 */
		policy = lgrp_madv_to_policy(behav, len, MAP_SHARED);
		already_set = lgrp_shm_policy_set(policy, amp, anon_index,
		    NULL, 0, len);

		/*
		 * If random memory allocation policy set already,
		 * don't bother reapplying it.
		 */
		if (already_set && !LGRP_MEM_POLICY_REAPPLICABLE(policy))
			return (0);

		/*
		 * Mark any existing pages in the given range for
		 * migration, flushing the I/O page cache, and using
		 * underlying segment to calculate anon index and get
		 * anonmap and vnode pointer from
		 */
		if (shmd->shm_softlockcnt > 0)
			segspt_purge(seg);

		page_mark_migrate(seg, shm_addr, size, amp, 0, NULL, 0, 0);
	}

	return (0);
}

/*ARGSUSED*/
void
segspt_shmdump(struct seg *seg)
{
	/* no-op for ISM segment */
}

/*ARGSUSED*/
static faultcode_t
segspt_shmsetpgsz(struct seg *seg, caddr_t addr, size_t len, uint_t szc)
{
	return (ENOTSUP);
}

/*
 * get a memory ID for an addr in a given segment
 */
static int
segspt_shmgetmemid(struct seg *seg, caddr_t addr, memid_t *memidp)
{
	struct shm_data *shmd = (struct shm_data *)seg->s_data;
	struct anon 	*ap;
	size_t		anon_index;
	struct anon_map	*amp = shmd->shm_amp;
	struct spt_data	*sptd = shmd->shm_sptseg->s_data;
	struct seg	*sptseg = shmd->shm_sptseg;
	anon_sync_obj_t	cookie;

	anon_index = seg_page(seg, addr);

	if (addr > (seg->s_base + sptd->spt_realsize)) {
		return (EFAULT);
	}

	ANON_LOCK_ENTER(&amp->a_rwlock, RW_READER);
	anon_array_enter(amp, anon_index, &cookie);
	ap = anon_get_ptr(amp->ahp, anon_index);
	if (ap == NULL) {
		struct page *pp;
		caddr_t spt_addr = sptseg->s_base + ptob(anon_index);

		pp = anon_zero(sptseg, spt_addr, &ap, kcred);
		if (pp == NULL) {
			anon_array_exit(&cookie);
			ANON_LOCK_EXIT(&amp->a_rwlock);
			return (ENOMEM);
		}
		(void) anon_set_ptr(amp->ahp, anon_index, ap, ANON_SLEEP);
		page_unlock(pp);
	}
	anon_array_exit(&cookie);
	ANON_LOCK_EXIT(&amp->a_rwlock);
	memidp->val[0] = (uintptr_t)ap;
	memidp->val[1] = (uintptr_t)addr & PAGEOFFSET;
	return (0);
}

/*
 * Get memory allocation policy info for specified address in given segment
 */
static lgrp_mem_policy_info_t *
segspt_shmgetpolicy(struct seg *seg, caddr_t addr)
{
	struct anon_map		*amp;
	ulong_t			anon_index;
	lgrp_mem_policy_info_t	*policy_info;
	struct shm_data		*shm_data;

	ASSERT(seg != NULL);

	/*
	 * Get anon_map from segshm
	 *
	 * Assume that no lock needs to be held on anon_map, since
	 * it should be protected by its reference count which must be
	 * nonzero for an existing segment
	 * Need to grab readers lock on policy tree though
	 */
	shm_data = (struct shm_data *)seg->s_data;
	if (shm_data == NULL)
		return (NULL);
	amp = shm_data->shm_amp;
	ASSERT(amp->refcnt != 0);

	/*
	 * Get policy info
	 *
	 * Assume starting anon index of 0
	 */
	anon_index = seg_page(seg, addr);
	policy_info = lgrp_shm_policy_get(amp, anon_index, NULL, 0);

	return (policy_info);
}

/*ARGSUSED*/
static int
segspt_shmcapable(struct seg *seg, segcapability_t capability)
{
	struct shm_data *shmd = seg->s_data;
	struct spt_data *sptd = shmd->shm_sptseg->s_data;

	if (SPT_OSM(sptd)) {
		if (capability == S_CAPABILITY_LOCK_GRANULE) {
			return (1);
		}
	}
	return (0);
}
