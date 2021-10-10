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
 * Copyright(c) 2007-2011 Intel Corporation. All rights reserved.
 */

/*
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include "igbvf_sw.h"

static int igbvf_add_mac(void *, const uint8_t *, uint64_t);
static int igbvf_rem_mac(void *, const uint8_t *);
static void igbvf_fill_ring(void *, mac_ring_type_t, const int, const int,
    mac_ring_info_t *, mac_ring_handle_t);
static void igbvf_fill_group(void *arg, mac_ring_type_t, const int,
    mac_group_info_t *, mac_group_handle_t);
static void igbvf_m_getfactaddr(void *, uint_t, uint8_t *);
static int igbvf_get_rx_ring_index(igbvf_t *, int, int);
static int igbvf_set_priv_prop(igbvf_t *, const char *, uint_t, const void *);
static int igbvf_get_priv_prop(igbvf_t *, const char *, uint_t, void *);
static void igbvf_priv_prop_info(igbvf_t *, const char *,
    mac_prop_info_handle_t);

/* ARGSUSED */
int
igbvf_m_stat(void *arg, uint_t stat, uint64_t *val)
{
	igbvf_t *igbvf = (igbvf_t *)arg;
	struct e1000_hw *hw = &igbvf->hw;
	igbvf_stat_t *igbvf_ks;

	igbvf_ks = (igbvf_stat_t *)igbvf->igbvf_ks->ks_data;

	mutex_enter(&igbvf->gen_lock);

	if (IGBVF_IS_SUSPENDED_PIO(igbvf)) {
		mutex_exit(&igbvf->gen_lock);
		return (ECANCELED);
	}

	switch (stat) {
	case MAC_STAT_IFSPEED:
		*val = igbvf->link_speed * 1000000ull;
		break;

	case MAC_STAT_MULTIRCV:
		igbvf_get_vf_stat(hw, E1000_VFMPRC, &igbvf_ks->mprc.value.ui64);
		*val = igbvf_ks->mprc.value.ui64;
		break;

	case MAC_STAT_RBYTES:
		igbvf_get_vf_stat(hw, E1000_VFGORC, &igbvf_ks->gorc.value.ui64);
		*val = igbvf_ks->gorc.value.ui64;
		break;

	case MAC_STAT_IPACKETS:
		igbvf_get_vf_stat(hw, E1000_VFGPRC, &igbvf_ks->gprc.value.ui64);
		*val = igbvf_ks->gprc.value.ui64;
		break;

	case MAC_STAT_OBYTES:
		igbvf_get_vf_stat(hw, E1000_VFGOTC, &igbvf_ks->gotc.value.ui64);
		*val = igbvf_ks->gotc.value.ui64;
		break;

	case MAC_STAT_OPACKETS:
		igbvf_get_vf_stat(hw, E1000_VFGPTC, &igbvf_ks->gptc.value.ui64);
		*val = igbvf_ks->gptc.value.ui64;
		break;

	case ETHER_STAT_LINK_DUPLEX:
		if (igbvf->link_duplex)
			*val = (igbvf->link_duplex == FULL_DUPLEX) ?
			    LINK_DUPLEX_FULL : LINK_DUPLEX_HALF;
		else
			*val = LINK_DUPLEX_UNKNOWN;
		break;

	default:
		mutex_exit(&igbvf->gen_lock);
		return (ENOTSUP);
	}

	mutex_exit(&igbvf->gen_lock);

	if (igbvf_check_acc_handle(igbvf->osdep.reg_handle) != DDI_FM_OK)
		ddi_fm_service_impact(igbvf->dip, DDI_SERVICE_UNAFFECTED);

	return (0);
}

/*
 * Bring the device out of the reset/quiesced state that it
 * was in when the interface was registered.
 */
