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

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>
#include <ulimit.h>
#include <unistd.h>
#include <locale.h>
#include <libintl.h>
#include <tzfile.h>
#include <project.h>

#include "cron.h"

#define	TMPFILE		"_at" /* prefix for temporary files	*/
/*
 * Mode for creating files in ATDIR.
 * Setuid bit on so that if an owner of a file gives that file
 * away to someone else, the setuid bit will no longer be set.
 * If this happens, atrun will not execute the file
 */
#define	ATMODE		(S_ISUID | S_IRUSR | S_IRGRP | S_IROTH)
#define	ROOT		0	/* user-id of super-user */
#define	MAXTRYS		100	/* max trys to create at job file */

#define	BADTIME		"bad time specification"
#define	BADQUEUE	"queue name must be a single character a-z"
#define	NOTCQUEUE	"queue c is reserved for cron entries"
#define	BADSHELL	"because your login shell isn't /usr/bin/sh,"\
			"you can't use at"
#define	WARNSHELL	"commands will be executed using %s\n"
#define	CANTCD		"can't change directory to the at directory"
#define	CANTCHOWN	"can't change the owner of your job to you"
#define	CANTCREATE	"can't create a job for you"
#define	INVALIDUSER	"you are not a valid user (no entry in /etc/passwd)"
#define	NOOPENDIR	"can't open the at directory"
#define	NOTALLOWED	"you are not authorized to use at.  Sorry."
#define	NAMETOOLONG	"login name too long"
#define	FILETOOLONG	"File name too long"
#define	USAGE\
	"usage: at [-c|-k|-s] [-m] [-f file] [-p project] [-q queuename] "\
	    "-t time\n"\
	"       at [-c|-k|-s] [-m] [-f file] [-p project] [-q queuename] "\
	    "timespec\n"\
	"       at -l [-p project] [-q queuename] [at_job_id...]\n"\
	"       at -r at_job_id ...\n"

#define	FORMAT		"%a %b %e %H:%M:%S %Y"

/* Macros used in format for fscanf */
#define	AQ(x)		#x
#define	BUFFMT(p)		AQ(p)

static int leap(int);
static int atoi_for2(char *);
static int check_queue(char *, int);
static int list_jobs(int, char **, int, int);
static int remove_jobs(int, char **, char *);
static void usage(void);
static void catch(int);
static void copy(char *, FILE *, int);
static void atime(struct tm *, struct tm *);
static int not_this_project(char *);
static char *mkjobname(time_t);
static time_t parse_time(char *);
static time_t gtime(struct tm *);
void atabort(char *)__NORETURN;
void yyerror(char *);
extern int yyparse(void);

extern int	audit_at_delete(char *, char *, int);
extern int	audit_at_create(char *, int);
extern int	audit_cron_is_anc_name(char *);
extern int	audit_cron_delete_anc_file(char *, char *);

/*
 * Error in getdate(3G)
 */
static char 	*errlist[] = {
/* 0 */ 	"",
/* 1 */	"getdate: The DATEMSK environment variable is not set",
/* 2 */	"getdate: Error on \"open\" of the template file",
/* 3 */	"getdate: Error on \"stat\" of the template file",
/* 4 */	"getdate: The template file is not a regular file",
/* 5 */	"getdate: An error is encountered while reading the template",
/* 6 */	"getdate: Malloc(3C) failed",
/* 7 */	"getdate: There is no line in the template that matches the input",
/* 8 */	"getdate: Invalid input specification"
};

#define	PNAMELEN	80

int		gmtflag = 0;
int		mday[12] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
uid_t		user;
struct	tm	*tp, at, rt;
static int	cshflag		= 0;
static int	kshflag		= 0;
static int	shflag		= 0;
static int	mflag		= 0;
static int	pflag		= 0;
static char	*Shell;
static char	*tfname;
static char	pname[PNAMELEN];
static char	pname1[PNAMELEN];
static short	jobtype = ATEVENT;	/* set to 1 if batch job */
extern char	*argp;
extern int	per_errno;
static projid_t	project;

