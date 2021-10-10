/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#pragma weak _scrwidth = scrwidth

#include "lint.h"
#include <sys/types.h>
#include <stdlib.h>
#include <wchar.h>
#include <ctype.h>
#include <sys/localedef.h>

int
scrwidth(wchar_t c)
{
	int ret;

	if (isascii(c)) {
		/* ASCII character */
		if (isprint(c)) {
			/* printable */
			return (1);
		} else {
			/* non-printable */
			return (0);
		}
	}
	/*
	 * wcwidth() will check if 'c' is
	 * printable or not.
	 */
	if ((ret = METHOD(__lc_charmap, wcwidth)(__lc_charmap, c)) == -1)
		return (0);
	return (ret);
}