int
igbvf_m_start(void *arg)
{
	igbvf_t *igbvf = (igbvf_t *)arg;

	mutex_enter(&igbvf->gen_lock);

	if (IGBVF_IS_SUSPENDED(igbvf)) {
		igbvf->sr_reconfigure = igbvf->sr_reconfigure &
		    ~IGBVF_SR_RC_STOP | IGBVF_SR_RC_START;
		mutex_exit(&igbvf->gen_lock);
		return (0);
	}

	if (igbvf_start(igbvf) != IGBVF_SUCCESS) {
		mutex_exit(&igbvf->gen_lock);
		return (EIO);
	}

	atomic_or_32(&igbvf->igbvf_state, IGBVF_STARTED);

	mutex_exit(&igbvf->gen_lock);

	return (0);
}

/*
 * Stop the device and put it in a reset/quiesced state such
 * that the interface can be unregistered.
 */
void
igbvf_m_stop(void *arg)
{
	igbvf_t *igbvf = (igbvf_t *)arg;

	mutex_enter(&igbvf->gen_lock);

	if (IGBVF_IS_SUSPENDED(igbvf)) {
		igbvf->sr_reconfigure = igbvf->sr_reconfigure &
		    ~IGBVF_SR_RC_START | IGBVF_SR_RC_STOP;
		mutex_exit(&igbvf->gen_lock);
		return;
	}

	atomic_and_32(&igbvf->igbvf_state, ~IGBVF_STARTED);

	igbvf_stop(igbvf);

	mutex_exit(&igbvf->gen_lock);
}

/*
 * Set the promiscuity of the device.
 */
int
igbvf_m_promisc(void *arg, boolean_t on)
{
	igbvf_t *igbvf = (igbvf_t *)arg;
	boolean_t prev_mode;
	int ret;

	mutex_enter(&igbvf->gen_lock);

	prev_mode = igbvf->promisc_mode;
	igbvf->promisc_mode = on;

	if (IGBVF_IS_SUSPENDED_PIO(igbvf)) {
		/* We've saved the setting and it'll take effect in resume */
		mutex_exit(&igbvf->gen_lock);
		return (0);
	}

	/*
	 * Always request multicast promiscuous.
	 * Unicast promiscuous is not supported.
	 */
	ret = igbvf_set_mcast_promisc(igbvf, on);
	if (ret != IGBVF_SUCCESS) {
		igbvf->promisc_mode = prev_mode;
		mutex_exit(&igbvf->gen_lock);
		IGBVF_DEBUGLOG_0(igbvf,
		    "Failed to set multicast promiscuous mode");
		return (EIO);
	}

	mutex_exit(&igbvf->gen_lock);

	return (0);
}

/*
 * Add/remove the addresses to/from the set of multicast
 * addresses for which the device will receive packets.
 */
int
igbvf_m_multicst(void *arg, boolean_t add, const uint8_t *mcst_addr)
{
	igbvf_t *igbvf = (igbvf_t *)arg;
	int result;

	mutex_enter(&igbvf->gen_lock);

	result = (add) ? igbvf_multicst_add(igbvf, mcst_addr)
	    : igbvf_multicst_remove(igbvf, mcst_addr);

	if (result != 0)
		goto multicst_end;

	if (IGBVF_IS_SUSPENDED_PIO(igbvf)) {
		/* We've saved the setting and it'll take effect in resume */
		goto multicst_end;
	}

	/*
	 * Request PF to update the multicast table in hardware
	 */
	if (igbvf_setup_multicst(igbvf) != IGBVF_SUCCESS) {
		/* Restore the previous software state */
		if (add)
			(void) igbvf_multicst_remove(igbvf, mcst_addr);
		else
			(void) igbvf_multicst_add(igbvf, mcst_addr);
		result = EIO;
	}

multicst_end:
	mutex_exit(&igbvf->gen_lock);

	return (result);
}

/*
 * Pass on M_IOCTL messages passed to the DLD, and support
 * private IOCTLs for debugging and ndd.
 */
