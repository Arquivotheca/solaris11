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

#include <sys/param.h>
#include <sys/sunddi.h>
#include <sys/sunndi.h>
#include <sys/sysmacros.h>
#include <sys/pci.h>
#include <sys/pcie_impl.h>
#include <sys/iommu.h>
#include <sys/modhash.h>

/*
 * Attributes used for page allocation. ddi_dma_mem_alloc only,
 * not used for any DMA operations.
 */
static ddi_dma_attr_t iommu_alloc_dma_attr = {
	DMA_ATTR_V0,
	0U,
	0xffffffffffffffffULL,
	0xffffffffU,
	0x1000,
	0x1,
	0x1,
	0xffffffffU,
	0xffffffffffffffffULL,
	1,
	4,
	0
};

static ddi_device_acc_attr_t iommu_alloc_acc_attr = {
	DDI_DEVICE_ATTR_V0,
	DDI_NEVERSWAP_ACC,
	DDI_STRICTORDER_ACC
};

list_t iommu_list;
kmutex_t iommu_list_lock;

static mod_hash_t *iommu_domain_hash;

static int
iommulib_pci_type(dev_info_t *dip)
{
	char *devtype;
	int type;

	type = IOMMU_PCI_UNKNOWN;

	if (ddi_prop_lookup_string(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "device_type", &devtype) == DDI_PROP_SUCCESS) {
		if (strcmp(devtype, "pci") == 0)
			type = IOMMU_PCI_PCI;
		else if (strcmp(devtype, "pciex") == 0)
			type = IOMMU_PCI_PCIEX;
		ddi_prop_free((void *)devtype);
	}

	return (type);
}

/*
 * Get basic information about a PCI device. Try to use properties
 * as much as possible, to avoid using PCI internals.
 */
int
iommulib_pci_base_info(dev_info_t *dip, iommu_pci_info_t *pinfo)
{
	uint_t len;
	pci_regspec_t *prp;
	int devid, vendorid, ret, classcode;

	prp = NULL;
	ret = -1;

	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "reg", (int **)&prp, &len) != DDI_PROP_SUCCESS)
		goto fail;

	vendorid = ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "vendor-id", -1);
	if (vendorid == -1)
		goto fail;

	devid = ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "device-id", -1);
	if (devid == -1)
		goto fail;

	classcode = ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "class-code", -1);
	if (classcode == -1)
		goto fail;

	ret = 0;

	pinfo->pi_seg = 0;
	pinfo->pi_bus = PCI_REG_BUS_G(prp->pci_phys_hi);
	pinfo->pi_init_bus = PCIE_GET_INITIAL_BUS(PCIE_DIP2BUS(dip));
	pinfo->pi_dev = PCI_REG_DEV_G(prp->pci_phys_hi);
	pinfo->pi_func = PCI_REG_FUNC_G(prp->pci_phys_hi);
	pinfo->pi_vendorid = vendorid;
	pinfo->pi_devid = devid;
	pinfo->pi_class = classcode >> 16;
	pinfo->pi_subclass = (classcode >> 8) & 0xff;
fail:
	if (prp != NULL)
		ddi_prop_free(prp);

	return (ret);
}

/*
 * In addition to the information retrieved by iommu_pci_base_info,
 * add some bridge information if applicable, and determine the
 * device type in a more specific way.
 */
