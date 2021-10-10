/*
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/stream.h>
#include <sys/strsubr.h>
#include <sys/stropts.h>
#include <sys/strlog.h>
#include <sys/strsun.h>
#define	_SUN_TPI_VERSION 2
#include <sys/tihdr.h>
#include <sys/timod.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>

#include <sys/vtrace.h>

#include <sys/kmem.h>

#include <sys/errno.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <sys/sockio.h>

#include <netinet/icmp6.h>
#include <net/if.h>
#include <inet/proto_set.h>
#include <inet/common.h>
#include <inet/ip.h>
#include <inet/ip6.h>
#include <inet/ip_ire.h>
#include <sys/note.h>
#include <sys/policy.h>
#include <sys/ib/clients/sdpib/sdp_main.h>
#include <sys/ib/clients/sdpib/sdp_misc.h>
#include <sys/ib/clients/sdpib/sdp_trace_util.h>


extern sdp_state_t *sdp_global_state;

_NOTE(MUTEX_PROTECTS_DATA(sdp_conn_t::conn_lock, sdp_conn_t))

#ifndef SO_SND_COPYAVOID
#define	SO_SND_COPYAVOID	0xffff
#endif

static void sdp_ioc_hca(queue_t *, mblk_t *);

/*
 * sdp_create: Create a new conn for a socket call.
 */
/*ARGSUSED*/
sdp_conn_t *
sdp_create(sock_upper_handle_t sdp_ulpd, sdp_conn_t *parent, int family,
    int flags, const sdp_upcalls_t *sdp_upcalls, cred_t *credp, int *error)
{
	sdp_conn_t *conn;
	dev_t devp;
	sdp_state_t *state;
	dev_t new_minor_dev = sdp_get_new_inst();

	sdp_print_dbg(NULL, "sdp_create:sdp_glob_state:%p",
	    (void *)sdp_global_state);

	/* User must supply a credential. */
	if (credp == NULL) {
		*error = ENOMEM;
		return (NULL);
	}

	/* Global state needs to be initialized */
	ASSERT(sdp_global_state != NULL);

	state = (sdp_state_t *)sdp_global_state;

	/*
	 * Create new cloned instance.
	 */
	devp = makedevice(ddi_driver_major(state->dip),
	    (minor_t)new_minor_dev);

	/*
	 * Allocate a connection struct for this stream.
	 * Set connection defaults.
	 */
	conn = sdp_conn_allocate(B_FALSE);
	if (conn == NULL) {
		sdp_print_warn(NULL, "sdp_create: failed to alloc new conn: "
		    "ulpd:<%p>", (void *)sdp_ulpd);

		*error = ENOMEM;
		return (NULL);
	}

	SDP_CONN_LOCK(conn);

	if (!sdp_conn_init_cred(conn, credp)) {
		*error = EINVAL;
		SDP_CONN_UNLOCK(conn);
		SDP_CONN_SOCK_PUT(conn); /* Free up sdp_conn_alloc() ref */
		return (NULL);
	}

	conn->sdpc_local_buff_size = conn->sdp_global_state->sdp_buff_size;
	conn->sdpc_send_buff_size = conn->sdpc_local_buff_size;
	conn->sdpc_max_rwin = conn->sdp_sdps->sdp_recv_hiwat;
	conn->sdpc_tx_max_queue_size = conn->sdp_sdps->sdp_xmit_hiwat;
	conn->sdpc_max_rbuffs =
	    conn->sdpc_max_rwin / conn->sdpc_local_buff_size;
	/*
	 * Locks, refcnt, etc. have been initialized in sdp_conn_allocate.
	 */
	conn->dip = devp;
	conn->sdp_priv_stream = (drv_priv(credp) == 0) ? 1 : 0;
	ASSERT(conn->sdp_global_state != NULL);

	switch (family) {
		case AF_INET6:
			conn->sdp_ipversion = IPV6_VERSION;
			conn->sdp_family = AF_INET6;
			conn->sdp_family_used = (uint8_t)family;
			break;

		case AF_INET:
		case AF_INET_SDP:
			conn->sdp_ipversion = IPV4_VERSION;
			conn->sdp_family = AF_INET;
			conn->sdp_family_used = (uint8_t)family;
			break;
		default:
			/*
			 * lookup should have failed
			 */
			ASSERT(0);
			break;
	}
	SDP_CONN_UNLOCK(conn);
	sdp_print_ctrl(conn, "sdp_create: IPv%d socket", conn->sdp_ipversion);

	*error = 0;
	/* Information required by upper layer */
	if (sdp_ulpd != NULL) {
		conn->sdp_ulpd = sdp_ulpd;
		ASSERT(sdp_upcalls != NULL);
		bcopy(sdp_upcalls, &conn->sdp_upcalls, sizeof (sdp_upcalls_t));
		sdp_set_sorcvbuf(conn);
	}
out:
	return (conn);
}

