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
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/mdb_modapi.h>
#include <mdb/mdb_ks.h>
#include <sys/id_space.h>
#include <nfs/nfs_srv_inst_impl.h>
#include <nfs/nfs4.h>
#include <nfs/export.h>

#include "nfs_mdb.h"
#include "nfs4_mdb.h"
#include "rfs4_db_mdb.h"


const mdb_bitmask_t nfs4_mdb_opt_bits[] = {
	{"Verbose", NFS_MDB_OPT_VERBOSE, NFS_MDB_OPT_VERBOSE},
	{"Walk_it", NFS_MDB_OPT_WALK_IT, NFS_MDB_OPT_WALK_IT},
	{"Solaris_srv", NFS_MDB_OPT_SOLARIS_SRV, NFS_MDB_OPT_SOLARIS_SRV},
	{"Solaris_clnt", NFS_MDB_OPT_SOLARIS_CLNT, NFS_MDB_OPT_SOLARIS_CLNT},
	{"Current FS Context Set", NFS_MDB_OPT_FS_SET, NFS_MDB_OPT_FS_SET},
	{"Current instance set", NFS_MDB_OPT_INST_SET, NFS_MDB_OPT_INST_SET},
	{NULL, 0, 0 }
};

typedef struct rfs4_bkt_info {
	rfs4_index_t ip;
	rfs4_bucket_t *bkp;
	size_t size;
	int num_bkt;
	int bkt;
} rfs4_bktinfo_t;

typedef struct nfs4_dcmd_info {
	int opt;
	uintptr_t addr;
} nfs4_dcmd_info_t;

static int rfs4_table_print(uintptr_t, rfs4_table_t *, int *);

int nfs4_mdb_opt = 0;

uintptr_t curr_fs_addr;
fsh_entry_t curr_fs, *curr_fsp;

/*
 * WALK: rfs4_bucket... Buckets hang off indexes and so user
 * needs to provide index address then we can walk the bkts,
 * BUT! we also need to know number of buckets so need to
 * also get the table!.
 */
int
rfs4_db_bkt_walk_init(mdb_walk_state_t *wsp)
{
	rfs4_bktinfo_t *bki;
	rfs4_index_t *ip;
	rfs4_bucket_t *bkp;
	rfs4_table_t tbl;

	if (wsp->walk_addr == NULL) {
		mdb_warn("only local rfs4_bucket walk supported\n");
		return (WALK_ERR);
	}

	bki = (rfs4_bktinfo_t *)mdb_zalloc(sizeof (rfs4_bktinfo_t), UM_SLEEP);

	ip = &(bki->ip);

	NFS_OBJ_FETCH(wsp->walk_addr, rfs4_index_t, ip, WALK_ERR);

	NFS_OBJ_FETCH((uintptr_t)ip->dbi_table, rfs4_table_t, &tbl, WALK_ERR);

	bki->size = sizeof (rfs4_bucket_t) * tbl.dbt_len;
	bki->num_bkt = tbl.dbt_len;
	bkp = bki->bkp = (rfs4_bucket_t *)mdb_alloc(bki->size, UM_SLEEP);

	if (mdb_vread(bkp, bki->size, (uintptr_t)ip->dbi_buckets) !=
	    bki->size) {
		mdb_warn("error reading rfs4_bucket at %p", ip->dbi_buckets);
		return (WALK_ERR);
	}

	wsp->walk_data = bki;

	return (WALK_NEXT);
}

int
rfs4_db_bkt_walk_step(mdb_walk_state_t *wsp)
{
	rfs4_bktinfo_t *bki = wsp->walk_data;
	rfs4_bucket_t *bkp;

	if (bki->bkt >= bki->num_bkt)
		return (WALK_DONE);

	bkp = &(bki->bkp[bki->bkt]);

	wsp->walk_addr = (uintptr_t)bkp;


	wsp->walk_callback(wsp->walk_addr, bkp, wsp->walk_cbdata);

	bki->bkt++;
	return (WALK_NEXT);
}

void
rfs4_db_bkt_walk_fini(mdb_walk_state_t *wsp)
{
	rfs4_bktinfo_t *bki = wsp->walk_data;

	mdb_free(bki->bkp, bki->size);
	mdb_free(bki, sizeof (rfs4_bktinfo_t));
}

/*
 * Walk: rfs4_indexs..
 */
int
rfs4_db_idx_walk_init(mdb_walk_state_t *wsp)
{
	if (wsp->walk_addr == NULL) {
		mdb_warn("only local rfs4_table walk supported\n");
		return (WALK_ERR);
	}

	return (WALK_NEXT);
}

int
rfs4_db_idx_walk_step(mdb_walk_state_t *wsp)
{
	rfs4_index_t indx, *ip = &indx;

	NFS_OBJ_FETCH(wsp->walk_addr, rfs4_index_t, ip, WALK_ERR);

	wsp->walk_data = ip;

	wsp->walk_callback(wsp->walk_addr, wsp->walk_data, wsp->walk_cbdata);

	if (ip->dbi_inext == NULL)
		return (WALK_DONE);

	wsp->walk_addr = (uintptr_t)ip->dbi_inext;
	return (WALK_NEXT);
}

/*
 * Walk: rfs4_database tables.
 */
int
rfs4_db_tbl_walk_init(mdb_walk_state_t *wsp)
{
	if (wsp->walk_addr == NULL) {
		mdb_warn("only local rfs4_table walk supported\n");
		return (WALK_ERR);
	}

	return (WALK_NEXT);
}

int
rfs4_db_tbl_walk_step(mdb_walk_state_t *wsp)
{
	rfs4_table_t tbl, *ptbl = &tbl;

	NFS_OBJ_FETCH(wsp->walk_addr, rfs4_table_t, ptbl, WALK_ERR);

	wsp->walk_data = ptbl;

	wsp->walk_callback(wsp->walk_addr, wsp->walk_data, wsp->walk_cbdata);

	if (tbl.dbt_tnext == NULL)
		return (WALK_DONE);

	wsp->walk_addr = (uintptr_t)tbl.dbt_tnext;

	return (WALK_NEXT);
}

static void
rfs4_dbe_print(uintptr_t addr, rfs4_dbe_t *entry)
{
	mdb_printf("DBE {  Address=%-?p data->%-?p ", addr, entry->dbe_data);
	mdb_printf("refcnt=%d ", entry->dbe_refcnt);
	mdb_printf("skisearch=%d\n", entry->dbe_skipsearch);
	mdb_printf("\tinvalid=%d ", entry->dbe_invalid);
	mdb_printf("time_rele=%Y \n}\n", entry->dbe_time_rele);
}


void
nfs4_clientid4_print(clientid4 *cl, int *opts)
{
	mdb_printf("%#llx", *cl);
	if (nfs4_mdb_opt & NFS_MDB_OPT_SOLARIS_SRV ||
	    opts && *opts & NFS_MDB_OPT_SOLARIS_SRV)
		mdb_printf("(srvrboot: %Y, c_id:%u)",
		    *(uint32_t *)cl, *(1 + (uint32_t *)cl));
}

void
nfs4_client_id4_print(nfs_client_id4 *cid)
{
	uchar_t *id_val;
	int id_len;

	mdb_printf("[verifier: ");

	nfs_bprint(NFS4_VERIFIER_SIZE, (uchar_t *)&cid->verifier);

	mdb_printf(", client identifier: ");

	id_len = cid->id_len;
	id_val = mdb_alloc(id_len, UM_SLEEP);

	if (mdb_vread(id_val, id_len, (uintptr_t)cid->id_val) != id_len) {
		mdb_warn("error reading nfs4_client_id4 id_val at %p",
		    cid->id_val);
		return;
	}

	if (nfs4_mdb_opt & NFS_MDB_OPT_SOLARIS_CLNT) {
		int str_len = strlen((char *)id_val);
		mdb_printf("%s/", id_val);
		nfs_print_netbuf_buf((char *)id_val + str_len + 1,
		    id_len - str_len - 1);
	}

	nfs_bprint(id_len, id_val);

	mdb_free(id_val, id_len);
	mdb_printf("] ");
}


