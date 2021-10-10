/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
%{
#pragma	ident	"%Z%%M%	%I%	%E% SMI"
%}

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
/* @(#)$RCSfile: gram.y,v $ $Revision: 1.4.7.7 $ (OSF) $Date: 1992/11/20 02:37:52 $ */

/*
 * COMPONENT_NAME: (CMDLOC) Locale Database Commands
 *
 *
 * (C) COPYRIGHT International Business Machines Corp. 1991
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 * 
 * 1.10  com/cmd/nls/gram.y, cmdnls, bos320, 9137320a 9/4/91 13:41:56
 */

%{
#include <limits.h>
#include "locdef.h"
%}

/*
 * ----------------------------------------------------------------------
 * Tokens for keywords
 * ----------------------------------------------------------------------
 */
%union {
	uint64_t	llval;
	int	ival;
	char	*id;
	struct _sym {
		char	*id;
		uint64_t	ucs;
	} sym;
}

/* keywords identifying the beginning and end of a locale category */
%token KW_END
%token KW_CHARMAP
%token KW_LC_COLLATE
%token KW_LC_CTYPE
%token KW_LC_MONETARY
%token KW_LC_NUMERIC
%token KW_LC_MSG
%token KW_LC_TIME
%token KW_METHODS
%token KW_COPY
%token KW_LCBIND
%token KW_WIDTH
%token KW_WIDTH_DEFAULT

/* keywords support the LCBIND category */
%token KW_CHARCLASS
%token KW_CHARTRANS

/* keywords supporting the LC_METHODS category */
%token KW_MBLEN
%token KW_MBTOWC
%token KW_MBSTOWCS
%token KW_WCTOMB
%token KW_WCSTOMBS
%token KW_WCWIDTH
%token KW_WCSWIDTH
%token KW_MBFTOWC
%token KW_FGETWC
%token KW_TOWUPPER
%token KW_TOWLOWER
%token KW_WCTYPE
%token KW_ISWCTYPE
%token KW_STRCOLL
%token KW_STRXFRM
%token KW_WCSCOLL
%token KW_WCSXFRM
%token KW_REGCOMP
%token KW_REGEXEC
%token KW_REGFREE
%token KW_REGERROR
%token KW_STRFMON
%token KW_STRFTIME
%token KW_STRPTIME
%token KW_GETDATE
%token KW_WCSFTIME
%token KW_CSWIDTH
%token KW_EUCPCTOWC
%token KW_WCTOEUCPC
%token KW_TRWCTYPE
%token KW_TOWCTRANS
%token KW_WCTRANS
%token KW_FGETWC_AT_NATIVE
%token KW_ISWCTYPE_AT_NATIVE
%token KW_MBFTOWC_AT_NATIVE
%token KW_MBSTOWCS_AT_NATIVE
%token KW_MBTOWC_AT_NATIVE
%token KW_TOWLOWER_AT_NATIVE
%token KW_TOWUPPER_AT_NATIVE
%token KW_WCSCOLL_AT_NATIVE
%token KW_WCSTOMBS_AT_NATIVE
%token KW_WCSXFRM_AT_NATIVE
%token KW_WCTOMB_AT_NATIVE
%token KW_PROCESS_CODE
%token KW_EUC
%token KW_DENSE
%token KW_UCS4
%token KW_FILE_CODE
%token KW_UTF8
%token KW_OTHER
%token KW_WCWIDTH_AT_NATIVE
%token KW_WCSWIDTH_AT_NATIVE
%token KW_TOWCTRANS_AT_NATIVE
%token KW_BTOWC
%token KW_WCTOB
%token KW_MBSINIT
%token KW_MBRLEN
%token KW_MBRTOWC
%token KW_WCRTOMB
%token KW_MBSRTOWCS
%token KW_WCSRTOMBS
%token KW_BTOWC_AT_NATIVE
%token KW_WCTOB_AT_NATIVE
%token KW_MBRTOWC_AT_NATIVE
%token KW_WCRTOMB_AT_NATIVE
%token KW_MBSRTOWCS_AT_NATIVE
%token KW_WCSRTOMBS_AT_NATIVE

/* keywords support LC_COLLATE category */
%token KW_COLLATING_ELEMENT
%token KW_COLLATING_SYMBOL
%token KW_ORDER_START
%token KW_ORDER_END
%token KW_FORWARD
%token KW_BACKWARD
%token KW_NO_SUBSTITUTE
%token KW_POSITION
%token KW_WITH
%token KW_FROM
%token KW_FNMATCH

/* keywords supporting LC_CTYPE category */
%token KW_ELLIPSIS

/* keywords supporting the LC_MONETARY category */
%token KW_INT_CURR_SYMBOL
%token KW_CURRENCY_SYMBOL
%token KW_MON_DECIMAL_POINT
%token KW_MON_THOUSANDS_SEP
%token KW_MON_GROUPING
%token KW_POSITIVE_SIGN
%token KW_NEGATIVE_SIGN
%token KW_INT_FRAC_DIGITS
%token KW_FRAC_DIGITS
%token KW_P_CS_PRECEDES
%token KW_P_SEP_BY_SPACE
%token KW_N_CS_PRECEDES
%token KW_N_SEP_BY_SPACE
%token KW_P_SIGN_POSN
%token KW_N_SIGN_POSN
%token KW_INT_P_CS_PRECEDES
%token KW_INT_P_SEP_BY_SPACE
%token KW_INT_N_CS_PRECEDES
%token KW_INT_N_SEP_BY_SPACE
%token KW_INT_P_SIGN_POSN
%token KW_INT_N_SIGN_POSN

/* keywords supporting the LC_NUMERIC category */
%token KW_DECIMAL_POINT
%token KW_THOUSANDS_SEP
%token KW_GROUPING

/* keywords supporting the LC_TIME category */
%token KW_ABDAY
%token KW_DAY
%token KW_ABMON
%token KW_MON
%token KW_D_T_FMT
%token KW_D_FMT
%token KW_T_FMT
%token KW_AM_PM
%token KW_ERA
%token KW_ERA_YEAR
%token KW_ERA_D_FMT
%token KW_ERA_T_FMT
%token KW_ERA_D_T_FMT
%token KW_ALT_DIGITS
%token KW_T_FMT_AMPM
%token KW_DATE_FMT

/* keywords for the LC_MSG category */
%token KW_YESEXPR
%token KW_NOEXPR
%token KW_YESSTR
%token KW_NOSTR

/* tokens for meta-symbols */
%token KW_CODESET
%token KW_ESC_CHAR
%token KW_MB_CUR_MAX
%token KW_MB_CUR_MIN
%token KW_COMMENT_CHAR

/* tokens for user defined symbols, integer constants, etc... */
%token <sym> N_SYMBOL
%token <sym> U_SYMBOL
%token <id> STRING
%token <ival> HEX_CONST
%token <ival> CHAR_CONST
%token <ival> NUM
%token <llval> BYTES
%token <id> LOC_NAME
%token <id> CHAR_CLASS_SYMBOL
%token <id> CHAR_TRANS_SYMBOL

%type <id> generic_symbol

