/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _MEMTESTIO_V_H
#define	_MEMTESTIO_V_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The following definitions are used to encode all the supported sun4v errors
 * that can be injected.  Errors are encoded using the following fields:
 *
 *	CPU | CLASS | SUBCLASS | TRAP | PROT | MODE | ACCESS | MISC
 */

/*
 * The cpu field defines the type of cpus on which the command is supported.
 *
 * These are continued from the definitions in the common memtestio.h file.
 */
#define	ERR_CPU_NI1		(ERR_CPU_GENMAX + 1)	/* Niagara-I */
#define	ERR_CPU_NI2		(ERR_CPU_GENMAX + 2)	/* Niagara-II */
#define	ERR_CPU_VF		(ERR_CPU_GENMAX + 3)	/* Victoria Falls */
#define	ERR_CPU_KT		(ERR_CPU_GENMAX + 4)	/* Rainbow Falls */

/*
 * The cpu macros look in the system info structure to determine the cpu type.
 */
#define	CPU_MASK_BIT3(ver)	((CPU_MASK(ver) >> 3) & 0x1)
#define	NIAGARA_IMPL		0x0023
#define	CPU_ISNIAGARA(cip)	(CPU_IMPL((cip)->c_cpuver) == NIAGARA_IMPL)
#define	NIAGARA2_IMPL		0x0024
#define	CPU_ISNIAGARA2(cip)	((CPU_IMPL((cip)->c_cpuver) == \
				    NIAGARA2_IMPL) && \
				    (CPU_MASK_BIT3((cip)->c_cpuver) != 1))
#define	CPU_ISVFALLS(cip)	((CPU_IMPL((cip)->c_cpuver) == \
				    NIAGARA2_IMPL) && \
				    (CPU_MASK_BIT3((cip)->c_cpuver) == 1))
#define	KT_IMPL			0x0028
#define	RFALLS_IMPL		KT_IMPL
#define	CPU_ISKT(cip)		(CPU_IMPL((cip)->c_cpuver) == KT_IMPL)
#define	CPU_ISRF(cip)		CPU_ISKT(cip)

/*
 * The class field splits up all errors into basic error classes.
 *
 * These are continued from the definitions in the common memtestio.h file.
 */
#define	ERR_CLASS_STB		(ERR_CLASS_GENMAX + 1) /* store buffer errs */
#define	ERR_CLASS_MCU		(ERR_CLASS_GENMAX + 2) /* SOC MCU errs */
#define	ERR_CLASS_LFU		(ERR_CLASS_GENMAX + 3) /* LFU errs */
#define	ERR_CLASS_NCX		(ERR_CLASS_GENMAX + 4) /* NCX errs */
#define	ERR_CLASS_CL		(ERR_CLASS_GENMAX + 5) /* coherency link errs */

#define	ERR_CLASS_ISSTB(x)	(ERR_CLASS(x) == ERR_CLASS_STB)
#define	ERR_CLASS_ISMCU(x)	(ERR_CLASS(x) == ERR_CLASS_MCU)
#define	ERR_CLASS_ISLFU(x)	(ERR_CLASS(x) == ERR_CLASS_LFU)
#define	ERR_CLASS_ISNCX(x)	(ERR_CLASS(x) == ERR_CLASS_NCX)
#define	ERR_CLASS_ISCL(x)	(ERR_CLASS(x) == ERR_CLASS_CL)

/*
 * The sub-class field further defines the error within its class.
 *
 * These are continued from the definitions in the common memtestio.h file.
 */
