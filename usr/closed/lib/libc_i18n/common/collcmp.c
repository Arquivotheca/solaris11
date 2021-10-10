/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

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
#include <errno.h>
#include "coll_local.h"

/*
 * This file exports three functions by having special macro defined or
 * nothing is defined. They are:
 *	coll_compare_wc		(_WC_COLL is defined)
 *	coll_compare_sb		(_SB_COLL is defined)
 *	coll_compare_std
 * These functions compare weights in given output streams (coll_output_t)
 * and return appropriate value. If error occurs (eg malloc failure), they
 * set error indicator in the output stream, and return FAILED(1), so that
 * no further comparison occurs. Error is evaluated at the end of caller
 * functions.
 */

#define	FAILED		(1)

#ifdef _WC_COLL
#define	CHAR		wchar_t
#define	EOS		L'\0'
#define	INSTR(cc)	((cc)->data.wstr)
#else
#define	CHAR		char
#define	EOS		'\0'
#define	INSTR(cc)	((cc)->data.str)
#endif

#if defined(_WC_COLL)
#define	COLL_COMPARE	coll_compare_wc
#define	CHR2WEIGHT	coll_wchr2weight
#define	STR2WEIGHT	coll_wstr2weight
#elif defined(_SB_COLL)
#define	COLL_COMPARE	coll_compare_sb
#define	CHR2WEIGHT	coll_chr2weight_sb
#define	STR2WEIGHT	coll_str2weight_sb
#else
#define	COLL_COMPARE	coll_compare_std
#define	CHR2WEIGHT	coll_chr2weight_std
#define	STR2WEIGHT	coll_str2weight_std
#endif

/*
 * Simple character(s) -> weight conversion function which can be used
 * when many-to-one/one-to-many mapping are not performed.
 */
#if defined(_WC_COLL)
#define	CHR2WEIGHT_SIMPLE(s, w, o) \
	{	\
		wchar_t _wc_ = *(s)++;	\
		if (bc) { \
			if ((_wc_ = _eucpctowc(cmapp, _wc_)) == WEOF) { \
				(o)->error = EILSEQ;	\
				return (FAILED); \
			} \
		}	\
		if (_wc_ > wc_max || _wc_ < wc_min) {	\
			(o)->error = EINVAL;		\
			_wc_ &= 0x7f;		\
		}	\
		w = coltbl[_wc_]; \
	}
#elif defined(_SB_COLL)
#define	CHR2WEIGHT_SIMPLE(s, w, o) \
	w = coltbl[(unsigned char)*(s)++]
#else
#define	CHR2WEIGHT_SIMPLE(s, w, o) \
	{ \
		wchar_t _wc_; \
		if ((r = (METHOD_NATIVE(cmapp, mbtowc)( \
		    cmapp, &_wc_, (s), mbcurmax))) == -1) { \
			(o)->error = EILSEQ; \
			_wc_ = (wchar_t)(unsigned char)*(s)++; \
		} else {		\
			s += r;	\
		}			\
		if (_wc_ > wc_max || _wc_ < wc_min) {	\
			(o)->error = EINVAL;		\
			_wc_ &= 0x7f;		\
		}	\
		(w) = coltbl[_wc_]; \
	}
#endif

/*
 * fills weights if output stream (coll_output_t) gets empty.
 */
static const CHAR *
wgt_fill(coll_cookie_t *cc, const CHAR *s, int order)
{
	coll_output_t *co = &cc->co;
	int	r;
	int	pos = 0;

	/*
	 * We clean up stream so that nignore gets also reset.
	 */
	coll_output_clean(co);
	do {
		r = CHR2WEIGHT(cc, s, order);
		if (r < 0)
			return (NULL);
		s += r;
		if (!coll_all_ignore(co))
			break;
		pos += co->olen;
		/*
		 * We got some, but they were all IGNOREs. Clean up stream.
		 */
		coll_output_clean(co);
	} while (*s != EOS);

	co->pos += pos;
	return (s);
}

#ifdef _WC_COLL
/*
 * Export these functions only when _WC_COLL is defined. They don't
 * depend on char type, so don't need to have multiple static code.
 */

/*
 * Compare weights.
 */
