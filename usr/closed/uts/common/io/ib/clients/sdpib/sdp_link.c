/*
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/stream.h>
#include <sys/dlpi.h>
#include <sys/dls.h>
#include <sys/stropts.h>
#include <sys/strsun.h>
#include <sys/sysmacros.h>
#include <sys/strlog.h>
#include <sys/ddi.h>
#include <sys/cmn_err.h>
#include <sys/socket.h>
#include <sys/tihdr.h>
#include <net/if.h>
#include <inet/ip2mac.h>
#include <net/if_types.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <sys/sockio.h>
#include <netinet/icmp6.h>
#include <sys/ethernet.h>
#include <inet/ip.h>
#include <inet/ip_ire.h>
#include <inet/ip_ftable.h>
#include <sys/dls.h>
#include <sys/disp.h>

#include <sys/ib/clients/sdpib/sdp_main.h>
#include <sys/ib/clients/sdpib/sdp_trace_util.h>

extern sdp_state_t *sdp_global_state;
extern int sdp_pr_cache;

#define	SDP_RTM_LEN 	0x158

static void sdp_resolver_timeout(void *arg);
static int sdp_check_prwqn(sdp_prwqn_t *wqnp);
static void sdp_prwqn_delete(sdp_prwqn_t *wqnp);
static void sdp_prwqn_insert(sdp_prwqn_t *wqnp);
static int sdp_get_hca_info(sdp_prwqn_t *wqnp);
static void sdp_pr_callback(sdp_prwqn_t *wqnp, int status);
static void sdp_pr_loopback(sdp_prwqn_t *wqnp, sdp_conn_t *conn);
static int  sdp_pr_resolver_query(sdp_prwqn_t *, ill_t *, zoneid_t);
static void sdp_pr_resolver_ack(ip2mac_t *, sdp_prwqn_t *, boolean_t);
static boolean_t sdp_check_sockdl(struct sockaddr_dl *);
static uint16_t sdp_get_ibd_pkey(const char *ifname, zoneid_t zoneid);

/*
 * callback for resolver lookups, both for success and failure.
 * common code for IPv4 and IPv6.
 */
static void
sdp_resolver_callback_main(ip2mac_t *ip2macp, sdp_prwqn_t *wqnp)
{
	if (ip2macp->ip2mac_err == 0)
		sdp_pr_resolver_ack(ip2macp, wqnp, B_FALSE);
	else
		sdp_resolver_timeout(wqnp);
}

static void
sdp_resolver_callback_async(void *arg)
{
	sdp_prwqn_t *wqnp = arg;
	ip2mac_t *ip2macp;

	ip2macp = wqnp->ip2macp;
	ASSERT(ip2macp != NULL);
	wqnp->ip2macp = NULL;
	sdp_resolver_callback_main(ip2macp, wqnp);
	kmem_free(ip2macp, sizeof (ip2mac_t));
}

/*
 * This can be called from interrupt context
 * and I.B> can hang any locks are held. So we need to queue
 */
static void
sdp_resolver_callback(ip2mac_t *ip2macp, void *arg)
{
	sdp_prwqn_t *wqnp = arg;

	if (curthread->t_flag & T_INTR_THREAD) {
		/*
		 * If we are in interrupt conext
		 * copy all the info and than proceed
		 * asynchronously
		 */
		wqnp->ip2macp = kmem_zalloc(sizeof (ip2mac_t), KM_NOSLEEP);
		if (wqnp->ip2macp == NULL) {
			/*
			 * This needs to be fixed.
			 */
			return;
		}
		*(wqnp->ip2macp) = *ip2macp;
		(void) taskq_dispatch(system_taskq,
		    sdp_resolver_callback_async, arg, TQ_SLEEP);
	} else {
		sdp_resolver_callback_main(ip2macp, wqnp);
	}
}
/*
 * query the ipv6 driver cache for ipv6 to mac address mapping.
 */
static int
sdp_ndp_lookup(sdp_prwqn_t *wqnp, ill_t *ill, zoneid_t zoneid)
{
	ip2mac_t ip2m;
	sin6_t *sin6;
	ip2mac_id_t ip2mid;
	int err;

	ASSERT(SDP_IS_V6_ADDR(&wqnp->src_addr));

	bzero(&ip2m, sizeof (ip2m));
	sin6 = (sin6_t *)&ip2m.ip2mac_pa;
	sin6->sin6_family = AF_INET6;
	sin6->sin6_addr = wqnp->dst_addr.ip6addr;
	ip2m.ip2mac_ifindex = ill->ill_phyint->phyint_ifindex;
	/*
	 * XXX XTBD set the scopeid?
	 * issue the request to IP for Neighbor Discovery
	 */
	ip2mid = ip2mac(IP2MAC_RESOLVE, &ip2m, sdp_resolver_callback,
	    wqnp, zoneid);
	err = ip2m.ip2mac_err;
	if (err == EINPROGRESS) {
		wqnp->ip2mac_id = ip2mid;
		wqnp->wqn_zoneid = zoneid;
		wqnp->flags |= SDP_PR_RESOLVE_PENDING;
		err = 0;
	} else if (err == 0) {
		sdp_pr_resolver_ack(&ip2m, wqnp, B_TRUE);
	}
	return (err);
}

/*
 * Resolve the IPv4 dst_addr to the mac address.
 */
