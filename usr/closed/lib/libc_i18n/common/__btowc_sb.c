/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "lint.h"
#include <sys/localedef.h>
#include <wchar.h>


/*ARGSUSED*/	/* *hdl required for interface, don't remove */
wint_t
__btowc_sb(_LC_charmap_t *hdl, int c)
{
	if ((c & ~0xff) == 0) {	/* single-byte character */
		/*
		 * will return the dense wide-char representation.
		 */
		return ((wint_t)c);
	}
	/* EOF case is also handled here */
	return (WEOF);
}
