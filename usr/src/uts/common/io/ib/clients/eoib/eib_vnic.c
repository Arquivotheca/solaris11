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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/kmem.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/ksynch.h>

#include <sys/ib/clients/eoib/eib_impl.h>

/*
 * Declarations private to this file
 */
static int eib_vnic_get_instance(eib_t *, int *);
static void eib_vnic_ret_instance(eib_t *, int);
static void eib_vnic_modify_enter(eib_t *, uint_t);
static void eib_vnic_modify_exit(eib_t *, uint_t);
static int eib_vnic_create_common(eib_t *, eib_vnic_t *, int *);
static int eib_vnic_set_partition(eib_t *, eib_vnic_t *, int *);
static void eib_vnic_make_vhub_mgid(uint8_t *, uint8_t, uint8_t *, uint8_t,
    uint8_t, uint32_t, ib_gid_t *);
static int eib_vnic_attach_ctl_mcgs(eib_t *, eib_vnic_t *, int *);
static int eib_vnic_attach_vhub_table(eib_t *, eib_vnic_t *);
static int eib_vnic_attach_vhub_update(eib_t *, eib_vnic_t *);
static int eib_vnic_attach_vhub_data(eib_t *, eib_vnic_t *, int *);
static void eib_vnic_reattach_ctl_mcgs(eib_t *, eib_vnic_t *);
static void eib_vnic_reattach_vhub_data(eib_t *, eib_vnic_t *);
static void eib_vnic_start_keepalives(eib_t *, eib_vnic_t *);
static int eib_vnic_lookup_dest(eib_vnic_t *, uint8_t *, uint16_t,
    ibt_adds_vect_t *, ib_qpn_t *, int *);
static void eib_rb_vnic_create_common(eib_t *, eib_vnic_t *, uint_t);
static void eib_rb_vnic_attach_ctl_mcgs(eib_t *, eib_vnic_t *);
static void eib_rb_vnic_attach_vhub_table(eib_t *, eib_vnic_t *);
static void eib_rb_vnic_attach_vhub_update(eib_t *, eib_vnic_t *);
static void eib_rb_vnic_attach_vhub_data(eib_t *, eib_vnic_t *);
static void eib_rb_vnic_join_multicast(eib_t *, eib_vnic_t *, uint8_t *, int);
static void eib_rb_vnic_start_keepalives(eib_t *, eib_vnic_t *);

/*
 * Definitions private to this file
 */
#define	EIB_VNIC_STRUCT_ALLOCD		0x0001
#define	EIB_VNIC_GOT_INSTANCE		0x0002
#define	EIB_VNIC_CREATE_COMMON_DONE	0x0004
#define	EIB_VNIC_CTLQP_CREATED		0x0008
#define	EIB_VNIC_DATAQP_CREATED		0x0010
#define	EIB_VNIC_LOGIN_DONE		0x0020
#define	EIB_VNIC_PARTITION_SET		0x0040
#define	EIB_VNIC_ATTACHED_TO_CTL_MCGS	0x0080
#define	EIB_VNIC_GOT_VHUB_TABLE		0x0100
#define	EIB_VNIC_KEEPALIVES_STARTED	0x0200
#define	EIB_VNIC_ATTACHED_TO_VHUB_DATA	0x0400
#define	EIB_VNIC_BROADCAST_JOINED	0x0800

/*
 * Destination type
 */
#define	EIB_TX_UNICAST			1
#define	EIB_TX_MULTICAST		2
#define	EIB_TX_BROADCAST		3

int
eib_vnic_create(eib_t *ss, uint8_t *macaddr, uint16_t vlan, eib_vnic_t **vnicp,
    int *err)
{
	eib_vnic_t *vnic = NULL;
	boolean_t failed_vnic = B_FALSE;
	uint_t progress = 0;

	eib_vnic_modify_enter(ss, EIB_VN_BEING_CREATED);

	/*
	 * When a previously created vnic is being resurrected due to a
	 * gateway reboot, there's a race possible where a creation request
	 * for the existing vnic could get filed with the vnic manager
	 * thread. So, before we go ahead with the creation of this vnic,
	 * make sure we already don't have the vnic.
	 */
	if (macaddr) {
		if (eib_data_lookup_vnic(ss, macaddr, vlan, vnicp,
		    &failed_vnic) == EIB_E_SUCCESS) {
			EIB_DPRINTF_WARN(ss->ei_instance, "eib_vnic_create: "
			    "vnic for mac=%x:%x:%x:%x:%x:%x, vlan=0x%x "
			    "already there, no duplicate creation", macaddr[0],
			    macaddr[1], macaddr[2], macaddr[3], macaddr[4],
			    macaddr[5], vlan);

			eib_vnic_modify_exit(ss, EIB_VN_BEING_CREATED);
			return (EIB_E_SUCCESS);
		} else if (failed_vnic) {
			EIB_DPRINTF_WARN(ss->ei_instance, "eib_vnic_create: "
			    "vnic for mac=%x:%x:%x:%x:%x:%x, vlan=0x%x "
			    "failed earlier, shouldn't be here at all",
			    macaddr[0], macaddr[1], macaddr[2], macaddr[3],
			    macaddr[4], macaddr[5], vlan);

			*err = EEXIST;

			eib_vnic_modify_exit(ss, EIB_VN_BEING_CREATED);
			return (EIB_E_FAILURE);
		}
	}

	/*
	 * Allocate a vnic structure for this instance
	 */
	vnic = kmem_zalloc(sizeof (eib_vnic_t), KM_SLEEP);
	vnic->vn_ss = ss;
	vnic->vn_instance = -1;
	mutex_init(&vnic->vn_lock, NULL, MUTEX_DRIVER, NULL);
	rw_init(&vnic->vn_vhub_mcgs_lock, NULL, RW_DRIVER, NULL);
	cv_init(&vnic->vn_cv, NULL, CV_DEFAULT, NULL);

	progress |= EIB_VNIC_STRUCT_ALLOCD;

	/*
	 * Get a vnic instance
	 */
	if (eib_vnic_get_instance(ss, &vnic->vn_instance) != EIB_E_SUCCESS) {
		*err = EMFILE;
		goto vnic_create_fail;
	}
	progress |= EIB_VNIC_GOT_INSTANCE;

	/*
	 * Initialize vnic's basic parameters.  Note that we set the 15-bit
	 * vnic id to send to gw during a login to be a 2-tuple of
	 * {devi_instance#, eoib_vnic_instance#}.
	 */
	vnic->vn_vlan = vlan;
	if (macaddr) {
		bcopy(macaddr, vnic->vn_macaddr, sizeof (vnic->vn_macaddr));
	}
	vnic->vn_id = (uint16_t)EIB_VNIC_ID(ss->ei_instance, vnic->vn_instance);

	/*
	 * Start up this vnic instance
	 */
	if (eib_vnic_create_common(ss, vnic, err) != EIB_E_SUCCESS)
		goto vnic_create_fail;

	progress |= EIB_VNIC_CREATE_COMMON_DONE;

	/*
	 * Return the created vnic
	 */
	if (vnicp) {
		*vnicp = vnic;
	}

	eib_vnic_modify_exit(ss, EIB_VN_BEING_CREATED);
	return (EIB_E_SUCCESS);

vnic_create_fail:
	eib_rb_vnic_create(ss, vnic, progress);
	eib_vnic_modify_exit(ss, EIB_VN_BEING_CREATED);
	return (EIB_E_FAILURE);
}

void
eib_vnic_delete(eib_t *ss, eib_vnic_t *vnic)
{
	eib_vnic_modify_enter(ss, EIB_VN_BEING_DELETED);
	eib_rb_vnic_create(ss, vnic, ~0);
	eib_vnic_modify_exit(ss, EIB_VN_BEING_DELETED);
}

/*ARGSUSED*/
int
eib_vnic_wait_for_login_ack(eib_t *ss, eib_vnic_t *vnic, int *err)
{
	clock_t deadline;
	int ret = EIB_E_SUCCESS;

	deadline = ddi_get_lbolt() + drv_usectohz(EIB_LOGIN_TIMEOUT_USEC);

	/*
	 * Wait for login ack/nack or wait time to get over. If we wake up
	 * with a login failure, record the reason.
	 */
	mutex_enter(&vnic->vn_lock);
	while (vnic->vn_state == EIB_LOGIN_ACK_WAIT) {
		if (cv_timedwait(&vnic->vn_cv, &vnic->vn_lock,
		    deadline) == -1) {
			if (vnic->vn_state == EIB_LOGIN_ACK_WAIT)
				vnic->vn_state = EIB_LOGIN_TIMED_OUT;
		}
	}

	if (vnic->vn_state != EIB_LOGIN_ACK_RCVD) {
		ret = EIB_E_FAILURE;
		*err =  (vnic->vn_state == EIB_LOGIN_TIMED_OUT) ?
		    ETIME : ECANCELED;
	}
	mutex_exit(&vnic->vn_lock);

	return (ret);
}

