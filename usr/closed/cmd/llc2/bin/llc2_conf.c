/*
 * Copyright (c) 1998, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/param.h>
#include <sys/types.h>
#include <libintl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <libdlpi.h>
#include "llc2_conf.h"

/* Head of the configuration file list. */
llc2_conf_entry_t *conf_head = NULL;

extern int debug;

/* Maximum line length in configuration file. */
#define	BUF_SIZE 	2048

#define	COMMENT_CHAR	'#'
#define	NEWLINE_CHAR	'\n'
#define	EQUAL_CHAR	'='

/* All the keywords in the configuration file. */
static const char *LLC2_ON = "llc2_on";
static const char *DEV_NAME = "devicename";
static const char *DEV_INSTANCE = "deviceinstance";
static const char *DEV_LOOPBACK = "deviceloopback";
static const char *TIME_INTRVL = "timeinterval";
static const char *ACK_TIMER = "acktimer";
static const char *RSP_TIMER = "rsptimer";
static const char *POLL_TIMER = "polltimer";
static const char *REJ_TIMER = "rejecttimer";
static const char *REM_BUSY_TIMER = "rembusytimer";
static const char *INACT_TIMER = "inacttimer";
static const char *MAX_RETRY = "maxretry";
static const char *XMIT_WIN = "xmitwindowsz";
static const char *RECV_WIN = "rcvwindowsz";

/*
 * Given a PPA, open the corresponding configuration file.
 *
 * Param:
 *	int ppa: the given PPA.
 *
 * Return:
 *	A FILE * to the opened file.
 *	NULL if operation is not successful.
 */
FILE *
open_conf(int ppa)
{
	char fname[MAXPATHLEN];
	FILE *fp;

	(void) sprintf(fname, "%s%s.%d", LLC2_CONF_DIR, LLC2_NAME, ppa);
	if ((fp = fopen(fname, "r")) == NULL) {
		(void) fprintf(stderr, gettext("Cannot open %s: %s\n"),
		    fname, strerror(errno));
		return (NULL);
	}
	return (fp);
}

/*
 * Scan all the configuration files in the default directory.  Then
 * create and add entries to the conf entry list.  Note that the file pointer
 * to the configuration files are not closed.  So subsequent functions
 * can use them without reopening the files.
 *
 * Return:
 *	LLC2_OK if all operations are successful.
 *	LLC2_FAIL if there is an error.
 */
int
add_conf(void)
{
	DIR *llc2_dirp;
	struct dirent *conf_dp;
	llc2_conf_entry_t *tmp_confp;
	int ppa;
	FILE *tmp_fp;

	if ((llc2_dirp = opendir(LLC2_CONF_DIR)) == NULL) {
		(void) fprintf(stderr, gettext("Cannot open default"
		    " directory: %s\n"), strerror(errno));
		return (LLC2_FAIL);
	}
	errno = 0;
	while ((conf_dp = readdir(llc2_dirp)) != NULL) {
		if (strncmp(conf_dp->d_name, LLC2_NAME, LLC2_NAME_LEN) == 0) {
			if ((sscanf(conf_dp->d_name, "llc2.%d", &ppa) <= 0) ||
			    ((tmp_fp = open_conf(ppa)) == NULL)) {
				continue;
			}
			tmp_confp = malloc(sizeof (llc2_conf_entry_t));
			if (tmp_confp == NULL) {
				errno = 0;
				continue;
			}
			tmp_confp->ppa = ppa;
			tmp_confp->fp = tmp_fp;
			ADD_ENTRY(conf_head, tmp_confp);
		}
	}
	(void) closedir(llc2_dirp);
	if (errno == 0) {
		return (LLC2_OK);
	} else {
		if (debug > 0) {
			(void) fprintf(stderr, "readdir() error: %s\n",
			    strerror(errno));
		}
		return (LLC2_FAIL);
	}
}


/*
 * Print out the conf entry list.
 */
void
print_conf_entry(void)
{
	llc2_conf_entry_t *confp;
	char dev_name[MAXPATHLEN];
	int instance;
	llc2_conf_param_t param;

	for (confp = conf_head; confp != NULL; confp = confp->next_entry_p) {
		if (read_conf_file(confp->fp, dev_name, &instance,
		    &param) == -1) {
			(void) fprintf(stderr, gettext("Error in file\n"));
			return;
		}
		(void) printf("Device: %s, instance: %d, PPA %d\n", dev_name,
		    instance, confp->ppa);
	}
}