void
igbvf_m_ioctl(void *arg, queue_t *q, mblk_t *mp)
{
	igbvf_t *igbvf = (igbvf_t *)arg;
	struct iocblk *iocp;
	enum ioc_reply status;

	iocp = (struct iocblk *)(uintptr_t)mp->b_rptr;
	iocp->ioc_error = 0;

	mutex_enter(&igbvf->gen_lock);
	if (IGBVF_IS_SUSPENDED(igbvf)) {
		mutex_exit(&igbvf->gen_lock);
		miocnak(q, mp, 0, EINVAL);
		return;
	}
	mutex_exit(&igbvf->gen_lock);

	switch (iocp->ioc_cmd) {
	case LB_GET_INFO_SIZE:
	case LB_GET_INFO:
	case LB_GET_MODE:
	case LB_SET_MODE:
		igbvf_log(igbvf, "Loopback is not supported");
		status = IOC_INVAL;
		break;

	default:
		status = IOC_INVAL;
		break;
	}

	/*
	 * Decide how to reply
	 */
	switch (status) {
	default:
	case IOC_INVAL:
		/*
		 * Error, reply with a NAK and EINVAL or the specified error
		 */
		miocnak(q, mp, 0, iocp->ioc_error == 0 ?
		    EINVAL : iocp->ioc_error);
		break;

	case IOC_DONE:
		/*
		 * OK, reply already sent
		 */
		break;

	case IOC_ACK:
		/*
		 * OK, reply with an ACK
		 */
		miocack(q, mp, 0, 0);
		break;

	case IOC_REPLY:
		/*
		 * OK, send prepared reply as ACK or NAK
		 */
		mp->b_datap->db_type = iocp->ioc_error == 0 ?
		    M_IOCACK : M_IOCNAK;
		qreply(q, mp);
		break;
	}
}

/*
 * Add a MAC address to the target RX group.
 */
static int
igbvf_add_mac(void *arg, const uint8_t *mac_addr, uint64_t flags)
{
	_NOTE(ARGUNUSED(flags))

	igbvf_rx_group_t *rx_group = (igbvf_rx_group_t *)arg;
	igbvf_t *igbvf = rx_group->igbvf;
	int ret;

	mutex_enter(&igbvf->gen_lock);

	/* add given address to list */
	ret = igbvf_unicst_add(igbvf, mac_addr);

	mutex_exit(&igbvf->gen_lock);
	return (ret);
}

/*
 * Remove a MAC address from the specified RX group.
 */
static int
igbvf_rem_mac(void *arg, const uint8_t *mac_addr)
{
	igbvf_rx_group_t *rx_group = (igbvf_rx_group_t *)arg;
	igbvf_t *igbvf = rx_group->igbvf;
	int ret;

	mutex_enter(&igbvf->gen_lock);

	/* remove given address */
	ret = igbvf_unicst_remove(igbvf, mac_addr);

	mutex_exit(&igbvf->gen_lock);

	return (ret);
}

/*
 * Enable interrupt on the specified rx ring.
 */
int
igbvf_rx_ring_intr_enable(mac_ring_driver_t rh)
{
	igbvf_rx_ring_t *rx_ring = (igbvf_rx_ring_t *)rh;
	igbvf_t *igbvf = rx_ring->igbvf;
	struct e1000_hw *hw = &igbvf->hw;
	uint32_t vect_bit = (1 << rx_ring->intr_vect);

	mutex_enter(&igbvf->gen_lock);

	igbvf->eims_mask |= vect_bit;

	if (IGBVF_IS_SUSPENDED_INTR(igbvf)) {
		/* We've saved the setting and it'll take effect in resume */
		mutex_exit(&igbvf->gen_lock);
		return (0);
	}

	/* Interrupt enabling for MSI-X */
	E1000_WRITE_REG(hw, E1000_EIMS, igbvf->eims_mask);
	E1000_WRITE_REG(hw, E1000_EIAC, igbvf->eims_mask);

	E1000_WRITE_FLUSH(hw);

	mutex_exit(&igbvf->gen_lock);

	return (0);
}

