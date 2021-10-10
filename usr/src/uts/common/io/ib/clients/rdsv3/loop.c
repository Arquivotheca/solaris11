/*
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This file contains code imported from the OFED rds source file loop.c
 * Oracle elects to have and use the contents of loop.c under and governed
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
#include <sys/rds.h>

#include <sys/ib/clients/rdsv3/rdsv3.h>
#include <sys/ib/clients/rdsv3/loop.h>
#include <sys/ib/clients/rdsv3/rdsv3_debug.h>

kmutex_t loop_conns_lock;
list_t loop_conns;

/*
 * This 'loopback' transport is a special case for flows that originate
 * and terminate on the same machine.
 *
 * Connection build-up notices if the destination address is thought of
 * as a local address by a transport.  At that time it decides to use the
 * loopback transport instead of the bound transport of the sending socket.
 *
 * The loopback transport's sending path just hands the sent rds_message
 * straight to the receiving path via an embedded rds_incoming.
 */

/*
 * Usually a message transits both the sender and receiver's conns as it
 * flows to the receiver.  In the loopback case, though, the receive path
 * is handed the sending conn so the sense of the addresses is reversed.
 */
static int
rdsv3_loop_xmit(struct rdsv3_connection *conn, struct rdsv3_message *rm)
{
	struct rdsv3_incoming *inc;
	int ret;

	/* Do not send cong updates to loopback */
	if (rm->m_hdr.h_flags & RDSV3_FLAG_CONG_BITMAP) {
		rdsv3_cong_map_updated(conn->c_fcong, ~(uint64_t)0);
		return (sizeof (struct rdsv3_header) + RDSV3_CONG_MAP_BYTES);
	}

	RDSV3_DPRINTF4("rdsv3_loop_xmit", "Enter(conn: %p, rm: %p)", conn, rm);

	inc = kmem_zalloc(sizeof (struct rdsv3_incoming), KM_SLEEP);
	rdsv3_inc_init(inc, conn, conn->c_laddr);
	inc->i_rm = rm;
	bcopy(&rm->m_hdr, &inc->i_hdr, sizeof (struct rdsv3_header));

	/* Matching put is in loop_inc_free() */
	rdsv3_message_addref(rm);

	rdsv3_recv_incoming(conn, conn->c_laddr, conn->c_faddr, inc,
	    KM_NOSLEEP);
	rdsv3_send_drop_acked(conn, ntohll(rm->m_hdr.h_sequence), NULL);

	ret = sizeof (struct rdsv3_header) + ntohl(rm->m_hdr.h_len);
	rdsv3_inc_put(inc);

	RDSV3_DPRINTF4("rdsv3_loop_xmit", "Return(conn: %p, rm: %p)", conn, rm);

	return (ret);
}

/*
 * See rds_loop_xmit(). Since our inc is embedded in the rm, we
 * make sure the rm lives at least until the inc is done.
 */
static void
rdsv3_loop_inc_free(struct rdsv3_incoming *inc)
{
	rdsv3_message_put(inc->i_rm);
	kmem_free(inc, sizeof (struct rdsv3_incoming));
}

static int
rdsv3_loop_xmit_cong_map(struct rdsv3_connection *conn,
    struct rdsv3_cong_map *map,
    unsigned long offset)
{
	RDSV3_DPRINTF4("rdsv3_loop_xmit_cong_map", "Enter(conn: %p)", conn);

	ASSERT(!offset);
	ASSERT(map == conn->c_lcong);

	rdsv3_cong_map_updated(conn->c_fcong, ~(uint64_t)0);

	RDSV3_DPRINTF4("rdsv3_loop_xmit_cong_map", "Return(conn: %p)", conn);

	return (sizeof (struct rdsv3_header) + RDSV3_CONG_MAP_BYTES);
}

/* we need to at least give the thread something to succeed */
/* ARGSUSED */
static int
rdsv3_loop_recv(struct rdsv3_connection *conn)
{
	return (0);
}

struct rdsv3_loop_connection {
	struct list_node loop_node;
	struct rdsv3_connection *conn;
};

/*
 * Even the loopback transport needs to keep track of its connections,
 * so it can call rdsv3_conn_destroy() on them on exit. N.B. there are
 * 1+ loopback addresses (127.*.*.*) so it's not a bug to have
 * multiple loopback conns allocated, although rather useless.
 */
