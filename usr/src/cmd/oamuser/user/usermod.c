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



#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
#include <string.h>
#include <userdefs.h>
#include <user_attr.h>
#include <errno.h>
#include <project.h>
#include <getopt.h>
#include "users.h"
#include "messages.h"
#include "funcs.h"
#include <libintl.h>
#include <syslog.h>
#include <auth_list.h>
#include <sys/mount.h>
#include <netdb.h>
#include "cmds.h"


/*
 *  usermod [-u uid [-o] | -g group | -G group [[,group]...] | -d dir [-m]
 *		| -s shell | -c comment | -l new_logname]
 *		| -f inactive | -e expire ]
 *		[ -A authorization [, authorization ...]]
 *		[ -P profile [, profile ...]]
 *		[ -R role [, role ...]]
 *		[ -K key=value ]
 *		[ -p project [, project]] [ -S [files|ldap]]  login
 *
 *	This command adds new user logins to the system.  Arguments are:
 *
 *	uid - an integer less than MAXUID
 *	group - an existing group's integer ID or char string name
 *	dir - a directory
 *	shell - a program to be used as a shell
 *	comment - any text string
 *	skel_dir - a directory
 *	base_dir - a directory
 *	rid - an integer less than 2**16 (USHORT)
 *	login - a string of printable chars except colon (:)
 *	inactive - number of days a login maybe inactive before it is locked
 *	expire - date when a login is no longer valid
 *	authorization - One or more comma separated authorizations defined
 *			in auth_attr(4).
 *	profile - One or more comma separated execution profiles defined
 *		  in prof_attr(4)
 *	role - One or more comma-separated role names defined in user_attr(4)
 *	key=value - One or more -K options each specifying a valid user_attr(4)
 *		attribute.
 *
 */

static uid_t uid; /* new uid */
static gid_t gid; /* gid of new login */
static char *new_logname = NULL; /* new login name with -l option */
static char *uidstr = NULL; /* uid from command line */
static char *group = NULL; /* group from command line */
static char *grps = NULL; /* multi groups from command line */
static char *dir = NULL; /* home dir from command line */
static char *shell = NULL; /* shell from command line */
static char *comment = NULL; /* comment from command line */
static char *logname = NULL; /* login name to add */
static char *inactstr = NULL; /* inactive from command line */
static char *expire = NULL; /* expiration date from command line */
static char *projects = NULL; /* project ids from command line */
static char *usertype;

char *cmdname;
static char gidstring[32], uidstring[32];
char inactstring[10];

static char *repository = NSS_REP_FILES;

char *
strcpmalloc(char *str)
{
	if (str == NULL)
		return (NULL);

	return (strdup(str));
}

struct passwd *
passwd_cpmalloc(struct passwd *opw)
{
	struct passwd *npw;

	if (opw == NULL)
		return (NULL);


	npw = malloc(sizeof (struct passwd));

	npw->pw_name = strcpmalloc(opw->pw_name);
	npw->pw_passwd = strcpmalloc(opw->pw_passwd);
	npw->pw_uid = opw->pw_uid;
	npw->pw_gid = opw->pw_gid;
	npw->pw_age = strcpmalloc(opw->pw_age);
	npw->pw_comment = strcpmalloc(opw->pw_comment);
	npw->pw_gecos = strcpmalloc(opw->pw_gecos);
	npw->pw_dir = strcpmalloc(opw->pw_dir);
	npw->pw_shell = strcpmalloc(opw->pw_shell);

	return (npw);
}

