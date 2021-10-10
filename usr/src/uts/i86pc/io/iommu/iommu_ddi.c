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
#include <sys/ndifm.h>

extern void io_parse_dmarobj(ddi_dma_obj_t *dmar_object, uint_t iovcnt,
    struct as **asp, caddr_t *vaddr, uint32_t *size, page_t ***pplist,
    page_t **pp, uint64_t *offset);
extern int io_get_page_cnt(ddi_dma_obj_t *dmar_object, offset_t offset,
    size_t size);

static void iommu_dma_split_cookie(iommu_hdl_priv_t *ihp, uint_t size,
    uint64_t addr, uint_t *ccountp);
static void iommu_dma_cleanhdl(iommu_hdl_priv_t *ihp);

static int iommu_dma_check(dev_info_t *dip, const void *handle,
    const void *addr, const void *not_used);

static void iommu_dma_initwin(iommu_hdl_priv_t *ihp, int *nwinp,
    uint_t *ccountp);

int
iommu_dma_allochdl(dev_info_t *rdip, ddi_dma_attr_t *attr,
    int (*waitfp)(caddr_t), caddr_t arg, ddi_dma_handle_t *dma_handlep)
{
	int kfl;
	iommu_hdl_priv_t *ihp;
	ddi_dma_impl_t *hp;
	iommu_t *iommu;
	uint64_t minaddr, maxaddr;
	iommu_domain_t *domain;
	iommu_dmap_t *idm;
	uint_t presize;

	iommu = IOMMU_DEVI(rdip)->id_iommu;
	presize = IOMMU_DEVI(rdip)->id_presize;

	kfl = (waitfp == DDI_DMA_SLEEP ? KM_SLEEP : KM_NOSLEEP);
	ihp = kmem_cache_alloc(iommu->iommu_hdl_cache, kfl);
	if (ihp == NULL) {
		if (waitfp != DDI_DMA_DONTWAIT)
			ddi_set_callback(waitfp, arg, &iommu->iommu_dma_cbid);
		return (DDI_DMA_NORESOURCES);
	}

	hp = &ihp->ihp_impl;

	ihp->ihp_prealloc = NULL;
	ihp->ihp_ncalloc = IOMMU_PRECOOKIES;
	ihp->ihp_cookies = ihp->ihp_precookies;
	ihp->ihp_canfast = B_FALSE;

	iommu_dma_cleanhdl(ihp);

	hp->dmai_error.err_fep = NULL;
	hp->dmai_minxfer = attr->dma_attr_minxfer;
	hp->dmai_burstsizes = attr->dma_attr_burstsizes;
	hp->dmai_rdip = rdip;
	hp->dmai_attr = *attr;
	/*
	 * Set the FM error function just once. iommu_dma_check
	 * will correctly deal with an unused handle by returning
	 * DDI_FM_UNKNOWN.
	 */
	if (attr->dma_attr_flags & DDI_DMA_FLAGERR)
		hp->dmai_error.err_cf = iommu_dma_check;
	hp->dmai_rflags = DMP_NOSYNC;

	minaddr = attr->dma_attr_addr_lo;
	if (attr->dma_attr_flags & _DDI_DMA_BOUNCE_ON_SEG) {
		maxaddr = attr->dma_attr_seg + 1;
		ihp->ihp_boundary = 0;
	} else {
		maxaddr = attr->dma_attr_addr_hi + 1;
		ihp->ihp_boundary = attr->dma_attr_seg + 1;
	}

	/* handle the rollover cases */
	if (maxaddr < attr->dma_attr_addr_hi)
		maxaddr = attr->dma_attr_addr_hi;

	ihp->ihp_minaddr = (void *)(uintptr_t)minaddr;
	ihp->ihp_maxaddr = (void *)(uintptr_t)maxaddr;
	ihp->ihp_align = MAX(attr->dma_attr_align, MMU_PAGESIZE);

	ihp->ihp_maxcsize = hp->dmai_attr.dma_attr_count_max > UINT_MAX ?
	    UINT_MAX : (uint32_t)hp->dmai_attr.dma_attr_count_max;
	ihp->ihp_maxcsize = (uint32_t)
	    MIN((uint64_t)ihp->ihp_maxcsize, attr->dma_attr_maxxfer);
	ihp->ihp_maxcsize -= ihp->ihp_maxcsize % attr->dma_attr_granular;

	hp->dmai_attr.dma_attr_maxxfer -=
	    hp->dmai_attr.dma_attr_maxxfer % attr->dma_attr_granular;


	/*
	 * If the DMA attributes for this handle have no limits,
	 * and using premapped space is not disabled for this
	 * device, preallocate and map some space that will
	 * be used if the requested bind size fits.
	 *
	 * A device can use preallocated DVMA space if its min/maxaddr
	 * span the entire 64 bits, and if it has a dma_attr_seg that
	 * aligns with presize, so that the preallocated chunks will
	 * never cross it. This catches most modern network/storage
	 * devices. Modern network devices usually have no limits,
	 * storage devices may have a 4G boundary limit.
	 *
	 * Also, for this optimization to be enabled, the domain will
	 * not have to be a shared one (true for all PCIEX devices).
	 * In that case, rdip will be equal to domain->dom_dip.
	 *
	 * The size and alignment of this space will be such that
	 * the PTEs remain within one PTP. So this optimization
	 * saves having to to a vmem_alloc for each bind, and
	 * also makes mapping/unmapping very straightforward.
	 */

	if (minaddr == 0 && maxaddr == ~0ULL &&
	    ihp->ihp_align <= MMU_PAGESIZE) {
		domain = IOMMU_DEVI(rdip)->id_domain;

		if (ihp->ihp_boundary == 0) {
			hp->dmai_rflags |= DMP_NOLIMIT;
			if (!(iommu->iommu_flags &
			    (IOMMU_FLAGS_STRICTFLUSH | IOMMU_FLAGS_WRITEBACK)))
				ihp->ihp_canfast = B_TRUE;
		}

		if (domain->dom_domtype == IOMMU_DOMTYPE_EXCL &&
		    ihp->ihp_boundary % presize == 0) {
			idm = kmem_cache_alloc(domain->dom_pre_cache, kfl);
			if (idm == NULL)
				goto out;
			idm->idm_npages = 0;
			ihp->ihp_prealloc = idm;
		}
	}
out:
	*dma_handlep = (ddi_dma_handle_t)ihp;

	ndi_fmc_insert(rdip, DMA_HANDLE, *dma_handlep, NULL);

	return (DDI_SUCCESS);
}

