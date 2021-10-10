/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _MEMTESTIO_CHP_H
#define	_MEMTESTIO_CHP_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * UltraSPARC-III+ (Cheetah Plus) specific error definitions.
 */

/*
 * Internal and Protocol Errors (IERR/PERR).
 *			 CPU  CLASS  SUBCL   TRAP  PROT  MODE  ACCESS  MISC
 */
#define	CHP_EC_MH	(CHP | INT | MH    | FAT | NA3 | KERN | LOAD | NA0)
#define	CHP_NO_REFSH	(CHP | INT | REFSH | FAT | NA3 | KERN | LOAD | NA0)

/*
 * Bus errors
 *			 CPU  CLASS  SUBCL  TRAP PROT  MODE  ACCESS   MISC
 */
#define	CHP_KD_DUE	(CHP | BUS | DATA | DEF | UE | KERN | PFETC | NA0)

/*
 * L2$ tag errors
 *			 CPU  CLASS  SUBCL  TRAP PROT  MODE  ACCESS  MISC
 */
#define	CHP_KD_ETSCE	(CHP | L2  | TAG  | PRE | CE | KERN | LOAD | NA0)
#define	CHP_KD_ETSCETL1	(CHP | L2  | TAG  | PRE | CE | KERN | LOAD | TL1)
#define	CHP_KI_ETSCE	(CHP | L2  | TAG  | PRE | CE | KERN | FETC | NA0)
#define	CHP_KI_ETSCETL1	(CHP | L2  | TAG  | PRE | CE | KERN | FETC | TL1)
#define	CHP_UD_ETSCE	(CHP | L2  | TAG  | PRE | CE | USER | LOAD | NA0)
#define	CHP_UI_ETSCE	(CHP | L2  | TAG  | PRE | CE | USER | FETC | NA0)
#define	CHP_KD_ETSUE	(CHP | L2  | TAG  | PRE | UE | KERN | LOAD | NA0)
#define	CHP_KD_ETSUETL1	(CHP | L2  | TAG  | PRE | UE | KERN | LOAD | TL1)
#define	CHP_KI_ETSUE	(CHP | L2  | TAG  | PRE | UE | KERN | FETC | NA0)
#define	CHP_KI_ETSUETL1	(CHP | L2  | TAG  | PRE | UE | KERN | FETC | TL1)
#define	CHP_UD_ETSUE	(CHP | L2  | TAG  | PRE | UE | USER | LOAD | NA0)
#define	CHP_UI_ETSUE	(CHP | L2  | TAG  | PRE | UE | USER | FETC | NA0)
#define	CHP_KD_ETHCE	(CHP | L2  | TAG  | DIS | CE | KERN | BLD | NA0)
#define	CHP_KI_ETHCE	(CHP | L2  | TAG  | DIS | CE | KERN | FETC | NA0)
#define	CHP_UD_ETHCE	(CHP | L2  | TAG  | DIS | CE | USER | BLD | NA0)
#define	CHP_UI_ETHCE	(CHP | L2  | TAG  | DIS | CE | USER | FETC | NA0)
#define	CHP_KD_ETHUE	(CHP | L2  | TAG  | DIS | UE | KERN | BLD | NA0)
#define	CHP_KI_ETHUE	(CHP | L2  | TAG  | DIS | UE | KERN | FETC | NA0)
#define	CHP_UD_ETHUE	(CHP | L2  | TAG  | DIS | UE | USER | LOAD | NA0)
#define	CHP_UI_ETHUE	(CHP | L2  | TAG  | DIS | UE | USER | FETC | NA0)

/*
 * D$ data and tag errors
 *			 CPU  CLASS  SUBCL  TRAP PROT  MODE  ACCESS  MISC
 */
#define	CHP_KD_DDSPEL	(CHP | DC  | DATA | PRE | PE | KERN | LOAD | NA0)
#define	CHP_KD_DDSPELTL1 (CHP| DC  | DATA | PRE | PE | KERN | LOAD | TL1)
#define	CHP_KD_DDSPES	(CHP | DC  | DATA | PRE | PE | KERN | STOR | NA0  | NI)
#define	CHP_UD_DDSPEL	(CHP | DC  | DATA | PRE | PE | USER | LOAD | NA0  | NI)
#define	CHP_UD_DDSPES	(CHP | DC  | DATA | PRE | PE | USER | STOR | NA0  | NI)
#define	CHP_KD_DTSPEL	(CHP | DC  | TAG  | PRE | PE | KERN | LOAD | NA0)
#define	CHP_KD_DTSPELTL1 (CHP| DC  | TAG  | PRE | PE | KERN | LOAD | TL1)
#define	CHP_KD_DTSPES	(CHP | DC  | TAG  | PRE | PE | KERN | STOR | NA0  | NI)
#define	CHP_UD_DTSPEL	(CHP | DC  | TAG  | PRE | PE | USER | LOAD | NA0  | NI)
#define	CHP_UD_DTSPES	(CHP | DC  | TAG  | PRE | PE | USER | STOR | NA0  | NI)
#define	CHP_KD_DTHPEL	(CHP | DC  | TAG  | NA4 | PE | KERN | LOAD | NA0)
#define	CHP_KD_DTHPES	(CHP | DC  | TAG  | NA4 | PE | KERN | STOR | NA0  | NI)
#define	CHP_UD_DTHPEL	(CHP | DC  | TAG  | NA4 | PE | USER | LOAD | NA0  | NI)
#define	CHP_UD_DTHPES	(CHP | DC  | TAG  | NA4 | PE | USER | STOR | NA0  | NI)

/*
 * I$ data and tag errors
 *			 CPU  CLASS  SUBCL  TRAP PROT  MODE  ACCESS  MISC
 */
#define	CHP_KI_IDSPE	(CHP | IC  | DATA | PRE | PE | KERN | FETC | NA0)
#define	CHP_KI_IDSPETL1	(CHP | IC  | DATA | PRE | PE | KERN | FETC | TL1)
#define	CHP_KI_IDSPEPCR	(CHP | IC  | DATA | PRE | PE | KERN | FETC | PCR)
#define	CHP_UI_IDSPE	(CHP | IC  | DATA | PRE | PE | USER | FETC | NA0  | NI)
#define	CHP_KI_ITSPE	(CHP | IC  | TAG  | PRE | PE | KERN | FETC | NA0)
#define	CHP_KI_ITSPETL1	(CHP | IC  | TAG  | PRE | PE | KERN | FETC | TL1)
#define	CHP_UI_ITSPE	(CHP | IC  | TAG  | PRE | PE | USER | FETC | NA0  | NI)
#define	CHP_KI_ITHPE	(CHP | IC  | TAG  | NA4 | PE | KERN | FETC | NA0)
#define	CHP_UI_ITHPE	(CHP | IC  | TAG  | NA4 | PE | USER | FETC | NA0  | NI)

#ifdef	__cplusplus
}
#endif

#endif	/* _MEMTESTIO_CHP_H */
