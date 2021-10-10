/*
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _MEMTEST_ASM_KT_H
#define	_MEMTEST_ASM_KT_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Header file for Rainbow Falls (UltraSPARC-T3 aka KT) assembly routines.
 */

/*
 * The following definitions were not in any kernel header files.
 */

/*
 * ASI definitions for the K/T injector are defined in the common header
 * file memtest_v_asm.h.
 */

/*
 * Custom hyperpriv register definitions for the K/T injector.
 */
#define	KT_L2CR_DIS		0x01		/* L2$ Disable */
#define	KT_L2CR_DMMODE		0x02		/* L2$ Direct-mapped mode */

#define	KT_SYS_MODE_GREG	0x80000001040	/* global node info */
#define	KT_SYS_MODE_LREG	0x81001040040	/* local node info */
#define	KT_SYS_MODE_REG		KT_SYS_MODE_LREG /* must use local */
#define	KT_SYS_MODE_PFLIP	0x80000000	/* plane_flip field bit[31] */
#define	KT_SYS_MODE_ID_MASK	0xf0		/* local node_id in bits[7:4] */
#define	KT_SYS_MODE_ID_SHIFT	4
#define	KT_SYS_MODE_GET_NODEID(reg)	(((reg) & KT_SYS_MODE_ID_MASK) \
					>> KT_SYS_MODE_ID_SHIFT)
#define	KT_SYS_MODE_MODE_MASK	(0x3 << 29)	/* mode in bits[30:29] */
#define	KT_SYS_MODE_MODE_SHIFT	29
#define	KT_SYS_MODE_GET_MODE(reg)	(((reg) & KT_SYS_MODE_MODE_MASK) \
					>> KT_SYS_MODE_MODE_SHIFT)
#define	KT_SYS_MODE_8MODE	0x3
#define	KT_SYS_MODE_4MODE	0x2
#define	KT_SYS_MODE_2MODE	0x1
#define	KT_SYS_MODE_1MODE	0x0

#define	KT_SYS_MODE_COUNT_MASK	(0x7 << 26)	/* node count (n - 1) [28:26] */
#define	KT_SYS_MODE_COUNT_SHIFT	26
#define	KT_CPU_NODE_ID_SHIFT	39	/* node id for global addressing */

#define	KT_DATA_NOT_FOUND	0xded

/*
 * Memory (DRAM) definitions for the K/T injector.
 */
#define	KT_DRAM_CSR_BASE_MSB 		0x804
#define	KT_DRAM_CSR_BASE  		(0x804ULL << 32)

#define	KT_DRAM_SCHD_CTL_REG		0x060	/* contains scrub enable */
#define	KT_DRAM_SCRUB_ENABLE_BIT	0x08ULL
#define	KT_DRAM_SCRUB_FREQ_REG		0x068	/* allowable mask 0xffff */
#define	KT_DRAM_SCRUB_INTERVAL_MASK	0xffff

#define	KT_DRAM_ERR_REC_REG		0x078
#define	KT_DRAM_ERR_SIG_REG		0x080
#define	KT_DRAM_ERROR_STATUS_REG	0x088
#define	KT_DRAM_ERROR_ADDR_REG		0x090
#define	KT_DRAM_ERROR_RETRY_STS_REG	0x098

#define	KT_DRAM_ERROR_SYND_REG		0x428
#define	KT_DRAM_ERROR_INJ_REG		0x430
#define	KT_DRAM_ERROR_LOC_REG		0x438
#define	KT_DRAM_ERROR_RETRY1_REG	0x468
#define	KT_DRAM_ERROR_RETRY2_REG	0x470

