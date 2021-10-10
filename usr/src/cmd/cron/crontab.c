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
 * Copyright (c) 1988, 2010, Oracle and/or its affiliates. All rights reserved.
 */
/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/


#include <sys/types.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <pwd.h>
#include <unistd.h>
#include <locale.h>
#include <nl_types.h>
#include <langinfo.h>
#include <libintl.h>
#include <security/pam_appl.h>
#include <limits.h>
#include <libzoneinfo.h>
#include "cron.h"
#include "getresponse.h"

#if defined(XPG4)
#define	VIPATH	"/usr/xpg4/bin/vi"
#elif defined(XPG6)
#define	VIPATH	"/usr/xpg6/bin/vi"
#else
#define	_XPG_NOTDEFINED
#define	VIPATH	"vi"
#endif

#define	TMPFILE		"_cron"		/* prefix for tmp file */
#define	CRMODE		0600	/* mode for creating crontabs */

#define	BADCREATE	\
	"can't create your crontab file in the crontab directory."
#define	BADOPEN		"can't open your crontab file."
#define	BADSHELL	\
	"because your login shell isn't /usr/bin/sh, you can't use cron."
#define	WARNSHELL	"warning: commands will be executed using /usr/bin/sh\n"
#define	BADUSAGE	\
	"usage:\n"			\
	"\tcrontab [file]\n"		\
	"\tcrontab -e [username]\n"	\
	"\tcrontab -l [username]\n"	\
	"\tcrontab -r [username]"
#define	INVALIDUSER	"you are not a valid user (no entry in /etc/passwd)."
#define	NOTALLOWED	"you are not authorized to use cron.  Sorry."
#define	NOTROOT		\
	"you must be super-user to access another user's crontab file"
#define	AUDITREJECT	"The audit context for your shell has not been set."
#define	EOLN		"unexpected end of line."
#define	UNEXPECT	"unexpected character found in line."
#define	OUTOFBOUND	"number out of bounds."
#define	ERRSFND		"errors detected in input, no crontab file generated."
#define	ED_ERROR	\
	"     The editor indicates that an error occurred while you were\n"\
	"     editing the crontab data - usually a minor typing error.\n\n"
#define	BADREAD		"error reading your crontab file"
#define	ED_PROMPT	\
	"     Edit again, to ensure crontab information is intact (%s/%s)?\n"\
	"     ('%s' will discard edits.)"
#define	NAMETOOLONG	"login name too long"
#define	BAD_TZ	"Timezone unrecognized in: %s"
#define	BAD_SHELL	"Invalid shell specified: %s"
#define	BAD_HOME	"Unable to access directory: %s\t%s\n"

extern int	per_errno;

extern int	audit_crontab_modify(char *, char *, int);
extern int	audit_crontab_delete(char *, int);
extern int	audit_crontab_not_allowed(uid_t, char *);

int		err;
int		cursor;
char		*cf;
char		*tnam;
char		edtemp[5+13+1];
char		line[CTLINESIZE];
static		char	login[UNAMESIZE];

static int	next_field(int, int);
static void	catch(int);
static void	crabort(char *);
static void	cerror(char *);
static void	copycron(FILE *);

