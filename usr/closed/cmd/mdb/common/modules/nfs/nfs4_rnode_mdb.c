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

#include <sys/mdb_modapi.h>
#include <nfs/nfs4.h>
#include <nfs/rnode4.h>

#define	RNODE4_MDB_HDR		0x00000001
#define	RNODE4_MDB_DELEG	0x00000002
#define	RNODE4_MDB_OO		0x00000004


/*ARGSUSED*/
static void
rnode4_fmt(uintptr_t addr, rnode4_t *rn, vnode_t *vn, uint_t *dcmd_opt)
{
	mdb_printf("%-?p %-?p %-?p %-?p %-?p %-7d 0x%-08x %-d\n",
			addr,
			rn->r_vnode,
			vn->v_vfsp,
			rn->r_fh,
			rn->r_server,
			rn->r_error,
			rn->r_flags,
			rn->r_count);

}

static int
rnode4_fetch(uintptr_t addr, rnode4_t *rn, vnode_t *vn)
{
	/* get the rnode4_t */
	if (mdb_vread(rn, sizeof (rnode4_t), addr) != sizeof (rnode4_t)) {
		mdb_warn("error reading rnode4_t at %p\n", addr);
		return (DCMD_ERR);
	}

	/* now get the vnode_t (the SHADOW knows..) */
	if (mdb_vread(vn, sizeof (vnode_t),
			(uintptr_t)rn->r_vnode) != sizeof (vnode_t)) {
		mdb_warn("error reading rnode4_t(%p) vnode_t at %p\n",
			addr, rn->r_vnode);
		return (DCMD_ERR);
	}
	return (DCMD_OK);
}

/*ARGSUSED*/
static int
rnode4_dump(uintptr_t addr, uintptr_t bogus, uint_t *dcmd_opt)
{
	rnode4_t rn;
	vnode_t vn;

	/* get the rnode4_t */
	if (rnode4_fetch(addr, &rn, &vn) == DCMD_ERR) {
		mdb_warn("error fetching rnode4_t at %p\n", addr);
		return (DCMD_ERR);
	}

	rnode4_fmt(addr, &rn, &vn, NULL);
	return (DCMD_OK);
}

/*
 * Do the actual work to see if this rnode4_t is something
 * we're interested in..
 */
/*ARGSUSED*/
static int
rnode4_find(uintptr_t addr, uintptr_t bogus, vfs_t *vfsp)
{
	rnode4_t rn;
	vnode_t vn;

	/* get the rnode4_t */
	if (rnode4_fetch(addr, &rn, &vn) == DCMD_ERR) {
		mdb_warn("error fetching rnode4_t at %p\n", addr);
		return (DCMD_ERR);
	}

	/* if this is an interesting rnode4_t show 'em to user */
	if (vfsp == vn.v_vfsp)
		rnode4_fmt(addr, &rn, &vn, NULL);

	return (DCMD_OK);
}

/*
 * DCMD: for a given VFS Pointer find the rnode4 nodes for it!
 */
/*ARGSUSED*/
int
rnode4_find_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	vfs_t 	*vfsp;

	if (!(flags & DCMD_ADDRSPEC)) {
		mdb_warn("no address specified\n");
		return (DCMD_USAGE);
	}

	vfsp = (vfs_t *)addr;

	mdb_printf("%-?s %-?s %-?s %-?s %-?s %-7s %-10s %s\n",
			"Address",
			"r_vnode",
			"vfsp",
			"r_fh",
			"r_server",
			"r_error",
			"r_flags",
			"r_count");


	if (mdb_walk("nfs_rtable4", (mdb_walk_cb_t)rnode4_find, vfsp) == -1) {
		mdb_warn("failed to walk nfs_rtable4\n");
		return (DCMD_ERR);
	}
	return (DCMD_OK);
}

/*
 * DCMD:
 */
/*ARGSUSED*/
int
rnode4_dump_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	rnode4_t rn;
	vnode_t  vn;

	mdb_printf("%-?s %-?s %-?s %-?s %-?s %-7s %-10s %s\n",
			"Address",
			"r_vnode",
			"vfsp",
			"r_fh",
			"r_server",
			"r_error",
			"r_flags",
			"r_count");


	if (! (flags & DCMD_ADDRSPEC)) {
		if (mdb_walk("nfs_rtable4",
				(mdb_walk_cb_t)rnode4_dump,
				NULL) == -1) {
			mdb_warn("failed to walk nfs_rtable4\n");
			return (DCMD_ERR);
		}
	} else {
		if (rnode4_fetch(addr, &rn, &vn) != DCMD_OK) {
			mdb_warn("error fetching rnode4_t at %p\n", addr);
			return (DCMD_ERR);
		}
		rnode4_fmt(addr, &rn, &vn, NULL);
	}
	return (DCMD_OK);
}

void
rnode4_dump_dcmd_help(void)
{
	mdb_printf("<rnode4 addr>::nfs_rnode4\n\n"
		"This prints NFSv4 rnode at address specified. If address\n"
		"is not specified, walks entire NFSv4 rnode table.\n");
}

void
rnode4_find_dcmd_help(void)
{
	mdb_printf("<vfs addr>::nfs_rnode4find\n\n"
		"This prints all NFSv4 rnodes that belong to \n"
		"the VFS address specified.\n");
}
