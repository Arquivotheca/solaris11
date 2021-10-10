/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "lint.h"
#include "file64.h"
#include <stdlib.h>
#include <sys/localedef.h>
#include <limits.h>
#include "mse.h"

size_t
__wcsrtombs_dense(_LC_charmap_t *hdl, char *dst, const wchar_t **src,
		size_t len, mbstate_t *ps)
{
	size_t	val;
	size_t	total = 0;
	char	temp[MB_LEN_MAX];
	size_t	i;
	const wchar_t *src0 = *src;

	MBSTATE_RESTART(ps);

	for (;;) {
		if (dst && (total == len))
			break;
		if (*src0 == 0) {
			if (dst) {
				*dst = '\0';
				*src = NULL;
			}
			break;
		}
		if ((val = METHOD_NATIVE(hdl, wcrtomb)
		    (hdl, temp, *src0, ps)) == (size_t)-1) {
			return (val);
		}
		total += val;
		if (dst && (total > len)) {
			total -= val;
			break;
		}
		if (dst != NULL) {
			for (i = 0; i < val; i++)
				*dst++ = temp[i];
			(*src)++;
		}
		src0++;
	}
	return (total);
}
