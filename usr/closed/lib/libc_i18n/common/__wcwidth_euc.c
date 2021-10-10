/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "lint.h"
#include <sys/localedef.h>
#include <widec.h>

/*
 * User method of the wcwidth() function for the default EUC locale
 */
int
__wcwidth_euc(_LC_charmap_t *hdl, wchar_t wc)
{
	if (wc == L'\0')
		return (0);

	if (METHOD(__lc_ctype, iswctype)(__lc_ctype, wc, _ISPRINT) == 0)
		return (-1);

	switch (wcsetno(wc)) {
	case 0:
		return (1);
	case 1:
		return (hdl->cm_eucinfo->euc_scrlen1);
	case 2:
		return (hdl->cm_eucinfo->euc_scrlen2);
	case 3:
		return (hdl->cm_eucinfo->euc_scrlen3);
	}
	/* NOTREACHED */
	return (0);	/* keep gcc happy */
}
