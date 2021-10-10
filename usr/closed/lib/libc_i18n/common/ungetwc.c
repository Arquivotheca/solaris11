/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*	Copyright (c) 1986 AT&T	*/
/*	  All Rights Reserved  	*/

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Ungetwc saves the process code c into the one character buffer
 * associated with an input stream "iop". That character, c,
 * will be returned by the next getwc call on that stream.
 */

#pragma weak _ungetwc = ungetwc

#include "lint.h"
#include "file64.h"
#include "mse_int.h"
#include <stdio.h>
#include <sys/localedef.h>
#include <stdlib.h>
#include <wchar.h>
#include <limits.h>
#include <errno.h>
#include "libc.h"
#include "stdiom.h"
#include "mse.h"

wint_t
ungetwc(wint_t wc, FILE *iop)
{
	char mbs[MB_LEN_MAX];
	unsigned char *p;
	int n;
	rmutex_t	*lk;

	if (wc == WEOF)
		return (WEOF);

	FLOCKFILE(lk, iop);

	if ((iop->_flag & _IOREAD) == 0) {
		FUNLOCKFILE(lk);
		return (WEOF);
	}

	n = METHOD(__lc_charmap, wctomb)(__lc_charmap, mbs, (wchar_t)wc);
	if (n <= 0) {
		FUNLOCKFILE(lk);
		return (WEOF);
	}

	if (iop->_ptr <= iop->_base) {
		if (iop->_base == NULL) {
			FUNLOCKFILE(lk);
			return (WEOF);
		}
		if (iop->_ptr == iop->_base && iop->_cnt == 0) {
			++iop->_ptr;
		} else if ((iop->_ptr - n) < (iop->_base - PUSHBACK)) {
			FUNLOCKFILE(lk);
			return (WEOF);
		}
	}

	p = (unsigned char *)(mbs+n-1); /* p points the last byte */
	while (n--) {
		*--(iop)->_ptr = (*p--);
		++(iop)->_cnt;
	}
	iop->_flag &= ~_IOEOF;
	FUNLOCKFILE(lk);
	return (wc);
}
