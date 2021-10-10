/*
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This file contains code imported from the OFED rds source file ib_send.c
 * Oracle elects to have and use the contents of ib_send.c under and governed
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
#include <sys/ib/clients/rdsv3/rdma.h>
#include <sys/ib/clients/rdsv3/ib.h>
#include <sys/ib/clients/rdsv3/rdsv3_debug.h>

static void
rdsv3_ib_send_rdma_complete(struct rdsv3_message *rm,
    int wc_status)
{
	int notify_status;

	RDSV3_DPRINTF4("rdsv3_ib_send_rdma_complete", "rm: %p, wc_status: %d",
	    rm, wc_status);

	switch (wc_status) {
	case IBT_WC_WR_FLUSHED_ERR:
		return;

	case IBT_WC_SUCCESS:
		notify_status = RDS_RDMA_SUCCESS;
		break;

	case IBT_WC_REMOTE_ACCESS_ERR:
		notify_status = RDS_RDMA_REMOTE_ERROR;
		break;

	default:
		notify_status = RDS_RDMA_OTHER_ERROR;
		break;
	}
	rdsv3_rdma_send_complete(rm, notify_status);

	RDSV3_DPRINTF4("rdsv3_ib_send_rdma_complete", "rm: %p, wc_status: %d",
	    rm, wc_status);
}

static void rdsv3_ib_dma_unmap_sg_rdma(struct ib_device *dev,
    uint_t num, struct rdsv3_rdma_sg scat[]);

void
rdsv3_ib_send_unmap_rdma(struct rdsv3_ib_connection *ic,
    struct rdsv3_rdma_op *op)
{
	RDSV3_DPRINTF4("rdsv3_ib_send_unmap_rdma", "ic: %p, op: %p", ic, op);
	if (op->r_mapped) {
		op->r_mapped = 0;
		if (ic->rds_ibdev && ic->rds_ibdev->dev) {
			rdsv3_ib_dma_unmap_sg_rdma(ic->rds_ibdev->dev,
			    op->r_nents, op->r_rdma_sg);
		} else {
			rdsv3_ib_dma_unmap_sg_rdma((struct ib_device *)NULL,
			    op->r_nents, op->r_rdma_sg);
		}
	}
}

static void
rdsv3_ib_send_unmap_rm(struct rdsv3_ib_connection *ic,
    struct rdsv3_ib_send_work *send,
    int wc_status)
{
	struct rdsv3_message *rm = send->s_rm;

	RDSV3_DPRINTF4("rdsv3_ib_send_unmap_rm", "ic %p send %p rm %p\n",
	    ic, send, rm);

	if (rm->m_rdma_op != NULL) {
		rdsv3_ib_send_unmap_rdma(ic, rm->m_rdma_op);

		/*
		 * If the user asked for a completion notification on this
		 * message, we can implement three different semantics:
		 *  1.	Notify when we received the ACK on the RDS message
		 *	that was queued with the RDMA. This provides reliable
		 *	notification of RDMA status at the expense of a one-way
		 *	packet delay.
		 *  2.	Notify when the IB stack gives us the completion
		 *	event for the RDMA operation.
		 *  3.	Notify when the IB stack gives us the completion
		 *	event for the accompanying RDS messages.
		 * Here, we implement approach #3. To implement approach #2,
		 * call rdsv3_rdma_send_complete from the cq_handler.
		 * To implement #1,
		 * don't call rdsv3_rdma_send_complete at all, and fall back to
		 * the notify
		 * handling in the ACK processing code.
		 *
		 * Note: There's no need to explicitly sync any RDMA buffers
		 * using
		 * ib_dma_sync_sg_for_cpu - the completion for the RDMA
		 * operation itself unmapped the RDMA buffers, which takes care
		 * of synching.
		 */
		rdsv3_ib_send_rdma_complete(rm, wc_status);

		if (rm->m_rdma_op->r_write)
			rdsv3_stats_add(s_send_rdma_bytes,
			    rm->m_rdma_op->r_bytes);
		else
			rdsv3_stats_add(s_recv_rdma_bytes,
			    rm->m_rdma_op->r_bytes);
	}

	/*
	 * If anyone waited for this message to get flushed out, wake
	 * them up now
	 */
	rdsv3_message_unmapped(rm);

	rdsv3_message_put(rm);
	send->s_rm = NULL;
}

void
rdsv3_ib_send_init_ring(struct rdsv3_ib_connection *ic)
{
	struct rdsv3_ib_send_work *send;
	uint32_t i;

	RDSV3_DPRINTF4("rdsv3_ib_send_init_ring", "ic: %p", ic);

	for (i = 0, send = ic->i_sends; i < ic->i_send_ring.w_nr; i++, send++) {
		send->s_rm = NULL;
		send->s_op = NULL;
	}
}

void
rdsv3_ib_send_clear_ring(struct rdsv3_ib_connection *ic)
{
	struct rdsv3_ib_send_work *send;
	uint32_t i;

	RDSV3_DPRINTF4("rdsv3_ib_send_clear_ring", "ic: %p", ic);

	for (i = 0, send = ic->i_sends; i < ic->i_send_ring.w_nr; i++, send++) {
		if (send->s_opcode == 0xdd)
			continue;
		if (send->s_rm)
			rdsv3_ib_send_unmap_rm(ic, send, IBT_WC_WR_FLUSHED_ERR);
		if (send->s_op)
			rdsv3_ib_send_unmap_rdma(ic, send->s_op);
	}

	RDSV3_DPRINTF4("rdsv3_ib_send_clear_ring", "Return: ic: %p", ic);
}

/*
 * The _oldest/_free ring operations here race cleanly with the alloc/unalloc
 * operations performed in the send path.  As the sender allocs and potentially
 * unallocs the next free entry in the ring it doesn't alter which is
 * the next to be freed, which is what this is concerned with.
 */
