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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * PX mmu initialization and configuration
 */
#include <sys/types.h>
#include <sys/kmem.h>
#include <sys/async.h>
#include <sys/sysmacros.h>
#include <sys/sunddi.h>
#include <sys/ddi_impldefs.h>
#include <sys/vmem.h>
#include <sys/machsystm.h>	/* lddphys() */
#include <sys/iommutsb.h>
#include "px_obj.h"

int
px_mmu_attach(px_t *px_p)
{
	dev_info_t		*dip = px_p->px_dip;
	px_mmu_t		*mmu_p;
	uint32_t		tsb_i = 0;
	char			name[32];
	px_dvma_range_prop_t	*dvma_prop;
	int			dvma_prop_len;
	uint64_t		dvma_len;
	uint32_t		tsb_entries;

	px_dvma_addr_t		 mapc_base;
	uint64_t		 mapc_size;
	uint64_t		slice_size;
	uint64_t		slice_size_align;
	void			*addr;

	/*
	 * Allocate mmu state structure and link it to the
	 * px state structure.
	 */
	mmu_p = kmem_zalloc(sizeof (px_mmu_t), KM_SLEEP);
	if (mmu_p == NULL)
		return (DDI_FAILURE);

	px_p->px_mmu_p = mmu_p;
	mmu_p->mmu_px_p = px_p;
	mmu_p->mmu_inst = ddi_get_instance(dip);

	/*
	 * Check for "virtual-dma" property that specifies
	 * the DVMA range.
	 */
	if (ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "virtual-dma", (caddr_t)&dvma_prop, &dvma_prop_len) !=
	    DDI_PROP_SUCCESS) {

		DBG(DBG_ATTACH, dip, "Getting virtual-dma failed\n");

		kmem_free(mmu_p, sizeof (px_mmu_t));
		px_p->px_mmu_p = NULL;

		return (DDI_FAILURE);
	}

	/*
	 * MUST be px_dvma_top_cache_quantum aligned
	 */
	mmu_p->mmu_dvma_base =
	    P2ROUNDUP((uint64_t)dvma_prop->dvma_base,
	    px_dvma_top_cache_quantum);
	mmu_p->mmu_dvma_end =
	    P2ALIGN((uint64_t)(mmu_p->mmu_dvma_base + dvma_prop->dvma_len),
	    px_dvma_top_cache_quantum) - 1;

	/*
	 * Some devices have issues DMAing to the first and last pages, so
	 * if our virtual DMA address range include the first or last page,
	 * adjust it so it doesn't.
	 */
	if (px_dvma_space_shrink && mmu_p->mmu_dvma_end == -1u) {
		mmu_p->mmu_dvma_end = -1u - px_dvma_top_cache_quantum;
	}

	dvma_len = mmu_p->mmu_dvma_end - mmu_p->mmu_dvma_base + 1;
	tsb_entries = MMU_BTOP(dvma_len);

	kmem_free(dvma_prop, dvma_prop_len);

	/*
	 * Setup base and bounds for DVMA and bypass mappings.
	 */
	mmu_p->dvma_base_pg = MMU_BTOP(mmu_p->mmu_dvma_base);
	mmu_p->dvma_end_pg = MMU_BTOP(mmu_p->mmu_dvma_end);

	/*
	 * Use the minimum slice size to align current slice size. Since
	 * px_dvma_mapc_scale is one byte and the maximum is 255, the
	 * denominator should be 256.
	 */
	slice_size_align = P2ROUNDUP(dvma_len, GIGABYTE) / 256;
	slice_size_align = P2ROUNDUP(slice_size_align,
	    px_dvma_top_cache_quantum);
	slice_size = P2ROUNDUP(dvma_len / px_dvma_mapc_scale, slice_size_align);
	mapc_base = mmu_p->mmu_dvma_base + px_dvma_mapc_slice * slice_size;
	mapc_size = (px_dvma_mapc_slice + 1 == px_dvma_mapc_scale) ?
	    (mmu_p->mmu_dvma_end - mapc_base + 1) : slice_size;

	/*
	 * Create two virtual memory maps for DVMA address space.  The top map
	 * 'mmu_dvma_map' covers all DVMA space. Then a new map 'mmu_dvma_mapc'
	 * for constrained allocations only is reserved from mmu_dvma_map, and
	 * use remain of mmu_dvma_map for unconstrained allocations.
	 */
	(void) snprintf(name, sizeof (name), "%s%d_dvma_vmem_1m",
	    ddi_driver_name(dip), ddi_get_instance(dip));

	mmu_p->mmu_dvma_map = vmem_create(name, (void *)mmu_p->mmu_dvma_base,
	    MMU_PTOB(tsb_entries), px_dvma_top_cache_quantum, NULL, NULL,
	    NULL, 0, VM_SLEEP | VMC_IDENTIFIER);

	/*
	 * Reserved for constrained allocations
	 */
	(void) snprintf(name, sizeof (name), "%s%d_dvma_vmem_8k",
	    ddi_driver_name(dip), ddi_get_instance(dip));

	/*
	 * For constrained only, so no quantum cache needed
	 */
	mmu_p->mmu_dvma_mapc = vmem_create(name, (void *)mapc_base,
	    mapc_size, MMU_PAGESIZE, NULL, NULL, NULL, 0,
	    VM_SLEEP | VMC_IDENTIFIER);

	/*
	 * Mark pages ranged in mmu_dvma_mapc as occupied
	 */
	addr = vmem_xalloc(mmu_p->mmu_dvma_map, mapc_size,
	    px_dvma_top_cache_quantum, 0, 0, (void *)mapc_base,
	    (void *)(mapc_base + mapc_size),
	    VM_NOSLEEP | VM_BESTFIT | VM_PANIC);
	ASSERT((uint64_t)addr == mapc_base);


	(void) snprintf(name, sizeof (name), "%s%d_dvma_vmem_16",
	    ddi_driver_name(dip), ddi_get_instance(dip));

	mmu_p->mmu_dvma_vmem_16 = vmem_create(name, NULL, 0,
	    px_dvma_cache_quantum, vmem_alloc, vmem_free, mmu_p->mmu_dvma_map,
	    0, VM_SLEEP | VMC_IDENTIFIER);

	mutex_init(&mmu_p->dvma_debug_lock, NULL, MUTEX_DRIVER, NULL);

	/*
	 * Mark pages mapped by OBP as occupied
	 */
	for (tsb_i = 0; tsb_i < tsb_entries; tsb_i++) {
		r_addr_t ra = 0;
		io_attributes_t attr;
		caddr_t va;

		if (px_lib_iommu_getmap(px_p->px_dip, PCI_TSBID(0, tsb_i),
		    &attr, &ra) != DDI_SUCCESS)
			continue;

		va = (caddr_t)(MMU_PTOB(mmu_p->dvma_base_pg + tsb_i));

		if (va >= (caddr_t)mapc_base &&
		    va <= (caddr_t)(mapc_base + mapc_size - 1)) {
			(void) vmem_xalloc(mmu_p->mmu_dvma_mapc, MMU_PAGE_SIZE,
			    MMU_PAGE_SIZE, 0, 0, va, va + MMU_PAGE_SIZE,
			    VM_NOSLEEP | VM_BESTFIT | VM_PANIC);
		} else {
			va = (caddr_t)P2ALIGN((int64_t)va,
			    px_dvma_top_cache_quantum);
			(void) vmem_xalloc(mmu_p->mmu_dvma_map,
			    px_dvma_top_cache_quantum, 0, 0, 0, va,
			    va + px_dvma_top_cache_quantum,
			    VM_NOSLEEP | VM_BESTFIT | VM_ABORT);
		}
	}

	/*
	 * mmu_dvma_map breakdown
	 */
	(void) snprintf(name, sizeof (name), "%s%d_dvma_cache1",
	    ddi_driver_name(dip), ddi_get_instance(dip));

	mmu_p->mmu_dvma_cache_1 = kmem_cache_create(name, MMU_PAGE_SIZE,
	    MMU_PAGE_SIZE, NULL, NULL, NULL, NULL, mmu_p->mmu_dvma_map,
	    KMC_NOTOUCH);

	(void) snprintf(name, sizeof (name), "%s%d_dvma_cache2",
	    ddi_driver_name(dip), ddi_get_instance(dip));

	mmu_p->mmu_dvma_cache_2 = kmem_cache_create(name,
	    MMU_PAGE_SIZE * 2, MMU_PAGE_SIZE *2, NULL, NULL, NULL, NULL,
	    mmu_p->mmu_dvma_map, KMC_NOTOUCH);

	(void) snprintf(name, sizeof (name), "%s%d_dvma_cache8",
	    ddi_driver_name(dip), ddi_get_instance(dip));

	mmu_p->mmu_dvma_cache_8 = kmem_cache_create(name,
	    MMU_PAGE_SIZE * 8, MMU_PAGE_SIZE * 8, NULL, NULL, NULL, NULL,
	    mmu_p->mmu_dvma_map, KMC_NOTOUCH);

	return (DDI_SUCCESS);
}

