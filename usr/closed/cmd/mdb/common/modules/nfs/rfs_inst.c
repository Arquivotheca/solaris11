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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <mdb/mdb_modapi.h>
#include <nfs/nfs_srv_inst_impl.h>
#include <nfs/nfs.h>
#include <rpc/svc.h>
#include <nfs/nfs_clnt.h>
#include "nfs_mdb.h"

uintptr_t  crip_addr = 0, crzp_addr = 0;
rfs_inst_t current_rip, *crip = NULL;
rfs_zone_t current_rzp, *crzp = NULL;

uintptr_t  grip_addr = 0, grzp_addr = 0;
rfs_inst_t global_rip, *grip = NULL;
rfs_zone_t global_rzp, *grzp = NULL;

int
rfs_inst_init(void)
{
	uintptr_t zoneaddr, rip_addr, rzp_addr;
	rfs_globals_t rfsg;

	if (mdb_readsym(&zoneaddr, sizeof (uintptr_t), "global_zone") == -1) {
		mdb_warn("failed to find 'global_zone'");
		return (DCMD_ERR);
	}

	/*
	 * If we fail to find "rfs" then the nfssrv kernel module is not
	 * loaded and this means that nothing has been shared in either the
	 * GZ or the NGZ.  This is not a fatal error, return success.
	 */
	if (mdb_readsym(&rfsg, sizeof (rfs_globals_t), "rfs") == -1)
		return (DCMD_OK);

	/*
	 * If we fail to find the address of rfs_zone_t in the GZ then
	 * the nfssrv module is loaded (probably by something being shared
	 * in the NGZ), but the GZ has not shared anything.
	 * This is not a fatal error, return success.
	 */
	rzp_addr = find_globals_bykey(zoneaddr, rfsg.rg_rfszone_key, TRUE);
	if (rzp_addr == NULL)
		return (DCMD_OK);

	if (mdb_vread(&global_rzp, sizeof (rfs_zone_t), rzp_addr) == -1) {
		mdb_warn("failed to read rfs_zone_t (%p) for GZ",
		    rzp_addr);
		return (DCMD_ERR);
	}

	rip_addr =
	    (uintptr_t)((char *)global_rzp.rz_instances.list_head.list_next -
	    global_rzp.rz_instances.list_offset);

	if (mdb_vread(&global_rip, sizeof (rfs_inst_t), rip_addr) == -1) {
		mdb_warn("failed to read rfs_inst_t (%p) for GZ",
		    rip_addr);
		return (DCMD_ERR);
	}

	grzp_addr = rzp_addr;
	grzp = &global_rzp;

	grip_addr = rip_addr;
	grip = &global_rip;

	return (DCMD_OK);
}

/* ARGSUSED */
int
set_rfs_inst(uintptr_t addr)
{
	rfs_inst_t new_inst;
	rfs_zone_t new_rzone;
	uintptr_t new_rip_addr, new_rzp_addr;

	new_rip_addr = addr;
	if (mdb_vread(&new_inst, sizeof (rfs_inst_t), new_rip_addr) == -1) {
		mdb_warn("failed to read rfs_inst_t from %p", new_rip_addr);
		return (DCMD_ERR);
	}

	new_rzp_addr = (uintptr_t)new_inst.ri_rzone;
	if (mdb_vread(&new_rzone, sizeof (rfs_zone_t), new_rzp_addr) == -1) {
		mdb_warn("failed to read rfs_zone_t from %p", new_rzp_addr);
		return (DCMD_ERR);
	}

	crzp_addr = new_rzp_addr;
	current_rzp = new_rzone;
	crzp = &current_rzp;

	crip_addr = new_rip_addr;
	current_rip = new_inst;
	crip = &current_rip;

	return (DCMD_OK);
}

rfs_inst_t *
get_rfs_inst(void)
{
	if (crip)
		return (crip);
	return (grip);
}

uintptr_t
get_rfs_inst_addr(void)
{
	if (crip_addr != 0)
		return (crip_addr);
	return (grip_addr);
}

int
rfs_inst_walk_init(mdb_walk_state_t *wsp)
{
	GElf_Sym sym;

	if (wsp->walk_addr == NULL) {
		if (mdb_lookup_by_name("rfs", &sym) == -1) {
			mdb_warn("failed to read rfs");
			return (WALK_ERR);
		}
		wsp->walk_addr = sym.st_value +
		    offsetof(rfs_globals_t, rg_instances);
	}

	if (mdb_layered_walk("list", wsp) == -1) {
		mdb_warn("failed to walk rg_instances");
		return (WALK_ERR);
	}
	return (WALK_NEXT);
}

