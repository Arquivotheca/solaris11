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
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/strsun.h>
#include <sys/sdt.h>
#include <sys/mac.h>
#include <sys/mac_impl.h>
#include <sys/mac_client_impl.h>
#include <sys/mac_cpu_impl.h>
#include <sys/mac_stat.h>
#include <sys/dls.h>
#include <sys/dls_impl.h>
#include <sys/ethernet.h>
#include <sys/cpupart.h>
#include <sys/pool.h>
#include <sys/pool_pset.h>
#include <sys/vlan.h>
#include <sys/zone.h>
#include <inet/ip.h>
#include <inet/ip6.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/sctp.h>

static flow_ops_t	flow_l2_ops;

typedef struct {
	const char	*fse_name;
	uint_t		fse_offset;
} flow_stats_entry_t;

#define	FS_OFF(f)	(offsetof(flow_stats_t, f))
static flow_stats_entry_t flow_stats_list[] = {
	{"rbytes",	FS_OFF(fs_ibytes)},
	{"ipackets",	FS_OFF(fs_ipackets)},
	{"ierrors",	FS_OFF(fs_ierrors)},
	{"idrops",	FS_OFF(fs_idrops)},
	{"idropbytes",	FS_OFF(fs_idropbytes)},
	{"obytes",	FS_OFF(fs_obytes)},
	{"opackets",	FS_OFF(fs_opackets)},
	{"oerrors",	FS_OFF(fs_oerrors)},
	{"odrops",	FS_OFF(fs_odrops)},
	{"odropbytes",	FS_OFF(fs_odropbytes)}
};
#define	FS_SIZE	(sizeof (flow_stats_list) / sizeof (flow_stats_entry_t))

/*
 * Checks whether a flow mask is legal.
 */
static flow_tab_info_t	*mac_flow_tab_info_get(flow_mask_t);

static void
flow_stat_init(kstat_named_t *knp)
{
	int	i;

	for (i = 0; i < FS_SIZE; i++, knp++) {
		kstat_named_init(knp, flow_stats_list[i].fse_name,
		    KSTAT_DATA_UINT64);
	}
}

static int
flow_stat_update(kstat_t *ksp, int rw)
{
	flow_entry_t		*flent = ksp->ks_private;
	kstat_named_t		*knp = ksp->ks_data;
	uint64_t		*statp;
	int			i;
	flow_stats_t		flow_stats = flent->fe_stat;

	if (rw != KSTAT_READ)
		return (EACCES);

	for (i = 0; i < FS_SIZE; i++, knp++) {
		statp = (uint64_t *)
		    ((uchar_t *)&flow_stats + flow_stats_list[i].fse_offset);
		knp->value.ui64 = *statp;
	}
	return (0);
}

static void
flow_stat_create(flow_entry_t *fep)
{
	kstat_t			*ksp1, *ksp2;
	kstat_named_t		*knp1, *knp2;
	uint_t			nstats = FS_SIZE;
	char			kstat_name[MAXFLOWNAMELEN];
	char			buf[ZONENAME_MAX];
	zoneid_t		zoneid;
	zone_t			*zone;
	mac_client_impl_t	*mcip = (mac_client_impl_t *)fep->fe_mcip;

	/*
	 * If this flow belongs to a client that doens't have a unicast
	 * address and uses the underlying link's name skip creating the
	 * stats.
	 */
	if (mcip != NULL &&
	    (mcip->mci_state_flags & MCIS_NO_UNICAST_ADDR) != 0 &&
	    (mcip->mci_state_flags & MCIS_USE_DATALINK_NAME) != 0) {
		return;
	}

	/* first create a kstat for this zone */
	zoneid = fep->fe_flow_desc.fd_zoneid;
	ksp1 = kstat_create_zone("unix", 0, fep->fe_flow_name, "flow",
	    KSTAT_TYPE_NAMED, nstats, 0, zoneid);
	if (ksp1 == NULL)
		return;

	ksp1->ks_update = flow_stat_update;
	ksp1->ks_private = fep;
	if (zoneid != GLOBAL_ZONEID)
		fep->fe_ksp_ngz = ksp1;
	else
		fep->fe_ksp_gz = ksp1;

	knp1 = (kstat_named_t *)ksp1->ks_data;
	flow_stat_init(knp1);
	kstat_install(ksp1);

	/*
	 * If global zone, we don't need to start the ngz kstat here. The ngz
	 * kstat will be set only when the gz dedicate this flow to the ngz.
	 */
	if (zoneid == GLOBAL_ZONEID)
		return;

	/*
	 * Then create a kstat for the global zone to view ngz's flow stat
	 * kstat name should be in format of zone-name/flow-name to avoid kstat
	 * namespace collision.
	 */
	zone = zone_find_by_id(zoneid);
	(void) strlcpy(buf, zone->zone_name, ZONENAME_MAX);
	zone_rele(zone);

	(void) snprintf(kstat_name, sizeof (kstat_name), "%s/%s", buf,
	    fep->fe_flow_name);

	ksp2 = kstat_create_zone("unix", 0, kstat_name, "flow",
	    KSTAT_TYPE_NAMED, nstats, 0, GLOBAL_ZONEID);
	if (ksp2 == NULL)
		return;

	ksp2->ks_update = flow_stat_update;
	ksp2->ks_private = fep;
	fep->fe_ksp_gz = ksp2;

	knp2 = (kstat_named_t *)ksp2->ks_data;
	flow_stat_init(knp2);
	kstat_install(ksp2);
}

void
flow_stat_destroy(flow_entry_t *fep)
{
	if (fep->fe_ksp_gz != NULL) {
		kstat_delete(fep->fe_ksp_gz);
		fep->fe_ksp_gz = NULL;
	}
	if (fep->fe_ksp_ngz != NULL) {
		kstat_delete(fep->fe_ksp_ngz);
		fep->fe_ksp_ngz = NULL;
	}
}

static flow_zone_tab_t *
mac_flow_tab_get(zoneid_t zoneid)
{
	flow_zone_tab_t	*mac_flow_tbl;
	netstack_t	*ns;

	ns = netstack_find_by_zoneid(zoneid);
	if (ns == NULL)
		return (NULL);

	mac_flow_tbl = ns->netstack_flow;
	netstack_rele(ns);
	ASSERT(mac_flow_tbl != NULL);
	ASSERT(mac_flow_tbl->flow_tab_cache != NULL);
	ASSERT(mac_flow_tbl->flow_hash != NULL);
	ASSERT(mac_flow_tbl->flow_cache != NULL);

	return (mac_flow_tbl);
}

int
mac_link_setlinkzid(mac_handle_t mh, zoneid_t zid)
{
	((mac_impl_t *)mh)->mi_zoneid = zid;
	return (0);
}

#define	FLOW_CACHE_NAME_LEN	32

/* ARGSUSED */
void *
mac_flow_zone_init(netstackid_t stackid, netstack_t *ns)
{
	char		cache_name[FLOW_CACHE_NAME_LEN];
	flow_zone_tab_t	*mac_flow_tbl;
	zoneid_t	zoneid;

	mac_flow_tbl = kmem_zalloc(sizeof (*mac_flow_tbl), KM_SLEEP);

	zoneid = netstackid_to_zoneid(stackid);

	mac_flow_tbl->zoneid = zoneid;
	(void) snprintf(cache_name, FLOW_CACHE_NAME_LEN, "flow_tab_cache_%d",
	    zoneid);
	mac_flow_tbl->flow_tab_cache = kmem_cache_create(cache_name,
	    sizeof (flow_tab_t), 0, NULL, NULL, NULL, NULL, NULL, 0);

	(void) snprintf(cache_name, FLOW_CACHE_NAME_LEN, "flow_hash_%d",
	    zoneid);
	mac_flow_tbl->flow_hash = mod_hash_create_extended(cache_name,
	    100, mod_hash_null_keydtor, mod_hash_null_valdtor,
	    mod_hash_bystr, NULL, mod_hash_strkey_cmp, KM_SLEEP);

	(void) snprintf(cache_name, FLOW_CACHE_NAME_LEN, "flow_entry_cache_%d",
	    zoneid);
	mac_flow_tbl->flow_cache = kmem_cache_create(cache_name,
	    sizeof (flow_entry_t), 0, NULL, NULL, NULL, NULL, NULL, 0);
	rw_init(&mac_flow_tbl->flow_tab_lock, NULL, RW_DEFAULT, NULL);

	return (mac_flow_tbl);
}

uint_t
mac_flow_zone_remove(mod_hash_key_t flow_name, mod_hash_val_t *flow_entry,
    void *arg)
{
	uint_t			err;
	datalink_id_t		linkid;
	mac_perim_handle_t	mph;
	flow_zone_tab_t		*mac_flow_tbl = arg;
	flow_entry_t		*flent = (flow_entry_t *)flow_entry;

	linkid = flent->fe_link_id;

	err = mac_perim_enter_by_linkid(linkid, &mph);
	if (err != 0)
		return (err);

	err = mac_flow_lookup_byname(flow_name, &flent, mac_flow_tbl->zoneid);
	if (err != 0) {
		mac_perim_exit(mph);
		return (err);
	}
	FLOW_USER_REFRELE(flent);
	mac_flow_rem_subflow(flent);
	mac_perim_exit(mph);
	flow_stat_destroy(flent);
	kmem_cache_free(mac_flow_tbl->flow_cache, flent);
	return (err);
}

/* ARGSUSED */
void
mac_flow_zone_shutdown(netstackid_t stackid, void *data)
{
	flow_zone_tab_t	*mac_flow_tbl = data;

	mod_hash_walk(mac_flow_tbl->flow_hash, mac_flow_zone_remove,
	    mac_flow_tbl);

	/*
	 * This zone shutdown callback can be called multiple times
	 * so we must clear out the flow_hash entries here as well
	 * to ensure the above mod_hash_walk has no entries to walk
	 * on the next call.
	 */
	mod_hash_clear(mac_flow_tbl->flow_hash);
}

/* ARGSUSED */
void
mac_flow_zone_fini(netstackid_t stackid, void *data)
{
	flow_zone_tab_t	*mac_flow_tbl = data;

	kmem_cache_destroy(mac_flow_tbl->flow_cache);
	kmem_cache_destroy(mac_flow_tbl->flow_tab_cache);
	mod_hash_destroy_hash(mac_flow_tbl->flow_hash);
	rw_destroy(&mac_flow_tbl->flow_tab_lock);
	kmem_free(mac_flow_tbl, sizeof (flow_zone_tab_t));
}

/*
 * Initialize the flow table
 */
void
mac_flow_init()
{
	netstack_register(NS_FLOW, mac_flow_zone_init, mac_flow_zone_shutdown,
	    mac_flow_zone_fini);
}

/*
 * Cleanup and release the flow table
 */
void
mac_flow_fini()
{
	netstack_unregister(NS_FLOW);
}

/*
 * mac_create_flow(): alloc memory from mac flow cache and create a mac flow
 */
