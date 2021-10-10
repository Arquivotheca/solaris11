/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * static char sccsid[] = "@(#)46	1.3.1.1  src/bos/usr/ccs/lib/libc/"
 * "__mbtowc_sb.c , bos, bos410 5/25/92 13:43:49";
 */
/*
 * COMPONENT_NAME: (LIBCCCPPC) LIBC Code-Point/Process-Code Conversion Functions
 *
 * FUNCTIONS: __mbtowc_sb
 *
 * ORIGINS: 27
 *
 * This module contains IBM CONFIDENTIAL code. -- (IBM
 * Confidential Restricted when combined with the aggregated
 * modules for this product)
 * OBJECT CODE ONLY SOURCE MATERIALS
 * (C) COPYRIGHT International Business Machines Corp. 1991 , 1992
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 *
 */

#include "lint.h"
#include <errno.h>
#include <sys/localedef.h>
#include <sys/types.h>

/* ARGSUSED */	/* *hdl required for interface, don't remove */
int
__mbtowc_sb(_LC_charmap_t *hdl, wchar_t *pwc, const char *ts, size_t len)
{
	unsigned char *s = (unsigned char *)ts;

	/*
	 * if s is NULL return 0
	 */
	if (s == NULL)
		return (0);

	/*
	 * If length == 0 return -1
	 */
	if (len < 1) {
		errno = EILSEQ;
		return (-1);
	}

	/*
	 * If pwc is not NULL, assign s to pwc.
	 * length is 1 unless NULL which has length 0
	 */
	if (pwc != NULL)
		*pwc = (wchar_t)*s;
	if (s[0] != '\0')
		return (1);
	else
		return (0);
}