static void
rfs4_client_print(uintptr_t addr, rfs4_client_t *cp)
{
	mdb_printf("%-0?p %-0?p %-0llx ", addr, cp->rc_dbe, cp->rc_clientid);

	nfs_bprint(NFS4_VERIFIER_SIZE, (uchar_t *)&cp->rc_confirm_verf);
	mdb_printf("%-5s %-5s ", (cp->rc_need_confirm) ? "True" : "False",
	    (cp->rc_unlksys_completed) ? "True" : "False");
	mdb_printf("%-0?p ", cp->rc_cp_confirmed);
	mdb_printf("%Y\n", cp->rc_last_access);
}

static int
rfs4_delegSid_print(uintptr_t addr, rfs4_deleg_state_t *dsp, int *opts)
{
	rfs4_file_t finfo;
	char buf[78];

	mdb_printf("%-0?p %-0?p %-0llx ", addr, dsp->rds_dbe, dsp->rds_delegid);
	mdb_printf("%-0?p %-0?p\n", dsp->rds_finfo, dsp->rds_client);

	/* get out now if verbose not needed */
	if (! (*opts & NFS_MDB_OPT_VERBOSE)) {
		return (DCMD_OK);
	}

	if (dsp->rds_time_granted)
		mdb_printf("\t\tTime: granted=%Y ", dsp->rds_time_granted);
	else
		mdb_printf("\t\tTime: granted=0 ");

	if (dsp->rds_time_recalled)
		mdb_printf("recalled=%Y ", dsp->rds_time_recalled);
	else
		mdb_printf("recalled=0 ");

	if (dsp->rds_time_revoked)
		mdb_printf("revoked=%Y\n", dsp->rds_time_revoked);
	else
		mdb_printf("revoked=0\n");

	/* show pathname if rfs4_file information is present */
	if (dsp->rds_finfo != NULL) {
		NFS_OBJ_FETCH((uintptr_t)dsp->rds_finfo, rfs4_file_t, &finfo,
		    DCMD_ERR);
		if (mdb_vnode2path((uintptr_t)finfo.rf_vp, buf,
		    sizeof (buf)) == 0)
			mdb_printf("\t\tpath=%s\n", buf);
		else
			mdb_printf("\t\tpath=??\n");
	}
	return (DCMD_OK);
}

/*ARGSUSED*/
static void
rfs4_dinfo_print(rfs4_dinfo_t *dip, int *opts)
{
	mdb_printf("\tdtype=");

	switch (dip->rd_dtype) {
	case OPEN_DELEGATE_NONE:
		mdb_printf("None  ");
		break;
	case OPEN_DELEGATE_READ:
		mdb_printf("Read  ");
		break;
	case OPEN_DELEGATE_WRITE:
		mdb_printf("Write ");
		break;
	default:
		mdb_printf("????? ");
	}

	mdb_printf("rdgrants=%-0d wrgrants=%-0d recall_cnt=%-0d ",
	    dip->rd_rdgrants, dip->rd_wrgrants, dip->rd_recall_count);
	mdb_printf("ever_recalled=%s\n",
	    (dip->rd_ever_recalled) ? "True" : "False");

	mdb_printf("\tTime: ");

	if (dip->rd_time_returned)
		mdb_printf("returned=%Y ", dip->rd_time_returned);
	else
		mdb_printf("returned=0 ");

	if (dip->rd_time_recalled)
		mdb_printf("recalled=%Y\n", dip->rd_time_recalled);
	else
		mdb_printf("recalled=0\n");

	if (dip->rd_time_lastgrant)
		mdb_printf("\t      lastgrant=%Y ", dip->rd_time_lastgrant);
	else
		mdb_printf("\t      lastgrant=0 ");

	if (dip->rd_time_lastwrite)
		mdb_printf("lastwrite=%Y\n", dip->rd_time_lastwrite);
	else
		mdb_printf("lastwrite=0\n");

	if (dip->rd_time_rm_delayed)
		mdb_printf("\t      rm_delayed=%Y\n", dip->rd_time_rm_delayed);
	else
		mdb_printf("\t      rm_delayed=0\n");

	if (dip->rd_conflicted_client) {
		mdb_printf("\tconflicted clientid: ");
		nfs4_clientid4_print(&(dip->rd_conflicted_client), opts);
		mdb_printf("\n");
	}
}

static int
rfs4_file_print(uintptr_t addr, rfs4_file_t *fp, int *opts)
{
	char buf[78];
	rfs4_dinfo_t *dip;
	nfs_fh4 fh;
	uchar_t *fh4_val;

	mdb_printf("%-0?p %-0?p %-0?p ", addr, fp->rf_dbe, fp->rf_vp);

	fh = fp->rf_filehandle;

	fh4_val = (uchar_t *)mdb_alloc(fh.nfs_fh4_len, UM_SLEEP);

	if (mdb_vread(fh4_val, fh.nfs_fh4_len,
	    (uintptr_t)fh.nfs_fh4_val) != fh.nfs_fh4_len) {
		mdb_warn("error reading nfs_fh4_val at %p", fh.nfs_fh4_val);
		mdb_free(fh4_val, fh.nfs_fh4_len);
		return (DCMD_ERR);
	}

	nfs_bprint(fh.nfs_fh4_len, fh4_val);
	mdb_printf("\n");
	mdb_free(fh4_val, fh.nfs_fh4_len);

	if (opts && ! (*opts & NFS_MDB_OPT_VERBOSE)) {
		return (DCMD_OK);
	}

	/* printout the file pathname */
	if (mdb_vnode2path((uintptr_t)fp->rf_vp, buf, sizeof (buf)) == 0)
		mdb_printf("\tpath=%s\n", buf);
	else
		mdb_printf("\tpath=??\n");

	dip = &fp->rf_dinfo;
	rfs4_dinfo_print(dip, opts);
	return (DCMD_OK);
}


static void
rfs4_stateid_print(stateid_t state)
{

	mdb_printf("\tchgseq=%-x boottime=%-x pid=%x\n\ttype=",
	    state.bits.chgseq, state.bits.boottime, state.bits.pid);

	switch (state.bits.type) {
	case OPENID:
		mdb_printf("OpenID ");
		break;
	case LOCKID:
		mdb_printf("LockID ");
		break;
	case DELEGID:
		mdb_printf("DelegID");
		break;
	default:
		mdb_printf("---?---");
	}

	mdb_printf(" ident=%x\n", state.bits.ident);
}

static int
rfs4_lsid_print(uintptr_t addr, rfs4_lo_state_t *los, int *opts)
{
	mdb_printf("%-0?p %-0?p %-0?p %-08d %-llx\n", addr, los->rls_dbe,
	    los->rls_locker, los->rls_seqid, los->rls_lockid);

	if (opts && *opts & NFS_MDB_OPT_VERBOSE) {
		rfs4_stateid_print(los->rls_lockid);
	}
	return (DCMD_OK);
}

static void
rfs4_lock_owner4_print(lock_owner4 *lo)
{
	void *buf;

	mdb_printf("clientid=");

	nfs4_clientid4_print(&(lo->clientid), NULL);

	buf = mdb_alloc(lo->owner_len, UM_SLEEP);

	if (mdb_vread(buf, lo->owner_len,
	    (uintptr_t)lo->owner_val) != lo->owner_len) {
		mdb_warn("error reading lock_owner owner_val at %p",
		    lo->owner_val);
		mdb_free(buf, lo->owner_len);
		return;
	}

	mdb_printf(", owner: ");
	nfs_bprint(lo->owner_len, buf);

	if (nfs4_mdb_opt & NFS_MDB_OPT_SOLARIS_CLNT &&
	    lo->owner_len == 2 * sizeof (int)) {
		int seq, pid;
		seq = *(int *)buf;
		pid = *(1 + (int *)buf);
		mdb_printf("(seq: %d, pid: %d(XXX))", seq, pid);
	}

	mdb_printf("\n");
	mdb_free(buf, lo->owner_len);
}

