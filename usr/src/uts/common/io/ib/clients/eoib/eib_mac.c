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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/devops.h>
#include <sys/kmem.h>
#include <sys/ksynch.h>
#include <sys/modctl.h>
#include <sys/stat.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>

#include <sys/ib/clients/eoib/eib_impl.h>

/*
 * Declarations private to this file
 */
static mblk_t *eib_mac_ring_poll(void *, int, int);
static int eib_mac_ring_start(mac_ring_driver_t, uint64_t);
static int eib_mac_ring_rx_stat(mac_ring_driver_t, uint_t, uint64_t *);
static int eib_mac_ring_enable_intr(mac_ring_driver_t);
static int eib_mac_ring_disable_intr(mac_ring_driver_t);
static int eib_mac_group_addmac(void *, const uint8_t *, uint64_t);
static int eib_mac_group_remmac(void *, const uint8_t *);
static void eib_mac_flush_rx_rings(eib_t *);
static void eib_rb_mac_start(eib_t *, eib_vnic_t *);

/*
 * This set of routines are used to set/clear the condition that the
 * caller is about to do something that affects the state of the nic.
 * If there's already someone doing either a start or a stop (possibly
 * due to the async handler, a plumb or a dlpi_open happening, or an
 * unplumb or dlpi_close coming in), we wait until that's done.
 */
void
eib_mac_set_nic_state(eib_t *ss, uint_t flags)
{
	eib_node_state_t *ns = ss->ei_node_state;

	mutex_enter(&ns->ns_lock);

	while ((ns->ns_nic_state & EIB_NIC_STARTING) ||
	    (ns->ns_nic_state & EIB_NIC_STOPPING)) {
		cv_wait(&ns->ns_cv, &ns->ns_lock);
	}
	ns->ns_nic_state |= flags;

	mutex_exit(&ns->ns_lock);
}

void
eib_mac_clr_nic_state(eib_t *ss, uint_t flags)
{
	eib_node_state_t *ns = ss->ei_node_state;

	mutex_enter(&ns->ns_lock);

	ns->ns_nic_state &= (~flags);

	cv_broadcast(&ns->ns_cv);
	mutex_exit(&ns->ns_lock);
}

void
eib_mac_upd_nic_state(eib_t *ss, uint_t clr_flags, uint_t set_flags)
{
	eib_node_state_t *ns = ss->ei_node_state;

	mutex_enter(&ns->ns_lock);

	ns->ns_nic_state &= (~clr_flags);
	ns->ns_nic_state |= set_flags;

	cv_broadcast(&ns->ns_cv);
	mutex_exit(&ns->ns_lock);
}

uint_t
eib_mac_get_nic_state(eib_t *ss)
{
	eib_node_state_t *ns = ss->ei_node_state;
	uint_t nic_state;

	mutex_enter(&ns->ns_lock);
	nic_state = ns->ns_nic_state;
	mutex_exit(&ns->ns_lock);

	return (nic_state);
}

void
eib_mac_link_state(eib_t *ss, link_state_t new_link_state,
    boolean_t force)
{
	eib_node_state_t *ns = ss->ei_node_state;
	boolean_t state_changed = B_FALSE;

	mutex_enter(&ns->ns_lock);

	/*
	 * We track the link state only if the current link state is
	 * not unknown.  Obviously therefore, the first calls to set
	 * the link state from eib_mac_start() have to pass an explicit
	 * 'force' flag to force the state change tracking.
	 */
	if (ns->ns_link_state != LINK_STATE_UNKNOWN)
		force = B_TRUE;

	if ((force) && (new_link_state != ns->ns_link_state)) {
		ns->ns_link_state = new_link_state;
		state_changed = B_TRUE;
	}
	mutex_exit(&ns->ns_lock);

	if (state_changed) {
		EIB_DPRINTF_DEBUG(ss->ei_instance,
		    "eib_mac_link_state: changing link state to %d",
		    new_link_state);

		mac_link_update(ss->ei_mac_hdl, new_link_state);
	} else  {
		EIB_DPRINTF_DEBUG(ss->ei_instance,
		    "eib_mac_link_state: link state already %d",
		    new_link_state);
	}
}

