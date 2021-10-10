/*
 * Copyright (c) 1989, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/* based on tzcode2011g */

/*
 * This file is in the public domain, so clarified as of
 * 2009-05-17 by Arthur David Olson.
 */

static char	elsieid[] = "@(#)zdump.c	8.10";

/*
 * This code has been made independent of the rest of the time
 * conversion package to increase confidence in the verification it provides.
 * You can use this code to help in verifying other implementations.
 */

#include "stdio.h"	/* for stdout, stderr, perror */
#include "string.h"	/* for strcpy */
#include "sys/types.h"	/* for timeINT_t */
#include "time.h"	/* for struct tm */
#include "stdlib.h"	/* for exit, malloc, atoi */
#include "locale.h"	/* for setlocale, textdomain */
#include "libintl.h"
#include "float.h"	/* for FLT_MAX and DBL_MAX */
#include "ctype.h"	/* for isalpha et al. */
#include "tzfile.h"	/* for defines */
#include <limits.h>

#ifdef TIME64_TRANS
typedef int64_t	timeINT_t;
#define	timeINT_t_MAX	INT64_MAX
#else
typedef time_t	timeINT_t;
#define	timeINT_t_MAX	LONG_MAX
#endif
#define	timeINT_t_MIN	INT_MIN		/* we stick to 32bit min value */

#define	INT32_LO_YEAR	1901

#ifndef ZDUMP_LO_YEAR
#define	ZDUMP_LO_YEAR	INT32_LO_YEAR
#endif /* !defined ZDUMP_LO_YEAR */

#ifndef ZDUMP_HI_YEAR
#define	ZDUMP_HI_YEAR	2500
#endif /* !defined ZDUMP_HI_YEAR */

#ifndef MAX_STRING_LENGTH
#define	MAX_STRING_LENGTH	1024
#endif /* !defined MAX_STRING_LENGTH */

#ifndef TRUE
#define	TRUE		1
#endif /* !defined TRUE */

#ifndef FALSE
#define	FALSE		0
#endif /* !defined FALSE */

#define	SECSPERNYEAR	(SECSPERDAY * DAYSPERNYEAR)
#define	SECSPERLYEAR	(SECSPERNYEAR + SECSPERDAY)

#ifndef GNUC_or_lint
#ifdef lint
#define	GNUC_or_lint
#else /* !defined lint */
#ifdef __GNUC__
#define	GNUC_or_lint
#endif /* defined __GNUC__ */
#endif /* !defined lint */
#endif /* !defined GNUC_or_lint */

#ifndef INITIALIZE
#ifdef GNUC_or_lint
#define	INITIALIZE(x)	((x) = 0)
#else /* !defined GNUC_or_lint */
#define	INITIALIZE(x)
#endif /* !defined GNUC_or_lint */
#endif /* !defined INITIALIZE */

static timeINT_t	absolute_min_time;
static timeINT_t	absolute_max_time;
static size_t	longest;
static char	*progname;
static int	warned;

static char	*abbr(struct tm	*tmp);
static void	abbrok(const char *abbrp, const char *zone);
static long	delta(struct tm *newp, struct tm *oldp);
static void	dumptime(const struct tm *tmp);
static timeINT_t	hunt(char *name, timeINT_t lot, timeINT_t hit);
static void	setabsolutes(void);
static void	show(char *zone, timeINT_t t, int v);
static const char *tformat(void);
static timeINT_t	yeartot(long y);

#ifdef TIME64_TRANS
extern struct tm *__gmtime64(int64_t *);
extern struct tm *__localtime64(int64_t *);

#define	localtime(x)	__localtime64(x)
#define	gmtime(x)	__gmtime64(x)
#endif

