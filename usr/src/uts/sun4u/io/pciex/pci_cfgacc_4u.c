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
#include <sys/promif.h>
#include <sys/pci.h>
#include <sys/sysmacros.h>
#include <sys/pcie_impl.h>
#include <sys/machsystm.h>
#include <sys/byteorder.h>
#include <sys/pci_cfgacc.h>

#define	PCI_CFG_SPACE		(PCI_REG_ADDR_G(PCI_ADDR_CONFIG))
#define	PCIE_CFG_SPACE_SIZE	(PCI_CONF_HDR_SIZE << 4)

/* RC BDF Shift in a Phyiscal Address */
#define	RC_PA_BDF_SHIFT			12
#define	RC_BDF_TO_CFGADDR(bdf, offset) (((bdf) << RC_PA_BDF_SHIFT) + (offset))

static boolean_t
pci_cfgacc_valid(pci_cfgacc_req_t *req)
{
	int sz = req->size;
	pcie_bus_t	*bus_p;
	uint64_t	devhdl;

	bus_p = PCIE_DIP2DOWNBUS(req->rcdip);
	if (bus_p == NULL)
		return (B_FALSE);

	devhdl = bus_p->bus_cfgacc_base;
	if (devhdl == INVALID_CFGACC_BASE)
		return (B_FALSE);
	if (IS_P2ALIGNED(req->offset, sz)		&&
	    (req->offset + sz - 1 < PCIE_CFG_SPACE_SIZE)	&&
	    ((sz & 0xf) && ISP2(sz)))
		return (B_TRUE);

	cmn_err(CE_WARN, "illegal PCI request: offset = %x, size = %d",
	    req->offset, sz);
	return (B_FALSE);
}

/*
 * Unprotected raw reads/writes of fabric device's config space.
 */
static uint64_t
pci_cfgacc_get(dev_info_t *dip, uint16_t bdf, uint16_t offset, uint8_t size)
{
	pcie_bus_t	*bus_p;
	uint64_t	base_addr;
	uint64_t	val;

	bus_p = PCIE_DIP2DOWNBUS(dip);
	ASSERT(bus_p != NULL);

	base_addr = bus_p->bus_cfgacc_base;
	base_addr += RC_BDF_TO_CFGADDR(bdf, offset);

	switch (size) {
	case 1:
		val = ldbphysio(base_addr);
		break;
	case 2:
		val = ldhphysio(base_addr);
		break;
	case 4:
		val = ldphysio(base_addr);
		break;
	default:
		return ((uint64_t)-1);
	}

	return (LE_64(val));
}

static void
pci_cfgacc_set(dev_info_t *dip, uint16_t bdf, uint16_t offset, uint8_t size,
    uint64_t val)
{
	pcie_bus_t	*bus_p;
	uint64_t	base_addr;

	bus_p = PCIE_DIP2DOWNBUS(dip);
	ASSERT(bus_p != NULL);

	base_addr = bus_p->bus_cfgacc_base;
	base_addr += RC_BDF_TO_CFGADDR(bdf, offset);

	switch (size) {
	case 1:
		stbphysio(base_addr, LE_8(val));
		break;
	case 2:
		sthphysio(base_addr, LE_16(val));
		break;
	case 4:
		stphysio(base_addr, LE_32(val));
		break;
	default:
		break;
	}
}

void
pci_cfgacc_acc(pci_cfgacc_req_t *req)
{
	if (!req->write)
		VAL64(req) = (uint64_t)-1;

	if (!pci_cfgacc_valid(req))
		return;

	if (req->write) {
		pci_cfgacc_set(req->rcdip, req->bdf, req->offset,
		    req->size, VAL64(req));
	} else {
		VAL64(req) = pci_cfgacc_get(req->rcdip, req->bdf,
		    req->offset, req->size);
	}
}
