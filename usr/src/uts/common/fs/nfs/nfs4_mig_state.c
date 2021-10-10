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

#include <nfs/nfs4_mig.h>
#include <sys/sdt.h>
#include <nfs/lm.h>
#include <sys/kmem.h>
#include <nfs/nfs_srv_inst.h>
#include <nfs/nfs_srv_inst_impl.h>

/*
 * NFSv4.0 Transparent State Migration (TSM).
 *
 * The basic TSM design consists of two steps: state harvest (on the source
 * storage server) and state hydrate (on the destination storage server).
 * State harvest consists of walking each state table, XDR encoding the
 * relevant state tokens for each entry of the state table, and writing the
 * XDR-ed state to file. The files are sent from the source to the destination.
 * On the destination server, the migrated state is decoded and inserted into
 * the state tables.
 *
 * The on-disk format of this stake tokens is implicitly defined by the XDR
 * routines. The relationship between the state tables is preserved via the
 * keys used to index into these state table.  For example, while migrating
 * open state, the open_owner4 and clientid4 protocol state tokens are migrated
 * along with each open state entry to re-link the open state with the
 * openowner and client state at the destination.
 *
 * The NFSv4.0 state is written out to files in the case of harvest and read
 * from the files in the case of hydrate. The XDR-encoded migrated state from
 * each state table is kept in a separate file. The details related to the
 * management of XDR buffers and writing those buffers to file is handled by
 * the xdr_vn implementation of the XDR interface (lowest level XDR routines in
 * the XDR implementation). The consumers of xdr_vn, i.e., harvest and hydrate
 * steps of migration, are blissfully unaware of these details. The files
 * carrying the migrated state are kept at the root of the filesystem being
 * migrated, and are carried along with the final snapshot and send of the
 * filesystem.
 *
 * There are three groups of functions:
 *
 * 	TSM Infrastructure Functions
 * 	XDR Encode/Decode Functions
 * 	State Hydrate Functions
 *
 * The "TSM Infrastructure Functions" and "XDR Encode/Decode Functions" (along
 * with the xdr_vn implementation of the XDR interface) form the backbone of
 * TSM and are common to both harvest and hydrate steps. The top level TSM
 * function, that is invoked via the nfssys syscall interface is rfs4_tsm. The
 * "State Hydrate Functions" are responsible for populating the state tables
 * with the migrated state.
 */

static void xdr_rfs4_state(rfs4_entry_t, void *);
static void xdr_rfs4_deleg_state(rfs4_entry_t, void *);
static void xdr_rfs4_lo_state(rfs4_entry_t, void *);
static bool_t xdr_rfs4_openowner(rfs4_openowner_t *, state_mig_t *);
static bool_t xdr_rfs4_client(rfs4_client_t *, state_mig_t *);
static bool_t xdr_rfs4_globals(rfs4_inst_t *, state_mig_t *,
    rfs4_mig_globals_t *);
static int rfs4_openowner_hydrate(rfs_inst_t *, state_mig_t *);
static int rfs4_state_hydrate(rfs_inst_t *, state_mig_t *);
static int rfs4_client_hydrate(rfs_inst_t *, state_mig_t *);
static int rfs4_deleg_state_hydrate(rfs_inst_t *, state_mig_t *);
static int rfs4_lo_state_hydrate(rfs_inst_t *, state_mig_t *);
static int rfs4_globals_hydrate(rfs_inst_t *, fsh_entry_t *, state_mig_t *);
static void mig_dup_destroy_tree(avl_tree_t *);
static int mig_cid_cmpr(const void *, const void *);
static int mig_oo_cmpr(const void *, const void *);
static bool_t xdr_verifier(XDR *, time_t *);

/*
 * NUM_MIGTABS identifies the end of the enum list, and should always be the
 * last element in the list.
 */
enum rfs4_mig_tabid {
    RFS4_CLIENT = 0,
    RFS4_OOWNER,
    RFS4_STATE,
    RFS4_GLOBAL,
    RFS4_DELEG,
    RFS4_LOCKS,
    NUM_MIGTABS
};

struct rfs4_mig_const {
	enum rfs4_mig_tabid tabid;
	char *rfs4_mig_filename;
	char *rfs4_mig_tabtype;
	int (*rfs4_mig_node_cmp)(const void *, const void *);
} tsm_consts[] = {

[RFS4_CLIENT] = {RFS4_CLIENT, ".rfs4_mig_client", "client",  mig_cid_cmpr},
[RFS4_OOWNER] = {RFS4_OOWNER, ".rfs4_mig_oowner", "oowner",  mig_oo_cmpr},
[RFS4_STATE]  = {RFS4_STATE,  ".rfs4_mig_state",  "state",   NULL},
[RFS4_GLOBAL] = {RFS4_GLOBAL, ".rfs4_mig_global", "globals", NULL},
[RFS4_DELEG]  = {RFS4_DELEG,  ".rfs4_mig_deleg",  "delegs",  NULL},
[RFS4_LOCKS]  = {RFS4_LOCKS,  ".rfs4_mig_locks",  "locks",   NULL},
[NUM_MIGTABS] = {NUM_MIGTABS, "last",             "last",    NULL}

};

/*
 * TSM Infrastructure Functions.
 */
static int
mig_tab_fini(state_mig_t *migs, vnode_t *dvp,
    struct rfs4_mig_const *mig_data, int tsm_flag)
{
	int err = 0;
	XDR *xdrs = &(migs->sm_xdrs);
	uint_t all_records_done = LAST_RECORD;

	/*
	 * migs->sm_status catches errors that may have occurred during
	 * initialization, harvest/hydrate, or cleanup.
	 */
	if (migs->sm_status == FALSE) {
		err = -1;
		goto clean;
	}

	/* Harvest: Mark end of migrated records and end of file */
	if (tsm_flag == TSM_HARVEST) {
		if (!xdr_u_int(xdrs, &all_records_done)) {
			err = -1;
			goto clean;
		}
	}

clean:
	xdrvn_destroy(xdrs);

	if (migs->sm_vp) {
		VN_RELE(migs->sm_vp);
		migs->sm_vp = NULL;
	}

	if (err != 0) {
		if ((VOP_REMOVE(dvp, mig_data->rfs4_mig_filename,
		    CRED(), NULL, 0)) != 0) {
			DTRACE_PROBE1(nfss__e__file_remove_failed,
			    char *, mig_data->rfs4_mig_filename);
		}
	}

	if (migs->sm_tab_type) {
		kmem_free(migs->sm_tab_type, MAXNAMELEN);
		migs->sm_tab_type = NULL;
	}

	if (tsm_flag == TSM_HARVEST)
		mig_dup_destroy_tree(&migs->sm_uniq_node_tree);

	return (err);
}

/*
 * mig_fini does a cleanup of the resources and checks for errors. It will
 * return error in two cases: (a) the cleanup of resources itself results in an
 * error; (b) the harvest/hydrate steps executed prior to calling mig_fini
 * resulted in error.
 */
static int
mig_fini(state_mig_t *rfs4_mig_tabs, vnode_t *dvp, int tsm_flag)
{
	state_mig_t *migs;
	int err = 0;
	int tab;

	ASSERT(rfs4_mig_tabs != NULL);
	if (rfs4_mig_tabs == NULL) {
		err = -1;
		goto out;
	}

	/*
	 * An error on just one table during harvest or hydrate is sufficient
	 * to signal failure for the entire harvest or hydrate step. Further,
	 * we do not bail out at the very first instance of an error, since we
	 * need to do cleanup for the rest of the tables as well.
	 */
	for (tab = 0; tab < NUM_MIGTABS; tab++) {
		migs = &rfs4_mig_tabs[tab];
		if (mig_tab_fini(migs, dvp, &tsm_consts[tab], tsm_flag) != 0) {
			err = -1;
		}
	}

out:
	return (err);
}