int
coll_wgt_comp(coll_output_t *co1, coll_output_t *co2)
{
	int	i, j, r;

	/*
	 * First check a common situation where both streams have only 1
	 * weight and they are not IGNOREs.
	 */
	if (co1->olen == 1 && co2->olen == 1 &&
	    co1->out[0] != WEIGHT_IGNORE &&
	    co2->out[0] != WEIGHT_IGNORE) {
		/* it's a simple one-by-one compare */
		r = (int)co1->out[0] - co2->out[0];
		if (r != 0)
			return (r);
		/* clean up all */
		coll_output_clean(co1);
		coll_output_clean(co2);
		return (0);
	}

	/*
	 * More complicate case. We need to skip IGNOREs.
	 */
	i = j = 0;
	do {
		/*
		 * Skip until we found something not IGNORE.
		 */
		while (i < co1->olen && co1->out[i] == WEIGHT_IGNORE)
			i++;
		while (j < co2->olen && co2->out[j] == WEIGHT_IGNORE)
			j++;
		/*
		 * If one of stream reached end. We need valid weights
		 * to be compared. So bail out from loop to fill weights.
		 */
		if (i == co1->olen || j == co2->olen)
			break;
		r = (int)co1->out[i++] - co2->out[j++];
		if (r != 0)
			return (r);
	} while (i < co1->olen && j < co2->olen);

	/*
	 * valid weight will be placed at the beginning. And pos/olen/nignore
	 * will be adjusted accordingly.
	 */
	coll_output_shift(co1, i);
	coll_output_shift(co2, j);

	return (0);
}

/*
 * Compare weights, with taking position into account.
 */
int
coll_wgt_pos_comp(coll_output_t *co1, coll_output_t *co2)
{
	int	i, j, r;

	i = j = 0;
	do {
		while (i < co1->olen && co1->out[i] == WEIGHT_IGNORE)
			i++;
		while (j < co2->olen && co2->out[j] == WEIGHT_IGNORE)
			j++;
		if (i == co1->olen || j == co2->olen)
			break;
		r = (co1->pos + i) - (co2->pos + j);
		if (r != 0)
			return (r);
		r = (int)co1->out[i++] - co2->out[j++];
		if (r != 0)
			return (r);
	} while (i < co1->olen && j < co2->olen);

	coll_output_shift(co1, i);
	coll_output_shift(co2, j);

	return (0);
}
#endif /* _WC_COLL */

#define	COMP(x, y) (pos ? coll_wgt_pos_comp(x, y) :  coll_wgt_comp(x, y))

static int
forward_comp_epilogue(coll_cookie_t *cc1, coll_cookie_t *cc2,
    const CHAR *s1, const CHAR *s2, int order, int pos)
{
	int	r;
	coll_output_t *co1, *co2;

	co1 = &cc1->co;
	co2 = &cc2->co;
	/*
	 * Either s1 or s2 gets EOS. However, there may be weights
	 * left in output stream.
	 * We've got one of co's empty, and one of strings got empty.
	 * Also, non-IGNORE weight should be placed in co->out[0] by
	 * the last call to coll_wgt_comp() if olen != 0.
	 */
	if (*s1 == EOS && *s2 == EOS) {
		/*
		 * one or both of streams should be empty.
		 */
		return ((int)co1->olen - co2->olen);
	}

	if (*s1 == EOS) {
		/*
		 * s2 isn't EOS. Remember that we came here because one of
		 * co's empty. Therefore, if co1 isn't empty, co2 must be
		 * empty.
		 */
		while (co1->olen != 0 && *s2 != EOS) {
			/* co2 is empty here */
			if ((s2 = wgt_fill(cc2, s2, order)) == NULL)
				return (FAILED);
			if ((r = COMP(co1, co2)) != 0)
				return (r);
		}
		/*
		 * Here we got either co1 exhausted or s2 gets EOS.
		 * When co1 gets empty, we still need to fill weights
		 * since s2 may not hit EOS. So, if co2 is empty, we
		 * fill weights.
		 * If co1 didn't get empty, then s2 should have reached
		 * EOS and co2 must be empty. So we don't do anything.
		 */
		if (co1->olen == 0 && co2->olen == 0) {
			if (*s2 != EOS) {
				if (wgt_fill(cc2, s2, order) == NULL)
					return (FAILED);
			}
		}
	} else {
		/* s2 reached EOS */
		while (co2->olen != 0 && *s1 != EOS) {
			if ((s1 = wgt_fill(cc1, s1, order)) == NULL)
				return (FAILED);
			if ((r = COMP(co1, co2)) != 0)
				return (r);
		}
		if (co1->olen == 0 && co2->olen == 0) {
			if (*s1 != EOS) {
				if (wgt_fill(cc1, s1, order) == NULL)
					return (FAILED);
			}
		}
	}
	/*
	 * At this point, we got at least one of co's empty.
	 */
	return ((int)co1->olen - co2->olen);
}