static int
sdp_arp_lookup(sdp_prwqn_t *wqnp, ill_t *ill, zoneid_t zoneid)
{
	ip2mac_t ip2m;
	sin_t *sin;
	ip2mac_id_t ip2mid;
	int err;

	ASSERT(SDP_IS_V4_ADDR(&wqnp->src_addr));

	bzero(&ip2m, sizeof (ip2m));
	sin = (sin_t *)&ip2m.ip2mac_pa;
	sin->sin_family = AF_INET;
	sin->sin_addr.s_addr = wqnp->dst_addr.ip4addr;
	ip2m.ip2mac_ifindex = ill->ill_phyint->phyint_ifindex;
	/*
	 * issue the request to IP for ARP
	 */
	ip2mid = ip2mac(IP2MAC_RESOLVE, &ip2m, sdp_resolver_callback,
	    wqnp, zoneid);
	err = ip2m.ip2mac_err;
	if (ip2m.ip2mac_err == EINPROGRESS) {
		wqnp->ip2mac_id = ip2mid;
		wqnp->flags |= SDP_PR_RESOLVE_PENDING;
		wqnp->wqn_zoneid = zoneid;
		err = 0;
	} else if (ip2m.ip2mac_err == 0) {
		sdp_pr_resolver_ack(&ip2m, wqnp, B_TRUE);
	}
	return (err);
}

/*
 * timeout routine when there is no response to arp/ndp resolver request.
 */
static void
sdp_resolver_timeout(void *arg)
{
	sdp_prwqn_t *wqnp = (sdp_prwqn_t *)arg;

	mutex_enter(&sdp_global_state->lookup_lock);

	/*
	 * make sure this is a valid request. the request could have been
	 * cancelled.
	 */
	if (sdp_check_prwqn(wqnp)) {
		mutex_exit(&sdp_global_state->lookup_lock);
		return;
	}
	wqnp->ip2mac_id = 0;
	wqnp->wqn_zoneid = -1;
	wqnp->flags &= ~SDP_PR_RESOLVE_PENDING;
	cv_broadcast(&wqnp->ip2mac_cv);
	/*
	 * indicate to user
	 */
	sdp_pr_callback(wqnp, EHOSTUNREACH);
	mutex_exit(&sdp_global_state->lookup_lock);
}

/*
 * delete a path record cache node.
 * called with lock acquired
 */
static void
sdp_prcn_delete(sdp_prcn_t *prcp)
{
	if (prcp->p_next) {
		if (prcp->next != NULL) {
			prcp->next->p_next = prcp->p_next;
		}
		*(prcp->p_next) = prcp->next;
		prcp->p_next = NULL;
		prcp->next = NULL;
	}
	kmem_cache_free(sdp_global_state->pr_kmcache, prcp);
}

/* ARGSUSED */
void
sdp_prcache_timer(void *arg)
{
	sdp_prcn_t 	*prcp;
	sdp_prcn_t	*n_prcp;

	mutex_enter(&sdp_global_state->lookup_lock);
	prcp = sdp_global_state->prc_head;
	while (prcp != NULL) {
		n_prcp = prcp->next;
		if (drv_hztousec(ddi_get_lbolt() - prcp->last_used_time) >
		    SDP_PR_CACHE_REAPING_AGE_USECS) {
			sdp_prcn_delete(prcp);
		}
		prcp = n_prcp;
	}

	/*
	 * rearm the timer
	 */
	sdp_global_state->prc_timeout_id = timeout(sdp_prcache_timer, NULL,
	    drv_usectohz(SDP_PR_CACHE_REAPING_AGE_USECS));
	mutex_exit(&sdp_global_state->lookup_lock);
}

/*
 * allocate a PR cache node and insert it in the list, copying the relevant
 * fields from wq node
 */
void
sdp_prcn_insert(sdp_prwqn_t *wqnp)
{
	sdp_prcn_t *prcp;

	/*
	 * if path record cache is disabled, return
	 */
	if (!sdp_pr_cache)
		return;

	if ((prcp = kmem_cache_alloc(sdp_global_state->pr_kmcache,
	    KM_NOSLEEP)) == NULL) {
		return;
	}

	/*
	 * copy the required fields
	 */
	prcp->dst_addr = wqnp->dst_addr;
	prcp->src_addr = wqnp->src_addr;
	prcp->hw_port = wqnp->hw_port;
	prcp->hca_hdl = wqnp->hca_hdl;
	prcp->pkey = wqnp->pkey;
	bcopy(&wqnp->path_attr, &prcp->path_attr, sizeof (ibt_path_attr_t));
	bcopy(&wqnp->path_info, &prcp->path_info, sizeof (ibt_path_info_t));
	prcp->last_used_time = ddi_get_lbolt();

	/*
	 * insert the node in the list
	 */
	prcp->next = sdp_global_state->prc_head;
	sdp_global_state->prc_head = prcp;
	prcp->p_next = &sdp_global_state->prc_head;
	if (prcp->next != NULL) {
		prcp->next->p_next = &prcp->next;
	}

}

/*
 * lookup a path record entry in the cache.
 * called with lookup lock acquired
 */
sdp_prcn_t *
sdp_prcache_lookup(sdp_ipx_addr_t *dst_addr)
{
	sdp_prcn_t *prcp;
	int len;

	/*
	 * if path record cache is disabled, return null
	 */
	if (!sdp_pr_cache)
		return (NULL);

	ASSERT(dst_addr->family == AF_INET || dst_addr->family == AF_INET6);

	len = (dst_addr->family == AF_INET) ? IP_ADDR_LEN :
	    sizeof (in6_addr_t);

	/*
	 * do a linear search of the cache list
	 */
	len = (dst_addr->family == AF_INET) ? IP_ADDR_LEN :
	    sizeof (in6_addr_t);
	for (prcp = sdp_global_state->prc_head; prcp != NULL;
	    prcp = prcp->next) {
		if (dst_addr->family == prcp->dst_addr.family) {
			if (bcmp(&dst_addr->un, &prcp->dst_addr.un, len) == 0)
				break;
		}
	}
	return (prcp);
}

