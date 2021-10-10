/*
 * Copyright 1991 NCR Corporation - Dayton, Ohio, USA
 *
 * Copyright (c) 1994, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * These are the interfaces needed by the server side of the Lock Manager.
 * The code in this file is independent of the version of the NLM protocol.
 * For code to specifically support version 1-3 see the file lm_nlm_server.c.
 * For code to specifically support version 4 see the file lm_nlm4_server.c
 */

#include <sys/types.h>
#include <sys/cred.h>
#include <sys/errno.h>
#include <sys/file.h>
#include <sys/fcntl.h>
#include <sys/flock.h>
#include <sys/share.h>
#include <sys/kmem.h>
#include <sys/netconfig.h>
#include <sys/proc.h>
#include <sys/stream.h>
#include <sys/systm.h>
#include <sys/strsubr.h>

#include <sys/sysmacros.h>
#include <sys/user.h>
#include <sys/utsname.h>
#include <sys/vfs.h>
#include <sys/debug.h>
#include <sys/pathname.h>
#include <sys/callb.h>
#include <netinet/in.h>
#include <rpc/rpc.h>
#include <rpc/xdr.h>
#include <rpcsvc/sm_inter.h>
#include <nfs/nfs.h>
#include <nfs/nfssys.h>
#include <nfs/nfs_clnt.h>
#include <nfs/export.h>
#include <nfs/rnode.h>
#include <nfs/lm.h>
#include <nfs/lm_impl.h>
#include <nfs/lm_server.h>
#include <rpcsvc/sm_inter.h>
#include <rpcsvc/nsm_addr.h>

/* this next cluster of includes is all for inet_ntop() */
#include <inet/common.h>
#include <inet/ip.h>
#include <inet/ip6.h>

#include <sys/cmn_err.h>
#include <sys/mutex.h>
#include <sys/clconf.h>
#include <sys/cladm.h>
#include <sys/zone.h>

/*
 * Clustering hooks for setting local lock manager status and deleting
 * all the advisory file locks held by a remote client.
 */
void (*lm_set_nlm_status)(int, flk_nlm_status_t) = NULL;
void (*lm_remove_file_locks)(int) = NULL;

/*
 * Node id where the NLM server is running.  Always zero unless system is
 * booted as part of a cluster.
 */
int lm_global_nlmid = 0;

#ifdef DEBUG
int lm_gc_sysids = 0;
#endif /* DEBUG */

#ifndef lint
_NOTE(DATA_READABLE_WITHOUT_LOCK(lm_globals::lm_server_status))
#endif

#ifdef DEBUG
#ifndef lint
_NOTE(READ_ONLY_DATA(lm_gc_sysids))
#endif /* lint */
#endif /* DEBUG */

#ifndef lint
_NOTE(DATA_READABLE_WITHOUT_LOCK(lm_globals::lm_lockd_pid))
#endif

/*
 * static function prototypes (forward declarations).
 */
static void free_shares(struct lm_vnode *lv);
static int  lm_block_match(lm_block_t *target, struct flock64 *flkp,
    struct lm_vnode *lmvp, int *exactp);
static void lm_free_tables(lm_globals_t *lm);
static void lm_xprtclose(const SVCMASTERXPRT *xprt);
static void format_netbuf(char *, int, const struct netbuf *);
static void lm_unregister_callback(void *arg);

/*
 * Lock Manager callout table.
 * This table is used by svc_getreq() to dispatch a request with
 * a given prog/vers pair to an appropriate service provider
 * dispatch routine.
 */
static SVC_CALLOUT __nlm_sc[] = {
	{ NLM_PROG, NLM4_VERS, NLM4_VERS, lm_nlm4_dispatch },
	{ NLM_PROG, NLM_VERS,  NLM_VERS3, lm_nlm_dispatch  }
};

static SVC_CALLOUT_TABLE nlm_sct = {
	sizeof (__nlm_sc) / sizeof (__nlm_sc[0]), FALSE, __nlm_sc
};

/*
 * Lock Manager Server system call.
 * Does all of the work of running a LM server.
 * uap->fd is the fd of an open transport provider.
 * Caller must check privileges.
 */
