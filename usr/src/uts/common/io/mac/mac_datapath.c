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
#include <sys/vlan.h>
#include <sys/dlpi.h>
#include <sys/strsubr.h>
#include <sys/pattr.h>
#include <sys/callb.h>
#include <sys/time.h>
#include <sys/thread.h>
#include <sys/mac_impl.h>
#include <sys/mac_client_impl.h>
#include <sys/mac_client_priv.h>
#include <sys/mac_flow_impl.h>
#include <sys/mac_cpu_impl.h>
#include <sys/sysmacros.h>

/*
 * This file contains the mac datapath implementation.
 * Details about each component can be found in each code section.
 */

/*
 * Declarations and tunables
 */

#define	INLINE_DRAIN		1
#define	WORKER_DRAIN		2
#define	MAX_FANOUT_SUBSET_SZ	16
#define	FD_NEWDATA		0x01

typedef struct {
	mac_queue_t	fd_ipq;
	mac_queue_t	fd_fullq;
	uint_t		fd_flags;
} fanout_data_t;

typedef enum {
	RX_DROP,
	TX_DROP
} mac_drop_type_t;

static uint_t	mac_pktsize(mblk_t *);
static int	mac_tx_try_hold(mac_client_impl_t *, mac_tx_percpu_t **);
static void	mac_tx_rele(mac_client_impl_t *, mac_tx_percpu_t *);
static mac_tx_cookie_t	mac_tx_single(mac_client_handle_t, mblk_t *,
			    uintptr_t, uint16_t, mblk_t **, mac_tx_percpu_t *);
static void	mac_tx_client_stats_update(mac_tx_percpu_t *, uint_t);
static void	mac_tx_ring_stats_update(mac_ring_t *, uint_t);
static void	mac_initq(mac_queue_t *);
static void	mac_flushq(mac_queue_t *);
static void	mac_enq(mac_queue_t *, mac_queue_t *);
static void	mac_rx_enqueue(mac_client_impl_t *, mac_ring_t *, boolean_t,
		    mac_pkt_info_t *, mac_queue_t *, mac_queue_t *, mblk_t *);
static void	mac_rx_stats_update(mac_client_impl_t *, mac_ring_t *,
		    uint_t, uint_t);
static mblk_t	*mac_rx_process(mac_client_impl_t *, mac_ring_t *, mblk_t *,
		    uint_t);
static void	mac_rx_sendup(mac_client_impl_t *, mblk_t *, mblk_t *,
		    mac_rx_attr_t *, uint_t);
static void	mac_rx_process_fanout(mac_client_impl_t *, mac_ring_t *,
		    mac_rx_fanout_t *, fanout_data_t *, uint_t);
static void	mac_rx_fanout_worker(mac_rx_fanout_t *);
static void	mac_poll_worker_wakeup(mac_ring_t *);
static void	mac_poll_ring_enable(mac_ring_t *);
static void	mac_poll_estimate_init(mac_ring_t *);

/*
 * Inline functions give additional performance boost for non-debug bits.
 */
#ifndef	DEBUG
#pragma	inline(mac_pktsize)
#pragma	inline(mac_tx_try_hold)
#pragma	inline(mac_tx_rele)
#pragma	inline(mac_tx_client_stats_update)
#pragma	inline(mac_tx_ring_stats_update)
#pragma	inline(mac_initq)
#pragma	inline(mac_flushq)
#pragma	inline(mac_enq)
#pragma	inline(mac_rx_enqueue)
#pragma	inline(mac_rx_stats_update)
#pragma	inline(mac_rx_process)
#pragma	inline(mac_rx_sendup)
#pragma	inline(mac_rx_process_fanout)
#pragma	inline(mac_poll_worker_wakeup)
#pragma	inline(mac_poll_ring_enable)
#pragma	inline(mac_poll_estimate_init)
#endif

/*
 * Serialization
 *
 * mac_serialize_override: this overrides the serialization flag advertised by
 * driver. mac_serialize_on is used instead to decide whether to do
 * serialization.
 */
boolean_t	mac_serialize_override = B_FALSE;
boolean_t	mac_serialize_on = B_FALSE;

/*
 * TX low/high watermarks
 *
 * mac_tx_lowat: flow control will be activated if the number of packets
 *               queued on a particular ring exceeds this number.
 *
 * mac_tx_hiwat: new packets will be dropped if the number of packets
 *               queued on a particular ring exceeds this number.
 */
uint_t		mac_tx_lowat = 1000;
uint_t		mac_tx_hiwat = 10000;

/*
 * TX inline drain
 *
 * if mac_tx_inline_drain is B_TRUE, allow the non-worker thread caller
 * to drain up to mac_tx_inline_drain_max packets. The purpose of this
 * is to reduce context switches.
 */
boolean_t	mac_tx_inline_drain = B_TRUE;
uint_t		mac_tx_inline_drain_max = 3;

/*
 * RX worker fanout
 *
 * mac_rx_fanout_enable:	enable worker threads for rx processing.
 *
 * mac_rx_fanout_inline_max:	max number of packets the poll/interrupt
 *				thread is allowed to drain.
 *
 * mac_rx_fanout_hiwat:		max number of packets allowed to be queued
 *				at each fanout queue. newer packets will be
 *				dropped once this limit is reached.
 *
 * mac_rx_fanout_boost:		this is set on slower multi-core systems to
 *				boost the number of fanout workers.
 *
 * mac_rx_fanout_*gb_*:		default number of fanout workers on 1Gbps and
 *				10Gbps links.
 *
 */
boolean_t	mac_rx_fanout_enable = B_TRUE;
uint_t		mac_rx_fanout_hiwat = 10000;
uint_t		mac_rx_fanout_inline_max = 1;

/*
 * This is needed for Infiniband to ensure that RX buffers get released
 * as quickly as possible.
 */
uint_t		mac_ib_rx_fanout_inline_max = 0;

#if defined(__sparc)
boolean_t	mac_rx_fanout_boost = B_TRUE;
#else
boolean_t	mac_rx_fanout_boost = B_FALSE;
#endif

uint32_t 	mac_rx_fanout_1g = 1;
uint32_t 	mac_rx_fanout_10g = 8;
uint32_t 	mac_rx_fanout_1g_boost = 8;
uint32_t 	mac_rx_fanout_10g_boost = 16;

/*
 * Polling
 *
 * mac_poll_enable:		enable polling.
 *
 * mac_poll_bytes:		max number of bytes to retrieve from the
 *				driver on each call to the driver's poll
 *				entry point.
 *
 * mac_poll_pkts:		max number of packets to retrieve from the
 *				driver on each call to the driver's poll
 *				entry point.
 *
 * mac_poll_threshold:		switch to polling mode if the number of packets
 *				enqueued onto one or more worker threads from a
 *				single rx upcall (mac_rx_common) exceeds this
 *				threshold.
 *
 * mac_poll_max_backlog:	do not switch into polling mode if the backlog
 *				on a fanout worker queue exceeds this number.
 *
 * mac_poll_delay:		wait till the worker threads are done
 *				processing the outstanding packets before
 *				getting new ones.
 *
 */
boolean_t	mac_poll_enable = B_TRUE;
boolean_t	mac_poll_loop = B_TRUE;
uint64_t	mac_poll_delay = 5000000;
int		mac_poll_bytes_default = 300000;
int		mac_poll_pkts_default = 4800;
uint_t		mac_poll_threshold = 20;
uint_t		mac_poll_max_backlog = 300;

boolean_t	mac_do_soft_lso = B_TRUE;


/*
 * Misc utility functions
 */

/*
 * This inline function avoids the function call overhead for
 * single-mblk packets.
 */
static uint_t
mac_pktsize(mblk_t *mp)
{
	return (mp->b_cont == NULL ? MBLKL(mp) : msgdsize(mp));
}

/*
 * This slower version can be used for non-performance-critical cases.
 */
static uint_t
mac_chainsize(mblk_t *mp_chain, uint_t *cnt)
{
	mblk_t	*mp =  mp_chain;
	uint_t	sz = 0, n = 0;

	while (mp != NULL) {
		n++;
		sz += msgdsize(mp);
		mp = mp->b_next;
	}
	if (cnt != NULL)
		*cnt = n;

	return (sz);
}

/*
 * Simple queue manipulation routines
 */
static void
mac_initq(mac_queue_t *q)
{
	q->mq_head = NULL;
	q->mq_tailp = &q->mq_head;
	q->mq_cnt = 0;
}

static void
mac_flushq(mac_queue_t *q)
{
	freemsgchain(q->mq_head);
	mac_initq(q);
}

static void
mac_enq(mac_queue_t *dq, mac_queue_t *sq)
{
	if (sq->mq_head == NULL)
		return;

	*dq->mq_tailp = sq->mq_head;
	dq->mq_tailp = sq->mq_tailp;
	dq->mq_cnt += sq->mq_cnt;
}

/*
 * Drop packet and update ring stats
 */
static void
mac_ring_drop_stats_update(mac_ring_t *ring, uint_t sz, uint_t n,
    mac_drop_type_t type)
{
	if (type == RX_DROP) {
		MR_RX_STAT(ring, idropbytes) += sz;
		MR_RX_STAT(ring, idropcnt) += n;
	} else if (type == TX_DROP) {
		MR_TX_STAT(ring, odropbytes) += sz;
		MR_TX_STAT(ring, odropcnt) += n;
	}
}

static void
mac_ring_drop(mac_ring_t *ring, mblk_t *mp_chain,
    mac_drop_type_t type)
{
	uint_t	sz, n;

	sz = mac_chainsize(mp_chain, &n);
	freemsgchain(mp_chain);
	if (ring == NULL)
		return;

	mac_ring_drop_stats_update(ring, sz, n, type);
}

/*
 * Drop packet and update mac client stats
 */
static void
mac_client_drop_stats_update(mac_client_impl_t *mcip, uint_t sz,
    uint_t n, mac_drop_type_t type)
{
	if (type == RX_DROP) {
		MCIP_RX_STAT(mcip, idropbytes) += sz;
		MCIP_RX_STAT(mcip, idropcnt) += n;
	} else if (type == TX_DROP) {
		MCIP_TX_STAT(mcip, odropbytes) += sz;
		MCIP_TX_STAT(mcip, odropcnt) += n;
	}
}

