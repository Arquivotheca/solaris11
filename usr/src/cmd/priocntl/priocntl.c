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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wait.h>
#include <search.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/param.h>
#include <sys/procset.h>
#include <sys/priocntl.h>
#include <procfs.h>
#include <macros.h>
#include <libgen.h>
#include <limits.h>
#include <errno.h>
#include "priocntl.h"

/*
 * This file contains the code implementing the class independent part
 * of the priocntl command.  Most of the useful work for the priocntl
 * command is done by the class specific sub-commands, the code for
 * which is elsewhere.  The class independent part of the command is
 * responsible for executing the appropriate class specific sub-commands
 * and providing any necessary input to the sub-commands.
 * Code in this file should never assume any knowledge of any specific
 * scheduler class (other than the SYS class).
 */

#define	CLASSPATH	"/usr/lib/class"

typedef struct classpids {
	char		clp_clname[PC_CLNMSZ];
	pid_lwp_t	*clp_pidlist;
	int		clp_pidlistsz;
	int		clp_npids;
} classpids_t;

static char usage[] =
"usage:	priocntl -l\n\
	priocntl -d [-i idtype] [idlist]\n\
	priocntl -s [-c class] [c.s.o.] [-i idtype] [idlist]\n\
	priocntl -e [-c class] [c.s.o.] command [argument(s)]\n";

static char	basenm[BASENMSZ];
static char	cmdpath[MAXPATHLEN];

static char	*procdir = "/proc";

static int	print_classlist(void);
static void	set_procs(char *, idtype_t, int, char **, int, char **);
static void	exec_cmd(char *, char **);
static int	print_procs(idtype_t, int, char *[]);
static void	ids2pids(idtype_t, id_t *, int, classpids_t *, int, plwp_t *,
    int *);
static void	add_pid_tolist(classpids_t *, int, char *, pid_t, id_t);
static void	increase_pidlist(classpids_t *);
static boolean_t	idmatch(char *, char *, int, char **);
static boolean_t	pidmatch(char *, char *, int, char **, plwp_t *, int *,
    char **);
static char	*get_clname(idtype_t, id_t);
/*
 * These variables are defined to be used in prio_getopt() below.
 */
static	int	prio_getopt();
/* LINTED static unused */
static	int	prio_optopt = 0;
static	char	*prio_optarg = 0;
static	int	prio_optind = 1;
static	int	prio_sp = 1;

int
main(int argc, char *argv[])
{
	int		c;
	int		lflag, dflag, sflag, eflag, cflag, iflag, csoptsflag;
	char		*clname = NULL;
	char		*idtypnm;
	idtype_t	idtype;
	int		idargc;
	char		**idargv;

	(void) strlcpy(cmdpath, argv[0], MAXPATHLEN);
	(void) strlcpy(basenm, basename(argv[0]), BASENMSZ);
	lflag = dflag = sflag = eflag = cflag = iflag = csoptsflag = 0;

	/*
	 * Parse the command line options. For reference, these are:
	 * -l  list currently configured classes
	 * -d  display scheduling parameters associated with a set of processes
	 * -s  set scheduling parameters associated with a set of processes
	 * -e  execute a command with given scheduling parameters
	 * -c  specifies the class to be set
	 * -i  specifies the type of id that follows (default is pid)
	 */
	while ((c = prio_getopt(argc, argv, "ldsec:i:")) != -1) {
		switch (c) {
		case 'l':
			lflag++;
			break;

		case 'd':
			dflag++;
			break;

		case 's':
			sflag++;
			break;

		case 'e':
			eflag++;
			break;

		case 'c':
			cflag++;
			clname = prio_optarg;
			break;

		case 'i':
			iflag++;
			idtypnm = prio_optarg;
			break;

		case '?':
			if (strcmp(argv[prio_optind - 1], "-c") == 0 ||
			    strcmp(argv[prio_optind - 1], "-i") == 0) {
				/*
				 * prio_getopt() will return ? if either
				 * of these appear without an argument.
				 */
				fatalerr(usage);
			}

			/*
			 * We assume for now that any option that
			 * prio_getopt() doesn't recognize (with the
			 * exception of 'c' and 'i') is intended for a
			 * class specific subcommand.  For now we also
			 * require that all class specific options
			 * take an argument (until we can get smarter
			 * about parsing our options).
			 */
			csoptsflag++;
			prio_optind++;
			prio_sp = 1;
			break;

		default:
			break;
		}
	}

	/*
	 * Verify that the right options don't overlap, and that each has
	 * the right set of suboptions and arguments, when required.
	 */
	if (lflag) {
		/*
		 * The -l option should not be used with any other options.
		 */
		if (dflag || sflag || eflag || cflag || iflag || csoptsflag)
			fatalerr(usage);

		return (print_classlist());

	} else if (dflag) {
		/*
		 * The -d option's idtype defaults to PID if none
		 * is specified.
		 */
		if (lflag || sflag || eflag || cflag || csoptsflag)
			fatalerr(usage);
		if (iflag) {
			if (str2idtyp(idtypnm, &idtype) == -1)
				fatalerr("%s: bad idtype %s\n", cmdpath,
				    idtypnm);
		} else {
			idtype = P_PID;
		}

		/*
		 * Check if the user specified a list of id's,
		 * default to all id's of this particular id type
		 * otherwise.
		 */
		if (prio_optind < argc) {
			idargc = argc - prio_optind;
			idargv = &argv[prio_optind];
		} else {
			idargc = 0;
		}

		return (print_procs(idtype, idargc, idargv));

	} else if (sflag) {
		/*
		 * The -s option behaves similarly to the -d
		 * option with regards to id type and list.
		 */
		if (lflag || dflag || eflag)
			fatalerr(usage);
		if (iflag) {
			if (str2idtyp(idtypnm, &idtype) == -1)
				fatalerr("%s: bad idtype %s\n", cmdpath,
				    idtypnm);
		} else {
			idtype = P_PID;
		}

		/*
		 * If no class is specified, we only change
		 * the scheduling parameters of the specified
		 * processes.
		 */
		if (cflag == 0)
			clname = NULL;

		if (prio_optind < argc) {
			idargc = argc - prio_optind;
			idargv = &argv[prio_optind];
		} else {
			idargc = 0;
		}

		set_procs(clname, idtype, idargc, idargv, argc, argv);

	} else if (eflag) {
		/*
		 * The -e flag behaves similarly to both the 'd' and 's'
		 * options with regards to id type and list, and the 's'
		 * option with regards to whether a class is specified
		 * or not.
		 */
		if (lflag || dflag || sflag || iflag)
			fatalerr(usage);

		if (cflag == 0)
			clname = NULL;

		if (prio_optind >= argc)
			fatalerr(usage);

		exec_cmd(clname, argv);

	} else {
		fatalerr(usage);
	}

	return (0);
}

