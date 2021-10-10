/*
 * Copyright (c) 1989, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/


/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

/*
 * Portions Copyright (c) 1988, Oracle and/or its affiliates. All
 * rights reserved.
 */

/*
 * script: Produce a record of a terminal session.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <locale.h>
#include <time.h>
#include <sys/stropts.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/termios.h>
#include <sys/file.h>
#include <errno.h>
#include <wait.h>

static void sigwinch(int);
static void finish(int);
static void fail(void);
static void fixtty(void);
static void dooutput(void);
static void doshell(void);
static void doinput(void) __NORETURN;
static void done(void) __NORETURN;
static void getslave(void);
static void getmaster(void);

char	*shell;
FILE	*fscript;
int	master;			/* file descriptor for master pseudo-tty */
int	slave;			/* file descriptor for slave pseudo-tty */
pid_t	child;
pid_t	subchild;
char	*fname = "typescript";

struct	termios b;
struct	winsize wsize;
char	*mptname = "/dev/ptmx";	/* master pseudo-tty device */

static int	aflg;
static int	syncpipe[2];
static int	ttyfd = -1, nowinsz;

int
main(int argc, char *argv[])
{
	uid_t ruidt;
	gid_t gidt;

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN	"SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	shell = getenv("SHELL");
	if (shell == NULL)
		shell = "/bin/sh";
	argc--, argv++;
	while (argc > 0 && argv[0][0] == '-') {
		switch (argv[0][1]) {

		case 'a':
			aflg++;
			break;

		default:
			(void) fprintf(stderr,
			    gettext("usage: script [ -a ] [ typescript ]\n"));
			exit(1);
		}
		argc--, argv++;
	}
	if (argc > 0)
		fname = argv[0];
	ruidt = getuid();
	gidt = getgid();
	if ((fscript = fopen(fname, aflg ? "a" : "w")) == NULL) {
		perror(fname);
		fail();
	}
	setbuf(fscript, NULL);
	(void) chown(fname, ruidt, gidt);

	getmaster();
	(void) printf(gettext("Script started, file is %s\n"), fname);
	(void) fflush(stdout);

	fixtty();

	/*
	 * create a pipe to synchronize with subchild (shell).
	 * doinput(parent) will wait at read() on the pipe and read() returns
	 * once subchild(shell) close the other side of the pipe. Subchild
	 * closes the pipe once the slave side of pty gets ready. Thus parent
	 * can safely supply the input to the master side.
	 */
	if (pipe(syncpipe) < 0) {
		perror("pipe");
		fail();
	}

	(void) signal(SIGCHLD, finish);
	child = fork();
	if (child < 0) {
		perror("fork");
		fail();
	}
	if (child == 0) {
		(void) close(syncpipe[0]);

		subchild = child = fork();
		if (child < 0) {
			perror("fork");
			fail();
		}
		if (child) {
			(void) close(syncpipe[1]);
			dooutput();
		} else {
			/* subchild */
			doshell();
		}
	}
	(void) close(syncpipe[1]);
	doinput();

	/* NOTREACHED */
	return (0);
}

static void
doinput(void)
{
	char ibuf[BUFSIZ];
	int cc;

	/* wait for the subchild (shell) to be ready */
	(void) read(syncpipe[0], ibuf, sizeof (ibuf));

	(void) fclose(fscript);
	(void) sigset(SIGWINCH, sigwinch);

	while ((cc = read(0, ibuf, BUFSIZ)) != 0) {
		if (cc == -1) {
			if (errno == EINTR) {   /* SIGWINCH probably */
				continue;
			} else {
				break;
			}
		}
		(void) write(master, ibuf, cc);
	}
	done();
}

/*ARGSUSED*/
static void
sigwinch(int sig)
{
	struct winsize ws;

	if (ttyfd < 0)
		return;
	if (ioctl(ttyfd, TIOCGWINSZ, &ws) == 0)
		(void) ioctl(master, TIOCSWINSZ, &ws);
}

/*ARGSUSED*/
static void
finish(int sig)
{
	int	status, die = 0;
	pid_t	pid;

	while ((pid = wait(&status)) > 0) {
		if (pid == child)
			die = 1;
	}
	if (die)
		done();
}

