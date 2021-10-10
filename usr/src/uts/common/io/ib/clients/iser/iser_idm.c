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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/ddi.h>
#include <sys/sunddi.h>

#include <sys/socket.h>		/* networking stuff */
#include <sys/sysmacros.h>	/* offsetof */

#include <sys/ib/clients/iser/iser.h>
#include <sys/ib/clients/iser/iser_idm.h>

/*
 * iSER transport routines
 *
 * All transport functions except iser_tgt_svc_create() are called through
 * the ops vector, iser_tgt_svc_create() is called from the async handler
 * inaddition to being called by the ULP
 */

static void iser_pdu_tx(idm_conn_t *ic, idm_pdu_t *pdu);

static idm_status_t iser_buf_tx_to_ini(idm_task_t *idt, idm_buf_t *idb);
static idm_status_t iser_buf_rx_from_ini(idm_task_t *idt, idm_buf_t *idb);
static idm_status_t iser_tgt_enable_datamover(idm_conn_t *ic);
static idm_status_t iser_ini_enable_datamover(idm_conn_t *ic);
static void iser_notice_key_values(struct idm_conn_s *ic,
    nvlist_t *negotiated_nvl);
static kv_status_t iser_declare_key_values(struct idm_conn_s *ic,
    nvlist_t *config_nvl, nvlist_t *outgoing_nvl);
static idm_status_t iser_free_task_rsrcs(idm_task_t *idt);
static kv_status_t iser_negotiate_key_values(idm_conn_t *ic,
    nvlist_t *request_nvl, nvlist_t *response_nvl, nvlist_t *negotiated_nvl);
static kv_status_t iser_handle_numerical(nvpair_t *nvp, uint64_t value,
    const idm_kv_xlate_t *ikvx, uint64_t min_value, uint64_t max_value,
    uint64_t iser_max_value, nvlist_t *request_nvl, nvlist_t *response_nvl,
    nvlist_t *negotiated_nvl);
static kv_status_t iser_handle_boolean(nvpair_t *nvp, boolean_t value,
    const idm_kv_xlate_t *ikvx, boolean_t iser_value, nvlist_t *request_nvl,
    nvlist_t *response_nvl, nvlist_t *negotiated_nvl);
static kv_status_t iser_handle_key(nvpair_t *nvp, const idm_kv_xlate_t *ikvx,
    nvlist_t *request_nvl, nvlist_t *response_nvl, nvlist_t *negotiated_nvl);
static kv_status_t iser_process_request_nvlist(nvlist_t *request_nvl,
    nvlist_t *response_nvl, nvlist_t *negotiated_nvl);
static boolean_t iser_conn_is_capable(idm_conn_req_t *ic,
    idm_transport_caps_t *caps);
static idm_status_t iser_buf_alloc(idm_buf_t *idb, uint64_t buflen);
static idm_status_t iser_buf_setup(idm_buf_t *idb);
static void iser_buf_teardown(idm_buf_t *idb);
static void iser_buf_free(idm_buf_t *idb);
static void iser_tgt_svc_destroy(struct idm_svc_s *is);
static idm_status_t iser_tgt_svc_online(struct idm_svc_s *is);
static void iser_tgt_svc_offline(struct idm_svc_s *is);
static idm_status_t iser_tgt_conn_connect(struct idm_conn_s *ic);
static idm_status_t iser_ini_conn_create(idm_conn_req_t *cr,
    struct idm_conn_s *ic);
static void iser_conn_destroy(struct idm_conn_s *ic);
static idm_status_t iser_ini_conn_connect(struct idm_conn_s *ic);
static void iser_conn_disconnect(struct idm_conn_s *ic);
static idm_status_t iser_buf_alloc_sgl(idm_buf_t *idb,
    struct stmf_sglist_ent *sglp, uint32_t numbufs, uint32_t flags);
static void iser_buf_free_sgl(idm_buf_t *idb);

/*
 * iSER IDM transport operations
 */
idm_transport_ops_t iser_transport_ops = {
	&iser_pdu_tx,			/* it_tx_pdu */
	&iser_buf_tx_to_ini,		/* it_buf_tx_to_ini */
	&iser_buf_rx_from_ini,		/* it_buf_rx_from_ini */
	NULL,				/* it_rx_datain */
	NULL,				/* it_rx_rtt */
	NULL,				/* it_rx_dataout */
	NULL,				/* it_alloc_conn_rsrc */
	NULL,				/* it_free_conn_rsrc */
	&iser_tgt_enable_datamover,	/* it_tgt_enable_datamover */
	&iser_ini_enable_datamover,	/* it_ini_enable_datamover */
	NULL,				/* it_conn_terminate */
	&iser_free_task_rsrcs,		/* it_free_task_rsrc */
	&iser_negotiate_key_values,	/* it_negotiate_key_values */
	&iser_notice_key_values,	/* it_notice_key_values */
	&iser_conn_is_capable,		/* it_conn_is_capable */
	&iser_buf_alloc,		/* it_buf_alloc */
	&iser_buf_free,			/* it_buf_free */
	&iser_buf_setup,		/* it_buf_setup */
	&iser_buf_teardown,		/* it_buf_teardown */
	&iser_tgt_svc_create,		/* it_tgt_svc_create */
	&iser_tgt_svc_destroy,		/* it_tgt_svc_destroy */
	&iser_tgt_svc_online,		/* it_tgt_svc_online */
	&iser_tgt_svc_offline,		/* it_tgt_svc_offline */
	&iser_conn_destroy,		/* it_tgt_conn_destroy */
	&iser_tgt_conn_connect,		/* it_tgt_conn_connect */
	&iser_conn_disconnect,		/* it_tgt_conn_disconnect */
	&iser_ini_conn_create,		/* it_ini_conn_create */
	&iser_conn_destroy,		/* it_ini_conn_destroy */
	&iser_ini_conn_connect,		/* it_ini_conn_connect */
	&iser_conn_disconnect,		/* it_ini_conn_disconnect */
	&iser_declare_key_values,	/* it_declare_key_values */
	&iser_buf_alloc_sgl,		/* it_buf_alloc_sgl */
	&iser_buf_free_sgl		/* it_buf_free_sgl */
};

/*
 * iSER IDM transport capabilities
 */
idm_transport_caps_t iser_transport_caps = {
	0		/* flags */
};

/*
 * "Tunables" for using zero-copy I/O.  ZCOPY is only supported on
 * HCAs (such as hermon cards) that support "Reserved LKey".
 */

