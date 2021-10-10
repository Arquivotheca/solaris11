/*
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This file contains code imported from the OFED rds source file ib_cm.c
 * Oracle elects to have and use the contents of ib_cm.c under and governed
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

#include <sys/ib/clients/of/ofed_kernel.h>
#include <sys/ib/clients/of/rdma/ib_addr.h>
#include <sys/ib/clients/of/rdma/rdma_cm.h>

#include <sys/ib/clients/rdsv3/rdsv3.h>
#include <sys/ib/clients/rdsv3/ib.h>
#include <sys/ib/clients/rdsv3/rdsv3_debug.h>

static uint32_t rdsv3_ib_protocol_compatible(struct rdma_cm_event *);
extern int rdsv3_enable_snd_cq;

/*
 * Set the selected protocol version
 */
static void
rdsv3_ib_set_protocol(struct rdsv3_connection *conn, unsigned int version)
{
	RDSV3_DPRINTF4("rdsv3_ib_set_protocol", "conn: %p version: %d",
	    conn, version);
	conn->c_version = version;
}

/*
 * Set up flow control
 */
static void
rdsv3_ib_set_flow_control(struct rdsv3_connection *conn, uint32_t credits)
{
	struct rdsv3_ib_connection *ic = conn->c_transport_data;

	RDSV3_DPRINTF2("rdsv3_ib_set_flow_control",
	    "Enter: conn: %p credits: %d", conn, credits);

	if (rdsv3_ib_sysctl_flow_control && credits != 0) {
		/* We're doing flow control */
		ic->i_flowctl = 1;
		rdsv3_ib_send_add_credits(conn, credits);
	} else {
		ic->i_flowctl = 0;
	}

	RDSV3_DPRINTF2("rdsv3_ib_set_flow_control",
	    "Return: conn: %p credits: %d",
	    conn, credits);
}

/*
 * Tune RNR behavior. Without flow control, we use a rather
 * low timeout, but not the absolute minimum - this should
 * be tunable.
 *
 * We already set the RNR retry count to 7 (which is the
 * smallest infinite number :-) above.
 * If flow control is off, we want to change this back to 0
 * so that we learn quickly when our credit accounting is
 * buggy.
 *
 * Caller passes in a qp_attr pointer - don't waste stack spacv
 * by allocation this twice.
 */
static void
rdsv3_ib_tune_rnr(struct rdsv3_ib_connection *ic, struct ib_qp_attr *attr)
{
	int ret;

	RDSV3_DPRINTF2("rdsv3_ib_tune_rnr", "Enter ic: %p attr: %p",
	    ic, attr);

	attr->min_rnr_timer = IB_RNR_TIMER_000_32;
	ret = ib_modify_qp(ic->i_cm_id->qp, attr, IB_QP_MIN_RNR_TIMER);
	if (ret)
		RDSV3_DPRINTF2("rdsv3_ib_tune_rnr",
		    "ib_modify_qp(IB_QP_MIN_RNR_TIMER): err=%d", -ret);
}

/*
 * Connection established.
 * We get here for both outgoing and incoming connection.
 */
void
rdsv3_ib_cm_connect_complete(struct rdsv3_connection *conn,
    struct rdma_cm_event *event)
{
	const struct rdsv3_ib_connect_private *dp = NULL;
	struct rdsv3_ib_connection *ic = conn->c_transport_data;
	struct rdsv3_ib_device *rds_ibdev =
	    ib_get_client_data(ic->i_cm_id->device, &rdsv3_ib_client);
	struct ib_qp_attr qp_attr;
	uint32_t version = 0;
	int err;

	RDSV3_DPRINTF2("rdsv3_ib_cm_connect_complete",
	    "Enter conn: %p event: %p", conn, event);

	/* Iff private_data_len is zero, private_data can be NULL */
	ASSERT(event->param.conn.private_data != NULL ||
	    event->param.conn.private_data_len == 0);

	if (event->param.conn.private_data_len >= sizeof (*dp)) {
		dp = event->param.conn.private_data;
		ASSERT(dp != NULL);

		/* check the protocol version */
		if ((version = rdsv3_ib_protocol_compatible(event)) == 0) {
			RDSV3_DPRINTF2("rdsv3_ib_cm_connect_complete RDS/IB: ",
			    "Connection to %u.%u.%u.%u version %u.%u failed",
			    NIPQUAD(conn->c_faddr),
			    RDS_PROTOCOL_MAJOR(conn->c_version),
			    RDS_PROTOCOL_MINOR(conn->c_version));
			rdsv3_conn_drop(conn);
			return;
		}

		/* set the protocol version to c_version */
		rdsv3_ib_set_protocol(conn, version);
		rdsv3_ib_set_flow_control(conn, ntohl(dp->dp_credit));
	}

	/* c_version must be set at this point. Double-check the version. */
	if (conn->c_version < RDS_PROTOCOL(3, 1)) {
		RDSV3_DPRINTF2("rdsv3_ib_cm_connect_complete",
		    "RDS/IB: %u.%u.%u.%u Connection to %u.%u.%u.%u "
		    "version %u.%u failed", NIPQUAD(conn->c_laddr),
		    NIPQUAD(conn->c_faddr),
		    RDS_PROTOCOL_MAJOR(conn->c_version),
		    RDS_PROTOCOL_MINOR(conn->c_version));
		rdsv3_conn_drop(conn);
		return;
	} else {
		RDSV3_DPRINTF2("rdsv3_ib_cm_connect_complete",
		    "RDS/IB: %u.%u.%u.%u connected to %u.%u.%u.%u "
		    "version %u.%u%s", NIPQUAD(conn->c_laddr),
		    NIPQUAD(conn->c_faddr),
		    RDS_PROTOCOL_MAJOR(conn->c_version),
		    RDS_PROTOCOL_MINOR(conn->c_version),
		    ic->i_flowctl ? ", flow control" : "");
	}

