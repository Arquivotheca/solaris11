/*
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * BSD 3 Clause License
 *
 * Copyright (c) 2007, The Storage Networking Industry Association.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *      - Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above copyright
 *	notice, this list of conditions and the following disclaimer in
 *	the documentation and/or other materials provided with the
 *	distribution.
 *
 *      - Neither the name of The Storage Networking Industry Association (SNIA)
 *	nor the names of its contributors may be used to endorse or promote
 *	products derived from this software without specific prior written
 *	permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/* Copyright (c) 2007, The Storage Networking Industry Association. */
/* Copyright (c) 1996, 1997 PDC, Network Appliance. All Rights Reserved */

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <time.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libgen.h>
#include <pthread.h>
#include <errno.h>
#include "ndmpd_log.h"
#include "ndmpd.h"
#include "ndmpd_common.h"

#define	LOG_FNAME	"ndmplog.%d"
#define	LOG_FILE_CNT	5
#define	LOG_FILE_SIZE	4 * 1024 * 1024
#define	LOG_SIZE_INT	256

static FILE *logfp;
static int ndmp_synclog = 1;


/*
 * Since we use buffered file I/O for log file, the thread may lose CPU.
 * At this time, another thread can destroy the contents of the buffer
 * that must be written to the log file.  The following mutex is used
 * to allow only one thread to write into the log file.
 */
mutex_t log_lock;
mutex_t idx_lock;

/*
 * mk_pathname
 *
 * Append the NDMP working directory path to the specified file
 */
static char *
mk_pathname(char *fname, char *path, int idx)
{
	static char buf[PATH_MAX];
	static char name[NAME_MAX];
	char *fmt;
	int len;

	len = strlen(path);
	fmt = (path[len - 1] == '/') ? "%s%s" : "%s/%s";

	/* LINTED variable format specifier */
	(void) snprintf(name, NAME_MAX, fname, idx);

	/* LINTED variable format specifier */
	(void) snprintf(buf, PATH_MAX, fmt, path, name);
	return (buf);
}


/*
 * openlogfile
 *
 * Open the NDMP log file
 */
static int
openlogfile(char *fname, char *mode)
{
	int rv;

	if (fname == NULL || *fname == '\0' || mode == NULL || *mode == '\0')
		return (-1);

	(void) mutex_lock(&log_lock);
	rv = 0;
	if (logfp != NULL) {
		rv = -1;
	} else if ((logfp = fopen(fname, mode)) == NULL) {
		syslog(LOG_ERR, "Error opening logfile %s, %m.", fname);
		syslog(LOG_ERR, "Using system log for logging.");
		rv = -1;
	}

	(void) mutex_unlock(&log_lock);
	return (rv);
}


/*
 * log_write_cur_time
 *
 * Add the current time for each log entry
 */
static void
log_write_cur_time(void)
{
	struct tm tm;
	time_t secs;

	secs = time(NULL);
	(void) localtime_r(&secs, &tm);
	(void) fprintf(logfp, "%2d/%02d %2d:%02d:%02d ",
	    tm.tm_mon + 1, tm.tm_mday,
	    tm.tm_hour, tm.tm_min, tm.tm_sec);
}


/*
 * add_newline
 *
 * The new line at the end of each log
 */
static void
add_newline(char *fmt)
{
	if (fmt[strlen(fmt) - 1] != '\n')
		(void) fputc('\n', logfp);
}


/*
 * log_append
 *
 * Append the message to the end of the log
 */
static void
log_append(char *msg)
{
	log_write_cur_time();
	(void) fwrite(msg, 1, strlen(msg), logfp);
	add_newline(msg);
	if (ndmp_synclog)
		(void) fflush(logfp);
}


/*
 * ndmp_log_open_file
 *
 * Open the log file either for append or write mode.
 */
