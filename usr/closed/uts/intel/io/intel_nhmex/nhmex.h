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
 *
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 *
 */

#ifndef	_NHMEX_H
#define	_NHMEX_H

#ifdef __cplusplus
extern "C" {
#endif

#define	NHMEX_EX_CPU	0x2b008086
#define	NHMEX_MB	0x03808086

#define	CHANNELS_PER_MEMORY_CONTROLLER	2
#define	MAX_CPU_MEMORY_CONTROLLERS	2
#define	CHANNELS_LOCKSTEP		2
#define	MAX_DIMMS_PER_CHANNEL		4
#define	MAX_RANKS_PER_DIMM		4
#define	MAX_FBDIMMS_PER_CHANNEL		4
#define	MAX_RANKS_PER_FBDIMM		2
#define	MAX_RANKS_PER_CHANNEL\
	(MAX_FBDIMMS_PER_CHANNEL * MAX_RANKS_PER_FBDIMM)
#define	MAX_CPU_NODES			256
#define	CPU_PCI_DEVS			25
#define	CPU_PCI_FUNCS			7
#define	BASE_HI_MEMORY	0x100000000ULL
#define	TOP_MEMORY_LO	0xf0000000ULL
#define	LOCKSTEP_RANKS	2
#define	REGION_MASK	0xfffffffff0000000ULL
#define	INTERLEAVE_WAY(pa) ((pa >> 6) & 7)

#define	DIMM_NUM(slot, mem, channel, dimm) \
	(((slot) * (MAX_CPU_MEMORY_CONTROLLERS * \
	CHANNELS_PER_MEMORY_CONTROLLER * MAX_DIMMS_PER_CHANNEL)) + \
	((mem) * (CHANNELS_PER_MEMORY_CONTROLLER * MAX_DIMMS_PER_CHANNEL)) + \
	((channel) * (MAX_DIMMS_PER_CHANNEL)) + (dimm))

#define	MAX_BUS_NUMBER	max_bus_number
extern int max_bus_number;
#define	SOCKET_BUS(id) (MAX_BUS_NUMBER - (id))

#define	CPU_ID_RD(id)	nhmex_pci_getl(SOCKET_BUS(id), 0, 0, 0, 0)
#define	M_CSR_FBD_POP_CTL(id, branch) \
	nhmex_pci_getl(SOCKET_BUS(id), (branch) ? 7 : 5, 0, 0xb8, 0)
#define	FBDIMM_PRESENT(reg, dimm)	((reg) & (1 << (dimm)))
#define	NDIMMS(reg) ((((reg) >> 12) & 7) + 1)
#define	DDR_TYPE(reg)	((((reg) >> 15) & 1) ? "DDR3" : "DDR2")
#define	M_PCSR_MAP_PHYS_DIMM(id, branch, reg) nhmex_pci_getl(SOCKET_BUS(id), \
	(branch) ? 7 : 5, 2, reg ? 0x48 : 0x44, 0)
#define	PDMAP(reg, num)		(((reg) >> ((num) * 5)) & 0x1f)
#define	PHYDIMM(reg, num)	(((reg) >> (((num) * 3) + 20)) & 7)
#define	M_PCSR_MAP_0(id, branch, num) \
	nhmex_pci_getl(SOCKET_BUS(id), (branch) ? 7 : 5, 0, 0xe0 + (num * 4), 0)
#define	M_PCSR_MAP_1(id, branch, num) \
	nhmex_pci_getl(SOCKET_BUS(id), (branch) ? 7 : 5, 0, 0xf0 + (num * 4), 0)
#define	DIMM_SPC(reg) (((reg) >> 4) & 1)
#define	DIMM_BANK(reg) (((reg) >> 14) & 0xf)
#define	DIMM(reg) ((reg) & 0xf)
#define	STACKED_RANK(reg) (((reg) >> 8) & 0xf)
#define	SRANK_BIT0_USE(reg) (((reg) >> 12) & 3)
#define	SRANK_BIT1_USE(reg) (((reg) >> 22) & 3)
#define	DIMM_BIT0_USE(reg) (((reg) >> 5) & 1)
#define	DIMM_BIT1_USE(reg) (((reg) >> 6) & 1)
#define	DIMM_BIT2_USE(reg) (((reg) >> 7) & 1)
#define	RANK_2X(reg) (((reg) >> 24) & 1)
#define	BIT_USED	2
#define	M_PCSR_MAP_OPEN_CLOSED(id, branch) \
	nhmex_pci_getl(SOCKET_BUS(id), (branch) ? 7 : 5, 2, 0x40, 0)
