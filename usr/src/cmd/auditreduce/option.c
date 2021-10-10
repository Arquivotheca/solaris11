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
 * Copyright (c) 1992, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Command line option processing for auditreduce.
 * The entry point is process_options(), which is called by main().
 * Process_options() is the only function visible outside this module.
 */

#include <locale.h>
#include <sys/zone.h>	/* for max zonename length */
#include "auditr.h"

/*
 * Object entry.
 * Maps object strings specified on the command line to a flag
 * used when searching by object type.
 */

struct obj_ent {
	char	*obj_str; /* string specified on the command line */
	int	obj_flag; /* flag used when searching */
};

typedef struct obj_ent obj_ent_t;

/*
 * Supports searches by object type.
 */
static obj_ent_t obj_tbl[] = {
			{ "file", OBJ_PATH },
			{ "filegroup", OBJ_FGROUP },
			{ "fileowner", OBJ_FOWNER },
			{ "fmri", OBJ_FMRI },
			{ "lp", OBJ_LP   },
			{ "msgqid", OBJ_MSG  },
			{ "msgqgroup", OBJ_MSGGROUP },
			{ "msgqowner", OBJ_MSGOWNER },
			{ "path", OBJ_PATH },
			{ "pid", OBJ_PROC },
			{ "procgroup", OBJ_PGROUP },
			{ "procowner", OBJ_POWNER },
			{ "semid", OBJ_SEM  },
			{ "semgroup", OBJ_SEMGROUP  },
			{ "semowner", OBJ_SEMOWNER  },
			{ "shmid", OBJ_SHM  },
			{ "shmgroup", OBJ_SHMGROUP  },
			{ "shmowner", OBJ_SHMOWNER  },
			{ "sock", OBJ_SOCK },
			{ "user", OBJ_USER } };

extern int	derive_date(char *, struct tm *);
extern int	parse_time(char *, int);
extern char	*re_comp2(char *);
extern time_t	tm_to_secs(struct tm *);
extern void	set_err(char *, ...);

static int	a_isnum(char *, int);
static int	check_file(audit_fcb_t *, int);
static int	gather_dir(char *);
static audit_pcb_t *get_next_pcb(char *);
static obj_ent_t *obj_lkup(char *);
static int	proc_class(char *);
static int	proc_date(char *, int);
static int	proc_file(char *, int);
static int	process_fileopt(int, char *argv[], int);
static int	proc_group(char *, gid_t *);
static int	proc_id(char *, int);
static int	proc_object(char *);
static void	proc_pcb(audit_pcb_t *, char *, int);
static int	proc_label(char *);
static int	proc_subject(char *);
static int	proc_sid(char *);
static int	proc_type(char *);
static int	proc_user(char *, uid_t *);
static int	proc_zonename(char *);
static int	proc_fmri(char *);
static int	catpath(char *, int, int, ...);
static int	validate_optarg(char *);

/*
 * .func	process_options - process command line options.
 * .desc	Process the user's command line options. These are of two types:
 *	single letter flags that are denoted by '-', and filenames. Some
 *	of the flags have arguments. Getopt() is used to get the flags.
 *	When this is done it calls process_fileopt() to handle any filenames
 *	that were there.
 * .call	ret = process_options(argc, argv).
 * .arg	argc	- the original value.
 * .arg	argv	- the original value.
 * .ret	0	- no errors detected.
 * .ret	-1	- command line error detected (message already printed).
 */
