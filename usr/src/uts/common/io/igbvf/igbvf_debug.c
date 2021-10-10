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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include "igbvf_sw.h"
#include "igbvf_debug.h"

#ifdef IGBVF_DEBUG
static uint8_t igbvf_dump_pci_msix(igbvf_t *, uint8_t);
static uint8_t igbvf_dump_pci_pcie(igbvf_t *, uint8_t);

/*
 * read a register - debugging replacement for macro of the same name in
 * e1000_osdep.h
 */
uint32_t
igbvf_read_reg(void *arg, uint32_t reg)
{
	struct e1000_hw *hw = (struct e1000_hw *)arg;
	igbvf_t	*igbvf = (igbvf_t *)(OS_DEP(hw))->igbvf;
	uint32_t result;

	result = ddi_get32((OS_DEP(hw))->reg_handle,
	    (uint32_t *)((uintptr_t)(hw)->hw_addr + reg));

	if (igbvf->prt_reg)
		igbvf_log(igbvf,
		    "E1000_READ_REG: reg 0x%x, value 0x%x", reg, result);

	return (result);
}

/*
 * read a register, array style - debugging replacement for macro of the same
 * name in e1000_osdep.h
 */
uint32_t
igbvf_read_reg_array(void *arg, uint32_t reg, uint32_t offset)
{
	struct e1000_hw *hw = (struct e1000_hw *)arg;
	igbvf_t	*igbvf = (igbvf_t *)(OS_DEP(hw))->igbvf;
	uint32_t result;

	result = ddi_get32((OS_DEP(hw))->reg_handle,
	    (uint32_t *)((uintptr_t)(hw)->hw_addr + reg + ((offset) << 2)));

	if (igbvf->prt_reg)
		igbvf_log(igbvf,
		    "E1000_READ_REG_ARRAY: reg 0x%x, offset 0x%x, value 0x%x",
		    reg, offset, result);

	return (result);
}

/*
 * write a register - debugging replacement for macro of the same name in
 * e1000_osdep.h
 */
void
igbvf_write_reg(void *arg, uint32_t reg, uint32_t value)
{
	struct e1000_hw *hw = (struct e1000_hw *)arg;
	igbvf_t	*igbvf = (igbvf_t *)(OS_DEP(hw))->igbvf;

	if (igbvf->prt_reg)
		igbvf_log(igbvf, "E1000_WRITE_REG offset 0x%x value 0x%x",
		    reg, value);

	ddi_put32((OS_DEP(hw))->reg_handle,
	    (uint32_t *)((uintptr_t)(hw)->hw_addr + reg), (value));
}

/*
 * write a register, array style - debugging replacement for macro of the same
 * name in e1000_osdep.h
 */
void
igbvf_write_reg_array(void *arg,
    uint32_t reg, uint32_t offset, uint32_t value)
{
	struct e1000_hw *hw = (struct e1000_hw *)arg;
	igbvf_t	*igbvf = (igbvf_t *)(OS_DEP(hw))->igbvf;

	if (igbvf->prt_reg)
		igbvf_log(igbvf,
		    "E1000_WRITE_REG_ARRAY: reg 0x%x, offset 0x%x, value 0x%x",
		    reg, offset, value);

	ddi_put32((OS_DEP(hw))->reg_handle,
	    (uint32_t *)((uintptr_t)(hw)->hw_addr + reg + ((offset) << 2)),
	    (value));
}

