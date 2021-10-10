/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _MEMTESTIO_CH_H
#define	_MEMTESTIO_CH_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * UltraSPARC-III (Cheetah) specific error definitions.
 */

/*
 * Safari bus definitions.
 */
#define	ERR_SAF_SHIFT		(ERR_CPU_SHIFT + 4)
#define	ERR_SAF_INV		UINT64_C(0x0)	/* invalid */
#define	ERR_SAF_NONE		UINT64_C(0x1)	/* nothing */
#define	ERR_SAF_BERR		UINT64_C(0x2)	/* bus error */
#define	ERR_SAF_TO		UINT64_C(0x3)	/* timeout error */
#define	ERR_SAF_MTAG		UINT64_C(0x4)	/* MTAG error */
#define	ERR_SAF_MASK		(0xf)
#define	ERR_SAF(x)		(((x) >> ERR_SAF_SHIFT) & ERR_SAF_MASK)
#define	ERR_SAF_ISBERR(x)	(ERR_SAF(x) == ERR_SAF_BERR)
#define	ERR_SAF_ISTO(x)		(ERR_SAF(x) == ERR_SAF_TO)
#define	ERR_SAF_ISMTAG(x)	(ERR_SAF(x) == ERR_SAF_MTAG)
#define	SNA			(ERR_SAF_NONE	<< ERR_SAF_SHIFT)
#define	SBE			(ERR_SAF_BERR	<< ERR_SAF_SHIFT)
#define	STO			(ERR_SAF_TO	<< ERR_SAF_SHIFT)
#define	SMTAG			(ERR_SAF_MTAG	<< ERR_SAF_SHIFT)

/*
 * System memory errors on prefetch.
 *			 CPU CLASS  SUBCL  TRAP PROT  MODE  ACCESS  MISC | SAF
 */
#define	CH_KD_CEPR	(CH | MEM | DATA | DIS | CE | KERN | PFETC| NA0 | SNA)
#define	CH_KD_UEPR	(CH | MEM | DATA | DIS | UE | KERN | PFETC| NA0 | SNA)

/*
 * System bus mtag tag errors.
 *			 CPU CLASS  SUBCL  TRAP PROT  MODE  ACCESS  MISC | SAF
 */
#define	CH_KD_EMU	(CH | BUS | TAG  | DEF | UE | KERN | LOAD | NA0 | SMTAG)
#define	CH_KD_EMC	(CH | BUS | TAG  | DIS | CE | KERN | LOAD | NA0 | SMTAG)

/*
 * Safari bus errors.
 *			 CPU CLASS  SUBCL  TRAP PROT  MODE  ACCESS  MISC | SAF
 */
#define	CH_KD_TO	(CH | BUS | DATA | DEF | BE  | KERN | LOAD | NA0 | STO)
#define	CH_KD_TOPEEK	(CH | BUS | DATA | DEF | BE  | KERN | LOAD | DDIPEEK \
									| STO)

/*
 * L2$ errors caused by D$/I$ fills.
 *			 CPU  CLASS  SUBCL  TRAP PROT  MODE  ACCESS  MISC
 */
#define	CH_KD_UCU	(CH  | L2  | DATA | PRE | UE | KERN | LOAD | NA0)
#define	CH_KU_UCUCOPYIN	(CH  | L2  | DATA | PRE | UE | KERN | LOAD | CPIN)
#define	CH_KD_UCUTL1	(CH  | L2  | DATA | PRE | UE | KERN | LOAD | TL1)
#define	CH_KD_OUCU	(CH  | L2  | DATA | PRE | UE | KERN | LOAD | ORPH | NI)
#define	CH_KI_UCU	(CH  | L2  | DATA | PRE | UE | KERN | FETC | NA0)
#define	CH_KI_UCUTL1	(CH  | L2  | DATA | PRE | UE | KERN | FETC | TL1)
#define	CH_KI_OUCU	(CH  | L2  | DATA | PRE | UE | KERN | FETC | ORPH)
#define	CH_UD_UCU	(CH  | L2  | DATA | PRE | UE | USER | LOAD | NA0)
#define	CH_UI_UCU	(CH  | L2  | DATA | PRE | UE | USER | FETC | NA0)
#define	CH_OBPD_UCU	(CH  | L2  | DATA | PRE | UE | OBP  | LOAD | NA0)
#define	CH_KD_UCC	(CH  | L2  | DATA | PRE | CE | KERN | LOAD | NA0)
#define	CH_KU_UCCCOPYIN	(CH  | L2  | DATA | PRE | CE | KERN | LOAD | CPIN)
#define	CH_KD_UCCTL1	(CH  | L2  | DATA | PRE | CE | KERN | LOAD | TL1)
#define	CH_KD_OUCC	(CH  | L2  | DATA | PRE | CE | KERN | LOAD | ORPH | NI)
#define	CH_KI_UCC	(CH  | L2  | DATA | PRE | CE | KERN | FETC | NA0)
#define	CH_KI_UCCTL1	(CH  | L2  | DATA | PRE | CE | KERN | FETC | TL1)
#define	CH_KI_OUCC	(CH  | L2  | DATA | PRE | CE | KERN | FETC | ORPH | NI)
#define	CH_UD_UCC	(CH  | L2  | DATA | PRE | CE | USER | LOAD | NA0)
#define	CH_UI_UCC	(CH  | L2  | DATA | PRE | CE | USER | FETC | NA0)
#define	CH_OBPD_UCC	(CH  | L2  | DATA | PRE | CE | OBP  | LOAD | NA0)

