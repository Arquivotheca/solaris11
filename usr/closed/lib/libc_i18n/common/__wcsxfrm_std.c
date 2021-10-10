/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * COMPONENT_NAME: (LIBCSTR) Standard C Library String Handling Functions
 *
 * FUNCTIONS:	__wcsxfrm_std
 *		__wcsxfrm_bc
 *
 * ORIGINS: 27
 *
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
#include <limits.h>
#include <wchar.h>
#include <sys/types.h>
#include <sys/localedef.h>
#include <alloca.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/byteorder.h>
#include "coll_local.h"

static size_t __wcsxfrm(_LC_collate_t *hdl,
    wchar_t *str_out, const wchar_t *str_in, size_t n, int fl);
static size_t adjust_tail(wchar_t *str_out, size_t max, size_t len);
static size_t get_adjusted_weight_width(coll_locale_t *cl);

size_t
__wcsxfrm_std(_LC_collate_t *hdl,
    wchar_t *str_out, const wchar_t *str_in, size_t n)
{
	return (__wcsxfrm(hdl, str_out, str_in, n, 0));
}

size_t
__wcsxfrm_bc(_LC_collate_t *hdl,
    wchar_t *str_out, const wchar_t *str_in, size_t n)
{
	return (__wcsxfrm(hdl, str_out, str_in, n, CCF_BC));
}

static size_t
__wcsxfrm(_LC_collate_t *hdl,
    wchar_t *str_out, const wchar_t *str_in, size_t n, int fl)
{
	int	order;		/* current order being collated */
	coll_locale_t cl;
	coll_cookie_t cc;
	coll_output_t *co;
	size_t	len, nbpw;
	int	err = 0;
	int	save_errno = errno;

	if (str_out == NULL)
		n = 0;
	n *= sizeof (wchar_t);

	coll_locale_init(&cl, hdl);

	/*
	 * If we got an old locale object, and if it wasn't simple, we
	 * need to convert to mbstring and run strxfrm.
	 */
	if ((cl.flag & (CLF_EXTINFO|CLF_SIMPLE)) == 0)
		fl |= CCF_CONVMB;
	else
		fl |= CCF_WIDE;
	coll_cookie_init(&cc, &cl, fl);

	cc.data.wstr = str_in;

	/*
	 * Convert input if needed.
	 */
	if (coll_conv_input(&cc) == NULL) {
		coll_cookie_fini(&cc);
		return ((size_t)-1);
	}

	nbpw = get_adjusted_weight_width(&cl);

	if ((cl.flag & (CLF_EXTINFO|CLF_SIMPLE)) == 0) {
		_LC_collate_t shdl;
		size_t	asz;

		/*
		 * We create a fake _LC_collate_t, which will temporarily flag
		 * the width of weights. This is necessary because strxfrm()
		 * can choose arbitrary weight-width which may not be suitable
		 * for wcsxfrm() (ie produces negative wchar_t).
		 */
		(void) memcpy(&shdl, hdl, sizeof (_LC_collate_t));
		asz = (hdl->co_nord + 1) * sizeof (wchar_t);
		shdl.co_sort = alloca(asz);
		(void) memcpy(shdl.co_sort, hdl->co_sort, asz);

		/* nbpw can be either 2 or 4 */
		if (nbpw == 2)
			shdl.co_sort[0] |= _COLL_WGT_WIDTH2;
		else
			shdl.co_sort[0] |= _COLL_WGT_WIDTH4;

		len = METHOD_NATIVE(hdl, strxfrm)(&shdl, (char *)str_out,
		    cc.data.str, n);
		if (errno != save_errno)
			err = errno;

		if (len != (size_t)-1)
			len = adjust_tail(str_out, n, len);

	} else {
		co = &cc.co;
		len = 0;

		if (n == 0)
			co->count_only = 1;

		for (order = 0; order <= (int)hdl->co_nord; order++) {
			coll_output_clean(co);

			if (coll_wstr2weight(&cc, order) != 0) {
				len = (size_t)-1;
				break;
			}
			if (coll_format_collate(co,
			    hdl->co_sort[order]) != 0) {
				len = (size_t)-1;
				break;
			}
			if (coll_output_add(co, hdl->co_col_min) != 0) {
				len = (size_t)-1;
				break;
			}
			len += coll_store_weight(nbpw,
			    (char *)str_out, len, n, co, B_TRUE);
		}
		err = co->error;

		if (len != (size_t)-1) {
			if (nbpw != sizeof (wchar_t)) {
				/* We clear/fill remaining bytes in wchar_t */
				len = adjust_tail(str_out, n, len);
			} else {
				/* We don't have remaining nor swapped bytes */
				len /= sizeof (wchar_t);
			}
		}
	}

	coll_cookie_fini(&cc);

	errno = (err != 0 ? err : save_errno);
	return (len);
}