/* iser_do_zcopy takes effect at connection startup time. */
boolean_t	iser_do_zcopy		= B_TRUE;
uint32_t	iser_ib_max_sge		= 0; /* # of RDMA_READ per WR */
uint32_t	iser_max_sgl_xfer_len	= 0; /* max zcopy I/O size */
uint32_t	iser_first_sgl_xfer_len	= 0;
uint32_t	iser_copy_threshold	= ISER_COPY_THRESHOLD;

int
iser_idm_register()
{
	idm_transport_attr_t	attr;
	idm_status_t		status;

	attr.type	= IDM_TRANSPORT_TYPE_ISER;
	attr.it_ops	= &iser_transport_ops;
	attr.it_caps	= &iser_transport_caps;

	status = idm_transport_register(&attr);
	if (status != IDM_STATUS_SUCCESS) {
		ISER_LOG(CE_WARN, "Failed to register iSER transport with IDM");
		return (DDI_FAILURE);
	}

	ISER_LOG(CE_NOTE, "Registered iSER transport with IDM");

	return (DDI_SUCCESS);
}

/*
 * iser_ini_conn_create()
 * Allocate an iSER initiator connection context
 */
static idm_status_t
iser_ini_conn_create(idm_conn_req_t *cr, idm_conn_t *ic)
{
	iser_chan_t	*iser_chan = NULL;
	iser_conn_t	*iser_conn;

	/* Allocate and set up a connection handle */
	iser_conn = kmem_zalloc(sizeof (iser_conn_t), KM_SLEEP);
	mutex_init(&iser_conn->ic_lock, NULL, MUTEX_DRIVER, NULL);

	/* Allocate and open a channel to the target node */
	iser_chan = iser_channel_alloc(NULL, &cr->cr_ini_dst_addr);
	if (iser_chan == NULL) {
		ISER_LOG(CE_WARN, "iser: failed to allocate channel");
		mutex_destroy(&iser_conn->ic_lock);
		kmem_free(iser_conn, sizeof (iser_conn_t));
		return (IDM_STATUS_FAIL);
	}

	/*
	 * The local IP and remote IP are filled in iser_channel_alloc. The
	 * remote port needs to be filled in from idm_conn_req_t. The local
	 * port is irrelevant. Internal representation of the port in the
	 * IDM sockaddr structure is in network byte order. IBT expects the
	 * port in host byte order.
	 */
	switch (cr->cr_ini_dst_addr.sin.sa_family) {
	case AF_INET:
		iser_chan->ic_rport = ntohs(cr->cr_ini_dst_addr.sin4.sin_port);
		break;
	case AF_INET6:
		iser_chan->ic_rport = ntohs(cr->cr_ini_dst_addr.sin6.sin6_port);
		break;
	default:
		iser_chan->ic_rport = ISCSI_LISTEN_PORT;
	}
	iser_chan->ic_lport = 0;

	cv_init(&iser_conn->ic_stage_cv, NULL, CV_DEFAULT, NULL);
	iser_conn->ic_type = ISER_CONN_TYPE_INI;
	iser_conn->ic_stage = ISER_CONN_STAGE_ALLOCATED;
	iser_conn->ic_chan = iser_chan;
	iser_conn->ic_idmc = ic;

	/*
	 * Set a pointer to the iser_conn in the iser_chan for easy
	 * access during CM event handling
	 */
	iser_chan->ic_conn = iser_conn;

	/* Set the iSER conn handle in the IDM conn private handle */
	ic->ic_transport_private = (void *)iser_conn;

	/* Set the transport header length */
	ic->ic_transport_hdrlen = ISER_HEADER_LENGTH;

	return (IDM_STATUS_SUCCESS);
}

/*
 * iser_internal_conn_destroy()
 * Tear down iSER-specific connection resources. This is used below
 * in iser_conn_destroy(), but also from the CM code when we may have
 * some of the connection established, but not fully connected.
 */
void
iser_internal_conn_destroy(iser_conn_t *ic)
{
	mutex_enter(&ic->ic_lock);
	iser_channel_free(ic->ic_chan);
	if ((ic->ic_type == ISER_CONN_TYPE_TGT) &&
	    (ic->ic_stage == ISER_CONN_STAGE_ALLOCATED)) {
		/*
		 * This is a target connection that has yet to be
		 * established. Free our reference on the target
		 * service handle.
		 */
		iser_tgt_svc_rele(ic->ic_idms->is_iser_svc);
	}
	cv_destroy(&ic->ic_stage_cv);
	mutex_exit(&ic->ic_lock);
	mutex_destroy(&ic->ic_lock);
	kmem_free(ic, sizeof (iser_conn_t));
}

/*
 * iser_conn_destroy()
 * Tear down an initiator or target connection.
 */
static void
iser_conn_destroy(idm_conn_t *ic)
{
	iser_conn_t	*iser_conn;
	iser_conn = (iser_conn_t *)ic->ic_transport_private;

	iser_internal_conn_destroy(iser_conn);
	ic->ic_transport_private = NULL;
}

/*
 * iser_ini_conn_connect()
 * Establish the connection referred to by the handle previously allocated via
 * iser_ini_conn_create().
 */
static idm_status_t
iser_ini_conn_connect(idm_conn_t *ic)
{
	iser_conn_t		*iser_conn;
	iser_status_t		status;

	iser_conn = (iser_conn_t *)ic->ic_transport_private;

	status = iser_channel_open(iser_conn->ic_chan);
	if (status != ISER_STATUS_SUCCESS) {
		ISER_LOG(CE_WARN, "iser: failed to open channel");
		return (IDM_STATUS_FAIL);
	}

	/*
	 * Set the local and remote addresses in the idm conn handle.
	 */
	iser_ib_conv_ibtaddr2sockaddr(&ic->ic_laddr,
	    &iser_conn->ic_chan->ic_localip, iser_conn->ic_chan->ic_lport);
	iser_ib_conv_ibtaddr2sockaddr(&ic->ic_raddr,
	    &iser_conn->ic_chan->ic_remoteip, iser_conn->ic_chan->ic_rport);

	mutex_enter(&iser_conn->ic_lock);
	/* Hold a reference on the IDM connection handle */
	idm_conn_hold(ic);
	iser_conn->ic_stage = ISER_CONN_STAGE_IC_CONNECTED;
	mutex_exit(&iser_conn->ic_lock);

	return (IDM_STATUS_SUCCESS);
}

/*
 * iser_conn_disconnect()
 * Shutdown this iSER connection
 */
