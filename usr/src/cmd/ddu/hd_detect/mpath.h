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
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _MPATH_H
#define	_MPATH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <mpapi.h>
#include <libdevinfo.h>

typedef struct {
	/* devinfo devices tree node for this multipath logical unit */
	di_node_t	node;
	MP_OID		oid;
	char		path[256];
	void*		next;
} lu_obj;

int mpath_init();
void mpath_fini();
lu_obj *getInitiaPortDevices(char *iportID, lu_obj *pObj);
int check_mpath_link(di_devlink_t devlink, void *arg);

#ifdef __cplusplus
}
#endif

#endif /* _MPATH_H */
