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
#include <sys/memlist.h>
#include <sys/sysmacros.h>
#include <sys/pci.h>
#include <sys/apic.h>

static int iommu_domain_merge_range(struct memlist **m, uint64_t low,
    uint64_t high);
static int iommu_domain_create_arena(dev_info_t *dip, iommu_t *iommu,
    iommu_domain_t *domain, struct memlist *unityp, struct memlist *resvp);
static void iommu_domain_map_unity(dev_info_t *dip,
    iommu_domain_t *dom, struct memlist *unityp, int kfl);

int
iommu_domain_mergelist(struct memlist **mlpp, struct memlist *mlp)
{
	while (mlp != NULL) {
		if (iommu_domain_merge_range(mlpp, mlp->ml_address,
		    mlp->ml_address + mlp->ml_size) < 0)
			return (-1);
		mlp = mlp->ml_next;
	}

	return (0);
}

void
iommu_domain_freelist(struct memlist *mlp)
{
	struct memlist *next;

	while (mlp != NULL) {
		next = mlp->ml_next;
		kmem_free(mlp, sizeof (struct memlist));
		mlp = next;
	}
}

static int
iommu_domain_merge_range(struct memlist **m, uint64_t low, uint64_t high)
{
	struct memlist *mmlp, *mlp, *next;
	uint64_t rstart, rend;

	mlp = *m;
	mmlp = NULL;

	low = low & MMU_PAGEMASK;
	high = (high + MMU_PAGEOFFSET) & MMU_PAGEMASK;

	/*
	 * See if this range can be merged with an existing entry.
	 */
	for (mlp = *m; mlp != NULL; mlp = mlp->ml_next) {
		rstart = mlp->ml_address;
		rend = mlp->ml_address + mlp->ml_size;

		if ((low < rstart && high < rstart) ||
		    (low > rend))
			continue;

		high = high > rend ? high : rend;
		low = low < rstart ? low : rstart;

		/*
		 * If this is the first entry that would cause overlap,
		 * record it. It will be used to store the merged values.
		 * Else, mark the entry as overlapping, so that it can
		 * be removed below.
		 */
		if (mmlp == NULL)
			mmlp = mlp;
		else
			mlp->ml_address = ~0ULL;
	}

	if (mmlp != NULL) {
		/*
		 * Store the merged values, and remove all entries that
		 * have become obsolete because of merging.
		 */
		mmlp->ml_address = low;
		mmlp->ml_size = high - low;
		for (mlp = *m; mlp != NULL; mlp = next) {
			next = mlp->ml_next;
			if (mlp->ml_address == ~0ULL) {
				if (mlp->ml_prev)
					mlp->ml_prev->ml_next = next;
				if (next)
					next->ml_prev = mlp->ml_prev;
				kmem_free(mlp, sizeof (*mlp));
			}
		}
	} else {
		/*
		 * Not merged. Allocate a new structure and insert it
		 * at the front. Order is not important.
		 */
		mlp = kmem_zalloc(sizeof (struct memlist), KM_SLEEP);
		mlp->ml_address = low;
		mlp->ml_size = high - low;
		if (*m == NULL) {
			*m = mlp;
		} else {
			(*m)->ml_prev = mlp;
			mlp->ml_next = *m;
			*m = mlp;
		}
	}

	return (0);
}

iommu_domain_t *
iommu_domain_alloc(dev_info_t *dip, iommu_t *iommu, int type,
    struct memlist *unityp, struct memlist *resvp)
{
	iommu_domain_t *dom;
	uint_t domid;
	char *cachename;
	size_t cnlen;

	cachename = NULL;
	dom = kmem_zalloc(sizeof (iommu_domain_t), KM_SLEEP);

	domid = (uintptr_t)vmem_alloc(iommu->iommu_domid_arena, 1, VM_NOSLEEP);
	if (domid == 0) {
		kmem_free(dom, sizeof (iommu_domain_t));
		return (NULL);
	}

