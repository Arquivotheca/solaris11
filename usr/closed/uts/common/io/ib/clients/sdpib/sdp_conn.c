/*
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * LEGAL NOTICE
 *
 * This file contains source code that implements the Sockets Direct
 * Protocol (SDP) as defined by the InfiniBand Architecture Specification,
 * Volume 1, Annex A4, Version 1.1.  Due to restrictions in the SDP license,
 * source code contained in this file may not be distributed outside of
 * Sun Microsystems without further legal review to ensure compliance with
 * the license terms.
 *
 * Sun employees and contactors are cautioned not to extract source code
 * from this file and use it for other purposes.  The SDP implementation
 * code in this and other files must be kept separate from all other source
 * code.
 *
 * As required by the license, the following notice is added to the source
 * code:
 *
 * This source code may incorporate intellectual property owned by
 * Microsoft Corporation.  Our provision of this source code does not
 * include any licenses or any other rights to you under any Microsoft
 * intellectual property.  If you would like a license from Microsoft
 * (e.g., to rebrand, redistribute), you need to contact Microsoft
 * directly.
 */

#include <sys/dls.h>
#include <sys/ib/clients/sdpib/sdp_main.h>
#include <sys/ib/clients/sdpib/sdp_trace_util.h>
#include <inet/tunables.h>
#include <inet/sdp_itf.h>

/*
 * sdp_conn.c
 *
 * Routines that allocate, modify, and destroy individual
 * SDP connections, the SDP connection table, and various
 * global lists which are members of the single-instance SDP dev root
 * struct.
 */

static sdp_dev_root_t _dev_root_s;
uint_t sdp_inline_bytes_max = 0;

static void sdp_conn_table_remove(sdp_conn_t *conn);

/* BEGIN CSTYLED */
sdpparam_t      sdp_param_arr[] = {
 { SDP_XMIT_LOWATER, (1<<30),   SDP_XMIT_HIWATER, "sdp_xmit_hiwat"},
 { SDP_RECV_LOWATER, (1<<30),   SDP_RECV_HIWATER, "sdp_recv_hiwat"},
 { 0,           128000,         1,              "sdp_sth_rcv_lowat" }
};

/* END CSTYLED */

/*
 * Put a connection into a listener conn's accept Q.
 */
int32_t
sdp_inet_accept_queue_put(sdp_conn_t *listen_conn, sdp_conn_t *accept_conn)
{
	sdp_conn_t *next_conn;

	SDP_CHECK_NULL(listen_conn, -EINVAL);
	SDP_CHECK_NULL(accept_conn, -EINVAL);

	if (listen_conn->parent != NULL ||
	    accept_conn->parent != NULL ||
	    listen_conn->accept_next == NULL ||
	    listen_conn->accept_prev == NULL) {
		sdp_print_note(listen_conn, "sdp_inet_accept_queue_put: "
		    "Inserting eager:%p fault", (void *)accept_conn);
		return (EFAULT);
	}
	next_conn = listen_conn->accept_next;

	accept_conn->accept_next = listen_conn->accept_next;
	listen_conn->accept_next = accept_conn;
	accept_conn->accept_prev = listen_conn;
	next_conn->accept_prev = accept_conn;

	accept_conn->parent = listen_conn;
	listen_conn->backlog_cnt++;

	SDP_CONN_SOCK_HOLD(accept_conn); /* INET reference */

	return (0);
}   /* sdp_inet_accept_queue_put */

/* ========================================================================= */

/*
 * Retrieve a connection from a listener conn's accept Queue.
 */
sdp_conn_t *
sdp_inet_accept_queue_get(sdp_conn_t *listen_conn)
{
	sdp_conn_t *prev_conn;
	sdp_conn_t *accept_conn;

	SDP_CHECK_NULL(listen_conn, NULL);

	ASSERT(MUTEX_HELD(&listen_conn->conn_lock));

	if (listen_conn->parent != NULL ||
	    listen_conn->accept_next == NULL ||
	    listen_conn->accept_prev == NULL ||
	    listen_conn == listen_conn->accept_next ||
	    listen_conn == listen_conn->accept_prev) {
		sdp_print_note(listen_conn, "sdp_inet_accept_queue_get:"
		    " retrieve listener:%p fault", (void *)listen_conn);
		return (NULL);
	}
	accept_conn = listen_conn->accept_prev;
	SDP_CONN_LOCK(accept_conn);
	prev_conn = accept_conn->accept_prev;

	listen_conn->accept_prev = accept_conn->accept_prev;

	if (prev_conn != listen_conn)
		SDP_CONN_LOCK(prev_conn);
	prev_conn->accept_next = listen_conn;
	if (prev_conn != listen_conn)
		SDP_CONN_UNLOCK(prev_conn);

	accept_conn->accept_next = NULL;
	accept_conn->accept_prev = NULL;
	accept_conn->parent = NULL;

	listen_conn->backlog_cnt--;

	return (accept_conn);
}   /* sdp_inet_accept_queue_get */

/* ========================================================================= */

/*
 * Remove a connection from a listener conn's accept Queue.
 */
int32_t
sdp_inet_accept_queue_remove(sdp_conn_t *eager_conn, boolean_t return_locked)
{
	sdp_conn_t *next_conn;
	sdp_conn_t *prev_conn;
	sdp_conn_t *parent_conn;

	if ((eager_conn == NULL) ||
	    (eager_conn->parent == NULL) ||
	    (eager_conn->accept_next == NULL) ||
	    (eager_conn->accept_prev == NULL)) {
		sdp_print_note(eager_conn, "sdp_inet_accept_queue_remove: "
		    "eager_conn:%p fault", (void *)eager_conn);
		return (1);
	}

	/*
	 * The correct locking order is listen --> eager
	 * put a ref cnt on the listener, drop eager lock,
	 * acquire listener lock and than eager lock
	 */
	parent_conn = eager_conn->parent;
	SDP_CONN_HOLD(parent_conn);
	SDP_CONN_UNLOCK(eager_conn);
	SDP_CONN_LOCK(parent_conn);
	SDP_CONN_LOCK(eager_conn);

	if (parent_conn->state == SDP_CONN_ST_CLOSED) {
		/*
		 * listener is closing, it will issue an abort
		 * so just return
		 */
		SDP_CONN_UNLOCK(parent_conn);
		SDP_CONN_PUT(parent_conn);
		return (1);
	}

	next_conn = eager_conn->accept_next;
	prev_conn = eager_conn->accept_prev;

	prev_conn->accept_next = eager_conn->accept_next;
	next_conn->accept_prev = eager_conn->accept_prev;

	eager_conn->parent->backlog_cnt--;

	if (!return_locked) {
		SDP_CONN_UNLOCK(eager_conn->parent);
		SDP_CONN_PUT(eager_conn->parent);
	}

	eager_conn->accept_next = NULL;
	eager_conn->accept_prev = NULL;
	eager_conn->parent = NULL;
	SDP_CONN_SOCK_PUT(eager_conn);

	return (0);
}   /* sdp_inet_accept_queue_remove */

/* ========================================================================= */

/*
 * sdp_inet_listen_start -- start listening for new connections on a socket
 */
int32_t
sdp_inet_listen_start(sdp_conn_t *conn)
{
	SDP_CHECK_NULL(conn, EINVAL);
	if (conn->state != SDP_CONN_ST_CLOSED) {
		sdp_print_warn(conn, "sdp_inet_listen_start: invalid listen "
		    "state <%04x> invalid", conn->state);
		return (EBADFD);
	}	/* if */

	conn->state = SDP_CONN_ST_LISTEN;
	conn->accept_next = conn;
	conn->accept_prev = conn;
	/*
	 * table lock
	 */
	mutex_enter(&conn->sdp_sdps->sdps_listen_lock);
	conn->lstn_next = conn->sdp_sdps->sdps_listen_list;
	conn->sdp_sdps->sdps_listen_list = conn;
	conn->lstn_p_next =
	    &conn->sdp_sdps->sdps_listen_list;

	if (conn->lstn_next != NULL)
		conn->lstn_next->lstn_p_next = &conn->lstn_next;

	SDP_CONN_SOCK_HOLD(conn);
	mutex_exit(&conn->sdp_sdps->sdps_listen_lock);

	return (0);
}   /* sdp_inet_listen_start */