/*
 * Print the heading for the class list and execute the class
 * specific sub-command with the -l option for each configured class.
 */
static int
print_classlist(void)
{
	id_t		cid;
	int		nclass;
	pcinfo_t	pcinfo;
	static char	subcmdpath[128];
	int		status;
	pid_t		pid;
	int		error = 0;

	/*
	 * No special privileges required for this operation.
	 * Set the effective UID back to the real UID.
	 */
	if (setuid(getuid()) == -1)
		fatalerr("%s: Can't set effective UID back to real UID\n",
		    cmdpath);

	if ((nclass = priocntl(0, 0, PC_GETCLINFO, NULL)) == -1)
		fatalerr("%s: Can't get number of configured classes,"
		    " priocntl(2) system call failed with errno %d\n",
		    cmdpath, errno);

	(void) printf("CONFIGURED CLASSES\n==================\n\n");
	(void) printf("SYS (System Class)\n");

	for (cid = 1; cid < nclass; cid++) {
		(void) printf("\n");
		(void) fflush(stdout);
		pcinfo.pc_cid = cid;

		if (priocntl(0, 0, PC_GETCLINFO, (caddr_t)&pcinfo) == -1)
			fatalerr("%s: can't get class name (class ID = %ld)\n",
			    cmdpath, cid);

		if (snprintf(subcmdpath, sizeof (subcmdpath), "%s/%s/%s%s",
		    CLASSPATH, pcinfo.pc_clname, pcinfo.pc_clname, basenm) >=
		    sizeof (subcmdpath))
			fatalerr("%s: can't generate %s specific subcommand\n",
			    cmdpath, pcinfo.pc_clname);

		if ((pid = fork()) == 0) {
			(void) execl(subcmdpath, subcmdpath, "-l", (char *)0);
			(void) printf("%s\n", pcinfo.pc_clname);
			fatalerr("\tCan't execute %s specific subcommand\n",
			    pcinfo.pc_clname);
		} else if (pid == (pid_t)-1) {
			(void) printf("%s\n", pcinfo.pc_clname);
			(void) fprintf(stderr,
			    "Can't execute %s specific subcommand)\n",
			    pcinfo.pc_clname);
			error = 1;
		} else {
			(void) wait(&status);
			if (status)
				error = 1;
		}
	}

	return (error);
}

/*
 * For each class represented within the set of processes specified by
 * idtype/idargv, print_procs() executes the class specific sub-command
 * with the -d option.  We pipe to each sub-command a list of pids in
 * the set belonging to that class.
 */
