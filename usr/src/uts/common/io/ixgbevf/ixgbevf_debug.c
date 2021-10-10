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
 * Copyright(c) 2007-2011 Intel Corporation. All rights reserved.
 */

/*
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include "ixgbevf_sw.h"
#include "ixgbevf_debug.h"

#ifdef IXGBE_DEBUG
extern ddi_device_acc_attr_t ixgbevf_regs_acc_attr;

/*
 * Dump interrupt-related registers & structures
 */
void
ixgbevf_dump_interrupt(void *adapter, char *tag)
{
	ixgbevf_t *ixgbevf = (ixgbevf_t *)adapter;
	struct ixgbe_hw *hw = &ixgbevf->hw;
	ixgbevf_intr_vector_t *vect;
	uint32_t ivar, hw_index;
	int i, j;

	/*
	 * interrupt control registers
	 */
	ixgbevf_log(ixgbevf, "interrupt: %s\n", tag);
	ixgbevf_log(ixgbevf, "..vteims: 0x%x\n",
	    IXGBE_READ_REG(hw, IXGBE_VTEIMS));
	ixgbevf_log(ixgbevf, "..vteimc: 0x%x\n",
	    IXGBE_READ_REG(hw, IXGBE_VTEIMC));
	ixgbevf_log(ixgbevf, "..vteiac: 0x%x\n",
	    IXGBE_READ_REG(hw, IXGBE_VTEIAC));
	ixgbevf_log(ixgbevf, "..vteiam: 0x%x\n",
	    IXGBE_READ_REG(hw, IXGBE_VTEIAM));
	ixgbevf_log(ixgbevf, "vteims_mask: 0x%x\n", ixgbevf->eims);

	/* ivar: interrupt vector allocation registers */
	for (i = 0; i < IXGBE_IVAR_REG_NUM; i++) {
		if (ivar = IXGBE_READ_REG(hw, IXGBE_VTIVAR(i))) {
			ixgbevf_log(ixgbevf, "vtivar[%d]: 0x%x\n", i, ivar);
		}
	}

	/* each allocated vector */
	for (i = 0; i < ixgbevf->intr_cnt; i++) {
	vect =  &ixgbevf->vect_map[i];
	ixgbevf_log(ixgbevf,
	    "vector %d  rx rings %d  tx rings %d  eitr: 0x%x\n",
	    i, vect->rxr_cnt, vect->txr_cnt,
	    IXGBE_READ_REG(hw, IXGBE_VTEITR(i)));

	/* for each rx ring bit set */
	j = bt_getlowbit(vect->rx_map, 0, (ixgbevf->num_rx_rings - 1));
	while (j >= 0) {
		hw_index = ixgbevf->rx_rings[j].hw_index;
		ixgbevf_log(ixgbevf,
		    "rx %d  vtivar %d  vtrxdctl: 0x%x  vtsrrctl: 0x%x\n",
		    hw_index, IXGBE_IVAR_RX_QUEUE(hw_index),
		    IXGBE_READ_REG(hw, IXGBE_VFRXDCTL(hw_index)),
		    IXGBE_READ_REG(hw, IXGBE_VFSRRCTL(hw_index)));
		j = bt_getlowbit(vect->rx_map, (j + 1),
		    (ixgbevf->num_rx_rings - 1));
	}

	/* for each tx ring bit set */
	j = bt_getlowbit(vect->tx_map, 0, (ixgbevf->num_tx_rings - 1));
	while (j >= 0) {
		ixgbevf_log(ixgbevf, "tx %d  vtivar %d  vttxdctl: 0x%x\n",
		    j, IXGBE_IVAR_TX_QUEUE(j),
		    IXGBE_READ_REG(hw, IXGBE_VFTXDCTL(j)));
		j = bt_getlowbit(vect->tx_map, (j + 1),
		    (ixgbevf->num_tx_rings - 1));
	}
	}
}

/*
 * Dump an ethernet address
 */
void
ixgbevf_dump_addr(void *adapter, char *tag, const uint8_t *addr)
{
	ixgbevf_t *ixgbevf = (ixgbevf_t *)adapter;
	char		form[25];

	(void) sprintf(form, "%02x:%02x:%02x:%02x:%02x:%02x",
	    *addr, *(addr + 1), *(addr + 2),
	    *(addr + 3), *(addr + 4), *(addr + 5));

	ixgbevf_log(ixgbevf, "%s %s\n", tag, form);
}

