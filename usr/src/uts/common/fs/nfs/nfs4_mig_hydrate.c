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
#include <nfs/export.h>
#include <sys/sdt.h>
#include <sys/cmn_err.h>
#include <sys/kmem.h>
#include <nfs/nfs_srv_inst_impl.h>

/*
 * structures contain migrated state needed for hydrate
 */
typedef struct rfs4_mig_client {
	clientid4	rc_mig_clientid;
	nfs_client_id4	rc_mig_nfs_client;
	struct sockaddr_storage rc_mig_addr;
	verifier4	rc_mig_confirm_verf;
	cb_client4	rc_mig_cb_client;
} rfs4_mig_client_t;

typedef struct rfs4_mig_openowner {
	open_owner4	ro_mig_owner;
	seqid4		ro_mig_open_seqid;
	uint32_t	ro_mig_need_confirm;
	uint32_t	ro_mig_postpone_confirm;
	nfs_fh4		ro_mig_reply_fh;
	nfs_resop4	ro_mig_reply;
} rfs4_mig_openowner_t;

typedef struct rfs4_mig_state {
	stateid_t	rs_mig_stateid;
	open_owner4	rs_mig_owner;
	nfs_fh4		rs_mig_filehandle;
	uint32_t	rs_mig_open_access;
	uint32_t	rs_mig_open_deny;
	uint32_t	rs_mig_share_access;
	uint32_t	rs_mig_share_deny;
	char 		*v_path;
} rfs4_mig_state_t;

typedef struct rfs4_mig_deleg_state {
	open_delegation_type4	rds_mig_dtype;
	stateid_t	rds_mig_delegid;
	nfs_fh4		rds_mig_filehandle;
	clientid4	rds_mig_clientid;
} rfs4_mig_deleg_state_t;

typedef struct rfs4_mig_lo_state {
	/* embedded lock owner state */
	lock_owner4	rls_mig_owner;

	nfs_fh4		rls_mig_filehandle;
	stateid_t	rls_mig_open_stateid;
	stateid_t	rls_mig_lockid;
	seqid4		rls_mig_seqid;
	nfs_resop4	rls_mig_reply;
	locklist_t	*rls_mig_lo_list;
} rfs4_mig_lo_state_t;