static void
mac_client_drop(mac_client_impl_t *mcip, mblk_t *mp_chain,
    mac_drop_type_t type)
{
	uint_t	sz, n;

	sz = mac_chainsize(mp_chain, &n);
	freemsgchain(mp_chain);
	if (mcip == NULL)
		return;

	mac_client_drop_stats_update(mcip, sz, n, type);
}

/*
 * TX datapath
 */

/*
 * Put a hold on a mac client to prevent it and underlying tx-related data
 * structures from disappearing. MCI_TX_QUIESCE indicates that a control
 * operation wants to quiesce the Tx data flow in which case we return an
 * error. Holding any of the per cpu locks ensures that the mci_tx_flag won't
 * change.
 *
 * 'CPU' must be accessed just once and used to compute the index into the
 * percpu array, and that index must be used for the entire duration of the
 * packet send operation. Note that the thread may be preempted and run on
 * another cpu any time and so we can't use 'CPU' more than once for the
 * operation.
 */
static int
mac_tx_try_hold(mac_client_impl_t *mcip, mac_tx_percpu_t **mytxp)
{
	mac_tx_percpu_t	*mytx;
	int		error;

	mytx = &mcip->mci_tx_pcpu[CPU->cpu_seqid & mac_tx_percpu_cnt];
	mutex_enter(&mytx->pcpu_tx_lock);
	if ((mcip->mci_tx_flag & MCI_TX_QUIESCE) == 0) {
		mytx->pcpu_tx_refcnt++;
		*mytxp = mytx;
		error = 0;
	} else {
		error = -1;
	}
	mutex_exit(&mytx->pcpu_tx_lock);
	return (error);
}

/*
 * Release the hold on the mac client. If needed, signal any control operation
 * waiting for Tx quiescence. The wait and signal are always done using the
 * mci_tx_pcpu[0]'s lock.
 */
static void
mac_tx_rele(mac_client_impl_t *mcip, mac_tx_percpu_t *mytx)
{
	mutex_enter(&mytx->pcpu_tx_lock);
	if (--mytx->pcpu_tx_refcnt == 0 &&
	    (mcip->mci_tx_flag & MCI_TX_QUIESCE) != 0) {
		mutex_exit(&mytx->pcpu_tx_lock);
		mutex_enter(&mcip->mci_tx_pcpu[0].pcpu_tx_lock);
		cv_signal(&mcip->mci_tx_cv);
		mutex_exit(&mcip->mci_tx_pcpu[0].pcpu_tx_lock);
	} else {
		mutex_exit(&mytx->pcpu_tx_lock);
	}
}

/*
 * Deliver a packet to a local mac client if a matching L2 flow is found.
 * Returns B_TRUE if the packet is processed (delivered or dropped) and
 * B_FALSE if the packet needs to be sent out the wire.
 */
static boolean_t
mac_tx_loopback(mac_impl_t *mip, mac_client_impl_t *mcip, mblk_t *mp,
    uint16_t hint, mac_tx_percpu_t *mytx)
{
	flow_entry_t		*dst_flent = NULL;
	mac_client_impl_t	*dst_mcip;
	size_t			hdrsize, pktsize;
	void			*cookie;
	int			err;
	boolean_t		islso;
	uint32_t		flags = 0;
	uint_t			localcnt = 1;
	uint32_t		mss;

	mac_lso_get(mp, &mss, &flags);
	islso = (flags & HW_LSO) != 0;

	err = mac_flow_lookup(mip->mi_flow_tab, mp, FLOW_OUTBOUND, &dst_flent);
	if (err != 0)
		return (B_FALSE);

	/*
	 * The looked-up flow entry is one of the flows of the mac client
	 * but it may not be the main flow (dst_mcip->mci_flent) owned by
	 * the mac client. We need to hold the main flow instead of the
	 * auxillary one.
	 */
	if ((dst_mcip = dst_flent->fe_mcip) != NULL &&
	    dst_mcip->mci_flent != dst_flent) {
		FLOW_REFRELE(dst_flent);
		dst_flent = dst_mcip->mci_flent;
		FLOW_TRY_REFHOLD(dst_flent, err);
		if (err != 0)
			return (B_FALSE);
	}

	if (mip->mi_info.mi_nativemedia == DL_ETHER) {
		struct ether_vlan_header *evhp =
		    (struct ether_vlan_header *)mp->b_rptr;

		if (ntohs(evhp->ether_tpid) == ETHERTYPE_VLAN)
			hdrsize = sizeof (*evhp);
		else
			hdrsize = sizeof (struct ether_header);
	} else {
		mac_header_info_t	mhi;

		err = mac_header_info((mac_handle_t)mip, mp, &mhi);
		if (err == 0)
			hdrsize = mhi.mhi_hdrsize;
	}
	pktsize = mac_pktsize(mp);

	/*
	 * Make sure the header is valid and packet size is within the allowed
	 * size for non-LSO packets. If not, drop the packet.
	 */
	if (err != 0 || (!islso && (pktsize - hdrsize) > mip->mi_sdu_max)) {
		DTRACE_PROBE2(loopback__drop, size_t, pktsize, mblk_t *, mp);
		mac_client_drop(mcip, mp, TX_DROP);
		FLOW_REFRELE(dst_flent);
		return (B_TRUE);
	}
	mac_tx_client_stats_update(mytx, pktsize);

	if ((dst_flent->fe_type & FLOW_MCAST) != 0) {
		/*
		 * A broadcast/multicast flow must have a flow cookie.
		 */
		cookie = mac_flow_get_client_cookie(dst_flent);
		mac_bcast_send(cookie, mcip, mp, MAC_RX_LOOPBACK);
	} else {
		mblk_t		*mp1 = NULL;

		/*
		 * mac_promisc_dispatch() needs to be called here because when
		 * we return from this function, mac_tx_single() will return
		 * without continuing to send the packet out the wire.
		 */
		if (mip->mi_promisc_list != NULL)
			mac_promisc_dispatch(mip, mp, mcip);

		/* If this is a LSO packet, do soft LSO */
		if (islso && mac_do_soft_lso)
			mp = mac_do_softlso(mp, mss, &pktsize, &localcnt);

		if (mp != NULL)
			mp1 = mac_fix_cksum(mp, B_TRUE, hint);

		if (mp1 != NULL) {
			MCIP_MISC_STAT(mcip, txlocalbytes) += pktsize;
			MCIP_MISC_STAT(mcip, txlocalcnt) += localcnt;

			(dst_flent->fe_cb_fn)(
			    dst_flent->fe_cb_arg1,
			    dst_flent->fe_cb_arg2,
			    mp1, MAC_RX_LOOPBACK);
		} else {
			mac_client_drop_stats_update(mcip, pktsize,
			    1, TX_DROP);
		}
	}
	FLOW_REFRELE(dst_flent);
	return (B_TRUE);
}

/*
 * Default behavior: (Absence of MAC_DROP_ON_NO_DESC, MAC_TX_NO_ENQUEUE flags)
 *
 * Enqueue packet onto a tx ring if there is space available. Only return the
 * 'blocked' status if the queue size has exceeded mac_tx_lowat. This helps
 * performance on nics that flow-control frequently. The mac_tx_lowat buffer
 * space allows for packets to be enqueued while simultaneously allowing the
 * tx worker thread to drain packets. The mac_tx_hiwat is the limit above
 * which we will always drop the packets. Queue lengths between the lowat and
 * hiwat return a 'blocked' status, but we don't drop packets while still in
 * this range to avoid dropping TCP packets.
 *
 * The above default behavior can be changed by the clients using special flags
 *
 * The MAC_DROP_ON_NO_DESC flag, typically used by IP in the forwarding path
 * asks us to drop packets the moment we run out of resources. The return
 * value has no significance.
 *
 * The MAC_TX_NO_ENQUEUE flag typically used by the aggregation driver
 * asks us to return the packet rather than enqueueing it in our Tx queue
 * if we run out of resources. A return value of B_TRUE implies the packet
 * was returned in 'ret_mp'. A return value of B_FALSE implies the packet
 * was consumed.
 */
static boolean_t
mac_tx_ring_enqueue(mac_ring_handle_t rh, mblk_t *mp, uint16_t flags,
    mblk_t **ret_mp)
{
	mac_ring_t 	*ring = (mac_ring_t *)rh;
	boolean_t	client_blocked;

	if ((flags & MAC_DROP_ON_NO_DESC) != 0) {
		if (ring->mr_tx_cnt >= mac_tx_lowat) {
			DTRACE_PROBE1(tx__nodesc__drop, mac_ring_t *, ring);
			mac_ring_drop(ring, mp, TX_DROP);
			return (B_FALSE);
		}
	} else if ((flags & MAC_TX_NO_ENQUEUE) != 0) {
		if (ring->mr_tx_cnt >= mac_tx_lowat) {
			if (ret_mp != NULL)
				*ret_mp = mp;
			return (B_TRUE);
		}
	}
	if (ring->mr_tx_cnt >= mac_tx_hiwat) {
		DTRACE_PROBE1(tx__hiwat__drop, mac_ring_t *, ring);
		mac_ring_drop(ring, mp, TX_DROP);
		return (B_FALSE);
	}
	*ring->mr_tx_tailp = mp;
	ring->mr_tx_tailp = &mp->b_next;
	ring->mr_tx_cnt++;

	client_blocked = ring->mr_tx_cnt >= mac_tx_lowat;
	if (client_blocked) {
		if ((ring->mr_worker_state & MR_TX_WAITING) == 0) {
			ring->mr_worker_state |= MR_TX_WAITING;
			MR_TX_STAT(ring, client_blockcnt) += 1;
		}
	}

	return ((flags & MAC_TX_NO_ENQUEUE) != 0 ? B_FALSE : client_blocked);
}

/*
 * Drain enqueued packets on a tx ring.
 * If caller is the tx worker, then drain continuously until flow-controlled.
 * If caller is not the tx worker, only drain up to mac_tx_inline_drain_max
 * packets. Inline draining is in useful for reducing context switches and
 * improving latencies. To avoid making the non-worker sender wait too long,
 * we limit the number of inline-drain packets to be mac_tx_inline_drain_max.
 */
