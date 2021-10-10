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
#include <prof_attr.h>
#include "getent.h"

/*
 * print execution profile attributes information
 */
static void
putprofent(profattr_t *pa, FILE *f)
{
	(void) fprintf(f, "%s:%s:%s:%s:",
	    pa->name != NULL ? pa->name : "",
	    pa->res1 != NULL ? pa->res1 : "",
	    pa->res2 != NULL ? pa->res2 : "",
	    pa->desc != NULL ? pa->desc : "");

	putkeyvalue(pa->attr, f);

	(void) fprintf(f, "\n");
	free_profattr(pa);
}

/*
 * get entries from profile description database
 */
int
dogetprofattr(const char **list)
{
	profattr_t *prof;
	int rc = EXC_SUCCESS;

	if (list == NULL || *list == NULL) {
		setprofattr();
		while ((prof = getprofattr()) != NULL) {
			putprofent(prof, stdout);
		}
		endprofattr();
	} else {
		for (; *list != NULL; list++) {
			if ((prof = getprofnam(*list)) == NULL) {
				rc = EXC_NAME_NOT_FOUND;
			} else {
				putprofent(prof, stdout);
			}
		}
	}

	return (rc);
}