/*
 * Disable interrupt on the specified rx ring.
 */
int
igbvf_rx_ring_intr_disable(mac_ring_driver_t rh)
{
	igbvf_rx_ring_t *rx_ring = (igbvf_rx_ring_t *)rh;
	igbvf_t *igbvf = rx_ring->igbvf;
	struct e1000_hw *hw = &igbvf->hw;
	uint32_t vect_bit = (1 << rx_ring->intr_vect);

	mutex_enter(&igbvf->gen_lock);

	igbvf->eims_mask &= ~vect_bit;

	if (IGBVF_IS_SUSPENDED_INTR(igbvf)) {
		/* We've saved the setting and it'll take effect in resume */
		mutex_exit(&igbvf->gen_lock);
		return (0);
	}

	/* Interrupt disabling for MSI-X */
	E1000_WRITE_REG(hw, E1000_EIMC, vect_bit);

	E1000_WRITE_REG(hw, E1000_EIAC, igbvf->eims_mask);

	E1000_WRITE_FLUSH(hw);

	mutex_exit(&igbvf->gen_lock);

	return (0);
}

/*
 * Get the global ring index by a ring index within a group.
 */
static int
igbvf_get_rx_ring_index(igbvf_t *igbvf, int gindex, int rindex)
{
	igbvf_rx_ring_t *rx_ring;
	int i;

	for (i = 0; i < igbvf->num_rx_rings; i++) {
		rx_ring = &igbvf->rx_rings[i];
		if (rx_ring->group_index == gindex)
			rindex--;
		if (rindex < 0) {
			return (i);
		}
	}
	return (-1);
}

static int
igbvf_ring_start(mac_ring_driver_t rh, uint64_t mr_gen_num)
{
	igbvf_rx_ring_t *rx_ring = (igbvf_rx_ring_t *)rh;

	mutex_enter(&rx_ring->rx_lock);
	rx_ring->ring_gen_num = mr_gen_num;
	mutex_exit(&rx_ring->rx_lock);
	return (0);
}

/*
 * Callback funtion for MAC layer to register all rings.
 */
/* ARGSUSED */
static void
igbvf_fill_ring(void *arg, mac_ring_type_t rtype, const int rg_index,
    const int index, mac_ring_info_t *infop, mac_ring_handle_t rh)
{
	igbvf_t *igbvf = (igbvf_t *)arg;
	mac_intr_t *mintr = &infop->mri_intr;

	switch (rtype) {
	case MAC_RING_TYPE_RX: {
		igbvf_rx_ring_t *rx_ring;
		int global_index;

		/*
		 * 'index' is the ring index within the group.
		 * We need the global ring index by searching in group.
		 */
		global_index = igbvf_get_rx_ring_index(igbvf, rg_index, index);
		ASSERT(global_index >= 0);

		rx_ring = &igbvf->rx_rings[global_index];
		rx_ring->ring_handle = rh;

		infop->mri_driver = (mac_ring_driver_t)rx_ring;
		infop->mri_start = igbvf_ring_start;
		infop->mri_stop = NULL;
		infop->mri_poll = (mac_ring_poll_t)igbvf_rx_ring_poll;
		infop->mri_stat = igbvf_rx_ring_stat;

		mintr->mi_enable = igbvf_rx_ring_intr_enable;
		mintr->mi_disable = igbvf_rx_ring_intr_disable;
		mintr->mi_ddi_handle = igbvf->htable[rx_ring->intr_vect];

		break;
	}
	case MAC_RING_TYPE_TX: {
		ASSERT(index < igbvf->num_tx_rings);

		igbvf_tx_ring_t *tx_ring = &igbvf->tx_rings[index];
		tx_ring->ring_handle = rh;

		infop->mri_driver = (mac_ring_driver_t)tx_ring;
		infop->mri_start = NULL;
		infop->mri_stop = NULL;
		infop->mri_tx = igbvf_tx_ring_send;
		infop->mri_stat = igbvf_tx_ring_stat;
		mintr->mi_ddi_handle = igbvf->htable[tx_ring->intr_vect];

		break;
	}
	default:
		break;
	}
}