int
iommu_dma_freehdl(dev_info_t *rdip, ddi_dma_handle_t dma_handle)
{
	iommu_hdl_priv_t *ihp;
	iommu_t *iommu;

	ndi_fmc_remove(rdip, DMA_HANDLE, dma_handle);

	ihp = (iommu_hdl_priv_t *)dma_handle;

	if (ihp->ihp_prealloc != NULL)
		kmem_cache_free(IOMMU_DEVI(rdip)->id_domain->dom_pre_cache,
		    ihp->ihp_prealloc);

	if (ihp->ihp_ncalloc > IOMMU_PRECOOKIES) {
		kmem_free(ihp->ihp_cookies,
		    ihp->ihp_ncalloc * sizeof (ddi_dma_cookie_t));
	}

	iommu = IOMMU_DEVI(rdip)->id_iommu;
	kmem_cache_free(iommu->iommu_hdl_cache, ihp);
	if (iommu->iommu_dma_cbid != 0)
		ddi_run_callback(&iommu->iommu_dma_cbid);

	return (DDI_SUCCESS);
}

int
iommu_dma_bindhdl(dev_info_t *rdip, ddi_dma_handle_t dma_handle,
    struct ddi_dma_req *dmareq, ddi_dma_cookie_t *cookiep, uint_t *ccountp)
{
	iommu_hdl_priv_t *ihp;
	ddi_dma_impl_t *hp;
	iommu_domain_t *domain;
	iommu_t *iommu;
	ddi_dma_atyp_t buftype;
	ddi_dma_obj_t *dmar_object;
	uint64_t offset, paddr, dvma, sdvma, rwmask, pgspace;
	int64_t unmap_time, *timep, newgen;
	uint_t nent, psize, size, ccount, npgalloc;
	page_t **pparray;
	caddr_t vaddr;
	page_t *page;
	struct as *vas;
	int flushdone, kfl, iovcnt, boundsplit, needsplit, ret;
	uint64_t *ptep;
	iommu_dmap_t *idm;
	ddi_dma_cookie_t *cp;

	ihp = (iommu_hdl_priv_t *)dma_handle;
	hp = &ihp->ihp_impl;

	if (ihp->ihp_sdvma != 0)
		return (DDI_DMA_INUSE);

	hp->dmai_rflags |= (dmareq->dmar_flags & DMP_DDIFLAGS);

	domain = IOMMU_DEVI(rdip)->id_domain;
	iommu = domain->dom_iommu;

	if (dmareq->dmar_fp == DDI_DMA_SLEEP)
		kfl = KM_SLEEP;
	else
		kfl = KM_NOSLEEP;

#ifdef IOMMU_BUGGY_DRIVERS
	rwmask = iommu_ops->ioo_rmask | iommu_ops->ioo_wmask |
	    iommu_ops->ioo_ptemask[0];
#else
	rwmask = iommu_ops->ioo_ptemask[0];
	if (dmareq->dmar_flags & DDI_DMA_READ)
		rwmask |= iommu_ops->ioo_rmask;