static int
rfs4_hydrate_mig_client(rfs_inst_t *rip, rfs4_mig_client_t *mcp)
{
	bool_t create;
	rfs4_client_t *cp = NULL, *cp_sf = NULL;
	rfs4_inst_t *vip = &rip->ri_v4;

	/* setup client's IP in the long-form clientID */
	mcp->rc_mig_nfs_client.cl_addr = (struct sockaddr *)&mcp->rc_mig_addr;

	/* specify clientid to use when creating rfs4_client_t */
	mcp->rc_mig_nfs_client.mig_clientid = mcp->rc_mig_clientid;

	create = TRUE;
	cp = rfs4_findclient(rip, &mcp->rc_mig_nfs_client, &create, NULL);

	/* should never happen */
	ASSERT(cp != NULL);
	if (cp == NULL) {
		DTRACE_PROBE1(nfss__e__hdclnt_db, rfs4_mig_client_t *, mcp);
		return (ENOENT);
	}

	if (create == FALSE) {
		rfs4_client_rele(cp);

		/*
		 * Before concluding that nfs_client_id4 already exists, first
		 * make sure that the migrated short form can be accessed using
		 * the short form index.
		 *
		 * We need to sychronize the check and creation of the new
		 * client entry to avoid duplicate entries in the client table
		 * as a result of simultaneous file system migrations that have
		 * the same short form and long form (same client mounts the
		 * migrated file systems on the source).
		 */
		rw_enter(&vip->r4_findclient_lock, RW_WRITER);
		create = FALSE;
		cp_sf = rfs4_findclient_db(vip, vip->r4_clientid_idx,
		    &mcp->rc_mig_clientid, &create, NULL, FALSE);

		if (cp_sf != NULL) {

			/*
			 * nfs_client_id4 already existed in the system.
			 *
			 * this can happen if a client mounts 2 FSes from
			 * server A and both FSes migrate to server B. Or a FS
			 * migrates back to its previous server.
			 */
			rw_exit(&vip->r4_findclient_lock);
			DTRACE_PROBE1(nfss__e__hdclnt_exist,
			    rfs4_mig_client_t *, mcp);

			rfs4_update_lease(cp_sf);
			rfs4_client_rele(cp_sf);

			return (0);
		} else {

			/*
			 * Create a new entry since the short form of the
			 * migrated entry is different.
			 */
			mcp->rc_mig_nfs_client.mig_create = 1;
			create = TRUE;
			cp = rfs4_findclient_db(vip, vip->r4_nfsclnt_idx,
			    &mcp->rc_mig_nfs_client, &create,
			    (void *)&mcp->rc_mig_nfs_client, TRUE);
			rw_exit(&vip->r4_findclient_lock);

			ASSERT(create == TRUE);
			if (cp == NULL) {
				DTRACE_PROBE1(nfss__e__hdcreate_failed,
				    rfs4_mig_client_t *, mcp);
				return (ENOENT);
			}
		}
	}

	rfs4_dbe_lock(cp->rc_dbe);

	/*
	 * new rfs4_client_t created with a new lease, update
	 * client state with migrated confirm verifier.
	 */
	cp->rc_confirm_verf = mcp->rc_mig_confirm_verf;
	cp->rc_need_confirm = 0; /* We never migrate unconfirmed clients */

	rfs4_ss_clid(cp);		/* Record clientid in stable storage */

	/*
	 * cb_state should be CB_UNINIT until client contacts
	 * server to setup new callback.
	 */
	cp->rc_cbinfo.cb_state = CB_UNINIT;	/* don't use callback yet */
	cp->rc_cbinfo.cb_callback.cb_location.r_addr = NULL;
	cp->rc_cbinfo.cb_callback.cb_location.r_addr = NULL;
	rfs4_dbe_unlock(cp->rc_dbe);

	/*
	 * Clear cached CLIENT handles, server will recreate on next calback.
	 */
	cp->rc_cbinfo.cb_chc_free = 0;
	bzero((char *)&cp->rc_cbinfo.cb_chc[0], sizeof (cp->rc_cbinfo.cb_chc));

	rfs4_client_rele(cp);

	return (0);
}

static int
rfs4_hydrate_mig_openowner(rfs_inst_t *rip, rfs4_mig_openowner_t *hop)
{
	bool_t create;
	rfs4_openowner_t *op;

	create = TRUE;
	op = rfs4_findopenowner(rip, &hop->ro_mig_owner, &create,
	    hop->ro_mig_open_seqid);

	if (op == NULL) {
		cmn_err(CE_WARN, "rfs4_hydrate_mig_openowner: Create failed\n");
		DTRACE_PROBE1(nfss__e__hdopenowner, rfs4_mig_openowner_t *,
		    hop);
		return (ENOENT);
	}

	if (create == FALSE) {
		/* state already existed, keep it the same */
		DTRACE_PROBE1(nfss__i__ooexist, rfs4_mig_openowner_t *, hop);
		rfs4_openowner_rele(op);
		return (0);
	}

	/* hydrate unconfirmed openowner state */
	op->ro_need_confirm = hop->ro_mig_need_confirm;
	op->ro_postpone_confirm = hop->ro_mig_postpone_confirm;

	/* hydrate the ro_reply data */
	op->ro_reply_fh.nfs_fh4_len = hop->ro_mig_reply_fh.nfs_fh4_len;
	op->ro_reply_fh.nfs_fh4_val =
	    kmem_alloc(op->ro_reply_fh.nfs_fh4_len, KM_SLEEP);

	rfs4_dbe_lock(op->ro_dbe);
	nfs_fh4_copy(&hop->ro_mig_reply_fh, &op->ro_reply_fh);
	rfs4_copy_reply(&op->ro_reply, &hop->ro_mig_reply);
	rfs4_dbe_unlock(op->ro_dbe);

	rfs4_update_lease(op->ro_client);
	rfs4_openowner_rele(op);

	return (0);
}