	ASSERT(ic->i_soft_cq == NULL);
	ic->i_soft_cq = rdsv3_af_intr_thr_create(rdsv3_ib_tasklet_fn,
	    (void *)ic, SCQ_INTR_BIND_CPU, rds_ibdev->aft_hcagp,
	    ic->i_cq->ibt_cq, conn->c_procspec, "rdsv3_ib_tasklet_fn");
	if (rdsv3_enable_snd_cq) {
		ic->i_snd_soft_cq = rdsv3_af_intr_thr_create(
		    rdsv3_ib_snd_tasklet_fn,
		    (void *)ic, SCQ_INTR_BIND_CPU, rds_ibdev->aft_hcagp,
		    ic->i_snd_cq->ibt_cq, conn->c_procspec,
		    "rdsv3_ib_snd_tasklet_fn");
	}
	ic->i_refill_rq = rdsv3_af_thr_create(rdsv3_ib_refill_fn, (void *)conn,
	    SCQ_WRK_BIND_CPU, rds_ibdev->aft_hcagp, conn->c_procspec,
	    "rdsv3_ib_refill_fn");
	rdsv3_af_grp_draw(rds_ibdev->aft_hcagp);

	(void) ib_req_notify_cq(ic->i_cq, IB_CQ_SOLICITED);
	if (rdsv3_enable_snd_cq) {
		(void) ib_req_notify_cq(ic->i_snd_cq, IB_CQ_NEXT_COMP);
	}

	/*
	 * Init rings and fill recv. this needs to wait until protocol
	 * negotiation
	 * is complete, since ring layout is different from 3.0 to 3.1.
	 */
	rdsv3_ib_send_init_ring(ic);
	rdsv3_ib_recv_init_ring(ic);
	/*
	 * Post receive buffers - as a side effect, this will update
	 * the posted credit count.
	 */
	while (rdsv3_ib_recv_refill(conn, 1) != 0) {
		/* don't expect this to happen but better safe than sorry */
		drv_usecwait(1);
	}

	/* Tune RNR behavior */
	rdsv3_ib_tune_rnr(ic, &qp_attr);

	qp_attr.qp_state = IB_QPS_RTS;
	err = ib_modify_qp(ic->i_cm_id->qp, &qp_attr, IB_QP_STATE);
	if (err)
		RDSV3_DPRINTF2("rdsv3_ib_cm_connect_complete",
		    "ib_modify_qp(IB_QP_STATE, RTS): err=%d", err);

	/* update ib_device with this local ipaddr & conn */
	err = rdsv3_ib_update_ipaddr(rds_ibdev, conn->c_laddr);
	if (err)
		RDSV3_DPRINTF2("rdsv3_ib_cm_connect_complete",
		    "rdsv3_ib_update_ipaddr failed (%d)", err);
	rdsv3_ib_add_conn(rds_ibdev, conn);

	/*
	 * If the peer gave us the last packet it saw, process this as if
	 * we had received a regular ACK.
	 */
	if (dp && dp->dp_ack_seq)
		rdsv3_send_drop_acked(conn, ntohll(dp->dp_ack_seq), NULL);

	rdsv3_connect_complete(conn);

	RDSV3_DPRINTF2("rdsv3_ib_cm_connect_complete",
	    "Return conn: %p event: %p",
	    conn, event);
}

static void
rdsv3_ib_cm_fill_conn_param(struct rdsv3_connection *conn,
    struct rdma_conn_param *conn_param,
    struct rdsv3_ib_connect_private *dp,
    uint32_t protocol_version,
    uint32_t max_responder_resources,
    uint32_t max_initiator_depth)
{
	struct rdsv3_ib_connection *ic = conn->c_transport_data;
	struct rdsv3_ib_device *rds_ibdev;

	RDSV3_DPRINTF2("rdsv3_ib_cm_fill_conn_param",
	    "Enter conn: %p conn_param: %p private: %p version: %d",
	    conn, conn_param, dp, protocol_version);

	(void) memset(conn_param, 0, sizeof (struct rdma_conn_param));

	rds_ibdev = ib_get_client_data(ic->i_cm_id->device, &rdsv3_ib_client);

	conn_param->responder_resources =
	    MIN(rds_ibdev->max_responder_resources, max_responder_resources);
	conn_param->initiator_depth =
	    MIN(rds_ibdev->max_initiator_depth, max_initiator_depth);
	conn_param->retry_count = min(rdsv3_ib_retry_count, 7);
	conn_param->rnr_retry_count = 7;

	if (dp) {
		(void) memset(dp, 0, sizeof (*dp));
		dp->dp_saddr = conn->c_laddr;
		dp->dp_daddr = conn->c_faddr;
		dp->dp_protocol_major = RDS_PROTOCOL_MAJOR(protocol_version);
		dp->dp_protocol_minor = RDS_PROTOCOL_MINOR(protocol_version);
		dp->dp_protocol_minor_mask =
		    htons(RDSV3_IB_SUPPORTED_PROTOCOLS);
		dp->dp_ack_seq = rdsv3_ib_piggyb_ack(ic);

		/* Advertise flow control */
		if (ic->i_flowctl) {
			unsigned int credits;

			credits = IB_GET_POST_CREDITS(
			    atomic_get(&ic->i_credits));
			dp->dp_credit = htonl(credits);
			atomic_add_32(&ic->i_credits,
			    -IB_SET_POST_CREDITS(credits));
		}

		conn_param->private_data = dp;
		conn_param->private_data_len = sizeof (*dp);
	}

	RDSV3_DPRINTF2("rdsv3_ib_cm_fill_conn_param",
	    "Return conn: %p conn_param: %p private: %p version: %d",
	    conn, conn_param, dp, protocol_version);
}

static void
rdsv3_ib_cq_event_handler(struct ib_event *event, void *data)
{
	RDSV3_DPRINTF3("rdsv3_ib_cq_event_handler", "event %u data %p",
	    event->event, data);
}

static void
rdsv3_ib_snd_cq_comp_handler(struct ib_cq *cq, void *context)
{
	struct rdsv3_connection *conn = context;
	struct rdsv3_ib_connection *ic = conn->c_transport_data;

	RDSV3_DPRINTF4("rdsv3_ib_snd_cq_comp_handler",
	    "Enter(conn: %p ic: %p cq: %p)", conn, ic, cq);

	rdsv3_af_thr_fire(ic->i_snd_soft_cq);
}

