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
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include "igb_sw.h"
#include "igb_debug.h"

#ifdef IGB_DEBUG
static uint8_t igb_dump_pci_pm(igb_t *, uint8_t);
static uint8_t igb_dump_pci_msi(igb_t *, uint8_t);
static uint8_t igb_dump_pci_msix(igb_t *, uint8_t);
static uint8_t igb_dump_pci_pcie(igb_t *, uint8_t);
static void igb_dump_pci_sriov(igb_t *, uint16_t);

/*
 * igb_dump_pci_sriov: dump sr-iov capability block
 */
static void
igb_dump_pci_sriov(igb_t *igb, uint16_t offset)
{
	ddi_acc_handle_t handle = igb->osdep.cfg_handle;
	uint32_t control;
	uint32_t mask =
	    ((1 << 3)	/* bit 3: (VF MSE) VF memory space enable */
	    | 1);	/* bit 0: (VFE) VF enable */

	igb_log(igb, "-------- SRIOV Capability --------");
	igb_log(igb,
	    "sr-iov capabilities: 0x%x",
	    pci_config_get32(handle, (offset + 4)));

	control = (uint32_t)pci_config_get16(handle,
	    (offset + PCIE_EXT_CAP_SRIOV_CONTROL_OFFSET));
	igb_log(igb,
	    "sr-iov control register: 0x%x", control);

	igb_log(igb,
	    "sr-iov initial VFs: 0x%x",
	    pci_config_get16(handle,
	    (offset + PCIE_EXT_CAP_SRIOV_INITIAL_VFS_OFFSET)));

	igb_log(igb,
	    "sr-iov total VFs: 0x%x",
	    pci_config_get16(handle,
	    (offset + PCIE_EXT_CAP_SRIOV_TOTAL_VFS_OFFSET)));

	igb_log(igb,
	    "sr-iov num VFs: 0x%x",
	    pci_config_get16(handle,
	    (offset + PCIE_EXT_CAP_SRIOV_NUMVFS_OFFSET)));

	igb_log(igb,
	    "sr-iov first VF offset: 0x%x",
	    pci_config_get16(handle,
	    (offset + PCIE_EXT_CAP_SRIOV_VF_OFFSET_OFFSET)));

	igb_log(igb,
	    "sr-iov VF stride: 0x%x",
	    pci_config_get16(handle,
	    (offset + PCIE_EXT_CAP_SRIOV_VF_STRIDE_OFFSET)));

	igb_log(igb,
	    "sr-iov VF deviceid: 0x%x",
	    pci_config_get16(handle,
	    (offset + PCIE_EXT_CAP_SRIOV_VF_DEV_ID_OFFSET)));

	igb_log(igb,
	    "sr-iov supported page size: 0x%x",
	    pci_config_get32(handle,
	    (offset + PCIE_EXT_CAP_SRIOV_SUPPORTED_PAGE_SIZE_OFFSET)));

	igb_log(igb,
	    "sr-iov system page size: 0x%x",
	    pci_config_get32(handle,
	    (offset + PCIE_EXT_CAP_SRIOV_SYSTEM_PAGE_SIZE_OFFSET)));

	igb_log(igb,
	    "sr-iov bar0 low: 0x%x",
	    pci_config_get32(handle,
	    (offset + PCIE_EXT_CAP_SRIOV_VF_BAR0_OFFSET)));

	igb_log(igb,
	    "sr-iov bar0 high: 0x%x",
	    pci_config_get32(handle,
	    (offset + PCIE_EXT_CAP_SRIOV_VF_BAR0_OFFSET + 4)));

	igb_log(igb,
	    "sr-iov bar2: 0x%x",
	    pci_config_get32(handle,
	    (offset + PCIE_EXT_CAP_SRIOV_VF_BAR0_OFFSET + 8)));

	igb_log(igb,
	    "sr-iov bar3 low: 0x%x",
	    pci_config_get32(handle,
	    (offset + PCIE_EXT_CAP_SRIOV_VF_BAR0_OFFSET + 12)));

	igb_log(igb,
	    "sr-iov bar3 high: 0x%x",
	    pci_config_get32(handle,
	    (offset + PCIE_EXT_CAP_SRIOV_VF_BAR0_OFFSET + 16)));

	igb_log(igb,
	    "sr-iov bar5: 0x%x",
	    pci_config_get32(handle,
	    (offset + PCIE_EXT_CAP_SRIOV_VF_BAR5_OFFSET)));

	igb_log(igb,
	    "sr-iov VF migration: 0x%x",
	    pci_config_get32(handle,
	    (offset + PCIE_EXT_CAP_SRIOV_VF_BAR5_OFFSET + 4)));

	igb_log(igb, "sr-iov mode is %s", (control & mask) ? "ON" : "OFF");
}

