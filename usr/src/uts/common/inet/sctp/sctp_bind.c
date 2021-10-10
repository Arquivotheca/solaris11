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

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/stream.h>
#include <sys/cmn_err.h>
#include <sys/kmem.h>
#define	_SUN_TPI_VERSION 2
#include <sys/tihdr.h>
#include <sys/stropts.h>
#include <sys/socket.h>
#include <sys/random.h>
#include <sys/policy.h>
#include <sys/tsol/tndb.h>
#include <sys/tsol/tnet.h>

#include <netinet/in.h>
#include <netinet/ip6.h>

#include <inet/common.h>
#include <inet/ip.h>
#include <inet/ip6.h>
#include <inet/ipclassifier.h>
#include "sctp_impl.h"
#include "sctp_asconf.h"
#include "sctp_addr.h"

/*
 * Minimum number of associations which can be created per listener.  Used
 * when the listener association count is in effect.
 */
static uint32_t sctp_min_assoc_listener = 2;

static int sctp_bindi(sctp_t *, in_port_t, boolean_t, cred_t *);
static int sctp_bind_chk_port(sctp_t *, const struct sockaddr *, in_port_t *,
    in_port_t *, cred_t *);
static int sctp_select_port(sctp_t *, in_port_t *, cred_t *);

/*
 * Returns 0 on success, EACCES on permission failure.
 */
static int
sctp_select_port(sctp_t *sctp, in_port_t *requested_port, cred_t *cr)
{
	sctp_stack_t	*sctps = sctp->sctp_sctps;

	/*
	 * Get a valid port (within the anonymous range and should not
	 * be a privileged one) to use if the user has not given a port.
	 * If multiple threads are here, they may all start with
	 * with the same initial port. But, it should be fine as long as
	 * sctp_bindi will ensure that no two threads will be assigned
	 * the same port.
	 */
	if (*requested_port == 0) {
		*requested_port = sctp_update_next_port(
		    sctps->sctps_next_port_to_try, crgetzone(cr), sctps);
		if (*requested_port == 0)
			return (EACCES);
	} else {
		int i;
		boolean_t priv = B_FALSE;

		/*
		 * If the requested_port is in the well-known privileged range,
		 * verify that the stream was opened by a privileged user.
		 * Note: No locks are held when inspecting sctp_g_*epriv_ports
		 * but instead the code relies on:
		 * - the fact that the address of the array and its size never
		 *   changes
		 * - the atomic assignment of the elements of the array
		 */
		if (*requested_port < sctps->sctps_smallest_nonpriv_port) {
			priv = B_TRUE;
		} else {
			for (i = 0; i < sctps->sctps_g_num_epriv_ports; i++) {
				if (*requested_port ==
				    sctps->sctps_g_epriv_ports[i]) {
					priv = B_TRUE;
					break;
				}
			}
		}
		if (priv) {
			/*
			 * sctp_bind() should take a cred_t argument so that
			 * we can use it here.
			 */
			if (secpolicy_net_privaddr(cr, *requested_port,
			    IPPROTO_SCTP) != 0) {
				dprint(1,
				    ("sctp_bind(x): no prive for port %d",
				    *requested_port));
				return (EACCES);
			}
		}
	}

	return (0);
}

/*
 * Given a port (network byte order), check if there is a sctp_t in
 * SCTPS_LISTEN state bound to that port.  Return B_TRUE if there is;
 * B_FALSE otherwise.
 */
static boolean_t
sctp_check_listener(sctp_stack_t *sctps, conn_t *connp, in_port_t lport)
{
	sctp_tf_t	*tbf;
	sctp_t		*lsctp;
	conn_t		*lconnp;

	tbf = &sctps->sctps_bind_fanout[SCTP_BIND_HASH(lport)];
	mutex_enter(&tbf->tf_lock);
	for (lsctp = tbf->tf_sctp; lsctp != NULL;
	    lsctp = lsctp->sctp_bind_hash) {
		lconnp = lsctp->sctp_connp;

		if (lport != lconnp->conn_lport ||
		    !IPCL_BIND_ZONE_MATCH(lconnp, connp)) {
			continue;
		}

		if (lsctp->sctp_state == SCTPS_LISTEN) {
			mutex_exit(&tbf->tf_lock);
			return (B_TRUE);
		}
	}
	mutex_exit(&tbf->tf_lock);

	return (B_FALSE);
}