	if (dmareq->dmar_flags & DDI_DMA_WRITE)
		rwmask |= iommu_ops->ioo_wmask;
#endif

	dmar_object = &dmareq->dmar_object;
	buftype = dmar_object->dmao_type;

	if (buftype == DMA_OTYP_MVECTOR) {
		io_parse_dmarobj(dmar_object, 0, &vas, &vaddr, &size,
		    &pparray, &page, &offset);
		npgalloc = io_get_page_cnt(dmar_object, 0, 0);
		pgspace = npgalloc << MMU_PAGESHIFT;
		ccount = dmar_object->dmao_obj.mvector->im_iovec_cnt;
	} else {
		size = dmar_object->dmao_size;
		vaddr = dmar_object->dmao_obj.virt_obj.v_addr;
		pparray = dmar_object->dmao_obj.virt_obj.v_priv;
		if (buftype == DMA_OTYP_PAGES) {
			page = dmar_object->dmao_obj.pp_obj.pp_pp;
			offset =  dmar_object->dmao_obj.pp_obj.pp_offset &
			    MMU_PAGEOFFSET;
			vas = NULL;
		} else {
			page = NULL;
			vas = dmar_object->dmao_obj.virt_obj.v_as;
			if (vas == NULL) {
				vas = &kas;
			}
			offset = (uintptr_t)vaddr & MMU_PAGEOFFSET;
		}
		npgalloc = mmu_btopr(size + offset);
		pgspace = npgalloc << MMU_PAGESHIFT;
		ccount = 1;
	}

	/*
	 * Account for the case where the requested size
	 * will cross the segment boundary. In that rare case,
	 * do not use a boundary when allocating (see below),
	 * and split the cookie as needed below.
	 */
	needsplit = boundsplit =
	    (ihp->ihp_boundary != 0 && pgspace > ihp->ihp_boundary);
	if (boundsplit)
		ccount += pgspace / ihp->ihp_boundary + 1;

	if (dmar_object->dmao_size > ihp->ihp_maxcsize) {
		needsplit = 1;
		ccount += dmar_object->dmao_size / ihp->ihp_maxcsize + 1;
	}

	idm = ihp->ihp_prealloc;
	if (idm != NULL && npgalloc <= IOMMU_DEVI(rdip)->id_npreptes) {
		/*
		 * Fastpath the common case (networking) of mapping
		 * a virtual address to just one PTE, with no TLB
		 * flush being needed.
		 */
		if (buftype == DMA_OTYP_VADDR && ihp->ihp_canfast &&
		    npgalloc == 1 &&
		    domain->dom_flush_gen > idm->idm_unmap_time) {
			idm->idm_npages = *ccountp = 1;
			ihp->ihp_sdvma = idm->idm_dvma;
			cookiep->dmac_laddress = idm->idm_dvma + offset;
			cookiep->dmac_size = size;
			cookiep->dmac_type = 0;
			*idm->idm_ptes = mmu_ptob(
			    (paddr_t)hat_getpfnum(vas->a_hat, vaddr)) | rwmask;
			return (DDI_DMA_MAPPED);
		}
		sdvma = idm->idm_dvma;
		idm->idm_npages = npgalloc;
		ptep = idm->idm_ptes;
		unmap_time = idm->idm_unmap_time;
		timep = NULL;
	} else {
		if (idm != NULL)
			idm->idm_npages = 0;
		if (hp->dmai_rflags & DMP_NOLIMIT)
			sdvma = (uint64_t)(uintptr_t)vmem_alloc(
			    domain->dom_arena, pgspace, kfl);
		else
			sdvma = (uint64_t)(uintptr_t)vmem_xalloc(
			    domain->dom_arena,
			    pgspace, ihp->ihp_align, 0,
			    boundsplit ? 0 : ihp->ihp_boundary,
			    ihp->ihp_minaddr, ihp->ihp_maxaddr, kfl);
		if (sdvma == 0)
			goto nores;

		nent = iommu_pgtable_get_ptes(domain, sdvma, npgalloc, kfl,
		    &timep, &ptep);
		unmap_time = 0;
		if (nent == npgalloc) {
			ihp->ihp_timep = timep;
			ihp->ihp_ptep = ptep;
		} else {
			ihp->ihp_timep = NULL;
			ihp->ihp_ptep = NULL;
		}
	}

	/*
	 * Make sure there is space for the cookies, allocate if
	 * necessary. Should almost never be needed for normal
	 * use.
	 */
	if (ccount > ihp->ihp_ncalloc) {
		if (ihp->ihp_ncalloc > IOMMU_PRECOOKIES) {
			kmem_free(ihp->ihp_cookies,
			    ihp->ihp_ncalloc *
			    sizeof (ddi_dma_cookie_t));
		}

		ihp->ihp_cookies = kmem_alloc(
		    ccount * sizeof (ddi_dma_cookie_t), kfl);
		if (ihp->ihp_cookies == NULL)
			goto nores;

		ihp->ihp_ncalloc = ccount;
	}