static int
print_procs(idtype_t idtype, int idargc, char *idargv[])
{
	int		i;
	id_t		id;
	id_t		pidlist[NIDS];
	int		nids;
	classpids_t	*clpids;
	int		nclass;
	id_t		cid;
	pcinfo_t	pcinfo;
	int		pidexists;
	FILE		*pipe_to_subcmd;
	char		subcmd[128], *clname = NULL;
	int		error = 0;
	plwp_t		plwplist[NIDS];
	int		nplwps = 0;

	/*
	 * Build a list of ids eliminating any duplicates in idargv.
	 */
	if (idtype == P_ALL) {
		/*
		 * No idlist should be specified. If one is specified,
		 * it is ignored.
		 */
		nids = 0;
	} else if (idargc == 0) {

		/*
		 * No ids supplied by user; use current id.
		 */
		if (getmyid(idtype, &pidlist[0]) == -1)
			fatalerr("%s: Can't get ID for current process,"
			    " idtype = %d\n", cmdpath, idtype);
		nids = 1;
	} else {
		nids = 0;

		for (i = 0; i < idargc && nids < NIDS; i++) {
			/*
			 * The default idtype is P_PID, which is
			 * usually not even specified by the user.
			 */
			switch (idtype) {
			case P_CID:
				if ((id = clname2cid(idargv[i])) == -1) {
					(void) fprintf(stderr, "%s: "
					    "Invalid or unconfigured class "
					    "class %s in idlist - ignored\n",
					    cmdpath, idargv[i]);

					error = 1;
				}
				break;

			case P_PID:
				/*
				 * All PIDs will be added to the idlist.
				 * Those with a LWP range will have each
				 * LWP added to plwplist with their respective
				 * PID. This is a bit redundant, but
				 * simplifies things later on.
				 */
				id = (id_t)str2pid(idargv[i], INT_MIN, INT_MAX,
				    plwplist, &nplwps, &clname);

				if (errno) {
					(void) fprintf(stderr,
					    "%s: Invalid id \"%s\"\n",
					    cmdpath, idargv[i]);
					error = 1;
					id = BADPID;
				}
				break;

			default:
				id = (id_t)str2num(idargv[i], INT_MIN, INT_MAX);

				if (errno) {
					(void) fprintf(stderr,
					    "%s: Invalid id \"%s\"\n",
					    cmdpath, idargv[i]);
					error = 1;
					id = BADPID;
				}
				break;
			}

			/*
			 * lsearch(3C) adds ids to the idlist,
			 * eliminating duplicates.
			 */
			(void) lsearch((void *)&id, (void *)pidlist,
			    (size_t *)&nids, sizeof (id), (int (*)())idcompar);
		}
	}

	/*
	 * Get information from all currently loaded scheduling classes.
	 */
	if ((nclass = priocntl(0, 0, PC_GETCLINFO, NULL)) == -1)
		fatalerr("%s: Can't get number of configured classes,"
		    " priocntl(2) system call failed with errno %d\n",
		    cmdpath, errno);

	if ((clpids = (classpids_t *)malloc(sizeof (classpids_t) * nclass)) ==
	    NULL)
		fatalerr("%s: Can't allocate memory for clpids.\n", cmdpath);

	for (cid = 1; cid < nclass; cid++) {
		pcinfo.pc_cid = cid;
		if (priocntl(0, 0, PC_GETCLINFO, (caddr_t)&pcinfo) == -1)
			fatalerr("%s: Can't get class name, cid = %ld\n",
			    cmdpath, cid);

		(void) strncpy(clpids[cid].clp_clname, pcinfo.pc_clname,
		    PC_CLNMSZ);

		/*
		 * The memory allocation for the pidlist uses realloc().
		 * A realloc() call is required, when "clp_npids" is
		 * equal to "clp_pidlistsz".
		 */
		clpids[cid].clp_pidlist = (pid_lwp_t *)NULL;
		clpids[cid].clp_pidlistsz = 0;
		clpids[cid].clp_npids = 0;
	}

	/*
	 * Build the pidlist.
	 */
	ids2pids(idtype, pidlist, nids, clpids, nclass, plwplist, &nplwps);

	/*
	 * No need for special privileges any more.
	 * Set the effective UID back to the real UID.
	 */
	if (setuid(getuid()) == -1)
		fatalerr("%s: Can't set effective UID back to real UID\n",
		    cmdpath);

	pidexists = 0;

	/*
	 * Pipe to each class specific priocntl(1) with the -d option and a
	 * list of process (and LWPs when specified) ids.
	 */
	for (cid = 1; cid < nclass; cid++) {
		if (clpids[cid].clp_npids == 0)
			continue;

		pidexists = 1;

		if (snprintf(subcmd, sizeof (subcmd), "%s/%s/%s%s -d",
		    CLASSPATH, clpids[cid].clp_clname, clpids[cid].clp_clname,
		    basenm) >= sizeof (subcmd)) {
			(void) fprintf(stderr,
			    "Can't generate %s specific subcommand\n",
			    clpids[cid].clp_clname);
			error = 1;
			free(clpids[cid].clp_pidlist);
			continue;
		}

		if ((pipe_to_subcmd = popen(subcmd, "w")) == NULL) {
			(void) printf("%s\n", clpids[cid].clp_clname);
			(void) fprintf(stderr,
			    "Can't execute %s specific subcommand\n",
			    clpids[cid].clp_clname);
			error = 1;
			free(clpids[cid].clp_pidlist);
			continue;
		}

		(void) fwrite(clpids[cid].clp_pidlist, sizeof (pid_lwp_t),
		    clpids[cid].clp_npids, pipe_to_subcmd);

		if (pclose(pipe_to_subcmd))
			error = 1;

		free(clpids[cid].clp_pidlist);
	}

	free(clpids);

	if (pidexists == 0)
		fatalerr("%s: Process(es) not found.\n", cmdpath);

	return (error);
}

/*
 * Execute the appropriate class specific sub-command with the arguments
 * pointed to by subcmdargv. If the user specified a class we simply
 * exec the sub-command for that class. If no class was specified we
 * verify that the processes in the set specified by idtype/idargv are
 * all in the same class and then execute the sub-command for that class.
 */