static boolean_t
mac_tx_ring_drain(mac_ring_t *ring, int type)
{
	mblk_t		*head, *mp, **oldtailp;
	mac_ring_info_t	*info = &ring->mr_info;
	uint_t		sz, drain_max, drained, total_drained = 0;
	boolean_t	driver_blocked = B_FALSE;

	ASSERT(MUTEX_HELD(&ring->mr_lock));
	ASSERT(ring->mr_worker_state & MR_TX_BUSY);
	drain_max = mac_tx_inline_drain_max;

	do {
		/*
		 * We dequeue the whole mr_tx_queue chain here but we don't
		 * decrement mr_tx_cnt until later. The reason for this is to
		 * prevent another thread from queueing more packets before
		 * we've completely finished sending the outstanding ones.
		 */
		head = ring->mr_tx_queue;
		if (head != NULL) {
			ring->mr_tx_queue = NULL;
			oldtailp = ring->mr_tx_tailp;
			ring->mr_tx_tailp = &ring->mr_tx_queue;
		}
		mutex_exit(&ring->mr_lock);

		drained = 0;
		while (head != NULL) {
			mp = head;
			head = mp->b_next;
			mp->b_next = NULL;
			sz = mac_pktsize(mp);

			mp = info->mri_tx(info->mri_driver, mp);
			if (mp != NULL) {
				driver_blocked = B_TRUE;
				mp->b_next = head;
				head = mp;
				break;
			}
			mac_tx_ring_stats_update(ring, sz);
			drained++;
			total_drained++;
			if (type == INLINE_DRAIN && total_drained > drain_max)
				break;
		}

		mutex_enter(&ring->mr_lock);
		if (head != NULL) {
			if (ring->mr_tx_queue == NULL)
				ring->mr_tx_tailp = oldtailp;

			*oldtailp = ring->mr_tx_queue;
			ring->mr_tx_queue = head;
		}
		/*
		 * We check for > 0 here because another thread may try to
		 * flush all packets from the ring, causing mr_tx_cnt to
		 * become 0.
		 */
		if (ring->mr_tx_cnt > 0)
			ring->mr_tx_cnt -= drained;

	} while (ring->mr_tx_queue != NULL && !driver_blocked &&
	    (type == WORKER_DRAIN || total_drained < drain_max));

	return (driver_blocked);
}

/*
 * Send a packet out a specified TX ring. The return value of this function
 * has the same semantics as the return value of mac_tx_ring_enqueue().
 * Please see the block comment above above mac_tx_ring_enqueue() for details.
 */
boolean_t
mac_tx_ring(mac_ring_handle_t rh, mblk_t *mp, uint16_t flags, mblk_t **ret_mp)
{
	mac_ring_t 	*ring = (mac_ring_t *)rh;
	mac_ring_info_t *info = &ring->mr_info;
	boolean_t	client_blocked = B_FALSE;
	boolean_t	do_drain = mac_tx_inline_drain;
	boolean_t	do_serialize;
	uint_t		sz;

	ASSERT(ring->mr_type == MAC_RING_TYPE_TX && ring->mr_state >= MR_INUSE);

	do_serialize = mac_serialize_override ? mac_serialize_on :
	    ((info->mri_flags & MAC_RING_TX_SERIALIZE) != 0);

	if (!do_serialize) {
		/*
		 * These checks need not be accurate because we recheck again
		 * with mr_lock held in the code below.
		 */
		if ((ring->mr_worker_state & MR_TX_BUSY) != 0 ||
		    ring->mr_tx_cnt != 0)
			goto serialize;

		sz = mac_pktsize(mp);
		mp = info->mri_tx(info->mri_driver, mp);
		if (mp != NULL) {
			/*
			 * No need to drain if we just failed to send the
			 * packet. Let the worker deal with it.
			 */
			do_drain = B_FALSE;
			goto serialize;
		}
		mac_tx_ring_stats_update(ring, sz);
		return (B_FALSE);
	}

serialize:
	/*
	 * Serialization mode
	 * Allows only one thread at a time to drain the TX ring queue.
	 * MR_TX_BUSY is marked by the owner thread to prevent other clients
	 * from accessing the same TX ring queue.
	 */
	mutex_enter(&ring->mr_lock);
	if ((ring->mr_worker_state & MR_TX_BUSY) != 0 ||
	    ring->mr_tx_cnt != 0) {
		client_blocked = mac_tx_ring_enqueue(rh, mp, flags, ret_mp);
		mutex_exit(&ring->mr_lock);
		return (client_blocked);
	}

	ring->mr_worker_state |= MR_TX_BUSY;
	*ring->mr_tx_tailp = mp;
	ring->mr_tx_tailp = &mp->b_next;
	ring->mr_tx_cnt++;

	if (do_drain)
		(void) mac_tx_ring_drain(ring, INLINE_DRAIN);

	client_blocked = (ring->mr_tx_cnt >= mac_tx_lowat);
	if (client_blocked) {
		if ((ring->mr_worker_state & MR_TX_WAITING) == 0) {
			ring->mr_worker_state |= MR_TX_WAITING;
			MR_TX_STAT(ring, client_blockcnt) += 1;
		}
	}

	if (ring->mr_tx_cnt != 0)
		cv_signal(&ring->mr_cv);

	ring->mr_worker_state &= ~MR_TX_BUSY;
	mutex_exit(&ring->mr_lock);

	return ((flags & MAC_TX_NO_ENQUEUE) != 0 ? B_FALSE : client_blocked);
}

/*
 * Update TX stats for a mac client
 */
static void
mac_tx_client_stats_update(mac_tx_percpu_t *mytx, uint_t sz)
{
	mutex_enter(&mytx->pcpu_tx_lock);
	mytx->pcpu_tx_obytes += sz;
	mytx->pcpu_tx_opackets += 1;
	mutex_exit(&mytx->pcpu_tx_lock);
}

/*
 * Update TX stats for a ring
 */
static void
mac_tx_ring_stats_update(mac_ring_t *ring, uint_t sz)
{
	MR_TX_STAT(ring, obytes) += sz;
	MR_TX_STAT(ring, opackets) += 1;
}

/*
 * Send a packet out the specified mac client.
 * Return a non-NULL opaque cookie if flow-control condition is triggered.
 * 'hint' is a numeric value used for choosing the outgoing TX ring.
 * 'flags' are used for indicating how the packet should be handled during
 * flow-control (see usage in mac_tx_ring_enqueue()).
 * 'ret_mp' allows the mac layer to return the mp back to the client rather
 * than leaving it queued in case of flow control.
 */
static mac_tx_cookie_t
mac_tx_single(mac_client_handle_t mch, mblk_t *mp, uintptr_t hint,
    uint16_t flags, mblk_t **ret_mp, mac_tx_percpu_t *mytx)
{
	mac_tx_cookie_t		cookie = NULL;
	mac_ring_handle_t	ring = NULL;
	mac_client_impl_t	*mcip = (mac_client_impl_t *)mch;
	mac_impl_t		*mip = mcip->mci_mip;
	mac_group_t		*tx_group;
	boolean_t		blocked;

	if (mcip->mci_feature != 0) {
		uint_t	sz = mac_pktsize(mp);

		/*
		 * If mac protection is enabled, only the permissible packets
		 * will be returned by mac_protect_check().
		 */
		if ((mcip->mci_feature & MAC_PROTECT) != 0) {
			if ((mp = mac_protect_check(mch, mp)) == NULL) {
				mac_client_drop_stats_update(mcip, sz, 1,
				    TX_DROP);
				goto done;
			}
		}
		if ((mcip->mci_feature &
		    (MAC_BWCTL_LINK | MAC_BWCTL_FLOW)) != 0) {
			mp = mac_bwctl_tx_check(mcip, mp, (void **)&cookie);
			if (mp == NULL) {
				mac_client_drop_stats_update(mcip, sz, 1,
				    TX_DROP);
				goto done;
			}
		}
	}

	/*
	 * Since dls always opens the underlying MAC, nclients equals
	 * to 1 means that the only active client is dls itself acting
	 * as a primary client of the MAC instance. Since dls will not
	 * send tagged packets in that case, and dls is trusted to send
	 * packets for its allowed VLAN(s), the VLAN tag insertion and
	 * check is required only if nclients is greater than 1.
	 */
	if (mip->mi_nclients > 1) {
		if (MAC_VID_CHECK_NEEDED(mcip)) {
			int	err = 0;

			MAC_VID_CHECK(mcip, mp, err);
			if (err != 0) {
				mac_client_drop(mcip, mp, TX_DROP);
				goto done;
			}
		}
		if (MAC_TAG_NEEDED(mcip)) {
			mp = mac_add_vlan_tag(mp,
			    MCIP_RESOURCE_PROPS_USRPRI(mcip),
			    mac_client_vid(mch));
			if (mp == NULL) {
				MCIP_MISC_STAT(mcip, txerrors) += 1;
				goto done;
			}
		}
		if (mip->mi_nactiveclients > 1 &&
		    mac_tx_loopback(mip, mcip, mp, (uint16_t)hint, mytx)) {
			goto done;
		}
	}

	/*
	 * TX ring selection
	 */
	if (mcip->mci_select_ring == NULL) {
		mac_rings_cache_t	*mrc;

		tx_group = mcip->mci_flent->fe_tx_ring_group;
		if (tx_group != NULL &&
		    (mcip->mci_state_flags & MCIS_SHARE_BOUND) == 0) {
			/*
			 * The user priority of a MAC client has a 1-1
			 * mapping with the Traffic Class, hence can
			 * directly index into the per-Traffic Class
			 * rings cache. In the future, if there is a
			 * user priority -> Traffic Class mapping we'd
			 * have to use the mapping here.
			 */
			mrc = &tx_group->mrg_rings_cache[
			    MCIP_RESOURCE_PROPS_USRPRI(mcip)];
			if (mrc->mrc_count == 0) {
				mac_client_drop(mcip, mp, TX_DROP);
				goto done;
			}
			ring = (mac_ring_handle_t)mrc->mrc_rings[
			    hint % mrc->mrc_count];
		} else {
			ring = mip->mi_default_tx_ring;
		}
	} else {
		ring = mcip->mci_select_ring(mcip->mci_select_ring_arg,
		    mp, hint);
		if (ring == NULL) {
			mac_client_drop(mcip, mp, TX_DROP);
			goto done;
		}
	}
	mac_tx_client_stats_update(mytx, mac_pktsize(mp));

	if (mip->mi_promisc_list != NULL)
		mac_promisc_dispatch(mip, mp, mcip);

	if (mip->mi_bridge_link == NULL) {
		blocked = mac_tx_ring(ring, mp, flags, ret_mp);
	} else {
		blocked = mac_tx_bridge(mip, ring, mp, flags, ret_mp);
	}
	if (blocked && cookie == NULL)
		cookie = (mac_tx_cookie_t)ring;

done:
	return (cookie);
}