#define	OPEN_MAP(reg) ((reg) & 1)

#define	SPD_DEV_SZ	0
#define	SPD_DEV_TYPE	2
#define	TYPE_DDR2	8
#define	TYPE_DDR3	0xb
#define	DIMMWIDTH	8

#define	B_PCSR_TAD_CTL_REG_WR(id, branch, val) \
	nhmex_pci_putl(SOCKET_BUS(id), (branch) ? 6 : 4, 0, 0x70, val)
#define	MAP_LIMIT_READ(select)	(1 | (2 << 2) | ((select) << 4))
#define	TAD_LIMIT_READ(select)	(1 | (1 << 2) | ((select) << 4))
#define	TAD_PAYLOAD_READ(select)	(1 | ((select) << 4))
#define	B_PCSR_TAD_RDDATA_REG_RD(id, branch) \
	nhmex_pci_getl(SOCKET_BUS(id), (branch) ? 6 : 4, 0, 0x74, 0)
#define	TAD_LIMIT(rddata) ((((uint64_t)(rddata) & 0xffff) + 1) << 28)
#define	TAD_OFFSET(rddata) ((((int64_t)(rddata) & 0xffff) << 52) >> 24)
#define	TAD_INTERLEAVE(rddata) (1 << (((rddata) >> 16) & 3))
#define	TAD_NXM(rddata) (((rddata) >> 18) & 1)
#define	MAP_LIMIT(rddata) ((((uint64_t)(rddata) & 0x3fff) + 1) << 30)
#define	MAX_MAP_LIMIT	3
#define	MAX_TAD		10
#define	B_PCSR_MEM_MIRROR_REG_RD(id, branch) \
	nhmex_pci_getl(SOCKET_BUS(id), (branch) ? 6 : 4, 0, 0x6c, 0)
#define	MIRRORING(reg) ((((reg) >> 10) & 3) == 2)
#define	MIGRATION(reg) ((((reg) >> 10) & 3) == 3)

/*
 * PCSR_SMB_STATUS
 */
#define	SPD_RD(id, branch, channel) \
	nhmex_pci_getl(SOCKET_BUS(id), 0, 2, 0x84, 0)
#define	SPD_BUSY		0x1000
#define	SPD_BUS_ERROR		0x2000
#define	SPD_READ_DATA_VALID	0x8000
/*
 * SAD
 */
#define	PCSR_SADARYID_WR(id, did, eid) \
	nhmex_pci_putl(SOCKET_BUS(id), 8, 0, 0xf0, ((did) << 5) | (eid))
#define	SAD_DRAM	0
#define	SAD_IO		1
#define	SAD_NDECODE_DRAM	20
#define	SAD_NDECODE_IO		8
#define	PCSR_SADPLDARY_RD(id) \
	nhmex_pci_getl(SOCKET_BUS(id), 8, 0, 0xf4, 0)
#define	SADPLDARY_NS(sadldary, idx)	(((sadldary) >> (idx * 4)) & 0xf)
#define	MAX_INTERLEAVE	8
#define	PCSR_SADPRMARY_RD(id) \
	nhmex_pci_getl(SOCKET_BUS(id), 8, 0, 0xf8, 0)
#define	SADPRMARY_HEMISPHERE(sadprmary)	(((sadprmary) >> 4) & 1)
#define	PCSR_SADCAMARY(id, idx) \
	(((uint64_t)nhmex_pci_getl(SOCKET_BUS(id), 8, 0, 0x80 + ((idx) * 4), \
	0) + 1) << 28)

/*
 * PCSR_PAGE_POLICY
 */
#define	PAGE_POLICY_RD(id, mc) \
	nhmex_pci_getl(SOCKET_BUS(id), (mc) ? 7 : 5, 2, 0x7c, 0)
