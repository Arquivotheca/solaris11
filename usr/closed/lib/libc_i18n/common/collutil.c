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
#include <sys/types.h>
#include <sys/localedef.h>
#include <string.h>
#include <stdlib.h>
#include <alloca.h>
#include <errno.h>
#include <wchar.h>
#include "coll_local.h"

/*
 * Functions which manages results of weight conversion.
 */

/*
 * initialize output stream
 */
void
coll_output_init(coll_output_t *op)
{
	op->out = op->sbuf;
	op->obsize = COLL_OUT_AUTOSZ;
	op->count_only = 0;
	op->error = 0;
	op->pos = 0;
}

#ifdef notdef
/*
 * Add a weight to output stream. IGNOREs are always counted and
 * the number is stored in nignore.
 * This function is defined as a macro for performance reason.
 */
int
coll_output_add(coll_output_t *op, wchar_t wt)
{
	if (op->count_only) {
		if (wt != WEIGHT_IGNORE)
			op->olen++;
		return (0);
	}

	if (op->olen == op->obsize)
		return (coll_output_add_slow(op, wt));

	if (wt == WEIGHT_IGNORE)
		op->nignore++;
	op->out[op->olen++] = wt;
	return (0);
}
#endif

/*
 * called from macro coll_output_add() when buffer needs to be
 * expaneded.
 */
int
coll_output_add_slow(coll_output_t *op, wchar_t wt)
{
	size_t nsz;
	wchar_t *wp;

	/*
	 * This function is called only when olen has reached obsize
	 * ie, need to expand buffer.
	 */
	if (op->obsize == COLL_OUT_AUTOSZ) {
		nsz = COLL_OUT_AUTOSZ + 128;
		wp = malloc(nsz * sizeof (wchar_t));
		if (wp == NULL) {
			op->error = ENOMEM;
			return (1);
		}
		(void) wmemcpy(wp, op->sbuf, COLL_OUT_AUTOSZ);
		op->obsize = nsz;
		op->out = wp;
	} else {
		nsz = op->obsize + 256;
		wp = realloc(op->out, nsz * sizeof (wchar_t));
		if (wp == NULL) {
			op->error = ENOMEM;
			return (1);
		}
		op->obsize = nsz;
		op->out = wp;
	}
	if (wt == WEIGHT_IGNORE)
		op->nignore++;
	op->out[op->olen++] = wt;
	return (0);
}

void
coll_output_fini(coll_output_t *op)
{
	if (op->obsize != COLL_OUT_AUTOSZ)
		free(op->out);
}

/*
 * Shift contents of buffer to the beginning. Following IGNOREs are
 * discarded, and nignore/pos are adjusted properly.
 */
void
coll_output_shift(coll_output_t *op, int shift)
{
	int	i, j, r, ign;
	wchar_t	wt;

	if (shift == 0)
		return;
	/* advance position */
	op->pos += shift;

	r = op->olen - shift;
	if (r == 0) {
		/* stream gets empty */
		coll_output_clean(op);
		return;
	}
	/*
	 * We hve something in the buffer. First check to see if
	 * we have following IGNOREs.
	 */
	i = 0;
	do {
		wt = op->out[shift + i];
		if (wt != WEIGHT_IGNORE)
			break;
	} while (++i < r);

	/* advance position to where we found non-IGNORE */
	op->pos += i;

	if (i == r) {
		/* remaining weights were all IGNORE */
		coll_output_clean(op);
		return;
	}

	/* real shift starts */
	ign = j = 0;
	op->out[j++] = wt;
	i++;
	while (i < r) {
		wt = op->out[shift + i];
		if (wt == WEIGHT_IGNORE)
			ign++;
		/* even if it's IGNORE, we keep it in the buffer */
		op->out[j++] = wt;
		i++;
	}
	op->olen = j;
	op->nignore = ign;
}

/*
 * Initialize the data related to locale object.
 */