int
sctp_listen(sctp_t *sctp, cred_t *cr)
{
	sctp_tf_t	*tf;
	sctp_stack_t	*sctps = sctp->sctp_sctps;
	conn_t		*connp = sctp->sctp_connp;

	ASSERT(cr != NULL);

	RUN_SCTP(sctp);
	/*
	 * SCTP handles listen() increasing the backlog, need to check
	 * if it should be handled here too
	 */
	if (sctp->sctp_state > SCTPS_BOUND ||
	    (sctp->sctp_connp->conn_state_flags & CONN_CLOSING)) {
		WAKE_SCTP(sctp);
		return (EINVAL);
	}

	/* Do an anonymous bind for unbound socket doing listen(). */
	if (sctp->sctp_nsaddrs == 0) {
		struct sockaddr_storage ss;
		int ret;

		bzero(&ss, sizeof (ss));
		ss.ss_family = connp->conn_family;

		WAKE_SCTP(sctp);
		if ((ret = sctp_bind(sctp, (struct sockaddr *)&ss,
		    sizeof (ss), cr)) != 0)
			return (ret);
		RUN_SCTP(sctp)
	}

	/*
	 * If SO_REUSEPORT is set when bind() is done, we need to make sure
	 * that at listen() time, there is only 1 listener on the port.
	 */
	if (connp->conn_reuseport_bind_done &&
	    sctp_check_listener(sctps, connp, connp->conn_lport)) {
		WAKE_SCTP(sctp);
		return (EOPNOTSUPP);
	}

	/* Cache things in the ixa without any refhold */
	ASSERT(!(connp->conn_ixa->ixa_free_flags & IXA_FREE_CRED));
	connp->conn_ixa->ixa_cred = cr;
	connp->conn_ixa->ixa_cpid = connp->conn_cpid;
	if (is_system_labeled())
		connp->conn_ixa->ixa_tsl = crgetlabel(cr);

	sctp->sctp_state = SCTPS_LISTEN;
	(void) random_get_pseudo_bytes(sctp->sctp_secret, SCTP_SECRET_LEN);
	sctp->sctp_last_secret_update = ddi_get_lbolt64();
	bzero(sctp->sctp_old_secret, SCTP_SECRET_LEN);

	/* Build a template header for sending INIT-ACK, ABORT, ... */
	if (sctp_build_hdrs(sctp, KM_NOSLEEP) != 0) {
		WAKE_SCTP(sctp);
		return (ENOBUFS);
	}

	/*
	 * If there is an association limit, allocate and initialize
	 * the counter struct.  Note that since listen can be called
	 * multiple times, the struct may have been allready allocated.
	 */
	if (!list_is_empty(&sctps->sctps_listener_conf) &&
	    sctp->sctp_listen_cnt == NULL) {
		sctp_listen_cnt_t *slc;
		uint32_t ratio;

		ratio = sctp_find_listener_conf(sctps,
		    ntohs(connp->conn_lport));
		if (ratio != 0) {
			slc = kmem_alloc(sizeof (sctp_listen_cnt_t), KM_SLEEP);
			/*
			 * Calculate the connection limit based on
			 * the configured ratio and physmem.  Since
			 * the send/receive buffer can be changed, the
			 * following is only an approximation.
			 */
			slc->slc_max = ptob(physmem) / ratio /
			    (connp->conn_rcvbuf + connp->conn_sndbuf);
			/* At least we should allow some associations! */
			if (slc->slc_max < sctp_min_assoc_listener)
				slc->slc_max = sctp_min_assoc_listener;
			slc->slc_cnt = 1;
			slc->slc_drop = 0;
			sctp->sctp_listen_cnt = slc;
		}
	}


	tf = &sctps->sctps_listen_fanout[SCTP_LISTEN_HASH(
	    ntohs(connp->conn_lport))];
	sctp_listen_hash_insert(tf, sctp);

	WAKE_SCTP(sctp);
	return (0);
}

