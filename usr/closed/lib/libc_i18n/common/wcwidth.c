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
 * static char rcsid[] = "@(#)$RCSfile: wcwidth.c,v $ "
 * "$Revision: 1.3.4.3 $ (OSF) $Date: 1992/11/30 17:30:40 $";
 * #endif
 */

/*
 * COMPONENT_NAME: (LIBCCCPPC) LIBC Code-Point/Process-Code Conversion Functions
 *
 * FUNCTIONS: wcwidth
 *
 *
 * (C) COPYRIGHT International Business Machines Corp. 1991
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 *
 * 1.2  com/lib/c/cppc/wcwidth.c, libccppc, 9130320 7/17/91 15:13:27
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#pragma weak _wcwidth = wcwidth

#include "lint.h"
#include <wchar.h>
#include <sys/localedef.h>
#include <sys/types.h>

int
wcwidth(wchar_t wc)
{
	return (METHOD(__lc_charmap, wcwidth)(__lc_charmap, wc));
}
