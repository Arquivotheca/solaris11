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

#include <assert.h>
#include <libshadowfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libshadowtest.h>

static void
dump_path(const char *path, void *data)
{
	const char *root = data;
	size_t len = strlen(root);

	if (strcmp(path, root) == 0) {
		(void) printf("<root>\n");
		return;
	}

	assert(strlen(path) > len);
	assert(strncmp(path, root, len) == 0);
	assert(path[len] == '/');

	(void) printf("%s\n", path + len + 1);
}

int
main(int argc, char **argv)
{
	shadow_handle_t *shp;
	int i, count;

	if (argc < 3) {
		(void) fprintf(stderr, "usage: stcomplete <path> <count> "
		    "[finalize]\n");
		return (2);
	}

	if (st_init() != 0)
		return (1);

	if ((shp = shadow_open(argv[1])) == NULL) {
		(void) fprintf(stderr, "failed to open shadow handle "
		    "for %s: %s\n", argv[1], shadow_errmsg());
		return (1);
	}

	count = atoi(argv[2]);
	for (i = 0; i < count - 1; i++) {
		if (shadow_migrate_one(shp) != 0) {
			(void) fprintf(stderr, "failed to migrate entry "
			    "%d: %s\n", i, shadow_errmsg());
			return (1);
		}
		(void) printf("\n%d:\n", i);
		(void) shadow_migrate_iter(shp, dump_path, argv[1]);

		if (shadow_migrate_done(shp)) {
			(void) fprintf(stderr, "migration complete after %d "
			    "iterations\n", i);
			return (1);
		}
	}

	/*
	 * For testing purposes, we want to make sure the pending list is
	 * empty, so we explicitly rotate it twice as we expect it to be empty.
	 * Otherwise we'll attempt to do the cleanup ioctl() and then the check
	 * for extra entries will fail.
	 */
	if (st_rotate(argv[1]) != 0 || st_rotate(argv[1]) != 0) {
		(void) fprintf(stderr, "failed to rotate pending log\n");
		return (1);
	}

	(void) printf("\nafter:\n");
	if (shadow_migrate_one(shp) == 0) {
		(void) fprintf(stderr, "successfully migrated extra entry\n");
		return (1);
	}

	if (shadow_errno() != ESHADOW_MIGRATE_DONE) {
		(void) fprintf(stderr, "shadow_migrate_one() failed but "
		    "returned %d: %s\n", shadow_errno(), shadow_errmsg());
		return (1);
	}

	if (!shadow_migrate_done(shp)) {
		(void) fprintf(stderr, "migration not complete despite "
		    "ESHADOW_MIGRATE_DONE\n");
		return (1);
	}

	if (argc > 3 && strcmp(argv[3], "true") == 0) {
		if (shadow_migrate_finalize(shp) != 0) {
			(void) fprintf(stderr, "failed to finalize "
			    "migration: %s\n", shadow_errmsg());
			return (1);
		}
	}

	shadow_close(shp);
	st_fini();

	return (0);
}
