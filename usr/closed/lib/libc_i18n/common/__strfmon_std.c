/*
 * Copyright (c) 1996, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * COMPONENT_NAME: LIBCFMT
 *
 * FUNCTIONS:  __strfmon_std
 *
 * ORIGINS: 27
 *
 * IBM CONFIDENTIAL -- (IBM Confidential Restricted when
 * combined with the aggregated modules for this product)
 * OBJECT CODE ONLY SOURCE MATERIALS
 * (C) COPYRIGHT International Business Machines Corp. 1991, 1992
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 */
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
 * COMPONENT_NAME: LIBCFMT
 *
 * FUNCTIONS:  strfmon
 *
 *
 * (C) COPYRIGHT International Business Machines Corp. 1991
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 *
 * 1.3  com/lib/c/fmt/strfmon.c, libcfmt, 9130320 7/17/91 15:21:47
 */

#include "lint.h"
#include <sys/types.h>
#include <sys/localedef.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <values.h>
#include <nan.h>
#include <floatingpoint.h>
#include "libc.h"
#include "xpg6.h"

#define	max(a, b)	(a < b ? b : a)
#define	min(a, b)	(a > b ? b : a)
#define	PUT(c)	(strp < strend ? *strp++  = (c) : toolong++)
#define	MAXFSIG	17 	/* Maximum significant figures in floating-point num */

#ifdef	_ALLOC_DEBUG
#define	SIZE	2	/* Size of a working buffer */
#else
#define	SIZE	100	/* Size of a working buffer */
#endif

#define	_RTOL	0		/* copying direction from right to left */
#define	_LTOR	1		/* copying direction from left to right */

#define	_F_LEFT		0x0001	/* - : left-justification */
#define	_F_WIDTH	0x0002	/* w : field width */
#define	_F_LPREC	0x0004	/* #n: left precision */
#define	_F_NOGRP	0x0008	/* ^ : no grouping */
#define	_F_NOCUR	0x0040	/* ! : no currency symbol */

#define	_PAREN_BIT	0x80	/* ( is specified */
#define	_POSN_BITS(p)	(((unsigned char)(p)) & ~_PAREN_BIT)
#define	_ISPAREN(p)	(((unsigned char)(p)) & _PAREN_BIT)
#define	_PAREN_MSK(p)	(((unsigned char)(p)) | _PAREN_BIT)

struct global_data {
	_LC_monetary_t	*hdl;	/* pointer to the monetary object */
	ssize_t	w_width;	/* minimum width for the current format */
	ssize_t	n_width;	/* width for the #n specifier */
	ssize_t	prec;		/* precision of current format */
	ssize_t	byte_left;	/* number of byte left in the output buffer */
	struct buffer_data	*bufd; /* pointer to temp out buffer struct */
	struct mon_info		*mon; /* pointer to monetary info struct */
	int	flags;		/* flags */
	char	xpg6;		/* _C99SUSv3_strfmon is set */
	char	mb_len;		/* length of filling char of current locale */
	char	fchr[MB_LEN_MAX+1];	/* the fill character buffer */
};

struct buffer_data {
	char	*pstart;	/* start of the buffer */
	char	*pend;		/* end of the buffer [pstart, pend] */
	char	*outb;		/* beginning of the output */
	char	*oute;		/* end of the output (where null terminated) */
	char	*divide;	/* initial value of outb */
	size_t	bufsize;	/* buffer size */
};

struct mon_info {
	char	*curr_symbol;	/* currency_symbol or int_curr_symbol */
	char	space_sep[2];	/* space separator string */
	char	p_loc;		/* p_cs_precedes or int_p_cs_precedes */
	char	p_sep;		/* p_sep_by_space or int_p_sep_by_space */
	char	n_loc;		/* n_cs_precedes or int_n_cs_precedes */
	char	n_sep;		/* n_sep_by_space or int_n_sep_by_space */
	char	p_pos;		/* p_sign_posn or int_p_sign_posn */
	char	n_pos;		/* n_sign_posn or int_n_sign posn */
};

static ssize_t	do_format(struct global_data *, char *, double);
static int	bidi_output(struct buffer_data *, const char *, size_t, int);
static int	out_cur_sign(struct global_data *, int);
static int	do_out_cur_sign(struct global_data *,
    char, char, char, char *);
static ssize_t	digits_to_left(double);
static char	*fcvt_r(double, int, int *, int *, char *);
static int	rgrowbuf(struct buffer_data *);
static int	lgrowbuf(struct buffer_data *);

/*
 * FUNCTION: This is the standard method for function strfmon().
 *	     It formats a list of double values and output them to the
 *	     output buffer s. The values returned are affected by the format
 *	     string and the setting of the locale category LC_MONETARY.
 *
 * PARAMETERS:
 *           _LC_monetary_t hdl - the handle of the pointer to
 *			the LC_MONETARY catagory of the specific locale.
 *           char *s - location of returned string
 *           size_t maxsize - maximum length of output including the null
 *			      termination character.
 *           char *format - format that montary value is to be printed out
 *
 * RETURN VALUE DESCRIPTIONS:
 *           - returns the number of bytes that comprise the return string
 *             excluding the terminating null character.
 *           - returns 0 if s is longer than maxsize or error detected.
 */
