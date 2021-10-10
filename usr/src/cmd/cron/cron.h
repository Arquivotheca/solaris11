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
 * Copyright (c) 1988, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_CRON_H
#define	_CRON_H

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

#include <sys/paths.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	FALSE		0
#define	TRUE		1
#define	MINUTE		60L
#define	HOUR		60L*60L
#define	DAY		24L*60L*60L
#define	NQUEUE		26		/* number of queues available */
#define	ATEVENT		0
#define	BATCHEVENT	1
#define	CRONEVENT	2

#define	ADD		'a'
#define	DELETE		'd'
#define	AT		'a'
#define	CRON		'c'
#define	REFRESH		'r'

#define	QUE(x)		('a'+(x))
#define	RCODE(x)	(((x)>>8)&0377)
#define	TSTAT(x)	((x)&0377)

#define	FLEN	20
#define	LLEN	20

/*
 * structure used for passing messages from the at and crontab commands to cron
 */
struct	message {
	char	etype;
	char	action;
	char	fname[FLEN];
	char	logname[LLEN];
};

/* anything below here can be changed */

#define	CRONDIR		"/var/spool/cron/crontabs"
#define	ATDIR		"/var/spool/cron/atjobs"
#define	ACCTFILE	"/var/cron/log"
#define	CRONALLOW	"/etc/cron.d/cron.allow"
#define	CRONDENY	"/etc/cron.d/cron.deny"
#define	ATALLOW		"/etc/cron.d/at.allow"
#define	ATDENY		"/etc/cron.d/at.deny"
#define	PROTO		"/etc/cron.d/.proto"
#define	QUEDEFS		"/etc/cron.d/queuedefs"
#define	FIFO		_PATH_SYSVOL "/cronfifo"
#define	DEFFILE		"/etc/default/cron"

#define	SHELL		"/usr/bin/sh"	/* shell to execute */

#define	ENV_SHELL	"SHELL="
#define	ENV_TZ		"TZ="
#define	ENV_HOME	"HOME="

#define	CTLINESIZE	1000	/* max chars in a crontab line */
#define	UNAMESIZE	20	/* max chars in a user name */

extern int	allowed(char *, char *, char *);
extern int	days_in_mon(int, int);
extern char	*errmsg(int);
extern char	*getuser(uid_t);
extern void	cron_sendmsg(char, char *, char *, char);
extern time_t	 num(char **);
extern int	xasprintf(char **, const char *, ...);
extern void	*xmalloc(size_t);
extern void	*xcalloc(size_t, size_t);
extern char	*xstrdup(const char *);
extern int	isvalid_shell(const char *shell);
extern int	isvalid_dir(const char *dir);

extern int	cron_admin(const char *);

#ifdef	__cplusplus
}
#endif

#endif	/* _CRON_H */
