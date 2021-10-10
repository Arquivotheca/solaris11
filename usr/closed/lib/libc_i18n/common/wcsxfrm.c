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
 *
 * #if !defined(lint) && !defined(_NOIDENT)
 * static char rcsid[] = "@(#)$RCSfile: wcsxfrm.c,v $ $Revision: 1.3.2.5 "
 *	"$ (OSF) $Date: 1992/02/20 23:08:58 $";
 * #endif
 */

/*
 * COMPONENT_NAME: (LIBCSTR) Standard C Library String Handling Functions
 *
 * FUNCTIONS: wcsxfrm
 *
 *
 * (C) COPYRIGHT International Business Machines Corp. 1989
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 *
 * 1.2  com/lib/c/str/wcsxfrm.c, libcstr, bos320, 9130320 7/17/91 15:07:05
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#pragma weak _wcsxfrm = wcsxfrm

#include "lint.h"
#include <sys/localedef.h>
#include <wchar.h>

size_t
wcsxfrm(wchar_t *ws1, const wchar_t *ws2, size_t n)
{
	return (METHOD(__lc_collate, wcsxfrm)(__lc_collate, ws1, ws2, n));
}

/* ARGSUSED */
size_t
__wcsxfrm_C(_LC_collate_t *coll, wchar_t *ws1, const wchar_t *ws2, size_t n)
{
	size_t	len, nn;
	const wchar_t	*pws2;

	len = 0;
	if (n == 0)
		goto calc_length;

	n--;
	nn = n >> 2;
	for (; len < nn; len++) {
		if ((*ws1 = *ws2) == L'\0')
			return (len * 4);
		if ((*(ws1 + 1) = *(ws2 + 1)) == L'\0')
			return (len * 4 + 1);
		if ((*(ws1 + 2) = *(ws2 + 2)) == L'\0')
			return (len * 4 + 2);
		if ((*(ws1 + 3) = *(ws2 + 3)) == L'\0')
			return (len * 4 + 3);
		ws1 += 4;
		ws2 += 4;
	}

	len *= 4;
	switch (n - len) {
	case 3:
		if ((*ws1++ = *ws2++) == L'\0')
			return (n - 3);
		/* FALLTHROUGH */
		len++;
	case 2:
		if ((*ws1++ = *ws2++) == L'\0')
			return (n - 2);
		/* FALLTHROUGH */
		len++;
	case 1:
		if ((*ws1++ = *ws2++) == L'\0')
			return (n - 1);
		/* FALLTHROUGH */
		len++;
	case 0:
		*ws1 = L'\0';
	}

calc_length:
	pws2 = ws2;
	for (; ; len += 4) {
		if (*pws2 == L'\0')
			return (len);
		if (*(pws2+1) == L'\0')
			return (len+1);
		if (*(pws2+2) == L'\0')
			return (len+2);
		if (*(pws2+3) == L'\0')
			return (len+3);
		pws2 += 4;
	}
	/* NOTREACHED */
}