static void
igbvf_fill_group(void *arg, mac_ring_type_t rtype, const int index,
    mac_group_info_t *infop, mac_group_handle_t gh)
{
	igbvf_t *igbvf = (igbvf_t *)arg;
	igbvf_rx_group_t *rx_group;

	ASSERT(rtype == MAC_RING_TYPE_RX);
	ASSERT((index >= 0) && (index < igbvf->num_rx_groups));

	rx_group = &igbvf->rx_groups[index];
	rx_group->group_handle = gh;

	infop->mgi_driver = (mac_group_driver_t)rx_group;
	infop->mgi_start = NULL;
	infop->mgi_stop = NULL;
	infop->mgi_addmac = igbvf_add_mac;
	infop->mgi_remmac = igbvf_rem_mac;
	infop->mgi_count = igbvf->ring_per_group;
	if (index == 0)
		infop->mgi_flags = MAC_GROUP_DEFAULT;
}

/*
 * The callback to query all the factory addresses. naddr must be the same as
 * the number of factory addresses (returned by MAC_CAPAB_MULTIFACTADDR), and
 * "addr" is the space allocated to keep all the addresses, whose size is
 * naddr * MAXMACADDRLEN.
 */
static void
igbvf_m_getfactaddr(void *arg, uint_t naddr, uint8_t *addr)
{
	igbvf_t *igbvf = (igbvf_t *)arg;
	int i;

	ASSERT(naddr == (igbvf->unicst_total - 1));

	for (i = 0; i < naddr; i++) {
		bcopy(igbvf->unicst_addr[i + 1].addr,
		    addr + (i * MAXMACADDRLEN), ETHERADDRL);
	}
}

/*
 * Obtain the MAC's capabilities and associated data from
 * the driver.
 */
boolean_t
igbvf_m_getcapab(void *arg, mac_capab_t cap, void *cap_data)
{
	igbvf_t *igbvf = (igbvf_t *)arg;

	switch (cap) {
	case MAC_CAPAB_HCKSUM: {
		uint32_t *tx_hcksum_flags = cap_data;

		/*
		 * We advertise our capabilities only if tx hcksum offload is
		 * enabled.  On receive, the stack will accept checksummed
		 * packets anyway, even if we haven't said we can deliver
		 * them.
		 */
		if (!igbvf->tx_hcksum_enable)
			return (B_FALSE);

		*tx_hcksum_flags = HCKSUM_INET_PARTIAL | HCKSUM_IPHDRCKSUM;
		break;
	}
	case MAC_CAPAB_LSO: {
		mac_capab_lso_t *cap_lso = cap_data;

		if (igbvf->lso_enable) {
			cap_lso->lso_flags = LSO_TX_BASIC_TCP_IPV4;
			cap_lso->lso_basic_tcp_ipv4.lso_max = IGBVF_LSO_MAXLEN;
			break;
		} else {
			return (B_FALSE);
		}
	}
	case MAC_CAPAB_MULTIFACTADDR: {
		mac_capab_multifactaddr_t *mfacp = cap_data;

		mfacp->mcm_naddr = igbvf->unicst_total - 1;
		mfacp->mcm_getaddr = igbvf_m_getfactaddr;

		break;
	}
	case MAC_CAPAB_RINGS: {
		mac_capab_rings_t *cap_rings = cap_data;

		cap_rings->mr_version = MAC_RINGS_VERSION_1;

		switch (cap_rings->mr_type) {
		case MAC_RING_TYPE_RX:
			cap_rings->mr_group_type = MAC_GROUP_TYPE_STATIC;
			cap_rings->mr_rnum = igbvf->num_rx_rings;
			cap_rings->mr_gnum = igbvf->num_rx_groups;
			cap_rings->mr_rget = igbvf_fill_ring;
			cap_rings->mr_gget = igbvf_fill_group;
			cap_rings->mr_gaddring = NULL;
			cap_rings->mr_gremring = NULL;
			break;
		case MAC_RING_TYPE_TX:
			cap_rings->mr_group_type = MAC_GROUP_TYPE_STATIC;
			cap_rings->mr_rnum = igbvf->num_tx_rings;
			cap_rings->mr_gnum = 1;
			cap_rings->mr_rget = igbvf_fill_ring;
			cap_rings->mr_gget = NULL;
			break;
		default:
			break;
		}
		break;
	}

	default:
		return (B_FALSE);
	}
	return (B_TRUE);
}

