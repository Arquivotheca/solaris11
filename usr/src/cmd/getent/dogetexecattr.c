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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <exec_attr.h>
#include "getent.h"

/*
 * print execution profile executable attributes entry information
 */
static void
putexecent(execattr_t *execp, FILE *f)
{
	for (; execp != NULL; execp = execp->next) {
		(void) fprintf(f, "%s:%s:%s:%s:%s:%s:",
		    execp->name != NULL ? execp->name : "",
		    execp->policy != NULL ? execp->policy : "",
		    execp->type != NULL ? execp->type : "",
		    execp->res1 != NULL ? execp->res1 : "",
		    execp->res2 != NULL ? execp->res2 : "",
		    execp->id != NULL ? execp->id : "");

		putkeyvalue(execp->attr, f);

		(void) fprintf(f, "\n");
	}

	free_execattr(execp);
}

/*
 * get executable entries from execution profiles database
 */
int
dogetexecattr(const char **list)
{
	execattr_t *exec;
	int rc = EXC_SUCCESS;

	if (list == NULL || *list == NULL) {
		setexecattr();
		while ((exec = getexecattr()) != NULL) {
			putexecent(exec, stdout);
		}
		endexecattr();
	} else {
		while (*list != NULL) {
			if ((exec = getexecprof(*list, NULL,
			    NULL, GET_ALL)) == NULL) {
				rc = EXC_NAME_NOT_FOUND;
			} else {
				putexecent(exec, stdout);
			}
			list++;
		}
	}

	return (rc);
}
