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
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
#include <userdefs.h>
#include <users.h>
#include <errno.h>
#include <strings.h>
#include <nss_dbdefs.h>
#include <auth_list.h>
#include <auth_list.h>
#include "users.h"
#include "messages.h"

/*
 *  groupmod -g gid [-o] | -n name group
 *
 *	This command modifies groups on the system.  Arguments are:
 *
 *	gid - a gid_t less than UID_MAX
 *	name - a string of printable characters excluding colon (:) and less
 *		than MAXGLEN characters long.
 *	group - a string of printable characters excluding colon(:) and less
 *		than MAXGLEN characters long.
 */

extern void errmsg();
static int list_add(char **, char **, char **, int);
static int list_remove(char **, char **, char **, int);

char *cmdname = "groupmod";

int
main(int argc, char *argv[])
{
	int ch;				/* return from getopt */
	gid_t gid;			/* group id */
	int oflag = 0;			/* flags */
	int retval;
	char *gidstr = NULL;		/* gid from command line */
	char *newname = NULL;		/* new group name with -n option */
	char *grpname;			/* group name from command line */
	int warning;
	unsigned int optmasks = MOD_MASK;	/* options masks */
	struct group grp;
	struct group *g_ptr = &grp;
	char *repostr = NULL;	/* repository, default comes from nsswitch */
	char *userlist = NULL;	/* user list from command line */
	char *ulist;
	char *baduser;
	char *users[NSS_BUFLEN_GROUP];
	char *resultlist[NSS_BUFLEN_GROUP];
	char pwbuf[NSS_BUFLEN_PASSWD];
	struct passwd pw;
	sec_repository_t *repo = NULL;	/* repository handle */
	sec_repository_t *lkrepo = NULL; /* lookup repository handle */
	nss_XbyY_buf_t *nssbuf = NULL;
	char opchar;			/* for add/subtract/replace */


	oflag = 0;	/* flags */

	while ((ch = getopt(argc, argv, "g:on:S:U:")) != EOF)  {
		switch (ch) {
			case 'g':
				gidstr = optarg;
				break;
			case 'o':
				oflag++;
				break;
			case 'n':
				newname = optarg;
				optmasks |= GRP_N_MASK; /* change in name */
				break;
			case 'S':
				repostr = optarg;
				break;
			case 'U':
				userlist = optarg;
				break;
			case '?':
				errmsg(M_MUSAGE);
				exit(EX_SYNTAX);
		}
	}

	if ((oflag && !gidstr) || optind != argc - 1) {
		errmsg(M_MUSAGE);
		exit(EX_SYNTAX);
	}

	grpname = argv[optind];

	/* Find the repository */
	if (repostr != NULL) {
		/* repository specified with -S */
		if (get_repository_handle(repostr, &repo) != SEC_REP_SUCCESS) {
			errmsg(M_MUSAGE);
			exit(EX_BADARG);
		}
	} else {
		/* Find the matching repository in nsswitch */
		if (find_in_nss(SEC_REP_DB_GROUP, grpname, &repo) !=
		    SEC_REP_SUCCESS) {
			errmsg(M_NO_GROUP, grpname);
			exit(EX_NAME_NOT_EXIST);
		}
	}

	init_nss_buffer(SEC_REP_DB_GROUP, &nssbuf);

	if (gidstr != NULL) {
		/* convert gidstr to integer */
		char *ptr;

		errno = 0;
		gid = (gid_t)strtol(gidstr, &ptr, 10);

		if (*ptr || errno == ERANGE) {
			errmsg(M_GID_INVALID, gidstr);
			exit(EX_BADARG);
		}

		switch (valid_group_id(gid, &g_ptr, repo, nssbuf)) {
		case RESERVED:
			errmsg(M_RESERVED, gid);
			break;

		case NOTUNIQUE:
			if (!oflag) {
				errmsg(M_GRP_USED, gidstr);
				exit(EX_ID_EXISTS);
			}
			break;

		case INVALID:
			errmsg(M_GID_INVALID, gidstr);
			exit(EX_BADARG);
			/*NOTREACHED*/

		case TOOBIG:
			errmsg(M_TOOBIG, gid);
			exit(EX_BADARG);
			/*NOTREACHED*/

		}

	} else {
		gid = (gid_t)-1;
	}

	if (newname) {
		switch (valid_group_name(newname, NULL, &warning,
		    repo, nssbuf)) {
		case INVALID:
			errmsg(M_GRP_INVALID, newname);
			exit(EX_BADARG);
		case NOTUNIQUE:
			errmsg(M_GRP_USED, newname);
			exit(EX_NAME_EXISTS);
		}
		if (warning)
			warningmsg(warning, newname);
	}

	if (userlist != NULL) {
		/* Determine if a prefix +/- is specified */
		if (userlist[0] == OP_ADD_CHAR ||
		    userlist[0] == OP_SUBTRACT_CHAR) {
			opchar = userlist[0];
			ulist = &userlist[1];
		} else {
			opchar = OP_REPLACE_CHAR;
			ulist = userlist;
		}

		if (get_repository_handle(NULL, &lkrepo) != SEC_REP_SUCCESS) {
			errmsg(M_MUSAGE);
			exit(EX_BADARG);
		}

		if ((baduser = check_users(ulist, lkrepo,
		    nssbuf, users)) != NULL) {
			errmsg(M_USER_INVALID, baduser);
			exit(EX_BADARG);
		}
	}

	/*
	 * Need to find the group to determine if additional
	 * authorization is required
	 */
	if ((retval = repo->rops->get_grnam(grpname, &g_ptr, nssbuf)) !=
	    SEC_REP_SUCCESS) {
		errmsg(M_NO_GROUP, grpname);
		return (EX_NAME_NOT_EXIST);
	}

	/*
	 * Require solaris.group.assign[/<group>] or
	 * solaris.group.assign with -o option
	 */
	if (getpwuid_r(getuid(), &pw, pwbuf, sizeof (pwbuf)) == NULL ||
	    (chkauthattr(GROUP_ASSIGN_AUTH, pw.pw_name) == 0 &&
	    (oflag || chkauthattr(appendedauth(grpname), pw.pw_name) == 0))) {
		errmsg(M_PERM_DENIED);
		exit(EX_UPDATE);
	}

	if (gidstr != NULL) {
		g_ptr->gr_gid = gid;
	}

	if (userlist != NULL) {
		/* Do the list operation on users */
		switch (opchar) {
		case OP_ADD_CHAR:
			if ((retval = list_add(g_ptr->gr_mem, users,
			    resultlist, NSS_BUFLEN_GROUP)) == 0) {
				g_ptr->gr_mem = resultlist;
			}
			break;

		case OP_SUBTRACT_CHAR:
			if ((retval = list_remove(g_ptr->gr_mem, users,
			    resultlist, NSS_BUFLEN_GROUP)) == 0) {
				g_ptr->gr_mem = resultlist;
			}
			break;

		default:
			/* Replace */
			g_ptr->gr_mem = users;
			retval = 0;
			break;
		}

		if (retval != 0) {
			errmsg(M_USER_INVALID, ulist);
			exit(EX_BADARG);
		}
	}

	/* Modify the group */
	switch (retval = repo->rops->put_group(g_ptr, newname, optmasks)) {
	case EX_UPDATE:
		errmsg(M_UPDATE, "modified");
		break;
	case EX_NAME_NOT_EXIST:
		errmsg(M_NO_GROUP, grpname);
		break;
	}

	free_nss_buffer(&nssbuf);

	return (retval);
}

