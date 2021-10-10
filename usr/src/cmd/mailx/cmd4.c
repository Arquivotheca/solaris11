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
 * Copyright (c) 1985, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/


/*
 * University Copyright- Copyright (c) 1982, 1986, 1988
 * The Regents of the University of California
 * All Rights Reserved
 *
 * University Acknowledgment- Portions of this document are derived from
 * software developed by the University of California, Berkeley, and its
 * contributors.
 */

#include "rcv.h"
#include <locale.h>

/*
 * mailx -- a modified version of a University of California at Berkeley
 *	mail program
 *
 * More commands..
 */

static char *stripquotes(char *str);

/*
 * pipe messages to cmd.
 */

int
dopipe(char str[])
{
	register int *ip, mesg;
	register struct message *mp;
	char *cp, *cmd;
	int f, *msgvec, nowait = 0;
	void (*sigint)(int), (*sigpipe)(int);
	long lc, cc, t;
	register pid_t pid;
	int page, s, pivec[2];
	char *Shell;
	FILE *pio = NULL;
	extern jmp_buf pipestop;
	extern void brokpipe(int);

	msgvec = (int *)salloc((msgCount + 2) * sizeof (*msgvec));
	if ((cmd = stripquotes(snarf(str, &f, 0))) == NOSTR) {
		if (f == -1) {
			(void) printf(gettext("pipe command error\n"));
			return (1);
		}
		if ((cmd = value("cmd")) == NOSTR) {
			(void) printf(gettext("\"cmd\" not set, ignored.\n"));
			return (1);
		}
	}
	if (!f) {
		*msgvec = first(0, MMNORM);
		if (*msgvec == NULL) {
			(void) printf(gettext("No messages to pipe.\n"));
			return (1);
		}
		msgvec[1] = NULL;
	}
	if (f && getmsglist(str, msgvec, 0) < 0)
		return (1);
	if (*(cp = cmd+strlen(cmd)-1) == '&') {
		*cp = 0;
		nowait++;
	}
	(void) printf(gettext("Pipe to: \"%s\"\n"), cmd);
	flush();

	if (setjmp(pipestop))
		goto err;
					/*  setup pipe */
	if (pipe(pivec) < 0) {
		perror("pipe");
		return (0);
	}

	if ((pid = vfork()) == 0) {
		(void) close(pivec[1]);	/* child */
		(void) close(0);
		(void) dup(pivec[0]);
		(void) close(pivec[0]);
		if ((Shell = value("SHELL")) == NOSTR || *Shell == '\0')
			Shell = SHELL;
		(void) execlp(Shell, Shell, "-c", cmd, 0);
		perror(Shell);
		_exit(1);
	}
	if (pid == (pid_t)-1) {		/* error */
		perror("fork");
		(void) close(pivec[0]);
		(void) close(pivec[1]);
		return (0);
	}

	(void) close(pivec[0]);		/* parent */
	pio = fdopen(pivec[1], "w");
	sigint = sigset(SIGINT, SIG_IGN);
	sigpipe = sigset(SIGPIPE, brokpipe);

					/* send all messages to cmd */
	page = (value("page") != NOSTR);
	lc = cc = 0;
	for (ip = msgvec; *ip && ip-msgvec < msgCount; ip++) {
		mesg = *ip;
		touch(mesg);
		mp = &message[mesg-1];
		dot = mp;
		if ((t = msend(mp, pio,
		    (value("alwaysignore") != NOSTR ||
		    value("pipeignore") != NOSTR)
		    ? M_IGNORE : 0, fputs)) < 0) {
			perror(cmd);
			(void) sigset(SIGPIPE, sigpipe);
			(void) sigset(SIGINT, sigint);
			(void) fclose(pio);
			return (1);
		}
		lc += t;
		cc += mp->m_size;
		if (page)
			(void) putc('\f', pio);
	}

	(void) fflush(pio);
	if (ferror(pio))
		perror(cmd);
	(void) fclose(pio);
	pio = NULL;

					/* wait */
	if (!nowait) {
		while (wait(&s) != pid)
			;
		s &= 0377;
		if (s != 0)
			goto err;
	}

	(void) printf("\"%s\" %ld/%ld\n", cmd, lc, cc);
	(void) sigset(SIGPIPE, sigpipe);
	(void) sigset(SIGINT, sigint);
	return (0);

err:
	(void) printf(gettext("Pipe to \"%s\" failed\n"), cmd);
	if (pio)
		(void) fclose(pio);
	(void) sigset(SIGPIPE, sigpipe);
	(void) sigset(SIGINT, sigint);
	return (0);
}

