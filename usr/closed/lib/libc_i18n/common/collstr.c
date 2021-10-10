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
#include <string.h>
#include <errno.h>
#include "coll_local.h"

static int substr(coll_cookie_t *, const char *, int);
static int exsubstr(coll_locale_t *, const char *, int, coll_output_t *);
static int collel(_LC_collate_t *, int, wchar_t, const char *,
    coll_output_t *);

int
#ifdef _SB_COLL
coll_str2weight_sb(coll_cookie_t *cc, int order)
#else
coll_str2weight_std(coll_cookie_t *cc, int order)
#endif
{
	coll_locale_t	*loc = cc->loc;
	_LC_collate_t	*hdl = loc->hdl;
	const _LC_weight_t coltbl = hdl->co_coltbl[order];
#ifndef _SB_COLL
	wchar_t	wc_min = hdl->co_wc_min;
	wchar_t	wc_max = hdl->co_wc_max;
	_LC_charmap_t *cmapp = hdl->cmapp;
	int	mbcurmax = cmapp->cm_mb_cur_max;
#endif
	const _LC_collel_t **cetbl;
	const char	*str = cc->data.str;
	const char	*smap, *wmap;
	coll_output_t	*op = &cc->co;
	wchar_t	wc, wt;
	int	rc, subhit;

	if (cc->flag & CCF_SIMPLE) {
		/*
		 * We just have simple one-to-one mapping weights.
		 */
simple:
		while (*str != '\0') {
#ifdef _SB_COLL
			wc = (wchar_t)(unsigned char)*str++;
#else
			if ((rc = (METHOD_NATIVE(cmapp, mbtowc)(
			    cmapp, &wc, str, mbcurmax))) == -1) {
				op->error = EILSEQ;
				wc = (wchar_t)(unsigned char)*str++;
			} else {
				str += rc;
			}
			if (wc > wc_max || wc < wc_min) {
				op->error = EINVAL;
				wc &= 0x7f;
			}
#endif
			wt = coltbl[wc];
			if (coll_output_add(op, wt) != 0)
				return (-1);
		}
		return (0);
	}

	/*
	 * We basically do same thing as coll_chr2weight(),
	 * but rolled out here for a performance reason.
	 */
	cetbl = hdl->co_cetbl;
	smap = loc->submap;
	wmap = loc->wgtstrmap;

	subhit = 0;

	while (*str != '\0') {
		if (smap != NULL && (smap[(unsigned char)*str] & 0x01)) {
			subhit = 1;
			if ((loc->flag & CLF_EXTINFO) != 0)
				rc = exsubstr(loc, str, order, op);
			else
				rc = substr(cc, str, order);
			if (rc != 0) {
				if (rc < 0) {
					/*
					 * If we found that this is a simple,
					 * go to the simple loop.
					 */
					if (rc == -3)
						goto simple;
					/*
					 * If we found that we have no valid
					 * subs but we still have collel,
					 * then reset smap and restart.
					 */
					if (rc == -2) {
						smap = loc->submap;
						continue;
					}
					/*
					 * error occured.
					 */
					return (-1);
				}
				str += rc;
				continue;
			}
		}
#ifdef _SB_COLL
		wc = (wchar_t)(unsigned char)*str++;
#else
		if ((rc = (METHOD_NATIVE(cmapp, mbtowc)(
		    cmapp, &wc, str, mbcurmax))) == -1) {
			op->error = EILSEQ;
			wc = (wchar_t)(unsigned char)*str++;
		} else {
			str += rc;
		}
		if (wc > wc_max || wc < wc_min) {
			op->error = EINVAL;
			wc &= 0x7f;
		}
#endif
		/*
		 * check collating element information which exists
		 * only in the old locale objects.
		 */
		if (cetbl != NULL && cetbl[wc] != NULL) {
			rc = collel(hdl, order, wc, str, op);
			if (rc != 0) {
				if (rc < 0)
					return (-1);
				str += (rc - 1);
				subhit = 1;
				continue;
			}
		}
		/*
		 * check weight string in the new locale object.
		 * It handles one-to-many mapping.
		 */
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
	/*
	 * If we don't have possible complicate conversion, mark
	 * it simple, which improve performance if we have multiple
	 * orders.
	 */
	if (order == 0 && subhit == 0)
		cc->flag |= CCF_SIMPLE;

	return (0);
}

int
#ifdef _SB_COLL
coll_chr2weight_sb(coll_cookie_t *cc, const char *str, int order)
#else
coll_chr2weight_std(coll_cookie_t *cc, const char *str, int order)
#endif
{
	coll_locale_t *loc = cc->loc;
	_LC_collate_t *hdl = loc->hdl;
#ifndef _SB_COLL
	wchar_t	wc_min = hdl->co_wc_min;
	wchar_t	wc_max = hdl->co_wc_max;
	int	mbcurmax = hdl->cmapp->cm_mb_cur_max;
#endif
	coll_output_t *op = &cc->co;
	wchar_t	wc, wt;
	int	rc, mlen;

	if (loc->submap != NULL &&
	    (loc->submap[(unsigned char)*str] & 0x01)) {
		if ((loc->flag & CLF_EXTINFO) != 0)
			rc = exsubstr(loc, str, order, op);
		else
			rc = substr(cc, str, order);
		if (rc != 0) {
			if (rc > 0 || rc == -1)
				return (rc);
		}
	}

#ifdef _SB_COLL
	wc = (wchar_t)(unsigned char)*str++;
	mlen = 1;
#else
	if ((mlen = (METHOD_NATIVE(hdl->cmapp, mbtowc)(
	    hdl->cmapp, &wc, str, mbcurmax))) == -1) {
		op->error = EILSEQ;
		wc = (wchar_t)(unsigned char)*str++;
		mlen = 1;
	} else {
		str += mlen;
	}
	if (wc > wc_max || wc < wc_min) {
		op->error = EINVAL;
		wc &= 0x7f;
	}
#endif
	if (hdl->co_cetbl != NULL && hdl->co_cetbl[wc] != NULL) {
		rc = collel(hdl, order, wc, str, op);
		if (rc != 0) {
			if (rc < 0)
				return (-1);
			return (mlen + rc - 1);
		}
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
			return (mlen);
		}
	}

	if (coll_output_add(op, wt) != 0)
		return (-1);

	return (mlen);
}

