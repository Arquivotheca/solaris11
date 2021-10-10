/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * static char sccsid[] = "@(#)54	1.2.1.2  src/bos/usr/ccs/lib/libc/
 * "__wcswidth_latin.c, bos, bos410 1/12/93 11:11:53";
 */
/*
 * COMPONENT_NAME: (LIBCCPPC) LIBC Code-Point/Process-Code Conversion Functions
 *
 * FUNCTIONS: __wcswidth_sb
 * FUNCTIONS: __wcswidth_C
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
#include <sys/localedef.h>
#include <sys/types.h>
#include "libc.h"

/*
 * returns the number of characters for a SINGLE-BYTE codeset
 */
/*ARGSUSED*/	/* hdl not used here, but needed for interface, don't remove */
int
__wcswidth_sb(_LC_charmap_t *hdl, const wchar_t *pwcs, size_t n)
{
	int	len;
	if (pwcs == NULL)
		return (0);
	for (len = 0; len < n && pwcs[len] != L'\0'; len++) {
		if (METHOD(__lc_ctype, iswctype)
		    (__lc_ctype, pwcs[len], _ISPRINT) == 0)
			return (-1);
	}
	return (len);
}

/*
 * returns the number of characters for the C locale
 */
/*ARGSUSED*/	/* hdl not used here, but needed for interface, don't remove */
int
__wcswidth_C(_LC_charmap_t *hdl, const wchar_t *pwcs, size_t n)
{
	int	len;
	if (pwcs == NULL)
		return (0);
	for (len = 0; len < n && pwcs[len] != L'\0'; len++) {
		if ((uint32_t)pwcs[len] > 255U || isprint(pwcs[len]) == 0)
			return (-1);
	}
	return (len);
}