int
lm_svc(struct lm_svc_args *uap)
{
	int error = 0;			/* intermediate error value */
	int retval = 0;			/* final return value */
	int tries;
	struct file *fp = NULL;
	SVCMASTERXPRT *xprt = NULL;	/* handle to use with given config */
	struct knetconfig config;
	struct lm_sysid *ls;
	struct lm_sysid *me;
	lm_globals_t *lm;

	lm = zone_getspecific(lm_zone_key, curproc->p_zone);

	/*
	 * Initialize global variables.
	 */
	lm->lm_sa = *uap;	/* Structure copy. */

	/*
	 * check the version number
	 */
	if (lm->lm_sa.version != LM_SVC_CUR_VERS) {
		cmn_err(CE_WARN, "lm_svc: expected version %d, got %d",
		    LM_SVC_CUR_VERS, uap->version);
		return (EINVAL);
	}

	/*
	 * Make sure that we won't accidentally clobber a struct flock
	 * l_pad area (this check has to go somewhere...).
	 */
#ifndef lint
	ASSERT(sizeof (pad_info_t) == 4 * sizeof (int));
#endif

	/* initialise the NFSv4 grace period & lease duration  */
	rfs4_grace_period = rfs4_lease_time = (time_t)lm->lm_sa.grace;

	/*
	 * Check fd.  Once we get the file pointer, we must
	 * release it before returning.
	 */
	if ((fp = getf(uap->fd)) == NULL) {
		return (EBADF);
	}

	/* set up knetconfig structure for tli routines */
	config.knc_semantics = uap->n_proto == LM_TCP ?
	    NC_TPI_COTS_ORD : NC_TPI_CLTS;
	if (uap->n_fmly == LM_INET)
		config.knc_protofmly = NC_INET;
	else if (uap->n_fmly == LM_INET6)
		config.knc_protofmly = NC_INET6;
	else
		config.knc_protofmly = NC_LOOPBACK;
#ifdef LOOPBACK_LOCKING
	if (uap->n_proto == LM_TCP || uap->n_proto == LM_UDP) {
		config.knc_proto = uap->n_proto == LM_TCP ? NC_TCP : NC_UDP;
	} else {
		config.knc_proto = NC_NOPROTO;
	}
#else
	config.knc_proto = uap->n_proto == LM_TCP ? NC_TCP : NC_UDP;
#endif
	config.knc_rdev	= uap->n_rdev;


	LM_DEBUG((1, "svc",
	    "fm= %s, pr= %s, dv= %lx, db= %u, handle_tm= %lu, "
	    "rexmit_tm=%lu, gr= %u",
	    config.knc_protofmly, config.knc_proto,
	    config.knc_rdev, lm->lm_sa.debug, lm->lm_sa.timout,
	    lm->lm_sa.retransmittimeout, lm->lm_sa.grace));

	mutex_enter(&lm->lm_lock);

	/*
	 * Check whether the lock manager is shutting down.  Once it's
	 * down, it's okay to restart it, but if it's still shutting down,
	 * we should bail out.
	 */
	if (lm->lm_server_status == LM_SHUTTING_DOWN) {
		retval = EAGAIN;
		mutex_exit(&lm->lm_lock);
		goto done;
	}

	/*
	 * If the lock manager is restarting or starting for the first
	 * time, remember lockd's pid, so that we can kill it when we
	 * shutdown.
	 */
	if (lm->lm_server_status == LM_DOWN ||
	    lm->lm_lockd_pid == 0) {
		lm->lm_lockd_pid = curproc->p_pid;
		/*
		 * set up the system portion of the owner handle id
		 * Currently this is the first four characters of the
		 * name for this node.  It is possible that this won't
		 * be unique.  The protocol does not require this to
		 * be unique.  It is also possible that nodename
		 * will be shorter than LM_OH_SYS_LEN.  This is also
		 * OK because this data is used as binary data and is
		 * not interpreted.
		 */
		(void) strncpy((char *)&lm->lm_owner_handle_sys,
		    uts_nodename(), LM_OH_SYS_LEN);

		/* Register for a RPC pool closure callback */

		if (svc_pool_control(NLM_SVCPOOL_ID, SVCPSET_UNREGISTER_PROC,
		    lm_unregister_callback, lm) != 0) {
			/*
			 * Could not find the RPC pool for ourselves.
			 * Bail out, inability to register the closure
			 * callback is a critical error.
			 */
			retval = EAGAIN;
			zcmn_err(lm->lm_zoneid, CE_WARN,
			    "lockd: failed to register callbacks for the pool");
			goto done;
		}
	}

	/*
	 * The NCR code used the amount of time since lm_svc was last called
	 * to determine if this is a restart.  Now that the lm_server_status
	 * state machine exists it is used to determine if this is a restart.
	 * When this lm_svc is called in the LM_DOWN state, consider this a
	 * restart.  If we are restarting, then:
	 * - Release all locks held by the LM.
	 * - Inform the status monitor and the local locking code.
	 */
	if (lm->lm_server_status == LM_DOWN) {
		zoneid_t zoneid = getzoneid();

		LM_DEBUG((9, "svc", "lockmanager server is starting up\n"));
		/*
		 * Set the start_time so that other LM daemons do not
		 * enter this code.
		 */
		lm->lm_start_time = gethrestime_sec();
		lm->lm_server_status = LM_UP;

		/*
		 * We avoid setting the lock manager (server) state inside
		 * non-global zones and the following check needs to be
		 * changed when cluster supports NFS server in non-global
		 * zones.
		 */
		if (lm_set_nlm_status && (GLOBAL_ZONEID == zoneid)) {
			/* cluster file system server module is loaded */
			(*lm_set_nlm_status)(lm_global_nlmid, FLK_NLM_UP);
			LM_DEBUG((9, "svc", "nlmid = %d", lm_global_nlmid));
		} else {
			/* default to LLM */
			flk_set_lockmgr_status(FLK_LOCKMGR_UP);
		}

		mutex_exit(&lm->lm_lock);

		/*
		 * Release all locks held by LM.
		 * Reset the SM told indicator.  They will be reestablished
		 * by a reclaim.  Release all client handles.
		 */
		rw_enter(&lm->lm_sysids_lock, RW_READER);
		for (ls = avl_first(&lm->lm_sysids); ls != NULL;
		    ls = AVL_NEXT(&lm->lm_sysids, ls)) {
			lm_ref_sysid(ls);
			lm_unlock_client(lm, ls);
			ls->sm_server = FALSE;
			lm_rel_sysid(ls);
		}
		rw_exit(&lm->lm_sysids_lock);
		lm_flush_clients(lm, NULL);

		/*
		 * Inform the SM that we have restarted.
		 */
		me = lm_get_me();
		if (me != NULL) {
			clock_t delay = (LM_STATD_DELAY * hz);

			for (tries = 0; tries < LM_RETRY; tries++) {
				error = lm_callrpc(lm, me, SM_PROG, SM_VERS,
				    SM_SIMU_CRASH, xdr_void, NULL,
				    xdr_void, NULL, LM_CR_TIMOUT,
				    LM_RETRY, FALSE);
				if (error == 0 || error != EIO)
					break;
				delay <<= 1;
				(void) delay_sig(delay);
			}
			LM_DEBUG((1, "svc", "[%p]: lm_callrpc returned %d",
			    (void *)curthread, error));
		}

		if (error != 0 || me == NULL) {
			nfs_cmn_err(error, CE_WARN,
			    "lockd: cannot contact statd (%m), continuing");
		}

		/*
		 * Send SIGLOST to all the client processes on this host.
		 * After performing SM_SIMU_CRASH, all /etc/sm files have been
		 * deleted.  Therefore inform SM again if necessary.
		 */
		rw_enter(&lm->lm_sysids_lock, RW_READER);

		lm_fail_clients(lm);

		if (me != NULL) {
			for (ls = avl_first(&lm->lm_sysids); ls != NULL;
			    ls = AVL_NEXT(&lm->lm_sysids, ls)) {
				lm_ref_sysid(ls);
				LM_DEBUG((1, "svc", "[%p]: processing sysid %x",
				    (void *)curthread, ls->sysid));
				if (ls->sm_server) {
					ls->sm_server = FALSE;
					lm_ref_sysid(me);
					lm_sm_server(lm, ls, me);
				}
				lm_rel_sysid(ls);
			}
			lm_rel_sysid(me);
		}
		rw_exit(&lm->lm_sysids_lock);

		/*
		 * Set the start_time again.
		 * It might take a long time to do a SM_SIMU_CRASH, and we
		 * want all clients to have a full grace period.
		 */
		mutex_enter(&lm->lm_lock);
		lm->lm_start_time = gethrestime_sec();
		mutex_exit(&lm->lm_lock);

	} else {
		/*
		 * When server is rebooted, record the time
		 * as an indicator that grace period has
		 * started. This need to be done only once per
		 * every reboot.
		 */
		if (lm->lm_grace_started) {
			lm->lm_start_time = gethrestime_sec();
			lm->lm_grace_started = FALSE;
		}
		mutex_exit(&lm->lm_lock);
	}
	/*
	 * Allow this xprt to be mapped to `config' via fp.  In
	 * NCR's design, an LM_SVC registration thread slept in the
	 * kernel and eventually serviced its `own' NLM requests
	 * (those for its config/addr/xprt).  Such a thread
	 * therefore always had access to its original knetconfig
	 * information, stashed in its u-area.
	 *
	 * Here the calling thread leaves the kernel and clone
	 * threads of the original service thread are fired off to
	 * service NLM requests.  The clone threads have no
	 * knowledge of what their `original' knetconfig was -
	 * unless we save it for them now.
	 */
	(void) lm_saveconfig(lm, fp, &config);

	/*
	 * Create a transport endpoint and create one or more kernel threads
	 * to run the LM service loop (svc_run).
	 */
	error = svc_tli_kcreate(fp, 0, NULL, NULL, &xprt, &nlm_sct,
	    lm_xprtclose, NLM_SVCPOOL_ID, FALSE);
	if (error) {
		LM_DEBUG((1, "svc", "svc_tli_kcreate returned %d", error));
		lm_rmconfig(lm, fp);
		releasef(uap->fd);
		return (error);
	}

	ASSERT(xprt != NULL);

	LM_DEBUG((1, "svc", "xprt= %p", (void *)xprt));

	LM_DEBUG((1, "svc", "Running, start_time= %lx\n", lm->lm_start_time));

done:
	/* Clustering: save the node id of the NLM server. */
	if (cluster_bootflags & CLUSTER_BOOTED) {
		lm_global_nlmid = clconf_get_nodeid();
	}

	LM_DEBUG((1, "lm_svc", "lm_global_nlmid= %d", lm_global_nlmid));

	if (fp != NULL)
		releasef(uap->fd);
	return (retval);
}

/*
 * Tell the SM to monitor a server for a client
 * (unless we've already done so previously).
 */
void
lm_sm_client(lm_globals_t *lm, struct lm_sysid *ls, struct lm_sysid *me)
{
	struct mon m;
	struct sm_stat_res ssr;
	int error;

	LM_DEBUG((4, "sm_client", "server= %s, sysid= %x, "
	    "sm_client= %d, me= %p",
	    ls->name, ls->sysid, ls->sm_client, (void *)me));

	if (!ls->sm_client && me != NULL) {
		m.mon_id.mon_name = ls->name;
		m.mon_id.my_id.my_name = curproc->p_zone->zone_nodename;
		m.mon_id.my_id.my_prog = NLM_PROG;
		m.mon_id.my_id.my_vers = NLM_VERS2;
		m.mon_id.my_id.my_proc = PRV_RECOVERY;

		error = lm_callrpc(lm, me, SM_PROG, SM_VERS, SM_MON, xdr_mon,
		    (caddr_t)&m, xdr_sm_stat_res, (caddr_t)&ssr,
		    LM_SM_TIMOUT, LM_RETRY, FALSE);
		if (error == 0) {
			ls->sm_client = (ssr.res_stat == stat_succ);
		} else {
			ls->sm_client = 0;
		}
		ls->sm_client = (ssr.res_stat == stat_succ);
	}

	if (me != NULL)
		lm_rel_sysid(me);
}

/*
 * Tell the SM to monitor a client for a server
 * (unless we've already done so previously).
 */