/*
 * Skip the spaces in a buffer.
 *
 * Param:
 *	buf: the buffer to be processed.
 *	bufp: after the call, bufp will point to the first non-space
 *	character or NULL if end of buffer is reached.
 */
#define	SKIP_SPACE(buf, bufp) \
{ \
	for ((bufp) = (buf); *(bufp) != NULL && isspace(*(bufp)); (bufp)++) \
		; \
} \

/*
 * Replace the first space character with NULL.
 *
 * Param:
 *	bufp: pointer the buffer to be processed.
 */
#define	REPLACE_SPACE(bufp) \
{ \
	char *tmp_bufp; \
	for (tmp_bufp = bufp; *tmp_bufp != NULL && isspace(*tmp_bufp) == 0; \
	    tmp_bufp++) \
		; \
	if (isspace(*tmp_bufp)) { \
		*tmp_bufp = NULL; \
	} \
}

/*
 * Read in the value of a keyword.  It is assumed that value is zero or
 * positive.
 *
 * Param:
 *	char *bufp: the buffer to be processed.
 *	const char *name: the name of the keyword.
 *
 * Return:
 *	the value of the keyword.
 *	LLC2_FAIL if there is an error.
 */
static int
get_value(char *bufp, const char *name)
{
	bufp += strlen(name);
	SKIP_SPACE(bufp, bufp);

	if (*bufp != EQUAL_CHAR) {
		return (LLC2_FAIL);
	}
	bufp += 1;
	SKIP_SPACE(bufp, bufp);

	if (! isdigit(*bufp)) {
		return (LLC2_FAIL);
	}
	return (atoi(bufp));
}

/* Constants used to denote which keyword has been found. */
#define	DEV_NAME_FOUND		0x0001
#define	DEV_INSTANCE_FOUND	0x0002
#define	DEV_LOOPBACK_FOUND	0x0004
#define	TIME_INTRVL_FOUND	0x0008
#define	ACK_TIMER_FOUND		0x0010
#define	RSP_TIMER_FOUND		0x0020
#define	POLL_TIMER_FOUND	0x0040
#define	REJ_TIMER_FOUND		0x0080
#define	REM_BUSY_TIMER_FOUND	0x0100
#define	INACT_TIMER_FOUND	0x0200
#define	MAX_RETRY_FOUND		0x0400
#define	XMIT_WIN_FOUND		0x0800
#define	RECV_WIN_FOUND		0x1000
#define	LLC2_ON_FOUND		0x2000

/*
 * Read a configuration file and get all the configuration entries.
 *
 * Here is a sample configuration file.  Note that the fields can be
 * in any order.
 *
 * #
 * # Copyright (c) 1998, 2011, Oracle and/or its affiliates.
 * # All rights reserved.
 * #
 *
 * # llc2 - Class II Logical Link Control Driver configuration file
 * # Built: Wed Jan 20 14:29:01 1999
 *
 * devicename=/dev/hme
 * deviceinstance=0
 * llc2_on=1       # LLC2: On/Off on this device
 * deviceloopback=1
 * timeinterval=0  # LLC2: Timer Multiplier
 * acktimer=2      # LLC2: Ack Timer
 * rsptimer=2      # LLC2: Response Timer
 * polltimer=4     # LLC2: Poll Timer
 * rejecttimer=6   # LLC2: Reject Timer
 * rembusytimer=8  # LLC2: Remote Busy Timer
 * inacttimer=30   # LLC2: Inactivity Timer
 * maxretry=6      # LLC2: Maximum Retry Value
 * xmitwindowsz=14 # LLC2: Transmit Window Size
 * rcvwindowsz=14  # LLC2: Receive Window Size
 *
 * Param:
 *	FILE *fp: pointer to the configuration file.
 *      char dev_name[] (reference): after the call, it will contain the
 *      device name.
 *      int *instance (referenced): it will contain the device instance.
 *	conf_param_t *paramp (referenced): after the call, it will
 *	contain the values of the keywords.
 *
 * Return:
 *	LLC2_OK if operation is successful.
 *	LLC2_FAIL if there is an error.
 */
