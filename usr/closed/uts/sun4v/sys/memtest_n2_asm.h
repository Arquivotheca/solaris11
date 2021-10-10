/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _MEMTEST_ASM_N2_H
#define	_MEMTEST_ASM_N2_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Header file for Niagara-II (UltraSPARC-T2) assembly routines.
 */

/*
 * The following definitions were not in any kernel header files.
 *
 * NOTE: this header and memtest_ni_asm.h are mutually exclusive because
 *	 many of the definitions are similar in both files.
 */

/*
 * ASI definitions for the Niagara-II injector.
 *
 * NOTE: since most of the following definitions are required for Niagara-I
 *	 and here perhaps they can be moved to a common header file.
 */
#define	ASI_MEM		0x14	/* Physical address, non-L1$-allocating */
#define	ASI_IO		0x15	/* Physical address, non-$able w/ side-effect */
#define	ASI_MEM_LE	0x1c	/* ASI_MEM, little endian */
#define	ASI_IO_LE	0x1d	/* ASI_IO, little endian */
#define	ASI_BLK_AIUP_LE	0x1e	/* ASI_BLK_AIUP, little endian */
#define	ASI_BLK_AIUS_LE	0x1f	/* ASI_BLK_AIUS, little endian */

#define	ASI_SCRATCHPAD		0x20	/* priv scratchpad registers */
#define	ASI_MMU			0x21
#define	ASI_BLKINIT_AIUP	0x22
#define	ASI_BLKINIT_AIUS	0x23
#define	ASI_BLKINIT_AIUP_LE	0x2a
#define	ASI_BLKINIT_AIUS_LE	0x2b

#define	ASI_QUAD_LDD		0x24	/* 128-bit atomic ldda/stda */
#define	ASI_QUAD_LDD_REAL	0x26	/* 128-bit atomic ldda/stda real */
#define	ASI_QUAD_LDD_LE		0x2c	/* 128-bit atomic ldda/stda, le */

#define	ASI_STREAM_MA		0x40	/* Niagara-I streaming extensions */
#define	ASI_STREAM		0x40	/* Niagara-II streaming extensions */

#define	ASI_DC_DATA		0x46	/* D$ data array diag access */
#define	ASI_DC_TAG		0x47	/* D$ tag array diag access */

#define	ASI_ERROR_EN		0x4b	/* Error enable */
#define	ASI_ERROR_STATUS 	0x4c	/* Error status */
#define	ASI_ERROR_ADDR		0x4d	/* Error address */

#define	ASI_HSCRATCHPAD		0x4f	/* Hypervisor scratchpad registers */

#define	ASI_IMMU		0x50	/* IMMU registers */
#define	ASI_IMMU_TSB_PS0	0x51	/* IMMU TSB PS0 */
#define	ASI_IMMU_TSB_PS1	0x52	/* IMMU TSB PS1 */
#define	ASI_ITLB_DATA_IN	0x54	/* IMMU data in register */

#define	ASI_MMU_TSB_CFG		0x54
#define	ASI_MMU_TSB_PTR		0x54
#define	ASI_PEND_TBLWLK		0x54	/* N2 - pending tablewalk reg */


#define	ASI_ITLB_DATA_ACC	0x55	/* IMMU data access register */
#define	ASI_ITLB_TAG		0x56	/* IMMU tag read register */
#define	ASI_IMMU_DEMAP		0x57	/* IMMU tlb demap */

#define	ASI_DMMU		0x58	/* DMMU registers */

#define	IDMMU_PARTITION_ID	0x80	/* Partition ID register */

#define	ASI_DMMU_TSB_PS0	0x59	/* DMMU TSB PS0 */
#define	ASI_DMMU_TSB_PS1	0x5a	/* DMMU TSB PS1 */
#define	ASI_DTLB_DIRECTPTR	0x5b	/* DMMU direct pointer */
#define	ASI_DTLB_DATA_IN	0x5c	/* DMMU data in register */
#define	ASI_DTLB_DATA_ACC	0x5d	/* DMMU data access register */
#define	ASI_DTLB_TAG		0x5e	/* DMMU tag read register */
#define	ASI_DMMU_DEMAP		0x5f	/* DMMU tlb demap */

