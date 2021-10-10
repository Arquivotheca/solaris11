/*
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This file contains code imported from the OFED rds source file connection.c
 * Oracle elects to have and use the contents of connection.c under and governed
 * by the OpenIB.org BSD license (see below for full license text). However,
 * the following notice accompanied the original version of this file:
 */

/*
 * Copyright (c) 2006 Oracle.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */
#include <sys/types.h>
#include <sys/kmem.h>
#include <sys/rds.h>

#include <sys/ib/clients/rdsv3/rdsv3.h>
#include <sys/ib/clients/rdsv3/loop.h>
#include <sys/ib/clients/rdsv3/rdsv3_debug.h>

#define	rdsv3_conn_info_set(var, test, suffix) do {               \
	if (test)                                               \
		var |= RDS_INFO_CONNECTION_FLAG_##suffix;     \
} while (0)


/*
 * This is called by transports as they're bringing down a connection.
 * It clears partial message state so that the transport can start sending
 * and receiving over this connection again in the future.  It is up to
 * the transport to have serialized this call with its send and recv.
 */
void
rdsv3_conn_reset(struct rdsv3_connection *conn)
{
	RDSV3_DPRINTF2("rdsv3_conn_reset",
	    "connection %u.%u.%u.%u to %u.%u.%u.%u reset",
	    NIPQUAD(conn->c_laddr), NIPQUAD(conn->c_faddr));

	rdsv3_stats_inc(s_conn_reset);
	rdsv3_send_reset(conn);
	conn->c_flags = 0;

	/*
	 * Do not clear next_rx_seq here, else we cannot distinguish
	 * retransmitted packets from new packets, and will hand all
	 * of them to the application. That is not consistent with the
	 * reliability guarantees of RDS.
	 */
}

/*
 * There is only every one 'conn' for a given pair of addresses in the
 * system at a time.  They contain messages to be retransmitted and so
 * span the lifetime of the actual underlying transport connections.
 *
 * For now they are not garbage collected once they're created.  They
 * are torn down as the module is removed, if ever.
 */
