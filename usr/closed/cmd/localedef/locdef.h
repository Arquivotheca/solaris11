/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _LOCALEDEF_LOCDEF_H
#define	_LOCALEDEF_LOCDEF_H

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
/* @(#)$RCSfile: locdef.h,v $ $Revision: 1.4.5.2 $ */
/* (OSF) $Date: 1992/08/10 14:44:37 $ */

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
 * 1.4  com/cmd/nls/locdef.h, , bos320, 9135320l 8/12/91 17:09:41
 *
 */

/* To make available the definition of _LP64 and _ILP32 */
#include <iconv.h>
#include <wchar.h>
#include <wctype.h>
#include <locale.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <libintl.h>
#include <limits.h>
#include <sys/localedef.h>
#include "symtab.h"
#include "method.h"
#include "semstack.h"
#include "localedef_msg.h"

/* sem_ctype.c, gram.y */
struct lcbind_symbol_table {
	char	*symbol_name;
	unsigned int    value;
};

struct lcbind_table {
	_LC_bind_table_t	lcbind;
	unsigned int 		nvalue;
	char			*orig_value;
	int			defined;
};

struct code_conv {
	char	*name;
	iconv_t	cd;		/* from UTF-8 to USER */
};

/* defined in localedef.c */
extern char	*yyfilenm;
extern int	verbose;
extern int	warning;
extern int	lp64p;
extern int	Charmap_pass;
extern struct code_conv	*code_conv;

/* defined in sem_chr.c */
extern void	define_all_wchars(void);
extern void	sem_set_str_lst(char **, int);
extern void	sem_set_str_cat(char **, int);
extern void	sem_set_str(char **);
extern void	sem_set_int(char *);
extern void	sem_digit_list(void);
extern void	sem_set_diglist(char **);
extern void	sem_set_sym_val(char *, item_type_t);
extern void	sem_char_ref(void);
extern void	sem_symbol(char *);
extern void	sem_existing_symbol(char *);
extern void	sem_symbol_def(void);
extern void	sem_symbol_def_euc(void);
extern void	sem_symbol_range_def(void);
extern void	sem_symbol_range_def_euc(void);
extern void	check_digit_values(void);
extern void	fill_euc_info(_LC_euc_info_t *);
extern void	init_width_table(void);
extern void	sem_column_width_def(void);
extern void	sem_column_width_range_def(void);
extern void	sem_column_width_default_def(void);
extern void	width_table_comp(void);
extern void	set_column_width(void);
extern int	wc_from_fc(uint64_t);
extern int	mbs_from_fc(char *, uint64_t);
extern char	*real_copy_string(const char *, int);
extern int	Space_character_code;
extern void	define_wchar(wchar_t);
extern int	wchar_defined(wchar_t);
extern int	width_flag;

/* defined in sem_coll.c */
extern void	sem_collate(void);
extern void	sem_init_colltbl(void);
extern void	sem_spec_collsym(void);
extern void	sem_set_dflt_collwgt(void);
extern void	sem_push_collel(void);
extern void	sem_coll_literal_ref(void);
extern void	sem_def_collel(void);
extern void	sem_sort_spec(void);
extern void	setup_substr(void);
extern void	sem_set_collwgt(int);
extern int	sem_coll_sym_ref(void);
extern void	check_range(void);
extern char	*char_info(wchar_t);
extern symbol_t	*ellipsis_sym;

/* defined in check.c */
extern void	check_upper(void);
extern void	check_lower(void);
extern void	check_alpha(void);
extern void	check_space(void);
extern void	check_cntl(void);
extern void	check_punct(void);
extern void	check_graph(void);
extern void	check_print(void);
extern void	check_digits(void);
extern void	check_xdigit(void);

/* defined in sem_ctype.c */
extern void	add_charclass(_LC_ctype_t *, struct lcbind_table *,
	_LC_bind_tag_t, int);
extern void	add_ctype(_LC_ctype_t *, struct lcbind_table *,
	char *);
extern void	sem_set_lcbind_symbolic_value(void);
extern void	add_char_ct_name(_LC_ctype_t *,	struct lcbind_table *,
	char *,	_LC_bind_tag_t,	char *,	unsigned int, int);
extern void	push_char_range(void);
extern void	push_char_sym(void);
extern struct lcbind_table	*Lcbind_Table;
extern struct lcbind_symbol_table	lcbind_symbol_table[];
extern int	length_lcbind_symbol_table;

/* defined in sem_method.c */
extern void	check_methods(void);
extern void	check_layer(void);
extern void	set_method(int, int);
extern library_t	lib_array[];
extern int	user_specified_libc;

/* defined in sem_xlat.c */
extern void	add_transformation(_LC_ctype_t *, struct lcbind_table *,
	char *);
extern void	sem_push_xlat(void);

/* defined in sem_comp.c */
extern void	compress_transtabs(_LC_ctype_t *, int);

