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

#include <sys/sunddi.h>
#include <sys/sunndi.h>
#include <sys/acpi/acpi.h>
#include <sys/acpica.h>
#include <sys/amd_iommu.h>
#include <sys/bootconf.h>
#include <sys/sysmacros.h>
#include <sys/ddidmareq.h>

#include "amd_iommu_impl.h"
#include "amd_iommu_acpi.h"
#include "amd_iommu_page_tables.h"

int
amd_iommu_set_devtbl_entry(amd_iommu_t *iommu, iommu_domain_t *domain,
    uint16_t deviceid)
{
	uint64_t entry[4] = {0};
	uint64_t *devtbl_entry;
	amd_iommu_cmdargs_t cmdargs = {0};
	int error, flags, i;
	uint16_t olddom;
	paddr_t rootpa;

	/*LINTED*/
	devtbl_entry = (uint64_t *)&iommu->aiomt_devtbl
	    [deviceid * AMD_IOMMU_DEVTBL_ENTRY_SZ];

	cmdargs.ca_deviceid = deviceid;
	error = amd_iommu_cmd(iommu, AMD_IOMMU_CMD_INVAL_DEVTAB_ENTRY,
	    &cmdargs, 0, 0);
	if (error != DDI_SUCCESS)
		return (error);

	cmdargs.ca_addr = (uintptr_t)0x7FFFFFFFFFFFF000;
	flags = AMD_IOMMU_CMD_FLAGS_PAGE_PDE_INVAL |
	    AMD_IOMMU_CMD_FLAGS_PAGE_INVAL_S;

	olddom = IOMMU_UNITY_DID;

	if (AMD_IOMMU_REG_GET64(&(devtbl_entry[0]), AMD_IOMMU_DEVTBL_V) == 1 &&
	    AMD_IOMMU_REG_GET64(&(devtbl_entry[0]), AMD_IOMMU_DEVTBL_TV) == 1) {
		olddom = AMD_IOMMU_REG_GET64(&(devtbl_entry[1]),
		    AMD_IOMMU_DEVTBL_DOMAINID);
	}

	/*
	 * If the old domain in the entry was valid, flush it too.
	 */
	if (olddom != IOMMU_UNITY_DID) {
		cmdargs.ca_domainid = olddom;
		error = amd_iommu_cmd(iommu, AMD_IOMMU_CMD_INVAL_IOMMU_PAGES,
		    &cmdargs, flags, 0);
		if (error != DDI_SUCCESS)
			return (error);
	}

	/*
	 * Flush the new domain.
	 */
	cmdargs.ca_domainid = domain->dom_did;

	error = amd_iommu_cmd(iommu, AMD_IOMMU_CMD_INVAL_IOMMU_PAGES,
	    &cmdargs, flags, 0);
	if (error != DDI_SUCCESS)
		return (error);

	if (domain->dom_did == IOMMU_UNITY_DID)
		rootpa = 0;
	else
		rootpa = domain->dom_pgtable_root->pgt_paddr >> MMU_PAGESHIFT;

	for (i = 0; i < 4; i++) {
		entry[i] = devtbl_entry[i];
	}

	AMD_IOMMU_REG_SET64(&(entry[1]), AMD_IOMMU_DEVTBL_EX, 1);
	AMD_IOMMU_REG_SET64(&(entry[1]), AMD_IOMMU_DEVTBL_SD, 0);
	AMD_IOMMU_REG_SET64(&(entry[1]), AMD_IOMMU_DEVTBL_CACHE, 0);
	AMD_IOMMU_REG_SET64(&(entry[1]), AMD_IOMMU_DEVTBL_IOCTL, 1);
	AMD_IOMMU_REG_SET64(&(entry[1]), AMD_IOMMU_DEVTBL_SA, 0);
	AMD_IOMMU_REG_SET64(&(entry[1]), AMD_IOMMU_DEVTBL_SE, 1);
	AMD_IOMMU_REG_SET64(&(entry[1]), AMD_IOMMU_DEVTBL_DOMAINID,
	    domain->dom_did);
	AMD_IOMMU_REG_SET64(&(entry[0]), AMD_IOMMU_DEVTBL_IW, 1);
	AMD_IOMMU_REG_SET64(&(entry[0]), AMD_IOMMU_DEVTBL_IR, 1);
	AMD_IOMMU_REG_SET64(&(entry[0]), AMD_IOMMU_DEVTBL_ROOT_PGTBL, rootpa);
	AMD_IOMMU_REG_SET64(&(entry[0]), AMD_IOMMU_DEVTBL_PG_MODE,
	    ((iommu_t *)iommu)->iommu_pgtable_nlevels);
	AMD_IOMMU_REG_SET64(&(entry[0]), AMD_IOMMU_DEVTBL_TV,
	    domain->dom_did == IOMMU_UNITY_DID ? 0 : 1);
	AMD_IOMMU_REG_SET64(&(entry[0]), AMD_IOMMU_DEVTBL_V,
	    domain->dom_did == IOMMU_UNITY_DID ? 0 : 1);

	for (i = 1; i < 4; i++) {
		devtbl_entry[i] = entry[i];
	}
	devtbl_entry[0] = entry[0];

	cmdargs.ca_deviceid = deviceid;
	error = amd_iommu_cmd(iommu, AMD_IOMMU_CMD_INVAL_DEVTAB_ENTRY,
	    &cmdargs, 0, 0);

	return (error);
}