/*
 * sdp_close: Dealloc a conn by dropping reference. The IB handshake might
 * still be in progress, but ref counting should take care of it.
 */
void
sdp_close(sdp_conn_t *conn)
{

	SDP_CONN_LOCK(conn);
	sdp_print_ctrl(conn, "sdp_close: ref: %d", conn->sdp_ib_refcnt);

	conn->sdp_ulpd = NULL;
	SDP_CONN_UNLOCK(conn);
	SDP_CONN_SOCK_PUT(conn); /* Free up sdp_conn_alloc() ref */
}

/*
 * sdp_disconnect: Begin closing of the socket connection, either a gracefully
 * or aborting the connection depending on the state.
 */
int
sdp_disconnect(sdp_conn_t *conn, int flags)
{
	int result = 0;

	if (conn == NULL)
		return (result);

	sdp_print_ctrl(conn, "sdp_disconnect: close on src port:%d",
	    ntohs(conn->src_port));

	SDP_CONN_LOCK(conn);
	if (conn->sdp_hashent == SDP_DEV_SK_INVALID) {
		sdp_print_dbg(conn, "sdp_disconnect: invalid sdp_hashent:%d",
		    conn->sdp_hashent);
		SDP_CONN_UNLOCK(conn);
		return (result);
	}

	conn->closeflags = (uint8_t)flags;
	conn->shutdown = SDP_SHUTDOWN_MASK;
	result = sdp_inet_release(conn);
	if (result < 0) {
		result -= result;
		goto done;
	}

	ASSERT(mutex_owned(&conn->conn_lock));

	if ((conn->inet_ops.linger) && (conn->inet_ops.lingertime > 0)) {
		if (!(conn->state & SDP_ST_MASK_CLOSED)) {
			result = sdp_do_linger(conn);
		}
	}
done:
	sdp_print_dbg(conn, "sdp_disconnect: Done processing:  ref:%d",
	    conn->sdp_ib_refcnt);
	SDP_CONN_UNLOCK(conn);
	return (result);
}

/*
 * sdp_bind: Process bind call.
 */
