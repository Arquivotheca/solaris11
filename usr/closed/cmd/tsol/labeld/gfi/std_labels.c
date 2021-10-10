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

#ifndef lint
static char    *l_ID = "CMW Labels Release 2.2.1; 11/24/93: std_labels.c";
#endif	/* lint */

#include "std_labels.h"
#ifdef	TSOL
#include <limits.h>
#include <wctype.h>
#include <widec.h>
#endif	/* TSOL */

/*
 * The subroutines contained herein use table-driven specifications of label
 * semantics to perform conversion of information labels, sensitivity labels,
 * and clearances between human-readable and internal bit encoding formats,
 * and to produce other labeling information needed for labeling printed
 * output.
 *
 * This file contains both external subroutines and internal subroutines used by
 * the external subroutines.  All external (global) names used by these
 * routines begin with "l_" to hopefully distinguish them from other external
 * names with which they may be compiled.  Although long (greater than 6
 * character) names are used throughout, all external names are unique in
 * their first 6 characters.
 *
 * A companion subroutine to those in this file is l_init, which appears in the
 * file l_init.c.  l_init reads a human-readable representation of the label
 * encoding tables from a file and converts them into the internal format
 * needed by the other subroutines.
 *
 * The external subroutines contained herein are l_parse, l_convert, l_b_parse,
 * l_b_convert, l_in_accreditation_range, l_valid, l_changeability, and
 * l_visibility.
 *
 * l_parse takes a human-readable representation of a single label (information,
 * sensitivity, or clearance) and converts it into the internal format.
 *
 * l_convert takes the internal representation of a single label and converts it
 * into its canonicalized human-readable form.
 *
 * l_b_parse uses l_parse to convert a banner than consists of a combination of
 * information and sensitivity labels in the standard human-readable format
 * of:
 *
 *		INFORMATION LABEL [SENSITIVITY LABEL]
 *
 * into the internal representation of the labels.
 *
 * l_b_convert uses l_convert to convert the internal representation of an
 * information and sensitivity label into the human-readable banner shown
 * above.
 *
 * l_in_accreditation_range determines whether or not a sensitivity label is in
 * the system's accreditation range.
 *
 * l_valid checks the internal representation of a label for validity.
 *
 * l_changeability and l_visibility provide information useful in producing a
 * graphical user interface for label changing.
 *
 * A more complete description of the usage of each subroutine appears before
 * the subroutine below.  The supporting internal subroutines appear below
 * before the external ones that use them.
 *
 * All of these routines require that the encodings set by l_init be initialized
 * before they can operate.  Therefore, each of these routines calls the
 * external subroutine l_encodings_initialized, whose job it is to ensure
 * that the variables have been initialized.  If l_encodings_initialized
 * returns FALSE, then these routines return without functioning, with an
 * appropriate error return.
 *
 * All of these subroutines are designed to operate on compartment and marking
 * bits strings that can be defined to be any length.  The definitions of how
 * compartments and markings are stored and operated on appear in the file
 * std_labels.h.  It is possible to change the number of compartment or
 * marking bits that these subroutines support by changing these definitions
 * and recompiling these subroutines.  Furthermore, it is possible to change
 * the definitions in std_labels.h such that the size of compartments are
 * markings are taken at execution time from global variables.  This is
 * possible because ALL operations on compartments and markings are done via
 * these definitions, and because these subroutines deal only with pointers
 * to these values.
 *
 * std_labels.h also contains the declarations of a number of external variables
 * that represent the conversion tables and system parameters read from the
 * encodings file by l_init.  These tables allow the same subroutines below
 * to work on either information, sensitivity, or clearance labels depending
 * on which l_tables are passed to the subroutines.
 *
 * These routines pass the internal form of classifications, compartments, and
 * markings as separate arguments rather than assuming the are combined in
 * any particular structure.  Furthermore, compartments and markings are
 * always passed with pointers (by reference) rather than by value.
 *
 * Note that these subroutines contain a couple of features to support the
 * construction of a "multiple choice" graphical interface for changing
 * labels. These features are noted below in the description of each
 * subroutine.
 */

/*
 * Define two needed variables to be the global temporary second compartment
 * and markings variables.  These variables are needed by the
 * make_parse_table and l_valid subroutines.  The first temporary variables
 * (l_t_compartments and l_t_markings) are used by l_b_parse, which
 * eventually calls make_parse_table, so that the first temporaries can't be
 * used for this purpose also.
 */

#define	comps_handled l_t2_compartments	/* compartments handled */
#define	marks_handled l_t2_markings	/* markings handled */

/*
 * The internal subroutine make_parse_table fills in a parse table based on
 * the passed classification, compartments, markings, and label tables.  A
 * parse table is a character array with each character corresponding to an
 * entry in the l_words table.  The character at index i of the parse table
 * is TRUE iff the word represented by the ith entry in the l_words table is
 * represented by the classification, compartments, and markings.  The caller
 * must assure that the passed parse table contains l_tables->l_num_entries
 * entries (and therefore correlates in size with the passed l_words table in
 * l_tables).  The flags argument specifies which l_word entries are to be
 * considered when scanning the l_word table.  If flags is ALL_ENTRIES, all
 * entries are considered.  Otherwise, only those entries whose l_w_flags
 * have all bits in flags on are considered.
 */

static void
make_parse_table(parse_table, class, comps_ptr, marks_ptr, l_tables, flags)
    char		*parse_table;	/* the parse table to be filled in */
    CLASSIFICATION	class;		/* the label classification */
    COMPARTMENTS	*comps_ptr;	/* the label compartments */
    MARKINGS		*marks_ptr;	/* the label markings */
    register struct l_tables *l_tables;	/* the encoding tables to use */
    short		flags;		/* l_w_flags of l_word entries to use */
{
    register int    i;		/* loop index for searching l_words */

    /*
     * Initialize set of compartments and markings we have already handled by
     * producing parse table entries that cover them.
     */

    COMPARTMENT_MASK_ZERO(comps_handled);
    MARKING_MASK_ZERO(marks_handled);

    /*
     * Loop through each word in the word table to determine whether this
     * word is indicated by the passed compartments and markings.  If so, put
     * the word in the parse table only if all the compartments indicated by
     * the word have not been already handled or not all the markings
     * indicated have been already handled.  Entries whose l_w_flags
     * components do not match the passed flags are ignored completely.
     */

#ifndef	TSOL
    for (i = l_tables->l_first_main_entry; i < l_tables->l_num_entries; i++) {
	if ((l_tables->l_words[i].l_w_flags & flags) != flags)
	    continue;
	if (COMPARTMENTS_IN(comps_ptr,
			    l_tables->l_words[i].l_w_cm_mask,
			    l_tables->l_words[i].l_w_cm_value))
	    if (MARKINGS_IN(marks_ptr,
			    l_tables->l_words[i].l_w_mk_mask,
			    l_tables->l_words[i].l_w_mk_value))
		if (!COMPARTMENT_MASK_IN(l_tables->l_words[i].l_w_cm_mask,
					 comps_handled)
		    || !MARKING_MASK_IN(l_tables->l_words[i].l_w_mk_mask,
					marks_handled))
		    /*
		     * We now know this word is indicated by the compartments
		     * and markings passed. However, the word should only be
		     * output if it is appropriate at the label's output
		     * minimum and output maximum classification.
		     */

		    if (class >= l_tables->l_words[i].l_w_output_min_class
		        && class <= l_tables->l_words[i].l_w_output_max_class) {

			/*
			 * Since this word is to be handled, mark its index
			 * TRUE in the parse table, and record that we have
			 * handled its compartments and markings.
			 */

			parse_table[i] = TRUE;
			COMPARTMENT_MASK_COMBINE(comps_handled,
					  l_tables->l_words[i].l_w_cm_mask);
			MARKING_MASK_COMBINE(marks_handled,
					  l_tables->l_words[i].l_w_mk_mask);
			continue;	/* in loop */
		    }
	parse_table[i] = FALSE;	/* if any if above not satisfied */
    }
#else	/* TSOL */
    for (i = l_tables->l_first_main_entry; i < l_tables->l_num_entries; i++) {
	if ((l_tables->l_words[i].l_w_flags & flags) != flags) {
	    parse_table[i] = FALSE;	/* if not included */
	    continue;
	}
	/* Account for possible NULL Information Label. */
	if (COMPARTMENTS_IN(comps_ptr, l_tables->l_words[i].l_w_cm_mask,
	    l_tables->l_words[i].l_w_cm_value) &&
	    !COMPARTMENT_MASK_IN(l_tables->l_words[i].l_w_cm_mask,
	    comps_handled) &&
	    class >= l_tables->l_words[i].l_w_output_min_class &&
	    class <= l_tables->l_words[i].l_w_output_max_class) {

		/*
		 * We now know this word is indicated by the compartments
		 * passed.  And, the word should be output because it is
		 * appropriate at the label's output minimum and output
		 * maximum classification.
		 */
		 if (marks_ptr == NULL) {

			/*
			 * Since this word is to be handled, mark its index
			 * TRUE in the parse table, and record that we have
			 * handled its compartments.
			 */

			parse_table[i] = TRUE;
			COMPARTMENT_MASK_COMBINE(comps_handled,
			    l_tables->l_words[i].l_w_cm_mask);
			continue;	/* in loop */
		} else if (MARKINGS_IN(marks_ptr,
		    l_tables->l_words[i].l_w_mk_mask,
		    l_tables->l_words[i].l_w_mk_value) &&
		    (!COMPARTMENT_MASK_IN(l_tables->l_words[i].l_w_cm_mask,
		    comps_handled) ||
		    !MARKING_MASK_IN(l_tables->l_words[i].l_w_mk_mask,
		    marks_handled))) {
			/*
			 * Since this word is to be handled, mark its index
			 * TRUE in the parse table, and record that we have
			 * handled its compartments and markings.
			 */

			parse_table[i] = TRUE;
			COMPARTMENT_MASK_COMBINE(comps_handled,
			    l_tables->l_words[i].l_w_cm_mask);
			MARKING_MASK_COMBINE(marks_handled,
			    l_tables->l_words[i].l_w_mk_mask);
			continue;	/* in loop */
		}
	}
	parse_table[i] = FALSE;	/* if any if above not satisfied */
    }
#endif	/* !TSOL */
}

/*
 * The internal subroutine word_forced_on returns true if the word indicated
 * by indx in the passed l_tables is forced on because of the passed
 * min_comps, max_comps, or the initial compartments and/or markings of the
 * passed class. TRUE is returned if the word is forced on, otherwise FALSE
 * is returned.
 */

static int
word_forced_on(indx, l_tables, min_comps_ptr, max_comps_ptr, class)
    register int    indx;
    register struct l_tables *l_tables;
    COMPARTMENTS   *min_comps_ptr;
    COMPARTMENTS   *max_comps_ptr;
    register CLASSIFICATION class;
{

    /*
     * If this word is a SPECIAL_INVERSE word, then it can never be forced on
     * for any of the reasons checked for below for other words.  Therefore,
     * just return indicating that the word is not forced on.
     */

    if (l_tables->l_words[indx].l_w_type & SPECIAL_INVERSE)
	return (FALSE);

    /*
     * If the word has no compartments specified, and has no markings
     * specified, then return FALSE, because such a word is for input only,
     * and can never appear in a label, and can therefore not ever be forced
     * on.
     */

    if (!(l_tables->l_words[indx].l_w_type & HAS_COMPARTMENTS)) {
	if (l_tables->l_words[indx].l_w_type & HAS_ZERO_MARKINGS)
	    return (FALSE);
    } else {

	/*
	 * If this word is a compartment, then if it's also inverse and
	 * specified by the max_comps, then return TRUE.
	 */

	if (l_tables->l_words[indx].l_w_type & COMPARTMENTS_INVERSE) {
	    /* if word has inverse compartment bit */
	    if (l_tables->l_words[indx].l_w_type & HAS_ZERO_MARKINGS) {
		/* w/o marking bits present */
		if (COMPARTMENTS_IN(max_comps_ptr,
				    l_tables->l_words[indx].l_w_cm_mask,
				    l_tables->l_words[indx].l_w_cm_value))
		    /* indicated by max_comps */
		    return (TRUE);
	    }

	/*
	 * If this word is a normal (non-inverse) compartment and is
	 * specified by the min_comps, then return TRUE.  To determine
	 * whether word is specified by the min_comps, compute in
	 * l_t5_compartments those non-inverse bits in this word that are
	 * also in the min_comps.  If there are any, then this word is forced
	 * on.
	 */

	} else {  /* compartment is normal */
	    COMPARTMENTS_COPY(l_t5_compartments, min_comps_ptr);
	    COMPARTMENTS_AND(l_t5_compartments,
			     l_tables->l_words[indx].l_w_cm_value);
	    COMPARTMENTS_COMBINE(l_t5_compartments, l_iv_compartments);
	    COMPARTMENTS_XOR(l_t5_compartments, l_iv_compartments);
	    if (!COMPARTMENTS_EQUAL(l_t5_compartments, l_0_compartments))
		return (TRUE);
	}
    }

    /*
     * If this word is specified by the initial compartments and/or markings,
     * then return (TRUE).
     */

    if ((class != NO_LABEL) &&
	COMPARTMENTS_IN(l_in_compartments[class],
	l_tables->l_words[indx].l_w_cm_mask,
	l_tables->l_words[indx].l_w_cm_value) &&
	MARKINGS_IN(l_in_markings[class],
	l_tables->l_words[indx].l_w_mk_mask,
	l_tables->l_words[indx].l_w_mk_value))
	    /* word indicated by initial comps and marks */
	    return (TRUE);

    return (FALSE);		/* if word not forced on */
}

#define	TURN_ON 0		/* flag argument value for normal case in
				 * label changing */
#define	TURN_OFF 0		/* flag argument value for normal case in
				 * label changing */
#define	CHECK_ONLY 1		/* flag argument value to check changeability */
#define	RECURSING 2		/* flag argument value for use when recursing
				 * ONLY */
#define	FORCE_OFF_BY_TURNON_WORD 3	/* flag argument for turnon_word's
					 * call to turnoff_word */
#define	L_FORCED_BY_TURNON_WORD 2	/* flag in parse table sez word was
					 * FORCED ON or OFF by another word */
#define	L_OK 4			/* flag in parse table sez is OK to be on
				 * given a constraint */
#define	L_FORCED_BY_TURNON_FORCED_WORDS 8  /* flag in parse table sez word
					    * was FORCED ON by another word */
#define	L_FORCED_BY_TURNOFF_WORD 16	/* flags in parse table sez word was
					 * FORCED ON or OFF */
/*
 * The subroutine turnoff_word, called by l_parse and turnon_word (below),
 * turns off the indicated word in the passed  parse_table, unless the word
 * was 1) required by some other word that is turned on in the parse_table,
 * 2) is an inverse compartment forced on by the max_comps , 3) is a non-
 * inverse compartment forced on by the min_comps, or 4) is a word whose
 * presence is indicated by the initial compartments and markings. TRUE is
 * returned if the word was turned off, else FALSE is returned.  Also, if the
 * word is turned off, the words_max_class, which is the maximum class
 * allowed by the words that are on, is adjusted to account for the removal
 * of the word; therefore, it may be raised if the word removed was the
 * reason for its being lower than the max_class.
 *
 * If the argument flag is CHECK_ONLY, then turnoff_word merely checks whether
 * the word can be turned off, but does not actually turn it off or update
 * words_max_class.  Otherwise the flag argument should be TURN_OFF, unless
 * turnoff_word is recursing, in which case the flag should be RECURSING.
 */