void
coll_locale_init(coll_locale_t *loc, _LC_collate_t *hdl)
{
	const char	*s;

	loc->hdl = hdl;
	loc->flag = 0;

	/*
	 * We do touch any future co_ext larger than 1.
	 */
	switch (hdl->co_ext) {
	case 1:
		loc->flag |= CLF_EXTINFO;

		/* check exteneded information */
		loc->extinfo = (const _LC_collextinfo_t *)hdl->co_extinfo;
		if (loc->extinfo != NULL) {
			if ((s = loc->extinfo->ext_submap) != NULL) {
				if ((s[0] & 0x01) != 0)
					loc->flag |= CLF_SUBS;
				if ((s[0] & 0x10) != 0)
					loc->flag |= CLF_WGTSTR;
			}
		}
		/*
		 * The new locale object doesn't have collel. So, if there
		 * is no subs table, and no weight string, then this locale
		 * is simple.
		 */
		if ((loc->flag & (CLF_SUBS|CLF_WGTSTR)) == 0)
			loc->flag |= CLF_SIMPLE;
		break;
	case 0:
		/* old locale objects */
		if (hdl->co_subs != NULL)
			loc->flag |= CLF_SUBS;

		if ((loc->flag & CLF_SUBS) == 0 && hdl->co_cetbl == NULL)
			loc->flag |= CLF_SIMPLE;
		break;
	}
}

/*
 * Initialize quick lookup table for subs table (submap).
 * There is basically nothing to do with the new locale object, since
 * it has pre-compiled table in the object. Old locale objects doesn't
 * have such, so we create one.
 */
static void
init_submap(coll_locale_t *loc)
{
	const char *src, *tgt;
	_LC_collate_t *hdl = loc->hdl;

	if ((loc->flag & CLF_INIT_SUBMAP) != 0)
		return;
	loc->flag |= CLF_INIT_SUBMAP;

	loc->wgtstr = NULL;
	loc->wgtstrmap = NULL;
	loc->submap = NULL;

	if ((loc->flag & CLF_EXTINFO) != 0) {
		if ((loc->flag & CLF_SIMPLE) != 0)
			return;
		/*
		 * weight string is always there if we have either
		 * one-to-many mappings or exsubs tables.
		 */
		loc->wgtstr = loc->extinfo->ext_wgtstr;

		/* The first byte is used for a flag. So we should skip it. */
		if (loc->flag & CLF_WGTSTR) {
			/*
			 * For one to many mapping.
			 */
			loc->wgtstrmap = loc->extinfo->ext_submap + 1;
		}
		if (loc->flag & CLF_SUBS) {
			/*
			 * Simple substitute mapping.
			 */
			loc->submap = loc->extinfo->ext_submap + 1;
		}
	} else {
		const _LC_subs_t *subs, *esubs;

		if ((loc->flag & CLF_SUBS) == 0)
			return;
		/*
		 * Quick check the validity of subs table assuming single
		 * byte locale. If it fails, we defer it till real
		 * substitute happens since it costs too much with the
		 * certain locale objects.
		 */
		subs = hdl->co_subs;
		esubs = subs + hdl->co_nsubs;
		for (; subs != esubs; subs++) {
			src = subs->ss_src;
			tgt = subs->ss_tgt;
			if (*src != *tgt ||
			    *(src + 1) != '\0' || *(tgt + 1) != '\0') {
				break;
			}
		}
		if (subs == esubs) {
			/*
			 * The subs table doesn't appear to have valid entries.
			 * We won't perform substitute since they have no
			 * effects.
			 */
			loc->flag &= ~CLF_SUBS;
			if (hdl->co_cetbl == NULL)
				loc->flag |= CLF_SIMPLE;
		} else {
			/* use embedded area for the lookup table */
			loc->submap = loc->map;
			(void) memset(loc->map, '\0', 256);
			for (subs = hdl->co_subs; subs != esubs; subs++) {
				src = subs->ss_src;
				loc->map[(unsigned char)*src] = 0x01;
			}
		}
	}
}

/*
 * These function manipulates cookie which maintains cache
 * data used over function calls. Cookies are allocated per
 * input string.
 */
void
coll_cookie_init(coll_cookie_t *cc, coll_locale_t *loc, int flag)
{
	cc->loc = loc;
	cc->flag = flag;
	coll_output_init(&cc->co);

	/*
	 * initialize the subtitute map only when character encoding
	 * is determined. If we are going to convert character
	 * (ie CCF_CONVMB|CCF_CONVWC), then init_submap() will be called
	 * later once conversion is done.
	 */
	if (cc->flag & (CCF_MBSTR|CCF_WIDE))
		init_submap(loc);
	/*
	 * If we found that there is nothing useful in the subs table
	 * during the call to init_submap() above, we ignore subs table,
	 * and mark this cookie for a string as simple.
	 */
	if (loc->flag & CLF_SIMPLE)
		cc->flag |= CCF_SIMPLE;
}

