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

#include <sys/types.h>
#include <sys/ddi.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/sunddi.h>
#include <sys/sysmacros.h>
#include <sys/iscsi_protocol.h>

#include <sys/ib/clients/iser/iser.h>
#include <sys/ib/clients/iser/iser_idm.h>

/* "Tuneables" (for developers) */
uint32_t	iser_qdepth;		  /* oc_rdma_ra_out, 0 => use max */
uint32_t	iser_responder_resources; /* oc_rdma_ra_in,  0 => use max */
uint32_t	iser_ib_sendq_size = ISER_IB_SENDQ_SIZE; /*  0 => use max */
uint32_t	iser_ib_recvq_size = ISER_IB_RECVQ_SIZE; /*  0 => use max */

/*
 * iser_ib.c
 * Routines for InfiniBand transport for iSER
 *
 * This file contains the routines to interface with the IBT API to attach and
 * allocate IB resources, handle async events, and post recv work requests.
 *
 */

static iser_hca_t *iser_ib_lookup_hca_by_gid(ib_gid_t gid);
static iser_hca_t *iser_ib_lookup_hca_by_guid(ib_guid_t guid);
static iser_hca_t *iser_ib_lookup_hca_by_guid_locked(ib_guid_t guid);

static iser_hca_t *iser_ib_alloc_hca(ib_guid_t guid);
static int iser_ib_free_hca(iser_hca_t *hca);
static int iser_ib_update_hcaports(iser_hca_t *hca);
static int iser_ib_init_hcas(void);
static int iser_ib_fini_hcas(void);

static iser_sbind_t *iser_ib_get_bind(
    iser_svc_t *iser_svc, ib_guid_t hca_guid, ib_gid_t gid);
static int iser_ib_activate_port(
    idm_svc_t *idm_svc, ib_guid_t guid, ib_gid_t gid);
static void iser_ib_deactivate_port(ib_guid_t hca_guid, ib_gid_t gid);

static void iser_ib_init_qp(iser_chan_t *chan, uint_t sq_size, uint_t rq_size);
static void iser_ib_fini_qp(iser_qp_t *qp);

static int iser_ib_setup_cq(ibt_hca_hdl_t hca_hdl, uint_t cq_size,
    ibt_cq_hdl_t *cq_hdl);

static void iser_ib_setup_chanargs(uint8_t hca_port, ibt_cq_hdl_t scq_hdl,
    ibt_cq_hdl_t rcq_hdl, uint_t sq_size, uint_t rq_size,
    iser_hca_t *hca, ibt_rc_chan_alloc_args_t *cargs);

static void iser_ib_handle_portup_event(ibt_hca_hdl_t hdl,
    ibt_async_event_t *event);
static void iser_ib_handle_portdown_event(ibt_hca_hdl_t hdl,
    ibt_async_event_t *event);
static void iser_ib_handle_hca_attach_event(ibt_hca_hdl_t hdl,
    ibt_async_event_t *event);
static void iser_ib_handle_hca_detach_event(ibt_hca_hdl_t hdl,
    ibt_async_event_t *event);

static void iser_ib_post_recv_task(void *arg);

static struct ibt_clnt_modinfo_s iser_ib_modinfo = {
	IBTI_V_CURR,
	IBT_STORAGE_DEV,
	iser_ib_async_handler,
	NULL,
	"iSER"
};

/*
 * iser_ib_init
 *
 * This function registers the HCA drivers with IBTF and registers and binds
 * iSER as a service with IBTF.
 */
int
iser_ib_init(void)
{
	int		status;

	mutex_init(&iser_state->is_hcalist_lock, NULL, MUTEX_DRIVER, NULL);
	mutex_enter(&iser_state->is_hcalist_lock);
	list_create(&iser_state->is_hcalist, sizeof (iser_hca_t),
	    offsetof(iser_hca_t, hca_node));
	iser_state->is_ibt_state = ISER_IBT_STATE_ATTACHING;

	/*
	 * Register the iSER module instance with IBTF. A asynchronous
	 * event handler is passed into IBTF via iser_ib_modinfo. The
	 * list of HCAs available in the system are to be obtained by
	 * querying IBTF, HCA Attach Event notifications are only sent
	 * for new HCA devices added to the operating system. If the
	 * registration is successful, the event handler can receive
	 * link state change notifications at any time after iSER has
	 * opened the HCA. The initial initialization and other
	 * reconfiguration changes are synchronized with is_hcalist_lock.
	 */
	status = ibt_attach(&iser_ib_modinfo, iser_state->is_dip, iser_state,
	    &iser_state->is_ibhdl);
	if (status != DDI_SUCCESS) {
		ISER_LOG(CE_NOTE, "iser_ib_init: ibt_attach failed (0x%x)",
		    status);

		list_destroy(&iser_state->is_hcalist);
		mutex_exit(&iser_state->is_hcalist_lock);
		mutex_destroy(&iser_state->is_hcalist_lock);
		return (DDI_FAILURE);
	}

	/* Create the global work request kmem_cache */
	iser_state->iser_wr_cache = kmem_cache_create("iser_wr_cache",
	    sizeof (iser_wr_t), 0, NULL, NULL, NULL,
	    iser_state, NULL, KM_SLEEP);

	/*
	 * Get the list of HCAs known to IBTF and save it in
	 * iSER state structure. HCA Attach Events are only
	 * received for new HCA devices added to the operating
	 * system.
	 */
	status = iser_ib_init_hcas();
	if (status != DDI_SUCCESS) {
		/* HCAs failed to initialize, tear it down */
		ISER_LOG(CE_NOTE, "iser_ib_init: failed to initialize HCAs");

		kmem_cache_destroy(iser_state->iser_wr_cache);
		iser_state->is_ibt_state = ISER_IBT_STATE_DETACHING;
		mutex_exit(&iser_state->is_hcalist_lock);
		(void) ibt_detach(iser_state->is_ibhdl);
		iser_state->is_ibhdl = NULL;
		list_destroy(&iser_state->is_hcalist);
		mutex_destroy(&iser_state->is_hcalist_lock);
		return (DDI_FAILURE);
	}
	iser_state->is_ibt_state = ISER_IBT_STATE_ATTACHED;
	mutex_exit(&iser_state->is_hcalist_lock);

	/*
	 * Target will register iSER as a service with IBTF
	 * and bind the service to the active HCA ports when
	 * the COMSTAR target comes online
	 */

	return (DDI_SUCCESS);
}

/*
 * iser_ib_fini
 *
 * This function detaches iand de-registers iSER from IBTF by calling
 * ibt_detach(). All the HCAs that are opened by iSER (ibt_open_hca())
 * are required to be closed before calling ibt_detach(). IBTF will
 * wait for all the asynchronous handler requests to complete before
 * detaching iSER and setting the handler to NULL. There is a small
 * interval of time during which the handler remains active after
 * ibt_detach() is called. iSER prevents processing of new events
 * e.g. HCA Attach, after ibt_detach() is called by keeping track of
 * this detach state (ISER_IBT_DETACHING)
 *
 */
int
iser_ib_fini(void)
{
	/* IDM would have already disabled all the services */

	/* Close all the HCAs and remove them from the global HCA list */
	if (iser_ib_fini_hcas() != DDI_SUCCESS)
		return (DDI_FAILURE);

	/* Teardown the global work request kmem_cache */
	kmem_cache_destroy(iser_state->iser_wr_cache);

	if (iser_state->is_ibhdl != NULL) {
		(void) ibt_detach(iser_state->is_ibhdl);
		iser_state->is_ibhdl = NULL;
	}
	/* It is alright to destroy the list and mutex after ibt_detach() */
	list_destroy(&iser_state->is_hcalist);
	mutex_destroy(&iser_state->is_hcalist_lock);
	return (DDI_SUCCESS);
}

/*
 * iser_ib_register_service
 *
 * This function registers the iSER service using the RDMA-Aware Service ID.
 */
int
iser_ib_register_service(idm_svc_t *idm_svc)
{
	ibt_srv_desc_t	srvdesc;
	iser_svc_t	*iser_svc;
	int		status;

	bzero(&srvdesc, sizeof (ibt_srv_desc_t));

	/* Set up IBTI client callback handler from the CM */
	srvdesc.sd_handler = iser_ib_cm_handler;

	srvdesc.sd_flags = IBT_SRV_NO_FLAGS;

	iser_svc = (iser_svc_t *)idm_svc->is_iser_svc;

	/* Register the service on the specified port */
	status = ibt_register_service(
	    iser_state->is_ibhdl, &srvdesc,
	    iser_svc->is_svcid, 1, &iser_svc->is_srvhdl, NULL);

	ISER_LOG(CE_NOTE, "iser_ib_register_service: status (0x%d)", status);
	return (status);
}

/*
 * iser_ib_bind_service
 *
 * This function binds a given iSER service on all available HCA ports. The
 * current specification does not allow user to specify transport bindings
 * for each iscsi target. The ULP invokes this function to bind the target
 * to all available iser ports after checking for the presence of an IB HCA.
 * iSER is "configured" whenever an IB-capable IP address exists. The lack
 * of active IB ports is a less-fatal condition, and sockets would be used
 * as the transport even though an Infiniband HCA is configured but unusable.
 *
 */