int
main(int argc, char **argv)
{
	int	c, r;
	int	rflag	= 0;
	int	lflag	= 0;
	int	eflag	= 0;
	int	errflg	= 0;
	char *pp;
	FILE *fp, *tmpfp;
	struct stat stbuf;
	struct passwd *pwp;
	time_t omodtime;
	char *editor;
	uid_t ruid;
	pid_t pid;
	int stat_loc;
	int ret;
	char real_login[UNAMESIZE];
	int tmpfd = -1;
	pam_handle_t *pamh;
	int pam_error;
	char *buf;

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"	/* Use this only if it weren't */
#endif
	(void) textdomain(TEXT_DOMAIN);

	if (init_yes() < 0) {
		(void) fprintf(stderr, gettext(ERR_MSG_INIT_YES),
		    strerror(errno));
		exit(1);
	}

	while ((c = getopt(argc, argv, "elr")) != EOF)
		switch (c) {
			case 'e':
				eflag++;
				break;
			case 'l':
				lflag++;
				break;
			case 'r':
				rflag++;
				break;
			case '?':
				errflg++;
				break;
		}

	if (eflag + lflag + rflag > 1)
		errflg++;

	argc -= optind;
	argv += optind;
	if (errflg || argc > 1)
		crabort(BADUSAGE);

	ruid = getuid();
	if ((pwp = getpwuid(ruid)) == NULL)
		crabort(INVALIDUSER);

	if (strlcpy(real_login, pwp->pw_name, sizeof (real_login))
	    >= sizeof (real_login))
		crabort(NAMETOOLONG);

	if ((eflag || lflag || rflag) && argc == 1) {
		if ((pwp = getpwnam(*argv)) == NULL)
			crabort(INVALIDUSER);

		if (!cron_admin(real_login)) {
			if (pwp->pw_uid != ruid)
				crabort(NOTROOT);
			else
				pp = getuser(ruid);
		} else
			pp = *argv++;
	} else {
		pp = getuser(ruid);
	}

	if (pp == NULL) {
		if (per_errno == 2)
			crabort(BADSHELL);
		else
			crabort(INVALIDUSER);
	}
	if (strlcpy(login, pp, sizeof (login)) >= sizeof (login))
		crabort(NAMETOOLONG);
	if (!allowed(login, CRONALLOW, CRONDENY))
		crabort(NOTALLOWED);

	/* Do account validation check */
	pam_error = pam_start("cron", pp, NULL, &pamh);
	if (pam_error != PAM_SUCCESS) {
		crabort((char *)pam_strerror(pamh, pam_error));
	}
	pam_error = pam_acct_mgmt(pamh, PAM_SILENT);
	if (pam_error != PAM_SUCCESS) {
		(void) fprintf(stderr, gettext("Warning - Invalid account: "
		    "'%s' not allowed to execute cronjobs\n"), pp);
	}
	(void) pam_end(pamh, PAM_SUCCESS);


	/* check for unaudited shell */
	if (audit_crontab_not_allowed(ruid, pp))
		crabort(AUDITREJECT);

	(void) xasprintf(&cf, "%s/%s", CRONDIR, login);

	if (rflag) {
		r = unlink(cf);
		cron_sendmsg(DELETE, login, login, CRON);
		(void) audit_crontab_delete(cf, r);
		exit(0);
	}
	if (lflag) {
		if ((fp = fopen(cf, "r")) == NULL)
			crabort(BADOPEN);
		while (fgets(line, CTLINESIZE, fp) != NULL)
			(void) fputs(line, stdout);
		(void) fclose(fp);
		exit(0);
	}
	if (eflag) {
		if ((fp = fopen(cf, "r")) == NULL) {
			if (errno != ENOENT)
				crabort(BADOPEN);
		}
		(void) strcpy(edtemp, "/tmp/crontabXXXXXX");
		tmpfd = mkstemp(edtemp);
		if (fchown(tmpfd, ruid, -1) == -1) {
			(void) close(tmpfd);
			crabort("fchown of temporary file failed");
		}
		(void) close(tmpfd);
		/*
		 * Fork off a child with user's permissions,
		 * to edit the crontab file
		 */
		if ((pid = fork()) == (pid_t)-1)
			crabort("fork failed");
		if (pid == 0) {		/* child process */
			/* give up super-user privileges. */
			(void) setuid(ruid);
			if ((tmpfp = fopen(edtemp, "w")) == NULL)
				crabort("can't create temporary file");
			if (fp != NULL) {
				/*
				 * Copy user's crontab file to temporary file.
				 */
				while (fgets(line, CTLINESIZE, fp) != NULL) {
					(void) fputs(line, tmpfp);
					if (ferror(tmpfp)) {
						(void) fclose(fp);
						(void) fclose(tmpfp);
						crabort("write error on"
						    "temporary file");
					}
				}
				if (ferror(fp)) {
					(void) fclose(fp);
					(void) fclose(tmpfp);
					crabort(BADREAD);
				}
				(void) fclose(fp);
			}
			if (fclose(tmpfp) == EOF)
				crabort("write error on temporary file");
			if (stat(edtemp, &stbuf) < 0)
				crabort("can't stat temporary file");
			omodtime = stbuf.st_mtime;
#ifdef _XPG_NOTDEFINED
			editor = getenv("VISUAL");
			if (editor == NULL) {
#endif
				editor = getenv("EDITOR");
				if (editor == NULL)
					editor = VIPATH;
#ifdef _XPG_NOTDEFINED
			}
#endif
			(void) xasprintf(&buf, "%s %s", editor, edtemp);
			(void) sleep(1);

			for (;;) {
				ret = system(buf);

				/* sanity checks */
				if ((tmpfp = fopen(edtemp, "r")) == NULL)
					crabort("can't open temporary file");
				if (fstat(fileno(tmpfp), &stbuf) < 0)
					crabort("can't stat temporary file");
				if (stbuf.st_size == 0)
					crabort("temporary file empty");
				if (omodtime == stbuf.st_mtime) {
					(void) unlink(edtemp);
					(void) fprintf(stderr, gettext(
					    "The crontab file was not"
					    " changed.\n"));
					exit(1);
				}
				if ((ret) && (errno != EINTR)) {
					/*
					 * Some editors (like 'vi') can return
					 * a non-zero exit status even though
					 * everything is okay. Need to check.
					 */
					(void) fprintf(stderr,
					    gettext(ED_ERROR));
					(void) fflush(stderr);
					if (isatty(fileno(stdin))) {
						/* Interactive */
						(void) fprintf(stdout,
						    gettext(ED_PROMPT),
						    yesstr, nostr, nostr);
						(void) fflush(stdout);

						if (yes()) {
							/* Edit again */
							continue;
						} else {
							/* Dump changes */
							(void) unlink(edtemp);
							exit(1);
						}
					} else {
						/*
						 * Non-interactive, dump changes
						 */
						(void) unlink(edtemp);
						exit(1);
					}
				}
				exit(0);
			} /* while (1) */
		}

		/* fix for 1125555 - ignore common signals while waiting */
		(void) signal(SIGINT, SIG_IGN);
		(void) signal(SIGHUP, SIG_IGN);
		(void) signal(SIGQUIT, SIG_IGN);
		(void) signal(SIGTERM, SIG_IGN);
		(void) wait(&stat_loc);
		if ((stat_loc & 0xFF00) != 0)
			exit(1);

		/*
		 * unlink edtemp as 'ruid'. The file contents will be held
		 * since we open the file descriptor 'tmpfp' before calling
		 * unlink.
		 */
		if (((ret = seteuid(ruid)) < 0) ||
		    ((tmpfp = fopen(edtemp, "r")) == NULL) ||
		    (unlink(edtemp) == -1)) {
			(void) fprintf(stderr, "crontab: %s: %s\n",
			    edtemp, errmsg(errno));
			if ((ret < 0) || (tmpfp == NULL))
				(void) unlink(edtemp);
			exit(1);
		} else
			(void) seteuid(0);

		copycron(tmpfp);
	} else {
		if (argc == 0)
			copycron(stdin);
		else if (seteuid(getuid()) != 0 || (fp = fopen(argv[0], "r"))
		    == NULL)
			crabort(BADOPEN);
		else {
			(void) seteuid(0);
			copycron(fp);
		}
	}
	cron_sendmsg(ADD, login, login, CRON);
/*
 *	if (per_errno == 2)
 *		(void) fprintf(stderr, gettext(WARNSHELL));
 */
	return (0);
}