static vnode_t *
rfs4_fh4tovnode(vnode_t *rootvp, nfs_fh4 *fh4)
{
	int err;
	nfs_fh4_fmt_t *fh4_fmtp;
	fid_t *fidp;
	vnode_t *vp;

	if (fh4 == NULL || rootvp == NULL || rootvp->v_vfsp == NULL)
		return (NULL);

	fh4_fmtp = fh4_to_fmt4(fh4);
	fidp = (fid_t *)&fh4_fmtp->fh4_len;

	if ((err = VFS_VGET(rootvp->v_vfsp, &vp, fidp))) {
		DTRACE_PROBE2(nfss__e__no_vnode, fid_t *, fidp,
		    int, err);
		return (NULL);
	}

	return (vp);
}

static int
rfs4_hydrate_mig_state(rfs_inst_t *rip, rfs4_mig_state_t *hsp, vnode_t *rootvp)
{
	int err, fflags;
	bool_t create;
	rfs4_openowner_t *oo;
	rfs4_file_t *fp;
	rfs4_state_t *sp, key;
	nfsstat4 stat;
	sysid_t sysid;
	vnode_t *vp;
	caller_context_t ct;
	fsh_entry_t *fse;

	/* find vnode of this open state */
	if ((vp = rfs4_fh4tovnode(rootvp, &hsp->rs_mig_filehandle)) == NULL)
		return (EINVAL);

	vn_setpath_str(vp, hsp->v_path, strlen(hsp->v_path));

	fse = fsh_get_ent(rip, rootvp->v_vfsp->vfs_fsid);
	ASSERT(fse != NULL);

	/* find rfs4_file_t of this open state */
	create = TRUE;
	fp = rfs4_findfile(fse, vp, &hsp->rs_mig_filehandle, &create);
	VN_RELE(vp);		/* release hold by nfs4_fhtovp() */

	if (fp == NULL) {
		DTRACE_PROBE1(nfss__e__no_file, rfs4_mig_state_t *, hsp);
		cmn_err(CE_WARN, "rfs4_hydrate_mig_state: NULL fp");
		fsh_ent_rele(rip, fse);
		return (ENOENT);
	}

	/* find rfs4_openowner_t of this open state */
	create = FALSE;
	if ((oo = rfs4_findopenowner(rip, &hsp->rs_mig_owner, &create, 0))
	    == NULL) {
		DTRACE_PROBE1(nfss__e__no_oo, rfs4_mig_state_t *, hsp);
		cmn_err(CE_WARN, "rfs4_hydrate_mig_state: NULL owner");
		rfs4_file_rele(fp);	/* release hold by rfs4_findfile() */
		fsh_ent_rele(rip, fse);
		return (ENOENT);
	}

	/* create rfs4_state_t from migrated state */
	key.rs_owner = oo;
	key.rs_finfo = fp;
	key.rs_stateid = hsp->rs_mig_stateid;	/* doing migration */
	create = TRUE;
	sp = (rfs4_state_t *)rfs4_dbsearch(fse->fse_state_owner_file_idx, &key,
	    &create, &key, RFS4_DBS_VALID);

	fsh_ent_rele(rip, fse);
	rfs4_file_rele(fp);		/* release hold by rfs4_findfile */
	rfs4_openowner_rele(oo);	/* release hold by rfs4_findopenowner */

	if (sp == NULL) {
		DTRACE_PROBE1(nfss__e__no_os, rfs4_mig_state_t *, hsp);
		cmn_err(CE_WARN,
		    "rfs4_hydrate_mig_state: create open state failed.");

		return (ENOENT);
	}

	if (create == FALSE) {
		DTRACE_PROBE1(nfss__e__os_exist, rfs4_mig_state_t *, hsp);
		cmn_err(CE_WARN, "rfs4_hydrate_mig_state: state existed.");
		err = EEXIST;

		/* open state is closed and invalidated below */
		goto errout;
	}

	/* try to get the sysid before continuing */
	if ((stat = rfs4_client_sysid(oo->ro_client, &sysid)) != NFS4_OK) {
		DTRACE_PROBE1(nfss__e__sysid, rfs4_mig_state_t *, hsp);
		cmn_err(CE_WARN,
		    "rfs4_hydrate_mig_state: rfs4_client_sysid %d", stat);
		err = ESRCH;
		goto errout;
	}

	rfs4_dbe_lock(sp->rs_dbe);

	/* create shrlock on this file */
	if (hsp->rs_mig_share_access || hsp->rs_mig_share_deny) {
		if ((err = rfs4_share(sp, hsp->rs_mig_share_access,
		    hsp->rs_mig_share_deny)) != 0) {
			DTRACE_PROBE1(nfss__e__shrlock, rfs4_mig_state_t *,
			    hsp);
			cmn_err(CE_WARN,
			    "rfs4_hydrate_mig_state: rfs4_share %d", err);

			rfs4_dbe_unlock(sp->rs_dbe);
			err = ENOLCK;
			goto errout;
		}
	}

	/* do VOP_OPEN on this vnode */
	fflags = 0;
	if (hsp->rs_mig_open_access & OPEN4_SHARE_ACCESS_READ)
		fflags |= FREAD;
	if (hsp->rs_mig_open_access & OPEN4_SHARE_ACCESS_WRITE)
		fflags |= FWRITE;

	ct.cc_sysid = sysid;
	ct.cc_pid = rfs4_dbe_getid(sp->rs_owner->ro_dbe);
	ct.cc_caller_id = rfs.rg_v4.rg4_caller_id;
	ct.cc_flags = CC_DONTBLOCK;

	/*
	 * the file was opened successfully on the src server meant
	 * the client's credential must be good. We can use kcred
	 * for this open.
	 */
	if ((err = VOP_OPEN(&vp, fflags, kcred, &ct))) {
		DTRACE_PROBE2(nfss__e__vopopen, rfs4_mig_state_t *, hsp,
		    vnode_t *, vp);
		cmn_err(CE_WARN, "rfs4_hydrate_mig_state: VOP_OPEN error %d",
		    err);

		rfs4_dbe_unlock(sp->rs_dbe);
		err = ENOENT;
		goto errout;
	}

	/* hydrate rs_open_access and rs_open_deny from migrated state */
	sp->rs_open_access = hsp->rs_mig_open_access;
	sp->rs_open_deny = hsp->rs_mig_open_deny;

	if (sp->rs_open_deny & OPEN4_SHARE_DENY_READ)
		fp->rf_deny_read++;
	if (sp->rs_open_deny & OPEN4_SHARE_DENY_WRITE)
		fp->rf_deny_write++;
	fp->rf_share_deny |= sp->rs_open_deny;

	if (sp->rs_open_access & OPEN4_SHARE_ACCESS_READ)
		fp->rf_access_read++;
	if (sp->rs_open_access & OPEN4_SHARE_ACCESS_WRITE)
		fp->rf_access_write++;
	fp->rf_share_access |= sp->rs_open_access;

	rfs4_update_lease(sp->rs_owner->ro_client);
	rfs4_dbe_unlock(sp->rs_dbe);
	rfs4_state_rele_nounlock(sp);

	return (0);

errout:
	sp->rs_closed = TRUE;
	rfs4_dbe_invalidate(sp->rs_dbe);
	rfs4_state_rele_nounlock(sp);

	return (err);
}