void
rdsv3_ib_send_cqe_handler(struct rdsv3_ib_connection *ic, ibt_wc_t *wc)
{
	struct rdsv3_connection *conn = ic->conn;
	struct rdsv3_ib_send_work *send;
	uint32_t completed, polled;
	uint32_t oldest;
	uint32_t i = 0;
	int ret;

	RDSV3_DPRINTF4("rdsv3_ib_send_cqe_handler",
	    "wc wc_id 0x%llx status %u byte_len %u imm_data %u\n",
	    (unsigned long long)wc->wc_id, wc->wc_status,
	    wc->wc_bytes_xfer, ntohl(wc->wc_immed_data));

	rdsv3_ib_stats_inc(s_ib_tx_cq_event);

	if (wc->wc_id == RDSV3_IB_ACK_WR_ID) {
		clear_bit(IB_ACK_IN_FLIGHT, &ic->i_ack_flags);
		if (ic->i_ack_queued + HZ/2 < jiffies)
			rdsv3_ib_stats_inc(s_ib_tx_stalled);

		/*
		 * Send the next ACK iff the previous ack completed
		 * successfully, we don't want to post another ACK
		 * if the connection is in error.
		 */
		if (wc->wc_status == IBT_WC_SUCCESS) {
			rdsv3_ib_attempt_ack(ic);
		}
		return;
	}

	oldest = rdsv3_ib_ring_oldest(&ic->i_send_ring);

	completed = rdsv3_ib_ring_completed(&ic->i_send_ring,
	    (wc->wc_id & ~RDSV3_IB_SEND_OP), oldest);

	for (i = 0; i < completed; i++) {
		send = &ic->i_sends[oldest];

		/*
		 * In the error case, wc->opcode sometimes contains
		 * garbage
		 */
		switch (send->s_opcode) {
		case IBT_WRC_SEND:
			if (send->s_rm)
				rdsv3_ib_send_unmap_rm(ic, send,
				    wc->wc_status);
			break;
		case IBT_WRC_RDMAW:
		case IBT_WRC_RDMAR:
			/*
			 * Nothing to be done - the SG list will
			 * be unmapped
			 * when the SEND completes.
			 */
			break;
		default:
#ifndef __lock_lint
			RDSV3_DPRINTF2("rdsv3_ib_send_cq_comp_handler",
			    "RDS/IB: %s: unexpected opcode "
			    "0x%x in WR!",
			    __func__, send->s_opcode);
#endif
			break;
		}

		send->s_opcode = 0xdd;
		if (send->s_queued + HZ/2 < jiffies)
			rdsv3_ib_stats_inc(s_ib_tx_stalled);

		/*
		 * If a RDMA operation produced an error, signal
		 * this right
		 * away. If we don't, the subsequent SEND that goes
		 * with this
		 * RDMA will be canceled with ERR_WFLUSH, and the
		 * application
		 * never learn that the RDMA failed.
		 */
		if (wc->wc_status ==
		    IBT_WC_REMOTE_ACCESS_ERR && send->s_op) {
			struct rdsv3_message *rm;

			rm = rdsv3_send_get_message(conn, send->s_op);
			if (rm) {
				if (rm->m_rdma_op != NULL)
					rdsv3_ib_send_unmap_rdma(ic,
					    rm->m_rdma_op);
				rdsv3_ib_send_rdma_complete(rm,
				    wc->wc_status);
				rdsv3_message_put(rm);
			}
		}

		oldest = (oldest + 1) % ic->i_send_ring.w_nr;
	}

	rdsv3_ib_send_ring_free(&ic->i_send_ring, completed);

	clear_bit(RDSV3_LL_SEND_FULL, &conn->c_flags);

	/* We expect errors as the qp is drained during shutdown */
	if (wc->wc_status != IBT_WC_SUCCESS && rdsv3_conn_up(conn)) {
		RDSV3_DPRINTF2("rdsv3_ib_send_cqe_handler",
		    "send completion on %u.%u.%u.%u -> %u.%u.%u.%u "
		    "had status %u, disconnecting and reconnecting",
		    NIPQUAD(conn->c_laddr), NIPQUAD(conn->c_faddr),
		    wc->wc_status);
		rdsv3_conn_drop(conn);
	}

	RDSV3_DPRINTF4("rdsv3_ib_send_cqe_handler", "Return: conn: %p", ic);
}

/*
 * This is the main function for allocating credits when sending
 * messages.
 *
 * Conceptually, we have two counters:
 *  -	send credits: this tells us how many WRs we're allowed
 *	to submit without overruning the reciever's queue. For
 *	each SEND WR we post, we decrement this by one.
 *
 *  -	posted credits: this tells us how many WRs we recently
 *	posted to the receive queue. This value is transferred
 *	to the peer as a "credit update" in a RDS header field.
 *	Every time we transmit credits to the peer, we subtract
 *	the amount of transferred credits from this counter.
 *
 * It is essential that we avoid situations where both sides have
 * exhausted their send credits, and are unable to send new credits
 * to the peer. We achieve this by requiring that we send at least
 * one credit update to the peer before exhausting our credits.
 * When new credits arrive, we subtract one credit that is withheld
 * until we've posted new buffers and are ready to transmit these
 * credits (see rdsv3_ib_send_add_credits below).
 *
 * The RDS send code is essentially single-threaded; rdsv3_send_xmit
 * grabs c_send_lock to ensure exclusive access to the send ring.
 * However, the ACK sending code is independent and can race with
 * message SENDs.
 *
 * In the send path, we need to update the counters for send credits
 * and the counter of posted buffers atomically - when we use the
 * last available credit, we cannot allow another thread to race us
 * and grab the posted credits counter.  Hence, we have to use a
 * spinlock to protect the credit counter, or use atomics.
 *
 * Spinlocks shared between the send and the receive path are bad,
 * because they create unnecessary delays. An early implementation
 * using a spinlock showed a 5% degradation in throughput at some
 * loads.
 *
 * This implementation avoids spinlocks completely, putting both
 * counters into a single atomic, and updating that atomic using
 * atomic_add (in the receive path, when receiving fresh credits),
 * and using atomic_cmpxchg when updating the two counters.
 */