#define	ERR_SUBCL_VD		(ERR_SUBCL_GENMAX + 1) /* cache VD errs */
#define	ERR_SUBCL_UA		(ERR_SUBCL_GENMAX + 2) /* cache UA errs */
#define	ERR_SUBCL_DIRT		(ERR_SUBCL_GENMAX + 3) /* cache dirty errs */
#define	ERR_SUBCL_SHRD		(ERR_SUBCL_GENMAX + 4) /* cache shared errs */
#define	ERR_SUBCL_DIR		(ERR_SUBCL_GENMAX + 5) /* cache dir errs */
#define	ERR_SUBCL_FBUF		(ERR_SUBCL_GENMAX + 6) /* cache fill buf errs */
#define	ERR_SUBCL_MBUF		(ERR_SUBCL_GENMAX + 7) /* cache miss buf errs */
#define	ERR_SUBCL_WBUF		(ERR_SUBCL_GENMAX + 8) /* cache write buf */
#define	ERR_SUBCL_MA		(ERR_SUBCL_GENMAX + 9) /* mod arith errs */
#define	ERR_SUBCL_CWQ		(ERR_SUBCL_GENMAX + 10) /* cwq errs */
#define	ERR_SUBCL_MMU		(ERR_SUBCL_GENMAX + 11) /* tlb mmu errs */
#define	ERR_SUBCL_SCR		(ERR_SUBCL_GENMAX + 12) /* scratchpad errs */
#define	ERR_SUBCL_TCA		(ERR_SUBCL_GENMAX + 13) /* tick compare errs */
#define	ERR_SUBCL_TSA		(ERR_SUBCL_GENMAX + 14) /* trap stack errs */

#define	ERR_SUBCLASS_ISVD(x)	(ERR_SUBCLASS(x) == ERR_SUBCL_VD)
#define	ERR_SUBCLASS_ISUA(x)	(ERR_SUBCLASS(x) == ERR_SUBCL_UA)
#define	ERR_SUBCLASS_ISDIRT(x)	(ERR_SUBCLASS(x) == ERR_SUBCL_DIRT)
#define	ERR_SUBCLASS_ISSHRD(x)	(ERR_SUBCLASS(x) == ERR_SUBCL_SHRD)
#define	ERR_SUBCLASS_ISDIR(x)	(ERR_SUBCLASS(x) == ERR_SUBCL_DIR)
#define	ERR_SUBCLASS_ISFBUF(x)	(ERR_SUBCLASS(x) == ERR_SUBCL_FBUF)
#define	ERR_SUBCLASS_ISMBUF(x)	(ERR_SUBCLASS(x) == ERR_SUBCL_MBUF)
#define	ERR_SUBCLASS_ISWBUF(x)	(ERR_SUBCLASS(x) == ERR_SUBCL_WBUF)
#define	ERR_SUBCLASS_ISMA(x)	(ERR_SUBCLASS(x) == ERR_SUBCL_MA)
#define	ERR_SUBCLASS_ISCWQ(x)	(ERR_SUBCLASS(x) == ERR_SUBCL_CWQ)
#define	ERR_SUBCLASS_ISMMU(x)	(ERR_SUBCLASS(x) == ERR_SUBCL_MMU)
#define	ERR_SUBCLASS_ISSCR(x)	(ERR_SUBCLASS(x) == ERR_SUBCL_SCR)
#define	ERR_SUBCLASS_ISTCA(x)	(ERR_SUBCLASS(x) == ERR_SUBCL_TCA)
#define	ERR_SUBCLASS_ISTSA(x)	(ERR_SUBCLASS(x) == ERR_SUBCL_TSA)

/*
 * The trap field defines the type of trap that is generated by the error.
 *
 * These are fully defined for sun4v in the common memtestio.h file.
 */


/*
 * The protection field defines the type of protection that is triggered
 * by the error.
 *
 * These are continued from the definitions in the common memtestio.h file.
 */
#define	ERR_PROT_ND		(ERR_PROT_GENMAX + 1) /* NotData errs */
#define	ERR_PROT_VAL		(ERR_PROT_GENMAX + 2) /* valid-bit errs */
#define	ERR_PROT_APE		(ERR_PROT_GENMAX + 3) /* address parity errs */

#define	ERR_PROT_ISND(x)	(ERR_PROT(x) == ERR_PROT_ND)
#define	ERR_PROT_ISVAL(x)	(ERR_PROT(x) == ERR_PROT_VAL)
#define	ERR_PROT_ISAPE(x)	(ERR_PROT(x) == ERR_PROT_APE)