void
eib_vnic_login_ack(eib_t *ss, eib_login_data_t *ld)
{
	eib_vnic_t *vnic;
	uint_t vnic_instance;
	uint_t hdrs_sz;
	uint16_t vnic_id;
	int nack = 1;

	/*
	 * The msb in the vnic id in login ack message is not
	 * part of our vNIC id.
	 */
	vnic_id = ld->ld_vnic_id & (~FIP_VL_VNIC_ID_MSBIT);

	/*
	 * Now, we deconstruct the vnic id and determine the vnic
	 * instance number. If this vnic_instance number isn't
	 * valid or the vnic_id of the vnic for this instance
	 * number doesn't match in our records, we quit.
	 */
	vnic_instance = EIB_VNIC_INSTANCE(vnic_id);
	if (vnic_instance >= EIB_MAX_VNICS)
		return;

	/*
	 * At this point, we haven't fully created the vnic, so
	 * this vnic should be present as ei_vnic_pending.
	 */
	rw_enter(&ss->ei_vnic_lock, RW_READER);
	if ((vnic = ss->ei_vnic_pending) == NULL) {
		rw_exit(&ss->ei_vnic_lock);
		return;
	}
	rw_exit(&ss->ei_vnic_lock);

	/*
	 * First check if the vnic is still sleeping, waiting for login ack.
	 * If not, we might as well quit now.
	 */
	mutex_enter(&vnic->vn_lock);
	if (vnic->vn_id != vnic_id || vnic->vn_state != EIB_LOGIN_ACK_WAIT) {
		mutex_exit(&vnic->vn_lock);
		return;
	}

	/*
	 * We NACK the waiter under these conditions:
	 *
	 * . syndrome was set
	 * . vhub mtu is bigger than our max mtu (minus eoib/eth hdrs sz)
	 * . assigned vlan is different from requested vlan (except
	 *   when we didn't request a specific vlan)
	 * . when the assigned mac is different from the requested mac
	 *   (except when we didn't request a specific mac)
	 * . when the VP bit indicates that vlan tag should be used
	 *   but we had not specified a vlan tag in our request
	 * . when the VP bit indicates that vlan tag should not be
	 *   present and we'd specified a vlan tag in our request
	 *
	 * The last case is interesting: if we had not specified any vlan id
	 * in our request, but the gateway has assigned a vlan and asks us
	 * to use/expect that tag on every packet dealt by this vnic, it
	 * means effectively the EoIB driver has to insert/remove vlan
	 * tagging on this vnic traffic, since the nw layer on Solaris
	 * won't be using/expecting any tag on traffic for this vnic. This
	 * feature is not supported currently.
	 */
	hdrs_sz = EIB_ENCAP_HDR_SZ + sizeof (struct ether_header) + VLAN_TAGSZ;
	if (ld->ld_syndrome) {
		EIB_DPRINTF_WARN(ss->ei_instance, "eib_vnic_login_ack: "
		    "non-zero syndrome 0x%lx, NACK", ld->ld_syndrome);

	} else if (ld->ld_vhub_mtu > (ss->ei_props->ep_mtu - hdrs_sz)) {
		EIB_DPRINTF_WARN(ss->ei_instance, "eib_vnic_login_ack: "
		    "vhub mtu (0x%x) bigger than port mtu (0x%x), NACK",
		    ld->ld_vhub_mtu, ss->ei_props->ep_mtu);

	} else if ((vnic->vn_vlan) && (vnic->vn_vlan != ld->ld_assigned_vlan)) {
		EIB_DPRINTF_WARN(ss->ei_instance, "eib_vnic_login_ack: "
		    "assigned vlan (0x%x) different from asked (0x%x), "
		    "for vnic id 0x%x, NACK", ld->ld_assigned_vlan,
		    vnic->vn_vlan, vnic->vn_id);

	} else if (bcmp(vnic->vn_macaddr, eib_zero_mac, ETHERADDRL) &&
	    bcmp(vnic->vn_macaddr, ld->ld_assigned_mac, ETHERADDRL)) {
		uint8_t *asked, *got;

		asked = vnic->vn_macaddr;
		got = ld->ld_assigned_mac;

		EIB_DPRINTF_WARN(ss->ei_instance, "eib_vnic_login_ack: "
		    "assigned mac (%x:%x:%x:%x:%x:%x) different from "
		    "asked (%x:%x:%x:%x:%x:%x) for vnic id 0x%x, NACK",
		    got[0], got[1], got[2], got[3], got[4], got[5], asked[0],
		    asked[1], asked[2], asked[3], asked[4], asked[5]);

	} else if ((vnic->vn_vlan == 0) && (ld->ld_vlan_in_packets)) {
		EIB_DPRINTF_WARN(ss->ei_instance, "eib_vnic_login_ack: "
		    "asked for tagless vlan, but VP flag is set "
		    "for vnic id 0x%x, NACK", vnic->vn_id);

	} else if ((vnic->vn_vlan) && (!ld->ld_vlan_in_packets)) {
		if (eib_wa_no_good_vp_flag) {
			ld->ld_vlan_in_packets = 1;
			ld->ld_vhub_id = EIB_VHUB_ID(ld->ld_gw_port_id,
			    ld->ld_assigned_vlan);
			nack = 0;
		} else {
			EIB_DPRINTF_WARN(ss->ei_instance, "eib_vnic_login_ack: "
			    "vlan was assigned correctly, but VP flag is not "
			    "set for vnic id 0x%x, NACK", vnic->vn_id);
		}
	} else {
		ld->ld_vhub_id = EIB_VHUB_ID(ld->ld_gw_port_id,
		    ld->ld_assigned_vlan);
		nack = 0;
	}

	/*
	 * ACK/NACK the waiter
	 */
	if (nack) {
		vnic->vn_state = EIB_LOGIN_NACK_RCVD;
	} else {
		bcopy(ld, &vnic->vn_login_data, sizeof (eib_login_data_t));
		vnic->vn_state = EIB_LOGIN_ACK_RCVD;
	}

	cv_signal(&vnic->vn_cv);
	mutex_exit(&vnic->vn_lock);
}

int
eib_vnic_wait_for_table(eib_t *ss, eib_vnic_t *vnic, int *err)
{
	clock_t deadline;
	int ret = EIB_E_SUCCESS;

	/*
	 * The EoIB spec does not detail exactly within what time a vhub table
	 * request is expected to be answered.  However, it does mention that
	 * in the worst case, the vhub update messages from the gateway must
	 * be seen atleast once in 2.5 * GW_KA_PERIOD (already saved in
	 * pp_gw_ka_ticks), so we'll settle for that limit.
	 */
	deadline = ddi_get_lbolt() + ss->ei_gw_props->pp_gw_ka_ticks;

	/*
	 * Wait for vhub table to be constructed. If we wake up with a
	 * vhub table construction failure, record the reason.
	 */
	mutex_enter(&vnic->vn_lock);
	while (vnic->vn_state == EIB_LOGIN_TBL_WAIT) {
		if (cv_timedwait(&vnic->vn_cv, &vnic->vn_lock,
		    deadline) == -1) {
			if (vnic->vn_state == EIB_LOGIN_TBL_WAIT)
				vnic->vn_state = EIB_LOGIN_TIMED_OUT;
		}
	}

	if (vnic->vn_state != EIB_LOGIN_TBL_DONE) {
		ret = EIB_E_FAILURE;
		*err =  (vnic->vn_state == EIB_LOGIN_TIMED_OUT) ?
		    ETIME : ECANCELED;
	}
	mutex_exit(&vnic->vn_lock);

	return (ret);
}

void
eib_vnic_vhub_table_done(eib_vnic_t *vnic, uint_t result_state)
{
	ASSERT(result_state == EIB_LOGIN_TBL_DONE ||
	    result_state == EIB_LOGIN_TBL_FAILED);

	/*
	 * Construction of vhub table for the vnic is done one way or
	 * the other.  Set the login wait state appropriately and signal
	 * the waiter. If it's a vhub table failure, we shouldn't parse
	 * any more vhub table or vhub update packets until the vnic state
	 * is changed.
	 */
	mutex_enter(&vnic->vn_lock);
	vnic->vn_state = result_state;
	cv_signal(&vnic->vn_cv);
	mutex_exit(&vnic->vn_lock);
}

/*ARGSUSED*/
int
eib_vnic_join_multicast(eib_t *ss, eib_vnic_t *vnic, uint8_t *mcast_mac,
    int *err)
{
	eib_login_data_t *ld = &vnic->vn_login_data;
	eib_chan_t *chan = vnic->vn_data_chan;
	ib_gid_t dgid;
	eib_mcg_t *data_mcgs;
	eib_mcg_member_t *mcm;
	eib_mcg_member_t *elem;
	eib_mcg_member_t *tail;
	ibt_mcg_info_t mcginfo;
	ibt_status_t ret;

	/*
	 * Create a new element in the mcg member list and add to it
	 */
	mcm = kmem_zalloc(sizeof (eib_mcg_member_t), KM_NOSLEEP);
	if (mcm == NULL) {
		*err = ENOMEM;
		return (EIB_E_FAILURE);
	}

	/*
	 * Figure out the dgid for this multicast mac address
	 */
	eib_vnic_make_vhub_mgid(ld->ld_gw_mgid_prefix,
	    (uint8_t)EIB_MGID_VHUB_DATA, mcast_mac, ld->ld_n_mac_mcgid, 0,
	    ld->ld_vhub_id, &dgid);

	/*
	 * If it isn't already present in the list of multicast addresses
	 * for this vnic, add it.
	 */

	rw_enter(&vnic->vn_vhub_mcgs_lock, RW_WRITER);
	if ((data_mcgs = vnic->vn_vhub_data_mcgs) == NULL) {
		*err = EINVAL;
		rw_exit(&vnic->vn_vhub_mcgs_lock);
		kmem_free(mcm, sizeof (eib_mcg_member_t));
		return (EIB_E_FAILURE);
	}

	tail = NULL;
	for (elem = data_mcgs->mg_elem; elem; elem = elem->mm_next) {
		tail = elem;
		if ((elem->mm_dgid.gid_prefix == dgid.gid_prefix) &&
		    (elem->mm_dgid.gid_guid == dgid.gid_guid)) {
			rw_exit(&vnic->vn_vhub_mcgs_lock);
			kmem_free(mcm, sizeof (eib_mcg_member_t));
			return (EIB_E_SUCCESS);
		}
	}

	/*
	 * New group to attach to. Check if we have too many mcgs.
	 */
	if (data_mcgs->mg_nelem >= EIB_MAX_MCGS_PER_VNIC) {
		*err = E2BIG;
		EIB_DPRINTF_VERBOSE(ss->ei_instance,
		    "eib_vnic_join_multicast: "
		    "too many mcgs already");
		rw_exit(&vnic->vn_vhub_mcgs_lock);
		kmem_free(mcm, sizeof (eib_mcg_member_t));
		return (EIB_E_FAILURE);
	}

	/*
	 * This group hasn't been attached to our qp yet. Do that while
	 * making sure that the MLID is set to the LID of zero-mac mcg.
	 */
	bcopy(data_mcgs->mg_mcginfo, &mcginfo, sizeof (ibt_mcg_info_t));
	mcginfo.mc_adds_vect.av_dgid = dgid;

	ret = ibt_attach_mcg(chan->ch_chan, &mcginfo);
	if (ret != IBT_SUCCESS) {
		EIB_DPRINTF_VERBOSE(ss->ei_instance,
		    "eib_vnic_join_multicast: "
		    "ibt_attach_mcg(mgid=%llx.%llx, mlid=0x%x) "
		    "failed, ret=%d", mcginfo.mc_adds_vect.av_dgid,
		    mcginfo.mc_adds_vect.av_dlid, ret);
		*err = EINVAL;
		rw_exit(&vnic->vn_vhub_mcgs_lock);
		kmem_free(mcm, sizeof (eib_mcg_member_t));
		return (EIB_E_FAILURE);
	}

	bcopy(mcast_mac, mcm->mm_mac, ETHERADDRL);
	mcm->mm_dgid = dgid;
	mcm->mm_next = NULL;

	if (tail)
		tail->mm_next = mcm;
	else
		data_mcgs->mg_elem = mcm;

	data_mcgs->mg_nelem++;

	rw_exit(&vnic->vn_vhub_mcgs_lock);

	return (EIB_E_SUCCESS);
}

void
eib_vnic_leave_multicast(eib_t *ss, eib_vnic_t *vnic, uint8_t *mcast_mac)
{
	eib_rb_vnic_join_multicast(ss, vnic, mcast_mac, EIB_FLG_LEAVE_ONE_MCG);
}

int
eib_vnic_setup_dest(eib_vnic_t *vnic, eib_wqe_t *swqe, uint8_t *dmac,
    uint16_t vlan)
{
	eib_t *ss = vnic->vn_ss;
	eib_stats_t *stats = ss->ei_stats;
	ibt_status_t ret;
	ib_qpn_t dqpn;
	int dtype;
	int rv;

	/*
	 * Lookup the destination in the vhub table or in our mcg list
	 */
	rv = eib_vnic_lookup_dest(vnic, dmac, vlan, &swqe->qe_av, &dqpn,
	    &dtype);
	if (rv != EIB_E_SUCCESS) {
		EIB_DPRINTF_WARN(ss->ei_instance, "eib_vnic_setup_dest: "
		    "eib_vnic_lookup_dest(dmac=%x:%x:%x:%x:%x:%x, vlan=0x%x) "
		    "failed", dmac[0], dmac[1], dmac[2], dmac[3], dmac[4],
		    dmac[5], vlan);

		return (EIB_E_FAILURE);
	}

	/*
	 * Modify the ud destination based on the address vector and qpn
	 * we got from the lookup.
	 */
	ret = ibt_modify_ud_dest(swqe->qe_dest, EIB_DATA_QKEY, dqpn,
	    &swqe->qe_av);
	if (ret != IBT_SUCCESS) {
		EIB_DPRINTF_WARN(ss->ei_instance,
		    "eib_vnic_setup_dest: "
		    "ibt_modify_ud_dest(qpn=0x%lx, qkey=0x%lx) "
		    "failed, ret=%d", dqpn, EIB_DATA_QKEY, ret);
		return (EIB_E_FAILURE);
	}

	if (dtype == EIB_TX_BROADCAST)
		EIB_INCR_COUNTER(&stats->st_brdcstxmit);
	else if (dtype == EIB_TX_MULTICAST)
		EIB_INCR_COUNTER(&stats->st_multixmit);

	return (EIB_E_SUCCESS);
}

