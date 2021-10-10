/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
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
#include "locdef.h"
#include "y.tab.h"

FILE	*infp;			/* read from this stream */

int	skip_to_EOL = 0;		/* Flag to ignore characters til EOL */

#define	MAX_KW_LEN  32

typedef struct {
    char key[MAX_KW_LEN+1];
    int  token_val;
} keyword_t;

int	instring = FALSE;

/*
 * Keywords for lexical scan.  THIS TABLE MUST BE IN ASCII ALPHABETIC
 * ORDER!!!  bsearch() is used to look things up in it.
 */
static const
keyword_t kw_tbl[] = {
"<code_set_name>",	KW_CODESET,
"<comment_char>",	KW_COMMENT_CHAR,
"<escape_char>",	KW_ESC_CHAR,
"<mb_cur_max>",		KW_MB_CUR_MAX,
"<mb_cur_min>",		KW_MB_CUR_MIN,
"CHARMAP",			KW_CHARMAP,
"END",				KW_END,
"LCBIND",			KW_LCBIND,
"LC_COLLATE",		KW_LC_COLLATE,
"LC_CTYPE",			KW_LC_CTYPE,
"LC_MESSAGES",		KW_LC_MSG,
"LC_MONETARY",		KW_LC_MONETARY,
"LC_NUMERIC",		KW_LC_NUMERIC,
"LC_TIME",			KW_LC_TIME,
"METHODS",			KW_METHODS,
"WIDTH",			KW_WIDTH,
"WIDTH_DEFAULT",	KW_WIDTH_DEFAULT,
"abday",			KW_ABDAY,
"abmon",			KW_ABMON,
"alt_digits",		KW_ALT_DIGITS,
"am_pm",			KW_AM_PM,
"backward",			KW_BACKWARD,
"btowc",			KW_BTOWC,
"btowc@native",		KW_BTOWC_AT_NATIVE,
"charclass",		KW_CHARCLASS,
"chartrans",		KW_CHARTRANS,
"collating-element",	KW_COLLATING_ELEMENT,
"collating-symbol",	KW_COLLATING_SYMBOL,
"copy",				KW_COPY,
"cswidth",			KW_CSWIDTH,
"currency_symbol",	KW_CURRENCY_SYMBOL,
"d_fmt",			KW_D_FMT,
"d_t_fmt",			KW_D_T_FMT,
"date_fmt",			KW_DATE_FMT,
"day",				KW_DAY,
"decimal_point",	KW_DECIMAL_POINT,
"dense",			KW_DENSE,
"era",				KW_ERA,
"era_d_fmt",		KW_ERA_D_FMT,
"era_d_t_fmt",		KW_ERA_D_T_FMT,
"era_t_fmt",		KW_ERA_T_FMT,
"era_year",			KW_ERA_YEAR,
"euc",				KW_EUC,
"eucpctowc",		KW_EUCPCTOWC,
"fgetwc",			KW_FGETWC,
"fgetwc@native",	KW_FGETWC_AT_NATIVE,
"file_code",		KW_FILE_CODE,
"fnmatch",			KW_FNMATCH,
"forward",			KW_FORWARD,
"frac_digits",		KW_FRAC_DIGITS,
"from",				KW_FROM,
"getdate",			KW_GETDATE,
"grouping",			KW_GROUPING,
"int_curr_symbol",	KW_INT_CURR_SYMBOL,
"int_frac_digits",	KW_INT_FRAC_DIGITS,
"int_n_cs_precedes",	KW_INT_N_CS_PRECEDES,
"int_n_sep_by_space",	KW_INT_N_SEP_BY_SPACE,
"int_n_sign_posn",	KW_INT_N_SIGN_POSN,
"int_p_cs_precedes",	KW_INT_P_CS_PRECEDES,
"int_p_sep_by_space",	KW_INT_P_SEP_BY_SPACE,
"int_p_sign_posn",	KW_INT_P_SIGN_POSN,
"iswctype",			KW_ISWCTYPE,
"iswctype@native",	KW_ISWCTYPE_AT_NATIVE,
"mbftowc",			KW_MBFTOWC,
"mbftowc@native",	KW_MBFTOWC_AT_NATIVE,
"mblen",			KW_MBLEN,
"mbrlen",			KW_MBRLEN,
"mbrtowc",			KW_MBRTOWC,
"mbrtowc@native",	KW_MBRTOWC_AT_NATIVE,
"mbsinit",			KW_MBSINIT,
"mbsrtowcs",		KW_MBSRTOWCS,
"mbsrtowcs@native",	KW_MBSRTOWCS_AT_NATIVE,
"mbstowcs",			KW_MBSTOWCS,
"mbstowcs@native",	KW_MBSTOWCS_AT_NATIVE,
"mbtowc",			KW_MBTOWC,
"mbtowc@native",	KW_MBTOWC_AT_NATIVE,
"mon",				KW_MON,
"mon_decimal_point",	KW_MON_DECIMAL_POINT,
"mon_grouping",		KW_MON_GROUPING,
"mon_thousands_sep",	KW_MON_THOUSANDS_SEP,
"n_cs_precedes",	KW_N_CS_PRECEDES,
"n_sep_by_space",	KW_N_SEP_BY_SPACE,
"n_sign_posn",		KW_N_SIGN_POSN,
"negative_sign",	KW_NEGATIVE_SIGN,
"no-substitute",	KW_NO_SUBSTITUTE,
"noexpr",			KW_NOEXPR,
"nostr",			KW_NOSTR,
"order_end",		KW_ORDER_END,
"order_start",		KW_ORDER_START,
"other",			KW_OTHER,
"p_cs_precedes",	KW_P_CS_PRECEDES,
"p_sep_by_space",	KW_P_SEP_BY_SPACE,
"p_sign_posn",		KW_P_SIGN_POSN,
"position",			KW_POSITION,
"positive_sign",	KW_POSITIVE_SIGN,
"process_code",		KW_PROCESS_CODE,
"regcomp",			KW_REGCOMP,
"regerror",			KW_REGERROR,
"regexec",			KW_REGEXEC,
"regfree",			KW_REGFREE,
"strcoll",			KW_STRCOLL,
"strfmon",			KW_STRFMON,
"strftime",			KW_STRFTIME,
"strptime",			KW_STRPTIME,
"strxfrm",			KW_STRXFRM,
"t_fmt",			KW_T_FMT,
"t_fmt_ampm",		KW_T_FMT_AMPM,
"thousands_sep",	KW_THOUSANDS_SEP,
"towctrans",		KW_TOWCTRANS,
"towctrans@native",	KW_TOWCTRANS_AT_NATIVE,
"towlower",			KW_TOWLOWER,
"towlower@native",	KW_TOWLOWER_AT_NATIVE,
"towupper",			KW_TOWUPPER,
"towupper@native",	KW_TOWUPPER_AT_NATIVE,
"trwctype",			KW_TRWCTYPE,
"ucs4",				KW_UCS4,
"utf8",				KW_UTF8,
"wcrtomb",			KW_WCRTOMB,
"wcrtomb@native",	KW_WCRTOMB_AT_NATIVE,
"wcscoll",			KW_WCSCOLL,
"wcscoll@native",	KW_WCSCOLL_AT_NATIVE,
"wcsftime",			KW_WCSFTIME,
"wcsrtombs",		KW_WCSRTOMBS,
"wcsrtombs@native",	KW_WCSRTOMBS_AT_NATIVE,
"wcstombs",			KW_WCSTOMBS,
"wcstombs@native",	KW_WCSTOMBS_AT_NATIVE,
"wcswidth",			KW_WCSWIDTH,
"wcswidth@native",	KW_WCSWIDTH_AT_NATIVE,
"wcsxfrm",			KW_WCSXFRM,
"wcsxfrm@native",	KW_WCSXFRM_AT_NATIVE,
"wctob",			KW_WCTOB,
"wctob@native",		KW_WCTOB_AT_NATIVE,
"wctoeucpc",		KW_WCTOEUCPC,
"wctomb",			KW_WCTOMB,
"wctomb@native",	KW_WCTOMB_AT_NATIVE,
"wctrans",			KW_WCTRANS,
"wctype",			KW_WCTYPE,
"wcwidth",			KW_WCWIDTH,
"wcwidth@native",	KW_WCWIDTH_AT_NATIVE,
"with",				KW_WITH,
"yesexpr",			KW_YESEXPR,
"yesstr",			KW_YESSTR,
};

