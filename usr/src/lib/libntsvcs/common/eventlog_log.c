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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <syslog.h>
#include <thread.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <sys/synch.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <ctype.h>
#include <smbsrv/ndl/eventlog.ndl>
#include <smbsrv/libntsvcs.h>

typedef struct logr_eventlog {
	const char *el_name;
	const char *el_path;
} logr_eventlog_t;

logr_eventlog_t logr_eventlog[] = {
	{ "System",     "/var/adm/messages" },
	{ "smbd",       "/var/smb/smbd_log.txt" }
};

typedef enum {
	LOGR_MONTH = 0,
	LOGR_DAY,
	LOGR_TIME,
	LOGR_HOST,
	LOGR_SOURCE,
	LOGR_IDTAG,
	LOGR_ID,
	LOGR_PRI_FAC,
	LOGR_NARG
} logr_syslog_tokens_t;

/*
 * Event code translation struct for use in processing config file
 */
typedef struct logr_priority {
	char	*p_name;
	int	p_value;
} logr_priority_t;

static logr_priority_t logr_pri_names[] = {
	"panic",	LOG_EMERG,
	"emerg",	LOG_EMERG,
	"alert",	LOG_ALERT,
	"crit",		LOG_CRIT,
	"err",		LOG_ERR,
	"error",	LOG_ERR,
	"warn",		LOG_WARNING,
	"warning",	LOG_WARNING,
	"notice",	LOG_NOTICE,
	"info",		LOG_INFO,
	"debug",	LOG_DEBUG
};

typedef struct logr_syslog_node {
	list_node_t	ln_node;
	char		ln_logline[LOGR_MAXENTRYLEN];
} logr_syslog_node_t;

static void *logr_interposer_hdl = NULL;
static struct {
	boolean_t (*logr_op_supported)(char *);
	int (*logr_op_snapshot)(logr_context_t *);
} logr_interposer_ops;

/*
 * Set the syslog timestamp.
 *
 * This is a private helper for logr_syslog_parse_entry(), which
 * must ensure that the appropriate argv entries are non-null.
 */
static void
logr_syslog_set_timestamp(char **argv, logr_entry_t *le)
{
	char *month = argv[LOGR_MONTH];
	char *day = argv[LOGR_DAY];
	char *time = argv[LOGR_TIME];
	struct timeval	now;
	struct tm tm, cur_tm;
	char buf[32];

	bzero(&tm, sizeof (tm));
	(void) snprintf(buf, 32, "%s %s %s", month, day, time);
	if (strptime(buf, "%b" "%d" "%H:%M:%S", &tm) == NULL) {
		le->le_timestamp.tv_sec = 0;
		return;
	}

	(void) gettimeofday(&now, NULL);
	(void) localtime_r(&now.tv_sec, &cur_tm);

	tm.tm_isdst = cur_tm.tm_isdst;
	tm.tm_year = cur_tm.tm_year;
	if (tm.tm_mon > cur_tm.tm_mon)
		tm.tm_year--;

	le->le_timestamp.tv_sec = mktime(&tm);
}

/*
 * Set the syslog priority.
 *
 * This is a private helper for logr_syslog_parse_entry(), which
 * must ensure that the appropriate argv entries are non-null.
 */
static void
logr_syslog_set_priority(char **argv, logr_entry_t *le)
{
	logr_priority_t *entry;
	char *token;
	int sz = sizeof (logr_pri_names) / sizeof (logr_pri_names[0]);
	int i;

	le->le_pri = LOG_INFO;

	if ((token = argv[LOGR_PRI_FAC]) == NULL)
		return;

	for (i = 0; i < sz; i++) {
		entry = &logr_pri_names[i];

		if (strstr(token, entry->p_name) != NULL) {
			le->le_pri = entry->p_value;
			break;
		}
	}
}

/*
 * Parse a syslog entry into a log_entry_t structure.  A typical syslog
 * entry has one of the following formats:
 *
 * <month> <day> <time> <host> <msg>
 * <month> <day> <time> <host> <source>: [ID <ID> <facility.priority>] <msg>
 *
 * For Example:
 * Oct 29 09:49:20 galaxy smbd[104039]: [ID 702911 daemon.info] init done
 */
