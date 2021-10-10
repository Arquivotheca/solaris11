/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * This file may contain confidential information of the Defense
 * Intelligence Agency and MITRE Corporation and should not be
 * distributed in source form without approval from Sun Legal.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifndef	lint
static char *l_ID = "CMW Labels Release 2.2; 8/25/93: Analyze_Encodings.c";
#endif	/* lint */

/*
 *	chk_encodings - check the syntax and analyze a label encodings file.
 *				From GFI 2.2 Analyze_Encodings.
 *
 *	Entry	argc[1] = "-a" for analysis plus syntax check.
 *		argc[2] = "-c max class" maximum class value.  Default 255.
 *		argc[3] = pathname of label encodings file to check.
 *			  default is /etc/security/label_encodings.
 *
 *	Exit	stdout = various messages.
 *		exit = 0, no syntax errors noted.
 *		     = 1, some syntax error(s) noted.
 */
#include <locale.h>
#include <stdarg.h>
#include <sys/vnode.h>

#include "gfi/std_labels.h"

#undef  NO_CLASSIFICATION
#undef  ALL_ENTRIES
#undef  ACCESS_RELATED
#undef  SHORT_WORDS
#undef  LONG_WORDS

#include <sys/tsol/label.h>

#include "impl.h"

char	*Admin_low = ADMIN_LOW;
char	*Admin_high = ADMIN_HIGH;
int	Admin_low_size;
int	Admin_high_size;

/* CIPSO maximum field sizes */

#define	CIPSO_CLASS_MAX	255
#define	CIPSO_COMPS_MAX	239

/*
 * The external subroutine l_error syslogs an appropriate error message about
 * a fatal conversion error.
 */

/* PRINTFLIKE2 */
void
l_error(const unsigned int line_number, const char *format, ...)
{
	va_list	v;

	if (line_number == 0) {
		(void) fprintf(stderr,
		    gettext("Label encodings conversion error:\n   "));
	} else {
		(void) fprintf(stderr,
		    gettext("Label encodings conversion error at "
		    "line %d:\n   "), line_number);
	}

	va_start(v, format);
	(void) vfprintf(stderr, gettext(format), v);
	va_end(v);
}

static void
printf_word(struct l_tables *l_tables, int i)
{
	boolean_t	do_short = B_FALSE;
	char		*short_prefix;
	char		*short_word;
	char		*short_suffix;

	/*
	 * First output the prefix if any, setting do_short if there is a short
	 * prefix, and setting short_prefix to the short prefix if present,
	 * else the output one.
	 */

	if (l_tables->l_words[i].l_w_prefix >= 0) {
		(void) printf("%s ", l_tables->l_words[
		    l_tables->l_words[i].l_w_prefix].l_w_output_name);

		if (l_tables->l_words[l_tables->l_words[
		    i].l_w_prefix].l_w_soutput_name != NULL) {
			do_short = B_TRUE;
			short_prefix = l_tables->l_words[
			    l_tables->l_words[i].l_w_prefix].l_w_soutput_name;
		} else {
			short_prefix = l_tables->l_words[
			    l_tables->l_words[i].l_w_prefix].l_w_output_name;
		}
	}

	/*
	 * Next output the word itself, setting do_short if there is a short
	 * word, and setting short_word to the short word if present,
	 * else the output one.
	 */

	(void) printf("%s", l_tables->l_words[i].l_w_output_name);

	if (l_tables->l_words[i].l_w_soutput_name != NULL) {
		do_short = B_TRUE;
		short_word = l_tables->l_words[i].l_w_soutput_name;
	} else {
		short_word = l_tables->l_words[i].l_w_output_name;
	}

	/*
	 * Next output the suffix if any, setting do_short if there is a short
	 * suffix, and setting short_suffix to the short suffix if present,
	 * else the output one.
	 */

	if (l_tables->l_words[i].l_w_suffix >= 0) {

		(void) printf(" %s", l_tables->l_words[
		    l_tables->l_words[i].l_w_suffix].l_w_output_name);
		if (l_tables->l_words[
		    l_tables->l_words[i].l_w_suffix].l_w_soutput_name != NULL) {
			do_short = B_TRUE;
			short_suffix = l_tables->l_words[
			    l_tables->l_words[i].l_w_suffix].l_w_soutput_name;
		} else {
			short_suffix = l_tables->l_words[
			    l_tables->l_words[i].l_w_suffix].l_w_output_name;
		}
	}

	/*
	 * Now, if there are short versions of the word, its prefix, or its
	 * suffix, put the shortened version of this word in parentheses after
	 * the long version (put in string above).
	 */

	if (do_short) {
		if (l_tables->l_words[i].l_w_prefix >= 0) {
			(void) printf(" (%s %s", short_prefix, short_word);
		} else {
			(void) printf(" (%s", short_word);
		}

		if (l_tables->l_words[i].l_w_suffix >= 0) {
			(void) printf(" %s)", short_suffix);
		} else {
			(void) printf(")");
		}
	}
}

static void
printf_word_names(struct l_tables *l_tables, int i)
{
	int	j;

	printf_word(l_tables, i);

	/* Produce any needed exact alias:  / longname(shortname) */

	for (j = l_tables->l_first_main_entry; j < l_tables->l_num_entries;
	    j++) {
		if (l_tables->l_words[j].l_w_exact_alias == i) {

			/* if synonym */
			(void) printf(" / ");
			/* Produce longname (shortname) */
			printf_word(l_tables, j);
		}
	}
}

