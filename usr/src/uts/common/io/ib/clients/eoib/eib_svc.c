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
#include <sys/callb.h>

#include <sys/ib/clients/eoib/eib_impl.h>

/*
 * Thread to handle EoIB events asynchronously
 */
void
eib_events_handler(eib_t *ss)
{
	eib_event_t *evi;
	eib_event_t *nxt;
	kmutex_t ci_lock;
	callb_cpr_t ci;

	mutex_init(&ci_lock, NULL, MUTEX_DRIVER, NULL);
	CALLB_CPR_INIT(&ci, &ci_lock, callb_generic_cpr, EIB_EVENTS_HDLR);

wait_for_event:
	mutex_enter(&ss->ei_ev_lock);
	while ((evi = ss->ei_event) == NULL) {
		mutex_enter(&ci_lock);
		CALLB_CPR_SAFE_BEGIN(&ci);
		mutex_exit(&ci_lock);

		cv_wait(&ss->ei_ev_cv, &ss->ei_ev_lock);

		mutex_enter(&ci_lock);
		CALLB_CPR_SAFE_END(&ci, &ci_lock);
		mutex_exit(&ci_lock);
	}

	/*
	 * Are we being asked to die ?
	 */
	if (evi->ev_code == EIB_EV_SHUTDOWN) {
		while (evi) {
			nxt = evi->ev_next;
			kmem_free(evi, sizeof (eib_event_t));
			evi = nxt;
		}
		ss->ei_event = NULL;
		mutex_exit(&ss->ei_ev_lock);

		mutex_enter(&ci_lock);
		CALLB_CPR_EXIT(&ci);
		mutex_destroy(&ci_lock);

		return;
	}

	/*
	 * Otherwise, pull out the first entry from our work queue
	 */
	ss->ei_event = evi->ev_next;
	evi->ev_next = NULL;

	mutex_exit(&ss->ei_ev_lock);

	/*
	 * Process this event
	 *
	 * Note that we don't want to race with plumb/unplumb in this
	 * handler, since we may have to restart vnics or do stuff that
	 * may get re-initialized or released if we allowed plumb/unplumb
	 * to happen in parallel.
	 *
	 * Note also that while eoib does not hold any HCA resources when
	 * it is unplumbed (i.e. nic not started) and therefore should not
	 * receive any port events, there may be past port events queued in
	 * our work list.  We need to ignore these (port down, port up, pkey
	 * change, sgid change, client rereg) if the nic is not started or
	 * we'll end up dereferencing hca handle (in eib_ibt_link_mod())
	 * that doesn't exist.
	 */
	eib_mac_set_nic_state(ss, EIB_NIC_RESTARTING);

	switch (evi->ev_code) {
	case EIB_EV_PORT_DOWN:
		EIB_DPRINTF_DEBUG(ss->ei_instance,
		    "eib_events_handler: Begin EIB_EV_PORT_DOWN");

		if (EIB_STARTED(ss))
			eib_mac_link_down(ss, B_FALSE);

		EIB_DPRINTF_DEBUG(ss->ei_instance,
		    "eib_events_handler: End EIB_EV_PORT_DOWN");
		break;

	case EIB_EV_PORT_UP:
		EIB_DPRINTF_DEBUG(ss->ei_instance,
		    "eib_events_handler: Begin EIB_EV_PORT_UP");

		if (EIB_STARTED(ss))
			eib_ibt_link_mod(ss);

		EIB_DPRINTF_DEBUG(ss->ei_instance,
		    "eib_events_handler: End EIB_EV_PORT_UP");
		break;

	case EIB_EV_PKEY_CHANGE:
		EIB_DPRINTF_DEBUG(ss->ei_instance,
		    "eib_events_handler: Begin EIB_EV_PKEY_CHANGE");

		if (EIB_STARTED(ss))
			eib_ibt_link_mod(ss);

		EIB_DPRINTF_DEBUG(ss->ei_instance,
		    "eib_events_handler: End EIB_EV_PKEY_CHANGE");
		break;

	case EIB_EV_SGID_CHANGE:
		EIB_DPRINTF_DEBUG(ss->ei_instance,
		    "eib_events_handler: Begin EIB_EV_SGID_CHANGE");

		if (EIB_STARTED(ss))
			eib_ibt_link_mod(ss);

		EIB_DPRINTF_DEBUG(ss->ei_instance,
		    "eib_events_handler: End EIB_EV_SGID_CHANGE");
		break;

	case EIB_EV_CLNT_REREG:
		EIB_DPRINTF_DEBUG(ss->ei_instance,
		    "eib_events_handler: Begin EIB_EV_CLNT_REREG");

		if (EIB_STARTED(ss))
			eib_ibt_link_mod(ss);

		EIB_DPRINTF_DEBUG(ss->ei_instance,
		    "eib_events_handler: End EIB_EV_CLNT_REREG");
		break;

	case EIB_EV_GW_UP:
		/*
		 * EoIB nexus has notified us that our gateway is now
		 * reachable. Unless we already think it is reachable,
		 * mark it so in our records and try to resurrect dead
		 * vnics.
		 */
		mutex_enter(&ss->ei_ka_lock);
		if (ss->ei_ka_gw_unreachable == B_FALSE) {
			mutex_exit(&ss->ei_ka_lock);
			break;
		}
		ss->ei_ka_gw_unreachable = B_FALSE;
		mutex_exit(&ss->ei_ka_lock);

		/*
		 * If we've not even started yet, we have nothing more to do.
		 */
		if (!EIB_STARTED(ss))
			break;

		if (eib_mac_hca_portstate(ss, NULL, NULL) != EIB_E_SUCCESS) {
			EIB_DPRINTF_DEBUG(ss->ei_instance,
			    "eib_events_handler: "
			    "HCA portstate failed, marking link down");

			eib_mac_link_down(ss, B_FALSE);
		} else {
			uint8_t vn0_mac[ETHERADDRL];

			EIB_DPRINTF_DEBUG(ss->ei_instance,
			    "eib_events_handler: "
			    "HCA portstate ok, resurrecting zombies");

			bcopy(eib_zero_mac, vn0_mac, ETHERADDRL);
			eib_vnic_resurrect_zombies(ss, vn0_mac);

			/*
			 * If we've resurrected the zombies because the gateway
			 * went down and came back, it is possible our unicast
			 * mac address changed from what it was earlier. If
			 * so, we need to update our unicast address with the
			 * mac layer before marking the link up.
			 */
			if (bcmp(vn0_mac, eib_zero_mac, ETHERADDRL) != 0) {
				EIB_DPRINTF_DEBUG(ss->ei_instance,
				    "eib_events_handler: updating unicast "
				    "addr to %x:%x:%x:%x:%x:%x", vn0_mac[0],
				    vn0_mac[1], vn0_mac[2], vn0_mac[3],
				    vn0_mac[4], vn0_mac[5]);

				mac_unicst_update(ss->ei_mac_hdl, vn0_mac);
			}

			EIB_DPRINTF_DEBUG(ss->ei_instance,
			    "eib_events_handler: eib_mac_link_up(B_FALSE)");

			eib_mac_link_up(ss, B_FALSE);
		}
		break;

	case EIB_EV_GW_INFO_UPDATE:
		EIB_DPRINTF_DEBUG(ss->ei_instance,
		    "eib_events_handler: Begin EIB_EV_GW_INFO_UPDATE");

		if (evi->ev_arg) {
			eib_update_props(ss, (eib_gw_info_t *)(evi->ev_arg));
			kmem_free(evi->ev_arg, sizeof (eib_gw_info_t));
		}

		EIB_DPRINTF_DEBUG(ss->ei_instance,
		    "eib_events_handler: End EIB_EV_GW_INFO_UPDATE");
		break;

	case EIB_EV_MCG_DELETED:
		EIB_DPRINTF_DEBUG(ss->ei_instance,
		    "eib_events_handler: Begin-End EIB_EV_MCG_DELETED");
		break;

	case EIB_EV_MCG_CREATED:
		EIB_DPRINTF_DEBUG(ss->ei_instance,
		    "eib_events_handler: Begin-End EIB_EV_MCG_CREATED");
		break;

	case EIB_EV_GW_EPORT_DOWN:
		EIB_DPRINTF_DEBUG(ss->ei_instance,
		    "eib_events_handler: Begin-End EIB_EV_GW_EPORT_DOWN");
		break;

	case EIB_EV_GW_DOWN:
		EIB_DPRINTF_DEBUG(ss->ei_instance,
		    "eib_events_handler: Begin-End EIB_EV_GW_DOWN");
		break;
	}

	eib_mac_clr_nic_state(ss, EIB_NIC_RESTARTING);

	kmem_free(evi, sizeof (eib_event_t));
	goto wait_for_event;

	/*NOTREACHED*/
}