#define	KW_TBL_SZ  (sizeof (kw_tbl) / sizeof (keyword_t))

typedef enum {
	T_OCT,
	T_DEC,
	T_HEX,
	T_HEX_CONST
} constant_t;

static int	input(void);

/*
 * SYM [0-9A-Za-z_-]
 * LTR [a-zA-Z_-]
 * HEX [0-9a-fA-F]
 */

/* character position tracking for error messages */
int	yycharno = 1;
int	yylineno = 1;

char	escape_char = '\\';
static char	comment_char = '#';
static int	seenfirst = 0;
				/*
				 * Once LC_<category> seen, the
				 * escape_char and comment_char cmds are
				 * no longer permitted
				 */

static int	eol_pos = 0;
static int	peekc = 0;	/* One-character pushback (beyond stdio) */
static char	locale_name[PATH_MAX + 1];	/* for copy directive */
static int	copy_p = 0;		/* for copy directive */

void
initlex(void) {
	comment_char = '#';
	escape_char = '\\';

	yycharno = 1;
	yylineno = 1;
}

static int
getspecialchar(const char *errstr) {
	int	c;

	/* Eat white-space */
	while (isspace(c = input()) && c != '\n')
		;

	if (c != '\n') {
		if (!isascii(c)) {
			diag_error(gettext(ERR_CHAR_NOT_PCS), c);
		}
	} else {
		diag_error(errstr);
	}

	while (input() != '\n')
		;

	return (c);
}