static int
copy_mig_tab_name(state_mig_t *migs,
    struct rfs4_mig_const *mig_data, char *fs_name)
{
	int tab_type_len = 0;
	int rem_bytes_len = 0;
	char *tab_name = mig_data->rfs4_mig_tabtype;

	migs->sm_tab_type = kmem_alloc(MAXNAMELEN, KM_SLEEP);
	if (strlcpy(migs->sm_tab_type, fs_name, MAXNAMELEN) >= MAXNAMELEN) {
		kmem_free(migs->sm_tab_type, MAXNAMELEN);
		migs->sm_tab_type = NULL;
		return (-1);
	}

	tab_type_len = strnlen(migs->sm_tab_type, MAXNAMELEN);
	migs->sm_tab_type[tab_type_len++] = '-';
	rem_bytes_len = MAXNAMELEN - (tab_type_len + 1);

	if (strlcpy(migs->sm_tab_type + tab_type_len, tab_name,
	    rem_bytes_len) >= rem_bytes_len) {
		kmem_free(migs->sm_tab_type, MAXNAMELEN);
		migs->sm_tab_type = NULL;
		return (-1);
	}

	return (0);
}

static int
mig_tab_init(state_mig_t *migs, vnode_t *dvp, char *fs_name,
    struct rfs4_mig_const *mig_data, int tsm_flag)
{
	int err = 0;
	char *fname = mig_data->rfs4_mig_filename;

	if ((err = copy_mig_tab_name(migs, mig_data, fs_name)) != 0) {
		migs->sm_status = FALSE;
		return (err);
	}

	if (tsm_flag == TSM_HARVEST) {
		vattr_t va;

		va.va_mask = AT_TYPE|AT_MODE|AT_SIZE;
		va.va_type = VREG;
		va.va_mode = (mode_t)0600;
		va.va_size = 0; /* truncate */
		if ((err = VOP_CREATE(dvp, fname, &va, NONEXCL, VWRITE,
		    &(migs->sm_vp), CRED(), 0, NULL, NULL)) != 0) {
			kmem_free(migs->sm_tab_type, MAXNAMELEN);
			migs->sm_tab_type = NULL;
			migs->sm_status = FALSE;
			return (err);
		}
		xdrvn_create(&(migs->sm_xdrs), migs->sm_vp, XDR_ENCODE);

		if (mig_data->rfs4_mig_node_cmp != NULL) {
			avl_create(&migs->sm_uniq_node_tree,
			    mig_data->rfs4_mig_node_cmp,
			    sizeof (uniq_node_t),
			    offsetof(uniq_node_t, un_avl_node));
		}
	} else if (tsm_flag == TSM_HYDRATE) {
		if ((err = lookupnameat(fname, UIO_SYSSPACE,
		    0, NULL, &(migs->sm_vp), dvp)) != 0) {
			kmem_free(migs->sm_tab_type, MAXNAMELEN);
			migs->sm_tab_type = NULL;
			migs->sm_status = FALSE;
			return (err);
		}
		xdrvn_create(&(migs->sm_xdrs), migs->sm_vp, XDR_DECODE);
	}

	migs->sm_status = TRUE;
	migs->sm_rootvp = dvp;

	return (err);
}


static int
mig_init(state_mig_t *rfs4_mig_tabs, vnode_t *dvp, char *fs_name, int tsm_flag)
{
	int err = 0;
	state_mig_t *migs;
	int tab;

	/* Initialize the mig state for each table */
	for (tab = 0; tab < NUM_MIGTABS; tab++) {
		migs = &rfs4_mig_tabs[tab];
		if ((err = mig_tab_init(migs, dvp, fs_name,
		    &tsm_consts[tab], tsm_flag)) != 0)
			return (err);
	}

	return (err);
}

/*
 * key is clientid4
 */
static int
mig_cid_cmpr(const void *n1, const void *n2)
{
	const uniq_node_t *a = n1, *b = n2;

	if (a->un_key < b->un_key)
		return (-1);
	else if (a->un_key > b->un_key)
		return (1);

	return (0);
}

/*
 * key is open_owner4
 */
static int
mig_oo_cmpr(const void *n1, const void *n2)
{
	const uniq_node_t *a = n1, *b = n2;
	int diff;
	open_owner4 *akey = (open_owner4 *)a->un_key;
	open_owner4 *bkey = (open_owner4 *)b->un_key;

	if (akey->clientid < bkey->clientid)
		return (-1);
	else if (akey->clientid > bkey->clientid)
		return (1);

	if (akey->owner_len < bkey->owner_len)
		return (-1);
	else if (akey->owner_len > bkey->owner_len)
		return (1);

	diff = memcmp(akey->owner_val, bkey->owner_val,
	    akey->owner_len);

	if (diff > 0)
		return (1);
	else if (diff < 0)
		return (-1);

	return (0);
}

static int
mig_dup_check(avl_tree_t *tree, uniq_node_t *nodep,
    enum rfs4_mig_tabid tabid)
{
	uniq_node_t *new_nodep = NULL;
	int found = 1;

	ASSERT(tsm_consts[tabid].rfs4_mig_node_cmp != NULL);
	if (tsm_consts[tabid].rfs4_mig_node_cmp == NULL) {
		DTRACE_PROBE1(nfss__e__tsm_cmp_func_null,
		    struct rfs4_mig_const *, &tsm_consts[tabid]);
		return (-1);
	}

	if ((avl_find(tree, nodep, NULL)) == NULL) {
		new_nodep = kmem_zalloc(sizeof (uniq_node_t), KM_SLEEP);
		new_nodep->un_key = nodep->un_key;
		avl_add(tree, new_nodep);
		found = 0;
	} else {
		DTRACE_PROBE1(nfss__i__tsm_dup_detected,
		    char *, tsm_consts[tabid].rfs4_mig_tabtype);
	}

	return (found);
}

static void
mig_dup_destroy_tree(avl_tree_t *avl)
{
	void *nodep;

	if (avl == NULL)
		return;

	for (nodep = avl_first(avl); nodep != NULL; nodep = avl_first(avl)) {
		avl_remove(avl, nodep);
		kmem_free(nodep, sizeof (uniq_node_t));
	}
	avl_destroy(avl);
}