/*
 * Bind the sctp_t to a sockaddr, which includes an address and other
 * information, such as port or flowinfo.
 */
int
sctp_bind(sctp_t *sctp, struct sockaddr *sa, socklen_t len, cred_t *cr)
{
	int	err = 0;
	conn_t	*connp = sctp->sctp_connp;

	ASSERT(cr != NULL);

	RUN_SCTP(sctp);

	if ((sctp->sctp_state >= SCTPS_BOUND) ||
	    (sctp->sctp_connp->conn_state_flags & CONN_CLOSING) ||
	    (sa == NULL || len == 0)) {
		/*
		 * Multiple binds not allowed for any SCTP socket
		 * Also binding with null address is not supported.
		 */
		err = EINVAL;
		goto done;
	}

	/* Size check. */
	switch (sa->sa_family) {
	case AF_INET:
		if (len < sizeof (struct sockaddr_in) ||
		    connp->conn_family == AF_INET6) {
			err = EINVAL;
			goto done;
		}
		break;
	case AF_INET6:
		if (len < sizeof (struct sockaddr_in6) ||
		    connp->conn_family == AF_INET) {
			err = EINVAL;
			goto done;
		}
		break;
	default:
		err = EAFNOSUPPORT;
		goto done;
	}
	err = sctp_bind_add(sctp, sa, 1, cr);

done:
	WAKE_SCTP(sctp);
	return (err);
}

static int
sctp_bind_chk_port(sctp_t *sctp, const struct sockaddr *addr,
    in_port_t *req_port, in_port_t *out_port, cred_t *cr)
{
	conn_t *connp = sctp->sctp_connp;
	const struct sockaddr_in *sin;
	const struct sockaddr_in6 *sin6;
	uint_t scope_id;

	switch (addr->sa_family) {
	case AF_INET:
		sin = (struct sockaddr_in *)addr;
		*out_port = ntohs(sin->sin_port);
		break;
	case AF_INET6:
		sin6 = (struct sockaddr_in6 *)addr;
		*out_port = ntohs(sin6->sin6_port);

		connp->conn_flowinfo = sin6->sin6_flowinfo &
		    ~IPV6_VERS_AND_FLOW_MASK;
		scope_id = sin6->sin6_scope_id;
		if (scope_id != 0 && IN6_IS_ADDR_LINKSCOPE(&sin6->sin6_addr)) {
			connp->conn_ixa->ixa_flags |= IXAF_SCOPEID_SET;
			connp->conn_ixa->ixa_scopeid = scope_id;
			connp->conn_incoming_ifindex = scope_id;
		} else {
			connp->conn_ixa->ixa_flags &= ~IXAF_SCOPEID_SET;
			connp->conn_incoming_ifindex = connp->conn_bound_if;
		}
		break;
	default:
		return (EAFNOSUPPORT);
	}

	*req_port = *out_port;
	return (sctp_select_port(sctp, out_port, cr));
}

/*
 * Perform bind/unbind operation of a list of addresses on a sctp_t
 */
int
sctp_bindx(sctp_t *sctp, const void *addrs, int addrcnt, int bindop, cred_t *cr)
{
	int ret = 0;

	/* Kernel caller should pass in correct info. */
	ASSERT(sctp != NULL);
	ASSERT(addrs != NULL);
	ASSERT(addrcnt > 0);
	ASSERT(cr != NULL);

	RUN_SCTP(sctp);

	switch (bindop) {
	case SCTP_BINDX_ADD_ADDR:
		ret = sctp_bind_add(sctp, addrs, addrcnt, cr);
		break;
	case SCTP_BINDX_REM_ADDR:
		ret = sctp_bind_del(sctp, addrs, addrcnt);
		break;
	default:
		ret = EINVAL;
		break;
	}

	WAKE_SCTP(sctp);
	return (ret);
}

/*
 * Add a list of addresses to a sctp_t.
 */