void
igb_dump_pci(void *arg)
{
	igb_t *igb = (igb_t *)arg;
	ddi_acc_handle_t handle;
	uint8_t cap_ptr;
	uint8_t next_ptr;

	handle = igb->osdep.cfg_handle;

	igb_log(igb, "-------- PCIe Config Space --------");

	igb_log(igb,
	    "PCI_CONF_VENID:\t0x%x",
	    pci_config_get16(handle, PCI_CONF_VENID));
	igb_log(igb,
	    "PCI_CONF_DEVID:\t0x%x",
	    pci_config_get16(handle, PCI_CONF_DEVID));
	igb_log(igb,
	    "PCI_CONF_COMMAND:\t0x%x",
	    pci_config_get16(handle, PCI_CONF_COMM));
	igb_log(igb,
	    "PCI_CONF_STATUS:\t0x%x",
	    pci_config_get16(handle, PCI_CONF_STAT));
	igb_log(igb,
	    "PCI_CONF_REVID:\t0x%x",
	    pci_config_get8(handle, PCI_CONF_REVID));
	igb_log(igb,
	    "PCI_CONF_PROG_CLASS:\t0x%x",
	    pci_config_get8(handle, PCI_CONF_PROGCLASS));
	igb_log(igb,
	    "PCI_CONF_SUB_CLASS:\t0x%x",
	    pci_config_get8(handle, PCI_CONF_SUBCLASS));
	igb_log(igb,
	    "PCI_CONF_BAS_CLASS:\t0x%x",
	    pci_config_get8(handle, PCI_CONF_BASCLASS));
	igb_log(igb,
	    "PCI_CONF_CACHE_LINESZ:\t0x%x",
	    pci_config_get8(handle, PCI_CONF_CACHE_LINESZ));
	igb_log(igb,
	    "PCI_CONF_LATENCY_TIMER:\t0x%x",
	    pci_config_get8(handle, PCI_CONF_LATENCY_TIMER));
	igb_log(igb,
	    "PCI_CONF_HEADER_TYPE:\t0x%x",
	    pci_config_get8(handle, PCI_CONF_HEADER));
	igb_log(igb,
	    "PCI_CONF_BIST:\t0x%x",
	    pci_config_get8(handle, PCI_CONF_BIST));
	igb_log(igb,
	    "PCI_CONF_BASE0:\t0x%x",
	    pci_config_get32(handle, PCI_CONF_BASE0));
	igb_log(igb,
	    "PCI_CONF_BASE1:\t0x%x",
	    pci_config_get32(handle, PCI_CONF_BASE1));
	igb_log(igb,
	    "PCI_CONF_BASE2:\t0x%x",
	    pci_config_get32(handle, PCI_CONF_BASE2));
	igb_log(igb,
	    "PCI_CONF_BASE3:\t0x%x",
	    pci_config_get32(handle, PCI_CONF_BASE3));
	igb_log(igb,
	    "PCI_CONF_BASE4:\t0x%x",
	    pci_config_get32(handle, PCI_CONF_BASE4));
	igb_log(igb,
	    "PCI_CONF_BASE5:\t0x%x",
	    pci_config_get32(handle, PCI_CONF_BASE5));
	igb_log(igb,
	    "PCI_CONF_CIS:\t0x%x",
	    pci_config_get32(handle, PCI_CONF_CIS));
	igb_log(igb,
	    "PCI_CONF_SUBVENID:\t0x%x",
	    pci_config_get16(handle, PCI_CONF_SUBVENID));
	igb_log(igb,
	    "PCI_CONF_SUBSYSID:\t0x%x",
	    pci_config_get16(handle, PCI_CONF_SUBSYSID));
	igb_log(igb,
	    "PCI_CONF_ROM:\t0x%x",
	    pci_config_get32(handle, PCI_CONF_ROM));

	cap_ptr = pci_config_get8(handle, PCI_CONF_CAP_PTR);

	igb_log(igb,
	    "PCI_CONF_CAP_PTR:\t0x%x", cap_ptr);
	igb_log(igb,
	    "PCI_CONF_ILINE:\t0x%x",
	    pci_config_get8(handle, PCI_CONF_ILINE));
	igb_log(igb,
	    "PCI_CONF_IPIN:\t0x%x",
	    pci_config_get8(handle, PCI_CONF_IPIN));
	igb_log(igb,
	    "PCI_CONF_MIN_G:\t0x%x",
	    pci_config_get8(handle, PCI_CONF_MIN_G));
	igb_log(igb,
	    "PCI_CONF_MAX_L:\t0x%x",
	    pci_config_get8(handle, PCI_CONF_MAX_L));

	/* Power Management */
	next_ptr = igb_dump_pci_pm(igb, cap_ptr);

	/* MSI Configuration */
	next_ptr = igb_dump_pci_msi(igb, next_ptr);

	/* MSI-X Configuration */
	next_ptr = igb_dump_pci_msix(igb, next_ptr);

	/* PCI Express Configuration */
	next_ptr = igb_dump_pci_pcie(igb, next_ptr);

	/* SRIOV Configuration */
	igb_dump_pci_sriov(igb, 0x160);
}