int
iser_ib_bind_service(idm_svc_t *idm_svc)
{
	iser_hca_t	*hca;
	ib_gid_t	gid;
	int		num_ports = 0;
	int		num_binds = 0;
	int		num_inactive_binds = 0; /* if HCA ports inactive */
	int		status;
	int		i;

	ASSERT(idm_svc != NULL);
	ASSERT(idm_svc->is_iser_svc != NULL);

	/* Register the iSER service on all available ports */
	mutex_enter(&iser_state->is_hcalist_lock);
	for (hca = list_head(&iser_state->is_hcalist);
	    hca != NULL;
	    hca = list_next(&iser_state->is_hcalist, hca)) {

		for (i = 0; i < hca->hca_num_ports; i++) {
			num_ports++;
			if (hca->hca_port_info[i].p_linkstate !=
			    IBT_PORT_ACTIVE) {
				/*
				 * Move on. We will attempt to bind service
				 * in our async handler if the port comes up
				 * at a later time.
				 */
				num_inactive_binds++;
				continue;
			}

			gid = hca->hca_port_info[i].p_sgid_tbl[0];

			/* If the port is already bound, skip */
			if (iser_ib_get_bind(
			    idm_svc->is_iser_svc, hca->hca_guid, gid) == NULL) {

				status = iser_ib_activate_port(
				    idm_svc, hca->hca_guid, gid);
				if (status != IBT_SUCCESS) {
					ISER_LOG(CE_NOTE,
					    "iser_ib_bind_service: "
					    "iser_ib_activate_port failure "
					    "(0x%x)", status);
					continue;
				}
			}
			num_binds++;
		}
	}
	mutex_exit(&iser_state->is_hcalist_lock);

	if (num_binds) {
		ISER_LOG(CE_NOTE, "iser_ib_bind_service: Service (iser_svc=%p) "
		    "available on (%d) of (%d) ports",
		    idm_svc->is_iser_svc, num_binds, num_ports);
		return (ISER_STATUS_SUCCESS);
	} else if (num_inactive_binds) {
		ISER_LOG(CE_NOTE, "iser_ib_bind_service: Could not bind "
		    "service, HCA ports are not active.");
		/*
		 * still considered success, the async handler will bind
		 * the service when the port comes up at a later time
		 */
		return (ISER_STATUS_SUCCESS);
	} else {
		ISER_LOG(CE_NOTE, "iser_ib_bind_service: Did not bind service");
		return (ISER_STATUS_FAIL);
	}
}

/*
 * iser_ib_unbind_service
 *
 * This function unbinds a given service on a all HCA ports
 */
void
iser_ib_unbind_service(idm_svc_t *idm_svc)
{
	iser_svc_t	*iser_svc;
	iser_sbind_t	*is_sbind, *next_sb;

	if (idm_svc != NULL && idm_svc->is_iser_svc != NULL) {

		iser_svc = idm_svc->is_iser_svc;

		for (is_sbind = list_head(&iser_svc->is_sbindlist);
		    is_sbind != NULL;
		    is_sbind = next_sb) {
			next_sb = list_next(&iser_svc->is_sbindlist, is_sbind);
			ISER_LOG(CE_NOTE, "iser_ib_unbind_service: %p",
			    (void *)iser_svc);
			(void) ibt_unbind_service(iser_svc->is_srvhdl,
			    is_sbind->is_sbindhdl);
			list_remove(&iser_svc->is_sbindlist, is_sbind);
			kmem_free(is_sbind, sizeof (iser_sbind_t));
		}
	}
}

/* ARGSUSED */
void
iser_ib_deregister_service(idm_svc_t *idm_svc)
{
	int		rc;
	iser_svc_t	*iser_svc;

	if (idm_svc != NULL && idm_svc->is_iser_svc != NULL) {

		iser_svc = (iser_svc_t *)idm_svc->is_iser_svc;
		if ((rc = ibt_deregister_service(
		    iser_state->is_ibhdl, iser_svc->is_srvhdl))
		    != IBT_SUCCESS) {
			ISER_LOG(CE_NOTE, "ibt_deregister_service: "
			    "[%p, %p %d, %llx] failure %d",
			    (void *)idm_svc, (void *)iser_svc,
			    idm_svc->is_svc_req.sr_port,
			    (long long unsigned int)iser_svc->is_svcid, rc);
		}
		(void) ibt_release_ip_sid(iser_svc->is_svcid);
	}
}

/*
 * iser_ib_get_paths
 * This function finds the IB path between the local and the remote address.
 *
 */
int
iser_ib_get_paths(ibt_ip_addr_t *local_ip, ibt_ip_addr_t *remote_ip,
    ibt_path_info_t *path, ibt_path_ip_src_t *path_src_ip)
{
	ibt_ip_path_attr_t	ipattr;
	int			status;

	(void) bzero(&ipattr, sizeof (ibt_ip_path_attr_t));
	ipattr.ipa_dst_ip	= remote_ip;
	ipattr.ipa_src_ip	= *local_ip;
	ipattr.ipa_max_paths	= 1;
	ipattr.ipa_ndst		= 1;

	(void) bzero(path, sizeof (ibt_path_info_t));
	status = ibt_get_ip_paths(iser_state->is_ibhdl, IBT_PATH_NO_FLAGS,
	    &ipattr, path, NULL, path_src_ip);
	if (status != IBT_SUCCESS) {
		ISER_LOG(CE_NOTE, "ibt_get_ip_paths: ibt_get_ip_paths "
		    "failure: status (%d)", status);
		return (status);
	}

	if (local_ip != NULL) {
		ISER_LOG(CE_NOTE, "iser_ib_get_paths success: IP[%x to %x]",
		    local_ip->un.ip4addr, remote_ip->un.ip4addr);
	} else {
		ISER_LOG(CE_NOTE, "iser_ib_get_paths success: "
		    "IP[INADDR_ANY to %x]", remote_ip->un.ip4addr);
	}

	return (ISER_STATUS_SUCCESS);
}

/*
 * iser_ib_alloc_channel_nopathlookup
 *
 * This function allocates a reliable connected channel. This function does
 * not invoke ibt_get_ip_paths() to do the path lookup. The HCA GUID and
 * port are input to this function.
 */
iser_chan_t *
iser_ib_alloc_channel_nopathlookup(ib_guid_t hca_guid, uint8_t hca_port)
{
	iser_hca_t		*hca;
	iser_chan_t		*chan;

	/* Lookup the hca using the gid in the path info */
	hca = iser_ib_lookup_hca_by_guid(hca_guid);
	if (hca == NULL) {
		ISER_LOG(CE_NOTE, "iser_ib_alloc_channel_nopathlookup: failed "
		    "to lookup HCA(%llx) handle", (longlong_t)hca_guid);
		return (NULL);
	}

	/* The lookup function acquired a hold on the hca */

	chan = iser_ib_alloc_rc_channel(hca, hca_port);
	if (chan == NULL) {
		ISER_LOG(CE_NOTE, "iser_ib_alloc_channel_nopathlookup: failed "
		    "to alloc channel on HCA(%llx) %d",
		    (longlong_t)hca_guid, hca_port);
		iser_ib_hca_rele(hca);
		return (NULL);
	}

	/* The hold will be released by the caller of this function */

	ISER_LOG(CE_NOTE, "iser_ib_alloc_channel_nopathlookup success: "
	    "chanhdl (0x%p), HCA(%llx) %d",
	    (void *)chan->ic_chanhdl, (longlong_t)hca_guid, hca_port);

	return (chan);
}

/*
 * iser_ib_alloc_channel_pathlookup
 *
 * This function allocates a reliable connected channel but first invokes
 * ibt_get_ip_paths() with the given local and remote addres to get the
 * HCA lgid and the port number.
 */
iser_chan_t *
iser_ib_alloc_channel_pathlookup(
    ibt_ip_addr_t *local_ip, ibt_ip_addr_t *remote_ip)
{
	ibt_path_info_t		ibt_path;
	ibt_path_ip_src_t	path_src_ip;
	ib_gid_t		lgid;
	uint8_t			hca_port; /* from path */
	iser_hca_t		*hca;
	iser_chan_t		*chan;
	int			status;

	/* Lookup a path to the given destination */
	status = iser_ib_get_paths(
	    local_ip, remote_ip, &ibt_path, &path_src_ip);

	if (status != ISER_STATUS_SUCCESS) {
		ISER_LOG(CE_NOTE, "iser_ib_alloc_channel_pathlookup: faild "
		    "Path lookup IP:[%llx to %llx] failed: status (%d)",
		    (longlong_t)local_ip->un.ip4addr,
		    (longlong_t)remote_ip->un.ip4addr,
		    status);
		return (NULL);
	}

	/* get the local gid from the path info */
	lgid = ibt_path.pi_prim_cep_path.cep_adds_vect.av_sgid;

	/* get the hca port from the path info */
	hca_port = ibt_path.pi_prim_cep_path.cep_hca_port_num;

	/* Lookup the hca using the gid in the path info */
	hca = iser_ib_lookup_hca_by_gid(lgid);
	if (hca == NULL) {
		ISER_LOG(CE_NOTE, "iser_ib_alloc_channel_pathlookup: failed "
		    "to lookup HCA (%llx) handle",
		    (longlong_t)hca->hca_guid);
		return (NULL);
	}

	/* lookup function takes a hold on the hca */
	chan = iser_ib_alloc_rc_channel(hca, hca_port);
	if (chan == NULL) {
		ISER_LOG(CE_NOTE, "iser_ib_alloc_channel_pathlookup: failed "
		    "to alloc channel from IP:[%llx to %llx] on HCA (%llx) %d",
		    (longlong_t)local_ip->un.ip4addr,
		    (longlong_t)remote_ip->un.ip4addr,
		    (longlong_t)hca->hca_guid, hca_port);
		iser_ib_hca_rele(hca);
		return (NULL);
	}

	ISER_LOG(CE_NOTE, "iser_ib_alloc_channel_pathlookup success: "
	    "chanhdl (0x%p), IP:[%llx to %llx], lgid (%llx:%llx), HCA(%llx) %d",
	    (void *)chan->ic_chanhdl,
	    (longlong_t)local_ip->un.ip4addr,
	    (longlong_t)remote_ip->un.ip4addr,
	    (longlong_t)lgid.gid_prefix, (longlong_t)lgid.gid_guid,
	    (longlong_t)hca->hca_guid, hca_port);

	iser_ib_hca_rele(hca);

	chan->ic_ibt_path	= ibt_path;
	chan->ic_localip	= path_src_ip.ip_primary;
	chan->ic_remoteip	= *remote_ip;

	return (chan);
}