int
sdp_bind(sdp_conn_t *conn, struct sockaddr *sa, socklen_t len)
{
	int result = 0;
	sin_t *uaddr = 0;
	sin6_t *uaddr6 = 0;
	sin_t sin;
	zoneid_t zoneid;
	uint_t origipversion;
	netstack_t *stack;
	ip_stack_t *ipst;

	if ((result = proto_verify_ip_addr(conn->sdp_family == AF_INET6 ?
	    AF_INET6 : AF_INET_SDP, sa, len)) != 0) {
		return (result);
	}

	SDP_CONN_LOCK(conn);

	origipversion = conn->sdp_ipversion;
	switch (sa->sa_family) {
	case AF_INET:	/* ipv4 */
	case AF_INET_SDP:
		if (len < sizeof (struct sockaddr_in) ||
		    conn->sdp_family == AF_INET6) {
			result = EINVAL;
			goto done;
		}
		if (!OK_32PTR((char *)sa)) {
			if (conn->inet_ops.debug) {
				(void) strlog(SDP_MODULE_ID, 0, 1,
				    SL_ERROR|SL_TRACE,
				    "sdp_bind: bad IPv4 address parameter");
			}
			result = EINVAL;
			goto done;
		}
		conn->sdp_ipversion = IPV4_VERSION;
		/* LINTED */
		uaddr = (struct sockaddr_in *)sa;
		break;

	case AF_INET6:	/* ipv6 */
		if (len < sizeof (struct sockaddr_in6) ||
		    conn->sdp_family == AF_INET) {
			result = EINVAL;
			goto done;
		}
		if (!OK_32PTR((char *)sa)) {
			if (conn->inet_ops.debug) {
				(void) strlog(SDP_MODULE_ID, 0, 1,
				    SL_ERROR|SL_TRACE,
				    "sdp_bind: bad IPv6 address parameter");
			}
			result = EINVAL;
			goto done;
		}
		/* LINTED */
		uaddr6 = (struct sockaddr_in6 *)sa;
		conn->sdp_ipversion =
		    IN6_IS_ADDR_V4MAPPED(&uaddr6->sin6_addr) ?
		    IPV4_VERSION : IPV6_VERSION;
		if (conn->sdp_ipversion == IPV4_VERSION) {
			sin.sin_port =  uaddr6->sin6_port;
			sin.sin_addr.s_addr = V4_PART_OF_V6((
			    uaddr6->sin6_addr));
			uaddr = (struct sockaddr_in *)&sin;
		}
		break;

	default:
		sdp_print_warn(conn, "sdp_bind: Unknown <%d> addr family",
		    sa->sa_family);
		if (conn->inet_ops.debug) {
			(void) strlog(SDP_MODULE_ID, 0, 1,
			    SL_TRACE | SL_ERROR,
			    "sdp_bind: Unknown <%d> addr family for conn"
			    " <%d> \n", sa->sa_family, conn->sdp_hashent);
		}
		result = EAFNOSUPPORT;
		goto done;
	}

	/*
	 * If ipv6_v6only is set, this must be a AF_INET6 SDP socket
	 */
	if (conn->sdp_ipversion == IPV4_VERSION && conn->inet_ops.ipv6_v6only) {
		result = EADDRNOTAVAIL;
		goto done;
	}

	if (origipversion != conn->sdp_ipversion)
		ASSERT(conn->sdp_family == AF_INET6);

	if (conn->state != SDP_CONN_ST_CLOSED || conn->src_port > 0) {
		result = EINVAL;
		goto done;
	}

	stack = conn->sdp_stack;
	ipst = stack->netstack_ip;
	ASSERT(ipst != NULL);

	/*
	 * Query into IP to validate the IP in the particular zone
	 *
	 * For exclusive stacks we set the zoneid to zero
	 * to make the operate as if in the global zone.
	 */
	zoneid = (stack->netstack_stackid != GLOBAL_NETSTACKID) ?
	    GLOBAL_ZONEID : conn->sdp_zoneid;
	if (conn->sdp_ipversion == IPV4_VERSION) {
		ASSERT(uaddr != NULL);
		dprint(10, ("sdp_bind: processing IPv4 bind: conn:%p",
		    (void *)conn));
		if (INADDR_ANY != uaddr->sin_addr.s_addr) {
			/*
			 * make sure we have a valid binding address
			 */
			dprint(10, ("Inside SDP_bind %d addr:%x", zoneid,
			    uaddr->sin_addr.s_addr));

			if (ip_laddr_verify_v4(uaddr->sin_addr.s_addr, zoneid,
			    ipst, B_FALSE) == IPVL_BAD) {
				result = EADDRNOTAVAIL;
				goto done;
			}
		}
		result = sdp_inet_bind4(conn, (sin_t *)uaddr, len);
	} else {
		ASSERT(uaddr6 != NULL);
		dprint(10, ("sdp_bind: processing IPv6 bind: conn:%p",
		    (void *)conn));
		if (!IN6_IS_ADDR_UNSPECIFIED(&uaddr6->sin6_addr)) {
			/*
			 * Note If we are to handle link-locals we need to
			 * get the scopeid/ifindex from the caller. We use zero.
			 */
			if (ip_laddr_verify_v6(&uaddr6->sin6_addr, zoneid,
			    ipst, B_FALSE, 0) == IPVL_BAD) {
				result = EADDRNOTAVAIL;
				goto done;
			}
		}
		result = sdp_inet_bind6(conn, (sin6_t *)uaddr6, len);
	}

	if (result != 0) {
		sdp_print_warn(conn, "sdp_bind: Failed <%d>", result);
		if (conn->inet_ops.debug) {
			(void) strlog(SDP_MODULE_ID, 0, 1,
			    SL_TRACE | SL_ERROR,
			    "sdp_bind: Bind for conn <%d> failed\n",
			    conn->sdp_hashent);
		}
	}
done:
	SDP_CONN_UNLOCK(conn);
	return (result);
}


/*
 * sdp_listen: Process listen call.
 */
int
sdp_listen(sdp_conn_t *conn, int32_t backlog)
{
	int result = 0;

	SDP_CONN_LOCK(conn);


	sdp_print_ctrl(conn, "sdp_listen: backlog:%d", backlog);

	if (conn->state != SDP_CONN_ST_CLOSED &&
	    conn->state != SDP_CONN_ST_LISTEN) {
		sdp_print_warn(conn, "sdp_listen: conn state not valid <%p>",
		    (void *)conn);
		result = EINVAL;
		goto done;
	}

	/* Do an anonymous bind for unbound socket doing listen(). */
	if (ntohs(conn->src_port) == 0) {
		result = sdp_inet_port_get(conn, sdp_binding_v4, 0);
		if (result != 0) {
			sdp_print_warn(conn, "sdp_listen: not valid port <%d>",
			    result);
			goto done;
		}
	}

	result = sdp_inet_listen(conn, backlog);
done:
	SDP_CONN_UNLOCK(conn);
	return (result);
}


/*
 * sdp_connect: Process connect call
 */
