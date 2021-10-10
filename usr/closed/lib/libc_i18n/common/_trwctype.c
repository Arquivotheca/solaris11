/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T		*/
/*	All Rights Reserved					*/
/*								*/

#include "lint.h"
#include <stdlib.h>
#include <wctype.h>
#include <sys/localedef.h>
#include "libc.h"

static const char *
_lc_get_ctype_flag_name(_LC_ctype_t *hdl, _LC_bind_tag_t tag,
    _LC_bind_value_t value)
{
	int	i;

	for (i = 0; i < hdl->nbinds; i++) {
		if ((tag == hdl->bindtab[i].bindtag) &&
		    (value == hdl->bindtab[i].bindvalue)) {
			return (hdl->bindtab[i].bindname);
		}
	}

	return (NULL);
}

wchar_t
__trwctype_std(_LC_ctype_t *hdl, wchar_t wc, int mask)
{
	const char	*name;
	wctrans_t	transtype;

	if (wc == WEOF || (uint32_t)wc < 0x9f) {
		return (wc);
	} else {
		name = _lc_get_ctype_flag_name(hdl,
		    (_LC_bind_tag_t)_LC_TAG_TRANS,
		    (_LC_bind_value_t)(intptr_t)mask);
		if (name == NULL)
			return (wc);

		transtype = wctrans(name);
		if (transtype == 0)
			return (wc);

		return (towctrans(wc, transtype));
	}

}

wchar_t
_trwctype(wchar_t wc, int mask)
{
	return (METHOD(__lc_ctype, _trwctype)(__lc_ctype, wc, mask));
}
