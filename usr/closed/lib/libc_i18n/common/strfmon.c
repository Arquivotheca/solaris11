/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * static char sccsid[] = "@(#)64	1.3.1.2  src/bos/usr/ccs/lib/libc/"
 * "strfmon.c, bos, bos410 1/12/93 11:19:23";
 */
/*
 * COMPONENT_NAME: LIBCFMT
 *
 * FUNCTIONS:  strfmon
 *
 * ORIGINS: 27
 *
 * IBM CONFIDENTIAL -- (IBM Confidential Restricted when
 * combined with the aggregated modules for this product)
 * OBJECT CODE ONLY SOURCE MATERIALS
 * (C) COPYRIGHT International Business Machines Corp. 1991, 1992
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * FUNCTION: strfmon() is a method driven fucntion where the actual monetary
 *	     formatting is done by a method pointed to by the current locale.
 *           The method formats a list of values and outputs them to the
 *           output buffer s. The values returned are affected by the format
 *           string and the setting of the locale category LC_MONETARY.
 *
 * PARAMETERS:
 *           char *s - location of returned string
 *           size_t maxsize - maximum length of output including the null
 *                            termination character.
 *           char *format - format that montary value is to be printed out
 *
 * RETURN VALUE DESCRIPTIONS:
 *           - returns the number of bytes that comprise the return string
 *             excluding the terminating null character.
 *           - returns 0 if s is longer than maxsize
 */

#pragma	weak _strfmon = strfmon

#include "lint.h"
#include <sys/localedef.h>

ssize_t
strfmon(char *s, size_t maxsize, const char *format, ...)
{
	va_list ap;
	ssize_t i;

	va_start(ap, format);

	i = METHOD(__lc_monetary, strfmon)(__lc_monetary, s, maxsize,
	    format, ap);
	va_end(ap);
	return (i);
}
