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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/kmem.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/ksynch.h>
#include <sys/dlpi.h>			/* HCKSUM_... */
#include <sys/pattr.h>			/* HCK_... */
#include <sys/ib/mgt/sm_attr.h>		/* SM_INIT_TYPE_REPLY_... */

#include <sys/ib/clients/eoib/eib_impl.h>

/*
 * Declarations private to this file
 */
static void eib_ibt_reset_partitions(eib_t *);
static void eib_ibt_wakeup_waiter(eib_t *, ibt_async_code_t,
    ibt_channel_hdl_t);
static int eib_ibt_chan_pkey(eib_t *, eib_chan_t *, ib_pkey_t, boolean_t,
    boolean_t *);
static boolean_t eib_ibt_has_chan_pkey_changed(eib_t *, eib_chan_t *);
static boolean_t eib_ibt_has_any_pkey_changed(eib_t *);
static void eib_ibt_record_srate(eib_t *);
static eib_srq_t *eib_ibt_setup_chan_srq(eib_t *, uint_t, uint_t,
    uint_t, boolean_t, uint_t);
static void eib_rb_ibt_setup_chan_srq(eib_t *, eib_srq_t *);

/*
 * Definitions private to this file
 */

/*
 * SM's init type reply flags
 */
#define	EIB_PORT_ATTR_LOADED(itr)				\
	(((itr) & SM_INIT_TYPE_REPLY_NO_LOAD_REPLY) == 0)
#define	EIB_PORT_ATTR_NOT_PRESERVED(itr)			\
	(((itr) & SM_INIT_TYPE_PRESERVE_CONTENT_REPLY) == 0)
#define	EIB_PORT_PRES_NOT_PRESERVED(itr)			\
	(((itr) & SM_INIT_TYPE_PRESERVE_PRESENCE_REPLY) == 0)

/*
 * eib_ibt_hca_init() initialization progress flags
 */
#define	EIB_HCAINIT_HCA_OPENED		0x01
#define	EIB_HCAINIT_ATTRS_ALLOCD	0x02
#define	EIB_HCAINIT_HCA_PORTS_QUERIED	0x04
#define	EIB_HCAINIT_PD_ALLOCD		0x08
#define	EIB_HCAINIT_CAPAB_RECORDED	0x10

int
eib_ibt_hca_init(eib_t *ss)
{
	ibt_status_t ret;
	ibt_hca_portinfo_t *pi;
	uint_t num_pi;
	uint_t sz_pi;
	uint_t progress = 0;

	if (ss->ei_hca_hdl)
		return (EIB_E_SUCCESS);

	/*
	 * Open the HCA
	 */
	ret = ibt_open_hca(ss->ei_ibt_hdl, ss->ei_props->ep_hca_guid,
	    &ss->ei_hca_hdl);
	if (ret != IBT_SUCCESS) {
		EIB_DPRINTF_ERR(ss->ei_instance,
		    "ibt_open_hca(hca_guid=0x%llx) "
		    "failed, ret=%d", ss->ei_props->ep_hca_guid, ret);
		goto ibt_hca_init_fail;
	}
	progress |= EIB_HCAINIT_HCA_OPENED;

	/*
	 * Query and store HCA attributes
	 */
	ss->ei_hca_attrs = kmem_zalloc(sizeof (ibt_hca_attr_t), KM_SLEEP);
	progress |= EIB_HCAINIT_ATTRS_ALLOCD;

	ret = ibt_query_hca(ss->ei_hca_hdl, ss->ei_hca_attrs);
	if (ret != IBT_SUCCESS) {
		EIB_DPRINTF_ERR(ss->ei_instance,
		    "ibt_query_hca(hca_hdl=0x%llx, "
		    "hca_guid=0x%llx) failed, ret=%d",
		    ss->ei_hca_hdl, ss->ei_props->ep_hca_guid, ret);
		goto ibt_hca_init_fail;
	}

	/*
	 * At this point, we don't even care about the linkstate, we only want
	 * to record our invariant base port guid and mtu
	 */
	ret = ibt_query_hca_ports(ss->ei_hca_hdl, ss->ei_props->ep_port_num,
	    &pi, &num_pi, &sz_pi);
	if (ret != IBT_SUCCESS) {
		EIB_DPRINTF_ERR(ss->ei_instance,
		    "ibt_query_hca_ports(hca_hdl=0x%llx, "
		    "port=0x%x) failed, ret=%d", ss->ei_hca_hdl,
		    ss->ei_props->ep_port_num, ret);
		goto ibt_hca_init_fail;
	}
	if (num_pi != 1) {
		EIB_DPRINTF_ERR(ss->ei_instance,
		    "ibt_query_hca_ports(hca_hdl=0x%llx, "
		    "port=0x%x) returned num_pi=%d", ss->ei_hca_hdl,
		    ss->ei_props->ep_port_num, num_pi);
		ibt_free_portinfo(pi, sz_pi);
		goto ibt_hca_init_fail;
	}

	ss->ei_props->ep_sgid = pi->p_sgid_tbl[0];
	ss->ei_props->ep_mtu = (128 << pi->p_mtu);
	ibt_free_portinfo(pi, sz_pi);

	progress |= EIB_HCAINIT_HCA_PORTS_QUERIED;

	/*
	 * Allocate a protection domain for all our transactions
	 */
	ret = ibt_alloc_pd(ss->ei_hca_hdl, IBT_PD_NO_FLAGS, &ss->ei_pd_hdl);
	if (ret != IBT_SUCCESS) {
		EIB_DPRINTF_ERR(ss->ei_instance,
		    "ibt_alloc_pd(hca_hdl=0x%llx, "
		    "hca_guid=0x%llx) failed, ret=%d",
		    ss->ei_hca_hdl, ss->ei_props->ep_hca_guid, ret);
		goto ibt_hca_init_fail;
	}
	progress |= EIB_HCAINIT_PD_ALLOCD;

	/*
	 * Finally, record the capabilities
	 */
	ss->ei_caps = kmem_zalloc(sizeof (eib_caps_t), KM_SLEEP);
	eib_ibt_record_capab(ss, ss->ei_hca_attrs, ss->ei_caps);
	eib_ibt_record_srate(ss);

	progress |= EIB_HCAINIT_CAPAB_RECORDED;

	return (EIB_E_SUCCESS);

ibt_hca_init_fail:
	eib_rb_ibt_hca_init(ss, progress);
	return (EIB_E_FAILURE);
}