%{

int	method_class = SB_CODESET;
int	mb_cur_max;
wchar_t	max_wchar_enc;
uint64_t	max_fc_enc;

_LC_euc_info_t euc_info = {
	(char) 1, (char) 0, (char) 0, (char) 0,	/* EUC width info */
	(char) 1, (char) 0, (char) 0, (char) 0, /* screen width info */
	(wchar_t) 0, (wchar_t) 0, (wchar_t) 0,	/* cs1,cs2,cs3_base */
	(wchar_t) 0,				/* dense_end */
	(wchar_t) 0, (wchar_t) 0, (wchar_t) 0	/* cs1,cs2,cs3_adjustment */
};
static int	euc_info_has_set = 0;

int	single_layer = TRUE;	/* if TRUE we are always in dense mode */
static int	Cswidth = FALSE;
static int	set_filecode = 0;
static int	set_proccode = 0;

/* Flags for determining if the category was empty when it was defined and
   if it has been defined before */

int	lc_time_flag = 0;
int	lc_monetary_flag = 0;
int	lc_ctype_flag = 0;
int	lc_message_flag = 0;
int	lc_numeric_flag = 0;
int	lc_collate_flag = 0;

int	lc_has_collating_elements = 0;

static int	args = 1;	/* number of arguments in method assign list */

static _LC_bind_tag_t	lcbind_tag;
static char	*ctype_symbol_name;
static int	sort_mask = 0;
static int	cur_order = 0;
static int	arblen;
static int	user_defined = 0;

static int	new_extfmt = FALSE;
static int	old_extfmt = FALSE;

static int	collation_error = 0;
%}

%%
/*
 * ----------------------------------------------------------------------
 * GRAMMAR for files parsed by the localedef utility.  This grammar 
 * supports both the CHARMAP and the LOCSRC definitions.  The 
 * implementation will call yyparse() twice.  Once to parse the CHARMAP 
 * file, and once to parse the LOCSRC file.
 * ----------------------------------------------------------------------
 */

file	:	charmap
	| category_list
	| method_def
	;

category_list	:
	category_list category
	| category
	;

/*
 * ----------------------------------------------------------------------
 * CHARMAP GRAMMAR 
 *
 * This grammar parses the charmap file as specified by POSIX 1003.2.
 * ----------------------------------------------------------------------
 */
charmap:	charmap_body
	| metasymbol_assign_sect charmap_body
	;

charmap_body:
	charmap_sect
	{
		/*
		 * No width specified.
		 * Assuming the locale provides its own method to
		 * implement wcwidth and wcswidth.
		 * So using the default setting to make the flat base
		 * defining the column width as 1.  The default is
		 * set in init.c
		 */
	}
	| charmap_sect column_width_sect
	{
		/*
		 * This locale defines the width table or
		 * the default width
		 */
	}
	;

charmap_sect	: 
	KW_CHARMAP '\n' charmap_stat_list KW_END KW_CHARMAP '\n'
	{
		if (euc_info_has_set == 0) {
			/*
			 * fill_euc_info() has not been called yet,
			 * so calling it now.
			 */
			fill_euc_info(&euc_info);
			euc_info_has_set = 1;
		}
		if (Charmap_pass == 2) {
			check_digit_values();
		}
	}
	;

column_width_sect	:
	column_width_def
	| column_width_default_def
	| column_width_default_def column_width_def
	| column_width_def column_width_default_def
	;

column_width_def	:
	KW_WIDTH '\n' {init_width_table();} column_width_stat_list
		KW_END KW_WIDTH '\n'
	;

column_width_default_def	:
	KW_WIDTH_DEFAULT number '\n'
	{
		sem_column_width_default_def();
	}
	;

charmap_stat_list	: 
	charmap_stat_list charmap_stat
	| charmap_stat
	;

charmap_stat	:
	symbol_def
  	| symbol_range_def
	;

symbol_range_def	:
	symbol KW_ELLIPSIS symbol byte_list {skip_to_EOL++;} '\n'
	{
		if (Charmap_pass == 1)
			sem_symbol_range_def_euc();
		else if (Charmap_pass == 2)
			sem_symbol_range_def();
	}
	;

symbol_def	:
	symbol byte_list {skip_to_EOL++;} '\n'
	{
		if (Charmap_pass == 1)
			sem_symbol_def_euc();
		else if (Charmap_pass == 2)
			sem_symbol_def();
	}
	;

column_width_stat_list	:
	column_width_stat_list column_width_stat
	| column_width_stat
	;

column_width_stat	:
	width_def
	| width_range_def
	;

width_range_def	:
	symbol KW_ELLIPSIS symbol number {skip_to_EOL++;} '\n'
	{
		sem_column_width_range_def();
	};

width_def	:
	symbol number {skip_to_EOL++;} '\n'
	{
		sem_column_width_def();
	}
	;

metasymbol_assign_sect	:
	metasymbol_assign metasymbol_assign_sect
	| metasymbol_assign
	;

metasymbol_assign	: 
	KW_MB_CUR_MAX number '\n'
  	{
		item_t	*it;
	  
		it = sem_pop();
		if (it->type != SK_INT)
			INTERNAL_ERROR;

		mb_cur_max		  = it->value.int_no;
		charmap.cm_mb_cur_max = it->value.int_no;

		(void) sem_push(it);

		if (method_class != USR_CODESET) {
			if (mb_cur_max == 1)
				method_class = SB_CODESET;
			else if ((mb_cur_max > 1) &&
			    (mb_cur_max <= MB_LEN_MAX))
				method_class = MB_CODESET;
			else
				INTERNAL_ERROR;
		}
		/* Insure that required methods are present */
		check_methods();
		sem_set_sym_val("<mb_cur_max>", SK_INT);
	}
	| KW_MB_CUR_MIN number '\n'
  	{
		item_t	*it;

		it = sem_pop();
		if (it->type != SK_INT)
			INTERNAL_ERROR;
		charmap.cm_mb_cur_min = it->value.int_no;
		if (it->value.int_no != 1) {
			diag_error(gettext(ERR_INV_MB_CUR_MIN),
			    it->value.int_no);
			destroy_item(it);		
		} else {
			(void) sem_push(it);
			sem_set_sym_val("<mb_cur_min>", SK_INT);
		}
	}
	| KW_CODESET text '\n'
  	{
		item_t *it;

		/*
		 * The code set name must consist of character in the PCS -
		 * which is analagous to doing an isgraph in the C locale
		 */

		it = sem_pop();
		if (it->type != SK_STR)
			INTERNAL_ERROR;
		if (Charmap_pass == 2) {
			unsigned char	*s;
			s = (unsigned char *)it->value.str;
			while (*s) {
				if (!isgraph(*s)) {
					error(4,
					    gettext(ERR_INV_CODE_SET_NAME),
					    it->value.str);
				}
				s++;
			}
			charmap.cm_csname = STRDUP(it->value.str);
			(void) sem_push(it);
			sem_set_sym_val("<code_set_name>", SK_STR);
		} else {
			destroy_item(it);
		}
	}
	;

/*
 * ----------------------------------------------------------------------
 * LOCSRC GRAMMAR 
 *
 * This grammar parses the LOCSRC file as specified by POSIX 1003.2.
 * ----------------------------------------------------------------------
 */
category	:	regular_category 
	{
		if (user_defined)
			diag_error(gettext(ERR_USER_DEF));
	}
	| non_reg_category
	;

regular_category	:	'\n' 
	| lc_collate
	| lc_ctype
	| lc_monetary
	| lc_numeric
	| lc_msg
	| lc_time
  	;

non_reg_category	:	unrecognized_cat
	;

/*
 * ----------------------------------------------------------------------
 * LC_COLLATE
 *
 * This section parses the LC_COLLATE category section of the LOCSRC
 * file.
 * ----------------------------------------------------------------------
 */

