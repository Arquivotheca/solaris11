/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _MTST_DEBUG_H
#define	_MTST_DEBUG_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef DEBUG
extern int mtst_dassert(const char *, const char *, int);
#define	ASSERT(x)	((void)((x) || mtst_dassert(#x, __FILE__, __LINE__)))
#else
#define	ASSERT(x)
#endif

extern void mtst_dprintf(const char *, ...);
extern void mtst_vdprintf(const char *, const char *, va_list);

#ifdef __cplusplus
}
#endif

#endif /* _MTST_DEBUG_H */