/*
 * insert a wait queue node in the list.
 * assumes mutex is acquired
 */
void
sdp_prwqn_insert(sdp_prwqn_t *wqnp)
{
	ASSERT(MUTEX_HELD(&sdp_global_state->lookup_lock));
	wqnp->next = sdp_global_state->wq_head;
	sdp_global_state->wq_head = wqnp;
	wqnp->p_next = &sdp_global_state->wq_head;
	if (wqnp->next != NULL) {
		wqnp->next->p_next = &wqnp->next;
	}
}

/*
 * delete a wait queue node from the list.
 * assumes mutex is acquired
 */
static void
sdp_prwqn_delete(sdp_prwqn_t *wqnp)
{
	ASSERT(MUTEX_HELD(&sdp_global_state->lookup_lock));
	if (wqnp->p_next != NULL) {
		if (wqnp->next != NULL) {
			wqnp->next->p_next = wqnp->p_next;
		}
		*(wqnp->p_next) = wqnp->next;
		wqnp->p_next = NULL;
		wqnp->next = NULL;
	}
	kmem_cache_free(sdp_global_state->wq_kmcache, wqnp);
}

/*
 * allocate a wait queue node, and insert it in the list
 */
sdp_prwqn_t *
sdp_create_prwqn(sdp_lookup_id_t lid, sdp_ipx_addr_t *dst_addr,
    sdp_ipx_addr_t *src_addr, uint32_t localroute, uint32_t bound_dev_if,
	sdp_pr_comp_func_t func, void *user_arg)
{
	sdp_prwqn_t *wqnp;

	if (dst_addr == NULL) {
		return (NULL);
	}
	if ((wqnp = kmem_cache_alloc(sdp_global_state->wq_kmcache,
	    KM_NOSLEEP)) == NULL) {
		return (NULL);
	}
	bzero(wqnp, sizeof (sdp_prwqn_t));

	ASSERT(dst_addr->family == AF_INET || dst_addr->family == AF_INET6);

	if (dst_addr->family == AF_INET) {
		wqnp->dst_addr.ip4addr = dst_addr->ip4addr; /* Net */
		wqnp->usrc_addr.ip4addr = src_addr->ip4addr;

	} else {
		wqnp->dst_addr.ip6addr = dst_addr->ip6addr;
	}
	wqnp->dst_addr.family = dst_addr->family;
	wqnp->func = func;
	wqnp->arg = user_arg;
	wqnp->id = lid;
	wqnp->localroute = localroute;
	wqnp->bound_dev_if = bound_dev_if;
	wqnp->p_next = NULL;
	wqnp->next = NULL;
	wqnp->wqn_zoneid = -1;
	cv_init(&wqnp->ip2mac_cv, NULL, CV_DRIVER, NULL);

	wqnp->ifproto = (dst_addr->family == AF_INET) ?
	    ETHERTYPE_IP : ETHERTYPE_IPV6;
	mutex_enter(&sdp_global_state->lookup_lock);
	sdp_prwqn_insert(wqnp);
	mutex_exit(&sdp_global_state->lookup_lock);

	return (wqnp);
}

/*
 * call the user function
 * called with lock held
 */
static void
sdp_pr_callback(sdp_prwqn_t *wqnp, int status)
{
	int32_t result = 0;

	ASSERT(MUTEX_HELD(&sdp_global_state->lookup_lock));

	if (status == 0) {
		/*
		 * post_path_complete routine if on failure at ibt_open_rc_chan
		 * cleans up the wqnp in sdp_cm_idle. For this we need to
		 * release the lock here and grab it again on return. Besides
		 * we need not clean the wqnp if post path returns an error.
		 */
		mutex_exit(&sdp_global_state->lookup_lock);
		result = wqnp->func(wqnp->id, 0, &wqnp->src_addr,
		    &wqnp->dst_addr, &wqnp->path_info, &wqnp->path_attr,
		    wqnp->hw_port, wqnp->pkey, &wqnp->hca_hdl, wqnp->arg);
		mutex_enter(&sdp_global_state->lookup_lock);
	} else {
		wqnp->func(wqnp->id, status, NULL,
		    NULL, NULL, NULL, 0, 0, NULL, wqnp->arg);
	}
	if (result == 0)
		sdp_prwqn_delete(wqnp);
}

/*
 * Check if the interface is loopback or IB.
 */
static int
sdp_check_interface(ill_t *ill)
{
	if (IS_LOOPBACK(ill) || ill->ill_mactype == DL_IB)
		return (0);

	dprint(SDP_WARN, ("Error: IP interface %s is not IB or loopback",
	    ill->ill_name));
	return (ENETUNREACH);
}