/*
 * Add two list of strings list1 & list2.
 * Duplicate strings are ignored.
 *
 * result - returned list
 * rsize - result size
 * Returns 0 - successful
 *	  otherwise - error
 */

static int
list_add(char **list1, char **list2, char **result, int rsize)
{
	int count = 0;
	char **list;

	/* Copy list1 */
	list = list1;
	while (list != NULL && *list != NULL) {
		result[count++] = *list++;
		if (count >= rsize) {
			return (-1); /* list full */
		}
	}

	/* Copy members of the list2 after checking for duplicates */
	list = list2;
	while (*list != NULL) {
		if (list1 == NULL || !list_contains(list1, *list)) {
			result[count++] = *list;
			if (count >= rsize) {
				return (-1); /* list full */
			}
		}
		list++;
	}

	result[count] = NULL;

	return (0);
}

/*
 * Remove list2 from list1
 *
 * result - returned list
 * rsize - result size
 * Returns 0 - successful
 *	   otherwise - error
 *	   if items in list2 is not a subset of list1
 */

static int
list_remove(char **list1, char **list2, char **result, int rsize)
{
	int count = 0;
	char **list;

	/* Copy only the members of the list1 not present in list2 */
	list = list1;
	while (*list != NULL) {
		if (!list_contains(list2, *list)) {
			result[count++] = *list;
			if (count >= rsize) {
				return (-1); /* list full */
			}
		}
		list++;
	}

	if (count == 0) {
		result[count++] = ""; /* empty list */
	}

	result[count] = NULL; /* last item on the list */

	return (0);
}
