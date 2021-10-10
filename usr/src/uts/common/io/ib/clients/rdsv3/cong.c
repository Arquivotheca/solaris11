/*
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This file contains code imported from the OFED rds source file cong.c
 * Oracle elects to have and use the contents of cong.c under and governed
 * by the OpenIB.org BSD license (see below for full license text). However,
 * the following notice accompanied the original version of this file:
 */


/*
 * Copyright (c) 2007 Oracle.  All rights reserved.
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
#include <sys/ib/clients/rdsv3/rdsv3_impl.h>
#include <sys/ib/clients/rdsv3/rdsv3_debug.h>

/*
 * This file implements the receive side of the unconventional congestion
 * management in RDS.
 *
 * Messages waiting in the receive queue on the receiving socket are accounted
 * against the sockets SO_RCVBUF option value.  Only the payload bytes in the
 * message are accounted for.  If the number of bytes queued equals or exceeds
 * rcvbuf then the socket is congested.  All sends attempted to this socket's
 * address should return block or return -EWOULDBLOCK.
 *
 * Applications are expected to be reasonably tuned such that this situation
 * very rarely occurs.  An application encountering this "back-pressure" is
 * considered a bug.
 *
 * This is implemented by having each node maintain bitmaps which indicate
 * which ports on bound addresses are congested.  As the bitmap changes it is
 * sent through all the connections which terminate in the local address of the
 * bitmap which changed.
 *
 * The bitmaps are allocated as connections are brought up.  This avoids
 * allocation in the interrupt handling path which queues messages on sockets.
 * The dense bitmaps let transports send the entire bitmap on any bitmap change
 * reasonably efficiently.  This is much easier to implement than some
 * finer-grained communication of per-port congestion.  The sender does a very
 * inexpensive bit test to test if the port it's about to send to is congested
 * or not.
 */

/*
 * Interaction with poll is a tad tricky. We want all processes stuck in
 * poll to wake up and check whether a congested destination became uncongested.
 * The really sad thing is we have no idea which destinations the application
 * wants to send to - we don't even know which rdsv3_connections are involved.
 * So until we implement a more flexible rds poll interface, we have to make
 * do with this:
 * We maintain a global counter that is incremented each time a congestion map
 * update is received. Each rds socket tracks this value, and if rdsv3_poll
 * finds that the saved generation number is smaller than the global generation
 * number, it wakes up the process.
 */
static atomic_t		rdsv3_cong_generation = ATOMIC_INIT(0);

/*
 * Congestion monitoring
 */
static struct list rdsv3_cong_monitor;
static krwlock_t rdsv3_cong_monitor_lock;

static void
rdsv3_free_bitmap(struct rdsv3_cong_map *map)
{
	uint_t i;

	for (i = 0; i < RDSV3_CONG_MAP_PAGES; i++) {
		kmem_free((void *)map->m_page_addrs[i], RDSV3_FRAG_SIZE);
	}
	kmem_free(map, sizeof (struct rdsv3_cong_map));
}


static struct rdsv3_cong_map *
rdsv3_alloc_bitmap(uint32_be_t addr)
{
	struct rdsv3_cong_map *map;
	uint_t i;

	map = kmem_zalloc(sizeof (struct rdsv3_cong_map), KM_NOSLEEP);
	if (!map)
		return (NULL);

	for (i = 0; i < RDSV3_CONG_MAP_PAGES; i++) {
		map->m_page_addrs[i] =
		    (unsigned long)kmem_zalloc(RDSV3_FRAG_SIZE, KM_NOSLEEP);
		if (map->m_page_addrs[i] == NULL) {
			for (i = 0; i < RDSV3_CONG_MAP_PAGES; i++) {
				if (map->m_page_addrs[i] != NULL)
					kmem_free((void *)map->m_page_addrs[i],
					    RDSV3_FRAG_SIZE);
			}
			kmem_free(map, sizeof (struct rdsv3_cong_map));
			return (NULL);
		}
	}

	map->m_addr = addr;
	rdsv3_init_waitqueue(&map->m_waitq);

	return (map);
}

/*
 * Congestion Bit Map for the local IP address
 */
struct rdsv3_cong_map *
rdsv3_alloc_cong_map(struct rdsv3_ip_bucket *bucketp)
{
	if (bucketp->trans->alloc_cong_map)
		return (bucketp->trans->alloc_cong_map(bucketp));
	else
		return (rdsv3_alloc_bitmap(bucketp->ip));
}

