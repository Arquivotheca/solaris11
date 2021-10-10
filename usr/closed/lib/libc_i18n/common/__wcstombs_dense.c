/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "lint.h"
#include <stdlib.h>
#include <sys/localedef.h>
#include <limits.h>

size_t
__wcstombs_dense(_LC_charmap_t *hdl, char *s, const wchar_t *pwcs, size_t n)
{
	int	val;
	size_t	total = 0;
	char	temp[MB_LEN_MAX];
	int	i;

	for (;;) {
		if (s && (total == n))
			break;
		if (*pwcs == 0) {
			if (s)
				*s = '\0';
			break;
		}
		if ((val = METHOD_NATIVE(hdl, wctomb)
				(hdl, temp, *pwcs++)) == -1)
			return (val);
		total += val;
		if (s && (total > n)) {
			total -= val;
			break;
		}
		if (s != NULL) {
			for (i = 0; i < val; i++)
				*s++ = temp[i];
		}
	}
	return (total);
}