void
eib_ibt_link_mod(eib_t *ss)
{
	eib_node_state_t *ns = ss->ei_node_state;
	ibt_hca_portinfo_t *pi;
	ibt_status_t ret;
	uint8_t vn0_mac[ETHERADDRL];
	boolean_t all_zombies = B_FALSE;
	boolean_t all_need_rejoin = B_FALSE;
	uint_t num_pi;
	uint_t sz_pi;
	uint8_t itr;

	if (ns->ns_link_state == LINK_STATE_UNKNOWN)
		return;

	/*
	 * See if we can get the port attributes or we're as good as down.
	 */
	ret = ibt_query_hca_ports(ss->ei_hca_hdl, ss->ei_props->ep_port_num,
	    &pi, &num_pi, &sz_pi);
	if ((ret != IBT_SUCCESS) || (pi->p_linkstate != IBT_PORT_ACTIVE)) {
		ibt_free_portinfo(pi, sz_pi);
		eib_mac_link_down(ss, B_FALSE);
		return;
	}

	/*
	 * If the SM re-initialized the port attributes, but did not preserve
	 * the old attributes, we need to check more.
	 */
	itr = pi->p_init_type_reply;
	if (EIB_PORT_ATTR_LOADED(itr) && EIB_PORT_ATTR_NOT_PRESERVED(itr)) {
		/*
		 * We're just coming back up; if we see that our base lid
		 * or sgid table has changed, we'll update these and try to
		 * restart all active vnics. If any of the vnic pkeys have
		 * changed, we'll reset the affected channels to the new pkey.
		 */
		if (bcmp(pi->p_sgid_tbl, &ss->ei_props->ep_sgid,
		    sizeof (ib_gid_t)) != 0) {
			EIB_DPRINTF_VERBOSE(ss->ei_instance,
			    "eib_ibt_link_mod: port sgid table changed "
			    "(old %llx.%llx != new %llx.%llx), "
			    "all vnics are zombies now.",
			    ss->ei_props->ep_sgid.gid_prefix,
			    ss->ei_props->ep_sgid.gid_guid,
			    pi->p_sgid_tbl[0].gid_prefix,
			    pi->p_sgid_tbl[0].gid_guid);

			ss->ei_props->ep_sgid = pi->p_sgid_tbl[0];
			all_zombies = B_TRUE;

		} else if (ss->ei_props->ep_blid != pi->p_base_lid) {
			EIB_DPRINTF_VERBOSE(ss->ei_instance,
			    "eib_ibt_link_mod: port base lid changed "
			    "(old 0x%x != new 0x%x), "
			    "all vnics are zombies now.",
			    ss->ei_props->ep_blid, pi->p_base_lid);

			ss->ei_props->ep_blid = pi->p_base_lid;
			all_zombies = B_TRUE;

		} else if (eib_ibt_has_any_pkey_changed(ss)) {
			EIB_DPRINTF_VERBOSE(ss->ei_instance,
			    "eib_ibt_link_mod: pkey has changed for vnic(s), "
			    "resetting all partitions");

			eib_ibt_reset_partitions(ss);
		}
	}

	if (pi) {
		ibt_free_portinfo(pi, sz_pi);
	}

	/*
	 * If the SM hasn't preserved our presence in MCGs, we need to
	 * rejoin all of them.
	 */
	if (EIB_PORT_PRES_NOT_PRESERVED(itr)) {
		EIB_DPRINTF_VERBOSE(ss->ei_instance, "eib_ibt_link_mod: "
		    "hca_guid=0x%llx, port=0x%x presence not preserved in SM, "
		    "rejoining all mcgs", ss->ei_props->ep_hca_guid,
		    ss->ei_props->ep_port_num);

		all_need_rejoin = B_TRUE;
	}

	/*
	 * Before we do the actual work of restarting/rejoining, we need to
	 * see if the GW is reachable at this point of time.  If not, we
	 * still continue to keep our link "down."  Whenever the GW becomes
	 * reachable again, we'll restart/rejoin all the vnics that we've
	 * just marked.
	 */
	rw_enter(&ss->ei_vnic_lock, RW_READER);
	if (all_zombies || all_need_rejoin) {
		if (rw_tryupgrade(&ss->ei_vnic_lock) == 0) {
			rw_exit(&ss->ei_vnic_lock);
			rw_enter(&ss->ei_vnic_lock, RW_WRITER);
		}
		if (all_zombies) {
			ss->ei_zombie_vnics = ss->ei_active_vnics;
		}
		if (all_need_rejoin) {
			ss->ei_rejoin_vnics = ss->ei_active_vnics;
		}
	}
	rw_exit(&ss->ei_vnic_lock);

	mutex_enter(&ss->ei_ka_lock);
	if (ss->ei_ka_gw_unreachable) {
		EIB_DPRINTF_WARN(ss->ei_instance, "eib_ibt_link_mod: "
		    "gateway (gw_port=0x%x) unreachable for "
		    "hca_guid=0x%llx, port=0x%x, link state down",
		    ss->ei_gw_props->pp_gw_portid, ss->ei_props->ep_hca_guid,
		    ss->ei_props->ep_port_num);

		eib_mac_link_down(ss, B_FALSE);
		mutex_exit(&ss->ei_ka_lock);
		return;
	}
	mutex_exit(&ss->ei_ka_lock);

	/*
	 * Try to awaken the dead if possible
	 */
	bcopy(eib_zero_mac, vn0_mac, ETHERADDRL);
	if (all_zombies) {
		EIB_DPRINTF_VERBOSE(ss->ei_instance, "eib_ibt_link_mod: "
		    "hca_guid=0x%llx, hca_port=0x%x, gw_port=0x%x, "
		    "attempting to resurrect zombies",
		    ss->ei_props->ep_hca_guid, ss->ei_props->ep_port_num,
		    ss->ei_gw_props->pp_gw_portid);

		eib_vnic_resurrect_zombies(ss, vn0_mac);
	}

	/*
	 * Re-join the mcgs if we need to
	 */
	if (all_need_rejoin) {
		EIB_DPRINTF_VERBOSE(ss->ei_instance, "eib_ibt_link_mod: "
		    "hca_guid=0x%llx, hca_port=0x%x, gw_port=0x%x, "
		    "attempting to rejoin mcgs",
		    ss->ei_props->ep_hca_guid, ss->ei_props->ep_port_num,
		    ss->ei_gw_props->pp_gw_portid);

		eib_vnic_rejoin_mcgs(ss);
	}

	/*
	 * If we've restarted the zombies because the gateway went down and
	 * came back, it is possible our unicast mac address changed from
	 * what it was earlier. If so, we need to update our unicast address
	 * with the mac layer before marking the link up.
	 */
	if (bcmp(vn0_mac, eib_zero_mac, ETHERADDRL) != 0)
		mac_unicst_update(ss->ei_mac_hdl, vn0_mac);

	/*
	 * Notify the link state up if required
	 */
	eib_mac_link_up(ss, B_FALSE);
}

