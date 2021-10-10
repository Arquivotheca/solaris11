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


#include	<sys/types.h>
#include	<stdio.h>
#include	<stdlib.h>
#include	<ctype.h>
#include	<limits.h>
#include	<userdefs.h>
#include	<users.h>
#include	<errno.h>
#include	<string.h>
#include	<auth_attr.h>
#include	<auth_list.h>
#include	"users.h"
#include	"messages.h"

extern void errmsg();
extern gid_t findnextgid();
extern int valid_gid(), add_group();

/*
 *  groupadd [-g gid [-o]] group
 *
 *	This command adds new groups to the system.  Arguments are:
 *
 *	gid - a gid_t less than MAXUID
 *	group - a string of printable characters excluding colon(:) and less
 *		than MAXGLEN characters long.
 */

char *cmdname = "groupadd";

int
main(int argc, char *argv[])
{
	int ch;				/* return from getopt */
	gid_t gid;			/* group id */
	int oflag = 0;	/* flags */
	int rc;
	char *gidstr = NULL;	/* gid from command line */
	char *grpname;			/* group name from command line */
	int warning;
	unsigned int optmasks = ADD_MASK;	/* options masks */
	struct group grp;
	struct group *grp_ptr = &grp;
	char *repostr = "files";	/* repository, default is files */
	char *userlist = NULL;		/* user list from command line */
	char *baduser;
	char *users[NSS_BUFLEN_GROUP];
	char pwbuf[NSS_BUFLEN_PASSWD];
	struct passwd pw;

	sec_repository_t *repo = NULL;	 /* repository handle */
	sec_repository_t *lkrepo = NULL; /* lookup repository handle */
	nss_XbyY_buf_t *nssbuf = NULL;

	while ((ch = getopt(argc, argv, "g:oS:U:")) != EOF)
		switch (ch) {
			case 'g':
				gidstr = optarg;
				break;
			case 'o':
				oflag++;
				break;
			case 'S':
				repostr = optarg;
				break;
			case 'U':
				userlist = optarg;
				break;
			case '?':
				errmsg(M_AUSAGE);
				exit(EX_SYNTAX);
		}

	if ((oflag && !gidstr) || optind != argc - 1) {
		errmsg(M_AUSAGE);
		exit(EX_SYNTAX);
	}

	/* Check for a valid repository */
	if (get_repository_handle(repostr, &repo) != SEC_REP_SUCCESS) {
		errmsg(M_AUSAGE);
		exit(EX_BADARG);
	}

	/* Check if the administrator is authorized to add a group */
	if (getpwuid_r(getuid(), &pw, pwbuf, sizeof (pwbuf)) == NULL ||
	    chkauthattr(GROUP_MANAGE_AUTH, pw.pw_name) == 0 ||
	    (oflag && chkauthattr(GROUP_ASSIGN_AUTH, pw.pw_name) == 0)) {
		errmsg(M_PERM_DENIED);
		exit(EX_UPDATE);
	}

	init_nss_buffer(SEC_REP_DB_GROUP, &nssbuf);

	grpname = argv[optind];

	switch (valid_group_name(grpname, NULL, &warning, repo, nssbuf)) {
	case INVALID:
		errmsg(M_GRP_INVALID, grpname);
		exit(EX_BADARG);
		/*NOTREACHED*/
	case NOTUNIQUE:
		errmsg(M_GRP_USED, grpname);
		exit(EX_NAME_EXISTS);
		/*NOTREACHED*/
	}
	if (warning)
		warningmsg(warning, grpname);

	if (gidstr) {
		/* Given a gid string - validate it */
		char *ptr;

		errno = 0;
		gid = (gid_t)strtol(gidstr, &ptr, 10);

		if (*ptr || errno == ERANGE) {
			errmsg(M_GID_INVALID, gidstr);
			exit(EX_BADARG);
			/*NOTREACHED*/
		}

		switch (valid_group_id(gid, &grp_ptr, repo, nssbuf)) {
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

		case TOOBIG:
			errmsg(M_TOOBIG, gid);
			exit(EX_BADARG);

		}

	} else {

		if ((gid = findnextgid()) == (gid_t)-1) {
			errmsg(M_GID_INVALID, "default id");
			exit(EX_ID_EXISTS);
		}

	}

	/* Validate userlist */
	if (userlist != NULL) {
		if (get_repository_handle(NULL, &lkrepo) != SEC_REP_SUCCESS) {
			errmsg(M_AUSAGE);
			exit(EX_BADARG);
		}

		if ((baduser = check_users(userlist, lkrepo,
		    nssbuf, users)) != NULL) {
			errmsg(M_USER_INVALID, baduser);
			exit(EX_BADARG);
		}
		grp.gr_mem = users;
	} else {
		grp.gr_mem = NULL;
	}

	grp.gr_gid = gid;
	grp.gr_name = grpname;
	grp.gr_passwd = "";

	if ((rc = repo->rops->put_group(&grp, NULL,
	    optmasks)) == EX_SUCCESS) {
		/* Add solaris.group.assign/grpname for the user */
		rc = group_authorize(pw.pw_name, grpname, B_TRUE);
	} else {
		errmsg(M_UPDATE, "created");
	}

	free_nss_buffer(&nssbuf);

	return (rc);
}