static struct rdsv3_connection *
__rdsv3_conn_create(struct rdsv3_ip_bucket *bucketp, uint32_be_t faddr,
    int gfp, int is_outgoing)
{
	struct rdsv3_connection *conn, *parent = NULL;
	struct rdsv3_transport *trans = bucketp->trans;
	void *devp = NULL;
	avl_index_t pos;
	int ret;

	/*
	 * <localhost> <--> <localhost> OK
	 * <ipoib address> <--> <ipoib address> OK
	 * <ipoib address> <--> <localhost> NOT OK
	 * <localhost> <--> <ipoib address> NOT OK
	 */
	if (rdsv3_isloopback(bucketp->ip)) {
		if (!rdsv3_isloopback(faddr)) {
			RDSV3_DPRINTF2("__rdsv3_conn_create",
			    "No route, invalid combination, src: %u.%u.%u.%u "
			    "dest: %u.%u.%u.%u", NIPQUAD(bucketp->ip),
			    NIPQUAD(faddr));
			return (ERR_PTR(-ENOTSUP));
		}
	} else if (rdsv3_isloopback(faddr)) {
		RDSV3_DPRINTF2("__rdsv3_conn_create",
		    "No route, invalid combination, src: %u.%u.%u.%u "
		    "dest: %u.%u.%u.%u", NIPQUAD(bucketp->ip),
		    NIPQUAD(faddr));
		return (ERR_PTR(-ENOTSUP));
	}

	rw_enter(&bucketp->conn_lock, RW_READER);
	conn = avl_find(&bucketp->conn_tree, &faddr, &pos);
	if (conn &&
	    conn->c_loopback &&
	    conn->c_trans != &rdsv3_loop_transport &&
	    bucketp->ip == faddr &&
	    !is_outgoing) {
		/*
		 * This is a looped back IB connection, and we're
		 * called by the code handling the incoming connect.
		 * We need a second connection object into which we
		 * can stick the other QP.
		 */
		parent = conn;
		conn = parent->c_passive;
	}
	rw_exit(&bucketp->conn_lock);
	if (conn)
		return (conn);

	RDSV3_DPRINTF2("__rdsv3_conn_create",
	    "Enter(%u.%u.%u.%u -> %u.%u.%u.%u)",
	    NIPQUAD(bucketp->ip), NIPQUAD(faddr));

	/* make sure the source address is still valid */
	if ((trans->laddr_check) &&
	    (trans->laddr_check(bucketp->ip, &devp) != 0)) {
		RDSV3_DPRINTF2("__rdsv3_conn_create",
		    "Enter(%u.%u.%u.%u -> %u.%u.%u.%u)",
		    NIPQUAD(bucketp->ip), NIPQUAD(faddr));
		return (ERR_PTR(-EADDRNOTAVAIL));
	}

	conn = kmem_cache_alloc(bucketp->conn_slab, gfp);
	if (!conn) {
		return (ERR_PTR(-ENOMEM));
	}

	/*
	 * rdsv3_conn_constuctor initializes the locks and lists,
	 * the rest needs to be initialized here.
	 */
	conn->c_laddr = bucketp->ip;
	conn->c_faddr = faddr;
	conn->c_trans = trans;
	conn->c_loopback = (bucketp->ip == faddr) ? 1 : 0;
	conn->c_passive = NULL;
	conn->c_senders = 0;
	conn->c_xmit_rm = NULL;
	conn->c_xmit_rdma_sent = 0;
	conn->c_next_tx_seq = 1;
	conn->c_next_rx_seq = 1;
	conn->c_state = RDSV3_CONN_DOWN;
	conn->c_flags = 0;
	conn->c_reconnect_jiffies = 0;
	conn->c_last_connect_jiffies = 0;
	RDSV3_INIT_DELAYED_WORK(conn, &conn->c_send_w, devp, rdsv3_send_worker);
	RDSV3_INIT_DELAYED_WORK(conn, &conn->c_recv_w, devp, rdsv3_recv_worker);
	RDSV3_INIT_DELAYED_WORK(conn, &conn->c_conn_w, devp,
	    rdsv3_connect_worker);
	RDSV3_INIT_DELAYED_WORK(conn, &conn->c_down_w, devp,
	    rdsv3_shutdown_worker);
	conn->c_map_queued = 0;
	conn->c_map_offset = 0;
	conn->c_map_bytes = 0;
	conn->c_unacked_packets = rdsv3_sysctl_max_unacked_packets;
	conn->c_unacked_bytes = rdsv3_sysctl_max_unacked_bytes;
	conn->c_version = 0;
	conn->c_procspec = NULL;
	conn->c_bucketp = bucketp;

	ret = rdsv3_cong_get_maps(conn);
	if (ret) {
		kmem_cache_free(bucketp->conn_slab, conn);
		return (ERR_PTR(ret));
	}

	ret = conn->c_trans->conn_alloc(conn, gfp);
	if (ret) {
		kmem_cache_free(bucketp->conn_slab, conn);
		return (ERR_PTR(ret));
	}

	RDSV3_DPRINTF2("__rdsv3_conn_create",
	    "allocated conn %p for %u.%u.%u.%u -> %u.%u.%u.%u over %s %s",
	    conn, NIPQUAD(conn->c_laddr), NIPQUAD(faddr),
	    conn->c_trans->t_name ? conn->c_trans->t_name : "[unknown]",
	    is_outgoing ? "(outgoing)" : "");

	/*
	 * Since we ran without holding the conn lock, someone could
	 * have created the same conn (either normal or passive) in the
	 * interim. We check while holding the lock. If we won, we complete
	 * init and return our conn. If we lost, we rollback and return the
	 * other one.
	 */
	rw_enter(&bucketp->conn_lock, RW_WRITER);
	if (parent) {
		/* Creating passive conn */
		if (parent->c_passive) {
			conn->c_trans->conn_free(conn->c_transport_data);
			kmem_cache_free(bucketp->conn_slab, conn);
			conn = parent->c_passive;
		} else {
			conn->c_procspec = rdsv3_af_procspec();
			parent->c_passive = conn;
		}
	} else {
		/* Creating normal conn */
		struct rdsv3_connection *found;

		found = avl_find(&bucketp->conn_tree, &faddr, &pos);
		if (found) {
			conn->c_trans->conn_free(conn->c_transport_data);
			kmem_cache_free(bucketp->conn_slab, conn);
			conn = found;
		} else {
			conn->c_procspec = rdsv3_af_procspec();
			avl_insert(&bucketp->conn_tree, conn, pos);
		}
	}

	rw_exit(&bucketp->conn_lock);

	RDSV3_DPRINTF2("__rdsv3_conn_create", "Return(conn: %p)", conn);

	return (conn);
}

struct rdsv3_connection *
rdsv3_conn_create(struct rdsv3_ip_bucket *bucketp, uint32_be_t faddr,
    int gfp)
{
	return (__rdsv3_conn_create(bucketp, faddr, gfp, 0));
}

