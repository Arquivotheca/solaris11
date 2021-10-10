/*
 * Copyright 2003 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

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
/*
 * #if !defined(lint) && !defined(_NOIDENT)
 * static char rcsid[] = "@(#)$RCSfile: scan.c,v $ $Revision: 1.3.7.8 $"
 *	" (OSF) $Date: 1992/09/14 13:53:09 $";
 * #endif
 */
/*
 * COMPONENT_NAME: (CMDLOC) Locale Database Commands
 *
 * FUNCTIONS:
 *
 *
 * (C) COPYRIGHT International Business Machines Corp. 1991
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 *
 * 1.8  com/cmd/nls/scan.c, cmdnls, bos320, 9132320m 8/11/91 14:08:48
 */

#include "iconv_int.h"
#include "iconv.tab.h"

FILE	*infp;

int	skip_to_EOL = 0;

int	yycharno = 1;
int	yylineno = 1;
int	maxbytes;
char	escape_char = '\\';
static int	instring = FALSE;
static char	comment_char = '#';
static int	peekc = 0;
static int	eol_pos = 0;

typedef struct {
#define	MAX_KW_LEN	32
	char	key[MAX_KW_LEN + 1];
	int	token_val;
} keyword_t;

static const keyword_t	kw_tbl[] = {
	"<code_set_name>",	KW_CODESET,
	"<comment_char>",	KW_COMMENT_CHAR,
	"<escape_char>",	KW_ESC_CHAR,
	"<mb_cur_max>",		KW_MB_CUR_MAX,
	"<mb_cur_min>",		KW_MB_CUR_MIN,
	"CHARMAP",		KW_CHARMAP,
	"END",			KW_END,
	"WIDTH",		KW_WIDTH,
	"WIDTH_DEFAULT",	KW_WIDTH_DEFAULT,
};

#define	KW_TBL_SZ	(sizeof (kw_tbl) / sizeof (keyword_t))

typedef enum {
	T_OCT,
	T_DEC,
	T_HEX,
	T_HEX_CONST
} constant_t;

static int	input(void);

#define	ERR_CHAR_NOT_PCS	\
	"'%c' is not a POSIX Portable Character.\n"

#define	ERR_ILL_CHAR	\
	"Illegal character '%c' in input file.\n"

#define	ERR_ILL_DEC_CONST	\
	"Illegal decimal constant '%s'.\n"

#define	ERR_ILL_OCT_CONST	\
	"Illegal octal constant '%s'.\n"

#define	ERR_ILL_HEX_CONST	\
	"Illegal hexadecimal constant '%s'.\n"

#define	ERR_LONG_INPUT	\
	"Input too long.\n"

#define	ERR_MISSING_QUOTE	\
	"Missing closing quote in string '%s'.\n"

#define	ERR_TOO_LONG_BYTES	\
	"Too long bytes.\n"

#define	ERR_ILL_CHAR_SYM	\
	"The character symbol '%s' is missing the closing '>'. \n"

#define	ERR_ESC_CHAR_MISSING	\
	"Character for escape_char statement missing.\n"

#define	ERR_COM_CHAR_MISSING	\
	"Character for <comment_char> statement missing.\n"

#define	ERR_UNKNOWN_KWD	\
	"Unrecognized keyword '%s' statement found.\n"

void
initlex(void) {
	comment_char = '#';
	escape_char = '\\';
	yycharno = 1;
	yylineno = 1;
	maxbytes = MAX_BYTES;
}

static int
getspecialchar(const char *errstr) {
	int	c;

	/* Eat white-space */
	while (isspace(c = input()) && c != '\n')
		;

	if (c != '\n') {
		if (!isascii(c)) {
			error(gettext(ERR_CHAR_NOT_PCS), c);
		}
	} else {
		error(errstr);
	}

	while (input() != '\n')
		;

	return (c);
}

static void	uninput(int);