static uint8_t
igb_dump_pci_pm(igb_t *igb, uint8_t offset)
{
	ddi_acc_handle_t handle;
	uint8_t next_ptr;

	handle = igb->osdep.cfg_handle;

	igb_log(igb, "-------- PM Capability --------");
	igb_log(igb,
	    "PCI_PM_CAP_ID:\t0x%x",
	    pci_config_get8(handle, offset));

	next_ptr = pci_config_get8(handle, offset + 1);

	igb_log(igb,
	    "PCI_PM_NEXT_PTR:\t0x%x", next_ptr);
	igb_log(igb,
	    "PCI_PM_CAP:\t0x%x",
	    pci_config_get16(handle, offset + PCI_PMCAP));
	igb_log(igb,
	    "PCI_PM_CSR:\t0x%x",
	    pci_config_get16(handle, offset + PCI_PMCSR));
	igb_log(igb,
	    "PCI_PM_CSR_BSE:\t0x%x",
	    pci_config_get8(handle, offset + PCI_PMCSR_BSE));
	igb_log(igb,
	    "PCI_PM_DATA:\t0x%x",
	    pci_config_get8(handle, offset + PCI_PMDATA));

	return (next_ptr);
}

static uint8_t
igb_dump_pci_msi(igb_t *igb, uint8_t offset)
{
	ddi_acc_handle_t handle;
	uint8_t next_ptr;

	handle = igb->osdep.cfg_handle;

	igb_log(igb, "-------- MSI Capability --------");
	igb_log(igb,
	    "PCI_MSI_CAP_ID:\t0x%x",
	    pci_config_get8(handle, offset));

	next_ptr = pci_config_get8(handle, offset + 1);

	igb_log(igb,
	    "PCI_MSI_NEXT_PTR:\t0x%x", next_ptr);
	igb_log(igb,
	    "PCI_MSI_CTRL:\t0x%x",
	    pci_config_get16(handle, offset + PCI_MSI_CTRL));
	igb_log(igb,
	    "PCI_MSI_ADDR:\t0x%x",
	    pci_config_get32(handle, offset + PCI_MSI_ADDR_OFFSET));
	igb_log(igb,
	    "PCI_MSI_ADDR_HI:\t0x%x",
	    pci_config_get32(handle, offset + 0x8));
	igb_log(igb,
	    "PCI_MSI_DATA:\t0x%x",
	    pci_config_get16(handle, offset + 0xC));

	return (next_ptr);
}

extern ddi_device_acc_attr_t igb_regs_acc_attr;

