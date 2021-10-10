/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * COPYRIGHT NOTICE
 *
 * This source code is designated as Restricted Confidential Information
 * and is subject to special restrictions in a confidential disclosure
 * agreement between HP, IBM, SUN, NOVELL and OSF.  Do not distribute
 * this source code outside your company without OSF's specific written
 * approval.  This source code, and all copies and derivative works
 * thereof, must be returned or destroyed at request. You must retain
 * this notice on any copies which you make.
 *
 * (c) Copyright 1990, 1991, 1992, 1993 OPEN SOFTWARE FOUNDATION, INC.
 * ALL RIGHTS RESERVED
 */
/*
 * OSF/1 1.2
 */
/*
 * #if !defined(lint) && !defined(_NOIDENT)
 * static char rcsid[] = "@(#)$RCSfile: sem_xlat.c,v $ $Revision: 1.4.2.3 $"
 *	" (OSF) $Date: 1992/02/18 20:26:08 $";
 * #endif
 */
/*
 * COMPONENT_NAME: (CMDLOC) Locale Database Commands
 *
 * FUNCTIONS:
 *
 *
 * (C) COPYRIGHT International Business Machines Corp. 1991
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 *
 * 1.4  com/cmd/nls/sem_xlat.c, cmdnls, bos320, 9138320 9/11/91 16:35:09
 */
#include "locdef.h"

/*
 *  FUNCTION: sem_push_xlat
 *
 *  DESCRIPTION:
 *  Creates a character range item from two character reference items.
 *  The routine pops two character reference items off the semantic stack.
 *  These items represent the "to" and "from" pair for a character case
 *  translation.  The implementation uses a character range structure to
 *  represent the pair.
 */
void
sem_push_xlat(void)
{
	item_t   *it0, *it1;
	item_t   *it;
	it1 = sem_pop();	/* this is the TO member of the pair */
	it0 = sem_pop();	/* this is the FROM member of the pair */

	/* this creates the item and sets the min and max to wc_enc */

	if (it0->type == SK_UNDEF) {
		switch (it1->type) {
		case SK_UNDEF:
			diag_error2(gettext(ERR_INVAL_XLAT),
			    it0->value.str, it1->value.str);
			break;
		case SK_CHR:
			diag_error2(gettext(ERR_INVAL_XLAT),
			    it0->value.str, it1->value.chr->str_enc);
			break;
		case SK_UINT64:
			{
				char	str1[24];

				(void) snprintf(str1, sizeof (str1),
				    "\\x%llx", it1->value.uint64_no);
				diag_error2(gettext(ERR_INVAL_XLAT),
				    it0->value.str, str1);
			}
			break;
		default:
			INTERNAL_ERROR;
		}
		destroy_item(it1);
		destroy_item(it0);
		return;
	} else if (it1->type == SK_UNDEF) {
		switch (it0->type) {
		case SK_CHR:
			diag_error2(gettext(ERR_INVAL_XLAT),
			    it0->value.chr->str_enc, it1->value.str);
			break;
		case SK_UINT64:
			{
				char	str1[24];

				(void) snprintf(str1, sizeof (str1),
				    "\\x%llx", it0->value.uint64_no);
				diag_error2(gettext(ERR_INVAL_XLAT),
				    str1, it1->value.str);
			}
			break;
		default:
			INTERNAL_ERROR;
		}
		destroy_item(it1);
		destroy_item(it0);
		return;
	}

	if (it0->type == it1->type)	/* Same type is easy case */
		switch (it0->type) {
		case SK_CHR:
			it = create_item(SK_RNG, it0->value.chr->fc_enc,
			    it1->value.chr->fc_enc);
			break;
		case SK_UINT64:
			it = create_item(SK_RNG, it0->value.uint64_no,
			    it1->value.uint64_no);
			break;
		default:
			INTERNAL_ERROR;
		}
	/*
	 * Not same types, we can coerce INT and CHR into a valid range
	 */
	else if (it0->type == SK_CHR && it1->type == SK_UINT64)
		it = create_item(SK_RNG, it0->value.chr->fc_enc,
		    it1->value.uint64_no);
	else if (it0->type == SK_UINT64 && it1->type == SK_CHR)
		it = create_item(SK_RNG, it0->value.uint64_no,
		    it1->value.chr->fc_enc);
	else
		INTERNAL_ERROR;

	destroy_item(it1);
	destroy_item(it0);

	(void) sem_push(it);
}

/*
 * FUNCTION: add_transformation
 *
 * DESCRIPTION:
 * This function and compress_transtabs() in sem_comp.c are strongly
 * related to the implementaion of towctrans, towupper, and towlower.
 * localedef must guarantee that it generates the transformation
 * tables of toupper and tolower in the fixed position, that is,
 * it must keep the following:
 * toupper table is in the 1st entry.
 * tolower table is in the 2nd entry.
 */