/* ARGSUSED */
migerr_t
rfs4_tsm(void *rp, vnode_t *dvp, int tsm_flag)
{
	int err = 0, err_fini = 0;
	fsh_entry_t *fs_entry = NULL;
	state_mig_t *rfs4_mig_tabs = NULL;
	state_mig_lock_t rfs4_mig_lock_tab;
	rfs4_mig_globals_t mig_globals;
	rfs_inst_t *rip;

	if (dvp == NULL)
		return (MIGERR_FSINVAL);

	ASSERT(tsm_flag == TSM_HARVEST || tsm_flag == TSM_HYDRATE);
	if (tsm_flag != TSM_HARVEST && tsm_flag != TSM_HYDRATE) {
		return (MIGERR_OP_ILLEGAL);
	}

	rip = rfs_inst_find(FALSE);
	if (rip == NULL)
		return (MIGERR_NONFSINST);

	/* can't migrate to or from server that is not v4 enabled */
	if (rip->ri_v4.r4_enabled == 0 ||
	    (rip->ri_v4.r4_server_state == NULL)) {
		rfs_inst_active_rele(rip);
		return (MIGERR_NOSTATE);
	}

	if (rfs4_grace_in(rip->ri_v4.r4_cur_grace)) {
		rfs_inst_active_rele(rip);
		return (MIGERR_INGRACE);
	}

	fs_entry = fsh_get_ent(rip, dvp->v_vfsp->vfs_fsid);
	if (fs_entry == NULL) {
		rfs_inst_active_rele(rip);
		return (MIGERR_FSNOENT);
	}

	mutex_enter(&fs_entry->fse_lock);
	if (nfs_rw_lock_held(&fs_entry->fse_freeze_lock,
	    RW_WRITER) == FALSE) {
		mutex_exit(&fs_entry->fse_lock);
		fsh_ent_rele(rip, fs_entry);
		rfs_inst_active_rele(rip);
		return (MIGERR_FSNOTFROZEN);
	}

	if (fs_entry->fse_state & FSE_TSM) {
		mutex_exit(&fs_entry->fse_lock);
		fsh_ent_rele(rip, fs_entry);
		rfs_inst_active_rele(rip);
		return (MIGERR_ALREADY);
	} else if (fs_entry->fse_state & FSE_MOVED) {
		mutex_exit(&fs_entry->fse_lock);
		fsh_ent_rele(rip, fs_entry);
		rfs_inst_active_rele(rip);
		return (MIGERR_FSMOVED);
	} else {
		fs_entry->fse_state |= FSE_TSM;
	}
	mutex_exit(&fs_entry->fse_lock);

	rfs4_mig_tabs = (state_mig_t *)kmem_zalloc(
	    sizeof (state_mig_t) * NUM_MIGTABS, KM_SLEEP);

	/*
	 * The init routines can fail selectively for one or more tables.
	 * Hence, it is necessary to call the cleanup routines.
	 */
	if ((mig_init(rfs4_mig_tabs, dvp, fs_entry->fse_name,
	    tsm_flag)) != 0) {
		goto out;
	}

	if (tsm_flag == TSM_HARVEST) {
		/*
		 * Harvest the open state table. While harvesting open state,
		 * harvest the client and openowner associated with it at the
		 * same time.
		 */
		rfs4_dbe_walk(fs_entry->fse_state_tab, xdr_rfs4_state,
		    rfs4_mig_tabs);
		if (rfs4_mig_tabs[RFS4_STATE].sm_status == FALSE)
			goto out;

		/* harvest delegation state */
		rfs4_dbe_walk(fs_entry->fse_deleg_state_tab,
		    xdr_rfs4_deleg_state, rfs4_mig_tabs);
		if (rfs4_mig_tabs[RFS4_DELEG].sm_status == FALSE)
			goto out;

		/* harvest lock state and record locks */
		rfs4_mig_lock_tab.sml_lo_state_tab = &rfs4_mig_tabs[RFS4_LOCKS];
		rfs4_mig_lock_tab.sml_lo_list = NULL;

		rfs4_dbe_walk(fs_entry->fse_lo_state_tab, xdr_rfs4_lo_state,
		    &rfs4_mig_lock_tab);
		if (rfs4_mig_tabs[RFS4_LOCKS].sm_status == FALSE)
			goto out;

		/* harvest global state */
		mig_globals.mig_stateid_verifier =
		    fs_entry->fse_stateid_verifier;
		if (fs_entry->fse_wr4verf != 0)
			mig_globals.mig_write4verf = fs_entry->fse_wr4verf;
		else
			mig_globals.mig_write4verf = rfs.rg_v4.rg4_write4verf;

		if (!xdr_rfs4_globals(&rip->ri_v4, rfs4_mig_tabs, &mig_globals))
			goto out;
	} else if (tsm_flag == TSM_HYDRATE) {
		/*
		 * clean up old state in case source server
		 * restarted in previous migration
		 */
		rfs4_clean_state_fse(fs_entry);

		/* The order of hydration is important */
		if ((err = rfs4_client_hydrate(rip, rfs4_mig_tabs)) != 0)
			goto out;
		if ((err = rfs4_openowner_hydrate(rip, rfs4_mig_tabs)) != 0)
			goto out;
		if ((err = rfs4_state_hydrate(rip, rfs4_mig_tabs)) != 0)
			goto out;
		if ((err = rfs4_deleg_state_hydrate(rip, rfs4_mig_tabs)) != 0)
			goto out;
		if ((err = rfs4_lo_state_hydrate(rip, rfs4_mig_tabs)) != 0)
			goto out;
		if ((err = rfs4_globals_hydrate(rip, fs_entry, rfs4_mig_tabs))
		    != 0)
			goto out;
	}

out:
	err_fini = mig_fini(rfs4_mig_tabs, dvp, tsm_flag);
	if (rfs4_mig_tabs) {
		kmem_free(rfs4_mig_tabs, sizeof (state_mig_t) * NUM_MIGTABS);
		rfs4_mig_tabs = NULL;
	}
	if ((err || err_fini) && tsm_flag == TSM_HYDRATE) {
		rfs4_clean_state_fse(fs_entry);
	}

	fsh_ent_rele(rip, fs_entry);
	rfs_inst_active_rele(rip);

	return (((err == 0) && (err_fini == 0)) ? MIG_OK : MIGERR_TSMFAIL);
}

/*
 * State Hydrating Functions
 */
static void
rfs4_free_client_xdrbufs(rfs4_client_t *cl)
{
	char	*id_val, *netid, *addr;

	if ((id_val = cl->rc_nfs_client.id_val) != NULL) {
		kmem_free(id_val, cl->rc_nfs_client.id_len);
		cl->rc_nfs_client.id_val = NULL;
	}

	if ((netid = cl->rc_cbinfo.cb_callback.cb_location.r_netid) != NULL) {
		kmem_free(netid, (strlen(netid) + 1));
		cl->rc_cbinfo.cb_callback.cb_location.r_netid = NULL;
	}

	if ((addr = cl->rc_cbinfo.cb_callback.cb_location.r_addr) != NULL) {
		kmem_free(addr, (strlen(addr) + 1));
		cl->rc_cbinfo.cb_callback.cb_location.r_addr = NULL;
	}
}

static void
rfs4_free_openowner_xdrbuf(rfs4_openowner_t *owner)
{
	nfs_fh4 *fh;

	if (owner->ro_owner.owner_val != NULL) {
		kmem_free(owner->ro_owner.owner_val, owner->ro_owner.owner_len);
		owner->ro_owner.owner_val = NULL;
	}

	fh = &owner->ro_reply_fh;
	if (fh->nfs_fh4_val != NULL) {
		kmem_free(fh->nfs_fh4_val, fh->nfs_fh4_len);
		fh->nfs_fh4_val = NULL;
	}
	rfs4_free_reply(&owner->ro_reply);
}

static void
rfs4_free_openstate_xdrbufs(rfs4_state_t *os)
{
	/* Free memory allocated during XDR decoding */
	if (os->rs_owner->ro_owner.owner_val) {
		kmem_free(os->rs_owner->ro_owner.owner_val,
		    os->rs_owner->ro_owner.owner_len);
		os->rs_owner->ro_owner.owner_val = NULL;
	}

	if (os->rs_finfo->rf_filehandle.nfs_fh4_val) {
		kmem_free(os->rs_finfo->rf_filehandle.nfs_fh4_val,
		    os->rs_finfo->rf_filehandle.nfs_fh4_len);
		os->rs_finfo->rf_filehandle.nfs_fh4_val = NULL;
	}

	if (os->rs_finfo->rf_vp->v_path) {
		kmem_free(os->rs_finfo->rf_vp->v_path,
		    (strlen(os->rs_finfo->rf_vp->v_path) + 1));
		os->rs_finfo->rf_vp->v_path = NULL;
	}
}