static void
rfs4_open_owner4_print(open_owner4 *oo)
{
	void *buf;

	mdb_printf("clientid=");
	nfs4_clientid4_print(&(oo->clientid), NULL);

	buf = mdb_alloc(oo->owner_len, UM_SLEEP);

	if (mdb_vread(buf, oo->owner_len,
	    (uintptr_t)oo->owner_val) != oo->owner_len) {
		mdb_warn("error reading open_owner owner_val at %p",
		    oo->owner_val);
		mdb_free(buf, oo->owner_len);
		return;
	}

	mdb_printf(", owner: ");
	nfs_bprint(oo->owner_len, buf);

	if (nfs4_mdb_opt & NFS_MDB_OPT_SOLARIS_CLNT &&
	    oo->owner_len == sizeof (uint64_t)) {
		uint64_t seq, pid;
		seq = *(uint64_t *)buf & 0xFFFFFFFFULL;
		pid = *(uint64_t *)buf >> 32;
		/*
		 * nfs_client_state.c mentions that a Solaris open_owner4 is:
		 * <1><open_owner_seq_num>
		 */
		mdb_printf("(1: %lld, seq: %lld)", pid, seq);
	}

	mdb_printf("\n");
	mdb_free(buf, oo->owner_len);
}

static int
rfs4_lo_print(uintptr_t addr, rfs4_lockowner_t *lo)
{
	mdb_printf("%-0?p %-0?p %-0?p %-05x ", addr, lo->rl_dbe, lo->rl_client,
	    lo->rl_pid);
	rfs4_lock_owner4_print(&lo->rl_owner);
	return (DCMD_OK);
}

static int
rfs4_oo_print(uintptr_t addr, rfs4_openowner_t *oo)
{
	mdb_printf("%-0?p %-0?p %-0?p %-8d ", addr, oo->ro_dbe, oo->ro_client,
	    oo->ro_open_seqid);
	rfs4_open_owner4_print(&oo->ro_owner);
	return (DCMD_OK);
}

static int
rfs4_osid_print(uintptr_t addr, rfs4_state_t *sp, int *opts)
{
	const char *share_type[4] = {
		"none",
		"read",
		"write",
		"read-write"
	};

	mdb_printf("%-0?p %-0?p %-0?p %-0?p %-llx\n",
	    addr, sp->rs_dbe, sp->rs_owner,
	    sp->rs_finfo, sp->rs_stateid);

	if (opts && *opts & NFS_MDB_OPT_VERBOSE) {
		rfs4_stateid_print(sp->rs_stateid);
		mdb_printf("share_access: %s ", sp->rs_share_access < 4 ?
		    share_type[sp->rs_share_access] : "??");
		mdb_printf("share_deny: %s ",
		    sp->rs_share_deny < 4 ? share_type[sp->rs_share_deny] :
		    "??");

		mdb_printf("file is: %s\n", sp->rs_closed ? "CLOSED" : "OPEN");
	}

	return (DCMD_OK);
}

/*ARGSUSED*/
static int
rfs4_client_dump(uintptr_t addr, void *private, int *opts)
{
	rfs4_client_t client;
	rfs4_dbe_t	e;
	uintptr_t	ptr;

	/*
	 * If the passed in address is a dbe then we need to
	 * get the dbe first to figure out the location of
	 * the real data otherwise it's the address of the
	 * real data!
	 */
	if (opts && *opts & NFS_MDB_OPT_DBE_ADDR) {
		NFS_OBJ_FETCH(addr, rfs4_dbe_t, &e, DCMD_ERR);
		ptr = (uintptr_t)e.dbe_data;
	} else {
		ptr = addr;
	}

	NFS_OBJ_FETCH(ptr, rfs4_client_t, &client, DCMD_ERR);

	rfs4_client_print(ptr, &client);

	return (DCMD_OK);
}

/*ARGSUSED*/
static int
rfs4_delegSid_dump(uintptr_t addr, void *private, int *opts)
{
	rfs4_deleg_state_t ds;
	rfs4_dbe_t	e;
	uintptr_t	ptr;

	/*
	 * If the passed in address is a dbe (we're walking)
	 * then we need to get the dbe first to figure out
	 * the location of the real data otherwise it _is_
	 * the address of the real data!
	 */
	if (opts && *opts & NFS_MDB_OPT_DBE_ADDR) {
		NFS_OBJ_FETCH(addr, rfs4_dbe_t, &e, DCMD_ERR);
		ptr = (uintptr_t)e.dbe_data;
	} else {
		ptr = addr;
	}

	NFS_OBJ_FETCH(ptr, rfs4_deleg_state_t, &ds, DCMD_ERR);

	rfs4_delegSid_print(ptr, &ds, opts);

	return (DCMD_OK);
}

/*ARGSUSED*/
static int
rfs4_file_dump(uintptr_t addr, void *private, int *opts)
{
	rfs4_file_t	f;
	rfs4_dbe_t	e;
	uintptr_t	ptr;

	/*
	 * If the passed in address is a dbe then we need to
	 * get the dbe first to figure out the location of
	 * the real data otherwise it's the address of the
	 * real data!
	 */
	if (opts && *opts & NFS_MDB_OPT_DBE_ADDR) {
		NFS_OBJ_FETCH(addr, rfs4_dbe_t, &e, DCMD_ERR);
		ptr = (uintptr_t)e.dbe_data;
	} else {
		ptr = addr;
	}

	NFS_OBJ_FETCH(ptr, rfs4_file_t, &f, DCMD_ERR);

	rfs4_file_print(ptr, &f, opts);

	return (DCMD_OK);
}

/*ARGSUSED*/
static int
rfs4_lsid_dump(uintptr_t addr, void *private, int *opts)
{
	rfs4_lo_state_t	los;
	rfs4_dbe_t	e;
	uintptr_t	ptr;

	/*
	 * If the passed in address is a dbe then we need to
	 * get the dbe first to figure out the location of
	 * the real data otherwise it's the address of the
	 * real data!
	 */
	if (opts && *opts & NFS_MDB_OPT_DBE_ADDR) {
		NFS_OBJ_FETCH(addr, rfs4_dbe_t, &e, DCMD_ERR);
		ptr = (uintptr_t)e.dbe_data;
	} else {
		ptr = addr;
	}

	NFS_OBJ_FETCH(ptr, rfs4_lo_state_t, &los, DCMD_ERR);

	rfs4_lsid_print(ptr, &los, opts);

	return (DCMD_OK);
}

/*ARGSUSED*/
static int
rfs4_lo_dump(uintptr_t addr, void *private, int *opts)
{
	rfs4_lockowner_t lo;
	rfs4_dbe_t	e;
	uintptr_t	ptr;

	/*
	 * If the passed in address is a dbe then we need to
	 * get the dbe first to figure out the location of
	 * the real data otherwise it's the address of the
	 * real data!
	 */
	if (opts && *opts & NFS_MDB_OPT_DBE_ADDR) {
		NFS_OBJ_FETCH(addr, rfs4_dbe_t, &e, DCMD_ERR);
		ptr = (uintptr_t)e.dbe_data;
	} else {
		ptr = addr;
	}

	NFS_OBJ_FETCH(ptr, rfs4_lockowner_t, &lo, DCMD_ERR);

	rfs4_lo_print(ptr, &lo);

	return (DCMD_OK);
}

/*ARGSUSED*/
static int
rfs4_oo_dump(uintptr_t addr, void *private, int *opts)
{
	rfs4_openowner_t oo;
	rfs4_dbe_t	e;
	uintptr_t	ptr;

	/*
	 * If the passed in address is a dbe then we need to
	 * get the dbe first to figure out the location of
	 * the real data otherwise it's the address of the
	 * real data!
	 */
	if (opts && *opts & NFS_MDB_OPT_DBE_ADDR) {
		NFS_OBJ_FETCH(addr, rfs4_dbe_t, &e, DCMD_ERR);
		ptr = (uintptr_t)e.dbe_data;
	} else {
		ptr = addr;
	}

	NFS_OBJ_FETCH(ptr, rfs4_openowner_t, &oo, DCMD_ERR);

	rfs4_oo_print(ptr, &oo);

	return (DCMD_OK);
}

