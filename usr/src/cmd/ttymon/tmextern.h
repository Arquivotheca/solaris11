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

#ifndef	_TMEXTERN_H
#define	_TMEXTERN_H

#include "tmstruct.h"
#include <poll.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef	__cplusplus
extern "C" {
#endif

	extern	void	setup_PCpipe(void);

/* tmautobaud.c	*/
	extern	int	auto_termio(int);
	extern	char	*autobaud(int, int);

/* tmchild.c 	*/
	extern	void	write_prompt(int, struct pmtab *, int, int);
	extern 	void 	timedout(void);

/* tmexpress.c 	*/
	extern	void	ttymon_express(int, char **);

/* tmhandler.c 	*/
	extern	void	do_poll(struct pollfd *, int);
	extern 	void 	sigterm(void);
	extern 	void 	sigchild(int);
	extern 	void	state_change(void);
	extern 	void	re_read(void);
	extern 	void	got_carrier(struct pmtab *);

/* tmlock.c 	*/
	extern	int	tm_checklock(int);
	extern	int	tm_lock(int);

/* tmlog.c 	*/
	extern 	void 	log(const char *, ...);
	extern 	void 	fatal(const char *, ...);
	extern	void	verbose(const char *, ...);
	extern	void	log_argv(const char *, char **);
	extern	void	debug(const char *, ...);
	extern	void	opendebug();
	extern	void	dump_ttydefs();

	extern	int	open_filelog_stderr(void);
	extern	int	open_device_log(int);

/* tmparse.c 	*/
	extern	char	*getword(char *, int *, int);
	extern	char	quoted(char *, int *);

/* tmpeek.c 	*/
	extern	int	poll_data(void);

/* tmpmtab.c 	*/
	extern	void	read_pmtab(void);
	extern	void	purge(void);

/* tmsac.c 	*/
	extern 	void	openpid(void);
	extern 	void	openpipes(void);
	extern 	void	get_environ(void);
	extern	void	sacpoll(void);

/* tmsig.c 	*/
	extern 	void catch_signals(void);
	extern 	void child_sigcatch(void);

/* tmterm.c 	*/
	extern  int	push_linedisc(int, char *, char *);
	extern	int	set_termio(int, char *, char *, int, long);
	extern	int	initial_termio(int, struct pmtab *);
	extern	int	hang_up_line(int);
	extern	void 	flush_input(int);

/* tmttydefs.c 	*/
	extern	void	read_ttydefs(const char *, int);
	extern 	struct 	Gdef *find_def(char *);
	extern	void	mkargv(char *, char **, int *, int);

/* tmutmp.c 	*/
	extern 	int 	account(char *);
	extern 	void 	cleanut(pid_t, int);

/* tmutil.c 	*/
	extern	int	check_device(char *);
	extern	int	check_cmd(char *);
	extern	void	cons_printf(const char *, ...);

/* misc sys call or lib function call */
	extern	int	check_version();

/* tmglobal.c 	*/
	extern	struct	pmtab	*PMtab;
	extern	int	Nentries;

	extern	struct 	Gdef Gdef[];
	extern	int	Ndefs;

	extern	int	Lckfd;

	extern	char	*Tag;
	extern	int	Reread_flag;

	extern	char	**environ;
	extern	char	*optarg;
	extern	int	optind, opterr;

	extern	int	Nlocked;

	extern	sigset_t	Origmask;
	extern	struct	sigaction	Sigalrm;	/* SIGALRM */
	extern	struct	sigaction	Sigcld;		/* SIGCLD */
	extern	struct	sigaction	Sigint;		/* SIGINT */
	extern	struct	sigaction	Sigpoll;	/* SIGPOLL */
	extern	struct	sigaction	Sigquit;	/* SIGQUIT */
	extern	struct	sigaction	Sigterm;	/* SIGTERM */

	extern	int	Verbose;
	extern  FILE	*DeviceLogfp;
	extern  FILE	*FileLogfp;

	extern	uid_t	Uucp_uid;
	extern	gid_t	Tty_gid;
	extern	struct	strbuf *peek_ptr;

	extern	int	Logmaxsz;
	extern	int	Splflag;

#ifdef	__cplusplus
}
#endif

#endif	/* _TMEXTERN_H */
