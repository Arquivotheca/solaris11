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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * Copyright (c) 2009, Intel Corporation.
 * All rights reserved.
 */

/*
 * DVMA code
 * This file contains Intel IOMMU code that deals with DVMA
 * i.e. DMA remapping.
 */

#include <sys/sysmacros.h>
#include <sys/pcie.h>
#include <sys/pci_cfgspace.h>
#include <vm/hat_i86.h>
#include <sys/memlist.h>
#include <sys/acpi/acpi.h>
#include <sys/acpica.h>
#include <sys/modhash.h>
#include <sys/immu.h>
#include <sys/x86_archext.h>
#include <sys/archsystm.h>
#include <sys/sunndi.h>

static void
context_set(immu_t *immu, iommu_domain_t *domain, int bus, int devfunc)
{
	iommu_page_t *context;
	hw_rce_t *hw_cent;

	context = &immu->immu_ctx_bus[bus];
	hw_cent = (hw_rce_t *)(context->ip_vaddr) + devfunc;

	mutex_enter(&immu->immu_ctx_lock);

	/* need to disable context entry before reprogramming it */
	bzero(hw_cent, sizeof (hw_rce_t));

	/*
	 * Flush everything here, to get rid of any cached old context
	 * entries for this domain, the previous domain, and any cached
	 * non-present entries (caching mode).
	 */
	immu_flush_context_gbl(immu, &immu->immu_ctx_inv_wait);

	CONT_SET_DID(hw_cent, domain->dom_did);
	CONT_SET_AW(hw_cent, immu->immu_iommu.iommu_pgtable_width);

	if (domain->dom_did == IOMMU_UNITY_DID &&
	    IMMU_ECAP_GET_PT(immu->immu_regs_excap)) {
		/*LINTED*/
		CONT_SET_ASR(hw_cent, (paddr_t)0);
		CONT_SET_TTYPE(hw_cent, TTYPE_PASSTHRU);
	} else {
		CONT_SET_ASR(hw_cent,
		    domain->dom_pgtable_root->pgt_paddr);
		/*LINTED*/
		CONT_SET_TTYPE(hw_cent, TTYPE_XLATE_ONLY);
	}

	CONT_SET_P(hw_cent);

	/*
	 * If an entry with an old, different domain was
	 * overwritten, that needs to be flushed too. However,
	 * that may not be enough for caching mode.
	 *
	 * Just use a big hammer and invalidate the entire
	 * context cache again, and the IOTLB. This operation is
	 * infrequent and not performance critical (only happens on
	 * attach / detach of a driver).
	 */
	immu_flush_context_gbl(immu, &immu->immu_ctx_inv_wait);
	immu_flush_iotlb_gbl(immu, &immu->immu_ctx_inv_wait);

	mutex_exit(&immu->immu_ctx_lock);
}