/*
 * Send a single packet
 */
mac_tx_cookie_t
mac_tx(mac_client_handle_t mch, mblk_t *mp, uintptr_t hint,
    uint16_t flags, mblk_t **ret_mp)
{
	mac_client_impl_t	*mcip = (mac_client_impl_t *)mch;
	mac_tx_cookie_t		cookie = NULL;
	mac_tx_percpu_t		*mytx;

	/*
	 * Hold a reference to the mac client to prevent it from disappearing.
	 */
	if (mac_tx_try_hold(mcip, &mytx) != 0) {
		mac_client_drop(mcip, mp, TX_DROP);
		return ((mac_tx_cookie_t)-1);
	}
	cookie = mac_tx_single(mch, mp, hint, flags, ret_mp, mytx);
	mac_tx_rele(mcip, mytx);
	return (cookie);
}


/*
 * Send a chain of packets
 */

/* ARGSUSED */
mac_tx_cookie_t
mac_tx_chain(mac_client_handle_t mch, mblk_t *mp_chain, uintptr_t hint,
    uint16_t flags, mblk_t **ret_mp)
{
	mac_client_impl_t	*mcip = (mac_client_impl_t *)mch;
	mblk_t			*mp = mp_chain, *next;
	mac_tx_percpu_t		*mytx;

	/*
	 * Hold a reference to the mac client to prevent it from disappearing.
	 */
	if (mac_tx_try_hold(mcip, &mytx) != 0) {
		mac_client_drop(mcip, mp, TX_DROP);
		return (NULL);
	}
	do {
		next = mp->b_next;
		mp->b_next = NULL;

		/*
		 * Because the only client of this function does not
		 * implement flow control, we also do not support it here.
		 */
		(void) mac_tx_single(mch, mp, hint,
		    MAC_DROP_ON_NO_DESC, ret_mp, mytx);
		mp = next;

	} while (mp != NULL);

	mac_tx_rele(mcip, mytx);
	return (NULL);
}
/*
 * TX flow control operation
 *
 * 1. mac_tx() returns a cookie to IP's idd_tx_df.
 * 2. Before inserting the blocked conn_t to IP's drain list, IP verifies that
 *    the conn_t is indeed flow-controlled by calling mac_tx_is_blocked().
 * 3. If the conn_t is still blocked, it is inserted into IP's drain list.
 * 4. At some point later, the driver invokes mac_tx_ring_update(). This will
 *    wake up the ring's associated worker thread and this thread will drain
 *    any queued packets and unblock senders from IP by calling
 *    mac_tx_invoke_callbacks(), which will invoke ill_flowctl_enable() from IP.
 */

/*
 * Check if a ring is in flow control state.
 */
boolean_t
mac_tx_is_ring_blocked(mac_ring_handle_t rh)
{
	mac_ring_t		*ring = (mac_ring_t *)rh;
	boolean_t		client_blocked;

	/*
	 * The first field in the object indicates if it is a ring.
	 * If not, then it must be a flow.
	 */
	if (ring->mr_type != MAC_RING_TYPE_TX)
		return (mac_tx_is_flow_blocked(ring));

	mutex_enter(&ring->mr_lock);
	client_blocked = (ring->mr_tx_cnt >= mac_tx_lowat &&
	    (ring->mr_worker_state & MR_TX_WAITING) != 0);
	mutex_exit(&ring->mr_lock);
	return (client_blocked);
}

/*
 * Check if the mac client or the object specified by the cookie
 * (ring or flow) is in flow control state.
 */
boolean_t
mac_tx_is_blocked(mac_client_handle_t mch, mac_tx_cookie_t cookie)
{
	mac_client_impl_t	*mcip = (mac_client_impl_t *)mch;
	mac_tx_percpu_t		*mytx;
	mac_ring_handle_t	rh = (mac_ring_handle_t)cookie;
	boolean_t		blocked = B_FALSE;
	mac_group_t		*tx_group;

	/*
	 * Check whether the active Tx threads count is bumped already.
	 */
	if (mac_tx_try_hold(mcip, &mytx) != 0) {
		/*
		 * This special cookie is used for blocking all traffic during
		 * control operations.
		 */
		if ((mcip->mci_mip->mi_state_flags & MIS_TX_BLOCK) != 0 &&
		    cookie == (mac_tx_cookie_t)-1)
			return (B_TRUE);

		return (B_FALSE);
	}
	if (cookie == (mac_tx_cookie_t)-1)
		goto done;

	if (rh == NULL) {
		tx_group = mcip->mci_flent->fe_tx_ring_group;
		if (tx_group != NULL) {
			int			cnt;
			mac_rings_cache_t	*mrc;

			mrc = &tx_group->mrg_rings_cache[
			    MCIP_RESOURCE_PROPS_USRPRI(mcip)];
			for (cnt = 0; cnt < mrc->mrc_count; cnt++) {
				if (mac_tx_is_ring_blocked(
				    (mac_ring_handle_t)
				    mrc->mrc_rings[cnt])) {
					blocked = B_TRUE;
					goto done;
				}
			}
		} else {
			rh = mcip->mci_mip->mi_default_tx_ring;
		}
		if (mac_tx_is_flow_blocked(
		    &mcip->mci_flent->fe_bwctl_cookie))
			blocked = B_TRUE;
	}

	if (rh != NULL && mac_tx_is_ring_blocked(rh))
		blocked = B_TRUE;

done:
	mac_tx_rele(mcip, mytx);
	return (blocked);
}

/*
 * Once the driver is done draining, send a MAC_NOTE_TX notification to unleash
 * the blocked clients again.
 */
void
mac_tx_notify(mac_impl_t *mip)
{
	i_mac_notify(mip, MAC_NOTE_TX);
}

/*
 * A driver's notification to resume transmission, in case of a provider
 * without TX rings.
 */
void
mac_tx_update(mac_handle_t mh)
{
	mac_tx_ring_update(mh, NULL);
}

/*
 * A driver's notification to resume transmission on the specified TX ring.
 */
void
mac_tx_ring_update(mac_handle_t mh, mac_ring_handle_t rh)
{
	mac_ring_t	*ring = (mac_ring_t *)rh;

	if (ring == NULL)
		ring = &((mac_impl_t *)mh)->mi_fake_tx_ring;

	if (ring->mr_prh != NULL)
		ring = (mac_ring_t *)ring->mr_prh;

	mutex_enter(&ring->mr_lock);
	if ((ring->mr_worker_state & MR_TX_NODESC) != 0) {
		MR_TX_STAT(ring, driver_unblockcnt) += 1;
		cv_signal(&ring->mr_cv);
	}

	ring->mr_worker_state |= MR_TX_AWAKEN;
	mutex_exit(&ring->mr_lock);
}

/*
 * Invoke the registered TX callback of a mac client.
 * (e.g. IP's callback would cause blocked conn_t's to be drained)
 */
void
mac_tx_invoke_callbacks(mac_client_impl_t *mcip, mac_tx_cookie_t cookie)
{
	mac_cb_t		*mcb;
	mac_tx_notify_cb_t	*mtnfp;
	mac_impl_t		*mip;

	MAC_CALLBACK_WALKER_INC(&mcip->mci_tx_notify_cb_info);
	for (mcb = mcip->mci_tx_notify_cb_list; mcb != NULL;
	    mcb = mcb->mcb_nextp) {
		mtnfp = (mac_tx_notify_cb_t *)mcb->mcb_objp;
		mtnfp->mtnf_fn(mtnfp->mtnf_arg, cookie);
	}
	MAC_CALLBACK_WALKER_DCR(&mcip->mci_tx_notify_cb_info,
	    &mcip->mci_tx_notify_cb_list);

	mip = (mcip->mci_state_flags & MCIS_IS_VNIC) != 0 ?
	    mcip->mci_upper_mip : mcip->mci_mip;

	if (mip != NULL)
		mac_tx_notify(mip);
}

/*
 * Invoke the callbacks of all mac clients that own this particular TX ring.
 * The caller of this function must ensure that the mac clients list must not
 * change before calling this function.
 */
void
mac_tx_ring_wakeup(mac_ring_t *ring)
{
	mac_impl_t		*mip = ring->mr_mip;
	mac_client_impl_t	*mcip;

	if (ring == &mip->mi_fake_tx_ring) {
		for (mcip = mip->mi_clients_list; mcip != NULL;
		    mcip = mcip->mci_client_next) {
			mac_tx_invoke_callbacks(mcip, (mac_tx_cookie_t)ring);
		}
	} else {
		mac_group_t		*group;
		mac_grp_client_t	*mgcp;

		group = (mac_group_t *)ring->mr_gh;
		for (mgcp = group->mrg_clients; mgcp != NULL;
		    mgcp = mgcp->mgc_next) {
			mcip = mgcp->mgc_client;
			mac_tx_invoke_callbacks(mcip, (mac_tx_cookie_t)ring);
		}
	}
}

/*
 * TX ring worker
 */
