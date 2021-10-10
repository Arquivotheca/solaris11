/*
 * Copyright (c) 2001, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _MEMTESTIO_H
#define	_MEMTESTIO_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <sys/inttypes.h>

/*
 * Boolean definitions.
 */
#define	TRUE	1
#define	FALSE	0

#define	FAIL	1
#define	PASS	0

/*
 * Memtest ioctl commands.
 */
#define	MEMTESTIOC		('M' << 8)
#define	MEMTEST_SETDEBUG	(MEMTESTIOC | 0)
#define	MEMTEST_GETSYSINFO	(MEMTESTIOC | 1)
#define	MEMTEST_GETCPUINFO	(MEMTESTIOC | 2)
#define	MEMTEST_INJECT_ERROR	(MEMTESTIOC | 3)
#define	MEMTEST_MEMREQ		(MEMTESTIOC | 4)
#define	MEMTEST_SETKVARS	(MEMTESTIOC | 5)
#define	MEMTEST_ENABLE_ERRORS	(MEMTESTIOC | 6)
#define	MEMTEST_FLUSH_CACHE	(MEMTESTIOC | 7)

/*
 * The following definitions are used to encode all the supported errors
 * that can be injected.  Errors are encoded using the following fields:
 *
 *	CPU | CLASS | SUBCLASS | TRAP | PROT | MODE | ACCESS | MISC
 */

/*
 * These bit field shift definitions are first so that the defs
 * below may be presented in their logical (hi->low) order.
 */
#define	ERR_MISC_SHIFT		(0)			/* misc bits[19:0] */
#define	ERR_ACC_SHIFT		(ERR_MISC_SHIFT  + 20)	/* acc bits[27:20] */
#define	ERR_MODE_SHIFT		(ERR_ACC_SHIFT   + 8)	/* mode bits[31:28] */
#define	ERR_PROT_SHIFT		(ERR_MODE_SHIFT  + 4)	/* prot bits[35:32] */
#define	ERR_TRAP_SHIFT		(ERR_PROT_SHIFT  + 4)	/* trap bits[39:36] */
#define	ERR_SUBCL_SHIFT		(ERR_TRAP_SHIFT  + 4)	/* subcl bits[47:40] */
#define	ERR_CLASS_SHIFT		(ERR_SUBCL_SHIFT + 8)	/* class bits[55:48] */
#define	ERR_CPU_SHIFT		(ERR_CLASS_SHIFT + 8)	/* cpu bits[59:56] */

/*
 * The cpu field defines the type of cpus on which the command is supported.
 *
 * Specific definitions for the sun4u and sun4v cpu types are defined in
 * architecture specific memtestio_u.h and memtestio_v.h files.
 */
#define	ERR_CPU_INV		(0x0ULL)	/* invalid */
#define	ERR_CPU_GEN		(0x1ULL)	/* common generic */
#define	ERR_CPU_GENMAX		ERR_CPU_GEN	/* max common value */

#define	ERR_CPU_MASK		(0xf)
#define	ERR_CPU(x)		((x >> ERR_CPU_SHIFT) & ERR_CPU_MASK)

/*
 * The class field splits up all errors into basic error classes.
 *
 * Specific definitions for the sun4u and sun4v classes are defined in
 * the architecture specific memtestio_u.h and memtestio_v.h files.
 */
#define	ERR_CLASS_INV		(0x00ULL)	/* invalid */
#define	ERR_CLASS_MEM		(0x01ULL)	/* memory (DRAM) errors */
#define	ERR_CLASS_BUS		(0x02ULL)	/* system bus errors */
#define	ERR_CLASS_DC		(0x03ULL)	/* level 1 data cache errors */
#define	ERR_CLASS_IC		(0x04ULL)	/* level 1 inst cache errors */
#define	ERR_CLASS_IPB		(0x05ULL)	/* L1 i$ prefetch buf errs */
#define	ERR_CLASS_PC		(0x06ULL)	/* L1 p-cache errs */
#define	ERR_CLASS_L2		(0x07ULL)	/* L2 cache errors */
#define	ERR_CLASS_L2WB		(0x08ULL)	/* L2 cache write-back errors */
#define	ERR_CLASS_L2CP		(0x09ULL)	/* L2 cache copy-back errors */
#define	ERR_CLASS_L3		(0x0aULL)	/* L3 cache errs */
#define	ERR_CLASS_L3WB		(0x0bULL)	/* L3 write-back errs */
#define	ERR_CLASS_L3CP		(0x0cULL)	/* L3 copy-back errs */
#define	ERR_CLASS_DTLB		(0x0dULL)	/* D-TLB parity errors */
#define	ERR_CLASS_ITLB		(0x0eULL)	/* I-TLB parity errors */
#define	ERR_CLASS_INT		(0x0fULL)	/* processor internal errors */
#define	ERR_CLASS_SOC		(0x10ULL)	/* system-on-chip errors */
#define	ERR_CLASS_GENMAX	ERR_CLASS_SOC	/* max common value */

#define	ERR_CLASS_UTIL		(0xffULL)	/* utility functions = 0xff */

#define	ERR_CLASS_MASK		(0xff)
#define	ERR_CLASS(x)		(((x) >> ERR_CLASS_SHIFT) & ERR_CLASS_MASK)
#define	ERR_CLASS_ISMEM(x)	(ERR_CLASS(x) == ERR_CLASS_MEM)
#define	ERR_CLASS_ISBUS(x)	(ERR_CLASS(x) == ERR_CLASS_BUS)
#define	ERR_CLASS_ISDC(x)	(ERR_CLASS(x) == ERR_CLASS_DC)
#define	ERR_CLASS_ISIC(x)	(ERR_CLASS(x) == ERR_CLASS_IC)
#define	ERR_CLASS_ISIPB(x)	(ERR_CLASS(x) == ERR_CLASS_IPB)
#define	ERR_CLASS_ISPC(x)	(ERR_CLASS(x) == ERR_CLASS_PC)
#define	ERR_CLASS_ISL2(x)	(ERR_CLASS(x) == ERR_CLASS_L2)
#define	ERR_CLASS_ISL2WB(x)	(ERR_CLASS(x) == ERR_CLASS_L2WB)
#define	ERR_CLASS_ISL2CP(x)	(ERR_CLASS(x) == ERR_CLASS_L2CP)
#define	ERR_CLASS_ISL3(x)	(ERR_CLASS(x) == ERR_CLASS_L3)
#define	ERR_CLASS_ISL3WB(x)	(ERR_CLASS(x) == ERR_CLASS_L3WB)
#define	ERR_CLASS_ISL3CP(x)	(ERR_CLASS(x) == ERR_CLASS_L3CP)
#define	ERR_CLASS_ISDTLB(x)	(ERR_CLASS(x) == ERR_CLASS_DTLB)
#define	ERR_CLASS_ISITLB(x)	(ERR_CLASS(x) == ERR_CLASS_ITLB)
#define	ERR_CLASS_ISINT(x)	(ERR_CLASS(x) == ERR_CLASS_INT)
#define	ERR_CLASS_ISSOC(x)	(ERR_CLASS(x) == ERR_CLASS_SOC)

