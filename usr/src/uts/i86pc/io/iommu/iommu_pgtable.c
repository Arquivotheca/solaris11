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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * Copyright (c) 2009, Intel Corporation.
 * All rights reserved.
 */

#include <sys/sunddi.h>
#include <sys/sunndi.h>
#include <sys/errno.h>
#include <sys/modctl.h>
#include <sys/iommu.h>
#include <sys/sysmacros.h>

/*ARGSUSED*/
int
iommu_pgtable_ctor(void *buf, void *arg, int kmflag)
{
	iommu_pgtable_t *pgtable;
	iommu_page_t ipage;
	void *next;
	iommu_t *iommu = arg;
	int64_t *stamps;
	int ret, i;

	ret = -1;
	pgtable = (iommu_pgtable_t *)buf;
	ipage.ip_vaddr = next = stamps = NULL;

	next = kmem_zalloc(MMU_PAGESIZE, kmflag);
	if (next == NULL)
		goto error;

	stamps = kmem_alloc(MMU_PAGESIZE, kmflag);
	if (stamps == NULL)
		goto error;

	if (iommulib_page_alloc(iommu, kmflag, &ipage) < 0)
		goto error;

	pgtable->pgt_paddr = ipage.ip_paddr;
	pgtable->pgt_vaddr = (uint64_t *)ipage.ip_vaddr;
	pgtable->pgt_memhdl = ipage.ip_memhdl;

	pgtable->pgt_next_array = next;
	pgtable->pgt_timestamps = stamps;

	rw_init(&pgtable->pgt_rwlock, NULL, RW_DEFAULT, NULL);

	for (i = 0; i < (MMU_PAGESIZE / sizeof (uint64_t)); i++)
		stamps[i] = 0;

	ret = 0;
error:
	if (ret < 0) {
		if (next != NULL)
			kmem_free(next, MMU_PAGESIZE);
		if (stamps != NULL)
			kmem_free(stamps, MMU_PAGESIZE);
		if (ipage.ip_vaddr != NULL)
			iommulib_page_free(&ipage);
	}

	return (0);
}

/*ARGSUSED*/
void
iommu_pgtable_dtor(void *buf, void *arg)
{
	iommu_pgtable_t *pgtable;

	pgtable = (iommu_pgtable_t *)buf;

	/* destroy will panic if lock is held. */
	rw_destroy(&pgtable->pgt_rwlock);

	ddi_dma_mem_free(&pgtable->pgt_memhdl);
	kmem_free(pgtable->pgt_next_array, MMU_PAGESIZE);
	kmem_free(pgtable->pgt_timestamps, MMU_PAGESIZE);
}

iommu_pgtable_t *
iommu_pgtable_alloc(iommu_t *iommu, int kmflags)
{
	iommu_pgtable_t *pgtable;

	pgtable = kmem_cache_alloc(iommu->iommu_pgtable_cache, kmflags);

	return (pgtable);
}

void
iommu_pgtable_free(iommu_t *iommu, iommu_pgtable_t *pgtable)
{
	(void) memset(pgtable->pgt_next_array, 0, MMU_PAGESIZE);
	(void) memset(pgtable->pgt_timestamps, 0, MMU_PAGESIZE);
	(void) memset(pgtable->pgt_vaddr, 0, MMU_PAGESIZE);

	kmem_cache_free(iommu->iommu_pgtable_cache, pgtable);
}

void
iommu_pgtable_teardown(iommu_t *iommu, iommu_pgtable_t *pgtable)
{
	int i;

	for (i = 0; i < (MMU_PAGESIZE / sizeof (uint64_t)); i++) {
		if (pgtable->pgt_next_array[i] != NULL)
			iommu_pgtable_teardown(iommu,
			    pgtable->pgt_next_array[i]);
	}

	iommu_pgtable_free(iommu, pgtable);
}

void
iommu_pgtable_xlate_setup(uint64_t dvma, iommu_xlate_t *xlate, int nlevels)
{
	int level;
	uint64_t offbits;

	/*
	 * Skip the first 12 bits which is the offset into
	 * 4K PFN (phys page frame based on MMU_PAGESIZE)
	 */
	offbits = dvma >> MMU_PAGESHIFT;

	/* skip to level 1 i.e. leaf PTE */
	for (level = 1, xlate++; level <= nlevels; level++, xlate++) {
		xlate->xlt_level = level;
		xlate->xlt_idx = (offbits & IOMMU_PGTABLE_LEVEL_MASK);
		xlate->xlt_pgtable = NULL;
		offbits >>= IOMMU_PGTABLE_LEVEL_STRIDE;
	}
}

/*
 * Read the pgtables
 */
boolean_t
iommu_pgtable_lookup_pdps(iommu_domain_t *domain, iommu_xlate_t *xlate,
    int nlevels)
{
	iommu_pgtable_t *pgtable;
	iommu_pgtable_t *next;
	uint_t idx;

	/* start with highest level pgtable i.e. root */
	xlate += nlevels;

	if (xlate->xlt_pgtable == NULL) {
		xlate->xlt_pgtable = domain->dom_pgtable_root;
	}

	for (; xlate->xlt_level > 1; xlate--) {
		idx = xlate->xlt_idx;
		pgtable = xlate->xlt_pgtable;

		if ((xlate - 1)->xlt_pgtable) {
			continue;
		}

		/*
		 * This function is only used to look up mappings
		 * that already exist. Therefore, there is no
		 * need for locking. If a mapping gets yanked
		 * from underneath us, that's a bug.
		 */
		next = pgtable->pgt_next_array[idx];
		(xlate - 1)->xlt_pgtable = next;
		if (next == NULL)
			return (B_FALSE);
	}

	return (B_TRUE);
}

