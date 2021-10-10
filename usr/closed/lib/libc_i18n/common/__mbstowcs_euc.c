/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "lint.h"
#include <stdlib.h>
#include <string.h>
#include <sys/localedef.h>

/*ARGSUSED*/
size_t
__mbstowcs_euc(_LC_charmap_t *hdl, wchar_t *pwcs, const char *s, size_t n)
{
	int	val;
	size_t	i, count;

	if (pwcs == NULL)
		count = strlen(s);
	else
		count = n;

	for (i = 0; i < count; i++) {
		if ((val = METHOD(hdl, mbtowc)(hdl, pwcs, s, MB_CUR_MAX)) == -1)
			return ((size_t)-1);
		if (val == 0)
			break;
		s += val;
		if (pwcs != NULL)
			pwcs++;
	}
	return (i);
}
