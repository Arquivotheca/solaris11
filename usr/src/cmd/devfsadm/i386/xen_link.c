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
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <regex.h>
#include <devfsadm.h>
#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/balloon.h>

/*
 * Handle miscellaneous children of xendev
 */
static int devxen(di_minor_t, di_node_t);

static devfsadm_create_t xen_cbt[] = {
	{ "xendev", DDI_PSEUDO, BALLOON_DRIVER_NAME,
	    TYPE_EXACT | DRV_EXACT, ILEVEL_0, devxen,
	},
};

DEVFSADM_CREATE_INIT_V0(xen_cbt);

static devfsadm_remove_t xen_remove_cbt[] = {
	{ "xendev", "^" BALLOON_PATHNAME "$", RM_ALWAYS | RM_PRE | RM_HOT,
	    ILEVEL_0, devfsadm_rm_all
	},
};

DEVFSADM_REMOVE_INIT_V0(xen_remove_cbt);

/*
 * /dev/xen/<foo>	->	/devices/xendev/<whatever>:<foo>
 */
static int
devxen(di_minor_t minor, di_node_t node)
{
	char buf[256];

	(void) snprintf(buf, sizeof (buf), "xen/%s", di_minor_name(minor));
	(void) devfsadm_mklink(buf, node, minor, 0);

	return (DEVFSADM_CONTINUE);
}