static int
context_create(immu_t *immu)
{
	int bus, devfunc, i;
	iommu_pgtable_t *pgtable_root;
	iommu_page_t *ip;
	iommu_t *iommu;
	iommu_domain_t *udom;
	hw_rce_t *hw_rent;
	hw_rce_t *hw_cent;

	iommu = (iommu_t *)immu;
	udom = iommu->iommu_unity_domain;

	if (iommulib_page_alloc(iommu, KM_SLEEP, &immu->immu_ctx_root) < 0)
		return (-1);

	hw_rent = (hw_rce_t *)immu->immu_ctx_root.ip_vaddr;
	for (bus = 0; bus < IMMU_ROOT_NUM; bus++, hw_rent++) {
		ip = &immu->immu_ctx_bus[bus];

		if (iommulib_page_alloc(iommu, KM_SLEEP, ip) < 0) {
			for (i = 0; i < bus; i++)
				iommulib_page_free(&immu->immu_ctx_bus[bus]);
			iommulib_page_free(&immu->immu_ctx_root);
			return (-1);
		}

		hw_cent = (hw_rce_t *)ip->ip_vaddr;

		for (devfunc = 0; devfunc < IMMU_CONT_NUM;
		    devfunc++, hw_cent++) {
			pgtable_root = udom->dom_pgtable_root;
			CONT_SET_DID(hw_cent, udom->dom_did);
			CONT_SET_AW(hw_cent, iommu->iommu_pgtable_width);

			if (IMMU_ECAP_GET_PT(immu->immu_regs_excap)) {
				CONT_SET_TTYPE(hw_cent, TTYPE_PASSTHRU);
				/*LINTED*/
				CONT_SET_ASR(hw_cent, (paddr_t)0);
			} else {
				/*LINTED*/
				CONT_SET_TTYPE(hw_cent, TTYPE_XLATE_ONLY);
				CONT_SET_ASR(hw_cent, pgtable_root->pgt_paddr);
			}

			CONT_SET_P(hw_cent);
		}

		ROOT_SET_CONT(hw_rent, ip->ip_paddr);
		ROOT_SET_P(hw_rent);
	}

	immu_flush_context_gbl(immu, &immu->immu_ctx_inv_wait);
	immu_flush_iotlb_gbl(immu, &immu->immu_ctx_inv_wait);

	return (0);
}

void
immu_print_fault_info(uint_t sid, uint64_t dvma)
{
	uint64_t *ptep;
	iommu_domain_t *domain;

	domain = iommulib_domhash_find(sid);
	if (domain == NULL) {
		ddi_err(DER_WARN, NULL,
		    "no domain for faulting SID %08x", sid);
		return;
	}

	ptep = iommulib_find_pte(domain, dvma);
	if (ptep == NULL) {
		ddi_err(DER_WARN, domain->dom_dip,
		    "pte not found in domid %d for faulting addr %" PRIx64,
		    domain->dom_did, dvma);
		return;
	}

	ddi_err(DER_WARN, domain->dom_dip,
	    "domid %d pte: %" PRIx64 "(paddr %" PRIx64 ")",
	    domain->dom_did,
	    (unsigned long long)*ptep, (unsigned long long)PDTE_PADDR(*ptep));
}


/*ARGSUSED*/
static int
immu_dvma_setup_unit(iommu_t *iommu, void *arg)
{
	immu_t *immu;

	immu = (immu_t *)iommu;

	immu->immu_dvma_setup = B_TRUE;

	return (IWALK_SUCCESS_CONTINUE);
}

/*
 * setup the DVMA subsystem
 * this code runs only for the first IOMMU unit
 */
void
immu_dvma_setup(void)
{
	if (!immu_dvma_enable)
		return;

	iommulib_domhash_create();

	iommulib_list_walk(immu_dvma_setup_unit, NULL, NULL);
}

/*
 * Startup up one DVMA unit
 */
void
immu_dvma_startup(immu_t *immu)
{
	if (!immu_dvma_enable)
		return;

	mutex_init(&immu->immu_ctx_lock, NULL, MUTEX_DEFAULT, NULL);

	iommulib_init_wait(&immu->immu_ctx_inv_wait, "ctxglobal", B_TRUE);

	immu_regs_wbf_flush(immu);

	if (iommulib_init_iommu_dvma((iommu_t *)immu) < 0)
		return;

	if (context_create(immu) < 0) {
		iommulib_teardown_iommu_dvma((iommu_t *)immu);
		return;
	}

	/*
	 * DVMA will start once IOMMU is "running"
	 */
	immu->immu_dvma_running = B_TRUE;
	immu->immu_iommu.iommu_flags |= IOMMU_FLAGS_DVMA_ENABLE;
}

/*ARGSUSED*/
void
immu_set_root_table(dev_info_t *dip, iommu_t *iommu, uint_t bus, uint_t dev,
    uint_t func, iommu_domain_t *domain)
{
	context_set((immu_t *)iommu, domain, bus, (dev << 3) | func);
}
