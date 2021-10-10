/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * FUNCTION: wcrtomb
 */

#include "lint.h"
#include "file64.h"
#include <stdio.h>
#include <errno.h>
#include <sys/localedef.h>
#include "libc.h"
#include "mse.h"

size_t
wcrtomb(char *s, wchar_t wc, mbstate_t *ps)
{
	if (ps == NULL) {
		if ((ps = _get_internal_mbstate(_WCRTOMB)) == NULL) {
			errno = ENOMEM;
			return ((size_t)-1);
		}
	}

	return (METHOD(__lc_charmap, wcrtomb)(__lc_charmap, s, wc, ps));
}