void
mac_tx_ring_worker(mac_ring_t *ring)
{
	callb_cpr_t	cprinfo;
	kmutex_t	*lock = &ring->mr_lock;
	kcondvar_t	*cv = &ring->mr_cv;

	CALLB_CPR_INIT(&cprinfo, lock, callb_generic_cpr,
	    "mac_tx_ring_worker");
	mutex_enter(lock);

	while ((ring->mr_worker_state & MR_TX_READY) != 0) {
		boolean_t	driver_blocked;

		CALLB_CPR_SAFE_BEGIN(&cprinfo);
		cv_wait(cv, lock);
		CALLB_CPR_SAFE_END(&cprinfo, lock);
		if ((ring->mr_worker_state & MR_TX_BUSY) != 0)
			continue;

		/*
		 * MR_TX_NODESC flag is owned by the worker and is not
		 * manipulated elsewhere.
		 */
		ring->mr_worker_state &= ~(MR_TX_AWAKEN | MR_TX_NODESC);
		ring->mr_worker_state |= MR_TX_BUSY;

again:
		driver_blocked = mac_tx_ring_drain(ring, WORKER_DRAIN);
		ASSERT(ring->mr_tx_queue == NULL || driver_blocked);

		if (driver_blocked) {
			if ((ring->mr_worker_state & MR_TX_AWAKEN) != 0) {
				ring->mr_worker_state &= ~MR_TX_AWAKEN;
				goto again;
			}
			ring->mr_worker_state |= MR_TX_NODESC;
			ring->mr_worker_state &= ~MR_TX_BUSY;
			MR_TX_STAT(ring, driver_blockcnt) += 1;
			continue;
		}

		DTRACE_PROBE1(wait, mac_ring_t *, ring);

		if ((ring->mr_worker_state & MR_TX_WAITING) != 0) {
			boolean_t do_wakeup;

			ring->mr_worker_state &= ~MR_TX_WAITING;
			ring->mr_refcnt++;

			/*
			 * Do not do wakeup if we're in the middle of
			 * setting up / tearing down mac clients.
			 */
			do_wakeup = ((ring->mr_mip->mi_state_flags &
			    MIS_TX_BLOCK) == 0);
			mutex_exit(lock);

			if (do_wakeup) {
				MR_TX_STAT(ring, client_unblockcnt) += 1;
				mac_tx_ring_wakeup(ring);
			}

			mutex_enter(lock);
			ring->mr_refcnt--;
			if (ring->mr_refcnt == 0 &&
			    (ring->mr_flag & MR_QUIESCE) != 0) {
				cv_signal(&ring->mr_ref_cv);
			}
			if (ring->mr_tx_queue != NULL &&
			    (ring->mr_worker_state & MR_TX_READY) != 0)
				goto again;
		}

		ring->mr_worker_state &= ~MR_TX_BUSY;
		continue;
	}
	ring->mr_worker = NULL;
	ring->mr_worker_state &= ~MR_TX_BUSY;
	cv_signal(cv);

	CALLB_CPR_EXIT(&cprinfo);
	thread_exit();
}

/*
 * RX datapath
 */

/*
 * Pick a fanout value based on linkspeed and default parameters
 */
uint32_t
mac_default_rx_fanout_cnt(mac_client_impl_t *mcip)
{
	uint64_t	ifspeed;
	uint32_t 	rxfanout;

	ifspeed = mac_client_stat_get((mac_client_handle_t)mcip,
	    MAC_STAT_IFSPEED);

	if ((ifspeed/1000000) > 1000) {
		/* 10Gbps link */
		rxfanout = mac_rx_fanout_boost ?
		    mac_rx_fanout_10g_boost :
		    mac_rx_fanout_10g;

	} else {
		/* 1Gbps link */
		rxfanout = mac_rx_fanout_boost ?
		    mac_rx_fanout_1g_boost :
		    mac_rx_fanout_1g;
	}
	return (rxfanout);
}

static uint32_t
mac_rx_fanout_compute(mac_client_impl_t *mcip)
{
	mac_resource_props_t	*mrp = MCIP_EFFECTIVE_PROPS(mcip);
	uint32_t		erxfanout;
	uint32_t		navailcpus;

	/* If rxfanout is not set, assign default rxfanout */
	erxfanout = ((mrp->mrp_mask & MRP_RXFANOUT) != 0) ? mrp->mrp_rxfanout :
	    mac_default_rx_fanout_cnt(mcip);

	navailcpus = ((mrp->mrp_mask & MRP_CPUS) != 0) ?
	    mrp->mrp_cpus.mc_ncpus : ncpus;

	/* Limit rxfanout by the number of available cpus */
	if (navailcpus < erxfanout)
		erxfanout = navailcpus;

	return (erxfanout);
}

/*
 * Recompute fanout when the link speed of a NIC changes or when a
 * MAC client's share is unbound.
 */
void
mac_rx_fanout_recompute(mac_impl_t *mip)
{
	mac_client_impl_t	*mcip;

	i_mac_perim_enter(mip);

	if ((mip->mi_state_flags & MIS_IS_VNIC) != 0 ||
	    mip->mi_linkstate != LINK_STATE_UP) {
		i_mac_perim_exit(mip);
		return;
	}

	for (mcip = mip->mi_clients_list; mcip != NULL;
	    mcip = mcip->mci_client_next) {
		if ((mcip->mci_state_flags &
		    (MCIS_SHARE_BOUND | MCIS_NO_UNICAST_ADDR |
		    MCIS_EXCLUSIVE)) != 0 ||
		    !MCIP_DATAPATH_SETUP(mcip))
			continue;

		mac_rx_group_setup(mcip);
		mac_cpu_pool_setup(mcip);
	}
	i_mac_perim_exit(mip);
}

/*
 * Initialize one mac_rx_fanout_t structure
 */
static void
mac_rx_fanout_init_one(mac_client_impl_t *mcip, mac_rx_fanout_t *rf,
    uint_t hint)
{
	mutex_init(&rf->rf_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&rf->rf_cv, NULL, CV_DRIVER, NULL);
	cv_init(&rf->rf_wait_cv, NULL, CV_DRIVER, NULL);
	mac_initq(&rf->rf_ipq);
	mac_initq(&rf->rf_fullq);
	rf->rf_total_cnt = 0;
	rf->rf_xmit_hint = hint;
	rf->rf_mcip = mcip;
	rf->rf_ring = NULL;
	rf->rf_ring_gen = 0;
	ASSERT(rf->rf_sqinfo.mis_sqp == NULL);
	if (mcip->mci_resource_add != NULL)
		mac_rx_resource_add(mcip, rf);
	rf->rf_state = RF_READY;
	rf->rf_worker = thread_create(NULL, 0,
	    mac_rx_fanout_worker, rf, 0, &p0, TS_RUN, maxclsyspri);
	rf->rf_worker_obj = numaio_object_create_thread(rf->rf_worker,
	    "RX fout", 0);
}

/*
 * Uninitialize one mac_rx_fanout_t structure
 */
static void
mac_rx_fanout_fini_one(mac_client_impl_t *mcip, mac_rx_fanout_t *rf)
{
	mutex_enter(&rf->rf_lock);
	mac_flushq(&rf->rf_ipq);
	mac_flushq(&rf->rf_fullq);
	rf->rf_state = 0;
	rf->rf_total_cnt = 0;
	numaio_object_destroy(rf->rf_worker_obj);
	cv_signal(&rf->rf_cv);
	if (rf->rf_worker != NULL)
		cv_wait(&rf->rf_cv, &rf->rf_lock);
	mutex_exit(&rf->rf_lock);
	if (mcip->mci_resource_remove != NULL &&
	    rf->rf_sqinfo.mis_sqp != NULL) {
		mac_rx_resource_remove(mcip, rf);
	}
	ASSERT(rf->rf_sqinfo.mis_sqp == NULL);
	rf->rf_mcip = NULL;
	rf->rf_ring = NULL;
	rf->rf_ring_gen = 0;
	mutex_destroy(&rf->rf_lock);
	cv_destroy(&rf->rf_cv);
	cv_destroy(&rf->rf_wait_cv);
}

/*
 * Destroy the array of rx fanout workers
 */
void
mac_rx_fanout_fini(mac_client_impl_t *mcip)
{
	int	i;

	ASSERT(mcip->mci_rx_fanout != NULL);
	mcip->mci_flent->fe_cb_fn = mac_rx_deliver;
	for (i = 0; i < mcip->mci_rx_fanout_cnt; i++)
		mac_rx_fanout_fini_one(mcip, &mcip->mci_rx_fanout[i]);

	kmem_free(mcip->mci_rx_fanout, sizeof (mac_rx_fanout_t) *
	    mcip->mci_rx_fanout_cnt);
	mcip->mci_rx_fanout = NULL;
	mcip->mci_rx_fanout_cnt = 0;
	mcip->mci_rx_fanout_cnt_per_ring = 0;
}

/*
 * Create the array of rx fanout workers
 * If the array already exists it will be torn down first.
 */
void
mac_rx_fanout_init(mac_client_impl_t *mcip)
{
	uint_t		per_ring_cnt, total_rings, fanout_cnt, idx, i, j;
	uint_t		rx_rings = 0;
	mac_group_t	*rx_group;

	if (mcip->mci_rx_fanout != NULL)
		mac_rx_fanout_fini(mcip);

	fanout_cnt = mac_rx_fanout_compute(mcip);

	if ((rx_group = mcip->mci_flent->fe_rx_ring_group) != NULL)
		rx_rings = rx_group->mrg_cur_count;

	total_rings = rx_rings;
	if (total_rings == 0)
		total_rings = 1;

	per_ring_cnt = fanout_cnt/total_rings;
	if (per_ring_cnt == 0)
		per_ring_cnt++;
	if (per_ring_cnt > MAX_FANOUT_SUBSET_SZ)
		per_ring_cnt = MAX_FANOUT_SUBSET_SZ;

	fanout_cnt = per_ring_cnt * total_rings;
	mcip->mci_rx_fanout_cnt_per_ring = per_ring_cnt;
	mcip->mci_rx_fanout_cnt = fanout_cnt;
	mcip->mci_rx_fanout = kmem_zalloc(sizeof (mac_rx_fanout_t) *
	    fanout_cnt, KM_SLEEP);

	for (i = 0; i < total_rings; i++) {
		for (j = 0; j < per_ring_cnt; j++) {
			idx = i * per_ring_cnt + j;
			mac_rx_fanout_init_one(mcip,
			    &mcip->mci_rx_fanout[idx], idx);
		}
	}
	mcip->mci_flent->fe_cb_fn = mac_rx_deliver_fanout;
}