/*ARGSUSED*/
void
eib_vnic_init_tables(eib_t *ss, eib_vnic_t *vnic)
{
	eib_vhub_table_t *tbl;
	eib_vhub_update_t *upd;

	tbl = kmem_zalloc(sizeof (eib_vhub_table_t), KM_SLEEP);
	rw_init(&tbl->tb_lock, NULL, RW_DRIVER, NULL);
	tbl->tb_eport_state = FIP_EPORT_UP;

	upd = kmem_zalloc(sizeof (eib_vhub_update_t), KM_SLEEP);
	mutex_init(&upd->up_lock, NULL, MUTEX_DRIVER, NULL);

	mutex_enter(&vnic->vn_lock);
	vnic->vn_vhub_table = tbl;
	vnic->vn_vhub_update = upd;
	mutex_exit(&vnic->vn_lock);
}

/*ARGSUSED*/
void
eib_vnic_fini_tables(eib_t *ss, eib_vnic_t *vnic, boolean_t clobber)
{
	eib_vhub_update_t *upd;
	eib_vhub_table_t *tbl;
	eib_vhub_map_t *elem;
	eib_vhub_map_t *nxt;
	int i;

	/*
	 * We come here only when we've either completely detached from
	 * the vhub multicast groups and so cannot receive anymore table
	 * or update control messages, or we've had a recent vhub table
	 * construction failure and the vnic state is currently
	 * EIB_LOGIN_TBL_FAILED and so won't parse any table or update
	 * control messages.  Also, since we haven't completed the vnic
	 * creation, no one from the tx path will be accessing the
	 * vn_vhub_table entries either.  All said, we're free to play
	 * around with the vnic's vn_vhub_table and vn_vhub_update here.
	 */

	mutex_enter(&vnic->vn_lock);
	upd = vnic->vn_vhub_update;
	tbl = vnic->vn_vhub_table;
	if (clobber) {
		vnic->vn_vhub_update = NULL;
		vnic->vn_vhub_table = NULL;
	}
	mutex_exit(&vnic->vn_lock);

	/*
	 * Destroy the vhub update entries if any
	 */
	if (upd) {
		/*
		 * Wipe clean the list of vnic entries accumulated via
		 * vhub updates so far.  Release eib_vhub_update_t only
		 * if explicitly asked to do so
		 */
		mutex_enter(&upd->up_lock);
		for (elem = upd->up_vnic_entry; elem != NULL; elem = nxt) {
			nxt = elem->mp_next;
			kmem_free(elem, sizeof (eib_vhub_map_t));
		}
		upd->up_vnic_entry = NULL;
		upd->up_tusn = 0;
		upd->up_eport_state = 0;
		mutex_exit(&upd->up_lock);

		if (clobber) {
			mutex_destroy(&upd->up_lock);
			kmem_free(upd, sizeof (eib_vhub_update_t));
		}
	}

	/*
	 * Destroy the vhub table entries
	 */
	if (tbl == NULL)
		return;

	/*
	 * Wipe clean the list of entries in the vhub table collected so
	 * far. Release eib_vhub_table_t only if explicitly asked to do so.
	 */
	rw_enter(&tbl->tb_lock, RW_WRITER);

	if (tbl->tb_gateway) {
		kmem_free(tbl->tb_gateway, sizeof (eib_vhub_map_t));
		tbl->tb_gateway = NULL;
	}

	if (tbl->tb_unicast_miss) {
		kmem_free(tbl->tb_unicast_miss, sizeof (eib_vhub_map_t));
		tbl->tb_unicast_miss = NULL;
	}

	if (tbl->tb_vhub_multicast) {
		kmem_free(tbl->tb_vhub_multicast, sizeof (eib_vhub_map_t));
		tbl->tb_vhub_multicast = NULL;
	}

	if (!eib_wa_no_mcast_entries) {
		for (i = 0; i < EIB_TB_NBUCKETS; i++) {
			for (elem = tbl->tb_mcast_entry[i]; elem != NULL;
			    elem = nxt) {
				nxt = elem->mp_next;
				kmem_free(elem, sizeof (eib_vhub_map_t));
			}
			tbl->tb_mcast_entry[i] = NULL;
		}
	}

	for (i = 0; i < EIB_TB_NBUCKETS; i++) {
		for (elem = tbl->tb_vnic_entry[i]; elem != NULL; elem = nxt) {
			nxt = elem->mp_next;
			kmem_free(elem, sizeof (eib_vhub_map_t));
		}
		tbl->tb_vnic_entry[i] = NULL;
	}

	tbl->tb_tusn = 0;
	tbl->tb_eport_state = 0;
	tbl->tb_entries_seen = 0;
	tbl->tb_entries_in_table = 0;
	tbl->tb_checksum = 0;

	rw_exit(&tbl->tb_lock);

	/*
	 * Don't throw away space created for holding vhub table if we haven't
	 * been explicitly asked to do so
	 */
	if (clobber) {
		rw_destroy(&tbl->tb_lock);
		kmem_free(tbl, sizeof (eib_vhub_table_t));
	}
}

void
eib_vnic_need_new(eib_t *ss, uint8_t *mac, uint16_t vlan)
{
	eib_vnic_req_t *vrq;

	EIB_INCR_COUNTER(&ss->ei_stats->st_noxmitbuf);

	/*
	 * Create a new vnic request for this {mac,vlan} tuple
	 */
	vrq = kmem_zalloc(sizeof (eib_vnic_req_t), KM_NOSLEEP);
	if (vrq == NULL) {
		EIB_DPRINTF_WARN(ss->ei_instance, "eib_vnic_need_new: "
		    "no memory, failed to queue new vnic creation request");
		return;
	}
	vrq->vr_next = NULL;
	vrq->vr_req = EIB_CR_REQ_NEW_VNIC;
	bcopy(mac, vrq->vr_mac, ETHERADDRL);
	vrq->vr_vlan = vlan;

	eib_vnic_enqueue_req(ss, vrq);
}

void
eib_vnic_do_delete(eib_t *ss, uint8_t *mac, uint16_t vlan)
{
	eib_vnic_req_t *vrq;

	/*
	 * Enqueue a delete-vnic request for this macaddr. Currently
	 * the mac layer doesn't have any interface that provides the
	 * vlan info, so we ignore it.  The vnic manager thread will
	 * simply delete all vnics with the same macaddr.
	 */
	vrq = kmem_zalloc(sizeof (eib_vnic_req_t), KM_NOSLEEP);
	if (vrq == NULL) {
		EIB_DPRINTF_WARN(ss->ei_instance, "eib_vnic_do_delete: "
		    "no memory, failed to queue vnic deletion request");
		return;
	}
	vrq->vr_next = NULL;
	vrq->vr_req = EIB_CR_REQ_DEL_VNIC;
	bcopy(mac, vrq->vr_mac, ETHERADDRL);
	vrq->vr_vlan = vlan;

	eib_vnic_enqueue_req(ss, vrq);
}

void
eib_vnic_enqueue_req(eib_t *ss, eib_vnic_req_t *vrq)
{
	eib_vnic_req_t *elem = NULL;
	uint8_t *m;

	/*
	 * Enqueue this new vnic request with the vnic manager and
	 * signal it.
	 */
	m = vrq->vr_mac;
	EIB_DPRINTF_DEBUG(ss->ei_instance, "eib_vnic_enqueue_req: "
	    "BEGIN file request for creation of %x:%x:%x:%x:%x:%x, 0x%x",
	    m[0], m[1], m[2], m[3], m[4], m[5], vrq->vr_vlan);


	mutex_enter(&ss->ei_vnic_req_lock);

	/*
	 * Death request has the highest priority.  If we've already been asked
	 * to die, we don't entertain any more requests.
	 */
	if (ss->ei_vnic_req) {
		if (ss->ei_vnic_req->vr_req == EIB_CR_REQ_DIE) {
			mutex_exit(&ss->ei_vnic_req_lock);
			kmem_free(vrq, sizeof (eib_vnic_req_t));
			return;
		}
	}

	if (vrq->vr_req == EIB_CR_REQ_DIE || vrq->vr_req == EIB_CR_REQ_FLUSH) {
		vrq->vr_next = ss->ei_vnic_req;
		ss->ei_vnic_req = vrq;
	} else {
		/*
		 * If there's already a creation request for this vnic that's
		 * being processed, return immediately without adding a new
		 * request.
		 */
		if ((elem = ss->ei_pending_vnic_req) != NULL) {
			EIB_DPRINTF_DEBUG(ss->ei_instance,
			    "eib_vnic_enqueue_req: "
			    "ei_pending_vnic_req not NULL");

			if ((elem->vr_vlan == vrq->vr_vlan) &&
			    (bcmp(elem->vr_mac, vrq->vr_mac,
			    ETHERADDRL) == 0)) {
				EIB_DPRINTF_DEBUG(ss->ei_instance,
				    "eib_vnic_enqueue_req: "
				    "pending request already present for "
				    "%x:%x:%x:%x:%x:%x, 0x%x", m[0], m[1], m[2],
				    m[3], m[4], m[5], vrq->vr_vlan);

				mutex_exit(&ss->ei_vnic_req_lock);
				kmem_free(vrq, sizeof (eib_vnic_req_t));

				EIB_DPRINTF_DEBUG(ss->ei_instance,
				    "eib_vnic_enqueue_req: "
				    "END file request");
				return;
			}

			EIB_DPRINTF_DEBUG(ss->ei_instance,
			    "eib_vnic_enqueue_req: "
			    "NO pending request for %x:%x:%x:%x:%x:%x, 0x%x",
			    m[0], m[1], m[2], m[3], m[4], m[5], vrq->vr_vlan);
		}

		/*
		 * Or if there's one waiting in the queue for processing, do
		 * the same thing
		 */
		for (elem = ss->ei_vnic_req; elem; elem = elem->vr_next) {
			/*
			 * If there's already a create request for this vnic
			 * waiting in the queue, return immediately
			 */
			if (elem->vr_req == EIB_CR_REQ_NEW_VNIC) {
				if ((elem->vr_vlan == vrq->vr_vlan) &&
				    (bcmp(elem->vr_mac, vrq->vr_mac,
				    ETHERADDRL) == 0)) {

					EIB_DPRINTF_DEBUG(ss->ei_instance,
					    "eib_vnic_enqueue_req: "
					    "request already present for "
					    "%x:%x:%x:%x:%x:%x, 0x%x", m[0],
					    m[1], m[2], m[3], m[4], m[5],
					    vrq->vr_vlan);

					mutex_exit(&ss->ei_vnic_req_lock);
					kmem_free(vrq, sizeof (eib_vnic_req_t));

					EIB_DPRINTF_DEBUG(ss->ei_instance,
					    "eib_vnic_enqueue_req: "
					    "END file request");
					return;
				}
			}

			if (elem->vr_next == NULL) {
				EIB_DPRINTF_DEBUG(ss->ei_instance,
				    "eib_vnic_enqueue_req: "
				    "request not found, filing afresh");
				break;
			}
		}

		/*
		 * Otherwise queue up this new creation request and signal the
		 * service thread.
		 */
		if (elem) {
			elem->vr_next = vrq;
		} else {
			ss->ei_vnic_req = vrq;
		}
	}

	cv_signal(&ss->ei_vnic_req_cv);
	mutex_exit(&ss->ei_vnic_req_lock);

	EIB_DPRINTF_DEBUG(ss->ei_instance,
	    "eib_vnic_enqueue_req: END file request");
}