static void
set_procs(char *clname, idtype_t idtype, int idargc, char **idargv,
    int argc, char **subcmdargv)
{
	char			idstr[PC_IDTYPNMSZ];
	char			myidstr[PC_IDTYPNMSZ];
	pcinfo_t		pcinfo;
	static psinfo_t		prinfo;
	static prcred_t		prcred;
	DIR			*dirp;
	struct dirent		*dentp;
	static char		pname[100];
	char			*fname, **nargv, *idptr, *cln_pid;
	int			procfd;
	int			saverr;
	static char		subcmdpath[128];
	boolean_t		procinset;
	id_t			id;
	size_t			len;
	plwp_t			plwplist[NIDS];
	int			i, nplwps = 0;

	if (clname == NULL && idtype == P_PID && idargc <= 1) {
		/*
		 * No class specified. If the user didn't specify any process,
		 * we get our PID. Otherwise idargv[0] indicates the target.
		 */
		if (idargc == 0) {
			if ((idptr = calloc(CLP_ID_LENGTH, sizeof (char))) ==
			    NULL)
				fatalerr("%s: Can't allocate memory for"
				    " idptr.\n", cmdpath);

			itoa(getpid(), idptr);
			clname = get_clname(P_PID, P_MYID);
		} else {
			idptr = idargv[0];
		}

		/*
		 * Only one PID specified. Check for a LWP range and
		 * verify that they are all in the same class.
		 */
		id = (id_t)str2pid(idptr, INT_MIN, INT_MAX, plwplist, &nplwps,
		    &clname);

		if (clname == NULL)
			fatalerr("%s: Process %ld has LWPs in different"
			    " scheduling classes.\n", cmdpath, id);

		if (errno)
			fatalerr("%s: Invalid id \"%s\"\n", cmdpath, idargv[0]);

	} else if (clname == NULL) {
		/*
		 * No class specified by user and potentially more
		 * than one process in specified set. Verify that
		 * all procs in set are in the same class.
		 */
		if (idargc == 0 && idtype != P_ALL) {
			/*
			 * No ids supplied by user; use current id.
			 */
			if (getmyidstr(idtype, myidstr) == -1)
				fatalerr("%s: Can't get ID string for current"
				    " process, idtype = %d\n", cmdpath, idtype);
		}

		if ((dirp = opendir(procdir)) == NULL)
			fatalerr("%s: Can't open PROC directory %s\n",
			    cmdpath, procdir);

		while ((dentp = readdir(dirp)) != NULL) {
			if (dentp->d_name[0] == '.')	/* skip . and .. */
				continue;

			len = snprintf(pname, sizeof (pname), "%s/%s/",
			    procdir, dentp->d_name);

			/* Really max(sizeof ("psinfo"), sizeof ("cred")) */
			if (len + sizeof ("psinfo") > sizeof (pname)) {
				(void) fprintf(stderr,
				    "%s: skipping %s, name too long.\n",
				    cmdpath, dentp->d_name);
				continue;
			}

			fname = pname + len;
retry:
			(void) strcpy(fname, "psinfo");
			if ((procfd = open(pname, O_RDONLY)) < 0)
				continue;

			if (read(procfd, &prinfo, sizeof (prinfo)) !=
			    sizeof (prinfo)) {
				saverr = errno;
				(void) close(procfd);

				if (saverr == EAGAIN)
					goto retry;

				if (saverr != ENOENT) {
					(void) fprintf(stderr,
					    "%s: Can't get process info for"
					    " %s\n", cmdpath, pname);
				}

				continue;
			}

			(void) close(procfd);

			if (idtype == P_UID || idtype == P_GID) {
				(void) strcpy(fname, "cred");

				if ((procfd = open(pname, O_RDONLY)) < 0 ||
				    read(procfd, &prcred, sizeof (prcred)) !=
				    sizeof (prcred)) {
					saverr = errno;
					if (procfd >= 0)
						(void) close(procfd);
					if (saverr == EAGAIN)
						goto retry;
					if (saverr != ENOENT) {
						(void) fprintf(stderr,
						    "%s: Can't get process"
						    " credentials for %s\n",
						    cmdpath, pname);
					}
					continue;
				}

				(void) close(procfd);
			}

			if (prinfo.pr_lwp.pr_state == 0 || prinfo.pr_nlwp == 0)
				continue;

			switch (idtype) {
			case P_PID:
				cln_pid = NULL;

				itoa((long)prinfo.pr_pid, idstr);
				procinset = pidmatch(idstr, myidstr, idargc,
				    idargv, plwplist, &nplwps, &cln_pid);

				if (procinset == B_TRUE) {
					if (cln_pid == NULL) {
						fatalerr("%s: Process %s has"
						    " LWPs in different"
						    " scheduling classes.\n",
						    cmdpath, idstr);
					}

					if (clname == NULL) {
						clname = cln_pid;
						break;
					}

					if (strncmp(clname, cln_pid,
					    PC_CLNMSZ) != 0) {
						fatalerr("%s: Specified"
						    " processes in different"
						    " scheduling classes.\n",
						    cmdpath);
					}
				}
				break;

			case P_PPID:
				itoa((long)prinfo.pr_ppid, idstr);
				procinset = idmatch(idstr, myidstr,
				    idargc, idargv);
				break;

			case P_PGID:
				itoa((long)prinfo.pr_pgid, idstr);
				procinset = idmatch(idstr, myidstr,
				    idargc, idargv);
				break;

			case P_SID:
				itoa((long)prinfo.pr_sid, idstr);
				procinset = idmatch(idstr, myidstr,
				    idargc, idargv);
				break;

			case P_CID:
				procinset = idmatch(prinfo.pr_lwp.pr_clname,
				    myidstr, idargc, idargv);
				break;

			case P_UID:
				itoa((long)prcred.pr_euid, idstr);
				procinset = idmatch(idstr, myidstr,
				    idargc, idargv);
				break;

			case P_GID:
				itoa((long)prcred.pr_egid, idstr);
				procinset = idmatch(idstr, myidstr,
				    idargc, idargv);
				break;

			case P_PROJID:
				itoa((long)prinfo.pr_projid, idstr);
				procinset = idmatch(idstr, myidstr,
				    idargc, idargv);
				break;

			case P_TASKID:
				itoa((long)prinfo.pr_taskid, idstr);
				procinset = idmatch(idstr, myidstr,
				    idargc, idargv);
				break;

			case P_ZONEID:
				itoa((long)prinfo.pr_zoneid, idstr);
				procinset = idmatch(idstr, myidstr,
				    idargc, idargv);
				break;

			case P_CTID:
				itoa((long)prinfo.pr_contract, idstr);
				procinset = idmatch(idstr, myidstr,
				    idargc, idargv);
				break;

			case P_ALL:
				procinset = B_TRUE;
				break;

			default:
				fatalerr("%s: Bad idtype %d in set_procs()\n",
				    cmdpath, idtype);
			}

			if (procinset == B_TRUE) {
				if (clname == NULL) {
					/*
					 * First proc found in set.
					 */
					if ((clname = calloc(PC_CLNMSZ,
					    sizeof (char))) == NULL)
						fatalerr("%s: Can't allocate"
						    " memory for clname.\n",
						    cmdpath);

					(void) strncpy(clname,
					    prinfo.pr_lwp.pr_clname, PC_CLNMSZ);
				} else {
					if (strcmp(clname,
					    prinfo.pr_lwp.pr_clname) != 0) {
						fatalerr("%s: Specified"
						    " processes in different"
						    " scheduling classes.\n",
						    cmdpath);
					}
				}
			}
		}

		(void) closedir(dirp);

		/*
		 * If the user specified LWPs, check if they are all in the
		 * same class.
		 */
		for (i = 0; i < nplwps - 1; i++) {
			if (strcmp(plwplist[i].clname,
			    plwplist[i + 1].clname) != 0) {
				fatalerr("%s: Specified LWPs in different"
				    " scheduling classes.\n", cmdpath);
			}
		}

		if (clname == NULL)
			fatalerr("%s: Process(es) not found.\n", cmdpath);
	} else {
		/*
		 * User specified class. Check it for validity.
		 */
		(void) strcpy(pcinfo.pc_clname, clname);

		if (priocntl(0, 0, PC_GETCID, (caddr_t)&pcinfo) == -1)
			fatalerr("%s: Invalid or unconfigured class %s\n",
			    cmdpath, clname);

		/*
		 * Check for a LWP range. The user specified a class, so we
		 * don't check if all processes/LWPs are in the same one.
		 */
		if (idtype == P_PID) {
			char *cln = NULL;

			for (i = 0; i < idargc; i++) {
				id = (id_t)str2pid(idargv[i], INT_MIN, INT_MAX,
				    plwplist, &nplwps, &cln);

				if (errno)
					fatalerr("%s: Invalid id \"%s\"\n",
					    cmdpath, idargv[0]);
			}
		}
	}

	/*
	 * No need for special privileges any more.
	 * Set the effective UID back to the real UID.
	 */
	if (setuid(getuid()) == -1)
		fatalerr("%s: Can't set effective UID back to real UID\n",
		    cmdpath);

	if (snprintf(subcmdpath, sizeof (subcmdpath), "%s/%s/%s%s",
	    CLASSPATH, clname, clname, basenm) >= sizeof (subcmdpath))
		fatalerr("%s: can't generate %s specific subcommand\n",
		    cmdpath, clname);

	/*
	 * If the user specified one or more LWPs, we recreate the original
	 * argv to contain one entry for each pair of PID/LWP. For example,
	 * '123/1-3' will be broken into '123/1', '123/2' and '123/3'.
	 * This is a bit laborious but necessary since we need to execv(2)
	 * the class specific priocntl(1) with the right arguments.
	 */
	if (idtype == P_PID && nplwps > 0) {
		int idx = 0, aidx = 0, nidx = 0, nargc = argc + nplwps;
		id_t pid;
		char *q;

		if ((nargv = calloc(nargc, sizeof (char *))) == NULL)
			fatalerr("%s: Can't allocate memory for nargv.\n",
			    cmdpath);

		/*
		 * Loop through the argv, rebuilding id's for which one or
		 * more LWPs were specified.
		 */
		while (idx < argc && aidx < idargc && nidx < nargc) {
			if (subcmdargv[idx] == idargv[aidx]) {
				/*
				 * This subcmdargv item is an ID. Check if it
				 * the user specified any LWPs for this PID.
				 */
				pid = (id_t)strtol(idargv[aidx], &q, 10);

				if (*q != '/') {
					/*
					 * Plain ID, no LWPs. Continue to
					 * the next item in argv.
					 */
					nargv[nidx] = idargv[aidx];
					nidx++;
					idx++;
					continue;
				}

				/*
				 * Find this PID in the plwp list, adding
				 * an entry for each of its LWPs.
				 */
				for (i = 0; i < nplwps; i++) {
					if (pid == plwplist[i].pid &&
					    plwplist[i].lwp != -1) {

						if ((nargv[nidx] = calloc(1,
						    sizeof (pid_lwp_t))) ==
						    NULL)
							fatalerr("%s: Can't"
							    " allocate memory"
							    " for nargv.\n",
							    cmdpath);

						(void) snprintf(nargv[nidx],
						    sizeof (pid_lwp_t),
						    "%d/%d ", pid,
						    plwplist[i].lwp);

						/*
						 * Mark this pid/lwp pair as
						 * accounted for.
						 */
						plwplist[i].lwp = -1;

						nidx++;
					}
				}
				aidx++;

			} else {
				/*
				 * Not an ID, just copy it to the new argv.
				 */
				nargv[nidx] = subcmdargv[idx];
				nidx++;
			}

			idx++;
		}

		/*
		 * Point subcmdargv to the new argv.
		 */
		subcmdargv = nargv;
	}

	subcmdargv[0] = subcmdpath;
	(void) execv(subcmdpath, subcmdargv);

	fatalerr("%s: Can't execute %s sub-command\n", cmdpath, clname);
}

