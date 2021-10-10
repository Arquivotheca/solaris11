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
 * Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/mman.h>
#include <sys/buf.h>
#include <sys/vmem.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/machparam.h>
#include <sys/io_mvec_impl.h>
#include <sys/modctl.h>
#include <vm/page.h>
#include <vm/seg_kmem.h>
#include <vm/seg_kpm.h>

#ifdef __sparc
#include <sys/cpu_module.h>
#define	BP_FLUSH(addr, size)	flush_instr_mem((void *)addr, size);
#else
#define	BP_FLUSH(addr, size)
#endif

int bp_force_copy = 0;
typedef enum {
	BP_COPYIN	= 0,
	BP_COPYOUT	= 1
} bp_copydir_t;
static int bp_copy_common(bp_copydir_t dir, struct buf *bp, void *driverbuf,
    offset_t offset, size_t size);

static vmem_t *bp_map_arena;
static size_t bp_align;
static uint_t bp_devload_flags = PROT_READ | PROT_WRITE | HAT_NOSYNC;
int	bp_max_cache = 1 << 17;		/* 128K default; tunable */
int	bp_mapin_kpm_enable = 1;	/* enable default; tunable */

static void *
bp_vmem_alloc(vmem_t *vmp, size_t size, int vmflag)
{
	return (vmem_xalloc(vmp, size, bp_align, 0, 0, NULL, NULL, vmflag));
}

void
bp_init(size_t align, uint_t devload_flags)
{
	bp_align = MAX(align, PAGESIZE);
	bp_devload_flags |= devload_flags;

	if (bp_align <= bp_max_cache)
		bp_map_arena = vmem_create("bp_map", NULL, 0, bp_align,
		    bp_vmem_alloc, vmem_free, heap_arena,
		    MIN(8 * bp_align, bp_max_cache), VM_SLEEP);
}

/*
 * Convert a bp so that b_un.b_addr is a kernel addressable memory location.
 *
 * Not all bp_mapin(9F) calls result in setting the private buf(9S)
 * B_REMAPPED flag.
 *
 * If a bp_mapin(9F) call that sets B_REMAPPED might occur somewhere
 * in the IO stack, that mapping must be torn down by a bp_mapout(9F)
 * call performed by the code that originally allocated/cloned the buf(9S).
 *
 * Currently, the (B_PAGEIO | B_PHYS | B_MVECTOR) b_flags established by the
 * allocating/cloning code determine whether a bp_mapin(9F) in the
 * IO stack might end up setting B_REMAPPED.
 *
 * When a buf(9S) is cloned via bioclone(9F), a buf(9S) is being allocated
 * (bp_mem agrument of NULL) or setup (non-NULL bp_mem). In general, the
 * bioclone caller has no knowledge of whether the IO stack might set
 * B_REMAPPED on a cloned buf(9S), so a bp_mapout(9F) is always needed.
 * When the bp_mem argument to bioclone(9S) is NULL, the returned buf must be
 * freed by freerbuf(9F), and the freerbuf(9F) implementation will do the
 * bp_mapout(9F). When bp_mem is non-NULL, the bioclone(9F) caller is always
 * responsible for calling bp_mapout from its iodone callback.
 *
 * NOTE: In the future, it would be better (less bug-prone) if bp_mapout
 * occurred as a side-effect of easier to understand, well-scoped, call-paired
 * alloc/free init/fini buf interfaces like getrbuf/freerbuf, bioinit/biofini,
 * scsi_alloc_consistent_buf/scsi_free_consistent_buf, etc.
 */
void
bp_mapin(struct buf *bp)
{
	caddr_t	addr;

	addr = bp_mapin_common(bp, VM_SLEEP);

	/* We should never be called with a buf that can't be mapped. */
	if (bp->b_bcount && (addr == NULL))
		cmn_err(CE_WARN, "bp_mapin: buf can't be mapped by %s",
		    mod_containing_pc(caller()));
	ASSERT((bp->b_bcount == 0) || addr);
}

