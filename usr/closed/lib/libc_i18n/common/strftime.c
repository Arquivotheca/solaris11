/*
 * Copyright (c) 1996, 2011, Oracle and/or its affiliates. All rights reserved.
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
 * #if !defined(lint) && !defined(_NOIDENT)
 * static char rcsid[] = "@(#)$RCSfile: strftime.c,v $ $Revision: 1.13.6.6"
 *	" $ (OSF) $Date: 1992/11/13 22:27:45 $";
 * #endif
 */
/*
 * COMPONENT_NAME: (LIBCGEN) Standard C Library General Functions
 *
 * FUNCTIONS:  strftime
 *
 *
 * (C) COPYRIGHT International Business Machines Corp. 1991
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 *
 * 1.3  com/lib/c/fmt/strftime.c, libcfmt, 9130320 7/17/91 15:22:50
 * 1.7  com/lib/c/fmt/__strftime_std.c, libcfmt, 9140320 9/26/91 13:59:49
 */

#include "lint.h"
#include "mtlib.h"
#include <sys/localedef.h>
#include <time.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <tzfile.h>
#include <limits.h>
#include "libc.h"
#include "libc_i18n.h"


/*
 * The buffer size for local temporary buffers we keep the formatted result
 * from doformat() and elsewhere.
 */
#define	BUFSIZE			(1024)

/*
 * The string buffer length size for the number of seconds from
 * the Epoch (%s). (For this buffer, we re-use a local buffer which is
 * allocated with the BUFSIZE as the amount of space).
 *
 * The possible values for the seconds from mktime() would be between
 * LONG_MIN and LONG_MAX. And thus, any value bigger than 20 should suffice.
 *
 * We use 128 to be plenty (not including a string terminating null byte).
 */
#define	EPOCH_SECS_OFFSET	(128)

/*
 * The following macro converts a positive number in string into
 * a corresponding integral number. This appears mainly for convenience
 * and better performance. This has a side effect of moving forward
 * the address pointed to by the argument 's' which is intended.
 */
#define	STRTONUM(s, n)	(n) = 0; \
			while (isdigit((unsigned char)*(s))) \
				(n) = (n) * 10 + *(s)++ - '0';

/*
 * The following macro is used to check if the element (i.e., data field)
 * pointed to by the argument 'f' of the current locale does contain real
 * value or not. If not, supply the fallback date/time element pointed to
 * by the argument 's'.
 */
#define	CHECKFMT(f, s) (((f) && *(f)) ? (f) : (s))

/*
 * The following macros are defined for code simplification and also for
 * possible performance enhancement by avoiding function calls and reducing
 * the number of instructions.
 *
 * The GETNUM_*() macros convert integral numbers into corresponding
 * character strings in decimal digit characters or locale's alternate
 * numeric symbols if exist. If the conversion will use locale's alternate
 * numeric symbols, then, the padding and case mapping type is not going to
 * be explicitly set but stay in the initial default value so that the type
 * will be decided at the last moment.
 *
 * The GET_DEC_NUM_*() macros convert integral numbers into corresponding
 * character strings in decimal digit characters.
 *
 * The *_SP() macros additionally replace leading '0' with a space character
 * during conversion. The padding type will be PCMTYPE_NUMBER_SPACE regardless
 * of whether the replacement of the leading '0' took place or not if this
 * isn't going to be converted using locale's alternate number symbols.
 */
#define	GETNUM_1(l, i, s, altnum, p) \
			if ((altnum) == NULL) { \
				(s)[0] = (i) % 10 + '0'; \
				(s)[1] = '\0'; \
				(l) = 1; \
				(p) = PCMTYPE_NUMBER; \
			} else { \
				(l) = get_alt_num((i), (s), (altnum)); \
			}

#define	GET_DEC_NUM_2(l, i, s, p) \
			(s)[0] = (i) / 10 % 10 + '0'; \
			(s)[1] = (i) % 10 + '0'; \
			(s)[2] = '\0'; \
			(l) = 2; \
			(p) = PCMTYPE_NUMBER;

#define	GET_DEC_NUM_2_SP(l, i, s, p) \
			(l) = (i) / 10 % 10; \
			(s)[0] = ((l) == 0) ? ' ' : (l) + '0'; \
			(s)[1] = (i) % 10 + '0'; \
			(s)[2] = '\0'; \
			(l) = 2; \
			(p) = PCMTYPE_NUMBER_SPACE;

#define	GETNUM_2(l, i, s, altnum, p) \
			if ((altnum) == NULL) { \
				GET_DEC_NUM_2((l), (i), (s), (p)); \
			} else { \
				(l) = get_alt_num((i), (s), (altnum)); \
			}

#define	GETNUM_2_SP(l, i, s, altnum, p) \
			if ((altnum) == NULL) { \
				GET_DEC_NUM_2_SP((l), (i), (s), (p)); \
			} else { \
				(l) = get_alt_num((i), (s), (altnum)); \
			}

#define	GET_DEC_NUM_3(l, i, s, p) \
			(s)[0] = (i) / 100 % 10 + '0'; \
			(s)[1] = (i) / 10 % 10 + '0'; \
			(s)[2] = (i) % 10 + '0'; \
			(s)[3] = '\0'; \
			(l) = 3; \
			(p) = PCMTYPE_NUMBER;

#define	GET_DEC_NUM_4(l, i, s, p) \
			(s)[0] = (i) / 1000 % 10 + '0'; \
			(s)[1] = (i) / 100 % 10 + '0'; \
			(s)[2] = (i) / 10 % 10 + '0'; \
			(s)[3] = (i) % 10 + '0'; \
			(s)[4] = '\0'; \
			(l) = 4; \
			(p) = PCMTYPE_NUMBER;

struct era_struct {
	char	dir;		/* direction of the current era */
	int	offset;		/* offset of the current era */
	char	st_date[100];   /* start date of the current era */
	char	end_date[100];  /* end date of the current era */
	char	name[100];	/* name of the current era */
	char	form[100];	/* format string of the current era */
};
typedef struct era_struct *era_ptr;

/*
 * Possible pad flag characters.
 *
 * Non-zero value means there is a pad character defined.
 *
 * The PAD_UNDEFINED means there is no padding behavior explicitly defined.
 * The PAD_REMOVE means there is no padding and should also remove any
 * preceding zero or space for numeric values if any, e.g., if %m == "07",
 * then, %-m would be "7". See strftime(3C) for PAD_ZERO, PAD_SPACE, and
 * any other details .
 */
