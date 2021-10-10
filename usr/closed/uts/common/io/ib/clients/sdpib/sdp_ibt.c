/*
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * LEGAL NOTICE
 *
 * This file contains source code that implements the Sockets Direct
 * Protocol (SDP) as defined by the InfiniBand Architecture Specification,
 * Volume 1, Annex A4, Version 1.1.  Due to restrictions in the SDP license,
 * source code contained in this file may not be distributed outside of
 * Sun Microsystems without further legal review to ensure compliance with
 * the license terms.
 *
 * Sun employees and contactors are cautioned not to extract source code
 * from this file and use it for other purposes.  The SDP implementation
 * code in this and other files must be kept separate from all other source
 * code.
 *
 * As required by the license, the following notice is added to the source
 * code:
 *
 * This source code may incorporate intellectual property owned by
 * Microsoft Corporation.  Our provision of this source code does not
 * include any licenses or any other rights to you under any Microsoft
 * intellectual property.  If you would like a license from Microsoft
 * (e.g., to rebrand, redistribute), you need to contact Microsoft
 * directly.
 */

#include <sys/ib/clients/sdpib/sdp_main.h>
#include <sys/ib/clients/sdpib/sdp_trace_util.h>

/*
 * open an HCA.
 * the IBTF hca handle is returned from the open.
 */

static ibt_status_t
sdp_hca_init(sdp_state_t *state, sdp_dev_hca_t *hcap)
{
	ibt_status_t	status;
	sdp_dev_port_t	*portp;
	uint8_t		index = 1; /* port->index */
	uint_t		num_ports = 0;
	uint_t		port_size;
	ibt_hca_portinfo_t *port_infop;
	ibt_hca_attr_t	hca_attr;
	ibt_clnt_hdl_t	sdp_ibt_hdl = state->sdp_ibt_hdl;
	ibt_srv_hdl_t	srv_hdl = state->sdp_ibt_srv_hdl;
	ibt_cq_sched_attr_t cq_sched_attr;
	ibt_sched_hdl_t cq_sched_hdl;

	status = ibt_open_hca(sdp_ibt_hdl, hcap->guid, &hcap->hca_hdl);
	if (status != IBT_SUCCESS) {
		/* report error and exit */
		if (status == IBT_HCA_IN_USE) {
			sdp_print_warn(NULL, "sdp_hca_init:ibt_open_hca() "
			    "failed: status <%d> (IBT_HCA_IN_USE)\n", status);
		} else {
			sdp_print_warn(NULL, "sdp_hca_init:ibt_open_hca() "
			    "failed: status <%d>\n", status);
		}
		return (status);
	}

	/*
	 * allocate a protection domain to this HCA. The PD will be inherited
	 * by each connection that uses the HCA.
	 */
	status = ibt_alloc_pd(hcap->hca_hdl, IBT_PD_NO_FLAGS, &hcap->pd_hdl);
	if (status != IBT_SUCCESS) {
		/* report error and exit */
		sdp_print_warn(NULL, "sdp_hca_init: "
		    "ibt_alloc_pd() fail <%d>", status);
		/*
		 * need to release all resources acquired to this point!
		 */
		(void) ibt_close_hca(hcap->hca_hdl);
		return (status);
	}

	status = ibt_query_hca(hcap->hca_hdl, &hca_attr);
	if (status == IBT_SUCCESS) {
		status = ibt_query_hca_ports(hcap->hca_hdl, 0, &port_infop,
		    &num_ports, &port_size);
	}
	if (status != IBT_SUCCESS) {
		(void) ibt_free_pd(hcap->hca_hdl, hcap->pd_hdl);
		(void) ibt_close_hca(hcap->hca_hdl);
		return (status);
	}

	/* Use reserved lkey if it is supported and buff_size is within limit */
	if ((hca_attr.hca_flags2 & IBT_HCA2_RES_LKEY) &&
	    (state->sdp_buff_size <= SDP_LKEY_MAX_BUFF_SIZE)) {
		hcap->hca_use_reserved_lkey = 1;
		ASSERT(state->sdp_buff_size != 0);
		/*
		 * Account for non aligned physical page at start and end.
		 */
		hcap->hca_nds =
		    ((state->sdp_buff_size + PAGESIZE - 1) >> PAGESHIFT) + 1;
		ASSERT(hcap->hca_nds <= SDP_QP_LIMIT_SG_MAX);
	} else {
		hcap->hca_use_reserved_lkey = 0;
		hcap->hca_nds = 1;
	}

	cq_sched_attr.cqs_flags = IBT_CQS_SCHED_GROUP;
	cq_sched_attr.cqs_pool_name = SDPIB_STR_NAME;
	status = ibt_alloc_cq_sched(hcap->hca_hdl, &cq_sched_attr,
	    &cq_sched_hdl);
	if (status != IBT_SUCCESS) {
		hcap->sdp_hca_cq_sched_hdl = NULL;
	} else {
		hcap->sdp_hca_cq_sched_hdl = cq_sched_hdl;
	}

	hcap->hca_inline_max = (hca_attr.hca_flags & IBT_HCA_WQE_SIZE_INFO) ?
	    (uint16_t)hca_attr.hca_conn_send_inline_sz : 0;
	hcap->hca_sdp_buff_size = state->sdp_buff_size;
	sdp_buff_cache_create(hcap);

	hcap->hca_nports = (uint8_t)num_ports;
	ASSERT(hcap->port_list == NULL);
	for (index = 0; index < num_ports; ) {
		portp = kmem_zalloc(sizeof (sdp_dev_port_t), KM_SLEEP);

		status = ibt_bind_service(srv_hdl,
		    port_infop[index++].p_sgid_tbl[0],
		    NULL, NULL, &portp->bind_hdl);
		portp->index = index;
		if (status != IBT_SUCCESS) {
			sdp_print_dbg(NULL, "sdp_ibt_init:"
			    " service registeration failed: <%d> \n", status);
		}
		/*
		 * insert new port into list
		 */
		portp->next = hcap->port_list;
		hcap->port_list = portp;
	}
	ibt_free_portinfo(port_infop, port_size);

	return (IBT_SUCCESS);
}