int
main(int argc, char **argv)
{
	FILE		*inputfile;
	int		i, fd;
	int		try = 0;
	int		fflag = 0;
	int		lflag = 0;
	int		qflag = 0;
	int		rflag = 0;
	int		tflag = 0;
	int		c;
	char		*file;
	char		*login;
	char		*job;
	char		*jobfile = NULL; /* file containing job to be run */
	char		argpbuf[LINE_MAX], timebuf[80];
	time_t		now;
	time_t		when = 0;
	struct	tm	*ct;
	char		*proj;
	struct project	prj, *pprj;
	char		mybuf[PROJECT_BUFSZ];
	char		ipbuf[PROJECT_BUFSZ];

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"	/* Use this only if it weren't */
#endif
	(void) textdomain(TEXT_DOMAIN);

	user = getuid();
	login = getuser(user);
	if (login == NULL) {
		if (per_errno == 2)
			atabort(BADSHELL);
		else
			atabort(INVALIDUSER);
	}

	if (!allowed(login, ATALLOW, ATDENY))
		atabort(NOTALLOWED);

	while ((c = getopt(argc, argv, "cklmsrf:p:q:t:")) != EOF)
		switch (c) {
			case 'c':
				cshflag++;
				break;
			case 'f':
				fflag++;
				jobfile = optarg;
				break;
			case 'k':
				kshflag++;
				break;
			case 'l':
				lflag++;
				break;
			case 'm':
				mflag++;
				break;
			case 'p':
				proj = optarg;
				pprj = &prj;
				if ((pprj = getprojbyname(proj, pprj,
				    (void *)&mybuf, sizeof (mybuf))) != NULL) {
					project = pprj->pj_projid;
					if (inproj(login, pprj->pj_name,
					    (void *)&ipbuf, sizeof (ipbuf)))
						pflag++;
					else {
						(void) fprintf(stderr,
						    gettext("at: user %s is "
						    "not a member of "
						    "project %s (%d)\n"),
						    login, pprj->pj_name,
						    project);
						exit(2);
					}
					break;
				}
				pprj = &prj;
				if (isdigit(proj[0]) &&
				    (pprj = getprojbyid(atoi(proj), pprj,
				    (void *)&mybuf, sizeof (mybuf))) != NULL) {
					project = pprj->pj_projid;
					if (inproj(login, pprj->pj_name,
					    (void *)&ipbuf, sizeof (ipbuf)))
						pflag++;
					else {
						(void) fprintf(stderr,
						    gettext("at: user %s is "
						    "not a member of "
						    "project %s (%d)\n"),
						    login, pprj->pj_name,
						    project);
						exit(2);
					}
					break;
				}
				(void) fprintf(stderr, gettext("at: project "
				    "%s not found.\n"), proj);
				exit(2);
				break;
			case 'q':
				qflag++;
				if (optarg[1] != '\0')
					atabort(BADQUEUE);
				jobtype = *optarg - 'a';
				if ((jobtype < 0) || (jobtype > 25))
					atabort(BADQUEUE);
				if (jobtype == 2)
					atabort(NOTCQUEUE);
				break;
			case 'r':
				rflag++;
				break;
			case 's':
				shflag++;
				break;
			case 't':
				tflag++;
				when = parse_time(optarg);
				break;
			default:
				usage();
		}

	argc -= optind;
	argv += optind;

	if (lflag + rflag > 1)
		usage();

	if (lflag) {
		if (cshflag || kshflag || shflag || mflag ||
		    fflag || tflag || rflag)
			usage();
		return (list_jobs(argc, argv, qflag, jobtype));
	}

	if (rflag) {
		if (cshflag || kshflag || shflag || mflag ||
		    fflag || tflag || qflag)
			usage();
		return (remove_jobs(argc, argv, login));
	}

	if ((argc + tflag == 0) && (jobtype != BATCHEVENT))
		usage();

	if (cshflag + kshflag + shflag > 1)
		atabort("ambiguous shell request");

	(void) time(&now);

	if (jobtype == BATCHEVENT)
		when = now;

	if (when == 0) { /* figure out what time to run the job */
		int	argplen = sizeof (argpbuf) - 1;

		argpbuf[0] = '\0';
		argp = argpbuf;
		i = 0;
		while (i < argc) {
			/* guard against buffer overflow */
			argplen -= strlen(argv[i]) + 1;
			if (argplen < 0)
				atabort(BADTIME);

			(void) strcat(argp, argv[i]);
			(void) strcat(argp, " ");
			i++;
		}
		if ((file = getenv("DATEMSK")) == 0 || file[0] == '\0') {
			tp = localtime(&now);
			/*
			 * Fix for 1047182 - we have to let yyparse
			 * check bounds on mday[] first, then fixup
			 * the leap year case.
			 */
			(void) yyparse();

			mday[1] = 28 + leap(at.tm_year);

			if (at.tm_mday > mday[at.tm_mon])
				atabort("bad date");

			atime(&at, &rt);
			when = gtime(&at);
			if (!gmtflag) {
				when += timezone;
				if (localtime(&when)->tm_isdst)
					when -= (timezone-altzone);
			}
		} else {	/*   DATEMSK is set  */
			if ((ct = getdate(argpbuf)) == NULL)
				atabort(errlist[getdate_err]);
			else
				when = mktime(ct);
		}
	}

	if (when < now)	/* time has already past */
		atabort("too late");

	(void) xasprintf(&tfname, "%s/%s%d", ATDIR, TMPFILE, getpid());

	/* catch INT, HUP, TERM and QUIT signals */
	if (signal(SIGINT, catch) == SIG_IGN)
		(void) signal(SIGINT, SIG_IGN);
	if (signal(SIGHUP, catch) == SIG_IGN)
		(void) signal(SIGHUP, SIG_IGN);
	if (signal(SIGQUIT, catch) == SIG_IGN)
		(void) signal(SIGQUIT, SIG_IGN);
	if (signal(SIGTERM, catch) == SIG_IGN)
		(void) signal(SIGTERM, SIG_IGN);
	if ((fd = open(tfname, O_CREAT|O_EXCL|O_WRONLY, ATMODE)) < 0)
		atabort(CANTCREATE);
	if (chown(tfname, user, getgid()) == -1) {
		(void) unlink(tfname);
		atabort(CANTCHOWN);
	}
	(void) close(1);
	(void) dup(fd);
	(void) close(fd);
	(void) snprintf(pname, sizeof (pname), "%s", PROTO);
	(void) snprintf(pname1, sizeof (pname1), "%s.%c", PROTO, 'a'+jobtype);

	/*
	 * Open the input file with the user's permissions.
	 */
	if (jobfile != NULL) {
		if ((seteuid(user) < 0) ||
		    (inputfile = fopen(jobfile, "r")) == NULL) {
			(void) unlink(tfname);
			(void) fprintf(stderr, "at: %s: %s\n",
			    jobfile, errmsg(errno));
			exit(1);
		}
		else
			(void) seteuid(0);
	} else
		inputfile = stdin;

	copy(jobfile, inputfile, when);
	while (rename(tfname, job = mkjobname(when)) == -1) {
		(void) sleep(1);
		if (++try > MAXTRYS / 10) {
			(void) unlink(tfname);
			atabort(CANTCREATE);
		}
	}
	(void) unlink(tfname);
	if (audit_at_create(job, 0))
		atabort(CANTCREATE);

	cron_sendmsg(ADD, login, strrchr(job, '/')+1, AT);
	if (per_errno == 2)
		(void) fprintf(stderr, gettext(WARNSHELL), Shell);
	if (strftime(timebuf, sizeof (timebuf), FORMAT, localtime(&when)) == 0)
		atabort(gettext("at: strftime failed: buffer too small."));
	(void) fprintf(stderr, gettext("job %s at %s\n"),
	    strrchr(job, '/')+1, timebuf);
	if (when - MINUTE < HOUR)
		(void) fprintf(stderr, gettext(
		    "at: this job may not be executed at the proper time.\n"));
	return (0);
}


