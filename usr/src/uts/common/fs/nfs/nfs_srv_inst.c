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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Lock ordering:
 * (1) rfs_globals_t	rg_lock
 * (2) rfs_zone_t	rz_lock
 * (3) rfs_inst_t	ri_lock
 */
#include <nfs/nfs_srv_inst_impl.h>
#include <nfs/export.h>
#include <nfs/nfs_log.h>
#include <nfs/nfs4_fsh.h>
#include <sys/cladm.h>

static rfs_inst_t *rfs_inst_create(char *, rfs_zone_t *);
static void rfs_inst_cold_to_stopped(rfs_inst_t *);
static void rfs_inst_offline(rfs_inst_t *);
static void rfs_inst_run(rfs_inst_t *);
static int rfs_inst_state_trylock(rfs_inst_t *);
static void rfs_inst_state_unlock(rfs_inst_t *);
static void rfs_inst_v4_enable(rfs_inst_t *, int);
static void rfs_inst_v4_disable(rfs_inst_t *);
static void rfs_inst_pool_unregister(void *);
static void rfs_inst_pool_shutdown(void *);
static rfs_inst_t *rfs_inst_any(rfs_zone_t *);
static void rfs_inst_destroy(rfs_inst_t *);
static void rfs_inst_pool_shutdown(void *);
static rfs_zone_t *rfs_zone_create(zone_t *);
static void rfs_zone_enable(rfs_zone_t *);
static void rfs_zone_destroy(rfs_zone_t *);

rfs_globals_t rfs;

#define	RFS_INST_CANSTOP(p) \
	(((p)->ri_state == RFS_INST_OFFLINE) && \
	((p)->ri_export_cnt == 0) && \
	(p)->ri_rzone->rz_shutting_down)

void
rfs_zone_uniqstr(rfs_zone_t *rzp, char *base_str, char *out_str,
    int out_buf_len)
{
	ASSERT(out_buf_len >= RFS_UNIQUE_BUFLEN);
	ASSERT(strlen(base_str) <= RFS_ZONE_UNIQUE_BASELEN);
	(void) snprintf(out_str, RFS_UNIQUE_BUFLEN, "%s%s", base_str,
	    rzp->rz_unique_suf);
}

void
rfs_inst_uniqstr(rfs_inst_t *rip, char *base_str, char *out_str,
    int out_buf_len)
{
	ASSERT(out_buf_len >= RFS_UNIQUE_BUFLEN);
	ASSERT(strlen(base_str) <= RFS_INST_UNIQUE_BASELEN);
	(void) snprintf(out_str, RFS_UNIQUE_BUFLEN, "%s%s", base_str,
	    rip->ri_unique_suf);
}

static rfs_inst_t *
rfs_inst_create(char *inst_name, rfs_zone_t *rzp)
{
	rfs_inst_t *rip;

	rip = kmem_zalloc(sizeof (rfs_inst_t), KM_SLEEP);
	mutex_init(&rip->ri_lock, NULL, MUTEX_DEFAULT, NULL);
	rip->ri_refcount = 1;
	rip->ri_unregistered = 1;
	rip->ri_state = RFS_INST_COLD;
	cv_init(&rip->ri_state_cv, NULL, CV_DEFAULT, NULL);
	rw_init(&rip->ri_nm.rn_export_rwlock, NULL, RW_DEFAULT, NULL);
	rw_init(&rip->ri_v4.r4_enabled_rwlock, NULL, RW_DEFAULT, NULL);
	rip->ri_hanfs_id = NODEID_UNKNOWN;
	rfs_zone_hold(rzp);
	rip->ri_rzone = rzp;

	rip->ri_name_len = strlen(inst_name);
	rip->ri_name = kmem_alloc(rip->ri_name_len + 1, KM_SLEEP);
	(void) strcpy(rip->ri_name, inst_name);
	return (rip);
}