static void
iser_conn_disconnect(idm_conn_t *ic)
{
	iser_conn_t	*iser_conn;

	iser_conn = (iser_conn_t *)ic->ic_transport_private;

	mutex_enter(&iser_conn->ic_lock);
	iser_conn->ic_stage = ISER_CONN_STAGE_CLOSING;
	mutex_exit(&iser_conn->ic_lock);

	/* Close the channel */
	iser_channel_close(iser_conn->ic_chan);

	/* Free our reference held on the IDM conn handle, and set CLOSED */
	mutex_enter(&iser_conn->ic_lock);
	idm_conn_rele(iser_conn->ic_idmc);
	iser_conn->ic_stage = ISER_CONN_STAGE_CLOSED;
	mutex_exit(&iser_conn->ic_lock);
}

/*
 * iser_tgt_svc_create()
 * Establish the CM service for inbound iSER service requests on the port
 * indicated by sr->sr_port.
 * idm_svc_req_t contains the service parameters.
 */
idm_status_t
iser_tgt_svc_create(idm_svc_req_t *sr, idm_svc_t *is)
{
	iser_svc_t		*iser_svc;

	iser_svc = kmem_zalloc(sizeof (iser_svc_t), KM_SLEEP);
	is->is_iser_svc = (void *)iser_svc;

	idm_refcnt_init(&iser_svc->is_refcnt, iser_svc);

	list_create(&iser_svc->is_sbindlist, sizeof (iser_sbind_t),
	    offsetof(iser_sbind_t, is_list_node));
	iser_svc->is_svcid = ibt_get_ip_sid(IPPROTO_TCP, sr->sr_port);

	/*
	 * Target service will be registered and
	 * bound when the target comes online
	 */
	return (IDM_STATUS_SUCCESS);
}

/* IDM refcnt utilities for the iSER service handle */
void
iser_tgt_svc_hold(iser_svc_t *is)
{
	idm_refcnt_hold(&is->is_refcnt);
}

void
iser_tgt_svc_rele(iser_svc_t *is)
{
	idm_refcnt_rele(&is->is_refcnt);
}

/*
 * iser_tgt_svc_destroy()
 * Teardown resources allocated in iser_tgt_svc_create()
 */
static void
iser_tgt_svc_destroy(idm_svc_t *is)
{
	iser_svc_t	*iser_svc;

	iser_svc = (iser_svc_t *)is->is_iser_svc;

	/* Wait for the iSER service handle's refcnt to zero */
	idm_refcnt_wait_ref(&iser_svc->is_refcnt);

	list_destroy(&iser_svc->is_sbindlist);

	idm_refcnt_destroy(&iser_svc->is_refcnt);

	kmem_free(iser_svc, sizeof (iser_svc_t));
}

/*
 * iser_tgt_svc_online()
 * Bind the CM service allocated via iser_tgt_svc_create().
 */
static idm_status_t
iser_tgt_svc_online(idm_svc_t *is)
{
	iser_svc_t	*iser_svc = (iser_svc_t *)is->is_iser_svc;
	iser_status_t	status;

	mutex_enter(&is->is_mutex);

	status = iser_register_service(is);
	if (status != DDI_SUCCESS) {
		ISER_LOG(CE_NOTE, "iser_tgt_svc_online: iser_register_service "
		    "failed on port (%llx): (0x%x)",
		    (long long unsigned int)iser_svc->is_svcid, status);
		(void) ibt_release_ip_sid(iser_svc->is_svcid);
		mutex_exit(&is->is_mutex);
		return (IDM_STATUS_FAIL);
	}

	/*
	 * Pass the IDM service handle as the client private data for
	 * later use.
	 */
	status = iser_bind_service(is);
	if (status != ISER_STATUS_SUCCESS) {
		ISER_LOG(CE_NOTE, "iser_tgt_svc_online: failed bind service");
		mutex_exit(&is->is_mutex);
		return (IDM_STATUS_FAIL);
	}

	mutex_exit(&is->is_mutex);
	return (IDM_STATUS_SUCCESS);
}

/*
 * iser_tgt_svc_offline
 * Unbind the service on all available HCA ports.
 */
static void
iser_tgt_svc_offline(idm_svc_t *is)
{
	mutex_enter(&is->is_mutex);

	iser_unbind_service(is);

	/*
	 * Deregister the iSER target service on this port and free
	 * the iser_svc structure from the idm_svc handle.
	 */
	iser_deregister_service(is);

	mutex_exit(&is->is_mutex);

}

/*
 * iser_tgt_conn_connect()
 * Establish the connection in ic, passed from idm_tgt_conn_finish(), which
 * is invoked from the SM as a result of an inbound connection request.
 */
/* ARGSUSED */
static idm_status_t
iser_tgt_conn_connect(idm_conn_t *ic)
{
	iser_conn_t	*iser_conn;
	iser_hca_t	*iser_hca;
	uint_t		max_sge;

	iser_conn = ic->ic_transport_private;
	iser_hca = iser_conn->ic_chan->ic_hca;

	/* For cards (e.g. hermon) that support Reserved LKey, set up zcopy */
	if (iser_do_zcopy &&
	    (iser_hca->hca_attr.hca_flags2 & IBT_HCA2_RES_LKEY)) {
		if (iser_max_sgl_xfer_len) {
			ic->ic_conn_params.max_sgl_xfer_len =
			    iser_max_sgl_xfer_len;
		} else {
			/*
			 * Determine the max length by the max number of
			 * WRS and SGE per WR that we are willing to queue
			 * in a single ibt_post_send operation.
			 */
			max_sge = iser_hca->hca_max_sgl_size;
			if (iser_ib_max_sge)
				max_sge = min(iser_ib_max_sge, max_sge);
			ic->ic_conn_params.max_sgl_xfer_len = (PAGESIZE *
			    ((ISER_IB_MAX_WRS * max_sge) - 1));
		}
		ic->ic_conn_params.first_sgl_xfer_len = iser_first_sgl_xfer_len;
		ic->ic_conn_params.copy_threshold = iser_copy_threshold;
	}

	return (IDM_STATUS_SUCCESS);
}

/*
 * iser_tgt_enable_datamover() sets the transport private data on the
 * idm_conn_t and move the conn stage to indicate logged in.
 */
static idm_status_t
iser_tgt_enable_datamover(idm_conn_t *ic)
{
	iser_conn_t	*iser_conn;

	iser_conn = (iser_conn_t *)ic->ic_transport_private;
	mutex_enter(&iser_conn->ic_lock);

	iser_conn->ic_stage = ISER_CONN_STAGE_LOGGED_IN;
	mutex_exit(&iser_conn->ic_lock);

	return (IDM_STATUS_SUCCESS);
}

