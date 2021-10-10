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
#include	<stdio.h>
#include	<strings.h>
#include	<sys/param.h>
#include	<users.h>
#include	<userdefs.h>
#include	<errno.h>
#include	"messages.h"
#include	<stdlib.h>


static projid_t projlist[NPROJECTS_MAX + 1];
static int nproj_max = NPROJECTS_MAX;

/* Validate a list of projects */
projid_t *
valid_lproject(char *list, sec_repository_t *rep, nss_XbyY_buf_t *b)
{
	int n_invalid = 0;
	int i = 0;
	int j;
	char *ptr;
	struct project *projent;
	int warning;

	if (!list || !*list)
		return (NULL);

	while (ptr = strtok(((i || n_invalid) ? NULL : list), ",")) {

		switch (valid_project_check(ptr, &projent, &warning, rep, b)) {
		case INVALID:
			errmsg(M_INVALID, ptr, "project id");
			n_invalid++;
			break;
		case TOOBIG:
			errmsg(M_TOOBIG, "projid", ptr);
			n_invalid++;
			break;
		case UNIQUE:
			errmsg(M_PROJ_NOTUSED, ptr);
			n_invalid++;
			break;
		case NOTUNIQUE:

			if (!i)
				/* ignore respecified primary  */
				projlist[i++] = projent->pj_projid;

			else {
				/* Keep out duplicates */
				for (j = 0; j < i; j++)
					if (projent->pj_projid == projlist[j])
						break;

				if (j == i)
					/* Not a duplicate */
					projlist[i++] = projent->pj_projid;
			}
			break;
		}
		if (warning)
			warningmsg(warning, ptr);
		free(projent);

		if (i >= nproj_max) {
			errmsg(M_MAXPROJECTS, nproj_max);
			break;
		}
	}

	/* Terminate the list */
	projlist[i] = -1;

	if (n_invalid)
		exit(EX_BADARG);

	return (projlist);
}
