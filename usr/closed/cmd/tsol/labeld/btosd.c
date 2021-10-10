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
 *	Convert Binary Labels to String Labels.
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

#define	slcall call->cargs.bsltos_arg
#define	slret ret->rvals.bsltos_ret
/*
 *	sltos - Convert Binary Sensitivity Label to Sensitivity Label string.
 *
 *	Entry	label = Binary Sensitivity Label to convert.
 *		flags = GFI flags to use.
 *			LABELS_NO_CLASS, if don't translate classification.
 *			LABELS_SHORT_CLASS, use short classification names
 *				where defined.
 *			LABELS_SHORT_WORDS, use short names for words where
 *				defined.
 *			GFI_ACCESS_RELATED, if only access related entries to be
 *				converted, otherwise all entries are converted.
 *
 *	Exit	None.
 *
 *	Returns	err = 1, If invalid label.
 *		slabel = Sensitivity Label string.
 *
 *	Uses	Admin_high, Admin_low, admin_high, admin_low, gfi_lock.
 *
 *	Calls	BCLTOSL, BLTYPE, BLEQUAL, DSL, LCLASS, PSL, RET_SIZE, VIEW,
 *		bsltoh_r, check_bounds, get_gfi_flags, h_alloc, h_free,
 *		l_convert, mutex_lock, mutex_unlock, printf, strcpy, strlen.
 */

void
sltos(labeld_call_t *call, labeld_ret_t *ret, size_t *len,
    const ucred_t *uc)
{
	_bslabel_impl_t	*label = (_bslabel_impl_t *)&slcall.label;
	ushort_t	flags = get_gfi_flags(slcall.flags, uc);

	*len = RET_SIZE(char, 0);
	if (debug > 1) {
		char	*hex = h_alloc(SUN_SL_ID);
		char	*sl = bsltoh_r((bslabel_t *)label, hex);

		(void) printf("labeld op=sltos:\n");
		(void) printf("\tlabel = %s,\n",
		    sl != NULL ? sl : "invalid SL type");
		(void) printf("\tflags = %x, gfi_flags = %x\n", slcall.flags,
		    flags);
		h_free(hex);
	}
	if (!BLTYPE(label, SUN_SL_ID)) {
		if (debug > 1)
			(void) printf("\tbad SL\n");
		ret->err = -1;
		return;
	}

	/* Special Case Admin High and Admin Low */
	if (BLEQUAL(label, BCLTOSL(&admin_low))) {
		(void) strcpy(slret.slabel, Admin_low);
	} else if (BLEQUAL(label, BCLTOSL(&admin_high))) {
		(void) strcpy(slret.slabel, Admin_high);
	} else {
		if (!check_bounds(uc, label)) {
			ret->err = -1;
			return;
		}

		/*
		 * Guard against illegal classifications
		 *
		 * Still required because of need for initial markings
		 * when checking validity.
		 */
		if (LCLASS(label) < 0 ||
		    LCLASS(label) > l_hi_sensitivity_label->l_classification ||
		    l_long_classification[LCLASS(label)] == NULL) {
			if (debug > 1)
				(void) printf("\tbad SL class\n");
			ret->err = -1;
			return;
		}
		(void) mutex_lock(&gfi_lock);
		if (!l_convert(slret.slabel, (CLASSIFICATION) LCLASS(label),
		    slcall.flags & LABELS_NO_CLASS ? NO_CLASSIFICATION :
		    (slcall.flags & LABELS_SHORT_CLASS ?
		    l_short_classification : l_long_classification),
		    (COMPARTMENTS *) &(label->_comps),
		    l_in_markings[(CLASSIFICATION) LCLASS(label)],
		    l_sensitivity_label_tables, NO_PARSE_TABLE,
		    (int)(slcall.flags & LABELS_SHORT_WORDS), flags,
		    slcall.flags & LABELS_NO_CLASS ? FALSE :
		    flags == ALL_ENTRIES, NO_INFORMATION_LABEL)) {
			(void) mutex_unlock(&gfi_lock);
			if (debug > 1)
				(void) printf("\tl_convert failed\n");
			ret->err = -1;
			return;
		}
		(void) mutex_unlock(&gfi_lock);
	}
	*len += strlen(slret.slabel) + 1;
	if (debug > 1)
		(void) printf("\tSL len[%d]=%s\n", *len, slret.slabel);
}  /* sltos */
#undef	slcall
#undef	slret

