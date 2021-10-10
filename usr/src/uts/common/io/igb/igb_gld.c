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

#include "igb_sw.h"

static int igb_set_priv_prop(igb_t *, const char *, uint_t, const void *);
static int igb_get_priv_prop(igb_t *, const char *, uint_t, void *);
static void igb_priv_prop_info(igb_t *, const char *, mac_prop_info_handle_t);
static boolean_t igb_param_locked(mac_prop_id_t);
static enum ioc_reply igb_iov_ioctl(igb_t *, struct iocblk *, mblk_t *);
static int igb_validate_iov_params(pci_param_t, char *);
static int igb_add_vf_vlan(igb_t *, uint16_t, uint32_t);
static int igb_rem_vf_vlan(igb_t *, uint16_t, uint32_t);
static int igb_disable_vf_port_vlan(igb_t *, uint16_t, uint32_t);

extern iov_param_desc_t igb_iov_param_list[IGB_NUM_IOV_PARAMS];

int
igb_m_stat(void *arg, uint_t stat, uint64_t *val)
{
	igb_t *igb = (igb_t *)arg;
	struct e1000_hw *hw = &igb->hw;
	igb_stat_t *igb_ks;
	uint32_t low_val, high_val;

	igb_ks = (igb_stat_t *)igb->igb_ks->ks_data;

	mutex_enter(&igb->gen_lock);

	if (IGB_IS_SUSPENDED_PIO(igb)) {
		mutex_exit(&igb->gen_lock);
		return (ECANCELED);
	}

	switch (stat) {
	case MAC_STAT_IFSPEED:
		*val = igb->link_speed * 1000000ull;
		break;

	case MAC_STAT_MULTIRCV:
		igb_ks->mprc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_MPRC);
		*val = igb_ks->mprc.value.ui64;
		break;

	case MAC_STAT_BRDCSTRCV:
		igb_ks->bprc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_BPRC);
		*val = igb_ks->bprc.value.ui64;
		break;

	case MAC_STAT_MULTIXMT:
		igb_ks->mptc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_MPTC);
		*val = igb_ks->mptc.value.ui64;
		break;

	case MAC_STAT_BRDCSTXMT:
		igb_ks->bptc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_BPTC);
		*val = igb_ks->bptc.value.ui64;
		break;

	case MAC_STAT_NORCVBUF:
		igb_ks->rnbc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_RNBC);
		*val = igb_ks->rnbc.value.ui64;
		break;

	case MAC_STAT_IERRORS:
		/* IERRORS = CRCERRS + RLEC + RXERRC + ALGNERRC */
		igb_ks->rxerrc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_RXERRC);
		igb_ks->algnerrc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_ALGNERRC);
		igb_ks->rlec.value.ui64 +=
		    E1000_READ_REG(hw, E1000_RLEC);
		igb_ks->crcerrs.value.ui64 +=
		    E1000_READ_REG(hw, E1000_CRCERRS);
		*val = igb_ks->rxerrc.value.ui64 +
		    igb_ks->algnerrc.value.ui64 +
		    igb_ks->rlec.value.ui64 +
		    igb_ks->crcerrs.value.ui64;
		break;

	case MAC_STAT_NOXMTBUF:
		*val = 0;
		break;

	case MAC_STAT_OERRORS:
		/* OERRORS = ECOL + LATECOL */
		igb_ks->ecol.value.ui64 +=
		    E1000_READ_REG(hw, E1000_ECOL);
		igb_ks->latecol.value.ui64 +=
		    E1000_READ_REG(hw, E1000_LATECOL);
		*val = igb_ks->ecol.value.ui64 +
		    igb_ks->latecol.value.ui64;
		break;

	case MAC_STAT_COLLISIONS:
		igb_ks->colc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_COLC);
		*val = igb_ks->colc.value.ui64;
		break;

	case MAC_STAT_RBYTES:
		/*
		 * The 64-bit register will reset whenever the upper
		 * 32 bits are read. So we need to read the lower
		 * 32 bits first, then read the upper 32 bits.
		 */
		low_val = E1000_READ_REG(hw, E1000_TORL);
		high_val = E1000_READ_REG(hw, E1000_TORH);
		igb_ks->tor.value.ui64 +=
		    (uint64_t)high_val << 32 | (uint64_t)low_val;
		*val = igb_ks->tor.value.ui64;
		break;

	case MAC_STAT_IPACKETS:
		igb_ks->tpr.value.ui64 +=
		    E1000_READ_REG(hw, E1000_TPR);
		*val = igb_ks->tpr.value.ui64;
		break;

	case MAC_STAT_OBYTES:
		/*
		 * The 64-bit register will reset whenever the upper
		 * 32 bits are read. So we need to read the lower
		 * 32 bits first, then read the upper 32 bits.
		 */
		low_val = E1000_READ_REG(hw, E1000_TOTL);
		high_val = E1000_READ_REG(hw, E1000_TOTH);
		igb_ks->tot.value.ui64 +=
		    (uint64_t)high_val << 32 | (uint64_t)low_val;
		*val = igb_ks->tot.value.ui64;
		break;

	case MAC_STAT_OPACKETS:
		igb_ks->tpt.value.ui64 +=
		    E1000_READ_REG(hw, E1000_TPT);
		*val = igb_ks->tpt.value.ui64;
		break;

	/* RFC 1643 stats */
	case ETHER_STAT_ALIGN_ERRORS:
		igb_ks->algnerrc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_ALGNERRC);
		*val = igb_ks->algnerrc.value.ui64;
		break;

	case ETHER_STAT_FCS_ERRORS:
		igb_ks->crcerrs.value.ui64 +=
		    E1000_READ_REG(hw, E1000_CRCERRS);
		*val = igb_ks->crcerrs.value.ui64;
		break;

	case ETHER_STAT_FIRST_COLLISIONS:
		igb_ks->scc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_SCC);
		*val = igb_ks->scc.value.ui64;
		break;

	case ETHER_STAT_MULTI_COLLISIONS:
		igb_ks->mcc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_MCC);
		*val = igb_ks->mcc.value.ui64;
		break;

	case ETHER_STAT_SQE_ERRORS:
		igb_ks->sec.value.ui64 +=
		    E1000_READ_REG(hw, E1000_SEC);
		*val = igb_ks->sec.value.ui64;
		break;

	case ETHER_STAT_DEFER_XMTS:
		igb_ks->dc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_DC);
		*val = igb_ks->dc.value.ui64;
		break;

	case ETHER_STAT_TX_LATE_COLLISIONS:
		igb_ks->latecol.value.ui64 +=
		    E1000_READ_REG(hw, E1000_LATECOL);
		*val = igb_ks->latecol.value.ui64;
		break;

	case ETHER_STAT_EX_COLLISIONS:
		igb_ks->ecol.value.ui64 +=
		    E1000_READ_REG(hw, E1000_ECOL);
		*val = igb_ks->ecol.value.ui64;
		break;

	case ETHER_STAT_MACXMT_ERRORS:
		igb_ks->ecol.value.ui64 +=
		    E1000_READ_REG(hw, E1000_ECOL);
		*val = igb_ks->ecol.value.ui64;
		break;

	case ETHER_STAT_CARRIER_ERRORS:
		igb_ks->cexterr.value.ui64 +=
		    E1000_READ_REG(hw, E1000_CEXTERR);
		*val = igb_ks->cexterr.value.ui64;
		break;

	case ETHER_STAT_TOOLONG_ERRORS:
		igb_ks->roc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_ROC);
		*val = igb_ks->roc.value.ui64;
		break;

	case ETHER_STAT_MACRCV_ERRORS:
		igb_ks->rxerrc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_RXERRC);
		*val = igb_ks->rxerrc.value.ui64;
		break;

	/* MII/GMII stats */
	case ETHER_STAT_XCVR_ADDR:
		/* The Internal PHY's MDI address for each MAC is 1 */
		*val = 1;
		break;

	case ETHER_STAT_XCVR_ID:
		*val = hw->phy.id | hw->phy.revision;
		break;

	case ETHER_STAT_XCVR_INUSE:
		switch (igb->link_speed) {
		case SPEED_1000:
			*val =
			    (hw->phy.media_type == e1000_media_type_copper) ?
			    XCVR_1000T : XCVR_1000X;
			break;
		case SPEED_100:
			*val =
			    (hw->phy.media_type == e1000_media_type_copper) ?
			    (igb->param_100t4_cap == 1) ?
			    XCVR_100T4 : XCVR_100T2 : XCVR_100X;
			break;
		case SPEED_10:
			*val = XCVR_10;
			break;
		default:
			*val = XCVR_NONE;
			break;
		}
		break;

	case ETHER_STAT_CAP_1000FDX:
		*val = igb->param_1000fdx_cap;
		break;

	case ETHER_STAT_CAP_1000HDX:
		*val = igb->param_1000hdx_cap;
		break;

	case ETHER_STAT_CAP_100FDX:
		*val = igb->param_100fdx_cap;
		break;

	case ETHER_STAT_CAP_100HDX:
		*val = igb->param_100hdx_cap;
		break;

	case ETHER_STAT_CAP_10FDX:
		*val = igb->param_10fdx_cap;
		break;

	case ETHER_STAT_CAP_10HDX:
		*val = igb->param_10hdx_cap;
		break;

	case ETHER_STAT_CAP_ASMPAUSE:
		*val = igb->param_asym_pause_cap;
		break;

	case ETHER_STAT_CAP_PAUSE:
		*val = igb->param_pause_cap;
		break;

	case ETHER_STAT_CAP_AUTONEG:
		*val = igb->param_autoneg_cap;
		break;

	case ETHER_STAT_ADV_CAP_1000FDX:
		*val = igb->param_adv_1000fdx_cap;
		break;

	case ETHER_STAT_ADV_CAP_1000HDX:
		*val = igb->param_adv_1000hdx_cap;
		break;

	case ETHER_STAT_ADV_CAP_100FDX:
		*val = igb->param_adv_100fdx_cap;
		break;

	case ETHER_STAT_ADV_CAP_100HDX:
		*val = igb->param_adv_100hdx_cap;
		break;

	case ETHER_STAT_ADV_CAP_10FDX:
		*val = igb->param_adv_10fdx_cap;
		break;

	case ETHER_STAT_ADV_CAP_10HDX:
		*val = igb->param_adv_10hdx_cap;
		break;

	case ETHER_STAT_ADV_CAP_ASMPAUSE:
		*val = igb->param_adv_asym_pause_cap;
		break;

	case ETHER_STAT_ADV_CAP_PAUSE:
		*val = igb->param_adv_pause_cap;
		break;

	case ETHER_STAT_ADV_CAP_AUTONEG:
		*val = hw->mac.autoneg;
		break;

	case ETHER_STAT_LP_CAP_1000FDX:
		*val = igb->param_lp_1000fdx_cap;
		break;

	case ETHER_STAT_LP_CAP_1000HDX:
		*val = igb->param_lp_1000hdx_cap;
		break;

	case ETHER_STAT_LP_CAP_100FDX:
		*val = igb->param_lp_100fdx_cap;
		break;

	case ETHER_STAT_LP_CAP_100HDX:
		*val = igb->param_lp_100hdx_cap;
		break;

	case ETHER_STAT_LP_CAP_10FDX:
		*val = igb->param_lp_10fdx_cap;
		break;

	case ETHER_STAT_LP_CAP_10HDX:
		*val = igb->param_lp_10hdx_cap;
		break;

	case ETHER_STAT_LP_CAP_ASMPAUSE:
		*val = igb->param_lp_asym_pause_cap;
		break;

	case ETHER_STAT_LP_CAP_PAUSE:
		*val = igb->param_lp_pause_cap;
		break;

	case ETHER_STAT_LP_CAP_AUTONEG:
		*val = igb->param_lp_autoneg_cap;
		break;

	case ETHER_STAT_LINK_ASMPAUSE:
		*val = igb->param_asym_pause_cap;
		break;

	case ETHER_STAT_LINK_PAUSE:
		*val = igb->param_pause_cap;
		break;

	case ETHER_STAT_LINK_AUTONEG:
		*val = hw->mac.autoneg;
		break;

	case ETHER_STAT_LINK_DUPLEX:
		if (igb->link_duplex)
			*val = (igb->link_duplex == FULL_DUPLEX) ?
			    LINK_DUPLEX_FULL : LINK_DUPLEX_HALF;
		else
			*val = LINK_DUPLEX_UNKNOWN;
		break;

	case ETHER_STAT_TOOSHORT_ERRORS:
		igb_ks->ruc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_RUC);
		*val = igb_ks->ruc.value.ui64;
		break;

	case ETHER_STAT_CAP_REMFAULT:
		*val = igb->param_rem_fault;
		break;

	case ETHER_STAT_ADV_REMFAULT:
		*val = igb->param_adv_rem_fault;
		break;

	case ETHER_STAT_LP_REMFAULT:
		*val = igb->param_lp_rem_fault;
		break;

	case ETHER_STAT_JABBER_ERRORS:
		igb_ks->rjc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_RJC);
		*val = igb_ks->rjc.value.ui64;
		break;

	case ETHER_STAT_CAP_100T4:
		*val = igb->param_100t4_cap;
		break;

	case ETHER_STAT_ADV_CAP_100T4:
		*val = igb->param_adv_100t4_cap;
		break;

	case ETHER_STAT_LP_CAP_100T4:
		*val = igb->param_lp_100t4_cap;
		break;

	default:
		mutex_exit(&igb->gen_lock);
		return (ENOTSUP);
	}

	mutex_exit(&igb->gen_lock);

	if (igb_check_acc_handle(igb->osdep.reg_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(igb->dip, DDI_SERVICE_DEGRADED);
		return (EIO);
	}

	return (0);
}