#define	KT_DRAM_FBD_ERROR_STATUS_REG	0xc00
#define	KT_DRAM_FBD_INJ_ERROR_REG	0xc08
#define	KT_DRAM_FBR_COUNT_REG		0xc10
#define	KT_DRAM_FBD_INJ_ENABLE		(1 << 28)
#define	KT_DRAM_FBD_SSHOT_ENABLE	(1 << 27)
#define	KT_DRAM_FBD_DIRECTION		(1 << 26)
#define	KT_DRAM_FBD_DIRECTION_SHIFT	26
#define	KT_DRAM_FBD_LINK_SHIFT		24
#define	KT_DRAM_FBD_LINK_MASK		0x3
#define	KT_DRAM_FBD_CRC_MASK		0xffffff	/* 24-bit CRC */
#define	KT_DRAM_FBD_COUNT_ENABLE_SHIFT	16
#define	KT_DRAM_FBD_COUNT_ENABLE_MASK	0x7
#define	KT_DRAM_FBD_COUNT_ENABLE_BITS	(0x7 << 16)

#define	KT_DRAM_TS3_FAILOVER_CONFIG_REG	0x828	/* same as N2 */

#define	KT_DRAM_BRANCH_MASK		0x40	/* paddr bit[6] chooses MCU */
#define	KT_DRAM_BRANCH_PA_SHIFT		0x6	/* reg stride = 4096 */
#define	KT_DRAM_BRANCH_OFFSET		0x1000	/* 4096 */
#define	KT_NUM_DRAM_BRANCHES		2

#define	KT_DRAM_INJECTION_ENABLE	(1ULL << 36)
#define	KT_DRAM_INJ_SSHOT_ENABLE	(1ULL << 35)
#define	KT_DRAM_INJ_DIRECTION		(1ULL << 34)
#define	KT_DRAM_INJ_CHUNK_SHIFT		32
#define	KT_DRAM_INJ_ECC_MASK		0xffffffff
#define	KT_DRAM_NOTDATA_MASK		0x00150001

/*
 * L2-cache definitions for the K/T injector.
 */
#define	KT_L2_16BANK_MASK		0x3c0	/* paddr bits [9:6] */
#define	KT_L2_8BANK_MASK		0x1c0	/* paddr bits [8:6] */
#define	KT_L2_4BANK_MASK		0x0c0	/* paddr bits [7:6] */
#define	KT_L2_BANK_MASK			KT_L2_16BANK_MASK	/* default */
#define	KT_L2_BANK_MASK_SHIFT		6
#define	KT_L2_BANK_OFFSET		0x40
#define	KT_NUM_L2_BANKS			16
#define	KT_NUM_L2_WAYS			24
#define	KT_L2_WAY_SHIFT			18

#define	KT_L2_BANK_CSR_BASE_MSB 	0x800
#define	KT_L2_BANK_CSR_BASE  		(0x800ULL << 32)
#define	KT_L2_BANK_AVAIL		0x1018
#define	KT_L2_BANK_AVAIL_FULL		0x80000001018
#define	KT_L2_BANK_EN			0x1020
#define	KT_L2_BANK_EN_FULL		0x80000001020
#define	KT_L2_BANK_EN_ALLEN		0x0ff

#define	KT_L2_IDX_HASH_EN		0x1030
#define	KT_L2_IDX_HASH_EN_FULL		0x80000001030

#define	KT_L2_IDX_TOPBITS		0x01f0000000	/* ADDR[32:28] */
#define	KT_L2_IDX_BOTBITS		0x00000c0000	/* ADDR[19:18] */

#define	KT_L2_DIAG_DATA_MSB	0x821	/* any of 0x820-0x823 or 0x830-0x833 */
#define	KT_L2_DIAG_DATA		(0x821ULL << 32)
#define	KT_L2_DIAG_TAG_MSB	0x824	/* any of 0x824-0x825 or 0x834-0x835 */
#define	KT_L2_DIAG_TAG		(0x824ULL << 32)
#define	KT_L2_DIAG_VADS_MSB	0x826	/* any of 0x826-0x827 or 0x836-0x837 */
#define	KT_L2_DIAG_VADS		(0x826ULL << 32)
#define	KT_L2_CTL_REG_MSB 	0x829
#define	KT_L2_CTL_REG		(0x829ULL << 32)