int
sctp_bind_add(sctp_t *sctp, const void *addrs, uint32_t addrcnt, cred_t *cr)
{
	int		err = 0;
	boolean_t	do_asconf = B_FALSE;
	sctp_stack_t	*sctps = sctp->sctp_sctps;
	conn_t		*connp = sctp->sctp_connp;

	if (sctp->sctp_state > SCTPS_ESTABLISHED ||
	    (sctp->sctp_connp->conn_state_flags & CONN_CLOSING)) {
		return (EINVAL);
	}

	if (sctp->sctp_state > SCTPS_LISTEN) {
		/*
		 * Let's do some checking here rather than undoing the
		 * add later (for these reasons).
		 */
		if (!sctps->sctps_addip_enabled ||
		    !sctp->sctp_understands_asconf ||
		    !sctp->sctp_understands_addip) {
			return (EINVAL);
		}
		do_asconf = B_TRUE;
	}

	/*
	 * On a clustered node, for an inaddr_any bind, we will pass the list
	 * of all the addresses in the global list, minus any address on the
	 * loopback interface, and expect the clustering susbsystem to give us
	 * the correct list for the 'port'. For explicit binds we give the
	 * list of addresses  and the clustering module validates it for the
	 * 'port'.
	 *
	 * On a non-clustered node, cl_sctp_check_addrs will be NULL and
	 * we proceed as usual.
	 */
	if (cl_sctp_check_addrs != NULL) {
		uchar_t		*addrlist = NULL;
		size_t		size = 0;
		int		unspec = 0;
		boolean_t	do_listen;
		uchar_t		*llist = NULL;
		size_t		lsize = 0;
		in_port_t	port;

		/* Need to find the requested port for clustering code. */
		if (sctp->sctp_state != SCTPS_BOUND) {
			switch (((struct sockaddr *)addrs)->sa_family) {
			case AF_INET:
				port = ntohs(
				    ((struct sockaddr_in *)addrs)->sin_port);
				break;
			case AF_INET6:
				port = ntohs(
				    ((struct sockaddr_in6 *)addrs)->sin6_port);
				break;
			default:
				return (EAFNOSUPPORT);
			}
		} else {
			port = connp->conn_lport;
		}

		/*
		 * If we are adding addresses after listening, but before
		 * an association is established, we need to update the
		 * clustering module with this info.
		 */
		do_listen = !do_asconf && sctp->sctp_state > SCTPS_BOUND &&
		    cl_sctp_listen != NULL;

		err = sctp_get_addrlist(sctp, addrs, &addrcnt, &addrlist,
		    &unspec, &size);
		if (err != 0) {
			ASSERT(addrlist == NULL);
			ASSERT(addrcnt == 0);
			ASSERT(size == 0);
			SCTP_KSTAT(sctps, sctp_cl_check_addrs);
			return (err);
		}
		ASSERT(addrlist != NULL);
		(*cl_sctp_check_addrs)(connp->conn_family, port, &addrlist,
		    size, &addrcnt, unspec == 1);
		if (addrcnt == 0) {
			/* We free the list */
			kmem_free(addrlist, size);
			return (EINVAL);
		}
		if (do_listen) {
			lsize = sizeof (in6_addr_t) * addrcnt;
			llist = kmem_alloc(lsize, KM_SLEEP);
		}
		err = sctp_valid_addr_list(sctp, addrlist, addrcnt, llist,
		    lsize);
		if (err == 0 && do_listen) {
			(*cl_sctp_listen)(connp->conn_family, llist,
			    addrcnt, connp->conn_lport);
			/* list will be freed by the clustering module */
		} else if (err != 0 && llist != NULL) {
			kmem_free(llist, lsize);
		}
		/* free the list we allocated */
		kmem_free(addrlist, size);
	} else {
		err = sctp_valid_addr_list(sctp, addrs, addrcnt, NULL, 0);
	}
	if (err != 0)
		return (err);

	/*
	 * End point not yet bound.  Check if the requested port (pick one if
	 * anonymous port is requested ) is available.  If not, remove all
	 * addresses added.
	 */
	if (sctp->sctp_state < SCTPS_BOUND) {
		in_port_t req_port, alloc_port;
		boolean_t bind_to_req_port_only;

		err = sctp_bind_chk_port(sctp, addrs, &req_port, &alloc_port,
		    cr);
		if (err != 0)
			goto err_free;

		/*
		 * Remember the uid before calling sctp_bindi().  We do this
		 * even if SO_REUSEPORT is not set.  This allows the app to
		 * set SO_REUSEPORT after bind() to allow port sharing.
		 */
		connp->conn_bind_uid = crgetuid(cr);

		bind_to_req_port_only = req_port == 0 ? B_FALSE : B_TRUE;
		err = sctp_bindi(sctp, alloc_port, bind_to_req_port_only, cr);
		if (err != 0)
			goto err_free;
		ASSERT(sctp->sctp_state == SCTPS_BOUND);
	}

	/* Need to send  ASCONF messages */
	if (do_asconf) {
		err = sctp_add_ip(sctp, addrs, addrcnt);
		if (err != 0) {
			sctp_del_saddr_list(sctp, addrs, addrcnt, B_FALSE);
			return (err);
		}
	}

	return (0);

err_free:
	sctp_free_saddrs(sctp);
	return (err);
}