/*
 * Enqueue the packet onto the ipq (with L2 header stripped) or the
 * fullq (with L2 header intact) depending on the attributes of the
 * packet.
 */
static void
mac_rx_enqueue(mac_client_impl_t *mcip, mac_ring_t *ring,
    boolean_t bypass, mac_pkt_info_t *pkt_infop, mac_queue_t *ipq,
    mac_queue_t *fullq, mblk_t *mp)
{
	mac_impl_t		*mip = mcip->mci_mip;
	mac_pkt_info_t		info, *infop;
	boolean_t		is_unicast;
	uint_t			media;
	uint32_t		sap;
	uint16_t		vid;
	size_t			hdrlen = 0;
	mac_queue_t		*q = fullq;
	struct ether_header	*ehp = (struct ether_header *)mp->b_rptr;

	if (!bypass)
		goto full;

	if (!IS_P2ALIGNED(ehp, sizeof (uint16_t)) || MBLKL(mp) < sizeof (*ehp))
		goto drop;

	media = mip->mi_info.mi_nativemedia;
	if (media == DL_ETHER &&
	    (sap = ntohs(ehp->ether_type)) == ETHERTYPE_IP) {
		hdrlen = sizeof (struct ether_header);
		vid = 0;
		is_unicast = ((((uint8_t *)&ehp->ether_dhost)[0] & 0x01) == 0);
	} else {
		/*
		 * We only support bypass for media types with the same
		 * sap space as ethernet.
		 */
		if (media != DL_ETHER && media != DL_IB && media != DL_WIFI)
			goto full;

		infop = pkt_infop ? pkt_infop : &info;
		if (pkt_infop != NULL ||
		    mac_pkt_get_info((mac_handle_t)mip, mp, infop) == 0) {
			hdrlen = infop->pi_hdrlen;
			vid = infop->pi_vid;
			sap = infop->pi_sap;
			is_unicast = infop->pi_unicast;
		} else {
			goto full;
		}
	}
	if (sap != ETHERTYPE_IP || !is_unicast)
		goto full;

	if (vid != 0 && !mac_client_check_flow_vid(mcip, vid))
		goto drop;

	mp->b_rptr += hdrlen;
	if (mp->b_rptr == mp->b_wptr) {
		if (mp->b_cont == NULL) {
			goto drop;
		} else {
			mblk_t	*mp1 = mp;
			mp = mp->b_cont;
			freeb(mp1);
		}
	}
	q = ipq;
	goto enqueue;

full:
	if (mcip->mci_nvids == 1 &&
	    !(mcip->mci_state_flags & MCIS_STRIP_DISABLE)) {
		mp = mac_strip_vlan_tag(mp);
		if (mp == NULL)
			goto drop;
	}

enqueue:
	*q->mq_tailp = mp;
	q->mq_tailp = &mp->b_next;
	q->mq_cnt++;
	return;

drop:
	DTRACE_PROBE1(rx__enqueue__drop, mac_ring_t *, ring);
	mac_client_drop(mcip, mp, RX_DROP);
}

static void
mac_poll_worker_wakeup(mac_ring_t *ring)
{
	mutex_enter(&ring->mr_lock);
	ring->mr_poll_pending = B_FALSE;
	cv_signal(&ring->mr_cv);
	mutex_exit(&ring->mr_lock);
}

static void
mac_poll_ring_enable(mac_ring_t *ring)
{
	mutex_enter(&ring->mr_lock);
	if ((ring->mr_worker_state & MR_RX_POLLING) == 0) {
		ring->mr_worker_state |= MR_RX_POLLING;
		(void) mac_hwring_disable_intr((mac_ring_handle_t)ring);
	}
	mutex_exit(&ring->mr_lock);
}

static void
mac_poll_estimate_init(mac_ring_t *ring)
{
	ring->mr_poll_gen_saved = ++ring->mr_poll_gen;
	ring->mr_poll_pending = B_FALSE;
	ring->mr_poll_estimate = 0;
}

/*
 * Poll worker thread
 */
void
mac_poll_worker(mac_ring_t *ring)
{
	callb_cpr_t	cprinfo;
	kmutex_t	*lock = &ring->mr_lock;
	kcondvar_t	*cv = &ring->mr_cv;
	mac_ring_info_t	*info = &ring->mr_info;
	boolean_t	abort = B_FALSE, pending = B_FALSE;
	mblk_t		*mp_chain;

	CALLB_CPR_INIT(&cprinfo, lock, callb_generic_cpr,
	    "mac_poll_worker");
	mutex_enter(lock);

	while ((ring->mr_worker_state & MR_RX_READY) != 0) {
		CALLB_CPR_SAFE_BEGIN(&cprinfo);
		if (pending) {
			/*
			 * If there are packets in flight, we wait up to
			 * mac_poll_delay nsecs before continuing to get
			 * more packets. This puts a bound on the time
			 * we wait in-between getting new packets and
			 * helps reduce packet drops.
			 */
			if (cv_timedwait_hires(cv, lock, mac_poll_delay,
			    MICROSEC, 0) < 0) {
				DTRACE_PROBE1(poll__timedout,
				    mac_ring_t *, ring);
			}
		} else {
			cv_wait(cv, lock);
		}
		CALLB_CPR_SAFE_END(&cprinfo, lock);
		if ((ring->mr_worker_state & MR_RX_POLLING) == 0)
			continue;
		mutex_exit(lock);
again:
		mp_chain = info->mri_poll(info->mri_driver,
		    mac_poll_bytes_default, mac_poll_pkts_default);
		if (mp_chain != NULL) {
			mac_rx_common((mac_handle_t)ring->mr_mip,
			    (mac_ring_handle_t)ring, mp_chain, MAC_RX_POLL);

			if ((ring->mr_worker_state & MR_RX_READY) == 0) {
				abort = B_TRUE;
			} else {
				if (!ring->mr_poll_pending || mac_poll_loop)
					goto again;
			}
		}
		mutex_enter(lock);
		pending = ring->mr_poll_pending;
		if (!pending || abort) {
			pending = abort = B_FALSE;
			ring->mr_worker_state &= ~MR_RX_POLLING;
			ring->mr_poll_gen++;
			ring->mr_poll_pending = B_FALSE;
			(void) mac_hwring_enable_intr((mac_ring_handle_t)ring);
		}
		/*
		 * if there are in-flight packets, we leave interrupts disabled
		 * and let one of the fanout threads wake us up to resume
		 * getting new packets. The goal is to delay re-enabling
		 * interrupts as much as possible and at the same time avoid
		 * dropping packets due to leaving them in the hardware for too
		 * long.
		 */
	}
	ring->mr_worker = NULL;
	cv_signal(cv);

	CALLB_CPR_EXIT(&cprinfo);
	thread_exit();
}

static void
mac_rx_stats_update(mac_client_impl_t *mcip, mac_ring_t *ring, uint_t sz,
    uint_t flags)
{
	if ((flags & (MAC_RX_MULTICAST | MAC_RX_BROADCAST)) != 0) {
		if ((flags & MAC_RX_MULTICAST) != 0) {
			MCIP_MISC_STAT(mcip, multircvbytes) += sz;
			MCIP_MISC_STAT(mcip, multircv) += 1;
		} else if ((flags & MAC_RX_BROADCAST) != 0) {
			MCIP_MISC_STAT(mcip, brdcstrcvbytes) += sz;
			MCIP_MISC_STAT(mcip, brdcstrcv) += 1;
		}
	} else {
		/* Don't count multicast/broadcast traffic again */
		if (ring != NULL) {
			if ((flags & MAC_RX_INTR) != 0) {
				MR_RX_STAT(ring, intrbytes) += sz;
				MR_RX_STAT(ring, intrcnt) += 1;

				MCIP_RX_STAT(mcip, intrbytes) += sz;
				MCIP_RX_STAT(mcip, intrcnt) += 1;
			} else if ((flags & MAC_RX_POLL) != 0) {
				MR_RX_STAT(ring, pollbytes) += sz;
				MR_RX_STAT(ring, pollcnt) += 1;

				MCIP_RX_STAT(mcip, pollbytes) += sz;
				MCIP_RX_STAT(mcip, pollcnt) += 1;
			}
		} else  {
			if ((flags & MAC_RX_LOOPBACK) != 0) {
				MCIP_MISC_STAT(mcip, rxlocalbytes) += sz;
				MCIP_MISC_STAT(mcip, rxlocalcnt) += 1;
			} else {
				/*
				 * For drivers that don't support rings, all the
				 * traffic is received by the interrupt path.
				 */
				MCIP_RX_STAT(mcip, intrbytes) += sz;
				MCIP_RX_STAT(mcip, intrcnt) += 1;
			}
		}
	}
}

/*
 * All inbound packets must go through this function.
 * Additional hooks may be placed in the future for other types
 * of processing.
 */
static mblk_t *
mac_rx_process(mac_client_impl_t *mcip, mac_ring_t *ring, mblk_t *mp,
    uint_t flags)
{
	uint_t	sz = mac_pktsize(mp);

	if (mcip->mci_promisc_list != NULL)
		mac_promisc_client_dispatch(mcip, mp);

	mac_rx_stats_update(mcip, ring, sz, flags);

	if (mcip->mci_feature != 0) {
		if ((mcip->mci_feature & MAC_PROTECT) != 0 &&
		    MAC_PROTECT_ENABLED(mcip, MPT_IPNOSPOOF)) {
			mac_protect_intercept_dhcp(mcip, mp);
		}
		if ((mcip->mci_feature &
		    (MAC_BWCTL_LINK | MAC_BWCTL_FLOW)) != 0) {
			mp = mac_bwctl_rx_check(mcip, mp);
			if (mp == NULL) {
				mac_client_drop_stats_update(mcip, sz,
				    1, RX_DROP);
			}
		}
	}
	return (mp);
}

/*
 * Send packets up to IP or DLS.
 */
/* ARGSUSED */
static void
mac_rx_sendup(mac_client_impl_t *mcip, mblk_t *ipq_chain,
    mblk_t *fullq_chain, mac_rx_attr_t *attrp, uint_t flags)
{
	if (ipq_chain != NULL) {
		mcip->mci_direct_rx_fn(mcip->mci_direct_rx_arg,
		    attrp, ipq_chain, NULL);
	}
	if (fullq_chain != NULL) {
		mcip->mci_rx_fn(mcip->mci_rx_arg, NULL,
		    fullq_chain, B_FALSE);
	}
}