void
rdsv3_free_cong_map(struct rdsv3_ip_bucket *bucketp)
{
	if (bucketp->trans->free_cong_map)
		bucketp->trans->free_cong_map(bucketp);
	else
		rdsv3_free_bitmap(bucketp->lcong);
}

int
rdsv3_cong_get_maps(struct rdsv3_connection *conn)
{
	conn->c_lcong = conn->c_bucketp->lcong;
	conn->c_fcong = rdsv3_alloc_bitmap(conn->c_faddr);

	if (!(conn->c_lcong && conn->c_fcong))
		return (-ENOMEM);

	return (0);
}

void
rdsv3_cong_queue_updates(struct rdsv3_ip_bucket *bucketp)
{
	struct rdsv3_connection *conn;

	RDSV3_DPRINTF4("rdsv3_cong_queue_updates",
	    "Enter(bucketp: %p)", bucketp);

	rw_enter(&bucketp->conn_lock, RW_READER);
	if (avl_is_empty(&bucketp->conn_tree)) {
		/* no connections */
		rw_exit(&bucketp->conn_lock);
		return;
	}

	conn = (struct rdsv3_connection *)avl_first(
	    &bucketp->conn_tree);
	do {
		if (!test_and_set_bit(0, &conn->c_map_queued)) {
			rdsv3_stats_inc(s_cong_update_queued);
			(void) rdsv3_send_xmit(conn);
		}

		conn = AVL_NEXT(&bucketp->conn_tree, conn);
	} while (conn != NULL);
	rw_exit(&bucketp->conn_lock);

	RDSV3_DPRINTF4("rdsv3_cong_queue_updates",
	    "Return(bucketp: %p)", bucketp);
}

void
rdsv3_cong_map_updated(struct rdsv3_cong_map *map, uint64_t portmask)
{
	RDSV3_DPRINTF4("rdsv3_cong_map_updated",
	    "waking map %p for %u.%u.%u.%u",
	    map, NIPQUAD(map->m_addr));

	rdsv3_stats_inc(s_cong_update_received);
	atomic_add_32(&rdsv3_cong_generation, 1);
#if 0
XXX
	if (waitqueue_active(&map->m_waitq))
#endif
		rdsv3_wake_up(&map->m_waitq);

	if (portmask && !list_is_empty(&rdsv3_cong_monitor)) {
		struct rdsv3_sock *rs;

		rw_enter(&rdsv3_cong_monitor_lock, RW_READER);
		RDSV3_FOR_EACH_LIST_NODE(rs, &rdsv3_cong_monitor,
		    rs_cong_list) {
			mutex_enter(&rs->rs_lock);
			rs->rs_cong_notify |= (rs->rs_cong_mask & portmask);
			rs->rs_cong_mask &= ~portmask;
			mutex_exit(&rs->rs_lock);
			if (rs->rs_cong_notify)
				rdsv3_wake_sk_sleep(rs);
		}
		rw_exit(&rdsv3_cong_monitor_lock);
	}

	RDSV3_DPRINTF4("rdsv3_cong_map_updated", "Return(map: %p)", map);
}

int
rdsv3_cong_updated_since(unsigned long *recent)
{
	unsigned long gen = atomic_get(&rdsv3_cong_generation);

	if (*recent == gen)
		return (0);
	*recent = gen;
	return (1);
}

/*
 * We're called under the locking that protects the sockets receive buffer
 * consumption.  This makes it a lot easier for the caller to only call us
 * when it knows that an existing set bit needs to be cleared, and vice versa.
 * We can't block and we need to deal with concurrent sockets working against
 * the same per-address map.
 */
void
rdsv3_cong_set_bit(struct rdsv3_cong_map *map, uint16_be_t port)
{
	unsigned long i;
	unsigned long off;

	RDSV3_DPRINTF4("rdsv3_cong_set_bit",
	    "setting congestion for %u.%u.%u.%u:%u in map %p",
	    NIPQUAD(map->m_addr), ntohs(port), map);

	i = ntohs(port) / RDSV3_CONG_MAP_PAGE_BITS;
	off = ntohs(port) % RDSV3_CONG_MAP_PAGE_BITS;
	set_le_bit(off, (void *)map->m_page_addrs[i]);
}

void
rdsv3_cong_clear_bit(struct rdsv3_cong_map *map, uint16_be_t port)
{
	unsigned long i;
	unsigned long off;

	RDSV3_DPRINTF4("rdsv3_cong_clear_bit",
	    "clearing congestion for %u.%u.%u.%u:%u in map %p\n",
	    NIPQUAD(map->m_addr), ntohs(port), map);

	i = ntohs(port) / RDSV3_CONG_MAP_PAGE_BITS;
	off = ntohs(port) % RDSV3_CONG_MAP_PAGE_BITS;
	clear_le_bit(off, (void *)map->m_page_addrs[i]);
}

