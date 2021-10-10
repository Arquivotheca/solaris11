/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_MEMTESTIO_PN_H
#define	_MEMTESTIO_PN_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * UltraSPARC-IV+ (Panther) specific error definitions.
 */

/*
 * L3$ errors, note that most L2$ errors use defs from US-III* files.
 *			 CPU  CLASS  SUBCL  TRAP PROT  MODE  ACCESS  MISC
 */
#define	PN_KD_L3UCU	(PN  | L3  | DATA | PRE | UE | KERN | LOAD | NA0)
#define	PN_KU_L3UCUCOPYIN	\
	(PN  | L3  | DATA | PRE | UE | KERN | LOAD | CPIN)
#define	PN_KD_L3UCUTL1	(PN  | L3  | DATA | PRE | UE | KERN | LOAD | TL1)
#define	PN_KD_L3OUCU	\
	(PN  | L3  | DATA | PRE | UE | KERN | LOAD | ORPH | NI)
#define	PN_KI_L3UCU	(PN  | L3  | DATA | PRE | UE | KERN | FETC | NA0)
#define	PN_KI_L3UCUTL1	(PN  | L3  | DATA | PRE | UE | KERN | FETC | TL1)
#define	PN_KI_L3OUCU	(PN  | L3  | DATA | PRE | UE | KERN | FETC | ORPH)
#define	PN_UD_L3UCU	(PN  | L3  | DATA | PRE | UE | USER | LOAD | NA0)
#define	PN_UI_L3UCU	(PN  | L3  | DATA | PRE | UE | USER | FETC | NA0)
#define	PN_OBPD_L3UCU	(PN  | L3  | DATA | PRE | UE | OBP  | LOAD | NA0)
#define	PN_KD_L3UCC	(PN  | L3  | DATA | PRE | CE | KERN | LOAD | NA0)
#define	PN_KU_L3UCCCOPYIN	\
	(PN  | L3  | DATA | PRE | CE | KERN | LOAD | CPIN)
#define	PN_KD_L3UCCTL1	(PN  | L3  | DATA | PRE | CE | KERN | LOAD | TL1)
#define	PN_KD_L3OUCC	\
	(PN  | L3  | DATA | PRE | CE | KERN | LOAD | ORPH | NI)
#define	PN_KI_L3UCC	(PN  | L3  | DATA | PRE | CE | KERN | FETC | NA0)
#define	PN_KI_L3UCCTL1	(PN  | L3  | DATA | PRE | CE | KERN | FETC | TL1)
#define	PN_KI_L3OUCC	\
	(PN  | L3  | DATA | PRE | CE | KERN | FETC | ORPH | NI)
#define	PN_UD_L3UCC	(PN  | L3  | DATA | PRE | CE | USER | LOAD | NA0)
#define	PN_UI_L3UCC	(PN  | L3  | DATA | PRE | CE | USER | FETC | NA0)
#define	PN_OBPD_L3UCC	(PN  | L3  | DATA | PRE | CE | OBP  | LOAD | NA0)

/*
 * L3$ block-load and store-merge errors.
 *			 CPU  CLASS  SUBCL  TRAP PROT  MODE  ACCESS  MISC
 */
#define	PN_KD_L3EDUL	(PN  | L3  | DATA | DEF | UE | KERN | BLD  | NA0)
#define	PN_KD_L3EDUS	(PN  | L3  | DATA | DIS | UE | KERN | STOR | NA0)
#define	PN_KD_L3EDUPR	(PN  | L3  | DATA | DIS | UE | KERN | PFETC| NA0)
#define	PN_UD_L3EDUL	(PN  | L3  | DATA | DEF | UE | USER | BLD  | NA0)
#define	PN_UD_L3EDUS	(PN  | L3  | DATA | DIS | UE | USER | STOR | NA0)
#define	PN_KD_L3EDCL	(PN  | L3  | DATA | DIS | CE | KERN | BLD  | NA0)
#define	PN_KD_L3EDCS	(PN  | L3  | DATA | DIS | CE | KERN | STOR | NA0)
#define	PN_KD_L3EDCPR	(PN  | L3  | DATA | DIS | CE | KERN | PFETC| NA0)
#define	PN_UD_L3EDCL	(PN  | L3  | DATA | DIS | CE | USER | BLD  | NA0)
#define	PN_UD_L3EDCS	(PN  | L3  | DATA | DIS | CE | USER | STOR | NA0)

/*
 * L3$ write-back errors.
 *			 CPU   CLASS  SUBCL  TRAP PROT  MODE  ACCESS  MISC
 */
#define	PN_KD_L3WDU	(PN  | L3WB | DATA | DIS | UE | KERN | LOAD | NA0)
#define	PN_KI_L3WDU	(PN  | L3WB | DATA | DIS | UE | KERN | FETC | NA0)
#define	PN_UD_L3WDU	(PN  | L3WB | DATA | DIS | UE | USER | LOAD | NA0)
#define	PN_UI_L3WDU	\
	(PN  | L3WB | DATA | DIS | UE | USER | FETC | NA0  | NS)
#define	PN_KD_L3WDC	(PN  | L3WB | DATA | DIS | CE | KERN | LOAD | NA0)
#define	PN_KI_L3WDC	(PN  | L3WB | DATA | DIS | CE | KERN | FETC | NA0)
#define	PN_UD_L3WDC	(PN  | L3WB | DATA | DIS | CE | USER | LOAD | NA0)
#define	PN_UI_L3WDC	\
	(PN  | L3WB | DATA | DIS | CE | USER | FETC | NA0  | NS)