void
eib_vnic_update_failed_macs(eib_t *ss, uint8_t *old_mac, uint16_t old_vlan,
    uint8_t *new_mac, uint16_t new_vlan)
{
	eib_vnic_req_t *vrq;
	eib_vnic_req_t *elem;
	eib_vnic_req_t *prev;

	vrq = kmem_zalloc(sizeof (eib_vnic_req_t), KM_NOSLEEP);
	if (vrq == NULL) {
		EIB_DPRINTF_WARN(ss->ei_instance,
		    "eib_vnic_update_failed_macs: "
		    "no memory, failed to drop old mac");
	} else {
		vrq->vr_next = NULL;
		vrq->vr_req = 0;	/* unused */
		bcopy(old_mac, vrq->vr_mac, ETHERADDRL);
		vrq->vr_vlan = old_vlan;
	}

	mutex_enter(&ss->ei_vnic_req_lock);

	/*
	 * We'll search the failed vnics list to see if the new {mac,vlan}
	 * tuple is in there and remove it if present (since the new address
	 * is no longer "failed").
	 */
	prev = NULL;
	for (elem = ss->ei_failed_vnic_req; elem; elem = elem->vr_next) {
		if ((bcmp(elem->vr_mac, new_mac, ETHERADDRL) == 0) &&
		    (elem->vr_vlan == new_vlan)) {
			if (prev) {
				prev->vr_next = elem->vr_next;
			} else {
				ss->ei_failed_vnic_req = elem->vr_next;
			}
			elem->vr_next = NULL;
			break;
		}
	}
	if (elem) {
		kmem_free(elem, sizeof (eib_vnic_req_t));
	}

	/*
	 * We'll also insert the old {mac,vlan} tuple to the "failed vnic req"
	 * list (it shouldn't be there already), to avoid trying to recreate
	 * the vnic we just explicitly discarded.
	 */
	if (vrq) {
		vrq->vr_next = ss->ei_failed_vnic_req;
		ss->ei_failed_vnic_req = vrq;
	}

	mutex_exit(&ss->ei_vnic_req_lock);
}

void
eib_vnic_resurrect_zombies(eib_t *ss, uint8_t *vn0_mac)
{
	int inst;

	/*
	 * We want to restart/relogin each vnic instance with the gateway,
	 * but with the same vnic id and instance as before.
	 */
	while ((inst = EIB_FIND_LSB_SET(ss->ei_zombie_vnics)) != -1) {
		EIB_DPRINTF_DEBUG(ss->ei_instance,
		    "eib_vnic_resurrect_zombies: "
		    "calling eib_vnic_restart(vn_inst=%d)", inst);

		eib_vnic_restart(ss, inst, vn0_mac);

		EIB_DPRINTF_DEBUG(ss->ei_instance,
		    "eib_vnic_resurrect_zombies: "
		    "eib_vnic_restart(vn_inst=%d) done", inst);
	}
}

void
eib_vnic_restart(eib_t *ss, int inst, uint8_t *vn0_mac)
{
	eib_vnic_t *vnic;
	eib_login_data_t *ld;
	uint8_t old_mac[ETHERADDRL];
	int ret;
	int err;

	if (inst < 0 || inst >= EIB_MAX_VNICS) {
		EIB_DPRINTF_WARN(ss->ei_instance,
		    "eib_vnic_restart: "
		    "vnic instance (%d) invalid", inst);
		return;
	}

	eib_vnic_modify_enter(ss, EIB_VN_BEING_MODIFIED);
	if ((vnic = ss->ei_vnic[inst]) != NULL) {
		/*
		 * Remember what mac was allocated for this vnic last time.
		 */
		bcopy(vnic->vn_login_data.ld_assigned_mac, old_mac, ETHERADDRL);

		/*
		 * Tear down and restart this vnic instance
		 */
		eib_rb_vnic_create_common(ss, vnic, ~0);
		ret = eib_vnic_create_common(ss, vnic, &err);
		if (ret != EIB_E_SUCCESS) {
			EIB_DPRINTF_WARN(ss->ei_instance,
			    "eib_vnic_restart: "
			    "eib_vnic_create_common(vnic_inst=%d) failed, "
			    "ret=%d", inst, err);
		}

		/*
		 * If this is vnic instance 0 and if our current assigned mac is
		 * different from what was assigned last time, we need to pass
		 * this information back to the caller, so the mac layer can be
		 * appropriately informed. We will also queue up the old mac
		 * and vlan in the "failed vnic req" list, so any future packets
		 * to this address on this interface will be dropped.
		 */
		ld = &vnic->vn_login_data;
		if ((inst == 0) &&
		    (bcmp(ld->ld_assigned_mac, old_mac, ETHERADDRL) != 0)) {
			uint8_t *m = ld->ld_assigned_mac;

			if (vn0_mac != NULL) {
				bcopy(ld->ld_assigned_mac, vn0_mac,
				    ETHERADDRL);
			}

			EIB_DPRINTF_VERBOSE(ss->ei_instance,
			    "eib_vnic_restart: updating failed macs list "
			    "old=%x:%x:%x:%x:%x:%x, new=%x:%x:%x:%x:%x:%x, "
			    "vlan=0x%x", old_mac[0], old_mac[1], old_mac[2],
			    old_mac[3], old_mac[4], old_mac[5], m[0], m[1],
			    m[2], m[3], m[4], m[5], vnic->vn_vlan);

			eib_vnic_update_failed_macs(ss, old_mac, vnic->vn_vlan,
			    ld->ld_assigned_mac, vnic->vn_vlan);
		}

		/*
		 * No longer a zombie or need to rejoin mcgs
		 */
		rw_enter(&ss->ei_vnic_lock, RW_WRITER);
		ss->ei_zombie_vnics &= (~((uint64_t)1 << inst));
		ss->ei_rejoin_vnics &= (~((uint64_t)1 << inst));
		rw_exit(&ss->ei_vnic_lock);
	}
	eib_vnic_modify_exit(ss, EIB_VN_BEING_MODIFIED);
}

void
eib_vnic_rejoin_mcgs(eib_t *ss)
{
	eib_vnic_t *vnic;
	int inst;

	/*
	 * For each vnic that still requires re-join, go through the
	 * control channels and data channel and reattach/rejoin mcgs.
	 */
	rw_enter(&ss->ei_vnic_lock, RW_WRITER);
	while ((inst = EIB_FIND_LSB_SET(ss->ei_rejoin_vnics)) != -1) {
		if ((vnic = ss->ei_vnic[inst]) != NULL) {
			eib_vnic_reattach_ctl_mcgs(ss, vnic);
			eib_vnic_reattach_vhub_data(ss, vnic);
		}
		ss->ei_rejoin_vnics &= (~((uint64_t)1 << inst));
	}
	rw_exit(&ss->ei_vnic_lock);
}

void
eib_rb_vnic_create(eib_t *ss, eib_vnic_t *vnic, uint_t progress)
{
	if (progress & EIB_VNIC_CREATE_COMMON_DONE) {
		eib_rb_vnic_create_common(ss, vnic, ~0);
	}

	if (progress & EIB_VNIC_GOT_INSTANCE) {
		eib_vnic_ret_instance(ss, vnic->vn_instance);
		vnic->vn_instance = -1;
	}

	if (progress & EIB_VNIC_STRUCT_ALLOCD) {
		cv_destroy(&vnic->vn_cv);
		rw_destroy(&vnic->vn_vhub_mcgs_lock);
		mutex_destroy(&vnic->vn_lock);
		kmem_free(vnic, sizeof (eib_vnic_t));
	}
}

/*
 * Currently, we only allow 64 vnics per eoib device instance, for
 * reasons described in eib.h (see EIB_VNIC_ID() definition), so we
 * could use a simple bitmap to assign the vnic instance numbers.
 * Once we start allowing more vnics per device instance, this
 * allocation scheme will need to be changed.
 */
static int
eib_vnic_get_instance(eib_t *ss, int *vinst)
{
	int bitpos;
	uint64_t nval;

	rw_enter(&ss->ei_vnic_lock, RW_WRITER);

	/*
	 * What we have is the active vnics list --  the in-use vnics are
	 * indicated by a 1 in the bit position, and the free ones are
	 * indicated by 0.  We need to find the least significant '0' bit
	 * to get the first free vnic instance.  Or we could bit-reverse
	 * the active list and locate the least significant '1'.
	 */
	nval = ~(ss->ei_active_vnics);
	if (nval == 0) {
		rw_exit(&ss->ei_vnic_lock);
		return (EIB_E_FAILURE);
	}

	/*
	 * The single bit-position values in a 64-bit integer are relatively
	 * prime with 67, so performing a modulus division with 67 guarantees
	 * a unique number between 0 and 63 for each value (setbit_mod67[]).
	 */
	bitpos = EIB_FIND_LSB_SET(nval);
	if (bitpos == -1) {
		rw_exit(&ss->ei_vnic_lock);
		return (EIB_E_FAILURE);
	}

	ss->ei_active_vnics |= ((uint64_t)1 << bitpos);
	*vinst = bitpos;

	rw_exit(&ss->ei_vnic_lock);

	return (EIB_E_SUCCESS);
}

static void
eib_vnic_ret_instance(eib_t *ss, int vinst)
{
	rw_enter(&ss->ei_vnic_lock, RW_WRITER);

	if (vinst >= EIB_MAX_VNICS) {
		EIB_DPRINTF_WARN(ss->ei_instance,
		    "eib_vnic_ret_instance: "
		    "vnic instance (%d) invalid", vinst);
	} else if ((ss->ei_active_vnics & ((uint64_t)1 << vinst)) == 0) {
		EIB_DPRINTF_WARN(ss->ei_instance,
		    "eib_vnic_ret_instance: "
		    "vnic instance (%d) not active!", vinst);
	} else {
		ss->ei_active_vnics &= (~((uint64_t)1 << vinst));
	}

	rw_exit(&ss->ei_vnic_lock);
}

static void
eib_vnic_modify_enter(eib_t *ss, uint_t op)
{
	mutex_enter(&ss->ei_vnic_state_lock);
	while (ss->ei_vnic_state & EIB_VN_BEING_MODIFIED)
		cv_wait(&ss->ei_vnic_state_cv, &ss->ei_vnic_state_lock);

	ss->ei_vnic_state |= op;
	mutex_exit(&ss->ei_vnic_state_lock);
}

static void
eib_vnic_modify_exit(eib_t *ss, uint_t op)
{
	mutex_enter(&ss->ei_vnic_state_lock);
	ss->ei_vnic_state &= (~op);
	cv_broadcast(&ss->ei_vnic_state_cv);
	mutex_exit(&ss->ei_vnic_state_lock);
}