#define	FIRST	0
#define	NEXT	1

/*
 * The function COMPARTMENT_BIT_ON returns the bit number of the FIRST, or
 * NEXT ON compartment bit from the passed COMPARTMENTS argument.  A -1 is
 * returned if there are no more ON bits left.
 *
 * This function depends on the internal structure for COMPARTMENTS.  It must
 * must always corrolate the structure for compartments in std_labels.h.
 *
 * The code below assumes that the internal representation of COMPARTMENTS
 * consists of an array of some size integer.  The define
 * TYPE_OF_COMPARTMENTS_PIECE is the type of integer used, long or short. The
 * define BITS_PER_COMPARTMENTS_PIECE is the number of bits in each piece.
 * The define PIECES_PER_COMPARTMENTS is the number of pieces in
 * COMPARTMENTS.
 */

#define	COMPARTMENTS_MAX_BITS	256
#define	MARKINGS_MAX_BITS	256

static int
COMPARTMENT_BIT_ON(COMPARTMENTS *comps_ptr, int first_or_next)
{
	static int	bit_index;	/* index of bits within compartments */

	if (first_or_next == FIRST) {
		bit_index = -1;
	}

	while (++bit_index < COMPARTMENTS_MAX_BITS) {
		COMPARTMENTS_ZERO(l_t2_compartments);
		COMPARTMENTS_BIT_SET(*l_t2_compartments, bit_index);
		if (COMPARTMENTS_ANY_BITS_MATCH(l_t2_compartments,
		    comps_ptr)) {
			return (bit_index);
		}
	}
	return (-1);
}

static int
MARKING_BIT_ON(MARKINGS *marks_ptr, int first_or_next)
{
	static int	bit_index;	/* index of bits within markings */

	if (first_or_next == FIRST) {
		bit_index = -1;
	}

	while (++bit_index < MARKINGS_MAX_BITS) {
		MARKINGS_ZERO(l_t2_markings);
		MARKINGS_BIT_SET(*l_t2_markings, bit_index);
		if (MARKINGS_ANY_BITS_MATCH(l_t2_markings, marks_ptr)) {
			return (bit_index);
		}
	}
	return (-1);
}

static void
printf_compartment_bits(COMPARTMENTS *comps_ptr)
{
	int		i;
	boolean_t	consecutive = B_FALSE;
	int		first_bit, last_bit;
	int		previous_bit = -1;

	i = COMPARTMENT_BIT_ON(comps_ptr, FIRST);
	if (i == -1) {
		/* if there are no compartment bits on */
		(void) printf(gettext(" NONE"));
		return;
	}
	for (; i != -1; i = COMPARTMENT_BIT_ON(comps_ptr, NEXT)) {
		if (!consecutive) {
			/*
			 * If we get here, the current ON bit is the
			 * beginning of a new string of ON bits, and
			 * must therefore be placed in the output.
			 * Save first and last bit of possible series
			 * of ON bits.
			 */
			if (previous_bit != -1) {
				first_bit = previous_bit;
			} else {
				first_bit = i;
			}
			last_bit = i;
			(void) printf(" %d", first_bit);
			consecutive = B_TRUE;
		} else {
			if (i == last_bit + 1) {
				/* this ON bit consecutive with previous */
				last_bit = i;
				continue;
			} else {
				/* this ON bit NOT consecutive with previous */
				if (last_bit != first_bit) {
					(void) printf("-%d", last_bit);
					if (i != -1) {
						previous_bit = i;
					}
				} else {
					(void) printf(" %d", i);
				}
				consecutive = B_FALSE;
			}
		}
	}

	if (consecutive && last_bit != first_bit) {
		(void) printf("-%d", last_bit);
	}
}

static void
printf_marking_bits(MARKINGS *marks_ptr)
{
	int		i;
	boolean_t	consecutive = B_FALSE;
	int		first_bit, last_bit;

	i = MARKING_BIT_ON(marks_ptr, FIRST);
	if (i == -1) {
		/* if there are no marking bits on */
		(void) printf(gettext(" NONE"));
		return;
	}
	for (; i != -1; i = MARKING_BIT_ON(marks_ptr, NEXT)) {
		if (!consecutive) {
			/*
			 * If we get here, the current ON bit is the
			 * beginning of a new string of ON bits, and
			 * must therefore be placed in the output.
			 * Save first and last bit of possible series
			 * of ON bits.
			 */
			first_bit = i;
			last_bit = i;
			(void) printf(" %d", first_bit);
			consecutive = B_TRUE;
		} else {
			if (i == last_bit + 1) {
				/* this ON bit consecutive with previous */
				last_bit = i;
				continue;
			} else {
				/* this ON bit NOT consecutive with previous */
				if (last_bit != first_bit) {
					(void) printf("-%d", last_bit);
				}
				consecutive = B_FALSE;
			}
		}
	}

	if (consecutive && last_bit != first_bit) {
		(void) printf("-%d", last_bit);
	}
}

/*
 * The subroutine printf_name_IL prints the information label for the passed
 * name, using the passed internal IL representation. TRUE is returned if all
 * is well, else FALSE.
 */