void
eib_mac_link_up(eib_t *ss, boolean_t force)
{
	eib_mac_link_state(ss, LINK_STATE_UP, force);
}

void
eib_mac_link_down(eib_t *ss, boolean_t force)
{
	eib_mac_link_state(ss, LINK_STATE_DOWN, force);
}

int
eib_mac_start(eib_t *ss)
{
	eib_vnic_t *vnic0 = NULL;
	eib_login_data_t *ld;
	int err;

	/*
	 * Perform HCA related initializations
	 */
	if (eib_ibt_hca_init(ss) != EIB_E_SUCCESS)
		goto start_fail;

	/*
	 * Make sure port is up. Also record the port base lid if it's up.
	 */
	if (eib_mac_hca_portstate(ss, &ss->ei_props->ep_blid,
	    &err) != EIB_E_SUCCESS) {
		goto start_fail;
	}

	/*
	 * Set up tx and rx buffer pools
	 */
	if (eib_rsrc_setup_bufs(ss, &err) != EIB_E_SUCCESS)
		goto start_fail;

	/*
	 * Set up SRQs for data and control/admin qps
	 */
	if (eib_ibt_setup_srqs(ss, &err) != EIB_E_SUCCESS)
		goto start_fail;

	/*
	 * Set up admin qp for logins and logouts
	 */
	if (eib_adm_setup_qp(ss, &err) != EIB_E_SUCCESS)
		goto start_fail;

	/*
	 * Create the vnic for physlink (instance 0)
	 */
	if (eib_vnic_create(ss, 0, 0, &vnic0, &err) != EIB_E_SUCCESS)
		goto start_fail;

	/*
	 * Update the mac layer about the correct values for MTU and
	 * unicast MAC address.  Note that we've already verified that the
	 * vhub mtu (plus the eoib encapsulation header) is not greater
	 * than our port mtu, so we can go ahead and report the vhub mtu
	 * (of vnic0) directly.
	 */
	ld = &(vnic0->vn_login_data);
	(void) mac_maxsdu_update(ss->ei_mac_hdl, ld->ld_vhub_mtu);
	mac_unicst_update(ss->ei_mac_hdl, ld->ld_assigned_mac);

	/*
	 * Report that the link is up and ready
	 */
	eib_mac_link_up(ss, B_TRUE);
	return (0);

start_fail:
	eib_rb_mac_start(ss, vnic0);
	eib_mac_link_down(ss, B_TRUE);
	return (err);
}

void
eib_mac_stop(eib_t *ss)
{
	eib_vnic_t *vnic;
	link_state_t cur_link_state = ss->ei_node_state->ns_link_state;
	int ndx;

	/*
	 * Stopping an EoIB device instance is somewhat different from starting
	 * it. Between the time the device instance was started and the call to
	 * eib_m_stop() now, a number of vnics could've been created. All of
	 * these will need to be destroyed before we can stop the device.
	 */
	for (ndx = EIB_MAX_VNICS - 1; ndx >= 0; ndx--) {
		if ((vnic = ss->ei_vnic[ndx]) != NULL)
			eib_vnic_delete(ss, vnic);
	}

	/*
	 * And now, to undo the things we did in start (other than creation
	 * of vnics itself)
	 */
	eib_rb_mac_start(ss, NULL);

	/*
	 * Now that we're completed stopped, there's no mac address assigned
	 * to us.  Update the mac layer with this information. Note that we
	 * can let the old max mtu information remain as-is, since we're likely
	 * to get that same mtu on a later plumb.
	 */
	mac_unicst_update(ss->ei_mac_hdl, eib_zero_mac);

	/*
	 * If our link state was up when the eib_m_stop() callback was called,
	 * we'll mark the link state as unknown now.  Otherwise, we'll leave
	 * the link state as-is (down).
	 */
	if (cur_link_state == LINK_STATE_UP)
		eib_mac_link_state(ss, LINK_STATE_UNKNOWN, B_TRUE);
}