int
sdp_connect(sdp_conn_t *conn, const struct sockaddr *dst, uint32_t addrlen)
{
	sin_t *sin;
	sin6_t *sin6;
	int result;

	if ((result = proto_verify_ip_addr(conn->sdp_family == AF_INET6 ?
	    AF_INET6 : AF_INET_SDP, dst, addrlen)) != 0) {
		return (result);
	}

	SDP_CONN_LOCK(conn);

	if (conn->state != SDP_CONN_ST_CLOSED) {
		result = EINVAL;
		goto done;
	}

	/*
	 * Extract the destination's info.
	 */
	sdp_print_ctrl(conn, "sdp_connect: family: <%d>", dst->sa_family);
try_again:
	switch (dst->sa_family) {
	case AF_INET:
	case AF_INET_SDP:
		/* LINTED */
		sin = (sin_t *)dst;
		if (IN_MULTICAST(ntohl(sin->sin_addr.s_addr))) {
			sdp_print_warn(conn, "%s",
			    "sdp_connect: Multicast Address not supported");
			SDP_CONN_UNLOCK(conn);
			return (ECONNREFUSED);
		}

		if (conn->inet_ops.ipv6_v6only) {
			sdp_print_warn(conn, "sdp_connect: v4 address not "
			    "supported on v6-only connection: %x",
			    sin->sin_addr.s_addr);
			SDP_CONN_UNLOCK(conn);
			return (EAFNOSUPPORT);
		}

		if (sin->sin_addr.s_addr == INADDR_ANY) {
			/*
			 * SunOS 4.x and 4.3 BSD allow an application
			 * to connect a TCP socket to INADDR_ANY.
			 * see tcp_connect_ipv[4,6] for detailed comments
			 */
			sin->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		}

		if (!conn->src_port) {
			result = sdp_inet_port_get(conn, sdp_binding_v4, 0);
			if (result != 0) {
				sdp_print_warn(conn, "sdp_connect:Error <%d> "
				    "getting port", result);
				goto done;
			}
		}
		/*
		 * No need to change the family type, sdp_inet_connect4 assumes
		 * it is type AF_INET.
		 */
		/* LINTED */
		result = sdp_inet_connect4(conn, (sin_t *)dst, addrlen);
		if (result != 0) {
			conn->conn_remv6 = ipv6_all_zeros;
			conn->dst_port = 0;
		}
		break;

	case AF_INET6:
		/* LINTED */
		sin6 = (sin6_t *)dst;
		/* Check for attempt to connect to non-unicast. */
		if (addrlen < sizeof (sin6_t)) {
			SDP_CONN_UNLOCK(conn);
			return (ECONNREFUSED);
		}

		if (conn->sdp_family == AF_INET6 &&
		    IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr)) {
			sin_t uaddr;

			uaddr.sin_port =  sin6->sin6_port;
			uaddr.sin_addr.s_addr = V4_PART_OF_V6((
			    sin6->sin6_addr));
			if (IN_MULTICAST(ntohl(uaddr.sin_addr.s_addr))) {
				sdp_print_warn(conn, "%s",
				    "sdp_connect: "
				    "Multicast Address not supported");
				SDP_CONN_UNLOCK(conn);
				return (ECONNREFUSED);
			}

			if (conn->inet_ops.ipv6_v6only) {
				sdp_print_warn(conn, "sdp_connect: v4 address "
				    "not supported on v6-only connection: %x",
				    sin->sin_addr.s_addr);
				SDP_CONN_UNLOCK(conn);
				return (EADDRNOTAVAIL);
			}

			if (uaddr.sin_addr.s_addr == INADDR_ANY) {
				/*
				 * SunOS 4.x and 4.3 BSD allow an application
				 * to connect a TCP socket to INADDR_ANY.
				 * see tcp_connect_ipv[4,6] for detailed
				 * comments
				 */
				uaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			}

			conn->sdp_ipversion = IPV4_VERSION;

			/* Bind to IPv4 address */
			if (!conn->src_port) {
				result = sdp_inet_port_get(conn,
				    sdp_binding_v4, 0);
				if (result != 0) {
					sdp_print_warn(conn, "sdp_connect: "
					    "error <%d> getting port", result);
					goto done;
				}
			}

			result = sdp_inet_connect4(conn, (sin_t *)&uaddr,
			    addrlen);
			if (result != 0) {
				conn->conn_remv6 = ipv6_all_zeros;
				conn->dst_port = 0;
			}
		} else {
			if (IN6_IS_ADDR_MULTICAST(&sin6->sin6_addr)) {
				sdp_print_warn(conn, "%s",
				    "sdp_connect: "
				    "Multicast Address not supported");
				SDP_CONN_UNLOCK(conn);
				return (ECONNREFUSED);
			}

			if (IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr)) {
				/*
				 * Interpret a zero destination to mean
				 * loopback.
				 */
				sin6->sin6_addr = ipv6_loopback;
			}
			if (!conn->src_port) {
				result = sdp_inet_port_get(conn,
				    sdp_binding_v6, 0);
				if (result != 0) {
					sdp_print_warn(conn, "sdp_connect: "
					    "error <%d> getting port", result);
					goto done;
				}
			}

			conn->sdp_ipversion = IPV6_VERSION;

			/* LINTED */
			result = sdp_inet_connect6(conn, (sin6_t *)dst,
			    addrlen);
			if (result != 0) {
				conn->conn_remv6 = ipv6_all_zeros;
				conn->dst_port = 0;
			}
		}
		break;

	default:
		result = EAFNOSUPPORT;
		goto done;
	}

	/*
	 * send error if connection attempt has failed.
	 */
	if ((result != 0) || (conn->error != 0)) {
		sdp_print_warn(conn, "sdp_connect: Connect failed err <%d>",
		    result);
		conn->state =  SDP_CONN_ST_CLOSED;
	}

