/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * This file may contain confidential information of the Defense
 * Intelligence Agency and MITRE Corporation and should not be
 * distributed in source form without approval from Sun Legal.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 *	String to binary label translation functions.
 */

#include <tsol/label.h>
#include <priv.h>
#include <zone.h>

#undef	NO_CLASSIFICATION
#undef	ALL_ENTRIES
#undef	ACCESS_RELATED
#undef	SHORT_WORDS
#undef	LONG_WORDS

#include "gfi/std_labels.h"

#include <sys/tsol/label_macro.h>
#include "labeld.h"
#include "impl.h"

#define	slcall call->cargs.stobsl_arg
#define	slret ret->rvals.stobsl_ret
/*
 *	stosl - Translate Sensitivity Label string to a Binary Sensitivity
 *		Label.
 *
 *	Entry	string = string to translate.
 *		label = Sensitivity Label to be updated.
 *		flags = LABELS_NEW_LABEL, create new Sensitivity Label
 *					  (initially Admin Low).
 *			LABELS_FULL_PARSE, create new Sensitivity Label
 *					   (initially Admin Low) and
 *					   translate without error correction.
 *			otherwise, modify existing Sensitivity Label.
 *
 *	Exit	None.
 *
 *	Returns	err = 	-1, If invalid label.
 *			0, If successful.
 *			Character offset in string of error.
 *		label = Updated.
 *
 *	Uses	Admin_high, Admin_low, boundary, admin_high, admin_low.
 *
 *	Calls	BCLTOSL, BCLUNDEF, BLEQUAL, BSLHIGH, BSLLOW,
 *			BLTYPE, DSL, IS_ADMIN_HIGH, IS_ADMIN_LOW, SETBSLABEL,
 *			check_bound, initialize, isspace, l_parse.
 *
 */

