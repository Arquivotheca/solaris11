/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * IBM CONFIDENTIAL -- (IBM Confidential Restricted when
 * combined with the aggregated modules for this product)
 * OBJECT CODE ONLY SOURCE MATERIALS
 * (C) COPYRIGHT International Business Machines Corp. 1989,1992
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 */

#include "lint.h"
#include <sys/types.h>
#include <sys/localedef.h>
#include <string.h>
#include <errno.h>
#include "coll_local.h"

int
#ifdef _SB_COLL
__strcoll_sb(_LC_collate_t *hdl, const char *str1, const char *str2)
#else
__strcoll_std(_LC_collate_t *hdl, const char *str1, const char *str2)
#endif
{
	int	order;		/* current order being collated */
	int	save_errno;	/* save the current errno */
	coll_locale_t cl;
	coll_cookie_t cc1, cc2;
	int	rc, err;

	/* See if str1 and str2 are the same string */
	if (str1 == str2)
		return (0);

	/* If str1 and str2 are null, they are equal. */
	if (*str1 == '\0' && *str2 == '\0')
		return (0);

	/* save the current errno */
	save_errno = errno;
	err = 0;

	coll_locale_init(&cl, hdl);
	coll_cookie_init(&cc1, &cl, CCF_MBSTR);
	coll_cookie_init(&cc2, &cl, CCF_MBSTR);

	cc1.data.str = str1;
	cc2.data.str = str2;

	for (order = 0; order <= (int)hdl->co_nord; order++) {
#ifdef _SB_COLL
		rc = coll_compare_sb(&cc1, &cc2, order);
#else
		rc = coll_compare_std(&cc1, &cc2, order);
#endif
		if (rc != 0)
			break;
	}
	if (cc1.co.error != 0) {
		err = cc1.co.error;
		rc = strcmp(str1, str2);
	}
	if (cc2.co.error != 0) {
		err = cc2.co.error;
		rc = strcmp(str1, str2);
	}

	coll_cookie_fini(&cc1);
	coll_cookie_fini(&cc2);

	errno = (err != 0 ? err : save_errno);
	return (rc);
}