void
px_mmu_detach(px_t *px_p)
{
	px_mmu_t *mmu_p = px_p->px_mmu_p;

	(void) px_lib_iommu_detach(px_p);

	/*
	 * Free the dvma caches and resource map.
	 */
	kmem_cache_destroy(mmu_p->mmu_dvma_cache_1);
	kmem_cache_destroy(mmu_p->mmu_dvma_cache_2);
	kmem_cache_destroy(mmu_p->mmu_dvma_cache_8);
	vmem_destroy(mmu_p->mmu_dvma_vmem_16);
	vmem_destroy(mmu_p->mmu_dvma_mapc);
	vmem_destroy(mmu_p->mmu_dvma_map);

	if (PX_DVMA_DBG_ON(mmu_p))
		px_dvma_debug_fini(mmu_p);

	mutex_destroy(&mmu_p->dvma_debug_lock);

	/*
	 * Free the mmu state structure.
	 */
	kmem_free(mmu_p, sizeof (px_mmu_t));
	px_p->px_mmu_p = NULL;
}

int
px_mmu_map_pages(px_mmu_t *mmu_p, ddi_dma_impl_t *mp, px_dvma_addr_t dvma_pg,
    size_t npages, size_t pfn_index)
{
	dev_info_t	*dip = mmu_p->mmu_px_p->px_dip;
	px_dvma_addr_t	pg_index = MMU_PAGE_INDEX(mmu_p, dvma_pg);
	io_attributes_t	attr = PX_GET_MP_TTE(mp->dmai_tte);

	ASSERT(npages <= mp->dmai_ndvmapages);
	DBG(DBG_MAP_WIN, dip, "px_mmu_map_pages:%x+%x=%x "
	    "npages=0x%x pfn_index=0x%x\n", (uint_t)mmu_p->dvma_base_pg,
	    (uint_t)pg_index, dvma_pg, (uint_t)npages, (uint_t)pfn_index);

	if (px_lib_iommu_map(dip, PCI_TSBID(0, pg_index), npages,
	    PX_ADD_ATTR_EXTNS(attr, mp->dmai_bdf), (void *)mp, pfn_index,
	    MMU_MAP_PFN) != DDI_SUCCESS) {
		DBG(DBG_MAP_WIN, dip, "px_mmu_map_pages: "
		    "px_lib_iommu_map failed\n");

		return (DDI_FAILURE);
	}

	if (!PX_MAP_BUFZONE(mp))
		goto done;

	DBG(DBG_MAP_WIN, dip, "px_mmu_map_pages: redzone pg=%x\n",
	    pg_index + npages);

	ASSERT(PX_HAS_REDZONE(mp));

	if (px_lib_iommu_map(dip, PCI_TSBID(0, pg_index + npages), 1,
	    PX_ADD_ATTR_EXTNS(attr, mp->dmai_bdf), (void *)mp,
	    pfn_index + npages - 1, MMU_MAP_PFN) != DDI_SUCCESS) {
		DBG(DBG_MAP_WIN, dip, "px_mmu_map_pages: mapping "
		    "REDZONE page failed\n");

		if (px_lib_iommu_demap(dip, PCI_TSBID(0, pg_index), npages)
		    != DDI_SUCCESS) {
			DBG(DBG_MAP_WIN, dip, "px_lib_iommu_demap: failed\n");
		}
		return (DDI_FAILURE);
	}

done:
	if (PX_DVMA_DBG_ON(mmu_p))
		px_dvma_alloc_debug(mmu_p, (char *)mp->dmai_mapping,
		    mp->dmai_size, mp);

	return (DDI_SUCCESS);
}

