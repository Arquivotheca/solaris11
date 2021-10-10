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
 * Copyright (c) 2011, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/mac_client_impl.h>
#include <sys/callb.h>

/*
 * Token buckets are used for enforcing bandwidth limits. Each flow_entry_t has
 * two token buckets. One for RX and one for TX. Each token bucket keeps the
 * following state info:
 *
 * bw_max (maximum burst size, in bytes)
 * bw_avail (currently available bandwidth, in bytes)
 * bw_per_tick (# bytes replenished on every 10ms)
 * bw_lowat (threshold to start replenishing bandwidth)
 * bw_block (threshold to start throttling)
 * bw_unblock (threshold to stop throttling)
 *
 * Initially, the buckets are full (bw_max == bw_avail) for both RX/TX.
 * Whenever bandwidth is consumed, bw_avail is decremented by the packet size.
 * If bw_avail drops below a certain low watermark, the owning flow entry for
 * this token bucket will get added to a list and a worker thread will be woken
 * to replenish tokens on all flows on this list. Every time this thread runs
 * (every 10ms), it pulls all flows off the list and replenishes each token
 * bucket by the configured bw_per_tick. If both the RX/TX token buckets become
 * full, the flow will not be re-added back to the list. Otherwise, the flow
 * will be re-added and more replenishing will happen at the next interval.
 *
 * To support multi-level bandwidth limits (for now, only 2), the above logic
 * is simply cascaded (i.e. user flows enforcement happens before the
 * underlying link's flow).
 */
static void	mac_tx_flow_stats_update(flow_entry_t *, uint_t, boolean_t);
static void	mac_rx_flow_stats_update(flow_entry_t *, uint_t, boolean_t);

uint64_t	mac_bwctl_min_depth = 65536;
uint64_t	mac_bwctl_lowat_ratio = 4;
uint64_t	mac_bwctl_unblock_ratio = 2;
uint64_t	mac_bwctl_block_ratio = 8;
uint64_t	mac_bwctl_burst = 100;

/*
 * Flags passed to mac_bwctl_consume()
 * BW_TX: consume tx bandwidth
 * BW_RX: consume rx bandwidth
 * BW_CB: invoke a callback when bandwidth is replenished.
 *        currently, only TX is making use of this.
 */
#define	BW_TX			0x01
#define	BW_RX			0x02
#define	BW_CB			0x04

/*
 * Status flags returned by mac_bwctl_state_update()
 * BW_DONE: finished replenishing bucket
 * BW_UNBLOCK: invoke callback if one exists
 */
#define	BW_DONE			0x01
#define	BW_UNBLOCK		0x02

/*
 * Status flags returned by mac_bwctl_consume()
 * BW_DROP: drop packet
 * BW_FLOW_BLOCKED: initiate flow-control for this flow
 * BW_LINK_BLOCKED: initiate flow-control for this link
 */
#define	BW_DROP			0x01
#define	BW_FLOW_BLOCKED		0x02
#define	BW_LINK_BLOCKED		0x04

static void	mac_bwctl_unblock(mac_client_impl_t *, flow_entry_t *);

/*
 * Initialize token bucket with bandwidth maxbw (in bits/sec)
 */
static void
mac_bwctl_state_init(mac_bwctl_state_t *bw, uint64_t maxbw)
{
	/*
	 * Convert maxbw to bytes/tick
	 */
	bw->bw_per_tick = (maxbw >> 3) / hz;
	if (bw->bw_per_tick == 0)
		bw->bw_per_tick = 1;

	bw->bw_max = bw->bw_per_tick * mac_bwctl_burst;
	if (bw->bw_max < mac_bwctl_min_depth)
		bw->bw_max = mac_bwctl_min_depth;

	bw->bw_avail = bw->bw_max;
	bw->bw_lowat = bw->bw_max / mac_bwctl_lowat_ratio;
	bw->bw_block = bw->bw_max / mac_bwctl_block_ratio;
	bw->bw_unblock = bw->bw_max / mac_bwctl_unblock_ratio;
	bw->bw_lastupdate = 0;
}

/*
 * Update both the RX and TX token buckets for this flow
 */
void
mac_bwctl_flow_update(flow_entry_t *flent)
{
	mac_resource_props_t	*mrp;

	mutex_enter(&flent->fe_lock);
	mrp = &flent->fe_resource_props;
	if ((mrp->mrp_mask & MRP_MAXBW) == 0) {
		mutex_exit(&flent->fe_lock);
		return;
	}
	mac_bwctl_state_init(&flent->fe_bwctl_rx_state, mrp->mrp_maxbw);
	mac_bwctl_state_init(&flent->fe_bwctl_tx_state, mrp->mrp_maxbw);
	mutex_exit(&flent->fe_lock);
}