void
stosl(labeld_call_t *call, labeld_ret_t *ret, size_t *len,
    const ucred_t *uc)
{
	_bslabel_impl_t	*slabel = (_bslabel_impl_t *)&slcall.label;
	bslabel_t	auto_label;
	_blevel_impl_t	*upper_bound = (_blevel_impl_t *)&auto_label;
	bslabel_t	*cred_label = ucred_getlabel(uc);
	int		l_ret;
	int		sl_low = 0;
	CLASSIFICATION	slclass;
	register char	*p = slcall.string;	/* scan pointer */
	register char	*s;

	*len = RET_SIZE(stobsl_ret_t, 0);

	auto_label = *cred_label;

	if (debug > 1) {
		(void) printf("labeld op=stosl:\n");
		(void) printf("\tstring = %s, flags = %x\n", slcall.string,
		    slcall.flags);
	}

	if (slcall.flags & (LABELS_NEW_LABEL | LABELS_FULL_PARSE)) {
		BSLLOW(slabel);
	} else if (!BLTYPE(slabel, SUN_SL_ID)) {
		ret->err = -1;
		return;
	}

	/* Special Case Admin High and Admin Low */

	while (isspace(*p))
		p++;

	if (*p == '[') {	/* accept leading '[' */
		*p = ' ';
		p++;
		while (isspace(*p))
			p++;
	}
	s = p;

	while (*p != '\0' && *p != ']') {	/* accept trailing ']' */
		p++;
	}
	*p = '\0';

	if (IS_ADMIN_LOW(s)) {
		BSLLOW((_bslabel_impl_t *)&slret.label);
		goto finish;
	} else if (IS_ADMIN_HIGH(s)) {
		BSLHIGH((_bslabel_impl_t *)&slret.label);
		goto finish;
	}

	/* check label to update */

	if (!check_bounds(uc, slabel)) {
		ret->err = -1;
		return;
	}

	/* Guard against illegal classifications */

	if (BLEQUAL(slabel, BCLTOSL(&admin_low))) {

		sl_low = 1;
		if (slcall.flags & LABELS_FULL_PARSE)
			LCLASS_SET(slabel, FULL_PARSE);
		else
			LCLASS_SET(slabel, NO_LABEL);
	} else if (BLEQUAL(slabel, BCLTOSL(&admin_high))) {

		/* Map Admin High to highest class and compartments */
		DSL(slabel);
	}

	/* Translate Admin Low and Admin High Bounds */


	if (BLEQUAL(upper_bound, BCLTOSL(&admin_low)) &&
	    (ucred_getzoneid(uc) != GLOBAL_ZONEID)) {

		if (debug > 1)
			(void) printf("\tbound == admin_low && ! priv\n");
		/*
		 * Return string in error, no label other than Admin Low
		 * is possible.
		 */

		ret->err = 1;
		return;
	} else if ((ucred_getzoneid(uc) == GLOBAL_ZONEID) ||
	    BLEQUAL(upper_bound, BCLTOSL(&admin_high))) {
		if (debug > 1)
			(void) printf("\tDSL(upper_bound)\n");
		DSL(upper_bound);
	} else if (debug > 1) {
			(void) printf("\tupper_bound = proc sl\n");
	}

	slclass = LCLASS(slabel);
	(void) mutex_lock(&gfi_lock);
	if ((l_ret = l_parse(slcall.string,
	    (CLASSIFICATION *) &slclass, (COMPARTMENTS *) &slabel->_comps,
	    l_t_markings,
	    l_sensitivity_label_tables,
	    l_lo_sensitivity_label->l_classification,
	    l_lo_sensitivity_label->l_compartments,
	    (CLASSIFICATION) LCLASS(upper_bound),
	    (COMPARTMENTS *) &upper_bound->_comps)) != L_GOOD_LABEL) {
		(void) mutex_unlock(&gfi_lock);
		/* translation error */

		if (debug > 1)
			(void) printf("l_parse error %d\n", l_ret);
		switch (l_ret) {
		case L_BAD_CLASSIFICATION:
			ret->err = -1;
			break;

		case L_BAD_LABEL:
		case 0:
			ret->err = 1;
			break;

		default:
			ret->err = l_ret;
		}
		return;
	}
	(void) mutex_unlock(&gfi_lock);

	LCLASS_SET(slabel, slclass);

	/*
	 * If SL class is still NO_LABEL or FULL_PARSE, there was no valid
	 * label in the string, return entire string in error.
	 */

	if (LCLASS(slabel) == NO_LABEL || LCLASS(slabel) == FULL_PARSE) {
		if (!sl_low) {

			ret->err = 1;
			return;
		}
		BSLLOW(slabel);
	}
	slret.label = *(bslabel_t *)slabel;
finish:
	if (debug > 1) {
		char    *hex = h_alloc(SUN_SL_ID);
		char    *sl = bsltoh_r(&slret.label, hex);

		(void) printf("\tlabel = %s,\n",
		    sl != NULL ? sl : "invalid SL type");
		h_free(hex);
	}
}  /* stosl */
#undef	slcall
#undef	slret

#define	clrcall call->cargs.stobclear_arg
#define	clrret ret->rvals.stobclear_ret
/*
 *	stoclear - Translate Clearance string to a Binary Clearance.
 *
 *	Entry	string = string to translate.
 *		clear = Clearance to be updated.
 *		flags = LABELS_NEW_LABEL, create new Clearnace
 *					  (initially Admin Low).
 *			LABELS_FULL_PARSE, create new Clearnace
 *					   (initially Admin Low) and
 *					   translate without error correction.
 *			otherwise, modify existing Clearnace.
 *
 *	Exit	None.
 *
 *	Returns	err = 	-1, If invalid label.
 *			0, If successful.
 *			Character offset in string of error.
 *		clear = Updated.
 *
 *	Uses	Admin_high, Admin_low, boundary, admin_high, admin_low.
 *
 *	Calls	BCLTOSL, BCLUNDEF, BLEQUAL, BSLHIGH, BSLLOW,
 *			BLTYPE, DSL, IS_ADMIN_HIGH, IS_ADMIN_LOW, SETBSLABEL,
 *			check_bound, initialize, isspace, l_parse.
 *
 */