struct rdsv3_connection *
rdsv3_conn_create_outgoing(struct rdsv3_ip_bucket *bucketp, uint32_be_t faddr,
    int gfp)
{
	return (__rdsv3_conn_create(bucketp, faddr, gfp, 1));
}

void
rdsv3_conn_shutdown(struct rdsv3_connection *conn)
{
	RDSV3_DPRINTF2("rdsv3_conn_shutdown", "Enter(conn: %p)", conn);

	/* shut it down unless it's down already */
	if (!rdsv3_conn_transition(conn, RDSV3_CONN_DOWN, RDSV3_CONN_DOWN)) {
		/*
		 * Quiesce the connection mgmt handlers before we start tearing
		 * things down. We don't hold the mutex for the entire
		 * duration of the shutdown operation, else we may be
		 * deadlocking with the CM handler. Instead, the CM event
		 * handler is supposed to check for state DISCONNECTING
		 */
		mutex_enter(&conn->c_cm_lock);
		if (!rdsv3_conn_transition(conn, RDSV3_CONN_UP,
		    RDSV3_CONN_DISCONNECTING) &&
		    !rdsv3_conn_transition(conn, RDSV3_CONN_ERROR,
		    RDSV3_CONN_DISCONNECTING)) {
			RDSV3_DPRINTF2("rdsv3_conn_shutdown",
			    "shutdown called in state %d",
			    atomic_get(&conn->c_state));
			rdsv3_conn_drop(conn);
			mutex_exit(&conn->c_cm_lock);
			return;
		}
		mutex_exit(&conn->c_cm_lock);

		/* verify everybody's out of rds_send_xmit() */
		mutex_enter(&conn->c_send_lock);
		while (atomic_get(&conn->c_senders)) {
			mutex_exit(&conn->c_send_lock);
			delay(1);
			mutex_enter(&conn->c_send_lock);
		}

		conn->c_trans->conn_shutdown(conn);
		rdsv3_conn_reset(conn);
		mutex_exit(&conn->c_send_lock);

		if (!rdsv3_conn_transition(conn, RDSV3_CONN_DISCONNECTING,
		    RDSV3_CONN_DOWN)) {
			/*
			 * This can happen - eg when we're in the middle of
			 * tearing down the connection, and someone unloads
			 * the rds module.
			 * Quite reproduceable with loopback connections.
			 * Mostly harmless.
			 */
#ifndef __lock_lint
			RDSV3_DPRINTF2("rdsv3_conn_shutdown",
			    "failed to transition to state DOWN, "
			    "current statis is: %d",
			    atomic_get(&conn->c_state));
			rdsv3_conn_drop(conn);
#endif
			return;
		}
	}

	/*
	 * Then reconnect if it's still live.
	 * The passive side of an IB loopback connection is never added
	 * to the conn hash, so we never trigger a reconnect on this
	 * conn - the reconnect is always triggered by the active peer.
	 */
	RDSV3_CANCEL_DELAYED_MSG(conn, &conn->c_conn_w);

	if (avl_find(&conn->c_bucketp->conn_tree,
	    &conn->c_faddr, NULL) == conn)
		rdsv3_queue_reconnect(conn);

	RDSV3_DPRINTF2("rdsv3_conn_shutdown", "Exit");
}

/*
 * Stop and free a connection.
 */
void
rdsv3_conn_destroy(struct rdsv3_connection *conn)
{
	struct rdsv3_message *rm, *rtmp;
	list_t to_be_dropped;

	RDSV3_DPRINTF4("rdsv3_conn_destroy",
	    "freeing conn %p for %u.%u.%u.%u -> %u.%u.%u.%u",
	    conn, NIPQUAD(conn->c_laddr), NIPQUAD(conn->c_faddr));

	avl_remove(&conn->c_bucketp->conn_tree, conn);

	RDSV3_CANCEL_DELAYED_MSG(conn, &conn->c_send_w);
	RDSV3_CANCEL_DELAYED_MSG(conn, &conn->c_recv_w);

	rdsv3_conn_shutdown(conn);

	/* tear down queued messages */

	list_create(&to_be_dropped, sizeof (struct rdsv3_message),
	    offsetof(struct rdsv3_message, m_conn_item));

	RDSV3_FOR_EACH_LIST_NODE_SAFE(rm, rtmp, &conn->c_retrans, m_conn_item) {
		list_remove_node(&rm->m_conn_item);
		list_insert_tail(&to_be_dropped, rm);
	}

	RDSV3_FOR_EACH_LIST_NODE_SAFE(rm, rtmp, &conn->c_send_queue,
	    m_conn_item) {
		list_remove_node(&rm->m_conn_item);
		list_insert_tail(&to_be_dropped, rm);
	}

	RDSV3_FOR_EACH_LIST_NODE_SAFE(rm, rtmp, &to_be_dropped, m_conn_item) {
		clear_bit(RDSV3_MSG_ON_CONN, &rm->m_flags);
		list_remove_node(&rm->m_conn_item);
		rdsv3_message_put(rm);
	}

	if (conn->c_xmit_rm)
		rdsv3_message_put(conn->c_xmit_rm);

	conn->c_trans->conn_free(conn->c_transport_data);

	ASSERT(list_is_empty(&conn->c_retrans));
	kmem_cache_free(conn->c_bucketp->conn_slab, conn);
}