/* ARGSUSED */
static int
rfs4_osid_dump(uintptr_t addr, void *private, int *opts)
{
	rfs4_state_t	os;
	rfs4_dbe_t	e;
	uintptr_t	ptr;

	/*
	 * If the passed in address is a dbe then we need to
	 * get the dbe first to figure out the location of
	 * the real data otherwise it's the address of the
	 * real data!
	 */
	if (opts && *opts & NFS_MDB_OPT_DBE_ADDR) {
		NFS_OBJ_FETCH(addr, rfs4_dbe_t, &e, DCMD_ERR);
		ptr = (uintptr_t)e.dbe_data;
	} else {
		ptr = addr;
	}

	NFS_OBJ_FETCH(ptr, rfs4_state_t, &os, DCMD_ERR);

	rfs4_osid_print(ptr, &os, opts);

	return (DCMD_OK);
}

/* ARGSUSED */
static int
rfs4_bucket_print(uintptr_t addr, rfs4_bucket_t *pbk)
{
	rfs4_link_t	l, *pl = pbk->dbk_head;
	rfs4_dbe_t	e;

	mdb_inc_indent(4);

	while (pl != NULL) {
		NFS_OBJ_FETCH((uintptr_t)pl, rfs4_link_t, &l, DCMD_ERR);
		NFS_OBJ_FETCH((uintptr_t)l.entry, rfs4_dbe_t, &e, DCMD_ERR);
		rfs4_dbe_print((uintptr_t)l.entry, &e);

		pl = l.next;
	}
	mdb_dec_indent(4);

	return (DCMD_OK);
}

/* ARGSUSED */
static int
rfs4_index_print(uintptr_t addr, rfs4_index_t *pidx)
{
	char	keyname[NAMMAX];

	mdb_printf("%-?p ", addr);

	mdb_readstr(keyname, 18, (uintptr_t)pidx->dbi_keyname);

	mdb_printf("%-18s", keyname);
	mdb_printf(" %-5s", (pidx->dbi_createable == TRUE) ? "TRUE" : "FALSE");
	mdb_printf(" %04d", pidx->dbi_tblidx);
	mdb_printf(" %-p\n", pidx->dbi_buckets);

	return (DCMD_OK);
}

/* ARGSUSED */
static int
rfs4_table_print(uintptr_t addr, rfs4_table_t *tb, int *opts)
{
	char tbname[NAMMAX+1];

	mdb_printf("%-?p ", addr);

	mdb_readstr(tbname, 13, (uintptr_t)tb->dbt_name);

	mdb_printf("%-13s %08x", tbname, tb->dbt_debug);
	mdb_printf(" %04d", tb->dbt_count);
	mdb_printf(" %04d", tb->dbt_len);
	mdb_printf(" %016p",  tb->dbt_indices);
	mdb_printf(" %04d", tb->dbt_idxcnt);
	mdb_printf(" %04d", tb->dbt_maxcnt);
	mdb_printf("\n");

	if (opts && *opts & NFS_MDB_OPT_VERBOSE) {
		mdb_inc_indent(8);
		mdb_printf("dbp=%p ", tb->dbt_db);
		mdb_printf("t_lock=[ ");
		nfs_rwlock_print(tb->dbt_t_lock);
		mdb_printf("] lock=[ ");
		nfs_mutex_print(tb->dbt_lock);
		mdb_printf("]\nid_space=%p ", tb->dbt_id_space);
		mdb_printf("min_cache_time=%d ", tb->dbt_min_cache_time);
		mdb_printf("max_cache_time=%d\n", tb->dbt_max_cache_time);

		mdb_printf("usize=%0d\nmaxentries=%0d ", tb->dbt_usize,
		    tb->dbt_maxentries);
		mdb_printf("len=%d ", tb->dbt_len);
		mdb_printf("count=%0d ", tb->dbt_count);
		mdb_printf("idxcnt=%0d ", tb->dbt_idxcnt);
		mdb_printf("maxcnt=%0d ", tb->dbt_maxcnt);
		mdb_printf("ccnt=%0d\n", tb->dbt_ccnt);

		mdb_printf("indices=%p ", tb->dbt_indices);
		mdb_printf(" create=[%a]", tb->dbt_create);
		mdb_printf("destroy=[%a]", tb->dbt_destroy);
		mdb_printf(" expiry=[%a]\n", tb->dbt_expiry);

		mdb_printf("mem_cache=%p debug=%08x ", tb->dbt_mem_cache,
		    tb->dbt_debug);
		mdb_printf("reaper_shutdown=%s\n",
		    (tb->dbt_reaper_shutdown == TRUE) ? "TRUE" : "FALSE");
		mdb_dec_indent(8);
	}
	return (DCMD_OK);
}


static int
rfs4_database_print(uintptr_t paddr, int *opts)
{
	struct rfs4_database	sdb, *dbp = &sdb;

	NFS_OBJ_FETCH(paddr, struct rfs4_database, dbp, DCMD_ERR);

	mdb_printf("rfs4_database=%p\n", paddr);
	mdb_printf("  debug_flags=%08X ", dbp->db_debug_flags);
	mdb_printf("  shutdown:\tcount=%0d\ttables=%p\n",
	    dbp->db_shutdown_count, dbp->db_tables);

	if (dbp->db_tables == NULL) {
		mdb_printf("No Tables.\n");
		return (DCMD_OK);
	}

	if (!(*opts & NFS_MDB_OPT_VERBOSE)) {
		mdb_printf("%-41s %-4s %-22s\n",
		    "------------------ Table -------------------", "Bkt",
		    "------- Indices -------");
		mdb_printf("%-?s %-13s %-8s %-4s %-4s %-16s %-4s %-4s\n",
		    "Address", "Name", "Flags", "Cnt", "Cnt", "Pointer", "Cnt",
		    "Max");
	}

	if (mdb_pwalk("rfs4_db_tbl", (mdb_walk_cb_t)rfs4_table_print, NULL,
	    (uintptr_t)dbp->db_tables) == -1) {
		mdb_warn("failed to walk rfs4 table");
		return (DCMD_ERR);
	}

	return (DCMD_OK);
}

int
rfs4_db_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	uintptr_t paddr;
	int opts = 0;
	rfs_inst_t *rip = get_rfs_inst();

	/*
	 * This dcmd does not support pipe in or out nor
	 * the loop syntax (add,cnt::)
	 */
	if (flags & (DCMD_LOOP | DCMD_LOOPFIRST | DCMD_PIPE | DCMD_PIPE_OUT)) {
		return (DCMD_USAGE);
	}

	if (mdb_getopts(argc, argv, 'v', MDB_OPT_SETBITS, NFS_MDB_OPT_VERBOSE,
	    &opts, NULL) != argc) {
		return (DCMD_USAGE);
	}

	opts |= nfs4_mdb_opt;

	/*
	 * If just envoked with "::dcmd" assume the "anchor" address based
	 * on the current FS context and the per instance r4_server_state
	 * symbol, else user told us the address to start via the "addr::dcmd"
	 * syntax.
	 */
	paddr = addr;
	if (!(flags & DCMD_ADDRSPEC)) {
		/*
		 * Check whether or not the current FS context is set.
		 */
		if (!(nfs4_mdb_opt & NFS_MDB_OPT_FS_SET))
			mdb_printf("Current FS context not set " \
			    "(See [addr]::nfs_set -f)\n");
		else
			mdb_printf("Current FS context = %p\n", curr_fs_addr);

		/*
		 * Print out the per instance database and tables using the
		 * current instance 'r4_server_state' database as the anchor.
		 */
		if (rip == NULL || rip->ri_v4.r4_server_state == NULL) {
			mdb_printf("NFSv4 server not started\n");
			return (DCMD_OK);
		}
		paddr = (uintptr_t)rip->ri_v4.r4_server_state;
		rfs4_database_print(paddr, &opts);

		/*
		 * Determine if the FS context was set using
		 * [address]::nfs_set -f.  If it was, then we will
		 * also print out the per-file system database and
		 * tables.
		 */
		if (nfs4_mdb_opt & NFS_MDB_OPT_FS_SET) {
			ulong_t offset;

			/*
			 * Set the address to point to the rfs4_database_t
			 * pointer element in the fsh_entry_t structure.
			 */
			GETOFFSET(fsh_entry_t, fse_state_store, &offset);
			paddr = curr_fs_addr + offset;

			/*
			 * Retrieve the address of the state_store.
			 */
			if (mdb_vread(&paddr, sizeof (rfs4_database_t *),
			    paddr) == -1) {
				mdb_warn("failed to get rfs4_database address");
				return (DCMD_ERR);
			}
			rfs4_database_print(paddr, &opts);
		}
	} else {
		paddr = addr;
		rfs4_database_print(paddr, &opts);
	}

	return (DCMD_OK);
}