/*
 * iser_ini_enable_datamover() is used by the iSCSI initator to request that a
 * specified iSCSI connection be transitioned to iSER-assisted mode.
 * In the case of iSER, the RDMA resources for a reliable connection have
 * already been allocated at this time, and the 'RDMAExtensions' is set to 'Yes'
 * so no further negotiations are required at this time.
 * The initiator now sends the first iSER Message - 'Hello' to the target
 * and waits for  the 'HelloReply' Message from the target before directing
 * the initiator to go into the Full Feature Phase.
 *
 * No transport op is required on the target side.
 */
static idm_status_t
iser_ini_enable_datamover(idm_conn_t *ic)
{

	iser_conn_t	*iser_conn;
	clock_t		delay;
	int		status;

	iser_conn = (iser_conn_t *)ic->ic_transport_private;

	mutex_enter(&iser_conn->ic_lock);
	iser_conn->ic_stage = ISER_CONN_STAGE_HELLO_SENT;
	mutex_exit(&iser_conn->ic_lock);

	/* Send the iSER Hello Message to the target */
	status = iser_xfer_hello_msg(iser_conn->ic_chan);
	if (status != ISER_STATUS_SUCCESS) {

		mutex_enter(&iser_conn->ic_lock);
		iser_conn->ic_stage = ISER_CONN_STAGE_HELLO_SENT_FAIL;
		mutex_exit(&iser_conn->ic_lock);

		return (IDM_STATUS_FAIL);
	}

	/*
	 * Acquire the iser_conn->ic_lock and wait for the iSER HelloReply
	 * Message from the target, i.e. iser_conn_stage_t to be set to
	 * ISER_CONN_STAGE_HELLOREPLY_RCV. If the handshake does not
	 * complete within a specified time period (.5s), then return failure.
	 *
	 */
	delay = ddi_get_lbolt() + drv_usectohz(500000);

	mutex_enter(&iser_conn->ic_lock);
	while ((iser_conn->ic_stage != ISER_CONN_STAGE_HELLOREPLY_RCV) &&
	    (ddi_get_lbolt() < delay)) {

		(void) cv_timedwait(&iser_conn->ic_stage_cv,
		    &iser_conn->ic_lock, delay);
	}

	switch (iser_conn->ic_stage) {
	case ISER_CONN_STAGE_HELLOREPLY_RCV:
		iser_conn->ic_stage = ISER_CONN_STAGE_LOGGED_IN;
		mutex_exit(&iser_conn->ic_lock);
		/*
		 * Return suceess to indicate that the initiator connection can
		 * go to the next phase - FFP
		 */
		return (IDM_STATUS_SUCCESS);
	default:
		iser_conn->ic_stage = ISER_CONN_STAGE_HELLOREPLY_RCV_FAIL;
		mutex_exit(&iser_conn->ic_lock);
		return (IDM_STATUS_FAIL);

	}

	/* STATEMENT_NEVER_REACHED */
}

/*
 * iser_free_task_rsrcs()
 * This routine does not currently need to do anything. It is used in
 * the sockets transport to explicitly complete any buffers on the task,
 * but we can rely on our RCaP layer to finish up it's work without any
 * intervention.
 */
/* ARGSUSED */
idm_status_t
iser_free_task_rsrcs(idm_task_t *idt)
{
	return (IDM_STATUS_SUCCESS);
}

/*
 * iser_negotiate_key_values() validates the key values for this connection
 */
/* ARGSUSED */
static kv_status_t
iser_negotiate_key_values(idm_conn_t *ic, nvlist_t *request_nvl,
    nvlist_t *response_nvl, nvlist_t *negotiated_nvl)
{
	kv_status_t		kvrc = KV_HANDLED;

	/* Process the request nvlist */
	kvrc = iser_process_request_nvlist(request_nvl, response_nvl,
	    negotiated_nvl);

	/* We must be using RDMA, so set the flag on the ic handle */
	ic->ic_rdma_extensions = B_TRUE;

	return (kvrc);
}

/* Process a list of key=value pairs from a login request */
static kv_status_t
iser_process_request_nvlist(nvlist_t *request_nvl, nvlist_t *response_nvl,
    nvlist_t *negotiated_nvl)
{
	const idm_kv_xlate_t	*ikvx;
	char			*nvp_name;
	nvpair_t		*nvp;
	nvpair_t		*next_nvp;
	kv_status_t		kvrc = KV_HANDLED;
	boolean_t		transit = B_TRUE;

	/* Process the list */
	nvp = nvlist_next_nvpair(request_nvl, NULL);
	while (nvp != NULL) {
		next_nvp = nvlist_next_nvpair(request_nvl, nvp);

		nvp_name = nvpair_name(nvp);
		ikvx = idm_lookup_kv_xlate(nvp_name, strlen(nvp_name));

		kvrc = iser_handle_key(nvp, ikvx, request_nvl, response_nvl,
		    negotiated_nvl);
		if (kvrc != KV_HANDLED) {
			if (kvrc == KV_HANDLED_NO_TRANSIT) {
				/* we countered, clear the transit flag */
				transit = B_FALSE;
			} else {
				/* error, bail out */
				break;
			}
		}

		nvp = next_nvp;
	}
	/*
	 * If the current kv_status_t indicates success, we've handled
	 * the entire list. Explicitly set kvrc to NO_TRANSIT if we've
	 * cleared the transit flag along the way.
	 */
	if ((kvrc == KV_HANDLED) && (transit == B_FALSE)) {
		kvrc = KV_HANDLED_NO_TRANSIT;
	}

	return (kvrc);
}