/*
 * iser_ib_alloc_rc_channel
 *
 * This function allocates a reliable communication channel using the specified
 * channel attributes.
 */
iser_chan_t *
iser_ib_alloc_rc_channel(iser_hca_t *hca, uint8_t hca_port)
{

	iser_chan_t			*chan;
	ibt_rc_chan_alloc_args_t	chanargs;
	uint_t				sq_size, rq_size;
	int				status;

	chan = kmem_zalloc(sizeof (iser_chan_t), KM_SLEEP);

	mutex_init(&chan->ic_chan_lock, NULL, MUTEX_DRIVER, NULL);
	mutex_init(&chan->ic_sq_post_lock, NULL, MUTEX_DRIVER, NULL);

	/* Set up the iSER channel handle with HCA */
	chan->ic_hca		= hca;

	/*
	 * Determine the queue sizes, based upon the HCA query data.
	 * For our Work Queues, we will use either our default value,
	 * or the HCA's maximum value, whichever is smaller.
	 * Zero means to always use the HCA's max value.
	 */
	sq_size = hca->hca_attr.hca_max_chan_sz;
	if (iser_ib_sendq_size)
		sq_size = min(iser_ib_sendq_size, sq_size);
	rq_size = hca->hca_attr.hca_max_chan_sz;
	if (iser_ib_recvq_size)
		rq_size = min(iser_ib_recvq_size, rq_size);

	/*
	 * For our Completion Queues, we again check the device maximum.
	 * We want to end up with CQs that are the next size up from the
	 * WQs they are servicing so that they have some overhead.
	 */
	if (hca->hca_attr.hca_max_cq_sz >= (sq_size + 1)) {
		chan->ic_sendcq_sz = sq_size + 1;
	} else {
		chan->ic_sendcq_sz = hca->hca_attr.hca_max_cq_sz;
		sq_size = chan->ic_sendcq_sz - 1;
	}

	if (hca->hca_attr.hca_max_cq_sz >= (rq_size + 1)) {
		chan->ic_recvcq_sz = rq_size + 1;
	} else {
		chan->ic_recvcq_sz = hca->hca_attr.hca_max_cq_sz;
		rq_size = chan->ic_recvcq_sz - 1;
	}

	/* Initialize the iSER channel's QP handle */
	iser_ib_init_qp(chan, sq_size, rq_size);

	/* Set up the Send Completion Queue */
	status = iser_ib_setup_cq(hca->hca_hdl, chan->ic_sendcq_sz,
	    &chan->ic_sendcq);
	if (status != ISER_STATUS_SUCCESS) {
		iser_ib_fini_qp(&chan->ic_qp);
		mutex_destroy(&chan->ic_chan_lock);
		mutex_destroy(&chan->ic_sq_post_lock);
		kmem_free(chan, sizeof (iser_chan_t));
		return (NULL);
	}
	ibt_set_cq_handler(chan->ic_sendcq, iser_ib_sendcq_handler, chan);
	(void) ibt_enable_cq_notify(chan->ic_sendcq, IBT_NEXT_COMPLETION);

	/* Set up the Receive Completion Queue */
	status = iser_ib_setup_cq(hca->hca_hdl, chan->ic_recvcq_sz,
	    &chan->ic_recvcq);
	if (status != ISER_STATUS_SUCCESS) {
		(void) ibt_free_cq(chan->ic_sendcq);
		iser_ib_fini_qp(&chan->ic_qp);
		mutex_destroy(&chan->ic_chan_lock);
		mutex_destroy(&chan->ic_sq_post_lock);
		kmem_free(chan, sizeof (iser_chan_t));
		return (NULL);
	}
	ibt_set_cq_handler(chan->ic_recvcq, iser_ib_recvcq_handler, chan);
	(void) ibt_enable_cq_notify(chan->ic_recvcq, IBT_NEXT_COMPLETION);

	/* Setup the channel arguments */
	iser_ib_setup_chanargs(hca_port, chan->ic_sendcq, chan->ic_recvcq,
	    sq_size, rq_size, hca, &chanargs);

	status = ibt_alloc_rc_channel(hca->hca_hdl,
	    IBT_ACHAN_NO_FLAGS, &chanargs, &chan->ic_chanhdl, &chan->ic_sizes);
	if (status != IBT_SUCCESS) {
		ISER_LOG(CE_NOTE, "iser_ib_alloc_rc_channel: failed "
		    "ibt_alloc_rc_channel: status (%d)", status);
		(void) ibt_free_cq(chan->ic_sendcq);
		(void) ibt_free_cq(chan->ic_recvcq);
		iser_ib_fini_qp(&chan->ic_qp);
		mutex_destroy(&chan->ic_chan_lock);
		mutex_destroy(&chan->ic_sq_post_lock);
		kmem_free(chan, sizeof (iser_chan_t));
		return (NULL);
	}

	/* Set the 'channel' as the client private data */
	(void) ibt_set_chan_private(chan->ic_chanhdl, chan);

	return (chan);
}

/*
 * iser_ib_open_rc_channel
 * This function opens a RC connection on the given allocated RC channel
 */
int
iser_ib_open_rc_channel(iser_chan_t *chan)
{
	ibt_ip_cm_info_t	ipcm_info;
	iser_private_data_t	iser_priv_data;
	ibt_chan_open_args_t	ocargs;
	ibt_rc_returns_t	ocreturns;
	int			status;
	iser_hca_t		*hca;

	hca = chan->ic_hca;

	mutex_enter(&chan->ic_chan_lock);

	/*
	 * For connection establishment, the initiator sends a CM REQ using the
	 * iSER RDMA-Aware Service ID. Included are the source and destination
	 * IP addresses, and the src port.
	 */
	bzero(&ipcm_info, sizeof (ibt_ip_cm_info_t));
	ipcm_info.src_addr = chan->ic_localip;
	ipcm_info.dst_addr = chan->ic_remoteip;
	ipcm_info.src_port = chan->ic_lport;

	/*
	 * The CM Private Data field defines the iSER connection parameters
	 * such as zero based virtual address exception (ZBVAE) and Send with
	 * invalidate Exception (SIE).
	 *
	 * Solaris IBT does not currently support ZBVAE or SIE.
	 */
	iser_priv_data.rsvd1	= 0;
	iser_priv_data.sie	= 1;
	iser_priv_data.zbvae	= 1;

	status = ibt_format_ip_private_data(&ipcm_info,
	    sizeof (iser_private_data_t), &iser_priv_data);
	if (status != IBT_SUCCESS) {
		ISER_LOG(CE_NOTE, "iser_ib_open_rc_channel failed: %d", status);
		mutex_exit(&chan->ic_chan_lock);
		return (status);
	}

	/*
	 * Set the SID we are attempting to connect to, based upon the
	 * remote port number.
	 */
	chan->ic_ibt_path.pi_sid = ibt_get_ip_sid(IPPROTO_TCP, chan->ic_rport);

	/* Set up the args for the channel open */
	bzero(&ocargs, sizeof (ibt_chan_open_args_t));
	ocargs.oc_path			= &chan->ic_ibt_path;
	ocargs.oc_cm_handler		= iser_ib_cm_handler;
	ocargs.oc_cm_clnt_private	= iser_state;
	ocargs.oc_rdma_ra_out		= (iser_qdepth ?
	    (uint8_t)min(iser_qdepth, hca->hca_attr.hca_max_rdma_out_chan) :
	    hca->hca_attr.hca_max_rdma_out_chan);
	ocargs.oc_rdma_ra_in		= (iser_responder_resources ?
	    (uint8_t)min(iser_responder_resources,
	    hca->hca_attr.hca_max_rdma_in_chan) :
	    hca->hca_attr.hca_max_rdma_in_chan);
	ocargs.oc_path_retry_cnt	= 2;
	ocargs.oc_path_rnr_retry_cnt	= 2;
	ocargs.oc_priv_data_len		= sizeof (iser_private_data_t);
	ocargs.oc_priv_data		= &iser_priv_data;

	bzero(&ocreturns, sizeof (ibt_rc_returns_t));

	/*
	 * When ibt_open_rc_channel() is called in IBT_BLOCKING mode,
	 * it will return when the channel is opened successfully
	 * or the attempt to open the channel is terminated by the
	 * remote host or local CM. If a transport failure occurs
	 * when the channel is being opened, it will return only
	 * after any outstanding event (e.g. IBT_CM_EVENT_FAILURE),
	 * associated with this channel is processed to completion
	 * by the CM handler. In this case, it is better to destroy
	 * the channel and its associated iSER connection after
	 * ibt_open_rc_channel() returns rather than by the CM
	 * handler.
	 *
	 * To avoid deadlocks, the channel mutex ic_chan_lock must
	 * be released before calling ibt_open_rc_channel().
	 */
	mutex_exit(&chan->ic_chan_lock);

	status = ibt_open_rc_channel(chan->ic_chanhdl,
	    IBT_OCHAN_NO_FLAGS, IBT_BLOCKING, &ocargs, &ocreturns);

	if (status != IBT_SUCCESS) {
		ISER_LOG(CE_NOTE, "iser_ib_open_rc_channel failed: %d", status);
		return (status);
	}

	return (IDM_STATUS_SUCCESS);
}

