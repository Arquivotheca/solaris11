/*
 * (C) COPYRIGHT International Business Machines Corp. 1985, 1993
 * All Rights Reserved
 *
 * (c) Copyright 1990, 1991, 1992 OPEN SOFTWARE FOUNDATION, INC.
 * ALL RIGHTS RESERVED
 *
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 *
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <errno.h>
#include <stdio.h>
#include <locale.h>
#include <nl_types.h>
#include <stdlib.h>
#include <limits.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <malloc.h>

#define	FPLUS	2
#define	FMINUS	4
#define	FBLANK	8
#define	FSHARP	16
#define	DOTSEEN	64
#define	DIGITSEEN 128

#define	MINSTR _POSIX2_LINE_MAX
/*
 * This typedef is used as our internal representation of strings.
 * We need this to avoid problems with real \0 characters being
 * treated as string terminators. Yuck.
 */
typedef struct {
	char	*begin;
	char	*end;
	int	bail;
} String;

/*
 * This structure will save the position of the string pointer
 * in terms of byte position from the beginning of the string
 * prior to realloc of the structure
 */
typedef struct {
	int	begin_diff;
	int	end_diff;
} String_diff;

static void escwork(char *, String *);
static int doargs(String *, String *, char *, char *[]);
static void finishline(String *, String *);
static void p_out(String *, size_t, const char *, ...);
static char *find_percent(String *);
static void old_printf(char *[]);

static int error = 0;
static 	char	*outstr = NULL;
static 	char	*tmpptr = NULL;
static  int		bflag = 0;	/* flag %b processing in progress */

static	size_t 	outstr_size = MINSTR +1;    /* inital output  buffer size */
static	size_t 	tmpptr_size = MINSTR+1;    /* inital temp buffer size */
static String Outstr = { NULL, NULL, 0 };
static String_diff Outstr_diff = { 0, 0 };

int
main(int argc, char **argv)
{
	String Fmt;
	char *start;
	int argn;

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	if (argc > 1 &&
	    strcmp(argv[1], "--") == 0) {	/* XCU4 "--" handling, sigh */
		argv++;
		argc--;
	}

	if (argv[1]) {
		Fmt.begin = argv[1];
		Fmt.end = argv[1] + strlen(argv[1]);
	} else {
		(void) fprintf(stderr,
		    gettext("Usage: printf format [argument...]\n"));
		exit(1);
	}

	/*
	 * Malloc inital buffer size of _POSIX_LINE_MAX
	 */

	outstr = malloc(outstr_size);
	if (outstr == NULL) {
		(void) fprintf(stderr, gettext("cannot allocate memory"));
			exit(1);
	}

	Outstr.begin = outstr;
	Outstr.end = outstr;
	tmpptr = malloc(tmpptr_size);
	if (tmpptr == NULL) {
		(void) fprintf(stderr, gettext("cannot allocate memory"));
			exit(1);
	}
	/*
	 * Transform octal numbers and backslash sequences to the correct
	 * character representations and stores the resulting string back
	 * into Fmt.begin.
	 */
	escwork(argv[1], &Fmt);

	/*
	 * If no format specification, simply output the format string
	 */
	if (find_percent(&Fmt) == NULL) {
		(void) fwrite(Fmt.begin, Fmt.end - Fmt.begin, 1, stdout);
		exit(0);
	}

	/*
	 * Escape sequences have been translated.  Now process
	 * format arguments.
	 */

	start = Fmt.begin;
	argn = 2;

	while (argn < argc) {
		int rc;

		errno = 0;

		if ((rc = doargs(&Outstr, &Fmt, argv[argn], argv)) == 1) {
			/* ending format string is a string */
			Fmt.begin = start;
		} else if (rc == 2) {
			/* invalid conversion or containing % char(s) */
			break;
		} else if (rc == 3) {
			/* found a \c, suppress all further */
			break;
		} else
			argn++;
	}

	/*
	 * Check to see if 'format' is done. if not transfer the
	 * rest of the 'format' to output string.
	 */

	if (Fmt.begin != Fmt.end)
		finishline(&Outstr, &Fmt);

	(void) fwrite(Outstr.begin, Outstr.end - Outstr.begin, 1, stdout);
	return (error);
}



