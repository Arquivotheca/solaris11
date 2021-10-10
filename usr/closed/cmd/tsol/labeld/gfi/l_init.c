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
static char    *l_ID = "CMW Labels Release 2.2.1; 11/24/93: l_init.c";
#endif	/* lint */

#include "std_labels.h"
#ifdef	TSOL
#include <wctype.h>
#include <widec.h>
#include <libintl.h>
#include <limits.h>
#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN	"SYS-TEST"
#endif
#endif	/* TSOL */

/*
 * The subroutine l_init initializes the label tables from the file passed as
 * an argument, converting the human-readable label tables in the file into
 * the internal table format. The encodings are stored in a single block of
 * allocated memory, and various pointers into this memory block is set
 * (those external variables defined in std_labels.h). The encodings file is
 * scanned twice, the first time to determine the proper size for the memory
 * block, and the second time to pull the information from the file, convert
 * it as necessary, and store it in the memory block.   The first pass is
 * called the counting pass, as indicated by the variable "counting" being
 * TRUE.
 * 
 * During the counting pass, the size of the memory block is determined.  During
 * the second pass, the space within the memory block is allocated.  The
 * memory block is managed in two portions: the "tables" space and the
 * "strings" space.  The "tables" space is used to store all data other than
 * character strings, i.e., all data that may have to be aligned on some kind
 * of boundary.  The "strings" space stores character strings and other data
 * stored as character strings (e.g., the internet protocol security option
 * protection authority flags).
 * 
 * During the counting pass, the size of the "tables" space is kept in the
 * variable "size_tables", which is managed by the macro TABLES_RESERVE, and
 * the size of the "strings" space is kept in the variable "size_strings",
 * which is directly manipulated as needed.
 * 
 * During the conversion (second) pass, space within the "tables" space is
 * allocated using the macro TABLES_ALLOCATE, and space within the "strings"
 * space is allocated by direct manipulation of the variable "strings".
 * 
 * The define L_ALIGNMENT in std_labels.h determines the maximum alignment
 * needed on this machine for pointers and longs (which are the largest
 * components of any allocated variables).  L_ALIGNMENT is used automatically
 * by the macros TABLES_RESERVE and TABLES_ALLOCATE, which are also defined
 * in std_labels.h.
 * 
 * Once allocation is completed, the memory block contains, in order:
 * 
 *	In "tables" space:
 * 
 *	    The actual storage pointed to directly by the external variables:
 * 
 * 		l_min_classification
 *		l_classification_protect_as
 *		l_lo_clearance
 *		l_lo_sensitivity_label
 *		l_hi_sensitivity_label
 *		l_information_label_tables
 *		l_sensitivity_label_tables
 *		l_clearance_tables
 *		l_channel_tables
 *		l_printer_banner_tables
 *		l_long_classification
 *		l_short_classification
 *		l_alternate_classification
 *		l_lc_name_information_label
 *		l_sc_name_information_label
 *		l_ac_name_information_label
 *		l_in_compartments
 *		l_in_markings
 *		l_accreditation_range
 * 
 * 	The information label word table (pointed to by
 *		l_information_label_tables)
 *	The information label required combinations table (pointed to by
 * 		l_information_label_tables)
 *	The information label combination constraints table (pointed to by
 *		l_information_label_tables)
 *	The information label combination constraints words (pointed to by
 *		the above table)
 * 
 * 	The sensitivity label word table (pointed to by
 *		l_sensitivity_label_tables)
 * 	The sensitivity label required combinations table (pointed to by
 * 		l_sensitivity_label_tables)
 *	The sensitivity label combination constraints table (pointed to by
 *		l_sensitivity_label_tables)
 *	The sensitivity label combination constraints words (pointed to by
 *		the above table)
 * 
 *	The clearance word table (pointed to by
 *		l_clearance_tables)
 *	The clearance required combinations table (pointed to by
 *		l_clearance_tables)
 *	The clearance combination constraints table (pointed to by
 *		l_clearance_tables)
 *	The clearance combination constraints words (pointed to by
 *		the above table)
 * 
 *	The channel word table (pointed to by
 *		l_channel_tables)
 * 
 *	The printer banner word table (pointed to by
 *		l_printer_banner_tables)
 * 
 *	The accreditation range specifications (pointed to by
 *		l_accreditation_range)
 * 
 *	The name information label specifications
 * 
 *	The input name specifications
 * 
 *	The COMPARTMENTS pointed to directly by the external variables:
 * 
 *		l_t_compartments
 *		l_t2_compartments
 *		l_t3_compartments
 *		l_t4_compartments
 *		l_t5_compartments
 *		l_0_compartments
 *		l_lo_clearance->l_compartments
 *		l_lo_sensitivity_label->l_compartments
 *		l_hi_sensitivity_label->l_compartments
 *		l_li_compartments
 *		l_iv_compartments
 *		l_in_compartments
 * 
 *	The COMPARTMENTS and COMPARTMENT_MASKs pointed to by the:
 * 
 *		Information label words table
 *		Sensitivity label words table
 *		Clearance words	table
 *		Channel words table
 *		Printer banner words table
 *		The accreditation range specifications
 *		The name information labels specifications
 * 
 *	The actual MARKINGS pointed to directly by the external variables:
 * 
 *		l_t_markings
 *		l_t2_markings
 *		l_t3_markings
 *		l_t4_markings
 *		l_t5_markings
 *		l_0_markings
 *		l_hi_markings
 *		l_li_markings
 *		l_iv_markings
 *		l_in_markings
 * 
 *	The MARKINGS and MARKING_MASKs pointed to by the:
 * 
 *		Information label words table
 *		Sensitivity label words table
 *		Clearance words table
 *		Channel words table
 *		Printer banner words table
 *		The name information labels specifications
 * 
 *	In "strings" space:
 * 
 *		The character string pointed to by l_version
 *		The classification long, short, and alternate names
 *		The output, soutput, long, short, and input names for each
 *			information label word
 *		The output, soutput, long, short, and input names for each
 *			sensitivity label word
 *		The output, soutput, long, short, and input names for each
 *			clearance word
 *		The output, soutput, long, short, and input names for each
 *			channel word
 *		The output, soutput, long, short, and input names for each
 *			printer banner word
 * 
 * l_init  initializes the global variables l_0_compartments and
 * l_0_markings to point to strings of zero compartment and marking bits,
 * to be used for comparison purposes.
 * 
 * l_init also provides five temporary COMPARTMENTS variables:
 * l_t_compartments, l_t2_compartments, l_t3_compartments, l_t4_compartments,
 * and l_t5_compartments; as well as five temporary MARKINGS variables:
 * l_t_markings, l_t2_markings, l_t3_markings, l_t4_markings, and l_t5_markings.
 */

/*
 * The external pointers that will be allocated by l_init.
 */

char           *l_version;
CLASSIFICATION *l_min_classification;
CLASSIFICATION *l_classification_protect_as;
struct l_sensitivity_label *l_lo_clearance;
struct l_sensitivity_label *l_lo_sensitivity_label;
struct l_sensitivity_label *l_hi_sensitivity_label;
MARKINGS       *l_hi_markings;
COMPARTMENTS   *l_li_compartments;
MARKINGS       *l_li_markings;
COMPARTMENTS   *l_iv_compartments;
MARKINGS       *l_iv_markings;
char          **l_long_classification;
char          **l_short_classification;
char          **l_alternate_classification;
struct l_information_label **l_lc_name_information_label;
struct l_information_label **l_sc_name_information_label;
struct l_information_label **l_ac_name_information_label;
COMPARTMENTS  **l_in_compartments;
MARKINGS      **l_in_markings;
struct l_accreditation_range *l_accreditation_range;
struct l_tables *l_information_label_tables;
struct l_tables *l_sensitivity_label_tables;
struct l_tables *l_clearance_tables;
struct l_tables *l_channel_tables;
struct l_tables *l_printer_banner_tables;
COMPARTMENTS   *l_0_compartments;
MARKINGS       *l_0_markings;
COMPARTMENTS   *l_t_compartments;
COMPARTMENTS   *l_t2_compartments;
COMPARTMENTS   *l_t3_compartments;
COMPARTMENTS   *l_t4_compartments;
COMPARTMENTS   *l_t5_compartments;
MARKINGS       *l_t_markings;
MARKINGS       *l_t2_markings;
MARKINGS       *l_t3_markings;
MARKINGS       *l_t4_markings;
MARKINGS       *l_t5_markings;

/*
 * Keyword lists for calling l_next_keyword (below).  They are defined here
 * in order of their first usage below.  These lists define keywords that are
 * found in the encodings file to define various aspects of the encodings.
 */

#define LIST_END (char *) 0

static char    *version[] =
{
    "VERSION=",
    LIST_END
};

static char    *classifications[] =
{
    "CLASSIFICATIONS:",
    LIST_END
};

static char    *class_keywords[] =
{
    "VALUE=",
    "NAME=",
    "SNAME=",
    "ANAME=",
    "INITIAL COMPARTMENTS=",
    "INITIAL MARKINGS=",
    LIST_END
};

#define CVALUE 0
#define CNAME 1
#define CSNAME 2
#define CANAME 3
#define INITIAL_COMPARTMENTS 4
#define INITIAL_MARKINGS 5

static char    *information_labels[] =
{
    "INFORMATION LABELS:",
    LIST_END
};

static char    *words[] =
{
    "WORDS:",
    LIST_END
};

static char    *name[] =
{
    "NAME=",
    LIST_END
};

static char    *label_keywords[] =
{
    "SNAME=",
    "INAME=",
    "MINCLASS=",
    "OMINCLASS=",
    "MAXCLASS=",
    "OMAXCLASS=",
    "COMPARTMENTS=",
    "MARKINGS=",
    "PREFIX=",
    "SUFFIX=",
    "PREFIX",
    "SUFFIX",
    "ACCESS RELATED",
    "FLAGS=",
    LIST_END
};

#define WSNAME 0
#define WINAME 1
#define WMINCLASS 2
#define WOMINCLASS 3
#define WMAXCLASS 4
#define WOMAXCLASS 5
#define WCOMPARTMENTS 6
#define WMARKINGS 7
#define WNEEDS_PREFIX 8
#define WNEEDS_SUFFIX 9
#define WIS_PREFIX 10
#define WIS_SUFFIX 11
#define WACCESS_RELATED 12
#define WFLAGS 13

static char    *required_combinations[] =
{
    "REQUIRED COMBINATIONS:",
    LIST_END
};

static char    *combination_constraints[] =
{
    "COMBINATION CONSTRAINTS:",
    LIST_END
};

static char    *sensitivity_labels[] =
{
    "SENSITIVITY LABELS:",
    LIST_END
};

static char    *clearances[] =
{
    "CLEARANCES:",
    LIST_END
};

static char    *channels[] =
{
    "CHANNELS:",
    LIST_END
};

static char    *printer_banners[] =
{
    "PRINTER BANNERS:",
    LIST_END
};

static char    *accreditation_range[] =
{
    "ACCREDITATION RANGE:",
    LIST_END
};

static char    *min_clearance[] =
{
    "MINIMUM CLEARANCE=",
    "CLASSIFICATION=",
    LIST_END
};

#define MINIMUM_CLEARANCE 0

/* ptr to CLASSIFICATION= above */
static char   **classification = &min_clearance[1];

static char    *ar_types[] =
{
    "ALL COMPARTMENT COMBINATIONS VALID EXCEPT:",
    "ALL COMPARTMENT COMBINATIONS VALID",
    "ONLY VALID COMPARTMENT COMBINATIONS:",
    LIST_END
};

/*
 * Note that the index into the above array is also the
 * l_type_accreditation_range defined in std_labels.h
 */

static char    *min_sensitivity_label[] =
{
    "MINIMUM SENSITIVITY LABEL=",
    LIST_END
};

static char    *min_protect_as_classification[] =
{
    "MINIMUM PROTECT AS CLASSIFICATION=",
    LIST_END
};

static char    *name_information_labels[] =
{
    "NAME INFORMATION LABELS:",
    LIST_END
};

static char    *name_information_labels_keywords[] =
{
    "NAME=",
    "IL=",
    LIST_END
};

#define NIL_NAME 0
#define NIL_IL 1

/*
 * The following static variables are GLOBAL throughout the subroutines that
 * comprise l_init.c.
 */

static int      counting;	/* flag determines which pass is current */
static int      max_classification_length;	/* length of longest
						 * classification name */
static CLASSIFICATION max_classification;	/* max classification value
						 * given a name */
static CLASSIFICATION max_allowed_class;	/* maximum allowed
						 * classification value */
static unsigned int max_allowed_comp;	/* maximum allowed compartment number */
static unsigned int max_allowed_mark;	/* maximum allowed marking number */
static unsigned int line_number;	/* line number in the encodings being
					 * scanned */
#define NO_LINE_NUMBER 0	/* tells l_error to put no line number in msg */
static char    *convert_buffer = NULL;	/* allocated buffer to test labels */
static short    line_continues;	/* flag sez line in l_buffer continues on
				 * next */

/*
 * The following globals are used as part of the allocation scheme to reserve
 * and allocate space for "tables" and "strings".
 */

static char    *allocated_memory = NULL;	/* ptr to start of large
						 * block allocated */
static unsigned long size_tables;	/* size of tables area to be
					 * allocated */
static unsigned long size_strings;	/* size of strings area to be
					 * allocated */
static char    *strings;	/* ptr to space for converted strings */
static char    *tables;		/* ptr to tables allocated */
static int      num_compartments;	/* number of COMPARTMENTS/MASK
					 * constants to allocate */
static COMPARTMENTS *compartments;	/* used to allocate COMPARTMENTS
					 * within tables */
static int      num_markings;	/* number of MARKINGS/MASK constants to
				 * allocate */
static MARKINGS *markings;	/* used to allocate MARKINGS within tables */
static int      num_information_labels;	/* number of NAME INFORMATION LABELS
					 * to allocate */
static struct l_information_label *information_label;	/* used to allocate NAME
							 * INFORMATION LABELS
							 * within tables */
static int      num_input_names;	/* number of input names to allocate */
static struct l_input_name *input_name;	/* used to allocate input names
					 * within tables */

/*
 * The following external subroutines and variables have an "l_" prefix and
 * are intended to used by vendor-modified l_eof and l_error subroutines.
 * l_error is intended to be modified by a vendor in the case that the vendor
 * wants l_init error messages to go someplace other than the standard
 * output.  l_eof is intended to be modified by a vendor in the case that
 * vendor-specific encodings must be parsed at the bottom of the encodings
 * file.  These subroutines and variables are also used by l_init to do its
 * parsing of the encodings file.  The two subroutines are:
 * 
 *	l_cleanup - frees all allocated memory and closes the encodings file
 *	l_next_keyword - returns information about whether an expected keyword
 *		was found, and fills in l_dp (for data ptr) to be the value of
 *		the found keyword
 * 
 * The variables are:
 * 
 *	l_encodings_file_ptr - the FILE pointer to the encodings file
 *	l_buffer - the buffer into which lines from the encodings file
 *		are read
 *	l_scan_ptr - a pointer into l_buffer to the place current being
 *		parsed; this is the place where l_next_keyword will look
 *		for the next keyword; if an expected keyword cannot be found,
 *		l_scan_ptr will point to the offending unexpected text.
 *	l_dp - a pointer into l_buffer to the data associated with a found
 *		keyword; the data will have been terminated by a '\0' once
 *		l_next_keyword returns.
 */

FILE           *l_encodings_file_ptr = NULL;	/* file ptr for file to read
						 * from */
char            l_buffer[MAX_ENCODINGS_LINE_LENGTH];	/* buffer to read file
							 * data into */
char           *l_scan_ptr;	/* ptr into l_buffer of current scan point */
char           *l_dp;		/* ptr to data returned for each keyword */

/*
 * The external subroutine l_cleanup cleans up after an error, by freeing any
 * allocated memory and closing the encodings file descriptor.  It is called
 * by l_error.
 */

void
l_cleanup()
{

    /*
     * Free the l_convert buffer if it has been allocated, and close the
     * encodings file.
     */

    if (convert_buffer)
	(void) free(convert_buffer);

    if (l_encodings_file_ptr)
	(void) fclose(l_encodings_file_ptr);

    /*
     * If allocated_memory is non-zero, then the main memory block has been
     * allocated, and should be freed.
     */

    if (allocated_memory)
	(void) free(allocated_memory);
}

/*
 * The external subroutine l_next_keyword finds the next keyword in the input
 * file, given a table of valid keywords.  l_next_keyword uses the global
 * variable l_scan_ptr to remember where it should continue looking in the
 * global l_buffer each time it is called. If needed to find the next
 * keyword, the next line of the file will be read into the l_buffer, and the
 * l_scan_ptr to where to look next will be adjusted. The index of the
 * keyword found is returned, or a -1 if no keyword is found, or a -2 if EOF
 * is reached.  Completely blank lines are ignored, as are comment lines,
 * which must start with a *.  If a * is found where a keyword is expected
 * (NOT within a keyword of within a value), the * and the rest of the line
 * are taken to be a comment.
 * 
 * If a keyword is found, a pointer to the data following the keyword is also
 * found, and returned by setting the global variable l_dp (for data ptr) to
 * point to it. The data will be in the form of a zero-terminated string.
 * The end of the data is denoted in the encodings file by either a semicolon
 * or the end of a line.  The semicolon is replaced by a zero to denote end
 * of data, as is the newline at the end of the line.
 * 
 * If data is found following a keyword that does not end in =, then
 * l_next_keyword returns as if the keyword was not found,
 * 
 * The first time l_next_keyword is called, l_scan_ptr to should point to a '\0'
 * in l_buffer.
 * 
 * l_next_keyword READS GLOBAL variables: l_encodings_file_ptr, l_buffer,
 * l_scan_ptr, l_dp. l_next_keyword WRITES GLOBAL variables: l_buffer,
 * l_scan_ptr, l_dp.
 */

