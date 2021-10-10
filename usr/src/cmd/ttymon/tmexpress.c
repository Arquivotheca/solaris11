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
#include	<unistd.h>
#include	<fcntl.h>
#include	<errno.h>
#include	<ctype.h>
#include	<string.h>
#include	<signal.h>
#include	<sys/stat.h>
#include	<utmpx.h>
#include	<sys/acl.h>
#include	<sys/stat.h>
#include	<sys/types.h>
#include	<sys/console.h>
#include	"ttymon.h"
#include	"tmextern.h"
#include	"tmstruct.h"

static	char	*devname;
static 	int	dryrun_mode;

static	int	parse_args();
static	void	ttymon_options();
static	void	usage();
static	char	*find_ttyname();

extern	void	tmchild();
extern	int	vml();

void		revokedevaccess(char *, uid_t, gid_t, mode_t);
/* cannot include libdevinfo.h */
extern int di_devperm_logout(const char *);

/*
 * ttymon_express - This is call when ttymon is invoked with args
 *		    or invoked as getty
 *		  - This special version of ttymon will monitor
 *		    one port only
 *		  - It is intended to be used when some process
 *		    wants to have a login session on the fly
 */
void
ttymon_express(int argc, char **argv)
{
	struct	pmtab	*pmtab;
	struct	sigaction	sigact;
	extern	int	Retry;
	extern	void	open_device();
	extern	int	checkut_line();

	sigact.sa_flags = 0;
	sigact.sa_handler = SIG_IGN;
	(void) sigemptyset(&sigact.sa_mask);
	(void) sigaction(SIGINT, &sigact, NULL);

	/*
	 * It isn't very useful to run ttymon on a terminal; but if the
	 * user does, suppress the log messages.
	 */
	if (!isatty(2)) {
		if (open_filelog_stderr() != 0) {
			(void) fprintf(stderr,
			    "ttymon: failed to initialize log\n");
			exit(1);
		}
	}

	if ((pmtab = ALLOC_PMTAB) == PNULL)
		fatal("ALLOC_PMTAB failed");

	if (parse_args(argc, argv, pmtab) != 0)
		fatal("parse_args failed");

	/*
	 * Only the superuser should execute this command.
	 */
	if (getuid() != 0)
		fatal("ttymon: must be run as root\n");

	read_ttydefs(NULL, FALSE);
	dump_ttydefs();

	/*
	 * In dry run mode, we can go this far safely.  This lets callers do
	 * some checking by running once in dryrun mode and looking for
	 * failures.  Not perfect, but it helps.
	 */
	if (dryrun_mode) {
		(void) fprintf(stdout, "dry run looks good");
		exit(0);
	}

	if ((pmtab->p_device != NULL) && (*(pmtab->p_device) != '\0'))
		while (checkut_line(pmtab->p_device))
			(void) sleep(15);

	if ((pmtab->p_device == NULL) || (*(pmtab->p_device) == '\0')) {
		devname = find_ttyname(0);
		if ((devname == NULL) || (*devname == '\0')) {
			fatal("ttyname cannot find the device on fd 0");
		}
		pmtab->p_device = devname;
		/*
		 * become session leader
		 * fd 0 is closed and reopened just to make sure
		 * controlling tty is set up right
		 */
		(void) setsid();
		(void) close(0);
		revokedevaccess(pmtab->p_device, 0, 0, 0);
		if (open(pmtab->p_device, O_RDWR) < 0) {
			fatal("open %s failed: %s", pmtab->p_device,
			    strerror(errno));
		}
		if ((pmtab->p_modules != NULL) &&
		    (*(pmtab->p_modules) != '\0')) {
			if (push_linedisc(0, pmtab->p_modules,
			    pmtab->p_device) == -1)
				exit(1);
		}
		if (initial_termio(0, pmtab) == -1)
			exit(1);
		(void) di_devperm_logout((const char *)pmtab->p_device);
	} else {
		(void) setsid();
		(void) close(0);
		Retry = FALSE;
		open_device(pmtab);
		if (Retry)		/* open failed */
			exit(1);
	}
	verbose("using device: %s", pmtab->p_device);
	/*
	 * If we're here, the terminal is open on FD 0.
	 */
	if (open_device_log(0) == -1) {
		fatal("failed to initialize logging on %s",
		    pmtab->p_device);
	}
	tmchild(pmtab);
	exit(1);	/*NOTREACHED*/
}

/*
 * parse_arg	- parse cmd line arguments
 */
static	int
parse_args(int argc, char **argv, struct pmtab *pmtab)
{
	static	char	p_server[] = "/usr/bin/login";
	extern	void	getty_account();
	static	char	termbuf[MAX_TERM_TYPE_LEN];
	static	struct	cons_getterm cnterm = {sizeof (termbuf), termbuf};
	int	cn_fd;

	/* initialize fields to some default first */
	pmtab->p_tag = "";
	pmtab->p_flags = 0;
	pmtab->p_identity = "root";
	pmtab->p_res1 = "reserved";
	pmtab->p_res2 = "reserved";
	pmtab->p_res3 = "reserved";
	pmtab->p_uid = 0;
	pmtab->p_gid = 0;
	pmtab->p_dir = "/";
	pmtab->p_ttyflags = 0;
	pmtab->p_count = 0;
	pmtab->p_server = p_server;
	pmtab->p_timeout = 0;
	pmtab->p_modules = "";
	pmtab->p_prompt = "login: ";
	pmtab->p_dmsg = "";
	pmtab->p_termtype = "";
	pmtab->p_device = "";
	pmtab->p_status = GETTY;
	pmtab->p_ttylabel = "9600";

	ttymon_options(argc, argv, pmtab);
	log_argv("ttymon started with: ", argv);

	/*
	 * The following code is only reached if -g was specified.
	 * It attempts to determine a suitable terminal type for
	 * the console login process.
	 *
	 * We also send an ioctl to the console device to query the
	 * TERM type.
	 *
	 * If any of the tests, system calls, or ioctls fail
	 * then pmtab->p_termtype retains its default value
	 * of "".  otherwise it is set to a term type value
	 * that was returned.
	 */
	if ((strlen(pmtab->p_termtype) == 0) &&
	    ((cn_fd = open(pmtab->p_device, O_RDONLY)) != -1)) {

		if (ioctl(cn_fd, CONS_GETTERM, &cnterm) != -1) {
			pmtab->p_termtype = cnterm.cn_term_type;
			verbose("Autoselected terminal type '%s' "
			    "based on '%s' response.",
			    pmtab->p_termtype, pmtab->p_device);
		}
		(void) close(cn_fd);
	}

	if ((pmtab->p_device != NULL) && (*(pmtab->p_device) != '\0'))
		getty_account(pmtab->p_device); /* utmp accounting */
	return (0);
}


