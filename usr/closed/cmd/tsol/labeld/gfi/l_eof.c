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
 *	l_eof.c - Trusted Solaris label encodings file end of file processing.
 *
 *		This routine serves to process the Trusted Solaris local
 *	extensions to the label encodings file.
 */

#include <malloc.h>
#include <tsol/label.h>

/* avoid conflicts with std_labels.h */

#ifdef	_TSOL_LABEL_H
#undef	ACCESS_RELATED
#undef	ALL_ENTRIES
#undef	LONG_WORDS
#undef	NO_CLASSIFICATION
#undef	SHORT_WORDS
#endif	/* TSOL_LABEL_H */

#include "std_labels.h"

#include "impl.h"		/* must follow std_labels.h to get cwe_t */

/* parametric values for translation */

int	view_label = TRUE;		/* TRUE, view external labels	*/
					/* FALSE, view internal labels	*/

unsigned short	default_flags = 0;	/* default translation flags */
unsigned short	forced_flags  = 0;	/* forced translation flags */

/* label to color name mapping */

cwe_t	*color_word = NULL;		/* list of color words		*/
cte_t	**color_table = NULL;		/* array of color names		*/
					/* indexed by Classification	*/
char	*low_color = NULL;		/* color name for Admin Low */
char	*high_color = NULL;		/* color name for Admin High */

/* label field names */

char	*Class_name = NULL;		/* name of the classification field */
char	*Comps_name = NULL;		/* name of the compartments field */
char	*Marks_name = NULL;		/* name of the markings field */

/* default user label range */

bslabel_t	def_user_sl;		/* default user SL */
bclear_t	def_user_clear;		/* default user Clearance */


/* Label encodings extension keywords */

#ifdef	LIST_END
#undef	LIST_END
#endif	/* LIST_END */
#define	LIST_END NULL

static char	*local_definitions[] =
{
	"LOCAL DEFINITIONS:",
	LIST_END
};

static char	*type[] =
{
	"ADMIN LOW NAME=",
	"ADMIN HIGH NAME=",
	"DEFAULT LABEL VIEW IS EXTERNAL",
	"DEFAULT LABEL VIEW IS INTERNAL",
	"DEFAULT FLAGS=",
	"FORCED FLAGS=",
	"FLOAT PROCESS INFORMATION LABEL",
	"DO NOT FLOAT PROCESS INFORMATION LABEL",
	"BOUND TRANSLATION BY CLEARANCE",
	"BOUND TRANSLATION BY SENSITIVITY LABEL",
	"CLASSIFICATION NAME=",
	"COMPARTMENTS NAME=",
	"MARKINGS NAME=",
	"DEFAULT USER SENSITIVITY LABEL=",
	"DEFAULT USER CLEARANCE=",
	LIST_END
};

#define	ADMIN_LOW_NAME		0
#define	ADMIN_HIGH_NAME		1
#define	EXTERNAL_DEFAULT_VIEW	2
#define	INTERNAL_DEFAULT_VIEW	3
#define	TYPE_DEFAULT_FLAGS	4
#define	TYPE_FORCED_FLAGS	5
#define	DO_FLOAT_PIL		6
#define	DON_T_FLOAT_PIL		7
#define	BOUND_CLEARANCE		8
#define	BOUND_SENSITIVITY	9
#define	CLASSIFICATION_NAME	10
#define	COMPARTMENTS_NAME	11
#define	MARKINGS_NAME		12
#define	DEF_USER_SL		13
#define	DEF_USER_CLEARANCE	14

static char	*color_names[] =
{
	"COLOR NAMES:",
	LIST_END
};

#define	COLOR_NAMES	0

static char	*color_values[] =
{
	"LABEL=",
	"COLOR=",
	"WORD=",
	LIST_END
};

#define	COLOR_LABEL	0
#define	COLOR_COLOR	1
#define	COLOR_WORD	2


/*
 *	initialize_defaults - Initialize default values required.
 *
 *	Entry	None
 *
 *	Exit	Fields requiring initialization initialized.
 *
 *	Returns	None.
 */