/* defined in copy.c */
extern void	copy_locale(int);
extern void	output_copy_notice(FILE *, int);
extern int	copying[];
extern int	copyflag;

/* defined in symtab.c */
extern symbol_t	*create_symbol(char *);
extern symbol_t	*loc_symbol(char *);
extern symbol_t	*sym_pop(void);
extern void	clear_symtab(void);
extern void	sym_free_chr(symbol_t *);
extern void	sym_free_all(symbol_t *);
extern int	add_symbol(symbol_t *);
extern int	sym_push(symbol_t *);
#ifdef	ODEBUG
extern void	dump_symtab(void);
#endif

/* defined in semstack.c */
extern int	sem_push(item_t *);
extern item_t	*sem_pop(void);
extern item_t	*create_item(item_type_t, ...);
extern void	destroy_item(item_t *);

/* defined in gen.c */
extern void	gen(FILE *);

/* defined in scan.c */
extern void	initlex(void);
extern int	yylex(void);
extern FILE	*infp;
extern char	escape_char;
extern int	skip_to_EOL;
extern int	instring;
extern int	yycharno;
extern int	yylineno;
extern int	value;

/* defined in gram.y */
extern void	initgram(void);
extern int	yyparse(void);
extern _LC_euc_info_t	euc_info;
extern int	mb_cur_max;
extern int	lc_has_collating_elements;
extern int	single_layer;
extern int	lc_ctype_flag;
extern int	lc_collate_flag;
extern int	lc_time_flag;
extern int	lc_monetary_flag;
extern int	lc_message_flag;
extern int	lc_numeric_flag;
extern wchar_t	max_wchar_enc;
extern uint64_t	max_fc_enc;
#ifdef	ODEBUG
extern int	yydebug;
#endif

/* defined in init.c */
extern void	init_symbol_tbl(int);
extern _LC_collate_t	collate;
extern _LC_ctype_t	ctype;
extern _LC_monetary_t	monetary;
extern _LC_numeric_t	numeric;
extern _LC_time_t	lc_time;
extern _LC_messages_t	messages;
extern _LC_locale_t	locale;
extern _LC_charmap_t	charmap;
extern _LC_collate_t	*collate_ptr;
extern _LC_ctype_t	*ctype_ptr;
extern _LC_monetary_t	*monetary_ptr;
extern _LC_numeric_t	*numeric_ptr;
extern _LC_time_t	*lc_time_ptr;
extern _LC_messages_t	*messages_ptr;

/* defined in method.c */
extern method_t	*std_methods;
extern int	method_class;
extern ow_method_t	*ow_methods;

/* err.c */
extern int	err_flag;
extern void	*safe_malloc(size_t, const char *, int);
extern void	*safe_realloc(void *, size_t, const char *, int);
extern char	*safe_strdup(const char *, const char *, int);
extern void	error(int, const char *, ...) __NORETURN;
extern void	diag_error(const char *, ...);
extern void	diag_verror(const char *, va_list);
extern void	diag_error2(const char *, ...);
extern void	usage(int);
extern void	yyerror(const char *);


#define	MALLOC(t, n)	\
	((t *)safe_malloc(sizeof (t) * (n), __FILE__, __LINE__))
#define	REALLOC(t, p, n)	\
	((t *)safe_realloc(p, sizeof (t) * (n), __FILE__, __LINE__))
#define	STRDUP(s)	\
	(safe_strdup(s, __FILE__, __LINE__))
#define	INTERNAL_ERROR	\
	error(4, ERR_INTERNAL, __FILE__, __LINE__)

/*
 * Ignore etc.
 */
#ifdef _LP64
#define	UNDEFINED	0
#define	IGNORE		(UINT_MAX)
#define	SUB_STRING	(UINT_MAX-1)
#define	PENDING		(UINT_MAX-2)
#else
#define	UNDEFINED	0
#define	IGNORE		(ULONG_MAX)
#define	SUB_STRING	(ULONG_MAX-1)
#define	PENDING		(ULONG_MAX-2)
#endif

#define	INT_METHOD(n)	(n)

#define	COLL_ERROR	0
#define	COLL_OK	1

/* scan.c */
#define	MAX_BYTES	(sizeof (uint64_t))

/* gen.c */
#define	STRING_MAX	255

/* gram.y */
#define	MAX_CODESETS	3

/* sem_chr.c */
#define	UNDEF_WIDTH	0xff
#define	MAX_WIDTH	0xfe	/* 0xff is used for UNDEF_WIDTH */
#define	MAX_PC	0x0fffffff
#define	INIT_MAX_PC	0xffff
#define	F_WIDTH		1
#define	F_WIDTH_DEF	2

#define	MIN_WEIGHT	0x01010101

#define	copy_string(src)	real_copy_string(src, TRUE)
#define	copy(src)		real_copy_string(src, FALSE)

#endif	/* _LOCALEDEF_LOCDEF_H */
