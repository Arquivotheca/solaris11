/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _MEMTEST_ASM_H
#define	_MEMTEST_ASM_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Common memtest header file for assembly routines.
 */

#include <sys/asm_linkage.h>
#include <sys/sun4asi.h>
#include <sys/privregs.h>

/*
 * The following definitions were not in any kernel header files.
 * Some if not all of them probably should be.
 */

/*
 * CPU Version Register defs.
 */
#define	CPUVER_IMPL_SHIFT	32	/* version reg IMPL field shift */
#define	CPUVER_IMPL_MASK	0xFFFF	/* version reg IMPL field mask */

#ifndef FLUSH_ADDR
#define	FLUSH_ADDR	0x00	/* note some chips ignore flush operand */
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _MEMTEST_ASM_H */