void
lm_sm_server(lm_globals_t *lm, struct lm_sysid *ls, struct lm_sysid *me)
{
	struct mon m;
	struct sm_stat_res ssr;
	int error;
	struct sockaddr_in *sin;
	struct sockaddr_in6 *sin6;
	struct reg1args regargs;
	struct reg1res regres;

	LM_DEBUG((4, "sm_server", "client= %s, sysid= %x, "
	    "sm_server= %d, me= %p", ls->name, ls->sysid,
	    ls->sm_server, (void *)me));

	if (me == NULL)
		return;
	if (ls->sm_server) {
		lm_rel_sysid(me);
		return;
	}

	/*
	 * Register the client's address (now, while we have it!)
	 * with our statd using the private NSM_ADDR protocol.
	 *
	 * The purpose of this is to help our server contact
	 * clients from other domains upon crash recovery by
	 * passing it a full IP address to commit to stable storage.
	 * See bug 1184192.
	 */
	bzero((caddr_t)&regargs, sizeof (regargs));
	bzero((caddr_t)&regres, sizeof (regres));

	if ((strcmp(ls->config.knc_protofmly, NC_INET) != 0) &&
	    (strcmp(ls->config.knc_protofmly, NC_INET6) != 0)) {
		cmn_err(CE_WARN, "can't register %s addr for %s",
		    ls->config.knc_protofmly, ls->name);
	} else {
		if (strcmp(ls->config.knc_protofmly, NC_INET) == 0) {
			sin = (struct sockaddr_in *)ls->addr.buf;
			regargs.family = sin->sin_family;
			regargs.address.n_len = sizeof (sin->sin_addr.s_addr);
			regargs.address.n_bytes = (char *)&sin->sin_addr.s_addr;
		} else {
			sin6 = (struct sockaddr_in6 *)ls->addr.buf;
			regargs.family = sin6->sin6_family;
			regargs.address.n_len =
			    sizeof (struct in6_addr);
			regargs.address.n_bytes =
			    (char *)sin6->sin6_addr.s6_addr;
		}
		regargs.name = lm_dup(ls->name, strlen(ls->name) + 1);

		error = lm_callrpc(lm, me, NSM_ADDR_PROGRAM, NSM_ADDR_V1,
		    NSMADDRPROC1_REG, xdr_reg1args,
		    (caddr_t)&regargs, xdr_reg1res,
		    (caddr_t)&regres, LM_SM_TIMOUT,
		    LM_RETRY, FALSE);
		if (error != 0 || regres.status != RPC_SUCCESS) {
			LM_DEBUG((3, "sm_server",
			"callrpc(NSMADDRPROC1_REG) err= %d, res_stat= %d",
			    error, regres.status));
		}
		kmem_free(regargs.name, strlen(regargs.name) + 1);
	}

	/*
	 * Now make the usual call to register this client with
	 * our statd.
	 */
	m.mon_id.mon_name = ls->name;
	m.mon_id.my_id.my_name = uts_nodename();
	m.mon_id.my_id.my_prog = NLM_PROG;
	m.mon_id.my_id.my_vers = NLM_VERS2;
	m.mon_id.my_id.my_proc = PRV_CRASH;

	error = lm_callrpc(lm, me, SM_PROG, SM_VERS, SM_MON, xdr_mon,
	    (caddr_t)&m, xdr_sm_stat_res, (caddr_t)&ssr,
	    LM_SM_TIMOUT, LM_RETRY, FALSE);
	if (error == 0) {
		ls->sm_server = (ssr.res_stat == stat_succ);
	} else {
		ls->sm_server = 0;
	}

	lm_rel_sysid(me);
}

/*
 * Release the lm_vnode, that is:
 *	- decrement count
 *	- if count==0 release vnode iff no remote locks or remote shares
 *	  exist.
 */
void
lm_rel_vnode(lm_globals_t *lm, struct lm_vnode *lv)
{
	int flag = FREAD | FWRITE;
	int error;
	struct vnode *tmpvp;
	struct flock64 flk;
	struct shrlock shr;

	mutex_enter(&lm->lm_lock);
	ASSERT(lv->count > 0);

	if (--(lv->count) == 0) {
		/*
		 * Check NFS locks and shares.
		 *
		 * F_HASREMOTELOCKS passes back a boolean flag in l_rpid:
		 * 1 ==> vp has NFS locks, else 0.
		 */

		flk.l_sysid = 0;	/* must be initialized */

		LM_DEBUG((2, "rel_vnode", "l_sysid = 0x%x", flk.l_sysid));

		error = VOP_FRLOCK(lv->vp, F_HASREMOTELOCKS, &flk, flag,
		    (u_offset_t)0, NULL, CRED(), NULL);

		LM_DEBUG((2, "rel_vnode", "error = %d, l_has_rmt = %d",
		    error, l_has_rmt(&flk)));

		/*
		 * Clustering:
		 * If VOP_FRLOCK returns an EIO, this means that the
		 * PXFS method invocation failed because the object reference
		 * to the server object is stale and no longer valid. We must
		 * release the vnode here because the NLM remembers both
		 * the file handle and the associated vnode; no clean up
		 * is done by the PXFS client.  Therefore, the condition
		 * is the following:  (i) if we're in a cluster, _and_
		 * (ii) the return value is EIO, then discard the vnode.
		 */
		if (error == EIO && lm_global_nlmid != 0) {
			tmpvp = lv->vp;
			lv->vp = NULL;
			bzero(&lv->fh2, sizeof (nfs_fhandle));
			bzero(&lv->fh3, sizeof (nfs_fh3));
			VN_RELE(tmpvp);
		} else {
			if (error == 0 && l_has_rmt(&flk) == 0) {
				/*
				 * No NFS locks exist on lv->vp, check shares.
				 * Set sysid == 0 to lookup all sysids.
				 * NOTE: the access field is overloaded as a
				 * boolean flag, 1 == vp has NFS shares,
				 * else 0.
				 */
				shr.s_access = 0;
				shr.s_sysid = 0;
				shr.s_pid = 0;
				shr.s_own_len = 0;
				shr.s_owner = NULL;

				lm_set_nlmid_shr(&shr.s_sysid);
				LM_DEBUG((2, "rel_vnode", "s_sysid = 0x%x",
				    shr.s_sysid));

				error = VOP_SHRLOCK(lv->vp, F_HASREMOTELOCKS,
				    &shr, flag, CRED(), NULL);

				LM_DEBUG((2, "rel_vnode",
				    "error = %d (shrlock), s_access = %d",
				    error, shr.s_access));

				/*
				 * Clustering:
				 *   If VOP_SHRLOCK returns EIO and we're in
				 * a cluster it is correct to release vnode.
				 */
				if (error == EIO && lm_global_nlmid != 0 ||
				    error == 0 && shr.s_access == 0) {
					/*
					 * No NFS locks or shares exist on
					 * lv->vp. Release vnode and mark
					 * lm_vnode as free.
					 */
					tmpvp = lv->vp;
					lv->vp = NULL;
					bzero(&lv->fh2, sizeof (nfs_fhandle));
					bzero(&lv->fh3, sizeof (nfs_fh3));
					VN_RELE(tmpvp);
				}
			}
		}
	}

	LM_DEBUG((2, "rel_vnode",
	    "cnt= %d, vp= %p, v_cnt= %d, v_flk= %p",
	    lv->count, (void *)lv->vp, (lv->vp ? lv->vp->v_count : -1),
	    lv->vp ? (void *)lv->vp->v_filocks : NULL));

	mutex_exit(&lm->lm_lock);
}

/*
 * Free any unused lm_vnode's.
 */
