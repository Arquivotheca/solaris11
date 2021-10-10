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
 * Copyright (c) 2006, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * mkdtemp(3C) - create a directory with a unique name.
 */

#include "lint.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

char *
mkdtemp(char *template)
{
	char *t;
	char *r;

	/* Save template */
	t = strdupa(template);
	for (;;) {
		r = mktemp(template);

		if (*r == '\0')
			return (NULL);

		if (mkdir(template, 0700) == 0)
			return (r);

		/* Other errors indicate persistent conditions. */
		if (errno != EEXIST)
			return (NULL);

		/* Reset template */
		(void) strcpy(template, t);
	}
}