int
eib_mac_multicast(eib_t *ss, boolean_t add, uint8_t *mcast_mac)
{
	int ret = EIB_E_SUCCESS;
	int err = 0;

	/*
	 * If it's a broadcast group join, each vnic needs to and is always
	 * joined to the broadcast address, so we return success immediately.
	 * If it's a broadcast group leave, we fail immediately for the same
	 * reason as above.
	 */
	if (bcmp(mcast_mac, eib_broadcast_mac, ETHERADDRL) == 0) {
		if (add)
			return (0);
		else
			return (EINVAL);
	}

	if (ss->ei_vnic[0]) {
		if (add) {
			ret = eib_vnic_join_multicast(ss, ss->ei_vnic[0],
			    mcast_mac, &err);
		} else {
			eib_vnic_leave_multicast(ss, ss->ei_vnic[0], mcast_mac);
			ret = EIB_E_SUCCESS;
		}
	}

	if (ret == EIB_E_SUCCESS)
		return (0);
	else
		return (err);
}

int
eib_mac_promisc(eib_t *ss, boolean_t set)
{
	int ret = EIB_E_SUCCESS;
	int err = 0;

	if (ss->ei_vnic[0]) {
		if (set) {
			ret = eib_vnic_join_multicast(ss, ss->ei_vnic[0],
			    eib_zero_mac, &err);
		} else {
			eib_vnic_leave_multicast(ss, ss->ei_vnic[0],
			    eib_zero_mac);
			ret = EIB_E_SUCCESS;
		}
	}

	if (ret == EIB_E_SUCCESS)
		return (0);
	else
		return (err);
}

