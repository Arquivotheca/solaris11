/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 *
 * FUNCTIONS: mbsinit
 *
 */

#include "lint.h"
#include <wchar.h>
#include <sys/localedef.h>

int
mbsinit(const mbstate_t *ps)
{
	return (METHOD(__lc_charmap, mbsinit)(__lc_charmap, ps));
}
