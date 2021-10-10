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
 * Copyright 2010 QLogic Corporation. All rights reserved.
 */

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/debug.h>
#include <sys/stropts.h>
#include <sys/stream.h>
#include <sys/strlog.h>
#include <sys/kmem.h>
#include <sys/stat.h>
#include <sys/kstat.h>
#include <sys/vtrace.h>
#include <sys/dlpi.h>
#include <sys/strsun.h>
#include <sys/ethernet.h>
#include <sys/modctl.h>
#include <sys/errno.h>
#include <sys/dditypes.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/sysmacros.h>
#include <sys/pci.h>

#include "qlcnic.h"
#include "qlcnic_hw.h"
#include "qlcnic_cmn.h"
#include "qlcnic_ioctl.h"
#include "qlcnic_phan_reg.h"

struct crb_addr_pair {
	uint32_t	addr, data;
};

#define	MAX_CRB_XFORM	60
static uint32_t crb_addr_xform[MAX_CRB_XFORM];
#define	ADDR_ERROR	(0xffffffff)

#define	crb_addr_transform(name)				\
		crb_addr_xform[QLCNIC_HW_PX_MAP_CRB_##name] =		\
		QLCNIC_HW_CRB_HUB_AGT_ADR_##name << 20

static void
crb_addr_transform_setup(void)
{
		crb_addr_transform(XDMA);
		crb_addr_transform(TIMR);
		crb_addr_transform(SRE);
		crb_addr_transform(SQN3);
		crb_addr_transform(SQN2);
		crb_addr_transform(SQN1);
		crb_addr_transform(SQN0);
		crb_addr_transform(SQS3);
		crb_addr_transform(SQS2);
		crb_addr_transform(SQS1);
		crb_addr_transform(SQS0);
		crb_addr_transform(RPMX7);
		crb_addr_transform(RPMX6);
		crb_addr_transform(RPMX5);
		crb_addr_transform(RPMX4);
		crb_addr_transform(RPMX3);
		crb_addr_transform(RPMX2);
		crb_addr_transform(RPMX1);
		crb_addr_transform(RPMX0);
		crb_addr_transform(ROMUSB);
		crb_addr_transform(SN);
		crb_addr_transform(QMN);
		crb_addr_transform(QMS);
		crb_addr_transform(PGNI);
		crb_addr_transform(PGND);
		crb_addr_transform(PGN3);
		crb_addr_transform(PGN2);
		crb_addr_transform(PGN1);
		crb_addr_transform(PGN0);
		crb_addr_transform(PGSI);
		crb_addr_transform(PGSD);
		crb_addr_transform(PGS3);
		crb_addr_transform(PGS2);
		crb_addr_transform(PGS1);
		crb_addr_transform(PGS0);
		crb_addr_transform(PS);
		crb_addr_transform(PH);
		crb_addr_transform(NIU);
		crb_addr_transform(I2Q);
		crb_addr_transform(EG);
		crb_addr_transform(MN);
		crb_addr_transform(MS);
		crb_addr_transform(CAS2);
		crb_addr_transform(CAS1);
		crb_addr_transform(CAS0);
		crb_addr_transform(CAM);
		crb_addr_transform(C2C1);
		crb_addr_transform(C2C0);
		crb_addr_transform(SMB);
		crb_addr_transform(OCM0);

	/*
	 * Used only in P3 just define it for P2 also.
	 */
	crb_addr_transform(I2C0);
}

/*
 * decode_crb_addr(0 - utility to translate from internal Phantom CRB address
 * to external PCI CRB address.
 */
static uint32_t
decode_crb_addr(uint32_t addr)
{
	int i;
	uint32_t base_addr, offset, pci_base;

	crb_addr_transform_setup();

	pci_base = ADDR_ERROR;
	base_addr = addr & 0xfff00000;
	offset = addr & 0x000fffff;

	for (i = 0; i < MAX_CRB_XFORM; i++) {
		if (crb_addr_xform[i] == base_addr) {
			pci_base = i << 20;
			break;
		}
	}

	if (pci_base == ADDR_ERROR) {
		return (pci_base);
	} else {
		return (pci_base + offset);
	}
}

static int
rom_lock(qlcnic_adapter *adapter)
{
	uint32_t done = 0;
	uint32_t timeout = 0;
	uint32_t rom_lock_timeout = 10000;

	while (!done) {
		/* acquire semaphore2 from PCI HW block */
		qlcnic_read_w0(adapter, QLCNIC_PCIE_REG(PCIE_SEM2_LOCK), &done);

		if (done == 1)
			break;
		if (timeout >= rom_lock_timeout) {
			cmn_err(CE_WARN, "%s%d rom_lock timed out %d %d",
			    adapter->name, adapter->instance, done, timeout);
			return (-1);
		}
		qlcnic_msleep(1);
		timeout++;
	}
	qlcnic_reg_write(adapter, QLCNIC_ROM_LOCK_ID, ROM_LOCK_DRIVER);
	return (0);
}

static void
rom_unlock(qlcnic_adapter *adapter)
{
	uint32_t val;

	/* release semaphore2 */
	qlcnic_read_w0(adapter, QLCNIC_PCIE_REG(PCIE_SEM2_UNLOCK), &val);
}

static int
wait_rom_done(qlcnic_adapter *adapter)
{
	uint32_t timeout = 0;
	uint32_t done = 0;
	uint32_t rom_max_timeout = 100;

	while (done == 0) {
		qlcnic_reg_read(adapter, QLCNIC_ROMUSB_GLB_STATUS, &done);
		done &= 2;
		timeout++;
		if (timeout >= rom_max_timeout) {
			cmn_err(CE_WARN,
			    "Timeout reached waiting for rom done, "
			    "QLCNIC_ROMUSB_GLB_STATUS value %x", done);
			return (-1);
		}
		drv_usecwait(10);
	}
	return (0);
}

static int
qlcnic_do_rom_fast_read(qlcnic_adapter *adapter, int addr, int *valp)
{
	qlcnic_reg_write(adapter, QLCNIC_ROMUSB_ROM_ADDRESS, addr);
	qlcnic_reg_write(adapter, QLCNIC_ROMUSB_ROM_ABYTE_CNT, 3);
	drv_usecwait(100);   /* prevent bursting on CRB */
	qlcnic_reg_write(adapter, QLCNIC_ROMUSB_ROM_DUMMY_BYTE_CNT, 0);
	qlcnic_reg_write(adapter, QLCNIC_ROMUSB_ROM_INSTR_OPCODE, 0xb);
	if (wait_rom_done(adapter) != DDI_SUCCESS) {
		cmn_err(CE_WARN, "Error waiting for rom done");
		return (-1);
	}

	/* reset abyte_cnt and dummy_byte_cnt */
	qlcnic_reg_write(adapter, QLCNIC_ROMUSB_ROM_ABYTE_CNT, 0);
	drv_usecwait(100);   /* prevent bursting on CRB */
	qlcnic_reg_write(adapter, QLCNIC_ROMUSB_ROM_DUMMY_BYTE_CNT, 0);

	qlcnic_reg_read(adapter, QLCNIC_ROMUSB_ROM_RDATA, valp);
	return (0);
}

int
qlcnic_rom_fast_read(struct qlcnic_adapter_s *adapter, int addr, int *valp)
{
	int ret;

	if (rom_lock(adapter) != 0) {
		cmn_err(CE_WARN, "%s(%d)rom_lock failed",
		    __FUNCTION__, __LINE__);
		return (-1);
	}

	ret = qlcnic_do_rom_fast_read(adapter, addr, valp);
	if (ret != 0) {
		cmn_err(CE_WARN, "%s qlcnic_do_rom_fast_read returned: %d",
		    __FUNCTION__, __LINE__);
		ret = -1;
	}
	rom_unlock(adapter);
	return (ret);
}

int
qlcnic_pinit_from_rom(struct qlcnic_adapter_s *adapter, int verbose)
{
	int addr, val, status, i, init_delay = 0, n;
	struct crb_addr_pair *buf;
	uint32_t off;
	uint16_t offset;

	status = qlcnic_get_board_info(adapter);
	if (status) {
		cmn_err(CE_WARN,
		    "%s: qlcnic_pinit_from_rom: Error getting brdinfo",
		    qlcnic_driver_name);
		return (-1);
	}
	QLCNIC_CRB_WRITELIT_ADAPTER(QLCNIC_ROMUSB_GLB_SW_RESET, 0xffffffff,
	    adapter);

	if (verbose) {
		int	val;
		if (qlcnic_rom_fast_read(adapter, 0x4008, &val) == 0)
			cmn_err(CE_NOTE, "ROM board type: 0x%08x\n", val);
		else
			cmn_err(CE_WARN, "Could not read board type");
		if (qlcnic_rom_fast_read(adapter, 0x400c, &val) == 0)
			cmn_err(CE_NOTE, "ROM board  num: 0x%08x\n", val);
		else
			cmn_err(CE_WARN, "Could not read board number");
		if (qlcnic_rom_fast_read(adapter, 0x4010, &val) == 0)
			cmn_err(CE_NOTE, "ROM chip   num: 0x%08x\n", val);
		else
			cmn_err(CE_WARN, "Could not read chip number");
	}

	if (QLCNIC_IS_REVISION_P3(adapter->ahw.revision_id)) {
		if (qlcnic_rom_fast_read(adapter, 0, &n) != 0 ||
		    (unsigned int)n != 0xcafecafe ||
		    qlcnic_rom_fast_read(adapter, 4, &n) != 0) {
			cmn_err(CE_WARN, "%s: ERROR Reading crb_init area: "
			    "n: %08x", qlcnic_driver_name, n);
			return (-1);
		}

		offset = n & 0xffffU;
		n = (n >> 16) & 0xffffU;
	} else {
		if (qlcnic_rom_fast_read(adapter, 0, &n) != 0 ||
		    !(n & 0x80000000)) {
			cmn_err(CE_WARN, "%s: ERROR Reading crb_init area: "
			    "n: %08x", qlcnic_driver_name, n);
			return (-1);
		}
		offset = 1;
		n &= ~0x80000000;
	}

	if (n  >= 1024) {
		cmn_err(CE_WARN, "%s: %s:n=0x%x Card flash not initialized",
		    qlcnic_driver_name, __FUNCTION__, n);
		return (-1);
	}

	if (verbose)
		cmn_err(CE_NOTE, "%s: %d CRB init values found in ROM.\n",
		    qlcnic_driver_name, n);

	buf = kmem_zalloc(n * sizeof (struct crb_addr_pair), KM_SLEEP);
	if (buf == NULL) {
		cmn_err(CE_WARN,
		    "%s: qlcnic_pinit_from_rom: Unable to get memory",
		    qlcnic_driver_name);
		return (-1);
	}

	for (i = 0; i < n; i++) {
		if (qlcnic_rom_fast_read(adapter, 8*i + 4*offset, &val) != 0 ||
		    qlcnic_rom_fast_read(adapter, 8*i + 4*offset + 4,
		    &addr) != 0) {
			kmem_free(buf, n * sizeof (struct crb_addr_pair));
			return (-1);
		}

		buf[i].addr = addr;
		buf[i].data = val;

		if (verbose)
			cmn_err(CE_NOTE, "%s: PCI:     0x%08x == 0x%08x\n",
			    qlcnic_driver_name,
			    decode_crb_addr(
			    addr), val);
	}

	for (i = 0; i < n; i++) {
		off = decode_crb_addr(buf[i].addr);
		if (off == ADDR_ERROR) {
			cmn_err(CE_WARN, "%s: CRB init value out of range: "
			    "0x%08x", qlcnic_driver_name, buf[i].addr);
			continue;
		}
		off += QLCNIC_PCI_CRBSPACE;

		if (off & 1)
			continue;

		/* skipping cold reboot MAGIC */
		if (off == QLCNIC_CAM_RAM(0x1fc)) {
			continue;
		}

		if (QLCNIC_IS_REVISION_P3(adapter->ahw.revision_id)) {
			if (off == (QLCNIC_CRB_I2C0 + 0x1c))
				continue;
			/* do not reset PCI */
			if (off == (ROMUSB_GLB + 0xbc))
				continue;
			if (off == (ROMUSB_GLB + 0xa8))
				continue;
			if (off == (ROMUSB_GLB + 0xc8))	/* core clock */
				continue;
			if (off == (ROMUSB_GLB + 0x24))	/* MN clock */
				continue;
			if (off == (ROMUSB_GLB + 0x1c))	/* MS clock */
				continue;
			if ((off & 0x0ff00000) == QLCNIC_CRB_DDR_NET)
				continue;
			if ((off == (QLCNIC_CRB_PEG_NET_1 + 0x18)) &&
			    !QLCNIC_IS_REVISION_P3PLUS(
			    adapter->ahw.revision_id)) {
				buf[i].data = 0x1020;
			}
			/* skip the function enable register */
			if (off == QLCNIC_PCIE_REG(PCIE_SETUP_FUNCTION))
				continue;
			if (off == QLCNIC_PCIE_REG(PCIE_SETUP_FUNCTION2))
				continue;

			if ((off & 0x0ff00000) == QLCNIC_CRB_SMB)
				continue;
		}

		init_delay = 1; /* Sleep 1 msec */
		/* After writing this register, HW needs time for CRB */
		/* to quiet down (else crb_window returns 0xffffffff) */
		if (off == QLCNIC_ROMUSB_GLB_SW_RESET) {
			init_delay = 1000; /* Sleep 1000 msec */
		}

		adapter->qlcnic_hw_write_wx(adapter, off, &buf[i].data, 4);

		qlcnic_msleep(init_delay);
	}

	kmem_free(buf, n * sizeof (struct crb_addr_pair));

	/*
	 * disable_peg_cache_all
	 * unreset_net_cache
	 */

	/* p2dn replyCount */
	QLCNIC_CRB_WRITELIT_ADAPTER(QLCNIC_CRB_PEG_NET_D+0xec, 0x1e, adapter);
	/* disable_peg_cache 0 */
	QLCNIC_CRB_WRITELIT_ADAPTER(QLCNIC_CRB_PEG_NET_D+0x4c, 8, adapter);
	/* disable_peg_cache 1 */
	QLCNIC_CRB_WRITELIT_ADAPTER(QLCNIC_CRB_PEG_NET_I+0x4c, 8, adapter);

	/*
	 * peg_clr_all
	 * peg_clr 0
	 */
	QLCNIC_CRB_WRITELIT_ADAPTER(QLCNIC_CRB_PEG_NET_0+0x8, 0, adapter);
	QLCNIC_CRB_WRITELIT_ADAPTER(QLCNIC_CRB_PEG_NET_0+0xc, 0, adapter);
	/* peg_clr 1 */
	QLCNIC_CRB_WRITELIT_ADAPTER(QLCNIC_CRB_PEG_NET_1+0x8, 0, adapter);
	QLCNIC_CRB_WRITELIT_ADAPTER(QLCNIC_CRB_PEG_NET_1+0xc, 0, adapter);
	/* peg_clr 2 */
	QLCNIC_CRB_WRITELIT_ADAPTER(QLCNIC_CRB_PEG_NET_2+0x8, 0, adapter);
	QLCNIC_CRB_WRITELIT_ADAPTER(QLCNIC_CRB_PEG_NET_2+0xc, 0, adapter);
	/* peg_clr 3 */
	QLCNIC_CRB_WRITELIT_ADAPTER(QLCNIC_CRB_PEG_NET_3+0x8, 0, adapter);
	QLCNIC_CRB_WRITELIT_ADAPTER(QLCNIC_CRB_PEG_NET_3+0xc, 0, adapter);

	return (0);
}

int
qlcnic_phantom_init(struct qlcnic_adapter_s *adapter, int pegtune_val)
{
	u32 val = 0;
	int retries = 120;

	if (!pegtune_val) {
		do {
			val = adapter->qlcnic_pci_read_normalize(adapter,
			    CRB_CMDPEG_STATE);

			if ((val == PHAN_INITIALIZE_COMPLETE) ||
			    (val == PHAN_INITIALIZE_ACK)) {
				return (DDI_SUCCESS);
			}

			/* 500 msec wait */
			drv_usecwait(500000);
		} while (--retries > 0);

		if (!retries) {
			val = adapter->qlcnic_pci_read_normalize(adapter,
			    QLCNIC_ROMUSB_GLB_PEGTUNE_DONE);
			cmn_err(CE_WARN, "WARNING: Initial boot wait loop"
			    "failed...state:%d", val);
			return (DDI_FAILURE);
		}
	}

	return (DDI_SUCCESS);
}

int
qlcnic_load_from_flash(struct qlcnic_adapter_s *adapter)
{
	u64 data;
	u32 i, hi, lo, size;
	u32 flashaddr = BOOTLD_START;
	u32 memaddr = BOOTLD_START;
	u32 image_length = IMAGE_START - BOOTLD_START;
	int ret = DDI_FAILURE;

	size = image_length / 8;
	for (i = 0; i < size; i++) {

		if (qlcnic_rom_fast_read(adapter, flashaddr, (int *)&lo) != 0) {
			cmn_err(CE_WARN, "qlcnic_rom_fast_read: error reading"
			    "flash address %x", flashaddr);
			return (DDI_FAILURE);
		}

		if (qlcnic_rom_fast_read(adapter, flashaddr + 4,
		    (int *)&hi) != 0) {
			cmn_err(CE_WARN, "qlcnic_rom_fast_read: error reading"
			    "flash address %x", flashaddr);
			return (DDI_FAILURE);
		}

		data = (((u64)hi <<32) | lo);
		ret = adapter->qlcnic_pci_mem_write(adapter, memaddr, data);
		if (ret != 0)
			goto exit;
		flashaddr += 8;
		memaddr += 8;
	}

	drv_usecwait(100);
	QLCNIC_READ_LOCK(&adapter->adapter_lock);

	if (QLCNIC_IS_REVISION_P3PLUS(adapter->ahw.revision_id)) {
		i = 0x1020;
		adapter->qlcnic_hw_write_wx(adapter,
		    QLCNIC_CRB_PEG_NET_0 + 0x18, &i, 4);
		i = 0x80001e;
		adapter->qlcnic_hw_write_wx(adapter, QLCNIC_ROMUSB_GLB_SW_RESET,
		    &i, 4);
	} else if (QLCNIC_IS_REVISION_P3(adapter->ahw.revision_id)) {
		i = 0x80001d;
		adapter->qlcnic_hw_write_wx(adapter, QLCNIC_ROMUSB_GLB_SW_RESET,
		    &i, 4);
	} else {
		i = 0x3fff;
		adapter->qlcnic_hw_write_wx(adapter,
		    QLCNIC_ROMUSB_GLB_CHIP_CLK_CTRL, &i, 4);
		i = 0;
		adapter->qlcnic_hw_write_wx(adapter, QLCNIC_ROMUSB_GLB_CAS_RST,
		    &i, 4);
	}

	QLCNIC_READ_UNLOCK(&adapter->adapter_lock);
	ret = DDI_SUCCESS;
exit:
	return (ret);
}
