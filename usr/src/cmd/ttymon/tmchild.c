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

#include	<stdio.h>
#include	<stdlib.h>
#include	<fcntl.h>
#include	<errno.h>
#include	<sys/types.h>
#include	<termio.h>
#include	<string.h>
#include	<signal.h>
#include	<poll.h>
#include	<unistd.h>
#include 	"sys/stropts.h"
#include 	<sys/resource.h>
#include	"ttymon.h"
#include	"tmstruct.h"
#include	"tmextern.h"

static void openline();
static void invoke_service();
static char	*do_autobaud();
static	struct	Gdef	*next_speed();

extern	struct	Gdef	*get_speed();
extern struct strbuf *peek_ptr, *do_peek();

/*
 * tmchild	- process that handles peeking data, determine baud rate
 *		  and invoke service on each individual port.
 *
 */
void
tmchild(struct pmtab *pmtab)
{
	struct Gdef *speedef;
	char	*auto_speed = "";
	struct	sigaction sigact;

	debug("in tmchild");
	peek_ptr = NULL;

	/* non-GETTY no longer supported */
	if (pmtab->p_status != GETTY)
		abort();

	speedef = get_speed(pmtab->p_ttylabel);
	openline(pmtab, speedef);
	if (pmtab->p_ttyflags & (C_FLAG|B_FLAG)) {
		if (pmtab->p_fd >= 0) {
			if ((pmtab->p_modules != NULL) &&
			    (*(pmtab->p_modules) != '\0')) {
				if (push_linedisc(pmtab->p_fd,
				    pmtab->p_modules, pmtab->p_device) == -1) {
					(void) close(pmtab->p_fd);
					return;
				}
			}
		}
	}
	if ((pmtab->p_ttyflags & C_FLAG) &&
	    (!(pmtab->p_flags & X_FLAG))) {
		/*
		 * if "c" flag is set, and the port is not disabled
		 * invoke service immediately
		 */
		if (set_termio(0, speedef->g_fflags,
		    NULL, FALSE, CANON) == -1) {
			fatal("set final termio failed");
		}
		invoke_service(pmtab);
		exit(1);	/*NOTREACHED*/
	}
	if (speedef->g_autobaud & A_FLAG) {
		auto_speed = do_autobaud(pmtab, speedef);
	}
	if (set_termio(0, speedef->g_fflags, NULL, FALSE, CANON) == -1) {
		fatal("set final termio failed");
	}
	if ((pmtab->p_ttyflags & (R_FLAG|A_FLAG)) ||
	    (pmtab->p_status == GETTY) || (pmtab->p_timeout > 0)) {
		write_prompt(1, pmtab, TRUE, TRUE);
		if (pmtab->p_timeout) {
			sigact.sa_flags = 0;
			sigact.sa_handler = timedout;
			(void) sigemptyset(&sigact.sa_mask);
			(void) sigaction(SIGALRM, &sigact, NULL);
			(void) alarm((unsigned)pmtab->p_timeout);
		}
	} else if ((pmtab->p_ttyflags & (B_FLAG)))
			write_prompt(pmtab->p_fd, pmtab, TRUE, TRUE);


	/* Loop until user is successful in invoking service. */
	for (;;) {

		/* Peek the user's typed response and respond appropriately. */
		switch (poll_data()) {
		case GOODNAME:
			debug("got GOODNAME");
			if (pmtab->p_timeout) {
				(void) alarm((unsigned)0);
				sigact.sa_flags = 0;
				sigact.sa_handler = SIG_DFL;
				(void) sigemptyset(&sigact.sa_mask);
				(void) sigaction(SIGALRM, &sigact, NULL);
			}
			if (pmtab->p_flags & X_FLAG) {
				write_prompt(1, pmtab, TRUE, FALSE);
				break;
			}
			if (set_termio(0, speedef->g_fflags, auto_speed,
			    FALSE, CANON) == -1) {
				fatal("set final termio failed");
			}
			invoke_service(pmtab);
			exit(1);	/*NOTREACHED*/

		case BADSPEED:
			/* wrong speed! try next speed in the list. */
			speedef = next_speed(speedef);
			debug("BADSPEED: setup next speed");

			if (speedef->g_autobaud & A_FLAG) {
				if (auto_termio(0) == -1) {
					exit(1);
				}
				auto_speed = do_autobaud(pmtab, speedef);
			} else {
				auto_speed = NULL;
				/*
				 * this reset may fail if the speed is not
				 * supported by the system
				 * we just cycle through it to the next one
				 */
				if (set_termio(0, speedef->g_iflags, NULL,
				    FALSE, CANON) != 0) {
					log("Warning -- speed of <%s> may "
					    "be not supported by the system",
					    speedef->g_id);
				}
			}
			write_prompt(1, pmtab, TRUE, TRUE);
			break;

		case NONAME:
			debug("got NONAME");
			write_prompt(1, pmtab, FALSE, FALSE);
			break;

		}  /* end switch */

		peek_ptr = NULL;
		if (pmtab->p_timeout) {
			sigact.sa_flags = 0;
			sigact.sa_handler = timedout;
			(void) sigemptyset(&sigact.sa_mask);
			(void) sigaction(SIGALRM, &sigact, NULL);
			(void) alarm((unsigned)pmtab->p_timeout);
		}
	} /* end for loop */
}

