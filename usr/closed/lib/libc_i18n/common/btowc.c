/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * FUNCTIONS: btowc
 */

#include "lint.h"
#include <stdio.h>
#include <wchar.h>
#include <sys/localedef.h>

wint_t
btowc(int c)
{
	return (METHOD(__lc_charmap, btowc)(__lc_charmap, c));
}