void
igbvf_dump_pci(void *arg)
{
	igbvf_t *igbvf = (igbvf_t *)arg;
	ddi_acc_handle_t handle;
	uint8_t cap_ptr;
	uint8_t next_ptr;

	handle = igbvf->osdep.cfg_handle;

	igbvf_log(igbvf, "-------- PCIe Config Space --------");

	igbvf_log(igbvf,
	    "PCI_CONF_VENID:\t0x%x",
	    pci_config_get16(handle, PCI_CONF_VENID));
	igbvf_log(igbvf,
	    "PCI_CONF_DEVID:\t0x%x",
	    pci_config_get16(handle, PCI_CONF_DEVID));
	igbvf_log(igbvf,
	    "PCI_CONF_COMMAND:\t0x%x",
	    pci_config_get16(handle, PCI_CONF_COMM));
	igbvf_log(igbvf,
	    "PCI_CONF_STATUS:\t0x%x",
	    pci_config_get16(handle, PCI_CONF_STAT));
	igbvf_log(igbvf,
	    "PCI_CONF_REVID:\t0x%x",
	    pci_config_get8(handle, PCI_CONF_REVID));
	igbvf_log(igbvf,
	    "PCI_CONF_PROG_CLASS:\t0x%x",
	    pci_config_get8(handle, PCI_CONF_PROGCLASS));
	igbvf_log(igbvf,
	    "PCI_CONF_SUB_CLASS:\t0x%x",
	    pci_config_get8(handle, PCI_CONF_SUBCLASS));
	igbvf_log(igbvf,
	    "PCI_CONF_BAS_CLASS:\t0x%x",
	    pci_config_get8(handle, PCI_CONF_BASCLASS));
	igbvf_log(igbvf,
	    "PCI_CONF_CACHE_LINESZ:\t0x%x",
	    pci_config_get8(handle, PCI_CONF_CACHE_LINESZ));
	igbvf_log(igbvf,
	    "PCI_CONF_LATENCY_TIMER:\t0x%x",
	    pci_config_get8(handle, PCI_CONF_LATENCY_TIMER));
	igbvf_log(igbvf,
	    "PCI_CONF_HEADER_TYPE:\t0x%x",
	    pci_config_get8(handle, PCI_CONF_HEADER));
	igbvf_log(igbvf,
	    "PCI_CONF_BIST:\t0x%x",
	    pci_config_get8(handle, PCI_CONF_BIST));
	igbvf_log(igbvf,
	    "PCI_CONF_BASE0:\t0x%x",
	    pci_config_get32(handle, PCI_CONF_BASE0));
	igbvf_log(igbvf,
	    "PCI_CONF_BASE1:\t0x%x",
	    pci_config_get32(handle, PCI_CONF_BASE1));
	igbvf_log(igbvf,
	    "PCI_CONF_BASE2:\t0x%x",
	    pci_config_get32(handle, PCI_CONF_BASE2));
	igbvf_log(igbvf,
	    "PCI_CONF_BASE3:\t0x%x",
	    pci_config_get32(handle, PCI_CONF_BASE3));
	igbvf_log(igbvf,
	    "PCI_CONF_BASE4:\t0x%x",
	    pci_config_get32(handle, PCI_CONF_BASE4));
	igbvf_log(igbvf,
	    "PCI_CONF_BASE5:\t0x%x",
	    pci_config_get32(handle, PCI_CONF_BASE5));

	igbvf_log(igbvf,
	    "PCI_CONF_CIS:\t0x%x",
	    pci_config_get32(handle, PCI_CONF_CIS));
	igbvf_log(igbvf,
	    "PCI_CONF_SUBVENID:\t0x%x",
	    pci_config_get16(handle, PCI_CONF_SUBVENID));
	igbvf_log(igbvf,
	    "PCI_CONF_SUBSYSID:\t0x%x",
	    pci_config_get16(handle, PCI_CONF_SUBSYSID));
	igbvf_log(igbvf,
	    "PCI_CONF_ROM:\t0x%x",
	    pci_config_get32(handle, PCI_CONF_ROM));

	cap_ptr = pci_config_get8(handle, PCI_CONF_CAP_PTR);

	igbvf_log(igbvf,
	    "PCI_CONF_CAP_PTR:\t0x%x", cap_ptr);
	igbvf_log(igbvf,
	    "PCI_CONF_ILINE:\t0x%x",
	    pci_config_get8(handle, PCI_CONF_ILINE));
	igbvf_log(igbvf,
	    "PCI_CONF_IPIN:\t0x%x",
	    pci_config_get8(handle, PCI_CONF_IPIN));
	igbvf_log(igbvf,
	    "PCI_CONF_MIN_G:\t0x%x",
	    pci_config_get8(handle, PCI_CONF_MIN_G));
	igbvf_log(igbvf,
	    "PCI_CONF_MAX_L:\t0x%x",
	    pci_config_get8(handle, PCI_CONF_MAX_L));

	/* MSI-X Configuration */
	next_ptr = igbvf_dump_pci_msix(igbvf, cap_ptr);

	/* PCI Express Configuration */
	next_ptr = igbvf_dump_pci_pcie(igbvf, next_ptr);
}

extern ddi_device_acc_attr_t igbvf_regs_acc_attr;