/*
 * iser_ib_close_rc_channel
 * This function closes the RC channel related to this iser_chan handle.
 * We invoke this in a non-blocking, no callbacks context.
 */
void
iser_ib_close_rc_channel(iser_chan_t *chan)
{
	int			status;

	mutex_enter(&chan->ic_chan_lock);
	status = ibt_close_rc_channel(chan->ic_chanhdl, IBT_BLOCKING, NULL,
	    0, NULL, NULL, 0);
	if (status != IBT_SUCCESS) {
		ISER_LOG(CE_NOTE, "iser_ib_close_rc_channel: "
		    "ibt_close_rc_channel failed: status (%d)", status);
	}
	mutex_exit(&chan->ic_chan_lock);
}

/*
 * iser_ib_free_rc_channel
 *
 * This function tears down an RC channel's QP initialization and frees it.
 * Note that we do not need synchronization here; the channel has been
 * closed already, so we should only have completion polling occuring.  Once
 * complete, we are free to free the IBTF channel, WQ and CQ resources, and
 * our own related resources.
 */
void
iser_ib_free_rc_channel(iser_chan_t *chan)
{
	iser_qp_t	*iser_qp;

	iser_qp = &chan->ic_qp;

	/* Wait for other threads to release the lock */
	mutex_enter(&chan->ic_chan_lock);
	mutex_enter(&chan->ic_sq_post_lock);

	/* Ensure the SQ is empty */
	while (chan->ic_sq_post_count != 0) {
		mutex_exit(&chan->ic_sq_post_lock);
		mutex_exit(&chan->ic_chan_lock);
		mutex_exit(&chan->ic_conn->ic_lock);
		delay(drv_usectohz(ISER_DELAY_HALF_SECOND));
		mutex_enter(&chan->ic_conn->ic_lock);
		mutex_enter(&chan->ic_chan_lock);
		mutex_enter(&chan->ic_sq_post_lock);
	}
	mutex_destroy(&chan->ic_sq_post_lock);

	/* Ensure the RQ is empty */
	(void) ibt_flush_channel(chan->ic_chanhdl);
	mutex_enter(&iser_qp->qp_lock);
	while (iser_qp->rq_level != 0) {
		mutex_exit(&iser_qp->qp_lock);
		mutex_exit(&chan->ic_chan_lock);
		mutex_exit(&chan->ic_conn->ic_lock);
		delay(drv_usectohz(ISER_DELAY_HALF_SECOND));
		mutex_enter(&chan->ic_conn->ic_lock);
		mutex_enter(&chan->ic_chan_lock);
		mutex_enter(&iser_qp->qp_lock);
	}

	/* Free our QP handle */
	mutex_exit(&iser_qp->qp_lock);
	(void) iser_ib_fini_qp(iser_qp);

	/* Free the IBT channel resources */
	(void) ibt_free_channel(chan->ic_chanhdl);
	chan->ic_chanhdl = NULL;

	/* Free the CQs */
	(void) ibt_free_cq(chan->ic_sendcq);
	(void) ibt_free_cq(chan->ic_recvcq);

	/* Free the chan handle */
	mutex_destroy(&chan->ic_chan_lock);
	kmem_free(chan, sizeof (iser_chan_t));
}

/*
 * iser_ib_post_recv
 *
 * This function handles keeping the RQ full on a given channel.
 * This routine will mostly be run on a taskq, and will check the
 * current fill level of the RQ, and post as many WRs as necessary
 * to fill it again.
 */

int
iser_ib_post_recv_async(ibt_channel_hdl_t chanhdl)
{
	iser_chan_t	*chan;
	int		status;

	/* Pull our iSER channel handle from the private data */
	chan = (iser_chan_t *)ibt_get_chan_private(chanhdl);

	/*
	 * Caller must check that chan->ic_conn->ic_stage indicates
	 * the connection is active (not closing, not closed) and
	 * it must hold the mutex cross the check and the call to this function
	 */
	ASSERT(mutex_owned(&chan->ic_conn->ic_lock));
	ASSERT((chan->ic_conn->ic_stage >= ISER_CONN_STAGE_ALLOCATED) &&
	    (chan->ic_conn->ic_stage <= ISER_CONN_STAGE_LOGGED_IN));
	idm_conn_hold(chan->ic_conn->ic_idmc);
	status = ddi_taskq_dispatch(iser_taskq, iser_ib_post_recv_task,
	    (void *)chanhdl, DDI_NOSLEEP);
	if (status != DDI_SUCCESS) {
		idm_conn_rele(chan->ic_conn->ic_idmc);
	}

	return (status);
}

static void
iser_ib_post_recv_task(void *arg)
{
	ibt_channel_hdl_t	chanhdl = arg;
	iser_chan_t		*chan;

	/* Pull our iSER channel handle from the private data */
	chan = (iser_chan_t *)ibt_get_chan_private(chanhdl);

	iser_ib_post_recv(chanhdl);
	idm_conn_rele(chan->ic_conn->ic_idmc);
}

void
iser_ib_post_recv(ibt_channel_hdl_t chanhdl)
{
	iser_chan_t	*chan;
	iser_hca_t	*hca;
	iser_msg_t	*msg;
	ibt_recv_wr_t	*wrlist, wr[ISER_IB_RQ_POST_MAX];
	int		rq_space, msg_ret;
	int		total_num, npost;
	uint_t		nposted;
	int		status, i;
	iser_qp_t	*iser_qp;

	/* Pull our iSER channel handle from the private data */
	chan = (iser_chan_t *)ibt_get_chan_private(chanhdl);

	ASSERT(chan != NULL);

	mutex_enter(&chan->ic_conn->ic_lock);

	/* Bail out if the connection is closed; no need for more recv WRs */
	if ((chan->ic_conn->ic_stage == ISER_CONN_STAGE_CLOSING) ||
	    (chan->ic_conn->ic_stage == ISER_CONN_STAGE_CLOSED)) {
		mutex_exit(&chan->ic_conn->ic_lock);
		return;
	}

	/* get the QP handle from the iser_chan */
	iser_qp = &chan->ic_qp;

	hca = chan->ic_hca;

	if (hca == NULL) {
		ISER_LOG(CE_NOTE, "iser_ib_post_recv: unable to retrieve "
		    "HCA handle");
		mutex_exit(&chan->ic_conn->ic_lock);
		return;
	}

	/* check for space to post on the RQ */
	mutex_enter(&iser_qp->qp_lock);
	rq_space = iser_qp->rq_depth - iser_qp->rq_level;
	if (rq_space == 0) {
		/* The RQ is full, clear the pending flag and return */
		iser_qp->rq_taskqpending = B_FALSE;
		mutex_exit(&iser_qp->qp_lock);
		mutex_exit(&chan->ic_conn->ic_lock);
		return;
	}

	/* Keep track of the lowest value for rq_min_post_level */
	if (iser_qp->rq_level < iser_qp->rq_min_post_level)
		iser_qp->rq_min_post_level = iser_qp->rq_level;

	mutex_exit(&iser_qp->qp_lock);

	/* we've room to post, so pull from the msg cache */
	msg = iser_msg_get(hca, rq_space, &msg_ret);
	if (msg == NULL) {
		ISER_LOG(CE_NOTE, "iser_ib_post_recv: no message handles "
		    "available in msg cache currently");
		/*
		 * There are no messages on the cache. Wait a half-
		 * second, then try again.
		 */
		delay(drv_usectohz(ISER_DELAY_HALF_SECOND));
		status = iser_ib_post_recv_async(chanhdl);
		if (status != DDI_SUCCESS) {
			ISER_LOG(CE_NOTE, "iser_ib_post_recv: failed to "
			    "redispatch routine");
			/* Failed to dispatch, clear pending flag */
			mutex_enter(&iser_qp->qp_lock);
			iser_qp->rq_taskqpending = B_FALSE;
			mutex_exit(&iser_qp->qp_lock);
		}
		mutex_exit(&chan->ic_conn->ic_lock);
		return;
	}

	if (msg_ret != rq_space) {
		ISER_LOG(CE_NOTE, "iser_ib_post_recv: requested number of "
		    "messages not allocated: requested (%d) allocated (%d)",
		    rq_space, msg_ret);
		/* We got some, but not all, of our requested depth */
		rq_space = msg_ret;
	}

	/*
	 * Now, walk through the allocated WRs and post them,
	 * ISER_IB_RQ_POST_MAX (or less) at a time.
	 */
	wrlist = &wr[0];
	total_num = rq_space;

	while (total_num) {
		/* determine the number to post on this iteration */
		npost = (total_num > ISER_IB_RQ_POST_MAX) ?
		    ISER_IB_RQ_POST_MAX : total_num;

		/* build a list of WRs from the msg list */
		for (i = 0; i < npost; i++) {
			wrlist[i].wr_id		= (ibt_wrid_t)(uintptr_t)msg;
			wrlist[i].wr_nds	= ISER_IB_RQ_SGLIST_SIZE;
			wrlist[i].wr_sgl	= &msg->msg_ds;
			msg = msg->nextp;
		}

		/* post the list to the RQ */
		nposted = 0;
		status = ibt_post_recv(chanhdl, wrlist, npost, &nposted);
		if ((status != IBT_SUCCESS) || (nposted != npost)) {
			ISER_LOG(CE_NOTE, "iser_ib_post_recv: ibt_post_recv "
			    "failed: requested (%d) posted (%d) status (%d)",
			    npost, nposted, status);
			total_num -= nposted;
			break;
		}

		/* decrement total number to post by the number posted */
		total_num -= nposted;
	}

	mutex_enter(&iser_qp->qp_lock);
	if (total_num != 0) {
		ISER_LOG(CE_NOTE, "iser_ib_post_recv: unable to fill RQ, "
		    "failed to post (%d) WRs", total_num);
		iser_qp->rq_level += rq_space - total_num;
	} else {
		iser_qp->rq_level += rq_space;
	}

	/*
	 * Now that we've filled the RQ, check that all of the recv WRs
	 * haven't just been immediately consumed. If so, taskqpending is
	 * still B_TRUE, so we need to fire off a taskq thread to post
	 * more WRs.
	 */
	if (iser_qp->rq_level == 0) {
		mutex_exit(&iser_qp->qp_lock);
		status = iser_ib_post_recv_async(chanhdl);
		if (status != DDI_SUCCESS) {
			ISER_LOG(CE_NOTE, "iser_ib_post_recv: failed to "
			    "dispatch followup routine");
			/* Failed to dispatch, clear pending flag */
			mutex_enter(&iser_qp->qp_lock);
			iser_qp->rq_taskqpending = B_FALSE;
			mutex_exit(&iser_qp->qp_lock);
		}
	} else {
		/*
		 * We're done, we've filled the RQ. Clear the taskq
		 * flag so that we can run again.
		 */
		iser_qp->rq_taskqpending = B_FALSE;
		mutex_exit(&iser_qp->qp_lock);
	}

	mutex_exit(&chan->ic_conn->ic_lock);
}