#ifndef TYPECHECK
#define	my_localtime	localtime
#else /* !defined TYPECHECK */
static struct tm *
my_localtime(tp)
timeINT_t	*tp;
{
	register struct tm	*tmp;

	tmp = localtime(tp);
	if (tp != NULL && tmp != NULL) {
		struct tm	tm;
		register timeINT_t	t;

		tm = *tmp;
		t = mktime(&tm);
		if (t - *tp >= 1 || *tp - t >= 1) {
			(void) fflush(stdout);
			(void) fprintf(stderr, "\n%s: ", progname);
			(void) fprintf(stderr, tformat(), *tp);
			(void) fprintf(stderr, " ->");
			(void) fprintf(stderr, " year=%d", tmp->tm_year);
			(void) fprintf(stderr, " mon=%d", tmp->tm_mon);
			(void) fprintf(stderr, " mday=%d", tmp->tm_mday);
			(void) fprintf(stderr, " hour=%d", tmp->tm_hour);
			(void) fprintf(stderr, " min=%d", tmp->tm_min);
			(void) fprintf(stderr, " sec=%d", tmp->tm_sec);
			(void) fprintf(stderr, " isdst=%d", tmp->tm_isdst);
			(void) fprintf(stderr, " -> ");
			(void) fprintf(stderr, tformat(), t);
			(void) fprintf(stderr, "\n");
		}
	}
	return (tmp);
}
#endif /* !defined TYPECHECK */

static void
abbrok(abbrp, zone)
const char * const	abbrp;
const char * const	zone;
{
	register const char *cp;
	int error = 0;

	if (warned)
		return;
	cp = abbrp;
	while (isascii(*cp) && isalpha((unsigned char)*cp))
		++cp;
	(void) fflush(stdout);
	if (cp - abbrp == 0) {
		/*
		 * TRANSLATION_NOTE
		 * The first %s prints for the program name (zdump),
		 * the second %s prints the timezone name, the third
		 * %s prints the timezone abbreviation (tzname[0] or
		 * tzname[1]).
		 */
		(void) fprintf(stderr, gettext("%s: warning: zone \"%s\" "
		    "abbreviation \"%s\" lacks alphabetic at start\n"),
		    progname, zone, abbrp);
		error = 1;
	} else if (cp - abbrp < 3) {
		(void) fprintf(stderr, gettext("%s: warning: zone \"%s\" "
		    "abbreviation \"%s\" has fewer than 3 alphabetics\n"),
		    progname, zone, abbrp);
		error = 1;
	} else if (cp - abbrp > 6) {
		(void) fprintf(stderr, gettext("%s: warning: zone \"%s\" "
		    "abbreviation \"%s\" has more than 6 alphabetics\n"),
		    progname, zone, abbrp);
		error = 1;
	}
	if (error == 0 && (*cp == '+' || *cp == '-')) {
		++cp;
		if (isascii(*cp) && isdigit((unsigned char)*cp))
			if (*cp++ == '1' && *cp >= '0' && *cp <= '4')
				++cp;
		if (*cp != '\0') {
			(void) fprintf(stderr, gettext("%s: warning: "
			    "zone \"%s\" abbreviation \"%s\" differs from "
			    "POSIX standard\n"), progname, zone, abbrp);
			error = 1;
		}
	}
	if (error)
		warned = TRUE;
}

static void
usage(stream, status)
FILE * const	stream;
const int	status;
{
	(void) fprintf(stream, gettext(
	    "%s: [ --version ] [ --help ] [ -v ] "
	    "[ -c [loyear,]hiyear ] zonename ...\n"), progname);
	exit(status);
}

