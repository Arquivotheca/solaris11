/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "lint.h"
#include <stdlib.h>
#include <sys/localedef.h>
#include <widec.h>

int
__wctomb_dense(_LC_charmap_t *hdl, char *s, wchar_t wchar)
{
	char *olds = s;
	int size, index;
	unsigned char d;
	_LC_euc_info_t	*eucinfo;
	unsigned int wc = (unsigned int) wchar;

	if (!s)
		return (0);

	/*
	 * If ASCII or C1 control, just store it without any
	 * conversion.
	 */
	if (wc <= 0x9f) {
		*s = (char)wchar;
		return (1);
	}

	if (wc < 256) {
		if (hdl->cm_mb_cur_max == 1) {
			*s = (char)wchar;
			return (1);
		} else
			return (-1);
	}

	eucinfo = hdl->cm_eucinfo;

	if (eucinfo->euc_bytelen2 && (wc < eucinfo->cs3_base)) {
		*s++ = (char)SS2;
		wc -= eucinfo->cs2_adjustment;
		size = eucinfo->euc_bytelen2;
	} else if (eucinfo->euc_bytelen3 && (wc < eucinfo->cs1_base)) {
		*s++ = (char)SS3;
		wc -= eucinfo->cs3_adjustment;
		size = eucinfo->euc_bytelen3;
	} else if (eucinfo->euc_bytelen1 && (wc <= eucinfo->dense_end)) {
		wc -= eucinfo->cs1_adjustment;
		size = eucinfo->euc_bytelen1;
	} else
		return (-1);

	if ((index = size) <= 0)
		return (-1);

	while (index--) {
		d = wc | 0200;
		wc >>= WCHAR_SHIFT;
		if (iscntrl(d))
			return (-1);
		s[index] = d;
	}
	return ((int)(s + size - olds));
}