static int
eib_vnic_create_common(eib_t *ss, eib_vnic_t *vnic, int *err)
{
	uint_t progress = 0;

	/*
	 * When we receive login acks within this vnic creation
	 * routine we need a way to retrieve the vnic structure
	 * from the vnic instance, so store this somewhere. Note
	 * that there can be only one outstanding vnic creation
	 * at any point of time, so we only need one vnic struct.
	 */
	rw_enter(&ss->ei_vnic_lock, RW_WRITER);
	ASSERT(ss->ei_vnic_pending == NULL);
	ss->ei_vnic_pending = vnic;
	rw_exit(&ss->ei_vnic_lock);

	/*
	 * Create a control qp for this vnic
	 */
	if (eib_ctl_create_qp(ss, vnic, err) != EIB_E_SUCCESS) {
		EIB_DPRINTF_WARN(ss->ei_instance,
		    "eib_vnic_create_common: "
		    "eib_ctl_create_qp(vn_id=0x%x) failed, ret=%d",
		    vnic->vn_id, *err);
		goto vnic_create_common_fail;
	}
	progress |= EIB_VNIC_CTLQP_CREATED;

	/*
	 * Create a data qp for this vnic
	 */
	if (eib_data_create_qp(ss, vnic, err) != EIB_E_SUCCESS) {
		EIB_DPRINTF_WARN(ss->ei_instance,
		    "eib_vnic_create_common: "
		    "eib_data_create_qp(vn_id=0x%x) failed, ret=%d",
		    vnic->vn_id, *err);
		goto vnic_create_common_fail;
	}
	progress |= EIB_VNIC_DATAQP_CREATED;

	/*
	 * Login to the gateway with this vnic's parameters
	 */
	if (eib_fip_login(ss, vnic, err) != EIB_E_SUCCESS) {
		EIB_DPRINTF_WARN(ss->ei_instance,
		    "eib_vnic_create_common: "
		    "eib_fip_login(vn_id=0x%x) failed, ret=%d",
		    vnic->vn_id, *err);
		goto vnic_create_common_fail;
	}
	progress |= EIB_VNIC_LOGIN_DONE;

	/*
	 * Associate the control and data qps for the vnic with the
	 * vHUB partition
	 */
	if (eib_vnic_set_partition(ss, vnic, err) != EIB_E_SUCCESS) {
		EIB_DPRINTF_WARN(ss->ei_instance,
		    "eib_vnic_create_common: "
		    "eib_vnic_set_partition(vn_id=0x%x) failed, ret=%d",
		    vnic->vn_id, *err);
		goto vnic_create_common_fail;
	}
	progress |= EIB_VNIC_PARTITION_SET;

	/*
	 * Attach to the vHUB table and vHUB update multicast groups
	 */
	if (eib_vnic_attach_ctl_mcgs(ss, vnic, err) != EIB_E_SUCCESS) {
		EIB_DPRINTF_WARN(ss->ei_instance,
		    "eib_vnic_create_common: "
		    "eib_vnic_attach_ctl_mcgs(vn_id=0x%x) failed, ret=%d",
		    vnic->vn_id, *err);
		goto vnic_create_common_fail;
	}
	progress |= EIB_VNIC_ATTACHED_TO_CTL_MCGS;

	/*
	 * Send the vHUB table request and construct the vhub table
	 */
	if (eib_fip_vhub_table(ss, vnic, err) != EIB_E_SUCCESS) {
		EIB_DPRINTF_WARN(ss->ei_instance,
		    "eib_vnic_create_common: "
		    "eib_fip_vhub_table(vn_id=0x%x) failed, ret=%d",
		    vnic->vn_id, *err);
		goto vnic_create_common_fail;
	}
	progress |= EIB_VNIC_GOT_VHUB_TABLE;

	/*
	 * Detach from the vHUB table mcg (we no longer need the vHUB
	 * table messages) and start the keepalives for this vnic.
	 */
	eib_vnic_start_keepalives(ss, vnic);
	eib_rb_vnic_attach_vhub_table(ss, vnic);

	progress |= EIB_VNIC_KEEPALIVES_STARTED;

	/*
	 * All ethernet vnics are automatically members of the broadcast
	 * group for the vlan they are participating in, so attach to the
	 * vhub data mcg for this vnic and add the broadcast mac address
	 * to the list of ethernet multicast mac addresses multiplexed
	 * onto the mcg.
	 */
	if (eib_vnic_attach_vhub_data(ss, vnic, err) != EIB_E_SUCCESS) {
		EIB_DPRINTF_WARN(ss->ei_instance,
		    "eib_vnic_create_common: "
		    "eib_vnic_attach_vhub_data(vn_id=0x%x) failed, ret=%d",
		    vnic->vn_id, *err);
		goto vnic_create_common_fail;
	}
	progress |= EIB_VNIC_ATTACHED_TO_VHUB_DATA;

	if (eib_vnic_join_multicast(ss, vnic, eib_broadcast_mac, err) !=
	    EIB_E_SUCCESS) {
		EIB_DPRINTF_WARN(ss->ei_instance,
		    "eib_vnic_create_common: eib_vnic_join_multicast"
		    "(vn_id=0x%x, mcast=ff:ff:ff:ff:ff:ff) failed, ret=%d",
		    vnic->vn_id, *err);
		goto vnic_create_common_fail;
	}
	progress |= EIB_VNIC_BROADCAST_JOINED;

	rw_enter(&ss->ei_vnic_lock, RW_WRITER);
	if (ss->ei_vnic[vnic->vn_instance] == NULL) {
		ss->ei_vnic[vnic->vn_instance] = vnic;
	}
	ss->ei_vnic_pending = NULL;
	rw_exit(&ss->ei_vnic_lock);

	return (EIB_E_SUCCESS);

vnic_create_common_fail:
	eib_rb_vnic_create_common(ss, vnic, progress);
	return (EIB_E_FAILURE);
}

static int
eib_vnic_set_partition(eib_t *ss, eib_vnic_t *vnic, int *err)
{
	int ret;

	/*
	 * Associate the control channel with the vhub partition
	 */
	ret = eib_ibt_modify_chan_pkey(ss, vnic->vn_ctl_chan,
	    vnic->vn_login_data.ld_vhub_pkey);
	if (ret != EIB_E_SUCCESS) {
		EIB_DPRINTF_WARN(ss->ei_instance,
		    "eib_vnic_set_partition: "
		    "eib_ibt_modify_chan_pkey(vn_id=0x%x, CTL_CHAN, "
		    "vhub_pkey=0x%x) failed", vnic->vn_id,
		    vnic->vn_login_data.ld_vhub_pkey);
		*err = EINVAL;
		return (EIB_E_FAILURE);
	}

	/*
	 * Now, do the same thing for the data channel. Note that if a
	 * failure happens, the channel state(s) are left as-is, since
	 * it is pointless to try to change them back using the same
	 * interfaces that have just failed.
	 */
	ret = eib_ibt_modify_chan_pkey(ss, vnic->vn_data_chan,
	    vnic->vn_login_data.ld_vhub_pkey);
	if (ret != EIB_E_SUCCESS) {
		EIB_DPRINTF_WARN(ss->ei_instance,
		    "eib_vnic_set_partition: "
		    "eib_ibt_modify_chan_pkey(vn_id=0x%x, DATA_CHAN, "
		    "vhub_pkey=0x%x) failed", vnic->vn_id,
		    vnic->vn_login_data.ld_vhub_pkey);
		*err = EINVAL;
		return (EIB_E_FAILURE);
	}

	return (EIB_E_SUCCESS);
}

static void
eib_vnic_make_vhub_mgid(uint8_t *mg_prefix, uint8_t mg_type,
    uint8_t *mcast_mac, uint8_t n_mac, uint8_t rss_hash, uint32_t vhub_id,
    ib_gid_t *mgid)
{
	eib_mgid_t em;
	uint64_t dmac_mask;
	uint64_t dmac = 0;
	uint8_t *dmac_str = (uint8_t *)&dmac;
	uint_t	vhub_id_nw;
	uint8_t *vhub_id_str = (uint8_t *)&vhub_id_nw;

	/*
	 * Copy mgid prefix and type
	 */
	bcopy(mg_prefix, em.gd_spec.sp_mgid_prefix, FIP_MGID_PREFIX_LEN);
	em.gd_spec.sp_type = mg_type;

	/*
	 * Take n_mac bits from mcast_mac and copy dmac
	 */
	bcopy(mcast_mac, dmac_str + 2, ETHERADDRL);
	dmac_mask = ((uint64_t)1 << n_mac) - 1;
	dmac_mask = htonll(dmac_mask);
	dmac &= dmac_mask;
	bcopy(dmac_str + 2, em.gd_spec.sp_dmac, ETHERADDRL);

	/*
	 * Copy rss hash and prepare vhub id from gw port id and vlan
	 */
	em.gd_spec.sp_rss_hash = rss_hash;

	vhub_id_nw = htonl(vhub_id);
	bcopy(vhub_id_str + 1, em.gd_spec.sp_vhub_id, FIP_VHUBID_LEN);

	/*
	 * Ok, now we've assembled the mgid as per EoIB spec. We now have to
	 * represent it in the way Solaris IBTF wants it and return (sigh).
	 */
	mgid->gid_prefix = ntohll(em.gd_sol.gid_prefix);
	mgid->gid_guid = ntohll(em.gd_sol.gid_guid);
}

static int
eib_vnic_attach_ctl_mcgs(eib_t *ss, eib_vnic_t *vnic, int *err)
{
	/*
	 * Get tb_vhub_table and tb_vhub_update allocated and ready before
	 * attaching to the vhub table and vhub update mcgs
	 */
	eib_vnic_init_tables(ss, vnic);

	if (eib_vnic_attach_vhub_update(ss, vnic) != EIB_E_SUCCESS) {
		EIB_DPRINTF_WARN(ss->ei_instance,
		    "eib_vnic_attach_ctl_mcgs: "
		    "eib_vnic_attach_vhub_update(vn_id=0x%x) failed",
		    vnic->vn_id);

		*err = EINVAL;
		eib_vnic_fini_tables(ss, vnic, B_TRUE);
		return (EIB_E_FAILURE);
	}

	if (eib_vnic_attach_vhub_table(ss, vnic) != EIB_E_SUCCESS) {
		EIB_DPRINTF_WARN(ss->ei_instance,
		    "eib_vnic_attach_ctl_mcgs: "
		    "eib_vnic_attach_vhub_table(vn_id=0x%x) failed",
		    vnic->vn_id);

		*err = EINVAL;
		eib_rb_vnic_attach_vhub_update(ss, vnic);
		eib_vnic_fini_tables(ss, vnic, B_TRUE);
		return (EIB_E_FAILURE);
	}

	return (EIB_E_SUCCESS);
}