/*
 * Execute the appropriate class specific sub-command with the arguments
 * pointed to by subcmdargv.  If the user specified a class we simply
 * exec the sub-command for that class.  If no class was specified we
 * execute the sub-command for our own current class.
 */
static void
exec_cmd(char *clname, char **subcmdargv)
{
	pcinfo_t	pcinfo;
	char		subcmdpath[128];

	/*
	 * No special privileges required for this operation.
	 * Set the effective UID back to the real UID.
	 */
	if (setuid(getuid()) == -1)
		fatalerr("%s: Can't set effective UID back to real UID\n",
		    cmdpath);

	if (clname == NULL) {
		clname = get_clname(P_PID, P_MYID);
	} else {
		/*
		 * User specified class. Check it for validity.
		 */
		(void) strcpy(pcinfo.pc_clname, clname);
		if (priocntl(0, 0, PC_GETCID, (caddr_t)&pcinfo) == -1)
			fatalerr("%s: Invalid or unconfigured class %s\n",
			    cmdpath, clname);
	}

	if (snprintf(subcmdpath, sizeof (subcmdpath), "%s/%s/%s%s",
	    CLASSPATH, clname, clname, basenm) >= sizeof (subcmdpath))
		fatalerr("%s: can't generate %s specific subcommand\n",
		    cmdpath, clname);

	subcmdargv[0] = subcmdpath;
	(void) execv(subcmdpath, subcmdargv);
	fatalerr("%s: Can't execute %s sub-command\n", cmdpath, clname);
}