static void
initialize_defaults(void)
{
	/* Initialize sizes of default Admin Low and High names */

	if (Admin_low_size == 0) {
		Admin_low_size = strlen(Admin_low);
	}
	if (Admin_high_size == 0) {
		Admin_high_size = strlen(Admin_high);
	}

	/* Initialize default field names */

	if (Class_name == NULL) {
		Class_name = CLASS_NAME;
	}
	if (Comps_name == NULL) {
		Comps_name = COMPS_NAME;
	}
	if (Marks_name == NULL) {
		Marks_name = MARKS_NAME;
	}

	/* Initialize default user label range */

	if (!BLTYPE(&def_user_sl, SUN_SL_ID)) {
		/* initialize SL type */
		SETBLTYPE(&def_user_sl, SUN_SL_ID);
		LCLASS_SET((_bslabel_impl_t *)&def_user_sl,
		    l_lo_sensitivity_label->l_classification);
		((_bslabel_impl_t *)&def_user_sl)->_comps =
		    *(Compartments_t *)l_lo_sensitivity_label->
		    l_compartments;
	}
	if (!BLTYPE(&def_user_clear, SUN_CLR_ID)) {
		/* initialize clearance type */
		SETBLTYPE(&def_user_clear, SUN_CLR_ID);
		LCLASS_SET((_bclear_impl_t *)&def_user_clear,
		    l_lo_clearance->l_classification);
		((_bclear_impl_t *)&def_user_clear)->_comps =
		    *(Compartments_t *)l_lo_clearance->l_compartments;
	}
}


/*
 *	l_eof - Process end of encodings file.
 *
 *	Entry	counting = TRUE, if counting pass.
 *			   False, is parsing pass.
 *
 *	Exit	view_label = TRUE, if default view labels as EXTERNAL,
 *			     FALSE, if default view labels as INTERNAL.
 *		default_flags = integer value of default translation GFI flags.
 *		forced_flags = integer value of forced translation GFI flags.
 *		color_table = specified word and color values.
 *		Class_name = name to use for the classification field.
 *		Comps_name = name to use for the compartment words field.
 *		def_user_sl = default Sensitivity Label for users.
 *		def_user_clear = default Clearance for users.
 *
 *	Returns	TRUE, if counting, no "LOCAL DEFINITIONS:" section found, or
 *			"LOCAL DEFINITIONS:" section correctly parsed.
 *		FALSE, if "LOCAL DEFINITIONS:" section in error, or EOF
 *			doesn't follow "LOCAL DEFINITONS:" section.
 *
 *	Uses	color_table, l_dp.
 *
 *	Calls	IS_ADMIN_HIGH, IS_ADMIN_LOW, calloc, l_convert, l_error,
 *			l_next_keyword, l_parse, malloc, strcmp, strcpy,
 *			strlen, strtol.
 */