/*
 * Bring the device out of the reset/quiesced state that it
 * was in when the interface was registered.
 */
int
igb_m_start(void *arg)
{
	igb_t *igb = (igb_t *)arg;

	mutex_enter(&igb->gen_lock);

	if (IGB_IS_SUSPENDED(igb)) {
		igb->sr_reconfigure = igb->sr_reconfigure &
		    ~IGB_SR_RC_STOP | IGB_SR_RC_START;
		mutex_exit(&igb->gen_lock);
		return (0);
	}

	if (igb_start(igb) != IGB_SUCCESS) {
		mutex_exit(&igb->gen_lock);
		return (EIO);
	}

	atomic_or_32(&igb->igb_state, IGB_STARTED);

	mutex_exit(&igb->gen_lock);

	/*
	 * Enable and start the watchdog timer
	 */
	igb_enable_watchdog_timer(igb);

	return (0);
}

/*
 * Stop the device and put it in a reset/quiesced state such
 * that the interface can be unregistered.
 */
void
igb_m_stop(void *arg)
{
	igb_t *igb = (igb_t *)arg;

	mutex_enter(&igb->gen_lock);

	if (IGB_IS_SUSPENDED(igb)) {
		igb->sr_reconfigure = igb->sr_reconfigure &
		    ~IGB_SR_RC_START | IGB_SR_RC_STOP;
		mutex_exit(&igb->gen_lock);
		return;
	}

	atomic_and_32(&igb->igb_state, ~IGB_STARTED);

	igb_stop(igb);

	mutex_exit(&igb->gen_lock);

	/*
	 * Disable and stop the watchdog timer
	 */
	igb_disable_watchdog_timer(igb);
}

/*
 * Set the promiscuity of the device.
 */