int
iommulib_pci_info(dev_info_t *dip, iommu_pci_info_t *pinfo)
{
	uint_t len;
	pci_bus_range_t busrange;
	int ptype, pptype;
	dev_info_t *pdip;

	pinfo->pi_type = IOMMU_DEV_OTHER;

	if (iommulib_pci_base_info(dip, pinfo) < 0)
		return (-1);

	pdip = ddi_get_parent(dip);

	ptype = iommulib_pci_type(dip);
	pptype = iommulib_pci_type(pdip);

	if (pinfo->pi_class == PCI_CLASS_BRIDGE) {
		switch (pinfo->pi_subclass) {
		case PCI_BRIDGE_ISA:
			pinfo->pi_type = IOMMU_DEV_PCI_ISA;
			return (0);
		case PCI_BRIDGE_CARDBUS:
			pinfo->pi_type = IOMMU_DEV_PCI_PCI;
			pinfo->pi_secbus = pinfo->pi_bus;
			return (0);
		case PCI_BRIDGE_PCI:
			if (ptype == IOMMU_PCI_UNKNOWN)
				return (-1);

			if (pptype == IOMMU_PCI_UNKNOWN)
				return (-1);

			switch (ptype) {
			case IOMMU_PCI_PCI:
				switch (pptype) {
				case IOMMU_PCI_PCI:
					pinfo->pi_type = IOMMU_DEV_PCI_PCI;
					break;
				case IOMMU_PCI_PCIEX:
					pinfo->pi_type = IOMMU_DEV_PCIE_PCI;
					break;
				}
				break;
			case IOMMU_PCI_PCIEX:
				switch (pptype) {
				case IOMMU_PCI_PCI:
					pinfo->pi_type = IOMMU_DEV_PCI_PCIE;
					break;
				case IOMMU_PCI_PCIEX:
					pinfo->pi_type = IOMMU_DEV_PCIE_PCIE;
					break;
				}
				break;
			}
			break;
		default:
			pinfo->pi_type = IOMMU_DEV_PCI;
			break;
		}

		len = sizeof (pci_bus_range_t);
		if (ddi_getlongprop_buf(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
		    "bus-range", (caddr_t)&busrange, (int *)&len)
		    != DDI_PROP_SUCCESS)
			return (-1);

		pinfo->pi_secbus = busrange.lo;
		pinfo->pi_subbus = busrange.hi;
	} else {
		if (pdip == ddi_root_node()) {
			if (ptype == IOMMU_PCI_PCIEX)
				pinfo->pi_type = IOMMU_DEV_PCIE_ROOT;
			else
				pinfo->pi_type = IOMMU_DEV_PCI_ROOT;
		} else {
			if (pptype == IOMMU_PCI_PCIEX)
				pinfo->pi_type = IOMMU_DEV_PCIE;
			else
				pinfo->pi_type = IOMMU_DEV_PCI;
		}
	}

	return (0);
}

dev_info_t *
iommulib_top_pci_bridge(dev_info_t *dip, iommu_pci_info_t *pinfop)
{
	dev_info_t *pdip, *bdip;
	iommu_pci_info_t pinfo;

	bdip = NULL;

	ndi_hold_devi(dip);

	for (pdip = ddi_get_parent(dip);
	    pdip != NULL && pdip != ddi_root_node();
	    pdip = ddi_get_parent(pdip)) {
		if (iommulib_pci_info(pdip, &pinfo) < 0)
			continue;
		switch (pinfo.pi_type) {
		case IOMMU_DEV_PCIE_ROOT:
		case IOMMU_DEV_PCI_ROOT:
			break;
		case IOMMU_DEV_PCIE_PCI:
		case IOMMU_DEV_PCIE_PCIE:
		case IOMMU_DEV_PCI_PCIE:
		case IOMMU_DEV_PCI_PCI:
			bdip = pdip;
			*pinfop = pinfo;
			continue;
		default:
			continue;
		}
	}

	ndi_rele_devi(dip);

	return (bdip);
}

/*
 * Initialize the wait structure used for queued invalidation (Intel)
 * or command processing (AMD).
 */
void
iommulib_init_wait(iommu_wait_t *iwp, const char *name, boolean_t sync)
{
	caddr_t vaddr;
	uint64_t paddr;

	iwp->iwp_sync = sync;

	vaddr = (caddr_t)&iwp->iwp_vstatus;
	paddr = mmu_ptob((paddr_t)hat_getpfnum(kas.a_hat, vaddr));
	paddr += ((uintptr_t)vaddr) & MMU_PAGEOFFSET;

	iwp->iwp_pstatus = paddr;
	iwp->iwp_name = name;
}

/*
 * Match a device to a quirk and call the applicable function.
 */
