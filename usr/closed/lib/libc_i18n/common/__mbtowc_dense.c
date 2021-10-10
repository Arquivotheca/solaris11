/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/


#include "lint.h"
#include "file64.h"
#include <stdlib.h>
#include <sys/localedef.h>
#include <errno.h>
#include <widec.h>
#include "mse.h"
#include "libc_i18n.h"

#define	ERR_RETURN	errno = EILSEQ; return (-1)

/*
 * Generic EUC version of mbtowc which produces Dense process code
 */
int
__mbtowc_dense(_LC_charmap_t *hdl, wchar_t *wchar, const char *s, size_t n)
{
	int		c;
	int		length;
	const char	*olds = s;
	_LC_euc_info_t	*eucinfo;

	if (s == (const char *) 0)
		return (0);
	if (n == 0)
		return (-1);

	c = (unsigned char)*s++;

	/*
	 * Optimization for a single-byte codeset.
	 *
	 * if ASCII or MB_CUR_MAX == 1 (i.e., single-byte codeset),
	 * store it to *wchar.
	 */
	if (isascii(c) || (hdl->cm_mb_cur_max == 1)) {
		if (n == 0)
			return (-1);
		if (wchar)
			*wchar = (wchar_t)c;
		return (c ? 1 : 0);
	}

	eucinfo = hdl->cm_eucinfo;

	if (wchar) { /* Do the real converion. */
		if (c == SS2) {
			if (n < (size_t)(eucinfo->euc_bytelen2 + 1))
				return (-1);
			if (eucinfo->euc_bytelen2 == 1) {
				c = (unsigned char) *s;
				if (! IS_VALID(c)) {
					ERR_RETURN;
				}
				*wchar = (wchar_t)(STRIP(c)
						    + eucinfo->cs2_base);
				return (2);
			} else if (eucinfo->euc_bytelen2 == 2) {
				int c2;

				c = (unsigned char) *s++;
				if (! IS_VALID(c)) {
					ERR_RETURN;
				}
				c2 = (unsigned char) *s;
				if (! IS_VALID(c2)) {
					ERR_RETURN;
				}
				*wchar = (wchar_t)(COMPOSE(STRIP(c), STRIP(c2))
						    + eucinfo->cs2_base);
				return (3);
			} else if (eucinfo->euc_bytelen2 == 3) {
				int	c2, c3;

				c = (unsigned char) *s++;
				if (! IS_VALID(c)) {
					ERR_RETURN;
				}
				c2 = (unsigned char) *s++;
				if (! IS_VALID(c2)) {
					ERR_RETURN;
				}
				c = COMPOSE(STRIP(c), STRIP(c2));
				c3 = (unsigned char) *s;
				if (! IS_VALID(c3)) {
					ERR_RETURN;
				}
				*wchar = (wchar_t)(COMPOSE(c, STRIP(c3))
						    + eucinfo->cs2_base);
				return (4);
			} else {
				int	c2;

				if ((length = eucinfo->euc_bytelen2) == 0) {
					*wchar = (wchar_t)c;
					return (1);
				}
				c = 0;
				while (length--) {
					c2 = (unsigned char) *s++;
					if (! IS_VALID(c2)) {
						ERR_RETURN;
					}
					c = COMPOSE(c, STRIP(c2));
				}
				*wchar = (wchar_t)(c + eucinfo->cs2_base);
				return ((int)(s - olds));
			}
		} else if (c == SS3) {
			if (n < (size_t)(eucinfo->euc_bytelen3 + 1))
				return (-1);
			if (eucinfo->euc_bytelen3 == 1) {
				c = (unsigned char) *s;
				if (! IS_VALID(c)) {
					ERR_RETURN;
				}
				*wchar = (wchar_t)(STRIP(c)
						    + eucinfo->cs3_base);
				return (2);
			} else if (eucinfo->euc_bytelen3 == 2) {
				int c2;

				c = (unsigned char) *s++;
				if (! IS_VALID(c)) {
					ERR_RETURN;
				}
				c2 = (unsigned char) *s;
				if (! IS_VALID(c2)) {
					ERR_RETURN;
				}
				*wchar = (wchar_t)(COMPOSE(STRIP(c), STRIP(c2))
						    + eucinfo->cs3_base);
				return (3);
			} else {
				int	c2;

				if ((length = eucinfo->euc_bytelen3) == 0) {
					*wchar = (wchar_t)c;
					return (1);
				}
				c = 0;
				while (length--) {
					c2 = (unsigned char) *s++;
					if (! IS_VALID(c2)) {
						ERR_RETURN;
					}
					c = COMPOSE(c, STRIP(c2));
				}
				*wchar = (wchar_t)(c + eucinfo->cs3_base);
				return ((int)(s - olds));
			}
		}

		if (IS_C1(c)) {
			*wchar = c;
			return (1);
		}
		if (n < (size_t)eucinfo->euc_bytelen1)
			return (-1);
		if (eucinfo->euc_bytelen1 == 2) {
			int	c2;

			c2 = (unsigned char) *s;
			if (! IS_VALID(c2)) {
				ERR_RETURN;
			}
			*wchar = (wchar_t)(COMPOSE(STRIP(c), STRIP(c2))
					    + eucinfo->cs1_base);
			return (2);
		} else {
			int	c2;

			length = eucinfo->euc_bytelen1;
			c = 0;
			while (length--) {
				c2 = (unsigned char) *s++;
				if (! IS_VALID(c2)) {
					ERR_RETURN;
				}
				c = COMPOSE(c, STRIP(c2));
			}
			*wchar = (wchar_t)(c + eucinfo->cs1_base);
			return ((int)(s - olds));
		}
	} else { /* wchar==0; just calculate the length of the multibyte char */
		/*
		 * Note that mbtowc(0, s, n) does more than looking at the
		 * first byte of s.  It returns the length of s only
		 * if s can be really converted to a valid wchar_t value.
		 * It returns -1 otherwise.
		 */
		if (c == SS2) {
			if ((length = eucinfo->euc_bytelen2) == 0)
				goto lab3;
			goto lab4;
		} else if (c == SS3) {
			if ((length = eucinfo->euc_bytelen3) == 0)
				goto lab3;
			goto lab4;
		}
lab3:
		if (IS_C1(c)) {
			return (1);
		}
		length = eucinfo->euc_bytelen1 - 1;
lab4:
		if ((length + 1) > n || length < 0)
			return (-1);
		while (length--) {
			c = (unsigned char)*s++;
			if (! IS_VALID(c)) {
				ERR_RETURN;
			}
		}
		return ((int)(s - olds));
	}
	/*NOTREACHED*/
}
