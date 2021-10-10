/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _MEMTEST_U_ASM_H
#define	_MEMTEST_U_ASM_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Sun4u memtest header file for assembly routines.
 */

#include <sys/memtest_asm.h>
/*
 * Need to define HUMMINGBIRD to get defs in spitregs.h.
 */
#define	HUMMINGBIRD
#include <sys/spitregs.h>
#include <sys/cheetahregs.h>

/*
 * The following definitions were not in any kernel header files.
 * Some if not all of them probably should be.
 */

/*
 * Spitfire Data Buffer Register defs.
 */
#define	SDB_FMD			0x100	/* force bad ecc mode bit */

/*
 * Cheetah+ specific defs.
 */
#define	CHP_DCACHE_IDX_INCR	0x4000	/* cheetah+ dcache index increment */
#define	CHP_ICACHE_FN_ADDR_ICR	0x2000	/* increment to access I$ target fn's */

/*
 * Hummingbird defs.
 */
#define	HB_EC_SET_SIZE_SHIFT	2	/* 4-way associative */
#define	HB_EC_TAG_MASK		0xFFFF	/* hummingbird ecache tag mask */

/*
 * L2 Cache Error Enable Register defs.
 */
#define	EN_REG_FME		0x00000400	/* force memory ecc */
#define	EN_REG_FMED		0x000ff800	/* memory ecc to force */
#define	EN_REG_FSP		0x00000020	/* force JBus parity */
#define	EN_REG_FPD		0x000003c0	/* JBus parity to force */
#define	EN_REG_FMED_SHIFT	11		/* shift for FMED field */
#define	EN_REG_FPD_SHIFT	6		/* shift for FPD field */
#define	EN_REG_FDECC_SHIFT	4		/* shift for data ECC */
#define	EN_REG_FMECC_SHIFT	14		/* shift for Mtag ECC */

/*
 * Jalapeno specific defs.
 */
#define	JA_EC_IDX_DISP_FLUSH	0x80000000
#define	JA_EC_IDX_WAY_SHIFT	32

/*
 * Serrano specific defs.
 */
#define	SR_L2_WAYS_MASK		0x300000	/* mask for way bits */
#define	SR_L2_WAYS_SHIFT	12		/* way shift */

#ifdef	__cplusplus
}
#endif

#endif	/* _MEMTEST_U_ASM_H */