	ihp->ihp_sdvma = sdvma;
	ihp->ihp_npages = npgalloc;

	flushdone = iovcnt = 0;
	dvma = sdvma;
	ccount = 0;

mapdvma_mvector:

	if (needsplit) {
		iommu_dma_split_cookie(ihp, size, dvma + offset, &ccount);
	} else {
		ihp->ihp_cookies[ccount].dmac_laddress = dvma + offset;
		ihp->ihp_cookies[ccount].dmac_size = size;
		ihp->ihp_cookies[ccount].dmac_type = 0;
		ccount++;
	}

mapdvma_contigvec:

	psize = MIN((MMU_PAGESIZE - offset), size);

	IOMMU_DPROBE3(iommu__map__dvma, dev_info_t *, rdip, ddi_dma_atyp_t,
	    buftype, uint_t, size);

	while (size > 0) {
		/* get the size for this page (i.e. partial or full page) */
		if (page != NULL) {
			/* get the paddr from the page_t */
			paddr = mmu_ptob((paddr_t)page->p_pagenum);
			page = page->p_next;
		} else if (pparray != NULL) {
			/* index into the array of page_t's to get the paddr */
			paddr = mmu_ptob((paddr_t)((*pparray)->p_pagenum));
			pparray++;
		} else {
			/* call into the VM to get the paddr */
			paddr = mmu_ptob(
			    (paddr_t)hat_getpfnum(vas->a_hat, vaddr));
			vaddr += psize;
		}

		*ptep = paddr | rwmask;
		dvma += MMU_PAGESIZE;

		/*
		 * timep is NULL if we're using a preallocated
		 * range of DVMA space. In that case, there is
		 * no need for timestamps, and the PTE array
		 * is contiguous.
		 */
		if (timep == NULL) {
			ptep++;
		} else {
			if (*timep > unmap_time)
				unmap_time = *timep;
			if (--nent == 0) {
				/*
				 * It's OK to pass npgalloc unmodified to
				 * this call, it only serves as an
				 * upper boundary in the number of contig
				 * entries returned. In this case, we're
				 * already bound by the loop, and nent can
				 * just be the maximum number of PTEs left
				 * in the page.
				 */
				nent = iommu_pgtable_get_ptes(domain, dvma,
				    npgalloc, kfl, &timep, &ptep);
			} else {
				ptep++;
				timep++;
			}
		}
		size -= psize;
		psize = MIN(size, MMU_PAGESIZE);
	}

	/*
	 * If this is an mvector, and we have more io_vectors left, go to
	 * the next cookie, and then process the next io_vector.
	 */
	if (buftype == DMA_OTYP_MVECTOR &&
	    ++iovcnt < dmar_object->dmao_obj.mvector->im_iovec_cnt) {
		io_parse_dmarobj(dmar_object, iovcnt, &vas, &vaddr, &size,
		    &pparray, &page, &offset);
		/*
		 * If the new offset is 0, and the previous mvector had
		 * a page aligned ending, don't start a new cookie;
		 * just update the size of the current one.
		 */
		cp = &ihp->ihp_cookies[ccount - 1];
		if (!needsplit && offset == 0 &&
		    cp->dmac_laddress + cp->dmac_size == dvma) {
			cp->dmac_size += size;
			goto mapdvma_contigvec;
		}
		goto mapdvma_mvector;
	}

	ihp->ihp_ccount = ccount;

	if (ccount > 1 && (ccount > hp->dmai_attr.dma_attr_sgllen ||
	    dmar_object->dmao_size > hp->dmai_attr.dma_attr_maxxfer)) {
		if (!(hp->dmai_rflags & DDI_DMA_PARTIAL)) {
			(void) iommu_dma_unbindhdl(rdip, dma_handle);
			return (DDI_DMA_NORESOURCES);
		}
		iommu_dma_initwin(ihp, &hp->dmai_nwin, &ccount);
		ret = DDI_DMA_PARTIAL_MAP;
	} else {
		ihp->ihp_curwin = 0;
		hp->dmai_nwin = 1;
		ret = DDI_DMA_MAPPED;
	}

	*cookiep = *ihp->ihp_cookies;
	*ccountp = ccount;
	hp->dmai_cookie = ihp->ihp_cookies;
	hp->dmai_cookie++;

