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

#ifndef __DRM_SUN_PCI_H__
#define __DRM_SUN_PCI_H__

#include <sys/sunddi.h>
#include "drm_linux.h"
#define PCI_CONFIG_REGION_NUMS 6

struct pci_config_region {
	unsigned long start;
	unsigned long size;
};

struct pci_dev {
	struct drm_device *dev;
	ddi_acc_handle_t pci_cfg_acc_handle;

	uint16_t vendor;
	uint16_t device;
	struct pci_config_region regions[PCI_CONFIG_REGION_NUMS];
	int domain;
	int bus;
	int slot;
	int func;
	int irq;

	ddi_iblock_cookie_t intr_block;

	int msi_enabled;
	ddi_intr_handle_t *msi_handle;
	int msi_size;
	int msi_actual;
	uint_t msi_pri;
	int msi_flag;
};

#define pci_resource_start(pdev, bar) ((pdev)->regions[(bar)].start)
#define pci_resource_len(pdev, bar) ((pdev)->regions[(bar)].size)
#define pci_resource_end(pdev, bar)                    \
	((pci_resource_len((pdev), (bar)) == 0 &&      \
	pci_resource_start((pdev), (bar)) == 0) ? 0 :  \
	(pci_resource_start((pdev), (bar)) +           \
	pci_resource_len((pdev), (bar)) - 1))

extern uint8_t* pci_map_rom(struct pci_dev *pdev, size_t *size);
extern void pci_unmap_rom(struct pci_dev *pdev, uint8_t *base);
extern int pci_read_config_byte(struct pci_dev *dev, int where, u8 *val);
extern int pci_read_config_word(struct pci_dev *dev, int where, u16 *val);
extern int pci_read_config_dword(struct pci_dev *dev, int where, u32 *val);
extern int pci_write_config_byte(struct pci_dev *dev, int where, u8 val);
extern int pci_write_config_word(struct pci_dev *dev, int where, u16 val);
extern int pci_write_config_dword(struct pci_dev *dev, int where, u32 val);

extern int pci_find_capability(struct pci_dev *pdev, int capid);
extern struct pci_dev * pci_dev_create(struct drm_device *dev);
extern void pci_dev_destroy(struct pci_dev *pdev);

#endif /* __DRM_SUN_PCI_H__ */