static void
printf_name_IL(char *name, struct l_information_label *il)
{
	static char	*il_buffer = NULL;

	if (il == NO_INFORMATION_LABEL)
		return;	/* do nothing if no IL specified */

	/*
	 * Allocate the buffer to l_convert the information labels of names
	 * that have been specified.
	 */

	if (il_buffer == NULL) {
		il_buffer =
		    calloc(l_information_label_tables->l_max_length, 1);
		if (il_buffer == NULL) {
			(void) fprintf(stderr,
			    gettext("\n Can't allocate %ld bytes for "
			    "producing information labels.\n"),
			    l_information_label_tables->l_max_length);
			return;
		}
	}

	if (!l_convert(il_buffer, il->l_classification, l_long_classification,
	    il->l_compartments, il->l_markings, l_information_label_tables,
	    NO_PARSE_TABLE, LONG_WORDS, ALL_ENTRIES, FALSE,
	    NO_INFORMATION_LABEL)) {
		(void) fprintf(stderr,
		    gettext("\n Cannot produce information label for name "
		    "\"%s\".\n"), name);
		return;
	}
	(void) printf(gettext("\n   IL(%s) = %s"), name, il_buffer);
}

static void
printf_word_IL(struct l_tables *l_tables, int i)
{
	/* First do long names. */
	printf_name_IL(l_tables->l_words[i].l_w_output_name,
	    l_tables->l_words[i].l_w_ln_information_label);

	/* Next do short names. */
	printf_name_IL(l_tables->l_words[i].l_w_soutput_name,
	    l_tables->l_words[i].l_w_sn_information_label);
}

