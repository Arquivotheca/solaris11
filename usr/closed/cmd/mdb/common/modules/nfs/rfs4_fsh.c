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

/*
 * This file contains the functions to interact with the NFSv4 file system
 * hash table.  The data structures of interest are: fsh_bucket and fsh_entry_t
 */

#include <mdb/mdb_modapi.h>
#include <mdb/mdb_ctf.h>
#include <mdb/mdb_ks.h>
#include "rfs4_fsh.h"
#include "nfs_mdb.h"
#include <nfs/nfs_srv_inst_impl.h>

/*
 * ::walk rfs4_fsh_bkt_val
 *     Walk the NFSv4 server file system hash table buckets.
 */
int
rfs4_fsh_bkt_val_walk_init(mdb_walk_state_t *wsp)
{
	rfs4_fsh_val_walk_data_t *data;
	uintptr_t paddr, rip_addr;
	ulong_t offset;

	data = mdb_zalloc(sizeof (rfs4_fsh_val_walk_data_t), UM_SLEEP);

	data->rfvwd_size = FSHTBLSZ * sizeof (fsh_bucket_t);
	data->rfvwd_num_buckets = FSHTBLSZ;

	data->rfvwd_table = (fsh_bucket_t *)mdb_alloc(data->rfvwd_size,
	    UM_SLEEP);

	rip_addr = get_rfs_inst_addr();
	GETOFFSET(rfs_inst_t, ri_fsh_table, &offset);
	paddr = (uintptr_t)rip_addr + offset;

	if ((mdb_vread(data->rfvwd_table, data->rfvwd_size, paddr)) !=
	    data->rfvwd_size) {
		mdb_warn("failed to read fsh_table");
		return (WALK_ERR);
	}

	wsp->walk_data = data;

	return (WALK_NEXT);
}

int
rfs4_fsh_bkt_val_walk_step(mdb_walk_state_t *wsp)
{
	rfs4_fsh_val_walk_data_t *data = wsp->walk_data;
	fsh_bucket_t *bkp;
	int status;

	/*
	 * Determine if we are done.  We are done walking when the current
	 * bucket index is equal to the number of buckets.
	 */
	if (data->rfvwd_idx == data->rfvwd_num_buckets)
		return (WALK_DONE);

	/*
	 * Find the address of the bucket
	 */
	bkp = &(data->rfvwd_table[data->rfvwd_idx]);

	/*
	 * Set walk_addr to point to the bucket.
	 */
	wsp->walk_addr = (uintptr_t)bkp;
	wsp->walk_cbdata = wsp->walk_data;

	status = wsp->walk_callback(wsp->walk_addr, bkp, wsp->walk_cbdata);

	/*
	 * Increment the current bucket number
	 */
	data->rfvwd_idx++;
	return (status);
}

void
rfs4_fsh_bkt_val_walk_fini(mdb_walk_state_t *wsp)
{
	rfs4_fsh_val_walk_data_t *data = wsp->walk_data;

	mdb_free(data->rfvwd_table, data->rfvwd_size);
	mdb_free(data, sizeof (rfs4_fsh_val_walk_data_t));
}

/*
 * ::walk rfs4_fsh_bkt
 * Walk the fsh_table printing out the address of each bucket (fsh_bucket_t)
 */
int
rfs4_fsh_bkt_addr_walk_init(mdb_walk_state_t *wsp)
{
	uintptr_t paddr, rip_addr;
	ulong_t offset;
	rfs4_fsh_addr_walk_data_t *data;

	data = mdb_zalloc(sizeof (rfs4_fsh_addr_walk_data_t), UM_SLEEP);

	data->rfawd_num_buckets = FSHTBLSZ;

	rip_addr = get_rfs_inst_addr();
	GETOFFSET(rfs_inst_t, ri_fsh_table, &offset);
	paddr = (uintptr_t)rip_addr + offset;

	data->rfawd_bkt_addr = paddr;
	wsp->walk_data = data;

	return (WALK_NEXT);
}

int
rfs4_fsh_bkt_addr_walk_step(mdb_walk_state_t *wsp)
{
	rfs4_fsh_addr_walk_data_t *data = wsp->walk_data;
	int status;

	/*
	 * Determine if we are done.  We are done walking when the current
	 * bucket index is equal to the number of buckets.
	 */
	if (data->rfawd_idx == data->rfawd_num_buckets)
		return (WALK_DONE);

	/*
	 * Set walk_addr to point to the bucket.
	 */
	wsp->walk_addr = data->rfawd_bkt_addr;
	wsp->walk_cbdata = wsp->walk_data;

	status = wsp->walk_callback(wsp->walk_addr, &wsp->walk_addr,
	    wsp->walk_cbdata);

	/*
	 * Increment the current bucket number and address
	 */
	data->rfawd_idx++;
	data->rfawd_bkt_addr += sizeof (fsh_bucket_t);

	return (status);
}

void
rfs4_fsh_bkt_addr_walk_fini(mdb_walk_state_t *wsp)
{
	rfs4_fsh_addr_walk_data_t *data = wsp->walk_data;

	mdb_free(data, sizeof (rfs4_fsh_addr_walk_data_t));
}

/*
 * <addr>::walk rfs4_fsh_ent
 *    Given a file system hash table bucket address (fsh_bucket_t),
 *    walk the entries (fsh_entry_t).
 */