int
l_next_keyword(keywords)
    char           *keywords[];	/* the list of valid keywords */
{
    register char  *bp;
    register int    i;
    register int    keyword_length;
    int             have_blank;
#ifndef	TSOL
    int             c;
#else	/* TSOL */
    register wchar_t *wbp;
    wint_t	    wc;
    wchar_t	    w_buf[MAX_ENCODINGS_LINE_LENGTH];
#endif	/* !TSOL */

    bp = l_scan_ptr;		/* set fast ptr to place to start scan for
				 * keyword */

    if (*bp == ' ')
	bp++;			/* skip over blank, if any */

    if (*bp == '*')		/* if rest of line is a comment */
	*bp = '\0';		/* cause rest of line to be ignored */

    /*
     * If we are at the end of this input line, then another line's worth of
     * input is read, leading blanks are removed, multiple blanks or tabs are
     * replaced with a single blank, and alphabetic characters are forced to
     * upper case.  The \n at the end of the line is replaced with a \0, and
     * bp is left at the start of the line.  If a line longer than
     * MAX_ENCODINGS_LINE_LENGTH characters is found, it is replaced in the
     * l_buffer by an error message (see below) that will end of getting
     * printed in an error message by the caller of l_next_keyword.
     */

#ifdef	TSOL
    while (*bp == '\0') {	/* if at end of a line */
	have_blank = TRUE;	/* causes leading blanks to be ignored */
	wbp = w_buf;

	while (EOF != (wc = fgetwc(l_encodings_file_ptr))) {
	    if (wbp == (w_buf + MAX_ENCODINGS_LINE_LENGTH)) {
		/* line too long */
		(void) snprintf(l_buffer, sizeof (l_buffer),
		    dgettext(TEXT_DOMAIN,
		    "<<<Line longer than %d characters>>>"),
		    MAX_ENCODINGS_LINE_LENGTH);
		l_scan_ptr = l_buffer;
		line_number++;
		return (-1);	/* indicate keyword not found */
	    }

	    if (wc != L'\n') {	/* for each character in input line */
		if (!have_blank && iswspace(wc)) {
		    have_blank = TRUE;
		    *wbp++ = L' ';
		} else if (!iswspace(wc)) {
		    have_blank = FALSE;
		    if (iswlower(wc))	{
			/* force upper case */
			*wbp++ = (wchar_t) towupper(wc);
		    } else {
			*wbp++ = (wchar_t) wc;
		    }
		}
	    } else {
		line_number++;	/* increment count of lines in file */
		break;
	    }
	}  /* while (EOF != (wc = fgetwc())) */

	if (wc == EOF && wbp == w_buf) {
	    l_scan_ptr = l_buffer;	/* in case caller calls again! */
	    *l_scan_ptr = '\0';	/* make line look empty in case called again */
	    return (-2);	/* return EOF indication */
	}

	*wbp = L'\0';		/* set end of line */

	i = (int) wcstombs(l_buffer, w_buf, MAX_ENCODINGS_LINE_LENGTH);
	if (i == MAX_ENCODINGS_LINE_LENGTH) {
	    /* line too long */
	    (void) snprintf(l_buffer, sizeof (l_buffer),
		dgettext(TEXT_DOMAIN, "<<<Line longer than %d bytes>>>"),
		MAX_ENCODINGS_LINE_LENGTH);
	    l_scan_ptr = l_buffer;
	    return (-1);	/* indicate keyword not found */
	} else if (i == -1) {
	    /* no multibyte character for given wide character */
	    (void) snprintf(l_buffer, sizeof (l_buffer),
		dgettext(TEXT_DOMAIN, "<<<Non-multibyte character in line>>>"));
	    l_scan_ptr = l_buffer;
	    return (-1);	/* indicate keyword not found */
	}

	bp = l_buffer;
	if (*bp == '*')		/* if a comment line */
	    *bp = '\0';		/* cause line to be ignored */
    }  /* while (*bp == `\0`) */
#else	/* !TSOL */
    while (*bp == '\0') {	/* if at end of a line */
	have_blank = TRUE;	/* causes leading blanks to be ignored */
	bp = l_buffer;

	while (EOF != (c = fgetc(l_encodings_file_ptr))) {
	    if (bp == (l_buffer + MAX_ENCODINGS_LINE_LENGTH)) {
		/* line too long */
		(void) sprintf(l_buffer, "<<<Line longer than %d characters>>>",
			       MAX_ENCODINGS_LINE_LENGTH);
		l_scan_ptr = l_buffer;
		line_number++;
		return (-1);	/* indicate keyword not found */
	    }

	    if (c != '\n') {	/* for each character in input line */
		if (!have_blank && isspace((char) c)) {
		    have_blank = TRUE;
		    *bp++ = ' ';
		} else if (!isspace((char) c)) {
		    have_blank = FALSE;
		    if (islower((char) c))	/* force upper case */
			*bp++ = toupper((char) c);
		    else
			*bp++ = (char) c;
		}
	    } else {
		line_number++;	/* increment count of lines in file */
		break;
	    }
	}

	*bp = '\0';		/* make line look empty in case called again */

	if (c == EOF && bp == l_buffer) {
	    l_scan_ptr = bp;	/* in case caller calls again! */
	    return (-2);	/* return EOF indication */
	}

	bp = l_buffer;
	if (*bp == '*')		/* if a comment line */
	    *bp = '\0';		/* cause line to be ignored */
    }
#endif	/* TSOL */

    /*
     * Now look for each keyword passed in the l_buffer.
     */

    l_scan_ptr = bp;		/* in case an error, return l_scan_ptr here */

    for (i = 0; keywords[i]; i++) {	/* for each valid keyword */
	keyword_length = strlen(keywords[i]);

	if (0 == strncmp(keywords[i], bp, keyword_length)) {
	    /* if keyword found */
	    bp += keyword_length;	/* skip over keyword */
	    if (*bp == ' ')
		bp++;		/* skip over blank, if any */
	    if (keywords[i][keyword_length - 1] != '=' && *bp && *bp != ';')
		return (-1);	/* error if data follows a non = keyword */
	    l_dp = bp;		/* set return pointer to data after keyword */
	    while (*bp && *bp != ';')
		bp++;		/* find ; that ends keyword */
	    if (*bp) {
		*bp = '\0';	/* make string termination of data portion */
		l_scan_ptr = bp + 1;	/* set return ptr to next place to
					 * look */
	    } else
		l_scan_ptr = bp;
	    return (i);		/* return index of keyword */
	}
    }
    return (-1);		/* return keyword not found */
}

/*
 * If the current line had a \ at the end (indicated by line_continues being
 * true), the subroutine check_continuation checks whether there is any
 * non-blank text left on the current line.  If not, the next line is read
 * and check for a \ at the end. If a \ is found, it is removed and
 * line_continues is left TRUE.  If no \ is found, line_continues is set to
 * FALSE.
 */

static void
check_continuation(next_section)
    char           *next_section[];	/* keyword list for calling
					 * l_next_keyword */
{
    register char  *cp;

    if (*l_scan_ptr == ' ')
	l_scan_ptr++;		/* skip any blank present */

    if (line_continues && *l_scan_ptr == '\0') {
	(void) l_next_keyword(next_section);

	/* get ptr to last char */
	cp = l_scan_ptr + strlen(l_scan_ptr) - 1;
	if (*cp == '\\')	/* if line continues */
	    *cp = '\0';		/* terminate before \ */
	else
	    line_continues = FALSE;
    }
}

/*
 * The next set of five subroutines are used to parse various types of bit
 * string indicators.
 * 
 * The first major subroutine parse_bits parses the most general form of bit
 * string indicators, and sets the passed bit value and masks strings
 * according to the indicators.  parse_bits is passed one of the next three
 * subroutines as an argument.  These three subroutines (set_compartment,
 * set_marking, and set_bits) are used to set bits in compartment, marking,
 * or other bits strings, respectively.
 * 
 * The final major subroutine in this group, parse_bit_range, parses a single
 * bit range, returning the low and high bit numbers in the range.
 */

/*
 * The internal subroutine parse_bits parses the bit indication strings that
 * follow COMPARTMENTS, MARKINGS, or FLAGS keywords.  The indication strings
 * can have one of the forms:
 * 
 *	[~]N
 *	[~]N1-N2
 * 
 * or any of the above forms separated by blanks.  N, N1, and N2 are decimal bit
 * numbers, starting with 0 as the leftmost bit.  The optional ~ means that
 * the indicated bit(s) should be OFF as opposed to the default of ON.
 * 
 * This subroutine is passed a ptr to the string to parse, the maximum
 * permissible value for a bit to be processed, the routine that actually
 * sets the bits as specified, and pointer to the bit mask and value to be
 * operated on by the passed routine. See below for the routines applicable
 * to COMPARTMENTS, MARKINGS, and FLAGS.
 * 
 * TRUE is returned if everything parsed OK.  FALSE is returned if the string
 * was not in the proper format.  A null string is ignored.
 */

static int
parse_bits(string, max_bit, routine, mask_ptr, value_ptr)
    char           *string;	/* the bit specification to parse */
    unsigned int    max_bit;	/* the maximum bit value to parse */
    void            (*routine) ();	/* routine to operate on COMPARTMENTS
					 * or MARKINGS */
    BIT_STRING     *mask_ptr;	/* ptr to COMPARTMENTS or MARKINGS mask */
    BIT_STRING     *value_ptr;	/* ptr to COMPARTMENTS or MARKINGS value */
{
    short           bit_on;
    unsigned int    first_bit;
    unsigned int    last_bit;

    if (*string == ' ')
	string++;		/* skip leading blank, if any */

    while (*string) {
	if (*string == '~') {
	    bit_on = FALSE;
	    string++;
	} else
	    bit_on = TRUE;

	if (!isdigit(*string))
	    return (FALSE);	/* must be decimal digit */

	first_bit = (unsigned int) strtol(string, &string, 10);

	if (first_bit > max_bit)
	    return (FALSE);	/* error if bit # too large */

	if (*string == '-') {	/* bit range specified? */
	    string++;		/* skip over the - */
	    if (!isdigit(*string))
		return (FALSE);	/* must be decimal digit */
	    last_bit = (unsigned int) strtol(string, &string, 10);
	    if (last_bit > max_bit	/* error if bit# too large */
		|| last_bit <= first_bit)	/* error if last_bit <=
						 * first_bit */
		return (FALSE);
	} else
	    last_bit = first_bit;

	for (; first_bit <= last_bit; first_bit++)
	    (*routine) (mask_ptr, value_ptr, first_bit, bit_on);

	if (*string == ' ')
	    string++;		/* skip trailing blank, if any */
    }
    return (TRUE);
}

/*
 * The internal subroutine set_compartment is intended to be called by
 * parse_bits (above).  It sets up the compartment mask and value for the
 * passed bit for the passed bit value.
 */

static void
set_compartment(mask_ptr, value_ptr, bit, bit_on)
    BIT_STRING     *mask_ptr;	/* COMPARTMENT_MASK to set */
    BIT_STRING     *value_ptr;	/* COMPARTMENTS value to set */
    int             bit;	/* the bit to set (numbered from left) */
    short           bit_on;	/* flag sez turn the bit on instead of off */
{
    COMPARTMENT_MASK_BIT_SET(mask_ptr->l_cm, bit);
    if (bit_on)
	COMPARTMENTS_BIT_SET(value_ptr->l_c, bit);
}

/*
 * The internal subroutine set_marking is intended to be called by parse_bits
 * (above).  It sets up the marking mask and value for the passed bit for the
 * passed bit value.
 */

static void
set_marking(mask_ptr, value_ptr, bit, bit_on)
    BIT_STRING     *mask_ptr;	/* MARKING_MASK to set */
    BIT_STRING     *value_ptr;	/* MARKINGS value to set */
    int             bit;	/* the bit to set (numbered from left) */
    short           bit_on;	/* flag sez turn the bit on instead of off */
{
    MARKING_MASK_BIT_SET(mask_ptr->l_mm, bit);
    if (bit_on)
	MARKINGS_BIT_SET(value_ptr->l_m, bit);
}

/*
 * The internal subroutine set_bits is intended to be called by parse_bits
 * (above).  It sets the specified bit if bit_on is true, in the variable
 * length bit string indicated by value_ptr.  The mask_ptr argument is
 * ignored, and is included only for compatibility with the other routines
 * called by parse_bits.
 */

static void
set_bits(mask_ptr, value_ptr, bit, bit_on)
    BIT_STRING     *mask_ptr;	/* argument is ignored */
    BIT_STRING     *value_ptr;	/* flags value to set */
    int             bit;	/* the bit to set (numbered from right) */
    short           bit_on;	/* flag sez turn the bit on instead of off */
{
    if (bit_on)
	((char *) &(value_ptr->l_short))[bit / BITS_PER_BYTE] |=
	    LEFTMOST_BIT_IN_BYTE >> (bit % BITS_PER_BYTE);
#ifdef lint
    mask_ptr = mask_ptr;	/* keep lint happy */
#endif	/* lint */
}

/*
 * The internal subroutine find_exact_alias looks for an exact alias for the
 * passed word in the passed l_tables, searching previous to the word in the
 * table.  An exact alias is another word with identical compartments and
 * markings.  If found, l_w_exact_alias is set to the index of the word for
 * which this word is an alias.
 */

static void
find_exact_alias(l_tables, first_main_entry, indx)
    register struct l_tables *l_tables;	/* l_tables for type of label being
					 * processed */
    int             first_main_entry;	/* first main entry in l_tables (or
					 * -1) */
    int             indx;	/* index of word to see if exact alias */
{
    register int    i;

    for (i = first_main_entry; i < indx; i++) {
	if (COMPARTMENT_MASK_EQUAL(l_tables->l_words[i].l_w_cm_mask,
				   l_tables->l_words[indx].l_w_cm_mask))
	    if (COMPARTMENTS_EQUAL(l_tables->l_words[i].l_w_cm_value,
				   l_tables->l_words[indx].l_w_cm_value))
		if (MARKING_MASK_EQUAL(l_tables->l_words[i].l_w_mk_mask,
				       l_tables->l_words[indx].l_w_mk_mask))
		    if (MARKINGS_EQUAL(l_tables->l_words[i].l_w_mk_value,
				       l_tables->l_words[indx].l_w_mk_value))
			if (l_tables->l_words[i].l_w_min_class ==
			    l_tables->l_words[indx].l_w_min_class
			    && l_tables->l_words[i].l_w_output_min_class ==
			    l_tables->l_words[indx].l_w_output_min_class
			    && l_tables->l_words[i].l_w_max_class ==
			    l_tables->l_words[indx].l_w_max_class) {
			    l_tables->l_words[indx].l_w_exact_alias = i;
			    return;
			}
    }
}

/*
 * The internal subroutine check_special_inverse handles the special case of
 * SPECIAL_INVERSE words.
 * 
 * Such, words require a prefix that specifies compartments or markings. The
 * intended usage of such a word is to contain inverse bits that are not
 * reserved as 1's in the initial compartments or markings, but instead are
 * reserved as 1's in the required prefix.  If the passed word is such a
 * word, then this subroutine makes sure that the word contains such inverse
 * bits as mentioned above, and that the bits specified by the word must be a
 * subset (including proper) of those specified in the prefix.  If not, an
 * error is indicated.  If so, the following flags are set in l_w_type:
 *	SPECIAL_INVERSE, to indicate that this word has special inverse bits,
 *		and requires a prefix with compartments or markings; and
 *	SPECIAL_COMPARTMENTS_INVERSE, to indicate that the word has inverse
 *		compartment bits (it may or may not also have inverse marking
 *		bits).
 */

static int
check_special_inverse(l_tables, indx, type)
    register struct l_tables *l_tables;	/* l_tables for type of label being
					 * processed */
    register int    indx;	/* index of word to check */
    char           *type;	/* the type of words being processed */
{
    register int    i;

    i = l_tables->l_words[indx].l_w_prefix;

    if (i >= 0 && (l_tables->l_words[i].l_w_cm_mask != l_0_compartments
	    || l_tables->l_words[i].l_w_mk_mask != l_0_markings)) {
        /* if word requires a prefix with compartments or markings */

	if (!COMPARTMENTS_DOMINATE(l_tables->l_words[i].l_w_cm_mask,
				   l_tables->l_words[indx].l_w_cm_mask)
	    || !MARKINGS_DOMINATE(l_tables->l_words[i].l_w_mk_mask,
				  l_tables->l_words[indx].l_w_mk_mask)) {

	    l_error(NO_LINE_NUMBER, "In %s WORDS, word \"%s\":\n\
   A word that requires a prefix with compartments or markings must specify\n\
   a subset of the bits in the prefix.\n",
		    type, l_tables->l_words[indx].l_w_output_name);
	    return (FALSE);
	}

	l_tables->l_words[indx].l_w_type |= SPECIAL_INVERSE;

	COMPARTMENTS_COPY(l_t_compartments, l_tables->l_words[indx].l_w_cm_mask);
	COMPARTMENTS_XOR(l_t_compartments, l_tables->l_words[indx].l_w_cm_value);
	/* l_t_compartments now has mask of 0 value bits */

	COMPARTMENTS_AND(l_t_compartments, l_tables->l_words[i].l_w_cm_value);
	/* l_t_compartments now has inverse bits from this word */

	if (!COMPARTMENTS_EQUAL(l_t_compartments, l_0_compartments)) {

	    l_tables->l_words[indx].l_w_type |= SPECIAL_COMPARTMENTS_INVERSE;
	} else {
	    /* no inverse compartment bits, so make sure markings are inverse */

	    MARKINGS_COPY(l_t_markings, l_tables->l_words[indx].l_w_mk_mask);
	    MARKINGS_XOR(l_t_markings, l_tables->l_words[indx].l_w_mk_value);
	    /* l_t_markings now has mask of 0 value bits */

	    MARKINGS_AND(l_t_markings, l_tables->l_words[i].l_w_mk_value);
	    /* l_t_markings now has inverse bits from this word */

	    if (MARKINGS_EQUAL(l_t_markings, l_0_markings)) {

		l_error(NO_LINE_NUMBER, "In %s WORDS, word \"%s\":\n\
  A word that requires a prefix with compartments or markings must also\n\
  specify special inverse compartment or marking bits that correspond to bits\n\
  in the required prefix's compartments or markings.\n",
			type, l_tables->l_words[indx].l_w_output_name);
		return (FALSE);
	    }
	}
    }

    return (TRUE);
}

/*
 * The internal subroutine do_words is called to handle the WORDS keywords
 * for each type of label.  It is passed a character string name of the
 * section whose words are being converted, for use in error messages, a
 * pointer to the appropriate count of words handled to update, and a pointer
 * to the appropriate l_tables.  The output_only flag indicates that the
 * words being processed will not be used for input, and that therefore the
 * output name and the long name can be the same, saving space in the tables.
 * 
 * do_words is called during both the counting and conversion passes, after the
 * keyword that starts each section has been found by l_next_keyword.  The
 * words in each section are handled in the same manner except for the
 * INFORMATION LABELS section, for which the additional ACCESS RELATED
 * keyword is handled.
 * 
 * Each NAME keyword starts a new l_words entry.  For the counting pass,
 * do_words counts the number of l_word entries, and updates size_strings and
 * size_tables to account for the strings and tables spaces that will be
 * needed during the conversion pass.
 * 
 * For the conversion pass, each keyword is handled in its own unique manner,
 * and HAS_COMPARTMENTS, COMPARTMENTS_INVERSE, and HAS_ZERO_MARKINGS are set
 * in l_w_type as appropriate.
 * 
 * TRUE is returned if everythings converts OK.  Otherwise FALSE is returned
 * after an appropriate error message is printed.
 */

#define OUTPUT_ONLY 1		/* output_only argument value */
#define INPUT_OUTPUT 0		/* output_only argument value */

