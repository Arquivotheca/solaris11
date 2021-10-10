/*
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/stropts.h>
#include <sys/strsun.h>
#include <sys/strsubr.h>
#include <sys/socket.h>
#include <sys/stat.h>		/* for S_IFCHR */
#include <sys/disp.h>		/* for thread pri */
#include <inet/common.h>	/* for pfi_t */
#include <net/if_arp.h>
#include <sys/socketvar.h>
#include <sys/sockio.h>
#include <sys/policy.h>

#include <inet/kstatcom.h>
#include <inet/arp.h>
#include <sys/ib/clients/sdpib/sdp_main.h>
#include <sys/ib/clients/sdpib/sdp_trace_util.h>


/*
 * An implementation of Sockets Direct Protocol based on PSARC 2003/064.
 * This implementation is according to InfiniBand Architecture, Annex 4,
 * version 1.2 SDP is a byte-stream transport protocol that closely attempts
 * to mimic TCP's stream semantics. SDP utilizes InfiniBand's advanced
 * protocol offload, kernel by-pass, and rdma capabilities. Because of this,
 * SDP can have result into lower CPU and memory bandwidth utilization when
 * compared to IPoIB using TCP, while preserving the familiar byte-stream
 * oriented semantics. SDP is based on traditional sockets SOCK_STREAM
 * semantics as commonly implemented over TCP/IP. Some of the features for SDP
 * are  IP addressing, connecting/accepting connect model, Out-of-band (OOB)
 * data, support for common socket options, support for byte-streaming over
 * a message passing protocol, capable of supporting RDMA mode data transfers
 * from send up-per-layer-protocol (ULP) buffers to receive ULP buffers.
 * SDP's ULP interface is a byte-stream interface that is layered on top of
 * IniniBand's Reliable Connection message-oriented transfer model. The
 * mapping of the byte-stream protocol to InfiniBand message-oriented
 * semantics was designed to enable ULP data to be  transferred by one of
 * two methods - through intermediate private buffers (Bcopy) or directly
 * between ULP buffers (Zcopy). SDP packets are transferred using
 * BSDH(Base socket direct header).
 */

/*
 * variables for device instances
 */
static minor_t instance_num = NULL;
static int32_t wraparound = B_FALSE;

/*
 * Global state records all the buffers registered with the hardware. A common
 * pool of buffers is registered at initialization time. Each HCA maintains its
 * own separate pool. The buffer in itself holds its complete description
 * including the descriptor and the associated memory. Pre-allocation is to
 * reduce buffer allocation time in data path. Apart from that global state
 * also keeps track of various lists like list of HCA's probed, list of
 * connections in listening for incoming passive connections, list of
 * connections bind to a specific port. Each list maintains a global lock.
 */
sdp_state_t *sdp_global_state = NULL;

/*
 * sdp support path record caching.
 */
int sdp_pr_cache = 0;

/*
 * Debug levels setting.
 */
int sdpdebug = 2;

/* Required entry points by the driver */
static int sdpib_attach(dev_info_t *dip, ddi_attach_cmd_t cmd);
static int sdpib_detach(dev_info_t *dip, ddi_detach_cmd_t cmd);
static int sdpib_getinfo(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg,
    void **result);
static int sdp_str_open(queue_t *q, dev_t *devp, int flag, int sflag,
    cred_t *credp);
static int sdp_str_close(queue_t *q, int flag);
static void sdp_event_handler(void *clnt_privatep, ibt_hca_hdl_t hca,
    ibt_async_code_t code, ibt_async_event_t *eventp);
static void sdp_wput(queue_t *q, mblk_t *mb);

/* Private entry points by the driver */
static void sdp_delete_thread(void *arg);

/*
 * Module Info passed to IBTL during IBT_ATTACH.
 * This data must be static (i.e. IBTL just keeps a pointer to this
 * data).
 */
static ibt_clnt_modinfo_t sdp_ibt_modinfo = {
	IBTI_V_CURR,
	IBT_CLINET_SDP,
	sdp_event_handler,
	NULL,
	SDPIB_STR_NAME
};

/* Streams read queue module info */
static struct module_info sdp_rinfo = {
	SDP_MODULE_ID,	/* module ID number */
	SDPIB_STR_NAME,	/* module name */
	0,		/* min packet size */
	INFPSZ,		/* maximum packet size */
	SDP_STR_HIWAT,	/* high water mark */
	SDP_STR_LOWAT	/* low water mark */
};

/* Streams write queue module info */
static struct module_info sdp_winfo = {
	SDP_MODULE_ID,	/* module ID number */
	SDPIB_STR_NAME,	/* module name */
	0,		/* min packet size */
	INFPSZ,
	SDP_STR_HIWAT,	/* high water mark */
	SDP_STR_LOWAT	/* low water mark */
};