static int
rfs4_hydrate_mig_deleg_state(rfs_inst_t *rip, rfs4_mig_deleg_state_t *hdp,
    vnode_t *rootvp)
{
	bool_t create;
	int ret;
	vnode_t *vp;
	rfs4_client_t *cp;
	rfs4_file_t *fp;
	rfs4_deleg_state_t ds, *dsp;
	fsh_entry_t *fse;

	if ((vp = rfs4_fh4tovnode(rootvp, &hdp->rds_mig_filehandle)) == NULL)
		return (EINVAL);

	if ((cp = rfs4_findclient_by_id(rip, hdp->rds_mig_clientid, FALSE)) ==
	    NULL) {
		DTRACE_PROBE1(nfss__e__no_client, rfs4_mig_deleg_state_t *,
		    hdp);
		cmn_err(CE_WARN,
		    "rfs4_rehydrate_deleg_state: can't find clientID %"
		    PRIu64"x", hdp->rds_mig_clientid);

		VN_RELE(vp);		/* release hold by nfs4_fhtovp() */
		return (ENOENT);
	}

	fse = fsh_get_ent(rip, rootvp->v_vfsp->vfs_fsid);
	ASSERT(fse != NULL);

	/* find rfs4_file_t of this open state, create it if not existed */
	create = TRUE;
	fp = rfs4_findfile(fse, vp, &hdp->rds_mig_filehandle, &create);
	VN_RELE(vp);		/* release hold by nfs4_fhtovp() */

	if (fp == NULL) {
		DTRACE_PROBE1(nfss__e__no_file, rfs4_mig_deleg_state_t *,
		    hdp);
		cmn_err(CE_WARN, "rfs4_rehydrate_deleg_state: NULL fp");

		rfs4_client_rele(cp);
		fsh_ent_rele(rip, fse);
		return (ENOENT);
	}

	/* create rfs4_deleg_state_t */
	ds.rds_client = cp;
	ds.rds_finfo = fp;
	ds.rds_delegid = hdp->rds_mig_delegid;

	create = TRUE;
	dsp = (rfs4_deleg_state_t *)rfs4_dbsearch(fse->fse_deleg_idx, &ds,
	    &create, &ds, RFS4_DBS_VALID);

	fsh_ent_rele(rip, fse);
	rfs4_file_rele(fp);
	rfs4_client_rele(cp);

	if (dsp == NULL) {
		DTRACE_PROBE1(nfss__e__no_ds, rfs4_mig_state_t *, hdp);
		cmn_err(CE_WARN, "rfs4_rehydrate_deleg_state: NULL dsp");

		return (ENOENT);
	}

	if (create == FALSE) {
		DTRACE_PROBE1(nfss__e__ds_exist, rfs4_mig_deleg_state_t *,
		    hdp);
		cmn_err(CE_WARN, "rfs4_rehydrate_deleg_state: dsp existed");

		rfs4_deleg_state_rele(dsp);
		return (EEXIST);
	}

	/* installing FEM */
	ASSERT(hdp->rds_mig_dtype != OPEN_DELEGATE_NONE);
	if (hdp->rds_mig_dtype == OPEN_DELEGATE_READ) {
		ret = fem_install(vp, rfs.rg_v4.rg4_deleg_rdops, (void *)fp,
		    OPUNIQ, rfs4_mon_hold, rfs4_mon_rele);
		/*
		 * Because a client can hold onto a delegation after the
		 * file has been closed, we need to keep track of the
		 * access to this file.  Otherwise the CIFS server would
		 * not know about the client accessing the file and could
		 * inappropriately grant an OPLOCK.
		 *
		 * fem_install() returns EBUSY when asked to install a
		 * OPUNIQ monitor more than once.  Therefore, check the
		 * return code because we only want this done once.
		 */
		if (ret == 0)
			vn_open_upgrade(vp, FREAD);
		else if (ret != EBUSY) {
			DTRACE_PROBE2(nfss__e__femrd, vnode_t *, vp,
			    rfs4_file_t *, fp);
			cmn_err(CE_WARN,
			    "rfs4_rehydrate_deleg_state: fem_install err %d",
			    ret);

			rfs4_deleg_state_rele(dsp);
			return (ret);
		}
	} else {
		ret = fem_install(vp, rfs.rg_v4.rg4_deleg_wrops, (void *)fp,
		    OPUNIQ, rfs4_mon_hold, rfs4_mon_rele);
		/*
		 * Because a client can hold onto a delegation after the
		 * file has been closed, we need to keep track of the
		 * access to this file.  Otherwise the CIFS server would
		 * not know about the client accessing the file and could
		 * inappropriately grant an OPLOCK.
		 *
		 * fem_install() returns EBUSY when asked to install a
		 * OPUNIQ monitor more than once.  For WRITE delegation
		 * we don't expect it's granted more than once for the
		 * same file so it's treated as an error condition.
		 */
		if (ret == 0)
			vn_open_upgrade(vp, FREAD|FWRITE);
		else {
			DTRACE_PROBE2(nfss__e__femwr, vnode_t *, vp,
			    rfs4_file_t *, fp);
			cmn_err(CE_WARN,
			    "rfs4_rehydrate_deleg_state: fem_install err %d",
			    ret);

			rfs4_deleg_state_rele(dsp);
			return (ret);
		}
	}

	/* Place on delegation list for file */
	ASSERT(!list_link_active(&dsp->rds_node));
	list_insert_tail(&fp->rf_delegstatelist, dsp);

	dsp->rds_dtype = fp->rf_dinfo.rd_dtype = hdp->rds_mig_dtype;

	/* extend the expiration of delegation */
	fp->rf_dinfo.rd_time_lastwrite = gethrestime_sec();

	/* Update delegation stats for this file */
	fp->rf_dinfo.rd_time_lastgrant = gethrestime_sec();

	fp->rf_dinfo.rd_conflicted_client = 0;
	fp->rf_dinfo.rd_ever_recalled = FALSE;

	if (dsp->rds_dtype == OPEN_DELEGATE_READ)
		fp->rf_dinfo.rd_rdgrants++;
	else
		fp->rf_dinfo.rd_wrgrants++;

	rfs4_deleg_state_rele(dsp);

	return (0);
}