void
coll_cookie_fini(coll_cookie_t *cc)
{
	coll_output_fini(&cc->co);
	if ((cc->flag & CCF_ALLOC) != 0)
		free((char *)cc->data.str);
}

static wchar_t
nextwgt(wchar_t wt)
{
	if ((wt & 0xff) == 0)
		wt |= 1;
	if ((wt & 0xff00) == 0)
		wt |= 0x100;
	if ((wt & 0xff0000) == 0)
		wt |= 0x10000;
	return (wt);
}

static int
format_forward_collate(coll_output_t *co, int pos)
{
	wchar_t wt, *ws, spos;
	int	i, j, len, malsz;

	if (!pos) {
		ws = co->out;
		for (i = j = 0; i < co->olen; i++) {
			if (ws[i] == WEIGHT_IGNORE)
				continue;
			if (i != j)
				ws[j] = ws[i];
			j++;
		}
		co->olen = j;
		co->nignore = 0;
	} else {
		len = co->olen;
		malsz = len * sizeof (wchar_t);
		if (malsz < _STACK_THR) {
			ws = alloca(malsz);
			malsz = 0;
		} else if ((ws = malloc(malsz)) == NULL) {
			co->error = ENOMEM;
			return (-1);
		}
		(void) wmemcpy(ws, co->out, len);
		coll_output_clean(co);

		spos = MIN_WEIGHT;
		for (i = 0; i < len; i++) {
			spos = nextwgt(spos + 1);
			wt = ws[i];
			if (wt == WEIGHT_IGNORE)
				continue;
			if (coll_output_add(co, spos) != 0 ||
			    coll_output_add(co, wt) != 0) {
				if (malsz != 0)
					free(ws);
				return (-1);
			}
		}
		if (malsz != 0)
			free(ws);
	}
	return (0);
}

static int
format_backward_collate(coll_output_t *co, int pos)
{
	wchar_t wt, *ws, spos;
	int	i, j, len, malsz;

	len = co->olen;
	malsz = len * sizeof (wchar_t);
	if (malsz < _STACK_THR) {
		ws = alloca(malsz);
		malsz = 0;
	} else if ((ws = malloc(malsz)) == NULL) {
		co->error = ENOMEM;
		return (-1);
	}
	(void) wmemcpy(ws, co->out, len);
	coll_output_clean(co);

	if (!pos) {
		/*
		 * size of co never grow. we can directly store.
		 */
		for (i = len - 1, j = 0; i >= 0; i--) {
			if (ws[i] == WEIGHT_IGNORE)
				continue;
			co->out[j++] = ws[i];
		}
		co->olen = j;
	} else {
	/*
	 * Spec says that "position" specifies that comparison operations
	 * for the weight level shall consider the relative position of
	 * elements in the strings not subject to IGNORE. The string
	 * containing an element not subject to IGNORE after the fewest
	 * collating elements subject to IGNORE from the start of the
	 * compare shall collate first.
	 *
	 * Here, "the start of the compare" is the end of string. Therefore,
	 * position/weights should be placed like:
	 * W1 W2 W3 -> P1 W3 P2 W2 P3 W1  (P1 < P2 < P3)
	 * BTW, old strxfrm does the following, but I don't think that
	 * is correct.
	 * W1 W2 W3 -> W3 P3 W2 P2 W1 P1
	 */
		spos = MIN_WEIGHT;
		for (i = len - 1; i >= 0; i--) {
			spos = nextwgt(spos + 1);
			wt = ws[i];
			if (wt == WEIGHT_IGNORE)
				continue;
			if (coll_output_add(co, spos) != 0 ||
			    coll_output_add(co, wt) != 0) {
				if (malsz != 0)
					free(ws);
				return (-1);
			}
		}
	}
	if (malsz != 0)
		free(ws);
	return (0);
}

/*
 * Format collating weights per sort modifier.
 */
int
coll_format_collate(coll_output_t *co, int mod)
{
	int	pos = (mod & _COLL_POSITION_MASK);

	if (co->count_only) {
		/*
		 * When count_only is turned on, olen holds number of
		 * weights excluding IGNOREs. Since we need one position
		 * weight for each weights, we need x 2 for position.
		 */
		if (pos)
			co->olen *= 2;
		return (0);
	}

	if (co->olen == 0)
		return (0);

	if (mod & _COLL_BACKWARD_MASK) {
		return (format_backward_collate(co, pos));
	} else {
		/*
		 * If we are doing simple forward collating, and if
		 * there is no IGNORE weights in the stream, we will
		 * have nothing to do with it.
		 */
		if (!pos && co->nignore == 0)
			return (0);
		return (format_forward_collate(co, pos));
	}
}