int
mac_flow_create(flow_desc_t *fd, mac_resource_props_t *mrp, char *name,
    void *client_cookie, uint_t type, flow_entry_t **flentp, zoneid_t zoneid)
{
	flow_entry_t		*flent = *flentp;
	int			err = 0, res;
	flow_zone_tab_t		*mac_flow_tbl;

	/*
	 * Validate if zone lwp limit exceeds or not
	 */
	mutex_enter(&curproc->p_lock);
	mutex_enter(&curproc->p_zone->zone_nlwps_lock);
	res = rctl_test(rc_zone_nlwps, curproc->p_zone->zone_rctls, curproc,
	    1, 0);
	mutex_exit(&curproc->p_zone->zone_nlwps_lock);
	mutex_exit(&curproc->p_lock);
	if (res & RCT_DENY)
		return (EAGAIN);

	mac_flow_tbl = mac_flow_tab_get(zoneid);
	if (mac_flow_tbl == NULL)
		return (ENOENT);

	if (mrp != NULL) {
		err = mac_validate_props(NULL, mrp);
		if (err != 0)
			return (err);
	}

	if (flent == NULL) {
		flent = kmem_cache_alloc(mac_flow_tbl->flow_cache, KM_SLEEP);
		bzero(flent, sizeof (*flent));
		mutex_init(&flent->fe_lock, NULL, MUTEX_DEFAULT, NULL);
		cv_init(&flent->fe_cv, NULL, CV_DEFAULT, NULL);

		/* Initialize the receiver function to a safe routine */
		flent->fe_cb_fn = mac_pkt_drop;
		flent->fe_index = -1;
	}

	(void) strlcpy(flent->fe_flow_name, name, MAXFLOWNAMELEN);
	flent->fe_bwctl_active = B_FALSE;
	flent->fe_bwctl_cookie.bw_type = MAC_BWCTL_COOKIE;
	flent->fe_bwctl_cookie.bw_flent = flent;
	flent->fe_tx_unblock_cb = NULL;

	/* This is an initial flow, will be configured later */
	if (fd == NULL) {
		flent->fe_flow_desc.fd_zoneid = zoneid;
		*flentp = flent;
		return (0);
	}

	flent->fe_client_cookie = client_cookie;
	flent->fe_type = type;

	/* Save flow desc */
	bcopy(fd, &flent->fe_flow_desc, sizeof (*fd));

	if (mrp != NULL) {
		/*
		 * We have already set fe_resource_props for a Link.
		 */
		if (type & FLOW_USER) {
			bcopy(mrp, &flent->fe_resource_props,
			    sizeof (mac_resource_props_t));
		}
		/*
		 * The effective resource list should reflect the priority
		 * that we set implicitly.
		 */
		if (!(mrp->mrp_mask & MRP_PRIORITY))
			mrp->mrp_mask |= MRP_PRIORITY;
		if (type & FLOW_USER)
			mrp->mrp_priority = MPL_SUBFLOW_DEFAULT;
		else
			mrp->mrp_priority = MPL_LINK_DEFAULT;
		bzero(mrp->mrp_pool, MAXPATHLEN);
		bzero(&mrp->mrp_cpus, sizeof (mac_cpus_t));
		bcopy(mrp, &flent->fe_effective_props,
		    sizeof (mac_resource_props_t));
	}
	flow_stat_create(flent);

	*flentp = flent;
	return (0);
}

/*
 * Validate flow entry and add it to a flow table.
 */
int
mac_flow_add(flow_tab_t *ft, flow_entry_t *flent)
{
	flow_entry_t	**headp, **p;
	flow_ops_t	*ops = &ft->ft_ops;
	flow_mask_t	mask;
	uint32_t	index;
	int		err;

	ASSERT(MAC_PERIM_HELD((mac_handle_t)ft->ft_mip));

	/*
	 * Check for invalid bits in mask.
	 */
	mask = flent->fe_flow_desc.fd_mask;
	if ((mask & ft->ft_mask) == 0 || (mask & ~ft->ft_mask) != 0)
		return (EOPNOTSUPP);

	/*
	 * Validate flent.
	 */
	if ((err = ops->fo_accept_fe(ft, flent)) != 0) {
		DTRACE_PROBE3(accept_failed, flow_tab_t *, ft,
		    flow_entry_t *, flent, int, err);
		return (err);
	}

	/*
	 * Flent is valid. now calculate hash and insert it
	 * into hash table.
	 */
	index = ops->fo_hash_fe(ft, flent);

	/*
	 * We do not need a lock up until now because we were
	 * not accessing the flow table.
	 */
	rw_enter(&ft->ft_lock, RW_WRITER);
	headp = &ft->ft_table[index];

	/*
	 * Check for duplicate flow.
	 */
	for (p = headp; *p != NULL; p = &(*p)->fe_next) {
		if ((*p)->fe_flow_desc.fd_mask !=
		    flent->fe_flow_desc.fd_mask)
			continue;

		if (ft->ft_ops.fo_match_fe(ft, *p, flent)) {
			rw_exit(&ft->ft_lock);
			DTRACE_PROBE3(dup_flow, flow_tab_t *, ft,
			    flow_entry_t *, flent, int, err);
			return (EALREADY);
		}
	}

	/*
	 * Insert flow to hash list.
	 */
	err = ops->fo_insert_fe(ft, headp, flent);
	if (err != 0) {
		rw_exit(&ft->ft_lock);
		DTRACE_PROBE3(insert_failed, flow_tab_t *, ft,
		    flow_entry_t *, flent, int, err);
		return (err);
	}

	/*
	 * Save the hash index so it can be used by mac_flow_remove().
	 */
	flent->fe_index = (int)index;

	/*
	 * Save the flow tab back reference.
	 */
	flent->fe_flow_tab = ft;
	FLOW_MARK(flent, FE_FLOW_TAB);
	ft->ft_flow_count++;
	rw_exit(&ft->ft_lock);
	return (0);
}

/*
 * Remove a flow from a mac client's subflow table
 */
void
mac_flow_rem_subflow(flow_entry_t *flent)
{
	flow_tab_t		*ft = flent->fe_flow_tab;
	mac_client_impl_t	*mcip = ft->ft_mcip;
	mac_handle_t		mh = (mac_handle_t)ft->ft_mip;
	zoneid_t		zoneid = flent->fe_flow_desc.fd_zoneid;

	ASSERT(MAC_PERIM_HELD(mh));

	mac_flow_remove(ft, flent, B_FALSE);
	if (flent->fe_mcip == NULL) {
		/*
		 * The interface is not yet plumbed and mac_client_flow_add
		 * was not done.
		 */
		if (FLOW_TAB_EMPTY(ft)) {
			mac_flow_tab_destroy(ft, zoneid);
			mcip->mci_subflow_tab = NULL;
			mcip->mci_feature &= ~MAC_BWCTL_FLOW;
		}
	} else {
		mac_flow_wait(flent, FLOW_DRIVER_UPCALL);
		mac_link_flow_clean((mac_client_handle_t)mcip, flent);
	}
	mac_fastpath_enable(mh);
}

/*
 * Add a flow to a mac client's subflow table and instantiate the flow.
 */
int
mac_flow_add_subflow(mac_client_handle_t mch, flow_entry_t *flent,
    boolean_t instantiate_flow)
{
	mac_client_impl_t	*mcip = (mac_client_impl_t *)mch;
	mac_handle_t		mh = (mac_handle_t)mcip->mci_mip;
	flow_tab_info_t		*ftinfo;
	flow_mask_t		mask;
	flow_tab_t		*ft;
	int			err;
	boolean_t		ft_created = B_FALSE;
	zoneid_t		zoneid = flent->fe_flow_desc.fd_zoneid;

	ASSERT(MAC_PERIM_HELD(mh));

	if ((err = mac_fastpath_disable(mh)) != 0)
		return (err);

	/*
	 * If the subflow table exists already just add the new subflow
	 * to the existing table, else we create a new subflow table below.
	 */
	ft = mcip->mci_subflow_tab;
	if (ft == NULL) {
		mask = flent->fe_flow_desc.fd_mask;
		/*
		 * Try to create a new table and then add the subflow to the
		 * newly created subflow table
		 */
		if ((ftinfo = mac_flow_tab_info_get(mask)) == NULL) {
			mac_fastpath_enable(mh);
			return (EOPNOTSUPP);
		}

		mac_flow_tab_create(ftinfo->fti_ops, mask, ftinfo->fti_size,
		    mcip->mci_mip, flent->fe_flow_desc.fd_zoneid, &ft);
		ft_created = B_TRUE;
	} else {
		/* ngz cannot add flows if global zone already imposed flows */
		if (ft->ft_gz_flows &&
		    flent->fe_flow_desc.fd_zoneid != GLOBAL_ZONEID)
			return (ENOTSUP);
	}

	err = mac_flow_add(ft, flent);
	if (err != 0) {
		if (ft_created)
			mac_flow_tab_destroy(ft, zoneid);
		mac_fastpath_enable(mh);
		return (err);
	}

	if (instantiate_flow) {
		/* Now activate the flow */
		ASSERT(MCIP_DATAPATH_SETUP(mcip));
		err = mac_link_flow_init((mac_client_handle_t)mcip, flent);
		if (err != 0) {
			mac_flow_remove(ft, flent, B_FALSE);
			if (ft_created)
				mac_flow_tab_destroy(ft, zoneid);
			mac_fastpath_enable(mh);
			return (err);
		}
	} else {
		FLOW_MARK(flent, FE_UF_NO_DATAPATH);
	}
	if (ft_created) {
		ASSERT(mcip->mci_subflow_tab == NULL);
		if (flent->fe_flow_desc.fd_zoneid == GLOBAL_ZONEID)
			ft->ft_gz_flows = B_TRUE;
		ft->ft_mcip = mcip;
		mcip->mci_subflow_tab = ft;
		mcip->mci_feature |= MAC_BWCTL_FLOW;
	}
	return (0);
}

/*
 * Remove flow entry from flow table.
 */
void
mac_flow_remove(flow_tab_t *ft, flow_entry_t *flent, boolean_t temp)
{
	flow_entry_t	**fp;

	ASSERT(MAC_PERIM_HELD((mac_handle_t)ft->ft_mip));
	if (!(flent->fe_flags & FE_FLOW_TAB))
		return;

	rw_enter(&ft->ft_lock, RW_WRITER);
	/*
	 * If this is a permanent removal from the flow table, mark it
	 * CONDEMNED to prevent future references. If this is a temporary
	 * removal from the table, say to update the flow descriptor then
	 * we don't mark it CONDEMNED
	 */
	if (!temp)
		FLOW_MARK(flent, FE_CONDEMNED);
	/*
	 * Locate the specified flent.
	 */
	fp = &ft->ft_table[flent->fe_index];
	while (*fp != flent)
		fp = &(*fp)->fe_next;

	/*
	 * The flent must exist. Otherwise it's a bug.
	 */
	ASSERT(fp != NULL);
	*fp = flent->fe_next;
	flent->fe_next = NULL;

	/*
	 * Reset fe_index to -1 so any attempt to call mac_flow_remove()
	 * on a flent that is supposed to be in the table (FE_FLOW_TAB)
	 * will panic.
	 */
	flent->fe_index = -1;
	FLOW_UNMARK(flent, FE_FLOW_TAB);
	ft->ft_flow_count--;
	rw_exit(&ft->ft_lock);
}

/*
 * This is the flow lookup routine used by the mac sw classifier engine.
 */
