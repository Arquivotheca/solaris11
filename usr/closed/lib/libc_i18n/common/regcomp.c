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
 * static char rcsid[] = "@(#)$RCSfile: regcomp.c,v $ $Revision: 1.3.4.3 $ "
 * "(OSF) $Date: 1992/12/03 18:40:55 $";
 * #endif
 */
/*
 * COMPONENT_NAME: (LIBCPAT) Standard C Library Pattern Functions
 *
 * FUNCTIONS: regcomp
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
 * 1.2  com/lib/c/pat/regcomp.c, libcpat, bos320, 9130320 7/17/91 15:26:09
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * FUNCTION: regcomp()
 *
 * DESCRIPTION: compile Regular Expression for use by regexec()
 *	        invoke appropriate method for this locale.
 */

#pragma weak _regcomp = regcomp

#include "lint.h"
#include <sys/localedef.h>
#include <regex.h>

int
regcomp(regex_t *preg, const char *pattern, int cflags)
{
	return (METHOD(__lc_collate, regcomp)
	    (__lc_collate, preg, pattern, cflags));
}
