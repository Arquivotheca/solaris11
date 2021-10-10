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
#include <userdefs.h>
#include <auth_attr.h>
#include <auth_list.h>
#include "users.h"
#include "messages.h"

/*
 *  groupdel group
 *
 *	This command deletes groups from the system.  Arguments are:
 *
 *	group - a character string group name
 */

char *cmdname = "groupdel";

extern void errmsg(), exit();

int
main(int argc, char **argv)
{
	char *group;		/* group name from command line */
	int retval = 0;
	int ch;
	unsigned int optmasks = DEL_MASK;	/* options masks */
	struct group grp;
	struct group *g_ptr = &grp;
	char *repostr = NULL;	/* repository, default comes from nsswitch */
	char pwbuf[NSS_BUFLEN_PASSWD];
	struct passwd pw;
	sec_repository_t *repo = NULL;	/* repository handle */
	nss_XbyY_buf_t *nssbuf = NULL;


	while ((ch = getopt(argc, argv, "S:")) != EOF)  {
		switch (ch) {
			case 'S':
				repostr = optarg;
				break;
			case '?':
				errmsg(M_DUSAGE);
				exit(EX_SYNTAX);
		}
	}

	if (optind != argc - 1) {
		errmsg(M_DUSAGE);
		exit(EX_SYNTAX);
	}

	group = argv[optind];

	/* Find the repository */
	if (repostr != NULL) {
		/* repository specified with -S */
		if (get_repository_handle(repostr, &repo) != SEC_REP_SUCCESS) {
			errmsg(M_DUSAGE);
			exit(EX_BADARG);
		}
	} else {
		/* Find the matching repository in nsswitch */
		if (find_in_nss(SEC_REP_DB_GROUP, group, &repo) !=
		    SEC_REP_SUCCESS) {
			errmsg(M_NO_GROUP, group);
			exit(EX_NAME_NOT_EXIST);
		}
	}

	init_nss_buffer(SEC_REP_DB_GROUP, &nssbuf);


	/*
	 * Need to find the group to determine if additional
	 * authorization is required
	 */
	if ((retval = repo->rops->get_grnam(group, &g_ptr, nssbuf)) !=
	    SEC_REP_SUCCESS) {
		errmsg(M_NO_GROUP, group);
		return (EX_NAME_NOT_EXIST);
	}

	/*
	 * Require both solaris.group.manage &
	 * solaris.group.assign[/<group> to delete a group
	 */
	if (getpwuid_r(getuid(), &pw, pwbuf, sizeof (pwbuf)) == NULL ||
	    chkauthattr(GROUP_MANAGE_AUTH, pw.pw_name) == 0 ||
	    chkauthattr(appendedauth(group), pw.pw_name) == 0) {
		errmsg(M_PERM_DENIED);
		exit(EX_UPDATE);
	}

	/* Delete the group */
	switch (retval = repo->rops->put_group(g_ptr, NULL, optmasks)) {
	case EX_UPDATE:
		errmsg(M_UPDATE, "deleted");
		break;

	case EX_NAME_NOT_EXIST:
		errmsg(M_NO_GROUP, group);
		break;

	case EX_SUCCESS:
		retval = group_authorize(pw.pw_name, group, B_FALSE);
		break;
	}

	free_nss_buffer(&nssbuf);

	return (retval);
}
