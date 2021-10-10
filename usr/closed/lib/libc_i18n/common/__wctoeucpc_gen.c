/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "lint.h"
#include <stdlib.h>
#include <sys/localedef.h>
#include <widec.h>

/*
 * Generic EUC version of Dense process code to EUC process code conversion
 */
wchar_t
__wctoeucpc_gen(_LC_charmap_t *hdl, wchar_t densepc)
{
	_LC_euc_info_t *eucinfo;
	uint32_t	udensepc = (uint32_t)densepc;

	/*
	 * If wc is in the single-byte range and the codeset is
	 * single-byte, convert to CS1 process code and return it. If
	 * the codeset is multi-byte, return an error. (0x80-0xFF is
	 * not used in the dense encoding for multi-byte in the
	 * generic case.)
	 */
	if (udensepc < 0x100) {
		/*
		 * It is assumed that the caller uses the _wctoeucpc() macro
		 * to invoke this function. The macro checks ASCII/C1 characters
		 * and if the given character is an ASCII/C1 character, it
		 * doesn't call this function. This is why this function
		 * checks ASCII or C1 here.
		 */
		if (udensepc <= 0x9f)
			return ((wchar_t)udensepc);
		if (hdl->cm_mb_cur_max == 1)
			return ((wchar_t)((udensepc & 0x7f) | WCHAR_CS1));
		return ((wchar_t)WEOF);
	}

	eucinfo = hdl->cm_eucinfo;

	/*
	 * CS2
	 */
	if (udensepc < eucinfo->cs3_base)
		return ((wchar_t)(udensepc - eucinfo->cs2_adjustment));

	/*
	 * CS3
	 */
	if (udensepc < eucinfo->cs1_base)
		return ((wchar_t)(udensepc - eucinfo->cs3_adjustment));

	/*
	 * CS1
	 */
	if (udensepc <= eucinfo->dense_end)
		return ((wchar_t)(udensepc - eucinfo->cs1_adjustment));

	/*
	 * Out of the range
	 */
	return ((wchar_t)WEOF);
}