#define	clrcall call->cargs.bcleartos_arg
#define	clrret ret->rvals.bcleartos_ret
/*
 *	cleartos - Convert Binary Clearance to Clearance string.
 *
 *	Entry	clear = Binary Clearance to convert.
 *		flags = GFI flags to use.
 *			LABELS_NO_CLASS, if don't translate classification.
 *			LABELS_SHORT_CLASS, use short classification names
 *				where defined.
 *			LABELS_SHORT_WORDS, use short names for words where
 *				defined.
 *
 *	Exit	None.
 *
 *	Returns	err = 	-1, If invalid label.
 *		cslabel = Clearance string.
 *
 *	Uses	Admin_high, Admin_low, clear_high, clear_low, gfi_lock.
 *
 *	Calls	BCLTOSL, BLTYPE, BLEQUAL, DCLR, LCLASS, PCLR, RET_SIZE, VIEW,
 *		bcleartoh_r, check_bounds, get_gfi_flags, h_alloc, h_free,
 *		l_convert, mutex_lock, mutex_unlock, printf, strcpy, strlen.
 */

void
cleartos(labeld_call_t *call, labeld_ret_t *ret, size_t *len,
    const ucred_t *uc)
{
	_bclear_impl_t	*clearance = (_bclear_impl_t *)&clrcall.clear;
	ushort_t	flags = get_gfi_flags(clrcall.flags, uc);
	int		check_validity = flags == ALL_ENTRIES;

	*len = RET_SIZE(char, 0);
	if (debug > 1) {
		char	*hex = h_alloc(SUN_CLR_ID);
		char	*clr = bcleartoh_r((bclear_t *)clearance, hex);

		(void) printf("labeld op=cleartos:\n");
		(void) printf("\tclear = %s,\n",
		    clr != NULL ? clr : "invalid CLR type");
		(void) printf("\tflags = %x, gfi_flags = %x\n", clrcall.flags,
		    flags);
		h_free(hex);
	}
	if (!BLTYPE(clearance, SUN_CLR_ID)) {
		if (debug > 1)
			(void) printf("\tbad CLR\n");
		ret->err = -1;
		return;
	}

	/* Special Case Admin High and Admin Low */
	if (BLEQUAL(clearance, &clear_low)) {
		(void) strcpy(clrret.cslabel, Admin_low);
	} else if (BLEQUAL(clearance, &clear_high)) {
		(void) strcpy(clrret.cslabel, Admin_high);
	} else {
		if (!check_bounds(uc, clearance)) {
			ret->err = -1;
			return;
		}

		/*
		 * Guard against illegal classifications
		 *
		 * Still required because of need for initial markings
		 * when checking validity.
		 */
		if (LCLASS(clearance) < 0 ||
		    LCLASS(clearance) >
		    l_hi_sensitivity_label->l_classification ||
		    l_long_classification[LCLASS(clearance)] == NULL) {
			if (debug > 1)
				(void) printf("\tbad CLR class\n");
			ret->err = -1;
			return;
		}
		(void) mutex_lock(&gfi_lock);
		if (!l_convert(clrret.cslabel,
		    (CLASSIFICATION)LCLASS(clearance),
		    clrcall.flags & LABELS_NO_CLASS ? NO_CLASSIFICATION :
		    (clrcall.flags & LABELS_SHORT_CLASS ?
		    l_short_classification : l_long_classification),
		    (COMPARTMENTS *)&(clearance->_comps),
		    l_in_markings[(CLASSIFICATION) LCLASS(clearance)],
		    l_clearance_tables, NO_PARSE_TABLE,
		    (int)(clrcall.flags & LABELS_SHORT_WORDS), flags,
		    clrcall.flags & LABELS_NO_CLASS ? FALSE :
		    check_validity, NO_INFORMATION_LABEL)) {
			(void) mutex_unlock(&gfi_lock);
			if (debug > 1)
				(void) printf("\tl_convert failed\n");
			ret->err = -1;
			return;
		}
		(void) mutex_unlock(&gfi_lock);
	}
	*len += strlen(clrret.cslabel) + 1;
	if (debug > 1)
		(void) printf("\tCLR len[%d]=%s\n", *len, clrret.cslabel);
}  /* cleartos */
#undef	clrcall
#undef	clrret