/*
 * Release all locks on the vnode, specified by the
 * open state, for this client.
 */
static void
rfs4_clean_locks(rfs4_state_t *sp)
{
	struct flock64 flk;

	flk.l_type = F_UNLKSYS;
	flk.l_whence = 0;
	flk.l_start = 0;
	flk.l_len = 0;
	flk.l_sysid = sp->rs_owner->ro_client->rc_sysidt;
	flk.l_pid = 0;
	(void) VOP_FRLOCK(sp->rs_finfo->rf_vp, F_SETLK,
	    &flk, F_REMOTELOCK | FREAD | FWRITE,
	    (u_offset_t)0, NULL, kcred, NULL);
}

static int
rfs4_hydrate_mig_lo_state(rfs_inst_t *rip, rfs4_mig_lo_state_t *hlp,
    vnode_t *rootvp)
{
	int flag;
	int err = 0;
	bool_t create;
	flock64_t flock;
	rfs4_lockowner_t *lo;
	rfs4_state_t *sp;
	rfs4_lo_state_t *lsp, arg;
	fsh_entry_t *fse;
	locklist_t *locks;

	if (rootvp == NULL || rootvp->v_vfsp == NULL)
		return (EINVAL);

	fse = fsh_get_ent(rip, rootvp->v_vfsp->vfs_fsid);
	ASSERT(fse != NULL);

	/*
	 * create lock owner if not already exist. The rl_pid is used only
	 * internally in the server to locate and identify locks. It's ok to
	 * use the newly assigned rl_pid instead of having to migrate this pid.
	 *
	 * Note that the rl_pid is embedded in the stateid_t of the rls_lockid
	 * state token, which was returned to the client. The server uses this
	 * token to locate the lock state, rfs4_lo_state_t, associated with the
	 * lock request. From the rfs4_lo_state_t, server locates the lock owner
	 * and uses its lo->rl_pid to locate its locks.
	 */
	create = TRUE;
	if ((lo = rfs4_findlockowner(rip, &hlp->rls_mig_owner, &create))
	    == NULL) {
		DTRACE_PROBE1(nfss__e__no_lockowner, rfs4_mig_lo_state_t *,
		    hlp);
		cmn_err(CE_WARN, "rfs4_hydrate_mig_lo_state: lockowner NULL");

		fsh_ent_rele(rip, fse);
		return (ENOENT);
	}

	/* locate the open state for this lock */
	create = FALSE;
	sp = (rfs4_state_t *)rfs4_dbsearch(fse->fse_state_idx,
	    &hlp->rls_mig_open_stateid, &create, NULL, RFS4_DBS_VALID);
	if (sp == NULL) {
		DTRACE_PROBE1(nfss__e__no_openstate, rfs4_mig_lo_state_t *,
		    hlp);
		cmn_err(CE_WARN,
		    "rfs4_hydrate_mig_lo_state: can't find open state");

		rfs4_lockowner_rele(lo);
		fsh_ent_rele(rip, fse);
		return (ENOENT);
	}

	/*
	 * create rfs4_lo_state_t, using migrated state tokens,
	 * for this lock owner.
	 */
	create = TRUE;
	arg.rls_locker = lo;
	arg.rls_state = sp;
	arg.rls_lockid = hlp->rls_mig_lockid;
	lsp = (rfs4_lo_state_t *)rfs4_dbsearch(fse->fse_lo_state_owner_idx,
	    &arg, &create, &arg, RFS4_DBS_VALID);

	if (lsp == NULL) {
		DTRACE_PROBE1(nfss__e__create_lostate, rfs4_mig_lo_state_t *,
		    hlp);
		cmn_err(CE_WARN,
		    "rfs4_hydrate_mig_lo_state: can't create rfs4_lo_state_t");

		rfs4_lockowner_rele(lo);
		rfs4_state_rele_nounlock(sp);
		fsh_ent_rele(rip, fse);
		return (ENOENT);
	}

	/* restore migrated lock state */
	lsp->rls_seqid = hlp->rls_mig_seqid;
	lsp->rls_lock_completed = 1;
	lsp->rls_skip_seqid_check = 0;
	rfs4_copy_reply(&lsp->rls_reply, &hlp->rls_mig_reply);

	/* install locks for this lock owner */
	locks = hlp->rls_mig_lo_list;
	while (locks) {
		flock.l_type = locks->ll_flock.l_type;
		flock.l_whence = 0;
		flock.l_start = locks->ll_flock.l_start;
		flock.l_len = locks->ll_flock.l_len;
		flock.l_sysid = lo->rl_client->rc_sysidt;
		flock.l_pid = lo->rl_pid;
		flag = (int)sp->rs_share_access | F_REMOTELOCK;

		err = rfs4_setlock(sp->rs_finfo->rf_vp, &flock, flag, kcred);
		if (err != 0) {
			DTRACE_PROBE1(nfss__e__setlock,
			    rfs4_mig_lo_state_t *, hlp);
			cmn_err(CE_WARN,
			    "rfs4_hydrate_mig_lo_state: setlock err %d", err);

			rfs4_clean_locks(sp);
			break;
		}
		locks = locks->ll_next;
	}

	/* give client a new lease to mksure migrated state remain valid */
	rfs4_update_lease(sp->rs_owner->ro_client);

	rfs4_state_rele_nounlock(sp);
	rfs4_lockowner_rele(lo);
	rfs4_lo_state_rele(lsp, FALSE);
	fsh_ent_rele(rip, fse);

	return (err);
}

