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
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include "nfs_mdb.h"
#include "nfs4_mdb.h"
#include <mdb/mdb_ctf.h>

static int print_fname(uintptr_t);

void
nfs4_server_info_help(void)
{
	mdb_printf("<nfs4_server_t>::nfs4_server_info\n"
	    "\t-v\t-> verbose information\n"
	    "\t-s\t-> assumes server is Solaris NFSv4 Server\n\n"
	    "The -s flag enables the dcmd to print server generated\n"
	    "data structures that are norally opaque to the client\n");
}

static const mdb_bitmask_t bm_ns[] = {
	{ "N4S_CLIENTID_SET",	N4S_CLIENTID_SET,	N4S_CLIENTID_SET },
	{ "N4S_CLIENTID_PEND",	N4S_CLIENTID_PEND,	N4S_CLIENTID_PEND },
	{ "N4S_CB_PINGED",	N4S_CB_PINGED,		N4S_CB_PINGED	},
	{ "N4S_CB_WAITER",	N4S_CB_WAITER,		N4S_CB_WAITER	},
	{ "N4S_BADOWNER_DEBUG",	N4S_BADOWNER_DEBUG,	N4S_BADOWNER_DEBUG },
	{ NULL, 0, 0 },
};

char *
lease4_state(lease4_t *l4p)
{
	static char	buf[1024];

	bzero(buf, 1024);

	if (l4p->l4_flags == 0)
		return ("LEASE_INVALID");

	if (l4p->l4_flags & L4_LSE_VALID)
		strcat(buf, "L4_LSE_VALID,");

	if (l4p->l4_flags & L4_THR_ACTIVE)
		strcat(buf, "L4_THR_ACTIVE,");

	if (l4p->l4_flags & L4_THR_KILL)
		strcat(buf, "L4_THR_KILL");

	return (buf);
}

int
nfs4_server_info(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	nfs4_server_t ns;
	int opts = 0;

	if (mdb_getopts(argc, argv,
	    'v', MDB_OPT_SETBITS, NFS_MDB_OPT_VERBOSE, &opts,
	    's', MDB_OPT_SETBITS, NFS_MDB_OPT_SOLARIS_SRV, &opts,
	    NULL) != argc)
		return (DCMD_USAGE);

	if (!(flags & DCMD_ADDRSPEC)) {
		if (mdb_walk_dcmd("nfs4_server", "nfs4_server_info", argc,
		    argv) == -1) {
			mdb_warn("couldn't %s |%s\n", "::walk nfs4_server",
			    "::nfs4_server_info");
			return (DCMD_ERR);
		}
		return (DCMD_OK);
	}

	NFS_OBJ_FETCH(addr, nfs4_server_t, &ns, DCMD_ERR);

	if (ns.saddr.len == 0)
		return (DCMD_OK);

	mdb_printf("Address: %p Zone: %d Server:", addr, ns.zoneid);
	nfs_print_netbuf(&ns.saddr);

	mdb_printf("\nProgram: %x ", ns.s_program);
	mdb_printf("Flags: %b\n", ns.s_flags, bm_ns);

	mdb_printf("Client ID: ");
	nfs4_clientid4_print(&(ns.clientid), &opts);

	mdb_printf("\nCLIDtoSend: ");
	nfs4_client_id4_print(&(ns.clidtosend));

	mdb_printf("\nmntinfo4 list: %p\n", ns.mntinfo4_list);

	mdb_printf("Deleg list: %p ::walk list\n",
	    &((nfs4_server_t *)addr)->s_deleg_list);
	mdb_printf("Lease State:\t%s\n", lease4_state(&ns.s_lease));
	mdb_printf("Lease Time:\t%d sec\n", ns.s_lease.l4_time);

	if (ns.s_lease.l4_last)
		mdb_printf("Last Lease:\t%Y\n", ns.s_lease.l4_last);

	mdb_printf("Credential: %p\n", ns.s_cred);
	mdb_printf("\n");
	return (DCMD_OK);
}

