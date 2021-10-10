/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "lint.h"
#include <sys/localedef.h>
#include <wchar.h>
#include <widec.h>

int
__wctob_euc(_LC_charmap_t *hdl, wint_t c)
{
	unsigned char	d;

	/*
	 * The specified 'c' is a user wide-char representation.
	 *
	 * In EUC-compatible locale, a C1 control character
	 * is mapped to a wide-char between 0x00000080 and 0x0000009f.
	 * And a single-byte character between 0xa0 and 0xff is mapped
	 * to a wide-char between 0x10000020 and 0x1000007f, respectively.
	 * However, due to a historical implementation,
	 * wctomb() for wide-char value between 0x000000a0 and
	 * 0x000000ff will return a single-byte character between 0xa0
	 * and 0xff, respectively.  But, for example, the wide-char
	 * value 0x000000a0 is not a valid wide-char corresponding
	 * to a singly-byte character 0xa0.  Therefore, to make sure
	 * that a wide-char value corresponds to a valid single-byte
	 * character, needs to perform the round-trip test between
	 * wctomb() and mbtowc().  That is, __wctob_euc() implements
	 * the right way rather than following the historical one.
	 */
	if (isascii(c)) {
		/* ASCII */
		return ((int)c);
	} else if (c == SS2) {
		if (hdl->cm_eucinfo->euc_bytelen2 != 0) {
			/* having codeset 2 */
			return (EOF);
		}
		return ((int)SS2);
	} else if (c == SS3) {
		if (hdl->cm_eucinfo->euc_bytelen3 != 0) {
			/* having codeset 3 */
			return (EOF);
		}
		return ((int)SS3);
	} else if (c >= 0x80 && c <= 0x9f) {
		/* C1 control */
		return ((int)c);
	}

	if ((c & ~0x7f) == WCHAR_CS1) {
		if (hdl->cm_eucinfo->euc_bytelen1 == 1) {
			/* single-byte codeset 1 */
			d = c | 0200;
			if (d >= 0x80 && d <= 0x9f) {
				/*
				 * C1 control won't be mapped
				 * to the codeset 1 area.  So,
				 * Wide chars between 0x10000000
				 * and 0x1000001f are invalid.
				 */
				return (EOF);
			}
			return ((int)d);
		}
	}
	/* multibyte */
	return (EOF);
}