extern ibt_cm_status_t cm_event_handler(void *arg,
	ibt_cm_event_t *ibt_cm_event,
	ibt_cm_return_args_t *ret_args,
	void *ret_priv_data,
	ibt_priv_data_len_t ret_len_max);

static ibt_cm_status_t
sdp_cm_event_handler(void *arg,	ibt_cm_event_t *ibt_cm_event,
    ibt_cm_return_args_t *ret_args, void *ret_priv_data,
    ibt_priv_data_len_t ret_len_max)
{
	ibt_cm_status_t	ret_status;

	ret_status = cm_event_handler(arg,
	    ibt_cm_event, ret_args,
	    ret_priv_data,
	    ret_len_max);
	return (ret_status);
}

/*
 * sdp_ibt_init -- post ibt_attach initialization for buffers and hca's.
 */
int
sdp_ibt_init(sdp_state_t *global_state)
{
	ibt_status_t	status;
	ib_guid_t *hca_guids;
	sdp_dev_hca_t *hcap = 0;
	int32_t		i;
	uint32_t	hca_count;
	ibt_srv_desc_t	srv_desc;
	ib_svc_id_t	ret_sid;

	sdp_print_init(NULL, "sdp_ibt_init: Initialization with state:%p",
	    (void *)global_state);

	bzero(&srv_desc, sizeof (ibt_srv_desc_t));
	srv_desc.sd_handler = cm_event_handler;
	srv_desc.sd_flags = 0;

	status = ibt_register_service(global_state->sdp_ibt_hdl,
	    &srv_desc, SDP_MSG_PORT_TO_SID(0), 0xffff,
	    &global_state->sdp_ibt_srv_hdl, &ret_sid);
	if (status != IBT_SUCCESS) {
		sdp_print_warn(NULL, "sdp_ibt_init: fail"
		    "to register server: <%d>", status);
		return (DDI_FAILURE);
	}

	/*
	 * determine the number of hcas we have and open each one.
	 * allocate a PD and cqs on each one.
	 */
	hca_count = ibt_get_hca_list(&hca_guids);
	global_state->hca_count = hca_count;

	/*
	 * Even if there is no HCAs on the system, we must allow
	 * sdpib_attach() to succeed.
	 */
	if (hca_count < 1) {
		dprint(SDP_WARN, ("No HCA's found on system!\n"));
		return (DDI_SUCCESS);
	}
	/* loop through hca's and initialize them all */
	for (i = 0; i < hca_count; i++) {
		hcap = kmem_zalloc(sizeof (sdp_dev_hca_t), KM_SLEEP);
		ASSERT(global_state->sdp_ibt_hdl);
		hcap->guid = hca_guids[i];
		status = sdp_hca_init(global_state, hcap);
		if (status != IBT_SUCCESS) {
			sdp_print_warn(NULL, "sdp_hca_init() failed"
			    " <%d>", status);
			kmem_free(hcap, sizeof (sdp_dev_hca_t));
			continue;
		}
		/*
		 * add HCA to the list
		 */
		mutex_enter(&global_state->hcas_mutex);
		hcap->next = global_state->hca_list;
		global_state->hca_list = hcap;
		mutex_exit(&global_state->hcas_mutex);
	} /* for hcas */
	ibt_free_hca_list(hca_guids, hca_count);
	return (DDI_SUCCESS);
}