	if (iommu->iommu_flags & IOMMU_FLAGS_STRICTFLUSH) {
		iommu_ops->ioo_flush_pages(iommu, domain->dom_did, sdvma,
		    npgalloc, &ihp->ihp_inv_wait);
	} else {
		/*
		 * For each domain-wide flush done here, the flush generation
		 * number is incremented. Each time a DVMA range is unmapped,
		 * the flush generation number is recorded. When it is
		 * re-mapped, that value is used to check if there have been
		 * any flushes since then. If so, no additional flush is
		 * needed.
		 *
		 * A race condition occurs when a thread needs to record
		 * the flush generation number, and a flush is in progress,
		 * but may or may not be finished. To avoid getting the
		 * wrong number in this case, we do the following:
		 * 1) Set bit 63 in the flush generation number
		 *    before starting the flush (side effect: that makes
		 *    the if() below come true for that case as well).
		 * 2) Unset bit 63 when the flush is done
		 * 3) If a thread finds that bit 63 is set, the generation
		 *    number it records will be generated by clearing
		 *    bit 63, and then adding 1, essentially assuming that
		 *    the flush has already been done, and that a new one
		 *    will be needed to take care of the current mapping.
		 *    This means that there is a small chance of unnecessary
		 *    flushes, but that is naturally better than having a small
		 *    chance of using stale mappings.
		 */
		if (domain->dom_flush_gen <= unmap_time) {
			mutex_enter(&domain->dom_flush_lock);
			if (domain->dom_flush_gen <= unmap_time) {
				newgen = domain->dom_flush_gen + 1;
				domain->dom_flush_gen |=
				    ((uint64_t)1 << 63);
				iommu_ops->ioo_flush_domain(iommu,
				    domain->dom_did, &ihp->ihp_inv_wait);
				domain->dom_flush_gen = newgen;
				flushdone = 1;
			}
			mutex_exit(&domain->dom_flush_lock);
		}

		if (!flushdone)
			iommu_ops->ioo_flush_buffers(iommu);
	}

	return (ret);

nores:
	iommu_dma_cleanhdl(ihp);
	if (dmareq->dmar_fp != DDI_DMA_DONTWAIT)
		ddi_set_callback(dmareq->dmar_fp, dmareq->dmar_arg,
		    &iommu->iommu_dma_cbid);

	return (DDI_DMA_NORESOURCES);
}


int
iommu_dma_unbindhdl(dev_info_t *rdip, ddi_dma_handle_t dma_handle)
{
	iommu_hdl_priv_t *ihp = (iommu_hdl_priv_t *)dma_handle;
	iommu_dmap_t *idm;
	iommu_t *iommu;
	int64_t flushgen, *timep;
	uint64_t sdvma;
	iommu_domain_t *domain;
	uint_t nent, npages, i;
	uint64_t *ptep;

	/*
	 * Check if the handle wasn't bound.
	 */
	if (ihp->ihp_sdvma == 0)
		return (DDI_FAILURE);

	domain = IOMMU_DEVI(rdip)->id_domain;
	iommu = IOMMU_DEVI(rdip)->id_iommu;

	idm = ihp->ihp_prealloc;
	if (idm != NULL && idm->idm_npages > 0) {
		/*
		 * The prealloc case is simple. Just zero out
		 * the preallocated PTEs, and record the flush
		 * generation number at unmap time.
		 */
		for (i = 0; i < idm->idm_npages; i++)
			idm->idm_ptes[i] = 0;
		IOMMU_GET_FLUSHGEN(domain, flushgen);
		idm->idm_unmap_time = flushgen;
		idm->idm_npages = 0;
	} else {
		/*
		 * The dyn alloc case is a little more complicated.
		 * Two possibilities: if the PTEs for the allocated
		 * space were contiguous (in one PTP), zero them
		 * out and record the flush generation number for
		 * each PTE.
		 *
		 * Else, walk through the contiguous ranges of
		 * PTEs, doing the same.
		 */
		if (ihp->ihp_timep != NULL) {
			timep = ihp->ihp_timep;
			ptep = ihp->ihp_ptep;
			for (i = 0; i < ihp->ihp_npages; i++)
				*ptep++ = 0;
			IOMMU_GET_FLUSHGEN(domain, flushgen);
			for (i = 0; i < ihp->ihp_npages; i++)
				*timep++ = flushgen;
		} else {
			sdvma = ihp->ihp_sdvma;
			npages = ihp->ihp_npages;
			while (npages > 0) {
				nent = iommu_pgtable_lookup_ptes(domain, sdvma,
				    npages, &timep, &ptep);
				ASSERT(nent > 0);
				for (i = 0; i < nent; i++)
					*ptep++ = 0;
				IOMMU_GET_FLUSHGEN(domain, flushgen);
				for (i = 0; i < nent; i++)
					*timep++ = flushgen;
				npages -= nent;
				sdvma += nent * MMU_PAGESIZE;
			}
		}

		if (ihp->ihp_impl.dmai_rflags & DMP_NOLIMIT)
			vmem_free(domain->dom_arena,
			    (void *)(uintptr_t)ihp->ihp_sdvma,
			    ihp->ihp_npages << MMU_PAGESHIFT);
		else
			vmem_xfree(domain->dom_arena,
			    (void *)(uintptr_t)ihp->ihp_sdvma,
			    ihp->ihp_npages << MMU_PAGESHIFT);

		IOMMU_DPROBE3(iommu__dvma__free, dev_info_t *, rdip, uint_t,
		    ihp->ihp_npages, uint64_t, ihp->ihp_sdvma);
	}

	iommu_dma_cleanhdl(ihp);

	if (iommu->iommu_dma_cbid != 0)
		ddi_run_callback(&iommu->iommu_dma_cbid);

	return (DDI_SUCCESS);
}

