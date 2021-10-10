/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _MEMTEST_CMP_H
#define	_MEMTEST_CMP_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef _ASM
#else

/*
 * CMP specific header file for memtest loadable driver.
 */

/*
 * Routines located in memtest_cmp.c
 */
extern	int	memtest_cmp_quiesce(mdata_t *mdatap);
extern	int	memtest_cmp_unquiesce(mdata_t *mdatap);
struct	cpu	*memtest_get_sibling(mdata_t *mdatap);
extern	int	memtest_park_core(mdata_t *mdatap);
extern	int	memtest_unpark_core(mdata_t *mdatap);

/*
 * Routines located in memtest_cmp_asm.s
 */
extern  uint64_t	cmp_park_core(int core_id);
extern  uint64_t	cmp_unpark_core(int core_id);
extern  uint64_t	cmp_get_core_id();
extern  uint64_t	cmp_get_core_reg(uint64_t as, uint64_t va);

#endif /* _ASM */

#define	SIBLING_CORE_ID(X)	((X) == 0 ? 1 : 0)

#ifdef	__cplusplus
}
#endif

#endif /* _MEMTEST_CMP_H */
