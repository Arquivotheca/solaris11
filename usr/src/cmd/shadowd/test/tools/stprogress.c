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
 * This is a tool to measure the effectiveness of shadow migration progress
 * estimates.  It spawns some number of threads to go off and migrate a
 * particular directory.  It checks the progress every second until it is
 * finished, outputting the results to stdout.  This can then be examined, or
 * fed through the 'stplot' program to generate gnuplot output that
 * demonstrates the delta from the ideal over the lifetime of the migration.
 */

#include <libshadowfs.h>

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static shadow_handle_t *g_shp;
static pthread_mutex_t g_lock;
static pthread_cond_t g_cv;
static uint64_t g_gen;
static size_t g_threads;

static void
usage(void)
{
	(void) fprintf(stderr, "stprogress <directory> [nthreads]\n");
	exit(2);
}

static void
stfatal(const char *desc)
{
	(void) fprintf(stderr, "%s: %s\n", desc, shadow_errmsg());
	exit(1);
}

/*ARGSUSED*/
static void *
process(void *unused)
{
	uint64_t gen;

	for (;;) {
		(void) pthread_mutex_lock(&g_lock);
		gen = g_gen;
		(void) pthread_mutex_unlock(&g_lock);

		if (shadow_migrate_one(g_shp) != 0) {
			switch (shadow_errno()) {
			case ESHADOW_MIGRATE_DONE:
				g_threads--;
				return (NULL);
				break;

			case ESHADOW_MIGRATE_BUSY:
				(void) pthread_mutex_lock(&g_lock);
				while (gen == g_gen)
					(void) pthread_cond_wait(&g_cv,
					    &g_lock);
				(void) pthread_mutex_unlock(&g_lock);
			}
		} else {
			(void) pthread_mutex_lock(&g_lock);
			g_gen++;
			(void) pthread_cond_broadcast(&g_cv);
			(void) pthread_mutex_unlock(&g_lock);
		}
	}

	/*NOTREACHED*/
	return (NULL);
}

int
main(int argc, char **argv)
{
	char *directory;
	size_t nthread = 1;
	size_t i;
	pthread_t id;
	shadow_status_t status;
	hrtime_t now;

	if (argc < 2)
		usage();

	directory = argv[1];

	if (argc > 2)
		nthread = atoi(argv[2]);

	if (nthread == 0)
		usage();

	(void) pthread_mutex_lock(&g_lock);

	for (i = 0; i < nthread; i++) {
		if (pthread_create(&id, NULL, process, NULL) != 0) {
			(void) fprintf(stderr, "pthread_create() failed\n");
			return (1);
		}
	}

	g_threads = nthread;

	if ((g_shp = shadow_open(directory)) == NULL)
		stfatal("failed to open handle");


	while (g_threads > 0) {
		(void) pthread_mutex_unlock(&g_lock);
		if (!shadow_migrate_done(g_shp)) {
			shadow_get_status(g_shp, &status);
			now = gethrtime();

			if (status.ss_processed == 0 ||
			    status.ss_estimated == 0) {
				(void) printf("%.2f %lld %lld -\n",
				    (double)(now - status.ss_start) / NANOSEC,
				    status.ss_processed, status.ss_estimated);
			} else {
				(void) printf("%.2f %lld %lld %.2f\n",
				    (double)(now - status.ss_start) / NANOSEC,
				    status.ss_processed, status.ss_estimated,
				    (double)status.ss_processed /
				    (double)(status.ss_processed +
				    status.ss_estimated));
			}
		}

		(void) fflush(stdout);
		(void) sleep(1);
		(void) pthread_mutex_lock(&g_lock);
	}

	(void) shadow_migrate_finalize(g_shp);

	shadow_close(g_shp);
	return (0);
}