/*
 * DCMD: for rfs4_table_t
 */
int
rfs4_tbl_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	rfs4_table_t tbl, *ptbl = &tbl;
	int opts = 0;


	if (mdb_getopts(argc, argv,
	    'v', MDB_OPT_SETBITS, NFS_MDB_OPT_VERBOSE, &opts,
	    'w', MDB_OPT_SETBITS, NFS_MDB_OPT_WALK_IT, &opts,
	    NULL) != argc)
		return (DCMD_USAGE);

	opts |= nfs4_mdb_opt;


	if (! (flags & DCMD_ADDRSPEC)) {
		mdb_warn("dcmd is local mode only.");
		return (DCMD_USAGE);
	}

	if (!(opts & NFS_MDB_OPT_VERBOSE) && (DCMD_HDRSPEC(flags))) {
		mdb_printf("%-53s %-4s %-22s\n",
		    "------------------------ Table --------------------------",
		    "Bkt", "------- Indices -------");
		mdb_printf("%-?s %-13s %-12s %-8s %-4s %-4s %-16s %-4s %-4s\n",
		    "Address", "Name", "Next", "Flags", "Cnt", "Cnt", "Pointer",
		    "Cnt", "Max");
	}

	if (opts & NFS_MDB_OPT_WALK_IT) {
		if (mdb_pwalk("rfs4_db_tbl", (mdb_walk_cb_t)rfs4_table_print,
		    &opts, (uintptr_t)addr) == -1) {
			mdb_warn("failed to walk rfs4 table");
			return (DCMD_ERR);
		}
	} else {
		NFS_OBJ_FETCH(addr, rfs4_table_t, ptbl, DCMD_ERR);
		rfs4_table_print(addr, ptbl, &opts);
	}

	return (DCMD_OK);
}

/*
 * DCMD: for rfs4_index_t
 */
int
rfs4_idx_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	rfs4_index_t idx, *pidx = &idx;
	int opts = 0;


	if (mdb_getopts(argc, argv,
	    'v', MDB_OPT_SETBITS, NFS_MDB_OPT_VERBOSE, &opts,
	    'w', MDB_OPT_SETBITS, NFS_MDB_OPT_WALK_IT, &opts,
	    NULL) != argc)
		return (DCMD_USAGE);



	if (! (flags & DCMD_ADDRSPEC)) {
		mdb_warn("dcmd is local mode only.");
		return (DCMD_USAGE);
	}

	opts |= nfs4_mdb_opt;

	NFS_OBJ_FETCH(addr, rfs4_index_t, pidx, DCMD_ERR);

	if (DCMD_HDRSPEC(flags) || opts & NFS_MDB_OPT_WALK_IT) {
		mdb_printf("%-16s %-18s %-5s %-4s %-13s\n", "Address", "Name",
		    "Creat", "Tndx", "Bkt Pointer");
		mdb_printf("%s %s %s %s %s\n", "----------------",
		    "------------------", "-----", "----", "-------------");
	}

	if (opts & NFS_MDB_OPT_WALK_IT) {
		if (mdb_pwalk("rfs4_db_idx", (mdb_walk_cb_t)rfs4_index_print,
		    NULL, addr) == -1) {
			mdb_warn("failed to walk rfs4 indexes");
			return (DCMD_ERR);
		}
	} else {
		rfs4_index_print(addr, pidx);
	}

	return (DCMD_OK);
}

/*
 * DCMD: for rfs4_bucket
 */
/*ARGSUSED*/
int
rfs4_bkt_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	if (! (flags & DCMD_ADDRSPEC)) {
		mdb_warn("dcmd is local mode only.");
		return (DCMD_USAGE);
	}

	if (mdb_pwalk("rfs4_db_bkt", (mdb_walk_cb_t)rfs4_bucket_print, NULL,
	    addr) == -1) {
		mdb_warn("failed to walk rfs4 buckets in index");
		return (DCMD_ERR);
	}
	return (DCMD_OK);
}

/*ARGSUSED*/
static int
rfs4_deleg_state_search(uintptr_t addr, void *private, rfs4_client_t *cp)
{
	rfs4_deleg_state_t ds;
	rfs4_dbe_t	e;

	NFS_OBJ_FETCH(addr, rfs4_dbe_t, &e, DCMD_ERR);
	NFS_OBJ_FETCH((uintptr_t)e.dbe_data, rfs4_deleg_state_t, &ds, DCMD_ERR);

	if (cp == ds.rds_client) {
		mdb_printf("%-?s %-?s %-8s %-?s Client\n", "Address", "Dbe",
		    "StateID", "File Info");
		rfs4_delegSid_print((uintptr_t)e.dbe_data, &ds, &nfs4_mdb_opt);
	}
	return (DCMD_OK);
}

/*ARGSUSED*/
static int
rfs4_lo_search(uintptr_t addr, void *private, rfs4_client_t *cp)
{
	rfs4_lockowner_t lo;
	rfs4_dbe_t	e;

	NFS_OBJ_FETCH(addr, rfs4_dbe_t, &e, DCMD_ERR);
	NFS_OBJ_FETCH((uintptr_t)e.dbe_data, rfs4_lockowner_t, &lo, DCMD_ERR);

	if (cp == lo.rl_client) {
		mdb_printf("%-?s %-?s %-?s %-05s Owner\n", "Address", "Dbe",
		    "Client", "Pid");
		rfs4_lo_print((uintptr_t)e.dbe_data, &lo);
	}
	return (DCMD_OK);
}

/*ARGSUSED*/
static int
rfs4_oo_search(uintptr_t addr, void *private, rfs4_client_t *cp)
{
	rfs4_openowner_t oo;
	rfs4_dbe_t	e;

	NFS_OBJ_FETCH(addr, rfs4_dbe_t, &e, DCMD_ERR);
	NFS_OBJ_FETCH((uintptr_t)e.dbe_data, rfs4_openowner_t, &oo, DCMD_ERR);

	if (cp == oo.ro_client) {
		mdb_printf("%-?s %-?s %-?s %-8s Owner\n", "Address", "Dbe",
		    "Client", "OpenSeq");
		rfs4_oo_print((uintptr_t)e.dbe_data, &oo);
	}
	return (DCMD_OK);
}

/*
 * Retrieves the kmem_cache name out of the rfs4_table specified by
 * table_addr.
 *
 * The Client, ClntIP, OpenOwner, Lockowner and File tables are per instance.
 *
 * The OpenStateID, LockStateID and DelegStateID tables specific
 * to each exported file system.  Therefore, each has a different,
 * uniquely named kmem_cache associated with it.
 */
static int
get_kmem_cache_name(char *out_buf, int out_len, uintptr_t table_addr)
{
	rfs4_table_t table;

	if (mdb_vread(&table, sizeof (rfs4_table_t), table_addr) == -1) {
		mdb_warn("failed to read rfs4_table when getting cache name");
		return (DCMD_ERR);
	}

	strncpy(out_buf, table.dbt_cache_name, out_len);

	return (DCMD_OK);
}

