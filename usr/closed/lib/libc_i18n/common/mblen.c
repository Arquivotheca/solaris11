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
 * static char rcsid[] = "@(#)$RCSfile: mblen.c,v $ "
 * "$Revision: 1.9.2.8 $ (OSF) $Date: 1992/03/30 02:40:42 $";
 * #endif
 */

/*
 * COMPONENT_NAME: (LIBCCCPPC) LIBC Code-Point/Process-Code Conversion Functions
 *
 * FUNCTIONS: mblen
 *
 *
 * (C) COPYRIGHT International Business Machines Corp. 1991
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 *
 * 1.2  com/lib/c/cppc/mblen.c, libccppc, 9130320 7/17/91 15:10:14
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#pragma weak _mblen = mblen

#include "lint.h"
#include <stdlib.h>
#include <sys/localedef.h>
#include <sys/types.h>

int
mblen(const char *s, size_t n)
{
	return (METHOD(__lc_charmap, mblen)(__lc_charmap, s, n));
}