static int
sdp_pr_resolver_query(sdp_prwqn_t *wqnp, ill_t *ill, zoneid_t zoneid)
{
	int rc = 0;
	int len;

	mutex_enter(&sdp_global_state->lookup_lock);

	wqnp->flags &= ~SDP_PR_RT_PENDING;

	sdp_print_dbg(NULL, "%s: outgoing if:%s", __func__, wqnp->ifname);
	/*
	 * if localroute is set, then make sure the rts
	 * returned gateway address is the same as the
	 * supplied source address
	 */
	if (wqnp->localroute) {
		len = (wqnp->dst_addr.family == AF_INET) ?
		    IP_ADDR_LEN : sizeof (in6_addr_t);
		if (bcmp(&wqnp->gateway.un, &wqnp->src_addr.un, len)) {
			rc = ENETUNREACH;
			sdp_print_warn(NULL, "%s: local route error:%d\n",
			    __func__, rc);
			goto error_path;
		}
	}

	/*
	 * if the user supplied a address, then verify rts returned
	 * the same address
	 */
	if (wqnp->usrc_addr.family) {
		len = (wqnp->usrc_addr.family == AF_INET) ?
		    IP_ADDR_LEN : sizeof (in6_addr_t);
		if (bcmp(&wqnp->usrc_addr.un, &wqnp->src_addr.un, len)) {
			rc = ENETUNREACH;
			sdp_print_warn(NULL, "%s: src addr mismatch:%d\n",
			    __func__, rc);
			goto error_path;
		}
	}

	/*
	 * at this stage, we have the source address and the IB
	 * interface, now get the destination mac address from
	 * arp or ipv6 drivers
	 */
	if (wqnp->dst_addr.family == AF_INET) {
		if ((rc = sdp_arp_lookup(wqnp, ill, zoneid)) != 0) {
			sdp_print_warn(NULL, "%s arp_req  error:%d\n",
			    __func__, rc);
			goto error_path;
		}
	} else {
		if ((rc = sdp_ndp_lookup(wqnp, ill, zoneid)) != 0) {
			sdp_print_warn(NULL, "%s ip6_query error:%d\n",
			    __func__, rc);
			goto error_path;
		}
	}
	mutex_exit(&sdp_global_state->lookup_lock);
	return (rc);

error_path:
	/*
	 * indicate to user
	 */
	mutex_exit(&sdp_global_state->lookup_lock);
	return (rc);
}