/* Streams read queue */
static struct qinit sdp_rinit = {
	(pfi_t)0,	/* put -- no streams device underneath us */
	(pfi_t)0,	/* service */
	sdp_str_open,	/* open */
	sdp_str_close,	/* close  */
	(pfi_t)0,	/* unused */
	&sdp_rinfo,	/* module info */
	NULL,		/* statistics */
	NULL,		/* synchronous read */
	NULL,		/* synchronous information */
	STRUIOT_NONE	/* standard uiomove() */
};

/* Streams write queue */
static struct qinit sdp_winit = {
	(pfi_t)sdp_wput, /* put */
	(pfi_t)0,	 /* service */
	(pfi_t)0,	 /* open */
	(pfi_t)0,	 /* close */
	(pfi_t)0,	 /* unused */
	&sdp_winfo,	 /* module info */
	NULL,		 /* statistics */
	(pfi_t)0,	 /* synchronous write */
	NULL,		 /* synchronous information */
	STRUIOT_NONE	 /* standard uiomove() */
};

/* Stream operations */
static struct streamtab sdpib_streamtab = {
	&sdp_rinit,	/* read queue */
	&sdp_winit,	/* write queue */
};

/* Character/block operations */
static struct cb_ops sdpib_cb_ops = {
	nodev,		/* open */
	nodev,		/* close */
	nodev,		/* strategy */
	nodev,		/* print */
	nodev,		/* dump */
	nodev,		/* read */
	nodev,		/* write */
	nodev,		/* ioctl */
	nodev,		/* devmap */
	nodev,		/* mmap */
	nodev,		/* segmap */
	nochpoll,	/* chpoll */
	ddi_prop_op,	/* prop_op (sun DDI-specific) */
	&sdpib_streamtab,	/* streams */
	D_MP,
	CB_REV
};

/* Driver operations */
static struct dev_ops sdpib_dev_ops = {
	DEVO_REV,	/* struct rev */
	0,		/* refcnt */
	sdpib_getinfo,	/* getinfo */
	nulldev,	/* identify */
	nulldev,	/* probe */
	sdpib_attach,	/* attach */
	sdpib_detach,	/* detach */
	nodev,		/* reset */
	&sdpib_cb_ops,	/* cb_ops */
	NULL,		/* bus_ops */
	nodev,		/* power */
	ddi_quiesce_not_needed,	/* devo_quiesce */
};
static struct fmodsw sdp_mod_fsw = {
	SDPIB_STR_NAME,
	&sdpib_streamtab,
	SDP_STR_MODMTFLAGS
};
static struct modlstrmod sdp_modlstrmod = {
	&mod_strmodops,
	SDP_STR_MODDESC,
	&sdp_mod_fsw
};

/* Module driver info */
static struct modldrv sdp_modldrv = {
	&mod_driverops,
	SDP_STR_DRVDESC,
	&sdpib_dev_ops
};

/* Module linkage */
static struct modlinkage sdp_modlinkage = {
	SDP_MODREV_1,
	&sdp_modlstrmod,
	&sdp_modldrv,
	NULL
};

/*
 * Should be removed eventually
 */
static int sdp_use_sm = 1;

static void
sdp_sm_notice_handler(void *arg, ib_gid_t gid, ibt_subnet_event_code_t code,
    ibt_subnet_event_t *event);
/*
 * sdpib_attach -- Attach device to IO framework
 */
static int
sdpib_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	ibt_status_t status;
	sdp_state_t *state;
	kthread_t *sdp_thr;

	switch (cmd) {
		case DDI_ATTACH:
			break;
		case DDI_RESUME:
			return (DDI_FAILURE);
		default:
			return (DDI_FAILURE);
	}

	/* sdp main initialization */
	status = sdp_init(&sdp_global_state);
	if (status != 0) {
		/* CONSTCOND */
		sdp_print_warn(NULL, "sdpib_attach: sdp_init failed "
		    "status :<%d>", status);
		goto out;
	}
	ASSERT(sdp_global_state != NULL);
	state = (sdp_state_t *)sdp_global_state;

	mutex_enter(&state->sdp_state_lock);
	/* Register with IBTF */
	status = ibt_attach(&sdp_ibt_modinfo, dip, (void *)state,
	    &state->sdp_ibt_hdl);
	if (status != IBT_SUCCESS) {
		/* CONSTCOND */
		sdp_print_warn(NULL, "sdpib_attach: ibt_attach failed <%d>",
		    status);
		goto out_attach;
	}

	if (sdp_use_sm) {
		/* Register to receive SM events */
		ibt_register_subnet_notices(state->sdp_ibt_hdl,
		    sdp_sm_notice_handler, NULL);
	}

	/* Initialize HCAs, etc. */
	status = sdp_ibt_init(state);
	if (status != DDI_SUCCESS) {
		/* CONSTCOND */
		sdp_print_warn(NULL, "sdpib_attach: sdp_ibt_init failed <%d>",
		    status);
		goto out_ibt_init;
	}

	/* Init path record lookup. */
	if (sdp_pr_lookup_init()) {
		goto out_pr_lookup;
	}

	/* Create a minor node for the device */
	status = ddi_create_minor_node(dip, SDPIB_STR_NAME, S_IFCHR, 0,
	    DDI_PSEUDO, 0);
	if (status != DDI_SUCCESS) {
		/* CONSTCOND */
		sdp_print_note(NULL, "sdpib_attach: ddi_create_minor_node "
		    "failed: <%d>", status);
		goto out_minor_node;
	}

	state->dip = dip;
	state->major = ddi_driver_major(dip);

	/*
	 * Start the connection deletion thread.
	 */
	if ((sdp_thr = thread_create(NULL, 0, sdp_delete_thread, state, 0,
	    &p0, TS_RUN, minclsyspri)) == NULL) {
		dprint(SDP_WARN, ("sdpib_attach: thread_create failed"));
		goto out_thread_create;
	}
	state->delete_thr_id = sdp_thr->t_did;

	state->sdp_state = SDP_ATTACHED;
	mutex_exit(&state->sdp_state_lock);
	netstack_register(NS_SDP, sdp_stack_init, NULL, sdp_stack_destroy);
	return (DDI_SUCCESS);