static void
rfs_inst_offline(rfs_inst_t *rip)
{
	if (rfs_inst_state_trylock(rip) == 0)
		return;
	if ((rip->ri_state == RFS_INST_STOPPED) ||
	    (rip->ri_state == RFS_INST_OFFLINE))
		goto out;

	if (rip->ri_state == RFS_INST_COLD) {
		nfs_exportinit(rip);
		nfsauth_init(rip);
		rfs_srvrinit(rip);
		rfs4_srvrinit(rip);
		fs_hash_init(rip);
	} else if (rip->ri_state == RFS_INST_RUNNING) {
		if (rip->ri_quiesce) {
			/* reset DSS state, for subsequent warm restart */
			rip->ri_v4.r4_dss_numnewpaths = 0;
			rip->ri_v4.r4_dss_newpaths = NULL;
			zcmn_err(getzoneid(), CE_NOTE,
			    "nfs_server: server is now quiesced; "
			    "NFSv4 state has been preserved");
		} else {
			rfs_inst_v4_disable(rip);
		}
	}

	mutex_enter(&rip->ri_lock);
	rip->ri_state = RFS_INST_OFFLINE;
	mutex_exit(&rip->ri_lock);
out:
	rfs_inst_state_unlock(rip);
}

static void
rfs_inst_run(rfs_inst_t *rip)
{
	if (rfs_inst_state_trylock(rip) == 0)
		return;

	if (rip->ri_state != RFS_INST_OFFLINE)
		goto out;

	if (svc_pool_control(NFS_SVCPOOL_ID, SVCPSET_HANDLE, NULL, rip))
		goto out;

	if (svc_pool_control(NFS_SVCPOOL_ID, SVCPSET_ASYNCREPLY, NULL, rip))
		goto out;

	if (rip->ri_quiesce) {
		zcmn_err(getzoneid(), CE_NOTE,
		    "nfs_server: server was previously quiesced; "
		    "existing state will be re-used");

		/*
		 * HA-NFSv4: this is also the signal
		 * that a Resource Group failover has
		 * occurred.
		 */
		if (cluster_bootflags & CLUSTER_BOOTED)
			hanfsv4_failover(rip);
	}

	mutex_enter(&rip->ri_lock);
	rip->ri_state = RFS_INST_RUNNING;
	rip->ri_unregistered = 0;
	rip->ri_quiesce = 0;
	rip->ri_state_active = 1;
	rip->ri_refcount++;
	mutex_exit(&rip->ri_lock);

	if (svc_pool_control(NFS_SVCPOOL_ID,
	    SVCPSET_UNREGISTER_PROC, rfs_inst_pool_unregister, rip) ||
	    svc_pool_control(NFS_SVCPOOL_ID,
	    SVCPSET_SHUTDOWN_PROC, rfs_inst_pool_shutdown, rip)) {
		rfs_inst_pool_unregister(rip);
		rfs_inst_pool_shutdown(rip);
	}
out:
	rfs_inst_state_unlock(rip);
}

static void
rfs_inst_stop(rfs_inst_t *rip)
{
	rfs_zone_t *rzp;

	if (rfs_inst_state_trylock(rip) == 0)
		return;

	if (!RFS_INST_CANSTOP(rip)) {
		rfs_inst_state_unlock(rip);
		return;
	}

	mutex_enter(&rip->ri_lock);
	rip->ri_state = RFS_INST_STOPPED;
	mutex_exit(&rip->ri_lock);

	rfs_inst_v4_disable(rip);

	rfs4_srvrfini(rip);
	rfs_srvrfini(rip);
	nfsauth_fini(rip);
	nfs_exportfini(rip);
	fs_hash_destroy(rip);

	rfs_inst_state_unlock(rip);

	rzp = rip->ri_rzone;
	mutex_enter(&rzp->rz_lock);
	list_remove(&rzp->rz_instances, rip);
	rzp->rz_inst_cnt--;
	mutex_exit(&rzp->rz_lock);
}