/*
 * Helper function for window handling. Assume that the next window
 * is pointed to by ihp_nextwinp. Find the next window by walking
 * cookies until either the maximum S/G length is reached, or
 * the maximum transfer limit is hit. Use the new next window
 * start to compute the offset and lengh requested by the caller.
 */
static int
iommu_dma_nextwin(iommu_hdl_priv_t *ihp, off_t *offp, size_t *lenp,
    uint_t *ccountp, ddi_dma_cookie_t *firstcookie)
{
	ddi_dma_cookie_t *cookiep, *ecookiep;
	ddi_dma_attr_t *attrp;
	ddi_dma_impl_t *hp;
	uint64_t xfer, size, edvma;
	uint_t ccount, extra;

	hp = &ihp->ihp_impl;
	attrp = &hp->dmai_attr;

	cookiep = ihp->ihp_nextwinp;
	ecookiep = &ihp->ihp_cookies[ihp->ihp_ccount - 1];

	xfer = 0;
	ccount = 0;

	if (offp)
		*offp = cookiep->dmac_laddress -
		    ihp->ihp_cookies->dmac_laddress;

	/*
	 * If a cookie is not a multiple of the granularity specified in
	 * the DMA attributes, it was split in iommu_dma_split_cookie
	 * because it crossed the boundary specified in dma_attr_seg.
	 * In this case, the next cookie will contain an amount of data
	 * that brings the total transfer size back to a multiple of the
	 * granularity. Therefore, these 2 cookies must be in the same window,
	 * otherwise one or more windows will not have a total size which is
	 * a multiple of the granularity, which is required.
	 *
	 * To get these cookies in the same window, count them together.
	 */
	while (cookiep <= ecookiep) {
		if (cookiep != ecookiep &&
		    cookiep->dmac_size % attrp->dma_attr_granular != 0) {
			extra = 1;
			size = cookiep->dmac_size + (cookiep + 1)->dmac_size;
		} else {
			extra = 0;
			size = cookiep->dmac_size;
		}

		if (xfer + size > attrp->dma_attr_maxxfer ||
		    (ccount + extra) >= attrp->dma_attr_sgllen)
			break;

		xfer += size;
		ccount += 1 + extra;
		cookiep += 1 + extra;
	}

	if (ccount != 0) {
		if (ccountp)
			*ccountp = ccount;
		if (lenp) {
			if (cookiep > ecookiep)
				edvma = ecookiep->dmac_laddress +
				    ecookiep->dmac_size;
			else
				edvma = cookiep->dmac_laddress;

			*lenp = edvma -
			    ihp->ihp_nextwinp->dmac_laddress;
		}
		if (firstcookie)
			*firstcookie = *ihp->ihp_nextwinp;

		hp->dmai_cookie = ihp->ihp_nextwinp;
		hp->dmai_cookie++;

		ihp->ihp_curwin++;
		ihp->ihp_nextwinp = cookiep;
		return (DDI_SUCCESS);
	}

	return (DDI_FAILURE);
}

/*
 * Initialize DMA window meta data. Only called (from iommu_dma_bindhdl)
 * if DMA windows are actually needed.
 */
static void
iommu_dma_initwin(iommu_hdl_priv_t *ihp, int *nwinp, uint_t *ccountp)
{
	uint_t ccount, extra;
	int nwin;
	uint64_t xfer, size;
	ddi_dma_cookie_t *cookiep, *ecookiep;
	ddi_dma_attr_t *attrp;

	ccount = 0;
	nwin = 1;
	xfer = 0;
	ihp->ihp_curwin = 0;
	attrp = &ihp->ihp_impl.dmai_attr;

	cookiep = &ihp->ihp_cookies[0];
	ecookiep = &ihp->ihp_cookies[ihp->ihp_ccount - 1];

	/*
	 * See the comment above in iommu_dma_nextwin for an explanation
	 * of the 'extra' handling.
	 */
	while (cookiep <= ecookiep) {
		if (cookiep != ecookiep &&
		    cookiep->dmac_size % attrp->dma_attr_granular != 0) {
			extra = 1;
			size = cookiep->dmac_size + (cookiep + 1)->dmac_size;
		} else {
			extra = 0;
			size = cookiep->dmac_size;
		}

		if (xfer + size > attrp->dma_attr_maxxfer ||
		    (ccount + extra) >= attrp->dma_attr_sgllen) {
			if (nwin++ == 1) {
				*ccountp = ccount;
				ihp->ihp_nextwinp = cookiep;
			}
			ccount = 0;
			xfer = 0;
		}

		ccount += 1 + extra;
		xfer += size;
		cookiep += 1 + extra;
	}

	*nwinp = nwin;
}