/*
 * Load the named message from the named file.
 */
int
loadmsg(char str[])
{
	char *file;
	int f, *msgvec;
	register int c, lastc = '\n';
	int blank;
	int lines;
	long ms;
	FILE *ibuf;
	struct message *mp;
	off_t size;

	msgvec = (int *)salloc((msgCount + 2) * sizeof (*msgvec));
	if ((file = snarf(str, &f, 1)) == NOSTR)
		return (1);
	if (f == -1)
		return (1);
	if (!f) {
		*msgvec = first(0, MMNORM);
		if (*msgvec == NULL) {
			(void) printf(gettext("No message to load into.\n"));
			return (1);
		}
		msgvec[1] = NULL;
	}
	if (f && getmsglist(str, msgvec, 0) < 0)
		return (1);
	if (msgvec[1] != NULL) {
		(void) printf(gettext(
		    "Can only load into a single message.\n"));
		return (1);
	}
	if ((file = expand(file)) == NOSTR)
		return (1);
	(void) printf("\"%s\" ", file);
	(void) fflush(stdout);
	if ((ibuf = fopen(file, "r")) == NULL) {
		perror("");
		return (1);
	}
	mp = &message[*msgvec-1];
	dot = mp;
	mp->m_flag |= MODIFY;
	mp->m_flag &= ~MSAVED;		/* should probably turn off more */
	(void) fseek(otf, (long)0, SEEK_END);
	size = fsize(otf);
	mp->m_offset = size;
	ms = 0L;
	lines = 0;
	while ((c = getc(ibuf)) != EOF) {
		if (c == '\n') {
			lines++;
			blank = lastc == '\n';
		}
		lastc = c;
		(void) putc(c, otf);
		if (ferror(otf))
			break;
		ms++;
	}
	if (!blank) {
		(void) putc('\n', otf);
		ms++;
		lines++;
	}
	mp->m_size = ms;
	mp->m_lines = lines;
	if (fferror(otf))
		perror("/tmp");
	(void) fclose(ibuf);
	setclen(mp);
	(void) printf(gettext("[Loaded] %d/%ld\n"), lines, ms);
	return (0);
}

/*
 * Display the named field.
 */
int
field(char str[])
{
	register int *ip;
	register struct message *mp;
	register char *cp, *fld;
	int f, *msgvec;

	msgvec = (int *)salloc((msgCount + 2) * sizeof (*msgvec));
	if ((fld = stripquotes(snarf(str, &f, 0))) == NOSTR) {
		if (f == -1)
			(void) printf(gettext("Bad field\n"));
		else
			(void) printf(gettext("No field specified\n"));
		return (1);
	}
	if (!f) {
		*msgvec = first(0, MMNORM);
		if (*msgvec == NULL) {
			(void) printf(gettext("No messages to display.\n"));
			return (1);
		}
		msgvec[1] = NULL;
	}
	if (f && getmsglist(str, msgvec, 0) < 0)
		return (1);

	for (ip = msgvec; *ip && ip-msgvec < msgCount; ip++) {
		mp = &message[*ip - 1];
		dot = mp;
		if ((cp = hfield(fld, mp, addone)) != NULL)
			(void) printf("%s\n", cp);
	}
	return (0);
}

/*
 *  Remove the quotes from around the string passed in (if any).  Return
 *  the beginning of the result.
 */

static char *
stripquotes(char *str)
{
	register int lastch;
	if (str == NOSTR) {
		return (NOSTR);
	}
	lastch = strlen(str)-1;
	if (any(*str, "\"'") && str[lastch] == *str) {
		str[lastch] = '\0';
		++str;
	}
	return (str);
}