void
eib_svc_enqueue_event(eib_t *ss, eib_event_t *evi)
{
	eib_event_t *elem = NULL;
	eib_event_t *tail = NULL;

	mutex_enter(&ss->ei_ev_lock);

	/*
	 * Notice to shutdown has a higher priority than the
	 * rest and goes to the head of the list. Everything
	 * else goes at the end.
	 */
	if (evi->ev_code == EIB_EV_SHUTDOWN) {
		evi->ev_next = ss->ei_event;
		ss->ei_event = evi;
	} else {
		for (elem = ss->ei_event; elem; elem = elem->ev_next)
			tail = elem;

		if (tail)
			tail->ev_next = evi;
		else
			ss->ei_event = evi;
	}

	cv_signal(&ss->ei_ev_cv);
	mutex_exit(&ss->ei_ev_lock);
}

/*
 * Thread to create or restart vnics when required
 */
void
eib_vnic_manager(eib_t *ss)
{
	eib_vnic_req_t *vrq;
	eib_vnic_req_t *elem;
	eib_vnic_req_t *nxt;
	eib_vnic_t *vnic;
	kmutex_t ci_lock;
	callb_cpr_t ci;
	uint_t vr_req;
	uint8_t *vr_mac;
	int ndx;
	int ret;
	int err;

	mutex_init(&ci_lock, NULL, MUTEX_DRIVER, NULL);
	CALLB_CPR_INIT(&ci, &ci_lock, callb_generic_cpr, EIB_VNIC_MANAGER);

wait_for_vnic_req:
	mutex_enter(&ss->ei_vnic_req_lock);

	while ((vrq = ss->ei_vnic_req) == NULL) {
		mutex_enter(&ci_lock);
		CALLB_CPR_SAFE_BEGIN(&ci);
		mutex_exit(&ci_lock);

		cv_wait(&ss->ei_vnic_req_cv, &ss->ei_vnic_req_lock);

		mutex_enter(&ci_lock);
		CALLB_CPR_SAFE_END(&ci, &ci_lock);
		mutex_exit(&ci_lock);
	}

	/*
	 * Pull out the first request
	 */
	ss->ei_vnic_req = vrq->vr_next;
	vrq->vr_next = NULL;

	vr_req = vrq->vr_req;
	vr_mac = vrq->vr_mac;

	switch (vr_req) {
	case EIB_CR_REQ_DIE:
	case EIB_CR_REQ_FLUSH:
		/*
		 * Cleanup all pending reqs and failed reqs
		 */
		for (elem = ss->ei_vnic_req; elem; elem = nxt) {
			nxt = elem->vr_next;
			kmem_free(elem, sizeof (eib_vnic_req_t));
		}
		for (elem = ss->ei_failed_vnic_req; elem; elem = nxt) {
			nxt = elem->vr_next;
			kmem_free(elem, sizeof (eib_vnic_req_t));
		}
		ss->ei_vnic_req = NULL;
		ss->ei_failed_vnic_req = NULL;
		ss->ei_pending_vnic_req = NULL;
		mutex_exit(&ss->ei_vnic_req_lock);

		break;

	case EIB_CR_REQ_DEL_VNIC:
		mutex_exit(&ss->ei_vnic_req_lock);

		EIB_DPRINTF_DEBUG(ss->ei_instance, "eib_vnic_manager: "
		    "vnic deletion request for %x:%x:%x:%x:%x:%x, 0x%x",
		    vr_mac[0], vr_mac[1], vr_mac[2], vr_mac[3], vr_mac[4],
		    vr_mac[5], vrq->vr_vlan);

		/*
		 * Make sure we don't race with the plumb/unplumb code. If
		 * we're already unplumbed, there's no vnic to delete.
		 */
		eib_mac_set_nic_state(ss, EIB_NIC_STOPPING);
		if (!EIB_STARTED(ss)) {
			eib_mac_clr_nic_state(ss, EIB_NIC_STOPPING);
			break;
		}

		/*
		 * Scan through all non-primary vnics and remove the ones
		 * with the given mac address.  Given that the vnic creation/
		 * deletion operation is effectively single-threaded, we
		 * can safely scan ei_vnic[] without any locks.
		 */
		for (ndx = 1; ndx < EIB_MAX_VNICS; ndx++) {
			if ((vnic = ss->ei_vnic[ndx]) == NULL)
				continue;

			/*
			 * Ignore vlan matching for now
			 */
			if (bcmp(vr_mac, vnic->vn_login_data.ld_assigned_mac,
			    ETHERADDRL) == 0) {
				eib_vnic_delete(ss, vnic);
			}
		}
		eib_mac_clr_nic_state(ss, EIB_NIC_STOPPING);
		break;

	case EIB_CR_REQ_NEW_VNIC:
		ss->ei_pending_vnic_req = vrq;
		mutex_exit(&ss->ei_vnic_req_lock);

		EIB_DPRINTF_DEBUG(ss->ei_instance, "eib_vnic_manager: "
		    "new vnic creation request for %x:%x:%x:%x:%x:%x, 0x%x",
		    vr_mac[0], vr_mac[1], vr_mac[2], vr_mac[3], vr_mac[4],
		    vr_mac[5], vrq->vr_vlan);

		/*
		 * Make sure we don't race with the plumb/unplumb code.  If
		 * the eoib instance has been unplumbed already, we ignore any
		 * creation requests that may have been pending.
		 */
		eib_mac_set_nic_state(ss, EIB_NIC_STARTING);

		if (!EIB_STARTED(ss)) {
			mutex_enter(&ss->ei_vnic_req_lock);
			ss->ei_pending_vnic_req = NULL;
			mutex_exit(&ss->ei_vnic_req_lock);
			eib_mac_clr_nic_state(ss, EIB_NIC_STARTING);
			break;
		}

		/*
		 * Try to create a new vnic with the supplied parameters.
		 */
		err = 0;
		if ((ret = eib_vnic_create(ss, vrq->vr_mac, vrq->vr_vlan,
		    NULL, &err)) != EIB_E_SUCCESS) {
			EIB_DPRINTF_WARN(ss->ei_instance, "eib_vnic_manager: "
			    "eib_vnic_create(mac=%x:%x:%x:%x:%x:%x, vlan=0x%x) "
			    "failed, ret=%d", vr_mac[0], vr_mac[1], vr_mac[2],
			    vr_mac[3], vr_mac[4], vr_mac[5], vrq->vr_vlan, err);
		}

		/*
		 * If we failed, add this vnic req to our failed list (unless
		 * it already exists there), so we won't try to create this
		 * vnic again.  Whether we fail or succeed, we're done with
		 * processing this req, so clear the pending req.
		 */
		mutex_enter(&ss->ei_vnic_req_lock);
		if ((ret != EIB_E_SUCCESS) && (err != EEXIST)) {
			vrq->vr_next = ss->ei_failed_vnic_req;
			ss->ei_failed_vnic_req = vrq;
			vrq = NULL;
		}
		ss->ei_pending_vnic_req = NULL;
		mutex_exit(&ss->ei_vnic_req_lock);

		/*
		 * Notify the mac layer that it should retry its tx again. If we
		 * had created the vnic successfully, we'll be able to send the
		 * packets; if we had not been successful, we'll drop packets on
		 * this vnic.
		 */
		EIB_DPRINTF_DEBUG(ss->ei_instance,
		    "eib_vnic_manager: calling mac_tx_update()");
		mac_tx_update(ss->ei_mac_hdl);

		eib_mac_clr_nic_state(ss, EIB_NIC_STARTING);
		break;

	default:
		EIB_DPRINTF_DEBUG(ss->ei_instance, "eib_vnic_manager: "
		    "unknown request 0x%lx, ignoring", vrq->vr_req);
		break;
	}

	/*
	 * Free the current req and quit if we have to
	 */
	if (vrq) {
		kmem_free(vrq, sizeof (eib_vnic_req_t));
	}

	if (vr_req == EIB_CR_REQ_DIE) {
		mutex_enter(&ci_lock);
		CALLB_CPR_EXIT(&ci);
		mutex_destroy(&ci_lock);

		return;
	}

	goto wait_for_vnic_req;
	/*NOTREACHED*/
}

