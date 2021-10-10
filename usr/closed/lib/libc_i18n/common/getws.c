/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*	Copyright (c) 1986 AT&T	*/
/*	  All Rights Reserved  	*/


/*	This module is created for NLS on Sep.03.86		*/

/*
 * Getws reads multibyte characters from stdin, converts them to process
 * codes, and places them in the array pointed to by "s". Getws
 * reads until a new-line character is read or an EOF.
 */

#include "lint.h"
#include "file64.h"
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <sys/localedef.h>
#include "mtlib.h"
#include "stdiom.h"
#include "libc.h"

wchar_t *
getws(wchar_t *ptr)
{
	wchar_t *ptr0 = ptr;
	int c;
	rmutex_t	*lk;
	wint_t	(*fp_fgetwc)(_LC_charmap_t *, FILE *);


	FLOCKFILE(lk, stdin);
	fp_fgetwc = METHOD(__lc_charmap, fgetwc);
	for (;;) {
		if ((c = fp_fgetwc(__lc_charmap, stdin)) == EOF) {
			if (ptr == ptr0) { /* no data */
				FUNLOCKFILE(lk);
				return (NULL);
			}
			break; /* no more data */
		}
		if (c == '\n') /* new line character */
			break;
		*ptr++ = c;
	}
	*ptr = 0;
	FUNLOCKFILE(lk);
	return (ptr0);
}