/*ARGSUSED*/
int
sdp_pr_lookup(sdp_conn_t *conn, uint8_t localroute, uint32_t bound_dev_if,
    sdp_pr_comp_func_t func, void *user_arg, sdp_lookup_id_t *lid)
{
	int rc = 0;
	sdp_prcn_t *prcp;
	sdp_prwqn_t *wqnp = NULL;
	sdp_ipx_addr_t dst_addr;
	sdp_ipx_addr_t src_addr;
	ire_t *ire = NULL;
	ill_t *ill = NULL;
	ill_t *hwaddr_ill = NULL;
	netstack_t *stack;
	ip_stack_t *ipst;
	zoneid_t ip_zoneid;

	if (!lid)
		return (EINVAL);

	ASSERT(conn->sdp_ipversion == IPV4_VERSION ||
	    conn->sdp_ipversion == IPV6_VERSION);

	switch (conn->sdp_ipversion) {
	case IPV4_VERSION:
		/*
		 * Dst addr needs to be in netwok order for arp resolution
		 */
		dst_addr.family = src_addr.family = AF_INET;
		dst_addr.ip4addr = conn->conn_rem;
		src_addr.ip4addr = conn->conn_src;
		sdp_print_dbg(conn, "sdp_pr_lookup: post a IPv4 query:"
		    " dst:<%x> ", ntohl(dst_addr.ip4addr));
		break;

	case IPV6_VERSION:
		dst_addr.family = src_addr.family = AF_INET6;
		bcopy(&conn->conn_remv6, &dst_addr.un, sizeof (in6_addr_t));
		dprint(SDP_DBG, ("sdp_pr_lookup: post a IPv6 query:"
		    "dst:<%x:%x:%x:%x>", conn->conn_remv6.s6_addr32[0],
		    conn->conn_remv6.s6_addr32[1],
		    conn->conn_remv6.s6_addr32[2],
		    conn->conn_remv6.s6_addr32[3]));
		break;

	default:
		dprint(SDP_WARN, ("Wrong IP version %d", conn->sdp_ipversion));
		return (EAFNOSUPPORT);
	}

	mutex_enter(&sdp_global_state->lookup_lock);
	*lid = ++sdp_global_state->lookup_id;

	/*
	 * lookup cache
	 */
	if ((prcp = sdp_prcache_lookup(&dst_addr)) != NULL) {
		/*
		 * return error if func is NULL
		 */
		if (func) {
			func(*lid, 0, &dst_addr, &prcp->src_addr,
			    &prcp->path_info, &prcp->path_attr,
			    prcp->hw_port, prcp->pkey,
			    &prcp->hca_hdl, user_arg);
			rc = 0;
		} else {
			rc = EINVAL;
		}
		mutex_exit(&sdp_global_state->lookup_lock);
		return (rc);
	}
	mutex_exit(&sdp_global_state->lookup_lock);

	if ((wqnp = sdp_create_prwqn(*lid, &dst_addr, &src_addr, localroute,
	    bound_dev_if, func, user_arg)) == NULL) {
		return (ENOMEM);
	}

	stack = conn->sdp_stack;
	ASSERT(stack != NULL);
	netstack_hold(stack);
	ipst = stack->netstack_ip;
	ASSERT(ipst != NULL);
	if (stack->netstack_stackid != GLOBAL_NETSTACKID) {
		/*
		 * For exclusive stacks we set the zoneid to zero
		 */
		ip_zoneid = GLOBAL_ZONEID;
	} else {
		ip_zoneid = conn->sdp_zoneid;
	}

	ASSERT(conn->sdp_ipversion == IPV4_VERSION ||
	    conn->sdp_ipversion == IPV6_VERSION);

	if (conn->sdp_ipversion == IPV4_VERSION) {
		ipaddr_t	setsrc = INADDR_ANY;

		ire = ire_route_recursive_v4(dst_addr.ip4addr, 0, NULL,
		    ip_zoneid, NULL, MATCH_IRE_DSTONLY, B_TRUE, 0, ipst,
		    &setsrc, NULL, NULL);

		ASSERT(ire != NULL);

		if (ire->ire_flags & (RTF_REJECT|RTF_BLACKHOLE)) {
			rc = ENETUNREACH;
			goto out;
		}

		wqnp->gateway.ip4addr = ire->ire_gateway_addr;
		wqnp->netmask.ip4addr = ire->ire_mask;

		ill = ire_nexthop_ill(ire);
		if (ill == NULL) {
			rc = ENETUNREACH;
			goto out;
		}

		/* Pick a source address */
		if (ip_select_source_v4(ill, setsrc, dst_addr.ip4addr,
		    INADDR_ANY, ip_zoneid, ipst, &wqnp->src_addr.ip4addr,
		    NULL, NULL) != 0) {
			rc = EADDRNOTAVAIL;
			goto out;
		}
		wqnp->src_addr.family = wqnp->gateway.family =
		    wqnp->netmask.family = AF_INET;

	} else if (conn->sdp_ipversion == IPV6_VERSION) {
		in6_addr_t	setsrc = ipv6_all_zeros;

		ire = ire_route_recursive_v6(&dst_addr.ip6addr, 0, NULL,
		    ip_zoneid, NULL, MATCH_IRE_DSTONLY, B_TRUE, 0, ipst,
		    &setsrc, NULL, NULL);

		ASSERT(ire != NULL);

		if (ire->ire_flags & (RTF_REJECT|RTF_BLACKHOLE)) {
			rc = ENETUNREACH;
			goto out;
		}

		wqnp->gateway.ip6addr = ire->ire_gateway_addr_v6;
		wqnp->netmask.ip6addr = ire->ire_mask_v6;

		ill = ire_nexthop_ill(ire);
		if (ill == NULL) {
			rc = ENETUNREACH;
			goto out;
		}

		/* Pick a source address */
		if (ip_select_source_v6(ill, &setsrc, &dst_addr.ip6addr,
		    ip_zoneid, ipst, B_FALSE, IPV6_PREFER_SRC_DEFAULT,
		    &wqnp->src_addr.ip6addr, NULL, NULL) != 0) {
			rc = EADDRNOTAVAIL;
			goto out;
		}
		wqnp->src_addr.family = wqnp->gateway.family =
		    wqnp->netmask.family = AF_INET6;
	}

	(void) strlcpy(wqnp->ifname, ill->ill_name, sizeof (wqnp->ifname));

	/*
	 * For IPMP data addresses, we need to use the hardware address of the
	 * interface bound to the given address.
	 */
	if (IS_IPMP(ill)) {
		ipif_t *ipif;

		if (conn->sdp_ipversion == IPV4_VERSION) {
			ipif = ipif_lookup_up_addr(
			    wqnp->src_addr.ip4addr, ill,
			    ip_zoneid, ipst);
		} else {
			ipif = ipif_lookup_up_addr_v6(
			    &wqnp->src_addr.ip6addr, ill,
			    ip_zoneid, ipst);
		}
		if (ipif == NULL) {
			rc = ENETUNREACH;
			goto out;
		}

		if ((hwaddr_ill = ipmp_ipif_hold_bound_ill(ipif)) == NULL) {
			ipif_refrele(ipif);
			rc = EFAULT;
			goto out;
		}
		ipif_refrele(ipif);
	} else {
		hwaddr_ill = ill;
		ill_refhold(hwaddr_ill);	/* for symmetry */
	}

	if ((rc = sdp_check_interface(hwaddr_ill)) != 0)
		goto out;
	/*
	 * For ipmp name resolution use the name of the hwaddr_ill
	 * used GLOBAL zoneid for the shared zone.
	 */
	if (IS_LOOPBACK(hwaddr_ill)) {
		sdp_pr_loopback(wqnp, conn);
	} else {
		wqnp->pkey = sdp_get_ibd_pkey(hwaddr_ill->ill_name,
		    stack->netstack_stackid == GLOBAL_NETSTACKID ?
		    GLOBAL_ZONEID : conn->sdp_zoneid);
		if (wqnp->pkey == 0) {
			rc = ENOENT;
			goto out;
		}
		bcopy(hwaddr_ill->ill_phys_addr, &wqnp->src_mac,
		    hwaddr_ill->ill_phys_addr_length);

		rc = sdp_pr_resolver_query(wqnp, ill, conn->sdp_zoneid);
	}

	if (rc != 0)
		goto out;

	/* Keep wqnp alive on sdp_global_state->wq_head */
	wqnp = NULL;
out:
	if (hwaddr_ill != NULL)
		ill_refrele(hwaddr_ill);
	if (wqnp != NULL) {
		mutex_enter(&sdp_global_state->lookup_lock);
		sdp_prwqn_delete(wqnp);
		mutex_exit(&sdp_global_state->lookup_lock);
	}
	if (ire != NULL)
		ire_refrele(ire);
	if (ill != NULL)
		ill_refrele(ill);
	netstack_rele(stack);

	return (rc);
}

