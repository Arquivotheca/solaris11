/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "lint.h"
#include <stdlib.h>
#include <ctype.h>
#include <sys/localedef.h>

/*
 * Native method of the wcwidth() function for the default EUC locale
 */
int
__wcwidth_dense(_LC_charmap_t *hdl, wint_t wc)
{
	_LC_euc_info_t	*eucinfo;
	uint32_t	uwc = (uint32_t)wc;

	if (uwc == 0)
		return (0);

	if (METHOD_NATIVE(__lc_ctype, iswctype)
	    (__lc_ctype, (wint_t)uwc, _ISPRINT) == 0) {
		return (-1);
	}

	if (uwc < 128U) {
		return (1);
	}

	if (uwc < 256U) {
		if (hdl->cm_mb_cur_max == 1) {
			return (1);
		} else {
			return (-1);
		}
	}

	eucinfo = hdl->cm_eucinfo;

	if (eucinfo->euc_bytelen2 && uwc < eucinfo->cs3_base) {
		return (eucinfo->euc_scrlen2);
	} else if (eucinfo->euc_bytelen3 && uwc < eucinfo->cs1_base) {
		return (eucinfo->euc_scrlen3);
	} else if (eucinfo->euc_bytelen1 && uwc <= eucinfo->dense_end) {
		return (eucinfo->euc_scrlen1);
	} else {
		return (-1);
	}
}
