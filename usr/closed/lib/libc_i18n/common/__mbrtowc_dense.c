/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "lint.h"
#include "file64.h"
#include <stdlib.h>
#include <sys/localedef.h>
#include <widec.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include "mse.h"
#include "libc_i18n.h"

#define	ERR_RETURN	errno = EILSEQ; MBSTATE_RESTART(ps); return ((size_t)-1)


/*
 * Generic EUC version of mbtowc which produces Dense process code
 */
size_t
__mbrtowc_dense(_LC_charmap_t *hdl, wchar_t *wchar, const char *s, size_t n,
	mbstate_t *ps)
{
	int		c;
	int		length;
	_LC_euc_info_t	*eucinfo;
	const char	*olds;		/* ptr to beginning of input string */
	const char	*strp;
	char		str[MB_LEN_MAX];
	int		nbytes;		/* number of bytes pointed to by strp */
	int		i;
	size_t	nc;

	if (s == NULL) {
		s = "";
		n = 1;
		wchar = NULL;
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
		nbytes = nc + ntemp;
	} else {
		strp = s;
		olds = s;
		nbytes = n;
	}

	c = (unsigned char)*strp++;

	/*
	 * Optimization for a single-byte codeset.
	 *
	 * if ASCII or MB_CUR_MAX == 1 (i.e., single-byte codeset),
	 * store it to *wchar.
	 */
	if (isascii(c) || (hdl->cm_mb_cur_max == 1)) {
		if (wchar)
			*wchar = (wchar_t)c;
		MBSTATE_RESTART(ps);
		return (c ? 1 : 0);
	}

	eucinfo = hdl->cm_eucinfo;

	if (wchar) { /* Do the real converion. */
		if (c == SS2) {
			/*
			 * If encounter a partial multibyte character;
			 * validate bytes and append in mbstate_t buffer
			 * if OK;  else return error.
			 */
			if (nbytes < (size_t)(eucinfo->euc_bytelen2 + 1)) {
				int ntemp;

				for (i = 0; i < nbytes; i++) {
					c = (unsigned char) *strp++;
					if (! IS_VALID(c)) {
						ERR_RETURN;
					}
				}
				ntemp = nbytes - nc;
				(void) __mbst_set_consumed_array(ps,
				    (const char *)s, nc, ntemp);
				__mbst_set_nconsumed(ps, (char)(nc + ntemp));
				return ((size_t)-2);
			}
			if (eucinfo->euc_bytelen2 == 1) {
				c = (unsigned char) *strp;
				if (! IS_VALID(c)) {
					ERR_RETURN;
				}
				*wchar = (wchar_t)(STRIP(c)
				    + eucinfo->cs2_base);
				MBSTATE_RESTART(ps);
				return (2);
			} else if (eucinfo->euc_bytelen2 == 2) {
				int c2;

				c = (unsigned char) *strp++;
				if (! IS_VALID(c)) {
					ERR_RETURN;
				}
				c2 = (unsigned char) *strp;
				if (! IS_VALID(c2)) {
					ERR_RETURN;
				}
				*wchar = (wchar_t)(COMPOSE(STRIP(c), STRIP(c2))
				    + eucinfo->cs2_base);
				MBSTATE_RESTART(ps);
				return (3);
			} else if (eucinfo->euc_bytelen2 == 3) {
				int	c2, c3;

				c = (unsigned char) *strp++;
				if (! IS_VALID(c)) {
					ERR_RETURN;
				}
				c2 = (unsigned char) *strp++;
				if (! IS_VALID(c2)) {
					ERR_RETURN;
				}
				c = COMPOSE(STRIP(c), STRIP(c2));
				c3 = (unsigned char) *strp;
				if (! IS_VALID(c3)) {
					ERR_RETURN;
				}
				*wchar = (wchar_t)(COMPOSE(c, STRIP(c3))
				    + eucinfo->cs2_base);
				MBSTATE_RESTART(ps);
				return (4);
			} else {
				int	c2;

				if ((length = eucinfo->euc_bytelen2) == 0) {
					*wchar = (wchar_t)c;
					MBSTATE_RESTART(ps);
					return (1);
				}
				c = 0;
				while (length--) {
					c2 = (unsigned char) *strp++;
					if (! IS_VALID(c2)) {
						ERR_RETURN;
					}
					c = COMPOSE(c, STRIP(c2));
				}
				*wchar = (wchar_t)(c + eucinfo->cs2_base);
				MBSTATE_RESTART(ps);
				return ((size_t)(strp - olds));
			}
		} else if (c == SS3) {
			/*
			 * If encounter a partial multibyte character;
			 * validate bytes and append in mbstate_t buffer
			 * if OK;  else return error.
			 */
			if (nbytes < (size_t)(eucinfo->euc_bytelen3 + 1)) {
				int ntemp;

				for (i = 0; i < nbytes; i++) {
					c = (unsigned char) *strp++;
					if (! IS_VALID(c)) {
						ERR_RETURN;
					}
				}
				ntemp = nbytes - nc;
				(void) __mbst_set_consumed_array(ps,
				    (const char *)s, nc, ntemp);
				__mbst_set_nconsumed(ps, (char)(nc + ntemp));
				return ((size_t)-2);
			}
			if (eucinfo->euc_bytelen3 == 1) {
				c = (unsigned char) *strp;
				if (! IS_VALID(c)) {
					ERR_RETURN;
				}
				*wchar = (wchar_t)(STRIP(c)
				    + eucinfo->cs3_base);
				MBSTATE_RESTART(ps);
				return (2);
			} else if (eucinfo->euc_bytelen3 == 2) {
				int c2;

				c = (unsigned char) *strp++;
				if (! IS_VALID(c)) {
					ERR_RETURN;
				}
				c2 = (unsigned char) *strp;
				if (! IS_VALID(c2)) {
					ERR_RETURN;
				}
				*wchar = (wchar_t)(COMPOSE(STRIP(c), STRIP(c2))
				    + eucinfo->cs3_base);
				MBSTATE_RESTART(ps);
				return (3);
			} else {
				int	c2;

				if ((length = eucinfo->euc_bytelen3) == 0) {
					*wchar = (wchar_t)c;
					return (1);
				}
				c = 0;
				while (length--) {
					c2 = (unsigned char) *strp++;
					if (! IS_VALID(c2)) {
						ERR_RETURN;
					}
					c = COMPOSE(c, STRIP(c2));
				}
				*wchar = (wchar_t)(c + eucinfo->cs3_base);
				MBSTATE_RESTART(ps);
				return ((size_t)(strp - olds));
			}
		}

		if (IS_C1(c)) {
			*wchar = c;
			MBSTATE_RESTART(ps);
			return (1);
		}
		/*
		 * If encounter a partial multibyte character;
		 * validate bytes and append in mbstate_t buffer
		 * if OK;  else return error.
		 */
		if (nbytes < (size_t)(eucinfo->euc_bytelen1)) {
			int ntemp;

			for (i = 0; i < nbytes; i++) {
				c = (unsigned char) *strp++;
				if (! IS_VALID(c)) {
					ERR_RETURN;
				}
			}
			ntemp = nbytes - nc;
			(void) __mbst_set_consumed_array(ps,
			    (const char *)s, nc, ntemp);
			__mbst_set_nconsumed(ps, (char)(nc + ntemp));
			return ((size_t)-2);
		}
		if (eucinfo->euc_bytelen1 == 2) {
			int	c2;

			c2 = (unsigned char) *strp;
			if (! IS_VALID(c2)) {
				ERR_RETURN;
			}
			*wchar = (wchar_t)(COMPOSE(STRIP(c), STRIP(c2))
			    + eucinfo->cs1_base);
			MBSTATE_RESTART(ps);
			return (2);
		} else {
			int	c2;

			length = eucinfo->euc_bytelen1;
			c = 0;
			while (length--) {
				c2 = (unsigned char) *strp++;
				if (! IS_VALID(c2)) {
					ERR_RETURN;
				}
				c = COMPOSE(c, STRIP(c2));
			}
			*wchar = (wchar_t)(c + eucinfo->cs1_base);
			MBSTATE_RESTART(ps);
			return ((size_t)(strp - olds));
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
			MBSTATE_RESTART(ps);
			return (1);
		}
		length = eucinfo->euc_bytelen1 - 1;
lab4:
		if (length < 0)
			return ((size_t)-1);

		/*
		 * If encounter a partial multibyte character;
		 * validate bytes and append in mbstate_t buffer
		 * if OK;  else return error.
		 */
		if ((length + 1) > nbytes) {
			int ntemp;

			for (i = 0; i < nbytes; i++) {
				c = (unsigned char) *strp++;
				if (! IS_VALID(c)) {
					ERR_RETURN;
				}
			}
			ntemp = nbytes - nc;
			(void) __mbst_set_consumed_array(ps,
			    (const char *)s, nc, ntemp);
			__mbst_set_nconsumed(ps, (char)(nc + ntemp));
			return ((size_t)-2);
		}

		while (length--) {
			c = (unsigned char)*strp++;
			if (! IS_VALID(c)) {
				ERR_RETURN;
			}
		}
		MBSTATE_RESTART(ps);
		return ((size_t)(strp - olds));
	}
	/* NOTREACHED */
}
