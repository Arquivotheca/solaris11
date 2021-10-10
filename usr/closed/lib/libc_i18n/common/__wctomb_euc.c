/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "lint.h"
#include <sys/localedef.h>
#include <widec.h>

int
__wctomb_euc(_LC_charmap_t *hdl, char *s, wchar_t wchar)
{
	char *olds = s;
	int size, index;
	unsigned char d;

	if (!s)
		return (0);
	if ((wchar & ~0xff) == 0) { /* ASCII or control code. */
		*s++ = (char)wchar;
		return (1);
	}
	switch (wchar & WCHAR_CSMASK) {
	case WCHAR_CS1:
		size = hdl->cm_eucinfo->euc_bytelen1;
		break;

	case WCHAR_CS2:
		*s++ = (char)SS2;
		size = hdl->cm_eucinfo->euc_bytelen2;
		break;

	case WCHAR_CS3:
		*s++ = (char)SS3;
		size = hdl->cm_eucinfo->euc_bytelen3;
		break;

	default:
		return (-1);
	}
	if ((index = size) <= 0)
		return (-1);
	while (index--) {
		d = wchar | 0200;
		wchar >>= WCHAR_SHIFT;
		if (iscntrl(d))
			return (-1);
		s[index] = d;
	}
	return ((int)(s + size - olds));
}