ssize_t
__strfmon_std(_LC_monetary_t *hdl, char *s, size_t maxsize,
    const char *format, va_list ap)
{
	char	*strp;		/* pointer to output buffer s */
	const char	*fbad;	/* points to where format start to be invalid */
	char	*strend;	/* last availabel byte in output buffer s */
	char	ch;		/* the current character in the format string */
	int	toolong = 0;	/* logical flag for valid output length */
	int	i;		/* a working variable */
	int	pflag;		/* logical flag for precision */
	ssize_t	ret;
	struct global_data	gdata;
	struct mon_info	imon, nmon, *imonp = NULL, *nmonp = NULL;
	char	int_curr_str[4];
	int	f_paren, f_sign;
	char	p, n;

	if (!s || !hdl || !format)
		return (-1);
	gdata.byte_left = maxsize;
	gdata.hdl = hdl;

	if (__xpg6 & _C99SUSv3_strfmon) {
		gdata.xpg6 = 1;
	} else {
		gdata.xpg6 = 0;
	}
	strp = s;
	strend = s + maxsize - 1;
	while (((ch = *format++) != '\0') && !toolong) {
		if (ch != '%') {
			PUT(ch);
			gdata.byte_left--;
		} else {
			fbad = format;
			pflag = 0;
			f_paren = 0;
			f_sign = 0;
			gdata.w_width = 0;
			gdata.n_width = 0;
			gdata.prec = 0;
			gdata.flags = 0;
			gdata.fchr[0] = ' ';
			gdata.fchr[1] = '\0';
			gdata.mb_len = 1;
			/* ------- scan for flags ------- */
			i = 0;
			for (; ; ) {
				switch (*format) {
				case '=': case '^': case '~': case '!':
				case '+': case '(': case '-':
					break;
				default:
					i = 1;	/* there are no more */
						/* optional flags    */
				}
				if (i)
					break;	/* exit loop */
				if (*format == '=') {	/* =f fill character */
					int	mlen;
					format++;
					if ((mlen = mblen(format, MB_LEN_MAX))
					    != -1) {
						(void) strncpy(gdata.fchr,
						    format, mlen);
						gdata.fchr[mlen] = '\0';
						format += mlen;
						gdata.mb_len = (char)mlen;
					} else {
						return (-1); /* invalid char */
					}
				}
				if (*format == '^') {
					/* no grouping for thousands */
					format++;
					gdata.flags |= _F_NOGRP;
				}
				if (*format == '+') {
					/* locale's +/- sign used */
					format++;
					if (f_paren != 0) {
						/*
						 * '+' and '(' are mutually
						 * exclusive.
						 */
						f_paren = 0;
					}
					f_sign = 1;
				} else if (*format == '(') {
					/* locale's paren. for neg. */
					format++;
					if (f_sign != 0) {
						/*
						 * '+' and '(' are mutually
						 * exclusive.
						 */
						f_sign = 0;
					}
					f_paren = 1;
				}
				if (*format == '!') {
					/* suppress currency symbol */
					format++;
					gdata.flags |= _F_NOCUR;
				}
				if (*format == '-') {
					/* - left justify */
					format++;
					gdata.flags |= _F_LEFT;
				}
			} /* end of while(1) loop */
			/* -------- end scan flags -------- */
			while (isdigit((unsigned char)*format)) {
				/* w width field */
				gdata.w_width *= 10;
				gdata.w_width += *format++ - '0';
				gdata.flags |= _F_WIDTH;
			}
			if (*format == '#') {
				/* #n digits precedes decimal(left precision) */
				gdata.flags |= _F_LPREC;
				format++;
				while (isdigit((unsigned char)*format)) {
					gdata.n_width *= 10;
					gdata.n_width += *format++ - '0';
				}
			}
			if (*format == '.') {
				/* .p precision (right precision) */
				pflag++;
				format++;
				while (isdigit((unsigned char)*format)) {
					gdata.prec *= 10;
					gdata.prec += *format++ - '0';
				}
			}
			switch (*format++) {
			case '%':
				PUT('%');
				gdata.byte_left--;
				break;

			case 'i':	/* international currency format */
				if (imonp == NULL) {
					imonp = &imon;
					if (gdata.xpg6) {
						/* UNIX03 mode */
						imonp->p_loc =
						    hdl->int_p_cs_precedes;
						imonp->p_sep =
						    hdl->int_p_sep_by_space;
						imonp->n_loc =
						    hdl->int_n_cs_precedes;
						imonp->n_sep =
						    hdl->int_n_sep_by_space;
						int_curr_str[0] =
						    hdl->int_curr_symbol[0];
						int_curr_str[1] =
						    hdl->int_curr_symbol[1];
						int_curr_str[2] =
						    hdl->int_curr_symbol[2];
						int_curr_str[3] = '\0';
						imonp->curr_symbol =
						    int_curr_str;
						imonp->space_sep[0] =
						    hdl->int_curr_symbol[3];
						imonp->space_sep[1] = '\0';
					} else {
						imonp->p_loc =
						    hdl->p_cs_precedes;
						imonp->p_sep =
						    hdl->p_sep_by_space;
						imonp->n_loc =
						    hdl->n_cs_precedes;
						imonp->n_sep =
						    hdl->n_sep_by_space;
						imonp->curr_symbol = (char *)
						    hdl->int_curr_symbol;
						imonp->space_sep[0] = ' ';
						imonp->space_sep[1] = '\0';
					}
				}

				if (gdata.xpg6) {
					/* UNIX03 mode */
					p = hdl->int_p_sign_posn;
					n = hdl->int_n_sign_posn;
					if (p > 4 || p == CHAR_MAX) {
						p = 1;
					}
					if (n > 4 || n == CHAR_MAX) {
						n = 1;
					}
					if (f_paren) {
						imonp->p_pos = _PAREN_MSK(p);
						imonp->n_pos = _PAREN_MSK(n);
					} else {
						imonp->p_pos = p;
						imonp->n_pos = n;
					}
				} else {
					p = hdl->p_sign_posn;
					n = hdl->n_sign_posn;
					if (p > 4 || p == CHAR_MAX) {
						p = 1;
					}
					if (n > 4 || n == CHAR_MAX) {
						n = 1;
					}
					if (f_paren) {
						imonp->p_pos = _PAREN_MSK(p);
						imonp->n_pos = _PAREN_MSK(n);
					} else {
						imonp->p_pos = p;
						imonp->n_pos = n;
					}
				}
				gdata.mon = imonp;

				if (!pflag &&
				    (gdata.prec = hdl->int_frac_digits) < 0)
					gdata.prec = 2;
				if ((ret = do_format(&gdata, strp,
				    va_arg(ap, double))) == -1) {
					return (-1);
				} else {
					strp += ret;
					gdata.byte_left -= ret;
				}
				break;

			case 'n':	/* local currency format */
				if (nmonp == NULL) {
					nmonp = &nmon;
					nmonp->p_loc = hdl->p_cs_precedes;
					nmonp->p_sep = hdl->p_sep_by_space;
					nmonp->n_loc = hdl->n_cs_precedes;
					nmonp->n_sep = hdl->n_sep_by_space;
					nmonp->curr_symbol = (char *)
					    hdl->currency_symbol;
					nmonp->space_sep[0] = ' ';
					nmonp->space_sep[1] = '\0';
				}
				p = hdl->p_sign_posn;
				n = hdl->n_sign_posn;
				if (p > 4 || p == CHAR_MAX) {
					p = 1;
				}
				if (n > 4 || n == CHAR_MAX) {
					n = 1;
				}
				if (f_paren) {
					nmonp->p_pos = _PAREN_MSK(p);
					nmonp->n_pos = _PAREN_MSK(n);
				} else {
					nmonp->p_pos = p;
					nmonp->n_pos = n;
				}
				gdata.mon = nmonp;

				if (!pflag &&
				    (gdata.prec = hdl->frac_digits) < 0)
					gdata.prec = 2;
				if ((ret = do_format(&gdata, strp,
				    va_arg(ap, double))) == -1) {
					return (-1);
				} else {
					strp += ret;
					gdata.byte_left -= ret;
				}
				break;
			default:
				format = fbad;
				PUT('%');
				gdata.byte_left--;
				break;
			}
		} /* else */
	} /* while */
	if (toolong) {
		errno = E2BIG;
		return (-1);
	} else {
		*strp = '\0';
		return ((ssize_t)(strp - s));
	}
}

