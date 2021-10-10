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
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include "ixgbe_sw.h"

/*
 * External Data Declarations.
 */
extern ddi_dma_attr_t ixgbe_buf_dma_attr;
extern ddi_dma_attr_t ixgbe_buf2k_dma_attr;
extern ddi_dma_attr_t ixgbe_tx_buf_dma_attr;
extern ddi_dma_attr_t ixgbe_desc_dma_attr;
extern ddi_device_acc_attr_t ixgbe_buf_acc_attr;
extern ddi_device_acc_attr_t ixgbe_desc_acc_attr;

/*
 * Local Data Declarations.
 */
static mac_capab_bm_t ixgbe_tx_capab_bm = {
	MAC_BUFFER_MGMT_TX,
	&ixgbe_buf_dma_attr,
	&ixgbe_buf_acc_attr,
	&ixgbe_desc_dma_attr,
	&ixgbe_desc_acc_attr,
	MIN_TX_RING_SIZE,
	MAX_TX_RING_SIZE,
	DEFAULT_TX_RING_SIZE,
	DEFAULT_TX_RING_SIZE,
	sizeof (union ixgbe_adv_tx_desc),
	0,
	0,
	0,
	0,
	0,
};

static mac_capab_bm_t ixgbe_rx_capab_bm = {
	MAC_BUFFER_MGMT_RX,
	&ixgbe_buf_dma_attr,
	&ixgbe_buf_acc_attr,
	&ixgbe_desc_dma_attr,
	&ixgbe_desc_acc_attr,
	MIN_RX_RING_SIZE,
	MAX_RX_RING_SIZE,
	DEFAULT_RX_RING_SIZE,
	DEFAULT_RX_RING_SIZE,
	sizeof (union ixgbe_adv_rx_desc),
	0,
	0,
	0,
	IPHDR_ALIGN_ROOM,
	0,
};

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
ixgbe_m_start(void *arg)
{
	ixgbe_t *ixgbe = (ixgbe_t *)arg;

	if (!mutex_tryenter(&ixgbe->gen_lock))
		return (EAGAIN);

	if (ixgbe->ixgbe_state & IXGBE_SUSPENDED) {
		mutex_exit(&ixgbe->gen_lock);
		return (ECANCELED);
	}

	if (ixgbe_start(ixgbe, B_TRUE) != IXGBE_SUCCESS) {
		mutex_exit(&ixgbe->gen_lock);
		return (EIO);
	}

	atomic_or_32(&ixgbe->ixgbe_state, IXGBE_STARTED);

	mutex_exit(&ixgbe->gen_lock);

	/*
	 * Enable and start the watchdog timer
	 */
	ixgbe_enable_watchdog_timer(ixgbe);

	return (0);
}

/*
 * Stop the device and put it in a reset/quiesced state such
 * that the interface can be unregistered.
 */
void
ixgbe_m_stop(void *arg)
{
	ixgbe_t *ixgbe = (ixgbe_t *)arg;

	mutex_enter(&ixgbe->gen_lock);

	if (ixgbe->ixgbe_state & IXGBE_SUSPENDED) {
		mutex_exit(&ixgbe->gen_lock);
		return;
	}

	atomic_and_32(&ixgbe->ixgbe_state, ~IXGBE_STARTED);

	ixgbe_stop(ixgbe, B_TRUE);

	mutex_exit(&ixgbe->gen_lock);

	/*
	 * Disable and stop the watchdog timer
	 */
	ixgbe_disable_watchdog_timer(ixgbe);
}

/*
 * Set the promiscuity of the device.
 */
int
ixgbe_m_promisc(void *arg, boolean_t on)
{
	ixgbe_t *ixgbe = (ixgbe_t *)arg;
	uint32_t reg_val;
	struct ixgbe_hw *hw = &ixgbe->hw;

	mutex_enter(&ixgbe->gen_lock);

	if (ixgbe->ixgbe_state & IXGBE_SUSPENDED) {
		mutex_exit(&ixgbe->gen_lock);
		return (ECANCELED);
	}
	reg_val = IXGBE_READ_REG(hw, IXGBE_FCTRL);

	if (on)
		reg_val |= (IXGBE_FCTRL_UPE | IXGBE_FCTRL_MPE);
	else
		reg_val &= (~(IXGBE_FCTRL_UPE | IXGBE_FCTRL_MPE));

	IXGBE_WRITE_REG(&ixgbe->hw, IXGBE_FCTRL, reg_val);

	mutex_exit(&ixgbe->gen_lock);

	return (0);
}

/*
 * Add/remove the addresses to/from the set of multicast
 * addresses for which the device will receive packets.
 */
int
ixgbe_m_multicst(void *arg, boolean_t add, const uint8_t *mcst_addr)
{
	ixgbe_t *ixgbe = (ixgbe_t *)arg;
	int result;

	mutex_enter(&ixgbe->gen_lock);

	if (ixgbe->ixgbe_state & IXGBE_SUSPENDED) {
		mutex_exit(&ixgbe->gen_lock);
		return (ECANCELED);
	}

	result = (add) ? ixgbe_multicst_add(ixgbe, mcst_addr)
	    : ixgbe_multicst_remove(ixgbe, mcst_addr);

	mutex_exit(&ixgbe->gen_lock);

	return (result);
}