/*
 * Consume up to the packet's size worth of bandwidth
 * Drop if available bandwidth is < the packet size.
 * Activate replenish thread if available bandwidth drops below bw_lowat.
 */
static uint_t
mac_bwctl_consume(mac_client_impl_t *mcip, flow_entry_t *flent,
    mblk_t *mp, uint_t flags)
{
	mac_resource_props_t	*mrp;
	mac_bwctl_state_t	*bw = NULL;
	size_t			pktsize = msgdsize(mp);
	boolean_t		replenish = B_FALSE;
	uint_t			status = 0;

	mrp = &flent->fe_resource_props;
	if ((mrp->mrp_mask & MRP_MAXBW) == 0)
		return (status);

	/*
	 * For the zero-bandwidth case, we just drop the packet
	 * unconditionally without consuming bandwidth.
	 */
	if (mrp->mrp_maxbw == 0) {
		DTRACE_PROBE1(bw__drop__zero, mac_client_impl_t *, mcip);
		status |= BW_DROP;
		return (status);
	}
	if ((flags & BW_TX) != 0) {
		bw = &flent->fe_bwctl_tx_state;
	} else if ((flags & BW_RX) != 0) {
		bw = &flent->fe_bwctl_rx_state;
	}
	mutex_enter(&flent->fe_lock);
	if (bw->bw_avail < pktsize) {
		DTRACE_PROBE1(bw__drop, mac_client_impl_t *, mcip);
		status |= BW_DROP;
	} else {
		bw->bw_avail -= pktsize;
	}
	if (!flent->fe_bwctl_active && bw->bw_avail <= bw->bw_lowat) {
		flent->fe_bwctl_active = B_TRUE;
		flent->fe_refcnt++;
		replenish = B_TRUE;
	}
	if (bw->bw_avail <= bw->bw_block &&
	    (flags & (BW_TX | BW_CB)) == (BW_TX | BW_CB)) {
		if (flent->fe_tx_unblock_cb == NULL)
			flent->fe_tx_unblock_cb = mac_bwctl_unblock;

		status |= ((flent->fe_type & FLOW_USER) != 0) ?
		    BW_FLOW_BLOCKED : BW_LINK_BLOCKED;
	}
	mutex_exit(&flent->fe_lock);

	if (replenish) {
		mutex_enter(&mcip->mci_bwctl_lock);
		flent->fe_bwctl_next = mcip->mci_bwctl_list;
		mcip->mci_bwctl_list = flent;
		cv_signal(&mcip->mci_bwctl_cv);
		mutex_exit(&mcip->mci_bwctl_lock);
	}
	return (status);
}

/*
 * Consume bandwidth on a subflow if a matching one is found.
 * If the packet has not been dropped, continue to consume bandwidth
 * of the link.
 */
static uint_t
mac_bwctl_check(mac_client_impl_t *mcip, flow_entry_t *flent,
    mblk_t *mp, uint_t flags)
{
	uint_t	status = 0;

	if ((mcip->mci_feature & MAC_BWCTL_FLOW) != 0 && flent != NULL) {
		status = mac_bwctl_consume(mcip, flent, mp, flags);
		if ((status & BW_DROP) != 0)
			return (status);
	}
	if ((mcip->mci_feature & MAC_BWCTL_LINK) != 0) {
		status |= mac_bwctl_consume(mcip, mcip->mci_flent, mp, flags);
	}
	return (status);
}

static void
mac_tx_flow_stats_update(flow_entry_t *flent, uint_t sz, boolean_t dropped)
{
	if (dropped) {
		flent->fe_stat.fs_odrops += 1;
		flent->fe_stat.fs_odropbytes += sz;
	} else {
		flent->fe_stat.fs_opackets += 1;
		flent->fe_stat.fs_obytes += sz;
	}
}

