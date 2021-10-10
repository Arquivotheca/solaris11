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
 * Copyright (c) 1989, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/



/*
 * error/logging/cleanup functions for ttymon.
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <paths.h>
#include "ttymon.h"
#include "tmstruct.h"
#include "tmextern.h"

static FILE *Debugfp = NULL;
static void vdebug(const char *, va_list);

int
open_filelog_stderr(void)
{
	int newfd;
	if ((newfd = dup(STDERR_FILENO)) < 0) {
		return (-1);
	}

	if (fcntl(newfd, F_SETFD, FD_CLOEXEC) < 0) {
		return (-1);
	}

	if ((FileLogfp = fdopen(newfd, "w")) == NULL) {
		return (-1);
	}
	return (0);
}

int
open_device_log(int fd)
{
	if ((DeviceLogfp = fdopen(fd, "w")) == NULL) {
		return (-1);
	}
	return (0);
}

/*
 * vlog(msg) - common message routine.
 */
static void
vlog(int verbose, int urgent, const char *fmt, va_list ap)
{
	char tstamp[32];	/* current time in readable form */
	struct timeval now;
	struct tm ltime;

	if (FileLogfp) {
		if (!isatty(fileno(FileLogfp))) {
			(void) gettimeofday(&now, NULL);
			(void) strftime(tstamp, sizeof (tstamp), "%b %e %T",
			    localtime_r(&now.tv_sec, &ltime));
			(void) fprintf(FileLogfp, "%s: ", tstamp);
		}
		(void) vfprintf(FileLogfp, fmt, ap);
		if (fmt[strlen(fmt) - 1] != '\n')
			(void) fputc('\n', FileLogfp);
		(void) fflush(FileLogfp);
	}

	if (DeviceLogfp && verbose == 0) {
		(void) vfprintf(DeviceLogfp, fmt, ap);
		if (fmt[strlen(fmt) - 1] != '\n')
			(void) fputc('\n', DeviceLogfp);
		(void) fflush(DeviceLogfp);
	}

	/*
	 * When there's no where else to put the message...
	 */
	if (urgent && FileLogfp == NULL && DeviceLogfp == NULL) {
		(void) vfprintf(stderr, fmt, ap);
		if (fmt[strlen(fmt) - 1] != '\n')
			(void) fputc('\n', stderr);
		(void) fflush(stderr);
	}

	vdebug(fmt, ap);
}

/*
 * log(fmt, ...) - put a message into the log file
 *	    - if Logfp is NULL, write message to stderr or CONSOLE
 */
/*PRINTFLIKE1*/
void
log(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vlog(0, 0, fmt, ap);
	va_end(ap);
}

void
verbose(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vlog(1, 0, fmt, ap);
	va_end(ap);
}

static void
urgent(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vlog(0, 1, fmt, ap);
	va_end(ap);
}

void
log_argv(const char *msg, char **argv)
{
	static  char	cmdline[1024];
	int i = 0;

	(void) strlcpy(cmdline, msg, sizeof (cmdline));
	while (argv[i] != NULL) {
		(void) strlcat(cmdline, argv[i], sizeof (cmdline));
		(void) strlcat(cmdline, " ", sizeof (cmdline));
		i++;
	}
	verbose("%s", cmdline);
}

/*
 * fatal(fmt, ...) - put a message into the log file, then exit.
 */
/*PRINTFLIKE1*/
void
fatal(const char *fmt, ...)
{
	if (fmt) {
		va_list ap;
		va_start(ap, fmt);
		vlog(0, 1, fmt, ap);
		va_end(ap);
	}
	urgent("ttymon exiting on fatal error");
	exit(1);
}

/*
 * opendebug - open debugging file, sets global file pointer Debugfp
 */
void
opendebug()
{
	int  fd, ret;
	char dbglog[MAXPATHLEN];

	(void) snprintf(dbglog, sizeof (dbglog), "%s.%ld", EX_DBG, getpid());

	if ((fd = open(dbglog, O_WRONLY|O_APPEND|O_CREAT, 0600)) < 0)
		fatal("open %s failed: %s", dbglog, strerror(errno));

	if (fd >= 3) {
		ret = fd;
	} else {
		if ((ret = fcntl(fd, F_DUPFD, 3)) < 0)
			fatal("F_DUPFD fcntl failed: %s",
			    strerror(errno));

	}
	if ((Debugfp = fdopen(ret, "a+")) == NULL)
		fatal("fdopen failed: %s", strerror(errno));

	if (ret != fd)
		(void) close(fd);
	/* set close-on-exec flag */
	if (fcntl(fileno(Debugfp), F_SETFD, 1) == -1)
		fatal("F_SETFD fcntl failed: %s", strerror(errno));

	log("debug log started at %s", dbglog);
}

/*
 * debug(fmt, ...) - put a message into debug file
 */
static void
vdebug(const char *fmt, va_list ap)
{
	char tstamp[32];	/* current time in readable form */
	struct timeval now;
	struct tm ltime;

	if (Debugfp == NULL)
		return;

	(void) gettimeofday(&now, NULL);
	(void) strftime(tstamp, sizeof (tstamp), "%b %e %T",
	    localtime_r(&now.tv_sec, &ltime));
	(void) fprintf(Debugfp, "debug: %s; %ld; ", tstamp, getpid());

	(void) vfprintf(Debugfp, fmt, ap);

	(void) fprintf(Debugfp, "\n");
	(void) fflush(Debugfp);
}

void
debug(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vdebug(fmt, ap);
	va_end(ap);
}