	dom->dom_iommu = iommu;
	dom->dom_dip = dip;
	dom->dom_domtype = type;
	dom->dom_maptype = IOMMU_MAPTYPE_XLATE;
	dom->dom_ref = 1;
	dom->dom_did = domid;
	dom->dom_flush_gen = 1;

	mutex_init(&dom->dom_flush_lock, NULL, MUTEX_DEFAULT, NULL);

	dom->dom_pgtable_root = iommu_pgtable_alloc(iommu, KM_SLEEP);

	if (iommu_domain_create_arena(dip, iommu, dom, unityp, resvp) < 0)
		goto error;

	cachename = iommulib_alloc_name("iprecache",
	    ddi_get_instance(iommu->iommu_dip) * (IOMMU_MAX_DID + 1) + domid,
	    &cnlen);
	if (cachename == NULL)
		goto error;

	dom->dom_pre_cache = kmem_cache_create(cachename, sizeof (iommu_dmap_t),
	    0, iommu_dmap_ctor, iommu_dmap_dtor, NULL, dom, NULL, 0);
	kmem_free(cachename, cnlen);

	iommu_domain_map_unity(dip, dom, unityp, KM_SLEEP);

	/*
	 * Flush the IOTLB for the domain, to be certain it
	 * starts out with a clean slate.
	 */
	iommu_ops->ioo_flush_domain(iommu, domid, NULL);

	return (dom);

error:
	if (cachename != NULL)
		kmem_free(cachename, cnlen);
	if (dom->dom_pgtable_root != NULL)
		iommu_pgtable_free(iommu, dom->dom_pgtable_root);
	vmem_free(iommu->iommu_domid_arena, (void *)(uintptr_t)domid, 1);
	kmem_free(dom, sizeof (iommu_domain_t));
	return (NULL);
}

iommu_domain_t *
iommu_domain_dup(iommu_domain_t *domain)
{
	atomic_inc_uint(&domain->dom_ref);

	return (domain);
}

/*ARGSUSED*/
void
iommu_domain_free(dev_info_t *dip, iommu_domain_t *domain)
{
	if (atomic_dec_uint_nv(&domain->dom_ref) == 0) {
		/*
		 * At this point, we should be the only thread here.
		 * No need for locking below.
		 */
		kmem_cache_destroy(domain->dom_pre_cache);
		vmem_destroy(domain->dom_arena);
		iommu_pgtable_teardown(domain->dom_iommu,
		    domain->dom_pgtable_root);

		vmem_free(domain->dom_iommu->iommu_domid_arena,
		    (void *)(uintptr_t)domain->dom_did, 1);
		kmem_free(domain, sizeof (iommu_domain_t));
	}
}

/*
 * Create a DVMA arena spanning the full virtual space, excluding
 * PCI MMIO regions (for peer-to-peer traffic), and the APIC memory
 * range. The PCI MMIO regions to avoid are found by looking at the
 * "ranges" property for the topmost PCI bridge that the requesting
 * device is under.
 */
