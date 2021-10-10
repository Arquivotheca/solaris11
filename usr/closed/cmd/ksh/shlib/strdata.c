#ident	"%Z%%M%	%I%	%E% SMI"	/* From AT&T Toolchest */

/*
 * Copyright 2002 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * data for string evaluator library
 */

#include	"streval.h"

const struct Optable optable[] =
	/* opcode	precedence,assignment  opname */
{
	{DEFAULT,	MAXPREC|NOASSIGN,	0177, 0 },
	{DONE,		0|NOASSIGN,		0 , 0 },
	{NEQ,		9|NOASSIGN,		'!', '=' },
	{NOT,		MAXPREC|NOASSIGN,	'!', 0 },
	{MOD,		13|NOFLOAT,		'%', 0 },
	{ANDAND,	5|NOASSIGN|SEQPOINT,	'&', '&' },
	{AND,		8|NOFLOAT,		'&', 0 },
	{LPAREN,	MAXPREC|NOASSIGN|SEQPOINT,'(', 0 },
	{RPAREN,	1|NOASSIGN,		')', 0 },
	{TIMES,		13,			'*', 0 },
#ifdef future
	{PLUSPLUS,	14|NOASSIGN|NOFLOAT|SEQPOINT, '+', '+'},
#endif
	{PLUS,		12,			'+', 0 },
#ifdef future
	{MINUSMINUS,	14|NOASSIGN|NOFLOAT|SEQPOINT, '-', '-'},
#endif
	{MINUS,		12,			'-', 0 },
	{DIVIDE,	13,			'/', 0 },
	{COLON,		2|NOASSIGN,		':', 0 },
	{LSHIFT,	11|NOFLOAT,		'<', '<' },
	{LE,		10|NOASSIGN,		'<', '=' },
	{LT,		10|NOASSIGN,		'<', 0 },
	{EQ,		9|NOASSIGN,		'=', '=' },
	{ASSIGNMENT,	2|RASSOC,		'=', 0 },
	{RSHIFT,	11|NOFLOAT,		'>', '>' },
	{GE,		10|NOASSIGN,		'>', '=' },
	{GT,		10|NOASSIGN,		'>', 0 },
#ifdef future
	{QCOLON,	3|NOASSIGN|SEQPOINT,	'?', ':' },
#endif
	{QUEST,		3|NOASSIGN|SEQPOINT|RASSOC,	'?', 0 },
	{XOR,		7|NOFLOAT,		'^', 0 },
	{OROR,		5|NOASSIGN|SEQPOINT,	'|', '|' },
	{OR,		6|NOFLOAT,		'|', 0 }
};


#ifndef KSHELL
    const char e_number[]	= "bad number";
#endif /* KSHELL */

/* TRANSLATION_NOTE
 * To be printed in arithmetic expression.
 * (e.g. "echo $(( 1 + ))" */
const char e_moretokens[]	= "more tokens expected";

/* TRANSLATION_NOTE
 * To be printed in arithmetic expression.
 * (e.g. "echo $(( \( ))") */
const char e_paren[]		= "unbalanced parenthesis";

/* TRANSLATION_NOTE
 * To be printed in arithmetic expression.
 * This message tells wrong usage of colon. */
const char e_badcolon[]		= "invalid use of :";

/* TRANSLATION_NOTE
 * To be printed in arithmetic expression.
 * (e.g. "echo $(( 1 / 0 ))") */
const char e_divzero[]		= "divide by zero";

/* TRANSLATION_NOTE
 * To be printed in arithmetic expression.
 * (e.g. "echo $(( / ))") */
const char e_synbad[]		= "syntax error";

/* TRANSLATION_NOTE
 * To be printed in arithmetic expression.
 * (e.g. "echo $(( = 2 ))") */
const char e_notlvalue[]	= "assignment requires lvalue";

/* TRANSLATION_NOTE
 * To be printed in either of:
 *	- in arithmetic expression
 *	- in "."(dot) command if recursion (dotted script dots itself)
 *	  is too deep
 *	  (e.g. name this script as `dotme.ksh' then ". dotme.ksh"
 *	  	--- cut here ---
 *		. dotme.ksh
 *		--- cut here ---
 *	  )
 *	- in executing function if recursion (function calls itself)
 *	  is too deep
 *	  (e.g. run this script
 *		--- cut here ---
 *		#!/usr/bin/ksh
 *		function me {
 *			me
 *		}
 *		me
 *		--- cut here ---
 *	  )
 */
const char e_recursive[]	= "recursion too deep";
const char e_questcolon[]	= ": expected for ? operator";
#ifdef FLOAT
    const char e_incompatible[]= "operands have incompatible types";
#endif /* FLOAT */

const char e_hdigits[] = "00112233445566778899aAbBcCdDeEfFgGhHiIjJkKlLmMnNoOpPqQrRsStTuUvVwWxXyYzZ";