#define	ERR_CLASS_ISUTIL(x)	(ERR_CLASS(x) == ERR_CLASS_UTIL)

/*
 * Also define a macro to check for the superclass of any cache.
 */
#define	ERR_CLASS_ISCACHE(x)	(ERR_CLASS_ISDC(x) || ERR_CLASS_ISIC(x) || \
					ERR_CLASS_ISL2(x) ||		   \
					ERR_CLASS_ISL2WB(x) ||		   \
					ERR_CLASS_ISL3(x) ||		   \
					ERR_CLASS_ISL3WB(x) ||		   \
					ERR_CLASS_ISL2CP(x) ||		   \
					ERR_CLASS_ISL3CP(x))

/*
 * The sub-class field further defines the error within its class.
 *
 * Specific definitions for the sun4u and sun4v sub-classes are defined in
 * the architecture specific memtestio_u.h and memtestio_v.h files.
 */
#define	ERR_SUBCL_INV		(0x00ULL)	/* invalid */
#define	ERR_SUBCL_NONE		(0x01ULL)	/* invalid */
#define	ERR_SUBCL_INTR		(0x02ULL)	/* interrupt vector error */
#define	ERR_SUBCL_DATA		(0x03ULL)	/* data error */
#define	ERR_SUBCL_TAG		(0x04ULL)	/* tag error */
#define	ERR_SUBCL_MH		(0x05ULL)	/* multi-hit tag err */
#define	ERR_SUBCL_IREG		(0x06ULL)	/* int reg file err */
#define	ERR_SUBCL_FREG		(0x07ULL)	/* FP reg file err */
#define	ERR_SUBCL_GENMAX	ERR_SUBCL_FREG	/* max common value */

#define	ERR_SUBCL_MASK		(0xff)
#define	ERR_SUBCLASS(x)		(((x) >> ERR_SUBCL_SHIFT) & ERR_SUBCL_MASK)
#define	ERR_SUBCLASS_ISINTR(x)	(ERR_SUBCLASS(x) == ERR_SUBCL_INTR)
#define	ERR_SUBCLASS_ISDATA(x)	(ERR_SUBCLASS(x) == ERR_SUBCL_DATA)
#define	ERR_SUBCLASS_ISTAG(x)	(ERR_SUBCLASS(x) == ERR_SUBCL_TAG)
#define	ERR_SUBCLASS_ISMH(x)	(ERR_SUBCLASS(x) == ERR_SUBCL_MH)
#define	ERR_SUBCLASS_ISIREG(x)	(ERR_SUBCLASS(x) == ERR_SUBCL_IREG)
#define	ERR_SUBCLASS_ISFREG(x)	(ERR_SUBCLASS(x) == ERR_SUBCL_FREG)

/*
 * The trap field defines the type of trap that is generated by the error.
 */
#define	ERR_TRAP_INV		(0x0ULL)	/* invalid */
#define	ERR_TRAP_NONE		(0x1ULL)	/* no trap */
#define	ERR_TRAP_PRE		(0x2ULL)	/* precise trap */
#define	ERR_TRAP_DIS		(0x3ULL)	/* disrupting trap */
#define	ERR_TRAP_DEF		(0x4ULL)	/* deferred trap */
#define	ERR_TRAP_FATAL		(0x5ULL)	/* fatal reset */
#define	ERR_TRAP_GENMAX		ERR_TRAP_FATAL	/* max common value */

#define	ERR_TRAP_MASK		(0xf)
#define	ERR_TRAP(x)		(((x) >> ERR_TRAP_SHIFT) & ERR_TRAP_MASK)
#define	ERR_TRAP_ISPRE(x)	(ERR_TRAP(x) == ERR_TRAP_PRE)
#define	ERR_TRAP_ISDIS(x)	(ERR_TRAP(x) == ERR_TRAP_DIS)
#define	ERR_TRAP_ISDEF(x)	(ERR_TRAP(x) == ERR_TRAP_DEF)
#define	ERR_TRAP_ISFAT(x)	(ERR_TRAP(x) == ERR_TRAP_FATAL)

/*
 * The protection field defines the type of protection that is triggered
 * by the error.
 */
#define	ERR_PROT_INV		(0x0ULL)	/* invalid */
#define	ERR_PROT_NONE		(0x1ULL)	/* none */
#define	ERR_PROT_UE		(0x2ULL)	/* uncorrectable ecc */
#define	ERR_PROT_CE		(0x3ULL)	/* correctable ecc */
#define	ERR_PROT_PE		(0x4ULL)	/* parity */
#define	ERR_PROT_BUS		(0x5ULL)	/* bus error */
#define	ERR_PROT_GENMAX		ERR_PROT_BUS	/* max common value */

#define	ERR_PROT_MASK		(0xf)
#define	ERR_PROT(x)		(((x) >> ERR_PROT_SHIFT) & ERR_PROT_MASK)
#define	ERR_PROT_ISUE(x)	(ERR_PROT(x) == ERR_PROT_UE)
#define	ERR_PROT_ISCE(x)	(ERR_PROT(x) == ERR_PROT_CE)
#define	ERR_PROT_ISPE(x)	(ERR_PROT(x) == ERR_PROT_PE)
#define	ERR_PROT_ISBUS(x)	(ERR_PROT(x) == ERR_PROT_BUS)

/*
 * The mode field defines what is accessing the data and how.
 */
#define	ERR_MODE_INV		(0x0ULL)	/* invalid */
#define	ERR_MODE_NONE		(0x1ULL)	/* unknown */
#define	ERR_MODE_HYPR		(0x2ULL)	/* hypervisor access */
#define	ERR_MODE_KERN		(0x3ULL)	/* kernel access */
#define	ERR_MODE_USER		(0x4ULL)	/* user access */
#define	ERR_MODE_DMA		(0x5ULL)	/* dma access */
#define	ERR_MODE_OBP		(0x6ULL)	/* obp access */
#define	ERR_MODE_UDMA		(0x7ULL)	/* user dma access */
#define	ERR_MODE_GENMAX		ERR_MODE_UDMA	/* max common value */

