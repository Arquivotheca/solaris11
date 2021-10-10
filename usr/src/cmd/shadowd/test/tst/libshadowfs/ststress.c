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

#include <libshadowfs.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void *
do_migrate(void *data)
{
	shadow_handle_t *shp = data;
	struct timespec ts;

	for (;;) {
		if (shadow_migrate_one(shp) == 0)
			continue;

		switch (shadow_errno()) {
		case ESHADOW_MIGRATE_DONE:
			return (NULL);

		case ESHADOW_MIGRATE_BUSY:
			ts.tv_sec = 0;
			ts.tv_nsec = 100 * 1000 * 1000;
			(void) nanosleep(&ts, NULL);
			break;

		default:
			(void) fprintf(stderr, "migration failed: %s\n",
			    shadow_errmsg());
			exit(1);
		}
	}

	/*NOTREACHED*/
	return (NULL);
}

int
main(int argc, char **argv)
{
	shadow_handle_t *shp;
	int i, count;
	pthread_t *tids;

	if (argc != 3) {
		(void) fprintf(stderr, "usage: ststress <path> <count>\n");
		return (2);
	}

	if ((shp = shadow_open(argv[1])) == NULL) {
		(void) fprintf(stderr, "failed to open shadow handle "
		    "for %s: %s\n", argv[1], shadow_errmsg());
		return (1);
	}

	count = atoi(argv[2]);

	if ((tids = malloc(count * sizeof (pthread_t))) == NULL) {
		(void) fprintf(stderr, "failed to allocated %d threads\n",
		    count);
		return (1);
	}

	for (i = 0; i < count; i++) {
		if (pthread_create(&tids[i], NULL, do_migrate, shp) != 0) {
			(void) fprintf(stderr, "failed to create thread\n");
			return (1);
		}
	}

	for (i = 0; i < count; i++)
		(void) pthread_join(tids[i], NULL);

	if (!shadow_migrate_done(shp)) {
		(void) fprintf(stderr, "migration failed to complete\n");
		return (1);
	}

	shadow_close(shp);

	return (0);
}
