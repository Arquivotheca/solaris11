/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "lint.h"
#include <sys/localedef.h>
#include <wchar.h>

/*ARGSUSED*/	/* hdl not used here, but needed for interface, don't remove */
int
__wctob_sb(_LC_charmap_t *hdl, wint_t c)
{
	if ((c & ~0xff) == 0) {
		/* single-byte char */
		return ((int)c);
	}
	return (EOF);
}