static void
openline(struct pmtab *pmtab, struct Gdef *speedef)
{
	char	 buffer[5];
	int	 rtn = 0;
	int	 line_count;

	debug("in openline");
	if (pmtab->p_status != GETTY) {
		(void) close(0);
		/* open should return fd 0, if not, then close it */
		if ((pmtab->p_fd = open(pmtab->p_device, O_RDWR)) != 0) {
			fatal("open \"%s\" failed: %s", pmtab->p_device,
			    strerror(errno));
		}
	}
	(void) close(1);
	(void) close(2);
	(void) dup(0);
	(void) dup(0);

	if (pmtab->p_ttyflags & R_FLAG) { /* wait_read is needed */
		if (pmtab->p_count) {
			if (peek_ptr != NULL)
				if ((peek_ptr->buf[0]&0x7F) == '\n' ||
				    (peek_ptr->buf[0]&0x7F) == '\r')
					pmtab->p_count--;

			/*
			 * - wait for "p_count" lines
			 * - datakit switch does not
			 *   know you are a host or a terminal
			 * - so it send you several lines of msg
			 * - we need to swallow that msg
			 * - we assume the baud rate is correct
			 * - if it is not, '\n' will not look like '\n'
			 * and we will wait forever here
			 */
			if (set_termio(0, speedef->g_fflags,
			    NULL, TRUE, CANON) == -1) {
				fatal("set final termio failed");
			}
			for (line_count = 0; line_count < pmtab->p_count; ) {
				if (read(0, buffer, 1) < 0 ||
				    *buffer == '\0' || *buffer == '\004') {
					(void) close(0);
					exit(0);
				}
				if (*buffer == '\n')
					line_count++;
			}
		} else { /* wait for 1 char */
			if (peek_ptr == NULL) {
				if (set_termio(0, NULL, NULL,
				    TRUE, RAW) == -1) {
					fatal("set termio RAW failed");
				}
				rtn = read(0, buffer, 1);
			} else
				*buffer = (peek_ptr->buf[0]&0x7F);

			/*
			 * NOTE: Cu on a direct line when ~. is encountered will
			 * send EOTs to the other side.  EOT=\004
			 */
			if (rtn < 0 || *buffer == '\004') {
				(void) close(0);
				exit(0);
			}
		}
		peek_ptr = NULL;
		if (!(pmtab->p_ttyflags & A_FLAG)) { /* autobaud not enabled */
			if (set_termio(0, speedef->g_fflags,
			    NULL, TRUE, CANON) == -1) {
				fatal("set final termio failed");
			}
		}
	}
	if (pmtab->p_ttyflags & B_FLAG) { /* port is bi-directional */
		/* set advisory lock on the line */
		if (tm_lock(0) != 0) {
			/*
			 * device is locked
			 * child exits and let the parent wait for
			 * the lock to go away
			 */
			exit(0);
		}
		/* change ownership back to root */
		(void) fchown(0, ROOTUID, Tty_gid);
		(void) fchmod(0, 0620);
	}
}

/*
 *	write_prompt	- write the msg to fd
 *			- if flush is set, flush input queue
 *			- if clear is set, write a new line
 */
void
write_prompt(int fd, struct pmtab *pmtab, int flush, int clear)
{
	char	*ptr, buffer[BUFSIZ];
	FILE	*fp;

	debug("in write_prompt");
	if (flush)
		flush_input(fd);
	if (clear) {
		(void) write(fd, "\r\n", 2);
	}

	/* print out /etc/issue */
	if ((fp = fopen(ISSUEFILE, "r")) != NULL) {
		while ((ptr = fgets(buffer, sizeof (buffer), fp)) != NULL) {
			(void) write(fd, ptr, strlen(ptr));
		}
		(void) fclose(fp);
	}

	/* Print prompt message. */
	(void) write(fd, pmtab->p_prompt, strlen(pmtab->p_prompt));
}

/*
 *	timedout	- input period timed out
 */
void
timedout()
{
	exit(1);
}

/*
 *	do_autobaud	- do autobaud
 *			- if it succeed, set the new speed and return
 *			- if it failed, it will get the nextlabel
 *			- if next entry is also autobaud,
 *			  it will loop back to do autobaud again
 *			- otherwise, it will set new termio and return
 */
