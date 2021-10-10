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
 * Prints the FID of the given file to stdout.  This is used to verify that the
 * hardlink table is updated correctly.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <libshadowtest.h>

int
main(int argc, char **argv)
{
	fid_t fid;
	int i;

	if (argc != 2) {
		(void) fprintf(stderr, "usage: stfid <file>\n");
		return (2);
	}

	if (st_init() != 0)
		return (1);

	if (st_get_fid(argv[1], &fid) != 0)
		return (1);

	st_fini();

	for (i = 0; i < fid.fid_len; i++)
		(void) printf("%02x", (uint8_t)fid.fid_data[i]);

	return (0);
}