static int
rfs4_to_mig_client(rfs4_client_t *cp, rfs4_mig_client_t *hcp)
{
	if (cp == NULL || hcp == NULL)
		return (EINVAL);

	hcp->rc_mig_clientid = cp->rc_clientid;
	hcp->rc_mig_confirm_verf = cp->rc_confirm_verf;
	hcp->rc_mig_nfs_client = cp->rc_nfs_client;
	hcp->rc_mig_nfs_client.mig_create = 0;
	hcp->rc_mig_addr = cp->rc_addr;
	hcp->rc_mig_cb_client = cp->rc_cbinfo.cb_callback;

	return (0);
}

int
rfs4_do_globals_hydrate(fsh_entry_t *fs_entry,
    struct rfs4_mig_globals *mig_globals)
{

	/* Hydrate the fse entry for the file system being migrated */
	fs_entry->fse_wr4verf = mig_globals->mig_write4verf;
	fs_entry->fse_stateid_verifier = mig_globals->mig_stateid_verifier;

	return (0);
}

static int
rfs4_to_mig_openowner(rfs4_openowner_t *op, rfs4_mig_openowner_t *hop)
{
	if (op == NULL || hop == NULL)
		return (EINVAL);

	hop->ro_mig_owner = op->ro_owner;
	hop->ro_mig_open_seqid = op->ro_open_seqid;
	hop->ro_mig_need_confirm = op->ro_need_confirm;
	hop->ro_mig_postpone_confirm = op->ro_postpone_confirm;
	hop->ro_mig_reply_fh = op->ro_reply_fh;
	hop->ro_mig_reply = op->ro_reply;

	return (0);
}

