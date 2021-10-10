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
 * Copyright(c) 2007-2010 Intel Corporation. All rights reserved.
 */

/*
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include "igb_osdep.h"
#include "igb_api.h"


void
e1000_write_pci_cfg(struct e1000_hw *hw, uint32_t reg, uint16_t *value)
{
	pci_config_put16(OS_DEP(hw)->cfg_handle, reg, *value);
}

void
e1000_read_pci_cfg(struct e1000_hw *hw, uint32_t reg, uint16_t *value)
{
	*value =
	    pci_config_get16(OS_DEP(hw)->cfg_handle, reg);
}

/*
 * Return the 16-bit value from pci-e config space at offset reg into the pci-e
 * capability block.  Note that this refers to the pci-e capability block in
 * standard pci config space, not the block in pci-e extended config space.
 */
int32_t
e1000_read_pcie_cap_reg(struct e1000_hw *hw, uint32_t reg, uint16_t *value)
{
	uint8_t pcie_id = PCI_CAP_ID_PCI_E;
	uint16_t pcie_cap;
	int32_t status;

	/* locate the pci-e capability block */
	status = pci_lcap_locate((OS_DEP(hw))->cfg_handle, pcie_id, &pcie_cap);
	if (status == DDI_SUCCESS) {

		/* read at given offset into block */
		*value = pci_config_get16(OS_DEP(hw)->cfg_handle,
		    (pcie_cap + reg));
	}

	return (status);
}

/*
 * Write the given 16-bit value to pci-e config space at offset reg into the
 * pci-e capability block.  Note that this refers to the pci-e capability block
 * in standard pci config space, not the block in pci-e extended config space.
 */
int32_t
e1000_write_pcie_cap_reg(struct e1000_hw *hw, uint32_t reg, uint16_t *value)
{
	uint8_t pcie_id = PCI_CAP_ID_PCI_E;
	uint16_t pcie_cap;
	int32_t status;

	/* locate the pci-e capability block */
	status = pci_lcap_locate(OS_DEP(hw)->cfg_handle, pcie_id, &pcie_cap);
	if (status == DDI_SUCCESS) {

		/* write at given offset into block */
		pci_config_put16(OS_DEP(hw)->cfg_handle,
		    (off_t)(pcie_cap + reg), *value);
	}

	return (status);
}