void
iommulib_quirk_match(dev_info_t *dip, iommu_pci_info_t *pinfo, void *arg,
    iommu_quirk_t *table)
{
	iommu_pci_info_t info;

	if (pinfo == NULL) {
		if (iommulib_pci_base_info(dip, &info) < 0)
			return;
		pinfo = &info;
	}

	for (;;) {
		switch (table->iq_type) {
		case IQMATCH_DEFAULT:
			/*
			 * If a default function was supplied, call it.
			 */
			if (table->iq_func != NULL)
				table->iq_func(dip, arg);
			return;
		case IQMATCH_DNAME:
			if (strcmp(ddi_driver_name(dip),
			    table->iq_dname) == 0) {
				table->iq_func(dip, arg);
				return;
			}
			break;
		case IQMATCH_CLASS:
			if (pinfo->pi_class == table->iq_class &&
			    (table->iq_subclass == (uint_t)~0 ||
			    pinfo->pi_subclass == table->iq_subclass)) {
				table->iq_func(dip, arg);
				return;
			}
			break;
		case IQMATCH_PCIID:
			if (pinfo->pi_vendorid == table->iq_vendorid &&
			    pinfo->pi_devid == table->iq_devid) {
				table->iq_func(dip, arg);
				return;
			}
			break;
		default:
			break;
		}
		table++;
	}
}

/*
 * Used to initialize unit and cache names.
 */
char *
iommulib_alloc_name(const char *str, int instance, size_t *lenp)
{
	size_t slen;
	char *s;

	slen = strlen(str) + IOMMU_ISTRLEN + 1;
	s = kmem_zalloc(slen, KM_SLEEP);
	(void) snprintf(s, slen, "%s%d", str, instance);
	if (lenp)
		*lenp = slen;

	return (s);
}

/*ARGSUSED*/
static int
iommu_hdl_priv_ctor(void *buf, void *arg, int kmf)
{
	ddi_dma_impl_t *hp;
	iommu_hdl_priv_t *ihp;

	hp = buf;
	ihp = buf;

	hp->dmai_private = NULL;
	hp->dmai_nwin = 1;
	iommulib_init_wait(&ihp->ihp_inv_wait, "dmahandle", B_TRUE);

	return (0);
}

/*
 * Initialize all DVMA-related structures for an IOMMU that are not
 * specific to either AMD or Intel.
 */
int
iommulib_init_iommu_dvma(iommu_t *iommu)
{
	char *hcachename, *pcachename, *daname;
	size_t pnlen, hnlen, dnlen;
	int instance, ret;

	ret = -1;

	pcachename = hcachename = daname = NULL;

	instance = ddi_get_instance(iommu->iommu_dip);

	pcachename = iommulib_alloc_name("pcache", instance,
	    &pnlen);
	if (pcachename == NULL)
		return (ret);

	hcachename = iommulib_alloc_name("hcache", instance, &hnlen);
	if (hcachename == NULL)
		goto error;

	iommu->iommu_pgtable_cache = kmem_cache_create(pcachename,
	    sizeof (iommu_pgtable_t), 0, iommu_pgtable_ctor,
	    iommu_pgtable_dtor, NULL, iommu, NULL, 0);
	if (iommu->iommu_pgtable_cache == NULL)
		goto error;

	iommu->iommu_hdl_cache = kmem_cache_create(hcachename,
	    sizeof (iommu_hdl_priv_t), 64, iommu_hdl_priv_ctor,
	    NULL, NULL, iommu, NULL, 0);
	if (iommu->iommu_hdl_cache == NULL)
		goto error;

	daname = iommulib_alloc_name("did_arena", instance, &dnlen);
	if (daname == NULL)
		goto error;

	iommu->iommu_domid_arena = vmem_create(daname,
	    (void *)(uintptr_t)(IOMMU_UNITY_DID + 1),
	    iommu->iommu_dom_maxdom - IOMMU_UNITY_DID,
	    1, NULL, NULL, NULL, 0, VM_SLEEP);
	if (iommu->iommu_domid_arena == NULL)
		goto error;

	if (ddi_dma_alloc_handle(ddi_root_node(), &iommu_alloc_dma_attr,
	    DDI_DMA_SLEEP, NULL, &iommu->iommu_pgtable_dmahdl) != DDI_SUCCESS)
		goto error;

	if (iommu_domain_create_unity(iommu) < 0) {
		ddi_dma_free_handle(&iommu->iommu_pgtable_dmahdl);
		goto error;
	}

	ret = 0;

error:
	if (pcachename != NULL)
		kmem_free(pcachename, pnlen);
	if (hcachename != NULL)
		kmem_free(hcachename, hnlen);
	if (daname != NULL)
		kmem_free(daname, dnlen);

	if (ret < 0) {
		if (iommu->iommu_pgtable_cache != NULL)
			kmem_cache_destroy(iommu->iommu_pgtable_cache);
		if (iommu->iommu_hdl_cache != NULL)
			kmem_cache_destroy(iommu->iommu_hdl_cache);
		if (iommu->iommu_domid_arena != NULL)
			vmem_destroy(iommu->iommu_domid_arena);
	}

	return (ret);
}

