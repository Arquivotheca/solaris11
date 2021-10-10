/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _MEMTESTIO_JA_H
#define	_MEMTESTIO_JA_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * UltraSPARC-IIIi (Jalapeno) specific error definitions.
 */

/*
 * JBus definitions.
 */
#define	ERR_JBUS_SHIFT		(ERR_CPU_SHIFT + 4)
#define	ERR_JBUS_INV		UINT64_C(0x0)	/* invalid */
#define	ERR_JBUS_NONE		UINT64_C(0x1)	/* nothing */
#define	ERR_JBUS_BERR		UINT64_C(0x2)	/* bus error response */
#define	ERR_JBUS_TO		UINT64_C(0x3)	/* unmapped error response */
#define	ERR_JBUS_UMS		UINT64_C(0x4)	/* unmapped store error */
#define	ERR_JBUS_OM		UINT64_C(0x5)	/* out of range memory error */
#define	ERR_JBUS_JETO		UINT64_C(0x6)	/* hardware timeout error */
#define	ERR_JBUS_SCE		UINT64_C(0x7)	/* control parity error */
#define	ERR_JBUS_JEIC		UINT64_C(0x8)	/* protocol error */
#define	ERR_JBUS_FR		UINT64_C(0x9)	/* foreign/remote error */
#define	ERR_JBUS_MASK		(0xf)
#define	ERR_JBUS(x)		(((x) >> ERR_JBUS_SHIFT) & ERR_JBUS_MASK)
#define	ERR_JBUS_ISBERR(x)	(ERR_JBUS(x) == ERR_JBUS_BERR)
#define	ERR_JBUS_ISTO(x)	(ERR_JBUS(x) == ERR_JBUS_TO)
#define	ERR_JBUS_ISUMS(x)	(ERR_JBUS(x) == ERR_JBUS_UMS)
#define	ERR_JBUS_ISOM(x)	(ERR_JBUS(x) == ERR_JBUS_OM)
#define	ERR_JBUS_ISJETO(x)	(ERR_JBUS(x) == ERR_JBUS_JETO)
#define	ERR_JBUS_ISSCE(x)	(ERR_JBUS(x) == ERR_JBUS_SCE)
#define	ERR_JBUS_ISJEIC(x)	(ERR_JBUS(x) == ERR_JBUS_JEIC)
#define	ERR_JBUS_ISFR(x)	(ERR_JBUS(x) == ERR_JBUS_FR)
#define	JNA			(ERR_JBUS_NONE  << ERR_JBUS_SHIFT)
#define	JBE			(ERR_JBUS_BERR	<< ERR_JBUS_SHIFT)
#define	JTO			(ERR_JBUS_TO	<< ERR_JBUS_SHIFT)
#define	JUMS			(ERR_JBUS_UMS	<< ERR_JBUS_SHIFT)
#define	JOM			(ERR_JBUS_OM	<< ERR_JBUS_SHIFT)
#define	JETO			(ERR_JBUS_JETO	<< ERR_JBUS_SHIFT)
#define	JEIC			(ERR_JBUS_JEIC	<< ERR_JBUS_SHIFT)
#define	JSCE			(ERR_JBUS_SCE	<< ERR_JBUS_SHIFT)
#define	JFR			(ERR_JBUS_FR	<< ERR_JBUS_SHIFT)

/*
 * Foreign/Remote memory errors.
 *			 CPU  CLASS  SUBCL  TRAP PROT  MODE  ACCESS  MISC  JBUS
 */
#define	JA_KD_FRUE	(JA | MEM | DATA | DEF | UE  | KERN | LOAD | NA0 | JFR)
#define	JA_KI_FRUE	(JA | MEM | DATA | DEF | UE  | KERN | FETC | NA0 | JFR)
#define	JA_UD_FRUE	(JA | MEM | DATA | DEF | UE  | USER | LOAD | NA0 | JFR)
#define	JA_UI_FRUE	(JA | MEM | DATA | DEF | UE  | USER | FETC | NA0 | JFR)
#define	JA_KD_FRCE	(JA | MEM | DATA | DIS | CE  | KERN | LOAD | NA0 | JFR)
#define	JA_KD_FRCETL1	(JA | MEM | DATA | DIS | CE  | KERN | LOAD | TL1 | JFR)
#define	JA_KD_FRCESTORM	(JA | MEM | DATA | DIS | CE  | KERN | LOAD | STORM|JFR)
#define	JA_KI_FRCE	(JA | MEM | DATA | DIS | CE  | KERN | FETC | NA0 | JFR)
#define	JA_KI_FRCETL1	(JA | MEM | DATA | DIS | CE  | KERN | FETC | TL1 | JFR)
#define	JA_UD_FRCE	(JA | MEM | DATA | DIS | CE  | USER | LOAD | NA0 | JFR)
#define	JA_UI_FRCE	(JA | MEM | DATA | DIS | CE  | USER | FETC | NA0 | JFR)

/*
 * JBus errors.
 *			 CPU  CLASS  SUBCL  TRAP PROT  MODE  ACCESS  MISC  JBUS
 */
#define	JA_KD_BE	(JA | BUS | DATA | DEF | BE  | KERN | LOAD | NA0 | JBE)
#define	JA_KD_BEPEEK	(JA | BUS | DATA | DEF | BE  | KERN | LOAD | DDIPEEK \
									| JBE)
#define	JA_KD_BEPR	(JA | BUS | DATA | DEF | BE  | KERN | PFETC| NA0 | JBE)
#define	JA_KD_TO	(JA | BUS | DATA | DEF | BE  | KERN | LOAD | NA0 | JTO)
#define	JA_KD_TOPR	(JA | BUS | DATA | DEF | BE  | KERN | PFETC| NA0 | JTO)
#define	JA_KD_BP	(JA | BUS | DATA | DEF | PE  | KERN | LOAD | NI  | JNA)
#define	JA_KD_WBP	(JA | BUS | DATA | DIS | PE  | KERN | NA1  | NI  | JNA)
#define	JA_KD_ISAP	(JA | BUS | ADDR | FAT | PE  | KERN | LOAD | NA0 | JNA)
#define	JA_KD_OM	(JA | BUS | DATA | DEF | BE  | KERN | LOAD | NA0 | JOM)
#define	JA_KD_UMS	(JA | BUS | DATA | DIS | BE  | KERN | STOR | NA0 | JUMS)
#define	JA_KD_JETO	(JA | BUS | DATA | FAT | BE  | KERN | LOAD | NI  | JETO)
#define	JA_KD_SCE	(JA | BUS | DATA | FAT | BE  | KERN | LOAD | NI  | JSCE)
#define	JA_KD_JEIC	(JA | BUS | DATA | FAT | BE  | KERN | LOAD | NA0 | JEIC)

/*
 * E$ tag errors.
 *			 CPU  CLASS  SUBCL  TRAP PROT  MODE  ACCESS  MISC  JBUS
 */
#define	JA_KD_ETP	(JA  | L2  | TAG  | DEF | PE | KERN | LOAD | NA0 | JNA)

#ifdef	__cplusplus
}
#endif

#endif	/* _MEMTESTIO_JA_H */