lc_collate	: 
	coll_sect_hdr coll_stats order_spec KW_END KW_LC_COLLATE '\n'
	{ 
		sem_collate(); 
	}
	| coll_sect_hdr order_spec KW_END KW_LC_COLLATE '\n'
	{ 
		sem_collate(); 
	}
	| coll_sect_hdr	KW_COPY locale_name '\n' KW_END KW_LC_COLLATE '\n'
	{
		copy_locale(LC_COLLATE);
	}
	;

coll_sect_hdr	:
	KW_LC_COLLATE '\n'
	{
		sem_init_colltbl();
	}
	;

coll_stats	:
  	coll_stats coll_stat
	| coll_stat
	;

coll_stat	:	'\n'
	| KW_COLLATING_ELEMENT symbol KW_FROM string '\n'
	{
		sem_def_collel();
		lc_has_collating_elements = 1;
	}
	| KW_COLLATING_SYMBOL symbol '\n'
	{
		sem_spec_collsym();
	}
	;

order_spec	: 
	KW_ORDER_START sort_sect coll_spec_list KW_ORDER_END
	{
		check_range();
		lc_collate_flag = 1;
	}
	| KW_ORDER_START sort_sect coll_spec_list KW_ORDER_END white_space
	{
		check_range();
		lc_collate_flag = 1;
	}
  	;

white_space	:	white_space '\n'
	| '\n'
	;

sort_sect	:	'\n'
	{
		item_t *i;

		i = create_item(SK_INT, _COLL_FORWARD_MASK);
		(void) sem_push(i);

		collate.co_nord++;

		sem_sort_spec();
		if (collate.co_nsubs >= 1)
			setup_substr();
	}
	| sort_modifier_spec '\n'
	{
		sem_sort_spec();
		if (collate.co_nsubs >= 1)
			setup_substr();
	}
	;


sort_modifier_spec	:
	sort_modifier_spec ';' sort_modifier_list
	{
		if (collate.co_nord == COLL_WEIGHTS_MAX) 
			diag_error(gettext(ERR_COLL_WEIGHTS));
		collate.co_nord++;
	}
	| sort_modifier_list
	{
		if (collate.co_nord == COLL_WEIGHTS_MAX) 
			diag_error(gettext(ERR_COLL_WEIGHTS));
		collate.co_nord++;
	}
	;

sort_modifier_list	:
	sort_modifier_list ',' sort_modifier
	{
		item_t *i;
	
		/*
		 * The forward and backward mask are mutually exclusive
		 * Ignore the second mask and continue processing
		 */

		i = sem_pop();
		if (((i->value.int_no & _COLL_FORWARD_MASK) &&
			(sort_mask == _COLL_BACKWARD_MASK)) ||
		    ((i->value.int_no & _COLL_BACKWARD_MASK) &&
			(sort_mask == _COLL_FORWARD_MASK))) {
			diag_error(gettext(ERR_FORWARD_BACKWARD));
			(void) sem_push(i);
		} else {
			i->value.int_no |= sort_mask;
			(void) sem_push(i);
		}
	}
	| sort_modifier
	{
		item_t *i;

		i = create_item(SK_INT, sort_mask);
		(void) sem_push(i);
	}
	;

sort_modifier	:
	KW_FORWARD             { sort_mask = _COLL_FORWARD_MASK;  }
	| KW_BACKWARD          { sort_mask = _COLL_BACKWARD_MASK; }
	| KW_NO_SUBSTITUTE     { sort_mask = _COLL_NOSUBS_MASK;   }
	| KW_POSITION          { sort_mask = _COLL_POSITION_MASK; }
	;

coll_spec_list	:
	coll_spec_list coll_symbol '\n'
  	{
		if (collation_error == 0) {
			sem_set_dflt_collwgt();
		} else {
			symbol_t	*sym;

			sym = sym_pop();
			diag_error2(gettext(ERR_INVAL_COLL), sym->sym_id);

			collation_error = 0;
		}
	}
	| coll_symbol '\n'
  	{
		if (collation_error == 0) {
			sem_set_dflt_collwgt();
		} else {
			symbol_t	*sym;

			sym = sym_pop();
			diag_error2(gettext(ERR_INVAL_COLL), sym->sym_id);

			collation_error = 0;
		}
	}
	| coll_spec_list coll_ell_spec '\n'
	{
		if (collation_error == 0) {
			sem_set_collwgt(cur_order);
		} else {
			symbol_t	*sym;

			sym = sym_pop();
			diag_error2(gettext(ERR_INVAL_COLL), sym->sym_id);

			collation_error = 0;
		}
	}
	| coll_ell_spec '\n'
	{
		if (collation_error == 0) {
			sem_set_collwgt(cur_order);
		} else {
			symbol_t	*sym;

			sym = sym_pop();
			diag_error2(gettext(ERR_INVAL_COLL), sym->sym_id);

			collation_error = 0;
		}
	}
	| coll_spec_list '\n'
	| '\n'
	;

coll_ell_spec	:	coll_symbol coll_rhs_list
	;

coll_rhs_list	:
	coll_rhs_list ';' coll_ell_list
	{
		if (collation_error == 0) {
			cur_order++;
		}
	}
	| coll_ell_list
	{
		if (collation_error == 0) {
			cur_order = 0;
		}
	}
	;

coll_ell_list	:	'"' coll_symbol_list '"'
	| coll_symbol_ref
	{
		if (collation_error == 0) {
			item_t	*i;
			i = create_item(SK_INT, 1);
			sem_push_collel();
			(void) sem_push(i);
		}
	}
	| KW_ELLIPSIS
	{
		if (collation_error == 0) {
			item_t	*i;

			i = create_item(SK_SYM, ellipsis_sym);
			(void) sem_push(i);
			i = create_item(SK_INT, 1);
			(void) sem_push(i);
		}
	}
	;

coll_symbol_list	:
	coll_symbol_list coll_symbol_ref
	{
		if (collation_error == 0) {
			item_t	*i;

			i = sem_pop();
			if (i == NULL || i->type != SK_INT)
				INTERNAL_ERROR;
			i->value.int_no++;
			sem_push_collel();
			(void) sem_push(i);
		}
	}
	| coll_symbol_ref
	{
		if (collation_error == 0) {
			item_t	*i;

			i = create_item(SK_INT, 1);
			sem_push_collel();
			(void) sem_push(i);
		}
	}
	;

/*
 * for collating identifier
 */
coll_symbol :
	char_symbol_ref
	{   
		int	ret;

		ret = sem_coll_sym_ref();
		if (ret == COLL_ERROR)
			collation_error++;
	}
	| byte_list
	{
		int	ret;

		sem_coll_literal_ref();
		ret = sem_coll_sym_ref();
		/*
		 * sem_coll_sym_ref() should never return COLL_ERROR
		 * here.  Though, in case, checks 'ret'
		 */
		if (ret == COLL_ERROR)
			collation_error++;
	}
	| KW_ELLIPSIS
	{
		(void) sym_push(ellipsis_sym);
	}
	;

coll_symbol_ref	: 
	char_symbol_ref
	{
		if (collation_error == 0) {
			int	ret;

			ret = sem_coll_sym_ref();
			if (ret == COLL_ERROR) {
				collation_error++;
				(void) sym_pop();
			}
		} else {
			(void) sym_pop();
		}
	}
	| byte_list
	{
		if (collation_error == 0) {
			int	ret;

			sem_coll_literal_ref();
			ret = sem_coll_sym_ref();
			if (ret == COLL_ERROR) {
				collation_error++;
				(void) sym_pop();
			}
		}
	}
	;
        
/*
 * -----------------------------------------------------------------------
 * LC_CTYPE
 *
 * This section parses the LC_CTYPE category section of the LOCSRC
 * file.
 * ----------------------------------------------------------------------
 */

