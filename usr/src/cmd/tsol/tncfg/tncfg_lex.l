%{
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <string.h>
#include <libintl.h>
#include "tncfg.h"
#include "tncfg_grammar.tab.h"

int lex_lineno = 1;	/* line number for error reporting */
static int state = INITIAL;
extern boolean_t cmd_file_mode;
extern boolean_t saw_error;
extern void yyerror(char *s);
char *safe_strdup(char *s);
%}

%a 7000
%p 5000
%e 2000
%n 1000

%{
/*
 * The two states below are for tokens and lists property values.
 * Note that simple property values are a subset of tokens.
 */
%}
%s TSTATE
%s LSTATE
%%

<INITIAL>"#"[^\n]*	{ }

<INITIAL>add	{
			BEGIN TSTATE;
			state = TSTATE;
			return ADD;
		}

<INITIAL>commit	{
			BEGIN TSTATE;
			state = TSTATE;
			return COMMIT;
		}

<INITIAL>delete {
			BEGIN TSTATE;
			state = TSTATE;
			return DELETE;
		}

<INITIAL>exit	{
			BEGIN TSTATE;
			state = TSTATE;
			return EXIT;
		}

<INITIAL>export	{
			BEGIN TSTATE;
			state = TSTATE;
			return EXPORT;
		}

<INITIAL>"?"|help {
			BEGIN TSTATE;
			state = TSTATE;
			return HELP;
		}

<INITIAL>info	{
			BEGIN TSTATE;
			state = TSTATE;
			return INFO;
		}

<INITIAL>remove	{
			BEGIN TSTATE;
			state = TSTATE;
			return REMOVE;
		}

<INITIAL>revert	{
			BEGIN TSTATE;
			state = TSTATE;
			return REVERT;
		}

<INITIAL>set {
			BEGIN TSTATE;
			state = TSTATE;
			return SET;
		}

<INITIAL>clear {
			BEGIN TSTATE;
			state = TSTATE;
			return CLEAR;
		}

<INITIAL>verify	{
			BEGIN TSTATE;
			state = TSTATE;
			return VERIFY;
		}

<INITIAL>list	{
			BEGIN TSTATE;
			state = TSTATE;
			return LIST;
		}

<INITIAL>get	{
			BEGIN TSTATE;
			state = TSTATE;
			return GET;
		}

<TSTATE>name	{ return TPNAME; }

<TSTATE>doi	{ return DOI; }

<TSTATE>host_type	{ return HOSTTYPE; }

<TSTATE>def_label	{ return DEFLABEL; }

<TSTATE>host		{ return HOSTADDR; }

<TSTATE>min_label	{ return MINLABEL; }

<TSTATE>max_label	{ return MAXLABEL; }

<TSTATE>aux_label	{ return LABELSET; }

<TSTATE>visible		{ return VISIBLE; }

<TSTATE>label		{ return ZONELABEL; }

<TSTATE>mlp_private	{ return MLP_PRIVATE; }

<TSTATE>mlp_shared	{ return MLP_SHARED; }

<TSTATE>=	{ return EQUAL; }
<LSTATE>=	{ return EQUAL; }

<TSTATE>"["	{
			BEGIN LSTATE;
			state = LSTATE;
			return OPEN_SQ_BRACKET;
		}

<LSTATE>"]"	{
			BEGIN TSTATE;
			state = TSTATE;
			return CLOSE_SQ_BRACKET;
		}

<LSTATE>","	{ return COMMA; }


<TSTATE>[^ \t\n\";=\[\]\(\)]+	{
			yylval.strval = safe_strdup(yytext);
			return TOKEN;
		}

<LSTATE>[^ \t\n\",;=\[\]\(\)]+	{
			yylval.strval = safe_strdup(yytext);
			return TOKEN;
		}

<TSTATE>\"[^\"\n]*[\"\n] {
			yylval.strval = safe_strdup(yytext + 1);
			if (yylval.strval[yyleng - 2] == '"')
				yylval.strval[yyleng - 2] = 0;
			return TOKEN;
		}

<LSTATE>\"[^\"\n]*[\"\n] {
			yylval.strval = safe_strdup(yytext + 1);
			if (yylval.strval[yyleng - 2] == '"')
				yylval.strval[yyleng - 2] = 0;
			return TOKEN;
		}

";"		{
			BEGIN INITIAL;
			return (yytext[0]);
		}

\n		{
			lex_lineno++;
			BEGIN INITIAL;
			return (yytext[0]);
		}

[ \t]		;	/* Ignore whitespace */

.		{
			return (yytext[0]);
		}

%%

char *
safe_strdup(char *s)
{
	char *result;

	if ((result = strdup(s)) == NULL) {
		yyerror("Out of memory");
		exit(Z_ERR);
	}
	return (result);
}

void
yyerror(char *s)
{
	/* feof(yyin) is not an error; anything else is, so we set saw_error */
	if (yytext[0] == '\0') {
		if (!feof(yyin)) {
			saw_error = B_TRUE;
			(void) fprintf(stderr, gettext("%s, token expected\n"),
			    s);
		}
		return;
	}

	saw_error = B_TRUE;
	if (cmd_file_mode)
		(void) fprintf(stderr, gettext("%s on line %d at '%s'\n"), s,
		    lex_lineno, (yytext[0] == '\n') ? "\\n" : yytext);
	else
		(void) fprintf(stderr, gettext("%s at '%s'\n"), s,
		    (yytext[0] == '\n') ? "\\n" : yytext);
	usage(B_FALSE, CMD_HELP, HELP_SUBCMDS);
}