/*
 * Pass on M_IOCTL messages passed to the DLD, and support
 * private IOCTLs for debugging and ndd.
 */
void
ixgbe_m_ioctl(void *arg, queue_t *q, mblk_t *mp)
{
	ixgbe_t *ixgbe = (ixgbe_t *)arg;
	struct iocblk *iocp;
	enum ioc_reply status;

	iocp = (struct iocblk *)(uintptr_t)mp->b_rptr;
	iocp->ioc_error = 0;

	mutex_enter(&ixgbe->gen_lock);
	if (ixgbe->ixgbe_state & IXGBE_SUSPENDED) {
		mutex_exit(&ixgbe->gen_lock);
		miocnak(q, mp, 0, EINVAL);
		return;
	}
	mutex_exit(&ixgbe->gen_lock);

	switch (iocp->ioc_cmd) {
	case LB_GET_INFO_SIZE:
	case LB_GET_INFO:
	case LB_GET_MODE:
	case LB_SET_MODE:
		status = ixgbe_loopback_ioctl(ixgbe, iocp, mp);
		break;
	case IOV_GET_PARAM_VER_INFO:
	case IOV_GET_PARAM_INFO:
	case IOV_VALIDATE_PARAM:
		status = ixgbe_iov_ioctl(ixgbe, iocp, mp);
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
			uint16_t *dev_id =
			    (uint16_t *)(uintptr_t)mp->b_cont->b_rptr;
			*dev_id = ixgbe->hw.device_id;
			cmn_err(CE_WARN, "return device id: 0x%x\n",
			    *dev_id);
			GlobalDip = GlobalDipArray[ixgbe->instance];
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

/* ixgbe supports pre-mapped DMA buffer cache */
static boolean_t ixgbe_do_mapped = B_TRUE;

/*
 * Obtain the MAC's capabilities and associated data from
 * the driver.
 */
boolean_t
ixgbe_m_getcapab(void *arg, mac_capab_t cap, void *cap_data)
{
	ixgbe_t *ixgbe = (ixgbe_t *)arg;

	switch (cap) {
	case MAC_CAPAB_HCKSUM: {
		uint32_t *tx_hcksum_flags = cap_data;

		/*
		 * We advertise our capabilities only if tx hcksum offload is
		 * enabled.  On receive, the stack will accept checksummed
		 * packets anyway, even if we haven't said we can deliver
		 * them.
		 */
		if (!ixgbe->tx_hcksum_enable)
			return (B_FALSE);

		*tx_hcksum_flags = HCKSUM_INET_PARTIAL | HCKSUM_IPHDRCKSUM;
		break;
	}
	case MAC_CAPAB_LSO: {
		mac_capab_lso_t *cap_lso = cap_data;

		if (ixgbe->lso_enable) {
			cap_lso->lso_flags = LSO_TX_BASIC_TCP_IPV4;
			cap_lso->lso_basic_tcp_ipv4.lso_max = IXGBE_LSO_MAXLEN;
			break;
		} else {
			return (B_FALSE);
		}
	}
	case MAC_CAPAB_RINGS: {
		mac_capab_rings_t *cap_rings = cap_data;

		cap_rings->mr_version = MAC_RINGS_VERSION_1;

		switch (cap_rings->mr_type) {
		case MAC_RING_TYPE_RX:
			cap_rings->mr_flags = MAC_RINGS_VLAN_TRANSPARENT;
			cap_rings->mr_group_type = MAC_GROUP_TYPE_STATIC;
			cap_rings->mr_rnum = ixgbe->num_rx_rings;
			cap_rings->mr_gnum = ixgbe->num_all_rx_groups;
			cap_rings->mr_rget = ixgbe_fill_ring;
			cap_rings->mr_gget = ixgbe_fill_group;
			cap_rings->mr_gaddring = NULL;
			cap_rings->mr_gremring = NULL;
			cap_rings->mr_ggetringtc = ixgbe_get_ring_tc;
			break;
		case MAC_RING_TYPE_TX:
			cap_rings->mr_flags = MAC_RINGS_VLAN_TRANSPARENT;
			cap_rings->mr_group_type = MAC_GROUP_TYPE_STATIC;
			cap_rings->mr_rnum = ixgbe->num_tx_rings;
			cap_rings->mr_gnum = ixgbe->num_all_tx_groups;
			cap_rings->mr_rget = ixgbe_fill_ring;
			cap_rings->mr_gget = ixgbe_fill_group;
			cap_rings->mr_gaddring = NULL;
			cap_rings->mr_gremring = NULL;
			cap_rings->mr_ggetringtc = ixgbe_get_ring_tc;
			break;
		default:
			break;
		}
		break;
	}
	case MAC_CAPAB_BUFFER_MGMT: {
		mac_capab_bm_t	*cap_bm = cap_data;

		if (cap_bm->mbm_type == MAC_BUFFER_MGMT_TX) {
			bcopy(&ixgbe_tx_capab_bm, cap_bm, sizeof (*cap_bm));
			cap_bm->mbm_buffer_size = (size_t)ixgbe->tx_buf_size;
			/*
			 * The desired buffer size should be what we
			 * get as result of the configuration set in ixgbe.conf
			 * or the default, if unchanged.
			 */
			cap_bm->mbm_desired_buffers = ixgbe->tx_ring_size;
			cap_bm->mbm_desired_descriptors = ixgbe->tx_ring_size;

			/*
			 * The DMA setup and teardown costs per byte are
			 * significantly lower for jumbograms. So we enable
			 * pre-mapped buffer caches only for 1500 MTU.
			 */
			if (ixgbe->default_mtu <= ETHERMTU && ixgbe_do_mapped)
				cap_bm->mbm_flags |= MAC_BM_PREMAP_OK;
			if (ixgbe->tx_head_wb_enable)
				cap_bm->mbm_desired_descriptors++;
		} else if (cap_bm->mbm_type == MAC_BUFFER_MGMT_RX) {
			bcopy(&ixgbe_rx_capab_bm, cap_bm, sizeof (*cap_bm));
			cap_bm->mbm_buffer_size = (size_t)ixgbe->rx_buf_size;
			/*
			 * The desired buffer size should be what we
			 * get as result of the configuration set in ixgbe.conf
			 * or the default, if unchanged.
			 */
			cap_bm->mbm_desired_buffers = ixgbe->rx_ring_size;
			cap_bm->mbm_desired_descriptors = ixgbe->rx_ring_size;

			/*
			 * If LRO is enabled, the NIC uses the entire 2K buffer
			 * to coalesce multiple packets. Due to the offset of
			 * IPHDR_ALIGN_ROOM at the start of the buffer, the
			 * real amount of space available for DMA is reduced
			 * by that amount. Thus the actual buffer needs to be
			 * at least 2K + IPHDR_ALIGN_ROOM bytes and we can't
			 * pack buffers, or we end up with DMA buffer overrun.
			 */
			if (ixgbe->default_mtu <= ETHERMTU) {
				if (!ixgbe->lro_enable) {
					cap_bm->mbm_packet_attrp =
					    &ixgbe_buf2k_dma_attr;
				} else {
					cap_bm->mbm_buffer_size =
					    ixgbe->rx_buf_size +
					    sizeof (uint64_t);
				}
			}
		} else {
			return (B_FALSE);
		}
		break;
	}
	case MAC_CAPAB_DCB: {
		mac_capab_dcb_t *cap_dcb = cap_data;

		cap_dcb->mcd_ntc = ixgbe->dcb_config.num_tcs.pg_tcs;
		if (cap_dcb->mcd_ntc > 0) {
			cap_dcb->mcd_flags = MAC_DCB_PFC;
		} else {
			cap_dcb->mcd_flags = 0;
		}
		cap_dcb->mcd_pfc = 0;
		break;
	}
	case MAC_CAPAB_FCOE: {
		mac_capab_fcoe_t *cap_fcoe = cap_data;

		if (ixgbe->fcoe_txcrc_enable) {
			cap_fcoe->mac_capab_fcoe_flags |= MAC_FCOE_FLAG_TXCRC;
		}
		if (ixgbe->fcoe_rxcrc_enable) {
			cap_fcoe->mac_capab_fcoe_flags |= MAC_FCOE_FLAG_RXCRC;
		}
		if (ixgbe->fcoe_lso_enable) {
			cap_fcoe->mac_capab_fcoe_flags |= MAC_FCOE_FLAG_LSO;
			cap_fcoe->mac_fcoe_max_lso_size = IXGBE_FCOE_LSO_MAXLEN;
		}
		if (ixgbe->fcoe_lro_enable) {
			cap_fcoe->mac_capab_fcoe_flags |= MAC_FCOE_FLAG_LRO;
			cap_fcoe->mac_fcoe_max_lro_size = IXGBE_FCOE_LRO_MAXLEN;
			cap_fcoe->mac_fcoe_min_lro_xchgid =
			    IXGBE_DDP_MIN_XCHGID;
			cap_fcoe->mac_fcoe_max_lro_xchgid =
			    IXGBE_DDP_MAX_XCHGID;
			cap_fcoe->mac_fcoe_setup_lro = ixgbe_fcoe_setup_lro;
			cap_fcoe->mac_fcoe_cancel_lro = ixgbe_fcoe_cancel_lro;
		}
		break;
	}
	default:
		return (B_FALSE);
	}
	return (B_TRUE);
}

static int
ixgbe_m_setpfc(void *arg, uint8_t map)
{
	int i;
	uint32_t reg;
	ixgbe_t *ixgbe = (ixgbe_t *)arg;
	struct ixgbe_hw *hw = &ixgbe->hw;
	int err = 0;

	if (ixgbe->dcb_config.num_tcs.pg_tcs == 0) {
		return ((map == 0) ? 0 : ENOTSUP);
	}

	/*
	 * Switch from Legacy to PFC if the current mode is not PFC and
	 * the requested mode is auto or PFC.
	 */
	if (map != 0 && hw->fc.current_mode != LINK_FLOWCTRL_PFC &&
	    hw->fc.requested_mode != LINK_FLOWCTRL_AUTO &&
	    hw->fc.requested_mode != LINK_FLOWCTRL_PFC) {
		return (EINVAL);
	}

	/*
	 * Switch from PFC to Leagacy if the current mode is PFC and
	 * the requested mode is auto.
	 */
	if (map == 0 && hw->fc.requested_mode == LINK_FLOWCTRL_AUTO &&
	    hw->fc.current_mode == LINK_FLOWCTRL_PFC) {
		/*
		 * Reset to default flow control.
		 */
		hw->fc.current_mode = ixgbe_fc_none;
		if (ixgbe_driver_setup_link(ixgbe, B_TRUE) != IXGBE_SUCCESS)
			err = EINVAL;
	} else if (map != 0 && hw->fc.current_mode != LINK_FLOWCTRL_PFC) {
		/* Switch to PFC */
		hw->fc.requested_mode = ixgbe_fc_pfc;
		ixgbe->dcb_config.pfc_mode_enable = B_TRUE;
		(void) ixgbe_dcb_config_pfc(hw, &ixgbe->dcb_config);
		if (ixgbe_driver_setup_link(ixgbe, B_TRUE) != IXGBE_SUCCESS)
			err = EINVAL;
	}

	for (i = 0; i < MAX_TRAFFIC_CLASS; i++) {
		reg = IXGBE_READ_REG(hw, IXGBE_FCRTH_82599(i));

		if (map & (1 << i)) {
			reg |= IXGBE_FCRTH_FCEN;
		} else {
			reg &= ~IXGBE_FCRTH_FCEN;
		}
		IXGBE_WRITE_REG(hw, IXGBE_FCRTH_82599(i), reg);
	}
	ixgbe->pfc_map = map;

	return (err);
}

int
ixgbe_m_setprop(void *arg, const char *pr_name, mac_prop_id_t pr_num,
    uint_t pr_valsize, const void *pr_val)
{
	ixgbe_t *ixgbe = (ixgbe_t *)arg;
	struct ixgbe_hw *hw = &ixgbe->hw;
	int err = 0;
	uint32_t flow_control;
	uint32_t cur_mtu, new_mtu;
	uint32_t rx_size;
	uint32_t tx_size;

	mutex_enter(&ixgbe->gen_lock);
	if (ixgbe->ixgbe_state & IXGBE_SUSPENDED) {
		mutex_exit(&ixgbe->gen_lock);
		return (ECANCELED);
	}

	if (ixgbe->loopback_mode != IXGBE_LB_NONE &&
	    ixgbe_param_locked(pr_num)) {
		/*
		 * All en_* parameters are locked (read-only)
		 * while the device is in any sort of loopback mode.
		 */
		mutex_exit(&ixgbe->gen_lock);
		return (EBUSY);
	}

	switch (pr_num) {
	case MAC_PROP_EN_10GFDX_CAP:
		/* read/write on copper, read-only on serdes */
		if (ixgbe->hw.phy.media_type != ixgbe_media_type_copper) {
			err = ENOTSUP;
			break;
		} else {
			ixgbe->param_en_10000fdx_cap = *(uint8_t *)pr_val;
			ixgbe->param_adv_10000fdx_cap = *(uint8_t *)pr_val;
			goto setup_link;
		}
	case MAC_PROP_EN_1000FDX_CAP:
		/* read/write on copper, read-only on serdes */
		if (ixgbe->hw.phy.media_type != ixgbe_media_type_copper) {
			err = ENOTSUP;
			break;
		} else {
			ixgbe->param_en_1000fdx_cap = *(uint8_t *)pr_val;
			ixgbe->param_adv_1000fdx_cap = *(uint8_t *)pr_val;
			goto setup_link;
		}
	case MAC_PROP_EN_100FDX_CAP:
		/* read/write on copper, read-only on serdes */
		if (ixgbe->hw.phy.media_type != ixgbe_media_type_copper) {
			err = ENOTSUP;
			break;
		} else {
			ixgbe->param_en_100fdx_cap = *(uint8_t *)pr_val;
			ixgbe->param_adv_100fdx_cap = *(uint8_t *)pr_val;
			goto setup_link;
		}
	case MAC_PROP_AUTONEG:
		/* read/write on copper, read-only on serdes */
		if (ixgbe->hw.phy.media_type != ixgbe_media_type_copper) {
			err = ENOTSUP;
			break;
		} else {
			ixgbe->param_adv_autoneg_cap = *(uint8_t *)pr_val;
			goto setup_link;
		}
	case MAC_PROP_FLOWCTRL:
		bcopy(pr_val, &flow_control, sizeof (flow_control));

		switch (flow_control) {
		default:
			err = ENOTSUP;
			break;
		case LINK_FLOWCTRL_NONE:
			hw->fc.requested_mode = ixgbe_fc_none;
			break;
		case LINK_FLOWCTRL_RX:
			hw->fc.requested_mode = ixgbe_fc_rx_pause;
			break;
		case LINK_FLOWCTRL_TX:
			hw->fc.requested_mode = ixgbe_fc_tx_pause;
			break;
		case LINK_FLOWCTRL_BI:
			hw->fc.requested_mode = ixgbe_fc_full;
			break;
		case LINK_FLOWCTRL_PFC:
			if (ixgbe->dcb_config.num_tcs.pg_tcs != 0)
				hw->fc.requested_mode = ixgbe_fc_pfc;
			else
				err = ENOTSUP;
			break;
		case LINK_FLOWCTRL_AUTO:
			if (ixgbe->dcb_config.num_tcs.pg_tcs != 0)
				hw->fc.requested_mode = ixgbe_fc_pfc_auto;
			else
				err = ENOTSUP;
			break;
		}
		if (flow_control == LINK_FLOWCTRL_PFC && err == 0) {
			ixgbe->dcb_config.pfc_mode_enable = B_TRUE;
			(void) ixgbe_dcb_config_pfc(hw, &ixgbe->dcb_config);
			hw->fc.current_mode = ixgbe_fc_pfc;
		}
setup_link:
		if (err == 0) {
			if (ixgbe_driver_setup_link(ixgbe, B_TRUE) !=
			    IXGBE_SUCCESS)
				err = EINVAL;
		}
		break;
	case MAC_PROP_ADV_10GFDX_CAP:
	case MAC_PROP_ADV_1000FDX_CAP:
	case MAC_PROP_ADV_100FDX_CAP:
	case MAC_PROP_STATUS:
	case MAC_PROP_SPEED:
	case MAC_PROP_DUPLEX:
		err = ENOTSUP; /* read-only prop. Can't set this. */
		break;
	case MAC_PROP_MTU:
		cur_mtu = ixgbe->default_mtu;
		bcopy(pr_val, &new_mtu, sizeof (new_mtu));
		if (new_mtu == cur_mtu) {
			err = 0;
			break;
		}

		if (new_mtu < DEFAULT_MTU || new_mtu > ixgbe->capab->max_mtu) {
			err = EINVAL;
			break;
		}

		if (ixgbe->ixgbe_state & IXGBE_STARTED) {
			err = EBUSY;
			break;
		}

		err = mac_maxsdu_update(ixgbe->mac_hdl, new_mtu);
		if (err == 0) {
			ixgbe->default_mtu = new_mtu;
			ixgbe->max_frame_size = ixgbe->default_mtu +
			    sizeof (struct ether_vlan_header) + ETHERFCSL;

			/*
			 * Set rx buffer size
			 */
			rx_size = ixgbe->max_frame_size + IPHDR_ALIGN_ROOM;
			ixgbe->rx_buf_size = ((rx_size >> 10) + ((rx_size &
			    (((uint32_t)1 << 10) - 1)) > 0 ? 1 : 0)) << 10;

			/*
			 * Set tx buffer size
			 */
			tx_size = ixgbe->max_frame_size;
			ixgbe->tx_buf_size = ((tx_size >> 10) + ((tx_size &
			    (((uint32_t)1 << 10) - 1)) > 0 ? 1 : 0)) << 10;

			/*
			 * Update flow control thresholds
			 */
			hw->fc.high_water =
			    FC_HIGH_WATER(ixgbe->max_frame_size);
			hw->fc.low_water =
			    FC_LOW_WATER(ixgbe->max_frame_size);
		}
		break;
	case MAC_PROP_PFC: {
		uint8_t pfcmap;

		bcopy(pr_val, &pfcmap, sizeof (pfcmap));

		err = ixgbe_m_setpfc(ixgbe, pfcmap);
		break;
	}
	case MAC_PROP_PRIVATE:
		err = ixgbe_set_priv_prop(ixgbe, pr_name, pr_valsize, pr_val);
		break;
	default:
		err = EINVAL;
		break;
	}
	mutex_exit(&ixgbe->gen_lock);
	return (err);
}

int
ixgbe_m_getprop(void *arg, const char *pr_name, mac_prop_id_t pr_num,
    uint_t pr_valsize, void *pr_val)
{
	ixgbe_t *ixgbe = (ixgbe_t *)arg;
	struct ixgbe_hw *hw = &ixgbe->hw;
	int err = 0;
	uint32_t flow_control;
	enum ixgbe_fc_mode mode;
	uint64_t tmp = 0;

	switch (pr_num) {
	case MAC_PROP_DUPLEX:
		ASSERT(pr_valsize >= sizeof (link_duplex_t));
		bcopy(&ixgbe->link_duplex, pr_val,
		    sizeof (link_duplex_t));
		break;
	case MAC_PROP_SPEED:
		ASSERT(pr_valsize >= sizeof (uint64_t));
		tmp = ixgbe->link_speed * 1000000ull;
		bcopy(&tmp, pr_val, sizeof (tmp));
		break;
	case MAC_PROP_AUTONEG:
		*(uint8_t *)pr_val = ixgbe->param_adv_autoneg_cap;
		break;
	case MAC_PROP_FLOWCTRL:
	case MAC_PROP_FLOWCTRL_EFFECTIVE:
		ASSERT(pr_valsize >= sizeof (uint32_t));
		if (pr_num == MAC_PROP_FLOWCTRL)
			mode = hw->fc.requested_mode;
		else
			mode = hw->fc.current_mode;
		switch (mode) {
			case ixgbe_fc_none:
				flow_control = LINK_FLOWCTRL_NONE;
				break;
			case ixgbe_fc_rx_pause:
				flow_control = LINK_FLOWCTRL_RX;
				break;
			case ixgbe_fc_tx_pause:
				flow_control = LINK_FLOWCTRL_TX;
				break;
			case ixgbe_fc_full:
				flow_control = LINK_FLOWCTRL_BI;
				break;
			case ixgbe_fc_pfc:
				flow_control = LINK_FLOWCTRL_PFC;
				break;
			case ixgbe_fc_pfc_auto:
				flow_control = LINK_FLOWCTRL_AUTO;
				break;
		}
		bcopy(&flow_control, pr_val, sizeof (flow_control));
		break;
	case MAC_PROP_ADV_10GFDX_CAP:
		*(uint8_t *)pr_val = ixgbe->param_adv_10000fdx_cap;
		break;
	case MAC_PROP_EN_10GFDX_CAP:
		*(uint8_t *)pr_val = ixgbe->param_en_10000fdx_cap;
		break;
	case MAC_PROP_ADV_1000FDX_CAP:
		*(uint8_t *)pr_val = ixgbe->param_adv_1000fdx_cap;
		break;
	case MAC_PROP_EN_1000FDX_CAP:
		*(uint8_t *)pr_val = ixgbe->param_en_1000fdx_cap;
		break;
	case MAC_PROP_ADV_100FDX_CAP:
		*(uint8_t *)pr_val = ixgbe->param_adv_100fdx_cap;
		break;
	case MAC_PROP_EN_100FDX_CAP:
		*(uint8_t *)pr_val = ixgbe->param_en_100fdx_cap;
		break;
	case MAC_PROP_PFC:
		*(uint8_t *)pr_val = ixgbe->pfc_map;
		break;
	case MAC_PROP_PRIVATE:
		err = ixgbe_get_priv_prop(ixgbe, pr_name,
		    pr_valsize, pr_val);
		break;
	default:
		err = EINVAL;
		break;
	}
	return (err);
}

void
ixgbe_m_propinfo(void *arg, const char *pr_name, mac_prop_id_t pr_num,
    mac_prop_info_handle_t prh)
{
	ixgbe_t *ixgbe = (ixgbe_t *)arg;
	uint_t perm;

	switch (pr_num) {
	case MAC_PROP_DUPLEX:
	case MAC_PROP_SPEED:
		mac_prop_info_set_perm(prh, MAC_PROP_PERM_READ);
		break;

	case MAC_PROP_ADV_100FDX_CAP:
	case MAC_PROP_ADV_1000FDX_CAP:
	case MAC_PROP_ADV_10GFDX_CAP:
		mac_prop_info_set_perm(prh, MAC_PROP_PERM_READ);
		mac_prop_info_set_default_uint8(prh, 1);
		break;

	case MAC_PROP_AUTONEG:
	case MAC_PROP_EN_10GFDX_CAP:
	case MAC_PROP_EN_1000FDX_CAP:
	case MAC_PROP_EN_100FDX_CAP:
		perm = (ixgbe->hw.phy.media_type == ixgbe_media_type_copper) ?
		    MAC_PROP_PERM_RW : MAC_PROP_PERM_READ;
		mac_prop_info_set_perm(prh, perm);
		mac_prop_info_set_default_uint8(prh, 1);
		break;

	case MAC_PROP_FLOWCTRL:
		mac_prop_info_set_default_link_flowctrl(prh,
		    LINK_FLOWCTRL_NONE);
		break;

	case MAC_PROP_MTU:
		mac_prop_info_set_range_uint32(prh,
		    DEFAULT_MTU, ixgbe->capab->max_mtu);
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
		} else if (strcmp(pr_name, "_rx_copy_thresh") == 0) {
			value = DEFAULT_RX_COPY_THRESHOLD;
		} else if (strcmp(pr_name, "_rx_limit_per_intr") == 0) {
			value = DEFAULT_RX_LIMIT_PER_INTR;
		} else if (strcmp(pr_name, "_rx_pfc_low") == 0) {
			value = DEFAULT_FCRTL;
		} else if (strcmp(pr_name, "_rx_pfc_high") == 0) {
			value = DEFAULT_FCRTH;
		} else if (strcmp(pr_name, "_intr_throttling") == 0) {
			value = ixgbe->capab->def_intr_throttle;
		} else {
			return;
		}

		(void) snprintf(valstr, sizeof (valstr), "%x", value);
	}
	}
}