/*
 * cancel a path record lookup in progress: deletes the data that
 * was passed in .
 */
int
sdp_pr_lookup_cancel(sdp_lookup_id_t id)
{
	sdp_prwqn_t *wqnp;

	mutex_enter(&sdp_global_state->lookup_lock);
	for (wqnp = sdp_global_state->wq_head; wqnp != NULL;
	    wqnp = wqnp->next) {
		if (wqnp->id == id) {
			sdp_prwqn_delete(wqnp);
			mutex_exit(&sdp_global_state->lookup_lock);
			return (0);
		}
	}
	mutex_exit(&sdp_global_state->lookup_lock);

	return (EINVAL);
}

/*
 * complete the path record lookup process. at this stage
 * we have source/destination gid's. get the hca handle,pkey,port and
 * retrieve the path record.
 * called with lock held
 */
void
sdp_pr_link_complete(sdp_prwqn_t *wqnp)
{
	int rc;

	ASSERT(!(curthread->t_flag & T_INTR_THREAD));
	ASSERT(MUTEX_HELD(&sdp_global_state->lookup_lock));
	/*
	 * get the hca handle, pkey and port from ibd
	 */
	dprint(SDP_DBG, ("sdp_pr_link_complete: Recv'd GID"));

	if ((rc = sdp_get_hca_info(wqnp)) != 0) {
		goto user_callback;
	}

	wqnp->ip2mac_id = 0;
	wqnp->wqn_zoneid = -1;
	wqnp->flags &= ~SDP_PR_RESOLVE_PENDING;
	cv_broadcast(&wqnp->ip2mac_cv);
	/*
	 * now get the path info
	 */
	wqnp->path_attr.pa_sgid = wqnp->sgid;
	wqnp->path_attr.pa_num_dgids = 1;
	wqnp->path_attr.pa_dgids = &wqnp->dgid;
	wqnp->path_attr.pa_sl = 0;
	if ((rc = ibt_get_paths(sdp_global_state->sdp_ibt_hdl,
	    sdp_global_state->sdp_apm_enabled ?
	    (IBT_PATH_APM | IBT_PATH_AVAIL) : IBT_PATH_NO_FLAGS,
	    &wqnp->path_attr, 1, &wqnp->path_info, NULL)) != IBT_SUCCESS) {
		dprint(SDP_WARN, (
		    "link_complete : ibt_get_paths failed, rc=%d", rc));
		rc = ENOLINK;
		goto user_callback;
	}
	/*
	 * create a cache entry
	 */
	sdp_prcn_insert(wqnp);
	rc = 0;
user_callback:
	/*
	 * complete the request
	 */
	sdp_pr_callback(wqnp, rc);
}

#define	H2N_GID(gid) \
{ \
	uint32_t	*ptr; \
	ptr = (uint32_t *)&gid.gid_prefix; \
	gid.gid_prefix = (uint64_t)(((uint64_t)ntohl(ptr[0]) << 32) | \
			(ntohl(ptr[1]))); \
	ptr = (uint32_t *)&gid.gid_guid; \
	gid.gid_guid = (uint64_t)(((uint64_t)ntohl(ptr[0]) << 32) | \
			(ntohl(ptr[1]))); \
}

/*
 * Address resolution was succesful: update the Path Record.
 */
static void
sdp_pr_resolver_ack(ip2mac_t *ip2macp, sdp_prwqn_t *wqnp,
    boolean_t lock_held)
{
	uchar_t *cp;
	int err;

	/*
	 * check wqnp is still valid. A cancel request could have been issued
	 */
	if (!lock_held)
		mutex_enter(&sdp_global_state->lookup_lock);
	if (sdp_check_prwqn(wqnp)) {
		if (!lock_held)
			mutex_exit(&sdp_global_state->lookup_lock);
		return;
	}
	if (!sdp_check_sockdl(&ip2macp->ip2mac_ha)) {
		dprint(3, ("Error: interface %s is not IB\n", wqnp->ifname));
		err = EHOSTUNREACH;
		goto user_callback;
	}
	cp = (uchar_t *)LLADDR(&ip2macp->ip2mac_ha);
	bcopy(cp, &wqnp->dst_mac, IPOIB_ADDRL);

	/*
	 * at this point we have src/dst gid's derived from the mac addresses
	 * now get the hca, port
	 */
	bcopy(&wqnp->src_mac.ipoib_gidpref, &wqnp->sgid, sizeof (ib_gid_t));
	bcopy(&wqnp->dst_mac.ipoib_gidpref, &wqnp->dgid, sizeof (ib_gid_t));

	H2N_GID(wqnp->sgid);
	H2N_GID(wqnp->dgid);

	sdp_pr_link_complete(wqnp);
	if (!lock_held)
		mutex_exit(&sdp_global_state->lookup_lock);
	return;
user_callback:
	/*
	 * indicate to user
	 */
	sdp_pr_callback(wqnp, err);
	if (!lock_held)
		mutex_exit(&sdp_global_state->lookup_lock);
}

/*
 * find the first available hca with a port up in the particular zone
 */
