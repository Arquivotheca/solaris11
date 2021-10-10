/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "lint.h"
#include "file64.h"
#include <errno.h>
#include <sys/localedef.h>
#include "mse.h"

/* ARGSUSED */	/* *hdl required for interface, don't remove */
size_t
__mbrtowc_sb(_LC_charmap_t *hdl, wchar_t *pwc, const char *ts, size_t len,
		mbstate_t *ps)
{
	unsigned char *s = (unsigned char *)ts;

	/*
	 * If s is NULL, equivalent to mbrtowc(NULL, "", 1, ps)
	 */
	if (s == NULL) {
		s = (unsigned char *)"";
		len = 1;
		pwc = NULL;
	}

	/*
	 * zero bytes contribute to an incomplete, but
	 * potentially valid character
	 */
	if (len == 0) {
		return ((size_t)-2);
	}

	MBSTATE_RESTART(ps);

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