static int
iommu_domain_create_arena(dev_info_t *dip, iommu_t *iommu,
    iommu_domain_t *domain, struct memlist *unityp, struct memlist *resvp)
{
	uint64_t maxaddr;
	dev_info_t *pdip, *bridgedip;
	uint64_t rstart, rsize, rend;
	pci_ranges_t *ranges, *rp;
	int len, i, n, ret;
	uint_t type;
	struct memlist *mlp, *alloclist;

	ret = -1;

	(void) snprintf(domain->dom_arena_name,
	    sizeof (domain->dom_arena_name),
	    "%s%d_dom%d_va", ddi_driver_name(iommu->iommu_dip),
	    ddi_get_instance(iommu->iommu_dip), domain->dom_did);

	if (iommu->iommu_dom_width == 64)
		maxaddr = 0xfffffffffffff000;
	else
		maxaddr = ((uint64_t)1 << iommu->iommu_dom_width);

	/*
	 * First, find the topmost PCI bridge for the requesting device
	 * (e.g. the host-PCI root).
	 */
	bridgedip = NULL;
	pdip = dip;
	while ((pdip = ddi_get_parent(pdip)) != NULL) {
		if (strcmp(ddi_node_name(pdip), "pci") != 0 &&
		    strcmp(ddi_node_name(pdip), "pciex") != 0)
			continue;
		bridgedip = pdip;
	}

	if (bridgedip == NULL) {
		ddi_err(DER_WARN, dip, "top PCI bridge not found");
		return (-1);
	}

	/*
	 * Then, get the MMIO ranges to exclude by looking at the "ranges"
	 * property. This is derived from what the FMA code does to
	 * match an MMIO address with a PCI tree.
	 */
	if (ddi_getlongprop(DDI_DEV_T_ANY, bridgedip, DDI_PROP_DONTPASS,
	    "ranges", (caddr_t)&ranges, &len) != DDI_SUCCESS) {
		ddi_err(DER_WARN, bridgedip, "no ranges property");
		return (-1);
	}

	alloclist = NULL;

	/*
	 * Start off the reserved list with the same entries as the unity
	 * mapped list. Add the APIC range. Then add the PCI MMIO ranges
	 * to it.
	 */
	if (iommu_domain_mergelist(&alloclist, unityp) < 0)
		goto error;

	if (iommu_domain_mergelist(&alloclist, resvp) < 0)
		goto error;

	/*
	 * Always exclude page 0 and the IOAPIC range.
	 * Page 0 is excluded below by starting the vmem arena
	 * at MMU_PAGESIZE.
	 */
	if (iommu_domain_merge_range(&alloclist,
	    APIC_LOCAL_ADDR, APIC_LOCAL_ADDR + APIC_LOCAL_MEMLEN) < 0)
		goto error;

	n = len / sizeof (pci_ranges_t);

	for (i = 0; i < n; i++) {
		rp = &ranges[i];
		type = PCI_REG_ADDR_G(rp->child_high);
		if (type != PCI_REG_ADDR_G(PCI_ADDR_MEM32) &&
		    type != PCI_REG_ADDR_G(PCI_ADDR_MEM64))
			continue;

		rstart = ((uint64_t)rp->parent_high << 32) + rp->parent_low;
		rsize = ((uint64_t)rp->size_high << 32) + rp->size_low;
		rend = rstart + rsize - 1;
		if (iommu_domain_merge_range(&alloclist, rstart, rend) < 0)
			goto error;
	}

	domain->dom_arena = vmem_create(
	    domain->dom_arena_name,
	    (void *)(uintptr_t)MMU_PAGESIZE,	/* start addr */
	    maxaddr,			/* size */
	    MMU_PAGESIZE,		/* quantum */
	    NULL,			/* afunc */
	    NULL,			/* ffunc */
	    NULL,			/* source */
	    IOMMU_CACHED_SIZE_LARGE,	/* qcache_max */
	    VM_SLEEP);
	if (domain->dom_arena == NULL) {
		ddi_err(DER_WARN, dip, "full arena creation failed");
		goto error;
	}

	/*
	 * Now that the domain has been allocated, walk through the
	 * reserved areas list and allocate the exact ranges in it, so that
	 * they are not used by devices.
	 */
	for (mlp = alloclist; mlp != NULL; mlp = mlp->ml_next) {
		/*
		 * Page 0 has already been excluded, since the vmem
		 * arena starts at 0. Adjust accordingly.
		 */
		if (mlp->ml_address == 0) {
			mlp->ml_address += MMU_PAGESIZE;
			mlp->ml_size -= MMU_PAGESIZE;
			if (mlp->ml_size == 0)
				continue;
		}
		if (!vmem_xalloc(domain->dom_arena, mlp->ml_size,
		    MMU_PAGESIZE, 0, 0, (void *)(uintptr_t)mlp->ml_address,
		    (void *)(uintptr_t)(mlp->ml_address + mlp->ml_size),
		    VM_SLEEP | VM_BESTFIT))
			ddi_err(DER_WARN, dip, "failed to reserve %" PRIx64
			    " - %" PRIx64, mlp->ml_address,
			    mlp->ml_address + mlp->ml_size - 1);
	}

	ret = 0;
error:
	kmem_free(ranges, len);

	iommu_domain_freelist(alloclist);

	return (ret);
}