static void
rfs_inst_destroy(rfs_inst_t *rip)
{
	ASSERT(rip->ri_refcount == 1);
	ASSERT(rip->ri_state == RFS_INST_STOPPED);

	rw_destroy(&rip->ri_nm.rn_export_rwlock);
	rw_destroy(&rip->ri_v4.r4_enabled_rwlock);
	cv_destroy(&rip->ri_state_cv);
	mutex_destroy(&rip->ri_lock);
	kmem_free(rip->ri_name, rip->ri_name_len + 1);
	rfs_zone_rele(rip->ri_rzone);
	kmem_free(rip, sizeof (*rip));
}

void
rfs_inst_hold(rfs_inst_t *rip)
{
	mutex_enter(&rip->ri_lock);
	rip->ri_refcount++;
	mutex_exit(&rip->ri_lock);
}

void
rfs_inst_rele(rfs_inst_t *rip)
{
	mutex_enter(&rip->ri_lock);
	ASSERT(rip->ri_refcount > 0);
	if (rip->ri_refcount == 1 && rip->ri_state == RFS_INST_STOPPED) {
		mutex_exit(&rip->ri_lock);
		mutex_enter(&rfs.rg_lock);
		mutex_enter(&rip->ri_lock);
		ASSERT(rip->ri_state == RFS_INST_STOPPED);
		if (rip->ri_refcount > 1) {
			rip->ri_refcount--;
			mutex_exit(&rip->ri_lock);
			mutex_exit(&rfs.rg_lock);
			return;
		}
		ASSERT(rip->ri_refcount == 1);
		mutex_exit(&rip->ri_lock);
		list_remove(&rfs.rg_instances, rip);
		ASSERT(rfs.rg_inst_cnt > 0);
		rfs.rg_inst_cnt--;
		mutex_exit(&rfs.rg_lock);
		rfs_inst_destroy(rip);
	} else {
		rip->ri_refcount--;
		mutex_exit(&rip->ri_lock);
	}
}

int
rfs_inst_active_tryhold(rfs_inst_t *rip, int online)
{
	int ret = 0;
	int rval;

	mutex_enter(&rip->ri_lock);

	while (rip->ri_state_locked != NULL) {
		rval = cv_wait_sig(&rip->ri_state_cv, &rip->ri_lock);
		if (!rval)
			goto out;
	}

	if ((online == FALSE) ||
	    ((rip->ri_state == RFS_INST_RUNNING) && !rip->ri_unregistered)) {
		rip->ri_state_active++;
		rip->ri_refcount++;
		ret = 1;
	}

out:
	mutex_exit(&rip->ri_lock);
	return (ret);
}

void
rfs_inst_active_rele(rfs_inst_t *rip)
{
	mutex_enter(&rip->ri_lock);
	ASSERT(rip->ri_state_active);
	ASSERT(rip->ri_state_locked == NULL);
	rip->ri_state_active--;
	if (rip->ri_state_active == 0)
		cv_broadcast(&rip->ri_state_cv);
	mutex_exit(&rip->ri_lock);
	if (RFS_INST_CANSTOP(rip))
		rfs_inst_stop(rip);
	rfs_inst_rele(rip);
}

static int
rfs_inst_state_trylock(rfs_inst_t *rip)
{
	mutex_enter(&rip->ri_lock);
	for (;;) {
		if (!rip->ri_unregistered) {
			ASSERT(rip->ri_state == RFS_INST_RUNNING);
			mutex_exit(&rip->ri_lock);
			return (0);
		}
		if (rip->ri_state_active == 0 && rip->ri_state_locked == NULL)
			break;
		cv_wait(&rip->ri_state_cv, &rip->ri_lock);
	}
	rip->ri_state_locked = curthread;
	mutex_exit(&rip->ri_lock);
	return (1);
}

static void
rfs_inst_state_unlock(rfs_inst_t *rip)
{
	ASSERT(rip->ri_state_locked == curthread);
	mutex_enter(&rip->ri_lock);
	rip->ri_state_locked = NULL;
	cv_broadcast(&rip->ri_state_cv);
	mutex_exit(&rip->ri_lock);
}