#define	ASI_TLB_INVALIDATE	0x60 /* TLB invalidate registers */

#define	ASI_ICACHE_INSTR	0x66
#define	ASI_ICACHE_TAG		0x67

#define	ASI_INTR_RCV	0x72	/* Interrupt receive register */
#define	ASI_INTR_UDB_W	0x73	/* Interrupt vector dispatch register */
#define	ASI_INTR_UDB_R	0x74	/* Incoming interrupt vector register */

#define	ASI_BLK_INIT_P	0xe2	/* Block initializing store, primary ctx */
#define	ASI_BLK_INIT_S	0xe3	/* Block initializing store, secondary ctx */
#define	ASI_BLK_INIT_P_LE 0xea	/* Block initializing store, primary ctx, le */

/*
 * Custom hyperpriv register definitions for the Niagara-II injector.
 */
#define	ASI_LSUCR		0x45
#define	LSUCR_IC		0x01		/* I$ enable */
#define	LSUCR_DC		0x02		/* D$ enable */
#define	LSUCR_IM		0x04		/* IMMU enable */
#define	LSUCR_DM		0x08		/* DMMU enable */

#define	N2_L2CR_DIS		0x01		/* L2$ Disable */
#define	N2_L2CR_DMMODE		0x02		/* L2$ Direct-mapped mode */
#define	N2_L2CR_SCRUBEN		0x04		/* L2$ scrubber enable */

#define	N2_SSI_LOG		0xff00000018	/* same as N1 */
#define	N2_SSI_TIMEOUT		0xff00010088	/* same as N1 */

/*
 * Memory (DRAM) definitions for the Niagara-II injector.
 */
#define	N2_DRAM_CSR_BASE_MSB 		0x84
#define	N2_DRAM_CSR_BASE  		(0x84ULL << 32)

#define	N2_DRAM_SCRUB_FREQ_REG		0x18
#define	N2_DRAM_REFRESH_FREQ_REG	0x20
#define	N2_DRAM_SCRUB_ENABLE_REG	0x40

#define	N2_DRAM_BRANCH_DISABLED_REG	0x138

#define	N2_DRAM_ERROR_STATUS_REG	0x280
#define	N2_DRAM_ERROR_ADDR_REG		0x288
#define	N2_DRAM_ERROR_INJ_REG		0x290
#define	N2_DRAM_ERROR_COUNTER_REG	0x298
#define	N2_DRAM_ERROR_LOC_REG		0x2a0
#define	N2_DRAM_ERROR_RETRY_REG		0x2a8

#define	N2_DRAM_TS3_FAILOVER_CONFIG_REG	0x828

#define	N2_DRAM_FBD_ERROR_SYND_REG	0xc00
#define	N2_DRAM_FBD_INJ_ERROR_SRC_REG	0xc08
#define	N2_DRAM_FBR_COUNT_REG		0xc10
#define	N2_DRAM_FBR_COUNT_DISABLE	0x10000

#define	N2_DRAM_BRANCH_MASK		0x180	/* paddr bits [8:7] */
#define	N2_DRAM_BRANCH_PA_SHIFT		0x5	/* reg stride = 4096 */
#define	N2_DRAM_BRANCH_OFFSET		0x1000
#define	N2_NUM_DRAM_BRANCHES		4
#define	N2_DRAM_PADDR_OFFSET		0x80	/* paddr bit [7] */
#define	N2_DRAM_SSHOT_ENABLE		(1ULL << 30)
#define	N2_DRAM_INJECTION_ENABLE	(1ULL << 31)

/*
 * L2-cache definitions for the Niagara-II injector.
 */
#define	N2_L2_8BANK_MASK		0x1c0	/* paddr bits [8:6] */
#define	N2_L2_4BANK_MASK		0xc0	/* paddr bits [7:6] */
#define	N2_L2_2BANK_MASK		0x40	/* paddr bit [6] */
#define	N2_L2_BANK_MASK			N2_L2_8BANK_MASK	/* default */
#define	N2_L2_BANK_MASK_SHIFT		6
#define	N2_L2_BANK_OFFSET		0x40
#define	N2_NUM_L2_BANKS			8
#define	N2_NUM_L2_WAYS			16
#define	N2_L2_WAY_SHIFT			18