static int
logr_syslog_parse_entry(char *logline, logr_entry_t *le)
{
	char buf[LOGR_MAXENTRYLEN];
	char *argv[LOGR_NARG];
	char *value;
	char *bp;
	int i;

	(void) memset(argv, 0, sizeof (char *) * LOGR_NARG);
	(void) strlcpy(buf, logline, LOGR_MAXENTRYLEN);

	for (bp = buf, i = 0; i < LOGR_NARG; ++i) {
		if (i == LOGR_SOURCE) {
			/*
			 * If the [ID key is not present, everything
			 * that follows is the message text.
			 */
			if (strstr(bp, "[ID") == NULL)
				break;
		}

		do {
			if ((value = strsep(&bp, " \t")) == NULL)
				break;
		} while (*value == '\0');

		if ((argv[i] = value) == NULL)
			return (-1);
	}

	/*
	 * bp should be pointing at the remaining message text.
	 */
	if ((value = strchr(bp, '\n')) != NULL)
		*value = '\0';

	(void) strlcpy(le->le_msg, bp, LOGR_MAXENTRYLEN);
	(void) strlcpy(le->le_hostname, argv[LOGR_HOST], MAXHOSTNAMELEN);
	logr_syslog_set_timestamp(argv, le);
	logr_syslog_set_priority(argv, le);
	return (0);
}

static void
logr_syslog_destroy_queue(list_t *queue)
{
	logr_syslog_node_t *head;

	while ((head = list_head(queue)) != NULL) {
		list_remove(queue, head);
		free(head);
	}
	list_destroy(queue);
}

static int
logr_syslog_construct_queue(FILE *fp, list_t *queue)
{
	logr_syslog_node_t *node, *head;
	int line_num = 0;
	char logline[LOGR_MAXENTRYLEN];

	list_create(queue, sizeof (logr_syslog_node_t),
	    offsetof(logr_syslog_node_t, ln_node));

	bzero(logline, LOGR_MAXENTRYLEN);
	while (fgets(logline, LOGR_MAXENTRYLEN, fp) != NULL) {
		/* Read the last 1024 entries in the queue */
		if (line_num > LOGR_NMSGMASK) {
			head = list_head(queue);
			list_remove(queue, head);
			free(head);
		}

		if ((node = malloc(sizeof (logr_syslog_node_t))) == NULL) {
			logr_syslog_destroy_queue(queue);
			return (-1);
		}
		bzero(node->ln_logline, LOGR_MAXENTRYLEN);

		(void) strlcpy(node->ln_logline, logline, LOGR_MAXENTRYLEN);
		list_insert_tail(queue, node);
		bzero(logline, LOGR_MAXENTRYLEN);
		line_num++;
	}

	return (0);
}

/*
 * logr_syslog_load
 *
 * Loads the given log file into log_info_t structure format.
 *
 * Returns pointer to the allocated log structure on success.
 * Note that the caller is responsible for freeing the allocated
 * memory for returned log_info_t structure.
 */
static int
logr_syslog_load(FILE *fp, logr_info_t *log)
{
	logr_entry_t *entry;
	int i = 0;

	list_t queue;
	logr_syslog_node_t *node;

	if (logr_syslog_construct_queue(fp, &queue) < 0)
		return (-1);

	node = list_head(&queue);
	while (node) {
		entry = &log->li_entry[i];

		if (logr_syslog_parse_entry(node->ln_logline, entry) != 0) {
			node = list_next(&queue, node);
			continue;
		}

		if (++i > LOGR_NMSGMASK)
			break;

		node = list_next(&queue, node);
	}

	logr_syslog_destroy_queue(&queue);
	log->li_idx = i;

	return (0);
}

/*
 * logr_syslog_snapshot
 *
 * Return a snapshot of the given log in the buffer
 * provided by the caller. Returns the number of entries in
 * the log.
 */