/*
 * Remove one or more addresses bound to the sctp_t.
 */
int
sctp_bind_del(sctp_t *sctp, const void *addrs, uint32_t addrcnt)
{
	int		error = 0;
	boolean_t	do_asconf = B_FALSE;
	uchar_t		*ulist = NULL;
	size_t		usize = 0;
	sctp_stack_t	*sctps = sctp->sctp_sctps;
	conn_t		*connp = sctp->sctp_connp;

	if (sctp->sctp_state > SCTPS_ESTABLISHED ||
	    (sctp->sctp_connp->conn_state_flags & CONN_CLOSING)) {
		return (EINVAL);
	}
	/*
	 * Fail the remove if we are beyond listen, but can't send this
	 * to the peer.
	 */
	if (sctp->sctp_state > SCTPS_LISTEN) {
		if (!sctps->sctps_addip_enabled ||
		    !sctp->sctp_understands_asconf ||
		    !sctp->sctp_understands_addip) {
			return (EINVAL);
		}
		do_asconf = B_TRUE;
	}

	/* Can't delete the last address nor all of the addresses */
	if (sctp->sctp_nsaddrs == 1 || addrcnt >= sctp->sctp_nsaddrs) {
		return (EINVAL);
	}

	if (cl_sctp_unlisten != NULL && !do_asconf &&
	    sctp->sctp_state > SCTPS_BOUND) {
		usize = sizeof (in6_addr_t) * addrcnt;
		ulist = kmem_alloc(usize, KM_SLEEP);
	}

	error = sctp_del_ip(sctp, addrs, addrcnt, ulist, usize);
	if (error != 0) {
		if (ulist != NULL)
			kmem_free(ulist, usize);
		return (error);
	}
	/* ulist will be non-NULL only if cl_sctp_unlisten is non-NULL */
	if (ulist != NULL) {
		ASSERT(cl_sctp_unlisten != NULL);
		(*cl_sctp_unlisten)(connp->conn_family, ulist, addrcnt,
		    connp->conn_lport);
		/* ulist will be freed by the clustering module */
	}

	return (error);
}

/*
 * Given a port number, go through all sctp_ts bound to the same port to
 * check if there is an address conflict.  Returns 0 for success, errno
 * value otherwise.
 *
 * If bind_to_req_port_only is B_FALSE, and the given port number is
 * not available, go through the port space and find one available.
 *
 * If port is available, update the sctp_t to record the port number,
 * insert it in the bind hash table and change the state to SCTPS_BOUND.
 */
