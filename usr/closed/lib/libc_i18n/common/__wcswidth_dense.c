/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "lint.h"
#include <stdlib.h>
#include <ctype.h>
#include <sys/localedef.h>

/*
 * Native method of the wcswidth() function for the default EUC locale
 */
int
__wcswidth_dense(_LC_charmap_t *hdl, const wchar_t *wcs, size_t n)
{
	int	col = 0;
	size_t	cur_max;
	uint32_t	uwc;
	uint32_t	base_cs3, base_cs1, end_dense;
	int	scrlen1, scrlen2, scrlen3;

	cur_max = hdl->cm_mb_cur_max;
	scrlen1 = hdl->cm_eucinfo->euc_scrlen1;
	scrlen2 = hdl->cm_eucinfo->euc_scrlen2;
	scrlen3 = hdl->cm_eucinfo->euc_scrlen3;
	end_dense = (uint32_t)hdl->cm_eucinfo->dense_end;
	base_cs1 = (uint32_t)hdl->cm_eucinfo->cs1_base;
	base_cs3 = (uint32_t)hdl->cm_eucinfo->cs3_base;

	while (n != 0 && (uwc = (uint32_t)*wcs) != 0) {
		if (METHOD_NATIVE(__lc_ctype, iswctype)
		    (__lc_ctype, (wint_t)uwc, _ISPRINT) == 0) {
			return (-1);
		}
		if (uwc < 128U) {
			col++;
		} else if (uwc < 256U) {
			if (cur_max == 1) {
				col++;
			} else {
				return (-1);
			}
		} else if (uwc < base_cs3) {
			col += scrlen2;
		} else if (uwc < base_cs1) {
			col += scrlen3;
		} else if (uwc < end_dense) {
			col += scrlen1;
		} else {
			return (-1);
		}
		wcs++;
		n--;
	}
	return (col);
}