/* ARGSUSED */
static int
rdsv3_loop_conn_alloc(struct rdsv3_connection *conn, int gfp)
{
	struct rdsv3_loop_connection *lc;

	RDSV3_DPRINTF4("rdsv3_loop_conn_alloc", "Enter(conn: %p)", conn);

	lc = kmem_zalloc(sizeof (struct rdsv3_loop_connection), KM_NOSLEEP);
	if (!lc)
		return (-ENOMEM);

	list_link_init(&lc->loop_node);
	lc->conn = conn;
	conn->c_transport_data = lc;

	mutex_enter(&loop_conns_lock);
	list_insert_tail(&loop_conns, lc);
	mutex_exit(&loop_conns_lock);

	RDSV3_DPRINTF4("rdsv3_loop_conn_alloc", "Return(conn: %p)", conn);

	return (0);
}

static void
rdsv3_loop_conn_free(void *arg)
{
	struct rdsv3_loop_connection *lc = arg;
	RDSV3_DPRINTF5("rdsv3_loop_conn_free", "lc %p\n", lc);
	list_remove_node(&lc->loop_node);
	kmem_free(lc, sizeof (struct rdsv3_loop_connection));
}

static int
rdsv3_loop_conn_connect(struct rdsv3_connection *conn)
{
	rdsv3_connect_complete(conn);
	return (0);
}

/* ARGSUSED */
static void
rdsv3_loop_conn_shutdown(struct rdsv3_connection *conn)
{
}

void
rdsv3_loop_exit(void)
{
	struct rdsv3_loop_connection *lc, *_lc;
	list_t tmp_list;

	RDSV3_DPRINTF4("rdsv3_loop_exit", "Enter");

	list_create(&tmp_list, sizeof (struct rdsv3_loop_connection),
	    offsetof(struct rdsv3_loop_connection, loop_node));

	/* avoid calling conn_destroy with irqs off */
	mutex_enter(&loop_conns_lock);
	list_splice(&loop_conns, &tmp_list);
	mutex_exit(&loop_conns_lock);

	RDSV3_FOR_EACH_LIST_NODE_SAFE(lc, _lc, &tmp_list, loop_node) {
		ASSERT(!lc->conn->c_passive);
		rdsv3_conn_destroy(lc->conn);
	}

	list_destroy(&loop_conns);
	mutex_destroy(&loop_conns_lock);

	RDSV3_DPRINTF4("rdsv3_loop_exit", "Return");
}

/* ARGSUSED */
void
rdsv3_loop_init_delayed_msg(rdsv3_delayed_work_t *dwp, void *ptr)
{
	dwp->wq = rdsv3_wq;
}

void
rdsv3_loop_queue_delayed_msg(rdsv3_delayed_work_t *dwp, uint_t delay)
{
	rdsv3_queue_delayed_work(rdsv3_wq, dwp, delay);
}

void
rdsv3_loop_cancel_delayed_msg(rdsv3_delayed_work_t *dwp)
{
	rdsv3_cancel_delayed_work(dwp);
}

void
rdsv3_loop_message_purge(struct rdsv3_message *rm)
{
	struct rdsv3_scatterlist *scat;
	int i;

	for (i = 0, scat = &rm->m_sg[0]; i < rm->m_nents; i++, scat++) {
		RDSV3_DPRINTF5("rdsv3_loop_message_purge",
		    "putting data page %p", (void *)rdsv3_sg_page(scat));

		kmem_free(rdsv3_sg_page(scat), rdsv3_sg_len(scat));
	}
}

/* ARGSUSED */
int
rdsv3_loop_message_copy_from_user(void *devp, struct rdsv3_message *rm,
    struct uio *uiop, size_t total_len)
{
	struct rdsv3_scatterlist *scat;
	int ret;

	RDSV3_DPRINTF4("rdsv3_loop_message_copy_from_user",
	    "Enter: %d", total_len);

	/*
	 * now allocate and copy in the data payload.
	 */
	scat = rm->m_sg;