/*
 * FUNCTION: do_format()
 *	     This function performs all the necessary formating for directives
 *	     %i and %n. The output will be written to the output
 *	     buffer str and the number of formatted bytes are returned.
 *
 * PARAMETERS:
 *           struct global_data *gp - global data
 *           char *str - location of returned string
 *           double dval - the double value to be formatted.
 *
 * RETURN VALUE DESCRIPTIONS:
 *           - returns the number of bytes that comprise the return string
 *             excluding the terminating null character.
 *           - returns -1 if s is longer than maxsize or any error.
 */
static ssize_t
do_format(struct global_data *gp, char *str, double dval)
{
	char	*s;		/* work buffer for string handling */
	char	*number;	/* work buffer for converted string */
	char	*buf;		/* for fcvt_r() */
	ssize_t dig_to_left;	/* number of digits to the left of decimal in */
				/* the actual value of dval */
	int	fcvt_prec;	/* number of digits to the right of decimal */
				/* for conversion of fcvt */
	int	decpt;		/* a decimal number to show where the radix */
				/* character is when counting from beginning */
	int	sign;		/* 1 for negative, 0 for positive */
	char	*separator;	/* thousand separator string of the locale */
	char	*radix;		/* locale's radix character */
	ssize_t gap;		/* number of filling character needed */
	ssize_t sep_width;	/* the width of the thousand separator */
	char	*mon_grp;
	size_t	buflen;
	struct buffer_data	bufdata, *bp;
	static const char	*left_parenthesis = "(";
	static const char	*right_parenthesis = ")";
	static const char	*decimal_point = ".";
	/*
	 * First, convert the double value into a character string by
	 * using fcvt_r(). To determine the precision used by fcvt_r().
	 *
	 * if no digits to left of decimal,
	 * 	then all requested digits will be emitted to right of decimal.
	 *	hence, use max of(max-sig-digits, user's-requested-precision).
	 *
	 * else if max-sig-digits <= digits-to-left
	 * 	then all digits will be emitted to left of decimal point.
	 *  	Want to use zero or negative prec value to insure that rounding
	 * 	will occur rather than truncation.
	 *
	 * else
	 *	digits can be emitted both to left and right of decimal, but
	 *	only potential for rounding/truncation is to right of decimal.
	 *	Hence, want to use user's request precision if it will not
	 *	cause truncation, else use largest prec that will round
	 *	correctly when max. number of digits is emitted.
	 */
	if (gp->prec > 20)
		gp->prec = 2; /* Compatible to Solaris. */

	bp = &bufdata;
	gp->bufd = bp;
	bp->pstart = malloc(sizeof (char) * SIZE);
	if (!bp->pstart)
		return (-1);
	bp->bufsize = SIZE;
	bp->pend = bp->pstart + bp->bufsize - 1;

	/*
	 * digits_to_left() returns the number of digits to the
	 * left of the decimal place.  This value can
	 * be 0 or negative for numbers less than 1.0.
	 * i.e.:
	 * 10.00 = > 2
	 *  1.00 = > 1
	 *  0.10 = > 0
	 *  0.01 = > -1
	 */
	/* get the num of digit to the left */
	dig_to_left = digits_to_left(dval);

	/* determine the precision to be used */
	if (dig_to_left <= 0) {
		fcvt_prec = min(gp->prec, MAXFSIG);
	} else if (dig_to_left >= MAXFSIG) {
		fcvt_prec = MAXFSIG - dig_to_left;
	} else {
		fcvt_prec = min(gp->prec, MAXFSIG - dig_to_left);
	}

	/* allocate the buffer for fcvt_r to store the converted string */
	if ((buf = malloc(DECIMAL_STRING_LENGTH)) == NULL) {
		free(bp->pstart);
		return (-1);
	}
	number = fcvt_r(dval, fcvt_prec, &decpt, &sign, buf);

	/*
	 * fcvt_r() may have rounded our number up and in so doing the
	 * number of digits to the left may have increased by one.
	 * Use 'decpt' as it has the correct number of decimal digits now.
	 */
	dig_to_left = (decpt <= 0)? 0 : decpt;

	/*
	 * Fixing the position of the radix character(if any). Output the
	 * number using the radix as the reference point. When ouptut grows to
	 * the right, decimal digits are processed and appropriate precision(if
	 * any) is used. When output grows to the left, grouping of digits
	 * (if needed), thousands separator (if any), and filling character
	 * (if any) will be used.
	 *
	 * Begin by processing the decimal digits.
	 */

	bp->divide = bp->pstart + (bp->bufsize / 2);
	/*
	 * Output string will be located at [bp->outb, bp->oute]
	 * including the null termination.
	 */
	bp->outb = bp->divide;
	bp->oute = bp->outb;
	if (gp->prec) {
		size_t	tlen;
		if (*(radix = (char *)gp->hdl->mon_decimal_point)) {
			/* set radix character position */
			tlen = strlen(radix);
			if (bidi_output(bp, radix, tlen, _LTOR) == -1) {
				free(buf);
				free(bp->pstart);
				return (-1);
			}
		} else {
			/*
			 * radix character not defined
			 * using the default decimal char
			 */
			if (bidi_output(bp, decimal_point, 1, _LTOR) == -1) {
				free(buf);
				free(bp->pstart);
				return (-1);
			}
		}
		s = number + decpt; 	/* copy the digits after the decimal */
		tlen = strlen(s);
		if (bidi_output(bp, s, tlen, _LTOR) == -1) {
			free(buf);
			free(bp->pstart);
			return (-1);
		}
	}

	/* points to the digit that precedes the radix */
	s = number + decpt;

	if (gp->flags & _F_NOGRP) {	/* no grouping is needed */
		if (bidi_output(bp, s - dig_to_left, dig_to_left,
		    _RTOL) == -1) {
			free(buf);
			free(bp->pstart);
			return (-1);
		}
	} else {
		if (*(mon_grp = (char *)gp->hdl->mon_grouping)) {
			/* get grouping format,eg: "^C^B\0" == "3;2" */
			ssize_t	n_width, num_dec, gsz, sv_gsz;

			num_dec = dig_to_left;
			separator = (char *)gp->hdl->mon_thousands_sep;
			sep_width = strlen(separator);
			while (num_dec > 0) {
				if (*mon_grp) {
					/* get group size */
					gsz = *mon_grp++;
					/* save this */
					sv_gsz = gsz;
				} else {
					/*
					 * no more group size defined.
					 * use the saved one
					 */
					gsz = sv_gsz;
				}
				if (gsz > num_dec || gsz == -1) {
					/*
					 * if the grouping size is larger
					 * than the num of digits, or
					 * if the grouping size is -1,
					 * then no more grouping
					 * performed.
					 */
					gsz = num_dec;
				}
				s -= gsz;
				if (bidi_output(bp, s, gsz, _RTOL) == -1) {
					free(buf);
					free(bp->pstart);
					return (-1);
				}
				num_dec -= gsz;

				if (num_dec) {
					if (bidi_output(bp, separator,
					    sep_width, _RTOL) == -1) {
						free(buf);
						free(bp->pstart);
						return (-1);
					}
				}
			} /* while */
			/*
			 * Note that in "%#<x>n", <x> is the number of digits,
			 * not spaces, so after the digits are in place, the
			 * rest of the field must be the same size as if the
			 * number took up the whole field.  This means
			 * adding fill chars where there would have been a
			 * separator.  e.g.: "%#10n"
			 *	$9,987,654,321.00
			 *	$@@@@@@@54,321.00	correct
			 *	$@@@@@54,321.00		incorrect
			 * So, n_width should be n_width (i.e. digits) +
			 * number of separators to be inserted.
			 * Solution:  just increment n_width for every
			 * separator that would have been inserted.  In this
			 * case, 3.  Also, follow mon_grouping rules about
			 * repeating and the -1.
			 */
			mon_grp = (char *)gp->hdl->mon_grouping;
			n_width = gp->n_width;
			if (*mon_grp) {
				if ((gsz = *mon_grp++) == -1) {
					gsz = n_width;
				}
				while (n_width - gsz > 0) {
					n_width -= gsz;
					gp->n_width++;
					if (*mon_grp &&
					    (gsz = *mon_grp++) == -1) {
						break;
					}
				}
			}
		} else {
			/* the grouping format is not defined in this locale */
			if (bidi_output(bp, s - dig_to_left, dig_to_left,
			    _RTOL) == -1) {
				free(buf);
				free(bp->pstart);
				return (-1);
			}
		}
	}

	/*
	 * Determine if padding is needed.
	 * If the number of digit prceeding the radix character is greater
	 * than #n(if any), #n is ignored. Otherwise, the output is padded
	 * with filling character("=f", if any) or blank is used by default.
	 */

	if ((gp->flags & _F_LPREC) &&
	    (gap = gp->n_width - (bp->divide - bp->outb)) > 0) {
		/* padding required */
		while (gap-- > 0) {
			if (bidi_output(bp, gp->fchr,
			    (size_t)gp->mb_len, _RTOL) == -1) {
				free(buf);
				free(bp->pstart);
				return (-1);
			}
		}
	}

	/*
	 * At here, the quantity value has already been decided. What comes
	 * next are the positive/negative sign, monetary symbol, parenthesis.
	 * The no_curflag will be considered first to determine the sign and
	 * currency format.
	 * If none of them are defined, the locale's defaults are used.
	 */

	if (out_cur_sign(gp, sign) == -1) {
		free(buf);
		free(bp->pstart);
		return (-1);
	}
	if ((sign && (gp->mon->n_pos == 0 || _ISPAREN(gp->mon->n_pos))) ||
	    (!sign && gp->mon->p_pos == 0)) {
		/*
		 * if a negative value is specified and
		 * [int_]n_sign_posn is set to 0 or '(' flag is specified,
		 * or if a non-negative value is specified and
		 * [int_]p_sign_posn is set to 0, parentheses enclose
		 * the quantity and the currency symbol.
		 */
		if (bidi_output(bp, left_parenthesis, 1, _RTOL) == -1 ||
		    bidi_output(bp, right_parenthesis, 1, _LTOR) == -1) {
			free(buf);
			free(bp->pstart);
			return (-1);
		}
	} else if (gp->flags & _F_LPREC &&
	    ((!sign && (gp->mon->n_pos == 0 || _ISPAREN(gp->mon->n_pos))) ||
	    (sign && gp->mon->p_pos == 0))) {
		/*
		 * In the case left precision "#n" is specified:
		 *  - if a non-negative value is passed, and if the locale's
		 *    n_sign_posn definition is 0 or '(' flag is specified.
		 *  - if a negative value is passed, and if the locale's
		 *    p_sign_posn definition is 0.
		 * then spaces are padded to make the format an equal length
		 * with the one when a value having an opposite sign were
		 * passed.
		 */
		if (bidi_output(bp, (const char *)" ", 1, _RTOL) == -1 ||
		    bidi_output(bp, (const char *)" ", 1, _LTOR) == -1) {
			free(buf);
			free(bp->pstart);
			return (-1);
		}
	}

	/*
	 * By setting e(the last byte of the buffer) to \0 and increment
	 * b(the first byte of the buffer), now the temp buffer should
	 * have a completely formatted null terminated string starting and
	 * ending at b and e. Before the result is copied into the s buffer,
	 * check if the formatted string is less than the w-field width and
	 * determine its left or right justification.
	 */

	*bp->oute = '\0';
	buflen = strlen(bp->outb);
	if (max(buflen, gp->w_width) >= gp->byte_left) {
		free(buf);
		free(bp->pstart);
		errno = E2BIG;
		return (-1);
	}
	if ((gp->flags & _F_WIDTH) && buflen < gp->w_width) {
		/* justification is needed */
		if (gp->flags & _F_LEFT) {
			(void) strcpy(str, bp->outb);
			str += buflen;
			(void) memset(str, ' ', gp->w_width - buflen);
			*(str + gp->w_width - buflen) = '\0';
		} else {
			(void) memset(str, ' ', gp->w_width - buflen);
			str += gp->w_width - buflen;
			(void) strcpy(str, bp->outb);
		}
		free(buf);
		free(bp->pstart);
		return ((ssize_t)gp->w_width);
	} else {
		(void) strcpy(str, bp->outb);
		free(buf);
		free(bp->pstart);
		return ((ssize_t)buflen);
	}
}

