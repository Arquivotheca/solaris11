/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * static char sccsid[] = "@(#)49	1.2.1.1  src/bos/usr/ccs/lib/libc/"
 * "__wcstombs_sb.c, bos, bos410 5/25/92 13:44:07";
 */
/*
 * COMPONENT_NAME: (LIBCCCPPC) LIBC Code-Point/Process-Code Conversion
 * 			Functions
 *
 * FUNCTIONS: __wcstombs_sb
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
#include <sys/localedef.h>
#include <sys/types.h>
#include "libc.h"

/*
 * This is a performance-improved version of the mbstowcs()
 * function for the C locale.
 */

/*ARGSUSED*/	/* *hdl required for interface, don't remove */
size_t
__wcstombs_sb(_LC_charmap_t *hdl, char *ts, const wchar_t *pwcs0, size_t n)
{
	size_t	nn, len;
	uint32_t	wc;
	uint32_t	*pwcs = (uint32_t *)pwcs0;
	unsigned char	*s = (unsigned char *)ts;

	/*
	 * if s is a null pointer, just count the number of characters
	 * in pwcs
	 */
	if (s == NULL) {
		len = 0;
		for (;;) {
			if (*pwcs == 0)
				return (len);
			if (*(pwcs+1) == 0)
				return (len+1);
			if (*(pwcs+2) == 0)
				return (len+2);
			if (*(pwcs+3) == 0)
				return (len+3);
			pwcs += 4;
			len += 4;
		}
	}

	nn = n >> 2;
	for (len = 0; len < nn; len++) {
		if ((wc = *pwcs) > 255U)
			return ((size_t)-1);
		*s = (unsigned char)wc;
		if (wc == 0)
			return (len * 4);

		if ((wc = *(pwcs+1)) > 255U)
			return ((size_t)-1);
		*(s+1) = (unsigned char)wc;
		if (wc == 0)
			return (len * 4 + 1);

		if ((wc = *(pwcs+2)) > 255U)
			return ((size_t)-1);
		*(s+2) = (unsigned char)wc;
		if (wc == 0)
			return (len * 4 + 2);

		if ((wc = *(pwcs+3)) > 255U)
			return ((size_t)-1);
		*(s+3) = (unsigned char)wc;
		if (wc == 0)
			return (len * 4 + 3);

		pwcs += 4;
		s += 4;
	}

	switch (n - len * 4) {
	case 3:
		if ((wc = *pwcs++) > 255U)
			return ((size_t)-1);
		*s++ = (unsigned char)wc;
		if (wc == 0)
			return (n - 3);
		/* FALLTHROUGH */
	case 2:
		if ((wc = *pwcs++) > 255U)
			return ((size_t)-1);
		*s++ = (unsigned char)wc;
		if (wc == 0)
			return (n - 2);
		/* FALLTHROUGH */
	case 1:
		if ((wc = *pwcs) > 255U)
			return ((size_t)-1);
		*s = (unsigned char)wc;
		if (wc == 0)
			return (n - 1);
		/* FALLTHROUGH */
	default:
		return (n);
	}

}