#define	KT_L2_ERR_REC_REG_MSB 	0x83d
#define	KT_L2_ERR_REC_REG	(0x83dULL << 32)
#define	KT_L2_ERR_SIG_REG_MSB 	0x82a
#define	KT_L2_ERR_SIG_REG	(0x82aULL << 32)
#define	KT_L2_ERR_STS_REG_MSB 	0x82b
#define	KT_L2_ERR_STS_REG	(0x82bULL << 32)
#define	KT_L2_ERR_STS_REG2_MSB 	0x82f
#define	KT_L2_ERR_STS_REG2	(0x82fULL << 32)
#define	KT_L2_ERR_INJ_REG_MSB 	0x82d	/* directory, WB, and FB errors */
#define	KT_L2_ERR_INJ_REG	(0x82dULL << 32)
#define	KT_L2_PREFETCHICE_MSB 	0x600	/* required opcode for prefectch-ICE */
#define	KT_L2_PREFETCHICE	(0x600ULL << 32)

#define	KT_L2_INJ_ECC_SHIFT	10
#define	KT_L2_INJ_ENABLE	(1 << 9)
#define	KT_L2_INJ_SSHOT		(1 << 8)
#define	KT_L2_INJ_FBUF		(1 << 5)	/* fill buffer */
#define	KT_L2_INJ_WBUF		(1 << 4)	/* write buffer */
#define	KT_L2_INJ_DIR		(1 << 3)	/* directory */
#define	KT_L2_INJ_MBUF		(1 << 2)	/* miss buffer */

#define	KT_COU_CEILING_REG	0x80100000000	/* ceiling_mask in bits[9:0] */
#define	KT_COU_CEILING_MASK	0x3ff
#define	KT_COU_CEILING_SHIFT	34		/* effective on PA[43:34] */

#define	KT_PADDR_INTERLEAVE_MASK	(KT_COU_CEILING_MASK << \
						KT_COU_CEILING_SHIFT)
#define	KT_PADDR_INTERLEAVE_BITS(paddr)	\
			(((paddr) >> KT_COU_CEILING_SHIFT) & \
			KT_COU_CEILING_MASK)

#define	KT_IS_FINE_INTERLEAVE(reg, paddr) \
			((KT_PADDR_INTERLEAVE_BITS(paddr) >= \
			((reg) & KT_COU_CEILING_MASK)) ? 0 : 1)

#define	KT_2NODE_FINE_PADDR_NODEID_MASK	0x400		/* bit[10] */
#define	KT_4NODE_FINE_PADDR_NODEID_MASK	0xc00		/* bits[11:10] */
#define	KT_FINE_PADDR_NODEID_SHIFT	10

#define	KT_2NODE_FINE_PADDR_NODEID(paddr) \
		(((paddr) & KT_2NODE_FINE_PADDR_NODEID_MASK) >> \
		KT_FINE_PADDR_NODEID_SHIFT)

#define	KT_4NODE_FINE_PADDR_NODEID(paddr) \
		(((paddr) & KT_4NODE_FINE_PADDR_NODEID_MASK) >> \
		KT_FINE_PADDR_NODEID_SHIFT)

#define	KT_2NODE_COARSE_PADDR_NODEID_MASK	0x200000000ULL /* bit[33] */
#define	KT_4NODE_COARSE_PADDR_NODEID_MASK	0x600000000ULL /* bits[34:33] */
#define	KT_COARSE_PADDR_NODEID_SHIFT		33

#define	KT_2NODE_COARSE_PADDR_NODEID(paddr) \
		(((paddr) & KT_2NODE_COARSE_PADDR_NODEID_MASK) >> \
		KT_COARSE_PADDR_NODEID_SHIFT)

#define	KT_4NODE_COARSE_PADDR_NODEID(paddr) \
		(((paddr) & KT_4NODE_COARSE_PADDR_NODEID_MASK) >> \
		KT_COARSE_PADDR_NODEID_SHIFT)

/*
 * Common sun4v MMU definitions are used for KT/RF.
 * One difference is the larger physical address space.
 */
#define	KT_TTE4V_PA_MASK	0xfffffffe000	/* PA[43:13] used in KT TTE */

/*
 * Custom register definitions for the K/T injector.
 */
#define	KT_LOCAL_STRAND_MASK	0xff	/* strand bitmask for core regs */

/*
 * Custom error register definitions for the K/T injector.
 */