/*
 * Fill in the classpids structures in the array pointed to by clpids
 * with pids for the processes in the set specified by idtype/idlist.
 * We read the /proc/<pid>/psinfo file to get the necessary process
 * information.
 */
static void
ids2pids(idtype_t idtype, id_t *idlist, int nids, classpids_t *clpids,
    int nclass, plwp_t *plwplist, int *nplwps)
{
	static psinfo_t		prinfo;
	static prcred_t		prcred;
	DIR			*dirp;
	struct dirent		*dentp;
	char			pname[100];
	char			*fname;
	int			procfd;
	int			saverr;
	int			i;
	char			*clname;
	size_t			len;

	if ((dirp = opendir(procdir)) == NULL)
		fatalerr("%s: Can't open PROC directory %s\n",
		    cmdpath, procdir);

	while ((dentp = readdir(dirp)) != NULL) {
		if (dentp->d_name[0] == '.')	/* skip . and .. */
			continue;

		len = snprintf(pname, sizeof (pname), "%s/%s/",
		    procdir, dentp->d_name);
		/* Really max(sizeof ("psinfo"), sizeof ("cred")) */
		if (len + sizeof ("psinfo") > sizeof (pname)) {
			(void) fprintf(stderr,
			    "%s: skipping %s, name too long.\n",
			    cmdpath, dentp->d_name);
			continue;
		}
		fname = pname + len;
retry:
		(void) strcpy(fname, "psinfo");
		if ((procfd = open(pname, O_RDONLY)) < 0)
			continue;

		if (read(procfd, &prinfo, sizeof (prinfo)) != sizeof (prinfo)) {
			saverr = errno;
			(void) close(procfd);
			if (saverr == EAGAIN)
				goto retry;
			if (saverr != ENOENT) {
				(void) fprintf(stderr,
				    "%s: Can't get process info for %s\n",
				    cmdpath, pname);
			}
			continue;
		}

		(void) close(procfd);

		if (idtype == P_UID || idtype == P_GID) {
			(void) strcpy(fname, "cred");
			if ((procfd = open(pname, O_RDONLY)) < 0 ||
			    read(procfd, &prcred, sizeof (prcred)) !=
			    sizeof (prcred)) {
				saverr = errno;
				(void) close(procfd);
				if (saverr == EAGAIN)
					goto retry;
				if (saverr != ENOENT) {
					(void) fprintf(stderr,
					    "%s: Can't get process credentials"
					    " for %s\n",
					    cmdpath, pname);
				}
				continue;
			}
			(void) close(procfd);
		}

		if (prinfo.pr_lwp.pr_state == 0 || prinfo.pr_nlwp == 0)
			continue;

		switch (idtype) {
		case P_PID:
			for (i = 0; i < nids; i++) {
				if (idlist[i] == (id_t)prinfo.pr_pid) {
					int j;
					boolean_t found = B_FALSE;

					/*
					 * Check if the user specified any LWPs
					 * for this PID. If not, simply add an
					 * entry without an LWP.
					 */
					for (j = 0; j < *nplwps; j++) {
						if (plwplist[j].pid ==
						    (id_t)prinfo.pr_pid) {
							add_pid_tolist(clpids,
							    nclass,
							    plwplist[j].clname,
							    plwplist[j].pid,
							    plwplist[j].lwp);

							found = B_TRUE;
						}
					}

					if (found == B_FALSE) {
						add_pid_tolist(clpids,
						    nclass,
						    prinfo.pr_lwp.pr_clname,
						    prinfo.pr_pid, 0);
					}
				}
			}
			break;

		case P_PPID:
			for (i = 0; i < nids; i++) {
				if (idlist[i] == (id_t)prinfo.pr_ppid)
					add_pid_tolist(clpids, nclass,
					    prinfo.pr_lwp.pr_clname,
					    prinfo.pr_pid, 0);
			}
			break;

		case P_PGID:
			for (i = 0; i < nids; i++) {
				if (idlist[i] == (id_t)prinfo.pr_pgid)
					add_pid_tolist(clpids, nclass,
					    prinfo.pr_lwp.pr_clname,
					    prinfo.pr_pid, 0);
			}
			break;

		case P_SID:
			for (i = 0; i < nids; i++) {
				if (idlist[i] == (id_t)prinfo.pr_sid)
					add_pid_tolist(clpids, nclass,
					    prinfo.pr_lwp.pr_clname,
					    prinfo.pr_pid, 0);
			}
			break;

		case P_CID:
			for (i = 0; i < nids; i++) {
				clname = clpids[idlist[i]].clp_clname;
				if (strcmp(clname,
				    prinfo.pr_lwp.pr_clname) == 0)
					add_pid_tolist(clpids, nclass,
					    prinfo.pr_lwp.pr_clname,
					    prinfo.pr_pid, 0);
			}
			break;

		case P_UID:
			for (i = 0; i < nids; i++) {
				if (idlist[i] == (id_t)prcred.pr_euid)
					add_pid_tolist(clpids, nclass,
					    prinfo.pr_lwp.pr_clname,
					    prinfo.pr_pid, 0);
			}
			break;

		case P_GID:
			for (i = 0; i < nids; i++) {
				if (idlist[i] == (id_t)prcred.pr_egid)
					add_pid_tolist(clpids, nclass,
					    prinfo.pr_lwp.pr_clname,
					    prinfo.pr_pid, 0);
			}
			break;

		case P_PROJID:
			for (i = 0; i < nids; i++) {
				if (idlist[i] == (id_t)prinfo.pr_projid)
					add_pid_tolist(clpids, nclass,
					    prinfo.pr_lwp.pr_clname,
					    prinfo.pr_pid, 0);
			}
			break;

		case P_TASKID:
			for (i = 0; i < nids; i++) {
				if (idlist[i] == (id_t)prinfo.pr_taskid)
					add_pid_tolist(clpids, nclass,
					    prinfo.pr_lwp.pr_clname,
					    prinfo.pr_pid, 0);
			}
			break;

		case P_ZONEID:
			for (i = 0; i < nids; i++) {
				if (idlist[i] == (id_t)prinfo.pr_zoneid)
					add_pid_tolist(clpids, nclass,
					    prinfo.pr_lwp.pr_clname,
					    prinfo.pr_pid, 0);
			}
			break;

		case P_CTID:
			for (i = 0; i < nids; i++) {
				if (idlist[i] == (id_t)prinfo.pr_contract)
					add_pid_tolist(clpids, nclass,
					    prinfo.pr_lwp.pr_clname,
					    prinfo.pr_pid, 0);
			}
			break;

		case P_ALL:
			add_pid_tolist(clpids, nclass, prinfo.pr_lwp.pr_clname,
			    prinfo.pr_pid, 0);
			break;

		default:
			fatalerr("%s: Bad idtype %d in ids2pids()\n",
			    cmdpath, idtype);
		}
	}

	(void) closedir(dirp);
}