static void
rfs4_free_delegstate_xdrbufs(rfs4_deleg_state_t *ds)
{
	if (ds->rds_finfo->rf_filehandle.nfs_fh4_val) {
		kmem_free(ds->rds_finfo->rf_filehandle.nfs_fh4_val,
		    ds->rds_finfo->rf_filehandle.nfs_fh4_len);
		ds->rds_finfo->rf_filehandle.nfs_fh4_val = NULL;
	}
}

static void
rfs4_free_lo_state_xdrbufs(rfs4_lo_state_t *ls,
    state_mig_lock_t *rfs4_mig_lock_tab) {

	locklist_t *llp, *next_llp;

	if (ls->rls_locker->rl_owner.owner_val) {
		kmem_free(ls->rls_locker->rl_owner.owner_val,
		    ls->rls_locker->rl_owner.owner_len);
		ls->rls_locker->rl_owner.owner_val = NULL;
	}
	rfs4_free_reply(&ls->rls_reply);

	llp = rfs4_mig_lock_tab->sml_lo_list;
	while (llp) {
		next_llp = llp->ll_next;
		kmem_free(llp, sizeof (*llp));
		llp = next_llp;
	}
	rfs4_mig_lock_tab->sml_lo_list = NULL;
}

static int
rfs4_openowner_hydrate(rfs_inst_t *rip, state_mig_t *rfs4_mig_tabs)
{
	state_mig_t		*migs = &rfs4_mig_tabs[RFS4_OOWNER];
	int 			err = 0;
	int			cnt = 0;
	rfs4_openowner_t	oo;
	uint_t 			record = 0;
	XDR 			*xdrs;

	xdrs = &migs->sm_xdrs;
	if (!xdr_u_int(xdrs, &record))
		return (-1);

	if (record == LAST_RECORD) {
		DTRACE_PROBE1(nfss__i__no_records_hydrated,
		    char *, migs->sm_tab_type);
		return (0);
	}

	while (record == NOT_LAST_RECORD) {
		bzero(&oo, sizeof (rfs4_openowner_t));
		if (!xdr_rfs4_openowner(&oo, rfs4_mig_tabs)) {
			err = -1;
			break;
		}

		/* do hydrate */
		if ((err = rfs4_do_openowner_hydrate(rip, &oo)) != 0)
			break;
		cnt++;

		if (!xdr_u_int(xdrs, &record)) {
			err = -1;
			break;
		}
		rfs4_free_openowner_xdrbuf(&oo);
	}

	if (err)
		rfs4_free_openowner_xdrbuf(&oo);
	DTRACE_PROBE2(nfss__i__num_records_hydrated,
	    int, cnt, char *, migs->sm_tab_type);

	return (err);
}

static int
rfs4_client_hydrate(rfs_inst_t *rip, state_mig_t *rfs4_mig_tabs)
{
	state_mig_t	*migs = &rfs4_mig_tabs[RFS4_CLIENT];
	int 		err = 0;
	int		cnt = 0;
	rfs4_client_t	client;
	uint_t 		record = 0;
	XDR 		*xdrs;

	xdrs = &migs->sm_xdrs;
	if (!xdr_u_int(xdrs, &record))
		return (-1);

	if (record == LAST_RECORD) {
		DTRACE_PROBE1(nfss__i__no_records_hydrated,
		    char *, migs->sm_tab_type);
		return (0);
	}

	while (record == NOT_LAST_RECORD) {
		bzero(&client, sizeof (rfs4_client_t));

		/* XDR in the migrated state from the harvest file */
		if (!xdr_rfs4_client(&client, rfs4_mig_tabs)) {
			err = -1;
			break;
		}

		/* hydrate the migrated state */
		if ((err = rfs4_do_client_hydrate(rip, &client)))
			break;
		cnt++;

		if (!xdr_u_int(xdrs, &record)) {
			err = -1;
			break;
		}
		rfs4_free_client_xdrbufs(&client);
	}

	if (err)
		rfs4_free_client_xdrbufs(&client);
	DTRACE_PROBE2(nfss__i__num_records_hydrated,
	    int, cnt, char *, migs->sm_tab_type);

	return (err);
}

static int
rfs4_state_hydrate(rfs_inst_t *rip, state_mig_t *rfs4_mig_tabs)
{
	state_mig_t	*migs = &rfs4_mig_tabs[RFS4_STATE];
	int		err = 0;
	int		cnt = 0;
	rfs4_state_t	os;
	rfs4_openowner_t oo;
	rfs4_file_t	file;
	vnode_t		vp;
	XDR 		*xdrs;
	uint_t		record = 0;


	/* Decode and print rfs4_state_t records */
	xdrs = &migs->sm_xdrs;
	if (!xdr_u_int(xdrs, &record))
		return (-1);

	if (record == LAST_RECORD) {
		DTRACE_PROBE1(nfss__i__no_records_hydrated,
		    char *, migs->sm_tab_type);
		return (0);
	}

	while (record == NOT_LAST_RECORD) {
		bzero(&os, sizeof (rfs4_state_t));
		bzero(&oo, sizeof (rfs4_openowner_t));
		bzero(&file, sizeof (rfs4_file_t));
		bzero(&vp, sizeof (vnode_t));
		os.rs_owner = &oo;
		os.rs_finfo = &file;
		os.rs_finfo->rf_vp = &vp;

		xdr_rfs4_state((rfs4_entry_t)&os, rfs4_mig_tabs);
		if (migs->sm_status == FALSE) {
			err = -1;
			break;
		}

		/* hydrate the open state */
		if ((err = rfs4_do_state_hydrate(rip, &os, migs->sm_rootvp)))
			break;
		cnt++;

		if (!xdr_u_int(xdrs, &record)) {
			err = -1;
			break;
		}
		rfs4_free_openstate_xdrbufs(&os);
	}

	if (err)
		rfs4_free_openstate_xdrbufs(&os);
	DTRACE_PROBE2(nfss__i__num_records_hydrated,
	    int, cnt, char *, migs->sm_tab_type);

	return (err);
}

static int
rfs4_deleg_state_hydrate(rfs_inst_t *rip, state_mig_t *rfs4_mig_tabs)
{
	int		cnt = 0;
	int		err = 0;
	uint_t		record = 0;
	state_mig_t	*migs = &rfs4_mig_tabs[RFS4_DELEG];
	XDR 		*xdrs;
	rfs4_file_t	file;
	rfs4_client_t	client;
	rfs4_deleg_state_t	ds;

	xdrs = &migs->sm_xdrs;

	/* check end-of-record marker */
	if (!xdr_u_int(xdrs, &record))
		return (-1);

	if (record == LAST_RECORD) {
		DTRACE_PROBE1(nfss__i__no_records_hydrated,
		    char *, migs->sm_tab_type);
		return (0);
	}

	while (record == NOT_LAST_RECORD) {
		bzero(&file, sizeof (rfs4_file_t));
		bzero(&ds, sizeof (rfs4_deleg_state_t));
		bzero(&client, sizeof (rfs4_client_t));

		ds.rds_finfo = &file;
		ds.rds_client = &client;

		xdr_rfs4_deleg_state((rfs4_entry_t)&ds, rfs4_mig_tabs);
		if (migs->sm_status == FALSE) {
			err = -1;
			break;
		}

		/* hydrate the delegation state */
		if ((err = rfs4_do_deleg_state_hydrate(rip, &ds,
		    migs->sm_rootvp)))
			break;
		cnt++;

		if (!xdr_u_int(xdrs, &record)) {
			err = -1;
			break;
		}
		rfs4_free_delegstate_xdrbufs(&ds);
	}

	if (err)
		rfs4_free_delegstate_xdrbufs(&ds);
	DTRACE_PROBE2(nfss__i__num_records_hydrated,
	    int, cnt, char *, migs->sm_tab_type);

	return (err);
}

