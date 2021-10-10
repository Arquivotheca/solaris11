/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*	Copyright (c) 1986 AT&T	*/
/*	  All Rights Reserved  	*/

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Fputwc transforms the wide character c into the multibyte character,
 * and writes it onto the output stream "iop".
 */

#pragma weak _fputwc = fputwc
#pragma weak _putwc = putwc

#include "lint.h"
#include "file64.h"
#include "mse_int.h"
#include "mtlib.h"
#include <stdlib.h>
#include <sys/localedef.h>
#include <stdio.h>
#include <wchar.h>
#include <limits.h>
#include <errno.h>
#include "stdiom.h"
#include "mse.h"
#include "libc_i18n.h"

wint_t
fputwc(wint_t wc, FILE *iop)
{
	char mbs[MB_LEN_MAX];
	unsigned char *p;
	int n;
	rmutex_t	*lk;

	if (wc == WEOF)
		return (WEOF);
	n = METHOD(__lc_charmap, wctomb)(__lc_charmap, mbs, (wchar_t)wc);
	if (n <= 0)
		return (WEOF);
	p = (unsigned char *)mbs;
	FLOCKFILE(lk, iop);
	while (n--) {
		if (PUTC((*p++), iop) == EOF) {
			FUNLOCKFILE(lk);
			return (WEOF);
		}
	}
	FUNLOCKFILE(lk);
	return (wc);
}

wint_t
putwc(wint_t wc, FILE *iop)
{
	return (fputwc(wc, iop));
}