#define	PAGE_MODE(page_policy)	((page_policy) & 3)
#define	OPEN_PAGE(page_policy) (((page_policy) & 3) == 2)
#define	CLOSED_PAGE(page_policy) (((page_policy) & 3) == 0)
#define	ADAPTIVE_PAGE(page_policy) (((page_policy) & 3) == 3)
/*
 * PCSR_SMB_CMD
 */
#define	SPDCMD_WR(id, branch, channel, val) \
	nhmex_pci_putl(SOCKET_BUS(id), 0, 2, 0x80, val)
#define	SPD_EEPROM_WRITE	0xa0000000
#define	SPD_ADDR(slave, addr) ((((slave) & 7) << 24) | (((addr) & 0xff) << 16))

#define	NHMEX_INTERCONNECT	"Intel QuickPath"

#define	PCSR_NB_REG_DATA_RD(bus, mc, smi) \
	nhmex_pci_getl(bus, (mc) ? 7 : 5, 2, (smi) ? 0x74 : 0x70, 0)
#define	PCSR_NB_REG_DATA_VAL_RD(bus, mc) \
	nhmex_pci_getl(bus, (mc) ? 7 : 5, 2, 0x78, 0)
#define	PCSR_NB_REG_DATA_VAL_WR(bus, mc, val) \
	nhmex_pci_putl(bus, (mc) ? 7 : 5, 2, 0x78, val)
#define	PCSR_FBD_CMD_RD(bus, mc, cmd) \
	nhmex_pci_getl(bus, (mc) ? 7 : 5, 0, 0x94 + ((cmd) * 4), 0)
#define	PCSR_FBD_CMD_WR(bus, mc, cmd, val) \
	nhmex_pci_putl(bus, (mc) ? 7 : 5, 0, 0x94 + ((cmd) * 4), val)

#define	MB_PCICFG_READ(func, ds, offset) \
	(0x10000 | ((func) << 8) | \
	((ds & 7) << 21) | (((ds >> 3) & 1) << 13) | \
	(offset))
#define	FBD_CMD_CFRD	0x7e0ceb1
#define	FBD_CMD_ERROR	2
#define	FBD_CMD_ACTIVE	1

#define	MB_MTR_RD(slot, mem, mb, dimm, datap) \
	nhmex_mb_rd(SOCKET_BUS(slot), mem, mb, dimm >= 2 ? 1 : 0, 2, \
	(dimm & 1) != 0 ? 0x3a : 0x38, datap)

#define	DDR3_DIMM_PRESENT(mtr)	(((mtr) >> 11) & 1)
#define	DIMM_WIDTH(mtr)	((((mtr) >> 9) & 3) ? "x8" : "x4")
#define	NUMRANK(mtr) ((((mtr) >> 7) & 3) + 1)
#define	NUMBANK(mtr) 8
#define	NUMROW(mtr) ((((mtr) >> 2) & 7) + 9)
#define	NUMCOL(mtr) (((mtr) & 3) + 10)

#define	DIMMSIZE(mtr)	((1ULL << (NUMCOL(mtr) + NUMROW(mtr))) \
	* NUMRANK(mtr) * NUMBANK(mtr) * DIMMWIDTH)

#define	MB_CSCTL_RD(slot, mem, mb, dimm, datap) \
	nhmex_mb_rd(SOCKET_BUS(slot), mem, mb, dimm, 3, 0x64, datap)
#define	CSCTL_CS_ENABLE(csctl, cs)	(((csctl) >> (((cs) * 4)) + 3) & 1)
#define	CSCTL_CS_ID(csctl, cs)	(((csctl) >> ((cs) * 4)) & 7)

#define	CS_PHYS_CTL_RD(bus, pci_dev, cs) \
	nhmex_pci_getw(bus, pci_dev, 3, (cs) + 0xd0, 0)

#define	MB_CSMASK(slot, mem, mb, datap) \
	nhmex_mb_rd(SOCKET_BUS(slot), mem, mb, 0, 3, 0x68, datap)
#define	CS_MASK(csmask, cs)	(((csmask) >> (cs * 2)) & 3)
#define	MASK_QR_DIMM 2
#define	DIMM_ID(cs_phys_ctl)	(((cs_phys_ctl) >> 5) & 3)
#define	CKE_ID(cs_phys_ctl)	((cs_phys_ctl) & 7)

#ifdef __cplusplus
}
#endif

#endif	/* _NHMEX_H */