#define	JOBNAMELEN	200

static char *
mkjobname(t)
time_t t;
{
	int i, fd;
	char *name;

	for (i = 0; i < MAXTRYS; i++) {
		(void) xasprintf(&name, "%s/%ld.%c", ATDIR, t, 'a'+jobtype);
		/* fix for 1099183, 1116833 - create file here, avoid race */
		if ((fd = open(name, O_CREAT | O_EXCL, ATMODE)) > 0) {
			(void) close(fd);
			return (name);
		}
		t += 1;
	}
	atabort("queue full");
	/* NOTREACHED */
}


/*ARGSUSED*/
static void
catch(int x)
{
	(void) unlink(tfname);
	exit(1);
}


void
atabort(msg)
char *msg;
{
	(void) fprintf(stderr, "at: %s\n", gettext(msg));

	exit(1);
}

int
yywrap(void)
{
	return (1);
}

/*ARGSUSED*/
void
yyerror(char *msg)
{
	atabort(BADTIME);
}

/*
 * add time structures logically
 */
static void
atime(struct tm *a, struct tm *b)
{
	if ((a->tm_sec += b->tm_sec) >= 60) {
		b->tm_min += a->tm_sec / 60;
		a->tm_sec %= 60;
	}
	if ((a->tm_min += b->tm_min) >= 60) {
		b->tm_hour += a->tm_min / 60;
		a->tm_min %= 60;
	}
	if ((a->tm_hour += b->tm_hour) >= 24) {
		b->tm_mday += a->tm_hour / 24;
		a->tm_hour %= 24;
	}
	a->tm_year += b->tm_year;
	if ((a->tm_mon += b->tm_mon) >= 12) {
		a->tm_year += a->tm_mon / 12;
		a->tm_mon %= 12;
	}
	a->tm_mday += b->tm_mday;
	mday[1] = 28 + leap(a->tm_year);
	while (a->tm_mday > mday[a->tm_mon]) {
		a->tm_mday -= mday[a->tm_mon++];
		if (a->tm_mon > 11) {
			a->tm_mon = 0;
			mday[1] = 28 + leap(++a->tm_year);
		}
	}

}

