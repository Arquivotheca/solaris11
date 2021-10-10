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
 * static char rcsid[] = "@(#)$RCSfile: strptime.c,v $ $Revision: 1.4.5.4 $ "
 * "(OSF) $Date: 1992/11/30 16:15:30 $";
 * #endif
 */
/*
 * COMPONENT_NAME: LIBCFMT
 *
 * FUNCTIONS:  strptime
 *
 * (C) COPYRIGHT International Business Machines Corp. 1991
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 *
 * 1.3  com/lib/c/fmt/strptime.c, libcfmt, 9130320 7/17/91 15:23:44
 * 1.8  com/lib/c/fmt/__strptime_std.c, libcfmt,9140320 9/26/91 14:00:15
 */

/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

#pragma	weak _getdate = getdate
#pragma	weak _strptime = strptime

#include "lint.h"
#include "mtlib.h"
#include <sys/types.h>
#include <sys/localedef.h>
#include <sys/stat.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <nl_types.h>
#include <langinfo.h>
#include <thread.h>
#include <ctype.h>
#include <limits.h>
#include <wchar.h>
#include "libc.h"
#include "tsd.h"
#include "xpg6.h"

#define	MONTH		12
#define	DAY_MON		31
#define	HOUR_24		23
#define	HOUR_12		11
#define	DAY_YR		366
#define	MINUTE		59
#define	SECOND_XPG5	61	/* XPG5 range for %S is [0,61] */
#define	SECOND_XPG6	60	/* XPG6 range for %S is [0,60] */
#define	VALID_SECOND	59
#define	WEEK_YR		53
#define	YEAR_1900	1900
#define	YEAR_2000	2000

enum { SUNDAY = 0, MONDAY, TUESDAY, WEDNESDAY, THURSDAY, FRIDAY, SATURDAY };

#define	JANUARY		(0)
#define	DECEMBER	(11)

#define	SKIP_TO_NWHITE(s)	while (*s && (isspace(*s))) s++

/*
 * Global data for internal routines.
 * Use structure and pass by reference to make code thread safe.
 */
typedef struct strptime_data {
	int hour;	/* value of hour from %H */
	int meridian;	/* value of AM or PM */
	int era_base;	/* lvalue of era base (%EC) */
	int era_offset;	/* value of era offset (%Ey) */
	int week_number_u; /* contains the week of the year %U */
	int week_number_v; /* contains the week of the year %V */
	int week_number_w; /* contains the week of the year %W */
	int century;	/* contains the century number */
	int week_based_year; /* the week-based year within century */
	int week_based_year_century; /* the week-based year including century */
	int calling_func; /* indicates which function called strptime_recurs */
	int calling_strptime; /* which strptime called strptime_recurs */
	int wrong_input;  /* indicates wrong input */
	int tm_mask;  /* mask for which tm fields are set */
} strptime_data_t;

/* Retain simple names.  */
#define	hour		(strptime_data->hour)
#define	meridian	(strptime_data->meridian)
#define	era_base	(strptime_data->era_base)
#define	era_offset	(strptime_data->era_offset)
#define	week_number_u    (strptime_data->week_number_u)
#define	week_number_v    (strptime_data->week_number_v)
#define	week_number_w    (strptime_data->week_number_w)
#define	century		(strptime_data->century)
#define	week_based_year	(strptime_data->week_based_year)
#define	week_based_year_century	(strptime_data->week_based_year_century)
#define	calling_func	(strptime_data->calling_func)
#define	calling_strptime	(strptime_data->calling_strptime)
#define	wrong_input	(strptime_data->wrong_input)
#define	tm_mask		(strptime_data->tm_mask)

/* flags for tm_mask */
#define	f_sec		0x0001
#define	f_min		0x0002
#define	f_hour		0x0004
#define	f_mday		0x0008
#define	f_mon		0x0010
#define	f_year		0x0020
#define	f_wday		0x0040
#define	f_yday		0x0080
#define	f_isdst		0x0100

/* More flags */
#define	f_year_era	0x0200		/* tm_year set by era's */

#define	ISSET(a, b)	((a) & (b))
#define	SET(a, b)	((a) |= (b))

/*
 *  tm_isdst flags to identify strptime() vs. _strptime_dontzero()
 */
#define	TM_ISDST_ZERO	0x10
#define	TM_ISDST_GT	0x20
#define	TM_ISDST_LT	0x40
#define	TM_MARK		(TM_ISDST_ZERO | TM_ISDST_GT | TM_ISDST_LT)

typedef enum {f_getdate, f_strptime} calling_func_t;	/* for calling_func */
enum {f_strptime_dozero, f_strptime_dontzero};	/* for calling_strptime */
enum {FILLER, AM, PM};		/* for meridian */

struct era_struct {
	char	dir;		/* dircetion of the current era */
	int	offset;		/* offset of the current era */
	char	st_date[100];	/* start date of the current era */
	char	end_date[100];	/* end date of the current era */
	char	name[100];	/* name of the current era */
	char	form[100];	/* format string of the current era */
};
typedef struct era_struct *era_ptr;

typedef struct simple_date {
	int	day;
	int	month;
	int	year;
} simple_date;

extern const int __lyday_to_month[];
extern const int __yday_to_month[];
extern const int __mon_lengths[2][12];
extern mutex_t _time_lock;

static struct tm	*calc_date(struct tm *, strptime_data_t *);
static int	read_tmpl(_LC_time_t *, char *, struct tm *, struct tm *,
    strptime_data_t *);
static void	getnow(struct tm *);
static void	init_str_data(strptime_data_t *, calling_func_t);
static int	verify_getdate(struct tm *, struct tm *, strptime_data_t *);
static int	verify_strptime(struct tm *, struct tm *, strptime_data_t *);
static void	Day(int, struct tm *);
static void	DMY(struct tm *);
static int	days(int);
static int	jan1(int);
static int	yday(struct tm *, int, struct tm *, strptime_data_t *);
static int	week_number_to_yday(struct tm *, int, strptime_data_t *);
static void	year(int, struct tm *);
static void	MON(int, struct tm *);
static void	Month(int, struct tm *);
static void	DOW(int, struct tm *);
static void	adddays(int, struct tm *);
static void	DOY(int, struct tm *);
static int	get_number(const unsigned char **, int, const char *, int);
static int	ascii_number(const unsigned char **, int);
static int	search_alt_digits(const unsigned char **, const char *);
static int	compare_str(const unsigned char *, const unsigned char *);

char	*__strptime_std(_LC_time_t *, const char *, const char *, struct tm *);
char	*__strptime_dontzero(const char *, const char *, struct tm *);

struct tm	*__getdate_std(_LC_time_t *, const char *);
static unsigned char	*strptime_recurse(_LC_time_t *, const unsigned char *,
    const unsigned char *, struct tm *, struct tm *, strptime_data_t *, int);

/*
 * FUNCTION: strptime() is a method driven functions where the time formatting
 *	     processes are done in the method points by
 *	     __lc_time->core.strptime.
 *           It parse the input buffer according to the format string. If
 *           time related data are recgonized, updates the tm time structure
 *           accordingly.
 *
 * PARAMETERS:
 *           const char *buf - the input data buffer to be parsed for any
 *                             time related information.
 *           const char *fmt - the format string which specifies the expected
 *                             format to be input from the input buf.
 *           struct tm *tm   - the time structure to be filled when appropriate
 *                             time related information is found.
 *
 * RETURN VALUE DESCRIPTIONS:
 *           - if successful, it returns the pointer to the character after
 *             the last parsed character in the input buf string.
 *           - if fail for any reason, it returns a NULL pointer.
 */