#define	PAD_UNDEFINED		('\0')
#define	PAD_REMOVE		('-')
#define	PAD_ZERO		('0')
#define	PAD_SPACE		(' ')

/*
 * Possible types of date/time elements for padding and case mapping.
 *
 * PCMTYPE_UNKNOWN means there is no known pad type yet.
 * PCMTYPE_NUMBER expects zero ('0', 0x30) as pad character by default.
 * PCMTYPE_NUMBER_SPACE and PCMTYPE_SPECIAL expect space (' ', 0x20) as
 * pad character by default.
 *
 * PCMTYPE_UNKNOWN may require for case mapping checking since
 * the corresponding date/time elements may contain characters that
 * can be case mapped into one way or the other.
 */
#define	PCMTYPE_UNKNOWN		(0)
#define	PCMTYPE_NUMBER		(1)
#define	PCMTYPE_NUMBER_SPACE	(2)
#define	PCMTYPE_SPECIAL		(3)

/*
 * Supported case mappings.
 *
 * Non-zero value means there is a case mapping requested. Refer to
 * strftime(3C) for more information. Currently, CASEMAP_TOLOWER is
 * internal use only in this file.
 */
#define	CASEMAP_NONE		(0)
#define	CASEMAP_SWITCH		(1)
#define	CASEMAP_TOUPPER		(2)
#define	CASEMAP_TOLOWER		(3)

/*
 * The following are string version of LLONG_MIN that we need for %s.
 */
#define	LLONG_MIN_STR		("-9223372036854775808")
#define	LLONG_MIN_STRLEN	(20)

/*
 * FUNCTION: get_alt_num()
 *	     This function convert a integral numeric value i into
 *	     character string, using the alternate numeric symbols.
 *	     The size of buf is assumed to be BUFSIZE.
 *
 * PARAMETERS:
 *	     int i - an integral value.
 *	     char *buf - address of user-supplied buffer.
 *	     const char *altnum - pointer to list of alternate digits, or
 *				empty string if none exists.
 *
 * RETURN VALUE DESCRIPTIONS:
 *	     It returns the byte length of the character string.
 */
static int
get_alt_num(int i, char *buf, const char *altnum)
{
	int	n;
	const char	*q;

	if (i < 0) {
		buf[0] = '\0';
		return (0);
	}

	/* i >= 0 */
	for (n = 0; n < i; n++) {
		q = strchr(altnum, ';');
		if (q == NULL)
			break;
		altnum = q + 1;
	}

	if (n < i) {
		/* no matching alternate numeric symbol found */
		buf[0] = '\0';
		return (0);
	}

	/*
	 * Now altnum points to the beginning of the alternate numeric symbol;
	 * locate the end of the symbol. (Most likely, the symbol will be
	 * multibyte characters).
	 */
	q = strchr(altnum, ';');
	if (q == NULL) {
		/*
		 * No terminating ';' found.
		 * Treat the entire sub-string from altnum as a symbol.
		 */
		q = altnum + strlen(altnum);
	}

	n = q - altnum;
	if (n < BUFSIZE) {
		/*
		 * if the buffer has enough room to copy the
		 * alternate numeric symbol and a null-terminator,
		 * copy it to the buffer.
		 */
		while (altnum < q) {
			*buf++ = *altnum++;
		}
	} else {
		n = 0;
	}

	*buf = '\0';

	return (n);
}

/*
 * FUNCTION: gettzoffset()
 *	     This function returns a string for offset from UTC,
 *           in ISO 8601:2000 format (+hhmm or -hhmm).
 *           Note the sign of UNIX timezone is opposite that of
 *           ISO 8601, so printed sign is reversed.
 *           The buffer is assumed to be at least 6 bytes long.
 *
 * PARAMETERS:
 *	     char *buffp - address of user-supplied buffer.
 *	     struct tm *tmp - time structure.
 *
 * RETURN VALUE DESCRIPTIONS:
 *	     The byte length of the string for offset returned.
 */
static size_t
gettzoffset(char *buffp, struct tm *tmp)
{
	time_t offset;
	int i;

	if (tmp->tm_isdst == 0) {
		offset = timezone;
	} else if (tmp->tm_isdst > 0) {
		offset = altzone;
	} else {
		*buffp = '\0';
		return (0);
	}

	/* Work with positive offset; reverse printed sign. */
	if (offset > 0) {
		*buffp = '-';
	} else {
		offset = -offset;
		*buffp = '+';
	}

	/*
	 * Convert offset to hours and then get the string for it, i.e., "hh".
	 * And then do the same for the minutes, "mm".
	 *
	 * The first and the last arguments to the GET_DEC_NUM_2() macro are
	 * not being used and just to hold places.
	 */
	i = offset / SECSPERHOUR;
	GET_DEC_NUM_2(i, i, buffp + 1, i);

	i = (offset % SECSPERHOUR) / SECSPERMIN;
	GET_DEC_NUM_2(i, i, buffp + 3, i);

	return (5);
}

/*
 *  This function returns the next era description segment
 *  in a semicolon separated string of concatenated era
 *  description segments.
 */
static char *
get_era_segment(const char *era_string)
{
	char *ptr;

	if (ptr = strchr(era_string, ';')) {
		return (++ptr);
	} else
		return (NULL);
}

#define	ADVANCE_ERASTRING(p) (p = strchr(p, ':') + 1)

/*
 *  This function extracts the fields of an era description
 *  segment.
 */