void
lm_free_vnode(void *cdrarg)
{
	lm_globals_t *lm = (lm_globals_t *)cdrarg;
	struct lm_vnode *lv;
	struct lm_vnode *prevlv = NULL;	/* previous kept lm_vnode */
	struct lm_vnode *nextlv = NULL;

	mutex_enter(&lm->lm_vnodes_lock);
	mutex_enter(&lm->lm_lock);
	LM_DEBUG((5, "free_vnode", "start length: %d\n", lm->lm_vnode_len));

	for (lv = lm->lm_vnodes; lv != NULL; lv = nextlv) {
		nextlv = lv->next;
		if (lv->vp != NULL) {
			prevlv = lv;
		} else {
			if (prevlv == NULL) {
				lm->lm_vnodes = nextlv;
			} else {
				prevlv->next = nextlv;
			}
			ASSERT(lm->lm_vnode_len != 0);
			--lm->lm_vnode_len;
			ASSERT(lv->count == 0);
			ASSERT(lv->blocked == NULL);
			kmem_cache_free(lm->lm_vnode_cache, lv);
		}
	}

	LM_DEBUG((5, "free_vnode", "end length: %d\n", lm->lm_vnode_len));
	mutex_exit(&lm->lm_lock);
	mutex_exit(&lm->lm_vnodes_lock);
}

/*
 * Return non-zero if there is an lm_vnode for the given local vp.
 */
int
lm_vp_active(const vnode_t *vp)
{
	struct lm_vnode *lv;
	int match = 0;
	lm_globals_t *lm;

	lm = zone_getspecific(lm_zone_key, curproc->p_zone);

	mutex_enter(&lm->lm_vnodes_lock);
	mutex_enter(&lm->lm_lock);

	for (lv = lm->lm_vnodes; lv != NULL; lv = lv->next) {
		if (lv->vp == vp) {
			match = 1;
			goto done;
		}
	}

done:
	mutex_exit(&lm->lm_lock);
	mutex_exit(&lm->lm_vnodes_lock);

	return (match);
}

/*
 * Remove all locks and shares set by the client having lm_sysid `ls'.
 */
void
lm_unlock_client(lm_globals_t *lm, struct lm_sysid *ls)
{
	struct lm_vnode *lv;
	struct flock64 flk;
	struct shrlock shr;
	int locks_released = 0;
	int new_sysid;
	int flag = FREAD | FWRITE;

	LM_DEBUG((4, "ulck_clnt", "name= %s, sysid= %x", ls->name, ls->sysid));

	/*
	 * Release all locks and shares held by the client, and unblock
	 * (deny) all threads blocking on locks for the client.
	 *
	 * That is, for each vnode in use release any lock/share held by
	 * client.  Note:  we have to increment count, so that the lm_vnode
	 * is not released while we are using it.
	 */
	mutex_enter(&lm->lm_vnodes_lock);

	for (lv = lm->lm_vnodes; lv; lv = lv->next) {
		mutex_enter(&lm->lm_lock);
		if (lv->vp == NULL) {
			mutex_exit(&lm->lm_lock);
			continue;
		}
		lv->count++;

		if (locks_released == 0) {
			/*
			 * Release *all* locks of this client.
			 * XXX: too long a time to hold locks here?
			 */

			/*
			 * Are we booted as a cluster and is PXFS loaded?
			 */
			if (lm_remove_file_locks) {
				/*
				 * This routine removes the file locks for a
				 * client over all nodes of a cluster.
				 */
				new_sysid = ls->sysid;
				lm_set_nlmid_flk(&new_sysid);

				LM_DEBUG((2, "unlock_client",
				    "nlmid = %d, sysid = 0x%x",
				    GETNLMID(new_sysid),
				    GETSYSID(new_sysid)));

				(*lm_remove_file_locks)(new_sysid);
			}

			/* Release UFS locks */
			flk.l_type = F_UNLKSYS;
			flk.l_whence = 0;
			flk.l_start = 0;
			flk.l_len = 0;
			flk.l_sysid = ls->sysid;
			flk.l_pid = 0;

			(void) VOP_FRLOCK(lv->vp, F_SETLK, &flk,
			    F_REMOTELOCK | flag, (u_offset_t)0,
			    NULL, CRED(), NULL);

			locks_released = 1;
		}

		/*
		 * Release any shares held by this sysid
		 * by setting own_len == 0 and then let go our hold on vp.
		 */
		shr.s_access = 0;
		shr.s_deny = 0;
		shr.s_sysid = (int32_t)ls->sysid;
		shr.s_pid = 0;
		shr.s_own_len = 0;
		shr.s_owner = NULL;

		lm_set_nlmid_shr(&shr.s_sysid);

		LM_DEBUG((2, "unlock_client", "nlmid = %d (shrlock)",
		    GETNLMID(shr.s_sysid)));

		(void) VOP_SHRLOCK(lv->vp, F_UNSHARE, &shr, flag, CRED(), NULL);

		mutex_exit(&lm->lm_lock);
		lm_rel_vnode(lm, lv);
	}

	mutex_exit(&lm->lm_vnodes_lock);

	/*
	 * release all threads waiting for responses to granted messages
	 */
	lm_release_blocks(lm, ls->sysid);

	/*
	 * Make sure we reinform the SM (why is this necessary?).
	 */
	ls->sm_client = FALSE;
}

/*
 * A client machine has rebooted.
 * Release all locks and shares held for the client.
 */
/* ARGSUSED */
bool_t
lm_crash(void *gen_args, void *resp, struct lm_nlm_disp *disp,
    struct lm_sysid *ls, uint_t xid)
{
	status *s = (status *)gen_args;
	lm_globals_t *lm;

	lm = zone_getspecific(lm_zone_key, curproc->p_zone);

	LM_DEBUG((2, "crash", "mon_name= %s, state= %d", s->mon_name,
	    s->state));

	if (s->mon_name) {
		rw_enter(&lm->lm_sysids_lock, RW_READER);
		for (ls = avl_first(&lm->lm_sysids); ls != NULL;
		    ls = AVL_NEXT(&lm->lm_sysids, ls)) {
			if (strcmp(s->mon_name, ls->name) == 0) {
				lm_ref_sysid(ls);
				lm_unlock_client(lm, ls);
				lm_flush_clients(lm, ls);
				lm_rel_sysid(ls);
			}
		}
		rw_exit(&lm->lm_sysids_lock);
	}
	LM_DEBUG((2, "crash", "End"));

	return (TRUE);
}

/*
 * Clustering: encodes the node id of the node where the NLM server is
 * running in "sysid."  This node id doubles as the "nlm id".
 */
void
lm_set_nlmid_flk(int *sysid)
{
	if (lm_global_nlmid != 0) {
		*sysid = (*sysid) | (lm_global_nlmid << BITS_IN_SYSID);
	}

}

/*
 * Clustering: encodes the node id of the node where the NLM server is
 * running in "sysid."  This node id doubles as the "nlm id".
 */
void
lm_set_nlmid_shr(int32_t *sysid)
{
	if (lm_global_nlmid != 0) {
		*sysid = (*sysid) | (lm_global_nlmid << BITS_IN_SYSID);
	}
}

/*
 * Retransmit (reclaim) all locks held by client (current zone) on server.
 *
 * XXX NCR porting issues:
 *  1. For HA, need heuristic to decide which sysids are for takeover server?
 */
void
lm_relock_server(lm_globals_t *lm, char *server)
{
	struct lm_sysid *ls;
	locklist_t *llp, *next_llp;

	LM_DEBUG((4, "rlck_serv", "server= %s", server ? server : "(NULL)"));

	/*
	 * We can't verify that caller has lm_sysids_lock as a reader the
	 * way we'd like to, but at least we can assert that somebody does.
	 */
	ASSERT(RW_READ_HELD(&lm->lm_sysids_lock));

	/*
	 * 0.  Make sure we have lm_sysids_lock, so that list is frozen.
	 * 1.  Walk lm_sysids list to map `server' to any and all sysid(s)
	 *		we have for it:
	 * 2.  foreach (sysid):
	 * 3.    get the list of active locks for this sysid.
	 * 4.    foreach (lock on list):
	 * 5. 		Reclaim lock (lm_reclaim_lock()) or
	 *			signal SIGLOST.
	 * 6.		free(lock list entry).
	 * 7.    end.
	 * 8.  end.
	 */

	LM_DEBUG((1, "rlck_serv", "entering"));
	for (ls = avl_first(&lm->lm_sysids); ls != NULL;
	    ls = AVL_NEXT(&lm->lm_sysids, ls)) {
		if (server != NULL && (strcmp(server, ls->name) != 0))
			continue;
		/*
		 * Fudge the reference count for the lm_sysid.  We know
		 * that the entry can't go away because we hold the lock
		 * for the list.  As long as we don't pass the lm_sysid to
		 * any other routines, we don't need to bump the reference
		 * count.
		 */
		LM_DEBUG((1, "rlck_serv", "calling VOP_FRLOCK(ACT) on %x",
		    ls->sysid | LM_SYSID_CLIENT));

		llp = flk_get_active_locks(ls->sysid | LM_SYSID_CLIENT, NOPID);
		LM_DEBUG((1, "rlck_serv", "VOP_FRLOCK(ACT) returned llp %p",
		    (void *)llp));
		while (llp) {
			LM_DEBUG((1, "rlck_serv", "calling lm_reclaim(%p)",
			    (void *)llp));
			lm_reclaim_lock(lm, llp->ll_vp, &llp->ll_flock);
			next_llp = llp->ll_next;
			VN_RELE(llp->ll_vp);
			kmem_free(llp, sizeof (*llp));
			llp = next_llp;
		}
	}
}