static size_t
wgt_width(int sort_mod)
{
	/*
	 * 3 and 1 are the majority. So check them first.
	 */
	if ((sort_mod & _COLL_WGT_WIDTH3) != 0)
		return (3);
	else if ((sort_mod & _COLL_WGT_WIDTH1) != 0)
		return (1);
	else if ((sort_mod & _COLL_WGT_WIDTH2) != 0)
		return (2);
	else if ((sort_mod & _COLL_WGT_WIDTH4) != 0)
		return (4);
	return (0);
}

/*
 * Calculate the effective bytes needed for collating weight
 */
size_t
coll_wgt_width(coll_locale_t *loc)
{
	_LC_collate_t	*hdl = loc->hdl;
	int	r, order, sort_mod;

	sort_mod = 0;
	for (order = 0; order <= (int)hdl->co_nord; order++)
		sort_mod |= hdl->co_sort[order];
	/*
	 * If position is specified in any orders, we need to use 4,
	 * otherwise position beyond 64K etc could get wrapped around
	 * which would lead to wrong results.
	 */
	if ((sort_mod & _COLL_POSITION_MASK) != 0)
		return (sizeof (wchar_t));

	sort_mod = hdl->co_sort[0];

	if (loc->flag & CLF_EXTINFO) {
		if ((r = wgt_width(sort_mod)) != 0)
			return (r);
	} else {
		/*
		 * Check to see if width has been specified by wcsxfrm()
		 * when strxfrm() is called from it.
		 */
		if ((sort_mod & _COLL_WGT_MASK) != 0) {
			if ((r = wgt_width(sort_mod)) != 0)
				return (r);
		}

		if (hdl->co_r_order == 0) {
			wchar_t wt;

			/* co_col_max represents max weights */
			wt = hdl->co_col_max;
			wt >>= 8;
			if (wt == 0x010101)
				return (1);
			wt >>= 8;
			if (wt == 0x0101)
				return (2);
			wt >>= 8;
			if (wt == 0x01)
				return (3);
		} else {
			/*
			 * The co_col_min and co_col_max doesn't indicate
			 * the min/max of regular weights, but min/max of
			 * relative weights. Also, we need at least 2 bytes
			 * in case position is specified. We can't easily
			 * determine necessary bytes from co_col_max. So,
			 * We use fixed rules of old runtime. That is 2 bytes
			 * for single byte locale, and 4 bytes for others.
			 */
			extern size_t __strxfrm_sb(_LC_collate_t *, char *,
			    const char *, size_t);

			if (hdl->core.native_api->strxfrm == __strxfrm_sb)
				return (2);
		}
	}

	/* It's 4 (size of wchar_t) by default. */
	return (sizeof (wchar_t));
}

/*
 * store1() - store4(). Take necessary portion (1-4bytes) of each weights
 * and store them into flat memory.
 */
static void
store1(char *strout, size_t cur, size_t max, coll_output_t *co)
{
	char	*ostr, *eostr;
	int	i;

	ostr = strout + cur;
	eostr = strout + max - 1;
	for (i = 0; i < co->olen && ostr != eostr; i++)
		*ostr++ = (char)(co->out[i] & 0xff);
	*ostr = '\0';
}

static void
store2(char *strout, size_t cur, size_t max, coll_output_t *co)
{
	wchar_t	wt;
	char	*ostr, *eostr;
	int	i, r;
	int	rem = max - cur - 1;

	ostr = strout + cur;
	eostr = strout + max - 1;

	if ((r = (rem % 2)) != 0)
		eostr--;

	for (i = 0; i < co->olen && ostr != eostr; i++) {
		wt = co->out[i];
		*ostr++ = (char)((wt >> 8) & 0xff);
		*ostr++ = (char)(wt & 0xff);
	}
	if (r != 0 && i < co->olen) {
		wt = co->out[i];
		*ostr++ = (char)((wt >> 8) & 0xff);
	}
	*ostr = '\0';
}