int
rfs_inst_walk_step(mdb_walk_state_t *wsp)
{
	uintptr_t addr = wsp->walk_addr;
	rfs_inst_t rfsi;

	NFS_OBJ_FETCH(addr, rfs_inst_t, &rfsi, WALK_ERR);
	return (wsp->walk_callback(wsp->walk_addr, &rfsi, wsp->walk_cbdata));
}

/*ARGSUSED*/
void
rfs_inst_walk_fini(mdb_walk_state_t *wsp)
{
}

void
rfs_inst_help(void)
{
	mdb_printf("rfs inst help\n");
}


int
rfs_inst_dcmd(uintptr_t addr, uint_t flags, int argc, mdb_arg_t *argv)
{
	rfs_inst_t rfsi;
	rfs_zone_t rfsz;
	zone_t azone;

	if (!(flags & DCMD_ADDRSPEC))
		return (mdb_walk_dcmd("rfs_inst", "rfs_inst", argc, argv));

	if (mdb_vread(&rfsi, sizeof (rfs_inst_t), addr) == -1) {
		mdb_warn("failed to read rfs_inst_t from %p\n", addr);
		return (DCMD_ERR);
	}

	if (mdb_vread(&rfsz, sizeof (rfs_zone_t),
	    (uintptr_t)rfsi.ri_rzone) == -1) {
		mdb_warn("failed to read rfs_zone_t from %p", rfsi.ri_rzone);
		return (DCMD_ERR);
	}

	if (mdb_vread(&azone, sizeof (zone_t),
	    (uintptr_t)rfsz.rz_zonep) == -1) {
		mdb_warn("failed to read zone_t from %p", rfsz.rz_zonep);
		return (DCMD_ERR);
	}

	mdb_printf("rfs_inst_t = %p  rfs_zone_t = %p  zone_id = %d\n",
	    addr, rfsi.ri_rzone, azone.zone_id);

	return (DCMD_OK);
}

int
rfs_zone_walk_init(mdb_walk_state_t *wsp)
{
	GElf_Sym sym;

	if (wsp->walk_addr == NULL) {
		if (mdb_lookup_by_name("rfs", &sym) == -1) {
			mdb_warn("failed to read rfs");
			return (WALK_ERR);
		}
		wsp->walk_addr = sym.st_value +
		    offsetof(rfs_globals_t, rg_zones);
	}

	if (mdb_layered_walk("list", wsp) == -1) {
		mdb_warn("failed to walk rg_zones list");
		return (WALK_ERR);
	}
	return (WALK_NEXT);
}

int
rfs_zone_walk_step(mdb_walk_state_t *wsp)
{
	uintptr_t addr = wsp->walk_addr;
	rfs_zone_t rfsz;

	NFS_OBJ_FETCH(addr, rfs_zone_t, &rfsz, WALK_ERR);
	return (wsp->walk_callback(wsp->walk_addr, &rfsz, wsp->walk_cbdata));
}

/*ARGSUSED*/
void
rfs_zone_walk_fini(mdb_walk_state_t *wsp)
{
}

void
rfs_zone_help(void)
{
	mdb_printf("rfs zone help\n");
}

int
rfs_zone_dcmd(uintptr_t addr, uint_t flags, int argc, mdb_arg_t *argv)
{
	rfs_zone_t rfsz;
	zone_t rfsz_zone;

	if (!(flags & DCMD_ADDRSPEC))
		return (mdb_walk_dcmd("rfs_zone", "rfs_zone", argc, argv));

	if (mdb_vread(&rfsz, sizeof (rfs_zone_t), addr) == -1) {
		mdb_warn("failed to read rfs_zone_t from %p", addr);
		return (DCMD_ERR);
	}

	if (mdb_vread(&rfsz_zone, sizeof (zone_t),
	    (uintptr_t)rfsz.rz_zonep) == -1) {
		mdb_warn("failed to read zone_t from %p", rfsz.rz_zonep);
		return (DCMD_ERR);
	}


	mdb_printf("rfs_zone_t = %p  zone_t = %p  zone_id = %d\n",
	    addr, rfsz.rz_zonep, rfsz_zone.zone_id);
	return (DCMD_OK);
}