static int
input(void)
{
	int	c;

	if (peekc != 0) {
		c = peekc;
		peekc = 0;
	} else {
		c = fgetc(infp);
		if (c == EOF) {
			return (c);
		}
	}

	do {
		/*
		 * if we see an escape char check if it's
		 * for line continuation char
		 */
		if (c == escape_char) {
			yycharno++;
			c = fgetc(infp);

			if (c == EOF) {
				error(gettext(ERR_ILL_CHAR));
			} else if (c != '\n') {
				uninput(c);
				return (escape_char);
			} else {
				yylineno++;
				eol_pos = yycharno;
				yycharno = 1;
				continue;
			}
		}

		/* eat comment */
		if (c == comment_char && yycharno == 1) {
			/*
			 * Comment character must be first on line or it's
			 * just another character.
			 */
			while ((c = fgetc(infp)) != '\n') {
				/* Don't count inside comment */
				if (c == EOF)
					return (c);
			}
		}

		if (c == '\n') {
			yylineno++;
			eol_pos = yycharno;
			/*
			 * Don't return '\n' if empty line
			 */
			if (yycharno == 1)
				continue;
			yycharno = 1;
		} else {
			yycharno++;
		}

		return (c);
	} while ((c = fgetc(infp)) != EOF);
	return (c);
}

static void
uninput(int c)
{
	int	t;

	if (peekc != 0) {		/* already one char pushed back */
		(void) ungetc(peekc, infp);
		t = peekc;
		peekc = c;
		c = t;
	} else {
		peekc = c;
	}

	if (c != '\n') {
		yycharno--;
	} else {
		yylineno--;
		yycharno = eol_pos;
	}
}

static int
geteval(constant_t type, const char *buf)
{
	const char	*p = buf;
	int	val = 0;

	switch (type) {
	case T_OCT:
		while (*p != '\0') {
			val = val * 8 + *p - '0';
			p++;
		}
		if (val > 255) {
			error(gettext(ERR_ILL_OCT_CONST), buf);
		}
		break;

	case T_DEC:
		while (*p != '\0') {
			val = val * 10 + *p - '0';
			p++;
		}
		if (val > 255) {
			error(gettext(ERR_ILL_DEC_CONST), buf);
		}
		break;

	case T_HEX:
	case T_HEX_CONST:
		while (*p != '\0') {
			if (isdigit(*p)) {
				val = val * 16 + *p - '0';
			} else {
				val = val * 16 + tolower(*p) - 'a' + 10;
			}
			p++;
		}
		if (type == T_HEX && val > 255) {
			error(gettext(ERR_ILL_HEX_CONST), buf);
		}
		break;

	default:
		INTERNAL_ERROR;
		/* NOTREACHED */
	}

	return (val);
}

#define	BUFFER_SIZE	8192
#define	BUFCHECK	\
	{\
		if (pos == BUFFER_SIZE - 1) {\
			error(gettext(ERR_LONG_INPUT));\
		}\
	}