static void
rfs_inst_v4_enable(rfs_inst_t *rip, int deleg)
{
	ASSERT(rip->ri_state_active);
	rw_enter(&rip->ri_v4.r4_enabled_rwlock, RW_WRITER);

	if (!rip->ri_v4.r4_enabled) {
		/*
		 * Check to see if delegation is to be
		 * enabled at the server
		 */
		if (deleg != FALSE)
			rfs4_set_deleg_policy(rip, SRV_NORMAL_DELEGATE);

		rfs4_state_init(rip);
		rip->ri_v4.r4_drc = rfs4_init_drc(rip->ri_v4.r4_drc_max,
		    rip->ri_v4.r4_drc_hash);
		rip->ri_v4.r4_enabled = 1;
	}

	rw_exit(&rip->ri_v4.r4_enabled_rwlock);
}

static void
rfs_inst_v4_disable(rfs_inst_t *rip)
{
	if (rip->ri_v4.r4_enabled) {
		rfs4_set_deleg_policy(rip, SRV_NEVER_DELEGATE);
		rfs4_state_fini(rip);
		rfs4_fini_drc(rip->ri_v4.r4_drc);
		rip->ri_v4.r4_drc = NULL;
		rip->ri_v4.r4_enabled = 0;
	}
}

int
rfs_inst_start(int maxvers, int deleg)
{
	rfs_inst_t *rip;
	int err;

	rip = rfs_inst_find(TRUE);
	if (rip == NULL)
		return (ENOENT);

	if (maxvers >= NFS_V4)
		rfs_inst_v4_enable(rip, deleg);

	rfs_inst_hold(rip);
	rfs_inst_active_rele(rip);

	rfs_inst_run(rip);

	err = (rip->ri_state == RFS_INST_RUNNING) ? 0 : ENOENT;
	rfs_inst_rele(rip);
	return (err);
}

void
rfs_inst_quiesce(void)
{
	rfs_inst_t *rip;

	rip = rfs_inst_find(FALSE);
	if (rip) {
		rip->ri_quiesce = 1;
		rfs_inst_active_rele(rip);
	}
}

static void
rfs_inst_pool_unregister(void *cb_arg)
{
	rfs_inst_t *rip = cb_arg;

	ASSERT(rip != NULL);
	mutex_enter(&rip->ri_lock);
	rip->ri_unregistered = 1;
	mutex_exit(&rip->ri_lock);
}

/*
 * Will be called at the point the server pool is being destroyed.
 * All transports have been closed and no service threads exist.
 */
static void
rfs_inst_pool_shutdown(void *cb_arg)
{
	rfs_inst_t *rip = cb_arg;

	ASSERT(rip != NULL);
	rfs_inst_hold(rip);
	ASSERT(rip->ri_unregistered && (rip->ri_state == RFS_INST_RUNNING));
	rfs_inst_active_rele(rip);
	rfs_inst_offline(rip);
	if (RFS_INST_CANSTOP(rip))
		rfs_inst_stop(rip);
	rfs_inst_rele(rip);
}

static rfs_inst_t *
rfs_inst_any(rfs_zone_t *rzp)
{
	rfs_inst_t *rip;

	ASSERT(mutex_owned(&rzp->rz_lock));

	for (rip = list_head(&rzp->rz_instances);
	    rip != NULL;
	    rip = list_next(&rzp->rz_instances, rip)) {
		if (rip->ri_state != RFS_INST_STOPPED) {
			rfs_inst_hold(rip);
			break;
		}
	}

	return (rip);
}