int
rdsv3_ib_send_grab_credits(struct rdsv3_ib_connection *ic,
    uint32_t wanted, uint32_t *adv_credits, int need_posted)
{
	unsigned int avail, posted, got = 0, advertise;
	long oldval, newval;

	RDSV3_DPRINTF4("rdsv3_ib_send_grab_credits", "ic: %p, %d %d %d",
	    ic, wanted, *adv_credits, need_posted);

	*adv_credits = 0;
	if (!ic->i_flowctl)
		return (wanted);

try_again:
	advertise = 0;
	oldval = newval = atomic_get(&ic->i_credits);
	posted = IB_GET_POST_CREDITS(oldval);
	avail = IB_GET_SEND_CREDITS(oldval);

	RDSV3_DPRINTF5("rdsv3_ib_send_grab_credits",
	    "wanted (%u): credits=%u posted=%u\n", wanted, avail, posted);

	/* The last credit must be used to send a credit update. */
	if (avail && !posted)
		avail--;

	if (avail < wanted) {
		struct rdsv3_connection *conn = ic->i_cm_id->context;

		/* Oops, there aren't that many credits left! */
		set_bit(RDSV3_LL_SEND_FULL, &conn->c_flags);
		got = avail;
	} else {
		/* Sometimes you get what you want, lalala. */
		got = wanted;
	}
	newval -= IB_SET_SEND_CREDITS(got);

	/*
	 * If need_posted is non-zero, then the caller wants
	 * the posted regardless of whether any send credits are
	 * available.
	 */
	if (posted && (got || need_posted)) {
		advertise = min(posted, RDSV3_MAX_ADV_CREDIT);
		newval -= IB_SET_POST_CREDITS(advertise);
	}

	/* Finally bill everything */
	if (atomic_cmpxchg(&ic->i_credits, oldval, newval) != oldval)
		goto try_again;

	*adv_credits = advertise;

	RDSV3_DPRINTF4("rdsv3_ib_send_grab_credits", "ic: %p, %d %d %d",
	    ic, got, *adv_credits, need_posted);

	return (got);
}

void
rdsv3_ib_send_add_credits(struct rdsv3_connection *conn, unsigned int credits)
{
	struct rdsv3_ib_connection *ic = conn->c_transport_data;

	if (credits == 0)
		return;

	RDSV3_DPRINTF5("rdsv3_ib_send_add_credits",
	    "credits (%u): current=%u%s\n",
	    credits,
	    IB_GET_SEND_CREDITS(atomic_get(&ic->i_credits)),
	    test_bit(RDSV3_LL_SEND_FULL, &conn->c_flags) ?
	    ", ll_send_full" : "");

	atomic_add_32(&ic->i_credits, IB_SET_SEND_CREDITS(credits));
	if (test_and_clear_bit(RDSV3_LL_SEND_FULL, &conn->c_flags))
		RDSV3_QUEUE_DELAYED_MSG(conn, &conn->c_send_w, 0);

	ASSERT(!(IB_GET_SEND_CREDITS(credits) >= 16384));

	rdsv3_ib_stats_inc(s_ib_rx_credit_updates);

	RDSV3_DPRINTF4("rdsv3_ib_send_add_credits",
	    "Return: conn: %p, credits: %d",
	    conn, credits);
}

void
rdsv3_ib_advertise_credits(struct rdsv3_connection *conn, unsigned int posted)
{
	struct rdsv3_ib_connection *ic = conn->c_transport_data;

	RDSV3_DPRINTF4("rdsv3_ib_advertise_credits", "conn: %p, posted: %d",
	    conn, posted);

	if (posted == 0)
		return;

	atomic_add_32(&ic->i_credits, IB_SET_POST_CREDITS(posted));

	/*
	 * Decide whether to send an update to the peer now.
	 * If we would send a credit update for every single buffer we
	 * post, we would end up with an ACK storm (ACK arrives,
	 * consumes buffer, we refill the ring, send ACK to remote
	 * advertising the newly posted buffer... ad inf)
	 *
	 * Performance pretty much depends on how often we send
	 * credit updates - too frequent updates mean lots of ACKs.
	 * Too infrequent updates, and the peer will run out of
	 * credits and has to throttle.
	 * For the time being, 16 seems to be a good compromise.
	 */
	if (IB_GET_POST_CREDITS(atomic_get(&ic->i_credits)) >= 16)
		set_bit(IB_ACK_REQUESTED, &ic->i_ack_flags);
}

static inline void
rdsv3_ib_xmit_populate_wr(struct rdsv3_ib_connection *ic,
    ibt_send_wr_t *wr, unsigned int pos,
    struct rdsv3_scatterlist *scat, unsigned int msg_len,
    int send_flags)
{
	ibt_wr_ds_t *sge;

	RDSV3_DPRINTF4("rdsv3_ib_xmit_populate_wr",
	    "ic: %p, wr: %p scat: %p %d %d %d",
	    ic, wr, scat, pos, msg_len, send_flags);

	wr->wr_id = pos | RDSV3_IB_SEND_OP;
	wr->wr_trans = IBT_RC_SRV;
	wr->wr_flags = send_flags;
	wr->wr_opcode = IBT_WRC_SEND;
	wr->wr_nds = 2;

	/* header */
	sge = &wr->wr_sgl[0];
	sge->ds_va = (ib_vaddr_t)(uintptr_t)&ic->i_send_hdrs[pos];
	sge->ds_len = sizeof (struct rdsv3_header);
	sge->ds_key = ic->i_mr->lkey;

	/* data */
	sge = &wr->wr_sgl[1];
	sge->ds_va = scat->sg.ds_va;
	sge->ds_key = scat->sg.ds_key;
	sge->ds_len = scat->sg.ds_len;

	RDSV3_DPRINTF4("rdsv3_ib_xmit_populate_wr",
	    "Return: ic: %p, wr: %p scat: %p", ic, wr, scat);
}