static int
extract_era_info(era_ptr era, const char *era_str)
{
	const char *endptr;
	size_t len;

	era->dir = era_str[0];
	if (era->dir != '-' && era->dir != '+')
		return (-1);
	ADVANCE_ERASTRING(era_str);
	era->offset = atoi(era_str);
	ADVANCE_ERASTRING(era_str);
	if ((endptr = strchr(era_str, ':')) == NULL)
		return (-2);
	len = endptr - era_str;
	(void) strncpy(era->st_date, era_str, len);
	*(era->st_date + len) = '\0';
	ADVANCE_ERASTRING(era_str);
	if ((endptr = strchr(era_str, ':')) == NULL)
		return (-3);
	len = endptr - era_str;
	(void) strncpy(era->end_date, era_str, len);
	*(era->end_date + len) = '\0';
	ADVANCE_ERASTRING(era_str);
	if ((endptr = strchr(era_str, ':')) == NULL)
		return (-4);
	len = endptr - era_str;
	(void) strncpy(era->name, era_str, len);
	*(era->name + len) = '\0';
	ADVANCE_ERASTRING(era_str);
	if ((endptr = strchr(era_str, ';')) == NULL) {
		if ((endptr = era_str + strlen(era_str)) <= era_str)
			return (-5);
	}
	len = endptr - era_str;
	(void) strncpy(era->form, era_str, len);
	*(era->form + len) = '\0';
	return (0);
}

/*
 * FUNCTION: conv_time()
 *	     This function converts the current Christian year into year
 *	     of the appropriate era. The era chosen such that the current
 *	     Chirstian year should fall between the start and end date of
 *	     the first matched era in the hdl->era string. All the era
 *	     associated information of a matched era will be stored in the era
 *	     structure and the era year will be stored in the year
 *	     variable.
 *
 * PARAMETERS:
 *	   _LC_TIME_t *hdl - the handle of the pointer to the LC_TIME
 *			     catagory of the specific locale.
 *	   struct tm *timeptr - date to be printed
 *	     era_ptr era - pointer to the current era.
 *	     int *year - year of the current era.
 *
 * RETURN VALUE DESCRIPTIONS:
 *	   - returns 1 if the current Christian year fall into an
 *	       valid era of the locale.
 *	   - returns 0 if not.
 */
static int
conv_time(_LC_time_t *hdl, struct tm *tm, era_ptr era, int *year)
{
	const char *era_s;
	char *str;
	int start_year;
	int start_month;
	int start_day;
	int end_year = 0;
	int end_month = 0;
	int end_day = 0;
	int cur_year;
	int cur_month;
	int cur_day;
	int no_limit;
	int found;
				/*
				 * extra = 1 when current date is less than
				 * the start date, otherwise 0. This is the
				 * adjustment for correct counting up to the
				 * month and day of the start date
				 */
	int extra = 0;
	cur_year = tm->tm_year + TM_YEAR_BASE;
	cur_month = tm->tm_mon + 1;
	cur_day = tm->tm_mday;

	era_s = *hdl->era;
	for (; era_s != NULL; era_s = get_era_segment(era_s)) {
		if (extract_era_info(era, era_s) != 0)
			continue;		/* Bad era string, try again */

		str = era->st_date;
		if (*str == '-') {
			str++;
			STRTONUM(str, start_year);
			start_year = -start_year;
		} else {
			STRTONUM(str, start_year);
		}

		str++;			/* skip the slash */
		STRTONUM(str, start_month);
		str++;			/* skip the slash */
		STRTONUM(str, start_day);

		str = era->end_date;
		if ((*str == '+' && *(str+1) == '*') ||
		    (*str == '-' && *(str+1) == '*')) {
			no_limit = 1;
		} else {
			no_limit = 0;
			if (*str == '-') {
				str++;
				STRTONUM(str, end_year);
				end_year = -end_year;
			} else {
				STRTONUM(str, end_year);
			}
			str++;		/* skip the slash */
			STRTONUM(str, end_month);
			str++;		/* skip the slash */
			STRTONUM(str, end_day);
		}
		if (no_limit && cur_year >= start_year) {
			found = 1;
		} else if (((cur_year > start_year) ||
		    (cur_year == start_year && cur_month > start_month) ||
		    (cur_year == start_year && cur_month == start_month &&
		    cur_day >= start_day)) &&
		    ((cur_year < end_year) ||
		    (cur_year == end_year && cur_month < end_month) ||
		    (cur_year == end_year && cur_month == end_month &&
		    cur_day <= end_day))) {
			found = 1;
		} else {
			continue;
		}

		if ((cur_month < start_month) ||
		    (cur_month == start_month && cur_day < start_day))
			extra = 1;
		if (era->dir == '+')
			*year = cur_year - start_year + era->offset - extra;
		else
			*year = end_year - cur_year - extra;

		if (found)
			return (1);
	}

	return (0);		/* No match for era times */
}


/*
 * FUNCTION: This function performs the actual formatting and it may
 *	     be called recursively with different values of code.
 *
 * PARAMETERS:
 *	   _LC_TIME_t *hdl - the handle of the pointer to the LC_TIME
 *			     category of the specific locale.
 *	   char *s - location of returned string
 *	   size_t maxsize - maximum length of output string
 *	   char *format - format that date is to be printed out
 *	   struct tm *timeptr - date to be printed
 *	   int gflag - this special attribute controls the outupt of
 *			certain field (eg: twelve hour form, without
 *			year or second for time and date format).
 *	   time_t secs - the number of seconds since the Epoch for %s.
 *
 * RETURN VALUE DESCRIPTIONS:
 *	   - returns the number of bytes that comprise the return string
 *	     excluding the terminating null character.
 *	   - returns 0 if s is longer than maxsize
 */
static size_t
doformat(_LC_time_t *hdl, char *s, size_t maxsize, const char *format,
	struct tm *timeptr, int gflag, time_t secs)
{
	int i;
	char ch;
	int firstday;		/* first day of the year */
	int weekno;
	char locbuffer[BUFSIZE]; /* local temporary buffer */
	wchar_t *wp;		/* Temporary wchar buffer pointer. */
	int year;		/* %o value, year in current era */
	const char *fbad;	/* points to where format start to be invalid */
	const char *p;		/* temp pointer */
	int altera;		/* Recursive call should reset 'altera' */
	struct era_struct eras;	/* the structure for current era */
	era_ptr era = &eras;	/* pointer to the current era */
	/* Points to alternate numeric representation */
	const char *altnum;
	int size;		/* counter of number of chars printed */
	static const char *xpg4_d_t_fmt = "%a %b %e %H:%M:%S %Y";
	struct tm	tm_tmp;
	int	wadj;
	int	adj_yday;
	char pc;		/* Pad character. */
	size_t fw;		/* Field width. */
	int cm;			/* Case mapping definition. */
	int pt;			/* Pad and casemap type. */
	size_t len;		/* The byte length of a date/time element. */
	long long llsecs;	/* The number of seconds from the Epoch. */
	size_t ret;		/* Return values from WPI. */

