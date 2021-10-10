/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * static char sccsid[] = "@(#)69	1.3.1.2  "
 * "src/bos/usr/ccs/lib/libc/regfree.c, bos, bos410 1/12/93 11:18:54";
 */
/*
 * COMPONENT_NAME: (LIBCPAT) Standard C Library Pattern Functions
 *
 * FUNCTIONS: regfree
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
 * FUNCTION:  regfree()
 *
 * DESCRIPTION: release preg memory malloc'd by regcomp()
 *              invoke appropriate method for this locale
 */

#pragma weak _regfree = regfree

#include "lint.h"
#include <sys/localedef.h>
#include <regex.h>

void
regfree(regex_t *preg)
{
	METHOD(__lc_collate, regfree)(__lc_collate, preg);
}