lc_ctype	:
	KW_LC_CTYPE '\n' 
	{ 
		/* The LC_CTYPE category can only be defined once in a file */

		if (lc_ctype_flag)
			diag_error(gettext(ERR_DUP_CATEGORY), "LC_CTYPE");
	} 
	lc_ctype_spec_list KW_END KW_LC_CTYPE '\n'
	{
		/* A category with no text is an error (POSIX) */

		if (!lc_ctype_flag)
			diag_error(gettext(ERR_EMPTY_CAT), "LC_CTYPE");
		else {
			if (ctype.mask == NULL) {
				lc_ctype_flag = 0;
				diag_error(gettext(ERR_NO_CTYPE_MASK));
			} else {
				check_upper();
				check_lower();
				check_alpha();
				check_space();
				check_cntl();
				check_punct();
				check_graph();
				check_print();
				check_digits();
				check_xdigit();
			}
		}
	}
	| KW_LC_CTYPE '\n' KW_COPY locale_name '\n' KW_END KW_LC_CTYPE '\n'
	{
		copy_locale(LC_CTYPE);
	}
	| KW_LC_CTYPE '\n' KW_END KW_LC_CTYPE '\n'
	{
		lc_ctype_flag = 1;

		/* A category with no text is an error (POSIX) */

		diag_error(gettext(ERR_EMPTY_CAT), "LC_CTYPE");

	}
	;

lc_ctype_spec_list	:
	lc_ctype_spec_list lc_ctype_spec
	| lc_ctype_spec
	;

lc_ctype_spec	:	'\n'
	| KW_CHARCLASS { arblen = 0; } charclass_keywords '\n'
	{
		lc_ctype_flag = 1;
		add_charclass(&ctype, Lcbind_Table, _LC_TAG_CCLASS, arblen);
	}
	| charclass_kw char_range_list '\n'
	{
		lc_ctype_flag = 1;
		add_ctype(&ctype, Lcbind_Table, ctype_symbol_name);
		free(ctype_symbol_name);
	}
	| charclass_kw '\n'
	{
		lc_ctype_flag = 1;
		add_ctype(&ctype, Lcbind_Table, ctype_symbol_name);
		free(ctype_symbol_name);
	}
	| KW_CHARTRANS { arblen = 0; } charclass_keywords '\n'
	{
		lc_ctype_flag = 1;
		add_charclass(&ctype, Lcbind_Table, _LC_TAG_TRANS, arblen);
	}
	| chartrans_kw char_pair_list '\n'
	{
		lc_ctype_flag = 1;
		add_transformation(&ctype, Lcbind_Table, ctype_symbol_name);
		free(ctype_symbol_name);
	}
	;

charclass_keywords	:
	charclass_keywords ';' charclass_keyword
	{
		arblen++;
	}
	| charclass_keyword
	{
		arblen++;
	}
	;

charclass_keyword	:
	generic_symbol
	{
		item_t 	*i;

		i = create_item(SK_STR, $1);
		(void) sem_push(i);
		free($1);
	}
	| CHAR_CLASS_SYMBOL
	{
		item_t	*i;

		i = create_item(SK_STR, $1);
		(void) sem_push(i);
		free($1);
	}
	| CHAR_TRANS_SYMBOL
	{
		item_t	*i;

		i = create_item(SK_STR, $1);
		(void) sem_push(i);
		free($1);
	}
	;

charclass_kw	:
	CHAR_CLASS_SYMBOL
	{
		ctype_symbol_name = $1;
	}
	;

chartrans_kw	:
	CHAR_TRANS_SYMBOL
	{
		ctype_symbol_name = $1;
	}
	;

char_pair_list	:	char_pair_list ';' char_pair
	| char_pair
	;

char_pair	:
	'(' char_ref ',' char_ref ')'
	{
		sem_push_xlat();
	}
	;

char_ref	:
	char_symbol_ref
	{
		sem_char_ref();
	}
	| byte_list
	;

char_range_list	:	char_range_list ';' ctype_symbol
	| char_range_list ';' KW_ELLIPSIS ';' char_ref
	{
		push_char_range();
	}  
	| ctype_symbol
	;

ctype_symbol	:
	char_ref
	{
		push_char_sym();
	}
	;

/*
 * ----------------------------------------------------------------------
 * LC_MONETARY
 *
 * This section parses the LC_MONETARY category section of the LOCSRC
 * file.
 * ----------------------------------------------------------------------
 */

lc_monetary	:
	KW_LC_MONETARY '\n' 
	{
		/*
		 * The LC_MONETARY category can only be defined once in a
		 * locale
		 */

		if (lc_monetary_flag)
			diag_error(gettext(ERR_DUP_CATEGORY), "LC_MONETARY");
	 
	}
	lc_monetary_spec_list KW_END KW_LC_MONETARY '\n'
	{
		/* A category must have at least one line of text (POSIX) */

		if (!lc_monetary_flag)
			diag_error(gettext(ERR_EMPTY_CAT), "LC_MONETARY");
	}
	| KW_LC_MONETARY '\n' KW_COPY locale_name '\n' KW_END KW_LC_MONETARY '\n'
	{
		copy_locale(LC_MONETARY);
	}
	| KW_LC_MONETARY '\n' KW_END KW_LC_MONETARY '\n'
	{
		lc_monetary_flag++;

		/* A category must have at least one line of text (POSIX) */

		diag_error(gettext(ERR_EMPTY_CAT), "LC_MONETARY");

	}
	;


lc_monetary_spec_list	:	lc_monetary_spec_list lc_monetary_spec
	| lc_monetary_spec_list '\n'
	{
		lc_monetary_flag++;
	}
	| lc_monetary_spec
	{
		lc_monetary_flag++;
	}
	| '\n'
	;

lc_monetary_spec	:
  	KW_INT_CURR_SYMBOL string '\n'
	{
		sem_set_str(&monetary.int_curr_symbol);
	}
	| KW_CURRENCY_SYMBOL string '\n'
	{
		sem_set_str(&monetary.currency_symbol);
	}
	| KW_MON_DECIMAL_POINT string '\n'
	{ 
		sem_set_str(&monetary.mon_decimal_point); 
	}
	| KW_MON_THOUSANDS_SEP string '\n'  
	{
		sem_set_str(&monetary.mon_thousands_sep);
	}
	| KW_POSITIVE_SIGN string '\n'
	{
		sem_set_str(&monetary.positive_sign);
	}
	| KW_NEGATIVE_SIGN string '\n'
	{
		sem_set_str(&monetary.negative_sign);
	}
	| KW_MON_GROUPING digit_list '\n'
	{
		sem_set_diglist(&monetary.mon_grouping);
	}
	| KW_INT_FRAC_DIGITS number '\n'
	{
		sem_set_int(&monetary.int_frac_digits);
	}
	| KW_FRAC_DIGITS number '\n'
	{
		sem_set_int(&monetary.frac_digits);
	}
	| KW_P_CS_PRECEDES number '\n'
	{
		sem_set_int(&monetary.p_cs_precedes);
	}
	| KW_P_SEP_BY_SPACE number '\n'
	{
		sem_set_int(&monetary.p_sep_by_space);
	}
	| KW_N_CS_PRECEDES number '\n'
	{
		sem_set_int(&monetary.n_cs_precedes);
	}
	| KW_N_SEP_BY_SPACE number '\n'
	{
		sem_set_int(&monetary.n_sep_by_space);
	}
	| KW_P_SIGN_POSN number '\n'
	{
		sem_set_int(&monetary.p_sign_posn);
	}
	| KW_N_SIGN_POSN number '\n'
	{
		sem_set_int(&monetary.n_sign_posn);
	}
	| KW_INT_P_CS_PRECEDES number '\n'
	{
		sem_set_int(&monetary.int_p_cs_precedes);
	}
	| KW_INT_P_SEP_BY_SPACE number '\n'
	{
		sem_set_int(&monetary.int_p_sep_by_space);
	}
	| KW_INT_N_CS_PRECEDES number '\n'
	{
		sem_set_int(&monetary.int_n_cs_precedes);
	}
	| KW_INT_N_SEP_BY_SPACE number '\n'
	{
		sem_set_int(&monetary.int_n_sep_by_space);
	}
	| KW_INT_P_SIGN_POSN number '\n'
	{
		sem_set_int(&monetary.int_p_sign_posn);
	}
	| KW_INT_N_SIGN_POSN number '\n'
	{
		sem_set_int(&monetary.int_n_sign_posn);
	}
	;