void
rdsv3_ib_snd_tasklet_fn(void *data)
{
	struct rdsv3_ib_connection *ic = (struct rdsv3_ib_connection *)data;
	struct rdsv3_connection *conn = ic->conn;
	struct rdsv3_ib_ack_state ack_state = { 0, };
	ibt_wc_t wc;
	uint_t polled;

	RDSV3_DPRINTF4("rdsv3_ib_snd_tasklet_fn",
	    "Enter(conn: %p ic: %p)", conn, ic);

	/*
	 * Poll in a loop before and after enabling the next event
	 */
	while (ibt_poll_cq(RDSV3_CQ2CQHDL(ic->i_snd_cq), &wc, 1, &polled) ==
	    IBT_SUCCESS) {
		RDSV3_DPRINTF4("rdsv3_ib_snd_tasklet_fn",
		    "wc_id 0x%llx type %d status %u byte_len %u imm_data %u\n",
		    (unsigned long long)wc.wc_id, wc.wc_type, wc.wc_status,
		    wc.wc_bytes_xfer, ntohl(wc.wc_immed_data));

		ASSERT(wc.wc_id & RDSV3_IB_SEND_OP);
		rdsv3_ib_send_cqe_handler(ic, &wc);
	}
	(void) ibt_enable_cq_notify(RDSV3_CQ2CQHDL(ic->i_snd_cq),
	    IBT_NEXT_COMPLETION);
	while (ibt_poll_cq(RDSV3_CQ2CQHDL(ic->i_snd_cq), &wc, 1, &polled) ==
	    IBT_SUCCESS) {
		RDSV3_DPRINTF4("rdsv3_ib_snd_tasklet_fn",
		    "wc_id 0x%llx type %d status %u byte_len %u imm_data %u\n",
		    (unsigned long long)wc.wc_id, wc.wc_type, wc.wc_status,
		    wc.wc_bytes_xfer, ntohl(wc.wc_immed_data));

		ASSERT(wc.wc_id & RDSV3_IB_SEND_OP);
		rdsv3_ib_send_cqe_handler(ic, &wc);
	}
}

static void
rdsv3_ib_cq_comp_handler(struct ib_cq *cq, void *context)
{
	struct rdsv3_connection *conn = context;
	struct rdsv3_ib_connection *ic = conn->c_transport_data;

	RDSV3_DPRINTF4("rdsv3_ib_cq_comp_handler",
	    "Enter(conn: %p cq: %p)", conn, cq);

	rdsv3_ib_stats_inc(s_ib_evt_handler_call);

	rdsv3_af_thr_fire(ic->i_soft_cq);
}

void
rdsv3_ib_refill_fn(void *data)
{
	struct rdsv3_connection *conn = (struct rdsv3_connection *)data;
	struct rdsv3_ib_connection *ic =
	    (struct rdsv3_ib_connection *)conn->c_transport_data;

	(void) rdsv3_ib_recv_refill(conn, 0);

	/*
	 * rdsv3_ib_recv_refill() can return -ENOMEM and fail to post
	 * any RECV WRs. If the ring is empty, this is our last chance
	 * to post WRs. Do not leave without posting at least one WR.
	 */
	if (rdsv3_ib_ring_empty(&ic->i_recv_ring)) {
		int ret;
		do {
			delay(1);
			rdsv3_ib_stats_inc(s_ib_rx_refill_from_thread);
			ret = rdsv3_ib_recv_refill(conn, 0);
		} while (ret == -ENOMEM);
	}
}

void
rdsv3_ib_tasklet_fn(void *data)
{
	struct rdsv3_ib_connection *ic = (struct rdsv3_ib_connection *)data;
	struct rdsv3_connection *conn = ic->conn;
	struct rdsv3_ib_ack_state ack_state = { 0, };
	ibt_wc_t wc[RDSV3_IB_WC_POLL_SIZE];
	uint_t polled;
	int i;

	RDSV3_DPRINTF4("rdsv3_ib_tasklet_fn",
	    "Enter(conn: %p ic: %p)", conn, ic);

	rdsv3_ib_stats_inc(s_ib_tasklet_call);

	/*
	 * Poll in a loop before and after enabling the next event
	 */
	while (ibt_poll_cq(RDSV3_CQ2CQHDL(ic->i_cq), &wc[0],
	    RDSV3_IB_WC_POLL_SIZE, &polled) == IBT_SUCCESS) {
		for (i = 0; i < polled; i++) {
			RDSV3_DPRINTF4("rdsv3_ib_tasklet_fn",
			"wc_id 0x%llx type %d status %u byte_len %u \
			    imm_data %u\n",
			    (unsigned long long)wc[i].wc_id, wc[i].wc_type,
			    wc[i].wc_status, wc[i].wc_bytes_xfer,
			    ntohl(wc[i].wc_immed_data));

			if (wc[i].wc_id & RDSV3_IB_SEND_OP) {
				rdsv3_ib_send_cqe_handler(ic, &wc[i]);
			} else {
				rdsv3_ib_recv_cqe_handler(ic, &wc[i],
				    &ack_state);
			}
		}
	}
	(void) ibt_enable_cq_notify(RDSV3_CQ2CQHDL(ic->i_cq),
	    IBT_NEXT_SOLICITED);
	while (ibt_poll_cq(RDSV3_CQ2CQHDL(ic->i_cq), &wc[0],
	    RDSV3_IB_WC_POLL_SIZE, &polled) == IBT_SUCCESS) {
		for (i = 0; i < polled; i++) {
			RDSV3_DPRINTF4("rdsv3_ib_tasklet_fn",
			"wc_id 0x%llx type %d status %u byte_len %u \
			    imm_data %u\n",
			    (unsigned long long)wc[i].wc_id, wc[i].wc_type,
			    wc[i].wc_status, wc[i].wc_bytes_xfer,
			    ntohl(wc[i].wc_immed_data));

			if (wc[i].wc_id & RDSV3_IB_SEND_OP) {
				rdsv3_ib_send_cqe_handler(ic, &wc[i]);
			} else {
				rdsv3_ib_recv_cqe_handler(ic, &wc[i],
				    &ack_state);
			}
		}
	}

	if (ack_state.ack_next_valid) {
		rdsv3_ib_set_ack(ic, ack_state.ack_next,
		    ack_state.ack_required);
	}
	if (ack_state.ack_recv_valid && ack_state.ack_recv > ic->i_ack_recv) {
		rdsv3_send_drop_acked(conn, ack_state.ack_recv, NULL);
		ic->i_ack_recv = ack_state.ack_recv;
	}
	if (rdsv3_conn_up(conn)) {
		if (!test_bit(RDSV3_LL_SEND_FULL, &conn->c_flags))
			(void) rdsv3_send_xmit(ic->conn);
		rdsv3_ib_attempt_ack(ic);
	}
}