int
main(argc, argv)
int	argc;
char	*argv[];
{
	register int		i;
	register int		c;
	register int		vflag;
	register char		*cutarg;
	register long		cutloyear = ZDUMP_LO_YEAR;
	register long		cuthiyear = ZDUMP_HI_YEAR;
	register timeINT_t		cutlotime;
	register timeINT_t		cuthitime;
	register char		*fakeenv;
	timeINT_t			now;
	timeINT_t			t;
	timeINT_t			newt;
	struct tm		tm;
	struct tm		newtm;
	register struct tm	*tmp;
	register struct tm	*newtmp;

	INITIALIZE(cutlotime);
	INITIALIZE(cuthitime);

	(void) setlocale(LC_ALL, "");

#if !defined(TEXT_DOMAIN)		/* Should be defined by cc -D */
#define	TEXT_DOMAIN	"SYS_TEST"	/* Use this only if it weren't */
#endif
	(void) textdomain(TEXT_DOMAIN);

	progname = argv[0];
	for (i = 1; i < argc; ++i)
		if (strcmp(argv[i], "--version") == 0) {
			(void) printf("%s\n", elsieid);
			exit(EXIT_SUCCESS);
		} else if (strcmp(argv[i], "--help") == 0) {
			usage(stdout, EXIT_SUCCESS);
		}
	vflag = 0;
	cutarg = NULL;
	while ((c = getopt(argc, argv, "c:v")) == 'c' || c == 'v')
		if (c == 'v')
			vflag = 1;
		else	cutarg = optarg;
	if ((c != EOF && c != -1) ||
		(optind == argc - 1 && strcmp(argv[optind], "=") == 0)) {
			usage(stderr, EXIT_FAILURE);
	}
	if (vflag) {

		if (cutarg != NULL) {
			long	lo;
			long	hi;
			char	dummy;

			if (sscanf(cutarg, "%ld%c", &hi, &dummy) == 1) {
				cuthiyear = hi;
			} else if (sscanf(cutarg, "%ld,%ld%c",
				&lo, &hi, &dummy) == 2) {
					cutloyear = lo;
					cuthiyear = hi;
			} else {
				(void) fprintf(stderr,
				    gettext("%s: wild -c argument %s\n"),
				    progname, cutarg);
				exit(EXIT_FAILURE);
			}
		}
		setabsolutes();
#if __sun
		if (cutarg != NULL) {
			cutlotime = yeartot(cutloyear);
			cuthitime = yeartot(cuthiyear);
		} else {
			absolute_min_time = INT32_MIN;
			absolute_max_time = INT32_MAX;
			cutlotime = yeartot(cutloyear);
			cuthitime = yeartot(cuthiyear);
		}
#endif
	}
	now = time(NULL);

	longest = 0;
	for (i = optind; i < argc; ++i)
		if (strlen(argv[i]) > longest)
			longest = strlen(argv[i]);

	if ((fakeenv = (char *)malloc(longest + 4)) == NULL) {
		(void) perror(progname);
		exit(EXIT_FAILURE);
	}
	(void) strcpy(fakeenv, "TZ=");
	if (putenv(fakeenv) != 0) {
		(void) perror(progname);
		exit(EXIT_FAILURE);
	}

	for (i = optind; i < argc; ++i) {
		static char	buf[MAX_STRING_LENGTH];

		(void) strcpy(&fakeenv[3], argv[i]);
		if (!vflag) {
			show(argv[i], now, FALSE);
			continue;
		}

#ifdef __sun
		/*
		 * We show the current time first, probably because we froze
		 * the behavior of zdump some time ago and then it got
		 * changed.
		 */
		show(argv[i], now, TRUE);
#endif
		warned = FALSE;
		t = absolute_min_time;
		show(argv[i], t, TRUE);
		t += SECSPERHOUR * HOURSPERDAY;
		show(argv[i], t, TRUE);
		if (t < cutlotime)
			t = cutlotime;
		tmp = my_localtime(&t);
		if (tmp != NULL) {
			tm = *tmp;
			(void) strncpy(buf, abbr(&tm), sizeof (buf) - 1);
		}
		for (;;) {
			if (t >= cuthitime || t >= cuthitime - SECSPERHOUR * 12)
				break;
			newt = t + SECSPERHOUR * 12;
			newtmp = localtime(&newt);
			if (newtmp != NULL)
				newtm = *newtmp;
			if ((tmp == NULL || newtmp == NULL) ? (tmp != newtmp) :
				(delta(&newtm, &tm) != (newt - t) ||
				newtm.tm_isdst != tm.tm_isdst ||
				strcmp(abbr(&newtm), buf) != 0)) {
					newt = hunt(argv[i], t, newt);
					newtmp = localtime(&newt);
					if (newtmp != NULL) {
						newtm = *newtmp;
						(void) strncpy(buf,
							abbr(&newtm),
							sizeof (buf) - 1);
					}
			}
			t = newt;
			tm = newtm;
			tmp = newtmp;
		}
#if defined(sun)
		t = yeartot(ZDUMP_HI_YEAR);
		show(argv[i], t, TRUE);
		t -= SECSPERHOUR * HOURSPERDAY;
		show(argv[i], t, TRUE);
#else
		t = absolute_max_time;
		t -= SECSPERHOUR * HOURSPERDAY;
		show(argv[i], t, TRUE);
		t += SECSPERHOUR * HOURSPERDAY;
		show(argv[i], t, TRUE);
#endif
	}
	if (fflush(stdout) || ferror(stdout)) {
		(void) fprintf(stderr, "%s: ", progname);
		(void) perror(gettext("Error writing to standard output"));
		exit(EXIT_FAILURE);
	}
	exit(EXIT_SUCCESS);
	/* If exit fails to exit... */
	return (EXIT_FAILURE);
}