static int
sctp_bindi(sctp_t *sctp, in_port_t port, boolean_t bind_to_req_port_only,
    cred_t *cr)
{
	/* number of times we have run around the loop */
	int		count = 0;
	/* maximum number of times to run around the loop */
	int		loopmax;
	sctp_stack_t	*sctps = sctp->sctp_sctps;
	conn_t		*connp = sctp->sctp_connp;
	zone_t		*zone = crgetzone(cr);

	/*
	 * Lookup for free addresses is done in a loop and "loopmax"
	 * influences how long we spin in the loop
	 */
	if (bind_to_req_port_only) {
		/*
		 * If the requested port is busy, don't bother to look
		 * for a new one. Setting loop maximum count to 1 has
		 * that effect.
		 */
		loopmax = 1;
	} else {
		/*
		 * If the requested port is busy, look for a free one
		 * in the anonymous port range.
		 * Set loopmax appropriately so that one does not look
		 * forever in the case all of the anonymous ports are in use.
		 */
		loopmax = (sctps->sctps_largest_anon_port -
		    sctps->sctps_smallest_anon_port + 1);
	}

	/* Ensure that the sctp_t is not currently in the bind hash. */
	sctp_bind_hash_remove(sctp);
	do {
		uint16_t	lport;
		sctp_tf_t	*tbf;
		sctp_t		*lsctp;
		int		addrcmp;

		lport = htons(port);

		/*
		 * Hold the lock on the hash bucket to ensure that
		 * the duplicate check plus the insertion is an atomic
		 * operation.
		 *
		 * This function does an inline lookup on the bind hash list
		 * Make sure that we access only members of sctp_t
		 * and that we don't look at sctp_sctp, since we are not
		 * doing a SCTPB_REFHOLD. For more details please see the notes
		 * in sctp_compress()
		 */
		tbf = &sctps->sctps_bind_fanout[SCTP_BIND_HASH(port)];
		mutex_enter(&tbf->tf_lock);
		for (lsctp = tbf->tf_sctp; lsctp != NULL;
		    lsctp = lsctp->sctp_bind_hash) {
			conn_t *lconnp = lsctp->sctp_connp;

			if (lport != lconnp->conn_lport ||
			    lsctp->sctp_state < SCTPS_BOUND)
				continue;

			/*
			 * On a labeled system, we must treat bindings to ports
			 * on shared IP addresses by sockets with MAC exemption
			 * privilege as being in all zones, as there's
			 * otherwise no way to identify the right receiver.
			 */
			if (!IPCL_BIND_ZONE_MATCH(lconnp, connp))
				continue;

			/*
			 * If SO_REUSEPORT is set, we allow the binding
			 * endpoint to bind() to the same port as long as the
			 * already bound endpoint also has SO_REUSEPORT set
			 * and the bound endpoint's bind time uid is the same
			 * as the binding endpoint's bind time uid.
			 *
			 * Note that SO_EXCLBIND overrides this option.
			 */
			if (connp->conn_reuseport && !connp->conn_exclbind) {
				if (lconnp->conn_reuseport &&
				    lconnp->conn_bind_uid ==
				    connp->conn_bind_uid) {
					continue;
				}
			}

			addrcmp = sctp_compare_saddrs(sctp, lsctp);
			if (addrcmp != SCTP_ADDR_DISJOINT) {
				if (!connp->conn_reuseaddr) {
					/* in use */
					break;
				} else if (lsctp->sctp_state == SCTPS_BOUND ||
				    lsctp->sctp_state == SCTPS_LISTEN) {
					/*
					 * socket option SO_REUSEADDR is set
					 * on the binding sctp_t.
					 *
					 * We have found a match of IP source
					 * address and source port, which is
					 * refused regardless of the
					 * SO_REUSEADDR setting, so we break.
					 */
					break;
				}
			}
		}
		if (lsctp != NULL) {
			/* The port number is busy */
			mutex_exit(&tbf->tf_lock);
		} else {
			if (is_system_labeled()) {
				mlp_type_t mlptype;

				mlptype = tsol_mlp_port_type(zone, IPPROTO_SCTP,
				    port, mlptBoth);
				if (mlptype != mlptSingle) {
					if (secpolicy_net_bindmlp(cr) != 0) {
						mutex_exit(&tbf->tf_lock);
						return (EACCES);
					}
					/*
					 * If we're binding a shared MLP, then
					 * make sure that this zone is the one
					 * that owns that MLP.  Shared MLPs can
					 * be owned by at most one zone.
					 *
					 * No need to handle exclusive-stack
					 * zones since ALL_ZONES only applies
					 * to the shared stack.
					 */

					if ((mlptype == mlptShared ||
					    mlptype == mlptBoth) &&
					    connp->conn_zoneid !=
					    tsol_mlp_findzone(IPPROTO_SCTP,
					    lport)) {
						mutex_exit(&tbf->tf_lock);
						return (EACCES);
					}
					connp->conn_mlp_type = mlptype;
				}
			}
			/*
			 * This port is ours. Insert in fanout and mark as
			 * bound to prevent others from getting the port
			 * number.
			 */
			sctp->sctp_state = SCTPS_BOUND;
			connp->conn_lport = lport;

			ASSERT(&sctps->sctps_bind_fanout[
			    SCTP_BIND_HASH(port)] == tbf);
			sctp_bind_hash_insert(tbf, sctp, B_TRUE);

			mutex_exit(&tbf->tf_lock);

			/*
			 * Remember the fact that this successful bind() is
			 * done with SO_REUSEPORT set.    The app may unset
			 * SO_REUSEPORT after bind().  But we need to make sure
			 * that there is only one listener on the port.
			 */
			if (connp->conn_reuseport)
				connp->conn_reuseport_bind_done = B_TRUE;

			/*
			 * We don't want sctp_next_port_to_try to "inherit"
			 * a port number supplied by the user in a bind.
			 *
			 * This is the only place where sctp_next_port_to_try
			 * is updated. After the update, it may or may not
			 * be in the valid range.
			 */
			if (!bind_to_req_port_only)
				sctps->sctps_next_port_to_try = port + 1;
			return (0);
		}

		if (count == 0) {
			/*
			 * We may have to return an anonymous port. So
			 * get one to start with.
			 */
			port = sctp_update_next_port(
			    sctps->sctps_next_port_to_try,
			    zone, sctps);
		} else {
			port = sctp_update_next_port(port + 1, zone, sctps);
		}
		if (port == 0)
			break;

		/*
		 * Don't let this loop run forever in the case where
		 * all of the anonymous ports are in use.
		 */
	} while (++count < loopmax);

	return (bind_to_req_port_only ? EADDRINUSE : EADDRNOTAVAIL);
}

