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
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This file implements the Directed Route (DR) loopback support in IBMF.
 */

#include <sys/ib/mgt/ibmf/ibmf_impl.h>
#include <sys/ib/mgt/ib_mad.h>

#define	MELLANOX_VENDOR	0x15b3

extern int ibmf_trace_level;

static int ibmf_i_dr_loopback_filter(ibmf_client_t *clientp,
    ibmf_msg_impl_t *msgimplp, int blocking);
static void ibmf_i_dr_loopback_term(ibmf_client_t *clientp,
    ibmf_msg_impl_t *msgimplp, int blocking);

/*
 * ibmf_i_check_for_loopback():
 *	Check for DR loopback traffic
 */
int
ibmf_i_check_for_loopback(ibmf_msg_impl_t *msgimplp, ibmf_msg_cb_t msg_cb,
    void *msg_cb_args, ibmf_retrans_t *retrans, boolean_t *loopback)
{
	sm_dr_mad_hdr_t	*dr_hdr;
	boolean_t	blocking;
	int		status;
	ibmf_ci_t	*cip = ((ibmf_client_t *)msgimplp->im_client)->ic_myci;


	*loopback = B_FALSE;
	dr_hdr = (sm_dr_mad_hdr_t *)msgimplp->im_msgbufs_send.im_bufs_mad_hdr;

	/*
	 * Some HCAs do not handle directed route loopback MADs.
	 * Such MADs are sent out on the wire instead of being looped back.
	 * This behavior causes the SM to hang since the SM starts
	 * its sweep with loopback DR MADs.
	 * This ibmf workaround does the loopback without passing the MAD
	 * into the transport layer.
	 * We should really check a property of the hardware to determine
	 * whether or not an IB HCA can "hear" itself rather than
	 * checking for specific HCAs or vendor of HCAs.
	 */
	if ((dr_hdr->MgmtClass == MAD_MGMT_CLASS_SUBN_DIRECT_ROUTE) &&
	    (dr_hdr->HopCount == 0) && (cip->ci_vendor_id == MELLANOX_VENDOR)) {
		if (msg_cb == NULL) {
			blocking = B_TRUE;
		} else {
			blocking = B_FALSE;
		}

		ibmf_i_init_msg(msgimplp, msg_cb, msg_cb_args, retrans,
		    blocking);

		status = ibmf_i_dr_loopback_filter(msgimplp->im_client,
		    msgimplp, blocking);
		if (status != IBMF_SUCCESS) {
			return (status);
		}

		*loopback = B_TRUE;
	}


	return (IBMF_SUCCESS);

}

/*
 * ibmf_i_dr_loopback_term():
 *	Perform termination processing of a DR loopback transaction
 */