/*
 * ----------------------------------------------------------------------
 * LC_MSG
 *
 * This section parses the LC_MSG category section of the LOCSRC
 * file.
 * ----------------------------------------------------------------------
*/

lc_msg	:
	KW_LC_MSG '\n' 
	{
		if (lc_message_flag)
			diag_error(gettext(ERR_DUP_CATEGORY), "LC_MESSAGE");

	}
	lc_msg_spec_list KW_END KW_LC_MSG '\n'
	{
		if (!lc_message_flag)
			diag_error(gettext(ERR_EMPTY_CAT), "LC_MESSAGE");
	}
	| KW_LC_MSG '\n' KW_COPY locale_name '\n' KW_END KW_LC_MSG '\n'
	{
		copy_locale(LC_MESSAGES);
	}
	| KW_LC_MSG '\n' KW_END KW_LC_MSG '\n'
	{
		lc_message_flag++;

		diag_error(gettext(ERR_EMPTY_CAT), "LC_MESSAGE");
	}
	;

lc_msg_spec_list	:	lc_msg_spec_list lc_msg_spec
	| lc_msg_spec_list '\n'
	{
		lc_message_flag++;
	}
	| lc_msg_spec
	{
		lc_message_flag++;
	}
	| '\n'
	;

lc_msg_spec	:
	KW_YESEXPR string '\n'
	{
		sem_set_str(&messages.yesexpr);
	}
	| KW_NOEXPR string '\n'
	{
		sem_set_str(&messages.noexpr);
	}
	| KW_YESSTR string '\n'
	{
		sem_set_str(&messages.yesstr);
	}
	| KW_NOSTR string '\n'
	{
		sem_set_str(&messages.nostr);
	}
	;

/*
 * ----------------------------------------------------------------------
 * LC_NUMERIC
 *
 * This section parses the LC_NUMERIC category section of the LOCSRC
 * file.
 * ----------------------------------------------------------------------
 */

lc_numeric :
	KW_LC_NUMERIC '\n' 
	{
		if (lc_numeric_flag)
			diag_error(gettext(ERR_DUP_CATEGORY), "LC_NUMERIC");

	}
	lc_numeric_spec_list KW_END KW_LC_NUMERIC '\n'
	{
		if (!lc_numeric_flag)
			diag_error(gettext(ERR_EMPTY_CAT), "LC_NUMERIC");
	}
	| KW_LC_NUMERIC '\n' KW_COPY locale_name '\n' KW_END KW_LC_NUMERIC '\n'
	{
		copy_locale(LC_NUMERIC);
	}
	| KW_LC_NUMERIC '\n' KW_END KW_LC_NUMERIC '\n'
	{
		lc_numeric_flag++;
		diag_error(gettext(ERR_EMPTY_CAT), "LC_NUMERIC");
	}
	;

lc_numeric_spec_list	:	lc_numeric_spec_list lc_numeric_spec
	| lc_numeric_spec
	{
		lc_numeric_flag++;
	}
	| lc_numeric_spec_list '\n'
	{
		lc_numeric_flag++;
	}
	| '\n'
	;


lc_numeric_spec	:
	KW_DECIMAL_POINT string '\n'
	{
		sem_set_str(&numeric.decimal_point);
		if (numeric.decimal_point == NULL) {
			numeric.decimal_point = "";
			diag_error(gettext(ERR_ILL_DEC_CONST), "");
		}
	}
	| KW_THOUSANDS_SEP string '\n'
	{
		sem_set_str(&numeric.thousands_sep);
	}
	| KW_GROUPING digit_list '\n'
	{
		sem_set_diglist(&numeric.grouping);
	}
	;

/*
 * ----------------------------------------------------------------------
 * LC_TIME
 *
 * This section parses the LC_TIME category section of the LOCSRC
 * file.
 * ----------------------------------------------------------------------
 */

lc_time	:
	KW_LC_TIME '\n' 
	{
		if (lc_time_flag)
			diag_error(gettext(ERR_DUP_CATEGORY), "LC_TIME");

	}
	lc_time_spec_list KW_END KW_LC_TIME '\n'
	{
		if (!lc_time_flag)
			diag_error(gettext(ERR_EMPTY_CAT), "LC_TIME");
	}
	| KW_LC_TIME '\n' KW_COPY locale_name '\n' KW_END KW_LC_TIME '\n'
	{
		copy_locale(LC_TIME);
	}
	| KW_LC_TIME '\n' KW_END KW_LC_TIME '\n'
	{
		lc_time_flag++;

		diag_error(gettext(ERR_EMPTY_CAT), "LC_TIME");
	}
	;

lc_time_spec_list	:
	lc_time_spec_list lc_time_spec
	{
		lc_time_flag++;
	}
	| lc_time_spec
	{
		lc_time_flag++;
	}
	| lc_time_spec_list '\n'
	{
		lc_time_flag++;
	}
	| '\n'
	;

lc_time_spec	:
	KW_ABDAY string_list '\n'
	{
		sem_set_str_lst(lc_time.abday, 7);
	}
	| KW_DAY string_list '\n'
	{
		sem_set_str_lst(lc_time.day, 7);
	}
	| KW_ABMON string_list '\n'
	{
		sem_set_str_lst(lc_time.abmon, 12);
	}
	| KW_MON string_list '\n'
	{
		sem_set_str_lst(lc_time.mon, 12);
	}
	| KW_D_T_FMT string '\n'
	{
		sem_set_str(&lc_time.d_t_fmt);
	}
	| KW_D_FMT string '\n'
	{
		sem_set_str(&lc_time.d_fmt);
	}
	| KW_T_FMT string '\n'
	{
		sem_set_str(&lc_time.t_fmt);
	}
	| KW_AM_PM string_list '\n'
	{
		sem_set_str_lst(lc_time.am_pm, 2);
	}
	| KW_T_FMT_AMPM string '\n'
	{
		sem_set_str(&lc_time.t_fmt_ampm);
	}
	| KW_ERA {arblen=0;} arblist '\n'
	{
		char **arbp = MALLOC(char *, arblen + 1);

		sem_set_str_lst(arbp, arblen);
		arbp[arblen] = NULL;
		lc_time.era = arbp;
	}
	| KW_ERA_D_FMT string '\n' 
	{
		sem_set_str(&lc_time.era_d_fmt);
	}
	| KW_ERA_D_T_FMT string '\n'
	{
		sem_set_str(&lc_time.era_d_t_fmt);
	}
	| KW_ERA_T_FMT string '\n'
	{
		sem_set_str(&lc_time.era_t_fmt);
	}
	| KW_ALT_DIGITS { arblen = 0; } arblist '\n'
      	{
		sem_set_str_cat(&lc_time.alt_digits, arblen);
	}
	| KW_DATE_FMT string '\n'
	{
		sem_set_str(&lc_time.date_fmt);
	}
	;