int
igbvf_m_setprop(void *arg, const char *pr_name, mac_prop_id_t pr_num,
    uint_t pr_valsize, const void *pr_val)
{
	igbvf_t *igbvf = (igbvf_t *)arg;
	int err = 0;
	uint32_t cur_mtu, new_mtu;
	uint32_t rx_size;
	uint32_t tx_size;

	mutex_enter(&igbvf->gen_lock);

	switch (pr_num) {
	case MAC_PROP_MTU:
		/* adapter must be stopped for an MTU change */
		if (IGBVF_IS_STARTED(igbvf)) {
			err = EBUSY;
			break;
		}

		cur_mtu = igbvf->default_mtu;
		bcopy(pr_val, &new_mtu, sizeof (new_mtu));
		if (new_mtu == cur_mtu) {
			err = 0;
			break;
		}

		if ((new_mtu < igbvf->min_mtu) ||
		    (new_mtu > igbvf->max_mtu)) {
			err = EINVAL;
			break;
		}

		err = mac_maxsdu_update(igbvf->mac_hdl, new_mtu);
		if (err == 0) {
			igbvf->default_mtu = new_mtu;
			igbvf->max_frame_size = igbvf->default_mtu +
			    sizeof (struct ether_vlan_header) + ETHERFCSL;

			/*
			 * Set rx buffer size
			 */
			rx_size = igbvf->max_frame_size + IPHDR_ALIGN_ROOM;
			igbvf->rx_buf_size = ((rx_size >> 10) + ((rx_size &
			    (((uint32_t)1 << 10) - 1)) > 0 ? 1 : 0)) << 10;

			/*
			 * Set tx buffer size
			 */
			tx_size = igbvf->max_frame_size;
			igbvf->tx_buf_size = ((tx_size >> 10) + ((tx_size &
			    (((uint32_t)1 << 10) - 1)) > 0 ? 1 : 0)) << 10;
		}
		break;
	case MAC_PROP_PRIVATE:
		err = igbvf_set_priv_prop(igbvf, pr_name, pr_valsize, pr_val);
		break;
	default:
		err = ENOTSUP;
		break;
	}

	mutex_exit(&igbvf->gen_lock);

	return (err);
}

int
igbvf_m_getprop(void *arg, const char *pr_name, mac_prop_id_t pr_num,
    uint_t pr_valsize, void *pr_val)
{
	igbvf_t *igbvf = (igbvf_t *)arg;
	int err = 0;

	switch (pr_num) {
	case MAC_PROP_PRIVATE:
		err = igbvf_get_priv_prop(igbvf, pr_name, pr_valsize, pr_val);
		break;
	default:
		err = ENOTSUP;
		break;
	}
	return (err);
}

