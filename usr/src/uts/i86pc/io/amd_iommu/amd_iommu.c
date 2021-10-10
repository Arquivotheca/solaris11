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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/file.h>
#include <sys/errno.h>
#include <sys/open.h>
#include <sys/stat.h>
#include <sys/cred.h>
#include <sys/modctl.h>
#include <sys/conf.h>
#include <sys/devops.h>
#include <sys/ddi.h>
#include <sys/x86_archext.h>

#include <sys/amd_iommu.h>
#include "amd_iommu_impl.h"
#include "amd_iommu_acpi.h"
#include "amd_iommu_page_tables.h"


#define	AMD_IOMMU_MINOR2INST(x)	(x)
#define	AMD_IOMMU_INST2MINOR(x)	(x)
#define	AMD_IOMMU_NODETYPE	"ddi_iommu"
#define	AMD_IOMMU_MINOR_NAME	"amd-iommu"

static int amd_iommu_attach(dev_info_t *dip, ddi_attach_cmd_t cmd);
static int amd_iommu_detach(dev_info_t *dip, ddi_detach_cmd_t cmd);

static void amd_iommu_startup(void);
static int amd_iommu_quiesce(int reboot);
static int amd_iommu_unquiesce(void);
static void *amd_iommu_find_dvma_unit(dev_info_t *dip, iommu_pci_info_t *pinfo);
static void amd_iommu_dev_reserved(dev_info_t *dip, iommu_pci_info_t *pinfo,
    struct memlist **mlpp, struct memlist **unityp);
static void amd_iommu_iommu_reserved(iommu_t *iommu, struct memlist **mlpp);
static void amd_iommu_set_root_table(dev_info_t *dip, iommu_t *iommu,
    uint_t bus, uint_t dev, uint_t func, iommu_domain_t *domain);
static void amd_iommu_flush_domain(iommu_t *iommu, uint_t domid,
    iommu_wait_t *iwp);
static void amd_iommu_flush_pages(iommu_t *iommu, uint_t domid, uint64_t sdvma,
    uint_t npages, iommu_wait_t *iwp);
static void amd_iommu_flush_buffers(iommu_t *iommu);


static struct dev_ops amd_iommu_dev_ops = {
	DEVO_REV,		/* devo_rev */
	0,			/* devo_refcnt */
	ddi_no_info,	/* devo_getinfo */
	nulldev,		/* devo_identify */
	nulldev,		/* devo_probe */
	amd_iommu_attach,	/* devo_attach */
	amd_iommu_detach,	/* devo_detach */
	nodev,			/* devo_reset */
	NULL,
	NULL,			/* devo_bus_ops */
	NULL,			/* devo_power */
	ddi_quiesce_not_needed,	/* devo_quiesce */
};

static struct modldrv modldrv = {
	&mod_driverops,
	"AMD IOMMU 0.1",
	&amd_iommu_dev_ops
};

static struct modlinkage modlinkage = {
	MODREV_1,
	(void *)&modldrv,
	NULL
};

struct iommu_ops amd_iommu_ops = {
	.ioo_start_units = amd_iommu_startup,
	.ioo_quiesce_units = amd_iommu_quiesce,
	.ioo_unquiesce_units = amd_iommu_unquiesce,
	.ioo_find_unit = amd_iommu_find_dvma_unit,
	.ioo_dev_reserved = amd_iommu_dev_reserved,
	.ioo_iommu_reserved = amd_iommu_iommu_reserved,
	.ioo_set_root_table = amd_iommu_set_root_table,
	.ioo_flush_domain = amd_iommu_flush_domain,
	.ioo_flush_pages = amd_iommu_flush_pages,
	.ioo_flush_buffers = amd_iommu_flush_buffers,

	.ioo_rmask = AMD_IOMMU_PTE_RMASK,
	.ioo_wmask = AMD_IOMMU_PTE_WMASK,
	.ioo_ptemask = {
		AMD_IOMMU_PTE_NEXTLEV(0) | AMD_IOMMU_PTE_PMASK |
		    AMD_IOMMU_PTE_FCMASK,
		AMD_IOMMU_PTE_NEXTLEV(1) | AMD_IOMMU_PTE_PMASK,
		AMD_IOMMU_PTE_NEXTLEV(2) | AMD_IOMMU_PTE_PMASK,
		AMD_IOMMU_PTE_NEXTLEV(3) | AMD_IOMMU_PTE_PMASK,
		AMD_IOMMU_PTE_NEXTLEV(4) | AMD_IOMMU_PTE_PMASK,
		AMD_IOMMU_PTE_NEXTLEV(5) | AMD_IOMMU_PTE_PMASK,
	},