arblist	:
	arblist ';' string
	{
		arblen++;
	}
	| string
	{
		arblen++;
	}
	;


unrecognized_cat	:
	generic_symbol '\n'
	{
		free($1);
		user_defined++;
		while (yylex() != KW_END)
			;	    
	}
	generic_symbol '\n'
	{
		diag_error(gettext(ERR_UNDEF_CAT), $1);
		free($1);
	}
	;

/*
 * ----------------------------------------------------------------------
 * METHODS
 *
 * This section defines the grammar which parses the methods file.
 * ----------------------------------------------------------------------
 */

method_def	: 
	lc_bind
	| methods
	| lc_bind methods
	| methods lc_bind
	;

lc_bind	:
	KW_LCBIND '\n' symbolic_assign_list lcbind_list KW_END KW_LCBIND '\n'
	;

symbolic_assign_list	:
	symbolic_assign_list symbolic_assign
	| symbolic_assign
	| '\n'
	;

symbolic_assign	:
	symbol_name '=' hexadecimal_number '\n'
	{
		sem_set_lcbind_symbolic_value();
	}
	;

symbol_name	:	text
	;

hexadecimal_number	:
	HEX_CONST
	{
		item_t	*hexadecimal_number;
		unsigned int	hex_num;

		hex_num = $1;
		hexadecimal_number = create_item(SK_INT, hex_num);
		(void) sem_push(hexadecimal_number);
	}
	;

lcbind_list	:
	lcbind_list lcbind_value
	| lcbind_value
	| '\n'
	;

lcbind_value	:
	CHAR_CLASS_SYMBOL char_category generic_symbol '\n'
	{
		/*
		 * user attempting to redefine a predefined charclass
		 * which is NOT allowed.
		 */
		diag_error(gettext(ERR_REDEFINE_CHARCLASS), $1);
		free($1);
	}
	| text char_category generic_symbol '\n'
	{
		item_t	*name;
		int	i;
		unsigned int	mask;
		int	found = 0;

		/* value = sem_pop(); */
		name = sem_pop();
		for (i = 0; i < length_lcbind_symbol_table; i++) {
			if (!strcmp($3,
				lcbind_symbol_table[i].symbol_name)) {
				mask = lcbind_symbol_table[i].value;
				found = 1;
				break;
			}
		}
		if (!found) {
			error(4, gettext(ERR_VALUE_NOT_FOUND), $3);
		}
		add_char_ct_name(&ctype, Lcbind_Table, name->value.str,
		    lcbind_tag, $3, mask, 0);
		free($3);
		destroy_item(name);
	}
	| text char_category hexadecimal_number '\n'
	{
		item_t	*name, *value;

		value = sem_pop();
		name = sem_pop();
		add_char_ct_name(&ctype, Lcbind_Table, name->value.str,
		    lcbind_tag, NULL, value->value.int_no, 0);
		destroy_item(value);
		destroy_item(name);
	}
	;

char_category	:
	KW_CHARCLASS
	{
		lcbind_tag = _LC_TAG_CCLASS;
	}
	| KW_CHARTRANS
	{
		lcbind_tag = _LC_TAG_TRANS;
	}
	;

methods	:
  	KW_METHODS '\n' 
	{
		method_class = USR_CODESET;
	}
	method_assign_list KW_END KW_METHODS '\n'
	{
		switch (charmap.cm_fc_type) {
		case _FC_EUC:
			if (Cswidth == FALSE) {
				/* cswidth is not specified */
				error(4, gettext(ERR_NO_CSWIDTH));
			}
			break;
		case _FC_UTF8:
			if (Cswidth == TRUE) {
				/* cswidth is specified */
				error(4, gettext(ERR_INV_CSWIDTH));
			}
			if (charmap.cm_pc_type != _PC_UCS4) {
				/* utf8 takes only ucs4 as process code */
				error(4, gettext(ERR_PROC_FILE_MISMATCH));
			}
			/*
			 * If the UTF-8 locale is specified, needs to do
			 * a special check to determine if it's a really
			 * double layer or not.  Old utf-8 locale was
			 * single layer.
			 */
			if (single_layer == FALSE) {
				check_layer();
			}
			break;
		case _FC_OTHER:
			if (Cswidth == TRUE) {
				/*
				 * cswidth is specified
				 * As _FC_OTHER is the default filecode,
				 * needs to check if it has been explicitly
				 * specified.  Otherwise, Cswidth cannot
				 * be specified with _FC_OTHER.
				 */
				if (set_filecode) {
					/* explicitly specified */
					error(4, gettext(ERR_INV_CSWIDTH));
					/* NOTREACHED */
				}
				/* assume the file code is euc */
				charmap.cm_fc_type = _FC_EUC;
			}
			break;
		}
	}
	;

method_assign_list	: 
	method_assign_list method_assign
	| method_assign_list '\n'
	| method_assign
	| '\n'
	;

method_string	:
	string
	{
		args = 1;
	}
	| string string
	{
		args = 2;
	}
	| string string string
	{
		args = 3;
		if (lp64p == TRUE)
			error(4, gettext(ERR_LP64_WITH_OLD_EXT));
		if (new_extfmt == TRUE)
			error(4, gettext(ERR_MIX_NEW_OLD_EXT));
		old_extfmt = TRUE;
	}
	| string string string string
	{
		args = 4;
		if (old_extfmt == TRUE)
			error(4, gettext(ERR_MIX_NEW_OLD_EXT));
		new_extfmt = TRUE;
	}
	;