out_thread_create:
	state->exit_flag = B_TRUE;
	cv_signal(&state->delete_cv);
	thread_join(state->delete_thr_id);
out_link:
	ddi_remove_minor_node(dip, "");
out_minor_node:
	(void) sdp_pr_lookup_cleanup();
out_pr_lookup:
	(void) sdp_ibt_fini(state);
out_ibt_init:
	if (sdp_use_sm) {
		/* Unregister to receive SM events */
		ibt_register_subnet_notices(state->sdp_ibt_hdl, NULL, NULL);
	}
	(void) ibt_detach(state->sdp_ibt_hdl);
	state->sdp_state = SDP_DETACHED;
out_attach:
	mutex_exit(&state->sdp_state_lock);
	sdp_exit();
out:
	return (DDI_FAILURE);
}


/*
 * sdpib_detach: Detach device from the IO framework, cleanup everything.
 */
static int
sdpib_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	ibt_status_t status;
	sdp_state_t *state = sdp_global_state;
	ibt_clnt_hdl_t saved_sdp_ibt_hdl;
	kthread_t *sdp_thr = NULL;
	int reinit_failed = 0;


	switch (cmd) {
		case DDI_DETACH:
			break;
		case DDI_SUSPEND:
			return (DDI_FAILURE);
		default:
			/* CONSTCOND */
			sdp_print_note(NULL, "sdpib_detach: unrecognized "
			    "cmd<%d>", cmd);
			return (DDI_FAILURE);
	}
	ASSERT(state);
	/*
	 * lock order based on sdp_conn_table_insert()
	 */
	mutex_enter(&state->sdp_state_lock);
	mutex_enter(&state->sock_lock);

	/*
	 * We can succeed with modunload only if there aren't any active
	 * connections.
	 */
	if (state->conn_array_num_entries != 0) {
		mutex_exit(&state->sock_lock);
		mutex_exit(&state->sdp_state_lock);
		return (DDI_FAILURE);
	}
	state->sdp_state = SDP_DETACHING;
	mutex_exit(&state->sock_lock);
	mutex_exit(&state->sdp_state_lock);

	/*
	 * The conn delete_list thread might access IB to free up resources. So
	 * complete processing any pending connection delete operation and
	 * Wait for the delete_list thread to exit.
	 */
	mutex_enter(&state->delete_lock);
	state->exit_flag = B_TRUE;
	cv_signal(&state->delete_cv);
	mutex_exit(&state->delete_lock);
	thread_join(state->delete_thr_id);

	(void) sdp_pr_lookup_cleanup();

	if (sdp_use_sm) {
		/* Unregister to receive SM events */
		ibt_register_subnet_notices(state->sdp_ibt_hdl, NULL, NULL);
	}

	/* Deinitialize all the global state recordings */
	status = sdp_ibt_fini(state);
	if (status != DDI_SUCCESS) {
		/* CONSTCOND */
		sdp_print_warn(NULL, "sdp_tran_fini: sdp_fini fail <%d>",
		    status);
		goto ibt_detach_err;
	}

	saved_sdp_ibt_hdl = state->sdp_ibt_hdl;
	status = ibt_detach(saved_sdp_ibt_hdl);
	if (status != IBT_SUCCESS) {
		sdp_print_warn(NULL, "sdp_tran_fini: ibt_detach_fail:<%d>",
		    status);
		goto ibt_detach_err;
	}

	netstack_unregister(NS_SDP);

	sdp_exit();
	sdp_global_state = NULL;
	ddi_remove_minor_node(dip, "");
	return (DDI_SUCCESS);

