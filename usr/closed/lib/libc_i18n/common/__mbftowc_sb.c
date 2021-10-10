/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "lint.h"
#include <sys/localedef.h>
#include "libc.h"

/*ARGSUSED*/
int
__mbftowc_sb(_LC_charmap_t *hdl, char *ts, wchar_t *wchar,
		int (*f)(void), int *peekc)
{
	unsigned char *s = (unsigned char *)ts;
	int c;

	if ((c = (*f)()) < 0)
		return (0);

	*s = (unsigned char)c;

	*wchar = (wchar_t)*s;
	return (1);
}
