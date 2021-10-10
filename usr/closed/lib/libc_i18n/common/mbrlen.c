/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * FUNCTIONS: mbrlen
 */

#include "lint.h"
#include "file64.h"
#include <wchar.h>
#include <errno.h>
#include <sys/localedef.h>
#include "libc.h"
#include "mse.h"

size_t
mbrlen(const char *s, size_t n, mbstate_t *ps)
{
	if (ps == NULL) {
		if ((ps = _get_internal_mbstate(_MBRLEN)) == NULL) {
			errno = ENOMEM;
			return ((size_t)-1);
		}
	}

	return (METHOD(__lc_charmap, mbrlen)(__lc_charmap, s, n, ps));
}