/*
 * FUNCTION: out_cur_sign()
 *	     This function ouputs the sign related symbol (if needed) and
 *	     the currency symbol (if needed) to the ouput buffer. It also
 *	     updates the beginning and ending pointers of the formatted
 *	     string. This function indeed extract the sign related information
 *	     of the current formatting value and pass them to the sign
 *	     independent formatting function do_out_cur_sign().
 *
 * PARAMETERS:
 *	     struct global_data *gp - global data
 *	     int sign - The sign of the current formatting monetary value.
 *
 * RETURN VALUE DESCRIPTIONS:
 *	     - returns -1 if an error happened
 *	     - otherwise, returns 0
 */

static int
out_cur_sign(struct global_data *gp, int sign)
{
	char	pn_cs_precedes;
	char	pn_sign_posn;
	char	pn_sep_by_space;
	char	*pn_sign;
	char	*padded, *p;
	int	ret;
	static const char	*minus_sign = "-";

	if (sign) {	/* negative number with sign and currency symbol */
		pn_cs_precedes = gp->mon->n_loc;
		pn_sign_posn = gp->mon->n_pos;
		pn_sep_by_space = gp->mon->n_sep;
		pn_sign = (char *)gp->hdl->negative_sign;
		if (strlen(pn_sign) == 0)
			pn_sign = (char *)minus_sign;
	} else {	/* positive number with sign and currency symbol */
		pn_cs_precedes = gp->mon->p_loc;
		pn_sign_posn = gp->mon->p_pos;
		pn_sep_by_space = gp->mon->p_sep;
		pn_sign = (char *)gp->hdl->positive_sign;
	}

	if (pn_cs_precedes != 1 && pn_cs_precedes != 0)
		pn_cs_precedes = 1;
	if (pn_sep_by_space > 3 || pn_sep_by_space == CHAR_MAX)
		pn_sep_by_space = 0;

	if (gp->flags & _F_LPREC) {
		/* align if left precision is used (i.e. "%#10n") */
		size_t	maxlen, tmplen;
		int	which_sign = 0;
		maxlen = strlen(gp->hdl->negative_sign);
		if (maxlen < (tmplen = strlen(gp->hdl->positive_sign))) {
			maxlen = tmplen;
			which_sign = 1;	/* positive sign string is longer */
		}
		p = padded = malloc(sizeof (char) * (maxlen + 1));
		if (!p) {
			return (-1);
		}

		if ((sign && which_sign == 1) ||
		    (!sign && which_sign == 0)) {
			tmplen = maxlen - strlen(pn_sign);
			(void) memset(p, ' ', tmplen);
			p += tmplen;
			(void) strcpy(p, pn_sign);
		} else {
			(void) strcpy(p, pn_sign);
		}
	} else {
		padded = pn_sign;
	}

	if (do_out_cur_sign(gp, pn_cs_precedes, pn_sign_posn,
	    pn_sep_by_space, padded) == -1) {
		ret = -1;
	} else {
		ret = 0;
	}

	if (gp->flags & _F_LPREC) {
		free(padded);
	}

	return (ret);
}