char *
strptime(const char *buf, const char *fmt, struct tm *tm)
{
	/* zero the tm struct */
	(void) memset(tm, 0, sizeof (struct tm));

	return (METHOD(__lc_time, strptime)(__lc_time, buf, fmt, tm));
}


char *
__strptime_dontzero(const char *buf, const char *fmt, struct tm *tm)
{
	char *retstr;
	int isdst_save;

	isdst_save = tm->tm_isdst;

	if (tm->tm_isdst == 0) {
		tm->tm_isdst = TM_ISDST_ZERO;
	} else if (tm->tm_isdst > 0) {
		tm->tm_isdst = TM_ISDST_GT;
	} else if (tm->tm_isdst < 0) {
		tm->tm_isdst = TM_ISDST_LT;
	}
	retstr = METHOD(__lc_time, strptime)(__lc_time, buf, fmt, tm);

	if ((tm->tm_isdst == TM_ISDST_ZERO) || (tm->tm_isdst == TM_ISDST_GT) ||
	    (tm->tm_isdst == TM_ISDST_LT))
		tm->tm_isdst = isdst_save;

	return (retstr);
}

struct  tm *
getdate(const char *expression)
{
	return (METHOD(__lc_time, getdate)(__lc_time, expression));
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
	} else {
		return (NULL);
	}
}

/*
 *  This function extracts a date from a
 *  date string of the form:  mm/dd/yy.
 */
