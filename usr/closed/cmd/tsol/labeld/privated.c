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
 *	Label library contract private interfaces.
 *
 *	Binary labels to String labels with dimming word lists.
 *	Dimming word list titles.
 *	Default user labels.
 */

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <syslog.h>

#include <priv.h>
#include <tsol/label.h>
#include <sys/tsol/priv.h>
#include <zone.h>

#undef	NO_CLASSIFICATION
#undef	ALL_ENTRIES
#undef	ACCESS_RELATED
#undef	SHORT_WORDS
#undef	LONG_WORDS

#include "gfi/std_labels.h"

#include "impl.h"

static char *
to_hex(_blevel_impl_t *l)
{
	char *hex;
	char *s;

	switch (l->id) {

	case SUN_SL_ID:
		hex = h_alloc(SUN_SL_ID);
		if ((s = bsltoh_r((bslabel_t *)l, hex)) != NULL)
			return (s);
		h_free(hex);
		break;
	case SUN_CLR_ID:
		hex = h_alloc(SUN_CLR_ID);
		if ((s = bcleartoh_r((bclear_t *)l, hex)) != NULL)
			return (s);
		h_free(hex);
		break;
	default:
		break;
	}
	return (strdup("invalid type"));
}

/*
 *	convert - Convert a parse table to words and dimming lists.
 *
 *	Entry	table = Label table to convert against.  Assumes parse_table
 *			contains a proper parse table from a previous label
 *			conversion.
 *		class = Classification of the label.
 *		min_class = Minimum classification for word names.
 *		min_comps = Minimum compartments set for word names.
 *		max_class = Maximum classification for word names.
 *		max_comps = Maximum compartments set for word names.
 *		full =	TRUE, if update words lists l_word, s_word.
 *			FALSE, if only update word_dimming.
 *
 *	Exit	comps = Index of first compartment word.
 *			size if no compartment words.
 *		marks = Index of first marking word.
 *			size if no marking words.
 *		size = Total number of words.
 *		long_len = Length of long word names.
 *		short_len = Length of short word names.
 *
 *		l_word = Updated, if requested.
 *		s_word = Updated, if requested.
 *		word_dimming = Updated.
 *
 *	Returns TRUE, if conversion complete.
 *		FALSE, if l_visibility
 *
 *	Uses	class_changable, class_visible, l_word, parse_table,
 *			s_word, word_changeable, word_dimming, word_visible.
 *
 *	Calls	COMPARTMENT_MASK_EQUAL, float_il, l_changeability,
 *			l_visibility, strcpy, strlen.
 */

