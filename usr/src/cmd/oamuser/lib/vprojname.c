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


/*LINTLIBRARY*/

#include	<sys/types.h>
#include	<stdio.h>
#include	<ctype.h>
#include	<userdefs.h>
#include	<nss_dbdefs.h>
#include	<users.h>
#include	<project.h>

/*
 * validate string given as project name.
 */
int
valid_projname(char *project, struct project **pptr, void *buf, size_t blen,
    int *warning)
{
	return (
	    valid_proj_byname(project, pptr, warning, NULL, NULL, buf, blen));
}


/*
 * validate string given as project name.
 */
int
valid_proj_byname(char *project, struct project **pptr, int *warning,
    sec_repository_t *rep, nss_XbyY_buf_t *b, void *buf, size_t blen)
{
	char *ptr = project;
	char c;
	int len = 0;
	int badchar = 0;

	*warning = 0;
	if (!project || !*project)
		return (INVALID);

	for (c = *ptr; c != NULL; ptr++, c = *ptr) {
		len++;
		if (!isprint(c) || (c == ':') || (c == '\n'))
			return (INVALID);
		if (!(islower(c) || isdigit(c)))
			badchar++;
	}

	if (len > PROJNAME_MAX)
		*warning = *warning | WARN_NAME_TOO_LONG;
	if (badchar != 0)
		*warning = *warning | WARN_BAD_PROJ_NAME;

	if ((rep == NULL) ?
	    (getprojbyname(project, *pptr, buf, blen) != NULL) :
	    rep->rops->get_projnam(project, pptr, b) == 0) {
		return (NOTUNIQUE);
	}

	return (UNIQUE);
}