static void
dooutput(void)
{
	time_t tvec;
	char obuf[BUFSIZ];
	char tbuf[BUFSIZ];
	int cc;

	(void) close(0);
	tvec = time(NULL);
	(void) strftime(tbuf, BUFSIZ, "%c", localtime(&tvec));
	(void) fprintf(fscript, gettext("Script started on %s\n"), tbuf);
	for (;;) {
		cc = read(master, obuf, sizeof (obuf));
		if (cc <= 0)
			break;
		(void) write(1, obuf, cc);
		(void) fwrite(obuf, 1, cc, fscript);
	}
	done();
}

static void
doshell(void)
{
	(void) setpgrp();	/* relinquish control terminal */
	getslave();
	(void) close(master);
	(void) fclose(fscript);
	(void) dup2(slave, 0);
	(void) dup2(slave, 1);
	(void) dup2(slave, 2);
	(void) close(slave);

	/* slave side is ready. let the parent start supplying the input */
	(void) close(syncpipe[1]);

	(void) execl(shell, shell, "-i", NULL);
	perror(shell);
	fail();
}

static void
fixtty(void)
{
	struct termios sbuf;

	/* If stdin isn't a tty, not worth fixing tty mode */
	if (ttyfd != 0)
		return;
	sbuf = b;
	sbuf.c_iflag &= ~(INLCR|IGNCR|ICRNL|IUCLC|IXON);
	sbuf.c_oflag &= ~OPOST;
	sbuf.c_lflag &= ~(ICANON|ISIG|ECHO);
	sbuf.c_cc[VMIN] = 1;
	sbuf.c_cc[VTIME] = 0;
	(void) tcsetattr(0, TCSAFLUSH, &sbuf);
}

static void
fail(void)
{
	(void) kill(0, SIGTERM);
	done();
}

static void
done()
{
	time_t tvec;
	char tbuf[BUFSIZ];

	if (subchild) {
		tvec = time(NULL);
		(void) strftime(tbuf, BUFSIZ, "%c", localtime(&tvec));
		(void) fprintf(fscript, gettext("\nscript done on %s\n"), tbuf);
		(void) fclose(fscript);
		(void) close(master);
	} else {
		if (ttyfd == 0)
			(void) tcsetattr(0, TCSADRAIN, &b);
		(void) printf(gettext("Script done, file is %s\n"), fname);
	}
	exit(0);
}

static void
getmaster(void)
{
	int	fd;

	if ((master = open(mptname, O_RDWR)) >= 0) { /* a pseudo-tty is free */
		for (fd = 0; fd < 3; fd++) {
			if (tcgetattr(fd, &b) == 0) {
				ttyfd = fd;
				break;
			}
		}
		if (ttyfd != -1) {
			if (ioctl(ttyfd, TIOCGWINSZ, &wsize) < 0)
				nowinsz = 1;
		}
	} else {				/* out of pseudo-tty's */
		perror(mptname);
		(void) fprintf(stderr, gettext("Out of pseudo-tty's\n"));
		fail();
	}
}

static void
getslave(void)
{
	char *slavename;	/* name of slave pseudo-tty */

	if (grantpt(master) < 0 || 	/* change permissions of slave */
	    unlockpt(master) < 0 ||	/* unlock slave */
	    (slavename = ptsname(master)) == NULL) { /* get name of slave */
		(void) fprintf(stderr, gettext("failed to get pseudo-tty\n"));
		fail();
	}
	slave = open(slavename, O_RDWR);	/* open slave */
	if (slave < 0) {			/* error on opening slave */
		perror(slavename);
		fail();
	}
	/* push pt hw emulation module */
	(void) ioctl(slave, I_PUSH, "ptem");
	/* push line discipline */
	(void) ioctl(slave, I_PUSH, "ldterm");

	if (ttyfd != -1) {
		(void) tcsetattr(slave, TCSAFLUSH, &b);
		if (!nowinsz)
			(void) ioctl(slave, TIOCSWINSZ, &wsize);
	}
}
