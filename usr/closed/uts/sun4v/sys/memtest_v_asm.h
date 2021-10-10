/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _MEMTEST_V_ASM_H
#define	_MEMTEST_V_ASM_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Sun4v memtest header file for assembly routines.
 */

#include <sys/async.h>
#include <sys/error.h>
#include <sys/memtest_asm.h>
#include <sys/hypervisor_api.h>
#include <sys/machasi.h>
#include <sys/machintreg.h>

/*
 * The following definitions were not in any kernel header files.
 */
#define	ASI_REAL_MEM	ASI_MEM	/* always requires RA->PA TLB entry */
#define	ASI_REAL_IO	ASI_IO	/* always requires RA->PA TLB entry */
#define	ASI_BLK_P	0xF0
#define	ASI_BLK_S	0xF1

#define	INVALIDATE_CACHE_LINE	0x18	/* opcode for prefetch-ICE instr */

#define	FPRS_FEF	0x4	/* enable fp */

#define	CEEN		0x1
#define	NCEEN		0x2

/* Hypervisor API (trap) definitions */
#define	DIAG_RA2PA	0x200
#define	DIAG_HEXEC	0x201

/* Common sun4v ASI definitions */
#define	ASI_MEM		0x14	/* was phys address, non-L1$-allocating */
#define	ASI_IO		0x15	/* was phys address, non-$able w/ side-effect */
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

#define	ASI_CMT_REG		0x41	/* CMT registers */
#define	ASI_CORE_RUNNING_STS	0x58	/* core running status offset */
#define	ASI_CORE_RUNNING_W1S	0x60	/* core running set offset */
#define	ASI_CORE_RUNNING_W1C	0x68	/* core running clear offset */

#define	ASI_LSU_DIAG_REG	0x42
#define	ASI_INJECT_ERROR_REG	0x43

#define	ASI_LSU_CTL_REG		0x45
#define	LSUCR_IC		0x01	/* I$ enable */
#define	LSUCR_DC		0x02	/* D$ enable */
#define	LSUCR_IM		0x04	/* IMMU enable */
#define	LSUCR_DM		0x08	/* DMMU enable */

#define	ASI_CMT_CORE_REG	0x63	/* core registers */
#define	ASI_CMT_CORE_INTR_ID	0x00	/* core interrupt reg offset */
#define	ASI_CMT_STRAND_ID	0x10	/* core strand id reg offset */

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
#define	ASI_PEND_TBLWLK		0x54	/* N2 onward - pending tablewalk reg */

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

#define	ASI_ICACHE_INSTR	0x66	/* I$ instr array diag access */
#define	ASI_ICACHE_TAG		0x67	/* I$ tag array diag access */

#define	ASI_INTR_RCV	0x72	/* Interrupt receive register */
#define	ASI_INTR_UDB_W	0x73	/* Interrupt vector dispatch register */
#define	ASI_INTR_UDB_R	0x74	/* Incoming interrupt vector register */

#define	ASI_BLK_INIT_P	0xe2	/* Block initializing store, primary ctx */
#define	ASI_BLK_INIT_S	0xe3	/* Block initializing store, secondary ctx */
#define	ASI_BLK_INIT_P_LE 0xea	/* Block initializing store, primary ctx, le */

/*
 * VA values for the ASI_MMU_* (0x54) registers.
 */
#define	MMU_ZERO_CTX_TSB_CFG_0	0x10
#define	MMU_ZERO_CTX_TSB_CFG_1	0x18
#define	MMU_ZERO_CTX_TSB_CFG_2	0x20
#define	MMU_ZERO_CTX_TSB_CFG_3	0x28
#define	MMU_NZ_CTX_TSB_CFG_0	0x30
#define	MMU_NZ_CTX_TSB_CFG_1	0x38
#define	MMU_NZ_CTX_TSB_CFG_2	0x40
#define	MMU_NZ_CTX_TSB_CFG_3	0x48
#define	MMU_ITSB_PTR_0		0x50
#define	MMU_ITSB_PTR_1		0x58
#define	MMU_ITSB_PTR_2		0x60
#define	MMU_ITSB_PTR_3		0x68
#define	MMU_DTSB_PTR_0		0x70
#define	MMU_DTSB_PTR_1		0x78
#define	MMU_DTSB_PTR_2		0x80
#define	MMU_DTSB_PTR_3		0x88
#define	MMU_TBLWLK_CTL		0x90
#define	MMU_TBLWLK_STATUS	0x98

/*
 * VA value(s) for the ASI_MMU_* (0x58) register.
 */
#define	MMU_TAG_ACCESS		0x30

/*
 * sun4v format TTE definitions.
 *
 * Note that these were defined with shifts but the assembler complained.
 */
#define	TTE4V_V		0x8000000000000000	/* <63> valid */
#define	TTE4V_NFO	0x4000000000000000	/* <62> non-fault only */
#define	TTE4V_PAR	0x2000000000000000	/* <61> KT hw parity, N2 lock */
#define	TTE4V_PA_SHIFT	13		/* <55:13> PA, only 43:13 used */
#define	TTE4V_IE	0x1000		/* <12> invert endianness */
#define	TTE4V_E		0x0800		/* <11> side effect */
#define	TTE4V_CP	0x0400		/* <10> physically cacheable */
#define	TTE4V_CV	0x0200		/* <9> virtually cachable */
#define	TTE4V_P		0x0100		/* <8> privilege required */
#define	TTE4V_X		0x0080		/* <7> execute perm */
#define	TTE4V_W		0x0040		/* <6> write perm */
#define	TTE4V_REF	0x0020		/* <5> sw - ref */
#define	TTE4V_W_PERM	0x0010		/* <4> sw - write perm */
#define	TTE4V_SZ	0x0		/* <3:0> pagesize */

#define	TTE4V_SZ_8K	0x0
#define	TTE4V_SZ_64K	0x1
#define	TTE4V_SZ_4M	0x3
#define	TTE4V_SZ_256M	0x5
#define	TTE4V_SZ_MASK	0xf

/* Scratchpad register offset definitions */
#define	HSCRATCH0		0x00	/* first scratch register */
#define	HSCRATCH1		0x08	/* second scratch register */
#define	HSCRATCH2		0x10	/* third scratch register */
#define	HSCRATCH3		0x18	/* fourth scratch register */
#define	HSCRATCH4		0x20	/* fifth scratch register, HV ONLY */
#define	HSCRATCH5		0x28	/* sixth scratch register, HV ONLY */
#define	HSCRATCH6		0x30	/* fifth scratch register */
#define	HSCRATCH7		0x38	/* sixth scratch register */

/* Custom Niagara family HV access definitions used in C and asm */
#define	EI_REG_SSHOT_ENB_SHIFT	30
#define	EI_REG_INJECT_ENB_SHIFT	31
#define	EI_REG_NOERR_SHIFT	32
#define	EI_REG_NOERR_BIT	(1ULL << 32)
#define	EI_REG_ACC_OP_SHIFT	33
#define	EI_REG_ACC_OP_BIT	(1ULL << 33)
#define	EI_REG_ACC_LOAD_SHIFT	34
#define	EI_REG_ACC_LOAD_BIT	(1ULL << 34)
#define	EI_REG_ACC_PCX_SHIFT	35
#define	EI_REG_ACC_PCX_BIT	(1ULL << 35)

#define	EI_REG_CMP_NOERR	0x1
#define	EI_REG_CMP_OP		0x2
#define	EI_REG_CMP_LOAD		0x4

#ifdef	__cplusplus
}
#endif

#endif	/* _MEMTEST_V_ASM_H */