#define	ERR_MODE_MASK		(0xf)
#define	ERR_MODE(x)		(((x) >> ERR_MODE_SHIFT) & ERR_MODE_MASK)
#define	ERR_MODE_ISHYPR(x)	(ERR_MODE(x) == ERR_MODE_HYPR)
#define	ERR_MODE_ISKERN(x)	(ERR_MODE(x) == ERR_MODE_KERN)
#define	ERR_MODE_ISUSER(x)	(ERR_MODE(x) == ERR_MODE_USER)
#define	ERR_MODE_ISDMA(x)	(ERR_MODE(x) == ERR_MODE_DMA)
#define	ERR_MODE_ISOBP(x)	(ERR_MODE(x) == ERR_MODE_OBP)
#define	ERR_MODE_ISUDMA(x)	(ERR_MODE(x) == ERR_MODE_UDMA)

/*
 * The access field defines the type of access that generates the error.
 */
#define	ERR_ACC_INV		(0x00ULL)	/* invalid */
#define	ERR_ACC_NONE		(0x01ULL)	/* unknown */
#define	ERR_ACC_LOAD		(0x02ULL)	/* data load */
#define	ERR_ACC_BLOAD		(0x03ULL)	/* block load (fp state) */
#define	ERR_ACC_STORE		(0x04ULL)	/* store merge */
#define	ERR_ACC_FETCH		(0x05ULL)	/* instruction fetch */
#define	ERR_ACC_PFETCH		(0x06ULL)	/* prefetch */
#define	ERR_ACC_ASI		(0x07ULL)	/* asi load */
#define	ERR_ACC_ASR		(0x08ULL)	/* asr load */
#define	ERR_ACC_PCX		(0x09ULL)	/* memory/L2 xbar */
#define	ERR_ACC_SCRB		(0x0aULL)	/* memory/L2 scrub acc */
#define	ERR_ACC_SP		(0x0bULL)	/* service processor acc */
#define	ERR_ACC_GENMAX		ERR_ACC_SP	/* max common value */

#define	ERR_ACC_MASK		(0xff)
#define	ERR_ACC(x)		(((x) >> ERR_ACC_SHIFT) & ERR_ACC_MASK)
#define	ERR_ACC_ISLOAD(x)	(ERR_ACC(x) == ERR_ACC_LOAD)
#define	ERR_ACC_ISBLOAD(x)	(ERR_ACC(x) == ERR_ACC_BLOAD)
#define	ERR_ACC_ISSTORE(x)	(ERR_ACC(x) == ERR_ACC_STORE)
#define	ERR_ACC_ISFETCH(x)	(ERR_ACC(x) == ERR_ACC_FETCH)
#define	ERR_ACC_ISPFETCH(x)	(ERR_ACC(x) == ERR_ACC_PFETCH)
#define	ERR_ACC_ISASI(x)	(ERR_ACC(x) == ERR_ACC_ASI)
#define	ERR_ACC_ISASR(x)	(ERR_ACC(x) == ERR_ACC_ASR)
#define	ERR_ACC_ISPCX(x)	(ERR_ACC(x) == ERR_ACC_PCX)
#define	ERR_ACC_ISSP(x)		(ERR_ACC(x) == ERR_ACC_SP)
#define	ERR_ACC_ISSCRB(x)	(ERR_ACC(x) == ERR_ACC_SCRB)

/*
 * These miscellaneous field help to further encode commands that
 * could not be encoded with just the other fields.
 *
 * Note that NIMP and NSUP are defined to be the same. Functionally they
 * behave the same, but the comments use the different definitions to
 * provide more info.
 *	NIMP = test could, should,   or will  be written
 *	NSUP = test can't, shouldn't or won't be written
 *
 * Note that this field is not defined in the same way as the other fields,
 * here each MISC option is given it's own bit so that multiple bits can
 * be set simultaneously to produce test variations.
 */
#define	ERR_MISC_INV		(0x0ULL)	/* invalid */
#define	ERR_MISC_NONE		(0x1ULL)	/* nothing */
#define	ERR_MISC_COPYIN		(0x2ULL)	/* copyin error */
#define	ERR_MISC_TL1		(0x4ULL)	/* trap level 1 error */
#define	ERR_MISC_DDIPEEK	(0x8ULL)	/* DDI peek error */
#define	ERR_MISC_PHYS		(0x10ULL)	/* paddr error w/o mod state */
#define	ERR_MISC_REAL		(0x20ULL)	/* raddr error w/o mod state */
#define	ERR_MISC_VIRT		(0x40ULL)	/* vaddr error with mod state */
#define	ERR_MISC_STORM		(0x80ULL)	/* storm of errors */
#define	ERR_MISC_ORPHAN		(0x100ULL)	/* orphaned error */
#define	ERR_MISC_PCR		(0x200ULL)	/* %pc relative instr error */
#define	ERR_MISC_PEEK		(0x400ULL)	/* poke error */
#define	ERR_MISC_POKE		(0x800ULL)	/* poke error */
#define	ERR_MISC_ENABLE		(0x1000ULL)	/* enable/disable errors */
#define	ERR_MISC_FLUSH		(0x2000ULL)	/* flush processor cache(s) */
#define	ERR_MISC_RAND		(0x4000ULL)	/* random error */
#define	ERR_MISC_PIO		(0x8000ULL)	/* PIO error */
#define	ERR_MISC_STICKY		(0x10000ULL)	/* sticky error */
#define	ERR_MISC_GENMAX		ERR_MISC_STICKY	/* max common value */

#define	ERR_MISC_NOTIMP		(0x80000ULL)	/* not implemented=20-bit max */
#define	ERR_MISC_NOTSUP		(0x80000ULL)	/* not supported=20-bit max */