	size = 0;
	while ((ch = *format++) != '\0') {
		if (ch != '%') {
			if (++size < maxsize) {
				*s++ = ch;
			} else {
				if (maxsize > 0)
					*s = '\0';
				return (0);
			}
		} else {
			char *bufp; /* This should get set in loop */

			/*
			 * Save the starting address of the current format
			 * string for an error case.
			 */
			fbad = format;

			/*
			 * For each conversion spec, we reset pad character,
			 * field width, case mapping, pad & case mapping type,
			 * altnum, altera, and bufp mainly to give initial
			 * values and also for convenience.
			 */
			pc = PAD_UNDEFINED;
			fw = 0;
			cm = CASEMAP_NONE;
			pt = PCMTYPE_UNKNOWN;
			altnum = NULL;
			altera = 0;
			bufp = locbuffer;
			len = 0;

			/*
			 * Check if we have an optional flag character.
			 *
			 * We only check a single flag character. Any
			 * following subsequent character is a start of
			 * a decimal field width definition, a conversion
			 * character, or a undefined/invalid character.
			 *
			 * If a pad character is defined, we pad with it
			 * regardless of the actual character type of
			 * the following date/time element.
			 *
			 * If there is no pad character defined, the character
			 * type of the actual date/time element dictates what
			 * would be the default pad character.
			 */
			switch (*format) {
			case '#':	/* Convert the case. */
				cm = CASEMAP_SWITCH;
				format++;
				break;
			case '^':	/* Convert to uppercase characters. */
				cm = CASEMAP_TOUPPER;
				format++;
				break;
			case '-':	/* Do not pad anything. */
				pc = PAD_REMOVE;
				format++;
				break;
			case '0':	/* Pad left with zeros. */
				pc = PAD_ZERO;
				format++;
				break;
			case '_':	/* Pad left with space. */
				pc = PAD_SPACE;
				format++;
				break;
			}

			/*
			 * Check if we have an optional field width.
			 *
			 * We consume and skip any leading zeros as nothing at
			 * this point in the field width value formulation.
			 */
			while (isdigit((unsigned char)*format)) {
				fw = fw * 10 + *format++ - '0';
			}

			switch (*format) {
			case 'O':
				format++;
				if (!hdl->alt_digits ||
				    !strchr("BdegHImMSuUVwWy", *format))
					format = fbad;
				else if (*(hdl->alt_digits))
					altnum = hdl->alt_digits;
				break;

			case 'E':
				format++;
				if (!hdl->era || !strchr("cCgGxXyY", *format))
					format = fbad;
				else
					altera = 1;
				break;
			}

			/*
			 * Check each conversion character, find out and
			 * convert into appropriate values, determine the pad
			 * and case mapping type, and also the byte length.
			 *
			 * The pad and case mapping type and the byte length
			 * will be used later at the last step when padding
			 * and case mapping operations are performed.
			 */
			switch (*format++) {
			default:
				/*
				 * In case of a unrecognized/bad conversion
				 * character, we output '%' now and treat it
				 * as a non-conversion character at the next
				 * loop by resetting the format pointer and
				 * flow down to the case for '%' at below.
				 */
				format = fbad;
				fw = 0;

				/* FALLTHROUGH */

			case '%':	/* X/Open - percent sign */
				bufp = "%";
				pt = PCMTYPE_SPECIAL;
				len = 1;
				break;

			case 'n':	/* X/Open - newline character */
				bufp = "\n";
				pt = PCMTYPE_SPECIAL;
				len = 1;
				break;

			case 't':	/* X/Open - tab character */
				bufp = "\t";
				pt = PCMTYPE_SPECIAL;
				len = 1;
				break;

			case 'm':	/* X/Open - month in decimal number */
				/*
				 * The GETNUM_*() macros not only saves
				 * converted numbers but also set type and
				 * byte length of the date/time element.
				 */
				GETNUM_2(len, timeptr->tm_mon + 1, locbuffer,
				    altnum, pt);
				break;

			case 'd': 	/* X/Open - day of month in decimal */
				GETNUM_2(len, timeptr->tm_mday, locbuffer,
				    altnum, pt);
				break;

			case 'e':	/* day of month with leading space */
				GETNUM_2_SP(len, timeptr->tm_mday, locbuffer,
				    altnum, pt);
				break;

			case 'g':	/* week-based year w/o century 00-99 */
				tm_tmp = *timeptr;
				/*
				 * if called from %EG, don't calculate
				 * adjustment
				 */
				if (!gflag) {
					wadj = 10 - (tm_tmp.tm_wday + 6) % 7;
					weekno = (wadj + tm_tmp.tm_yday) / 7;
					if (weekno == 0) {
						tm_tmp.tm_year--;
					} else if ((tm_tmp.tm_yday > 361) &&
					    ((wadj + tm_tmp.tm_yday - 365 -
					    isleap(tm_tmp.tm_year +
					    TM_YEAR_BASE)) / 7 == 1)) {
						tm_tmp.tm_year++;
					}
				}
				if (altera) {
					if (conv_time(hdl, &tm_tmp, era,
					    &year)) {
						/*
						 * The GET_DEC_NUM_*() macros
						 * not only saves converted
						 * numbers but also set type
						 * and byte length of
						 * the date/time element.
						 */
						GET_DEC_NUM_4(len, year,
						    locbuffer, pt);
						while (*bufp == '0') {
							bufp++;
							len--;
						}
					} else {
						/*
						 * if era_year or era->form
						 * is not specified, %Ey will
						 * display %y output.
						 */
						GET_DEC_NUM_2(len,
						    tm_tmp.tm_year,
						    locbuffer, pt);
					}
				} else {
					GETNUM_2(len, tm_tmp.tm_year % 100,
					    locbuffer, altnum, pt);
				}
				break;

			case 'y':	/* X/Open - year w/o century 00-99 */
				if (altera) {
					if (conv_time(hdl, timeptr, era,
					    &year)) {
						GET_DEC_NUM_4(len, year,
						    locbuffer, pt);
						while (*bufp == '0') {
							bufp++;
							len--;
						}
					} else {
						/*
						 * if era_year or era->form
						 * is not specified, %Ey will
						 * display %y output.
						 */
						GET_DEC_NUM_2(len,
						    timeptr->tm_year % 100,
						    locbuffer, pt);
					}
				} else {
					GETNUM_2(len, timeptr->tm_year % 100,
					    locbuffer, altnum, pt);
				}
				break;

			case 'H':	/* X/Open - hour (0-23) in decimal */
				GETNUM_2(len, timeptr->tm_hour, locbuffer,
				    altnum, pt);
				break;

			case 'M':	/* X/Open - minute in decimal */
				GETNUM_2(len, timeptr->tm_min, locbuffer,
				    altnum, pt);
				break;

			case 's':
				/*
				 * Solaris extension:
				 *
				 * The number of seconds since the Epoch
				 * (00:00:00 UTC, January 1, 1970).
				 */

				/*
				 * Since lltostr() doesn't understand
				 * negative numbers well, we need to turn
				 * them into positive numbers and then later
				 * prefix the minus sign as needed.
				 *
				 * We use llsecs for a possibility that
				 * secs == LONG_MIN in 32-bit environment.
				 * We also need to save the sign to prefix
				 * '-' later anyway.
				 *
				 * In case of secs == LLONG_MIN,
				 * the lltostr() wouldn't be able to
				 * convert it and thus we just copy over
				 * necessary literal constant and other
				 * values and break.
				 *
				 * In any other cases of overflow, mktime()
				 * will give us -1 for secs.
				 */
				llsecs = (long long)secs;
				if (llsecs == LLONG_MIN) {
					(void) strcpy(locbuffer, LLONG_MIN_STR);
					pt = PCMTYPE_NUMBER_SPACE;
					len = LLONG_MIN_STRLEN;
					break;
				}
				if (llsecs < 0)
					llsecs = -llsecs;

				bufp = lltostr(llsecs,
				    locbuffer + EPOCH_SECS_OFFSET);
				locbuffer[EPOCH_SECS_OFFSET] = '\0';

				/*
				 * If there is '-' in front of the seconds,
				 * it would be most ideal to have the type of
				 * PCMTYPE_NUMBER_SPACE. Otherwise,
				 * PCMTYPE_NUMBER.
				 *
				 * Moving back one address position with bufp
				 * should be fine for the reason as described
				 * at the EPOCH_SECS_OFFSET macro definition.
				 */
				if (secs < 0) {
					*--bufp = '-';
					pt = PCMTYPE_NUMBER_SPACE;
				} else {
					pt = PCMTYPE_NUMBER;
				}

				len = locbuffer + EPOCH_SECS_OFFSET - bufp;

				break;

			case 'S':	/* X/Open - second in decimal */
				GETNUM_2(len, timeptr->tm_sec, locbuffer,
				    altnum, pt);
				break;

			case 'j': 	/* X/Open - day of year in decimal */
				GET_DEC_NUM_3(len, timeptr->tm_yday + 1,
				    locbuffer, pt);
				break;

			case 'w': 	/* X/Open - weekday in decimal */
				GETNUM_1(len, timeptr->tm_wday, locbuffer,
				    altnum, pt);
				break;

			case 'r': 	/* X/Open - time in AM/PM notation */
				/*
				 * In this case, we do not really know what
				 * would be the actual type and length of
				 * the date/time element.
				 *
				 * We will use PCMTYPE_UNKNOWN so that actual
				 * type and length will be determined at
				 * the last moment.
				 */
				len = doformat(hdl, locbuffer, BUFSIZE,
				    CHECKFMT(hdl->t_fmt_ampm, "%I:%M:%S %p"),
				    timeptr, 0, secs);
				break;

			case 'R':	/* X/Open - time as %H:%M */
				len = doformat(hdl, locbuffer, BUFSIZE,
				    "%H:%M", timeptr, 0, 0);
				pt = PCMTYPE_NUMBER;
				break;

			case 'T': 	/* X/Open - time in %H:%M:%S notation */
				len = doformat(hdl, locbuffer, BUFSIZE,
				    "%H:%M:%S", timeptr, 0, 0);
				pt = PCMTYPE_NUMBER;
				break;

			case 'X': 	/* X/Open - the locale time notation */
				if (altera && hdl->core.hdr.size >
				    offsetof(_LC_time_t, era_t_fmt))
					/*
					 * locale object is recent enough to
					 * have era_t_fmt field.
					 */
					p = CHECKFMT(hdl->era_t_fmt,
					    hdl->t_fmt);
				else
					p = hdl->t_fmt;

				len = doformat(hdl, locbuffer, BUFSIZE, p,
				    timeptr, 0, secs);
				break;

			case 'a': 	/* X/Open - locale's abv weekday name */
				/*
				 * It is also not certain what would be
				 * the actual type and length of the date/time
				 * element in this case and a few others at
				 * below. We defer the decision until the last
				 * moment by using PCMTYPE_UNKNOWN.
				 */
				(void) strlcpy(locbuffer,
				    hdl->abday[timeptr->tm_wday], BUFSIZE);
				break;

			case 'h':	/* X/Open - locale's abv month name */

			case 'b':
				(void) strlcpy(locbuffer,
				    hdl->abmon[timeptr->tm_mon], BUFSIZE);
				break;

			case 'P':
				/*
				 * Solaris extension:
				 *
				 * Locale's equivalent of either a.m. or p.m
				 * in lowercase if applicable for the current
				 * locale.
				 */
				/*
				 * We do the following by sepcifying
				 * an appropriate case mapping operation and
				 * flow into the case for %p at below.
				 */
				if (cm == CASEMAP_NONE)
					cm = CASEMAP_TOLOWER;
				else if (cm == CASEMAP_SWITCH)
					cm = CASEMAP_TOUPPER;

				/* FALLTHROUGH */

			case 'p': 	/* X/Open - locale's equivalent AM/PM */
				if (timeptr->tm_hour < 12)
					(void) strlcpy(locbuffer,
					    hdl->am_pm[0], BUFSIZE);
				else
					(void) strlcpy(locbuffer,
					    hdl->am_pm[1], BUFSIZE);
				break;

			case 'F':
				/*
				 * Solaris extension:
				 *
				 * Equivalent to %Y-%m-%d. The ISO 8601:2004
				 * standard date in extended format.
				 */
				len = doformat(hdl, locbuffer, BUFSIZE,
				    "%Y-%m-%d", timeptr, 0, 0);
				pt = PCMTYPE_NUMBER;
				break;

			case 'G':	/* week-based year w/century */
				tm_tmp = *timeptr;
				/*
				 * if called from %EG, don't calculate
				 * adjustment
				 */
				if (!gflag) {
					wadj = 10 - (tm_tmp.tm_wday + 6) % 7;
					weekno = (wadj + tm_tmp.tm_yday) / 7;
					if (weekno == 0) {
						/*
						 * if week falls in previous
						 * year, adjust year
						 */
						tm_tmp.tm_year--;
					} else if ((tm_tmp.tm_yday > 361) &&
					    ((wadj + tm_tmp.tm_yday - 365 -
					    isleap(tm_tmp.tm_year +
					    TM_YEAR_BASE)) / 7 == 1)) {
						/*
						 * if week falls in next year
						 * adjust year
						 */
						tm_tmp.tm_year++;
					}
				}
				if (altera) {
					if (conv_time(hdl, &tm_tmp, era,
					    &year)) {
						/* gflag should be 1 */
						len = doformat(hdl, locbuffer,
						    BUFSIZE, era->form,
						    &tm_tmp, 1, secs);
					} else {
						/*
						 * if era_year or era->form
						 * is not specified, %EG will
						 * display %G output.
						 */
						GET_DEC_NUM_4(len,
						    tm_tmp.tm_year +
						    TM_YEAR_BASE, locbuffer,
						    pt);
					}
				} else {
					GET_DEC_NUM_4(len, tm_tmp.tm_year +
					    TM_YEAR_BASE, locbuffer, pt);
				}
				break;

			case 'Y':	/* X/Open - year w/century in decimal */
				if (altera) { /* POSIX.2 %EY full altrnate yr */
					if (conv_time(hdl, timeptr, era,
					    &year)) {
						len = doformat(hdl, locbuffer,
						    BUFSIZE, era->form,
						    timeptr, 0, secs);
					} else {
						/*
						 * if era_year or era->form
						 * is not specified, %EY will
						 * display %Y output.
						 */
						GET_DEC_NUM_4(len,
						    timeptr->tm_year +
						    TM_YEAR_BASE,
						    locbuffer, pt);
					}
				} else {
					GET_DEC_NUM_4(len, timeptr->tm_year +
					    TM_YEAR_BASE, locbuffer, pt);
				}
				break;

			case 'z':	/* X/Open - timezone offset */
				len = gettzoffset(locbuffer, timeptr);
				/*
				 * Considering that the offset is an empty
				 * string, +hhmm, or -hhmm, it is rather more
				 * desireable and correct to have the type of
				 * number with space padding.
				 */
				pt = PCMTYPE_NUMBER_SPACE;
				break;

			case 'Z':	/* X/Open - timezone name if exists */
				/*
				 * We also do not really can tell the byte
				 * length of the time zone name. Hence, use
				 * PCMTYPE_UNKNOWN.
				 */
				bufp = (timeptr->tm_isdst == 0) ?
				    tzname[0] : (timeptr->tm_isdst > 0) ?
				    tzname[1] : "";
				break;

			case 'A': 	/* X/Open -locale's full weekday name */
				(void) strlcpy(locbuffer,
				    hdl->day[timeptr->tm_wday], BUFSIZE);
				break;

			case 'B':	/* X/Open - locale's full month name */
				/*
				 * This also covers %OB which is also
				 * a Solaris extension but due to that there is
				 * no locale LC_TIME entry for that, we just
				 * fallback to %B for now.
				 *
				 * Later, if necessary, this will be revisited
				 * as a new RFE with new localedef extensions.
				 */
				(void) strlcpy(locbuffer,
				    hdl->mon[timeptr->tm_mon], BUFSIZE);
				break;

			case 'I': 	/* X/Open - hour (1-12) in decimal */
				i = timeptr->tm_hour;
				GETNUM_2(len, (i > 12) ? i - 12 : (i) ? i : 12,
				    locbuffer, altnum, pt);
				break;

			case 'k': 	/* SunOS - hour (0-23) precede blank */
				GET_DEC_NUM_2_SP(len, timeptr->tm_hour,
				    locbuffer, pt);
				break;

			case 'l': 	/* SunOS - hour (1-12) precede blank */
				i = timeptr->tm_hour;
				GET_DEC_NUM_2_SP(len, (i > 12) ? i - 12 : (i) ?
				    i : 12, locbuffer, pt);
				break;

			case 'D': 	/* X/Open - date in %m/%d/%y format */
				len = doformat(hdl, locbuffer, BUFSIZE,
				    "%m/%d/%y", timeptr, 0, 0);
				pt = PCMTYPE_NUMBER;
				break;

			case 'x': 	/* X/Open - locale's date */
				if (altera)
					p = CHECKFMT(hdl->era_d_fmt,
					    hdl->d_fmt);
				else
					p = hdl->d_fmt;

				len = doformat(hdl, locbuffer, BUFSIZE,
				    p, timeptr, 0, secs);
				break;

			case 'c': 	/* X/Open - locale's date and time */
				if (altera) {
					/*
					 * %Ec and era_d_t_fmt field exists
					 */
					p = CHECKFMT(hdl->era_d_t_fmt,
					    hdl->d_t_fmt);
				} else {
					if (__xpg4 != 0) { /* XPG4 mode */
						if (IS_C_TIME(hdl)) {
							p = (char *)
							    xpg4_d_t_fmt;
						} else {
							p = hdl->d_t_fmt;
						}
					} else {	/* Solaris mode */
						p = hdl->d_t_fmt;
					}
				}

				len = doformat(hdl, locbuffer, BUFSIZE, p,
				    timeptr, 0, secs);
				break;

			case 'u':
				/*
				 * X/Open - week day as a number [1-7]
				 * (Monday as 1)
				 */
				if ((i = timeptr->tm_wday) == 0) {
					i = 7;
				}
				GETNUM_1(len, i, locbuffer, altnum, pt);
				break;

			case 'U':
				/*
				 * X/Open - week number of the year (0-53)
				 * (Sunday is the first day of week 1)
				 */
				weekno = (timeptr->tm_yday + 7 -
				    timeptr->tm_wday) / 7;
				GETNUM_2(len, weekno, locbuffer, altnum, pt);
				break;

			case 'V':
				/*
				 * ISO 8601 week number.
				 * In the ISO 8601 week-based system, weeks
				 * begin on a Monday and week 1 of the year
				 * is the week that includes both January 4th
				 * and the first Thursday of the year.
				 * If the first Monday of January is
				 * the 2nd, 3rd, or 4th, the preceding days
				 * are part of the last week of
				 * the preceding year.
				 */

				/*
				 * Pick wadj so that:
				 *	weekno = (wadj + timeptr->tm_yday) / 7;
				 * If timeptr->tm_yday is the first day of
				 * week 2,
				 *
				 * then
				 *	(wadj + timeptr->tm_yday - 1)/7 == 1
				 * and
				 *	(wadj + timeptr->tm_yday) / 7 == 2
				 * Solve for wadj:
				 *	wadj = 14 - timeptr->tm_yday
				 *
				 * First tm_wday  first tm_yday  wadj
				 *  day		   in week 2
				 *  sun    0		10	   4
				 *  mon    1		4	  10
				 *  tue    2		5	   9
				 *  wed    3		6	   8
				 *  thur   4		7	   7
				 *  fri    5		8	   6
				 *  sat    6		9	   5
				 *
				 * solve for wadj
				 * wadj = (10 - (timeptr->tm_wday + 6) % 7);
				 */

				wadj = 10 - (timeptr->tm_wday + 6) % 7;
				weekno = (wadj + timeptr->tm_yday) / 7;

				/* Is day in last week of previous year? */
				if (weekno == 0) {
					/*
					 * tm_yday is in the last week of
					 * previous year.  Calculate weekno
					 * by treating days as if they were
					 * additional days tm_yday = 365,
					 * 366, ... in previous year.
					 */
					adj_yday = timeptr->tm_yday + 365 +
					    isleap(timeptr->tm_year +
					    (TM_YEAR_BASE - 1));
					weekno = (wadj + adj_yday) / 7;
				} else if (timeptr->tm_yday > 361) {
					/*
					 * tm_yday could be in the first week
					 * of next year.  Calculate weekno by
					 * treating days as if they were in
					 * next year by subtracting the length
					 * of this year.  If adjusted weekno is
					 * 1, then it is in next year.
					 */
					adj_yday = timeptr->tm_yday - 365 -
					    isleap((timeptr->tm_year +
					    TM_YEAR_BASE));
					if ((wadj + adj_yday) / 7 == 1)
						weekno = 1;
				}

				GETNUM_2(len, weekno, locbuffer, altnum, pt);
				break;

			case 'W':
				/*
				 * X/Open - week number of the year (0-53)
				 * (Monday is the first day of week 1)
				 */
				firstday =
				    (timeptr->tm_wday + 6) % 7;	/* Prev day */
				weekno = (timeptr->tm_yday + 7 - firstday) / 7;

				GETNUM_2(len, weekno, locbuffer, altnum, pt);
				break;

			case '+':
				/*
				 * Solaris extension:
				 *
				 * Locale's date and time representation as
				 * produced by date(1).
				 */
				goto DF_SOLARIS_DATE;

			case 'C':
				if ((altera) && (conv_time(hdl, timeptr, era,
				    &year) == 1)) {
					bufp = era->name;
				} else if (__xpg4 == 0) { /* Solaris mode */
DF_SOLARIS_DATE:
					p = hdl->date_fmt;
					len = doformat(hdl, locbuffer,
					    BUFSIZE, p, timeptr, 0, secs);
				} else {	/* XPG4 mode */
					GET_DEC_NUM_2(len, ((timeptr->tm_year +
					    TM_YEAR_BASE) / 100),
					    locbuffer, pt);
				}
				break;
			}

			/*
			 * Now, we have a converted/retrieved string value
			 * we may need to perform case mapping, padding, or
			 * both operations.
			 */

			/*
			 * Do case mapping only if it is explicitly requested
			 * and the pad and case mapping type is unknown.
			 */
			if (cm != CASEMAP_NONE && pt == PCMTYPE_UNKNOWN) {
				wchar_t wbuf[BUFSIZE];	/* Temp wchar buffer. */

				/*
				 * To do case conversions, we need to
				 * first convert what bufp points into
				 * a wide character string.
				 *
				 * If illegal byte is detected during
				 * mbstowcs(), then, we cannot really do
				 * case mapping operation and, in that case,
				 * just skip the operation.
				 */
				ret = mbstowcs(wbuf, bufp, BUFSIZ);
				if (ret == (size_t)-1) {
					goto DF_SKIP_CASE_MAPPING;
				}

				/*
				 * Do case mapping.
				 *
				 * It is assumed that the bufp is always
				 * terminated with a NULL and so thus wbuf.
				 */
				wp = wbuf;
				if (cm == CASEMAP_SWITCH) {
					/*
					 * Skip any leading space class
					 * characters.
					 */
					while (iswspace(*wp))
						wp++;

					/*
					 * Move forward until you encounter
					 * a character that has a case and
					 * then figure out what kind of case
					 * conversion will be needed for
					 * the remainder of the string.
					 *
					 * If it has a title case, preserve
					 * the first uppercase letter and
					 * apply toupper case conversion for
					 * the remaining characters.
					 */
					while (*wp) {
						if (iswupper(*wp)) {
							if (*(wp + 1) &&
							    iswlower(*(wp+1))) {
								wp++;
								goto DF_TOUPPER;
							}
							goto DF_TOLOWER;
						} else if (iswlower(*wp)) {
							goto DF_TOUPPER;
						}

						wp++;
					}
				} else if (cm == CASEMAP_TOUPPER) {
DF_TOUPPER:
					while (*wp) {
						*wp = towupper(*wp);
						wp++;
					}
				} else if (cm == CASEMAP_TOLOWER) {
DF_TOLOWER:
					while (*wp) {
						*wp = towlower(*wp);
						wp++;
					}
				} else {
					/* This shouldn't happen. */
					goto DF_SKIP_CASE_MAPPING;
				}

				/*
				 * Convert back to a multibyte string.
				 *
				 * By the definition of wide character codes
				 * and multibyte character codes and their
				 * 1:1 unique mapping nature, if mbstowcs()
				 * was successful, then, wcstombs() must be
				 * also successful and will not cause any
				 * conversion failure.
				 *
				 * This also applies to towupper() and
				 * towlower() operations.
				 */
				(void) wcstombs(bufp, wbuf, BUFSIZE);
			}

DF_SKIP_CASE_MAPPING:
			/*
			 * If the length is still not yet known, compute it
			 * with strlen().
			 */
			if (len == 0)
				len = strlen(bufp);

			/*
			 * If there is a field width defined, an explicit
			 * padding behavior defined, or both, then, we perform
			 * padding opertation.
			 *
			 * To be compatible with other platforms, the unit
			 * of field width will be in byte length not the so-
			 * called screen column width of terminals with fixed
			 * width fonts.
			 */
			if (fw > 0 || pc != PAD_UNDEFINED) {
				/*
				 * If PAD_REMOVE is requested, any leading
				 * zero ('0') or space (' ') characters in
				 * front of a number will be removed.
				 *
				 * We also do the same if the pad character is
				 * PAD_SPACE and the pad/map type is either
				 * PCMTYPE_NUMBER or PCMTYPE_UNKNOWN. So thus
				 * if the pad character is PAD_ZERO and the
				 * pad/map type is PCMTYPE_NUMBER_SPACE or
				 * PCMTYPE_UNKNOWN. This includes a case where
				 * a number is preceded by a space (' ') such
				 * as %e, %k, and %l and also a case where
				 * a number is possibly preceded by a '0' such
				 * as %H, %M, %T, and %R.
				 */
				if (pc == PAD_REMOVE ||
				    (pc == PAD_SPACE && (pt == PCMTYPE_NUMBER ||
				    pt == PCMTYPE_UNKNOWN)) ||
				    (pc == PAD_ZERO && (pt == PCMTYPE_UNKNOWN ||
				    pt == PCMTYPE_NUMBER_SPACE))) {
					p = bufp;

					while (*p == ' ' || *p == '0')
						p++;

					if (*p == '\0' && p != bufp &&
					    *(p - 1) == '0')
						--p;

					if (isdigit((unsigned char)*p)) {
						if (fw == 0)
							fw = len;
						len -= p - bufp;
						bufp = (char *)p;
					}
				}

				/*
				 * We pad only when the field width defined
				 * is bigger than the actual byte length and
				 * PAD_REMOVE is not explicitly defined.
				 *
				 * For a case that a field width is also
				 * defined with PAD_REMOVE, we do not pad
				 * anything to be compatible with other
				 * platforms.
				 */
				if (fw > len && pc != PAD_REMOVE) {
					/*
					 * If pad character isn't explicitly
					 * specified, we decide on it by
					 * looking at the pad & case map type,
					 * the first character, or both.
					 */
					if (pc == PAD_UNDEFINED) {
						if (pt == PCMTYPE_NUMBER ||
						    (pt == PCMTYPE_UNKNOWN &&
						    isdigit(
						    (unsigned char)*bufp)))
							pc = PAD_ZERO;
						else
							pc = PAD_SPACE;
					}

					/*
					 * This is the amount/number of padding
					 * that will be applied.
					 */
					fw = fw - len;
				} else {
					fw = 0;
				}
			}

			/*
			 * Finally, we copy over the resulting characters.
			 */
			size += fw + len;
			if (size < maxsize) {
				while (fw > 0) {
					*s++ = pc;
					fw--;
				}

				while (*bufp)
					*s++ = *bufp++;
			} else {
				if (maxsize > 0)
					*s = '\0';
				return (0);
			}
		} /* if (ch != '%'). */
	}