static void
view_words(char *type, struct l_tables *l_tables)
{
	/* loop indexes into word and classification tables */
	int			i, j;
	boolean_t		got_one;
	struct l_input_name	*input_name;

	/* Produce heading for type of words being produced. */

	(void) printf("\n---> %s WORDS <---\n", type);

	/*
	 * Loop through each non-exact-alias word,
	 * producing various information.
	 */

	for (i = 0; i < l_tables->l_num_entries; i++) {
		if (l_tables->l_words[i].l_w_exact_alias == NO_EXACT_ALIAS) {

			/*
			 * Produce Word: longname (shortname) / aliaslongname
			 * (aliasshortname) ...
			 */

			if (l_tables->l_words[i].l_w_prefix == L_IS_PREFIX) {
				if (l_tables->l_words[i].l_w_cm_mask !=
				    l_0_compartments ||
				    l_tables->l_words[i].l_w_mk_mask !=
				    l_0_markings) {
					(void) printf(gettext("\nSpecial "
					    "Inverse Prefix Word: "));
				} else {
					(void) printf(
					    gettext("\nPrefix Word: "));
				}
			} else if (l_tables->l_words[i].l_w_suffix ==
			    L_IS_SUFFIX) {
				(void) printf(gettext("\nSuffix Word: "));
			} else {
				(void) printf(gettext("\nWord: "));
			}

			/* Produce longname (shortname)... */
			printf_word_names(l_tables, i);
			/*
			 * Produce any input names for the main word
			 * and the alias word(s).
			 */

			for (input_name = l_tables->l_words[i].l_w_input_name;
			    input_name != NO_MORE_INPUT_NAMES;
			    input_name = input_name->next_input_name) {
				(void) printf(gettext("\n   Input name (%s) "
				    "= %s"),
				    l_tables->l_words[i].l_w_output_name,
				    input_name->name_string);
			}

			for (j = l_tables->l_first_main_entry;
			    j < l_tables->l_num_entries; j++) {
				if (l_tables->l_words[j].l_w_exact_alias == i) {
					/* if synonym */
					for (input_name = l_tables->l_words[
					    j].l_w_input_name;
					    input_name != NO_MORE_INPUT_NAMES;
					    input_name = input_name->
					    next_input_name) {
						(void) printf(gettext("\n   "
						    "Input name (%s) = %s"),
						    l_tables->l_words[j
						    ].l_w_output_name,
						    input_name->name_string);
					}
				}
			}

			/* Produce the ILs of any names output above. */

			printf_word_IL(l_tables, i);

			for (j = l_tables->l_first_main_entry;
			    j < l_tables->l_num_entries; j++) {
				if (l_tables->l_words[j].l_w_exact_alias == i) {
					/* if synonym */
					printf_word_IL(l_tables, j);
				}
			}

			/*
			 * Now, if this word is a prefix or a suffix,
			 * continue in the loop, not producing the rest
			 * of the output that is produced for real words.
			 */

			if (l_tables->l_words[i].l_w_prefix == L_IS_PREFIX ||
			    l_tables->l_words[i].l_w_suffix == L_IS_SUFFIX) {
				(void) printf("\n");
				continue;
			}

			/* Produce Valid classification range xx -> yy */

			(void) printf(gettext("\n   Valid classification "
			    "range: %s -> %s\n   Type: "),
			    l_short_classification[L_MAX(l_tables->l_words[i
			    ].l_w_min_class, *l_min_classification)],
			    l_short_classification[L_MIN(l_tables->l_words[i
			    ].l_w_max_class,
			    l_hi_sensitivity_label->l_classification)]);

			/*
			 * For each valid classification, compute the mask
			 * of default bits. To compute these default
			 * compartments and markings, we take the initial
			 * ones and turn OFF those initial ones that are
			 * inverse.  The inverse ones are turned off by 1)
			 * turning them on, then XOR-ing with themselves to
			 * force them off.
			 */

			COMPARTMENTS_ZERO(l_t_compartments);
			MARKINGS_ZERO(l_t_markings);

			for (j = 0;
			    j <= l_hi_sensitivity_label->l_classification;
			    j++) {
				if (l_long_classification[j] != NULL) {
					/* if a valid classification */
					COMPARTMENTS_COMBINE(l_t_compartments,
					    l_in_compartments[j]);
					MARKINGS_COMBINE(l_t_markings,
					    l_in_markings[j]);
				}
			}
			COMPARTMENTS_COMBINE(l_t_compartments,
			    l_iv_compartments);
			COMPARTMENTS_XOR(l_t_compartments, l_iv_compartments);

			MARKINGS_COMBINE(l_t_markings, l_iv_markings);
			MARKINGS_XOR(l_t_markings, l_iv_markings);

			/*
			 * Once the masks of default bits are computed
			 * (l_t_compartments and l_t_markings), check this
			 * word to see if it is default.
			 */

			if (COMPARTMENTS_ANY_BITS_MATCH(l_t_compartments,
			    l_tables->l_words[i].l_w_cm_value) ||
			    MARKINGS_ANY_BITS_MATCH(l_t_markings,
			    l_tables->l_words[i].l_w_mk_value)) {
				(void) printf(gettext("Default"));
			} else if (l_tables->l_words[i].l_w_type &
			    SPECIAL_INVERSE) {
				(void) printf(gettext("Special Inverse"));
			} else if (!COMPARTMENTS_EQUAL(l_tables->l_words[i
			    ].l_w_cm_mask, l_tables->l_words[i].l_w_cm_value) ||
			    !MARKINGS_EQUAL(l_tables->l_words[i].l_w_mk_mask,
			    l_tables->l_words[i].l_w_mk_value)) {
				(void) printf(gettext("Regular Inverse"));
			} else {
				(void) printf(gettext("Normal"));
			}

			/* Produce (optional) Access related portion of Type: */

			if (l_tables->l_words[i].l_w_flags & ACCESS_RELATED) {
				(void) printf(gettext("; Access related"));
			}

			/*
			 * Produce (optional) Not visible below
			 * classification ZZ portion of Type:
			 */

			if (l_tables->l_words[i].l_w_output_min_class >
			    *l_min_classification) {
				/* if ominclass specified */
				(void) printf(gettext("\n         Not "
				    "visible below classification: %s"),
				    l_short_classification[
				    l_tables->l_words[i].l_w_output_min_class]);
			}

			/*
			 * Produce (optional) Not visible above
			 * classification ZZ portion of Type:
			 */

			if (l_tables->l_words[i].l_w_output_max_class <
			    l_hi_sensitivity_label->l_classification + 1) {
				/* if omaxclass specified */
				(void) printf(gettext("\n         Not "
				    "visible above classification: %s"),
				    l_short_classification[
				    l_tables->l_words[i].l_w_output_max_class]);
			}
			/*
			 * Produce newline ending Type:,
			 * and listing of words hierarchically above this one.
			 */

			(void) printf(gettext("\n   Words "
			    "hierarchically above:"));

			/* init flag saying we haven't found a word above */
			got_one = B_FALSE;

			for (j = l_tables->l_first_main_entry;
			    j < l_tables->l_num_entries; j++) {
				if (j != i &&
				    l_tables->l_words[j].l_w_exact_alias ==
				    NO_EXACT_ALIAS &&
				    COMPARTMENT_MASK_DOMINATE(l_tables->
				    l_words[j].l_w_cm_mask,
				    l_tables->l_words[i].l_w_cm_mask) &&
				    COMPARTMENTS_DOMINATE(l_tables->
				    l_words[j].l_w_cm_value,
				    l_tables->l_words[i].l_w_cm_value) &&
				    MARKING_MASK_DOMINATE(l_tables->
				    l_words[j].l_w_mk_mask,
				    l_tables->l_words[i].l_w_mk_mask) &&
				    MARKINGS_DOMINATE(l_tables->
				    l_words[j].l_w_mk_value,
				    l_tables->l_words[i].l_w_mk_value)) {
					got_one = B_TRUE;
					(void) printf("\n      ");
					/* Produce longname (shortname) */
					printf_word_names(l_tables, j);
				}
			}
			if (!got_one) {
				(void) printf(gettext(" NONE"));
			}

			/*
			 * Produce listing of words hierarchically
			 * below this one.
			 */
			(void) printf(gettext("\n   Words "
			    "hierarchically below:"));

			/* init flag saying we haven't found a word below */
			got_one = B_FALSE;

			for (j = l_tables->l_first_main_entry;
			    j < l_tables->l_num_entries; j++) {
				if (j != i &&
				    l_tables->l_words[j].l_w_exact_alias ==
				    NO_EXACT_ALIAS &&
				    COMPARTMENT_MASK_DOMINATE(l_tables->
				    l_words[i].l_w_cm_mask,
				    l_tables->l_words[j].l_w_cm_mask) &&
				    COMPARTMENTS_DOMINATE(l_tables->
				    l_words[i].l_w_cm_value,
				    l_tables->l_words[j].l_w_cm_value) &&
				    MARKING_MASK_DOMINATE(l_tables->
				    l_words[i].l_w_mk_mask,
				    l_tables->l_words[j].l_w_mk_mask) &&
				    MARKINGS_DOMINATE(l_tables->
				    l_words[i].l_w_mk_value,
				    l_tables->l_words[j].l_w_mk_value)) {
					got_one = B_TRUE;
					(void) printf("\n      ");
					/* Produce longname (shortname) */
					printf_word_names(l_tables, j);
				}
			}
			if (!got_one) {
				(void) printf(gettext(" NONE\n"));
			} else {
				(void) printf(gettext("\n"));
			}
		}
	}
}