int
mac_flow_lookup(flow_tab_t *ft, mblk_t *mp, uint_t flags, flow_entry_t **flentp)
{
	flow_state_t	s;
	flow_entry_t	*flent;
	flow_ops_t	*ops = &ft->ft_ops;
	boolean_t	retried = B_FALSE;
	int		i, err;

	s.fs_flags = flags;
retry:
	s.fs_mp = mp;

	/*
	 * Walk the list of predeclared accept functions.
	 * Each of these would accumulate enough state to allow the next
	 * accept routine to make progress.
	 */
	for (i = 0; i < FLOW_MAX_ACCEPT && ops->fo_accept[i] != NULL; i++) {
		if ((err = (ops->fo_accept[i])(ft, &s)) != 0) {
			mblk_t	*last;

			/*
			 * ENOBUFS indicates that the mp could be too short
			 * and may need a pullup.
			 */
			if (err != ENOBUFS || retried)
				return (err);

			/*
			 * The pullup is done on the last processed mblk, not
			 * the starting one. pullup is not done if the mblk
			 * has references or if b_cont is NULL.
			 */
			last = s.fs_mp;
			if (DB_REF(last) > 1 || last->b_cont == NULL ||
			    pullupmsg(last, -1) == 0)
				return (EINVAL);

			retried = B_TRUE;
			DTRACE_PROBE2(need_pullup, flow_tab_t *, ft,
			    flow_state_t *, &s);
			goto retry;
		}
	}

	/*
	 * The packet is considered sane. We may now attempt to
	 * find the corresponding flent.
	 */
	rw_enter(&ft->ft_lock, RW_READER);
	flent = ft->ft_table[ops->fo_hash(ft, &s)];
	for (; flent != NULL; flent = flent->fe_next) {
		if (flent->fe_match(ft, flent, &s)) {
			FLOW_TRY_REFHOLD(flent, err);
			if (err != 0)
				continue;
			*flentp = flent;
			rw_exit(&ft->ft_lock);
			return (0);
		}
	}
	rw_exit(&ft->ft_lock);
	return (ENOENT);
}

/*
 * Walk flow table.
 * The caller is assumed to have proper perimeter protection.
 */
int
mac_flow_walk_nolock(flow_tab_t *ft, int (*fn)(flow_entry_t *, void *),
    void *arg)
{
	int		err, i, cnt = 0;
	flow_entry_t	*flent;

	if (ft == NULL)
		return (0);

	for (i = 0; i < ft->ft_size; i++) {
		for (flent = ft->ft_table[i]; flent != NULL;
		    flent = flent->fe_next) {
			cnt++;
			err = (*fn)(flent, arg);
			if (err != 0)
				return (err);
		}
	}
	VERIFY(cnt == ft->ft_flow_count);
	return (0);
}

/*
 * Same as the above except a mutex is used for protection here.
 */
int
mac_flow_walk(flow_tab_t *ft, int (*fn)(flow_entry_t *, void *),
    void *arg)
{
	int		err;

	if (ft == NULL)
		return (0);

	rw_enter(&ft->ft_lock, RW_WRITER);
	err = mac_flow_walk_nolock(ft, fn, arg);
	rw_exit(&ft->ft_lock);
	return (err);
}

static boolean_t	mac_flow_clean(flow_entry_t *);

/*
 * Destroy a flow entry. Called when the last reference on a flow is released.
 */
void
mac_flow_destroy(flow_entry_t *flent)
{
	flow_zone_tab_t	*mac_flow_tbl;

	mac_flow_tbl = mac_flow_tab_get(flent->fe_flow_desc.fd_zoneid);
	if (mac_flow_tbl == NULL)
		return;

	ASSERT(flent->fe_refcnt == 0);

	if ((flent->fe_type & FLOW_USER) != 0) {
		ASSERT(mac_flow_clean(flent));
	} else {
		mac_flow_cleanup(flent);
	}
	mutex_destroy(&flent->fe_lock);
	cv_destroy(&flent->fe_cv);
	flow_stat_destroy(flent);
	kmem_cache_free(mac_flow_tbl->flow_cache, flent);
}

/*
 * XXX eric
 * The MAC_FLOW_PRIORITY checks in mac_resource_ctl_set() and
 * mac_link_flow_modify() should really be moved/reworked into the
 * two functions below. This would consolidate all the mac property
 * checking in one place. I'm leaving this alone for now since it's
 * out of scope of the new flows work.
 */
/* ARGSUSED */
uint32_t
mac_flow_modify_props(flow_entry_t *flent, mac_resource_props_t *mrp)
{
	uint32_t		changed_mask = 0;
	mac_resource_props_t	*fmrp = &flent->fe_effective_props;
	int			i;

	if ((mrp->mrp_mask & MRP_MAXBW) != 0 &&
	    (!(fmrp->mrp_mask & MRP_MAXBW) ||
	    (fmrp->mrp_maxbw != mrp->mrp_maxbw))) {
		changed_mask |= MRP_MAXBW;
		if (mrp->mrp_maxbw == MRP_MAXBW_RESETVAL) {
			fmrp->mrp_mask &= ~MRP_MAXBW;
			fmrp->mrp_maxbw = 0;
		} else {
			fmrp->mrp_mask |= MRP_MAXBW;
			fmrp->mrp_maxbw = mrp->mrp_maxbw;
		}
	}

	if ((mrp->mrp_mask & MRP_PRIORITY) != 0) {
		if (fmrp->mrp_priority != mrp->mrp_priority)
			changed_mask |= MRP_PRIORITY;
		if (mrp->mrp_priority == MPL_RESET) {
			fmrp->mrp_priority = MPL_SUBFLOW_DEFAULT;
			fmrp->mrp_mask &= ~MRP_PRIORITY;
		} else {
			fmrp->mrp_priority = mrp->mrp_priority;
			fmrp->mrp_mask |= MRP_PRIORITY;
		}
	}

	/* modify cpus */
	if ((mrp->mrp_mask & MRP_CPUS) != 0) {
		if (fmrp->mrp_ncpus == mrp->mrp_ncpus) {
			for (i = 0; i < mrp->mrp_ncpus; i++) {
				if (mrp->mrp_cpu[i] != fmrp->mrp_cpu[i])
					break;
			}
			if (i == mrp->mrp_ncpus) {
				/*
				 * The new set of cpus passed is exactly
				 * the same as the existing set.
				 */
				return (changed_mask);
			}
		}
		changed_mask |= MRP_CPUS;
		MAC_COPY_CPUS(mrp, fmrp);
	}

	/* modify fanout */
	if ((mrp->mrp_mask & MRP_RXFANOUT) != 0 &&
	    (!(fmrp->mrp_mask & MRP_RXFANOUT) ||
	    (fmrp->mrp_rxfanout != mrp->mrp_rxfanout))) {
		changed_mask |= MRP_RXFANOUT;
		if (mrp->mrp_rxfanout == MRP_PROP_RESET) {
			fmrp->mrp_mask &= ~MRP_RXFANOUT;
			fmrp->mrp_rxfanout = 0;
		} else {
			fmrp->mrp_mask |= MRP_RXFANOUT;
			fmrp->mrp_rxfanout = mrp->mrp_rxfanout;
		}
	}

	/*
	 * Modify the rings property.
	 */
	if (mrp->mrp_mask & MRP_RX_RINGS || mrp->mrp_mask & MRP_TX_RINGS)
		mac_set_rings_effective(flent->fe_mcip);

	if ((mrp->mrp_mask & MRP_POOL) != 0) {
		if (strcmp(fmrp->mrp_pool, mrp->mrp_pool) != 0)
			changed_mask |= MRP_POOL;
		if (strlen(mrp->mrp_pool) == 0)
			fmrp->mrp_mask &= ~MRP_POOL;
		else
			fmrp->mrp_mask |= MRP_POOL;
		(void) strncpy(fmrp->mrp_pool, mrp->mrp_pool, MAXPATHLEN);
	}

	/* Modify the user priority property */
	if ((mrp->mrp_mask & MRP_USRPRI) != 0) {
		uint8_t	pri = fmrp->mrp_usrpri;

		if (mrp->mrp_mask & MRP_PROP_RESET) {
			fmrp->mrp_mask &= ~MRP_USRPRI;
			fmrp->mrp_usrpri = 0;
		} else {
			fmrp->mrp_mask |= MRP_USRPRI;
			fmrp->mrp_usrpri = mrp->mrp_usrpri;
		}
		if (fmrp->mrp_usrpri != pri)
			changed_mask |= MRP_USRPRI;
	}
	return (changed_mask);
}

/* ARGSUSED */
void
mac_flow_modify(flow_tab_t *ft, flow_entry_t *flent, mac_resource_props_t *mrp)
{
	uint32_t changed_mask;
	mac_client_impl_t *mcip = flent->fe_mcip;

	ASSERT(flent != NULL);
	ASSERT(MAC_PERIM_HELD((mac_handle_t)ft->ft_mip));

	rw_enter(&ft->ft_lock, RW_WRITER);

	/* Update the cached values inside the subflow entry */
	changed_mask = mac_flow_modify_props(flent, mrp);
	rw_exit(&ft->ft_lock);

	/*
	 * Push the changed parameters to the scheduling code.
	 */
	if (changed_mask & MRP_MAXBW) {
		mac_bwctl_flow_update(flent);
		if ((mrp->mrp_mask & ~MRP_MAXBW) == 0)
			return;
	}

	i_mac_setup_enter(mcip->mci_mip);

	if (mrp->mrp_mask & MRP_PRIORITY)
		mac_flow_update_priority(mcip);

	if ((mrp->mrp_mask & (MRP_RXFANOUT | MRP_CPUS | MRP_RX_RINGS)) &&
	    mcip->mci_rx_fanout != NULL) {
		mac_rx_group_setup(mcip);
	}

	/*
	 * Calling *_setup() is too big a hammer. Everything will
	 * be redone for cpu bindings. Instead *_setup() should
	 * be called only if the number of soft rings changes.
	 * Just a CPU change should result in the calling of
	 * mac_cpu_modify().
	 */
	if (changed_mask & MRP_CPUS) {
		mac_cpu_setup(mcip);
	} else if (mrp->mrp_mask & MRP_POOL) {
		mac_cpu_pool_setup(mcip);
	}

	i_mac_setup_exit(mcip->mci_mip);
}

/*
 * This function waits for a certain condition to be met and is generally
 * used before a destructive or quiescing operation.
 */
void
mac_flow_wait(flow_entry_t *flent, mac_flow_state_t event)
{
	mutex_enter(&flent->fe_lock);
	flent->fe_flags |= FE_WAITER;

	switch (event) {
	case FLOW_DRIVER_UPCALL:
		/*
		 * We want to make sure the driver upcalls have finished.
		 */
		while (flent->fe_refcnt != 1)
			cv_wait(&flent->fe_cv, &flent->fe_lock);
		break;

	case FLOW_USER_REF:
		/*
		 * Wait for the fe_user_refcnt to drop to 0. The flow has
		 * been removed from the global flow hash.
		 */
		ASSERT(!(flent->fe_flags & FE_G_FLOW_HASH));
		while (flent->fe_user_refcnt != 0)
			cv_wait(&flent->fe_cv, &flent->fe_lock);
		break;

	default:
		ASSERT(0);
	}

	flent->fe_flags &= ~FE_WAITER;
	mutex_exit(&flent->fe_lock);
}

static boolean_t
mac_flow_clean(flow_entry_t *flent)
{
	ASSERT(flent->fe_next == NULL);
	ASSERT(flent->fe_mbg == NULL);

	return (B_TRUE);
}

void
mac_flow_cleanup(flow_entry_t *flent)
{
	if ((flent->fe_type & FLOW_USER) == 0) {
		ASSERT((flent->fe_mbg == NULL && flent->fe_mcip != NULL) ||
		    (flent->fe_mbg != NULL && flent->fe_mcip == NULL));
		ASSERT(flent->fe_refcnt == 0);
	} else {
		ASSERT(flent->fe_refcnt == 1);
	}

	if (flent->fe_mbg != NULL) {
		/* This is a multicast or broadcast flow entry */
		mac_bcast_grp_free(flent->fe_mbg);
		flent->fe_mbg = NULL;
	}
}

void
mac_flow_get_desc(flow_entry_t *flent, flow_desc_t *fd)
{
	ASSERT(flent != NULL);

	/*
	 * Grab the fe_lock to see a self-consistent fe_flow_desc.
	 * Updates to the fe_flow_desc happen under the fe_lock
	 * after removing the flent from the flow table
	 */
	mutex_enter(&flent->fe_lock);
	bcopy(&flent->fe_flow_desc, fd, sizeof (*fd));
	mutex_exit(&flent->fe_lock);
}