mblk_t *
mac_bwctl_tx_check(mac_client_impl_t *mcip, mblk_t *mp, void **cookiep)
{
	flow_entry_t	*flent = NULL;
	void		*cookie = NULL;
	uint_t		status, sz = msgdsize(mp);

	if (mcip->mci_subflow_tab != NULL) {
		(void) mac_flow_lookup(mcip->mci_subflow_tab,
		    mp, FLOW_OUTBOUND, &flent);
	}
	if ((mcip->mci_feature &
	    (MAC_BWCTL_LINK | MAC_BWCTL_FLOW)) != 0) {
		status = mac_bwctl_check(mcip, flent, mp, BW_TX | BW_CB);
		if ((status & BW_FLOW_BLOCKED) != 0) {
			cookie = &flent->fe_bwctl_cookie;
		} else if ((status & BW_LINK_BLOCKED) != 0) {
			cookie = &mcip->mci_flent->fe_bwctl_cookie;
		}
		if ((status & BW_DROP) != 0) {
			freemsg(mp);
			mp = NULL;
		}
	}
	if (flent != NULL) {
		mac_tx_flow_stats_update(flent, sz, (mp == NULL));
		FLOW_REFRELE(flent);
	}
	if (cookiep != NULL)
		*cookiep = cookie;
	return (mp);
}

static void
mac_rx_flow_stats_update(flow_entry_t *flent, uint_t sz, boolean_t dropped)
{
	if (dropped) {
		flent->fe_stat.fs_idrops += 1;
		flent->fe_stat.fs_idropbytes += sz;
	} else {
		flent->fe_stat.fs_ipackets += 1;
		flent->fe_stat.fs_ibytes += sz;
	}
}

mblk_t *
mac_bwctl_rx_check(mac_client_impl_t *mcip, mblk_t *mp)
{
	flow_entry_t	*flent = NULL;
	uint_t		status, sz = msgdsize(mp);

	if (mcip->mci_subflow_tab != NULL) {
		(void) mac_flow_lookup(mcip->mci_subflow_tab,
		    mp, FLOW_INBOUND, &flent);
	}
	if ((mcip->mci_feature &
	    (MAC_BWCTL_LINK | MAC_BWCTL_FLOW)) != 0) {
		status = mac_bwctl_check(mcip, flent, mp, BW_RX);
		if ((status & BW_DROP) != 0) {
			freemsg(mp);
			mp = NULL;
		}
	}
	if (flent != NULL) {
		mac_rx_flow_stats_update(flent, sz, (mp == NULL));
		FLOW_REFRELE(flent);
	}
	return (mp);
}

/*
 * Called on every tick for replenishing bandwidth
 */
static uint_t
mac_bwctl_state_update(mac_bwctl_state_t *bw)
{
	uint64_t	bytes, elapsed, curr;
	uint_t		status = 0;

	if (bw->bw_avail >= bw->bw_max)
		return (BW_DONE | BW_UNBLOCK);

	bytes = bw->bw_per_tick;

	/*
	 * The replenish thread usually runs every tick. In case of system
	 * overload, this may not be true. To compensate for the lost time,
	 * we try to scale up bw_per_tick by (elapsed time / nsecs per tick).
	 */
	curr = gethrtime();
	if (bw->bw_lastupdate != 0) {
		elapsed = (curr - bw->bw_lastupdate);
		/*
		 * We don't do NSEC_TO_TICK(elapsed) below because if elapsed
		 * is less than nsec_per_tick then NSEC_TO_TICK() returns 0 and
		 * bytes will be set to 0. 'elapsed' should actually never
		 * be less than nsec_per_tick because we always sleep for 1 tick
		 * at a time. What we have below is only for safety measure.
		 */
		if (elapsed != 0)
			bytes = NSEC_TO_TICK(elapsed * bw->bw_per_tick);
	}
	bw->bw_lastupdate = curr;
	bw->bw_avail += bytes;
	if (bw->bw_avail > bw->bw_max)
		bw->bw_avail = bw->bw_max;

	if (bw->bw_avail > bw->bw_unblock)
		status |= BW_UNBLOCK;

	if (bw->bw_avail == bw->bw_max) {
		bw->bw_lastupdate = 0;
		status |= BW_DONE;
	}
	return (status);
}

/*
 * Called by the upper layer to check if this flow is in flow-control state
 */
boolean_t
mac_tx_is_flow_blocked(void *arg)
{
	mac_bwctl_cookie_t	*cookie = arg;
	mac_bwctl_state_t	*bw;
	flow_entry_t		*flent;
	boolean_t		blocked = B_FALSE;

	ASSERT(cookie->bw_type == MAC_BWCTL_COOKIE);
	flent = cookie->bw_flent;
	mutex_enter(&flent->fe_lock);
	bw = &flent->fe_bwctl_tx_state;
	if (flent->fe_bwctl_active && bw->bw_avail < bw->bw_block &&
	    flent->fe_tx_unblock_cb != NULL) {
		blocked = B_TRUE;
	}
	mutex_exit(&flent->fe_lock);
	return (blocked);
}