int
eib_mac_tx(eib_t *ss, mblk_t *mp)
{
	eib_ether_hdr_t evh;
	eib_vnic_t *vnic = NULL;
	eib_wqe_t *swqe = NULL;
	boolean_t failed_vnic;
	int found;
	int ret;

	/*
	 * Grab a send wqe.  If we cannot get one, wake up a service
	 * thread to monitor the swqe status and let the mac layer know
	 * as soon as we have enough tx wqes to start the traffic again.
	 */
	if ((swqe = eib_rsrc_grab_swqe(ss, EIB_WPRI_LO)) == NULL) {
		EIB_DPRINTF_WARN(ss->ei_instance, "eib_mac_tx: "
		    "no swqe available, holding tx until resource "
		    "becomes available");
		eib_rsrc_txwqes_needed(ss);
		return (EIB_E_FAILURE);
	}

	/*
	 * Determine dmac, smac and vlan information
	 */
	eib_data_parse_ether_hdr(mp, &evh);

	/*
	 * Lookup the {smac, vlan} tuple in our vnic list. If it isn't
	 * there, this is obviously a new packet on a vnic/vlan that
	 * we haven't been informed about. So go ahead and file a request
	 * to create a new vnic. This is obviously not a clean thing to
	 * do - we should be informed when a vnic/vlan is being created
	 * and should be given a proper opportunity to login to the gateway
	 * and do the creation.  But we don't have that luxury now, and
	 * this is the next best thing to do.  Note that we return failure
	 * from here, so tx flow control should prevent further packets
	 * from coming in until the vnic creation has completed.
	 */
	found = eib_data_lookup_vnic(ss, evh.eh_smac, evh.eh_vlan, &vnic,
	    &failed_vnic);
	if (found != EIB_E_SUCCESS) {
		uint8_t *m = evh.eh_smac;

		/*
		 * Return the swqe back to the pool
		 */
		eib_rsrc_return_swqe(ss, swqe);

		/*
		 * If we had previously tried creating this vnic and had
		 * failed, we'll simply drop the packets on this vnic.
		 * Otherwise, we'll queue up a request to create this vnic.
		 */
		if (failed_vnic) {
			EIB_DPRINTF_VERBOSE(ss->ei_instance, "eib_mac_tx: "
			    "vnic creation for mac=%x:%x:%x:%x:%x:%x "
			    "vlan=0x%x failed previously, dropping pkt",
			    m[0], m[1], m[2], m[3], m[4], m[5], evh.eh_vlan);
			return (EIB_E_SUCCESS);
		} else {
			eib_vnic_need_new(ss, evh.eh_smac, evh.eh_vlan);
			return (EIB_E_FAILURE);
		}
	}

	/*
	 * We'll try to setup the destination in the swqe for this dmac
	 * and vlan.  If we don't succeed, there's no need to undo any
	 * vnic-creation we might've made above (if we didn't find the
	 * vnic corresponding to the {smac, vlan} originally). Note that
	 * this is not a resource issue, so we'll issue a warning and
	 * drop the packet, but won't return failure from here.
	 */
	ret = eib_vnic_setup_dest(vnic, swqe, evh.eh_dmac, evh.eh_vlan);
	if (ret != EIB_E_SUCCESS) {
		uint8_t *dmac;

		dmac = evh.eh_dmac;
		EIB_DPRINTF_WARN(ss->ei_instance, "eib_mac_tx: "
		    "eib_vnic_setup_dest() failed for mac=%x:%x:%x:%x:%x:%x, "
		    "vlan=0x%x, dropping pkt", dmac[0], dmac[1], dmac[2],
		    dmac[3], dmac[4], dmac[5], evh.eh_vlan);

		eib_rsrc_return_swqe(ss, swqe);
		return (EIB_E_SUCCESS);
	}

	/*
	 * The only reason why this would fail is if we needed LSO buffer(s)
	 * to prepare this frame and couldn't find enough of those.
	 */
	ret = eib_data_prepare_frame(vnic, swqe, mp, &evh);
	if (ret != EIB_E_SUCCESS) {
		EIB_DPRINTF_WARN(ss->ei_instance, "eib_mac_tx: "
		    "eib_data_prepare_frame() failed (no LSO bufs?), "
		    "holding tx until resource becomes available");

		eib_rsrc_return_swqe(ss, swqe);
		eib_rsrc_lsobufs_needed(ss);
		return (EIB_E_FAILURE);
	}

	eib_data_post_tx(vnic, swqe);

	return (EIB_E_SUCCESS);
}

void
eib_mac_ring_fill(void *arg, mac_ring_type_t rtype, const int group_index,
    const int ring_index, mac_ring_info_t *infop, mac_ring_handle_t rh)
{
	eib_t *ss = (eib_t *)arg;
	eib_rx_ring_t *rx_ring;
	mac_intr_t *mintr;

	/*
	 * Currently we only have one RX group and ring
	 */
	if (rtype != MAC_RING_TYPE_RX) {
		EIB_DPRINTF_DEBUG(ss->ei_instance, "eib_mac_fill_ring: "
		    "rtype is %d (not MAC_RING_TYPE_RX!)", rtype);
		return;
	} else if (group_index != 0) {
		EIB_DPRINTF_DEBUG(ss->ei_instance, "eib_mac_fill_ring: "
		    "group index is %d (not 0!)", group_index);
		return;
	} else if (ring_index != 0) {
		EIB_DPRINTF_DEBUG(ss->ei_instance, "eib_mac_fill_ring: "
		    "ring index is %d (not 0!)", ring_index);
		return;
	}

	rx_ring = ss->ei_rx_ring[ring_index];
	rx_ring->rr_hdl = rh;

	infop->mri_driver = (mac_ring_driver_t)rx_ring;
	infop->mri_start = eib_mac_ring_start;
	infop->mri_stop = NULL;
	infop->mri_poll = eib_mac_ring_poll;
	infop->mri_stat = eib_mac_ring_rx_stat;

	/*
	 * Ring level interrupts
	 */
	mintr = &infop->mri_intr;
	mintr->mi_enable = eib_mac_ring_enable_intr;
	mintr->mi_disable = eib_mac_ring_disable_intr;
}