int
process_options(int argc, char **argv)
{
	int	opt;
	int	error = FALSE;
	int	error_combo = FALSE;
	extern int	optind;		/* in getopt() */
	extern char	*optarg;	/* in getopt() - holds arg to flag */
	struct stat	statbuf;

	static char	*options = "ACD:M:NQR:S:VO:"
	    "a:b:c:d:e:g:j:l:m:o:r:s:t:u:z:";

	error_str = gettext("general error");

	zonename = NULL;
	/*
	 * Big switch to process the flags.
	 * Start_over: is for handling the '-' for standard input. Getopt()
	 * doesn't recognize it.
	 */
start_over:
	while ((opt = getopt(argc, argv, options)) != EOF) {
		if (optarg != NULL && validate_optarg(optarg) == -1) {
			(void) fprintf(stderr,
			    gettext("%s argument error - %s.\n"),
			    ar, error_str);
			return (-1);
		}

		switch (opt) {
		case 'A':		/* all records from the files */
			f_all = TRUE;
			break;
		case 'C':		/* process only completed files */
			f_complete = TRUE;
			break;
		case 'D':		/* delete the files when done */
			/* force 'A' 'C' 'O' to be active */
			f_all = f_complete = TRUE;
			f_outfile = optarg;
			f_delete = TRUE;
			break;
		case 'M':		/* only files from a certain machine */
			f_machine = optarg;
			break;
		case 'N':		/* new object selection mode */
			new_mode = TRUE;
			break;
		case 'Q':		/* no file error reporting */
			f_quiet = TRUE;
			break;
		case 'R':		/* from specified root */
			f_root = optarg;
			break;
		case 'S':		/* from specific directory */
			f_sdir = optarg;
			break;
		case 'V':		/* list all files as they are opened */
			f_verbose = TRUE;
			break;
		case 'O':		/* write to outfile */
			f_outfile = optarg;
			break;
		case 'a':		/* after 'date' */
		case 'b':		/* before 'date' */
		case 'd':		/* from 'day' */
			if (proc_date(optarg, opt) == -1) {
				error = TRUE;
			}
			break;
		case 'j':		/* subject */
			if (proc_subject(optarg) == -1) {
				error = TRUE;
			}
			break;
		case 'm':		/* message 'type' */
			if (proc_type(optarg) == -1) {
				error = TRUE;
			}
			break;
		case 'o':		/* object type */
			if (proc_object(optarg) == -1) {
				error = TRUE;
			}
			break;
		case 'c':		/* message class */
			if (proc_class(optarg) == -1) {
				error = TRUE;
			}
			break;
		case 'u':		/* form audit user */
		case 'e':		/* form effective user */
		case 'r':		/* form real user */
		case 'f':		/* form effective group */
		case 'g':		/* form real group */
			if (proc_id(optarg, opt) == -1) {
				error = TRUE;
			}
			break;
		case 'l':		/* TX label range */
			if (!is_system_labeled()) {
				(void) fprintf(stderr,
				    gettext("%s option 'l' requires "
				    "Trusted Extensions.\n"), ar);
				return (-1);
			}
			if (proc_label(optarg) == -1) {
				error = TRUE;
			}
			break;
		case 's':		/* session ID */
			if (proc_sid(optarg) == -1) {
				error = TRUE;
			}
			break;
		case 'z':		/* zone name */
			if (proc_zonename(optarg) == -1) {
				error = TRUE;
			}
			break;
		case 't':		/* terminal ID reserved for later */
		default:
			return (-1);
		}
		if (error) {
			(void) fprintf(stderr,
			    gettext("%s command line error - %s.\n"),
			    ar, error_str);
			return (-1);
		}
	}
	/* catch '-' option for stdin processing - getopt() won't see it */
	if (optind < argc) {
		if (argv[optind][0] == '-' && argv[optind][1] == '\0') {
			optind++;
			f_stdin = TRUE;
			goto start_over;
		}
	}

	/*
	 * Give a default value for 'b' option if not specified.
	 */
	if (m_before == 0) {
		m_before = MAXLONG;	/* forever */
	}

	/*
	 * Validate combinations of options.
	 * The following are done:
	 *	1. Can't have 'M' or 'S' or 'R' with filenames.
	 *	2. Can't have an after ('a') time after a before ('b') time.
	 *	3. Delete ('D') must have 'C' and 'A' and 'O' with it.
	 *	4. Input from stdin ('-') can't have filenames too.
	 */
	if ((f_machine != NULL || f_sdir != NULL || f_root != NULL) &&
	    argc != optind) {
		error_str = gettext(
		    "no filenames allowed with 'M', 'S' or 'R' options");
		error_combo = TRUE;
	}
	if (m_after >= m_before) {
		error_str =
		    gettext("'a' parameter must be before 'b' parameter");
		error_combo = TRUE;
	}
	if (f_delete && (!f_complete || !f_all || !f_outfile)) {
		error_str = gettext(
		    "'C', 'A', and 'O' must be specified with 'D'");
		error_combo = TRUE;
	}
	if (f_stdin && argc != optind) {
		error_str = gettext("no filenames allowed with '-' option");
		error_combo = TRUE;
	}

	if (f_root == NULL) {
		f_root = "/var/audit";
	}
	if (stat(f_root, &statbuf) != 0) {
		set_err(gettext("%s could not stat directory %s"), ar, f_root);
		perror(errbuf);
		return (-1);
	}
	if (!S_ISDIR(statbuf.st_mode)) {
		(void) fprintf(stderr, gettext("%s %s is not a directory\n"),
		    ar, f_root);
		return (-1);
	}

	/* If error with option combos then print message and exit. */
	if (error_combo) {
		(void) fprintf(stderr,
		    gettext("%s command line error - %s.\n"), ar, error_str);
		return (-1);
	}

	/*
	 * Now handle any filenames included in the command line.
	 */
	return (process_fileopt(argc, argv, optind));
}

int
proc_subject(char *optarg)
{
	if (flags & M_SUBJECT) {
		error_str = gettext("'j' option specified multiple times");
		return (-1);
	}
	flags |= M_SUBJECT;
	subj_id = atol(optarg);
	return (0);
}

int
proc_sid(char *optarg)
{
	if (flags & M_SID) {
		error_str = gettext("'s' option specified multiple times");
		return (-1);
	}
	flags |= M_SID;
	m_sid = (au_asid_t)atol(optarg);
	return (0);
}

