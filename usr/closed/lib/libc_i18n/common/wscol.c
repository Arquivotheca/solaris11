/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*	Copyright (c) 1986 AT&T	*/
/*	  All Rights Reserved  	*/


#include "lint.h"
#include <stdlib.h>
#include <widec.h>
#include <sys/localedef.h>

int
wscol(const wchar_t *ws)
{

	int	col = 0;
	int	ret;

	while (*ws) {
		ret = (METHOD(__lc_charmap, wcwidth)(__lc_charmap, *ws));
	/*
	 * Note that this CSIed version has a bit of different behavior from
	 * the original code in which non-printable char may have various
	 * display width assigned.
	 * If this ever cause complain from user, a specific method may be
	 * required to support Solaris specific wide-character funcitons.
	 */
		if (ret == -1) { /* Nonprintable char */
			col++;
		} else {
			col += ret;
		}
		ws++;
	}
	return (col);
}