int
eib_ibt_modify_chan_pkey(eib_t *ss, eib_chan_t *chan, ib_pkey_t pkey)
{
	/*
	 * Make sure the channel pkey and index are set to what we need
	 */
	return (eib_ibt_chan_pkey(ss, chan, pkey, B_TRUE, NULL));
}

/*ARGSUSED*/
void
eib_ibt_async_handler(void *clnt_private, ibt_hca_hdl_t hca_hdl,
    ibt_async_code_t code, ibt_async_event_t *event)
{
	eib_t *ss = (eib_t *)clnt_private;
	eib_event_t *evi;
	uint_t ev_code;

	ev_code = EIB_EV_NONE;

	switch (code) {
	case IBT_EVENT_SQD:
		EIB_DPRINTF_VERBOSE(ss->ei_instance,
		    "eib_ibt_async_handler: got IBT_EVENT_SQD");
		eib_ibt_wakeup_waiter(ss, code, event->ev_chan_hdl);
		break;

	case IBT_EVENT_EMPTY_CHAN:
		EIB_DPRINTF_VERBOSE(ss->ei_instance,
		    "eib_ibt_async_handler: got IBT_EVENT_EMPTY_CHAN");
		eib_ibt_wakeup_waiter(ss, code, event->ev_chan_hdl);
		break;

	case IBT_EVENT_PORT_UP:
		if (event->ev_port == ss->ei_props->ep_port_num) {
			EIB_DPRINTF_VERBOSE(ss->ei_instance,
			    "eib_ibt_async_handler: got IBT_EVENT_PORT_UP");
			ev_code = EIB_EV_PORT_UP;
		}
		break;

	case IBT_ERROR_PORT_DOWN:
		if (event->ev_port == ss->ei_props->ep_port_num) {
			EIB_DPRINTF_VERBOSE(ss->ei_instance,
			    "eib_ibt_async_handler: got IBT_ERROR_PORT_DOWN");
			ev_code = EIB_EV_PORT_DOWN;
		}
		break;

	case IBT_CLNT_REREG_EVENT:
		if (event->ev_port == ss->ei_props->ep_port_num) {
			EIB_DPRINTF_VERBOSE(ss->ei_instance,
			    "eib_ibt_async_handler: got IBT_CLNT_REREG_EVENT");
			ev_code = EIB_EV_CLNT_REREG;
		}
		break;

	case IBT_PORT_CHANGE_EVENT:
		if ((event->ev_port == ss->ei_props->ep_port_num) &&
		    (event->ev_port_flags & IBT_PORT_CHANGE_PKEY)) {
			EIB_DPRINTF_VERBOSE(ss->ei_instance,
			    "eib_ibt_async_handler: "
			    "got IBT_PORT_CHANGE_EVENT(PKEY_CHANGE)");
			ev_code = EIB_EV_PKEY_CHANGE;
		} else if ((event->ev_port == ss->ei_props->ep_port_num) &&
		    (event->ev_port_flags & IBT_PORT_CHANGE_SGID)) {
			EIB_DPRINTF_VERBOSE(ss->ei_instance,
			    "eib_ibt_async_handler: "
			    "got IBT_PORT_CHANGE_EVENT(SGID_CHANGE)");
			ev_code = EIB_EV_SGID_CHANGE;
		}
		break;

	case IBT_HCA_ATTACH_EVENT:
		/*
		 * For HCA attach, after a new HCA is plugged in and
		 * configured using cfgadm, an explicit plumb will need
		 * to be run, so we don't need to do anything here.
		 */
		EIB_DPRINTF_VERBOSE(ss->ei_instance, "eib_ibt_async_handler: "
		    "got IBT_HCA_ATTACH_EVENT");
		break;

	case IBT_HCA_DETACH_EVENT:
		EIB_DPRINTF_VERBOSE(ss->ei_instance, "eib_ibt_async_handler: "
		    "got IBT_HCA_DETACH_EVENT");

		eib_mac_set_nic_state(ss, EIB_NIC_STOPPING);

		/*
		 * If the eoib instance is in use, do not allow the
		 * HCA detach to proceed.
		 */
		if (EIB_STARTED(ss)) {
			EIB_DPRINTF_WARN(ss->ei_instance,
			    "eib_ibt_async_handler: eoib "
			    "instance still in use, failing HCA detach");
			cmn_err(CE_WARN,
			    "!eoib%d still in use, failing HCA detach",
			    ss->ei_instance);
			eib_mac_clr_nic_state(ss, EIB_NIC_STOPPING);
			break;
		}

		/*
		 * We don't do ibt_open_hca() until the eoib instance is
		 * started, so we shouldn't be holding any HCA resource
		 * at this point.  However, if an earlier unplumb had
		 * not cleaned up the HCA resources properly (say,
		 * because the network layer hadn't returned the buffers
		 * at that time), we could be holding hca resources.
		 * We will try to release them here, and protect the
		 * code from racing with some other plumb/unplumb
		 * operation.
		 */
		if (eib_mac_cleanup_hca_resources(ss) != EIB_E_SUCCESS) {
			EIB_DPRINTF_WARN(ss->ei_instance,
			    "eib_events_handler: could not release hca "
			    "resources, failing HCA detach");
		}
		eib_mac_clr_nic_state(ss, EIB_NIC_STOPPING);
		break;
	}

	if (ev_code != EIB_EV_NONE) {
		evi = kmem_zalloc(sizeof (eib_event_t), KM_NOSLEEP);
		if (evi == NULL) {
			EIB_DPRINTF_WARN(ss->ei_instance,
			    "eib_ibt_async_handler: "
			    "no memory, could not handle event 0x%lx", ev_code);
		} else {
			evi->ev_code = ev_code;
			evi->ev_arg = NULL;
			eib_svc_enqueue_event(ss, evi);
		}
	}
}