ibt_detach_err:
	status = sdp_ibt_init(state);
	if (status != DDI_SUCCESS) {
		reinit_failed = 1;
		sdp_print_warn(NULL, "sdp_ibt_init: sdp reinit in sdpib_detach "
		"failed <%d>: SDP in inconsistent state", status);
	}
	if (sdp_pr_lookup_init()) {
		reinit_failed = 1;
		sdp_print_warn(NULL, "sdp_pr_lookup_init: sdp reinit in "
		    "sdpib_detach failed <%d>: SDP in inconsistent state",
		    status);
	}

	if (sdp_use_sm) {
		/* Register to receive SM events */
		ibt_register_subnet_notices(state->sdp_ibt_hdl,
		    sdp_sm_notice_handler, NULL);
	}
out:
	/*
	 * Restart the connection deletion thread.
	 */
	if ((sdp_thr = thread_create(NULL, 0, sdp_delete_thread, state, 0,
	    &p0, TS_RUN, minclsyspri)) == NULL) {
		reinit_failed = 1;
		dprint(SDP_WARN, ("sdpib_detach: thread_create of "
		    "delete_thread failed in detach error recovery path"));
	}
	state->delete_thr_id = sdp_thr->t_did;

	/*
	 * We are in SDP_DETACHED state. No new connections would be allowed
	 * when in SDP_DETACHED state and we transistion to SDP_DETACHED only
	 * if no connections currently exist. So we are in quiesced state.
	 * So just set the state to SDP_ATTACHED in the error recovery path.
	 */
	if (!reinit_failed)
		state->sdp_state = SDP_ATTACHED;

	sdp_print_init(NULL, "sdpib_detach: detach fail sdp_global_state:%p",
	    (void *)sdp_global_state);

	return (DDI_FAILURE);
}

/* ARGSUSED */
static int
sdpib_getinfo(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg, void **result)
{
	sdp_state_t *state;
	int error = DDI_FAILURE;

	switch (cmd) {
	case DDI_INFO_DEVT2DEVINFO:
		state = sdp_global_state;
		if (state != NULL && state->dip != NULL) {
			*result = (void *)(state->dip);
			error = DDI_SUCCESS;
		}
		break;

	case DDI_INFO_DEVT2INSTANCE:
		*result = NULL;
		error = DDI_SUCCESS;
		break;

	default:
		break;
	}

	return (error);
}

/*
 * Return the version number of the SDP kernel interface.
 */
int
sdp_itf_ver(int cl_ver)
{
	if (cl_ver != SDP_ITF_VER)
		return (-1);
	return (SDP_ITF_VER);
}

static sdp_dev_port_t *
get_port_by_index(sdp_dev_hca_t *hcap, uint8_t ev_port)
{
	sdp_dev_port_t *ret_port = NULL;

	for (ret_port = hcap->port_list; ret_port != NULL;
	    ret_port = ret_port->next) {
		if (ev_port == ret_port->index)
			break;
	}
	return (ret_port);
}

/*
 * Return B_TURE if the conn macthes the parameters passed in.
 */
boolean_t
sdp_match_conn(sdp_conn_t *connp, ibt_hca_hdl_t hca_hdl, int port,
    ib_gid_t *sgid, ib_gid_t *dgid)
{
	if (connp == NULL ||
	    ((hca_hdl != NULL) && (connp->hca_hdl != hca_hdl)) ||
	    ((port != 0) && (connp->hw_port != port))) {
		return (B_FALSE);
	}

	if (connp->state == SDP_CONN_ST_LISTEN ||
	    connp->state == SDP_CONN_ST_CLOSED ||
	    connp->state == SDP_CONN_ST_ERROR ||
	    connp->state == SDP_CONN_ST_INVALID) {
		return (B_FALSE);
	}

	/*
	 * Skip unconnected conns
	 */
	if (((connp->s_gid.gid_prefix == 0) &&
	    (connp->s_gid.gid_guid == 0)) ||
	    ((connp->d_gid.gid_prefix == 0) &&
	    (connp->d_gid.gid_guid == 0))) {
		return (B_FALSE);
	}

	/*
	 * Skip loopback
	 * Once the connection is setup -- loopback keeps working even if
	 * the ports are taken down -- this is true for 127.0.0.1 as well as
	 * any other locally hosted address.
	 */
	if ((connp->s_gid.gid_prefix == connp->d_gid.gid_prefix) &&
	    (connp->s_gid.gid_guid == connp->d_gid.gid_guid)) {
		return (B_FALSE);
	}

	/*
	 * There is no need to check alternate source path
	 * as SM reports events on all ports. So an event with the
	 * alternate port will also be generated and call here.
	 */
	if ((sgid != NULL) &&
	    ((connp->s_gid.gid_prefix != sgid->gid_prefix) ||
	    (connp->s_gid.gid_guid != sgid->gid_guid))) {
		return (B_FALSE);
	}

	/*
	 * check alternate destination paths only when APM is active
	 */
	if ((dgid != NULL) &&
	    (((connp->d_gid.gid_prefix != dgid->gid_prefix) ||
	    (connp->d_gid.gid_guid != dgid->gid_guid)) &&
	    (!sdp_global_state->sdp_apm_enabled ||
	    ((connp->d_alt_gid.gid_prefix != dgid->gid_prefix) ||
	    (connp->d_alt_gid.gid_guid != dgid->gid_guid))))) {
		return (B_FALSE);
	}

	dprint(SDP_DBG, ("Matched connp %p: sgid:%llx:%llx dgid:%llx:%llx \n",
	    (void *)connp,
	    (longlong_t)connp->s_gid.gid_prefix,
	    (longlong_t)connp->s_gid.gid_guid,
	    (longlong_t)connp->d_gid.gid_prefix,
	    (longlong_t)connp->d_gid.gid_guid));

	return (B_TRUE);
}