/*
 * Fail all locks held by client (only current zone) on server.
 */
void
lm_fail_clients(lm_globals_t *lm)
{
	struct lm_sysid *ls;
	locklist_t *llp, *next_llp;

	/*
	 * We can't verify that caller has lm_sysids_lock as a reader the
	 * way we'd like to, but at least we can assert that somebody does.
	 */
	ASSERT(RW_READ_HELD(&lm->lm_sysids_lock));

	/*
	 * 1.  Make sure we have lm_sysids_lock, so that list is frozen.
	 * 2.  foreach (sysid):
	 * 3.    get the list of active locks for this sysid.
	 * 4.    foreach (lock on list):
	 * 5.  		signal SIGLOST.
	 * 6.		free(lock list entry).
	 * 7.    end.
	 * 8.  end.
	 */

	LM_DEBUG((1, "fail_clients", "entering fail_client"));
	for (ls = avl_first(&lm->lm_sysids); ls != NULL;
	    ls = AVL_NEXT(&lm->lm_sysids, ls)) {
		lm_ref_sysid(ls);
		LM_DEBUG((1, "fail_clients", "calling VOP_FRLOCK(ACT) on %x",
		    ls->sysid | LM_SYSID_CLIENT));

		llp = flk_get_active_locks(ls->sysid | LM_SYSID_CLIENT, NOPID);
		LM_DEBUG((1, "fail_clients", "VOP_FRLOCK(ACT) returned llp %p",
		    (void *)llp));
		while (llp) {
			/*
			 * lockd should not affect NFSv4 locks, so only
			 * signal the process for v2/v3 vnodes.
			 */
			if (vn_matchops(llp->ll_vp, nfs3_vnodeops) ||
			    vn_matchops(llp->ll_vp, nfs_vnodeops)) {
				LM_DEBUG((1, "fail_clients",
				    "calling lm_send_siglost(%p)",
				    (void *)llp));
				lm_send_siglost(lm, &llp->ll_flock, ls);
			}
			next_llp = llp->ll_next;
			VN_RELE(llp->ll_vp);
			kmem_free(llp, sizeof (*llp));
			llp = next_llp;
		}
		lm_rel_sysid(ls);
	}
}

/*
 * Reclaim the given lock for the given NFS v2 or v3 file.  No-op if the
 * file is not a v2 or v3 file.
 */
void
lm_reclaim_lock(lm_globals_t *lm, struct vnode *vp, struct flock64 *flkp)
{
	if (!(vn_matchops(vp, nfs3_vnodeops) || vn_matchops(vp, nfs_vnodeops)))
		return;

	if (VTOMI(vp)->mi_vers == NFS_V3) {
		lm_nlm4_reclaim(lm, vp, flkp);
	} else {
		lm_nlm_reclaim(lm, vp, flkp);
	}
}

/*
 * nlm dispatch bookkeeping routines.
 */

/*
 * bump the counter keeping track of the number of outstanding requests
 * check on the status of the server to make sure it is still up
 * return 0 if everything is OK
 * return 1 if an error is found (the server is not up)
 * N.B. the counter of keeping track of the number of outstanding requests
 * is bumped even if there is an error.  This means the caller must call
 * nlm_dispatch_exit before returning from the dispatch routine.
 */
int
nlm_dispatch_enter(lm_globals_t *lm, SVCXPRT *xprt)
{
	int error;

#ifdef DEBUG
	if (lm_gc_sysids)
		lm_free_sysid_table(lm);
#endif

	mutex_enter(&lm->lm_lock);
	/*
	 * this is a new request.  bump the count of outstanding requests
	 */
	lm->lm_num_outstanding++;
	/*
	 * We shouldn't be getting new requests if we're down.
	 */
	if (lm->lm_server_status != LM_UP) {
		if (lm->lm_server_status == LM_DOWN) {
			mutex_exit(&lm->lm_lock);
			cmn_err(CE_WARN,
			    "lm_nlm_dispatch: unexpected request.");
		} else {
			mutex_exit(&lm->lm_lock);
		}
		svcerr_systemerr(xprt);	/* could just drop on the floor? */
		error = 1;
	} else {
		mutex_exit(&lm->lm_lock);
		error = 0;
	}
	return (error);
}

/*
 * decrement the counter keeping track of the number of outstanding requests
 * if the server status is in the process of coming down and this is the
 * last outstanding request, then bring the server completely down by
 * releasing all the dynamically allocated resources.
 */
void
nlm_dispatch_exit(lm_globals_t *lm)
{
	mutex_enter(&lm->lm_lock);
	/*
	 * this request is now done.  decrement the number of outstanding
	 * requests.  If this is the last request and we are in the process
	 * of shutting down, mark the lock manager as down and free all of
	 * our resources (including remote locks already granted).  As soon
	 * as we have dynamic garbage collection
	 * in the kernel, we won't need to free the tables anymore.
	 */
	lm->lm_num_outstanding--;
	if ((lm->lm_num_outstanding == 0) &&
	    (lm->lm_server_status == LM_SHUTTING_DOWN)) {
		/*
		 * Clustering
		 * We avoid setting the lock manager (server) state inside
		 * non-global zones and the following check needs to be
		 * changed when cluster supports NFS server in non-global
		 * zones.
		 */
		if (lm_set_nlm_status && (GLOBAL_ZONEID == getzoneid())) {
			/* pxfs server module loaded */
			(*lm_set_nlm_status)(lm_global_nlmid, FLK_NLM_DOWN);
		} else {
			/* pxfs server module not loaded; default to LLM */
			flk_set_lockmgr_status(FLK_LOCKMGR_DOWN);
		}

		mutex_exit(&lm->lm_lock);
		lm_free_tables(lm);
		/*
		 * Now that all the cleanup has happened, mark the lock
		 * manager as down and notify anyone waiting for this
		 * event.
		 */
		mutex_enter(&lm->lm_lock);
		lm->lm_server_status = LM_DOWN;
		cv_broadcast(&lm->lm_status_cv);
		mutex_exit(&lm->lm_lock);
	} else {
		mutex_exit(&lm->lm_lock);
	}
}

/*
 * Shut down the lock manager and return when everything is done.  Returns
 * zero for success or an errno value.
 * Caller must check privileges.
 */
int
lm_shutdown()
{
	proc_t *p;
	lm_globals_t *lm;

	lm = zone_getspecific(lm_zone_key, curproc->p_zone);

	if (lm->lm_lockd_pid == 0) {
		return (ESRCH);		/* lock manager wasn't started */
	}

	/*
	 * Try to signal lockd.  If there's no such pid, it probably means
	 * that lockd has already gone away for some reason, so just wait
	 * until the lock manager is completely down.
	 */
	mutex_enter(&pidlock);
	p = prfind(lm->lm_lockd_pid);
	if (p != NULL) {
		psignal(p, SIGTERM);
	}
	mutex_exit(&pidlock);

	mutex_enter(&lm->lm_lock);
	while (lm->lm_server_status != LM_DOWN) {
		cv_wait(&lm->lm_status_cv, &lm->lm_lock);
	}
	mutex_exit(&lm->lm_lock);

	return (0);
}

/*
 * This callback routine is invoked by RPC on pool closure.
 * If there are active requests at this point, the server
 * status is transitioned to SHUTTING_DOWN and any sleeping
 * threads are woken up. This allows for the closure of
 * active connections since active requests hold references
 * to the connections.
 */