void
ixgbevf_pci_dump(void *arg)
{
	ixgbevf_t *ixgbevf = (ixgbevf_t *)arg;
	ddi_acc_handle_t handle;
	uint8_t cap_ptr;
	uint8_t next_ptr;
	uint32_t msix_bar;
	uint32_t msix_ctrl;
	uint32_t msix_tbl_sz;
	uint32_t tbl_offset;
	uint32_t tbl_bir;
	uint32_t pba_offset;
	uint32_t pba_bir;
	off_t offset;
	off_t mem_size;
	uintptr_t base;
	ddi_acc_handle_t acc_hdl;
	int i;

	handle = ixgbevf->osdep.cfg_handle;

	ixgbevf_log(ixgbevf, "Begin dump PCI config space");

	ixgbevf_log(ixgbevf,
	    "PCI_CONF_VENID:\t0x%x\n",
	    pci_config_get16(handle, PCI_CONF_VENID));
	ixgbevf_log(ixgbevf,
	    "PCI_CONF_DEVID:\t0x%x\n",
	    pci_config_get16(handle, PCI_CONF_DEVID));
	ixgbevf_log(ixgbevf,
	    "PCI_CONF_COMMAND:\t0x%x\n",
	    pci_config_get16(handle, PCI_CONF_COMM));
	ixgbevf_log(ixgbevf,
	    "PCI_CONF_STATUS:\t0x%x\n",
	    pci_config_get16(handle, PCI_CONF_STAT));
	ixgbevf_log(ixgbevf,
	    "PCI_CONF_REVID:\t0x%x\n",
	    pci_config_get8(handle, PCI_CONF_REVID));
	ixgbevf_log(ixgbevf,
	    "PCI_CONF_PROG_CLASS:\t0x%x\n",
	    pci_config_get8(handle, PCI_CONF_PROGCLASS));
	ixgbevf_log(ixgbevf,
	    "PCI_CONF_SUB_CLASS:\t0x%x\n",
	    pci_config_get8(handle, PCI_CONF_SUBCLASS));
	ixgbevf_log(ixgbevf,
	    "PCI_CONF_BAS_CLASS:\t0x%x\n",
	    pci_config_get8(handle, PCI_CONF_BASCLASS));
	ixgbevf_log(ixgbevf,
	    "PCI_CONF_CACHE_LINESZ:\t0x%x\n",
	    pci_config_get8(handle, PCI_CONF_CACHE_LINESZ));
	ixgbevf_log(ixgbevf,
	    "PCI_CONF_LATENCY_TIMER:\t0x%x\n",
	    pci_config_get8(handle, PCI_CONF_LATENCY_TIMER));
	ixgbevf_log(ixgbevf,
	    "PCI_CONF_HEADER_TYPE:\t0x%x\n",
	    pci_config_get8(handle, PCI_CONF_HEADER));
	ixgbevf_log(ixgbevf,
	    "PCI_CONF_BIST:\t0x%x\n",
	    pci_config_get8(handle, PCI_CONF_BIST));
	ixgbevf_log(ixgbevf,
	    "PCI_CONF_BASE0:\t0x%x\n",
	    pci_config_get32(handle, PCI_CONF_BASE0));
	ixgbevf_log(ixgbevf,
	    "PCI_CONF_BASE1:\t0x%x\n",
	    pci_config_get32(handle, PCI_CONF_BASE1));
	ixgbevf_log(ixgbevf,
	    "PCI_CONF_BASE2:\t0x%x\n",
	    pci_config_get32(handle, PCI_CONF_BASE2));

	/* MSI-X BAR */
	msix_bar = pci_config_get32(handle, PCI_CONF_BASE3);
	ixgbevf_log(ixgbevf,
	    "PCI_CONF_BASE3:\t0x%x\n", msix_bar);

	ixgbevf_log(ixgbevf,
	    "PCI_CONF_BASE4:\t0x%x\n",
	    pci_config_get32(handle, PCI_CONF_BASE4));
	ixgbevf_log(ixgbevf,
	    "PCI_CONF_BASE5:\t0x%x\n",
	    pci_config_get32(handle, PCI_CONF_BASE5));
	ixgbevf_log(ixgbevf,
	    "PCI_CONF_CIS:\t0x%x\n",
	    pci_config_get32(handle, PCI_CONF_CIS));
	ixgbevf_log(ixgbevf,
	    "PCI_CONF_SUBVENID:\t0x%x\n",
	    pci_config_get16(handle, PCI_CONF_SUBVENID));
	ixgbevf_log(ixgbevf,
	    "PCI_CONF_SUBSYSID:\t0x%x\n",
	    pci_config_get16(handle, PCI_CONF_SUBSYSID));
	ixgbevf_log(ixgbevf,
	    "PCI_CONF_ROM:\t0x%x\n",
	    pci_config_get32(handle, PCI_CONF_ROM));

	cap_ptr = pci_config_get8(handle, PCI_CONF_CAP_PTR);

	ixgbevf_log(ixgbevf,
	    "PCI_CONF_CAP_PTR:\t0x%x\n", cap_ptr);
	ixgbevf_log(ixgbevf,
	    "PCI_CONF_ILINE:\t0x%x\n",
	    pci_config_get8(handle, PCI_CONF_ILINE));
	ixgbevf_log(ixgbevf,
	    "PCI_CONF_IPIN:\t0x%x\n",
	    pci_config_get8(handle, PCI_CONF_IPIN));
	ixgbevf_log(ixgbevf,
	    "PCI_CONF_MIN_G:\t0x%x\n",
	    pci_config_get8(handle, PCI_CONF_MIN_G));
	ixgbevf_log(ixgbevf,
	    "PCI_CONF_MAX_L:\t0x%x\n",
	    pci_config_get8(handle, PCI_CONF_MAX_L));

	/* Power Management */
	offset = cap_ptr;

	ixgbevf_log(ixgbevf,
	    "PCI_PM_CAP_ID:\t0x%x\n",
	    pci_config_get8(handle, offset));

	next_ptr = pci_config_get8(handle, offset + 1);

	ixgbevf_log(ixgbevf,
	    "PCI_PM_NEXT_PTR:\t0x%x\n", next_ptr);
	ixgbevf_log(ixgbevf,
	    "PCI_PM_CAP:\t0x%x\n",
	    pci_config_get16(handle, offset + PCI_PMCAP));
	ixgbevf_log(ixgbevf,
	    "PCI_PM_CSR:\t0x%x\n",
	    pci_config_get16(handle, offset + PCI_PMCSR));
	ixgbevf_log(ixgbevf,
	    "PCI_PM_CSR_BSE:\t0x%x\n",
	    pci_config_get8(handle, offset + PCI_PMCSR_BSE));
	ixgbevf_log(ixgbevf,
	    "PCI_PM_DATA:\t0x%x\n",
	    pci_config_get8(handle, offset + PCI_PMDATA));

	/* MSI Configuration */
	offset = next_ptr;

	ixgbevf_log(ixgbevf,
	    "PCI_MSI_CAP_ID:\t0x%x\n",
	    pci_config_get8(handle, offset));

	next_ptr = pci_config_get8(handle, offset + 1);

	ixgbevf_log(ixgbevf,
	    "PCI_MSI_NEXT_PTR:\t0x%x\n", next_ptr);
	ixgbevf_log(ixgbevf,
	    "PCI_MSI_CTRL:\t0x%x\n",
	    pci_config_get16(handle, offset + PCI_MSI_CTRL));
	ixgbevf_log(ixgbevf,
	    "PCI_MSI_ADDR:\t0x%x\n",
	    pci_config_get32(handle, offset + PCI_MSI_ADDR_OFFSET));
	ixgbevf_log(ixgbevf,
	    "PCI_MSI_ADDR_HI:\t0x%x\n",
	    pci_config_get32(handle, offset + 0x8));
	ixgbevf_log(ixgbevf,
	    "PCI_MSI_DATA:\t0x%x\n",
	    pci_config_get16(handle, offset + 0xC));

	/* MSI-X Configuration */
	offset = next_ptr;

	ixgbevf_log(ixgbevf,
	    "PCI_MSIX_CAP_ID:\t0x%x\n",
	    pci_config_get8(handle, offset));

	next_ptr = pci_config_get8(handle, offset + 1);
	ixgbevf_log(ixgbevf,
	    "PCI_MSIX_NEXT_PTR:\t0x%x\n", next_ptr);

	msix_ctrl = pci_config_get16(handle, offset + PCI_MSIX_CTRL);
	msix_tbl_sz = msix_ctrl & 0x7ff;
	ixgbevf_log(ixgbevf,
	    "PCI_MSIX_CTRL:\t0x%x\n", msix_ctrl);

	tbl_offset = pci_config_get32(handle, offset + PCI_MSIX_TBL_OFFSET);
	tbl_bir = tbl_offset & PCI_MSIX_TBL_BIR_MASK;
	tbl_offset = tbl_offset & ~PCI_MSIX_TBL_BIR_MASK;
	ixgbevf_log(ixgbevf,
	    "PCI_MSIX_TBL_OFFSET:\t0x%x\n", tbl_offset);
	ixgbevf_log(ixgbevf,
	    "PCI_MSIX_TBL_BIR:\t0x%x\n", tbl_bir);

	pba_offset = pci_config_get32(handle, offset + PCI_MSIX_PBA_OFFSET);
	pba_bir = pba_offset & PCI_MSIX_PBA_BIR_MASK;
	pba_offset = pba_offset & ~PCI_MSIX_PBA_BIR_MASK;
	ixgbevf_log(ixgbevf,
	    "PCI_MSIX_PBA_OFFSET:\t0x%x\n", pba_offset);
	ixgbevf_log(ixgbevf,
	    "PCI_MSIX_PBA_BIR:\t0x%x\n", pba_bir);

	/* PCI Express Configuration */
	offset = next_ptr;

	ixgbevf_log(ixgbevf,
	    "PCIE_CAP_ID:\t0x%x\n",
	    pci_config_get8(handle, offset + PCIE_CAP_ID));

	next_ptr = pci_config_get8(handle, offset + PCIE_CAP_NEXT_PTR);

	ixgbevf_log(ixgbevf,
	    "PCIE_CAP_NEXT_PTR:\t0x%x\n", next_ptr);
	ixgbevf_log(ixgbevf,
	    "PCIE_PCIECAP:\t0x%x\n",
	    pci_config_get16(handle, offset + PCIE_PCIECAP));
	ixgbevf_log(ixgbevf,
	    "PCIE_DEVCAP:\t0x%x\n",
	    pci_config_get32(handle, offset + PCIE_DEVCAP));
	ixgbevf_log(ixgbevf,
	    "PCIE_DEVCTL:\t0x%x\n",
	    pci_config_get16(handle, offset + PCIE_DEVCTL));
	ixgbevf_log(ixgbevf,
	    "PCIE_DEVSTS:\t0x%x\n",
	    pci_config_get16(handle, offset + PCIE_DEVSTS));
	ixgbevf_log(ixgbevf,
	    "PCIE_LINKCAP:\t0x%x\n",
	    pci_config_get32(handle, offset + PCIE_LINKCAP));
	ixgbevf_log(ixgbevf,
	    "PCIE_LINKCTL:\t0x%x\n",
	    pci_config_get16(handle, offset + PCIE_LINKCTL));
	ixgbevf_log(ixgbevf,
	    "PCIE_LINKSTS:\t0x%x\n",
	    pci_config_get16(handle, offset + PCIE_LINKSTS));

	/* MSI-X Memory Space */
	if (ddi_dev_regsize(ixgbevf->dip, 4, &mem_size) != DDI_SUCCESS) {
		ixgbevf_log(ixgbevf, "ddi_dev_regsize() failed");
		return;
	}

	if ((ddi_regs_map_setup(ixgbevf->dip, 4, (caddr_t *)&base, 0, mem_size,
	    &ixgbevf_regs_acc_attr, &acc_hdl)) != DDI_SUCCESS) {
		ixgbevf_log(ixgbevf, "ddi_regs_map_setup() failed");
		return;
	}

	ixgbevf_log(ixgbevf, "MSI-X Memory Space: (mem_size = %d, base = %x)",
	    mem_size, base);

	for (i = 0; i <= msix_tbl_sz; i++) {
		ixgbevf_log(ixgbevf, "MSI-X Table Entry(%d):", i);
		ixgbevf_log(ixgbevf, "lo_addr:\t%x",
		    ddi_get32(acc_hdl,
		    (uint32_t *)(base + tbl_offset + (i * 16))));
		ixgbevf_log(ixgbevf, "up_addr:\t%x",
		    ddi_get32(acc_hdl,
		    (uint32_t *)(base + tbl_offset + (i * 16) + 4)));
		ixgbevf_log(ixgbevf, "msg_data:\t%x",
		    ddi_get32(acc_hdl,
		    (uint32_t *)(base + tbl_offset + (i * 16) + 8)));
		ixgbevf_log(ixgbevf, "vct_ctrl:\t%x",
		    ddi_get32(acc_hdl,
		    (uint32_t *)(base + tbl_offset + (i * 16) + 12)));
	}

	ixgbevf_log(ixgbevf, "MSI-X Pending Bits:\t%x",
	    ddi_get32(acc_hdl, (uint32_t *)(base + pba_offset)));

	ddi_regs_map_free(&acc_hdl);
}