/*
 * common routine so can be called with/without VM_SLEEP
 */
void *
bp_mapin_common(struct buf *bp, int flag)
{
	struct as	*as;
	pfn_t		pfnum;
	page_t		*pp;
	page_t		**pplist;
	caddr_t		kaddr;
	caddr_t		addr;
	uintptr_t	off;
	size_t		size;
	pgcnt_t		npages;
	int		color;

	/*
	 * return if already mapped in, no pageio/physio/mvector, or physio
	 * to kas
	 */
	if ((bp->b_flags & B_REMAPPED) ||
	    !(bp->b_flags & (B_PAGEIO | B_PHYS | B_MVECTOR)) ||
	    (((bp->b_flags & (B_PAGEIO | B_PHYS | B_MVECTOR)) == B_PHYS) &&
	    ((bp->b_proc == NULL) || (bp->b_proc->p_as == &kas))))
		return (bp->b_un.b_addr);

	if (bp->b_bcount == 0)
		return (NULL);

	/*
	 * if we have a mvector, we can't present a single VA for the
	 * entire buf_t. allocate a mapped buffer and we'll work out
	 * of that until we bp_mapout.
	 */
	if (bp->b_flags & B_MVECTOR) {
		io_mvec_bshadow_t *mvshadow;
		int rc;

		/* B_SHADOW and B_MVECTOR don't make sense together */
		ASSERT((bp->b_flags & B_SHADOW) == 0);

		/* stow away the mvector in the b_shadow field */
		bp->b_shadow = kmem_alloc(sizeof (io_mvec_bshadow_t),
		    flag & VM_KMFLAGS);
		if (bp->b_shadow == NULL)
			return (NULL);

		mvshadow = (io_mvec_bshadow_t *)bp->b_shadow;
		mvshadow->ms_mvector = bp->b_un.b_mvector;
		mvshadow->ms_bufsize = bp->b_bcount + PAGESIZE;
		mvshadow->ms_buf = kmem_alloc(mvshadow->ms_bufsize,
		    flag & VM_KMFLAGS);
		if (mvshadow->ms_buf == NULL) {
			kmem_free(mvshadow, sizeof (io_mvec_bshadow_t));
			bp->b_shadow = NULL;
			return (NULL);
		}

		bp->b_un.b_addr = (caddr_t)(((uintptr_t)mvshadow->ms_buf +
		    PAGEOFFSET) & PAGEMASK);

		/* mapin the mvector (which is now stowed in b_shadow) */
		rc = io_mvector_mapin(mvshadow->ms_mvector,
		    (flag & VM_KMFLAGS) | IO_MVECTOR_MAPIN_FLAG_COPYONLY);
		if (rc != 0) {
			kmem_free(mvshadow->ms_buf, mvshadow->ms_bufsize);
			/* b_un is union */
			bp->b_un.b_mvector = mvshadow->ms_mvector;
			kmem_free(mvshadow, sizeof (io_mvec_bshadow_t));
			bp->b_shadow = NULL;
			return (NULL);
		}

		/*
		 * if this is a write, copy the mvector to the mapped buffer.
		 * we know the copy will always pass since the mvector has been
		 * mapped in.
		 */
		if (bp->b_flags & B_WRITE) {
			(void) io_mvector_copyout(mvshadow->ms_mvector,
			    bp->b_un.b_addr, 0, bp->b_bcount);
		}

		/*
		 * update the buf so that we no longer have an mvector, but
		 * a remapped buf with the mvector stored in b_shadow.
		 */
		bp->b_flags &= ~B_MVECTOR;
		bp->b_flags |= B_MVEC_SHADOW | B_REMAPPED;

		return (bp->b_un.b_addr);
	}

	ASSERT((bp->b_flags & (B_PAGEIO | B_PHYS)) != (B_PAGEIO | B_PHYS));

	addr = (caddr_t)bp->b_un.b_addr;
	off = (uintptr_t)addr & PAGEOFFSET;
	size = P2ROUNDUP(bp->b_bcount + off, PAGESIZE);
	npages = btop(size);

	/* Fastpath single page IO to locked memory by using kpm. */
	if ((bp->b_flags & (B_SHADOW | B_PAGEIO)) && (npages == 1) &&
	    kpm_enable && bp_mapin_kpm_enable) {
		if (bp->b_flags & B_SHADOW)
			pp = *bp->b_shadow;
		else
			pp = bp->b_pages;
		kaddr = hat_kpm_mapin(pp, NULL);
		bp->b_un.b_addr = kaddr + off;
		bp->b_flags |= B_REMAPPED;
		return (bp->b_un.b_addr);
	}

	/*
	 * Allocate kernel virtual space for remapping.
	 */
	color = bp_color(bp);
	ASSERT(color < bp_align);

	if (bp_map_arena != NULL) {
		kaddr = (caddr_t)vmem_alloc(bp_map_arena,
		    P2ROUNDUP(color + size, bp_align), flag);
		if (kaddr == NULL)
			return (NULL);
		kaddr += color;
	} else {
		kaddr = vmem_xalloc(heap_arena, size, bp_align, color,
		    0, NULL, NULL, flag);
		if (kaddr == NULL)
			return (NULL);
	}

	ASSERT(P2PHASE((uintptr_t)kaddr, bp_align) == color);

	/*
	 * Map bp into the virtual space we just allocated.
	 */
	if (bp->b_flags & B_PAGEIO) {
		pp = bp->b_pages;
		pplist = NULL;
	} else if (bp->b_flags & B_SHADOW) {
		pp = NULL;
		pplist = bp->b_shadow;
	} else {
		pp = NULL;
		pplist = NULL;
		if (bp->b_proc == NULL || (as = bp->b_proc->p_as) == NULL)
			as = &kas;
	}

	bp->b_flags |= B_REMAPPED;
	bp->b_un.b_addr = kaddr + off;

	while (npages-- != 0) {
		if (pp) {
			pfnum = pp->p_pagenum;
			pp = pp->p_next;
		} else if (pplist == NULL) {
			pfnum = hat_getpfnum(as->a_hat,
			    (caddr_t)((uintptr_t)addr & MMU_PAGEMASK));
			if (pfnum == PFN_INVALID)
				panic("bp_mapin_common: hat_getpfnum for"
				    " addr %p failed\n", (void *)addr);
			addr += PAGESIZE;
		} else {
			pfnum = (*pplist)->p_pagenum;
			pplist++;
		}

		hat_devload(kas.a_hat, kaddr, PAGESIZE, pfnum,
		    bp_devload_flags, HAT_LOAD_LOCK);

		kaddr += PAGESIZE;
	}
	return (bp->b_un.b_addr);
}