static void
store3(char *strout, size_t cur, size_t max, coll_output_t *co)
{
	wchar_t	wt;
	char	*ostr, *eostr;
	int	i, r;
	int	rem = max - cur - 1;

	ostr = strout + cur;
	eostr = strout + max - 1;

	if ((r = (rem % 3)) != 0)
		eostr -= r;

	for (i = 0; i < co->olen && ostr != eostr; i++) {
		wt = co->out[i];
		*ostr++ = (char)((wt >> 16) & 0xff);
		*ostr++ = (char)((wt >> 8) & 0xff);
		*ostr++ = (char)(wt & 0xff);
	}
	if (r != 0 && i < co->olen) {
		wt = co->out[i];
		switch (r) {
		case 1:
			*ostr++ = (char)((wt >> 16) & 0xff);
			break;
		case 2:
			*ostr++ = (char)((wt >> 16) & 0xff);
			*ostr++ = (char)((wt >> 8) & 0xff);
			break;
		}
	}
	*ostr = '\0';
}

static void
store4(char *strout, size_t cur, size_t max, coll_output_t *co)
{
	wchar_t	wt;
	char	*ostr, *eostr;
	int	i, r;
	int	rem = max - cur - 1;

	ostr = strout + cur;
	eostr = strout + max - 1;

	if ((r = (rem % 4)) != 0)
		eostr -= r;

	for (i = 0; i < co->olen && ostr != eostr; i++) {
		wt = co->out[i];
		*ostr++ = (char)((wt >> 24) & 0xff);
		*ostr++ = (char)((wt >> 16) & 0xff);
		*ostr++ = (char)((wt >> 8) & 0xff);
		*ostr++ = (char)(wt & 0xff);
	}
	if (r != 0 && i < co->olen) {
		wt = co->out[i];
		switch (r) {
		case 1:
			*ostr++ = (char)((wt >> 24) & 0xff);
			break;
		case 2:
			*ostr++ = (char)((wt >> 24) & 0xff);
			*ostr++ = (char)((wt >> 16) & 0xff);
			break;
		case 3:
			*ostr++ = (char)((wt >> 24) & 0xff);
			*ostr++ = (char)((wt >> 16) & 0xff);
			*ostr++ = (char)((wt >> 8) & 0xff);
			break;
		}
	}
	*ostr = '\0';
}

static void
store4w(wchar_t *strout, size_t cur, size_t max, coll_output_t *co)
{
	wchar_t	*ostr, *eostr;
	int	i;

	ostr = strout + cur;
	eostr = strout + max - 1;
	for (i = 0; i < co->olen && ostr != eostr; i++)
		*ostr++ = co->out[i];
	*ostr = L'\0';
}

/*
 * Store collation weights to give memory area.
 *	strout:	pointer to the beginning of memory area
 *	cur:	offset where weights are stored.
 *	max:	max offset allowed.
 * If cur (when \0 is stored) is going beyond max, it bails
 * out from the loop and return the size which was supposed
 * to be stored.
 */
size_t
coll_store_weight(int nbpw, char *strout, size_t cur, size_t max,
	coll_output_t *co, boolean_t output_wide)
{
	if (max == 0 || cur >= max)
		return (co->olen * nbpw);

	switch (nbpw) {
	case 1:
		store1(strout, cur, max, co);
		break;
	case 2:
		store2(strout, cur, max, co);
		break;
	case 3:
		store3(strout, cur, max, co);
		break;
	case 4: /* sizeof (wchar_t) */
		if (output_wide) {
			/*
			 * store4w() stores "wchar_t" instead of "char".
			 * index/count needs to be the number of wchar_t.
			 */
			/*LINTED*/
			store4w((wchar_t *)strout, cur / sizeof (wchar_t),
			    max / sizeof (wchar_t), co);
		} else {
			store4(strout, cur, max, co);
		}
		break;
	}
	return (co->olen * nbpw);
}

#ifdef notdef
/*
 * Convert input string into appropriate format. The memory area
 * for the results can be allocated by alloca(). Therefore, this doesn't
 * exist as a real function, but a macro. Below is the pseudo code to
 * understand what it does.
 */
size_t
coll_conv_input(coll_cookie_t *cc)
{
	void	*buf;

	if (coll_conv_calc_size(cc) != 0) {
		buf = malloc(cc->allocsz);
	} else {
		if (cc->allocsz == 0)
			buf = cc->data.wstr;
		else
			buf = alloca(cc->allocsz);
	}
	return (coll_conv_input_real(cc, buf));
}
#endif