int
proc_object(char *optarg)
{
	char	*obj_str;
	char	*obj_val;
	char	*obj_arg;
	int	err;
	int	rval = 0;

	obj_ent_t *oep;
	struct hostent *he;

	if (flags & M_OBJECT) {
		error_str = gettext("'o' option specified multiple times");
		return (-1);
	}
	flags |= M_OBJECT;
	if ((obj_arg = strdup(optarg)) == NULL) {
		return (-1);
	}
	if ((obj_str = strtok(obj_arg, "=")) == NULL ||
	    (oep = obj_lkup(obj_str)) == NULL ||
	    (obj_val = strtok(NULL, "=")) == NULL) {
		set_err(gettext("invalid object arg (%s)"), obj_arg);
		free(obj_arg);
		return (-1);
	}

	obj_flag = oep->obj_flag;

	switch (obj_flag) {
	case OBJ_PATH:
		if ((error_str = re_comp2(obj_val)) != NULL) {
			rval = -1;
			break;
		}
		break;
	case OBJ_SOCK:
		if (!a_isnum(obj_val, TRUE)) {
			obj_id = atol(obj_val);
			socket_flag = SOCKFLG_PORT;
			break;
		}
		if (*obj_val == '0') {
			(void) sscanf(obj_val, "%x", (uint_t *)&obj_id);
			socket_flag = SOCKFLG_PORT;
			break;
		}

		he = getipnodebyname((const void *)obj_val, AF_INET6, 0, &err);
		if (he == 0) {
			he = getipnodebyname((const void *)obj_val, AF_INET,
			    0, &err);
			if (he == 0) {
				set_err(gettext("invalid machine name (%s)"),
				    obj_val);
				rval = -1;
				break;
			}
		}

		if (he->h_addrtype == AF_INET6) {
			/* LINTED */
			if (IN6_IS_ADDR_V4MAPPED(
			    (in6_addr_t *)he->h_addr_list[0])) {
				/* address is IPv4 (32 bits) */
				(void) memcpy(&obj_id,
				    he->h_addr_list[0] + 12, 4);
				ip_type = AU_IPv4;
			} else {
				(void) memcpy(ip_ipv6, he->h_addr_list[0], 16);
				ip_type = AU_IPv6;
			}
		} else {
			/* address is IPv4 (32 bits) */
			(void) memcpy(&obj_id, he->h_addr_list[0], 4);
			ip_type = AU_IPv4;
		}

		freehostent(he);
		socket_flag = SOCKFLG_MACHINE;
		break;
	case OBJ_MSG:
	case OBJ_SEM:
	case OBJ_SHM:
	case OBJ_PROC:
		obj_id = atol(obj_val);
		break;
	case OBJ_FGROUP:
	case OBJ_MSGGROUP:
	case OBJ_SEMGROUP:
	case OBJ_SHMGROUP:
	case OBJ_PGROUP:
		rval = proc_group(obj_val, &obj_group);
		break;
	case OBJ_FOWNER:
	case OBJ_MSGOWNER:
	case OBJ_SEMOWNER:
	case OBJ_SHMOWNER:
	case OBJ_POWNER:
		rval = proc_user(obj_val, &obj_owner);
		break;
	case OBJ_FMRI:
		rval = proc_fmri(obj_val);
		break;
	case OBJ_USER:
		rval = proc_user(obj_val, &obj_user);
		break;
	case OBJ_LP: /* lp objects have not yet been defined */
	default: /* impossible */
		set_err(gettext("invalid object type (%s)"), obj_str);
		rval = -1;
		break;
	} /* switch */

	free(obj_arg);
	return (rval);
}


obj_ent_t *
obj_lkup(char *obj_str)
{
	int	i;

	for (i = 0; i < sizeof (obj_tbl) / sizeof (obj_ent_t); i++) {
		if (strcmp(obj_str, obj_tbl[i].obj_str) == 0) {
			return (&obj_tbl[i]);
		}
	}

	/* not in table */
	return (NULL);
}


/*
 * .func	proc_type - process record type.
 * .desc	Process a record type. It is either as a number or a mnemonic.
 * .call	ret = proc_type(optstr).
 * .arg	optstr	- ptr to name or number.
 * .ret	0	- no errors detected.
 * .ret	-1	- error detected (error_str contains description).
 */
int
proc_type(char *optstr)
{
	struct au_event_ent	*aep;

	/*
	 * Either a number or a name.
	 */

	if (flags & M_TYPE) {
		error_str = gettext("'m' option specified multiple times");
		return (-1);
	}
	flags |= M_TYPE;
	m_type = 0;
	if (a_isnum(optstr, TRUE)) {
		if ((aep = getauevnam(optstr)) != NULL) {
			m_type = aep->ae_number;
		}
	} else {
		if ((aep = getauevnum((au_event_t)atoi(optstr))) != NULL) {
			m_type = aep->ae_number;
		}
	}
	if ((m_type == 0)) {
		set_err(gettext("invalid event (%s)"), optstr);
		return (-1);
	}
	return (0);
}


/*
 * .func	a_isnum - is it a number?
 * .desc	Determine if a string is a number or a name.
 *	A number may have a leading '+' or '-', but then must be
 *	all digits.
 * .call	ret = a_isnum(str).
 * .arg	str - ptr to the string.
 * .arg	leading	- TRUE if leading '+-' allowed.
 * .ret	0	- is a number.
 * .ret	1	- is not a number.
 */
