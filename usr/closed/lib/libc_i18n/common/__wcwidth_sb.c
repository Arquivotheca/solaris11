/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * static char sccsid[] = "@(#)62	1.2.1.2  src/bos/usr/ccs/lib/libc/"
 * "__wcwidth_latin.c, bos, bos410 1/12/93 11:12:11";
 */
/*
 * COMPONENT_NAME: (LIBCCPPC) LIBC Code-Point/Process-Code Conversion Functions
 *
 * FUNCTIONS: __wcwidth_sb
 *
 * ORIGINS: 27
 *
 * This module contains IBM CONFIDENTIAL code. -- (IBM
 * Confidential Restricted when combined with the aggregated
 * modules for this product)
 * OBJECT CODE ONLY SOURCE MATERIALS
 * (C) COPYRIGHT International Business Machines Corp. 1991 , 1992
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 *
 */

#include "lint.h"
#include <wchar.h>
#include <sys/localedef.h>
#include <sys/types.h>
#include <ctype.h>

/*
 * User method of the wcwidth() function for the single-byte locale
 */
/* ARGSUSED */	/* hdl not used here, but needed for interface, don't remove */
int
__wcwidth_sb(_LC_charmap_t *hdl, wchar_t wc)
{
	/*
	 * if wc is null, return 0
	 */
	if (wc == L'\0')
		return (0);
	if (METHOD(__lc_ctype, iswctype)(__lc_ctype, wc, _ISPRINT) == 0)
		return (-1);

	/*
	 * single-display width
	 */
	return (1);
}

/*
 * User method of the wcwidth() function for the C locale
 */
/* ARGSUSED */	/* hdl not used here, but needed for interface, don't remove */
int
__wcwidth_C(_LC_charmap_t *hdl, wchar_t wc)
{
	/*
	 * if wc is null, return 0
	 */
	if (wc == L'\0')
		return (0);
	if ((uint32_t)wc > 255U || isprint(wc) == 0)
		return (-1);

	/*
	 * single-display width
	 */
	return (1);
}