static void uninput(int);

static int
input(void)
{
	int c;

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
				diag_error(gettext(ERR_UNEXPECTED_EOF));
				return (c);
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
	int  t;

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
			diag_error(gettext(ERR_ILL_OCT_CONST), buf);
			val = 0;
		}
		break;

	case T_DEC:
		while (*p != '\0') {
			val = val * 10 + *p - '0';
			p++;
		}
		if (val > 255) {
			diag_error(gettext(ERR_ILL_DEC_CONST), buf);
			val = 0;
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
			diag_error(gettext(ERR_ILL_HEX_CONST), buf);
			val = 0;
		}
		break;

	default:
		INTERNAL_ERROR;
		/* NOTREACHED */
	}

	return (val);
}

#ifdef DEBUG
#define	RETURN(t) \
	{ \
		switch (t) { \
		case CHAR_CONST: \
			(void) fprintf(stderr, "CHAR '%c'\n", yylval.ival);\
			break; \
		case N_SYMBOL: \
			(void) fprintf(stderr, "N_SYMB %s\n", yylval.sym.id);\
			break; \
		case U_SYMBOL: \
			(void) fprintf(stderr, "U_SYMB %s\n", yylval.sym.id);\
			(void) fprintf(stderr, "U_SYMB 0x%llx\n",\
				yylval.sym.ucs);\
			break; \
		case STRING: \
			(void) fprintf(stderr, "STR %s\n", yylval.id);\
			break; \
		case NUM: \
			(void) fprintf(stderr, "NUM 0x%llx\n", yylval.llval);\
			break;\
		case EOF: \
			(void) fprintf(stderr, "EOF\n"); break; \
		default: \
			{ \
				if (isascii(t)) { \
					(void) fprintf(stderr, "'%c'\n", t); \
				} else { \
					(void) fprintf(stderr, "KEYW %s\n", \
					    buf); \
				} \
			} \
		} \
		return (t); \
	}


#else
#define	RETURN(t)	return (t)
#endif /* DEBUG */

#define	BUFFER_SIZE	8192
#define	BUFCHECK	\
	{\
		if (pos == BUFFER_SIZE - 1) {\
			error(2, gettext(ERR_LONG_INPUT));\
		}\
	}

