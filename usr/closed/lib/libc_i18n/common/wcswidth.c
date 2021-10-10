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
 * static char rcsid[] = "@(#)$RCSfile: wcswidth.c,v $ "
 * "$Revision: 1.3.4.2 $ (OSF) $Date: 1992/11/19 15:19:51 $";
 * #endif
 */

/*
 * COMPONENT_NAME: (LIBCCCPPC) LIBC Code-Point/Process-Code Conversion Functions
 *
 * FUNCTIONS: wcswidth
 *
 *
 * (C) COPYRIGHT International Business Machines Corp. 1991
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 *
 * 1.2  com/lib/c/cppc/wcswidth.c, libccppc, 9130320 7/17/91 15:12:32
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#pragma weak _wcswidth = wcswidth

#include "lint.h"
#include <wchar.h>
#include <sys/localedef.h>
#include <sys/types.h>

int
wcswidth(const wchar_t *wcs, size_t n)
{
	return (METHOD(__lc_charmap, wcswidth)(__lc_charmap, wcs, n));
}
