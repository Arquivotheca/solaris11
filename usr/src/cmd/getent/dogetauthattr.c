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
#include <auth_attr.h>
#include "getent.h"

/*
 * print authorization attributes information
 */
static void
putauthent(authattr_t *authp, FILE *f)
{
	(void) fprintf(f, "%s:%s:%s:%s:%s:",
	    authp->name != NULL ? authp->name : "",
	    authp->res1 != NULL ? authp->res1 : "",
	    authp->res2 != NULL ? authp->res2 : "",
	    authp->short_desc != NULL ? authp->short_desc : "",
	    authp->long_desc != NULL ? authp->long_desc : "");

	putkeyvalue(authp->attr, f);

	(void) fprintf(f, "\n");
	free_authattr(authp);
}

/*
 * get entries from authorization description database
 */
int
dogetauthattr(const char **list)
{
	authattr_t *auth;
	int rc = EXC_SUCCESS;

	if (list == NULL || *list == NULL) {
		setauthattr();
		while ((auth = getauthattr()) != NULL) {
			putauthent(auth, stdout);
		}
		endauthattr();
	} else {
		for (; *list != NULL; list++) {
			if ((auth = getauthnam(*list)) == NULL) {
				rc = EXC_NAME_NOT_FOUND;
			} else {
				putauthent(auth, stdout);
			}
		}
	}

	return (rc);
}
