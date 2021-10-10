/* BEGIN CSTYLED */

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
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/conf.h>
#include <sys/kmem.h>
#include <sys/visual_io.h>
#include <sys/font.h>
#include <sys/fbio.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/open.h>
#include <sys/modctl.h>
#include <sys/vgareg.h>
#include <sys/vgasubr.h>
#include <sys/pci.h>
#include <sys/kd.h>
#include <sys/ddi_impldefs.h>
#include <sys/sunldi.h>
#include <sys/mkdev.h>
#include <gfx_private.h>
#include <sys/agpgart.h>
#include <sys/agp/agpdefs.h>
#include <sys/agp/agpmaster_io.h>
#include "drmP.h"

#define	PCI_BUS(x)   (((x) & 0xff0000) >> 16)
#define	PCI_SLOT(x)  (((x)>>11) & 0x1f)
#define	PCI_FUNC(x)  (((x) & 0x700) >> 8)

struct pci_dev *
pci_dev_create(struct drm_device *dev)
{
	dev_info_t *dip = dev->devinfo;
	pci_regspec_t *regspec;
	struct pci_dev *pdev;
	int *regs, ret, len, i;
	uint_t nregs = 0;

	pdev = kmem_zalloc(sizeof(struct pci_dev), KM_NOSLEEP);
	if (!pdev)
		return (NULL);

	/* access handle */
	ret = pci_config_setup(dip, &pdev->pci_cfg_acc_handle);
	if (ret != DDI_SUCCESS) {
		DRM_ERROR("pci_config_setup() failed");
		goto err_setup;
	}

	/* XXX Fix domain number (alpha hoses) */
	pdev->domain = 0;

	/* bus, slot, func */
	ret = ddi_prop_lookup_int_array(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, "reg", (int **)&regs, &nregs);
	if (ret != DDI_PROP_SUCCESS) {
		DRM_ERROR("ddi_prop_lookup_int_array() failed");
		goto err_info;
	}
	pdev->bus = (int)PCI_BUS(regs[0]);
	pdev->slot = (int)PCI_SLOT(regs[0]);
	pdev->func = (int)PCI_FUNC(regs[0]);
	ddi_prop_free(regs);

	/* irq */
	ret = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, "interrupts", -1);
	if (ret == -1) {
		DRM_ERROR("ddi_prop_get_int() failed");
		goto err_irq;
	}
	if (ret > 0)
		pdev->irq = pci_config_get8(pdev->pci_cfg_acc_handle, PCI_CONF_ILINE);

	if (ddi_intr_hilevel(dip, 0) != 0) {
		DRM_ERROR("high-level interrupts are not supported");
		goto err_irq;
	}

	if (ddi_get_iblock_cookie(dip, (uint_t)0,
	    &pdev->intr_block) != DDI_SUCCESS) {
		DRM_ERROR("cannot get iblock cookie");
		goto err_irq;
	}

	/* regions */
	ret = ddi_getlongprop(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, "assigned-addresses", (caddr_t)&regspec, &len);
	if (ret != DDI_PROP_SUCCESS) {
		DRM_ERROR("ddi_getlongprop() failed");
		goto err_regions;
	}
	for (i = 0; i < PCI_CONFIG_REGION_NUMS; i++) {
		pdev->regions[i].start =
		    (unsigned long)regspec[i].pci_phys_low;
		pdev->regions[i].size =
		    (unsigned long)regspec[i].pci_size_low;
	}
	kmem_free(regspec, (size_t)len);

	/* vendor, device */
	pdev->vendor = ddi_prop_get_int(DDI_DEV_T_ANY,
	    dev->devinfo, DDI_PROP_DONTPASS, "vendor-id", 0);
	pdev->device = ddi_prop_get_int(DDI_DEV_T_ANY,
	    dev->devinfo, DDI_PROP_DONTPASS, "device-id", 0);

	pdev->dev = dev;
	return pdev;

err_regions:
err_irq:
err_info:
	pci_config_teardown(&pdev->pci_cfg_acc_handle);
err_setup:
	kmem_free(pdev, sizeof(struct pci_dev));
	return (NULL);
}

void pci_dev_destroy(struct pci_dev *pdev)
{
	pci_config_teardown(&pdev->pci_cfg_acc_handle);
	kmem_free(pdev, sizeof(struct pci_dev));
}

int pci_read_config_byte(struct pci_dev *pdev, int where, u8 *val)
{
	*val = pci_config_get8(pdev->pci_cfg_acc_handle, where);
	return 0;
}

int pci_read_config_word(struct pci_dev *pdev, int where, u16 *val)
{
	*val = pci_config_get16(pdev->pci_cfg_acc_handle, where);
	return 0;
}

int pci_read_config_dword(struct pci_dev *pdev, int where, u32 *val)
{
	*val = pci_config_get32(pdev->pci_cfg_acc_handle, where);
	return 0;
}

int pci_write_config_byte(struct pci_dev *pdev, int where, u8 val)
{
	pci_config_put8(pdev->pci_cfg_acc_handle, where, val);
	return 0;
}

int pci_write_config_word(struct pci_dev *pdev, int where, u16 val)
{
	pci_config_put16(pdev->pci_cfg_acc_handle, where, val);
	return 0 ;
}

int pci_write_config_dword(struct pci_dev *pdev, int where, u32 val)
{
	pci_config_put32(pdev->pci_cfg_acc_handle, where, val);
	return 0;
}

/* LINTED */
u8* pci_map_rom(struct pci_dev *pdev, size_t *size)
{
	u32 base;

	base = 0xC0000;
	*size = 0x20000;

	return (u8*)drm_sun_ioremap(base, *size, DRM_MEM_UNCACHED);
}

/* LINTED */
void pci_unmap_rom(struct pci_dev *pdev, u8 *base)
{
	iounmap(base);
}

int pci_find_capability(struct pci_dev *pdev, int capid)
{
	uint8_t cap = 0;
	uint16_t caps_ptr;

	/* has capabilities list ? */
	if ((pci_config_get16(pdev->pci_cfg_acc_handle,
	    PCI_CONF_STAT) & PCI_CONF_CAP_MASK) == 0)
		return (0);

	caps_ptr = pci_config_get8(
	    pdev->pci_cfg_acc_handle, PCI_CONF_CAP_PTR);
	while (caps_ptr != PCI_CAP_NEXT_PTR_NULL) {
		cap = pci_config_get32(
		    pdev->pci_cfg_acc_handle, caps_ptr);
		if ((cap & PCI_CONF_CAPID_MASK) == capid)
			return (cap);
		caps_ptr = pci_config_get8(
		    pdev->pci_cfg_acc_handle,
		    caps_ptr + PCI_CAP_NEXT_PTR);
	}

	return (0);
}
