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
 * Given a ZFS dataset, outputs the GUID as a hexidecimal number.  The zfs(1M)
 * command can do this for us, but it outputs it as a decimal which is a pain
 * to deal with.  ksh comes close to converting this for us, but
 * inappropriately interprets some large values as a negative number.
 */

#include <libzfs.h>
#include <stdio.h>

int
main(int argc, char **argv)
{
	libzfs_handle_t *hdl;
	zfs_handle_t *zhp;
	uint64_t guid;

	if (argc != 2) {
		(void) fprintf(stderr, "usage: zfsguid <dataset>\n");
		return (2);
	}

	if ((hdl = libzfs_init()) == NULL) {
		(void) fprintf(stderr, "failed to initialize libzfs\n");
		return (1);
	}

	libzfs_print_on_error(hdl, B_TRUE);

	if ((zhp = zfs_open(hdl, argv[1], ZFS_TYPE_DATASET)) == NULL)
		return (1);

	guid = zfs_prop_get_int(zhp, ZFS_PROP_GUID);

	(void) printf("%llx\n", guid);

	zfs_close(zhp);
	libzfs_fini(hdl);

	return (0);
}
