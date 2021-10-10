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
 * Copyright (c) 1986, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*
 * University Copyright- Copyright (c) 1982, 1986, 1988
 * The Regents of the University of California
 * All Rights Reserved
 *
 * University Acknowledgment- Portions of this document are derived from
 * software developed by the University of California, Berkeley, and its
 * contributors.
 */

/*
 *	date - with format capabilities and international flair
 */

#include	<locale.h>
#include	<fcntl.h>
#include	<langinfo.h>
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<time.h>
#include	<unistd.h>
#include	<sys/time.h>
#include	<sys/types.h>
#include	<ctype.h>
#include	<utmpx.h>
#include	<tzfile.h>


/*
 * The size of newfmt[] buffer that we use to compare the upper limit.
 */
#define	NEWFMT_BUFSIZ	(BUFSIZ - 1)

/*
 * The default length of nanoseconds in the range of [000000000-999999999] for
 * %N.
 */
#define	NS_DEFAULT_LEN	(9)

/*
 * The optional flag characters for pad characters supported in %N.
 */
#define	PAD_REMOVE	('-')
#define	PAD_ZERO	('0')
#define	PAD_SPACE	(' ')

#define	year_size(A)	((isleap(A)) ? 366 : 365)

/*
 * The buf[] will contain the formatted output from stftime(3C).
 * The newfmt[] is to save the new format string with all %N occurrences in
 * fmt string converted to the nanoseconds.
 */
static 	char	buf[BUFSIZ];
static	char	newfmt[BUFSIZ];

/*
 * The amount of time in seconds and nanoseconds from the Epoch.
 * The clock_val is retained for a possible better readability.
 */
static	timespec_t ts;
static	time_t	clock_val;

/*
 * The macros at below are convenience macros to append pad characters and
 * nanoseconds into the newfmt[]; they append as many pad characters and
 * digits as possible.
 */
#define	APPEND_PADCHARS(sz, pc) \
			{ \
				int k; \
				k = (sz); \
				if (len + k > NEWFMT_BUFSIZ) { \
					k = NEWFMT_BUFSIZ - len; \
					if (k <= 0) \
						goto PROCESS_STRFTIME; \
				} \
				(void) memset((void *)(newfmt + len), \
				    (int)(pc), (size_t)k); \
				len += k; \
			}

#define	APPEND_NANOSECONDS(sz, ns) \
			{ \
				int k; \
				k = (sz); \
				if (len + k > NEWFMT_BUFSIZ) { \
					k = NEWFMT_BUFSIZ - len; \
					if (k <= 0) \
						goto PROCESS_STRFTIME; \
				} \
				(void) memcpy((void *)(newfmt + len), \
				    (const void *)(ns), (size_t)k); \
				len += k; \
			}