int
a_isnum(char *str, int leading)
{
	char	*strs;

	if ((leading == TRUE) && (*str == '-' || *str == '+')) {
		strs = str + 1;
	} else {
		strs = str;
	}

	if (*strs == '\0' || strlen(strs) != strspn(strs, "0123456789")) {
		return (1);
	}

	return (0);
}


/*
 * .func	proc_id	- process user/group id's/
 * .desc	Process either a user number/name or group number/name.
 *	For names check to see if the name is active in the system
 *	to derive the number. If it is not active then fail. For a number
 *	also check to see if it is active, but only print a warning if it
 *	is not. An administrator may be looking at activity of a 'phantom'
 *	user.
 * .call	ret = proc_id(optstr, opt).
 * .arg	optstr	- ptr to name or number.
 * .arg	opt	- 'u' - audit user, 'e' - effective user, 'r' - real user,
 *		  'g' - group, 'f' - effective group.
 * .ret	0	- no errors detected.
 * .ret	-1	- error detected (error_str contains description).
 */
int
proc_id(char *optstr, int opt)
{
	switch (opt) {
	case 'e': 		/* effective user id */
		if (flags & M_USERE) {
			error_str = gettext(
			    "'e' option specified multiple times");
			return (-1);
		}
		flags |= M_USERE;
		return (proc_user(optstr, &m_usere));
		/* NOTREACHED */
	case 'f': 		/* effective group id */
		if (flags & M_GROUPE) {
			error_str = gettext(
			    "'f' option specified multiple times");
			return (-1);
		}
		flags |= M_GROUPE;
		return (proc_group(optstr, &m_groupe));
		/* NOTREACHED */
	case 'r': 		/* real user id */
		if (flags & M_USERR) {
			error_str = gettext(
			    "'r' option specified multiple times");
			return (-1);
		}
		flags |= M_USERR;
		return (proc_user(optstr, &m_userr));
		/* NOTREACHED */
	case 'u': 		/* audit user id */
		if (flags & M_USERA) {
			error_str = gettext(
			    "'u' option specified multiple times");
			return (-1);
		}
		flags |= M_USERA;
		return (proc_user(optstr, &m_usera));
		/* NOTREACHED */
	case 'g': 		/* real group id */
		if (flags & M_GROUPR) {
			error_str = gettext(
			    "'g' option specified multiple times");
			return (-1);
		}
		flags |= M_GROUPR;
		return (proc_group(optstr, &m_groupr));
		/* NOTREACHED */
	default: 		/* impossible */
		set_err(gettext("'%c' unknown option"), opt);
		return (-1);
		/* NOTREACHED */
	}
	/*NOTREACHED*/
}


int
proc_group(char *optstr, gid_t *gid)
{
	struct group	*grp;

	if ((grp = getgrnam(optstr)) == NULL) {
		if (!a_isnum(optstr, TRUE)) {
			*gid = (gid_t)atoi(optstr);
			return (0);
		}
		set_err(gettext("group name invalid (%s)"), optstr);
		return (-1);
	}
	*gid = grp->gr_gid;
	return (0);
}


int
proc_user(char *optstr, uid_t *uid)
{
	struct passwd	*usr;

	if ((usr = getpwnam(optstr)) == NULL) {
		if (!a_isnum(optstr, TRUE)) {
			*uid = (uid_t)atoi(optstr);
			return (0);
		}
		set_err(gettext("user name invalid (%s)"),
		    optstr == NULL ? "unspecified" : optstr);
		return (-1);
	}
	*uid = usr->pw_uid;
	return (0);
}


/*
 * .func proc_date - process date argument.
 * .desc Handle a date/time argument. See if the user has erred in combining
 *	the types of date arguments. Then parse the string and check for
 *	validity of each part.
 * .call	ret = proc_date(optstr, opt).
 * .arg	optstr	- ptr to date/time string.
 * .arg	opt	- 'd' for day, 'a' for after, or 'b' for before.
 * .ret	0	- no errors detected.
 * .ret	-1	- errors detected (error_str knows what it is).
 */
int
proc_date(char *optstr, int opt)
{
	static int	m_day = FALSE;

	if (opt == 'd') {
		if (m_day == TRUE) {
			error_str = gettext(
			    "'d' option may not be used with 'a' or 'b'");
			return (-1);
		}
		m_day = TRUE;
	}
	if ((opt == 'd') && (m_before || m_after)) {
		error_str = gettext(
		    "'d' option may not be used with 'a' or 'b'");
		return (-1);
	}
	if ((opt == 'a' || opt == 'b') && m_day) {
		error_str = gettext(
		    "'a' or 'b' option may not be used with 'd'");
		return (-1);
	}
	if ((opt == 'a') && (m_after != 0)) {
		error_str = gettext("'a' option specified multiple times");
		return (-1);
	}
	if ((opt == 'b') && (m_before != 0)) {
		error_str = gettext("'b' option specified multiple times");
		return (-1);
	}
	if (parse_time(optstr, opt) == -1) {
		return (-1);
	}
	return (0);
}