/*
 * 	escwork
 *
 * This routine transforms octal numbers and backslash sequences to the
 * correct character representations and stores the resulting string
 * in the String 'Dest'.
 *
 * The returned value is a character pointer to the last available position in
 * destination string, the function itself returns an indication of whether
 * or not it detected the 'bailout' character \c while parsing the string.
 */

static void
escwork(char *source,			/* pointer to source */
	String *Dest)			/* pointer to destination */
{
	char *destin;
	int j;
	int mbcnt = 0;
	wchar_t wd;

	/*
	 * Preserve the underlying string for the sake of '$' arguments.
	 */
	Dest->begin = strdup(source);
	Dest->bail = 0;		/* set to 1 when we hit the \c character */

	for (destin = Dest->begin; *source; source += (mbcnt > 0) ? mbcnt : 1) {
		mbcnt = mbtowc(&wd, source, MB_CUR_MAX);
		if (mbcnt == 1 && wd == '\\') {
			/*
			 * process escape sequences
			 */
			switch (*++source) {
			case 'a':	/* bell/alert */
				*destin++ = '\a';
				continue;
			case 'b':
				*destin++ = '\b';
				continue;
			case 'c':	/* no newline, nothing */
				Dest->end = destin;
				Dest->bail = 1;
				*destin = 0;
				return;
			case 'f':
				*destin++ = '\f';
				continue;
			case 'n':
				*destin++ = '\n';
				continue;
			case 'r':
				*destin++ = '\r';
				continue;
			case 't':
				*destin++ = '\t';
				continue;
			case 'v':
				*destin++ = '\v';
				continue;
			case '\\':
				*destin++ = '\\';
				continue;
			case '0': /* 0-prefixed octal chars */
			case '1': /* non-0-prefixed octal chars */
			case '2': case '3': case '4': case '5':
			case '6': case '7':
				/*
				 * the following 2 lines should not be
				 * necessary, but VSC allows for \0ddd.
				 * If processing %b and there's a leading
				 * zero to flag octal, increment source
				 * past the zero to the first digit of
				 * the octal number.
				 */
				if (bflag == 1 && *source == '0')
					source++;
				j = wd = 0;
				while ((*source >= '0' &&
				    *source <= '7') && j++ < 3) {
					wd <<= 3;
					wd |= (*source++ - '0');
				}
				*destin++ = (char)wd;
				/* Change % to %% */
				if (wd == '%')
					*destin++ = wd;
				--source;
				continue;
			default:
				--source;
			}	/* end of switch */
		}
		mbcnt = wctomb(destin, wd);		/* normal character */
		destin += (mbcnt > 0) ? mbcnt : 1;
	}	/* end of for */

	Dest->end = destin;
	*destin = '\0';
}

static void
do_realloc(String *Dest, size_t len)
{
	outstr_size = len;
	Outstr_diff.begin_diff = Dest->begin - outstr;
	Outstr_diff.end_diff = Dest->end - outstr;
	outstr = realloc(outstr, outstr_size);
	if (outstr == NULL) {
		(void) fprintf(stderr, gettext("cannot allocate memory"));
		exit(++error);
	}
	Dest->begin = outstr + Outstr_diff.begin_diff;
	Dest->end = outstr + Outstr_diff.end_diff;
}