static int
leap(int year)
{
	return (isleap(year + TM_YEAR_BASE));
}

/*
 * return time from time structure
 */
static time_t
gtime(tptr)
struct	tm *tptr;
{
	int i;
	long	tv;
	int	dmsize[12] =
	    {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};


	tv = 0;
	for (i = 1970; i != tptr->tm_year+TM_YEAR_BASE; i++)
		tv += (365 + isleap(i));
		/*
		 * We call isleap since leap() adds
		 * 1900 onto any value passed
		 */

	if (!leap(tptr->tm_year) && at.tm_mday == 29 && at.tm_mon == 1)
		atabort("bad date - not a leap year");

	if ((leap(tptr->tm_year)) && tptr->tm_mon >= 2)
		++tv;

	for (i = 0; i < tptr->tm_mon; ++i)
		tv += dmsize[i];
	tv += tptr->tm_mday - 1;
	tv = 24 * tv + tptr->tm_hour;
	tv = 60 * tv + tptr->tm_min;
	tv = 60 * tv + tptr->tm_sec;
	return (tv);
}

/*
 * make job file from proto + stdin
 */
static void
copy(char *jobfile, FILE *inputfile, int when)
{
	int c;
	FILE *pfp;
	FILE *xfp;
	char *shell;
	char	dirbuf[PATH_MAX + 1];
	char	line[LINE_MAX];
	char **ep;
	mode_t um;
	char *val;
	extern char **environ;
	int pfd[2];
	pid_t pid;
	uid_t realusr;
	int ttyinput;
	int ulimit_flag = 0;
	struct rlimit rlp;
	struct project prj, *pprj;
	char pbuf[PROJECT_BUFSZ];
	char pbuf2[PROJECT_BUFSZ];
	char *user;

	/*
	 * Fix for 1099381:
	 * If the inputfile is from a tty, then turn on prompting, and
	 * put out a prompt now, instead of waiting for a lot of file
	 * activity to complete.
	 */
	ttyinput = isatty(fileno(inputfile));
	if (ttyinput) {
		(void) fputs("at> ", stderr);
		(void) fflush(stderr);
	}

	/*
	 * Fix for 1053807:
	 * Determine what shell we should use to run the job. If the user
	 * didn't explicitly request that his/her current shell be over-
	 * ridden (shflag or cshflag), then we use the current shell.
	 */
	if (cshflag)
		Shell = shell = "/bin/csh";
	else if (kshflag) {
		Shell = shell = "/bin/ksh";
		ulimit_flag = 1;
	} else if (shflag) {
		Shell = shell = "/bin/sh";
		ulimit_flag = 1;
	} else if (((Shell = val = getenv("SHELL")) != NULL) &&
	    (*val != '\0')) {
		shell = "$SHELL";
		if ((strstr(val, "/sh") != NULL) ||
		    (strstr(val, "/ksh") != NULL))
			ulimit_flag = 1;
	} else {
		/* SHELL is NULL or unset, therefore use default */
#ifdef XPG4
		Shell = shell = "/usr/xpg4/bin/sh";
#else
		Shell = shell = "/bin/sh";
#endif /* XPG4 */
		ulimit_flag = 1;
	}

	(void) printf(": %s job\n", jobtype ? "batch" : "at");
	(void) printf(": jobname: %.127s\n",
	    (jobfile == NULL) ? "stdin" : jobfile);
	(void) printf(": notify by mail: %s\n", (mflag) ? "yes" : "no");

	if (pflag) {
		(void) printf(": project: %ld\n", project);
	} else {
		/*
		 * Check if current user is a member of current project.
		 * This check is done here to avoid setproject() failure
		 * later when the job gets executed.  If current user does
		 * not belong to current project, user's default project
		 * will be used instead.  This is achieved by not specifying
		 * the project (": project: <project>\n") in the job file.
		 */
		if ((user = getuser(getuid())) == NULL)
			atabort(INVALIDUSER);
		project = getprojid();
		pprj = getprojbyid(project, &prj, pbuf, sizeof (pbuf));
		if (pprj != NULL) {
			if (inproj(user, pprj->pj_name, pbuf2, sizeof (pbuf2)))
				(void) printf(": project: %ld\n", project);
		}
	}

	for (ep = environ; *ep; ep++) {
		if (strchr(*ep, '\'') != NULL)
			continue;
		if ((val = strchr(*ep, '=')) == NULL)
			continue;
		*val++ = '\0';
		(void) printf("export %s; %s='%s'\n", *ep, *ep, val);
		*--val = '=';
	}
	if ((pfp = fopen(pname1, "r")) == NULL &&
	    (pfp = fopen(pname, "r")) == NULL)
		atabort("no prototype");
	/*
	 * Put in a line to run the proper shell using the rest of
	 * the file as input.  Note that 'exec'ing the shell will
	 * cause sh() to leave a /tmp/sh### file around. (1053807)
	 */
	(void) printf("%s << '...the rest of this file is shell input'\n",
	    shell);

	um = umask(0);
	while ((c = getc(pfp)) != EOF) {
		if (c != '$')
			(void) putchar(c);
		else switch (c =  getc(pfp)) {
		case EOF:
			goto out;
		case 'd':
			/*
			 * fork off a child with submitter's permissions,
			 * otherwise, when IFS=/, /usr/bin/pwd would be parsed
			 * by the shell as file "bin".  The shell would
			 * then search according to the submitter's PATH
			 * and run the file bin with root permission
			 */

			(void) fflush(stdout);
			dirbuf[0] = NULL;
			if (pipe(pfd) != 0)
				atabort("pipe open failed");
			realusr = getuid(); /* get realusr before the fork */
			if ((pid = fork()) == (pid_t)-1)
				atabort("fork failed");
			if (pid == 0) {			/* child process */
				(void) close(pfd[0]);
				/* remove setuid for pwd */
				(void) setuid(realusr);
				if ((xfp = popen("/usr/bin/pwd", "r"))
				    != NULL) {
					(void) fscanf(xfp,
					    "%" BUFFMT(PATH_MAX) "s", dirbuf);
					(void) pclose(xfp);
					xfp = fdopen(pfd[1], "w");
					(void) fprintf(xfp, "%s", dirbuf);
					(void) fclose(xfp);
				}
				_exit(0);
			}
			(void) close(pfd[1]);		/* parent process */
			xfp = fdopen(pfd[0], "r");
			(void) fscanf(xfp, "%" BUFFMT(PATH_MAX) "s", dirbuf);
			(void) printf("%s", dirbuf);
			(void) fclose(xfp);
			break;
		case 'm':
			(void) printf("%lo", um);
			break;
		case '<':
			if (ulimit_flag) {
				if (getrlimit(RLIMIT_FSIZE, &rlp) == 0) {
					if (rlp.rlim_cur == RLIM_INFINITY)
						(void) printf(
						    "ulimit unlimited\n");
					else
						(void) printf("ulimit %lld\n",
						    rlp.rlim_cur / 512);
				}
			}
			/*
			 * fix for 1113572 - use fputs() so that a
			 * newline isn't appended to the one returned
			 * with fgets(); 1099381 - prompt for input.
			 */
			while (fgets(line, LINE_MAX, inputfile) != NULL) {
				(void) fputs(line, stdout);
				if (ttyinput)
					(void) fputs("at> ", stderr);
			}
			if (ttyinput) /* clean up the final output */
				(void) fputs("<EOT>\n", stderr);
			break;
		case 't':
			(void) printf(":%lu", (long)when);
			break;
		default:
			(void) putchar(c);
		}
	}
out:
	(void) fclose(pfp);
	(void) fflush(NULL);
}