/*
 * The only reason that this can be called is the rare occasion that
 * the number of cookies was larger than the maximum S/G length in the DMA
 * attributes, or if the total DMA object size exceeded the maximum
 * transfer size specified in the DMA attributes.
 *
 * All cookies are already mapped, it's just a matter of setting the
 * right pointers.
 *
 * offp and lenp are really only used with ddi_dma_sync, and thus not
 * used in our case, but compute them anyway, to adhere to the specified
 * interface.
 */

/*ARGSUSED*/
int
iommu_dma_win(dev_info_t *rdip, ddi_dma_handle_t handle, uint_t win,
    off_t *offp, size_t *lenp, ddi_dma_cookie_t *cookiep, uint_t *ccountp)
{
	iommu_hdl_priv_t *ihp;
	ddi_dma_impl_t *hp;

	ihp = (iommu_hdl_priv_t *)handle;
	hp = &ihp->ihp_impl;

	if (win >= hp->dmai_nwin)
		return (DDI_FAILURE);

	/*
	 * The normal case is that the next window is requested.
	 */
	if (win == ihp->ihp_curwin + 1)
		return (iommu_dma_nextwin(ihp, offp, lenp, ccountp, cookiep));

	/*
	 * For all other requests, start counting at the beginning. Not
	 * optimal, but these requests should be rare, or for window 0.
	 */
	ihp->ihp_nextwinp = ihp->ihp_cookies;
	ihp->ihp_curwin = -1;
	while (iommu_dma_nextwin(ihp, offp, lenp, ccountp, cookiep) ==
	    DDI_SUCCESS && ihp->ihp_curwin != win)
		;

	return (ihp->ihp_curwin == win ? DDI_SUCCESS : DDI_FAILURE);
}

int
iommu_dmap_ctor(void *buf, void *arg, int kfl)
{
	iommu_domain_t *domain;
	iommu_dmap_t *idm;
	iommu_t *iommu;
	uint64_t maxaddr, stamp;
	iommu_xlate_t xlate[IOMMU_PGTABLE_MAXLEVELS + 1] = {0}, *xlp;
	uint_t presize, npreptes, i;

	domain = arg;
	iommu = domain->dom_iommu;
	idm = buf;

	ASSERT(domain->dom_domtype == IOMMU_DOMTYPE_EXCL);

	presize = IOMMU_DEVI(domain->dom_dip)->id_presize;
	npreptes = IOMMU_DEVI(domain->dom_dip)->id_npreptes;

	maxaddr = 1ULL << iommu->iommu_dom_width;
	idm->idm_dvma = (uintptr_t)vmem_xalloc(domain->dom_arena,
	    presize, presize, 0, 0, (void *)(uintptr_t)MMU_PAGESIZE,
	    (void *)(uintptr_t)maxaddr, kfl);

	if (idm->idm_dvma == 0)
		return (-1);

	iommu_pgtable_xlate_setup(idm->idm_dvma, xlate,
	    iommu->iommu_pgtable_nlevels);
	iommu_pgtable_alloc_pdps(iommu, domain, xlate, kfl);

	xlp = &xlate[1];

	/*
	 * The PTE space is within one PTP.  Initialize it, and set
	 * the unmap time (really flush generation number) to the
	 * minimum value, indicating that this space does not need to
	 * be flushed from the TLB; that comes after first use.
	 */
	idm->idm_ptes = xlp->xlt_pgtable->pgt_vaddr + xlp->xlt_idx;
	idm->idm_timep = xlp->xlt_pgtable->pgt_timestamps + xlp->xlt_idx;

	idm->idm_unmap_time = 0;

	/*
	 * This page table page may have been used before, so
	 * initialize the unmap time (generation number)
	 * from the timestamps. If this is the first time
	 * these PTEs are used, the timestamps will be 0.
	 */
	for (i = 0; i < npreptes; i++) {
		stamp = idm->idm_timep[i];
		if (stamp > idm->idm_unmap_time)
			idm->idm_unmap_time = stamp;
		idm->idm_ptes[i] = 0;
	}

	return (0);
}