static void
lm_unregister_callback(void *arg)
{
	lm_globals_t *lm = (lm_globals_t *)arg;

	mutex_enter(&lm->lm_lock);
	/*
	 * Record that we are shutting down.  Tell the local
	 * locking code to wake up sleeping remote requests.
	 */
	if (lm->lm_server_status == LM_UP) {
		lm->lm_server_status = LM_SHUTTING_DOWN;
		/*
		 * Clustering
		 *
		 * We avoid setting the lock manager (server) state
		 * inside non-global zones and the following check
		 * needs to be changed when cluster supports NFS
		 * server in non-global zones.
		 */
		if (lm_set_nlm_status && (GLOBAL_ZONEID ==
		    getzoneid())) {
			/* pxfs server module loaded */
			(*lm_set_nlm_status)(lm_global_nlmid,
			    FLK_NLM_SHUTTING_DOWN);
		} else {
			/*
			 * Invoke LLM directly at this node.
			 */
			flk_set_lockmgr_status(FLK_WAKEUP_SLEEPERS);
		}
	}
	mutex_exit(&lm->lm_lock);
}

/*
 * Callback routine for when a transport is closed.  Removes the config for
 * the transport from the config table.  If this is the last transport,
 * initiate the shut down sequence for the lockmanager
 */
void
lm_xprtclose(const SVCMASTERXPRT *xprt)
{
	lm_globals_t *lm;

	lm = zone_getspecific(lm_zone_key, curproc->p_zone);

	lm_rmconfig(lm, xprt->xp_fp);

	mutex_enter(&lm->lm_lock);
	if (lm->lm_numconfigs == 0) {
		/*
		 * Record that we are shutting down.  Tell the local
		 * locking code to wake up sleeping remote requests.
		 */
		if (lm->lm_server_status == LM_UP) {
			lm->lm_server_status = LM_SHUTTING_DOWN;
			/*
			 * Clustering
			 *
			 * We avoid setting the lock manager (server) state
			 * inside non-global zones and the following check
			 * needs to be changed when cluster supports NFS
			 * server in non-global zones.
			 */
			if (lm_set_nlm_status && (GLOBAL_ZONEID ==
			    getzoneid())) {
				/* pxfs server module loaded */
				(*lm_set_nlm_status)(lm_global_nlmid,
				    FLK_NLM_SHUTTING_DOWN);
			} else {
				/*
				 * Invoke LLM directly at this node.
				 */
				flk_set_lockmgr_status(FLK_WAKEUP_SLEEPERS);
			}
		}

		/*
		 * if there are no outstanding requests, then the lock manager
		 * is completely down.  Mark it as so and finish the shut down
		 * process
		 */
		if (lm->lm_num_outstanding == 0) {
			/*
			 * Clustering:
			 * Is pxfs server module loaded?
			 *
			 * We avoid setting the lock manager (server) state
			 * inside non-global zones and the following check
			 * needs to be changed when cluster supports NFS
			 * server in non-global zones.
			 */
			if (lm_set_nlm_status && (GLOBAL_ZONEID ==
			    getzoneid())) {	/* yes */
				(*lm_set_nlm_status)(lm_global_nlmid,
				    FLK_NLM_DOWN);
			} else {
				/* no, so invoke LLM directly at this node */
				flk_set_lockmgr_status(FLK_LOCKMGR_DOWN);
			}

			mutex_exit(&lm->lm_lock);
			lm_free_tables(lm);
			mutex_enter(&lm->lm_lock);
			lm->lm_server_status = LM_DOWN;
			cv_broadcast(&lm->lm_status_cv);
			mutex_exit(&lm->lm_lock);
		} else {
			mutex_exit(&lm->lm_lock);
		}
	} else {
		mutex_exit(&lm->lm_lock);
	}
}

/*
 * Try to free all the server-side dynamically allocated tables.
 * Some tables may be shared with the client-side code, so it may not be safe
 * to completely free them up.
 */
static void
lm_free_tables(lm_globals_t *lm)
{
#ifdef DEBUG
	/*
	 * Like all ASSERTs, these only come into play for DEBUG
	 * kernels.  The mutexing is only done to appease warlock.
	 */
	mutex_enter(&lm->lm_lock);
	ASSERT(lm->lm_server_status != LM_UP);
	ASSERT(lm->lm_numconfigs == 0);
	ASSERT(lm->lm_num_outstanding == 0);
	mutex_exit(&lm->lm_lock);
#endif

	lm_free_xprt_map(lm);
	lm_free_all_vnodes(lm);		/* also frees shares */
	lm_flush_clients_mem(lm);
	lm_free_sysid_table(lm);
	lm_async_free(lm);
}

/*
 * Free all the lm_vnodes in the lock manager.
 */
void
lm_free_all_vnodes(lm_globals_t *lm)
{
	struct lm_vnode *lv;
	struct lm_vnode *nextlv = NULL;

	mutex_enter(&lm->lm_vnodes_lock);
	mutex_enter(&lm->lm_lock);

	for (lv = lm->lm_vnodes; lv != NULL; lv = nextlv) {
		nextlv = lv->next;
		/*
		 * Since there are no outstanding requests, there should be
		 * no lm_block entries associated with the lm_vnode.
		 */
		ASSERT(lv->blocked == NULL);

		if (lv->vp) {
			free_shares(lv);
			VN_RELE(lv->vp);
		}
		ASSERT(lv->count == 0);
		kmem_cache_free(lm->lm_vnode_cache, lv);
	}
	lm->lm_vnode_len = 0;
	lm->lm_vnodes = NULL;
	mutex_exit(&lm->lm_lock);
	mutex_exit(&lm->lm_vnodes_lock);
}

/*
 * Free any file-sharing records for the given vnode.
 */
static void
free_shares(struct lm_vnode *lv)
{
	struct shrlock shr;
	int flag = FREAD|FWRITE;

	/*
	 * Release any remote shares by specifying sysid == 0 and pid == 0
	 * and own_len == 0.
	 */
	shr.s_access = 0;
	shr.s_deny = 0;
	shr.s_sysid = 0;
	shr.s_pid = 0;
	shr.s_own_len = 0;
	shr.s_owner = NULL;

	lm_set_nlmid_shr(&shr.s_sysid);

	LM_DEBUG((2, "free_shares", "nlmid = %d (shrlock)",
	    GETNLMID(shr.s_sysid)));

	(void) VOP_SHRLOCK(lv->vp, F_UNSHARE, &shr, flag, CRED(), NULL);
}

/*
 * Determine if this sysid has any share requests outstanding.
 * Loop through all active vnodes testing for shares with this sysid
 */
int
lm_shr_sysid_has_locks(lm_globals_t *lm, int32_t sysid)
{
	int result = 0;
	struct lm_vnode *lv;
	struct shrlock shr;
	int flag = FREAD | FWRITE;
	int error;

	mutex_enter(&lm->lm_vnodes_lock);
	mutex_enter(&lm->lm_lock);
	for (lv = lm->lm_vnodes; lv != NULL; lv = lv->next) {
		if (lv->vp) {
			/*
			 * NOTE: the access field is overloaded as a
			 * boolean flag, 1 == vp has NFS shares, else 0.
			 */
			shr.s_access = 0;
			shr.s_sysid = sysid;
			shr.s_pid = 0;

			lm_set_nlmid_shr(&shr.s_sysid);

			LM_DEBUG((2, "shr_sysid_has_locks",
			    "nlmid = %d, sysid = 0x%x (shrlock)",
			    GETNLMID(shr.s_sysid), shr.s_sysid));

			error = VOP_SHRLOCK(lv->vp, F_HASREMOTELOCKS,
			    &shr, flag, CRED(), NULL);
			LM_DEBUG((2, "shr_sysid_has_locks",
			    "error = %d (shrlock), s_access = %d",
			    error, shr.s_access));

			if (error == 0 && shr.s_access != 0) {
				result = 1;
				break;
			}
		}
	}
	mutex_exit(&lm->lm_lock);
	mutex_exit(&lm->lm_vnodes_lock);

	return (result);
}

/*
 * Save an xprt in lm_xprts.
 *
 * This simply allows us to map a service thread to its unique
 * clone xprt.
 */
