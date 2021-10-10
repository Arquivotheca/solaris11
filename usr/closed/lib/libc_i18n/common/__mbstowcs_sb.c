/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "lint.h"
#include <string.h>
#include <sys/localedef.h>
#include <sys/types.h>
#include "libc.h"

/*
 * This is a performance-improved version of the mbstowcs()
 * function for the C locale.
 */

/*ARGSUSED*/	/* *hdl required for interface, don't remove */
size_t
__mbstowcs_sb(_LC_charmap_t *hdl, wchar_t *pwcs0, const char *ts, size_t n)
{
	size_t	nn, len;
	unsigned char	*s = (unsigned char *)ts;
	wchar_t	*pwcs = (wchar_t *)pwcs0;

	if (s == NULL)
		return (0);

	if (pwcs == 0)
		return (strlen((const char *)s));

	nn = n >> 2;
	for (len = 0; len < nn; len++) {
		if ((*pwcs = (wchar_t)*s) == 0)
			return (len * 4);

		if ((*(pwcs+1) = (wchar_t)*(s+1)) == 0)
			return (len * 4 + 1);

		if ((*(pwcs+2) = (wchar_t)*(s+2)) == 0)
			return (len * 4 + 2);

		if ((*(pwcs+3) = (wchar_t)*(s+3)) == 0)
			return (len * 4 + 3);

		pwcs += 4;
		s += 4;
	}

	switch (n - len * 4) {
	case 3:
		if ((*pwcs++ = (wchar_t)*s++) == 0)
			return (n - 3);
		/* FALLTHROUGH */
	case 2:
		if ((*pwcs++ = (wchar_t)*s++) == 0)
			return (n - 2);
		/* FALLTHROUGH */
	case 1:
		if ((*pwcs = (wchar_t)*s) == 0)
			return (n - 1);
		/* FALLTHROUGH */
	default:
		return (n);
	}
}
