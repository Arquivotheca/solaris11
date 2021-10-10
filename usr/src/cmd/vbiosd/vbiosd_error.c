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
/*
 * Error reporting/logging and debug routines.
 */

#include <sys/types.h>
#include <stdio.h>
#include <syslog.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include "vbiosd.h"

#define	MAXLEN	512

void
vbiosd_setup_log()
{
	/* Initialize syslog attributes. */
	openlog(MYNAME, LOG_PID, LOG_DAEMON);

	if (vbiosd_debug_report == B_FALSE)
		(void) setlogmask(LOG_UPTO(LOG_WARNING));
	else
		(void) setlogmask(LOG_UPTO(LOG_DEBUG));
}

static char *
log2name(int pri)
{
	switch (pri) {
	case LOG_ERR: 		return ("error");
	case LOG_WARNING: 	return ("warning");
	case LOG_DEBUG:		return ("debug");
	default:		return ("unknown");
	}
}

static void
vbiosd_vprint(int pri, boolean_t logerrno, int err, const char *format,
    va_list varg)
{
	char	msg[MAXLEN];

	(void) vsnprintf(msg, sizeof (msg), format, varg);

	if (vbiosd_is_daemon) {
		/* setlogmask will filter out messages for us. */
		if (logerrno)
			syslog(pri, "%s, %s\n", msg, strerror(err));
		else
			syslog(pri, "%s\n", msg);
	} else {
		/* We need to check for DEBUG mode. */
		if (pri == LOG_DEBUG && !vbiosd_debug_report)
			return;

		if (logerrno)
			(void) fprintf(stderr, "%s [%s]: %s, %s\n", MYNAME,
			    log2name(pri), msg, strerror(err));
		else
			(void) fprintf(stderr, "%s [%s]: %s\n", MYNAME,
			    log2name(pri), msg);

		/* Just in case... */
		(void) fflush(stderr);
	}
}

void
vbiosd_print(int pri, boolean_t logerrno, const char *format, ...)
{
	va_list	varg;
	int	err = errno;

	va_start(varg, format);
	vbiosd_vprint(pri, logerrno, err, format, varg);
	va_end(varg);
}

/* For x86emu error reporting mechanism. */
void
printk(const char *fmt, ...)
{
	va_list	varg;
	va_start(varg, fmt);
	vbiosd_vprint(LOG_WARNING, NO_ERRNO, 0, fmt, varg);
	va_end(varg);
}
