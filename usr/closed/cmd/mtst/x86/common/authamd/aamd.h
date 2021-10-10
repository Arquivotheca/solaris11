/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifndef _AAMD_H
#define	_AAMD_H

#include <sys/types.h>
#include <sys/mca_x86.h>

#include <mtst_cpumod_api.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int aamd_synthesize_nb(mtst_cpuid_t *, uint_t, const mtst_argspec_t *,
    int, uint64_t);
extern int aamd_synthesize_cmn(mtst_cpuid_t *, uint_t, const mtst_argspec_t *,
    int, uint64_t);

#ifdef __cplusplus
}
#endif

#endif /* _AAMD_H */
