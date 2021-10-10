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
#include <pwd.h>
#include <sys/param.h>
#include <nssec.h>
#ifndef	MAXUID
#include <limits.h>
#ifdef UID_MAX
#define	MAXUID	UID_MAX
#else
#define	MAXUID	60000
#endif
#endif

static
uid_t getrangeboundid(uid_t, uid_t, sec_repository_t *, nss_XbyY_buf_t *);
static int isreserveduid(uid_t);

/*
 * Find the highest uid currently in use and return it. If the highest unused
 * uid is MAXUID, then attempt to find a hole in the range. If there are no
 * more unused uids, then return -1.
 */
uid_t
find_next_avail_uid(sec_repository_t *rep, nss_XbyY_buf_t *b)
{
	uid_t uid = DEFRID + 1;
	struct passwd *pwd;
	uchar_t overflow = 0;

	rep->rops->set_pwent();
	for (pwd = rep->rops->get_pwent(b); pwd != NULL;
	    pwd = rep->rops->get_pwent(b)) {
		if (isreserveduid(pwd->pw_uid)) /* Skip reserved IDs */
			continue;
		if (pwd->pw_uid >= uid) {
			if (pwd->pw_uid == MAXUID) { /* Overflow check */
				overflow = 1;
				break;
			}
			uid = pwd->pw_uid + 1;
			while (isreserveduid(uid) &&
			    uid < MAXUID) { /* Skip reserved IDs */
				uid++;
			}
		}
	}
	if (pwd != NULL) {
		free(pwd);
	}
	rep->rops->end_pwent();
	if (overflow == 1) /* Find a hole */
		return (getrangeboundid(DEFRID + 1, MAXUID, rep, b));
	return (uid);
}

/*
 * Check to see that the uid is a reserved uid
 * -- nobody, noaccess or nobody4
 */
static int
isreserveduid(uid_t uid)
{
	return (uid == 60001 || uid == 60002 || uid == 65534);
}

/*
 * getrangeboundid() attempts to return the next valid usable id between the
 * supplied upper and lower limits. If these limits exceed the system
 * boundaries of DEFRID +1 and MAXUID (lower and upper bound respectively),
 * then they are ignored and DEFRID + 1 and MAXUID are used.
 *
 * Returns a valid uid_t between DEFRID +1 and MAXUID, -1 is returned on fail
 */
static uid_t
getrangeboundid(uid_t start, uid_t stop, sec_repository_t *rep,
    nss_XbyY_buf_t *b)
{
	uid_t low = (start <= DEFRID) ? DEFRID + 1 : start;
	uid_t high = (stop < MAXUID) ? stop : MAXUID;
	uid_t uid;
	struct passwd *pwd = NULL;

	for (uid = low; uid <= high; uid++) {
		if (isreserveduid(uid))
			continue;
		if (rep->rops->get_pwid(uid, &pwd, b) == SEC_REP_NOT_FOUND)
			break;
	}
	return ((uid > high) ? -1 : uid);
}
