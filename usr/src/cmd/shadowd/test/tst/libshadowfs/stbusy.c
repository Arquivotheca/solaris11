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
 * Verify that we correctly get ESHADOW_MIGRATE_BUSY when the only possible
 * entries are being processed by all possible threads.
 */

#include <libshadowfs.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void *
migrate_one(void *data)
{
	shadow_handle_t *shp = data;

	if (shadow_migrate_one(shp) != 0) {
		(void) fprintf(stderr, "failed to migrate entry\n");
		exit(1);
	}

	return (NULL);
}

int
main(int argc, char **argv)
{
	shadow_handle_t *shp;
	pthread_t tid;
	struct timespec ts;

	if (argc != 2) {
		(void) fprintf(stderr, "usage: stbusy <path>\n");
		return (2);
	}

	if ((shp = shadow_open(argv[1])) == NULL) {
		(void) fprintf(stderr, "failed to open shadow handle "
		    "for %s: %s\n", argv[1], shadow_errmsg());
		return (1);
	}

	shadow_migrate_delay(shp, 500);

	if (pthread_create(&tid, NULL, migrate_one, shp) != 0) {
		(void) fprintf(stderr, "failed to create thread\n");
		return (1);
	}

	ts.tv_sec = 0;
	ts.tv_nsec = 250 * 1000 * 1000;

	(void) nanosleep(&ts, NULL);

	if (shadow_migrate_one(shp) == 0) {
		(void) fprintf(stderr, "successfully migrated entry\n");
		return (1);
	}

	if (shadow_errno() != ESHADOW_MIGRATE_BUSY) {
		(void) fprintf(stderr, "shadow_migrate_one() failed "
		    "with %d: %s\n", shadow_errno(), shadow_errmsg());
		return (1);
	}

	(void) pthread_join(tid, NULL);

	shadow_close(shp);

	return (0);
}