static void
mac_rx_fanout_wait_one(mac_rx_fanout_t *rf)
{
	mutex_enter(&rf->rf_lock);
	mac_flushq(&rf->rf_ipq);
	mac_flushq(&rf->rf_fullq);
	rf->rf_total_cnt = 0;
	rf->rf_state |= RF_QUIESCE;
	while ((rf->rf_state & RF_BUSY) != 0)
		cv_wait(&rf->rf_wait_cv, &rf->rf_lock);
	rf->rf_state &= ~RF_QUIESCE;
	mutex_exit(&rf->rf_lock);
}

void
mac_rx_fanout_wait(mac_client_impl_t *mcip)
{
	uint_t	i;

	for (i = 0; i < mcip->mci_rx_fanout_cnt; i++)
		mac_rx_fanout_wait_one(&mcip->mci_rx_fanout[i]);
}

static void
mac_rx_fanout_worker(mac_rx_fanout_t *rf)
{
	callb_cpr_t		cprinfo;
	kmutex_t		*lock = &rf->rf_lock;
	kcondvar_t		*cv = &rf->rf_cv;
	mac_client_impl_t	*mcip = rf->rf_mcip;
	mblk_t			*ipq_chain, *fullq_chain;
	mac_ring_t		*ring;
	mac_rx_attr_t		attr;
	uint_t			total, gen;

	CALLB_CPR_INIT(&cprinfo, lock, callb_generic_cpr,
	    "mac_rx_fanout_worker");
	mutex_enter(lock);

	while ((rf->rf_state & RF_READY) != 0) {
		CALLB_CPR_SAFE_BEGIN(&cprinfo);
		cv_wait(cv, lock);
		CALLB_CPR_SAFE_END(&cprinfo, lock);
		rf->rf_state |= RF_BUSY;
again:
		total = rf->rf_total_cnt;
		ipq_chain = rf->rf_ipq.mq_head;
		fullq_chain = rf->rf_fullq.mq_head;
		mac_initq(&rf->rf_ipq);
		mac_initq(&rf->rf_fullq);

		ring = rf->rf_ring;
		rf->rf_ring = NULL;
		gen = rf->rf_ring_gen;

		attr.ra_sqp = rf->rf_sqinfo.mis_sqp;
		attr.ra_xmit_hint = rf->rf_xmit_hint;
		mutex_exit(lock);

		mac_rx_sendup(mcip, ipq_chain, fullq_chain, &attr, 0);
		if (ring != NULL && ring->mr_poll_gen == gen)
			mac_poll_worker_wakeup(ring);

		mutex_enter(lock);
		if (rf->rf_total_cnt > 0)
			rf->rf_total_cnt -= total;

		if ((rf->rf_state & RF_QUIESCE) != 0)
			cv_signal(&rf->rf_wait_cv);

		if (rf->rf_total_cnt == 0 ||
		    (rf->rf_state & RF_READY) == 0) {
			rf->rf_state &= ~RF_BUSY;
			continue;
		} else {
			goto again;
		}
	}
	rf->rf_worker = NULL;
	cv_signal(cv);

	CALLB_CPR_EXIT(&cprinfo);
	thread_exit();
}

/*
 * Process the accumulated packets for this particular fanout queue.
 * We could either pass up the packets in the caller's context or
 * enqueue the packets and let the fanout thread process them.
 */
static void
mac_rx_process_fanout(mac_client_impl_t *mcip, mac_ring_t *ring,
    mac_rx_fanout_t *rf, fanout_data_t *fd, uint_t flags)
{
	uint_t		total;
	mac_rx_attr_t	attr;

	total = fd->fd_ipq.mq_cnt + fd->fd_fullq.mq_cnt;
	if (total == 0)
		return;

	mutex_enter(&rf->rf_lock);
	if ((rf->rf_state & RF_BUSY) == 0 && rf->rf_total_cnt == 0 &&
	    total <= mcip->mci_tunables.mt_rx_fanout_inline_max &&
	    (flags & MAC_RX_LOOPBACK) == 0) {
		attr.ra_sqp = rf->rf_sqinfo.mis_sqp;
		attr.ra_xmit_hint = rf->rf_xmit_hint;
		mutex_exit(&rf->rf_lock);
		mac_rx_sendup(mcip, fd->fd_ipq.mq_head,
		    fd->fd_fullq.mq_head, &attr, flags);
		return;
	}
	if (rf->rf_total_cnt > mac_rx_fanout_hiwat) {
		mutex_exit(&rf->rf_lock);
		DTRACE_PROBE1(rx__fanout__drop, mac_ring_t *, ring);
		mac_flushq(&fd->fd_ipq);
		mac_flushq(&fd->fd_fullq);
		return;
	}
	mac_enq(&rf->rf_ipq, &fd->fd_ipq);
	mac_enq(&rf->rf_fullq, &fd->fd_fullq);
	rf->rf_total_cnt += total;

	/*
	 * Decide whether we should turn polling on or keep it on by setting
	 * the mr_poll_pending flag. The mr_poll_pending flag tells the poll
	 * thread to hold off on re-enabling interrupts even if it runs out
	 * of packets to get from the wire.
	 */
	if (ring != NULL && mac_poll_enable &&
	    ring->mr_poll_gen_saved == ring->mr_poll_gen &&
	    !ring->mr_poll_pending) {
		ring->mr_poll_estimate += total;
		if (ring->mr_poll_estimate > mac_poll_threshold &&
		    rf->rf_total_cnt < mac_poll_max_backlog) {
			ring->mr_poll_pending = B_TRUE;
			rf->rf_ring_gen = ++ring->mr_poll_gen;
			rf->rf_ring = ring;
			if ((flags & MAC_RX_INTR) != 0)
				mac_poll_ring_enable(ring);
		}
	}
	if ((rf->rf_state & RF_BUSY) == 0)
		cv_signal(&rf->rf_cv);
	mutex_exit(&rf->rf_lock);
}

/*
 * The purpose of this function is to distribute packets amongst the configured
 * RX fanout threads. Each mac client has an array of mac_rx_fanout_t's. Each
 * mac_rx_fanout_t contains the worker thread ID and two queues. One for packets
 * going to IP (with L2 header stripped) and another for packets going to DLS
 * (with L2 header intact). The mci_rx_fanout array is partitioned as follows:
 * Suppose the array size is 8 and there exists 2 rx rings,
 * [ 0 1 2 3 4 5 6 7 ] mac_rx_fanout_t
 * [   0   ] [   1   ] mac_ring_t
 * rx ring mr_gindex 'i' will make use of fanout entries (i * subset_size) to
 * (i * subset_size + subset_size - 1).
 * The mr_gindex 'i' is the index of the ring within its RX group. This is
 * not the index of the ring within the whole mac_impl_t (mr_index).
 * Details about how the subset_size is derived and how it affects the fanout
 * array construction can be found in mac_rx_fanout_init() above.
 */
void
mac_rx_deliver_fanout(void *arg1, void *arg2, mblk_t *mp_chain, uint_t flags)
{
	mac_client_impl_t	*mcip = arg1;
	mac_impl_t		*mip = mcip->mci_mip;
	mac_ring_t		*ring = arg2;
	mblk_t			*mp, *next;
	mac_pkt_info_t		pkt_info, *infop;
	mac_rx_fanout_t		*subset;
	uint_t			subset_start, subset_sz, i;
	boolean_t		bypass_ok, bypass;
	fanout_data_t		*fd, subset_data[MAX_FANOUT_SUBSET_SZ];
	mac_queue_t		*ipqp, *fullqp;
	uint16_t		hash;

	bypass_ok = ((mcip->mci_state_flags & MCIS_RX_BYPASS_OK) != 0 &&
	    (mcip->mci_state_flags & MCIS_RX_BYPASS_DISABLE) == 0 &&
	    mip->mi_devpromisc == 0);

	if ((flags & MAC_RX_LOOPBACK) == 0) {
		subset_sz = mcip->mci_rx_fanout_cnt_per_ring;
		ASSERT(subset_sz <= MAX_FANOUT_SUBSET_SZ);

		i = (ring != NULL) ? (uint_t)ring->mr_gindex : 0;
		subset_start = i * subset_sz;
		if (subset_start + subset_sz > mcip->mci_rx_fanout_cnt)
			subset_start = 0;
	} else {
		subset_sz = mcip->mci_rx_fanout_cnt;
		if (subset_sz > MAX_FANOUT_SUBSET_SZ)
			subset_sz = MAX_FANOUT_SUBSET_SZ;
		subset_start = 0;
	}
	subset = &mcip->mci_rx_fanout[subset_start];

	for (i = 0; i < subset_sz; i++)
		subset_data[i].fd_flags = 0;

	for (mp = mp_chain; mp != NULL; mp = next) {
		next = mp->b_next;
		mp->b_next = NULL;

		mp = mac_rx_process(mcip, ring, mp, flags);
		if (mp == NULL)
			continue;

		infop = NULL;
		bypass = bypass_ok;

		/*
		 * Obtain the hash from the hardware or generate one using
		 * mac_pkt_hash(). The modulo 251 (largest 8bit prime) has
		 * an additional bit-shuffle effect. This is useful if the
		 * subset size is small (e.g. 2).
		 */
		if ((DB_CKSUMFLAGS(mp) & HW_HASH) != 0) {
			hash = DB_HASH(mp);
			hash %= 251;
		} else {
			uint32_t	hash32;

			hash32 = mac_pkt_hash((mac_handle_t)mip, mp,
			    MAC_PKT_HASH_L4, &pkt_info);
			if (hash32 != 0)
				infop = &pkt_info;

			hash = (uint16_t)(hash32 % 251);
		}
		if (hash == 0)
			bypass = B_FALSE;

		fd = &subset_data[hash % subset_sz];
		if (fd->fd_flags == 0) {
			mac_initq(&fd->fd_ipq);
			mac_initq(&fd->fd_fullq);
			fd->fd_flags |= FD_NEWDATA;
		}
		ipqp = &fd->fd_ipq;
		fullqp = &fd->fd_fullq;
		mac_rx_enqueue(mcip, ring, bypass, infop, ipqp, fullqp, mp);
	}
	for (i = 0; i < subset_sz; i++) {
		fd = &subset_data[i];
		if ((fd->fd_flags & FD_NEWDATA) != 0) {
			mac_rx_process_fanout(mcip, ring, &subset[i],
			    fd, flags);
		}
	}
}