/* Handle a given list, boolean or numerical key=value pair */
static kv_status_t
iser_handle_key(nvpair_t *nvp, const idm_kv_xlate_t *ikvx,
    nvlist_t *request_nvl, nvlist_t *response_nvl, nvlist_t *negotiated_nvl)
{
	kv_status_t		kvrc = KV_UNHANDLED;
	boolean_t		bool_val;
	uint64_t		num_val;
	int			nvrc;

	/* Retrieve values for booleans and numericals */
	switch (ikvx->ik_key_id) {
		/* Booleans */
	case KI_RDMA_EXTENSIONS:
	case KI_IMMEDIATE_DATA:
		nvrc = nvpair_value_boolean_value(nvp, &bool_val);
		ASSERT(nvrc == 0);
		break;
		/* Numericals */
	case KI_INITIATOR_RECV_DATA_SEGMENT_LENGTH:
	case KI_TARGET_RECV_DATA_SEGMENT_LENGTH:
	case KI_MAX_OUTSTANDING_UNEXPECTED_PDUS:
		nvrc = nvpair_value_uint64(nvp, &num_val);
		ASSERT(nvrc == 0);
		break;
	default:
		break;
	}

	/*
	 * Now handle the values according to the key name. Keys not
	 * specifically handled here will be negotiated by the iscsi
	 * target. Negotiated values take effect when
	 * iser_notice_key_values gets called.
	 */
	switch (ikvx->ik_key_id) {
	case KI_RDMA_EXTENSIONS:
		/* Ensure "Yes" */
		kvrc = iser_handle_boolean(nvp, bool_val, ikvx, B_TRUE,
		    request_nvl, response_nvl, negotiated_nvl);
		break;
	case KI_TARGET_RECV_DATA_SEGMENT_LENGTH:
		/* Validate the proposed value */
		kvrc = iser_handle_numerical(nvp, num_val, ikvx,
		    ISER_TARGET_RECV_DATA_SEGMENT_LENGTH_MIN,
		    ISER_TARGET_RECV_DATA_SEGMENT_LENGTH_MAX,
		    ISER_TARGET_RECV_DATA_SEGMENT_LENGTH_IMPL_MAX,
		    request_nvl, response_nvl, negotiated_nvl);
		break;
	case KI_INITIATOR_RECV_DATA_SEGMENT_LENGTH:
		/* Validate the proposed value */
		kvrc = iser_handle_numerical(nvp, num_val, ikvx,
		    ISER_INITIATOR_RECV_DATA_SEGMENT_LENGTH_MIN,
		    ISER_INITIATOR_RECV_DATA_SEGMENT_LENGTH_MAX,
		    ISER_INITIATOR_RECV_DATA_SEGMENT_LENGTH_IMPL_MAX,
		    request_nvl, response_nvl, negotiated_nvl);
		break;
	case KI_IMMEDIATE_DATA:
		/* Ensure "No" */
		kvrc = iser_handle_boolean(nvp, bool_val, ikvx, B_FALSE,
		    request_nvl, response_nvl, negotiated_nvl);
		break;
	case KI_MAX_OUTSTANDING_UNEXPECTED_PDUS:
		/* Validate the proposed value */
		kvrc = iser_handle_numerical(nvp, num_val, ikvx,
		    ISER_MAX_OUTSTANDING_UNEXPECTED_PDUS_MIN,
		    ISER_MAX_OUTSTANDING_UNEXPECTED_PDUS_MAX,
		    ISER_MAX_OUTSTANDING_UNEXPECTED_PDUS_IMPL_MAX,
		    request_nvl, response_nvl, negotiated_nvl);
		break;
	default:
		/*
		 * All other keys, including invalid keys, will be
		 * handled at the client layer.
		 */
		kvrc = KV_HANDLED;
		break;
	}

	return (kvrc);
}


/* Validate a proposed boolean value, and set the alternate if necessary */
static kv_status_t
iser_handle_boolean(nvpair_t *nvp, boolean_t value, const idm_kv_xlate_t *ikvx,
    boolean_t iser_value, nvlist_t *request_nvl, nvlist_t *response_nvl,
    nvlist_t *negotiated_nvl)
{
	kv_status_t		kvrc = KV_UNHANDLED;
	int			nvrc;
	boolean_t		respond = B_FALSE;

	if (value != iser_value) {
		/*
		 * Respond back to initiator with our value, and
		 * set the return value to unset the transit bit.
		 */
		value = iser_value;
		nvrc = nvlist_add_boolean_value(negotiated_nvl,
		    ikvx->ik_key_name, value);
		if (nvrc == 0) {
			kvrc = KV_HANDLED_NO_TRANSIT;
			respond = B_TRUE;
		}

	} else {
		/* Add this to our negotiated values */
		nvrc = nvlist_add_nvpair(negotiated_nvl, nvp);
		/* Respond if this is not a declarative */
		respond = (ikvx->ik_declarative == B_FALSE);
	}

	/* Response of Simple-value Negotiation */
	if (nvrc == 0 && respond) {
		nvrc = nvlist_add_boolean_value(response_nvl,
		    ikvx->ik_key_name, value);
		/* Remove from the request (we've handled it) */
		(void) nvlist_remove_all(request_nvl, ikvx->ik_key_name);
	}

	if (kvrc == KV_HANDLED_NO_TRANSIT) {
		return (kvrc);
	}

	return (idm_nvstat_to_kvstat(nvrc));
}

/*
 * Validate a proposed value against the iSER and/or iSCSI RFC's minimum and
 * maximum values, and set an alternate, if necessary.  Note that the value
 * 'iser_max_value" represents our implementation maximum (typically the max).
 */
static kv_status_t
iser_handle_numerical(nvpair_t *nvp, uint64_t value, const idm_kv_xlate_t *ikvx,
    uint64_t min_value, uint64_t max_value, uint64_t iser_max_value,
    nvlist_t *request_nvl, nvlist_t *response_nvl, nvlist_t *negotiated_nvl)
{
	kv_status_t		kvrc = KV_UNHANDLED;
	int			nvrc;
	boolean_t		respond = B_FALSE;

	/* Validate against standard */
	if ((value < min_value) || (value > max_value)) {
		kvrc = KV_VALUE_ERROR;
	} else {
		if (value > iser_max_value) {
			/*
			 * Respond back to initiator with our value, and
			 * set the return value to unset the transit bit.
			 */
			value = iser_max_value;
			nvrc = nvlist_add_uint64(negotiated_nvl,
			    ikvx->ik_key_name, value);
			if (nvrc == 0) {
				kvrc = KV_HANDLED_NO_TRANSIT;
				respond = B_TRUE;
			}
		} else {
			/* Add this to our negotiated values */
			nvrc = nvlist_add_nvpair(negotiated_nvl, nvp);
			/* Respond if this is not a declarative */
			respond = (ikvx->ik_declarative == B_FALSE);
		}

		/* Response of Simple-value Negotiation */
		if (nvrc == 0 && respond) {
			nvrc = nvlist_add_uint64(response_nvl,
			    ikvx->ik_key_name, value);
			/* Remove from the request (we've handled it) */
			(void) nvlist_remove_all(request_nvl,
			    ikvx->ik_key_name);
		}
	}

	if (kvrc == KV_HANDLED_NO_TRANSIT) {
		return (kvrc);
	}

	return (idm_nvstat_to_kvstat(nvrc));
}

/*
 * iser_declare_key_values() declares the declarative key values for
 * this connection.
 */