#define	ERR_MISC_MASK		(0xfffff)	/* 20-bit field */
#define	ERR_MISC(x)		(((x) >> ERR_MISC_SHIFT) & ERR_MISC_MASK)
#define	ERR_MISC_ISCOPYIN(x)	(ERR_MISC(x) & ERR_MISC_COPYIN)
#define	ERR_MISC_ISTL1(x)	(ERR_MISC(x) & ERR_MISC_TL1)
#define	ERR_MISC_ISDDIPEEK(x)	(ERR_MISC(x) & ERR_MISC_DDIPEEK)
#define	ERR_MISC_ISPOKE(x)	(ERR_MISC(x) & ERR_MISC_POKE)
#define	ERR_MISC_ISPHYS(x)	(ERR_MISC(x) & ERR_MISC_PHYS)
#define	ERR_MISC_ISREAL(x)	(ERR_MISC(x) & ERR_MISC_REAL)
#define	ERR_MISC_ISVIRT(x)	(ERR_MISC(x) & ERR_MISC_VIRT)
#define	ERR_MISC_ISSTORM(x)	(ERR_MISC(x) & ERR_MISC_STORM)
#define	ERR_MISC_ISORPHAN(x)	(ERR_MISC(x) & ERR_MISC_ORPHAN)
#define	ERR_MISC_ISPCR(x)	(ERR_MISC(x) & ERR_MISC_PCR)
#define	ERR_MISC_ISPEEK(x)	(ERR_MISC(x) & ERR_MISC_PEEK)
#define	ERR_MISC_ISPOKE(x)	(ERR_MISC(x) & ERR_MISC_POKE)
#define	ERR_MISC_ISENABLE(x)	(ERR_MISC(x) & ERR_MISC_ENABLE)
#define	ERR_MISC_ISFLUSH(x)	(ERR_MISC(x) & ERR_MISC_FLUSH)
#define	ERR_MISC_ISRAND(x)	(ERR_MISC(x) & ERR_MISC_RAND)
#define	ERR_MISC_ISPIO(x)	(ERR_MISC(x) & ERR_MISC_PIO)
#define	ERR_MISC_ISSTICKY(x)	(ERR_MISC(x) & ERR_MISC_STICKY)
#define	ERR_MISC_ISNOTIMP(x)	(ERR_MISC(x) & ERR_MISC_NOTIMP)
#define	ERR_MISC_ISNOTSUP(x)	(ERR_MISC(x) & ERR_MISC_NOTSUP)

/*
 * Since the PHYS, REAL, and VIRT test types have been designed to
 * impact the system as little as possible often these tests need
 * to use special code paths, this definition aids in that.
 */
#define	ERR_MISC_ISLOWIMPACT(x)	(ERR_MISC_ISPHYS(x) || ERR_MISC_ISREAL(x) || \
					ERR_MISC_ISVIRT(x))

/*
 * Short definitions to help make the command encodings more readable.
 *
 * Other short definitions for the sun4u and sun4v are defined in
 * the architecture specific memtestio_u.h and memtestio_v.h files.
 */
#define	GEN	(ERR_CPU_GEN	 << ERR_CPU_SHIFT)

#define	NA6	(ERR_CLASS_INV	 << ERR_CLASS_SHIFT)
#define	BUS	(ERR_CLASS_BUS	 << ERR_CLASS_SHIFT)
#define	MEM	(ERR_CLASS_MEM	 << ERR_CLASS_SHIFT)
#define	DC	(ERR_CLASS_DC	 << ERR_CLASS_SHIFT)
#define	IC	(ERR_CLASS_IC	 << ERR_CLASS_SHIFT)
#define	IPB	(ERR_CLASS_IPB	 << ERR_CLASS_SHIFT)
#define	PC	(ERR_CLASS_PC	 << ERR_CLASS_SHIFT)
#define	L2	(ERR_CLASS_L2	 << ERR_CLASS_SHIFT)
#define	L2WB	(ERR_CLASS_L2WB	 << ERR_CLASS_SHIFT)
#define	L2CP	(ERR_CLASS_L2CP	 << ERR_CLASS_SHIFT)
#define	L3	(ERR_CLASS_L3	 << ERR_CLASS_SHIFT)
#define	L3WB	(ERR_CLASS_L3WB	 << ERR_CLASS_SHIFT)
#define	L3CP	(ERR_CLASS_L3CP	 << ERR_CLASS_SHIFT)
#define	DTLB	(ERR_CLASS_DTLB	 << ERR_CLASS_SHIFT)
#define	ITLB	(ERR_CLASS_ITLB	 << ERR_CLASS_SHIFT)
#define	INT	(ERR_CLASS_INT	 << ERR_CLASS_SHIFT)
#define	SOC	(ERR_CLASS_SOC	 << ERR_CLASS_SHIFT)
#define	UTIL	(ERR_CLASS_UTIL	 << ERR_CLASS_SHIFT)

#define	NA5	(ERR_SUBCL_NONE	 << ERR_SUBCL_SHIFT)
#define	INTR	(ERR_SUBCL_INTR	 << ERR_SUBCL_SHIFT)
#define	DATA	(ERR_SUBCL_DATA	 << ERR_SUBCL_SHIFT)
#define	TAG	(ERR_SUBCL_TAG	 << ERR_SUBCL_SHIFT)
#define	MH	(ERR_SUBCL_MH	 << ERR_SUBCL_SHIFT)
#define	IREG	(ERR_SUBCL_IREG	 << ERR_SUBCL_SHIFT)
#define	FREG	(ERR_SUBCL_FREG	 << ERR_SUBCL_SHIFT)

#define	NA4	(ERR_TRAP_NONE	 << ERR_TRAP_SHIFT)
#define	PRE	(ERR_TRAP_PRE	 << ERR_TRAP_SHIFT)
#define	DIS	(ERR_TRAP_DIS	 << ERR_TRAP_SHIFT)
#define	DEF	(ERR_TRAP_DEF	 << ERR_TRAP_SHIFT)
#define	FAT	(ERR_TRAP_FATAL	 << ERR_TRAP_SHIFT)

#define	NA3	(ERR_PROT_NONE	 << ERR_PROT_SHIFT)
#define	CE	(ERR_PROT_CE	 << ERR_PROT_SHIFT)
#define	UE	(ERR_PROT_UE	 << ERR_PROT_SHIFT)
#define	PE	(ERR_PROT_PE	 << ERR_PROT_SHIFT)
#define	BE	(ERR_PROT_BUS	 << ERR_PROT_SHIFT)

#define	NA2	(ERR_MODE_NONE	 << ERR_MODE_SHIFT)
#define	HYPR	(ERR_MODE_HYPR	 << ERR_MODE_SHIFT)
#define	KERN	(ERR_MODE_KERN	 << ERR_MODE_SHIFT)
#define	USER	(ERR_MODE_USER	 << ERR_MODE_SHIFT)
#define	DMA	(ERR_MODE_DMA	 << ERR_MODE_SHIFT)
#define	OBP	(ERR_MODE_OBP	 << ERR_MODE_SHIFT)
#define	UDMA	(ERR_MODE_UDMA   << ERR_MODE_SHIFT)

