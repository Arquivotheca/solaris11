/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "lint.h"
#include "file64.h"
#include <errno.h>
#include <widec.h>
#include <sys/localedef.h>
#include "libc.h"
#include "mse.h"

size_t
__wcrtomb_euc(_LC_charmap_t *hdl, char *s, wchar_t wc, mbstate_t *ps)
{
	char *olds = s;
	int size, index;
	unsigned char d;

	MBSTATE_RESTART(ps);

	if (s == (char *)NULL)
		return (1);

	if ((wc & ~0xff) == 0) { /* ASCII or control code. */
		*s++ = (char)wc;
		return (1);
	}
	switch (wc & WCHAR_CSMASK) {
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
		errno = EILSEQ;
		return ((size_t)-1);
	}
	if ((index = size) <= 0) {
		errno = EILSEQ;
		return ((size_t)-1);
	}
	while (index--) {
		d = wc | 0200;
		wc >>= WCHAR_SHIFT;
		if (iscntrl(d)) {
			errno = EILSEQ;
			return ((size_t)-1);
		}
		s[index] = d;
	}
	return ((size_t)(s + size - olds));
}
