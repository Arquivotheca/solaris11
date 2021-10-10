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
__wctob_dense(_LC_charmap_t *hdl, wint_t c)
{
	if (isascii(c)) {
		/* ASCII */
		return ((int)c);
	} else if (c == SS2) {
		if (hdl->cm_eucinfo->euc_bytelen2 != 0) {
			/* having codeset 2 */
			return (EOF);
		}
		/* C1 */
		return ((int)SS2);
	} else if (c == SS3) {
		if (hdl->cm_eucinfo->euc_bytelen3 != 0) {
			/* having codeset 3 */
			return (EOF);
		}
		/* C1 */
		return ((int)SS3);
	} else if (c >= 0x80 && c <= 0x9f) {
		/* C1 control */
		return ((int)c);
	}

	if ((c & ~0xff) == 0) {
		if (hdl->cm_eucinfo->euc_bytelen1 == 1) {
			return ((int)c);
		}
	}
	return (EOF);
}
