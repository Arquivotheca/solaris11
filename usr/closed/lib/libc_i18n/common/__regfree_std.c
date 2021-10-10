/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * static char sccsid[] = "@(#)44	1.2.1.1 "
 * "src/bos/usr/ccs/lib/libc/__regfree_std.c, bos, bos410 5/25/92 14:04:23";
 */
/*
 * COMPONENT_NAME: (LIBCPAT) Standard C Library Pattern Functions
 *
 * FUNCTIONS: __regfree_std
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

#include "lint.h"
#include <sys/localedef.h>
#include "reglocal.h"
#include <regex.h>
#include <stdlib.h>

/*
 * __regfree_std() - Free Compiled RE Pattern Dynamic Memory
 */
/* ARGSUSED */
void
__regfree_std(_LC_collate_t *hdl, regex_t *preg)
{
	if (preg->re_comp) {
		free(preg->re_comp);
		preg->re_comp = NULL;
	}
	if (preg->re_sc) {
		if (preg->re_sc->re_lsub)
			free(preg->re_sc->re_lsub);
		if (preg->re_sc->re_esub)
			free(preg->re_sc->re_esub);
		free(preg->re_sc);
		preg->re_sc = NULL;
	}
}
