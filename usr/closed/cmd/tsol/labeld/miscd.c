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
 *	Miscellaneous functions.
 */

#include <stdio.h>

#include <tsol/label.h>

#undef	NO_CLASSIFICATION
#undef	ALL_ENTRIES
#undef	ACCESS_RELATED
#undef	SHORT_WORDS
#undef	LONG_WORDS

#include "gfi/std_labels.h"

#include "impl.h"


#define	DOM(sl, l_sl) \
(((CLASSIFICATION)LCLASS(sl) >= (l_sl)->l_classification) && \
(COMPARTMENTS_DOMINATE((COMPARTMENTS *)&(sl)->_comps, (l_sl)->l_compartments)))

#define	ISDOM(sl, l_sl) \
(((l_sl)->l_classification >= (CLASSIFICATION)LCLASS(sl)) && \
(COMPARTMENTS_DOMINATE((l_sl)->l_compartments, (COMPARTMENTS *)&(sl)->_comps)))


#define	incall call->cargs.inset_arg
#define	inret ret->rvals.inset_ret
/*
 *	inset - Test Sensitivity Label to be in Accreditation Range.
 *
 *	Entry	label = Sensitivity Label to check.
 *		id = SYSTEM_ACCREDITATION_RANGE, system accreditation range.
 *		     USER_ACCREDIATATION_RANGE, user accreditation range.
 *
 *	Exit	None.
 *
 *	Returns	inset =	0, If label is invalid label type, or
 *			   label is not in specified Accreditation Range.
 *			1, If label in specified Accreditation Range.
 *
 *	Calls	BLTYPE, check_bounds, l_in_accreditation_range, l_valid.
 *
 *	Uses	admin_high, admin_low, gfi_lock, l_lo_sensitivity_label,
 *			l_hi_sensitivity_label.
 *
 */

void
inset(labeld_call_t *call, labeld_ret_t *ret, size_t *len,
    const ucred_t *uc)
{
	_bslabel_impl_t *label = (_bslabel_impl_t *)&incall.label;

	*len = RET_SIZE(inset_ret_t, 0);

	if (debug > 1)
		(void) printf("labeld op=inset:\n");

	/*
	 * Validate the label.
	 */

	if (!BLTYPE(label, SUN_SL_ID)) {

		inret.inset = 0;
		return;
	}

	if (!check_bounds(uc, label)) {

		inret.inset = 0;
		return;
	}

	if (incall.type == SYSTEM_ACCREDITATION_RANGE) {
		if (debug > 1) {
			(void) printf("System AR: \n%s\n",
			    bsltoh((bslabel_t *)label));
			(void) printf("l_low = %d, l_hi = %d\n",
			    l_lo_sensitivity_label->l_classification,
			    l_hi_sensitivity_label->l_classification);
		}

		/*
		 * verify:
		 * l_lo_sensitivity_label <= label <= l_hi_sensitivity_label
		 */

		if (!DOM(label, l_lo_sensitivity_label) ||
		    !ISDOM(label, l_hi_sensitivity_label)) {
			inret.inset = 0;
			return;
		}
		if (debug > 1)
			(void) printf("passed dominance\n");

		/* protect against indexing l_in_markings with a bad class */

		if ((CLASSIFICATION)LCLASS(label) >
		    l_hi_sensitivity_label->l_classification ||
		    (CLASSIFICATION)LCLASS(label) < *l_min_classification ||
		    !l_long_classification[(CLASSIFICATION)LCLASS(label)]) {
			inret.inset = 0;
			return;
		}
		(void) mutex_lock(&gfi_lock);
		if (l_valid((CLASSIFICATION)LCLASS(label),
		    (COMPARTMENTS *)&label->_comps,
		    l_in_markings[LCLASS(label)],
		    l_sensitivity_label_tables, ALL_ENTRIES))

			inret.inset = 1;
		else
			inret.inset = 0;
		(void) mutex_unlock(&gfi_lock);
	} else if (incall.type == USER_ACCREDITATION_RANGE) {
		(void) mutex_lock(&gfi_lock);
		if (l_in_accreditation_range((CLASSIFICATION)LCLASS(label),
		    (COMPARTMENTS *)&label->_comps))

			inret.inset = 1;
		else
			inret.inset = 0;
		(void) mutex_unlock(&gfi_lock);
	} else {
		inret.inset = 0;
	}
}  /* inset */
#undef	incall
#undef	inret

