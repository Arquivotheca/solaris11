/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "lint.h"
#include "file64.h"
#include <sys/localedef.h>
#include <widec.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include "mse.h"

#define	IS_C1(c) (((c) >= 0x80) && ((c) <= 0x9f))

size_t
__mbrtowc_euc(_LC_charmap_t *hdl, wchar_t *wchar, const char *s, size_t n,
		mbstate_t *ps)
{
	int		length;
	int		c;
	const char	*olds;		/* ptr to beginning of input string */
	const char	*strp;
	char		str[MB_LEN_MAX];
	int		n2;		/* number of bytes pointed to by strp */
	size_t	nc;

	if (s == (const char *) NULL) {
		s = "";
		n = 1;
		wchar = (wchar_t *)NULL;
	}
	/*
	 * zero bytes contribute to an incomplete, but
	 * potentially valid character
	 */
	if (n == 0)
		return ((size_t)-2);

	/*
	 * If previous call was partial read of a multibyte
	 * character, recover previously read bytes into str array,
	 * then concatenate the input string.
	 */
	nc = __mbst_get_nconsumed(ps);
	if (nc > 0) {
		size_t ntemp;

		(void) __mbst_get_consumed_array(ps, str, 0, nc);
		ntemp = (n <= (MB_CUR_MAX - nc)) ? n : (MB_CUR_MAX - nc);
		(void) memcpy(str + nc, s, ntemp);
		strp = str;
		olds = str + nc;
		n2 = nc + ntemp;
	} else {
		strp = s;
		olds = s;
		n2 = n;
	}

	c = (unsigned char)*strp++;
	n2--;

	if (wchar) { /* Do the real converion. */
		wchar_t intcode;
		wchar_t mask;

		if (isascii(c)) {
			*wchar = c;
			MBSTATE_RESTART(ps);
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
			MBSTATE_RESTART(ps);
			return (1);
		}
		length = hdl->cm_eucinfo->euc_bytelen1 - 1;
		mask = WCHAR_CS1;
		intcode = c & WCHAR_S_MASK;
lab2:
		if (length < 0) {
			MBSTATE_RESTART(ps);
			errno = EILSEQ;
			return ((size_t)-1);
		}

		for (; (length != 0) && (n2 != 0); length--, n2--) {
			c = (unsigned char)*strp++;
			if (isascii(c) || IS_C1(c)) {
				MBSTATE_RESTART(ps);
				errno = EILSEQ; /* AT&T doesn't set this. */
				return ((size_t)-1); /* Illegal EUC sequence. */
			}
			intcode = (intcode << WCHAR_SHIFT) | (c & WCHAR_S_MASK);
		}

		/*
		 * Encontered a partial multibyte character;
		 * Append the input string to mbstate_t buffer
		 */
		if (length > n2) {
			(void) __mbst_set_consumed_array(ps,
			    (const char *)s, nc, n);
			__mbst_set_nconsumed(ps, (char)(nc + n));
			return ((size_t)-2);
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
			MBSTATE_RESTART(ps);
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
			MBSTATE_RESTART(ps);
			return (1);
		}
		length = hdl->cm_eucinfo->euc_bytelen1 - 1;
lab4:
		if (length < 0) {
			MBSTATE_RESTART(ps);
			errno = EILSEQ;
			return ((size_t)-1);
		}

		for (; (length != 0) && (n2 != 0); length--, n2--) {
			c = (unsigned char)*strp++;
			if (isascii(c) || IS_C1(c)) {
				MBSTATE_RESTART(ps);
				errno = EILSEQ; /* AT&T doesn't set this. */
				return ((size_t)-1); /* Illegal EUC sequence. */
			}
		}

		/*
		 * Encontered a partial multibyte character;
		 * Append the input string to mbstate_t buffer
		 */
		if (length > n2) {
			(void) __mbst_set_consumed_array(ps,
			    (const char *)s, nc, n);
			__mbst_set_nconsumed(ps, (char)(nc + n));
			return ((size_t)-2);
		}
	}
	MBSTATE_RESTART(ps);
	return ((size_t)(strp - olds));
}
