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


/*LINTLIBRARY*/

#include	<sys/types.h>
#include	<stdio.h>
#include	<stdlib.h>
#include	<ctype.h>
#include	<errno.h>
#include	<limits.h>
#include	<sys/param.h>
#include	<users.h>
#include	<userdefs.h>

#ifndef	MAXUID
#include	<limits.h>
#define	MAXUID	UID_MAX
#endif

extern int _getgroupsbymember(const char *, gid_t [], int, int);

int
isalldigit(char *str)
{
	while (*str != '\0') {
		if (!isdigit((unsigned char) * str))
			return (0);
		str++;
	}
	return (1);
}

/*
 * validate a group name or number and return the appropriate
 * group structure for it.
 */

int
valid_group_check(char *group, struct group **gptr, int *warning,
    sec_repository_t *rep, nss_XbyY_buf_t *b)
{
	int r, warn;
	long l;
	char *ptr;
	struct group *grp;

	*warning = 0;

	if (rep == NULL)
		return (SEC_REP_NOREP);

	if (!isalldigit(group))
		return (valid_group_name(group, gptr, warning, rep, b));

	/*
	 * There are only digits in group name.
	 * strtol() doesn't return negative number here.
	 */
	errno = 0;
	l = strtol(group, &ptr, 10);
	if ((l == LONG_MAX && errno == ERANGE) || l > MAXUID) {
		r = TOOBIG;
	} else {
		if ((r = valid_group_id((gid_t)l, &grp, rep, b)) == NOTUNIQUE) {
			/* It is a valid existing gid */
			if (gptr != NULL) {
				*gptr = grp;
			}
			return (NOTUNIQUE);
		}
	}
	/*
	 * It's all digit, but not a valid gid nor an existing gid.
	 * There might be an existing group name of all digits.
	 */
	if ((r = valid_group_name(group, &grp, &warn, rep, b)) == NOTUNIQUE) {
		/* It does exist */
		*warning = warn;
		if (gptr != NULL) {
			*gptr = grp;
		}
		return (NOTUNIQUE);
	}
	/*
	 * It isn't either existing gid or group name. We return the
	 * error code from valid_gid() assuming that given string
	 * represents an integer GID.
	 */
	return (r);
}

/*
 * check gids to see if user
 * is part of these groups
 */
int
check_groups_for_user(uid_t uid, struct group_entry **gids)
{
	int ngroups_max;
	gid_t *grplist;
	struct passwd *pwd;
	int ngrp = -1;
	int i, j;
	int found;

	if (gids != NULL) {

		ngroups_max = sysconf(_SC_NGROUPS_MAX);
		grplist = (gid_t *)
		    malloc((ngroups_max + 1) * sizeof (gid_t));

		if (!grplist)
			return (-1);
		pwd = getpwuid(uid);

		if (pwd == NULL)
			return (-1);

		grplist[0] = pwd->pw_gid;

		/*
		 * The getgroupsbymember needs to be implemented
		 * for files and ldap separately.
		 * currently going through name service switch.
		 */
		ngrp = _getgroupsbymember(pwd->pw_name, grplist,
		    ngroups_max, 1);

		j = 0;
		if (ngrp > 0) {
			while (gids[j] != NULL) {
				found = 0;
				for (i = 0; i < ngrp; i++) {
					if (grplist[i] == gids[j]->gid) {
						found = 1;
						break;
					}
				}
				if (!found) {
					return (gids[j]->gid);
				}
				j++;
			}
		} else {
			return (-1);
		}

	}
	return (0);
}