int
l_eof(const int counting, const unsigned int *line_number)
{
	int key;

	if (counting)
		return (TRUE);

	/*
	 * [
	 * LOCAL DEFINITIONS:
	 *
	 * [ADMIN LOW NAME= <string>;]
	 *
	 * [ADMIN HIGH STRING= <string>;]
	 *
	 * [DEFAULT LABEL VIEW IS EXTERNAL; | INTERNAL;]
	 *
	 * [DEFAULT FLAGS= <value>;]
	 *
	 * [FORCED FLAGES= <value>;]
	 *
	 * [[DO NOT] FLOAT PROCESS INFORMATION LABEL;] ignored;
	 *
	 * [BOUND TRANSLATION BY SENSITIVITY LABEL; | CLEARANCE;] ignored;
	 *
	 * [CLASSIFICATION NAME= <string>;]
	 *
	 * [COMPARTMENTS NAME= <string>;]
	 *
	 * [MARKINGS NAME= <string>;] ignored;
	 *
	 * [DEFAULT USER SENSITIVITY LABEL= <sensitivity label>;]
	 *
	 * [DEFAULT USER CLEARANCE= <clearance>;]
	 *
	 */

	if ((key = l_next_keyword(local_definitions)) == -2) {

		/* EOF found, no local extensions */
		initialize_defaults();
		return (TRUE);
	} else if (key == -1) {

		/* EOF not found, but LOCAL DEFINITIONS: also not found */
		l_error(*line_number, "End of File or LOCAL DEFINITIONS:"
		    " not found.\n Found instead: \"%s\".\n",
		    l_scan_ptr);
		return (FALSE);
	}

	while ((key = l_next_keyword(type)) >= 0) {

	    switch (key) {

	    case ADMIN_LOW_NAME:
		if (Admin_low_size != 0) {
			l_error(*line_number, "Duplicate %s ignored.\n",
			    type[key]);
			break;
		}

		Admin_low_size = strlen(l_dp);
		if ((Admin_low = malloc((unsigned)Admin_low_size + 1)) ==
		    NULL) {
			l_error(*line_number, "Can't allocate %ld"
			    " bytes for %s\n", Admin_low_size, type[key]);
			return (FALSE);
		}

		(void) strcpy(Admin_low, l_dp);
		break;

	    case ADMIN_HIGH_NAME:
		if (Admin_high_size != 0) {
			l_error(*line_number, "Duplicate %s ignored.\n",
			    type[key]);
			break;
		}

		Admin_high_size = strlen(l_dp);
		if ((Admin_high = malloc((unsigned)Admin_high_size + 1)) ==
		    NULL) {
			l_error(*line_number, "Can't allocate %ld"
						" bytes for %s\n",
						Admin_high_size,
						type[key]);
			return (FALSE);
		}

		(void) strcpy(Admin_high, l_dp);
		break;

	    case EXTERNAL_DEFAULT_VIEW:
	    case INTERNAL_DEFAULT_VIEW:
		view_label = key == EXTERNAL_DEFAULT_VIEW;
		break;

	    case TYPE_DEFAULT_FLAGS:
		default_flags = (unsigned short) strtol(l_dp, NULL, 0);
		break;

	    case TYPE_FORCED_FLAGS:
		forced_flags = (unsigned short) strtol(l_dp, NULL, 0);
		break;

	    case DO_FLOAT_PIL:
	    case DON_T_FLOAT_PIL:
		l_error(*line_number, "%s is obsolete; ignored.\n",
		    type[key]);
		break;

	    case CLASSIFICATION_NAME:
		if (Class_name != NULL) {
			l_error(*line_number, "Duplicate %s ignored.\n",
						type[key]);
			break;
		}

		if ((Class_name = malloc(strlen(l_dp) + 1)) == NULL) {
			l_error(*line_number, "Can't allocate %ld bytes "
			    "for %s\n", strlen(l_dp) + 1, type[key]);
			return (FALSE);
		}

		(void) strcpy(Class_name, l_dp);
		break;

	    case COMPARTMENTS_NAME:
		if (Comps_name != NULL) {
			l_error(*line_number, "Duplicate %s ignored.\n",
			    type[key]);
			break;
		}

		if ((Comps_name = malloc(strlen(l_dp) + 1)) == NULL) {
			l_error(*line_number, "Can't allocate %ld bytes "
			    "for %s\n", strlen(l_dp) + 1, type[key]);
			return (FALSE);
		}

		(void) strcpy(Comps_name, l_dp);
		break;

	    case MARKINGS_NAME:
		l_error(*line_number, "%s is obsolete; ignored.\n",
		    type[key]);
		break;

	case DEF_USER_SL: {
		CLASSIFICATION	sl_class;
		COMPARTMENTS	sl_comps;
		char		*convert_buffer;

		if (BLTYPE(&def_user_sl, SUN_SL_ID)) {
			l_error(*line_number, "Duplicate %s ignored.\n",
			    type[key]);
		}
		sl_class = FULL_PARSE;
		if (l_parse(l_dp,
		    &sl_class, &sl_comps,
		    l_t_markings,
		    l_sensitivity_label_tables,
		    l_lo_sensitivity_label->l_classification,
		    l_lo_sensitivity_label->l_compartments,
		    l_hi_sensitivity_label->l_classification,
		    l_hi_sensitivity_label->l_compartments) != L_GOOD_LABEL) {
			l_error(*line_number, "Invalid %s \"%s\".\n",
			    type[key], l_dp);
			return (FALSE);
		}

		if ((convert_buffer = (char *)calloc(
		    (unsigned)l_sensitivity_label_tables->l_max_length, 1)) ==
		    NULL) {
			l_error(*line_number, "Can't allocate %ld bytes "
			    "for %s\n",
			    l_sensitivity_label_tables->l_max_length,
			    type[key]);
			return (FALSE);
		}
		(void) l_convert(convert_buffer,
		    sl_class,
		    l_short_classification,
		    &sl_comps,
		    l_t_markings,
		    l_sensitivity_label_tables,
		    NO_PARSE_TABLE, LONG_WORDS, ALL_ENTRIES, FALSE,
		    NO_INFORMATION_LABEL);
		if (strcmp(convert_buffer, l_dp) != 0) {
			l_error(*line_number, "\"%s %s\" is not in canoncial "
			    "form.\nIs %s what is intended?\n", type[key],
			    l_dp, convert_buffer);
			return (FALSE);
		}
		(void) free(convert_buffer);

		SETBLTYPE(&def_user_sl, SUN_SL_ID);	/* initialize type */
		LCLASS_SET((_bslabel_impl_t *)&def_user_sl, sl_class);
		((_bslabel_impl_t *)&def_user_sl)->_comps =
		    *(Compartments_t *)&sl_comps;

		break;
	}

	case DEF_USER_CLEARANCE: {
		CLASSIFICATION	clear_class;
		COMPARTMENTS	clear_comps;
		char		*convert_buffer;

		if (BLTYPE(&def_user_clear, SUN_CLR_ID)) {
			l_error(*line_number, "Duplicate %s ignored.\n",
			    type[key]);
		}
		clear_class = FULL_PARSE;
		if (l_parse(l_dp,
		    &clear_class, &clear_comps,
		    l_t_markings,
		    l_clearance_tables,
		    l_lo_clearance->l_classification,
		    l_lo_clearance->l_compartments,
		    l_hi_sensitivity_label->l_classification,
		    l_hi_sensitivity_label->l_compartments) != L_GOOD_LABEL) {
			l_error(*line_number, "Invalid %s \"%s\".\n",
			    type[key], l_dp);
			return (FALSE);
		}

		if ((convert_buffer = (char *)calloc(
		    (unsigned)l_clearance_tables->l_max_length, 1)) ==
		    NULL) {
			l_error(*line_number, "Can't allocate %ld bytes "
			    "for %s\n",
			    l_sensitivity_label_tables->l_max_length,
			    type[key]);
			return (FALSE);
		}
		(void) l_convert(convert_buffer,
		    clear_class,
		    l_short_classification,
		    &clear_comps,
		    l_t_markings,
		    l_clearance_tables,
		    NO_PARSE_TABLE, LONG_WORDS, ALL_ENTRIES, FALSE,
		    NO_INFORMATION_LABEL);
		if (strcmp(convert_buffer, l_dp) != 0) {
			l_error(*line_number, "\"%s %s\" is not in canoncial "
			    "form.\nIs %s what is intended?\n", type[key],
			    l_dp, convert_buffer);
			return (FALSE);
		}
		(void) free(convert_buffer);
		SETBLTYPE(&def_user_clear, SUN_CLR_ID);	/* initialize type */
		LCLASS_SET((_bclear_impl_t *)&def_user_clear, clear_class);
		((_bclear_impl_t *)&def_user_clear)->_comps =
		    *(Compartments_t *)&clear_comps;

		break;
	}

	    case BOUND_SENSITIVITY:
	    case BOUND_CLEARANCE:
		l_error(*line_number, "%s obsolete,\n   Bound is always"
		    " a Sensitivity Label.\n", type[key]);
		break;

	    }  /* switch(key) */

	}  /* while ((key = l_next_keyword(type)) >= 0) */

	/* Initialize sizes and field names */

	initialize_defaults();

	/*
	 * [
	 * COLOR NAMES:
	 *
	 * LABEL= label; COLOR= ASCII or hex color name;
	 * WORD=  word;	 COLOR= ASCII or hex color name;
	 *
	 * ]
	 * ]
	 */

	if ((key = l_next_keyword(color_names)) == COLOR_NAMES) {
	    char		**this_name = NULL;
	    cte_t		*entry;
	    cwe_t		*word;
	    CLASSIFICATION	color_class;
	    COMPARTMENTS	color_comps;
	    short		i;
	    struct l_tables	*t;

	    if ((color_table = (cte_t **)calloc((unsigned)
		(l_hi_sensitivity_label->l_classification+1),
		sizeof (cte_t))) == NULL) {

		l_error(*line_number, "Can't allocate %ld bytes for color"
		    " names table.\n",
		    (l_hi_sensitivity_label->l_classification *
		    sizeof (cte_t *)));
		return (FALSE);
	    }

	    for (i = 0; i <= l_hi_sensitivity_label->l_classification; i++) {

		color_table[i] = NULL;
	    }

	    while ((key = l_next_keyword(color_values)) >= 0) {

		switch (key) {

		case COLOR_LABEL:
		    if (this_name != NULL) {

			l_error(*line_number, "Label preceding \"%s\" did not"
			    " have a color specification.\n", l_dp);
			return (FALSE);
		    }

		    if (IS_ADMIN_LOW(l_dp)) {

			if (low_color != NULL) {

			    l_error(*line_number, "Admin_Low color already"
				" assigned as \"%s\".\n", low_color);
			    return (FALSE);
			}

			this_name = &low_color;
		    } else if (IS_ADMIN_HIGH(l_dp)) {

			if (high_color != NULL) {

			    l_error(*line_number, "Admin_High color already"
				" assigned as \"%s\".\n", high_color);
			    return (FALSE);
			}

			this_name = &high_color;
		    } else {

			color_class = NO_LABEL;

			if (l_parse(l_dp, &color_class, &color_comps,
			    l_t_markings, l_sensitivity_label_tables,
			    *l_min_classification, l_0_compartments,
			    l_hi_sensitivity_label->l_classification,
			    l_hi_sensitivity_label->l_compartments)
			    != L_GOOD_LABEL) {

			    l_error(*line_number, "Invalid color label"
				" \"%s\".\n", l_dp);
			    return (FALSE);
			}

			entry = color_table[color_class];

			if (entry == NULL) {

			    if ((entry = (cte_t *)malloc(sizeof (cte_t))) ==
				NULL) {

				l_error(*line_number, "Can't allocate %ld"
				    " bytes for color table entry.\n",
				    sizeof (cte_t));
				return (FALSE);
			    }


			    color_table[color_class] = entry;
			} else {

			    while (entry->color != NULL &&
				entry->next != NULL) {

				entry = entry->next;
			    }

			    if (entry->color != NULL) {
				if ((entry->next =
				    (cte_t *)malloc(sizeof (cte_t))) == NULL) {

				    l_error(*line_number, "Can't allocate %ld"
					" bytes for color table entry.\n",
					sizeof (cte_t));
				    return (FALSE);
				}

				entry = entry->next;
			    }
			}
			SETBLTYPE(&entry->level, SUN_SL_ID);
			LCLASS_SET(&entry->level, color_class);
			entry->level._comps =
			    *(Compartments_t *)&color_comps;
			entry->next = NULL;
			entry->color = NULL;
			this_name = &entry->color;
		    }

		    break;

		case COLOR_WORD:

		    if (this_name != NULL) {

			l_error(*line_number, "Label preceding \"%s\" did not"
			    " have a color specification.\n", l_dp);
			return (FALSE);
		    }

		    /* find the word in the Sensitivity Label table */

		    t = l_sensitivity_label_tables;
		    for (i = t->l_first_main_entry; i < t->l_num_entries; i++) {

			if (((t->l_words[i].l_w_short_name != NULL) &&
			    (strcmp(l_dp,
			    t->l_words[i].l_w_short_name) == 0)) ||
			    ((t->l_words[i].l_w_long_name != NULL) &&
			    (strcmp(l_dp, t->l_words[i].l_w_long_name) == 0)))

			    goto found;
		    }

		    /* word not found */

		    l_error(*line_number, "Word \"%s\" not found as a valid"
			" Sensitivity Label word.\n", l_dp);
		    return (FALSE);

found:
		    if (color_word == NULL) {

			if ((color_word = (cwe_t *)malloc(sizeof (cwe_t))) ==
			    NULL) {

			    l_error(*line_number, "Can't allocate %ld"
				" bytes for color word entry.\n",
				sizeof (cwe_t));
				return (FALSE);
			}
			word = color_word;
		    } else {

			if ((word->next = (cwe_t *)malloc(sizeof (cwe_t))) ==
			    NULL) {

			    l_error(*line_number, "Can't allocate %ld"
				" bytes for color word entry.\n",
				sizeof (cwe_t));
				return (FALSE);
			}
			word = word->next;
		    }

		    COMPARTMENTS_ZERO(&word->comps);
		    COMPARTMENT_MASK_COPY(&word->mask,
			t->l_words[i].l_w_cm_mask);
		    if (t->l_words[i].l_w_type & SPECIAL_INVERSE) {
			short prefix = t->l_words[i].l_w_prefix;

			COMPARTMENTS_SET(&word->comps,
			    t->l_words[prefix].l_w_cm_mask,
			    t->l_words[prefix].l_w_cm_value);
			COMPARTMENT_MASK_COPY(&word->mask, &word->comps);
		    }
		    COMPARTMENTS_SET(&word->comps, t->l_words[i].l_w_cm_mask,
			t->l_words[i].l_w_cm_value);
		    word->next = NULL;
		    word->color = NULL;
		    this_name = &word->color;

		    break;

		case COLOR_COLOR:

		    if (this_name == NULL) {

			l_error(*line_number, "Found color \"%s\" without"
			    " associated label.\n", l_dp);
			return (FALSE);
		    }

		    if ((*this_name = malloc((unsigned)strlen(l_dp) + 1)) ==
			NULL) {

			l_error(*line_number, "Can't allocate %ld bytes"
			    " for color name \"%s\".\n", l_dp);
			return (FALSE);
		    }

		    (void) strcpy(*this_name, l_dp);
		    this_name = NULL;
		    break;
		}
	    }  /* while ((key = l_next_keyword(color_values)) >= 0) */

	}  /* if ((key = l_next_keyword(color_names)) == COLOR_NAMES) */

	if (key == -2)

		/* found EOF */
		return (TRUE);

	l_error(*line_number, "End of File not found where expected.\n"
	    "Found instead: \"%s\".\n", l_scan_ptr);
	return (FALSE);
}  /* l_eof */