#define	NA1	(ERR_ACC_NONE	 << ERR_ACC_SHIFT)
#define	LOAD	(ERR_ACC_LOAD	 << ERR_ACC_SHIFT)
#define	BLD	(ERR_ACC_BLOAD	 << ERR_ACC_SHIFT)
#define	STOR	(ERR_ACC_STORE	 << ERR_ACC_SHIFT)
#define	FETC	(ERR_ACC_FETCH	 << ERR_ACC_SHIFT)
#define	PFETC	(ERR_ACC_PFETCH	 << ERR_ACC_SHIFT)
#define	ASI	(ERR_ACC_ASI	 << ERR_ACC_SHIFT)
#define	ASR	(ERR_ACC_ASR	 << ERR_ACC_SHIFT)
#define	PCX	(ERR_ACC_PCX	 << ERR_ACC_SHIFT)
#define	SCRB	(ERR_ACC_SCRB	 << ERR_ACC_SHIFT)
#define	SP	(ERR_ACC_SP	 << ERR_ACC_SHIFT)

#define	NA0	(ERR_MISC_NONE   << ERR_MISC_SHIFT)
#define	CPIN	(ERR_MISC_COPYIN << ERR_MISC_SHIFT)
#define	TL1	(ERR_MISC_TL1	 << ERR_MISC_SHIFT)
#define	DDIPEEK	(ERR_MISC_DDIPEEK<< ERR_MISC_SHIFT)
#define	PHYS	(ERR_MISC_PHYS	 << ERR_MISC_SHIFT)
#define	REAL	(ERR_MISC_REAL	 << ERR_MISC_SHIFT)
#define	VIRT	(ERR_MISC_VIRT	 << ERR_MISC_SHIFT)
#define	STORM	(ERR_MISC_STORM	 << ERR_MISC_SHIFT)
#define	ORPH	(ERR_MISC_ORPHAN << ERR_MISC_SHIFT)
#define	PCR	(ERR_MISC_PCR    << ERR_MISC_SHIFT)
#define	PEEK	(ERR_MISC_PEEK	 << ERR_MISC_SHIFT)
#define	POKE	(ERR_MISC_POKE	 << ERR_MISC_SHIFT)
#define	ENB	(ERR_MISC_ENABLE << ERR_MISC_SHIFT)
#define	FLSH	(ERR_MISC_FLUSH	 << ERR_MISC_SHIFT)
#define	RAND	(ERR_MISC_RAND	 << ERR_MISC_SHIFT)
#define	MPIO	(ERR_MISC_PIO	 << ERR_MISC_SHIFT)
#define	STKY	(ERR_MISC_STICKY << ERR_MISC_SHIFT)
#define	NS	(ERR_MISC_NOTSUP << ERR_MISC_SHIFT)
#define	NI	(ERR_MISC_NOTIMP << ERR_MISC_SHIFT)

/*
 * These definitions are used to set/check flags which indicate
 * what options and/or arguments were specified by the user.
 */
#define	FLAGS_ADDR		(0x00000001)	/* phys/virt address spec'd */
#define	FLAGS_CHKBIT		(0x00000002)	/* corrupt checkbits not data */
#define	FLAGS_USER_DEBUG	(0x00000004)	/* enable user level debug */
#define	FLAGS_KERN_DEBUG	(0x00000008)	/* enable kern level debug */
#define	FLAGS_DELAY		(0x00000010)	/* delay in sec */
#define	FLAGS_NOERR		(0x00000020)	/* don't invoke errs */
#define	FLAGS_VERBOSE		(0x00000040)	/* enable verbose messages */
#define	FLAGS_CORRUPT_OFFSET	(0x00000080)	/* corruption offset spec'd */
#define	FLAGS_ACCESS_OFFSET	(0x00000100)	/* access offset spec'd */
#define	FLAGS_RANDOM		(0x00000200)	/* enable random mode */
#define	FLAGS_SYSLOG		(0x00000400)	/* enable syslog(3) logging */
#define	FLAGS_XORPAT		(0x00000800)	/* xor pattern specified */
#define	FLAGS_MISC1		(0x00001000)	/* 1st command arg spec'd */
#define	FLAGS_MISC2		(0x00002000)	/* 2nd command arg spec'd */
#define	FLAGS_PID		(0x00004000)	/* user process id specified */
#define	FLAGS_BINDCPU		(0x00008000)	/* bind thread(s) to cpu(s) */
#define	FLAGS_BINDMEM		(0x00010000)	/* bind memory to test */
#define	FLAGS_STORE		(0x00020000)	/* store to invoke the error */
#define	FLAGS_XCALL		(0x00040000)	/* cross-call to inject error */
#define	FLAGS_SET_EER		(0x00080000)	/* set L2$ err enable reg */
#define	FLAGS_SET_ECCR		(0x00100000)	/* set L2$ control reg */
#define	FLAGS_SET_DCR		(0x00200000)	/* set dispatch control reg */
#define	FLAGS_SET_DCUCR		(0x00400000)	/* set D$ unit control reg */
#define	FLAGS_NOSET_EERS	(0x00800000)	/* don't set err enable regs */
#define	FLAGS_CACHE_CLN		(0x01000000)	/* force $line state to clean */
#define	FLAGS_CACHE_DIRTY	(0x02000000)	/* force $line state to dirty */
#define	FLAGS_MAP_KERN_BUF	(0x04000000)	/* force use of kern buffer */
#define	FLAGS_MAP_USER_BUF	(0x08000000)	/* force use of user buffer */
#define	FLAGS_FLUSH_DIS		(0x10000000)	/* disable pre-inj $ flush */
#define	FLAGS_MEMSCRUB_DISABLE	(0x20000000)	/* disable mem scrubber */
#define	FLAGS_L2SCRUB_DISABLE	(0x40000000)	/* disable L2$ scrubber */
#define	FLAGS_MEMSCRUB_ENABLE	(0x80000000)	/* enable mem scrubber */
#define	FLAGS_L2SCRUB_ENABLE	(0x000100000000ULL) /* enable L2$ scrubber */
#define	FLAGS_MEMSCRUB_ASIS	(0x000200000000ULL) /* keep mem scrubber asis */
#define	FLAGS_L2SCRUB_ASIS	(0x000400000000ULL) /* keep L2$ scrubber asis */
#define	FLAGS_INF_INJECT	(0x000800000000ULL) /* set infinite injection */
#define	FLAGS_I_COUNT		(0x001000000000ULL) /* iterate inject/invoke */
#define	FLAGS_I_STRIDE		(0x002000000000ULL) /* stride for iterations */
#define	FLAGS_CACHE_INDEX	(0x004000000000ULL) /* specify a cache index */
#define	FLAGS_CACHE_WAY		(0x008000000000ULL) /* specify a cache way */

/*
 * These definitions are used to set/check qflags which indicate
 * quiece'ing options that were specified by the user or test type.
 */