/*
 * L2$ block-load and store-merge errors.
 *			 CPU  CLASS  SUBCL  TRAP PROT  MODE  ACCESS  MISC
 */
#define	CH_KD_EDUL	(CH  | L2  | DATA | DEF | UE | KERN | BLD  | NA0)
#define	CH_KD_EDUS	(CH  | L2  | DATA | DIS | UE | KERN | STOR | NA0)
#define	CH_KD_EDUPR	(CH  | L2  | DATA | DIS | UE | KERN | PFETC| NA0)
#define	CH_UD_EDUL	(CH  | L2  | DATA | DEF | UE | USER | BLD  | NA0)
#define	CH_UD_EDUS	(CH  | L2  | DATA | DIS | UE | USER | STOR | NA0)
#define	CH_KD_EDCL	(CH  | L2  | DATA | DIS | CE | KERN | BLD  | NA0)
#define	CH_KD_EDCS	(CH  | L2  | DATA | DIS | CE | KERN | STOR | NA0)
#define	CH_KD_EDCPR	(CH  | L2  | DATA | DIS | CE | KERN | PFETC| NA0)
#define	CH_UD_EDCL	(CH  | L2  | DATA | DIS | CE | USER | BLD  | NA0)
#define	CH_UD_EDCS	(CH  | L2  | DATA | DIS | CE | USER | STOR | NA0)

/*
 * E$ write-back errors.
 *			 CPU  CLASS   SUBCL  TRAP PROT  MODE  ACCESS  MISC
 */
#define	CH_KD_WDU	(CH  | L2WB | DATA | DIS | UE | KERN | LOAD | NA0)
#define	CH_KI_WDU	(CH  | L2WB | DATA | DIS | UE | KERN | FETC | NA0)
#define	CH_UD_WDU	(CH  | L2WB | DATA | DIS | UE | USER | LOAD | NA0)
#define	CH_UI_WDU	(CH  | L2WB | DATA | DIS | UE | USER | FETC | NA0  | NS)
#define	CH_KD_WDC	(CH  | L2WB | DATA | DIS | CE | KERN | LOAD | NA0)
#define	CH_KI_WDC	(CH  | L2WB | DATA | DIS | CE | KERN | FETC | NA0)
#define	CH_UD_WDC	(CH  | L2WB | DATA | DIS | CE | USER | LOAD | NA0)
#define	CH_UI_WDC	(CH  | L2WB | DATA | DIS | CE | USER | FETC | NA0  | NS)

/*
 * L2$ copy-back errors.
 *			 CPU  CLASS   SUBCL  TRAP PROT  MODE  ACCESS  MISC
 */
#define	CH_KD_CPU	(CH  | L2CP | DATA | DIS | UE | KERN | LOAD | NA0)
#define	CH_KI_CPU	(CH  | L2CP | DATA | DIS | UE | KERN | FETC | NA0)
#define	CH_UD_CPU	(CH  | L2CP | DATA | DIS | UE | USER | LOAD | NA0)
#define	CH_UI_CPU	(CH  | L2CP | DATA | DIS | UE | USER | FETC | NA0  | NS)
#define	CH_KD_CPC	(CH  | L2CP | DATA | DIS | CE | KERN | LOAD | NA0)
#define	CH_KI_CPC	(CH  | L2CP | DATA | DIS | CE | KERN | FETC | NA0)
#define	CH_UD_CPC	(CH  | L2CP | DATA | DIS | CE | USER | LOAD | NA0)
#define	CH_UI_CPC	(CH  | L2CP | DATA | DIS | CE | USER | FETC | NA0  | NS)

#ifdef	__cplusplus
}
#endif

#endif	/* _MEMTESTIO_CH_H */