/*
 * Register services with the port that just came up
 */
static void
sdp_bind_port_services(sdp_state_t *state, ibt_hca_hdl_t hca_hdl,
    ibt_async_event_t *event)
{
	sdp_dev_hca_t *hcap = NULL;
	ibt_status_t status;
	sdp_dev_port_t *portp;

	ASSERT(state != NULL);
	mutex_enter(&state->hcas_mutex);
	hcap = get_hcap_by_hdl(state, hca_hdl);
	if (hcap == NULL) {
		mutex_exit(&state->hcas_mutex);
		return;
	}
	portp = get_port_by_index(hcap, event->ev_port);
	if (portp == NULL) {
		mutex_exit(&state->hcas_mutex);
		return;
	}
	/*
	 * get the current port state and enter it
	 * in the struct.
	 * port state could have changed since we
	 * were called by the async event handler.
	 */
	status = ibt_get_port_state(hcap->hca_hdl, event->ev_port,
	    &portp->sgid, &portp->base_lid);
	if (status != IBT_SUCCESS) {
		mutex_exit(&state->hcas_mutex);
		return;
	}

	if (portp->bind_hdl == NULL) {
		if (ibt_bind_service(state->sdp_ibt_srv_hdl,
		    portp->sgid, NULL, NULL, &portp->bind_hdl) != IBT_SUCCESS) {
			sdp_print_dbg(NULL, "sdp_modify_port_state:"
			    " service registeration failed: <%d> \n", status);
		}
	}
	mutex_exit(&state->hcas_mutex);
}

static int
sdp_apm_get_set_altpath(ibt_channel_hdl_t channel_hdl, ib_gid_t *sgid,
    ib_gid_t *dgid)
{
	ibt_alt_path_info_t path_info;
	ibt_alt_path_attr_t path_attr;
	ibt_ap_returns_t ap_rets;
	ibt_status_t status;

	bzero(&path_info, sizeof (ibt_alt_path_info_t));
	bzero(&path_attr, sizeof (ibt_alt_path_attr_t));

	if (dgid != NULL) {
		path_attr.apa_sgid = *sgid;
		path_attr.apa_dgid = *dgid;
	}
	status = ibt_get_alt_path(channel_hdl, IBT_PATH_AVAIL,
	    &path_attr, &path_info);
	if (status != IBT_SUCCESS) {
		dprint(SDP_DBG, ("ibt_get_alt_path status %x \n", status));
		return (1);
	}
	bzero(&ap_rets, sizeof (ibt_ap_returns_t));
	status = ibt_set_alt_path(channel_hdl, IBT_BLOCKING,
	    &path_info, NULL, 0, &ap_rets);
	if ((status != IBT_SUCCESS) ||
	    (ap_rets.ap_status != IBT_CM_AP_LOADED)) {
		dprint(SDP_DBG, ("ibt_set_alt_path status %x ap_status %x\n",
		    status, ap_rets.ap_status));
		return (1);
	}
	return (0);
}

static int sdp_apm_arm_count = 2;
static int sdp_apm_arm_delay = 1;

