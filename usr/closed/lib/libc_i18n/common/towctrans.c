/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * COPYRIGHT NOTICE
 *
 * This source code is designated as Restricted Confidential Information
 * and is subject to special restrictions in a confidential disclosure
 * agreement between HP, IBM, SUN, NOVELL and OSF.  Do not distribute
 * this source code outside your company without OSF's specific written
 * approval.  This source code, and all copies and derivative works
 * thereof, must be returned or destroyed at request. You must retain
 * this notice on any copies which you make.
 *
 * (c) Copyright 1990, 1991, 1992, 1993 OPEN SOFTWARE FOUNDATION, INC.
 * ALL RIGHTS RESERVED
 */
/*
 * OSF/1 1.2
 */
/*
 * COMPONENT_NAME: (LIBCCHR) LIBC Character Classification Funcions
 *
 * FUNCTIONS: towlower
 * FUNCTIONS: towupper
 * FUNCTIONS: towctrans
 *
 *
 * (C) COPYRIGHT International Business Machines Corp. 1991
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 *
 * 1.2  com/lib/c/chr/towlower.c, libcchr, bos320, 9130320 7/17/91 15:16:55
 * 1.2  com/lib/c/chr/towupper.c, libcchr, bos320, 9130320 7/17/91 15:17:24
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "lint.h"
#include <ctype.h>
#include <wchar.h>
#include <sys/localedef.h>
#include "libc.h"

#undef towlower
#undef towupper

#pragma weak _towlower = towlower
#pragma weak _towupper = towupper

#define	_TOUPPER_INDEX	1
#define	_TOLOWER_INDEX	2

/*
 * The following code depends on the fact that the localedef command
 * generates the transformation table of toupper for the 2nd entry (index:1)
 * of _LC_transnm_t and _LC_transtabs_t and the transformation table of
 * tolower for the 3rd entry (index:2) of _LC_transnm_t and _LC_transtabs_t.
 * The 1st entry (index:0) won't be used.
 */

static wint_t
__towctrans_local(const _LC_transtabs_t *tabs, uint32_t uwc)
{
	while (tabs) {
		if (uwc >= (uint32_t)tabs->tmin) {
			if (uwc <= (uint32_t)tabs->tmax) {
				/* wc belongs to this sub-table */
				return (tabs->table[uwc - tabs->tmin]);
			}
			/*
			 * wc is larger than the range of
			 * this sub-table.
			 * so, continues to scan other tables
			 */
			tabs = tabs->next;
		} else {
			/*
			 * wc is smaller than the minimum code
			 * of this sub-table, which means
			 * this wc is not contained in
			 * this transformation table
			 */
			return ((wint_t)uwc);
		}
	}
	return ((wint_t)uwc);
}

wint_t
__towlower_std(_LC_ctype_t *hdl, wint_t wc)
{
	const _LC_transtabs_t	*tabs;
	uint32_t	uwc = (uint32_t)wc;

	tabs = hdl->transtabs + _TOLOWER_INDEX;

	if (uwc <= (uint32_t)tabs->tmax) {
		/*
		 * minimum value of the top sub-table of
		 * tolower is always 0
		 */
		return (tabs->table[uwc]);
	}
	if (uwc > (uint32_t)hdl->transname[_TOLOWER_INDEX].tmax) {
		/*
		 * wc is larger than the maximum code
		 * of tolower table.
		 */
		return ((wint_t)uwc);
	}
	return (__towctrans_local(tabs->next, uwc));
}

wint_t
__towupper_std(_LC_ctype_t *hdl, wint_t wc)
{
	const _LC_transtabs_t	*tabs;
	uint32_t	uwc = (uint32_t)wc;

	tabs = hdl->transtabs + _TOUPPER_INDEX;

	if (uwc <= (uint32_t)tabs->tmax) {
		/*
		 * minimum value of the top sub-table of
		 * toupper is always 0
		 */
		return (tabs->table[uwc]);
	}
	if (uwc > (uint32_t)hdl->transname[_TOUPPER_INDEX].tmax) {
		/*
		 * wc is larger than the manimum code
		 * of toupper table.
		 */
		return ((wint_t)uwc);
	}
	return (__towctrans_local(tabs->next, uwc));
}

wint_t
__towctrans_std(_LC_ctype_t *hdl, wint_t wc, wctrans_t ind)
{
	const _LC_transtabs_t	*tabs;
	uint32_t	uwc = (uint32_t)wc;

	if (ind == 0)
		return ((wint_t)uwc);

	if (uwc < (uint32_t)hdl->transname[ind].tmin ||
	    uwc > (uint32_t)hdl->transname[ind].tmax) {
		/*
		 * wc is out of the range of this transfomration
		 * talbe.
		 */
		return ((wint_t)uwc);
	}
	tabs = hdl->transtabs + hdl->transname[ind].index;
	return (__towctrans_local(tabs, uwc));
}

wint_t
towlower(wint_t wc)
{
	return (METHOD(__lc_ctype, towlower)(__lc_ctype, wc));
}

wint_t
towupper(wint_t wc)
{
	return (METHOD(__lc_ctype, towupper)(__lc_ctype, wc));
}

wint_t
towctrans(wint_t wc, wctrans_t index)
{
	return (METHOD(__lc_ctype, towctrans)(__lc_ctype, wc, index));
}