/*
 * This can be called multiple times for a given message.  The first time
 * we see a message we map its scatterlist into the IB device so that
 * we can provide that mapped address to the IB scatter gather entries
 * in the IB work requests.  We translate the scatterlist into a series
 * of work requests that fragment the message.  These work requests complete
 * in order so we pass ownership of the message to the completion handler
 * once we send the final fragment.
 *
 * The RDS core uses the c_send_lock to only enter this function once
 * per connection.  This makes sure that the tx ring alloc/unalloc pairs
 * don't get out of sync and confuse the ring.
 */
int
rdsv3_ib_xmit(struct rdsv3_connection *conn, struct rdsv3_message *rm)
{
	struct rdsv3_ib_connection *ic = conn->c_transport_data;
	struct ib_device *dev = ic->rds_ibdev->dev;
	struct rdsv3_ib_send_work *send = NULL;
	ibt_send_wr_t *wr;
	struct rdsv3_scatterlist *scat;
	uint32_t pos, mlength;
	uint32_t i;
	uint32_t work_alloc;
	uint32_t credit_alloc;
	uint32_t posted;
	uint32_t adv_credits = 0;
	int send_flags = 0;
	int sent;
	int ret;
	int flow_controlled = 0;

	RDSV3_DPRINTF4("rdsv3_ib_xmit", "conn: %p, rm: %p", conn, rm);

	/* Do not send cong updates to IB loopback */
	if (conn->c_loopback &&
	    rm->m_hdr.h_flags & RDSV3_FLAG_CONG_BITMAP) {
		rdsv3_cong_map_updated(conn->c_fcong, ~(uint64_t)0);
		return (sizeof (struct rdsv3_header) + RDSV3_CONG_MAP_BYTES);
	}

#ifndef __lock_lint
	/* FIXME we may overallocate here */
	mlength = ntohl(rm->m_hdr.h_len);
	if (mlength == 0)
		i = 1;
	else
		i = ceil(mlength, RDSV3_FRAG_SIZE);
#endif

	work_alloc = rdsv3_ib_send_ring_alloc(&ic->i_send_ring, i, &pos);
	if (work_alloc == 0) {
		/*
		 * There is a window right here where someone could
		 * have freed up entries on the ring.  Let's make
		 * sure it really really really is full.
		 */
		set_bit(RDSV3_LL_SEND_FULL, &conn->c_flags);
		work_alloc = rdsv3_ib_send_ring_alloc(&ic->i_send_ring,
		    i, &pos);
		if (work_alloc == 0) {
			rdsv3_ib_stats_inc(s_ib_tx_ring_full);
			return (-ENOMEM);
		}
		clear_bit(RDSV3_LL_SEND_FULL, &conn->c_flags);
	}

	credit_alloc = work_alloc;
	if (ic->i_flowctl) {
		credit_alloc = rdsv3_ib_send_grab_credits(ic, work_alloc,
		    &posted, 0);
		adv_credits += posted;
		if (credit_alloc < work_alloc) {
			rdsv3_ib_ring_unalloc(&ic->i_send_ring,
			    work_alloc - credit_alloc);
			work_alloc = credit_alloc;
			flow_controlled++;
		}
		if (work_alloc == 0) {
			rdsv3_ib_stats_inc(s_ib_tx_throttle);
			return (-ENOMEM);
		}
	}

	ASSERT(ic->i_rm == NULL);

	ic->i_unsignaled_wrs = rdsv3_ib_sysctl_max_unsig_wrs;
	ic->i_unsignaled_bytes = rdsv3_ib_sysctl_max_unsig_bytes;

	/* Finalize the header */
	if (test_bit(RDSV3_MSG_ACK_REQUIRED, &rm->m_flags))
		rm->m_hdr.h_flags |= RDSV3_FLAG_ACK_REQUIRED;
	if (test_bit(RDSV3_MSG_RETRANSMITTED, &rm->m_flags))
		rm->m_hdr.h_flags |= RDSV3_FLAG_RETRANSMITTED;

	/*
	 * If it has a RDMA op, tell the peer we did it. This is
	 * used by the peer to release use-once RDMA MRs.
	 */
	if (rm->m_rdma_op) {
		struct rdsv3_ext_header_rdma ext_hdr;

		ext_hdr.h_rdma_rkey = htonl(rm->m_rdma_op->r_key);
		(void) rdsv3_message_add_extension(&rm->m_hdr,
		    RDSV3_EXTHDR_RDMA, &ext_hdr,
		    sizeof (ext_hdr));
	}
	if (rm->m_rdma_cookie) {
		(void) rdsv3_message_add_rdma_dest_extension(
		    &rm->m_hdr,
		    rdsv3_rdma_cookie_key(rm->m_rdma_cookie),
		    rdsv3_rdma_cookie_offset(rm->m_rdma_cookie));
	}

	/*
	 * Note - rdsv3_ib_piggyb_ack clears the ACK_REQUIRED bit, so
	 * we should not do this unless we have a chance of at least
	 * sticking the header into the send ring. Which is why we
	 * should call rdsv3_ib_ring_alloc first.
	 */
	rm->m_hdr.h_ack = htonll(rdsv3_ib_piggyb_ack(ic));

	/*
	 * Update adv_credits since we reset the ACK_REQUIRED bit.
	 */
	(void) rdsv3_ib_send_grab_credits(ic, 0, &posted, 1);
	adv_credits += posted;
	ASSERT(adv_credits <= 255);

	if (adv_credits) {
		/* add credit and redo the header checksum */
		rm->m_hdr.h_credit = adv_credits;
		adv_credits = 0;
		rdsv3_ib_stats_inc(s_ib_tx_credit_updates);
	}
	rdsv3_message_make_checksum(&rm->m_hdr);

	/*
	 * Sometimes you want to put a fence between an RDMA
	 * READ and the following SEND.
	 * We could either do this all the time
	 * or when requested by the user. Right now, we let
	 * the application choose.
	 */
	if (rm->m_rdma_op && rm->m_rdma_op->r_fence)
		send_flags = IBT_WR_SEND_FENCE;

	/* small message, handle inline */
	if (mlength <= rdsv3_max_inline) {
		send = &ic->i_sends[pos];
		wr = &ic->i_send_wrs[0];

		wr->wr_id = pos | RDSV3_IB_SEND_OP;
		wr->wr_trans = IBT_RC_SRV;
		wr->wr_flags = send_flags | IBT_WR_SEND_INLINE;
		wr->wr_opcode = IBT_WRC_SEND;
		wr->wr_nds = 1;

		wr->wr_sgl[0].ds_va = (ib_vaddr_t)(uintptr_t)&rm->m_hdr;
		wr->wr_sgl[0].ds_len = mlength + sizeof (struct rdsv3_header);
		wr->wr_sgl[0].ds_key = 0;

		send->s_queued = jiffies;
		send->s_op = NULL;
		send->s_opcode = wr->wr_opcode;

		goto skip_copy;
	}

	/* if there's data reference it with a chain of work reqs */
	for (i = 0, scat = &rm->m_sg[0]; i < work_alloc; i++) {
		send = &ic->i_sends[pos];

		wr = &ic->i_send_wrs[i];
		rdsv3_ib_xmit_populate_wr(ic, wr, pos, scat, mlength,
		    send_flags);
		send->s_queued = jiffies;
		send->s_op = NULL;
		send->s_opcode = wr->wr_opcode;

		/*
		 * We want to delay signaling completions just enough to get
		 * the batching benefits but not so much that we create dead
		 * time
		 * on the wire.
		 */
		if (ic->i_unsignaled_wrs-- == 0) {
			ic->i_unsignaled_wrs = rdsv3_ib_sysctl_max_unsig_wrs;
			wr->wr_flags |=
			    IBT_WR_SEND_SIGNAL | IBT_WR_SEND_SOLICIT;
		}

		ic->i_unsignaled_bytes -= rdsv3_sg_len(scat);
		if (ic->i_unsignaled_bytes <= 0) {
			ic->i_unsignaled_bytes =
			    rdsv3_ib_sysctl_max_unsig_bytes;
			wr->wr_flags |=
			    IBT_WR_SEND_SIGNAL | IBT_WR_SEND_SOLICIT;
		}

		/*
		 * Always signal the last one if we're stopping due to flow
		 * control.
		 */
		if (flow_controlled && i == (work_alloc-1)) {
			wr->wr_flags |=
			    IBT_WR_SEND_SIGNAL | IBT_WR_SEND_SOLICIT;
		}

		RDSV3_DPRINTF5("rdsv3_ib_xmit", "send %p wr %p num_sge %u \n",
		    send, wr, wr->wr_nds);

		scat++;

		/*
		 * Tack on the header after the data. The header SGE
		 * should already
		 * have been set up to point to the right header buffer.
		 */
		(void) memcpy(&ic->i_send_hdrs[pos], &rm->m_hdr,
		    sizeof (struct rdsv3_header));

		pos = (pos + 1) % ic->i_send_ring.w_nr;
	}

skip_copy:

	/* we finished the message, now send completion owns it */
	wr->wr_flags |= IBT_WR_SEND_SIGNAL | IBT_WR_SEND_SOLICIT;
	send->s_rm = rm;

	ASSERT(i == work_alloc);

	if (ic->i_flowctl && i < credit_alloc)
		rdsv3_ib_send_add_credits(conn, credit_alloc - i);

	/* XXX need to worry about failed_wr and partial sends. */
	ret = ibt_post_send(ib_get_ibt_channel_hdl(ic->i_cm_id),
	    ic->i_send_wrs, i, &posted);
	if (posted != i) {
		RDSV3_DPRINTF2("rdsv3_ib_xmit",
		    "ic %p rm %p nwr: %d ret %d:%d",
		    ic, rm, i, ret, posted);
	}
	if (ret) {
		RDSV3_DPRINTF2("rdsv3_ib_xmit",
		    "RDS/IB: ib_post_send from %u.%u.%u.%u to %u.%u.%u.%u "
		    "returned %d", NIPQUAD(conn->c_laddr),
		    NIPQUAD(conn->c_faddr), ret);
		rdsv3_conn_drop(ic->conn);
		return (-EAGAIN);
	}

	RDSV3_DPRINTF4("rdsv3_ib_xmit", "Return: conn: %p, rm: %p", conn, rm);

	/*
	 * Account the RDS header in the number of bytes we sent, but just once.
	 * The caller has no concept of fragmentation.
	 */
	return (mlength + sizeof (struct rdsv3_header));
}

