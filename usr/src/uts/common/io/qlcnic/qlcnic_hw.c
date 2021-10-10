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
#include "qlcnic_brdcfg.h"
#include "qlcnic_driver_info.h"

#define	MASK(n)		((1ULL<<(n))-1)
#define	MN_WIN(addr)	(((addr & 0x1fc0000) >> 1) | ((addr >> 25) & 0x3ff))
#define	OCM_WIN(addr)	(((addr & 0x1ff0000) >> 1) | ((addr >> 25) & 0x3ff))
#define	OCM_WIN_P3P(addr) (addr & 0xffc0000)
#define	MS_WIN(addr)	(addr & 0x0ffc0000)
#define	QLCNIC_PCI_MN_2M	(0)
#define	QLCNIC_PCI_MS_2M	(0x80000)
#define	QLCNIC_PCI_OCM0_2M	(0xc0000)
#define	VALID_OCM_ADDR(addr)	(((addr) & 0x3f800) != 0x3f800)
#define	GET_MEM_OFFS_2M(addr)	(addr & MASK(18))

#define	CRB_BLK(off)	((off >> 20) & 0x3f)
#define	CRB_SUBBLK(off)	((off >> 16) & 0xf)
#define	CRB_WINDOW_2M	(0x130060)
#define	QLCNIC_PCI_CAMQM_2M_END	(0x04800800UL)
#define	CRB_HI(off)	((crb_hub_agt[CRB_BLK(off)] << 20) | ((off) & 0xf0000))
#define	QLCNIC_PCI_CAMQM_2M_BASE	(0x000ff800UL)
#define	CRB_INDIRECT_2M	(0x1e0000UL)