method_assign	: 
	KW_PROCESS_CODE KW_EUC '\n'
	{
		if (set_proccode) {
			error(4, gettext(ERR_MULTI_PROC_CODE));
		}
		set_proccode++;

		single_layer = FALSE; /* double layer */
		charmap.cm_pc_type = _PC_EUC;
	}
	|
	KW_PROCESS_CODE KW_DENSE '\n'
	{
		if (set_proccode) {
			error(4, gettext(ERR_MULTI_PROC_CODE));
		}
		set_proccode++;

		single_layer = TRUE;
		charmap.cm_pc_type = _PC_DENSE;
	}
	|
	KW_PROCESS_CODE KW_UCS4 '\n'
	{
		if (set_proccode) {
			error(4, gettext(ERR_MULTI_PROC_CODE));
		}
		set_proccode++;

		single_layer = FALSE; /* possible double layer */
		charmap.cm_pc_type = _PC_UCS4;
	}
	|
	KW_FILE_CODE KW_EUC	'\n'
	{
		if (set_filecode) {
			error(4, gettext(ERR_MULTI_FILE_CODE));
		}
		set_filecode++;

		charmap.cm_fc_type = _FC_EUC;
	}
	|
	KW_FILE_CODE KW_UTF8 '\n'
	{
		if (set_filecode) {
			error(4, gettext(ERR_MULTI_FILE_CODE));
		}
		set_filecode++;

		charmap.cm_fc_type = _FC_UTF8;
	}
	|
	KW_FILE_CODE KW_OTHER '\n'
	{
		if (set_filecode) {
			error(4, gettext(ERR_MULTI_FILE_CODE));
		}
		set_filecode++;

		charmap.cm_fc_type = _FC_OTHER;
	}
	|
	KW_CSWIDTH num_pairs '\n'
	{
		Cswidth = TRUE;
	}
	|
	KW_MBLEN method_string '\n'
	{
		set_method(CHARMAP_MBLEN, args);
	}
	| KW_MBTOWC method_string '\n'
	{
		/*
		 * If the locale is single layer, each user
		 * method API setting will be mapped to the
		 * corresponding native method API entry.
		 */
		if (single_layer == FALSE)
			set_method(CHARMAP_MBTOWC, args);
		else
			set_method(CHARMAP_MBTOWC_AT_NATIVE, args);
	}
	| KW_MBSTOWCS method_string '\n'
	{
		if (single_layer == FALSE)
			set_method(CHARMAP_MBSTOWCS, args);
		else
			set_method(CHARMAP_MBSTOWCS_AT_NATIVE, args);
	}
	| KW_WCTOMB method_string '\n'
	{
		if (single_layer == FALSE)
			set_method(CHARMAP_WCTOMB, args);
		else
			set_method(CHARMAP_WCTOMB_AT_NATIVE, args);
	}
	| KW_WCSTOMBS method_string '\n'
	{
		if (single_layer == FALSE)
			set_method(CHARMAP_WCSTOMBS, args);
		else
			set_method(CHARMAP_WCSTOMBS_AT_NATIVE, args);
	}
	| KW_WCWIDTH method_string '\n'
	{
		if (single_layer == FALSE)
			set_method(CHARMAP_WCWIDTH, args);
		else
			set_method(CHARMAP_WCWIDTH_AT_NATIVE, args);
	}
	| KW_WCSWIDTH method_string '\n'
	{
		if (single_layer == FALSE)
			set_method(CHARMAP_WCSWIDTH, args);
		else
			set_method(CHARMAP_WCSWIDTH_AT_NATIVE, args);
	}
	| KW_MBFTOWC method_string '\n'
	{
		if (single_layer == FALSE)
			set_method(CHARMAP_MBFTOWC, args);
		else
			set_method(CHARMAP_MBFTOWC_AT_NATIVE, args);
	}
	| KW_FGETWC method_string '\n'
	{
		if (single_layer == FALSE)
			set_method(CHARMAP_FGETWC, args);
		else
			set_method(CHARMAP_FGETWC_AT_NATIVE, args);
	}
	| KW_TOWUPPER method_string '\n'
	{
		if (single_layer == FALSE)
			set_method(CTYPE_TOWUPPER, args);
		else
			set_method(CTYPE_TOWUPPER_AT_NATIVE, args);
	}
	| KW_TOWLOWER method_string '\n'
	{
		if (single_layer == FALSE)
			set_method(CTYPE_TOWLOWER, args);
		else
			set_method(CTYPE_TOWLOWER_AT_NATIVE, args);
	}
	| KW_WCTYPE method_string '\n'
	{
		set_method(CTYPE_WCTYPE, args);
	}
	| KW_ISWCTYPE method_string '\n'
	{
		if (single_layer == FALSE)
			set_method(CTYPE_ISWCTYPE, args);
		else
			set_method(CTYPE_ISWCTYPE_AT_NATIVE, args);
	}
	| KW_STRCOLL method_string '\n'
	{
		set_method(COLLATE_STRCOLL, args);
	}
	| KW_STRXFRM method_string '\n'
	{
		set_method(COLLATE_STRXFRM, args);
	}
	| KW_WCSCOLL method_string '\n'
	{
		if (single_layer == FALSE)
			set_method(COLLATE_WCSCOLL, args);
		else
			set_method(COLLATE_WCSCOLL_AT_NATIVE, args);
	}
	| KW_WCSXFRM method_string '\n'
	{
		if (single_layer == FALSE)
			set_method(COLLATE_WCSXFRM, args);
		else
			set_method(COLLATE_WCSXFRM_AT_NATIVE, args);
	}
	| KW_REGCOMP method_string '\n'
	{
		set_method(COLLATE_REGCOMP, args);
	}
	| KW_REGEXEC method_string '\n'
	{
		set_method(COLLATE_REGEXEC, args);
	}
	| KW_REGFREE method_string '\n'
	{
		set_method(COLLATE_REGFREE, args);
	}
	| KW_REGERROR method_string '\n'
	{
		set_method(COLLATE_REGERROR, args);
	}
	| KW_STRFMON method_string '\n'
	{
		set_method(MONETARY_STRFMON, args);
	}
	| KW_STRFTIME method_string '\n'
	{
		set_method(TIME_STRFTIME, args);
	}
	| KW_STRPTIME method_string '\n'
	{
		set_method(TIME_STRPTIME, args);
	}
	| KW_GETDATE method_string '\n'
	{
		set_method(TIME_GETDATE, args);
	}
	| KW_WCSFTIME method_string '\n'
	{
		set_method(TIME_WCSFTIME, args);
	}
	| KW_EUCPCTOWC method_string '\n'
	{
		set_method(CHARMAP_EUCPCTOWC, args);
	}
	| KW_WCTOEUCPC method_string '\n'
	{
		set_method(CHARMAP_WCTOEUCPC, args);
	}
	| KW_TRWCTYPE method_string '\n'
	{
		set_method(CTYPE_TRWCTYPE, args);
	}
	| KW_TOWCTRANS method_string '\n'
	{
		if (single_layer == FALSE)
			set_method(CTYPE_TOWCTRANS, args);
		else
			set_method(CTYPE_TOWCTRANS_AT_NATIVE, args);
	}
	| KW_WCTRANS method_string '\n'
	{
		set_method(CTYPE_WCTRANS, args);
	}
	| KW_FGETWC_AT_NATIVE method_string '\n'
	{
		set_method(CHARMAP_FGETWC_AT_NATIVE, args);
	}
	| KW_ISWCTYPE_AT_NATIVE method_string '\n'
	{
		set_method(CTYPE_ISWCTYPE_AT_NATIVE, args);
	}
	| KW_MBFTOWC_AT_NATIVE method_string '\n'
	{
		set_method(CHARMAP_MBFTOWC_AT_NATIVE, args);
	}
	| KW_MBSTOWCS_AT_NATIVE method_string '\n'
	{
		set_method(CHARMAP_MBSTOWCS_AT_NATIVE, args);
	}
	| KW_MBTOWC_AT_NATIVE method_string '\n'
	{
		set_method(CHARMAP_MBTOWC_AT_NATIVE, args);
	}
	| KW_TOWLOWER_AT_NATIVE method_string '\n'
	{
		set_method(CTYPE_TOWLOWER_AT_NATIVE, args);
	}
	| KW_TOWUPPER_AT_NATIVE method_string '\n'
	{
		set_method(CTYPE_TOWUPPER_AT_NATIVE, args);
	}
	| KW_WCSCOLL_AT_NATIVE method_string '\n'
	{
		set_method(COLLATE_WCSCOLL_AT_NATIVE, args);
	}
	| KW_WCSTOMBS_AT_NATIVE method_string '\n'
	{
		set_method(CHARMAP_WCSTOMBS_AT_NATIVE, args);
	}
	| KW_WCSXFRM_AT_NATIVE method_string '\n'
	{
		set_method(COLLATE_WCSXFRM_AT_NATIVE, args);
	}
	| KW_WCTOMB_AT_NATIVE method_string '\n'
	{
		set_method(CHARMAP_WCTOMB_AT_NATIVE, args);
	}
	| KW_WCWIDTH_AT_NATIVE method_string '\n'
	{
		set_method(CHARMAP_WCWIDTH_AT_NATIVE, args);
	}
	| KW_WCSWIDTH_AT_NATIVE method_string '\n'
	{
		set_method(CHARMAP_WCSWIDTH_AT_NATIVE, args);
	}
	| KW_TOWCTRANS_AT_NATIVE method_string '\n'
	{
		set_method(CTYPE_TOWCTRANS_AT_NATIVE, args);
	}
	| KW_FNMATCH method_string '\n'
	{
		set_method(COLLATE_FNMATCH, args);
	}
	| KW_BTOWC method_string '\n'
	{
		if (single_layer == FALSE)
			set_method(CHARMAP_BTOWC, args);
		else
			set_method(CHARMAP_BTOWC_AT_NATIVE, args);
	}
	| KW_WCTOB method_string '\n'
	{
		if (single_layer == FALSE)
			set_method(CHARMAP_WCTOB, args);
		else
			set_method(CHARMAP_WCTOB_AT_NATIVE, args);
	}
	| KW_MBSINIT method_string '\n'
	{
		set_method(CHARMAP_MBSINIT, args);
	}
	| KW_MBRLEN method_string '\n'
	{
		set_method(CHARMAP_MBRLEN, args);
	}
	| KW_MBRTOWC method_string '\n'
	{
		if (single_layer == FALSE)
			set_method(CHARMAP_MBRTOWC, args);
		else
			set_method(CHARMAP_MBRTOWC_AT_NATIVE, args);
	}
	| KW_WCRTOMB method_string '\n'
	{
		if (single_layer == FALSE)
			set_method(CHARMAP_WCRTOMB, args);
		else
			set_method(CHARMAP_WCRTOMB_AT_NATIVE, args);
	}
	| KW_MBSRTOWCS method_string '\n'
	{
		if (single_layer == FALSE)
			set_method(CHARMAP_MBSRTOWCS, args);
		else
			set_method(CHARMAP_MBSRTOWCS_AT_NATIVE, args);
	}
	| KW_WCSRTOMBS method_string '\n'
	{
		if (single_layer == FALSE)
			set_method(CHARMAP_WCSRTOMBS, args);
		else
			set_method(CHARMAP_WCSRTOMBS_AT_NATIVE, args);
	}
	| KW_BTOWC_AT_NATIVE method_string '\n'
	{
		set_method(CHARMAP_BTOWC_AT_NATIVE, args);
	}
	| KW_WCTOB_AT_NATIVE method_string '\n'
	{
		set_method(CHARMAP_WCTOB_AT_NATIVE, args);
	}
	| KW_MBRTOWC_AT_NATIVE method_string '\n'
	{
		set_method(CHARMAP_MBRTOWC_AT_NATIVE, args);
	}
	| KW_WCRTOMB_AT_NATIVE method_string '\n'
	{
		set_method(CHARMAP_WCRTOMB_AT_NATIVE, args);
	}
	| KW_MBSRTOWCS_AT_NATIVE method_string '\n'
	{
		set_method(CHARMAP_MBSRTOWCS_AT_NATIVE, args);
	}
	| KW_WCSRTOMBS_AT_NATIVE method_string '\n'
	{
		set_method(CHARMAP_WCSRTOMBS_AT_NATIVE, args);
	}
	;