/*
 * Update a field of a flow entry. The mac perimeter ensures that
 * this is the only thread doing a modify operation on this mac end point.
 * So the flow table can't change or disappear. The ft_lock protects access
 * to the flow entry, and holding the lock ensures that there isn't any thread
 * accessing the flow entry or attempting a flow table lookup. However
 * data threads that are using the flow entry based on the old descriptor
 * will continue to use the flow entry. If strong coherence is required
 * then the flow will have to be quiesced before the descriptor can be
 * changed.
 */
void
mac_flow_set_desc(flow_entry_t *flent, flow_desc_t *fd)
{
	flow_tab_t	*ft = flent->fe_flow_tab;
	flow_desc_t	old_desc;
	int		err;

	if (ft == NULL) {
		/*
		 * The flow hasn't yet been inserted into the table,
		 * so only the caller knows about this flow, however for
		 * uniformity we grab the fe_lock here.
		 */
		mutex_enter(&flent->fe_lock);
		bcopy(fd, &flent->fe_flow_desc, sizeof (*fd));
		mutex_exit(&flent->fe_lock);
	}

	ASSERT(MAC_PERIM_HELD((mac_handle_t)ft->ft_mip));

	/*
	 * Need to remove the flow entry from the table and reinsert it,
	 * into a potentially diference hash line. The hash depends on
	 * the new descriptor fields. However access to fe_desc itself
	 * is always under the fe_lock. This helps log and stat functions
	 * see a self-consistent fe_flow_desc.
	 */
	mac_flow_remove(ft, flent, B_TRUE);
	old_desc = flent->fe_flow_desc;

	mutex_enter(&flent->fe_lock);
	bcopy(fd, &flent->fe_flow_desc, sizeof (*fd));
	mutex_exit(&flent->fe_lock);

	if (mac_flow_add(ft, flent) != 0) {
		/*
		 * The add failed say due to an invalid flow descriptor.
		 * Undo the update
		 */
		flent->fe_flow_desc = old_desc;
		err = mac_flow_add(ft, flent);
		ASSERT(err == 0);
	}
}

void
mac_flow_set_name(flow_entry_t *flent, const char *name)
{
	flow_tab_t	*ft = flent->fe_flow_tab;

	if (ft == NULL) {
		/*
		 *  The flow hasn't yet been inserted into the table,
		 * so only the caller knows about this flow
		 */
		(void) strlcpy(flent->fe_flow_name, name, MAXFLOWNAMELEN);
	} else {
		ASSERT(MAC_PERIM_HELD((mac_handle_t)ft->ft_mip));
	}

	mutex_enter(&flent->fe_lock);
	(void) strlcpy(flent->fe_flow_name, name, MAXFLOWNAMELEN);
	mutex_exit(&flent->fe_lock);
}

/*
 * Return the client-private cookie that was associated with
 * the flow when it was created.
 */
void *
mac_flow_get_client_cookie(flow_entry_t *flent)
{
	return (flent->fe_client_cookie);
}

/*
 * Forward declarations.
 */
static uint32_t	flow_l2_hash(flow_tab_t *, flow_state_t *);
static uint32_t	flow_l2_hash_fe(flow_tab_t *, flow_entry_t *);
static int	flow_l2_accept(flow_tab_t *, flow_state_t *);
static uint32_t	flow_ether_hash(flow_tab_t *, flow_state_t *);
static uint32_t	flow_ether_hash_fe(flow_tab_t *, flow_entry_t *);
static int	flow_ether_accept(flow_tab_t *, flow_state_t *);

/*
 * Create flow table.
 */
void
mac_flow_tab_create(flow_ops_t *ops, flow_mask_t mask, uint_t size,
    mac_impl_t *mip, zoneid_t zoneid, flow_tab_t **ftp)
{
	flow_tab_t	*ft;
	flow_ops_t	*new_ops;
	flow_zone_tab_t	*mac_flow_tbl;

	mac_flow_tbl = mac_flow_tab_get(zoneid);
	if (mac_flow_tbl == NULL)
		return;

	ft = kmem_cache_alloc(mac_flow_tbl->flow_tab_cache, KM_SLEEP);
	bzero(ft, sizeof (*ft));

	ft->ft_table = kmem_zalloc(size * sizeof (flow_entry_t *), KM_SLEEP);

	/*
	 * We make a copy of the ops vector instead of just pointing to it
	 * because we might want to customize the ops vector on a per table
	 * basis (e.g. for optimization).
	 */
	new_ops = &ft->ft_ops;
	bcopy(ops, new_ops, sizeof (*ops));
	ft->ft_mask = mask;
	ft->ft_size = size;
	ft->ft_mip = mip;
	ft->ft_gz_flows = B_FALSE;
	ft->ft_zoneid = zoneid;

	/*
	 * Optimizations for DL_ETHER media.
	 */
	if (mip->mi_info.mi_nativemedia == DL_ETHER) {
		if (new_ops->fo_hash == flow_l2_hash)
			new_ops->fo_hash = flow_ether_hash;
		if (new_ops->fo_hash_fe == flow_l2_hash_fe)
			new_ops->fo_hash_fe = flow_ether_hash_fe;
		if (new_ops->fo_accept[0] == flow_l2_accept)
			new_ops->fo_accept[0] = flow_ether_accept;
	}
	*ftp = ft;
}

void
mac_flow_l2tab_create(mac_impl_t *mip, flow_tab_t **ftp)
{
	mac_flow_tab_create(&flow_l2_ops, FLOW_LINK_DST | FLOW_LINK_VID,
	    1024, mip, mip->mi_zoneid, ftp);
}

/*
 * Destroy flow table.
 */
void
mac_flow_tab_destroy(flow_tab_t *ft, zoneid_t zoneid)
{
	flow_zone_tab_t	*mac_flow_tbl;

	if (ft == NULL)
		return;

	mac_flow_tbl = mac_flow_tab_get(zoneid);
	if (mac_flow_tbl == NULL)
		return;

	ASSERT(ft->ft_flow_count == 0);
	kmem_free(ft->ft_table, ft->ft_size * sizeof (flow_entry_t *));
	bzero(ft, sizeof (*ft));
	kmem_cache_free(mac_flow_tbl->flow_tab_cache, ft);
}

/*
 * Add a new flow entry to the global flow hash table
 */
int
mac_flow_hash_add(flow_entry_t *flent)
{
	int		err;
	flow_zone_tab_t	*mac_flow_tbl;

	mac_flow_tbl = mac_flow_tab_get(flent->fe_flow_desc.fd_zoneid);
	if (mac_flow_tbl == NULL)
		return (ENOENT);

	rw_enter(&mac_flow_tbl->flow_tab_lock, RW_WRITER);
	err = mod_hash_insert(mac_flow_tbl->flow_hash,
	    (mod_hash_key_t)flent->fe_flow_name, (mod_hash_val_t)flent);
	if (err != 0) {
		rw_exit(&mac_flow_tbl->flow_tab_lock);
		return (EEXIST);
	}
	/* Mark as inserted into the global flow hash table */
	FLOW_MARK(flent, FE_G_FLOW_HASH);
	rw_exit(&mac_flow_tbl->flow_tab_lock);
	return (0);
}

/*
 * Remove a flow entry from the flow hash table
 */
void
mac_flow_hash_remove(flow_entry_t *flent)
{
	mod_hash_val_t	val;
	flow_zone_tab_t	*mac_flow_tbl;

	mac_flow_tbl = mac_flow_tab_get(flent->fe_flow_desc.fd_zoneid);
	if (mac_flow_tbl == NULL)
		return;

	rw_enter(&mac_flow_tbl->flow_tab_lock, RW_WRITER);
	VERIFY(mod_hash_remove(mac_flow_tbl->flow_hash,
	    (mod_hash_key_t)flent->fe_flow_name, &val) == 0);

	/* Clear the mark that says inserted into the global flow hash table */
	FLOW_UNMARK(flent, FE_G_FLOW_HASH);
	rw_exit(&mac_flow_tbl->flow_tab_lock);
}

/*
 * Retrieve a flow entry from the global flow hash table.
 */
int
mac_flow_lookup_byname(char *name, flow_entry_t **flentp, zoneid_t zoneid)
{
	int		err;
	flow_entry_t	*flent;
	flow_zone_tab_t	*mac_flow_tbl;

	mac_flow_tbl = mac_flow_tab_get(zoneid);
	if (mac_flow_tbl == NULL)
		return (ENOENT);

	rw_enter(&mac_flow_tbl->flow_tab_lock, RW_READER);
	err = mod_hash_find(mac_flow_tbl->flow_hash, (mod_hash_key_t)name,
	    (mod_hash_val_t *)&flent);
	if (err != 0) {
		rw_exit(&mac_flow_tbl->flow_tab_lock);
		return (ENOENT);
	}
	ASSERT(flent != NULL);
	FLOW_USER_REFHOLD(flent);
	rw_exit(&mac_flow_tbl->flow_tab_lock);

	*flentp = flent;
	return (0);
}

/*
 * Initialize or release mac client flows by walking the subflow table.
 * These are typically invoked during plumb/unplumb of links.
 */

static int
mac_link_init_flows_cb(flow_entry_t *flent, void *arg)
{
	mac_client_impl_t	*mcip = arg;

	if (mac_link_flow_init(arg, flent) != 0) {
		cmn_err(CE_WARN, "Failed to initialize flow '%s' on link '%s'",
		    flent->fe_flow_name, mcip->mci_name);
	} else {
		FLOW_UNMARK(flent, FE_UF_NO_DATAPATH);
	}
	return (0);
}

void
mac_link_init_flows(mac_client_handle_t mch)
{
	mac_client_impl_t	*mcip = (mac_client_impl_t *)mch;

	(void) mac_flow_walk_nolock(mcip->mci_subflow_tab,
	    mac_link_init_flows_cb, mcip);
}

boolean_t
mac_link_has_flows(mac_client_handle_t mch)
{
	mac_client_impl_t	*mcip = (mac_client_impl_t *)mch;

	if (!FLOW_TAB_EMPTY(mcip->mci_subflow_tab))
		return (B_TRUE);

	return (B_FALSE);
}

static int
mac_link_release_flows_cb(flow_entry_t *flent, void *arg)
{
	FLOW_MARK(flent, FE_UF_NO_DATAPATH);
	mac_flow_wait(flent, FLOW_DRIVER_UPCALL);
	mac_link_flow_clean(arg, flent);
	return (0);
}

void
mac_link_release_flows(mac_client_handle_t mch)
{
	mac_client_impl_t	*mcip = (mac_client_impl_t *)mch;

	(void) mac_flow_walk_nolock(mcip->mci_subflow_tab,
	    mac_link_release_flows_cb, mcip);
}

void
mac_rename_flow(flow_entry_t *fep, const char *new_name)
{
	mac_flow_set_name(fep, new_name);
	if (fep->fe_ksp_ngz != NULL || fep->fe_ksp_gz != NULL) {
		flow_stat_destroy(fep);
		flow_stat_create(fep);
	}
}

/*
 * mac_link_flow_init()
 */
int
mac_link_flow_init(mac_client_handle_t mch, flow_entry_t *flent)
{
	mac_client_impl_t 	*mcip = (mac_client_impl_t *)mch;
	mac_resource_props_t	*mrp = &flent->fe_resource_props;
	mac_impl_t		*mip = mcip->mci_mip;

	ASSERT(mch != NULL);
	ASSERT(MAC_PERIM_HELD((mac_handle_t)mip));

	flent->fe_mcip = mcip;
	if ((mrp->mrp_mask & MRP_MAXBW) != 0)
		mac_bwctl_flow_update(flent);
	return (0);
}