/*ARGSUSED*/
void
eib_ibt_record_capab(eib_t *ss, ibt_hca_attr_t *hca_attrs, eib_caps_t *caps)
{
	uint_t max_swqe = EIB_DATA_MAX_SWQE;
	uint_t max_rwqe = EIB_DFL_DATA_SRQ_WR_SZ;

	/*
	 * Checksum
	 */
	caps->cp_cksum_flags = 0;
	if ((!eib_wa_no_cksum_offload) &&
	    (hca_attrs->hca_flags & IBT_HCA_CKSUM_FULL)) {
		caps->cp_cksum_flags = HCKSUM_IPHDRCKSUM | HCKSUM_INET_FULL_V4;
	}

	/*
	 * Reserved L-Key
	 */
	if (hca_attrs->hca_flags2 & IBT_HCA2_RES_LKEY) {
		caps->cp_resv_lkey_capab = 1;
		caps->cp_resv_lkey = hca_attrs->hca_reserved_lkey;
	}

	/*
	 * LSO
	 */
	caps->cp_lso_maxlen = 0;
	if (!eib_wa_no_lso) {
		if (hca_attrs->hca_max_lso_size > EIB_LSO_MAXLEN) {
			caps->cp_lso_maxlen = EIB_LSO_MAXLEN;
		} else {
			caps->cp_lso_maxlen = hca_attrs->hca_max_lso_size;
		}
	}

	/*
	 * SGL
	 *
	 * Translating virtual address regions into physical regions
	 * for using the Reserved LKey feature results in a wr sgl that
	 * is a little longer. Since failing ibt_map_mem_iov() is costly,
	 * we'll record a high-water mark (65%) when we should stop
	 * trying to use Reserved LKey
	 */
	if (hca_attrs->hca_flags & IBT_HCA_WQE_SIZE_INFO) {
		caps->cp_max_sgl = hca_attrs->hca_ud_send_sgl_sz;
	} else {
		caps->cp_max_sgl = hca_attrs->hca_max_sgl;
	}
	if (caps->cp_max_sgl > EIB_MAX_SGL) {
		caps->cp_max_sgl = EIB_MAX_SGL;
	}
	caps->cp_hiwm_sgl = (caps->cp_max_sgl * 65) / 100;

	/*
	 * SWQE/RWQE: meet max chan size and max cq size limits (leave room
	 * to avoid cq overflow event)
	 */
	if (max_swqe > hca_attrs->hca_max_chan_sz)
		max_swqe = hca_attrs->hca_max_chan_sz;
	if (max_swqe > (hca_attrs->hca_max_cq_sz - 1))
		max_swqe = hca_attrs->hca_max_cq_sz - 1;
	caps->cp_max_swqe = max_swqe;

	if (max_rwqe > hca_attrs->hca_max_chan_sz)
		max_rwqe = hca_attrs->hca_max_chan_sz;
	if (max_rwqe > (hca_attrs->hca_max_cq_sz - 1))
		max_rwqe = hca_attrs->hca_max_cq_sz - 1;
	caps->cp_max_rwqe = max_rwqe;
}