int
nfs4_server_walk_init(mdb_walk_state_t *wsp)
{
	GElf_Sym sym;
	if (wsp->walk_addr == NULL) {
		if (mdb_lookup_by_name("nfs4_server_lst", &sym) == -1) {
			mdb_warn("failed to read `nfs4_server_lst'\n");
			return (WALK_ERR);
		}
		wsp->walk_addr = sym.st_value;
	}
	wsp->walk_data = (void *)wsp->walk_addr;
	return (WALK_NEXT);
}

int
nfs4_server_walk_step(mdb_walk_state_t *wsp)
{
	int status;
	nfs4_server_t ns;

	NFS_OBJ_FETCH(wsp->walk_addr, nfs4_server_t, &ns, WALK_ERR);

	status = wsp->walk_callback(wsp->walk_addr, &ns, wsp->walk_cbdata);

	if (status == WALK_ERR)
		return (WALK_ERR);

	wsp->walk_addr = (uintptr_t)ns.forw;

	if (wsp->walk_addr == (uintptr_t)wsp->walk_data)
		return (WALK_DONE);

	return (WALK_NEXT);
}

/*ARGSUSED*/
void
nfs4_server_walk_fini(mdb_walk_state_t *wsp)
{
}

int
deleg_rnode4_walk_init(mdb_walk_state_t *wsp)
{
	if (mdb_layered_walk("list", wsp) == -1) {
		mdb_warn("couldn't walk 'list'");
		return (WALK_ERR);
	}
	return (WALK_NEXT);
}

int
deleg_rnode4_walk_step(mdb_walk_state_t *wsp)
{
	uintptr_t addr = wsp->walk_addr;
	rnode4_t r4;

	NFS_OBJ_FETCH(addr, rnode4_t, &r4, WALK_ERR);
	return (wsp->walk_callback(addr, &r4, wsp->walk_cbdata));
}

/*ARGSUSED*/
void
deleg_rnode4_walk_fini(mdb_walk_state_t *wsp)
{
}

int
nfs4_svnode_walk_init(mdb_walk_state_t *wsp)
{
	if (wsp->walk_addr == NULL) {
		mdb_warn("global walk not supported\n");
		return (WALK_ERR);
	}
	wsp->walk_data = (void *)wsp->walk_addr;
	return (WALK_NEXT);
}
int
nfs4_svnode_walk_step(mdb_walk_state_t *wsp)
{
	int status = WALK_NEXT;
	svnode_t sv;

	NFS_OBJ_FETCH(wsp->walk_addr, svnode_t, &sv, WALK_ERR);

	status = wsp->walk_callback(wsp->walk_addr, &sv, wsp->walk_cbdata);

	if (status == WALK_ERR)
		return (WALK_ERR);

	wsp->walk_addr = (uintptr_t)sv.sv_forw;

	if (wsp->walk_addr == (uintptr_t)wsp->walk_data)
		return (WALK_DONE);

	return (status);
}

/*ARGSUSED*/
void
nfs4_svnode_walk_fini(mdb_walk_state_t *wsp)
{
}

void
nfs4_svnode_info_help(void)
{
	mdb_printf("<svnode_t>::nfs4_svnode\n");
}

/*ARGSUSED*/
int
nfs4_svnode_info(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	svnode_t sv;

	if (DCMD_HDRSPEC(flags))
		mdb_printf("%-?s %-?s %s\n", "SVNODE", "VNODE", "PATH");

	NFS_OBJ_FETCH(addr, svnode_t, &sv, DCMD_ERR);

	mdb_printf("%-?p %-?p ", addr, sv.sv_r_vnode);
	print_fname((uintptr_t)sv.sv_name);
	mdb_printf("\n");
	return (DCMD_OK);
}

void
nfs4_fname_help(void)
{
	mdb_printf("<nfs4_fname_t>::nfs4_fname\n");
}