/*
 * mac_link_flow_add()
 * Used by flowadm(1m) or kernel mac clients for creating flows.
 */
int
mac_link_flow_add(datalink_id_t linkid, char *flow_name,
    flow_desc_t *flow_desc, mac_resource_props_t *mrp)
{
	flow_entry_t		*flent = NULL;
	int			err;
	dls_dl_handle_t		dlh;
	dls_link_t		*dlp;
	boolean_t		link_held = B_FALSE;
	boolean_t		hash_added = B_FALSE;
	mac_perim_handle_t	mph;
	flow_tab_t		*ft;
	zoneid_t		zoneid = flow_desc->fd_zoneid;

	err = mac_flow_lookup_byname(flow_name, &flent, zoneid);
	if (err == 0) {
		FLOW_USER_REFRELE(flent);
		return (EEXIST);
	}

	/*
	 * First create a flow entry given the description provided
	 * by the caller.
	 */
	err = mac_flow_create(flow_desc, mrp, flow_name, NULL,
	    FLOW_USER | FLOW_OTHER, &flent, zoneid);

	if (err != 0)
		return (err);

	/*
	 * We've got a local variable referencing this flow now, so we need
	 * to hold it. We'll release this flow before returning.
	 * All failures until we return will undo any action that may internally
	 * held the flow, so the last REFRELE will assure a clean freeing
	 * of resources.
	 */
	FLOW_REFHOLD(flent);

	flent->fe_link_id = linkid;
	FLOW_MARK(flent, FE_INCIPIENT);

	err = mac_perim_enter_by_linkid(linkid, &mph);
	if (err != 0) {
		FLOW_FINAL_REFRELE(flent);
		return (err);
	}

	/*
	 * dls will eventually be merged with mac so it's ok
	 * to call dls' internal functions.
	 */
	err = dls_devnet_hold_link(linkid, &dlh, &dlp);
	if (err != 0)
		goto bail;

	link_held = B_TRUE;

	/*
	 * if there is gz imposed flows on a ngz link, ngz cannot create any
	 * flows on this link
	 */
	ft = ((mac_client_impl_t *)dlp->dl_mch)->mci_subflow_tab;
	if (flent->fe_flow_desc.fd_zoneid != GLOBAL_ZONEID &&
	    (ft != NULL && ft->ft_gz_flows)) {
		err = ENOTSUP;
		goto bail;
	}

	/*
	 * Add the flow to the global flow table, this table will be per
	 * exclusive zone so each zone can have its own flow namespace.
	 * RFE 6625651 will fix this.
	 *
	 */
	if ((err = mac_flow_hash_add(flent)) != 0)
		goto bail;

	hash_added = B_TRUE;

	/*
	 * do not allow flows to be configured on an anchor VNIC
	 */
	if (mac_capab_get(dlp->dl_mh, MAC_CAPAB_ANCHOR_VNIC, NULL)) {
		err = ENOTSUP;
		goto bail;
	}

	/*
	 * Add the subflow to the subflow table. Also instantiate the flow
	 * in the mac if there is an active user (we check if the MAC client's
	 * datapath has been setup).
	 */
	err = mac_flow_add_subflow(dlp->dl_mch, flent,
	    MCIP_DATAPATH_SETUP((mac_client_impl_t *)dlp->dl_mch));
	if (err != 0)
		goto bail;

	FLOW_UNMARK(flent, FE_INCIPIENT);
	dls_devnet_rele_link(dlh, dlp);
	mac_perim_exit(mph);
	return (0);

bail:
	if (hash_added)
		mac_flow_hash_remove(flent);

	if (link_held)
		dls_devnet_rele_link(dlh, dlp);

	/*
	 * Wait for any transient global flow hash refs to clear
	 * and then release the creation reference on the flow
	 */
	mac_flow_wait(flent, FLOW_USER_REF);
	FLOW_FINAL_REFRELE(flent);
	mac_perim_exit(mph);
	return (err);
}

/*
 * mac_link_flow_clean()
 */
void
mac_link_flow_clean(mac_client_handle_t mch, flow_entry_t *sub_flow)
{
	mac_client_impl_t 	*mcip = (mac_client_impl_t *)mch;
	mac_impl_t		*mip = mcip->mci_mip;
	boolean_t		last_subflow;
	zoneid_t		zoneid = sub_flow->fe_flow_desc.fd_zoneid;

	ASSERT(mch != NULL);
	ASSERT(MAC_PERIM_HELD((mac_handle_t)mip));

	/*
	 * This sub flow entry may fail to be fully initialized by
	 * mac_link_flow_init(). If so, simply return.
	 */
	if (sub_flow->fe_mcip == NULL)
		return;

	last_subflow = FLOW_TAB_EMPTY(mcip->mci_subflow_tab);
	sub_flow->fe_mcip = NULL;
	mac_flow_cleanup(sub_flow);

	/*
	 * If all the subflows are gone, renable some of the stuff
	 * we disabled when adding a subflow, polling etc.
	 */
	if (last_subflow) {
		/*
		 * The subflow table itself is not protected by any locks or
		 * refcnts. Hence quiesce the client upfront before clearing
		 * mci_subflow_tab.
		 */
		mac_client_quiesce(mcip);
		mac_flow_tab_destroy(mcip->mci_subflow_tab, zoneid);
		mcip->mci_subflow_tab = NULL;
		mcip->mci_feature &= ~MAC_BWCTL_FLOW;
		mac_client_restart(mcip);
	}
}

/* Set ksp_ngz for gz-imposed flows when a zone boots up */
int
mac_link_flow_createkstat(char *flow_name, zoneid_t zoneid)
{
	flow_entry_t		*flent;
	int			err;
	kstat_t			*ksp_ngz;
	kstat_named_t		*knp_ngz;
	uint_t			nstats = FS_SIZE;
	char			kstat_name[MAXFLOWNAMELEN];

	if (zoneid == GLOBAL_ZONEID)
		return (0);

	/*
	 * This is for global zone flows that are imposed on ngz only.
	 * zoneid is the ngz's id on which flows will be imposed
	 */
	err = mac_flow_lookup_byname(flow_name, &flent, GLOBAL_ZONEID);
	if (err != 0)
		return (err);
	FLOW_USER_REFRELE(flent);

	(void) snprintf(kstat_name, sizeof (kstat_name), "global/%s",
	    flow_name);

	ksp_ngz = kstat_create_zone("unix", 0, kstat_name, "flow",
	    KSTAT_TYPE_NAMED, nstats, 0, zoneid);
	if (ksp_ngz == NULL)
		return (EINVAL);

	ksp_ngz->ks_update = flow_stat_update;
	ksp_ngz->ks_private = flent;
	flent->fe_ksp_ngz = ksp_ngz;

	knp_ngz = (kstat_named_t *)ksp_ngz->ks_data;
	flow_stat_init(knp_ngz);
	kstat_install(ksp_ngz);

	return (0);
}

/*
 * mac_link_flow_remove()
 * Used by flowadm(1m) or kernel mac clients for removing flows.
 */
int
mac_link_flow_remove(char *flow_name, zoneid_t zoneid)
{
	flow_entry_t		*flent;
	mac_perim_handle_t	mph;
	int			err;
	datalink_id_t		linkid;

	err = mac_flow_lookup_byname(flow_name, &flent, zoneid);
	if (err != 0)
		return (err);

	linkid = flent->fe_link_id;
	FLOW_USER_REFRELE(flent);

	/*
	 * The perim must be acquired before acquiring any other references
	 * to maintain the lock and perimeter hierarchy. Please note the
	 * FLOW_REFRELE above.
	 */
	err = mac_perim_enter_by_linkid(linkid, &mph);
	if (err != 0)
		return (err);

	/*
	 * Note the second lookup of the flow, because a concurrent thread
	 * may have removed it already while we were waiting to enter the
	 * link's perimeter.
	 */
	err = mac_flow_lookup_byname(flow_name, &flent, zoneid);
	if (err != 0) {
		mac_perim_exit(mph);
		return (err);
	}
	FLOW_USER_REFRELE(flent);

	/*
	 * Remove the flow from the subflow table.
	 */
	mac_flow_rem_subflow(flent);

	/*
	 * Finally, remove the flow from the global table.
	 */
	mac_flow_hash_remove(flent);

	/*
	 * Wait for any transient global flow hash refs to clear
	 * and then release the creation reference on the flow
	 */
	mac_flow_wait(flent, FLOW_USER_REF);
	FLOW_FINAL_REFRELE(flent);

	mac_perim_exit(mph);

	return (0);
}

/*
 * mac_link_flow_modify()
 * Modifies the properties of a flow identified by its name.
 */
int
mac_link_flow_modify(char *flow_name, mac_resource_props_t *mrp,
    zoneid_t zoneid)
{
	flow_entry_t		*flent;
	mac_client_impl_t 	*mcip;
	int			err = 0;
	mac_perim_handle_t	mph;
	datalink_id_t		linkid;
	flow_tab_t		*flow_tab;

	err = mac_validate_props(NULL, mrp);
	if (err != 0)
		return (err);

	err = mac_flow_lookup_byname(flow_name, &flent, zoneid);
	if (err != 0)
		return (err);

	linkid = flent->fe_link_id;
	FLOW_USER_REFRELE(flent);

	/*
	 * The perim must be acquired before acquiring any other references
	 * to maintain the lock and perimeter hierarchy. Please note the
	 * FLOW_REFRELE above.
	 */
	err = mac_perim_enter_by_linkid(linkid, &mph);
	if (err != 0)
		return (err);

	/*
	 * Note the second lookup of the flow, because a concurrent thread
	 * may have removed it already while we were waiting to enter the
	 * link's perimeter.
	 */
	err = mac_flow_lookup_byname(flow_name, &flent, zoneid);
	if (err != 0) {
		mac_perim_exit(mph);
		return (err);
	}
	FLOW_USER_REFRELE(flent);

	/*
	 * If this flow is attached to a MAC client, then pass the request
	 * along to the client.
	 * Otherwise, just update the cached values.
	 */
	mcip = flent->fe_mcip;
	mac_update_resources(mrp, &flent->fe_resource_props, B_TRUE);
	if (mcip != NULL) {
		if ((flow_tab = mcip->mci_subflow_tab) == NULL) {
			err = ENOENT;
		} else {
			mac_flow_modify(flow_tab, flent, mrp);
		}
	} else {
		(void) mac_flow_modify_props(flent, mrp);
	}

done:
	mac_perim_exit(mph);
	return (err);
}


/*
 * State structure and misc functions used by mac_link_flow_walk().
 */
typedef struct {
	int	(*ws_func)(mac_flowinfo_t *, void *);
	void	*ws_arg;
} flow_walk_state_t;

static void
mac_link_flowinfo_copy(mac_flowinfo_t *finfop, flow_entry_t *flent)
{
	(void) strlcpy(finfop->fi_flow_name, flent->fe_flow_name,
	    MAXFLOWNAMELEN);
	finfop->fi_link_id = flent->fe_link_id;
	finfop->fi_flow_desc = flent->fe_flow_desc;
	finfop->fi_resource_props = flent->fe_resource_props;
}

static int
mac_link_flow_walk_cb(flow_entry_t *flent, void *arg)
{
	flow_walk_state_t	*statep = arg;
	mac_flowinfo_t		*finfo;
	int			err;

	finfo = kmem_zalloc(sizeof (*finfo), KM_SLEEP);
	mac_link_flowinfo_copy(finfo, flent);
	err = statep->ws_func(finfo, statep->ws_arg);
	kmem_free(finfo, sizeof (*finfo));
	return (err);
}