int
ndmp_log_open_file(void)
{
	char *fname;
	char oldfname[PATH_MAX];
	char *lpath;
	struct stat64 st;
	int i;

	/* Create the debug path if doesn't exist */
	lpath = ndmpd_get_prop(NDMP_DEBUG_PATH);
	if ((lpath == NULL) || (*lpath == NULL))
		lpath = "/var/ndmp";

	if (stat64(lpath, &st) < 0) {
		if (mkdirp(lpath, 0755) < 0) {
			NDMP_LOG(LOG_ERR, "Could not create log path %s: %m.",
			    lpath);
			lpath = "/var";
		}
	}

	/*
	 * NDMP log file name will be {logfilename}.0 to {logfilename}.5, where
	 * {logfilename}.0 will always be the latest and the {logfilename}.5
	 * will be the oldest available file on the system. We keep maximum of 5
	 * log files. With the new session the files are shifted to next number
	 * and if the last file {logfilename}.5 exist, it will be overwritten
	 * with {logfilename}.4.
	 */
	i = LOG_FILE_CNT - 1;
	while (i >= 0) {
		fname = mk_pathname(LOG_FNAME, lpath, i);
		(void) strncpy(oldfname, fname, PATH_MAX);
		if (stat64(oldfname, &st) == -1) {
			i--;
			continue;
		}

		fname = mk_pathname(LOG_FNAME, lpath, i + 1);
		if (rename(oldfname, fname))
			syslog(LOG_DEBUG,
			    "Could not rename from %s to %s",
			    oldfname, fname);
		i--;
	}

	fname = mk_pathname(LOG_FNAME, lpath, 0);
	return (openlogfile(fname, "a"));
}

/*
 * ndmp_log_close_file
 *
 * Close the log file
 */
void
ndmp_log_close_file(void)
{
	(void) mutex_lock(&log_lock);
	if (logfp != NULL) {
		(void) fclose(logfp);
		logfp = NULL;
	}
	(void) mutex_unlock(&log_lock);
}

void
ndmp_log(ulong_t priority, char *func, int ln, char *fmt, ...)
{
	int c;
	va_list args;
	char *f, *b;
	char ndmp_log_buf[PATH_MAX+KILOBYTE];
	char ndmp_syslog_buf[PATH_MAX+KILOBYTE];
	char buf[PATH_MAX+KILOBYTE];
	char tstampbuf[NDMP_LOG_MAX];
	char *errstr;
	boolean_t force_error_report = FALSE;
	int session_id = 0;

	thread_data_t *thread_data =
	    (thread_data_t *)pthread_getspecific(thread_data_key);

	/* logging may happen before thread_data is initialized, so check */
	if (thread_data != NULL) {
		session_id = thread_data->session_id;
	}

	if (session_id == 0) {
		force_error_report = TRUE;
	}

	if (priority > 7)
		priority = LOG_ERR;

	if (priority != LOG_DEBUG) {
		force_error_report = TRUE;
	}

	if (fmt == NULL) {
		/* A programming error if fmt is NULL, report for analysis */
		(void) strcpy(buf,
		    "ndmp_log() called with null message pointer");
		force_error_report = TRUE;
	} else {
		va_start(args, fmt);
		/* Replace text error messages if fmt contains %m */
		b = buf;
		f = fmt;
		while (((c = *f++) != '\0') && (c != '\n') &&
		    (b < &buf[PATH_MAX + KILOBYTE])) {
			if (c != '%') {
				*b++ = c;
				continue;
			}
			if ((c = *f++) != 'm') {
				*b++ = '%';
				*b++ = c;
				continue;
			}

			if ((errstr = strerror(errno)) == NULL) {
				(void) snprintf(b, &buf[PATH_MAX+KILOBYTE] - b,
				    "error %d", errno);
			} else {
				while ((*errstr != '\0') &&
				    (b < &buf[PATH_MAX+KILOBYTE])) {
					if (*errstr == '%') {
						(void) strncpy(b, "%%", 2);
						b += 2;
					} else {
						*b++ = *errstr;
					}
					errstr++;
				}
				*b = '\0';
			}
			b += strlen(b);
		}
		*b = '\0';
	}

	/* LINTED variable format specifier */
	(void) vsnprintf(ndmp_syslog_buf, sizeof (ndmp_syslog_buf), buf, args);
	va_end(args);

	if (force_error_report == TRUE) {
		(void) snprintf(tstampbuf, NDMP_LOG_MAX, "[%s:%d]", func, ln);
		(void) snprintf(ndmp_log_buf, sizeof (ndmp_log_buf),
		    "[%d] *** E R R O R *** %s:%s",
		    session_id, tstampbuf, ndmp_syslog_buf);
	} else {
		(void) snprintf(ndmp_log_buf, sizeof (ndmp_log_buf), "[%d] %s",
		    session_id, ndmp_syslog_buf);
	}

	(void) mutex_lock(&log_lock);

	/* Send all logs other than debug, to syslog log file. */
	if (priority != LOG_DEBUG)
		syslog(priority, "[%d] %s", session_id, ndmp_syslog_buf);

	if (logfp != NULL)
		log_append(ndmp_log_buf);

	(void) mutex_unlock(&log_lock);
}