#define	N2_L2_BANK_CSR_BASE_MSB 	0x80
#define	N2_L2_BANK_CSR_BASE  		(0x80ULL << 32)
#define	N2_L2_BANK_AVAIL		0x1018
#define	N2_L2_BANK_AVAIL_FULL		0x8000001018
#define	N2_L2_BANK_EN			0x1020
#define	N2_L2_BANK_EN_FULL		0x8000001020
#define	N2_L2_BANK_EN_STATUS		0x1028
#define	N2_L2_BANK_EN_STATUS_FULL	0x8000001028

#define	N2_L2_BANK_EN_STATUS_ALLEN	0xf
#define	N2_L2_BANK_EN_STATUS_PM		0x10

#define	N2_L2_IDX_HASH_EN		0x1030
#define	N2_L2_IDX_HASH_EN_FULL		0x8000001030
#define	N2_L2_IDX_HASH_EN_STATUS	0x1038
#define	N2_L2_IDX_HASH_EN_STATUS_FULL	0x8000001038

#define	N2_L2_IDX_TOPBITS		0x01f0000000	/* ADDR[32:28] */
#define	N2_L2_IDX_BOTBITS		0x00000c0000	/* ADDR[19:18] */

#define	N2_L2_DIAG_DATA_MSB	0xa1	/* any of 0xa0-0xa3 or 0xb0-0xb3 */
#define	N2_L2_DIAG_DATA		(0xa1ULL << 32)
#define	N2_L2_DIAG_TAG_MSB	0xa4	/* any of 0xa4-0xa5 or 0xb4-0xb5 */
#define	N2_L2_DIAG_TAG		(0xa4ULL << 32)
#define	N2_L2_DIAG_VUAD_MSB	0xa6	/* any of 0xa6-0xa7 or 0xb6-0xb7 */
#define	N2_L2_DIAG_VUAD		(0xa6ULL << 32)
#define	N2_L2_CTL_REG_MSB 	0xa9
#define	N2_L2_CTL_REG		(0xa9ULL << 32)
#define	N2_L2_ERR_ENB_REG_MSB 	0xaa	/* or 0xba */
#define	N2_L2_ERR_ENB_REG	(0xaaULL << 32)
#define	N2_L2_ERR_STS_REG_MSB 	0xab	/* or 0xbb */
#define	N2_L2_ERR_STS_REG	(0xabULL << 32)
#define	N2_L2_ERR_INJ_REG_MSB 	0xad	/* directory errors only */
#define	N2_L2_ERR_INJ_REG	(0xadULL << 32)
#define	N2_L2_PREFETCHICE_MSB 	0x60	/* required opcode for prefectch-ICE */
#define	N2_L2_PREFETCHICE	(0x60ULL << 32)

/*
 * Custom MMU definitions for the Niagara-II injector.
 */
#define	N2_MMU_DEMAP_PAGE	0x0
#define	N2_MMU_DEMAP_CONTEXT	0x1
#define	N2_MMU_DEMAP_ALL	0x2
#define	N2_MMU_DEMAP_ALL_PAGES	0x3

#define	N2_TSB_BASE_ADDR_MASK	0xffffffe000	/* are PA[39:13] */
#define	N2_TSB_SIZE_MASK	0x7		/* actual size (512 * 2^val) */
#define	N2_TSB_PAGESIZE_MASK	0xf8		/* page size */
#define	N2_TSB_RA_NOT_PA	0x100		/* TSB contains raddrs */

#define	N2_TTE4V_PA_MASK	0xffffffe000	/* PA[39:13] used in N2 TTE */
#define	NI_TTE4V_L_SHIFT	61	/* XXX sw lock bit, works for N2? 57? */

/*
 * Custom register definitions for the Niagara-II injector.
 */