static int
forward_comp(coll_cookie_t *cc1, coll_cookie_t *cc2, int order)
{
	const CHAR *s1 = INSTR(cc1);
	const CHAR *s2 = INSTR(cc2);
	coll_output_t *co1, *co2;
	wchar_t	wt1, wt2;
	int	r;

	co1 = &cc1->co;
	co2 = &cc2->co;

	coll_output_clean(co1);
	coll_output_clean(co2);

	if ((cc1->flag & cc2->flag & CCF_SIMPLE) != 0) {
		_LC_collate_t	*hdl = cc1->loc->hdl;
		_LC_weight_t	coltbl = hdl->co_coltbl[order];
#ifndef _SB_COLL
		_LC_charmap_t *cmapp = hdl->cmapp;
		wchar_t	wc_min = hdl->co_wc_min;
		wchar_t	wc_max = hdl->co_wc_max;
#ifndef _WC_COLL
		int	mbcurmax = cmapp->cm_mb_cur_max;
#endif
#endif
#ifdef _WC_COLL
		int	bc = (cc1->flag & CCF_BC);
#endif
		while (*s1 != EOS && *s2 != EOS) {
			do {
				CHR2WEIGHT_SIMPLE(s1, wt1, co1);
			} while (wt1 == WEIGHT_IGNORE && *s1 != EOS);
			do {
				CHR2WEIGHT_SIMPLE(s2, wt2, co2);
			} while (wt2 == WEIGHT_IGNORE && *s2 != EOS);
			r = (int)wt1 - wt2;
			if (r != 0)
				return (r);
		}
		if (*s1 != EOS) {
			do {
				CHR2WEIGHT_SIMPLE(s1, wt1, co1);
			} while (wt1 == WEIGHT_IGNORE && *s1 != EOS);
			if (wt1 != WEIGHT_IGNORE)
				return (1);
		} else if (*s2 != EOS) {
			do {
				CHR2WEIGHT_SIMPLE(s2, wt2, co2);
			} while (wt2 == WEIGHT_IGNORE && *s2 != EOS);
			if (wt2 != WEIGHT_IGNORE)
				return (-1);
		}
		return (0);
	}

	while (*s1 != EOS && *s2 != EOS) {
		if (co1->olen == 0) {
			if ((s1 = wgt_fill(cc1, s1, order)) == NULL)
				return (FAILED);
		}
		if (co2->olen == 0) {
			if ((s2 = wgt_fill(cc2, s2, order)) == NULL)
				return (FAILED);
		}
		if ((r = coll_wgt_comp(co1, co2)) != 0)
			return (r);
	}
	return (forward_comp_epilogue(cc1, cc2, s1, s2, order, 0));
}

