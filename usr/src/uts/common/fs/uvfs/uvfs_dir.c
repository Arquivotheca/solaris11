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

#include <sys/uvfs.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/cmn_err.h>
#include <sys/vfs.h>
#include <sys/stat.h>
#include <sys/vfs_opreg.h>
#include <sys/vnode.h>
#include <sys/fcntl.h>
#include <sys/uvfs_vfsops.h>
#include <sys/uvfs_uvnode.h>
#include <sys/uvfs_upcall.h>

int
uvfs_dirlook(uvnode_t *uvdp, char *name, vnode_t **vpp, cred_t *cr)
{
	int error = 0;
	libuvfs_stat_t stat;
	libuvfs_fid_t fid;
	libuvfs_fid_t *fidp = &fid;
	uvnode_t *uvp;

	if (name[0] == 0 || (name[0] == '.' && name[1] == 0)) {
		*vpp = UVTOV(uvdp);
		VN_HOLD(*vpp);
		return (0);
	} else if (name[0] == '.' && name[1] == '.' && name[2] == 0) {
		fidp = &uvdp->uv_pfid;
		error = uvfs_up_vget(uvdp->uv_uvfsvfs, fidp, &stat, CRED());
	} else {
		error = uvfs_up_lookup(uvdp, name, fidp, &stat, cr);
	}

	if (error == 0) {
		if ((error = uvfs_uvget(UVTOV(uvdp)->v_vfsp, &uvp,
		    fidp, &stat)) == 0) {
			*vpp = UVTOV(uvp);
		}
	}
	return (error);
}