/*
 * iser_ib_handle_hca_attach_event()
 * This handles the IBT_HCA_ATTACH unaffiliated asynchronous event.
 */
/* ARGSUSED */
static void
iser_ib_handle_hca_attach_event(ibt_hca_hdl_t hdl, ibt_async_event_t *event)
{
	iser_hca_t		*hca;

	ISER_LOG(CE_NOTE, "iser_ib_handle_hca_attach: HCA(0x%llx)",
	    (longlong_t)event->ev_hca_guid);

	mutex_enter(&iser_state->is_hcalist_lock);
	if (iser_state->is_ibt_state == ISER_IBT_STATE_DETACHING) {
		ISER_LOG(CE_NOTE, "iser_ib_handle_hca_attach failed: "
		    "iSER is detaching from IBTF, HCA(0x%llx)",
		    (longlong_t)event->ev_hca_guid);
		mutex_exit(&iser_state->is_hcalist_lock);
		return;
	}

	hca = iser_ib_lookup_hca_by_guid_locked(event->ev_hca_guid);
	if (hca != NULL) {
		ISER_LOG(CE_NOTE, "iser_ib_handle_hca_attach failed: "
		    "HCA(0x%llx) already exists in list",
		    (longlong_t)event->ev_hca_guid);
		iser_ib_hca_rele(hca);
		mutex_exit(&iser_state->is_hcalist_lock);
		return;
	}

	/* HCA is just made available, port events will follow */
	hca = iser_ib_alloc_hca(event->ev_hca_guid);
	if (hca == NULL) {
		ISER_LOG(CE_NOTE, "iser_ib_handle_hca_attach "
		    "iser_ib_alloc_hca_failed: HCA(0x%llx)",
		    (longlong_t)event->ev_hca_guid);
		mutex_exit(&iser_state->is_hcalist_lock);
		return;
	}
	list_insert_tail(&iser_state->is_hcalist, hca);
	iser_state->is_num_hcas++;
	mutex_exit(&iser_state->is_hcalist_lock);
}

/*
 * iser_ib_handle_portup_event()
 * This handles the IBT_EVENT_PORT_UP unaffiliated asynchronous event.
 *
 * To facilitate a seamless bringover of the port and configure the CM service
 * for inbound iSER service requests on this newly active port, the existing
 * IDM services will be checked for iSER support.
 * If an iSER service was already created, then this service will simply be
 * bound to the gid of the newly active port. If on the other hand, the CM
 * service did not exist, i.e. only socket communication, then a new CM
 * service will be first registered with the saved service parameters and
 * then bound to the newly active port.
 *
 */
/* ARGSUSED */
static void
iser_ib_handle_portup_event(ibt_hca_hdl_t hdl, ibt_async_event_t *event)
{
	iser_hca_t		*hca;
	ib_gid_t		gid;
	idm_svc_t		*idm_svc;
	int			status;

	ISER_LOG(CE_NOTE, "iser_ib_handle_portup_event: HCA(0x%llx) port(%d)",
	    (longlong_t)event->ev_hca_guid, event->ev_port);

	hca = iser_ib_lookup_hca_by_guid(event->ev_hca_guid);
	if (hca == NULL) {

		ISER_LOG(CE_NOTE, "iser_ib_handle_portup_event "
		    "iser_ib_lookup_hca_failed: HCA(0x%llx) port(%d)",
		    (longlong_t)event->ev_hca_guid, event->ev_port);
		return;
	}

	status = iser_ib_update_hcaports(hca);
	if (status != IBT_SUCCESS) {
		ISER_LOG(CE_NOTE, "iser_ib_handle_portup_event "
		    "status(0x%x): iser_ib_update_hcaports failed: "
		    "HCA(0x%llx) port(%d)", status,
		    (longlong_t)event->ev_hca_guid, event->ev_port);

		iser_ib_hca_rele(hca);
		return;
	}

	gid = hca->hca_port_info[event->ev_port - 1].p_sgid_tbl[0];

	/*
	 * Iterate through the global list of IDM target services
	 * and check for existing iSER CM service.
	 */
	mutex_enter(&idm.idm_global_mutex);
	for (idm_svc = list_head(&idm.idm_tgt_svc_list);
	    idm_svc != NULL;
	    idm_svc = list_next(&idm.idm_tgt_svc_list, idm_svc)) {


		if (idm_svc->is_iser_svc == NULL) {

			ISER_LOG(CE_NOTE, "iser_ib_handle_portup_event: "
			    "Establishing a new service for iSER requests");

			/* Establish a new CM service for iSER requests */
			status = iser_tgt_svc_create(
			    &idm_svc->is_svc_req, idm_svc);

			if (status != IBT_SUCCESS) {
				ISER_LOG(CE_NOTE, "iser_ib_handle_portup_event "
				    "status(0x%x): iser_tgt_svc_create failed: "
				    "HCA(0x%llx) port(%d)", status,
				    (longlong_t)event->ev_hca_guid,
				    event->ev_port);

				continue;
			}

			status = iser_ib_register_service(idm_svc);

			if (status != IBT_SUCCESS) {
				ISER_LOG(CE_NOTE, "iser_ib_handle_portup_event "
				    "status(0x%x): register service failed: "
				    "HCA(0x%llx) port(%d)", status,
				    (longlong_t)event->ev_hca_guid,
				    event->ev_port);

				continue;
			}
		}

		status = iser_ib_activate_port(
		    idm_svc, event->ev_hca_guid, gid);
		if (status != IBT_SUCCESS) {

			ISER_LOG(CE_NOTE, "iser_ib_handle_portup_event "
			    "status(0x%x): Bind service on port "
			    "(%llx:%llx) failed",
			    status, (longlong_t)gid.gid_prefix,
			    (longlong_t)gid.gid_guid);

			continue;
		}
		ISER_LOG(CE_NOTE, "iser_ib_handle_portup_event: service bound "
		    "HCA(0x%llx) port(%d)", (longlong_t)event->ev_hca_guid,
		    event->ev_port);
	}
	mutex_exit(&idm.idm_global_mutex);
	iser_ib_hca_rele(hca);

	ISER_LOG(CE_NOTE, "iser_ib_handle_portup_event success: "
	    "HCA(0x%llx) port(%d)", (longlong_t)event->ev_hca_guid,
	    event->ev_port);
}

/*
 * iser_ib_handle_portdown_event()
 * This handles the IBT_EVENT_PORT_DOWN unaffiliated asynchronous error.
 *
 * Unconfigure the CM service on the deactivated port and teardown the
 * connections that are using the CM service.
 */