int
eib_ibt_setup_srqs(eib_t *ss, int *err)
{
	if (ss->ei_ctladm_srq == NULL) {
		ss->ei_ctladm_srq = eib_ibt_setup_chan_srq(ss,
		    EIB_DFL_CTLADM_SRQ_WR_SZ, EIB_MIN_CTLADM_SRQ_WR_SZ,
		    0, B_FALSE, EIB_NUM_CTLADM_RXQ);
		if (ss->ei_ctladm_srq == NULL)
			goto ibt_setup_srqs_fail;
	}

	if (ss->ei_data_srq == NULL) {
		ss->ei_data_srq = eib_ibt_setup_chan_srq(ss,
		    ss->ei_caps->cp_max_rwqe, EIB_MIN_DATA_SRQ_WR_SZ,
		    EIB_IP_HDR_ALIGN, B_TRUE, EIB_NUM_DATA_RXQ);
		if (ss->ei_data_srq == NULL)
			goto ibt_setup_srqs_fail;
	}

	return (EIB_E_SUCCESS);

ibt_setup_srqs_fail:
	*err = EINVAL;
	eib_rb_ibt_setup_srqs(ss);
	return (EIB_E_FAILURE);
}

void
eib_rb_ibt_hca_init(eib_t *ss, uint_t progress)
{
	ibt_status_t ret;

	if (progress & EIB_HCAINIT_CAPAB_RECORDED) {
		if (ss->ei_caps) {
			kmem_free(ss->ei_caps, sizeof (eib_caps_t));
			ss->ei_caps = NULL;
		}
	}

	if (progress & EIB_HCAINIT_PD_ALLOCD) {
		if (ss->ei_pd_hdl) {
			ret = ibt_free_pd(ss->ei_hca_hdl, ss->ei_pd_hdl);
			if (ret != IBT_SUCCESS) {
				EIB_DPRINTF_WARN(ss->ei_instance,
				    "eib_rb_ibt_hca_init: "
				    "ibt_free_pd(hca_hdl=0x%lx, pd_hdl=0x%lx) "
				    "failed, ret=%d", ss->ei_hca_hdl,
				    ss->ei_pd_hdl, ret);
			}
			ss->ei_pd_hdl = NULL;
		}
	}

	if (progress & EIB_HCAINIT_HCA_PORTS_QUERIED) {
		ss->ei_props->ep_mtu = 0;
		bzero(&ss->ei_props->ep_sgid, sizeof (ib_gid_t));
	}

	if (progress & EIB_HCAINIT_ATTRS_ALLOCD) {
		kmem_free(ss->ei_hca_attrs, sizeof (ibt_hca_attr_t));
		ss->ei_hca_attrs = NULL;
	}

	if (progress & EIB_HCAINIT_HCA_OPENED) {
		ret = ibt_close_hca(ss->ei_hca_hdl);
		if (ret != IBT_SUCCESS) {
			EIB_DPRINTF_WARN(ss->ei_instance,
			    "ibt_close_hca(hca_hdl=0x%lx) failed, "
			    "ret=%d", ss->ei_hca_hdl, ret);
		}
		ss->ei_hca_hdl = NULL;
	}
}

void
eib_rb_ibt_setup_srqs(eib_t *ss)
{
	eib_wqe_t *rwqe;

	/*
	 * When this is called, we're sure that there are no wqes with
	 * the nw layer.  But between the time we allocated the SRQ(s)
	 * and now, we may have posted a number of rwqes to the HCA
	 * which will need to be returned to the wqe pool before we
	 * can free the SRQs.
	 *
	 * Since we also need to free the mblks associated with the
	 * data rwqes, we'll simply call freemsg() on the mblk pointer
	 * if present and the callback from STREAMS will take care of
	 * returning the rwqe. If the mblk pointer is not present, this
	 * would be a wqe we posted to the ctl/adm SRQ and we can return
	 * it to the pool directly.
	 */
	while (rwqe = eib_rsrc_nxt_rwqe_to_free(ss)) {
		if (rwqe->qe_mp) {
			rwqe->qe_info |= EIB_WQE_FLG_RET_TO_POOL;
			freemsg(rwqe->qe_mp);
		} else {
			eib_rsrc_return_rwqe(ss, rwqe);
		}
	}

	eib_rb_ibt_setup_chan_srq(ss, ss->ei_data_srq);
	ss->ei_data_srq = NULL;

	eib_rb_ibt_setup_chan_srq(ss, ss->ei_ctladm_srq);
	ss->ei_ctladm_srq = NULL;
}