static int
turnoff_word(l_tables, parse_table, indx, class, min_comps_ptr,
	     words_max_class_ptr, max_class, max_comps_ptr, flag)
    register struct l_tables *l_tables;	/* the encoding tables */
    char           *parse_table;	/* the parse table to change */
    register int    indx;	/* the parse table index to turn off */
    CLASSIFICATION  class;	/* the current classification for this word */
    COMPARTMENTS   *min_comps_ptr;	/* the minimum allowable compartments */
    CLASSIFICATION *words_max_class_ptr;	/* ptr to max class specified
						 * by words on */
    CLASSIFICATION  max_class;	/* the maximum classification whose existence
				 * to acknowledge */
    COMPARTMENTS   *max_comps_ptr;	/* the maximum allowable compartments */
    int             flag;	/* flag sez only check whether word can be
				 * turned off */
{
    register struct l_word_pair *wp;
    register int    i;
    CLASSIFICATION  original_words_max_class;

    if (parse_table[indx] & TRUE) {	/* if this word is on... */

	/*
	 * Check the required combinations to see if this word cannot be
	 * turned off because it is required by another word that is on.  If
	 * it cannot be turned off, return FALSE.
	 */

	for (wp = l_tables->l_required_combinations;
	     wp != l_tables->l_end_required_combinations; wp++)
	    if (indx == wp->l_word2  /* if word to turn off was required... */
		&& parse_table[wp->l_word1] & TRUE)	/* and the word that
							 * required it is on */
		return (FALSE);	/* then don't turn off this word */

	/*
	 * If this word is forced ON (as checked by word_forced_on above),
	 * return FALSE, because the word cannot be turned off.
	 */

	if (word_forced_on(indx, l_tables, min_comps_ptr, max_comps_ptr, class))
	    return (FALSE);

	/*
	 * So far, the word can be turned off.  Now make special checks for
	 * SPECIAL_INVERSE words, after saving the original value of
	 * words_max_class, in case it must be restored later if the word
	 * can't be turned off.
	 */

	if (l_tables->l_words[indx].l_w_type & SPECIAL_INVERSE) {
	    original_words_max_class = *words_max_class_ptr;

	    /*
	     * Check whether this word has inverse compartments, and whether
	     * turning it off will raise the level above the max_comps.
	     */

	    if (l_tables->l_words[indx].l_w_type & SPECIAL_COMPARTMENTS_INVERSE
		&& COMPARTMENTS_IN(max_comps_ptr,
				   l_tables->l_words[indx].l_w_cm_mask,
				   l_tables->l_words[indx].l_w_cm_value)) {

		/*
		 * We have determined that turning off this word will raise
		 * the level above the max_comps.  Therefore, first determine
		 * whether all other on words with the same prefix can be
		 * turned off.  To perform this check, first assume that this
		 * word can be turned off, and so mark it in the parse_table.
		 * Then try to turn off all other words by calling
		 * turnoff_word recursively.
		 */

		/* assume we can turn off for now */
		parse_table[indx] ^= TRUE;
		parse_table[indx] |= L_FORCED_BY_TURNOFF_WORD;

		for (i = l_tables->l_first_main_entry;
		     			i < l_tables->l_num_entries; i++)
		    if (i != indx && parse_table[i] & TRUE
			    && l_tables->l_words[indx].l_w_prefix ==
					l_tables->l_words[i].l_w_prefix) {
			if (!turnoff_word(l_tables, parse_table, i, class,
					  min_comps_ptr,
					  words_max_class_ptr, max_class,
					  max_comps_ptr, RECURSING))

			    goto cant_turn_off;
			parse_table[i] |= L_FORCED_BY_TURNOFF_WORD;
		    }
	    }
	    /*
	     * Now, check to see if turning off this word might lower the
	     * level below the min_comps.  If it might, then the word cannot
	     * be turned off if it is the ONLY special inverse word with the
	     * same prefix.  If there are other ones on with the same prefix,
	     * then turning off this word would have no effect on the
	     * min_comps.
	     */

	    if (l_tables->l_words[indx].l_w_type & HAS_COMPARTMENTS
		&& COMPARTMENTS_DOMINATE(min_comps_ptr,
				        l_tables->l_words[indx].l_w_cm_value)) {
		/* if turning off could lower */

		for (i = l_tables->l_first_main_entry;
		     i < l_tables->l_num_entries; i++)
		    if (i != indx && parse_table[i] & TRUE
			&& l_tables->l_words[indx].l_w_prefix ==
						l_tables->l_words[i].l_w_prefix)
			break;	/* out of loop */

		if (i == l_tables->l_num_entries)	/* must be last word */
		    goto cant_turn_off;
	    }
	}

	/*
	 * All is well, so if flag is not RECURSING, meaning that the
	 * L_FORCED_BY_TURNOFF_WORD indicators in the parse_table should not
	 * be left as they are, then scan through the parse table, changing
	 * the state of L_FORCED_BY_TURNOFF_WORD entries if flag is
	 * CHECK_ONLY, and removing the L_FORCED_BY_TURNOFF_WORD flag in any
	 * case.  The value of CHECK_ONLY is 1, causing the ^= statement
	 * below to invert the last bit in the parse_table if the flag has
	 * the value CHECK_ONLY, and to leave it unchanged as
	 * L_FORCED_BY_TURNOFF_WORD is removed if flag has the value TURN_OFF
	 * (0).
	 */

	if (flag != RECURSING)
	    for (i = l_tables->l_first_main_entry;
		 i < l_tables->l_num_entries; i++) {

		if (parse_table[i] & L_FORCED_BY_TURNOFF_WORD)
		    parse_table[i] ^= L_FORCED_BY_TURNOFF_WORD | flag;
	    }

	/*
	 * We now know that its legal to turn off the word, so, unless flag
	 * is CHECK_ONLY, turn it off in the parse_table, and check whether
	 * words_max_class must be recomputed.
	 */

	if (flag != CHECK_ONLY) {
	    parse_table[indx] = FALSE;

	    if (flag == FORCE_OFF_BY_TURNON_WORD)
		parse_table[indx] |= L_FORCED_BY_TURNON_WORD;

	    if (l_tables->l_words[indx].l_w_max_class == *words_max_class_ptr) {
		/* initial value if no words on */
		*words_max_class_ptr = max_class;

		for (i = l_tables->l_first_main_entry;
		     i < l_tables->l_num_entries; i++)
		    if (parse_table[i])
			*words_max_class_ptr = L_MIN(*words_max_class_ptr,
					    l_tables->l_words[i].l_w_max_class);
	    }
	}
    }
    return (TRUE);

cant_turn_off:

    /*
     * The word can't be turned off, so scan through the parse table,
     * changing the state of L_FORCED_BY_TURNOFF_WORD entries and removing
     * the L_FORCED_BY_TURNOFF_WORD flag.  Then restore the original
     * words_max_class and return FALSE.
     */

    for (i = l_tables->l_first_main_entry; i < l_tables->l_num_entries; i++) {
	if (parse_table[i] & L_FORCED_BY_TURNOFF_WORD)
	    parse_table[i] ^= L_FORCED_BY_TURNOFF_WORD | TRUE;
    }

    *words_max_class_ptr = original_words_max_class;

    return (FALSE);
}

/*
 * The subroutine turnon_word, called by l_parse, l_visibility, and
 * l_changeability (below) and itself (recursively), determines whether the
 * indicated word can be turned on in the passed parse table, and if so,
 * turns it on if the flag argument is TURN_ON.  If the indicated word IS
 * turned on, any other words that must be turned on as a result of turning
 * this word on are also turned on, and any words that must be turned off as
 * a result of turning on this word are turned off, and the words_max_class
 * is raised if required by the turned on word.  l_parse calls turnon_word
 * with the flag argument TURN_ON.
 *
 * If turnon_word is called with the flag argument CHECK_ONLY, as it is called
 * by l_visibility and l_changeability, then all checking regarding whether
 * or not the word can be turn on is performed, by the parse_table and the
 * words_max_class are not changed.
 *
 * TRUE is returned if the word could be--or was--turned on. FALSE is returned
 * if the word could not be turned on.
 *
 * If the argument class is passed as NO_LABEL, then no checking involving the
 * classification of the label is done.  l_visibility calls turnon_word in
 * this manner, and it is possible for l_parse to call turnon_word in this
 * manner.
 *
 * When turnon_word calls itself recursively, it uses the flag argument
 * RECURSING.  This value indicates to turnon_word that it should not remove
 * the L_FORCED_BY_TURNON_WORD flags put temporarily in the parse_table. Only
 * the  top-level instantiation of turnon_word removes the
 * L_FORCED_BY_TURNON_WORD flags.
 *
 * Words that must be turned OFF as a result of turning on a word consist of
 * those words that are in hierarchies BELOW the word to be turned on.
 *
 * Words that must be turned ON as a result of turning on a word include:
 *
 *	1)  Those words that are inverse compartments forced on by the
 *	    max_comps, but whose omin_class prevent their presence in the label
 *	    until the word being turned on raises the classification to or
 *	    above their omin_class.
 *
 *	2)  Those words whose presence is specified by the initial compartments
 *	    and initial markings of a classification, if turning on the word
 *	    raises the classification.
 *
 *	3)  Words required by the word being turned on via a
 *	    REQUIRED COMBINATION.
 *
 * In addition to the above words, if turnon_word is called with class NO_LABEL,
 * (as is the case when l_visibility calls) then any inverse compartments
 * forced on by the max_comps, which are currently off, but would be on if
 * the word being turned on (or being checked for visibility) were on (if a
 * class value above the omin_class of the word being checked was also above
 * the omin_class of the inverse compartment; or the omin_class of the word
 * being checked must be >= the omin_class of the inverse compartment), must
 * also be turned on as part of the checking.
 */