rfs_inst_t *
rfs_inst_find(int create)
{
	rfs_zone_t *rzp;
	rfs_inst_t *rip = NULL;
	rfs_inst_t *crip;
	int rval;

	rzp = rfs_zone_find(curproc->p_zone->zone_id, create);
	if (rzp == NULL)
		return (NULL);

	while (!rzp->rz_shutting_down) {
		mutex_enter(&rzp->rz_lock);
		rip = rfs_inst_any(rzp);
		mutex_exit(&rzp->rz_lock);

		if (rip) {
			mutex_enter(&rip->ri_lock);
			if ((rip->ri_state_locked != NULL) ||
			    (rip->ri_state == RFS_INST_COLD) ||
			    (rip->ri_state == RFS_INST_RUNNING &&
			    rip->ri_unregistered)) {
				rval = cv_wait_sig(&rip->ri_state_cv,
				    &rip->ri_lock);
				mutex_exit(&rip->ri_lock);
				rfs_inst_rele(rip);
				rip = NULL;
				if (!rval)
					break;
				continue;
			}
			mutex_exit(&rip->ri_lock);
		} else if (create) {
			rip = rfs_inst_create(RFS_INST_NAME_DFL, rzp);
			mutex_enter(&rfs.rg_lock);
			list_insert_tail(&rfs.rg_instances, rip);
			rfs.rg_inst_cnt++;
			mutex_exit(&rfs.rg_lock);

			mutex_enter(&rzp->rz_lock);
			crip = rfs_inst_any(rzp);
			if (crip == NULL) {
				rip->ri_id = rzp->rz_next_rfsinst_id++;
				ASSERT(rip->ri_id == 0);
				(void) snprintf(rip->ri_unique_suf,
				    sizeof (rip->ri_unique_suf), "_%X_%X",
				    rzp->rz_zonep->zone_id, rip->ri_id);
				list_insert_tail(&rzp->rz_instances, rip);
				rzp->rz_inst_cnt++;
			}
			mutex_exit(&rzp->rz_lock);

			if (crip) {
				ASSERT(crip->ri_rzone == rzp);
				rfs_inst_rele(crip);
				rfs_inst_cold_to_stopped(rip);
				rfs_inst_rele(rip);
				rip = NULL;
				continue;
			}

			rfs_inst_offline(rip);
			if (rip->ri_state != RFS_INST_OFFLINE) {
				rfs_inst_cold_to_stopped(rip);
				rfs_inst_rele(rip);
				rip = NULL;
				break;
			}
		} else {
			break;
		}

		if (rfs_inst_active_tryhold(rip, FALSE) == 0) {
			rfs_inst_rele(rip);
			rip = NULL;
			break;
		}
		if ((rip->ri_state != RFS_INST_RUNNING) &&
		    (rip->ri_state != RFS_INST_OFFLINE)) {
			rfs_inst_active_rele(rip);
			rfs_inst_rele(rip);
			rip = NULL;
			break;
		}
		rfs_inst_rele(rip);
		break;
	}

	rfs_zone_rele(rzp);
	return (rip);
}

static void
rfs_inst_cold_to_stopped(rfs_inst_t *rip)
{
	if (rfs_inst_state_trylock(rip) == 0)
		return;

	ASSERT(rip->ri_state == RFS_INST_COLD);
	mutex_enter(&rip->ri_lock);
	rip->ri_state = RFS_INST_STOPPED;
	mutex_exit(&rip->ri_lock);

	rfs_inst_state_unlock(rip);
}

rfs_inst_t *
rfs_inst_svcreq_to_rip(struct svc_req *req)
{
	rfs_inst_t *rip = SVC_REQ2PHANDLE(req);

	ASSERT(rip);
	ASSERT(rip->ri_state == RFS_INST_RUNNING);
	ASSERT(rip->ri_state_active);

	return (rip);
}

rfs_inst_t *
rfs_inst_svcxprt_to_rip(SVCXPRT *xprt)
{
	rfs_inst_t *rip = SVC_XPRT2PHANDLE(xprt);

	ASSERT(rip);
	ASSERT(rip->ri_state == RFS_INST_RUNNING);
	ASSERT(rip->ri_state_active);

	return (rip);
}

zone_t *
rfs_zoneid_to_zone(zoneid_t zoneid)
{
	zone_t *zonep;

	if (curproc->p_zone->zone_id == zoneid) {
		zonep = curproc->p_zone;
		zone_hold(zonep);
	} else
		zonep = zone_find_by_id(zoneid);
	return (zonep);
}