int
sdp_ibt_fini_hca(sdp_dev_hca_t *hcap, ibt_srv_hdl_t srv_hdl)
{
	ibt_status_t status;
	sdp_dev_port_t *portp = NULL;


	for (portp = hcap->port_list; portp != NULL; portp = hcap->port_list) {
		if (portp->bind_hdl != NULL)
			(void) ibt_unbind_service(srv_hdl, portp->bind_hdl);
		hcap->port_list = portp->next;
		portp->next = NULL;
		kmem_free(portp, sizeof (sdp_dev_port_t));
	}

	if (hcap->sdp_hca_cq_sched_hdl != NULL) {
		status = ibt_free_cq_sched(hcap->hca_hdl,
		    hcap->sdp_hca_cq_sched_hdl);
		ASSERT(status == IBT_SUCCESS);
		hcap->sdp_hca_cq_sched_hdl = NULL;
	}

	status = ibt_free_pd(hcap->hca_hdl, hcap->pd_hdl);
	ASSERT(status == IBT_SUCCESS);

	status = ibt_close_hca(hcap->hca_hdl);
	ASSERT(status == IBT_SUCCESS);

	sdp_buff_cache_destroy(hcap);

	return (DDI_SUCCESS);
}

/*
 * called from _detach, reverse what we did in attach.
 */
int
sdp_ibt_fini(sdp_state_t *global_state)
{
	int retval = DDI_SUCCESS;
	sdp_dev_hca_t *hcap = 0;

	sdp_print_init(NULL, "sdp_ibt_fini: de-initialization with state:%p",
	    (void *)global_state);

	mutex_enter(&global_state->hcas_mutex);
	for (hcap = global_state->hca_list; hcap != NULL;
	    hcap = global_state->hca_list) {
		retval = sdp_ibt_fini_hca(hcap, global_state->sdp_ibt_srv_hdl);
		if (retval != DDI_SUCCESS) {
			sdp_print_warn(NULL, "sdp_ibt_fini: sdp_ibt_fini_hca "
			    "failed for hcap %p\n", (void *)hcap);
			mutex_exit(&global_state->hcas_mutex);
			return (DDI_FAILURE);
		}
		global_state->hca_list = hcap->next;
		hcap->next = NULL;
		global_state->hca_count--;
		kmem_free(hcap, sizeof (sdp_dev_hca_t));
	}	/* for */
	(void) ibt_deregister_service(global_state->sdp_ibt_hdl,
	    global_state->sdp_ibt_srv_hdl);
	mutex_exit(&global_state->hcas_mutex);
	return (DDI_SUCCESS);
}

sdp_dev_hca_t *
get_hcap_by_hdl(sdp_state_t *state, ibt_hca_hdl_t hdl)
{
	sdp_dev_hca_t *ret_hcap = NULL;

	ASSERT(state != NULL);
	ASSERT(mutex_owned(&state->hcas_mutex));

	for (ret_hcap = state->hca_list; ret_hcap != NULL;
	    ret_hcap = ret_hcap->next) {
		if (ret_hcap->hca_hdl == hdl) {
			break;
		} /* if found hcap */
	} /* for */
	/*
	 * it's possible for an HCA to be removed from the list
	 * if a thread grabs the hca mutex before we do.
	 */
	return (ret_hcap);
}

/*
 * sdp_ibt_hca_attach:
 * called when async handler delivers an IBT_HCA_ATTACH_EVENT
 */
