/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * FUNCTIONS: wcsrtombs
 */

#include "lint.h"
#include "file64.h"
#include <stdio.h>
#include <errno.h>
#include <sys/localedef.h>
#include "libc.h"
#include "mse.h"

size_t
wcsrtombs(char *dst, const wchar_t **src, size_t len, mbstate_t *ps)
{
	if (ps == NULL) {
		if ((ps = _get_internal_mbstate(_WCSRTOMBS)) == NULL) {
			errno = ENOMEM;
			return ((size_t)-1);
		}
	}

	return (METHOD(__lc_charmap, wcsrtombs)(__lc_charmap, dst, src,
			len, ps));
}