/*
 * calculates buffer size necessary to store string which will be
 * converted from input string. Called from the macro coll_conv_input().
 */
size_t
coll_conv_calc_size(coll_cookie_t *cc)
{
	size_t	malsz;

	if (cc->flag & CCF_CONVMB) {
		/* convert wide chars into mbstring */
		cc->inlen = wcslen(cc->data.wstr);
		malsz = cc->inlen * cc->loc->hdl->cmapp->cm_mb_cur_max + 1;
	} else if (cc->flag & CCF_CONVWC) {
		/* convert mbstring to wide char */
		cc->inlen = strlen(cc->data.str);
		malsz = (cc->inlen + 1) * sizeof (wchar_t);
	} else if ((cc->flag & (CCF_WIDE|CCF_BC)) == (CCF_WIDE|CCF_BC)) {
		/* convert user wc to dense pc */
		cc->inlen = wcslen(cc->data.wstr);
		malsz = (cc->inlen + 1) * sizeof (wchar_t);
	} else {
		/* no conversion */
		cc->inlen = 0;
		malsz = 0;
	}

	cc->allocsz = malsz;
	if (malsz > _STACK_THR) {
		cc->flag |= CCF_ALLOC;
		return (1);
	}
	return (0);
}

/*
 * Convert input string to appropriate encoding.
 */
const wchar_t *
coll_conv_input_real(coll_cookie_t *cc, void *buf)
{
	coll_locale_t *loc = cc->loc;
	_LC_collate_t *hdl = loc->hdl;
	const wchar_t	*wstr;
	int	i, len, rc;

	if (buf == NULL) {
		cc->co.error = ENOMEM;
		return (NULL);
	}

	if (cc->flag & CCF_ATTACHED)
		return (cc->data.wstr);

	cc->flag |= CCF_ATTACHED;

	if (cc->flag & CCF_CONVMB) {
		/*
		 * We extract wchar into reguar string. This is used to
		 * call mb method when we deal with old locale object.
		 */
		wstr = cc->data.wstr;
		len = cc->allocsz;
		if (cc->flag & CCF_BC) {
			rc = METHOD(hdl->cmapp, wcstombs)
			    (hdl->cmapp, buf, wstr, len);
		} else {
			rc = METHOD_NATIVE(hdl->cmapp, wcstombs)
			    (hdl->cmapp, buf, wstr, len);
		}
		if (rc == (size_t)-1) {
			cc->co.error = EILSEQ;
			return (NULL);
		}
		cc->flag &= ~CCF_CONVMB;
		cc->flag |= CCF_MBSTR;
		/*
		 * We don't initialize submap here, since we'll call
		 * into mb method.
		 */
		cc->data.str = buf;
		return ((wchar_t *)buf);
	} else if (cc->flag & CCF_CONVWC) {
		/*
		 * convert mb string to wc
		 */
		rc = METHOD_NATIVE(hdl->cmapp, mbstowcs)
		    (hdl->cmapp, buf, cc->data.str, cc->inlen + 1);
		if (rc == (size_t)-1) {
			cc->co.error = EILSEQ;
			return (NULL);
		}
		cc->flag &= ~CCF_CONVWC;
		cc->flag |= CCF_WIDE;

		/* initialize sub map */
		init_submap(loc);

		cc->data.wstr = buf;
		return ((wchar_t *)buf);
	} else if ((cc->flag & (CCF_WIDE|CCF_BC)) == (CCF_WIDE|CCF_BC)) {
		/*
		 * convert EUC wc to PC.
		 */
		wchar_t	wc_min = hdl->co_wc_min;
		wchar_t	wc_max = hdl->co_wc_max;
		wchar_t	wc, *ws;

		wstr = cc->data.wstr;
		len = cc->inlen;
		ws = (wchar_t *)buf;
		for (i = 0; i < len; i++) {
			if ((wc = _eucpctowc(hdl->cmapp, wstr[i])) == WEOF) {
				cc->co.error = EILSEQ;
				return (NULL);
			}
			if (wc > wc_max || wc < wc_min) {
				cc->co.error = EINVAL;
				wc &= 0x7f;
			}
			ws[i] = wc;
		}
		ws[i] = L'\0';
		cc->flag &= ~CCF_BC;
		cc->data.wstr = buf;
		return ((wchar_t *)buf);
	} else {
		/*
		 * We have new locale object, and using dense code. So
		 * we don't need to convert.
		 */
		return (cc->data.wstr);
	}
}