int
sdp_ibt_hca_attach(sdp_state_t *state, ibt_async_event_t *event)
{
	ibt_status_t	status;
	sdp_dev_hca_t *hcap;

	ASSERT(state != NULL);
	ASSERT(state->sdp_ibt_hdl);

	mutex_enter(&state->sdp_state_lock);
	if (state->sdp_state != SDP_ATTACHED) {
		mutex_exit(&state->sdp_state_lock);
		sdp_print_warn(NULL, "sdp_ibt_hca_attach: SDP not attached. "
		    "Current state %d", state->sdp_state);
		return (DDI_FAILURE);
	}
	hcap = kmem_zalloc(sizeof (sdp_dev_hca_t), KM_SLEEP);
	hcap->guid = event->ev_hca_guid;
	status = sdp_hca_init(state, hcap);
	if (status != IBT_SUCCESS) {
		mutex_exit(&state->sdp_state_lock);
		sdp_print_warn(NULL, "sdp_ibt_hca_attach: sdp_hca_init() failed"
		    " <%d>", status);
		kmem_free(hcap, sizeof (sdp_dev_hca_t));
		return (DDI_FAILURE);
	}
	/*
	 * add HCA to the list
	 */
	mutex_enter(&state->hcas_mutex);
	hcap->next = state->hca_list;
	state->hca_list = hcap;
	state->hca_count++;
	mutex_exit(&state->hcas_mutex);
	mutex_exit(&state->sdp_state_lock);
	return (DDI_SUCCESS);
}

/*
 * sdp_ibt_hca_detach:
 * called when async handler delivers an IBT_HCA_DETACH_EVENT
 */
int
sdp_ibt_hca_detach(sdp_state_t *state, ibt_hca_hdl_t detach_hca)
{
	sdp_dev_hca_t *hcap = NULL;
	sdp_dev_hca_t *prev_hcap = NULL;
	int retval;

	ASSERT(state != NULL);

	mutex_enter(&state->sdp_state_lock);
	if (state->sdp_state != SDP_ATTACHED) {
		mutex_exit(&state->sdp_state_lock);
		sdp_print_warn(NULL, "sdp_ibt_hca_detach: SDP not attached. "
		    "Current state %d", state->sdp_state);
		return (DDI_FAILURE);
	}
	mutex_enter(&state->hcas_mutex);
	for (hcap = state->hca_list; hcap != NULL;
	    hcap = hcap->next) {

		if (hcap->hca_hdl != detach_hca) {
			prev_hcap = hcap;
			continue;
		}

		if (hcap->hca_num_conns > 0 || hcap->sdp_hca_refcnt > 0) {
			mutex_exit(&state->hcas_mutex);
			mutex_exit(&state->sdp_state_lock);
			return (DDI_FAILURE);
		}
		retval = sdp_ibt_fini_hca(hcap, state->sdp_ibt_srv_hdl);
		if (retval != DDI_SUCCESS) {
			sdp_print_warn(NULL, "sdp_ibt_hca_detach: "
			    "sdp_ibt_fini_hca failed for hcap %p",
			    (void *)hcap);
			mutex_exit(&state->hcas_mutex);
			mutex_exit(&state->sdp_state_lock);
			return (DDI_FAILURE);
		}
		/*
		 * We found HCA in the hca_list and freed its resources. Remove
		 * our HCA from the hca_list.
		 */
		if (state->hca_list == hcap) {
			state->hca_list = hcap->next;
		} else {
			prev_hcap->next = hcap->next;
		}
		hcap->next = NULL;
		kmem_free(hcap, sizeof (sdp_dev_hca_t));
		state->hca_count--;
		mutex_exit(&state->hcas_mutex);
		mutex_exit(&state->sdp_state_lock);
		return (DDI_SUCCESS);
	}
	mutex_exit(&state->hcas_mutex);
	mutex_exit(&state->sdp_state_lock);
	return (DDI_FAILURE);
}

/*
 * sdp_ibt_alloc_rc_channel-- called after successfull gid resolution on active
 * side.
 */