rfs_zone_t *
rfs_zone_find(zoneid_t zoneid, int create)
{
#ifdef DEBUG
	rfs_zone_t *crzp;
#endif
	rfs_zone_t *rzp = NULL;
	rfs_zone_t *new_rzp = NULL;
	zone_t *zonep;
	int rval;

	if ((zonep = rfs_zoneid_to_zone(zoneid)) == NULL)
		return (NULL);

	for (;;) {
		mutex_enter(&rfs.rg_lock);
		rzp = zone_getspecific(rfs.rg_rfszone_key, zonep);
		if (rzp != NULL) {
			ASSERT(rzp->rz_zonep == zonep);
			rfs_zone_hold(rzp);
			goto out;
		}

		if (create) {
			if (new_rzp == NULL) {
				mutex_exit(&rfs.rg_lock);
				new_rzp = rfs_zone_create(zonep);
				continue;
			}
#ifdef DEBUG
			for (crzp = list_head(&rfs.rg_zones); crzp != NULL;
			    crzp = list_next(&rfs.rg_zones, crzp)) {
				ASSERT(crzp->rz_zonep != zonep);
			}
#endif
			rval = zone_setspecific(rfs.rg_rfszone_key, zonep,
			    new_rzp);
			ASSERT(rval == 0);
			rzp = new_rzp;
			new_rzp = NULL;
			list_insert_head(&rfs.rg_zones, rzp);
			rfs.rg_zone_cnt++;
			if (zone_status_get(zonep) > ZONE_IS_RUNNING)
				rzp->rz_shutting_down = 1;
		}
		break;
	}
out:
	mutex_exit(&rfs.rg_lock);
	if (new_rzp != NULL)
		rfs_zone_destroy(new_rzp);
	if (rzp != NULL) {
		mutex_enter(&rzp->rz_lock);
		if (! rzp->rz_enabled)
			rfs_zone_enable(rzp);
		mutex_exit(&rzp->rz_lock);
	}
	zone_rele(zonep);
	return (rzp);
}

static void
rfs_zone_enable(rfs_zone_t *rzp)
{
	ASSERT(MUTEX_HELD(&rzp->rz_lock));
	ASSERT(rzp->rz_enabled == 0);

	rzp->rz_name_len = strlen(rzp->rz_zonep->zone_name);
	rzp->rz_name = kmem_alloc(rzp->rz_name_len + 1, KM_SLEEP);
	(void) strcpy(rzp->rz_name, rzp->rz_zonep->zone_name);
	(void) snprintf(rzp->rz_unique_suf, sizeof (rzp->rz_unique_suf), "_%X",
	    rzp->rz_zonep->zone_id);

	list_create(&rzp->rz_instances, sizeof (rfs_inst_t),
	    offsetof(rfs_inst_t, ri_znode));
	mutex_init(&rzp->rz_mountd_lock, NULL, MUTEX_DEFAULT, NULL);
	nfslog_init(rzp);
	rzp->rz_enabled = 1;
}

static rfs_zone_t *
rfs_zone_create(zone_t *zonep)
{
	rfs_zone_t *rzp;

	rzp = kmem_zalloc(sizeof (rfs_zone_t), KM_SLEEP);
	mutex_init(&rzp->rz_lock, NULL, MUTEX_DEFAULT, NULL);
	rzp->rz_refcount = 1;
	rzp->rz_zonep = zonep;
	zone_init_ref(&rzp->rz_zone_ref);
	zone_hold_ref(zonep, &rzp->rz_zone_ref, ZONE_REF_NFSSRV);
	rzp->rz_nfsstats = zone_getspecific(nfsstat_zone_key, zonep);
	return (rzp);
}

