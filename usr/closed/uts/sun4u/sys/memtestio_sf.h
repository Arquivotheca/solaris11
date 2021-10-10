/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _MEMTESTIO_SF_H
#define	_MEMTESTIO_SF_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * UltraSPARC-II (Spitfire) specific error definitions.
 */

/*
 * UPA bus definitions.
 */
#define	ERR_UPA_SHIFT		(ERR_CPU_SHIFT + 4)
#define	ERR_UPA_INV		UINT64_C(0x0)	/* invalid */
#define	ERR_UPA_NONE		UINT64_C(0x1)	/* nothing */
#define	ERR_UPA_BERR		UINT64_C(0x2)	/* bus error */
#define	ERR_UPA_TO		UINT64_C(0x3)	/* timeout error */
#define	ERR_UPA_MASK		(0xf)
#define	ERR_UPA(x)		(((x) >> ERR_UPA_SHIFT) & ERR_UPA_MASK)
#define	ERR_UPA_ISBERR(x)	(ERR_UPA(x) == ERR_UPA_BERR)
#define	ERR_UPA_ISTO(x)		(ERR_UPA(x) == ERR_UPA_TO)
#define	UNA			(ERR_UPA_NONE	<< ERR_UPA_SHIFT)
#define	UBE			(ERR_UPA_BERR	<< ERR_UPA_SHIFT)
#define	UTO			(ERR_UPA_TO	<< ERR_UPA_SHIFT)

/*
 * UPA Bus errors.
 *			 CPU CLASS  SUBCL  TRAP PROT  MODE  ACCESS  MISC  UPA
 */
#define	SF_KD_BE	(SF | BUS | DATA | DEF | BE | KERN | LOAD | NA0 | UBE)
#define	SF_KD_BEPEEK	(SF | BUS | DATA | DEF | BE | KERN | LOAD | DDIPEEK \
									| UBE)
#define	SF_UD_BE	(SF | BUS | DATA | DEF | BE | USER | LOAD | NI  | UBE)
#define	SF_KD_TO	(SF | BUS | DATA | DEF | BE | KERN | LOAD | NI  | UTO)
#define	SF_UD_TO	(SF | BUS | DATA | DEF | BE | USER | LOAD | NI  | UTO)
#define	SF_K_IVU	(SF | BUS | INTR | DEF | UE | KERN | NA1  | NI  | UNA)
#define	SF_K_IVC	(SF | BUS | INTR | DEF | CE | KERN | NA1  | NI  | UNA)

/*
 * L2$ data errors.
 *			 CPU CLASS  SUBCL  TRAP PROT  MODE  ACCESS  MISC
 */
#define	SF_KD_EDP	(SF | L2  | DATA | DEF | PE | KERN | LOAD | NA0)
#define	SF_KU_EDPCOPYIN	(SF | L2  | DATA | DEF | PE | KERN | LOAD | CPIN)
#define	SF_KD_EDPTL1	(SF | L2  | DATA | DEF | PE | KERN | LOAD | TL1)
#define	SF_KI_EDP	(SF | L2  | DATA | DEF | PE | KERN | FETC | NA0)
#define	SF_KI_EDPTL1	(SF | L2  | DATA | DEF | PE | KERN | FETC | TL1)
#define	SF_UD_EDP	(SF | L2  | DATA | DEF | PE | USER | LOAD | NA0)
#define	SF_UI_EDP	(SF | L2  | DATA | DEF | PE | USER | FETC | NA0)
#define	SF_OBPD_EDP	(SF | L2  | DATA | DEF | PE | OBP  | LOAD | NA0)

/*
 * L2$ tag errors.
 *			 CPU CLASS  SUBCL  TRAP PROT  MODE  ACCESS  MISC
 */
#define	SF_KD_ETP	(SF | L2  | TAG  | DEF | PE | KERN | LOAD | NA0)

/*
 * L2$ write-back errors.
 *			 CPU CLASS   SUBCL  TRAP PROT  MODE  ACCESS  MISC
 */
#define	SF_KD_WP	(SF | L2WB | DATA | DIS | PE | KERN | LOAD | NA0)
#define	SF_KI_WP	(SF | L2WB | DATA | DIS | PE | KERN | FETC | NA0)
#define	SF_UD_WP	(SF | L2WB | DATA | DIS | PE | USER | LOAD | NA0)
#define	SF_UI_WP	(SF | L2WB | DATA | DIS | PE | USER | FETC | NA0 | NS)

/*
 * L2$ copy-back errors.
 *			 CPU CLASS   SUBCL  TRAP PROT  MODE  ACCESS  MISC
 */
#define	SF_KD_CP	(SF | L2CP | DATA | DIS | PE | KERN | LOAD | NA0)
#define	SF_UD_CP	(SF | L2CP | DATA | DIS | PE | USER | LOAD | NA0)

#ifdef	__cplusplus
}
#endif

#endif	/* _MEMTESTIO_SF_H */
