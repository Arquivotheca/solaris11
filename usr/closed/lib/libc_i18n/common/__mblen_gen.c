/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "lint.h"
#include <sys/localedef.h>
#include <stdlib.h>

/*ARGSUSED*/
int
__mblen_gen(_LC_charmap_t *hdl, const char *s, size_t n)
{
	return (mbtowc((wchar_t *)0, s, n));
}
