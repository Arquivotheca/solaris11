/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _MEMTEST_ASM_NI_H
#define	_MEMTEST_ASM_NI_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Header file for Niagara (UltraSPARC-T1) assembly routines.
 */

/*
 * The following definitions were not in any kernel header files.
 * They come from specific Niagara hypervisor header files and are required
 * by the injector routines that run in hyperprivileged mode.
 *
 * NOTE: that not all the definitions below are required for the compile but
 *	 they may be required during runtime or the driver will not attach.
 *
 * For clarity the source header file is listed above each section.
 */

/*
 * Definitions from include/devices/pc16550.h.
 */
#define	RBR_ADDR	0x0
#define	THR_ADDR	0x0
#define	IER_ADDR	0x1
#define	IIR_ADDR	0x2
#define	FCR_ADDR	0x2
#define	LCR_ADDR	0x3
#define	MCR_ADDR	0x4
#define	LSR_ADDR	0x5
#define	MSR_ADDR	0x6
#define	SCR_ADDR	0x7
#define	DLL_ADDR	0x0
#define	DLM_ADDR	0x1
#define	LSR_DRDY	0x1
#define	LSR_BINT	0x10
#define	LSR_THRE	0x20
#define	LSR_TEMT	0x40

/*
 * Definitions from include/niagara/asi.h.
 */
#define	ASI_MEM		0x14	/* Physical address, non-L1$-allocating */
#define	ASI_IO		0x15	/* Physical address, non-$able w/ side-effect */
#define	ASI_MEM_LE	0x1c	/* ASI_MEM, little endian */
#define	ASI_IO_LE	0x1d	/* ASI_IO, little endian */
#define	ASI_BLK_AIUP_LE	0x1e	/* ASI_BLK_AIUP, little endian */
#define	ASI_BLK_AIUS_LE	0x1f	/* ASI_BLK_AIUS, little endian */

#define	ASI_ERROR_EN	0x4b	/* Error enable */
#define	ASI_ERROR_STATUS 0x4c	/* Error status */
#define	ASI_ERROR_ADDR	0x4d	/* Error address */

#define	ASI_DTSBBASE_CTX0_PS0	0x31
#define	ASI_DTSBBASE_CTX0_PS1	0x32
#define	ASI_DTSB_CONFIG_CTX0	0x33
#define	ASI_ITSBBASE_CTX0_PS0	0x35
#define	ASI_ITSBBASE_CTX0_PS1	0x36
#define	ASI_ITSB_CONFIG_CTX0	0x37
#define	ASI_DTSBBASE_CTXN_PS0	0x39
#define	ASI_DTSBBASE_CTXN_PS1	0x3a
#define	ASI_DTSB_CONFIG_CTXN	0x3b
#define	ASI_ITSBBASE_CTXN_PS0	0x3d
#define	ASI_ITSBBASE_CTXN_PS1	0x3e
#define	ASI_ITSB_CONFIG_CTXN	0x3f

#define	ASI_IMMU	0x50	/* IMMU registers */
#define	ASI_IMMU_TSB_PS0 0x51	/* IMMU TSB PS0 */
#define	ASI_IMMU_TSB_PS1 0x52	/* IMMU TSB PS1 */
#define	ASI_ITLB_DATA_IN 0x54	/* IMMU data in register */
#define	ASI_ITLB_DATA_ACC 0x55	/* IMMU data access register */
#define	ASI_ITLB_TAG	0x56	/* IMMU tag read register */
#define	ASI_IMMU_DEMAP	0x57	/* IMMU tlb demap */

#define	ASI_DMMU	0x58	/* DMMU registers */

#define	IDMMU_PARTITION_ID	0x80 /* Partition ID register */

#define	ASI_DMMU_TSB_PS0 0x59	/* DMMU TSB PS0 */
#define	ASI_DMMU_TSB_PS1 0x5a	/* DMMU TSB PS1 */
#define	ASI_DTLB_DIRECTPTR 0x5b	/* DMMU direct pointer */
#define	ASI_DTLB_DATA_IN 0x5c	/* DMMU data in register */
#define	ASI_DTLB_DATA_ACC 0x5d	/* DMMU data access register */
#define	ASI_DTLB_TAG	0x5e	/* DMMU tag read register */
#define	ASI_DMMU_DEMAP	0x5f	/* DMMU tlb demap */

#define	ASI_TLB_INVALIDATE 0x60 /* TLB invalidate registers */

/*
 * Definitions from include/niagara/config.h.
 */
#define	HV_UART		0xfff0c2c000

/*
 * Definitions from include/niagara/dram.h.
 */