/* ARGSUSED */
static kv_status_t
iser_declare_key_values(idm_conn_t *ic, nvlist_t *config_nvl,
    nvlist_t *outgoing_nvl)
{
	kv_status_t		kvrc;
	int			nvrc = 0;
	int			rc;
	uint64_t		uint64_val;

	if ((rc = nvlist_lookup_uint64(config_nvl,
	    ISER_KV_KEY_NAME_MAX_OUTSTANDING_PDU, &uint64_val)) != ENOENT) {
		ASSERT(rc == 0);
		if (outgoing_nvl) {
			nvrc = nvlist_add_uint64(outgoing_nvl,
			    ISER_KV_KEY_NAME_MAX_OUTSTANDING_PDU, uint64_val);
		}
	}
	kvrc = idm_nvstat_to_kvstat(nvrc);
	return (kvrc);
}

/*
 * iser_notice_key_values() activates the negotiated key values for
 * this connection.
 */
static void
iser_notice_key_values(idm_conn_t *ic, nvlist_t *negotiated_nvl)
{
	iser_conn_t		*iser_conn;
	boolean_t		boolean_val;
	uint64_t		uint64_val;
	int			nvrc;
	char			*digest_choice_string;

	iser_conn = (iser_conn_t *)ic->ic_transport_private;

	/*
	 * Validate the final negotiated operational parameters,
	 * and save a copy.
	 */
	if ((nvrc = nvlist_lookup_string(negotiated_nvl,
	    "HeaderDigest", &digest_choice_string)) != ENOENT) {
		ASSERT(nvrc == 0);

		/*
		 * Per the iSER RFC, override the negotiated value with "None"
		 */
		iser_conn->ic_op_params.op_header_digest = B_FALSE;
	}

	if ((nvrc = nvlist_lookup_string(negotiated_nvl,
	    "DataDigest", &digest_choice_string)) != ENOENT) {
		ASSERT(nvrc == 0);

		/*
		 * Per the iSER RFC, override the negotiated value with "None"
		 */
		iser_conn->ic_op_params.op_data_digest = B_FALSE;
	}

	if ((nvrc = nvlist_lookup_boolean_value(negotiated_nvl,
	    "RDMAExtensions", &boolean_val)) != ENOENT) {
		ASSERT(nvrc == 0);
		iser_conn->ic_op_params.op_rdma_extensions = boolean_val;
	}

	if ((nvrc = nvlist_lookup_boolean_value(negotiated_nvl,
	    "OFMarker", &boolean_val)) != ENOENT) {
		ASSERT(nvrc == 0);
		/*
		 * Per the iSER RFC, override the negotiated value with "No"
		 */
		iser_conn->ic_op_params.op_ofmarker = B_FALSE;
	}

	if ((nvrc = nvlist_lookup_boolean_value(negotiated_nvl,
	    "IFMarker", &boolean_val)) != ENOENT) {
		ASSERT(nvrc == 0);
		/*
		 * Per the iSER RFC, override the negotiated value with "No"
		 */
		iser_conn->ic_op_params.op_ifmarker = B_FALSE;
	}

	if ((nvrc = nvlist_lookup_uint64(negotiated_nvl,
	    "TargetRecvDataSegmentLength", &uint64_val)) != ENOENT) {
		ASSERT(nvrc == 0);
		iser_conn->ic_op_params.op_target_recv_data_segment_length =
		    uint64_val;
	}

	if ((nvrc = nvlist_lookup_uint64(negotiated_nvl,
	    "InitiatorRecvDataSegmentLength", &uint64_val)) != ENOENT) {
		ASSERT(nvrc == 0);
		iser_conn->ic_op_params.op_initiator_recv_data_segment_length =
		    uint64_val;
	}

	if ((nvrc = nvlist_lookup_uint64(negotiated_nvl,
	    "MaxOutstandingUnexpectedPDUs", &uint64_val)) != ENOENT) {
		ASSERT(nvrc == 0);
		iser_conn->ic_op_params.op_max_outstanding_unexpected_pdus =
		    uint64_val;
	}

	/* Test boolean values which are required by RFC 5046 */
#ifdef ISER_DEBUG
	ASSERT(iser_conn->ic_op_params.op_rdma_extensions == B_TRUE);
	ASSERT(iser_conn->ic_op_params.op_header_digest == B_FALSE);
	ASSERT(iser_conn->ic_op_params.op_data_digest == B_FALSE);
	ASSERT(iser_conn->ic_op_params.op_ofmarker == B_FALSE);
	ASSERT(iser_conn->ic_op_params.op_ifmarker == B_FALSE);
#endif
}


/*
 * iser_conn_is_capable() verifies that the passed connection is provided
 * for by an iSER-capable link.
 * NOTE: When utilizing InfiniBand RC as an RCaP, this routine will check
 * if the link is on IPoIB. This only indicates a chance that the link is
 * on an RCaP, and thus iSER-capable, since we may be running on an IB-Eth
 * gateway, or other IB but non-RCaP link. Rather than fully establishing the
 * link to verify RCaP here, we instead will return B_TRUE
 * indicating the link is iSER-capable, if the link is IPoIB. If then in
 * iser_ini_conn_create() the link proves not be RCaP, IDM will fall back
 * to using the IDM Sockets transport.
 */
/* ARGSUSED */
static boolean_t
iser_conn_is_capable(idm_conn_req_t *cr, idm_transport_caps_t *caps)
{
	/* A NULL value for laddr indicates implicit source */
	return (iser_path_exists(NULL, &cr->cr_ini_dst_addr));
}

/*
 * iser_pdu_tx() transmits a Control PDU via the iSER channel. We pull the
 * channel out of the idm_conn_t passed in, and pass it and the pdu to the
 * iser_xfer routine.
 */
static void
iser_pdu_tx(idm_conn_t *ic, idm_pdu_t *pdu)
{
	iser_conn_t	*iser_conn;
	iser_status_t	iser_status;

	iser_conn = (iser_conn_t *)ic->ic_transport_private;

	iser_status = iser_xfer_ctrlpdu(iser_conn->ic_chan, pdu);
	if (iser_status != ISER_STATUS_SUCCESS) {
		ISER_LOG(CE_WARN, "iser_pdu_tx: failed iser_xfer_ctrlpdu: "
		    "ic (0x%p) pdu (0x%p)", (void *) ic, (void *) pdu);
		/* Fail this PDU transmission */
		idm_pdu_complete(pdu, IDM_STATUS_FAIL);
	}

	/*
	 * We successfully posted this PDU for transmission.
	 * The completion handler will invoke idm_pdu_complete()
	 * with the completion status. See iser_cq.c for more
	 * information.
	 */
}