static void
sdp_manage_apm(sdp_conn_t *connp, sdp_apm_event event)
{
	int i;
	ibt_rc_chan_query_attr_t chan_attrs;
	ibt_status_t status;

	if (event == SDP_APM_PATH_MIGRATED) {
		connp->sdp_apm_path_migrated = B_TRUE;
		if (connp->sdp_apm_port_up)
			goto port_up;
		return;
	} else if (event == SDP_APM_PORT_DOWN) {
		if (!sdp_global_state->sdp_apm_enabled) {
			SDP_CONN_LOCK(connp);
			sdp_conn_abort(connp);
			SDP_CONN_UNLOCK(connp);
		} else {
			connp->sdp_apm_port_up = B_FALSE;
			connp->sdp_apm_path_migrated = B_FALSE;
		}
		return;
	}
	ASSERT(event == SDP_APM_PORT_UP);

	if (!sdp_global_state->sdp_apm_enabled ||
	    !connp->sdp_apm_path_migrated) {
		/*
		 * This can happen if the connection was
		 * established when the secondary port was down
		 * and is later brought up OR if the port is brough up
		 * while it is already up.
		 */
		return;
	}
port_up:
	/*
	 * get and set altpath with original sgid and dgid used
	 * in connect
	 */
	if (sdp_apm_get_set_altpath(connp->channel_hdl,
	    &connp->s_gid, &connp->d_gid)) {
		sdp_print_err(connp, "%s", "sdp_manage_apm error \n");
		return;
	}
	/*
	 * wait for migration state to be ARMed
	 */
	for (i = 0; i < sdp_apm_arm_count; i++) {
		bzero(&chan_attrs, sizeof (ibt_rc_chan_query_attr_t));
		status = ibt_query_rc_channel(connp->channel_hdl,
		    &chan_attrs);
		if (status != IBT_SUCCESS) {
			sdp_print_dbg(connp, "ibt_query_rc_channel error %d\n",
			    status);
			return;
		}
		if (chan_attrs.rc_mig_state == IBT_STATE_ARMED) {
			break;
		}
		/* add a bit of delay */
		delay(SEC_TO_TICK(sdp_apm_arm_delay));
	}
	if (i >= sdp_apm_arm_count) {
		sdp_note("IBT state not ARMed for conn %p\n", (void *)connp);
		return;
	}

#ifdef NOTYET
		status = ibt_migrate_path(connp->channel_hdl);
		if (status != IBT_SUCCESS) {
			sdp_print_err(connp,
			    "ibt_migrate_path error %d\n", status);
			continue;
		}
		connp->sdp_force_migration = B_TRUE;
#endif
	/*
	 * get and altpath with NULL sgid and dgid to indicate
	 * unspecified dgid
	 */
	(void) sdp_apm_get_set_altpath(connp->channel_hdl, NULL, NULL);
}

static void
sdp_apm_port_event(sdp_state_t *state, ibt_hca_hdl_t hca_hdl,
    ib_gid_t *sgid, ib_gid_t *dgid, int port, int event)
{
	int counter;
	sdp_conn_t *connp;

	if (!sdp_global_state->sdp_apm_enabled && event == SDP_APM_PORT_UP) {
		/*
		 * If APM is not enabled there is nothing to do for port up
		 */
		return;
	}

	mutex_enter(&state->sock_lock);
	for (counter = 0; counter < state->conn_array_size; counter++) {
		connp = state->conn_array[counter];
		if (!sdp_match_conn(connp, hca_hdl,
		    sdp_global_state->sdp_apm_enabled ? 0:port, sgid, dgid) ||
		    !connp->sdp_active_open)
			continue;
		sdp_manage_apm(connp, event);
	}
	mutex_exit(&state->sock_lock);
}
/*
 * We simultaneously use both SM and IBT port notification because
 * When a local port comes up it does not generate a SM event. SM port event
 * is reported with the sgid of the port reporting the event and dgid is
 * the port that is effected.
 * This will not match the case where the effected port is the source.
 * For that we use the IBT notifications.
 */


/* ARGSUSED */
static void
sdp_sm_notice_handler(void *arg, ib_gid_t sgid, ibt_subnet_event_code_t code,
    ibt_subnet_event_t *event)
{
	sdp_state_t	*state = sdp_global_state;
	ib_gid_t	*dgid;

	dgid = &event->sm_notice_gid;
	switch (code) {
	case IBT_SM_EVENT_GID_AVAIL:
		sdp_print_note(NULL, "IBT_SM_EVENT_GID_AVAIL %llx\n",
		    (longlong_t)event->sm_notice_gid.gid_guid);
		sdp_apm_port_event(state, NULL, &sgid, dgid, 0,
		    SDP_APM_PORT_UP);
		return;
	case IBT_SM_EVENT_GID_UNAVAIL:
		sdp_print_note(NULL, "IBT_SM_EVENT_GID_UNAVAIL %llx\n",
		    (longlong_t)event->sm_notice_gid.gid_guid);
		sdp_apm_port_event(state, NULL, &sgid, dgid, 0,
		    SDP_APM_PORT_DOWN);
		return;
	default:
		sdp_print_note(NULL, "unhandled IBT_SM_EVENT_[%d]\n", code);
	}
}

/*
 * sdp_event_handler -- process port up/down events by flushing the prcache
 * entries.
 */