static size_t
max_check(char spec, int width, int prec)
{
	size_t	maxlen;
	int	pp;
/*
 * 'e' or 'E'
 *
 * [-|+| ]d.ddd[e|E]+dd
 * The number of digits after the radix is 'prec' (or 6)
 *
 * 1 (sign) + 1 (digit) + 1 (radix) + 'prec' +
 * 1 (e or E) + 1 (+ or -) + 3 (max exponent is 308)
 * = 'prec' + 8
 *
 * if 'width' > 'prec' + 8, 'width' is the max length.
 * otherwise, 'prec' + 8 is the max length.
 *
 *
 * 'f'
 *
 * [-|+| ]ddddd.ddddd
 * The number of digits after the radix is 'prec' (or 6)
 *
 * 1 (sign) + M (digits) + 1 (radix) + 'prec'
 * = 'prec' + M + 2
 *
 * DBL_MAX = 1.7976931348623157E+308 (<float.h>)
 * therefore, M = 309
 *
 * if 'width' > 'prec' + 311, 'width' is the max length.
 * otherwise, 'prec' + 311 is the max length.
 *
 *
 * 'g' or 'G'
 *
 * [-|+| ]d.ddddd[e|E]+ddd
 *
 * 1 (sign) + 'prec' (significant) + 1 (radix) +
 * 1 (e or E) + 1 (+ or -) + 3 (max component is 308)
 * = 'prec' + 7
 *
 * [-|+| ]ddddd.ddddd
 *
 * 1 (sign) + 'prec' (significant) + 1 (radix)
 * = 'prec' + 2
 *
 * if 'width' > 'prec' + 7, 'width' is the max length.
 * otherwise, 'prec' + 7 is the max length.
 *
 *
 * 'x' or 'X'
 *
 * [0x|0X]hhhhhhhhh
 *
 * 2 (0x or 0X) + max('prec', M)
 * = max('prec', M) + 2
 *
 * ULONG_MAX = 4294967295UL = 0xFFFFFFFF (<limits.h>)
 * therefore, M = 8
 *
 * if 'width' > max('prec', 8) + 2, 'width' is the max length.
 * otherwise, max('prec', 8) + 2 is the max length.
 *
 *
 * 'd' or 'i'
 *
 * [-|+| ]ddddddddd
 *
 * 1 (sign) + max('prec', M)
 * = max('prec', M) + 1
 *
 * LONG_MAX =  2147483647 (<limits.h>)
 * therefore, M = 10
 *
 * if 'width' > max('prec', 10) + 1, 'width' is the max length.
 * otherwise, max('prec', 10) + 1 is the max length.
 *
 *
 * 'o'
 *
 * [0]oooooooo
 *
 * 1 ('0') + max('prec', M)
 * = max('prec', M) + 1
 *
 * ULONG_MAX = 4294967295 = 37777777777 (<limits.h>)
 * therefore, M = 11
 *
 * if 'width' > max('prec', 11) + 1, 'width' is the max length.
 * otherwise, max('prec', 11) + 1 is the max length.
 *
 *
 * 'u'
 *
 * ddddddddd
 *
 * max('prec', M)
 *
 * ULONG_MAX = 4294967295 (<limits.h>)
 * therefore, M = 10
 *
 * if 'width' > max('prec', 10), 'width' is the max length.
 * otherwise, max('prec', 10) is the max length.
 *
 */
	switch (spec) {
	case 'e':
	case 'E':
		pp = prec ? (prec + 8) : (6 + 8);
		break;
	case 'f':
		pp = prec ? (prec + 311) : (6 + 311);
		break;
	case 'g':
	case 'G':
		pp = prec ? (prec + 7) : (6 + 7);
		break;
	case 'x':
	case 'X':
		pp = (prec > 8) ? (prec + 2) : (8 + 2);
		break;
	case 'd':
	case 'i':
		pp = (prec > 10) ? (prec + 1) : (10 + 1);
		break;
	case 'o':
		pp = (prec > 11) ? (prec + 1) : (11 + 1);
		break;
	case 'u':
		pp = (prec > 10) ? prec : 10;
		break;
	}

	if (width) {
		if (width > pp)
			maxlen = width;
		else
			maxlen = pp;
	} else
		maxlen = pp;

	return (maxlen + 1);
}

/*
 *    doargs
 *
 * This routine does the actual formatting of the input arguments.
 *
 * This routine handles the format of the following form:
 *	%n$pw.df
 *		n:	argument number followed by $
 *		p:	prefix, zero or more of {- + # or blank}.
 *		w:	width specifier. It is optional.
 *		.:	decimal.
 *		d:	precision specifier.
 *                      A null digit string is treated as zero.
 *		f:	format xXioudfeEgGcbs.
 *
 * The minimum set required is "%f".  Note that "%%" prints one "%" in output.
 *
 * RETURN VALUE DESCRIPTION:
 *	0 	forms a valid conversion specification.
 *	1	the ending format string is a string.
 *	2	cannot form a valid conversion; or the string contains
 *		literal % char(s).
 *
 * NOTE: If a character sequence in the format begins with a % character,
 *	 but does not form a valid conversion specification, the doargs()
 *	 will pass the invalid format to the sprintf() and let it handle
 *	 the situation.
 */