/* ========================================================================= */

/*
 * sdp_inet_listen_stop -- stop listening for new connections on a socket
 */
int32_t
sdp_inet_listen_stop(sdp_conn_t *listen_conn)
{
	sdp_conn_t *accept_conn;

	SDP_CHECK_NULL(listen_conn, EINVAL);

	if (listen_conn->state != SDP_CONN_ST_LISTEN) {
		ASSERT(0);
		return (EBADFD);
	}	/* if */
	listen_conn->state = SDP_CONN_ST_CLOSED;

	/*
	 * table lock
	 */
	mutex_enter(&listen_conn->sdp_sdps->sdps_listen_lock);

	/*
	 * remove from listening list.
	 */
	if (listen_conn->lstn_next != NULL) {
		listen_conn->lstn_next->lstn_p_next = listen_conn->lstn_p_next;
	}
	*(listen_conn->lstn_p_next) = listen_conn->lstn_next;

	listen_conn->lstn_p_next = NULL;
	listen_conn->lstn_next = NULL;

	mutex_exit(&listen_conn->sdp_sdps->sdps_listen_lock);

	/*
	 * reject and delete all pending connections
	 */
	while ((accept_conn = sdp_inet_accept_queue_get(listen_conn))
	    != NULL) {
		ASSERT(MUTEX_HELD(&accept_conn->conn_lock));
		sdp_print_note(accept_conn, "sdp_inet_listen_stop: Aborting "
		    "pending conn, state:<%04x>", accept_conn->state);
		sdp_conn_abort(accept_conn);
		SDP_CONN_UNLOCK(accept_conn);
		/* INET reference (accept_queue_put). */
		SDP_CONN_SOCK_PUT(accept_conn);
	}

	listen_conn->accept_next = NULL;
	listen_conn->accept_prev = NULL;
	SDP_CONN_SOCK_PUT(listen_conn);

	return (0);
}   /* sdp_inet_listen_stop */

/* ========================================================================= */

/*
 * sdp_inet_listen_lookup -- lookup a connection in the listen list
 */
sdp_conn_t *
sdp_inet_listen_lookup(sdp_conn_t *eager_conn, ibt_cm_event_t *cm_event)
{
	sdp_conn_t *conn2, *match_conn = NULL;
	uint16_t port = eager_conn->src_port;
	ibt_part_attr_t	*attr_list, *attr;
	boolean_t isv4 = (eager_conn->sdp_ipversion == IPV4_VERSION);
	int i, nparts;

	dprint(SDP_DBG, ("sdp_inet_listen_lookup: family:%d addr:%x",
	    eager_conn->sdp_family, eager_conn->conn_src));

	/*
	 * Find all the matching ibp instances
	 */
	if ((ibt_get_all_part_attr(&attr_list, &nparts) != IBT_SUCCESS) ||
	    (nparts == 0)) {
		dprint(SDP_DBG, ("sdp_inet_listen_lookup: family:%d addr:%x "
		    "failed to get IB part list - %d", eager_conn->sdp_family,
		    eager_conn->conn_src, nparts));
		return (NULL);
	}

	for (attr = attr_list, i = 0; i < nparts; i++, attr++) {

		char ifname[MAXLINKNAMELEN];
		ill_t *ill;
		ipif_t *ipif;
		netstack_t *stack;
		ip_stack_t *ipst;
		sdp_stack_t *sdps;
		ip_laddr_t addr_type;
		zoneid_t zoneid;

		/*
		 * Check whether this is the IB Partition that we are
		 * interested in
		 */
		if (attr->pa_hca_guid != cm_event->cm_event.req.req_hca_guid ||
		    attr->pa_pkey != cm_event->cm_event.req.req_pkey ||
		    attr->pa_port_guid != cm_event->cm_event.req.
		    req_prim_addr.av_sgid.gid.ucast_gid.ugid_guid) {
			continue;
		}

		/*
		 * We found the match ibp, now get the corresponding ill_t and
		 * the stack instance it belongs to.
		 */
		if (dls_devnet_get_active_linkname(attr->pa_plinkid, &zoneid,
		    ifname, MAXLINKNAMELEN) != 0) {
			dprint(SDP_DBG, ("sdp_inet_listen_lookup: "
			    "dls_devnet_get_active_linkname %d failed",
			    attr->pa_plinkid));
			continue;
		}

		stack = netstack_find_by_zoneid(zoneid);
		ipst = stack->netstack_ip;
		ASSERT(ipst != NULL);
		sdps = stack->netstack_sdp;

		if ((ill = ill_lookup_on_name(ifname, B_FALSE, !isv4, NULL,
		    ipst)) == NULL) {
			dprint(SDP_DBG, ("sdp_inet_listen_lookup: failed to "
			    "find ill for %s", ifname));
			netstack_rele(stack);
			continue;
		}

		/*
		 * find a listening connection listening on the IP address
		 */
		mutex_enter(&sdps->sdps_listen_lock);
		for (conn2 = sdps->sdps_listen_list; conn2 != NULL;
		    conn2 = conn2->lstn_next) {
			dprint(SDP_DBG, ("%s: conn2-src-port :%d port %d "
			    "conn2->sdp_family = %d eager-sdp_family = %d "
			    "conn2->srcv6 %08x:%08x:%08x:%08x "
			    "eager-conn->srcv6 %08x:%08x:%08x:%08x\n",
			    __func__, conn2->src_port, port,
			    conn2->sdp_ipversion, eager_conn->sdp_ipversion,
			    conn2->conn_srcv6.s6_addr32[0],
			    conn2->conn_srcv6.s6_addr32[1],
			    conn2->conn_srcv6.s6_addr32[2],
			    conn2->conn_srcv6.s6_addr32[3],
			    eager_conn->conn_srcv6.s6_addr32[0],
			    eager_conn->conn_srcv6.s6_addr32[1],
			    eager_conn->conn_srcv6.s6_addr32[2],
			    eager_conn->conn_srcv6.s6_addr32[3]));
			if (conn2->state != SDP_CONN_ST_LISTEN) {
				/*
				 * (Hack) Skip any listener stuck in close.
				 */
				continue;
			}
			if (port != conn2->src_port)
				continue;

			if (((conn2->sdp_ipversion !=
			    eager_conn->sdp_ipversion) ||
			    (!(V6_OR_V4_INADDR_ANY(conn2->conn_srcv6)) &&
			    (bcmp(&eager_conn->conn_srcv6,
			    &conn2->conn_srcv6, sizeof (in6_addr_t)) != 0))) &&
			    ((conn2->sdp_ipversion != IPV6_VERSION) ||
			    !V6_OR_V4_INADDR_ANY(conn2->conn_srcv6) ||
			    (conn2->inet_ops.ipv6_v6only))) {
				continue;
			}

			ASSERT(conn2->sdp_stack == stack);

			/*
			 * For exclusive stacks we set the zoneid to zero
			 * to make the operate as if in the global zone.
			 */
			zoneid = (stack->netstack_stackid !=
			    GLOBAL_NETSTACKID) ? GLOBAL_ZONEID :
			    conn2->sdp_zoneid;

			/*
			 * Loopback
			 *
			 * Note that for loopback traffic, the remote IP is
			 * filled in with a valid IP address in the originated
			 * zone and the hello message was sent from the
			 * the ill_t owns that IP address (see
			 * sdp_pr_loopback()), which is also the ill_t receiving
			 * the hello message. This is now used to determine
			 * whether the listening connection we found above is
			 * in the right zone in which this connection is
			 * originated from.
			 *
			 * Non-loopback
			 *
			 * Query into IP to validate the destination IP
			 * is on this receiving ill_t
			 */
			addr_type = IPVL_UNICAST_UP;
			if (isv4 &&
			    eager_conn->conn_src == htonl(INADDR_LOOPBACK)) {
				ipif = ipif_lookup_up_addr(eager_conn->conn_rem,
				    ill, zoneid, ipst);
				addr_type = ip_laddr_verify_v4(
				    eager_conn->conn_src, zoneid, ipst,
				    B_FALSE);
			} else if (!isv4 && IN6_IS_ADDR_LOOPBACK(
			    &eager_conn->saddr)) {
				ipif  = ipif_lookup_up_addr_v6(
				    &eager_conn->conn_remv6, ill, zoneid, ipst);
				addr_type = ip_laddr_verify_v6(
				    &eager_conn->conn_srcv6, zoneid, ipst,
				    B_FALSE, 0);
			} else if (isv4) {
				ipif = ipif_lookup_up_addr(eager_conn->conn_src,
				    ill, zoneid, ipst);
			} else {
				ipif = ipif_lookup_up_addr_v6(
				    &eager_conn->conn_srcv6, ill, zoneid, ipst);
			}

			if (ipif == NULL || addr_type != IPVL_UNICAST_UP) {
				if (ipif != NULL)
					ipif_refrele(ipif);
				continue;
			}
			ipif_refrele(ipif);

			/*
			 * the connection is the ame as the last match we
			 * found, which means the same IP address is valid on
			 * two different ill_t, this is only possible if
			 * the ill is in an IPMP group
			 */
			if (conn2 == match_conn)
				continue;

			/* more than one match, unsupported configuration */
			if (match_conn != NULL) {
				mutex_exit(&sdps->sdps_listen_lock);
				ill_refrele(ill);
				netstack_rele(stack);
				SDP_CONN_PUT(match_conn);
				(void) ibt_free_part_attr(attr_list, nparts);
				return (NULL);
			}

			/*
			 * Continue loop to see whether there is another match
			 */
			SDP_CONN_HOLD(conn2);
			match_conn = conn2;
		}
		mutex_exit(&sdps->sdps_listen_lock);
		ill_refrele(ill);
		netstack_rele(stack);
	}
	sdp_print_dbg(conn2, "sdp_inet_listen: conn:%p", (void *)match_conn);

	(void) ibt_free_part_attr(attr_list, nparts);
	return (match_conn);
}   /* sdp_inet_listen_lookup */

