/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "lint.h"
#include "file64.h"
#include <errno.h>
#include <sys/localedef.h>
#include "libc.h"
#include "mse.h"

/*ARGSUSED*/	/* hdl, ps not used here; needed for interface, don't remove */
size_t
__wcrtomb_sb(_LC_charmap_t *hdl, char *s, wchar_t pwc, mbstate_t *ps)
{
	MBSTATE_RESTART(ps);

	/*
	 * if s is NULL return 1
	 */
	if (s == NULL)
		return (1);

	if ((uint32_t)pwc > 255U) {
		errno = EILSEQ;
		return ((size_t)-1);
	}

	s[0] = (char)pwc;

	return (1);
}
