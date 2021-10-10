/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#pragma weak _fgetwc = fgetwc
#pragma weak _getwc = getwc

#include "lint.h"
#include "file64.h"
#include "mse_int.h"
#include "mtlib.h"
#include <stdio.h>
#include <sys/localedef.h>
#include <widec.h>
#include <euc.h>
#include <errno.h>
#include "stdiom.h"
#include "mse.h"
#include "libc_i18n.h"

wint_t
__fgetwc_euc(_LC_charmap_t *hdl, FILE *iop)
{
	int	c, length;
	wint_t	intcode, mask;

	if ((c = GETC(iop)) == EOF)
		return (WEOF);

	if (isascii(c))		/* ASCII code */
		return ((wint_t)c);


	intcode = 0;
	if (c == SS2) {
		if ((length = hdl->cm_eucinfo->euc_bytelen2) == 0)
			goto lab1;
		mask = WCHAR_CS2;
		goto lab2;
	} else if (c == SS3) {
		if ((length = hdl->cm_eucinfo->euc_bytelen3) == 0)
			goto lab1;
		mask = WCHAR_CS3;
		goto lab2;
	}

lab1:
	if (IS_C1(c))
		return ((wint_t)c);
	length = hdl->cm_eucinfo->euc_bytelen1 - 1;
	mask = WCHAR_CS1;
	intcode = c & WCHAR_S_MASK;
lab2:
	if (length < 0)		/* codeset 1 is not defined? */
		return ((wint_t)c);
	while (length--) {
		c = GETC(iop);
		if (c == EOF || isascii(c) || (IS_C1(c))) {
			(void) UNGETC(c, iop);
			__fseterror_u(iop);
			errno = EILSEQ;
			return (WEOF); /* Illegal EUC sequence. */
		}
		intcode = (intcode << WCHAR_SHIFT) | (c & WCHAR_S_MASK);
	}
	return ((wint_t)(intcode|mask));
}

wint_t
_fgetwc_unlocked(FILE *iop)
{
	return (METHOD(__lc_charmap, fgetwc)(__lc_charmap, iop));
}

wint_t
fgetwc(FILE *iop)
{
	rmutex_t	*lk;
	wint_t result;

	FLOCKFILE(lk, iop);
	result = METHOD(__lc_charmap, fgetwc)(__lc_charmap, iop);
	FUNLOCKFILE(lk);
	return (result);
}

#undef getwc
wint_t
getwc(FILE *iop)
{
	return (fgetwc(iop));
}
