/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * FUNCTION: mbsrtowcs
 */

#include "lint.h"
#include "file64.h"
#include <wchar.h>
#include <sys/localedef.h>
#include <errno.h>
#include "libc.h"
#include "mse.h"

size_t
mbsrtowcs(wchar_t *dst, const char **src, size_t len, mbstate_t *ps)
{
	if (ps == NULL) {
		if ((ps = _get_internal_mbstate(_MBSRTOWCS)) == NULL) {
			errno = ENOMEM;
			return ((size_t)-1);
		}
	}

	return (METHOD(__lc_charmap, mbsrtowcs)(__lc_charmap, dst, src, len,
			ps));
}