static int
turnon_word(l_tables, parse_table, indx, class, min_comps_ptr,
	    words_max_class_ptr, max_class, max_comps_ptr, flag)
    register struct l_tables *l_tables;	/* the encoding tables */
    char           *parse_table;/* the parse table to modify */
    register int    indx;	/* the parse table index to turn on */
    CLASSIFICATION  class;	/* the current classification for this word */
    COMPARTMENTS   *min_comps_ptr;	/* the minimum allowable compartments */
    CLASSIFICATION *words_max_class_ptr;	/* ptr to max class specified
						 * by words on */
    CLASSIFICATION  max_class;	/* the maximum classification whose existence
				 * to acknowledge */
    COMPARTMENTS   *max_comps_ptr;	/* the maximum allowable compartments */
    int             flag;	/* flag sez TURN_ON or CHECK_ONLY or
				 * RECURSING */
{
    register struct l_word_pair *wp;
    register int    i;
    struct l_constraints *cp;
    short          *lp;
    short          *invalid_list;
    short          *end_invalid_list;
    short           dont_turn_on;
    CLASSIFICATION  original_words_max_class;
    int             have_word_to_test;

    /*
     * First make sure that we are not infinite looping because of a loop
     * in the REQUIRED COMBINATIONS section of the encodings.  In no case
     * should turnon_word recurse more times than the number of REAL words
     * (not prefixes or suffixes) in the word table for this l_tables.  The
     * static variable recursion_count is initialized to the number of REAL
     * words in the word table when turnon_word is called with flag other than
     * RECURSING, and is decremented each time it is called with the flag as
     * RECURSING.  When the count gets to zero, the infinite loop is terminated
     * and FALSE is returned.
     */

    static int      recursion_count;

    if (flag != RECURSING)
	recursion_count = l_tables->l_num_entries -
						l_tables->l_first_main_entry;
    else if (--recursion_count == 0)
	return (FALSE);

    /*
     * Check whether the word to turn on is visible at the passed max class
     * and compartments.  If not, return indicating that this word cannot be
     * turned on.
     */

    if (!WORD_VISIBLE(indx, max_class, max_comps_ptr))
	return (FALSE);

    /*
     * Now check whether the word can be turned on given its maximum and
     * minimum classifications.  It cannot be turned on if its max_class is
     * below the current class.  It also cannot be turned on if its min_class
     * is above the words_max_class.
     */

    if (class != NO_LABEL) {
	if (class > l_tables->l_words[indx].l_w_max_class
	    || *words_max_class_ptr < l_tables->l_words[indx].l_w_min_class)
	    return (FALSE);

	/*
	 * Next determine whether this word has an output maximum
	 * classification below the label classification, in which case the
	 * word must be off and cannot be turned on, so return FALSE.
	 */

	if (class > l_tables->l_words[indx].l_w_output_max_class)
	    return (FALSE);

	/*
	 * Next determine whether this word has an output minimum
	 * classification ABOVE the label classification, which is in turn
	 * greater than or equal to the word's minimum classification.  If
	 * so, the word must be off and cannot be turned on, so return FALSE.
	 */

	if (class < l_tables->l_words[indx].l_w_output_min_class
	    && class >= l_tables->l_words[indx].l_w_min_class)
	    return (FALSE);
    }

    /*
     * Next determine whether this word	is an inverse compartment that is
     * forced OFF by the min_comps, and hence cannot be turned on.  If so,
     * return FALSE.
     */

    if (l_tables->l_words[indx].l_w_type & COMPARTMENTS_INVERSE)
	/* if word has inverse compartment bit */
	if (!COMPARTMENTS_IN(min_comps_ptr,
			     l_tables->l_words[indx].l_w_cm_mask,
			     l_tables->l_words[indx].l_w_cm_value))
	    /* and not indicated by the minimum compartments */
	    return (FALSE);

    /*
     * Next save the original value of words_max_class, in case it must be
     * restored later if the word can't be turned on.
     */

    original_words_max_class = *words_max_class_ptr;

    /*
     * This pass through the word table implements the turning on of any
     * inverse compartments forced on by the max_comps or of any words
     * indicated by the initial compartments and initial markings, as
     * described above.
     */

    if (class == NO_LABEL || l_tables->l_words[indx].l_w_min_class > class)
	/* or if word indx will raise the classification */

	for (i = l_tables->l_first_main_entry; i < l_tables->l_num_entries; i++)
	    /* scan word table ... */
	    if (i != indx && !parse_table[i] & TRUE) {

		/* for words other than indx that are not already ON */

		/* haven't found a word to test yet */
		have_word_to_test = FALSE;

		if (l_tables->l_words[i].l_w_type & COMPARTMENTS_INVERSE)
		    /* if word has inverse compartment bit */
		    if (l_tables->l_words[i].l_w_type & HAS_ZERO_MARKINGS)
			/* w/o marking bits present */
			if (COMPARTMENTS_IN(max_comps_ptr,
					    l_tables->l_words[i].l_w_cm_mask,
					    l_tables->l_words[i].l_w_cm_value))
			    /* indicated by max_comps */
			    have_word_to_test = TRUE;

		if (have_word_to_test == FALSE	/* if nothing to test yet */
		    && class != NO_LABEL
		    && COMPARTMENTS_IN(l_in_compartments[
					 l_tables->l_words[indx].l_w_min_class],
				       l_tables->l_words[i].l_w_cm_mask,
				       l_tables->l_words[i].l_w_cm_value))
		    if (MARKINGS_IN(l_in_markings[
					 l_tables->l_words[indx].l_w_min_class],
				    l_tables->l_words[i].l_w_mk_mask,
				    l_tables->l_words[i].l_w_mk_value))
			/* word indicated by initial comps and marks */
			have_word_to_test = TRUE;

		if (have_word_to_test) {
		    /*
		     * if we found in inverse compartment forced on by
		     * max_comps or a word whose presence is is indicated
		     * by the initial compartments and markings, test to
		     * see if would be present in a label (given its
		     * omin_class) if the indx word were turned on
		     */

		    /*
		     * If the word to be checked is an exact alias for
		     * another word that is already on, then continue in the
		     * loop because this alias need not (and indeed cannot)
		     * be forced on.
		     */

		    if (l_tables->l_words[i].l_w_exact_alias != NO_EXACT_ALIAS
			&& parse_table[l_tables->l_words[i].l_w_exact_alias] &
			   TRUE)
			continue;

		    /*
		     * If the word to be checked has an omaxclass below the
		     * current classification below the indx word's
		     * min_class, or below the indx word's omin_class, then
		     * continue in the loop because this word need not be
		     * forced on.
		     */

		    if (l_tables->l_words[i].l_w_output_max_class < class
			|| l_tables->l_words[i].l_w_output_max_class <
					l_tables->l_words[indx].l_w_min_class
			|| l_tables->l_words[i].l_w_output_max_class <
				  l_tables->l_words[indx].l_w_output_min_class)
			continue;

		    /*
		     * If the word to be checked has a maxclass below the
		     * current classification below the indx word's
		     * min_class, or below the indx word's omin_class, then
		     * continue in the loop because this word need not be
		     * forced on.
		     */

		    if (l_tables->l_words[i].l_w_max_class < class
			|| l_tables->l_words[i].l_w_max_class <
					l_tables->l_words[indx].l_w_min_class
			|| l_tables->l_words[i].l_w_max_class <
				  l_tables->l_words[indx].l_w_output_min_class)
			continue;

		    /*
		     * If the indx word's min_class is above the found word's
		     * omin_class, try to turn it on at the indx word's
		     * min_class.
		     */

		    if (l_tables->l_words[indx].l_w_min_class >=
				   l_tables->l_words[i].l_w_output_min_class) {
			if (turnon_word(l_tables, parse_table, i,
					l_tables->l_words[indx].l_w_min_class,
					min_comps_ptr, words_max_class_ptr,
					max_class, max_comps_ptr, RECURSING))
			    /* if word can be turned on mark it as FORCED */
			    parse_table[i] |= L_FORCED_BY_TURNON_WORD;
			else
			    goto cant_turn_on;
		    }
		    /*
		     * If the indx word's omin_class is above the found
		     * word's omin_class, try to turn it on at the indx
		     * word's omin_class.
		     */

		    else if (class == NO_LABEL
			     && l_tables->l_words[indx].l_w_output_min_class >=
			            l_tables->l_words[i].l_w_output_min_class) {
			if (turnon_word(l_tables, parse_table, i,
			       l_tables->l_words[indx].l_w_output_min_class,
			      		min_comps_ptr, words_max_class_ptr,
					max_class, max_comps_ptr, RECURSING))
			    /* if word can be turned on mark it as FORCED */
			    parse_table[i] |= L_FORCED_BY_TURNON_WORD;
			else
			    goto cant_turn_on;
		    }
		}
	    }
    /*
     * If we get here, all inverse compartments and "initial" words that must
     * be turned on have been.
     *
     * Next check the list of constraints to see whether any constraint prevents
     * this word from being turned on.  Scan the list of constraints,
     * processing each entry depending on its type: NOT_WITH or ONLY_WITH.
     */

    for (cp = l_tables->l_constraints; cp != l_tables->l_end_constraints;
	 cp++) {

	/*
	 * Processing for a constraint of type ONLY_WITH.  For this type, any
	 * words in the first list can appear ONLY with words from the second
	 * list.
	 */

	if (cp->l_c_type == ONLY_WITH) {

	    /*
	     * First check whether the word to turn on is in the first list.
	     * If so, the second list contains the ONLY other words that can
	     * be on, so update the parse_table with the only valid words to
	     * be on, then scan the parse_table to see if any other words are
	     * on.  If so, return FALSE.
	     */

	    for (lp = cp->l_c_first_list; lp != cp->l_c_second_list; lp++)
		if (indx == *lp) {
		    for (lp = cp->l_c_second_list;
			 lp < cp->l_c_end_second_list; lp++)
			/* mark word OK to be on */
			parse_table[*lp] |= L_OK;
		    dont_turn_on = FALSE;
		    for (i = l_tables->l_first_main_entry;
			 i < l_tables->l_num_entries; i++) {
			if (parse_table[i] & L_OK)
			    parse_table[i] &= ~L_OK;
			else if (i != indx && parse_table[i] & TRUE)
			    /* on but not L_OK */
			    dont_turn_on = TRUE;
		    }

		    if (dont_turn_on)
			goto cant_turn_on;
		    break;
		}
	    /*
	     * Now check whether any word in the first list is on.  If so,
	     * then the word to turn on must be in the second list or return
	     * FALSE.
	     */

	    for (lp = cp->l_c_first_list; lp != cp->l_c_second_list; lp++)
		if (parse_table[*lp] & TRUE && indx != *lp) {
		    for (lp = cp->l_c_second_list;
			 lp < cp->l_c_end_second_list; lp++)
			if (indx == *lp)
			    break;	/* all is well */
		    if (lp == cp->l_c_end_second_list)
			/* if word to turn on not found */
			goto cant_turn_on;
		    break;
		}
	}
	/*
	 * If the constraint type is NOT_WITH, the two lists specify words
	 * that cannot be combined (none of the first list can be combined
	 * with any of the second). Thus, the word we are trying to turn on
	 * cannot be turned on if it is part of such an invalid combination,
	 * the other half of which:
	 *	1) is already turned on, or
	 *	2) which will be forced on by the max_comps (i.e. an inverse
	 *	   compartment).
	 */

	else {			/* type is NOT_WITH */
	    invalid_list = 0;
	    for (lp = cp->l_c_first_list; lp != cp->l_c_second_list; lp++)
		if (indx == *lp) {
		    invalid_list = cp->l_c_second_list;
		    end_invalid_list = cp->l_c_end_second_list;
		    break;
		}

	    if (!invalid_list)
		for (lp = cp->l_c_second_list; lp != cp->l_c_end_second_list;
		     lp++)
		    if (indx == *lp) {
			invalid_list = cp->l_c_first_list;
			end_invalid_list = cp->l_c_second_list;
			break;
		    }

	    if (invalid_list)
		for (lp = invalid_list; lp != end_invalid_list; lp++) {
		    if (parse_table[*lp] & TRUE)
			goto cant_turn_on;

		    if (l_tables->l_words[*lp].l_w_type & COMPARTMENTS_INVERSE)
			/* if word has inverse compartment bit */
			if (l_tables->l_words[*lp].l_w_type & HAS_ZERO_MARKINGS)
			    /* w/o marking bits present */
			    if (COMPARTMENTS_IN(max_comps_ptr,
					   l_tables->l_words[*lp].l_w_cm_mask,
					   l_tables->l_words[*lp].l_w_cm_value))
				/* indicated by max_comps */
				goto cant_turn_on;
		}
	}
    }

    /*
     * If we get here, all of the COMBINATION CONSTRAINT checking has been
     * passed.
     *
     * Next check each word already on in label to see if its compartments and
     * markings dominate those of the word we have been asked to turn on.  If
     * such a word is found, just return without turning on the word.
     */

    for (i = l_tables->l_first_main_entry; i < l_tables->l_num_entries; i++)
	if (i != indx && parse_table[i] & TRUE) {
	    if (COMPARTMENT_MASK_DOMINATE(l_tables->l_words[i].l_w_cm_mask,
				          l_tables->l_words[indx].l_w_cm_mask))
		if (COMPARTMENTS_DOMINATE(l_tables->l_words[i].l_w_cm_value,
				          l_tables->l_words[indx].l_w_cm_value))
		    if (MARKING_MASK_DOMINATE(l_tables->l_words[i].l_w_mk_mask,
				           l_tables->l_words[indx].l_w_mk_mask))
			if (MARKINGS_DOMINATE(l_tables->l_words[i].l_w_mk_value,
				          l_tables->l_words[indx].l_w_mk_value))
			    goto cant_turn_on;

	    /*
	     * Else if this word we are turning on dominates another already
	     * on, turn off the other word.  If the other word cannot be
	     * turned off, then return FALSE, indicating that this word
	     * cannot be turned on.
	     */

	    if (COMPARTMENT_MASK_DOMINATE(l_tables->l_words[indx].l_w_cm_mask,
					  l_tables->l_words[i].l_w_cm_mask))
		if (COMPARTMENTS_DOMINATE(l_tables->l_words[indx].l_w_cm_value,
					  l_tables->l_words[i].l_w_cm_value))
		    if (MARKING_MASK_DOMINATE(l_tables->l_words[indx].l_w_mk_mask,
					      l_tables->l_words[i].l_w_mk_mask))
			if (MARKINGS_DOMINATE(l_tables->l_words[indx].l_w_mk_value,
					     l_tables->l_words[i].l_w_mk_value))
			    if (parse_table[i] & L_FORCED_BY_TURNON_WORD
				|| !turnoff_word(l_tables, parse_table, i,
						 class, min_comps_ptr,
						 words_max_class_ptr, max_class,
						 max_comps_ptr,
						 FORCE_OFF_BY_TURNON_WORD))
				goto cant_turn_on;
			    else
				continue;	/* in loop for next word */

	    /*
	     * Else if this word is mutually exclusive with the word to be
	     * turned on, just return without turning on this word.  The
	     * above two checks have determined that the word to be turned on
	     * does not dominate this word, and that this word does not
	     * dominate the word to be turned on.  Therefore, if at this
	     * point the mask of either word dominates the other (as opposed
	     * to its value), then the words are mutually exclusive.
	     */

	    if (COMPARTMENT_MASK_DOMINATE(l_tables->l_words[indx].l_w_cm_mask,
					  l_tables->l_words[i].l_w_cm_mask))
		if (MARKING_MASK_DOMINATE(l_tables->l_words[indx].l_w_mk_mask,
					  l_tables->l_words[i].l_w_mk_mask))
		    goto cant_turn_on;

	    if (COMPARTMENT_MASK_DOMINATE(l_tables->l_words[i].l_w_cm_mask,
				          l_tables->l_words[indx].l_w_cm_mask))
		if (MARKING_MASK_DOMINATE(l_tables->l_words[i].l_w_mk_mask,
				          l_tables->l_words[indx].l_w_mk_mask))
		    goto cant_turn_on;
	}
    /*
     * If we get here, all dominance relationships have been passed, and any
     * dominated words that must be turned off have been.
     *
     * Now try to turn on any words required by this word.  If this word
     * requires another and the other word is not visible at the maximum
     * class and comps, then this word cannot be turned on.  If any required
     * word visible at the maximum class and comps cannot be turned on, then
     * return FALSE.  In turning on the required words, remember in the parse
     * table whether they were FORCED on or already on.
     */

    for (wp = l_tables->l_required_combinations;
	 wp != l_tables->l_end_required_combinations; wp++) {
	if (indx == wp->l_word1) {	/* if this word requires another */
	    if (!parse_table[wp->l_word2] & TRUE) {
		/* if needed word not already on */
		if (parse_table[wp->l_word2] & L_FORCED_BY_TURNON_WORD
		    || turnon_word(l_tables, parse_table, wp->l_word2, class,
				   min_comps_ptr, words_max_class_ptr,
				   max_class, max_comps_ptr, RECURSING))
		    /* if word can be turned on mark it as FORCED */
		    parse_table[wp->l_word2] |= L_FORCED_BY_TURNON_WORD;
		else
		    goto cant_turn_on;
	    }
	}
    }

    /*
     * At this point it has been determined that the word can be turned on.
     * However, if this word requires a prefix with compartments or markings
     * specified, then turning it on could both add and remove compartment
     * bits, and could therefore cause the level to rise above the max_comps,
     * or fall below the min_comps.  Both of these cases must be checked for.
     */

    if (l_tables->l_words[indx].l_w_type & SPECIAL_INVERSE) {

	/*
	 * We now know that word requires a prefix with comps or marks.
	 * First determine whether turning on the word would lower the level
	 * below the min_comps.  If so, the word cannot be turned on.
	 */

	if (l_tables->l_words[indx].l_w_type & SPECIAL_COMPARTMENTS_INVERSE) {
	    COMPARTMENTS_COPY(l_t5_compartments,
					   l_tables->l_words[indx].l_w_cm_mask);
	    COMPARTMENTS_XOR(l_t5_compartments,
					  l_tables->l_words[indx].l_w_cm_value);
	    /* l_t5_compartments now has mask of inverse bits */

	    COMPARTMENTS_AND(l_t5_compartments, min_comps_ptr);
	    /*
	     * l_t5_compartments now has inverse bits from this word on in
	     * min_comps
	     */

	    if (!COMPARTMENTS_EQUAL(l_t5_compartments, l_0_compartments))
		goto cant_turn_on;
	}

	/*
	 * If turning on this word would raise the level above the max_comps,
	 * then we must determine whether turning on other words that require
	 * the same prefix would lower the level enough to proceed.  If so,
	 * the other words are turned on.  If not, this word cannot be turned
	 * on.
	 */

	if (l_tables->l_words[indx].l_w_type & HAS_COMPARTMENTS
	    && !COMPARTMENTS_DOMINATE(max_comps_ptr,
          l_tables->l_words[l_tables->l_words[indx].l_w_prefix].l_w_cm_value)) {
	    /* and turning on would raise */

	    parse_table[indx] = TRUE | L_FORCED_BY_TURNON_WORD;

	    for (i = l_tables->l_first_main_entry;
		 i < l_tables->l_num_entries; i++)
		if (i != indx && !parse_table[i] & TRUE
		    && l_tables->l_words[indx].l_w_prefix ==
					      l_tables->l_words[i].l_w_prefix) {
		    /*
		     * determine whether this word, if turned on, would help
		     * solve the problem
		     */

		    if (!COMPARTMENTS_DOMINATE(max_comps_ptr,
					   l_tables->l_words[i].l_w_cm_mask)
		    && !COMPARTMENTS_EQUAL(l_tables->l_words[i].l_w_cm_mask,
				           l_tables->l_words[i].l_w_cm_value)) {

			if (turnon_word(l_tables, parse_table, i,
			           l_tables->l_words[indx].l_w_output_min_class,
			      		min_comps_ptr, words_max_class_ptr,
					max_class, max_comps_ptr, RECURSING))
			    /* if word can be turned on mark it as FORCED */

			    parse_table[i] |= L_FORCED_BY_TURNON_WORD;
			else
			    goto cant_turn_on;
		    }
		}
	}
    }

    /*
     * All is well, so if flag is not RECURSING, meaning that the
     * L_FORCED_BY_TURNON_WORD indicators in the parse_table should not be
     * left as they are, then scan through the parse table, changing the
     * state of L_FORCED_BY_TURNON_WORD entries if flag is CHECK_ONLY, and
     * removing the L_FORCED_BY_TURNON_WORD flag in any case.  The value of
     * CHECK_ONLY is 1, causing the ^= statement below to invert the last bit
     * in the parse_table if the flag has the value CHECK_ONLY, and to leave
     * it unchanged as L_FORCED_BY_TURNON_WORD is removed if flag has the
     * value TURN_ON (0).
     */

    if (flag != RECURSING)
	for (i = l_tables->l_first_main_entry;
	     i < l_tables->l_num_entries; i++) {
	    if (parse_table[i] & L_FORCED_BY_TURNON_WORD)
		parse_table[i] ^= L_FORCED_BY_TURNON_WORD | flag;
	}

    /*
     * We now know that its legal to turn on the word, so, unless flag is
     * CHECK_ONLY, turn it on in the parse_table, and adjust the maximum
     * classification allowed for this word (lower the words_max_class if
     * this word has a lower max class than the other words).
     */

    if (flag != CHECK_ONLY) {
	parse_table[indx] = TRUE;
	*words_max_class_ptr = L_MIN(*words_max_class_ptr,
				     l_tables->l_words[indx].l_w_max_class);
    }
    return (TRUE);

cant_turn_on:

    /*
     * The word can't be turned on, so scan through the parse table, changing
     * the state of L_FORCED_BY_TURNON_WORD entries and removing the
     * L_FORCED_BY_TURNON_WORD flag.  Then restore the original
     * words_max_class and return FALSE.
     */

    for (i = l_tables->l_first_main_entry; i < l_tables->l_num_entries; i++) {
	if (parse_table[i] & L_FORCED_BY_TURNON_WORD)
	    parse_table[i] ^= L_FORCED_BY_TURNON_WORD | TRUE;
    }

    *words_max_class_ptr = original_words_max_class;

    return (FALSE);
}

/*
 * The internal subroutine turnon_forced_words determines whether all words
 * visible at the passed classification that are forced on by the min_comps,
 * max_comps, or by the initial compartments/markings for the classification
 * can be turned on.  If not, FALSE is returned.  If so, they are turned on
 * in the passed parse table if the flag argument is TURN_ON, and TRUE is
 * returned.  They are not turned on if the flag argument is CHECK_ONLY.
 */