static size_t
adjust_tail(wchar_t *wsout, size_t max, size_t len)
{
	int	i, rem;
	size_t	rc;

	rem = len % sizeof (wchar_t);
	rc = len / sizeof (wchar_t);
	if (max == 0) {
		/*
		 * We are just counting. If there is remainder, we need
		 * one more.
		 */
		if (rem != 0)
			rc++;
		return (rc);
	}

	max /= sizeof (wchar_t);
	/*
	 * We have non-zero max. We need to clean remaining bytes when
	 * of course we have remainder, and also the filling word isn't the
	 * word for NULL-terminate.
	 */
	if (rem != 0 && rc < (max - 1)) {
		char *p = (char *)&wsout[rc];
		for (i = rem; i < sizeof (wchar_t); i++)
			p[i] = '\0';
		/* we've got one more complete word */
		rc++;
	}
	/* terminate appropriate position */
	if (rc >= max)
		wsout[max - 1] = L'\0';
	else
		wsout[rc] = L'\0';

#if defined(_LITTLE_ENDIAN)
	/* swap bytes for little endian machines */
	while (*wsout != L'\0') {
		wchar_t wc = *wsout;
		*wsout = BSWAP_32(wc);
		wsout++;
	}
#endif
	return (rc);
}

#if WCHAR_MAX != ((1 << 31) - 1)
#error	"Unknown size/type of wchar_t"
#endif
/*
 * Here we assume that wchar_t is a signed 4 byte integer.
 * We need to adjust nbpw carefully, because compressing collation
 * sequeuce may set the MSB of wchar_t by shifting lower bytes(>127) onto
 * the highest byte. If the MSB of wchar_t is set, wcscmp() and wmemcmp()
 * can mulfunction.
 */
static size_t
get_adjusted_weight_width(coll_locale_t *cl)
{
	_LC_collate_t *hdl = cl->hdl;
	size_t	nbpw;

	nbpw = coll_wgt_width(cl);

	switch (nbpw) {
	case 1:
		/*
		 * This is very unlikely, but if the max weight
		 * doesnt' exceed 0x0101017f, we can take the lowest
		 * byte and put it in wchar_t.
		 */
		if (cl->flag & CLF_EXTINFO) {
			const _LC_collextinfo_t *ext = cl->extinfo;

			if (ext != NULL &&
			    ext->ext_col_max != 0 &&
			    ext->ext_col_max < 0x01010180) {
				break;
			}
		}
		/*
		 * If the lower half of weight doesn't exceed 0x10ff,
		 * using lower 2 bytes will not set the MSB of wchar_t.
		 */
		nbpw = 2;
		break;
	case 2:
		/*
		 * If the lower 2 byte of weight doesn't exceed 0x7fff,
		 * we can safely use the 2 byte for weight.
		 */
		if (cl->flag & CLF_EXTINFO) {
			const _LC_collextinfo_t *ext = cl->extinfo;

			if (ext != NULL &&
			    ext->ext_col_max != 0 &&
			    ext->ext_col_max < 0x01018000) {
				break;
			}
		}
		/*
		 * If r_order is set, co_col_max is the max weight of the
		 * relative order. So we can't trust it.
		 */
		if (hdl->co_r_order == 0 && hdl->co_col_max < 0x01018000)
			break;
		/*FALLTHRU*/
	default:
		/* Otherwise, we must use full word */
		nbpw = 4; /* sizeof (wchar_t) */
		break;
	}
	return (nbpw);
}
