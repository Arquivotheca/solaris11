/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "lint.h"
#include <wchar.h>
#include <sys/localedef.h>

/*ARGSUSED*/
size_t
__mbrlen_gen(_LC_charmap_t *hdl, const char *s, size_t n, mbstate_t *ps)
{
	return (METHOD(hdl, mbrtowc)(hdl, (wchar_t *)0, s, n, ps));
}