static int
eib_vnic_attach_vhub_table(eib_t *ss, eib_vnic_t *vnic)
{
	eib_chan_t *chan = vnic->vn_ctl_chan;
	eib_login_data_t *ld = &vnic->vn_login_data;
	eib_mcg_t *mcg;
	ibt_mcg_info_t *tbl_mcginfo;
	ibt_mcg_attr_t mcg_attr;
	ibt_status_t ret;
	uint_t entries;

	/*
	 * Compose the MGID for receiving VHUB table
	 */
	bzero(&mcg_attr, sizeof (ibt_mcg_attr_t));

	eib_vnic_make_vhub_mgid(ld->ld_gw_mgid_prefix,
	    (uint8_t)EIB_MGID_VHUB_TABLE, eib_zero_mac, ld->ld_n_mac_mcgid,
	    0, ld->ld_vhub_id, &(mcg_attr.mc_mgid));
	mcg_attr.mc_pkey = (ib_pkey_t)ld->ld_vhub_pkey;
	mcg_attr.mc_qkey = (ib_qkey_t)EIB_FIP_QKEY;

	/*
	 * Locate the multicast group for receiving vhub table
	 */
	ret = ibt_query_mcg(ss->ei_props->ep_sgid, &mcg_attr, 1,
	    &tbl_mcginfo, &entries);
	if (ret != IBT_SUCCESS || entries != 1) {
		EIB_DPRINTF_WARN(ss->ei_instance,
		    "eib_vnic_attach_vhub_table: "
		    "ibt_query_mcg(mgid=%llx.%llx, pkey=0x%x) failed, "
		    "ret=%d", mcg_attr.mc_mgid.gid_prefix,
		    mcg_attr.mc_mgid.gid_guid, mcg_attr.mc_pkey, ret);
		return (EIB_E_FAILURE);
	}

	/*
	 * Allocate for and prepare the mcg to add to our list
	 */
	mcg = kmem_zalloc(sizeof (eib_mcg_t), KM_NOSLEEP);
	if (mcg == NULL) {
		EIB_DPRINTF_WARN(ss->ei_instance,
		    "eib_vnic_attach_vhub_table: "
		    "no memory, failed to attach to vhub table "
		    "(mgid=%llx.%llx, pkey=0x%x)", mcg_attr.mc_mgid.gid_prefix,
		    mcg_attr.mc_mgid.gid_guid, mcg_attr.mc_pkey);
		ibt_free_mcg_info(tbl_mcginfo, 1);
		return (EIB_E_FAILURE);
	}

	mcg->mg_rgid = ss->ei_props->ep_sgid;
	mcg->mg_mgid = mcg_attr.mc_mgid;
	mcg->mg_join_state = IB_MC_JSTATE_FULL;
	mcg->mg_mcginfo = tbl_mcginfo;
	mcg->mg_elem = NULL;

	/*
	 * Join the multicast group
	 */
	mcg_attr.mc_join_state = mcg->mg_join_state;
	mcg_attr.mc_flow = tbl_mcginfo->mc_adds_vect.av_flow;
	mcg_attr.mc_tclass = tbl_mcginfo->mc_adds_vect.av_tclass;
	mcg_attr.mc_sl = tbl_mcginfo->mc_adds_vect.av_srvl;
	mcg_attr.mc_scope = 0;	/* IB_MC_SCOPE_SUBNET_LOCAL perhaps ? */

	ret = ibt_join_mcg(mcg->mg_rgid, &mcg_attr, tbl_mcginfo, NULL, NULL);
	if (ret != IBT_SUCCESS) {
		EIB_DPRINTF_WARN(ss->ei_instance,
		    "eib_vnic_attach_vhub_table: "
		    "ibt_join_mcg(mgid=%llx.%llx, pkey=0x%x, jstate=0x%x) "
		    "failed, ret=%d", mcg_attr.mc_mgid.gid_prefix,
		    mcg_attr.mc_mgid.gid_guid, mcg_attr.mc_pkey,
		    mcg_attr.mc_join_state, ret);

		kmem_free(mcg, sizeof (eib_mcg_t));
		ibt_free_mcg_info(tbl_mcginfo, 1);
		return (EIB_E_FAILURE);
	}

	/*
	 * Attach to the multicast group to receive tbl multicasts
	 */
	ret = ibt_attach_mcg(chan->ch_chan, tbl_mcginfo);
	if (ret != IBT_SUCCESS) {
		EIB_DPRINTF_WARN(ss->ei_instance,
		    "eib_vnic_attach_vhub_table: "
		    "ibt_attach_mcg(mgid=%llx.%llx, pkey=0x%x) "
		    "failed, ret=%d", mcg_attr.mc_mgid.gid_prefix,
		    mcg_attr.mc_mgid.gid_guid, mcg_attr.mc_pkey);

		(void) ibt_leave_mcg(mcg->mg_rgid, mcg->mg_mgid,
		    eib_reserved_gid, mcg->mg_join_state);
		kmem_free(mcg, sizeof (eib_mcg_t));
		ibt_free_mcg_info(tbl_mcginfo, 1);
		return (EIB_E_FAILURE);
	}

	rw_enter(&vnic->vn_vhub_mcgs_lock, RW_WRITER);
	vnic->vn_vhub_tbl_mcg = mcg;
	rw_exit(&vnic->vn_vhub_mcgs_lock);

	return (EIB_E_SUCCESS);
}

static int
eib_vnic_attach_vhub_update(eib_t *ss, eib_vnic_t *vnic)
{
	eib_chan_t *chan = vnic->vn_ctl_chan;
	eib_login_data_t *ld = &vnic->vn_login_data;
	eib_mcg_t *mcg;
	ibt_mcg_info_t *upd_mcginfo;
	ibt_mcg_attr_t mcg_attr;
	ibt_status_t ret;
	uint_t entries;

	/*
	 * Compose the MGID for receiving VHUB updates
	 */
	bzero(&mcg_attr, sizeof (ibt_mcg_attr_t));

	eib_vnic_make_vhub_mgid(ld->ld_gw_mgid_prefix,
	    (uint8_t)EIB_MGID_VHUB_UPDATE, eib_zero_mac,
	    ld->ld_n_mac_mcgid, 0, ld->ld_vhub_id, &(mcg_attr.mc_mgid));
	mcg_attr.mc_pkey = (ib_pkey_t)ld->ld_vhub_pkey;
	mcg_attr.mc_qkey = (ib_qkey_t)EIB_FIP_QKEY;

	/*
	 * Locate the multicast group for receiving vhub updates
	 */
	ret = ibt_query_mcg(ss->ei_props->ep_sgid, &mcg_attr, 1,
	    &upd_mcginfo, &entries);
	if (ret != IBT_SUCCESS || entries != 1) {
		EIB_DPRINTF_WARN(ss->ei_instance,
		    "eib_vnic_attach_vhub_update: "
		    "ibt_query_mcg(mgid=%llx.%llx, pkey=0x%x) failed, "
		    "ret=%d", mcg_attr.mc_mgid.gid_prefix,
		    mcg_attr.mc_mgid.gid_guid, mcg_attr.mc_pkey, ret);
		return (EIB_E_FAILURE);
	}

	/*
	 * Allocate for and prepare the mcg to add to our list
	 */
	mcg = kmem_zalloc(sizeof (eib_mcg_t), KM_NOSLEEP);
	if (mcg == NULL) {
		EIB_DPRINTF_WARN(ss->ei_instance,
		    "eib_vnic_attach_vhub_update: "
		    "no memory, failed to attach to vhub update "
		    "(mgid=%llx.%llx, pkey=0x%x)", mcg_attr.mc_mgid.gid_prefix,
		    mcg_attr.mc_mgid.gid_guid, mcg_attr.mc_pkey);

		ibt_free_mcg_info(upd_mcginfo, 1);
		return (EIB_E_FAILURE);
	}

	mcg->mg_rgid = ss->ei_props->ep_sgid;
	mcg->mg_mgid = mcg_attr.mc_mgid;
	mcg->mg_join_state = IB_MC_JSTATE_FULL;
	mcg->mg_mcginfo = upd_mcginfo;
	mcg->mg_elem = NULL;

	/*
	 * Join the multicast group
	 */
	mcg_attr.mc_join_state = mcg->mg_join_state;
	mcg_attr.mc_flow = upd_mcginfo->mc_adds_vect.av_flow;
	mcg_attr.mc_tclass = upd_mcginfo->mc_adds_vect.av_tclass;
	mcg_attr.mc_sl = upd_mcginfo->mc_adds_vect.av_srvl;
	mcg_attr.mc_scope = 0;	/* IB_MC_SCOPE_SUBNET_LOCAL perhaps ? */

	ret = ibt_join_mcg(mcg->mg_rgid, &mcg_attr, upd_mcginfo, NULL, NULL);
	if (ret != IBT_SUCCESS) {
		EIB_DPRINTF_WARN(ss->ei_instance,
		    "eib_vnic_attach_vhub_update: "
		    "ibt_join_mcg(mgid=%llx.%llx, pkey=0x%x, jstate=0x%x) "
		    "failed, ret=%d", mcg_attr.mc_mgid.gid_prefix,
		    mcg_attr.mc_mgid.gid_guid, mcg_attr.mc_pkey,
		    mcg_attr.mc_join_state, ret);

		kmem_free(mcg, sizeof (eib_mcg_t));
		ibt_free_mcg_info(upd_mcginfo, 1);
		return (EIB_E_FAILURE);
	}

	/*
	 * Attach to the multicast group to receive upd multicasts
	 */
	ret = ibt_attach_mcg(chan->ch_chan, upd_mcginfo);
	if (ret != IBT_SUCCESS) {
		EIB_DPRINTF_WARN(ss->ei_instance,
		    "eib_vnic_attach_vhub_update: "
		    "ibt_attach_mcg(mgid=%llx.%llx, pkey=0x%x) "
		    "failed, ret=%d", mcg_attr.mc_mgid.gid_prefix,
		    mcg_attr.mc_mgid.gid_guid, mcg_attr.mc_pkey);

		(void) ibt_leave_mcg(mcg->mg_rgid, mcg->mg_mgid,
		    eib_reserved_gid, mcg->mg_join_state);
		kmem_free(mcg, sizeof (eib_mcg_t));
		ibt_free_mcg_info(upd_mcginfo, 1);
		return (EIB_E_FAILURE);
	}

	rw_enter(&vnic->vn_vhub_mcgs_lock, RW_WRITER);
	vnic->vn_vhub_upd_mcg = mcg;
	rw_exit(&vnic->vn_vhub_mcgs_lock);

	return (EIB_E_SUCCESS);
}

static int
eib_vnic_attach_vhub_data(eib_t *ss, eib_vnic_t *vnic, int *err)
{
	eib_chan_t *chan = vnic->vn_data_chan;
	eib_login_data_t *ld = &vnic->vn_login_data;
	eib_mcg_t *mcg = NULL;
	ibt_mcg_info_t *data_mcginfo;
	ibt_mcg_attr_t mcg_attr;
	ibt_status_t ret;
	uint_t entries;

	/*
	 * Compose the multicast MGID to join
	 */
	bzero(&mcg_attr, sizeof (ibt_mcg_attr_t));

	eib_vnic_make_vhub_mgid(ld->ld_gw_mgid_prefix,
	    (uint8_t)EIB_MGID_VHUB_DATA, eib_zero_mac, ld->ld_n_mac_mcgid, 0,
	    ld->ld_vhub_id, &(mcg_attr.mc_mgid));
	mcg_attr.mc_pkey = (ib_pkey_t)ld->ld_vhub_pkey;
	mcg_attr.mc_qkey = (ib_qkey_t)EIB_DATA_QKEY;

	/*
	 * Locate the multicast group for vhub data traffic
	 */
	ret = ibt_query_mcg(ss->ei_props->ep_sgid, &mcg_attr, 1,
	    &data_mcginfo, &entries);
	if (ret != IBT_SUCCESS || entries != 1) {
		EIB_DPRINTF_WARN(ss->ei_instance,
		    "eib_vnic_attach_vhub_data: "
		    "ibt_query_mcg(mgid=%llx.%llx, pkey=0x%x) failed, "
		    "ret=%d", mcg_attr.mc_mgid.gid_prefix,
		    mcg_attr.mc_mgid.gid_guid, mcg_attr.mc_pkey, ret);
		*err = ENOENT;
		return (EIB_E_FAILURE);
	}

	/*
	 * Allocate for and prepare the mcg to add to our list
	 */
	mcg = kmem_zalloc(sizeof (eib_mcg_t), KM_NOSLEEP);
	if (mcg == NULL) {
		EIB_DPRINTF_WARN(ss->ei_instance,
		    "eib_vnic_attach_vhub_data: "
		    "no memory, failed to attach to vhub data "
		    "(mgid=%llx.%llx, pkey=0x%x)", mcg_attr.mc_mgid.gid_prefix,
		    mcg_attr.mc_mgid.gid_guid, mcg_attr.mc_pkey);

		ibt_free_mcg_info(data_mcginfo, 1);
		*err = ENOMEM;
		return (EIB_E_FAILURE);
	}

	mcg->mg_rgid = ss->ei_props->ep_sgid;
	mcg->mg_mgid = mcg_attr.mc_mgid;
	mcg->mg_join_state = IB_MC_JSTATE_FULL;
	mcg->mg_mcginfo = data_mcginfo;
	mcg->mg_elem = NULL;

	/*
	 * Join the multicast group
	 */
	mcg_attr.mc_join_state = mcg->mg_join_state;
	mcg_attr.mc_flow = data_mcginfo->mc_adds_vect.av_flow;
	mcg_attr.mc_tclass = data_mcginfo->mc_adds_vect.av_tclass;
	mcg_attr.mc_sl = data_mcginfo->mc_adds_vect.av_srvl;
	mcg_attr.mc_scope = 0;	/* IB_MC_SCOPE_SUBNET_LOCAL perhaps ? */

	ret = ibt_join_mcg(mcg->mg_rgid, &mcg_attr, data_mcginfo, NULL, NULL);
	if (ret != IBT_SUCCESS) {
		EIB_DPRINTF_WARN(ss->ei_instance,
		    "eib_vnic_attach_vhub_data: "
		    "ibt_join_mcg(mgid=%llx.%llx, pkey=0x%x, jstate=0x%x) "
		    "failed, ret=%d", mcg_attr.mc_mgid.gid_prefix,
		    mcg_attr.mc_mgid.gid_guid, mcg_attr.mc_pkey,
		    mcg_attr.mc_join_state, ret);

		kmem_free(mcg, sizeof (eib_mcg_t));
		ibt_free_mcg_info(data_mcginfo, 1);
		*err = EINVAL;
		return (EIB_E_FAILURE);
	}

	/*
	 * Attach to the multicast group to receive data multicast messages
	 */
	ret = ibt_attach_mcg(chan->ch_chan, data_mcginfo);
	if (ret != IBT_SUCCESS) {
		EIB_DPRINTF_WARN(ss->ei_instance,
		    "eib_vnic_attach_vhub_data: "
		    "ibt_attach_mcg(mgid=%llx.%llx, pkey=0x%x) "
		    "failed, ret=%d", mcg_attr.mc_mgid.gid_prefix,
		    mcg_attr.mc_mgid.gid_guid, mcg_attr.mc_pkey);

		(void) ibt_leave_mcg(mcg->mg_rgid, mcg->mg_mgid,
		    eib_reserved_gid, mcg->mg_join_state);
		kmem_free(mcg, sizeof (eib_mcg_t));
		ibt_free_mcg_info(data_mcginfo, 1);
		*err = EINVAL;
		return (EIB_E_FAILURE);
	}

	rw_enter(&vnic->vn_vhub_mcgs_lock, RW_WRITER);
	vnic->vn_vhub_data_mcgs = mcg;
	rw_exit(&vnic->vn_vhub_mcgs_lock);

	return (EIB_E_SUCCESS);
}