/*ARGSUSED*/
int
nfs4_fname(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	mdb_printf("Path:\t");
	print_fname((uintptr_t)addr);
	mdb_printf("\n");
	return (DCMD_OK);
}

/* recursive */
static int
print_fname(uintptr_t addr)
{
	nfs4_fname_t fn;
	char *s;
	int status = 0;

	NFS_OBJ_FETCH(addr, nfs4_fname_t, &fn, -1);

	if (fn.fn_parent != NULL)
		status = print_fname((uintptr_t)fn.fn_parent);

	s = mdb_alloc(fn.fn_len + 1, UM_SLEEP);
	if (mdb_vread(s, fn.fn_len, (uintptr_t)fn.fn_name) == -1) {
		mdb_warn("couldn't read fn_name (%d bytes) at %p\n", fn.fn_len,
		    fn.fn_name);
		status = -1;
		mdb_printf("/??");
		goto bye;
	}
	s[fn.fn_len] = '\0';
	mdb_printf("/%s", s);
bye:
	mdb_free(s, fn.fn_len + 1);
	return (status);
}

static int
nfs4_oo_print(uintptr_t addr, nfs4_open_owner_t *oo, int *opts)
{
	mdb_printf("%-0?p %-0?p %-8d %-8d %s %s\n", addr, oo->oo_cred,
	    oo->oo_ref_count, oo->oo_seqid,
	    oo->oo_just_created ? "True    " : "False   ",
	    oo->oo_seqid_inuse  ? "True    " : "False   ");

	if (opts && ! *opts & NFS_MDB_OPT_VERBOSE) {
		return (DCMD_OK);
	}

	mdb_printf("\topen_owner_name=");
	nfs_bprint(sizeof (uint64_t), (uchar_t *)&oo->oo_name);

	return (DCMD_OK);
}

/* ARGSUSED */
int
nfs4_get_oo_and_print(uintptr_t addr, void *buf, int *opts)
{
	nfs4_open_owner_t oo;

	NFS_OBJ_FETCH(addr, nfs4_open_owner_t, &oo, DCMD_ERR);

	if (nfs4_oo_print(addr, &oo, opts) == -1) {
		mdb_warn("Failed to walk mi_oo_list");
		return (DCMD_ERR);
	}
	return (DCMD_OK);
}

/*
 * nfs4 freed open owner.
 */
int
nfs4_foo_dcmd(uintptr_t addr, uint_t flags, int argc,
		const mdb_arg_t *argv)
{
	mntinfo4_t mi4;
	mdb_ctf_id_t ctf_id;
	ulong_t offset;
	uintptr_t  foo_ptr;
	int opts = 0;

	/*
	 * If no address specified walk the vfs table looking
	 * for NFSv4 vfs entries and call this dcmd with the
	 * vfs.vfs_data pointer (the mntinfo4 address)
	 */
	if (!(flags & DCMD_ADDRSPEC)) {
		int status;
		mdb_arg_t *argvv;

		argvv = mdb_alloc((argc)*sizeof (mdb_arg_t), UM_SLEEP);
		bcopy(argv, argvv, argc*sizeof (mdb_arg_t));
		status = mdb_walk_dcmd("nfs4_mnt", "nfs4_foo", argc, argvv);
		mdb_free(argvv, (argc)*sizeof (mdb_arg_t));
		return (status);
	}

	if (mdb_getopts(argc, argv,
	    'v', MDB_OPT_SETBITS, NFS_MDB_OPT_VERBOSE, &opts,
	    NULL) != argc) {
		return (DCMD_USAGE);
	}

	/* pickup any global options */
	opts |= nfs4_mdb_opt;

	/* read in the mntinfo struct */
	NFS_OBJ_FETCH(addr, mntinfo4_t, &mi4, DCMD_ERR);

	mdb_printf("mntinfo4_t: %p mi_foo_num=%d mi_foo_max=%d\n", addr,
	    mi4.mi_foo_num, mi4.mi_foo_max);

	/*
	 * Walk the open owner circular queue
	 */
	if ((mdb_ctf_lookup_by_name("mntinfo4_t", &ctf_id) == 0) &&
	    (mdb_ctf_offsetof(ctf_id, "mi_foo_list", &offset) == 0) &&
	    (offset % (sizeof (uintptr_t) * NBBY) == 0)) {
		offset /= NBBY;
	} else {
		offset = offsetof(mntinfo4_t, mi_foo_list);
	}

	foo_ptr = addr + offset;

	if (mdb_pwalk("list", (mdb_walk_cb_t)nfs4_get_oo_and_print, &opts,
	    foo_ptr) == -1) {
		mdb_warn("Failed to walk mi_foo_list");
		return (DCMD_ERR);
	}

	return (DCMD_OK);
}

