/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2004 Sun Microsystems, Inc.
 * All rights reserved.  Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "nfs_mdb.h"
#include "nfs4_mdb.h"

/*
 * nfs_vfs:  Walks VFS list (begining at rootvfs)
 * and if vfs_op is equal to a nfs vfsops will call out
 * to walk_callback.
 */

extern uintptr_t vfs_op2, vfs_op3, vfs_op4;

int
nfs_vfs_walk_init(mdb_walk_state_t *wsp)
{
	if (wsp->walk_addr == NULL) {
		if (mdb_readvar(&wsp->walk_data, "rootvfs") == -1) {
			mdb_warn("failed to read `rootvfs'\n");
			return (WALK_ERR);
		}
	}
	return (WALK_NEXT);
}

int
nfs_vfs_walk_step(mdb_walk_state_t *wsp)
{
	int status = WALK_NEXT;
	vfs_t vfs;

	if (wsp->walk_addr == NULL) {
		/* First visit for this walk */
		wsp->walk_addr = (uintptr_t)wsp->walk_data;
	} else if (wsp->walk_addr == (uintptr_t)wsp->walk_data) {
			/* we're done with the circular list */
			return (WALK_DONE);
	}

	NFS_OBJ_FETCH(wsp->walk_addr, vfs_t, &vfs, WALK_ERR);

	if ((uintptr_t)vfs.vfs_op == vfs_op4 ||
	    (uintptr_t)vfs.vfs_op == vfs_op3 ||
	    (uintptr_t)vfs.vfs_op == vfs_op2) {
		status = wsp->walk_callback(wsp->walk_addr, &vfs,
				wsp->walk_cbdata);
	}
	wsp->walk_addr = (uintptr_t)vfs.vfs_next;
	return (status);
}

/*
 * nfs4_mnt:  Walks VFS list (begining at rootvfs)
 * and if vfs_op is equal to a nfsv4 vfsop will call out
 * to walk_callback passing the mntinfo4_t pointer.
 */
int
nfs4_mnt_walk_init(mdb_walk_state_t *wsp)
{
	if (wsp->walk_addr == NULL) {
		if (mdb_readvar(&wsp->walk_data, "rootvfs") == -1) {
			mdb_warn("failed to read `rootvfs'\n");
			return (WALK_ERR);
		}
	}
	return (WALK_NEXT);
}

int
nfs4_mnt_walk_step(mdb_walk_state_t *wsp)
{
	int status = WALK_NEXT;
	vfs_t vfs;

	if (wsp->walk_addr == NULL) {
		/* First visit for this walk */
		wsp->walk_addr = (uintptr_t)wsp->walk_data;
	} else if (wsp->walk_addr == (uintptr_t)wsp->walk_data) {
			/* we're done with the circular list */
			return (WALK_DONE);
	}

	NFS_OBJ_FETCH(wsp->walk_addr, vfs_t, &vfs, WALK_ERR);

	if ((uintptr_t)vfs.vfs_op == vfs_op4) {
		status = wsp->walk_callback((uintptr_t)vfs.vfs_data,
					    vfs.vfs_data,
				wsp->walk_cbdata);
	}
	wsp->walk_addr = (uintptr_t)vfs.vfs_next;
	return (status);
}

/*
 * nfs_mnt:  Walks VFS list (begining at rootvfs)
 * and if vfs_op is equal to a non nfsv4 vfsop will
 * call out to walk_callback passing the mntinfo_t
 * pointer.
 */
int
nfs_mnt_walk_init(mdb_walk_state_t *wsp)
{
	if (wsp->walk_addr == NULL) {
		if (mdb_readvar(&wsp->walk_data, "rootvfs") == -1) {
			mdb_warn("failed to read `rootvfs'\n");
			return (WALK_ERR);
		}
	}
	return (WALK_NEXT);
}

int
nfs_mnt_walk_step(mdb_walk_state_t *wsp)
{
	int status = WALK_NEXT;
	vfs_t vfs;

	if (wsp->walk_addr == NULL) {
		/* First visit for this walk */
		wsp->walk_addr = (uintptr_t)wsp->walk_data;
	} else if (wsp->walk_addr == (uintptr_t)wsp->walk_data) {
			/* we're done with the circular list */
			return (WALK_DONE);
	}

	NFS_OBJ_FETCH(wsp->walk_addr, vfs_t, &vfs, WALK_ERR);

	if ((uintptr_t)vfs.vfs_op == vfs_op3 ||
	    (uintptr_t)vfs.vfs_op == vfs_op2) {
		status = wsp->walk_callback((uintptr_t)vfs.vfs_data,
				vfs.vfs_data,
				wsp->walk_cbdata);
	}
	wsp->walk_addr = (uintptr_t)vfs.vfs_next;
	return (status);
}

/*ARGSUSED*/
void
nfs_null_walk_fini(mdb_walk_state_t *wsp)
{
}