static int
remove_jobs(int argc, char **argv, char *login)
/* remove jobs that are specified */
{
	int		i, r;
	int		error = 0;
	struct stat	buf;
	struct passwd *pw;

	pw = getpwuid(user);
	if (pw == NULL) {
		atabort("Invalid user.\n");
	}

	if (argc == 0)
		usage();
	if (chdir(ATDIR) == -1)
		atabort(CANTCD);
	for (i = 0; i < argc; i++)
		if (strchr(argv[i], '/') != NULL) {
			(void) fprintf(stderr, "at: %s: not a valid job-id\n",
					argv[i]);
		} else if (stat(argv[i], &buf)) {
			(void) fprintf(stderr, "at: %s: ", argv[i]);
			perror("");
		} else if ((user != buf.st_uid) &&
		    (!cron_admin(pw->pw_name))) {
			(void) fprintf(stderr, "at: you don't own %s\n",
			    argv[i]);
			error = 1;
		} else {
			if (cron_admin(pw->pw_name)) {
				login = getuser((uid_t)buf.st_uid);
				if (login == NULL) {
					if (per_errno == 2)
						atabort(BADSHELL);
					else
						atabort(INVALIDUSER);
					}
			}

			if (strlen(login) >= UNAMESIZE)
				atabort(NAMETOOLONG);

			if (argv[i] && (strlen(argv[i]) >= FLEN))
				atabort(FILETOOLONG);

			cron_sendmsg(DELETE, login, argv[i], AT);
			r = unlink(argv[i]);
			(void) audit_at_delete(argv[i], ATDIR, r);
		}
	return (error);
}



