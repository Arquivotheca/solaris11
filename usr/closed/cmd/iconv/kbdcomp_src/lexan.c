/*
 * Copyright (c) 1997, by Sun Microsystems, Inc.
 * All rights reserved.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/


#ident	"%Z%%M%	%I%	%E% SMI"	/* SVr4.0 k18.2   */


#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include "y.tab.h"

#define	ACCEPT	0	/* ? */
#define	MAXSTR	512
#define	NULLOP	0


char textline[MAXSTR];
int textptr;
int linnum = 1;

extern int nerrors;
extern FILE *lexfp;

static int	tokenable(int);
static int	eat3(void);
static int	screen(void);
static void	getstring(void);
static int	getchcon(void);

int
yylex(void)	/* lexical analyzer */
{
	int c;

	while (1) {
		textptr = 0;
		if ((c = getc(lexfp)) == EOF) {
			return (ACCEPT);
		}
morecomment:
		while (c == ' ' || c == '\n' || c == '\t') {
			if (c == '\n') {
				++linnum;
			}
			c = getc(lexfp);
		}
		if (c == EOF)
			return (ACCEPT);
		if (c == '#') {
			while ((c = getc(lexfp)) != '\n' && c != EOF)
				;
			if (c == EOF)
				return (ACCEPT);
			goto morecomment;
		}

		if (tokenable(c)) {
moretoken:
			textline[textptr++] = c;
			while (tokenable(c = getc(lexfp)) && (c != EOF)) {
				textline[textptr++] = c;
			}
			if (c == '\\') {
				c = eat3();
				goto moretoken;
			}
			ungetc(c, lexfp);
			textline[textptr] = 0;
			return (screen());
		}
		switch (c) {
		case '(':
			return ((int)'(');
		case ')':
			return ((int)')');
		case '{':
			return ((int)'{');
		case '}':
			return ((int)'}');
		case '"':
			ungetc(c, lexfp);
			getstring();
			return (STRING);
		case '\'':
			textline[textptr] = getchcon();
			if ((c = getc(lexfp)) != '\'') {
				fprintf(stderr, gettxt("kbdcomp:61",
"Non-terminated character constant, line %d\n"),
					linnum);
				ungetc(c, lexfp);
				++nerrors;
			}
			++textptr; textline[textptr++] = 0;
			return (STRING);
		case '\\':
			ungetc(c, lexfp);
			c = getchcon();
			goto moretoken;
		case ' ':
		case '\b':
		case '\f':
		case '\t':
		case '\n': ungetc(c, lexfp); break;
		default:
			if (c != EOF) {
				fprintf(stderr, gettxt("kbdcomp:62",
"Unknown delimiter on line %d\n"), linnum);
				nerrors++;
			}
			break;
		}
	}
}

/*
 * getstring	Get the stuff between double quotes, return as one thing.
 */

static void
getstring(void)
{
	char *s;
	int tmp, c, warned, began;

	s = textline;
	tmp = warned = 0;
	began = linnum;	/* line number we started on, in case it gets bumped */
	c = getc(lexfp);	/* get "; throw it away! */
	while ((c = getc(lexfp)) != '"') {
		switch (c) {
		case '\\':
			if ((c = getc(lexfp)) == '\n') {
				ungetc(c, lexfp);
				++linnum;
			} else {
				ungetc(c, lexfp);
				*s++ = eat3();
			}
			++tmp;
			break;

		case '\n':
			*s++ = '\n';
			++tmp;
			if (!warned)
				fprintf(stderr, gettxt("kbdcomp:63",
					"Warning: string contains a newline at "
					"line %d (continuing)\n"), linnum);
			warned = 1;
			++linnum;
			break;

		case EOF:
			fprintf(stderr, gettxt("kbdcomp:64",
"Unexpected EOF in string beginning on line %d\n"),
				began);
			exit(1);

		default:
			*s++ = c;
			++tmp;
		}
		if (tmp > (MAXSTR-2)) {
			fprintf(stderr, gettxt("kbdcomp:65",
"Warning: string length > %d chars (continuing)\n"),
				MAXSTR-2);
			while ((c = getc(lexfp)) && c != EOF)
				;
		}
	}
	*s++ = '\0';
}

/*
 * getchcon	Get a character constant, put in textline.  It's
 *		either 'char' or '\xxx'.
 */

static int
getchcon(void)
{
	int c;

	if ((c = getc(lexfp)) == '\\')
		return (eat3());
	else {
		/*
		 * ''' is legal, just odd...
		 */
		if ((c == '\'') || (c < (int) ' '))
			fprintf(stderr, gettxt("kbdcomp:66",
"Warning: non-standard constant, line %d\n"), linnum);
		return (c);
	}
}


static int
eat3(void)	/* eat a 3-digit octal constant, we have seen the backslash */
{
	int i, c, tmp;

	tmp = 0;
	switch (c = getc(lexfp)) {
	case '\'': return ('\'');
	case '"': return ('"');
	case 'n': return ('\n');
	case 't': return ('\t');
	case 'f': return ('\f');
	case 'b': return ('\b');
	case '\\': return ('\\');
	default:
		if (c >= '0' && c <= '9') {	/* must be 3 digit */
			ungetc(c, lexfp);
			break;
		} else
			return (c);	/* a backslashed thing */
	}
	for (i = 0; i < 3; i++) {
		if ((c = getc(lexfp)) != EOF) {
			tmp = (tmp << 3) | (c - '0');
		}
	}
	return (tmp);
}


struct resword {
	char *res;
	int  ires;
};

struct resword reserved[] = {
	{ "map", MAP },
	{ "sparse", SPARSE },
	{ "full", FULL },
	{ "define", DEFINE },
			/*	{ "key", KEY }, <-- obsolete keyword */
	{ "keylist", KEYLIST },
	{ "error", NERROR },
	{ "timed", TIMED },
	{ "link", LINK },
	{ "strlist", STRLIST },
	{ "extern", XTERN },	/* RESERVED word for future release */
	{ "\0", NULLOP }
};

/*
 * screen - check to see if something is a reserved word or not.  If not,
 * it's a "string".
 */

static int
screen(void)
{
	char *s, *t;
	int i;

	i = 0;
	s = reserved[i].res;
	while (*s) {
		if (strcmp(textline, s) == 0) {
			return (reserved[i].ires);
		}
		s = reserved[++i].res;
	}
	return (STRING);
}

/*
 * Return whether something can be part of a token or not.  The brackets
 * and spaces used as delimiters cannot be parts of tokens, neither can
 * "control" characters; everything else is O.K.
 */

static int
tokenable(int c)
{
	if ((c & 0x7F) < ' ')
		return (0);
	switch (c) {
	case '\n':	/* newline */
	case '\t':	/* tab */
	case ' ':	/* space */
	case '"':	/* double-quote */
	case '\'':	/* single-quote */
	case '(':	/* left paren */
	case ')':	/* right paren */
	case '\\':	/* backslash */
	case '{':	/* left curly */
	case '}':	/* right curly */
		return (0);	/* can't be used in token */
	default:
		return (1);	/* can be used in token */
	}
}
