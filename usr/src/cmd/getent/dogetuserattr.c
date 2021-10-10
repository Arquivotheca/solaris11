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
#include <user_attr.h>
#include "getent.h"

/*
 * print extended user attributes information
 */
static void
putuattrent(userattr_t *ua, FILE *f)
{
	(void) fprintf(f, "%s:%s:%s:%s:",
	    ua->name != NULL ? ua->name : "",
	    ua->qualifier != NULL ? ua->qualifier : "",
	    ua->res1 != NULL ? ua->res1 : "",
	    ua->res2 != NULL ? ua->res2 : "");

	putkeyvalue(ua->attr, f);

	(void) fprintf(f, "\n");
	free_userattr(ua);
}

/*
 * get entries from extended user attributes database
 */
int
dogetuserattr(const char **list)
{
	userattr_t *uattr;
	int rc = EXC_SUCCESS;
	char *ptr;
	uid_t uid;

	if (list == NULL || *list == NULL) {
		setuserattr();
		while ((uattr = getuserattr()) != NULL) {
			putuattrent(uattr, stdout);
		}
		enduserattr();
	} else {
		for (; *list != NULL; list++) {
			errno = 0;

			/*
			 * Here we assume that the argument passed is
			 * a uid, if it can be completely transformed
			 * to a long integer. If the argument passed is
			 * not numeric, then  we take it as the user name
			 * and proceed.
			 */
			uid = strtoul(*list, &ptr, 10);
			if (*ptr == '\0' && errno == 0) {
				uattr = getuseruid(uid);
			} else {
				uattr = getusernam(*list);
			}

			if (uattr == NULL) {
				rc = EXC_NAME_NOT_FOUND;
			} else {
				putuattrent(uattr, stdout);
			}
		}
	}

	return (rc);
}
