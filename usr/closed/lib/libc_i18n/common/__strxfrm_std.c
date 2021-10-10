/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

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

size_t
#ifdef _SB_COLL
__strxfrm_sb(_LC_collate_t *hdl, char *str_out, const char *str_in, size_t n)
#else
__strxfrm_std(_LC_collate_t *hdl, char *str_out, const char *str_in, size_t n)
#endif
{
	int	order;		/* current order being collated */
	size_t	len, nbpw;
	coll_locale_t cl;
	coll_cookie_t cc;
	coll_output_t *co;
	int	rc, err;
	int	save_errno = errno;

	if (str_out == NULL)
		n = 0;

	coll_locale_init(&cl, hdl);
#ifndef _SB_COLL
	/*
	 * When we deal with multibyte locale, cost to convert mb to wc isn't
	 * cheap, and more worse, we need to do the conversion for every
	 * orders for all characters. To avoid this, we convert string to
	 * wide char at first, and then do the weight conversion with wchar
	 * since new locale object can handle all with wide chars.
	 */
	if ((cl.flag & (CLF_EXTINFO|CLF_SIMPLE)) != 0) {
		/*
		 * We have either new locale object, or old locale object
		 * which doesn't have subs/collel table. So we can go with
		 * wide.
		 */
		coll_cookie_init(&cc, &cl, CCF_CONVWC);
		cc.data.str = str_in;

		if (coll_conv_input(&cc) == NULL)
			return ((size_t)-1);
	} else
#endif
	{
		coll_cookie_init(&cc, &cl, CCF_MBSTR);
		cc.data.str = str_in;
	}

	nbpw = coll_wgt_width(&cl);
	co = &cc.co;

	if (n == 0)
		co->count_only = 1;

	len = 0;
	for (order = 0; order <= (int)hdl->co_nord; order++) {
		coll_output_clean(co);
#ifdef _SB_COLL
		rc = coll_str2weight_sb(&cc, order);
#else
		if (cc.flag & CCF_WIDE)
			rc = coll_wstr2weight(&cc, order);
		else
			rc = coll_str2weight_std(&cc, order);
#endif
		if (rc != 0) {
			len = (size_t)-1;
			break;
		}
		/*
		 * Format weights per sort order.
		 */
		if (coll_format_collate(co, hdl->co_sort[order]) != 0) {
			len = (size_t)-1;
			break;
		}
		/*
		 * When similar strings are compared, let's say "aaaa" and
		 * "aaa", colllating weights will look like:
		 * string	weights (P=primary, S=sencodary weight)
		 * "aaaa"	PPPPSSSS
		 * "aaa"	PPPSSS
		 * When these weigts are compared using strcmp(), "aaa" would
		 * become greater if S > P. This is wrong. To avoid this,
		 * we add col_min (M) at the end of each orders, so that any
		 * weights if exists are larger than the end of string.
		 *	"aaaa"	PPPPMSSSSM
		 *	"aaa"	PPPMSSSM
		 */
		if (coll_output_add(co, hdl->co_col_min) != 0) {
			len = (size_t)-1;
			break;
		}
		len += coll_store_weight(nbpw, str_out, len, n, co, B_FALSE);
	}
	err = co->error;

	coll_cookie_fini(&cc);

	errno = (err != 0 ? err : save_errno);
	return (len);
}
