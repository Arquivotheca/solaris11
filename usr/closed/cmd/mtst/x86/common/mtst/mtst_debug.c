/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Debuggability/observability support routines
 */

#include <stdio.h>
#include <stdarg.h>

#include <mtst_err.h>
#include <mtst.h>

void
mtst_vdprintf(const char *cmd, const char *fmt, va_list ap)
{
	(void) fprintf(stderr, "DEBUG: %s", (cmd == NULL ? "" : cmd));
	(void) vfprintf(stderr, fmt, ap);
}

void
mtst_dprintf(const char *fmt, ...)
{
	va_list ap;

	if (!(mtst.mtst_flags & MTST_F_DEBUG))
		return;

	va_start(ap, fmt);
	mtst_vdprintf(NULL, fmt, ap);
	va_end(ap);
}

#ifdef DEBUG
int
mtst_dassert(const char *expr, const char *file, int line)
{
	mtst_die("\"%s\", line %d: assertion failed: %s\n", file, line, expr);
	/*NOTREACHED*/
	return (0);
}
#endif