static int
convert(struct l_tables *table, char *parse_table, CLASSIFICATION class,
    _brange_impl_t *bounds, cvt_ret_t *ret, char *buf, uint_t full)
{
	char *class_visible;
	char *class_changeable;
	char *word_visible;
	char *word_changeable;
	char *word_dimming = &buf[ret->dim];

	register int i, j;
	register char *s;
	register char *l;
	int prefix, suffix;

	/* allocate lists */
	if ((class_visible = calloc(l_hi_sensitivity_label->l_classification +
	    1, 2 * sizeof (char))) == NULL) {
		if (debug)
			(void) printf("bcvt: unable to allocate %d class "
			    "tables\n",
			    2 * (l_hi_sensitivity_label->l_classification + 1));
		else
			syslog(LOG_ERR,
			    "bcvt: unable to allocate %d class tables\n",
			    2 * (l_hi_sensitivity_label->l_classification + 1));
	}
	class_changeable = class_visible +
	    (l_hi_sensitivity_label->l_classification + 1);
	if ((word_visible = calloc(table->l_num_entries, 2 * sizeof (char))) ==
	    NULL) {
		if (debug)
			(void) printf("bcvt: unable to allocate %d word "
			    "tables\n", 2 * table->l_num_entries);
		else
			syslog(LOG_ERR,
			    "bcvt: unable to allocate %d word tables\n",
			    2 * table->l_num_entries);
	}
	word_changeable = word_visible + table->l_num_entries;

	/* Get visibility and changeability of words */

	if (debug > 2) {
		char	*l;

		l = to_hex(&(bounds->lower_bound));
		(void) printf("\tvisibility lower_bound = %s,\n", l);
		h_free(l);
		l = to_hex(&(bounds->upper_bound));
		(void) printf("\tvisibility upper_bound = %s,\n", l);
		h_free(l);
	}
	(void) mutex_lock(&gfi_lock);
	if (!l_visibility(class_visible, word_visible, table,
	    (CLASSIFICATION)LCLASS(&bounds->lower_bound),
	    (COMPARTMENTS *)&(bounds->lower_bound._comps),
	    (CLASSIFICATION)LCLASS(&bounds->upper_bound),
	    (COMPARTMENTS *)&(bounds->upper_bound._comps))) {
		(void) mutex_unlock(&gfi_lock);
		free(class_visible);
		free(word_visible);

		if (debug > 1)
			(void) printf("bcvt: l_visibility failed, "
			    "probably out of memory\n");
		return (FALSE);
	}

	(void) l_changeability(class_changeable, word_changeable, parse_table,
	    class, table,
	    (CLASSIFICATION)LCLASS(&bounds->lower_bound),
	    (COMPARTMENTS *)&(bounds->lower_bound._comps),
	    (CLASSIFICATION)LCLASS(&bounds->upper_bound),
	    (COMPARTMENTS *)&(bounds->upper_bound._comps));
	(void) mutex_unlock(&gfi_lock);

	/* Get long visible classifications and set if changeable */

	ret->lwords = ret->dim + l_hi_sensitivity_label->l_classification + 1 +
	    table->l_num_entries;
	l = &buf[ret->lwords];
	j = 0;

	for (i = 0; i <= l_hi_sensitivity_label->l_classification; i++) {
		if (class_visible[i]) {
			if (full) {	/* convert full long word lists */
				(void) strcpy(l, l_long_classification[i]);
				if (debug > 3)
					(void) printf("class [%d] %s", i, l);
				l += strlen(l_long_classification[i]) + 1;
			}
			if (!class_changeable[i])
				word_dimming[j] = CVT_DIM;
			else
				word_dimming[j] = 0;
			if (i == (int)class)
				word_dimming[j] |= CVT_SET;
			if (!full && debug > 3)
				(void) printf("class [%d]", i);
			if (debug > 3)
				(void) printf(" %s | %s\n",
				    (word_dimming[j] & CVT_DIM) ? "dim" : "   ",
				    (word_dimming[j] & CVT_SET) ? "set" : "");
			j++;
		}  /* if (class_visible[i]) */
	}  /* for all classifications */

	/* Get long visible words and set if changeable */

	ret->first_comp = j;
	ret->first_mark = -1;

	for (i = table->l_first_main_entry; i < table->l_num_entries; i++) {
		register struct l_word *word;

		if (word_visible[i]) {
			word = &(table->l_words[i]);
			/* Find first marking */
			if ((ret->first_mark == -1) &&
			    COMPARTMENT_MASK_EQUAL(word->l_w_cm_mask,
			    l_0_compartments)) {
				ret->first_mark = j;
			}
			if (debug > 3)
				(void) printf("word [%d]", j);
			if (full) {	/* convert full word lists */
				/* Long Word */

				/* Prefix ? */
				if ((prefix = word->l_w_prefix) >= 0) {
					(void) strcpy(l,
					    table->l_words[prefix].
					    l_w_output_name);
					if (debug > 3)
						(void) printf(" %s ", l);
					l += strlen(table->l_words[prefix].
					    l_w_output_name);
					/* separate prefix with a blank */
					*l++ = ' ';
				}

				/* Word */
				(void) strcpy(l, word->l_w_output_name);
				if (debug > 3)
					(void) printf(" %s ", l);
				l += strlen(word->l_w_output_name);

				/* Suffix ? */
				if ((suffix = word->l_w_suffix) >= 0) {
					/* separate suffix with a blank */
					*l++ = ' ';
					(void) strcpy(l,
					    table->l_words[suffix].
					    l_w_output_name);
					if (debug > 3)
						(void) printf(" %s ", l);
					l += strlen(table->l_words[suffix].
					    l_w_output_name);
				}
				l++;
			}  /* if (full) */

			if (!word_changeable[i])
				word_dimming[j] = CVT_DIM;
			else
				word_dimming[j] = 0;
			if (parse_table[i])
				word_dimming[j] |= CVT_SET;
			if (debug > 3)
				(void) printf(" %s | %s\n",
				    word_dimming[j]&CVT_DIM?"dim":"   ",
				    word_dimming[j]&CVT_SET?"set":"");

			j++;
		}  /* if (word_visible[i]) */

	}  /* for all words in the table */

	if (ret->first_mark == -1) {
		/* No markings found */

		ret->first_mark = j;
	}
	ret->d_len = j;
	ret->l_len = (uint_t)(l - &buf[ret->lwords]);

	/* now for short words */
	ret->swords = ret->lwords + (bufp_t)(l - &buf[ret->lwords]);
	s = &buf[ret->swords];

	for (i = 0; i <= l_hi_sensitivity_label->l_classification; i++) {
		if (full && class_visible[i]) {
			(void) strcpy(s, l_short_classification[i]);
			s += strlen(l_short_classification[i]) + 1;
		}
	}
	for (i = table->l_first_main_entry; i < table->l_num_entries; i++) {
		register struct l_word *word;

		if (full && word_visible[i]) {
			word = &(table->l_words[i]);

			/* Short Word ? */
			if (word->l_w_soutput_name) {
				/* Prefix ? */
				if ((prefix = word->l_w_prefix) >= 0) {
					if (table->l_words[prefix].
					    l_w_soutput_name) {
						(void) strcpy(s,
						    table->l_words[prefix].
						    l_w_soutput_name);
						s += strlen(
						    table->l_words[prefix].
						    l_w_soutput_name);
					} else {
						(void) strcpy(s,
						    table->l_words[prefix].
						    l_w_output_name);
						s += strlen(
						    table->l_words[prefix].
						    l_w_output_name);
					}
					/* separate prefix with a blank */
					*s++ = ' ';
				}

				/* Short Word */
				(void) strcpy(s, word->l_w_soutput_name);
				s += strlen(word->l_w_soutput_name);

				/* Suffix ? */
				if ((suffix = word->l_w_suffix) >= 0) {
					/* separate suffix with a blank */
					*s++ = ' ';
					if (table->l_words[suffix].
					    l_w_soutput_name) {
						(void) strcpy(s,
						    table->l_words[suffix].
						    l_w_soutput_name);
						s += strlen(
						    table->l_words[suffix].
						    l_w_soutput_name);
					} else {
						(void) strcpy(s,
						    table->l_words[suffix].
						    l_w_output_name);
						s += strlen(
						    table->l_words[suffix].
						    l_w_output_name);
					}
				}  /* if (suffix >= 0) */
				s++;
			} else {
				*s++ = '\0';  /* No Short Name */
			}  /* if short word */
		}  /* if (full && word_visible[i]) */
	}  /* for all words in the table */

	ret->s_len = (uint_t)(s - &buf[ret->swords]);
	return (TRUE);
}  /* convert */