static void
rdsv3_ib_dma_unmap_sg_rdma(struct ib_device *dev, uint_t num,
	struct rdsv3_rdma_sg scat[])
{
	ibt_hca_hdl_t hca_hdl;
	int i;
	int num_sgl;

	RDSV3_DPRINTF4("rdsv3_ib_dma_unmap_sg", "rdma_sg: %p", scat);

	if (dev) {
		hca_hdl = ib_get_ibt_hca_hdl(dev);
	} else {
		hca_hdl = scat[0].hca_hdl;
		RDSV3_DPRINTF2("rdsv3_ib_dma_unmap_sg_rdma",
		    "NULL dev use cached hca_hdl %p", hca_hdl);
	}

	if (hca_hdl == NULL)
		return;
	scat[0].hca_hdl = NULL;

	for (i = 0; i < num; i++) {
		if (scat[i].mihdl != NULL) {
			num_sgl = (scat[i].iovec.bytes / PAGESIZE) + 2;
			kmem_free(scat[i].swr.wr_sgl,
			    (num_sgl * sizeof (ibt_wr_ds_t)));
			scat[i].swr.wr_sgl = NULL;
			(void) ibt_unmap_mem_iov(hca_hdl, scat[i].mihdl);
			scat[i].mihdl = NULL;
		} else
			break;
	}
}