/*
 * Thread to monitor tx wqes and update the mac layer when needed.
 * Note that this thread can only be started after the tx wqe pool
 * has been allocated and initialized.
 */
void
eib_monitor_tx_wqes(eib_t *ss)
{
	eib_wqe_pool_t *wp = ss->ei_tx;
	kmutex_t ci_lock;
	callb_cpr_t ci;

	mutex_init(&ci_lock, NULL, MUTEX_DRIVER, NULL);
	CALLB_CPR_INIT(&ci, &ci_lock, callb_generic_cpr, EIB_TXWQES_MONITOR);

	ASSERT(wp != NULL);

monitor_wqe_status:
	mutex_enter(&wp->wp_lock);

	/*
	 * Wait till someone falls short of wqes
	 */
	while (wp->wp_status == 0) {
		mutex_enter(&ci_lock);
		CALLB_CPR_SAFE_BEGIN(&ci);
		mutex_exit(&ci_lock);

		cv_wait(&wp->wp_cv, &wp->wp_lock);

		mutex_enter(&ci_lock);
		CALLB_CPR_SAFE_END(&ci, &ci_lock);
		mutex_exit(&ci_lock);
	}

	/*
	 * Have we been asked to die ?
	 */
	if (wp->wp_status & EIB_TXWQE_MONITOR_DIE) {
		mutex_exit(&wp->wp_lock);

		mutex_enter(&ci_lock);
		CALLB_CPR_EXIT(&ci);
		mutex_destroy(&ci_lock);

		return;
	}

	ASSERT((wp->wp_status & EIB_TXWQE_SHORT) != 0);

	/*
	 * Start monitoring free wqes till they cross min threshold
	 */
	while ((wp->wp_nfree < EIB_NFREE_SWQES_HWM) &&
	    ((wp->wp_status & EIB_TXWQE_MONITOR_DIE) == 0)) {

		mutex_enter(&ci_lock);
		CALLB_CPR_SAFE_BEGIN(&ci);
		mutex_exit(&ci_lock);

		cv_wait(&wp->wp_cv, &wp->wp_lock);

		mutex_enter(&ci_lock);
		CALLB_CPR_SAFE_END(&ci, &ci_lock);
		mutex_exit(&ci_lock);
	}

	/*
	 * Have we been asked to die ?
	 */
	if (wp->wp_status & EIB_TXWQE_MONITOR_DIE) {
		mutex_exit(&wp->wp_lock);

		mutex_enter(&ci_lock);
		CALLB_CPR_EXIT(&ci);
		mutex_destroy(&ci_lock);

		return;
	}

	ASSERT(wp->wp_nfree >= EIB_NFREE_SWQES_HWM);
	wp->wp_status &= (~EIB_TXWQE_SHORT);

	mutex_exit(&wp->wp_lock);

	/*
	 * Inform the mac layer that tx resources are now available
	 * and go back to monitoring
	 */
	if (ss->ei_mac_hdl) {
		mac_tx_update(ss->ei_mac_hdl);
	}
	goto monitor_wqe_status;

	/*NOTREACHED*/
}