static int
doargs(
	String *Dest,			/* destination string */
	String *Fmtptr,			/* format string */
	char *args,			/* argument to process */
	char *argv[])			/* full argument list */
{
	char tmpchar, *last;
	char *ptr;
	long lnum;
	double fnum;
	int percent;			/* flag for "%" */
	int width, prec, flag;
	char *fmt;
	size_t	max_len;

	percent = 0;

	/*
	 *   "%" indicates a conversion is about to happen.  This section
	 *   parses for a "%"
	 */
	for (fmt = last = Fmtptr->begin; last < Fmtptr->end; last++) {
		if (!percent) {
			if (*last == '%') {
				percent++;
				fmt = last;
				flag = width = prec = 0;
			} else
				p_out(Dest, 2, "%c", *last);
			continue;
		}

		/*
		 * '%' has been found check the next character for conversion.
		 */

		switch (*last) {
		case '%':
			p_out(Dest, 2, "%c", *last);
			percent = 0;
			continue;
		case 'x':
		case 'X':
		case 'd':
		case 'o':
		case 'i':
		case 'u':
			if (*args == '\'' || *args == '"') {
				if (args[1] == '\0')
					ptr = args;
				else
					ptr = args+2;
				lnum = (int)(unsigned char)args[1];
			} else {
				if (*last != 'd' && *last != 'i')
					lnum = strtoul(args, &ptr, 0);
				else
					lnum = strtol(args, &ptr, 0);
			}
			if (errno) {  /* overflow, underflow or invalid base */
				(void) fprintf(stderr, "printf: %s: %s\n",
				    args, strerror(errno));
				error++;
			} else if (ptr == args) {
				(void) fprintf(stderr, gettext(
				    "printf: %s expected numeric value\n"),
				    args);
				error++;
			} else if (*ptr != NULL) {
				(void) fprintf(stderr, gettext(
				    "printf: %s not completely converted\n"),
				    args);
				error++;
			}
			max_len = max_check(*last, width, prec);
			tmpchar = *(++last);
			*last = '\0';
			p_out(Dest, max_len, fmt, lnum);
			*last = tmpchar;
			break;
		case 'f':
		case 'e':
		case 'E':
		case 'g':
		case 'G':
			if (*args == '\'' || *args == '"') {
				if (args[1] == '\0')
					ptr = args;
				else
					ptr = args+2;
				fnum = (int)(unsigned char)args[1];
			} else
				fnum = strtod(args, &ptr);
			/*
			 * strtod() handles error situations somewhat different
			 * from strtoul() and strtol(), e.g., strtod() will set
			 * errno for incomplete conversion, but strtoul() and
			 * strtol() will not. The following error test order
			 * is used in order to have the same behaviour as for
			 * u, d, etc. conversions
			 */
			if (ptr == args) {
				(void) fprintf(stderr, gettext(
				    "printf: %s expected numeric value\n"),
				    args);
				error++;
			} else if (*ptr != NULL) {
				(void) fprintf(stderr, gettext(
				    "printf: %s not completely converted\n"),
				    args);
				error++;
			} else if (errno) { /* overflow, underflow or EDOM */
				(void) fprintf(stderr, "printf: %s: %s\n",
				    args, strerror(errno));
				error++;
			}
			max_len = max_check(*last, width, prec);
			tmpchar = *(++last);
			*last = '\0';
			p_out(Dest, max_len, fmt, fnum);
			*last = tmpchar;
			break;
		case 'c':
			if (*args == '\0') {
				last++;		/* printf %c "" => no output */
				break;
			}
			max_len = width ? width + 1 : 2;
			tmpchar = *(++last);
			*last = '\0';
			p_out(Dest, max_len, fmt, *args);
			*last = tmpchar;
			break;
		case 's':
			max_len = strlen(args);
			if (width > max_len) {
				max_len = width;
			}
			tmpchar = *(++last);
			*last = '\0';
			p_out(Dest, max_len + 1, fmt, args);
			*last = tmpchar;
			break;
		case 'b': {
			int pre = 0, post = 0;
			String Arg, *a = &Arg;
			char *begin, *end;

			/*
			 * XXX	Sigh - %b is -far- too complex.
			 * Set bflag=1 to tell escwork %b is being proccessed.
			 * Set bflag back to zero upon return from escwork.
			 */
			bflag = 1;
			escwork(args, a);
			bflag = 0;
			begin = a->begin;
			end = a->end;

			/*
			 * The 'precision' specifies the minimum number of
			 * chars to be eaten from the string.  We need to
			 * check for multibyte characters in case we truncate
			 * one in the middle.  Oops.
			 */
			if (flag & DOTSEEN) {
				char *p;
				int mbcnt, count;
				wchar_t wd;

				count = 0;
				for (p = begin; p < end; p += mbcnt) {
					mbcnt = mbtowc(&wd, p, MB_CUR_MAX);
					if (mbcnt <= 0)
						mbcnt = 1;
					if (count + mbcnt > prec)
						end = p;
					count += mbcnt;
				}
			}

			/*
			 * The 'width' specifies the minimum width, padded
			 * with spaces
			 */
			if ((end - begin) < width) {
				if (flag & FMINUS) {
					/* left-justified */
					post = width - (end - begin);
				} else {
					/* right justified */
					pre = width - (end - begin);
				}
			}

			/*
			 * write it all out
			 */
			if (pre)
				p_out(Dest, pre + 1, "%*s", pre, "");
			while (begin < end)
				p_out(Dest, 2, "%c", *begin++);
			if (post)
				p_out(Dest, post + 1, "%*s", post, "");
			free(a->begin);

			if (a->bail) {
				/*
				 * escwork detected the "give up now"
				 * character, bailing at that point in
				 * the string.
				 */
				Fmtptr->begin = Fmtptr->end;
				return (3);
				/*NOTREACHED*/
			}
			last++;
		}
			break;

		default:	/* 0 flag, width or precision */
			if (isdigit(*last)) {
				int value = strtol(last, &ptr, 10);
				if (errno) {
					(void) fprintf(stderr,
					    "printf: %s: %s\n",
					    last, strerror(errno));
					error++;
				}
				flag |= DIGITSEEN;
				if (flag & DOTSEEN)
					prec = value;
				else
					width = value;

				last += ptr - last - 1;
				continue;
			}
			switch (*last) {
			case '-':
				flag |= FMINUS;
				continue;
			case '+':
				flag |= FPLUS;
				continue;
			case ' ':
				flag |= FBLANK;
				continue;
			case '#':
				flag |= FSHARP;
				continue;
			case '.':
				flag |= DOTSEEN;
				continue;
			case '$':
				/*
				 * This is only allowed for compatibility
				 * with the SVR4 base version of printf.
				 *
				 * Once we see that the format specification
				 * contains a '$', we know that it must be
				 * an 'old' usage of printf, so we simply
				 * discard all the work we've done so far,
				 * and behave exactly the same way the old
				 * printf used to do.
				 */
				if (flag == DIGITSEEN) {
					old_printf(argv);
					/*NOTREACHED*/
				}
				(void) fprintf(stderr,
				    gettext("printf: bad '$' argument\n"));
				error++;
				/*FALLTHROUGH*/
			default:
				p_out(Dest, 2, "%c", *last++);
				break;
			}
		}

		Fmtptr->begin = last;

		return (0);
	} 	/* end of for */

	if (find_percent(Fmtptr) == NULL) {
		/*
		 * Check for the 'bailout' character ..
		 */
		if (Fmtptr->bail)
			return (3);
		/*
		 * the ending format string is a string
		 * fmtptr points to the end of format string
		 */
		Fmtptr->begin = last;
		return (1);
	} else {
		/*
		 * cannot form a valid conversion; or
		 * a string containing literal % char(s)
		 * fmtptr points to the end of format string
		 */
		Fmtptr->begin = last;
		return (2);
	}
}