boolean_t
ixgbe_param_locked(mac_prop_id_t pr_num)
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
ixgbe_set_priv_prop(ixgbe_t *ixgbe, const char *pr_name,
    uint_t pr_valsize, const void *pr_val)
{
	int err = 0;
	long result;
	struct ixgbe_hw *hw = &ixgbe->hw;
	struct ixgbe_dcb_config *dcb_config;
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
			ixgbe->tx_copy_thresh = (uint32_t)result;
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
			ixgbe->tx_recycle_thresh = (uint32_t)result;
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
			ixgbe->tx_overload_thresh = (uint32_t)result;
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
			ixgbe->tx_resched_thresh = (uint32_t)result;
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
			ixgbe->rx_copy_thresh = (uint32_t)result;
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
			ixgbe->rx_limit_per_intr = (uint32_t)result;
		}
		return (err);
	}
	if (strcmp(pr_name, "_rx_pfc_low") == 0) {
		int num_tcs;
		u32 fcrtl;
		dcb_config = &ixgbe->dcb_config;

		num_tcs = dcb_config->num_tcs.pg_tcs;
		if (pr_val == NULL) {
			return (EINVAL);
		}
		if (num_tcs == 0) {
			return (ENOTSUP);
		}
		(void) ddi_strtol(pr_val, (char **)NULL, 0, &result);
		fcrtl = result * 1024;
		ixgbe->rx_pfc_low = fcrtl;

		for (i = 0; i < num_tcs; i++) {
			if (dcb_config->tc_config[i].dcb_pfc ==
			    pfc_enabled_full ||
			    dcb_config->tc_config[i].dcb_pfc == pfc_enabled_tx)
				fcrtl |= IXGBE_FCRTL_XONE;
			IXGBE_WRITE_REG(hw, IXGBE_FCRTL_82599(i), fcrtl);
		}
		return (err);
	}
	if (strcmp(pr_name, "_rx_pfc_high") == 0) {
		int num_tcs;
		u32 fcrth;
		dcb_config = &ixgbe->dcb_config;

		num_tcs = dcb_config->num_tcs.pg_tcs;
		if (pr_val == NULL) {
			return (EINVAL);
		}
		if (num_tcs == 0) {
			return (ENOTSUP);
		}
		(void) ddi_strtol(pr_val, (char **)NULL, 0, &result);
		fcrth = result * 1024;
		ixgbe->rx_pfc_high = fcrth;

		for (i = 0; i < num_tcs; i++) {
			if (dcb_config->tc_config[i].dcb_pfc ==
			    pfc_enabled_full ||
			    dcb_config->tc_config[i].dcb_pfc == pfc_enabled_tx)
				fcrth |= IXGBE_FCRTH_FCEN;
			IXGBE_WRITE_REG(hw, IXGBE_FCRTH_82599(i), fcrth);
		}
		return (err);
	}
	if (strcmp(pr_name, "_intr_throttling") == 0) {
		if (pr_val == NULL) {
			err = EINVAL;
			return (err);
		}
		(void) ddi_strtol(pr_val, (char **)NULL, 0, &result);

		if (result < ixgbe->capab->min_intr_throttle ||
		    result > ixgbe->capab->max_intr_throttle)
			err = EINVAL;
		else {
			ixgbe->intr_throttling[0] = (uint32_t)result;

			/*
			 * 82599/X540 requires the interrupt throttling rate is
			 * a multiple of 8. This is enforced by the register
			 * definiton.
			 */
			if (hw->mac.type >= ixgbe_mac_82599EB)
				ixgbe->intr_throttling[0] =
				    ixgbe->intr_throttling[0] & 0xFF8;

			for (i = 0; i < MAX_INTR_VECTOR; i++)
				ixgbe->intr_throttling[i] =
				    ixgbe->intr_throttling[0];

			/* Set interrupt throttling rate */
			for (i = 0; i < ixgbe->intr_cnt; i++)
				IXGBE_WRITE_REG(hw, IXGBE_EITR(i),
				    ixgbe->intr_throttling[i]);
		}
		return (err);
	}
	return (ENOTSUP);
}