extern krwlock_t	rdsv3_bind_lock;
extern avl_tree_t	rdsv3_bind_tree;

/* ARGSUSED */
static void
rdsv3_conn_message_info(struct rsock *sock, unsigned int len,
    struct rdsv3_info_iterator *iter,
    struct rdsv3_info_lengths *lens,
    int want_send)
{
	struct list *list;
	struct rdsv3_connection *conn;
	struct rdsv3_message *rm;
	struct rdsv3_ip_bucket *bucketp;
	unsigned int total = 0;

	RDSV3_DPRINTF4("rdsv3_conn_message_info", "Enter");

	len /= sizeof (struct rds_info_message);

	rw_enter(&rdsv3_bind_lock, RW_READER);
	if (avl_is_empty(&rdsv3_bind_tree)) {
		/* no connections */
		rw_exit(&rdsv3_bind_lock);
		return;
	}

	bucketp = (struct rdsv3_ip_bucket *)avl_first(&rdsv3_bind_tree);
	do {
		rw_enter(&bucketp->conn_lock, RW_READER);
		if (avl_is_empty(&bucketp->conn_tree)) {
			/* no connections */
			rw_exit(&bucketp->conn_lock);
			continue;
		}

		conn = (struct rdsv3_connection *)avl_first(
		    &bucketp->conn_tree);
		do {
			if (want_send)
				list = &conn->c_send_queue;
			else
				list = &conn->c_retrans;

			mutex_enter(&conn->c_lock);

			/* XXX too lazy to maintain counts.. */
			RDSV3_FOR_EACH_LIST_NODE(rm, list, m_conn_item) {
				total++;
				if (total <= len)
					rdsv3_hdr_info_copy(&rm->m_hdr, iter,
					    conn->c_laddr, conn->c_faddr, 0);
			}

			mutex_exit(&conn->c_lock);

			conn = AVL_NEXT(&bucketp->conn_tree, conn);
		} while (conn != NULL);
		rw_exit(&bucketp->conn_lock);

		bucketp = AVL_NEXT(&rdsv3_bind_tree, bucketp);
	} while (bucketp != NULL);

	rw_exit(&rdsv3_bind_lock);

	lens->nr = total;
	lens->each = sizeof (struct rds_info_message);

	RDSV3_DPRINTF4("rdsv3_conn_message_info", "Return");
}

static void
rdsv3_conn_message_info_send(struct rsock *sock, unsigned int len,
    struct rdsv3_info_iterator *iter,
    struct rdsv3_info_lengths *lens)
{
	rdsv3_conn_message_info(sock, len, iter, lens, 1);
}

static void
rdsv3_conn_message_info_retrans(struct rsock *sock,
    unsigned int len,
    struct rdsv3_info_iterator *iter,
    struct rdsv3_info_lengths *lens)
{
	rdsv3_conn_message_info(sock, len, iter, lens, 0);
}