/*
 *   finishline
 *
 *	This routine finishes processing the extra format specifications
 *
 *      If a character sequence in the format begins with a % character,
 *      but does not form a valid conversion specification, nothing will
 *      be written to output string.
 */

static void
finishline(
	String *Dest,	/* destination string */
	String *Fmtptr)	/* format string */
{
	char tmpchar, *last;
	int percent;				/* flag for "%" */
	int width, prec, flag;
	char *ptr;
	char *fmtptr;
	size_t	max_len;

	/*
	 * Check remaining format for "%".  If no "%", transfer
	 * line to output.  If found "%" replace with null for %s or
	 * %c, replace with 0 for all others.
	 */

	percent = 0;

	for (last = fmtptr = Fmtptr->begin; last != Fmtptr->end; last++) {
		if (!percent) {
			if (*last == '%') {
				percent++;
				fmtptr = last;
				flag = width = prec = 0;
			} else
				p_out(Dest, 2, "%c", *last);
			continue;
		}

		/*
		 * OK. '%' has been found check the next character
		 * for conversion.
		 */
		switch (*last) {
		case '%':
			p_out(Dest, 2, "%%");
			percent = 0;
			continue;
		case 'x':
		case 'X':
		case 'd':
		case 'o':
		case 'i':
		case 'u':
			max_len = max_check(*last, width, prec);
			tmpchar = *(++last);
			*last = '\0';
			p_out(Dest, max_len, fmtptr, 0);
			*last = tmpchar;
			fmtptr = last;
			percent = 0;
			last--;
			break;
		case 'f':
		case 'e':
		case 'E':
		case 'g':
		case 'G':
			max_len = max_check(*last, width, prec);
			tmpchar = *(++last);
			*last = '\0';
			p_out(Dest, max_len, fmtptr, 0.0);
			*last = tmpchar;
			fmtptr = last;
			percent = 0;
			last--;
			break;
		case 'b':
		case 'c':
		case 's':
			if (!width) {
				max_len = 1;
			} else {
				max_len = width + 1;
			}
			*last = 's';
			tmpchar = *(++last);
			*last = '\0';
			p_out(Dest, max_len, fmtptr, "");
			*last = tmpchar;
			fmtptr = last;
			percent = 0;
			last--;
			break;
		default:	/* 0 flag, width or precision */
			if (isdigit(*last)) {
				int	value = strtol(last, &ptr, 10);
				if (errno) {
					(void) fprintf(stderr,
					    "printf: %s: %s\n",
					    last, strerror(errno));
					error++;
				}
				flag |= DIGITSEEN;
				if (flag & DOTSEEN)
					prec = value;
				else
					width = value;

				last += ptr - last - 1;
				continue;
			}
			switch (*last) {
			case '.':
				flag |= DOTSEEN;
				continue;
			case '-':
			case '+':
			case ' ':
			case '#':
				continue;
			default:
				break;
			}
		}
	}
}

