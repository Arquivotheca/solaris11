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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <regex.h>
#include <devfsadm.h>
#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/mkdev.h>
#include <sys/fs/uvfs.h>

/* zfs unit test driver */

static int uvfs(di_minor_t minor, di_node_t node);

/*
 * devfs create callback register
 */
static devfsadm_create_t uvfs_create_cbt[] = {
	{ "pseudo", "ddi_pseudo", UVFS_DRIVER,
	    TYPE_EXACT | DRV_EXACT, ILEVEL_0, uvfs,
	},
};
DEVFSADM_CREATE_INIT_V0(uvfs_create_cbt);

/*
 * For the uvfs control node:
 *	/dev/uvfs -> /devices/pseudo/uvfs@0:uvfs
 */
static int
uvfs(di_minor_t minor, di_node_t node)
{
	if (strcmp(di_minor_name(minor), UVFS_DRIVER) == 0)
		(void) devfsadm_mklink(UVFS_DRIVER, node, minor, 0);

	return (DEVFSADM_CONTINUE);
}