	if (maxsize > 0)
		*s = '\0';
	return (size);
}


/*
 * FUNCTION: strfmon_std()
 *	     This is the standard method to format the date and ouput to
 *	     the output buffer s. The values returned are affected by
 *	     the setting of the locale category LC_TIME and the time
 *	     information in the tm time structure.
 *
 * PARAMETERS:
 *	   _LC_TIME_t *hdl - the handle of the pointer to the LC_TIME
 *			       catagory of the specific locale.
 *	   char *s - location of returned string
 *	   size_t maxsize - maximum length of output string
 *	   char *fmt - format that date is to be printed out
 *	   struct tm *timeptr - date to be printed
 *
 * RETURN VALUE DESCRIPTIONS:
 *	   - returns the number of bytes that comprise the return string
 *	       excluding the terminating null character.
 *	   - returns 0 if s is longer than maxsize
 */
size_t
__strftime_std(_LC_time_t *hdl, char *s, size_t maxsize, const char *fmt,
	const struct tm *timeptr)
{
	time_t secs;

	if (fmt == NULL)
		fmt = "%c";	/* SVVS 4.0 */
	/*
	 * Invoke mktime, for its side effects (timezone).
	 *
	 * Also, collect the number of seconds since the Epoch for
	 * a possible use for the conversion specification, %s.
	 *
	 * In case of overflow or the seconds cannot be represented within
	 * the possible range of time_t, then, mktime() will set EOVERFLOW
	 * errno and the seconds returned will be -1. We do not do any
	 * specific things on such error cases but it, the "-1" as a string,
	 * will be given to the caller.
	 */
	{
		struct tm tmp = *timeptr;

		secs = mktime(&tmp);
	}
	return (doformat(hdl, s, maxsize, fmt, (struct tm *)timeptr, 0, secs));
}


/*
 * FUNCTION: strftime() is a method driven function where the time formatting
 *           processes are done the method points by __lc_time->core.strftime.
 *           It formats the date and output to the output buffer s. The values
 *           returned are affected by the setting of the locale category
 *           LC_TIME and the time information in the tm time structure.
 *
 * PARAMETERS:
 *           char *s - location of returned string
 *           size_t maxsize - maximum length of output string
 *           char *format - format that date is to be printed out
 *           struct tm *timeptr - date to be printed
 *
 * RETURN VALUE DESCRIPTIONS:
 *           - returns the number of bytes that comprise the return string
 *             excluding the terminating null character.
 *           - returns 0 if s is longer than maxsize
 */

size_t
strftime(char *s, size_t maxsize, const char *format,
		const struct tm *timeptr)
{
	return (METHOD(__lc_time, strftime)
	    (__lc_time, s, maxsize, format, timeptr));
}