static int
rfs4_lo_state_hydrate(rfs_inst_t *rip, state_mig_t *rfs4_mig_tabs)
{
	state_mig_lock_t rfs4_mig_lock_tab;
	int		err = 0;
	int		cnt = 0;
	rfs4_lo_state_t	ls;
	rfs4_lockowner_t lo_owner;
	rfs4_state_t	os;
	rfs4_file_t	finfo;
	XDR 		*xdrs;
	uint_t		record = 0;
	state_mig_t	*migs;

	/*
	 * lock state hydration is a little different than other
	 * tables, since we carry record locks along with the lock
	 * state.
	 */
	migs = rfs4_mig_lock_tab.sml_lo_state_tab = &rfs4_mig_tabs[RFS4_LOCKS];
	rfs4_mig_lock_tab.sml_lo_list = NULL;

	/* Decode rfs4_lo_state_t records */
	xdrs = &migs->sm_xdrs;
	if (!xdr_u_int(xdrs, &record))
		return (-1);

	if (record == LAST_RECORD) {
		DTRACE_PROBE1(nfss__i__no_records_hydrated,
		    char *, migs->sm_tab_type);
		return (0);
	}

	while (record == NOT_LAST_RECORD) {
		bzero(&ls, sizeof (rfs4_lo_state_t));
		bzero(&lo_owner, sizeof (rfs4_lockowner_t));
		bzero(&os, sizeof (rfs4_state_t));
		bzero(&finfo, sizeof (rfs4_file_t));
		ls.rls_locker = &lo_owner;
		ls.rls_state = &os;
		os.rs_finfo = &finfo;

		xdr_rfs4_lo_state((rfs4_entry_t)&ls, &rfs4_mig_lock_tab);
		if (migs->sm_status == FALSE) {
			err = -1;
			break;
		}

		/* hydrate the lock state and record locks */
		if ((err = rfs4_do_lo_state_hydrate(rip, &ls,
		    &rfs4_mig_lock_tab)))
			break;
		cnt++;

		if (!xdr_u_int(xdrs, &record)) {
			err = -1;
			break;
		}
		rfs4_free_lo_state_xdrbufs(&ls, &rfs4_mig_lock_tab);
	}

	if (err)
		rfs4_free_lo_state_xdrbufs(&ls, &rfs4_mig_lock_tab);
	DTRACE_PROBE2(nfss__i_2num_records_hydrated,
	    int, cnt, char *, migs->sm_tab_type);

	return (err);
}

static int
rfs4_globals_hydrate(rfs_inst_t *rip, fsh_entry_t *fs_entry,
    state_mig_t *rfs4_mig_tabs)
{
	rfs4_mig_globals_t	mig_globals;
	int			err = 0;

	/* decode the migrated state from file to memory */
	if (!xdr_rfs4_globals(&rip->ri_v4, rfs4_mig_tabs, &mig_globals)) {
		err = -1;
		goto out;
	}

	/* hydrate the migrated state */
	if ((err = rfs4_do_globals_hydrate(fs_entry, &mig_globals))) {
		err = -1;
		goto out;
	}

out:
	return (err);
}


/*
 * XDR Encode/Decode Functions.
 */


static bool_t
xdr_mig_stateid(XDR *xdrs, stateid_t *stateidp)
{
	unsigned int mig_bt, mig_type, mig_mcid, mig_clnodeid, mig_ident;

	if (!xdr_u_int(xdrs, &stateidp->bits.chgseq))
		return (FALSE);

	/*
	 * stateid4 is interpreted by the destination server
	 */
	mig_bt = stateidp->bits.boottime;
	if (!xdr_u_int(xdrs, &mig_bt))
		return (FALSE);
	mig_type = stateidp->bits.type;
	if (!xdr_u_int(xdrs, &mig_type))
		return (FALSE);
	mig_mcid = stateidp->bits.mcid;
	if (!xdr_u_int(xdrs, &mig_mcid))
		return (FALSE);
	mig_clnodeid = stateidp->bits.clnodeid;
	if (!xdr_u_int(xdrs, &mig_clnodeid))
		return (FALSE);
	mig_ident = stateidp->bits.ident;
	if (!xdr_u_int(xdrs, &mig_ident))
		return (FALSE);
	if (xdrs->x_op == XDR_DECODE) {
		stateidp->bits.boottime = mig_bt;
		stateidp->bits.type = mig_type;
		stateidp->bits.mcid = mig_mcid;
		stateidp->bits.clnodeid = mig_clnodeid;
		stateidp->bits.ident = mig_ident;
	}

	if (!xdr_int(xdrs, &stateidp->bits.pid))
		return (FALSE);
	return (TRUE);
}

static bool_t
xdr_mig_nfs_fh4(XDR *xdrs,  nfs_fh4 *fh)
{
	uint_t len, *fhsizep = &fh->nfs_fh4_len;
	char *fh4_datap, *fh4_xdatap;
	nfs_fh4_fmt_t *fhp = NULL;

	if (!xdr_u_int(xdrs, fhsizep))
		return (FALSE);

	/*
	 * filehandle is interpreted by the destination server
	 */
	if (*fhsizep != 0) {
		if (xdrs->x_op == XDR_DECODE) {
			fh->nfs_fh4_val = kmem_zalloc(*fhsizep, KM_SLEEP);
			fhp = (nfs_fh4_fmt_t *)fh->nfs_fh4_val;
		}

		if (xdrs->x_op == XDR_ENCODE) {
			fhp = (nfs_fh4_fmt_t *)fh->nfs_fh4_val;
		}

		/* fsid */
		if (!xdr_int(xdrs, &fhp->fh4_fsid.val[0]))
			return (FALSE);
		if (!xdr_int(xdrs, &fhp->fh4_fsid.val[1]))
			return (FALSE);

		/* file number length and data */
		fh4_datap = fhp->fh4_data;
		len = fhp->fh4_len;
		if (!xdr_u_int(xdrs, (uint_t *)&len))
			return (FALSE);
		if (xdrs->x_op == XDR_DECODE)
			fhp->fh4_len = len;
		if (!xdr_bytes(xdrs, (char **)&fh4_datap,
		    &len, NFS_FH4MAXDATA))
			return (FALSE);

		/* export file number length and data */
		fh4_xdatap = fhp->fh4_xdata;
		len = fhp->fh4_xlen;
		if (!xdr_u_int(xdrs, (uint_t *)&len))
			return (FALSE);
		if (xdrs->x_op == XDR_DECODE)
			fhp->fh4_xlen = len;
		if (!xdr_bytes(xdrs, &fh4_xdatap,
		    &len, NFS_FH4MAXDATA))
			return (FALSE);

		if (!xdr_u_int(xdrs, &fhp->fh4_flag))
			return (FALSE);
	}

	return (TRUE);
}