/* ARGSUSED */
static int
rfs4_clnt_search(uintptr_t addr, void *private, clientid4 *cid)
{
	rfs4_client_t client;
	rfs4_dbe_t	e;
	uintptr_t	rip_addr;
	uintptr_t	r4i_addr;
	uintptr_t 	paddr, table_addr;
	rfs4_inst_t	r4i;
	ulong_t		offset;
	char		cname[RFS_UNIQUE_BUFLEN];

	rip_addr = get_rfs_inst_addr();
	if (rip_addr == 0) {
		mdb_printf("NFSv4 server not started\n");
		return (DCMD_OK);
	}

	/*
	 * Grab the rfs4_dbe_t and rfs4_client_t
	 */
	NFS_OBJ_FETCH(addr, rfs4_dbe_t, &e, DCMD_ERR);
	NFS_OBJ_FETCH((uintptr_t)e.dbe_data, rfs4_client_t, &client, DCMD_ERR);

	if (client.rc_clientid != *cid) {
		return (DCMD_OK);
	}

	mdb_printf("%-?s %-?s %-16s %-16s %-5s %-5s %-?s Last Access\n",
	    "Address", "dbe", "clientid", "confirm_verf", "NCnfm", "unlnk",
	    "cp_confirmed");
	rfs4_client_print((uintptr_t)e.dbe_data, &client);

	/*
	 * Grab the rfs4_inst_t from the current instance.
	 * This is used in determining the kmem_cache names.
	 */
	GETOFFSET(rfs_inst_t, ri_v4, &offset);
	r4i_addr = (uintptr_t)rip_addr + offset;
	NFS_OBJ_FETCH(r4i_addr, rfs4_inst_t, &r4i, DCMD_ERR);

	/*
	 * Search for openowners for this rfs4_client_t.  Obtain kmem_cache
	 * name from the per instance open owner table.  (See r4_openowner_tab
	 * member of rfs4_inst_t structure.)
	 */
	if (get_kmem_cache_name(cname, sizeof (cname),
	    (uintptr_t)r4i.r4_openowner_tab) == -1) {
		mdb_warn("failed to get openowner table (%p) kmem_cache name",
		    (uintptr_t)r4i.r4_openowner_tab);
		return (DCMD_ERR);
	}

	if (mdb_walk(cname, (mdb_walk_cb_t)rfs4_oo_search, e.dbe_data) == -1) {
		mdb_warn("search failed to walk %s", cname);
		return (DCMD_ERR);
	}

	/*
	 * Search for lockowners for this rfs4_client_t.  Obtain kmem_cache
	 * name from the per instance lock owner table.  (See r4_lockowner_tab
	 * member of rfs4_inst_t structure).
	 */
	if (get_kmem_cache_name(cname, sizeof (cname),
	    (uintptr_t)r4i.r4_lockowner_tab) == -1) {
		mdb_warn("failed to get lockowner tbl (%p) kmem_cache name",
		    (uintptr_t)r4i.r4_lockowner_tab);
		return (DCMD_ERR);
	}

	if (mdb_walk(cname, (mdb_walk_cb_t)rfs4_lo_search, e.dbe_data) == -1) {
		mdb_warn("search failed to walk %s", cname);
		return (DCMD_ERR);

	}

	/*
	 * Search for any Delegation State for this rfs4_client_t.  First,
	 * we have to retrieve the name of the kmem_cache from the per
	 * file system deleg_state_tab.  This is dependent on the
	 * current FS context being set.  If this is not set, an error will
	 * be returned to the user.
	 */
	if (!(nfs4_mdb_opt & NFS_MDB_OPT_FS_SET)) {
		mdb_warn("Could not search for client's delegation state." \
		    "Current FS context not set (See [addr]::nfs_set -f)");
		return (DCMD_ERR);
	}

	GETOFFSET(fsh_entry_t, fse_deleg_state_tab, &offset);
	paddr = curr_fs_addr + offset;

	if (mdb_vread(&table_addr, sizeof (rfs4_table_t *),
	    paddr) == -1) {
		mdb_warn("failed to read rfs4_table address");
		return (DCMD_ERR);
	}

	if (get_kmem_cache_name(cname, sizeof (cname),
	    table_addr) == -1) {
		mdb_warn("failed to get kmem_cache name");
		return (DCMD_ERR);
	}

	if (mdb_walk(cname, (mdb_walk_cb_t)rfs4_deleg_state_search,
	    e.dbe_data) == -1) {
		mdb_warn("search failed to walk %s", cname);
		return (DCMD_ERR);
	}

	return (DCMD_OK);
}


/*
 * DCMD: for rfs4_client kmem_cache
 */
int
rfs4_clnt_kc_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	clientid4 clientid = 0;
	int opts = 0;
	uintptr_t rip_addr;
	uintptr_t r4i_addr;
	rfs4_inst_t r4i;
	ulong_t offset;
	char cname[RFS_UNIQUE_BUFLEN];

	if (mdb_getopts(argc, argv,
	    'v', MDB_OPT_SETBITS, NFS_MDB_OPT_VERBOSE, &opts,
	    'c', MDB_OPT_UINT64, &clientid,
	    NULL) != argc)
		return (DCMD_USAGE);

	opts |= nfs4_mdb_opt;

	/*
	 * Grab the rfs4_inst_t from the current instance.
	 * This is used to determine the kmem_cache names.
	 */
	rip_addr = get_rfs_inst_addr();
	if (rip_addr == 0) {
		mdb_printf("NFSv4 server not started\n");
		return (DCMD_OK);
	}

	GETOFFSET(rfs_inst_t, ri_v4, &offset);
	r4i_addr = rip_addr + offset;
	NFS_OBJ_FETCH(r4i_addr, rfs4_inst_t, &r4i, DCMD_ERR);

	/*
	 * Obtain kmem_cache name from the per instance client table.
	 * (See r4_client_tab member of rfs4_inst_t structure).
	 */
	if (get_kmem_cache_name(cname, sizeof (cname),
	    (uintptr_t)r4i.r4_client_tab) == -1) {
		mdb_warn("failed to get client tbl (%p) kmem_cache name",
		    (uintptr_t)r4i.r4_client_tab);
		return (DCMD_ERR);
	}

	/*
	 * if -c <clientid> specified we are going to search for
	 * the clientid.
	 */
	if (clientid != 0) {
		/* First look for matching rfs4_clients */
		if (mdb_walk(cname, (mdb_walk_cb_t)rfs4_clnt_search,
		    &clientid) == -1) {
			mdb_warn("search failed to walk %s", cname);
			return (DCMD_ERR);
		}

		return (DCMD_OK);
	}

	/*
	 * Otherwise we are going to dump either the whole cache or
	 * a single entry.
	 */
	mdb_printf("%-?s %-?s %-16s %-16s %-5s %-5s %-?s Last Access\n",
	    "Address", "dbe", "clientid", "confirm_verf", "NCnfm", "unlnk",
	    "cp_confirmed");


	if (! (flags & DCMD_ADDRSPEC)) {
		opts |= NFS_MDB_OPT_DBE_ADDR;
		if (mdb_walk(cname, (mdb_walk_cb_t)rfs4_client_dump,
		    &opts) == -1) {
			mdb_warn("failed to walk %s", cname);
			return (DCMD_ERR);
		}

		return (DCMD_OK);
	}

	/* just a single entry to dump */
	return (rfs4_client_dump(addr, NULL, &opts));
}

/*
 * DCMD: for rfs4_deleg_state kmem_cache
 */