/* ARGSUSED */
void
rdsv3_for_each_conn_info(struct rsock *sock, unsigned int len,
    struct rdsv3_info_iterator *iter,
    struct rdsv3_info_lengths *lens,
    int (*visitor)(struct rdsv3_connection *, void *),
    size_t item_len)
{
	struct rdsv3_ip_bucket *bucketp;
	struct rdsv3_connection *conn;
	uint8_t *buffer;

	lens->nr = 0;
	lens->each = item_len;

	rw_enter(&rdsv3_bind_lock, RW_READER);
	if (avl_is_empty(&rdsv3_bind_tree)) {
		/* no connections */
		rw_exit(&rdsv3_bind_lock);
		return;
	}

	/* allocate a little extra as this can get cast to a uint64_t */
	buffer = kmem_zalloc(item_len + 8, KM_SLEEP);

	bucketp = (struct rdsv3_ip_bucket *)avl_first(&rdsv3_bind_tree);
	do {
		rw_enter(&bucketp->conn_lock, RW_READER);
		if (avl_is_empty(&bucketp->conn_tree)) {
			/* no connections */
			rw_exit(&bucketp->conn_lock);
			continue;
		}

		conn = (struct rdsv3_connection *)avl_first(
		    &bucketp->conn_tree);
		do {
			/* XXX no c_lock usage.. */
			if (visitor(conn, buffer)) {
				/*
				 * We copy as much as we can fit in the buffer,
				 * but we count all items so that the caller
				 * can resize the buffer.
				 */
				if (len >= item_len) {
					RDSV3_DPRINTF4(
					    "rdsv3_for_each_conn_info",
					    "buffer: %p iter: %p bytes: %d",
					    buffer, iter->addr + iter->offset,
					    item_len);
					rdsv3_info_copy(iter, buffer, item_len);
					len -= item_len;
				}
				lens->nr++;
			}
			conn = AVL_NEXT(&bucketp->conn_tree, conn);
		} while (conn != NULL);
		rw_exit(&bucketp->conn_lock);

		bucketp = AVL_NEXT(&rdsv3_bind_tree, bucketp);
	} while (bucketp != NULL);

	rw_exit(&rdsv3_bind_lock);

	kmem_free(buffer, item_len + 8);
}

static int
rdsv3_conn_info_visitor(struct rdsv3_connection *conn, void *buffer)
{
	struct rds_info_connection *cinfo = buffer;

	cinfo->next_tx_seq = conn->c_next_tx_seq;
	cinfo->next_rx_seq = conn->c_next_rx_seq;
	cinfo->laddr = conn->c_laddr;
	cinfo->faddr = conn->c_faddr;
	(void) strncpy((char *)cinfo->transport, conn->c_trans->t_name,
	    sizeof (cinfo->transport));
	cinfo->flags = 0;

	rdsv3_conn_info_set(cinfo->flags,
	    MUTEX_HELD(&conn->c_send_lock), SENDING);

	/* XXX Future: return the state rather than these funky bits */
	rdsv3_conn_info_set(cinfo->flags,
	    atomic_get(&conn->c_state) == RDSV3_CONN_CONNECTING,
	    CONNECTING);
	rdsv3_conn_info_set(cinfo->flags,
	    atomic_get(&conn->c_state) == RDSV3_CONN_UP,
	    CONNECTED);
	return (1);
}

static void
rdsv3_conn_info(struct rsock *sock, unsigned int len,
    struct rdsv3_info_iterator *iter, struct rdsv3_info_lengths *lens)
{
	rdsv3_for_each_conn_info(sock, len, iter, lens,
	    rdsv3_conn_info_visitor, sizeof (struct rds_info_connection));
}

int
rdsv3_conn_init()
{
	RDSV3_DPRINTF4("rdsv3_conn_init", "Enter");

	rdsv3_loop_init();

	rdsv3_info_register_func(RDS_INFO_CONNECTIONS, rdsv3_conn_info);
	rdsv3_info_register_func(RDS_INFO_SEND_MESSAGES,
	    rdsv3_conn_message_info_send);
	rdsv3_info_register_func(RDS_INFO_RETRANS_MESSAGES,
	    rdsv3_conn_message_info_retrans);

	RDSV3_DPRINTF4("rdsv3_conn_init", "Return");

	return (0);
}

void
rdsv3_conn_exit()
{
	RDSV3_DPRINTF4("rdsv3_conn_exit", "Enter");

	rdsv3_loop_exit();

	rdsv3_info_deregister_func(RDS_INFO_CONNECTIONS, rdsv3_conn_info);
	rdsv3_info_deregister_func(RDS_INFO_SEND_MESSAGES,
	    rdsv3_conn_message_info_send);
	rdsv3_info_deregister_func(RDS_INFO_RETRANS_MESSAGES,
	    rdsv3_conn_message_info_retrans);

	RDSV3_DPRINTF4("rdsv3_conn_exit", "Return");
}

/*
 * Force a disconnect
 */
void
rdsv3_conn_drop(struct rdsv3_connection *conn)
{
	conn->c_state = RDSV3_CONN_ERROR;
	RDSV3_QUEUE_DELAYED_MSG(conn, &conn->c_down_w, 0);
}
