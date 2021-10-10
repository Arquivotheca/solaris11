/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/


#include "lint.h"
#include "file64.h"
#include <sys/localedef.h>
#include <errno.h>
#include <widec.h>
#include "mse.h"
#include "libc_i18n.h"

int
__mbtowc_euc(_LC_charmap_t *hdl, wchar_t *wchar, const char *s, size_t n)
{
	int		length;
	int		c;
	const char	*olds = s;

	if (s == NULL)
		return (0);
	if (n == 0)
		return (-1);
	c = (unsigned char)*s++;

	if (wchar) { /* Do the real converion. */
		register wchar_t intcode;
		wchar_t mask;

		if (isascii(c)) {
			*wchar = c;
			return (c ? 1 : 0);
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
		length = hdl->cm_eucinfo->euc_bytelen1 - 1;
		mask = WCHAR_CS1;
		intcode = c & WCHAR_S_MASK;
lab2:
		if (length + 1 > n || length < 0)
			return (-1);
		while (length--) {
			c = (unsigned char)*s++;
			if (isascii(c) || IS_C1(c)) {
				errno = EILSEQ; /* AT&T doesn't set this. */
				return (-1); /* Illegal EUC sequence. */
			}
			intcode = (intcode << WCHAR_SHIFT) | (c & WCHAR_S_MASK);
		}
		*wchar = intcode | mask;
	} else { /* wchar==0; just calculate the length of the multibyte char */
		/*
		 * Note that mbtowc(0, s, n) does more than looking at the
		 * first byte of s.  It returns the length of s only
		 * if s can be really converted to a valid wchar_t value.
		 * It returns -1 otherwise.
		 */
		if (isascii(c)) {
			return (c ? 1 : 0);
		}
		if (c == SS2) {
			if ((length = hdl->cm_eucinfo->euc_bytelen2) == 0)
				goto lab3;
			goto lab4;
		} else if (c == SS3) {
			if ((length = hdl->cm_eucinfo->euc_bytelen3) == 0)
				goto lab3;
			goto lab4;
		}
lab3:
		/* checking C1 characters */
		if IS_C1(c) {
			return (1);
		}
		length = hdl->cm_eucinfo->euc_bytelen1 - 1;
lab4:
		if (length + 1 > n || length < 0)
			return (-1);
		while (length--) {
			c = (unsigned char)*s++;
			if (isascii(c) || IS_C1(c)) {
				errno = EILSEQ; /* AT&T doesn't set this. */
				return (-1); /* Illegal EUC sequence. */
			}
		}
	}
	return ((int)(s - olds));
}