static void
sdp_event_handler(void *clnt_privatep, ibt_hca_hdl_t hca,
	ibt_async_code_t code, ibt_async_event_t *eventp)
{
	sdp_state_t	*state = (sdp_state_t *)clnt_privatep;
	sdp_conn_t	*connp = NULL;

	switch (code) {
		case IBT_ERROR_CATASTROPHIC_CHAN:
			break;
		case IBT_ERROR_CQ:
			break;
		case IBT_ERROR_PORT_DOWN:
			/*
			 * Only ev_hca_guid (HCA's guid) And
			 * ev_port (HCA's port number) are populated
			 */
			sdp_print_note(NULL, "SDP hca %p port %d Down\n",
			    (void *)hca, eventp->ev_port);
			sdp_prcache_cleanup();
			sdp_apm_port_event(state, hca, NULL, NULL,
			    eventp->ev_port, SDP_APM_PORT_DOWN);
			break;
		case IBT_EVENT_PORT_UP:
			/*
			 * Only ev_hca_guid (HCA's guid) And
			 * ev_port (HCA's port number) are populated
			 */
			sdp_print_note(NULL, "SDP hca %p port %d Up\n",
			    (void *)hca, eventp->ev_port);
			sdp_prcache_cleanup();
			(void) sdp_bind_port_services(state, hca, eventp);
			sdp_apm_port_event(state, hca, NULL, NULL,
			    eventp->ev_port, SDP_APM_PORT_UP);
			break;
		case IBT_HCA_ATTACH_EVENT:
			/*
			 * NOTE: In some error recovery paths, it is possible
			 * to receive IBT_HCA_ATTACH_EVENTs on already known
			 * HCAs.
			 */
			sdp_prcache_cleanup();
			dprint(SDP_CONT, ("sdp_event_handler(): "
			    "IBT_HCA_ATTACH_EVENT"));
			if (sdp_ibt_hca_attach(state, eventp)
			    != DDI_SUCCESS) {
				dprint(SDP_CONT, ("IBT_HCA_ATTACH_EVENT "
				    "sdp_ibt_hca_attach failed"));
			}
			break;
		case IBT_HCA_DETACH_EVENT:
			sdp_prcache_cleanup();
			dprint(SDP_CONT, ("sdp_event_handler(): "
			    "IBT_HCA_DETTACH_EVENT"));
			if (sdp_ibt_hca_detach(state, hca) != DDI_SUCCESS) {
				dprint(SDP_CONT, ("IBT_HCA_DETACH_EVENT "
				    "sdp_ibt_hca_detach failed"));
			}
			break;
		case IBT_EVENT_PATH_MIGRATED:
			/*
			 * This event is associated with APM.
			 * Each path (QP) that gets migrated generates
			 * an event.
			 */
			dprint(SDP_CONT, ("SDP Path Migrated"));
			connp = ibt_get_chan_private(eventp->ev_chan_hdl);
			ASSERT(connp != NULL);
			mutex_enter(&state->sock_lock);
			sdp_manage_apm(connp, SDP_APM_PATH_MIGRATED);
			mutex_exit(&state->sock_lock);
			break;
		default:
			break;
	}
}

/*
 * Per-instance open called on a new stream.
 */
/* ARGSUSED */
static int
sdp_str_open(queue_t *q, dev_t *devp, int flag, int sflag, cred_t *credp)
{
	netstack_t	*ns;
	sdp_stack_t	*sdps;

	if (secpolicy_ip_config(credp, B_FALSE) != 0)
		return (EPERM);

	if (q->q_ptr != NULL)
		return (0);  /* Re-open of an already open instance. */

	ns = netstack_find_by_cred(credp);
	ASSERT(ns != NULL);
	sdps = ns->netstack_sdp;
	ASSERT(sdps != NULL);

	q->q_ptr = sdps;
	WR(q)->q_ptr = q->q_ptr;

	qprocson(q);
	return (0);
}

/* ARGSUSED */
static int
sdp_str_close(queue_t *q, int flag)
{
	sdp_stack_t	*sdps = (sdp_stack_t *)q->q_ptr;

	qprocsoff(q);
	netstack_rele(sdps->sdps_netstack);
	return (0);
}

/*
 * Received a put from sockfs. We only support ndd get/set
 */
static void
sdp_wput(queue_t *q, mblk_t *mp)
{
	switch (mp->b_datap->db_type) {
	case M_FLUSH:
		*mp->b_rptr &= ~FLUSHW;
		if (*mp->b_rptr & FLUSHR)
			qreply(q, mp);
		else
			freemsg(mp);
		break;
	case M_IOCTL:
		sdp_wput_ioctl(q, mp);
		break;
	default:
		freemsg(mp);
	}
}


/*
 * Generate a new instance number for a connection.
 */
minor_t
sdp_get_new_inst()
{
	minor_t old_devid = instance_num;

	/*
	 * Get the atomically incremented instance for new cloned device
	 */
	instance_num = atomic_add_32_nv(&instance_num, 1);

	/*
	 * check for wraparound.
	 */
	if ((old_devid > instance_num) || (wraparound)) {
		wraparound = B_TRUE;
	}
	return (instance_num);
}

/*
 * Thread which waits on delete_cv.
 * Gets a signal from interrupt context and deletes the connection.
 * This is done as we cannot free QP's and CQ's in interrupt context.
 */