void
mac_rx_deliver(void *arg1, void *arg2, mblk_t *mp_chain, uint_t flags)
{
	mac_client_impl_t	*mcip = arg1;
	mac_impl_t		*mip = mcip->mci_mip;
	mac_ring_t		*ring = arg2;
	mblk_t			*mp, *next;
	boolean_t		bypass;
	mac_queue_t		ipq, fullq;

	bypass = ((mcip->mci_state_flags & MCIS_RX_BYPASS_OK) != 0 &&
	    (mcip->mci_state_flags & MCIS_RX_BYPASS_DISABLE) == 0 &&
	    mip->mi_devpromisc == 0);

	mac_initq(&ipq);
	mac_initq(&fullq);

	for (mp = mp_chain; mp != NULL; mp = next) {
		next = mp->b_next;
		mp->b_next = NULL;

		mp = mac_rx_process(mcip, ring, mp, flags);
		if (mp == NULL)
			continue;

		mac_rx_enqueue(mcip, ring, bypass, NULL, &ipq, &fullq, mp);
	}
	mac_rx_sendup(mcip, ipq.mq_head, fullq.mq_head, NULL, flags);
}

static flow_entry_t *
mac_rx_classify(mac_impl_t *mip, mblk_t *mp)
{
	flow_entry_t		*flent = NULL;
	uint_t			flags = FLOW_INBOUND;
	mac_client_impl_t	*mcip;
	int			err;

	/*
	 * If the mac is a port of an aggregation, pass FLOW_IGNORE_VLAN
	 * to mac_flow_lookup() so that the VLAN packets can be successfully
	 * passed to the non-VLAN aggregation flows.
	 *
	 * Note that there is possibly a race between this and
	 * mac_unicast_remove/add() and VLAN packets could be incorrectly
	 * classified to non-VLAN flows of non-aggregation mac clients. These
	 * VLAN packets will be then filtered out by the mac module.
	 */
	if ((mip->mi_state_flags & MIS_EXCLUSIVE) != 0)
		flags |= FLOW_IGNORE_VLAN;

	err = mac_flow_lookup(mip->mi_flow_tab, mp, flags, &flent);
	if (err != 0)
		return (NULL);

	/*
	 * This flent might just be an additional one on the MAC client,
	 * i.e. for classification purposes (different fdesc), however
	 * the resources are in the mci_flent, so if this isn't the
	 * mci_flent, we need to get it.
	 */
	if ((mcip = flent->fe_mcip) != NULL && mcip->mci_flent != flent) {
		FLOW_REFRELE(flent);
		flent = mcip->mci_flent;
		FLOW_TRY_REFHOLD(flent, err);
		if (err != 0)
			return (NULL);
	}
	return (flent);
}

/*
 * This is the upward reentry point for packets arriving from the bridging
 * module and from mac_rx for links not part of a bridge.
 */
void
mac_rx_private(mac_handle_t mh, mac_resource_handle_t mrh, mblk_t *mp_chain)
{
	mac_rx_common(mh, (mac_ring_handle_t)mrh, mp_chain, MAC_RX_INTR);
}

void
mac_rx_sw_classified(mac_impl_t *mip, mac_ring_t *ring, mblk_t *mp_chain,
    uint_t flags)
{
	mblk_t		*mp, *next, *head, **tailp;
	flow_entry_t	*flent, *prev_flent = NULL;

	if (FLOW_TAB_EMPTY(mip->mi_flow_tab)) {
		mac_ring_drop(ring, mp_chain, RX_DROP);
		return;
	}
	mp = mp_chain;
	head = NULL;
	tailp = &head;
	do {
		next = mp->b_next;
		mp->b_next = NULL;

		/* Construct the sub-chain */
		flent = mac_rx_classify(mip, mp);
		if (flent == NULL) {
			mac_ring_drop(ring, mp, RX_DROP);
		} else {
			if (flent == prev_flent) {
				FLOW_REFRELE(prev_flent);
			} else {
				if (prev_flent != NULL) {
					prev_flent->fe_cb_fn(
					    prev_flent->fe_cb_arg1, ring, head,
					    flags | MAC_RX_SW);
					FLOW_REFRELE(prev_flent);
					head = NULL;
					tailp = &head;
				}
				prev_flent = flent;
			}
			*tailp = mp;
			tailp = &mp->b_next;
		}
		mp = next;
	} while (mp != NULL);

	/* The last sub-chain if any */
	if (head != NULL) {
		prev_flent->fe_cb_fn(prev_flent->fe_cb_arg1, ring,
		    head, flags | MAC_RX_SW);
		FLOW_REFRELE(prev_flent);
	}
}

/*
 * This is the common function shared by the interrupt, poll, and bridge
 * RX code paths.
 */
void
mac_rx_common(mac_handle_t mh, mac_ring_handle_t mrh, mblk_t *mp_chain,
    uint_t flags)
{
	mac_impl_t		*mip = (mac_impl_t *)mh;
	mac_ring_t		*ring = (mac_ring_t *)mrh;
	flow_entry_t		*flent;

	/*
	 * If there are any promiscuous mode callbacks defined for
	 * this MAC, pass them a copy if appropriate.
	 */
	if (mip->mi_promisc_list != NULL)
		mac_promisc_dispatch(mip, mp_chain, NULL);

	if (ring != NULL) {
		mac_group_t	*group;

		/*
		 * If the ring teardown has started, just return. The 'ring'
		 * continues to be valid until the driver unregisters the mac.
		 * Hardware classified packets will not make their way up
		 * beyond this point once the teardown has started. The driver
		 * never passes a pointer to a flow entry or any structure
		 * that can be freed much before mac_unregister.
		 */
		mutex_enter(&ring->mr_lock);
		if ((ring->mr_state != MR_INUSE) ||
		    (ring->mr_flag & MR_BUSY) != 0 ||
		    (group = (mac_group_t *)ring->mr_gh) == NULL) {
			mutex_exit(&ring->mr_lock);
			DTRACE_PROBE2(ring__drop, mac_ring_t *, ring,
			    uint_t, ring->mr_flag);
			mac_ring_drop(ring, mp_chain, RX_DROP);
			return;
		}
		MR_REFHOLD_LOCKED(ring);
		mac_poll_estimate_init(ring);

		flent = ((mip->mi_state_flags & MIS_RX_BLOCK) == 0 &&
		    (group->mrg_flags & MRG_QUIESCE) == 0) ?
		    group->mrg_flent : NULL;
		mutex_exit(&ring->mr_lock);

		if (flent != NULL) {
			DTRACE_PROBE2(hw__classified, flow_entry_t *, flent,
			    mblk_t *, mp_chain);

			flent->fe_cb_fn(flent->fe_cb_arg1, ring, mp_chain,
			    flags | MAC_RX_HW);
			MR_REFRELE(ring);
			return;
		}
		/* We'll fall through to software classification */
	} else {
		int	err = -1;

		rw_enter(&mip->mi_rw_lock, RW_READER);
		if (mip->mi_single_active_client != NULL) {
			flent = mip->mi_single_active_client->mci_flent_list;
			FLOW_TRY_REFHOLD(flent, err);
		}
		rw_exit(&mip->mi_rw_lock);
		if (err == 0) {
			flent->fe_cb_fn(flent->fe_cb_arg1, NULL,
			    mp_chain, flags | MAC_RX_SW);
			FLOW_REFRELE(flent);
			return;
		}
	}
	mac_rx_sw_classified(mip, ring, mp_chain, flags);
	if (ring != NULL)
		MR_REFRELE(ring);
}

/*
 * This function is invoked for packets received by the MAC driver in
 * interrupt context. The ring generation number provided by the driver
 * is matched with the ring generation number held in MAC. If they do not
 * match, received packets are considered stale packets coming from an older
 * assignment of the ring. Drop them.
 */
void
mac_rx_ring(mac_handle_t mh, mac_ring_handle_t mrh, mblk_t *mp_chain,
    uint64_t mr_gen_num)
{
	mac_ring_t	*ring = (mac_ring_t *)mrh;
	mac_impl_t	*mip = (mac_impl_t *)mh;

	if ((ring != NULL) && (ring->mr_gen_num != mr_gen_num)) {
		DTRACE_PROBE2(mac__rx__rings__stale__packet, uint64_t,
		    ring->mr_gen_num, uint64_t, mr_gen_num);
		mac_ring_drop(ring, mp_chain, RX_DROP);
		return;
	}
	mip->mi_rx(mh, mrh, mp_chain, MAC_RX_INTR);
}

/*
 * This function is invoked for each packet received by the underlying driver.
 */
void
mac_rx(mac_handle_t mh, mac_resource_handle_t mrh, mblk_t *mp_chain)
{
	mac_rx_ring(mh, (mac_ring_handle_t)mrh, mp_chain, 0);
}

/* ARGSUSED */
void
mac_rx_bridge(mac_handle_t mh, mac_ring_handle_t mrh, mblk_t *mp_chain,
    uint_t flags)
{
	mac_impl_t *mip = (mac_impl_t *)mh;

	/*
	 * Once we take a reference on the bridge link, the bridge
	 * module itself can't unload, so the callback pointers are
	 * stable.
	 */
	mutex_enter(&mip->mi_bridge_lock);
	if ((mh = mip->mi_bridge_link) != NULL)
		mac_bridge_ref_cb(mh, B_TRUE);
	mutex_exit(&mip->mi_bridge_lock);
	if (mh == NULL) {
		mac_rx_common((mac_handle_t)mip, mrh, mp_chain, MAC_RX_INTR);
	} else {
		mac_bridge_rx_cb(mh, (mac_resource_handle_t)mrh, mp_chain);
		mac_bridge_ref_cb(mh, B_FALSE);
	}
}