/*ARGSUSED*/
static void
iommu_domain_map_unity(dev_info_t *dip, iommu_domain_t *dom,
    struct memlist *unityp, int kfl)
{
	while (unityp != NULL) {
		iommu_pgtable_map_unity(dom, unityp->ml_address,
		    unityp->ml_size / MMU_PAGESIZE, kfl);
		unityp = unityp->ml_next;
	}
}

/*
 * Create the 1:1 pseudo-domain. If passthrough is not supported,
 * fill it with the available physical and BIOS memory.
 */
int
iommu_domain_create_unity(iommu_t *iommu)
{
	struct memlist *mp;
	uint64_t start, end;
	uint64_t npages;
	iommu_domain_t *domain;

	domain = kmem_zalloc(sizeof (iommu_domain_t), KM_SLEEP);

	domain->dom_did = IOMMU_UNITY_DID;
	domain->dom_maptype = IOMMU_MAPTYPE_UNITY;
	domain->dom_domtype = IOMMU_DOMTYPE_SHARED;
	domain->dom_iommu = iommu;
	domain->dom_dip = iommu->iommu_dip;

	iommu->iommu_unity_domain = domain;

	if (iommu->iommu_flags & IOMMU_FLAGS_PASSTHROUGH)
		return (0);

	domain->dom_pgtable_root = iommu_pgtable_alloc(iommu, KM_SLEEP);

	/*
	 * Unity domains are a mirror of the physical memory
	 * installed in the system.
	 */

#ifdef IOMMU_BUGGY_DRIVERS
	/*
	 * Dont skip page 0. Some broken HW/FW accesses it.
	 */
	iommu_pgtable_map_unity(domain, 0, 1, VM_SLEEP);
#endif

	memlist_read_lock();

	mp = phys_install;

	/*
	 * Map all physical memory, BIOS regions, and DMAR RMRR
	 * regions. The latter should be a part of the BIOS list,
	 * but some BIOSes leave them out. Include them explicitly
	 * to be sure.
	 *
	 * Some of these may overlap, but that's ok, the worst case
	 * is that some PTEs are written more than once, which is
	 * no big deal.
	 */

	npages = mp->ml_size / MMU_PAGESIZE;
	if (mp->ml_address == 0) {
		/* since we already mapped page 0 above */
		start = MMU_PAGESIZE;
		npages--;
	} else {
		start = mp->ml_address;
	}

	iommu_pgtable_map_unity(domain, start, npages, VM_SLEEP);

	mp = mp->ml_next;
	while (mp) {
		start = mp->ml_address;
		npages = mp->ml_size / MMU_PAGESIZE;

		iommu_pgtable_map_unity(domain, start, npages, VM_SLEEP);
		mp = mp->ml_next;
	}

	memlist_read_unlock();

	mp = bios_rsvd;
	while (mp) {
		start = mp->ml_address & MMU_PAGEMASK;
		end = (mp->ml_address + mp->ml_size + MMU_PAGEOFFSET)
		    & MMU_PAGEMASK;
		npages = (end - start) / MMU_PAGESIZE;

		iommu_pgtable_map_unity(domain, start, npages, VM_SLEEP);

		mp = mp->ml_next;
	}

	iommu_ops->ioo_iommu_reserved(iommu, &mp);
	while (mp) {
		start = mp->ml_address & MMU_PAGEMASK;
		end = (mp->ml_address + mp->ml_size + MMU_PAGEOFFSET)
		    & MMU_PAGEMASK;
		npages = (end - start) / MMU_PAGESIZE;

		iommu_pgtable_map_unity(domain, start, npages, VM_SLEEP);

		mp = mp->ml_next;
	}

	iommu_ops->ioo_flush_domain(iommu, domain->dom_did, NULL);

	return (0);
}
