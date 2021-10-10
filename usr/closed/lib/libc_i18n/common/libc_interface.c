/*
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include "lint.h"
#include "mtlib.h"
#include "mbstatet.h"
#include "file64.h"
#include <sys/types.h>
#include <stdio.h>
#include <wchar.h>
#include <sys/localedef.h>
#include "stdiom.h"
#include "mse.h"

#define	GET_FUNCP(lc)	\
	(f_flag == FP_WCTOMB ? \
	(void (*)(void))METHOD((lc), wctomb) : \
	(void (*)(void))METHOD((lc), fgetwc))

int
_set_orientation_wide(FILE *iop, void **lcharmap,
    void (*(*funcp))(void), int f_flag)
{
	_IOP_orientation_t	orientation;
	mbstate_t	*mbst;

	orientation = GET_BYTE_MODE(iop) ? _BYTE_MODE :
	    GET_WC_MODE(iop) ? _WC_MODE : _NO_MODE;

	switch (orientation) {
	case _NO_MODE:
		CLEAR_BYTE_MODE(iop);
		SET_WC_MODE(iop);

		mbst = _getmbstate(iop);
		if (mbst == NULL) {
			return (-1);
		}
		mark_locale_as_oriented();
		mbst->__lc_locale = (void *)__lc_locale;
		if (lcharmap) {
			*lcharmap = __lc_charmap;
			*funcp = GET_FUNCP(__lc_charmap);
		}
		break;

	case _WC_MODE:
		mbst = _getmbstate(iop);
		if (mbst == NULL) {
			return (-1);
		}
		if (lcharmap) {
			*lcharmap =
			    ((_LC_locale_t *)mbst->__lc_locale)->lc_charmap;
			*funcp = GET_FUNCP((_LC_charmap_t *)*lcharmap);
		}
		break;

	case _BYTE_MODE:
		mbst = _getmbstate(iop);
		if (mbst == NULL) {
			return (-1);
		}
		if (lcharmap) {
			if (mbst->__lc_locale == NULL)
				*lcharmap = __lc_charmap;
			else
				*lcharmap = ((_LC_locale_t *)
				    mbst->__lc_locale)->lc_charmap;
			*funcp = GET_FUNCP((_LC_charmap_t *)*lcharmap);
		}
	}
	return (0);
}

void *
__mbst_get_lc_and_fp(const mbstate_t *ps, void (*(*funcp))(void), int f_flag)
{
	_LC_locale_t	*loc;
	_LC_charmap_t	*lc;

	loc = (_LC_locale_t *)ps->__lc_locale;
	if (loc) {
		/* if loc is valid, use the lc_charmap member */
		lc = loc->lc_charmap;
	} else {
		/* if loc is null, use __lc_charmap setting */
		lc = __lc_charmap;
	}

	*funcp = GET_FUNCP(lc);

	return ((void *)lc);
}