/*
 * FUNCTION: do_out_cur_sign()
 *	This is a common function to process positive and negative
 *	monetary values. It outputs the sign related symbol (if needed)
 *	and the currency symbol (if needed) to the output buffer.
 *
 * PARAMETERS:
 *	     struct global_data *gp - global data
 *	     char pn_cs_precedes - The p_cs_precedes or n_cs_precedes value.
 *	     char pn_sign_posn - The p_sign_posn or n_sign_posn value.
 *	     char pn_sep_by_space - The p_sep_by_space or n_sep_by_space value.
 *	     char *pn_sign - The positive_sign or negative_sign value.
 *
 * RETURN VALUE DESCRIPTIONS:
 *	     - returns -1 if an error happened
 *           - otherwise, returns 0
 */
static int
do_out_cur_sign(struct global_data *gp, char pn_cs_precedes,
    char pn_sign_posn, char pn_sep_by_space, char *pn_sign)
{
	size_t	cur_len, sign_len;
	struct buffer_data	*bp = gp->bufd;
	static const char	*spc;
	spc = gp->mon->space_sep;

	cur_len = strlen(gp->mon->curr_symbol);
	sign_len = strlen(pn_sign);

	if (pn_cs_precedes == 1) {	/* cur_sym preceds quantity */
		switch (_POSN_BITS(pn_sign_posn)) {
		case 0:
			/*
			 * parentheses surround quantity and currency symbol
			 *
			 * in XPG6
			 * sep_by_space	no _F_NOCUR	_F_NOCUR
			 *	0:	(JPY100)	(100)
			 *	1:	(JPY 100)	(100)
			 *	2:	(JPY100)	(100)
			 *
			 * in non-XPG6
			 * sep_by_space	no _F_NOCUR	_F_NOCUR
			 *	0:	(JPY100)	(100)
			 *	1:	(JPY100)	(100)
			 *	2:	(JPY100)	(100)
			 */
			if (!(gp->flags & _F_NOCUR)) {
				if (gp->xpg6 && pn_sep_by_space == 1) {
					/*
					 * space separates currency symbol from
					 * quantity
					 */
					if (bidi_output(bp, spc, 1, _RTOL) ==
					    -1) {
						return (-1);
					}
				}
				/*
				 * print the currency symbol
				 */
				if (bidi_output(bp, gp->mon->curr_symbol,
				    cur_len, _RTOL) == -1) {
					return (-1);
				}
			}
			break;
		case 1:
		case 3:
			/*
			 * sign string precedes the currency symbol
			 *
			 * sep_by_space	no _F_NOCUR	_F_NOCUR
			 *	0:	-JPY100		-100
			 *	1:	-JPY 100	- 100
			 *	2:	- JPY100	- 100
			 */
			if (pn_sep_by_space == 1) {
				/*
				 * space separates currency symbol from
				 * quantity
				 */
				if (bidi_output(bp, spc, 1, _RTOL) == -1) {
					return (-1);
				}
			}
			if (!(gp->flags & _F_NOCUR)) {
				/*
				 * print the currency symbol
				 */
				if (bidi_output(bp, gp->mon->curr_symbol,
				    cur_len, _RTOL) == -1) {
					return (-1);
				}
			}
			if (pn_sep_by_space == 2) {
				/*
				 * space separates sign string from
				 * currency symbol
				 */
				if (bidi_output(bp, spc, 1, _RTOL) == -1) {
					return (-1);
				}
			}
			/*
			 * sign string
			 */
			if (!_ISPAREN(pn_sign_posn)) {
				if (bidi_output(bp, pn_sign, sign_len, _RTOL) ==
				    -1) {
					return (-1);
				}
			}
			break;

		case 2:
			/*
			 * sign string succeeds quantity
			 *
			 * in XPG6
			 * sep_by_space	no _F_NOCUR	_F_NOCUR
			 *	0:	JPY100-		100-
			 *	1:	JPY 100-	100-
			 *	2:	JPY100 -	100 -
			 *
			 * in non-XPG6
			 * sep_by_space	no _F_NOCUR	_F_NOCUR
			 *	0:	JPY100-		100-
			 *	1:	JPY100-		100-
			 *	2:	JPY100-		100-
			 */
			if (!(gp->flags & _F_NOCUR)) {
				if (gp->xpg6 && pn_sep_by_space == 1) {
					/*
					 * space separates currency symbol from
					 * quantity
					 */
					if (bidi_output(bp, spc, 1, _RTOL) ==
					    -1) {
						return (-1);
					}
				}
				/*
				 * print the currency symbol
				 */
				if (bidi_output(bp, gp->mon->curr_symbol,
				    cur_len, _RTOL) == -1) {
					return (-1);
				}
			}
			if (gp->xpg6 && pn_sep_by_space == 2) {
				/*
				 * space separates quantity from sign string
				 */
				if (bidi_output(bp, spc, 1, _LTOR) == -1) {
					return (-1);
				}
			}
			/*
			 * sign string succeeds quantity
			 */
			if (!_ISPAREN(pn_sign_posn)) {
				if (bidi_output(bp, pn_sign, sign_len, _LTOR) ==
				    -1) {
					return (-1);
				}
			}
			break;
		case 4:
			/*
			 * sign string immediately succeeds the currency symbol
			 *
			 * sep_by_space	no _F_NOCUR	_F_NOCUR
			 *	0:	JPY-100		-100
			 *	1:	JPY- 100	- 100
			 *	2:	JPY -100	-100
			 */
			switch (pn_sep_by_space) {
			case 0:
			case 2:
				/*
				 * sign string
				 */
				if (!_ISPAREN(pn_sign_posn)) {
					if (bidi_output(bp, pn_sign,
					    sign_len, _RTOL) == -1) {
						return (-1);
					}
				}
				break;
			case 1:
				/*
				 * space separates quantity and sign string
				 */
				if (bidi_output(bp, spc, 1, _RTOL) == -1) {
					return (-1);
				}
				/*
				 * sign string
				 */
				if (!_ISPAREN(pn_sign_posn)) {
					if (bidi_output(bp, pn_sign, sign_len,
					    _RTOL) == -1) {
						return (-1);
					}
				}
				break;
			}
			if (!(gp->flags & _F_NOCUR)) {
				if (pn_sep_by_space == 2) {
					/*
					 * space separates currency symbol and
					 * sign string
					 */
					if (bidi_output(bp, spc, 1, _RTOL) ==
					    -1) {
						return (-1);
					}
				}
				/*
				 * print the currency symbol
				 */
				if (bidi_output(bp, gp->mon->curr_symbol,
				    cur_len, _RTOL) == -1) {
					return (-1);
				}
			}
			break;
		}
	} else if (pn_cs_precedes == 0) { /* cur_sym after quantity */
		switch (_POSN_BITS(pn_sign_posn)) {
		case 0:
			/*
			 * parentheses surround quantity and currency symbol
			 *
			 * in XPG6
			 * sep_by_space	no _F_NOCUR	_F_NOCUR
			 *	0:	(100JPY)	(100)
			 *	1:	(100 JPY)	(100)
			 *	2:	(100JPY)	(100)
			 *
			 * in non-XPG6
			 * sep_by_space	no _F_NOCUR	_F_NOCUR
			 *	0:	(100JPY)	(100)
			 *	1:	(100JPY)	(100)
			 *	2:	(100JPY)	(100)
			 */
			if (!(gp->flags & _F_NOCUR)) {
				if (gp->xpg6 && pn_sep_by_space == 1) {
					/*
					 * space separates quantity from
					 * currency symbol
					 */
					if (bidi_output(bp, spc, 1, _LTOR) ==
					    -1) {
						return (-1);
					}
				}
				/*
				 * print the currency symbol
				 */
				if (bidi_output(bp, gp->mon->curr_symbol,
				    cur_len, _LTOR) == -1) {
					return (-1);
				}
			}
			break;
		case 1:
			/*
			 * sign string precedes the quantity
			 *
			 * in XPG6
			 * sep_by_space	no _F_NOCUR	_F_NOCUR
			 *	0:	-100JPY		-100
			 *	1:	-100 JPY	-100
			 *	2:	- 100JPY	-100
			 *
			 * in non-XPG6
			 * sep_by_space	no _F_NOCUR	_F_NOCUR
			 *	0:	-100JPY		-100
			 *	1:	-100JPY		-100
			 *	2:	-100JPY		-100
			 */
			if (gp->xpg6 && pn_sep_by_space == 2) {
				/*
				 * space separates sign string from quantity
				 */
				if (bidi_output(bp, spc, 1, _RTOL) == -1) {
					return (-1);
				}
			}
			/*
			 * sign string precedes the quantity
			 */
			if (!_ISPAREN(pn_sign_posn)) {
				if (bidi_output(bp, pn_sign, sign_len, _RTOL) ==
				    -1) {
					return (-1);
				}
			}
			if (!(gp->flags & _F_NOCUR)) {
				if (gp->xpg6 && pn_sep_by_space == 1) {
					/*
					 * space separates quantity from
					 * currency symbol
					 */
					if (bidi_output(bp, spc, 1, _LTOR) ==
					    -1) {
						return (-1);
					}
				}
				/*
				 * print the currency symbol
				 */
				if (bidi_output(bp, gp->mon->curr_symbol,
				    cur_len, _LTOR) == -1) {
					return (-1);
				}
			}
			break;
		case 2:
		case 4:
			/*
			 * sign string succeeds the currency symbol
			 *
			 * sep_by_space	no _F_NOCUR	_F_NOCUR
			 *	0:	100JPY-		100-
			 *	1:	100 JPY-	100 -
			 *	2:	100JPY -	100 -
			 */
			if (pn_sep_by_space == 1) {
				/*
				 * space separates quantity from
				 * currency symbol
				 */
				if (bidi_output(bp, spc, 1, _LTOR) == -1) {
					return (-1);
				}
			}
			if (!(gp->flags & _F_NOCUR)) {
				/*
				 * print the currency symbol
				 */
				if (bidi_output(bp, gp->mon->curr_symbol,
				    cur_len, _LTOR) == -1) {
					return (-1);
				}
			}
			if (pn_sep_by_space == 2) {
				/*
				 * space separates currency symbol from
				 * sign string
				 */
				if (bidi_output(bp, spc, 1, _LTOR) == -1) {
					return (-1);
				}
			}
			/*
			 * sign string
			 */
			if (!_ISPAREN(pn_sign_posn)) {
				if (bidi_output(bp, pn_sign, sign_len, _LTOR) ==
				    -1) {
					return (-1);
				}
			}
			break;
		case 3:
			/*
			 * sign string immediately precedes the currency symbol
			 *
			 * sep_by_space	no _F_NOCUR	_F_NOCUR
			 *	0:	100-JPY		100-
			 *	1:	100 -JPY	100 -
			 *	2:	100- JPY	100-
			 */
			if (pn_sep_by_space == 1) {
				/*
				 * space separates quantity from
				 * sign string
				 */
				if (bidi_output(bp, spc, 1, _LTOR) == -1) {
					return (-1);
				}
			}
			/*
			 * sign string
			 */
			if (!_ISPAREN(pn_sign_posn)) {
				if (bidi_output(bp, pn_sign, sign_len, _LTOR) ==
				    -1) {
					return (-1);
				}
			}
			if (!(gp->flags & _F_NOCUR)) {
				if (pn_sep_by_space == 2) {
					/*
					 * space separates sign string from
					 * currency symbol
					 */
					if (bidi_output(bp, spc, 1, _LTOR) ==
					    -1) {
						return (-1);
					}
				}
				/*
				 * print the currency symbol
				 */
				if (bidi_output(bp, gp->mon->curr_symbol,
				    cur_len, _LTOR) == -1) {
					return (-1);
				}
			}
			break;
		}
	} else {		/* currency position not defined */
		switch (pn_sign_posn) {
		case 1:
			/*
			 * sign string precedes the quantity
			 */
			if (bidi_output(bp, pn_sign, sign_len, _RTOL) == -1) {
				return (-1);
			}
			break;
		case 2:
			/*
			 * sign string succeeds the quantity
			 */
			if (bidi_output(bp, pn_sign, sign_len, _LTOR) == -1) {
				return (-1);
			}
			break;
		}
	}
	return (0);
}

