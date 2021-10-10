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
 * Sets a shadow mount into 'spin' mode where any attempt to migrate a file or
 * directory will spin waiting for a signal.  This is used to test EINTR
 * handling.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libshadowtest.h>

int
main(int argc, char **argv)
{
	boolean_t mode;

	if (argc != 3) {
		(void) fprintf(stderr, "usage: stspin <file> <on|off>\n");
		return (2);
	}

	if (strcmp(argv[2], "on") == 0) {
		mode = B_TRUE;
	} else if (strcmp(argv[2], "off") == 0) {
		mode = B_FALSE;
	} else {
		(void) fprintf(stderr, "usage: stspin <file> <on|off>\n");
		return (2);
	}

	if (st_init() != 0)
		return (1);

	if (st_spin(argv[1], mode) != 0)
		return (1);

	st_fini();

	return (0);
}