/* ARGSUSED */
uint_t
rdsv3_ib_dma_map_sg_rdma(struct ib_device *dev, struct rdsv3_rdma_sg scat[],
    uint_t num)
{
	ibt_hca_hdl_t hca_hdl;
	ibt_iov_attr_t iov_attr;
	struct buf *bp;
	uint_t i, j, k;
	uint_t count;
	int ret;

	RDSV3_DPRINTF4("rdsv3_ib_dma_map_sg_rdma", "scat: %p, num: %d",
	    scat, num);

	hca_hdl = ib_get_ibt_hca_hdl(dev);
	scat[0].hca_hdl = hca_hdl;
	bzero(&iov_attr, sizeof (ibt_iov_attr_t));
	iov_attr.iov_flags = IBT_IOV_BUF;
	iov_attr.iov_lso_hdr_sz = 0;

	for (i = 0, count = 0; i < num; i++) {
		/* transpose umem_cookie  to buf structure */
		bp = ddi_umem_iosetup(scat[i].umem_cookie,
		    scat[i].iovec.addr & PAGEOFFSET, scat[i].iovec.bytes,
		    B_WRITE, 0, 0, NULL, DDI_UMEM_SLEEP);
		if (bp == NULL) {
			/* free resources  and return error */
			goto out;
		}
		/* setup ibt_map_mem_iov() attributes */
		iov_attr.iov_buf = bp;
		iov_attr.iov_wr_nds = (scat[i].iovec.bytes / PAGESIZE) + 2;
		scat[i].swr.wr_sgl =
		    kmem_zalloc(iov_attr.iov_wr_nds * sizeof (ibt_wr_ds_t),
		    KM_SLEEP);

		ret = ibt_map_mem_iov(hca_hdl, &iov_attr,
		    (ibt_all_wr_t *)&scat[i].swr, &scat[i].mihdl);
		freerbuf(bp);
		if (ret != IBT_SUCCESS) {
			RDSV3_DPRINTF2("rdsv3_ib_dma_map_sg_rdma",
			    "ibt_map_mem_iov returned: %d", ret);
			/* free resources and return error */
			kmem_free(scat[i].swr.wr_sgl,
			    iov_attr.iov_wr_nds * sizeof (ibt_wr_ds_t));
			goto out;
		}
		count += scat[i].swr.wr_nds;

#ifdef  DEBUG
		for (j = 0; j < scat[i].swr.wr_nds; j++) {
			RDSV3_DPRINTF5("rdsv3_ib_dma_map_sg_rdma",
			    "sgl[%d] va %llx len %x", j,
			    scat[i].swr.wr_sgl[j].ds_va,
			    scat[i].swr.wr_sgl[j].ds_len);
		}
#endif
		RDSV3_DPRINTF4("rdsv3_ib_dma_map_sg_rdma",
		    "iovec.bytes: 0x%x scat[%d]swr.wr_nds: %d",
		    scat[i].iovec.bytes, i, scat[i].swr.wr_nds);
	}

	count = ((count - 1) / RDSV3_IB_MAX_SGE) + 1;
	RDSV3_DPRINTF4("rdsv3_ib_dma_map_sg_rdma", "Ret: num: %d", count);
	return (count);

out:
	rdsv3_ib_dma_unmap_sg_rdma(dev, num, scat);
	return (0);
}