int
read_conf_file(FILE *fp, char dev_name[], int *instance,
	llc2_conf_param_t *paramp)
{
	char buf[BUF_SIZE];
	char *bufp;
	int found = 0;
	int found_all;

	if (fseek(fp, 0, SEEK_SET) < 0) {
		if (debug > 0) {
			(void) fprintf(stderr, "Cannot lseek(): %s\n",
			    strerror(errno));
		}
		return (LLC2_FAIL);
	}

	found_all = DEV_NAME_FOUND | DEV_INSTANCE_FOUND | DEV_LOOPBACK_FOUND |
	    TIME_INTRVL_FOUND | ACK_TIMER_FOUND |
	    RSP_TIMER_FOUND | POLL_TIMER_FOUND | REJ_TIMER_FOUND |
	    REM_BUSY_TIMER_FOUND | INACT_TIMER_FOUND | MAX_RETRY_FOUND |
	    XMIT_WIN_FOUND | RECV_WIN_FOUND | LLC2_ON_FOUND;

	while ((fgets(buf, BUF_SIZE, fp) != NULL) && (found != found_all)) {
		SKIP_SPACE(buf, bufp);
		if (*bufp == COMMENT_CHAR) {
			if (buf[strlen(buf)] != NEWLINE_CHAR) {
				while (getc(fp) != NEWLINE_CHAR)
					;
			}
			continue;
		}
		if (strncmp(bufp, DEV_NAME, strlen(DEV_NAME)) == 0) {
			bufp += strlen(DEV_NAME);
			SKIP_SPACE(bufp, bufp);

			if (*bufp != EQUAL_CHAR) {
				(void) fprintf(stderr, gettext("Invalid line:"
				    " %s\n"), buf);
				return (LLC2_FAIL);
			}
			bufp += 1;
			SKIP_SPACE(bufp, bufp);
			if (strlen(bufp) >= MAXPATHLEN) {
				(void) fprintf(stderr, gettext("Invalid line:"
				    " %s\n"), buf);
				return (LLC2_FAIL);
			}
			(void) strcpy(dev_name, bufp);
			REPLACE_SPACE(dev_name);
			found |= DEV_NAME_FOUND;
			continue;
		} else if (strncmp(bufp, DEV_INSTANCE,
		    strlen(DEV_INSTANCE)) == 0) {
			if ((*instance = get_value(bufp, DEV_INSTANCE)) ==
			    LLC2_FAIL) {
				(void) fprintf(stderr, gettext("Invalid line:"
				    " %s\n"), buf);
				return (LLC2_FAIL);
			}
			found |= DEV_INSTANCE_FOUND;
			continue;
		} else if (strncmp(bufp, LLC2_ON, strlen(LLC2_ON)) == 0) {
			if ((paramp->llc2_on = get_value(bufp, LLC2_ON)) ==
			    LLC2_FAIL) {
				(void) fprintf(stderr, gettext("Invalid line:"
				    " %s\n"), buf);
				return (LLC2_FAIL);
			}
			found |= LLC2_ON_FOUND;
			continue;
		} else if (strncmp(bufp, DEV_LOOPBACK,
		    strlen(DEV_LOOPBACK)) == 0) {
			if ((paramp->dev_loopback =
			    get_value(bufp, DEV_LOOPBACK)) == LLC2_FAIL) {
				(void) fprintf(stderr, gettext("Invalid line:"
				    " %s\n"), buf);
				return (LLC2_FAIL);
			}
			found |= DEV_LOOPBACK_FOUND;
			continue;
		} else if (strncmp(bufp, TIME_INTRVL,
		    strlen(TIME_INTRVL)) == 0) {
			if ((paramp->time_intrvl =
			    get_value(bufp, TIME_INTRVL)) == LLC2_FAIL) {
				(void) fprintf(stderr, gettext("Invalid line:"
				    " %s\n"), buf);
				return (LLC2_FAIL);
			}
			found |= TIME_INTRVL_FOUND;
			continue;
		} else if (strncmp(bufp, ACK_TIMER, strlen(ACK_TIMER)) == 0) {
			if ((paramp->ack_timer =
			    get_value(bufp, ACK_TIMER)) == LLC2_FAIL) {
				(void) fprintf(stderr, gettext("Invalid line:"
				    " %s\n"), buf);
				return (LLC2_FAIL);
			}
			found |= ACK_TIMER_FOUND;
			continue;
		} else if (strncmp(bufp, RSP_TIMER, strlen(RSP_TIMER)) == 0) {
			if ((paramp->rsp_timer =
			    get_value(bufp, RSP_TIMER)) == LLC2_FAIL) {
				(void) fprintf(stderr, gettext("Invalid line:"
				    " %s\n"), buf);
				return (LLC2_FAIL);
			}
			found |= RSP_TIMER_FOUND;
			continue;
		} else if (strncmp(bufp, POLL_TIMER,
		    strlen(POLL_TIMER)) == 0) {
			if ((paramp->poll_timer =
			    get_value(bufp, POLL_TIMER)) == LLC2_FAIL) {
				(void) fprintf(stderr, gettext("Invalid line:"
				    " %s\n"), buf);
				return (LLC2_FAIL);
			}
			found |= POLL_TIMER_FOUND;
			continue;
		} else if (strncmp(bufp, REJ_TIMER, strlen(REJ_TIMER)) == 0) {
			if ((paramp->rej_timer =
			    get_value(bufp, REJ_TIMER)) == LLC2_FAIL) {
				(void) fprintf(stderr, gettext("Invalid line:"
				    " %s\n"), buf);
				return (LLC2_FAIL);
			}
			found |= REJ_TIMER_FOUND;
			continue;
		} else if (strncmp(bufp, REM_BUSY_TIMER,
		    strlen(REM_BUSY_TIMER)) == 0) {
			if ((paramp->rem_busy_timer =
			    get_value(bufp, REM_BUSY_TIMER)) == LLC2_FAIL) {
				(void) fprintf(stderr, gettext("Invalid line:"
				    " %s\n"), buf);
				return (LLC2_FAIL);
			}
			found |= REM_BUSY_TIMER_FOUND;
			continue;
		} else if (strncmp(bufp, INACT_TIMER,
		    strlen(INACT_TIMER)) == 0) {
			if ((paramp->inact_timer =
			    get_value(bufp, INACT_TIMER)) == LLC2_FAIL) {
				(void) fprintf(stderr, gettext("Invalid line:"
				    " %s\n"), buf);
				return (LLC2_FAIL);
			}
			found |= INACT_TIMER_FOUND;
			continue;
		} else if (strncmp(bufp, MAX_RETRY, strlen(MAX_RETRY)) == 0) {
			if ((paramp->max_retry =
			    get_value(bufp, MAX_RETRY)) == LLC2_FAIL) {
				(void) fprintf(stderr, gettext("Invalid line:"
				    " %s\n"), buf);
				return (LLC2_FAIL);
			}
			found |= MAX_RETRY_FOUND;
			continue;
		} else if (strncmp(bufp, XMIT_WIN, strlen(XMIT_WIN)) == 0) {
			if ((paramp->xmit_win =
			    get_value(bufp, XMIT_WIN)) == LLC2_FAIL) {
				(void) fprintf(stderr, gettext("Invalid line:"
				    " %s\n"), buf);
				return (LLC2_FAIL);
			}
			found |= XMIT_WIN_FOUND;
			continue;
		} else if (strncmp(bufp, RECV_WIN, strlen(RECV_WIN)) == 0) {
			if ((paramp->recv_win =
			    get_value(bufp, RECV_WIN)) == LLC2_FAIL) {
				(void) fprintf(stderr, gettext("Invalid line:"
				    " %s\n"), buf);
				return (LLC2_FAIL);
			}
			found |= RECV_WIN_FOUND;
			continue;
		} else {
			continue;
		}
	}
	/*
	 * Per NCR's request.  If the keyword llc2_on is not found,
	 * assume that the value is 1 so that the device will be plumbed.
	 * This is to provide backward compatibility of old NCR LLC2
	 * configuration files.
	 */
	if ((found & LLC2_ON_FOUND) == 0) {
		paramp->llc2_on = 1;
		found |= LLC2_ON_FOUND;
	}
	if (found == found_all) {
		return (LLC2_OK);
	} else {
		if (debug > 0) {
			(void) fprintf(stderr, "Cannot find all required"
			    " keywords.\n");
		}
		return (LLC2_FAIL);
	}
}