static int
rdsv3_cong_test_bit(struct rdsv3_cong_map *map, uint16_be_t port)
{
	unsigned long i;
	unsigned long off;

	i = ntohs(port) / RDSV3_CONG_MAP_PAGE_BITS;
	off = ntohs(port) % RDSV3_CONG_MAP_PAGE_BITS;

	RDSV3_DPRINTF5("rdsv3_cong_test_bit", "port: 0x%x i = %lx off = %lx",
	    ntohs(port), i, off);

	return (test_le_bit(off, (void *)map->m_page_addrs[i]));
}

void
rdsv3_cong_add_socket(struct rdsv3_sock *rs)
{
	RDSV3_DPRINTF4("rdsv3_cong_add_socket", "Enter(rs: %p)", rs);

	rw_enter(&rdsv3_cong_monitor_lock, RW_WRITER);
	if (!list_link_active(&rs->rs_cong_list))
		list_insert_head(&rdsv3_cong_monitor, rs);
	rw_exit(&rdsv3_cong_monitor_lock);
}

void
rdsv3_cong_remove_socket(struct rdsv3_sock *rs)
{
	RDSV3_DPRINTF4("rdsv3_cong_remove_socket", "Enter(rs: %p)", rs);

	rw_enter(&rdsv3_cong_monitor_lock, RW_WRITER);
	list_remove_node(&rs->rs_cong_list);
	rw_exit(&rdsv3_cong_monitor_lock);

	if (rdsv3_cong_test_bit(rs->rs_bucketp->lcong, rs->rs_bound_port)) {
		rdsv3_cong_clear_bit(rs->rs_bucketp->lcong, rs->rs_bound_port);
		rdsv3_cong_queue_updates(rs->rs_bucketp);
	}
}

int
rdsv3_cong_wait(struct rdsv3_cong_map *map, uint16_be_t port, int nonblock,
    struct rdsv3_sock *rs)
{
	RDSV3_DPRINTF4("rdsv3_cong_wait", "Enter(rs: %p, mode: %d)",
	    rs, nonblock);

	if (!rdsv3_cong_test_bit(map, port))
		return (0);
	if (nonblock) {
		if (rs && rs->rs_cong_monitor) {
			/*
			 * It would have been nice to have an atomic set_bit on
			 * a uint64_t.
			 */
			mutex_enter(&rs->rs_lock);
			rs->rs_cong_mask |=
			    RDS_CONG_MONITOR_MASK(ntohs(port));
			mutex_exit(&rs->rs_lock);

			/*
			 * Test again - a congestion update may have arrived in
			 * the meantime.
			 */
			if (!rdsv3_cong_test_bit(map, port))
				return (0);
		}
		rdsv3_stats_inc(s_cong_send_error);
		return (-ENOBUFS);
	}

	rdsv3_stats_inc(s_cong_send_blocked);
	RDSV3_DPRINTF3("rdsv3_cong_wait", "waiting on map %p for port %u",
	    map, ntohs(port));

	mutex_enter(&map->m_waitq.waitq_mutex);
	map->m_waitq.waitq_waiters++;
	while (rdsv3_cong_test_bit(map, port)) {
		if (cv_wait_sig(&map->m_waitq.waitq_cv,
		    &map->m_waitq.waitq_mutex) == 0) {
			map->m_waitq.waitq_waiters--;
			mutex_exit(&map->m_waitq.waitq_mutex);
			return (-EINTR);
		}
	}
	map->m_waitq.waitq_waiters--;
	mutex_exit(&map->m_waitq.waitq_mutex);

	return (0);
}

void
rdsv3_cong_exit(void)
{
	ASSERT(list_is_empty(&rdsv3_cong_monitor));
	rw_destroy(&rdsv3_cong_monitor_lock);
}

/*
 * congestion map message is pre-allocated
 */
struct rdsv3_message *
rdsv3_cong_update_alloc(struct rdsv3_connection *conn)
{
	rdsv3_message_addref(conn->c_lcong->m_rm);
	return (conn->c_lcong->m_rm);
}

void
rdsv3_cong_init(void)
{
	list_create(&rdsv3_cong_monitor, sizeof (struct rdsv3_sock),
	    offsetof(struct rdsv3_sock, rs_cong_list));
	rw_init(&rdsv3_cong_monitor_lock, NULL, RW_DRIVER, NULL);
}
