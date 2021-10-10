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
 * Copyright (c) 2000, 2011, Oracle and/or its affiliates. All rights reserved.
 */


#include	<sys/types.h>
#include 	<limits.h>
#include	<stdio.h>
#include	<project.h>
#include	<sys/param.h>
#include	<nss_dbdefs.h>
#include	<users.h>
#include	<userdefs.h>

/*  validate a project id */
int
valid_projid(projid_t projid, struct project *pptr, void *buf, size_t len)
{
	if (projid < 0)
		return (INVALID);

	if (projid > MAXPROJID)
		return (TOOBIG);

	if (getprojbyid(projid, pptr, buf, len) != NULL) {
		return (NOTUNIQUE);
	}

	return (UNIQUE);
}

/*  validate a project id in a specified nameservice repository */
int
valid_proj_byid(projid_t projid, struct project **t_pptr,
    sec_repository_t *rep, nss_XbyY_buf_t *b)
{

	if (projid < 0)
		return (INVALID);

	if (projid > MAXPROJID)
		return (TOOBIG);

	if (rep->rops->get_projid(projid, t_pptr, b) == 0) {
		return (NOTUNIQUE);
	}
	return (UNIQUE);
}