static int
logr_syslog_snapshot(char *logname, logr_info_t *loginfo)
{
	FILE *fp;
	char path[MAXPATHLEN];
	int i;

	if ((loginfo == NULL) || (!logr_is_supported(logname)))
		return (-1);

	path[0] = '\0';
	for (i = 0; i < sizeof (logr_eventlog)/sizeof (logr_eventlog[0]); ++i) {
		if (strcasecmp(logname, logr_eventlog[i].el_name) == 0)
			(void) strlcpy(path, logr_eventlog[i].el_path,
			    MAXPATHLEN);
	}

	if ((fp = fopen(path, "r")) == 0)
		return (-1);

	if (logr_syslog_load(fp, loginfo) < 0) {
		(void) fclose(fp);
		return (-1);
	}
	(void) fclose(fp);

	if (loginfo->li_idx <= LOGR_NMSGMASK)
		return (loginfo->li_idx);

	return (LOGR_NMSGMASK+1);
}

/*
 * logr_is_supported
 *
 * Determines if a given log is supported or not.
 * Returns B_TRUE on success, B_FALSE on failure.
 */
boolean_t
logr_is_supported(char *log_name)
{
	int i;

	if (log_name == NULL)
		return (B_FALSE);

	if (logr_interposer_ops.logr_op_supported != NULL)
		return (logr_interposer_ops.logr_op_supported(log_name));

	for (i = 0; i < sizeof (logr_eventlog)/sizeof (logr_eventlog[0]); ++i) {
		if (strcasecmp(log_name, logr_eventlog[i].el_name) == 0)
			return (B_TRUE);
	}

	return (B_FALSE);
}

/*
 * logr_get_snapshot
 *
 * Allocate memory and make a copy, as a snapshot, from system log.
 * Returns 0 on success, -1 on failure.
 */
int
logr_get_snapshot(logr_context_t *ctx)
{
	logr_read_data_t *data = NULL;

	if (logr_interposer_ops.logr_op_snapshot != NULL)
		return (logr_interposer_ops.logr_op_snapshot(ctx));

	ctx->lc_cached_read_data = malloc(sizeof (logr_read_data_t));
	if (ctx->lc_cached_read_data != NULL) {
		data = ctx->lc_cached_read_data;

		data->rd_log = (logr_info_t *)malloc(sizeof (logr_info_t));
		if (data->rd_log == NULL) {
			free(data);
			return (-1);
		}
		bzero(data->rd_log, sizeof (logr_info_t));

		data->rd_tot_recnum = logr_syslog_snapshot(ctx->lc_source_name,
		    data->rd_log);
		if (data->rd_tot_recnum < 0) {
			free(data->rd_log);
			free(data);
			return (-1);
		}

		data->rd_first_read = 1;

		return (0);
	}

	return (-1);
}

/*
 * logr_init
 *
 * Initializes the Eventlog service.
 * Checks to see if a event log utility library
 * is interposed. If yes then it'll initializes logr_interposer_ops
 * structure with function pointers from this library.
 */
void
logr_init(void)
{
	logr_interposer_hdl = smb_dlopen();
	if (logr_interposer_hdl == NULL)
		return;

	bzero((void *)&logr_interposer_ops, sizeof (logr_interposer_ops));

	logr_interposer_ops.logr_op_supported =
	    (boolean_t (*)())dlsym(logr_interposer_hdl, "logr_is_supported");

	logr_interposer_ops.logr_op_snapshot =
	    (int (*)())dlsym(logr_interposer_hdl, "logr_get_snapshot");

	if (logr_interposer_ops.logr_op_supported == NULL ||
	    logr_interposer_ops.logr_op_snapshot == NULL)
		logr_fini();
}

/*
 * logr_fini
 *
 * Finalizes the Eventlog service.
 * Closes handle to interposed library.
 */
void
logr_fini(void)
{
	smb_dlclose(logr_interposer_hdl);
	logr_interposer_hdl = NULL;
	bzero((void *)&logr_interposer_ops, sizeof (logr_interposer_ops));
}