/*
 * .func	proc_class - process message class argument.
 * .desc	Process class type and see if it is for real.
 * .call	ret = proc_class(optstr).
 * .arg	optstr	- ptr to class.
 * .ret	0	- class has class.
 * .ret	-1	- class in no good.
 */
int
proc_class(char *optstr)
{
	if (flags & M_CLASS) {
		error_str = gettext("'c' option specified multiple times");
		return (-1);
	}
	flags |= M_CLASS;

	(void) getauditflagsbin(optstr, &mask);

	if (mask.am_success != mask.am_failure) {
		flags |= M_SORF;
	}

	return (0);
}


/*
 * .func process_fileopt - process command line file options.
 * .desc Process the command line file options and gather the specified files
 *	together in file groups based upon file name suffix. The user can
 *	specify files explicitly on the command line or via a directory.
 *	This is called after the command line flags are processed (as
 *	denoted by '-').
 * .call	ret = process_fileopt(argc, argv, optindex).
 * .arg	argc	- current value of argc.
 * .arg	argv	- current value of argv.
 * .arg	optindex- current index into argv (as setup by getopt()).
 * .ret	0	- no errors detected.
 * .ret	-1	- error detected (message already printed).
 */
int
process_fileopt(int argc, char **argv, int optindex)
{
	char	f_dr[MAXNAMLEN+1] = { '\0' };
	char	*f_dir = f_root;
	char	*fname;
	static char	*std = "standard input";
	audit_fcb_t *fcb;
	audit_pcb_t *pcb;

	/*
	 * Take input from stdin, not any files.
	 * Use a single fcb to do this.
	 */
	if (f_stdin) {
		fcb = a_calloc(1, sizeof (*fcb) + strlen(std));
		(void) strcpy(fcb->fcb_file, std);
		fcb->fcb_suffix = fcb->fcb_name = fcb->fcb_file;
		fcb->fcb_next = NULL;
		fcb->fcb_start = 0;
		fcb->fcb_end = MAXLONG;		/* forever */
		if ((pcb = get_next_pcb(NULL)) == NULL) {
			free(fcb);
			return (-1);
		}
		pcb->pcb_suffix = fcb->fcb_file;
		pcb->pcb_dfirst = pcb->pcb_first = fcb;	/* one-item list */
		pcb->pcb_dlast = pcb->pcb_last = fcb;
		pcb->pcb_cur = fcb;

	} else if (argc == optindex) {
		/*
		 * No files specified on the command line. If requested,
		 * process the specific directory.
		 */
		if (f_sdir) {
			if (strchr(f_sdir, '/')) {	/* given full path */
				f_dir = f_sdir;
			} else {		/* directory off audit root */
				if (catpath(f_dr, sizeof (f_dr), 3, f_root,
				    "/",  f_sdir) == -1) {
					(void) fprintf(stderr, gettext("%s "
					    "too long path - %s/%s\n"), ar,
					    f_root, f_sdir);
					return (-1);
				}
				f_dir = f_dr;
			}
		}
		/* Gather all of the files in the f_dir. */
		return (gather_dir(f_dir));

	} else {
		/* User specified filenames on the command line. */
		f_cmdline = TRUE;
		for (; optindex < argc; optindex++) {
			fname = argv[optindex];		/* get a filename */
			if (proc_file(fname, FALSE) == -1) {
				return (-1);
			}
		}
	}
	return (0);
}


/*
 * .func	gather_dir - gather a directory's files together.
 * .desc	Process all of the files in a specific directory. The files may
 *	be checked for adherence to the file name form at.
 *	If the directory can't be opened that is ok - just print
 *	a message and continue.
 * .call	ret = gather_dir(dir).
 * .arg	dir	- ptr to full pathname of directory.
 * .ret	0	- no errors detected.
 * .ret	-1	- error detected (message already printed).
 */
int
gather_dir(char *dir)
{
	char	fn[MAXNAMLEN+1];
	DIR 	*dirp;
	struct dirent	*dp;

	if ((dirp = opendir(dir)) == NULL) {
		if (errno != ENOTDIR) {
			set_err(gettext("%s could not open directory %s"), ar,
			    dir);
			perror(errbuf);
		}
		return (0);
	}
	for (dp = readdir(dirp); dp != NULL; dp = readdir(dirp)) {
		if (dp->d_name[0] == '.') {	/* can't see hidden files */
			continue;
		}
		if (catpath(fn, sizeof (fn), 3, dir, "/", dp->d_name) == -1) {
			(void) fprintf(stderr, gettext("%s too long path name "
			    "- directory:%s file:%s (skipping)\n"), ar, dir,
			    dp->d_name);
			continue;
		}
		if (proc_file(fn, TRUE) == -1) {
			(void) closedir(dirp);
			return (-1);
		}
	}
	(void) closedir(dirp);
	return (0);
}