/* Comments for the keywords to be written to the configuration files. */
#define	LLC2_ON_COMMENT		"# LLC2: On/Off on this device"
#define	TIME_INTRVL_COMMENT	"# LLC2: Timer Multiplier"
#define	ACK_TIMER_COMMENT	"# LLC2: Ack Timer"
#define	RSP_TIMER_COMMENT	"# LLC2: Response Timer"
#define	POLL_TIMER_COMMENT	"# LLC2: Poll Timer"
#define	REJ_TIMER_COMMENT	"# LLC2: Reject Timer"
#define	REM_BUSY_TIMER_COMMENT	"# LLC2: Remote Busy Timer"
#define	INACT_TIMER_COMMENT	"# LLC2: Inactivity Timer"
#define	MAX_RETRY_COMMENT	"# LLC2: Maximum Retry Value"
#define	XMIT_WIN_COMMENT	"# LLC2: Transmit Window Size"
#define	RECV_WIN_COMMENT	"# LLC2: Receive Window Size"

/*
 * Create a configuration file in the default directory.
 *
 * Param:
 *	char *linkname: the linkname.
 *	int ppa: the PPA for this device.
 *	conf_param_t *paramp: a pointer to parameter structure which
 *	contains all the values of the keywords to be written to the file.
 *
 * Return:
 *	LLC2_OK if operation is successful.
 *	LLC2_FAIL if there is an error.
 */