/*
 * Reverse the above.
 */
void
iommulib_teardown_iommu_dvma(iommu_t *iommu)
{
	kmem_cache_destroy(iommu->iommu_pgtable_cache);
	kmem_cache_destroy(iommu->iommu_hdl_cache);
	vmem_destroy(iommu->iommu_domid_arena);
	ddi_dma_free_handle(&iommu->iommu_pgtable_dmahdl);
}

/*
 * Allocate one page with the right parameters. Used for page table
 * pages and root table pages.
 */
int
iommulib_page_alloc(iommu_t *iommu, int kmf, iommu_page_t *ip)
{
	int (*dmafp)(caddr_t);
	size_t len;
	uint_t flags;

	len = 0;

	dmafp = (kmf & KM_NOSLEEP) ? DDI_DMA_DONTWAIT : DDI_DMA_SLEEP;

	flags = DDI_DMA_CONSISTENT;
	if (!(iommu->iommu_flags & IOMMU_FLAGS_COHERENT))
		flags |= IOMEM_DATA_UC_WR_COMBINE;

	if (ddi_dma_mem_alloc(iommu->iommu_pgtable_dmahdl,
	    MMU_PAGESIZE, &iommu_alloc_acc_attr, flags, dmafp, NULL,
	    (caddr_t *)&ip->ip_vaddr, &len, &ip->ip_memhdl) != DDI_SUCCESS)
		return (-1);

	ip->ip_paddr = mmu_ptob((paddr_t)hat_getpfnum(kas.a_hat, ip->ip_vaddr));

	(void) memset(ip->ip_vaddr, 0, MMU_PAGESIZE);

	return (0);
}

void
iommulib_page_free(iommu_page_t *ip)
{
	ddi_dma_mem_free(&ip->ip_memhdl);
}

/*
 * Insert and IOMMU in to the global list of units.
 */
void
iommulib_list_insert(iommu_t *iommu)
{
	mutex_enter(&iommu_list_lock);
	list_insert_tail(&iommu_list, iommu);
	mutex_exit(&iommu_list_lock);
}

/*
 * Perform an operation on all IOMMUs in the system.
 */
void
iommulib_list_walk(int (*func)(iommu_t *, void *), void *arg, int *nfail)
{
	iommu_t *iommu, *next;
	int ret;

	if (nfail != NULL)
		*nfail = 0;

	mutex_enter(&iommu_list_lock);
	for (iommu = list_head(&iommu_list); iommu != NULL; iommu = next) {
		next = list_next(&iommu_list, iommu);
		ret = func(iommu, arg);
		switch (ret) {
		case IWALK_SUCCESS_CONTINUE:
			break;
		case IWALK_SUCCESS_STOP:
			goto done;
		case IWALK_FAILURE_CONTINUE:
			if (nfail != NULL)
				(*nfail)++;
			break;
		case IWALK_FAILURE_STOP:
			if (nfail != NULL)
				(*nfail)++;
			goto done;
		case IWALK_REMOVE:
			list_remove(&iommu_list, iommu);
			break;
		}
	}
done:
	mutex_exit(&iommu_list_lock);
}

/*
 * Find an IOMMU from its dev_info
 */
iommu_t *
iommulib_find_iommu(dev_info_t *dip)
{
	iommu_t *iommu;

	mutex_enter(&iommu_list_lock);
	for (iommu = list_head(&iommu_list); iommu != NULL;
	    iommu = list_next(&iommu_list, iommu)) {
		if (iommu->iommu_dip == dip) {
			mutex_exit(&iommu_list_lock);
			return (iommu);
		}
	}
	mutex_exit(&iommu_list_lock);

	return (NULL);
}