static void
copycron(fp)
FILE *fp;
{
	FILE *tfp;
	char pid[6], *tnam_end;
	int t;
	char buf[LINE_MAX];

	(void) sprintf(pid, "%-5ld", getpid());
	tnam = xmalloc(strlen(CRONDIR)+strlen(TMPFILE)+7);
	(void) strcat(strcat(strcat(strcpy(tnam, CRONDIR), "/"), TMPFILE), pid);
	/* cut trailing blanks */
	tnam_end = strchr(tnam, ' ');
	if (tnam_end != NULL)
		*tnam_end = 0;
	/* catch SIGINT, SIGHUP, SIGQUIT signals */
	if (signal(SIGINT, catch) == SIG_IGN)
		(void) signal(SIGINT, SIG_IGN);
	if (signal(SIGHUP, catch) == SIG_IGN)
		(void) signal(SIGHUP, SIG_IGN);
	if (signal(SIGQUIT, catch) == SIG_IGN)
		(void) signal(SIGQUIT, SIG_IGN);
	if (signal(SIGTERM, catch) == SIG_IGN)
		(void) signal(SIGTERM, SIG_IGN);
	if ((t = creat(tnam, CRMODE)) == -1) crabort(BADCREATE);
	if ((tfp = fdopen(t, "w")) == NULL) {
		(void) unlink(tnam);
		crabort(BADCREATE);
	}
	err = 0;	/* if errors found, err set to 1 */
	while (fgets(line, CTLINESIZE, fp) != NULL) {
		cursor = 0;
		while (line[cursor] == ' ' || line[cursor] == '\t')
			cursor++;
		/* fix for 1039689 - treat blank line like a comment */
		if (line[cursor] == '#' || line[cursor] == '\n')
			goto cont;

		if (strncmp(&line[cursor], ENV_TZ, strlen(ENV_TZ)) == 0) {
			char *x;

			(void) strncpy(buf, &line[cursor + strlen(ENV_TZ)],
			    sizeof (buf));
			if ((x = strchr(buf, '\n')) != NULL)
				*x = NULL;

			if (isvalid_tz(buf, NULL, _VTZ_ALL)) {
				goto cont;
			} else {
				err = 1;
				(void) fprintf(stderr, BAD_TZ, &line[cursor]);
				continue;
			}
		} else if (strncmp(&line[cursor], ENV_SHELL,
		    strlen(ENV_SHELL)) == 0) {
			char *x;

			(void) strncpy(buf, &line[cursor + strlen(ENV_SHELL)],
			    sizeof (buf));
			if ((x = strchr(buf, '\n')) != NULL)
				*x = NULL;

			if (isvalid_shell(buf)) {
				goto cont;
			} else {
				err = 1;
				(void) fprintf(stderr,
				    BAD_SHELL, &line[cursor]);
				continue;
			}
		} else if (strncmp(&line[cursor], ENV_HOME,
		    strlen(ENV_HOME)) == 0) {
			char *x;

			(void) strncpy(buf, &line[cursor + strlen(ENV_HOME)],
			    sizeof (buf));
			if ((x = strchr(buf, '\n')) != NULL)
				*x = NULL;
			if (chdir(buf) == 0) {
				goto cont;
			} else {
				err = 1;
				(void) fprintf(stderr, BAD_HOME, &line[cursor],
				    strerror(errno));
				continue;
			}
		}

		if (next_field(0, 59)) continue;
		if (next_field(0, 23)) continue;
		if (next_field(1, 31)) continue;
		if (next_field(1, 12)) continue;
		if (next_field(0, 06)) continue;
		if (line[++cursor] == '\0') {
			cerror(EOLN);
			continue;
		}
cont:
		if (fputs(line, tfp) == EOF) {
			(void) unlink(tnam);
			crabort(BADCREATE);
		}
	}
	(void) fclose(fp);
	(void) fclose(tfp);

	/* audit differences between old and new crontabs */
	(void) audit_crontab_modify(cf, tnam, err);

	if (!err) {
		/* make file tfp the new crontab */
		(void) unlink(cf);
		if (link(tnam, cf) == -1) {
			(void) unlink(tnam);
			crabort(BADCREATE);
		}
	} else {
		crabort(ERRSFND);
	}
	(void) unlink(tnam);
}