/*
 * L3$ copy-back errors.
 *			 CPU   CLASS  SUBCL  TRAP PROT  MODE  ACCESS  MISC
 */
#define	PN_KD_L3CPU	(PN  | L3CP | DATA | DIS | UE | KERN | LOAD | NA0)
#define	PN_KI_L3CPU	(PN  | L3CP | DATA | DIS | UE | KERN | FETC | NA0)
#define	PN_UD_L3CPU	(PN  | L3CP | DATA | DIS | UE | USER | LOAD | NA0)
#define	PN_UI_L3CPU	\
	(PN  | L3CP | DATA | DIS | UE | USER | FETC | NA0  | NS)
#define	PN_KD_L3CPC	(PN  | L3CP | DATA | DIS | CE | KERN | LOAD | NA0)
#define	PN_KI_L3CPC	(PN  | L3CP | DATA | DIS | CE | KERN | FETC | NA0)
#define	PN_UD_L3CPC	(PN  | L3CP | DATA | DIS | CE | USER | LOAD | NA0)
#define	PN_UI_L3CPC	\
	(PN  | L3CP | DATA | DIS | CE | USER | FETC | NA0  | NS)

/*
 * L3$ tag errors.
 * Note: Panther TSxE commands like TSCE, TSUE are covered by THxE cases.
 *			 CPU CLASS  SUBCL  TRAP PROT  MODE  ACCESS  MISC
 */
#define	PN_KD_L3ETHCE	(PN | L3  | TAG  | DIS | CE | KERN | BLD  | NA0)
#define	PN_KI_L3ETHCE	(PN | L3  | TAG  | DIS | CE | KERN | FETC | NA0)
#define	PN_UD_L3ETHCE	(PN | L3  | TAG  | DIS | CE | USER | BLD  | NA0)
#define	PN_UI_L3ETHCE	(PN | L3  | TAG  | DIS | CE | USER | FETC | NA0)
#define	PN_KD_L3ETHUE	(PN | L3  | TAG  | DIS | UE | KERN | BLD  | NA0)
#define	PN_KI_L3ETHUE	(PN | L3  | TAG  | DIS | UE | KERN | FETC | NA0)
#define	PN_UD_L3ETHUE	(PN | L3  | TAG  | DIS | UE | USER | LOAD | NA0)
#define	PN_UI_L3ETHUE	(PN | L3  | TAG  | DIS | UE | USER | FETC | NA0)

/*
 * L2/L3$ data and tag sticky errors.
 *			  CPU  CLASS SUBCL  TRAP PROT  MODE  ACCESS MISC
 */
#define	PN_KD_THCE_STKY   (PN | L2 | TAG  | DIS | CE | KERN | BLD | STKY)
#define	PN_KD_L3THCE_STKY (PN | L3 | TAG  | DIS | CE | KERN | BLD | STKY)

#define	PN_KD_EDC_STKY    (PN | L2 | DATA | DIS | CE | KERN | BLD | STKY)
#define	PN_KD_L3EDC_STKY  (PN | L3 | DATA | DIS | CE | KERN | BLD | STKY)

/*
 * L3$ errors injected at address/offset/index.
 * Note that the L2 "phys" commands use the generic command defs.
 *			 CPU CLASS  SUBCL  TRAP  PROT  MODE  ACCESS  MISC
 */
#define	PN_L3PHYS	(PN | L3  | DATA | NA4 | NA3 | NA2  | NA1 | PHYS)
#define	PN_L3TPHYS	(PN | L3  | TAG  | NA4 | NA3 | NA2  | NA1 | PHYS)

/*
 * L2/L3$ Internal Processor (IERR) errors.
 *			 CPU CLASS  SUBCL TRAP  PROT  MODE  ACCESS  MISC
 */
#define	PN_L2_MH	(PN | INT | MH  | FAT | NA3 | KERN | LOAD | NA0)
#define	PN_L3_MH	(PN | INT | MH  | FAT | BE  | KERN | LOAD | NA0)
#define	PN_L2_ILLSTATE	(PN | INT | TAG | FAT | NA3 | KERN | LOAD | NA0)
#define	PN_L3_ILLSTATE	(PN | INT | TAG | FAT | BE  | KERN | LOAD | NA0)

/*
 * I-D TLB errors.
 *			 CPU  CLASS  SUBCL  TRAP PROT  MODE  ACCESS  MISC
 */
#define	PN_KD_TLB	(PN | DTLB | DATA | DEF | PE | KERN | LOAD | NA0)
#define	PN_KD_TLBTL1	(PN | DTLB | DATA | DEF | PE | KERN | LOAD | TL1)
#define	PN_KI_TLB	(PN | ITLB | DATA | DEF | PE | KERN | FETC | NA0)
#define	PN_UD_TLB	(PN | DTLB | DATA | DEF | PE | USER | LOAD | NA0)
#define	PN_UI_TLB	(PN | ITLB | DATA | DEF | PE | USER | FETC | NA0)

/*
 * P-Cache Parity error(s).
 *			 CPU CLASS  SUBCL  TRAP PROT  MODE  ACCESS  MISC
 */
#define	PN_KD_PC	(PN | DC  | DATA | PRE | PE | KERN | LOAD | NA0)

/*
 * (IPB)I-Cache Prefetch Buffer Parity error(s).
 *			 CPU CLASS  SUBCL  TRAP PROT  MODE  ACCESS  MISC
 */
#define	PN_KI_IPB	(PN | IPB | DATA | PRE | PE | KERN | FETC | NA0)

#ifdef	__cplusplus
}
#endif

#endif /* _MEMTESTIO_PN_H */