/* ========================================================================= */

/*
 * sdp_inet_port_get -- bind a socket to a port.
 */
int32_t
sdp_inet_port_get(sdp_conn_t *conn, sdp_binding_addr bind_ver, uint16_t port)
{
	struct sdp_inet_ops *srch_ops;
	sdp_conn_t *look;
	int32_t counter;
	uint16_t low_port;
	uint16_t top_port;
	int32_t port_ok = 1;
	int32_t result;
	sdp_stack_t *sdps;

	SDP_CHECK_NULL(conn, EINVAL);
	ASSERT(bind_ver);

	/*
	 * lock table
	 */
	sdps = conn->sdp_sdps;
	mutex_enter(&sdps->sdps_bind_lock);

	/*
	 * simple linked list of connections ordered on local port number.
	 */
	if (port > 0) {
		for (look = sdps->sdps_bind_list, port_ok = 1;
		    look != NULL; look = look->bind_next) {
			srch_ops = &look->inet_ops;

			/*
			 * 1) same port
			 * 2) force reuse is off.
			 * 3) same bound interface, or neither has a bound
			 * interface
			 * The check for state != SDP_CONN_ST_ERROR is a
			 * workaround to skip conn's stuck in close. (Hack).
			 */
			if (ntohs(look->src_port) == port &&
			    conn->sdp_zoneid == look->sdp_zoneid &&
			    !(conn->inet_ops.reuse > 1) &&
			    !(srch_ops->reuse > 1) &&
			    (conn->inet_ops.bound_dev_if ==
			    srch_ops->bound_dev_if) &&
			    (look->state != SDP_CONN_ST_ERROR)) {

				/*
				 * 3) either socket has reuse turned off
				 * 4) socket already listening on this port
				 */
				if (conn->inet_ops.reuse == 0 ||
				    srch_ops->reuse == 0 ||
				    look->state == SDP_CONN_ST_LISTEN) {

					/*
					 * 5) neither socket is using a
					 * specific address
					 * 6) both sockets are trying for the
					 * same interface.
					 */
					if (((bind_ver == sdp_binding_v4) &&
					    (V6_OR_V4_INADDR_ANY(
					    conn->conn_srcv6) ||
					    ((look->sdp_bound_state ==
					    sdp_bound_v4) &&
					    (V6_OR_V4_INADDR_ANY(
					    look->conn_srcv6)) ||
					    (IN6_ARE_ADDR_EQUAL(
					    &conn->conn_srcv6,
					    &look->conn_srcv6))))) ||
					    ((bind_ver == sdp_binding_v6) &&
					    (V6_OR_V4_INADDR_ANY(
					    conn->conn_srcv6) ||
					    ((look->sdp_bound_state ==
					    sdp_bound_v6) &&
					    (V6_OR_V4_INADDR_ANY(
					    look->conn_srcv6))||
					    (IN6_ARE_ADDR_EQUAL(
					    &conn->conn_srcv6,
					    &look->conn_srcv6)))))) {
						port_ok = 0;
						break;
					}
				}
			}
		}	/* for */
		if (port_ok == 0) {
			result = EADDRINUSE;
			goto done;
		}	/* else */

	} else {
		low_port = SDP_PORT_RANGE_LOW;
		top_port = SDP_PORT_RANGE_HIGH;
		if (sdps->sdps_port_rover < 0)
			sdps->sdps_port_rover = low_port;

		for (counter = (top_port - low_port) + 1; counter > 0;
		    counter--) {
			sdps->sdps_port_rover++;
			if (sdps->sdps_port_rover < low_port ||
			    sdps->sdps_port_rover > top_port) {
				sdps->sdps_port_rover = low_port;
			}
			for (look = sdps->sdps_bind_list;
			    look != NULL &&
			    look->src_port != sdps->sdps_port_rover;
			    look = look->bind_next) {
				/*
				 * pass
				 */
			}	/* for */

			if (look == NULL) {
				/*
				 * if we find the port in priv ports list,
				 * advancing port_rover
				 */
				port = (uint16_t)sdps->sdps_port_rover;
				if (!sdp_is_extra_priv_port(sdps, port))
					break;
			}	/* else */
		}	/* for */

		if (port == 0) {
			result = EADDRINUSE;
			goto done;
		}	/* else */
	}	/* else */

	conn->src_port = htons(port);

	/*
	 * insert into bind list.
	 */
	conn->bind_next = sdps->sdps_bind_list;
	sdps->sdps_bind_list = conn;
	conn->bind_p_next = &sdps->sdps_bind_list;

	if (conn->bind_next != NULL) {
		conn->bind_next->bind_p_next = &conn->bind_next;
	}	/* if */
	result = 0;
done:
	mutex_exit(&sdps->sdps_bind_lock);
	return (result);
}   /* sdp_inet_port_get */

/* ========================================================================= */

/*
 * sdp_inet_port_put -- unbind a socket from a port.
 */
int32_t
sdp_inet_port_put(sdp_conn_t *conn)
{
	sdp_stack_t *sdps;

	SDP_CHECK_NULL(conn, EINVAL);

	sdp_print_dbg(conn, "sdp_inet_port_put: port:<%u>",
	    ntohs(conn->src_port));

	if (conn->src_port) {
		if (conn->bind_p_next == NULL) {
			return (EADDRNOTAVAIL);
		}	/* if */

		sdps = conn->sdp_sdps;
		/*
		 * lock table
		 */
		mutex_enter(&sdps->sdps_bind_lock);

		/*
		 * remove from bind list.
		 */
		if (conn->bind_next != NULL) {
			conn->bind_next->bind_p_next = conn->bind_p_next;
		}	/* if */
		*(conn->bind_p_next) = conn->bind_next;

		conn->bind_p_next = NULL;
		conn->bind_next = NULL;
		conn->src_port = 0;

		mutex_exit(&sdps->sdps_bind_lock);
	}
	return (0);
}   /* sdp_inet_port_put */

/* ========================================================================= */