/*
 * Thread to monitor lso bufs and update the mac layer as needed.
 * Note that this thread can only be started after the lso buckets
 * have been allocated and initialized.
 */
void
eib_monitor_lso_bufs(eib_t *ss)
{
	eib_lsobkt_t *bkt = ss->ei_lso;
	kmutex_t ci_lock;
	callb_cpr_t ci;

	mutex_init(&ci_lock, NULL, MUTEX_DRIVER, NULL);
	CALLB_CPR_INIT(&ci, &ci_lock, callb_generic_cpr, EIB_LSOBUFS_MONITOR);

	ASSERT(bkt != NULL);

monitor_lso_status:
	mutex_enter(&bkt->bk_lock);

	/*
	 * Wait till someone falls short of LSO buffers or we're asked
	 * to die
	 */
	while (bkt->bk_status == 0) {
		mutex_enter(&ci_lock);
		CALLB_CPR_SAFE_BEGIN(&ci);
		mutex_exit(&ci_lock);

		cv_wait(&bkt->bk_cv, &bkt->bk_lock);

		mutex_enter(&ci_lock);
		CALLB_CPR_SAFE_END(&ci, &ci_lock);
		mutex_exit(&ci_lock);
	}

	if (bkt->bk_status & EIB_LBUF_MONITOR_DIE) {
		mutex_exit(&bkt->bk_lock);

		mutex_enter(&ci_lock);
		CALLB_CPR_EXIT(&ci);
		mutex_destroy(&ci_lock);

		return;
	}

	ASSERT((bkt->bk_status & EIB_LBUF_SHORT) != 0);

	/*
	 * Start monitoring free LSO buffers till there are enough
	 * free buffers available
	 */
	while ((bkt->bk_nfree < EIB_LSO_FREE_BUFS_THRESH) &&
	    ((bkt->bk_status & EIB_LBUF_MONITOR_DIE) == 0)) {

		mutex_enter(&ci_lock);
		CALLB_CPR_SAFE_BEGIN(&ci);
		mutex_exit(&ci_lock);

		cv_wait(&bkt->bk_cv, &bkt->bk_lock);

		mutex_enter(&ci_lock);
		CALLB_CPR_SAFE_END(&ci, &ci_lock);
		mutex_exit(&ci_lock);
	}

	if (bkt->bk_status & EIB_LBUF_MONITOR_DIE) {
		mutex_exit(&bkt->bk_lock);

		mutex_enter(&ci_lock);
		CALLB_CPR_EXIT(&ci);
		mutex_destroy(&ci_lock);

		return;
	}

	/*
	 * We have enough lso buffers available now
	 */
	ASSERT(bkt->bk_nfree >= EIB_LSO_FREE_BUFS_THRESH);
	bkt->bk_status &= (~EIB_LBUF_SHORT);

	mutex_exit(&bkt->bk_lock);

	/*
	 * Inform the mac layer that tx lso resources are now available
	 * and go back to monitoring
	 */
	if (ss->ei_mac_hdl) {
		mac_tx_update(ss->ei_mac_hdl);
	}
	goto monitor_lso_status;

	/*NOTREACHED*/
}