#define	slvcall call->cargs.slvalid_arg
#define	slvret ret->rvals.slvalid_ret
/*
 *	slvalid - Test Sensitivity Label to be valid for this system.
 *
 *	Entry	label = Sensitivity Label to check.
 *
 *	Exit	None.
 *
 *	Returns	valid =	0, If label is invalid label type, or
 *			   label is not in valid for this system.
 *			1, If label in valid for this system.
 *
 *	Calls	BLTYPE, check_bounds, l_valid.
 *
 *	Uses	gfi_lock.
 */

void
slvalid(labeld_call_t *call, labeld_ret_t *ret, size_t *len,
    const ucred_t *uc)
{
	_bslabel_impl_t	*label = (_bslabel_impl_t *)&slvcall.label;

	*len = RET_SIZE(slvalid_ret_t, 0);

	if (debug > 1)
		(void) printf("labeld op=slvalid:\n");

	/*
	 * Validate the label.
	 */

	if (!BLTYPE(label, SUN_SL_ID)) {

		slvret.valid = 0;
		return;
	}

	if (!check_bounds(uc, label)) {

		slvret.valid = 0;
		return;
	}

	/* protect against indexing l_in_markings with a bad class */

	if ((CLASSIFICATION)LCLASS(label) >
	    l_hi_sensitivity_label->l_classification ||
	    (CLASSIFICATION)LCLASS(label) < *l_min_classification ||
	    !l_long_classification[(CLASSIFICATION)LCLASS(label)]) {

		slvret.valid = 0;
		return;
	}

	(void) mutex_lock(&gfi_lock);
	if (l_valid((CLASSIFICATION)LCLASS(label),
	    (COMPARTMENTS *)&label->_comps, l_in_markings[LCLASS(label)],
	    l_sensitivity_label_tables, ALL_ENTRIES))

		slvret.valid = 1;
	else
		slvret.valid = 0;

	(void) mutex_unlock(&gfi_lock);
}  /* slvalid */
#undef	slvcall
#undef	slvret

#define	clrvcall call->cargs.clrvalid_arg
#define	clrvret ret->rvals.clrvalid_ret
/*
 *	clearvalid - Test Clearance to be valid for this system.
 *
 *	Entry	clearance = Clearance to check.
 *
 *	Exit	None.
 *
 *	Returns	valid =	0, If label is invalid label type, or
 *			   label is not in valid for this system.
 *			1, If label in valid for this system.
 *
 *	Calls	BLTYPE, check_bounds, l_valid.
 *
 *	Uses	gfi_lock.
 */

void
clearvalid(labeld_call_t *call, labeld_ret_t *ret, size_t *len,
    const ucred_t *uc)
{
	_bclear_impl_t	*clearance = (_bclear_impl_t *)&clrvcall.clear;

	*len = RET_SIZE(clrvalid_ret_t, 0);

	if (debug > 1)
		(void) printf("labeld op=clearvalid:\n");

	/*
	 * Validate the label.
	 */

	if (!BLTYPE(clearance, SUN_SL_ID)) {

		clrvret.valid = 0;
		return;
	}

	if (!check_bounds(uc, clearance)) {

		clrvret.valid = 0;
		return;
	}

	/* protect against indexing l_in_markings with a bad class */

	if ((CLASSIFICATION)LCLASS(clearance) >
	    l_hi_sensitivity_label->l_classification ||
	    (CLASSIFICATION)LCLASS(clearance) < *l_min_classification ||
	    !l_long_classification[(CLASSIFICATION)LCLASS(clearance)]) {

		clrvret.valid = 0;
		return;
	}

	(void) mutex_lock(&gfi_lock);
	if (l_valid((CLASSIFICATION)LCLASS(clearance),
	    (COMPARTMENTS *)&clearance->_comps,
	    l_in_markings[LCLASS(clearance)], l_clearance_tables, ALL_ENTRIES))

		clrvret.valid = 1;
	else
		clrvret.valid = 0;

	(void) mutex_unlock(&gfi_lock);
}  /* clearvalid */
#undef	clrvcall
#undef	clrvret