/*
 * sdp_inet_port_inherit -- inherit a port from another socket (accept)
 */
int32_t
sdp_inet_port_inherit(sdp_conn_t *parent, sdp_conn_t *child)
{
	int32_t result;
	sdp_stack_t *sdps;

	SDP_CHECK_NULL(child, -EINVAL);
	SDP_CHECK_NULL(parent, -EINVAL);
	SDP_CHECK_NULL(parent->bind_p_next, -EINVAL);

	/*
	 * lock table
	 */
	sdps = parent->sdp_sdps;
	mutex_enter(&sdps->sdps_bind_lock);

	if (child->bind_p_next != NULL ||
	    child->src_port != parent->src_port) {
		result = -EADDRNOTAVAIL;
		goto done;
	}

	/*
	 * insert into listening list.
	 */
	child->bind_next = parent->bind_next;
	parent->bind_next = child;
	child->bind_p_next = &parent->bind_next;

	if (child->bind_next != NULL) {
		child->bind_next->bind_p_next = &child->bind_next;
	}	/* if */
	result = 0;
done:
	mutex_exit(&sdps->sdps_bind_lock);
	return (result);
}   /* sdp_inet_port_inherit */

/* ========================================================================= */

/*
 * sdp_conn_table_insert -- insert a connection into the connection table
 */
int32_t
sdp_conn_table_insert(sdp_conn_t *conn)
{
	int32_t counter;
	int32_t result = -ENOMEM;

	SDP_CHECK_NULL(conn, -EINVAL);

	/*
	 * before inserting a connection into the connection table, check if
	 * SDP is in SDP_ATTACHED state. If it is detaching we shouldn't insert
	 * this connection.
	 */
	mutex_enter(&_dev_root_s.sdp_state_lock);
	if (_dev_root_s.sdp_state != SDP_ATTACHED) {
		mutex_exit(&_dev_root_s.sdp_state_lock);
		return (-EINVAL);
	}
	/*
	 * lock table
	 */
	mutex_enter(&_dev_root_s.sock_lock);

	/*
	 * find an empty slot.
	 */
	for (counter = 0; counter < _dev_root_s.conn_array_size;
	    counter++, _dev_root_s.conn_array_rover++) {
		if (!(_dev_root_s.conn_array_rover <
		    _dev_root_s.conn_array_size)) {
			_dev_root_s.conn_array_rover = 0;
		}	/* if */
		if (_dev_root_s.conn_array[_dev_root_s.conn_array_rover] ==
		    NULL) {
			_dev_root_s.conn_array[_dev_root_s.conn_array_rover] =
			    conn;
			/* LINTED */
			ASSERT(_dev_root_s.conn_array_num_entries >= 0);
			_dev_root_s.conn_array_num_entries++;
			conn->sdp_hashent = _dev_root_s.conn_array_rover++;
			result = 0;
			break;
		}	/* if */
	}	/* for */

	/*
	 * unlock table
	 */
	mutex_exit(&_dev_root_s.sock_lock);
	mutex_exit(&_dev_root_s.sdp_state_lock);
	return (result);
}   /* sdp_conn_table_insert */

static int
sdpconn_update(kstat_t *ksp, int rw)
{
	int ns;
	int counter;
	sdp_conn_t *connp;
	zoneid_t   myzoneid = (zoneid_t)(uintptr_t)ksp->ks_private;

	ASSERT((zoneid_t)(uintptr_t)ksp->ks_private == getzoneid());

	if (rw == KSTAT_WRITE) {	/* bounce all writes */
		return (EACCES);
	}

	ASSERT(MUTEX_HELD(&_dev_root_s.sock_lock));
	ns = 0;
	for (counter = 0; counter < _dev_root_s.conn_array_size; counter++) {
		connp = _dev_root_s.conn_array[counter];
		if (connp == NULL || connp->sdp_zoneid != myzoneid ||
		    (connp->sdp_ib_refcnt == 0 &&
		    connp->sdp_inet_refcnt == 0)) {
			continue;
		}
		ns++;
	}
	ksp->ks_ndata = ns;
	ksp->ks_data_size = ns * sizeof (sdp_connect_info_t);
	return (0);
}

void
sdp_state(int state, char *c)
{
	char *ptr;
	/*
	 * state is represented in no more than 3 characters.
	 */
	switch (state) {
	case SDP_CONN_ST_LISTEN:
		ptr = "LST";
		break;
	case SDP_CONN_ST_ESTABLISHED:
		ptr = "EST";
		break;
	case SDP_CONN_ST_REQ_PATH:
		/*
		 * Path lookup
		 */
		ptr = "PL";
		break;
	case SDP_CONN_ST_REQ_SENT:
		/*
		 * Hello request sent
		 */
		ptr = "HS";
		break;
	case SDP_CONN_ST_REQ_RECV:
		/*
		 * Hello Request Recvd
		 */
		ptr = "HR";
		break;
	case SDP_CONN_ST_REP_RECV:
		/*
		 * Hello Ack Recvd
		 */
		ptr = "HAR";
		break;
	case SDP_CONN_ST_RTU_SENT:
		/*
		 * Hello Ack sent
		 */
		ptr = "HAS";
		break;
	case SDP_CONN_ST_DIS_RECV_1:
		/*
		 * Fin received
		 */
		ptr = "DR";
		break;
	case SDP_CONN_ST_DIS_SEND_1:
		/*
		 * Fin sent
		 */
		ptr = "DS";
		break;
	case SDP_CONN_ST_DIS_SENT_1:
		/*
		 * Fin Ack recvd
		 */
		ptr = "DSA";
		break;
	case SDP_CONN_ST_DIS_RECV_R:
		/*
		 * Simultaneous Disconnect
		 */
		ptr = "DRC";
		break;
	case SDP_CONN_ST_DIS_SEND_2:
		/*
		 * Disconnect sent, the other side is already closed
		 */
		ptr = "DSC";
		break;
	case SDP_CONN_ST_TIME_WAIT_1:
		ptr = "TW1";
		break;
	case SDP_CONN_ST_TIME_WAIT_2:
		ptr = "TW2";
		break;
	case SDP_CONN_ST_CLOSED:
		ptr = "CLD";
		break;
	case SDP_CONN_ST_ERROR:
		/*
		 * Closed Error
		 */
		ptr = "ERR";
		break;
	case SDP_CONN_ST_INVALID:
		ptr = "INV";
		break;
	deafult:
		ptr = "UN";
		break;
	}
	ASSERT(strlen(ptr) <= SCI_STATE_LEN);
	bcopy(ptr, c, strlen(ptr));
}

static int
sdpconn_snapshot(kstat_t *ksp, void *buf, int rw)
{
	int			ns;
	sdp_connect_info_t	*ksci;
	zoneid_t		myzoneid = (zoneid_t)(uintptr_t)ksp->ks_private;
	sdp_conn_t		*connp;
	int			counter;

	ASSERT((zoneid_t)(uintptr_t)ksp->ks_private == getzoneid());

	ASSERT(MUTEX_HELD(&_dev_root_s.sock_lock));

	ksp->ks_snaptime = gethrtime();

	if (rw == KSTAT_WRITE) {	/* bounce all writes */
		return (EACCES);
	}

	ksci = (sdp_connect_info_t *)buf;
	ns = 0;

	ASSERT(MUTEX_HELD(&_dev_root_s.sock_lock));

	for (counter = 0; counter < _dev_root_s.conn_array_size; counter++) {
		connp = _dev_root_s.conn_array[counter];
		if (connp == NULL || connp->sdp_zoneid != myzoneid ||
		    (connp->sdp_ib_refcnt == 0 &&
		    connp->sdp_inet_refcnt == 0)) {
			continue;
		}
		/*
		 * If the sonode was activated between the update and the
		 * snapshot, we're done - as this is only a snapshot.
		 */
		if ((caddr_t)(ksci) >= (caddr_t)buf + ksp->ks_data_size) {
			break;
		}
		ksci->sci_size = sizeof (sdp_connect_info_t);
		sdp_state(connp->state, ksci->sci_state);
		/* ksci->sci_state = connp->state; */
		ksci->sci_zoneid = connp->sdp_zoneid;
		ksci->sci_family = connp->sdp_family_used;
		if (ksci->sci_family == AF_INET_SDP)
			ksci->sci_family = AF_INET;
		ksci->sci_lport = connp->src_port;
		ksci->sci_fport = connp->dst_port;
		if (ksci->sci_family == AF_INET) {
			ksci->sci_laddr[0] = connp->conn_src;
			ksci->sci_faddr[0] = connp->conn_rem;
		} else {
			bcopy(connp->saddr.s6_addr, ksci->sci_laddr,
			    sizeof (in6_addr_t));
			bcopy(connp->faddr.s6_addr, ksci->sci_faddr,
			    sizeof (in6_addr_t));
		}
		ksci->sci_lbuff_size = connp->sdpc_local_buff_size;
		ksci->sci_rbuff_size = connp->sdpc_remote_buff_size;
		ksci->sci_recv_bytes_pending = connp->sdp_recv_byte_strm;
		ksci->sci_lbuff_posted = connp->l_recv_bf;
		ksci->sci_lbuff_advt = connp->l_advt_bf;
		ksci->sci_rbuff_advt = connp->r_recv_bf;
		ksci->sci_tx_bytes_queued = connp->sdpc_tx_bytes_queued;
		ksci->sci_tx_bytes_unposted = connp->sdpc_tx_bytes_unposted;
		ns++;
		ksci++;

	}

	ksp->ks_ndata = ns;
	return (0);
}