/*
 * Thread to manage the keepalive requirements for vnics and the gateway.
 */
void
eib_manage_keepalives(eib_t *ss)
{
	eib_ka_vnics_t *elem;
	eib_ka_vnics_t *nxt;
	clock_t deadline;
	uint64_t new_zombies;
	int64_t lbolt64;
	int err;
	kmutex_t ci_lock;
	callb_cpr_t ci;

	mutex_init(&ci_lock, NULL, MUTEX_DRIVER, NULL);
	CALLB_CPR_INIT(&ci, &ci_lock, callb_generic_cpr, EIB_KEEPALIVES_HDLR);

	mutex_enter(&ss->ei_ka_lock);

periodic_keepalive:
	deadline = ddi_get_lbolt() + ss->ei_gw_props->pp_vnic_ka_ticks;

	while ((ss->ei_ka_event &
	    (EIB_KA_MANAGER_DIE | EIB_KA_VNICS_TIMED_OUT)) == 0) {
		mutex_enter(&ci_lock);
		CALLB_CPR_SAFE_BEGIN(&ci);
		mutex_exit(&ci_lock);

		if (cv_timedwait(&ss->ei_ka_cv, &ss->ei_ka_lock,
		    deadline) == -1) {
			ss->ei_ka_event |= EIB_KA_VNICS_TIMED_OUT;
		}

		mutex_enter(&ci_lock);
		CALLB_CPR_SAFE_END(&ci, &ci_lock);
		mutex_exit(&ci_lock);
	}

	if (ss->ei_ka_event & EIB_KA_MANAGER_DIE) {
		for (elem = ss->ei_ka_vnics; elem; elem = nxt) {
			nxt = elem->ka_next;
			kmem_free(elem, sizeof (eib_ka_vnics_t));
		}
		ss->ei_ka_vnics = NULL;
		mutex_exit(&ss->ei_ka_lock);

		mutex_enter(&ci_lock);
		CALLB_CPR_EXIT(&ci);
		mutex_destroy(&ci_lock);

		return;
	}

	/*
	 * Are there any vnics that need keepalive management ?
	 */
	ss->ei_ka_event &= ~EIB_KA_VNICS_TIMED_OUT;
	if (ss->ei_ka_vnics == NULL)
		goto periodic_keepalive;

	/*
	 * Ok, we need to send vnic keepalives to our gateway. But first
	 * check if the gateway heartbeat is good as of this moment. If
	 * not, mark the gateway as unreachable, the ka vnics as zombies
	 * and the link as down.
	 */
	lbolt64 = ddi_get_lbolt64();
	if (ss->ei_ka_gw_heartbeat != 0) {
		if ((lbolt64 - ss->ei_ka_gw_heartbeat) >
		    ss->ei_gw_props->pp_gw_ka_ticks) {

			EIB_DPRINTF_WARN(ss->ei_instance,
			    "eib_manage_keepalives: no keepalives from gateway "
			    "0x%x for hca_guid=0x%llx, port=0x%x, "
			    "last_gw_ka=0x%llx", ss->ei_gw_props->pp_gw_portid,
			    ss->ei_props->ep_hca_guid,
			    ss->ei_props->ep_port_num,
			    ss->ei_ka_gw_heartbeat);

			new_zombies = 0;
			for (elem = ss->ei_ka_vnics; elem; elem = nxt) {
				nxt = elem->ka_next;
				new_zombies |=
				    ((uint64_t)1 << elem->ka_vnic->vn_instance);
				kmem_free(elem, sizeof (eib_ka_vnics_t));
			}
			ss->ei_ka_vnics = NULL;
			ss->ei_ka_gw_unreachable = B_TRUE;

			rw_enter(&ss->ei_vnic_lock, RW_WRITER);
			ss->ei_zombie_vnics |= new_zombies;
			rw_exit(&ss->ei_vnic_lock);

			eib_mac_link_down(ss, B_FALSE);

			goto periodic_keepalive;
		}
	}

	for (elem = ss->ei_ka_vnics; elem; elem = elem->ka_next)
		(void) eib_fip_heartbeat(ss, elem->ka_vnic, &err);

	goto periodic_keepalive;
	/*NOTREACHED*/
}