done:
	SDP_CONN_UNLOCK(conn);
	return (result);
}


/*
 * called at interrupt level for passive connect.
 */
int
sdp_pass_establish(sdp_conn_t *eager_conn)
{
	sdp_conn_t *listen_conn;
	int error = 0;
	sdp_stack_t *sdps = eager_conn->sdp_sdps;

	ASSERT(MUTEX_HELD(&eager_conn->conn_lock));

	/*
	 * accept_list should hold one sock_refcnt
	 */
	ASSERT(eager_conn->sdp_inet_refcnt == 1);
	/*
	 * There should be minimum of 1 refcnts, one for sdp_conn_allocate
	 */
	ASSERT(eager_conn->sdp_ib_refcnt >= 1);

	/*
	 * copy the conn ptr to the options field so we
	 * can retrieve it in T_CONN_RES
	 */
	if ((eager_conn->sdp_family == AF_INET) ||
	    (eager_conn->sdp_family == AF_INET6)) {
		listen_conn = eager_conn->parent;
		ASSERT(listen_conn != NULL);
		/*
		 * Intention is to remove the eager from listen connection's
		 * accept queue.
		 */
		if (sdp_inet_accept_queue_remove(eager_conn, B_TRUE)) {
			error = ECONNRESET;
			goto on_error;
		}
		eager_conn->sdp_ulpd =
		    listen_conn->sdp_ulp_newconn(listen_conn->sdp_ulpd,
		    eager_conn);
		SDP_CONN_UNLOCK(listen_conn);
		SDP_CONN_PUT(listen_conn);
		if (eager_conn->sdp_ulpd == NULL) {
			error = ECONNRESET;
			goto on_error;
		}
		SDP_CONN_SOCK_HOLD(eager_conn); /* Add reference for socket */
	}

	sdp_send_flush(eager_conn);

	/*
	 * Now that connection is about to succeed, post buffers
	 */
	sdp_conn_set_buff_limits(eager_conn);
	error = sdp_recv_post(eager_conn);
	if (error == 0) {
		SDP_STAT_INC(sdps, CurrEstab);
	}

on_error:
	return (error);
}


/*
 * sdp_shutdown: Process shutdown call.
 */
int
sdp_shutdown(sdp_conn_t *conn, int flag)
{
	int result = 0;

	SDP_CONN_LOCK(conn);

	sdp_print_ctrl(conn, "sdp_shutdown: refcnt:%d", conn->sdp_ib_refcnt);

	result = sdp_inet_shutdown(flag, conn);
	if (result < 0) {
		sdp_print_warn(conn, "sdp_shutdown: Failed <%d>", result);
		SDP_CONN_UNLOCK(conn);
		return (result);
	}
	SDP_CONN_UNLOCK(conn);
	return (result);
}


/*
 * sdp_wput_ioctl handles all M_IOCTL messages.
 */
void
sdp_wput_ioctl(queue_t *q, mblk_t *mp)
{
	sdp_stack_t *sdps = (sdp_stack_t *)q->q_ptr;
	struct iocblk *iocp;

	/* LINTED */
	iocp = (struct iocblk *)mp->b_rptr;
	switch (iocp->ioc_cmd) {
	case TI_GETMYNAME:
		mi_copyin(q, mp, NULL, SIZEOF_STRUCT(strbuf, iocp->ioc_flag));
		return;
	case ND_SET:
		/* nd_getset does the necessary checks */
		/*FALLTHRU*/
	case ND_GET:
		if (!nd_getset(q, sdps->sdps_nd, mp))
			miocnak(q, mp, 0, ENOTSUP);
		else
			qreply(q, mp);
		return;
	case SDP_IOC_HCA:
		sdp_ioc_hca(q, mp);
		return;
	default:
		miocnak(q, mp, 0, ENOTSUP);
		return;
	}
}

