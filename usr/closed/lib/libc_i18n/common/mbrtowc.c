/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * FUNCTION: mbrtowc
 */
#include "lint.h"
#include "file64.h"
#include <sys/localedef.h>
#include <wchar.h>
#include <errno.h>
#include "libc.h"
#include "mse.h"

size_t
mbrtowc(wchar_t *pwc, const char *s, size_t len, mbstate_t *ps)
{
	if (ps == NULL) {
		if ((ps = _get_internal_mbstate(_MBRTOWC)) == NULL) {
			errno = ENOMEM;
			return ((size_t)-1);
		}
	}

	return (METHOD(__lc_charmap, mbrtowc)(__lc_charmap, pwc, s, len, ps));
}
