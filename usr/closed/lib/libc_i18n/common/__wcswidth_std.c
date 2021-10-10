/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "lint.h"
#include <wchar.h>
#include <sys/localedef.h>
#include <sys/types.h>

static int
_search_width(_LC_charmap_t *hdl, uint32_t uwc)
{
	const _LC_width_range_t	*range;
	_LC_widthtabs_t	*tbl;
	int	i, idx, from, to, mid;

	tbl = hdl->cm_tbl;
	idx = hdl->cm_tbl_ent;
	for (i = 0; i < idx; i++) {
		range = tbl[i].ranges;
		from = 0;
		to = tbl[i].entries - 1;
		while (from <= to) {
			mid = (from + to) / 2;
			if (uwc < range[mid].min) {
				to = mid - 1;
			} else if (uwc > range[mid].max) {
				from = mid + 1;
			} else {
				return ((int)tbl[i].width);
			}
		}
	}
	return ((int)hdl->cm_def_width);
}

int
__wcwidth_std(_LC_charmap_t *hdl, wchar_t wc)
{
	uint32_t	uwc = (uint32_t)wc;
	/*
	 * if wc is null, return 0
	 */
	if (uwc == 0)
		return (0);

	if (METHOD_NATIVE(__lc_ctype, iswctype)
	    (__lc_ctype, (wint_t)uwc, _ISPRINT) == 0) {
		return (-1);
	}

	if (uwc <= hdl->cm_base_max) {
		/*
		 * Base area is flat
		 */
		return ((int)hdl->cm_def_width);
	}

	/* search the width table */
	return (_search_width(hdl, uwc));
}

int
__wcswidth_std(_LC_charmap_t *hdl, const wchar_t *pwcs, size_t n)
{
	int	base_max, def_width;
	int	col = 0;
	uint32_t	uwc;


	def_width = (int)hdl->cm_def_width;
	base_max = (int)hdl->cm_base_max;

	while (n != 0 && (uwc = (uint32_t)*pwcs) != 0) {
		if (METHOD_NATIVE(__lc_ctype, iswctype)
		    (__lc_ctype, (wint_t)uwc, _ISPRINT) == 0) {
			return (-1);
		}

		if (uwc <= base_max) {
			/* flat base area or no width table */
			col += def_width;
			pwcs++;
			n--;
			continue;
		}

		/* search the width table */
		col += _search_width(hdl, uwc);
		pwcs++;
		n--;
	}
	return (col);
}

int
__wcwidth_bc(_LC_charmap_t *hdl, wchar_t wc)
{
	wchar_t	nwc;

	if ((nwc = _eucpctowc(hdl, wc)) == WEOF) {
		return (-1);
	}
	return (METHOD_NATIVE(hdl, wcwidth)(hdl, nwc));
}

int
__wcswidth_bc(_LC_charmap_t *hdl, const wchar_t *pwcs, size_t n)
{
	wchar_t	wc, nwc;
	int	ret, width;

	width = 0;
	while (n != 0 && (wc = *pwcs) != 0) {
		if ((nwc = _eucpctowc(hdl, wc)) == WEOF)
			return (-1);
		ret = METHOD_NATIVE(hdl, wcwidth)(hdl, nwc);
		if (ret == -1)
			return (-1);
		width += ret;
		pwcs++;
		n--;
	}
	return (width);
}