static int
turnon_forced_words(parse_table, class, l_tables, min_comps_ptr, max_class,
		    max_comps_ptr, words_max_class_ptr, flag)
    char           *parse_table;/* parse table representing label */
    register CLASSIFICATION class;	/* the label classification */
    register struct l_tables *l_tables;	/* the encodings tables to use */
    COMPARTMENTS   *min_comps_ptr;	/* min compartments possible */
    CLASSIFICATION  max_class;	/* max classification to acknowledge */
    COMPARTMENTS   *max_comps_ptr;	/* max compartments to acknowledge */
    CLASSIFICATION *words_max_class_ptr;	/* max classification of
						 * words in label */
    int             flag;	/* flag sez TURN_ON or CHECK_ONLY */
{
    register int    j;		/* array index */

    for (j = l_tables->l_first_main_entry; j < l_tables->l_num_entries; j++)
        /* for each off word visible at the classification */
	if (class >= l_tables->l_words[j].l_w_output_min_class
	    && class <= l_tables->l_words[j].l_w_output_max_class
	    && !parse_table[j] & TRUE) {

	    /*
	     * If this word is forced ON (as checked by word_forced_on above),
	     * call turnon_word to see if the word can be turned on.  The flag
	     * argument determines whether turnon_word will actually turn on
	     * the word or just check whether it can be turned on.  FALSE is
	     * returned if the word cannot be turned on.  If it can, continue
	     * in the loop checking the remaining words.
	     *
	     * If the word to be checked is an exact alias for another word that
	     * is already on, then continue in the loop because this alias
	     * need not (and indeed cannot) be forced on.
	     */

	    if (word_forced_on(j, l_tables, min_comps_ptr, max_comps_ptr,
			       class)) {
		if (l_tables->l_words[j].l_w_exact_alias != NO_EXACT_ALIAS
		&& parse_table[l_tables->l_words[j].l_w_exact_alias] & TRUE)
		    continue;

		if (!turnon_word(l_tables, parse_table, j, class,
				 min_comps_ptr, words_max_class_ptr,
				 max_class, max_comps_ptr, TURN_ON))
		    return (FALSE);

		if (flag == CHECK_ONLY)
		    /* record that we forced word on */
		    parse_table[j] |= L_FORCED_BY_TURNON_FORCED_WORDS;
	    }
	}

    /*
     * Now that we have determined that all needed words can be turned on, if
     * flag is CHECK_ONLY, we must turn off all words be forced on.  In any
     * case TRUE is returned.
     */

    if (flag == CHECK_ONLY)
	for (j = l_tables->l_first_main_entry;
	     j < l_tables->l_num_entries; j++) {
	    if (parse_table[j] & L_FORCED_BY_TURNON_FORCED_WORDS)
		parse_table[j] ^= L_FORCED_BY_TURNON_FORCED_WORDS | TRUE;
	}

    return (TRUE);		/* all words check out OK */
}

/*
 * The internal subroutine label_valid, which must be called after
 * make_parse_table, and before any other usage of the global variables
 * comps_handled and marks_handled (l_t2_compartments and l_t2_markings),
 * checks the internal format of the label indicated by the passed
 * parse_table (from make_parse_table), classification, compartments, and
 * markings, with respect to the passed l_tables and flags.  The l_tables
 * indicates the type of label to check. The flags argument specifies which
 * l_word entries are to be considered when scanning the l_word table.  If
 * flags is ALL_ENTRIES, all entries are considered.  Otherwise, only those
 * entries whose l_w_flags have all bits in flags on are considered.
 *
 * The passed class argument is assumed by label_valid to be valid, and must be
 * checked before this subroutine is called.
 *
 * The validity check is based on the l_tables passed.  Only l_tables that can
 * be used to parse a human-readable label into internal form should be used
 * (i.e. only l_information_label_tables, l_sensitivity_label_tables, or
 * l_clearance_tables).  When a sensitivity label or clearance is being
 * checked for validity, the markings passed MUST be l_in_markings[class],
 * where class is the classification passed.  If any other markings are
 * passed for a sensitivity label or clearance, label_valid will return
 * FALSE.
 *
 * The following conditions must hold for the label to be considered valid:
 *
 *	1)  The bit patterns in the compartments and markings must be fully
 *	    accounted for by the initial compartments and markings and words
 *	    in the passed l_word table.  In other words, there can be no
 *	    "extra" bits or undefined bit patterns.
 *	2)  All words in the passed l_word table indicated by the compartments
 *	    and markings must have a maximum classification >= the passed
 *	    classification >= minimum classification.
 *	3)  The combination constraints and required combinations must be
 *	    followed for the words represented by the compartments and markings.
 *
 * TRUE is returned if the label is valid, otherwise FALSE is returned.
 */

static int
label_valid(parse_table, class, comps_ptr, marks_ptr, l_tables)
    char           *parse_table;/* parse table for the label */
    CLASSIFICATION  class;	/* the label classification */
    COMPARTMENTS   *comps_ptr;	/* the label compartments */
    MARKINGS       *marks_ptr;	/* the label markings */
    register struct l_tables *l_tables;	/* the l_tables to use */
{
    register int    i;		/* loop index for searching parse table */
    register struct l_word_pair *wp;	/* index for looping required
					 * combinations */
    register struct l_constraints *cp;	/* index for looping through
					 * constraints */
    short          *lp;		/* ptr to each entry in constraint lists */
    int             indx;	/* place to constraint list entry */
    short           ok;		/* flag sez ONLY_WITH constraint is ok */

    /*
     * make_parse_table left the global variables comps_handled and
     * marks_handled (l_t2_compartments and l_t2_markings) with the combined
     * compartment and marking masks of all words in the l_tables that were
     * represented by the label's compartments and markings.  We now check
     * whether there are any "extra" bits in the compartments or markings
     * that cannot be accounted for by the comps_handled and marks_handled,
     * by the initial compartments or markings, or by the compartments or
     * markings specified on the prefixes of all SPECIAL_INVERSE words
     * present in the label.
     *
     * To check for this, we do a boolean logic check.  If X is the compartment
     * or marking bits, H is the compartments or markings handled by the
     * represented words, and I is the initial compartments/markings and
     * SPECIAL_INVERSE compartments/markings, the label is valid if and only
     * if (X|H)=(I|H).  In other words, if all bits NOT handled are equal to
     * the initial bits, the label is valid.
     */

    /*
     * Set l_t5_compartments/markings to be the initial compartments/markings
     * for the classification combined with the compartments/markings if all
     * SPECIAL_INVERSE word prefixes specified in the label.
     */

    COMPARTMENTS_COPY(l_t5_compartments, l_in_compartments[class]);
    MARKINGS_COPY(l_t5_markings, l_in_markings[class]);

    for (i = l_tables->l_first_main_entry; i < l_tables->l_num_entries; i++)
	if (parse_table[i] && l_tables->l_words[i].l_w_type & SPECIAL_INVERSE) {
	    COMPARTMENTS_COMBINE(l_t5_compartments,
	       l_tables->l_words[l_tables->l_words[i].l_w_prefix].l_w_cm_value);
	    MARKINGS_COMBINE(l_t5_markings,
	       l_tables->l_words[l_tables->l_words[i].l_w_prefix].l_w_mk_value);
	}

    /*
     * Perform boolean checks for "extra" compartments and markings.
     */

    COMPARTMENTS_COPY(l_t_compartments, comps_ptr);	/* copy compartments */
    COMPARTMENTS_COMBINE(l_t_compartments, comps_handled); /* now have (X|H) */
    COMPARTMENTS_COMBINE(comps_handled, l_t5_compartments); /* now have (I|H) */
    if (!COMPARTMENTS_EQUAL(comps_handled, l_t_compartments))
	return (FALSE);		/* error if (X|H)!=(I|H) */

#ifdef	TSOL
    if (marks_ptr != NULL) {
    	MARKINGS_COPY(l_t_markings, marks_ptr);	/* copy markings */
    	MARKINGS_COMBINE(l_t_markings, marks_handled);	/* now have (X|H) */
    	MARKINGS_COMBINE(marks_handled, l_t5_markings);	/* now have (I|H) */
    	if (!MARKINGS_EQUAL(marks_handled, l_t_markings))
		return (FALSE);		/* error if (X|H)!=(I|H) */
    }
#else	/* !TSOL */
    MARKINGS_COPY(l_t_markings, marks_ptr);	/* copy markings */
    MARKINGS_COMBINE(l_t_markings, marks_handled);	/* now have (X|H) */
    MARKINGS_COMBINE(marks_handled, l_t5_markings);	/* now have (I|H) */
    if (!MARKINGS_EQUAL(marks_handled, l_t_markings))
	return (FALSE);		/* error if (X|H)!=(I|H) */
#endif	/* TSOL */

    /*
     * If control gets here, the label has no "extra" bits or invalid bit
     * patterns. Loop through the parse table, checking that the
     * classification is not below the minimum classification or above the
     * maximum classification for each word.
     */

    for (i = l_tables->l_first_main_entry; i < l_tables->l_num_entries; i++)
	if (parse_table[i]
	    && (class < l_tables->l_words[i].l_w_min_class
		|| class > l_tables->l_words[i].l_w_max_class))
	    return (FALSE);

    /*
     * Now loop through the required combinations to make sure each of them
     * is satisfied by the words represented in the parse_table.
     */

    for (wp = l_tables->l_required_combinations;
	 wp != l_tables->l_end_required_combinations; wp++)
	if (parse_table[wp->l_word1]	/* if requiring word in label */
	    &&!parse_table[wp->l_word2])	/* and required word isn't */
	    return (FALSE);	/* return an error */

    /*
     * Next check the list of constraints to see whether any constraint is
     * not satisfied by the words represented in the parse_table.
     */

    for (cp = l_tables->l_constraints;
	 cp != l_tables->l_end_constraints; cp++) {

	/*
	 * Check whether any word in the first list is present.  If so, the
	 * check to be made depends on the type of constraint.
	 */

	for (lp = cp->l_c_first_list; lp != cp->l_c_second_list; lp++)
	    if (parse_table[*lp]) {
		/* if any word from first list is present */

		/*
		 * Processing for a constraint of type ONLY_WITH.  For this
		 * type, any words in the first list can appear ONLY with words
		 * from the second list.  To check, scan parse table, making
		 * sure that each word present, other than the one from the
		 * first list, appears in the second list of the constraint.
		 */

		if (cp->l_c_type == ONLY_WITH) {
		    indx = *lp;	/* save word we found */
		    for (i = l_tables->l_first_main_entry;
			 i < l_tables->l_num_entries; i++)
			if (parse_table[i] && i != indx) {
			    ok = FALSE;
			    for (lp = cp->l_c_second_list;
				 lp < cp->l_c_end_second_list; lp++)
				if (i == *lp) {
				    ok = TRUE;
				    break;
				}
			    if (!ok)
				return (FALSE);
			}
		}
		/*
		 * If the constraint type is NOT_WITH, the two lists specify
		 * words that cannot be combined (none of the first list can
		 * be combined with any of the second). To check, make sure
		 * that none of the words on the second list are present in
		 * the label.
		 */

		else {		/* type is NOT_WITH */
		    for (lp = cp->l_c_second_list;
			 lp < cp->l_c_end_second_list; lp++)
			if (parse_table[*lp])
			    return (FALSE);
		}
		break;		/* out of for loop for first list */
	    }
    }
    return (TRUE);		/* all is well */
}

/*
 * The l_parse subroutine parses the passed character representation of a
 * label (either information, sensitivity, or clearance), using the passed
 * l_tables to indicate valid words for the label.  The resultant internal
 * encodings are placed in the passed classification, compartments, and
 * markings.
 *
 * l_parse operates in two modes.  In the normal mode, the syntax of the input
 * is as follows:
 *
 *		[+][CLASSIFICATION] [[+|-][WORD]]...
 *
 * where brackets denote optional entries, ... denotes zero or more of the
 * proceeding bracketed entry can be specified with blanks preceding it.
 * Blanks, tabs,  commas, and slashes are interchangeable, with multiples
 * allowed. CLASSIFICATIONS and WORDS themselves can contain blanks if so
 * encoded in the encoding tables.
 *
 * If the passed string starts with + or - characters, then the string will be
 * interpreted as changes to the existing label. If the passed string starts
 * with a classification followed by a + or -, then the new classification is
 * used, but the rest of the old label is retained and modified as specified
 * in the passed string.  If the passed string is empty or contains, only
 * blanks, then l_parse returns immediately without changing the label.
 *
 * l_parse returns L_BAD_CLASSIFICATION if any of its classification arguments
 * are bad. l_parse performs a great deal of error correction, and returns
 * L_GOOD_LABEL if everything parsed OK; otherwise, the index into the input
 * string where parsing stopped is returned.
 *
 * If the parse fails because the beginning of the input string does not specify
 * a valid classification, a 0 is returned.  If a valid classification is
 * found, but some later portion of the input string cannot be recognized,
 * l_parse returns the index into the input string where the unrecognized
 * word begins.
 *
 * The maximum classification and compartments whose existence should be
 * acknowledged are also passed, to insure that no words in the l_words table
 * above these maxima are revealed.  Any such words are completed IGNORED.
 *
 * The minimum classification and compartments specifiable are also passed.  Any
 * valid label below these minima is automatically converted to these minima.
 *
 * The passed classification, compartments, and markings are assumed to contain
 * a "previous value" of the label in case label modification is specified by
 * the input (with a + or -).  However, if the classification is NO_LABEL,
 * then there is assumed to be no previous label, and any attempt to modify
 * the label will be treated instead as a specification of a new label.
 *
 * The allowed syntax of the input string allows l_parse to conveniently support
 * a "multiple choice" graphical interface in the following manner.  Such an
 * interface would typically display words that can appear in a label, along
 * with an indication of whether the word is present in the current label.
 * l_parse can be called with "+word" to turn on a given word, or "-word" to
 * turn off a given word.
 *
 * The above describes the behavior of l_parse in its normal mode of operation.
 * However, if called with the class_ptr argument pointing to a
 * classification with the value FULL_PARSE, l_parse operates in full_parse
 * mode.  The following describes the behavior of l_parse in full_parse mode.
 * In this mode, the label specified by the passed string must be fully and
 * correctly specified, because l_parse will perform no error correction, and
 * an existing label cannot be changed.  Thus, in full_parse mode, the syntax
 * of the input is as follows:
 *
 *		CLASSIFICATION [WORD]...
 *
 * where brackets denote optional entries, ... denotes zero or more of the
 * proceeding bracketed entry can be specified with blanks preceding it.
 * Blanks, tabs,  commas, and slashes are interchangeable, with multiples
 * allowed. CLASSIFICATIONS and WORDS themselves can contain blanks if so
 * encoded in the encoding tables.
 *
 * In full_parse mode, l_parse returns L_GOOD_LABEL if everything parsed OK, in
 * which case the passed classification, compartments, and markings are
 * returned with the internal representation of the parsed label.  However,
 * if the label specified contains a valid classification and valid words,
 * but the label is illegal because if violates some kind of constraint
 * (e.g., required combinations, combination constraint, minimum or maximum
 * classification, word hierarchy), L_BAD_LABEL is returned.
 *
 * In full_parse mode the maximum classification and compartments whose
 * existence should be acknowledged are also passed, to insure that no words
 * in the l_words table above these maxima are revealed.  Any such words are
 * completed IGNORED. The minimum classification and compartments specifiable
 * are also passed.  Any valid label below these minima is an error, and
 * L_BAD_LABEL is returned.
 *
 * If operating in full_parse mode, and if L_BAD_LABEL is returned, the passed
 * compartments and markings may be changed to meaningless values, and should
 * be ignored in this case.  However, the passed classification is NEVER
 * changed if an error is returned.
 *
 * Summary of l_parse return codes:
 *
 *	L_BAD_CLASSIFICATION	- indicates that the classification represented
 *				  an invalid classification; the passed
 *				  classification, compartments, and markings
 *				  are unchanged
 *
 *	L_GOOD_LABEL		- indicates that the input parsed OK;
 *				  the passed classification, compartments,
 *				  and markings are changed to reflect the
 *				  parsed input
 *
 *	L_BAD_LABEL		- indicates that the input represents a label
 *				  containing a recognized classification and
 *				  recognized words, but violates some kind of
 *				  constraint imposed on the label (possible if
 *				  the classification was passed as FULL_PARSE);
 *				  the passed classification is unchanged, but
 *				  the passed compartments and markings may be
 *				  changed in meaningless ways
 *
 *	anything else		- indicates that some portion of the input was
 *				  not recognizable, and the return code is the
 *				  index in the input of the start of the
 *				  unrecognizable portion; if the encodings
 *				  themselves could not be found, the entire
 *				  input is deemed unrecognizable, and 0 is
 *				  returned
 */

