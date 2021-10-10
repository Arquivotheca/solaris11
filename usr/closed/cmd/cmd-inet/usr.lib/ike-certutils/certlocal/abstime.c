/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Copyright (c) 1983, 1995-1997 Eric P. Allman
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <strings.h>
#include <libintl.h>
#include <locale.h>
#include <ctype.h>
#include <math.h>

#undef snprintf

/*
 * The following function has been partially taken from the
 * sendmail 8.8.8 distribution and modified to
 * accept negative and positive values and return
 * the absolute number of seconds since the epoch.  It
 * also bails out on parse errors.  It makes use of
 * libc functions to do some of the calculations.
 */

/*
 *  ABSTIME -- convert relative time to absolute time
 *
 *	Takes a time as an ascii string with a trailing character
 *	giving units and a sign indicating past or future:
 *	  s -- seconds
 *	  m -- minutes
 *	  h -- hours
 *	  d -- days
 *	  w -- weeks
 *	  M -- months
 *	  y -- years
 *	For example, "+3d12h" is three and a half days from now,
 *	and "-3y2M" is three years and 2 months ago.
 *
 *	Parameters:
 *		p -- pointer to ascii time.
 *		inputtime -- pointer to time_t structure
 *
 *	Sets:
 *		inputtime to the absolute number of seconds since
 *		the epoch, with positive or negative offset applied
 *
 *	Returns:
 *		Boolean True or False
 *
 *	Side Effects:
 *		none.
 */

boolean_t
abstime(char *p, time_t *inputtime)
{
	struct tm *caltime;
	time_t currenttime;
	long t, sec = 0;
	int month, year;
	int sign = 0;
	char c;

	/*
	 * Note that the underlying library deals with overflows,
	 * as long as the values fit into the provided structure.
	 * We can special case month and year and do simple math
	 * for other parts.
	 */

	/*
	 * Put the current time into an appropriate
	 * tm structure.
	 */
	(void) time(&currenttime);
	caltime = gmtime(&currenttime);

	/*
	 * Allow very big value for input and do bounds
	 * checking before dealing with the calendar structure
	 */
	month = caltime->tm_mon;
	year = caltime->tm_year;

	/*
	 * Everything that is deterministic can be added up
	 * in absolute seconds.  Then the months and years
	 * can be done with calendar time and leap year calculations
	 * and such will be calculated using libc functions.
	 */
	if ((c = *p) == '-') {
		sign = 1;
	} else if (c != '+') {
		(void) fprintf(stderr, "%s\n", gettext("Format error:"
		    " relative time must start with + or -"));
		return (B_FALSE);
	}
	p++;
	while (*p != '\0') {
		t = 0;
		while ((c = *p++) != '\0' && isascii(c) && isdigit(c))
			t = t * 10 + (c - '0');
		if (sign == 1)
			t = -t;
		if (c == '\0') {
			c = 's';
			p--;
		} else if (strchr("yMwdhms", c) == NULL) {
			(void) fprintf(stderr, "%s `%c'\n",
			    gettext("Invalid time unit"), c);
			return (B_FALSE);
		}
		switch (c) {
			case 'w':		/* weeks */
				t *= 7;
				/* FALLTHRU */
			case 'd':		/* days */
				t *= 24;
				/* FALLTHRU */
			case 'h':		/* hours */
				t *= 60;
				/* FALLTHRU */
			case 'm':		/* minutes */
				t *= 60;
				/* FALLTHRU */
			case 's':		/* seconds */
				sec += t;
				break;
			case 'M':		/* Months */
				month += t;
				year += month / 12;
				month = month % 12;
				if (month < 0) {
					year--;
					month += 12;
				}
				break;
			case 'y':		/* years */
				year += t;
				if (year < 0 || year > pow(2, 16)) {
					fprintf(stderr,
					    gettext("Units out of range.\n"));
					return (B_FALSE);
				}
				break;
		}
	}
	/*
	 * We're returning seconds since the epoch here, so...
	 * First repopulate the calendar structure to account
	 * for months and years params.  We're making the assumption
	 * that adding a year or month means the same day
	 * next year or month.  This method seems to be the most
	 * intuitive.  For instance, if it is Jan 26, 2005 and I want
	 * my cert to expire 3 years and 1 month from today, I expect
	 * that date to be Feb 26, 2008.  If there is an overflow to a
	 * non-existent date, like Feb 31, the library will account
	 * for that.  We then add that value to the number of seconds
	 * we calculated in the deterministic part (if any) and we
	 * should have it.
	 */

	caltime->tm_mon = month;
	caltime->tm_year = year;

	errno = 0;
	*inputtime = mktime(caltime);
	if (*inputtime < 0 || errno != 0)
		return (B_FALSE);
	*inputtime += sec;
	return (B_TRUE);
}
