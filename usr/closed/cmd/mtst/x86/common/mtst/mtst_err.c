/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>

#include <mtst_cmd.h>
#include <mtst.h>

static const char *pname;

#pragma init(mtst_getpname)
const char *
mtst_getpname(void)
{
	const char *p, *q;

	if (pname != NULL)
		return (pname);

	if ((p = getexecname()) != NULL)
		q = strrchr(p, '/');
	else
		q = NULL;

	if (q == NULL)
		pname = p;
	else
		pname = q + 1;

	return (pname);
}

void
mtst_vwarn(const char *format, va_list ap)
{
	int err = errno;

	(void) fprintf(stderr, "%s: ", pname);

	if (mtst.mtst_curcmd != NULL)
		(void) fprintf(stderr, "%s: ", mtst.mtst_curcmd->mcmd_cmdname);

	(void) vfprintf(stderr, format, ap);

	if (format[strlen(format) - 1] != '\n')
		(void) fprintf(stderr, ": %s\n", strerror(err));
}

void
mtst_warn(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	mtst_vwarn(format, ap);
	va_end(ap);
}

void
mtst_die(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	mtst_vwarn(format, ap);
	va_end(ap);

	exit(1);
}