static void
xdr_rfs4_lo_state(rfs4_entry_t ent, void *arg)
{
	rfs4_lo_state_t	*ls = (rfs4_lo_state_t *)ent;
	state_mig_lock_t *rfs4_mig_lock_tab = (state_mig_lock_t *)arg;
	state_mig_t	*migs = rfs4_mig_lock_tab->sml_lo_state_tab;
	XDR		*xdrs = &(migs->sm_xdrs);
	static uint_t	record = NOT_LAST_RECORD;
	uint_t		lock_record;
	locklist_t 	*locks = NULL, *llp, *last;
	int 		lock_err = 0;
	vnode_t		*vp;
	int 		sysid, num_reclo;
	pid_t		pid;

	if (migs->sm_status == FALSE) {
		DTRACE_PROBE(nfss__e__mig_status_false);
		return;
	}

	if (xdrs->x_op == XDR_ENCODE) {
		if (rfs4_dbe_is_invalid(ls->rls_dbe)) {
			DTRACE_PROBE1(nfss__i__skip_state,
			    char *, migs->sm_tab_type);
			return;
		}

		if (ls->rls_locks_cleaned || ls->rls_lock_completed == 0) {
			DTRACE_PROBE1(nfss__i__skip_state,
			    char *, migs->sm_tab_type);
			return;
		}

		if (!xdr_u_int(xdrs, &record)) {
			goto out;
		}
	}

	/* lock_owner4 */
	if (!xdr_u_longlong_t(xdrs,
	    (u_longlong_t *)&ls->rls_locker->rl_owner.clientid))
		goto out;
	if (!xdr_bytes(xdrs, (char **)&ls->rls_locker->rl_owner.owner_val,
	    (uint_t *)&ls->rls_locker->rl_owner.owner_len,
	    NFS4_OPAQUE_LIMIT))
		goto out;

	/* open stateid */
	if (!xdr_mig_stateid(xdrs, &ls->rls_state->rs_stateid))
		goto out;

	/* lock stateid */
	if (!xdr_mig_stateid(xdrs, &ls->rls_lockid))
		goto out;

	/* seqid */
	if (!xdr_uint32_t(xdrs, &ls->rls_seqid))
		goto out;

	/* cached reply */
	if (!xdr_int(xdrs, (int *)&ls->rls_reply.resop))
		goto out;
	if (!xdr_LOCK4res(xdrs, &ls->rls_reply.nfs_resop4_u.oplock))
		goto out;

	if (xdrs->x_op == XDR_ENCODE) {
		vp = ls->rls_state->rs_finfo->rf_vp;
		sysid = ls->rls_locker->rl_client->rc_sysidt;
		pid = ls->rls_locker->rl_pid;

		/*
		 * Harvest record locks for the sysid-pid-vp combination, if
		 * they exist.  If no sysid, then no record locks exist.
		 */
		if (ls->rls_locker->rl_client->rc_sysidt != LM_NOSYSID) {
			locks = flk_active_locks_for_vp_sysid_pid(vp,
			    sysid, pid);
			lock_record = NOT_LAST_RECORD;
			num_reclo = 0;
			for (llp = locks; llp != NULL; llp = llp->ll_next) {
				ASSERT(llp->ll_vp == vp);

				/* end of lock record marker */
				if (!xdr_u_int(xdrs, &lock_record)) {
					lock_err = 1;
					goto clean;
				}

				/* lock record: type, start, length */
				if (!xdr_short(xdrs, &llp->ll_flock.l_type)) {
					lock_err = 1;
					goto clean;
				}
				if (!xdr_longlong_t(xdrs,
				    (longlong_t *)&llp->ll_flock.l_start)) {
					lock_err = 1;
					goto clean;
				}
				if (!xdr_longlong_t(xdrs,
				    (longlong_t *)&llp->ll_flock.l_len)) {
					lock_err = 1;
					goto clean;
				}
				num_reclo++;
			}
			DTRACE_PROBE2(nfss__i__num_reclo_harvested,
			    rfs4_lo_state_t *, ls, int, num_reclo);
		}

		/* all lock records done */
		lock_record = LAST_RECORD;
		if (!xdr_u_int(xdrs, &lock_record)) {
			lock_err = 1;
			goto clean;
		}
	}

	if (xdrs->x_op == XDR_DECODE) {
			/* check if there are any record locks to hydrate */
			if (!xdr_u_int(xdrs, &lock_record))
				goto out;

			while (lock_record == NOT_LAST_RECORD) {
				if (rfs4_mig_lock_tab->sml_lo_list == NULL) {
					rfs4_mig_lock_tab->sml_lo_list =
					    kmem_zalloc(sizeof (locklist_t),
					    KM_SLEEP);
					last = rfs4_mig_lock_tab->sml_lo_list;
				} else {
					last->ll_next =
					    kmem_zalloc(sizeof (locklist_t),
					    KM_SLEEP);
					last = last->ll_next;
				}

				llp = last;
				if (!xdr_short(xdrs, &llp->ll_flock.l_type))
					goto out;
				if (!xdr_longlong_t(xdrs,
				    (longlong_t *)&llp->ll_flock.l_start))
					goto out;
				if (!xdr_longlong_t(xdrs,
				    (longlong_t *)&llp->ll_flock.l_len))
					goto out;

				if (!xdr_u_int(xdrs, &lock_record))
					goto out;
			}
	}

clean:
	if ((xdrs->x_op == XDR_ENCODE) && (locks != NULL))
		flk_free_locklist(locks);
	if (lock_err == 0)
		return;
out:
	migs->sm_status = FALSE;
}

static void
xdr_rfs4_deleg_state(rfs4_entry_t ent, void *arg)
{
	uint_t		dtype;
	rfs4_deleg_state_t 	*ds = (rfs4_deleg_state_t *)ent;
	state_mig_t 	*rfs4_mig_tabs = (state_mig_t *)arg;
	state_mig_t 	*migs = &rfs4_mig_tabs[RFS4_DELEG];
	XDR 		*xdrs = &(migs->sm_xdrs);
	static uint_t	record = NOT_LAST_RECORD;

	if (migs->sm_status == FALSE) {
		DTRACE_PROBE(nfss__e__mig_status_false);
		return;
	}

	/* end-of-record marker */
	if (xdrs->x_op == XDR_ENCODE) {
		/* Harvest only active delegation state */
		if ((rfs4_dbe_is_invalid(ds->rds_dbe)) ||
		    (ds->rds_dtype == DELEG_NONE)) {
			DTRACE_PROBE1(nfss__i__skip_state,
			    char *, migs->sm_tab_type);
			return;
		}

		/* collect client state for this delegation state */
		if (!xdr_rfs4_client(ds->rds_client, rfs4_mig_tabs)) {
			goto out;
		}

		if (!xdr_u_int(xdrs, &record)) {
			goto out;
		}
	}

	/* delegation type */
	dtype = ds->rds_dtype;
	if (!xdr_u_int(xdrs, &dtype)) {
		goto out;
	}
	if (xdrs->x_op == XDR_DECODE)
		ds->rds_dtype = dtype;

	/* stateid_t */
	if (!xdr_mig_stateid(xdrs, &ds->rds_delegid)) {
		goto out;
	}

	/* nfs_fh4 */
	if (!xdr_mig_nfs_fh4(xdrs, &ds->rds_finfo->rf_filehandle)) {
		goto out;
	}

	/* clientid4 */
	if (!xdr_u_longlong_t(xdrs,
	    (u_longlong_t *)&ds->rds_client->rc_clientid)) {
		goto out;
	}

	return;
out:
	migs->sm_status = FALSE;
}