	.ioo_dma_allochdl = iommu_dma_allochdl,
	.ioo_dma_freehdl = iommu_dma_freehdl,
	.ioo_dma_bindhdl = iommu_dma_bindhdl,
	.ioo_dma_unbindhdl = iommu_dma_unbindhdl,
	.ioo_dma_win = iommu_dma_win
};


amd_iommu_debug_t amd_iommu_debug;
kmutex_t amd_iommu_global_lock;
const char *amd_iommu_modname = "amd_iommu";
amd_iommu_alias_t **amd_iommu_alias;
amd_iommu_page_table_hash_t amd_iommu_page_table_hash;
static void *amd_iommu_statep;
int amd_iommu_64bit_bug;
int amd_iommu_unity_map;
int amd_iommu_no_RW_perms;
int amd_iommu_no_unmap;
int amd_iommu_pageva_inval_all;

int amd_iommu_enable = 0;
int amd_iommu_dvma_enable = 1;
int amd_iommu_intrmap_enable = 0;

int
_init(void)
{
	int error = ENOTSUP;

#if !defined(__xpv)

	if (get_hwenv() != HW_NATIVE)
		return (ENOTSUP);

	iommulib_bootops((char *)amd_iommu_modname, &amd_iommu_enable,
	    &amd_iommu_dvma_enable, &amd_iommu_intrmap_enable);

	if (!amd_iommu_enable)
		return (ENOTSUP);


	error = ddi_soft_state_init(&amd_iommu_statep,
	    sizeof (struct amd_iommu_state), 1);
	if (error) {
		cmn_err(CE_WARN, "%s: _init: failed to init soft state.",
		    amd_iommu_modname);
		return (error);
	}

	if (amd_iommu_acpi_init() != DDI_SUCCESS) {
		if (amd_iommu_debug) {
			cmn_err(CE_WARN, "%s: _init: ACPI init failed.",
			    amd_iommu_modname);
		}
		ddi_soft_state_fini(&amd_iommu_statep);
		return (ENOTSUP);
	}

	error = mod_install(&modlinkage);
	if (error) {
		cmn_err(CE_WARN, "%s: _init: mod_install failed.",
		    amd_iommu_modname);
		amd_iommu_acpi_fini();
		ddi_soft_state_fini(&amd_iommu_statep);
		amd_iommu_statep = NULL;
		return (error);
	}
	error = 0;

	iommu_ops = &amd_iommu_ops;
#endif

	return (error);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

int
_fini(void)
{
	int error;

	error = mod_remove(&modlinkage);
	if (error)
		return (error);

	amd_iommu_acpi_fini();
	ddi_soft_state_fini(&amd_iommu_statep);
	amd_iommu_statep = NULL;

	return (0);
}

static int
amd_iommu_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	int instance = ddi_get_instance(dip);
	const char *driver = ddi_driver_name(dip);
	struct amd_iommu_state *statep;

	ASSERT(instance >= 0);
	ASSERT(driver);

	switch (cmd) {
	case DDI_ATTACH:
		if (ddi_soft_state_zalloc(amd_iommu_statep, instance)
		    != DDI_SUCCESS) {
			cmn_err(CE_WARN, "Unable to allocate soft state for "
			    "%s%d", driver, instance);
			return (DDI_FAILURE);
		}

		statep = ddi_get_soft_state(amd_iommu_statep, instance);
		if (statep == NULL) {
			cmn_err(CE_WARN, "Unable to get soft state for "
			    "%s%d", driver, instance);
			ddi_soft_state_free(amd_iommu_statep, instance);
			return (DDI_FAILURE);
		}

		statep->aioms_devi = dip;
		statep->aioms_instance = instance;
		statep->aioms_iommu_start = NULL;
		statep->aioms_iommu_end = NULL;

		if (amd_iommu_setup(dip, statep) != DDI_SUCCESS) {
			cmn_err(CE_WARN, "Unable to initialize AMD IOMMU "
			    "%s%d", driver, instance);
			ddi_remove_minor_node(dip, NULL);
			ddi_soft_state_free(amd_iommu_statep, instance);
			return (DDI_FAILURE);
		}

		ddi_report_dev(dip);

		return (DDI_SUCCESS);

	case DDI_RESUME:
		return (DDI_SUCCESS);
	default:
		return (DDI_FAILURE);
	}
}

