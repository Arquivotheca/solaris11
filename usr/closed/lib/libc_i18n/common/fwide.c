/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "lint.h"
#include "mtlib.h"
#include "file64.h"
#include <stdio.h>
#include <sys/localedef.h>
#include <wchar.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "stdiom.h"
#include "mse.h"

int
fwide(FILE *iop, int mode)
{
	struct stat	buf;
	_IOP_orientation_t	orientation;
	mbstate_t	*mbst;
	int	ret;
	rmutex_t	*lk;

	if (iop == NULL) {
		errno = EBADF;
		return (0);
	}

	FLOCKFILE(lk, iop);
	if (fstat(FILENO(iop), &buf) == -1) {
		FUNLOCKFILE(lk);
		errno = EBADF;
		return (0);
	}

	orientation = _getorientation(iop);
	mbst = _getmbstate(iop);
	if (mbst == NULL) {
		FUNLOCKFILE(lk);
		errno = EBADF;
		return (0);
	}
	if (orientation == _NO_MODE) {
		/* Stream has not been bound */
		if (mode > 0) {
			/* Try to set Wide */
			_setorientation(iop, _WC_MODE);

			/* Set the current _LC_locale_t */
			__mbst_set_locale(mbst, (const void *)__lc_locale);
			ret = 1;
		} else if (mode < 0) {
			/* Try to set Byte */
			_setorientation(iop, _BYTE_MODE);

			/* Set the current _LC_locale_t */
			__mbst_set_locale(mbst, (const void *)__lc_locale);
			ret = -1;
		} else {
			ret = 0;
		}
	} else if (orientation == _WC_MODE) {
		ret = 1;
	} else {
		/* orientation should be _BYTE_MODE */
		ret = -1;
	}

	FUNLOCKFILE(lk);
	return (ret);
}
