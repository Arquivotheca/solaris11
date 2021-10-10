/*
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */
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

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <paths.h>
#include <security/pam_appl.h>
#include <security/pam_modules.h>
#include <security/pam_impl.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <syslog.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define	DEFAULT_TICKETDIR	_PATH_SYSVOL "/tty_tickets"
#define	SUDO_TICKETDIR		_PATH_SYSVOL "/sudo"
#define	DEFAULT_TIMEOUT		5

/*
 * The content of this ticket will be different between 32 bit and 64 bit
 * versions of this module.  Consideration was given to making it either
 * always be 32 bit or always 64 bit but that doesn't solve the problem
 * of us having mismatched bit sizes for su and sudo which is the only reason
 * for attempting to do this.  The module fails safely anyway since
 * we explicitly check the file size before reading it, if the file is of
 * the wrong size it is unlinked.  This will only be visbile to users
 * who are running mixed bit size su and sudo alternate between them within
 * the lifetime of a single ticket, it will look exactly like a ticket expiry.
 */
struct ticket_content {
	dev_t			tc_dev;
	dev_t			tc_rdev;
	ino_t			tc_ino;
	struct timeval	tc_ctime;
};

static boolean_t debug = B_FALSE;

/*
 * This module must only ever return PAM_SUCCESS or PAM_IGNORE, if we
 * return PAM_SERVICE_ERR we might stop someone who can authenticate to
 * root being able to su to fix any problems.  Given that the intent is
 * that this module is marked as 'sufficient' in the stack and is always
 * above pam_unix_auth.so that is fine.
 */
int
common(pam_handle_t *pamh, int argc, const char **argv,
    char *ticketname, char *tty, time_t *timeout)
{
	char *auser, *user, *btty, *ptty;
	int i;
	boolean_t sudo_compat = B_FALSE;

	(void) pam_get_item(pamh, PAM_USER, (void **)&user);
	if (user == NULL || user[0] == '\0') {
		return (PAM_IGNORE);
	}

	(void) pam_get_item(pamh, PAM_AUSER, (void **)&auser);
	if (auser == NULL || auser[0] == '\0') {
		return (PAM_IGNORE);
	}
	(void) pam_get_item(pamh, PAM_TTY, (void **)&ptty);
	if (ptty == NULL || ptty[0] == '\0') {
		return (PAM_IGNORE);
	}

	/* Set defaults and set any override options */
	*timeout = DEFAULT_TIMEOUT;
	for (i = 0; i < argc; i++) {
		if (strncmp(argv[i], "timeout=", sizeof ("timeout=")) == 0) {
			*timeout = atol(&argv[i][sizeof ("timeout=")]);
		} else if (strncmp(argv[i], "sudo-compat",
		    sizeof ("sudo-compat")) == 0) {
			sudo_compat = B_TRUE;
			if (strcmp(user, "root") != 0) {
				return (PAM_IGNORE);
			}
		} else if (strncmp(argv[i], "debug", sizeof ("debug")) == 0) {
			debug = B_TRUE;
		}
	}

	/* don't use basename() because it modifies its argument */
	btty = strrchr(ptty, '/') + 1;
	(void) strlcpy(tty, ptty, MAXPATHLEN);
	if (sudo_compat) {
		(void) snprintf(ticketname, MAXPATHLEN,
		    "%s/%s/%s", SUDO_TICKETDIR, auser, btty);
	} else {
		(void) snprintf(ticketname, MAXPATHLEN, "%s/%s/%s/%s",
		    DEFAULT_TICKETDIR, auser, user, btty);
	}

	return (PAM_SUCCESS);
}

