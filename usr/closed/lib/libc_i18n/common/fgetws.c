/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*	Copyright (c) 1986 AT&T	*/
/*	  All Rights Reserved  	*/


/*	This module is created for NLS on Sep.03.86		*/

/*
 * Fgetws reads EUC characters from the "iop", converts
 * them to process codes, and places them in the wchar_t
 * array pointed to by "ptr". Fgetws reads until n-1 process
 * codes are transferred to "ptr", or EOF.
 */

#include "lint.h"
#include "file64.h"
#include "mse_int.h"
#include <stdlib.h>
#include <stdio.h>
#include <widec.h>
#include <errno.h>
#include <sys/localedef.h>
#include "mtlib.h"
#include "stdiom.h"
#include "libc.h"
#include "mse.h"
#include "libc_i18n.h"

#undef fgetws

wchar_t *
fgetws(wchar_t *ptr, int size, FILE *iop)
{
	wchar_t *ptr0 = ptr;
	int c;
	rmutex_t	*lk;
	wint_t	(*fp_fgetwc)(_LC_charmap_t *, FILE *);

	FLOCKFILE(lk, iop);
	fp_fgetwc = METHOD(__lc_charmap, fgetwc);
	for (size--; size > 0; size--) { /* until size-1 */
		if ((c = fp_fgetwc(__lc_charmap, iop)) == EOF) {
			if (ptr == ptr0) { /* no data */
				FUNLOCKFILE(lk);
				return (NULL);
			}
			break; /* no more data */
		}
		*ptr++ = c;
		if (c == '\n')   /* new line character */
			break;
	}
	*ptr = 0;
	FUNLOCKFILE(lk);
	return (ptr0);
}