#define	inforet ret->rvals.info_ret.info
#define	MAX(a, b) (((a) > (b))?(a):(b))
/*
 *	info - Return information about the label encodings database.
 *
 *	Entry	None.
 *
 *	Exit	None.
 *
 *	Returns	label_info structure updated.
 *
 *	Calls	strlen.
 *
 *	Uses	l_hi_sensitivity_label, l_long_classification,
 *		l_alternate_classification, l_short_classification,
 *		l_information_label_tables, l_sensitivity_label_tables,
 *		l_clearance_tables, l_version, l_printer_banner_tables,
 *		l_channel_tables.
 */

/* ARGSUSED */
void
info(labeld_call_t *call, labeld_ret_t *ret, size_t *len, const ucred_t *uc)
{
	int	i;
	int	clen = 0;

	*len = RET_SIZE(info_ret_t, 0);

	if (debug > 1)
		(void) printf("labeld op=info:\n");

	for (i = 0; i <= l_hi_sensitivity_label->l_classification; i++) {
		if (l_long_classification[i] != NULL) {
			clen = MAX(clen, (int)strlen(l_long_classification[i]));
		}
		if (l_short_classification[i] != NULL) {
			clen = MAX(clen,
			    (int)strlen(l_short_classification[i]));
		}
		if (l_alternate_classification[i] != NULL) {
			clen = MAX(clen,
			    (int)strlen(l_alternate_classification[i]));
		}
	}

	inforet.ilabel_len = (short)(l_information_label_tables->l_max_length +
	    1);
	inforet.slabel_len = (short)(l_sensitivity_label_tables->l_max_length +
	    1);
	inforet.clabel_len = (short)(l_information_label_tables->l_max_length +
	    l_sensitivity_label_tables->l_max_length + 4);
	inforet.clear_len  = (short)(l_clearance_tables->l_max_length + 1);
	inforet.vers_len   = (short)(strlen(l_version) + 1);
	inforet.header_len = (short)(clen + 1);
	inforet.protect_as_len = inforet.slabel_len + inforet.ilabel_len - 1;
	inforet.caveats_len = (short)(l_printer_banner_tables->l_max_length +
	    1);
	inforet.channels_len = (short)(l_channel_tables->l_max_length + 1);

	if (debug > 1) {
		(void) printf("\tilen = %d, slen = %d, clen = %d, clear_len = "
		    "%d\n", inforet.ilabel_len, inforet.slabel_len,
		    inforet.clabel_len, inforet.clear_len);
		(void) printf("\tvlen = %d, hlen = %d, plen = %d, calen = %d, "
		    "chlen = %d\n", inforet.vers_len, inforet.header_len,
		    inforet.protect_as_len, inforet.caveats_len,
		    inforet.channels_len);
	}
}  /* info */
#undef	inforet
#undef	MAX

#define	lvret ret->rvals.vers_ret
/*
 *	vers - Return the label encodings database version string.
 *
 *	Entry	None.
 *
 *	Exit	None.
 *
 *	Returns	vers = version string.
 *
 *	Calls	strncpy.
 *
 *	Uses	l_version.
 */