int
rdsv3_ib_xmit_rdma(struct rdsv3_connection *conn, struct rdsv3_rdma_op *op)
{
	struct rdsv3_ib_connection *ic = conn->c_transport_data;
	struct rdsv3_ib_send_work *send = NULL;
	struct rdsv3_rdma_sg *scat;
	uint64_t remote_addr;
	uint32_t pos;
	uint32_t work_alloc;
	uint32_t i, j, k, idx;
	uint32_t left, count;
	uint32_t posted;
	int sent;
	ibt_status_t status;
	ibt_send_wr_t *wr;
	ibt_wr_ds_t *sge;

	RDSV3_DPRINTF4("rdsv3_ib_xmit_rdma", "rdsv3_ib_conn: %p", ic);

	/* map the message the first time we see it */
	if (!op->r_mapped) {
		op->r_count = rdsv3_ib_dma_map_sg_rdma(ic->rds_ibdev->dev,
		    op->r_rdma_sg, op->r_nents);
		RDSV3_DPRINTF5("rdsv3_ib_xmit_rdma", "ic %p mapping op %p: %d",
		    ic, op, op->r_count);
		if (op->r_count == 0) {
			rdsv3_ib_stats_inc(s_ib_tx_sg_mapping_failure);
			RDSV3_DPRINTF2("rdsv3_ib_xmit_rdma",
			    "fail: ic %p mapping op %p: %d",
			    ic, op, op->r_count);
			return (-ENOMEM); /* XXX ? */
		}
		op->r_mapped = 1;
	}

	/*
	 * Instead of knowing how to return a partial rdma read/write
	 * we insist that there
	 * be enough work requests to send the entire message.
	 */
	work_alloc = rdsv3_ib_send_ring_alloc(&ic->i_send_ring,
	    op->r_count, &pos);
	if (work_alloc == 0) {
		rdsv3_ib_stats_inc(s_ib_tx_ring_full);
		return (-ENOMEM);
	}

	RDSV3_DPRINTF4("rdsv3_ib_xmit_rdma", "pos %u cnt %u", pos, op->r_count);
	/*
	 * take the scatter list and transpose into a list of
	 * send wr's each with a scatter list of RDSV3_IB_MAX_SGE
	 */
	scat = &op->r_rdma_sg[0];
	sent = 0;
	remote_addr = op->r_remote_addr;

	for (i = 0, k = 0; i < op->r_nents; i++) {
		left = scat[i].swr.wr_nds;
		for (idx = 0; left > 0; k++) {
			send = &ic->i_sends[pos];
			send->s_queued = jiffies;
			send->s_opcode = op->r_write ? IBT_WRC_RDMAW :
			    IBT_WRC_RDMAR;
			send->s_op = op;

			wr = &ic->i_send_wrs[k];
			wr->wr_flags = 0;
			wr->wr_id = pos | RDSV3_IB_SEND_OP;
			wr->wr_trans = IBT_RC_SRV;
			wr->wr_opcode = op->r_write ? IBT_WRC_RDMAW :
			    IBT_WRC_RDMAR;
			wr->wr.rc.rcwr.rdma.rdma_raddr = remote_addr;
			wr->wr.rc.rcwr.rdma.rdma_rkey = op->r_key;

			if (left > RDSV3_IB_MAX_SGE) {
				count = RDSV3_IB_MAX_SGE;
				left -= RDSV3_IB_MAX_SGE;
			} else {
				count = left;
				left = 0;
			}
			wr->wr_nds = count;

			for (j = 0; j < count; j++) {
				sge = &wr->wr_sgl[j];
				*sge = scat[i].swr.wr_sgl[idx];
				remote_addr += scat[i].swr.wr_sgl[idx].ds_len;
				sent += scat[i].swr.wr_sgl[idx].ds_len;
				idx++;
				RDSV3_DPRINTF5("xmit_rdma",
				    "send_wrs[%d]sgl[%d] va %llx len %x",
				    k, j, sge->ds_va, sge->ds_len);
			}
			RDSV3_DPRINTF5("rdsv3_ib_xmit_rdma",
			    "wr[%d] %p key: %x code: %d tlen: %d",
			    k, wr, wr->wr.rc.rcwr.rdma.rdma_rkey,
			    wr->wr_opcode, sent);

			/*
			 * We want to delay signaling completions just enough
			 * to get the batching benefits but not so much that
			 * we create dead time on the wire.
			 */
			if (ic->i_unsignaled_wrs-- == 0) {
				ic->i_unsignaled_wrs =
				    rdsv3_ib_sysctl_max_unsig_wrs;
				wr->wr_flags = IBT_WR_SEND_SIGNAL;
			}

			pos = (pos + 1) % ic->i_send_ring.w_nr;
		}
	}

	status = ibt_post_send(ib_get_ibt_channel_hdl(ic->i_cm_id),
	    ic->i_send_wrs, k, &posted);
	if (status != IBT_SUCCESS) {
		RDSV3_DPRINTF2("rdsv3_ib_xmit_rdma",
		    "RDS/IB: rdma ib_post_send to %u.%u.%u.%u "
		    "returned %d", NIPQUAD(conn->c_faddr), status);
	}
	RDSV3_DPRINTF4("rdsv3_ib_xmit_rdma", "Ret: %p", ic);
	return (status);
}

void
rdsv3_ib_xmit_complete(struct rdsv3_connection *conn)
{
	struct rdsv3_ib_connection *ic = conn->c_transport_data;

	RDSV3_DPRINTF4("rdsv3_ib_xmit_complete", "conn: %p", conn);

	/*
	 * We may have a pending ACK or window update we were unable
	 * to send previously (due to flow control). Try again.
	 */
	rdsv3_ib_attempt_ack(ic);
}

/*
 * This relies on dma_map_sg() not touching sg[].page during merging.
 */
void
rdsv3_ib_message_purge(struct rdsv3_message *rm)
{
	struct rdsv3_scatterlist *scat;
	unsigned long i;

	RDSV3_DPRINTF4("rdsv3_ib_message_purge", "Enter(rm: %p)", rm);

	for (i = 0, scat = &rm->m_sg[0]; i < rm->m_nents; i++, scat++) {
		RDSV3_DPRINTF5("rdsv3_ib_message_purge", "putting frag %p",
		    scat->sfrag);

		ASSERT(scat->sfrag);
		if (atomic_dec_and_test(&scat->sfrag->f_refcnt))
			kmem_cache_free(scat->sfrag->f_ibdev->ib_send_slab,
			    scat->sfrag);
	}

	if (rm->m_rdma_op)
		rdsv3_rdma_free_op(rm->m_rdma_op);
	if (rm->m_rdma_mr) {
		struct rdsv3_mr *mr = rm->m_rdma_mr;
		if (mr->r_refcount == 0) {
			RDSV3_DPRINTF4("rdsv3_ib_message_purge ASSERT 0",
			    "rm %p mr %p", rm, mr);
			return;
		}
		if (mr->r_refcount == 0xdeadbeef) {
			RDSV3_DPRINTF4("rdsv3_ib_message_purge ASSERT deadbeef",
			    "rm %p mr %p", rm, mr);
			return;
		}
		if (atomic_dec_and_test(&mr->r_refcount)) {
			rm->m_rdma_mr = NULL;
			__rdsv3_put_mr_final(mr);
		}
	}

	RDSV3_DPRINTF4("rdsv3_ib_message_purge", "Return(rm: %p)", rm);

}

int
rdsv3_ib_message_copy_from_user(void *devp, struct rdsv3_message *rm,
    struct uio *uiop, size_t total_len)
{
	struct rdsv3_scatterlist *scat;
	struct rdsv3_page_frag *sfrag;
	struct kmem_cache *slab =
	    ((struct rdsv3_ib_device *)devp)->ib_send_slab;
	int ret;