static int
rfs4_to_mig_state(rfs4_state_t *sp, rfs4_mig_state_t *hsp)
{
	rfs4_openowner_t *oo;
	rfs4_file_t *fp;

	if (hsp == NULL || sp == NULL)
		return (EINVAL);

	oo = sp->rs_owner;
	fp = sp->rs_finfo;

	hsp->rs_mig_stateid = sp->rs_stateid;
	ASSERT(sp->rs_stateid.bits.pid == 0);

	hsp->rs_mig_owner = oo->ro_owner;
	hsp->rs_mig_filehandle = fp->rf_filehandle;
	hsp->rs_mig_open_access = sp->rs_open_access;
	hsp->rs_mig_open_deny = sp->rs_open_deny;
	hsp->rs_mig_share_access = sp->rs_share_access;
	hsp->rs_mig_share_deny = sp->rs_share_deny;
	hsp->v_path = sp->rs_finfo->rf_vp->v_path;

	return (0);
}

static int
rfs4_to_mig_deleg_state(rfs4_deleg_state_t *dp, rfs4_mig_deleg_state_t *hdp)
{
	if (dp == NULL || hdp == NULL)
		return (EINVAL);

	hdp->rds_mig_dtype = dp->rds_dtype;
	hdp->rds_mig_delegid = dp->rds_delegid;
	hdp->rds_mig_filehandle = dp->rds_finfo->rf_filehandle;
	hdp->rds_mig_clientid = dp->rds_client->rc_clientid;

	return (0);
}

