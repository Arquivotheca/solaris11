/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _MTST_INJECT_H
#define	_MTST_INJECT_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/types.h>

#include <mtst_cpumod_api.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int mtst_inject_sequence(mtst_inj_stmt_t *, uint_t);

#ifdef __cplusplus
}
#endif

#endif /* _MTST_INJECT_H */