int
ixgbe_get_priv_prop(ixgbe_t *ixgbe, const char *pr_name,
    uint_t pr_valsize, void *pr_val)
{
	int err = ENOTSUP;
	int value;

	if (strcmp(pr_name, "_adv_pause_cap") == 0) {
		value = ixgbe->param_adv_pause_cap;
		err = 0;
		goto done;
	}
	if (strcmp(pr_name, "_adv_asym_pause_cap") == 0) {
		value = ixgbe->param_adv_asym_pause_cap;
		err = 0;
		goto done;
	}
	if (strcmp(pr_name, "_tx_copy_thresh") == 0) {
		value = ixgbe->tx_copy_thresh;
		err = 0;
		goto done;
	}
	if (strcmp(pr_name, "_tx_recycle_thresh") == 0) {
		value = ixgbe->tx_recycle_thresh;
		err = 0;
		goto done;
	}
	if (strcmp(pr_name, "_tx_overload_thresh") == 0) {
		value = ixgbe->tx_overload_thresh;
		err = 0;
		goto done;
	}
	if (strcmp(pr_name, "_tx_resched_thresh") == 0) {
		value = ixgbe->tx_resched_thresh;
		err = 0;
		goto done;
	}
	if (strcmp(pr_name, "_rx_copy_thresh") == 0) {
		value = ixgbe->rx_copy_thresh;
		err = 0;
		goto done;
	}
	if (strcmp(pr_name, "_rx_limit_per_intr") == 0) {
		value = ixgbe->rx_limit_per_intr;
		err = 0;
		goto done;
	}
	if (strcmp(pr_name, "_rx_pfc_low") == 0) {
		value = ixgbe->rx_pfc_low;
		err = 0;
		goto done;
	}
	if (strcmp(pr_name, "_rx_pfc_high") == 0) {
		value = ixgbe->rx_pfc_high;
		err = 0;
		goto done;
	}
	if (strcmp(pr_name, "_intr_throttling") == 0) {
		value = ixgbe->intr_throttling[0];
		err = 0;
		goto done;
	}
done:
	if (err == 0) {
		(void) snprintf(pr_val, pr_valsize, "%d", value);
	}
	return (err);
}

