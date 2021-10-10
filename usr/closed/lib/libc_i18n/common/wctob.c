/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * FUNCTIONS: wctob
 */

#include "lint.h"
#include <stdio.h>
#include <wchar.h>
#include <sys/localedef.h>

int
wctob(wint_t c)
{
	return (METHOD(__lc_charmap, wctob)(__lc_charmap, c));
}