int
main(int argc, char *argv[])
{
	int ch, ret = EX_SUCCESS, call_pass = 0, oflag = 0;
	int tries, mflag = 0, inact,  flag = 0;
	boolean_t fail_if_busy = B_FALSE;
	char *ptr;
	struct passwd *pstruct; /* password struct for login */
	struct passwd *pw = NULL;
	struct group *g_ptr; /* validated group from -g */
	struct stat statbuf; /* status buffer for stat */
	int warning;
	struct group_entry **gidlist;
	projid_t *projlist;
	char **nargv; /* arguments for execvp of passmgmt */
	int argindex; /* argument index into nargv */
	userattr_t *ua = NULL;
	char *val;
	int role; /* current account is role */
	sec_repository_t *rep = NULL;
	sec_repository_t *nss_rep = NULL;
	nss_XbyY_buf_t *b = NULL;
	struct userdefs *usrdefs;
	int create_home = 0;
	int old_home_exists = 0;
	char homedir[ PATH_MAX + 1 ];
	char old_auto_home[MAXPATHLEN + MAXHOSTNAMELEN + 2];
	char *old_path;
	char *autohomedir = NULL;
	int loggedin = 0;
	int rep_count = 0;
	int count = 0;
	char *path = NULL;
	char assignauth[NSS_LINELEN_GROUP];
	char opchar = OP_REPLACE_CHAR;


	cmdname = argv[0];

	opterr = 0; /* no print errors from getopt */
	/* get user type based on the program name */
	usertype = getusertype(argv[0]);



	while ((ch = getopt(argc, argv,
	    "c:d:e:f:G:g:l:mop:s:u:A:P:R:K:S:")) != EOF)
		switch (ch) {
		case 'c':
			comment = optarg;
			flag++;
			break;
		case 'd':
			dir = optarg;
			fail_if_busy = B_TRUE;
			flag++;
			break;
		case 'e':
			expire = optarg;
			flag++;
			break;
		case 'f':
			inactstr = optarg;
			flag++;
			break;
		case 'G':
			grps = optarg;
			flag++;
			break;
		case 'g':
			group = optarg;
			fail_if_busy = B_TRUE;
			flag++;
			break;
		case 'l':
			new_logname = optarg;
			fail_if_busy = B_TRUE;
			flag++;
			break;
		case 'm':
			mflag++;
			flag++;
			fail_if_busy = B_TRUE;
			break;
		case 'o':
			oflag++;
			flag++;
			fail_if_busy = B_TRUE;
			break;
		case 'p':
			projects = optarg;
			flag++;
			break;
		case 's':
			shell = optarg;
			flag++;
			break;
		case 'u':
			uidstr = optarg;
			flag++;
			fail_if_busy = B_TRUE;
			break;
		case 'A':
			change_key(USERATTR_AUTHS_KW, optarg, NULL);
			flag++;
			break;
		case 'P':
			change_key(USERATTR_PROFILES_KW, optarg, NULL);
			flag++;
			break;
		case 'R':
			change_key(USERATTR_ROLES_KW, optarg, usertype);
			flag++;
			break;
		case 'K':
			change_key(NULL, optarg, usertype);
			flag++;
			break;
		case 'S':
			repository = optarg;
			rep_count ++;
			if (rep_count > 1) {
				errmsg(M_MULTI_DEFINED, "-S");
				exit(EX_SYNTAX);
			}
			ret = get_repository_handle(repository, &rep);
			if (ret == SEC_REP_NOREP) {
				exit(EX_SYNTAX);
			} else if (ret == SEC_REP_SYSTEM_ERROR) {
				exit(EX_FAILURE);
			}
			break;
		default:
		case '?':
			if (is_role(usertype))
				errmsg(M_MRUSAGE);
			else
				errmsg(M_MUSAGE);
			exit(EX_SYNTAX);
		}


	if (optind != argc - 1 || flag == 0) {
		if (is_role(usertype))
			errmsg(M_MRUSAGE);
		else
			errmsg(M_MUSAGE);
		exit(EX_SYNTAX);
	}

	if (rep_count == 0) {
		ret = find_in_nss(SEC_REP_DB_PASSWD, argv[optind], &rep);
		if (ret == SEC_REP_NOT_FOUND) {
			errmsg(M_EXIST, argv[optind], "");
			exit(EX_NAME_NOT_EXIST);
		} else if (ret == SEC_REP_SYSTEM_ERROR) {
			exit(EX_FAILURE);
		}
		if (rep != NULL && rep->type == SEC_REP_LDAP) {
			repository = NSS_REP_LDAP;
		} else if (rep != NULL && rep->type == SEC_REP_FILES) {
			repository = NSS_REP_FILES;
		}
	}

	if (get_repository_handle(NULL, &nss_rep) != SEC_REP_SUCCESS) {
		exit(EX_FAILURE);
	}

	if ((!uidstr && oflag) || (mflag && !dir)) {
		if (is_role(usertype))
			errmsg(M_MRUSAGE);
		else
			errmsg(M_MUSAGE);
		exit(EX_SYNTAX);
	}

	usrdefs = (struct userdefs *)getusrdef(usertype);

	logname = argv[optind];

	/* Determine whether the account is a role or not */
	init_nss_buffer(SEC_REP_DB_USERATTR, &b);
	ret = rep->rops->get_usernam(logname, &ua, b);

	if ((ua == NULL) ||
	    (val = kva_match(ua->attr, USERATTR_TYPE_KW)) == NULL ||
	    strcmp(val, USERATTR_TYPE_NONADMIN_KW) != 0)
		role = 0;
	else
		role = 1;

	if (ua != NULL && ua->res1 != NULL && strcmp(ua->res1, "RO") == 0) {
		errmsg(M_ACCOUNT_READONLY);
		errmsg(M_PERM_DENIED);
		exit(EX_NO_PERM);
	}

	if (process_add_remove(ua) == -1) {
		exit(EX_BADARG);
		/*NOTREACHED*/
	}
	process_change_key(nss_rep);

	free(ua);
	free_nss_buffer(&b);


	if (is_role(usertype)) {
		/* Roles can't have roles */
		if (getsetdefval(USERATTR_ROLES_KW, NULL) != NULL) {
			errmsg(M_MRUSAGE);
			exit(EX_SYNTAX);
		}
		/* If it was an ordinary user, delete its roles */
		if (!role)
			change_key(USERATTR_ROLES_KW, "", NULL);
	}


	init_nss_buffer(SEC_REP_DB_PASSWD, &b);
	ret = rep->rops->get_pwnam(logname, &pw, b);
	if (ret == SEC_REP_NOT_FOUND || !(pw)) {
		errmsg(M_EXIST, logname, repository);
		exit(EX_NAME_NOT_EXIST);
	}

	/* Verify that rolemod is used for roles and usermod for users */

	if (role != is_role(usertype)) {
		if (role)
			errmsg(M_ISROLE);
		else
			errmsg(M_ISUSER);
		exit(EX_SYNTAX);
	}

	if (pw->pw_uid <= DEFRID) {
		if (check_user_authorized() < 0) {
			errmsg(M_RESERVED, pw->pw_uid);
			errmsg(M_PERM_DENIED);
			exit(EX_NO_AUTH);
		}
	}

	pstruct = passwd_cpmalloc(pw);

	/*
	 * We can't modify a logged in user if any of the following
	 * are being changed:
	 * uid (-u & -o), group (-g), home dir (-m), loginname (-l).
	 * If none of those are specified it is okay to go ahead
	 * some types of changes only take effect on next login, some
	 * like authorisations and profiles take effect instantly.
	 * One might think that -K type=role should require that the
	 * user not be logged in, however this would make it very
	 * difficult to make the root account a role using this command.
	 */
	if (isbusy(logname)) {
		if (fail_if_busy) {
			errmsg(M_BUSY, logname, "change");
			exit(EX_BUSY);
		}
		loggedin = 1;
	}


	if (new_logname != NULL && strcmp(new_logname, logname)) {
		free(pw);
		switch (valid_login_check(new_logname, &pw, &warning, rep, b)) {
		case INVALID:
			errmsg(M_INVALID, new_logname, "login name");
			exit(EX_BADARG);
			/*NOTREACHED*/

		case NOTUNIQUE:
			errmsg(M_USED, new_logname);
			exit(EX_NAME_EXISTS);
			/*NOTREACHED*/
		default:
			call_pass = 1;
			break;
		}
		if (warning)
			warningmsg(warning, logname);
		free(pw);
	}

	free_nss_buffer(&b);


	if (uidstr) {
		/* convert uidstr to integer */
		errno = 0;
		uid = (uid_t)strtol(uidstr, &ptr, (int)10);
		if (*ptr || errno == ERANGE) {
			errmsg(M_INVALID, uidstr, "user id");
			exit(EX_BADARG);
		}

		if (uid != pstruct->pw_uid) {
			init_nss_buffer(SEC_REP_DB_PASSWD, &b);
			switch (valid_uid_check(uid, &pw, rep, b)) {
			case NOTUNIQUE:
				if (!oflag) {
					/* override not specified */
					errmsg(M_UID_USED, uid);
					exit(EX_ID_EXISTS);
				}
				if (check_user_authorized() < 0) {
					errmsg(M_UID_USED, uid);
					errmsg(M_PERM_DENIED);
					exit(EX_NO_AUTH);
				}
				break;
			case RESERVED:
				errmsg(M_RESERVED, uid);
				if (check_user_authorized() < 0) {
					errmsg(M_PERM_DENIED);
					exit(EX_NO_AUTH);
				}
				break;
			case TOOBIG:
				errmsg(M_TOOBIG, "uid", uid);
				exit(EX_BADARG);
				break;
			case INVALID:
				errmsg(M_INVALID, uidstr, "uid");
				exit(EX_BADARG);
				break;
			}
			free(pw);
			free_nss_buffer(&b);
			call_pass = 1;

		} else {
			/* uid's the same, so don't change anything */
			uidstr = NULL;
			oflag = 0;
		}

	} else uid = pstruct->pw_uid;

	if ((pw = getpwuid(getuid())) == NULL) {
		errmsg(M_GETUID);
		errmsg(M_UPDATE_MODIFIED);
		exit(EX_UPDATE);
	}

	if (group) {
		init_nss_buffer(SEC_REP_DB_GROUP, &b);
		switch (valid_group_check(group, &g_ptr, &warning, rep, b)) {
		case INVALID:
			errmsg(M_INVALID, group, "group id");
			exit(EX_BADARG);
			/*NOTREACHED*/
		case TOOBIG:
			errmsg(M_TOOBIG, "gid", group);
			exit(EX_BADARG);
			/*NOTREACHED*/
		case UNIQUE:
			errmsg(M_GRP_NOTUSED, group);
			exit(EX_NAME_NOT_EXIST);
			/*NOTREACHED*/
		case RESERVED:
			gid = (gid_t)strtol(group, &ptr, (int)10);
			errmsg(M_RESERVED_GID, gid);
			break;
		}
		free(g_ptr);
		free_nss_buffer(&b);
		if (warning)
			warningmsg(warning, group);

		if (g_ptr != NULL) {
			gid = g_ptr->gr_gid;
		} else {
			gid = pstruct->pw_gid;
		}

		/* call passmgmt if gid is different, else ignore group */
		if (gid != pstruct->pw_gid)
			call_pass = 1;
		else group = NULL;

	} else gid = pstruct->pw_gid;

	if (grps && *grps) {
		init_nss_buffer(SEC_REP_DB_GROUP, &b);

		/* Check if +/- operation is specified */
		if (grps[0] == OP_ADD_CHAR || grps[0] == OP_SUBTRACT_CHAR) {
			opchar = grps[0];
			grps++; /* skip the operator */
		}

		if (!(gidlist = valid_lgroup(grps, gid, rep, b)))
			exit(EX_BADARG);

		if (update_gids(gidlist, logname, opchar) == -1) {
			errmsg(M_INVALID, grps, "argument");
			exit(EX_BADARG);
		}

		free_nss_buffer(&b);

	} else {
		gidlist = (struct group_entry **)0;
	}


	if (projects && *projects) {
		init_nss_buffer(SEC_REP_DB_PROJECT, &b);
		if (!(projlist =
		    (projid_t *)valid_lproject(projects, rep, b)))
			exit(EX_BADARG);

		free_nss_buffer(&b);

	} else
		projlist = (projid_t *)0;

	snprintf(homedir, sizeof (homedir),
	    "/home/%s", (new_logname ? new_logname : logname));

	/* check to see if auto_home entry exists. */
	old_auto_home[0] = '\0';
	if (rep->rops->get_autohome(logname, &old_auto_home[0])
	    == EX_SUCCESS && old_auto_home[0] != '\0') {
		char *ptr = NULL;

		if (valid_dir_input(old_auto_home, &old_path) == 1) {
			if ((ptr = strchr(old_path, ':')) != NULL)
				ptr++;
			else
				ptr = old_path;
			old_path = ptr;
			if (stat(old_path, &statbuf) == 0) {
				old_home_exists = 1;
			}
		}
	}

	/*
	 * Use old directory as template for new one if not supplied
	 */
	if (new_logname && dir == NULL) {
		char *old_dir;

		old_dir = strrchr(old_auto_home, '/');
		if (old_dir != NULL) {
			size_t offset;

			old_dir++;
			offset = old_dir - old_auto_home;
			if (strcmp(logname, old_dir) == 0)
			strncpy(old_dir, new_logname,
			    sizeof (old_auto_home) - offset);
			dir = old_auto_home;
		}
	}

	/*
	 * Doing the authorization check here because
	 * if only -d option was changed then passmgmt
	 * will not be called to check auths.
	 */
	if (dir != NULL) {
		char *auth;

		if (role) auth = ROLE_MANAGE_AUTH;
		else auth = USER_MANAGE_AUTH;

		if (chkauthattr(auth, pw->pw_name) == 0) {
			errmsg(M_AUTOHOME_SET, auth);
			errmsg(M_PERM_DENIED);
			exit(EX_NO_AUTH);
		}

		/*
		 * create_home will be set to 1 when the hostname
		 * in the -d argument is the same as the local
		 * machine.
		 */

		create_home = valid_dir_input(dir, &autohomedir);
		switch (create_home) {
		case -1:
			errmsg(M_RELPATH, dir);
			exit(EX_BADARG);
			break;
		case 1:
			path = strchr(autohomedir, ':');
			if (path)
				path++;
			else
				path = autohomedir;

			if (stat(path, &statbuf) == 0) {
				/* Home directory exists */
				if (check_perm(statbuf, pstruct->pw_uid,
				    pstruct->pw_gid, S_IWOTH | S_IXOTH) != 0) {
					errmsg(M_NO_PERM, logname, dir);
					exit(EX_NO_PERM);

				}
				mflag = 0;
			}
			break;
		default:
			break;
		}

	}

	if (shell) {
		if (REL_PATH(shell)) {
			errmsg(M_RELPATH, shell);
			exit(EX_BADARG);
		}
		if (strcmp(pstruct->pw_shell, shell) == 0) {
			/* ignore s option if shell is not different */
			shell = NULL;
		} else {
			if (stat(shell, &statbuf) < 0 ||
			    (statbuf.st_mode & S_IFMT) != S_IFREG ||
			    (statbuf.st_mode & 0555) != 0555) {

				errmsg(M_INVALID, shell, "shell");
				exit(EX_BADARG);
			}

			call_pass = 1;
		}
	}

	if (comment)
		/* ignore comment if comment is not changed */
		if (strcmp(pstruct->pw_comment, comment))
			call_pass = 1;
		else
			comment = NULL;

	/* inactive string is a positive integer */
	if (inactstr) {
		/* convert inactstr to integer */
		inact = (int)strtol(inactstr, &ptr, 10);
		if (*ptr || inact < 0) {
			errmsg(M_INVALID, inactstr, "inactivity period");
			exit(EX_BADARG);
		}
		call_pass = 1;
	}

	/* expiration string is a date, newer than today */
	if (expire) {
		if (*expire &&
		    valid_expire(expire, (time_t *)0) == INVALID) {
			errmsg(M_INVALID, expire, "expiration date");
			exit(EX_BADARG);
		}
		call_pass = 1;
	}

	if (grps) {
		/*
		 * Need to have either solaris.group.assign or
		 * solaris.group.assign/<groupname> for each group
		 */
		if (chkauthattr(GROUP_ASSIGN_AUTH, pw->pw_name) == 0) {
			count = 0;
			while (gidlist[count] != NULL) {
				(void) snprintf(assignauth, sizeof (assignauth),
				    "%s/%s", GROUP_ASSIGN_AUTH,
				    gidlist[count]->group_name);
				if (chkauthattr(assignauth, pw->pw_name) == 0) {
					errmsg(M_GROUP_ASSIGN, assignauth);
					errmsg(M_PERM_DENIED);
					exit(EX_NO_AUTH);
				}
				count++;
			}
		}


		/* redefine login's supplementary group memberships */
		ret = rep->rops->edit_groups(logname, new_logname,
		    gidlist, 1);
		if (ret != EX_SUCCESS) {
			errmsg(M_UPDATE_GROUP);
			errmsg(M_UPDATE_MODIFIED);
			exit(EX_GRP_UPDATE);
		}
	}
	if (projects) {
		if (chkauthattr(PROJECT_ASSIGN_AUTH,
		    pw->pw_name) == 0) {
			errmsg(M_PROJECT_ASSIGN, PROJECT_ASSIGN_AUTH);
			errmsg(M_PERM_DENIED);
			exit(EX_NO_AUTH);
			/* need to check if member of project */
		}

		ret = rep->rops->edit_projects(logname, (char *)NULL,
		    projlist, 1);
		if (ret != EX_SUCCESS) {
			errmsg(M_UPDATE_PROJECT);
			exit(EX_PROJ_UPDATE);
		}
	}

	if (nkeys > 0)
		call_pass = 1;


	/* that's it for validations - now do the work */

	if (call_pass) {

		/* only get to here if need to call passmgmt */
		/* set up arguments to  passmgmt in nargv array */
		nargv = malloc((30 + nkeys * 2) * sizeof (char *));

		argindex = 0;
		nargv[argindex++] = PASSMGMT;
		nargv[argindex++] = "-m"; /* modify */

		if (comment) { /* comment */
			nargv[argindex++] = "-c";
			nargv[argindex++] = comment;
		}

		/* flags for home directory */
		if (new_logname) {

			nargv[argindex++] = "-h";
			nargv[argindex++] = homedir;

			/* redefine login name */
			nargv[argindex++] = "-l";
			nargv[argindex++] = new_logname;
		}

		if (group) {
			/* set gid flag */
			nargv[argindex++] = "-g";
			(void) sprintf(gidstring, "%u", gid);
			nargv[argindex++] = gidstring;
		}

		if (shell) { /* shell */
			nargv[argindex++] = "-s";
			nargv[argindex++] = shell;
		}

		if (inactstr) {
			nargv[argindex++] = "-f";
			nargv[argindex++] = inactstr;
		}

		if (expire) {
			nargv[argindex++] = "-e";
			nargv[argindex++] = expire;
		}

		if (uidstr) { /* set uid flag */
			nargv[argindex++] = "-u";
			(void) sprintf(uidstring, "%u", uid);
			nargv[argindex++] = uidstring;
		}

		if (oflag) nargv[argindex++] = "-o";

		if (repository) {
			nargv[argindex++] = "-S";
			nargv[argindex++] = repository;
		}

		if (nkeys > 0)
			addkey_args(nargv, &argindex);

		/* finally - login name */
		nargv[argindex++] = logname;

		/* set the last to null */
		nargv[argindex++] = NULL;

		/* now call passmgmt */
		ret = PEX_FAILED;
		for (tries = 3; ret != PEX_SUCCESS && tries--; ) {
			switch (ret = call_passmgmt(nargv)) {
			case PEX_SUCCESS:
				if (loggedin)
					warningmsg(WARN_LOGGED_IN, logname);
				break;
			case PEX_BUSY:
				break;

			case PEX_HOSED_FILES:
				errmsg(M_HOSED_FILES);
				exit(EX_INCONSISTENT);
				break;

			case PEX_SYNTAX:
			case PEX_BADARG:
				/*
				 * should NEVER occur that passmgmt
				 * usage is wrong
				 */
				if (is_role(usertype))
					errmsg(M_MRUSAGE);
				else
					errmsg(M_MUSAGE);
				exit(EX_SYNTAX);
				break;

			case PEX_BADUID:
				/*
				 * uid in use - shouldn't happen print
				 * message anyway
				 */
				errmsg(M_UID_USED, uid);
				exit(EX_ID_EXISTS);
				break;

			case PEX_BADNAME:
				/* invalid loname */
				errmsg(M_USED, logname);
				exit(EX_NAME_EXISTS);
				break;

			case PEX_NO_AUTH:
			case PEX_NO_ROLE:
			case PEX_NO_PROFILE:
			case PEX_NO_GROUP:
			case PEX_NO_PRIV:
			case PEX_NO_LABEL:
			case PEX_NO_SYSLABEL:
			case PEX_NO_PROJECT:
			case PEX_NO_PERM:
				errmsg(M_PERM_DENIED);
				errmsg(M_UPDATE_MODIFIED);
				exit(ret);
				break;
			case PEX_FAILED:
				ret = EX_UPDATE;
			default:
				errmsg(M_UPDATE_MODIFIED);
				exit(ret);
				break;
			}
		}
		if (tries == 0) {
			errmsg(M_UPDATE_MODIFIED);
		}
		if (ret != EX_SUCCESS)
			exit(ret);
	}

	free_add_remove();

	/*
	 * Finally update the home directory
	 * This done last because it depends on an updated passwd entry.
	 */
	if (mflag == 1 || new_logname) {
		char *username = new_logname? new_logname: logname;

		if (create_home == 1) {
			if (old_home_exists == 1) {
				if (uidstr == NULL &&
				    (try_remount(old_path, path) ==
				    EX_SUCCESS)) {
					mflag = 0;
				} else {
					/* create new home directory */
					if (create_home_dir(username, path,
					    NULL, uid, gid) != 0)
						exit(EX_HOMEDIR);
					if (move_dir(old_path, path,
					    username, 1) != 0) {
						exit(EX_HOMEDIR);
					}
				}
				(void) umount(homedir);
				(void) rmdir(old_path);
			} else {
				/* create new home directory */
				if (create_home_dir(username, path,
				    usrdefs->defskel, uid, gid) != 0)
					exit(EX_HOMEDIR);
			}
		} else {
			errmsg(M_HOMEDIR_CREATE, autohomedir);
		}
	}

	/* update the auto_home entry. */

	if (autohomedir != NULL) {
		if (ret = rep->rops->edit_autohome(logname, new_logname,
		    autohomedir, 0)) {
			errmsg(M_UPDATE_AUTOHOME);
			errmsg(M_UPDATE_MODIFIED);
			exit(EX_AUTO_HOME);
		}
	}
	/* Rename /var/user/$USER space if the logname changed. */
	if (new_logname != NULL) {
		char *oldvaruser = NULL;
		char *newvaruser = NULL;

		(void) asprintf(&oldvaruser, "/var/user/%s", logname);
		(void) asprintf(&newvaruser, "/var/user/%s", new_logname);
		if (oldvaruser == NULL || newvaruser == NULL ||
		    rename(oldvaruser, newvaruser) != 0) {
				errmsg(M_RENAME_WARNING, logname);
			}
		free(oldvaruser);
		free(newvaruser);
	}
	return (ret);
}