int
rfs4_delegState_kc_dcmd(uintptr_t addr, uint_t flags, int argc,
			const mdb_arg_t *argv)
{
	int opts = 0;
	uintptr_t rip_addr;

	rip_addr = get_rfs_inst_addr();
	if (rip_addr == 0) {
		mdb_printf("NFSv4 server not started\n");
		return (DCMD_OK);
	}

	if (mdb_getopts(argc, argv,
	    'v', MDB_OPT_SETBITS, NFS_MDB_OPT_VERBOSE, &opts,
	    NULL) != argc) {
		return (DCMD_USAGE);
	}

	if (!(nfs4_mdb_opt & NFS_MDB_OPT_FS_SET)) {
		mdb_warn("Current FS context not set (See [addr]::nfs_set -f)");
		return (DCMD_ERR);
	}

	mdb_printf("%-?s %-?s %-8s %-?s Client\n", "Address", "Dbe", "StateID",
	    "File Info");

	opts |= nfs4_mdb_opt;

	/*
	 * We are going to dump either the whole cache or
	 * a single entry.
	 */
	if (! (flags & DCMD_ADDRSPEC)) {
		uintptr_t paddr, table_addr;
		ulong_t offset;
		char cname[RFS_UNIQUE_BUFLEN];

		opts |= NFS_MDB_OPT_DBE_ADDR;

		GETOFFSET(fsh_entry_t, fse_deleg_state_tab, &offset);
		paddr = curr_fs_addr + offset;

		if (mdb_vread(&table_addr, sizeof (rfs4_table_t *),
		    paddr) == -1) {
			mdb_warn("failed to read rfs4_table address");
			return (DCMD_ERR);
		}

		if (get_kmem_cache_name(cname, sizeof (cname),
		    table_addr) == -1) {
			mdb_warn("failed to get kmem_cache name");
			return (DCMD_ERR);
		}

		if (mdb_walk(cname, (mdb_walk_cb_t)rfs4_delegSid_dump,
		    &opts) == -1) {
			mdb_warn("failed to walk %s", cname);
			return (DCMD_ERR);
		}
		return (DCMD_OK);
	}

	/* just a single entry to dump */
	return (rfs4_delegSid_dump(addr, NULL, &opts));
}

/*
 * DCMD: for rfs4_file kmem_cache
 */
int
rfs4_file_kc_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	int opts = 0;
	uintptr_t rip_addr;

	rip_addr = get_rfs_inst_addr();
	if (rip_addr == 0) {
		mdb_printf("NFSv4 server not started\n");
		return (DCMD_OK);
	}

	if (mdb_getopts(argc, argv,
	    'v', MDB_OPT_SETBITS, NFS_MDB_OPT_VERBOSE, &opts,
	    NULL) != argc) {
		return (DCMD_USAGE);
	}
	if (!(nfs4_mdb_opt & NFS_MDB_OPT_FS_SET)) {
		mdb_warn("Current FS context not set (See [addr]::nfs_set -f)");
		return (DCMD_ERR);
	}


	mdb_printf("%-?s %-?s %-?s Filehandle\n", "Address", "Dbe", "Vnode");

	opts |= nfs4_mdb_opt;

	if (! (flags & DCMD_ADDRSPEC)) {
		uintptr_t paddr, table_addr;
		ulong_t offset;
		char cname[RFS_UNIQUE_BUFLEN];

		opts |= NFS_MDB_OPT_DBE_ADDR;

		GETOFFSET(fsh_entry_t, fse_file_tab, &offset);
		paddr = curr_fs_addr + offset;

		if (mdb_vread(&table_addr, sizeof (rfs4_table_t *),
		    paddr) == -1) {
			mdb_warn("failed to read rfs4_table address");
			return (DCMD_ERR);
		}

		if (mdb_walk(cname, (mdb_walk_cb_t)rfs4_file_dump,
		    &opts) == -1) {
			mdb_warn("failed to walk %s", cname);
			return (DCMD_ERR);
		}
		return (DCMD_OK);
	}

	/* just a single entry to dump */
	return (rfs4_file_dump(addr, NULL, &opts));
}


/*
 * DCMD: for rfs4_lock_stateid kmem_cache
 */
int
rfs4_loSid_kc_dcmd(uintptr_t addr, uint_t flags, int argc,
		const mdb_arg_t *argv)
{
	int opts = 0;
	uintptr_t rip_addr;

	rip_addr = get_rfs_inst_addr();
	if (rip_addr == 0) {
		mdb_printf("NFSv4 server not started\n");
		return (DCMD_OK);
	}

	if (mdb_getopts(argc, argv,
	    'v', MDB_OPT_SETBITS, NFS_MDB_OPT_VERBOSE, &opts,
	    NULL) != argc) {
		return (DCMD_USAGE);
	}

	if (!(nfs4_mdb_opt & NFS_MDB_OPT_FS_SET)) {
		mdb_warn("Current FS context not set (See [addr]::nfs_set -f)");
		return (DCMD_ERR);
	}

	mdb_printf("%-?s %-?s %-?s %-8s Lockid\n", "Address", "Dbe", "Locker",
	    "seqid");

	opts |= nfs4_mdb_opt;

	/*
	 * We are going to dump either the whole cache or
	 * a single entry.
	 */
	if (! (flags & DCMD_ADDRSPEC)) {
		uintptr_t paddr, table_addr;
		ulong_t offset;
		char cache_nm[RFS_UNIQUE_BUFLEN];

		opts |= NFS_MDB_OPT_DBE_ADDR;

		GETOFFSET(fsh_entry_t, fse_lo_state_tab, &offset);
		paddr = curr_fs_addr + offset;

		if (mdb_vread(&table_addr, sizeof (rfs4_table_t *),
		    paddr) == -1) {
			mdb_warn("failed to read rfs4_table address");
			return (DCMD_ERR);
		}

		if (get_kmem_cache_name(cache_nm, sizeof (cache_nm),
		    table_addr) == -1) {
			mdb_warn("failed to get kmem_cache name");
			return (DCMD_ERR);
		}

		if (mdb_walk(cache_nm, (mdb_walk_cb_t)rfs4_lsid_dump,
		    &opts) == -1) {
			mdb_warn("failed to walk %s", cache_nm);
			return (DCMD_ERR);
		}
		return (DCMD_OK);
	}

	/* just a single entry to dump */
	return (rfs4_lsid_dump(addr, NULL, &opts));
}

/*
 * DCMD: for rfs4_lockowner kmem_cache
 */
int
rfs4_lo_kc_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	int opts = 0;
	uintptr_t rip_addr;

	rip_addr = get_rfs_inst_addr();
	if (rip_addr == 0) {
		mdb_printf("NFSv4 server not started\n");
		return (DCMD_OK);
	}

	if (mdb_getopts(argc, argv,
	    'v', MDB_OPT_SETBITS, NFS_MDB_OPT_VERBOSE, &opts,
	    NULL) != argc) {
		return (DCMD_USAGE);
	}

	mdb_printf("%-?s %-?s %-?s %-05s Owner\n", "Address", "Dbe", "Client",
	    "Pid");

	opts |= nfs4_mdb_opt;

	/*
	 * We are going to dump either the whole cache or
	 * a single entry.
	 */
	if (! (flags & DCMD_ADDRSPEC)) {
		uintptr_t r4i_addr;
		rfs4_inst_t r4i;
		ulong_t offset;
		char cname[RFS_UNIQUE_BUFLEN];

		/*
		 * Grab the rfs4_inst_t from the current instance.
		 * This is used to determine the kmem_cache names.
		 */
		GETOFFSET(rfs_inst_t, ri_v4, &offset);
		r4i_addr = (uintptr_t)rip_addr + offset;
		NFS_OBJ_FETCH(r4i_addr, rfs4_inst_t, &r4i, DCMD_ERR);

		if (get_kmem_cache_name(cname, sizeof (cname),
		    (uintptr_t)r4i.r4_lockowner_tab) == -1) {
			mdb_warn("failed to get lockowner tbl (%p) kmem_cache "
			    "name", (uintptr_t)r4i.r4_lockowner_tab);
			return (DCMD_ERR);
		}

		opts |= NFS_MDB_OPT_DBE_ADDR;
		if (mdb_walk(cname, (mdb_walk_cb_t)rfs4_lo_dump, &opts) == -1) {
			mdb_warn("failed to walk %s", cname);
			return (DCMD_ERR);
		}
		return (DCMD_OK);
	}

	/* just a single entry to dump */
	return (rfs4_lo_dump(addr, NULL, &opts));
}

/*
 * DCMD: for rfs4_openowner kmem_cache
 */