static void
setabsolutes(void)
{
#ifdef __sun
	absolute_min_time = timeINT_t_MIN;
	absolute_max_time = timeINT_t_MAX;
#else
	if (0.5 == (timeINT_t)0.5) {
		/*
		 * timeINT_t is floating.
		 */
		if (sizeof (timeINT_t) == sizeof (float)) {
			absolute_min_time = (timeINT_t)-FLT_MAX;
			absolute_max_time = (timeINT_t)FLT_MAX;
		} else if (sizeof (timeINT_t) == sizeof (double)) {
			absolute_min_time = (timeINT_t)-DBL_MAX;
			absolute_max_time = (timeINT_t)DBL_MAX;
		} else {
			(void) fprintf(stderr, gettext(
"%s: use of -v on system with floating timeINT_t other than float or double\n"),
			    progname);
			exit(EXIT_FAILURE);
		}
	} else if (0 > (timeINT_t)-1) {
		/*
		 * timeINT_t is signed.  Assume overflow wraps around.
		 */
		timeINT_t t = 0;
		timeINT_t t1 = 1;

		while (t < t1) {
			t = t1;
			t1 = 2 * t1 + 1;
		}

		absolute_max_time = t;
		t = -t;
		absolute_min_time = t - 1;
		if (t < absolute_min_time)
			absolute_min_time = t;
	} else {
		/*
		 * timeINT_t is unsigned.
		 */
		absolute_min_time = 0;
		absolute_max_time = absolute_min_time - 1;
	}
#endif
}

static timeINT_t
yeartot(y)
const long	y;
{
	register long	myy;
	register long	seconds;
	register timeINT_t	t;

	myy = EPOCH_YEAR;
	t = 0;
	while (myy != y) {
		if (myy < y) {
			seconds = isleap(myy) ? SECSPERLYEAR : SECSPERNYEAR;
			++myy;
			if (t > absolute_max_time - seconds) {
				t = absolute_max_time;
				break;
			}
			t += seconds;
		} else {
			--myy;
			seconds = isleap(myy) ? SECSPERLYEAR : SECSPERNYEAR;
			if (t < absolute_min_time + seconds) {
				t = absolute_min_time;
				break;
			}
			t -= seconds;
		}
	}
	return (t);
}

static timeINT_t
hunt(char *name, timeINT_t lot, timeINT_t hit)
{
	timeINT_t			t;
	long			diff;
	struct tm		lotm;
	register struct tm	*lotmp;
	struct tm		tm;
	register struct tm	*tmp;
	char			loab[MAX_STRING_LENGTH];

	lotmp = my_localtime(&lot);
	if (lotmp != NULL) {
		lotm = *lotmp;
		(void) strncpy(loab, abbr(&lotm), sizeof (loab) - 1);
	}
	for (;;) {
		diff = (long)(hit - lot);
		if (diff < 2)
			break;
		t = lot;
		t += diff / 2;
		if (t <= lot)
			++t;
		else if (t >= hit)
			--t;
		tmp = my_localtime(&t);
		if (tmp != NULL)
			tm = *tmp;
		if ((lotmp == NULL || tmp == NULL) ? (lotmp == tmp) :
		    (delta(&tm, &lotm) == (t - lot) &&
		    tm.tm_isdst == lotm.tm_isdst &&
		    strcmp(abbr(&tm), loab) == 0)) {
				lot = t;
				lotm = tm;
				lotmp = tmp;
		} else	hit = t;
	}
	show(name, lot, TRUE);
	show(name, hit, TRUE);
	return (hit);
}

/*
 * Thanks to Paul Eggert for logic used in delta.
 */

static long
delta(newp, oldp)
struct tm	*newp;
struct tm	*oldp;
{
	register long	result;
	register int	tmy;

	if (newp->tm_year < oldp->tm_year)
		return (-delta(oldp, newp));
	result = 0;
	for (tmy = oldp->tm_year; tmy < newp->tm_year; ++tmy)
		result += DAYSPERNYEAR + isleap_sum(tmy, TM_YEAR_BASE);
	result += newp->tm_yday - oldp->tm_yday;
	result *= HOURSPERDAY;
	result += newp->tm_hour - oldp->tm_hour;
	result *= MINSPERHOUR;
	result += newp->tm_min - oldp->tm_min;
	result *= SECSPERMIN;
	result += newp->tm_sec - oldp->tm_sec;
	return (result);
}

