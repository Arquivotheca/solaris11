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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Returns success if the filesystem contains files with persistent errors.
 * This requires iterating over entries in the pending list and correctly
 * identifying that only files with errors exist in the pending list.
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <libshadowfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
	shadow_handle_t *shp;
	shadow_status_t stat;

	if (argc != 2) {
		(void) fprintf(stderr, "usage: stpersistent <path>\n");
		return (2);
	}

	if ((shp = shadow_open(argv[1])) == NULL) {
		(void) fprintf(stderr, "failed to open shadow handle "
		    "for %s: %s\n", argv[1], shadow_errmsg());
		return (1);
	}

	for (;;) {
		if (shadow_migrate_one(shp) != 0) {
			(void) fprintf(stderr, "failed to migrate entry: %s\n",
			    shadow_errmsg());
			return (1);
		}

		if (shadow_migrate_only_errors(shp)) {
			shadow_get_status(shp, &stat);
			(void) fprintf(stdout, "%d persistent errors\n",
			    stat.ss_errors);
			return (0);
		}
	}

	/*NOTREACHED*/
	return (0);
}
