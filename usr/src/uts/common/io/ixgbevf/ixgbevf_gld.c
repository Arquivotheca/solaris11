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

#include "ixgbevf_sw.h"

#ifdef DEBUG_INTEL
/*
 * hack for ethregs: save all dev_info_t's in a global array
 */
extern dev_info_t *GlobalDip;
extern dev_info_t *GlobalDipArray[];
#endif  /* DEBUG_INTEL */

/*
 * Bring the device out of the reset/quiesced state that it
 * was in when the interface was registered.
 */
int
ixgbevf_m_start(void *arg)
{
	ixgbevf_t *ixgbevf = (ixgbevf_t *)arg;

	if (!mutex_tryenter(&ixgbevf->gen_lock))
		return (EAGAIN);

	if (ixgbevf->ixgbevf_state & IXGBE_SUSPENDED) {
		mutex_exit(&ixgbevf->gen_lock);
		return (ECANCELED);
	}

	if (ixgbevf_start(ixgbevf, B_TRUE) != IXGBE_SUCCESS) {
		mutex_exit(&ixgbevf->gen_lock);
		return (EIO);
	}

	atomic_or_32(&ixgbevf->ixgbevf_state, IXGBE_STARTED);

	mutex_exit(&ixgbevf->gen_lock);

	/*
	 * Enable and start the watchdog timer
	 */
	ixgbevf_enable_watchdog_timer(ixgbevf);

	return (0);
}

/*
 * Stop the device and put it in a reset/quiesced state such
 * that the interface can be unregistered.
 */
void
ixgbevf_m_stop(void *arg)
{
	ixgbevf_t *ixgbevf = (ixgbevf_t *)arg;

	mutex_enter(&ixgbevf->gen_lock);

	if (ixgbevf->ixgbevf_state & IXGBE_SUSPENDED) {
		mutex_exit(&ixgbevf->gen_lock);
		return;
	}

	atomic_and_32(&ixgbevf->ixgbevf_state, ~IXGBE_STARTED);

	ixgbevf_stop(ixgbevf, B_TRUE);

	mutex_exit(&ixgbevf->gen_lock);

	/*
	 * Disable and stop the watchdog timer
	 */
	ixgbevf_disable_watchdog_timer(ixgbevf);
}

/*
 * Set the promiscuity of the device.
 */
int
ixgbevf_m_promisc(void *arg, boolean_t on)
{
	ixgbevf_t *ixgbevf = (ixgbevf_t *)arg;

	mutex_enter(&ixgbevf->gen_lock);

	if (ixgbevf->ixgbevf_state & IXGBE_SUSPENDED) {
		mutex_exit(&ixgbevf->gen_lock);
		return (ECANCELED);
	}

	/*
	 * Always request both multicast and unicast promiscuous.
	 * Since unicast is very likely to be denied by PF,
	 * ignore failure of unicast setting.
	 */
	if (ixgbevf_set_mcast_promisc(ixgbevf, on)) {
		mutex_exit(&ixgbevf->gen_lock);
		IXGBE_DEBUGLOG_0(ixgbevf,
		    "Failed to set multicast promiscuous mode");
		return (EIO);
	}

	ixgbevf->promisc_mode = on;

	mutex_exit(&ixgbevf->gen_lock);

	return (0);
}

/*
 * Add/remove the addresses to/from the set of multicast
 * addresses for which the device will receive packets.
 */
int
ixgbevf_m_multicst(void *arg, boolean_t add, const uint8_t *mcst_addr)
{
	ixgbevf_t *ixgbevf = (ixgbevf_t *)arg;
	int result;

	mutex_enter(&ixgbevf->gen_lock);

	if (ixgbevf->ixgbevf_state & IXGBE_SUSPENDED) {
		mutex_exit(&ixgbevf->gen_lock);
		return (ECANCELED);
	}

	result = (add) ? ixgbevf_multicst_add(ixgbevf, mcst_addr)
	    : ixgbevf_multicst_remove(ixgbevf, mcst_addr);

	mutex_exit(&ixgbevf->gen_lock);

	return (result);
}

