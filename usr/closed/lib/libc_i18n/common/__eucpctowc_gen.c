/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "lint.h"
#include <stdlib.h>
#include <widec.h>
#include <sys/localedef.h>

/*
 * Generic EUC version of EUC process code to Dense process code conversion.
 */
wchar_t
__eucpctowc_gen(_LC_charmap_t *hdl, wchar_t eucpc)
{
	_LC_euc_info_t	*eucinfo;
	uint32_t	nwc;
	uint32_t	ueucpc = (uint32_t)eucpc;

	switch (ueucpc & WCHAR_CSMASK) {
	case WCHAR_CS1:
		eucinfo = hdl->cm_eucinfo;
		nwc = ueucpc + eucinfo->cs1_adjustment;
		if (nwc < eucinfo->cs1_base || nwc > eucinfo->dense_end)
			return ((wchar_t)WEOF);
		return ((wchar_t)nwc);

	case WCHAR_CS2:
		eucinfo = hdl->cm_eucinfo;
		nwc = ueucpc + eucinfo->cs2_adjustment;
		if (nwc < eucinfo->cs2_base || nwc >= eucinfo->cs3_base)
			return ((wchar_t)WEOF);
		return ((wchar_t)nwc);

	case WCHAR_CS3:
		eucinfo = hdl->cm_eucinfo;
		nwc = ueucpc + eucinfo->cs3_adjustment;
		if (nwc < eucinfo->cs3_base || nwc >= eucinfo->cs1_base)
			return ((wchar_t)WEOF);
		return ((wchar_t)nwc);
	}

	/*
	 * It is assumed that the caller uses the _eucpctowc() macro
	 * to invoke this function. The macro checks ASCII/C1 characters
	 * and if the given character is an ASCII/C1 character, it
	 * doesn't call this function. This is why this function
	 * checks ASCII or C1 at the last.
	 */
	if (ueucpc <= 0x9f)
		return ((wchar_t)ueucpc);
	return ((wchar_t)WEOF);
}