#define	QFLAGS_OFFLINE_CPUS_EN	(0x0000000001)	/* allow unused cpus offlined */
#define	QFLAGS_OFFLINE_CPUS_DIS	(0x0000000002)	/* do not offline unused cpus */
#define	QFLAGS_OFFLINE_SIB_CPUS	(0x0000000004)	/* only offline sibling cpus */
#define	QFLAGS_ONLINE_CPUS_BFI	(0x0000000008)	/* offline for inject, */
						/* online before the invoke */
#define	QFLAGS_PAUSE_CPUS_EN	(0x0000000010)	/* allow pause_cpus() call */
#define	QFLAGS_PAUSE_CPUS_DIS	(0x0000000020)	/* disable pause_cpus() call */
#define	QFLAGS_UNPAUSE_CPUS_BFI	(0x0000000040)	/* pause for inject, */
						/* unpause before the invoke */
#define	QFLAGS_PARK_CPUS_EN	(0x0000000080)	/* allow cpus to be parked */
#define	QFLAGS_PARK_CPUS_DIS	(0x0000000100)	/* do not park cpus */
#define	QFLAGS_UNPARK_CPUS_BFI	(0x0000000200)	/* park for inject, */
						/* unpark before the invoke */
#define	QFLAGS_CYC_SUSPEND_EN	(0x0000000400)	/* suspend cyclics for inject */

/*
 * Maximum number of threads currently supported.
 * Multi-threaded tests typically have one producer and one consumer.
 */
#define	MAX_NTHREADS		2

/*
 * CPU to memory error injection relationships.
 *
 * LOCAL means that CPUs can only inject errors into "local" memory.
 */
#define	MEMFLAGS_LOCAL		1

/*
 * Definitions used by thread synchronization
 * routine wait_sync().
 */
#define	SYNC_WAIT_FOREVER	0
#define	SYNC_WAIT_MAX		10000000	/* in microseconds */
#define	SYNC_WAIT_MIN		1000000		/* in microseconds */
#define	SYNC_STATUS_OK		0
#define	SYNC_STATUS_TIMEOUT	1
#define	SYNC_STATUS_ERROR	2

/*
 * This is the user IO structure used to pass command information
 * to the driver. This structure is data model independent.
 *
 * XXX	specific members of this struct are NOT sun4u/4v common, they
 *	are the four error register fields , perhaps a union
 *	can be used to keep this structure for both architectures.
 */
typedef struct ioc {
	uint64_t	ioc_flags;	/* flags */
	uint64_t	ioc_qflags;	/* quiece flags for operation */
	uint_t		ioc_delay;	/* delay factor for some commands  */
	uint64_t	ioc_command;	/* encoded error type command */
	int		ioc_cpu;	/* bound cpu */
	uint64_t	ioc_addr;	/* command specific addr/offset */
	uint64_t	ioc_xorpat;	/* XOR pattern used for corruption */
	uint64_t	ioc_misc1;	/* command specific data */
	uint64_t	ioc_misc2;	/* command specific data */
	uint64_t	ioc_cache_idx;	/* cache index for certain tests */
	uint_t		ioc_cache_way;	/* cache way for certain tests */
	uint64_t	ioc_i_count;	/* # of times to inj/invoke error */
	uint64_t	ioc_i_stride;	/* iterate corruption/access offsets */
	int		ioc_c_offset;	/* corruption offset */
	int		ioc_a_offset;	/* access offset */
	char		ioc_cmdstr[20];	/* command string */
	uint64_t	ioc_databuf;	/* user allocated buffer vaddr */
	uint64_t	ioc_bufsize;	/* size of user buffer */
	uint64_t	ioc_bufbase;	/* base vaddr of user buffer */
	int		ioc_pid;	/* process id to affect */
	int		ioc_xc_cpu;	/* cpu to cross-call */
	uint64_t	ioc_eer;	/* value to set EER to */
	uint64_t	ioc_ecr;	/* value to set ECCR to */
	uint64_t	ioc_dcr;	/* value to set DCR to */
	uint64_t	ioc_dcucr;	/* value to set DCUCR to */
	int		ioc_bind_mem_criteria;
	int		ioc_nthreads;	/* # of threads required for test */
	int		ioc_bind_thr_criteria[MAX_NTHREADS];
	uint64_t	ioc_bind_thr_data[MAX_NTHREADS];
	int		ioc_thr2cpu_binding[MAX_NTHREADS];
} ioc_t;

/*
 * These definitions are used to get values from an IO control buffer.
 */
#define	IOC_FLAGS(iocp)		((iocp)->ioc_flags)
#define	IOC_QFLAGS(iocp)	((iocp)->ioc_qflags)
#define	IOC_DELAY(iocp)		((iocp)->ioc_delay)
#define	IOC_COMMAND(iocp)	((iocp)->ioc_command)
#define	IOC_ADDR(iocp)		((iocp)->ioc_addr)
#define	IOC_XORPAT(iocp)	((iocp)->ioc_xorpat)
#define	IOC_MISC(iocp)		((iocp)->ioc_misc1)
#define	IOC_MISC2(iocp)		((iocp)->ioc_misc2)
#define	IOC_I_COUNT(iocp)	((iocp)->ioc_i_count)
#define	IOC_I_STRIDE(iocp)	((iocp)->ioc_i_stride)
#define	IOC_A_OFFSET(iocp)	((iocp)->ioc_a_offset)
#define	IOC_C_OFFSET(iocp)	((iocp)->ioc_c_offset)
#define	IOC_PID(iocp)		((iocp)->ioc_pid)

/*
 * These definitions are used to check option settings within
 * the flags element of the IO control buffer.
 */
