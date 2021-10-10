/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 *
 * FUNCTION: __btowc_euc
 *
 */

#include "lint.h"
#include <sys/localedef.h>
#include <wchar.h>
#include <widec.h>

wint_t
__btowc_euc(_LC_charmap_t *hdl, int c)
{
	wchar_t	intcode;

	if (isascii(c)) {
		/* ASCII char */
		return ((wint_t)c);
	} else if (c == SS2) {
		if (hdl->cm_eucinfo->euc_bytelen2 != 0) {
			/* having codeset 2 */
			return (WEOF);
		}
		/* C1 */
		return ((wint_t)c);
	} else if (c == SS3) {
		if (hdl->cm_eucinfo->euc_bytelen3 != 0) {
			/* having codeset 3 */
			return (WEOF);
		}
		/* C1 */
		return ((wint_t)c);
	} else if (c >= 0x80 && c <= 0x9f) {
		/* C1 control */
		return ((wint_t)c);
	}

	if ((c & ~0xff) == 0) {	/* 0xa0 <= c <= 0xff */
		/* this character belongs to the codeset 1 */
		if (hdl->cm_eucinfo->euc_bytelen1 == 1) {
			/*
			 * will return the user wide-char representation.
			 */
			intcode = (c & WCHAR_S_MASK) | WCHAR_CS1;
			return ((wint_t)intcode);
		}
	}
	/* EOF case is also handled here */
	return (WEOF);
}