/*
 * 	ttymon_options - scan and check args for ttymon express
 */

static	void
ttymon_options(int argc, char **argv, struct pmtab *pmtab)
{
	int 	c;			/* option letter */
	char 	*timeout;
	int  	gflag = 0;		/* -g seen */
	int	size = 0;
	char	tbuf[BUFSIZ];

	extern	char	*optarg;
	extern	int	optind;
	extern	void	copystr();
	extern	char	*strsave();

	while ((c = getopt(argc, argv, "DNT:gd:ht:p:m:l:")) != -1) {
		switch (c) {
		case 'D':
			opendebug();
			debug("Starting ttymon");
			break;
		case 'N':
			debug("Dryrun mode");
			dryrun_mode = 1;
			break;
		case 'g':
			gflag = 1;
			break;
		case 'd':
			pmtab->p_device = optarg;
			break;
		case 'h':
			pmtab->p_ttyflags &= ~H_FLAG;
			break;

		case 'T':
			pmtab->p_termtype = optarg;
			break;
/*
 *		case 'b':
 *			pmtab->p_ttyflags |= B_FLAG;
 *			pmtab->p_ttyflags |= R_FLAG;
 *			break;
 */
		case 't':
			timeout = optarg;
			while (*optarg) {
				if (!isdigit(*optarg++)) {
					log("Invalid argument for "
					    "\"-t\" -- number expected.");
					usage();
				}
			}
			pmtab->p_timeout = atoi(timeout);
			break;
		case 'p':
			copystr(tbuf, optarg);
			pmtab->p_prompt = strsave(getword(tbuf, &size, TRUE));
			break;
		case 'm':
			pmtab->p_modules = optarg;
			if (vml(pmtab->p_modules) != 0)
				usage();
			break;
		case 'l':
			pmtab->p_ttylabel = optarg;
			break;
		case '?':
			usage();
			break;	/*NOTREACHED*/
		}
	}
	if (optind < argc)
		usage();

	if (!gflag)
		usage();
}

/*
 * usage - print out a usage message
 */

static 	void
usage()
{
	char	*umsg = "Usage: ttymon\n  ttymon -g [-h] [-d device] "
	    "[-l ttylabel] [-t timeout] [-p prompt] [-m modules]\n";

	if (isatty(STDERR_FILENO))
		(void) fprintf(stderr, "%s", umsg);
	else
		cons_printf(umsg);
	log(umsg);
	exit(2);
}

/*
 * find_ttyname(fd) 	- find the name of device associated with fd.
 *			- it first tries utmpx to see if an entry exists
 *			- with my pid and ut_line is defined. If ut_line
 *			- is defined, it will see if the major and minor
 *			- number of fd and devname from utmpx match.
 *			- If utmpx search fails, ttyname(fd) will be called.
 */
static	char	*
find_ttyname(int fd)
{
	pid_t ownpid;
	struct utmpx *u;
	static	struct	stat	statf, statu;
	static	char	buf[BUFSIZ];

	ownpid = getpid();
	setutxent();
	while ((u = getutxent()) != NULL) {
		if (u->ut_pid == ownpid) {
			if (strlen(u->ut_line) != 0) {
				if (*(u->ut_line) != '/') {
					(void) strcpy(buf, "/dev/");
					(void) strncat(buf, u->ut_line,
					    sizeof (u->ut_line));
				} else {
					(void) strncat(buf, u->ut_line,
					    sizeof (u->ut_line));
				}
			}
			else
				u = NULL;
			break;
		}
	}
	endutxent();
	if ((u != NULL) &&
	    (fstat(fd, &statf) == 0) &&
	    (stat(buf, &statu) == 0) &&
	    (statf.st_dev == statu.st_dev) &&
	    (statf.st_rdev == statu.st_rdev)) {
		debug("Attempting to find device name from utmpx.");
		return (buf);
	} else {
		debug("Calling ttyname() to find device name.");
		return (ttyname(fd));
	}
}

/*
 * Revoke all access to a device node and make sure that there are
 * no interposed streams devices attached.  Must be called before a
 * device is actually opened.
 * When fdetach is called, the underlying device node is revealed; it
 * will have the previous owner and that owner can re-attach; so we
 * retry until we win.
 * Ignore non-existent devices.
 */
void
revokedevaccess(char *dev, uid_t uid, gid_t gid, mode_t mode)
{
	do {
		if (chown(dev, uid, gid) == -1)
			return;
	} while (fdetach(dev) == 0);

	/* Remove ACLs */

	(void) acl_strip(dev, uid, gid, mode);
}