static int
amd_iommu_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	int instance = ddi_get_instance(dip);
	const char *driver = ddi_driver_name(dip);
	struct amd_iommu_state *statep;

	ASSERT(instance >= 0);
	ASSERT(driver);

	switch (cmd) {
	case DDI_DETACH:
		statep = ddi_get_soft_state(amd_iommu_statep, instance);
		if (statep == NULL) {
			cmn_err(CE_WARN, "%s%d: Cannot get soft state",
			    driver, instance);
			return (DDI_FAILURE);
		}
		return (DDI_FAILURE);
	case DDI_SUSPEND:
		return (DDI_SUCCESS);
	default:
		return (DDI_FAILURE);
	}
}

/*ARGSUSED*/
static int
amd_iommu_quiesce_unit(iommu_t *iommu, void *arg)
{
	amd_iommu_t *aiop;

	aiop = (amd_iommu_t *)iommu;
	amd_iommu_stop(aiop);

	return (IWALK_SUCCESS_CONTINUE);
}

/*ARGSUSED*/
static int
amd_iommu_unquiesce_unit(iommu_t *iommu, void *arg)
{
	amd_iommu_t *aiop;

	aiop = (amd_iommu_t *)iommu;
	(void) amd_iommu_start(aiop);

	return (IWALK_SUCCESS_CONTINUE);
}

/*ARGSUSED*/
static int
amd_iommu_teardown_unit(iommu_t *iommu, void *arg)
{
	amd_iommu_t *aiop;

	aiop = (amd_iommu_t *)iommu;
	(void) amd_iommu_fini(aiop, AMD_IOMMU_TEARDOWN);

	return (IWALK_SUCCESS_CONTINUE);
}

/*ARGSUSED*/
static int
amd_iommu_start_unit(iommu_t *iommu, void *arg)
{
	amd_iommu_t *aiop;

	aiop = (amd_iommu_t *)iommu;
	(void) amd_iommu_start(aiop);

	return (IWALK_SUCCESS_CONTINUE);
}

static int
amd_iommu_quiesce(int reboot)
{
	if (reboot)
		iommulib_list_walk(amd_iommu_teardown_unit, NULL, NULL);
	else
		iommulib_list_walk(amd_iommu_quiesce_unit, NULL, NULL);

	return (DDI_SUCCESS);
}

static int
amd_iommu_unquiesce(void)
{
	iommulib_list_walk(amd_iommu_unquiesce_unit, NULL, NULL);

	return (DDI_SUCCESS);
}

static void
amd_iommu_startup(void)
{
	iommulib_domhash_create();

	iommulib_list_walk(amd_iommu_start_unit, NULL, NULL);
}

struct amd_find_iommu {
	dev_info_t *afi_dip;
	iommu_pci_info_t *afi_pinfo;
	amd_iommu_acpi_ivhd_t *afi_hinfop;
	iommu_t *afi_iommu;
	int afi_seg;
	uint8_t afi_bus;
	uint8_t afi_devfn;
};

/*
 * Find a matching IOMMU for a device. Passed in are an IVHD entry pointer,
 * and PCI info. The IVHD table is the preferred source of information,
 * but if we didn't find one, fall back to the range registers.
 */
static int
amd_iommu_find_unit(iommu_t *iommu, void *arg)
{
	struct amd_find_iommu *afip;
	amd_iommu_t *aiop;

	afip = arg;
	aiop = (amd_iommu_t *)iommu;

	if (afip->afi_hinfop == NULL) {
		if (afip->afi_seg != iommu->iommu_seg || !aiop->aiomt_rng_valid)
			return (IWALK_FAILURE_CONTINUE);
		if (afip->afi_bus == aiop->aiomt_rng_bus &&
		    afip->afi_devfn >= aiop->aiomt_first_devfn &&
		    afip->afi_devfn <= aiop->aiomt_last_devfn) {
			afip->afi_iommu = iommu;
			return (IWALK_SUCCESS_STOP);
		}
	} else {
		if (aiop->aiomt_bdf == afip->afi_hinfop->ach_IOMMU_deviceid &&
		    iommu->iommu_seg == afip->afi_hinfop->ach_IOMMU_pci_seg &&
		    aiop->aiomt_cap_base ==
		    afip->afi_hinfop->ach_IOMMU_cap_off) {
			afip->afi_iommu = iommu;
			return (IWALK_SUCCESS_STOP);
		}

	}

	return (IWALK_FAILURE_CONTINUE);
}