static void
ibmf_i_dr_loopback_term(ibmf_client_t *clientp, ibmf_msg_impl_t *msgimplp,
    int blocking)
{
	uint_t refcnt;


	mutex_enter(&msgimplp->im_mutex);

	if (blocking) {
		/*
		 * For sequenced, and blocking transactions, we wait for
		 * the response. For non-sequenced, and blocking transactions,
		 * we are done since the send has completed (no send completion
		 * as when calling into IBTF).
		 */
		if ((msgimplp->im_flags & IBMF_MSG_FLAGS_SEQUENCED) &&
		    ((msgimplp->im_trans_state_flags &
		    IBMF_TRANS_STATE_FLAG_SIGNALED) == 0)) {

			msgimplp->im_trans_state_flags |=
			    IBMF_TRANS_STATE_FLAG_WAIT;


			cv_wait(&msgimplp->im_trans_cv, &msgimplp->im_mutex);

			msgimplp->im_trans_state_flags &=
			    ~IBMF_TRANS_STATE_FLAG_WAIT;

			msgimplp->im_flags &= ~IBMF_MSG_FLAGS_BUSY;

			mutex_exit(&msgimplp->im_mutex);

		} else if ((msgimplp->im_flags &
		    IBMF_MSG_FLAGS_SEQUENCED) == 0) {

			msgimplp->im_trans_state_flags |=
			    IBMF_TRANS_STATE_FLAG_DONE;
			msgimplp->im_flags &= ~IBMF_MSG_FLAGS_BUSY;

			mutex_exit(&msgimplp->im_mutex);

			ibmf_i_client_rem_msg(clientp, msgimplp, &refcnt);
		} else {

			msgimplp->im_flags &= ~IBMF_MSG_FLAGS_BUSY;
			mutex_exit(&msgimplp->im_mutex);
		}

	} else if ((msgimplp->im_flags & IBMF_MSG_FLAGS_SEQUENCED) == 0) {

		msgimplp->im_trans_state_flags |= IBMF_TRANS_STATE_FLAG_DONE;
		msgimplp->im_flags &= ~IBMF_MSG_FLAGS_BUSY;
		mutex_exit(&msgimplp->im_mutex);

		ibmf_i_client_rem_msg(clientp, msgimplp, &refcnt);

		if (msgimplp->im_trans_cb) {
			msgimplp->im_trans_cb((ibmf_handle_t)clientp,
			    (ibmf_msg_t *)msgimplp, msgimplp->im_trans_cb_arg);
		}
	} else {

		msgimplp->im_flags &= ~IBMF_MSG_FLAGS_BUSY;
		mutex_exit(&msgimplp->im_mutex);
	}


}

static void
ibmf_i_dr_init_recbuf(ibmf_msg_impl_t *msgimplp)
{
	uint32_t	clhdrsz, clhdroff;
	uchar_t		*msgbufp;
	size_t		offset;

	ibmf_i_mgt_class_to_hdr_sz_off(MAD_MGMT_CLASS_SUBN_DIRECT_ROUTE,
	    &clhdrsz, &clhdroff);
	msgbufp = (uchar_t *)msgimplp->im_msgbufs_recv.im_bufs_mad_hdr;

	/* Get the offset of the class header */
	offset = sizeof (ib_mad_hdr_t) + clhdroff;

	/* initialize class header pointer */
	if (clhdrsz == 0) {
		msgimplp->im_msgbufs_recv.im_bufs_cl_hdr = NULL;
	} else {
		msgimplp->im_msgbufs_recv.im_bufs_cl_hdr =
		    (void *)(msgbufp + offset);
	}
	msgimplp->im_msgbufs_recv.im_bufs_cl_hdr_len = clhdrsz;
	offset += clhdrsz;

	/* initialize data area pointer */
	msgimplp->im_msgbufs_recv.im_bufs_cl_data = (void *)(msgbufp + offset);
	msgimplp->im_msgbufs_recv.im_bufs_cl_data_len =
	    IBMF_MAD_SIZE - sizeof (ib_mad_hdr_t) - clhdroff - clhdrsz;
}

/*
 * ibmf_i_dr_loopback_filter():
 * This function intercepts Directed Route MADs with zero hop count,
 * or loopback DR MADs. If the MAD is outbound from the SM, the SMA's
 * client handle is located, and the receive callback invoked.
 * If the MAD is outbound from the SMA, the SM's client handle is located
 * and the receive callback invoked.
 *
 * This filtering is needed for some HCAs where the SMA cannot handle DR
 * MAD's that need to be treated as a loopback MAD. On these HCAs, we see
 * the zero hopcount MAD being sent out on the wire which it should not.
 */
