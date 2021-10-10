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
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/types.h>
#include <sys/cmn_err.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <sys/sunddi.h>
#include <sys/pci_cfgspace.h>
#include <sys/pci.h>
#include <sys/pcie.h>
#include <vm/seg_kmem.h>
#include <sys/mman.h>
#include <sys/cpu_module.h>
#include "nhmex.h"

extern int nhmex_max_cpu_nodes;
extern krwlock_t inhmex_mc_lock;

static ddi_acc_handle_t *dev_pci_hdl;
#define	DDI_HDL_HASH(node, dev, func) \
	(((node) * CPU_PCI_DEVS * CPU_PCI_FUNCS) + \
	((dev) * CPU_PCI_FUNCS) + (func))

void
nhmex_pci_cfg_setup(dev_info_t *dip)
{
	pci_regspec_t reg;
	int i, j, k;

	ASSERT(RW_LOCK_HELD(&inhmex_mc_lock));

	dev_pci_hdl = kmem_zalloc(sizeof (ddi_acc_handle_t) *
	    nhmex_max_cpu_nodes * CPU_PCI_DEVS * CPU_PCI_FUNCS, KM_SLEEP);
	reg.pci_phys_mid = 0;
	reg.pci_phys_low = 0;
	reg.pci_size_hi = 0;
	reg.pci_size_low = PCIE_CONF_HDR_SIZE; /* overriden in pciex */
	for (i = 0; i < nhmex_max_cpu_nodes; i++) {
		for (j = 0; j < CPU_PCI_DEVS; j++) {
			for (k = 0; k < CPU_PCI_FUNCS; k++) {
				reg.pci_phys_hi = ((SOCKET_BUS(i))
				    << PCI_REG_BUS_SHIFT) +
				    (j << PCI_REG_DEV_SHIFT) +
				    (k << PCI_REG_FUNC_SHIFT);
				if (ddi_prop_update_int_array(
				    DDI_MAJOR_T_UNKNOWN, dip, "reg",
				    (int *)&reg, sizeof (reg)/sizeof (int)) !=
				    DDI_PROP_SUCCESS)
					cmn_err(CE_WARN, "nhmex_pci_cfg_setup: "
					    "cannot create reg property");

				if (pci_config_setup(dip,
				    &dev_pci_hdl[DDI_HDL_HASH(i, j, k)]) !=
				    DDI_SUCCESS)
					cmn_err(CE_WARN, "intel_nhmex: "
					    "pci_config_setup failed");
			}
		}
	}
}

void
nhmex_pci_cfg_free()
{
	int i, j, k;
	ddi_acc_handle_t *hdl;

	ASSERT(RW_LOCK_HELD(&inhmex_mc_lock));

	for (i = 0; i < nhmex_max_cpu_nodes; i++) {
		for (j = 0; j < CPU_PCI_DEVS; j++) {
			for (k = 0; k < CPU_PCI_FUNCS; k++) {
				hdl = &dev_pci_hdl[DDI_HDL_HASH(i, j, k)];
				pci_config_teardown(hdl);
			}
		}
	}
	kmem_free(dev_pci_hdl, sizeof (ddi_acc_handle_t) *
	    nhmex_max_cpu_nodes * CPU_PCI_DEVS * CPU_PCI_FUNCS);
	dev_pci_hdl = NULL;
}

static ddi_acc_handle_t
nhmex_get_hdl(int bus, int dev, int func)
{
	ddi_acc_handle_t hdl;
	int slot;

	if (bus > SOCKET_BUS(nhmex_max_cpu_nodes) && bus <= SOCKET_BUS(0) &&
	    dev < CPU_PCI_DEVS && func < CPU_PCI_FUNCS && dev_pci_hdl) {
		slot = SOCKET_BUS(0) - bus;
		ASSERT(slot >= 0 && slot < nhmex_max_cpu_nodes);
		hdl = dev_pci_hdl[DDI_HDL_HASH(slot, dev, func)];
	} else {
		hdl = 0;
	}
	return (hdl);
}

uint8_t
nhmex_pci_getb(int bus, int dev, int func, int reg, int *interpose)
{
	ddi_acc_handle_t hdl;

	hdl = nhmex_get_hdl(bus, dev, func);
	return (cmi_pci_getb(bus, dev, func, reg, interpose, hdl));
}

uint16_t
nhmex_pci_getw(int bus, int dev, int func, int reg, int *interpose)
{
	ddi_acc_handle_t hdl;

	hdl = nhmex_get_hdl(bus, dev, func);
	return (cmi_pci_getw(bus, dev, func, reg, interpose, hdl));
}

uint32_t
nhmex_pci_getl(int bus, int dev, int func, int reg, int *interpose)
{
	ddi_acc_handle_t hdl;

	hdl = nhmex_get_hdl(bus, dev, func);
	return (cmi_pci_getl(bus, dev, func, reg, interpose, hdl));
}

void
nhmex_pci_putb(int bus, int dev, int func, int reg, uint8_t val)
{
	ddi_acc_handle_t hdl;

	hdl = nhmex_get_hdl(bus, dev, func);
	cmi_pci_putb(bus, dev, func, reg, hdl, val);
}

void
nhmex_pci_putw(int bus, int dev, int func, int reg, uint16_t val)
{
	ddi_acc_handle_t hdl;

	hdl = nhmex_get_hdl(bus, dev, func);
	cmi_pci_putw(bus, dev, func, reg, hdl, val);
}

void
nhmex_pci_putl(int bus, int dev, int func, int reg, uint32_t val)
{
	ddi_acc_handle_t hdl;

	hdl = nhmex_get_hdl(bus, dev, func);
	cmi_pci_putl(bus, dev, func, reg, hdl, val);
}