/*
 * Convert BDF to a source id value, and back.
 */
uint32_t
iommulib_bdf_to_sid(uint_t seg, uint_t bus, uint_t dev, uint_t func)
{
	return ((seg << 16) | (bus << 8) | (dev << 3) | func);
}

void
iommulib_sid_to_bdf(uint32_t sid, uint_t *seg, uint_t *bus, uint_t *dev,
    uint_t *func)
{
	*seg = sid >> 16;
	*bus = (sid >> 8) & 0xff;
	*dev = (sid >> 3) & 0x1f;
	*func = sid & 0x7;
}

/*
 * Create a global hash table for domains, so that they can be
 * found easily if only the source id of a device is available
 * (e.g. in a fault interrupt).
 */
void
iommulib_domhash_create(void)
{
	uint_t kval;
	size_t nchains;

	nchains = 255;
	kval = mod_hash_iddata_gen(nchains);

	iommu_domain_hash = mod_hash_create_extended("iommu_domain_hash",
	    nchains, mod_hash_null_keydtor, mod_hash_null_valdtor,
	    mod_hash_byid, (void *)(uintptr_t)kval, mod_hash_idkey_cmp,
	    KM_SLEEP);
}

void
iommulib_domhash_insert(uint32_t sid, iommu_domain_t *domain)
{
	(void) mod_hash_insert(iommu_domain_hash,
	    (mod_hash_key_t)(uintptr_t)sid,
	    (mod_hash_val_t)(uintptr_t)domain);
}

iommu_domain_t *
iommulib_domhash_find(uint32_t sid)
{
	iommu_domain_t *domain;

	if (mod_hash_find(iommu_domain_hash, (mod_hash_val_t)(uintptr_t)sid,
	    (mod_hash_val_t *)&domain) == 0)
		return (domain);

	return (NULL);
}

void
iommulib_domhash_remove(uint32_t sid)
{
	mod_hash_val_t val;

	(void) mod_hash_remove(iommu_domain_hash,
	    (mod_hash_key_t)(uintptr_t)sid, &val);
}

uint64_t *
iommulib_find_pte(iommu_domain_t *domain, uint64_t dvma)
{
	iommu_t *iommu;
	iommu_xlate_t xlate[IOMMU_PGTABLE_MAXLEVELS + 1] = {0}, *xlatep;

	iommu = domain->dom_iommu;

	iommu_pgtable_xlate_setup(dvma, xlate, iommu->iommu_pgtable_nlevels);
	if (!iommu_pgtable_lookup_pdps(domain, xlate,
	    iommu->iommu_pgtable_nlevels))
		return (NULL);

	xlatep = &xlate[1];
	return (xlatep->xlt_pgtable->pgt_vaddr + xlatep->xlt_idx);
}

/*
 * Parse boot commandline options.
 */
static void
iommulib_getbootopt(char *opt, int *bval)
{
	int ret;
	char *val = NULL;

	ret = ddi_prop_lookup_string(DDI_DEV_T_ANY, ddi_root_node(),
	    DDI_PROP_DONTPASS, opt, &val);
	if (ret != DDI_PROP_SUCCESS)
		return;

	if (strcmp(val, "true") == 0)
		*bval = 1;
	else if (strcmp(val, "false") == 0)
		*bval = 0;
}

/*
 * Get boot commandline options. This also includes a check
 * for "disable-intel_iommu" or "disable-amd_iommu", to have
 * consistent handling of both cases.
 */
void
iommulib_bootops(char *driver, int *enable, int *dvma_enable,
    int *intrmap_enable)
{
	if (devnamesp[ddi_name_to_major(driver)].dn_flags & DN_DRIVER_REMOVED) {
		*enable = *dvma_enable = *intrmap_enable = 0;
		return;
	}

	iommulib_getbootopt("iommu-enable", enable);
	if (!*enable) {
		*dvma_enable = *intrmap_enable = 0;
		return;
	}

	iommulib_getbootopt("iommu-dvma-enable", dvma_enable);
	iommulib_getbootopt("iommu-intrmap-enable", intrmap_enable);

	if (!*dvma_enable && !*intrmap_enable)
		*enable = 0;
}
