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
 * Copyright (c) 1997, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/signal.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/mman.h>
#include <sys/class.h>
#include <sys/proc.h>
#include <sys/procfs.h>
#include <sys/kmem.h>
#include <sys/cred.h>
#include <sys/archsystm.h>
#include <sys/machsystm.h>

#include <sys/reboot.h>
#include <sys/uadmin.h>

#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/session.h>
#include <sys/ucontext.h>

#include <sys/dnlc.h>
#include <sys/var.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/thread.h>
#include <sys/vtrace.h>
#include <sys/consdev.h>
#include <sys/frame.h>
#include <sys/stack.h>
#include <sys/swap.h>
#include <sys/vmparam.h>
#include <sys/cpuvar.h>

#include <sys/privregs.h>

#include <vm/hat.h>
#include <vm/anon.h>
#include <vm/as.h>
#include <vm/page.h>
#include <vm/seg.h>
#include <vm/seg_kmem.h>
#include <vm/seg_map.h>
#include <vm/seg_vn.h>
#include <vm/vm_dep.h>

#include <sys/exec.h>
#include <sys/acct.h>
#include <sys/modctl.h>
#include <sys/tuneable.h>

#include <c2/audit.h>

#include <sys/trap.h>
#include <sys/sunddi.h>
#include <sys/bootconf.h>
#include <sys/memlist.h>
#include <sys/memlist_plat.h>
#include <sys/systeminfo.h>
#include <sys/promif.h>
#include <sys/prom_plat.h>

u_longlong_t	spec_hole_start = 0x80000000000ull;
u_longlong_t	spec_hole_end = 0xfffff80000000000ull;

pgcnt_t
num_phys_pages()
{
	pgcnt_t npages = 0;
	struct memlist *mp;

	for (mp = phys_install; mp != NULL; mp = mp->ml_next)
		npages += mp->ml_size >> PAGESHIFT;

	return (npages);
}


pgcnt_t
size_virtalloc(prom_memlist_t *avail, size_t nelems)
{

	u_longlong_t	start, end;
	pgcnt_t		allocpages = 0;
	uint_t		hole_allocated = 0;
	uint_t		i;

	for (i = 0; i < nelems - 1; i++) {

		start = avail[i].addr + avail[i].size;
		end = avail[i + 1].addr;

		/*
		 * Notes:
		 *
		 * (1) OBP on platforms with US I/II pre-allocates the hole
		 * represented by [spec_hole_start, spec_hole_end);
		 * pre-allocation is done to make this range unavailable
		 * for any allocation.
		 *
		 * (2) OBP on starcat always pre-allocates the hole similar to
		 * platforms with US I/II.
		 *
		 * (3) OBP on serengeti does _not_ pre-allocate the hole.
		 *
		 * (4) OBP ignores Spitfire Errata #21; i.e. it does _not_
		 * fill up or pre-allocate an additional 4GB on both sides
		 * of the hole.
		 *
		 * (5) kernel virtual range [spec_hole_start, spec_hole_end)
		 * is _not_ used on any platform including those with
		 * UltraSPARC III where there is no hole.
		 *
		 * Algorithm:
		 *
		 * Check if range [spec_hole_start, spec_hole_end) is
		 * pre-allocated by OBP; if so, subtract that range from
		 * allocpages.
		 */
		if (end >= spec_hole_end && start <= spec_hole_start)
			hole_allocated = 1;

		allocpages += btopr(end - start);
	}

	if (hole_allocated)
		allocpages -= btop(spec_hole_end - spec_hole_start);

	return (allocpages);
}

/*
 * Returns the max contiguous physical memory present in the
 * memlist "physavail".
 */
uint64_t
get_max_phys_size(
	struct memlist	*physavail)
{
	uint64_t	max_size = 0;

	for (; physavail; physavail = physavail->ml_next) {
		if (physavail->ml_size > max_size)
			max_size = physavail->ml_size;
	}

	return (max_size);
}


static void
more_pages(uint64_t base, uint64_t len)
{
	void kphysm_add();

	kphysm_add(base, len, 1);
}