/*
 * Search the array pointed to by clpids for the classpids
 * structure corresponding to clname and add pid to its
 * pidlist.
 */
static void
add_pid_tolist(classpids_t *clpids, int	nclass, char *clname, pid_t pid,
    id_t lwp)
{
	classpids_t *clp;
	int npids;

	for (clp = clpids; clp != &clpids[nclass]; clp++) {
		if (strcmp(clp->clp_clname, clname) == 0) {
			if (clp->clp_npids == clp->clp_pidlistsz)
				increase_pidlist(clp);

			npids = clp->clp_npids;

			if (lwp > 0) {
				(void) snprintf((clp->clp_pidlist)[npids],
				    sizeof (pid_lwp_t), "%d/%d", pid, lwp);
			} else {
				(void) snprintf((clp->clp_pidlist)[npids],
				    sizeof (pid_lwp_t), "%d", pid);
			}

			clp->clp_npids++;
			return;
		}
	}
}

static void
increase_pidlist(classpids_t *clp)
{
	if ((clp->clp_pidlist = realloc(clp->clp_pidlist,
	    (clp->clp_pidlistsz + NPIDS) * sizeof (pid_lwp_t))) ==
	    NULL)
		/*
		 * The pidlist is filled up and we cannot increase the size.
		 */
		fatalerr("%s: Can't allocate memory for pidlist.\n", cmdpath);

	clp->clp_pidlistsz += NPIDS;
}

