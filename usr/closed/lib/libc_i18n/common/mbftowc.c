/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/


#include "lint.h"
#include <ctype.h>
#include <stdlib.h>
#include <sys/localedef.h>
#include <widec.h>
#include "libc_i18n.h"

/* returns number of bytes read by (*f)() */
int
__mbftowc_euc(_LC_charmap_t *hdl, char *s, wchar_t *wchar,
    int (*f)(void), int *peekc)
{
	int length;
	wchar_t intcode;
	int c;
	char *olds = s;
	wchar_t mask;

	if ((c = (*f)()) < 0)
		return (0);
	*s++ = (char)c;
	if (isascii(c)) {
		*wchar = c;
		return (1);
	}
	intcode = 0;
	if (c == SS2) {
		if ((length = hdl->cm_eucinfo->euc_bytelen2) == 0)
			goto lab1;
		mask = WCHAR_CS2;
		goto lab2;
	} else if (c == SS3) {
		if ((length = hdl->cm_eucinfo->euc_bytelen3) == 0)
			goto lab1;
		mask = WCHAR_CS3;
		goto lab2;
	}

lab1:
	/* checking C1 characters */
	if IS_C1(c) {
		*wchar = c;
		return (1);
	}
	mask = WCHAR_CS1;
	length = hdl->cm_eucinfo->euc_bytelen1 - 1;
	intcode = c & WCHAR_S_MASK;
lab2:
	if (length < 0)
		return (-1);

	while (length--) {
		*s++ = c = (*f)();
		if (isascii(c) || IS_C1(c)) { /* Illegal EUC sequence. */
			if (c >= 0)
				*peekc = c;
			--s;
			return (-((int)(s - olds)));
		}
		intcode = (intcode << WCHAR_SHIFT) | (c & WCHAR_S_MASK);
	}
	*wchar = intcode | mask;
	return ((int)(s - olds));
}

int
_mbftowc(char *s, wchar_t *wchar, int (*f)(void), int *peekc)
{
	return (METHOD(__lc_charmap, mbftowc)
	    (__lc_charmap, s, wchar, f, peekc));
}
