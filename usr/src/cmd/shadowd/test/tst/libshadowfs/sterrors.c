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
 * Tests error erporting.  This can be either through the normal path or the
 * ioctl() path.  For the latter, the user can specify a file or directory to
 * rename that will cause it to be missed during the normal traversal.
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
	shadow_error_report_t *errors;
	int i, count;
	int fd;

	if (argc < 3) {
		(void) fprintf(stderr, "usage: sterrors <path> <count>"
		    "[oldname] [newname]\n");
		return (2);
	}

	if ((shp = shadow_open(argv[1])) == NULL) {
		(void) fprintf(stderr, "failed to open shadow handle "
		    "for %s: %s\n", argv[1], shadow_errmsg());
		return (1);
	}

	count = atoi(argv[2]);
	for (i = 0; i < count; i++) {
		if (shadow_migrate_one(shp) != 0) {
			(void) fprintf(stderr, "failed to migrate entry "
			    "%d: %s\n", i, shadow_errmsg());
			return (1);
		}

		if (count == 0 && argc >= 5) {
			if ((fd = open(argv[1], O_RDONLY)) < 0) {
				(void) fprintf(stderr, "failed to open %s\n",
				    argv[1]);
				return (1);
			}

			if (renameat(fd, argv[3], fd, argv[4]) != 0) {
				(void) fprintf(stderr,
				    "failed to rename %s: %s\n",
				    argv[2], strerror(errno));
				return (1);
			}

			(void) close(fd);
		}
	}

	shadow_get_status(shp, &stat);

	(void) printf("%d error(s)\n", stat.ss_errors);
	if (stat.ss_errors > 0) {
		if ((errors = shadow_get_errors(shp,
		    stat.ss_errors)) == NULL)  {
			(void) fprintf(stderr, "failed to get errors: %s\n",
			    shadow_errmsg());
		}

		for (i = 0; i < stat.ss_errors; i++)
			(void) printf("%d %s\n", errors[i].ser_errno,
			    errors[i].ser_path);

		shadow_free_errors(errors, stat.ss_errors);
	}

	shadow_close(shp);

	return (0);
}