void
stoclear(labeld_call_t *call, labeld_ret_t *ret, size_t *len,
    const ucred_t *uc)
{
	_bclear_impl_t	*clear = (_bclear_impl_t *)&clrcall.clear;
	bslabel_t	auto_label;
	_blevel_impl_t	*upper_bound = (_blevel_impl_t *)&auto_label;
	bslabel_t	*cred_label = ucred_getlabel(uc);
	int		l_ret;
	int		clr_low = 0;
	CLASSIFICATION	clrclass;
	register char	*p = clrcall.string;	/* scan pointer */

	*len = RET_SIZE(stobclear_ret_t, 0);

	auto_label = *cred_label;

	if (debug > 1) {
		(void) printf("labeld op=stoclear:\n");
		(void) printf("\tstring = %s, flags = %x\n", clrcall.string,
		    clrcall.flags);
	}

	if (clrcall.flags & (LABELS_NEW_LABEL | LABELS_FULL_PARSE)) {
		BCLEARLOW(clear);
	} else if (!BLTYPE(clear, SUN_CLR_ID)) {
		ret->err = -1;
		return;
	}

	/* Special Case Admin High and Admin Low */

	while (isspace(*p))
		p++;

	if (IS_ADMIN_LOW(p)) {
		BCLEARLOW((_bclear_impl_t *)&clrret.clear);
		goto finish;
	} else if (IS_ADMIN_HIGH(p)) {
		BCLEARHIGH((_bclear_impl_t *)&clrret.clear);
		goto finish;
	}

	/* check label to update */

	if (!check_bounds(uc, clear)) {
		ret->err = -1;
		return;
	}

	/* Guard against illegal classifications */

	if (BLEQUAL(clear, BCLTOSL(&admin_low))) {

		clr_low = 1;
		if (clrcall.flags & LABELS_FULL_PARSE)
			LCLASS_SET(clear, FULL_PARSE);
		else
			LCLASS_SET(clear, NO_LABEL);
	} else if (BLEQUAL(clear, &clear_high)) {

		/* Map Admin High to highest class and compartments */

		DCLR(clear);
	}

	/* Translate Admin Low and Admin High Bounds */

	if (BLEQUAL(upper_bound, BCLTOSL(&admin_low)) &&
	    (ucred_getzoneid(uc) != GLOBAL_ZONEID)) {

		if (debug > 1)
			(void) printf("\tbound == admin_low && ! priv\n");
		/*
		 * Return string in error, no label other than Admin Low
		 * is possible.
		 */

		ret->err = 1;
		return;

	} else if ((ucred_getzoneid(uc) == GLOBAL_ZONEID) ||
	    BLEQUAL(upper_bound, BCLTOSL(&admin_high))) {
		if (debug > 1)
			(void) printf("\tDSL(upper_bound)\n");
		DSL(upper_bound);
	} else if (debug > 1) {
			(void) printf("\tupper_bound = proc sl\n");
	}

	clrclass = LCLASS(clear);
	(void) mutex_lock(&gfi_lock);
	if ((l_ret = l_parse(clrcall.string,
	    (CLASSIFICATION *) &clrclass, (COMPARTMENTS *) &clear->_comps,
	    l_t_markings,
	    l_clearance_tables,
	    l_lo_clearance->l_classification,
	    l_lo_clearance->l_compartments,
	    (CLASSIFICATION) LCLASS(upper_bound),
	    (COMPARTMENTS *) &upper_bound->_comps)) != L_GOOD_LABEL) {
		(void) mutex_unlock(&gfi_lock);
		/* translation error */

		if (debug > 1)
			(void) printf("l_parse error %d\n", l_ret);
		switch (l_ret) {
		case L_BAD_CLASSIFICATION:
			ret->err = -1;
			break;

		case L_BAD_LABEL:
		case 0:
			ret->err = 1;
			break;

		default:
			ret->err = l_ret;
		}
		return;
	}
	(void) mutex_unlock(&gfi_lock);

	LCLASS_SET(clear, clrclass);

	/*
	 * If SL class is still NO_LABEL or FULL_PARSE, there was no valid
	 * label in the string, return entire string in error.
	 */

	if (LCLASS(clear) == NO_LABEL || LCLASS(clear) == FULL_PARSE) {
		if (!clr_low) {

			ret->err = 1;
			return;
		}
		BCLEARLOW(clear);
	}

	clrret.clear = *(bclear_t *)clear;
finish:
	if (debug > 1) {
		char    *hex = h_alloc(SUN_CLR_ID);
		char    *clr = bcleartoh_r(&clrret.clear, hex);

		(void) printf("\tclearance = %s,\n", clr != NULL ? clr :
		    "invalid CLR type");
		h_free(hex);
	}
}  /* stoclear */
#undef	clrcall
#undef	clrret
