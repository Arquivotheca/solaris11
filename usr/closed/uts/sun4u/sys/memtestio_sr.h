/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _MEMTESTIO_SR_H
#define	_MEMTESTIO_SR_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * UltraSPARC-IIIi+ (Serrano) specific error definitions.
 */

/*
 * L2$ tag errors.
 *			CPU  CLASS  SUBCL  TRAP PROT  MODE  ACCESS  MISC  JBUS
 */
#define	SR_KD_ETU	(SR  | L2 | TAG  | DEF | UE | KERN | LOAD | NA0 | JNA)
#define	SR_KI_ETU	(SR  | L2 | TAG  | DEF | UE | KERN | FETC | NI  | JNA)
#define	SR_UD_ETU	(SR  | L2 | TAG  | DEF | UE | USER | LOAD | NA0 | JNA)
#define	SR_UI_ETU	(SR  | L2 | TAG  | DEF | UE | USER | FETC | NI  | JNA)
#define	SR_KD_ETC	(SR  | L2 | TAG  | DIS | CE | KERN | LOAD | NA0 | JNA)
#define	SR_KD_ETCTL1	(SR  | L2 | TAG  | DIS | CE | KERN | LOAD | TL1 | JNA)
#define	SR_KI_ETC	(SR  | L2 | TAG  | DIS | CE | KERN | FETC | NI  | JNA)
#define	SR_KI_ETCTL1	(SR  | L2 | TAG  | DIS | CE | KERN | FETC | NI  | JNA)
#define	SR_UD_ETC	(SR  | L2 | TAG  | DIS | CE | USER | LOAD | NA0 | JNA)
#define	SR_UI_ETC	(SR  | L2 | TAG  | DIS | CE | USER | FETC | NI  | JNA)
#define	SR_KD_ETI	(SR  | L2 | TAG  | DIS | CE | KERN | LOAD | NI  | JNA)
#define	SR_KD_ETS	(SR  | L2 | TAG  | DEF | UE | KERN | LOAD | NI  | JNA)

#ifdef	__cplusplus
}
#endif

#endif	/* _MEMTESTIO_SR_H */