void
px_mmu_unmap_pages(px_mmu_t *mmu_p, ddi_dma_impl_t *mp, px_dvma_addr_t dvma_pg,
    uint_t npages)
{
	px_dvma_addr_t	pg_index = MMU_PAGE_INDEX(mmu_p, dvma_pg);

	DBG(DBG_UNMAP_WIN, mmu_p->mmu_px_p->px_dip,
	    "px_mmu_unmap_pages:%x+%x=%x npages=0x%x\n",
	    (uint_t)mmu_p->dvma_base_pg, (uint_t)pg_index, dvma_pg,
	    (uint_t)npages);

	if (px_lib_iommu_demap(mmu_p->mmu_px_p->px_dip,
	    PCI_TSBID(0, pg_index), npages) != DDI_SUCCESS) {
		DBG(DBG_UNMAP_WIN, mmu_p->mmu_px_p->px_dip,
		    "px_lib_iommu_demap: failed\n");
	}

	if (!PX_MAP_BUFZONE(mp))
		return;

	DBG(DBG_UNMAP_WIN, mmu_p->mmu_px_p->px_dip, "px_mmu_unmap_pages: "
	    "redzone pg=%x\n", pg_index + npages);

	ASSERT(PX_HAS_REDZONE(mp));

	if (px_lib_iommu_demap(mmu_p->mmu_px_p->px_dip,
	    PCI_TSBID(0, pg_index + npages), 1) != DDI_SUCCESS) {
		DBG(DBG_UNMAP_WIN, mmu_p->mmu_px_p->px_dip,
		    "px_lib_iommu_demap: failed\n");
	}
}