/*
 * The mode field defines what is accessing the data and how.
 *
 * These are fully defined for sun4v in the common memtestio.h file.
 */


/*
 * The access field defines the type of access that generates the error.
 *
 * These are continued from the definitions in the common memtestio.h file.
 */
#define	ERR_ACC_OP		(ERR_ACC_GENMAX + 1) /* instruction operation */
#define	ERR_ACC_MAL		(ERR_ACC_GENMAX + 2) /* mod arithmetic load */
#define	ERR_ACC_MAS		(ERR_ACC_GENMAX + 3) /* mod arithmetic store */
#define	ERR_ACC_CWQ		(ERR_ACC_GENMAX + 4) /* CWQ crypto acc */
#define	ERR_ACC_DTLB		(ERR_ACC_GENMAX + 5) /* dTLB fill acc */
#define	ERR_ACC_ITLB		(ERR_ACC_GENMAX + 6) /* iTLB fill acc */
#define	ERR_ACC_PRICE		(ERR_ACC_GENMAX + 7) /* prefetch ICE acc */

#define	ERR_ACC_ISOP(x)		(ERR_ACC(x) == ERR_ACC_OP)
#define	ERR_ACC_ISMAL(x)	(ERR_ACC(x) == ERR_ACC_MAL)
#define	ERR_ACC_ISMAS(x)	(ERR_ACC(x) == ERR_ACC_MAS)
#define	ERR_ACC_ISCWQ(x)	(ERR_ACC(x) == ERR_ACC_CWQ)
#define	ERR_ACC_ISDTLB(x)	(ERR_ACC(x) == ERR_ACC_DTLB)
#define	ERR_ACC_ISITLB(x)	(ERR_ACC(x) == ERR_ACC_ITLB)
#define	ERR_ACC_ISPRICE(x)	(ERR_ACC(x) == ERR_ACC_PRICE)

/*
 * The miscellaneous field helps to further encode commands that
 * could not be encoded with just the other fields.  Note that the MISC
 * field is a bit field unlike the other command fields.
 *
 * These are fully defined for sun4v in the common memtestio.h file.
 */


/*
 * Short definitions to help make the command encodings more readable.
 */
#define	NI1	(ERR_CPU_NI1	 << ERR_CPU_SHIFT)
#define	NI2	(ERR_CPU_NI2	 << ERR_CPU_SHIFT)
#define	VF	(ERR_CPU_VF	 << ERR_CPU_SHIFT)
#define	KT	(ERR_CPU_KT	 << ERR_CPU_SHIFT)
#define	RF	(ERR_CPU_KT	 << ERR_CPU_SHIFT)

#define	STB	(ERR_CLASS_STB	 << ERR_CLASS_SHIFT)
#define	MCU	(ERR_CLASS_MCU	 << ERR_CLASS_SHIFT)
#define	LFU	(ERR_CLASS_LFU	 << ERR_CLASS_SHIFT)
#define	NCX	(ERR_CLASS_NCX   << ERR_CLASS_SHIFT)
#define	CL	(ERR_CLASS_CL	 << ERR_CLASS_SHIFT)

#define	VD	(ERR_SUBCL_VD	 << ERR_SUBCL_SHIFT)
#define	UA	(ERR_SUBCL_UA	 << ERR_SUBCL_SHIFT)
#define	DIRT	(ERR_SUBCL_DIRT	 << ERR_SUBCL_SHIFT)
#define	SHRD	(ERR_SUBCL_SHRD	 << ERR_SUBCL_SHIFT)
#define	DIR	(ERR_SUBCL_DIR	 << ERR_SUBCL_SHIFT)
#define	FBUF	(ERR_SUBCL_FBUF	 << ERR_SUBCL_SHIFT)
#define	MBUF	(ERR_SUBCL_MBUF	 << ERR_SUBCL_SHIFT)
#define	WBUF	(ERR_SUBCL_WBUF	 << ERR_SUBCL_SHIFT)
#define	MA	(ERR_SUBCL_MA	 << ERR_SUBCL_SHIFT)
#define	CWQ	(ERR_SUBCL_CWQ	 << ERR_SUBCL_SHIFT)
#define	MMU	(ERR_SUBCL_MMU	 << ERR_SUBCL_SHIFT)
#define	SCR	(ERR_SUBCL_SCR	 << ERR_SUBCL_SHIFT)
#define	TCA	(ERR_SUBCL_TCA	 << ERR_SUBCL_SHIFT)
#define	TSA	(ERR_SUBCL_TSA	 << ERR_SUBCL_SHIFT)

