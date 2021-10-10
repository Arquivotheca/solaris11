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
 * static char rcsid[] = "@(#)$RCSfile: mbtowc.c,v $ "
 * "$Revision: 1.6.2.8 $ (OSF) $Date: 1992/03/30 02:40:48 $";
 * #endif
 */

/*
 * COMPONENT_NAME: (LIBCCCPPC) LIBC Code-Point/Process-Code Conversion Functions
 *
 * FUNCTIONS: mbtowc
 *
 *
 * (C) COPYRIGHT International Business Machines Corp. 1991
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 *
 * 1.2  com/lib/c/cppc/mbtowc.c, libccppc, 9130320 7/17/91 15:14:28
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#pragma weak _mbtowc = mbtowc

#include "lint.h"
#include <stdlib.h>
#include <sys/localedef.h>
#include <sys/types.h>

int
mbtowc(wchar_t *pwc, const char *s, size_t len)
{
	return (METHOD(__lc_charmap, mbtowc)(__lc_charmap, pwc, s, len));
}
