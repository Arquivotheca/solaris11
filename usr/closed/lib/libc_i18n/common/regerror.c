/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * static char sccsid[] = "@(#)65	1.3.1.2  "
 * "src/bos/usr/ccs/lib/libc/regerror.c, bos, bos410 1/12/93 11:18:47";
 */
/*
 * COMPONENT_NAME: (LIBCPAT) Standard C Library Pattern Functions
 *
 * FUNCTIONS: regerror
 *
 * ORIGINS: 27
 *
 * This module contains IBM CONFIDENTIAL code. -- (IBM
 * Confidential Restricted when combined with the aggregated
 * modules for this product)
 * OBJECT CODE ONLY SOURCE MATERIALS
 *
 * (C) COPYRIGHT International Business Machines Corp. 1991, 1992
 * All Rights Reserved
 * Licensed Materials - Property of IBM
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * FUNCTION: regerror()
 *
 * DESCRIPTION: fetch message text of regcomp() or regexec() error
 *	        invoke appropriate method for this locale.
 */

#pragma weak _regerror = regerror

#include "lint.h"
#include <sys/localedef.h>
#include <regex.h>

size_t
regerror(int errcode, const regex_t *preg, char *errbuf, size_t errbuf_size)
{
	return (METHOD(__lc_collate, regerror)
	    (__lc_collate, errcode, preg, errbuf, errbuf_size));
}