static int
forward_pos_comp(coll_cookie_t *cc1, coll_cookie_t *cc2, int order)
{
	const CHAR *s1 = INSTR(cc1);
	const CHAR *s2 = INSTR(cc2);
	coll_output_t *co1, *co2;
	wchar_t	wt1, wt2;
	int	s1pos, s2pos;
	int	r;

	co1 = &cc1->co;
	co2 = &cc2->co;

	coll_output_clean(co1);
	coll_output_clean(co2);

	s1pos = s2pos = 0;

	if ((cc1->flag & cc2->flag & CCF_SIMPLE) != 0) {
		_LC_collate_t	*hdl = cc1->loc->hdl;
		_LC_weight_t	coltbl = hdl->co_coltbl[order];
#ifndef _SB_COLL
		_LC_charmap_t *cmapp = hdl->cmapp;
		wchar_t	wc_min = hdl->co_wc_min;
		wchar_t	wc_max = hdl->co_wc_max;
#ifndef _WC_COLL
		int	mbcurmax = cmapp->cm_mb_cur_max;
#endif
#endif
#ifdef _WC_COLL
		int	bc = (cc1->flag & CCF_BC);
#endif

		while (*s1 != EOS && *s2 != EOS) {
			do {
				CHR2WEIGHT_SIMPLE(s1, wt1, co1);
				if (wt1 != WEIGHT_IGNORE)
					break;
				s1pos++;
			} while (*s1 != EOS);
			do {
				CHR2WEIGHT_SIMPLE(s2, wt2, co2);
				if (wt2 != WEIGHT_IGNORE)
					break;
				s2pos++;
			} while (*s2 != EOS);

			if (wt1 != WEIGHT_IGNORE && wt2 != WEIGHT_IGNORE) {
				r = s1pos - s2pos;
				if (r != 0)
					return (r);
			}
			r = (int)wt1 - wt2;
			if (r != 0)
				return (r);
		}
		if (*s1 != EOS) {
			do {
				CHR2WEIGHT_SIMPLE(s1, wt1, co1);
			} while (wt1 == WEIGHT_IGNORE && *s1 != EOS);
			if (wt1 != WEIGHT_IGNORE)
				return (1);
		} else if (*s2 != EOS) {
			do {
				CHR2WEIGHT_SIMPLE(s2, wt2, co2);
			} while (wt2 == WEIGHT_IGNORE && *s2 != EOS);
			if (wt2 != WEIGHT_IGNORE)
				return (-1);
		}
		return (0);
	}

	co1->pos = 0;
	co2->pos = 0;

	while (*s1 != EOS && *s2 != EOS) {
		if (co1->olen == 0) {
			if ((s1 = wgt_fill(cc1, s1, order)) == NULL)
				return (FAILED);
		}
		if (co2->olen == 0) {
			if ((s2 = wgt_fill(cc2, s2, order)) == NULL)
				return (FAILED);
		}
		if ((r = coll_wgt_pos_comp(co1, co2)) != 0)
			return (r);
	}
	return (forward_comp_epilogue(cc1, cc2, s1, s2, order, 1));
}

/*
 * This function actually does a bit inconsistent, but this's what
 * the old runtime does. The reason why this cannot be replaced with
 * wcscmp() is that both weight can include negative value which were
 * inadvertently generated by old localedef (SUB_STRING left in colltbl).
 * The wcscmp() doesn't handle them as unsigned, which results wrong
 * return value. Old runtime has same problem if it found different.
 * However, old runtime does right thing when one got the end of string.
 * So the following function does half-right thing to make new runtime
 * return exact same results as old. Not needed to be for standards, but
 * for testing purpose; it makes us easy to compare results.
 */
#ifdef _WC_COLL
int
coll_wgtcmp(const wchar_t *s1, const wchar_t *s2)
{
	while (*s1 != '\0' && *s2 != '\0') {
		if (*s1 != *s2)
			return (*s1 - *s2);
		s1++;
		s2++;
	}
	if (*s1 == *s2)
		return (0);
	if (*s1 == '\0')
		return (-1);
	else
		return (1);
}
#endif

static int
backward_comp(coll_cookie_t *cc1, coll_cookie_t *cc2, int order, int sort_mod)
{
	coll_output_t *co1, *co2;

	co1 = &cc1->co;
	co2 = &cc2->co;

	coll_output_clean(co1);
	coll_output_clean(co2);

	if (STR2WEIGHT(cc1, order) < 0 ||
	    STR2WEIGHT(cc2, order) < 0) {
		return (FAILED);
	}

	if (coll_format_collate(co1, sort_mod) != 0 ||
	    coll_output_add(co1, L'\0') != 0) {
		return (FAILED);
	}
	if (coll_format_collate(co2, sort_mod) != 0 ||
	    coll_output_add(co2, L'\0') != 0) {
		return (FAILED);
	}

	return (coll_wgtcmp(co1->out, co2->out));
}

int
COLL_COMPARE(coll_cookie_t *cc1, coll_cookie_t *cc2, int order)
{
	wchar_t sort_mod = cc1->loc->hdl->co_sort[order];
	int	rc;

	if (sort_mod & _COLL_BACKWARD_MASK) {
		rc = backward_comp(cc1, cc2, order, sort_mod);
	} else {
		if (sort_mod & _COLL_POSITION_MASK) {
			rc = forward_pos_comp(cc1, cc2, order);
		} else {
			rc = forward_comp(cc1, cc2, order);
		}
	}
	return (rc);
}