void
eib_mac_group_fill(void *arg, mac_ring_type_t rtype, const int index,
    mac_group_info_t *infop, mac_group_handle_t gh)
{
	eib_t *ss = (eib_t *)arg;

	/*
	 * A single RX ring is what we advertised.  Request for anything
	 * else is unsupported.
	 */
	if (rtype != MAC_RING_TYPE_RX) {
		EIB_DPRINTF_DEBUG(ss->ei_instance, "eib_mac_fill_group: "
		    "rtype is %d (not MAC_RING_TYPE_RX!)", rtype);
		return;
	} else if (index != 0) {
		EIB_DPRINTF_DEBUG(ss->ei_instance, "eib_mac_fill_group: "
		    "index is %d (not 0!)", index);
		return;
	}

	ss->ei_mac_rxgrp_hdl = gh;

	infop->mgi_driver = (mac_group_driver_t)ss;
	infop->mgi_flags = MAC_GROUP_DEFAULT;
	infop->mgi_start = NULL;
	infop->mgi_stop = NULL;
	infop->mgi_addmac = eib_mac_group_addmac;
	infop->mgi_remmac = eib_mac_group_remmac;
	infop->mgi_count = 1;
}

int
eib_mac_hca_portstate(eib_t *ss, ib_lid_t *blid, int *err)
{
	ibt_hca_portinfo_t *pi;
	ibt_status_t ret;
	uint_t num_pi;
	uint_t sz_pi;

	ret = ibt_query_hca_ports(ss->ei_hca_hdl, ss->ei_props->ep_port_num,
	    &pi, &num_pi, &sz_pi);
	if (ret != IBT_SUCCESS) {
		EIB_DPRINTF_ERR(ss->ei_instance,
		    "eib_mac_hca_portstate: ibt_query_hca_ports(hca_hdl=0x%llx,"
		    " port=0x%x) failed, ret=%d", ss->ei_hca_hdl,
		    ss->ei_props->ep_port_num, ret);
		goto mac_hca_portstate_fail;
	}
	if (num_pi != 1) {
		EIB_DPRINTF_ERR(ss->ei_instance,
		    "eib_mac_hca_portstate: ibt_query_hca_ports(hca_hdl=0x%llx,"
		    " port=0x%x) returned num_pi=%d", ss->ei_hca_hdl,
		    ss->ei_props->ep_port_num, num_pi);
		goto mac_hca_portstate_fail;
	}

	if (pi->p_linkstate != IBT_PORT_ACTIVE)
		goto mac_hca_portstate_fail;

	/*
	 * Return the port's base lid if asked
	 */
	if (blid) {
		*blid = pi->p_base_lid;
	}

	ibt_free_portinfo(pi, sz_pi);
	return (EIB_E_SUCCESS);

mac_hca_portstate_fail:
	if (pi) {
		ibt_free_portinfo(pi, sz_pi);
	}
	if (err) {
		*err = ENETDOWN;
	}
	return (EIB_E_FAILURE);
}

int
eib_mac_cleanup_hca_resources(eib_t *ss)
{
	if (ss->ei_rx) {
		if (ss->ei_rx->wp_num_with_nw)
			return (EIB_E_FAILURE);
	}

	if (ss->ei_ctladm_srq || ss->ei_data_srq)
		eib_rb_ibt_setup_srqs(ss);

	if (ss->ei_tx || ss->ei_rx || ss->ei_lso)
		eib_rb_rsrc_setup_bufs(ss);

	if (ss->ei_hca_hdl)
		eib_rb_ibt_hca_init(ss, ~0);

	ss->ei_props->ep_blid = 0;

	return (EIB_E_SUCCESS);
}