/*
 * Release all the resources associated with a previous bp_mapin() call.
 *
 * WARNING: See bp_mapin() comment above to determine if you should be
 * calling bp_mapout().
 */
void
bp_mapout(struct buf *bp)
{
	caddr_t		addr;
	uintptr_t	off;
	uintptr_t	base;
	uintptr_t	color;
	size_t		size;
	pgcnt_t		npages;
	page_t		*pp;

	if ((bp->b_flags & B_REMAPPED) == 0)
		return;				/* noop */

	/*
	 * if we mapped in a mvector, see if we need to sync the mvector,
	 * free up the mapped buffer, and restore the mvector
	 */
	if (bp->b_flags & B_MVEC_SHADOW) {
		io_mvec_bshadow_t *mvshadow;

		mvshadow = (io_mvec_bshadow_t *)bp->b_shadow;

		/*
		 * if this was a read, and we didn't copy the mapped buffer
		 * into the mvector in biodone(), do that now.
		 */
		if ((bp->b_flags & B_READ) &&
		    !i_io_mvector_insync(mvshadow->ms_mvector))
			/* we know we have a kaddr, so copyin can't fail */
			(void) io_mvector_copyin(bp->b_un.b_addr,
			    mvshadow->ms_mvector, 0, bp->b_bcount);

		/*
		 * free up the mapped buffer and switch the buf back to the
		 * original mvector.
		 */
		kmem_free(mvshadow->ms_buf, mvshadow->ms_bufsize);
		bp->b_un.b_mvector = mvshadow->ms_mvector; /* b_un is union */
		kmem_free(mvshadow, sizeof (io_mvec_bshadow_t));
		bp->b_shadow = NULL;
		bp->b_flags &= ~(B_MVEC_SHADOW | B_REMAPPED);
		bp->b_flags |= B_MVECTOR;

		return;
	}

	addr = bp->b_un.b_addr;
	off = (uintptr_t)addr & PAGEOFFSET;
	size = P2ROUNDUP(bp->b_bcount + off, PAGESIZE);
	npages = btop(size);

	bp->b_un.b_addr = (caddr_t)off;		/* debugging aid */

	if ((bp->b_flags & (B_SHADOW | B_PAGEIO)) && (npages == 1) &&
	    kpm_enable && bp_mapin_kpm_enable) {
		if (bp->b_flags & B_SHADOW)
			pp = *bp->b_shadow;
		else
			pp = bp->b_pages;
		addr = (caddr_t)((uintptr_t)addr & MMU_PAGEMASK);
		hat_kpm_mapout(pp, NULL, addr);
		bp->b_flags &= ~B_REMAPPED;
		return;
	}

	base = (uintptr_t)addr & MMU_PAGEMASK;
	BP_FLUSH(base, size);
	hat_unload(kas.a_hat, (void *)base, size,
	    HAT_UNLOAD_NOSYNC | HAT_UNLOAD_UNLOCK);
	if (bp_map_arena != NULL) {
		color = P2PHASE(base, bp_align);
		vmem_free(bp_map_arena, (void *)(base - color),
		    P2ROUNDUP(color + size, bp_align));
	} else
		vmem_free(heap_arena, (void *)base, size);
	bp->b_flags &= ~B_REMAPPED;
}