static void
less_pages(uint64_t base, uint64_t len)
{
	uint64_t pa, end = base + len;
	extern int kcage_on;

	for (pa = base; pa < end; pa += PAGESIZE) {
		pfn_t pfnum;
		page_t *pp;

		pfnum = (pfn_t)(pa >> PAGESHIFT);
		if ((pp = page_numtopp_nolock(pfnum)) == NULL)
			cmn_err(CE_PANIC, "missing pfnum %lx", pfnum);

		/*
		 * must break up any large pages that may have
		 * constituent pages being utilized for
		 * prom_alloc()'s. page_reclaim() can't handle
		 * large pages.
		 */
		if (pp->p_szc != 0)
			page_boot_demote(pp);

		if (!PAGE_LOCKED(pp) && pp->p_lckcnt == 0) {
			/*
			 * Ahhh yes, a prom page,
			 * suck it off the freelist,
			 * lock it, and hashin on prom_pages vp.
			 */
			if (page_trylock(pp, SE_EXCL) == 0)
				cmn_err(CE_PANIC, "prom page locked");

			(void) page_reclaim(pp, NULL);
			page_hashin(pp, &promvp, (offset_t)pa, NULL);

			if (kcage_on) {
				ASSERT(pp->p_szc == 0);
				if (PP_ISNORELOC(pp) == 0) {
					PP_SETNORELOC(pp);
					PLCNT_XFER_NORELOC(pp);
				}
			}
			(void) page_pp_lock(pp, 0, 1);
		}
	}
}

void
diff_memlists(struct memlist *proto, struct memlist *diff, void (*func)())
{
	uint64_t p_base, p_end, d_base, d_end;

	while (proto != NULL) {
		/*
		 * find diff item which may overlap with proto item
		 * if none, apply func to all of proto item
		 */
		while (diff != NULL &&
		    proto->ml_address >= diff->ml_address + diff->ml_size)
			diff = diff->ml_next;
		if (diff == NULL) {
			(*func)(proto->ml_address, proto->ml_size);
			proto = proto->ml_next;
			continue;
		}
		if (proto->ml_address == diff->ml_address &&
		    proto->ml_size == diff->ml_size) {
			proto = proto->ml_next;
			diff = diff->ml_next;
			continue;
		}

		p_base = proto->ml_address;
		p_end = p_base + proto->ml_size;
		d_base = diff->ml_address;
		d_end = d_base + diff->ml_size;
		/*
		 * here p_base < d_end
		 * there are 5 cases
		 */

		/*
		 *	d_end
		 *	d_base
		 *  p_end
		 *  p_base
		 *
		 * apply func to all of proto item
		 */
		if (p_end <= d_base) {
			(*func)(p_base, proto->ml_size);
			proto = proto->ml_next;
			continue;
		}

		/*
		 * ...
		 *	d_base
		 *  p_base
		 *
		 * normalize by applying func from p_base to d_base
		 */
		if (p_base < d_base)
			(*func)(p_base, d_base - p_base);

		if (p_end <= d_end) {
			/*
			 *	d_end
			 *  p_end
			 *	d_base
			 *  p_base
			 *
			 *	-or-
			 *
			 *	d_end
			 *  p_end
			 *  p_base
			 *	d_base
			 *
			 * any non-overlapping ranges applied above,
			 * so just continue
			 */
			proto = proto->ml_next;
			continue;
		}

		/*
		 *  p_end
		 *	d_end
		 *	d_base
		 *  p_base
		 *
		 *	-or-
		 *
		 *  p_end
		 *	d_end
		 *  p_base
		 *	d_base
		 *
		 * Find overlapping d_base..d_end ranges, and apply func
		 * where no overlap occurs.  Stop when d_base is above
		 * p_end
		 */
		for (p_base = d_end, diff = diff->ml_next; diff != NULL;
		    p_base = d_end, diff = diff->ml_next) {
			d_base = diff->ml_address;
			d_end = d_base + diff->ml_size;
			if (p_end <= d_base) {
				(*func)(p_base, p_end - p_base);
				break;
			} else
				(*func)(p_base, d_base - p_base);
		}
		if (diff == NULL)
			(*func)(p_base, p_end - p_base);
		proto = proto->ml_next;
	}
}