/*
 * .func	proc_file - process a single candidate file.
 * .desc	Check out a file to see if it should be used in the merge.
 *	This includes checking the name (mode is TRUE) against the
 *	file format, checking access rights to the file, and thence
 *	getting and fcb and installing the fcb into the correct pcb.
 *	If the file fails then the fcb is not installed into a pcb
 *	and the file disappears from view.
 * .call	proc_file(fname, mode).
 * .arg	fname	- ptr to full path name of file.
 * .arg	mode	- TRUE if checking adherence to file name format.
 * .ret	0	- no fatal errors detected.
 * .ret	-1	- fatal error detected - quit altogether
 *		  (message already printed).
 */
int
proc_file(char *fname, int mode)
{
	int	reject = FALSE;
	struct stat	stat_buf;
	audit_fcb_t	*fcb, *fcbp, *fcbprev;
	audit_pcb_t	*pcb;

	/*
	 * See if it is a weird file like a directory or
	 * character special (around here?).
	 */
	if (stat(fname, &stat_buf)) {
		return (0);
	}
	if (!S_ISREG(stat_buf.st_mode)) {
		return (0);
	}

	/* Allocate a new fcb to hold fcb and full filename. */
	fcb = a_calloc(1, sizeof (*fcb) + strlen(fname));
	(void) strcpy(fcb->fcb_file, fname);
	if (check_file(fcb, mode) == -1) { /* check file name */
		if (!f_quiet && f_verbose) {
			(void) fprintf(stderr, "%s %s:\n  %s.\n", ar,
			    error_str, fname);
		}
		reject = TRUE;
	} else {
		/*
		 * Check against file criteria.
		 * Check finish-time here, and start-time later on
		 * while processing.
		 * This is because the start time on a file can be after
		 * the first record(s).
		 */
		if (f_complete && (fcb->fcb_flags & FF_NOTTERM) && !f_cmdline ||
		    !f_all && (fcb->fcb_end < m_after)) {
			reject = TRUE;
		}
		if (f_machine != NULL) {
			if (strlen(fcb->fcb_suffix) != strlen(f_machine) ||
			    (strcmp(fcb->fcb_suffix, f_machine) != 0)) {
				reject = TRUE;
			}
		}
	}

	if (reject == FALSE) {
		filenum++;	/* count of total files to be processed */
		fcb->fcb_next = NULL;
		if ((pcb = get_next_pcb(fcb->fcb_suffix)) == NULL) {
			free(fcb);
			return (-1);
		}
		/* Place FCB into the PCB in order - oldest first.  */
		fcbp = pcb->pcb_first;
		fcbprev = NULL;
		while (fcbp != NULL) {
			if (fcb->fcb_start < fcbp->fcb_start) {
				if (fcbprev != NULL) {
					fcbprev->fcb_next = fcb;
				} else {
					pcb->pcb_dfirst = pcb->pcb_first = fcb;
				}
				fcb->fcb_next = fcbp;
				break;
			}
			fcbprev = fcbp;
			fcbp = fcbp->fcb_next;
		}
		/* younger than all || empty list */
		if (fcb->fcb_next == NULL) {
			if (pcb->pcb_first == NULL) {
				pcb->pcb_dfirst = pcb->pcb_first = fcb;
			}
			pcb->pcb_dlast = pcb->pcb_last = fcb;
			if (fcbprev != NULL) {
				fcbprev->fcb_next = fcb;
			}
		}

	} else {
		free(fcb);		/* rejected */
	}
	return (0);
}


/*
 * .func	check_file - check filename and setup fcb.
 * .desc	Check adherence to the file format (do_check is TRUE) and setup
 *	the fcb with useful information.
 *	filename format: yyyymmddhhmmss.yyyymmddhhmmss.suffix
 *			 yyyymmddhhmmss.not_terminated.suffix
 *	If do_check is FALSE then still see if the filename does confirm
 *	to the format. If it does then extract useful information from
 *	it (start time and end time).  But if it doesn't then don't print
 *	any error messages.
 * .call	ret = check_file(fcb, do_check).
 * .arg	fcb	- ptr to fcb that holds the file.
 * .arg	do_check - if TRUE do check adherence to file format.
 * .ret	0	- no errors detected.
 * .ret	-1	- file failed somehow (error_str tells why).
 */