/*
 * Handle many-to-many mapping in old locale objects.
 */
static int
substr(coll_cookie_t *cc, const char *input, int order)
{
	coll_locale_t *loc = cc->loc;
	_LC_collate_t *hdl = loc->hdl;
	int	r, sflag;
	size_t	len;
	const _LC_subs_t *subs, *esubs;
	const char *save;

	if ((hdl->co_sort[order] & _COLL_SUBS_MASK) == 0)
		return (0);
	/*
	 * Check the validity of subs table in the old locale objects.
	 * Since localedef was unable to handle multiple collating symbols
	 * in weight specifier, there can be many subs entries in old locale
	 * objects which have same ss_src and ss_tgt. As they have no effects,
	 * we check them here, and turns of substitute by resetting submap if
	 * all entries are invalid.
	 */
	if ((loc->flag & CLF_SUBCHK) == 0) {
		loc->flag |= CLF_SUBCHK;

		subs = hdl->co_subs;
		esubs = subs + hdl->co_nsubs;
		for (; subs != esubs; subs++) {
			if (strcmp(subs->ss_src, subs->ss_tgt) != 0)
				break;
		}
		if (subs == esubs) {
			/*
			 * all entries have same src and target, which
			 * means the substitute has no effects.
			 */
			loc->flag &= ~CLF_SUBS;
			loc->submap = NULL;
			if (hdl->co_cetbl == NULL) {
				loc->flag |= CLF_SIMPLE;
				cc->flag |= CCF_SIMPLE;
				return (-3);
			}
			return (-2);
		}
	}

	subs = hdl->co_subs;
	esubs = subs + hdl->co_nsubs;
	/* do the old method */
	for (; subs != esubs; subs++) {
		if ((subs->ss_act[order] & _SUBS_ACTIVE) == 0)
			continue;
		len = strlen(subs->ss_src);
		if ((r = strncmp(input, subs->ss_src, len)) == 0)
			break;
		if (r < 0)
			return (0);
	}
	if (subs == esubs)
		return (0);
	/*
	 * we don't want the target string to be converted further.
	 * Turn on SIMPLE so that it will perform one-to-one conversion.
	 */
	save = cc->data.str;
	sflag = cc->flag;
	cc->data.str = subs->ss_tgt;
	cc->flag |= CCF_SIMPLE;
#ifdef _SB_COLL
	r = coll_str2weight_sb(cc, order);
#else
	r = coll_str2weight_std(cc, order);
#endif
	cc->data.str = save;
	cc->flag = sflag;
	if (r < 0)
		return (r);

	return (len);
}