/* ARGSUSED */
void
vers(labeld_call_t *call, labeld_ret_t *ret, size_t *len, const ucred_t *uc)
{
	*len = RET_SIZE(char, strlen(l_version) + 1);

	if (debug > 1)
		(void) printf("labeld op=vers:\n");

	if (debug > 2)
		(void) printf("\t\"%s\"\n", l_version);

	(void) strcpy(lvret.vers, l_version);
}  /* vers */
#undef	lvret

#define	ccall call->cargs.color_arg
#define	cret ret->rvals.color_ret
/*
 *	color - Return the color name for this level's Compartment Word,
 *		exact match, or Classification.
 *
 *	Entry	label = Level to look up.
 *
 *	Exit	err = 1, If invalid label or no color for this Classification.
 *	Returns	color = Color name specified for level's Compartment Word,
 *			exact match, or Classification.
 *
 *	Uses	admin_high, admin_low.
 *
 *	Calls	BLTYPE, check_bounds, strcpy, strlen.
 */

void
color(labeld_call_t *call, labeld_ret_t *ret, size_t *len,
    const ucred_t *uc)
{
	_blevel_impl_t	*level = (_blevel_impl_t *)&ccall.label;
	char	*colorstr;

	*len = RET_SIZE(char, 0);

	if (debug > 1)
		(void) printf("labeld op=color:\n");
	/*
	 * Validate the label.
	 */
	if (!(BLTYPE(level, SUN_SL_ID) ||
	    BLTYPE(level, SUN_IL_ID) ||
	    BLTYPE(level, SUN_CLR_ID))) {
		ret->err = 1;
		return;
	}
	if (!check_bounds(uc, level)) {
		ret->err = 1;
		return;
	}
	/* check if colors for labels defined */
	if (color_table == NULL) {
		ret->err = 1;
		return;
	}
	/* first check for admin_low and admin_high */
	if (BLEQUAL(level, BCLTOSL(&admin_low))) {
		colorstr = low_color;
		if (debug > 2)
			(void) printf("\tadmin_low[%d]=\"%s\"\n",
			    strlen(colorstr) + 1, colorstr);
		goto done;
	} else if (BLEQUAL(level, BCLTOSL(&admin_high))) {
		colorstr = high_color;
		if (debug > 2)
			(void) printf("\tadmin_high[%d]=\"%s\"\n",
			    strlen(colorstr) + 1, colorstr);
		goto done;
	}
	/* check if colors for words defined */
	if (color_word != NULL) {
		cwe_t *word = color_word;
		COMPARTMENTS color_comps;

		do {
			COMPARTMENTS_COPY(&color_comps,
			    (COMPARTMENTS *)&level->_comps);
			COMPARTMENTS_AND(&color_comps, &word->mask);
			if (COMPARTMENTS_EQUAL(&color_comps, &word->comps)) {
				colorstr = word->color;
				if (debug > 1)
					(void) printf("\tword color[%d]=\"%s\""
					    "\n", strlen(colorstr) + 1,
					    colorstr);
				goto done;
			}
		} while ((word = word->next) != NULL);
	}
	/* now check for exact match and default classification */
	if (LCLASS(level) < 0 ||
	    LCLASS(level) > l_hi_sensitivity_label->l_classification ||
	    color_table[LCLASS(level)] == NULL) {
		ret->err = 1;
		return;
	} else {
		cte_t *entry = color_table[LCLASS(level)];

		colorstr = entry->color;
		while (entry != NULL) {
			if (BLEQUAL(level, &entry->level)) {
				colorstr = entry->color;
				break;
			}
			entry = entry->next;
		}  /* while (entry != NULL) */
		if (debug > 2)
			(void) printf("\tlabel color[%d]=\"%s\"\n",
			    strlen(colorstr) + 1, colorstr);
	}
done:
	*len = RET_SIZE(char, strlen(colorstr) + 1);
	(void) strcpy(cret.color, colorstr);
}  /* color */
#undef	ccall
#undef	cret