static void
print_default(char *string, blevel_t *label, struct l_tables *table)
{

	if (string != NULL &&
	    l_convert(string, (CLASSIFICATION)LCLASS((_blevel_impl_t *)label),
	    l_short_classification,
	    (COMPARTMENTS *)&(((_blevel_impl_t *)(label))->_comps),
	    l_in_markings[(CLASSIFICATION)LCLASS((_blevel_impl_t *)label)],
	    table, NO_PARSE_TABLE, LONG_WORDS, ALL_ENTRIES, FALSE,
	    NO_INFORMATION_LABEL)) {
		(void) printf("\"%s\"\n", string);
	} else {
		char *hex;

		if (label_to_str((m_label_t *)label, &hex, M_INTERNAL,
		    DEF_NAMES) != 0) {
			perror("label_to_str");
			hex = strdup("bad_label");
		}

		(void) printf("\"%s\"\n", hex);
		free(hex);
	}
}

static void
print_color_word(struct l_tables *t, cwe_t *word, char *string)
{
	int i;
	COMPARTMENTS color_comps;

	if (string != NULL) {

		for (i = t->l_first_main_entry; i < t->l_num_entries; i++) {
			COMPARTMENTS_ZERO(&color_comps);
			if (t->l_words[i].l_w_type & SPECIAL_INVERSE) {

				short prefix = t->l_words[i].l_w_prefix;
				COMPARTMENTS_SET(&color_comps,
				    t->l_words[prefix].l_w_cm_mask,
				    t->l_words[prefix].l_w_cm_value);
			}
			COMPARTMENTS_SET(&color_comps,
			    t->l_words[i].l_w_cm_mask,
			    t->l_words[i].l_w_cm_value);
			COMPARTMENTS_AND(&color_comps, &word->mask);
			if (COMPARTMENTS_EQUAL(&color_comps, &word->comps)) {
				(void) printf(gettext("\tWord: "));
				printf_word(t, i);
				(void) printf(" = \"%s\"\n", word->color);
			}
		}
	} else {

		(void) printf(gettext("\tCompartment Bits:"));
		printf_compartment_bits(&word->comps);
		(void) printf(" = \"%s\"\n", word->color);
	}
}

static void
print_color_entry(cte_t *entry, char *string)
{

	if (string != NULL &&
	    l_convert(string,
	    (CLASSIFICATION) LCLASS((_blevel_impl_t *)&(entry->level)),
	    l_short_classification,
	    (COMPARTMENTS *)&(((_blevel_impl_t *)&(entry->level))->_comps),
	    l_in_markings[
	    (CLASSIFICATION)LCLASS((_blevel_impl_t *)&(entry->level))],
	    l_sensitivity_label_tables,
	    NO_PARSE_TABLE, LONG_WORDS, ALL_ENTRIES, FALSE,
	    NO_INFORMATION_LABEL)) {
		(void) printf("\t%s = \"%s\"\n", string, entry->color);
	} else {
		char *hex;

		if (label_to_str((m_label_t *)&(entry->level), &hex,
		    M_INTERNAL, DEF_NAMES) != 0) {
			perror("label_to_str");
			hex = strdup("bad_label");
		}
		(void) printf("\t%s = \"%s\"\n", hex, entry->color);
		free(hex);
	}
}

static int
lastbit(COMPARTMENTS *comps_ptr)
{
	int	i;
	int	last_bit;

	if ((last_bit = COMPARTMENT_BIT_ON(comps_ptr, FIRST)) != -1) {
		while ((i = COMPARTMENT_BIT_ON(comps_ptr, NEXT)) != -1)  {
			last_bit = i;
		}
	}
	return (last_bit);
}