/*ARGSUSED*/
int
pam_sm_authenticate(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
	char ticketname[MAXPATHLEN];
	char tty[MAXPATHLEN];
	struct stat ttysb = { 0 };
	struct stat ticketsb = { 0 };
	struct ticket_content ticket_content;
	time_t now;
	time_t timeout;
	int ticket = -1;
	int error;
	int rb;

	error = common(pamh, argc, argv, ticketname, tty, &timeout);
	if (error != PAM_SUCCESS) {
		return (error);
	}

	error = PAM_IGNORE;
	if (stat(tty, &ttysb) == -1) {
		__pam_log(LOG_AUTH|LOG_ERR,
		    "pam_tty_tickets: unable to stat tty %s: %m\n", tty);
		goto out;
	}

	ticket = open(ticketname, O_RDONLY|O_NOFOLLOW|O_NOCTTY, 0600);
	if (ticket == -1) {
		if (debug) {
			__pam_log(LOG_AUTH|LOG_DEBUG,
			    "pam_tty_tickets: unable to open ticket %s: %m",
			    ticketname);
		}
		goto out;
	}

	if (fstat(ticket, &ticketsb) != 0) {
		if (debug) {
			__pam_log(LOG_AUTH|LOG_DEBUG,
			    "pam_tty_tickets: unable to stat ticket %s: %m",
			    ticketname);
		}
		goto out;
	}

	if (ticketsb.st_size != sizeof (ticket_content)) {
		if (debug) {
			__pam_log(LOG_AUTH|LOG_DEBUG,
			    "pam_tty_tickets: invalid ticket %s"
			    "size got %d expected %d",
			    ticketname, ticketsb.st_size,
			    sizeof (ticket_content));
		}
		(void) unlink(ticketname);
		goto out;
	}

	rb = read(ticket, &ticket_content, sizeof (ticket_content));
	if (rb != sizeof (ticket_content)) {
		if (debug) {
			__pam_log(LOG_AUTH|LOG_DEBUG,
			    "pam_tty_tickets: invalid ticket content %s",
			    ticketname);
		}
		goto out;
	}

	if (ticket_content.tc_dev != ttysb.st_dev ||
	    ticket_content.tc_rdev != ttysb.st_rdev ||
	    ticket_content.tc_ino != ttysb.st_ino) {
		(void) unlink(ticketname);
		if (debug) {
			__pam_log(LOG_AUTH|LOG_DEBUG,
			    "pam_tty_tickets: invalid ticket %s for tty",
			    ticketname, tty);
		}
		goto out;
	}

	now = time(NULL);
	if (now - ticketsb.st_mtime > (timeout * 60)) {
		(void) unlink(ticketname);
		__pam_log(LOG_AUTH|LOG_INFO,
		    "pam_tty_tickets: ticket %s expired", ticketname);
		return (PAM_IGNORE);
	}

	__pam_log(LOG_AUTH|LOG_INFO,
	    "pam_tty_tickets: ticket %s valid", ticketname);
	error = PAM_SUCCESS;
out:
	if (ticket != -1) {
		(void) close(ticket);
	}

	return (error);
}

int
pam_sm_setcred(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
	time_t now;
	int wb;
	char ticketname[MAXPATHLEN];
	char *ticketdir = NULL;
	char tty[MAXPATHLEN];
	struct stat ttysb = { 0 };
	struct stat ticketsb = { 0 };
	int ticket = -1;
	int error = PAM_IGNORE;
	struct ticket_content ticket_content;
	time_t timeout;

	error = common(pamh, argc, argv, ticketname, tty, &timeout);
	if (error != PAM_SUCCESS) {
		return (error);
	}

	/*
	 * If we are being asked to delete the creds then we don't
	 * care if the ticket has expired or if it already existed
	 * just attempt to remove it.
	 */
	if (flags & PAM_DELETE_CRED) {
		__pam_log(LOG_AUTH|LOG_INFO,
		    "pam_tty_tickets: deleting ticket %s", ticketname);
		(void) unlink(ticketname);
		goto out;
	}

	now = time(NULL);
	if (lstat(ticketname, &ticketsb) == 0 &&
	    !(now - ticketsb.st_mtime > (timeout * 60))) {
		__pam_log(LOG_AUTH|LOG_INFO,
		    "pam_tty_tickets: ticket %s still valid", ticketname);
		goto out;
	}

	ticketdir = dirname(strdup(ticketname));
	if (mkdirp(ticketdir, 0700) == -1 && errno != EEXIST) {
		__pam_log(LOG_AUTH|LOG_ERR,
		    "pam_tty_tickets: unable to create ticket directory %s",
		    ": %m");
		goto out;
	}

	if (stat(tty, &ttysb) == -1) {
		__pam_log(LOG_AUTH|LOG_ERR,
		    "pam_tty_tickets: pam_sm_setcred unable to stat tty %s: %m",
		    tty);
		goto out;
	}

	ticket_content.tc_dev = ttysb.st_dev;
	ticket_content.tc_rdev = ttysb.st_rdev;
	ticket_content.tc_ino = ttysb.st_ino;
	ticket_content.tc_ctime.tv_sec = 0;
	ticket_content.tc_ctime.tv_usec = 0;
	ticket = open(ticketname,
	    O_CREAT|O_WRONLY|O_NOFOLLOW|O_NOCTTY|O_TRUNC, 0600);
	if (ticket == -1) {
		__pam_log(LOG_AUTH|LOG_ERR,
		    "pam_tty_tickets: unable to create ticket %s: %m",
		    ticketname);
		goto out;
	}

	wb = write(ticket, &ticket_content, sizeof (ticket_content));
	if (wb != sizeof (ticket_content)) {
		__pam_log(LOG_AUTH|LOG_ERR,
		    "pam_tty_tickets: unable to create ticket content %s: %m",
		    ticketname);
		(void) unlink(ticketname);
		goto out;
	}

	__pam_log(LOG_AUTH|LOG_INFO,
	    "pam_tty_tickets: new ticket %s created", ticketname);
	error = PAM_SUCCESS;
out:
	if (ticket != -1) {
		(void) close(ticket);
	}
	if (ticketdir != NULL) {
		free(ticketdir);
	}

	return (error);
}