static void
sdp_pr_loopback(sdp_prwqn_t *wqnp, sdp_conn_t *conn)
{
	ibt_part_attr_t *attr_list, *attr;
	char ifname[MAXLINKNAMELEN];
	zoneid_t ip_zoneid;
	ill_t *ill;
	ip_stack_t *ipst;
	netstack_t *stack;
	int i, entries;
	in6_addr_t setsrc;

	stack = conn->sdp_stack;
	ipst = stack->netstack_ip;

	mutex_enter(&sdp_global_state->lookup_lock);

	/*
	 * Find all the matching ibp instances
	 */
	if ((ibt_get_all_part_attr(&attr_list, &entries) !=
	    IBT_SUCCESS) || (entries == 0)) {
		dprint(SDP_DBG, ("sdp_pr_loopback: failed to get IB "
		    "part list - %d", entries));
		sdp_pr_callback(wqnp, ENOLINK);
		mutex_exit(&sdp_global_state->lookup_lock);
		return;
	}

	if (stack->netstack_stackid != GLOBAL_NETSTACKID) {
		/*
		 * For exclusive stacks we set the zoneid to zero
		 */
		ip_zoneid = GLOBAL_ZONEID;
	} else {
		ip_zoneid = conn->sdp_zoneid;
	}

	for (attr = attr_list, i = 0; i < entries; i++, attr++) {
		zoneid_t	zid;

		if (ibt_get_port_state_byguid(attr->pa_hca_guid,
		    attr->pa_port, &wqnp->sgid, NULL) != IBT_SUCCESS) {
			continue;
		}

		if (dls_devnet_get_active_linkname(attr->pa_plinkid, &zid,
		    ifname, MAXLINKNAMELEN) != 0) {
			dprint(SDP_DBG, ("sdp_pr_loopback: "
			    "dls_devnet_get_active_linkname %d failed",
			    attr->pa_plinkid));
			continue;
		}

		/*
		 * For an share-zone, the link owns by the global zone.
		 */
		if (zid != (stack->netstack_stackid == GLOBAL_NETSTACKID ?
		    GLOBAL_ZONEID : conn->sdp_zoneid)) {
			continue;
		}

		if ((ill = ill_lookup_on_name(ifname, B_FALSE,
		    wqnp->src_addr.family != AF_INET, NULL, ipst)) == NULL) {
			dprint(SDP_DBG, ("sdp_pr_loopback: failed to find ill "
			    "%s", ifname));
			continue;
		}

		/*
		 * Pick a source address so that the receiver can determine
		 * which zone this connection comes from
		 */
		setsrc = ipv6_all_zeros;
		if ((wqnp->src_addr.family == AF_INET &&
		    ip_select_source_v4(ill, INADDR_ANY, conn->conn_rem,
		    INADDR_ANY, ip_zoneid, ipst, &wqnp->src_addr.ip4addr,
		    NULL, NULL) != 0) || (wqnp->src_addr.family == AF_INET6 &&
		    ip_select_source_v6(ill, &setsrc, &conn->conn_remv6,
		    ip_zoneid, ipst, B_TRUE, IPV6_PREFER_SRC_DEFAULT,
		    &wqnp->src_addr.ip6addr, NULL, NULL) != 0)) {
			dprint(SDP_DBG, ("sdp_pr_loopback: failed to find a "
			    "valid IP address on %s", ifname));
			ill_refrele(ill);
			continue;
		}
		ill_refrele(ill);

		wqnp->pkey = attr->pa_pkey;

		/* on loopback sgid = dgid */
		bcopy(&wqnp->sgid, &wqnp->dgid, sizeof (ib_gid_t));
		sdp_pr_link_complete(wqnp);
		mutex_exit(&sdp_global_state->lookup_lock);
		(void) ibt_free_part_attr(attr_list, entries);
		return;
	}
	(void) ibt_free_part_attr(attr_list, entries);

	/*
	 * no match found, indicate to user
	 */
	sdp_pr_callback(wqnp, ENOLINK);
	mutex_exit(&sdp_global_state->lookup_lock);
}

/*
 * check if the given wait queue node is in the queue
 * called with lookup lock acquired
 */
static int
sdp_check_prwqn(sdp_prwqn_t *wqnp)
{
	sdp_prwqn_t *twqnp;

	for (twqnp = sdp_global_state->wq_head;
	    twqnp != NULL; twqnp = twqnp->next) {
		if (twqnp == wqnp) {
			return (0);
		}
	}
	return (-1);
}

/*
 * create kmem cache for wait queue, pr cache nodes
 */
int
sdp_pr_lookup_init(void)
{
	mutex_init(&sdp_global_state->lookup_lock, NULL, MUTEX_DRIVER, NULL);

	if ((sdp_global_state->wq_kmcache = kmem_cache_create("sdp_wait_list",
	    sizeof (sdp_prwqn_t), 0, NULL, NULL, NULL, NULL, NULL,
	    0)) == NULL) {
		return (ENOMEM);
	}

	if (sdp_pr_cache) {
		if ((sdp_global_state->pr_kmcache =
		    kmem_cache_create("sdp pr cache",
		    sizeof (sdp_prwqn_t), 0,
		    NULL, NULL, NULL, NULL, NULL,
		    0)) == NULL) {
			kmem_cache_destroy(sdp_global_state->wq_kmcache);
			return (ENOMEM);
		}
		sdp_global_state->prc_timeout_id =
		    timeout(sdp_prcache_timer, NULL,
		    drv_usectohz(SDP_PR_CACHE_REAPING_AGE_USECS));

	}

	return (0);
}

/*
 * destroy the cache
 */