static  short	month_size[12] =
	{ 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
static  struct  utmpx wtmpx[2] = {
	{"", "", OTIME_MSG, 0, OLD_TIME, 0, 0, 0},
	{"", "", NTIME_MSG, 0, NEW_TIME, 0, 0, 0}
	};
static char *usage =
	"usage:\tdate [-u] mmddHHMM[[cc]yy][.SS]\n\tdate [-u] [+format]\n"
	"\tdate -a [-]sss[.fff]\n";
static int uflag = 0;

static int get_adj(char *, struct timeval *);
static int setdate(struct tm *, char *);
static void process_N_conv_spec(char *, char *);


int
main(int argc, char **argv)
{
	struct tm *tp, tm;
	struct timeval tv;
	char *fmt;
	int c, aflag = 0, illflag = 0;

	(void) setlocale(LC_ALL, "");

#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	while ((c = getopt(argc, argv, "a:u")) != EOF)
		switch (c) {
		case 'a':
			aflag++;
			if (get_adj(optarg, &tv) < 0) {
				(void) fprintf(stderr,
				    gettext("date: invalid argument -- %s\n"),
				    optarg);
				illflag++;
			}
			break;
		case 'u':
			uflag++;
			break;
		default:
			illflag++;
		}

	argc -= optind;
	argv  = &argv[optind];

	/* -u and -a are mutually exclusive */
	if (uflag && aflag)
		illflag++;

	if (illflag) {
		(void) fprintf(stderr, gettext(usage));
		exit(1);
	}

	/*
	 * Get the high resolution time including nanoseconds since the Epoch.
	 * We do not need to check on whether the function has failed or not.
	 *
	 * We use ts.tv_nsec data field, i.e., the nanoseconds, only for
	 * the %N processing. For other purposes, we are keep using clock_val
	 * for a possible better readability.
	 */
	(void) clock_gettime(CLOCK_REALTIME, &ts);
	clock_val = ts.tv_sec;

	if (aflag) {
		if (adjtime(&tv, 0) < 0) {
			perror(gettext("date: Failed to adjust date"));
			exit(1);
		}
		exit(0);
	}

	if (argc > 0) {
		if (*argv[0] == '+')
			fmt = &argv[0][1];
		else {
			if (setdate(localtime(&clock_val), argv[0])) {
				(void) fprintf(stderr, gettext(usage));
				exit(1);
			}
			fmt = nl_langinfo(_DATE_FMT);
		}
	} else
		fmt = nl_langinfo(_DATE_FMT);

	if (uflag) {
		(void) putenv("TZ=GMT0");
		tzset();
		tp = gmtime(&clock_val);
	} else
		tp = localtime(&clock_val);


	/*
	 * Process %N in the fmt string before the strftime() call.
	 *
	 * This is due to that if we call strftime() first, then, we will
	 * not know if %N was originally %%N or %N.
	 */
	process_N_conv_spec(fmt, newfmt);

	/*
	 * As the last step, call strftime() to convert on any other
	 * conversion specifications and then output the result.
	 */
	(void) memcpy(&tm, tp, sizeof (struct tm));
	(void) strftime(buf, BUFSIZ, newfmt, &tm);

	(void) puts(buf);

	return (0);
}

int
setdate(struct tm *current_date, char *date)
{
	int	i;
	int	mm;
	int	hh;
	int	min;
	int	sec = 0;
	char	*secptr;
	int	yy;
	int	dd	= 0;
	int	minidx	= 6;
	int	len;
	int	dd_check;

	/*  Parse date string  */
	if ((secptr = strchr(date, '.')) != NULL && strlen(&secptr[1]) == 2 &&
	    isdigit(secptr[1]) && isdigit(secptr[2]) &&
	    (sec = atoi(&secptr[1])) >= 0 && sec < 60)
		secptr[0] = '\0';	/* eat decimal point only on success */

	len = strlen(date);

	for (i = 0; i < len; i++) {
		if (!isdigit(date[i])) {
			(void) fprintf(stderr,
			gettext("date: bad conversion\n"));
			exit(1);
		}
	}
	switch (strlen(date)) {
	case 12:
		yy = atoi(&date[8]);
		date[8] = '\0';
		break;
	case 10:
		/*
		 * The YY format has the following representation:
		 * 00-68 = 2000 thru 2068
		 * 69-99 = 1969 thru 1999
		 */
		if (atoi(&date[8]) <= 68) {
			yy = 1900 + (atoi(&date[8]) + 100);
		} else {
			yy = 1900 + atoi(&date[8]);
		}
		date[8] = '\0';
		break;
	case 8:
		yy = 1900 + current_date->tm_year;
		break;
	case 4:
		yy = 1900 + current_date->tm_year;
		mm = current_date->tm_mon + 1; 	/* tm_mon goes from 1 to 11 */
		dd = current_date->tm_mday;
		minidx = 2;
		break;
	default:
		(void) fprintf(stderr, gettext("date: bad conversion\n"));
		return (1);
	}

	min = atoi(&date[minidx]);
	date[minidx] = '\0';
	hh = atoi(&date[minidx-2]);
	date[minidx-2] = '\0';

	if (!dd) {
		/*
		 * if dd is 0 (not between 1 and 31), then
		 * read the value supplied by the user.
		 */
		dd = atoi(&date[2]);
		date[2] = '\0';
		mm = atoi(&date[0]);
	}

	if (hh == 24)
		hh = 0, dd++;

	/*  Validate date elements  */
	dd_check = 0;
	if (mm >= 1 && mm <= 12) {
		dd_check = month_size[mm - 1];	/* get days in this month */
		if (mm == 2 && isleap(yy))	/* adjust for leap year */
			dd_check++;
	}
	if (!((mm >= 1 && mm <= 12) && (dd >= 1 && dd <= dd_check) &&
	    (hh >= 0 && hh <= 23) && (min >= 0 && min <= 59))) {
		(void) fprintf(stderr, gettext("date: bad conversion\n"));
		return (1);
	}

	/*  Build date and time number  */
	for (clock_val = 0, i = 1970; i < yy; i++)
		clock_val += year_size(i);
	/*  Adjust for leap year  */
	if (isleap(yy) && mm >= 3)
		clock_val += 1;
	/*  Adjust for different month lengths  */
	while (--mm)
		clock_val += (time_t)month_size[mm - 1];
	/*  Load up the rest  */
	clock_val += (time_t)(dd - 1);
	clock_val *= 24;
	clock_val += (time_t)hh;
	clock_val *= 60;
	clock_val += (time_t)min;
	clock_val *= 60;
	clock_val += sec;

	if (!uflag) {
		/* convert to GMT assuming standard time */
		/* correction is made in localtime(3C) */

		/*
		 * call localtime to set up "timezone" variable applicable
		 * for clock_val time, to support Olson timezones which
		 * can allow timezone rules to change.
		 */
		(void) localtime(&clock_val);

		clock_val += (time_t)timezone;

		/* correct if daylight savings time in effect */

		if (localtime(&clock_val)->tm_isdst)
			clock_val = clock_val - (time_t)(timezone - altzone);
	}

	(void) time(&wtmpx[0].ut_xtime);
	if (stime(&clock_val) < 0) {
		perror("date");
		return (1);
	}
#if defined(i386)
	/* correct the kernel's "gmt_lag" and the PC's RTC */
	(void) system("/usr/sbin/rtc -c > /dev/null 2>&1");
#endif
	(void) time(&wtmpx[1].ut_xtime);
	(void) pututxline(&wtmpx[0]);
	(void) pututxline(&wtmpx[1]);
	(void) updwtmpx(WTMPX_FILE, &wtmpx[0]);
	(void) updwtmpx(WTMPX_FILE, &wtmpx[1]);
	return (0);
}

int
get_adj(char *cp, struct timeval *tp)
{
	register int mult;
	int sign;

	/* arg must be [-]sss[.fff] */

	tp->tv_sec = tp->tv_usec = 0;
	if (*cp == '-') {
		sign = -1;
		cp++;
	} else {
		sign = 1;
	}

	while (*cp >= '0' && *cp <= '9') {
		tp->tv_sec *= 10;
		tp->tv_sec += *cp++ - '0';
	}
	if (*cp == '.') {
		cp++;
		mult = 100000;
		while (*cp >= '0' && *cp <= '9') {
			tp->tv_usec += (*cp++ - '0') * mult;
			mult /= 10;
		}
	}
	/*
	 * if there's anything left in the string,
	 * the input was invalid.
	 */
	if (*cp) {
		return (-1);
	} else {
		tp->tv_sec *= sign;
		tp->tv_usec *= sign;
		return (0);
	}
}


/*
 * The process_N_conv_spec() function scan "fmt" and copy the format string
 * into the "newfmt". In doing so, the function converts each valid %N
 * conversion specification into the nanoseconds string.
 */
void
process_N_conv_spec(char *fmt, char *newfmt)
{
	/*
	 * The nsbuf[] is a temporary character string buffer to
	 * contain nanosecond values for %N. Even with LP64, the LONG_MAX is
	 * 19 digits and thus 24 at nsbuf[] should be sufficient.
	 */
	char nsbuf[24];
	char *saved;
	char *nsp;
	size_t len;
	size_t nslen;
	char pc;
	int not_processed;
	int i;
	int fw;

	saved = fmt;
	len = 0;
	not_processed = 1;

	while ((fmt = strchr(fmt, '%')) != NULL) {
		/*
		 * Store characters prior to the '%' into the newfmt if any.
		 *
		 * If we have too many characters, stop processing and jump to
		 * where null termination is done and then strftime() is called
		 * so that the time formatting will be done with the newfmt
		 * that has up to the last completed characters and/or
		 * conversion specifications.
		 */
		while (saved < fmt) {
			if (len >= NEWFMT_BUFSIZ)
				goto PROCESS_STRFTIME;
			newfmt[len++] = *saved++;
		}

		/*
		 * Save the current address. This is where the next storing
		 * will start if the conversion specification isn't a %N.
		 */
		saved = ++fmt;

		/*
		 * Check if we have an optional flag character or "%%".
		 */
		switch (*fmt) {
		case '%':
			/*
			 * If we have "%%", then, store a '%' and then
			 * move on to the next conversion specification by
			 * doing "continue". (The other '%' will be stored at
			 * the beginning of the next loop.)
			 */
			if (len >= NEWFMT_BUFSIZ)
				goto PROCESS_STRFTIME;
			newfmt[len++] = '%';
			fmt++;
			continue;

		case '-':	/* Do not pad anything. */
			pc = PAD_REMOVE;
			fmt++;
			break;

		case '0':	/* Pad left with zero. */
			pc = PAD_ZERO;
			fmt++;
			break;

		case '_':	/* Pad left with space. */
			pc = PAD_SPACE;
			fmt++;
			break;

		case '#':	/* By default, pad with zero if necessary. */
		case '^':
			fmt++;

			/* FALLTHROUGH */

		default:
			pc = PAD_ZERO;
			break;
		}

		/*
		 * Collect a field width.
		 *
		 * If the field width is explicitly specified as zero,
		 * then, to be compatible with other platforms, set it to
		 * the NS_DEFAULT_LEN. It appears that when the field width
		 * is explicitly specified as zero, then, the padding specified
		 * isn't working in other platforms which appears unreasonable
		 * and thus we do not follow that particular runtime behavior.
		 */
		fw = 0;
		while (isdigit(*fmt)) {
			fw = fw * 10 + *fmt++ - '0';
		}
		if (fw == 0)
			fw = NS_DEFAULT_LEN;

		/*
		 * If it isn't %N, then, move on to the next conversion
		 * specification.
		 */
		if (*fmt != 'N') {
			if (len >= NEWFMT_BUFSIZ)
				goto PROCESS_STRFTIME;
			newfmt[len++] = '%';
			continue;
		}

		/*
		 * Save the current address. This is where the next storing
		 * will start after the processing of this %N.
		 */
		saved = ++fmt;

		/*
		 * Convert the nanoseconds into a string form. We do this
		 * only once.
		 *
		 * We need this so that we know the length and for better
		 * padding and field width processing at below.
		 */
		if (not_processed) {
			nsp = lltostr(ts.tv_nsec, nsbuf + sizeof (nsbuf) - 1);
			nsbuf[sizeof (nsbuf) - 1] = '\0';

			nslen = nsbuf + sizeof (nsbuf) - 1 - nsp;

			not_processed = 0;
		}

		/*
		 * Append the nanoseconds.
		 */
		if (fw >= NS_DEFAULT_LEN) {
			/*
			 * If there was a field width explicitly defined more
			 * than or equal to the NS_DEFAULT_LEN, then, pad as
			 * necessary.
			 *
			 * The PAD_REMOVE means do not append leading zero(s).
			 * even with field width specified.
			 */
			if (pc != PAD_REMOVE && (fw -= nslen) > 0)
				APPEND_PADCHARS(fw, pc);

			APPEND_NANOSECONDS(nslen, nsp);
		} else {
			/*
			 * If the field width defined is less than
			 * the NS_DEFAULT_LEN, then, append only the amount of
			 * digits that is requested with the field width from
			 * the start of the nanosecond string buffer assuming
			 * that the length is NS_DEFAULT_LEN.
			 *
			 * The PAD_REMOVE also means do not append leading
			 * zero(s).
			 */
			i = NS_DEFAULT_LEN - nslen;

			if (i >= fw) {
				/*
				 * There is no significant digit to append.
				 * Append pad character(s) if any and then
				 * '0' to indicate there isn't any significant
				 * digit.
				 */
				if (pc != PAD_REMOVE && --fw > 0)
					APPEND_PADCHARS(fw, pc);

				APPEND_PADCHARS(1, '0');
			} else {
				/*
				 * There is/are siginificant digit(s) to
				 * append. Append pad characters first and
				 * then the siginificant digit(s).
				 */
				if (pc != PAD_REMOVE && i > 0)
					APPEND_PADCHARS(i, pc);

				APPEND_NANOSECONDS(fw - i, nsp);
			}
		}
	}

	/* Append any remaining characters. */
	while (len < NEWFMT_BUFSIZ && *saved != '\0')
		newfmt[len++] = *saved++;

PROCESS_STRFTIME:
	newfmt[len] = '\0';
}