/* ARGSUSED */
static void
iser_ib_handle_portdown_event(ibt_hca_hdl_t hdl, ibt_async_event_t *event)
{
	iser_hca_t		*hca;
	ib_gid_t		gid;
	int			status;

	/*
	 * Query all ports on the HCA and update the port information
	 * maintainted in the iser_hca_t structure
	 */
	hca = iser_ib_lookup_hca_by_guid(event->ev_hca_guid);
	if (hca == NULL) {
		ISER_LOG(CE_NOTE, "iser_ib_handle_portdown_event: failed "
		    "to lookup HCA(0x%llx)", (longlong_t)event->ev_hca_guid);
		return;
	}

	status = iser_ib_update_hcaports(hca);
	if (status != IBT_SUCCESS) {
		ISER_LOG(CE_NOTE, "iser_ib_handle_portdown_event status(0x%x): "
		    "ibt_ib_update_hcaports failed: HCA(0x%llx) port(%d)",
		    status, (longlong_t)event->ev_hca_guid, event->ev_port);

		iser_ib_hca_rele(hca);
		return;
	}

	/* get the gid of the new port */
	gid = hca->hca_port_info[event->ev_port - 1].p_sgid_tbl[0];
	iser_ib_deactivate_port(event->ev_hca_guid, gid);
	iser_ib_hca_rele(hca);

	ISER_LOG(CE_NOTE, "iser_ib_handle_portdown_event success: "
	    "HCA(0x%llx) port(%d)", (longlong_t)event->ev_hca_guid,
	    event->ev_port);
}

/*
 * iser_ib_handle_hca_detach_event()
 * Quiesce all activity bound for the port, teardown the connection, unbind
 * iSER services on all ports and release the HCA handle.
 */
/* ARGSUSED */
static void
iser_ib_handle_hca_detach_event(ibt_hca_hdl_t hdl, ibt_async_event_t *event)
{
	iser_hca_t	*nexthca, *hca;
	int		i, status;

	ISER_LOG(CE_NOTE, "iser_ib_handle_hca_detach_event: HCA(0x%llx)",
	    (longlong_t)event->ev_hca_guid);

	hca = iser_ib_lookup_hca_by_guid(event->ev_hca_guid);
	if (hca == NULL) {
		ISER_LOG(CE_WARN, "iser_ib_handle_hca_detach: "
		    "Failed to lookup HCA(0x%llx)",
		    (longlong_t)event->ev_hca_guid);
		return;
	}

	for (i = 0; i < hca->hca_num_ports; i++) {
		iser_ib_deactivate_port(hca->hca_guid,
		    hca->hca_port_info[i].p_sgid_tbl[0]);
	}
	iser_ib_hca_rele(hca);

	/*
	 * Update the HCA list maintained in the iser_state. Free the
	 * resources allocated to the HCA, i.e. caches, protection domain
	 */
	mutex_enter(&iser_state->is_hcalist_lock);
	for (hca = list_head(&iser_state->is_hcalist);
	    hca != NULL;
	    hca = nexthca) {

		nexthca = list_next(&iser_state->is_hcalist, hca);

		if (hca->hca_guid == event->ev_hca_guid) {

			list_remove(&iser_state->is_hcalist, hca);
			iser_state->is_num_hcas--;
			mutex_exit(&iser_state->is_hcalist_lock);

			status = iser_ib_free_hca(hca);
			if (status != DDI_SUCCESS) {
				ISER_LOG(CE_WARN, "iser_ib_handle_hca_detach: "
				    "Failed to free hca(%p)", (void *)hca);
			}
			return;
		}
	}
	/* Should never happen that the HCA was not found in the list */
	mutex_exit(&iser_state->is_hcalist_lock);

}

/*
 * iser_ib_async_handler
 * An IBT Asynchronous Event handler is registered with the framework and
 * passed via the ibt_attach() routine. The HCA Attach Event is received
 * for a new HCA device added to the system after ibt_attach(). The link
 * state changes such as Port Up and Port Down and HCA Detach Events are
 * received for the HCAs which are opened (ibt_open_hca) by iSER. To avoid
 * deadlocks no events should be processed after iSER has begun to teardown
 * its resources in iser_ib_fini()
 */
/* ARGSUSED */
void
iser_ib_async_handler(void *clntp, ibt_hca_hdl_t hdl, ibt_async_code_t code,
    ibt_async_event_t *event)
{

	/* Handler will check if iSER is detaching from IBTF */
	switch (code) {
	case IBT_EVENT_PORT_UP:
		iser_ib_handle_portup_event(hdl, event);
		break;

	case IBT_ERROR_PORT_DOWN:
		iser_ib_handle_portdown_event(hdl, event);
		break;

	case IBT_HCA_ATTACH_EVENT:
		iser_ib_handle_hca_attach_event(hdl, event);
		break;

	case IBT_HCA_DETACH_EVENT:
		iser_ib_handle_hca_detach_event(hdl, event);
		break;

	default:
		break;
	}
}

/*
 * iser_ib_init_hcas
 *
 * This function opens all the HCA devices, gathers the HCA state information
 * and adds the HCA handle for each HCA found in the iser_soft_state. The
 * is_hcalist_lock is held by the caller of this function.
 */