/*
 * Alloc any PDPs needed for a mapping, that has been set up in to
 * the xlate array.
 */
void
iommu_pgtable_alloc_pdps(iommu_t *iommu, iommu_domain_t *domain,
    iommu_xlate_t *xlate, int kfl)
{
	iommu_pgtable_t *pgtable;
	iommu_pgtable_t *new;
	iommu_pgtable_t *next;
	uint64_t *hwp;
	int level;
	uint_t idx;
	krw_t rwtype;

	/* start with highest level pgtable i.e. root */
	xlate += iommu->iommu_pgtable_nlevels;

	new = NULL;
	xlate->xlt_pgtable = domain->dom_pgtable_root;
	for (level = iommu->iommu_pgtable_nlevels; level > 1;
	    level--, xlate--) {
		idx = xlate->xlt_idx;
		pgtable = xlate->xlt_pgtable;

		/* Lock the pgtable in READ mode first */
		rw_enter(&pgtable->pgt_rwlock, RW_READER);
		rwtype = RW_READER;
again:
		hwp = pgtable->pgt_vaddr + idx;
		next = (pgtable->pgt_next_array)[idx];

		/*
		 * check if leafier level already has a pgtable
		 * if yes, verify
		 */
		if (next == NULL) {
			if (new == NULL) {
				new = iommu_pgtable_alloc(iommu, kfl);
				if (new == NULL) {
					ddi_err(DER_PANIC, NULL,
					    "pgtable alloc err");
				}
			}

			/* Change to a write lock */
			if (rwtype == RW_READER &&
			    rw_tryupgrade(&pgtable->pgt_rwlock) == 0) {
				rw_exit(&pgtable->pgt_rwlock);
				rw_enter(&pgtable->pgt_rwlock, RW_WRITER);
				rwtype = RW_WRITER;
				goto again;
			}
			rwtype = RW_WRITER;
			next = new;
			(pgtable->pgt_next_array)[idx] = next;
			new = NULL;
			*hwp = next->pgt_paddr |
			    iommu_ops->ioo_rmask | iommu_ops->ioo_wmask |
			    iommu_ops->ioo_ptemask[level - 1];
			rw_downgrade(&pgtable->pgt_rwlock);
			rwtype = RW_READER;
		}

		(xlate - 1)->xlt_pgtable = next;
		rw_exit(&pgtable->pgt_rwlock);
	}

	if (new != NULL)
		iommu_pgtable_free(iommu, new);
}

/*
 * Create a 1:1 mapping.
 * The dvma argument doubles as the DVMA address and the physical address.
 */
void
iommu_pgtable_map_unity(iommu_domain_t *domain, uint64_t dvma,
    uint64_t npages, int kfl)
{
	uint_t n;
	uint64_t *pte;

	while (npages > 0) {
		n = iommu_pgtable_get_ptes(domain, dvma, npages, kfl, NULL,
		    &pte);
		npages -= n;

		while (n--) {
			*pte++ = dvma |
			    iommu_ops->ioo_rmask | iommu_ops->ioo_wmask;
			dvma += MMU_PAGESIZE;
		}
	}
}

/*
 * Return the first contiguous stretch of PTEs for a mapping, allocating
 * PDPs as needed. Most mappings will only need to call this once,
 * but if PTP boundary is crossed (2M boundary), it will have to be
 * called again to get the next stretch.
 *
 * Returns the number of contiguous PTEs.
 */
int
iommu_pgtable_get_ptes(iommu_domain_t *domain, uint64_t sdvma, uint_t snvpages,
    int kfl, int64_t **timep, uint64_t **ptep)
{
	iommu_t *iommu = domain->dom_iommu;
	int nlevels = iommu->iommu_pgtable_nlevels, idx;
	iommu_xlate_t xlate[IOMMU_PGTABLE_MAXLEVELS + 1] = {0}, *xlatep;
	iommu_pgtable_t *pgtable;
	uint_t n;

	iommu_pgtable_xlate_setup(sdvma, xlate, nlevels);

	iommu_pgtable_alloc_pdps(iommu, domain, xlate, kfl);

	xlatep = &xlate[1];
	pgtable = xlatep->xlt_pgtable;
	idx = xlatep->xlt_idx;

	if (timep)
		*timep = &pgtable->pgt_timestamps[idx];
	*ptep = pgtable->pgt_vaddr + idx;

	n = IOMMU_PGTABLE_MAXIDX - idx + 1;
	n = MIN(n, snvpages);

	return (n);
}

/*
 * As above, but only look up the PTEs, as the needed PDPs were already
 * allocated. Used for unmapping.
 */
int
iommu_pgtable_lookup_ptes(iommu_domain_t *domain, uint64_t sdvma,
    uint_t snvpages, int64_t **timep, uint64_t **ptep)
{
	iommu_t *iommu = domain->dom_iommu;
	int nlevels = iommu->iommu_pgtable_nlevels, idx;
	iommu_xlate_t xlate[IOMMU_PGTABLE_MAXLEVELS + 1] = {0}, *xlatep;
	iommu_pgtable_t *pgtable;
	uint_t n;

	iommu_pgtable_xlate_setup(sdvma, xlate, nlevels);

	if (!iommu_pgtable_lookup_pdps(domain, xlate, nlevels))
		return (0);

	xlatep = &xlate[1];
	pgtable = xlatep->xlt_pgtable;
	idx = xlatep->xlt_idx;

	if (timep)
		*timep = &pgtable->pgt_timestamps[idx];
	*ptep = pgtable->pgt_vaddr + idx;

	n = IOMMU_PGTABLE_MAXIDX - idx + 1;
	n = MIN(n, snvpages);

	return (n);
}