/* ARGSUSED */
void
igbvf_m_propinfo(void *arg, const char *pr_name, mac_prop_id_t pr_num,
    mac_prop_info_handle_t prh)
{
	igbvf_t *igbvf = (igbvf_t *)arg;

	switch (pr_num) {
	case MAC_PROP_MTU:
		mac_prop_info_set_range_uint32(prh,
		    igbvf->min_mtu, igbvf->max_mtu);
		break;

	case MAC_PROP_PRIVATE:
		igbvf_priv_prop_info(igbvf, pr_name, prh);
		break;
	default:
		break;
	}
}

/* ARGSUSED */
static int
igbvf_set_priv_prop(igbvf_t *igbvf, const char *pr_name,
    uint_t pr_valsize, const void *pr_val)
{
	int err = 0;
	long result;
	struct e1000_hw *hw = &igbvf->hw;
	int i;

	if (strcmp(pr_name, "_tx_copy_thresh") == 0) {
		if (pr_val == NULL) {
			err = EINVAL;
			return (err);
		}
		(void) ddi_strtol(pr_val, (char **)NULL, 0, &result);
		if (result < MIN_TX_COPY_THRESHOLD ||
		    result > MAX_TX_COPY_THRESHOLD)
			err = EINVAL;
		else {
			igbvf->tx_copy_thresh = (uint32_t)result;
		}
		return (err);
	}
	if (strcmp(pr_name, "_tx_recycle_thresh") == 0) {
		if (pr_val == NULL) {
			err = EINVAL;
			return (err);
		}
		(void) ddi_strtol(pr_val, (char **)NULL, 0, &result);
		if (result < MIN_TX_RECYCLE_THRESHOLD ||
		    result > MAX_TX_RECYCLE_THRESHOLD)
			err = EINVAL;
		else {
			igbvf->tx_recycle_thresh = (uint32_t)result;
		}
		return (err);
	}
	if (strcmp(pr_name, "_tx_overload_thresh") == 0) {
		if (pr_val == NULL) {
			err = EINVAL;
			return (err);
		}
		(void) ddi_strtol(pr_val, (char **)NULL, 0, &result);
		if (result < MIN_TX_OVERLOAD_THRESHOLD ||
		    result > MAX_TX_OVERLOAD_THRESHOLD)
			err = EINVAL;
		else {
			igbvf->tx_overload_thresh = (uint32_t)result;
		}
		return (err);
	}
	if (strcmp(pr_name, "_tx_resched_thresh") == 0) {
		if (pr_val == NULL) {
			err = EINVAL;
			return (err);
		}
		(void) ddi_strtol(pr_val, (char **)NULL, 0, &result);
		if (result < MIN_TX_RESCHED_THRESHOLD ||
		    result > MAX_TX_RESCHED_THRESHOLD ||
		    result > igbvf->tx_ring_size)
			err = EINVAL;
		else {
			igbvf->tx_resched_thresh = (uint32_t)result;
		}
		return (err);
	}
	if (strcmp(pr_name, "_rx_copy_thresh") == 0) {
		if (pr_val == NULL) {
			err = EINVAL;
			return (err);
		}
		(void) ddi_strtol(pr_val, (char **)NULL, 0, &result);
		if (result < MIN_RX_COPY_THRESHOLD ||
		    result > MAX_RX_COPY_THRESHOLD)
			err = EINVAL;
		else {
			igbvf->rx_copy_thresh = (uint32_t)result;
		}
		return (err);
	}
	if (strcmp(pr_name, "_rx_limit_per_intr") == 0) {
		if (pr_val == NULL) {
			err = EINVAL;
			return (err);
		}
		(void) ddi_strtol(pr_val, (char **)NULL, 0, &result);
		if (result < MIN_RX_LIMIT_PER_INTR ||
		    result > MAX_RX_LIMIT_PER_INTR)
			err = EINVAL;
		else {
			igbvf->rx_limit_per_intr = (uint32_t)result;
		}
		return (err);
	}
	if (strcmp(pr_name, "_intr_throttling") == 0) {
		if (pr_val == NULL) {
			err = EINVAL;
			return (err);
		}
		(void) ddi_strtol(pr_val, (char **)NULL, 0, &result);

		if (result < igbvf->capab->min_intr_throttle ||
		    result > igbvf->capab->max_intr_throttle)
			err = EINVAL;
		else if (result != igbvf->intr_throttling[0]) {
			igbvf->intr_throttling[0] = (uint32_t)result;

			for (i = 0; i < MAX_NUM_EITR; i++)
				igbvf->intr_throttling[i] =
				    igbvf->intr_throttling[0];

			if (IGBVF_IS_SUSPENDED_PIO(igbvf)) {
				/* The setting will take effect in resume */
				return (err);
			}

			/* Set interrupt throttling rate */
			for (i = 0; i < igbvf->intr_cnt; i++)
				E1000_WRITE_REG(hw, E1000_EITR(i),
				    igbvf->intr_throttling[i]);

			if (igbvf_check_acc_handle(igbvf->osdep.reg_handle) !=
			    DDI_FM_OK) {
				ddi_fm_service_impact(igbvf->dip,
				    DDI_SERVICE_DEGRADED);
				return (EIO);
			}
		}
		return (err);
	}
	return (ENOTSUP);
}