static mblk_t  *
eib_mac_ring_poll(void *arg, int bytes_to_pickup, int pkts_to_pickup)
{
	eib_rx_ring_t *rx_ring = (eib_rx_ring_t *)arg;
	mblk_t *mp_chain;
	mblk_t *prev_mp;
	mblk_t *mp;
	ptrdiff_t nbytes = 0;
	int npkts = 0;

	mutex_enter(&rx_ring->rr_lock);

	mp_chain = rx_ring->rr_mp;
	prev_mp = NULL;

	for (mp = rx_ring->rr_mp; mp; mp = mp->b_next) {
		if (nbytes >= bytes_to_pickup || npkts >= pkts_to_pickup)
			break;

		nbytes += ((uintptr_t)mp->b_wptr - (uintptr_t)mp->b_rptr);
		npkts++;
		prev_mp = mp;
	}

	/*
	 * If we picked up anything, terminate the returning chain
	 */
	if (nbytes)
		prev_mp->b_next = NULL;

	/*
	 * Update the head and tail of the remaining chain in the
	 * rx ring with us.
	 */
	rx_ring->rr_mp = mp;
	if (mp == NULL)
		rx_ring->rr_mp_tail = NULL;

	mutex_exit(&rx_ring->rr_lock);

	return (mp_chain);
}

static int
eib_mac_ring_start(mac_ring_driver_t rh, uint64_t mr_gen_num)
{
	eib_rx_ring_t *rx_ring = (eib_rx_ring_t *)rh;

	mutex_enter(&rx_ring->rr_lock);
	rx_ring->rr_gen_num = mr_gen_num;
	mutex_exit(&rx_ring->rr_lock);

	return (0);
}

static int
eib_mac_ring_rx_stat(mac_ring_driver_t rh, uint_t stat, uint64_t *val)
{
	eib_rx_ring_t *rx_ring = (eib_rx_ring_t *)rh;
	eib_stats_t *stats;

	if (rx_ring == NULL || rx_ring->rr_ss == NULL)
		return (ENOTSUP);

	/*
	 * We don't have per-ring statistics at the moment; since
	 * we only support one rx ring anyway, returning the
	 * per-device-instance statistics is accurate enough.
	 */
	if ((stats = rx_ring->rr_ss->ei_stats) == NULL)
		return (ENOTSUP);

	switch (stat) {
	case MAC_STAT_RBYTES:
		*val = stats->st_rbytes;
		break;

	case MAC_STAT_IPACKETS:
		*val = stats->st_ipkts;
		break;

	case MAC_STAT_IERRORS:
		*val = stats->st_ierrors;
		break;

	default:
		return (ENOTSUP);
	}

	return (0);
}

static int
eib_mac_ring_enable_intr(mac_ring_driver_t arg)
{
	eib_rx_ring_t *rx_ring = (eib_rx_ring_t *)arg;
	eib_vnic_t *vnic0 = rx_ring->rr_ss->ei_vnic[0];
	uint_t old_poll_state;

	mutex_enter(&rx_ring->rr_lock);
	old_poll_state = rx_ring->rr_polling;
	rx_ring->rr_polling = 0;
	mutex_exit(&rx_ring->rr_lock);

	/*
	 * Since we only have one rx ring and since all data vnics queue
	 * to the same rx ring, for now we simply trigger an interrupt on
	 * the completion queue for vnic0 (which should be the last one to
	 * go away and therefore be present if this routine is being called).
	 * Eventually, when we implement multiple rx rings, we'll need to
	 * maintain the set of vnic/cqs associated with each rx ring in the
	 * ring structure and trigger an interrupt only for a cq from that
	 * list.
	 */
	if ((old_poll_state == 1) && (vnic0 != NULL))
		(void) ddi_intr_trigger_softint(vnic0->vn_data_rx_si_hdl, NULL);

	return (0);
}

static int
eib_mac_ring_disable_intr(mac_ring_driver_t arg)
{
	eib_rx_ring_t *rx_ring = (eib_rx_ring_t *)arg;

	/*
	 * When we implement multiple rx rings properly, we'll have to truly
	 * disable intr by scanning all cqs associated with this rx ring.
	 * For now, we simply set the polling flag for this rx ring.
	 */
	mutex_enter(&rx_ring->rr_lock);
	rx_ring->rr_polling = 1;
	mutex_exit(&rx_ring->rr_lock);

	return (0);
}