static void
eib_ibt_reset_partitions(eib_t *ss)
{
	eib_vnic_t *vnic;
	eib_chan_t *chan = NULL;
	uint64_t av;
	int inst = 0;

	/*
	 * We already have the vhub pkey recorded in our eib_chan_t.
	 * We only need to make sure our pkey index still matches it.
	 * If not, modify the channel appropriately and update our
	 * records.
	 */
	if ((chan = ss->ei_admin_chan) != NULL)
		(void) eib_ibt_modify_chan_pkey(ss, chan, chan->ch_pkey);

	rw_enter(&ss->ei_vnic_lock, RW_READER);
	av = ss->ei_active_vnics;
	while ((inst = EIB_FIND_LSB_SET(av)) != -1) {
		if ((vnic = ss->ei_vnic[inst]) != NULL) {
			if ((chan = vnic->vn_ctl_chan) != NULL) {
				(void) eib_ibt_modify_chan_pkey(ss, chan,
				    chan->ch_pkey);
			}
			if ((chan = vnic->vn_data_chan) != NULL) {
				(void) eib_ibt_modify_chan_pkey(ss, chan,
				    chan->ch_pkey);
			}
		}
		av &= (~((uint64_t)1 << inst));
	}
	rw_exit(&ss->ei_vnic_lock);
}

static void
eib_ibt_wakeup_waiter(eib_t *ss, ibt_async_code_t code,
    ibt_channel_hdl_t ev_chan_hdl)
{
	eib_vnic_t *vnic;
	eib_chan_t *chan = NULL;
	uint64_t av;
	int inst = 0;

	/*
	 * If this is not a SQD event or a Last WQE Reached event,
	 * return. Otherwise, figure out the channel involved.
	 */
	if ((code != IBT_EVENT_SQD) && (code != IBT_EVENT_EMPTY_CHAN))
		return;

	rw_enter(&ss->ei_vnic_lock, RW_READER);

	/*
	 * Check admin channel
	 */
	chan = ss->ei_admin_chan;
	if ((chan) && (chan->ch_chan == ev_chan_hdl))
		goto wakeup_waiter;

	/*
	 * Check channels of vnic under creation/deletion (if any)
	 */
	if ((vnic = ss->ei_vnic_torndown) != NULL) {
		chan = vnic->vn_ctl_chan;
		if ((chan) && (chan->ch_chan == ev_chan_hdl))
			goto wakeup_waiter;

		chan = vnic->vn_data_chan;
		if ((chan) && (chan->ch_chan == ev_chan_hdl))
			goto wakeup_waiter;

	} else if ((vnic = ss->ei_vnic_pending) != NULL) {
		chan = vnic->vn_ctl_chan;
		if ((chan) && (chan->ch_chan == ev_chan_hdl))
			goto wakeup_waiter;

		chan = vnic->vn_data_chan;
		if ((chan) && (chan->ch_chan == ev_chan_hdl))
			goto wakeup_waiter;
	}

	/*
	 * Check channels of active vnics
	 */
	av = ss->ei_active_vnics;
	while ((inst = EIB_FIND_LSB_SET(av)) != -1) {
		if ((vnic = ss->ei_vnic[inst]) != NULL) {
			chan = vnic->vn_ctl_chan;
			if (chan->ch_chan == ev_chan_hdl)
				goto wakeup_waiter;

			chan = vnic->vn_data_chan;
			if (chan->ch_chan == ev_chan_hdl)
				goto wakeup_waiter;
		}
		av &= (~((uint64_t)1 << inst));
	}

	if (inst == -1)
		chan = NULL;

wakeup_waiter:
	if (chan) {
		if (code == IBT_EVENT_EMPTY_CHAN) {
			mutex_enter(&chan->ch_emptychan_lock);
			chan->ch_emptychan = B_TRUE;
			cv_signal(&chan->ch_emptychan_cv);
			mutex_exit(&chan->ch_emptychan_lock);

		} else if (code == IBT_EVENT_SQD) {
			mutex_enter(&chan->ch_cep_lock);
			chan->ch_cep_state = IBT_STATE_SQD;
			cv_signal(&chan->ch_cep_cv);
			mutex_exit(&chan->ch_cep_lock);
		}
	}

	rw_exit(&ss->ei_vnic_lock);
}