/* initialize zone specific kstat related items */
static void *
sdpconn_kstat_init(zoneid_t zoneid)
{
	kstat_t *ksp;

	ksp = kstat_create_zone("sdpib", 0, "sdp_conn_stats", "misc",
	    KSTAT_TYPE_RAW, 0, KSTAT_FLAG_VAR_SIZE|KSTAT_FLAG_VIRTUAL, zoneid);

	if (ksp != NULL) {
		ksp->ks_update = sdpconn_update;
		ksp->ks_snapshot = sdpconn_snapshot;
		ksp->ks_lock = &_dev_root_s.sock_lock;
		ksp->ks_private = (void *)(uintptr_t)zoneid;
		kstat_install(ksp);
	}
	return (ksp);
}

static void
sdpconn_kstat_fini(zoneid_t zoneid, void *arg)
{
	kstat_t *ksp = (kstat_t *)arg;

	if (ksp != NULL) {
		ASSERT(zoneid == (zoneid_t)(uintptr_t)ksp->ks_private);
		kstat_delete(ksp);
	}
}


/* ========================================================================= */

/*
 * sdp_conn_table_remove -- remove a connection from the connection table
 */
static void
sdp_conn_table_remove(sdp_conn_t *conn)
{
	SDP_CHECK_NULL(conn, -EINVAL);

	mutex_enter(&_dev_root_s.sock_lock);

	/*
	 * At this point the conn should only be in the conn table.
	 */

	ASSERT(conn->sdp_ib_refcnt == 0 && conn->sdp_inet_refcnt == 0);

	/*
	 * validate entry
	 */
	ASSERT(conn != NULL);
	ASSERT(conn->sdp_hashent != SDP_DEV_SK_INVALID);
	ASSERT(conn->sdp_hashent >= 0);
	ASSERT(conn == _dev_root_s.conn_array[conn->sdp_hashent]);
	/*
	 * Make sure that the conn is not in the listen list
	 */
	ASSERT(conn->accept_next == NULL);
	ASSERT(conn->accept_prev == NULL);

	/*
	 * drop entry
	 */
	_dev_root_s.conn_array[conn->sdp_hashent] = NULL;
	conn->sdp_hashent = SDP_DEV_SK_INVALID;

	ASSERT(_dev_root_s.conn_array_num_entries > 0);
	_dev_root_s.conn_array_num_entries--;
	mutex_exit(&_dev_root_s.sock_lock);
}

/* ========================================================================= */

/*
 * sdp_conn_deallocate -- connection deallocation
 */
void
sdp_conn_deallocate(sdp_conn_t *conn)
{

	sdp_print_dbg(conn, "sdp_conn_dealloc: dealloc with sport:%d",
	    ntohs(conn->src_port));

	ASSERT(MUTEX_HELD(&conn->sdp_reflock));
	ASSERT(conn->sdp_ib_refcnt == 0 && conn->sdp_inet_refcnt == 0);

	sdp_conn_table_remove(conn);
	if (conn->sdp_credp != NULL) {
		crfree(conn->sdp_credp);
		conn->sdp_credp = NULL;
	}

	(void) sdp_inet_port_put(conn);

	if (conn->sdp_stack != NULL) {
		SDP_STAT_DEC(conn->sdp_sdps, ConnTableSize);
		netstack_rele(conn->sdp_stack);
		conn->sdp_stack = NULL;
		conn->sdp_zoneid = (zoneid_t)-1;
	}

	/*
	 * clear the buffer pools
	 */
	(void) sdp_buff_pool_destroy(&conn->recv_pool);
	(void) sdp_buff_pool_destroy(&conn->send_post);
	(void) sdp_buff_pool_destroy(&conn->recv_post);

	cv_destroy(&conn->closecv);
	cv_destroy(&conn->ss_rxdata_cv);
	cv_destroy(&conn->ss_txdata_cv);

	/*
	 * clear advertisement tables
	 */
	(void) sdp_conn_advt_table_destroy(&conn->src_pend);
	(void) sdp_conn_advt_table_destroy(&conn->src_actv);
	(void) sdp_conn_advt_table_destroy(&conn->snk_pend);

	/*
	 * generic table clear
	 */
	sdp_generic_table_destroy(&conn->send_ctrl);
	sdp_generic_table_destroy(&conn->send_queue);

	mutex_destroy(&conn->conn_lock);
	mutex_destroy(&conn->sdp_reflock);
	kmem_cache_free(_dev_root_s.conn_cache, conn);
}

/*
 * sdp_conn_deallocate_ib -- deallocate all ib resources
 */
void
sdp_conn_deallocate_ib(sdp_conn_t *conn)
{
	sdp_state_t *sdp_state;
	sdp_dev_hca_t *hcap = NULL;

	ASSERT(conn->sdp_ib_refcnt == 0);
	ASSERT(conn->state != SDP_CONN_ST_ESTABLISHED);
	sdp_state = conn->sdp_global_state;

	/*
	 * clear the buffer pools
	 */
	(void) sdp_buff_pool_clear(&conn->recv_pool);
	(void) sdp_buff_pool_clear(&conn->send_post);
	(void) sdp_buff_pool_clear(&conn->recv_post);

	/*
	 * clear advertisement tables
	 */
	(void) sdp_conn_advt_table_clear(&conn->src_pend);
	(void) sdp_conn_advt_table_clear(&conn->src_actv);
	(void) sdp_conn_advt_table_clear(&conn->snk_pend);

	/*
	 * generic table clear
	 */
	sdp_generic_table_clear(&conn->send_ctrl);
	sdp_generic_table_clear(&conn->send_queue);

	if (conn->channel_hdl != NULL) {
		(void) ibt_free_channel(conn->channel_hdl);
		conn->channel_hdl = NULL;
	}

	if (conn->scq_hdl != NULL) {
		ibt_set_cq_handler(conn->scq_hdl, 0, 0);
		(void) ibt_free_cq(conn->scq_hdl);
		conn->scq_hdl = NULL;
	}

	if (conn->rcq_hdl != NULL) {
		ibt_set_cq_handler(conn->rcq_hdl, 0, 0);
		(void) ibt_free_cq(conn->rcq_hdl);
		conn->rcq_hdl = NULL;
	}

	if (conn->hca_hdl != NULL) {
		/*
		 * Look up correct HCA
		 */
		mutex_enter(&sdp_state->hcas_mutex);
		hcap = get_hcap_by_hdl(sdp_state, conn->hca_hdl);
		if (hcap != NULL && --hcap->hca_num_conns == 0)
			cv_broadcast(&sdp_state->hcas_cv);
		mutex_exit(&sdp_state->hcas_mutex);
		conn->hca_hdl = NULL;
	}
	conn->hcap = NULL;
	conn->pd_hdl = NULL;
	conn->sdp_conn_buff_cache = NULL;
}