#define	KT_DRAM_ERR_REC_ERREN	0xbfc0000000000000	/* all errs enabled */
#define	KT_DRAM_ERR_SIG_ERREN	0x1f80000000000000	/* all traps enabled */
#define	KT_L2_ERR_REC_ERREN	0xf560000000000000	/* all errs enabled */
#define	KT_CERER_ERREN		0xec75c0f3f8b7ffc9	/* all errs enabled */
#define	KT_SETER_ERREN		0x7000000000000000	/* all traps enabled */

/* NCU error registers */
#define	KT_NCU_ERR_STS_REG	0x80000003000	/* NESR */
#define	KT_NCU_LOG_ENB_REG	0x80000003008	/* NERER */
#define	KT_NCU_SIG_ENB_REG	0x80000003010	/* NESER */
#define	KT_SOC_ERR_INJ_REG	0x80000003018	/* SOC error injection */
#define	KT_NCU_ERR_STEER_REG	0x80000003048	/* error steering reg */
#define	KT_NCU_SSI_TIMEOUT	0x80000000050	/* PRM correct, 0050 not 3050 */
#define	KT_NCU_LOG_REG_ERREN	0xfffe000000000000	/* all errs enabled */
#define	KT_NCU_TO_MASK		0xfffffc00	/* mask for NCU TO field */

#define	KT_RESET_FAT_ENB_REG	0x80900000820	/* reset fatal error enable */
#define	KT_RESET_FAT_ENB_ERREN	0xfffffff80	/* all errs enabled */

#define	KT_SSI_PA_BASE		0xffff0000000	/* start of SSI address space */
#define	KT_SSI_BOOT_BASE	0x00000800000	/* start of BootROM */
#define	KT_SSI_TO_MASK		0x000ffffffff
#define	KT_SSI_ERR_CFG_ERREN	(7ULL << 50)	/* SSI bits are in NERER */

/* LFU error registers, three LFUs, reg step 2048 */
#define	KT_LFU_REG_BASE		(0x802ULL << 32)
#define	KT_LFU_TRAN_ST_TO_REG	(KT_LFU_REG_BASE + 0x38) /* same as VF */
#define	KT_LFU_CFG_ST_TO_REG	(KT_LFU_REG_BASE + 0x40) /* same as VF */
#define	KT_LFU_SERDES_INVP_REG	(KT_LFU_REG_BASE + 0x60) /* same as VF */
#define	KT_LFU_ERR_STS_REG	(KT_LFU_REG_BASE + 0x80) /* same as VF */
#define	KT_LFU_ERR_INJ1_REG	(KT_LFU_REG_BASE + 0x90) /* egress frame inj */
#define	KT_LFU_ERR_INJ2_REG	(KT_LFU_REG_BASE + 0x98) /* " */
#define	KT_LFU_ERR_INJ3_REG	(KT_LFU_REG_BASE + 0xa0) /* " */
#define	KT_LFU_LOG_ENB_REG	(KT_LFU_REG_BASE + 0x190)
#define	KT_LFU_SIG_ENB_REG	(KT_LFU_REG_BASE + 0x198)

#define	KT_LFU_NUM_UNITS	3	/* VF had 4 */
#define	KT_LFU_NUM_LANES	14	/* 14 lanes same as VF */
#define	KT_LFU_LANE_MASK	0x3fff	/* 14 lanes same as VF */
#define	KT_LFU_REG_STEP		0x800	/* 2048 */

#define	KT_LFU_INJECTION_ENABLE	(1ULL << 41)
#define	KT_LFU_SSHOT_ENABLE	(1ULL << 40)
#define	KT_LFU_PERIOD_MASK	0x7ff
#define	KT_LFU_PERIOD_SHIFT	42
#define	KT_LFU_COUNT_MASK	0x7
#define	KT_LFU_COUNT_SHIFT	53
#define	KT_LFU_TO_MAX		0xaf

/*
 * Definitions for the KT/RF SOC error injection register.
 */
#define	KT_SOC_REG_INJ_ENABLE	(1ULL << 33)
#define	KT_SOC_REG_SSHOT	(1ULL << 32)


#ifdef	__cplusplus
}
#endif

#endif	/* _MEMTEST_ASM_KT_H */