static void
cipso_check()
{
	int	i;

	if (l_information_label_tables == NULL) {
		/* errors didn't let us get this far */
		return;
	}
	if (l_hi_sensitivity_label->l_classification > CIPSO_CLASS_MAX) {
		(void) printf(gettext("\n\n Warning Maximum Classification %d "
		    "greater then CIPSO Maximum %d\n\n"),
		    l_hi_sensitivity_label->l_classification,
		    CIPSO_CLASS_MAX);
	}

	if (l_sensitivity_label_tables == NULL) {
		/* errors didn't let us get this far */
		return;
	}

	/* Build Normal Compartment Bits */

	/*
	 * Scan the word table, accumulating a mask of all defined bits
	 * in l_t_compartments. Then remove the regular inverse bits from
	 * this mask.
	 */

	COMPARTMENTS_ZERO(l_t_compartments);
	for (i = l_sensitivity_label_tables->l_first_main_entry;
	    i < l_sensitivity_label_tables->l_num_entries; i++) {
		if (l_sensitivity_label_tables->l_words[i].l_w_prefix !=
		    L_IS_PREFIX &&
		    l_sensitivity_label_tables->l_words[i].l_w_suffix !=
		    L_IS_SUFFIX &&
		    l_sensitivity_label_tables->l_words[i].l_w_exact_alias ==
		    NO_EXACT_ALIAS) {
			COMPARTMENTS_COMBINE(l_t_compartments,
			    l_sensitivity_label_tables->l_words[i].l_w_cm_mask);
		}
	}
	COMPARTMENTS_XOR(l_t_compartments, l_iv_compartments);
	if ((i = lastbit(l_t_compartments)) > CIPSO_COMPS_MAX) {
		(void) printf(gettext("\n\n Warning Maximum Normal "
		    "Compartment %d greater then CIPSO Maximum %d\n\n"),
		    i, CIPSO_COMPS_MAX);
	}

	/*
	 * Check bits reserved but not defined.  These are from the
	 * system high compartment bits, removing the defined bits
	 * checked above. l_t2_ is used to accumulate the bits,
	 * because l_t_ currently has the normal defined bits.
	 * However, before finding the last bit, l_t2_ is copied
	 * to l_t, because l_t2_ is use in finding the last bit.
	 */

	COMPARTMENTS_COPY(l_t2_compartments,
	    l_hi_sensitivity_label->l_compartments);
	COMPARTMENTS_XOR(l_t2_compartments, l_t_compartments);
	COMPARTMENTS_XOR(l_t2_compartments, l_iv_compartments);
	COMPARTMENTS_COPY(l_t_compartments, l_t2_compartments);

	if ((i = lastbit(l_t_compartments)) > CIPSO_COMPS_MAX) {
		(void) printf(gettext("\n\n Warning Maximum Reserved "
		    "Compartment %d greater then CIPSO Maximum %d\n\n"),
		    i, CIPSO_COMPS_MAX);
	}

	if ((i = lastbit(l_iv_compartments)) > CIPSO_COMPS_MAX) {
		(void) printf(gettext("\n\n Warning Maximum Inverse "
		    "Compartment %d greater then CIPSO Maximum %d\n\n"),
		    i, CIPSO_COMPS_MAX);
	}
}

static void
printf_in_comps(COMPARTMENTS *comps)
{
	if (comps != NULL) {
		(void) printf(gettext("\n\tInitial Compartment bits:"));
		printf_compartment_bits(comps);
	}
}

static void
printf_in_marks(MARKINGS *marks)
{
	if (marks != NULL) {
		(void) printf(gettext("\n\tInitial Markings bits:"));
		printf_marking_bits(marks);
	}
}

int
l_encodings_initialized(void)
{
	return (TRUE);
}

/*
 * The main program.  Call l_init.  Then, whether or not successful, produce
 * info about defined classifications and word tables with non-zero number of
 * entries.
 */