	RDSV3_DPRINTF4("rdsv3_ib_message_copy_from_user",
	    "Enter: %d", total_len);

	/*
	 * now allocate and copy in the data payload.
	 */
	scat = &rm->m_sg[0];

	while (total_len) {
		sfrag = kmem_cache_alloc(slab, KM_SLEEP);

		scat->length =
		    (total_len > RDSV3_FRAG_SIZE) ? RDSV3_FRAG_SIZE : total_len;
		rdsv3_stats_add(s_copy_from_user, scat->length);
		ret = uiomove(sfrag->f_page, scat->length, UIO_WRITE, uiop);
		if (ret) {
			RDSV3_DPRINTF2("rdsv3_message_copy_from_user",
			    "uiomove failed");
			kmem_cache_free(slab, sfrag);
			rdsv3_ib_message_purge(rm);
			return (ret);
		}

		scat->sfrag = sfrag;
		scat->vaddr = sfrag->f_page;

		scat->sg.ds_va = sfrag->f_sge.ds_va;
		scat->sg.ds_key = sfrag->f_sge.ds_key;
		scat->sg.ds_len = scat->length;

		sfrag->f_ibdev = (struct rdsv3_ib_device *)devp;
		atomic_add_32(&scat->sfrag->f_refcnt, 1);
		rm->m_nents++;

		total_len -= scat->length;
		scat++;
	}

	return (0);
}

/*
 * Congestion Bit Map for the local IP address
 */
struct rdsv3_cong_map *
rdsv3_ib_alloc_cong_map(struct rdsv3_ip_bucket *bucketp)
{
	struct rdsv3_cong_map *map;
	struct rdsv3_message *rm;
	struct rdsv3_scatterlist *scat;
	struct rdsv3_page_frag *sfrag;
	struct kmem_cache *slab =
	    ((struct rdsv3_ib_device *)bucketp->devp)->ib_send_slab;
	uint_t i;

	RDSV3_DPRINTF4("rdsv3_alloc_cong_map",
	    "Enter(addr: %u.%u.%u.%u)", NIPQUAD(bucketp->ip));

	/* allocate congestion map, followed by message */
	map = kmem_zalloc(sizeof (struct rdsv3_cong_map) +
	    sizeof (struct rdsv3_message) +
	    (RDSV3_CONG_MAP_PAGES * sizeof (struct rdsv3_scatterlist)),
	    KM_NOSLEEP);
	if (!map)
		return (NULL);

	rm = (struct rdsv3_message *)((uint8_t *)map +
	    sizeof (struct rdsv3_cong_map));
	map->m_rm = rm;
	map->m_addr = bucketp->ip;

	for (i = 0; i < RDSV3_CONG_MAP_PAGES; i++) {
		sfrag = kmem_cache_alloc(slab, KM_NOSLEEP);
		if (sfrag == NULL) {
			RDSV3_DPRINTF2("rdsv3_alloc_cong_map",
			    "kmem_cache_alloc failed for %u.%u.%u.%u",
			    NIPQUAD(bucketp->ip));
			break;
		}
		bzero(sfrag->f_page, RDSV3_FRAG_SIZE);
		scat = &rm->m_sg[i];
		scat->length = RDSV3_FRAG_SIZE;
		scat->sfrag = sfrag;
		scat->vaddr = sfrag->f_page;
		scat->sg.ds_va = sfrag->f_sge.ds_va;
		scat->sg.ds_key = sfrag->f_sge.ds_key;
		scat->sg.ds_len = sfrag->f_sge.ds_len;
		sfrag->f_ibdev = (struct rdsv3_ib_device *)bucketp->devp;
		map->m_page_addrs[i] = (unsigned long)sfrag->f_page;
	}

	/* failure */
	if (i != RDSV3_CONG_MAP_PAGES) {
		/* above allocation failed */
		for (i = 0; i < RDSV3_CONG_MAP_PAGES; i++) {
			if (rm->m_sg[i].sfrag != NULL)
				kmem_cache_free(slab, rm->m_sg[i].sfrag);
		}
		kmem_free(map, sizeof (struct rdsv3_cong_map) +
		    sizeof (struct rdsv3_message) +
		    (RDSV3_CONG_MAP_PAGES *
		    sizeof (struct rdsv3_scatterlist)));
		return (NULL);
	}

	rm->m_nents = RDSV3_CONG_MAP_PAGES;
	rm->m_hdr.h_len = htonl(RDSV3_CONG_MAP_PAGES * RDSV3_FRAG_SIZE);
	rm->m_hdr.h_flags = RDSV3_FLAG_CONG_BITMAP;
	rm->m_trans = bucketp->trans;
	rm->m_refcount = 2;

	rdsv3_init_waitqueue(&map->m_waitq);

	RDSV3_DPRINTF4("rdsv3_alloc_cong_map",
	    "Return(addr: %u.%u.%u.%u)", NIPQUAD(bucketp->ip));

	return (map);
}

void
rdsv3_ib_free_cong_map(struct rdsv3_ip_bucket *bucketp)
{
	struct rdsv3_cong_map *map;
	struct rdsv3_message *rm;
	uint_t i;

	map = bucketp->lcong;
	rm = map->m_rm;

	for (i = 0; i < RDSV3_CONG_MAP_PAGES; i++) {
		kmem_cache_free(
		    ((struct rdsv3_ib_device *)bucketp->devp)->ib_send_slab,
		    rm->m_sg[i].sfrag);
	}

	kmem_free(map, sizeof (struct rdsv3_cong_map) +
	    sizeof (struct rdsv3_message) +
	    (RDSV3_CONG_MAP_PAGES *
	    sizeof (struct rdsv3_scatterlist)));
}
