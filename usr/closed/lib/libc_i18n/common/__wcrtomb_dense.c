/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "lint.h"
#include "file64.h"
#include <sys/localedef.h>
#include <widec.h>
#include <errno.h>
#include "mse.h"

#define	ERR_RETURN	errno = EILSEQ; return ((size_t)-1)

size_t
__wcrtomb_dense(_LC_charmap_t *hdl, char *s, wchar_t wchar, mbstate_t *ps)
{
	char *olds = s;
	int size, index;
	unsigned char d;
	_LC_euc_info_t	*eucinfo;
	unsigned int wc = (unsigned int) wchar;

	MBSTATE_RESTART(ps);

	if (s == (char *)NULL)
		return (1);

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
		} else {
			ERR_RETURN;
		}
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
	} else {
		ERR_RETURN;
	}

	if ((index = size) <= 0) {
		ERR_RETURN;
	}

	while (index--) {
		d = wc | 0200;
		wc >>= WCHAR_SHIFT;
		if (iscntrl(d)) {
			ERR_RETURN;
		}
		s[index] = d;
	}
	return ((size_t)(s + size - olds));
}