#define	DRAM_CSR_BASE  			(0x97LL << 32)
#define	DRAM_PORT_SHIFT  		12
#define	DRAM_MAX_PORT  			3
#define	DRAM_SCRUB_FREQ_REG		0x18
#define	DRAM_REFRESH_FREQ_REG		0x20
#define	DRAM_SCRUB_ENABLE_REG		0x40

#define	DRAM_CHANNEL_DISABLED_REG	0x138

#define	DRAM_ERROR_STATUS_REG		0x280
#define	DRAM_ERROR_ADDR_REG		0x288
#define	DRAM_ERROR_INJ_REG		0x290
#define	DRAM_ERROR_COUNTER_REG		0x298
#define	DRAM_ERROR_LOC_REG		0x2a0

/*
 * Definitions from include/niagara/errs.h.
 */
#define	L2_CSR_BASE		0xA000000000
#define	L2_CTL_REG		(L2_CSR_BASE + 0x900000000)
#define	L2_DIS_SHIFT		(0)
#define	L2_DIS			(1 << L2_DIS_SHIFT)
#define	L2_DMMODE_SHIFT		(1)
#define	L2_DMMODE		(1 << L2_DMMODE_SHIFT)
#define	L2_SCRUBENABLE_SHIFT	2
#define	L2_SCRUBENABLE		(1 << L2_SCRUBENABLE_SHIFT)
#define	L2_SCRUBINTERVAL_SHIFT	3
#define	L2_SCRUBINTERVAL_MASK	(0xFFFULL << L2_SCRUBENABLE_SHIFT)
#define	L2_ERRORSTEER_SHIFT	15
#define	L2_ERRORSTEER_MASK	(0x1FULL << L2_ERRORSTEER_MASK)
#define	L2_DBGEN_SHIFT		20
#define	L2_DBGEN		(1 << L2_DBGEN_SHIFT)

#define	L2_EEN_BASE		0xAA00000000
#define	L2_EEN_STEP		0x40
#define	L2_ESR_BASE		0xAB00000000
#define	L2_ESR_STEP		0x40
#define	L2_BANK_STEP		0x40
#define	L2_BANK_SHIFT		6
#define	L2_BANK_MASK		(0x3)
#define	L2_SET_SHIFT		8
#define	L2_SET_MASK		(0x3FF)
#define	L2_WAY_SHIFT		18
#define	L2_WAY_MASK		(0xF)
#define	NO_L2_BANKS		4

#define	L2_EAR_BASE		0xac00000000
#define	L2_EAR_STEP		0x40

#define	NO_DRAM_BANKS		4
#define	DRAM_BANK_STEP		0x1000
#define	DRAM_ESR_BASE		0x9700000280
#define	DRAM_ESR_STEP		0x1000
#define	DRAM_BANK_SHIFT		12
#define	DRAM_EAR_BASE		0x9700000288
#define	DRAM_EAR_STEP		0x1000
#define	DRAM_EIR_BASE		0x9700000290
#define	DRAM_ECR_BASE		0x9700000298
#define	DRAM_ECR_STEP		0x1000
#define	DRAM_ECR_ENB		(1 << 17)
#define	DRAM_ECR_VALID		(1 << 16)
#define	DRAM_ECR_COUNT_MASK	(0xFFFF)
#define	DRAM_ECR_COUNT_SHIFT	0

/*
 * Definitions from include/niagara/jbi_regs.h.
 */
#define	JBI_BASE		0x8000000000
#define	JBI_CONFIG1		JBI_BASE
#define	JBI_CONFIG2		(JBI_BASE + 0x00008)
#define	JBI_DEBUG		(JBI_BASE + 0x04000)
#define	JBI_DEBUG_ARB		(JBI_BASE + 0x04100)
#define	JBI_ERR_INJECT		(JBI_BASE + 0x04800)
#define	JBI_ERR_CONFIG		(JBI_BASE + 0x10000)
#define	JBI_ERR_LOG		(JBI_BASE + 0x10020)
#define	JBI_ERR_OVF		(JBI_BASE + 0x10028)
#define	JBI_LOG_ENB		(JBI_BASE + 0x10030)
#define	JBI_SIG_ENB		(JBI_BASE + 0x10038)
#define	JBI_LOG_ADDR		(JBI_BASE + 0x10040)
#define	JBI_LOG_DATA0		(JBI_BASE + 0x10050)
#define	JBI_LOG_DATA1		(JBI_BASE + 0x10058)
#define	JBI_LOG_CTRL		(JBI_BASE + 0x10048)
#define	JBI_LOG_PAR		(JBI_BASE + 0x10060)
#define	JBI_LOG_NACK		(JBI_BASE + 0x10070)
#define	JBI_LOG_ARB		(JBI_BASE + 0x10078)
#define	JBI_L2_TIMEOUT		(JBI_BASE + 0x10080)
#define	JBI_ARB_TIMEOUT		(JBI_BASE + 0x10088)
#define	JBI_TRANS_TIMEOUT	(JBI_BASE + 0x10090)
#define	JBI_INTR_TIMEOUT	(JBI_BASE + 0x10098)
#define	JBI_MEMSIZE		(JBI_BASE + 0x100a0)
#define	JBI_PERF_CTL		(JBI_BASE + 0x20000)
#define	JBI_PERF_COUNT		(JBI_BASE + 0x20008)

