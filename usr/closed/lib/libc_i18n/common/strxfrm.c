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
 * static char rcsid[] = "@(#)$RCSfile: strxfrm.c,v $ $Revision: 1.8.2.5 "
 *	"$ (OSF) $Date: 1992/02/20 23:05:40 $";
 * #endif
 */
/*
 * COMPONENT_NAME: (LIBCSTR) Standard C Library String Handling Functions
 *
 * FUNCTIONS: strxfrm
 *
 *
 * (C) COPYRIGHT International Business Machines Corp. 1989
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 *
 * 1.18  com/lib/c/str/strxfrm.c, libcstr, bos320,9130320 7/17/91 15:06:35
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#pragma weak _strxfrm = strxfrm

#include "lint.h"
#include <sys/types.h>
#include <sys/localedef.h>
#include <string.h>

size_t
strxfrm(char *s1, const char *s2, size_t n)
{
	return (METHOD(__lc_collate, strxfrm)(__lc_collate, s1, s2, n));
}

/* ARGSUSED */	/* *coll required by interface, don't remove */
size_t
__strxfrm_C(_LC_collate_t *coll, char *s1, const char *ts2, size_t n)
{
	size_t	len, nn;
	const char	*s2 = ts2;

	if (n == 0) {
		return (strlen(s2));
	}

	n--;
	nn = n >> 2;
	for (len = 0; len < nn; len++) {
		if ((*s1 = *s2) == '\0')
			return (len * 4);
		if ((*(s1 + 1) = *(s2 + 1)) == '\0')
			return (len * 4 + 1);
		if ((*(s1 + 2) = *(s2 + 2)) == '\0')
			return (len * 4 + 2);
		if ((*(s1 + 3) = *(s2 + 3)) == '\0')
			return (len * 4 + 3);
		s1 += 4;
		s2 += 4;
	}

	len *= 4;
	switch (n - len) {
	case 3:
		if ((*s1++ = *s2++) == '\0')
			return (n - 3);
		/* FALLTHROUGH */
		len++;
	case 2:
		if ((*s1++ = *s2++) == '\0')
			return (n - 2);
		/* FALLTHROUGH */
		len++;
	case 1:
		if ((*s1++ = *s2++) == '\0')
			return (n - 1);
		/* FALLTHROUGH */
		len++;
	case 0:
		*s1 = '\0';
	}

	len += strlen(s2);

	return (len);
}
