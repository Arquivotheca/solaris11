/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "lint.h"
#include <sys/localedef.h>
#include <widec.h>

/*
 * User method of the wcswidth() function for the default EUC locale
 */
int
__wcswidth_euc(_LC_charmap_t *hdl, const wchar_t *wcs, size_t n)
{
	int	col = 0;
	uint32_t	uwc;

	while (n != 0 && (uwc = (uint32_t)*wcs) != 0) {
		if (METHOD(__lc_ctype, iswctype)
		    (__lc_ctype, (wint_t)uwc, _ISPRINT) == 0)
			return (-1);
		switch (wcsetno(uwc)) {
		case 0:
			col += 1;
			break;
		case 1:
			col += hdl->cm_eucinfo->euc_scrlen1;
			break;
		case 2:
			col += hdl->cm_eucinfo->euc_scrlen2;
			break;
		case 3:
			col += hdl->cm_eucinfo->euc_scrlen3;
			break;
		}
		wcs++;
		n--;
	}
	return (col);
}