/*
 *   p_out
 *
 *   This routine preforms a conversion of the current format
 *   being processed into a temp buffer then moves it to the
 *   output buffer. The buffer size is limited only by memory.
 */
static void
p_out(String *s, size_t len, const char *format, ...)
{
	int rc = 0;
	va_list ap;

	if (tmpptr_size < len) {
		tmpptr_size = len;
		tmpptr = realloc(tmpptr, tmpptr_size);
		if (tmpptr == NULL) {
			(void) fprintf(stderr,
			    gettext("cannot allocate memory"));
			exit(1);
		}
	}
	va_start(ap, format);
	rc = vsprintf(tmpptr, format, ap);
	if (rc < 0) {
		(void) fprintf(stderr, gettext("printf: bad conversion\n"));
		rc = 0;
		error++;
	} else if (errno != 0 &&
	    errno != ERANGE && errno != EINVAL && errno != EDOM) {
		/*
		 * strtol(), strtoul() or strtod() should've reported
		 * the error if errno is ERANGE, EINVAL or EDOM
		 */
		perror("printf");
		error++;
	} else if ((rc + (s->end - s->begin)) > outstr_size) {
		do_realloc(s, rc + (s->end - s->begin));
	}
	va_end(ap);
	s->end = (char *)memcpy(s->end, tmpptr, rc) + rc;
}

static char *
find_percent(String *s)
{
	int mbcnt;
	wchar_t wd;
	char *p;

	for (p = s->begin; p != s->end; p += (mbcnt > 0) ? mbcnt : 1) {
		mbcnt = mbtowc(&wd, p, MB_CUR_MAX);
		if (mbcnt == 1 && wd == '%')
			return (p);
	}
	return (NULL);
}

#include <libgen.h>

/*
 * This is the printf from 5.0 -> 5.4
 * This version is only used when '$' format specifiers are detected
 * (in which case all bets are off w.r.t. the rest of format functionality)
 */
static void
old_printf(char *argv[])
{
	char *fmt;

	if ((fmt = strdup(argv[1])) == (char *)0) {
		(void) fprintf(stderr, gettext("malloc failed\n"));
		exit(1);
	}
	(void) strccpy(fmt, argv[1]);
	(void) printf(fmt, argv[2], argv[3], argv[4], argv[5],
	    argv[6], argv[7], argv[8], argv[9],
	    argv[10], argv[11], argv[12], argv[13],
	    argv[14], argv[15], argv[16], argv[17],
	    argv[18], argv[19], argv[20]);
	exit(0);
}