int
check_file(audit_fcb_t *fcb, int do_check)
{
	int	ret = -1;
	char	errb[sizeof (errbuf)];		/* build error message */
	char	*namep, *slp;
	struct tm	tme;

	/* get just the filename */
	for (slp = namep = fcb->fcb_file; *namep != '\0'; namep++) {
		if (*namep == '/') {
			slp = namep + 1; /* slp -> the filename itself */
		}
	}
	if (do_check == FALSE) {
		fcb->fcb_end = MAXLONG;		/* forever */
		fcb->fcb_suffix = NULL;
		fcb->fcb_name = slp;
		ret = 0;
	}

	/* get working copy of filename */
	if (strlen(slp) < 31 || slp[14] != '.' || slp[29] != '.') {
		error_str = gettext("invalid filename format");
		return (ret);
	}
	namep = a_calloc(1, strlen(slp) + 1);
	(void) strcpy(namep, slp);
	namep[14] = '\0';			/* mark off start time */
	namep[29] = '\0';			/* mark off finish time */
	if (derive_date(namep, &tme) == -1) {
		(void) strcpy(errb, errbuf);
		set_err(gettext("starting time-stamp invalid - %s"), errb);
		error_str = errbuf;
		free(namep);
		return (ret);
	}
	/*
	 * Keep start time from filename. Use it to order files in
	 * the file list. Later we will update this when we read
	 * the first record from the file.
	 */
	fcb->fcb_start = tm_to_secs(&tme);

	if (strcmp(&namep[15], "not_terminated") == 0) {
		fcb->fcb_end = MAXLONG;		/* forever */
		/*
		 * Only treat a 'not_terminated' file as such if
		 * it is not on the command line.
		 */
		if (do_check == TRUE) {
			fcb->fcb_flags |= FF_NOTTERM;
		}
	} else if (derive_date(&namep[15], &tme) == -1) {
		(void) strcpy(errb, errbuf);
		set_err(gettext("ending time-stamp invalid - %s"), errb);
		free(namep);
		return (ret);
	} else {
		fcb->fcb_end = tm_to_secs(&tme);
	}
	fcb->fcb_name = slp;
	fcb->fcb_suffix = &slp[30];
	free(namep);
	return (0);
}


/*
 * .func get_next_pcb - get a pcb to use.
 * .desc	The pcb's in the array audit_pcbs are used to hold single file
 *	groups in the form of a linked list. Each pcb holds files that
 *	are tied together by a common suffix in the file name. Here we
 *	get either 1. the existing pcb holding a specified suffix or
 *	2. a new pcb if we can't find an existing one.
 * .call	pcb = get_next_pcb(suffix).
 * .arg	suffix	- ptr to suffix we are seeking.
 * .ret	pcb	- ptr to pcb that hold s the sought suffix.
 * .ret	NULL- serious failure in memory allocation. Quit processing.
 */
audit_pcb_t *
get_next_pcb(char *suffix)
{
	int	i = 0;
	size_t	zerosize;
	size_t	size;
	audit_pcb_t	*pcb;

	/* Search through (maybe) entire array. */
	while (i < pcbsize) {
		pcb = &audit_pcbs[i++];
		if (pcb->pcb_first == NULL) {
			proc_pcb(pcb, suffix, i);
			return (pcb);	/* came to an unused one */
		}
		if (suffix) {
			if (strcmp(pcb->pcb_suffix, suffix) == 0) {
				return (pcb);	/* matched one with suffix */
			}
		}
	}
	/*
	 * Uh-oh, the entire array is used and we haven't gotten one yet.
	 * Allocate a bigger array.
	 */
	pcbsize += PCB_INC;
	size = pcbsize * sizeof (audit_pcb_t);
	zerosize = size - ((pcbsize - PCB_INC) * sizeof (audit_pcb_t));
	if ((audit_pcbs = realloc(audit_pcbs, size)) == NULL) {
		set_err(gettext("%s memory reallocation failed (%d bytes)"), ar,
		    size);
		perror(errbuf);
		audit_stats();		/* give user statistics on usage */
		return (NULL);		/* really bad thing to have happen */
	}
	(void) memset(&audit_pcbs[pcbsize-PCB_INC], 0, zerosize);
	pcb = &audit_pcbs[pcbsize-PCB_INC];	/* allocate the first new one */
	proc_pcb(pcb, suffix, pcbsize - PCB_INC);
	return (pcb);
}


/*
 * .func proc_pcb - process pcb.
 * .desc	Common pcb processing for above routine.
 * .call	proc_pcb(pcb, suffix, i).
 * .arg	pcb	- ptr to pcb.
 * .arg	suffix	- prt to suffix tha t ties this group together.
 * .arg	i	- index into audit_pcbs[ ].
 * .ret	void.
 */
void
proc_pcb(audit_pcb_t *pcb, char *suffix, int i)
{
	if (suffix != NULL) {
		pcb->pcb_suffix = suffix;
	}
	pcbnum++;	/* one more pcb in use */
	pcb->pcb_size = AUDITBUFSIZE;
	pcb->pcb_rec = a_calloc(1, AUDITBUFSIZE);
	pcb->pcb_time = -1;
	pcb->pcb_flags |= PF_USEFILE;	/* note this one controls files */
	pcb->pcb_procno = i;	/* save index into audit_pcbs [] for id */
}


/*
 * .func	proc_label - process label range argument.
 * .desc	Parse label range lower-bound[;upper-bound]
 * .call	ret = proc_label(optstr).
 * .arg	opstr	- ptr to label range string
 * .ret 0	- no errors detected.
 * .ret -1	- errors detected (error_str set).
 */