	while (total_len) {
		ret = rdsv3_page_remainder_alloc(scat, total_len, 0);
		if (ret) {
			rdsv3_loop_message_purge(rm);
			return (ret);
		}
		rm->m_nents++;

		rdsv3_stats_add(s_copy_from_user, rdsv3_sg_len(scat));

		ret = uiomove(rdsv3_sg_page(scat), rdsv3_sg_len(scat),
		    UIO_WRITE, uiop);
		if (ret) {
			RDSV3_DPRINTF2("rdsv3_message_copy_from_user",
			    "uiomove failed");
			rdsv3_loop_message_purge(rm);
			return (ret);
		}

		total_len -= rdsv3_sg_len(scat);
		scat++;
	}

	return (0);
}

int
rdsv3_loop_inc_copy_to_user(struct rdsv3_incoming *inc,
    uio_t *uiop, size_t size)
{
	struct rdsv3_message *rm;
	struct rdsv3_scatterlist *sg;
	unsigned long to_copy;
	unsigned long vec_off;
	int copied;
	int ret;
	uint32_t len;

	rm = inc->i_rm;
	len = ntohl(inc->i_hdr.h_len);

	RDSV3_DPRINTF4("rdsv3_loop_inc_copy_to_user",
	    "Enter(rm: %p, len: %d)", rm, len);

	/* Handle inline messages */
	if (len <= rdsv3_max_inline) {
		rdsv3_stats_add(s_copy_to_user, len);

		ret = uiomove(rm->m_inline, len, UIO_READ, uiop);
		if (ret) {
			RDSV3_DPRINTF2("rdsv3_loop_inc_copy_to_user",
			    "Inline uiomove(%d) failed: %d", len, ret);
			return (0);
		}


		return (len);
	}

	sg = rm->m_sg;
	vec_off = 0;
	copied = 0;

	while (copied < size && copied < len) {

		to_copy = min(len - copied, sg->length - vec_off);
		to_copy = min(size - copied, to_copy);

		RDSV3_DPRINTF5("rdsv3_message_inc_copy_to_user",
		    "copying %lu bytes to user iov %p from sg [%p, %u] + %lu\n",
		    to_copy, uiop,
		    rdsv3_sg_page(sg), sg->length, vec_off);
		rdsv3_stats_add(s_copy_to_user, to_copy);

		ret = uiomove(rdsv3_sg_page(sg), to_copy, UIO_READ, uiop);
		if (ret)
			break;

		vec_off += to_copy;
		copied += to_copy;

		if (vec_off == sg->length) {
			vec_off = 0;
			sg++;
		}
	}

	return (copied);
}

/*
 * This is missing .xmit_* because loop doesn't go through generic
 * rdsv3_send_xmit() and doesn't call rdsv3_recv_incoming().  .listen_stop and
 * .laddr_check are missing because transport.c doesn't iterate over
 * rdsv3_loop_transport.
 */
#ifndef __lock_lint
struct rdsv3_transport rdsv3_loop_transport = {
	.xmit			= rdsv3_loop_xmit,
	.copy_from_user		= rdsv3_loop_message_copy_from_user,
	.message_purge		= rdsv3_loop_message_purge,
	.alloc_cong_map		= NULL,
	.xmit_cong_map		= rdsv3_loop_xmit_cong_map,
	.free_cong_map		= NULL,
	.recv			= rdsv3_loop_recv,
	.conn_alloc		= rdsv3_loop_conn_alloc,
	.conn_free		= rdsv3_loop_conn_free,
	.conn_connect		= rdsv3_loop_conn_connect,
	.conn_shutdown		= rdsv3_loop_conn_shutdown,
	.inc_copy_to_user	= rdsv3_loop_inc_copy_to_user,
	.inc_free		= rdsv3_loop_inc_free,
	.init_delayed_msg	= rdsv3_loop_init_delayed_msg,
	.queue_delayed_msg	= rdsv3_loop_queue_delayed_msg,
	.cancel_delayed_msg	= rdsv3_loop_cancel_delayed_msg,
	.laddr_check		= NULL,
	.xmit_prepare		= NULL,
	.xmit_complete		= NULL,
	.xmit_rdma		= NULL,

	.cm_handle_connect	= NULL,
	.cm_initiate_connect	= NULL,
	.cm_connect_complete	= NULL,

	.stats_info_copy	= NULL,
	.get_mr			= NULL,
	.sync_mr		= NULL,
	.free_mr		= NULL,
	.flush_mrs		= NULL,
	.t_name			= "loopback",
};
#else
struct rdsv3_transport rdsv3_loop_transport;
#endif