static int
eib_ibt_chan_pkey(eib_t *ss, eib_chan_t *chan, ib_pkey_t new_pkey,
    boolean_t set, boolean_t *pkey_changed)
{
	ibt_qp_info_t qp_attr;
	ibt_status_t ret;
	uint16_t new_pkey_ix;

	ret = ibt_pkey2index(ss->ei_hca_hdl, ss->ei_props->ep_port_num,
	    new_pkey, &new_pkey_ix);
	if (ret != IBT_SUCCESS) {
		EIB_DPRINTF_WARN(ss->ei_instance, "eib_ibt_chan_pkey: "
		    "ibt_pkey2index(hca_hdl=0x%llx, port_num=0x%x, "
		    "pkey=0x%x) failed, ret=%d",
		    ss->ei_hca_hdl, ss->ei_props->ep_port_num, new_pkey, ret);
		return (EIB_E_FAILURE);
	}

	/*
	 * If the pkey and the pkey index we have already matches the
	 * new one, nothing to do.
	 */
	mutex_enter(&chan->ch_pkey_lock);
	if ((chan->ch_pkey == new_pkey) && (chan->ch_pkey_ix == new_pkey_ix)) {
		if (pkey_changed) {
			*pkey_changed = B_FALSE;
		}
		mutex_exit(&chan->ch_pkey_lock);
		return (EIB_E_SUCCESS);
	}
	if (pkey_changed) {
		*pkey_changed = B_TRUE;
	}
	mutex_exit(&chan->ch_pkey_lock);

	/*
	 * Otherwise, if we're asked only to test if the pkey index
	 * supplied matches the one recorded in the channel, return
	 * success, but don't set the pkey.
	 */
	if (!set) {
		return (EIB_E_SUCCESS);
	}

	/*
	 * Otherwise, we need to change channel pkey.  Pause the
	 * channel sendq first.
	 */
	ret = ibt_pause_sendq(chan->ch_chan, IBT_CEP_SET_SQD_EVENT);
	if (ret != IBT_SUCCESS) {
		EIB_DPRINTF_WARN(ss->ei_instance, "eib_ibt_chan_pkey: "
		    "ibt_pause_sendq(chan_hdl=0x%llx) failed, ret=%d",
		    chan->ch_chan, ret);
		return (EIB_E_FAILURE);
	}

	/*
	 * Wait for the channel to enter the IBT_STATE_SQD state
	 */
	mutex_enter(&chan->ch_cep_lock);
	while (chan->ch_cep_state != IBT_STATE_SQD)
		cv_wait(&chan->ch_cep_cv, &chan->ch_cep_lock);
	mutex_exit(&chan->ch_cep_lock);

	/*
	 * Modify the qp with the supplied pkey index and unpause the channel
	 * If either of these operations fail, we'll leave the channel in
	 * the paused state and fail.
	 */
	bzero(&qp_attr, sizeof (ibt_qp_info_t));

	qp_attr.qp_trans = IBT_UD_SRV;
	qp_attr.qp_current_state = IBT_STATE_SQD;
	qp_attr.qp_state = IBT_STATE_SQD;
	qp_attr.qp_transport.ud.ud_pkey_ix = new_pkey_ix;

	/*
	 * Modify the qp to set the new pkey index, then unpause the
	 * channel and put it in RTS state and update the new values
	 * in our records
	 */
	mutex_enter(&chan->ch_pkey_lock);

	ret = ibt_modify_qp(chan->ch_chan,
	    IBT_CEP_SET_STATE | IBT_CEP_SET_PKEY_IX, &qp_attr, NULL);
	if (ret != IBT_SUCCESS) {
		mutex_exit(&chan->ch_pkey_lock);
		EIB_DPRINTF_WARN(ss->ei_instance, "eib_ibt_chan_pkey: "
		    "ibt_modify_qp(chan_hdl=0x%llx, IBT_CEP_SET_PKEY_IX) "
		    "failed for new_pkey_ix=0x%x, ret=%d",
		    chan->ch_chan, new_pkey_ix, ret);
		return (EIB_E_FAILURE);
	}

	if ((ret = ibt_unpause_sendq(chan->ch_chan)) != IBT_SUCCESS) {
		mutex_exit(&chan->ch_pkey_lock);
		EIB_DPRINTF_WARN(ss->ei_instance, "eib_ibt_chan_pkey: "
		    "ibt_unpause_sendq(chan_hdl=0x%llx) failed, ret=%d",
		    chan->ch_chan, ret);
		return (EIB_E_FAILURE);
	}

	chan->ch_pkey = new_pkey;
	chan->ch_pkey_ix = new_pkey_ix;
	mutex_exit(&chan->ch_pkey_lock);

	return (EIB_E_SUCCESS);
}

static boolean_t
eib_ibt_has_chan_pkey_changed(eib_t *ss, eib_chan_t *chan)
{
	boolean_t changed;
	int ret;

	/*
	 * Don't modify the pkey, just ask if the pkey index for the channel's
	 * pkey has changed for any reason.  If we fail, assume that the pkey
	 * has changed.
	 */
	ret = eib_ibt_chan_pkey(ss, chan, chan->ch_pkey, B_FALSE, &changed);
	if (ret != EIB_E_SUCCESS)
		changed = B_TRUE;

	return (changed);
}

static boolean_t
eib_ibt_has_any_pkey_changed(eib_t *ss)
{
	eib_vnic_t *vnic;
	eib_chan_t *chan = NULL;
	uint64_t av;
	int inst = 0;

	/*
	 * Return true if the pkey index of any our pkeys (of the channels
	 * of all active vnics) has changed.
	 */

	chan = ss->ei_admin_chan;
	if ((chan) && (eib_ibt_has_chan_pkey_changed(ss, chan)))
		return (B_TRUE);

	rw_enter(&ss->ei_vnic_lock, RW_READER);
	av = ss->ei_active_vnics;
	while ((inst = EIB_FIND_LSB_SET(av)) != -1) {
		if ((vnic = ss->ei_vnic[inst]) != NULL) {
			chan = vnic->vn_ctl_chan;
			if ((chan) && (eib_ibt_has_chan_pkey_changed(ss, chan)))
				return (B_TRUE);

			chan = vnic->vn_data_chan;
			if ((chan) && (eib_ibt_has_chan_pkey_changed(ss, chan)))
				return (B_TRUE);
		}
		av &= (~((uint64_t)1 << inst));
	}
	rw_exit(&ss->ei_vnic_lock);

	return (B_FALSE);
}

