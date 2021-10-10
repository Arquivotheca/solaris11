/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * This file may contain confidential information of the Defense
 * Intelligence Agency and MITRE Corporation and should not be
 * distributed in source form without approval from Sun Legal.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifndef lint
static char    *l_ID = "CMW Labels Release 2.2.1; 11/24/93: l_error.c";
#endif	/* lint */

#include <stdio.h>
#include <stdarg.h>
#include <syslog.h>

#include <tsol/label.h>

#include "impl.h"

/*
 * The external subroutine l_error prints an approrpriate error message about
 * a fatal conversion error, then frees any allocated storage, and closes the
 * encodings file.
 */

/* VARARGS1 */
void
l_error(const unsigned int line_number, const char *format, ...)
{
	va_list	v;

	if (debug) {
		if (line_number == 0)
			(void) fprintf(stderr,
			    "Label encodings conversion error:\n   ");
		else
			(void) fprintf(stderr,
			    "Label encodings conversion error at line %d:\n   ",
			    line_number);
		va_start(v, format);
		(void) vfprintf(stderr, format, v);
		va_end(v);
	} else {
		if (line_number == 0)
			(void) syslog(LOG_ERR,
			    "Label encodings conversion error:");
		else
			(void) syslog(LOG_ERR,
			    "Label encodings conversion error at line %d:",
			    line_number);

		va_start(v, format);
		(void) vsyslog(LOG_ERR, format, v);
		va_end(v);
	}
}