/*
 * FUNCTION: bidi_output()
 *	     This function copies the infield to output buffer, outfield,
 *	     either by appending data to the end of the buffer when the
 *	     direction (dir) is 1 or by inserting data to the beginning
 *	     of the buffer when the direction (dir) is 0.
 *
 * PARAMETERS:
 *	    struct buffer_data *bp - buffer data
 *	    char *infield - The character string to be copied into the
 *			    output buffer outfield.
 *	    size_t len - the length of the infield string
 *	    int dir - When dir is _LTOR, infield is appended to the end of
 *			the output buffer.
 *			When dir is _RTOL, infield is inserted in front of the
 *			output buffer.
 *
 * RETURN VALUE DESCRIPTIONS:
 *	    - returns -1 if an error happened
 *	    - otherwise, returns 0
 */
static int
bidi_output(struct buffer_data *bp, const char *infield, size_t len, int dir)
{
	if (!(*infield))
		return (0);
	if (dir == _LTOR) {		/* output from left to right */
		while ((bp->pend - bp->oute) < len) {
			if (rgrowbuf(bp) == -1) {
				return (-1);
			}
		}
		(void) strncpy(bp->oute, infield, len);
		bp->oute += len;
	} else {
		while ((bp->outb - bp->pstart) < len) {
			if (lgrowbuf(bp) == -1) {
				return (-1);
			}
		}
		bp->outb -= len;
		(void) strncpy(bp->outb, infield, len);

	}
	return (0);
}