/*
 * Pass on M_IOCTL messages passed to the DLD, and support
 * private IOCTLs for debugging and ndd.
 */
void
ixgbevf_m_ioctl(void *arg, queue_t *q, mblk_t *mp)
{
	ixgbevf_t *ixgbevf = (ixgbevf_t *)arg;
	struct iocblk *iocp;
	enum ioc_reply status;

	iocp = (struct iocblk *)(uintptr_t)mp->b_rptr;
	iocp->ioc_error = 0;

	mutex_enter(&ixgbevf->gen_lock);
	if (ixgbevf->ixgbevf_state & IXGBE_SUSPENDED) {
		mutex_exit(&ixgbevf->gen_lock);
		miocnak(q, mp, 0, EINVAL);
		return;
	}
	mutex_exit(&ixgbevf->gen_lock);

	switch (iocp->ioc_cmd) {
	case LB_GET_INFO_SIZE:
	case LB_GET_INFO:
	case LB_GET_MODE:
	case LB_SET_MODE:
		break;

#ifdef DEBUG_INTEL
	/*
	 * return adapter device id to caller; used by ethregs
	 */
	case IXGBE_IOC_DEV_ID:
		if (iocp->ioc_count != sizeof (uint16_t)) {
			cmn_err(CE_WARN, "IXGBE_IOC_DEV_ID ioc_count: %d\n",
			    (int)iocp->ioc_count);
			status = IOC_INVAL;
		} else if (mp->b_cont == NULL) {
			cmn_err(CE_WARN, "IXGBE_IOC_DEV_ID b_cont null\n");
			status = IOC_INVAL;
		} else {
			uint16_t *dev_id = (uint16_t *)mp->b_cont->b_rptr;
			*dev_id = ixgbevf->hw.device_id;
			cmn_err(CE_WARN, "return device id: 0x%x\n",
			    *dev_id);
			GlobalDip = GlobalDipArray[ixgbevf->instance];
			status = IOC_REPLY;
			iocp->ioc_error = 0;
		}
		break;
#endif	/* DEBUG_INTEL */

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

void
ixgbevf_m_getfactaddr(void *arg, uint_t naddr, uint8_t *addr)
{
	ixgbevf_t *ixgbevf = (ixgbevf_t *)arg;
	struct ixgbe_hw *hw = &ixgbevf->hw;
	int i;

	ASSERT(naddr == (ixgbevf->unicst_total - 1));

	for (i = 0; i < naddr; i++) {
		bcopy(&hw->vf_macaddr_list[i*IXGBE_ETH_LENGTH_OF_ADDRESS],
		    addr + i * MAXMACADDRLEN, ETHERADDRL);
	}
}

/*
 * Obtain the MAC's capabilities and associated data from
 * the driver.
 */
boolean_t
ixgbevf_m_getcapab(void *arg, mac_capab_t cap, void *cap_data)
{
	ixgbevf_t *ixgbevf = (ixgbevf_t *)arg;

	switch (cap) {
	case MAC_CAPAB_HCKSUM: {
		uint32_t *tx_hcksum_flags = cap_data;

		/*
		 * We advertise our capabilities only if tx hcksum offload is
		 * enabled.  On receive, the stack will accept checksummed
		 * packets anyway, even if we haven't said we can deliver
		 * them.
		 */
		if (!ixgbevf->tx_hcksum_enable)
			return (B_FALSE);

		*tx_hcksum_flags = HCKSUM_INET_PARTIAL | HCKSUM_IPHDRCKSUM;
		break;
	}
	case MAC_CAPAB_LSO: {
		mac_capab_lso_t *cap_lso = cap_data;

		if (ixgbevf->lso_enable) {
			cap_lso->lso_flags = LSO_TX_BASIC_TCP_IPV4;
			cap_lso->lso_basic_tcp_ipv4.lso_max = IXGBE_LSO_MAXLEN;
			break;
		} else {
			return (B_FALSE);
		}
	}
	case MAC_CAPAB_MULTIFACTADDR: {
		mac_capab_multifactaddr_t *mfacp = cap_data;

		/* The primary address is skipped */
		mfacp->mcm_naddr = ixgbevf->unicst_total - 1;
		mfacp->mcm_getaddr = ixgbevf_m_getfactaddr;

		break;
	}
	case MAC_CAPAB_RINGS: {
		mac_capab_rings_t *cap_rings = cap_data;

		cap_rings->mr_version = MAC_RINGS_VERSION_1;

		switch (cap_rings->mr_type) {
		case MAC_RING_TYPE_RX:
			cap_rings->mr_group_type = MAC_GROUP_TYPE_STATIC;
			cap_rings->mr_rnum = ixgbevf->num_rx_rings;
			cap_rings->mr_gnum = ixgbevf->num_groups;
			cap_rings->mr_rget = ixgbevf_fill_ring;
			cap_rings->mr_gget = ixgbevf_fill_group;
			cap_rings->mr_gaddring = NULL;
			cap_rings->mr_gremring = NULL;
			break;
		case MAC_RING_TYPE_TX:
			cap_rings->mr_group_type = MAC_GROUP_TYPE_STATIC;
			cap_rings->mr_rnum = ixgbevf->num_tx_rings;
			cap_rings->mr_gnum = ixgbevf->num_groups;
			cap_rings->mr_rget = ixgbevf_fill_ring;
			cap_rings->mr_gget = ixgbevf_fill_group;
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
ixgbevf_m_setprop(void *arg, const char *pr_name, mac_prop_id_t pr_num,
    uint_t pr_valsize, const void *pr_val)
{
	ixgbevf_t *ixgbevf = (ixgbevf_t *)arg;
	int err = 0;
	uint32_t cur_mtu, new_mtu;
	uint32_t rx_size;
	uint32_t tx_size;

	mutex_enter(&ixgbevf->gen_lock);
	if (ixgbevf->ixgbevf_state & IXGBE_SUSPENDED) {
		mutex_exit(&ixgbevf->gen_lock);
		return (ECANCELED);
	}

	if (ixgbevf->loopback_mode != IXGBE_LB_NONE &&
	    ixgbevf_param_locked(pr_num)) {
		/*
		 * All en_* parameters are locked (read-only)
		 * while the device is in any sort of loopback mode.
		 */
		mutex_exit(&ixgbevf->gen_lock);
		return (EBUSY);
	}

	switch (pr_num) {
	case MAC_PROP_MTU:
		cur_mtu = ixgbevf->default_mtu;
		bcopy(pr_val, &new_mtu, sizeof (new_mtu));
		if (new_mtu == cur_mtu) {
			err = 0;
			break;
		}

		if (new_mtu < DEFAULT_MTU ||
		    new_mtu > ixgbevf->capab->max_mtu) {
			err = EINVAL;
			break;
		}

		if (ixgbevf->ixgbevf_state & IXGBE_STARTED) {
			err = EBUSY;
			break;
		}

		err = mac_maxsdu_update(ixgbevf->mac_hdl, new_mtu);
		if (err == 0) {
			ixgbevf->default_mtu = new_mtu;
			ixgbevf->max_frame_size = ixgbevf->default_mtu +
			    sizeof (struct ether_vlan_header) + ETHERFCSL;

			/*
			 * Set rx buffer size
			 */
			rx_size = ixgbevf->max_frame_size + IPHDR_ALIGN_ROOM;
			ixgbevf->rx_buf_size = ((rx_size >> 10) + ((rx_size &
			    (((uint32_t)1 << 10) - 1)) > 0 ? 1 : 0)) << 10;

			/*
			 * Set tx buffer size
			 */
			tx_size = ixgbevf->max_frame_size;
			ixgbevf->tx_buf_size = ((tx_size >> 10) + ((tx_size &
			    (((uint32_t)1 << 10) - 1)) > 0 ? 1 : 0)) << 10;
		}
		break;
	case MAC_PROP_PRIVATE:
		err = ixgbevf_set_priv_prop(ixgbevf, pr_name, pr_valsize,
		    pr_val);
		break;
	default:
		err = EINVAL;
		break;
	}
	mutex_exit(&ixgbevf->gen_lock);
	return (err);
}

int
ixgbevf_m_getprop(void *arg, const char *pr_name, mac_prop_id_t pr_num,
    uint_t pr_valsize, void *pr_val)
{
	ixgbevf_t *ixgbevf = (ixgbevf_t *)arg;
	int err = 0;

	switch (pr_num) {
	case MAC_PROP_PRIVATE:
		err = ixgbevf_get_priv_prop(ixgbevf, pr_name,
		    pr_valsize, pr_val);
		break;
	default:
		err = EINVAL;
		break;
	}
	return (err);
}

void
ixgbevf_m_propinfo(void *arg, const char *pr_name, mac_prop_id_t pr_num,
    mac_prop_info_handle_t prh)
{
	ixgbevf_t *ixgbevf = (ixgbevf_t *)arg;

	switch (pr_num) {
	case MAC_PROP_MTU:
		mac_prop_info_set_range_uint32(prh,
		    DEFAULT_MTU, ixgbevf->capab->max_mtu);
		break;

	case MAC_PROP_PRIVATE: {
		char valstr[64];
		int value;

		bzero(valstr, sizeof (valstr));

		if (strcmp(pr_name, "_adv_pause_cap") == 0 ||
		    strcmp(pr_name, "_adv_asym_pause_cap") == 0) {
			mac_prop_info_set_perm(prh, MAC_PROP_PERM_READ);
			return;
		}

		if (strcmp(pr_name, "_tx_copy_thresh") == 0) {
			value = DEFAULT_TX_COPY_THRESHOLD;
		} else if (strcmp(pr_name, "_tx_recycle_thresh") == 0) {
			value = DEFAULT_TX_RECYCLE_THRESHOLD;
		} else if (strcmp(pr_name, "_tx_overload_thresh") == 0) {
			value = DEFAULT_TX_OVERLOAD_THRESHOLD;
		} else if (strcmp(pr_name, "_tx_resched_thresh") == 0) {
			value = DEFAULT_TX_RESCHED_THRESHOLD;
		} else 	if (strcmp(pr_name, "_rx_copy_thresh") == 0) {
			value = DEFAULT_RX_COPY_THRESHOLD;
		} else 	if (strcmp(pr_name, "_rx_limit_per_intr") == 0) {
			value = DEFAULT_RX_LIMIT_PER_INTR;
		} 	if (strcmp(pr_name, "_intr_throttling") == 0) {
			value = ixgbevf->capab->def_intr_throttle;
		} else {
			return;
		}

		(void) snprintf(valstr, sizeof (valstr), "%x", value);
	}
	}
}

boolean_t
ixgbevf_param_locked(mac_prop_id_t pr_num)
{
	/*
	 * All en_* parameters are locked (read-only) while
	 * the device is in any sort of loopback mode ...
	 */
	switch (pr_num) {
		case MAC_PROP_EN_10GFDX_CAP:
		case MAC_PROP_EN_1000FDX_CAP:
		case MAC_PROP_EN_100FDX_CAP:
		case MAC_PROP_AUTONEG:
		case MAC_PROP_FLOWCTRL:
			return (B_TRUE);
	}
	return (B_FALSE);
}

/* ARGSUSED */
int
ixgbevf_set_priv_prop(ixgbevf_t *ixgbevf, const char *pr_name,
    uint_t pr_valsize, const void *pr_val)
{
	int err = 0;
	long result;
	struct ixgbe_hw *hw = &ixgbevf->hw;
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
			ixgbevf->tx_copy_thresh = (uint32_t)result;
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
			ixgbevf->tx_recycle_thresh = (uint32_t)result;
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
			ixgbevf->tx_overload_thresh = (uint32_t)result;
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
		    result > MAX_TX_RESCHED_THRESHOLD)
			err = EINVAL;
		else {
			ixgbevf->tx_resched_thresh = (uint32_t)result;
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
			ixgbevf->rx_copy_thresh = (uint32_t)result;
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
			ixgbevf->rx_limit_per_intr = (uint32_t)result;
		}
		return (err);
	}
	if (strcmp(pr_name, "_intr_throttling") == 0) {
		if (pr_val == NULL) {
			err = EINVAL;
			return (err);
		}
		(void) ddi_strtol(pr_val, (char **)NULL, 0, &result);

		if (result < ixgbevf->capab->min_intr_throttle ||
		    result > ixgbevf->capab->max_intr_throttle)
			err = EINVAL;
		else {
			ixgbevf->intr_throttling[0] = (uint32_t)result;

			/*
			 * 82599 requires the interupt throttling rate is
			 * a multiple of 8. This is enforced by the register
			 * definiton.
			 */
			if (hw->mac.type == ixgbe_mac_82599_vf)
				ixgbevf->intr_throttling[0] =
				    ixgbevf->intr_throttling[0] & 0xFF8;

			for (i = 0; i < MAX_INTR_VECTOR; i++)
				ixgbevf->intr_throttling[i] =
				    ixgbevf->intr_throttling[0];

			/* Set interrupt throttling rate */
			for (i = 0; i < ixgbevf->intr_cnt; i++)
				IXGBE_WRITE_REG(hw, IXGBE_EITR(i),
				    ixgbevf->intr_throttling[i]);
		}
		return (err);
	}
	return (ENOTSUP);
}

int
ixgbevf_get_priv_prop(ixgbevf_t *ixgbevf, const char *pr_name,
    uint_t pr_valsize, void *pr_val)
{
	int err = ENOTSUP;
	int value;

	if (strcmp(pr_name, "_adv_pause_cap") == 0) {
		value = ixgbevf->param_adv_pause_cap;
		err = 0;
		goto done;
	}
	if (strcmp(pr_name, "_adv_asym_pause_cap") == 0) {
		value = ixgbevf->param_adv_asym_pause_cap;
		err = 0;
		goto done;
	}
	if (strcmp(pr_name, "_tx_copy_thresh") == 0) {
		value = ixgbevf->tx_copy_thresh;
		err = 0;
		goto done;
	}
	if (strcmp(pr_name, "_tx_recycle_thresh") == 0) {
		value = ixgbevf->tx_recycle_thresh;
		err = 0;
		goto done;
	}
	if (strcmp(pr_name, "_tx_overload_thresh") == 0) {
		value = ixgbevf->tx_overload_thresh;
		err = 0;
		goto done;
	}
	if (strcmp(pr_name, "_tx_resched_thresh") == 0) {
		value = ixgbevf->tx_resched_thresh;
		err = 0;
		goto done;
	}
	if (strcmp(pr_name, "_rx_copy_thresh") == 0) {
		value = ixgbevf->rx_copy_thresh;
		err = 0;
		goto done;
	}
	if (strcmp(pr_name, "_rx_limit_per_intr") == 0) {
		value = ixgbevf->rx_limit_per_intr;
		err = 0;
		goto done;
	}
	if (strcmp(pr_name, "_intr_throttling") == 0) {
		value = ixgbevf->intr_throttling[0];
		err = 0;
		goto done;
	}
done:
	if (err == 0) {
		(void) snprintf(pr_val, pr_valsize, "%d", value);
	}
	return (err);
}
