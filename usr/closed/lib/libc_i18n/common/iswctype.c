/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*	Copyright (c) 1986 AT&T	*/
/*	  All Rights Reserved  	*/

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
 * static char rcsid[] = "@(#)$RCSfile: iswctype.c,v $ "
 * "$Revision: 1.1.2.2 $ (OSF) $Date: 1992/03/17 03:50:48 $";
 * #endif
 */
/*
 * COMPONENT_NAME: (LIBCCHR) LIBC Character Classification Funcions
 *
 * FUNCTIONS: iswctype
 *
 *
 * (C) COPYRIGHT International Business Machines Corp. 1991
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 *
 *  1.2  com/lib/c/chr/iswctype.c, libcchr, bos320, 9130320 7/17/91 15:16:26
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#pragma weak __iswctype = iswctype

#include "lint.h"
#include <ctype.h>
#include <wchar.h>
#include <sys/localedef.h>

int
iswctype(wint_t wc, wctype_t mask)
{
	return (METHOD(__lc_ctype, iswctype)(__lc_ctype, wc, mask));
}

unsigned int
_iswctype(wchar_t wc, int mask)
{
	if (wc == WEOF || (uint32_t)wc < 0x9f)
		return (0);
	return (iswctype(wc, mask));
}
