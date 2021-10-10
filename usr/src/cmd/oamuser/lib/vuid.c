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



#include	<sys/types.h>
#include	<stdio.h>
#include	<pwd.h>
#include	<userdefs.h>
#include	<users.h>

#include	<sys/param.h>

#ifndef MAXUID
#include	<limits.h>
#define	MAXUID	UID_MAX
#endif

int
valid_uid_check(uid_t uid, struct passwd **pptr,
sec_repository_t *rep, nss_XbyY_buf_t *b)
{
	struct passwd *t_pptr;

	if ((int)uid <= 0)
		return (INVALID);

	if (uid <= DEFRID) {
		if (pptr != NULL) {
			if (rep->rops->get_pwid(uid, &t_pptr, b) == 0) {
				*pptr = t_pptr;
			}
		}
		return (RESERVED);
	}

	if (uid > MAXUID)
		return (TOOBIG);

	if (rep->rops->get_pwid(uid, &t_pptr, b) == 0) {
		if (pptr != NULL) {
			*pptr = t_pptr;
		}
		return (NOTUNIQUE);
	}

	return (UNIQUE);
}
