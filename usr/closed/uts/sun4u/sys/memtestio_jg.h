/*
 * Copyright 2003 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _MEMTESTIO_JG_H
#define	_MEMTESTIO_JG_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * UltraSPARC-IV (Jaguar) specific error definitions.
 */

#define	ERR_JG_SHIFT	(ERR_CPU_SHIFT + 4)
#define	ERR_JG_INV	UINT64_C(0x0)   /* invalid */
#define	ERR_JG_PPE	UINT64_C(0x1)   /* data parity */
#define	ERR_JG_DPE	UINT64_C(0x2)   /* LSB parity */

#define	PPE		(ERR_JG_PPE << ERR_JG_SHIFT)
#define	DPE		(ERR_JG_DPE << ERR_JG_SHIFT)

/*
 * System bus errors.
 *			 CPU CLASS  SUBCL  TRAP PROT  MODE  ACCESS  MISC
 */
#define	JG_KD_PPE	(JG | BUS | DATA | FAT | PE | KERN | LOAD | NA0 | PPE)
#define	JG_KD_DPE	(JG | BUS | DATA | FAT | PE | KERN | LOAD | NA0 | DPE)
#define	JG_KD_SAF	(JG | BUS | ADDR | FAT | PE | KERN | LOAD | NA0)

#ifdef	__cplusplus
}
#endif

#endif	/* _MEMTESTIO_JG_H */
