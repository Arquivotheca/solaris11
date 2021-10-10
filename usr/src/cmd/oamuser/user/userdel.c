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


/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/


/*
 * Copyright (c) 1989, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <ctype.h>
#include <limits.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <userdefs.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <strings.h>
#include <getopt.h>
#include "users.h"
#include "messages.h"
#include "funcs.h"
#include <libintl.h>
#include <auth_list.h>
#include <syslog.h>
#include <sys/mount.h>

/*
 *  userdel [-S [files|ldap]] [-r] login
 *
 * This command deletes user logins.  Arguments are:
 *
 * -r - when given, this option removes home directory & its contents
 *
 * login - a string of printable chars except colon (:)
 */

static char *logname; /* login name to delete */
static char *nargv[20]; /* arguments for execvp of passmgmt */

char *cmdname;

static char *repository = NULL;

extern int check_user_authorized();
extern int call_passmgmt(char *nargv[]);
extern int remove_home_dir(char *, uid_t, gid_t, sec_repository_t *);
extern int isbusy(char *);

int
main(int argc, char **argv)
{
	int ch, ret = 0, rflag = 0, argindex, tries;
	struct passwd *pstruct;
	char *usertype = NULL;
	sec_repository_t *rep = NULL;
	nss_XbyY_buf_t *pwdbuf = NULL;
	nss_XbyY_buf_t *uabuf = NULL;
	char *default_rep = NSS_REP_FILES;
	userattr_t *ua = NULL;
	char *val = NULL;
	int isrole = 0;
	int rep_count = 0;
	char path[MAXPATHLEN + 1];

	cmdname = argv[0];

	repository = default_rep;
	opterr = 0; /* no print errors from getopt */
	usertype = getusertype(argv[0]);

	while ((ch = getopt(argc, argv, "rS:")) != EOF) {
		switch (ch) {
		case 'r':
			rflag++;
			break;
		case 'S':
			repository = optarg;
			rep_count++;
			if (rep_count > 1) {
				errmsg(M_MULTI_DEFINED, "-S");
				exit(EX_SYNTAX);
			}
			break;
		case '?':
			if (is_role(usertype))
				errmsg(M_DRUSAGE);
			else
				errmsg(M_DUSAGE);
			exit(EX_SYNTAX);
		}
	}

	if (optind != argc - 1) {
		if (is_role(usertype))
			errmsg(M_DRUSAGE);
		else
			errmsg(M_DUSAGE);
		exit(EX_SYNTAX);
	}

	logname = argv[optind];

	if (strcmp(logname, "root") == 0) {
		errmsg(M_DEL_ROOT);
		errmsg(M_PERM_DENIED);
		exit(EX_NO_PERM);
	}

	if (rep_count == 0) {
		ret = find_in_nss(SEC_REP_DB_PASSWD, logname, &rep);
		if (ret == SEC_REP_NOT_FOUND) {
			errmsg(M_EXIST, logname, "");
			exit(EX_NAME_NOT_EXIST);
		} else if (ret == SEC_REP_SYSTEM_ERROR) {
			exit(EX_FAILURE);
		}
		if (rep != NULL && rep->type == SEC_REP_LDAP) {
			repository = NSS_REP_LDAP;
		} else if (rep != NULL && rep->type == SEC_REP_FILES) {
			repository = NSS_REP_FILES;
		}
	} else {
		ret = get_repository_handle(repository, &rep);
		if (ret == SEC_REP_NOREP) {
			exit(EX_SYNTAX);
		} else if (ret == SEC_REP_SYSTEM_ERROR) {
			exit(EX_FAILURE);
		}
	}


	init_nss_buffer(SEC_REP_DB_PASSWD, &pwdbuf);
	(void) rep->rops->get_pwnam(logname, &pstruct, pwdbuf);

	if (pstruct == NULL) {
		errmsg(M_EXIST, logname, repository);
		exit(EX_NAME_NOT_EXIST);
	}

	/* Determine whether the account is a role or not */
	init_nss_buffer(SEC_REP_DB_USERATTR, &uabuf);
	(void) rep->rops->get_usernam(logname, &ua, uabuf);

	if ((ua == NULL) ||
	    (val = kva_match(ua->attr, USERATTR_TYPE_KW)) == NULL ||
	    strcmp(val, USERATTR_TYPE_NONADMIN_KW) != 0)
		isrole = 0;
	else
		isrole = 1;

	if (ua != NULL && ua->res1 != NULL && strcmp(ua->res1, "RO") == 0) {
		errmsg(M_ACCOUNT_READONLY);
		errmsg(M_PERM_DENIED);
		exit(EX_NO_PERM);
	}

	free(ua);
	free_nss_buffer(&uabuf);

	/* Verify that rolemod is used for roles and usermod for users */

	if (isrole != is_role(usertype)) {
		if (isrole)
			errmsg(M_DELROLE);
		else
			errmsg(M_DELUSER);
		exit(EX_SYNTAX);
	}

	if (pstruct->pw_uid <= DEFRID) {
		if (check_user_authorized() < 0) {
			errmsg(M_RESERVED, pstruct->pw_uid);
			errmsg(M_PERM_DENIED);
			exit(EX_NO_AUTH);
		}
	}

	if (isbusy(logname)) {
		errmsg(M_BUSY, logname, "remove");
		exit(EX_BUSY);
	}

	/* remove home directory */
	if (rflag) {
		char *auth;
		struct passwd *pw;

		if (isrole) auth = ROLE_MANAGE_AUTH;
		else auth = USER_MANAGE_AUTH;

		if ((pw = getpwuid(getuid())) == NULL) {
			errmsg(M_GETUID);
			errmsg(M_UPDATE_DELETED);
			exit(EX_UPDATE);
		}
		if (chkauthattr(auth, pw->pw_name) == 0) {
			errmsg(M_ACCOUNT_DELETE, auth);
			errmsg(M_PERM_DENIED);
			exit(EX_NO_AUTH);
		}

		if (remove_home_dir(logname, pstruct->pw_uid,
		    pstruct->pw_gid, rep) != EX_SUCCESS)
			exit(EX_HOMEDIR);
	}

	/* that's it for validations - now do the work */
	/* set up arguments to  passmgmt in nargv array */
	nargv[0] = PASSMGMT;
	nargv[1] = "-d"; /* delete */
	argindex = 2; /* next argument */

	if (repository) {
		nargv[argindex++] = "-S";
		nargv[argindex++] = repository;
	}
	/* finally - login name */
	nargv[argindex++] = logname;

	/* set the last to null */
	nargv[argindex++] = NULL;

	/* now call passmgmt */
	ret = PEX_FAILED;
	for (tries = 3; ret != PEX_SUCCESS && tries--; ) {
		switch (ret = call_passmgmt(nargv)) {
		case PEX_SUCCESS:
			ret = rep->rops->edit_groups(logname, (char *)NULL,
			    (struct group_entry **)NULL, 1);
			if (ret != EX_SUCCESS) {
				errmsg(M_UPDATE_GROUP);
				errmsg(M_LOGIN_DELETED);
				exit(EX_GRP_UPDATE);
			}
			break;

		case PEX_BUSY:
			break;

		case PEX_HOSED_FILES:
			errmsg(M_HOSED_FILES);
			exit(EX_INCONSISTENT);
			break;

		case PEX_SYNTAX:
		case PEX_BADARG:
			/* should NEVER occur that passmgmt usage is wrong */
			if (is_role(usertype))
				errmsg(M_DRUSAGE);
			else
				errmsg(M_DUSAGE);
			exit(EX_SYNTAX);
			break;

		case PEX_BADUID:
			/*
			 * uid is used - shouldn't happen but print
			 * message anyway
			 */
			errmsg(M_UID_USED, pstruct->pw_uid);
			exit(EX_ID_EXISTS);
			break;

		case PEX_BADNAME:
			/* invalid logname */
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
		case PEX_NO_PERM:
			errmsg(M_PERM_DENIED);
			errmsg(M_UPDATE_DELETED);
			exit(ret);
			break;
		case PEX_FAILED:
			ret = EX_UPDATE;
		default:
			errmsg(M_UPDATE_DELETED);
			exit(ret);
			break;
		}
	}
	if (tries == 0)
		errmsg(M_UPDATE_DELETED);
	if (ret != EX_SUCCESS)
		exit(ret);

	free_nss_buffer(&pwdbuf);
	/*
	 * Now, remove this user from all project entries
	 */

	if (rep->rops->edit_projects(logname, (char *)NULL,
	    (projid_t *)NULL, 1) != EX_SUCCESS) {
		errmsg(M_UPDATE_PROJECT);
		errmsg(M_LOGIN_DELETED);
		exit(EX_PROJ_UPDATE);
	}

	/* delete the auto_home entry for the user. */
	if (rep->rops->edit_autohome(logname, (char *)NULL, NULL,
	    DEL_MASK) != EX_SUCCESS) {
		errmsg(M_UPDATE_AUTOHOME);
		errmsg(M_LOGIN_DELETED);
		exit(EX_AUTO_HOME);
	}

	(void) snprintf(path, sizeof (path), "/home/%s", logname);
	(void) umount(path);

	exit(ret);
	/*NOTREACHED*/

	return (EX_SUCCESS);
}