static void *
amd_iommu_find_dvma_unit(dev_info_t *dip, iommu_pci_info_t *pinfo)
{
	struct amd_find_iommu afi;
	amd_iommu_acpi_ivhd_t *hinfop;
	uint16_t bdf;

	bdf = (uint16_t)iommulib_bdf_to_sid(0, pinfo->pi_init_bus,
	    pinfo->pi_dev, pinfo->pi_func);
	hinfop = amd_iommu_lookup_ivhd(pinfo->pi_seg, bdf);

	afi.afi_dip = dip;
	afi.afi_hinfop = hinfop;
	afi.afi_iommu = NULL;
	afi.afi_seg = pinfo->pi_seg;
	afi.afi_bus = pinfo->pi_init_bus;
	afi.afi_devfn = (uint8_t)bdf;

	iommulib_list_walk(amd_iommu_find_unit, &afi, NULL);

	return (afi.afi_iommu);
}

static void
amd_iommu_dev_reserved(dev_info_t *dip, iommu_pci_info_t *pinfo,
    struct memlist **resvpp, struct memlist **unitypp)
{
	amd_iommu_acpi_rsvd_mem(dip, pinfo, resvpp, unitypp);
}

/*ARGSUSED*/
static void
amd_iommu_iommu_reserved(iommu_t *iommu, struct memlist **mlpp)
{
	/*
	 * This is only used for the 1:1 domain when no hardware
	 * passthrough is available. Since AMD IOMMUs always
	 * have hardware passthrough, this function can remain
	 * empty.
	 */
}

/*ARGSUSED*/
static void
amd_iommu_set_root_table(dev_info_t *dip, iommu_t *iommu,
    uint_t bus, uint_t dev, uint_t func, iommu_domain_t *domain)
{
	amd_iommu_t *aiop;
	amd_iommu_acpi_ivhd_t *hinfop;
	int32_t bdf, alias;

	aiop = (amd_iommu_t *)iommu;

	bdf = iommulib_bdf_to_sid(0, bus, dev, func);

	hinfop = amd_iommu_lookup_ivhd(iommu->iommu_seg, bdf);
	if (hinfop == NULL || hinfop->ach_src_deviceid == -1)
		alias = -1;
	else
		alias = hinfop->ach_src_deviceid;

	(void) amd_iommu_set_devtbl_entry(aiop, domain, (uint16_t)bdf);
	if (alias != -1 && alias != bdf)
		(void) amd_iommu_set_devtbl_entry(aiop, domain,
		    (uint16_t)alias);
}

/*ARGSUSED*/
static void
amd_iommu_flush_domain(iommu_t *iommu, uint_t domid, iommu_wait_t *iwp)
{
	amd_iommu_cmdargs_t cmdargs = {0};
	int flags;

	cmdargs.ca_domainid = (uint16_t)domid;
	cmdargs.ca_addr = (uintptr_t)0x7FFFFFFFFFFFF000;
	flags = AMD_IOMMU_CMD_FLAGS_PAGE_PDE_INVAL |
	    AMD_IOMMU_CMD_FLAGS_PAGE_INVAL_S;

	(void) amd_iommu_cmd((amd_iommu_t *)iommu,
	    AMD_IOMMU_CMD_INVAL_IOMMU_PAGES, &cmdargs, flags, 0);
}

/*ARGSUSED*/
static void
amd_iommu_flush_pages(iommu_t *iommu, uint_t domid, uint64_t sdvma,
    uint_t npages, iommu_wait_t *iwp)
{
	amd_iommu_flush_domain(iommu, domid, NULL);
}

/*ARGSUSED*/
static void
amd_iommu_flush_buffers(iommu_t *iommu)
{
	/* Nothing to do. Should not be called. */
}
