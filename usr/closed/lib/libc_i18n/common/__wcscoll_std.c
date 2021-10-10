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
#include <alloca.h>
#include <stdlib.h>
#include <errno.h>
#include "coll_local.h"

static int __wcscoll(_LC_collate_t *hdl,
    const wchar_t *str1, const wchar_t *str2, int bc);

int
__wcscoll_std(_LC_collate_t *hdl, const wchar_t *str1, const wchar_t *str2)
{
	return (__wcscoll(hdl, str1, str2, 0));
}

int
__wcscoll_bc(_LC_collate_t *hdl, const wchar_t *str1, const wchar_t *str2)
{
	return (__wcscoll(hdl, str1, str2, CCF_BC));
}

static int
__wcscoll(_LC_collate_t *hdl, const wchar_t *str1, const wchar_t *str2, int bc)
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
	if (*str1 == L'\0' && *str2 == L'\0')
		return (0);

	/* save the current errno */
	save_errno = errno;
	err = 0;

	coll_locale_init(&cl, hdl);

	if ((cl.flag & (CLF_EXTINFO|CLF_SIMPLE)) == 0) {
		/*
		 * We are dealing with old locale object, and the locale
		 * object wasn't the simple one. So, we can't do this with
		 * wide chars, but need to convert string into mbstring and
		 * run strcoll.
		 */
		coll_cookie_init(&cc1, &cl, bc|CCF_CONVMB);
		coll_cookie_init(&cc2, &cl, bc|CCF_CONVMB);

		cc1.data.wstr = str1;
		cc2.data.wstr = str2;

		if (coll_conv_input(&cc1) == NULL)
			goto out;
		if (coll_conv_input(&cc2) == NULL)
			goto out;

		rc = METHOD_NATIVE(hdl, strcoll)(hdl,
		    cc1.data.str, cc2.data.str);

		if (errno != save_errno)
			err = errno;
	} else {
		coll_cookie_init(&cc1, &cl, bc|CCF_WIDE);
		coll_cookie_init(&cc2, &cl, bc|CCF_WIDE);

		cc1.data.wstr = str1;
		cc2.data.wstr = str2;

		for (order = 0; order <= (int)hdl->co_nord; order++) {
			rc = coll_compare_wc(&cc1, &cc2, order);
			if (rc != 0)
				break;
		}
	}
out:
	if (cc1.co.error != 0) {
		err = cc1.co.error;
		rc = wcscmp(str1, str2);
	}
	if (cc2.co.error != 0) {
		err = cc2.co.error;
		rc = wcscmp(str1, str2);
	}

	coll_cookie_fini(&cc1);
	coll_cookie_fini(&cc2);

	errno = (err != 0 ? err : save_errno);
	return (rc);
}