void
eib_stop_events_handler(eib_t *ss)
{
	eib_event_t *evi;

	evi = kmem_zalloc(sizeof (eib_event_t), KM_SLEEP);
	evi->ev_code = EIB_EV_SHUTDOWN;
	evi->ev_arg = NULL;

	eib_svc_enqueue_event(ss, evi);

	thread_join(ss->ei_events_handler);
}

void
eib_stop_vnic_manager(eib_t *ss)
{
	eib_vnic_req_t *vrq;

	vrq = kmem_zalloc(sizeof (eib_vnic_req_t), KM_SLEEP);
	vrq->vr_req = EIB_CR_REQ_DIE;
	vrq->vr_next = NULL;

	eib_vnic_enqueue_req(ss, vrq);

	thread_join(ss->ei_vnic_manager);
}

void
eib_stop_monitor_tx_wqes(eib_t *ss)
{
	eib_wqe_pool_t *wp = ss->ei_tx;

	mutex_enter(&wp->wp_lock);

	wp->wp_status |= EIB_TXWQE_MONITOR_DIE;

	cv_signal(&wp->wp_cv);
	mutex_exit(&wp->wp_lock);

	thread_join(ss->ei_txwqe_monitor);
}

int
eib_stop_monitor_lso_bufs(eib_t *ss)
{
	eib_lsobkt_t *bkt = ss->ei_lso;

	/*
	 * If there are some buffers still not reaped, return
	 * without doing anything. Otherwise, stop the lso bufs
	 * monitor and wait for it to die.
	 */
	mutex_enter(&bkt->bk_lock);
	if (bkt->bk_nelem != bkt->bk_nfree) {
		mutex_exit(&bkt->bk_lock);
		return (EIB_E_FAILURE);
	}
	bkt->bk_status |= EIB_LBUF_MONITOR_DIE;

	cv_signal(&bkt->bk_cv);
	mutex_exit(&bkt->bk_lock);

	thread_join(ss->ei_lsobufs_monitor);
	return (EIB_E_SUCCESS);
}