#define	F_ADDR(iocp)		((iocp)->ioc_flags & FLAGS_ADDR)
#define	F_DELAY(iocp)		((iocp)->ioc_flags & FLAGS_DELAY)
#define	F_VERBOSE(iocp)		((iocp)->ioc_flags & FLAGS_VERBOSE)
#define	F_CHKBIT(iocp)		((iocp)->ioc_flags & FLAGS_CHKBIT)
#define	F_RANDOM(iocp)		((iocp)->ioc_flags & FLAGS_RANDOM)
#define	F_XORPAT(iocp)		((iocp)->ioc_flags & FLAGS_XORPAT)
#define	F_MISC1(iocp)		((iocp)->ioc_flags & FLAGS_MISC1)
#define	F_MISC2(iocp)		((iocp)->ioc_flags & FLAGS_MISC2)
#define	F_I_COUNT(iocp)		((iocp)->ioc_flags & FLAGS_I_COUNT)
#define	F_I_STRIDE(iocp)	((iocp)->ioc_flags & FLAGS_I_STRIDE)
#define	F_NOERR(iocp)		((iocp)->ioc_flags & FLAGS_NOERR)
#define	F_USER_DEBUG(iocp)	((iocp)->ioc_flags & FLAGS_USER_DEBUG)
#define	F_KERN_DEBUG(iocp)	((iocp)->ioc_flags & FLAGS_KERN_DEBUG)
#define	F_C_OFFSET(iocp)	((iocp)->ioc_flags & FLAGS_CORRUPT_OFFSET)
#define	F_A_OFFSET(iocp)	((iocp)->ioc_flags & FLAGS_ACCESS_OFFSET)
#define	F_BINDCPU(iocp)		((iocp)->ioc_flags & FLAGS_BINDCPU)
#define	F_BINDMEM(iocp)		((iocp)->ioc_flags & FLAGS_BINDMEM)
#define	F_PID(iocp)		((iocp)->ioc_flags & FLAGS_PID)
#define	F_STORE(iocp)		((iocp)->ioc_flags & FLAGS_STORE)
#define	F_XCALL(iocp)		((iocp)->ioc_flags & FLAGS_XCALL)

/*
 * XXX	something has to be done about the four architecture specific
 *	register options below.  They are NOT common.  Perhaps remove the
 *	options and the fields in the ioc struct.
 */
#define	F_SET_EER(iocp)		((iocp)->ioc_flags & FLAGS_SET_EER)
#define	F_SET_ECCR(iocp)	((iocp)->ioc_flags & FLAGS_SET_ECCR)
#define	F_SET_DCR(iocp)		((iocp)->ioc_flags & FLAGS_SET_DCR)
#define	F_SET_DCUCR(iocp)	((iocp)->ioc_flags & FLAGS_SET_DCUCR)

#define	F_NOSET_EERS(iocp)	((iocp)->ioc_flags & FLAGS_NOSET_EERS)
#define	F_CACHE_CLN(iocp)	((iocp)->ioc_flags & FLAGS_CACHE_CLN)
#define	F_CACHE_DIRTY(iocp)	((iocp)->ioc_flags & FLAGS_CACHE_DIRTY)
#define	F_CACHE_INDEX(iocp)	((iocp)->ioc_flags & FLAGS_CACHE_INDEX)
#define	F_CACHE_WAY(iocp)	((iocp)->ioc_flags & FLAGS_CACHE_WAY)
#define	F_MAP_KERN_BUF(iocp)	((iocp)->ioc_flags & FLAGS_MAP_KERN_BUF)
#define	F_MAP_USER_BUF(iocp)	((iocp)->ioc_flags & FLAGS_MAP_USER_BUF)
#define	F_FLUSH_DIS(iocp)	((iocp)->ioc_flags & FLAGS_FLUSH_DIS)
#define	F_MEMSCRUB_DIS(iocp)	((iocp)->ioc_flags & FLAGS_MEMSCRUB_DISABLE)
#define	F_MEMSCRUB_EN(iocp)	((iocp)->ioc_flags & FLAGS_MEMSCRUB_ENABLE)
#define	F_MEMSCRUB_ASIS(iocp)	((iocp)->ioc_flags & FLAGS_MEMSCRUB_ASIS)
#define	F_L2SCRUB_DIS(iocp)	((iocp)->ioc_flags & FLAGS_L2SCRUB_DISABLE)
#define	F_L2SCRUB_EN(iocp)	((iocp)->ioc_flags & FLAGS_L2SCRUB_ENABLE)
#define	F_L2SCRUB_ASIS(iocp)	((iocp)->ioc_flags & FLAGS_L2SCRUB_ASIS)
#define	F_INF_INJECT(iocp)	((iocp)->ioc_flags & FLAGS_INF_INJECT)

/*
 * These definitions are used to check system quiece'ing settings within
 * the qflags element of the IO control buffer.
 */
#define	QF_OFFLINE_CPUS_EN(iocp)  ((iocp)->ioc_qflags & QFLAGS_OFFLINE_CPUS_EN)
#define	QF_OFFLINE_CPUS_DIS(iocp) ((iocp)->ioc_qflags & QFLAGS_OFFLINE_CPUS_DIS)
#define	QF_OFFLINE_SIB_CPUS(iocp) ((iocp)->ioc_qflags & QFLAGS_OFFLINE_SIB_CPUS)
#define	QF_ONLINE_CPUS_BFI(iocp)  ((iocp)->ioc_qflags & QFLAGS_ONLINE_CPUS_BFI)
#define	QF_PAUSE_CPUS_EN(iocp)	  ((iocp)->ioc_qflags & QFLAGS_PAUSE_CPUS_EN)
#define	QF_PAUSE_CPUS_DIS(iocp)	  ((iocp)->ioc_qflags & QFLAGS_PAUSE_CPUS_DIS)
#define	QF_UNPAUSE_CPUS_BFI(iocp) ((iocp)->ioc_qflags & QFLAGS_UNPAUSE_CPUS_BFI)
#define	QF_PARK_CPUS_EN(iocp)	  ((iocp)->ioc_qflags & QFLAGS_PARK_CPUS_EN)
#define	QF_PARK_CPUS_DIS(iocp)	  ((iocp)->ioc_qflags & QFLAGS_PARK_CPUS_DIS)
#define	QF_UNPARK_CPUS_BFI(iocp)  ((iocp)->ioc_qflags & QFLAGS_UNPARK_CPUS_BFI)
#define	QF_CYC_SUSPEND_EN(iocp)	  ((iocp)->ioc_qflags & QFLAGS_CYC_SUSPEND_EN)

/*
 * This is the system information structure returned by the GETSYSINFO ioctl.
 */
typedef	struct system_info {
	int		s_ncpus;		/* number of cpus */
	int		s_ncpus_online;		/* number of online cpus */
	int		s_maxcpuid;		/* maximum cpuid */
} system_info_t;

/*
 * This is the system information structure returned by the GETCPUINFO ioctl.
 * This information applies to the CPU at the time of the ioctl.
 * The caller should bind itself to a cpu for this information
 * to remain consistent.
 *
 * XXX  There are fields that refer to architecture/platform specific registers
 *	These should be removed from this structure.  Perhaps create a chip-
 *	specific section with definitions and access routines in chip-specific
 *	files and include them here either via a union or a void ptr.
 *	Currently the following is architecture/chip-specific:
 *	 o The fields from c_ecr to c_ducr refer to sun4u specific registers
 *	 o The c_sys_mode and c_l2_ctl fields refer to registers specific
 *	   to Victoria Falls but are also used on subsequent sun4v procs.
 */