/* ARGSUSED */
static void
sdp_delete_thread(void *arg)
{
	sdp_state_t	*state = sdp_global_state;

	mutex_enter(&state->delete_lock);
	while (!state->exit_flag) {
		cv_wait(&state->delete_cv, &state->delete_lock);
		while (state->delete_list != NULL) {
			sdp_conn_t *connp;
			connp = state->delete_list;
			state->delete_list = NULL;
			mutex_exit(&state->delete_lock);
			sdp_conn_destruct(connp);
			mutex_enter(&state->delete_lock);
		}
	}
	ASSERT(state->delete_list == NULL);
	mutex_exit(&state->delete_lock);
}

/*
 * _init()
 */
int32_t
_init()
{
	int32_t status;

	status = mod_install(&sdp_modlinkage);
	if (status != 0)
		return (status);

	return (DDI_SUCCESS);
}   /* _init() */

/*
 * _info()
 */
int
_info(struct modinfo *sdp_modinfop)
{
	int status;

	status = mod_info(&sdp_modlinkage, sdp_modinfop);
	return (status);
}

/*
 * _fini()
 */
int
_fini()
{
	int status;

	status = mod_remove(&sdp_modlinkage);
	return (status);
}

int
sdp_ioc_hca_offline(ib_guid_t guid, boolean_t force, boolean_t query)
{
	sdp_state_t *state = (sdp_state_t *)sdp_global_state;
	int i, status = 0;
	sdp_dev_hca_t *hcap;
	sdp_conn_t *conn;

	/* lookup the HCA by the given GUID */
	mutex_enter(&state->hcas_mutex);
	for (hcap = state->hca_list; hcap != NULL; hcap = hcap->next) {
		if (hcap->guid == guid) {
			/*
			 * Non-forceful case, just check whether there is
			 * any connection associated with the HCA, if so,
			 * return EBUSY
			 */
			if (!force) {
				if (hcap->hca_num_conns == 0) {
					if (!query)
						hcap->sdp_hca_offlined = B_TRUE;
				} else {
					status = EBUSY;
				}
				mutex_exit(&state->hcas_mutex);
				return (status);
			}

			/*
			 * Forceful but query case, simply return success.
			 * all connections will be aborted later in a non-query
			 * operation.
			 */
			if (query) {
				mutex_exit(&state->hcas_mutex);
				return (0);
			}

			/*
			 * Forceful and non-query case; temporarily bump the
			 * references so that this hca won't be freed. Also
			 * set the offline flag to prevent the HCA from being
			 * used by any new connections
			 */
			hcap->sdp_hca_refcnt++;
			hcap->sdp_hca_offlined = B_TRUE;
			break;
		}
	}
	mutex_exit(&state->hcas_mutex);

	if (hcap == NULL)
		return (ENOENT);

	/*
	 * Forceful and query case. Find and abort all connections over the
	 * given HCA.
	 */
	ASSERT(force && !query);
	mutex_enter(&state->sock_lock);
	for (i = 0; i < state->conn_array_size; i++) {
		if ((conn = state->conn_array[i]) == NULL)
			continue;

		SDP_CONN_LOCK(conn);
		if (conn->hca_hdl != hcap->hca_hdl) {
			SDP_CONN_UNLOCK(conn);
			continue;
		}

		sdp_conn_abort(conn);
		SDP_CONN_UNLOCK(conn);
	}
	mutex_exit(&state->sock_lock);

	mutex_enter(&state->hcas_mutex);
	ASSERT(hcap->sdp_hca_offlined);
	while (hcap->hca_num_conns != 0) {
		/*
		 * If the CR process is killed before all connections are
		 * aborted, break the loop and return EBUSY.
		 */
		if (cv_wait_sig(&state->hcas_cv, &state->hcas_mutex) <= 0) {
			cmn_err(CE_WARN, "failed to abort all connections "
			    "num_conns=%d", hcap->hca_num_conns);
			status = EBUSY;
			hcap->sdp_hca_offlined = B_FALSE;
			break;
		}
	}

	hcap->sdp_hca_refcnt--;
	mutex_exit(&state->hcas_mutex);
	return (status);
}

int
sdp_ioc_hca_online(ib_guid_t guid)
{
	sdp_state_t *state = (sdp_state_t *)sdp_global_state;
	sdp_dev_hca_t *hcap;

	/* lookup the HCA by the given GUID */
	mutex_enter(&state->hcas_mutex);
	for (hcap = state->hca_list; hcap != NULL; hcap = hcap->next) {
		if (hcap->guid == guid) {
			/* unset the offline flag */
			hcap->sdp_hca_offlined = B_FALSE;
			mutex_exit(&state->hcas_mutex);
			return (0);
		}
	}
	mutex_exit(&state->hcas_mutex);
	return (ENOENT);
}