/*
 * copy data from a KVA into a buf_t which may not be mapped in. offset
 * is relative to the buf_t only.
 */
int
bp_copyout(void *driverbuf, struct buf *bp, offset_t offset, size_t size)
{
	if (bp->b_flags & B_MVECTOR)
		return (io_mvector_copyin(driverbuf, bp->b_un.b_mvector,
		    offset, size));

	return (bp_copy_common(BP_COPYOUT, bp, driverbuf, offset, size));
}

/*
 * copy data from a buf_t which may not be mapped in, into a KVA.. offset
 * is relative to the buf_t only.
 */
int
bp_copyin(struct buf *bp, void *driverbuf, offset_t offset, size_t size)
{
	if (bp->b_flags & B_MVECTOR)
		return (io_mvector_copyout(bp->b_un.b_mvector, driverbuf,
		    offset, size));

	return (bp_copy_common(BP_COPYIN, bp, driverbuf, offset, size));
}


#define	BP_COPY(dir, driverbuf, baddr, sz)	\
	(dir == BP_COPYIN) ? \
	bcopy(baddr, driverbuf, sz) :  bcopy(driverbuf, baddr, sz)

static int
bp_copy_common(bp_copydir_t dir, struct buf *bp, void *driverbuf,
    offset_t offset, size_t size)
{
	page_t **pplist;
	uintptr_t poff;
	uintptr_t voff;
	struct as *as;
	caddr_t kaddr;
	caddr_t addr;
	page_t *page;
	size_t psize;
	page_t *pp;
	pfn_t pfn;


	ASSERT((offset + size) <= bp->b_bcount);

	/*
	 * If the buf_t already has a KVA, just do a bcopy.
	 *
	 * NOTE: the following conditional should match the fast-path check at
	 * the beginning of bp_mapin_common().
	 */
	if ((bp->b_flags & B_REMAPPED) ||
	    !(bp->b_flags & (B_PAGEIO | B_PHYS | B_MVECTOR)) ||
	    (((bp->b_flags & (B_PAGEIO | B_PHYS | B_MVECTOR)) == B_PHYS) &&
	    ((bp->b_proc == NULL) || (bp->b_proc->p_as == &kas)))) {
		BP_COPY(dir, driverbuf, bp->b_un.b_addr + offset, size);
		return (0);
	}

	/*
	 * If we don't have kpm enabled, we need to do the slow path.
	 * We return without calling bp_mapout(9F) because we did not allocate
	 * the buf(9S).
	 */
	if (!kpm_enable || bp_force_copy) {
		bp_mapin(bp);
		BP_COPY(dir, driverbuf, bp->b_un.b_addr + offset, size);
		return (0);
	}

	/*
	 * kpm is enabled, and we need to map in the buf_t for the copy
	 */

	/* setup pp, plist, and make sure 'as' is right */
	if (bp->b_flags & B_PAGEIO) {
		pp = bp->b_pages;
		pplist = NULL;
	} else if (bp->b_flags & B_SHADOW) {
		pp = NULL;
		pplist = bp->b_shadow;
	} else {
		pp = NULL;
		pplist = NULL;
		if (bp->b_proc == NULL || (as = bp->b_proc->p_as) == NULL) {
			as = &kas;
		}
	}

	/*
	 * locals for the address, the offset into the first page, and the
	 * size of the first page we are going to copy.
	 */
	addr = (caddr_t)bp->b_un.b_addr;
	poff = (uintptr_t)addr & PAGEOFFSET;
	psize = MIN(PAGESIZE - poff, size);

	/*
	 * If we are starting at an offset with the buf_t, make the necessary
	 * adjustments (pplist, pp, addr, poff, and psize).
	 */
	if (offset) {
		uint32_t npages = btop(poff + offset);
		if (pplist) {
			pplist += npages;
		} else if (pp) {
			while (npages) {
				pp = pp->p_next;
				npages--;
			}
		}
		addr += offset;
		poff = (uintptr_t)addr & PAGEOFFSET;
		psize = MIN(PAGESIZE - poff, size);
	}

	/*
	 * we always start with a 0 offset into the driverbuf provided. The
	 * offset passed in only applies to the buf_t.
	 */
	voff = 0;

	/* Loop until we've copied al the data */
	while (size > 0) {

		/*
		 * for a pp or pplist, get the pfn, then go to the next page_t
		 * for the next time around the loop.
		 */
		if (pp) {
			page = pp;
			pp = pp->p_next;
		} else if (pplist) {
			page = (*pplist);
			pplist++;

		/*
		 * We have a user VA, then needs to figure out the page by pfn.
		 */
		} else {
			pfn = hat_getpfnum(as->a_hat,
			    (caddr_t)((uintptr_t)addr & PAGEMASK));
			if (pfn == PFN_INVALID) {
				return (-1);
			}
			page = page_numtopp_nolock(pfn);
			addr += psize;
		}

		/*
		 * get a kpm mapping to the page, them copy in/out of the
		 * page. update size left and offset into the driverbuf passed
		 * in for the next time around the loop.
		 */
		kaddr = hat_kpm_mapin(page, NULL) + poff;
		BP_COPY(dir, (void *)((uintptr_t)driverbuf + voff), kaddr,
		    psize);
		hat_kpm_mapout(page, NULL, kaddr - poff);

		size -= psize;
		voff += psize;

		poff = 0;
		psize = MIN(PAGESIZE, size);
	}

	return (0);
}