void
sync_memlists(struct memlist *orig, struct memlist *new)
{

	/*
	 * Find pages allocated via prom by looking for
	 * pages on orig, but no on new.
	 */
	diff_memlists(orig, new, less_pages);

	/*
	 * Find pages free'd via prom by looking for
	 * pages on new, but not on orig.
	 */
	diff_memlists(new, orig, more_pages);
}


/*
 * Find the page number of the highest installed physical
 * page and the number of pages installed (one cannot be
 * calculated from the other because memory isn't necessarily
 * contiguous).
 */
void
installed_top_size_memlist_array(
	prom_memlist_t *list,	/* base of array */
	size_t	nelems,		/* number of elements */
	pfn_t *topp,		/* return ptr for top value */
	pgcnt_t *sumpagesp)	/* return prt for sum of installed pages */
{
	pfn_t top = 0;
	pgcnt_t sumpages = 0;
	pfn_t highp;		/* high page in a chunk */
	size_t i;

	for (i = 0; i < nelems; list++, i++) {
		highp = (list->addr + list->size - 1) >> PAGESHIFT;
		if (top < highp)
			top = highp;
		sumpages += (list->size >> PAGESHIFT);
	}

	*topp = top;
	*sumpagesp = sumpages;
}

/*
 * Copy a memory list.  Used in startup() to copy boot's
 * memory lists to the kernel.
 */
void
copy_memlist(
	prom_memlist_t	*src,
	size_t		nelems,
	struct memlist	**dstp)
{
	struct memlist *dst, *prev;
	size_t	i;

	dst = *dstp;
	prev = dst;

	for (i = 0; i < nelems; src++, i++) {
		dst->ml_address = src->addr;
		dst->ml_size = src->size;
		dst->ml_next = 0;
		if (prev == dst) {
			dst->ml_prev = 0;
			dst++;
		} else {
			dst->ml_prev = prev;
			prev->ml_next = dst;
			dst++;
			prev++;
		}
	}

	*dstp = dst;
}


static struct bootmem_props {
	prom_memlist_t	*ptr;
	size_t		nelems;		/* actual number of elements */
	size_t		maxsize;	/* max buffer */
} bootmem_props[3];

#define	PHYSINSTALLED	0
#define	PHYSAVAIL	1
#define	VIRTAVAIL	2

/*
 * Comapct contiguous memory list elements
 */
static void
compact_promlist(struct bootmem_props *bpp)
{
	int i = 0, j;
	struct prom_memlist *pmp = bpp->ptr;

	for (;;) {
		if (pmp[i].addr + pmp[i].size == pmp[i+1].addr) {
			pmp[i].size += pmp[i+1].size;
			bpp->nelems--;
			for (j = i + 1; j < bpp->nelems; j++)
				pmp[j] = pmp[j+1];
			pmp[j].addr = 0;
		} else
			i++;
		if (i == bpp->nelems)
			break;
	}
}

/*
 *  Sort prom memory lists into ascending order
 */
static void
sort_promlist(struct bootmem_props *bpp)
{
	int i, j, min;
	struct prom_memlist *pmp = bpp->ptr;
	struct prom_memlist temp;

	for (i = 0; i < bpp->nelems; i++) {
		min = i;

		for (j = i+1; j < bpp->nelems; j++)  {
			if (pmp[j].addr < pmp[min].addr)
				min = j;
		}

		if (i != min)  {
			/* Swap pmp[i] and pmp[min] */
			temp = pmp[min];
			pmp[min] = pmp[i];
			pmp[i] = temp;
		}
	}
}

static int max_bootlist_sz;