/*
 * FUNCTION: digits_to_left()
 *      Compute number of digits to left of decimal point.
 *      Scale number to range 10.0..1.0 counting divides or
 *      deducting multiplies.
 *      Positive value mean digits to left, negative is digits to right.
 *      If the number is very large scale using large divisors.
 *      If its intermediate do it the slow way.
 *      If its very small scale using large multipliers
 *      This replaces the totally IEEE dependent __ld() with a
 *      (mostly) independent version stolen from ecvt.c.
 *      Slight speed mods since ecvt does more work than we need.
 *
 * PARAMETERS:
 *      double dvalue   - value to work with
 *
 * RETURN VALUE DESCRIPTIONS:
 *      int             - number of digits to left of decimal point
 */
static ssize_t
digits_to_left(double dvalue)
{
	static const struct log {	/* scale factors */
		double  pow10;
		int	pow;
	} log[] = {	1e32,   32,
			1e16,   16,
			1e8,    8,
			1e4,    4,
			1e2,    2,
			1e1,    1 };
	const struct log	*scale = log;
	ssize_t		digits = 1;	/* default (no scale) */

	/*
	 * check for fluff.
	 * Original expression
	 * if (IS_NAN(dvalue) || IS_INF(dvalue) || IS_ZERO(dvalue))
	 */
	if (IsNANorINF(dvalue) || dvalue == 0)
		return (0);

	/*
	 * make it positive
	 * Original expression
	 * dvalue = fabs(dvalue);
	 */
	if (dvalue < 0.0)
		dvalue *= -1;

	/* now scale it into 10.0..1.0 range */
	if (dvalue >= 2.0 * DMAXPOWTWO) {	/* big */
		do {    /* divide down */
			for (; dvalue >= scale->pow10; digits += scale->pow)
				dvalue /= scale->pow10;
		} while ((scale++)->pow > 1);

	} else if (dvalue >= 10.0) {		/* medium */
		do {    /* divide down */
			digits++;
		} while ((dvalue /= 10.0) >= 10.0);
	} else if (dvalue < 1.0) {		/* small */
			do {    /* multiply up */
				for (; dvalue * scale->pow10 < 10.0;
				    digits -= scale->pow)
					dvalue *= scale->pow10;
			} while ((scale++)->pow > 1);
	}
	return (digits);
}