static void
show(char *zone, timeINT_t t, int v)
{
	register struct tm	*tmp;

	(void) printf("%-*s  ", (int)longest, zone);
	if (v) {
		tmp = gmtime(&t);
		if (tmp == NULL) {
			(void) printf(tformat(), t);
		} else {
			dumptime(tmp);
			(void) printf(" UTC");
		}
		(void) printf(" = ");
	}
	tmp = my_localtime(&t);
	dumptime(tmp);
	if (tmp != NULL) {
		if (*abbr(tmp) != '\0')
			(void) printf(" %s", abbr(tmp));
		if (v) {
			(void) printf(" isdst=%d", tmp->tm_isdst);
#ifdef TM_GMTOFF
			(void) printf(" gmtoff=%ld", tmp->TM_GMTOFF);
#endif /* defined TM_GMTOFF */
		}
	}
	(void) printf("\n");
	if (tmp != NULL && *abbr(tmp) != '\0')
		abbrok(abbr(tmp), zone);
}

static char *
abbr(tmp)
struct tm	*tmp;
{
	register char	*result;
	static char	nada;

	if (tmp->tm_isdst != 0 && tmp->tm_isdst != 1)
		return (&nada);
	result = tzname[tmp->tm_isdst];
	return ((result == NULL) ? &nada : result);
}

/*
 * The code below can fail on certain theoretical systems;
 * it works on all known real-world systems as of 2004-12-30.
 */

static const char *
tformat(void)
{
#ifdef __sun
#if timeINT_t_MAX > LONG_MAX
	return ("%lld");
#else
	return ("%ld");
#endif
#else
	if (0.5 == (timeINT_t)0.5) {	/* floating */
		if (sizeof (timeINT_t) > sizeof (double))
			return ("%Lg");
		return ("%g");
	}
	if (0 > (timeINT_t)-1) {		/* signed */
		if (sizeof (timeINT_t) > sizeof (long))
			return ("%lld");
		if (sizeof (timeINT_t) > sizeof (int))
			return ("%ld");
		return ("%d");
	}
	if (sizeof (timeINT_t) > sizeof (unsigned long))
		return ("%llu");
	if (sizeof (timeINT_t) > sizeof (unsigned int))
		return ("%lu");
	return ("%u");
#endif
}

static void
dumptime(timeptr)
register const struct tm	*timeptr;
{
	static const char	wday_name[][3] = {
		"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
	};
	static const char	mon_name[][3] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};
	register const char	*wn;
	register const char	*mn;
	register int		lead;
	register int		trail;

	if (timeptr == NULL) {
		(void) printf("NULL");
		return;
	}
	/*
	 * The packaged versions of localtime and gmtime never put out-of-range
	 * values in tm_wday or tm_mon, but since this code might be compiled
	 * with other (perhaps experimental) versions, paranoia is in order.
	 */
	if (timeptr->tm_wday < 0 || timeptr->tm_wday >=
		(int)(sizeof (wday_name) / sizeof (wday_name[0])))
			wn = "???";
	else		wn = wday_name[timeptr->tm_wday];
	if (timeptr->tm_mon < 0 || timeptr->tm_mon >=
		(int)(sizeof (mon_name) / sizeof (mon_name[0])))
			mn = "???";
	else		mn = mon_name[timeptr->tm_mon];
	(void) printf("%.3s %.3s%3d %.2d:%.2d:%.2d ",
		wn, mn,
		timeptr->tm_mday, timeptr->tm_hour,
		timeptr->tm_min, timeptr->tm_sec);
#define	DIVISOR	10
	trail = timeptr->tm_year % DIVISOR + TM_YEAR_BASE % DIVISOR;
	lead = timeptr->tm_year / DIVISOR + TM_YEAR_BASE / DIVISOR +
		trail / DIVISOR;
	trail %= DIVISOR;
	if (trail < 0 && lead > 0) {
		trail += DIVISOR;
		--lead;
	} else if (lead < 0 && trail > 0) {
		trail -= DIVISOR;
		++lead;
	}
	if (lead == 0)
		(void) printf("%d", trail);
	else
		(void) printf("%d%d", lead, ((trail < 0) ? -trail : trail));
}