typedef	struct	cpu_info {
	int		c_cpuid;		/* cpu id */
	int		c_core_id;		/* cpu core id for CMP */
	uint64_t	c_cpuver;		/* cpu version register */
	uint64_t	c_ecr;			/* e$ control reg */
	uint64_t	c_secr;			/* e$ cfg & timing ctrl reg */
	uint64_t	c_eer;			/* e$ error enable reg */
	uint64_t	c_dcr;			/* dispatch control reg */
	uint64_t	c_dcucr;		/* d-cache unit control reg */
	int		c_dc_size;		/* d-cache size */
	int		c_dc_linesize;		/* d-cache linesize */
	int		c_dc_assoc;		/* d-cache associativity */
	int		c_ic_size;		/* i-cache size */
	int		c_ic_linesize;		/* i-cache linesize */
	int		c_ic_assoc;		/* i-cache associativity */
	int		c_l2_size;		/* l2-cache size */
	int		c_l2_linesize;		/* l2-cache linesize */
	int		c_l2_sublinesize;	/* l2-cache sublinesize */
	int		c_l2_assoc;		/* l2-cache associativity */
	int		c_l2_flushsize;		/* l2-cache flush area size */
	int		c_l3_size;		/* l3-cache size */
	int		c_l3_linesize;		/* l3-cache linesize */
	int		c_l3_sublinesize;	/* l3-cache sublinesize */
	int		c_l3_assoc;		/* l3-cache associativity */
	int		c_l3_flushsize;		/* l3-cache flush area size */
	uint_t		c_shared_caches;	/* bitfield for shared caches */
	int		c_already_chosen;	/* used while choosing cpus */
	int		c_offlined;		/* set if ei offlined cpu */
	uint64_t	c_mem_flags;		/* type of memory */
	uint64_t	c_mem_start;		/* start of local memory */
	uint64_t	c_mem_size;		/* len of local owned */
	/*
	 * sun4v specific information needed for determining what memory and
	 * cpuids are local or foreign to this CPU.
	 */
	uint64_t	c_sys_mode;		/* system mode register */
	uint64_t	c_l2_ctl;		/* L2 ctrl register */
} cpu_info_t;

/*
 * These macros look in the system info structure to determine the cpu type.
 */
#define	CPUVER_IMPL_SHIFT	32
#define	CPUVER_IMPL_MASK	0xFFFF
#define	CPUVER_MASK_SHIFT	24
#define	CPUVER_MASK_MASK	0xFF
#define	CPUVER_MFG_SHIFT	48
#define	CPUVER_MFG_MASK		0xFFFF
#define	CPU_IMPL(ver)	(((ver) >> CPUVER_IMPL_SHIFT) & CPUVER_IMPL_MASK)
#define	CPU_MASK(ver)	(((ver) >> CPUVER_MASK_SHIFT) & CPUVER_MASK_MASK)
#define	CPU_MFG(ver)	(((ver) >> CPUVER_MFG_SHIFT) & CPUVER_MFG_MASK)

/*
 * These define the bitfields for the shared cache flag in the cpu_info struct.
 */
#define	CPU_SHARED_DC		0x00
#define	CPU_SHARED_IC		0x01
#define	CPU_SHARED_L2		0x02
#define	CPU_SHARED_L3		0x04

/*
 * Sub command types for MEMTEST_MEMREQ ioctl.
 * The order of these must be kept consistent with the memory
 * suboption definitions in mtst.c.
 */
#define	MREQ_UVA_TO_PA		1	/* convert user va to pa */
#define	MREQ_UVA_GET_ATTR	2	/* get mapping attributes for uva */
#define	MREQ_UVA_SET_ATTR	3	/* set mapping attributes for uva */
#define	MREQ_UVA_LOCK		4	/* lock pages belonging to uva */
#define	MREQ_UVA_UNLOCK		5	/* unlock pages belonginf to uva */
#define	MREQ_KVA_TO_PA		6	/* convert kernel va to pa */
#define	MREQ_PA_TO_UNUM		7	/* convert pa/synd to unum */
#define	MREQ_FIND_FREE_PAGES	8	/* find free pages in range */
#define	MREQ_LOCK_FREE_PAGES	9	/* find/lock free pages in range */
#define	MREQ_LOCK_PAGES		10	/* lock pages */
#define	MREQ_UNLOCK_PAGES	11	/* unlock pages */
#define	MREQ_IDX_TO_PA		12	/* find pa for cache index */
#define	MREQ_RA_TO_PA		13	/* convert real addr to phys addr */
#define	MREQ_MAX		13	/* max value for memory request */

typedef	struct	mem_req {
	uint_t		m_cmd;		/* subcommand */
	uint_t		m_subcmd;	/* sub subcommand */
	uint64_t	m_vaddr;	/* virtual address */
	int		m_pid;		/* user process id */
	uint64_t	m_paddr1;	/* physical address */
	uint64_t	m_paddr2;	/* second pa required by some cmds */
	uint64_t	m_index;	/* cache index to find maping for */
	uint_t		m_way;		/* cache way to find maping for */
	uint64_t	m_size;		/* length of request */
	uint_t		m_attr;		/* attributes for mapping */
	int		m_synd;		/* syndrome required by some cmds */
	char		m_str[256];	/* string required by some cmds */
} mem_req_t;

/*
 * This is the mtst configuration file structure used to store the
 * kernel variables which may be temporarily altered by the error injector.
 *
 * XXX	these kernel vars only exist in sun4u and need to removed from
 *	all common and sun4v code.  So maybe the config file is only needed
 *	for the sun4u stuff... except it could be used for the scrub values
 *	and any other things we want to preserve on the system.
 */
typedef struct	config_file {
	int32_t		count;		/* number of running mtst programs */
	boolean_t	saved;		/* true/false flag if kvars valid */
	boolean_t	rw;		/* flag to indicate read or write */
	uint32_t	kv_ce_debug;
	uint32_t	kv_ce_show_data;
	uint32_t	kv_ce_verbose_memory;
	uint32_t	kv_ce_verbose_other;
	uint32_t	kv_ue_debug;
	uint32_t	kv_aft_verbose;
	uint32_t	kv_sfmmu_allow_nc_trans;
} config_file_t;

#define	CFG_FNAME	_PATH_SYSVOL "/mtst.config"

/*
 * This is the minimum size required by the driver for the user allocated
 * data buffer that is used for corruption.
 */
#define	MIN_DATABUF_SIZE	8192

#ifdef	__cplusplus
}
#endif

#endif	/* _MEMTESTIO_H */