#define	N2_ASI_LSU_DIAG_REG		0x42
#define	N2_ASI_LSU_CTL_REG		0x45	/* or use above ASI_LSUCR */

#define	N2_LOCAL_CORE_MASK		0xff	/* bitmask for core regs */

#define	ASI_CMT_REG			0x41	/* cmt registers */
#define	ASI_CORE_RUNNING_STS		0x58	/* core running status offset */
#define	ASI_CORE_RUNNING_W1S		0x60	/* core running set offset */
#define	ASI_CORE_RUNNING_W1C		0x68	/* core running clear offset */

#define	ASI_CMT_CORE_REG		0x63	/* core registers */
#define	ASI_CMT_CORE_INTR_ID		0x00	/* core interrupt reg offset */
#define	ASI_CMT_STRAND_ID		0x10	/* core strand id reg offset */

/*
 * Custom error register definitions for the Niagara-II injector.
 */
#define	N2_L2_FTL_RST_REG	0x8900000820
#define	N2_L2_FTL_RST_ERREN	0xff00		/* all banks enabled */

#define	N2_CERER_REG		0x10		/* VA for ASI_ERROR_STATUS */
#define	N2_SETER_REG		0x18		/* VA for ASI_ERROR_STATUS */
#define	N2_CERER_ERREN		0xecf5c1f3f8bfffff	/* all errs enabled */
#define	N2_SETER_ERREN		0x7000000000000000	/* all traps enabled */

#define	N2_SSI_ERR_CFG_ERREN	(1ULL << 24)

#define	N2_SOC_ERR_STS_REG	0x8000003000
#define	N2_SOC_LOG_ENB_REG	0x8000003008
#define	N2_SOC_INT_ENB_REG	0x8000003010
#define	N2_SOC_ERR_INJ_REG	0x8000003018
#define	N2_SOC_FTL_ENB_REG	0x8000003020
#define	N2_SOC_ERR_PND_REG	0x8000003028
#define	N2_SOC_ERR_STEER_REG	0x9001041000
#define	N2_SOC_ERREN		0x76dbeffffff	/* all errs enabled */
#define	N2_SOC_FAT_ERREN	0x400327fdbf3	/* all UE errs except dmudpar */

#define	N2_IRF_STRIDE_SIZE	32
#define	N2_HPRIV_IRF_GLOBAL	0x1
#define	N2_HPRIV_IRF_IN		0x2
#define	N2_HPRIV_IRF_LOCAL	0x3
#define	N2_HPRIV_IRF_OUT	0x4

/*
 * PIU Unit offsets for CSR registers.
 */
#define	PIU_CSR_BASE		(0x88ULL << 32)
#define	MMU_CTL_AND_STATUS_REG	(0x640000 + PIU_CSR_BASE)
#define	N2_MMU_CTL_REG		0x8800640000

/*
 * Modular Arithmetic Unit of SPU offset defines used with ASI_STREAM.
 *
 * The offsets in MA_OP_ADDR_OFFSETS are set to be four words apart.
 * This has the effect of giving loads/stores a default offset of zero.
 * The value of the exponent (exp - 1) is set to 1.
 */
#define	ASI_MA_CONTROL_REG	0x80
#define	ASI_MA_MPA_REG		0x88
#define	ASI_MA_ADDR_REG		0x90
#define	ASI_MA_NP_REG		0x98
#define	ASI_MA_SYNC_REG		0xa0
#define	MA_OP_ADDR_OFFSETS	0x000001100c080400

/*
 * Control Word Queue of SPU offset defines used with ASI_STREAM.
 */
#define	ASI_CWQ_HEAD_REG	0x00
#define	ASI_CWQ_TAIL_REG	0x08
#define	ASI_CWQ_FIRST_REG	0x10
#define	ASI_CWQ_LAST_REG	0x18
#define	ASI_CWQ_CSR_REG		0x20
#define	ASI_CWQ_CSR_ENABLE_REG	0x28
#define	ASI_CWQ_SYNC_REG	0x30