/*
 * FUNCTION: fcvt_r()
 *	Convert the specified double value to a NULL-terminated
 *	string of ndigits in buf.  This function is an reentrant
 *	version of the libc function fcvt().
 */
static char *
fcvt_r(double number, int ndigits, int *decpt, int *sign, char *buf)
{
	char	*ptr, *val;
	char	ch;
	int	deci_val;

	ptr = fconvert(number, ndigits, decpt, sign, buf);

	val = ptr;
	deci_val = *decpt;

	while ((ch = *ptr) != '\0') {
		if (ch != '0') {
			/* If there are leading zeros, gets updated. */
			*decpt = deci_val;
			return (ptr);
		}
		ptr++;
		deci_val--;
	}
	return (val);
}

static int
rgrowbuf(struct buffer_data *bp)
{
	ptrdiff_t	diff_b, diff_e, diff_d;
	char	*new;

	diff_b = bp->outb - bp->pstart;
	diff_e = bp->oute - bp->pstart;
	diff_d = bp->divide - bp->pstart;

	new = realloc(bp->pstart, bp->bufsize * 2);
	if (!new) {
		return (-1);
	}
	bp->bufsize *= 2;
	bp->pstart = new;
	bp->pend = new + bp->bufsize - 1;
	bp->outb = new + diff_b;
	bp->oute = new + diff_e;
	bp->divide = new + diff_d;
	return (0);
}

static int
lgrowbuf(struct buffer_data *bp)
{
	ptrdiff_t	diff_b, diff_e, diff_d;
	char	*new, *new_b, *new_e;

	diff_b = bp->outb - bp->pstart;
	diff_e = bp->oute - bp->pstart;
	diff_d = bp->divide - bp->pstart;

	new = malloc(bp->bufsize * 2);
	if (!new) {
		return (-1);
	}
	new_b = new + diff_b + bp->bufsize;
	new_e = new + diff_e + bp->bufsize;
	(void) memcpy(new_b, bp->outb, bp->oute - bp->outb);
	free(bp->pstart);
	bp->divide = new + diff_d + bp->bufsize;
	bp->bufsize *= 2;
	bp->pstart = new;
	bp->pend = new + bp->bufsize - 1;
	bp->outb = new_b;
	bp->oute = new_e;
	return (0);
}