void
iommu_dmap_dtor(void *buf, void *arg)
{
	iommu_domain_t *domain;
	iommu_dmap_t *idm;
	uint_t npreptes, i;

	domain = arg;
	idm = buf;

	npreptes = IOMMU_DEVI(domain->dom_dip)->id_npreptes;

	/*
	 * Set the timestamps to the unmap time of this dmap,
	 * so that flushing is handled correctly on re-use of
	 * the PTEs.
	 */
	for (i = 0; i < npreptes; i++) {
		idm->idm_timep[i] = idm->idm_unmap_time;
		idm->idm_ptes[i] = 0;
	}

	vmem_xfree(domain->dom_arena, (void *)(uintptr_t)idm->idm_dvma,
	    IOMMU_DEVI(domain->dom_dip)->id_presize);
}

/*
 * Split a cookie in chunks that do not cross the segment boundary
 * (originally from the DMA attributes) and that do not exceed the
 * maximum cookie size.  Should almost never be needed, virtually all
 * cases should result in one cookie (or one cookie per vector in the
 * case of an mvector object).
 */
static void
iommu_dma_split_cookie(iommu_hdl_priv_t *ihp, uint_t size, uint64_t addr,
    uint_t *ccountp)
{
	uint64_t nextbound, csize;
	uint32_t left, gran, granmod;
	ddi_dma_cookie_t *cookiep;

	/*
	 * The initial boundary to check for is found by rounding up
	 * the starting address plus one to a multiple of the boundary.
	 */
	if (ihp->ihp_boundary != 0)
		nextbound = ((addr + ihp->ihp_boundary) / ihp->ihp_boundary) *
		    ihp->ihp_boundary;
	else
		nextbound = 0xffffffffffffffffULL;

	gran = ihp->ihp_impl.dmai_attr.dma_attr_granular;
	left = size;
	granmod = 0;

	cookiep = &ihp->ihp_cookies[*ccountp];

	while (left) {
		/*
		 * If the previous cookie was not a multiple of the
		 * granularity, add an extra cookie with the amount
		 * required to get the transfer size back to being a
		 * multiple of the granularity. A boundary crossing
		 * can be the only reason for this, since maxxfer has
		 * already been rounded down (if needed).
		 */
		if (granmod) {
			csize = gran - granmod;
			granmod = 0;
		} else {
			csize = nextbound - addr;
			csize = MIN(csize, (uint64_t)left);
			csize = MIN(csize, (uint64_t)ihp->ihp_maxcsize);
			if (csize != left && csize % gran != 0)
				granmod = csize % gran;
			else
				granmod = 0;
		}

		cookiep->dmac_laddress = addr;
		cookiep->dmac_size = csize;
		cookiep->dmac_type = 0;

		addr += csize;
		left -= csize;
		(*ccountp)++;
		cookiep++;

		/*
		 * If a boundary was hit, set up the next one.
		 */
		if (addr == nextbound)
			nextbound += ihp->ihp_boundary;
	}
}

static void
iommu_dma_cleanhdl(iommu_hdl_priv_t *ihp)
{
	ddi_dma_impl_t *hp;

	hp = &ihp->ihp_impl;

	hp->dmai_fault = 0;
	hp->dmai_fault_check = NULL;
	hp->dmai_fault_notify = NULL;
	hp->dmai_error.err_ena = 0;
	hp->dmai_error.err_status = DDI_FM_OK;
	hp->dmai_error.err_expected = DDI_FM_ERR_UNEXPECTED;
	hp->dmai_error.err_ontrap = NULL;

	hp->dmai_rflags &= (DMP_NOSYNC | DMP_NOLIMIT);

	ihp->ihp_sdvma = 0;
}

/*
 * Check if a faulting address belongs to a handle. The faulting address
 * will be page-aligned.
 */
/*ARGSUSED*/
static int
iommu_dma_check(dev_info_t *dip, const void *handle, const void *addr,
    const void *not_used)
{
	iommu_hdl_priv_t *ihp;
	uint64_t fault_addr, start, end;
	iommu_dmap_t *idm;
	int i;

	ihp = (iommu_hdl_priv_t *)handle;
	fault_addr = *(uint64_t *)addr;

	if (ihp->ihp_sdvma == 0)
		return (DDI_FM_UNKNOWN);

	idm = ihp->ihp_prealloc;
	if (idm != NULL && idm->idm_npages != 0) {
		start = idm->idm_dvma & MMU_PAGEMASK;
		end = start + idm->idm_npages * MMU_PAGESIZE;
		if (fault_addr >= start && fault_addr < end)
			return (DDI_FM_NONFATAL);
	} else {
		for (i = 0; i < ihp->ihp_ncalloc; i++) {
			start = ihp->ihp_cookies[i].dmac_laddress;
			end = start + ihp->ihp_cookies[i].dmac_size;
			start &= ~MMU_PAGEOFFSET;
			end &= ~MMU_PAGEOFFSET;
			if (fault_addr >= start && fault_addr < end)
				return (DDI_FM_NONFATAL);
		}
	}

	return (DDI_FM_UNKNOWN);
}
