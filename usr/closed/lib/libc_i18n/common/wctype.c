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
 * #if !defined(lint) && !defined(_NOIDENT)
 * static char rcsid[] = "@(#)$RCSfile: wctype.c,v $ "
 * "$Revision: 1.1.4.2 $ (OSF) $Date: 1992/11/20 19:37:53 $";
 * #endif
 */

/*
 * COMPONENT_NAME: (LIBCCHR) LIBC Character Classification Funcions
 *
 * FUNCTIONS: wctype
 *
 *
 * (C) COPYRIGHT International Business Machines Corp. 1991
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 *
 * 1.2  com/lib/c/chr/wctype.c, libcchr, bos320, 9130320 7/17/91 15:15:57
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#pragma weak __wctype = wctype

#include "lint.h"
#include <wctype.h>
#include <sys/localedef.h>
#include <string.h>

wctype_t
__wctype_std(_LC_ctype_t *hdl, const char *name)
{
	int	first;
	int	mid;
	int	last;
	int	ret;

	/* look for mask value in the lc_bind table */

	first = 0;
	last = hdl->nbinds - 1;

	while (first <= last) {
		mid = (first + last) / 2;
		ret = strcmp(name, hdl->bindtab[mid].bindname);
		if (ret == 0)
			if (hdl->bindtab[mid].bindtag == _LC_TAG_CCLASS)
				return ((wctype_t)
				    (intptr_t)hdl->bindtab[mid].bindvalue);
			else
				return (0);
		else if (ret < 0)
			last = mid - 1;
		else /* ret > 0 */
			first = mid + 1;
	}

	return (0);	/* value not found */
}

wctype_t
wctype(const char *name)
{
	return (METHOD(__lc_ctype, wctype)(__lc_ctype, name));
}