static int
igbvf_get_priv_prop(igbvf_t *igbvf, const char *pr_name, uint_t pr_valsize,
    void *pr_val)
{
	int value;

	if (strcmp(pr_name, "_tx_copy_thresh") == 0) {
		value = igbvf->tx_copy_thresh;
	} else if (strcmp(pr_name, "_tx_recycle_thresh") == 0) {
		value = igbvf->tx_recycle_thresh;
	} else if (strcmp(pr_name, "_tx_overload_thresh") == 0) {
		value = igbvf->tx_overload_thresh;
	} else if (strcmp(pr_name, "_tx_resched_thresh") == 0) {
		value = igbvf->tx_resched_thresh;
	} else if (strcmp(pr_name, "_rx_copy_thresh") == 0) {
		value = igbvf->rx_copy_thresh;
	} else if (strcmp(pr_name, "_rx_limit_per_intr") == 0) {
		value = igbvf->rx_limit_per_intr;
	} else if (strcmp(pr_name, "_intr_throttling") == 0) {
		value = igbvf->intr_throttling[0];
	} else {
		return (ENOTSUP);
	}

	(void) snprintf(pr_val, pr_valsize, "%d", value);
	return (0);
}

static void
igbvf_priv_prop_info(igbvf_t *igbvf, const char *pr_name,
    mac_prop_info_handle_t prh)
{
	char valstr[64];
	int value;

	if (strcmp(pr_name, "_tx_copy_thresh") == 0) {
		value = DEFAULT_TX_COPY_THRESHOLD;
	} else if (strcmp(pr_name, "_tx_recycle_thresh") == 0) {
		value = DEFAULT_TX_RECYCLE_THRESHOLD;
	} else if (strcmp(pr_name, "_tx_overload_thresh") == 0) {
		value = DEFAULT_TX_OVERLOAD_THRESHOLD;
	} else if (strcmp(pr_name, "_tx_resched_thresh") == 0) {
		value = DEFAULT_TX_RESCHED_THRESHOLD;
	} else if (strcmp(pr_name, "_rx_copy_thresh") == 0) {
		value = DEFAULT_RX_COPY_THRESHOLD;
	} else if (strcmp(pr_name, "_rx_limit_per_intr") == 0) {
		value = DEFAULT_RX_LIMIT_PER_INTR;
	} else if (strcmp(pr_name, "_intr_throttling") == 0) {
		value = igbvf->capab->def_intr_throttle;
	} else {
		return;
	}

	(void) snprintf(valstr, sizeof (valstr), "%d", value);
	mac_prop_info_set_default_str(prh, valstr);
}
