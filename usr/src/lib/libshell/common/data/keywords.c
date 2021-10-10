/***********************************************************************
*                                                                      *
*               This software is part of the ast package               *
*          Copyright (c) 1982-2011 AT&T Intellectual Property          *
*                      and is licensed under the                       *
*                  Common Public License, Version 1.0                  *
*                    by AT&T Intellectual Property                     *
*                                                                      *
*                A copy of the License is available at                 *
*            http://www.opensource.org/licenses/cpl1.0.txt             *
*         (with md5 checksum 059e8cd6165cb4c31e351f2b69388fd9)         *
*                                                                      *
*              Information and Software Systems Research               *
*                            AT&T Research                             *
*                           Florham Park NJ                            *
*                                                                      *
*                  David Korn <dgk@research.att.com>                   *
*                                                                      *
***********************************************************************/
#pragma prototyped
#include	"shtable.h"
#include	<shell.h>
#include	"shlex.h"
#include	"FEATURE/options"

/*
 * table of reserved words in shell language
 * This list must be in in ascii sorted order
 */

const Shtable_t shtab_reserved[] =
{
		"!",		NOTSYM,
		"[[",		BTESTSYM,
		"case",		CASESYM,
		"do",		DOSYM,
		"done",		DONESYM,
		"elif",		ELIFSYM,
		"else",		ELSESYM,
		"esac",		ESACSYM,
		"fi",		FISYM,
		"for",		FORSYM,
		"function",	FUNCTSYM,
		"if",		IFSYM,
		"in",		INSYM,
#if SHOPT_NAMESPACE
		"namespace",	NSPACESYM,
#endif /* SHOPT_NAMESPACE */
		"select",	SELECTSYM,
		"then",		THENSYM,
		"time",		TIMESYM,
		"until",	UNTILSYM,
		"while",	WHILESYM,
		"{",		LBRACE,
		"}",		RBRACE,
		"",		0,
};

const char	e_unexpected[]	= "unexpected";
const char	e_unmatched[]	= "unmatched";
const char	e_endoffile[]	= "end of file";
const char	e_newline[]	= "newline";

