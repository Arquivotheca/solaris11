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
#include	<grp.h>
#include	<users.h>
#include	<sys/param.h>
#include	<userdefs.h>

/*
 * MAXUID should be in param.h; if it isn't,
 * try for UID_MAX in limits.h
 */
#ifndef	MAXUID
#include	<limits.h>
#define	MAXUID	UID_MAX
#endif

struct group *getgrgid();

/*  validate a GID */
int
valid_gid(gid_t gid, struct group **gptr)
{
	register struct group *t_gptr;

	if ((int)gid < 0)
		return (INVALID);

	if (gid > MAXUID)
		return (TOOBIG);

	if ((t_gptr = getgrgid(gid)) != NULL) {
		if (gptr != NULL) {
			*gptr = t_gptr;
		}
		return (NOTUNIQUE);
	}

	if (gid <= DEFGID) {
		if (gptr != NULL) {
			*gptr = getgrgid(gid);
		}
		return (RESERVED);
	}

	return (UNIQUE);
}

/*  validate a GID against any nameservice repository */
int
valid_group_id(gid_t gid, struct group **gptr, sec_repository_t *rep,
    nss_XbyY_buf_t *b)
{
	struct group *t_gptr;

	if ((int)gid < 0)
		return (INVALID);

	if (gid > MAXUID)
		return (TOOBIG);

	if (rep->rops->get_grid(gid, &t_gptr, b) == 0) {
		if (gptr != NULL) {
			*gptr = t_gptr;
		}
		return (NOTUNIQUE);
	}

	if (gid <= DEFGID) {
		if (gptr != NULL) {
			*gptr = t_gptr;
		}
		return (RESERVED);
	}

	return (UNIQUE);
}