enum ioc_reply
ixgbe_iov_ioctl(ixgbe_t *ixgbe, struct iocblk *iocp, mblk_t *mp)
{
	int mode, rval;
	iov_param_ver_info_t *verp;
	iov_param_validate_t *valp;
	pci_param_t param;
	size_t size;
	char reason[MAX_REASON_LEN + 1];
	char *pbuf;
	mblk_t *bp;
	uint32_t mblen;

	bzero(reason, MAX_REASON_LEN + 1);

	if (mp->b_cont == NULL)
		return (IOC_INVAL);

	switch (iocp->ioc_cmd) {
	default:
		return (IOC_INVAL);

	case IOV_GET_PARAM_VER_INFO:
		IXGBE_DEBUGLOG_0(ixgbe, "Get param ver info");
		size = sizeof (iov_param_ver_info_t);
		if (iocp->ioc_count < size)
			return (IOC_INVAL);

		verp = (iov_param_ver_info_t *)(uintptr_t)mp->b_cont->b_rptr;
		verp->version = IOV_PARAM_DESC_VERSION;
		verp->num_params = IXGBE_NUM_IOV_PARAMS;

		break;

	case IOV_GET_PARAM_INFO:
		IXGBE_DEBUGLOG_0(ixgbe, "Get param info");
		pbuf = (char *)& ixgbe_iov_param_list[0];
		size = sizeof (ixgbe_iov_param_list);
		if (iocp->ioc_count < size)
			return (IOC_INVAL);

		for (bp = mp->b_cont; bp != NULL; bp = bp->b_cont) {
			if ((mblen = MBLKL(bp)) == 0)
				continue;
			bcopy(pbuf, bp->b_rptr, mblen);
			pbuf += mblen;
		}

		break;

	case IOV_VALIDATE_PARAM:
		IXGBE_DEBUGLOG_0(ixgbe, "Validate param");
		size = sizeof (iov_param_validate_t);
		if (iocp->ioc_count < size)
			return (IOC_INVAL);

		(void) strncpy(reason, "Failed to read params", MAX_REASON_LEN);

		valp = (iov_param_validate_t *)(uintptr_t)mp->b_cont->b_rptr;
		mode = FKIOCTL | iocp->ioc_flag;

		rval = pci_param_get_ioctl(ixgbe->dip,
		    (intptr_t)valp, mode, &param);

		if (rval == DDI_SUCCESS) {
			rval = ixgbe_validate_iov_params(param, reason);
		}

		(void) pci_param_free(param);

		if (rval == DDI_SUCCESS) {
			/* validation success; no data to return */
			size = 0;
		} else {
			IXGBE_DEBUGLOG_1(ixgbe, "%s", reason);
			/* validation failed; return reason */
			size = sizeof (reason);
			bcopy(reason, valp->pv_reason, size);
		}

		break;
	}

	iocp->ioc_count = size;
	iocp->ioc_error = 0;

	return (IOC_REPLY);
}