static int
list_jobs(int argc, char **argv, int qflag, int queue)
{
	DIR		*dir;
	int		i;
	int		error = 0;
	char		*patdir, *atdir, *ptr;
	char		timebuf[80];
	time_t		t;
	struct stat	buf, st1, st2;
	struct dirent	*dentry;
	struct passwd	*pw;
	unsigned int	atdirlen;
	struct passwd	*pwd, pwds;
	char buf_pwd[1024];
	char job_file[PATH_MAX];

	pwd = getpwuid_r(user, &pwds, buf_pwd, sizeof (buf_pwd));
	if (pwd == NULL) {
		atabort("Invalid user.\n");
	}

	/* list jobs for user */
	if (chdir(ATDIR) == -1)
		atabort(CANTCD);

	atdirlen = strlen(ATDIR);
	atdir = xmalloc(atdirlen + 1);
	(void) strcpy(atdir, ATDIR);
	patdir = strrchr(atdir, '/');
	*patdir = '\0';
	if (argc == 0) {
		/* list all jobs for a user */
		if (stat(ATDIR, &st1) != 0 || stat(atdir, &st2) != 0)
			atabort("Can not get status of spooling"
			    "directory for at");
		if ((dir = opendir(ATDIR)) == NULL)
			atabort(NOOPENDIR);
		for (;;) {
			if ((dentry = readdir(dir)) == NULL)
				break;
			if ((dentry->d_ino == st1.st_ino) ||
			    (dentry->d_ino == st2.st_ino))
				continue;
			if (audit_cron_is_anc_name(dentry->d_name) == 1)
				continue;
			if (stat(dentry->d_name, &buf)) {
				(void) unlink(dentry->d_name);
				(void) audit_cron_delete_anc_file(
				    dentry->d_name, NULL);
				continue;
			}
			if ((!cron_admin(pwd->pw_name)) &&
			    (buf.st_uid != user))
				continue;
			ptr = dentry->d_name;
			if (((t = num(&ptr)) == 0) || (*ptr != '.'))
				continue;
			(void) strlcpy(job_file, patdir, PATH_MAX);
			(void) strlcat(job_file, dentry->d_name, PATH_MAX);
			if (pflag && not_this_project(job_file))
				continue;
			if (strftime(timebuf, sizeof (timebuf), FORMAT,
			    localtime(&t)) == 0)
				atabort(gettext(
				    "at: strftime failed: buffer too small."));
			if ((cron_admin(pwd->pw_name)) &&
			    ((pw = getpwuid(buf.st_uid)) != NULL)) {
				if (!qflag || (qflag &&
				    check_queue(ptr, queue)))
					(void) printf("user = %s\t%s\t%s\n",
					    pw->pw_name, dentry->d_name,
					    timebuf);
			} else
				if (!qflag || (qflag &&
				    check_queue(ptr, queue)))
					(void) printf("%s\t%s\n",
					    dentry->d_name, timebuf);
		}
		(void) closedir(dir);
	} else	/* list particular jobs for user */
		for (i = 0; i < argc; i++) {
			ptr = argv[i];
			(void) strlcpy(job_file, patdir, PATH_MAX);
			(void) strlcat(job_file, ptr, PATH_MAX);
			if (((t = num(&ptr)) == 0) || (*ptr != '.')) {
				(void) fprintf(stderr, gettext(
				    "at: invalid job name %s\n"), argv[i]);
				error = 1;
			} else if (stat(argv[i], &buf)) {
				(void) fprintf(stderr, "at: %s: ", argv[i]);
				perror("");
				error = 1;
			} else if ((user != buf.st_uid) &&
			    (!cron_admin(pwd->pw_name))) {
				(void) fprintf(stderr, gettext(
				    "at: you don't own %s\n"), argv[i]);
				error = 1;
			} else if (pflag && not_this_project(job_file)) {
				continue;
			} else {
				if (!qflag || (qflag &&
				    check_queue(ptr, queue))) {
					if (strftime(timebuf, sizeof (timebuf),
					    FORMAT, localtime(&t)) == 0)
						atabort(gettext(
						    "at: strftime failed: "
						    "buffer too small."));
					(void) printf("%s\t%s\n",
					    argv[i], timebuf);
				}
			}
		}
	return (error);
}