static void
rdsv3_ib_qp_event_handler(struct ib_event *event, void *data)
{
	struct rdsv3_connection *conn = data;
	struct rdsv3_ib_connection *ic = conn->c_transport_data;

	RDSV3_DPRINTF2("rdsv3_ib_qp_event_handler", "conn %p ic %p event %u",
	    conn, ic, event->event);

	switch (event->event) {
	case IB_EVENT_COMM_EST:
		(void) rdma_notify(ic->i_cm_id, IB_EVENT_COMM_EST);
		break;
	default:
		if (conn) {
			RDSV3_DPRINTF2("rdsv3_ib_qp_event_handler",
			    "RDS/IB: Fatal QP Event %u - "
			    "connection %u.%u.%u.%u ->%u.%u.%u.%u "
			    "...reconnecting",
			    event->event, NIPQUAD(conn->c_laddr),
			    NIPQUAD(conn->c_faddr));
			rdsv3_conn_drop(conn);
		} else {
			RDSV3_DPRINTF2("rdsv3_ib_qp_event_handler",
			    "RDS/IB: Fatal QP Event %u - connection"
			    "...reconnecting", event->event);
		}
		break;
	}

	RDSV3_DPRINTF2("rdsv3_ib_qp_event_handler", "Return conn: %p event: %p",
	    conn, event);
}

extern int rdsv3_ib_alloc_hdrs(ib_device_t *dev,
    struct rdsv3_ib_connection *ic);
extern void rdsv3_ib_free_hdrs(ib_device_t *dev,
    struct rdsv3_ib_connection *ic);

/*
 * This needs to be very careful to not leave IS_ERR pointers around for
 * cleanup to trip over.
 */
static int
rdsv3_ib_setup_qp(struct rdsv3_connection *conn)
{
	struct rdsv3_ib_connection *ic = conn->c_transport_data;
	struct ib_device *dev = ic->i_cm_id->device;
	struct ib_qp_init_attr attr;
	struct rdsv3_ib_device *rds_ibdev;
	ibt_send_wr_t *wrp;
	ibt_wr_ds_t *sgl;
	int ret, i;
	ib_comp_handler comp_handler;

	RDSV3_DPRINTF2("rdsv3_ib_setup_qp", "Enter conn: %p", conn);

	if (dev == NULL) {
		RDSV3_DPRINTF2("rdsv3_ib_setup_qp",
		    "RDS/IB: no ib_dev in cmd_id");
		return (-EOPNOTSUPP);
	}
	/*
	 * rdsv3_ib_add_one creates a rdsv3_ib_device object per IB device,
	 * and allocates a protection domain, memory range and FMR pool
	 * for each.  If that fails for any reason, it will not register
	 * the rds_ibdev at all.
	 */
	rds_ibdev = ib_get_client_data(dev, &rdsv3_ib_client);
	if (!rds_ibdev) {
		RDSV3_DPRINTF2("rdsv3_ib_setup_qp",
		    "RDS/IB: No client_data for device %s", dev->name);
		return (-EOPNOTSUPP);
	}
	ic->rds_ibdev = rds_ibdev;

	if (rds_ibdev->max_wrs < ic->i_send_ring.w_nr + 1)
		rdsv3_ib_ring_resize(&ic->i_send_ring, rds_ibdev->max_wrs - 1);
	if (rds_ibdev->max_wrs < ic->i_recv_ring.w_nr + 1)
		rdsv3_ib_ring_resize(&ic->i_recv_ring, rds_ibdev->max_wrs - 1);

	/* Protection domain and memory range */
	ic->i_pd = rds_ibdev->pd;

	/*
	 * IB_CQ_VECTOR_LEAST_ATTACHED and/or the corresponding feature is
	 * not implmeneted in Hermon yet, but we can pass it to ib_create_cq()
	 * anyway.
	 */
	comp_handler = rdsv3_ib_cq_comp_handler;
	ic->i_cq = ib_create_cq(rds_ibdev->dev, comp_handler,
	    rdsv3_ib_cq_event_handler, conn,
	    ic->i_recv_ring.w_nr + ic->i_send_ring.w_nr + 1,
	    rdsv3_af_grp_get_sched(ic->rds_ibdev->aft_hcagp));
	if (IS_ERR(ic->i_cq)) {
		ret = PTR_ERR(ic->i_cq);
		ic->i_cq = NULL;
		RDSV3_DPRINTF2("rdsv3_ib_setup_qp",
		    "ib_create_cq failed: %d", ret);
		goto out;
	}
	if (rdsv3_enable_snd_cq) {
		ic->i_snd_cq = ib_create_cq(rds_ibdev->dev,
		    rdsv3_ib_snd_cq_comp_handler,
		    rdsv3_ib_cq_event_handler, conn, ic->i_send_ring.w_nr + 1,
		    rdsv3_af_grp_get_sched(ic->rds_ibdev->aft_hcagp));
		if (IS_ERR(ic->i_snd_cq)) {
			ret = PTR_ERR(ic->i_snd_cq);
			(void) ib_destroy_cq(ic->i_cq);
			ic->i_cq = NULL;
			ic->i_snd_cq = NULL;
			RDSV3_DPRINTF2("rdsv3_ib_setup_qp",
			    "ib_create_cq send cq failed: %d", ret);
			goto out;
		}
	}

	/* XXX negotiate max send/recv with remote? */
	(void) memset(&attr, 0, sizeof (attr));
	attr.event_handler = rdsv3_ib_qp_event_handler;
	attr.qp_context = conn;
	/* + 1 to allow for the single ack message */
	attr.cap.max_send_wr = ic->i_send_ring.w_nr + 1;
	attr.cap.max_recv_wr = ic->i_recv_ring.w_nr + 1;
	attr.cap.max_send_sge = rds_ibdev->max_sge;
	attr.cap.max_recv_sge = RDSV3_IB_RECV_SGE;
	attr.cap.max_inline_data = rds_ibdev->max_inline;
	attr.sq_sig_type = IB_SIGNAL_REQ_WR;
	attr.qp_type = IB_QPT_RC;
	if (rdsv3_enable_snd_cq) {
		attr.send_cq = ic->i_snd_cq;
	} else {
		attr.send_cq = ic->i_cq;
	}
	attr.recv_cq = ic->i_cq;

	/*
	 * XXX this can fail if max_*_wr is too large?  Are we supposed
	 * to back off until we get a value that the hardware can support?
	 */
	ret = rdma_create_qp(ic->i_cm_id, ic->i_pd, &attr);
	if (ret) {
		RDSV3_DPRINTF2("rdsv3_ib_setup_qp",
		    "rdma_create_qp failed: %d", ret);
		goto out;
	}

	if (attr.cap.max_inline_data < RDSV3_MAX_INLINE +
	    sizeof (struct rdsv3_header)) {
		ret = -ENOMEM;
		RDSV3_DPRINTF2("rdsv3_ib_setup_qp",
		    "Unexpected inline data size error: %d", ret);
		goto out;
	}

	ret = rdsv3_ib_alloc_hdrs(rds_ibdev->dev, ic);
	if (ret != 0) {
		ret = -ENOMEM;
		RDSV3_DPRINTF2("rdsv3_ib_setup_qp",
		    "rdsv3_ib_alloc_hdrs failed: %d", ret);
		goto out;
	}

	ic->i_sends = kmem_alloc(ic->i_send_ring.w_nr *
	    sizeof (struct rdsv3_ib_send_work), KM_NOSLEEP);
	if (ic->i_sends == NULL) {
		ret = -ENOMEM;
		RDSV3_DPRINTF2("rdsv3_ib_setup_qp",
		    "send allocation failed: %d", ret);
		goto out;
	}
	(void) memset(ic->i_sends, 0, ic->i_send_ring.w_nr *
	    sizeof (struct rdsv3_ib_send_work));

	ic->i_send_wrs =
	    kmem_alloc(ic->i_send_ring.w_nr * (sizeof (ibt_send_wr_t) +
	    RDSV3_IB_MAX_SGE * sizeof (ibt_wr_ds_t)), KM_NOSLEEP);
	if (ic->i_send_wrs == NULL) {
		ret = -ENOMEM;
		RDSV3_DPRINTF2("rdsv3_ib_setup_qp",
		    "Send WR allocation failed: %d", ret);
		goto out;
	}
	sgl = (ibt_wr_ds_t *)((uint8_t *)ic->i_send_wrs +
	    (ic->i_send_ring.w_nr * sizeof (ibt_send_wr_t)));
	for (i = 0; i < ic->i_send_ring.w_nr; i++) {
		wrp = &ic->i_send_wrs[i];
		wrp->wr_sgl = &sgl[i * RDSV3_IB_MAX_SGE];
	}

	ic->i_recvs = kmem_alloc(ic->i_recv_ring.w_nr *
	    sizeof (struct rdsv3_ib_recv_work), KM_NOSLEEP);
	if (ic->i_recvs == NULL) {
		ret = -ENOMEM;
		RDSV3_DPRINTF2("rdsv3_ib_setup_qp",
		    "recv allocation failed: %d", ret);
		goto out;
	}
	(void) memset(ic->i_recvs, 0, ic->i_recv_ring.w_nr *
	    sizeof (struct rdsv3_ib_recv_work));

	ic->i_recv_wrs =
	    kmem_alloc(ic->i_recv_ring.w_nr * sizeof (ibt_recv_wr_t),
	    KM_NOSLEEP);
	if (ic->i_recv_wrs == NULL) {
		ret = -ENOMEM;
		RDSV3_DPRINTF2("rdsv3_ib_setup_qp",
		    "Recv WR allocation failed: %d", ret);
		goto out;
	}

	rdsv3_ib_recv_init_ack(ic);

	RDSV3_DPRINTF2("rdsv3_ib_setup_qp",
	    "conn %p cmid %p qp %p pd %p mr %p cq %p",
	    conn, ic->i_cm_id, ic->i_cm_id->qp, ic->i_pd, ic->i_mr, ic->i_cq);

out:
	return (ret);
}