int
ixgbe_validate_iov_params(pci_param_t param, char *reason)
{
	pci_plist_t plist;
	int rval, i;
	uint16_t num_vf;
	uint32_t unicast_slots;
	uint32_t total_slots = 0;

	rval = pci_plist_get(param, &plist);
	if (rval != DDI_SUCCESS) {
		(void) strncpy(reason, "No params for PF", MAX_REASON_LEN);
		return (DDI_FAILURE);
	}

	rval = pci_plist_lookup_uint16(plist, NUM_VF_NVLIST_NAME, &num_vf);
	if (rval != DDI_SUCCESS) {
		(void) strncpy(reason,
		    "Failed to read vf number", MAX_REASON_LEN);
		return (DDI_FAILURE);
	}

	if (num_vf > IXGBE_MAX_CONFIG_VF) {
		(void) snprintf(reason, MAX_REASON_LEN,
		    "Greater than %d VFs is not supported",
		    IXGBE_MAX_CONFIG_VF);
		return (DDI_FAILURE);
	}

	rval = ixgbe_get_param_unicast_slots(plist, &unicast_slots, reason);
	if (rval != DDI_SUCCESS) {
		return (DDI_FAILURE);
	}
	IXGBE_DEBUGLOG_1(NULL,
	    "Validate unicast-slots for PF: %d", unicast_slots);

	if (unicast_slots != 0)
		total_slots += unicast_slots;
	else
		total_slots++;

	for (i = 0; i < num_vf; i++) {
		rval = pci_plist_getvf(param, i, &plist);
		if (rval != DDI_SUCCESS) {
			(void) snprintf(reason, MAX_REASON_LEN,
			    "No params for VF%d", i);
			return (DDI_FAILURE);
		}

		rval = ixgbe_get_param_unicast_slots(plist,
		    &unicast_slots, reason);
		if (rval != DDI_SUCCESS) {
			return (DDI_FAILURE);
		}
		IXGBE_DEBUGLOG_2(NULL,
		    "Validate unicast-slots for VF%d: %d", i, unicast_slots);

		if (unicast_slots != 0)
			total_slots += unicast_slots;
		else
			total_slots++;
	}

	if (total_slots > ixgbe_iov_param_unicast_slots.pd_max64) {
		(void) snprintf(reason, MAX_REASON_LEN,
		    "Greater than %d unicast-slots not supported",
		    (uint32_t)ixgbe_iov_param_unicast_slots.pd_max64);
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}