static int
do_words(type, count_ptr, comps_ptr, marks_ptr, l_tables, output_only)
    char           *type;	/* the type of label being handled, for error
				 * messages */
    int            *count_ptr;	/* ptr to count of number of word entries */
    COMPARTMENTS   *comps_ptr;	/* ptr to compartments to update with word's
				 * comps */
    MARKINGS       *marks_ptr;	/* ptr to markings to update with word's
				 * marks */
    register struct l_tables *l_tables;	/* l_tables for type of label being
					 * processed */
    short           output_only;/* flag sez words not used for input */
{
    int             keyword;	/* the keyword found by l_next_keyword */
    int             first_main_entry;	/* index of first main entry
					 * (non-prefix/suffix) */
    register int    indx;	/* index into l_word table being filled */
    register int    j;		/* loop index */

    /*
     * Make sure encodings start with WORDS keyword.
     */

    if (0 > l_next_keyword(words)) {
	l_error(line_number, "Can't find %s WORDS specification.\n\
   Found instead: \"%s\".\n", type, l_scan_ptr);
	return (FALSE);
    }
    /*
     * WORDS parse loop initialization.
     */

    if (counting)
	*count_ptr = 0;		/* initialize count of words in table */
    else {
	indx = -1;		/* initialize index into IL l_word table */
	first_main_entry = -1;	/* first main entry not found yet */
    }

    /*
     * The NAME keyword must start each WORDS entry.  This loop is for each
     * NAME keyword.  A subordinate loop finds and processes the other
     * keywords associated with a word.
     */

    while (0 == l_next_keyword(name)) {

	/*
	 * LABEL WORDS NAME processing for counting pass.  Count the number
	 * of label words.  Adjust size_tables to account for this word
	 * entry, and size_strings for the size of the name string.  If the
	 * name string has a / in it, its size must be counted twice because
	 * the output_name for this word will be different than the
	 * long_name.  Account in size_strings for the space needed to
	 * allocate the COMPARTMENTS and MARKINGS masks and value strings.
	 */

	if (counting) {
	    ++(*count_ptr);	/* increment count of this type of word */
	    j = strlen(l_dp) + 1;	/* compute size of name string */
	    size_strings += j;	/* account for size of string */

	    if (!output_only) {
		for (; *l_dp; l_dp++)
		    if (*l_dp == '/')
			break;	/* abort loop early if / found */

		if (*l_dp)	/* if loop aborted early, / was found */
		    size_strings += j;	/* account for size of string w/ /
					 * changed to blank */
	    }
	}
	/*
	 * LABEL WORDS NAME processing for conversion pass.
	 */

	else {

	    /*
	     * If this is not the first word about to be processed, and if
	     * the first main (non-prefix/suffix) entry has not yet been
	     * found, check whether the previous entry was a prefix or
	     * suffix, and if not, set first_main_entry to be the index of
	     * the previous word.
	     * 
	     * Then, record in l_w_type whether the previous word had zero
	     * markings, and make error checks for special inverse words, and
	     * see if the word is an exact alias.
	     */

	    if (-1 != indx) {	/* if at least one word found */
		if (-1 == first_main_entry	/* if first main entry not
						 * found yet */
		    && l_tables->l_words[indx].l_w_prefix != L_IS_PREFIX
		    && l_tables->l_words[indx].l_w_suffix != L_IS_SUFFIX)
		    first_main_entry = indx;

		if (MARKINGS_EQUAL(l_tables->l_words[indx].l_w_mk_value,
				   l_0_markings))
		    l_tables->l_words[indx].l_w_type |= HAS_ZERO_MARKINGS;

		if (check_special_inverse(l_tables, indx, type) == FALSE)
		    return (FALSE);

		if (-1 != first_main_entry)
		    find_exact_alias(l_tables, first_main_entry, indx);
	    }
	    /*
	     * Now increment index for word about to be processed, and
	     * initialize those l_words entries that have non-zero default
	     * values.  Then copy the NAME string to the strings buffer, and
	     * set the flag to indicate that we have not yet processed
	     * compartments or markings for this word.
	     */

	    indx++;		/* increment index to l_words entry to
				 * process */
	    l_tables->l_words[indx].l_w_cm_mask = l_0_compartments;
	    l_tables->l_words[indx].l_w_cm_value = l_0_compartments;
	    l_tables->l_words[indx].l_w_mk_mask = l_0_markings;
	    l_tables->l_words[indx].l_w_mk_value = l_0_markings;
	    l_tables->l_words[indx].l_w_prefix = L_NONE;
	    l_tables->l_words[indx].l_w_suffix = L_NONE;
	    l_tables->l_words[indx].l_w_exact_alias = NO_EXACT_ALIAS;
	    l_tables->l_words[indx].l_w_min_class = -1;
	    l_tables->l_words[indx].l_w_output_min_class = -1;
	    l_tables->l_words[indx].l_w_max_class = max_classification + 1;
	    l_tables->l_words[indx].l_w_output_max_class = max_classification
							   + 1;
	    /* save ptr to output name */
	    l_tables->l_words[indx].l_w_output_name = strings;
	    while (*strings++ = *l_dp++)
		continue;	/* copy output name to strings buffer */
#ifdef	TSOL
	    if (strchr(l_tables->l_words[indx].l_w_output_name, (int)',')
		!= NULL) {
		l_error(line_number, "Illegal ',' in NAME \"%s\".\n",
	    		    l_tables->l_words[indx].l_w_output_name);
		return (FALSE);
	    }
#endif	/* TSOL */

	    /*
	     * Now, if these words are not used for output only, then if the
	     * long name contains any / characters, then it must be stored
	     * again with the slashes turned to blanks as the long name, so
	     * that it can match canonicalized input that has slashes turned
	     * to blanks.  If there is no /, then the long name is the same
	     * as the output name.
	     */

	    if (!output_only) {
		for (l_dp = l_tables->l_words[indx].l_w_output_name;
		     *l_dp; l_dp++)
		    if (*l_dp == '/')
			break;	/* abort loop early if / found */

		if (*l_dp) {	/* if loop aborted early, / was found */
		    l_tables->l_words[indx].l_w_long_name = strings;
		    for (l_dp = l_tables->l_words[indx].l_w_output_name;
			 *l_dp; l_dp++) {
			if (*l_dp == '/')
			    *strings++ = ' ';	/* convert / to blank */
			else
			    *strings++ = *l_dp;
		    }
		    *strings++ = '\0';	/* terminate the string */
		} else
		    l_tables->l_words[indx].l_w_long_name =
			l_tables->l_words[indx].l_w_output_name;
	    } else
		l_tables->l_words[indx].l_w_long_name =
		    l_tables->l_words[indx].l_w_output_name;
	}

	/*
	 * Now that the NAME keyword has been processed, search for and
	 * handle the remaining keywords associated with the word.
	 */

	while (0 <= (keyword = l_next_keyword(label_keywords))) {

	    /*
	     * If the counting pass, just count string sizes for the short
	     * name, and number of compartment or marking strings needed.
	     */

	    if (counting) {
		switch (keyword) {
		case WSNAME:

		    /*
		     * If the short name string has a / in it, its size must
		     * be counted twice because the soutput_name for this
		     * word will be different than the short_name.
		     */

		    j = strlen(l_dp) + 1;	/* compute size of name
						 * string */
		    size_strings += j;	/* account for size of string */

		    if (!output_only) {
			for (; *l_dp; l_dp++)
			    if (*l_dp == '/')
				break;	/* abort loop early if / found */

			if (*l_dp)	/* if loop aborted early, / was found */
			    size_strings += j;	/* account for size of string
						 * w/ / changed to blank */
		    }
		    break;

		case WINAME:

		    /*
		     * Input names are stored only once, with slashes
		     * removed, because they are NEVER output.  For each
		     * iname= (there can be any number), size_strings must be
		     * adjusted, as must num_input_names.
		     */
		    size_strings += strlen(l_dp) + 1;	/* add size of name
							 * string */
		    num_input_names++;	/* increment count of input names */
		    break;

		case WCOMPARTMENTS:

		    if (!*l_dp)
			break;	/* accept and ignore empty keyword */
		    num_compartments += 2;	/* for mask and value */
		    break;

		case WMARKINGS:

		    if (!*l_dp)
			break;	/* accept and ignore empty keyword */
		    num_markings += 2;	/* for mask and value */
		    break;
		}
	    }
	    /*
	     * If the conversion pass, each keyword is fully coverted into
	     * the internal tabular format.  Various error checks are made.
	     * The SNAME is copied to the strings area, and a pointer to it
	     * left in the l_word structure. The minimum and output minimum
	     * classifications (if any) are looked up in the classification
	     * table, and any required prefix or suffix is looked up in the
	     * l_words table, and must therefore appear before any references
	     * to it. Compartment and marking bits are converted into
	     * internal form and stored in the table entry.  If the PREFIX or
	     * SUFFIX keyword appears, this fact is so noted.  Finally, the
	     * ACCESS RELATED keyword is processed only if the l_tables
	     * passed is the l_information_label_tables, as this keyword only
	     * applies in information labels.
	     */

	    else {		/* not counting...convert and store values */
		switch (keyword) {
		case WSNAME:

		    if (l_tables->l_words[indx].l_w_short_name) {
			l_error(NO_LINE_NUMBER, "In %s WORDS, word \"%s\":\n\
   Duplicate keyword \"%s %s\".\n", type,
				l_tables->l_words[indx].l_w_output_name,
				label_keywords[keyword], l_dp);
			return (FALSE);
		    }
		    /*
		     * Store the name in the buffer for the short output
		     * name.
		     */

		    l_tables->l_words[indx].l_w_soutput_name = strings;
		    while (*strings++ = *l_dp++)
			continue;	/* copy name to strings buffer */
#ifdef	TSOL
	    	    if (strchr(l_tables->l_words[indx].l_w_soutput_name,
			       (int)',') != NULL) {
			l_error(line_number, "Illegal ',' in SNAME %s.\n",
	    		        l_tables->l_words[indx].l_w_soutput_name);
			return (FALSE);
	    	    }
#endif	/* TSOL */

		    /*
		     * Now, if these words are not used for output only, then
		     * if the short name contains any / characters, then it
		     * must be stored again with the slashes turned to blanks
		     * as the short name, so that it can match canonicalized
		     * input that has slashes turned to blanks.  If there is
		     * no /, then the short name is the same as the soutput
		     * name.
		     */

		    if (!output_only) {
			for (l_dp = l_tables->l_words[indx].l_w_soutput_name;
			     *l_dp; l_dp++)
			    if (*l_dp == '/')
				break;	/* abort loop early if / found */

			if (*l_dp) {	/* if loop aborted early, / was found */
			    l_tables->l_words[indx].l_w_short_name = strings;
			    for (l_dp=l_tables->l_words[indx].l_w_soutput_name;
			         *l_dp; l_dp++) {
				if (*l_dp == '/')
				    *strings++ = ' ';	/* convert / to blank */
				else
				    *strings++ = *l_dp;
			    }
			    *strings++ = '\0';	/* terminate the string */
			} else
			    l_tables->l_words[indx].l_w_short_name =
				l_tables->l_words[indx].l_w_soutput_name;
		    } else
			l_tables->l_words[indx].l_w_short_name =
			    l_tables->l_words[indx].l_w_soutput_name;

		    break;

		case WINAME:

		    /*
		     * An input name makes sense only for words that are not
		     * used output only, so print an error message if these
		     * are output_only words.
		     */

		    if (output_only) {
			l_error(NO_LINE_NUMBER, "In %s WORDS, word \"%s\":\n\
   Keyword INAME does not apply to %s words.\n", type,
				l_tables->l_words[indx].l_w_output_name, type);
			return (FALSE);
		    }
		    /*
		     * Allocate a new input_name structure, linking it into
		     * the START of the list started by l_w_input_name.  Then
		     * store the input name with any slashes turned into
		     * blanks.
		     */

		    input_name->next_input_name =
			l_tables->l_words[indx].l_w_input_name;
		    /* link current list to new entry */

		    l_tables->l_words[indx].l_w_input_name = input_name;
		    /* put new entry as start of list */

		    input_name->name_string = strings;

		    while (*strings = *l_dp++) {
			if (*strings == '/')
			    *strings = ' ';	/* change / to blank */
			strings++;
		    }
#ifdef	TSOL
	    	    if (strchr(input_name->name_string, (int)',') != NULL) {
			l_error(line_number, "Illegal ',' in INAME \"%s\".\n",
	    		        input_name->name_string);
			return (FALSE);
		    }
#endif	/* TSOL */

		    strings++;
		    input_name++;
		    break;

		case WMINCLASS:

		    if (l_tables->l_words[indx].l_w_min_class != -1) {
			l_error(NO_LINE_NUMBER, "In %s WORDS, word \"%s\":\n\
   Duplicate keyword \"%s %s\".\n", type,
				l_tables->l_words[indx].l_w_output_name,
				label_keywords[keyword], l_dp);
			return (FALSE);
		    }
		    for (j = 0; j <= max_classification; j++)
			if (l_long_classification[j]) {
			    if (0 == strcmp(l_dp, l_long_classification[j]))
				break;
			    if (0 == strcmp(l_dp, l_short_classification[j]))
				break;
			    if (l_alternate_classification[j] &&
				0 == strcmp(l_dp,l_alternate_classification[j]))
				break;
			}
		    if (j <= max_classification)	/* if a match found */
			l_tables->l_words[indx].l_w_min_class = j;
		    else {
			l_error(NO_LINE_NUMBER, "In %s WORDS, word \"%s\":\n\
   MINIMUM CLASSIFICATION \"%s\" not found.\n", type,
			     l_tables->l_words[indx].l_w_output_name, l_dp);
			return (FALSE);	/* error if not found */
		    }

		    if (l_tables->l_words[indx].l_w_min_class >
			l_tables->l_words[indx].l_w_max_class) {
			l_error(NO_LINE_NUMBER, "In %s WORDS, word \"%s\":\n\
   MINCLASS \"%s\" is greater than MAXCLASS \"%s\".\n", type,
			      l_tables->l_words[indx].l_w_output_name, l_dp,
		l_long_classification[l_tables->l_words[indx].l_w_max_class]);
			return (FALSE);
		    }
		    if (l_tables->l_words[indx].l_w_min_class >
			l_tables->l_words[indx].l_w_output_max_class) {
			l_error(NO_LINE_NUMBER, "In %s WORDS, word \"%s\":\n\
   MINCLASS \"%s\" is greater than OMAXCLASS \"%s\".\n", type,
			      l_tables->l_words[indx].l_w_output_name, l_dp,
	l_long_classification[l_tables->l_words[indx].l_w_output_max_class]);
			return (FALSE);
		    }
		    break;

		case WOMINCLASS:

		    if (l_tables->l_words[indx].l_w_output_min_class != -1) {
			l_error(NO_LINE_NUMBER, "In %s WORDS, word \"%s\":\n\
   Duplicate keyword \"%s %s\".\n", type,
				l_tables->l_words[indx].l_w_output_name,
				label_keywords[keyword], l_dp);
			return (FALSE);
		    }
		    for (j = 0; j <= max_classification; j++)
			if (l_long_classification[j]) {
			    if (0 == strcmp(l_dp, l_long_classification[j]))
				break;
			    if (0 == strcmp(l_dp, l_short_classification[j]))
				break;
			    if (l_alternate_classification[j] &&
				0 == strcmp(l_dp,l_alternate_classification[j]))
				break;
			}
		    if (j <= max_classification)	/* if a match found */
			l_tables->l_words[indx].l_w_output_min_class = j;
		    else {
			l_error(NO_LINE_NUMBER, "In %s WORDS, word \"%s\":\n\
   OUTPUT MINIMUM CLASSIFICATION \"%s\" not found.\n", type,
			     l_tables->l_words[indx].l_w_output_name, l_dp);
			return (FALSE);	/* error if not found */
		    }

		    if (l_tables->l_words[indx].l_w_output_min_class >
					l_tables->l_words[indx].l_w_max_class) {
			l_error(NO_LINE_NUMBER, "In %s WORDS, word \"%s\":\n\
   OMINCLASS \"%s\" is greater than MAXCLASS \"%s\".\n", type,
			      l_tables->l_words[indx].l_w_output_name, l_dp,
		l_long_classification[l_tables->l_words[indx].l_w_max_class]);
			return (FALSE);
		    }
		    break;

		case WMAXCLASS:

		    if (l_tables->l_words[indx].l_w_max_class !=
						     max_classification + 1) {
			l_error(NO_LINE_NUMBER, "In %s WORDS, word \"%s\":\n\
   Duplicate keyword \"%s %s\".\n", type,
				l_tables->l_words[indx].l_w_output_name,
				label_keywords[keyword], l_dp);
			return (FALSE);
		    }
		    for (j = 0; j <= max_classification; j++)
			if (l_long_classification[j]) {
			    if (0 == strcmp(l_dp, l_long_classification[j]))
				break;
			    if (0 == strcmp(l_dp, l_short_classification[j]))
				break;
			    if (l_alternate_classification[j] &&
				0 == strcmp(l_dp,l_alternate_classification[j]))
				break;
			}
		    if (j <= max_classification)	/* if a match found */
			l_tables->l_words[indx].l_w_max_class = j;
		    else {
			l_error(NO_LINE_NUMBER, "In %s WORDS, word \"%s\":\n\
   MAXIMUM CLASSIFICATION \"%s\" not found.\n", type,
			     l_tables->l_words[indx].l_w_output_name, l_dp);
			return (FALSE);	/* error if not found */
		    }

		    if (l_tables->l_words[indx].l_w_output_min_class >
					l_tables->l_words[indx].l_w_max_class) {
			l_error(NO_LINE_NUMBER, "In %s WORDS, word \"%s\":\n\
   MAXCLASS \"%s\" is less than OMINCLASS \"%s\".\n", type,
			      l_tables->l_words[indx].l_w_output_name, l_dp,
	l_long_classification[l_tables->l_words[indx].l_w_output_min_class]);
			return (FALSE);
		    }
		    if (l_tables->l_words[indx].l_w_min_class >
					l_tables->l_words[indx].l_w_max_class) {
			l_error(NO_LINE_NUMBER, "In %s WORDS, word \"%s\":\n\
   MAXCLASS \"%s\" is less than MINCLASS \"%s\".\n", type,
			      l_tables->l_words[indx].l_w_output_name, l_dp,
		l_long_classification[l_tables->l_words[indx].l_w_min_class]);
			return (FALSE);
		    }
		    break;

		case WOMAXCLASS:

		    if (l_tables->l_words[indx].l_w_output_max_class !=
						max_classification + 1) {
			l_error(NO_LINE_NUMBER, "In %s WORDS, word \"%s\":\n\
   Duplicate keyword \"%s %s\".\n", type,
				l_tables->l_words[indx].l_w_output_name,
				label_keywords[keyword], l_dp);
			return (FALSE);
		    }
		    for (j = 0; j <= max_classification; j++)
			if (l_long_classification[j]) {
			    if (0 == strcmp(l_dp, l_long_classification[j]))
				break;
			    if (0 == strcmp(l_dp, l_short_classification[j]))
				break;
			    if (l_alternate_classification[j] &&
				0 == strcmp(l_dp,l_alternate_classification[j]))
				break;
			}
		    if (j <= max_classification)	/* if a match found */
			l_tables->l_words[indx].l_w_output_max_class = j;
		    else {
			l_error(NO_LINE_NUMBER, "In %s WORDS, word \"%s\":\n\
   OUTPUT MAXIMUM CLASSIFICATION \"%s\" not found.\n", type,
			     l_tables->l_words[indx].l_w_output_name, l_dp);
			return (FALSE);	/* error if not found */
		    }

		    if (l_tables->l_words[indx].l_w_output_max_class <
					l_tables->l_words[indx].l_w_min_class) {
			l_error(NO_LINE_NUMBER, "In %s WORDS, word \"%s\":\n\
   OMAXCLASS \"%s\" is less than MINCLASS \"%s\".\n", type,
			      l_tables->l_words[indx].l_w_output_name, l_dp,
		l_long_classification[l_tables->l_words[indx].l_w_min_class]);
			return (FALSE);
		    }
		    break;

		case WCOMPARTMENTS:

		    if (!COMPARTMENT_MASK_EQUAL(
					l_tables->l_words[indx].l_w_cm_mask,
						l_0_compartments)) {
			l_error(NO_LINE_NUMBER, "In %s WORDS, word \"%s\":\n\
   Duplicate keyword \"%s %s\".\n", type,
				l_tables->l_words[indx].l_w_output_name,
				label_keywords[keyword], l_dp);
			return (FALSE);
		    }
		    if (!*l_dp)
			break;	/* accept and ignore empty keyword */

		    l_tables->l_words[indx].l_w_cm_mask = compartments;
		    compartments += COMPARTMENTS_SIZE;
		    l_tables->l_words[indx].l_w_cm_value = compartments;
		    compartments += COMPARTMENTS_SIZE;

		    if (!parse_bits(l_dp, max_allowed_comp, set_compartment,
			 (BIT_STRING *) l_tables->l_words[indx].l_w_cm_mask,
			 (BIT_STRING *) l_tables->l_words[indx].l_w_cm_value)) {
			l_error(NO_LINE_NUMBER, "In %s WORDS, word \"%s\":\n\
   Invalid COMPARTMENTS specification \"%s\".\n", type,
				l_tables->l_words[indx].l_w_output_name, l_dp);
			return (FALSE);
		    }
		    COMPARTMENTS_COMBINE(comps_ptr,
				       l_tables->l_words[indx].l_w_cm_mask);

		    /*
		     * Now that the compartment mask and value have been
		     * determined, use this information to determine whether
		     * this input/output word is inverse, and if so, add the
		     * inverse bit(s) of this word to l_iv_compartments, the
		     * mask of inverse compartment bits.  The initial
		     * compartment bits were previously stored in
		     * l_t2_compartments as the initial compartments were
		     * parsed.
		     * 
		     * The algorithm is to identify potentially inverse bits by
		     * XOR-ing the word's mask with its value, yielding a 1
		     * in those bits positions where the value is 0 and the
		     * mask is 1.  This result is then AND-ed with the
		     * initial bits, with the resulting 1 bits being inverse.
		     * These inverse bits (if any) are then combined into
		     * l_iv_compartments.
		     * 
		     * Once l_iv_compartments is set, set l_w_type to indicate
		     * that this word has compartment bits, and any of the
		     * compartment bits are inverse.
		     */

		    if (!output_only) {
			COMPARTMENTS_COPY(l_t_compartments,
				       l_tables->l_words[indx].l_w_cm_mask);
			COMPARTMENTS_XOR(l_t_compartments,
				      l_tables->l_words[indx].l_w_cm_value);
			/* l_t_compartments now has mask of 0 value bits */
			COMPARTMENTS_AND(l_t_compartments, l_t2_compartments);
			/*
			 * l_t_compartments now has inverse bits from this word
			 */
			COMPARTMENTS_COMBINE(l_iv_compartments,
					     l_t_compartments);

			l_tables->l_words[indx].l_w_type |= HAS_COMPARTMENTS;

			if (!COMPARTMENTS_EQUAL(l_t_compartments,
						l_0_compartments))
			    l_tables->l_words[indx].l_w_type |=
							COMPARTMENTS_INVERSE;
		    }
		    break;

		case WMARKINGS:

		    if (l_tables != l_information_label_tables
			&& l_tables != l_printer_banner_tables) {
			l_error(NO_LINE_NUMBER, "In %s WORDS, word \"%s\":\n\
   Keyword MARKINGS does not apply to %s words.\n", type,
				l_tables->l_words[indx].l_w_output_name, type);
			return (FALSE);
		    }

		    if (!MARKING_MASK_EQUAL(l_tables->l_words[indx].l_w_mk_mask,
					    l_0_markings)) {
			l_error(NO_LINE_NUMBER, "In %s WORDS, word \"%s\":\n\
   Duplicate keyword \"%s %s\".\n", type,
				l_tables->l_words[indx].l_w_output_name,
				label_keywords[keyword], l_dp);
			return (FALSE);
		    }

		    if (!*l_dp)
			break;	/* accept and ignore empty keyword */

		    l_tables->l_words[indx].l_w_mk_mask = markings;
		    markings += MARKINGS_SIZE;
		    l_tables->l_words[indx].l_w_mk_value = markings;
		    markings += MARKINGS_SIZE;

		    if (!parse_bits(l_dp, max_allowed_mark, set_marking,
			 (BIT_STRING *) l_tables->l_words[indx].l_w_mk_mask,
			 (BIT_STRING *) l_tables->l_words[indx].l_w_mk_value)) {
			l_error(NO_LINE_NUMBER, "In %s WORDS, word \"%s\":\n\
   Invalid MARKINGS specification \"%s\".\n", type,
				l_tables->l_words[indx].l_w_output_name, l_dp);
			return (FALSE);
		    }
		    MARKINGS_COMBINE(marks_ptr,
				     l_tables->l_words[indx].l_w_mk_mask);

		    /*
		     * Now that the marking mask and value have been
		     * determined, use this information to determine whether
		     * this input/output word is inverse, and if so, add the
		     * inverse bit(s) of this word to l_iv_markings, the mask
		     * of inverse marking bits.  The initial marking bits
		     * were previously stored in l_t2_markings as the initial
		     * markings were parsed.
		     * 
		     * The algorithm is to identify potentially inverse bits by
		     * XOR-ing the word's mask with its value, yielding a 1
		     * in those bits positions where the value is 0 and the
		     * mask is 1.  This result is then AND-ed with the
		     * initial bits, with the resulting 1 bits being inverse.
		     * These inverse bits (if any) are then combined into
		     * l_iv_markings.
		     */

		    if (!output_only) {
			MARKINGS_COPY(l_t_markings,
				      l_tables->l_words[indx].l_w_mk_mask);
			MARKINGS_XOR(l_t_markings,
				     l_tables->l_words[indx].l_w_mk_value);
			/* l_t_markings now has mask of 0 value bits */
			MARKINGS_AND(l_t_markings, l_t2_markings);
			/* l_t_markings now has inverse bits from this word */
			MARKINGS_COMBINE(l_iv_markings, l_t_markings);
		    }
		    break;

		case WNEEDS_PREFIX:

		    if (l_tables->l_words[indx].l_w_prefix != L_NONE) {
			l_error(NO_LINE_NUMBER, "In %s WORDS, word \"%s\":\n\
   Duplicate keyword \"%s %s\".\n", type,
				l_tables->l_words[indx].l_w_output_name,
				label_keywords[keyword], l_dp);
			return (FALSE);
		    }
		    for (j = 0; j < indx; j++) {	/* for each table entry
							 * up to this one... */
			if (0 == strcmp(l_dp,
					l_tables->l_words[j].l_w_output_name))
			    break;
			if (l_tables->l_words[j].l_w_soutput_name
			    && 0 == strcmp(l_dp,
					l_tables->l_words[j].l_w_soutput_name))
			    break;
		    }
		    if (j < indx)	/* if a match found */
			l_tables->l_words[indx].l_w_prefix = j;
		    else {	/* error if not found */
			l_error(NO_LINE_NUMBER, "In %s WORDS, word \"%s\":\n\
   PREFIX \"%s\" not found.\n", type,
				l_tables->l_words[indx].l_w_output_name, l_dp);
			return (FALSE);	/* error if not found */
		    }
		    break;

		case WNEEDS_SUFFIX:

		    if (l_tables->l_words[indx].l_w_suffix != L_NONE) {
			l_error(NO_LINE_NUMBER, "In %s WORDS, word \"%s\":\n\
   Duplicate keyword \"%s %s\".\n", type,
				l_tables->l_words[indx].l_w_output_name,
				label_keywords[keyword], l_dp);
			return (FALSE);
		    }

		    for (j = 0; j < indx; j++) {	/* for each table entry
							 * up to this one... */
			if (0 == strcmp(l_dp,
					l_tables->l_words[j].l_w_output_name))
			    break;
			if (l_tables->l_words[j].l_w_soutput_name
			    && 0 == strcmp(l_dp,
					l_tables->l_words[j].l_w_soutput_name))
			    break;
		    }
		    if (j < indx)	/* if a match found */
			l_tables->l_words[indx].l_w_suffix = j;
		    else {	/* error if not found */
			l_error(NO_LINE_NUMBER, "In %s WORDS, word \"%s\":\n\
   SUFFIX \"%s\" not found.\n", type,
				l_tables->l_words[indx].l_w_output_name, l_dp);
			return (FALSE);	/* error if not found */
		    }
		    break;

		case WIS_PREFIX:

		    if (l_tables->l_words[indx].l_w_prefix != L_NONE) {
			l_error(NO_LINE_NUMBER, "In %s WORDS, word \"%s\":\n\
   Duplicate keyword \"PREFIX\".\n",
				type, l_tables->l_words[indx].l_w_output_name);
			return (FALSE);
		    }

		    l_tables->l_words[indx].l_w_prefix = L_IS_PREFIX;
		    break;

		case WIS_SUFFIX:

		    if (l_tables->l_words[indx].l_w_suffix != L_NONE) {
			l_error(NO_LINE_NUMBER, "In %s WORDS, word \"%s\":\n\
   Duplicate keyword \"SUFFIX\".\n",
				type, l_tables->l_words[indx].l_w_output_name);
			return (FALSE);
		    }

		    l_tables->l_words[indx].l_w_suffix = L_IS_SUFFIX;
		    break;

		case WACCESS_RELATED:

		    if (l_tables != l_information_label_tables) {
			l_error(NO_LINE_NUMBER, "In %s WORDS, word \"%s\":\n\
   Keyword ACCESS RELATED does not apply to %s words.\n", type,
			     l_tables->l_words[indx].l_w_output_name, type);
			return (FALSE);
		    }

		    if (l_tables->l_words[indx].l_w_flags & ACCESS_RELATED) {
			l_error(NO_LINE_NUMBER, "In %s WORDS, word \"%s\":\n\
   Duplicate keyword \"ACCESS RELATED\".\n",
				type, l_tables->l_words[indx].l_w_output_name);
			return (FALSE);
		    }

		    l_tables->l_words[indx].l_w_flags |= ACCESS_RELATED;

		    break;

		case WFLAGS:

		    if (l_tables->l_words[indx].l_w_flags & ~ACCESS_RELATED) {
			l_error(NO_LINE_NUMBER, "In %s WORDS, word \"%s\":\n\
   Duplicate keyword \"%s %s\".\n",
				type, l_tables->l_words[indx].l_w_output_name,
				label_keywords[keyword], l_dp);
			return (FALSE);
		    }

		    /*
		     * Alignment warning can be disabled because actual
		     * dereference of pointer is done by set_bits(), which
		     * accesses l_short and then casts to char *.
		     */
		    if (!parse_bits(l_dp,
			     (unsigned int) (sizeof(short) * BITS_PER_BYTE) - 2,
				    set_bits,
			 /* LINTED: alignment */
			 (BIT_STRING *) & l_tables->l_words[indx].l_w_flags,
			 /* LINTED: alignment */
			 (BIT_STRING *) & l_tables->l_words[indx].l_w_flags)) {
			l_error(NO_LINE_NUMBER, "In %s WORDS, word \"%s\":\n\
   Invalid FLAGS specification \"%s\".\n", type,
				l_tables->l_words[indx].l_w_output_name, l_dp);
			return (FALSE);
		    }
		}
	    }
	}
    }

    if (counting) {		/* reserve space for all the l_words in this
				 * table */
	TABLES_RESERVE(struct l_word, *count_ptr);
    }
    /*
     * If this was the second pass, set l_num_entries to be the count of
     * words processed.  Then, if any words were specified, and the first
     * main (non-prefix/suffix) entry has not been found yet, check the last
     * word to see if it is the first main entry.  If not, process an error.
     * If so, save the index of the first main entry in the table.  If there
     * were no words entered, save 0 as the index of the first main entry.
     */

    else {
	l_tables->l_num_entries = *count_ptr;	/* save number of words */

	if (*count_ptr != 0) {	/* if at least one word found */
	    if (-1 == first_main_entry) {	/* if first main entry not
						 * found yet */
		if (l_tables->l_words[indx].l_w_prefix != L_IS_PREFIX
		    && l_tables->l_words[indx].l_w_suffix != L_IS_SUFFIX)
		    first_main_entry = indx;

		else {
		    l_error(line_number,
			    "No %s WORDS non-prefix/suffix words.\n", type);
		    return (FALSE);
		}
	    }
	    l_tables->l_first_main_entry = first_main_entry;

	    if (MARKINGS_EQUAL(l_tables->l_words[indx].l_w_mk_value,
			       l_0_markings))
		l_tables->l_words[indx].l_w_type |= HAS_ZERO_MARKINGS;

	    if (check_special_inverse(l_tables, indx, type) == FALSE)
		return (FALSE);

	    find_exact_alias(l_tables, first_main_entry, indx);
	} else			/* no words were entered */
	    l_tables->l_first_main_entry = 0;
    }

    return (TRUE);
}