/*
 * open the command file and read the project id line
 * compare to the project number provided via -p on the command line
 * return 0 if they match, 1 if they don't match or an error occurs.
 */
#define	SKIPCOUNT 3	/* lines to skip to get to project line in file */

static int
not_this_project(char *filename)
{
	FILE *fp;
	projid_t sproj;
	int i;

	if ((fp = fopen(filename, "r")) == NULL)
		return (1);

	for (i = 0; i < SKIPCOUNT; i++)
		(void) fscanf(fp, "%*[^\n]\n");

	(void) fscanf(fp, ": project: %d\n", &sproj);
	(void) fclose(fp);

	return (sproj == project ? 0 : 1);
}

static int
check_queue(char *name, int queue)
{
	if ((name[strlen(name) - 1] - 'a') == queue)
		return (1);
	else
		return (0);
}

static time_t
parse_time(char *t)
{
	int		century = 0;
	int		seconds = 0;
	char		*p;
	time_t		when	= 0;
	struct tm	tm;

	/*
	 * time in the following format (defined by the touch(1) spec):
	 *	[[CC]YY]MMDDhhmm[.SS]
	 */
	if ((p = strchr(t, '.')) != NULL) {
		if (strchr(p+1, '.') != NULL)
			atabort(BADTIME);
		seconds = atoi_for2(p+1);
		*p = '\0';
	}

	(void) memset(&tm, 0, sizeof (struct tm));
	when = time(0);
	tm.tm_year = localtime(&when)->tm_year;

	switch (strlen(t)) {
		case 12:	/* CCYYMMDDhhmm */
			century = atoi_for2(t);
			t += 2;
			/*FALLTHROUGH*/
		case 10:	/* YYMMDDhhmm */
			tm.tm_year = atoi_for2(t);
			t += 2;
			if (century == 0) {
				if (tm.tm_year < 69)
					tm.tm_year += 100;
			} else
				tm.tm_year += (century - 19) * 100;
			/*FALLTHROUGH*/
		case 8:		/* MMDDhhmm */
			tm.tm_mon = atoi_for2(t) - 1;
			t += 2;
			tm.tm_mday = atoi_for2(t);
			t += 2;
			tm.tm_hour = atoi_for2(t);
			t += 2;
			tm.tm_min = atoi_for2(t);
			t += 2;
			tm.tm_sec = seconds;
			break;
		default:
			atabort(BADTIME);
	}

	if ((when = mktime(&tm)) == -1)
		atabort(BADTIME);
	if (tm.tm_isdst)
		when -= (timezone-altzone);
	return (when);
}

static int
atoi_for2(char *p) {
	int value;

	value = (*p - '0') * 10 + *(p+1) - '0';
	if ((value < 0) || (value > 99))
		atabort(BADTIME);
	return (value);
}

static void
usage(void)
{
	(void) fprintf(stderr, USAGE);
	exit(1);
}