#define	ND	(ERR_PROT_ND	 << ERR_PROT_SHIFT)
#define	VAL	(ERR_PROT_VAL	 << ERR_PROT_SHIFT)
#define	APE	(ERR_PROT_APE	 << ERR_PROT_SHIFT)

#define	OP	(ERR_ACC_OP	 << ERR_ACC_SHIFT)
#define	MAL	(ERR_ACC_MAL	 << ERR_ACC_SHIFT)
#define	MAS	(ERR_ACC_MAS	 << ERR_ACC_SHIFT)
#define	CWQAC	(ERR_ACC_CWQ	 << ERR_ACC_SHIFT)
#define	DTAC	(ERR_ACC_DTLB	 << ERR_ACC_SHIFT)
#define	ITAC	(ERR_ACC_ITLB	 << ERR_ACC_SHIFT)
#define	PRICE	(ERR_ACC_PRICE	 << ERR_ACC_SHIFT)

/*
 * Sun4v generic error command definitions (for sun4v_generic_cmds array).
 */

/*
 * Memory (DRAM) errors injected by address and utility functions.
 * Note that the utility commands should have a MODE of NA2 so that the
 * pre-test initialization does not prepare for an actual injection.
 *			 CPU  CLASS   SUBCL  TRAP  PROT  MODE  ACCESS  MISC
 */
#define	G4V_ENABLE	(GEN | UTIL | NA5  | NA4 | NA3 | NA2  | NA1  | ENB)
#define	G4V_FLUSH	(GEN | UTIL | NA5  | NA4 | NA3 | NA2  | NA1  | FLSH)

#define	G4V_UMVIRT	(GEN | MEM  | DATA | NA4 | NA3 | USER | NA1  | VIRT)
#define	G4V_KMVIRT	(GEN | MEM  | DATA | NA4 | NA3 | NA2  | NA1  | VIRT)
#define	G4V_MREAL	(GEN | MEM  | DATA | NA4 | NA3 | NA2  | NA1  | REAL)
#define	G4V_MPHYS	(GEN | MEM  | DATA | NA4 | NA3 | NA2  | NA1  | PHYS)

#define	G4V_KVPEEK	(GEN | UTIL | NA5  | NA4 | NA3 | NA2  | LOAD | PEEK | \
									VIRT)
#define	G4V_KVPOKE	(GEN | UTIL | NA5  | NA4 | NA3 | NA2  | STOR | POKE | \
									VIRT)
#define	G4V_KMPEEK	(GEN | UTIL | NA5  | NA4 | NA3 | NA2  | LOAD | PEEK | \
									REAL)
#define	G4V_KMPOKE	(GEN | UTIL | NA5  | NA4 | NA3 | NA2  | STOR | POKE | \
									REAL)
#define	G4V_HVPEEK	(GEN | UTIL | NA5  | NA4 | NA3 | NA2  | LOAD | PEEK | \
									PHYS)
#define	G4V_HVPOKE	(GEN | UTIL | NA5  | NA4 | NA3 | NA2  | STOR | POKE | \
									PHYS)
#define	G4V_ASIPEEK	(GEN | UTIL | NA5  | NA4 | NA3 | NA2  | ASI  | PEEK)
#define	G4V_ASIPOKE	(GEN | UTIL | NA5  | NA4 | NA3 | NA2  | ASI  | POKE)
#define	G4V_SPFAIL	(GEN | UTIL | NA5  | DIS | NA3 | NA2  | SP   | NA0)

#ifdef	__cplusplus
}
#endif

#endif	/* _MEMTESTIO_V_H */