static uint32_t
rdsv3_ib_protocol_compatible(struct rdma_cm_event *event)
{
	const struct rdsv3_ib_connect_private *dp =
	    event->param.conn.private_data;
	uint16_t common;
	uint32_t version = 0;

	RDSV3_DPRINTF2("rdsv3_ib_protocol_compatible", "Enter event: %p",
	    event);

	/*
	 * rdma_cm private data is odd - when there is any private data in the
	 * request, we will be given a pretty large buffer without telling us
	 * the
	 * original size. The only way to tell the difference is by looking at
	 * the contents, which are initialized to zero.
	 * If the protocol version fields aren't set,
	 * this is a connection attempt
	 * from an older version. This could could be 3.0 or 2.0 -
	 * we can't tell.
	 * We really should have changed this for OFED 1.3 :-(
	 */

	/* Be paranoid. RDS always has privdata */
	if (dp == NULL || !event->param.conn.private_data_len) {
		RDSV3_DPRINTF2("rdsv3_ib_protocol_compatible",
		    "RDS incoming connection has no private data, rejecting");
		return (0);
	}

	/* Even if len is crap *now* I still want to check it. -ASG */
	if (event->param.conn.private_data_len < sizeof (*dp) ||
	    dp->dp_protocol_major == 0) {
		version = RDS_PROTOCOL_3_0;
	} else {
		common = ntohs(dp->dp_protocol_minor_mask) &
		    RDSV3_IB_SUPPORTED_PROTOCOLS;
		if (dp->dp_protocol_major == 3 && common) {
			version = RDS_PROTOCOL_3_0;
			while ((common >>= 1) != 0)
				version++;
		} else {
			RDSV3_DPRINTF2("rdsv3_ib_protocol_compatible",
			    "RDS: Connection from %u.%u.%u.%u using "
			    "incompatible protocol version %u.%u\n",
			    NIPQUAD(dp->dp_saddr),
			    dp->dp_protocol_major,
			    dp->dp_protocol_minor);
		}
	}

	RDSV3_DPRINTF2("rdsv3_ib_protocol_compatible", "Return event: %p",
	    event);

	/* Solaris RDSv3 doesn't support the 3.0 protocol */
	return (version <= RDS_PROTOCOL_3_0 ? 0 : version);
}

