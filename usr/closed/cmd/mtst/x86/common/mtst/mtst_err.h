/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _MTST_ERR_H
#define	_MTST_ERR_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const char *mtst_getpname(void);
extern void mtst_vwarn(const char *, va_list);
extern void mtst_warn(const char *, ...);
extern void mtst_die(const char *, ...);

#ifdef __cplusplus
}
#endif

#endif /* _MTST_ERR_H */
