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
 * Copyright (c) 1993, 2011, Oracle and/or its affiliates. All rights reserved.
 *
 * private interfaces for auditd plugins and auditd.
 */

#include <bsm/adt.h>
#include <bsm/audit.h>
#include <bsm/audit_record.h>
#include <bsm/audit_uevents.h>
#include <bsm/libbsm.h>
#include <errno.h>
#include <fcntl.h>
#include <libintl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>
#include <wait.h>
#include "audit_plugin.h"

static char	auditwarn[] = "/etc/security/audit_warn";
static pthread_mutex_t	syslog_lock;

static void
init_syslog_mutex()
{
	(void) pthread_mutex_init(&syslog_lock, NULL);
}

/*
 * audit_syslog() -- generate syslog messages from threads that use
 * different severity, facility code, and application names.
 *
 * syslog(3C) is thread safe, but the set openlog() / syslog() /
 * closelog() is not.
 *
 * Assumption:  the app_name and facility code are paired, i.e.,
 * if the facility code for this call is the same as for the
 * the previous, the app_name hasn't changed.
 */
void
__audit_syslog(const char *app_name, int flags, int facility, int severity,
    const char *message, ...)
{
	static pthread_once_t	once_control = PTHREAD_ONCE_INIT;
	static int		logopen = 0;
	static int		prev_facility = -1;
	va_list			args;

	(void) pthread_once(&once_control, init_syslog_mutex);

	va_start(args, message);
	(void) pthread_mutex_lock(&syslog_lock);
	if (prev_facility != facility) {
		if (logopen)
			closelog();
		openlog(app_name, flags, facility);
		vsyslog(severity, message, args);
		(void) pthread_mutex_unlock(&syslog_lock);
	} else {
		vsyslog(severity, message, args);
		(void) pthread_mutex_unlock(&syslog_lock);
	}
	va_end(args);
}

/*
 * __audit_dowarn - invoke the shell script auditwarn to notify the
 *	adminstrator about a given problem.
 * parameters -
 *	option - what the problem is
 *	text - when used with options soft and hard: which file was being
 *		   used when the filesystem filled up
 *	       when used with the plugin option:  error detail
 *	count - used with various options: how many times auditwarn has
 *		been called for this problem since it was last cleared.
 */
void
__audit_dowarn(char *option, char *text, int count)
{
	pid_t		pid;
	int		st;
	char		countstr[5];
	char		empty[1] = "";

	if ((pid = fork1()) == -1) {
		__audit_syslog("auditd", LOG_PID | LOG_ODELAY | LOG_CONS,
		    LOG_DAEMON, LOG_ALERT, "audit_warn fork failed");
		return;
	}
	if (pid != 0) {
		(void) waitpid(pid, &st, 0);
		return;
	}

	(void) snprintf(countstr, 5, "%d", count);
	if (text == NULL) {
		text = empty;
	}

	/* __audit_syslog() called only in case execl() failed */
	if (strcmp(option, "soft") == 0 || strcmp(option, "hard") == 0) {
		(void) execl(auditwarn, auditwarn, option, text, 0);
		__audit_syslog("auditd", LOG_PID | LOG_ODELAY | LOG_CONS,
		    LOG_AUTH, LOG_ALERT, "%s limit in %s.",
		    (strcmp(option, "soft") == 0) ? "soft" : "hard", text);

	} else if (strcmp(option, "allhard") == 0) {
		(void) execl(auditwarn, auditwarn, option, countstr, 0);
		__audit_syslog("auditd", LOG_PID | LOG_ODELAY | LOG_CONS,
		    LOG_AUTH, LOG_ALERT, "All audit filesystems are full.");

	} else if (strcmp(option, "plugin") == 0) {
		(void) execl(auditwarn, auditwarn, option, text, countstr, 0);
		__audit_syslog("auditd", LOG_PID | LOG_ODELAY | LOG_CONS,
		    LOG_AUTH, LOG_ALERT, "error %s.", option);

	} else {
		(void) execl(auditwarn, auditwarn, option, 0);
		__audit_syslog("auditd", LOG_PID | LOG_ODELAY | LOG_CONS,
		    LOG_AUTH, LOG_ALERT, "error %s.", option);
	}

	exit(1);
}

/*
 * __audit_dowarn2 - invoke the shell script auditwarn to notify the
 *	adminstrator about a given problem.
 * parameters -
 *	option - what the problem is
 *	name - entity reporting the problem (ie, plugin name)
 *	error - error string
 *	text - when used with options soft and hard: which file was being
 *		   used when the filesystem filled up
 *	       when used with the plugin option:  error detail
 *	count - used with various options: how many times auditwarn has
 *		been called for this problem since it was last cleared.
 */