static int
rfs4_to_mig_lo_state(rfs4_lo_state_t *lsp, locklist_t *lo_list,
    rfs4_mig_lo_state_t *mlsp)
{
	if (lsp == NULL || lsp->rls_state == NULL ||
	    lsp->rls_locker == NULL || mlsp == NULL)
		return (EINVAL);

	mlsp->rls_mig_owner = lsp->rls_locker->rl_owner;
	mlsp->rls_mig_filehandle = lsp->rls_state->rs_finfo->rf_filehandle;
	mlsp->rls_mig_open_stateid = lsp->rls_state->rs_stateid;
	mlsp->rls_mig_lockid = lsp->rls_lockid;
	mlsp->rls_mig_seqid = lsp->rls_seqid;
	mlsp->rls_mig_reply = lsp->rls_reply;
	mlsp->rls_mig_lo_list = lo_list;

	return (0);
}

int
rfs4_do_client_hydrate(rfs_inst_t *rip, rfs4_client_t *cl)
{
	int err;
	rfs4_mig_client_t mig_cl;

	if (cl->rc_need_confirm != 0) {
		DTRACE_PROBE1(nfss__i__clconfirm, rfs4_client_t *, cl);
		return (0);
	}

	if ((err = rfs4_to_mig_client(cl, &mig_cl)) != 0)
		return (err);

	return (rfs4_hydrate_mig_client(rip, &mig_cl));
}

int
rfs4_do_openowner_hydrate(rfs_inst_t *rip, rfs4_openowner_t *oo)
{
	int err;
	rfs4_mig_openowner_t mig_oo;

	if ((err = rfs4_to_mig_openowner(oo, &mig_oo)) != 0)
		return (err);

	return (rfs4_hydrate_mig_openowner(rip, &mig_oo));
}

int
rfs4_do_state_hydrate(rfs_inst_t *rip, rfs4_state_t *os, vnode_t *rootvp)
{
	int err;
	rfs4_mig_state_t mig_os;

	if (os->rs_closed) {
		DTRACE_PROBE1(nfss__i__osclosed, rfs4_state_t *, os);
		return (0);
	}

	if ((err = rfs4_to_mig_state(os, &mig_os)) != 0)
		return (err);

	return (rfs4_hydrate_mig_state(rip, &mig_os, rootvp));
}

int
rfs4_do_deleg_state_hydrate(rfs_inst_t *rip, rfs4_deleg_state_t *dp,
    vnode_t *rootvp)
{
	int err;
	rfs4_mig_deleg_state_t mig_deleg;

	/* hydrate only active delegation state */
	if (dp->rds_dtype == OPEN_DELEGATE_NONE) {
		DTRACE_PROBE1(nfss__i__delegnone, rfs4_deleg_state_t *, dp);
		return (0);
	}

	if ((err = rfs4_to_mig_deleg_state(dp, &mig_deleg)) != 0)
		return (err);

	return (rfs4_hydrate_mig_deleg_state(rip, &mig_deleg, rootvp));
}

int
rfs4_do_lo_state_hydrate(rfs_inst_t *rip, rfs4_lo_state_t *ls,
    state_mig_lock_t *rfs4_mig_lock_tab)
{
	int err;
	rfs4_mig_lo_state_t mig_lo_state;

	if ((err = rfs4_to_mig_lo_state(ls, rfs4_mig_lock_tab->sml_lo_list,
	    &mig_lo_state)))
		return (err);

	return (rfs4_hydrate_mig_lo_state(rip, &mig_lo_state,
	    rfs4_mig_lock_tab->sml_lo_state_tab->sm_rootvp));
}