int
rdsv3_ib_cm_handle_connect(struct rdma_cm_id *cm_id,
    struct rdma_cm_event *event)
{
	uint64_be_t lguid = cm_id->route.path_rec->sgid.global.interface_id;
	uint64_be_t fguid = cm_id->route.path_rec->dgid.global.interface_id;
	const struct rdsv3_ib_connect_private *dp =
	    event->param.conn.private_data;
	struct rdsv3_ib_connect_private dp_rep;
	struct rdsv3_connection *conn = NULL;
	struct rdsv3_ib_connection *ic = NULL;
	struct rdsv3_ip_bucket *bucketp;
	struct rdma_conn_param conn_param;
	uint32_t version;
	int err, destroy = 1;
	boolean_t conn_created = B_FALSE;

	RDSV3_DPRINTF2("rdsv3_ib_cm_handle_connect",
	    "Enter cm_id: %p event: %p", cm_id, event);

	/* Check whether the remote protocol version matches ours. */
	version = rdsv3_ib_protocol_compatible(event);
	if (!version) {
		RDSV3_DPRINTF2("rdsv3_ib_cm_handle_connect",
		    "version mismatch");
		goto out;
	}

	RDSV3_DPRINTF2("rdsv3_ib_cm_handle_connect",
	    "saddr %u.%u.%u.%u daddr %u.%u.%u.%u RDSv%d.%d lguid 0x%llx fguid "
	    "0x%llx", NIPQUAD(dp->dp_saddr), NIPQUAD(dp->dp_daddr),
	    RDS_PROTOCOL_MAJOR(version), RDS_PROTOCOL_MINOR(version),
	    (unsigned long long)ntohll(lguid),
	    (unsigned long long)ntohll(fguid));

	/* Pass global zone for now */
	bucketp = rdsv3_find_ip_bucket(dp->dp_daddr, GLOBAL_ZONEID);
	if (IS_ERR(bucketp)) {
		/* this should not happen */
		RDSV3_DPRINTF2("rdsv3_ib_cm_handle_connect",
		    "Incoming REQ with invalid IP address: %u.%u.%u.%u",
		    NIPQUAD(dp->dp_daddr));
		goto out;
	}

	conn = rdsv3_conn_create(bucketp, dp->dp_saddr, KM_NOSLEEP);
	if (IS_ERR(conn)) {
		RDSV3_DPRINTF2("rdsv3_ib_cm_handle_connect",
		    "rdsv3_conn_create failed (%ld)", PTR_ERR(conn));
		conn = NULL;
		goto out;
	}

	/*
	 * The connection request may occur while the
	 * previous connection exist, e.g. in case of failover.
	 * But as connections may be initiated simultaneously
	 * by both hosts, we have a random backoff mechanism -
	 * see the comment above rdsv3_queue_reconnect()
	 */
	mutex_enter(&conn->c_cm_lock);
	if (!rdsv3_conn_transition(conn, RDSV3_CONN_DOWN,
	    RDSV3_CONN_CONNECTING)) {
		if (rdsv3_conn_state(conn) == RDSV3_CONN_UP) {
			RDSV3_DPRINTF2("rdsv3_ib_cm_handle_connect",
			    "incoming connect when connected: %p",
			    conn);
			rdsv3_conn_drop(conn);
			rdsv3_ib_stats_inc(s_ib_listen_closed_stale);
			mutex_exit(&conn->c_cm_lock);
			goto out;
		} else if (rdsv3_conn_state(conn) == RDSV3_CONN_CONNECTING) {
			/* Wait and see - our connect may still be succeeding */
			RDSV3_DPRINTF2("rdsv3_ib_cm_handle_connect",
			    "peer-to-peer connection request: %p, "
			    "lguid: 0x%llx fguid: 0x%llx",
			    conn, lguid, fguid);
			rdsv3_ib_stats_inc(s_ib_connect_raced);
		}
		mutex_exit(&conn->c_cm_lock);
		goto out;
	}

	ic = conn->c_transport_data;

	rdsv3_ib_set_protocol(conn, version);
	rdsv3_ib_set_flow_control(conn, ntohl(dp->dp_credit));

	/*
	 * If the peer gave us the last packet it saw, process this as if
	 * we had received a regular ACK.
	 */
	if (dp->dp_ack_seq)
		rdsv3_send_drop_acked(conn, ntohll(dp->dp_ack_seq), NULL);

	ASSERT(!cm_id->context);
	ASSERT(!ic->i_cm_id);

	if (ic->i_cm_id != NULL)
		RDSV3_PANIC();

	ic->i_cm_id = cm_id;
	cm_id->context = conn;

	/*
	 * We got halfway through setting up the ib_connection, if we
	 * fail now, we have to take the long route out of this mess.
	 */
	destroy = 0;

	err = rdsv3_ib_setup_qp(conn);
	if (err) {
		RDSV3_DPRINTF2("rdsv3_ib_cm_handle_connect",
		    "rdsv3_ib_setup_qp failed (%d)", err);
		mutex_exit(&conn->c_cm_lock);
		rdsv3_conn_drop(conn);
		goto out;
	}

	rdsv3_ib_cm_fill_conn_param(conn, &conn_param, &dp_rep, version,
	    event->param.conn.responder_resources,
	    event->param.conn.initiator_depth);

	/* rdma_accept() calls rdma_reject() internally if it fails */
	err = rdma_accept(cm_id, &conn_param);
	mutex_exit(&conn->c_cm_lock);
	if (err) {
		RDSV3_DPRINTF2("rdsv3_ib_cm_handle_connect",
		    "rdma_accept failed (%d)", err);
		rdsv3_conn_drop(conn);
		goto out;
	}

	RDSV3_DPRINTF2("rdsv3_ib_cm_handle_connect",
	    "Return cm_id: %p event: %p", cm_id, event);

	return (0);

out:
	(void) rdma_reject(cm_id, NULL, 0);
	return (destroy);
}