static void
sdp_ioc_hca(queue_t *q, mblk_t *mp)
{
	sdp_stack_t *sdps = (sdp_stack_t *)q->q_ptr;
	struct iocblk *iop;
	sdp_ioc_hca_t *sih;
	int rc;
	cred_t *cr;

	/* LINTED: alignment */
	iop = (struct iocblk *)mp->b_rptr;
	if ((cr = msg_getcred(mp, NULL)) == NULL)
		cr = iop->ioc_cr;

	if (cr != NULL && secpolicy_net_config(cr, B_FALSE) != 0) {
		miocnak(q, mp, 0, EPERM);
		return;
	}

	if (sdps->sdps_netstack->netstack_stackid != GLOBAL_NETSTACKID) {
		miocnak(q, mp, 0, EPERM);
		return;
	}

	/* LINTED: alignment */
	sih = (sdp_ioc_hca_t *)mp->b_cont->b_rptr;
	if (sih->sih_offline)
		rc = sdp_ioc_hca_offline(sih->sih_guid, sih->sih_force,
		    sih->sih_query);
	else
		rc = sdp_ioc_hca_online(sih->sih_guid);
	if (rc == 0)
		miocack(q, mp, 0, 0);
	else
		miocnak(q, mp, 0, rc);
}

/*
 * sdp_ioctl is called by to handle all messages.
 */
/*ARGSUSED*/
int
sdp_ioctl(sdp_conn_t *conn, int cmd, int32_t *value, struct cred *cr)
{
	int result = EINVAL;

	switch (cmd) {
	case SIOCATMARK:
		SDP_CONN_LOCK(conn);
		*value = 0;
		result = 0;
		if (conn->sdp_recv_oob_state & SDP_RECV_OOB_ATMARK)
			*value = 1;
		SDP_CONN_UNLOCK(conn);
		break;
	default:
		result = EINVAL;
		break;
	}
	return (result);
}


/*
 * sdp_get_opt:
 */
int
sdp_get_opt(sdp_conn_t *conn, int level, int name, void *ptr,
    socklen_t *optlen)
{
	int	*i1 = (int *)ptr;
	int	retval = 0;

	dprint(SDP_DBG, ("sdp_get_opt: level:%d name:%d",
	    level, name));

	SDP_CONN_LOCK(conn);
	switch (level) {
	case SOL_SOCKET:
		switch (name) {
		case SO_LINGER:	{
			struct linger *lgr = (struct linger *)ptr;

			lgr->l_onoff = conn->inet_ops.linger ? SO_LINGER : 0;
			lgr->l_linger = conn->inet_ops.lingertime;
			}
			*optlen = sizeof (struct linger);
			break;
		case SO_DEBUG:
			*i1 = conn->inet_ops.debug ? SO_DEBUG : 0;
			break;
		case SO_DONTROUTE:
			*i1 = conn->inet_ops.localroute ? SO_DONTROUTE : 0;
			break;
		case SO_BROADCAST:
			*i1 = conn->inet_ops.broadcast ? SO_BROADCAST : 0;
			break;
		case SO_REUSEADDR:
			*i1 = conn->inet_ops.reuse ? SO_REUSEADDR : 0;
			break;
		case SO_OOBINLINE:
			*i1 = conn->inet_ops.urginline ? SO_OOBINLINE : 0;
			break;
		case SO_TYPE:
			*i1 = SOCK_STREAM;
			break;
		case SO_SNDBUF:
			*i1 = conn->sdpc_tx_max_queue_size;
			break;
		case SO_RCVBUF:
			*i1 = conn->sdpc_max_rwin;
			break;
		case SO_KEEPALIVE:
			*i1 = conn->inet_ops.ka_enabled ? SO_KEEPALIVE : 0;
			break;
		case SO_USELOOPBACK:
			*i1 = conn->inet_ops.useloopback ? SO_USELOOPBACK : 0;
			break;
		case SO_DGRAM_ERRIND:
			*i1 = conn->inet_ops.dgram_errind ?
			    SO_DGRAM_ERRIND : 0;
			break;
		case SO_SND_COPYAVOID:
			*i1 = conn->inet_ops.snd_zcopy_aware ?
			    SO_SND_COPYAVOID : 0;
			break;
		default:
			sdp_print_warn(conn, "sdp_opt_set: SOL_SOCKET "
			    "unsupported opt <%x>\n", name);
			SDP_CONN_UNLOCK(conn);
			return (ENOPROTOOPT);
		}
		break;

	case IPPROTO_IP:
		switch (name) {
		case IP_TOS:
			*i1 = conn->inet_ops.ip_tos;
			break;
		default:
			retval = EINVAL;
			break;
		}
		break;

	case IPPROTO_IPV6:
		switch (name) {
		case IPV6_V6ONLY:
			*i1 = conn->inet_ops.ipv6_v6only;
			break;
		default:
			retval = EINVAL;
			break;
		}
		break;

	case IPPROTO_TCP:
		switch (name) {
		case TCP_NODELAY:
			*i1 = (int)conn->inet_ops.tcp_nodelay;
			break;
		case TCP_MAXSEG:
			*i1 = conn->sdpc_send_buff_size != 0 ?
			    conn->sdpc_send_buff_size :
			    conn->sdpc_local_buff_size;
			break;
		case TCP_NOTIFY_THRESHOLD:
			*i1 = (int)conn->inet_ops.first_timer_threshold;
			break;
		case TCP_ABORT_THRESHOLD:
			*i1 = conn->inet_ops.second_timer_threshold;
			break;
		case TCP_CONN_NOTIFY_THRESHOLD:
			*i1 = conn->inet_ops.first_ctimer_threshold;
			break;
		case TCP_CONN_ABORT_THRESHOLD:
			*i1 = conn->inet_ops.second_ctimer_threshold;
			break;
		case TCP_RECVDSTADDR:
			*i1 = conn->inet_ops.recvdstaddr;
			break;
		case TCP_ANONPRIVBIND:
			*i1 = conn->inet_ops.anon_priv_bind;
			break;
		case TCP_EXCLBIND:
			*i1 = conn->inet_ops.exclbind ? TCP_EXCLBIND : 0;
			break;
		default:
			retval = EINVAL;
			break;
		}
		break;

	case PROTO_SDP:
		switch (name) {
		case SDP_NODELAY:
			*i1 = (conn->nodelay == 1) ? SDP_NODELAY : 0;
			break;
		default:
			retval = EINVAL;
			break;
		}
		break;

	default:
		dprint(SDP_WARN, ("sdp_opt_get: SOL_SOCKET"
		    " unsupported level <%x> opt=<%x>\n", level, name));
		retval = ENOPROTOOPT;
		break;

	}
	SDP_CONN_UNLOCK(conn);
	return (retval);
}