void
eib_stop_manage_keepalives(eib_t *ss)
{
	mutex_enter(&ss->ei_ka_lock);

	ss->ei_ka_event |= EIB_KA_MANAGER_DIE;

	cv_signal(&ss->ei_ka_cv);
	mutex_exit(&ss->ei_ka_lock);

	thread_join(ss->ei_keepalives_manager);
}

void
eib_flush_vnic_reqs(eib_t *ss)
{
	eib_vnic_req_t *vrq;

	vrq = kmem_zalloc(sizeof (eib_vnic_req_t), KM_SLEEP);
	vrq->vr_req = EIB_CR_REQ_FLUSH;
	vrq->vr_next = NULL;

	eib_vnic_enqueue_req(ss, vrq);
}

/*ARGSUSED*/
void
eib_gw_alive_cb(dev_info_t *dip, ddi_eventcookie_t cookie, void *arg,
    void *impl_data)
{
	eib_t *ss = (eib_t *)arg;
	eib_event_t *evi;

	evi = kmem_zalloc(sizeof (eib_event_t), KM_NOSLEEP);
	if (evi == NULL) {
		EIB_DPRINTF_WARN(ss->ei_instance, "eib_gw_alive_cb: "
		    "no memory, ignoring this gateway alive event");
	} else {
		evi->ev_code = EIB_EV_GW_UP;
		evi->ev_arg = NULL;
		eib_svc_enqueue_event(ss, evi);
	}
}