static void
mac_bwctl_unblock(mac_client_impl_t *mcip, flow_entry_t *flent)
{
	mac_tx_cookie_t	cookie = (mac_tx_cookie_t)&flent->fe_bwctl_cookie;
	mac_tx_invoke_callbacks(mcip, cookie);
}

static void
mac_bwctl_replenish(mac_client_impl_t *mcip, flow_entry_t *flent)
{
	mac_bwctl_state_t	*rx_bw = &flent->fe_bwctl_rx_state;
	mac_bwctl_state_t	*tx_bw = &flent->fe_bwctl_tx_state;
	flow_unblock_cb_t	tx_cb = NULL;
	uint_t			rx_status, tx_status;

	mutex_enter(&flent->fe_lock);
	rx_status = mac_bwctl_state_update(rx_bw);
	/* no callback for rx for now */

	tx_status = mac_bwctl_state_update(tx_bw);
	if ((tx_status & BW_UNBLOCK) != 0 && flent->fe_tx_unblock_cb != NULL) {
		tx_cb = flent->fe_tx_unblock_cb;
		flent->fe_tx_unblock_cb = NULL;
	}
	if ((rx_status & BW_DONE) != 0 && (tx_status & BW_DONE) != 0) {
		flent->fe_bwctl_active = B_FALSE;
		mutex_exit(&flent->fe_lock);
		if (tx_cb != NULL)
			tx_cb(mcip, flent);

		FLOW_REFRELE(flent);
		return;
	}
	mutex_exit(&flent->fe_lock);
	if (tx_cb != NULL)
		tx_cb(mcip, flent);

	ASSERT(flent->fe_bwctl_active);
	mutex_enter(&mcip->mci_bwctl_lock);
	flent->fe_bwctl_next = mcip->mci_bwctl_list;
	mcip->mci_bwctl_list = flent;
	mutex_exit(&mcip->mci_bwctl_lock);
}

static void
mac_bwctl_worker(mac_client_impl_t *mcip)
{
	callb_cpr_t	cprinfo;
	kmutex_t	*lock = &mcip->mci_bwctl_lock;
	kcondvar_t	*cv = &mcip->mci_bwctl_cv;
	flow_entry_t	*flent, *next;

	CALLB_CPR_INIT(&cprinfo, lock, callb_generic_cpr,
	    "mac_bwctl_worker");
	mutex_enter(lock);

	while ((mcip->mci_bwctl_state & MAC_BWCTL_READY) != 0) {
		CALLB_CPR_SAFE_BEGIN(&cprinfo);
		cv_wait(cv, lock);
		CALLB_CPR_SAFE_END(&cprinfo, lock);

again:
		flent = mcip->mci_bwctl_list;
		mcip->mci_bwctl_list = NULL;
		mutex_exit(lock);

		delay(1);
		while (flent != NULL) {
			next = flent->fe_bwctl_next;
			flent->fe_bwctl_next = NULL;
			mac_bwctl_replenish(mcip, flent);
			flent = next;
		}
		mutex_enter(lock);
		if (mcip->mci_bwctl_list != NULL)
			goto again;
	}
	mcip->mci_bwctl_worker = NULL;
	cv_signal(cv);

	CALLB_CPR_EXIT(&cprinfo);
	thread_exit();
}

void
mac_bwctl_init(mac_client_impl_t *mcip)
{
	mutex_init(&mcip->mci_bwctl_lock, NULL, MUTEX_DRIVER, NULL);
	cv_init(&mcip->mci_bwctl_cv, NULL, CV_DRIVER, NULL);
	mcip->mci_bwctl_list = NULL;
	mcip->mci_bwctl_state = MAC_BWCTL_READY;
	mcip->mci_bwctl_worker = thread_create(NULL, 0, mac_bwctl_worker, mcip,
	    0, &p0, TS_RUN, maxclsyspri);
}

void
mac_bwctl_fini(mac_client_impl_t *mcip)
{
	mutex_enter(&mcip->mci_bwctl_lock);
	mcip->mci_bwctl_state = 0;
	cv_signal(&mcip->mci_bwctl_cv);
	if (mcip->mci_bwctl_worker != NULL)
		cv_wait(&mcip->mci_bwctl_cv, &mcip->mci_bwctl_lock);
	mutex_exit(&mcip->mci_bwctl_lock);

	ASSERT(mcip->mci_bwctl_list == NULL);
	mutex_destroy(&mcip->mci_bwctl_lock);
	cv_destroy(&mcip->mci_bwctl_cv);
}