static int
iser_ib_init_hcas(void)
{
	ib_guid_t	*guid;
	int		num_hcas;
	int		i;
	iser_hca_t	*hca;

	/* Retrieve the HCA list */
	num_hcas = ibt_get_hca_list(&guid);
	if (num_hcas == 0) {
		/*
		 * This shouldn't happen, but might if we have all HCAs
		 * detach prior to initialization.
		 */
		return (DDI_FAILURE);
	}

	for (i = 0; i < num_hcas; i++) {

		ISER_LOG(CE_NOTE, "iser_ib_init_hcas: initializing HCA "
		    "(0x%llx)", (longlong_t)guid[i]);

		hca = iser_ib_alloc_hca(guid[i]);
		if (hca == NULL) {
			/* This shouldn't happen, teardown and fail */
			(void) ibt_free_hca_list(guid, num_hcas);
			return (DDI_FAILURE);
		}

		list_insert_tail(&iser_state->is_hcalist, hca);
		iser_state->is_num_hcas++;

	}

	/* Free the IBT HCA list */
	(void) ibt_free_hca_list(guid, num_hcas);

	/* Check that we've initialized at least one HCA */
	if (list_is_empty(&iser_state->is_hcalist)) {
		ISER_LOG(CE_NOTE, "iser_ib_init_hcas: failed to initialize "
		    "any HCAs");

		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

/*
 * iser_ib_fini_hcas
 *
 * Teardown the iSER HCA list initialized above.
 */
static int
iser_ib_fini_hcas(void)
{
	iser_hca_t	*hca;
	int		status;

	mutex_enter(&iser_state->is_hcalist_lock);
	iser_state->is_ibt_state = ISER_IBT_STATE_DETACHING;
	while ((hca = list_head(&iser_state->is_hcalist)) != NULL) {

		ISER_LOG(CE_NOTE, "iser_ib_fini_hcas: Remove HCA(0x%llx)",
		    (long long unsigned int)hca->hca_guid);
		list_remove(&iser_state->is_hcalist, hca);
		iser_state->is_num_hcas--;
		mutex_exit(&iser_state->is_hcalist_lock);
		status = iser_ib_free_hca(hca);
		if (status != IBT_SUCCESS) {
			ISER_LOG(CE_NOTE, "iser_ib_fini_hcas: failed to free "
			    "HCA during fini");
			return (DDI_FAILURE);
		}
		mutex_enter(&iser_state->is_hcalist_lock);
	}
	mutex_exit(&iser_state->is_hcalist_lock);

	return (DDI_SUCCESS);
}

/*
 * iser_ib_alloc_hca
 *
 * This function opens the given HCA device, gathers the HCA state information
 * and adds the HCA handle
 */
static iser_hca_t *
iser_ib_alloc_hca(ib_guid_t guid)
{
	iser_hca_t	*hca;
	int		status;

	/* Allocate an iser_hca_t HCA handle */
	hca = (iser_hca_t *)kmem_zalloc(sizeof (iser_hca_t), KM_SLEEP);
	idm_refcnt_init(&hca->hca_refcnt, hca);

	/* Open this HCA */
	status = ibt_open_hca(iser_state->is_ibhdl, guid, &hca->hca_hdl);
	if (status != IBT_SUCCESS) {
		ISER_LOG(CE_NOTE, "iser_ib_alloc_hca: ibt_open_hca failed:"
		    " guid (0x%llx) status (0x%x)", (longlong_t)guid, status);

		kmem_free(hca, sizeof (iser_hca_t));
		return (NULL);
	}

	hca->hca_guid		= guid;
	hca->hca_clnt_hdl	= iser_state->is_ibhdl;

	/* Query the HCA */
	status = ibt_query_hca(hca->hca_hdl, &hca->hca_attr);
	if (status != IBT_SUCCESS) {
		ISER_LOG(CE_NOTE, "iser_ib_alloc_hca: ibt_query_hca "
		    "failure: guid (0x%llx) status (0x%x)",
		    (longlong_t)guid, status);

		(void) ibt_close_hca(hca->hca_hdl);
		kmem_free(hca, sizeof (iser_hca_t));
		return (NULL);
	}

	/* Query all ports on the HCA */
	status = ibt_query_hca_ports(hca->hca_hdl, 0,
	    &hca->hca_port_info, &hca->hca_num_ports,
	    &hca->hca_port_info_sz);
	if (status != IBT_SUCCESS) {
		ISER_LOG(CE_NOTE, "iser_ib_alloc_hca: "
		    "ibt_query_hca_ports failure: guid (0x%llx) "
		    "status (0x%x)", (longlong_t)guid, status);

		(void) ibt_close_hca(hca->hca_hdl);
		kmem_free(hca, sizeof (iser_hca_t));
		return (NULL);
	}

	/* Allocate a single PD on this HCA */
	status = ibt_alloc_pd(hca->hca_hdl, IBT_PD_NO_FLAGS,
	    &hca->hca_pdhdl);
	if (status != IBT_SUCCESS) {
		ISER_LOG(CE_NOTE, "iser_ib_alloc_hca: ibt_alloc_pd "
		    "failure: guid (0x%llx) status (0x%x)",
		    (longlong_t)guid, status);

		ibt_free_portinfo(hca->hca_port_info, hca->hca_port_info_sz);
		(void) ibt_close_hca(hca->hca_hdl);
		kmem_free(hca, sizeof (iser_hca_t));
		return (NULL);
	}

	/* Initialize the message and data MR caches for this HCA */
	iser_init_hca_caches(hca);

	/* Determine the maximum sglist size on the send queue */
	if (hca->hca_attr.hca_flags2 & IBT_HCA2_RES_LKEY) {
		ASSERT(hca->hca_attr.hca_flags & IBT_HCA_WQE_SIZE_INFO);
		/* Limit max sglist to the max RDMA_READ operation */
		hca->hca_max_sgl_size =
		    hca->hca_attr.hca_conn_rdma_read_sgl_sz;
	} else {
		hca->hca_max_sgl_size = 1; /* no Zero Copy allowed */
	}

	return (hca);
}

static int
iser_ib_free_hca(iser_hca_t *hca)
{
	int			status;
	ibt_hca_portinfo_t	*hca_port_info;
	uint_t			hca_port_info_sz;

	ASSERT(hca != NULL);

	hca_port_info = hca->hca_port_info;
	hca_port_info_sz = hca->hca_port_info_sz;

	/*
	 * Wait for all the threads to release the hold
	 * on the HCA so that the resources can be freed.
	 * Any thread that has a HCA hold but released
	 * the is_hcalist_lock MUST be able to run to
	 * completion without needing to reacquire the
	 * lock.
	 */
	idm_refcnt_wait_ref(&hca->hca_refcnt);

	/*
	 * Free the memory regions before freeing
	 * the associated protection domain
	 */
	iser_fini_hca_caches(hca);

	/*
	 * The protection domain cannot be deallocated if it is
	 * still associated with any channel, memory region or
	 * memory window or destination handle
	 */
	status = ibt_free_pd(hca->hca_hdl, hca->hca_pdhdl);
	ASSERT(status == IBT_SUCCESS);
	if (status != IBT_SUCCESS) {
		ISER_LOG(CE_NOTE, "iser_ib_free_hca: failed to free PD "
		    "status=0x%x", status);
		return (DDI_FAILURE);
	}

	/*
	 * ibt_close_hca() can fail if the resources
	 * allocated to iSER on this HCA are not freed
	 */
	status = ibt_close_hca(hca->hca_hdl);
	ASSERT(status == IBT_SUCCESS);
	if (status != IBT_SUCCESS) {
		ISER_LOG(CE_NOTE, "iser_ib_free_hcas: failed to close HCA "
		    "status=0x%x", status);
		return (DDI_FAILURE);
	}

	ibt_free_portinfo(hca_port_info, hca_port_info_sz);

	idm_refcnt_destroy(&hca->hca_refcnt);

	kmem_free(hca, sizeof (iser_hca_t));
	return (DDI_SUCCESS);

}

static int
iser_ib_update_hcaports(iser_hca_t *hca)
{
	ibt_hca_portinfo_t	*pinfop, *oldpinfop;
	uint_t			size, oldsize, nport;
	int			status;

	ASSERT(hca != NULL);

	status = ibt_query_hca_ports(hca->hca_hdl, 0, &pinfop, &nport, &size);
	if (status != IBT_SUCCESS) {
		ISER_LOG(CE_NOTE, "ibt_query_hca_ports failed: %d", status);
		return (status);
	}

	oldpinfop = hca->hca_port_info;
	oldsize	= hca->hca_port_info_sz;
	hca->hca_port_info = pinfop;
	hca->hca_port_info_sz = size;

	(void) ibt_free_portinfo(oldpinfop, oldsize);

	return (IBT_SUCCESS);
}

/*
 * iser_ib_lookup_hca_by_gid
 * Given a gid, find the corresponding hca
 */
iser_hca_t *
iser_ib_lookup_hca_by_gid(ib_gid_t gid)
{

	iser_hca_t	*hca;
	int		i;

	mutex_enter(&iser_state->is_hcalist_lock);
	if (iser_state->is_ibt_state == ISER_IBT_STATE_DETACHING) {
		ISER_LOG(CE_NOTE, "iser_ib_lookup_hca_by_gid exiting: "
		    "iSER is detaching from IBTF");
		mutex_exit(&iser_state->is_hcalist_lock);
		return (NULL);
	}
	for (hca = list_head(&iser_state->is_hcalist);
	    hca != NULL;
	    hca = list_next(&iser_state->is_hcalist, hca)) {

		for (i = 0; i < hca->hca_num_ports; i++) {
			if ((hca->hca_port_info[i].p_sgid_tbl[0].gid_prefix ==
			    gid.gid_prefix) &&
			    (hca->hca_port_info[i].p_sgid_tbl[0].gid_guid ==
			    gid.gid_guid)) {

				iser_ib_hca_hold(hca);
				mutex_exit(&iser_state->is_hcalist_lock);

				return (hca);
			}
		}
	}
	mutex_exit(&iser_state->is_hcalist_lock);
	return (NULL);
}

iser_hca_t *
iser_ib_lookup_hca_by_guid(ib_guid_t guid)
{
	iser_hca_t	*hca;

	mutex_enter(&iser_state->is_hcalist_lock);
	hca = iser_ib_lookup_hca_by_guid_locked(guid);
	mutex_exit(&iser_state->is_hcalist_lock);
	return (hca);
}

/*
 * iser_ib_lookup_hca_by_guid_locked
 * Given a HCA guid, find the corresponding HCA,
 * Caller has acquired the is_hcalist_lock
 */
iser_hca_t *
iser_ib_lookup_hca_by_guid_locked(ib_guid_t guid)
{

	iser_hca_t	*hca;

	ASSERT(MUTEX_HELD(&iser_state->is_hcalist_lock));

	if (iser_state->is_ibt_state == ISER_IBT_STATE_DETACHING) {
		ISER_LOG(CE_NOTE, "iser_ib_lookup_hca_by_guid_locked exiting: "
		    "iSER is detaching from IBTF");
		return (NULL);
	}
	for (hca = list_head(&iser_state->is_hcalist);
	    hca != NULL;
	    hca = list_next(&iser_state->is_hcalist, hca)) {

		if (hca->hca_guid == guid) {
			iser_ib_hca_hold(hca);
			return (hca);
		}
	}
	return (NULL);
}

/*
 * iser_ib_conv_sockaddr2ibtaddr
 * This function converts a socket address into the IBT format
 */
void iser_ib_conv_sockaddr2ibtaddr(
    idm_sockaddr_t *saddr, ibt_ip_addr_t *ibt_addr)
{
	if (saddr == NULL) {
		ibt_addr->family = AF_UNSPEC;
		ibt_addr->un.ip4addr = 0;
	} else {
		switch (saddr->sin.sa_family) {
		case AF_INET:

			ibt_addr->family	= saddr->sin4.sin_family;
			ibt_addr->un.ip4addr	= saddr->sin4.sin_addr.s_addr;
			break;

		case AF_INET6:

			ibt_addr->family	= saddr->sin6.sin6_family;
			ibt_addr->un.ip6addr	= saddr->sin6.sin6_addr;
			break;

		default:
			ibt_addr->family = AF_UNSPEC;
		}

	}
}

/*
 * iser_ib_conv_ibtaddr2sockaddr
 * This function converts an IBT ip address handle to a sockaddr
 */
void iser_ib_conv_ibtaddr2sockaddr(struct sockaddr_storage *ss,
    ibt_ip_addr_t *ibt_addr, in_port_t port)
{
	struct sockaddr_in *sin;
	struct sockaddr_in6 *sin6;

	switch (ibt_addr->family) {
	case AF_INET:
	case AF_UNSPEC:

		sin = (struct sockaddr_in *)ibt_addr;
		sin->sin_port = ntohs(port);
		bcopy(sin, ss, sizeof (struct sockaddr_in));
		break;

	case AF_INET6:

		sin6 = (struct sockaddr_in6 *)ibt_addr;
		sin6->sin6_port = ntohs(port);
		bcopy(sin6, ss, sizeof (struct sockaddr_in6));
		break;

	default:
		ISER_LOG(CE_NOTE, "iser_ib_conv_ibtaddr2sockaddr: "
		    "unknown family type: 0x%x", ibt_addr->family);
	}
}

/*
 * iser_ib_setup_cq
 * This function sets up the Completion Queue size and allocates the specified
 * Completion Queue
 */
static int
iser_ib_setup_cq(ibt_hca_hdl_t hca_hdl, uint_t cq_size, ibt_cq_hdl_t *cq_hdl)
{

	ibt_cq_attr_t		cq_attr;
	int			status;

	cq_attr.cq_size		= cq_size;
	cq_attr.cq_sched	= 0;
	cq_attr.cq_flags	= IBT_CQ_NO_FLAGS;

	/* Allocate a Completion Queue */
	status = ibt_alloc_cq(hca_hdl, &cq_attr, cq_hdl, NULL);
	if (status != IBT_SUCCESS) {
		ISER_LOG(CE_NOTE, "iser_ib_setup_cq: ibt_alloc_cq failure (%d)",
		    status);
		return (status);
	}

	return (ISER_STATUS_SUCCESS);
}

/*
 * iser_ib_setup_chanargs
 *
 */
static void
iser_ib_setup_chanargs(uint8_t hca_port, ibt_cq_hdl_t scq_hdl,
    ibt_cq_hdl_t rcq_hdl, uint_t sq_size, uint_t rq_size,
    iser_hca_t *hca, ibt_rc_chan_alloc_args_t *cargs)
{
	bzero(cargs, sizeof (ibt_rc_chan_alloc_args_t));

	/*
	 * Set up the size of the channels send queue, receive queue and the
	 * maximum number of elements in a scatter gather list of work requests
	 * posted to the send and receive queues.
	 */
	cargs->rc_sizes.cs_sq		= sq_size;
	cargs->rc_sizes.cs_rq		= rq_size;
	cargs->rc_sizes.cs_sq_sgl	= hca->hca_max_sgl_size;
	cargs->rc_sizes.cs_rq_sgl	= ISER_IB_RQ_SGLIST_SIZE;

	/*
	 * We will indicate for each WR which requests should receive a send
	 * request completion.
	 */
	cargs->rc_flags			= IBT_WR_SIGNALED;

	/* If the HCA supports Reserved LKey, then turn that on */
	if (hca->hca_attr.hca_flags2 & IBT_HCA2_RES_LKEY) {
		cargs->rc_flags |= IBT_FAST_REG_RES_LKEY;
	}

	/* Enable RDMA read and RDMA write on the channel end points */
	cargs->rc_control		= IBT_CEP_RDMA_RD | IBT_CEP_RDMA_WR;

	/* Set the local hca port on which the channel is allocated */
	cargs->rc_hca_port_num		= hca_port;

	/* Set the Send and Receive Completion Queue handles */
	cargs->rc_scq			= scq_hdl;
	cargs->rc_rcq			= rcq_hdl;

	/* Set the protection domain associated with the channel */
	cargs->rc_pd			= hca->hca_pdhdl;

	/* No SRQ usage */
	cargs->rc_srq			= NULL;
}

/*
 * iser_ib_init_qp
 * Initialize the QP handle
 */
void
iser_ib_init_qp(iser_chan_t *chan, uint_t sq_size, uint_t rq_size)
{
	/* Initialize the handle lock */
	mutex_init(&chan->ic_qp.qp_lock, NULL, MUTEX_DRIVER, NULL);

	/* Record queue sizes */
	chan->ic_qp.sq_size = sq_size;
	chan->ic_qp.rq_size = rq_size;

	/* Initialize the RQ monitoring data */
	chan->ic_qp.rq_depth  = rq_size;
	chan->ic_qp.rq_level  = 0;
	chan->ic_qp.rq_lwm = (chan->ic_recvcq_sz * ISER_IB_RQ_LWM_PCT) / 100;

	/* Initialize the taskq flag */
	chan->ic_qp.rq_taskqpending = B_FALSE;
}

/*
 * iser_ib_fini_qp
 * Teardown the QP handle
 */
void
iser_ib_fini_qp(iser_qp_t *qp)
{
	/* Destroy the handle lock */
	mutex_destroy(&qp->qp_lock);
}

static int
iser_ib_activate_port(idm_svc_t *idm_svc, ib_guid_t guid, ib_gid_t gid)
{
	iser_svc_t	*iser_svc;
	iser_sbind_t	*is_sbind;
	int		status;

	iser_svc = idm_svc->is_iser_svc;

	/*
	 * Save the address of the service bind handle in the
	 * iser_svc_t to undo the service binding at a later time
	 */
	is_sbind = kmem_zalloc(sizeof (iser_sbind_t), KM_SLEEP);
	is_sbind->is_gid	= gid;
	is_sbind->is_guid	= guid;

	status  = ibt_bind_service(iser_svc->is_srvhdl, gid, NULL,
	    idm_svc, &is_sbind->is_sbindhdl);

	if (status != IBT_SUCCESS) {
		ISER_LOG(CE_NOTE, "iser_ib_activate_port: status(0x%x): "
		    "Bind service(%llx) on port(%llx:%llx) failed",
		    status, (longlong_t)iser_svc->is_svcid,
		    (longlong_t)gid.gid_prefix, (longlong_t)gid.gid_guid);

		kmem_free(is_sbind, sizeof (iser_sbind_t));

		return (status);
	}

	list_insert_tail(&iser_svc->is_sbindlist, is_sbind);

	return (IBT_SUCCESS);
}

static void
iser_ib_deactivate_port(ib_guid_t hca_guid, ib_gid_t gid)
{
	iser_svc_t		*iser_svc;
	iser_conn_t		*iser_conn;
	iser_sbind_t		*is_sbind;
	idm_conn_t		*idm_conn;
	idm_svc_t		*idm_svc;
	ibt_path_info_t		ibt_path;
	ib_gid_t		lgid;

	/*
	 * TARGET:
	 * 1) Iterate through the global list of IDM target connections
	 * and issue a TRANSPORT_FAIL for all connections on this port
	 * 2) Iterate through the global list of IDM target services
	 * and tear down the iSER service bound to the deactivated port
	 */
	mutex_enter(&idm.idm_global_mutex);
	for (idm_conn = list_head(&idm.idm_tgt_conn_list);
	    idm_conn != NULL;
	    idm_conn = list_next(&idm.idm_tgt_conn_list, idm_conn)) {

		if (idm_conn->ic_transport_type != IDM_TRANSPORT_TYPE_ISER) {
			/* this is not an iSER connection, skip it */
			continue;
		}

		iser_conn = idm_conn->ic_transport_private;
		ASSERT(iser_conn->ic_type == ISER_CONN_TYPE_TGT);

		/* check for HCA and HCA port match */
		ibt_path = iser_conn->ic_chan->ic_ibt_path;
		lgid = ibt_path.pi_prim_cep_path.cep_adds_vect.av_sgid;
		if ((ibt_path.pi_hca_guid == hca_guid) &&
		    (lgid.gid_prefix == gid.gid_prefix) &&
		    (lgid.gid_guid == gid.gid_guid)) {

			/* Fail the transport for this connection */
			ISER_LOG(CE_NOTE, "iser_ib_deactivate_port: "
			    "Send CE_TRANSPORT_FAIL on target conn[0x%p]",
			    (void *)idm_conn);
			idm_conn_event(idm_conn, CE_TRANSPORT_FAIL,
			    IDM_STATUS_FAIL);
		}
	}
	for (idm_svc = list_head(&idm.idm_tgt_svc_list);
	    idm_svc != NULL;
	    idm_svc = list_next(&idm.idm_tgt_svc_list, idm_svc)) {

		if (idm_svc->is_iser_svc == NULL) {
			continue;
		}
		iser_svc = idm_svc->is_iser_svc;
		is_sbind = iser_ib_get_bind(iser_svc, hca_guid, gid);
		if (is_sbind != NULL) {
			/* This service is still bound, tear it down */
			(void) ibt_unbind_service(iser_svc->is_srvhdl,
			    is_sbind->is_sbindhdl);
			list_remove(&iser_svc->is_sbindlist, is_sbind);
			kmem_free(is_sbind, sizeof (iser_sbind_t));
		}
	}
	/*
	 * INITIATOR: Iterate through the global list of IDM initiator
	 * connections. Issue a TRANSPORT_FAIL for any connections on
	 * this port
	 */
	for (idm_conn = list_head(&idm.idm_ini_conn_list);
	    idm_conn != NULL;
	    idm_conn = list_next(&idm.idm_ini_conn_list, idm_conn)) {

		if (idm_conn->ic_transport_type != IDM_TRANSPORT_TYPE_ISER) {
			/* this is not an iSER connection, skip it */
			continue;
		}

		iser_conn = idm_conn->ic_transport_private;
		ASSERT(iser_conn->ic_type == ISER_CONN_TYPE_INI);

		/* check for HCA and HCA port match */
		ibt_path = iser_conn->ic_chan->ic_ibt_path;
		lgid = ibt_path.pi_prim_cep_path.cep_adds_vect.av_sgid;
		if ((ibt_path.pi_hca_guid == hca_guid) &&
		    (lgid.gid_prefix == gid.gid_prefix) &&
		    (lgid.gid_guid == gid.gid_guid)) {

			/* Fail the transport for this connection */
			ISER_LOG(CE_NOTE, "iser_ib_deactivate_port: "
			    "Send CE_TRANSPORT_FAIL on initiator conn[0x%p]",
			    (void *)idm_conn);
			idm_conn_event(idm_conn, CE_TRANSPORT_FAIL,
			    IDM_STATUS_FAIL);
		}
	}
	mutex_exit(&idm.idm_global_mutex);
}

static iser_sbind_t *
iser_ib_get_bind(iser_svc_t *iser_svc, ib_guid_t hca_guid, ib_gid_t gid)
{
	iser_sbind_t	*is_sbind;

	for (is_sbind = list_head(&iser_svc->is_sbindlist);
	    is_sbind != NULL;
	    is_sbind = list_next(&iser_svc->is_sbindlist, is_sbind)) {

		if ((is_sbind->is_guid == hca_guid) &&
		    (is_sbind->is_gid.gid_prefix == gid.gid_prefix) &&
		    (is_sbind->is_gid.gid_guid == gid.gid_guid)) {
			return (is_sbind);
		}
	}
	return (NULL);
}

void
iser_ib_hca_hold(iser_hca_t *hca)
{
	idm_refcnt_hold(&hca->hca_refcnt);
}

void
iser_ib_hca_rele(iser_hca_t *hca)
{
	idm_refcnt_rele(&hca->hca_refcnt);
}
