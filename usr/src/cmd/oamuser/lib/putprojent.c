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


#include <stdio.h>
#include <project.h>
#include <unistd.h>

/*
 * putprojent()	function to write a project structure to a file
 */
void
putprojent(struct project *projstr, FILE *to)
{
	char **memptr;		/* member vector pointer */

	(void) fprintf(to, "%s:%ld:%s:", projstr->pj_name,
	    projstr->pj_projid, projstr->pj_comment);

	/*
	 * do user names first
	 */
	memptr = projstr->pj_users;

	while (*memptr != NULL) {
		(void) fprintf(to, "%s", *memptr);
		memptr++;
		if (*memptr != NULL)
			(void) fprintf(to, ",");
	}

	(void) fprintf(to, ":");

	/*
	 * now do groups
	 */
	memptr = projstr->pj_groups;

	while (*memptr != NULL) {
		(void) fprintf(to, "%s", *memptr);
		memptr++;
		if (*memptr != NULL)
			(void) fprintf(to, ",");
	}

	(void) fprintf(to, ":%s\n", projstr->pj_attr);
}