/*
 * nfs4 open owner buckets!.
 */
int
nfs4_oob_dcmd(uintptr_t addr, uint_t flags, int argc,
		const mdb_arg_t *argv)
{
	mdb_ctf_id_t ctf_id;
	ulong_t offset, oo_hb_size;
	uintptr_t  bp, bp_addr;
	int bi;
	int opts = 0;

	/*
	 * If no address specified walk the vfs table looking
	 * for NFSv4 vfs entries and call this dcmd with the
	 * vfs.vfs_data pointer (the mntinfo4 address)
	 */
	if (!(flags & DCMD_ADDRSPEC)) {
		int status;
		mdb_arg_t *argvv;

		argvv = mdb_alloc((argc)*sizeof (mdb_arg_t), UM_SLEEP);
		bcopy(argv, argvv, argc*sizeof (mdb_arg_t));
		status = mdb_walk_dcmd("nfs4_mnt", "nfs4_oob", argc, argvv);
		mdb_free(argvv, (argc)*sizeof (mdb_arg_t));
		return (status);
	}

	if (mdb_getopts(argc, argv,
	    'v', MDB_OPT_SETBITS, NFS_MDB_OPT_VERBOSE, &opts,
	    NULL) != argc) {
		return (DCMD_USAGE);
	}

	/* pickup any global options */
	opts |= nfs4_mdb_opt;

	if ((mdb_ctf_lookup_by_name("mntinfo4_t", &ctf_id) == 0) &&
	    (mdb_ctf_offsetof(ctf_id, "mi_oo_list", &offset) == 0) &&
	    (offset % (sizeof (uintptr_t) * NBBY) == 0)) {
		offset /= NBBY;
	} else {
		offset = offsetof(mntinfo4_t, mi_oo_list);
	}


	bp = addr + offset;

	if ((mdb_ctf_lookup_by_name("nfs4_oo_hash_bucket_t", &ctf_id) != 0))
		oo_hb_size = mdb_ctf_type_size(ctf_id);
	else
		oo_hb_size = sizeof (nfs4_oo_hash_bucket_t);

	if (opts & NFS_MDB_OPT_VERBOSE) {
		mdb_printf("\nmntinfo4 = %p", addr);
	}


	mdb_printf("\n%-?s %-?s %-8s %-8s %s %s %s\n", "OO Address", "Cred",
	    "RefCnt", "SeqID", "JustCre", "SeqInUse", "BadSeqid");

	/*
	 * Iterate over the buckets.
	 */
	for (bi = 0; bi < NFS4_NUM_OO_BUCKETS; bi++) {

		bp_addr = bp + (bi * oo_hb_size);

		if (mdb_pwalk("list", (mdb_walk_cb_t)nfs4_get_oo_and_print,
		    &opts, bp_addr) == -1) {
			mdb_warn("Failed to walk mi_oo_list");
			return (DCMD_ERR);
		}
	}
	return (DCMD_OK);
}