/*
 * iser_buf_tx_to_ini() transmits the data buffer encoded in idb to the
 * initiator to fulfill SCSI Read commands. An iser_xfer routine is invoked
 * to implement the RDMA operations.
 *
 * Caller holds idt->idt_mutex.
 */
static idm_status_t
iser_buf_tx_to_ini(idm_task_t *idt, idm_buf_t *idb)
{
	iser_status_t	iser_status;
	idm_status_t	idm_status = IDM_STATUS_SUCCESS;

	ASSERT(mutex_owned(&idt->idt_mutex));

	iser_status = iser_xfer_buf_to_ini(idt, idb);

	if (iser_status != ISER_STATUS_SUCCESS) {
		ISER_LOG(CE_WARN, "iser_buf_tx_to_ini: failed "
		    "iser_xfer_buf_to_ini: idt (0x%p) idb (0x%p)",
		    (void *) idt, (void *) idb);
		idm_buf_tx_to_ini_done(idt, idb, IDM_STATUS_ABORTED);
		return (IDM_STATUS_FAIL);
	}

	/*
	 * iSCSIt's Data Completion Notify callback is invoked from
	 * the Work Request Send completion Handler
	 */

	mutex_exit(&idt->idt_mutex);
	return (idm_status);
}

/*
 * iser_buf_tx_from_ini() transmits data from the initiator into the buffer
 * in idb to fulfill SCSI Write commands. An iser_xfer routine is invoked
 * to implement the RDMA operations.
 *
 * Caller holds idt->idt_mutex.
 */
static idm_status_t
iser_buf_rx_from_ini(idm_task_t *idt, idm_buf_t *idb)
{
	iser_status_t	iser_status;
	idm_status_t	idm_status = IDM_STATUS_SUCCESS;

	ASSERT(mutex_owned(&idt->idt_mutex));

	iser_status = iser_xfer_buf_from_ini(idt, idb);

	if (iser_status != ISER_STATUS_SUCCESS) {
		ISER_LOG(CE_WARN, "iser_buf_rx_from_ini: failed "
		    "iser_xfer_buf_from_ini: idt (0x%p) idb (0x%p)",
		    (void *) idt, (void *) idb);
		idm_buf_rx_from_ini_done(idt, idb, IDM_STATUS_ABORTED);
		return (IDM_STATUS_FAIL);
	}

	/*
	 * iSCSIt's Data Completion Notify callback is invoked from
	 * the Work Request Send completion Handler
	 */

	mutex_exit(&idt->idt_mutex);
	return (idm_status);
}

/*
 * iser_buf_alloc() allocates a buffer and registers it with the IBTF for
 * use with iSER. Each HCA has it's own kmem cache for establishing a pool
 * of registered buffers, when once initially allocated, will remain
 * registered with the HCA. This routine is invoked only on the target,
 * where we have the requirement to pre-allocate buffers for the upper layers.
 * Note: buflen is compared to ISER_DEFAULT_BUFLEN, and allocation is failed
 * if the requested buflen is larger than our default.
 */
/* ARGSUSED */
static idm_status_t
iser_buf_alloc(idm_buf_t *idb, uint64_t buflen)
{
	iser_conn_t	*iser_conn;
	iser_hca_t	*iser_hca;
	iser_buf_t	*iser_buf;

	if (buflen > ISER_DEFAULT_BUFLEN) {
		return (IDM_STATUS_FAIL);
	}

	iser_conn = (iser_conn_t *)idb->idb_ic->ic_transport_private;
	iser_hca = iser_conn->ic_chan->ic_hca;

	/*
	 * Allocate a buffer from this HCA's cache. Once initialized, these
	 * will remain allocated and registered (see above).
	 */
	iser_buf = kmem_cache_alloc(iser_hca->iser_buf_cache, KM_NOSLEEP);
	if (iser_buf == NULL) {
		ISER_LOG(CE_NOTE, "iser_buf_alloc: alloc failed");
		return (IDM_STATUS_FAIL);
	}

	/* Set the allocated data buffer pointer in the IDM buf handle */
	idb->idb_buf = iser_buf->buf;

	/* Set the private buf and reg handles in the IDM buf handle */
	idb->idb_buf_private = (void *)iser_buf;
	idb->idb_reg_private = (void *)iser_buf->iser_mr;

	return (IDM_STATUS_SUCCESS);
}


/*
 * iser_buf_free() frees the buffer handle passed in. Note that the cached
 * kmem object has an HCA-registered buffer in it which will not be freed.
 * This allows us to build up a cache of pre-allocated and registered
 * buffers for use on the target.
 */
static void
iser_buf_free(idm_buf_t *buf)
{
	iser_buf_t	*iser_buf;

	iser_buf = buf->idb_buf_private;
	kmem_cache_free(iser_buf->cache, iser_buf);
}


/*
 * iser_buf_alloc_sgl() allocates a idm_buf_t and links it with the scatter
 * gather list of buffers passed in by the caller.  The SGL buffers are
 * registered with the IBTF for use with iSER. This routine is invoked only
 * on the target.
 */