/*
 * Compare id strings for equality. If idargv contains ids
 * (idargc > 0) compare idstr to each id in idargv, otherwise
 * just compare to curidstr.
 */
static boolean_t
idmatch(char *idstr, char *curidstr, int idargc, char **idargv)
{
	int i;

	if (idargc == 0) {
		if (strcmp(curidstr, idstr) == 0)
			return (B_TRUE);
	} else {
		for (i = 0; i < idargc; i++) {
			if (strcmp(idargv[i], idstr) == 0)
				return (B_TRUE);
		}
	}

	return (B_FALSE);
}

/*
 * Similar to idmatch above, but for id's of type PID. After comparing
 * id strings for equality, the id string is passed to str2pid() so
 * it can add this id to the plwp list.
 */
static boolean_t
pidmatch(char *idstr, char *curidstr, int idargc, char **idargv,
    plwp_t *plwplist, int *nplwps, char **clname)
{
	char *pid, buf[CLP_PIDLIST_SZ];
	boolean_t ret = B_FALSE;
	int i;

	if (idargc == 0) {
		(void) strncpy(buf, curidstr, CLP_PIDLIST_SZ);
		pid = strtok(buf, "/");

		if (strcmp(pid, idstr) == 0) {
			(void) str2pid(curidstr, INT_MIN, INT_MAX, plwplist,
			    nplwps, clname);
			ret = B_TRUE;
		}
	} else {
		for (i = 0; i < idargc; i++) {
			(void) strncpy(buf, idargv[i], CLP_PIDLIST_SZ);
			pid = strtok(buf, "/");

			if (strcmp(pid, idstr) == 0) {
				(void) str2pid(idargv[i], INT_MIN, INT_MAX,
				    plwplist, nplwps, clname);
				ret = B_TRUE;
			}
		}
	}

	return (ret);
}

/*
 * This is a copy of the getopt() function found in libc:getopt.c. A separate
 * copy is required to fix the bug id #1114636. To fix the problem we need to
 * reset the _sp to 1. Since _sp in libc:getopt() is not exposed, a copy of
 * the getopt() is kept so that prio_sp can be reset to 1.
 */
static int
prio_getopt(int	argc, char **argv, char *opts)
{
	register char c;
	register char *cp;

	/*
	 * First check for illegal syntax in the passed in options.
	 */
	if (prio_sp == 1)
		if (prio_optind >= argc ||
		    argv[prio_optind][0] != '-' || argv[prio_optind][1] == '\0')
			return (EOF);
		else if (strcmp(argv[prio_optind], "--") == NULL) {
			prio_optind++;
			return (EOF);
		}

	/*
	 * Now check if the option is valid (part of 'opts').
	 */
	prio_optopt = c = (unsigned char)argv[prio_optind][prio_sp];
	if (c == ':' || (cp = strchr(opts, c)) == NULL) {
		if (argv[prio_optind][++prio_sp] == '\0') {
			prio_optind++;
			prio_sp = 1;
		}
		return ('?');
	}

	/*
	 * Check if this option requires an argument.
	 */
	if (*++cp == ':') {
		if (argv[prio_optind][prio_sp+1] != '\0') {
			prio_optarg = &argv[prio_optind++][prio_sp+1];
		} else {
			if (++prio_optind >= argc) {
				prio_sp = 1;
				return ('?');
			} else {
				prio_optarg = argv[prio_optind++];
			}
		}

		prio_sp = 1;
	} else {
		if (argv[prio_optind][++prio_sp] == '\0') {
			prio_sp = 1;
			prio_optind++;
		}
		prio_optarg = NULL;
	}

	return (c);
}

char *
get_clname(idtype_t idtype, id_t pid)
{
	char *cln;

	if ((cln = calloc(PC_CLNMSZ, sizeof (char))) == NULL)
		fatalerr("%s: Can't allocate memory for cln.\n", cmdpath);

	if (priocntl(idtype, pid, PC_GETXPARMS, NULL, PC_KY_CLNAME, cln, 0) ==
	    -1) {
		free(cln);

		if (errno == ESRCH) {
			fatalerr("%s: Process not found.\n", cmdpath);
		} else {
			fatalerr("%s: Can't get class of current process\n"
			    "priocntl(2) system call failed with errno %d\n",
			    cmdpath, errno);
		}
	}

	return (cln);
}