/*
 * Handle many-to-many mapping using new locale object
 */
static int
exsubstr(coll_locale_t *loc, const char *input,
    int order, coll_output_t *op)
{
	int	r, len;
	int	l, m, h;
	const char *src;
	const wchar_t *wtgt;
	wchar_t wt;
	const _LC_exsubs_t *nsubs, *tsubs, *ensubs;

	/*
	 * Get the size and offset of the entries for the given order.
	 */
	r = loc->extinfo->ext_hsuboff[order];
	len = loc->extinfo->ext_hsubsz[order];
	nsubs = loc->extinfo->ext_hsubs + r;
	ensubs = nsubs + len;

	/*
	 * 8 is totally experimental.
	 */
	if (len > 8) {
		/* perform binary search */
		l = 0;
		h = len - 1;
		while (l <= h) {
			m = (l + h) / 2;
			src = nsubs[m].ess_src.sp;
			if ((r = *input - *src) == 0) {
				len = nsubs[m].ess_srclen;
				if ((r = strncmp(input, src, len)) == 0)
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
		/* linear search */
		for (; nsubs != ensubs; nsubs++) {
			src = nsubs->ess_src.sp;
			if ((r = *input - *src) == 0) {
				len = nsubs->ess_srclen;
				if ((r = strncmp(input, src, len)) == 0)
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
		src = tsubs->ess_src.sp;
		len = tsubs->ess_srclen;
		if (*input != *src || strncmp(input, src, len) != 0) {
			nsubs = tsubs - 1;
			break;
		}
	}

	wtgt = loc->wgtstr + nsubs->ess_wgt.wgtidx;
	/* skip first wchar which is the length of target */
	len = *wtgt++;
	while (len--) {
		wt = *wtgt++;
		if (coll_output_add(op, wt) != 0)
			return (-1);
	}
	return (nsubs->ess_srclen);
}

/*
 * Handle many-to-one mapping with old locale objects
 */
static int
collel(_LC_collate_t *hdl, int order,
    wchar_t wc, const char *str, struct coll_output *op)
{
	const _LC_collel_t *ce;
	wchar_t	wt;
	int	len, r;

	ce = hdl->co_cetbl[wc];
	while (ce->ce_sym != NULL) {
		len = strlen(ce->ce_sym);
		if ((r = strncmp(str, ce->ce_sym, len)) == 0)
			break;
		if (r < 0)
			return (0);
		ce++;
	}
	if (ce->ce_sym == NULL)
		return (0);

	wt = ce->ce_wgt[order];
	if (coll_output_add(op, wt) != 0)
		return (-1);
	/*
	 * len can be 0, although weight has been taken.
	 * So we return +1, so the caller know if there is a match.
	 */
	return (len + 1);
}