#define	bsccall call->cargs.bslcvt_arg
#define	bscret ret->rvals.bslcvt_ret
/*
 *	slcvt - Convert a Binary Sensitivity Label to a Sensitivity Label
 *		string and return dimming information.
 *
 *	Entry	label = Binary Sensitivity Label to convert.
 *		bounds = Lower and upper bound of the words in name and
 *			 dimming list.
 *			    bounds are promoted to l_lo_sensitivity_label.
 *			    bounds are demoted to caller's Sensitivity Label,
 *			    unless privileged.
 *		flags = GFI flags to use.
 *			LABELS_FULL_CONVERT, if update all words and lists.
 *			otherwise, if convert new label and update dimming list.
 *
 *	Exit	None.
 *
 *	Returns	err = 	0, If successful.
 *			-1, If invalid label.
 *		string = ASCII coded label.
 *		lwords = Array of long word names.
 *		swords = Array of short word names.
 *		l_len = Length of opaque array of long words.
 *		s_len = Length of opaque array of short words.
 *		first_comp = Index of first compartment in words lists.
 *		dim = Dimming list.
 *		d_len = Number of elements in display.
 *
 *	Uses	Admin_low, Admin_high, boundary, l_word, parse_table,
 *			privileged, s_word, admin_high, admin_low,
 *			view_label, word_dimming.
 *
 *	Calls	BCLTOSL, BLDOMINATES, BLEQUAL, BLTYPE, COMPARTMENTS_DOMINATE,
 *			DSL, PSL, VIEW, check_bound, convert, float_client,
 *			initialize, l_convert, strcpy.
 */