/*
 * sdp_polldata: The read/send etc wakes up client doing a wait. Then
 * VOP_POLL() ends up calling this routine which sends relevant data for send
 * and recv streams.
 */
int
sdp_polldata(sdp_conn_t *conn, int flag)
{

	sdp_print_dbg(conn, "sdp_polldata: buff:<%d>",
	    buff_pool_size(&conn->recv_pool));

	switch (flag) {
		default:
			ASSERT(0);
			break;
		case SDP_READ:
			return (atomic_add_32_nv(
			    (uint32_t *)&conn->sdp_recv_byte_strm, 0));
		case SDP_XMIT:
			return (sdp_inet_writable(conn));
		case SDP_OOB:
			return (conn->sdp_recv_oob_state &
			    (SDP_RECV_OOB_PEND | SDP_RECV_OOB_PRESENT |
			    SDP_RECV_OOB_CONSUMED));
	}
	return (0);
}

int
sdp_set_opt(sdp_conn_t *conn, int level, int name, const void *invalp,
    socklen_t inlen)
{
	int *i1 = (int *)invalp;
	boolean_t onoff = (*i1 == 0) ? 0 : 1;
	int retval = 0;

	dprint(SDP_DBG, ("sdp_opt_set: level:%d name:%d",
	    level, name));

	SDP_CONN_LOCK(conn);

	/*
	 * For fixed length options, no sanity check
	 * of passed in length is done. It is assumed *_optcom_req()
	 * routines do the right thing.
	 */
	switch (level) {
	case SOL_SOCKET:
		if (inlen < sizeof (int32_t)) {
			retval = EINVAL;
			break;
		}
		switch (name) {
		case SO_LINGER: {
			struct linger *lgr = (struct linger *)invalp;

			if (inlen != sizeof (struct linger)) {
				retval = EINVAL;
				break;
			}
			if (lgr->l_onoff) {
				conn->inet_ops.linger = 1;
				conn->inet_ops.lingertime = lgr->l_linger;
			} else {
				conn->inet_ops.linger = 0;
				conn->inet_ops.lingertime = 0;
			}
			break;
		}
		case SO_DEBUG:
			conn->inet_ops.debug = onoff;
			break;
		case SO_REUSEADDR:
			conn->inet_ops.reuse = onoff;
			break;
		case SO_OOBINLINE:
			conn->inet_ops.urginline = onoff;
			break;
		case SO_SNDBUF:

			if (*i1 <= 0) {
				retval = EINVAL;
				break;
			}

			if (*i1 > (1<<30)) {
				retval = ENOBUFS;
				break;
			}

			/*
			 * Save the value for returning
			 */
			if (*i1 < SDP_XMIT_LOWATER)
				*i1 = SDP_XMIT_LOWATER;

			conn->sdpc_tx_max_queue_size = *i1;
			sdp_send_flush(conn);
			break;
		case SO_RCVBUF: {
			uint32_t buff_size, nbuffs;

			if (*i1 > conn->sdp_sdps->sdp_recv_hiwat_max) {
				retval = ENOBUFS;
				break;
			}

			if (*i1 <= 0) {
				retval = EINVAL;
				break;
			}

			buff_size = conn->sdpc_send_buff_size != 0 ?
			    conn->sdpc_send_buff_size :
			    conn->sdpc_local_buff_size;

			nbuffs = *i1/buff_size;

			/*
			 * The new number of buffers can not be
			 * less than the minimum number of buffers
			 * or the current number of posted buffers.
			 */
			if ((nbuffs < SDP_MIN_BUFF_COUNT) ||
			    (nbuffs < conn->l_recv_bf)) {
				/*
				 * Silently fail
				 */
				break;
			}
			conn->sdpc_max_rwin = *i1;
			conn->sdpc_max_rbuffs = nbuffs;
			sdp_set_sorcvbuf(conn);

			break;
		}
		case SO_DONTROUTE:
			conn->inet_ops.localroute = onoff;
			break;
		case SO_KEEPALIVE:
			conn->inet_ops.ka_enabled = onoff;
			break;
		case SO_USELOOPBACK:
			conn->inet_ops.useloopback = onoff;
			break;
		case SO_BROADCAST:
			conn->inet_ops.broadcast = onoff;
			break;
		case SO_DGRAM_ERRIND:
			conn->inet_ops.dgram_errind = onoff;
			break;
		case SO_SND_COPYAVOID:
			conn->inet_ops.snd_zcopy_aware = onoff;
			break;
		default:
			dprint(SDP_WARN, ("sdp_opt_set: unsupported: "
			    "level=<%x> opt=<%x>\n", level, name));
			retval = EINVAL;
			break;
		}
		break;
	case IPPROTO_IP:
		switch (name) {
		case IP_TOS:
			conn->inet_ops.ip_tos = *i1;
			break;
		default:
			retval = EINVAL;
			break;
		}
		break;
	case IPPROTO_IPV6:
		switch (name) {
		case IPV6_V6ONLY:
			if (conn->inet_ops.ipv6_v6only == onoff)
				break;

			if (conn->sdp_family != AF_INET6 ||
			    conn->state != SDP_CONN_ST_CLOSED) {
				retval = EINVAL;
				break;
			}
			conn->inet_ops.ipv6_v6only = onoff;
			break;
		default:
			retval = EINVAL;
			break;
		}
		break;
	case IPPROTO_TCP:
		switch (name) {
		case TCP_NODELAY:
			conn->inet_ops.tcp_nodelay = *i1;
			break;
		case TCP_NOTIFY_THRESHOLD:
			conn->inet_ops.first_timer_threshold = *i1;
			break;
		case TCP_ABORT_THRESHOLD:
			conn->inet_ops.second_timer_threshold = *i1;
			break;
		case TCP_CONN_NOTIFY_THRESHOLD:
			conn->inet_ops.first_ctimer_threshold = *i1;
			break;
		case TCP_CONN_ABORT_THRESHOLD:
			conn->inet_ops.second_ctimer_threshold = *i1;
			break;
		case TCP_RECVDSTADDR:
			conn->inet_ops.recvdstaddr = onoff;
			break;
		case TCP_ANONPRIVBIND:
			conn->inet_ops.anon_priv_bind = onoff;
			break;
		case TCP_EXCLBIND:
			conn->inet_ops.exclbind = onoff;
			break;
		default:
			retval = EINVAL;
			break;
		}
		break;
	case PROTO_SDP:
		switch (name) {
		case SDP_NODELAY:
			conn->nodelay = *i1 ? 1 : 0;
			if (conn->nodelay) {
				sdp_send_flush(conn);
			}
			break;
		default:
			retval = EINVAL;
			break;
		}
		break;
	default:
		dprint(SDP_WARN, ("sdp_opt_set: unsupported: "
		    "level=<%x> opt=<%x>\n", level, name));
		retval = EINVAL;
		break;

	}

	SDP_CONN_UNLOCK(conn);
	return (retval);
}

void
sdp_set_sorcvbuf(sdp_conn_t *conn)
{
	struct sock_proto_props sopp;

	sopp.sopp_flags = SOCKOPT_RCVHIWAT;
	sopp.sopp_rxhiwat = conn->sdpc_max_rwin;
	sopp.sopp_flags |= SOCKOPT_RCVLOWAT;
	sopp.sopp_rxlowat = SDP_RECV_LOWATER;
	(*conn->sdp_ulp_set_proto_props)(conn->sdp_ulpd, &sopp);
}