/*
 * sdp_conn_destruct -- final destructor for connection.
 */
void
sdp_conn_destruct(sdp_conn_t *conn)
{
	sdp_conn_t	*tconn;

	/*
	 * There should be no need to hold CONN lock because
	 * the conn is not be visible to the application.
	 * Only IB knows about it and we are about to tell
	 * IB to revoke those references.
	 * Note: holding CONN lock can result in a deadlock if
	 * IB callback happens before IB references are revoked.
	 */
	while (conn != NULL) {
		tconn = conn->delete_next;
		SDP_CONN_PUT(conn);
		conn = tconn;
	}
}

/*
 * Put conn on delete list and signal delete_thread to clean up
 */
void
sdp_conn_destruct_isr(sdp_conn_t *conn)
{
	mutex_enter(&_dev_root_s.delete_lock);
	conn->delete_next = (sdp_conn_t *)_dev_root_s.delete_list;
	_dev_root_s.delete_list	= conn;
	cv_signal(&_dev_root_s.delete_cv);
	mutex_exit(&_dev_root_s.delete_lock);
}

void
sdp_conn_set_buff_limits(sdp_conn_t *conn)
{
	int old_num_of_buffs;

	old_num_of_buffs = conn->sdpc_max_rbuffs;
	/*
	 * sdpc_send_buff_size size is to smaller of what we can send and what
	 * the peer can accept
	 */
	conn->sdpc_send_buff_size = min(conn->sdpc_local_buff_size,
	    conn->sdpc_remote_buff_size);

	conn->sdpc_max_rbuffs = conn->sdpc_max_rwin/conn->sdpc_send_buff_size;
	ASSERT(conn->sdpc_max_rbuffs >= SDP_MIN_BUFF_COUNT);
	ASSERT(conn->sdpc_max_rbuffs >= old_num_of_buffs);
	sdp_set_sorcvbuf(conn);
}

/* ========================================================================= */


/*
 * sdp_conn_allocate_ib -- allocate IB structures for a new connection.
 */
int32_t
sdp_conn_allocate_ib(sdp_conn_t *conn, ib_guid_t *hca_guid,
    uint8_t hw_port, boolean_t where)
{
	sdp_dev_port_t *port = NULL;
	sdp_dev_hca_t *hcap = NULL;
	int32_t result = 0;
	sdp_state_t *state = &_dev_root_s;

	SDP_CHECK_NULL(conn, -EINVAL);

	/*
	 * Look up correct HCA and port.
	 */
	mutex_enter(&state->hcas_mutex);
	for (hcap = state->hca_list; hcap != NULL; hcap = hcap->next) {
		if (*hca_guid == hcap->guid) {
			for (port = hcap->port_list; port != NULL;
			    port = port->next) {
				if (hw_port == port->index) {
					break;
				}	/* if */
			}	/* for */
			break;
		}	/* if */
	}	/* for */

	if (hcap == NULL || port == NULL || hcap->sdp_hca_offlined) {
		mutex_exit(&state->hcas_mutex);
		return (ERANGE);
	} /* if */
	hcap->hca_num_conns++;
	mutex_exit(&state->hcas_mutex);

	/*
	 * Set port-specific connection parameters.
	 */
	conn->hcap	= hcap;
	conn->hca_hdl	= hcap->hca_hdl;
	conn->pd_hdl	= hcap->pd_hdl;
	conn->hw_port	= port->index;
	conn->sdp_conn_buff_cache = hcap->sdp_hca_buff_cache;
	conn->ib_inline_max = min(hcap->hca_inline_max,
	    (uint16_t)sdp_inline_bytes_max);

	conn->sdpc_local_buff_size = hcap->hca_sdp_buff_size;

	conn->sdpc_max_rbuffs =
	    conn->sdpc_max_rwin / conn->sdpc_local_buff_size;

	ASSERT(conn->sdpc_max_rbuffs >= SDP_MIN_BUFF_COUNT);

	sdp_buff_pool_init(&conn->recv_post);
	sdp_buff_pool_init(&conn->recv_pool);
	sdp_buff_pool_init(&conn->send_post);

	if (conn->scq_hdl == NULL) {
		ibt_cq_attr_t cq_atts;

		cq_atts.cq_size = conn->scq_size;
		cq_atts.cq_sched = hcap->sdp_hca_cq_sched_hdl;
		cq_atts.cq_flags = hcap->sdp_hca_cq_sched_hdl == NULL ?
		    IBT_CQ_NO_FLAGS : IBT_CQS_SCHED_GROUP;
		result = ibt_alloc_cq(conn->hca_hdl, &cq_atts, &conn->scq_hdl,
		    &conn->scq_size);
		if (result != IBT_SUCCESS) {
			sdp_print_warn(conn, "sdp_conn_allocate_ib: error<%d>"
			    " creating scq completion queue ", result);
			if ((conn->inet_ops.debug)) {
				(void) strlog(SDP_MODULE_ID, 0, 1,
				    SL_TRACE | SL_ERROR,
				    "sdp_conn_allocate_ib: error <%d>"
				    "create scq completion queue (size <%d>)",
				    result, conn->scq_size);
			}
			result = -EPROTO;
			conn->scq_hdl = NULL;
			goto out_error;
		}	/* if failure to alloc cq */
		/* Notify after the sooner of 8 send completions or 50 usecs */
		(void) ibt_modify_cq(conn->scq_hdl, 8, 50, 0);

		ibt_set_cq_private(conn->scq_hdl, (void *)conn);
		ibt_set_cq_handler(conn->scq_hdl, sdp_scq_handler,
		    (void *)conn);
	} /* if no recv cq handle yet */
	if (conn->rcq_hdl == NULL) {
		ibt_cq_attr_t cq_atts;

		cq_atts.cq_size = conn->rcq_size;
		cq_atts.cq_sched = hcap->sdp_hca_cq_sched_hdl;
		cq_atts.cq_flags = hcap->sdp_hca_cq_sched_hdl == NULL ?
		    IBT_CQ_NO_FLAGS : IBT_CQS_SCHED_GROUP;
		result = ibt_alloc_cq(conn->hca_hdl, &cq_atts,
		    &conn->rcq_hdl, &conn->rcq_size);
		if (result != IBT_SUCCESS) {
			sdp_print_warn(conn, "sdp_conn_allocate_ib: error<%d>"
			    " creating rx completion queue", result);
			if (conn->inet_ops.debug)
				(void) strlog(SDP_MODULE_ID, 0, 1,
				    SL_TRACE | SL_ERROR, "sdp_conn_alloc_ib: "
				    "error <%d> creating rx completion "
				    "queue (size <%d>)", result,
				    conn->rcq_size);
			result = -EPROTO;
			conn->rcq_hdl = NULL;
			goto out_error;
		}	/* if failure to alloc cq */
		ibt_set_cq_private(conn->rcq_hdl, (void *)conn);
		ibt_set_cq_handler(conn->rcq_hdl, sdp_rcq_handler,
		    (void *)conn);
	} /* if no recv cq handle yet */

	/*
	 * enable completions
	 */
	result = ibt_enable_cq_notify(conn->scq_hdl, IBT_NEXT_COMPLETION);
	if (result != IBT_SUCCESS) {
		sdp_print_warn(NULL, "sdp_conn_allocate_ib: "
		    "ibt_enable_cq_notify(scq) failed: %d", result);
		if (conn->inet_ops.debug) {
			(void) strlog(SDP_MODULE_ID, 0, 1,
			    SL_TRACE | SL_ERROR, "sdp_conn_allocate_ib:"
			    " ibt_enable_cq_notify(scq)"
			    " failed: status %d\n", result);
		}
		result = -EPROTO;
		goto out_error;
	}

	if (where == 1) {
		result = ibt_enable_cq_notify(conn->rcq_hdl,
		    IBT_NEXT_COMPLETION);
		if (result != IBT_SUCCESS) {
			sdp_print_warn(NULL, "sdp_conn_allocate_ib: "
			    "ibt_enable_cq_notify(rcq) failed: %d", result);
			if (conn->inet_ops.debug) {
				(void) strlog(SDP_MODULE_ID, 0, 1,
				    SL_TRACE | SL_ERROR,
				    "sdp_conn_allocate_ib: "
				    "ibt_enable_cq_notify(rcq) failed: %d\n",
				    result);
			}
			result = -EPROTO;
			goto out_error;
		}
	}
	return (0);

out_error:
	/*
	 * Resources freed when conn is destroyed.
	 */
	return (result);

}   /* sdp_conn_allocate_ib  */

