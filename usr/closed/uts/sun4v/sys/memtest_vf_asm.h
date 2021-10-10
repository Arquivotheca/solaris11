/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _MEMTEST_ASM_VF_H
#define	_MEMTEST_ASM_VF_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#define	VF_SYS_MODE_REG			0x8100000008ULL

#define	VF_SYS_MODE_NODEID_MASK		0x30	/* bits[5:4] */
#define	VF_SYS_MODE_NODEID_SHIFT	4

#define	VF_SYS_MODE_GET_NODEID(reg)	(((reg) & VF_SYS_MODE_NODEID_MASK) \
					>> VF_SYS_MODE_NODEID_SHIFT)

#define	VF_SYS_MODE_WAY_MASK		0x380	/* bits[9:7] */
#define	VF_SYS_MODE_WAY_SHIFT		7
#define	VF_SYS_MODE_1_WAY		0x0	/* bits[9:7] == 0 */
#define	VF_SYS_MODE_2_WAY		0x4	/* bit[9] == 1 */
#define	VF_SYS_MODE_3_WAY		0x2	/* bit[8] == 1 */
#define	VF_SYS_MODE_4_WAY		0x1	/* bit[7] == 1 */

#define	VF_SYS_MODE_GET_WAY(reg)	(((reg) & VF_SYS_MODE_WAY_MASK) \
					>> VF_SYS_MODE_WAY_SHIFT)

#define	VF_L2_CTL_REG			0xa900000000ULL

#define	VF_L2_CTL_CM_MASK		0x1fe000000ULL	/* bits[32:25] */
#define	VF_L2_CTL_CM_SHIFT		25ULL

#define	VF_CEILING_MASK(reg)		((reg & VF_L2_CTL_CM_MASK) >> \
					VF_L2_CTL_CM_SHIFT)

/*
 * Bits from a physical memory address needed for comparison with
 * the ceiling mask in order to determine whether the address falls
 * in the range of memory interleaved on 512B boundaries or 1GB
 * boundaries.
 */
#define	VF_ADDR_INTERLEAVE_MASK		0xff00000000ULL	/* bits[39:32] */
#define	VF_ADDR_INTERLEAVE_SHIFT	32ULL

#define	VF_ADDR_INTERLEAVE_BITS(paddr) \
		(((paddr) & VF_ADDR_INTERLEAVE_MASK) >> \
		VF_ADDR_INTERLEAVE_SHIFT)

#define	VF_IS_512B_INTERLEAVE(reg, paddr) \
		((VF_ADDR_INTERLEAVE_BITS(paddr) >= \
		VF_CEILING_MASK(reg)) ? 0 : 1)

/*
 * Bits from a physical memory address needed to determine its node id
 */
#define	VF_2WY_512B_ADDR_NODEID_MASK	0x200		/* bit[9] */
#define	VF_4WY_512B_ADDR_NODEID_MASK	0x600		/* bits[10:9] */
#define	VF_512B_ADDR_NODEID_SHIFT	9

#define	VF_2WY_512B_ADDR_NODEID(paddr)	\
	(((paddr) & VF_2WY_512B_ADDR_NODEID_MASK) >> VF_512B_ADDR_NODEID_SHIFT)

#define	VF_4WY_512B_ADDR_NODEID(paddr)	\
	(((paddr) & VF_4WY_512B_ADDR_NODEID_MASK) >> VF_512B_ADDR_NODEID_SHIFT)

#define	VF_2WY_1GB_ADDR_NODEID_MASK	0x40000000	/* bit[30] */
#define	VF_4WY_1GB_ADDR_NODEID_MASK	0xc0000000	/* bits[31:30] */
#define	VF_1GB_ADDR_NODEID_SHIFT	30

#define	VF_2WY_1GB_ADDR_NODEID(paddr)	\
	(((paddr) & VF_2WY_1GB_ADDR_NODEID_MASK) >> VF_1GB_ADDR_NODEID_SHIFT)

#define	VF_4WY_1GB_ADDR_NODEID(paddr)	\
	(((paddr) & VF_4WY_1GB_ADDR_NODEID_MASK) >> VF_1GB_ADDR_NODEID_SHIFT)

#define	VF_DRAM_BRANCH_MASK		0x100	/* paddr bit[8] */
#define	VF_DRAM_BRANCH_PA_SHIFT		0x4	/* reg stride = 4096 */
#define	VF_DRAM_BRANCH_OFFSET		0x1000

#define	VF_NUM_DRAM_BRANCHES		2

#define	VF_COU_BASE			0x8110000000ULL

#define	VF_COU_ERR_ENB_REG		(VF_COU_BASE + 0x0)
#define	VF_COU_ERR_STS_REG		(VF_COU_BASE + 0x10)
#define	VF_COU_ERREN			3
#define	VF_COU_ERREN_MASK		0xfffffffffffffffc
#define	VF_COU_COUNT			4
#define	VF_COU_STEP			0x1000

/*
 * The L2 register definitions here are new to Victoria Falls.
 * #defines for pre-exisiting registers carried over from Niagara-2 are
 * found in memtest_n2_asm.h.
 */
#define	VF_L2_ERR_ENB_TO_MASK		0xfffffc00	/* bits[31:10] */
#define	VF_L2_ERR_ENB_TO_SHIFT		10

#define	VF_L2_ND_ERR_MSB		0xae
#define	VF_L2_ND_ERR_REG		(0xaeULL << 32)

#define	VF_L2_ERR_SYND_REG_MSB		0xaf
#define	VF_L2_ERR_SYND_REG		(0xafULL << 32)

#define	VF_NCX_TWR_REG			0x8100001000ULL
#define	VF_NCX_TWR_MASK			0xfffffc00	/* bits[31:10] */
#define	VF_NCX_TWR_SHIFT		10

/*
 * LFU registers
 */
#define	VF_LFU_BASE			0x8120000000ULL

#define	VF_LFU_TRAN_STATE_TO_REG	(VF_LFU_BASE + 0x38)
#define	VF_LFU_CFG_STATE_TO_REG		(VF_LFU_BASE + 0x40)
#define	VF_LFU_SERDES_INVPAIR_REG	(VF_LFU_BASE + 0x60)
#define	VF_LFU_ERR_STS_REG		(VF_LFU_BASE + 0x80)
#define	VF_LFU_ERR_INJ1_REG		(VF_LFU_BASE + 0x90)
#define	VF_LFU_ERR_INJ2_REG		(VF_LFU_BASE + 0x98)
#define	VF_LFU_ERR_INJ3_REG		(VF_LFU_BASE + 0xa0)

#define	VF_LFU_COUNT			4
#define	VF_LFU_STEP			0x1000

#define	VF_LFU_LANE_MASK		0x3fffULL
#define	VF_LFU_NUM_LANES		14
#define	VF_LFU_NUM_UNITS		4
#define	VF_LFU_SSHOT_ENABLE		(1ULL << 40)
#define	VF_LFU_INJECTION_ENABLE		(1ULL << 41)

#ifdef	__cplusplus
}
#endif

#endif	/* _MEMTEST_ASM_VF_H */