/*
 * Don't let port fall into the privileged range.
 * Since the extra privileged ports can be arbitrary we also
 * ensure that we exclude those from consideration.
 * sctp_g_epriv_ports is not sorted thus we loop over it until
 * there are no changes.
 *
 * Note: No locks are held when inspecting sctp_g_*epriv_ports
 * but instead the code relies on:
 * - the fact that the address of the array and its size never changes
 * - the atomic assignment of the elements of the array
 */
in_port_t
sctp_update_next_port(in_port_t port, zone_t *zone, sctp_stack_t *sctps)
{
	int i;
	boolean_t restart = B_FALSE;

retry:
	if (port < sctps->sctps_smallest_anon_port)
		port = sctps->sctps_smallest_anon_port;

	if (port > sctps->sctps_largest_anon_port) {
		if (restart)
			return (0);
		restart = B_TRUE;
		port = sctps->sctps_smallest_anon_port;
	}

	if (port < sctps->sctps_smallest_nonpriv_port)
		port = sctps->sctps_smallest_nonpriv_port;

	for (i = 0; i < sctps->sctps_g_num_epriv_ports; i++) {
		if (port == sctps->sctps_g_epriv_ports[i]) {
			port++;
			/*
			 * Make sure whether the port is in the
			 * valid range.
			 *
			 * XXX Note that if sctp_g_epriv_ports contains
			 * all the anonymous ports this will be an
			 * infinite loop.
			 */
			goto retry;
		}
	}

	if (is_system_labeled() &&
	    (i = tsol_next_port(zone, port, IPPROTO_SCTP, B_TRUE)) != 0) {
		port = i;
		goto retry;
	}

	return (port);
}