void
__audit_dowarn2(char *option, char *name, char *error, char *text, int count)
{
	pid_t		pid;
	int		st;
	char		countstr[5];
	char		empty[4] = "...";
	char		none[3] = "--";

	if ((pid = fork()) == -1) {
		__audit_syslog("auditd", LOG_PID | LOG_ODELAY | LOG_CONS,
		    LOG_DAEMON, LOG_ALERT, "audit_warn fork failed");
		return;
	}
	if (pid != 0) {
		(void) waitpid(pid, &st, 0);
		return;
	}
	(void) snprintf(countstr, 5, "%d", count);
	if ((text == NULL) || (*text == '\0')) {
		text = empty;
	}
	if ((name == NULL) || (*name == '\0')) {
		name = none;
	}

	(void) execl(auditwarn, auditwarn, option, name, error, text,
	    countstr, 0);

	/* execl() failed */
	__audit_syslog("auditd", LOG_PID | LOG_ODELAY | LOG_CONS, LOG_AUTH,
	    LOG_ALERT, "%s plugin error: %s", name, text);

	exit(1);
}

/*
 * logpost - post the new audit log file name.
 *
 *	Entry	name = active audit.log file name
 *			NULL, if checking writability (auditd),
 *			changing audit log files, error, or exiting (binfile).
 *
 *	Exit	0 = success
 *		1 = system error -- errno
 *		    audit_warn called with the specific error
 *
 */
int
__logpost(char *name)
{
	int lerrno;

	if (unlink(BINFILE_FILE) != 0 &&
	    errno != ENOENT) {

		lerrno = errno;
		__audit_dowarn("tmpfile", strerror(errno), 0);
		errno = lerrno;
		return (1);
	}
	if (name == NULL || *name == '\0') {
		/* audit_binfile not active, no file pointer */
		return (0);
	}
	if (symlink(name, BINFILE_FILE) != 0) {

		lerrno = errno;
		__audit_dowarn("tmpfile", strerror(errno), 0);
		errno = lerrno;
		return (1);
	}

	return (0);
}

/*
 * debug use - open a file for auditd and its plugins for debug
 */
FILE *
__auditd_debug_file_open() {
	static FILE	*fp = NULL;

	if (fp != NULL)
		return (fp);
	if ((fp = fopen("/var/audit/debug", "aF")) == NULL)
		(void) fprintf(stderr, "failed to open debug file:  %s\n",
		    strerror(errno));

	return (fp);
}

/*
 * debug log for auditd, its plugins, and for logging adt issues.
 */
/*PRINTFLIKE1*/
void
__auditd_debug(char *fmt, ...)
{
	va_list	va;
	char	cbuf[26];	/* standard */
	time_t	now = time(NULL);
	FILE	*debug = __auditd_debug_file_open();

	if (debug == NULL) {
		return;
	}

	(void) fprintf(debug, "%.15s ", ctime_r(&now, cbuf, sizeof (cbuf)) + 4);
	va_start(va, fmt);
	(void) vfprintf(debug, fmt, va);
	va_end(va);
	(void) fflush(debug);
}

/*
 * __do_sethost - set the kernel instance host-id
 *
 * If the hostname look fails, with errno ENETDOWN, the returned
 * address is loopback, we choose to accept this just log it.
 *
 * If the kernel instance host-id is loopback, we try
 * to get it again, note this is likely only to happen on
 * a refresh of the audit service.
 *
 *	Entry	who = the caller
 *
 *	Returns	0 = success and system instance terminal ID set
 *		errno = ENETDOWN, loopback address set
 *		errno = fatal error
 */