static void
rfs_zone_destroy(rfs_zone_t *rzp)
{
	ASSERT(rzp->rz_refcount == 1);
	ASSERT(rzp->rz_inst_cnt == 0);

	if (rzp->rz_enabled) {
		nfslog_fini(rzp);
		if (rzp->rz_mountd_dh)
			door_ki_rele(rzp->rz_mountd_dh);
		mutex_destroy(&rzp->rz_mountd_lock);
		list_destroy(&rzp->rz_instances);
		kmem_free(rzp->rz_name, rzp->rz_name_len + 1);
	}
	mutex_destroy(&rzp->rz_lock);
	zone_rele_ref(&rzp->rz_zone_ref, ZONE_REF_NFSSRV);
	kmem_free(rzp, sizeof (*rzp));
}


/*
 * nfs server's zone shutdown callback
 * rfs_zone_t marked as shutting down
 * all quiesced server instances are destroyed
 * rfs_zone_t is destroyed when refcount goes to 0
 */
/* ARGSUSED */
void
rfs_zone_shutdown(zoneid_t zoneid, void *pdata)
{
	rfs_zone_t *rzp;
	rfs_inst_t *rip;

	rzp = rfs_zone_find(zoneid, FALSE);
	if (rzp == NULL)
		return;

	mutex_enter(&rzp->rz_lock);
	rzp->rz_shutting_down = 1;

	/*
	 * assume 1 instance per zone for now.
	 */
	if ((rip = list_head(&rzp->rz_instances)) != NULL) {
		/*
		 * hold rip so that it can't be destroyed
		 * by a competing thread immediately after unlock.
		 */
		rfs_inst_hold(rip);
		mutex_exit(&rzp->rz_lock);

		if (rfs_inst_active_tryhold(rip, FALSE)) {
			if ((rip->ri_state == RFS_INST_RUNNING) ||
			    (rip->ri_state == RFS_INST_OFFLINE)) {
				/* force remove any shares */
				rfs_inst_unexport_all(rip);
			}
			rfs_inst_active_rele(rip);
		}

		/*
		 * Quiesced instances must be destroyed here because their
		 * SVCPOOLs (and associated zone shutdown callbacks) were
		 * destroyed at quiesce time.
		 */
		if (RFS_INST_CANSTOP(rip))
			rfs_inst_stop(rip);

		rfs_inst_rele(rip);
	} else
		mutex_exit(&rzp->rz_lock);
	rfs_zone_rele(rzp);
}

void
rfs_zone_hold(rfs_zone_t *rzp)
{
	mutex_enter(&rzp->rz_lock);
	rzp->rz_refcount++;
	mutex_exit(&rzp->rz_lock);
}

void
rfs_zone_rele(rfs_zone_t *rzp)
{
#ifdef DEBUG
	rfs_zone_t *crzp;
#endif
	int rval;

	mutex_enter(&rzp->rz_lock);
	ASSERT(rzp->rz_refcount > 0);
	if (rzp->rz_refcount == 1 && rzp->rz_shutting_down) {
		mutex_exit(&rzp->rz_lock);
		mutex_enter(&rfs.rg_lock);
		mutex_enter(&rzp->rz_lock);
		if (rzp->rz_refcount > 1) {
			rzp->rz_refcount--;
			mutex_exit(&rzp->rz_lock);
			mutex_exit(&rfs.rg_lock);
			return;
		}
		ASSERT(rzp->rz_refcount == 1);
		ASSERT(rzp->rz_inst_cnt == 0);
		ASSERT(rfs.rg_zone_cnt > 0);
		list_remove(&rfs.rg_zones, rzp);
		rfs.rg_zone_cnt--;
#ifdef DEBUG
		if (rzp->rz_zonep != NULL) {
			crzp = zone_getspecific(rfs.rg_rfszone_key,
			    rzp->rz_zonep);
			ASSERT(crzp == rzp);
		}
#endif
		if (rzp->rz_zonep != NULL) {
			rval = zone_setspecific(rfs.rg_rfszone_key,
			    rzp->rz_zonep, NULL);
			ASSERT(rval == 0);
		}
		mutex_exit(&rzp->rz_lock);
		mutex_exit(&rfs.rg_lock);
		rfs_zone_destroy(rzp);
	} else {
		rzp->rz_refcount--;
		mutex_exit(&rzp->rz_lock);
	}
}