/*
 * This routine is currently used simply to derive and record the port
 * speed from the loopback path information (for debug purposes).  For
 * EoIB, currently the srate used in address vectors to IB neighbors
 * and the gateway is fixed at IBT_SRATE_10. Eventually though, this
 * information (and sl) has to come from the gateway for all destinations
 * in the vhub table.
 */
static void
eib_ibt_record_srate(eib_t *ss)
{
	ib_gid_t sgid = ss->ei_props->ep_sgid;
	ibt_srate_t srate = IBT_SRATE_10;
	ibt_path_info_t path;
	ibt_path_attr_t path_attr;
	ibt_status_t ret;
	uint8_t num_paths;

	bzero(&path_attr, sizeof (path_attr));
	path_attr.pa_dgids = &sgid;
	path_attr.pa_num_dgids = 1;
	path_attr.pa_sgid = sgid;

	ret = ibt_get_paths(ss->ei_ibt_hdl, IBT_PATH_NO_FLAGS,
	    &path_attr, 1, &path, &num_paths);
	if (ret == IBT_SUCCESS && num_paths >= 1) {
		switch (srate = path.pi_prim_cep_path.cep_adds_vect.av_srate) {
		case IBT_SRATE_2:
		case IBT_SRATE_10:
		case IBT_SRATE_30:
		case IBT_SRATE_5:
		case IBT_SRATE_20:
		case IBT_SRATE_40:
		case IBT_SRATE_60:
		case IBT_SRATE_80:
		case IBT_SRATE_120:
			break;
		default:
			srate = IBT_SRATE_10;
		}
	}

	ss->ei_props->ep_srate = srate;

	EIB_DPRINTF_DEBUG(ss->ei_instance, "eib_ibt_record_srate: "
	    "srate = %d", srate);
}

static eib_srq_t *
eib_ibt_setup_chan_srq(eib_t *ss, uint_t wr_sz, uint_t min_wr_sz,
    uint_t iphdr_align, boolean_t alloc_mp, uint_t num_rxq)
{
	eib_srq_t *srq;
	eib_rxq_t *rxq;
	ibt_srq_sizes_t srq_sizes;
	ibt_srq_sizes_t real_srq_sizes;
	ibt_status_t ret;
	int i;

	/*
	 * SRQ parameters
	 */
	srq = kmem_zalloc(sizeof (eib_srq_t), KM_SLEEP);
	srq->sr_srq_wr_sz = wr_sz;
	srq->sr_srq_min_wr_sz = min_wr_sz;
	srq->sr_ip_hdr_align = iphdr_align;
	srq->sr_alloc_mp = alloc_mp;
	srq->sr_num_rxq = num_rxq;
	srq->sr_srq_hdl = NULL;

	for (i = 0; i < srq->sr_num_rxq; i++) {
		rxq = &srq->sr_rxq[i];
		mutex_init(&rxq->rx_q_lock, NULL, MUTEX_DRIVER, NULL);
	}

	/*
	 * Allocate the SRQ
	 */
	do {
		srq_sizes.srq_wr_sz = srq->sr_srq_wr_sz;
		srq_sizes.srq_sgl_sz = 1;

		ret = ibt_alloc_srq(ss->ei_hca_hdl, IBT_SRQ_NO_FLAGS,
		    ss->ei_pd_hdl, &srq_sizes, &srq->sr_srq_hdl,
		    &real_srq_sizes);

		srq->sr_srq_wr_sz = (ret == IBT_SUCCESS) ?
		    real_srq_sizes.srq_wr_sz : (srq->sr_srq_wr_sz >> 1);

	} while ((ret == IBT_HCA_WR_EXCEEDED) &&
	    (srq->sr_srq_wr_sz >= srq->sr_srq_min_wr_sz));

	if (ret != IBT_SUCCESS) {
		EIB_DPRINTF_WARN(ss->ei_instance, "eib_setup_srq: "
		    "could not alloc srq, ret=%d", ret);
		goto ibt_setup_chan_srq_fail;
	}

	/*
	 * Post work requests into the SRQ and fill it up
	 */
	if ((ret = eib_chan_fill_srq(ss, srq)) != EIB_E_SUCCESS) {
		EIB_DPRINTF_WARN(ss->ei_instance, "eib_setup_srq: "
		    "could not post rx into srq, ret=%d", ret);
		goto ibt_setup_chan_srq_fail;
	}

	return (srq);

ibt_setup_chan_srq_fail:
	eib_rb_ibt_setup_chan_srq(ss, srq);
	return (NULL);
}

static void
eib_rb_ibt_setup_chan_srq(eib_t *ss, eib_srq_t *srq)
{
	eib_rxq_t *rxq;
	ibt_status_t ret;
	int i;

	if (srq == NULL)
		return;

	if (srq->sr_srq_hdl) {
		if ((ret = ibt_free_srq(srq->sr_srq_hdl)) != IBT_SUCCESS) {
			EIB_DPRINTF_DEBUG(ss->ei_instance,
			    "eib_rb_ibt_setup_chan_srq: could not free "
			    "srq, ret=%d", ret);
		}
		srq->sr_srq_hdl = NULL;
	}

	for (i = 0; i < srq->sr_num_rxq; i++) {
		rxq = &srq->sr_rxq[i];
		mutex_destroy(&rxq->rx_q_lock);
	}

	kmem_free(srq, sizeof (eib_srq_t));
}