static void
xdr_rfs4_state(rfs4_entry_t ent, void *arg)
{
	rfs4_state_t 	*os = (rfs4_state_t *)ent;
	state_mig_t 	*rfs4_mig_tabs = (state_mig_t *)arg;
	state_mig_t 	*migs = &rfs4_mig_tabs[RFS4_STATE];
	XDR 		*xdrs = &(migs->sm_xdrs);
	vnode_t		*vp = os->rs_finfo->rf_vp;
	static uint_t	record = NOT_LAST_RECORD;

	if (migs->sm_status == FALSE) {
		DTRACE_PROBE(nfss__e__mig_status_false);
		return;
	}

	/*
	 * xdr_rfs4_state is invoked via the walker function. The walker
	 * routine does the locking and unlocking of the database entry. We
	 * actually hold on to the lock, because we want to get harvesting done
	 * quickly.
	 */
	if (xdrs->x_op == XDR_ENCODE) {

		/*
		 * Collect client and openowner first. Even if the openstate is
		 * not active, we may still need the client and openowner,
		 * e.g., the delegation state needs an associated client state.
		 */

		if (!xdr_rfs4_client(os->rs_owner->ro_client, rfs4_mig_tabs))
			goto out;
		if (!xdr_rfs4_openowner(os->rs_owner, rfs4_mig_tabs))
			goto out;

		/* Harvest only active openstate */
		if ((rfs4_dbe_is_invalid(os->rs_dbe)) ||
		    (os->rs_closed == TRUE)) {
			DTRACE_PROBE1(nfss__i__skip_state,
			    char *, migs->sm_tab_type);
			return;
		}
		if (!xdr_u_int(xdrs, &record))
			goto out;

		/* v_path for dtrace analytics */
		mutex_enter(&vp->v_lock);
		if (!xdr_string(xdrs, &vp->v_path, MAXPATHLEN)) {
			mutex_exit(&vp->v_lock);
			goto out;
		}
		mutex_exit(&vp->v_lock);
	}

	/*
	 * In the DECODE case, the vp is used simply as a placeholder to
	 * extract v_path, so no need to use v_lock.
	 */
	if (xdrs->x_op == XDR_DECODE) {
		if (!xdr_string(xdrs, &vp->v_path, MAXPATHLEN))
			goto out;
	}

	/* stateid is handled in a special manner for migration */
	if (!xdr_mig_stateid(xdrs, &os->rs_stateid))
		goto out;

	/* open_owner4 */
	if (!xdr_u_longlong_t(xdrs,
	    (u_longlong_t *)&os->rs_owner->ro_owner.clientid))
		goto out;
	if (!xdr_bytes(xdrs, (char **)&os->rs_owner->ro_owner.owner_val,
	    (uint_t *)&os->rs_owner->ro_owner.owner_len,
	    NFS4_OPAQUE_LIMIT))
		goto out;

	if (!xdr_mig_nfs_fh4(xdrs, &os->rs_finfo->rf_filehandle))
		goto out;

	/* share lock and open mode bits */
	if (!xdr_u_int(xdrs, &os->rs_open_access))
		goto out;
	if (!xdr_u_int(xdrs, &os->rs_open_deny))
		goto out;
	if (!xdr_u_int(xdrs, &os->rs_share_access))
		goto out;
	if (!xdr_u_int(xdrs, &os->rs_share_deny))
		goto out;

	return;
out:
	migs->sm_status = FALSE;
}

static bool_t
xdr_rfs4_openowner(rfs4_openowner_t *oo, state_mig_t *rfs4_mig_tabs)
{
	state_mig_t 	*migs = &rfs4_mig_tabs[RFS4_OOWNER];
	XDR 		*xdrs = &(migs->sm_xdrs);
	bool_t		xdr_success = TRUE;
	unsigned int	need_confirm;
	unsigned int	postpone_confirm;
	static uint_t	record = NOT_LAST_RECORD;
	avl_tree_t	*uniq_avl;
	uniq_node_t	uniq_node;
	int		found;

	if (migs->sm_status == FALSE) {
		DTRACE_PROBE(nfss__e__mig_status_false);
		return (FALSE);
	}

	if (xdrs->x_op == XDR_ENCODE) {

		/* Skip harvesting invalid dbe entry */
		rfs4_dbe_lock(oo->ro_dbe);
		if (rfs4_dbe_is_invalid(oo->ro_dbe)) {
			DTRACE_PROBE1(nfss__i__skip_state,
			    char *, migs->sm_tab_type);
			goto out;
		}

		/* Check if the openowner has already been harvested */
		uniq_avl = &rfs4_mig_tabs[RFS4_OOWNER].sm_uniq_node_tree;
		uniq_node.un_key  = (void *)&oo->ro_owner;
		found = mig_dup_check(uniq_avl, &uniq_node, RFS4_OOWNER);
		if (found == -1) {
			xdr_success = FALSE;
			goto out;
		} else if (found == 1)
			goto out;

		if (!xdr_u_int(xdrs, &record)) {
			xdr_success = FALSE;
			goto out;
		}
	}

	/* open_owner4 */
	if (!xdr_u_longlong_t(xdrs,
	    (u_longlong_t *)&oo->ro_owner.clientid)) {
		xdr_success = FALSE;
		goto out;
	}
	if (!xdr_bytes(xdrs, (char **)&oo->ro_owner.owner_val,
	    (uint_t *)&oo->ro_owner.owner_len,
	    NFS4_OPAQUE_LIMIT)) {
		xdr_success = FALSE;
		goto out;
	}

	need_confirm = oo->ro_need_confirm;
	if (!xdr_u_int(xdrs, &need_confirm)) {
		xdr_success = FALSE;
		goto out;
	}

	postpone_confirm = oo->ro_postpone_confirm;
	if (!xdr_u_int(xdrs, &postpone_confirm)) {
		xdr_success = FALSE;
		goto out;
	}

	if (!xdr_u_int(xdrs, &oo->ro_open_seqid)) {
		xdr_success = FALSE;
		goto out;
	}
	if (!xdr_mig_nfs_fh4(xdrs, &oo->ro_reply_fh)) {
		xdr_success = FALSE;
		goto out;
	}
	if (!xdr_int(xdrs, (int *)&oo->ro_reply.resop)) {
		xdr_success = FALSE;
		goto out;
	}
	if (!xdr_OPEN4res(xdrs, &oo->ro_reply.nfs_resop4_u.opopen)) {
		xdr_success = FALSE;
		goto out;
	}
out:
	if (xdrs->x_op == XDR_ENCODE)
		rfs4_dbe_unlock(oo->ro_dbe);
	migs->sm_status = xdr_success;
	return (xdr_success);
}