static uint8_t
igb_dump_pci_msix(igb_t *igb, uint8_t offset)
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

	handle = igb->osdep.cfg_handle;

	igb_log(igb, "-------- MSI-X Capability --------");
	igb_log(igb,
	    "PCI_MSIX_CAP_ID:\t0x%x",
	    pci_config_get8(handle, offset));

	next_ptr = pci_config_get8(handle, offset + 1);
	igb_log(igb,
	    "PCI_MSIX_NEXT_PTR:\t0x%x", next_ptr);

	msix_ctrl = pci_config_get16(handle, offset + PCI_MSIX_CTRL);
	msix_tbl_sz = msix_ctrl & 0x7ff;
	igb_log(igb,
	    "PCI_MSIX_CTRL:\t0x%x", msix_ctrl);

	tbl_offset = pci_config_get32(handle, offset + PCI_MSIX_TBL_OFFSET);
	tbl_bir = tbl_offset & PCI_MSIX_TBL_BIR_MASK;
	tbl_offset = tbl_offset & ~PCI_MSIX_TBL_BIR_MASK;
	igb_log(igb,
	    "PCI_MSIX_TBL_OFFSET:\t0x%x", tbl_offset);
	igb_log(igb,
	    "PCI_MSIX_TBL_BIR:\t0x%x", tbl_bir);

	pba_offset = pci_config_get32(handle, offset + PCI_MSIX_PBA_OFFSET);
	pba_bir = pba_offset & PCI_MSIX_PBA_BIR_MASK;
	pba_offset = pba_offset & ~PCI_MSIX_PBA_BIR_MASK;
	igb_log(igb,
	    "PCI_MSIX_PBA_OFFSET:\t0x%x", pba_offset);
	igb_log(igb,
	    "PCI_MSIX_PBA_BIR:\t0x%x", pba_bir);

	/* MSI-X Memory Space */
	if ((ret = ddi_dev_regsize(igb->dip, IGB_ADAPTER_MSIXTAB, &mem_size)) !=
	    DDI_SUCCESS) {
		igb_log(igb, "ddi_dev_regsize() failed: 0x%x", ret);
		return (next_ptr);
	}

	if ((ret = ddi_regs_map_setup(igb->dip, IGB_ADAPTER_MSIXTAB,
	    (caddr_t *)&base, 0, mem_size, &igb_regs_acc_attr, &acc_hdl)) !=
	    DDI_SUCCESS) {
		igb_log(igb, "ddi_regs_map_setup() failed: 0x%x", ret);
		return (next_ptr);
	}

	igb_log(igb, "-- MSI-X Memory Space (mem_size = %d, base = %x) --",
	    mem_size, base);

	for (i = 0; i <= msix_tbl_sz; i++) {
		igb_log(igb, "MSI-X Table Entry(%d):", i);
		igb_log(igb, "lo_addr:\t%x",
		    ddi_get32(acc_hdl,
		    (uint32_t *)(base + tbl_offset + (i * 16))));
		igb_log(igb, "up_addr:\t%x",
		    ddi_get32(acc_hdl,
		    (uint32_t *)(base + tbl_offset + (i * 16) + 4)));
		igb_log(igb, "msg_data:\t%x",
		    ddi_get32(acc_hdl,
		    (uint32_t *)(base + tbl_offset + (i * 16) + 8)));
		igb_log(igb, "vct_ctrl:\t%x",
		    ddi_get32(acc_hdl,
		    (uint32_t *)(base + tbl_offset + (i * 16) + 12)));
	}

	igb_log(igb, "MSI-X Pending Bits:\t%x",
	    ddi_get32(acc_hdl, (uint32_t *)(base + pba_offset)));

	ddi_regs_map_free(&acc_hdl);

	return (next_ptr);
}

static uint8_t
igb_dump_pci_pcie(igb_t *igb, uint8_t offset)
{
	ddi_acc_handle_t handle;
	uint8_t next_ptr;

	handle = igb->osdep.cfg_handle;

	igb_log(igb, "-------- PCIe Capability --------");
	igb_log(igb,
	    "PCIE_CAP_ID:\t0x%x",
	    pci_config_get8(handle, offset + PCIE_CAP_ID));

	next_ptr = pci_config_get8(handle, offset + PCIE_CAP_NEXT_PTR);

	igb_log(igb,
	    "PCIE_CAP_NEXT_PTR:\t0x%x", next_ptr);
	igb_log(igb,
	    "PCIE_PCIECAP:\t0x%x",
	    pci_config_get16(handle, offset + PCIE_PCIECAP));
	igb_log(igb,
	    "PCIE_DEVCAP:\t0x%x",
	    pci_config_get32(handle, offset + PCIE_DEVCAP));
	igb_log(igb,
	    "PCIE_DEVCTL:\t0x%x",
	    pci_config_get16(handle, offset + PCIE_DEVCTL));
	igb_log(igb,
	    "PCIE_DEVSTS:\t0x%x",
	    pci_config_get16(handle, offset + PCIE_DEVSTS));
	igb_log(igb,
	    "PCIE_LINKCAP:\t0x%x",
	    pci_config_get32(handle, offset + PCIE_LINKCAP));
	igb_log(igb,
	    "PCIE_LINKCTL:\t0x%x",
	    pci_config_get16(handle, offset + PCIE_LINKCTL));
	igb_log(igb,
	    "PCIE_LINKSTS:\t0x%x",
	    pci_config_get16(handle, offset + PCIE_LINKSTS));

	return (next_ptr);
}

void
igb_write_reg(void *arg, uint32_t reg, uint32_t value)
{
	struct e1000_hw *hw = (struct e1000_hw *)arg;

	ddi_put32((OS_DEP(hw))->reg_handle,
	    (uint32_t *)((uintptr_t)(hw)->hw_addr + reg), (value));
}

uint32_t
igb_read_reg(void *arg, uint32_t reg)
{
	struct e1000_hw *hw = (struct e1000_hw *)arg;

	return (ddi_get32((OS_DEP(hw))->reg_handle,
	    (uint32_t *)((uintptr_t)(hw)->hw_addr + reg)));
}

void
igb_write_reg_array(void *arg,
    uint32_t reg, uint32_t offset, uint32_t value)
{
	struct e1000_hw *hw = (struct e1000_hw *)arg;

	ddi_put32((OS_DEP(hw))->reg_handle,
	    (uint32_t *)((uintptr_t)(hw)->hw_addr + reg + ((offset) << 2)),
	    (value));
}
#endif	/* IGB_DEBUG */