static void
eib_vnic_start_keepalives(eib_t *ss, eib_vnic_t *vnic)
{
	eib_ka_vnics_t *kav;
	eib_ka_vnics_t *elem;
	int err;

	kav = kmem_zalloc(sizeof (eib_ka_vnics_t), KM_SLEEP);
	kav->ka_vnic = vnic;
	kav->ka_next = NULL;

	/*
	 * Send the first keepalive and then queue this vnic up with
	 * the keepalives manager
	 */
	(void) eib_fip_heartbeat(ss, vnic, &err);

	mutex_enter(&ss->ei_ka_lock);
	for (elem = ss->ei_ka_vnics; elem; elem = elem->ka_next) {
		if (elem->ka_next == NULL)
			break;
	}
	if (elem) {
		elem->ka_next = kav;
	} else {
		ss->ei_ka_vnics = kav;
	}
	mutex_exit(&ss->ei_ka_lock);
}

/*ARGSUSED*/
static int
eib_vnic_lookup_dest(eib_vnic_t *vnic, uint8_t *dmac, uint16_t vlan,
    ibt_adds_vect_t *av, ib_qpn_t *dqpn, int *dtype)
{
	eib_t *ss = vnic->vn_ss;
	eib_vhub_table_t *tbl = vnic->vn_vhub_table;
	eib_login_data_t *ld = &vnic->vn_login_data;
	eib_vhub_map_t *elem;
	eib_vhub_map_t *gw;
	eib_mcg_t *mcg;
	ib_gid_t dgid;
	uint8_t bkt = (dmac[ETHERADDRL-1]) % EIB_TB_NBUCKETS;

	/*
	 * If this was a unicast dmac, locate the vhub entry matching the
	 * unicast dmac in our vhub table.  If it's not found, return the
	 * gateway entry
	 */
	if (EIB_UNICAST_MAC(dmac)) {
		*dtype = EIB_TX_UNICAST;
		bzero(av, sizeof (ibt_adds_vect_t));

		rw_enter(&tbl->tb_lock, RW_READER);

		gw = tbl->tb_gateway;
		for (elem = tbl->tb_vnic_entry[bkt]; elem != NULL;
		    elem = elem->mp_next) {
			if (bcmp(elem->mp_mac, dmac, ETHERADDRL) == 0)
				break;
		}
		if ((elem == NULL) && (gw == NULL)) {
			rw_exit(&tbl->tb_lock);
			return (EIB_E_FAILURE);
		}
		av->av_srate = IBT_SRATE_10;
		av->av_srvl = (elem) ? elem->mp_sl : gw->mp_sl;
		av->av_port_num = ss->ei_props->ep_port_num;
		av->av_send_grh = B_FALSE;
		av->av_dlid = (elem) ? elem->mp_lid : gw->mp_lid;
		av->av_src_path = 0;  /* we use base lid */

		*dqpn = (elem) ? elem->mp_qpn : gw->mp_qpn;

		rw_exit(&tbl->tb_lock);

		return (EIB_E_SUCCESS);
	}

	/*
	 * Is it a broadcast ?
	 */
	*dqpn = IB_MC_QPN;
	*dtype = (bcmp(dmac, eib_broadcast_mac, ETHERADDRL) == 0) ?
	    EIB_TX_BROADCAST : EIB_TX_MULTICAST;

	/*
	 * If this was a multicast dmac, use the address vector from the
	 * default mcginfo for this vhub ("zero-mac" mgid) and modify it
	 * by updating the av_dgid with the gid composed using the full
	 * multicast mac address we're looking for.  This way, we ensure that
	 * the address vector has MLID that corresponds to the lid of the
	 * mcg created in the gateway for this vhub, and the DGID corresponds
	 * to the intended gid (but which doesn't have a real mcg associated
	 * with it).
	 */
	eib_vnic_make_vhub_mgid(ld->ld_gw_mgid_prefix,
	    (uint8_t)EIB_MGID_VHUB_DATA, dmac, ld->ld_n_mac_mcgid,
	    0, ld->ld_vhub_id, &dgid);

	rw_enter(&vnic->vn_vhub_mcgs_lock, RW_READER);
	if ((mcg = vnic->vn_vhub_data_mcgs) == NULL) {
		rw_exit(&vnic->vn_vhub_mcgs_lock);
		EIB_DPRINTF_DEBUG(ss->ei_instance, "eib_vnic_lookup_dest: "
		    "could not find data mcg");
		return (EIB_E_FAILURE);
	}
	bcopy(&mcg->mg_mcginfo->mc_adds_vect, av, sizeof (ibt_adds_vect_t));
	av->av_dgid = dgid;
	rw_exit(&vnic->vn_vhub_mcgs_lock);

	return (EIB_E_SUCCESS);
}

static void
eib_vnic_reattach_ctl_mcgs(eib_t *ss, eib_vnic_t *vnic)
{
	/*
	 * For reattaching to control mcgs, we will not reinitialize the
	 * vhub table/vhub update we've constructed.  We'll simply detach
	 * from the table and update mcgs and reattach to them.  Hopefully,
	 * we wouldn't have missed any updates and won't have to restart
	 * the vnic.
	 */
	eib_rb_vnic_attach_vhub_table(ss, vnic);
	eib_rb_vnic_attach_vhub_update(ss, vnic);

	if (eib_vnic_attach_vhub_update(ss, vnic) != EIB_E_SUCCESS) {
		EIB_DPRINTF_WARN(ss->ei_instance,
		    "eib_vnic_reattach_ctl_mcgs: "
		    "eib_vnic_attach_vhub_update(vn_id=0x%x) failed",
		    vnic->vn_id);
	}

	if (eib_vnic_attach_vhub_table(ss, vnic) != EIB_E_SUCCESS) {
		EIB_DPRINTF_WARN(ss->ei_instance,
		    "eib_vnic_reattach_ctl_mcgs: "
		    "eib_vnic_attach_vhub_table(vn_id=0x%x) failed",
		    vnic->vn_id);

		eib_rb_vnic_attach_vhub_update(ss, vnic);
	}
}

static void
eib_vnic_reattach_vhub_data(eib_t *ss, eib_vnic_t *vnic)
{
	eib_chan_t *chan = vnic->vn_data_chan;
	eib_mcg_t *data_mcgs;
	eib_mcg_member_t *mcg_members = NULL;
	eib_mcg_member_t *elem;
	eib_mcg_member_t *next;
	ibt_mcg_info_t mcginfo;
	ibt_status_t ret;
	int err;

	/*
	 * First, save off the list of multicast ethernet addresses multiplexed
	 * onto this mcg and detach from all the mgid/mlid pairs. If the old
	 * mcg doesn't exist now, the detach could fail, but that's ok.
	 */
	rw_enter(&vnic->vn_vhub_mcgs_lock, RW_WRITER);
	if ((data_mcgs = vnic->vn_vhub_data_mcgs) != NULL) {
		mcg_members = data_mcgs->mg_elem;
		data_mcgs->mg_elem = NULL;
		data_mcgs->mg_nelem = 0;

		bcopy(data_mcgs->mg_mcginfo, &mcginfo, sizeof (ibt_mcg_info_t));
		for (elem = mcg_members; elem; elem = elem->mm_next) {
			mcginfo.mc_adds_vect.av_dgid = elem->mm_dgid;
			(void) ibt_detach_mcg(chan->ch_chan, &mcginfo);
		}
	}
	rw_exit(&vnic->vn_vhub_mcgs_lock);

	/*
	 * Rejoin the IB mcg for vhub data
	 */
	eib_rb_vnic_attach_vhub_data(ss, vnic);
	if (eib_vnic_attach_vhub_data(ss, vnic, &err) != EIB_E_SUCCESS) {
		EIB_DPRINTF_WARN(ss->ei_instance,
		    "eib_vnic_reattach_vhub_data: "
		    "eib_vnic_attach_vhub_data(vn_id=0x%x) failed, ret=%d",
		    vnic->vn_id, err);
		goto vnic_reattach_vhub_data_fail;
	}

	/*
	 * Reattach original list of mglid/mlid pairs and restore the list
	 * of multiplexed multicast addresses.
	 */
	rw_enter(&vnic->vn_vhub_mcgs_lock, RW_WRITER);
	if ((data_mcgs = vnic->vn_vhub_data_mcgs) != NULL) {
		for (elem = mcg_members; elem; elem = next) {
			next = elem->mm_next;

			mcginfo.mc_adds_vect.av_dgid = elem->mm_dgid;
			ret = ibt_attach_mcg(chan->ch_chan, &mcginfo);
			if (ret == IBT_SUCCESS) {
				elem->mm_next = data_mcgs->mg_elem;
				data_mcgs->mg_elem = elem;
				data_mcgs->mg_nelem++;
			} else {
				EIB_DPRINTF_DEBUG(ss->ei_instance,
				    "eib_vnic_reattach_vhub_data: "
				    "ibt_attach_mcg(mgid=%llx.%llx, mlid=0x%x) "
				    "failed, ret=%d",
				    mcginfo.mc_adds_vect.av_dgid,
				    mcginfo.mc_adds_vect.av_dlid, ret);

				kmem_free(elem, sizeof (eib_mcg_member_t));
			}
		}
	}
	rw_exit(&vnic->vn_vhub_mcgs_lock);

	return;

vnic_reattach_vhub_data_fail:
	for (elem = mcg_members; elem; elem = next) {
		next = elem->mm_next;
		kmem_free(elem, sizeof (eib_mcg_member_t));
	}
}