int
rfs4_oo_kc_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	int opts = 0;
	uintptr_t rip_addr;

	rip_addr = get_rfs_inst_addr();
	if (rip_addr == 0) {
		mdb_printf("NFSv4 server not started\n");
		return (DCMD_OK);
	}

	if (mdb_getopts(argc, argv,
	    'v', MDB_OPT_SETBITS, NFS_MDB_OPT_VERBOSE, &opts,
	    NULL) != argc) {
		return (DCMD_USAGE);
	}

	mdb_printf("%-?s %-?s %-?s %-8s Owner\n", "Address", "Dbe", "Client",
	    "OpenSeq");

	opts |= nfs4_mdb_opt;

	if (! (flags & DCMD_ADDRSPEC)) {
		uintptr_t r4i_addr;
		rfs4_inst_t r4i;
		ulong_t offset;
		char cname[RFS_UNIQUE_BUFLEN];

		/*
		 * Grab the rfs4_inst_t from the current instance.
		 * This is used to determine the kmem_cache names.
		 */
		GETOFFSET(rfs_inst_t, ri_v4, &offset);
		r4i_addr = (uintptr_t)rip_addr + offset;
		NFS_OBJ_FETCH(r4i_addr, rfs4_inst_t, &r4i, DCMD_ERR);

		if (get_kmem_cache_name(cname, sizeof (cname),
		    (uintptr_t)r4i.r4_openowner_tab) == -1) {
			mdb_warn("failed to get openowner tbl (%p) kmem_cache "
			    "name", (uintptr_t)r4i.r4_openowner_tab);
			return (DCMD_ERR);
		}

		opts |= NFS_MDB_OPT_DBE_ADDR;
		if (mdb_walk(cname, (mdb_walk_cb_t)rfs4_oo_dump, &opts) == -1) {
			mdb_warn("failed to walk %s", cname);
			return (DCMD_ERR);
		}
		return (DCMD_OK);
	}

	return (rfs4_oo_dump(addr, NULL, &opts));
}

/*
 * DCMD: for OpenStateID kmem_cache
 */
int
rfs4_osid_kc_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	int opts = 0;
	uintptr_t rip_addr;

	rip_addr = get_rfs_inst_addr();
	if (rip_addr == 0) {
		mdb_printf("NFSv4 server not started\n");
		return (DCMD_OK);
	}

	if (mdb_getopts(argc, argv,
	    'v', MDB_OPT_SETBITS, NFS_MDB_OPT_VERBOSE, &opts,
	    NULL) != argc) {
		return (DCMD_USAGE);
	}

	if (!(nfs4_mdb_opt & NFS_MDB_OPT_FS_SET)) {
		mdb_warn("Current FS context not set (See [addr]::nfs_set -f)");
		return (DCMD_ERR);
	}

	mdb_printf("%-?s %-?s %-?s %-?s StateID\n", "Address", "Dbe", "Owner",
	    "finfo");

	opts |= nfs4_mdb_opt;

	if (!(flags & DCMD_ADDRSPEC)) {
		uintptr_t paddr, table_addr;
		ulong_t offset;
		char cache_nm[RFS_UNIQUE_BUFLEN];

		opts |= NFS_MDB_OPT_DBE_ADDR;

		GETOFFSET(fsh_entry_t, fse_state_tab, &offset);
		paddr = curr_fs_addr + offset;

		if (mdb_vread(&table_addr, sizeof (rfs4_table_t *),
		    paddr) == -1) {
			mdb_warn("failed to read rfs4_table address");
			return (DCMD_ERR);
		}

		if (get_kmem_cache_name(cache_nm, sizeof (cache_nm),
		    table_addr) == -1) {
			mdb_warn("failed to get kmem_cache name");
			return (DCMD_ERR);
		}

		if (mdb_walk(cache_nm, (mdb_walk_cb_t)rfs4_osid_dump,
		    &opts) == -1) {
			mdb_warn("failed to walk %s", cache_nm);
			return (DCMD_ERR);
		}
		return (DCMD_OK);
	}

	return (rfs4_osid_dump(addr, NULL, &opts));
}

/*
 * DCMD: for nfs4set -- to set global output options.
 */
/*ARGSUSED*/
int
nfs_setopt_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	fsh_entry_t new_fs;

	nfs4_mdb_opt = 0;

	if (mdb_getopts(argc, argv,
	    'v', MDB_OPT_SETBITS, NFS_MDB_OPT_VERBOSE, &nfs4_mdb_opt,
	    'w', MDB_OPT_SETBITS, NFS_MDB_OPT_WALK_IT, &nfs4_mdb_opt,
	    's', MDB_OPT_SETBITS, NFS_MDB_OPT_SOLARIS_SRV, &nfs4_mdb_opt,
	    'c', MDB_OPT_SETBITS, NFS_MDB_OPT_SOLARIS_CLNT, &nfs4_mdb_opt,
	    'f', MDB_OPT_SETBITS, NFS_MDB_OPT_FS_SET, &nfs4_mdb_opt,
	    'i', MDB_OPT_SETBITS, NFS_MDB_OPT_INST_SET, &nfs4_mdb_opt,
	    NULL) != argc) {
		return (DCMD_USAGE);
	}

	if (nfs4_mdb_opt & NFS_MDB_OPT_FS_SET) {
		if (!(flags & DCMD_ADDRSPEC)) {
			mdb_warn("usage: [address]::nfs_set -f");
			return (DCMD_ERR);
		}

		if (mdb_vread(&new_fs, sizeof (fsh_entry_t), addr) == -1) {
			mdb_warn("failed to read fsh_entry_t from %p", addr);
			return (DCMD_ERR);
		}

		curr_fs_addr = addr;
		curr_fs = new_fs;
		curr_fsp = &new_fs;
	}

	if (nfs4_mdb_opt & NFS_MDB_OPT_INST_SET) {
		if (!(flags & DCMD_ADDRSPEC)) {
			mdb_warn("usage: [address]::nfs_set -i");
			return (DCMD_ERR);
		}

		if (set_rfs_inst(addr) != DCMD_OK) {
			mdb_warn("failed to set rfs instance");
			return (DCMD_ERR);
		}
	}

	if (nfs4_mdb_opt & NFS_MDB_OPT_INST_SET) {
		if (!(flags & DCMD_ADDRSPEC)) {
			mdb_warn("usage: [address]::nfs_set -i");
			return (DCMD_ERR);
		}

		if (set_rfs_inst(addr) != DCMD_OK) {
			mdb_warn("failed to set rfs instance");
			return (DCMD_ERR);
		}
	}

	mdb_printf("%hb\n", nfs4_mdb_opt, nfs4_mdb_opt_bits);

	return (DCMD_OK);
}

/*ARGSUSED*/
int
nfs_getopt_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	uintptr_t rip_addr;

	if (flags & DCMD_ADDRSPEC) {
		return (DCMD_USAGE);
	}

	mdb_printf("Verbose = %s\n",
	    ((nfs4_mdb_opt & NFS_MDB_OPT_VERBOSE) ? "on" : "off"));
	mdb_printf("Walk_it = %s\n",
	    ((nfs4_mdb_opt & NFS_MDB_OPT_WALK_IT) ? "on" : "off"));
	mdb_printf("Solaris_srv = %s\n",
	    ((nfs4_mdb_opt & NFS_MDB_OPT_SOLARIS_SRV) ? "on" : "off"));
	mdb_printf("Solaris_clnt = %s\n",
	    ((nfs4_mdb_opt & NFS_MDB_OPT_SOLARIS_CLNT) ? "on" : "off"));

	if (nfs4_mdb_opt & NFS_MDB_OPT_FS_SET)
		mdb_printf("Current FS = %p\n", curr_fs_addr);
	else
		mdb_printf("Current FS not set");
	/*
	 * If the user explicitly set the instance print:
	 *   "Current instance = [address] (explicit set)"
	 *
	 * If the user did not set the instance and we default to the
	 * global zone instance print:
	 *   "Current instance = [address] (default)"
	 */
	rip_addr = get_rfs_inst_addr();
	mdb_printf("Current instance = %p", rip_addr);
	mdb_printf(" %s\n", ((nfs4_mdb_opt & NFS_MDB_OPT_INST_SET) ?
	    "(explicit set)" : "(default)"));

	return (DCMD_OK);
}
