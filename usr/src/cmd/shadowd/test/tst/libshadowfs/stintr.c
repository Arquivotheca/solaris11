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
 * This test program spawns a thread after setting up the migration to spin,
 * and then makes sure we can cancel it, and the entry still appears on the
 * pending list.
 */

#include <assert.h>
#include <libshadowfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <libshadowtest.h>
#include <sys/lwp.h>
#include <signal.h>
#include <unistd.h>

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

void *
migrate_one(void *arg)
{
	shadow_handle_t *shp = arg;

	if (shadow_migrate_one(shp) != 0) {
		(void) fprintf(stderr, "failed async migration\n");
		exit(1);
	}

	return (NULL);
}

int
main(int argc, char **argv)
{
	shadow_handle_t *shp;
	pthread_t tid;

	if (argc != 2) {
		(void) fprintf(stderr, "usage: stintr <root>\n");
		return (2);
	}

	if ((shp = shadow_open(argv[1])) == NULL) {
		(void) fprintf(stderr, "failed to open shadow handle "
		    "for %s: %s\n", argv[1], shadow_errmsg());
		return (1);
	}

	(void) shadow_migrate_iter(shp, dump_path, argv[1]);
	(void) printf("\n");

	/*
	 * Set shadow mount to spin and spawn a thread.
	 */
	if (st_init() != 0)
		return (1);

	if (st_spin(argv[1], B_TRUE) != 0)
		return (1);

	st_fini();

	if (pthread_create(&tid, NULL, migrate_one, shp) != 0) {
		(void) fprintf(stderr, "failed to create thread\n");
		return (1);
	}

	(void) pthread_cancel(tid);

	if (pthread_join(tid, NULL) != 0) {
		(void) fprintf(stderr, "pthread_join() failed\n");
		return (1);
	}

	(void) shadow_migrate_iter(shp, dump_path, argv[1]);

	shadow_close(shp);

	return (0);
}