int
l_parse(input, class_ptr, comps_ptr, marks_ptr, l_tables, min_class,
	min_comps_ptr, max_class, max_comps_ptr)
    char           *input;	/* the input string to parse */
    CLASSIFICATION *class_ptr;	/* the classification to use/return */
    COMPARTMENTS   *comps_ptr;	/* the compartments to use/return */
    MARKINGS       *marks_ptr;	/* the markings to use/return */
    register struct l_tables *l_tables;	/* the encodings tables to use */
    CLASSIFICATION  min_class;	/* min classification specifiable */
    COMPARTMENTS   *min_comps_ptr;	/* min compartments specifiable */
    CLASSIFICATION  max_class;	/* max classification to acknowledge */
    COMPARTMENTS   *max_comps_ptr;	/* max compartments to acknowledge */
{
    register int    i;		/* array index */
    register char  *s;		/* ptr into string to be parsed */
    register struct l_word_pair *wp;	/* ptr to word combination pair */
    int             word_length;/* length of a word in table being matched */
    int             len_matched;/* length of input matched each time */
    int             index_matched;	/* index in word table of word
					 * matched against input */
    CLASSIFICATION  class;	/* classification parsed */
    int             prefix = L_NONE;	/* current prefix being parsed */
    int             suffix = L_NONE;	/* current suffix being parsed */
    int             add = TRUE;	/* are we adding or subtracting ? */
    int             prefix_followed = FALSE;	/* have we found something to
						 * go after a prefix found? */
    char           *saved_s;	/* ptr to last prefix or word that needs
				 * suffix matched */
    char           *parse_table;/* ptr to parse table allocated by make parse
				 * table */
    char           *fparse_table;	/* ptr to parse table allocated for
					 * full parse */
    char           *norm_input;	/* ptr to normalized input string */
    short          *norm_map;	/* ptr to normalizes input string mapping */
    int             have_blank;	/* flag for normalization routine */
    int             changing_label = FALSE;	/* flag sez label is being
						 * changed */
    CLASSIFICATION  words_max_class;	/* max classification allowed by
					 * words on */
    int             full_parse;	/* flag sez that input must be fully
				 * specified */
    int             return_value;	/* return value for function */
    struct l_input_name *input_name;	/* for scanning input names */


    if (!l_encodings_initialized())
	return (0);		/* error return if encodings not in place */

    if (*class_ptr == FULL_PARSE) {
	/* if full parse, w/o error correction requested */
	full_parse = TRUE;	/* record l_parse operating mode */
	class = NO_LABEL;	/* proceed with no label to change */
    } else {
	full_parse = FALSE;
	class = *class_ptr;	/* start with passed classification */
    }

    /*
     * First make sure that the passed class_ptr, min_class, and max_class
     * arguments represent valid classifications.  Return L_BAD_CLASSIFICATION
     * if not.  The class_ptr argument will be checked for validity only if it
     * has a value other than NO_LABEL.
     */

    if (class != NO_LABEL
	&& (class > l_hi_sensitivity_label->l_classification
	    || class < *l_min_classification
	    || !l_long_classification[class]))
	return (L_BAD_CLASSIFICATION);

    if (min_class > l_hi_sensitivity_label->l_classification
	|| min_class < *l_min_classification
	|| !l_long_classification[min_class])
	return (L_BAD_CLASSIFICATION);

    if (max_class > l_hi_sensitivity_label->l_classification
	|| max_class < *l_min_classification
	|| !l_long_classification[max_class])
	return (L_BAD_CLASSIFICATION);

    /*
     * Copy the input string to norm_input, normalizing it by:
     *
     *	1) Forcing all alphabetic characters to upper case,
     *	2) Changing all slashes (/) and tabs to blanks, and
     *	3) Replacing multiple blanks with one blank.
     *
     * This normalization allows for uniform comparison of input against the
     * stored tables.
     *
     * Also, since the normalized string could be shorter than the original
     * string, maintain a mapping in norm_map between the strings.  For
     * example, if the ith character in norm_input corresponds to the i+3rd
     * character in the input string, then norm_map[i] = i+3.  The norm_map
     * is used for returning the index of erroneous input.
     */

#ifdef	TSOL
#ifdef	notdef
    if (MB_CUR_MAX > sizeof (char)) {
#else	/* !notdef */
    /* LINTED: constant conditional */
    if (TRUE) {
#endif	/* notdef */
	size_t len;
	wchar_t *wc_input;
	wchar_t *wc_start;
	short	*wc_map;
	short	*wc_map_start;
	char	*string;
	short	*nm;

	len = strlen(input);

	wc_start = (wchar_t *) malloc((len+1) * sizeof (wchar_t));
	wc_map_start = (short *) malloc((len+1) * sizeof (short));
	wc_input = wc_start;
	wc_map = wc_map_start;

	if (wc_input == NULL || wc_map == NULL)
	    return (0);

	string = input;

	/*
	 * Convert from multibyte to wide character, normalize, and
	 * initialize offset map.
	 * This also has to take into account that it is possible for a
	 * normalized multibyte character to be longer than the original.
	 */
	have_blank = TRUE;	/* causes leading blanks to be ignored */

	while (*string) {
	    len = mbtowc(wc_input, string, MB_LEN_MAX);

	    if (!have_blank &&
		(iswspace(*wc_input) ||
		 *wc_input == L'/' ||
		 *wc_input == L',')) {

		*wc_input++ = L' ';

		*wc_map++ = (short) (string - input);
		have_blank = TRUE;
	    } else if (!iswspace(*wc_input) &&
			*wc_input != L'/' &&
			*wc_input != L',') {

		if (iswlower(*wc_input))	/* force upper case */
		    *wc_input = towupper(*wc_input);

		wc_input++;
		*wc_map++ = (short) (string - input);
		have_blank = FALSE;
	    }

	    /* skip following <space> or '/', or ',' */
	    string += len;
	}  /* while (*string) */

	*wc_input = L'\0';

	/*
	 * Convert wide characters to normalized multibyte string and
	 * update the offset map.
	 */
	if ((len = wcstombs(NULL, wc_start, len * MB_LEN_MAX)) == (size_t)-1) {
	    /* multibyte translation failure */
	    free(wc_start);
	    free(wc_map_start);
	    return (0);
	}

	norm_input = (char *) calloc(len + 1, sizeof (char));
	norm_map = (short *) calloc(len + 1, sizeof (short));

	if (norm_input == NULL || norm_map == NULL)
	    return (0);

	s = norm_input;
	nm = norm_map;

	for (wc_input = wc_start, wc_map = wc_map_start;
	     *wc_input;
	     wc_input++, wc_map++) {

	    len = wctomb(s, *wc_input);
	    *nm = *wc_map;
	    s += len;
	    nm += len;
	}

	free(wc_start);
	free(wc_map_start);

	*s = '\0';
	s = norm_input;
    } else
#endif	/* TSOL */
    {
	register char  *string;
	register short *nm;

	norm_input = (char *) calloc((unsigned) strlen(input) + 1, 1);
	norm_map = (short *) calloc((unsigned) strlen(input) + 1, sizeof(short));

	if (norm_input == NULL || norm_map == NULL)
	    return (0);

	s = norm_input;
	nm = norm_map;

	have_blank = TRUE;	/* causes leading blanks to be ignored */

	for (string = input; *string; string++) {
	    /* for each character in input string */
	    if (!have_blank
		&& (isspace(*string) || *string == '/' || *string == ',')) {
		have_blank = TRUE;
		*s++ = ' ';
		*nm++ = (short) (string - input);
		continue;
	    } else if (!isspace(*string) && *string != '/' && *string != ',') {
		have_blank = FALSE;
		if (islower(*string))	/* force upper case */
		    *s++ = toupper(*string);
		else
		    *s++ = *string;
		*nm++ = (short) (string - input);
	    }
	}
	*s = '\0';
	s = norm_input;
    }

    /*
     * If there are no non-blank characters left in the input string, just
     * return without doing anything.
     */

    if (*s == '\0') {		/* if no input */
	(void) free((char *) norm_map);
	(void) free(norm_input);
	/* if not full parse, make no changes, report no errors */
	return (class == NO_LABEL ? 0 : L_GOOD_LABEL);
    }
    /*
     * Initialize words_max_class (the maximum classification allowed by
     * words that are on) to be the max_class passed, on the assumption (for
     * now) that the user is NOT changing an old label.
     */

    words_max_class = max_class;

    /*
     * If NOT in full_parse mode, then the input string can contain + and -
     * characters to indicate changes to an existing label.  Thus, if we are
     * no in full_parse mode, and if the first character of the string is a +,
     * then this indicates that the current label, as represented by the
     * passed class, comps, and marks, is to be modified rather than being
     * replaced. Therefore, if the first character is a +, just skip it for
     * now and proceed to look for a classification.
     */

    if (!full_parse)
	while (*s == '+')
	    s++;

    /*
     * Once a - is entered, a classification is invalid.  Therefore, try to
     * parse a classification only if there is no -, or if we are in
     * full_parse mode.
     */

    if (*s != '-' || full_parse) {

	/*
	 * Now, try to match the next part of the string against each long,
	 * short, and alternate classification name.  The longest match found
	 * will be used. Therefore, if both "S E C R E T" and "S" appear as
	 * classification long, short, or alternate names, the input string
	 * "S E C R E T" will be matched against "S E C R E T", not "S".
	 */

	len_matched = 0;	/* nothing matched yet */

	for (i = 0; i <= max_class; i++) {  /* for each classification name */
	    if (l_long_classification[i]) {
		word_length = strlen(l_short_classification[i]);
		if (word_length > len_matched
		  && 0 == strncmp(s, l_short_classification[i], word_length)
		    && (s[word_length] == ' ' || s[word_length] == '\0')) {
		    len_matched = word_length;
		    index_matched = i;
		}

		word_length = strlen(l_long_classification[i]);
		if (word_length > len_matched
		    && 0 == strncmp(s, l_long_classification[i], word_length)
		    && (s[word_length] == ' ' || s[word_length] == '\0')) {
		    len_matched = word_length;
		    index_matched = i;
		}

		if (l_alternate_classification[i]) {
		    word_length = strlen(l_alternate_classification[i]);
		    if (word_length > len_matched
			&& 0 == strncmp(s, l_alternate_classification[i],
					word_length)
		     && (s[word_length] == ' ' || s[word_length] == '\0')) {
			len_matched = word_length;
			index_matched = i;
		    }
		}
	    }
	}

	if (len_matched > 0) {	/* if class found */
	    /* move scan pointer below classification matched */
	    s += len_matched;
	    class = index_matched;	/* save classification */
	}
    }

    /*
     * If the string was not matched against a classification, this is not an
     * error unless we are in full_parse mode.
     *
     * If we are not in full_parse mode, and the classification cannot be
     * parsed, we assume no classification was entered, retain the original
     * classification (if any), and continue parsing as a word.  This is OK
     * even if there was no original classification, because the classification
     * will be forced upward to the passed minimum classification, and the later
     * to the minimum required by any words specified.
     *
     * If we are in full_parse mode, then it is an error if the classification
     * was not parsed, or was parsed but is below the passed minimum.  If so,
     * produce an error return.
     */

    if (!full_parse)
	class = L_MAX(class, min_class);

    else {	/* if FULL_PARSE */
	if (class < min_class) {

	    /* if not set or set too low */
	    (void) free((char *) norm_map);
	    (void) free(norm_input);
	    		/* return 0 if no classification found,
	     		 * return incorrect label code if class above min */
	    return (class == NO_LABEL ? 0 : L_BAD_LABEL);
	}

	/*
	 * If performing a full parse, then a "full" parse_table
	 * (fparse_table) must be allocated.  This parse table will be used
	 * to indicate words specified EXPLICITLY by the input string.
	 */

	fparse_table = (char *) calloc(l_tables->l_num_entries, 1);
	if (fparse_table == NULL)
	    return (0);
    }

    /*
     * Allocate a parse_table of all FALSE entries.
     */

    parse_table = (char *) calloc(l_tables->l_num_entries, 1);

    if (parse_table == NULL)
	return (0);

    /*
     * At this point, if a classification was found, s points after it.
     * Otherwise, s points to first word or a - before the first word (if not
     * in full_parse mode).
     *
     * If there was an original label specified, and if s now points to a
     * + or -, or if the first character entered (norm_input[0]) was a +, then
     * set the parse table based on the passed classification, compartments,
     * and markings, because the caller has requested modifying the current
     * label with the + or -.  After the parse_table is filled in, the
     * words_max_class must be recomputed to be the lowest l_w_max_class
     * specified by the words on in the parse table.
     */

    if (*s == ' ')
	s++;			/* skip any blank */

    if (*class_ptr != NO_LABEL	/* if label already had a value */
	&& *class_ptr != FULL_PARSE
	&& (*s == '+' || *s == '-' || norm_input[0] == '+')) {
	changing_label = TRUE;

	make_parse_table(parse_table, *class_ptr, comps_ptr, marks_ptr,
			 l_tables, ALL_ENTRIES);

	for (i = l_tables->l_first_main_entry; i < l_tables->l_num_entries; i++)
	    if (parse_table[i])
		words_max_class = L_MIN(words_max_class,
					l_tables->l_words[i].l_w_max_class);
    }
    /*
     * If we are not changing an existing label, or if we are but have changed
     * the classification (above), then we must check whether any words must be
     * turned on because they 1) represent an inverse compartment forced on by
     * the max_comps, 2) a normal compartment forced on by the min_comps, or
     * 3) are forced on by the initial compartments and/or markings.
     *
     * If so, try to turn on the inverse compartment or initial word in the
     * parse table, because it must be on before any other words are added
     * through parsing the string.  If turning on a word fails (e.g., because
     * some other other word with which it cannot be combined is present in
     * the label), then if not in full_parse mode, put the classification
     * back to its original value, because it cannot be changed in the manner
     * indicated; if in full_parse mode, return an error because an illegal
     * classification must have been entered.
     */

    if (!changing_label || class != *class_ptr)
	if (!turnon_forced_words(parse_table, class, l_tables, min_comps_ptr,
				 max_class, max_comps_ptr, &words_max_class,
				 TURN_ON)) {
	    if (full_parse) {
		/* error an error if can't turn on in full parse mode */
		(void) free(parse_table);
		(void) free(fparse_table);
		(void) free((char *) norm_map);
		(void) free(norm_input);
		return (L_BAD_LABEL);
	    } else		/* not full parse, reset class because user
				 * tried to raise it too high */

		class = *class_ptr;
	}

    /*
     * This is the start of the main loop to check each remaining part of the
     * string against the word table.
     */

    while (*s != '\0') {	/* while there is more left to parse... */

	if (*s == ' ')
	    s++;		/* skip any blank */
	if (*s == '\0')
	    break;

	/*
	 * If not in full_parse mode, check for + or -.  If found, set boolean
	 * add appropriately and skip over + or -.
	 */

	if (!full_parse && (*s == '+' || *s == '-')) {
	    add = (*s == '+');	/* set add appropriately */
	    s++;		/* skip over + or - */
	    continue;		/* back to start of main loop */
	}
	/*
	 * Now, try to match the next part of the string against each word in
	 * the word table that is visible given the maximum classification and
	 * compartments.  The longest match found in the table will be used.
	 * Therefore, if both "EYES" and "EYES ONLY" appear in the word table
	 * in any order, the input string "EYES ONLY" will be matched against
	 * "EYES ONLY", not "EYES".
	 */

	len_matched = 0;	/* nothing matched yet */
	for (i = 0; i < l_tables->l_num_entries; i++)
	    /* for each word in table */
	    if (l_tables->l_words[i].l_w_prefix == L_IS_PREFIX
		|| WORD_VISIBLE(i, max_class, max_comps_ptr)) {
		/*
		 * If this word appears to be visible, but requires another
		 * word that is not visible, then treat this word as invisible
		 * also (and therefore ignore it by continuing in the for loop).
		 */

		for (wp = l_tables->l_required_combinations;
		     wp != l_tables->l_end_required_combinations; wp++)
		    if (i == wp->l_word1
		        && !WORD_VISIBLE(wp->l_word2, max_class, max_comps_ptr))
		        /* if this word requires another... */
			break;
		if (wp != l_tables->l_end_required_combinations)
		    continue;

		/*
		 * Continue in loop (ignoring this word in table) if we are
		 * not parsing after a prefix and this word requires a prefix.
		 */

		if (prefix == L_NONE && l_tables->l_words[i].l_w_prefix >= 0)
		    continue;

		/*
		 * Continue in loop (ignoring this word in table) if we ARE
		 * parsing after a prefix and this word does not require this
		 * prefix.
		 */

		if (prefix >= 0 && prefix != l_tables->l_words[i].l_w_prefix)
		    continue;

		/*
		 * Continue in loop (ignoring this word in table) if we are
		 * not parsing after a word that requires a suffix and this
		 * word IS a suffix.
		 */

		if (suffix == L_NONE
		    && l_tables->l_words[i].l_w_suffix == L_IS_SUFFIX)
		    continue;

		/*
		 * Continue in loop (ignoring this word in table) if we ARE
		 * parsing after a word that requires a suffix and this word is
		 * not a suffix or another word that requires the same suffix.
		 */

		if (suffix >= 0 && (suffix != l_tables->l_words[i].l_w_suffix
				    && suffix != i))
		    continue;

		/*
		 * If this word is not to be ignored, and if the length of this
		 * word is greater than that of any word matched above this one
		 * in the table, then compare the string being parsed to this
		 * word.  If it matches, save the new (greater) length matched
		 * and the index of the word matched, and try for a longer
		 * match against the short name.
		 */

		word_length = strlen(l_tables->l_words[i].l_w_long_name);
		if (word_length > len_matched
		    && 0 == strncmp(s, l_tables->l_words[i].l_w_long_name,
		    		    word_length)
		    && (s[word_length] == ' ' || s[word_length] == '\0')) {
		    len_matched = word_length;
		    index_matched = i;
		}

		if (l_tables->l_words[i].l_w_short_name) {
		    word_length = strlen(l_tables->l_words[i].l_w_short_name);
		    if (word_length > len_matched
			&& 0 == strncmp(s, l_tables->l_words[i].l_w_short_name,
					word_length)
		     	&& (s[word_length] == ' ' || s[word_length] == '\0')) {
			len_matched = word_length;
			index_matched = i;
		    }
		}
		/*
		 * Now continue searching all input names (if any) looking
		 * for the longest match.
		 */

		for (input_name = l_tables->l_words[i].l_w_input_name;
		     input_name != NO_MORE_INPUT_NAMES;
		     input_name = input_name->next_input_name) {
		    word_length = strlen(input_name->name_string);
		    if (word_length > len_matched
		        && 0 == strncmp(s, input_name->name_string, word_length)
		        && (s[word_length] == ' ' || s[word_length] == '\0')) {
			len_matched = word_length;
			index_matched = i;
		    }
		}
	    }
	/*
	 * Find out if string matches word in table.
	 */

	if (len_matched > 0) {	/* if found */

	    /*
	     * If we are parsing after having found a prefix, then this entry
	     * REQUIRES this prefix, so set prefix_followed to indicate we
	     * have "satisfied" the prefix and do normal processing.
	     */

	    if (prefix >= 0)
		prefix_followed = TRUE;

	    /*
	     * If the word matched IS a prefix, we must record that we have
	     * encountered a prefix and continue in the main loop.
	     */

	    else if (l_tables->l_words[index_matched].l_w_prefix ==
								L_IS_PREFIX) {
		prefix = index_matched;	/* save prefix we found */
		prefix_followed = FALSE;	/* record that nothing
						 * follows it yet */
		saved_s = s;	/* save ptr to prefix word matched */
		s += len_matched;	/* s ptr to rest to parse */
		if (full_parse &&
		    (l_tables->l_words[index_matched].l_w_type &
		    HAS_COMPARTMENTS)) {
			/*
			 * this prefix has compartments, therefore defines
			 * special inverse compartments that are used
			 * by the words that are defined as SPECIAL_INVERSE,
			 * mark it seen.
			 */
			fparse_table[index_matched] = TRUE;
		}
		continue;	/* back to main loop; nothing more to do for
				 * this word */
	    }
	    /*
	     * Now, if the word REQUIRES a prefix, and we didn't have one, we
	     * must process the error.
	     */

	    else if (l_tables->l_words[index_matched].l_w_prefix >= 0)
		 /* if proper prefix didn't precede */

		break;		/* out of while parse loop */

	    /*
	     * If we have previously encountered a word that requires a suffix,
	     * see if we have found it now.  If so, record that we no longer
	     * need a suffix and continue at top of main loop.  If this is not
	     * the suffix, break out of the loop to process an error.
	     */

	    if (suffix >= 0) {	/* if we need a suffix */
		if (index_matched == suffix) {
		    /* if we found the suffix we await */
		    suffix = L_NONE;
		    s += len_matched;	/* s ptr to rest to parse */
		    continue;		/* we've handled this word */
		}
	    }
	    /*
	     * If the word matched IS a suffix, then produce an error message
	     * because we are not expecting a suffix at this point.
	     */

	    else if (l_tables->l_words[index_matched].l_w_suffix == L_IS_SUFFIX)
		break;		/* out of while parse loop */

	    /*
	     * If this word REQUIRES a suffix, we should record that fact and
	     * do normal processing (below).
	     */

	    else if (l_tables->l_words[index_matched].l_w_suffix >= 0) {
		/* if suffix required */
		saved_s = s;	/* save ptr to word that needs suffix */
		/* save suffix we need */
		suffix = l_tables->l_words[index_matched].l_w_suffix;
	    }

	    s += len_matched;	/* s ptr to rest to parse */

	    /*
	     * Now that we have handled the special cases for prefixes and
	     * suffixes, we can handle the word as an add or a subtract.  If an
	     * add (which will be the case for every word in full_parse mode),
	     * call turnon_word for the appropriate entry in the parse table
	     * (index i).  If a subtract, call turnoff_word for the appropriate
	     * entry in the parse table.
	     *
	     * If in full_parse mode, also set the appropriate fparse_table
	     * entry to TRUE.  If this manner, the parse_table represents the
	     * words both explicitly turned on and off, as well as those
	     * implicitly turned on and off by other words, whereas the
	     * fparse_table represents only those words explicitly turned on.
	     * In full_parse mode, these two parse tables must match for the
	     * label to have been fully specified.
	     */

	    if (add)
		(void) turnon_word(l_tables, parse_table, index_matched, class,
				   min_comps_ptr, &words_max_class, max_class,
				   max_comps_ptr, TURN_ON);
	    else
		(void) turnoff_word(l_tables, parse_table, index_matched, class,
				    min_comps_ptr, &words_max_class, max_class,
				    max_comps_ptr, TURN_OFF);

	    if (full_parse)
		fparse_table[index_matched] = TRUE;
	}
	/*
	 * If control falls after the else below, then the string could not be
	 * matched against entry in the word table that was not ignored.  This
	 * either means that the string does not match any legitimate entry in
	 * the table, or that we were looking for a word after a prefix, but
	 * the string does not match any entry that can go after the same
	 * prefix.  Therefore, if the prefix we had parsed has already been
	 * followed by some word (prefix_followed), then indicate that we are
	 * no longer processing after a prefix and try to look up the word
	 * again.  If the prefix was not followed, then the string cannot be
	 * matched, so break out of the parse loop to process the error.
	 */

	else {			/* word not found in table */
	    if (prefix_followed) {
		prefix_followed = FALSE;
		prefix = L_NONE;
	    } else
		break;		/* out of while parse loop */
	}
    }

    /*
     * Now, if not all input was parsed, or a suffix is missing, or a prefix
     * entered was not followed, process a syntax error.
     */

    if (suffix >= 0)		/* if a needed suffix was missing */
	s = saved_s;		/* backup s to input that needs the suffix */
    else if (prefix >= 0 && !prefix_followed)	/* if prefix not followed */
	s = saved_s;		/* backup s to prefix that was not followed */

    if (*s) {			/* if not all input parsed */
	i = norm_map[(int) (s - norm_input)];	/* compute index into original
						 * input of syntax error */
	(void) free(parse_table);
	if (full_parse)
	    free(fparse_table);
	(void) free((char *) norm_map);
	(void) free(norm_input);
	return (i);
    }
    /*
     * If not in full_parse mode, 1) make sure the classification is high enough
     * for all of the words on in parse_table, 2) make sure the classification
     * is not higher than words_max_class, and 3) that the classification does
     * not remain as NO_LABEL.
     */

    if (!full_parse) {
	for (i = l_tables->l_first_main_entry; i < l_tables->l_num_entries; i++)
	    if (parse_table[i])
		class = L_MAX(class, l_tables->l_words[i].l_w_min_class);

	/*
	 * At this point, if the class is higher than words_max_class, this
	 * is because the user raised it too high above (no word could have
	 * raised it because words are not turned on if they are above it).
	 * Therefore, class can be lowered to the words_max_class.
	 */

	class = L_MIN(class, words_max_class);

	/*
	 * If we get here with class = NO_LABEL, then the classification
	 * specified could not be used because its forces some default word
	 * or inverse word on that cannot be turned on for some reason.  When
	 * this occurs, we just return OK, leaving the class, compartments,
	 * and markings arguments unchanged.
	 */

	if (class == NO_LABEL) {
	    (void) free(parse_table);
	    (void) free((char *) norm_map);
	    (void) free(norm_input);
	    return (L_GOOD_LABEL);
	}
    }
    /*
     * Now that all words have been turned on in the parse_table, scan the
     * parse_table and turn on the entries for any prefixes referenced by
     * SPECIAL_INVERSE words.
     */

    for (i = l_tables->l_first_main_entry; i < l_tables->l_num_entries; i++) {
	if (parse_table[i] && l_tables->l_words[i].l_w_type & SPECIAL_INVERSE) {
	    parse_table[l_tables->l_words[i].l_w_prefix] = TRUE;
	}
    }

    /*
     * Now the parse_table is correctly set, so set the passed
     * classification, compartments, and markings properly.
     *
     * First initialize the compartments and markings with their proper values
     * if no entries are indicated in the parse table.  There there can be
     * several such initial compartments and markings, depending the
     * classification of the data. The initial compartments and markings
     * tables contains this information.
     */

    COMPARTMENTS_COPY(comps_ptr, l_in_compartments[class]);
#ifdef	TSOL
    if (marks_ptr != NULL ) {
    	MARKINGS_COPY(marks_ptr, l_in_markings[class]);
    }
#else	/* !TSOL */
    MARKINGS_COPY(marks_ptr, l_in_markings[class]);
#endif	/* TSOL */

    /*
     * Now that there is no more input to parse, and both parse tables are set,
     * they must be compared.  If they do not equal, then the string did not
     * fully specify a label, and hence an error is returned.
     *
     * If they do equal, then if a word is on, make sure that the class
     * specified was high enough for the word if in full_parse mode.  If not,
     * return an error.  Set the appropriate bits in the compartments and
     * markings for each word on if no error is returned.
     */

    return_value = L_GOOD_LABEL;	/* assume good return for now */

    for (i = 0; i < l_tables->l_num_entries; i++) {
	 /* start at 0 in case any SPECIAL_INVERSE prefixes are on */
	if (full_parse && parse_table[i] != fparse_table[i]) {
	    /* label not fully specified */
	    return_value = L_BAD_LABEL;	/* specify incorrect label code */
	    break;		/* out of loop */
	} else if (parse_table[i]) {
	    if (full_parse && class < l_tables->l_words[i].l_w_min_class) {
		return_value = L_BAD_LABEL;   /* specify incorrect label code */
		break;		/* out of loop */
	    }

	    COMPARTMENTS_SET(comps_ptr, l_tables->l_words[i].l_w_cm_mask,
			     l_tables->l_words[i].l_w_cm_value);
#ifdef	TSOL
	    if (marks_ptr != NULL) {
	    	MARKINGS_SET(marks_ptr, l_tables->l_words[i].l_w_mk_mask,
		    l_tables->l_words[i].l_w_mk_value);
	    }
#else	/* !TSOL */
	    MARKINGS_SET(marks_ptr, l_tables->l_words[i].l_w_mk_mask,
			 l_tables->l_words[i].l_w_mk_value);
#endif	/* TSOL */
	}
    }

    /*
     * Now return the classification.
     */

    if (return_value == L_GOOD_LABEL)
	*class_ptr = class;

    /*
     * Free allocated areas and return with return_value as set above.
     */

    (void) free(parse_table);
    if (full_parse)
	(void) free(fparse_table);
    (void) free((char *) norm_map);
    (void) free(norm_input);
    return (return_value);
}

/*
 * The subroutine float_il floats the IL passed as the first argument with
 * the IL passed as the second argument.
 */

#ifndef	TSOL
static void
#else	/* TSOL */
void
#endif	/* TSOL */
float_il(target_il, source_il)
    register struct l_information_label *target_il;
    register struct l_information_label *source_il;
{
    target_il->l_classification = L_MAX(target_il->l_classification,
					source_il->l_classification);
    COMPARTMENTS_COMBINE(target_il->l_compartments, source_il->l_compartments);
    MARKINGS_COMBINE(target_il->l_markings, source_il->l_markings);
}

/*
 * The l_convert subroutine converts the passed classification, compartments,
 * and markings into a character string representation thereof, which it puts
 * in the passed string.  The passed string is assumed to at least as long as
 * specified by the l_max_length in the passed l_tables, which specifies the
 * maximum amount of space needed to hold a label converted from the l_tables
 * encodings.
 *
 * If the parse_table argument is passed as NO_PARSE_TABLE, l_convert will
 * allocate, use, and free its own parse table.  However, if the caller wants
 * access to the parse table after l_convert returns, the caller can pass a
 * ptr to a parse table that l_convert will use instead.
 *
 * The parse table is a character array the same size as the passed l_words
 * table, containing TRUE in each character whose corresponding word is
 * represented in the label, and FALSE otherwise.  The parse table would be
 * useful to a caller implementing a "multiple choice" user interface for
 * label specification, by specifying which words in the multiple choice are
 * present in a given label.
 *
 * The l_classification argument is a ptr to a classification name table
 * (short, long or alternate names), or NO_CLASSIFICATION if no classification
 * is to be output.
 *
 * If the use_short_names arguments is passed as TRUE, then the short names for
 * words are output, rather than the output name.
 *
 * The flags argument specifies which l_word entries are to be considered when
 * scanning the l_word table.  If flags is ALL_ENTRIES, all entries are
 * considered. Otherwise, only those entries whose l_w_flags have all the
 * bits in flags on are considered.
 *
 * If the argument check_validity is TRUE, l_convert performs a complete
 * validity check on the classification, compartments, and markings, based on
 * the l_tables passed.  Only l_tables that can be used to parse a
 * human-readable label into internal form should be used (i.e. only
 * l_information_label_tables, l_sensitivity_label_tables, or
 * l_clearance_tables) if check_validity is TRUE. When a sensitivity label or
 * clearance is being checked for validity, the markings passed MUST be
 * l_in_markings[class], where class is the classification passed.  If any
 * other markings are passed for a sensitivity label or clearance, l_convert
 * will return FALSE and not perform the conversion.
 *
 * If check_validity is FALSE, l_convert checks only that the class argument
 * represents a proper classification.
 *
 * If the argument information_label is passed as NO_INFORMATION_LABEL, then
 * l_convert does not compute or return the information label of the human-
 * readable string returned.  If information_label is not NULL, then it is
 * assumed to point to a valid l_information_label structure which will be
 * FLOATED with the information labels of all names returned in string.
 *
 * l_convert returns FALSE and performs no conversion if the class argument is
 * bad or if the requested validity check fails.  Otherwise l_convert returns
 * TRUE and performs a conversion.
 *
 * This subroutine can be used for information, sensitivity, and clearance
 * labels, using different l_tables, and passing dummy markings for a
 * sensitivity label or clearance.  It can also be used for producing various
 * labels for printer banners.  Using the l_tables l_channel_tables produces
 * output that represents the appropriate "HANDLE VIA" caveat for printer
 * banner pages, if passed a sensitivity label.  Using the l_tables
 * l_printer_banner produces a string that represents "other" special printer
 * banner output specific to the sensitivity label passed.  Using the
 * l_tables l_information_label_tables with flags of ACCESS_RELATED produces
 * those access related markings needed for part of the printer banner page.
 */

int
l_convert(string, class, l_classification, comps_ptr, marks_ptr, l_tables,
	  caller_parse_table, use_short_names, flags, check_validity,
	  information_label)
    register char  *string;	/* place to put the output */
    CLASSIFICATION  class;	/* the label classification */
    char           *l_classification[];	/* the human-readable
					 * classification table */
    COMPARTMENTS   *comps_ptr;	/* the label compartments */
    MARKINGS       *marks_ptr;	/* the label markings */
    register struct l_tables *l_tables;	/* the l_tables to use */
    char           *caller_parse_table;	/* place to return the parse table */
    int             use_short_names;   /* flag sez use short names for output */
    int             flags;	/* l_w_flags of l_word entries to use */
    int             check_validity;	/* flag sez check validity of label */
				       /* information label of label returned */
    struct l_information_label *information_label;
{
    char           *parse_table;/* ptr to parse table for this label */
    int             valid;	/* flag sez label being converted is valid */
    register int    i;		/* loop index for searching parse table */
    int             prefix = L_NONE;	/* indicates output of words after a
					 * prefix */
    int             suffix = L_NONE;	/* indicates suffix must be output */
    struct l_information_label *il;	/* work ptr to an IL */

    if (!l_encodings_initialized())
	return (FALSE);		/* return if encodings not in place */

    /*
     * First make sure that the classification passed is not too high, too low,
     * or does not represent a value for which no human-readable name has been
     * defined.  If so, return an error.
     */

    if (class > l_hi_sensitivity_label->l_classification
	|| class < *l_min_classification || !l_long_classification[class])
	return (FALSE);

    /*
     * Next, create and fill in a parse table for this label.
     */

    if (caller_parse_table != NO_PARSE_TABLE)
	parse_table = caller_parse_table;
    else {
	parse_table = (char *) calloc(l_tables->l_num_entries, 1);
	if (parse_table == NULL)
	    return (FALSE);
    }

    make_parse_table(parse_table, class, comps_ptr, marks_ptr, l_tables, flags);

    /*
     * Next, check the label and save result of check in valid.
     */

    if (check_validity)
	valid = label_valid(parse_table, class, comps_ptr, marks_ptr, l_tables);
    else
	valid = TRUE;	/* assume valid if check_validity not requested */

    /*
     * Now, if the label is valid, convert it to a human-readable string.
     */

    if (valid) {		/* convert the label into the output string */

	/*
	 * Put classification in output string unless NO_CLASSIFICATION
	 * requested.
	 */

	if (l_classification != NO_CLASSIFICATION) {
	    (void) strcpy(string, l_classification[class]);

	    if (information_label != NO_INFORMATION_LABEL) {
		/* if caller wants information label floated */
		if (l_classification == l_long_classification)
		    il = l_lc_name_information_label[class];
		else if (l_classification == l_short_classification)
		    il = l_sc_name_information_label[class];
		else if (l_classification == l_alternate_classification)
		    il = l_ac_name_information_label[class];

		if (il != NO_INFORMATION_LABEL)
		    float_il(information_label, il);
	    }
	} else
	    string[0] = '\0';

	/*
	 * Loop through each entry in the parse table to determine whether
	 * each word is to be output.
	 */

	for (i = l_tables->l_first_main_entry; i < l_tables->l_num_entries; i++)
	    if (parse_table[i]) {

		/*
		 * Given that this word is to be output, first determine
		 * whether a / must be output as a separation character.
		 * A slash is needed if this is the non-first word output
		 * after a prefix or before a suffix.
		 */

		if (suffix >= 0 && suffix == l_tables->l_words[i].l_w_suffix
		    /* if suffix for this word is same as the previous */
		    || prefix >= 0 && prefix == l_tables->l_words[i].l_w_prefix)

		    /* if prefix for this word is same as the previous */
		    (void) strcat(string, "/");

		/*
		 * If this word is not a continuation of words before a suffix
		 * or after a prefix and if the previous word required a suffix,
		 * then output that suffix now.
		 */

		else {
		    if (suffix >= 0) {
			/* output the suffix */
			string += strlen(string);
			/* delimiter before suffix */
			*string = ' ';
			(void) strcpy(&string[1],
				  (use_short_names &&
			           l_tables->l_words[suffix].l_w_soutput_name) ?
				    l_tables->l_words[suffix].l_w_soutput_name :
				    l_tables->l_words[suffix].l_w_output_name);

			if (information_label != NO_INFORMATION_LABEL) {
			    il = (use_short_names &&
				  l_tables->l_words[suffix].l_w_soutput_name) ?
			    l_tables->l_words[suffix].l_w_sn_information_label :
			    l_tables->l_words[suffix].l_w_ln_information_label;

			    if (il != NO_INFORMATION_LABEL)
				float_il(information_label, il);
			}
		    }
		    /*
		     * Now, assume we have no prefix and then check whether
		     * this word requires one.  If so, remember what prefix it
		     * requires and output the prefix.  Otherwise, output just
		     * the normal blank separator.
		     */

		    prefix = L_NONE;  /* end of prefix we had output (if any) */
		    if (l_tables->l_words[i].l_w_prefix >= 0) {
			/* if prefix required */

			/* remember prefix */
			prefix = l_tables->l_words[i].l_w_prefix;
			string += strlen(string);
			*string = ' ';	/* delimiter before suffix */
			(void) strcpy(&string[1],
				  (use_short_names &&
				   l_tables->l_words[prefix].l_w_soutput_name) ?
				   l_tables->l_words[prefix].l_w_soutput_name :
				   l_tables->l_words[prefix].l_w_output_name);

			if (information_label != NO_INFORMATION_LABEL) {
			    il = (use_short_names &&
				  l_tables->l_words[prefix].l_w_soutput_name) ?
			    l_tables->l_words[prefix].l_w_sn_information_label :
			    l_tables->l_words[prefix].l_w_ln_information_label;

			    if (il != NO_INFORMATION_LABEL)
				float_il(information_label, il);
			}
		    }
		    /* put on normal separator */
		    (void) strcat(string, " ");
		}

		/*
		 * Now, if this word requires that a suffix be printed after
		 * it, remember in index of the suffix in the word table for
		 * output near the top of this loop, or after loop.
		 */

		/* will be L_NONE or index of suffix in word table */
		suffix = l_tables->l_words[i].l_w_suffix;
		/*
		 * Now we are finally ready to output the word matched.
		 */

		(void) strcat(string,
			      (use_short_names &&
			       l_tables->l_words[i].l_w_soutput_name) ?
			       l_tables->l_words[i].l_w_soutput_name :
			       l_tables->l_words[i].l_w_output_name);

		if (information_label != NO_INFORMATION_LABEL) {
		    il = (use_short_names &&
			  l_tables->l_words[i].l_w_soutput_name) ?
			  l_tables->l_words[i].l_w_sn_information_label :
			  l_tables->l_words[i].l_w_ln_information_label;

		    if (il != NO_INFORMATION_LABEL)
			float_il(information_label, il);
		}
	    }
	/*
	 * Now that all words have been output, output the trailing suffix if
	 * one is needed.
	 */

	if (suffix >= 0) {
	    string += strlen(string);	/* output the suffix */
	    *string = ' ';	/* delimiter before suffix */
	    (void) strcpy(&string[1],
				  (use_short_names &&
				   l_tables->l_words[suffix].l_w_soutput_name) ?
				   l_tables->l_words[suffix].l_w_soutput_name :
				   l_tables->l_words[suffix].l_w_output_name);

	    if (information_label != NO_INFORMATION_LABEL) {
		il = (use_short_names &&
		      l_tables->l_words[suffix].l_w_soutput_name) ?
		      l_tables->l_words[suffix].l_w_sn_information_label :
		      l_tables->l_words[suffix].l_w_ln_information_label;

		if (il != NO_INFORMATION_LABEL)
		    float_il(information_label, il);
	    }
	}
    }
    /*
     * Finally, free the parse table if allocated, and return the result of
     * the validity check.
     */

    if (caller_parse_table == NO_PARSE_TABLE)
	(void) free(parse_table);

    return (valid);
}


#ifndef TSOL
/*
 * The external subroutine l_b_parse uses l_parse (above) to parse an input
 * string of the form:
 *
 *		INFORMATION LABEL [SENSITIVITY LABEL]
 *
 * into the internal representation of the information label and sensitivity
 * label specified.  If a parsing error occurs, the index into the passed
 * string where parsing stopped is returned.  L_GOOD_LABEL is returned if
 * everything parsed OK.  If invalid classifications are passed,
 * L_BAD_CLASSIFICATION is returned.
 *
 * Since the legality of an information label is determined by its associated
 * sensitivity label (because the IL classification and compartments cannot
 * be above the SL classification and compartments), the sensitivity label is
 * parsed first, and its values are used in parsing the information label.
 *
 * The passed maximum classification and compartments are used when parsing the
 * sensitivity label, so that no information classified above these maxima
 * are revealed.
 *
 * When parsing the information label, its maximum value is taken to be the
 * sensitivity level.
 *
 * If no [ is present in the input string, the passed sensitivity label is left
 * unchanged.  If a [ is present, the closing ] is optional.
 *
 * If no [ is present in the input string, and if the sensitivity label
 * classification is NO_LABEL, then neither label is changed, and 0 is
 * returned, indicating the entire input string is in error.
 *
 * If the last argument is TRUE, the sensitivity label can be changed, and is
 * therefore parsed.  Otherwise, it is completely ignored if present.
 *
 * This subroutine implements an important convention for specifying changes to
 * both an IL and a SL: that the IL cannot be raised above the SL unless the
 * user first or concurrently requests that the SL be appropriately raised.
 * Therefore, the SL portion is parsed first so that a corresponding rise in
 * the IL is not disallowed because the SL hasn't been raised yet.  Also,
 * this subroutine assures that the SL can never be lowered below the IL.
 *
 * The above describes the behavior of l_b_parse in its normal mode of
 * operation. However, if called with the iclass_ptr or sclass_ptr argument
 * pointing to a classification with the value FULL_PARSE, l_b_parse operates
 * in full_parse mode for the type of label whose classification was passed
 * as FULL_PARSE.  See l_parse (above), for a description of full_parse mode.
 * The sclass_ptr should not be pointing to a classification with the value
 * FULL_PARSE if allow_sl_changing is FALSE.
 *
 * If no [ is present in the input string, or if allow_sl_changing if FALSE, and
 * if the sensitivity label classification is FULL_PARSE, then neither label
 * is changed, and 0 is returned, indicating the entire input string is in
 * error.
 *
 * The global temporary compartments and markings (l_t_compartments and
 * l_t_markings) are changed if the allow_sl_changing argument is TRUE.  The
 * input string passed is changed by l_b_parse, but is restored to its
 * original value before returning.
 */

int
l_b_parse(input, iclass_ptr, icomps_ptr, imarks_ptr, sclass_ptr, scomps_ptr,
	  max_class, max_comps_ptr, allow_sl_changing)
    char           *input;	/* the input string to parse */
    CLASSIFICATION *iclass_ptr;	/* the information label classification */
    COMPARTMENTS   *icomps_ptr;	/* the information label compartments */
    MARKINGS       *imarks_ptr;	/* the information label markings */
    CLASSIFICATION *sclass_ptr;	/* the sensitivity label classification */
    COMPARTMENTS   *scomps_ptr;	/* the sensitivity label compartments */
    CLASSIFICATION  max_class;	/* the maximum classification to reveal */
    COMPARTMENTS   *max_comps_ptr;	/* the maximum compartments to reveal */
    int             allow_sl_changing;	/* flag sez SL can be changed */
{
    register char  *sen_label;	/* ptr to start of SL in input */
    register char  *end_bracket;/* for scanning SL for ] */
    int             got_start_bracket = FALSE;	/* flag sez input had an
						 * starting [ */
    int             got_end_bracket = FALSE;	/* flag sez input had an
						 * ending ] */
    int             error;	/* saves return code from l_parse */
    CLASSIFICATION  temp_class;	/* temporary storage for SL classification */

    if (!l_encodings_initialized())
	return (0);		/* error return if encodings not in place */

    /*
     * First make sure that the passed iclass_ptr, sclass_ptr, and max_class
     * arguments represent valid classifications.  Return a
     * L_BAD_CLASSIFICATION if not.  The iclass_ptr and sclass_ptr arguments
     * will be checked for validity only if they have values other than
     * NO_LABEL or FULL_PARSE.
     */

    if (*iclass_ptr != NO_LABEL
	&& *iclass_ptr != FULL_PARSE
	&& (*iclass_ptr > l_hi_sensitivity_label->l_classification
	    || *iclass_ptr < *l_min_classification
	    || !l_long_classification[*iclass_ptr]))
	return (L_BAD_CLASSIFICATION);

    if (*sclass_ptr != NO_LABEL
	&& *sclass_ptr != FULL_PARSE
	&& (*sclass_ptr > l_hi_sensitivity_label->l_classification
	    || *sclass_ptr < *l_min_classification
	    || !l_long_classification[*sclass_ptr]))
	return (L_BAD_CLASSIFICATION);

    if (max_class > l_hi_sensitivity_label->l_classification
	|| max_class < *l_min_classification
	|| !l_long_classification[max_class])
	return (L_BAD_CLASSIFICATION);

    /*
     * Determine whether there are SL changes following a [ in the input.
     * If so, process the SL first if allow_sl_changing is TRUE.
     */

    for (sen_label = input; *sen_label; sen_label++)	/* scan input */
	if (*sen_label == '[') {	/* if SL found */
	    got_start_bracket = TRUE;	/* remember we changed [ in input */
	    *sen_label = '\0';	/* terminate IL string */

	    if (allow_sl_changing) {

		/*
		 * To process the SL changes, make a temporary copy of the SL
		 * to change with l_parse. If the IL parses OK, and is
		 * dominated by the SL,then the (possibly changed) temporary SL
		 * can be copied into the real SL.  Otherwise, neither label is
		 * changed.
		 */

		/* make temp copy of SL */
		temp_class = *sclass_ptr;
		COMPARTMENTS_COPY(l_t_compartments, scomps_ptr);

		/*
		 * Find the ending bracket of the SL if any, and if present,
		 * change it to a \0 and remember that we changed it so we
		 * can change it back before returning.
		 */

		/* scan SL for closing ] */
		for (end_bracket = ++sen_label; *end_bracket; end_bracket++)
		    if (*end_bracket == ']') {
			got_end_bracket = TRUE;	/* remember we removed a ] */
			*end_bracket = '\0';	/* remove closing ] */
			break;
		    }
		/*
		 * Now, parse the SL with the range of l_lo_sensitivity_label
		 * to the passed maxima. Return an error if it cannot parse
		 * within this range.
		 */

		error = l_parse(sen_label, &temp_class, l_t_compartments,
				l_t_markings, l_sensitivity_label_tables,
				l_lo_sensitivity_label->l_classification,
				l_lo_sensitivity_label->l_compartments,
				max_class, max_comps_ptr);

		if (got_end_bracket)
		    *end_bracket = ']';	/* put ] back in caller's input
					 * string */

		if (error != L_GOOD_LABEL) {
		    /* if an SL parsing error occurred */

		    /* put [ back in caller's input string */
		    *--sen_label = '[';
		    if (error != L_BAD_LABEL)
		        /* FULL_PARSE error on SL */
			/* adjust error to be in SL part */
			error += (sen_label - input);

		    return (error);	/* return proper error index */
		}
		/*
		 * Now, parse the IL with the range of the lowest IL to the
		 * SL parsed above. Return an error if it cannot parse within
		 * this range.
		 */

		error = l_parse(input, iclass_ptr, icomps_ptr, imarks_ptr,
				l_information_label_tables,
				*l_min_classification, l_li_compartments,
				temp_class, l_t_compartments);

		*--sen_label = '[';	/* put [ back in caller's input
					 * string */

		if (error != L_GOOD_LABEL)
		    return (error);	/* return if an error occurred */

		/*
		 * Now, since both the IL and the SL parsed OK, there is one
		 * final check that must be made, in case no IL changes were
		 * made, but the SL was lowered below the IL.  If the SL does
		 * not dominate the IL, an error is reported for the entire
		 * input (i.e. starting at the first character in the input).
		 */

		if (*iclass_ptr > temp_class
		    || !COMPARTMENTS_DOMINATE(l_t_compartments, icomps_ptr))
		    return (0);	/* error return for entire input */

		/*
		 * Now that ALL error checks have been passed, the temporary,
		 * changed, SL can be copied to the real SL, and a return
		 * with no error can be made.
		 */

		*sclass_ptr = temp_class;  /* no errors...copy temp SL to SL */
		COMPARTMENTS_COPY(scomps_ptr, l_t_compartments);

		return (L_GOOD_LABEL);	/* return with no error */
	    } else
		break;		/* out of search for [ loop */
	}
    /*
     * In the case that there was no SL changes present, or the SL can't be
     * changed, just parse the IL and pass along its return code.  If
     * *sclass_ptr is NO_LABEL or FULL_PARSE, just return without doing
     * anything, indicating the entire input string as being in error.
     */

    if (*sclass_ptr == NO_LABEL || *sclass_ptr == FULL_PARSE)
	return (0);		/* entire string is in error */

    error = l_parse(input, iclass_ptr, icomps_ptr, imarks_ptr,
		    l_information_label_tables, *l_min_classification,
		    l_li_compartments, *sclass_ptr, scomps_ptr);

    if (got_start_bracket)
	*sen_label = '[';	/* restore input string */

    return (error);
}

/*
 * The external subroutine l_b_convert uses l_convert (above) to convert the
 * passed information label and sensitivity label internal representations
 * into a string of the form:
 *
 *		INFORMATION LABEL [SENSITIVITY LABEL]
 *
 * The passed string must be of size l_information_label_tables->l_max_length +
 * l_sensitivity_label_tables->l_max_length + 3.
 *
 * If the caller desires, the information label and sensitivity label parse
 * tables used for conversion can be returned in the passed parse tables.
 * The parse tables are passed as above for l_convert.  The use_short_names
 * argument has the same meaning as above for l_convert.
 *
 * If the argument check_validity is TRUE, l_b_convert performs a complete
 * validity check on the two labels.  If check_validity is FALSE, l_b_convert
 * checks only that the classification arguments represent proper
 * classifications.
 *
 * If the argument information_label is passed as NO_INFORMATION_LABEL, then
 * l_b_convert does not compute or return the information label of the human-
 * readable string returned.  If information_label is not NULL, then it is
 * assumed to point to a valid l_information_label structure which will be
 * FLOATED with the information labels of all names returned in string.
 *
 * l_b_convert returns FALSE and performs no conversion if the classification
 * arguments are bad or if the requested validity checks fail.  Otherwise
 * l_b_convert returns TRUE and performs a conversion.
 */

int
l_b_convert(string, iclass, icomps_ptr, imarks_ptr, iparse_table, sclass,
	    scomps_ptr, sparse_table, use_short_names, check_validity,
	    information_label)
    char           *string;	/* the place to return the output string */
    CLASSIFICATION  iclass;	/* the information label classification */
    COMPARTMENTS   *icomps_ptr;	/* the information label compartments */
    MARKINGS       *imarks_ptr;	/* the information label markings */
    char           *iparse_table;	/* the information label parse table */
    CLASSIFICATION  sclass;	/* the sensitivity label classification */
    COMPARTMENTS   *scomps_ptr;	/* the sensitivity label compartments */
    char           *sparse_table;	/* the sensitivity label parse table */
    int             use_short_names;	/* flag sez use short names for
					 * output */
    int             check_validity;	/* flag sez check validity of label */
				     /* information labels of label returned */
    struct l_information_label *information_label;
{
    register char  *sp;		/* fast ptr into output string */

    if (!l_encodings_initialized())
	return (FALSE);		/* error return if encodings not in place */

    sp = string;

    if (!l_convert(sp, iclass, l_long_classification, icomps_ptr, imarks_ptr,
		   l_information_label_tables, iparse_table, use_short_names,
		   ALL_ENTRIES, check_validity, information_label))
	return (FALSE);

    sp += strlen(sp);	/* sp now ptr after information label in string */

    (void) strcpy(sp, " [");	/* add starting sensitivity label delimiter */

    sp += 2;			/* sp now ptr to place for sensitivity label */

    if (!l_convert(sp, sclass, l_short_classification, scomps_ptr,
		   l_in_markings[sclass], l_sensitivity_label_tables,
		   sparse_table, use_short_names, ALL_ENTRIES, check_validity,
		   information_label))
	return (FALSE);

    (void) strcat(sp, "]");	/* add closing delimiter */

    return (TRUE);
}
#endif /* TSOL */

/*
 * The external subroutine l_in_accreditation_range returns TRUE iff the
 * passed classification and compartments are in the accreditation range of
 * the system.
 */

int
l_in_accreditation_range(class, comps_ptr)
    CLASSIFICATION  class;	/* sensitivity label classification */
    register COMPARTMENTS *comps_ptr;	/* sensitivity label compartments */
{
    register COMPARTMENTS *ar;	/* fast ptr to accreditation range
				 * compartments */

    if (!l_encodings_initialized())
	return (FALSE);		/* error return if encodings not in place */

    /*
     * First make sure that the classification passed is not too high, too low,
     * or does not represent a value for which no human-readable name has been
     * defined.  If so, return an error.
     */

    if (class > l_hi_sensitivity_label->l_classification
	|| class < *l_min_classification || !l_long_classification[class])
	return (FALSE);

    /*
     * Now make proper check based on the type of accreditation range
     * specification for the classification specified.
     */

    switch (l_accreditation_range[class].l_ar_type) {
    case L_ALL_VALID_EXCEPT:

	for (ar = l_accreditation_range[class].l_ar_start;
	     ar != l_accreditation_range[class].l_ar_end;
	     ar += COMPARTMENTS_SIZE)
	    if (COMPARTMENTS_EQUAL(comps_ptr, ar))
		break;

	if (ar != l_accreditation_range[class].l_ar_end)
	    return (FALSE);	/* no SL in AR */
	else
	    return (TRUE);

    case L_ALL_VALID:

	return (TRUE);

    case L_ONLY_VALID:

	for (ar = l_accreditation_range[class].l_ar_start;
	     ar != l_accreditation_range[class].l_ar_end;
	     ar += COMPARTMENTS_SIZE)
	    if (COMPARTMENTS_EQUAL(comps_ptr, ar))
		break;
	if (ar != l_accreditation_range[class].l_ar_end)
	    return (TRUE);	/* found SL in AR */
	else
	    return (FALSE);

    case L_NONE_VALID:
    default:
	return (FALSE);
    }
}

/*
 * The external subroutine l_valid checks the validity if the internal format
 * of a label based on the passed classification, compartments, markings,
 * label tables, and flags.  See the subroutine label_valid for a description
 * of the checking performed.  TRUE is returned if the label is valid given
 * the type of label indicated by the l_tables.  FALSE is returned otherwise.
 * The validity check is based on the l_tables passed.  Only l_tables that
 * can be used to parse a human-readable label into internal form should be
 * used (i.e. only l_information_label_tables, l_sensitivity_label_tables, or
 * l_clearance_tables).  When a sensitivity label or clearance is being
 * checked for validity, the markings passed MUST be l_in_markings[class],
 * where class is the classification passed.  If any other markings are
 * passed for a sensitivity label or clearance, l_valid will return FALSE.
 */

int
l_valid(class, comps_ptr, marks_ptr, l_tables, flags)
    CLASSIFICATION  class;	/* the label classification */
    COMPARTMENTS   *comps_ptr;	/* the label compartments */
    MARKINGS       *marks_ptr;	/* the label markings */
    register struct l_tables *l_tables;	/* the l_tables to use */
    int             flags;	/* l_w_flags of l_word entries to use */
{
    register char  *parse_table;/* ptr to parse table for this label */
    register int    valid;	/* flag sez label is valid */

    if (!l_encodings_initialized())
	return (FALSE);		/* error if encodings not in place */

    /*
     * First make sure that the classification passed is not too high, too low,
     * or does not represent a value for which no human-readable name has been
     * defined.  If so, return an error.
     */

    if (class > l_hi_sensitivity_label->l_classification
	|| class < *l_min_classification || !l_long_classification[class])
	return (FALSE);

    /*
     * Next, create and fill in a parse table for this label.
     */

    parse_table = (char *) calloc(l_tables->l_num_entries, 1);

    if (parse_table == NULL)
	return (FALSE);

    make_parse_table(parse_table, class, comps_ptr, marks_ptr, l_tables, flags);

    /*
     * Next, check the label and save result of check in valid.
     */

    valid = label_valid(parse_table, class, comps_ptr, marks_ptr, l_tables);

    /*
     * Finally, free the parse table and return the result of label_valid.
     */

    (void) free(parse_table);
    return (valid);
}

/*
 * The external subroutine l_changeability determines whether each
 * classification's and each word's presence in a label can be changed (i.e.,
 * whether the classification can be changed or whether the word can be added
 * if not present, or removed if present).  These indications are returned in
 * the passed boolean character arrays class_changeable and word_changeable.
 * Class_changeable must contain l_hi_sensitivity_label-> l_classification+1
 * entries.  Word_changeable must be the same size as a parse_table for the
 * label.  The passed l_tables, parse_table, and class must all indicate the
 * label.  The parse_table should have been computed by a previous call to
 * l_convert or l_b_convert.  The passed min_class, min_comps_ptr, max_class,
 * and max_comps_ptr, bound the valid values for the label.
 *
 * If word_changeable is passed as a NULL ptr, then word changeability is not
 * checked.  If class_changeable is passed as a NULL ptr, then classification
 * changeability is not checked.
 *
 * This subroutine is useful in implementing a graphical user interface for
 * label changing.  The information returned in class_changeable and
 * word_changeable can be used to graphically denote which label
 * classifications and words can be changed (added or removed) in a label
 * changing interface.
 *
 * l_changeability CANNOT be called with a class of NO_LABEL.  It makes no sense
 * to determine the changeability of classifications and words in a label if
 * you don't have a label!
 *
 * l_changeability will return FALSE if any errors occur, else TRUE.
 */

int
l_changeability(class_changeable, word_changeable, parse_table, class, l_tables,
		min_class, min_comps_ptr, max_class, max_comps_ptr)
    char           *class_changeable;	/* table of classification
					 * changeability flags */
    char           *word_changeable;	/* table of word changeability flags */
    char           *parse_table;	/* parse table representing label */
    CLASSIFICATION  class;		/* the label classification */
    register struct l_tables *l_tables;	/* the encodings tables to use */
    CLASSIFICATION  min_class;		/* min classification possible */
    COMPARTMENTS   *min_comps_ptr;	/* min compartments possible */
    CLASSIFICATION  max_class;		/* max classification to acknowledge */
    COMPARTMENTS   *max_comps_ptr;	/* max compartments to acknowledge */
{
    register int    i;			/* array index */
    CLASSIFICATION  words_min_class;	/* min class required by words present */
    CLASSIFICATION  words_max_class;	/* max class required by words present */

    if (!l_encodings_initialized())
	return (FALSE);		/* error return if encodings not in place */

    /*
     * First, loop through the parse table, computing words_min_class, the
     * minimum classification required given the words that are on, and
     * words_max_class, the maximum classification required by words that are
     * on.
     */

    words_min_class = min_class;
    words_max_class = max_class;

    for (i = l_tables->l_first_main_entry; i < l_tables->l_num_entries; i++)
	if (parse_table[i]) {	/* if this word is on... */
	    words_min_class = L_MAX(words_min_class,
				    l_tables->l_words[i].l_w_min_class);

	    words_max_class = L_MIN(words_max_class,
				    l_tables->l_words[i].l_w_max_class);

	}
    /*
     * Next, if class_changeable is not NULL... loop through the
     * classifications, setting the flags in class_changeable to be TRUE if
     * and only if the label's classification can be changed to the
     * corresponding classification, or if the corresponding classification
     * IS the label's classification, and the label's classification can be
     * changed.
     */

    if (class_changeable != NULL)
	for (i = 0; i <= l_hi_sensitivity_label->l_classification; i++) {
	    if (l_long_classification[i]	/* if a valid classification */
		&&words_min_class != words_max_class
		&& i >= words_min_class
		&& i <= words_max_class) {

		/*
		 * At this point the classification is between the min and
		 * the max, and isn't the ONLY classification (which would
		 * make it unchangeable!). We can conclude that the
		 * classification is changeable unless it is greater than the
		 * current classification, and if raising the classification
		 * forces either 1) an inverse compartment forced on by the
		 * max_comps, 2) a normal compartment forced on by the
		 * min_comps, or 3) a word forced on by the initial
		 * compartments and markings to be present in the label
		 * because the new classification is now greater than or
		 * equal to its omin_class.  If so, see if the inverse
		 * compartment or initial word can be turned on.  If turning
		 * it on fails (e.g., because some other other word with
		 * which it cannot be combined is present in the label, then
		 * indicate that the classification is NOT changeable.  The
		 * subroutine turnon_forced_words checks whether the forced
		 * words can be turned on.
		 */

		class_changeable[i] = TRUE;	/* assume for now */

		if (i > class) {  /* if classification above current one */
		    class_changeable[i] = turnon_forced_words(parse_table, i,
							      l_tables,
							      min_comps_ptr,
							      max_class,
							      max_comps_ptr,
							      &words_max_class,
							      CHECK_ONLY);
		}
	    } else	/* if classification not between min and max,
			 * or is the ONLY possible classification value */
		class_changeable[i] = FALSE;
	}

    /*
     * Next, if word_changeable is not NULL... For each entry in the
     * parse_table, determine whether or not its state can be changed from
     * OFF to ON or ON to OFF, and indicate its changeability in
     * word_changeable.
     */

    if (word_changeable != NULL)
	for (i = l_tables->l_first_main_entry;
	     i < l_tables->l_num_entries; i++) {

	    /*
	     * If the word is currently on, check whether it can be turned
	     * off with turnoff_word, changing word_changeable to TRUE if so.
	     */

	    if (parse_table[i]) {	/* word is on...can it be turned off? */
		word_changeable[i] = turnoff_word(l_tables, parse_table, i,
						  class, min_comps_ptr,
						  &words_max_class, max_class,
						  max_comps_ptr, CHECK_ONLY);
	    }
	    /*
	     * Next, if the word is currently off, check whether it can be
	     * turned on with turnon_word, changing word_changeable to TRUE
	     * if so.
	     */

	    else {		/* word is off, can it be turned on? */
		word_changeable[i] = turnon_word(l_tables, parse_table, i,
						 class, min_comps_ptr,
						 &words_max_class, max_class,
						 max_comps_ptr, CHECK_ONLY);
	    }

	}
    return (TRUE);
}

/*
 * The external subroutine l_visibility determines whether each
 * classification and each word could ever appear in a label given the passed
 * l_tables, minimum classification/compartments, and maximum classification/
 * compartments.  These indications are returned in the passed boolean
 * character arrays class_visible and word_visible.  Class_visible must
 * contain l_hi_sensitivity_label->l_classification+1 entries.  Word_visible
 * must be the same size as a parse_table for the label with the passed
 * l_tables.  The passed min_class, min_comps_ptr, max_class, and
 * max_comps_ptr bound the valid values for the label.
 *
 * If word_visible is passed as a NULL ptr, then word visibility is not checked.
 * If class_visible is passed as a NULL ptr, then classification visibility
 * is not checked.
 *
 * This subroutine is useful in implementing a graphical user interface for
 * label changing.  The information returned in class_visible and
 * word_visible can be used to graphically denote which label classifications
 * and words can be placed in a dialog box or help information giving valid
 * classifications/words to specify.
 *
 * l_visibility will return FALSE if any errors occur, else TRUE.
 */

int
l_visibility(class_visible, word_visible, l_tables, min_class, min_comps_ptr,
	     max_class, max_comps_ptr)
    char           *class_visible;	/* table of classification
					 * changeability flags */
    char           *word_visible;	/* table of word changeability flags */
    register struct l_tables *l_tables;	/* the encodings tables to use */
    CLASSIFICATION  min_class;		/* min classification possible */
    COMPARTMENTS   *min_comps_ptr;	/* min compartments possible */
    CLASSIFICATION  max_class;		/* max classification to acknowledge */
    COMPARTMENTS   *max_comps_ptr;	/* max compartments to acknowledge */
{
    register int    i;			/* array index */
    char           *parse_table;	/* a parse table to use as temporary */

    if (!l_encodings_initialized())
	return (FALSE);		/* error return if encodings not in place */

    parse_table = (char *) calloc(l_tables->l_num_entries, 1);

    if (parse_table == NULL)
	return (FALSE);

    /*
     * If class_visible is not NULL... First, loop through the
     * classifications, setting the flags in classifications_ptr to be TRUE
     * if and only if the label's classification can be changed to the
     * corresponding classification, or if the corresponding classification
     * IS the label's classification, and the label's classification can be
     * changed.  We can change to a classification only if turning on all
     * words forced on at that classification could succeed (checked by
     * turnon_forced_words).
     */

    if (class_visible != NULL)
	for (i = 0; i <= l_hi_sensitivity_label->l_classification; i++) {
	    if (l_long_classification[i]	/* if a valid classification */
		&&i >= min_class
		&& i <= max_class)
		class_visible[i] = turnon_forced_words(parse_table, i, l_tables,
						       min_comps_ptr, max_class,
						       max_comps_ptr,
						       &max_class, CHECK_ONLY);
	    else
		class_visible[i] = FALSE;
	}

    /*
     * If word_visible is not NULL... Now scan the word table, checking
     * whether each word could be turned on as the first word in a label
     * (class=NO_LABEL).  If so, consider the word visible.  If not, consider
     * it invisible.  A parse table must be allocated to be passed to
     * turnon_word.  It is freed after use.
     */

    if (word_visible != NULL)
	for (i = l_tables->l_first_main_entry;
	     i < l_tables->l_num_entries; i++) {
	    word_visible[i] = turnon_word(l_tables, parse_table, i, NO_LABEL,
					  min_comps_ptr, &max_class, max_class,
					  max_comps_ptr, CHECK_ONLY);
	}

    free(parse_table);
    return (TRUE);
}