/*
 * Dump registers
 */
void
ixgbevf_dump_regs(void *adapter)
{
	ixgbevf_t *ixgbevf = (ixgbevf_t *)adapter;
	uint32_t reg_val, hw_index;
	struct ixgbe_hw *hw = &ixgbevf->hw;
	int i;
	DEBUGFUNC("ixgbevf_dump_regs");

	/* Dump basic's like CTRL, STATUS, CTRL_EXT. */
	ixgbevf_log(ixgbevf, "Basic IXGBE registers..");
	reg_val = IXGBE_READ_REG(hw, IXGBE_VFCTRL);
	ixgbevf_log(ixgbevf, "\tVFCTRL=%x\n", reg_val);
	reg_val = IXGBE_READ_REG(hw, IXGBE_VFSTATUS);
	ixgbevf_log(ixgbevf, "\tVFSTATUS=%x\n", reg_val);
	reg_val = IXGBE_READ_REG(hw, IXGBE_VFLINKS);
	ixgbevf_log(ixgbevf, "\tVFLINKS=%x\n", reg_val);

	/* Misc Interrupt regs */
	ixgbevf_log(ixgbevf, "Some IXGBE interrupt registers..");

	reg_val = IXGBE_READ_REG(hw, IXGBE_VTIVAR(0));
	ixgbevf_log(ixgbevf, "\tVTIVAR(0)=%x\n", reg_val);

	reg_val = IXGBE_READ_REG(hw, IXGBE_VTIVAR_MISC);
	ixgbevf_log(ixgbevf, "\tVTIVAR_MISC=%x\n", reg_val);

	/* Dump RX related reg's */
	ixgbevf_log(ixgbevf, "Receive registers...");
	for (i = 0; i < ixgbevf->num_rx_rings; i++) {
		hw_index = ixgbevf->rx_rings[i].hw_index;
		reg_val = IXGBE_READ_REG(hw, IXGBE_RXDCTL(hw_index));
		ixgbevf_log(ixgbevf, "\tVFRXDCTL(%d)=%x\n", hw_index, reg_val);
		reg_val = IXGBE_READ_REG(hw, IXGBE_SRRCTL(hw_index));
		ixgbevf_log(ixgbevf, "\tVFSRRCTL(%d)=%x\n", hw_index, reg_val);
	}

	/* Dump TX related regs */
	ixgbevf_log(ixgbevf, "Some transmit registers..");
	for (i = 0; i < ixgbevf->num_tx_rings; i++) {
		reg_val = IXGBE_READ_REG(hw, IXGBE_VFTXDCTL(i));
		ixgbevf_log(ixgbevf, "\tVFTXDCTL(%d)=%x\n", i, reg_val);

		reg_val = IXGBE_READ_REG(hw, IXGBE_VFTDBAL(i));
		ixgbevf_log(ixgbevf, "\tVFTDBAL(%d)=%x\n", i, reg_val);
		reg_val = IXGBE_READ_REG(hw, IXGBE_VFTDBAH(i));
		ixgbevf_log(ixgbevf, "\tVFTDBAH(%d)=%x\n", i, reg_val);

		reg_val = IXGBE_READ_REG(hw, IXGBE_VFTDWBAL(i));
		ixgbevf_log(ixgbevf, "\tVFTDWBAL(%d)=%x\n", i, reg_val);
		reg_val = IXGBE_READ_REG(hw, IXGBE_VFTDWBAH(i));
		ixgbevf_log(ixgbevf, "\tVFTDWBAH(%d)=%x\n", i, reg_val);
	}
}

#endif