int
main(int argc, char *argv[])
{
	int	i;		/* index into classification tables */
	char	encodings_file[MAXPATHLEN];
	int	aflag = 0;	/* analyze flag != 0 for full analysis */
	int	Max_class = MAX_CLASS;
	int	Xflag = 0;	/* private highest MAC label in hex */
	int	opt;
	int	err_return;	/* return status, used with -a */

	(void) setlocale(LC_ALL, "");
	(void) snprintf(encodings_file, MAXPATHLEN, "%s%s", ENCODINGS_PATH,
	    ENCODINGS_NAME);
#if !defined(TEXT_DOMAIN)		/* Should be defined by cc -D */
#define	TEXT_DOMAIN	"SYS_TEST"	/* Use this only if it weren't */
#endif
	(void) textdomain(TEXT_DOMAIN);

	opterr = 0;
	while ((opt = getopt(argc, argv, ":Xac:")) != -1) {
		switch (opt) {

		case 'a':	/* set full analyze mode */
			aflag++;
			break;

		case 'c':	/* set maximum classification value */
			Max_class = strtol(optarg, NULL, 10);
			if (Max_class == 0) {
				(void) fprintf(stderr,
				    gettext("%s: -c invalid parameter\n"),
				    argv[0]);
				exit(2);
			}
			break;

		case 'X':	/* private highest MAC label in hex */
			Xflag++;
			break;

		case '?':
			(void) fprintf(stderr,
			    gettext("Usage: %s [-a] [-c max class] "
			    "[label_encodings]\n"), argv[0]);
			exit(2);
		}
	}
	if (optind < argc) {
		(void) strlcpy(encodings_file, argv[optind],
		    sizeof (encodings_file));
	}

	if (l_init(encodings_file, Max_class, MAX_COMPS, MAX_MARKS) != 0) {
		(void) fprintf(stderr, gettext("%s: label encodings syntax "
		    "check failed.\n"), encodings_file);
		cipso_check();

		if (!aflag)
			exit(1);
		err_return = 1;
	} else {
		if (!Xflag) {
			(void) printf(gettext("No errors found in %s.\n\n"),
			    encodings_file);
			cipso_check();
		} else {
			m_label_t *l = m_label_alloc(MAC_LABEL);
			char *hex = NULL;

			_MSETTYPE(l, SUN_MAC_ID);
			DSL(l);
			if (label_to_str(l, &hex, M_INTERNAL, 0) != 0) {
				(void) fprintf(stderr, "X flag failed\n");
			}
			(void) printf("%s\n", hex);
			m_label_free(l);
			exit(0);
		}

		if (!aflag)
			exit(0);
		err_return = 0;
	}

	/* Produce output of the version. */

	if (l_version != NULL)
		(void) printf("\n---> VERSION = %s <---\n", l_version);

	/* Produce output of classifications. */

	if (l_long_classification != NULL) {
		(void) printf("\n---> CLASSIFICATIONS <---\n");

		/*
		 * Look for defined classifications, and output
		 * all three names.  On separate lines after each name,
		 * produce the IL of any name that was specified in the
		 * form: IL(name) = <information label>
		 */

		for (i = 0; i <= l_hi_sensitivity_label->l_classification;
		    i++) {
			if (l_long_classification[i] != NULL) {
				/* if a valid classification */
				(void) printf(gettext("\nClassification %d: "
				    "%s (%s)"), i, l_long_classification[i],
				    l_short_classification[i]);
				if (l_alternate_classification[i] != NULL) {
					(void) printf(" / %s",
					    l_alternate_classification[i]);
				}
				printf_in_comps(l_in_compartments[i]);
				printf_in_marks(l_in_markings[i]);
				printf_name_IL(l_long_classification[i],
				    l_lc_name_information_label[i]);
				printf_name_IL(l_short_classification[i],
				    l_sc_name_information_label[i]);
				printf_name_IL(l_alternate_classification[i],
				    l_ac_name_information_label[i]);
			}
		}
		(void) printf("\n");

		/*
		 * If l_information_label_tables tables is initialized,
		 * produce an analysis of the compartment and marking bits,
		 * and then a view of of the information label words.
		 */

		if (l_information_label_tables != NULL) {
			(void) printf(gettext("\n---> COMPARTMENTS "
			    "AND MARKINGS USAGE ANALYSIS <---\n"));

			/*
			 * Scan the word table, accumulating a mask of
			 * all defined bits in l_t_compartments and
			 * l_t_markings. Then remove the regular inverse
			 * bits from this mask.
			 */

			COMPARTMENTS_ZERO(l_t_compartments);
			MARKINGS_ZERO(l_t_markings);

			for (i = l_information_label_tables->l_first_main_entry;
			    i < l_information_label_tables->l_num_entries;
			    i++) {
				if (l_information_label_tables->l_words[
				    i].l_w_prefix != L_IS_PREFIX &&
				    l_information_label_tables->l_words[
				    i].l_w_suffix != L_IS_SUFFIX &&
				    l_information_label_tables->l_words[
				    i].l_w_exact_alias == NO_EXACT_ALIAS) {
					COMPARTMENTS_COMBINE(l_t_compartments,
					    l_information_label_tables->
					    l_words[i].l_w_cm_mask);
					MARKINGS_COMBINE(l_t_markings,
					    l_information_label_tables->
					    l_words[i].l_w_mk_mask);
				}
			}
			COMPARTMENTS_XOR(l_t_compartments, l_iv_compartments);
			MARKINGS_XOR(l_t_markings, l_iv_markings);

			(void) printf(gettext("\nNormal compartment bits "
			    "defined:"));
			printf_compartment_bits(l_t_compartments);

			(void) printf(gettext("\nRegular inverse compartment "
			    "bits defined:"));
			printf_compartment_bits(l_iv_compartments);

			/*
			 * Printf bits reserved but not defined.  These
			 * are gotten from the system high compartment
			 * and marking bits, removing the defined bits
			 * printed above. l_t2_ will be used to accumulate
			 * the bits, because l_t_ currently has the normal
			 * defined bits.
			 * However, before printing, l_t2_ is copied to
			 * l_t_, because l_t2_ is used to print.
			 */

			COMPARTMENTS_COPY(l_t2_compartments,
			    l_hi_sensitivity_label->l_compartments);
			COMPARTMENTS_XOR(l_t2_compartments, l_t_compartments);
			COMPARTMENTS_XOR(l_t2_compartments, l_iv_compartments);
			COMPARTMENTS_COPY(l_t_compartments, l_t2_compartments);

			(void) printf(gettext("\nCompartment bits reserved "
			    "as 1 but not defined:"));
			printf_compartment_bits(l_t_compartments);

			/*
			 * Now printf normal, inverse, and reserved markings
			 * bits.
			 */

			(void) printf(gettext("\n\nNormal marking bits "
			    "defined:"));
			printf_marking_bits(l_t_markings);

			(void) printf(gettext("\nRegular inverse marking "
			    "bits defined:"));
			printf_marking_bits(l_iv_markings);

			/*
			 * Printf bits reserved but not defined.  These
			 * are gotten from the system high compartment
			 * and marking bits, removing the defined bits
			 * printed above. l_t2_ will be used to accumulate
			 * the bits, because l_t_ currently has the normal
			 * defined bits.
			 * However, before printing, l_t2_ is copied to
			 * l_t_, because l_t2_ is used to print.
			 */

			MARKINGS_COPY(l_t2_markings, l_hi_markings);
			MARKINGS_XOR(l_t2_markings, l_t_markings);
			MARKINGS_XOR(l_t2_markings, l_iv_markings);
			MARKINGS_COPY(l_t_markings, l_t2_markings);

			(void) printf(gettext("\nMarking bits reserved "
			    "as 1 but not defined:"));
			printf_marking_bits(l_t_markings);
			(void) printf("\n");

			/* Show the information label words. */

			view_words("INFORMATION LABEL",
			    l_information_label_tables);

			/*
			 * If l_sensitivity_label_tables tables is
			 * initialized, produce view of it.
			 */

			if (l_sensitivity_label_tables != NULL) {
				view_words("SENSITIVITY LABEL",
				    l_sensitivity_label_tables);

				/*
				 * If l_clearance_tables tables is
				 * initialized, produce view of it.
				 */
				if (l_clearance_tables != NULL) {
					view_words("CLEARANCE",
					    l_clearance_tables);
				}

				/*
				 * If l_channel_tables tables is
				 * initialized, produce view of it.
				 */
				if (l_channel_tables != NULL) {
					view_words("CHANNEL",
					    l_channel_tables);
				}
				/*
				 * If l_printer_banner_tables tables is
				 * initialized, produce view of it.
				 */

				if (l_printer_banner_tables != NULL) {
					view_words("PRINTER BANNER",
					    l_printer_banner_tables);
				}
			}
		}
	}
	(void) printf("\n---> LOCAL DEFINITIONS <---\n");

#ifdef	OBSOLETE
	(void) printf(gettext("\nLabel Translation Options:\n"));

	(void) printf(gettext("\tAdmin Low Label Name is \"%s\"\n"),
	    Admin_low);
	(void) printf(gettext("\tAdmin High Label Name is \"%s\"\n"),
	    Admin_high);

	if (view_label) {
		(void) printf(gettext("\tDefault Label View is External\n"));
	} else {
		(void) printf(gettext("\tDefault Label View is Internal\n"));
	}

	(void) printf(gettext("\tDefault Word Selection Flags = 0x%x\n"),
	    default_flags);
	(void) printf(gettext("\tForced Word Selection Flags = 0x%x\n"),
	    forced_flags);

	if (Class_name == NULL) {
		(void) printf(gettext("\tClassification Field Name is "
		    "\"(nil)\"\n"));
	} else {
		(void) printf(gettext("\tClassification Field Name is "
		    "\"%s\"\n"), Class_name);
	}
	if (Comps_name == NULL) {
		(void) printf(gettext("\tCompartments Field Name is "
		    "\"(nil)\"\n"));
	} else {
		(void) printf(gettext("\tCompartments Field Name is "
		    "\"%s\"\n"), Comps_name);
	}
#else	/* !OBSOLETE */

	if (Class_name == NULL) {
		(void) printf(gettext("\nClassification Field Name is "
		    "\"(nil)\"\n"));
	} else {
		(void) printf(gettext("\nClassification Field Name is "
		    "\"%s\"\n"), Class_name);
	}
	if (Comps_name == NULL) {
		(void) printf(gettext("Compartments Field Name is "
		    "\"(nil)\"\n"));
	} else {
		(void) printf(gettext("Compartments Field Name is "
		    "\"%s\"\n"), Comps_name);
	}
#endif	/* OBSOLETE */

	if (!bltype(&def_user_clear, SUN_CLR_ID)) {
		(void) printf(gettext("\nDefault User Clearance not "
		    "Defined.\n"));
	} else {
		char	*string = NULL;

		if (l_clearance_tables != NULL) {
			string = calloc(l_clearance_tables->l_max_length, 1);
		}
		(void) printf(gettext("\nDefault User Clearance = "));
		print_default(string, &def_user_clear, l_clearance_tables);
	}
	if (!bltype(&def_user_sl, SUN_SL_ID)) {
		(void) printf(gettext("\nDefault User Sensitivity Label not "
		    "Defined.\n"));
	} else {
		char	*string = NULL;

		if (l_sensitivity_label_tables != NULL) {
			string = calloc(l_sensitivity_label_tables->
			    l_max_length, 1);
		}
		(void) printf(gettext("\nDefault User Sensitivity Label = "));
		print_default(string, &def_user_sl, l_sensitivity_label_tables);
	}

	if (color_table == NULL) {
		(void) printf(gettext("\nNo Label to Color Name Mapping "
		    "Defined.\n"));
	} else {
		cwe_t	*word;	/* color word entry */
		int	i;	/* color table index */
		cte_t	*entry;	/* color table entry */
		char	*string = NULL;	/* label string */

		if (l_sensitivity_label_tables != NULL) {
			string = calloc(l_sensitivity_label_tables->
			    l_max_length, 1);
		}
		if ((word = color_word) != NULL) {
			(void) printf(gettext("\n---> WORD to COLOR MAPPING "
			    "<---\n\n"));
			do {
				print_color_word(l_sensitivity_label_tables,
				    word, string);
			} while ((word = word->next) != (cwe_t *)0);
		}
		(void) printf(gettext("\n---> SENSITIVITY LABEL to COLOR "
		    "MAPPING <---\n\n"));
		if (low_color != NULL) {
			(void) printf("\t%s = \"%s\"\n", Admin_low, low_color);
		}
		for (i = 0; i <= l_hi_sensitivity_label->l_classification;
		    i++) {
			/* walk the color table */
			if ((entry = color_table[i]) == NULL)
				continue;

			print_color_entry(entry, string);

			while ((entry = entry->next) != NULL) {
				print_color_entry(entry, string);
			}
		}

		if (high_color != NULL) {
			(void) printf("\t%s = \"%s\"\n", Admin_high,
			    high_color);
		}
	}
	return (err_return);
}
