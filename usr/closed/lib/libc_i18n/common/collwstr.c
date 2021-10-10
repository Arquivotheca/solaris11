/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 *   (C) COPYRIGHT International Business Machines Corp. 1991, 1992
 *   All Rights Reserved
 *   Licensed Materials - Property of IBM
 *   US Government Users Restricted Rights - Use, duplication or
 *   disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 */

#include "lint.h"
#include "libc_i18n.h"
#include <sys/types.h>
#include <sys/localedef.h>
#include <errno.h>
#include "coll_local.h"

static int substr(coll_locale_t *, const wchar_t *, int,
    coll_output_t *);

int
coll_wstr2weight(coll_cookie_t *cc, int order)
{
	coll_locale_t	*loc = cc->loc;
	_LC_collate_t *hdl = loc->hdl;
	_LC_weight_t coltbl = hdl->co_coltbl[order];
	wchar_t	wc_min = hdl->co_wc_min;
	wchar_t	wc_max = hdl->co_wc_max;
	_LC_charmap_t *cmapp = hdl->cmapp;
	coll_output_t	*op = &cc->co;
	const wchar_t	*str = cc->data.wstr;
	wchar_t	wt, wc;
	int	rc, subhit, bc;
	const char *smap, *wmap;

	if (cc->flag & CCF_SIMPLE) {
		if (cc->flag & CCF_BC) {
			while (*str != L'\0') {
				wc = *str++;
				if ((wc = _eucpctowc(cmapp, wc)) == WEOF)
					return (-1);
				if (wc > wc_max || wc < wc_min) {
					op->error = EINVAL;
					wc &= 0x7f;
				}
				wt = coltbl[wc];
				if (coll_output_add(op, wt) != 0)
					return (-1);
			}
		} else {
			while (*str != L'\0') {
				wc = *str++;
				if (wc > wc_max || wc < wc_min) {
					op->error = EINVAL;
					wc &= 0x7f;
				}
				wt = coltbl[wc];
				if (coll_output_add(op, wt) != 0)
					return (-1);
			}
		}
		return (0);
	}

	bc = (cc->flag & CCF_BC);
	smap = loc->submap;
	wmap = loc->wgtstrmap;

	subhit = 0;

	while (*str != L'\0') {
		wc = *str;
		if (bc) {
			if ((wc = _eucpctowc(cmapp, wc)) == WEOF)
				return (-1);
		}
		if (wc > wc_max || wc < wc_min) {
			op->error = EINVAL;
			wc &= 0x7f;
		}

		if (smap != NULL && (smap[wc] & 0x01)) {
			subhit = 1;
			rc = substr(loc, str, order, op);
			if (rc != 0) {
				if (rc < 0)
					return (-1);
				str += rc;
				continue;
			}
		}
		str++;

		wt = coltbl[wc];

		if (wmap != NULL && (wmap[wc] & 0x10)) {
			const wchar_t *wsub;
			int idx;

			subhit = 1;
			if ((idx = WGTSTR_IDX(wt)) > 0) {
				wsub = loc->wgtstr + idx;
				idx = *wsub++;
				while (idx--) {
					wt = *wsub++;
					if (coll_output_add(op, wt) != 0)
						return (-1);
				}
				continue;
			}
		}
		if (coll_output_add(op, wt) != 0)
			return (-1);
	}

	if (order == 0 && subhit == 0)
		cc->flag |= CCF_SIMPLE;

	return (0);
}

int
coll_wchr2weight(coll_cookie_t *cc, const wchar_t *str, int order)
{
	coll_locale_t *loc = cc->loc;
	_LC_collate_t *hdl = loc->hdl;
	coll_output_t *op = &cc->co;
	wchar_t wc, wt;
	int	rc;

	wc = *str;
	if (cc->flag & CCF_BC) {
		if ((wc = _eucpctowc(hdl->cmapp, wc)) == WEOF)
			return (-1);
	}
	if (wc > hdl->co_wc_max || wc < hdl->co_wc_min) {
		op->error = EINVAL;
		wc &= 0x7f;
	}

	if (loc->submap != NULL && (loc->submap[wc] & 0x01)) {
		rc = substr(loc, str, order, op);
		if (rc != 0)
			return (rc);
	}

	wt = hdl->co_coltbl[order][wc];

	if (loc->wgtstrmap != NULL && (loc->wgtstrmap[wc] & 0x10)) {
		const wchar_t *wsub;
		int idx;

		if ((idx = WGTSTR_IDX(wt)) > 0) {
			wsub = loc->wgtstr + idx;
			idx = *wsub++;
			while (idx--) {
				wt = *wsub++;
				if (coll_output_add(op, wt) != 0)
					return (-1);
			}
			return (1);
		}
	}

	if (coll_output_add(op, wt) != 0)
		return (-1);

	return (1);
}

/*
 * Handle many-to-many mapping
 */
static int
substr(coll_locale_t *loc, const wchar_t *input,
    int order, coll_output_t *op)
{
	int	r, len;
	int	l, m, h;
	const wchar_t *src, *tgt;
	wchar_t wt;
	const _LC_exsubs_t *nsubs, *tsubs, *ensubs;

	r = loc->extinfo->ext_hsuboff[order];
	len = loc->extinfo->ext_hsubsz[order];
	nsubs = loc->extinfo->ext_hwsubs + r;
	ensubs = nsubs + len;

	if (len > 8) {
		l = 0;
		h = len - 1;
		while (l <= h) {
			m = (l + h) / 2;
			src = nsubs[m].ess_src.wp;
			if ((r = *input - *src) == 0) {
				len = nsubs[m].ess_srclen;
				if ((r = wcsncmp(input, src, len)) == 0)
					break;
			}
			if (r < 0)
				h = m - 1;
			else if (r > 0)
				l = m + 1;
		}
		if (l > h)
			return (0);
		nsubs += m;
	} else {
		for (; nsubs != ensubs; nsubs++) {
			src = nsubs->ess_src.wp;
			if ((r = *input - *src) == 0) {
				len = nsubs->ess_srclen;
				if ((r = wcsncmp(input, src, len)) == 0)
					break;
			}
			if (r < 0)
				return (0);
		}
		if (nsubs == ensubs)
			return (0);
	}

	/*
	 * We will check the table forward to see if there is a longer match.
	 */
	for (tsubs = nsubs + 1; tsubs != ensubs; tsubs++) {
		src = tsubs->ess_src.wp;
		len = tsubs->ess_srclen;
		if (*input != *src || wcsncmp(input, src, len) != 0) {
			nsubs = tsubs - 1;
			break;
		}
	}

	tgt = loc->wgtstr + nsubs->ess_wgt.wgtidx;
	/* 1st wchar is the length of weights */
	len = *tgt++;
	while (len--) {
		wt = *tgt++;
		if (coll_output_add(op, wt) != 0)
			return (-1);
	}
	return (nsubs->ess_srclen);
}