struct lm_xprt *
lm_savexprt(lm_globals_t *lm, SVCXPRT *xprt)
{
	struct lm_xprt *lx;

	mutex_enter(&lm->lm_lock);
	for (lx = lm->lm_xprts; lx; lx = lx->next) {
		if (lx->valid == 0) {
			break;
		}
	}

	if (lx == (struct lm_xprt *)NULL) {
		lx = kmem_cache_alloc(lm->lm_xprt_cache, KM_SLEEP);
		lx->next = lm->lm_xprts;
		lm->lm_xprts = lx;
	}
	lx->thread = curthread;
	lx->xprt = xprt;
	lx->valid = 1;

	LM_DEBUG((7, "savexprt", "lm_xprt= %p thread= %p xprt= %p next= %p",
	    (void *)lx, (void *)lx->thread, (void *)lx->xprt,
	    (void *)lx->next));
	mutex_exit(&lm->lm_lock);
	return (lx);
}

/*
 * Fetch lm_xprt corresponding to current thread.
 */
struct lm_xprt *
lm_getxprt(lm_globals_t *lm)
{
	struct lm_xprt *lx;

	mutex_enter(&lm->lm_lock);
	for (lx = lm->lm_xprts; lx; lx = lx->next) {
		if (lx->valid && lx->thread == curthread)
			break;
	}

	if (lx == (struct lm_xprt *)NULL) {	/* should never happen */
		LM_DEBUG((7, "getxprt", "no lm_xprt for thread %p!",
		    (void *)curthread));
	}
	mutex_exit(&lm->lm_lock);

	ASSERT(lx != NULL);
	LM_DEBUG((7, "getxprt", "lx= %p", (void *)lx));
	return (lx);
}

void
lm_relxprt(lm_globals_t *lm, SVCXPRT *xprt)
{
	struct lm_xprt *lx;

	mutex_enter(&lm->lm_lock);
	for (lx = lm->lm_xprts; lx; lx = lx->next) {
		if (lx->valid && lx->thread == curthread && lx->xprt == xprt) {
			break;
		}
	}

	if (lx == (struct lm_xprt *)NULL) {	/* should never happen */
		LM_DEBUG((7, "relxprt", "no lm_xprt for thread %p!",
		    (void *)curthread));
	} else {
		lx->valid = 0;
	}
	mutex_exit(&lm->lm_lock);

	LM_DEBUG((7, "relxprt", "lx= %p", (void *)lx));
}

/*
 * Free all the entries in the xprt table.
 */
void
lm_free_xprt_map(lm_globals_t *lm)
{
	struct lm_xprt *lx;
	struct lm_xprt *nextlx;

	mutex_enter(&lm->lm_lock);
#ifdef lint
	nextlx = (struct lm_xprt *)NULL;
#endif
	for (lx = lm->lm_xprts; lx; lx = nextlx) {
		nextlx = lx->next;
		ASSERT(!lx->valid);
		kmem_cache_free(lm->lm_xprt_cache, lx);
	}

	lm->lm_xprts = NULL;
	mutex_exit(&lm->lm_lock);
}

/*
 * Routines to process the list of blocked lock requests.
 */

/*
 * Initialize an lm_block struct.
 */
void
lm_init_block(lm_globals_t *lm, lm_block_t *new, struct flock64 *flkp,
    struct lm_vnode *lv, netobj *id)
{
	ASSERT(MUTEX_HELD(&lm->lm_lock));

	new->lmb_state = LMB_PENDING;
	new->lmb_no_callback = FALSE;
	new->lmb_flk = flkp;
	new->lmb_id = id;
	new->lmb_vn = lv;
	new->lmb_next = NULL;
}

/*
 * add an item to the list of blocked locked requests for the associated
 * lm_vnode.
 * N.B. This routine must be called with the mutex lm->lm_lock held
 */
void
lm_add_block(lm_globals_t *lm, lm_block_t *new)
{
	struct lm_vnode *lv;

	ASSERT(MUTEX_HELD(&lm->lm_lock));
	ASSERT(new->lmb_state == LMB_PENDING);
	lv = new->lmb_vn;
	new->lmb_next = lv->blocked;
	lv->blocked = new;
}

/*
 * remove an entry from the list of blocked lock requests
 * N.B. This routine must be called with the mutex lm->lm_lock held
 */
void
lm_remove_block(lm_globals_t *lm, lm_block_t *target)
{
	lm_block_t *cur;
	lm_block_t **lmbpp;
	struct lm_vnode *lv;

	ASSERT(MUTEX_HELD(&lm->lm_lock));
	lv = target->lmb_vn;
	lmbpp = &lv->blocked;
	cur = lv->blocked;
	while (cur != (lm_block_t *)NULL) {
		if (cur == target) {
			/* remove it from the list and quit */
			*lmbpp = cur->lmb_next;
			return;
		}
		lmbpp = &(cur->lmb_next);
		cur = cur->lmb_next;
	}
#ifdef DEBUG
	/*
	 * lm_block_lock thought there was an entry for target on
	 * the list but is was not there.
	 */
	cmn_err(CE_PANIC, "lm_remove_block: missing entry in list");
#endif /* DEBUG */
}

/*
 * Check whether an lm_block already exists that either matches or overlaps
 * the given request.
 *
 * req_id is the "unique identifier" for the new request.  For asynchronous
 * calls it should be the same as the request cookie.  For synchronous
 * calls, it should refer to the RPC transaction ID.
 *
 * The caller should hold lm->lm_lock.
 *
 * Return value:
 * LMM_REXMIT	the given request is a retransmission of an earlier
 *		request.  *lmb_resp is set to point to the lm_block for the
 *		earlier request.
 *
 * LMM_FULL	the given request matches an earlier request.  It may be a
 *		retransmission.  *lmb_resp is set to point to the lm_block
 *		for the earlier request.
 *
 * LMM_PARTIAL	the given request overlaps the region of an earlier
 *		request.  *lmb_resp is set to point to the lm_block for
 *		the earlier request.
 *
 * LMM_NONE	the given request does not match any known lm_block.
 *
 */
lm_match_t
lm_find_block(lm_globals_t *lm, struct flock64 *req_flkp,
    struct lm_vnode *req_lv, netobj *req_id, lm_block_t **lmb_resp)
{
	lm_block_t *partial_lmbp = NULL;
	lm_block_t *full_lmbp = NULL;
	lm_block_t *lmbp;
	lm_match_t result;
	int exact;

	ASSERT(MUTEX_HELD(&lm->lm_lock));
	for (lmbp = req_lv->blocked; lmbp != NULL; lmbp = lmbp->lmb_next) {
		/*
		 * Try to find a retransmission.  Keep track of full and
		 * partial matches.
		 */
		if (lm_block_match(lmbp, req_flkp, req_lv, &exact)) {
			if (exact) {
				if (lm_netobj_eq(req_id, lmbp->lmb_id)) {
					*lmb_resp = lmbp;
					return (LMM_REXMIT);
				} else {
					full_lmbp = lmbp;
				}
			} else {
				partial_lmbp = lmbp;
			}
		}
	}

	/*
	 * Didn't find a retransmission.  Did we find a full or partial match?
	 */
	result = LMM_NONE;
	if (full_lmbp != NULL) {
		*lmb_resp = full_lmbp;
		result = LMM_FULL;
	} else if (partial_lmbp != NULL) {
		*lmb_resp = partial_lmbp;
		result = LMM_PARTIAL;
	}

	return (result);
}

/*
 * Release all entries associated with a particular sysid from the
 * lists of blocked lock requests.
 */
void
lm_release_blocks(lm_globals_t *lm, sysid_t target)
{
	lm_block_t *lmbp;
	struct lm_vnode *lv;

	mutex_enter(&lm->lm_vnodes_lock);
	mutex_enter(&lm->lm_lock);
	for (lv = lm->lm_vnodes; lv != NULL; lv = lv->next) {
		for (lmbp = lv->blocked; lmbp != NULL; lmbp = lmbp->lmb_next) {
			if (lmbp->lmb_flk->l_sysid == target) {
				/* disable any pending GRANTED callback RPC */
				lmbp->lmb_no_callback = TRUE;
			}
		}
	}
	mutex_exit(&lm->lm_lock);
	mutex_exit(&lm->lm_vnodes_lock);
}