static	char	*
do_autobaud(struct pmtab *pmtab, struct Gdef *speedef)
{
	int	done = FALSE;
	char	*auto_speed;
	debug("in do_autobaud");

	while (!done) {
		if ((auto_speed = autobaud(0, pmtab->p_timeout)) == NULL) {
			speedef = next_speed(speedef);
			if (speedef->g_autobaud & A_FLAG) {
				continue;
			} else {
				if (set_termio(0, speedef->g_iflags, NULL,
				    TRUE, CANON) != 0) {
					exit(1);
				}
				done = TRUE;
			}
		} else {
			if (set_termio(0, speedef->g_fflags, auto_speed,
			    TRUE, CANON) != 0) {
				exit(1);
			}
			done = TRUE;
		}
	}

	debug("autobaud done: speed %s", auto_speed ? auto_speed : "<null>");

	return (auto_speed);
}

/*
 * 	next_speed(speedef)
 *	- find the next entry according to nextlabel. If "nextlabel"
 *	  is not valid, go back to the old ttylabel.
 */

static	struct	Gdef *
next_speed(struct Gdef *speedef)
{
	struct	Gdef *sp;

	if (strcmp(speedef->g_nextid, speedef->g_id) == 0)
		return (speedef);
	if ((sp = find_def(speedef->g_nextid)) == NULL) {
		log("%s's next speed-label (%s) is bad.", speedef->g_id,
		    speedef->g_nextid);

		/* go back to the original entry. */
		if ((sp = find_def(speedef->g_id)) == NULL) {
			/* if failed, complain and quit. */
			fatal("unable to find (%s) again", speedef->g_id);
		}
	}
	return (sp);
}

static	char	 pbuf[BUFSIZ];	/* static buf for TTYPROMPT 	*/
static	char	 tbuf[BUFSIZ];	/* static buf for TERM		*/

/*
 * void invoke_service	- invoke the service
 */

static	void
invoke_service(struct pmtab *pmtab)
{
	char	 *argvp[MAXARGS];		/* service cmd args */
	int	 cnt = 0;			/* arg counter */

	debug("in invoke_service");

	if (tcgetsid(0) != getsid(getpid())) {
		cons_printf("Warning -- ttymon cannot allocate controlling "
		    "tty on \"%s\",\n", pmtab->p_device);
		cons_printf("\tThere may be another session active on this "
		    "port.\n");

		if (strcmp("/dev/console", pmtab->p_device) != 0) {
			/*
			 * if not on console, write to stderr to warn the user
			 * also.
			 */
			(void) fprintf(stderr, "Warning -- ttymon cannot "
			    "allocate controlling tty on \"%s\",\n",
			    pmtab->p_device);
			(void) fprintf(stderr, "\tthere may be another session "
			    "active on this port.\n");
		}
	}

	if (pmtab->p_status != GETTY) {
		abort();
	}

	if (pmtab->p_flags & U_FLAG) {
		if (account(pmtab->p_device) != 0) {
			fatal("invoke_service: account failed");
		}
	}

	/* parse command line */
	mkargv(pmtab->p_server, &argvp[0], &cnt, MAXARGS-1);

	if (!(pmtab->p_ttyflags & C_FLAG)) {
		(void) snprintf(pbuf, sizeof (pbuf),
		    "TTYPROMPT=%s", pmtab->p_prompt);
		if (putenv(pbuf)) {
			fatal("cannot expand service <%s> environment",
			    argvp[0]);
		}
	}

	if (pmtab->p_status != GETTY)
		fatal("non-getty (tmexpress) services are no longer supported");

	if (setgid(pmtab->p_gid))
		fatal("cannot set group id to %u: %s", pmtab->p_gid,
		    strerror(errno));

	if (setuid(pmtab->p_uid))
		fatal("cannot set user id to %u: %s", pmtab->p_uid,
		    strerror(errno));

	if (chdir(pmtab->p_dir))
		fatal("cannot chdir to %s: %s", pmtab->p_dir, strerror(errno));

	if (pmtab->p_uid != ROOTUID) {
		/* change ownership and mode of device */
		(void) fchown(0, pmtab->p_uid, Tty_gid);
		(void) fchmod(0, 0620);
	}


	if (pmtab->p_status != GETTY) {
		abort();
	}

	if (pmtab->p_termtype != (char *)NULL) {
		(void) snprintf(tbuf, sizeof (tbuf),
		    "TERM=%s", pmtab->p_termtype);
		if (putenv(tbuf)) {
			fatal("cannot expand service <%s> environment",
			    argvp[0]);
		}
	}
	/* restore signal handlers and mask */
	(void) sigaction(SIGINT, &Sigint, NULL);
	(void) sigaction(SIGALRM, &Sigalrm, NULL);
	(void) sigaction(SIGPOLL, &Sigpoll, NULL);
	(void) sigaction(SIGQUIT, &Sigquit, NULL);
	(void) sigaction(SIGCLD, &Sigcld, NULL);
	(void) sigaction(SIGTERM, &Sigterm, NULL);

	(void) sigprocmask(SIG_SETMASK, &Origmask, NULL);
	log_argv("exec'ing: ", argvp);
	(void) execve(argvp[0], argvp, environ);

	/* exec returns only on failure! */
	fatal("tmchild: exec service failed: %s", strerror(errno));
}
