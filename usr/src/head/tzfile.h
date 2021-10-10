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
 * Copyright (c) 1988, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved	*/

#ifndef _TZFILE_H
#define	_TZFILE_H

/*
 * A part of this file comes from public domain source, so
 * clarified as of June 5, 1996 by Arthur David Olson
 */

#include <sys/types.h>

/*
 * WARNING:
 * The interfaces defined in this header file are for Sun private use only.
 * The contents of this file are subject to change without notice for the
 * future releases.
 */

/* For further information, see ctime(3C) and zic(1M) man pages. */

/*
 * This file is in the public domain, so clarified as of
 * 1996-06-05 by Arthur David Olson.
 */

/*
 * This header is for use ONLY with the time conversion code.
 * There is no guarantee that it will remain unchanged,
 * or that it will remain at all.
 * Do NOT copy it to any system include directory.
 * Thank you!
 */

/* static char	tzfilehid[] = "@(#)tzfile.h	7.18"; */

/*
 * Note: Despite warnings from the authors of this code, Solaris has
 * placed this header file in the system include directory.  This was
 * probably done in order to build both zic and zdump which are in
 * separate source directories, but both use this file.
 */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Information about time zone files.
 */

#ifndef TZDIR
#define	TZDIR	"/usr/share/lib/zoneinfo" /* Time zone object file directory */
#endif /* !defined TZDIR */

#ifndef TZDEFAULT
#define	TZDEFAULT	"localtime"
#endif /* !defined TZDEFAULT */

#ifndef TZDEFRULES
#define	TZDEFRULES	"posixrules"
#endif /* !defined TZDEFRULES */

/*
 * Each file begins with. . .
 */

#define	TZ_MAGIC	"TZif"

struct tzhead {
	char	tzh_magic[4];		/* TZ_MAGIC */
	char	tzh_version[1];		/* '\0' or '2' as of 2005 */
	char	tzh_reserved[15];	/* reserved--must be zero */
	char	tzh_ttisgmtcnt[4];	/* coded number of trans. time flags */
	char	tzh_ttisstdcnt[4];	/* coded number of trans. time flags */
	char	tzh_leapcnt[4];		/* coded number of leap seconds */
	char	tzh_timecnt[4];		/* coded number of transition times */
	char	tzh_typecnt[4];		/* coded number of local time types */
	char	tzh_charcnt[4];		/* coded number of abbr. chars */
};

/*
 * . . .followed by. . .
 *
 *	tzh_timecnt (char [4])s		coded transition times a la time(2)
 *	tzh_timecnt (unsigned char)s	types of local time starting at above
 *	tzh_typecnt repetitions of
 *		one (char [4])		coded UTC offset in seconds
 *		one (unsigned char)	used to set tm_isdst
 *		one (unsigned char)	that's an abbreviation list index
 *	tzh_charcnt (char)s		'\0'-terminated zone abbreviations
 *	tzh_leapcnt repetitions of
 *		one (char [4])		coded leap second transition times
 *		one (char [4])		total correction after above
 *	tzh_ttisstdcnt (char)s		indexed by type; if TRUE, transition
 *					time is standard time, if FALSE,
 *					transition time is wall clock time
 *					if absent, transition times are
 *					assumed to be wall clock time
 *	tzh_ttisgmtcnt (char)s		indexed by type; if TRUE, transition
 *					time is UTC, if FALSE,
 *					transition time is local time
 *					if absent, transition times are
 *					assumed to be local time
 */

/*
 * In the current implementation, "tzset()" refuses to deal with files that
 * exceed any of the limits below.
 */

#ifndef TZ_MAX_TIMES
#define	TZ_MAX_TIMES	1200
#endif /* !defined TZ_MAX_TIMES */

#ifndef TZ_MAX_TYPES
#ifndef NOSOLAR
#define	TZ_MAX_TYPES	256 /* Limited by what (unsigned char)'s can hold */
#endif /* !defined NOSOLAR */
#ifdef NOSOLAR
/*
 * Must be at least 14 for Europe/Riga as of Jan 12 1995,
 * as noted by Earl Chew.
 */
#define	TZ_MAX_TYPES	20	/* Maximum number of local time types */
#endif /* !defined NOSOLAR */
#endif /* !defined TZ_MAX_TYPES */

#ifndef TZ_MAX_CHARS
#define	TZ_MAX_CHARS	50	/* Maximum number of abbreviation characters */
				/* (limited by what unsigned chars can hold) */
#endif /* !defined TZ_MAX_CHARS */

#ifndef TZ_MAX_LEAPS
#define	TZ_MAX_LEAPS	50	/* Maximum number of leap second corrections */
#endif /* !defined TZ_MAX_LEAPS */

#define	SECSPERMIN	60
#define	MINSPERHOUR	60
#define	HOURSPERDAY	24
#define	DAYSPERWEEK	7
#define	DAYSPERNYEAR	365
#define	DAYSPERLYEAR	366
#define	SECSPERHOUR	(SECSPERMIN * MINSPERHOUR)
#define	SECSPERDAY	((time_t)SECSPERHOUR * HOURSPERDAY)
#define	MONSPERYEAR	12

#define	TM_SUNDAY	0
#define	TM_MONDAY	1
#define	TM_TUESDAY	2
#define	TM_WEDNESDAY	3
#define	TM_THURSDAY	4
#define	TM_FRIDAY	5
#define	TM_SATURDAY	6

#define	TM_JANUARY	0
#define	TM_FEBRUARY	1
#define	TM_MARCH	2
#define	TM_APRIL	3
#define	TM_MAY		4
#define	TM_JUNE		5
#define	TM_JULY		6
#define	TM_AUGUST	7
#define	TM_SEPTEMBER	8
#define	TM_OCTOBER	9
#define	TM_NOVEMBER	10
#define	TM_DECEMBER	11

#define	TM_YEAR_BASE	1900

#define	EPOCH_YEAR	1970
#define	EPOCH_WDAY	TM_THURSDAY

#define	isleap(y) (((y) % 4) == 0 && (((y) % 100) != 0 || ((y) % 400) == 0))

#ifndef USG

/*
 * Use of the underscored variants may cause problems if you move your code to
 * certain System-V-based systems; for maximum portability, use the
 * underscore-free variants.  The underscored variants are provided for
 * backward compatibility only; they may disappear from future versions of
 * this file.
 */

#define	SECS_PER_MIN	SECSPERMIN
#define	MINS_PER_HOUR	MINSPERHOUR
#define	HOURS_PER_DAY	HOURSPERDAY
#define	DAYS_PER_WEEK	DAYSPERWEEK
#define	DAYS_PER_NYEAR	DAYSPERNYEAR
#define	DAYS_PER_LYEAR	DAYSPERLYEAR
#define	SECS_PER_HOUR	SECSPERHOUR
#define	SECS_PER_DAY	SECSPERDAY
#define	MONS_PER_YEAR	MONSPERYEAR

#endif /* !defined USG */

/*
 * Since everything in isleap is modulo 400 (or a factor of 400), we know that
 *	isleap(y) == isleap(y % 400)
 * and so
 *	isleap(a + b) == isleap((a + b) % 400)
 * or
 *	isleap(a + b) == isleap(a % 400 + b % 400)
 * This is true even if % means modulo rather than Fortran remainder
 * (which is allowed by C89 but not C99).
 * We use this to avoid addition overflow problems.
 */

#define	isleap_sum(a, b)	isleap((a) % 400 + (b) % 400)

#ifdef	__cplusplus
}
#endif

#endif	/* _TZFILE_H */