int
rdsv3_ib_cm_initiate_connect(struct rdma_cm_id *cm_id)
{
	struct rdsv3_connection *conn = cm_id->context;
	struct rdsv3_ib_connection *ic = conn->c_transport_data;
	struct rdma_conn_param conn_param;
	struct rdsv3_ib_connect_private dp;
	int ret;

	RDSV3_DPRINTF2("rdsv3_ib_cm_initiate_connect", "Enter: cm_id: %p",
	    cm_id);

	/*
	 * If the peer doesn't do protocol negotiation, we must
	 * default to RDSv3.1 on Solaris RDSv3.
	 */
	rdsv3_ib_set_protocol(conn, RDS_PROTOCOL_3_1);
	ic->i_flowctl =
	    rdsv3_ib_sysctl_flow_control;	/* advertise flow control */

	ret = rdsv3_ib_setup_qp(conn);
	if (ret) {
		RDSV3_DPRINTF2("rdsv3_ib_cm_initiate_connect",
		    "rdsv3_ib_setup_qp failed (%d)", ret);
		rdsv3_conn_drop(conn);
		goto out;
	}

	rdsv3_ib_cm_fill_conn_param(conn, &conn_param, &dp,
	    RDS_PROTOCOL_VERSION, UINT_MAX, UINT_MAX);

	ret = rdma_connect(cm_id, &conn_param);
	if (ret) {
		RDSV3_DPRINTF2("rdsv3_ib_cm_initiate_connect",
		    "rdma_connect failed (%d)", ret);
		rdsv3_conn_drop(conn);
	}

	RDSV3_DPRINTF2("rdsv3_ib_cm_initiate_connect",
	    "Return: cm_id: %p", cm_id);

out:
	/*
	 * Beware - returning non-zero tells the rdma_cm to destroy
	 * the cm_id. We should certainly not do it as long as we still
	 * "own" the cm_id.
	 */
	if (ret) {
		if (ic->i_cm_id == cm_id)
			ret = 0;
	}
	return (ret);
}

int
rdsv3_ib_conn_connect(struct rdsv3_connection *conn)
{
	struct rdsv3_ib_connection *ic = conn->c_transport_data;
	struct sockaddr_in src, dest;
	ipaddr_t	laddr, faddr;
	int ret;

	RDSV3_DPRINTF2("rdsv3_ib_conn_connect", "Enter: conn: %p", conn);

	/*
	 * XXX I wonder what affect the port space has
	 */
	/* delegate cm event handler to rdma_transport */
	ic->i_cm_id = rdma_create_id(rdsv3_rdma_cm_event_handler, conn,
	    RDMA_PS_TCP);
	if (IS_ERR(ic->i_cm_id)) {
		ret = PTR_ERR(ic->i_cm_id);
		ic->i_cm_id = NULL;
		RDSV3_DPRINTF2("rdsv3_ib_conn_connect",
		    "rdma_create_id() failed: %d", ret);
		goto out;
	}

	RDSV3_DPRINTF3("rdsv3_ib_conn_connect",
	    "created cm id %p for conn %p", ic->i_cm_id, conn);

	/* The ipaddr should be in the network order */
	laddr = conn->c_laddr;
	faddr = conn->c_faddr;
	ret = rdsv3_sc_path_lookup(&laddr, &faddr);
	if (ret == 0) {
		RDSV3_DPRINTF2(LABEL, "Path not found (0x%x 0x%x)",
		    ntohl(laddr), ntohl(faddr));
	}

	src.sin_family = AF_INET;
	src.sin_addr.s_addr = (uint32_t)laddr;
	src.sin_port = (uint16_t)htons(0);

	dest.sin_family = AF_INET;
	dest.sin_addr.s_addr = (uint32_t)faddr;
	dest.sin_port = (uint16_t)htons(RDSV3_PORT);

	ret = rdma_resolve_addr(ic->i_cm_id, (struct sockaddr *)&src,
	    (struct sockaddr *)&dest,
	    RDSV3_RDMA_RESOLVE_TIMEOUT_MS);
	if (ret) {
		RDSV3_DPRINTF2("rdsv3_ib_conn_connect",
		    "addr resolve failed for cm id %p: %d", ic->i_cm_id, ret);
		rdma_destroy_id(ic->i_cm_id);
		ic->i_cm_id = NULL;
	}

	RDSV3_DPRINTF2("rdsv3_ib_conn_connect", "Return: conn: %p", conn);

out:
	return (ret);
}

/*
 * This is so careful about only cleaning up resources that were built up
 * so that it can be called at any point during startup.  In fact it
 * can be called multiple times for a given connection.
 */