/* ARGSUSED */
static idm_status_t
iser_buf_alloc_sgl(idm_buf_t *idb, struct stmf_sglist_ent *sglp,
	uint32_t numbufs, uint32_t flags)
{
	iser_conn_t	*iser_conn;
	iser_hca_t	*iser_hca;
	iser_sgl_t	*iser_sgl;
	int		i;
	ibt_iov_attr_t	iov_attr;
	ibt_iov_t	*iov_arr;
	uint32_t	numpages, totlen;
	ibt_wr_ds_t	*iods;
	ibt_status_t	ibt_status;

	iser_conn = (iser_conn_t *)idb->idb_ic->ic_transport_private;
	iser_hca = iser_conn->ic_chan->ic_hca;

	/*
	 * XXX
	 * Might be worth considering allocating iov_arr w/max sgl size
	 * and using a kmem_cache.  Same for iser_sgl.
	 */
	/* iov_arr can be big, so do not use kernel stack for it */
	iov_arr = kmem_alloc(numbufs * sizeof (ibt_iov_t), KM_NOSLEEP);
	if (iov_arr == NULL) {
		ISER_LOG(CE_NOTE, "iser_buf_alloc_sgl: alloc failed");
		return (IDM_STATUS_FAIL);
	}
	iov_attr.iov_as = NULL;
	iov_attr.iov = iov_arr;
	iov_attr.iov_buf = NULL;
	iov_attr.iov_list_len = numbufs;
	iov_attr.iov_lso_hdr_sz = 0;
	iov_attr.iov_flags = IBT_IOV_NOSLEEP;

	totlen = 0;
	for (i = 0; i < numbufs; i++) {
		iov_arr[i].iov_addr = (caddr_t)(void *)sglp[i].seg_addr;
		iov_arr[i].iov_len = sglp[i].seg_length;
		totlen += sglp[i].seg_length;
	}
	/*
	 * A 128K I/O can need 34 slots, which needs a + 2, but
	 * the ibt_map_mem_iov operation will fail unless there
	 * are 35 slots.   So we need a + 3.
	 */
	numpages = 3 +
	    ((totlen + PAGESIZE - 1) / PAGESIZE);

	iser_sgl = kmem_alloc(sizeof (iser_sgl_t) +
	    (numpages * sizeof (ibt_wr_ds_t)), KM_NOSLEEP);
	if (iser_sgl == NULL) {
		ISER_LOG(CE_NOTE, "iser_buf_alloc_sgl: alloc failed");
		kmem_free(iov_arr, numbufs * sizeof (ibt_iov_t));
		return (IDM_STATUS_FAIL);
	}
	/* Set the private buf and reg handles in the IDM buf handle */
	bzero(&iser_sgl->buf_wr, sizeof (iser_sgl->buf_wr));
	idb->idb_buf_private = iser_sgl;
	idb->idb_reg_private = NULL;
	idb->idb_buf = NULL;

	iser_sgl->buf_numpages = numpages;
	iov_attr.iov_wr_nds = numpages;

	iods = (ibt_wr_ds_t *)(iser_sgl + 1); /* pointer arithmetic */
	iser_sgl->buf_wr.wr_sgl = iods;

	ibt_status = ibt_map_mem_iov(iser_hca->hca_hdl, &iov_attr,
	    (ibt_all_wr_t *)&iser_sgl->buf_wr, &iser_sgl->buf_mi_hdl);
	kmem_free(iov_arr, numbufs * sizeof (ibt_iov_t));

	if (ibt_status != IBT_SUCCESS) {
		ISER_LOG(CE_NOTE, "iser_buf_alloc_sgl: ibt_map_mem_iov failed"
		    " numpages %d numbufs %d totlen %d ibt_status %d",
		    numpages, numbufs, totlen, ibt_status);
		iser_buf_free_sgl(idb);
		return (ibt_status == IBT_INSUFF_RESOURCE ?
		    IDM_STATUS_NORESOURCES : IDM_STATUS_FAIL);
	}
	return (IDM_STATUS_SUCCESS);
}


/*
 * iser_buf_free_sgl() frees the buffer handle passed in.
 */
static void
iser_buf_free_sgl(idm_buf_t *idb)
{
	iser_conn_t	*iser_conn;
	iser_hca_t	*iser_hca;
	iser_sgl_t	*iser_sgl;
	int		numpages;
	ibt_status_t	stat;

	iser_conn = (iser_conn_t *)idb->idb_ic->ic_transport_private;
	iser_hca = iser_conn->ic_chan->ic_hca;

	iser_sgl = idb->idb_buf_private;
	numpages = iser_sgl->buf_numpages;
	ASSERT(numpages > 0);

	if (iser_sgl->buf_mi_hdl) {
		if ((stat = ibt_unmap_mem_iov(iser_hca->hca_hdl,
		    iser_sgl->buf_mi_hdl)) != IBT_SUCCESS) {
			ISER_LOG(CE_WARN, "ibt_unmap_mem_iov failed %d", stat);
		}
		iser_sgl->buf_mi_hdl = 0;
	}

	kmem_free(iser_sgl, sizeof (iser_sgl_t) +
	    (numpages * sizeof (ibt_wr_ds_t)));
	idb->idb_buf_private = NULL;
}

/*
 * iser_buf_setup() is invoked on the initiator in order to register memory
 * on demand for use with the iSER layer.
 */
static idm_status_t
iser_buf_setup(idm_buf_t *idb)
{
	iser_conn_t	*iser_conn;
	iser_chan_t	*iser_chan;
	iser_hca_t	*iser_hca;
	iser_buf_t	*iser_buf;
	int		status;

	ASSERT(idb->idb_buf != NULL);

	iser_conn = (iser_conn_t *)idb->idb_ic->ic_transport_private;
	ASSERT(iser_conn != NULL);

	iser_hca = iser_conn->ic_chan->ic_hca;

	iser_chan = iser_conn->ic_chan;
	ASSERT(iser_chan != NULL);

	/*
	 * Memory registration is known to be slow, so for small
	 * transfers, use pre-registered memory buffers and just
	 * copy the data into/from them at the appropriate time
	 */
	if (idb->idb_buflen < ISER_BCOPY_THRESHOLD) {
		iser_buf =
		    kmem_cache_alloc(iser_hca->iser_buf_cache, KM_NOSLEEP);

		if (iser_buf == NULL) {

			/* Fail over to dynamic registration */
			status = iser_reg_rdma_mem(iser_chan->ic_hca, idb);
			idb->idb_bufalloc = IDB_REG_MEM;
			return (status);
		}

		/*
		 * Set the allocated data buffer pointer in the IDM buf handle
		 * Data is to be copied from/to this buffer using bcopy
		 */
		idb->idb_bufptr = idb->idb_buf;
		idb->idb_bufbcopy = B_TRUE;

		idb->idb_buf = iser_buf->buf;

		/* Set the private buf and reg handles in the IDM buf handle */
		idb->idb_buf_private = (void *)iser_buf;
		idb->idb_reg_private = (void *)iser_buf->iser_mr;

		/* Ensure bufalloc'd flag is set */
		idb->idb_bufalloc = IDB_IDMALLOC;

		return (IDM_STATUS_SUCCESS);

	} else {

		/* Dynamically register the memory passed in on the idb */
		status = iser_reg_rdma_mem(iser_chan->ic_hca, idb);

		/* Ensure bufalloc'd flag is unset */
		idb->idb_bufalloc = IDB_REG_MEM;

		return (status);
	}
}

/*
 * iser_buf_teardown() is invoked on the initiator in order to register memory
 * on demand for use with the iSER layer.
 */
static void
iser_buf_teardown(idm_buf_t *idb)
{
	iser_conn_t	*iser_conn;

	iser_conn = (iser_conn_t *)idb->idb_ic->ic_transport_private;

	/* Deregister the memory passed in on the idb */
	iser_dereg_rdma_mem(iser_conn->ic_chan->ic_hca, idb);
}