static int
eib_mac_group_addmac(void *arg, const uint8_t *mac_addr, uint64_t flags)
{
	_NOTE(ARGUNUSED(flags))

	eib_t *ss = (eib_t *)arg;

	if (mac_addr) {
		EIB_DPRINTF_DEBUG(ss->ei_instance, "eib_mac_group_ADDMAC: "
		    "%02x:%02x:%02x:%02x:%02x:%02x", mac_addr[0], mac_addr[1],
		    mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
	}

	return (0);
}

static int
eib_mac_group_remmac(void *arg, const uint8_t *mac_addr)
{
	eib_t *ss = (eib_t *)arg;

	if (mac_addr) {
		EIB_DPRINTF_DEBUG(ss->ei_instance, "eib_mac_group_REMMAC: "
		    "%02x:%02x:%02x:%02x:%02x:%02x", mac_addr[0], mac_addr[1],
		    mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);

		/*
		 * We don't have vlan information, so pass a dummy value.
		 * The vnic delete functions will ignore the vlan info and
		 * remove all non-primary vnics with the same mac address.
		 * The primary vnic (vnic0) is expected to get removed via
		 * the m_stop() callback from the mac layer.
		 */
		eib_vnic_do_delete(ss, (uint8_t *)mac_addr, 0);
	}

	return (0);
}

static void
eib_mac_flush_rx_rings(eib_t *ss)
{
	eib_rx_ring_t *rx_ring;
	mblk_t *head;
	mblk_t *mp;
	int i;

	for (i = 0; i < EIB_NUM_RX_RINGS; i++) {
		if ((rx_ring = ss->ei_rx_ring[i]) != NULL) {
			mutex_enter(&rx_ring->rr_lock);
			head = rx_ring->rr_mp;
			rx_ring->rr_mp = NULL;
			rx_ring->rr_mp_tail = NULL;
			mutex_exit(&rx_ring->rr_lock);

			for (mp = head; mp; mp = mp->b_next)
				freemsg(mp);
		}
	}
}

static void
eib_rb_mac_start(eib_t *ss, eib_vnic_t *vnic0)
{
	int ntries;

	/*
	 * If vnic0 is non-null, delete it
	 */
	if (vnic0) {
		eib_rb_vnic_create(ss, vnic0, ~0);
	}

	/*
	 * Tear down the rest of it
	 */
	if (ss->ei_admin_chan) {
		eib_rb_adm_setup_qp(ss);
	}

	/*
	 * Release any mblks in the rx ring(s) still waiting to be collected by
	 * the mac layer (should result in update of ei_num_with_nw count).
	 */
	eib_mac_flush_rx_rings(ss);

	/*
	 * Try to release the SRQs and the buffers.  But first, we need
	 * to see if the network layer has been holding onto our rx buffers.
	 * If so, we wait a reasonable time for it to hand them back to
	 * us.  If we don't get it still, we have nothing to do but avoid
	 * rolling back hca init since we cannot unregister the memory,
	 * release the pd or close the hca.
	 */
	for (ntries = 0; ntries < EIB_MAX_ATTEMPTS; ntries++) {
		if (eib_mac_cleanup_hca_resources(ss) == EIB_E_SUCCESS)
			break;

		delay(drv_usectohz(EIB_DELAY_HALF_SECOND));
	}
	if (ntries == EIB_MAX_ATTEMPTS) {
		EIB_DPRINTF_WARN(ss->ei_instance, "eib_rb_mac_start: "
		    "bufs still with nw, tx=0x%llx, rx=0x%llx, lso=0x%llx",
		    ss->ei_tx, ss->ei_rx, ss->ei_lso);
	}

	/*
	 * Pending vnic creation requests (and failed-vnic records) will have
	 * to be cleaned up in any case
	 */
	eib_flush_vnic_reqs(ss);
}
