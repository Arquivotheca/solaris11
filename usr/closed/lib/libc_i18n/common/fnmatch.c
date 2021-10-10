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
 * static char rcsid[] = "@(#)$RCSfile: fnmatch.c,v $ $Revision: 1.3.4.5 "
 *	"$ (OSF) $Date: 1992/11/05 21:58:53 $";
 * #endif
 */

/*
 * COMPONENT_NAME: (LIBCPAT) Standard C Library Pattern Functions
 *
 * FUNCTIONS: fnmatch
 *
 *
 *
 * (C) COPYRIGHT International Business Machines Corp. 1991
 * All Rights Reserved
 * Licensed Materials - Property of IBM
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 *
 * 1.2  com/lib/c/pat/fnmatch.c, libcpat, bos320, 9130320 7/17/91 15:24:47
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#pragma weak _fnmatch = fnmatch

#include "lint.h"
#include <sys/localedef.h>
#include <fnmatch.h>
#include "libc_i18n.h"

int
fnmatch(const char *ppat, const char *string, int flags)
{
	return (METHOD(__lc_collate, fnmatch)
	    (__lc_collate, ppat, string, string, flags));
}