static int
next_field(lower, upper)
int lower, upper;
{
	int num, num2;

	while ((line[cursor] == ' ') || (line[cursor] == '\t')) cursor++;
	if (line[cursor] == '\0') {
		cerror(EOLN);
		return (1);
	}
	if (line[cursor] == '*') {
		cursor++;
		if ((line[cursor] != ' ') && (line[cursor] != '\t')) {
			cerror(UNEXPECT);
			return (1);
		}
		return (0);
	}
	for (;;) {
		if (!isdigit(line[cursor])) {
			cerror(UNEXPECT);
			return (1);
		}
		num = 0;
		do {
			num = num*10 + (line[cursor]-'0');
		} while (isdigit(line[++cursor]));
		if ((num < lower) || (num > upper)) {
			cerror(OUTOFBOUND);
			return (1);
		}
		if (line[cursor] == '-') {
			if (!isdigit(line[++cursor])) {
				cerror(UNEXPECT);
				return (1);
			}
			num2 = 0;
			do {
				num2 = num2*10 + (line[cursor]-'0');
			} while (isdigit(line[++cursor]));
			if ((num2 < lower) || (num2 > upper)) {
				cerror(OUTOFBOUND);
				return (1);
			}
		}
		if ((line[cursor] == ' ') || (line[cursor] == '\t')) break;
		if (line[cursor] == '\0') {
			cerror(EOLN);
			return (1);
		}
		if (line[cursor++] != ',') {
			cerror(UNEXPECT);
			return (1);
		}
	}
	return (0);
}

static void
cerror(msg)
char *msg;
{
	(void) fprintf(stderr,
	    gettext("%scrontab: error on previous line; %s\n"), line, msg);
	err = 1;
}


/*ARGSUSED*/
static void
catch(int x)
{
	(void) unlink(tnam);
	exit(1);
}

static void
crabort(msg)
char *msg;
{
	int sverrno;

	if (strcmp(edtemp, "") != 0) {
		sverrno = errno;
		(void) unlink(edtemp);
		errno = sverrno;
	}
	if (tnam != NULL) {
		sverrno = errno;
		(void) unlink(tnam);
		errno = sverrno;
	}
	(void) fprintf(stderr, "crontab: %s\n", gettext(msg));
	exit(1);
}