/*
 * Compare a lock request with an lm_block entry.  The return value
 * is non-zero if the request matches, and zero if the request does
 * not match.  A match is defined as having the same lm_vnode, sysid,
 * and pid, and the same or overlapping region.  *exactp is set to non-zero if
 * the regions match exactly, zero if they just overlap.
 *
 * The region comparisons assume the lock regions are represented
 * as an offset and length from the beginning of the file.
 */
static int
lm_block_match(lm_block_t *target, struct flock64 *flkp, struct lm_vnode *lmvp,
    int *exactp)
{
	u_offset_t end_r;
	u_offset_t start_r;
	u_offset_t end_t;
	u_offset_t start_t;

	if (target->lmb_vn != lmvp)
		return (0);
	if (target->lmb_flk->l_sysid != flkp->l_sysid)
		return (0);
	if (target->lmb_flk->l_pid != flkp->l_pid)
		return (0);

	/*
	 * get starting and ending points of the lock request
	 * MAX_U_OFFSET_T is used for "to EOF."
	 */
	start_r = flkp->l_start;
	if (flkp->l_len == 0) {
		end_r = MAX_U_OFFSET_T;
	} else {
		end_r = start_r + (flkp->l_len - 1);
	}
	start_t = target->lmb_flk->l_start;
	if (target->lmb_flk->l_len == 0) {
		end_t = MAX_U_OFFSET_T;
	} else {
		end_t = start_t + (target->lmb_flk->l_len - 1);
	}

	/*
	 * The only check left is region overlap.  If there is overlap
	 * return that a match was found.
	 *
	 * This comparison for overlap was taken from the OVERLAP
	 * macro in sys/flock_impl.h.
	 */
	if (((start_t <= start_r) && (start_r <= end_t)) ||
	    ((start_r <= start_t) && (start_t <= end_r))) {
		/* overlap */
		*exactp = (start_t == start_r && end_t == end_r);
		return (1);
	} else {
		/* no overlap */
		return (0);
	}
}


/*
 * Search through the lm_vnode's blocked list for requests matching the one
 * specified by <flkp, lmvp> and set the "no callback" flag for each such
 * request.  There is a match if the lmvp is found for the same process id,
 * and any portion of the lock region.
 *
 * The caller of this routine should hold lm->lm_lock.
 */
void
lm_cancel_granted_rxmit(lm_globals_t *lm, struct flock64 *flkp,
    struct lm_vnode *lmvp)
{
	lm_block_t *lmbp;
	int exact;			/* unused */

	ASSERT(MUTEX_HELD(&lm->lm_lock));
	for (lmbp = lmvp->blocked; lmbp != (lm_block_t *)NULL;
	    lmbp = lmbp->lmb_next) {
		if (lm_block_match(lmbp, flkp, lmvp, &exact)) {
			lmbp->lmb_no_callback = TRUE;
		}
	}
}

/*
 * Generate a log message for when we've been told to free the locks for a
 * given host.  "name" is who we were told to free locks for, and "ls" has
 * information about where the request came from.
 */
void
lm_log_free_all(const char *name, const struct lm_sysid *ls)
{
	char ipbuf[INET6_ADDRSTRLEN];
	char *addr = NULL;
	char *buf = NULL;
	int buflen;

	if (strcmp(ls->config.knc_protofmly, NC_INET) == 0) {
		struct sockaddr_in *v4addr =
		    (struct sockaddr_in *)ls->addr.buf;

		addr = inet_ntop(AF_INET, (void *)&v4addr->sin_addr, ipbuf,
		    sizeof (ipbuf));
		if (addr == NULL)
			addr = "UnknownAddress";
	} else if (strcmp(ls->config.knc_protofmly, NC_INET6) == 0) {
		struct sockaddr_in6 *v6addr =
		    (struct sockaddr_in6 *)ls->addr.buf;

		addr = inet_ntop(AF_INET6, (void *)&v6addr->sin6_addr, ipbuf,
		    sizeof (ipbuf));
		if (addr == NULL)
			addr = "UnknownAddress";
	} else {
		const struct netbuf *nbp = &ls->addr;
		int prefixlen;

		/* print the netbuf as a series of hex characters */
		buflen = strlen(ls->config.knc_protofmly) +
		    strlen(": ") + (2 * nbp->len) + 1;
		buf = kmem_alloc(buflen, KM_SLEEP);
		(void) strcpy(buf, ls->config.knc_protofmly);
		(void) strcat(buf, ": ");
		prefixlen = strlen(buf);
		format_netbuf(buf + prefixlen, buflen - prefixlen, nbp);
		addr = buf;
	}

	/*
	 * Don't print ls->name.  It can be spoofed, in which case logging
	 * it could do more harm than good.
	 */
	cmn_err(CE_NOTE, "Received NLM_FREE_ALL (%s) from %s", name, addr);

	if (buf != NULL) {
		kmem_free(buf, buflen);
	}
}

/*
 * Format nbp->addr as a string of hex digits.
 */

static void
format_netbuf(char *buf, int buflen, const struct netbuf *nbp)
{
	uint_t i;
	int nchar;

	for (i = 0; i < nbp->len && buflen > 0; i++) {
		unsigned char c = ((unsigned char *)nbp->buf)[i];

		nchar = snprintf(buf, buflen, "%02x", c);
		buf += nchar;
		buflen -= nchar;
	}
}

#ifdef DEBUG

/*
 * Dump the lm_block list for the given lm_vnode.  The caller should hold
 * lm->lm_lock.
 */
void
lm_dump_block(lm_globals_t *lm, struct lm_vnode *lv)
{
	lm_block_t *lmbp;
	uint_t id;

	ASSERT(MUTEX_HELD(&lm->lm_lock));
	for (lmbp = lv->blocked; lmbp != NULL; lmbp = lmbp->lmb_next) {
		if (lmbp->lmb_id->n_bytes != NULL)
			id = *(uint_t *)(lmbp->lmb_id->n_bytes);
		else
			id = 0;
#ifdef	_LP64
		printf("%p: [%d %s [off=%lu len=%lu type=%d pid=%d] %u]\n",
		    (void *)lmbp, lmbp->lmb_state,
		    (lmbp->lmb_no_callback ? "X" : " "),
		    lmbp->lmb_flk->l_start, lmbp->lmb_flk->l_len,
		    lmbp->lmb_flk->l_type, lmbp->lmb_flk->l_pid, id);
#else	/* _LP64 */
		printf("%p: [%d %s [off=%llu len=%llu type=%d pid=%d] %u]\n",
		    (void *)lmbp, lmbp->lmb_state,
		    (lmbp->lmb_no_callback ? "X" : " "),
		    lmbp->lmb_flk->l_start, lmbp->lmb_flk->l_len,
		    lmbp->lmb_flk->l_type, lmbp->lmb_flk->l_pid, id);
#endif	/* _LP64 */
	}
}

#endif /* DEBUG */

/*
 * Handle the unsharing of a directory/filesystem.
 */
void
lm_unexport(struct exportinfo *exi)
{
	fsid_t *fsidp = &exi->exi_fsid;
	struct lm_vnode *lv;
	lm_globals_t *lm;

	lm = zone_getspecific(lm_zone_key, curproc->p_zone);

	/*
	 * Recheck the lm_vnodes to see if any are still in use.  If the
	 * only remote locks are via the lock manager, this would normally
	 * be a no-op.  But if other modules (e.g., NFSv4) had held a lock
	 * (or share reservation), lm_rel_vnode() would not release the
	 * vnode while that lock existed.  And those other modules don't
	 * know to notify the lock manager when they've released the last
	 * lock.  So we clean up here.
	 */
	mutex_enter(&lm->lm_vnodes_lock);
	for (lv = lm->lm_vnodes; lv != NULL; lv = lv->next) {
		mutex_enter(&lm->lm_lock);
		if (lv->vp == NULL ||
		    !EQFSID(fsidp, &lv->vp->v_vfsp->vfs_fsid)) {
			mutex_exit(&lm->lm_lock);
			continue;
		}
		lv->count++;
		mutex_exit(&lm->lm_lock);
		lm_rel_vnode(lm, lv);
	}
	mutex_exit(&lm->lm_vnodes_lock);
}