/*ARGSUSED*/
void
eib_login_ack_cb(dev_info_t *dip, ddi_eventcookie_t cookie, void *arg,
    void *impl_data)
{
	eib_t *ss = (eib_t *)arg;
	uint8_t *pkt = (uint8_t *)impl_data;
	eib_login_data_t ld;

	/*
	 * We have received a login ack message from the gateway via the EoIB
	 * nexus (solicitation qpn).  The packet is passed to us raw (unparsed)
	 * and we have to figure out if this is a vnic login ack.
	 */
	if (eib_fip_parse_login_ack(ss, pkt + EIB_GRH_SZ, &ld) == EIB_E_SUCCESS)
		eib_vnic_login_ack(ss, &ld);
}

/*ARGSUSED*/
void
eib_gw_info_cb(dev_info_t *dip, ddi_eventcookie_t cookie, void *arg,
    void *impl_data)
{
	eib_t *ss = (eib_t *)arg;
	eib_event_t *evi;

	evi = kmem_zalloc(sizeof (eib_event_t), KM_NOSLEEP);
	if (evi == NULL) {
		EIB_DPRINTF_WARN(ss->ei_instance, "eib_gw_info_cb: "
		    "no memory, ignoring this gateway props update event");
		return;
	}
	evi->ev_arg = kmem_zalloc(sizeof (eib_gw_info_t), KM_NOSLEEP);
	if (evi->ev_arg == NULL) {
		EIB_DPRINTF_WARN(ss->ei_instance, "eib_gw_info_cb: "
		    "no memory, ignoring this gateway props update event");
		kmem_free(evi, sizeof (eib_event_t));
		return;
	}
	bcopy(impl_data, evi->ev_arg, sizeof (eib_gw_info_t));
	evi->ev_code = EIB_EV_GW_INFO_UPDATE;

	eib_svc_enqueue_event(ss, evi);
}