/*
 * Control Word Queue defines for operations and fields.
 *
 * The below offsets are for the first 64-bit word of the control word,
 * the Initial and Complete control words have the eight 64-bit fields
 * laid out as follows:
 *	0) control
 *	1) source addr
 *	2) auth key addr
 *	3) auth IV addr
 *	4) final auth state addr
 *	5) encryption key addr
 *	6) encryption initialization vector addr
 *	7) destination addr
 */
#define	N2_CWQ_OP_SHIFT		56
#define	N2_CWQ_SOB_SHIFT	54
#define	N2_CWQ_EOB_SHIFT	53
#define	N2_CWQ_INT_SHIFT	48
#define	N2_CWQ_STRAND_SHIFT	37

#define	N2_CWQ_CW_SIZE		64
#define	N2_CWQ_SRC_ADDR_OFFSET	8		/* byte offset for CW */
#define	N2_CWQ_DST_ADDR_OFFSET	56		/* byte offset for CW = 7*8 */

/*
 * System on Chip bit defines for the SOC error registers.
 * Note that the SHIFT definitions for these registers are
 * defined in the file memtestio_n2.h so that they can be
 * used in the command definitions in the user file mtst_n2.c
 * as the xorpat values.
 */
#define	N2_SOC_NCUDMUCREDIT		0x040000000000
#define	N2_SOC_MCU3ECC			0x020000000000
#define	N2_SOC_MCU3FBR			0x010000000000
#define	N2_SOC_MCU3FBU			0x008000000000

#define	N2_SOC_MCU2ECC			0x004000000000
#define	N2_SOC_MCU2FBR			0x002000000000
#define	N2_SOC_MCU2FBU			0x001000000000

#define	N2_SOC_MCU1ECC			0x000800000000
#define	N2_SOC_MCU1FBR			0x000400000000
#define	N2_SOC_MCU1FBU			0x000200000000

#define	N2_SOC_MCU0ECC			0x000100000000
#define	N2_SOC_MCU0FBR			0x000080000000
#define	N2_SOC_MCU0FBU			0x000040000000

#define	N2_SOC_NIUDATAPARITY		0x000020000000
#define	N2_SOC_NIUCTAGUE		0x000010000000
#define	N2_SOC_NIUCTAGCE		0x000008000000
#define	N2_SOC_SIOCTAGCE		0x000004000000
#define	N2_SOC_SIOCTAGUE		0x000002000000

#define	N2_SOC_NCUCTAGCE		0x000000800000
#define	N2_SOC_NCUCTAGUE		0x000000400000
#define	N2_SOC_NCUDMUUE			0x000000200000
#define	N2_SOC_NCUCPXUE			0x000000100000
#define	N2_SOC_NCUPCXUE			0x000000080000
#define	N2_SOC_NCUPCXDATA		0x000000040000
#define	N2_SOC_NCUINTTABLE		0x000000020000
#define	N2_SOC_NCUMONDOFIFO		0x000000010000
#define	N2_SOC_NCUMONDOTABLE		0x000000008000
#define	N2_SOC_NCUDATAPARITY		0x000000004000

#define	N2_SOC_DMUDATAPARITY		0x000000002000
#define	N2_SOC_DMUSIICREDIT		0x000000001000
#define	N2_SOC_DMUCTAGUE		0x000000000800
#define	N2_SOC_DMUCTAGCE		0x000000000400
#define	N2_SOC_DMUNCUCREDIT		0x000000000200
#define	N2_SOC_DMUINTERNAL		0x000000000100

#define	N2_SOC_SIIDMUAPARITY		0x000000000080
#define	N2_SOC_SIINIUDPARITY		0x000000000040
#define	N2_SOC_SIIDMUDPARITY		0x000000000020
#define	N2_SOC_SIINIUAPARITY		0x000000000010
#define	N2_SOC_SIIDMUCTAGCE		0x000000000008
#define	N2_SOC_SIINIUCTAGCE		0x000000000004
#define	N2_SOC_SIIDMUCTAGUE		0x000000000002
#define	N2_SOC_SIINIUCTAGUE		0x000000000001

#ifdef	__cplusplus
}
#endif

#endif	/* _MEMTEST_ASM_N2_H */
