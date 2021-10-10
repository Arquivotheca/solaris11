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
 * Given a list of filenames, verify that the pending FID list matches the list
 * of files and/or directories specified in the command line.
 *
 * Usage: stpending [-e] <idx> [files ...]
 *
 * If '-e' is specified, then test to see if the pending list is empty - the
 * first file argument is only used to determine the root of the filesystem.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <libshadowtest.h>

int
main(int argc, char **argv)
{
	int idx;
	boolean_t check_empty = B_FALSE;

	if (argc < 2) {
		(void) fprintf(stderr,
		    "usage: stpending [-e] <0|1> [files ...]\n");
		return (2);
	}

	if (strcmp(argv[1], "-e") == 0) {
		check_empty = B_TRUE;
		argc--;
		argv++;
	}

	if (argc < 2) {
		(void) fprintf(stderr,
		    "usage: stpending [-e] <0|1> [files ...]\n");
		return (2);
	}

	idx = atoi(argv[1]);

	if (st_init() != 0)
		return (1);

	if (check_empty) {
		if (st_verify_pending_empty(idx, argv[2]) != 0)
			return (1);
	} else {
		if (st_verify_pending(idx, argc - 2, argv + 2) != 0)
			return (1);
	}

	st_fini();

	return (0);
}