void
sdp_prcache_cleanup(void)
{
	sdp_prcn_t *prcp;
	sdp_prcn_t *n_prcp;

	if (!sdp_pr_cache)
		return;

	mutex_enter(&sdp_global_state->lookup_lock);
	prcp = sdp_global_state->prc_head;
	while (prcp != NULL) {
		n_prcp = prcp->next;
		sdp_prcn_delete(prcp);
		prcp = n_prcp;
	}
	mutex_exit(&sdp_global_state->lookup_lock);
}

int
sdp_pr_lookup_cleanup(void)
{
	sdp_prwqn_t *wqnp;

	/*
	 * Cancel any outstanding resolver requests
	 */
	mutex_enter(&sdp_global_state->lookup_lock);
again:
	for (wqnp = sdp_global_state->wq_head; wqnp != NULL;
	    wqnp = wqnp->next) {
		if (wqnp->ip2mac_id == 0)
			continue;
		ASSERT(wqnp->wqn_zoneid != -1);
		(void) ip2mac_cancel(wqnp->ip2mac_id, wqnp->wqn_zoneid);
		if (wqnp->flags & SDP_PR_RESOLVE_PENDING) {
			cv_wait(&wqnp->ip2mac_cv,
			    &sdp_global_state->lookup_lock);
			/*
			 * wqnp could have been delted, so start
			 * from the head of the list
			 */
			goto again;
		}
		sdp_prwqn_delete(wqnp);
	}
	mutex_exit(&sdp_global_state->lookup_lock);
	kmem_cache_destroy(sdp_global_state->wq_kmcache);
	if (sdp_pr_cache) {
		mutex_enter(&sdp_global_state->lookup_lock);
		(void) untimeout(sdp_global_state->prc_timeout_id);
		mutex_exit(&sdp_global_state->lookup_lock);
		sdp_prcache_cleanup();
		kmem_cache_destroy(sdp_global_state->pr_kmcache);
	}
	mutex_destroy(&sdp_global_state->lookup_lock);

	return (0);
}

/*
 * get the root device info pointer
 * should have device tree lock around this function
 */
dev_info_t *
sdp_get_root_dip(void)
{
	dev_info_t *dip, *cdip = NULL;

	dip = sdp_global_state->dip;
	while ((dip = ddi_get_parent(dip)) != NULL) {
		cdip = dip;
	}
	return (cdip);
}

/*
 * get ibd partition key
 * ifname passed in might be different than the name stored in wqnp->ifname
 * because for ipmp we need to use the name of the interface hosting the active
 * address
 */
static uint16_t
sdp_get_ibd_pkey(const char *ifname, zoneid_t zoneid)
{
	datalink_id_t	linkid;
	ibt_part_attr_t	part_attr;

	if (dls_devnet_get_active_linkid(ifname, zoneid, &linkid) != 0) {
		sdp_print_err(NULL, "name to linkid conversion failed "
		    "for %s/%d\n", ifname, zoneid);
		return (0);
	}

	part_attr.pa_pkey = 0;
	if (ibt_get_part_attr(linkid, &part_attr) != IBT_SUCCESS) {
		sdp_print_err(NULL, "ibd partition key retrieval failed "
		    "for %s\n", ifname);
	}

	return (part_attr.pa_pkey);
}

/*
 * given a ipo_ib interface and source gid, return a hca handle, port and pkey
 */
static int
sdp_get_hca_info(sdp_prwqn_t *wqnp)
{
	ib_guid_t p_guid;
	int j;
	ibt_hca_portinfo_t *port_infop;
	uint_t psize, port_infosz;
	ibt_status_t ibt_status;
	ib_gid_t tgid;
	sdp_dev_hca_t *hcap;

	/*
	 * walk thru' the list of HCA's and identify the
	 * hca whose port guid matches that of the
	 * given guid
	 */
	p_guid = wqnp->sgid.gid_guid;
	mutex_enter(&sdp_global_state->hcas_mutex);
	hcap = sdp_global_state->hca_list;
	while (hcap != NULL) {
		if (hcap->sdp_hca_offlined) {
			hcap = hcap->next;
			continue;
		}

		/*
		 * walk thru' the port's of this hca
		 */
		for (j = 1; j <= hcap->hca_nports; j++) {
			ibt_status = ibt_query_hca_ports(hcap->hca_hdl,
			    j, &port_infop, &psize,
			    &port_infosz);
			if ((ibt_status != IBT_SUCCESS) || (psize != 1)) {
				mutex_exit(&sdp_global_state->hcas_mutex);
				dprint(SDP_WARN, (
				    "get_hca_info : failed in "
				    "ibt_query_port()\n"));
				return (EINVAL);
			}

			tgid = *port_infop->p_sgid_tbl;
			if (p_guid == tgid.gid_guid) {
				/*
				 * got the correct one
				 */
				wqnp->hw_port = (uint8_t)j;
				ibt_free_portinfo(port_infop, port_infosz);
				wqnp->hca_hdl = hcap->hca_hdl;
				mutex_exit(&sdp_global_state->hcas_mutex);
				if (wqnp->pkey != 0)
					return (0);
				else
					return (ENODEV);
			}
			ibt_free_portinfo(port_infop, port_infosz);
		}
		hcap = hcap->next;
	}
	mutex_exit(&sdp_global_state->hcas_mutex);
	return (ENODEV);
}

/*
 * do sanity checks on the link-level sockaddr
 */
boolean_t
sdp_check_sockdl(struct sockaddr_dl *sdl)
{

	if (sdl->sdl_type != IFT_IB || sdl->sdl_alen != IPOIB_ADDRL)
		return (B_FALSE);

	return (B_TRUE);
}
