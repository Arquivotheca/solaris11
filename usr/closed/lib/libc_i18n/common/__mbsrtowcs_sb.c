/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "lint.h"
#include "file64.h"
#include <wchar.h>
#include <sys/localedef.h>
#include "libc.h"
#include "mse.h"

/*ARGSUSED*/	/* *hdl, ps required for interface, don't remove */
size_t
__mbsrtowcs_sb(_LC_charmap_t *hdl, wchar_t *dst, const char **src, size_t len,
	mbstate_t *ps)
{
	unsigned char *s = (unsigned char *)*src;
	size_t	i;

	MBSTATE_RESTART(ps);

	/* equivalent to mbrtowc(dst, NULL, ps), which would return 0 */
	if (s == NULL)
		return (0);

	if (dst == 0)
		return (strlen((const char *)s));

	for (i = 0; i < len; i++) {
		*dst = (wchar_t)*s;
		dst++;

		if (*s == '\0') {
			s = NULL;
			break;
		} else {
			s++;
		}
	}
	*src = (char *)s;
	return (i);
}