static void
extract_era_date(struct simple_date *date, const char *era_str)
{
	char *p = (char *)era_str;

	if (p[1] == '*') {
		if (p[0] == '-') {	/* dawn of time */
			date->day = 1;
			date->month = 0;
			date->year = INT_MIN;
		} else {		/* end of time */
			date->day = 31;
			date->month = 11;
			date->year = INT_MAX;
		}
		return;
	}

	date->year = atoi(p);
	if (strchr(p, ':') < strchr(p, '/')) {	/* date is year only */
		date->month = 0;
		date->day = 1;
		return;
	}
	p = strchr(p, '/') + 1;
	date->month = atoi(p) - 1;
	p = strchr(p, '/') + 1;
	date->day = atoi(p);
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
	if (era->dir != '-' && era->dir != '+') {
		return (-1);
	}
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

static unsigned char *
parse_alternate(_LC_time_t *hdl, const unsigned char *buf,
    const unsigned char *fmt, struct tm *tm, struct tm *ct,
    strptime_data_t *strptime_data)
{
	int	off, len;
	unsigned char *newbuf;
	unsigned char *efmt;
	const char *era_s;
	struct era_struct era_struct;
	era_ptr era;
	simple_date stdate;		/* start date */
	struct tm tm_tmp;			/* save a copy of tm struct */

	SKIP_TO_NWHITE(buf);

	era = &era_struct;

	/* check for (hdl->era && *hdl->era) done in strptime_recurse() */

	era_s = *hdl->era;
	for (; era_s != NULL; era_s = get_era_segment(era_s)) {
		tm_tmp = *tm;

		if (extract_era_info(era, era_s) != 0)
			continue;	/* Malformated era, ignore it */

		switch (*fmt) {
		case 'c':		/* Alternative date time */
			efmt = (unsigned char *)nl_langinfo(ERA_D_T_FMT);
			break;

		case 'Y':
			efmt = (unsigned char *)era->form;	/* for %EY */
			break;

		case 'C':		/* Base year */
			if ((len = compare_str(buf,
			    (const unsigned char *)era->name)) < 0)
				continue;
			buf += len;
			extract_era_date(&stdate, era->st_date);

			/* make offset 0-based so gannen era entry works */
			era_base = stdate.year - (era->offset - 1) - 1900;
			SET(tm_mask, f_year_era);
			goto matched;

		case 'x':		/* Alternative date representation */
			efmt = (unsigned char *)nl_langinfo(ERA_D_FMT);
			break;

		case 'X':		/* Alternative time format */
			efmt = (unsigned char *)nl_langinfo(ERA_T_FMT);
			break;

		case 'y':		/* offset from %EC(year only) */
			if ((off = get_number(&buf, 8, NULL, NULL)) == -1) {
				continue;
			} else {
				/*
				 * make offset 0-based so gannen
				 * era entry works (%Ey=1 is assumed)
				 */
				era_offset = off - 1;
				SET(tm_mask, f_year_era);
			}
			goto matched;

		default:
			return (NULL);
		}
		/* recurse using efmt set above */
		newbuf = strptime_recurse(hdl, buf, efmt, &tm_tmp, ct,
		    strptime_data, 1);
		if (newbuf == NULL)
			continue;
		buf = newbuf;
		goto matched;

	} /* end for */

	return (NULL);		/* Fell thru for-loop without matching era */

matched:
	/*
	 * Here only when matched on appropriate era construct
	 */
	*tm = tm_tmp;	/* put final values to tm struct if OK */
	return ((unsigned char *)buf);
}

/*
 * FUNCTION: This the standard method for function strptime and getdate.
 *	     It parses the input buffer according to the format string. If
 *	     time related data are recgonized, updates the tm time structure
 *	     accordingly.
 *
 * PARAMETERS:
 *           _LC_time_t *hdl - pointer to the handle of the LC_TIME
 *			       catagory which contains all the time related
 *			       information of the specific locale.
 *	     const char *buf - the input data buffer to be parsed for any
 *			       time related information.
 *	     const char *fmt - the format string which specifies the expected
 *			       format to be input from the input buf.
 *	     struct tm *tm   - the time structure to be filled when appropriate
 *			       time related information is found.
 *			       The fields of tm structure are:
 *
 *			       int 	tm_sec		seconds [0,61]
 *			       int	tm_min		minutes [0,61]
 *			       int	tm_hour		hour [0,23]
 *			       int	tm_mday		day of month [1,31]
 *			       int	tm_mon		month of year [0,11]
 *			       int	tm_wday		day of week [0,6] Sun=0
 *			       int	tm_yday		day of year [0,365]
 *			       int 	tm_isdst	daylight saving flag
 *	     struct tm *ct   - the time structure for current time.
 *			       Used mainly to support getdate.
 *	     strptime_data_t *strptime_data   - stores data used between
 *				routines.
 *	     int flag	     - flag for recursive call (1 = recursive call)
 *
 * RETURN VALUE DESCRIPTIONS:
 *           - if successful, for strptime, it returns the pointer to the
 *	       character after the last parsed character in the input buf
 *	       string.  For getdate, it returns 1.
 *           - if fail for any reason, it returns a NULL pointer.
 */
static unsigned char *
strptime_recurse(_LC_time_t *hdl, const unsigned char *buf,
    const unsigned char *fmt, struct tm *tm, struct tm *ct,
    strptime_data_t *strptime_data, int flag)
{
	unsigned char	bufchr;		/* current char in buf string */
	unsigned char	fmtchr;		/* current char in fmt string */
	int	found;		/* boolean flag for a match of buf and fmt */
	int	width;		/* the field width of an locale entry */
	int 	lwidth;		/* the field width of an locale entry */
	int	i, secs;
	int	oflag;
	int	blen, flen;
	int	mbmax;
	wchar_t	wfmtchr, wbufchr;

	mbmax = MB_CUR_MAX;

	if (flag == 0) {
		getnow(ct);
		if (calling_func == f_strptime) {
			if (tm->tm_isdst & TM_MARK) {
				calling_strptime = f_strptime_dontzero;
			} else {
				calling_strptime = f_strptime_dozero;
			}
		}
	}
	SKIP_TO_NWHITE(fmt);
	while (((fmtchr = *fmt) != NULL) && ((bufchr = *buf) != NULL)) {
						/* stop when buf or fmt ends */
		oflag = 0;
		if (fmtchr != '%') {
			SKIP_TO_NWHITE(buf);
			bufchr = *buf;
			if (isascii(fmtchr) ||
			    ((flen = mbtowc(&wfmtchr,
			    (const char *)fmt, mbmax)) == -1) ||
			    ((blen = mbtowc(&wbufchr,
			    (const char *)buf, mbmax)) == -1)) {
				/*
				 * If fmt is ascii character, use
				 * single-byte tolower() routine.
				 * If fmt is multibyte, but illegal multibyte
				 * character is encountered in format or input
				 * string, default to use of single-byte
				 * tolower() routine.
				 */
				if (tolower(bufchr) == tolower(fmtchr)) {
					fmt++;
					buf++;
					SKIP_TO_NWHITE(fmt);
					continue;    /* skip ordinary char */
				} else {
					/* case mismatch detected  */
					return (NULL);
				}
			} else {
				/*
				 * Multibyte strings encountered.
				 * Use wide char towlower() to check case.
				 */
				if (towlower((wint_t)wbufchr) ==
				    towlower((wint_t)wfmtchr)) {
					fmt += flen;
					buf += blen;
					SKIP_TO_NWHITE(fmt);
					continue;    /* skip ordinary mb-char */
				} else {
					/* case mismatch detected  */
					return (NULL);
				}
			}
		} else {			/* fmtchr == '%' */
			fmt++;
			fmtchr = *fmt++;
			if (fmtchr == 'O') {
				oflag++;
				fmtchr = *fmt++;
			}
			switch (fmtchr) {
			case 'a':
			case 'A':
			/* locale's full or abbreviate weekday name */
				SKIP_TO_NWHITE(buf);
				found = 0;
				if ((calling_func == f_strptime) ||
				    (fmtchr == 'A')) {
					for (i = 0; i < 7 && !found; i++) {
						if ((lwidth = compare_str(buf,
						    (const unsigned char *)
						    hdl->day[i])) > 0) {
							found = 1;
							buf += lwidth;
							break;
						}
					}
				}
				if ((found == 0) &&
				    ((calling_func == f_strptime) ||
				    (fmtchr == 'a'))) {
					for (i = 0; i < 7 && !found; i++) {
						if ((width = compare_str(buf,
						    (const unsigned char *)
						    hdl->abday[i])) > 0) {
							found = 1;
							buf += width;
							break;
						}
					}
				}
				if (found == 0)
					return (NULL);
				if (ISSET(tm_mask, f_wday) &&
				    tm->tm_wday != i) {
					wrong_input++;
					return (NULL);
				}
				tm->tm_wday = i;
				SET(tm_mask, f_wday);
				break;

			case 'b':
			case 'B':
			case 'h':
			/* locale's full or abbreviate month name */
				SKIP_TO_NWHITE(buf);
				found = 0;
				if ((calling_func == f_strptime) ||
				    (fmtchr == 'B')) {
					for (i = 0; i < 12 && !found; i++) {
						if ((lwidth = compare_str(buf,
						    (const unsigned char *)
						    hdl->mon[i])) > 0) {
							found = 1;
							buf += lwidth;
							break;
						}
					}
				}
				if ((!found) && ((calling_func == f_strptime) ||
				    (fmtchr == 'b') || (fmtchr == 'h'))) {
					for (i = 0; i < 12 && !found; i++) {
						if ((width = compare_str(buf,
						    (const unsigned char *)
						    hdl->abmon[i])) > 0) {
							found = 1;
							buf += width;
							break;
						}
					}
				}
				if (found == 0) {
					return (NULL);
				}
				if (ISSET(tm_mask, f_mon) && tm->tm_mon != i) {
					wrong_input++;
					return (NULL);
				}
				tm->tm_mon = i;
				SET(tm_mask, f_mon);
				break;

			case 'c': 		/* locale's date and time */
				SKIP_TO_NWHITE(buf);
				if ((buf = strptime_recurse(hdl, buf,
				    (const unsigned char *)hdl->d_t_fmt, tm, ct,
				    strptime_data, 1)) == NULL)
					return (NULL);
				break;

			case 'd':		/* day of month, 1-31 */
			case 'e':
				SKIP_TO_NWHITE(buf);
				i = get_number(&buf, 2, hdl->alt_digits, oflag);
				if (i < 1 || i > DAY_MON) {
					return (NULL);
				}
				if (ISSET(tm_mask, f_mday) &&
				    tm->tm_mday != i) {
					wrong_input++;
					return (NULL);
				}
				tm->tm_mday = i;
				SET(tm_mask, f_mday);
				break;

			case 'D':		/* %m/%d/%y */
				SKIP_TO_NWHITE(buf);
				if ((buf = strptime_recurse(hdl, buf,
				    (const unsigned char *)"%m/%d/%y", tm, ct,
				    strptime_data, 1)) == NULL)
					return (NULL);
				break;

			case 'E':
				/* Is format valid? */
				if (strchr("cCxXyY", *fmt) == NULL)
					return (NULL);
				/* Are there ERAs in this locale? */
				if (hdl->era && *hdl->era) {
					/* yes */
					buf = parse_alternate(hdl, buf, fmt,
					    tm, ct, strptime_data);
				} else {
					/* no, use unmodified format. */
					unsigned char rfmt[3] = "% \0";
					rfmt[1] = *fmt;
					buf = strptime_recurse(hdl, buf, rfmt,
					    tm, ct, strptime_data, 1);
				}
				if (buf == NULL)
					return (NULL);
				fmt++;
				break;

			case 'F':		/* %Y-%m-%d */
				SKIP_TO_NWHITE(buf);
				if ((buf = strptime_recurse(hdl, buf,
				    (const unsigned char *)"%Y-%m-%d", tm, ct,
				    strptime_data, 1)) == NULL)
					return (NULL);
				break;

			case 'g':		/* week-based year, 00-99 */
				SKIP_TO_NWHITE(buf);
				i = get_number(&buf, 2, hdl->alt_digits, oflag);
				if (i < 0)
					return (NULL);
				if (week_based_year != -1 &&
				    week_based_year != i) {
					wrong_input++;
					return (NULL);
				}
				week_based_year = i;
				break;

			case 'G':
			/* week-based year with century, 0000-9999 */
				SKIP_TO_NWHITE(buf);
				i = get_number(&buf, 4, hdl->alt_digits, oflag);
				if (i < 0)
					return (NULL);
				if (week_based_year_century != -1 &&
				    week_based_year_century != i) {
					wrong_input++;
					return (NULL);
				}
				week_based_year_century = i;
				break;

			case 'H':		/* hour 0-23 */
			case 'k':
				SKIP_TO_NWHITE(buf);
				i = get_number(&buf, 2, hdl->alt_digits, oflag);
				if (i >= 0 && i <= HOUR_24)
					i = i + 1;
				else
					return (NULL);
				if (hour && hour != i) {
					wrong_input++;
					return (NULL);
				}
				hour = i;
				break;

			case 'I':		/* hour 1-12 */
			case 'l':
				SKIP_TO_NWHITE(buf);
				i = get_number(&buf, 2, hdl->alt_digits, oflag);
				if (i < 1 || i > HOUR_12 + 1)
					return (NULL);
				if (ISSET(tm_mask, f_hour) &&
				    tm->tm_hour != i) {
					wrong_input++;
					return (NULL);
				}
				tm->tm_hour = i;
				SET(tm_mask, f_hour);
				break;

			case 'j':		/* day of year, 1-366 */
				SKIP_TO_NWHITE(buf);
				i = get_number(&buf, 3, hdl->alt_digits, oflag);
				if (i < 1 || i > DAY_YR)
					return (NULL);
				i--;
				if (ISSET(tm_mask, f_yday) &&
				    tm->tm_yday != i) {
					wrong_input++;
					return (NULL);
				}
				tm->tm_yday = i;
				SET(tm_mask, f_yday);
				break;

			case 'm':		/* month of year, 1-12 */
				SKIP_TO_NWHITE(buf);
				i = get_number(&buf, 2, hdl->alt_digits, oflag);
				if (i <= 0 || i > MONTH)
					return (NULL);
				i--;
				if (ISSET(tm_mask, f_mon) && tm->tm_mon != i) {
					wrong_input++;
					return (NULL);
				}
				tm->tm_mon = i;
				SET(tm_mask, f_mon);
				break;

			case 'M':		/* minute 0-59 */
				SKIP_TO_NWHITE(buf);
				i = get_number(&buf, 2, hdl->alt_digits, oflag);
				if (i < 0 || i > MINUTE)
					return (NULL);
				if (ISSET(tm_mask, f_min) && tm->tm_min != i) {
					wrong_input++;
					return (NULL);
				}
				tm->tm_min = i;
				SET(tm_mask, f_min);
				break;

			case 'n':		/* new line character */
				while (*buf && isspace(*buf))
					buf++;	/* skip all white space */
				break;

			case 'p':		/* locale's AM or PM */
			case 'P':
				SKIP_TO_NWHITE(buf);
				/* Accept am_pm[] composed of whitespace */
				if ((width = compare_str(buf,
				    (const unsigned char *)
				    hdl->am_pm[0])) >= 0) {
					i = AM;
					buf += width;
				} else if ((lwidth = compare_str(buf,
				    (const unsigned char *)
				    hdl->am_pm[1])) >= 0) {
					i = PM;
					buf += lwidth;
				} else {
					return (NULL);
				}

				if (meridian && meridian != i) {
					wrong_input++;
					return (NULL);
				}
				meridian = i;

				break;

			case 'R': 		/* %H:%M */
				SKIP_TO_NWHITE(buf);
				buf = strptime_recurse(hdl, buf,
				    (const unsigned char *)"%H:%M", tm,
				    ct, strptime_data, 1);
				if (buf == NULL)
					return (NULL);
				break;

			case 'r':		/* locale's am/pm time format */
				SKIP_TO_NWHITE(buf);
				if ((buf = strptime_recurse(hdl, buf,
				    (const unsigned char *)hdl->t_fmt_ampm, tm,
				    ct, strptime_data, 1)) == NULL)
					return (NULL);
				break;

			case 'S':		/* second 0-61 */
				SKIP_TO_NWHITE(buf);
				i = get_number(&buf, 2, hdl->alt_digits, oflag);
				if (__xpg6 & _C99SUSv3_strptime_seconds) {
					/* XPG6 range [0-60] */
					secs = SECOND_XPG6;
				} else {
					/* XPG5 range [0-61] */
					secs = SECOND_XPG5;
				}
				if (i < 0 || i > secs)
					return (NULL);
				if (ISSET(tm_mask, f_sec) && tm->tm_sec != i) {
					wrong_input++;
					return (NULL);
				}
				tm->tm_sec = i;
				SET(tm_mask, f_sec);
				break;

			case 't':		/* tab character */
				while (*buf && isspace(*buf))
					buf++;	/* skip all white prior to \n */
				break;

			case 'T':		/* %H:%M:%S */
				SKIP_TO_NWHITE(buf);
				if ((buf = strptime_recurse(hdl, buf,
				    (const unsigned char *)"%H:%M:%S", tm, ct,
				    strptime_data, 1)) == NULL)
					return (NULL);
				break;

			case 'u':		/* weekday, 1-7, 1 == Monday */
				SKIP_TO_NWHITE(buf);
				i = get_number(&buf, 1, hdl->alt_digits, oflag);
				if (i < 1 || i > 7)
					return (NULL);
				if (i == 7)
					i = 0;
				if (ISSET(tm_mask, f_wday) &&
				    tm->tm_wday != i) {
					wrong_input++;
					return (NULL);
				}
				tm->tm_wday = i;
				SET(tm_mask, f_wday);
				break;

			case 'U':		/* week of year, 0-53, Sunday */
				SKIP_TO_NWHITE(buf);
				i = get_number(&buf, 2, hdl->alt_digits, oflag);
				if (i < 0 || i > WEEK_YR)
					return (NULL);
				if (week_number_u != -1 && week_number_u != i) {
					wrong_input++;
					return (NULL);
				}
				week_number_u = i;
				break;

			case 'V':		/* ISO8601 week of year, 1-53 */
				SKIP_TO_NWHITE(buf);
				i = get_number(&buf, 2, hdl->alt_digits, oflag);
				if (i < 1 || i > WEEK_YR)
					return (NULL);
				if (week_number_v != -1 && week_number_v != i) {
					wrong_input++;
					return (NULL);
				}
				week_number_v = i;
				break;

			case 'W':		/* week of year, 0-53, Monday */
				SKIP_TO_NWHITE(buf);
				i = get_number(&buf, 2, hdl->alt_digits, oflag);
				if (i < 0 || i > WEEK_YR)
					return (NULL);
				if (week_number_w != -1 && week_number_w != i) {
					wrong_input++;
					return (NULL);
				}
				week_number_w = i;
				break;

			case 'w':		/* weekday, 0-6, 0 == Sunday */
				SKIP_TO_NWHITE(buf);
				i = get_number(&buf, 1, hdl->alt_digits, oflag);
				if (i < 0 || i > 6)
					return (NULL);
				if (ISSET(tm_mask, f_wday) &&
				    tm->tm_wday != i) {
					wrong_input++;
					return (NULL);
				}
				tm->tm_wday = i;
				SET(tm_mask, f_wday);
				break;

			case 'x':		/* locale's date format */
				SKIP_TO_NWHITE(buf);
				if ((buf = strptime_recurse(hdl, buf,
				    (const unsigned char *)hdl->d_fmt,
				    tm, ct, strptime_data, 1)) == NULL)
					return (NULL);
				break;

			case 'X':		/* locale's time format */
				SKIP_TO_NWHITE(buf);
				if ((buf = strptime_recurse(hdl, buf,
				    (const unsigned char *)hdl->t_fmt,
				    tm, ct, strptime_data, 1)) == NULL)
					return (NULL);
				break;

			case 'C':		/* century number */
				SKIP_TO_NWHITE(buf);
				i = get_number(&buf, 2, hdl->alt_digits, oflag);
				if (i < 0 || i > 99)
					return (NULL);
				if (century != -1 && century != i) {
					wrong_input++;
					return (NULL);
				}
				century = i;
				break;

			case 'y':		/* year of century, 0-99 */
				SKIP_TO_NWHITE(buf);
				i = get_number(&buf, 2, hdl->alt_digits, oflag);
				if (ISSET(tm_mask, f_year) &&
				    (tm->tm_year != i)) {
					wrong_input++;
					return (NULL);
				}
				tm->tm_year = i;
				SET(tm_mask, f_year);
				break;

			case 'Y':		/* year with century, dddd */
				SKIP_TO_NWHITE(buf);
				i = get_number(&buf, 4, hdl->alt_digits, oflag);
				if ((century != -1) && (century != i / 100)) {
					wrong_input++;
					return (NULL);
				}
				century = i / 100;
				i = i - (century * 100);
				if (ISSET(tm_mask, f_year) &&
				    (tm->tm_year != i)) {
					wrong_input++;
					return (NULL);
				}
				tm->tm_year = i;
				SET(tm_mask, f_year);
				break;

			case 'z':		/* offset in +hhmm or -hhmm */
				/*
				 * There is nothing to extract from
				 * the offset value that can be used for
				 * struct tm data fields.
				 *
				 * We simply check the syntax and move to
				 * the next item.
				 *
				 * Since I'm just re-using the existing
				 * variables, read the i and the secs variables
				 * at below as hour offset and minute offset,
				 * respectively.
				 */
				SKIP_TO_NWHITE(buf);
				if (*buf == '+' || *buf == '-') {
					buf++;
					i = get_number(&buf, 2, NULL, 0);
					if (i < 0 || i > 12)
						return (NULL);
					secs = get_number(&buf, 2, NULL, 0);
					if ((i == 12 && secs > 0) ||
					    (secs < 0 || secs > 59))
						return (NULL);
				} else {
					return (NULL);
				}
				break;

			case 'Z':		/* time zone name */
				SKIP_TO_NWHITE(buf);
				tzset();
				/*
				 * Check tzname[1] first, because tzname[0]
				 * might be a substring of tzname[1].
				 * (Example:  TZ=MET and "MET" and "MET DST".
				 * This assumes tzname[1] is never a
				 * substring of tzname[0].)
				 *
				 * Note tzname[1] might be composed of only
				 * whitespace (if the timezone does not have
				 * daylight time), so be sure compare_str()
				 * returns a value greater than zero.
				 *
				 * Check if tzname[0] and tzname[1] are the
				 * same (Example: TZ=Australia/West and "WST").
				 * If so, for strptime(), set tm_isdst = -1. For
				 * getdate(), since a later call to calc_date()
				 * calls mktime() to populate the tm struct,
				 * and since we don't know whether we've
				 * matched the std or dst timezone name, also
				 * set tm_isdst = -1, and let mktime() fill in
				 * tm_isdst.  Otherwise, if mktime() is given
				 * an incorrect value in tm_isdst, it can
				 * return an incorrect value in tm_hour.
				 */
				{
					char	*tz0, *tz1;

					lmutex_lock(&_time_lock);
					tz0 = tzname[0];
					tz1 = tzname[1];
					lmutex_unlock(&_time_lock);

					if ((width = compare_str(buf,
					    (const unsigned char *)tz1)) > 0) {
						if (strcmp(tz1, tz0) != 0) {
							tm->tm_isdst = 1;
						} else {
							tm->tm_isdst = -1;
						}
						buf += width;
					} else if ((lwidth = compare_str(buf,
					    (const unsigned char *)tz0)) > 0) {
						tm->tm_isdst = 0;
						buf += lwidth;
					} else {
						return (NULL);
					}
					SET(tm_mask, f_isdst);
				}
				break;

			case '%' :		/* double % character */
				SKIP_TO_NWHITE(buf);
				bufchr = *buf;
				if (bufchr == '%')
					buf++;
				else
					return (NULL);
				break;

			default:
				wrong_input++;
				return (NULL);
			} /* switch */
		} /* else */
		SKIP_TO_NWHITE(fmt);
	} /* while */
	if (fmtchr)
		return (NULL); 		/* buf string ends before fmt string */
	if (flag)
		return ((unsigned char *)buf);
	if (calling_func == f_getdate) {
		while (isspace(*buf))
			buf++;
		if (*buf)
			return (NULL);
		if (verify_getdate(tm, ct, strptime_data))
			return ((unsigned char *)1);  /* success completion */
		else
			return (NULL);
	} else {	/* calling_function == f_strptime */
		if (verify_strptime(tm, ct, strptime_data))
			return ((unsigned char *)buf);	/* success completion */
		else
			return (NULL);
	}
}


/*
 * Read the user specified template file by line
 * until a match occurs.
 * The DATEMSK environment variable points to the template file.
 */

static int
read_tmpl(_LC_time_t *hdl, char *line, struct tm *t, struct tm *ct,
    strptime_data_t *strptime_data)
{
	FILE *fp;
	char *file;
	char *bp;
	char start[512];
	struct stat64 sb;
	unsigned char *ret = NULL;

	if (((file = getenv("DATEMSK")) == 0) || file[0] == '\0') {
		getdate_err = 1;
		return (0);
	}
	if (access(file, R_OK) != 0 || (fp = fopen(file, "rF")) == NULL) {
		getdate_err = 2;
		return (0);
	}
	if (fstat64(fileno(fp), &sb) < 0) {
		getdate_err = 3;
		goto end;
	}
	if ((sb.st_mode & S_IFMT) != S_IFREG) {
		getdate_err = 4;
		goto end;
	}

	for (;;) {
		bp = start;
		if (fgets(bp, 512, fp) == NULL) {
			if (!feof(fp)) {
				getdate_err = 5;
				ret = 0;
				break;
			}
			getdate_err = 7;
			ret = 0;
			break;
		}
		if (*(bp+strlen(bp)-1) != '\n')  { /* terminating newline? */
			getdate_err = 5;
			ret = 0;
			break;
		}
		*(bp + strlen(bp) - 1) = '\0';
#ifdef DEBUG
printf("line number \"%2d\"---> %s\n", linenum, bp);
#endif
		if (strlen(bp)) {  /*  anything left?  */

			/* Initialiize "hidden" global/static var's */
			init_str_data(strptime_data, f_getdate);

			if (ret = strptime_recurse(hdl,
			    (const unsigned char *)line,
			    (const unsigned char *)bp, t, ct, strptime_data, 0))
				break;
		}
	}
end:
	(void) fclose(fp);
	if (ret == NULL)
		return (0);
	else
		return (1);
}


/*
 * return time from time structure
 */
static struct tm *
calc_date(struct tm *ct, strptime_data_t *strptime_data)
{
	time_t	tv;
	struct	tm nct;
	struct	tm saved_ct;
	int	norm_hour;

	nct = *ct;
	saved_ct = *ct;
	tv = mktime(ct);
	if ((ISSET(tm_mask, f_isdst) == 0) && ct->tm_isdst != nct.tm_isdst) {
		nct.tm_isdst = ct->tm_isdst;
		tv = mktime(&nct);
	}
	ct = localtime_r(&tv, ct);

	/*
	 * If mktime() normalized tm_hour for a reason other than
	 * for a.) normalizing tm_hour > 24 (verify_getdate() adds 24
	 * hours if no date is given and the input time is < the current time,
	 * because tomorrow is assumed), or b.) normalizing tm_sec > 59,
	 * then tm_hour must have been normalized by mktime() due to being
	 * an invalid hour in the standard-to-daylight or daylight-to-standard
	 * transition.  First calculate the expected normalized tm_hour
	 * (due to a. or b. above), then compare it to tm_hour returned by
	 * mktime().  If they don't agree, return error.
	 */
	if (saved_ct.tm_sec <= VALID_SECOND) {
		norm_hour = saved_ct.tm_hour;
	} else if ((saved_ct.tm_min + 1) > MINUTE) {
		norm_hour = saved_ct.tm_hour + 1;
	} else {
		norm_hour = saved_ct.tm_hour;
	}
	if (norm_hour > HOUR_24) {
		norm_hour %= 24;
	}
	if (ct->tm_hour != norm_hour) {
		return (NULL);
	}
	return (ct);
}

static void
getnow(struct tm *ct)	/*  get current date */
{
	time_t now;

	now = time((time_t *)NULL);
	ct = localtime_r(&now, ct);
	ct->tm_yday += 1;
}

static void
init_str_data(strptime_data_t *strptime_data, calling_func_t func_type)
{
	/*
	 * Initialiize "hidden" global/static var's
	 * (Note:  wrong_input value is saved.)
	 */
	hour = 0;
	meridian = 0;
	era_base = 0;
	era_offset = 0;
	week_number_u = -1;
	week_number_v = -1;
	week_number_w = -1;
	century = -1;
	week_based_year = -1;
	week_based_year_century = -1;
	calling_func = func_type;
	calling_strptime = 0;
	tm_mask = 0;
}

/*
 * Calculate week-based year.
 */
static int
process_week_based_year(struct tm *t, strptime_data_t *strptime_data)
{
	/*
	 * First consolidate week_based_year and week_based_year_century into
	 * week_based_year which will then have century value in it.
	 */
	if (week_based_year_century != -1) {
		if (week_based_year != -1 &&
		    week_based_year != (week_based_year_century % 100))
			return (0);

		week_based_year = week_based_year_century;
	} else if (week_based_year != -1) {
		if (week_based_year <= 68)
			week_based_year += YEAR_2000;
		else
			week_based_year += YEAR_1900;
	}

	if (week_based_year == -1)
		return (1);

	/*
	 * If consolidated week_based_year is available, do our best and
	 * try to set (or, check on) the tm_year. In doing so, try to use
	 * tm_mon, tm_mday, tm_wday, and/or tm_yday data fields.
	 */
	if (ISSET(tm_mask, f_wday)) {
		/*
		 * If it is January 1, 2, or 3 and weekday is
		 * Friday, Saturday, or Sunday, then, the year is
		 * week_based_year + 1.
		 *
		 * If it is December 29, 30, or 31 and weekday is
		 * Monday, Tuesday, or Wednesday, then, the year is
		 * week_based_year - 1.
		 */
		if (ISSET(tm_mask, f_mon) && ISSET(tm_mask, f_mday)) {
			if (t->tm_mon == JANUARY && t->tm_mday <= 3 &&
			    (t->tm_wday == FRIDAY || t->tm_wday == SATURDAY ||
			    t->tm_wday == SUNDAY)) {
				week_based_year++;
			} else if (t->tm_mon == DECEMBER && t->tm_mday >= 29 &&
			    (t->tm_wday >= MONDAY && t->tm_wday <= WEDNESDAY)) {
				week_based_year--;
			}
		} else if (ISSET(tm_mask, f_yday)) {
			if (t->tm_yday <= 2 && (t->tm_wday == FRIDAY ||
			    t->tm_wday == SATURDAY || t->tm_wday == SUNDAY)) {
				week_based_year++;
			} else if (t->tm_yday >= 363 && (t->tm_wday >= MONDAY &&
			    t->tm_wday <= WEDNESDAY)) {
				week_based_year--;
			} else if (t->tm_yday == 362 && t->tm_wday == MONDAY) {
				if (ISSET(tm_mask, f_mday)) {
					if (t->tm_mday == 29)
						week_based_year--;
				} else if (ISSET(tm_mask, f_year)) {
					/*
					 * If we do not have necessary
					 * info and the tm_year is
					 * already set, we put higher
					 * priority on the current
					 * tm_year over the week based
					 * year value(s) we have now.
					 */
					return (1);
				}
			}
		} else if (ISSET(tm_mask, f_year)) {
			/*
			 * The same here. We value the current tm_year
			 * defined than the value we have from week
			 * based year value(s).
			 */
			return (1);
		}
	} else if (ISSET(tm_mask, f_year)) {
		/*
		 * The same here. We value the current tm_year
		 * defined than the value we have from week
		 * based year value(s).
		 */
		return (1);
	}

	/* Calculate century if necessary. */
	if (century == -1)
		century = week_based_year / 100;
	else if (century != (week_based_year / 100))
		return (0);

	/*
	 * Re-normalize the year into year of century which will
	 * then soon be changed into year offset at verify_*().
	 */
	week_based_year %= 100;

	if (ISSET(tm_mask, f_year) && week_based_year != t->tm_year)
		return (0);

	SET(tm_mask, f_year);
	t->tm_year = week_based_year;

	return (1);
}

/*
 * Check validity of input for strptime
 */
static int
verify_strptime(struct tm *t, struct tm *ct, strptime_data_t *strptime_data)
{
	int leap;

	/* Process week based year values to get year offset and century. */
	if (process_week_based_year(t, strptime_data) <= 0)
		return (0);

	/* Calculate year */
	if (century != -1) {
		if (ISSET(tm_mask, f_year)) {
			/* Century and year offset are both specified. */
			t->tm_year += (100 * century) - YEAR_1900;
		} else {
			/*
			 * Default zeroing strptime() behavior:
			 * Century is specified, but year offset not
			 * specified;  use year offset = 0.
			 */
			if (calling_strptime == f_strptime_dozero) {
				t->tm_year = (100 * century) - YEAR_1900;

			} else {
			/*
			 * Non-zeroing strptime() behavior:
			 * Century is specified, but year offset not
			 * specified;  use year offset = input tm_year offset.
			 * Also handle the case if input tm_year is negative.
			 */
				t->tm_year %= 100;
				if (t->tm_year < 0)
					t->tm_year += 100;
				t->tm_year += (100 * century) - YEAR_1900;
			}
			SET(tm_mask, f_year);
		}
	} else {
		/* If century not specified, 0-68 means year 2000-2068 */
		if (ISSET(tm_mask, f_year) && (t->tm_year <= 68)) {
			t->tm_year += 100;
		}
	}

	/* Calculate era year */
	if (ISSET(tm_mask, f_year_era)) {
		t->tm_year = era_base + era_offset;
	}

	leap = (days(t->tm_year) == 366);

	if (week_number_u != -1 || week_number_v != -1 || week_number_w != -1)
		if (week_number_to_yday(t, t->tm_year, strptime_data) == -1)
			return (0);

	if (ISSET(tm_mask, f_yday))
		if (yday(t, leap, ct, strptime_data) == -1)
			return (0);

	if (ISSET(tm_mask, f_hour) ||
	    ((calling_strptime == f_strptime_dontzero) && (meridian))) {
		switch (meridian) {
		case PM:
			t->tm_hour %= 12;
			t->tm_hour += 12;
			break;
		case AM:
			t->tm_hour %= 12;
			break;
		}
	}
	if (hour)
		t->tm_hour = hour - 1;

	return (1);
}

/*
 * Check validity of input for getdate
 */
static int
verify_getdate(struct tm *t, struct tm *ct, strptime_data_t *strptime_data)
{
	int leap;

	/* Process week based year values to get year offset and century. */
	if (process_week_based_year(t, strptime_data) <= 0)
		return (0);

	/* Calculate year */
	if (century != -1) {
		if (ISSET(tm_mask, f_year)) {
			/* century and year offset are both specified. */
			t->tm_year += (100 * century) - YEAR_1900;
		} else {
			/*
			 * century is specified, but year offset not
			 * specified;  use current year offset
			 */
			t->tm_year = (100 * century) + (ct->tm_year % 100)
			    - YEAR_1900;
			SET(tm_mask, f_year);
		}
	} else {
		/* If century not specified, 0-68 means year 2000-2068 */
		if (ISSET(tm_mask, f_year) && (t->tm_year <= 68)) {
			t->tm_year += 100;
		}
	}

	/* Calculate era year */
	if (ISSET(tm_mask, f_year_era)) {
		t->tm_year = era_base + era_offset;
	}
	if (ISSET(tm_mask, (f_year | f_year_era)))
		year(t->tm_year, ct);

	leap = (days(ct->tm_year) == 366);

	if (week_number_u != -1 || week_number_v != -1 || week_number_w != -1)
		if (week_number_to_yday(t, ct->tm_year, strptime_data) == -1) {
			wrong_input++;
			return (0);
		}
	if (ISSET(tm_mask, f_yday))
		if (yday(t, leap, ct, strptime_data) == -1) {
			wrong_input++;
			return (0);
		} else {
			t->tm_yday = 0;
		}
	if (ISSET(tm_mask, f_mon))
		MON(t->tm_mon, ct);
	if (ISSET(tm_mask, f_mday))
		Day(t->tm_mday, ct);
	if (ISSET(tm_mask, f_wday))
		DOW(t->tm_wday, ct);

#ifdef	_LP64
	/*
	 * In _LP64, if year is set, bounds check between 0001-9999.
	 * Because %Y takes 4 digits number that can be a year
	 * from 0001 to 9999.  Also %C takes 2 digits number
	 * from 00 to 99.
	 */
	if ((ct->tm_year < -1899) || (ct->tm_year > 8099)) {
		wrong_input++;
		return (0);
	}
#else  /* _LP64 */
	/*
	 * The last time 32-bit UNIX can handle is 1/19/2038 03:14:07 GMT;
	 * the earliest time is 12/13/1901 20:45:52 GMT;
	 * for simplicity start at 1902, and stop at 1/17/2038.
	 */
	if (ct->tm_year < 2 || ct->tm_year > 138 ||
	    (ct->tm_year == 138 && (ct->tm_mon > 0 ||
	    (ct->tm_mon == 0 && ct->tm_mday > 17)))) {
		wrong_input++;
		return (0);
	}
#endif /* _LP64 */

	if ((ISSET(tm_mask, f_mday) && ((t->tm_mday != ct->tm_mday) ||
	    (t->tm_mday > __mon_lengths[leap][ct->tm_mon]))) ||
	    (ISSET(tm_mask, f_wday) && ((t->tm_wday) != ct->tm_wday)) ||
	    ((ISSET(tm_mask, f_hour) && hour) ||
	    (ISSET(tm_mask, f_hour) && !meridian) ||
	    (!ISSET(tm_mask, f_hour) && meridian) || (hour && meridian))) {
		wrong_input++;
		return (0);
	}

	if (ISSET(tm_mask, f_hour)) {
		switch (meridian) {
		case PM:
			t->tm_hour %= 12;
			t->tm_hour += 12;
			break;
		case AM:
			t->tm_hour %= 12;
			break;
		default:
			return (0);
		}
	}
	if (hour)
		t->tm_hour = hour - 1;

	if (((ISSET(tm_mask, (f_year | f_year_era)) == 0) &&
	    (ISSET(tm_mask, f_mon) == 0) &&
	    (ISSET(tm_mask, f_mday) == 0) &&
	    (ISSET(tm_mask, f_wday) == 0)) &&
	    ((ISSET(tm_mask, (f_hour | f_min | f_sec)) || hour) &&
	    ((t->tm_hour < ct->tm_hour) || ((t->tm_hour == ct->tm_hour) &&
	    (t->tm_min < ct->tm_min)) || ((t->tm_hour == ct->tm_hour) &&
	    (t->tm_min == ct->tm_min) && (t->tm_sec < ct->tm_sec)))))
		t->tm_hour += 24;

	if (ISSET(tm_mask, (f_hour | f_min | f_sec)) || hour) {
		ct->tm_hour = t->tm_hour;
		ct->tm_min = t->tm_min;
		ct->tm_sec = t->tm_sec;
	}

	if (ISSET(tm_mask, f_isdst))
		ct->tm_isdst = t->tm_isdst;
	else
		ct->tm_isdst = 0;
	return (1);
}


/*
 *  Parses a number.  If oflag is set, and alternate digits
 *  are defined for the locale, call the routine to
 *  parse alternate digits.  Otherwise, parse ASCII digits.
 */
static int
get_number(const unsigned char **buf, int length,
    const char *alt_digits, int oflag)
{
	int ret;

	if ((oflag == 0) || (alt_digits == NULL) ||
	    ((alt_digits != NULL) && (*alt_digits == '\0'))) {
		ret = ascii_number(buf, length);
	} else {
		ret = search_alt_digits(buf, alt_digits);
	}
	return (ret);
}

/*
 * Parse the number given by the specification.
 * Allow at most length digits.
 */
static int
ascii_number(const unsigned char **input, int length)
{
	int	val;
	unsigned char c;

	val = 0;
	if (!isdigit(**input))
		return (-1);
	while (length--) {
		if (!isdigit(c = **input))
			return (val);
		val = 10*val + c - '0';
		(*input)++;
	}
	return (val);
}

/*
 * Compare input against list of alternate digits.
 */
static int
search_alt_digits(const unsigned char **buf, const char *alt_digits)
{
	int num, c;
	int length, prev_length, length2;
	char *digs, *cand, *tmp;

	digs = strdupa(alt_digits);
	cand = strtok_r(digs, ";", &tmp);
	prev_length = 0;
	num = -1;
	c = 0;
	while (cand != NULL) {
		/*
		 * Find the alternate digit (cand) with longest length that
		 * matches the input.  (Example: in the ja locale, the alternate
		 * digit for 10 is <jyu>, while 11 is <jyu><ichi>.  If we stop
		 * at the first match (when input is <jyu><ichi>), 10 will
		 * incorrectly be returned;  we have to continue searching
		 * for an alternate digit with the longest length.)
		 * If the length of the current cand is smaller than the length
		 * of a previously matched cand, we don't have to check the
		 * current cand.
		 */
		length2 = (tmp != NULL) ? tmp - cand - 1 : strlen(cand);
		if (length2 >= prev_length) {
			/*
			 * cand can be a candidate for alt_digit
			 */
			length = compare_str(*buf, (const unsigned char *)cand);
			if (length > prev_length) {
				/*
				 * Matching alt_digit found.
				 *
				 * This alt_digit is longer than the
				 * previous matched alt_digit.
				 * Taking this one.
				 */
				prev_length = length;
				num = c;
			}
		}
		c++;
		cand = strtok_r(NULL, ";", &tmp);
	}

	if (num == -1)
		return (-1);

	*buf += prev_length;
	return (num);
}

static void
Day(int day, struct tm *ct)
{
	if (day < ct->tm_mday)
		if (++ct->tm_mon == 12)
			++ct->tm_year;
	ct->tm_mday = day;
	DMY(ct);
}

static void
DMY(struct tm *ct)
{
	int doy;
	if (days(ct->tm_year) == 366)
		doy = __lyday_to_month[ct->tm_mon];
	else
		doy = __yday_to_month[ct->tm_mon];
	ct->tm_yday = doy + ct->tm_mday;
	ct->tm_wday = (jan1(ct->tm_year) + ct->tm_yday - 1) % 7;
}

static int
days(int y)
{
	y += 1900;
	return (y % 4 == 0 && y % 100 != 0 || y % 400 == 0 ? 366 : 365);
}


/*
 *	return day of the week
 *	of jan 1 of given year
 */
static int
jan1(int yr)
{
	int y, d;

/*
 *	normal gregorian calendar
 *	one extra day per four years
 */

	y = yr + 1900;
	d = 4+y+(y+3)/4;

/*
 *	julian calendar
 *	regular gregorian
 *	less three days per 400
 */

	if (y > 1800) {
		d -= (y-1701)/100;
		d += (y-1601)/400;
	}

/*
 *	great calendar changeover instant
 */

	if (y > 1752)
		d += 3;

	return (d%7);
}

static void
year(int yr, struct tm *ct)
{
	ct->tm_mon = 0;
	ct->tm_mday = 1;
	ct->tm_year = yr;
	DMY(ct);
}

static void
MON(int month, struct tm *ct)
{
	ct->tm_mday = 1;
	Month(month, ct);
}

static void
Month(int month, struct tm *ct)
{
	if (month < ct->tm_mon)
		ct->tm_year++;
	ct->tm_mon = month;
	DMY(ct);
}

static void
DOW(int dow, struct tm *ct)
{
	adddays((dow+7-ct->tm_wday)%7, ct);
}

static void
adddays(int n, struct tm *ct)
{
	DOY(ct->tm_yday+n, ct);
}

static void
DOY(int doy, struct tm *ct)
{
	int i, leap;

	if (doy > days(ct->tm_year)) {
		doy -= days(ct->tm_year);
		ct->tm_year++;
	}
	ct->tm_yday = doy;

	leap = (days(ct->tm_year) == 366);
	for (i = 0; doy > __mon_lengths[leap][i]; i++)
		doy -= __mon_lengths[leap][i];
	ct->tm_mday = doy;
	ct->tm_mon = i;
	ct->tm_wday = (jan1(ct->tm_year)+ct->tm_yday-1) % 7;
}

/* ARGSUSED */
static int
yday(struct tm *t, int leap, struct tm *ct, strptime_data_t *strptime_data)
{
	int	month;
	int	day_of_month;
	int	*days_to_months;

	days_to_months = (int *)(leap ? __lyday_to_month : __yday_to_month);

	/*
	 * If tm_year is not set, then set it to current year,
	 * for getdate() or for default (zeroing) strptime().
	 */
	if ((ISSET(tm_mask, (f_year | f_year_era)) == 0) &&
	    (calling_strptime != f_strptime_dontzero)) {
		t->tm_year = ct->tm_year;
		year(t->tm_year, ct);
	}

	for (month = 1; month < 12; month++)
		if (t->tm_yday < days_to_months[month])
			break;
	month--;

	if (ISSET(tm_mask, f_mon) && t->tm_mon != month)
		return (-1);

	t->tm_mon = month;
	SET(tm_mask, f_mon);
	day_of_month = t->tm_yday - days_to_months[month] + 1;
	if (ISSET(tm_mask, f_mday) && t->tm_mday != day_of_month)
		return (-1);

	t->tm_mday = day_of_month;
	SET(tm_mask, f_mday);
	return (0);
}

static int
week_number_to_yday(struct tm *t, int year, strptime_data_t *strptime_data)
{
	int	yday;
	int	jday;

	/* For strptime_dontzero(), correct range of input tm struct tm_wday */
	if ((calling_strptime == f_strptime_dontzero) &&
	    !ISSET(tm_mask, f_wday)) {
		if ((t->tm_wday < 0) || (t->tm_wday > 6)) {
			t->tm_wday %= 7;
			if (t->tm_wday < 0)
				t->tm_wday += 7;
		}
		SET(tm_mask, f_wday);
	}
	if (week_number_u != -1) {
		yday = 7 * week_number_u + t->tm_wday - (jday = jan1(year));
		/* Jan. 1 is a Sunday */
		if (jday == 0)
			yday -= 7;
		if (ISSET(tm_mask, f_yday) && t->tm_yday != yday)
			return (-1);
		t->tm_yday = yday;
		SET(tm_mask, f_yday);
	}
	if (week_number_v != -1) {
		/*
		 * First calculate what'd be the week day of Jan. 4'th and
		 * then add 4 to have the offset.
		 */
		jday = (jan1(year) + 3) % 7 + 4;

		/*
		 * Day of year would be 7 x week number + day of week - offset.
		 */
		yday = 7 * week_number_v + t->tm_wday - jday;
		if (t->tm_wday == SUNDAY)
			yday += 7;

		if (ISSET(tm_mask, f_yday) && t->tm_yday != yday)
			return (-1);

		t->tm_yday = yday;
		SET(tm_mask, f_yday);
	}
	if (week_number_w != -1) {
		yday = (8 - (jday = jan1(year)) % 7) + 7 * (week_number_w - 1) +
		    t->tm_wday - 1;
		if (t->tm_wday == 0)
			yday += 7;
		/* Jan. 1 is a Sunday or Monday */
		if (jday < 2)
			yday -= 7;
		if (ISSET(tm_mask, f_yday) && t->tm_yday != yday)
			return (-1);
		t->tm_yday = yday;
		SET(tm_mask, f_yday);
	}
	return (0);
}


/*
 * FUNCTION:  compare_str()
 *
 * PARAMETERS:
 *	const unsigned char *s1 = pointer to the input buffer;
 *				s1 is assumed to begin at
 *				non-whitespace.
 *	unsigned char *s2 = pointer to the string to match;
 *				s2 may begin or end with whitespace.
 * RETURN VALUE DESCRIPTIONS:
 *	Returns:
 *	   > 0  - number of bytes of s1 matched s2
 *           0  - 0 bytes of s1 matched s2; s2 was composed
 *			only of whitespace
 *	    -1  - a mismatch was detected
 */
static int
compare_str(const unsigned char *s1, const unsigned char *s2)
{
	wchar_t wchar1, wchar2;
	int len1, len2;
	int	mbmax;
	const unsigned char *s1_orig;

	mbmax = MB_CUR_MAX;

	SKIP_TO_NWHITE(s2);
	s1_orig = s1;

	while ((*s2 != '\0') && (*s1 != '\0')) {
		if (isascii(*s2) ||
		    ((len1 = mbtowc(&wchar1, (const char *)s1, mbmax)) == -1) ||
		    ((len2 = mbtowc(&wchar2, (const char *)s2, mbmax)) == -1)) {
			/*
			 * ascii character - use single-byte tolower()
			 * routine.  If illegal multibyte character is
			 * encountered in s1 or s2, default to use
			 * single-byte tolower() routine.
			 */
			if (tolower(*s1) == tolower(*s2)) {
				s1++;
				s2++;
			} else {
				/* mismatch detected  */
				break;
			}
		} else {
			/*
			 * multi-byte character - use wide-character
			 * towlower() function
			 */
			if (towlower((wint_t)wchar1) ==
			    towlower((wint_t)wchar2)) {
				s1 += len1;
				s2 += len2;
			} else {
				/* mismatch detected  */
				break;
			}
		}
	}
	SKIP_TO_NWHITE(s2);
	if (*s2 == '\0') {
		return ((int)(s1 - s1_orig));
	} else {
		return (-1);
	}
}

/*
 * This is a wrapper for the real function which is recursive.
 * getdate and strptime share the parsing function, strptime_recurse.
 * The global data is encapsulated in a structure which is initialised
 * here and then passed by reference.
 */
struct  tm *
__getdate_std(_LC_time_t *hdl, const char *expression)
{
	struct tm t;
	struct tm *res;
	strptime_data_t real_strptime_data;
	strptime_data_t *strptime_data = &real_strptime_data;
	struct tm *ct;

	ct = tsdalloc(_T_GETDATE, sizeof (struct tm), NULL);
	if (ct == NULL) {
		getdate_err = 6;
		return (NULL);
	}
	/* Initialiize "hidden" global/static var's */
	init_str_data(strptime_data, f_getdate);
	(void) memset(&t, 0, sizeof (struct tm));
	wrong_input = 0;	/* only initialize this once */

	if (read_tmpl(hdl, (char *)expression, &t, ct, strptime_data)) {
		if ((res = calc_date(ct, strptime_data)) == NULL) {
			getdate_err = 8;
		}
		return (res);
	} else {
		if (wrong_input)
			getdate_err = 8;
		return (NULL);
	}
}


/*
 * This is a wrapper for the real function which is recursive.
 * The global data is encapsulated in a structure which is initialised
 * here and then passed by reference.
 */
char *
__strptime_std(_LC_time_t *hdl, const char *buf, const char *fmt, struct tm *tm)
{
	strptime_data_t real_strptime_data;
	strptime_data_t *strptime_data = &real_strptime_data;
	struct tm	ct;

	/* Initialiize "hidden" global/static var's */
	init_str_data(strptime_data, f_strptime);
	wrong_input = 0;

	/* Call the recursive, thread-safe routine */
	return ((char *)strptime_recurse(hdl, (const unsigned char *)buf,
	    (const unsigned char *)fmt, tm, &ct, strptime_data, 0));
}