/*
 * The internal subroutine word_index parses the text pointed to by the
 * global l_scan_ptr by looking it up in the passed l_tables, and returns the
 * index of the word in the table or -1 if the word is not found.  l_scan_ptr
 * is updated to point after the text looked up.  Note that this routine is
 * called only after the text has been processed by l_next_keyword, which
 * means that the text is assumed to be already normalized.  The algorithm
 * used here is essentially the same as that used in l_parse.
 */

#ifdef	TSOL
static int
word_index(struct l_tables *l_tables)
{
	int	i;
	char	*s = l_scan_ptr;
	int	prefix = L_NONE;
	int	suffix = L_NONE;
	int	retval;
	int	len;
	struct	l_word	*word;

	/* loop to end of line */
	while (*s != '\0') {

		/* loop through all the table words */
		for (i = 0; i < l_tables->l_num_entries; i++) {

			word = &(l_tables->l_words[i]);
			if ((word->l_w_output_name &&
			    (len = strlen(word->l_w_output_name)) &&
			    (strncmp(s, word->l_w_output_name, len) == 0) &&
			    (s[len] == ' ' || s[len] == '\0')) ||
			    (word->l_w_soutput_name &&
			    (len = strlen(word->l_w_soutput_name)) &&
			    (strncmp(s, word->l_w_soutput_name, len) == 0) &&
			    (s[len] == ' ' || s[len] == '\0'))) {

				/* Match found skip over it for next loop */
				s += len;
				if (*s == ' ')
					s++;
				if (word->l_w_prefix == L_IS_PREFIX) {
					/* prefix */
					prefix = i;
					goto next;
				} else if (word->l_w_suffix != L_IS_SUFFIX) {
					/* regular word */
					if (prefix == word->l_w_prefix &&
					    word->l_w_suffix == L_NONE) {
						/* found word and we're done */
						l_scan_ptr = s;
						return (i);
					} else {
						/* must need a suffix */
						suffix = word->l_w_suffix;
						retval = i;
						goto next;
					}
				/* word is a suffix, is it ours */
				} else if (suffix == i) {
					l_scan_ptr = s;
					return (retval);
				} else {
					/* suffix, just not ours, error */
					return (-1);
				}
			}
			/* This table word doesn't match our word */
		}
		/* No table word matches our word */
		return (-1);
next:
		continue;
	}
	/* failed to find valid word */
	return (-1);
}
#else	/* !TSOL */
static int
word_index(l_tables)
    register struct l_tables *l_tables;	/* the tables to look up the text in */
{
    register int    i;
    register char  *s = l_scan_ptr;	/* fast ptr to place to scan */
    int             len_matched;
    int             prefix = L_NONE;
    int             suffix = L_NONE;
    int             return_index;	/* the index to return */

    /*
     * This is the start of the main loop to check each remaining part of the
     * string against the word table.
     */

    while (*s != '\0') {	/* while there is more left to parse... */

	/*
	 * Now, try to match the next part of the string against each word in
	 * the word table that is visible given the maximum classification
	 * and compartments.
	 */

	for (i = 0; i < l_tables->l_num_entries; i++) {	/* for each word in
							 * table */

	    /*
	     * Continue in loop (ignoring this word in table) if we are not
	     * parsing after a prefix and this word requires a prefix.
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
	     * Continue in loop (ignoring this word in table) if we are not
	     * parsing after a word that requires a suffix and this word IS a
	     * suffix.
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
	     * If this word is not to be ignored, then compare the string
	     * being parsed to this word.
	     */

	    len_matched = strlen(l_tables->l_words[i].l_w_output_name);
	    if (0 == strncmp(s, l_tables->l_words[i].l_w_output_name,
			     len_matched)
		&& (s[len_matched] == ' ' || s[len_matched] == '\0'))
		break;

	    if (l_tables->l_words[i].l_w_soutput_name) {
		len_matched = strlen(l_tables->l_words[i].l_w_soutput_name);
		if (0 == strncmp(s, l_tables->l_words[i].l_w_soutput_name,
				 len_matched)
		    && (s[len_matched] == ' ' || s[len_matched] == '\0'))
		    break;
	    }
	}

	/*
	 * Find out if string matches word in table.
	 */

	if (i < l_tables->l_num_entries) {	/* if found */
	    s += len_matched;	/* set ptr to rest to parse */
	    if (*s == ' ')
		s++;		/* skip over blank, if any */
	    l_scan_ptr = s;

	    /*
	     * If we are parsing after having found a prefix, then this entry
	     * REQUIRES this prefix, so return the index of this word.
	     */

	    if (prefix >= 0)
		return (i);

	    /*
	     * If the word matched IS a prefix, we must record that we have
	     * encountered a prefix and continue in the main loop.
	     */

	    else if (l_tables->l_words[i].l_w_prefix == L_IS_PREFIX) {
		if (suffix >= 0)
		    break;	/* no prefix allowed where suffix needed */
		prefix = i;	/* save prefix we found */
		continue;	/* back to main loop; nothing more to do for
				 * this word */
	    }
	    /*
	     * Now, if the word REQUIRES a prefix, and we didn't have one or
	     * had the wrong one, we must process the error.
	     */

	    else if (l_tables->l_words[i].l_w_prefix >= 0)
	    	/* if proper prefix didn't precede */

		break;		/* error return */

	    /*
	     * If we have previously encountered a word that requires a
	     * suffix, see if we have found it now.  If not, process an
	     * error.
	     */

	    if (suffix >= 0) {	/* if we need a suffix */
		if (i != suffix)/* if needed suffix not found */
		    break;	/* error return */
	    }
	    /*
	     * If the word matched IS a suffix, then make an error return
	     * because we are not expecting a suffix at this point.
	     */

	    else if (l_tables->l_words[i].l_w_suffix == L_IS_SUFFIX)
		break;		/* error return */

	    /*
	     * If this word REQUIRES a suffix, we should record that fact and
	     * fall through the bottom of the parse loop to check for the
	     * suffix.
	     */

	    else if (l_tables->l_words[i].l_w_suffix >= 0) {
		/* if suffix required, save suffix we need */
		suffix = l_tables->l_words[i].l_w_suffix;
		return_index = i;	/* save index to return if suffix
					 * found */
		continue;
	    }
	    /*
	     * If this words neither is nor requires a prefix or suffix,
	     * remember its index to return.
	     */

	    else
		return_index = i;

	    /*
	     * Now that we have handled the special cases for prefixes and
	     * suffixes, this is a valid word, so return its index.
	     */

	    return (return_index);
	} else
	    break;		/* error, leave loop if word not found */
    }
    return (-1);
}
#endif	/* TSOL */