int
create_conf(char *linkname, int ppa, llc2_conf_param_t *paramp)
{
	FILE *fp;
	char conf_name[MAXPATHLEN];
	char provider[DLPI_LINKNAME_MAX];
	uint_t instance;
	time_t cur_time = time(NULL);

	/* Set the permission to 0444. */
	(void) umask(S_IWUSR|S_IWGRP|S_IWOTH|S_IXUSR|S_IXGRP|S_IXOTH);
	(void) sprintf(conf_name, "%s%s.%d", LLC2_CONF_DIR, LLC2_NAME, ppa);
	if ((fp = fopen(conf_name, "w")) == NULL) {
		(void) fprintf(stderr, gettext("Cannot create conf file"
		    " %s: %s\n"), conf_name, strerror(errno));
		return (LLC2_FAIL);
	}

	/* Print out the copyright banner. */
	(void) fprintf(fp, "#\n");
	(void) fprintf(fp, "# Copyright 2008 Sun Microsystems, Inc."
	    " All rights reserved.\n");
	(void) fprintf(fp, "# Use is subject to license terms.\n");
	(void) fprintf(fp, "#\n");
	(void) fprintf(fp, "\n# llc2 - Class II Logical Link Control Driver"
	    " configuration file\n");
	if (cur_time != (time_t)-1) {
		(void) fprintf(fp, "# Built: %s\n\n", ctime(&cur_time));
	}

	(void) dlpi_parselink(linkname, provider, sizeof (provider), &instance);

	(void) fprintf(fp, "%s=%s\n", DEV_NAME, provider);
	(void) fprintf(fp, "%s=%d\n", DEV_INSTANCE, instance);
	(void) fprintf(fp, "%s=%d\t%s\n", LLC2_ON, paramp->llc2_on,
	    LLC2_ON_COMMENT);
	(void) fprintf(fp, "%s=%d\n", DEV_LOOPBACK, paramp->dev_loopback);
	(void) fprintf(fp, "%s=%d\t%s\n", TIME_INTRVL, paramp->time_intrvl,
	    TIME_INTRVL_COMMENT);
	(void) fprintf(fp, "%s=%d\t%s\n", ACK_TIMER, paramp->ack_timer,
	    ACK_TIMER_COMMENT);
	(void) fprintf(fp, "%s=%d\t%s\n", RSP_TIMER, paramp->rsp_timer,
	    RSP_TIMER_COMMENT);
	(void) fprintf(fp, "%s=%d\t%s\n", POLL_TIMER, paramp->poll_timer,
	    POLL_TIMER_COMMENT);
	(void) fprintf(fp, "%s=%d\t%s\n", REJ_TIMER, paramp->rej_timer,
	    REJ_TIMER_COMMENT);
	(void) fprintf(fp, "%s=%d\t%s\n", REM_BUSY_TIMER,
	    paramp->rem_busy_timer, REM_BUSY_TIMER_COMMENT);
	(void) fprintf(fp, "%s=%d\t%s\n", INACT_TIMER, paramp->inact_timer,
	    INACT_TIMER_COMMENT);
	(void) fprintf(fp, "%s=%d\t%s\n", MAX_RETRY, paramp->max_retry,
	    MAX_RETRY_COMMENT);
	(void) fprintf(fp, "%s=%d\t%s\n", XMIT_WIN, paramp->xmit_win,
	    XMIT_WIN_COMMENT);
	(void) fprintf(fp, "%s=%d\t%s\n", RECV_WIN, paramp->recv_win,
	    RECV_WIN_COMMENT);

	(void) fclose(fp);
	return (LLC2_OK);
}