/*
 * mac_link_flow_walk()
 * Invokes callback 'func' for all flows belonging to the specified link.
 */
int
mac_link_flow_walk(datalink_id_t linkid,
    int (*func)(mac_flowinfo_t *, void *), void *arg)
{
	mac_client_impl_t	*mcip;
	mac_perim_handle_t	mph;
	flow_walk_state_t	state;
	dls_dl_handle_t		dlh;
	dls_link_t		*dlp;
	int			err;

	err = mac_perim_enter_by_linkid(linkid, &mph);
	if (err != 0)
		return (err);

	err = dls_devnet_hold_link(linkid, &dlh, &dlp);
	if (err != 0) {
		mac_perim_exit(mph);
		return (err);
	}

	mcip = (mac_client_impl_t *)dlp->dl_mch;
	state.ws_func = func;
	state.ws_arg = arg;

	err = mac_flow_walk_nolock(mcip->mci_subflow_tab,
	    mac_link_flow_walk_cb, &state);

	dls_devnet_rele_link(dlh, dlp);
	mac_perim_exit(mph);
	return (err);
}

/*
 * mac_link_flow_info()
 * Retrieves information about a specific flow.
 */
int
mac_link_flow_info(char *flow_name, mac_flowinfo_t *finfo, zoneid_t curzoneid)
{
	flow_entry_t	*flent;
	int		err;
	zoneid_t	inputzoneid;
	zone_t		*zone;
	char		*token, fname[MAXFLOWNAMELEN], zonename[ZONENAME_MAX];
	char		tmp[MAXFLOWNAMELEN];

	(void) strlcpy(tmp, flow_name, MAXFLOWNAMELEN);
	/*
	 * global zone may request to show a ngz's flow, so the flowname might
	 * be in format of "zone-name/flow-name"
	 */
	token = strchr(tmp, '/');
	if (token != NULL) {
		*token = '\0';
		(void) strlcpy(zonename, tmp, ZONENAME_MAX);
		token++;
		(void) strlcpy(fname, token, MAXFLOWNAMELEN);
		if (strcmp(zonename, "global") == 0) {
			inputzoneid = GLOBAL_ZONEID;
		} else {
			zone = zone_find_by_name(zonename);
			if (zone == NULL)
				return (ENOTSUP);
			inputzoneid = zone->zone_id;
			zone_rele(zone);
		}
	} else {
		inputzoneid = curzoneid;
		(void) strlcpy(fname, flow_name, MAXFLOWNAMELEN);
	}

	/* ngz can not show another ngz's flows */
	if ((curzoneid != GLOBAL_ZONEID) && (curzoneid != inputzoneid) &&
	    (inputzoneid != GLOBAL_ZONEID))
		return (ENOTSUP);

	err = mac_flow_lookup_byname(fname, &flent, inputzoneid);
	if (err != 0)
		return (err);

	mac_link_flowinfo_copy(finfo, flent);
	FLOW_USER_REFRELE(flent);
	return (0);
}

/*
 * Hash function macro that takes an Ethernet address and VLAN id as input.
 */
#define	HASH_ETHER_VID(a, v, s)	\
	((((uint32_t)(a)[3] + (a)[4] + (a)[5]) ^ (v)) % (s))

/*
 * Generic layer-2 address hashing function that takes an address and address
 * length as input.  This is the DJB hash function.
 */
static uint32_t
flow_l2_addrhash(uint8_t *addr, size_t addrlen, size_t htsize)
{
	uint32_t	hash = 5381;
	size_t		i;

	for (i = 0; i < addrlen; i++)
		hash = ((hash << 5) + hash) + addr[i];
	return (hash % htsize);
}

#define	PKT_TOO_SMALL(s, end) ((s)->fs_mp->b_wptr < (end))

#define	CHECK_AND_ADJUST_START_PTR(s, start) {		\
	if ((s)->fs_mp->b_wptr == (start)) {		\
		mblk_t	*next = (s)->fs_mp->b_cont;	\
		if (next == NULL)			\
			return (EINVAL);		\
							\
		(s)->fs_mp = next;			\
		(start) = next->b_rptr;			\
	}						\
}

/* ARGSUSED */
static boolean_t
flow_l2_match(flow_tab_t *ft, flow_entry_t *flent, flow_state_t *s)
{
	flow_l2info_t		*l2 = &s->fs_l2info;
	flow_desc_t		*fd = &flent->fe_flow_desc;

	return (l2->l2_vid == fd->fd_vid &&
	    bcmp(l2->l2_daddr, fd->fd_dst_mac, fd->fd_mac_len) == 0);
}

/*
 * Layer 2 hash function.
 * Must be paired with flow_l2_accept() within a set of flow_ops
 * because it assumes the dest address is already extracted.
 */
static uint32_t
flow_l2_hash(flow_tab_t *ft, flow_state_t *s)
{
	return (flow_l2_addrhash(s->fs_l2info.l2_daddr,
	    ft->ft_mip->mi_type->mt_addr_length, ft->ft_size));
}

/*
 * This is the generic layer 2 accept function.
 * It makes use of mac_header_info() to extract the header length,
 * sap, vlan ID and destination address.
 */
static int
flow_l2_accept(flow_tab_t *ft, flow_state_t *s)
{
	boolean_t		is_ether;
	flow_l2info_t		*l2 = &s->fs_l2info;
	mac_header_info_t	mhi;
	int			err;

	is_ether = (ft->ft_mip->mi_info.mi_nativemedia == DL_ETHER);
	if ((err = mac_header_info((mac_handle_t)ft->ft_mip,
	    s->fs_mp, &mhi)) != 0) {
		if (err == EINVAL)
			err = ENOBUFS;

		return (err);
	}

	l2->l2_start = s->fs_mp->b_rptr;
	l2->l2_daddr = (uint8_t *)mhi.mhi_daddr;

	if (is_ether && mhi.mhi_bindsap == ETHERTYPE_VLAN &&
	    ((s->fs_flags & FLOW_IGNORE_VLAN) == 0)) {
		struct ether_vlan_header	*evhp =
		    (struct ether_vlan_header *)l2->l2_start;

		if (PKT_TOO_SMALL(s, l2->l2_start + sizeof (*evhp)))
			return (ENOBUFS);

		l2->l2_sap = ntohs(evhp->ether_type);
		l2->l2_vid = VLAN_ID(ntohs(evhp->ether_tci));
		l2->l2_hdrsize = sizeof (*evhp);
	} else {
		l2->l2_sap = mhi.mhi_bindsap;
		l2->l2_vid = 0;
		l2->l2_hdrsize = (uint32_t)mhi.mhi_hdrsize;
	}
	return (0);
}

/*
 * flow_ether_hash()/accept() are optimized versions of flow_l2_hash()/
 * accept(). The notable difference is that dest address is now extracted
 * by hash() rather than by accept(). This saves a few memory references
 * for flow tables that do not care about mac addresses.
 */
static uint32_t
flow_ether_hash(flow_tab_t *ft, flow_state_t *s)
{
	flow_l2info_t			*l2 = &s->fs_l2info;
	struct ether_vlan_header	*evhp;

	evhp = (struct ether_vlan_header *)l2->l2_start;
	l2->l2_daddr = evhp->ether_dhost.ether_addr_octet;
	return (HASH_ETHER_VID(l2->l2_daddr, l2->l2_vid, ft->ft_size));
}

static uint32_t
flow_ether_hash_fe(flow_tab_t *ft, flow_entry_t *flent)
{
	flow_desc_t	*fd = &flent->fe_flow_desc;

	ASSERT((fd->fd_mask & FLOW_LINK_VID) != 0 || fd->fd_vid == 0);
	return (HASH_ETHER_VID(fd->fd_dst_mac, fd->fd_vid, ft->ft_size));
}

/* ARGSUSED */
static int
flow_ether_accept(flow_tab_t *ft, flow_state_t *s)
{
	flow_l2info_t			*l2 = &s->fs_l2info;
	struct ether_vlan_header	*evhp;
	uint16_t			sap;

	evhp = (struct ether_vlan_header *)s->fs_mp->b_rptr;
	l2->l2_start = (uchar_t *)evhp;

	if (PKT_TOO_SMALL(s, l2->l2_start + sizeof (struct ether_header)))
		return (ENOBUFS);

	if ((sap = ntohs(evhp->ether_tpid)) == ETHERTYPE_VLAN &&
	    ((s->fs_flags & FLOW_IGNORE_VLAN) == 0)) {
		if (PKT_TOO_SMALL(s, l2->l2_start + sizeof (*evhp)))
			return (ENOBUFS);

		l2->l2_sap = ntohs(evhp->ether_type);
		l2->l2_vid = VLAN_ID(ntohs(evhp->ether_tci));
		l2->l2_hdrsize = sizeof (struct ether_vlan_header);
	} else {
		l2->l2_sap = sap;
		l2->l2_vid = 0;
		l2->l2_hdrsize = sizeof (struct ether_header);
	}
	return (0);
}

/*
 * Validates a layer 2 flow entry.
 */
static int
flow_l2_accept_fe(flow_tab_t *ft, flow_entry_t *flent)
{
	flow_desc_t	*fd = &flent->fe_flow_desc;

	/*
	 * Dest address is mandatory, and 0 length addresses are not yet
	 * supported.
	 */
	if ((fd->fd_mask & FLOW_LINK_DST) == 0 || fd->fd_mac_len == 0)
		return (EINVAL);

	if ((fd->fd_mask & FLOW_LINK_VID) != 0) {
		/*
		 * VLAN flows are only supported over ethernet macs.
		 */
		if (ft->ft_mip->mi_info.mi_nativemedia != DL_ETHER)
			return (EINVAL);

		if (fd->fd_vid == 0)
			return (EINVAL);

	}
	flent->fe_match = flow_l2_match;
	return (0);
}

/*
 * Calculates hash index of flow entry.
 */
static uint32_t
flow_l2_hash_fe(flow_tab_t *ft, flow_entry_t *flent)
{
	flow_desc_t	*fd = &flent->fe_flow_desc;

	ASSERT((fd->fd_mask & FLOW_LINK_VID) == 0 && fd->fd_vid == 0);
	return (flow_l2_addrhash(fd->fd_dst_mac,
	    ft->ft_mip->mi_type->mt_addr_length, ft->ft_size));
}

/*
 * This is used for duplicate flow checking.
 */
/* ARGSUSED */
static boolean_t
flow_l2_match_fe(flow_tab_t *ft, flow_entry_t *f1, flow_entry_t *f2)
{
	flow_desc_t	*fd1 = &f1->fe_flow_desc, *fd2 = &f2->fe_flow_desc;

	ASSERT(fd1->fd_mac_len == fd2->fd_mac_len && fd1->fd_mac_len != 0);
	return (bcmp(&fd1->fd_dst_mac, &fd2->fd_dst_mac,
	    fd1->fd_mac_len) == 0 && fd1->fd_vid == fd2->fd_vid);
}

/*
 * Generic flow entry insertion function.
 * Used by flow tables that do not have ordering requirements.
 */
/* ARGSUSED */
static int
flow_generic_insert_fe(flow_tab_t *ft, flow_entry_t **headp,
    flow_entry_t *flent)
{
	ASSERT(MAC_PERIM_HELD((mac_handle_t)ft->ft_mip));

	if (*headp != NULL) {
		ASSERT(flent->fe_next == NULL);
		flent->fe_next = *headp;
	}
	*headp = flent;
	return (0);
}

/*
 * IP version independent DSField matching function.
 */