int
rfs4_fsh_ent_walk_init(mdb_walk_state_t *wsp)
{
	if (wsp->walk_addr == NULL) {
		mdb_warn("must supply address of fsh_bucket_t");
		return (WALK_ERR);
	}

	/*
	 * Adjust the walk_addr to point to fsb_entries, the list of
	 * fsh_entry_t's for this bucket.
	 */
	wsp->walk_addr += offsetof(fsh_bucket_t, fsb_entries);

	/*
	 * Walk the fsh_entry list.
	 */
	if (mdb_layered_walk("list", wsp) == -1) {
		mdb_warn("failed to walk fsh_entries 'list'");
		return (WALK_ERR);
	}

	return (WALK_NEXT);
}

int
rfs4_fsh_ent_walk_step(mdb_walk_state_t *wsp)
{
	fsh_entry_t fe;

	NFS_OBJ_FETCH(wsp->walk_addr, fsh_entry_t, &fe, WALK_ERR);
	return (wsp->walk_callback(wsp->walk_addr, &fe, wsp->walk_cbdata));
}

/*
 * Prints the fsh_bucket_t information output for the ::rfs4_fsh_stats dcmd
 */
/*ARGSUSED*/
static int
rfs4_fsh_stats_print(uintptr_t addr, fsh_bucket_t *fbp, void *private)
{
	rfs4_fsh_val_walk_data_t *data = private;

	mdb_printf("%8d %11d\n", data->rfvwd_idx, fbp->fsb_chainlen);

	return (DCMD_OK);
}

/*
 * ::rfs4_fsh_stats dcmd
 *
 * Dcmd to dump (summarize) the file system hash bucket distribution info.
 */
/*ARGSUSED*/
int
rfs4_fsh_stats_dcmd(uintptr_t addr, uint_t flags, int argc,
    const mdb_arg_t *argv)
{
	mdb_printf("NFSv4 Server File System Hash Table Distribution Info\n");
	mdb_printf("-----------------------------------------------------\n\n");
	mdb_printf("Bucket #     Chain Length\n");
	mdb_printf("--------     ------------\n");

	/*
	 * Walk the file system hash table buckets.
	 */
	if (mdb_walk("rfs4_fsh_bkt_val", (mdb_walk_cb_t)rfs4_fsh_stats_print,
	    NULL) == -1) {
		mdb_warn("failed to walk file system hash table buckets");
		return (DCMD_ERR);
	}

	return (DCMD_OK);
}

/*
 * ::rfs4_fsh_ent_sum
 *
 * Print a summary of fsh_entry_t. This is intended to display a fsh hash
 * bucket, and hence we only pick the most important pieces
 * of information for the main summary.  More detailed information can
 * always be found by doing a
 * '::print fsh_entry_t' on the underlying fsh_entry_t.
 */

/* ARGSUSED */
int
rfs4_fsh_ent_sum_dcmd(uintptr_t addr, uint_t flags, int argc,
    const mdb_arg_t *argv)
{
	fsh_entry_t	fse;
	char	buf[MAXPATHLEN];

	if (!(flags & DCMD_ADDRSPEC))
		return (DCMD_USAGE);

	if (mdb_vread(&fse, sizeof (fsh_entry_t), addr) == -1) {
		mdb_warn("failed to read fsh_entry_t at %p", addr);
		return (DCMD_ERR);
	}

	if (fse.fse_mntpt == NULL)
		strcpy(buf, "??");
	else {
		int len = mdb_read_refstr((uintptr_t)fse.fse_mntpt, buf,
		    sizeof (buf));
		if (len <= 0)
			strcpy(buf, "??");
	}

	mdb_printf("\n%s\n", buf);
	mdb_inc_indent(4);
	mdb_printf("addr: %-16p    ref: %-8d    state: 0x%x\n",
	    (uintptr_t)addr, fse.fse_refcnt, fse.fse_state);
	mdb_printf("fsid: (0x%x 0x%x)\n", fse.fse_fsid.val[0],
	    fse.fse_fsid.val[1]);
	mdb_dec_indent(4);

	return (DCMD_OK);
}

/*
 * [addr]::rfs4_fsh_bkt_sum
 *
 * Print a summary of all fsh_entry_t structures on the system.
 * This is equivalent to:
 * '::walk rfs4_fsh_bkt | ::walk rfs4_fsh_ent | ::rfs4_fsh_ent_sum'.
 */

/*ARGSUSED*/
static int
rfs4_fsh_sum_cb(uintptr_t addr, fsh_bucket_t *fbp, void *private)
{
	return (mdb_pwalk_dcmd("rfs4_fsh_ent", "rfs4_fsh_ent_sum",
	    NULL, NULL, addr));
}

/*ARGSUSED*/
int
rfs4_fsh_bkt_sum_dcmd(uintptr_t addr, uint_t flags, int argc,
    const mdb_arg_t *argv)
{
	if (!(flags & DCMD_ADDRSPEC))
		addr = 0;

	if (mdb_walk("rfs4_fsh_bkt", (mdb_walk_cb_t)rfs4_fsh_sum_cb,
	    NULL) == -1) {
		mdb_warn("failed to walk file system hash table buckets");
		return (DCMD_ERR);
	}

	return (DCMD_OK);
}