/*
 * The internal subroutine do_combinations converts the REQUIRED COMBINATIONS
 * and COMBINATION CONSTRAINTS for each type of label.  It is passed a string
 * with the name of the section being handled, a pointer to the appropriate
 * count of required combinations handled to update, the appropriate
 * l_tables, and the keyword list for the NEXT section, which acts as the end
 * of the COMBINATION CONSTRAINTS for this section.
 * 
 * TRUE is returned if everything converted OK; otherwise FALSE is returned
 * after an appropriate error message is printed.
 */

static int
do_combinations(type, rc_count_ptr, c_count_ptr, c_size_ptr, c_words,
		l_tables, next_section)
    char           *type;	/* the type of label being processed */
    int            *rc_count_ptr;	/* ptr to count of number of required
					 * combinations */
    int            *c_count_ptr;/* ptr to count of number of constraints */
    int            *c_size_ptr;	/* ptr to count of number of constraints
				 * words */
    short          *c_words;	/* ptr to constraints words */
    register struct l_tables *l_tables;	/* the l_tables for that type of
					 * label */
    char           *next_section[];	/* keyword list for next section
					 * delimiter */
{
    int             i, j;
    struct l_word_pair *wp;	/* ptr to a word pair in required
				 * combinations array */
    struct l_constraints *csp;	/* ptr to a constraint structure */
    int             keyword;
    register char  *cp;
    short           word;

    /*
     * The next part of the file must be the REQUIRED COMBINATIONS keyword.
     * Return an error if not.  The end of the REQUIRED COMBINATIONS section
     * comes when the COMBINATION CONSTRAINTS keyword is found.
     */

    if (0 > l_next_keyword(required_combinations)) {
	l_error(line_number,
		"Can't find %s REQUIRED COMBINATIONS specification.\n\
   Found instead: \"%s\".\n", type, l_scan_ptr);
	return (FALSE);
    }
    if (counting)
	*rc_count_ptr = 0;	/* initialize count of required combinations */
    else
	wp = l_tables->l_required_combinations;	/* initialize ptr to required
						 * combinations array */

    while (-1 == (keyword = l_next_keyword(combination_constraints))) {
	if (counting) {
	    (*rc_count_ptr)++;	/* increment count of required combinations */
	    l_scan_ptr += strlen(l_scan_ptr);	/* start next scan w/ next
						 * line */
	} else {
	    if (-1 == (i = word_index(l_tables))
		|| -1 == (j = word_index(l_tables))
		|| *l_scan_ptr) {
		l_error(line_number,
			"Unrecognized %s REQUIRED COMBINATION \"%s\".\n",
			type, l_buffer);
		return (FALSE);
	    }
	    wp->l_word1 = i;
	    wp->l_word2 = j;
	    wp++;
	}
    }

    if (counting) {
	TABLES_RESERVE(struct l_word_pair, *rc_count_ptr);
    }
    /*
     * At this point we know the COMBINATION CONSTRAINTS keyword has been
     * found if keyword is 0.  If not, print an error message and return.
     * The end of this section comes when the next_section keyword is found.
     */

    if (keyword != 0) {
	l_error(line_number,
		"Can't find %s COMBINATION CONSTRAINTS specification.\n\
   Found instead: \"%s\".\n",
		type, l_scan_ptr);
	return (FALSE);
    }
    if (counting) {
	*c_count_ptr = 0;	/* initialize count of required combinations */
	*c_size_ptr = 0;	/* initialize count of required combinations
				 * words */
    } else {
	csp = l_tables->l_constraints;	/* initialize ptr to constraints
					 * array */
    }

    while (-1 == (keyword = l_next_keyword(next_section))) {

	/*
	 * Counting pass processing for each line.  Loop through the line and
	 * its continuation lines (if any), counting the number of words on
	 * the line.  Once the number of words are determined, the amount of
	 * space needed to store the l_constraint structure and the (variable
	 * number of) word indexes that follow it is determined and reserved
	 * in the tables.
	 * 
	 * A constraint can take any of the forms:
	 * 
	 *	words1 ! words2
	 *	words1 & words2
	 *	words1 &
	 * 
	 * where words1 and words2 are either a single word or multiple words
	 * separated by a | character.  The first form means that none of the
	 * words in words1 can be combined with any of the words in words2.
	 * The second form means that any of the words in words1 can be
	 * combined only with words in words2.  The third form means that any
	 * of the words in words1 cannot be combined with any other words.
	 * 
	 * A line containing a constraint can be continued onto the next line by
	 * placing a \ at the end of each line to be continued.  The \ cannot
	 * fall arbitrarily in the line; it must fall between a word and a
	 * delimiter (!, &, or |), not within a word.
	 */

	if (counting) {
	    (*c_count_ptr)++;	/* increment number of constraints */
	    (*c_size_ptr)++;	/* increment number of words */
	    for (;;) {		/* loop through line and any continuation
				 * lines */
		for (cp = l_scan_ptr; *cp; cp++)
		    switch (*cp) {
		    case '|':
		    case '!':
			(*c_size_ptr)++;
			break;

		    case '&':
			if (*++cp == ' ')
			    cp++;	/* skip over blank if any */
			if (*cp)
			    (*c_size_ptr)++;
			else
			    cp--;
			break;
		    }

		if (*--cp == '\\') {	/* if line is continued */
		    l_scan_ptr = cp + 1;	/* this causes l_next_keyword
						 * to read new line */
		    if (-1 != (keyword = l_next_keyword(next_section))) {
			l_error(line_number, "In %s COMBINATION CONSTRAINTS:\n\
   Keyword \"%s\" cannot start a continuation line.\n",
				type, next_section[0]);
			return (FALSE);
		    }
		    continue;	/* continue to process the new line read */
		}
		break;
	    }

	    l_scan_ptr += strlen(l_scan_ptr);	/* start next scan w/ next
						 * line */
	}
	/*
	 * Conversion pass processing for each line.  Loop through the line
	 * and its continuation lines parsing a single combination
	 * constraint.
	 */

	else {			/* not counting...parse specification of
				 * constraints */
	    short           first_half = TRUE;

	    csp->l_c_first_list = c_words;

	    line_continues = FALSE;
	    /* get ptr to last char */
	    cp = l_scan_ptr + strlen(l_scan_ptr) - 1;
	    if (*cp == '\\') {	/* if line continues */
		line_continues = TRUE;
		*cp = '\0';	/* terminate before \ */
	    }
	    for (;;) {
		if (*l_scan_ptr == ' ')
		    l_scan_ptr++;
		word = word_index(l_tables);
		if (word == -1) {
		    l_error(line_number,
  "Missing or unrecognized word in %s COMBINATION CONSTRAINTS\n   \"%s\".\n",
			    type, l_buffer);
		    return (FALSE);
		}

		*c_words++ = word;

		check_continuation(next_section);

		switch (*l_scan_ptr) {
		case '|':

		    l_scan_ptr++;
		    check_continuation(next_section);
		    continue;

		case '\0':

		    if (first_half) {
			l_error(line_number,
		"Missing ! or & in %s COMBINATION CONSTRAINTS\n   \"%s\".\n",
				type, l_buffer);
			return (FALSE);
		    } else {	/* valid end of list */
			csp->l_c_end_second_list = c_words;
			break;
		    }

		default:

		    if (*l_scan_ptr != '&' && *l_scan_ptr != '!') {
			l_error(line_number,
	"Missing |, !, or & in %s COMBINATION CONSTRAINTS\n   \"%s\".\n",
				type, l_buffer);
			return (FALSE);
		    }

		    csp->l_c_type = (*l_scan_ptr == '!') ? NOT_WITH : ONLY_WITH;

		    if (first_half) {
			first_half = FALSE;
			csp->l_c_second_list = c_words;
		    } else {	/* multiple & or ! */
			l_error(line_number,
	"Multiple &'s and/or !'s in %s COMBINATION CONSTRAINTS\n   \"%s\".\n",
				type, l_buffer);
			return (FALSE);
		    }

		    if (*l_scan_ptr++ == '&') {
			if (*l_scan_ptr == ' ')
			    l_scan_ptr++;	/* skip blank, if any */
			if (!line_continues && *l_scan_ptr == '\0') {
			    csp->l_c_end_second_list = c_words;
			    break;
			}
		    }
		    check_continuation(next_section);

		    continue;
		}
		break;		/* out of for loop if switch broken out of */
	    }
	    csp++;		/* increment index of each constraint */
	}
    }

    if (counting) {
	TABLES_RESERVE(struct l_constraints, *c_count_ptr);
	TABLES_RESERVE(short, *c_size_ptr);
    }
    return (TRUE);
}

/*
 * The internal subroutine compute_max_length computes the maximum length of
 * a human-readable label converted using the passed l_tables, and fills this
 * value into the l_max_length in the passed l_tables.
 * 
 * The maximum length is the maximum classification length, plus the sum of the
 * lengths of each word in l_tables with their prefix or suffix, plus spaces
 * in between.  compute_max_length performs a worst case length computation,
 * because hierarchies and combination constraints that could shorten the
 * longest label are ignored.
 */

static void
compute_max_length(l_tables)
    register struct l_tables *l_tables;
{
    register int    i;

    int             prefix = L_NONE;	/* indicates output of words after a
					 * prefix */
    int             suffix = L_NONE;	/* indicates suffix must be output */

    /*
     * First count longest classification and space for \0 at end.
     */

    l_tables->l_max_length = max_classification_length + 1;

    /*
     * Loop through each entry in the l_words table to add the size of this
     * word (plus its prefix or suffix if necessary) to the l_max_length.
     */

    for (i = l_tables->l_first_main_entry; i < l_tables->l_num_entries; i++) {

	/*
	 * Ignore words if they are prefixes or suffixes themselves.
	 */

	if (l_tables->l_words[i].l_w_prefix == L_IS_PREFIX)
	    continue;
	if (l_tables->l_words[i].l_w_suffix == L_IS_SUFFIX)
	    continue;

	/*
	 * If the previous word required a suffix, and this word does not
	 * required the same one, then add the length of the previous word's
	 * suffix to the length.
	 */

	if (suffix >= 0 && suffix != l_tables->l_words[i].l_w_suffix)
	    l_tables->l_max_length +=
		strlen(l_tables->l_words[suffix].l_w_output_name) + 1;

	/*
	 * Now, remember the suffix this word needs (if any) for later usage
	 * (above).
	 */

	suffix = l_tables->l_words[i].l_w_suffix;	/* will be L_NONE or
							 * index of suffix in
							 * word table */
	/*
	 * Now, if this word requires a prefix, add the prefix length unless
	 * the prefix was output for a previous word.
	 */

	if (l_tables->l_words[i].l_w_prefix >= 0
	    && prefix != l_tables->l_words[i].l_w_prefix)
	    l_tables->l_max_length +=
      strlen(l_tables->l_words[l_tables->l_words[i].l_w_prefix].l_w_output_name)
		+ 1;

	/*
	 * Now, remember the prefix this word needs (if any) for later usage
	 * (above).
	 */

	prefix = l_tables->l_words[i].l_w_prefix;	/* will be L_NONE or
							 * index of prefix in
							 * word table */

	/*
	 * Now, add the length of this word itself.
	 */

	l_tables->l_max_length +=
			       strlen(l_tables->l_words[i].l_w_output_name) + 1;
    }
}

/*
 * The internal subroutine check_default_words checks each word in the passed
 * l_tables to make sure that any word containing default bits for a given
 * classification 1) contains ONLY default bits, and 2) has a min_class <=
 * the classification for which the word is default, if its omin_class is >=
 * the classification for which the word is default (in other words, if the
 * word is default for a classification and output for that classification,
 * its minclass must not be greater than that classification).  If a bad word
 * is found, an error message is printed, and FALSE is returned.  TRUE is
 * returned if all words are OK.
 */

static int
check_default_words(l_tables, type)
    register struct l_tables *l_tables;	/* l_tables for type of label being
					 * processed */
    char           *type;	/* the type of label being handled, for error
				 * messages */
{
    register int    i;		/* word index */
    register int    j;		/* classification index */

    /*
     * For each valid classification, compute the mask of default bits. To
     * compute these default compartments and markings, we take the initial
     * ones and turn OFF those initial ones that are inverse.  The inverse
     * ones are turned off by 1) turning them on, then XOR-ing with
     * themselves to force them off.
     */

    for (j = 0; j <= l_hi_sensitivity_label->l_classification; j++) {
	if (l_long_classification[j]) {	/* if a valid classification */
	    COMPARTMENTS_COPY(l_t4_compartments, l_in_compartments[j]);
	    COMPARTMENTS_COMBINE(l_t4_compartments, l_iv_compartments);
	    COMPARTMENTS_XOR(l_t4_compartments, l_iv_compartments);

	    MARKINGS_COPY(l_t4_markings, l_in_markings[j]);
	    MARKINGS_COMBINE(l_t4_markings, l_iv_markings);
	    MARKINGS_XOR(l_t4_markings, l_iv_markings);

	    /*
	     * Once the masks of default bits are computed (l_t4_compartments
	     * and l_t4_markings), check each word in the table.
	     */

	    for (i = l_tables->l_first_main_entry;
		 i < l_tables->l_num_entries; i++) {
		if (COMPARTMENTS_ANY_BITS_MATCH(l_t4_compartments,
					  l_tables->l_words[i].l_w_cm_value)
		    || MARKINGS_ANY_BITS_MATCH(l_t4_markings,
				       l_tables->l_words[i].l_w_mk_value)) {
		    /*
		     * If this word has ANY default bits in it, then it must
		     * be all default bits, as tested by COMPARTMENTS_IN and
		     * MARKINGS_IN below.
		     */

		    if (COMPARTMENTS_IN(l_t4_compartments,
					l_tables->l_words[i].l_w_cm_mask,
					l_tables->l_words[i].l_w_cm_value))
			if (MARKINGS_IN(l_t4_markings,
					l_tables->l_words[i].l_w_mk_mask,
					l_tables->l_words[i].l_w_mk_value)) {
			    /*
			     * If word is all default bits, make sure its
			     * minclass is OK.
			     */

			    if (l_tables->l_words[i].l_w_output_min_class <= j
				&& l_tables->l_words[i].l_w_min_class > j) {
				l_error(NO_LINE_NUMBER,
					"In %s WORDS, word \"%s\":\n\
   Default word for %s has a greater\n\
   minimum classification (%s).\n",
				 	type,
					l_tables->l_words[i].l_w_output_name,
					l_long_classification[j],
		     l_long_classification[l_tables->l_words[i].l_w_min_class]);
				return (FALSE);
			    }
			    continue;	/* checkin next word */
			}
		    /*
		     * Control gets here if the word has some, but not all
		     * default bits.
		     */

		    l_error(NO_LINE_NUMBER, "In %s WORDS, word \"%s\":\n\
   Word contains default bits in combination with non-default bits.\n",
			    type, l_tables->l_words[i].l_w_output_name);
		    return (FALSE);
		}
	    }
	}
    }

    return (TRUE);
}

/*
 * The internal subroutine check_inverse_words checks that for each inverse
 * compartment word in l_tables1, there is a word in l_tables2 whose 1)
 * inverse compartment bits are a subset of the first wordUs inverse
 * compartment bits, 2) normal compartment bits (if any) are a subset of the
 * first wordUs normal compartment bits (if any), and 3) markings contain no
 * normal bits.  If any such bad words are found, an error message is printed
 * and FALSE is returned. Otherwise TRUE is returned.
 */

static int
check_inverse_words(l_tables1, type1, l_tables2, type2)
    struct l_tables *l_tables1;	/* l_tables for first label type */
    char           *type1;	/* the type of first label, for error
				 * messages */
    struct l_tables *l_tables2;	/* l_tables for second label type */
    char           *type2;	/* the type of second label, for error
				 * messages */
{
    register int    i1;		/* index into l_tables1 */
    register int    i2;		/* index into l_tables2 */

    for (i1 = l_tables1->l_first_main_entry;
	 i1 < l_tables1->l_num_entries; i1++) {
	if (l_tables1->l_words[i1].l_w_type &
	    		(COMPARTMENTS_INVERSE | SPECIAL_COMPARTMENTS_INVERSE)) {
	    for (i2 = l_tables2->l_first_main_entry;
		 i2 < l_tables2->l_num_entries; i2++) {
		if (l_tables2->l_words[i2].l_w_type &
		    	  (COMPARTMENTS_INVERSE | SPECIAL_COMPARTMENTS_INVERSE))
		    if (COMPARTMENT_MASK_DOMINATE(
					    l_tables1->l_words[i1].l_w_cm_mask,
					    l_tables2->l_words[i2].l_w_cm_mask))
			if (MARKINGS_EQUAL(l_tables2->l_words[i2].l_w_mk_value,
					   l_0_markings))
			    break;	/* this word is OK */
	    }

	    if (i2 == l_tables2->l_num_entries) {	/* if no appropriate
							 * word found in
							 * l_tables2 */
		l_error(NO_LINE_NUMBER, "In %s WORDS, word \"%s\":\n\
   No corresponding inverse compartment found in %s WORDS.\n",
			type1, l_tables1->l_words[i1].l_w_output_name,
			type2);
		return (FALSE);
	    }
	}
    }

    return (TRUE);
}

/*
 * The following two subroutines support the optional NAME INFORMATION LABELS
 * section.  The first, set_word_name_IL, looks up a passed name in the word
 * table passed, checking for long and short names.  If found, the
 * appropriate (long or short) name information label is set to the passed
 * value and TRUE is returned.  Else FALSE is returned.  set_word_name_IL
 * looks for every occurrence of the name, not just the first.
 */

static int
set_word_name_IL(l_tables, name, information_label)
    register struct l_tables *l_tables;	/* the l_tables to search */
    char           *name;		/* the name to search for */
    struct l_information_label *information_label;	/* the information label
							 * to set */
{
    register int    i;
    int             found_name = FALSE;

    for (i = 0; i < l_tables->l_num_entries; i++) {
	if (0 == strcmp(l_tables->l_words[i].l_w_output_name, name)) {
	    l_tables->l_words[i].l_w_ln_information_label = information_label;
	    found_name = TRUE;
	}
	if (l_tables->l_words[i].l_w_soutput_name
	    && 0 == strcmp(l_tables->l_words[i].l_w_soutput_name, name)) {
	    l_tables->l_words[i].l_w_sn_information_label = information_label;
	    found_name = TRUE;
	}
    }

    return (found_name);
}

/*
 * The subroutine set_classification_name_IL, looks up a passed name in the
 * passed classification name table, finding only the first occurrence in the
 * table.  If found, the passed name information label table is set to the
 * passed value and TRUE is returned.  Else FALSE is returned.
 */

static int
set_classification_name_IL(classification_name,
			   classification_information_label,
			   name, information_label)
    char          **classification_name;	/* the classification name
						 * table */
    struct l_information_label **classification_information_label;
    char           *name;			/* the name to search for */
    struct l_information_label *information_label;	/* the information label
							 * to set */
{
    register int    i;

    for (i = 0; i <= max_classification; i++) {	/* for each classification
						 * name */
	if (classification_name[i]
	    && 0 == strcmp(classification_name[i], name)) {
	    classification_information_label[i] = information_label;
	    return (TRUE);
	}
    }

    return (FALSE);
}

