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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include	<stdio.h>
#include	<values.h>
#include	<signal.h>
#include	<locale.h>
#include	<unistd.h>
#include	<limits.h>
#include	<stdlib.h>
#include	<string.h>
#include 	<errno.h>
#include	<math.h>
#include	<macros.h>

/* Scaling units for interval conversion. */
#define	DAYS	24
#define	HOURS	60
#define	MINUTES 60

/* Exit codes. */
#define	EXIT_OK		0
#define	EXIT_ERROR	1
#define	EXIT_USAGE	2

static void sig_alarm(int sig);
static void usage(const char *msg, int status);

int
main(int argc, char **argv)
{
	double		temp_sleep, secs, fsecs;
	char		*conversion_ptr;
	struct timespec	sleep_timespec, remaining_timespec;
	int		nanosleep_status;
	double		sleep_s = 0;
	int		operands_processed = 0;
	int		no_more_options = 0;

	(void) setlocale(LC_ALL, "");
#if ! defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"	/* Use this only if it weren't */
#endif
	(void) textdomain(TEXT_DOMAIN);

	(void) signal(SIGALRM, sig_alarm);


	for (argc--, argv++; argc > 0; argc--, argv++) {
		errno = 0;
		if (no_more_options == 0 && strcmp("--", *argv) == 0) {
			no_more_options = 1;
			continue;
		}
		temp_sleep = strtod(*argv, &conversion_ptr);

		/* Check for conversion errors. */
		switch (errno) {
		case 0:
			/* Value was OK. */
			break;
		case ERANGE:
		case EINVAL:
			usage(gettext("invalid interval"), EXIT_USAGE);
			break;
		default:
			usage(gettext("unknown error"), EXIT_ERROR);
			break;
		}

		/*
		 * Consume any trailing characters from the conversion in
		 * case they are exponents.
		 */
		if (strnlen(conversion_ptr, ARG_MAX) <= 1) {
			switch (*conversion_ptr) {
			case 'd':
				temp_sleep *= (DAYS * HOURS * MINUTES);
				break;
			case 'h':
				temp_sleep *= (HOURS * MINUTES);
				break;
			case 'm':
				temp_sleep *= (MINUTES);
				break;
			case 's':
			case '\0':
				/* Value is already in seconds. */
				break;
			default:
				usage(gettext("invalid interval"),
				    EXIT_USAGE);
			}
		} else {
			usage(gettext("invalid interval"), EXIT_USAGE);
		}

		/*
		 * All arguments are cumulative interval to sleep for,
		 * so sum them together. Check for NaN.
		 */
		sleep_s += temp_sleep;
		if (isnan(sleep_s)) {
			usage(gettext("interval overflow"), EXIT_USAGE);
		}
		/* Successfully processed an operand */
		operands_processed++;
	}

	if (operands_processed == 0) {
		usage(gettext("no operands provided"), EXIT_USAGE);
	}

	/* Check the total requested interval is non-negative. */
	if (sleep_s < 0) {
		usage(gettext("interval must not be negative"), EXIT_USAGE);
	}

	/*
	 * Break the sleep interval up into seconds and nanoseconds to get the
	 * apropriate units for the underlying nanosleep call.
	 */
	fsecs = modf(sleep_s, &secs);
	sleep_timespec.tv_sec = (time_t)min(secs, MAXLONG);
	sleep_timespec.tv_nsec = (long)min(fsecs * NANOSEC, NANOSEC - 1);

	/* Sleep until woken or the required interval has elapsed. */
	do {
		errno = 0;
		nanosleep_status = nanosleep(&sleep_timespec,
		    &remaining_timespec);
		if (nanosleep_status != 0) {
			switch (errno) {
			case EINTR:
				/*
				 * Signal interrupted, SIGALARM should be
				 * caught by handler, other signals will
				 * get their default disposition.
				 */
				sleep_timespec = remaining_timespec;
				break;
			case EINVAL:
			case ENOSYS:
			default:
				/*
				 * Invalid timespec, unsupported syscall,
				 * or something unknown went bad, so exit.
				 */
				usage(gettext("internal error"), EXIT_ERROR);
				break;
			}
		}
	} while (nanosleep_status != 0);
	return (EXIT_OK);
}

/*
 * Print a usage error an exit.
 */
static void
usage(const char *msg, int status)
{
	if (msg != NULL) {
		(void) fprintf(stderr, "%s\n", msg);
	}
	(void) fprintf(stderr, gettext("usage: sleep interval[d|h|m|s] ...\n"));
	exit(status);
}

/*
 * XCU4: Sleep must terminate with zero exit status upon receiving SIGALRM.
 */
static void
sig_alarm(int sig)
{
	(void) sig;	/* For lint. */
	exit(0);
}