static int
ibmf_i_dr_loopback_filter(ibmf_client_t *clientp, ibmf_msg_impl_t *msgimplp,
    int blocking)
{
	ibmf_client_t	*rclientp;
	sm_dr_mad_hdr_t	*dr_hdr;
	ibmf_msg_impl_t	*rmsgimplp;
	boolean_t	rbuf_alloced;
	int		msg_trans_state_flags, msg_flags;
	uint_t		ref_cnt;
	int		ret;
	uint16_t	attr_id;


	dr_hdr = (sm_dr_mad_hdr_t *)msgimplp->im_msgbufs_send.im_bufs_mad_hdr;
	attr_id = b2h16(dr_hdr->AttributeID);

	/* set transaction flag for a sequenced transaction */
	if (msgimplp->im_transp_op_flags & IBMF_MSG_TRANS_FLAG_SEQ)
		msgimplp->im_flags |= IBMF_MSG_FLAGS_SEQUENCED;

	/*
	 * If the DR SMP method is a Get or a Set, the target is the SMA, else,
	 * if the method is a GetResponse, the target is the SM. If the
	 * Attribute is SMInfo, the target is always the SM.
	 */
	if ((((dr_hdr->R_Method == MAD_METHOD_GET) ||
	    (dr_hdr->R_Method == MAD_METHOD_SET)) &&
	    (attr_id != SM_SMINFO_ATTRID)) ||
	    (dr_hdr->R_Method == MAD_METHOD_TRAP_REPRESS)) {

		ret = ibmf_i_lookup_client_by_mgmt_class(clientp->ic_myci,
		    clientp->ic_client_info.port_num, SUBN_AGENT, &rclientp);
		if (ret != IBMF_SUCCESS) {
			return (ret);
		}

	} else if ((dr_hdr->R_Method == MAD_METHOD_GET_RESPONSE) ||
	    (dr_hdr->R_Method == MAD_METHOD_TRAP) ||
	    (attr_id == SM_SMINFO_ATTRID)) {

		ret = ibmf_i_lookup_client_by_mgmt_class(clientp->ic_myci,
		    clientp->ic_client_info.port_num, SUBN_MANAGER, &rclientp);
		if (ret != IBMF_SUCCESS) {
			return (ret);
		}
		/*
		 * This is an IBMF client that is an SM, that sent
		 * a loopback DR MAD to us requesting the SMInfo
		 * Attribute. This makes no sense fail the request.
		 */
		if ((rclientp == clientp) && (attr_id == SM_SMINFO_ATTRID)) {
			return (IBMF_FAILURE);
		}
	} else {

		return (IBMF_FAILURE);
	}

	/*
	 * Initialize the Transaction ID and Mgmt Class fields in the
	 * message context.
	 * NOTE: The IB MAD header in the incoming MAD is in wire (big-endian)
	 * format and needs to be converted to the host endian format where
	 * applicable (multi-byte fields)
	 */
	msgimplp->im_tid	= b2h64(dr_hdr->TransactionID);
	msgimplp->im_mgt_class 	= dr_hdr->MgmtClass;

	/*
	 * Find the message context in the target client corresponding to the
	 * transaction ID and management class in the source message context
	 */
	rmsgimplp = ibmf_i_find_msg(rclientp, msgimplp->im_tid,
	    dr_hdr->MgmtClass, dr_hdr->R_Method,
	    msgimplp->im_local_addr.ia_remote_lid, NULL, B_FALSE, NULL,
	    IBMF_REG_MSG_LIST);

	if (rmsgimplp != NULL) {

		mutex_enter(&rmsgimplp->im_mutex);

		/*
		 * If the message has been marked unitialized or done
		 * release the message mutex and return
		 */
		if ((rmsgimplp->im_trans_state_flags &
		    IBMF_TRANS_STATE_FLAG_DONE) ||
		    (rmsgimplp->im_trans_state_flags &
		    IBMF_TRANS_STATE_FLAG_UNINIT)) {
			IBMF_MSG_DECR_REFCNT(rmsgimplp);
			msg_trans_state_flags = rmsgimplp->im_trans_state_flags;
			msg_flags = rmsgimplp->im_flags;
			ref_cnt = rmsgimplp->im_ref_count;
			mutex_exit(&rmsgimplp->im_mutex);
			/*
			 * This thread may notify the client only if the
			 * transaction is done, the message has been removed
			 * from the client's message list, and the message
			 * reference count is 0.
			 * If the transaction is done, and the message reference
			 * count = 0, there is still a possibility that a
			 * packet could arrive for the message and its reference
			 * count increased if the message is still on the list.
			 * If the message is still on the list, it will be
			 * removed by a call to ibmf_i_client_rem_msg() at
			 * the completion point of the transaction.
			 * So, the reference count should be checked after the
			 * message has been removed.
			 */
			if ((msg_trans_state_flags &
			    IBMF_TRANS_STATE_FLAG_DONE) &&
			    !(msg_flags & IBMF_MSG_FLAGS_ON_LIST) &&
			    (ref_cnt == 0)) {
				ibmf_i_dr_init_recbuf(rmsgimplp);
				ibmf_i_notify_client(rmsgimplp);
			}
			return (IBMF_FAILURE);
		}
	} else {

		_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*rmsgimplp))

		/* This is an unsolicited message */

		rmsgimplp = (ibmf_msg_impl_t *)kmem_zalloc(
		    sizeof (ibmf_msg_impl_t), KM_NOSLEEP);
		if (rmsgimplp == NULL) {
			return (IBMF_NO_RESOURCES);
		}

		mutex_init(&rmsgimplp->im_mutex, NULL, MUTEX_DRIVER, NULL);

		rmsgimplp->im_client	= rclientp;
		rmsgimplp->im_qp_hdl	= msgimplp->im_qp_hdl;
		rmsgimplp->im_unsolicited = B_TRUE;
		rmsgimplp->im_tid 	= b2h64(dr_hdr->TransactionID);
		rmsgimplp->im_mgt_class	= dr_hdr->MgmtClass;

		/* indicate the client callback is active */
		if (rmsgimplp->im_qp_hdl == IBMF_QP_HANDLE_DEFAULT) {
			mutex_enter(&rclientp->ic_mutex);
			IBMF_RECV_CB_SETUP(rclientp);
			mutex_exit(&rclientp->ic_mutex);
		} else {
			ibmf_alt_qp_t *qpp;

			qpp = (ibmf_alt_qp_t *)rmsgimplp->im_qp_hdl;
			mutex_enter(&qpp->isq_mutex);
			IBMF_ALT_RECV_CB_SETUP(qpp);
			mutex_exit(&qpp->isq_mutex);
		}

		/* Increment the message reference count */
		IBMF_MSG_INCR_REFCNT(rmsgimplp);
		rmsgimplp->im_trans_state_flags = IBMF_TRANS_STATE_FLAG_UNINIT;

		_NOTE(NOW_VISIBLE_TO_OTHER_THREADS(*rmsgimplp))

		/* add message to client's list; will acquire im_mutex */
		ibmf_i_client_add_msg(rclientp, rmsgimplp);

		mutex_enter(&rmsgimplp->im_mutex);

		/* no one should have touched our state */
		ASSERT(rmsgimplp->im_trans_state_flags ==
		    IBMF_TRANS_STATE_FLAG_UNINIT);

		/* transition out of uninit state */
		rmsgimplp->im_trans_state_flags = IBMF_TRANS_STATE_FLAG_INIT;
	}

	/* Allocate memory for the receive buffers */
	if (rmsgimplp->im_msgbufs_recv.im_bufs_mad_hdr == NULL) {
		rmsgimplp->im_msgbufs_recv.im_bufs_mad_hdr =
		    (ib_mad_hdr_t *)kmem_zalloc(IBMF_MAD_SIZE, KM_NOSLEEP);
		if (rmsgimplp->im_msgbufs_recv.im_bufs_mad_hdr == NULL) {
			IBMF_MSG_DECR_REFCNT(rmsgimplp);
			mutex_exit(&rmsgimplp->im_mutex);
			kmem_free(rmsgimplp, sizeof (ibmf_msg_impl_t));
			return (IBMF_NO_RESOURCES);
		}
		rbuf_alloced = B_TRUE;
	}

	/* Copy the send buffers into the receive buffers */

	/* Copy the MAD header */
	bcopy((void *)msgimplp->im_msgbufs_send.im_bufs_mad_hdr,
	    (void *)rmsgimplp->im_msgbufs_recv.im_bufs_mad_hdr,
	    sizeof (ib_mad_hdr_t));

	/*
	 * Copy the management class header
	 * For DR MADs, class header is of size 40 bytes and start
	 * right after the MAD header.
	 */
	rmsgimplp->im_msgbufs_recv.im_bufs_cl_hdr =
	    (uchar_t *)rmsgimplp->im_msgbufs_recv.im_bufs_mad_hdr +
	    sizeof (ib_mad_hdr_t);
	rmsgimplp->im_msgbufs_recv.im_bufs_cl_hdr_len =
	    msgimplp->im_msgbufs_send.im_bufs_cl_hdr_len;
	bcopy((void *)msgimplp->im_msgbufs_send.im_bufs_cl_hdr,
	    (void *)rmsgimplp->im_msgbufs_recv.im_bufs_cl_hdr,
	    msgimplp->im_msgbufs_send.im_bufs_cl_hdr_len);

	/* Copy the management class data */
	rmsgimplp->im_msgbufs_recv.im_bufs_cl_data =
	    (uchar_t *)rmsgimplp->im_msgbufs_recv.im_bufs_mad_hdr +
	    sizeof (ib_mad_hdr_t) +
	    rmsgimplp->im_msgbufs_recv.im_bufs_cl_hdr_len;
	rmsgimplp->im_msgbufs_recv.im_bufs_cl_data_len =
	    msgimplp->im_msgbufs_send.im_bufs_cl_data_len;
	bcopy((void *)msgimplp->im_msgbufs_send.im_bufs_cl_data,
	    (void *)rmsgimplp->im_msgbufs_recv.im_bufs_cl_data,
	    msgimplp->im_msgbufs_send.im_bufs_cl_data_len);

	/* Copy the global address information from the source message */
	bcopy((void *)&msgimplp->im_global_addr,
	    (void *)&rmsgimplp->im_global_addr,
	    sizeof (ibmf_global_addr_info_t));

	/* Copy the local address information from the source message */
	bcopy((void *)&msgimplp->im_local_addr,
	    (void *)&rmsgimplp->im_local_addr,
	    sizeof (ibmf_addr_info_t));

	/*
	 * Call the receive callback for the agent/manager the packet is
	 * destined for.
	 */
	rmsgimplp->im_trans_state_flags |= IBMF_TRANS_STATE_FLAG_DONE;

	/*
	 * Decrement the message reference count
	 * This count was incremented either when the message was found
	 * on the client's message list (ibmf_i_find_msg()) or when
	 * a new message was created for unsolicited data
	 */
	IBMF_MSG_DECR_REFCNT(rmsgimplp);

	mutex_exit(&rmsgimplp->im_mutex);

	if (rbuf_alloced) {
		mutex_enter(&clientp->ic_kstat_mutex);
		IBMF_ADD32_KSTATS(clientp, recv_bufs_alloced, 1);
		mutex_exit(&clientp->ic_kstat_mutex);
	}

	/* add the source message to the source client's list */
	ibmf_i_client_add_msg(clientp, msgimplp);

	/* remove the destination message from the list */
	ibmf_i_client_rem_msg(rclientp, rmsgimplp, &ref_cnt);

	/*
	 * Notify the client if the message reference count is zero.
	 * At this point, we know that the transaction is done and
	 * the message has been removed from the client's message list.
	 * So, we only need to make sure the reference count is zero
	 * before notifying the client.
	 */
	if (ref_cnt == 0) {
		ibmf_i_dr_init_recbuf(rmsgimplp);
		ibmf_i_notify_client(rmsgimplp);
	}

	/* perform source client transaction termination processing */
	ibmf_i_dr_loopback_term(clientp, msgimplp, blocking);


	return (IBMF_SUCCESS);
}