/*
 * The external subroutine l_init initializes the Label Encodings. It returns
 * 0 if all is well, -1 otherwise.  It prints an error message about, and
 * stops after, the first error found.  It's arguments specify the file name
 * of the file that contains the encodings, as well as the maximum legal
 * classification value, compartment bit number, and marking bit number
 * specifiable in the encodings.  It is serially reusable.
 */

int
l_init(filename, maximum_class, maximum_comp, maximum_mark)
    char           *filename;	/* the file that contains the encodings */
    unsigned int    maximum_class;	/* maximum classification value */
    unsigned int    maximum_comp;	/* maximum compartment number */
    unsigned int    maximum_mark;	/* maximum marking number */
{
    register int    i;
    int             keyword;
    CLASSIFICATION  value;
    char           *long_name = NULL;
    char           *short_name = NULL;
    char           *alternate_name = NULL;
    CLASSIFICATION  ar_class;
    COMPARTMENTS   *ar;
    CLASSIFICATION  dummy_class;
    COMPARTMENTS   *comps = NULL;
    MARKINGS       *marks = NULL;
    int             num_classifications;	/* the size of the various
						 * classification arrays */
    int             num_information_label_words;
    int             num_information_label_required_combinations;
    int             num_information_label_constraints;
    int             size_information_label_constraints;	/* # of words in all IL
							 * constraints */
    short          *information_label_constraint_words;	/* place to store IL
							 * constraint words */
    int             num_sensitivity_label_words;
    int             num_sensitivity_label_required_combinations;
    int             num_sensitivity_label_constraints;
    int             size_sensitivity_label_constraints;	/* # of words in all SL
							 * constraints */
    short          *sensitivity_label_constraint_words;	/* place to store SL
							 * constraint words */
    int             num_clearance_words;
    int             num_clearance_required_combinations;
    int             num_clearance_constraints;
    int             size_clearance_constraints;	/* # of words in all
						 * clearance constraints */
    short          *clearance_constraint_words;	/* place to store clearance
						 * constraint words */
    int             num_channel_words;
    int             num_printer_banner_words;
    CLASSIFICATION  min_classification;	/* min classification value given a
					 * name */
    int             name_without_IL;	/* flag sez NAME= found, but no
					 * following IL= found yet */

    /*
     * Save maximum allowed classification, compartment, and marking values
     * in global variables for usage in other functions.
     */

    if (maximum_class > INT16_MAX) {
	l_error(NO_LINE_NUMBER, "Maximum requested classification too "
	    "large.\n   "
	    "Classifications must not be greater than %d\n", INT16_MAX);
	return (-1);
    }
    max_allowed_class = maximum_class;
    max_allowed_comp = maximum_comp;
    max_allowed_mark = maximum_mark;

    /*
     * Initialize the max classification assigned a name and the min
     * classification assigned a name such that the max starts with the
     * lowest possible value and the min starts with the highest possible
     * value.  Each variable will be adjusted later as each classification
     * name is processed.
     */

    max_classification = 0;
    min_classification = max_allowed_class;

    /*
     * Call cleanup in case this isn't the first time l_init has been called.
     * Then initialize global variables.
     */

    l_cleanup();

    counting = TRUE;
    max_classification_length = 0;
    line_number = 0;
    convert_buffer = NULL;
    allocated_memory = NULL;
    l_encodings_file_ptr = NULL;
    size_tables = 0;
    size_strings = 0;
    num_information_labels = 0;
    num_input_names = 0;
    input_name = NO_MORE_INPUT_NAMES;

    /*
     * While counting is TRUE, do_combinations doesn't access these variables.
     */
    information_label_constraint_words = sensitivity_label_constraint_words =
	clearance_constraint_words = NULL;

    /*
     * Open the file with the human-readable encodings and initialize
     * variables so that l_next_keyword can read the file properly.
     */

    l_encodings_file_ptr = fopen(filename, "r");

    if (l_encodings_file_ptr == NULL) {	/* if file not found */
	l_error(NO_LINE_NUMBER, "Encodings file \"%s\" not found.\n", filename);
	return (-1);
    }
    l_buffer[0] = '\0';		/* initial condition for calling
				 * l_next_keyword */
    l_scan_ptr = l_buffer;	/* initial condition for calling
				 * l_next_keyword */

    /*
     * Initialize size_tables to account for non-variable size portions: the
     * minimum and minimum "protect as" classifications, the lo clearance and
     * sensitivity label, the hi sensitivity label, and the 5 l_tables.
     */

    TABLES_RESERVE(CLASSIFICATION, 1);
    TABLES_RESERVE(CLASSIFICATION, 1);
    TABLES_RESERVE(struct l_sensitivity_label, 1);
    TABLES_RESERVE(struct l_sensitivity_label, 1);
    TABLES_RESERVE(struct l_sensitivity_label, 1);
    TABLES_RESERVE(struct l_tables, 1);
    TABLES_RESERVE(struct l_tables, 1);
    TABLES_RESERVE(struct l_tables, 1);
    TABLES_RESERVE(struct l_tables, 1);
    TABLES_RESERVE(struct l_tables, 1);

    /*
     * Initialize num_compartments and num_markings to account for the space
     * for:
     * 
     *		l_t_compartments
     *		l_t2_compartments
     *		l_t3_compartments
     *		l_t4_compartments
     *		l_t5_compartments
     *		l_t_markings
     *		l_t2_markings
     *		l_t3_markings
     *		l_t4_markings
     *		l_t5_markings
     *		l_0_compartments
     *		l_0_markings
     *		l_lo_clearance.l_compartments
     *		l_lo_sensitivity_label.l_compartments
     *		l_hi_sensitivity_label.l_compartments
     *		l_hi_markings
     *		l_li_compartments
     *		l_li_markings
     *		l_iv_compartments
     *		l_iv_markings
     */

    num_compartments = 11;
    num_markings = 9;

    /*
     * The following loop is used to scan through the encodings file two
     * times. The first time, indicated by the flag counting being TRUE, we
     * scan through the encodings, counting size of all character strings in
     * size_strings and counting the size of variable-length tables and
     * increasing size_tables accordingly.  The second time through the
     * values are converted from the file and stored in the areas allocated
     * after the first pass. Other than parsing the CLASSIFICATIONS section
     * of the encodings, the subroutines above do most of the work.
     */

    for (;;) {

	/*
	 * THE VERSION SECTION.
	 */

	/*
	 * The beginning of the file must be the version specification.
	 * Return an error if not.
	 */

	keyword = l_next_keyword(version);

	if (keyword < 0) {
	    l_error(line_number, "Can't find VERSION specification.\n\
   Found instead: \"%s\".\n", l_scan_ptr);
	    return (-1);
	}
	/*
	 * Process the VERSION= keyword depending on which pass.  For the
	 * counting pass, just remember the size of the string.  For the
	 * conversion pass, copy the string into space allocated to hold it,
	 * and set l_version to point to the string.
	 */

	if (counting)		/* account in size_strings for size of the
				 * version string */
	    size_strings += strlen(l_dp) + 1;	/* account for size of string */
	else {
	    l_version = strings;/* save pointer to version string */
	    while (*strings++ = *l_dp++)
		continue;	/* copy version to strings */
	}

	/*
	 * THE CLASSIFICATIONS SECTION.
	 */

	/*
	 * The next part of the file must be the CLASSIFICATIONS keyword.
	 * Return an error if not.
	 */

	keyword = l_next_keyword(classifications);

	if (keyword < 0) {
	    l_error(line_number, "Can't find CLASSIFICATIONS specification.\n\
   Found instead: \"%s\".\n", l_scan_ptr);
	    return (-1);
	}
	/*
	 * Loop through each CLASSIFICATIONS keyword (for each pass),
	 * computing the max_classification_length, the min_ and max_
	 * classification values, and converting the encodings into the
	 * CLASSIFICATION arrays l_long_classification,
	 * l_short_classification, l_alternate_classification,
	 * l_in_compartments, and l_in_markings.
	 */

	while (0 <= (keyword = l_next_keyword(class_keywords))) {
	    if (counting) {
		switch (keyword) {
		case CNAME:
		    num_compartments++;
		    num_markings++;
		    /* control intended to fall through to CSNAME case */
#ifdef	TSOL
/*FALLTHROUGH*/
#endif	/* TSOL */
		case CSNAME:
		    max_classification_length =
#ifndef	TSOL
			L_MAX(max_classification_length, strlen(l_dp));
#else	/* TSOL */
			L_MAX(max_classification_length, (int) strlen(l_dp));
#endif	/* !TSOL */
		    size_strings += strlen(l_dp) + 1;	/* account for size of
							 * string */
		    break;

		case CANAME:
		    max_classification_length =
#ifndef	TSOL
			L_MAX(max_classification_length, strlen(l_dp));
#else	/* TSOL */
			L_MAX(max_classification_length, (int) strlen(l_dp));
#endif	/* !TSOL */
		    size_strings += strlen(l_dp) + 1;	/* account for size of
							 * string */
		    break;

		case CVALUE:
		    value = (CLASSIFICATION) strtol(l_dp, &l_dp, 10);
		    if (*l_dp) {/* error if other than decimal */
			l_error(line_number,
	"Invalid characters in CLASSIFICATION value specification \"%s\".\n",
				l_dp);
			return (-1);
		    }
#ifdef	TSOL
			if ((unsigned)value > max_allowed_class) {
			    l_error(line_number,
				"Classification has an invalid VALUE: \"%d\" "
				"(max is %d).\n", value, max_allowed_class);
			    return (-1);
			}
#endif	/* TSOL */
		    min_classification = L_MIN(min_classification, value);
		    max_classification = L_MAX(max_classification, value);
		    break;
		}
	    } else {		/* not counting...convert and store values */
		if (!long_name && keyword != CNAME) {	/* first keyword must be
							 * NAME */
		    l_error(line_number,
		     "The first keyword after CLASSIFICATIONS must be NAME.\n");
		    return (-1);
		}
		switch (keyword) {
		case CNAME:
		    if (long_name) {	/* if this is NOT first NAME */
			if (!short_name) {
			    l_error(line_number,
			      "Classification \"%s\" does not have an SNAME.\n",
				    long_name);
			    return (-1);
			}
			if (value == -1) {
			    l_error(line_number,
			       "Classification \"%s\" does not have a VALUE.\n",
				    long_name);
			    return (-1);
			}
#ifndef	TSOL
			if ((unsigned) value > max_allowed_class) {
			    l_error(line_number,
	"Classification \"%s\" has an invalid VALUE: \"%d\" (max is %d).\n",
				    long_name, value, max_allowed_class);
			    return (-1);
			}
#endif	/* !TSOL */
			l_long_classification[value] = long_name;
			l_short_classification[value] = short_name;
			l_alternate_classification[value] = alternate_name;
			l_in_compartments[value] = comps;
			l_in_markings[value] = marks;
		    }

		    short_name = (char *) 0;
		    alternate_name = (char *) 0;
		    value = -1;

		    comps = compartments;
		    compartments += COMPARTMENTS_SIZE;
		    marks = markings;
		    markings += MARKINGS_SIZE;

		    long_name = strings;
		    while (*strings++ = *l_dp++)
			continue;	/* copy class to strings */
#ifdef	TSOL
		    if (strpbrk(long_name, "/,") != NULL) {
			l_error(line_number,
				"Illegal '/' or ',' in NAME \"%s\".\n",
				long_name);
			return (-1);
		    }
#endif	/* TSOL */
		    break;

		case CSNAME:
		    short_name = strings;
		    while (*strings++ = *l_dp++)
			continue;	/* copy class to strings */
#ifdef	TSOL
		    if (strpbrk(short_name, "/,") != NULL) {
			l_error(line_number,
			        "Illegal '/' or ',' in SNAME \"%s\".\n",
				short_name);
			return (-1);
		    }
#endif	/* TSOL */
		    break;

		case CANAME:
		    alternate_name = strings;
		    while (*strings++ = *l_dp++)
			continue;	/* copy class to strings */
#ifdef	TSOL
		    if (strpbrk(alternate_name, "/,") != NULL) {
			l_error(line_number,
			        "Illegal '/' or ',' in ANAME \"%s\".\n",
				alternate_name);
			return (-1);
		    }
#endif	/* TSOL */
		    break;

		case CVALUE:
#ifdef	TSOL
			/*
			 * This claim of first pass checking is a lie.  As is
			 * the check above in CNAME.  This only works for
			 * n-1 of the CLASSIFICATION definitions.  See TSOL
			 * fixes in first pass.
			 */
#endif	/* TSOL */
		    /* first pass did error check */
		    value = (CLASSIFICATION) strtol(l_dp, &l_dp, 10);
		    break;

		case INITIAL_COMPARTMENTS:
		    if (!parse_bits(l_dp, max_allowed_comp, set_compartment,
				    (BIT_STRING *) l_t_compartments,
				    (BIT_STRING *) comps)) {
			l_error(line_number, "In CLASSIFICATION \"%s\":\n\
   Invalid INITIAL COMPARTMENTS specification \"%s\".\n", long_name, l_dp);
			return (-1);
		    }
		    COMPARTMENTS_COMBINE(l_hi_sensitivity_label->l_compartments,
					 comps);
		    /* add initial compartments to maximum SL */
		    COMPARTMENTS_COMBINE(l_t2_compartments, comps);
		    /*
		     * add initial compartments to l_t2_compartments, which
		     * is used to store all initial compartments for all
		     * classifications
		     */
		    break;

		case INITIAL_MARKINGS:
		    if (!parse_bits(l_dp, max_allowed_mark, set_marking,
				    (BIT_STRING *) l_t_markings,
				    (BIT_STRING *) marks)) {
			l_error(line_number, "In CLASSIFICATION \"%s\":\n\
   Invalid INITIAL MARKINGS specification \"%s\".\n", long_name, l_dp);
			return (-1);
		    }
		    MARKINGS_COMBINE(l_hi_markings, marks);
		    /* add initial markings to maximum IL markings */
		    MARKINGS_COMBINE(l_t2_markings, marks);
		    /*
		     * add initial markings to l_t2_markings, which is used
		     * to store all initial markings for all classifications
		     */
		    break;
		}
	    }
	}

	/*
	 * Now that all CLASSIFICATIONS keywords have been processed,
	 * determine how to adjust size_tables for CLASSIFICATIONS if
	 * counting pass, or store the last classification if the conversion
	 * pass.
	 */

	if (counting) {
	    			/* convert to amount of space/entry needed */
	    num_classifications = max_classification + 1;

	    TABLES_RESERVE(char *, num_classifications);    /* l_long_class */
	    TABLES_RESERVE(char *, num_classifications);    /* l_short_class */
	    TABLES_RESERVE(char *, num_classifications); /* l_alternate_class */
	    				/* l_lc_name_information_label */
	    TABLES_RESERVE(struct l_information_label *, num_classifications);
	    				/* l_sc_name_information_label */
	    TABLES_RESERVE(struct l_information_label *, num_classifications);
	    				/* l_ac_name_information_label */
	    TABLES_RESERVE(struct l_information_label *, num_classifications);
	    						/* l_in_compartments */
	    TABLES_RESERVE(COMPARTMENTS *, num_classifications);
	    						/* l_in_markings */
	    TABLES_RESERVE(MARKINGS *, num_classifications);
	    					/* l_accreditation_range */
	    TABLES_RESERVE(struct l_accreditation_range, num_classifications);
	} else {
	    /* store last classification */
	    l_long_classification[value] = long_name;
	    l_short_classification[value] = short_name;
	    l_alternate_classification[value] = alternate_name;
	    l_in_compartments[value] = comps;
	    l_in_markings[value] = marks;
	}

	/*
	 * Process an error if no classifications were encoded.
	 */

	if (max_classification_length == 0) {	/* no CLASSIFICATIONS encoded */
	    l_error(line_number,
		    "Can't find any CLASSIFICATIONS NAME specification.\n\
   Found instead: \"%s\".\n", l_scan_ptr);
	    return (-1);
	}
	/*
	 * THE INFORMATION LABELS SECTION.
	 */

	/*
	 * The next part of the file must be the INFORMATION LABELS keyword.
	 * Return an error if not.
	 */

	if (0 > l_next_keyword(information_labels)) {
	    l_error(line_number,
		    "Can't find INFORMATION LABELS specification.\n\
   Found instead: \"%s\".\n", l_scan_ptr);
	    return (-1);
	}
	/*
	 * Call do_words to process the INFORMATION LABEL WORDS.
	 * l_t3_compartments is used to save the compartment bits referenced
	 * by the information label words.  If do_words is successful during
	 * the conversion pass, the system high compartments
	 * (l_hi_sensitivity_label->l_compartments) must be updated to
	 * reflect these bits. l_t3_markings is used to save the marking bits
	 * referenced by the information label words. If do_words is
	 * successful during the conversion pass, the system high markings
	 * (l_hi_markings) must be updated to reflect these bits.
	 */

	if (!do_words("INFORMATION LABELS", &num_information_label_words,
		      l_t3_compartments, l_t3_markings,
		      l_information_label_tables, INPUT_OUTPUT))
	    return (-1);

	if (!counting) {
	    COMPARTMENTS_COMBINE(l_hi_sensitivity_label->l_compartments,
				 l_t3_compartments);
	    MARKINGS_COMBINE(l_hi_markings,
			     l_t3_markings);
	}
	/*
	 * Call do_combinations to process the INFORMATION LABEL REQUIRED and
	 * INVALID COMBINATIONS.
	 */
	if (!do_combinations("INFORMATION LABELS",
			     &num_information_label_required_combinations,
			     &num_information_label_constraints,
			     &size_information_label_constraints,
			     information_label_constraint_words,
			     l_information_label_tables,
			     sensitivity_labels))
	    return (-1);

	/*
	 * THE SENSITIVITY LABELS SECTION.
	 */

	/*
	 * At this point we know the SENSITIVITY LABELS keyword has been
	 * found.  Call do_words to handle the WORDS keywords.
	 * l_t4_compartments is used to save the compartment bits referenced
	 * by the sensitivity label words.  If do_words is successful during
	 * the conversion pass, these compartment bits must equal those
	 * specified for information labels.  If not, print an error.
	 * l_t4_markings should not be changed, because sensitivity labels
	 * cannot have markings.
	 */

	if (!do_words("SENSITIVITY LABELS", &num_sensitivity_label_words,
		      l_t4_compartments, l_t4_markings,
		      l_sensitivity_label_tables, INPUT_OUTPUT))
	    return (-1);

	if (!counting) {
	    if (!COMPARTMENTS_EQUAL(l_t3_compartments, l_t4_compartments)) {
		l_error(NO_LINE_NUMBER,
	"The compartment bits specified for sensitivity labels do not equal\n\
   those specified for information labels.\n");
		return (-1);
	    }
	    COMPARTMENTS_ZERO(l_t4_compartments);	/* for usage below */
	}
	/*
	 * Call do_combinations to process the SENSITIVITY LABEL REQUIRED and
	 * INVALID COMBINATIONS.
	 */

	if (!do_combinations("SENSITIVITY LABELS",
			     &num_sensitivity_label_required_combinations,
			     &num_sensitivity_label_constraints,
			     &size_sensitivity_label_constraints,
			     sensitivity_label_constraint_words,
			     l_sensitivity_label_tables,
			     clearances))
	    return (-1);

	/*
	 * THE CLEARANCES SECTION.
	 */

	/*
	 * At this point we know the CLEARANCES keyword has been found.  Call
	 * do_words to handle the WORDS keywords.  l_t4_compartments is used
	 * to save the compartment bits referenced by the clearance words.
	 * If do_words is successful during the conversion pass, these
	 * compartment bits must equal those specified for information and
	 * sensitivity labels.  If not, print an error.   l_t4_markings
	 * should not be changed, because clearances cannot have markings.
	 */

	if (!do_words("CLEARANCES", &num_clearance_words,
		      l_t4_compartments, l_t4_markings,
		      l_clearance_tables, INPUT_OUTPUT))
	    return (-1);

	if (!counting
	    && !COMPARTMENTS_EQUAL(l_t3_compartments, l_t4_compartments)) {
	    l_error(NO_LINE_NUMBER,
		  "The compartment bits specified for clearances do not equal\n\
   those specified for information labels and sensitivity labels.\n");
	    return (-1);
	}
	/*
	 * Call do_combinations to process the CLEARANCES REQUIRED and
	 * INVALID COMBINATIONS.
	 */

	if (!do_combinations("CLEARANCES",
			     &num_clearance_required_combinations,
			     &num_clearance_constraints,
			     &size_clearance_constraints,
			     clearance_constraint_words,
			     l_clearance_tables,
			     channels))
	    return (-1);

	/*
	 * If this is not the counting pass, then all of the input/output
	 * word tables (information label, sensitivity label, and clearance)
	 * are completely processed.  Therefore, the bit inverse compartment
	 * and marking bits (l_iv_compartments and l_iv_markings) have now
	 * been computed by do_words. It is now possible to compute the
	 * lowest IL and make sure it and the maximum sensitivity label are
	 * well formed.  If not, an error is produced and the encodings are
	 * rejected.
	 */

	if (!counting) {

	    /*
	     * To compute the minimum IL compartments and markings
	     * (l_li_compartments and l_li_markings), we need to know which
	     * of the initial compartments and markings for the lowest
	     * classification are default.  Default compartments and markings
	     * are those initial ones that are NOT inverse.  To compute these
	     * default compartments and markings, we take the initial ones
	     * and turn OFF those initial ones that are inverse.  The inverse
	     * ones are turned off by 1) turning them on, then XOR-ing with
	     * themselves to force them off.
	     */

	    COMPARTMENTS_COPY(l_li_compartments,
	    		      l_in_compartments[*l_min_classification]);
	    COMPARTMENTS_COMBINE(l_li_compartments, l_iv_compartments);
	    COMPARTMENTS_XOR(l_li_compartments, l_iv_compartments);

	    MARKINGS_COPY(l_li_markings, l_in_markings[*l_min_classification]);
	    MARKINGS_COMBINE(l_li_markings, l_iv_markings);
	    MARKINGS_XOR(l_li_markings, l_iv_markings);

	    /*
	     * Now that l_li_compartments and l_li_markings are computed, we
	     * can make sure that the minimum IL is well formed by calling
	     * l_valid.  If not, print an error message and reject the
	     * encodings file.
	     */

	    if (!l_valid(*l_min_classification, l_li_compartments,
			 l_li_markings, l_information_label_tables,
			 ALL_ENTRIES)) {
		l_error(NO_LINE_NUMBER,
	"Minimum information label not well formed.  The initial compartments\n\
   or initial markings for \"%s\" are specified incorrectly.\n",
			l_long_classification[*l_min_classification]);
		return (-1);
	    }
	    /*
	     * Now make sure that the maximum SL is well formed by calling
	     * l_valid.  If not, print an error message and reject the
	     * encodings file.
	     */


	    if (!l_valid(l_hi_sensitivity_label->l_classification,
			 l_hi_sensitivity_label->l_compartments,
			l_in_markings[l_hi_sensitivity_label->l_classification],
			 l_sensitivity_label_tables, ALL_ENTRIES)) {
		l_error(NO_LINE_NUMBER,
			"Maximum sensitivity label not well formed.\n");
		return (-1);
	    }
	    COMPARTMENTS_ZERO(l_t4_compartments);	/* for usage below */
	}
	/*
	 * THE CHANNELS SECTION.
	 */

	/*
	 * At this point we know the CHANNELS keyword has been found.  Call
	 * do_words to handle the WORDS keywords.  l_t4_compartments is used
	 * to save the compartment bits referenced by the channels words.  If
	 * do_words is successful during the conversion pass, these
	 * compartment bits must dominate those specified for information and
	 * sensitivity labels and clearances.  If not, print an error.
	 * l_t4_markings should not be changed, because channels cannot have
	 * markings.
	 */

	if (!do_words("CHANNELS", &num_channel_words,
		      l_t4_compartments, l_t4_markings,
		      l_channel_tables, OUTPUT_ONLY))
	    return (-1);

	if (!counting
	    && !COMPARTMENTS_DOMINATE(l_t3_compartments, l_t4_compartments)) {
	    l_error(NO_LINE_NUMBER,
	   "The compartment bits specified for channels are not dominated by\n\
  those specified for information labels, sensitivity labels, and clearances.\n");
	    return (-1);
	}
	/*
	 * THE PRINTER BANNERS SECTION.
	 */

	/*
	 * The next part of the file must be the PRINTER BANNERS keyword.
	 * Return an error if not.
	 */

	if (0 > l_next_keyword(printer_banners)) {
	    l_error(line_number, "Can't find PRINTER BANNERS specification.\n\
   Found instead: \"%s\".\n", l_scan_ptr);
	    return (-1);
	}
	/*
	 * Call do_words to handle the WORDS keywords.  l_t4_compartments is
	 * used to save the compartment bits referenced by the printer banner
	 * words.  If do_words is successful during the conversion pass,
	 * these compartment bits must dominate those specified for
	 * information and sensitivity labels and clearances.  If not, print
	 * an error.  l_t4_markings is used to hold those marking bits
	 * referenced by the printer banner words.  If do_words is successful
	 * during the conversion pass, these marking bits must dominate those
	 * specified for information labels.
	 */

	if (!do_words("PRINTER BANNERS", &num_printer_banner_words,
		      l_t4_compartments,
		      l_t4_markings,
		      l_printer_banner_tables, OUTPUT_ONLY))
	    return (-1);

	if (!counting) {
	    if (!COMPARTMENTS_DOMINATE(l_t3_compartments, l_t4_compartments)) {
		l_error(NO_LINE_NUMBER,
     "The compartment bits specified for printer banners are not dominated by\n\
   those specified for information labels, sensitivity labels, and clearances.\n");
		return (-1);
	    }
	    if (!MARKINGS_DOMINATE(l_t3_markings, l_t4_markings)) {
		l_error(NO_LINE_NUMBER,
	"The marking bits specified for printer banners are not dominated by\n\
   those specified for information labels.\n");
		return (-1);
	    }
	}
	/*
	 * Now that all the l_tables information is read from the encodings
	 * file, compute the l_max_length for each l_tables, and allocate
	 * some buffers based on these lengths that will be needed later.
	 */

	if (!counting) {
	    unsigned int    max_convert_buffer_length;

	    compute_max_length(l_information_label_tables);
	    compute_max_length(l_sensitivity_label_tables);
	    compute_max_length(l_clearance_tables);
	    compute_max_length(l_channel_tables);
	    compute_max_length(l_printer_banner_tables);

	    max_convert_buffer_length =
				      l_information_label_tables->l_max_length;
	    max_convert_buffer_length = L_MAX(max_convert_buffer_length,
				  l_sensitivity_label_tables->l_max_length);
	    max_convert_buffer_length = L_MAX(max_convert_buffer_length,
					  l_clearance_tables->l_max_length);

	    convert_buffer = (char *) calloc(max_convert_buffer_length, 1);
	    if (convert_buffer == NULL) {
		l_error(NO_LINE_NUMBER,
			"Can't allocate %ld bytes for checking labels.\n",
			max_convert_buffer_length);
		return (-1);
	    }
	    /*
	     * Now scan each information label, sensitivity label, and
	     * clearance word looking for 1) default words with a minclass
	     * above the classification for which the word is default and 2)
	     * words with associated default bits that ALSO have non-default
	     * bits associated.  Both such types of words are errors, so
	     * print an appropriate error message and reject the encodings
	     * file.
	     */

	    if (!check_default_words(l_information_label_tables,
	    			     "INFORMATION LABELS"))
		return (-1);

	    if (!check_default_words(l_sensitivity_label_tables,
				     "SENSITIVITY LABELS"))
		return (-1);

	    if (!check_default_words(l_clearance_tables,
				     "CLEARANCES"))
		return (-1);

	    /*
	     * Now scan each sensitivity label word, making sure that each
	     * inverse word has a corresponding information label word whose
	     * 1) inverse compartment bits are a subset of the sensitivity
	     * label wordUs inverse compartment bits, 2) normal compartment
	     * bits (if any) are a subset of the sensitivity label wordUs
	     * normal compartment bits (if any), and 3) markings contain no
	     * normal bits.  If not, print an appropriate error message and
	     * reject the encodings file.
	     */

	    if (!check_inverse_words(l_sensitivity_label_tables,
				     "SENSITIVITY LABELS",
				     l_information_label_tables,
				     "INFORMATION LABELS"))
		return (-1);

	    /*
	     * Now scan each clearance word, making sure that each inverse
	     * word has a corresponding sensitivity label word whose 1)
	     * inverse compartment bits are a subset of the clearance wordUs
	     * inverse compartment bits, 2) normal compartment bits (if any)
	     * are a subset of the clearance wordUs normal compartment bits
	     * (if any), and 3) markings contain no normal bits.  If not,
	     * print an appropriate error message and reject the encodings
	     * file.
	     */

	    if (!check_inverse_words(l_clearance_tables,
				     "CLEARANCES",
				     l_sensitivity_label_tables,
				     "SENSITIVITY LABELS"))
		return (-1);


	}
	/*
	 * The next part of the file must be the ACCREDITATION RANGE keyword.
	 * Return an error if not.
	 */

	if (0 > l_next_keyword(accreditation_range)) {
	    l_error(line_number,
		    "Can't find ACCREDITATION RANGE specification.\n\
   Found instead: \"%s\".\n", l_scan_ptr);
	    return (-1);
	}
	/*
	 * Now at least one CLASSIFICATION keyword must be present in a
	 * meaningful accreditation range.
	 */

	if (0 > l_next_keyword(classification)) {
	    l_error(line_number,
		"Can't find ACCREDITATION RANGE CLASSIFICATION specification.\n\
   Found instead: \"%s\".\n", l_scan_ptr);
	    return (-1);
	}
	/*
	 * The loop below processes the accreditation range specification for
	 * a given classification.  If the end of the accreditation range
	 * specification is another CLASSIFICATION specification, then the
	 * looping continues.  If the end of the accreditation range
	 * specification is the MINIMUM CLEARANCE specification, then the
	 * loop is broken out.
	 */

	for (;;) {
	    if (!counting) {
		for (i = 0; i <= max_classification; i++)
		    if (l_long_classification[i]) {
			if (0 == strcmp(l_dp, l_long_classification[i]))
			    break;
			if (0 == strcmp(l_dp, l_short_classification[i]))
			    break;
			if (l_alternate_classification[i] &&
			    0 == strcmp(l_dp, l_alternate_classification[i]))
			    break;
		    }
		if (i <= max_classification)	/* if a match found */
		    ar_class = i;
		else {
		    l_error(line_number,
		       "ACCREDITATION RANGE CLASSIFICATION \"%s\" not found.\n",
			    l_dp);
		    return (-1);/* error if not found */
		}
	    }
	    /*
	     * Now determine which type of accreditation range specification
	     * was made.
	     */

	    if (0 > (keyword = l_next_keyword(ar_types))) {
		l_error(line_number,
		        "ACCREDITATION RANGE specifier \"%s\" is invalid.\n",
			l_scan_ptr);
		return (-1);
	    }
	    if (!counting) {
		/* save type */
		l_accreditation_range[ar_class].l_ar_type = keyword + 1;
		/* ptr to ar */
		l_accreditation_range[ar_class].l_ar_start = compartments;
	    }
	    /*
	     * Now read the remaining lines of the file until the MINIMUM
	     * CLEARANCE or another CLASSIFICATION keyword is found.
	     */

	    while (-1 == (keyword = l_next_keyword(min_clearance))) {

		/*
		 * If counting pass, adjust num_compartments to account for
		 * each accreditation range compartments specification.
		 */

		if (counting) {
		    num_compartments++;
		} else {	/* not counting...store away the
				 * specification */

		    /*
		     * We should not have gotten any input here if ALL VALID
		     * was specified.
		     */

		    if (l_accreditation_range[ar_class].l_ar_type ==
								  L_ALL_VALID) {
			l_error(line_number,
			      "In ACCREDITATION RANGE, classification \"%s\":\n\
   No sensitivity labels allowed after ALL COMPARTMENT COMBINATIONS VALID.\n",
				l_long_classification[ar_class]);
			return (-1);
		    }
		    /*
		     * Now, call l_parse to parse the label, storing the
		     * result in the accreditation range.  If a parsing
		     * error, print message and return.
		     */

		    dummy_class = NO_LABEL;	/* l_parse is NOT changing
						 * existing label */

		    if (L_GOOD_LABEL != l_parse(l_scan_ptr,
						&dummy_class,
						compartments, l_t_markings,
						l_sensitivity_label_tables,
						*l_min_classification,
						l_li_compartments,
						max_classification,
				      l_hi_sensitivity_label->l_compartments)) {
			l_error(line_number,
			      "In ACCREDITATION RANGE, classification \"%s\":\n\
   Invalid sensitivity label \"%s\".\n",
				l_long_classification[ar_class], l_scan_ptr);
			return (-1);
		    }

		    if (dummy_class != ar_class) {
			l_error(line_number,
			      "In ACCREDITATION RANGE, classification \"%s\":\n\
   Classification in sensitivity label \"%s\" must be \"%s\".\n",
				l_long_classification[ar_class],
				l_scan_ptr, l_long_classification[ar_class]);
			return (-1);
		    }

		    (void) l_convert(convert_buffer, dummy_class,
				     l_short_classification,
				     compartments, l_t_markings,
				     l_sensitivity_label_tables,
				     NO_PARSE_TABLE, LONG_WORDS,
				     ALL_ENTRIES, FALSE,
				     NO_INFORMATION_LABEL);
		    if (0 != strcmp(convert_buffer, l_scan_ptr)) {
			l_error(line_number,
			      "In ACCREDITATION RANGE, classification \"%s\":\n\
   SENSITIVITY LABEL \"%s\" not in canonical form.\n\
   Is %s what was intended?\n", l_long_classification[ar_class],
				l_scan_ptr, convert_buffer);
			return (-1);
		    }

		    for (ar = l_accreditation_range[ar_class].l_ar_start;
			 ar != compartments;
			 ar += COMPARTMENTS_SIZE)
			 /* for each entry before this one */
			if (COMPARTMENTS_EQUAL(ar, compartments)) {
			    l_error(line_number,
			     "In ACCREDITATION RANGE, classification \"%s\":\n\
   Duplicate sensitivity label \"%s\".\n", l_long_classification[ar_class],
				    l_scan_ptr);
			    return (-1);
			}

		    compartments += COMPARTMENTS_SIZE;
		}
		l_scan_ptr += strlen(l_scan_ptr);	/* start next scan w/
							 * next line */
	    }

	    /*
	     * At this point we are done with the accreditation range for
	     * this particular classification.  If we reached EOF, produce an
	     * error message.  Otherwise set the end pointer for this
	     * accreditation range, then continue in loop if another
	     * CLASSIFICATION was found, or break out if MINIMUM CLEARANCE
	     * was found.
	     */

	    if (keyword == -2) {/* EOF */
		l_error(line_number,
			"Can't find MINIMUM CLEARANCE specification.\n\
   Found instead: \"%s\".\n", l_scan_ptr);
		return (-1);
	    }
	    if (!counting
		&& l_accreditation_range[ar_class].l_ar_type != L_ALL_VALID) {
		l_accreditation_range[ar_class].l_ar_end = compartments;
	    }

	    if (keyword == MINIMUM_CLEARANCE)
		break;		/* out of CLASSIFICATION loop */
	}

	/*
	 * At this point we know the MINIMUM CLEARANCE specification has been
	 * found. If counting, just reserve space for 1 struct
	 * l_sensitivity_label.  If the second pass, l_parse the label and
	 * store it, and set l_lo_clearance.
	 */

	if (!counting) {

	    /*
	     * Before parsing the minimum clearance, save and change the
	     * pointer to the end of the clearance constraints such that the
	     * combination constraints are NOT enforced on the minimum
	     * clearance.
	     */

	    struct l_constraints *save_constraints;

	    save_constraints = l_clearance_tables->l_end_constraints;
	    l_clearance_tables->l_end_constraints =
					     l_clearance_tables->l_constraints;

	    /* l_parse is NOT changing existing label */
	    l_lo_clearance->l_classification = NO_LABEL;

	    if (L_GOOD_LABEL != l_parse(l_dp,
					&l_lo_clearance->l_classification,
					l_lo_clearance->l_compartments,
					l_t_markings, l_clearance_tables,
					*l_min_classification,
					l_li_compartments, max_classification,
				      l_hi_sensitivity_label->l_compartments)) {
		l_error(line_number, "In ACCREDITATION RANGE:\n\
   Invalid MINIMUM CLEARANCE \"%s\".\n", l_dp);
		return (-1);
	    }
	    (void) l_convert(convert_buffer,
			     l_lo_clearance->l_classification,
			     l_short_classification,
			     l_lo_clearance->l_compartments, l_t_markings,
			     l_clearance_tables,
			     NO_PARSE_TABLE, LONG_WORDS, ALL_ENTRIES, FALSE,
			     NO_INFORMATION_LABEL);
	    if (0 != strcmp(convert_buffer, l_dp)) {
		l_error(line_number, "In ACCREDITATION RANGE:\n\
   MINIMUM CLEARANCE \"%s\" not in canonical form.\n\
   Is %s what was intended?\n", l_dp, convert_buffer);
		return (-1);
	    }
	    /*
	     * Restore the clearance combination constraints table.
	     */

	    l_clearance_tables->l_end_constraints = save_constraints;
	}
	/*
	 * Next the MINIMUM SENSITIVITY LABEL keyword must follow.  Return an
	 * error if not.
	 */

	if (0 > l_next_keyword(min_sensitivity_label)) {
	    l_error(line_number,
		    "Can't find MINIMUM SENSITIVITY LABEL specification.\n\
   Found instead: \"%s\".\n", l_scan_ptr);
	    return (-1);
	}
	/*
	 * At this point we know the MINIMUM SENSITIVITY LABEL specification
	 * has been found.  If counting, just reserve space for 1 struct
	 * l_sensitivity_label.  If the second pass, l_parse the label and
	 * store it, and set l_lo_sensitivity_label.
	 */

	if (!counting) {
	    /* l_parse is NOT changing existing label */
	    l_lo_sensitivity_label->l_classification = NO_LABEL;

	    if (L_GOOD_LABEL != l_parse(l_dp,
				     &l_lo_sensitivity_label->l_classification,
				        l_lo_sensitivity_label->l_compartments,
					l_t_markings,
					l_sensitivity_label_tables,
					*l_min_classification,
					l_li_compartments, max_classification,
				     l_hi_sensitivity_label->l_compartments)) {
		l_error(line_number, "In ACCREDITATION RANGE:\n\
   Invalid MINIMUM SENSITIVITY LABEL \"%s\".\n", l_dp);
		return (-1);
	    }

	    (void) l_convert(convert_buffer,
			     l_lo_sensitivity_label->l_classification,
			     l_short_classification,
			     l_lo_sensitivity_label->l_compartments,
			     l_t_markings,
			     l_sensitivity_label_tables,
			     NO_PARSE_TABLE, LONG_WORDS, ALL_ENTRIES, FALSE,
			     NO_INFORMATION_LABEL);
	    if (0 != strcmp(convert_buffer, l_dp)) {
		l_error(line_number, "In ACCREDITATION RANGE:\n\
   MINIMUM SENSITIVITY LABEL \"%s\" not in canonical form.\n\
   Is %s what was intended?\n", l_dp, convert_buffer);
		return (-1);
	    }
	    if (l_lo_clearance->l_classification <
					l_lo_sensitivity_label->l_classification
		|| !COMPARTMENTS_DOMINATE(l_lo_clearance->l_compartments,
				      l_lo_sensitivity_label->l_compartments)) {
		l_error(line_number, "In ACCREDITATION RANGE:\n\
   MINIMUM SENSITIVITY LABEL must be dominated by MINIMUM CLEARANCE.\n");
		return (-1);
	    }
	}
	/*
	 * Next the MINIMUM PROTECT AS CLASSIFICATION keyword must follow.
	 * Return an error if not.
	 */

	if (0 > l_next_keyword(min_protect_as_classification)) {
	    l_error(line_number,
	        "Can't find MINIMUM PROTECT AS CLASSIFICATION specification.\n\
   Found instead: \"%s\".\n", l_scan_ptr);
	    return (-1);
	}
	/*
	 * At this point we know the MINIMUM PROTECT AS CLASSIFICATION
	 * specification has been found.  If counting, just reserve space for
	 * 1 CLASSIFICATION.  If the second pass, l_parse the label and
	 * process an error if it does not parse or includes compartments or
	 * markings.  Otherwise, store the classification parsed.
	 */

	if (!counting) {
	    for (i = 0; i <= max_classification; i++)
		if (l_long_classification[i]) {
		    if (0 == strcmp(l_dp, l_long_classification[i]))
			break;
		    if (0 == strcmp(l_dp, l_short_classification[i]))
			break;
		    if (l_alternate_classification[i] &&
			0 == strcmp(l_dp, l_alternate_classification[i]))
			break;
		}
	    if (i <= max_classification) {	/* if a match found */
		if (i > l_lo_clearance->l_classification) {
		    /* if too high */
		    l_error(line_number, "In ACCREDITATION RANGE:\n\
   MINIMUM PROTECT AS CLASSIFICATION \"%s\" greater than\n\
   classification in MINIMUM CLEARANCE.\n", l_dp);
		    return (-1);
		} else
		    *l_classification_protect_as = i;
	    } else {
		l_error(line_number, "In ACCREDITATION RANGE:\n\
   Invalid MINIMUM PROTECT AS CLASSIFICATION \"%s\".\n", l_dp);
		return (-1);
	    }
	}
	/*
	 * The optional NAME INFORMATION LABELS section.
	 */

	if (0 == l_next_keyword(name_information_labels)) {
	    name_without_IL = FALSE;

	    while (0 <= (keyword =
			    l_next_keyword(name_information_labels_keywords))) {
		if (counting) {
		    switch (keyword) {
		    case NIL_NAME:	/* found NAME= keyword */
			name_without_IL = TRUE;	/* we now have a name without
						 * a corresponding IL (yet) */
			break;

		    case NIL_IL:	/* found IL= keyword */
			if (!name_without_IL) {
			    l_error(line_number, "In NAME INFORMATION LABELS:\n\
   A NAME= keyword must precede an IL= keyword.\n");
			    return (-1);
			}
			name_without_IL = FALSE;
			num_information_labels++;
			num_compartments++;
			num_markings++;
			break;
		    }
		} else {	/* not counting */
		    switch (keyword) {
		    case NIL_NAME:	/* found NAME= keyword */
			name_without_IL = TRUE;	/* we now have a name without
						 * an IL */

			if (!set_word_name_IL(l_information_label_tables, l_dp,
					      information_label) &
			    !set_word_name_IL(l_sensitivity_label_tables, l_dp,
					      information_label) &
			    !set_word_name_IL(l_clearance_tables, l_dp,
					      information_label) &
			    !set_word_name_IL(l_channel_tables, l_dp,
					      information_label) &
			    !set_word_name_IL(l_printer_banner_tables, l_dp,
					      information_label) &
			    !set_classification_name_IL(l_long_classification,
						    l_lc_name_information_label,
						        l_dp,
							information_label) &
			    !set_classification_name_IL(l_short_classification,
						    l_sc_name_information_label,
						        l_dp,
							information_label) &
			    !set_classification_name_IL(
						     l_alternate_classification,
						    l_ac_name_information_label,
						        l_dp,
							information_label)) {
			    l_error(line_number, "In NAME INFORMATION LABELS:\n\
   NAME \"%s\" not found.\n", l_dp);
			    return (-1);
			}
			break;

		    case NIL_IL:	/* found IL= keyword */
			information_label->l_compartments = compartments;
			compartments += COMPARTMENTS_SIZE;
			information_label->l_markings = markings;
			markings += MARKINGS_SIZE;

			information_label->l_classification = NO_LABEL;
			if (L_GOOD_LABEL != l_parse(l_dp,
				           &information_label->l_classification,
					      information_label->l_compartments,
					          information_label->l_markings,
						     l_information_label_tables,
						      *l_min_classification,
						      l_li_compartments,
						      max_classification,
				      l_hi_sensitivity_label->l_compartments)) {
			    l_error(line_number, "In NAME INFORMATION LABELS:\n\
   Invalid INFORMATION LABEL \"%s\".\n", l_dp);
			    return (-1);
			}
			(void) l_convert(convert_buffer,
					 information_label->l_classification,
					 l_long_classification,
					 information_label->l_compartments,
					 information_label->l_markings,
					 l_information_label_tables,
					 NO_PARSE_TABLE, LONG_WORDS,
					 ALL_ENTRIES, FALSE,
					 NO_INFORMATION_LABEL);
			if (0 != strcmp(convert_buffer, l_dp)) {
			    l_error(line_number, "In NAME INFORMATION LABELS:\n\
   INFORMATION LABEL \"%s\" not in canonical form.\n\
   Is %s what was intended?\n", l_dp, convert_buffer);
			    return (-1);
			}
			information_label++;
			break;
		    }
		}
	    }

	    if (counting) {
		if (num_information_labels > 0) {
		    TABLES_RESERVE(struct l_information_label,
				   num_information_labels);
		}
		if (name_without_IL) {
		    l_error(line_number, "In NAME INFORMATION LABELS:\n\
   A NAME= keyword must always be followed by an IL= keyword.\n");
		    return (-1);
		}
#ifndef	TSOL
		if (num_input_names > 0) {
		    TABLES_RESERVE(struct l_input_name, num_input_names);
		}
#endif	/* !TSOL */
	    }
	}
	/*
	 * Now, if counting, reserve space for all of the COMPARTMENTS and
	 * MARKINGS.
	 */

	if (counting) {
	    TABLES_RESERVE(COMPARTMENTS, num_compartments * COMPARTMENTS_SIZE);
	    TABLES_RESERVE(MARKINGS, num_markings * MARKINGS_SIZE);
#ifdef	TSOL
	    /*
	     * Reserve space for alternate input names.
	     */
	    if (num_input_names > 0) {
		TABLES_RESERVE(struct l_input_name, num_input_names);
	    }
#endif	/* TSOL */
	}
	/*
	 * At this point we should be at end of file.  Call l_eof to process
	 * the end of the file (if any).  l_eof will return an error message
	 * if end of file was not reached when expected, or if an error was
	 * found in the end of the file. If there was an error, print the
	 * error message and return with an error.
	 */

#ifndef	TSOL
	if (!l_eof(counting, line_number))
#else	/* TSOL */
	if (!l_eof(counting, &line_number))
#endif	/* !TSOL */
	    return (-1);	/* error return if not end of file */

	/*
	 * At this point we are done with the encodings file.  If we've been
	 * counting the amount of space to allocate, then allocate it,
	 * producing an error upon failure to allocate.  Allocate in chunks
	 * of L_CHUNK bytes, aligning the start of the allocated memory as
	 * indicated by L_ALIGNMENT.  The memory is is allocated in chunks to
	 * make sure enough memory can be allocated for very large encodings
	 * files.  Print an error message if the encodings file is too large
	 * to handle in L_CHUNK chunks.
	 */

	if (counting) {
	    unsigned long   long_chunks_to_allocate;
	    unsigned int    chunks_to_allocate;

	    counting = FALSE;	/* end of counting (first) pass */

	    long_chunks_to_allocate = (size_tables + size_strings +
				       L_ALIGNMENT + L_CHUNK - 1) / L_CHUNK;
	    chunks_to_allocate = (unsigned int) long_chunks_to_allocate;

	    if (long_chunks_to_allocate != (long) chunks_to_allocate
		|| NULL == (allocated_memory =
				(char *) calloc(chunks_to_allocate, L_CHUNK))) {
		l_error(NO_LINE_NUMBER,
			"Can't allocate %ld bytes for encodings.\n",
			size_tables + size_strings);
		return (-1);
	    }

	    tables = allocated_memory;	/* tables will be used to allocate
					 * within memory */
	    L_ALIGN(tables, L_ALIGNMENT);	/* align properly */

	    strings = tables + size_tables;	/* set ptr to place for
						 * strings */

	    /*
	     * Now that the space is allocated and tables and strings are set
	     * to the respective places to put tables and strings, allocate
	     * the fixed tables using the TABLES_ALLOCATE macro.
	     */

	    /* LINTED: alignment */
	    TABLES_ALLOCATE(CLASSIFICATION, l_min_classification, 1);
	    /* LINTED: alignment */
	    TABLES_ALLOCATE(CLASSIFICATION, l_classification_protect_as, 1);
	    /* LINTED: alignment */
	    TABLES_ALLOCATE(struct l_sensitivity_label, l_lo_clearance, 1);
	    /* LINTED: alignment */
	    TABLES_ALLOCATE(struct l_sensitivity_label,
			    l_lo_sensitivity_label, 1);
	    /* LINTED: alignment */
	    TABLES_ALLOCATE(struct l_sensitivity_label,
			    l_hi_sensitivity_label, 1);
	    /* LINTED: alignment */
	    TABLES_ALLOCATE(struct l_tables, l_information_label_tables, 1);
	    /* LINTED: alignment */
	    TABLES_ALLOCATE(struct l_tables, l_sensitivity_label_tables, 1);
	    /* LINTED: alignment */
	    TABLES_ALLOCATE(struct l_tables, l_clearance_tables, 1);
	    /* LINTED: alignment */
	    TABLES_ALLOCATE(struct l_tables, l_channel_tables, 1);
	    /* LINTED: alignment */
	    TABLES_ALLOCATE(struct l_tables, l_printer_banner_tables, 1);
	    /* LINTED: alignment */
	    TABLES_ALLOCATE(char *, l_long_classification, num_classifications);
	    /* LINTED: alignment */
	    TABLES_ALLOCATE(char *,
			    l_short_classification, num_classifications);
	    /* LINTED: alignment */
	    TABLES_ALLOCATE(char *,
	    		    l_alternate_classification, num_classifications);
	    /* LINTED: alignment */
	    TABLES_ALLOCATE(struct l_information_label *,
			    l_lc_name_information_label, num_classifications);
	    /* LINTED: alignment */
	    TABLES_ALLOCATE(struct l_information_label *,
			    l_sc_name_information_label, num_classifications);
	    /* LINTED: alignment */
	    TABLES_ALLOCATE(struct l_information_label *,
			    l_ac_name_information_label, num_classifications);
	    /* LINTED: alignment */
	    TABLES_ALLOCATE(COMPARTMENTS *,
			    l_in_compartments, num_classifications);
	    /* LINTED: alignment */
	    TABLES_ALLOCATE(MARKINGS *, l_in_markings, num_classifications);
	    /* LINTED: alignment */
	    TABLES_ALLOCATE(struct l_accreditation_range,
			    l_accreditation_range, num_classifications);


	    /* LINTED: alignment */
	    TABLES_ALLOCATE(struct l_word,
			    l_information_label_tables->l_words,
			    num_information_label_words);
	    /* LINTED: alignment */
	    TABLES_ALLOCATE(struct l_word_pair,
			    l_information_label_tables->l_required_combinations,
			    num_information_label_required_combinations);
	    l_information_label_tables->l_end_required_combinations =
		/* LINTED: alignment */
		(struct l_word_pair *)tables;
	    /* LINTED: alignment */
	    TABLES_ALLOCATE(struct l_constraints,
			    l_information_label_tables->l_constraints,
			    num_information_label_constraints);
	    l_information_label_tables->l_end_constraints =
		/* LINTED: alignment */
		(struct l_constraints *)tables;
	    /* LINTED: alignment */
	    TABLES_ALLOCATE(short,
			    information_label_constraint_words,
			    size_information_label_constraints);


	    /* LINTED: alignment */
	    TABLES_ALLOCATE(struct l_word,
			    l_sensitivity_label_tables->l_words,
			    num_sensitivity_label_words);
	    /* LINTED: alignment */
	    TABLES_ALLOCATE(struct l_word_pair,
	    		    l_sensitivity_label_tables->l_required_combinations,
			    num_sensitivity_label_required_combinations);
	    l_sensitivity_label_tables->l_end_required_combinations =
		/* LINTED: alignment */
		(struct l_word_pair *)tables;
	    /* LINTED: alignment */
	    TABLES_ALLOCATE(struct l_constraints,
			    l_sensitivity_label_tables->l_constraints,
			    num_sensitivity_label_constraints);
	    l_sensitivity_label_tables->l_end_constraints =
		/* LINTED: alignment */
		(struct l_constraints *)tables;
	    /* LINTED: alignment */
	    TABLES_ALLOCATE(short,
			    sensitivity_label_constraint_words,
			    size_sensitivity_label_constraints);


	    /* LINTED: alignment */
	    TABLES_ALLOCATE(struct l_word,
			    l_clearance_tables->l_words, num_clearance_words);
	    /* LINTED: alignment */
	    TABLES_ALLOCATE(struct l_word_pair,
			    l_clearance_tables->l_required_combinations,
			    num_clearance_required_combinations);
	    l_clearance_tables->l_end_required_combinations =
		/* LINTED: alignment */
		(struct l_word_pair *)tables;
	    /* LINTED: alignment */
	    TABLES_ALLOCATE(struct l_constraints,
	    		    l_clearance_tables->l_constraints,
			    num_clearance_constraints);
	    l_clearance_tables->l_end_constraints =
		/* LINTED: alignment */
		(struct l_constraints *)tables;
	    /* LINTED: alignment */
	    TABLES_ALLOCATE(short,
	    		    clearance_constraint_words,
			    size_clearance_constraints);


	    /* LINTED: alignment */
	    TABLES_ALLOCATE(struct l_word,
			    l_channel_tables->l_words,
			    num_channel_words);


	    /* LINTED: alignment */
	    TABLES_ALLOCATE(struct l_word,
			    l_printer_banner_tables->l_words,
			    num_printer_banner_words);

	    if (num_information_labels > 0) {
		/* LINTED: alignment */
		TABLES_ALLOCATE(struct l_information_label,
				information_label, num_information_labels);
	    }
	    if (num_input_names > 0) {
		/* LINTED: alignment */
		TABLES_ALLOCATE(struct l_input_name,
				input_name, num_input_names);
	    }

	    /* LINTED: alignment */
	    TABLES_ALLOCATE(COMPARTMENTS,
			    compartments, num_compartments * COMPARTMENTS_SIZE);
	    /* LINTED: alignment */
	    TABLES_ALLOCATE(MARKINGS, markings, num_markings * MARKINGS_SIZE);

	    /*
	     * Now allocate the initial portion of the compartments and
	     * markings.
	     */

	    l_t_compartments = compartments;
	    compartments += COMPARTMENTS_SIZE;
	    l_t2_compartments = compartments;
	    compartments += COMPARTMENTS_SIZE;
	    l_t3_compartments = compartments;
	    compartments += COMPARTMENTS_SIZE;
	    l_t4_compartments = compartments;
	    compartments += COMPARTMENTS_SIZE;
	    l_t5_compartments = compartments;
	    compartments += COMPARTMENTS_SIZE;
	    l_t_markings = markings;
	    markings += MARKINGS_SIZE;
	    l_t2_markings = markings;
	    markings += MARKINGS_SIZE;
	    l_t3_markings = markings;
	    markings += MARKINGS_SIZE;
	    l_t4_markings = markings;
	    markings += MARKINGS_SIZE;
	    l_t5_markings = markings;
	    markings += MARKINGS_SIZE;
	    l_0_compartments = compartments;
	    compartments += COMPARTMENTS_SIZE;
	    l_0_markings = markings;
	    markings += MARKINGS_SIZE;
	    l_lo_clearance->l_compartments = compartments;
	    compartments += COMPARTMENTS_SIZE;
	    l_lo_sensitivity_label->l_compartments = compartments;
	    compartments += COMPARTMENTS_SIZE;
	    l_hi_sensitivity_label->l_compartments = compartments;
	    compartments += COMPARTMENTS_SIZE;
	    l_hi_markings = markings;
	    markings += MARKINGS_SIZE;
	    l_li_compartments = compartments;
	    compartments += COMPARTMENTS_SIZE;
	    l_li_markings = markings;
	    markings += MARKINGS_SIZE;
	    l_iv_compartments = compartments;
	    compartments += COMPARTMENTS_SIZE;
	    l_iv_markings = markings;
	    markings += MARKINGS_SIZE;


	    /*
	     * Now that the initial portions of the tables and strings have
	     * been allocated, initialize any variables allocated above whose
	     * value is known at this time, before entering the conversion
	     * loop.
	     */

	    *l_min_classification = min_classification;
	    l_hi_sensitivity_label->l_classification = max_classification;

	    /*
	     * Now reset the seek ptr to the start of the encodings file, so
	     * that the second (conversion) pass can re-read the file.
	     */

	    /* go back to the start of the encodings file */

	    (void) fseek(l_encodings_file_ptr, 0L, 0);
	    line_number = 0;	/* reset counter of lines in encodings */
	    l_buffer[0] = '\0';	/* initial condition for calling
				 * l_next_keyword */
	    l_scan_ptr = l_buffer;	/* initial condition for calling
					 * l_next_keyword */
	} else
	    break;		/* done with main loop if end of conversion
				 * pass */
    }

    (void) free(convert_buffer);
    (void) fclose(l_encodings_file_ptr);	/* close the encodings file */

    return (0);
}