/* ARGSUSED */
static boolean_t
flow_ip_dsfield_match(flow_tab_t *ft, flow_entry_t *flent, flow_state_t *s)
{
	flow_l3info_t	*l3info = &s->fs_l3info;
	flow_desc_t	*fd = &flent->fe_flow_desc;

	switch (l3info->l3_version) {
	case IPV4_VERSION: {
		ipha_t		*ipha = (ipha_t *)l3info->l3_start;

		return ((ipha->ipha_type_of_service &
		    fd->fd_dsfield_mask) == fd->fd_dsfield);
	}
	case IPV6_VERSION: {
		ip6_t		*ip6h = (ip6_t *)l3info->l3_start;

		return ((IPV6_FLOW_TCLASS(ip6h->ip6_vcf) &
		    fd->fd_dsfield_mask) == fd->fd_dsfield);
	}
	default:
		return (B_FALSE);
	}
}

/*
 * IP v4 and v6 address matching.
 * The netmask only needs to be applied on the packet but not on the
 * flow_desc since fd_local_addr/fd_remote_addr are premasked subnets.
 */

/* ARGSUSED */
static boolean_t
flow_ip_v4_match(flow_tab_t *ft, flow_entry_t *flent, flow_state_t *s)
{
	flow_l3info_t	*l3info = &s->fs_l3info;
	flow_desc_t	*fd = &flent->fe_flow_desc;
	ipha_t		*ipha = (ipha_t *)l3info->l3_start;
	in_addr_t	addr;

	addr = (l3info->l3_dst_or_src ? ipha->ipha_dst : ipha->ipha_src);
	if ((fd->fd_mask & FLOW_IP_LOCAL) != 0) {
		return ((addr & V4_PART_OF_V6(fd->fd_local_netmask)) ==
		    V4_PART_OF_V6(fd->fd_local_addr));
	}
	return ((addr & V4_PART_OF_V6(fd->fd_remote_netmask)) ==
	    V4_PART_OF_V6(fd->fd_remote_addr));
}

/* ARGSUSED */
static boolean_t
flow_ip_v6_match(flow_tab_t *ft, flow_entry_t *flent, flow_state_t *s)
{
	flow_l3info_t	*l3info = &s->fs_l3info;
	flow_desc_t	*fd = &flent->fe_flow_desc;
	ip6_t		*ip6h = (ip6_t *)l3info->l3_start;
	in6_addr_t	*addrp;

	addrp = (l3info->l3_dst_or_src ? &ip6h->ip6_dst : &ip6h->ip6_src);
	if ((fd->fd_mask & FLOW_IP_LOCAL) != 0) {
		return (V6_MASK_EQ(*addrp, fd->fd_local_netmask,
		    fd->fd_local_addr));
	}
	return (V6_MASK_EQ(*addrp, fd->fd_remote_netmask, fd->fd_remote_addr));
}

/* ARGSUSED */
static boolean_t
flow_ip_proto_match(flow_tab_t *ft, flow_entry_t *flent, flow_state_t *s)
{
	flow_l3info_t	*l3info = &s->fs_l3info;
	flow_desc_t	*fd = &flent->fe_flow_desc;

	return (l3info->l3_protocol == fd->fd_protocol);
}

static uint32_t
flow_ip_hash(flow_tab_t *ft, flow_state_t *s)
{
	flow_l3info_t	*l3info = &s->fs_l3info;
	flow_mask_t	mask = ft->ft_mask;

	if ((mask & FLOW_IP_LOCAL) != 0) {
		l3info->l3_dst_or_src = ((s->fs_flags & FLOW_INBOUND) != 0);
	} else if ((mask & FLOW_IP_REMOTE) != 0) {
		l3info->l3_dst_or_src = ((s->fs_flags & FLOW_OUTBOUND) != 0);
	} else if ((mask & FLOW_IP_DSFIELD) != 0) {
		/*
		 * DSField flents are arranged as a single list.
		 */
		return (0);
	}
	/*
	 * IP addr flents are hashed into two lists, v4 or v6.
	 */
	ASSERT(ft->ft_size >= 2);
	return ((l3info->l3_version == IPV4_VERSION) ? 0 : 1);
}

static uint32_t
flow_ip_proto_hash(flow_tab_t *ft, flow_state_t *s)
{
	flow_l3info_t	*l3info = &s->fs_l3info;

	return (l3info->l3_protocol % ft->ft_size);
}

/* ARGSUSED */
static int
flow_ip_accept(flow_tab_t *ft, flow_state_t *s)
{
	flow_l2info_t	*l2info = &s->fs_l2info;
	flow_l3info_t	*l3info = &s->fs_l3info;
	uint16_t	sap = l2info->l2_sap;
	uchar_t		*l3_start;

	l3_start = l2info->l2_start + l2info->l2_hdrsize;

	/*
	 * Adjust start pointer if we're at the end of an mblk.
	 */
	CHECK_AND_ADJUST_START_PTR(s, l3_start);

	l3info->l3_start = l3_start;
	if (!OK_32PTR(l3_start))
		return (EINVAL);

	switch (sap) {
	case ETHERTYPE_IP: {
		ipha_t	*ipha = (ipha_t *)l3_start;

		if (PKT_TOO_SMALL(s, l3_start + IP_SIMPLE_HDR_LENGTH))
			return (ENOBUFS);

		l3info->l3_hdrsize = IPH_HDR_LENGTH(ipha);
		l3info->l3_protocol = ipha->ipha_protocol;
		l3info->l3_version = IPV4_VERSION;
		l3info->l3_fragmented =
		    IS_V4_FRAGMENT(ipha->ipha_fragment_offset_and_flags);
		break;
	}
	case ETHERTYPE_IPV6: {
		ip6_t		*ip6h = (ip6_t *)l3_start;
		ip6_frag_t	*frag = NULL;
		uint16_t	ip6_hdrlen;
		uint8_t		nexthdr;

		if (!mac_ip_hdr_length_v6(ip6h, s->fs_mp->b_wptr, &ip6_hdrlen,
		    &nexthdr, &frag)) {
			return (ENOBUFS);
		}
		l3info->l3_hdrsize = ip6_hdrlen;
		l3info->l3_protocol = nexthdr;
		l3info->l3_version = IPV6_VERSION;
		l3info->l3_fragmented = (frag != NULL);
		break;
	}
	default:
		return (EINVAL);
	}
	return (0);
}

/* ARGSUSED */
static int
flow_ip_proto_accept_fe(flow_tab_t *ft, flow_entry_t *flent)
{
	flow_desc_t	*fd = &flent->fe_flow_desc;

	switch (fd->fd_protocol) {
	case IPPROTO_TCP:
	case IPPROTO_UDP:
	case IPPROTO_SCTP:
	case IPPROTO_ICMP:
	case IPPROTO_ICMPV6:
		flent->fe_match = flow_ip_proto_match;
		return (0);
	default:
		return (EINVAL);
	}
}

/* ARGSUSED */
static int
flow_ip_accept_fe(flow_tab_t *ft, flow_entry_t *flent)
{
	flow_desc_t	*fd = &flent->fe_flow_desc;
	flow_mask_t	mask;
	uint8_t		version;
	in6_addr_t	*addr, *netmask;

	/*
	 * DSField does not require a IP version.
	 */
	if (fd->fd_mask == FLOW_IP_DSFIELD) {
		if (fd->fd_dsfield_mask == 0)
			return (EINVAL);

		flent->fe_match = flow_ip_dsfield_match;
		return (0);
	}

	/*
	 * IP addresses must come with a version to avoid ambiguity.
	 */
	if ((fd->fd_mask & FLOW_IP_VERSION) == 0)
		return (EINVAL);

	version = fd->fd_ipversion;
	if (version != IPV4_VERSION && version != IPV6_VERSION)
		return (EINVAL);

	mask = fd->fd_mask & ~FLOW_IP_VERSION;
	switch (mask) {
	case FLOW_IP_LOCAL:
		addr = &fd->fd_local_addr;
		netmask = &fd->fd_local_netmask;
		break;
	case FLOW_IP_REMOTE:
		addr = &fd->fd_remote_addr;
		netmask = &fd->fd_remote_netmask;
		break;
	default:
		return (EINVAL);
	}

	/*
	 * Apply netmask onto specified address.
	 */
	V6_MASK_COPY(*addr, *netmask, *addr);
	if (version == IPV4_VERSION) {
		ipaddr_t	v4addr = V4_PART_OF_V6((*addr));
		ipaddr_t	v4mask = V4_PART_OF_V6((*netmask));

		if (v4addr == 0 || v4mask == 0)
			return (EINVAL);
		flent->fe_match = flow_ip_v4_match;
	} else {
		if (IN6_IS_ADDR_UNSPECIFIED(addr) ||
		    IN6_IS_ADDR_UNSPECIFIED(netmask))
			return (EINVAL);
		flent->fe_match = flow_ip_v6_match;
	}
	return (0);
}

static uint32_t
flow_ip_proto_hash_fe(flow_tab_t *ft, flow_entry_t *flent)
{
	flow_desc_t	*fd = &flent->fe_flow_desc;

	return (fd->fd_protocol % ft->ft_size);
}

static uint32_t
flow_ip_hash_fe(flow_tab_t *ft, flow_entry_t *flent)
{
	flow_desc_t	*fd = &flent->fe_flow_desc;

	/*
	 * DSField flents are arranged as a single list.
	 */
	if ((fd->fd_mask & FLOW_IP_DSFIELD) != 0)
		return (0);

	/*
	 * IP addr flents are hashed into two lists, v4 or v6.
	 */
	ASSERT(ft->ft_size >= 2);
	return ((fd->fd_ipversion == IPV4_VERSION) ? 0 : 1);
}

/* ARGSUSED */
static boolean_t
flow_ip_proto_match_fe(flow_tab_t *ft, flow_entry_t *f1, flow_entry_t *f2)
{
	flow_desc_t	*fd1 = &f1->fe_flow_desc, *fd2 = &f2->fe_flow_desc;

	return (fd1->fd_protocol == fd2->fd_protocol);
}

/* ARGSUSED */
static boolean_t
flow_ip_match_fe(flow_tab_t *ft, flow_entry_t *f1, flow_entry_t *f2)
{
	flow_desc_t	*fd1 = &f1->fe_flow_desc, *fd2 = &f2->fe_flow_desc;
	in6_addr_t	*a1, *m1, *a2, *m2;

	ASSERT(fd1->fd_mask == fd2->fd_mask);
	if (fd1->fd_mask == FLOW_IP_DSFIELD) {
		return (fd1->fd_dsfield == fd2->fd_dsfield &&
		    fd1->fd_dsfield_mask == fd2->fd_dsfield_mask);
	}

	/*
	 * flow_ip_accept_fe() already validated the version.
	 */
	ASSERT((fd1->fd_mask & FLOW_IP_VERSION) != 0);
	if (fd1->fd_ipversion != fd2->fd_ipversion)
		return (B_FALSE);

	switch (fd1->fd_mask & ~FLOW_IP_VERSION) {
	case FLOW_IP_LOCAL:
		a1 = &fd1->fd_local_addr;
		m1 = &fd1->fd_local_netmask;
		a2 = &fd2->fd_local_addr;
		m2 = &fd2->fd_local_netmask;
		break;
	case FLOW_IP_REMOTE:
		a1 = &fd1->fd_remote_addr;
		m1 = &fd1->fd_remote_netmask;
		a2 = &fd2->fd_remote_addr;
		m2 = &fd2->fd_remote_netmask;
		break;
	default:
		/*
		 * This is unreachable given the checks in
		 * flow_ip_accept_fe().
		 */
		return (B_FALSE);
	}

	if (fd1->fd_ipversion == IPV4_VERSION) {
		return (V4_PART_OF_V6((*a1)) == V4_PART_OF_V6((*a2)) &&
		    V4_PART_OF_V6((*m1)) == V4_PART_OF_V6((*m2)));

	} else {
		return (IN6_ARE_ADDR_EQUAL(a1, a2) &&
		    IN6_ARE_ADDR_EQUAL(m1, m2));
	}
}