/*
 * ----------------------------------------------------------------------
 * GENERAL
 *
 * This section parses the syntatic elements shared by one or more of
 * the above.
 * ----------------------------------------------------------------------
 */

generic_symbol	:
	N_SYMBOL
	{
		$$ = $1.id;
	}
	| U_SYMBOL
	{
		$$ = $1.id;
	}
	;

digit_list	:
	digit_list ';' number
	{
		/* add the new number to the digit list */
		item_t	*n_digits, *next_digit;
		/* swap digit and count and increment count */
		next_digit = sem_pop();
		n_digits = sem_pop();

		n_digits->value.int_no++;
		(void) sem_push(next_digit);
		(void) sem_push(n_digits);
	}
	| number
	{
		item_t	*i;

		/* create count and push on top of stack */
		i = create_item(SK_INT, 1);
		(void) sem_push(i);
	}
	;

char_symbol_ref	:
	generic_symbol
	{
		sem_existing_symbol($1);
		free($1);
	}
	;

symbol	:
	generic_symbol
	{
		sem_symbol($1);
		free($1);
	}
	;

string_list	:	string_list ';' string
	| string
	;

text	:	string
	| generic_symbol
	{
		item_t	*i = create_item(SK_STR, $1);
		(void) sem_push(i);
		free($1);
	}
	;

string	:
	'"' { instring = TRUE; } STRING '"'
	{
		item_t	*i;
	    
		i = create_item(SK_STR, $3);
		(void) sem_push(i);
		free($3);
	}
	| STRING
	{
		item_t	*i;
	    
		i = create_item(SK_STR, $1);
		(void) sem_push(i);
		free($1);
	}
	;

byte_list	:
	BYTES
	{
		item_t	*it;

		it = create_item(SK_UINT64, $1);
		(void) sem_push(it);
	}
	| CHAR_CONST
	{
		item_t	*it;
		it = create_item(SK_UINT64, (uint64_t)$1);
		(void) sem_push(it);
	}
	| U_SYMBOL
	{
		item_t	*it;
		it = create_item(SK_UINT64, $1.ucs);
		(void) sem_push(it);
		free($1.id);
	}

number	:	NUM
	{
		item_t	*it;

		it = create_item(SK_INT, $1);
		(void) sem_push(it);
	}
	;

num_pairs	:	num_pairs ',' num_pair
	| num_pair
	;

num_pair	:
	number ':' number
	{
		item_t	*char_width, *screen_width;
		static int	codeset = 1;

		screen_width = sem_pop();
		if (screen_width->type != SK_INT)
			INTERNAL_ERROR;
		if (screen_width->value.int_no > MAX_WIDTH) {
			error(4, gettext(ERR_TOO_LARGE_WIDTH),
			    screen_width->value.int_no);
		}
			
		char_width = sem_pop();
		if (char_width->type != SK_INT)
			INTERNAL_ERROR;

		if (codeset > MAX_CODESETS)
			error(4, gettext(ERR_TOO_MANY_CODESETS), MAX_CODESETS);

		switch (codeset) {
		case 1:		/* codeset 1 */
			if ((char_width->value.int_no + 1) > MB_LEN_MAX)
				error(4, gettext(ERR_MB_LEN_MAX_TOO_BIG),
				    MB_LEN_MAX - 1);
			euc_info.euc_bytelen1 =
			    (char)char_width->value.int_no;
			euc_info.euc_scrlen1 =
			    (char)screen_width->value.int_no;
			break;
		case 2:		/* codeset 2 */
			if ((char_width->value.int_no + 1) > MB_LEN_MAX)
				error(4, gettext(ERR_MB_LEN_MAX_TOO_BIG),
				    MB_LEN_MAX - 1);
			euc_info.euc_bytelen2 =
			    (char)char_width->value.int_no;
			euc_info.euc_scrlen2 =
			    (char)screen_width->value.int_no;
			break;
		case 3:		/* codeset 3 */
			if ((char_width->value.int_no + 1) > MB_LEN_MAX)
				error(4, gettext(ERR_MB_LEN_MAX_TOO_BIG),
				    MB_LEN_MAX - 1);
			euc_info.euc_bytelen3 =
			    (char)char_width->value.int_no;
			euc_info.euc_scrlen3 =
			    (char)screen_width->value.int_no;
			break;
		}

		destroy_item(char_width);
		destroy_item(screen_width);

		codeset++;
	}
	;

locale_name	:	LOC_NAME
	{
		item_t	*i;
	    
		i = create_item(SK_STR, $1);
		(void) sem_push(i);
		free($1);
	}
	;
%%

void
initgram(void) {
}