static bool_t
xdr_rfs4_client(rfs4_client_t *cl, state_mig_t *rfs4_mig_tabs)
{
	state_mig_t		*migs = &rfs4_mig_tabs[RFS4_CLIENT];
	XDR 			*xdrs = &(migs->sm_xdrs);
	struct sockaddr 	*ca;
	cb_client4 		*cbp;
	bool_t			xdr_success = TRUE;
	uint_t			record = NOT_LAST_RECORD;
	uint_t			*uip;
	avl_tree_t		*uniq_avl;
	uniq_node_t		uniq_node;
	int			found = 0;
	rfs4_lemo_entry_t	*lp = NULL;

	if (migs->sm_status == FALSE) {
		DTRACE_PROBE(nfss__e__mig_status_false);
		return (FALSE);
	}

	if (xdrs->x_op == XDR_ENCODE) {

		/*
		 * Skip harvesting invalid dbe entry and unconfirmed client
		 * state.
		 */
		rfs4_dbe_lock(cl->rc_dbe);
		if ((rfs4_dbe_is_invalid(cl->rc_dbe)) ||
		    (cl->rc_need_confirm)) {
			DTRACE_PROBE1(nfss__i__skip_state,
			    char *, migs->sm_tab_type);
			goto out;
		}

		/* Check if the client has already been harvested */
		uniq_avl = &rfs4_mig_tabs[RFS4_CLIENT].sm_uniq_node_tree;
		uniq_node.un_key  = (void *)&cl->rc_clientid;
		found = mig_dup_check(uniq_avl, &uniq_node, RFS4_CLIENT);
		if (found == -1) {
			xdr_success = FALSE;
			goto out;
		} else if (found == 1)
			goto out;

		/* add the fsid to the lease moved list */
		lp = rfs4_lemo_insert(cl,
		    rfs4_mig_tabs->sm_vp->v_vfsp->vfs_fsid);

		if (!xdr_u_int(xdrs, &record)) {
			xdr_success = FALSE;
			goto out;
		}
	}

	/* clientid4 */
	if (!xdr_u_longlong_t(xdrs,
	    (u_longlong_t *)&cl->rc_clientid)) {
		xdr_success = FALSE;
		goto out;
	}

	/* nfs_client_id4 */
	if (!xdr_u_longlong_t(xdrs,
	    (u_longlong_t *)&cl->rc_nfs_client.verifier)) {
		xdr_success = FALSE;
		goto out;
	}
	if (!xdr_bytes(xdrs, (char **)&cl->rc_nfs_client.id_val,
	    (uint_t *)&cl->rc_nfs_client.id_len, NFS4_OPAQUE_LIMIT)) {
		xdr_success = FALSE;
		goto out;
	}

	ca = (struct sockaddr *)&cl->rc_addr;
	if (xdrs->x_op == XDR_DECODE)
		bzero(ca, sizeof (struct sockaddr_storage));

	if (!xdr_short(xdrs, (short *)&ca->sa_family)) {
		xdr_success = FALSE;
		goto out;
	}
	if (!xdr_short(xdrs, (short *)&((struct sockaddr_in *)ca)->sin_port)) {
		xdr_success = FALSE;
		goto out;
	}
	if (ca->sa_family == AF_INET) {
		/* IPv4 */
		if (!xdr_u_int(xdrs,
		    (uint_t *)&((struct sockaddr_in *)ca)->sin_addr)) {
			xdr_success = FALSE;
			goto out;
		}
	} else {
		/* IPv6 */
		uip = (uint_t *)&((struct sockaddr_in6 *)ca)->sin6_addr;
		if (!xdr_u_int(xdrs, &uip[0])) {
			xdr_success = FALSE;
			goto out;
		}
		if (!xdr_u_int(xdrs, &uip[1])) {
			xdr_success = FALSE;
			goto out;
		}
		if (!xdr_u_int(xdrs, &uip[2])) {
			xdr_success = FALSE;
			goto out;
		}
		if (!xdr_u_int(xdrs, &uip[3])) {
			xdr_success = FALSE;
			goto out;
		}
	}

	/* verifier4 */
	if (!xdr_u_longlong_t(xdrs,
	    (u_longlong_t *)&cl->rc_confirm_verf)) {
		xdr_success = FALSE;
		goto out;
	}

	/* cb_client4 */
	cbp = &cl->rc_cbinfo.cb_callback;
	if (!xdr_u_int(xdrs, &cbp->cb_program)) {
		xdr_success = FALSE;
		goto out;
	}
	if (!xdr_string(xdrs, &cbp->cb_location.r_netid,
	    NFS4_OPAQUE_LIMIT)) {
		xdr_success = FALSE;
		goto out;
	}
	if (!xdr_string(xdrs, &cbp->cb_location.r_addr,
	    NFS4_OPAQUE_LIMIT)) {
		xdr_success = FALSE;
		goto out;
	}

out:
	if (xdr_success == FALSE && lp != NULL)
		rfs4_lemo_remove(cl, lp);
	if (xdrs->x_op == XDR_ENCODE) {
		rfs4_dbe_unlock(cl->rc_dbe);
	}
	migs->sm_status = xdr_success;
	return (xdr_success);
}

static bool_t
xdr_verifier(XDR *xdrs, time_t *timep)
{
#if defined(_ILP32)
	if (!xdr_int(xdrs, (int32_t *)timep))
		return (FALSE);
#else
	if (!xdr_longlong_t(xdrs, (longlong_t *)timep))
		return (FALSE);
#endif

	return (TRUE);
}

static bool_t
xdr_rfs4_globals(rfs4_inst_t *vip, state_mig_t *rfs4_mig_tabs,
    rfs4_mig_globals_t *mig_globals)
{
	state_mig_t		*migs = &rfs4_mig_tabs[RFS4_GLOBAL];
	XDR 			*xdrs = &(migs->sm_xdrs);
	bool_t			xdr_success = TRUE;
	mc_verifier_node_t	*mc_nodep = NULL;
	verifier_node_t		*verfp = NULL;
	uint_t			verf_record = NOT_LAST_RECORD;
	uint_t			nodeid_record = NOT_LAST_RECORD;
	time_t			id_verifier;
	uint32_t		mc_id;

	if (migs->sm_status == FALSE) {
		DTRACE_PROBE(nfss__e__mig_status_false);
		return (FALSE);
	}

	/* write verifier */
	if (!xdr_longlong_t(xdrs,
	    (longlong_t *)&(mig_globals->mig_write4verf))) {
		xdr_success = FALSE;
		goto out;
	}

	/* stateid verifier */
	if (!xdr_verifier(xdrs, &mig_globals->mig_stateid_verifier)) {
		xdr_success = FALSE;
		goto out;
	}

	/* clientid verifier tree */
	if (xdrs->x_op == XDR_ENCODE) {
		mutex_enter(&vip->r4_mc_verftree_lock);
		for (mc_nodep = avl_first(&vip->r4_mc_verifier_tree);
		    mc_nodep != NULL;
		    mc_nodep = AVL_NEXT(&vip->r4_mc_verifier_tree, mc_nodep)) {
			if (!xdr_u_int(xdrs, &nodeid_record)) {
					xdr_success = FALSE;
					goto out;
			}
			if (!xdr_uint32_t(xdrs, &mc_nodep->mcv_nodeid)) {
				xdr_success = FALSE;
				goto out;
			}

			verf_record = NOT_LAST_RECORD;
			for (verfp = list_head(&mc_nodep->mcv_list);
			    verfp != NULL;
			    verfp = list_next(&mc_nodep->mcv_list, verfp)) {
				if (!xdr_u_int(xdrs, &verf_record)) {
					xdr_success = FALSE;
					goto out;
				}
				if (!xdr_verifier(xdrs, &verfp->vl_verifier)) {
					xdr_success = FALSE;
					goto out;
				}
			}

			verf_record = LAST_RECORD;
			if (!xdr_u_int(xdrs, &verf_record)) {
				xdr_success = FALSE;
				goto out;
			}
		}
		mutex_exit(&vip->r4_mc_verftree_lock);

		nodeid_record = LAST_RECORD;
		if (!xdr_u_int(xdrs, &nodeid_record)) {
			xdr_success = FALSE;
			goto out;
		}
	}

	if (xdrs->x_op == XDR_DECODE) {
		if (!xdr_u_int(xdrs, &nodeid_record)) {
			xdr_success = FALSE;
			goto out;
		}
		while (nodeid_record != LAST_RECORD) {
			if (!xdr_uint32_t(xdrs, &mc_id)) {
				xdr_success = FALSE;
				goto out;
			}
			if (!xdr_u_int(xdrs, &verf_record)) {
				xdr_success = FALSE;
				goto out;
			}

			while (verf_record != LAST_RECORD) {
				if (!xdr_verifier(xdrs, &id_verifier)) {
					xdr_success = FALSE;
					goto out;
				}

				/*
				 * The XDR routine hydrates the metacluster
				 * verifier array inline.
				 */
				mutex_enter(&vip->r4_mc_verftree_lock);
				if (!mc_verf_exists(vip, mc_id, id_verifier,
				    &mc_nodep)) {
					mc_insert_verf(vip, mc_id, id_verifier,
					    mc_nodep);
				}
				mutex_exit(&vip->r4_mc_verftree_lock);

				if (!xdr_u_int(xdrs, &verf_record)) {
					xdr_success = FALSE;
					goto out;
				}
			}

			if (!xdr_u_int(xdrs, &nodeid_record)) {
				xdr_success = FALSE;
				goto out;
			}
		}
	}

out:
	if (xdrs->x_op == XDR_ENCODE &&
	    xdr_success == FALSE &&
	    mc_nodep != NULL) {
		mutex_exit(&vip->r4_mc_verftree_lock);
	}
	migs->sm_status = xdr_success;
	return (xdr_success);
}
