/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * FUNCTION: __mbsinit_sb
 */
#include "lint.h"
#include <sys/localedef.h>
#include <wchar.h>
#include "libc.h"

/* ARGSUSED */
int
__mbsinit_sb(_LC_charmap_t *hdl, const mbstate_t *ps)
{
	if (ps == NULL)
		return (1);

	if (__mbst_get_nconsumed(ps) == 0)
		return (1);

	return (0);
}