void
slcvt(labeld_call_t *call, labeld_ret_t *ret, size_t *len,
    const ucred_t *uc)
{
	_bslabel_impl_t	*label = (_bslabel_impl_t *)&bsccall.label;
	_blevel_impl_t	*low = (_blevel_impl_t *)&bsccall.bounds.lower_bound;
	_blevel_impl_t	*high = (_blevel_impl_t *)&bsccall.bounds.upper_bound;
	ushort_t	flags = get_gfi_flags(bsccall.flags, uc);
	_bslabel_impl_t boundary;
	bslabel_t	*cred_label = ucred_getlabel(uc);
			/* TRUE if check for valid SL  */
	int		check_validity = flags == ALL_ENTRIES;
	int		privileged = (ucred_getzoneid(uc) == GLOBAL_ZONEID);
	const priv_set_t	*pset;
	char		*parse_table;
	char		*label_low_high = NULL;

	boundary = *(_bslabel_impl_t *)cred_label;

	/*
	 * Check privileges of caller for label translation
	 */
	if (!privileged) {
		pset = ucred_getprivset(uc, PRIV_EFFECTIVE);
		privileged = priv_ismember(pset, PRIV_SYS_TRANS_LABEL);
	}

	if (debug > 1) {
		char	*l = to_hex(label);

		(void) printf("labeld op=slcvt:\n");
		(void) printf("\tlabel = %s,\n", l);
		(void) printf("\tflags = %x, gfi_flags = %x\n", bsccall.flags,
		    flags);
		h_free(l);
		l = to_hex(&boundary);
		(void) printf("\tprocess SL (boundary) = %s,\n", l);
		h_free(l);
		l = to_hex(low);
		(void) printf("\tlower_bound = %s,\n", l);
		h_free(l);
		l = to_hex(high);
		(void) printf("\tupper_bound = %s,\n", l);
		h_free(l);
	}

	*len = RET_SIZE(bslcvt_ret_t, 0);
	if (!BLTYPE(label, SUN_SL_ID)) {
		if (debug > 1)
			(void) printf("\tbad SL\n");
		ret->err = -1;
		return;
	}
	if (!(BLTYPE(low, SUN_SL_ID) ||
	    BLTYPE(low, SUN_IL_ID) ||
	    BLTYPE(low, SUN_CLR_ID))) {
		if (debug > 1)
			(void) printf("\tbad lower_bound\n");
		ret->err = -1;
		return;
	}

	if (!(BLTYPE(high, SUN_SL_ID) ||
	    BLTYPE(high, SUN_IL_ID) ||
	    BLTYPE(high, SUN_CLR_ID))) {
		if (debug > 1)
			(void) printf("\tbad upper_bound\n");
		ret->err = -1;
		return;
	}

	/* Special Case Admin High and Admin Low */

	if (BLEQUAL(label, BCLTOSL(&admin_low))) {
		label_low_high = Admin_low;
		PSL(label);
		if (debug > 1) {
			char	*l = to_hex(label);

			(void) printf("\tpromoting admin_low SL to %s,\n", l);
			h_free(l);
		}
	} else if (BLEQUAL(label, BCLTOSL(&admin_high))) {
		label_low_high = Admin_high;
		DSL(label);
		if (debug > 1) {
			char	*l = to_hex(label);

			(void) printf("\tdemoting admin_high SL to %s,\n", l);
			h_free(l);
		}
	}
	if (!check_bounds(uc, label)) {
		ret->err = -1;
		return;
	}
	if (BLEQUAL(&boundary, BCLTOSL(&admin_low))) {
		PSL(&boundary);
		if (debug > 1) {
			char	*l = to_hex(&boundary);

			(void) printf("\tpromoting admin_low boundary to %s,"
			    "\n", l);
			h_free(l);
		}
	} else if (BLEQUAL(&boundary, BCLTOSL(&admin_high))) {
		DSL(&boundary);
		if (debug > 1) {
			char	*l = to_hex(&boundary);

			(void) printf("\tdemoting admin_high boundary to %s,"
			    "\n", l);
			h_free(l);
		}
	}
	/* Promote low and high to lowest SL and demote to boundary. */

	if (!privileged && BLDOMINATES(low, &boundary)) {
		*low = boundary;
		if (debug > 1) {
			char	*l = to_hex(low);

			(void) printf("\tsetting lower_bound to boundary = %s,"
			    "\n", l);
			h_free(l);
		}
	} else if (privileged &&
	    ((LCLASS(low) >= (short)l_hi_sensitivity_label->l_classification) &&
	    COMPARTMENTS_DOMINATE((COMPARTMENTS *)&(low->_comps),
	    l_hi_sensitivity_label->l_compartments))) {
		DSL(low);
		if (debug > 1) {
			char	*l = to_hex(low);

			(void) printf("\tdemoting lower_bound to = %s,\n", l);
			h_free(l);
		}
	}

	if (((short)l_lo_sensitivity_label->l_classification >= LCLASS(high)) &&
	    COMPARTMENTS_DOMINATE(l_lo_sensitivity_label->l_compartments,
	    (COMPARTMENTS *)&(high->_comps))) {
		PSL(high);
		if (debug > 1) {
			char	*l = to_hex(high);

			(void) printf("\tpromoting upper_bound to = %s,\n", l);
			h_free(l);
		}
	} else if (!privileged && BLDOMINATES(&high, &boundary)) {
		*high = boundary;
		if (debug > 1) {
			char	*l = to_hex(high);

			(void) printf("\tsetting upper_bound to boundary = %s,"
			    "\n", l);
			h_free(l);
		}
	} else if (privileged &&
	    ((LCLASS(high) >=
	    (short)l_hi_sensitivity_label->l_classification) &&
	    COMPARTMENTS_DOMINATE((COMPARTMENTS *)&(high->_comps),
	    l_hi_sensitivity_label->l_compartments))) {
		DSL(high);
		if (debug > 1) {
			char	*l = to_hex(high);

			(void) printf("\tdemoting upper_bound to = %s,\n", l);
			h_free(l);
		}
	}
	if (!BLDOMINATES(high, low)) {
		if (debug > 1)
			(void) printf("\tupper_bound doesn't dominate "
			    "lower_bound\n");
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

	/* Convert label and get parse table. */

	bscret.string = 0;
	if ((parse_table = calloc(l_sensitivity_label_tables->l_num_entries,
	    1)) == NULL) {
		if (debug)
			(void) printf("slcvt unable to allocate %d parse table"
			    "\n", l_sensitivity_label_tables->l_num_entries);
		else
			syslog(LOG_ERR,
			    "slcvt unable to allocate %d parse table",
			    l_sensitivity_label_tables->l_num_entries);
	}

	(void) mutex_lock(&gfi_lock);
	if (!l_convert(&bscret.buf[bscret.string],
	    (CLASSIFICATION) LCLASS(label),
	    l_long_classification, (COMPARTMENTS *)&(label->_comps),
	    l_in_markings[(CLASSIFICATION)LCLASS(label)],
	    l_sensitivity_label_tables, parse_table,
	    LONG_WORDS, flags, check_validity, NO_INFORMATION_LABEL)) {
		(void) mutex_unlock(&gfi_lock);
		if (debug > 1)
			(void) printf("\tl_convert failed\n");
		ret->err = -1;
		return;
	}
	(void) mutex_unlock(&gfi_lock);

	/* Special case Admin High and Admin Low names */
	if (label_low_high != NULL) {
		(void) strcpy(&bscret.buf[bscret.string], label_low_high);
	}

	bscret.dim = bscret.string + strlen(&bscret.buf[bscret.string]) + 1;
	if (!convert(l_sensitivity_label_tables, parse_table,
	    (CLASSIFICATION)LCLASS(label),
	    (_brange_impl_t *)&bsccall.bounds, &bscret, bscret.buf,
	    bsccall.flags & LABELS_FULL_CONVERT)) {
		if (debug > 1)
			(void) printf("\tunable to convert to dimming list\n");
		free(parse_table);
		ret->err = -1;
		return;
	}
	free(parse_table);
	*len += bscret.swords + bscret.s_len;
	if (debug > 1) {
		(void) printf("\tstring=%d, dim=%d, lwords=%d, swords=%d\n",
		    bscret.string, bscret.dim, bscret.lwords, bscret.swords);
		(void) printf("\td_len=%d, l_len=%d, s_len=%d, first_comp=%d\n",
		    bscret.d_len, bscret.l_len, bscret.s_len,
		    bscret.first_comp);
	}
}  /* slcvt */
#undef	bsccall
#undef	bscret

#define	bcccall call->cargs.bclearcvt_arg
#define	bccret ret->rvals.bclearcvt_ret
/*
 *	LABELS_BCLEARCONVERT - Convert a Clearance to an ASCII string and return
 *			    dimming information.
 *
 *	Entry	clearance = Clearance to convert.
 *			promote Admin Low to l_lo_clearance.
 *			demote Admin High to l_hi_sensitivity_label.
 *		bounds = Lower and upper bound of the words in name and
 *			 dimming list.
 *			    bounds are promoted to l_lo_clearance.
 *			    bounds are demoted to caller's clearance.
 * *****	boundary unless privileged, then not changed.
 *		flags = GFI flags to use.
 *			LABELS_FULL_CONVERT, if update all words and lists.
 *			otherwise, convert new label and update dimming list.
 *
 *	Exit	None.
 *
 *	Returns	rval = 	0, If invalid label.
 *			1, If successful.
 *		ascii_label = ASCII coded label.
 *		long_words = Array of long word names.
 *		short_words = Array of short word names.
 *		long_len = Length of opaque array of long words.
 *		short_len = Length of opaque array of short words.
 *		first_compartment = Index of first compartment.
 *		display = Dimming list.
 *		display_len = Number of elements in display.
 *
 *	Uses	boundary, clear_high, clear_low, l_word, parse_table,
 *		privileged, s_word, view_label, word_dimming.
 *
 *	Calls	BCLTOSL, BLEQUAL, BLDOMINATES, BLTYPE, COMPARTMENTS_DOMINATE,
 *			DCLR, PCLR, DSL, PSL, VIEW, check_bound, convert,
 *			float_client, initialize, l_convert, strcpy.
 */

void
clearcvt(labeld_call_t *call, labeld_ret_t *ret, size_t *len,
    const ucred_t *uc)
{
	_bclear_impl_t	*clearance = (_bclear_impl_t *)&bcccall.clear;
	_blevel_impl_t	*low = (_blevel_impl_t *)&bcccall.bounds.lower_bound;
	_blevel_impl_t	*high = (_blevel_impl_t *)&bcccall.bounds.upper_bound;
	ushort_t	flags = get_gfi_flags(bcccall.flags, uc);
	_bslabel_impl_t boundary;
	bslabel_t	*cred_label = ucred_getlabel(uc);
			/* TRUE if check for valid CLR  */
	int		check_validity = flags == ALL_ENTRIES;
	int		privileged = (ucred_getzoneid(uc) == GLOBAL_ZONEID);
	const priv_set_t	*pset;
	char		*parse_table;
	char		*clearance_low_high = NULL;

	boundary = *(_bslabel_impl_t *)cred_label;

	/*
	 * Check privileges of caller for label translation
	 */
	if (!privileged) {
		pset = ucred_getprivset(uc, PRIV_EFFECTIVE);
		privileged = priv_ismember(pset, PRIV_SYS_TRANS_LABEL);
	}

	if (debug > 1) {
		char	*l = to_hex(clearance);

		(void) printf("labeld op=clearcvt:\n");
		(void) printf("\tclearance = %s,\n", l);
		h_free(l);
		(void) printf("\tflags = %x, gfi_flags = %x\n", bcccall.flags,
		    flags);
		l = to_hex(&boundary);
		(void) printf("\tprocess SL (boundary) = %s,\n", l);
		h_free(l);
		l = to_hex(low);
		(void) printf("\tlower_bound = %s,\n", l);
		h_free(l);
		l = to_hex(high);
		(void) printf("\tupper_bound = %s,\n", l);
		h_free(l);
	}

	*len = RET_SIZE(bclearcvt_ret_t, 0);
	if (!BLTYPE(clearance, SUN_CLR_ID)) {
		if (debug > 1)
			(void) printf("\tbad CLR\n");
		ret->err = -1;
		return;
	}
	if (!(BLTYPE(low, SUN_SL_ID) ||
	    BLTYPE(low, SUN_IL_ID) ||
	    BLTYPE(low, SUN_CLR_ID))) {
		if (debug > 1)
			(void) printf("\tbad lower_bound\n");
		ret->err = -1;
		return;
	}

	if (!(BLTYPE(high, SUN_SL_ID) ||
	    BLTYPE(high, SUN_IL_ID) ||
	    BLTYPE(high, SUN_CLR_ID))) {
		if (debug > 1)
			(void) printf("\tbad upper_bound\n");
		ret->err = -1;
		return;
	}

	/* Special Case Admin High and Admin Low */

	if (BLEQUAL(clearance, &clear_low)) {
		clearance_low_high = Admin_low;
		/* the minimum clearance is not a valid clearance -- per JPLW */
		check_validity = FALSE;
		PCLR(clearance);
		if (debug > 1) {
			char	*l = to_hex(clearance);

			(void) printf("\tpromoting admin_low CLR to %s,\n", l);
			h_free(l);
		}
	} else if (BLEQUAL(clearance, &clear_high)) {
		clearance_low_high = Admin_high;
		DCLR(clearance);
		if (debug > 1) {
			char	*l = to_hex(clearance);

			(void) printf("\tdemoting admin_high CLR to %s,\n", l);
			h_free(l);
		}
	}
	if (!check_bounds(uc, clearance)) {
		ret->err = -1;
		return;
	}
	if (BLEQUAL(&boundary, BCLTOSL(&admin_low))) {
		PCLR(&boundary);
		if (debug > 1) {
			char	*l = to_hex(&boundary);

			(void) printf("\tpromoting admin_low boundary to %s,\n",
			    l);
			h_free(l);
		}
	} else if (BLEQUAL(&boundary, BCLTOSL(&admin_high))) {
		DCLR(&boundary);
		if (debug > 1) {
			char	*l = to_hex(&boundary);

			(void) printf("\tdemoting admin_high boundary to %s,\n",
			    l);
			h_free(l);
		}
	}
	/* Promote low and high to lowest Clearance and demote to boundary. */

	if (!privileged && BLDOMINATES(low, &boundary)) {
		*low = boundary;
		if (debug > 1) {
			char	*l = to_hex(low);

			(void) printf("\tsetting lower_bound to boundary = %s,"
			    "\n", l);
			h_free(l);
		}
	} else if (privileged &&
	    ((LCLASS(low) >= (short)l_hi_sensitivity_label->l_classification) &&
	    COMPARTMENTS_DOMINATE((COMPARTMENTS *)&(low->_comps),
	    l_hi_sensitivity_label->l_compartments))) {
		DCLR(low);
		if (debug > 1) {
			char	*l = to_hex(low);

			(void) printf("\tdemoting lower_bound to = %s,\n", l);
			h_free(l);
		}
	}

	if (((short)l_lo_clearance->l_classification >= LCLASS(high)) &&
	    COMPARTMENTS_DOMINATE(l_lo_clearance->l_compartments,
	    (COMPARTMENTS *)&(high->_comps))) {
		PCLR(high);
		if (debug > 1) {
			char	*l = to_hex(high);

			(void) printf("\tpromoting upper_bound to %s,\n", l);
			h_free(l);
		}
	} else if (!privileged && BLDOMINATES(&high, &boundary)) {
		*high = boundary;
		if (debug > 1) {
			char	*l = to_hex(high);

			(void) printf("\tsetting upper_bound to boundary %s,"
			    "\n", l);
			h_free(l);
		}
	} else if (privileged &&
	    ((LCLASS(high) >=
	    (short)l_hi_sensitivity_label->l_classification) &&
	    COMPARTMENTS_DOMINATE((COMPARTMENTS *)&(high->_comps),
	    l_hi_sensitivity_label->l_compartments))) {
		DCLR(high);
		if (debug > 1) {
			char	*l = to_hex(high);

			(void) printf("\tdemoting upper_bound to = %s,\n", l);
			h_free(l);
		}
	}
	if (!BLDOMINATES(high, low)) {
		if (debug > 1)
			(void) printf("\tupper_bound doesn't dominate "
			    "lower_bound\n");
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
	    LCLASS(clearance) > l_hi_sensitivity_label->l_classification ||
	    l_long_classification[LCLASS(clearance)] == NULL) {
		if (debug > 1)
			(void) printf("\tbad CLR class\n");
		ret->err = -1;
		return;
	}

	/* Convert clearance and get parse table. */

	bccret.string = 0;
	if ((parse_table = calloc(l_clearance_tables->l_num_entries, 1)) ==
	    NULL) {
		if (debug)
			(void) printf("clearcvt unable to allocate %d parse "
			    "table\n", l_clearance_tables->l_num_entries);
		else
			syslog(LOG_ERR,
			    "clearcvt unable to allocate %d parse table",
			    l_clearance_tables->l_num_entries);
	}

	(void) mutex_lock(&gfi_lock);
	if (!l_convert(&bccret.buf[bccret.string],
	    (CLASSIFICATION)LCLASS(clearance),
	    l_long_classification, (COMPARTMENTS *)&(clearance->_comps),
	    l_in_markings[(CLASSIFICATION)LCLASS(clearance)],
	    l_clearance_tables, parse_table,
	    LONG_WORDS, flags, check_validity, NO_INFORMATION_LABEL)) {
		(void) mutex_unlock(&gfi_lock);
		if (debug > 1)
			(void) printf("\tl_convert failed\n");
		ret->err = -1;
		return;
	}
	(void) mutex_unlock(&gfi_lock);

	/* Special case Admin High and Admin Low names */
	if (clearance_low_high != NULL) {
		(void) strcpy(&bccret.buf[bccret.string], clearance_low_high);
	}

	bccret.dim = bccret.string + strlen(&bccret.buf[bccret.string]) + 1;
	if (!convert(l_clearance_tables, parse_table,
	    (CLASSIFICATION)LCLASS(clearance),
	    (_brange_impl_t *)&bcccall.bounds, &bccret, bccret.buf,
	    bcccall.flags & LABELS_FULL_CONVERT)) {
		if (debug > 1)
			(void) printf("\tunable to convert to dimming list\n");
		free(parse_table);
		ret->err = -1;
		return;
	}
	free(parse_table);
	*len += bccret.swords + bccret.s_len;
	if (debug > 1) {
		(void) printf("\tstring=%d, dim=%d, lwords=%d, swords=%d\n",
		    bccret.string, bccret.dim, bccret.lwords, bccret.swords);
		(void) printf("\td_len=%d, l_len=%d, s_len=%d, first_comp=%d\n",
		    bccret.d_len, bccret.l_len, bccret.s_len,
		    bccret.first_comp);
	}
}  /* clearcvt */
#undef	bcccall
#undef	bccret

#define	lfret ret->rvals.fields_ret
/* ARGSUSED */
void
fields(labeld_call_t *call, labeld_ret_t *ret, size_t *len,
    const ucred_t *uc)
{
	*len = RET_SIZE(ret->rvals, 0);

	lfret.classi = 0;
	(void) strcpy(&lfret.buf[lfret.classi], Class_name);
	lfret.compsi = strlen(Class_name) + 1;
	(void) strcpy(&lfret.buf[lfret.compsi], Comps_name);
	lfret.marksi = lfret.compsi + strlen(Comps_name) + 1;
	(void) strcpy(&lfret.buf[lfret.marksi], Marks_name);
	*len += lfret.marksi + strlen(Marks_name) + 1;

	if (debug > 1) {
		(void) printf("labeld op=fields:\n");
		(void) printf("\tclass buf = %d, \"%s\"\n", lfret.classi,
		    &lfret.buf[lfret.classi]);
		(void) printf("\tcomps buf = %d, \"%s\"\n", lfret.compsi,
		    &lfret.buf[lfret.compsi]);
		(void) printf("\tmarks buf = %d, \"%s\"\n", lfret.marksi,
		    &lfret.buf[lfret.marksi]);
	}
}
#undef	lfret

#define	udret ret->rvals.udefs_ret
/* ARGSUSED */
void
udefs(labeld_call_t *call, labeld_ret_t *ret, size_t *len,
    const ucred_t *uc)
{
	*len = RET_SIZE(udefs_ret_t, 0);

	udret.sl = def_user_sl;
	udret.clear = def_user_clear;

	if (debug > 1)
		(void) printf("labeld op=udefs:\n");
}
#undef	udret