/*
 * px_mmu_map_window - map a dvma window into the mmu
 * used by: px_dma_win(), px_dma_ctlops() - DDI_DMA_MOVWIN, DDI_DMA_NEXTWIN
 * return value: none
 */
/*ARGSUSED*/
int
px_mmu_map_window(px_mmu_t *mmu_p, ddi_dma_impl_t *mp, px_window_t win_no)
{
	uint32_t obj_pg0_off = mp->dmai_roffset;
	uint32_t win_pg0_off = win_no ? 0 : obj_pg0_off;
	size_t win_size = mp->dmai_winsize;
	size_t pfn_index = win_size * win_no;			/* temp value */
	size_t obj_off = win_no ? pfn_index - obj_pg0_off : 0;	/* xferred sz */
	px_dvma_addr_t dvma_pg = MMU_BTOP(mp->dmai_mapping);
	size_t res_size = mp->dmai_object.dmao_size - obj_off + win_pg0_off;
	int ret = DDI_SUCCESS;

	ASSERT(!(win_size & MMU_PAGE_OFFSET));
	if (win_no >= mp->dmai_nwin)
		return (ret);
	if (res_size < win_size)		/* last window */
		win_size = res_size;		/* mp->dmai_winsize unchanged */

	mp->dmai_mapping = MMU_PTOB(dvma_pg) | win_pg0_off;
	mp->dmai_size = win_size - win_pg0_off;	/* cur win xferrable size */
	mp->dmai_offset = obj_off;		/* win offset into object */
	pfn_index = MMU_BTOP(pfn_index);	/* index into pfnlist */
	ret = px_mmu_map_pages(mmu_p, mp, dvma_pg, MMU_BTOPR(win_size),
	    pfn_index);

	return (ret);
}

/*
 * px_mmu_unmap_window
 * This routine is called to break down the mmu mappings to a dvma window.
 * Non partial mappings are viewed as single window mapping.
 * used by: px_dma_unbindhdl(), px_dma_window(),
 *	and px_dma_ctlops() - DDI_DMA_FREE, DDI_DMA_MOVWIN, DDI_DMA_NEXTWIN
 * return value: none
 */
/*ARGSUSED*/
void
px_mmu_unmap_window(px_mmu_t *mmu_p, ddi_dma_impl_t *mp)
{
	px_dvma_addr_t dvma_pg = MMU_BTOP(mp->dmai_mapping);
	uint_t npages = MMU_BTOP(mp->dmai_winsize);

	px_mmu_unmap_pages(mmu_p, mp, dvma_pg, npages);

	if (PX_DVMA_DBG_ON(mmu_p))
		px_dvma_free_debug(mmu_p, (char *)mp->dmai_mapping,
		    mp->dmai_size, mp);
}


#if 0
/*
 * The following table is for reference only. It denotes the
 * the TSB table size measured in number of 8 byte entries.
 * It is represented by bits 3:0 in the MMU TSB CTRL REG.
 */
static int px_mmu_tsb_sizes[] = {
	0x0,		/* 1K */
	0x1,		/* 2K */
	0x2,		/* 4K */
	0x3,		/* 8K */
	0x4,		/* 16K */
	0x5,		/* 32K */
	0x6,		/* 64K */
	0x7,		/* 128K */
	0x8		/* 256K */
};
#endif

static char *px_mmu_errsts[] = {
	"Protection Error", "Invalid Error", "Timeout", "ECC Error(UE)"
};

/*ARGSUSED*/
static int
px_log_mmu_err(px_t *px_p)
{
	/*
	 * Place holder, the correct eror bits need tobe logged.
	 */
	return (0);
}