int
igb_m_promisc(void *arg, boolean_t on)
{
	igb_t *igb = (igb_t *)arg;

	mutex_enter(&igb->gen_lock);

	igb->promisc_mode = on;

	if (IGB_IS_SUSPENDED_PIO(igb)) {
		/* We've saved the setting and it'll take effect in resume */
		mutex_exit(&igb->gen_lock);
		return (0);
	}

	igb_setup_promisc(igb);

	mutex_exit(&igb->gen_lock);

	if (igb_check_acc_handle(igb->osdep.reg_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(igb->dip, DDI_SERVICE_DEGRADED);
		return (EIO);
	}

	return (0);
}

/*
 * Add/remove the addresses to/from the set of multicast
 * addresses for which the device will receive packets.
 */
int
igb_m_multicst(void *arg, boolean_t add, const uint8_t *mcst_addr)
{
	igb_t *igb = (igb_t *)arg;
	int result;

	mutex_enter(&igb->gen_lock);

	result = (add) ? igb_multicst_add(igb, mcst_addr)
	    : igb_multicst_remove(igb, mcst_addr);

	if (result != 0)
		goto multicst_end;

	if (IGB_IS_SUSPENDED_PIO(igb)) {
		/* We've saved the setting and it'll take effect in resume */
		goto multicst_end;
	}

	/*
	 * Update the multicast table in the hardware
	 */
	igb_setup_multicst(igb, igb->sriov_pf ? igb->pf_grp : 0);

	if (igb_check_acc_handle(igb->osdep.reg_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(igb->dip, DDI_SERVICE_DEGRADED);
		result = EIO;
	}

multicst_end:
	mutex_exit(&igb->gen_lock);

	return (result);
}

/*
 * Pass on M_IOCTL messages passed to the DLD, and support
 * private IOCTLs for debugging and ndd.
 */
void
igb_m_ioctl(void *arg, queue_t *q, mblk_t *mp)
{
	igb_t *igb = (igb_t *)arg;
	struct iocblk *iocp;
	enum ioc_reply status;

	iocp = (struct iocblk *)(uintptr_t)mp->b_rptr;
	iocp->ioc_error = 0;

	mutex_enter(&igb->gen_lock);
	if (IGB_IS_SUSPENDED(igb)) {
		mutex_exit(&igb->gen_lock);
		miocnak(q, mp, 0, EINVAL);
		return;
	}
	mutex_exit(&igb->gen_lock);

	switch (iocp->ioc_cmd) {
	case LB_GET_INFO_SIZE:
	case LB_GET_INFO:
	case LB_GET_MODE:
	case LB_SET_MODE:
		status = igb_loopback_ioctl(igb, iocp, mp);
		break;

	case IOV_GET_PARAM_VER_INFO:
	case IOV_GET_PARAM_INFO:
	case IOV_VALIDATE_PARAM:
		status = igb_iov_ioctl(igb, iocp, mp);
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
		miocack(q, mp, iocp->ioc_count, iocp->ioc_error);
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
igb_add_mac(void *arg, const uint8_t *mac_addr, uint64_t flags)
{
	_NOTE(ARGUNUSED(flags))

	igb_group_t *group = (igb_group_t *)arg;
	igb_t *igb = group->igb;
	int ret;

	mutex_enter(&igb->gen_lock);

	/* add given address for the given group */
	ret = igb_unicst_add(igb, mac_addr, group->index);
	if (ret) {
		IGB_DEBUGLOG_1(igb,
		    "Can not add mac address, ret = %d", ret);
	}

	mutex_exit(&igb->gen_lock);

	return (ret);
}

/*
 * Remove a MAC address from the specified RX group.
 */
static int
igb_rem_mac(void *arg, const uint8_t *mac_addr)
{
	igb_group_t *group = (igb_group_t *)arg;
	igb_t *igb = group->igb;
	int ret;

	mutex_enter(&igb->gen_lock);

	/* remove given address */
	ret = igb_unicst_remove(igb, mac_addr, group->index);

	mutex_exit(&igb->gen_lock);

	return (ret);
}

static int
igb_add_vlan_filter(void *arg, uint16_t vlan_id, uint32_t flags)
{
	igb_group_t	*group = (igb_group_t *)arg;
	igb_t		*igb = group->igb;
	vf_data_t	*vf_data = &igb->vf[group->index];
	int		ret = 0;

	ASSERT(group->index < igb->pf_grp);
	ASSERT(vlan_id > 0);

	mutex_enter(&igb->gen_lock);

	/* Pvid and vid can't co-exist */
	if (flags == 0) {
		if (vf_data->port_vlan_id == 0) {
			ret = igb_add_vf_vlan(igb, vlan_id, group->index);
		} else {
			cmn_err(CE_WARN, "setting for vid is not "
			    "allowed when pvid is set");
			ret = EINVAL;
		}
	} else if (flags == MAC_GROUP_VLAN_TRANSPARENT_ENABLE) {
		if (vf_data->num_vlans == 0)
			ret = igb_enable_vf_port_vlan(igb, vlan_id,
			    group->index);
		else {
			cmn_err(CE_WARN, "setting for pvid is not "
			    "allowed when vid is set");
			ret = EINVAL;
		}
	} else {
		ret = EINVAL;
	}

	mutex_exit(&igb->gen_lock);

	return (ret);
}

static int
igb_remove_vlan_filter(void *arg, uint16_t vlan_id)
{
	igb_group_t	*group = (igb_group_t *)arg;
	igb_t		*igb = group->igb;
	int		ret = 0;

	ASSERT(group->index < igb->pf_grp);
	ASSERT(vlan_id > 0);

	mutex_enter(&igb->gen_lock);

	if (igb->vf[group->index].port_vlan_id == vlan_id)
		ret = igb_disable_vf_port_vlan(igb, vlan_id, group->index);
	else
		ret = igb_rem_vf_vlan(igb, vlan_id, group->index);

	mutex_exit(&igb->gen_lock);

	return (ret);
}

static int
igb_set_mtu(void *arg, uint32_t mtu)
{
	igb_group_t	*group = (igb_group_t *)arg;

	ASSERT(group->index < group->igb->pf_grp);

	if ((mtu > MAX_MTU) || (mtu < MIN_MTU)) {
		IGB_DEBUGLOG_2(group->igb,
		    "Invalid Max MTU size %d for VF%d", mtu, group->index);
		return (EINVAL);
	}

	group->vf->max_mtu = mtu;

	return (0);
}

static int
igb_get_sriov_info(void *arg, mac_sriov_info_t *infop)
{
	igb_group_t	*group = (igb_group_t *)arg;

	ASSERT(group->index < group->igb->pf_grp);

	/*
	 * Return the group index, which equals to the vf index
	 */
	infop->msi_vf_index = group->index;

	return (0);
}

/*
 * Enable interrupt on the specified rx ring.
 */
int
igb_rx_ring_intr_enable(mac_ring_driver_t rh)
{
	igb_rx_ring_t *rx_ring = (igb_rx_ring_t *)rh;
	igb_t *igb = rx_ring->igb;
	struct e1000_hw *hw = &igb->hw;
	uint32_t vect_bit = (1 << rx_ring->int_vec);

	if (!igb->polling_enable)
		return (0);

	mutex_enter(&igb->gen_lock);

	if (igb->intr_type == DDI_INTR_TYPE_MSIX) {
		igb->eims_mask |= vect_bit;
	} else {
		igb->ims_mask |= E1000_IMS_RXT0;
	}

	if (IGB_IS_SUSPENDED_INTR(igb)) {
		/* We've saved the setting and it'll take effect in resume */
		mutex_exit(&igb->gen_lock);
		return (0);
	}

	if (igb->intr_type == DDI_INTR_TYPE_MSIX) {
		/* Interrupt enabling for MSI-X */
		E1000_WRITE_REG(hw, E1000_EIMS, igb->eims_mask);
		E1000_WRITE_REG(hw, E1000_EIAC, igb->eims_mask);
	} else {
		/* Interrupt enabling for MSI and legacy */
		E1000_WRITE_REG(hw, E1000_IMS, igb->ims_mask);
	}

	E1000_WRITE_FLUSH(hw);
	mutex_exit(&igb->gen_lock);

	return (0);
}

/*
 * Disable interrupt on the specified rx ring.
 */
int
igb_rx_ring_intr_disable(mac_ring_driver_t rh)
{
	igb_rx_ring_t *rx_ring = (igb_rx_ring_t *)rh;
	igb_t *igb = rx_ring->igb;
	struct e1000_hw *hw = &igb->hw;
	uint32_t vect_bit = (1 << rx_ring->int_vec);

	if (!igb->polling_enable)
		return (0);

	mutex_enter(&igb->gen_lock);

	if (igb->intr_type == DDI_INTR_TYPE_MSIX) {
		igb->eims_mask &= ~vect_bit;
	} else {
		igb->ims_mask &= ~E1000_IMS_RXT0;
	}

	if (IGB_IS_SUSPENDED_INTR(igb)) {
		/* We've saved the setting and it'll take effect in resume */
		mutex_exit(&igb->gen_lock);
		return (0);
	}

	if (igb->intr_type == DDI_INTR_TYPE_MSIX) {
		/* Interrupt disabling for MSI-X */
		E1000_WRITE_REG(hw, E1000_EIMC, vect_bit);
		E1000_WRITE_REG(hw, E1000_EIAC, igb->eims_mask);
	} else {
		/* Interrupt disabling for MSI and legacy */
		E1000_WRITE_REG(hw, E1000_IMC, E1000_IMS_RXT0);
	}

	E1000_WRITE_FLUSH(hw);
	mutex_exit(&igb->gen_lock);

	return (0);
}

/*
 * Get the global ring index by a ring index within a group.
 */
int
igb_get_rx_ring_index(igb_t *igb, int gindex, int rindex)
{
	igb_rx_ring_t *rx_ring;
	int i;

	for (i = 0; i < igb->num_rx_rings; i++) {
		rx_ring = &igb->rx_rings[i];
		if (rx_ring->group_index == gindex)
			rindex--;
		if (rindex < 0)
			return (i);
	}

	return (-1);
}

static int
igb_ring_start(mac_ring_driver_t rh, uint64_t mr_gen_num)
{
	igb_rx_ring_t *rx_ring = (igb_rx_ring_t *)rh;

	mutex_enter(&rx_ring->rx_lock);
	rx_ring->ring_gen_num = mr_gen_num;
	mutex_exit(&rx_ring->rx_lock);
	return (0);
}

/*
 * Callback funtion for MAC layer to register all rings.
 */
/* ARGSUSED */
void
igb_fill_ring(void *arg, mac_ring_type_t rtype, const int rg_index,
    const int index, mac_ring_info_t *infop, mac_ring_handle_t rh)
{
	igb_t *igb = (igb_t *)arg;
	mac_intr_t *mintr = &infop->mri_intr;

	switch (rtype) {
	case MAC_RING_TYPE_RX: {
		igb_rx_ring_t *rx_ring;
		int global_index;

		/* Nothing to do for SRIOV VF groups */
		if (igb->sriov_pf && (rg_index < igb->pf_grp)) {
			IGB_DEBUGLOG_1(igb,
			    "Not fill rx ring for VF group %d", rg_index);
			break;
		}

		/*
		 * 'index' is the ring index within the group.
		 * We need the global ring index by searching in group.
		 */
		global_index = igb_get_rx_ring_index(igb, rg_index, index);

		ASSERT(global_index >= 0);

		rx_ring = &igb->rx_rings[global_index];
		rx_ring->ring_handle = rh;

		infop->mri_driver = (mac_ring_driver_t)rx_ring;
		infop->mri_start = igb_ring_start;
		infop->mri_stop = NULL;
		infop->mri_poll = (mac_ring_poll_t)igb_rx_ring_poll;
		infop->mri_stat = igb_rx_ring_stat;

		mintr->mi_enable = igb_rx_ring_intr_enable;
		mintr->mi_disable = igb_rx_ring_intr_disable;
		if (igb->intr_type & (DDI_INTR_TYPE_MSIX | DDI_INTR_TYPE_MSI)) {
			mintr->mi_ddi_handle =
			    igb->htable[rx_ring->int_vec];
		}
		break;
	}
	case MAC_RING_TYPE_TX: {
		igb_tx_ring_t *tx_ring;

		/* Nothing to do for SRIOV VF groups */
		if (igb->sriov_pf && (rg_index < igb->pf_grp)) {
			IGB_DEBUGLOG_1(igb,
			    "Not fill tx ring for VF group %d", rg_index);
			break;
		}

		ASSERT(index < igb->num_tx_rings);

		tx_ring = &igb->tx_rings[index];
		tx_ring->ring_handle = rh;

		infop->mri_driver = (mac_ring_driver_t)tx_ring;
		infop->mri_start = NULL;
		infop->mri_stop = NULL;
		infop->mri_tx = igb_tx_ring_send;
		infop->mri_stat = igb_tx_ring_stat;

		if (igb->intr_type & (DDI_INTR_TYPE_MSIX | DDI_INTR_TYPE_MSI)) {
			mintr->mi_ddi_handle =
			    igb->htable[tx_ring->int_vec];
		}
		break;
	}
	default:
		break;
	}
}

void
igb_fill_group(void *arg, mac_ring_type_t rtype, const int index,
    mac_group_info_t *infop, mac_group_handle_t gh)
{
	igb_t *igb = (igb_t *)arg;
	igb_group_t *group;

	ASSERT((index >= 0) && (index < igb->num_groups));
	group = &igb->groups[index];

	switch (rtype) {
	case MAC_RING_TYPE_RX:
		group->rg_handle = gh;

		infop->mgi_driver = (mac_group_driver_t)group;
		infop->mgi_start = NULL;
		infop->mgi_stop = NULL;
		infop->mgi_addmac = igb_add_mac;
		infop->mgi_remmac = igb_rem_mac;
		infop->mgi_setmtu = NULL;
		infop->mgi_getsriov_info = NULL;
		infop->mgi_addvlan = NULL;
		infop->mgi_remvlan = NULL;

		if (igb->sriov_pf && (index < igb->pf_grp)) {
			/* For SRIOV VF groups */
			infop->mgi_count = 0;
			infop->mgi_addvlan = igb_add_vlan_filter;
			infop->mgi_remvlan = igb_remove_vlan_filter;
			infop->mgi_setmtu = igb_set_mtu;
			infop->mgi_getsriov_info = igb_get_sriov_info;
			infop->mgi_flags = MAC_GROUP_VLAN_TRANSPARENT_ALL;
		} else {
			/* For SRIOV PF group or non-SRIOV groups */
			infop->mgi_count = igb->rxq_per_group;
			if (index == igb->pf_grp) {
				infop->mgi_flags = MAC_GROUP_DEFAULT |
				    MAC_GROUP_VLAN_TRANSPARENT_ALL;
			}
		}
		break;

	case MAC_RING_TYPE_TX:
		ASSERT(igb->sriov_pf);

		group->tg_handle = gh;

		infop->mgi_driver = (mac_group_driver_t)group;
		infop->mgi_start = NULL;
		infop->mgi_stop = NULL;
		infop->mgi_addmac = NULL;
		infop->mgi_remmac = NULL;
		infop->mgi_addvlan = NULL;
		infop->mgi_remvlan = NULL;
		infop->mgi_setmtu = NULL;
		infop->mgi_getsriov_info = NULL;

		if (index < igb->pf_grp) {
			/* For SRIOV VF groups */
			infop->mgi_count = 0;
			infop->mgi_getsriov_info = igb_get_sriov_info;
			infop->mgi_flags = MAC_GROUP_VLAN_TRANSPARENT_ALL;
		} else {
			/* For SRIOV PF group */
			infop->mgi_count = igb->txq_per_group;
			infop->mgi_flags = MAC_GROUP_DEFAULT |
			    MAC_GROUP_VLAN_TRANSPARENT_ALL;
		}
		break;

	default:
		break;
	}
}

/*
 * Obtain the MAC's capabilities and associated data from
 * the driver.
 */
boolean_t
igb_m_getcapab(void *arg, mac_capab_t cap, void *cap_data)
{
	igb_t *igb = (igb_t *)arg;

	switch (cap) {
	case MAC_CAPAB_HCKSUM: {
		uint32_t *tx_hcksum_flags = cap_data;

		/*
		 * We advertise our capabilities only if tx hcksum offload is
		 * enabled.  On receive, the stack will accept checksummed
		 * packets anyway, even if we haven't said we can deliver
		 * them.
		 */
		if (!igb->tx_hcksum_enable)
			return (B_FALSE);

		*tx_hcksum_flags = HCKSUM_INET_PARTIAL | HCKSUM_IPHDRCKSUM;
		break;
	}
	case MAC_CAPAB_LSO: {
		mac_capab_lso_t *cap_lso = cap_data;

		if (igb->lso_enable) {
			cap_lso->lso_flags = LSO_TX_BASIC_TCP_IPV4;
			cap_lso->lso_basic_tcp_ipv4.lso_max = IGB_LSO_MAXLEN;
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
			cap_rings->mr_flags = 0;
			cap_rings->mr_group_type = MAC_GROUP_TYPE_STATIC;
			cap_rings->mr_rnum = igb->num_rx_rings;
			cap_rings->mr_gnum = igb->num_groups;
			cap_rings->mr_rget = igb_fill_ring;
			cap_rings->mr_gget = igb_fill_group;
			cap_rings->mr_gaddring = NULL;
			cap_rings->mr_gremring = NULL;

			break;
		case MAC_RING_TYPE_TX:
			cap_rings->mr_flags = 0;
			cap_rings->mr_group_type = MAC_GROUP_TYPE_STATIC;
			cap_rings->mr_rnum = igb->num_tx_rings;
			cap_rings->mr_rget = igb_fill_ring;
			if (igb->sriov_pf) {
				cap_rings->mr_gnum = igb->num_groups;
				cap_rings->mr_gget = igb_fill_group;
			} else {
				/* Not advertise tx groups in non-SRIOV mode */
				cap_rings->mr_gnum = 1;
				cap_rings->mr_gget = NULL;
			}
			cap_rings->mr_gaddring = NULL;
			cap_rings->mr_gremring = NULL;
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
igb_m_setprop(void *arg, const char *pr_name, mac_prop_id_t pr_num,
    uint_t pr_valsize, const void *pr_val)
{
	igb_t *igb = (igb_t *)arg;
	struct e1000_hw *hw = &igb->hw;
	int err = 0;
	uint32_t flow_control;
	uint32_t cur_mtu, new_mtu;
	uint32_t rx_size;
	uint32_t tx_size;

	mutex_enter(&igb->gen_lock);

	if (igb->loopback_mode != IGB_LB_NONE && igb_param_locked(pr_num)) {
		/*
		 * All en_* parameters are locked (read-only)
		 * while the device is in any sort of loopback mode.
		 */
		mutex_exit(&igb->gen_lock);
		return (EBUSY);
	}

	switch (pr_num) {
	case MAC_PROP_EN_1000FDX_CAP:
		/* read/write on copper, read-only on serdes */
		if (hw->phy.media_type != e1000_media_type_copper) {
			err = ENOTSUP;
			break;
		}
		igb->param_en_1000fdx_cap = *(uint8_t *)pr_val;
		igb->param_adv_1000fdx_cap = *(uint8_t *)pr_val;
		goto setup_link;
	case MAC_PROP_EN_100FDX_CAP:
		if (hw->phy.media_type != e1000_media_type_copper) {
			err = ENOTSUP;
			break;
		}
		igb->param_en_100fdx_cap = *(uint8_t *)pr_val;
		igb->param_adv_100fdx_cap = *(uint8_t *)pr_val;
		goto setup_link;
	case MAC_PROP_EN_100HDX_CAP:
		if (hw->phy.media_type != e1000_media_type_copper) {
			err = ENOTSUP;
			break;
		}
		igb->param_en_100hdx_cap = *(uint8_t *)pr_val;
		igb->param_adv_100hdx_cap = *(uint8_t *)pr_val;
		goto setup_link;
	case MAC_PROP_EN_10FDX_CAP:
		if (hw->phy.media_type != e1000_media_type_copper) {
			err = ENOTSUP;
			break;
		}
		igb->param_en_10fdx_cap = *(uint8_t *)pr_val;
		igb->param_adv_10fdx_cap = *(uint8_t *)pr_val;
		goto setup_link;
	case MAC_PROP_EN_10HDX_CAP:
		if (hw->phy.media_type != e1000_media_type_copper) {
			err = ENOTSUP;
			break;
		}
		igb->param_en_10hdx_cap = *(uint8_t *)pr_val;
		igb->param_adv_10hdx_cap = *(uint8_t *)pr_val;
		goto setup_link;
	case MAC_PROP_AUTONEG:
		if (hw->phy.media_type != e1000_media_type_copper) {
			err = ENOTSUP;
			break;
		}
		igb->param_adv_autoneg_cap = *(uint8_t *)pr_val;
		goto setup_link;
	case MAC_PROP_FLOWCTRL:
		bcopy(pr_val, &flow_control, sizeof (flow_control));

		switch (flow_control) {
		default:
			err = ENOTSUP;
			break;
		case LINK_FLOWCTRL_NONE:
			hw->fc.requested_mode = e1000_fc_none;
			break;
		case LINK_FLOWCTRL_RX:
			hw->fc.requested_mode = e1000_fc_rx_pause;
			break;
		case LINK_FLOWCTRL_TX:
			hw->fc.requested_mode = e1000_fc_tx_pause;
			break;
		case LINK_FLOWCTRL_BI:
			hw->fc.requested_mode = e1000_fc_full;
			break;
		}
setup_link:
		if (err == 0) {
			if (igb_setup_link(igb, B_TRUE) != IGB_SUCCESS)
				err = EINVAL;
		}
		break;
	case MAC_PROP_ADV_1000FDX_CAP:
	case MAC_PROP_ADV_1000HDX_CAP:
	case MAC_PROP_ADV_100T4_CAP:
	case MAC_PROP_ADV_100FDX_CAP:
	case MAC_PROP_ADV_100HDX_CAP:
	case MAC_PROP_ADV_10FDX_CAP:
	case MAC_PROP_ADV_10HDX_CAP:
	case MAC_PROP_EN_1000HDX_CAP:
	case MAC_PROP_EN_100T4_CAP:
	case MAC_PROP_STATUS:
	case MAC_PROP_SPEED:
	case MAC_PROP_DUPLEX:
		err = ENOTSUP; /* read-only prop. Can't set this. */
		break;
	case MAC_PROP_MTU:
		/* adapter must be stopped for an MTU change */
		if (IGB_IS_STARTED(igb)) {
			err = EBUSY;
			break;
		}

		cur_mtu = igb->default_mtu;
		bcopy(pr_val, &new_mtu, sizeof (new_mtu));
		if (new_mtu == cur_mtu) {
			err = 0;
			break;
		}

		if (new_mtu < MIN_MTU || new_mtu > MAX_MTU) {
			err = EINVAL;
			break;
		}

		err = mac_maxsdu_update(igb->mac_hdl, new_mtu);
		if (err == 0) {
			igb->default_mtu = new_mtu;
			igb->max_frame_size = igb->default_mtu +
			    sizeof (struct ether_vlan_header) + ETHERFCSL;

			/*
			 * Set rx buffer size
			 */
			rx_size = igb->max_frame_size + IPHDR_ALIGN_ROOM;
			igb->rx_buf_size = ((rx_size >> 10) + ((rx_size &
			    (((uint32_t)1 << 10) - 1)) > 0 ? 1 : 0)) << 10;

			/*
			 * Set tx buffer size
			 */
			tx_size = igb->max_frame_size;
			igb->tx_buf_size = ((tx_size >> 10) + ((tx_size &
			    (((uint32_t)1 << 10) - 1)) > 0 ? 1 : 0)) << 10;
		}
		break;
	case MAC_PROP_PRIVATE:
		err = igb_set_priv_prop(igb, pr_name, pr_valsize, pr_val);
		break;
	default:
		err = ENOTSUP;
		break;
	}

	mutex_exit(&igb->gen_lock);

	return (err);
}

int
igb_m_getprop(void *arg, const char *pr_name, mac_prop_id_t pr_num,
    uint_t pr_valsize, void *pr_val)
{
	igb_t *igb = (igb_t *)arg;
	struct e1000_hw *hw = &igb->hw;
	int err = 0;
	uint32_t flow_control;
	uint64_t tmp = 0;

	switch (pr_num) {
	case MAC_PROP_DUPLEX:
		ASSERT(pr_valsize >= sizeof (link_duplex_t));
		bcopy(&igb->link_duplex, pr_val, sizeof (link_duplex_t));
		break;
	case MAC_PROP_SPEED:
		ASSERT(pr_valsize >= sizeof (uint64_t));
		tmp = igb->link_speed * 1000000ull;
		bcopy(&tmp, pr_val, sizeof (tmp));
		break;
	case MAC_PROP_AUTONEG:
		ASSERT(pr_valsize >= sizeof (uint8_t));
		*(uint8_t *)pr_val = igb->param_adv_autoneg_cap;
		break;
	case MAC_PROP_FLOWCTRL:
		ASSERT(pr_valsize >= sizeof (uint32_t));
		switch (hw->fc.requested_mode) {
			case e1000_fc_none:
				flow_control = LINK_FLOWCTRL_NONE;
				break;
			case e1000_fc_rx_pause:
				flow_control = LINK_FLOWCTRL_RX;
				break;
			case e1000_fc_tx_pause:
				flow_control = LINK_FLOWCTRL_TX;
				break;
			case e1000_fc_full:
				flow_control = LINK_FLOWCTRL_BI;
				break;
		}
		bcopy(&flow_control, pr_val, sizeof (flow_control));
		break;
	case MAC_PROP_ADV_1000FDX_CAP:
		*(uint8_t *)pr_val = igb->param_adv_1000fdx_cap;
		break;
	case MAC_PROP_EN_1000FDX_CAP:
		*(uint8_t *)pr_val = igb->param_en_1000fdx_cap;
		break;
	case MAC_PROP_ADV_1000HDX_CAP:
		*(uint8_t *)pr_val = igb->param_adv_1000hdx_cap;
		break;
	case MAC_PROP_EN_1000HDX_CAP:
		*(uint8_t *)pr_val = igb->param_en_1000hdx_cap;
		break;
	case MAC_PROP_ADV_100T4_CAP:
		*(uint8_t *)pr_val = igb->param_adv_100t4_cap;
		break;
	case MAC_PROP_EN_100T4_CAP:
		*(uint8_t *)pr_val = igb->param_en_100t4_cap;
		break;
	case MAC_PROP_ADV_100FDX_CAP:
		*(uint8_t *)pr_val = igb->param_adv_100fdx_cap;
		break;
	case MAC_PROP_EN_100FDX_CAP:
		*(uint8_t *)pr_val = igb->param_en_100fdx_cap;
		break;
	case MAC_PROP_ADV_100HDX_CAP:
		*(uint8_t *)pr_val = igb->param_adv_100hdx_cap;
		break;
	case MAC_PROP_EN_100HDX_CAP:
		*(uint8_t *)pr_val = igb->param_en_100hdx_cap;
		break;
	case MAC_PROP_ADV_10FDX_CAP:
		*(uint8_t *)pr_val = igb->param_adv_10fdx_cap;
		break;
	case MAC_PROP_EN_10FDX_CAP:
		*(uint8_t *)pr_val = igb->param_en_10fdx_cap;
		break;
	case MAC_PROP_ADV_10HDX_CAP:
		*(uint8_t *)pr_val = igb->param_adv_10hdx_cap;
		break;
	case MAC_PROP_EN_10HDX_CAP:
		*(uint8_t *)pr_val = igb->param_en_10hdx_cap;
		break;
	case MAC_PROP_PRIVATE:
		err = igb_get_priv_prop(igb, pr_name, pr_valsize, pr_val);
		break;
	default:
		err = ENOTSUP;
		break;
	}
	return (err);
}

void
igb_m_propinfo(void *arg, const char *pr_name, mac_prop_id_t pr_num,
    mac_prop_info_handle_t prh)
{
	igb_t *igb = (igb_t *)arg;
	struct e1000_hw *hw = &igb->hw;

	switch (pr_num) {
	case MAC_PROP_DUPLEX:
	case MAC_PROP_SPEED:
	case MAC_PROP_ADV_1000FDX_CAP:
	case MAC_PROP_ADV_1000HDX_CAP:
	case MAC_PROP_EN_1000HDX_CAP:
	case MAC_PROP_ADV_100T4_CAP:
	case MAC_PROP_EN_100T4_CAP:
		mac_prop_info_set_perm(prh, MAC_PROP_PERM_READ);
		break;

	case MAC_PROP_EN_1000FDX_CAP:
		if (hw->phy.media_type != e1000_media_type_copper)
			mac_prop_info_set_perm(prh, MAC_PROP_PERM_READ);
		else
			mac_prop_info_set_default_uint8(prh,
			    igb->param_1000fdx_cap);
		break;

	case MAC_PROP_ADV_100FDX_CAP:
	case MAC_PROP_EN_100FDX_CAP:
		if (hw->phy.media_type != e1000_media_type_copper)
			mac_prop_info_set_perm(prh, MAC_PROP_PERM_READ);
		else
			mac_prop_info_set_default_uint8(prh,
			    igb->param_100fdx_cap);
		break;

	case MAC_PROP_ADV_100HDX_CAP:
	case MAC_PROP_EN_100HDX_CAP:
		if (hw->phy.media_type != e1000_media_type_copper)
			mac_prop_info_set_perm(prh, MAC_PROP_PERM_READ);
		else
			mac_prop_info_set_default_uint8(prh,
			    igb->param_100hdx_cap);
		break;

	case MAC_PROP_ADV_10FDX_CAP:
	case MAC_PROP_EN_10FDX_CAP:
		if (hw->phy.media_type != e1000_media_type_copper)
			mac_prop_info_set_perm(prh, MAC_PROP_PERM_READ);
		else
			mac_prop_info_set_default_uint8(prh,
			    igb->param_10fdx_cap);
		break;

	case MAC_PROP_ADV_10HDX_CAP:
	case MAC_PROP_EN_10HDX_CAP:
		if (hw->phy.media_type != e1000_media_type_copper)
			mac_prop_info_set_perm(prh, MAC_PROP_PERM_READ);
		else
			mac_prop_info_set_default_uint8(prh,
			    igb->param_10hdx_cap);
		break;

	case MAC_PROP_AUTONEG:
		if (hw->phy.media_type != e1000_media_type_copper)
			mac_prop_info_set_perm(prh, MAC_PROP_PERM_READ);
		else
			mac_prop_info_set_default_uint8(prh,
			    igb->param_autoneg_cap);
		break;

	case MAC_PROP_FLOWCTRL:
		mac_prop_info_set_default_link_flowctrl(prh, LINK_FLOWCTRL_BI);
		break;

	case MAC_PROP_MTU:
		mac_prop_info_set_range_uint32(prh, MIN_MTU, MAX_MTU);
		break;

	case MAC_PROP_PRIVATE:
		igb_priv_prop_info(igb, pr_name, prh);
		break;
	}

}

static boolean_t
igb_param_locked(mac_prop_id_t pr_num)
{
	/*
	 * All en_* parameters are locked (read-only) while
	 * the device is in any sort of loopback mode ...
	 */
	switch (pr_num) {
		case MAC_PROP_EN_1000FDX_CAP:
		case MAC_PROP_EN_1000HDX_CAP:
		case MAC_PROP_EN_100T4_CAP:
		case MAC_PROP_EN_100FDX_CAP:
		case MAC_PROP_EN_100HDX_CAP:
		case MAC_PROP_EN_10FDX_CAP:
		case MAC_PROP_EN_10HDX_CAP:
		case MAC_PROP_AUTONEG:
		case MAC_PROP_FLOWCTRL:
			return (B_TRUE);
	}
	return (B_FALSE);
}

/* ARGSUSED */
static int
igb_set_priv_prop(igb_t *igb, const char *pr_name,
    uint_t pr_valsize, const void *pr_val)
{
	int err = 0;
	long result;
	struct e1000_hw *hw = &igb->hw;
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
			igb->tx_copy_thresh = (uint32_t)result;
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
			igb->tx_recycle_thresh = (uint32_t)result;
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
			igb->tx_overload_thresh = (uint32_t)result;
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
		    result > igb->tx_ring_size)
			err = EINVAL;
		else {
			igb->tx_resched_thresh = (uint32_t)result;
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
			igb->rx_copy_thresh = (uint32_t)result;
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
			igb->rx_limit_per_intr = (uint32_t)result;
		}
		return (err);
	}
	if (strcmp(pr_name, "_intr_throttling") == 0) {
		if (pr_val == NULL) {
			err = EINVAL;
			return (err);
		}
		(void) ddi_strtol(pr_val, (char **)NULL, 0, &result);

		if (result < igb->capab->min_intr_throttle ||
		    result > igb->capab->max_intr_throttle)
			err = EINVAL;
		else if (result != igb->intr_throttling[0]) {
			igb->intr_throttling[0] = (uint32_t)result;

			for (i = 0; i < MAX_NUM_EITR; i++)
				igb->intr_throttling[i] =
				    igb->intr_throttling[0];

			if (IGB_IS_SUSPENDED_PIO(igb)) {
				/* The setting will take effect in resume */
				return (err);
			}

			/* Set interrupt throttling rate */
			for (i = 0; i < igb->intr_cnt; i++)
				E1000_WRITE_REG(hw, E1000_EITR(i),
				    igb->intr_throttling[i]);

			if (igb_check_acc_handle(igb->osdep.reg_handle) !=
			    DDI_FM_OK) {
				ddi_fm_service_impact(igb->dip,
				    DDI_SERVICE_DEGRADED);
				return (EIO);
			}
		}
		return (err);
	}
	return (ENOTSUP);
}

static int
igb_get_priv_prop(igb_t *igb, const char *pr_name, uint_t pr_valsize,
    void *pr_val)
{
	int value;

	if (strcmp(pr_name, "_adv_pause_cap") == 0) {
		value = igb->param_adv_pause_cap;
	} else if (strcmp(pr_name, "_adv_asym_pause_cap") == 0) {
		value = igb->param_adv_asym_pause_cap;
	} else if (strcmp(pr_name, "_tx_copy_thresh") == 0) {
		value = igb->tx_copy_thresh;
	} else if (strcmp(pr_name, "_tx_recycle_thresh") == 0) {
		value = igb->tx_recycle_thresh;
	} else if (strcmp(pr_name, "_tx_overload_thresh") == 0) {
		value = igb->tx_overload_thresh;
	} else if (strcmp(pr_name, "_tx_resched_thresh") == 0) {
		value = igb->tx_resched_thresh;
	} else if (strcmp(pr_name, "_rx_copy_thresh") == 0) {
		value = igb->rx_copy_thresh;
	} else if (strcmp(pr_name, "_rx_limit_per_intr") == 0) {
		value = igb->rx_limit_per_intr;
	} else if (strcmp(pr_name, "_intr_throttling") == 0) {
		value = igb->intr_throttling[0];
	} else {
		return (ENOTSUP);
	}

	(void) snprintf(pr_val, pr_valsize, "%d", value);
	return (0);
}

static void
igb_priv_prop_info(igb_t *igb, const char *pr_name, mac_prop_info_handle_t prh)
{
	char valstr[64];
	int value;

	if (strcmp(pr_name, "_adv_pause_cap") == 0 ||
	    strcmp(pr_name, "_adv_asym_pause_cap") == 0) {
		mac_prop_info_set_perm(prh, MAC_PROP_PERM_READ);
		return;
	} else if (strcmp(pr_name, "_tx_copy_thresh") == 0) {
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
	} else 	if (strcmp(pr_name, "_intr_throttling") == 0) {
		value = igb->capab->def_intr_throttle;
	} else {
		return;
	}

	(void) snprintf(valstr, sizeof (valstr), "%d", value);
	mac_prop_info_set_default_str(prh, valstr);
}

static enum ioc_reply
igb_iov_ioctl(igb_t *igb, struct iocblk *iocp, mblk_t *mp)
{
	int mode, rval;
	iov_param_ver_info_t *verp;
	iov_param_validate_t *valp;
	pci_param_t param;
	size_t size, len, left;
	char reason[MAX_REASON_LEN + 1];
	mblk_t *bp;
	char *buf;

	bzero(reason, MAX_REASON_LEN + 1);

	if (mp->b_cont == NULL)
		return (IOC_INVAL);

	switch (iocp->ioc_cmd) {
	default:
		return (IOC_INVAL);

	case IOV_GET_PARAM_VER_INFO:
		size = sizeof (iov_param_ver_info_t);
		if (iocp->ioc_count < size)
			return (IOC_INVAL);

		verp = (iov_param_ver_info_t *)(uintptr_t)mp->b_cont->b_rptr;
		verp->version = IOV_PARAM_DESC_VERSION;
		verp->num_params = IGB_NUM_IOV_PARAMS;

		break;

	case IOV_GET_PARAM_INFO:
		size = sizeof (igb_iov_param_list);
		if (iocp->ioc_count < size)
			return (IOC_INVAL);

		buf = (char *)igb_iov_param_list;
		left = size;
		for (bp = mp->b_cont; bp != NULL; bp = bp->b_cont) {
			if ((len = MBLKL(bp)) == 0)
				continue;

			if (len > left)
				len = left;

			bcopy(buf, bp->b_rptr, len);

			buf += len;
			left -= len;

			if (left == 0)
				break;
		}

		break;

	case IOV_VALIDATE_PARAM:
		size = sizeof (iov_param_validate_t);
		if (iocp->ioc_count < size)
			return (IOC_INVAL);

		(void) strncpy(reason,
		    "Failed to read params\n", MAX_REASON_LEN);

		valp = (iov_param_validate_t *)(uintptr_t)mp->b_cont->b_rptr;
		mode = FKIOCTL | iocp->ioc_flag;

		rval = pci_param_get_ioctl(igb->dip,
		    (intptr_t)valp, mode, &param);

		if (rval == DDI_SUCCESS) {
			rval = igb_validate_iov_params(param, reason);
		}

		(void) pci_param_free(param);

		if (rval == DDI_SUCCESS) {
			/* validation success; no data to return */
			size = 0;
		} else {
			IGB_DEBUGLOG_1(igb, "%s", reason);
			/* validation failed; return reason */
			size = sizeof (reason);
			bcopy(reason, valp->pv_reason, size);
			iocp->ioc_count = size;
			iocp->ioc_error = EINVAL;
			return (IOC_ACK);
		}

		break;
	}

	iocp->ioc_count = size;
	iocp->ioc_error = 0;

	return (IOC_REPLY);
}

static int
igb_validate_iov_params(pci_param_t param, char *reason)
{
	pci_plist_t plist;
	int rval, i;
	uint16_t num_vf;
	uint32_t unicast_slots;
	uint32_t total_slots = 0;

	rval = pci_plist_get(param, &plist);
	if (rval != DDI_SUCCESS) {
		(void) strncpy(reason, "No params for PF\n", MAX_REASON_LEN);
		return (DDI_FAILURE);
	}

	rval = pci_plist_lookup_uint16(plist, NUM_VF_NVLIST_NAME, &num_vf);
	if (rval != DDI_SUCCESS) {
		(void) strncpy(reason,
		    "Failed to read vf number\n", MAX_REASON_LEN);
		return (DDI_FAILURE);
	}

	if (num_vf > IGB_MAX_CONFIG_VF) {
		(void) snprintf(reason, MAX_REASON_LEN,
		    "Greater than %d VFs is not supported\n",
		    IGB_MAX_CONFIG_VF);
		return (DDI_FAILURE);
	}

	rval = igb_get_param_unicast_slots(plist, &unicast_slots, reason);
	if (rval != DDI_SUCCESS) {
		return (DDI_FAILURE);
	}

	if (unicast_slots != 0)
		total_slots += unicast_slots;
	else
		total_slots++;

	for (i = 0; i < num_vf; i++) {
		rval = pci_plist_getvf(param, i, &plist);
		if (rval != DDI_SUCCESS) {
			(void) snprintf(reason, MAX_REASON_LEN,
			    "No params for VF%d\n", i);
			return (DDI_FAILURE);
		}

		rval = igb_get_param_unicast_slots(plist,
		    &unicast_slots, reason);
		if (rval != DDI_SUCCESS) {
			return (DDI_FAILURE);
		}

		if (unicast_slots != 0)
			total_slots += unicast_slots;
		else
			total_slots++;
	}

	if (total_slots > igb_iov_param_unicast_slots.pd_max64) {
		(void) snprintf(reason, MAX_REASON_LEN,
		    "Greater than %d unicast-slots not supported\n",
		    (uint32_t)igb_iov_param_unicast_slots.pd_max64);
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

static int
igb_add_vf_vlan(igb_t *igb, uint16_t vlan_id, uint32_t vf_index)
{
	vf_data_t *vf_data = &igb->vf[vf_index];
	int i, ret;

	ASSERT(mutex_owned(&igb->gen_lock));

	/* Search all configured VLAN IDs */
	for (i = 0; i < vf_data->num_vlans; i++) {
		if (vf_data->vlan_ids[i] == vlan_id) {
			IGB_DEBUGLOG_3(igb,
			    "Add VLAN %d existed at slot %d for VF%d",
			    vlan_id, i, vf_index);
			/* VLAN ID existed, just return success */
			return (0);
		}
	}

	if (vf_data->num_vlans >= E1000_VLVF_ARRAY_SIZE) {
		IGB_DEBUGLOG_2(igb,
		    "No space to set VLAN %d for VF%d", vlan_id, vf_index);
		return (ENOSPC);
	}

	if ((ret = igb_vlvf_set(igb, vlan_id, B_TRUE, vf_index)) == 0) {
		vf_data->vlan_ids[vf_data->num_vlans] = vlan_id;
		vf_data->num_vlans++;
	}

	return (ret);
}

static int
igb_rem_vf_vlan(igb_t *igb, uint16_t vlan_id, uint32_t vf_index)
{
	vf_data_t *vf_data = &igb->vf[vf_index];
	int i, ret;

	ASSERT(mutex_owned(&igb->gen_lock));

	/* Search all configured VLAN IDs */
	for (i = 0; i < vf_data->num_vlans; i++) {
		if (vf_data->vlan_ids[i] == vlan_id) {
			/* VLAN ID found */
			break;
		}
	}

	if (i == vf_data->num_vlans) {
		IGB_DEBUGLOG_2(igb,
		    "VLAN %d not found for VF%d", vlan_id, vf_index);
		/* Address not found, just return success */
		return (0);
	}

	if ((ret = igb_vlvf_set(igb, vlan_id, B_FALSE, vf_index)) == 0) {
		for (i++; i < vf_data->num_vlans; i++) {
			vf_data->vlan_ids[i - 1] = vf_data->vlan_ids[i];
		}
		vf_data->num_vlans--;
		vf_data->vlan_ids[vf_data->num_vlans] = 0;
	}

	return (ret);
}

int
igb_enable_vf_port_vlan(igb_t *igb, uint16_t vlan_id, uint32_t vf_index)
{
	struct e1000_hw *hw = &igb->hw;
	vf_data_t *vf_data = &igb->vf[vf_index];
	uint32_t vmolr, vmvir;
	int ret;

	ASSERT(mutex_owned(&igb->gen_lock));

	/* Enable VLAN filter */
	if ((ret = igb_vlvf_set(igb, vlan_id, B_TRUE, vf_index)) != 0)
		return (ret);

	/* Indicate the VF to enable transparent VLAN */
	if (igb_transparent_vlan_vf(igb, vf_index, B_TRUE) != IGB_SUCCESS) {
		igb_log(igb,
		    "Failed to enable transparent vlan on VF%d, "
		    "the setting will take effect when the VF is re-attached",
		    vf_index);
	}

	vf_data->port_vlan_id = vlan_id;

	/* Enable VLAN strip */
	vmolr = E1000_READ_REG(hw, E1000_VMOLR(vf_index));
	vmolr |= E1000_VMOLR_STRVLAN;
	E1000_WRITE_REG(hw, E1000_VMOLR(vf_index), vmolr);

	/* Enable VLAN insert - always insert Default VLAN */
	vmvir = vlan_id;
	vmvir |= E1000_VMVIR_VLANA_DEFAULT;
	E1000_WRITE_REG(hw, E1000_VMVIR(vf_index), vmvir);

	E1000_WRITE_FLUSH(hw);

	return (0);
}

static int
igb_disable_vf_port_vlan(igb_t *igb, uint16_t vlan_id, uint32_t vf_index)
{
	struct e1000_hw *hw = &igb->hw;
	vf_data_t *vf_data = &igb->vf[vf_index];
	uint32_t vmolr, vmvir;
	int ret;

	ASSERT(mutex_owned(&igb->gen_lock));

	/* Disable VLAN filter */
	if ((ret = igb_vlvf_set(igb, vlan_id, B_FALSE, vf_index)) != 0)
		return (ret);

	/* Indicate the VF to disable transparent VLAN */
	if (igb_transparent_vlan_vf(igb, vf_index, B_FALSE) != IGB_SUCCESS) {
		igb_log(igb,
		    "Failed to disable transparent vlan on VF%d, "
		    "the setting will take effect when the VF is re-attached",
		    vf_index);
	}

	vf_data->port_vlan_id = 0;

	/* Disable VLAN strip */
	vmolr = E1000_READ_REG(hw, E1000_VMOLR(vf_index));
	vmolr &= ~E1000_VMOLR_STRVLAN;
	E1000_WRITE_REG(hw, E1000_VMOLR(vf_index), vmolr);

	/* Disable VLAN insert */
	vmvir = 0;
	E1000_WRITE_REG(hw, E1000_VMVIR(vf_index), vmvir);

	E1000_WRITE_FLUSH(hw);

	return (0);
}