static crb_128M_2M_block_map_t	crb_128M_2M_map[64] = {
	    {{{0, 0, 0, 0}}}, /* 0: PCI */
	    {{{1, 0x0100000, 0x0102000, 0x120000}, /* 1: PCIE */
	    {1, 0x0110000, 0x0120000, 0x130000},
	    {1, 0x0120000, 0x0122000, 0x124000},
	    {1, 0x0130000, 0x0132000, 0x126000},
	    {1, 0x0140000, 0x0142000, 0x128000},
	    {1, 0x0150000, 0x0152000, 0x12a000},
	    {1, 0x0160000, 0x0170000, 0x110000},
	    {1, 0x0170000, 0x0172000, 0x12e000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {1, 0x01e0000, 0x01e0800, 0x122000},
	    {0, 0x0000000, 0x0000000, 0x000000}}},
	    {{{1, 0x0200000, 0x0210000, 0x180000}}}, /* 2: MN */
	    {{{0, 0, 0, 0}}}, /* 3: */
	    {{{1, 0x0400000, 0x0401000, 0x169000}}}, /* 4: P2NR1 */
	    {{{1, 0x0500000, 0x0510000, 0x140000}}}, /* 5: SRE   */
	    {{{1, 0x0600000, 0x0610000, 0x1c0000}}}, /* 6: NIU   */
	    {{{1, 0x0700000, 0x0704000, 0x1b8000}}}, /* 7: QM    */
	    {{{1, 0x0800000, 0x0802000, 0x170000}, /* 8: SQM0  */
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {1, 0x08f0000, 0x08f2000, 0x172000}}},
	    {{{1, 0x0900000, 0x0902000, 0x174000}, /* 9: SQM1 */
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {1, 0x09f0000, 0x09f2000, 0x176000}}},
	    {{{0, 0x0a00000, 0x0a02000, 0x178000}, /* 10: SQM2 */
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {1, 0x0af0000, 0x0af2000, 0x17a000}}},
	    {{{0, 0x0b00000, 0x0b02000, 0x17c000}, /* 11: SQM3 */
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {1, 0x0bf0000, 0x0bf2000, 0x17e000}}},
	    {{{1, 0x0c00000, 0x0c04000, 0x1d4000}}}, /* 12: I2Q */
	    {{{1, 0x0d00000, 0x0d04000, 0x1a4000}}}, /* 13: TMR */
	    {{{1, 0x0e00000, 0x0e04000, 0x1a0000}}}, /* 14: ROMUSB */
	    {{{1, 0x0f00000, 0x0f01000, 0x164000}}}, /* 15: PEG4 */
	    {{{0, 0x1000000, 0x1004000, 0x1a8000}}}, /* 16: XDMA */
	    {{{1, 0x1100000, 0x1101000, 0x160000}}}, /* 17: PEG0 */
	    {{{1, 0x1200000, 0x1201000, 0x161000}}}, /* 18: PEG1 */
	    {{{1, 0x1300000, 0x1301000, 0x162000}}}, /* 19: PEG2 */
	    {{{1, 0x1400000, 0x1401000, 0x163000}}}, /* 20: PEG3 */
	    {{{1, 0x1500000, 0x1501000, 0x165000}}}, /* 21: P2ND */
	    {{{1, 0x1600000, 0x1601000, 0x166000}}}, /* 22: P2NI */
	    {{{0, 0, 0, 0}}}, /* 23: */
	    {{{0, 0, 0, 0}}}, /* 24: */
	    {{{0, 0, 0, 0}}}, /* 25: */
	    {{{0, 0, 0, 0}}}, /* 26: */
	    {{{0, 0, 0, 0}}}, /* 27: */
	    {{{0, 0, 0, 0}}}, /* 28: */
	    {{{1, 0x1d00000, 0x1d10000, 0x190000}}}, /* 29: MS */
	    {{{1, 0x1e00000, 0x1e01000, 0x16a000}}}, /* 30: P2NR2 */
	    {{{1, 0x1f00000, 0x1f10000, 0x150000}}}, /* 31: EPG */
	    {{{0}}}, /* 32: PCI */
	    {{{1, 0x2100000, 0x2102000, 0x120000}, /* 33: PCIE */
	    {1, 0x2110000, 0x2120000, 0x130000},
	    {1, 0x2120000, 0x2122000, 0x124000},
	    {1, 0x2130000, 0x2132000, 0x126000},
	    {1, 0x2140000, 0x2142000, 0x128000},
	    {1, 0x2150000, 0x2152000, 0x12a000},
	    {1, 0x2160000, 0x2170000, 0x110000},
	    {1, 0x2170000, 0x2172000, 0x12e000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000},
	    {0, 0x0000000, 0x0000000, 0x000000}}},
	    {{{1, 0x2200000, 0x2204000, 0x1b0000}}}, /* 34: CAM */
	    {{{0}}}, /* 35: */
	    {{{0}}}, /* 36: */
	    {{{0}}}, /* 37: */
	    {{{0}}}, /* 38: */
	    {{{0}}}, /* 39: */
	    {{{1, 0x2800000, 0x2804000, 0x1a4000}}}, /* 40: TMR */
	    {{{1, 0x2900000, 0x2901000, 0x16b000}}}, /* 41: P2NR3 */
	    {{{1, 0x2a00000, 0x2a00400, 0x1ac400}}}, /* 42: RPMX1 */
	    {{{1, 0x2b00000, 0x2b00400, 0x1ac800}}}, /* 43: RPMX2 */
	    {{{1, 0x2c00000, 0x2c00400, 0x1acc00}}}, /* 44: RPMX3 */
	    {{{1, 0x2d00000, 0x2d00400, 0x1ad000}}}, /* 45: RPMX4 */
	    {{{1, 0x2e00000, 0x2e00400, 0x1ad400}}}, /* 46: RPMX5 */
	    {{{1, 0x2f00000, 0x2f00400, 0x1ad800}}}, /* 47: RPMX6 */
	    {{{1, 0x3000000, 0x3000400, 0x1adc00}}}, /* 48: RPMX7 */
	    {{{0, 0x3100000, 0x3104000, 0x1a8000}}}, /* 49: XDMA */
	    {{{1, 0x3200000, 0x3204000, 0x1d4000}}}, /* 50: I2Q */
	    {{{1, 0x3300000, 0x3304000, 0x1a0000}}}, /* 51: ROMUSB */
	    {{{0}}}, /* 52: */
	    {{{1, 0x3500000, 0x3500400, 0x1ac000}}}, /* 53: RPMX0 */
	    {{{1, 0x3600000, 0x3600400, 0x1ae000}}}, /* 54: RPMX8 */
	    {{{1, 0x3700000, 0x3700400, 0x1ae400}}}, /* 55: RPMX9 */
	    {{{1, 0x3800000, 0x3804000, 0x1d0000}}}, /* 56: OCM0 */
	    {{{1, 0x3900000, 0x3904000, 0x1b4000}}}, /* 57: CRYPTO */
	    {{{1, 0x3a00000, 0x3a04000, 0x1d8000}}}, /* 58: SMB */
	    {{{0}}}, /* 59: I2C0 */
	    {{{0}}}, /* 60: I2C1 */
	    {{{1, 0x3d00000, 0x3d04000, 0x1d8000}}}, /* 61: LPC */
	    {{{1, 0x3e00000, 0x3e01000, 0x167000}}}, /* 62: P2NC */
	    {{{1, 0x3f00000, 0x3f01000, 0x168000}}} /* 63: P2NR0 */
};

/*
 * top 12 bits of crb internal address (hub, agent)
 */
static unsigned crb_hub_agt[64] = {
	0,
	QLCNIC_HW_CRB_HUB_AGT_ADR_PS,
	QLCNIC_HW_CRB_HUB_AGT_ADR_MN,
	QLCNIC_HW_CRB_HUB_AGT_ADR_MS,
	0,
	QLCNIC_HW_CRB_HUB_AGT_ADR_SRE,
	QLCNIC_HW_CRB_HUB_AGT_ADR_NIU,
	QLCNIC_HW_CRB_HUB_AGT_ADR_QMN,
	QLCNIC_HW_CRB_HUB_AGT_ADR_SQN0,
	QLCNIC_HW_CRB_HUB_AGT_ADR_SQN1,
	QLCNIC_HW_CRB_HUB_AGT_ADR_SQN2,
	QLCNIC_HW_CRB_HUB_AGT_ADR_SQN3,
	QLCNIC_HW_CRB_HUB_AGT_ADR_I2Q,
	QLCNIC_HW_CRB_HUB_AGT_ADR_TIMR,
	QLCNIC_HW_CRB_HUB_AGT_ADR_ROMUSB,
	QLCNIC_HW_CRB_HUB_AGT_ADR_PGN4,
	QLCNIC_HW_CRB_HUB_AGT_ADR_XDMA,
	QLCNIC_HW_CRB_HUB_AGT_ADR_PGN0,
	QLCNIC_HW_CRB_HUB_AGT_ADR_PGN1,
	QLCNIC_HW_CRB_HUB_AGT_ADR_PGN2,
	QLCNIC_HW_CRB_HUB_AGT_ADR_PGN3,
	QLCNIC_HW_CRB_HUB_AGT_ADR_PGND,
	QLCNIC_HW_CRB_HUB_AGT_ADR_PGNI,
	QLCNIC_HW_CRB_HUB_AGT_ADR_PGS0,
	QLCNIC_HW_CRB_HUB_AGT_ADR_PGS1,
	QLCNIC_HW_CRB_HUB_AGT_ADR_PGS2,
	QLCNIC_HW_CRB_HUB_AGT_ADR_PGS3,
	0,
	QLCNIC_HW_CRB_HUB_AGT_ADR_PGSI,
	QLCNIC_HW_CRB_HUB_AGT_ADR_SN,
	0,
	QLCNIC_HW_CRB_HUB_AGT_ADR_EG,
	0,
	QLCNIC_HW_CRB_HUB_AGT_ADR_PS,
	QLCNIC_HW_CRB_HUB_AGT_ADR_CAM,
	0,
	0,
	0,
	0,
	0,
	QLCNIC_HW_CRB_HUB_AGT_ADR_TIMR,
	0,
	QLCNIC_HW_CRB_HUB_AGT_ADR_RPMX1,
	QLCNIC_HW_CRB_HUB_AGT_ADR_RPMX2,
	QLCNIC_HW_CRB_HUB_AGT_ADR_RPMX3,
	QLCNIC_HW_CRB_HUB_AGT_ADR_RPMX4,
	QLCNIC_HW_CRB_HUB_AGT_ADR_RPMX5,
	QLCNIC_HW_CRB_HUB_AGT_ADR_RPMX6,
	QLCNIC_HW_CRB_HUB_AGT_ADR_RPMX7,
	QLCNIC_HW_CRB_HUB_AGT_ADR_XDMA,
	QLCNIC_HW_CRB_HUB_AGT_ADR_I2Q,
	QLCNIC_HW_CRB_HUB_AGT_ADR_ROMUSB,
	0,
	QLCNIC_HW_CRB_HUB_AGT_ADR_RPMX0,
	QLCNIC_HW_CRB_HUB_AGT_ADR_RPMX8,
	QLCNIC_HW_CRB_HUB_AGT_ADR_RPMX9,
	QLCNIC_HW_CRB_HUB_AGT_ADR_OCM0,
	0,
	QLCNIC_HW_CRB_HUB_AGT_ADR_SMB,
	QLCNIC_HW_CRB_HUB_AGT_ADR_I2C0,
	QLCNIC_HW_CRB_HUB_AGT_ADR_I2C1,
	0,
	QLCNIC_HW_CRB_HUB_AGT_ADR_PGNC,
	0,
};

#define	CRB_WIN_LOCK_TIMEOUT 100000000

static void
crb_win_lock(struct qlcnic_adapter_s *adapter)
{
	int i;
	int done = 0;
	int timeout = 0;

	while (!done) {
		/* acquire semaphore3 from PCI HW block */
		adapter->qlcnic_hw_read_wx(adapter,
		    QLCNIC_PCIE_REG(PCIE_SEM7_LOCK), &done, 4);
		if (done == 1)
			break;
		if (timeout >= CRB_WIN_LOCK_TIMEOUT) {
			cmn_err(CE_WARN, "%s%d: crb_win_lock timed out",
			    adapter->name, adapter->instance);
			return;
		}
		timeout++;
		/*
		 * Yield CPU
		 */
		for (i = 0; i < 20; i++)
			;
	}
	adapter->qlcnic_crb_writelit_adapter(adapter, QLCNIC_CRB_WIN_LOCK_ID,
	    adapter->portnum);
}

static void
crb_win_unlock(struct qlcnic_adapter_s *adapter)
{
	int val;

	adapter->qlcnic_hw_read_wx(adapter, QLCNIC_PCIE_REG(PCIE_SEM7_UNLOCK),
	    &val, 4);
}

/*
 * Changes the CRB window to the specified window.
 */
void
qlcnic_pci_change_crbwindow_128M(qlcnic_adapter *adapter, uint32_t window)
{
	uint32_t offset;
	uint32_t tmp;
	int i;

	if (adapter->ahw.crb_win == window) {
		return;
	}

	/*
	 * Move the CRB window.
	 * We need to write to the "direct access" region of PCI
	 * to avoid a race condition where the window register has
	 * not been successfully written across CRB before the target
	 * register address is received by PCI. The direct region bypasses
	 * the CRB bus.
	 */
	offset = PCI_OFFSET_SECOND_RANGE(adapter,
	    QLCNIC_PCIX_PH_REG(PCIE_CRB_WINDOW_REG(adapter->ahw.pci_func)));

	QLCNIC_PCI_WRITE_32(*(unsigned int *)&window, (void *)((uptr_t)offset));
	/* MUST make sure window is set before we forge on... */
	for (i = 10; i > 0; i--) {
		tmp = QLCNIC_PCI_READ_32((void *)((uptr_t)offset));
		if (tmp == *(uint32_t *)&window)
			break;
	}
	if (i > 0) {
		adapter->ahw.crb_win = window;
	} else {
		cmn_err(CE_WARN, "%s: %s WARNING: CRB window value not "
		    "registered properly: 0x%08x.",
		    qlcnic_driver_name, __FUNCTION__, tmp);
	}
}

/*
 * Changes the CRB window to the specified window.
 */
/* ARGSUSED */
void
qlcnic_pci_change_crbwindow_2M(qlcnic_adapter *adapter, uint32_t wndw)
{
}


uint32_t
qlcnic_get_crbwindow(qlcnic_adapter *adapter)
{
	return (adapter->ahw.crb_win);
}
/*
 * Return -1 if off is not valid,
 * 1 if window access is needed. 'off' is set to offset from
 * CRB space in 128M pci map
 * 0 if no window access is needed. 'off' is set to 2M addr
 * In: 'off' is offset from base in 128M pci map
 */
static int
qlcnic_pci_get_crb_addr_2M(qlcnic_adapter *adapter, u64 off, u64 *addr)
{
	crb_128M_2M_sub_block_map_t *m;

	if ((off >= QLCNIC_CRB_MAX) || (off < QLCNIC_PCI_CRBSPACE)) {
		return (-1);
	}
	off -= QLCNIC_PCI_CRBSPACE;

	/*
	 * Try direct map
	 */
	m = &crb_128M_2M_map[CRB_BLK(off)].sub_block[CRB_SUBBLK(off)];

	if (m->valid && (m->start_128M <= off) && (m->end_128M > off)) {
		*addr = adapter->ahw.pci_base0 + m->start_2M +
		    (off - m->start_128M);
		return (0);
	}

	/*
	 * Not in direct map, use crb window
	 */
	*addr = adapter->ahw.pci_base0 + CRB_INDIRECT_2M + (off & MASK(16));
	return (1);
}
/*
 * In: 'off' is offset from CRB space in 128M pci map
 * Out: 'off' is 2M pci map addr
 * side effect: lock crb window
 */
static void
qlcnic_pci_set_crbwindow_2M(qlcnic_adapter *adapter, u64 off)
{
	u32 window;
	u64 addr = adapter->ahw.pci_base0 + CRB_WINDOW_2M;

	off -= QLCNIC_PCI_CRBSPACE;
	window = CRB_HI(off);

	if (adapter->ahw.crb_win == window)
		return;

	QLCNIC_PCI_WRITE_32(window, (void *)(uptr_t)addr);
	/*
	 * Read back value to make sure write has gone through before trying
	 * to use it.
	 */
	if (window != QLCNIC_PCI_READ_32((void *)(uptr_t)addr)) {
		cmn_err(CE_WARN, "%s: Written crbwin (0x%x) != Read crbwin "
		    "(0x%x), off=0x%llx", __FUNCTION__, window,
		    QLCNIC_PCI_READ_32((void *)(uptr_t)addr),
		    off);
	}

	adapter->ahw.crb_win = window;

}

int
qlcnic_hw_write_ioctl_128M(qlcnic_adapter *adapter, u64 off, void *data,
    int len)
{
	void *addr;
	u64 offset = off;

	if (ADDR_IN_WINDOW1(off)) { /* Window 1 */
		addr = CRB_NORMALIZE(adapter, off);
		if (!addr) {
			offset = CRB_NORMAL(off);
			if (adapter->ahw.pci_len0 == 0)
				offset -= QLCNIC_PCI_CRBSPACE;
			addr = (void *) ((uint8_t *)adapter->ahw.pci_base0 +
			    offset);
		}
		QLCNIC_READ_LOCK(&adapter->adapter_lock);
	} else { /* Window 0 */
		addr = (void *) (uptr_t)(pci_base_offset(adapter, off));
		if (!addr) {
			offset = off;
			addr = (void *) ((uint8_t *)adapter->ahw.pci_base0 +
			    offset);
		}
		QLCNIC_WRITE_LOCK_IRQS(&adapter->adapter_lock, flags);
		qlcnic_pci_change_crbwindow_128M(adapter, 0);
	}

	switch (len) {
		case 1:
			QLCNIC_PCI_WRITE_8 (*(uint8_t *)data, addr);
			break;
		case 2:
			QLCNIC_PCI_WRITE_16 (*(uint16_t *)data, addr);
			break;
		case 4:
			QLCNIC_PCI_WRITE_32 (*(uint32_t *)data, addr);
			break;
		case 8:
			QLCNIC_PCI_WRITE_64 (*(uint64_t *)data, addr);
			break;
		default:
#if !defined(NDEBUG)
		if ((len & 0x7) != 0)
			cmn_err(CE_WARN, "%s: %s len(%d) not multiple of 8.",
			    qlcnic_driver_name, __FUNCTION__, len);
#endif
		QLCNIC_HW_BLOCK_WRITE_64(data, addr, (len>>3));
		break;
	}
	if (ADDR_IN_WINDOW1(off)) { /* Window 1 */
		QLCNIC_READ_UNLOCK(&adapter->adapter_lock);
	} else { /* Window 0 */
		qlcnic_pci_change_crbwindow_128M(adapter, QLCNIC_WINDOW_ONE);
		QLCNIC_WRITE_UNLOCK_IRQR(&adapter->adapter_lock, flags);
	}

	return (0);
}

/*
 * Note : 'len' argument should be either 1, 2, 4, or a multiple of 8.
 */
int
qlcnic_hw_write_wx_128M(qlcnic_adapter *adapter, u64 off, void *data, int len)
{
	/*
	 * This is modified from _qlcnic_hw_write().
	 * qlcnic_hw_write does not exist now.
	 */
	void *addr;

	if (ADDR_IN_WINDOW1(off)) { /* Window 1 */
		addr = CRB_NORMALIZE(adapter, off);
		QLCNIC_READ_LOCK(&adapter->adapter_lock);
	} else { /* Window 0 */
		addr = (void *)(uptr_t)(pci_base_offset(adapter, off));
		QLCNIC_WRITE_LOCK_IRQS(&adapter->adapter_lock, flags);
		qlcnic_pci_change_crbwindow_128M(adapter, 0);
	}


	if (!addr) {
		if (ADDR_IN_WINDOW1(off)) { /* Window 1 */
			QLCNIC_READ_UNLOCK(&adapter->adapter_lock);
		} else { /* Window 0 */
			qlcnic_pci_change_crbwindow_128M(adapter,
			    QLCNIC_WINDOW_ONE);
			QLCNIC_WRITE_UNLOCK_IRQR(&adapter->adapter_lock, flags);
		}
		return (1);
	}

	switch (len) {
		case 1:
			QLCNIC_PCI_WRITE_8 (*(uint8_t *)data, addr);
			break;
		case 2:
			QLCNIC_PCI_WRITE_16 (*(uint16_t *)data, addr);
			break;
		case 4:
			QLCNIC_PCI_WRITE_32 (*(uint32_t *)data, addr);
			break;
		case 8:
			QLCNIC_PCI_WRITE_64 (*(uint64_t *)data, addr);
			break;
		default:
#if !defined(NDEBUG)
			if ((len & 0x7) != 0)
				cmn_err(CE_WARN,
				    "%s: %s  len(%d) not multiple of 8.",
				    qlcnic_driver_name, __FUNCTION__, len);
#endif
			QLCNIC_HW_BLOCK_WRITE_64(data, addr, (len>>3));
			break;
	}
	if (ADDR_IN_WINDOW1(off)) { /* Window 1 */
		QLCNIC_READ_UNLOCK(&adapter->adapter_lock);
	} else { /* Window 0 */
		qlcnic_pci_change_crbwindow_128M(adapter, QLCNIC_WINDOW_ONE);
		QLCNIC_WRITE_UNLOCK_IRQR(&adapter->adapter_lock, flags);
	}

	return (0);
}

/*
 * Note : only 32-bit writes!
 */
void
qlcnic_pci_write_normalize_128M(qlcnic_adapter *adapter, u64 off, u32 data)
{
	QLCNIC_PCI_WRITE_32(data, CRB_NORMALIZE(adapter, off));
}

/*
 * Note : only 32-bit reads!
 */
u32
qlcnic_pci_read_normalize_128M(qlcnic_adapter *adapter, u64 off)
{
	return (QLCNIC_PCI_READ_32(CRB_NORMALIZE(adapter, off)));
}

/*
 * Note : only 32-bit writes!
 */
int
qlcnic_pci_write_immediate_128M(qlcnic_adapter *adapter, u64 off, u32 *data)
{
	QLCNIC_PCI_WRITE_32(*data,
	    (void *) (uptr_t)(PCI_OFFSET_SECOND_RANGE(adapter, off)));
	return (0);
}

/*
 * Note : only 32-bit reads!
 */
int
qlcnic_pci_read_immediate_128M(qlcnic_adapter *adapter, u64 off, u32 *data)
{
	*data = QLCNIC_PCI_READ_32((void *)
	    (uptr_t)(pci_base_offset(adapter, off)));
	return (0);
}

/*
 * Note : only 32-bit writes!
 */
void
qlcnic_pci_write_normalize_2M(qlcnic_adapter *adapter, u64 off, u32 data)
{
	u32 temp = data;

	adapter->qlcnic_hw_write_wx(adapter, off, &temp, 4);
}

/*
 * Note : only 32-bit reads!
 */
u32
qlcnic_pci_read_normalize_2M(qlcnic_adapter *adapter, u64 off)
{
	u32 temp;

	adapter->qlcnic_hw_read_wx(adapter, off, &temp, 4);

	return (temp);
}

/*
 * Note : only 32-bit writes!
 */
int
qlcnic_pci_write_immediate_2M(qlcnic_adapter *adapter, u64 off, u32 *data)
{
	u32 temp = *data;

	adapter->qlcnic_hw_write_wx(adapter, off, &temp, 4);

	return (0);
}

/*
 * Note : only 32-bit reads!
 */
int
qlcnic_pci_read_immediate_2M(qlcnic_adapter *adapter, u64 off, u32 *data)
{
	u32 temp;

	adapter->qlcnic_hw_read_wx(adapter, off, &temp, 4);

	*data = temp;

	return (0);
}

/*
 * write cross hw window boundary is not supported
 * 'len' should be either 1, 2, 4, or multiple of 8
 */
int
qlcnic_hw_write_wx_2M(qlcnic_adapter *adapter, u64 off, void *data, int len)
{
	int rv;
	u64 addr;

	rv = qlcnic_pci_get_crb_addr_2M(adapter, off, &addr);

	if (rv == -1) {
		cmn_err(CE_PANIC, "%s: invalid offset: 0x%016llx",
		    __FUNCTION__, off);
		return (-1);
	}

	if (rv == 1) {

		QLCNIC_WRITE_LOCK_IRQS(&adapter->adapter_lock, flags);
		crb_win_lock(adapter);
		qlcnic_pci_set_crbwindow_2M(adapter, off);
	}

	switch (len) {
	case 1:
		QLCNIC_PCI_WRITE_8(*(uint8_t *)data, (void *)(uptr_t)addr);
		break;
	case 2:
		QLCNIC_PCI_WRITE_16(*(uint16_t *)data, (void *)(uptr_t)addr);
		break;
	case 4:
		QLCNIC_PCI_WRITE_32(*(uint32_t *)data, (void *)(uptr_t)addr);
		break;
	case 8:
		QLCNIC_PCI_WRITE_64(*(uint64_t *)data, (void *)(uptr_t)addr);
		break;
	default:
#if !defined(NDEBUG)
		if ((len & 0x7) != 0)
			cmn_err(CE_WARN, "%s: %s  len(%d) not multiple of 8.",
			    qlcnic_driver_name, __FUNCTION__, len);
#endif
		QLCNIC_HW_BLOCK_WRITE_64(data, (uptr_t)addr, (len>>3));
		break;
	}
	if (rv == 1) {
		crb_win_unlock(adapter);
		QLCNIC_WRITE_UNLOCK_IRQR(&adapter->adapter_lock, flags);
	}

	return (0);
}

int
qlcnic_hw_read_ioctl_128M(qlcnic_adapter *adapter, u64 off, void *data, int len)
{
	void		*addr;
	u64		offset;

	if (ADDR_IN_WINDOW1(off)) { /* Window 1 */
		addr = CRB_NORMALIZE(adapter, off);
		if (!addr) {
			offset = CRB_NORMAL(off);
			if (adapter->ahw.pci_len0 == 0)
				offset -= QLCNIC_PCI_CRBSPACE;
			addr = (void *) ((uint8_t *)adapter->ahw.pci_base0 +
			    offset);
		}
		QLCNIC_READ_LOCK(&adapter->adapter_lock);
	} else { /* Window 0 */
		addr = (void *) (uptr_t)(pci_base_offset(adapter, off));
		if (!addr) {
			offset = off;
			addr = (void *) ((uint8_t *)adapter->ahw.pci_base0 +
			    offset);
		}
		QLCNIC_WRITE_LOCK_IRQS(&adapter->adapter_lock, flags);
		qlcnic_pci_change_crbwindow_128M(adapter, 0);
	}

	switch (len) {
	case 1:
		*(uint8_t *)data = QLCNIC_PCI_READ_8(addr);
		break;
	case 2:
		*(uint16_t *)data = QLCNIC_PCI_READ_16(addr);
		break;
	case 4:
		*(uint32_t *)data = QLCNIC_PCI_READ_32(addr);
		break;
	case 8:
		*(uint64_t *)data = QLCNIC_PCI_READ_64(addr);
		break;
	default:
#if !defined(NDEBUG)
		if ((len & 0x7) != 0)
			cmn_err(CE_WARN, "%s: %s len(%d) not multiple of 8.",
			    qlcnic_driver_name, __FUNCTION__, len);
#endif
		QLCNIC_HW_BLOCK_READ_64(data, addr, (len>>3));
		break;
	}

	if (ADDR_IN_WINDOW1(off)) { /* Window 1 */
		QLCNIC_READ_UNLOCK(&adapter->adapter_lock);
	} else { /* Window 0 */
		qlcnic_pci_change_crbwindow_128M(adapter, QLCNIC_WINDOW_ONE);
		QLCNIC_WRITE_UNLOCK_IRQR(&adapter->adapter_lock, flags);
	}

	return (0);
}

int
qlcnic_hw_read_wx_2M(qlcnic_adapter *adapter, u64 off, void *data, int len)
{
	int rv;
	u64 addr;

	rv = qlcnic_pci_get_crb_addr_2M(adapter, off, &addr);

	if (rv == -1) {
		cmn_err(CE_PANIC, "%s: invalid offset: 0x%016llx",
		    __FUNCTION__, off);
		return (-1);
	}

	if (rv == 1) {
		QLCNIC_WRITE_LOCK_IRQS(&adapter->adapter_lock, flags);
		crb_win_lock(adapter);
		qlcnic_pci_set_crbwindow_2M(adapter, off);
	}

	switch (len) {
	case 1:
		*(uint8_t  *)data = QLCNIC_PCI_READ_8((void *)(uptr_t)addr);
		break;
	case 2:
		*(uint16_t *)data = QLCNIC_PCI_READ_16((void *)(uptr_t)addr);
		break;
	case 4:
		*(uint32_t *)data = QLCNIC_PCI_READ_32((void *)(uptr_t)addr);
		break;
	case 8:
		*(uint64_t *)data = QLCNIC_PCI_READ_64((void *)(uptr_t)addr);
		break;
	default:
#if !defined(NDEBUG)
		if ((len & 0x7) != 0)
			cmn_err(CE_WARN, "%s: %s len(%d) not multiple of 8.",
			    qlcnic_driver_name, __FUNCTION__, len);
#endif
		QLCNIC_HW_BLOCK_READ_64(data, (void *)(uptr_t)addr, (len>>3));
		break;
	}

	if (rv == 1) {
		crb_win_unlock(adapter);
		QLCNIC_WRITE_UNLOCK_IRQR(&adapter->adapter_lock, flags);
	}

	return (0);
}

int
qlcnic_hw_read_wx_128M(qlcnic_adapter *adapter, u64 off, void *data, int len)
{
	void *addr;

	if (ADDR_IN_WINDOW1(off)) {
		/* Window 1 */
		addr = CRB_NORMALIZE(adapter, off);
		QLCNIC_READ_LOCK(&adapter->adapter_lock);
	} else { /* Window 0 */
		addr = (void *) (uptr_t)(pci_base_offset(adapter, off));
		QLCNIC_WRITE_LOCK_IRQS(&adapter->adapter_lock, flags);
		qlcnic_pci_change_crbwindow_128M(adapter, 0);
	}

	if (!addr) {
		if (ADDR_IN_WINDOW1(off)) { /* Window 1 */
			QLCNIC_READ_UNLOCK(&adapter->adapter_lock);
		} else { /* Window 0 */
			qlcnic_pci_change_crbwindow_128M(adapter,
			    QLCNIC_WINDOW_ONE);
			QLCNIC_WRITE_UNLOCK_IRQR(&adapter->adapter_lock, flags);
		}
		return (1);
	}

	switch (len) {
		case 1:
			*(uint8_t  *)data = QLCNIC_PCI_READ_8(addr);
			break;
		case 2:
			*(uint16_t *)data = QLCNIC_PCI_READ_16(addr);
			break;
		case 4:
			*(uint32_t *)data = QLCNIC_PCI_READ_32(addr);
			break;
		case 8:
			*(uint64_t *)data = QLCNIC_PCI_READ_64(addr);
			break;
		default:
#if !defined(NDEBUG)
			if ((len & 0x7) != 0)
				cmn_err(CE_WARN,
				    "%s: %s len(%d) not multiple of 8.",
				    qlcnic_driver_name, __FUNCTION__, len);
#endif
			QLCNIC_HW_BLOCK_READ_64(data, addr, (len>>3));
			break;
	}

	if (ADDR_IN_WINDOW1(off)) { /* Window 1 */
		QLCNIC_READ_UNLOCK(&adapter->adapter_lock);
	} else { /* Window 0 */
		qlcnic_pci_change_crbwindow_128M(adapter, QLCNIC_WINDOW_ONE);
		QLCNIC_WRITE_UNLOCK_IRQR(&adapter->adapter_lock, flags);
	}

	return (0);
}

/*  PCI Windowing for DDR regions.  */
#define	ADDR_IN_RANGE(addr, low, high)	    \
	(((addr) <= (high)) && ((low) ? ((addr) >= (low)) : 1))

uint64_t
pci_base_offset(struct qlcnic_adapter_s *adapter,
    unsigned long off)
{
	if (ADDR_IN_RANGE(off, FIRST_PAGE_GROUP_START, FIRST_PAGE_GROUP_END))
		return (PCI_OFFSET_FIRST_RANGE(adapter, off));

	if (ADDR_IN_RANGE(off, SECOND_PAGE_GROUP_START, SECOND_PAGE_GROUP_END))
		return (PCI_OFFSET_SECOND_RANGE(adapter, off));

	if (ADDR_IN_RANGE(off, THIRD_PAGE_GROUP_START, THIRD_PAGE_GROUP_END))
		return (PCI_OFFSET_THIRD_RANGE(adapter, off));

	return (0);
}

#ifdef DEBUG_ON
/*
 * check memory access boundary.
 * used by test agent. support ddr access only for now
 */
/* ARGSUSED */
static unsigned long
qlcnic_pci_mem_bound_check(struct qlcnic_adapter_s *adapter,
    unsigned long long addr, int size)
{
	if (adapter == NULL)
		return (1);
	if (!ADDR_IN_RANGE(addr,
	    QLCNIC_ADDR_DDR_NET, QLCNIC_ADDR_DDR_NET_MAX) ||
	    !ADDR_IN_RANGE(addr + size -1, QLCNIC_ADDR_DDR_NET,
	    QLCNIC_ADDR_DDR_NET_MAX) || ((size != 1) && (size != 2) &&
	    (size != 4) && (size != 8)))
		return (0);

	return (1);
}
#endif /* DEBUG_ON */

u64
qlcnic_get_ioaddr(qlcnic_adapter *adapter, u32 offset)
{
	u64 addr;

	(void) qlcnic_pci_get_crb_addr_2M(adapter, offset, &addr);

	return (addr);
}

unsigned long long
qlcnic_pci_set_window_128M(struct qlcnic_adapter_s *adapter,
    unsigned long long addr)
{
	if (adapter == NULL)
		return (-1ULL);
	if (ADDR_IN_RANGE(addr, QLCNIC_ADDR_OCM0, QLCNIC_ADDR_OCM0_MAX)) {
		addr = addr - QLCNIC_ADDR_OCM0 + QLCNIC_PCI_OCM0;
	} else if (ADDR_IN_RANGE(addr, QLCNIC_ADDR_OCM1,
	    QLCNIC_ADDR_OCM1_MAX)) {
		addr = addr - QLCNIC_ADDR_OCM1 + QLCNIC_PCI_OCM1;
	} else {
		addr = -1ULL;
	}
	return (addr);
}

unsigned long long
qlcnic_pci_set_window_2M(struct qlcnic_adapter_s *adapter,
    unsigned long long addr)
{
	u32 window;
	u32 win_read;
	u32 temp1;

	if ((addr & 0x00ff800) == 0xff800) {
		/* if bits 19:18&17:11 are on */
		cmn_err(CE_WARN, "%s: QM access not handled.",
		    __FUNCTION__);
		addr = -1ULL;
		return (addr);
	}

	if (QLCNIC_IS_REVISION_P3PLUS(adapter->ahw.revision_id))
		window = OCM_WIN_P3P(addr);
	else
		window = OCM_WIN(addr);

	adapter->qlcnic_hw_write_wx(adapter, adapter->ahw.ocm_win_crb,
	    &window, 4);
	adapter->qlcnic_hw_read_wx(adapter, adapter->ahw.ocm_win_crb,
	    &win_read, 4);
	temp1 = ((window & 0x1FF) << 7) | ((window & 0x0FFFE0000) >> 17);

	if (win_read != temp1) {
		cmn_err(CE_WARN,
		    "%s: Written wind (0x%x) != Read win (0x%x)",
		    __FUNCTION__, window, win_read);
	}
	adapter->ahw.ocm_win = window;

	addr = GET_MEM_OFFS_2M(addr) + QLCNIC_PCI_OCM0_2M;

	return (addr);
}

/* check if address is in the same windows as the previous access */
static uint32_t
qlcnic_pci_is_same_window(struct qlcnic_adapter_s *adapter,
    unsigned long long addr)
{
	if (adapter == NULL)
		return (0);
	if (ADDR_IN_RANGE(addr, QLCNIC_ADDR_OCM0, QLCNIC_ADDR_OCM0_MAX)) {
		return (1);
	} else if (ADDR_IN_RANGE(addr, QLCNIC_ADDR_OCM1,
	    QLCNIC_ADDR_OCM1_MAX)) {
		return (1);
	}

	return (0);
}

static int
qlcnic_pci_mem_read_direct(struct qlcnic_adapter_s *adapter,
    u64 off, void *data, int size)
{
	void *addr;
	int ret = 0;
	u64 start;

	QLCNIC_WRITE_LOCK_IRQS(&adapter->adapter_lock, flags);

	/*
	 * If attempting to access unknown address or straddle hw windows,
	 * do not access.
	 */
	if (((start = adapter->qlcnic_pci_set_window(adapter, off)) == -1UL) ||
	    (qlcnic_pci_is_same_window(adapter, off + size -1) == 0)) {
		QLCNIC_WRITE_UNLOCK_IRQR(&adapter->adapter_lock, flags);
		cmn_err(CE_WARN, "%s out of bound pci memory access. "
		    "offset is 0x%llx", qlcnic_driver_name, off);
		return (-1);
	}

	addr = (void *) (uptr_t)(pci_base_offset(adapter, start));
	if (!addr)
		addr = (void *) ((uint8_t *)adapter->ahw.pci_base0 + start);

	switch (size) {
		case 1:
			*(uint8_t  *)data = QLCNIC_PCI_READ_8(addr);
			break;
		case 2:
			*(uint16_t *)data = QLCNIC_PCI_READ_16(addr);
			break;
		case 4:
			*(uint32_t *)data = QLCNIC_PCI_READ_32(addr);
			break;
		case 8:
			*(uint64_t *)data = QLCNIC_PCI_READ_64(addr);
			break;
		default:
			ret = -1;
			break;
	}

	QLCNIC_WRITE_UNLOCK_IRQR(&adapter->adapter_lock, flags);
	return (ret);
}

static int
qlcnic_pci_mem_write_direct(struct qlcnic_adapter_s *adapter, u64 off,
    void *data, int size)
{
	void *addr;
	int ret = 0;
	u64 start;

	QLCNIC_WRITE_LOCK_IRQS(&adapter->adapter_lock, flags);

	/*
	 * If attempting to access unknown address or straddle hw windows,
	 * do not access.
	 */
	if (((start = adapter->qlcnic_pci_set_window(adapter, off)) == -1UL) ||
	    (qlcnic_pci_is_same_window(adapter, off + size -1) == 0)) {
		QLCNIC_WRITE_UNLOCK_IRQR(&adapter->adapter_lock, flags);
		cmn_err(CE_WARN, "%s out of bound pci memory access. "
		    "offset is 0x%llx", qlcnic_driver_name, off);
		return (-1);
	}

	addr = (void *) (uptr_t)(pci_base_offset(adapter, start));
	if (!addr)
		addr = (void *) ((uint8_t *)adapter->ahw.pci_base0 + start);

	switch (size) {
		case 1:
			QLCNIC_PCI_WRITE_8(*(uint8_t  *)data, addr);
			break;
		case 2:
			QLCNIC_PCI_WRITE_16(*(uint16_t *)data, addr);
			break;
		case 4:
			QLCNIC_PCI_WRITE_32(*(uint32_t *)data, addr);
			break;
		case 8:
			QLCNIC_PCI_WRITE_64(*(uint64_t *)data, addr);
			break;
		default:
			ret = -1;
			break;
	}
	QLCNIC_WRITE_UNLOCK_IRQR(&adapter->adapter_lock, flags);
	return (ret);
}

#define	MAX_CTL_CHECK   1000

int
qlcnic_pci_mem_write_128M(struct qlcnic_adapter_s *adapter, u64 off, u64 data)
{
	int j, ret;
	uint32_t temp, off_lo, off_hi, addr_hi, data_hi, data_lo;
	uint64_t mem_crb;

	/* Only 64-bit aligned access */
	if (off & 7)
		return (-1);

	if (ADDR_IN_RANGE(off, QLCNIC_ADDR_DDR_NET, QLCNIC_ADDR_DDR_NET_MAX)) {
		mem_crb = pci_base_offset(adapter,
		    QLCNIC_CRB_DDR_NET+MIU_TEST_AGT_BASE);
		addr_hi = MIU_TEST_AGT_ADDR_HI;
		data_lo = MIU_TEST_AGT_WRDATA_LO;
		data_hi = MIU_TEST_AGT_WRDATA_HI;
		off_lo = off & MIU_TEST_AGT_ADDR_MASK;
		off_hi = 0;
		goto correct;
	}

	if (ADDR_IN_RANGE(off, QLCNIC_ADDR_OCM0, QLCNIC_ADDR_OCM0_MAX) ||
	    ADDR_IN_RANGE(off, QLCNIC_ADDR_OCM1, QLCNIC_ADDR_OCM1_MAX)) {
		if (adapter->ahw.pci_len0 != 0) {
			ret = qlcnic_pci_mem_write_direct(adapter, off, &data,
			    8);
			return (ret);
		}
	}

	return (-1);

correct:
	QLCNIC_WRITE_LOCK_IRQS(&adapter->adapter_lock, flags);
	qlcnic_pci_change_crbwindow_128M(adapter, 0);

	QLCNIC_PCI_WRITE_32(off_lo,
	    (void *) (uptr_t)(mem_crb+MIU_TEST_AGT_ADDR_LO));
	QLCNIC_PCI_WRITE_32(off_hi,
	    (void *) (uptr_t)(mem_crb+addr_hi));
	QLCNIC_PCI_WRITE_32(data & 0xffffffff,
	    (void *) (uptr_t)(mem_crb+data_lo));
	QLCNIC_PCI_WRITE_32((data >> 32) & 0xffffffff,
	    (void *) (uptr_t)(mem_crb+data_hi));
	QLCNIC_PCI_WRITE_32(TA_CTL_ENABLE|TA_CTL_WRITE,
	    (void *) (uptr_t)(mem_crb+TEST_AGT_CTRL));
	QLCNIC_PCI_WRITE_32(TA_CTL_START | TA_CTL_ENABLE | TA_CTL_WRITE,
	    (void *) (uptr_t)(mem_crb+TEST_AGT_CTRL));

	for (j = 0; j < MAX_CTL_CHECK; j++) {
		temp = QLCNIC_PCI_READ_32((void *)
		    (uptr_t)(mem_crb+TEST_AGT_CTRL));
		if ((temp & TA_CTL_BUSY) == 0) {
			break;
		}
	}

	if (j >= MAX_CTL_CHECK) {
		cmn_err(CE_WARN, "%s: %s Fail to write thru agent",
		    __FUNCTION__, qlcnic_driver_name);
		ret = -1;
	} else {
		ret = 0;
	}

	qlcnic_pci_change_crbwindow_128M(adapter, QLCNIC_WINDOW_ONE);
	QLCNIC_WRITE_UNLOCK_IRQR(&adapter->adapter_lock, flags);
	return (ret);
}

int
qlcnic_pci_mem_read_128M(struct qlcnic_adapter_s *adapter, u64 off, u64 *data)
{
	int j, ret;
	u32 temp, off_lo, off_hi, addr_hi, data_hi, data_lo;
	u64 val;
	uint64_t mem_crb;

	/* Only 64-bit aligned access */
	if (off & 7)
		return (-1);

	if (ADDR_IN_RANGE(off, QLCNIC_ADDR_DDR_NET, QLCNIC_ADDR_DDR_NET_MAX)) {
		mem_crb = pci_base_offset(adapter,
		    QLCNIC_CRB_DDR_NET+MIU_TEST_AGT_BASE);
		addr_hi = MIU_TEST_AGT_ADDR_HI;
		data_lo = MIU_TEST_AGT_RDDATA_LO;
		data_hi = MIU_TEST_AGT_RDDATA_HI;
		off_lo = off & MIU_TEST_AGT_ADDR_MASK;
		off_hi = 0;
		goto correct;
	}

	if (ADDR_IN_RANGE(off, QLCNIC_ADDR_OCM0, QLCNIC_ADDR_OCM0_MAX) ||
	    ADDR_IN_RANGE(off, QLCNIC_ADDR_OCM1, QLCNIC_ADDR_OCM1_MAX)) {
		if (adapter->ahw.pci_len0 != 0) {
			(void) qlcnic_pci_mem_read_direct(adapter, off, &data,
			    8);
			return (0);
		}
	}

	return (-1);

correct:

	QLCNIC_WRITE_LOCK_IRQS(&adapter->adapter_lock, flags);
	qlcnic_pci_change_crbwindow_128M(adapter, 0);

	QLCNIC_PCI_WRITE_32(off_lo,
	    (void *) (uptr_t)(mem_crb+MIU_TEST_AGT_ADDR_LO));
	QLCNIC_PCI_WRITE_32(off_hi,
	    (void *) (uptr_t)(mem_crb+addr_hi));
	QLCNIC_PCI_WRITE_32(TA_CTL_ENABLE,
	    (void *) (uptr_t)(mem_crb+TEST_AGT_CTRL));
	QLCNIC_PCI_WRITE_32(TA_CTL_START|TA_CTL_ENABLE,
	    (void *) (uptr_t)(mem_crb+TEST_AGT_CTRL));

	for (j = 0; j < MAX_CTL_CHECK; j++) {
		temp = QLCNIC_PCI_READ_32((void *)
		    (uptr_t)(mem_crb+TEST_AGT_CTRL));
		if ((temp & TA_CTL_BUSY) == 0) {
			break;
		}
	}

	if (j >= MAX_CTL_CHECK) {
		cmn_err(CE_WARN, "%s: %s Fail to read through agent.",
		    __FUNCTION__, qlcnic_driver_name);
		ret = -1;
	} else {
		temp = QLCNIC_PCI_READ_32((void *)(uptr_t)(mem_crb+data_hi));
		val = ((u64)temp << 32);
		temp = QLCNIC_PCI_READ_32((void *)(uptr_t)(mem_crb+data_lo));
		val |= temp;
		*data = val;
		ret = 0;
	}

	qlcnic_pci_change_crbwindow_128M(adapter, QLCNIC_WINDOW_ONE);
	QLCNIC_WRITE_UNLOCK_IRQR(&adapter->adapter_lock, flags);

	return (ret);
}

int
qlcnic_pci_mem_write_2M(struct qlcnic_adapter_s *adapter, u64 off, u64 data)
{
	int i, j, ret;
	uint32_t temp, off8;
	uint64_t mem_crb, stride;

	/* Only 64-bit aligned access */
	if (off & 7)
		return (-1);

	/* P3 onward, test agent base for MIU and SIU is same */
	if (ADDR_IN_RANGE(off, QLCNIC_ADDR_QDR_NET,
	    QLCNIC_ADDR_QDR_NET_MAX_P3)) {
		mem_crb = qlcnic_get_ioaddr(adapter,
		    QLCNIC_CRB_QDR_NET + MIU_TEST_AGT_BASE);
		goto correct;
	}

	if (ADDR_IN_RANGE(off, QLCNIC_ADDR_DDR_NET, QLCNIC_ADDR_DDR_NET_MAX)) {
		mem_crb = qlcnic_get_ioaddr(adapter,
		    QLCNIC_CRB_DDR_NET + MIU_TEST_AGT_BASE);
		goto correct;
	}
	if (ADDR_IN_RANGE(off, QLCNIC_ADDR_OCM0, QLCNIC_ADDR_OCM0_MAX)) {
		cmn_err(CE_NOTE, "direct write addr %llx, data %llx",
		    off, data);
		ret = qlcnic_pci_mem_write_direct(adapter, off, &data, 8);
		return (ret);
	}
	cmn_err(CE_WARN,
	    "qlcnic_pci_mem_write_2M(%d) failed, off %llx, data %llx",
	    adapter->instance, off, data);

	return (-1);

correct:

	stride = QLCNIC_IS_REVISION_P3PLUS(adapter->ahw.revision_id) ? 16 : 8;

	off8 = off & ~(stride-1);

	QLCNIC_WRITE_LOCK_IRQS(&adapter->adapter_lock, flags);

	QLCNIC_PCI_WRITE_32(off8,
	    (void *)(uptr_t)(mem_crb + MIU_TEST_AGT_ADDR_LO));

	QLCNIC_PCI_WRITE_32(0,
	    (void *)(uptr_t)(mem_crb + MIU_TEST_AGT_ADDR_HI));

	i = 0;
	if (stride == 16) {
		QLCNIC_PCI_WRITE_32(TA_CTL_ENABLE,
		    (void *)(uptr_t)(mem_crb + TEST_AGT_CTRL));

		QLCNIC_PCI_WRITE_32(TA_CTL_START | TA_CTL_ENABLE,
		    (void *)(uptr_t)(mem_crb + TEST_AGT_CTRL));

		for (j = 0; j < MAX_CTL_CHECK; j++) {
			temp = QLCNIC_PCI_READ_32((void *)
			    (uptr_t)(mem_crb + TEST_AGT_CTRL));
			if ((temp & TA_CTL_BUSY) == 0) {
				break;
			}
		}

		if (j >= MAX_CTL_CHECK) {
			cmn_err(CE_WARN, "%s: Fail to write through agent1,"
			    " addr %llx",
			    qlcnic_driver_name, off);
			ret = -1;
			goto done;
		}

		i = (off & 0xf) ? 0 : 2;
		temp = QLCNIC_PCI_READ_32((void *)
		    (uptr_t)(mem_crb + MIU_TEST_AGT_RDDATA(i)));

		QLCNIC_PCI_WRITE_32(temp,
		    (void *)(uptr_t)(mem_crb + MIU_TEST_AGT_WRDATA(i)));

		temp = QLCNIC_PCI_READ_32((void *)
		    (uptr_t)(mem_crb + MIU_TEST_AGT_RDDATA(i + 1)));

		QLCNIC_PCI_WRITE_32(temp,
		    (void *)(uptr_t)(mem_crb + MIU_TEST_AGT_WRDATA(i + 1)));

		i = (off & 0xf) ? 2 : 0;
	}

	QLCNIC_PCI_WRITE_32(data & 0xffffffff,
	    (void *)(uptr_t)(mem_crb + MIU_TEST_AGT_WRDATA(i)));

	QLCNIC_PCI_WRITE_32((data >> 32) & 0xffffffff,
	    (void *)(uptr_t)(mem_crb + MIU_TEST_AGT_WRDATA(i + 1)));

	QLCNIC_PCI_WRITE_32(TA_CTL_ENABLE | TA_CTL_WRITE,
	    (void *)(uptr_t)(mem_crb + TEST_AGT_CTRL));

	QLCNIC_PCI_WRITE_32(TA_CTL_START | TA_CTL_ENABLE | TA_CTL_WRITE,
	    (void *)(uptr_t)(mem_crb + TEST_AGT_CTRL));

	for (j = 0; j < MAX_CTL_CHECK; j++) {
		temp = QLCNIC_PCI_READ_32((void *)
		    (uptr_t)(mem_crb + TEST_AGT_CTRL));
		if ((temp & TA_CTL_BUSY) == 0) {
			break;
		}
	}

	if (j >= MAX_CTL_CHECK) {
		cmn_err(CE_WARN, "%s: Fail to write through agent2 addr %llx",
		    qlcnic_driver_name, off);
		ret = -1;
		goto done;
	} else {
		ret = 0;
	}

done:
	QLCNIC_WRITE_UNLOCK_IRQR(&adapter->adapter_lock, flags);

	return (ret);
}

int
qlcnic_pci_mem_read_2M(struct qlcnic_adapter_s *adapter, u64 off, u64 *data)
{
	int j, ret;
	uint32_t temp, off8;
	uint64_t val, stride, mem_crb;

	/* Only 64-bit aligned access */
	if (off & 7)
		return (-1);

	/* P3 onward, test agent base for MIU and SIU is same */
	if (ADDR_IN_RANGE(off, QLCNIC_ADDR_QDR_NET,
	    QLCNIC_ADDR_QDR_NET_MAX_P3)) {
		mem_crb = qlcnic_get_ioaddr(adapter,
		    QLCNIC_CRB_QDR_NET+MIU_TEST_AGT_BASE);
		goto correct;
	}

	if (ADDR_IN_RANGE(off, QLCNIC_ADDR_DDR_NET, QLCNIC_ADDR_DDR_NET_MAX)) {
		mem_crb = qlcnic_get_ioaddr(adapter,
		    QLCNIC_CRB_DDR_NET+MIU_TEST_AGT_BASE);
		goto correct;
	}

	if (ADDR_IN_RANGE(off, QLCNIC_ADDR_OCM0, QLCNIC_ADDR_OCM0_MAX)) {
		(void) qlcnic_pci_mem_read_direct(adapter, off, &data, 8);
		return (0);
	}

	return (-1);

correct:
	stride = QLCNIC_IS_REVISION_P3PLUS(adapter->ahw.revision_id) ? 16 : 8;

	off8 = off & ~(stride-1);

	QLCNIC_WRITE_LOCK_IRQS(&adapter->adapter_lock, flags);

	QLCNIC_PCI_WRITE_32(off8,
	    (void *) (uptr_t)(mem_crb + MIU_TEST_AGT_ADDR_LO));

	QLCNIC_PCI_WRITE_32(0,
	    (void *) (uptr_t)(mem_crb + MIU_TEST_AGT_ADDR_HI));

	QLCNIC_PCI_WRITE_32(TA_CTL_ENABLE,
	    (void *) (uptr_t)(mem_crb + TEST_AGT_CTRL));

	QLCNIC_PCI_WRITE_32(TA_CTL_START | TA_CTL_ENABLE,
	    (void *) (uptr_t)(mem_crb + TEST_AGT_CTRL));

	for (j = 0; j < MAX_CTL_CHECK; j++) {
		temp = QLCNIC_PCI_READ_32((void *)
		    (uptr_t)(mem_crb + TEST_AGT_CTRL));
		if ((temp & TA_CTL_BUSY) == 0) {
			break;
		}
	}

	if (j >= MAX_CTL_CHECK) {
		cmn_err(CE_WARN, "%s: Fail to read through agent",
		    qlcnic_driver_name);
		ret = -1;
	} else {
		off8 = MIU_TEST_AGT_RDDATA_LO;
		if ((stride == 16) && (off & 0xf))
			off8 = MIU_TEST_AGT_RDDATA_UPPER_LO;

		temp = QLCNIC_PCI_READ_32((void *)
		    (uptr_t)(mem_crb + off8 + 4));
		val = (u64)temp << 32;

		temp = QLCNIC_PCI_READ_32((void *)
		    (uptr_t)(mem_crb + off8));
		val |= temp;
		*data = val;
		ret = 0;

	}

	QLCNIC_WRITE_UNLOCK_IRQR(&adapter->adapter_lock, flags);

	return (ret);
}

int
qlcnic_crb_writelit_adapter_2M(struct qlcnic_adapter_s *adapter, u64 off,
    int data)
{
	return (qlcnic_hw_write_wx_2M(adapter, off, &data, 4));
}

int
qlcnic_crb_writelit_adapter_128M(struct qlcnic_adapter_s *adapter, u64 off,
    int data)
{
	void *addr;

	if (ADDR_IN_WINDOW1(off)) {
		QLCNIC_READ_LOCK(&adapter->adapter_lock);
		QLCNIC_PCI_WRITE_32(data, CRB_NORMALIZE(adapter, off));
		QLCNIC_READ_UNLOCK(&adapter->adapter_lock);
	} else {
		QLCNIC_WRITE_LOCK_IRQS(&adapter->adapter_lock, flags);
		qlcnic_pci_change_crbwindow_128M(adapter, 0);
		addr = (void *)(uptr_t)(pci_base_offset(adapter, off));
		QLCNIC_PCI_WRITE_32(data, addr);
		qlcnic_pci_change_crbwindow_128M(adapter, QLCNIC_WINDOW_ONE);
		QLCNIC_WRITE_UNLOCK_IRQR(&adapter->adapter_lock, flags);
	}

	return (0);
}

int
qlcnic_get_board_info(struct qlcnic_adapter_s *adapter)
{
	int rv = 0;
	qlcnic_board_info_t *boardinfo;
	int i;
	int addr = BRDCFG_START;
	uint32_t *ptr32;
	uint32_t gpioval;

	boardinfo = &adapter->ahw.boardcfg;
	ptr32 = (uint32_t *)boardinfo;

	for (i = 0; i < sizeof (qlcnic_board_info_t) / sizeof (uint32_t); i++) {
		if (qlcnic_rom_fast_read(adapter, addr, (int *)ptr32) == -1) {
			return (-1);
		}
		ptr32++;
		addr += sizeof (uint32_t);
	}

	if (boardinfo->magic != QLCNIC_BDINFO_MAGIC) {
		cmn_err(CE_WARN, "%s: ERROR reading board config."
		    " Read %x, expected %x", qlcnic_driver_name,
		    boardinfo->magic, QLCNIC_BDINFO_MAGIC);
		rv = -1;
		goto exit;
	}

	if (boardinfo->board_type == QLCNIC_BRDTYPE_P3_4_GB_MM) {
		gpioval = QLCNIC_CRB_READ_VAL_ADAPTER(
		    QLCNIC_ROMUSB_GLB_PAD_GPIO_I,
		    adapter);
		if ((gpioval & 0x8000) == 0)
			boardinfo->board_type = QLCNIC_BRDTYPE_P3_10G_TRP;
	}

	switch ((qlcnic_brdtype_t)boardinfo->board_type) {
	case QLCNIC_BRDTYPE_P3_HMEZ:
	case QLCNIC_BRDTYPE_P3_XG_LOM:
	case QLCNIC_BRDTYPE_P3_10G_CX4:
	case QLCNIC_BRDTYPE_P3_10G_CX4_LP:
	case QLCNIC_BRDTYPE_P3_IMEZ:
	case QLCNIC_BRDTYPE_P3_10G_SFP_PLUS:
	case QLCNIC_BRDTYPE_P3_10G_XFP:
	case QLCNIC_BRDTYPE_P3_10000_BASE_T:
		adapter->ahw.board_type = QLCNIC_XGBE;
		break;
	case QLCNIC_BRDTYPE_P3_REF_QG:
	case QLCNIC_BRDTYPE_P3_4_GB:
	case QLCNIC_BRDTYPE_P3_4_GB_MM:
		adapter->ahw.board_type = QLCNIC_GBE;
		break;
	case QLCNIC_BRDTYPE_P3_10G_TRP:
		if (adapter->portnum < 2)
			adapter->ahw.board_type = QLCNIC_XGBE;
		else
			adapter->ahw.board_type = QLCNIC_GBE;
		break;
	default:
		cmn_err(CE_WARN, "%s: Unknown board type %x",
		    qlcnic_driver_name, boardinfo->board_type);
		break;
	}
exit:
	return (rv);
}

/* NIU access sections */

int
qlcnic_macaddr_set(struct qlcnic_adapter_s *adapter, uint8_t *addr)
{
	int ret = 0, i, retry_count = 10;
	unsigned char mac_addr[ETHERADDRL];

	/* For P3, we should not set MAC in HW any more */
	if (QLCNIC_IS_REVISION_P3(adapter->ahw.revision_id))
		return (0);

	switch (adapter->ahw.board_type) {
		case QLCNIC_GBE:
	/*
	 * Flaky Mac address registers on qgig require several writes.
	 */
			for (i = 0; i < retry_count; ++i) {
				if (qlcnic_niu_macaddr_set(adapter, addr) != 0)
					return (-1);

				(void) qlcnic_niu_macaddr_get(adapter,
				    (unsigned char *)mac_addr);
				if (memcmp(mac_addr, addr, 6) == 0)
					return (0);
			}
			cmn_err(CE_WARN, "%s: Flaky MAC addr registers",
			    qlcnic_driver_name);
			break;

		case QLCNIC_XGBE:
			ret = qlcnic_niu_xg_macaddr_set(adapter, addr);
			break;

		default:
			cmn_err(CE_WARN,  "\r\nUnknown board type encountered"
			    " while setting the MAC address.");
			return (-1);
	}
	return (ret);
}

#define	MTU_FUDGE_FACTOR 100
int
qlcnic_set_mtu(struct qlcnic_adapter_s *adapter, int new_mtu)
{
	u32 port = adapter->physical_port;
	int ret = 0;
	u32 port_mode = 0;

	if (adapter->ahw.revision_id >= QLCNIC_P3_A2)
		return (qlcnic_fw_cmd_set_mtu(adapter, new_mtu));

	new_mtu += MTU_FUDGE_FACTOR; /* so that MAC accepts frames > MTU */
	switch (adapter->ahw.board_type) {
		case QLCNIC_GBE:
			qlcnic_write_w0(adapter,
			    QLCNIC_NIU_GB_MAX_FRAME_SIZE(
			    adapter->physical_port),
			    new_mtu);

			break;

		case QLCNIC_XGBE:
			adapter->qlcnic_hw_read_wx(adapter,
			    QLCNIC_PORT_MODE_ADDR,
			    &port_mode, 4);
			if (port_mode == QLCNIC_PORT_MODE_802_3_AP) {
				qlcnic_write_w0(adapter,
				    QLCNIC_NIU_AP_MAX_FRAME_SIZE(port),
				    new_mtu);
			} else {
				if (adapter->physical_port == 0) {
					qlcnic_write_w0(adapter,
					    QLCNIC_NIU_XGE_MAX_FRAME_SIZE,
					    new_mtu);
				} else {
					qlcnic_write_w0(adapter,
					    QLCNIC_NIU_XG1_MAX_FRAME_SIZE,
					    new_mtu);
				}
			}
			break;

		default:
			cmn_err(CE_WARN, "%s: Unknown brdtype.",
			    qlcnic_driver_name);
	}

	return (ret);
}

int
qlcnic_set_promisc_mode(struct qlcnic_adapter_s *adapter)
{
	int ret;

	if (adapter->promisc)
		return (0);

	switch (adapter->ahw.board_type) {
		case QLCNIC_GBE:
			ret = qlcnic_niu_set_promiscuous_mode(adapter,
			    QLCNIC_NIU_PROMISCOUS_MODE);
			break;

		case QLCNIC_XGBE:
			ret = qlcnic_niu_xg_set_promiscuous_mode(adapter,
			    QLCNIC_NIU_PROMISCOUS_MODE);
			break;

		default:
			ret = -1;
			break;
	}

if (!ret)
	adapter->promisc = 1;

		return (ret);
}

int
qlcnic_unset_promisc_mode(struct qlcnic_adapter_s *adapter)
{
	int ret = 0;

	/*
	 * P3 does not unset promiscous mode. Why?
	 */
	if (adapter->ahw.revision_id >= QLCNIC_P3_A2) {
		return (0);
	}

	if (!adapter->promisc)
		return (0);

	switch (adapter->ahw.board_type) {
		case QLCNIC_GBE:
			ret = qlcnic_niu_set_promiscuous_mode(adapter,
			    QLCNIC_NIU_NON_PROMISCOUS_MODE);
			break;

		case QLCNIC_XGBE:
			ret = qlcnic_niu_xg_set_promiscuous_mode(adapter,
			    QLCNIC_NIU_NON_PROMISCOUS_MODE);
			break;

		default:
			ret = -1;
			break;
	}

	if (!ret)
		adapter->promisc = 0;

	return (ret);
}

int
qlcnic_phy_read(qlcnic_adapter *adapter, uint32_t reg,
    uint32_t *readval)
{
	u32 ret = 0;

	switch (adapter->ahw.board_type) {
	case QLCNIC_GBE:
		ret = qlcnic_niu_gbe_phy_read(adapter, reg, readval);
		break;

	case QLCNIC_XGBE:
		cmn_err(CE_WARN, "%s: Function %s is not implemented for XG",
		    qlcnic_driver_name, __FUNCTION__);
		break;

	default:
		cmn_err(CE_WARN,
		    "qlcnic_phy_read failed, unknown board type");
	}

	return (ret);
}

int
qlcnic_init_port(struct qlcnic_adapter_s *adapter)
{
	u32 portnum = adapter->physical_port;
	u32 ret = 0;
	u32 reg = 0;
	qlcnic_niu_gbe_ifmode_t mode_dont_care = 0;
	u32 port_mode = 0;

	qlcnic_set_link_parameters(adapter);

	if (QLCNIC_IS_REVISION_P3(adapter->ahw.revision_id)) {
		/* do via fw */
		return (0);
	}

	switch (adapter->ahw.board_type) {
	case QLCNIC_GBE:
		ret = qlcnic_niu_enable_gbe_port(adapter, mode_dont_care);
		break;

	case QLCNIC_XGBE:
		adapter->qlcnic_hw_read_wx(adapter, QLCNIC_PORT_MODE_ADDR,
		    &port_mode, 4);
		if (port_mode == QLCNIC_PORT_MODE_802_3_AP) {
			ret =
			    qlcnic_niu_enable_gbe_port(adapter, mode_dont_care);
		} else {
			adapter->qlcnic_crb_writelit_adapter(adapter,
			    QLCNIC_NIU_XGE_CONFIG_0 + (0x10000 * portnum), 0x5);
			QLCNIC_CRB_READ_CHECK_ADAPTER(QLCNIC_NIU_XGE_CONFIG_1 +
			    (0x10000 * portnum), &reg, adapter);
			if (adapter->ahw.revision_id < QLCNIC_P3_A2)
				reg = (reg & ~0x2000UL);
			adapter->qlcnic_crb_writelit_adapter(adapter,
			    QLCNIC_NIU_XGE_CONFIG_1 + (0x10000 * portnum), reg);
		}
		break;

	default:
		cmn_err(CE_WARN, "init_port failed, unknown board type %x",
		    adapter->ahw.board_type);
	}

	return (ret);
}

void
qlcnic_stop_port(struct qlcnic_adapter_s *adapter)
{
	if (QLCNIC_IS_REVISION_P3(adapter->ahw.revision_id))
		return;

	switch (adapter->ahw.board_type) {
	case QLCNIC_GBE:
		(void) qlcnic_niu_disable_gbe_port(adapter);
		break;

	case QLCNIC_XGBE:
		(void) qlcnic_niu_disable_xg_port(adapter);
		break;

	default:
		cmn_err(CE_WARN, "stop_port failed, unknown board type %x",
		    adapter->ahw.board_type);
	}
}

void
qlcnic_crb_write_adapter(unsigned long off, void *data,
    struct qlcnic_adapter_s *adapter)
{
	(void) adapter->qlcnic_hw_write_wx(adapter, off, data, 4);
}

int
qlcnic_crb_read_adapter(unsigned long off, void *data,
    struct qlcnic_adapter_s *adapter)
{
	return (adapter->qlcnic_hw_read_wx(adapter, off, data, 4));
}

int
qlcnic_crb_read_val_adapter(unsigned long off, struct qlcnic_adapter_s *adapter)
{
	int data;

	adapter->qlcnic_hw_read_wx(adapter, off, &data, 4);
	return (data);
}

void
qlcnic_set_link_parameters(struct qlcnic_adapter_s *adapter)
{
	qlcnic_niu_phy_status_t status;
	uint16_t defval = (uint16_t)-1;
	qlcnic_niu_control_t mode;
	u32 port_mode = 0, val1;

	if (QLCNIC_IS_REVISION_P3(adapter->ahw.revision_id)) {
		adapter->qlcnic_hw_read_wx(adapter,
		    PF_LINK_SPEED_REG(adapter->ahw.pci_func), &val1, 4);
		adapter->link_speed = PF_LINK_SPEED_VAL(adapter->ahw.pci_func,
		    val1) * PF_LINK_SPEED_MHZ;
		adapter->link_duplex = LINK_DUPLEX_FULL;
		if ((adapter->link_speed == 0) &&
		    (adapter->ahw.board_type == QLCNIC_XGBE)) {
			adapter->link_duplex = LINK_DUPLEX_FULL;
			adapter->link_speed = QLCNIC_XGBE_LINK_SPEED;
		}
		return;
	}


	qlcnic_read_w0(adapter, QLCNIC_NIU_MODE, (uint32_t *)&mode);
	if (mode.enable_ge) { /* Gb 10/100/1000 Mbps mode */
		adapter->qlcnic_hw_read_wx(adapter, QLCNIC_PORT_MODE_ADDR,
		    &port_mode, 4);
		if (port_mode == QLCNIC_PORT_MODE_802_3_AP) {
			adapter->link_speed = MBPS_1000;
			adapter->link_duplex = LINK_DUPLEX_FULL;
		} else {
		if (qlcnic_phy_read(adapter,
		    QLCNIC_NIU_GB_MII_MGMT_ADDR_PHY_STATUS,
		    (qlcnic_crbword_t *)&status) == 0) {
			if (status.link) {
				switch (status.speed) {
				case 0: adapter->link_speed = MBPS_10;
					break;
				case 1: adapter->link_speed = MBPS_100;
					break;
				case 2: adapter->link_speed = MBPS_1000;
					break;
				default:
					adapter->link_speed = defval;
					break;
				}
				switch (status.duplex) {
				case 0: adapter->link_duplex = LINK_DUPLEX_HALF;
					break;
				case 1: adapter->link_duplex = LINK_DUPLEX_FULL;
					break;
				default:
					adapter->link_duplex = defval;
					break;
				}
			} else {
				adapter->link_speed = defval;
				adapter->link_duplex = defval;
			}
		} else {
			adapter->link_speed = defval;
			adapter->link_duplex = defval;
		}
		}
	}
}

void
qlcnic_flash_print(struct qlcnic_adapter_s *adapter)
{
	int valid = 1;
	qlcnic_board_info_t *board_info = &(adapter->ahw.boardcfg);

	if (board_info->magic != QLCNIC_BDINFO_MAGIC) {
		cmn_err(CE_WARN, "%s UNM Unknown board config, Read 0x%x "
		    "expected as 0x%x", qlcnic_driver_name,
		    board_info->magic, QLCNIC_BDINFO_MAGIC);
		valid = 0;
	}

	if (valid) {
		qlcnic_user_info_t  user_info;
		int	i;
		int	addr = USER_START;
		int	*ptr32;

		ptr32 = (int *)&user_info;
		for (i = 0; i < sizeof (qlcnic_user_info_t) / sizeof (uint32_t);
		    i++) {
			if (qlcnic_rom_fast_read(adapter, addr, ptr32) == -1) {
				cmn_err(CE_WARN,
				    "%s: ERROR reading %s board userarea.",
				    qlcnic_driver_name, qlcnic_driver_name);
				return;
			}
			ptr32++;
			addr += sizeof (uint32_t);
		}
		if (verbmsg != 0) {
			char	*brd_name;
			GET_BRD_NAME_BY_TYPE(board_info->board_type, brd_name);
			cmn_err(CE_NOTE, "%s %s Board, S/N %s, Chip id 0x%x\n",
			    qlcnic_driver_name, brd_name, user_info.serial_num,
			    board_info->chip_id);
		}
	}
}

/* send request to fw using default tx ring */
static int
qlcnic_send_cmd_descs(qlcnic_adapter *adapter, cmdDescType0_t *cmd_desc_arr,
    int nr_elements)
{
	struct qlcnic_cmd_buffer *pbuf;
	unsigned int i = 0;
	unsigned int producer;
	struct qlcnic_tx_ring_s *tx_ring = &(adapter->tx_ring[0]);

	/*
	 * We need to check if space is available.
	 */
	mutex_enter(&tx_ring->tx_lock);
	producer = tx_ring->cmdProducer;

	do {
		pbuf = &tx_ring->cmd_buf_arr[producer];
		pbuf->head = pbuf->tail = NULL;
		pbuf->msg = NULL;
		(void) memcpy(&tx_ring->cmdDescHead[producer],
		    &cmd_desc_arr[i], sizeof (cmdDescType0_t));
		qlcnic_desc_dma_sync(tx_ring->cmd_desc_dma_handle, producer,
		    1, adapter->MaxTxDescCount, sizeof (cmdDescType0_t),
		    DDI_DMA_SYNC_FORDEV);
		producer = get_next_index(producer, adapter->MaxTxDescCount);
		i++;
	} while (i != nr_elements);

	tx_ring->cmdProducer = producer;
	tx_ring->freecmds -= i;

	qlcnic_update_cmd_producer(tx_ring, producer);

	mutex_exit(&tx_ring->tx_lock);
	return (0);
}

typedef struct {
	u64	qhdr, req_hdr, words[6];
} qlcnic_req_t;

typedef struct {
	u8	op, tag, mac_addr[6];
} qlcnic_mac_req_t;

int
qlcnic_p3_sre_macaddr_change(qlcnic_adapter *adapter, u8 *addr, u8 op)
{
	qlcnic_req_t req;
	qlcnic_mac_req_t *mac_req;
	int rv;
	u64 word;

	(void) memset(&req, 0, sizeof (qlcnic_req_t));
	req.qhdr = HOST_TO_LE_64(((u64)QLCNIC_REQUEST) << 23);

	word = QLCNIC_MAC_EVENT | (((u64)adapter->portnum) << 16);
	req.req_hdr = HOST_TO_LE_64(word);

	mac_req = (qlcnic_mac_req_t *)&req.words[0];
	mac_req->op = op;
	(void) memcpy(mac_req->mac_addr, addr, 6);

	rv = qlcnic_send_cmd_descs(adapter, (cmdDescType0_t *)&req, 1);
	if (rv != 0) {
		cmn_err(CE_WARN, "%s%d: Could not send mac update: rv %d",
		    adapter->name, adapter->instance, rv);
	}

	return (rv);
}

static int
qlcnic_p3_nic_set_promisc(qlcnic_adapter *adapter, u32 mode)
{
	qlcnic_req_t req;
	u64 word;

	(void) memset(&req, 0, sizeof (qlcnic_req_t));

	req.qhdr = HOST_TO_LE_64((((u64)QLCNIC_HOST_REQUEST) << 23));
	word = QLCNIC_H2C_OPCODE_PROXY_SET_VPORT_MISS_MODE |
	    ((u64)adapter->portnum << 16);
	req.req_hdr = HOST_TO_LE_64(word);
	req.words[0] = HOST_TO_LE_64((u64)mode);

	return (qlcnic_send_cmd_descs(adapter, (cmdDescType0_t *)&req, 1));
}

/*
 * Currently only invoked at interface initialization time
 */
void
qlcnic_p3_nic_set_multi(qlcnic_adapter *adapter)
{
	u8 bcast_addr[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	int ret;

	adapter->unicst_avail = MAX_UNICAST_LIST_SIZE;

	DPRINTF(DBG_INIT, (CE_NOTE, "qlcnic_p3_nic_set_multi(%d) entered",
	    adapter->instance));
	/* Remove this when we support multicast in H/W */
	if (qlcnic_p3_nic_set_promisc(adapter, VPORT_MISS_MODE_ACCEPT_ALL))
		cmn_err(CE_WARN, "Could not set promisc mode");

	ret = qlcnic_p3_sre_macaddr_change(adapter, adapter->mac_addr,
	    QLCNIC_MAC_ADD);
	if (ret == 0) {
		bcopy(adapter->mac_addr,
		    adapter->unicst_addr[0].addr.ether_addr_octet,
		    ETHERADDRL);
		adapter->unicst_addr[0].set = 1;
		adapter->unicst_avail--;
	} else {
		cmn_err(CE_WARN,
		    "qlcnic(%d) sre_macaddr_change failed to add mac address",
		    adapter->instance);
	}

	ret = qlcnic_p3_sre_macaddr_change(adapter, bcast_addr, QLCNIC_MAC_ADD);
	if (ret != 0) {
		cmn_err(CE_WARN,
		    "qlcnic(%d) sre_macaddr_change failed to add bcast address",
		    adapter->instance);
	}
}

int
qlcnic_configure_rss(qlcnic_adapter *adapter)
{
	int i;
	qlcnic_req_t rss_req;
	uint64_t word;
	uint64_t hash_key[] = { 0xbeac01fa6a42b73bULL, 0x8030f20c77cb2da3ULL,
			0xae7b30b4d0ca2bcbULL, 0x43a38fb04167253dULL,
			0x255b0ec26d5a56daULL };

	(void) memset(&rss_req, 0, sizeof (rss_req));

	rss_req.qhdr = HOST_TO_LE_64((uint64_t)QLCNIC_HOST_REQUEST << 23);

	word = QLCNIC_H2C_OPCODE_CONFIG_RSS |
	    ((uint64_t)adapter->portnum << 16);
	rss_req.req_hdr = HOST_TO_LE_64(word);
	word =  ((uint64_t)(RSS_HASHTYPE_IP_TCP & 0x3) << 4) |
	    ((uint64_t)(RSS_HASHTYPE_IP_TCP & 0x3) << 6) |
	    ((uint64_t)0x1 << 8) |
	    ((uint64_t)0x7 << 48);
	rss_req.words[0] = HOST_TO_LE_64(word);
	for (i = 0; i < sizeof (hash_key) / sizeof (uint64_t); i++) {
		rss_req.words[i+1] = HOST_TO_LE_64(hash_key[i]);
	}

	cmn_err(CE_NOTE, "!%s%d: Programming RSS mode",
	    adapter->name, adapter->instance);
	/* Send it to the f/w */
	return (qlcnic_send_cmd_descs(adapter, (cmdDescType0_t *)&rss_req, 1));
}

int
qlcnic_configure_ip_addr(struct qlcnic_tx_ring_s *tx_ring, uint32_t ip_addr)
{
	qlcnic_adapter *adapter = tx_ring->adapter;
	qlcnic_req_t *ip_req;
	uint64_t word;
	struct qlcnic_cmd_buffer *pbuf;
	uint32_t producer, next_producer;

	producer = tx_ring->cmdProducer;
	next_producer = get_next_index(producer, adapter->MaxTxDescCount);
	if (next_producer == tx_ring->lastCmdConsumer) {
		cmn_err(CE_WARN, "%s%d: Out of transmit descriptors",
		    adapter->name, adapter->instance);
		return (-1);
	}
	ip_req = (qlcnic_req_t *)&tx_ring->cmdDescHead[producer];
	(void) memset(ip_req, 0, sizeof (*ip_req));

	ip_req->qhdr = HOST_TO_LE_64((uint64_t)QLCNIC_HOST_REQUEST << 23);

	word = QLCNIC_H2C_OPCODE_CONFIG_IPADDR |
	    ((uint64_t)adapter->portnum << 16);
	ip_req->req_hdr = HOST_TO_LE_64(word);
	ip_req->words[0] = HOST_TO_LE_64((uint64_t)QLCNIC_IP_UP);
	ip_req->words[1] = HOST_TO_LE_64((uint64_t)ip_addr);

	cmn_err(CE_NOTE, "!%s%d: IP address set to 0x%x", adapter->name,
	    adapter->instance, ip_addr);

	/*
	 * Send it to the f/w, lock is already held so cannot
	 * use qlcnic_send_cmd_descs()
	 */

	/*
	 * We need to check if space is available.
	 */
	pbuf = &tx_ring->cmd_buf_arr[producer];
	pbuf->head = pbuf->tail = NULL;
	pbuf->msg = NULL;
	qlcnic_desc_dma_sync(tx_ring->cmd_desc_dma_handle, producer,
	    1, adapter->MaxTxDescCount, sizeof (cmdDescType0_t),
	    DDI_DMA_SYNC_FORDEV);
	producer = get_next_index(producer, adapter->MaxTxDescCount);

	tx_ring->cmdProducer = producer;
	tx_ring->freecmds --;
	qlcnic_update_cmd_producer(tx_ring, producer);

	return (0);
}

int
qlcnic_configure_intr_coalesce(qlcnic_adapter *adapter)
{
	qlcnic_req_t coalesce_req;
	uint64_t word;

	(void) memset(&coalesce_req, 0, sizeof (coalesce_req));

	coalesce_req.qhdr = HOST_TO_LE_64((uint64_t)QLCNIC_HOST_REQUEST << 23);

	word = QLCNIC_H2C_OPCODE_CONFIG_INTR_COALESCE |
	    ((uint64_t)adapter->portnum << 16);
	coalesce_req.req_hdr = HOST_TO_LE_64(word);

	(void) memcpy(&coalesce_req.words[0], &adapter->coal,
	    sizeof (adapter->coal));

	cmn_err(CE_WARN, "%s%d: Programming interrupt coalescing",
	    adapter->name, adapter->instance);
	/* Send it to the f/w */
	return (qlcnic_send_cmd_descs(adapter, (cmdDescType0_t *)&coalesce_req,
	    1));
}

static int qlcnic_set_fw_loopback(qlcnic_adapter *adapter, u32 flag)
{
	qlcnic_req_t req;
	uint64_t word;

	(void) memset(&req, 0, sizeof (qlcnic_req_t));
	req.qhdr = HOST_TO_LE_64((uint64_t)QLCNIC_HOST_REQUEST << 23);

	word = QLCNIC_H2C_OPCODE_CONFIG_LOOPBACK |
	    ((uint64_t)adapter->portnum << 16);
	req.req_hdr = HOST_TO_LE_64(word);
	req.words[0] = HOST_TO_LE_64((uint64_t)flag);

	return (qlcnic_send_cmd_descs(adapter, (cmdDescType0_t *)&req, 1));
}

int
qlcnic_set_ilb_mode(qlcnic_adapter *adapter)
{
	if (qlcnic_set_fw_loopback(adapter, 1))
		return (DDI_FAILURE);

	if (qlcnic_p3_nic_set_promisc(adapter, VPORT_MISS_MODE_ACCEPT_ALL)) {
		(void) qlcnic_set_fw_loopback(adapter, 0);
		return (DDI_FAILURE);
	}

	qlcnic_msleep(1000);
	return (0);
}

void
qlcnic_clear_ilb_mode(qlcnic_adapter *adapter)
{
	int mode = VPORT_MISS_MODE_DROP;

	(void) qlcnic_set_fw_loopback(adapter, 0);

	/* force to accept all right now */
	mode = VPORT_MISS_MODE_ACCEPT_ALL;
	(void) qlcnic_p3_nic_set_promisc(adapter, mode);
}
int
qlcnic_config_led_blink(qlcnic_adapter *adapter, u32 flag)
{
	qlcnic_req_t req;
	uint64_t word;
	u32 rate = 0xf;
	(void) memset(&req, 0, sizeof (req));
	req.qhdr = HOST_TO_LE_64((uint64_t)QLCNIC_HOST_REQUEST << 23);

	word = QLCNIC_H2C_OPCODE_CONFIG_LED |
	    ((uint64_t)adapter->portnum << 16);
	req.req_hdr = HOST_TO_LE_64(word);
	req.words[0] = HOST_TO_LE_64((uint64_t)rate << 32);
	req.words[1] = HOST_TO_LE_64((uint64_t)flag);

	cmn_err(CE_WARN, "%s%d: Programming LED blinking",
	    adapter->name, adapter->instance);
	/* Send it to the f/w */
	return (qlcnic_send_cmd_descs(adapter, (cmdDescType0_t *)&req, 1));
}