void
init_boot_memlists(void)
{
	size_t	size, len;
	char *start;
	struct bootmem_props *tmp;

	/*
	 * These lists can get fragmented as the prom allocates
	 * memory, so generously round up.
	 */
	size = prom_phys_installed_len() + prom_phys_avail_len() +
	    prom_virt_avail_len();
	size *= 4;
	size = roundup(size, PAGESIZE);
	start = prom_alloc(0, size, BO_NO_ALIGN);

	/*
	 * Get physinstalled
	 */
	tmp = &bootmem_props[PHYSINSTALLED];
	len = prom_phys_installed_len();
	if (len == 0)
		panic("no \"reg\" in /memory");
	tmp->nelems = len / sizeof (struct prom_memlist);
	tmp->maxsize = len;
	tmp->ptr = (prom_memlist_t *)start;
	start += len;
	size -= len;
	(void) prom_phys_installed((caddr_t)tmp->ptr);
	sort_promlist(tmp);
	compact_promlist(tmp);

	/*
	 * Start out giving each half of available space
	 */
	max_bootlist_sz = size;
	len = size / 2;
	tmp = &bootmem_props[PHYSAVAIL];
	tmp->maxsize = len;
	tmp->ptr = (prom_memlist_t *)start;
	start += len;

	tmp = &bootmem_props[VIRTAVAIL];
	tmp->maxsize = len;
	tmp->ptr = (prom_memlist_t *)start;
}


void
copy_boot_memlists(
    prom_memlist_t **physinstalled, size_t *physinstalled_len,
    prom_memlist_t **physavail, size_t *physavail_len,
    prom_memlist_t **virtavail, size_t *virtavail_len)
{
	size_t	plen, vlen, move = 0;
	struct bootmem_props *il, *pl, *vl;

	plen = prom_phys_avail_len();
	if (plen == 0)
		panic("no \"available\" in /memory");
	vlen = prom_virt_avail_len();
	if (vlen == 0)
		panic("no \"available\" in /virtual-memory");
	if (plen + vlen > max_bootlist_sz)
		panic("ran out of prom_memlist space");

	pl = &bootmem_props[PHYSAVAIL];
	vl = &bootmem_props[VIRTAVAIL];

	/*
	 * re-adjust ptrs if needed
	 */
	if (plen > pl->maxsize) {
		/* move virt avail up */
		move = plen - pl->maxsize;
		pl->maxsize = plen;
		vl->ptr += move / sizeof (struct prom_memlist);
		vl->maxsize -= move;
	} else if (vlen > vl->maxsize) {
		/* move virt avail down */
		move = vlen - vl->maxsize;
		vl->maxsize = vlen;
		vl->ptr -= move / sizeof (struct prom_memlist);
		pl->maxsize -= move;
	}

	pl->nelems = plen / sizeof (struct prom_memlist);
	vl->nelems = vlen / sizeof (struct prom_memlist);

	/* now we can retrieve the properties */
	(void) prom_phys_avail((caddr_t)pl->ptr);
	(void) prom_virt_avail((caddr_t)vl->ptr);

	/* .. and sort them */
	sort_promlist(pl);
	sort_promlist(vl);

	il = &bootmem_props[PHYSINSTALLED];
	*physinstalled = il->ptr;
	*physinstalled_len = il->nelems;

	*physavail = pl->ptr;
	*physavail_len = pl->nelems;

	*virtavail = vl->ptr;
	*virtavail_len = vl->nelems;
}


/*
 * Find the page number of the highest installed physical
 * page and the number of pages installed (one cannot be
 * calculated from the other because memory isn't necessarily
 * contiguous).
 */
void
installed_top_size(
	struct memlist *list,	/* pointer to start of installed list */
	pfn_t *topp,		/* return ptr for top value */
	pgcnt_t *sumpagesp)	/* return prt for sum of installed pages */
{
	pfn_t top = 0;
	pfn_t highp;		/* high page in a chunk */
	pgcnt_t sumpages = 0;

	for (; list; list = list->ml_next) {
		highp = (list->ml_address + list->ml_size - 1) >> PAGESHIFT;
		if (top < highp)
			top = highp;
		sumpages += (uint_t)(list->ml_size >> PAGESHIFT);
	}

	*topp = top;
	*sumpagesp = sumpages;
}
