/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * #if !defined(lint) && !defined(_NOIDENT)
 * static char sccsid[] = "@(#)68	1.4.2.2  "
 *	"src/bos/usr/ccs/lib/libc/colval.c, bos, bos410 1/12/93 11:12:54";
 *
 * #endif
 */
/*
 * COMPONENT_NAME: (LIBCSTR) Standard C Library String Handling Functions
 *
 * FUNCTIONS: _mbucoll
 *
 * ORIGINS: 27
 *
 * IBM CONFIDENTIAL -- (IBM Confidential Restricted when
 * combined with the aggregated modules for this product)
 * OBJECT CODE ONLY SOURCE MATERIALS
 * (C) COPYRIGHT International Business Machines Corp. 1991,1992
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 */

#include "lint.h"
#include <sys/types.h>
#include <limits.h>
#include <sys/localedef.h>
#include "coll_local.h"

/* _mbucoll - determine unique collating weight of collating symbol. */

wchar_t
_mbucoll(_LC_collate_t *hdl, char *str, char **next_char)
{
	int	rc;
	int	r_ord_idx = hdl->co_nord + hdl->co_r_order;
	coll_locale_t cl;
	coll_cookie_t cc;
	wchar_t	wt;

	coll_locale_init(&cl, hdl);
	coll_cookie_init(&cc, &cl, CCF_MBSTR);

	coll_output_clean(&cc.co);
	if ((rc = coll_chr2weight_std(&cc, str, r_ord_idx)) == -1) {
		coll_cookie_fini(&cc);
		return ((wchar_t)-1);
	}
	wt = cc.co.out[0];
	coll_cookie_fini(&cc);

	*next_char = str + rc;
	return (wt);
}