/* ========================================================================= */

/*
 * Set stack-instance/zone related field
 */
boolean_t
sdp_conn_init_cred(sdp_conn_t *conn, cred_t *credp)
{
	netstack_t *ns;

	if ((ns = netstack_find_by_cred(credp)) == NULL) {
		sdp_print_warn(conn, "sdp_conn_init_cred: find netstack "
		    "failed %d", crgetzoneid(credp));
		return (B_FALSE);
	}

	crhold(credp);
	conn->sdp_credp = credp;
	conn->sdp_stack = ns;
	conn->sdp_zoneid = crgetzoneid(credp);
	SDP_STAT_INC(conn->sdp_sdps, ConnTableSize);
	return (B_TRUE);
}

/*
 * sdp_conn_allocate -- allocate a new socket, and init.
 * - passive: indicate whether this connection is a passive one
 */
sdp_conn_t *
sdp_conn_allocate(boolean_t passive)
{
	sdp_conn_t *conn;
	int32_t result;

	conn = kmem_cache_alloc(_dev_root_s.conn_cache, KM_SLEEP);
	if (conn == NULL) {
		return (NULL);
	}

	bzero(conn, sizeof (sdp_conn_t));

	/*
	 * the STRM interface specific data is map/cast over the TCP specific
	 * area of the sock.
	 */

	SDP_CONN_ST_INIT(conn);

	conn->oob_offset = -1;

	conn->nodelay = 0;
	conn->sdp_bound_state = sdp_unbound;

	conn->accept_next = NULL;
	conn->accept_prev = NULL;
	conn->parent = NULL;
	conn->delete_next = NULL;

	conn->sdp_hashent = SDP_DEV_SK_INVALID;
	conn->flags = 0;
	conn->shutdown = SDP_SHUTDOWN_NONE;
	conn->recv_mode = SDP_MODE_COMB;
	conn->send_mode = SDP_MODE_COMB;

	conn->send_seq = 0;
	conn->recv_seq = 0;
	conn->advt_seq = 0;

	conn->nond_recv = 0;
	conn->nond_send = 0;

	conn->s_wq_size = 0;

	conn->sdpc_tx_max_queue_size = 0;
	conn->sdpc_tx_bytes_queued = 0;
	conn->sdpc_tx_bytes_unposted = 0;
	conn->sdpc_remote_buff_size = 0;
	conn->sdpc_send_buff_size = 0;

	conn->src_recv = 0;
	conn->snk_recv = 0;

	conn->send_bytes = 0;
	conn->recv_bytes = 0;
	conn->write_bytes = 0;
	conn->read_bytes = 0;
	conn->sdp_recv_byte_strm = 0;
	conn->sdp_recv_oob_msg = 0;
	conn->sdp_recv_oob_offset = 0;
	conn->sdp_recv_oob_state = 0;

	conn->send_usig = 0;
	conn->send_cons = 0;

	conn->s_wq_cur = SDP_DEV_SEND_POST_MAX;
	conn->s_wq_par = 0;

	conn->rcq_size = SDP_DEV_RECV_CQ_SIZE;
	conn->scq_size = SDP_DEV_SEND_CQ_SIZE;

	conn->scq_hdl = NULL;
	conn->rcq_hdl = NULL;
	conn->hcap = NULL;
	conn->hca_hdl = NULL;
	conn->pd_hdl = NULL;
	conn->channel_hdl = NULL;

	conn->hw_port = IBT_HCA_PORT_INVALID;

	conn->sdp_conn_buff_cache = NULL;

	conn->xmitflags = 0;
	conn->sdp_zoneid = (zoneid_t)-1;
	conn->sdp_stack = NULL;

	/*
	 * All the following will be initialized again  in sdp_conn_init_cred().
	 */
	conn->sdpc_max_rbuffs = 0;
	conn->sdpc_max_rwin = 0;

	/*
	 * generic send queue
	 */
	result = sdp_generic_table_init(&conn->send_queue);
	SDP_EXPECT(!(0 > result));
	result = sdp_generic_table_init(&conn->send_ctrl);
	SDP_EXPECT(!(0 > result));

	/*
	 * Initialize zcopy advertisement tables
	 * This code is needed for interoperability with Linux.
	 */
	result = sdp_conn_advt_table_init(&conn->src_pend);
	SDP_EXPECT(!(0 > result));
	result = sdp_conn_advt_table_init(&conn->src_actv);
	SDP_EXPECT(!(0 > result));
	result = sdp_conn_advt_table_init(&conn->snk_pend);
	SDP_EXPECT(!(0 > result));

	cv_init(&conn->closecv, "sdp conn close cv", CV_DRIVER, NULL);
	cv_init(&conn->ss_rxdata_cv, "sdp conn recv cv", CV_DRIVER, NULL);
	cv_init(&conn->ss_txdata_cv, "sdp conn tx cv", CV_DRIVER, NULL);
	mutex_init(&conn->conn_lock, "SDP connection lock", MUTEX_DRIVER,
	    NULL);
	mutex_init(&conn->sdp_reflock, "SDP refrence cnt lock", MUTEX_DRIVER,
	    NULL);

	conn->sdp_global_state = &_dev_root_s;

	/*
	 * set the initial version to be the highest version we support.
	 * For passive open this value will be over written in sdp_cm_req_recv()
	 * with the version that the peer sent in the connection message.
	 */
	conn->sdp_msg_version = SDP_MSG_VERSION;
	conn->recv_wrid = 0;

	conn->sdp_active_open = B_FALSE;
	/*
	 * insert connection into lookup table
	 */
	result = sdp_conn_table_insert(conn);
	if (result != 0) {
		sdp_print_warn(conn, "sdp_conn_allocate_ib: conn insert failed"
		    " <%d>", result);
		kmem_cache_free(_dev_root_s.conn_cache, conn);
		return (NULL);
	}

	/*
	 * For passive connection, the socket reference is held when the
	 * connection is added into accept_list
	 */
	if (passive) {
		SDP_CONN_HOLD(conn);		/* CM reference */
	} else {
		SDP_CONN_SOCK_HOLD(conn);	/* inet reference */
	}
	SDP_CONN_ST_SET(conn, SDP_CONN_ST_CLOSED);
	return (conn);
}

/* ========================================================================= */

static zone_key_t sdpconn_zone_key;

/*
 * sdp_conn_table_init -- create a sdp connection table
 */