int
sdp_ibt_alloc_rc_channel(sdp_conn_t *conn)
{
	ibt_rc_chan_alloc_args_t	alloc_args;
	ibt_status_t			status = 0;
	ibt_chan_sizes_t		sizes;
	ibt_channel_hdl_t		channel_hdl;
	ibt_chan_alloc_flags_t		alloc_flags = IBT_ACHAN_NO_FLAGS;
	ibt_hca_hdl_t			hca_hdl = conn->hca_hdl;

	/*
	 * alloc the RC channels.
	 */
	bzero(&alloc_args, sizeof (ibt_rc_chan_alloc_args_t));
	bzero(&sizes, sizeof (ibt_chan_sizes_t));

	alloc_args.rc_flags = IBT_WR_SIGNALED |
	    ((conn->hcap->hca_use_reserved_lkey) ? IBT_FAST_REG_RES_LKEY : 0);
	alloc_args.rc_control = IBT_CEP_NO_FLAGS;

	alloc_args.rc_scq = conn->scq_hdl;
	alloc_args.rc_rcq = conn->rcq_hdl;
	alloc_args.rc_pd = conn->pd_hdl;

	alloc_args.rc_hca_port_num = conn->hw_port;
	alloc_args.rc_clone_chan = NULL;
	/* scatter/gather */
	alloc_args.rc_sizes.cs_sq_sgl = conn->hcap->hca_nds;
	/* scatter/gather */
	alloc_args.rc_sizes.cs_rq_sgl = conn->hcap->hca_nds;
	if (conn->ib_inline_max) {
		alloc_flags |= IBT_ACHAN_USES_INLINE;
		alloc_args.rc_sizes.cs_inline = conn->ib_inline_max;
	}

	alloc_args.rc_sizes.cs_sq = conn->scq_size;
	alloc_args.rc_sizes.cs_rq = conn->rcq_size;

	/*
	 * allocate our (client) side of the rc channel.
	 */
	status = ibt_alloc_rc_channel(hca_hdl, alloc_flags, &alloc_args,
	    &channel_hdl, &sizes);
	if (status != IBT_SUCCESS) {
		sdp_print_warn(conn, "sdp_ibt_alloc_rc_chan fail:<%d>",
		    status);
		if (conn->inet_ops.debug) {
			(void) strlog(SDP_MODULE_ID, 0, 1,
			    SL_TRACE | SL_ERROR,
			    "sdp_ibt_alloc_rc_channel: "
			    "ibt_alloc_rc_channel failed <%d>",
			    status);
		}
		/* report error and exit */
		goto done;
	}

	ibt_set_chan_private(channel_hdl, (void *)(uintptr_t)conn);

	/*
	 * store the channel handle in the connection structure.
	 */
	conn->channel_hdl = channel_hdl;

	return (0);

done:
	return (EPROTO);
}

/* Retry count before port failure is reported */
int sdp_path_retry_cnt = 1;

/* Retry count for Receive Not Ready (no posted buffers) */
int sdp_path_rnr_retry_cnt = 7;

/*
 * sdp_ibt_alloc_open_rc_channel --
 * allocates channel and sets the conn->path to it.
 * opens the channel, and steps through the connection protocol.
 * hello_msg is deallocated in calling function.
 */