static void
eib_rb_vnic_create_common(eib_t *ss, eib_vnic_t *vnic, uint_t progress)
{
	int err;

	rw_enter(&ss->ei_vnic_lock, RW_WRITER);
	ss->ei_vnic[vnic->vn_instance] = NULL;
	ss->ei_vnic_pending = NULL;
	ss->ei_vnic_torndown = vnic;
	rw_exit(&ss->ei_vnic_lock);

	if (progress & EIB_VNIC_BROADCAST_JOINED) {
		eib_rb_vnic_join_multicast(ss, vnic, eib_broadcast_mac,
		    EIB_FLG_LEAVE_ALL_MCGS);
	}

	if (progress & EIB_VNIC_ATTACHED_TO_VHUB_DATA) {
		eib_rb_vnic_attach_vhub_data(ss, vnic);
	}

	if (progress & EIB_VNIC_KEEPALIVES_STARTED) {
		eib_rb_vnic_start_keepalives(ss, vnic);
	}

	if (progress & EIB_VNIC_ATTACHED_TO_CTL_MCGS) {
		eib_rb_vnic_attach_ctl_mcgs(ss, vnic);
	}

	if (progress & EIB_VNIC_LOGIN_DONE) {
		(void) eib_fip_logout(ss, vnic, &err);
	}

	if (progress & EIB_VNIC_DATAQP_CREATED) {
		eib_rb_data_create_qp(ss, vnic);
	}

	if (progress & EIB_VNIC_CTLQP_CREATED) {
		eib_rb_ctl_create_qp(ss, vnic);
	}

	rw_enter(&ss->ei_vnic_lock, RW_WRITER);
	ss->ei_vnic_torndown = NULL;
	rw_exit(&ss->ei_vnic_lock);
}

static void
eib_rb_vnic_attach_ctl_mcgs(eib_t *ss, eib_vnic_t *vnic)
{
	/*
	 * Detach from the vhub table and vhub update mcgs before blowing
	 * up vn_vhub_table and vn_vhub_update, since these are assumed to
	 * be available by the control cq handler.
	 */
	eib_rb_vnic_attach_vhub_table(ss, vnic);
	eib_rb_vnic_attach_vhub_update(ss, vnic);
	eib_vnic_fini_tables(ss, vnic, B_TRUE);
}

/*ARGSUSED*/
static void
eib_rb_vnic_attach_vhub_table(eib_t *ss, eib_vnic_t *vnic)
{
	eib_chan_t *chan = vnic->vn_ctl_chan;
	eib_mcg_t *mcg;
	ibt_status_t ret;

	if (chan == NULL)
		return;

	rw_enter(&vnic->vn_vhub_mcgs_lock, RW_WRITER);
	mcg = vnic->vn_vhub_tbl_mcg;
	vnic->vn_vhub_tbl_mcg = NULL;
	rw_exit(&vnic->vn_vhub_mcgs_lock);

	if (mcg) {
		ret = ibt_detach_mcg(chan->ch_chan, mcg->mg_mcginfo);
		if (ret != IBT_SUCCESS) {
			EIB_DPRINTF_VERBOSE(ss->ei_instance,
			    "eib_rb_vnic_attach_vhub_table: "
			    "ibt_detach_mcg(mcinfo=0x%llx, "
			    "mgid=%llx.%llx) failed, ret=%d",
			    mcg->mg_mcginfo, mcg->mg_mgid.gid_prefix,
			    mcg->mg_mgid.gid_guid, ret);
		}

		ret = ibt_leave_mcg(mcg->mg_rgid, mcg->mg_mgid,
		    eib_reserved_gid, mcg->mg_join_state);
		if (ret != IBT_SUCCESS) {
			EIB_DPRINTF_WARN(ss->ei_instance,
			    "eib_rb_vnic_attach_vhub_table: "
			    "ibt_leave_mcg(mgid=%llx.%llx, jstate=0x%x) "
			    "failed, ret=%d", mcg->mg_mgid.gid_prefix,
			    mcg->mg_mgid.gid_guid, mcg->mg_join_state, ret);
		}

		if (mcg->mg_mcginfo) {
			ibt_free_mcg_info(mcg->mg_mcginfo, 1);
		}
		kmem_free(mcg, sizeof (eib_mcg_t));
	}
}

/*ARGSUSED*/
static void
eib_rb_vnic_attach_vhub_update(eib_t *ss, eib_vnic_t *vnic)
{
	eib_chan_t *chan = vnic->vn_ctl_chan;
	eib_mcg_t *mcg;
	ibt_status_t ret;

	if (chan == NULL)
		return;

	rw_enter(&vnic->vn_vhub_mcgs_lock, RW_WRITER);
	mcg = vnic->vn_vhub_upd_mcg;
	vnic->vn_vhub_upd_mcg = NULL;
	rw_exit(&vnic->vn_vhub_mcgs_lock);

	if (mcg) {
		ret = ibt_detach_mcg(chan->ch_chan, mcg->mg_mcginfo);
		if (ret != IBT_SUCCESS) {
			EIB_DPRINTF_VERBOSE(ss->ei_instance,
			    "eib_rb_vnic_attach_vhub_update: "
			    "ibt_detach_mcg(mcinfo=0x%llx, "
			    "mgid=%llx.%llx) failed, ret=%d",
			    mcg->mg_mcginfo, mcg->mg_mgid.gid_prefix,
			    mcg->mg_mgid.gid_guid, ret);
		}

		ret = ibt_leave_mcg(mcg->mg_rgid, mcg->mg_mgid,
		    eib_reserved_gid, mcg->mg_join_state);
		if (ret != IBT_SUCCESS) {
			EIB_DPRINTF_WARN(ss->ei_instance,
			    "eib_rb_vnic_attach_vhub_update: "
			    "ibt_leave_mcg(mgid=%llx.%llx, jstate=0x%x) "
			    "failed, ret=%d", mcg->mg_mgid.gid_prefix,
			    mcg->mg_mgid.gid_guid, mcg->mg_join_state, ret);
		}

		if (mcg->mg_mcginfo) {
			ibt_free_mcg_info(mcg->mg_mcginfo, 1);
		}
		kmem_free(mcg, sizeof (eib_mcg_t));
	}
}

/*ARGSUSED*/
static void
eib_rb_vnic_attach_vhub_data(eib_t *ss, eib_vnic_t *vnic)
{
	eib_chan_t *chan = vnic->vn_data_chan;
	eib_mcg_t *mcg;
	ibt_status_t ret;

	if (chan == NULL)
		return;

	rw_enter(&vnic->vn_vhub_mcgs_lock, RW_WRITER);
	mcg = vnic->vn_vhub_data_mcgs;
	vnic->vn_vhub_data_mcgs = NULL;
	rw_exit(&vnic->vn_vhub_mcgs_lock);

	if (mcg) {
		ret = ibt_detach_mcg(chan->ch_chan, mcg->mg_mcginfo);
		if (ret != IBT_SUCCESS) {
			EIB_DPRINTF_VERBOSE(ss->ei_instance,
			    "eib_rb_vnic_attach_vhub_data: "
			    "ibt_detach_mcg(mcinfo=0x%llx, "
			    "mgid=%llx.%llx) failed, ret=%d",
			    mcg->mg_mcginfo, mcg->mg_mgid.gid_prefix,
			    mcg->mg_mgid.gid_guid, ret);
		}

		ret = ibt_leave_mcg(mcg->mg_rgid, mcg->mg_mgid,
		    eib_reserved_gid, mcg->mg_join_state);
		if (ret != IBT_SUCCESS) {
			EIB_DPRINTF_WARN(ss->ei_instance,
			    "eib_rb_vnic_attach_vhub_data: "
			    "ibt_leave_mcg(mgid=%llx.%llx, jstate=0x%x) "
			    "failed, ret=%d", mcg->mg_mgid.gid_prefix,
			    mcg->mg_mgid.gid_guid, mcg->mg_join_state, ret);
		}

		if (mcg->mg_mcginfo) {
			ibt_free_mcg_info(mcg->mg_mcginfo, 1);
		}
		kmem_free(mcg, sizeof (eib_mcg_t));
	}
}

/*ARGSUSED*/
static void
eib_rb_vnic_join_multicast(eib_t *ss, eib_vnic_t *vnic, uint8_t *mcast_mac,
    int flag)
{
	eib_chan_t *chan = vnic->vn_data_chan;
	eib_mcg_t *data_mcgs;
	eib_mcg_member_t *to_detach;
	eib_mcg_member_t *elem;
	eib_mcg_member_t *prev;
	eib_mcg_member_t *next;
	ibt_mcg_info_t mcginfo;
	ibt_status_t ret;

	rw_enter(&vnic->vn_vhub_mcgs_lock, RW_WRITER);

	/*
	 * Gather the list of multicast mcgs to leave (all or one)
	 */
	if ((data_mcgs = vnic->vn_vhub_data_mcgs) == NULL) {
		rw_exit(&vnic->vn_vhub_mcgs_lock);
		return;
	}
	bcopy(data_mcgs->mg_mcginfo, &mcginfo, sizeof (ibt_mcg_info_t));

	to_detach = NULL;
	if (flag == EIB_FLG_LEAVE_ALL_MCGS) {
		to_detach = data_mcgs->mg_elem;
		data_mcgs->mg_elem = NULL;
		data_mcgs->mg_nelem = 0;
	} else {
		prev = NULL;
		for (elem = data_mcgs->mg_elem; elem; elem = elem->mm_next) {
			if (bcmp(mcast_mac, elem->mm_mac, ETHERADDRL) == 0) {
				if (prev)
					prev->mm_next = elem->mm_next;
				else
					data_mcgs->mg_elem = elem->mm_next;

				elem->mm_next = NULL;
				to_detach = elem;

				ASSERT(data_mcgs->mg_nelem > 0);
				data_mcgs->mg_nelem--;

				break;
			}
			prev = elem;
		}
	}

	/*
	 * Detach (free) all identified mcgs
	 */
	for (elem = to_detach; elem; elem = next) {
		next = elem->mm_next;

		mcginfo.mc_adds_vect.av_dgid = elem->mm_dgid;
		ret = ibt_detach_mcg(chan->ch_chan, &mcginfo);
		if (ret != IBT_SUCCESS) {
			EIB_DPRINTF_VERBOSE(ss->ei_instance,
			    "eib_rb_vnic_join_multicast: "
			    "ibt_detach_mcg(mgid=%llx.%llx, mlid=0x%x) "
			    "failed, ret=%d", mcginfo.mc_adds_vect.av_dgid,
			    mcginfo.mc_adds_vect.av_dlid, ret);
		}
		kmem_free(elem, sizeof (eib_mcg_member_t));
	}

	rw_exit(&vnic->vn_vhub_mcgs_lock);
}

/*ARGSUSED*/
static void
eib_rb_vnic_start_keepalives(eib_t *ss, eib_vnic_t *vnic)
{
	eib_ka_vnics_t *prev;
	eib_ka_vnics_t *elem;

	/*
	 * We only need to locate and remove the vnic entry from the
	 * keepalives manager list
	 */

	mutex_enter(&ss->ei_ka_lock);

	prev = NULL;
	for (elem = ss->ei_ka_vnics; elem; elem = elem->ka_next) {
		if (elem->ka_vnic == vnic)
			break;

		prev = elem;
	}
	if (elem == NULL) {
		EIB_DPRINTF_DEBUG(ss->ei_instance,
		    "eib_rb_vnic_start_keepalives: no keepalive element found "
		    "for vnic 0x%llx (vn_inst=%d) with keepalive manager",
		    vnic, vnic->vn_instance);
	} else {
		if (prev) {
			prev->ka_next = elem->ka_next;
		} else {
			ss->ei_ka_vnics = elem->ka_next;
		}
		kmem_free(elem, sizeof (eib_ka_vnics_t));
	}
	mutex_exit(&ss->ei_ka_lock);
}