static int
flow_ip_mask2plen(in6_addr_t *v6mask)
{
	int		bits;
	int		plen = IPV6_ABITS;
	int		i;

	for (i = 3; i >= 0; i--) {
		if (v6mask->s6_addr32[i] == 0) {
			plen -= 32;
			continue;
		}
		bits = ffs(ntohl(v6mask->s6_addr32[i])) - 1;
		if (bits == 0)
			break;
		plen -= bits;
	}
	return (plen);
}

/* ARGSUSED */
static int
flow_ip_insert_fe(flow_tab_t *ft, flow_entry_t **headp,
    flow_entry_t *flent)
{
	flow_entry_t	**p = headp;
	flow_desc_t	*fd0, *fd;
	in6_addr_t	*m0, *m;
	int		plen0, plen;

	ASSERT(MAC_PERIM_HELD((mac_handle_t)ft->ft_mip));

	/*
	 * No special ordering needed for dsfield.
	 */
	fd0 = &flent->fe_flow_desc;
	if ((fd0->fd_mask & FLOW_IP_DSFIELD) != 0) {
		if (*p != NULL) {
			ASSERT(flent->fe_next == NULL);
			flent->fe_next = *p;
		}
		*p = flent;
		return (0);
	}

	/*
	 * IP address flows are arranged in descending prefix length order.
	 */
	m0 = ((fd0->fd_mask & FLOW_IP_LOCAL) != 0) ?
	    &fd0->fd_local_netmask : &fd0->fd_remote_netmask;
	plen0 = flow_ip_mask2plen(m0);
	ASSERT(plen0 != 0);

	for (; *p != NULL; p = &(*p)->fe_next) {
		fd = &(*p)->fe_flow_desc;

		/*
		 * Normally a dsfield flent shouldn't end up on the same
		 * list as an IP address because flow tables are (for now)
		 * disjoint. If we decide to support both IP and dsfield
		 * in the same table in the future, this check will allow
		 * for that.
		 */
		if ((fd->fd_mask & FLOW_IP_DSFIELD) != 0)
			continue;

		/*
		 * We also allow for the mixing of local and remote address
		 * flents within one list.
		 */
		m = ((fd->fd_mask & FLOW_IP_LOCAL) != 0) ?
		    &fd->fd_local_netmask : &fd->fd_remote_netmask;
		plen = flow_ip_mask2plen(m);

		if (plen <= plen0)
			break;
	}
	if (*p != NULL) {
		ASSERT(flent->fe_next == NULL);
		flent->fe_next = *p;
	}
	*p = flent;
	return (0);
}

/*
 * Transport layer protocol and port matching functions.
 */

/* ARGSUSED */
static boolean_t
flow_transport_lport_match(flow_tab_t *ft, flow_entry_t *flent, flow_state_t *s)
{
	flow_l3info_t	*l3info = &s->fs_l3info;
	flow_l4info_t	*l4info = &s->fs_l4info;
	flow_desc_t	*fd = &flent->fe_flow_desc;

	return (fd->fd_protocol == l3info->l3_protocol &&
	    fd->fd_local_port == l4info->l4_hash_port);
}

/* ARGSUSED */
static boolean_t
flow_transport_rport_match(flow_tab_t *ft, flow_entry_t *flent, flow_state_t *s)
{
	flow_l3info_t	*l3info = &s->fs_l3info;
	flow_l4info_t	*l4info = &s->fs_l4info;
	flow_desc_t	*fd = &flent->fe_flow_desc;

	return (fd->fd_protocol == l3info->l3_protocol &&
	    fd->fd_remote_port == l4info->l4_hash_port);
}

/*
 * Transport hash function.
 * Since we only support either local or remote port flows,
 * we only need to extract one of the ports to be used for
 * matching.
 */
static uint32_t
flow_transport_hash(flow_tab_t *ft, flow_state_t *s)
{
	flow_l3info_t	*l3info = &s->fs_l3info;
	flow_l4info_t	*l4info = &s->fs_l4info;
	uint8_t		proto = l3info->l3_protocol;
	boolean_t	dst_or_src;

	if ((ft->ft_mask & FLOW_ULP_PORT_LOCAL) != 0) {
		dst_or_src = ((s->fs_flags & FLOW_INBOUND) != 0);
	} else {
		dst_or_src = ((s->fs_flags & FLOW_OUTBOUND) != 0);
	}

	l4info->l4_hash_port = dst_or_src ? l4info->l4_dst_port :
	    l4info->l4_src_port;

	return ((l4info->l4_hash_port ^ (proto << 4)) % ft->ft_size);
}

/*
 * Unlike other accept() functions above, we do not need to get the header
 * size because this is our highest layer so far. If we want to do support
 * other higher layer protocols, we would need to save the l4_hdrsize
 * in the code below.
 */

/* ARGSUSED */
static int
flow_transport_accept(flow_tab_t *ft, flow_state_t *s)
{
	flow_l3info_t	*l3info = &s->fs_l3info;
	flow_l4info_t	*l4info = &s->fs_l4info;
	uint8_t		proto = l3info->l3_protocol;
	uchar_t		*l4_start;

	l4_start = l3info->l3_start + l3info->l3_hdrsize;

	/*
	 * Adjust start pointer if we're at the end of an mblk.
	 */
	CHECK_AND_ADJUST_START_PTR(s, l4_start);

	l4info->l4_start = l4_start;
	if (!OK_32PTR(l4_start))
		return (EINVAL);

	if (l3info->l3_fragmented == B_TRUE)
		return (EINVAL);

	switch (proto) {
	case IPPROTO_TCP: {
		struct tcphdr	*tcph = (struct tcphdr *)l4_start;

		if (PKT_TOO_SMALL(s, l4_start + sizeof (*tcph)))
			return (ENOBUFS);

		l4info->l4_src_port = tcph->th_sport;
		l4info->l4_dst_port = tcph->th_dport;
		break;
	}
	case IPPROTO_UDP: {
		struct udphdr	*udph = (struct udphdr *)l4_start;

		if (PKT_TOO_SMALL(s, l4_start + sizeof (*udph)))
			return (ENOBUFS);

		l4info->l4_src_port = udph->uh_sport;
		l4info->l4_dst_port = udph->uh_dport;
		break;
	}
	case IPPROTO_SCTP: {
		sctp_hdr_t	*sctph = (sctp_hdr_t *)l4_start;

		if (PKT_TOO_SMALL(s, l4_start + sizeof (*sctph)))
			return (ENOBUFS);

		l4info->l4_src_port = sctph->sh_sport;
		l4info->l4_dst_port = sctph->sh_dport;
		break;
	}
	default:
		return (EINVAL);
	}

	return (0);
}

/*
 * Validates transport flow entry.
 * The protocol field must be present.
 */

/* ARGSUSED */
static int
flow_transport_accept_fe(flow_tab_t *ft, flow_entry_t *flent)
{
	flow_desc_t	*fd = &flent->fe_flow_desc;
	flow_mask_t	mask = fd->fd_mask;

	if ((mask & FLOW_IP_PROTOCOL) == 0)
		return (EINVAL);

	switch (fd->fd_protocol) {
	case IPPROTO_TCP:
	case IPPROTO_UDP:
	case IPPROTO_SCTP:
		break;
	default:
		return (EINVAL);
	}

	switch (mask & ~FLOW_IP_PROTOCOL) {
	case FLOW_ULP_PORT_LOCAL:
		if (fd->fd_local_port == 0)
			return (EINVAL);

		flent->fe_match = flow_transport_lport_match;
		break;
	case FLOW_ULP_PORT_REMOTE:
		if (fd->fd_remote_port == 0)
			return (EINVAL);

		flent->fe_match = flow_transport_rport_match;
		break;
	case 0:
		/*
		 * transport-only flows conflicts with our table type.
		 */
		return (EOPNOTSUPP);
	default:
		return (EINVAL);
	}

	return (0);
}

static uint32_t
flow_transport_hash_fe(flow_tab_t *ft, flow_entry_t *flent)
{
	flow_desc_t	*fd = &flent->fe_flow_desc;
	uint16_t	port = 0;

	port = ((fd->fd_mask & FLOW_ULP_PORT_LOCAL) != 0) ?
	    fd->fd_local_port : fd->fd_remote_port;

	return ((port ^ (fd->fd_protocol << 4)) % ft->ft_size);
}

/* ARGSUSED */
static boolean_t
flow_transport_match_fe(flow_tab_t *ft, flow_entry_t *f1, flow_entry_t *f2)
{
	flow_desc_t	*fd1 = &f1->fe_flow_desc, *fd2 = &f2->fe_flow_desc;

	if (fd1->fd_protocol != fd2->fd_protocol)
		return (B_FALSE);

	if ((fd1->fd_mask & FLOW_ULP_PORT_LOCAL) != 0)
		return (fd1->fd_local_port == fd2->fd_local_port);

	if ((fd1->fd_mask & FLOW_ULP_PORT_REMOTE) != 0)
		return (fd1->fd_remote_port == fd2->fd_remote_port);

	return (B_TRUE);
}

static flow_ops_t flow_l2_ops = {
	flow_l2_accept_fe,
	flow_l2_hash_fe,
	flow_l2_match_fe,
	flow_generic_insert_fe,
	flow_l2_hash,
	{flow_l2_accept}
};

static flow_ops_t flow_ip_ops = {
	flow_ip_accept_fe,
	flow_ip_hash_fe,
	flow_ip_match_fe,
	flow_ip_insert_fe,
	flow_ip_hash,
	{flow_l2_accept, flow_ip_accept}
};

static flow_ops_t flow_ip_proto_ops = {
	flow_ip_proto_accept_fe,
	flow_ip_proto_hash_fe,
	flow_ip_proto_match_fe,
	flow_generic_insert_fe,
	flow_ip_proto_hash,
	{flow_l2_accept, flow_ip_accept}
};

static flow_ops_t flow_transport_ops = {
	flow_transport_accept_fe,
	flow_transport_hash_fe,
	flow_transport_match_fe,
	flow_generic_insert_fe,
	flow_transport_hash,
	{flow_l2_accept, flow_ip_accept, flow_transport_accept}
};

static flow_tab_info_t flow_tab_info_list[] = {
	{&flow_ip_ops, FLOW_IP_VERSION | FLOW_IP_LOCAL, 2},
	{&flow_ip_ops, FLOW_IP_VERSION | FLOW_IP_REMOTE, 2},
	{&flow_ip_ops, FLOW_IP_DSFIELD, 1},
	{&flow_ip_proto_ops, FLOW_IP_PROTOCOL, 256},
	{&flow_transport_ops, FLOW_IP_PROTOCOL | FLOW_ULP_PORT_LOCAL, 1024},
	{&flow_transport_ops, FLOW_IP_PROTOCOL | FLOW_ULP_PORT_REMOTE, 1024}
};

#define	FLOW_MAX_TAB_INFO \
	((sizeof (flow_tab_info_list)) / sizeof (flow_tab_info_t))

static flow_tab_info_t *
mac_flow_tab_info_get(flow_mask_t mask)
{
	int	i;

	for (i = 0; i < FLOW_MAX_TAB_INFO; i++) {
		if (mask == flow_tab_info_list[i].fti_mask)
			return (&flow_tab_info_list[i]);
	}
	return (NULL);
}