int
proc_label(char *optstr)
{
	char	*p;
	int	error;

	if (flags & M_LABEL) {
		error_str = gettext("'l' option specified multiple times");
		return (-1);
	}
	flags |= M_LABEL;

	if ((m_label = malloc(sizeof (m_range_t))) == NULL) {
		return (-1);
	}
	m_label->lower_bound = NULL;
	m_label->upper_bound = NULL;

	p = strchr(optstr, ';');
	if (p == NULL) {
		/* exact label match, lower and upper range bounds the same */
		if (str_to_label(optstr, &m_label->lower_bound, MAC_LABEL,
		    L_NO_CORRECTION, &error) == -1) {
			set_err(gettext("invalid sensitivity label (%s) "
			    "err %d"), optstr, error);
			goto errout;
		}
		m_label->upper_bound = m_label->lower_bound;
		return (0);
	}
	if (p == optstr) {
		/* lower bound is not specified .. default is admin_low */
		if (str_to_label(ADMIN_LOW, &m_label->lower_bound, MAC_LABEL,
		    L_NO_CORRECTION, &error) == -1) {
			goto errout;
		}

		p++;
		if (*p == '\0') {
			/* upper bound not specified .. default is admin_high */
			if (str_to_label(ADMIN_HIGH, &m_label->upper_bound,
			    MAC_LABEL, L_NO_CORRECTION, &error) == -1) {
				goto errout;
			}
		} else {
			if (str_to_label(p, &m_label->upper_bound, MAC_LABEL,
			    L_NO_CORRECTION, &error) == -1) {
				set_err(gettext("invalid sensitivity label "
				    "(%s) err %d"), p, error);
				goto errout;
			}
		}
		return (0);
	}
	*p++ = '\0';
	if (str_to_label(optstr, &m_label->lower_bound, MAC_LABEL,
	    L_NO_CORRECTION, &error) == -1) {
		set_err(gettext("invalid sensitivity label (%s) err %d"),
		    optstr, error);
		goto errout;
	}
	if (*p == '\0') {
		/* upper bound is not specified .. default is admin_high */
		if (str_to_label(ADMIN_HIGH, &m_label->upper_bound,
		    MAC_LABEL, L_NO_CORRECTION, &error) == -1) {
			goto errout;
		}
	} else {
		if (str_to_label(p, &m_label->upper_bound, MAC_LABEL,
		    L_NO_CORRECTION, &error) == -1) {
			set_err(gettext("invalid sensitivity label (%s) "
			    "err %d"), p, error);
			goto errout;
		}
	}
	/* make sure that upper bound dominates the lower bound */
	if (!bldominates(m_label->upper_bound, m_label->lower_bound)) {
		*--p = ';';
		set_err(gettext("invalid sensitivity label range (%s)"),
		    optstr);
		goto errout;
	}
	return (0);

errout:
	m_label_free(m_label->upper_bound);
	m_label_free(m_label->lower_bound);
	free(m_label);

	return (-1);
}

/*
 * proc_zonename - pick up zone name.
 *
 * all non-empty and not-too-long strings are valid since any name
 * may be valid.
 *
 * ret 0:	non-empty string
 * ret -1:	empty string or string is too long.
 */
static int
proc_zonename(char *optstr)
{
	size_t	length = strlen(optstr);
	if ((length < 1) || (length > ZONENAME_MAX)) {
		set_err(gettext("invalid zone name: %s"), optstr);
		return (-1);
	}
	zonename = strdup(optstr);
	flags |= M_ZONENAME;
	return (0);
}

/*
 * proc_frmi - set up frmi for pattern matching.
 *	Logic ripped off of scf_walk_fmri()
 *		Thanks to the smf team.
 *
 * ret 0:	OK
 * ret -1:	error
 */
static int
proc_fmri(char *optstr)
{
	if (strpbrk(optstr, "*?[") != NULL) {
		/* have a pattern to glob for */

		fmri.sp_type = PATTERN_GLOB;
		if (optstr[0] == '*' ||
		    (strlen(optstr) >= 4 && optstr[3] == ':')) {
			fmri.sp_arg = strdup(optstr);
		} else if ((fmri.sp_arg = malloc(strlen(optstr) + 6)) != NULL) {
			(void) snprintf(fmri.sp_arg, strlen(optstr) + 6,
			    "svc:/%s", optstr);
		}
	} else {
		fmri.sp_type = PATTERN_PARTIAL;
		fmri.sp_arg = strdup(optstr);
	}
	if (fmri.sp_arg == NULL) {
		return (-1);
	}

	return (0);
}

/*
 * catpath - concatenate the path string
 *
 * ret 0:	OK
 * ret -1:	error
 */
static int
catpath(char *p_buf, int p_buf_sz, int argc, ...)
{
	va_list	vl;

	*p_buf = '\0';
	va_start(vl, argc);
	while (argc-- > 0) {
		if (strlcat(p_buf, va_arg(vl, char *), p_buf_sz) >= p_buf_sz) {
			return (-1);
		}
	}
	va_end(vl);

	return (0);
}

/*
 * validate_optarg - validate the contents of optarg - format string attacks.
 */
static int
validate_optarg(char *optarg)
{
	if (strstr(optarg, "%n") != NULL) {
		set_err("'%%n' string cannot be part of an argument");
		return (-1);
	}

	return (0);
}