static uint8_t
igbvf_dump_pci_msix(igbvf_t *igbvf, uint8_t offset)
{
	ddi_acc_handle_t handle;
	uint8_t next_ptr;
	uint32_t msix_ctrl;
	uint32_t msix_tbl_sz;
	uint32_t tbl_offset;
	uint32_t tbl_bir;
	uint32_t pba_offset;
	uint32_t pba_bir;
	ddi_acc_handle_t acc_hdl;
	off_t mem_size;
	uintptr_t base;
	int ret;
	int i;

	handle = igbvf->osdep.cfg_handle;

	igbvf_log(igbvf, "-------- MSI-X Capability --------");
	igbvf_log(igbvf,
	    "PCI_MSIX_CAP_ID:\t0x%x",
	    pci_config_get8(handle, offset));

	next_ptr = pci_config_get8(handle, offset + 1);
	igbvf_log(igbvf,
	    "PCI_MSIX_NEXT_PTR:\t0x%x", next_ptr);

	msix_ctrl = pci_config_get16(handle, offset + PCI_MSIX_CTRL);
	msix_tbl_sz = msix_ctrl & 0x7ff;
	igbvf_log(igbvf,
	    "PCI_MSIX_CTRL:\t0x%x", msix_ctrl);

	tbl_offset = pci_config_get32(handle, offset + PCI_MSIX_TBL_OFFSET);
	tbl_bir = tbl_offset & PCI_MSIX_TBL_BIR_MASK;
	tbl_offset = tbl_offset & ~PCI_MSIX_TBL_BIR_MASK;
	igbvf_log(igbvf,
	    "PCI_MSIX_TBL_OFFSET:\t0x%x", tbl_offset);
	igbvf_log(igbvf,
	    "PCI_MSIX_TBL_BIR:\t0x%x", tbl_bir);

	pba_offset = pci_config_get32(handle, offset + PCI_MSIX_PBA_OFFSET);
	pba_bir = pba_offset & PCI_MSIX_PBA_BIR_MASK;
	pba_offset = pba_offset & ~PCI_MSIX_PBA_BIR_MASK;
	igbvf_log(igbvf,
	    "PCI_MSIX_PBA_OFFSET:\t0x%x", pba_offset);
	igbvf_log(igbvf,
	    "PCI_MSIX_PBA_BIR:\t0x%x", pba_bir);

	if ((ret = ddi_dev_regsize(igbvf->dip, IGBVF_ADAPTER_MSIXTAB,
	    &mem_size)) != DDI_SUCCESS) {
		igbvf_log(igbvf, "ddi_dev_regsize() failed: 0x%x", ret);
		return (next_ptr);
	}

	if ((ret = ddi_regs_map_setup(igbvf->dip, IGBVF_ADAPTER_MSIXTAB,
	    (caddr_t *)&base, 0, mem_size, &igbvf_regs_acc_attr, &acc_hdl)) !=
	    DDI_SUCCESS) {
		igbvf_log(igbvf, "ddi_regs_map_setup() failed: 0x%x", ret);
		return (next_ptr);
	}

	igbvf_log(igbvf, "-- MSI-X Memory Space (mem_size = %d, base = %x) --",
	    mem_size, base);

	for (i = 0; i <= msix_tbl_sz; i++) {
		igbvf_log(igbvf, "MSI-X Table Entry(%d):", i);
		igbvf_log(igbvf, "lo_addr:\t%x",
		    ddi_get32(acc_hdl,
		    (uint32_t *)(base + tbl_offset + (i * 16))));
		igbvf_log(igbvf, "up_addr:\t%x",
		    ddi_get32(acc_hdl,
		    (uint32_t *)(base + tbl_offset + (i * 16) + 4)));
		igbvf_log(igbvf, "msg_data:\t%x",
		    ddi_get32(acc_hdl,
		    (uint32_t *)(base + tbl_offset + (i * 16) + 8)));
		igbvf_log(igbvf, "vct_ctrl:\t%x",
		    ddi_get32(acc_hdl,
		    (uint32_t *)(base + tbl_offset + (i * 16) + 12)));
	}

	igbvf_log(igbvf, "MSI-X Pending Bits:\t%x",
	    ddi_get32(acc_hdl, (uint32_t *)(base + pba_offset)));

	ddi_regs_map_free(&acc_hdl);

	return (next_ptr);
}

static uint8_t
igbvf_dump_pci_pcie(igbvf_t *igbvf, uint8_t offset)
{
	ddi_acc_handle_t handle;
	uint8_t next_ptr;

	handle = igbvf->osdep.cfg_handle;

	igbvf_log(igbvf, "-------- PCIe Capability --------");
	igbvf_log(igbvf,
	    "PCIE_CAP_ID:\t0x%x",
	    pci_config_get8(handle, offset + PCIE_CAP_ID));

	next_ptr = pci_config_get8(handle, offset + PCIE_CAP_NEXT_PTR);

	igbvf_log(igbvf,
	    "PCIE_CAP_NEXT_PTR:\t0x%x", next_ptr);
	igbvf_log(igbvf,
	    "PCIE_PCIECAP:\t0x%x",
	    pci_config_get16(handle, offset + PCIE_PCIECAP));
	igbvf_log(igbvf,
	    "PCIE_DEVCAP:\t0x%x",
	    pci_config_get32(handle, offset + PCIE_DEVCAP));
	igbvf_log(igbvf,
	    "PCIE_DEVCTL:\t0x%x",
	    pci_config_get16(handle, offset + PCIE_DEVCTL));
	igbvf_log(igbvf,
	    "PCIE_DEVSTS:\t0x%x",
	    pci_config_get16(handle, offset + PCIE_DEVSTS));
	igbvf_log(igbvf,
	    "PCIE_LINKCAP:\t0x%x",
	    pci_config_get32(handle, offset + PCIE_LINKCAP));
	igbvf_log(igbvf,
	    "PCIE_LINKCTL:\t0x%x",
	    pci_config_get16(handle, offset + PCIE_LINKCTL));
	igbvf_log(igbvf,
	    "PCIE_LINKSTS:\t0x%x",
	    pci_config_get16(handle, offset + PCIE_LINKSTS));

	return (next_ptr);
}
#endif	/* IGBVF_DEBUG */