int
sdp_ibt_alloc_open_rc_channel(sdp_conn_t *conn, ibt_path_info_t *path,
		sdp_msg_hello_t *hello_msg)
{
	ibt_rc_chan_alloc_args_t	alloc_args;
	ibt_status_t			status = 0;
	ibt_rc_returns_t		open_returns;
	ibt_chan_open_args_t		open_args;
	ibt_chan_sizes_t		sizes;
	ibt_channel_hdl_t		channel_hdl;
	sdp_msg_hello_ack_t		*ack_msg;
	ibt_chan_alloc_flags_t		alloc_flags = IBT_ACHAN_NO_FLAGS;
	ibt_hca_hdl_t	hca_hdl		= conn->hca_hdl;

	ASSERT(mutex_owned(&conn->conn_lock));

	/*
	 * alloc the RC channels.
	 */
	bzero(&alloc_args, sizeof (ibt_rc_chan_alloc_args_t));
	bzero(&sizes, sizeof (ibt_chan_sizes_t));

	alloc_args.rc_flags = IBT_WR_SIGNALED |
	    ((conn->hcap->hca_use_reserved_lkey) ? IBT_FAST_REG_RES_LKEY : 0);
	alloc_args.rc_control = IBT_CEP_NO_FLAGS;

	alloc_args.rc_scq = conn->scq_hdl;
	alloc_args.rc_rcq = conn->rcq_hdl;
	alloc_args.rc_pd = conn->pd_hdl;

	alloc_args.rc_hca_port_num = conn->hw_port;
	alloc_args.rc_clone_chan = NULL;

	/* scatter/gather */
	alloc_args.rc_sizes.cs_sq_sgl = conn->hcap->hca_nds;
	/* scatter/gather */
	alloc_args.rc_sizes.cs_rq_sgl = conn->hcap->hca_nds;

	alloc_args.rc_sizes.cs_sq = conn->scq_size;
	alloc_args.rc_sizes.cs_rq = conn->rcq_size;

	if (conn->ib_inline_max) {
		alloc_flags |= IBT_ACHAN_USES_INLINE;
		alloc_args.rc_sizes.cs_inline = conn->ib_inline_max;
	}

	/*
	 * allocate our (client) side of the rc channel.
	 */
	status = ibt_alloc_rc_channel(hca_hdl,
	    alloc_flags,
	    &alloc_args, &channel_hdl,
	    &sizes);
	if (status != IBT_SUCCESS) {
		sdp_print_warn(conn, "sdp_alloc_rc_chan fail:<%d>",
		    status);
		/* report error and exit */
		if (conn->inet_ops.debug) {
			(void) strlog(SDP_MODULE_ID, 0, 1,
			    SL_TRACE | SL_ERROR,
			    "sdp_ibt_alloc_open_rc_channel: "
			    " ibt_alloc_rc_channel failed <%d>",
			    status);
		}

		goto out_alloc_chan;
	}

	ibt_set_chan_private(channel_hdl, (void *)(uintptr_t)conn);

	/*
	 * store the channel handle in the connection structure.
	 */
	conn->channel_hdl		= channel_hdl;

	/*
	 * open the channels
	 */
	bzero(&open_args, sizeof (ibt_chan_open_args_t));
	bzero(&open_returns, sizeof (ibt_rc_returns_t));

	ack_msg = kmem_zalloc(sizeof (sdp_msg_hello_ack_t), KM_SLEEP);

	open_args.oc_cm_handler		= sdp_cm_event_handler;
	open_args.oc_cm_clnt_private	= (void *)(uintptr_t)conn;

	/*
	 * update path record with the SID
	 */
	path->pi_sid = SDP_MSG_PORT_TO_SID(ntohs(conn->dst_port));

	if (path->pi_prim_pkt_lt < (ib_time_t)16)
		path->pi_prim_pkt_lt = 16;
	open_args.oc_path		= path;


	open_returns.rc_priv_data_len = sizeof (sdp_msg_hello_ack_t);
	open_returns.rc_priv_data = ack_msg;

	open_args.oc_path_rnr_retry_cnt	= sdp_path_rnr_retry_cnt;
	open_args.oc_path_retry_cnt = (uint8_t)sdp_path_retry_cnt;

	open_args.oc_rdma_ra_out = 1;
	open_args.oc_rdma_ra_in	= 1;
	open_args.oc_priv_data_len = sizeof (sdp_msg_hello_t);

	ASSERT(open_args.oc_priv_data_len	<= IBT_REQ_PRIV_DATA_SZ);
	ASSERT(open_returns.rc_priv_data_len	<= IBT_REP_PRIV_DATA_SZ);
	ASSERT(open_args.oc_cm_handler		!= NULL);

	open_args.oc_priv_data	= (void *)(hello_msg);
	open_args.oc_cm_pkt_lt			= 16;

	SDP_CONN_HOLD(conn);
	SDP_CONN_UNLOCK(conn);
	status = ibt_open_rc_channel(conn->channel_hdl,
	    IBT_OCHAN_NO_FLAGS,
	    IBT_BLOCKING, &open_args,
	    &open_returns);

	if (status != IBT_SUCCESS) {
		/* check open_returns report error and exit */
		sdp_print_warn(conn, "sdp_open_rc_chan fail:<%d>",
		    (int)open_returns.rc_status);
		conn->error = -ECONNREFUSED;
		kmem_free(ack_msg, sizeof (sdp_msg_hello_ack_t));
		SDP_STAT_INC(conn->sdp_sdps, AttemptFails);
		if (conn->inet_ops.debug) {
			(void) strlog(SDP_MODULE_ID, 0, 1,
			    SL_TRACE | SL_ERROR,
			    "sdp_ibt_alloc_open_rc_channel: "
			    "ibt_open_rc_channel failed <%d>",
			    open_returns.rc_status);
		}
		goto out_alloc_rc;
	}
	kmem_free(ack_msg, sizeof (sdp_msg_hello_ack_t));
	SDP_CONN_LOCK(conn);
	SDP_CONN_PUT(conn);

	return (0);

out_alloc_rc:
	SDP_CONN_LOCK(conn);
	SDP_CONN_PUT(conn);
out_alloc_chan:
	return (-EPROTO);
} /* sdp_ibt_alloc_open_rc_channel */