/* ARGSUSED */
int32_t
sdp_conn_table_init(int32_t proto_family, int32_t conn_size,
    int32_t buff_min, int32_t buff_max, sdp_state_t **global_state)
{
	int32_t result;

	bzero(&_dev_root_s, sizeof (sdp_dev_root_t));

	_dev_root_s.sdp_state = SDP_ATTACHING;
	mutex_init(&_dev_root_s.sdp_state_lock, "SDP state lock", MUTEX_DRIVER,
	    NULL);
	mutex_init(&_dev_root_s.sock_lock, "SDP sock lock", MUTEX_DRIVER,
	    NULL);
	mutex_init(&_dev_root_s.delete_lock, "SDP delete lock", MUTEX_DRIVER,
	    NULL);
	cv_init(&_dev_root_s.delete_cv, "SDP delete_cv", CV_DEFAULT, NULL);

	_dev_root_s.exit_flag = B_FALSE;

	/*
	 * create socket table
	 */
	if (!(conn_size > 0)) {
		result = -EINVAL;
		goto error_size;
	}	/* if */

	_dev_root_s.conn_array =
	    (void *) kmem_zalloc((conn_size * sizeof (intptr_t)), KM_SLEEP);

	if (_dev_root_s.conn_array == NULL) {
		result = -ENOMEM;
		goto error_size;
	}

	/* top is reserved for * invalid */
	_dev_root_s.conn_array_size = conn_size - 1;

	/*
	 * buffer memory
	 */
	_dev_root_s.conn_cache = kmem_cache_create("sdp_conn_cache",
	    sizeof (sdp_conn_t), 0, NULL, NULL, NULL, NULL, 0, KM_SLEEP);

	if (_dev_root_s.conn_cache == NULL) {
		result = -ENOMEM;
		goto error_cache;
	}

	*global_state = &_dev_root_s;
	zone_key_create(&sdpconn_zone_key, sdpconn_kstat_init, NULL,
	    sdpconn_kstat_fini);

	sdp_print_dbg(NULL, "sdp_conn_table_init: sdp_global_state:%p",
	    (void *)*global_state);

	return (0);
error_cache:
	kmem_free(_dev_root_s.conn_array,
	    (SDP_DEV_SK_LIST_SIZE * sizeof (intptr_t)));
error_size:
	return (result);
}   /* sdp_conn_table_init */

/* ========================================================================= */

/*
 * sdp_conn_table_clear -- destroy connection managment and tables
 */
int32_t
sdp_conn_table_clear(void)
{
	ASSERT(MUTEX_NOT_HELD(&_dev_root_s.sock_lock));

	(void) zone_key_delete(sdpconn_zone_key);
	mutex_destroy(&_dev_root_s.sock_lock);
	mutex_destroy(&_dev_root_s.delete_lock);
	cv_destroy(&_dev_root_s.delete_cv);

	kmem_free(_dev_root_s.conn_array,
	    (SDP_DEV_SK_LIST_SIZE * sizeof (intptr_t)));

	/*
	 * delete conn cache
	 */
	kmem_cache_destroy(_dev_root_s.conn_cache);

	_dev_root_s.sdp_state = SDP_DETACHED;
	mutex_destroy(&_dev_root_s.sdp_state_lock);
	return (0);
}   /* sdp_conn_table_clear */

/* ARGSUSED */
int32_t
sdp_conn_error(sdp_conn_t *conn)
{
	/*
	 * The connection error parameter is set and read under the
	 * connection lock.
	 */
	int32_t error = 0;

	return (error);
}	/* sdp_conn_error */

#ifdef TRACE_REFCNT
void
SDP_CONN_HOLD(sdp_conn_t *conn)
{
	/*
	 * After the last reference is released when the connection is
	 * aborted or disconnected, no reference should be held.
	 */
	mutex_enter(&conn->sdp_reflock);
	ASSERT(conn->sdp_ib_refcnt != 0 ||
	    (conn->state != SDP_CONN_ST_CLOSED &&
	    conn->state != SDP_CONN_ST_ERROR &&
	    conn->state != SDP_CONN_ST_TIME_WAIT_1 &&
	    conn->state != SDP_CONN_ST_TIME_WAIT_2));
	conn->sdp_ib_refcnt++;
	mutex_exit(&conn->sdp_reflock);
	dprint(SDP_DBG, ("sdp_hold: conn:%p refcnt=<%d> caller=<%p>",
	    (void *)conn, conn->sdp_ib_refcnt, caller()));
}

void
SDP_CONN_PUT(sdp_conn_t *conn)
{
	dprint(SDP_DBG, ("sdp_put: conn:%p refcnt=<%d> caller=<%p>\n",
	    (void *)conn, conn->sdp_ib_refcnt, caller()));
	mutex_enter(&conn->sdp_reflock);
	if (--conn->sdp_ib_refcnt == 0) {
		sdp_conn_deallocate_ib(conn);
		if (conn->sdp_inet_refcnt == 0) {
			sdp_conn_deallocate(conn);
			return;
		}
	}
	mutex_exit(&conn->sdp_reflock);
}

void
SDP_CONN_PUT_ISR(sdp_conn_t *conn)
{
	dprint(SDP_DBG, ("sdp_put_isr: refcnt=<%d> caller=<%p>\n",
	    conn->sdp_ib_refcnt, caller()));

	/*
	 * To avoid deadlock, put the connection into the delete list and a
	 * thread is used to call SDP_CONN_PUT() on each conn on the list
	 */
	sdp_conn_destruct_isr(conn);
}

void
SDP_CONN_SOCK_HOLD(sdp_conn_t *conn)
{
	mutex_enter(&conn->sdp_reflock);
	conn->sdp_inet_refcnt++;
	mutex_exit(&conn->sdp_reflock);
	dprint(SDP_DBG, ("sdp_sock_hold: conn:%p refcnt=<%d> caller=<%p>",
	    (void *)conn, conn->sdp_ib_refcnt, caller()));
}

void
SDP_CONN_SOCK_PUT(sdp_conn_t *conn)
{
	dprint(SDP_DBG, ("sdp_sock_put: conn:%p refcnt=<%d> caller=<%p>\n",
	    (void *)conn, conn->sdp_ib_refcnt, caller()));
	mutex_enter(&conn->sdp_reflock);
	if (--conn->sdp_inet_refcnt == 0 && conn->sdp_ib_refcnt == 0)
		sdp_conn_deallocate(conn);
	else
		mutex_exit(&conn->sdp_reflock);
}

#endif	/* TRACE_RFCNT */

void	*sdp_caller;

void
sdp_mutex_enter(sdp_conn_t *conn)
{
	mutex_enter(&conn->conn_lock);
	sdp_caller = caller();
}

void *
sdp_stack_init(netstackid_t stackid, netstack_t *stack)
{
	sdp_stack_t *sdps;
	sdpparam_t *sdppap;

	sdps = (sdp_stack_t *)kmem_zalloc(sizeof (sdp_stack_t), KM_SLEEP);
	sdps->sdps_netstack = stack;
	mutex_init(&sdps->sdps_bind_lock, "SDP bind lock",
	    MUTEX_DRIVER, NULL);
	mutex_init(&sdps->sdps_listen_lock, "SDP listen lock",
	    MUTEX_DRIVER, NULL);
	sdps->sdps_port_rover = -1;

	sdppap = (sdpparam_t *)kmem_alloc(sizeof (sdp_param_arr), KM_SLEEP);
	sdps->sdps_param_arr = sdppap;
	bcopy(sdp_param_arr, sdppap, sizeof (sdp_param_arr));

	mutex_init(&sdps->sdps_epriv_port_lock, "SDP epriv ports lock",
	    MUTEX_DRIVER, NULL);
	sdps->sdps_epriv_ports[0] = ULP_DEF_EPRIV_PORT1;
	sdps->sdps_epriv_ports[1] = ULP_DEF_EPRIV_PORT2;
	sdps->sdps_num_epriv_ports = SDP_NUM_EPRIV_PORTS;

	mutex_init(&sdps->sdps_param_lock, "SDP param lock",
	    MUTEX_DRIVER, NULL);
	(void) sdp_param_register(&sdps->sdps_nd, sdppap, A_CNT(sdp_param_arr));
	sdps->sdps_kstat = sdp_kstat_init(stackid, &sdps->sdps_named_ks);

	return (sdps);
}

void
sdp_stack_destroy(netstackid_t stackid, void *arg)
{
	sdp_stack_t *sdps;

	sdps = (sdp_stack_t *)arg;

	sdp_kstat_fini(stackid, sdps->sdps_kstat);
	sdps->sdps_kstat = NULL;
	mutex_destroy(&sdps->sdps_listen_lock);
	mutex_destroy(&sdps->sdps_bind_lock);
	mutex_destroy(&sdps->sdps_epriv_port_lock);
	mutex_destroy(&sdps->sdps_param_lock);
	kmem_free(sdps->sdps_param_arr, sizeof (sdp_param_arr));
	sdps->sdps_param_arr = NULL;
	nd_free(&sdps->sdps_nd);
	kmem_free(sdps, sizeof (sdp_stack_t));
}