int
yylex(void)
{
	int c;
	extern struct lcbind_table *Lcbind_Table;
	extern _LC_ctype_t ctype;
	int	pos;
	static char	buf[BUFFER_SIZE];

	/*
	 * If we have just processed a 'copy' keyword:
	 *   locale_name[0] == '"'  - let the quoted string routine handle it
	 *   locale_name[0] == '\0' - We hit and EOF while getting the locale
	 *   else
	 *    we have an unquoted locale name which we already read
	 */
	if (copy_p && locale_name[0] != '"') {
		copy_p = 0;
		if (locale_name[0] == '\0')
			RETURN(EOF);

		yylval.id = STRDUP(locale_name);
		RETURN(LOC_NAME);
	}

	if (skip_to_EOL) {
		/*
		 * Ignore all text til we reach a new line.
		 * (For example, in char maps, after the <encoding>
		 */
		skip_to_EOL = 0;

		while ((c = input()) != EOF) {
			if (c == '\n')
				RETURN(c);
		}
		RETURN(EOF);
	}

	while ((c = input()) != EOF) {
		/*
		 * strings! replacement of escaped constants and <char>
		 * references are handled in copy_string() in sem_chr.c
		 *
		 * Replace all escape_char with \ so that strings will have a
		 * common escape_char for locale and processing in
		 * other locations.
		 *
		 * "[^\n"]*"
		 */
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

			if (c == '\n' || c == EOF)
				diag_error(gettext(ERR_MISSING_QUOTE), buf);

			if (copy_p) {
				if (c == '"') {
					c = input();
				}
				copy_p = 0;
				instring = FALSE;
				yylval.id = STRDUP(buf);
				RETURN(LOC_NAME);
			}

			instring = FALSE;
			yylval.id = STRDUP(buf);
			RETURN(STRING);
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
				diag_error(gettext(ERR_ILL_DEC_CONST),
				    sign == 1 ? "+" : "-");
			} else if (pos == 1) {
				yylval.ival = sign * geteval(T_DEC, buf);
				RETURN(NUM);
			} else if (pos > 1) {
				if ((hex_number && (pos == 2 || pos > 10)) ||
				    hex_number == 0) {
					yylval.id = STRDUP(buf);
					RETURN(STRING);
				}
				yylval.ival = geteval(T_HEX_CONST, &buf[2]);
				RETURN(HEX_CONST);
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
				case 'd':  /* decimal constant */
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
						diag_error(
						    gettext(ERR_ILL_DEC_CONST),
						    buf);
					}
					if (byte_cnt > MAX_BYTES) {
diag_error(gettext(ERR_TOO_LONG_BYTES));
					} else {
						byte_cnt++;
						value <<= 8;
						value += (uint64_t)
						    geteval(T_DEC, buf);
					}
					continue;

				case 'x':  /* hex constant */
				case 'X':
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
						diag_error(
						    gettext(ERR_ILL_HEX_CONST),
						    buf);
					}
					if (byte_cnt > MAX_BYTES) {
diag_error(gettext(ERR_TOO_LONG_BYTES));
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
							RETURN(CHAR_CONST);
						}
						uninput(c);
						uninput('\\');
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
						diag_error(
						    gettext(ERR_ILL_OCT_CONST),
						    buf);
						if ((c != EOF && !isspace(c)) ||
						    c == '\n') {
							uninput(c);
						}
						if (byte_cnt > MAX_BYTES) {
diag_error(gettext(ERR_TOO_LONG_BYTES));
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
						diag_error(
						    gettext(ERR_ILL_OCT_CONST),
						    buf);
					}
					if (byte_cnt > MAX_BYTES) {
diag_error(gettext(ERR_TOO_LONG_BYTES));
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
			RETURN(BYTES);
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
				diag_error(gettext(ERR_ILL_CHAR_SYM), buf);
				buf[pos-1] = '>';
			}

			/* look for one of the special 'meta-symbols' */
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
				RETURN(kw->token_val);
			}
			yylval.sym.id = STRDUP(buf);
			if (buf[1] == 'U' && (pos == 7 || pos == 11)) {
				/* <Uxxxx> or <Uxxxxxxxx> */
				uint64_t	u64;
				int	ii;
				char	tmpbuf[16];
				for (ii = 2; ii <= pos - 2; ii++) {
					if (!isxdigit((unsigned char)buf[ii])) {
						/*
						 * Not an ISO/IEC 10646-1:2000
						 * standard position constant
						 * value.
						 */
						RETURN(N_SYMBOL);
					}
				}
				(void) memcpy(tmpbuf, &buf[2], pos - 3);
				tmpbuf[pos - 3] = '\0';
				u64 = (uint64_t)geteval(T_HEX_CONST, tmpbuf);
				yylval.sym.ucs = u64;
				RETURN(U_SYMBOL);
			}
			RETURN(N_SYMBOL);
		}


		/*
		 * symbol for character class names - or keyword.
		 *
		 * [:alpha:_]+[:digit::alpha:_-]
		 */
		if (isalpha(c) || c == '_') {
			keyword_t	*kw;
			int	i;

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
				switch (kw->token_val) {
				case KW_LC_COLLATE:
				case KW_LC_CTYPE:
				case KW_LC_MSG:
				case KW_LC_MONETARY:
				case KW_LC_NUMERIC:
				case KW_LC_TIME:
					seenfirst++;
					RETURN(kw->token_val);

				case KW_COPY:
				{
					char	*ptr, *endp;
					copy_p = 1;
					while (isspace(c = input()) &&
					    c != '\n')
						;
					if (c == EOF) {
						/* indicate EOF for later */
						locale_name[0] = '\0';
						RETURN(KW_COPY);
					}
					if (c == '\n') {
						locale_name[0] = (char)c;
						uninput(c);
						RETURN(KW_COPY);
					}
					if (c == '"') {
						/* indicate quotes for later */
						locale_name[0] = (char)c;
						instring = TRUE;
						RETURN(KW_COPY);
					}
					/* save locale name for later */
					ptr = locale_name;
					endp = &locale_name[PATH_MAX];
					do {
						if (ptr == endp)
error(2, gettext(ERR_NAME_TOO_LONG), PATH_MAX);
						*ptr++ = (char)c;
					} while (((c = input()) != EOF) &&
					    !isspace(c));
					*ptr = '\0';
					uninput(c);
					RETURN(KW_COPY);
				}

				default:
					RETURN(kw->token_val);
				} /* switch */

			}
			if (!seenfirst &&
			    strcmp(buf, "comment_char") == 0 &&
			    yycharno == 14) {
				comment_char = (char)getspecialchar(
				    gettext(ERR_COM_CHAR_MISSING));
				continue;

			}
			if (!seenfirst &&
			    strcmp(buf, "escape_char") == 0 &&
			    yycharno == 13) {
				escape_char = (char)getspecialchar(
				    gettext(ERR_ESC_CHAR_MISSING));
				continue;
			}
			/*
			 * search for charclass or chartrans
			 */
			for (i = 0; i < ctype.nbinds; i++) {
				int	retval;
				if (strcmp(buf,
				    Lcbind_Table[i].lcbind.bindname) == 0) {
					/* found symbol */
					if (Lcbind_Table[i].lcbind.bindtag ==
					    _LC_TAG_CCLASS) {
						retval = CHAR_CLASS_SYMBOL;
					} else {
						retval = CHAR_TRANS_SYMBOL;
					}
					yylval.id = STRDUP(buf);
					RETURN(retval);
				}
			}
			yylval.sym.id = STRDUP(buf);
			RETURN(N_SYMBOL);
		}

		if (c == '.') {
			pos = 0;
			buf[pos++] = (char)c;
			c = input();
			buf[pos++] = (char)c;

			if (c != '.') {
				uninput(c);
				yylval.ival = (unsigned char)c;
				RETURN(CHAR_CONST);
			}

			c = input();
			buf[pos++] = (char)c;
			if (c == '.') {
				RETURN(KW_ELLIPSIS);
			} else {
				buf[pos] = '\0';
				diag_error(gettext(ERR_UNKNOWN_KWD), buf);
				continue;
			}
		}

		/*
		 * The newline is this grammer statement terminator - yuk!
		 */
		if (c == '\n') {
			RETURN(c);
		}

		if (isspace(c)) {
			continue;
		}

		if (c == ';' || c == '(' || c == ')' ||
		    c == ',' || c == ':' || c == '=' ||
		    c == '"') {
			RETURN(c);
		}

		if (isascii(c) && isprint(c)) {
			yylval.ival = (unsigned char)c;
			RETURN(CHAR_CONST);
		}

		diag_error(gettext(ERR_ILL_CHAR), c);

	} /* while c != EOF */

	RETURN(EOF);
}
