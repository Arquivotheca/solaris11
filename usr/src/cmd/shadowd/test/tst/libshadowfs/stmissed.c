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
 * Tests the case where we miss a file or directory that is renamed behind our
 * back.  In this case migration should continue and complete without incident.
 * We assume we get passed the root of filesystem that has at least one or more
 * directory within it.  We take a relative path argument that is an entry in
 * the root directory to rename and its new name.
 */

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libshadowfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
	shadow_handle_t *shp;
	int fd;

	if (argc != 4) {
		(void) fprintf(stderr, "usage: stmissed <path> <oldname> "
		    "<newname>\n");
		return (2);
	}

	if ((shp = shadow_open(argv[1])) == NULL) {
		(void) fprintf(stderr, "failed to open shadow handle "
		    "for %s: %s\n", argv[1], shadow_errmsg());
		return (1);
	}

	/* Migrate root directory */
	(void) shadow_migrate_one(shp);

	/*
	 * Rename one of the entries
	 */
	if ((fd = open(argv[1], O_RDONLY)) < 0) {
		(void) fprintf(stderr, "failed to open %s\n", argv[1]);
		return (1);
	}

	if (renameat(fd, argv[2], fd, argv[3]) != 0) {
		(void) fprintf(stderr, "failed to rename %s: %s\n",
		    argv[2], strerror(errno));
		return (1);
	}

	(void) close(fd);

	for (;;) {
		if (shadow_migrate_one(shp) == 0)
			continue;

		if (shadow_errno() == ESHADOW_MIGRATE_DONE)
			break;

		(void) fprintf(stderr, "failed to migrate entry: %s\n",
		    shadow_errmsg());
		return (1);
	}

	if (shadow_migrate_finalize(shp) != 0) {
		(void) fprintf(stderr, "failed to finalize "
		    "migration: %s\n", shadow_errmsg());
		return (1);
	}

	shadow_close(shp);

	return (0);
}