void
rdsv3_ib_conn_shutdown(struct rdsv3_connection *conn)
{
	struct rdsv3_ib_connection *ic = conn->c_transport_data;
	int err = 0;

	RDSV3_DPRINTF2("rdsv3_ib_conn_shutdown",
	    "cm %p pd %p cq %p qp %p", ic->i_cm_id,
	    ic->i_pd, ic->i_cq, ic->i_cm_id ? ic->i_cm_id->qp : NULL);

	if (ic->i_cm_id) {
		struct ib_device *dev = NULL;

		/* set the device pointer for this HCA */
		if (ic->rds_ibdev != NULL) {
			dev = ic->rds_ibdev->dev;
		}

		RDSV3_DPRINTF2("rdsv3_ib_conn_shutdown",
		    "disconnecting cm %p", ic->i_cm_id);
		err = rdma_disconnect(ic->i_cm_id);
		if (err) {
			/*
			 * Actually this may happen quite frequently, when
			 * an outgoing connect raced with an incoming connect.
			 */
			RDSV3_DPRINTF2("rdsv3_ib_conn_shutdown",
			    "failed to disconnect, cm: %p err %d",
			    ic->i_cm_id, err);
		}

		if (ic->i_cm_id->qp) {
			(void) ibt_flush_qp(
			    ib_get_ibt_channel_hdl(ic->i_cm_id));

			/*
			 * Don't wait for the send ring to be empty -- there
			 * may be completed non-signaled entries sitting on
			 * there. We unmap these below.
			 */
			rdsv3_wait_event(&ic->i_recv_ring.w_empty_wait,
			    rdsv3_ib_ring_empty(&ic->i_recv_ring));

			/*
			 * Note that Linux original code calls
			 * rdma_destroy_qp() after rdsv3_ib_recv_clear_ring(ic).
			 */
			rdma_destroy_qp(ic->i_cm_id);
		}

		/* first destroy the ib state that generates callbacks */
		if (rdsv3_enable_snd_cq) {
			if (ic->i_snd_cq)
				(void) ib_destroy_cq(ic->i_snd_cq);
		}
		if (ic->i_cq)
			(void) ib_destroy_cq(ic->i_cq);
		rdma_destroy_id(ic->i_cm_id);

		if (rdsv3_enable_snd_cq) {
			if (ic->i_snd_soft_cq) {
				rdsv3_af_thr_destroy(ic->i_snd_soft_cq);
				ic->i_snd_soft_cq = NULL;
			}
		}
		if (ic->i_soft_cq) {
			rdsv3_af_thr_destroy(ic->i_soft_cq);
			ic->i_soft_cq = NULL;
		}
		if (ic->i_refill_rq) {
			rdsv3_af_thr_destroy(ic->i_refill_rq);
			ic->i_refill_rq = NULL;
		}

		if (ic->i_mr) {
			ASSERT(dev != NULL);
			rdsv3_ib_free_hdrs(dev, ic);
		}

		if (ic->i_sends)
			rdsv3_ib_send_clear_ring(ic);
		if (ic->i_recvs)
			rdsv3_ib_recv_clear_ring(ic);

		/*
		 * Move connection back to the nodev list.
		 */
		if (ic->i_on_dev_list)
			rdsv3_ib_remove_conn(ic->rds_ibdev, conn);

		ic->i_cm_id = NULL;
		ic->i_pd = NULL;
		ic->i_mr = NULL;
		ic->i_cq = NULL;
		ic->i_snd_cq = NULL;
		ic->i_send_hdrs = NULL;
		ic->i_recv_hdrs = NULL;
		ic->i_ack = NULL;
	}
	ASSERT(!ic->i_on_dev_list);

	/* Clear the ACK state */
	clear_bit(IB_ACK_IN_FLIGHT, &ic->i_ack_flags);
	ic->i_ack_next = 0;
	ic->i_ack_recv = 0;

	/* Clear flow control state */
	ic->i_flowctl = 0;
	ic->i_credits = 0;

	rdsv3_ib_ring_init(&ic->i_send_ring,
	    rdsv3_ib_sysctl_max_send_wr + rdsv3_ib_sysctl_max_unsig_wrs);
	rdsv3_ib_ring_init(&ic->i_recv_ring, rdsv3_ib_sysctl_max_recv_wr);

	if (ic->i_ibinc) {
		rdsv3_inc_put(&ic->i_ibinc->ii_inc);
		ic->i_ibinc = NULL;
	}

	if (ic->i_sends) {
		kmem_free(ic->i_sends,
		    ic->i_send_ring.w_nr * sizeof (struct rdsv3_ib_send_work));
		ic->i_sends = NULL;
	}
	if (ic->i_send_wrs) {
		kmem_free(ic->i_send_wrs, ic->i_send_ring.w_nr *
		    (sizeof (ibt_send_wr_t) +
		    RDSV3_IB_MAX_SGE * sizeof (ibt_wr_ds_t)));
		ic->i_send_wrs = NULL;
	}
	if (ic->i_recvs) {
		kmem_free(ic->i_recvs,
		    ic->i_recv_ring.w_nr * sizeof (struct rdsv3_ib_recv_work));
		ic->i_recvs = NULL;
	}
	if (ic->i_recv_wrs) {
		kmem_free(ic->i_recv_wrs, ic->i_recv_ring.w_nr *
		    (sizeof (ibt_recv_wr_t)));
		ic->i_recv_wrs = NULL;
	}
	RDSV3_DPRINTF2("rdsv3_ib_conn_shutdown", "Return conn: %p", conn);
}

/* ARGSUSED */
int
rdsv3_ib_conn_alloc(struct rdsv3_connection *conn, int gfp)
{
	struct rdsv3_ib_connection *ic;

	RDSV3_DPRINTF2("rdsv3_ib_conn_alloc", "conn: %p", conn);

	/* XXX too lazy? */
	ic = kmem_zalloc(sizeof (struct rdsv3_ib_connection), gfp);
	if (!ic)
		return (-ENOMEM);

	list_link_init(&ic->ib_node);

	mutex_init(&ic->i_recv_mutex, NULL, MUTEX_DRIVER, NULL);
	mutex_init(&ic->i_ack_lock, NULL, MUTEX_DRIVER, NULL);

	/*
	 * rdsv3_ib_conn_shutdown() waits for these to be emptied so they
	 * must be initialized before it can be called.
	 */
	rdsv3_ib_ring_init(&ic->i_send_ring,
	    rdsv3_ib_sysctl_max_send_wr + rdsv3_ib_sysctl_max_unsig_wrs);
	rdsv3_ib_ring_init(&ic->i_recv_ring, rdsv3_ib_sysctl_max_recv_wr);

	ic->conn = conn;
	conn->c_transport_data = ic;

	mutex_enter(&ib_nodev_conns_lock);
	list_insert_tail(&ib_nodev_conns, ic);
	mutex_exit(&ib_nodev_conns_lock);

	RDSV3_DPRINTF2("rdsv3_ib_conn_alloc", "conn %p conn ic %p",
	    conn, conn->c_transport_data);
	return (0);
}

/*
 * Free a connection. Connection must be shut down and not set for reconnect.
 */
void
rdsv3_ib_conn_free(void *arg)
{
	struct rdsv3_ib_connection *ic = arg;
	kmutex_t	*lock_ptr;

	RDSV3_DPRINTF2("rdsv3_ib_conn_free", "ic %p\n", ic);

#ifndef __lock_lint
	/*
	 * Conn is either on a dev's list or on the nodev list.
	 * A race with shutdown() or connect() would cause problems
	 * (since rds_ibdev would change) but that should never happen.
	 */
	lock_ptr = ic->i_on_dev_list ?
	    &ic->rds_ibdev->spinlock : &ib_nodev_conns_lock;

	mutex_enter(lock_ptr);
	list_remove_node(&ic->ib_node);
	mutex_exit(lock_ptr);
#endif
	kmem_free(ic, sizeof (*ic));
}

/*
 * An error occurred on the connection
 */
void
__rdsv3_ib_conn_error(struct rdsv3_connection *conn)
{
	rdsv3_conn_drop(conn);
}
