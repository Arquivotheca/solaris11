/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _MEMTESTIO_OC_H
#define	_MEMTESTIO_OC_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * SPARC64-VI (Olympus C) specific error definitions.
 */

#define	ERR_OC_SHIFT		(ERR_CPU_SHIFT + 4)
#define	ERR_OC_INV		UINT64_C(0x0)	/* invalid */

/* MAC-detected mirror mode PTRL */
#define	ERR_OC_MMP		UINT64_C(0x1)	/* 1-sided */
#define	ERR_OC_MMP2		UINT64_C(0x2)	/* 2-sided */
#define	MMP			(ERR_OC_MMP << ERR_OC_SHIFT)
#define	MMP2			(ERR_OC_MMP2 << ERR_OC_SHIFT)
#define	ERR_OC_MASK		(0xf)
#define	ERR_OC(x)		(((x) >> ERR_OC_SHIFT) & ERR_OC_MASK)
#define	ERR_OC_ISMMP(x)		(ERR_OC(x) == ERR_OC_MMP)
#define	ERR_OC_ISMMP2(x)	(ERR_OC(x) == ERR_OC_MMP2)

/*
 * ICE and CMPE
 * Must not exceed ERR_MISC_NOTIMP.
 */
#define	ERR_MISC_ICE		(ERR_MISC_GENMAX << 1)
#define	ERR_MISC_ISICE(x)	(ERR_MISC(x) & ERR_MISC_ICE)
#define	ICE			(ERR_MISC_ICE << ERR_MISC_SHIFT)
#define	ERR_MISC_CMPE		(ERR_MISC_GENMAX << 2)
#define	ERR_MISC_ISCMPE(x)	(ERR_MISC(x) & ERR_MISC_CMPE)
#define	CMPE			(ERR_MISC_CMPE << ERR_MISC_SHIFT)

/*
 * CPU-detected or MAC-detected errors.
 * Use existing generic sun4u errors:
 *	G4U_KD_UE, G4U_KI_UE, G4U_UD_UE, G4U_UI_UE, G4U_MPHYS, G4U_KMPEEK,
 *	G4U_KMPOKE.
 */


/*
 * CPU-detected only errors.
 *			 CPU CLASS  SUBCL  TRAP PROT  MODE  ACCESS  MISC
 */
#define	OC_KD_UETL1	(OC | MEM  | DATA | DEF | UE | KERN | LOAD | TL1)
#define	OC_KI_UETL1	(OC | MEM  | DATA | DEF | UE | KERN | FETC | TL1)
#define	OC_L1DUE	(OC | DC   | DATA | DEF | UE | KERN | LOAD | NA0)
#define	OC_L2UE		(OC | L2   | DATA | DEF | UE | KERN | LOAD | NA0)
#define	OC_L1DUETL1	(OC | DC   | DATA | DEF | UE | KERN | LOAD | TL1)
#define	OC_L2UETL1	(OC | L2   | DATA | DEF | UE | KERN | LOAD | TL1)
#define	OC_KD_MTLB	(OC | DTLB | DATA | DEF | UE | KERN | LOAD | NA0)
#define	OC_KI_MTLB	(OC | ITLB | DATA | DEF | UE | KERN | FETC | NA0)
#define	OC_UD_MTLB	(OC | DTLB | DATA | DEF | UE | USER | LOAD | NA0)
#define	OC_UI_MTLB	(OC | ITLB | DATA | DEF | UE | USER | FETC | NA0)
#define	OC_IUGR		(OC | INT  | IREG | DEF | PE | KERN | LOAD | NA0)

/*
 * MAC-detected errors, normal mode
 *			CPU CLASS  SUBCL  TRAP PROT  MODE  ACCESS  MISC
 */
#define	OC_ICE		(OC | MEM | DATA | NA4 | CE  | KERN | LOAD | ICE)
#define	OC_PCE		(OC | MEM | DATA | NA4 | CE  | KERN | LOAD | NA0)
#define	OC_PUE		(OC | MEM | DATA | DEF | UE  | KERN | LOAD | NA0 | MMP)

/*
 * MAC-detected errors, mirror mode
 *			 CPU CLASS   SUBCL  TRAP PROT  MODE  ACCESS  MISC
 */
#define	OC_KD_CMPE	(OC | MEM | DATA | DEF | UE | KERN | LOAD | CMPE)
#define	OC_KI_CMPE	(OC | MEM | DATA | DEF | UE | KERN | FETC | CMPE)
#define	OC_UD_CMPE	(OC | MEM | DATA | DEF | UE | USER | LOAD | CMPE)
#define	OC_UI_CMPE	(OC | MEM | DATA | DEF | UE | USER | FETC | CMPE)
#define	OC_CMPE		(OC | MEM | DATA | DEF | UE | KERN | LOAD | CMPE | MMP)
#define	OC_MUE		(OC | MEM | DATA | DEF | UE | KERN | LOAD | NA0  | MMP2)

#ifdef	__cplusplus
}
#endif

#endif	/* _MEMTESTIO_OC_H */