int
__do_sethost(char *who)
{
	au_tid_addr_t	*termid = NULL;
	auditinfo_addr_t audit_info;
	int		save_errno = 0;

	/*
	 * Get the current audit info and look for a no-trivial
	 * IP address
	 */
	if (auditon(A_GETKAUDIT, (caddr_t)&audit_info,
	    sizeof (audit_info)) < 0) {
		save_errno = errno;
		__audit_syslog(who, LOG_PID | LOG_CONS | LOG_NOWAIT,
		    LOG_DAEMON, LOG_ALERT, "__do_sethost unable to get kernel "
		    "audit context %s", strerror(save_errno));
		errno = save_errno;
		return (errno);
	}
#ifdef	DEBUG
	__auditd_debug("__do_sethosts(%s), type=%d, 0=%x, 1=%x, 2=%x, 3=%x\n",
	    who,
	    audit_info.ai_termid.at_type,
	    audit_info.ai_termid.at_addr[0],
	    audit_info.ai_termid.at_addr[1],
	    audit_info.ai_termid.at_addr[2],
	    audit_info.ai_termid.at_addr[3]);
#endif	/* DEBUG */
	if ((audit_info.ai_termid.at_addr[0] != 0) &&
	    (audit_info.ai_termid.at_addr[0] != htonl(INADDR_LOOPBACK))) {
#ifdef	DEBUG
		__auditd_debug("__do_sethost(%s) already set\n", who);
#endif	/* DEBUG */
		return (0);
	}

	/* Force a new lookup */
	audit_info.ai_termid.at_type = AU_IPv4;
	audit_info.ai_termid.at_addr[0] = 0;
	audit_info.ai_termid.at_addr[1] = 0;
	audit_info.ai_termid.at_addr[2] = 0;
	audit_info.ai_termid.at_addr[3] = 0;
	if (auditon(A_SETKAUDIT, (caddr_t)&audit_info,
	    sizeof (audit_info)) < 0) {
		save_errno = errno;
		__audit_syslog(who, LOG_PID | LOG_CONS | LOG_NOWAIT,
		    LOG_DAEMON, LOG_ALERT, "__do_sethost unable to set kernel "
		    "audit context %s", strerror(save_errno));
		return (save_errno);
	}

	errno = ENOTSUP;	/* for termid == NULL */
	/* get my terminal ID */
	if (adt_load_hostname(NULL, (adt_termid_t **)&termid) < 0 ||
	    termid == NULL) {
		save_errno = errno;
		__auditd_debug("__do_sethost unable to get local IP "
		    "address: %s\n", strerror(errno));
		if ((errno = save_errno) != ENETDOWN) {
			free(termid);
			return (errno);
		}
	}

	audit_info.ai_termid = *termid;

	/* Update the kernel audit info with new IP address */
	if (auditon(A_SETKAUDIT, (caddr_t)&audit_info,
	    sizeof (audit_info)) < 0) {
		save_errno = errno;
		__audit_syslog(who, LOG_PID | LOG_CONS | LOG_NOWAIT,
		    LOG_DAEMON, LOG_ALERT, "__do_sethost unable to set kernel "
		    "audit context %s", strerror(save_errno));
	}
	free(termid);
	return (save_errno);
}

/*
 * getshift() - get the multiplier of bytes based on the string describing the
 * passed unit.
 */
static boolean_t
getshift(char *str, int *shift)
{
	const char *ends = "BKMGTPEZ";
	int	i;

	if (str[0] == '\0') {
		*shift = 0;
		return (B_TRUE);
	}
	for (i = 0; i < strlen(ends); i++) {
		if (toupper(str[0]) == ends[i]) {
			break;
		}
	}
	if (i == strlen(ends)) {
		return (B_FALSE);
	}

	if (str[1] == '\0' || (toupper(str[1]) == 'B' && str[2] == '\0' &&
	    toupper(str[0]) != 'B')) {
		*shift = i * 10;
		return (B_TRUE);
	}

	return (B_FALSE);
}

/*
 * __audit_hrstrtonum - translate human readable form of size (eg. "100K")
 * to the actual number in bytes.
 *
 * The code has been borrowed from the libzfs:zfs_nicestrtonum() function.
 */
boolean_t
__audit_hrstrtonum(char *hrstr, uint64_t *num)
{
	char	*end;
	int	shift;
	int	save_errno = errno;

	if (isdigit(*hrstr) == 0 && *hrstr != '.') {
		return (B_FALSE);
	}

	errno = 0;
	*num = strtoull(hrstr, &end, 10);
	if (errno != 0) {
		return (B_FALSE);
	}

	if (*end == '.') {
		long double fval;

		fval = strtold(hrstr, &end);

		if (!getshift(end, &shift)) {
			return (B_FALSE);
		}

		fval *= (1 << shift);
		if (fval > UINT64_MAX) {
			return (B_FALSE);
		}

		*num = (uint64_t)fval;

	} else {
		if (!getshift(end, &shift)) {
			return (B_FALSE);
		}
		if (shift >= 8 * sizeof (*num) ||
		    (*num << shift) >> shift != *num) {
			return (B_FALSE);
		}

		*num <<= shift;
	}

	errno = save_errno;
	return (B_TRUE);
}