/* ARGSUSED1 */
void
add_transformation(_LC_ctype_t *ctype, struct lcbind_table *lcbind_table,
    char *ctype_symbol_name)
{
	extern wchar_t max_wchar_enc;
	item_t *it;
	int	slot;
	int i;
	int do_mask_check;
	unsigned int	from_mask;
	unsigned int	to_mask;

	/* check if array allocated yet - allocate if NULL */
	if (ctype->transname == NULL) {
		ctype->transname = MALLOC(_LC_transnm_t, 32);
		ctype->transtabs = MALLOC(_LC_transtabs_t, 32);
		/*
		 * toupper and tolower always exist in the locale.
		 * So start from 2
		 */
		ctype->ntrans = 2;
	}

	/*
	 * slot0 (index == 1) and slot1 (index == 2) need to be reserved
	 * for toupper and tolower
	 */

	if (strcmp("toupper", ctype_symbol_name) == 0) {
		slot = 0;
	} else if (strcmp("tolower", ctype_symbol_name) == 0) {
		slot = 1;
	} else {
		slot = ctype->ntrans;
		ctype->ntrans++;
	}

	/* lookup existing transname entry and add to it */

	/* allocate transtab vector */
	if (ctype->transtabs[slot].table == NULL) {
		ctype->transtabs[slot].tmax = max_wchar_enc;
		if (((strcmp("toupper", ctype_symbol_name) == 0) ||
		    (strcmp("tolower", ctype_symbol_name) == 0)) &&
		    (ctype->transtabs[slot].tmax < 255))
			ctype->transtabs[slot].tmax = 255;

		ctype->transtabs[slot].tmin = 0;
		ctype->transtabs[slot].table =
		    MALLOC(wchar_t, ctype->transtabs[slot].tmax + 1);
		ctype->transname[slot].name = STRDUP(ctype_symbol_name);
		ctype->transname[slot].index = slot;
	}


	/* set up default translations which is identity */
	for (i = 0; i <= ctype->transtabs[slot].tmax; i++)
		ctype->transtabs[slot].table[i] = i;

	/*
	 * setup work for checking if the characters are in lower and
	 * upper if doing tolower or tolower transformations
	 */
	if (strcmp("toupper", ctype_symbol_name) == 0) {
		do_mask_check = TRUE;
		from_mask = _ISLOWER;
		to_mask   = _ISUPPER;
	} else if (strcmp("tolower", ctype_symbol_name) == 0) {
		do_mask_check = TRUE;
		from_mask = _ISUPPER;
		to_mask   = _ISLOWER;
	} else {
		do_mask_check = FALSE;
		from_mask = 0xffffffff;
		to_mask   = 0xffffffff;
	}

	/* for each range on stack - the min is the FROM pc, and the max is */
	/* the TO pc. */
	while ((it = sem_pop()) != NULL) {
		/*
		 * check if the characters are in lower and upper if
		 * doing tolower or toupper transformations.
		 */
		wchar_t	wc_min, wc_max;

		wc_min = wc_from_fc(it->value.range->min);
		wc_max = wc_from_fc(it->value.range->max);
		if (wc_min < 0 || wc_max < 0) {
			destroy_item(it);
			continue;
		}
		if ((do_mask_check == FALSE) ||
		    ((do_mask_check == TRUE) &&
		    (ctype->mask[wc_min] & from_mask) &&
		    (ctype->mask[wc_max] & to_mask) &&
		    (wc_min <= ctype->transtabs[slot].tmax))) {
			ctype->transtabs[slot].table[wc_min] = wc_max;
		} else
			diag_error(gettext(ERR_TOU_TOL_ILL_DEFINED));

		destroy_item(it);
	}

	/*
	 * Search the translation for the last character that is case sensitive
	 */
	for (i = ctype->transtabs[slot].tmax; i > 0; i--)
		if (i != ctype->transtabs[slot].table[i])
			break;

	ctype->transtabs[slot].tmax = i;

	/* Check to see if there is value greater than tmax */
	for (; i >= 0; i--)
		if (ctype->transtabs[slot].tmax <
		    ctype->transtabs[slot].table[i])
			ctype->transtabs[slot].tmax =
			    ctype->transtabs[slot].table[i];

	/*
	 * Search the translation for the first character that is case sensitive
	 */

	for (i = 0; i <= ctype->transtabs[slot].tmax; i++)
		if (i != ctype->transtabs[slot].table[i])
			break;

	ctype->transtabs[slot].tmin = i;

	/* Check to see if there is a value smaller than tmin */
	for (; i <= ctype->transtabs[slot].tmax; i++)
		if (ctype->transtabs[slot].table[i] <
		    ctype->transtabs[slot].tmin)
			ctype->transtabs[slot].tmin =
			    ctype->transtabs[slot].table[i];
	/*
	 * Do the same for the low end but NOT for "toupper" and "tolower"
	 */
	if (strcmp("toupper", ctype->transname[slot].name) == 0) {
		if (ctype->transtabs[slot].tmax < 255) {
			ctype->transtabs[slot].tmax = 255;
		}
		ctype->transtabs[slot].tmin = 0;
	} else if (strcmp("tolower", ctype->transname[slot].name) == 0) {
		if (ctype->transtabs[slot].tmax < 255) {
			ctype->transtabs[slot].tmax = 255;
		}
		ctype->transtabs[slot].tmin = 0;
	}

	compress_transtabs(ctype, slot);

	if (strcmp("toupper", ctype->transname[slot].name) == 0) {
		ctype->upper = ctype->transtabs[slot].table;
		ctype->max_upper = ctype->transname[slot].tmax;
	} else if (strcmp("tolower", ctype->transname[slot].name) == 0) {
		ctype->lower = ctype->transtabs[slot].table;
		ctype->max_lower = ctype->transname[slot].tmax;
	}
}
