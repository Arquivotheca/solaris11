/*
 * Copyright 2003 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
%{
#pragma	ident	"%Z%%M%	%I%	%E% SMI"
%}

%{
#include "iconv_int.h"
%}

%union {
	uint64_t	llval;
	int	ival;
	char	*id;
}

%token KW_END
%token KW_CHARMAP
%token KW_WIDTH
%token KW_WIDTH_DEFAULT
%token KW_ELLIPSIS

/* tokens for meta-symbols */
%token KW_CODESET
%token KW_ESC_CHAR
%token KW_MB_CUR_MAX
%token KW_MB_CUR_MIN
%token KW_COMMENT_CHAR

/* tokens for user defined symbols, integer constants, etc... */
%token <id> SYMBOL
%token <id> STRING
%token <ival> HEX_CONST
%token <ival> CHAR_CONST
%token <ival> NUM
%token <llval> BYTES

%start charmap

%%
charmap	:
	metasymbol_assign_sect charmap_sect
	{
	}
	| metasymbol_assign_sect charmap_sect column_width_sect
	{
	}
	;

metasymbol_assign_sect	:
	metasymbol_assign metasymbol_assign_sect
	| metasymbol_assign
	;

metasymbol_assign	:
	KW_MB_CUR_MAX NUM '\n'
	{
		set_mbcurmax($2);
	}
	| KW_MB_CUR_MIN {skip_to_EOL++;} '\n'
	| KW_CODESET {skip_to_EOL++;} '\n'
	;

charmap_sect	:
	KW_CHARMAP '\n' charmap_stat_list KW_END KW_CHARMAP '\n'
	{
#ifdef	DEBUG		
		if (curr_map == FROMMAP) {
			/* frommap */
			set_frommap();
		} else {
			/* tomap */
			set_tomap();
		}
#endif
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

symbol_def	:
	SYMBOL BYTES {skip_to_EOL++;} '\n'
	{
		add_symbol_def($1, $2);
	}
	;

symbol_range_def	:
	SYMBOL KW_ELLIPSIS SYMBOL BYTES {skip_to_EOL++;} '\n'
	{
		add_symbol_range_def($1, $3, $4);
	}
	;
	
column_width_sect	:
	column_width_def
	| column_width_default_def column_width_def
	| column_width_def column_width_default_def
	;

column_width_def	:
	KW_WIDTH '\n' column_width_stat_list KW_END KW_WIDTH '\n'
	;

column_width_default_def	:
	KW_WIDTH_DEFAULT NUM '\n'
	;

column_width_stat_list	:
	column_width_stat_list column_width_stat
	| column_width_stat
	;

column_width_stat	:
	width_def
	| width_range_def
	;

width_def	:
	SYMBOL NUM {skip_to_EOL++;} '\n'
	{
		free($1);
	}
	;

width_range_def	:
	SYMBOL KW_ELLIPSIS SYMBOL NUM {skip_to_EOL++;} '\n'
	{
		free($1);
		free($3);
	}
	;
%%

void
inityacc(void)
{
}