/* ARGSUSED */
int
nfs4_os_print(uintptr_t addr, void *buf, int *opts)
{
	nfs4_open_stream_t os;

	NFS_OBJ_FETCH(addr, nfs4_open_stream_t, &os, DCMD_ERR);

	mdb_printf("%-0?p %-08d %08d  %08d %08d  %08d %08d %08d %08d  "
	    "%08d  %08d  %08d\n", addr, os.os_ref_count, os.os_share_acc_read,
	    os.os_share_acc_write, os.os_mmap_read, os.os_mmap_write,
	    os.os_share_deny_none, os.os_share_deny_read,
	    os.os_share_deny_write, os.os_open_ref_count, os.os_dc_openacc,
	    os.os_mapcnt);

	if (opts && *opts & NFS_MDB_OPT_VERBOSE) {
		mdb_printf("%-?s ", " ");
		if (os.os_valid)
			mdb_printf("os_valid ");
		if (os.os_delegation)
			mdb_printf("os_delegation ");
		if (os.os_final_close)
			mdb_printf("os_final_close ");
		if (os.os_pending_close)
			mdb_printf("os_pending_close ");
		if (os.os_failed_reopen)
			mdb_printf("os_failed_reopen ");
		if (os.os_force_close)
			mdb_printf("os_force_close ");
		mdb_printf("os_orig_oo_name: ");
		nfs_bprint(sizeof (uint64_t), (uchar_t *)&os.os_orig_oo_name);
		mdb_printf("\n");
	}
	return (DCMD_OK);
}

void
nfs4_os_help(void)
{
	mdb_printf("<rnode4_t>::nfs4_os <-v>\n\t-> -v is for verbose mode\n\n");
}

/* ARGSUSED */
int
nfs4_os_dump(uintptr_t addr, void *private, int *opts)
{
	mdb_ctf_id_t ctf_id;
	ulong_t offset;
	uintptr_t osp_list_ptr;

	/*
	 * Walk the rp's r_open_streams list.
	 */
	if ((mdb_ctf_lookup_by_name("rnode4_t", &ctf_id) == 0) &&
	    (mdb_ctf_offsetof(ctf_id, "r_open_streams", &offset) == 0) &&
	    (offset % (sizeof (uintptr_t) * NBBY) == 0)) {
		offset /= NBBY;
	} else {
		offset = offsetof(rnode4_t, r_open_streams);
	}

	osp_list_ptr = addr + offset;

	if (mdb_pwalk("list", (mdb_walk_cb_t)nfs4_os_print, opts,
	    osp_list_ptr) == -1) {
		mdb_warn("Failed to walk r_open_streams");
		return (DCMD_ERR);
	}
	return (DCMD_OK);
}

/*
 * nfs4 open streams.. (not the same as open babbling brooks)
 */
int
nfs4_os_dcmd(uintptr_t addr, uint_t flags, int argc,
		const mdb_arg_t *argv)
{
	int opts = 0;

	if (mdb_getopts(argc, argv,
	    'v', MDB_OPT_SETBITS, NFS_MDB_OPT_VERBOSE, &opts,
	    NULL) != argc) {
		return (DCMD_USAGE);
	}

	/* pickup any global options */
	opts |= nfs4_mdb_opt;

	mdb_printf("%-?s ref     |---- os_share ----|----- os_mmap ----|"
	    "----- os_share_deny ------|   open  |  deleg  |         |\n", " ");

	mdb_printf("%<u>%-?s %-8s|%8s %8s|%8s  %8s|%8s %8s %8s|"
	    "%8s |%8s |%8s |%</u>\n", "Address", "count", "acc_read",
	    "acc_write", "read", "write", "none", "read", "write", "count",
	    "access", "mapcnt");

	/*
	 * If no address specified walk the rnode4 cache
	 */
	if (!(flags & DCMD_ADDRSPEC)) {
		if (mdb_walk("nfs_rtable4", (mdb_walk_cb_t)nfs4_os_dump,
		    &opts) == -1) {
			mdb_warn("unable to walk nfs_rtable4\n");
			return (DCMD_ERR);
		}
		return (DCMD_OK);
	}

	return (nfs4_os_dump(addr, NULL, &opts));
}