int
yylex(void)
{
	int	c;
	int	pos;
	static char	buf[BUFFER_SIZE];


	if (skip_to_EOL) {
		/*
		 * Ignore all text til we reach a new line.
		 * (For example, in char maps, after the <encoding>
		 */
		skip_to_EOL = 0;

		while ((c = input()) != EOF) {
			if (c == '\n')
				return (c);
		}
		return (EOF);
	}

	while ((c = input()) != EOF) {
		if (instring == TRUE) {
			uninput(c);
			pos = 0;
			while ((c = input()) != '"' && c != '\n' && c != EOF) {
				if (c == escape_char) {
					BUFCHECK;
					buf[pos++] = escape_char;
					if ((c = input()) != EOF) {
						BUFCHECK;
						buf[pos++] = (char)c;
					} else {
						break;
					}
				} else {
					BUFCHECK;
					buf[pos++] = (char)c;
				}
			}
			buf[pos] = '\0';

			if (c == '"')
				uninput(c);
			if (c == '\n' || c == EOF) {
				error(gettext(ERR_MISSING_QUOTE), buf);
			}

			instring = FALSE;
			yylval.id = STRDUP(buf);
			return (STRING);
		}

		/*
		 * [0-9]+
		 * | [-+][0-9]+
		 */
		if (isdigit(c) || c == '-' || c == '+') {
			int	hex_number = 0;
			int	sign = 1;

			pos = 0;
			if (c == '-') {
				sign = -1;
			} else if (c == '+') {
				sign = 1;
			} else {
				buf[pos++] = (char)c;
			}
			if (c == '0' && ((c = input()) != EOF)) {
				if (c == 'x' || c == 'X') {
					hex_number = 1;
					buf[pos++] = (char)c;
				} else {
					uninput(c);
				}
			}
			while ((c = input()) != EOF) {
				if (hex_number) {
					if (isxdigit(c)) {
						BUFCHECK;
						buf[pos++] = (char)c;
					} else {
						break;
					}
				} else {
					if (isdigit(c)) {
						BUFCHECK;
						buf[pos++] = (char)c;
					} else {
						break;
					}
				}
			}
			buf[pos] = '\0';

			if ((c != EOF && !isspace(c)) || c == '\n') {
				uninput(c);
			}

			if (pos == 0) {
				error(gettext(ERR_ILL_DEC_CONST),
				    sign == 1 ? "+" : "-");
			} else if (pos == 1) {
				yylval.ival = sign * geteval(T_HEX, buf);
				return (NUM);
			} else if (pos > 1) {
				if ((hex_number && (pos == 2 || pos > 10)) ||
				    hex_number == 0) {
					yylval.id = STRDUP(buf);
					return (STRING);
				}
				yylval.ival = geteval(T_HEX_CONST, &buf[2]);
				return (HEX_CONST);
			}
		}

		/*
		 * '\\'
		 * \\d[0-9]{1,3}
		 * \\0[0-8]{1,3}
		 * \\[xX][0-9a-fA-F]{2}
		 * \\[^xX0d]
		 */
		if (c == escape_char) {
			uint64_t	value;
			int	byte_cnt;

			value = 0;
			byte_cnt = 1;
			do {
				c = input();
				pos = 0;

				switch (c) {
				case 'd': /* decimal constant */
					while (isdigit((c = input()))) {
						BUFCHECK;
						buf[pos++] = (char)c;
					}
					buf[pos] = '\0';
					if ((c != EOF && !isspace(c)) ||
					    c == '\n') {
						uninput(c);
					}
					/* check number of digits in */
					/* decimal constant */
					if (pos != 2 && pos != 3) {
						error(
						    gettext(ERR_ILL_DEC_CONST),
							buf);
					}
					if (byte_cnt > maxbytes) {
error(gettext(ERR_TOO_LONG_BYTES));
					} else {
						byte_cnt++;
						value <<= 8;
						value += (uint64_t)
						    geteval(T_DEC, buf);
					}
					continue;

				case 'x':
				case 'X': /* hex constant */
					while (isxdigit((c = input()))) {
						BUFCHECK;
						buf[pos++] = (char)c;
					}
					buf[pos] = '\0';
					if ((c != EOF && !isspace(c)) ||
					    c == '\n') {
						uninput(c);
					}
					if (pos != 2) {
						/* wrong number of digits in */
						/* hex const */
						error(
						    gettext(ERR_ILL_HEX_CONST),
							buf);
					}
					if (byte_cnt > maxbytes) {
error(gettext(ERR_TOO_LONG_BYTES));
					} else {
						byte_cnt++;
						value <<= 8;
						value += (uint64_t)
						    geteval(T_HEX, buf);
					}
					continue;

				default:
					if (!isdigit(c)) {
						if (byte_cnt == 1) {
							/*
							 * single quoted
							 * character
							 */
							yylval.ival =
							    (unsigned char)c;
							return (CHAR_CONST);
						}
						uninput(c);
						uninput(escape_char);
						goto EndByteString;
					}
					buf[pos++] = (char)c;
					if (c == '8' || c == '9') {
						while (isdigit((c = input()))) {
							/*
							 * invalid oct
							 */
							BUFCHECK;
							buf[pos++] = (char)c;
						}
						buf[pos] = '\0';
						error(
						    gettext(ERR_ILL_OCT_CONST),
							buf);
						if ((c != EOF && !isspace(c)) ||
						    c == '\n') {
							uninput(c);
						}
						if (byte_cnt > maxbytes) {
error(gettext(ERR_TOO_LONG_BYTES));
						} else {
							byte_cnt++;
							value <<= 8;
							value += (uint64_t)
							    geteval(T_DEC, buf);
						}
						continue;
					}
					while ((c = input()) >= '0' &&
					    c <= '7') {
						BUFCHECK;
						buf[pos++] = (char)c;
					}
					if ((c != EOF && !isspace(c)) ||
					    c == '\n') {
						uninput(c);
					}
					/*
					 * check number of digits
					 * in octal constant
					 */
					if (pos != 2 && pos != 3) {
						/*
						 * too many digits in
						 * octal constant
						 */
						error(
						    gettext(ERR_ILL_OCT_CONST),
							buf);
					}
					if (byte_cnt > maxbytes) {
error(gettext(ERR_TOO_LONG_BYTES));
					} else {
						byte_cnt++;
						value <<= 8;
						value += (uint64_t)
						    geteval(T_OCT, buf);
					}
					continue;
				}
			} while ((c = input()) == escape_char);

			if ((c != EOF && !isspace(c)) || c == '\n') {
				uninput(c);
			}

EndByteString:
			yylval.llval = value;
			return (BYTES);
		}


		/*
		 * symbol for character names - or keyword:
		 *
		 * < [:isgraph:]+ >
		 */
		if (c == '<') {
			keyword_t	*kw;

			pos = 0;
			buf[pos++] = (char)c;

			do {
				c = input();
				BUFCHECK;
				if (c == escape_char) {
					buf[pos++] = (char)input();
				} else {
					buf[pos++] = (char)c;
				}
			} while (c != '>' && isgraph(c));
			buf[pos] = '\0';
			if (c != '>') {
				uninput(c);
				buf[pos - 1] = '\0';
				error(gettext(ERR_ILL_CHAR_SYM), buf);
			}

			/* look for one of special 'meta-symbols' */
			kw = bsearch(buf, kw_tbl,
			    KW_TBL_SZ, sizeof (keyword_t),
			    (int (*)(const void *, const void *))strcmp);
			if (kw != NULL) {
				/* check for escape character replacement. */
				if (kw->token_val == KW_ESC_CHAR) {
					escape_char = (char)getspecialchar(
					    gettext(ERR_ESC_CHAR_MISSING));
					continue;
				} else if (kw->token_val == KW_COMMENT_CHAR) {
					comment_char = (char)getspecialchar(
					    gettext(ERR_COM_CHAR_MISSING));
					continue;
				}
				return (kw->token_val);
			}
			yylval.id = STRDUP(buf);
			return (SYMBOL);
		}

		/*
		 * symbol for character class names - or keyword.
		 *
		 * [:alpha:_]+[:digit::alpha:_-]
		 */
		if (isalpha(c) || c == '_') {
			keyword_t	*kw;

			pos = 0;
			buf[pos++] = (char)c;
			while (isalnum((c = input())) || c == '_' ||
			    c == '-' || c == '@') {
				BUFCHECK;
				buf[pos++] = (char)c;
			}
			buf[pos] = '\0';

			if ((c != EOF && !isspace(c)) || c == '\n') {
				uninput(c);
			}

			kw = bsearch(buf, kw_tbl,
			    KW_TBL_SZ, sizeof (keyword_t),
			    (int (*)(const void *, const void *))strcmp);

			if (kw != NULL) {
				return (kw->token_val);
			}

			yylval.id = STRDUP(buf);
			return (SYMBOL);
		}

		if (c == '.') {
			pos = 0;
			buf[pos++] = (char)c;
			c = input();
			buf[pos++] = (char)c;

			if (c != '.') {
				uninput(c);
				yylval.ival = (unsigned char)c;
				return (CHAR_CONST);
			}

			c = input();
			buf[pos++] = (char)c;
			if (c == '.') {
				return (KW_ELLIPSIS);
			} else {
				buf[pos] = '\0';
				error(gettext(ERR_UNKNOWN_KWD), buf);
			}
		}

		/*
		 * The newline is this grammer statement terminator - yuk!
		 */
		if (c == '\n') {
			return (c);
		}

		if (isspace(c)) {
			continue;
		}

		if (c == ';' || c == '(' || c == ')' ||
		    c == ',' || c == ':' || c == '=' ||
		    c == '"') {
			return (c);
		}

		if (isascii(c) && isprint(c)) {
			yylval.ival = (unsigned char)c;
			return (CHAR_CONST);
		}

		error(gettext(ERR_ILL_CHAR), c);
	}

	return (EOF);
}