/*
 * Definitions from include/niagara/hprivregs.h.
 */
#define	ASI_LSUCR	0x45
#define	L2CR_DIS	0x00000001	/* L2$ Disable */
#define	L2CR_DMMODE	0x00000002	/* L2$ Direct-mapped mode */

#define	SSI_LOG		0xff00000018
#define	SSI_TIMEOUT	0xff00010088

/*
 * Definitions from include/niagara/mmu.h.
 */
#define	DEMAP_ALL		0x2
#define	TAGTRG_VA_LSHIFT	22
#define	ASI_TSB_CONFIG_PS1_SHIFT 8

#define	NI_TTE4V_L_SHIFT	61
#define	NI_TLB_IN_4V_FORMAT	(1 << 10)

#define	TTE4U_V		0x8000000000000000
#define	TTE4U_SZL	0x6000000000000000
#define	TTE4U_NFO	0x1000000000000000
#define	TTE4U_IE	0x0800000000000000
#define	TTE4U_SZH	0x0001000000000000
#define	TTE4U_DIAG	0x0000ff0000000000
#define	TTE4U_PA_SHIFT	13
#define	TTE4U_L		0x0000000000000040
#define	TTE4U_CP	0x0000000000000020
#define	TTE4U_CV	0x0000000000000010
#define	TTE4U_E		0x0000000000000008
#define	TTE4U_P		0x0000000000000004
#define	TTE4U_W		0x0000000000000002

/*
 * Definitions from niagara/hypervisor/offsets.h.
 *
 * XXX	NOTE defining these here is inherently dangerous since the offsets.h
 *	file is generated during the build of the hypervisor.  This means
 *	that any code relying on these offsets can break if the hypervisor
 *	on the system being tested is significantly different than the
 *	hypervisor vintage from which these defs were taken.
 *
 *	I don't see an easy way around this since there is no hypervisor
 *	code in the ON gate.  This is slightly less gross than just dropping
 *	an offsets.h file into the memtest dir since req'd defs are known.
 */
#define	CPU_GUEST 0x0
#define	GUEST_PARTID 0x0
#define	GUEST_MEM_OFFSET 0x18

#define	ERPT_EHDL 0x40
#define	ERPT_STICK 0x48
#define	ERPT_CPUVER 0x50
#define	ERPT_SPARC_AFSR 0x58
#define	ERPT_SPARC_AFAR 0x60
#define	ERPT_L2_AFSR 0x68
#define	ERPT_L2_AFSR_INCR 0x8
#define	ERPT_L2_AFAR 0x88
#define	ERPT_L2_AFAR_INCR 0x8
#define	ERPT_DRAM_AFSR 0xa8
#define	ERPT_DRAM_AFSR_INCR 0x8
#define	ERPT_DRAM_AFAR 0xc8
#define	ERPT_DRAM_AFAR_INCR 0x8
#define	ERPT_DRAM_LOC 0xe8
#define	ERPT_DRAM_LOC_INCR 0x8
#define	ERPT_DRAM_CNTR 0x108
#define	ERPT_DRAM_CNTR_INCR 0x8
#define	ERPT_TSTATE 0x128
#define	ERPT_HTSTATE 0x130
#define	ERPT_TPC 0x138
#define	ERPT_CPUID 0x140
#define	ERPT_TT 0x142
#define	ERPT_TL 0x144

#define	CPU_CE_RPT 0x3a0
#define	CPU_UE_RPT 0xce8

/*
 * Custom definitions for the Niagara injector.
 */
#define	ASI_REAL_MEM		ASI_MEM	/* always requires RA->PA TLB entry */
#define	ASI_REAL_IO		ASI_IO	/* always requires RA->PA TLB entry */
#define	ASI_LSU_DIAG_REG	0x42
#define	ASI_INJECT_ERROR_REG	0x43
#define	ASI_BLK_P		0xF0
#define	ASI_BLK_S		0xF1

#define	L2FLUSH_BASEADDR	0x0	/* rsvd address used for flush caches */

/*
 * Modular Arithmetic Unit test defines used with ASI_STREAM_MA.
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

#ifdef	__cplusplus
}
#endif

#endif	/* _MEMTEST_ASM_NI_H */
