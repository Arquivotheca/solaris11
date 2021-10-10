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

#include "igb_sw.h"

/*
 * hold all the special functions to work as a PF driver.
 * This file may or may not survive.
 */

/*
 * have at least "nsec" seconds elapsed since the "before" timestamp
 */
#define	seconds_later(before, nsec)	\
	((ddi_get_time() - before) >= nsec ? 1 : 0)

static void igb_pf_enable_rx(igb_t *);
static void igb_pf_enable_tx(igb_t *);
static void igb_rcv_ack_from_vf(igb_t *, uint16_t);
static void igb_vf_reset(igb_t *, uint16_t);
static void igb_vf_reset_msg(igb_t *, uint16_t);
static void igb_clear_vf_unicst(igb_t *, uint16_t);
static void igb_set_vf_rlpml(igb_t *, int, uint16_t);

static int igb_vf_negotiate(igb_t *, uint32_t *, uint16_t);
static int igb_enable_vf_mac_addr(igb_t *, uint32_t *, uint16_t);
static int igb_disable_vf_mac_addr(igb_t *, uint32_t *, uint16_t);
static int igb_get_vf_mac_addrs(igb_t *, uint32_t *, uint32_t *, uint16_t);
static void igb_get_vf_queues(igb_t *, uint32_t *, uint16_t);
static void igb_set_vf_mcast_promisc(igb_t *, uint32_t *, uint16_t);
static void igb_set_vf_multicasts(igb_t *, uint32_t *, uint16_t);
static void igb_get_vf_mtu(igb_t *, uint32_t *, uint16_t);
static int igb_set_vf_mtu(igb_t *, uint32_t, uint16_t);

static int igb_poll_for_msg(igb_t *, uint16_t);
static int igb_poll_for_ack(igb_t *, uint16_t);
static int igb_read_posted_mbx(igb_t *, uint32_t *, uint16_t, uint16_t);
static int igb_write_posted_mbx(igb_t *, uint32_t *, uint16_t, uint16_t);
static int igb_read_mbx_pf(igb_t *, uint32_t *, uint16_t, uint16_t);
static int igb_write_mbx_pf(igb_t *, uint32_t *, uint16_t, uint16_t);
static int igb_obtain_mbx_lock_pf(igb_t *, uint16_t);

/*
 * Functions which implement message dispatch for an API version
 */
static void igb_rcv_msg_vf_api20(igb_t *, uint16_t);

/*
 * Array of function pointers, each one providing support for one version of
 * the mailbox API.  Subscript needs to match the e1000_pfvf_api_rev enum.
 *
 * Current driver support:
 *   API 1.0: no support
 *   API 1.1: solaris legacy VF driver
 *   API 2.0: solaris Phase1 VF driver
 */
#define	MAX_API_SUPPORT	3
msg_api_implement_t igb_mbx_api[MAX_API_SUPPORT] = {
	NULL,
	igb_rcv_msg_vf_api20
};

/*
 * Routines that implement the Physical Function driver under SR-IOV using
 * Intel mailbox mechanism.
 */

/*
 * Callback from SR-IOV framework
 */
/* ARGSUSED */
int
igb_vf_config_handler(dev_info_t *dip, ddi_cb_action_t action,
    void *cbarg, void *arg1, void *arg2)
{
	igb_t			*igb;
	pciv_config_vf_t	*vf_config;

	/* identify incoming pointers */
	vf_config = (pciv_config_vf_t *)cbarg;
	igb = (igb_t *)arg1;

	/* only one action is valid */
	if (action != DDI_CB_PCIV_CONFIG_VF) {
		igb_error(igb,
		    "igb_vf_config_handler: Invalid action 0x%x", action);
		return (DDI_FAILURE);
	}

	/*
	 * Do necessary action based on cmd.
	 */
	switch (vf_config->cmd) {
	case PCIV_EVT_VFENABLE_PRE:
		return (PCIV_REQREATTACH);

	case PCIV_EVT_VFENABLE_POST:
		break;

	case PCIV_EVT_VFDISABLE_PRE:
		break;

	case PCIV_EVT_VFDISABLE_POST:
		break;

	default:
		break;
	}

	return (DDI_SUCCESS);
}

void
igb_init_vf_settings(igb_t *igb)
{
	vf_data_t *vf_data;
	int i;

	for (i = 0; i < igb->num_vfs; i++) {
		vf_data = &igb->vf[i];

		vf_data->max_mtu = MAX_MTU;
		vf_data->vf_api = igb_mbx_api[e1000_mbox_api_20];
	}
}

static void
igb_pf_enable_rx(igb_t *igb)
{
	struct e1000_hw *hw = &igb->hw;
	uint32_t rctl, rxcsum, max_frame;

	/*
	 * Setup the Receive Control Register (RCTL)
	 */
	rctl = E1000_READ_REG(hw, E1000_RCTL);

	/*
	 * Clear the field used for wakeup control.  This driver doesn't do
	 * wakeup but leave this here for completeness.
	 */
	rctl &= ~(3 << E1000_RCTL_MO_SHIFT);
	rctl &= ~(E1000_RCTL_LBM_TCVR | E1000_RCTL_LBM_MAC);

	rctl |= (E1000_RCTL_EN |	/* Enable Receive Unit */
	    E1000_RCTL_BAM |		/* Accept Broadcast Packets */
	    E1000_RCTL_LPE |		/* Large Packet Enable */
					/* Multicast filter offset */
	    (hw->mac.mc_filter_type << E1000_RCTL_MO_SHIFT) |
	    E1000_RCTL_RDMTS_HALF |	/* rx descriptor threshold */
	    E1000_RCTL_VFE |		/* Vlan Filter Enable */
	    E1000_RCTL_SECRC);		/* Strip Ethernet CRC */

	/*
	 * Setup the Rx Long Packet Max Length register.
	 * If this is an 82576 in VMDQ or SR-IOV mode, set it to the maximum
	 * and let the per-group VMOLR(i).RLPML control each ring-group's
	 * maximum frame.
	 */
	max_frame = igb->max_frame_size;
	if (hw->mac.type == e1000_82576) {
		max_frame = MAX_MTU +
		    sizeof (struct ether_vlan_header) + ETHERFCSL;
	}
	E1000_WRITE_REG(hw, E1000_RLPML, max_frame);

	/*
	 * Hardware checksum settings
	 */
	if (igb->rx_hcksum_enable) {
		rxcsum =
		    E1000_RXCSUM_TUOFL |	/* TCP/UDP checksum */
		    E1000_RXCSUM_IPOFL;		/* IP checksum */

		E1000_WRITE_REG(hw, E1000_RXCSUM, rxcsum);
	}

	/*
	 * SR-IOV PF driver operations must enable queue drop for all
	 * VF and PF queues to prevent head-of-line blocking if an
	 * untrusted VF does not provide descriptors to adapter.
	 */
	E1000_WRITE_REG(hw, E1000_QDE, ALL_QUEUES);

	/*
	 * Setup RSS and VMDQ classifiers for multiple receive queues
	 */
	igb->capab->set_rx_classify(igb);

	/*
	 * Enable the receive unit
	 */
	E1000_WRITE_REG(hw, E1000_RCTL, rctl);
}

static void
igb_pf_enable_tx(igb_t *igb)
{
	struct e1000_hw *hw = &igb->hw;
	uint32_t reg_val;

	/*
	 * Setup the Transmit Control Register (TCTL)
	 */
	reg_val = E1000_READ_REG(hw, E1000_TCTL);
	reg_val &= ~E1000_TCTL_CT;
	reg_val |= E1000_TCTL_PSP | E1000_TCTL_RTLC |
	    (E1000_COLLISION_THRESHOLD << E1000_CT_SHIFT);

	/* Enable transmits */
	reg_val |= E1000_TCTL_EN;

	E1000_WRITE_REG(hw, E1000_TCTL, reg_val);
}

/*
 * Enable the virtual functions
 */
void
igb_enable_vf(igb_t *igb)
{
	struct e1000_hw *hw = &igb->hw;
	uint32_t ctrl_ext;

	ASSERT(mutex_owned(&igb->gen_lock));

	/* Enable mailbox interrupt, disable rx/tx interrupts */
	igb_enable_mailbox_interrupt(igb);

	/* allow VFs to use their mailbox */
	E1000_WRITE_REG(hw, E1000_MBVFIMR, 0xff);

	/* notify VFs that reset has been completed */
	ctrl_ext = E1000_READ_REG(hw, E1000_CTRL_EXT);
	ctrl_ext |= E1000_CTRL_EXT_PFRSTD;
	E1000_WRITE_REG(hw, E1000_CTRL_EXT, ctrl_ext);
	E1000_WRITE_FLUSH(hw);

	/*
	 * In SRIOV mode, the receive unit and transmit unit
	 * need to be enabled before each VF setup rx and tx.
	 */
	igb_pf_enable_rx(igb);
	igb_pf_enable_tx(igb);
}

/*
 * Put all VFs into a holding state
 */
void
igb_hold_vfs(igb_t *igb)
{
	struct e1000_hw *hw = &igb->hw;
	uint32_t msg, enable;
	int ret;
	int i;

	ASSERT(mutex_owned(&igb->gen_lock));

	for (i = 0; i < igb->num_vfs; i++) {
		/* clear any previous data */
		igb->vf[i].flags = 0;

		/* send control message */
		msg = E1000_PF_CONTROL_MSG | E1000_VT_MSGTYPE_CTS;
		ret = igb_write_mbx_pf(igb, &msg, 1, i);
		if (ret) {
			igb_log(igb,
			    "Failed to send PF control message to VF%d", i);
		}
	}

	/* disable all VF transmits and receives; leave PF enabled */
	enable = (1 << igb->pf_grp);
	E1000_WRITE_REG(hw, E1000_VFRE, enable);
	E1000_WRITE_REG(hw, E1000_VFTE, enable);
}

/*
 * Notify the virtual functions
 */
void
igb_notify_vfs(igb_t *igb)
{
	uint32_t msg;
	int ret;
	int i;

	ASSERT(mutex_owned(&igb->gen_lock));

	for (i = 0; i < igb->num_vfs; i++) {
		msg = E1000_PF_CONTROL_MSG;
		if (igb->vf[i].flags & IGB_VF_FLAG_CTS)
			msg |= E1000_VT_MSGTYPE_CTS;
		ret = igb_write_mbx_pf(igb, &msg, 1, i);
		if (ret) {
			igb_log(igb,
			    "Failed to send PF control message to VF%d", i);
		}
	}
}

int
igb_transparent_vlan_vf(igb_t *igb, uint16_t vf, boolean_t enable)
{
	uint32_t msg[2];
	int ret;

	ASSERT(mutex_owned(&igb->gen_lock));

	igb->mbx_hold = B_TRUE;

	msg[0] = E1000_PF_TRANSPARENT_VLAN;
	msg[1] = enable ? 1 : 0;

	if (igb->vf[vf].flags & IGB_VF_FLAG_CTS)
		msg[0] |= E1000_VT_MSGTYPE_CTS;

	/* send message */
	ret = igb_write_posted_mbx(igb, msg, 2, vf);
	if (ret) {
		ret = IGB_FAILURE;
		goto transparent_vlan_end;
	}

	/* read response */
	ret = igb_read_posted_mbx(igb, msg, 1, vf);
	if (ret) {
		ret = IGB_FAILURE;
		goto transparent_vlan_end;
	}

	/* require ACK of the message */
	if (!(msg[0] & E1000_VT_MSGTYPE_ACK)) {
		ret = IGB_FAILURE;
		goto transparent_vlan_end;
	}

	ret = IGB_SUCCESS;

transparent_vlan_end:
	igb->mbx_hold = B_FALSE;
	cv_broadcast(&igb->mbx_hold_cv);

	return (ret);
}

/*
 * Check for and dispatch any mailbox message from any Virtual Function
 */
void
igb_msg_task(void *arg)
{
	igb_t *igb = (igb_t *)arg;
	struct e1000_hw *hw = &igb->hw;
	uint16_t vf;
	clock_t time_stop;
	clock_t time_left;

	mutex_enter(&igb->gen_lock);

	if (IGB_IS_SUSPENDED_INTR(igb)) {
		mutex_exit(&igb->gen_lock);
		return;
	}

	/* Wait for up to 1 second */
	time_stop = ddi_get_lbolt() + SEC_TO_TICK(1);
	while (igb->mbx_hold) {
		time_left = cv_timedwait(&igb->mbx_hold_cv,
		    &igb->gen_lock, time_stop);
		if (time_left == -1)
			break;
	}

	if (igb->mbx_hold) {
		mutex_exit(&igb->gen_lock);
		igb_log(igb,
		    "Time out waiting other mailbox transaction to complete");
		return;
	}

	for (vf = 0; vf < igb->num_vfs; vf++) {
		/* process any reset requests */
		if (!e1000_check_for_rst(hw, vf))
			igb_vf_reset(igb, vf);

		/* process any messages pending */
		if (!e1000_check_for_msg(hw, vf)) {
			ASSERT(igb->vf[vf].vf_api != NULL);
			igb->vf[vf].vf_api(igb, vf);
		}

		/* process any acks */
		if (!e1000_check_for_ack(hw, vf))
			igb_rcv_ack_from_vf(igb, vf);
	}

	mutex_exit(&igb->gen_lock);

	if (igb_check_acc_handle(igb->osdep.reg_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(igb->dip, DDI_SERVICE_DEGRADED);
	}
}

static void
igb_rcv_ack_from_vf(igb_t *igb, uint16_t vf)
{
	vf_data_t *vf_data = &igb->vf[vf];
	uint32_t msg = E1000_VT_MSGTYPE_NACK;
	int ret;

	ASSERT(mutex_owned(&igb->gen_lock));

	/* if device isn't clear to send it shouldn't be reading either */
	if (!(vf_data->flags & IGB_VF_FLAG_CTS) &&
	    seconds_later(vf_data->last_nack, 2)) {
		ret = igb_write_mbx_pf(igb, &msg, 1, vf);
		if (ret) {
			igb_log(igb, "Failed to reply NACK to VF%d", vf);
		}
		vf_data->last_nack = ddi_get_time();
	}
}

/*
 * Receive a message from a virtual function, message API 2.0
 * 1. read message from mailbox
 * 2. dispatch to a handler routine to perform the action
 * 3. write an ack message back to virtual function
 */
static void
igb_rcv_msg_vf_api20(igb_t *igb, uint16_t vf)
{
	uint32_t msgbuf[E1000_VFMAILBOX_SIZE];
	vf_data_t *vf_data = &igb->vf[vf];
	int32_t retval;
	uint32_t msgsiz;

	ASSERT(mutex_owned(&igb->gen_lock));

	/* read message from mailbox */
	retval = igb_read_mbx_pf(igb, msgbuf,
	    E1000_VFMAILBOX_SIZE, vf);
	if (retval) {
		igb_log(igb, "Error receiving message from VF%d", vf);
		return;
	}

	/* this is a message we already processed, do nothing */
	if (msgbuf[0] & (E1000_VT_MSGTYPE_ACK | E1000_VT_MSGTYPE_NACK))
		return;

	/*
	 * Handle the reset message.
	 */
	if (msgbuf[0] == E1000_VF_RESET) {
		igb_vf_reset_msg(igb, vf);
		return;
	}

	/*
	 * Require VF to complete a reset before anything else.
	 */
	if (!(vf_data->flags & IGB_VF_FLAG_CTS)) {
		msgbuf[0] = E1000_VT_MSGTYPE_NACK;
		if (seconds_later(vf_data->last_nack, 2)) {
			retval = igb_write_mbx_pf(igb, msgbuf, 1, vf);
			if (retval) {
				igb_log(igb,
				    "Failed to reply NACK to VF%d", vf);
			}
			vf_data->last_nack = ddi_get_time();
		}
		return;
	}

	/*
	 * Lower 16 bits of the first 4 bytes indicate message type.
	 * Some message types use the upper 16 bits; otherwise,
	 * message starts in the second 4-byte dword.
	 */
	retval = IGB_SUCCESS;
	msgsiz = 1;

	switch ((msgbuf[0] & 0xFFFF)) {
	case E1000_VF_API_NEGOTIATE:
		retval = igb_vf_negotiate(igb, msgbuf, vf);
		msgsiz = 2;
		break;
	case E1000_VF_GET_QUEUES:
		igb_get_vf_queues(igb, msgbuf, vf);
		msgsiz = 4;
		break;
	case E1000_VF_ENABLE_MACADDR:
		retval = igb_enable_vf_mac_addr(igb, msgbuf, vf);
		break;
	case E1000_VF_DISABLE_MACADDR:
		retval = igb_disable_vf_mac_addr(igb, msgbuf, vf);
		break;
	case E1000_VF_GET_MACADDRS:
		retval = igb_get_vf_mac_addrs(igb, msgbuf, &msgsiz, vf);
		break;
	case E1000_VF_SET_MULTICAST:
		igb_set_vf_multicasts(igb, msgbuf, vf);
		break;
	case E1000_VF_SET_MCAST_PROMISC:
		igb_set_vf_mcast_promisc(igb, msgbuf, vf);
		break;
	case E1000_VF_GET_MTU:
		igb_get_vf_mtu(igb, msgbuf, vf);
		msgsiz = 3;
		break;
	case E1000_VF_SET_MTU:
		retval = igb_set_vf_mtu(igb, msgbuf[1], vf);
		break;
	default:
		igb_log(igb, "Unhandled Msg %08x from VF%d", msgbuf[0], vf);
		retval = -E1000_ERR_MBX;
		break;
	}

	/* notify the VF of the results of what it sent us */
	if (retval)
		msgbuf[0] |= E1000_VT_MSGTYPE_NACK;
	else
		msgbuf[0] |= E1000_VT_MSGTYPE_ACK;

	msgbuf[0] |= E1000_VT_MSGTYPE_CTS;

	retval = igb_write_mbx_pf(igb, msgbuf, msgsiz, vf);
	if (retval) {
		igb_log(igb, "Failed to reply message to VF%d", vf);
	}
}

/* Reset worker routines */

/*
 * Do the actions associated with a Function Level Reset (FLR) of the given
 * virtual function
 */
static void
igb_vf_reset(igb_t *igb, uint16_t vf)
{
	ASSERT(mutex_owned(&igb->gen_lock));

	/* clear all flags */
	igb->vf[vf].flags = 0;
	igb->vf[vf].last_nack = ddi_get_time();

	/* reset offloads to defaults */
	igb_set_vmolr(igb, vf);

	/* reset multicast table array for vf */
	igb->vf[vf].num_mc_hashes = 0;

	/* flush and reset the MTA with new values */
	igb_setup_multicst(igb, vf);

	/* clear unicast addresses for vf */
	igb_clear_vf_unicst(igb, vf);
}

static void
igb_vf_reset_msg(igb_t *igb, uint16_t vf)
{
	struct e1000_hw *hw = &igb->hw;
	uint8_t *vf_mac = NULL;
	uint32_t reg, msgbuf[3];
	u8 *addr = (u8 *)(&msgbuf[1]);
	int ret;
	int i;

	ASSERT(mutex_owned(&igb->gen_lock));

	/* find the first mac address for the VF */
	for (i = 0; i < igb->unicst_total; i++) {
		if ((igb->unicst_addr[i].vmdq_group == vf) &&
		    (igb->unicst_addr[i].flags & IGB_ADDRESS_SET)) {
			vf_mac = igb->unicst_addr[i].addr;
			break;
		}
	}

	if (vf_mac == NULL) {
		igb_error(igb, "No primary MAC address assigned to VF%d", vf);
		/* reply to reset with nack */
		msgbuf[0] = E1000_VF_RESET | E1000_VT_MSGTYPE_NACK;
		ret = igb_write_mbx_pf(igb, msgbuf, 1, vf);
		if (ret) {
			igb_log(igb, "Failed to reply message to VF%d", vf);
		}
		return;
	}

	/* process all the same items cleared in a function level reset */
	igb_vf_reset(igb, vf);

	/* enable transmit and receive for vf */
	reg = E1000_READ_REG(hw, E1000_VFTE);
	E1000_WRITE_REG(hw, E1000_VFTE, reg | (1 << vf));
	reg = E1000_READ_REG(hw, E1000_VFRE);
	E1000_WRITE_REG(hw, E1000_VFRE, reg | (1 << vf));

	igb->vf[vf].flags = IGB_VF_FLAG_CTS;

	/* reply to reset with ack and vf mac address */
	msgbuf[0] = E1000_VF_RESET | E1000_VT_MSGTYPE_ACK;
	(void) memcpy(addr, vf_mac, ETHERADDRL);
	ret = igb_write_mbx_pf(igb, msgbuf, 3, vf);
	if (ret) {
		igb_log(igb, "Failed to reply message to VF%d", vf);
	}
}

/* MAC address worker routines */

/*
 * igb_enable_vf_mac_addr - VF requests to enable a unicast MAC address.
 * This is used by API 1.1 and API 2.0
 */
static int
igb_enable_vf_mac_addr(igb_t *igb, uint32_t *msg, uint16_t vf)
{
	uint8_t *mac_addr = (uint8_t *)&msg[1];
	int slot;

	ASSERT(mutex_owned(&igb->gen_lock));

	/* is it a valid address? */
	if (!is_valid_mac_addr(mac_addr)) {
		igb_error(igb, "MAC address not valid: %s",
		    ether_sprintf((struct ether_addr *)mac_addr));
		return (IGB_FAILURE);
	}

	/* is it a configured/allowed address? */
	slot = igb_unicst_find(igb, mac_addr, vf);
	if (slot < 0) {
		igb_error(igb, "MAC address not configured for VF%d", vf);
		return (IGB_FAILURE);
	}

	/*
	 * Set VMDQ according to current mode and write to adapter
	 * receive address registers
	 */
	igb_rar_set_vmdq(igb, mac_addr, slot, vf);

	/* Enable the MAC address in software state */
	igb->unicst_addr[slot].flags |= IGB_ADDRESS_ENABLED;

	return (IGB_SUCCESS);
}

/*
 * igb_disable_vf_mac_addr - VF requests to disable a unicast MAC address.
 * This is used by API 1.1 and API 2.0
 */
static int
igb_disable_vf_mac_addr(igb_t *igb, uint32_t *msg, uint16_t vf)
{
	uint8_t *mac_addr = (uint8_t *)&msg[1];
	int slot;

	ASSERT(mutex_owned(&igb->gen_lock));

	/* is it a configured/allowed address? */
	slot = igb_unicst_find(igb, mac_addr, vf);
	if (slot < 0) {
		igb_error(igb, "MAC address not configured for VF%d", vf);
		return (IGB_FAILURE);
	}

	/* Clear the MAC address from adapter registers */
	igb_rar_clear(igb, slot);

	/* Disable the MAC address from software state */
	igb->unicst_addr[slot].flags &= ~IGB_ADDRESS_ENABLED;

	return (IGB_SUCCESS);
}

/*
 * Clear the unicast addresses used by given Virtual Function.
 * This is used by API 1.1 and 2.0
 */
static void
igb_clear_vf_unicst(igb_t *igb, uint16_t vf)
{
	int slot;

	ASSERT(mutex_owned(&igb->gen_lock));

	for (slot = 0; slot < igb->unicst_total; slot++) {
		if ((igb->unicst_addr[slot].vmdq_group == vf) &&
		    (igb->unicst_addr[slot].flags & IGB_ADDRESS_ENABLED)) {
			/* Clear the MAC address from adapter registers */
			igb_rar_clear(igb, slot);

			/* Disable the MAC address from software state */
			igb->unicst_addr[slot].flags &= ~IGB_ADDRESS_ENABLED;
		}
	}
}

/*
 * igb_get_vf_mac_addrs - return list of configured MAC addresses to the VF
 *
 * Default MAC address is returned in the Reset message, so is not included in
 * this message.  VF requests a starting offset, which will usually be 0,
 * meaning start of the list.  A maximum of 8 addresses can be returned in a
 * message, so if there are more than 8 addresses in the list, VF must send a
 * second (third, etc) request with non-zero offset.
 *
 * The mac_addr_chg flag is used to detect if MAC address list has changed
 * during this message sequence.  Flag is set whenever the address list is
 * changed.  Flag is cleared whenever a 0-offset get-macaddrs message is sent.
 *
 * If PF sees the flag set when a non-0-offset request comes in, it means the
 * list has changed since the VF requested the first part of list.  In this
 * case, PF will NACK the message and VF must restart the sequence from the
 * beginning.
 *
 * Because the size of address list is variable, length of the response message
 * is variable and is returned in *msgsiz.
 */
static int
igb_get_vf_mac_addrs(igb_t *igb, uint32_t *msg, uint32_t *msgsiz, uint16_t vf)
{
	vf_data_t *vf_data = &igb->vf[vf];
	int off = msg[1];	/* requested list offset */
	uint32_t total;		/* total number of addresses */
	uint32_t cnt;		/* count of addresses returned */
	uint8_t *pnt;		/* point to copied address */
	int i;

	ASSERT(mutex_owned(&igb->gen_lock));

	/*
	 * check for address list change in the middle of a sequence
	 */
	/* offset 0 clears change flag */
	if (off == 0) {
		vf_data->mac_addr_chg = 0;

	/* non-0 offset and change flag set; fail the message */
	} else if (vf_data->mac_addr_chg) {
		IGB_DEBUGLOG_0(igb, "MAC addresses changed");
		*msgsiz = 1;
		return (IGB_FAILURE);
	}

	/*
	 * total addresses in list
	 *
	 * The first mac address is used as the default mac address,
	 * which has been returned to the VF through the ACK of the
	 * reset message.
	 *
	 * The remaining mac addresses are sent to the VF as the
	 * multiple alternative mac addresses.
	 */
	if (vf_data->num_mac_addrs <= 1) {
		IGB_DEBUGLOG_0(igb, "No available alternative MAC addresses");
		msg[1] = 0;
		msg[2] = off;
		msg[3] = 0;
		*msgsiz = 4;
		return (IGB_SUCCESS);
	}

	total = vf_data->num_mac_addrs - 1;
	msg[1] = total;

	/* validate starting offset of returned addresses */
	if (off < total) {
		msg[2] = off;
	} else {
		IGB_DEBUGLOG_2(igb, "Invalid offset to get MAC addresses: "
		    "offset = %d, total = %d", off, total);
		msg[2] = off;
		msg[3] = 0;
		*msgsiz = 4;
		return (IGB_SUCCESS);
	}

	/* calculate how many addresses can be put into this message */
	cnt = total - off;
	if (cnt > 8)
		cnt = 8;
	msg[3] = cnt;

	/*
	 * Return size of response message.  Units are 4-byte longwords and
	 * there are 4 longwords of message heading.
	 */
	*msgsiz = 4 + (((cnt * ETHERADDRL) + 3) / 4);

	/* copy cnt addresses into message buffer */
	off++;		/* bypass the first (default) mac address */
	pnt = (uint8_t *)&msg[4];
	for (i = 0; i < igb->unicst_total; i++) {
		if ((igb->unicst_addr[i].vmdq_group == vf) &&
		    (igb->unicst_addr[i].flags & IGB_ADDRESS_SET))
			off--;
		else
			continue;

		if (off >= 0)
			continue;

		bcopy(igb->unicst_addr[i].addr, pnt, ETHERADDRL);
		pnt += ETHERADDRL;

		if (--cnt == 0)
			break;
	}
	ASSERT(cnt == 0);

	return (IGB_SUCCESS);
}

/* Other worker routines */

static void
igb_set_vf_multicasts(igb_t *igb, uint32_t *msgbuf, uint16_t vf)
{
	int n = (msgbuf[0] & E1000_VT_MSGINFO_MASK) >> E1000_VT_MSGINFO_SHIFT;
	uint16_t *hash_list = (uint16_t *)&msgbuf[1];
	vf_data_t *vf_data = &igb->vf[vf];
	int i;

	ASSERT(mutex_owned(&igb->gen_lock));

	/*
	 * keep the number of multicast addresses assigned
	 * to this VF for later use to restore when the PF multicast
	 * list changes
	 */
	vf_data->num_mc_hashes = n;

	/* only up to 30 hash values supported */
	if (n > IGB_MAX_VF_MC_ENTRIES)
		n = IGB_MAX_VF_MC_ENTRIES;

	/* store the hashes for later use */
	for (i = 0; i < n; i++)
		vf_data->mc_hashes[i] = hash_list[i];

	/*
	 * Flush and reset the mta with the new values
	 */
	igb_setup_multicst(igb, vf);
}

static void
igb_set_vf_rlpml(igb_t *igb, int size, uint16_t vf)
{
	struct e1000_hw *hw = &igb->hw;
	uint32_t vmolr;

	ASSERT(mutex_owned(&igb->gen_lock));

	vmolr = E1000_READ_REG(hw, E1000_VMOLR(vf));
	vmolr &= ~E1000_VMOLR_RLPML_MASK;
	vmolr |= size | E1000_VMOLR_LPE;
	E1000_WRITE_REG(hw, E1000_VMOLR(vf), vmolr);
}

/*
 * Set the VM offload register for given virtual function
 */
void
igb_set_vmolr(igb_t *igb, uint16_t vf)
{
	struct e1000_hw *hw = &igb->hw;
	uint32_t vmolr, size;

	ASSERT(mutex_owned(&igb->gen_lock));

	/*
	 * This register exists only on 82576 and newer so if we are older then
	 * we should exit and do nothing
	 */
	if (hw->mac.type < e1000_82576) {
		igb_log(igb, "igb_set_vmolr: invalid mac type");
		return;
	}

	/*
	 * Set likely defaults.  If the long packet size field in bits 13:0
	 * is greater than standard MTU, set the long packet enable bit.
	 */
	vmolr = E1000_READ_REG(hw, E1000_VMOLR(vf));
	size = vmolr & E1000_VMOLR_RLPML_MASK;
	if (size > ETHERMTU)
		vmolr |= E1000_VMOLR_LPE;

	/* accept untagged packets */
	vmolr |= E1000_VMOLR_AUPE;

	/* disable rss */
	vmolr &= ~E1000_VMOLR_RSSE;

	/* Accept broadcast packets */
	vmolr |= E1000_VMOLR_BAM;

	E1000_WRITE_REG(hw, E1000_VMOLR(vf), vmolr);
}

void
igb_clear_vf_vfta(igb_t *igb, uint16_t vf)
{
	struct e1000_hw *hw = &igb->hw;
	uint32_t pool_mask, reg;
	int i;

	ASSERT(mutex_owned(&igb->gen_lock));

	pool_mask = 1 << (E1000_VLVF_POOLSEL_SHIFT + vf);

	/* Find the vlan filter for this id */
	for (i = 0; i < E1000_VLVF_ARRAY_SIZE; i++) {
		reg = E1000_READ_REG(hw, E1000_VLVF(i));

		/* remove the vf from the pool */
		reg &= ~pool_mask;

		/* if pool is empty then disable this VLVF entry */
		if (!(reg & E1000_VLVF_POOLSEL_MASK) &&
		    (reg & E1000_VLVF_VLANID_ENABLE)) {
			/*
			 * The VID should not be removed from the
			 * filter table (VFTA) to allow it on PF.
			 */
			reg = 0;
		}

		E1000_WRITE_REG(hw, E1000_VLVF(i), reg);
	}
}

int
igb_vlvf_set(igb_t *igb, uint16_t vid, boolean_t add, uint16_t vf)
{
	struct e1000_hw *hw = &igb->hw;
	uint32_t pf_bit, vf_bit;
	uint32_t reg, i;
	int ret = 0;

	ASSERT(mutex_owned(&igb->gen_lock));
	ASSERT(vf < igb->pf_grp);

	/* The vlvf table only exists on 82576 hardware and newer */
	if (hw->mac.type < e1000_82576)
		return (ENODEV);

	pf_bit = 1 << (E1000_VLVF_POOLSEL_SHIFT + igb->pf_grp);
	vf_bit = 1 << (E1000_VLVF_POOLSEL_SHIFT + vf);

	/* Find the vlan filter for this id */
	for (i = 0; i < E1000_VLVF_ARRAY_SIZE; i++) {
		reg = E1000_READ_REG(hw, E1000_VLVF(i));
		if ((reg & E1000_VLVF_VLANID_ENABLE) &&
		    (vid == (reg & E1000_VLVF_VLANID_MASK)))
			break;
	}

	if (add) {
		if (i == E1000_VLVF_ARRAY_SIZE) {
			/*
			 * Did not find a matching VLAN ID entry that was
			 * enabled.  Search for a free filter entry, i.e.
			 * one without the enable bit set
			 */
			for (i = 0; i < E1000_VLVF_ARRAY_SIZE; i++) {
				reg = E1000_READ_REG(hw, E1000_VLVF(i));
				if (!(reg & E1000_VLVF_VLANID_ENABLE))
					break;
			}
		}
		if (i < E1000_VLVF_ARRAY_SIZE) {
			/* Found an enabled/available entry */
			/*
			 * The bit for PF is also set to allow the vlan traffic
			 * between VF and PF.
			 */
			reg |= vf_bit | pf_bit;

			/* if not enabled we need to enable it */
			if (!(reg & E1000_VLVF_VLANID_ENABLE)) {
				/*
				 * The VID has been set in the filter table;
				 * No need to set VFTA any more.
				 */
				reg |= E1000_VLVF_VLANID_ENABLE;
			}
			reg &= ~E1000_VLVF_VLANID_MASK;
			reg |= vid;
			E1000_WRITE_REG(hw, E1000_VLVF(i), reg);
		} else {
			ret = ENOSPC;
		}
	} else {
		if (i < E1000_VLVF_ARRAY_SIZE) {
			/* remove vf from the pool */
			reg &= ~vf_bit;

			/* if pool is empty then disable this VLVF entry */
			if ((reg & E1000_VLVF_POOLSEL_MASK) == pf_bit) {
				/*
				 * The VID should not be removed from the
				 * filter table (VFTA) to allow it on PF.
				 */
				reg = 0;
			}
			E1000_WRITE_REG(hw, E1000_VLVF(i), reg);
		}
	}

	return (ret);
}

/*
 * igb_vf_negotiate - Respond to VF request to negotiate API version
 */
static int
igb_vf_negotiate(igb_t *igb, uint32_t *msg, uint16_t vf)
{
	uint32_t api_req = msg[1];
	vf_data_t *vf_data = &igb->vf[vf];
	int ret;

	/*
	 * If request matches a supported version, install the function pointer
	 * and return success.
	 * If requested version is not supported, don't change the default
	 * pointer.  Return failure and a hint of preferred version.
	 */
	switch (api_req) {
	case e1000_mbox_api_20:	/* solaris phase1 VF driver */
		vf_data->vf_api = igb_mbx_api[e1000_mbox_api_20];
		ret = IGB_SUCCESS;
		break;

	case e1000_mbox_api_10:	/* linux/freebsd VF drivers not supported */
	default:		/* any other version: not supported */
		msg[1] = e1000_mbox_api_20;
		ret = IGB_FAILURE;
		break;
	}

	return (ret);
}

/*
 * igb_get_vf_queues - Return the number of tx and rx queues available to
 * the VF, and indicate the VF to enable or disable transparent vlan.
 */
static void
igb_get_vf_queues(igb_t *igb, uint32_t *msg, uint16_t vf)
{
	/* maximum tx queues the vf may use */
	msg[1] = 1;

	/* maximum rx queues the vf may use */
	msg[2] = 1;

	/* enable/disable transparent vlan for the vf */
	msg[3] = igb->vf[vf].port_vlan_id > 0 ? 1 : 0;
}

/*
 * igb_set_vf_mcast_promisc - Enable or disable multicast promiscuous as
 * requested in the message.
 */
static void
igb_set_vf_mcast_promisc(igb_t *igb, uint32_t *msg, uint16_t vf)
{
	uint32_t enable = msg[1];
	vf_data_t *vf_data = &igb->vf[vf];
	struct e1000_hw *hw = &igb->hw;
	uint32_t vmolr;

	ASSERT(mutex_owned(&igb->gen_lock));

	/*
	 * enable multicast promiscuous and set the flag bit
	 */
	vmolr = E1000_READ_REG(hw, E1000_VMOLR(vf));
	if (enable) {
		vmolr |= E1000_VMOLR_MPME;
		vf_data->flags |= IGB_VF_FLAG_MULTI_PROMISC;

	/*
	 * disable multicast promiscuous and clear the flag bit
	 */
	} else {
		vmolr &= ~E1000_VMOLR_MPME;
		vf_data->flags &= ~IGB_VF_FLAG_MULTI_PROMISC;
	}

	E1000_WRITE_REG(hw, E1000_VMOLR(vf), vmolr);
}

/*
 * igb_get_vf_mtu - Return the upper and lower bounds on MTU in the message
 */
static void
igb_get_vf_mtu(igb_t *igb, uint32_t *msg, uint16_t vf)
{
	/* lower bound on MTU */
	msg[1] = MIN_MTU;

	/* upper bound on VF MTU is the PF's current setting */
	msg[2] = igb->vf[vf].max_mtu;
}

/*
 * igb_set_vf_mtu - Set the requested MTU for the given vf.
 */
static int
igb_set_vf_mtu(igb_t *igb, uint32_t mtu, uint16_t vf)
{
	int max_frame;

	ASSERT(mutex_owned(&igb->gen_lock));

	/*
	 * validate requested mtu
	 */
	if ((mtu < MIN_MTU) || (mtu > igb->vf[vf].max_mtu)) {
		IGB_DEBUGLOG_1(igb, "Invalid MTU request: %d", mtu);
		return (IGB_FAILURE);
	}

	/*
	 * set the corresponding size of max frame
	 */
	max_frame = mtu + sizeof (struct ether_vlan_header) + ETHERFCSL;
	igb_set_vf_rlpml(igb, max_frame, vf);

	return (IGB_SUCCESS);
}

/*
 * igb_poll_for_msg - Wait for message notification
 */
static int
igb_poll_for_msg(igb_t *igb, uint16_t mbx_id)
{
	struct e1000_hw *hw = &igb->hw;
	struct e1000_mbx_info *mbx = &hw->mbx;
	clock_t countdown = 100;	/* 100 ticks */
	clock_t time_left;
	clock_t time_stop;

	ASSERT(mutex_owned(&igb->gen_lock));

	if (!mbx->ops.check_for_msg)
		goto out;

	while (countdown && mbx->ops.check_for_msg(hw, mbx_id)) {
		time_stop = ddi_get_lbolt() + 1;

		time_left = cv_timedwait(&igb->mbx_poll_cv,
		    &igb->gen_lock, time_stop);

		if (time_left < 0)
			countdown--;
	}

out:
	return (countdown ? E1000_SUCCESS : -E1000_ERR_MBX);
}

/*
 * igb_poll_for_ack - Wait for message acknowledgement
 */
static int
igb_poll_for_ack(igb_t *igb, uint16_t mbx_id)
{
	struct e1000_hw *hw = &igb->hw;
	struct e1000_mbx_info *mbx = &hw->mbx;
	clock_t countdown = 100;	/* 100 ticks */
	clock_t time_left;
	clock_t time_stop;

	ASSERT(mutex_owned(&igb->gen_lock));

	if (!mbx->ops.check_for_ack)
		goto out;

	while (countdown && mbx->ops.check_for_ack(hw, mbx_id)) {
		time_stop = ddi_get_lbolt() + 1;

		time_left = cv_timedwait(&igb->mbx_poll_cv,
		    &igb->gen_lock, time_stop);

		if (time_left < 0)
			countdown--;
	}

out:
	return (countdown ? E1000_SUCCESS : -E1000_ERR_MBX);
}

/*
 * igb_read_posted_mbx - Wait for message notification and receive message
 */
static int
igb_read_posted_mbx(igb_t *igb, uint32_t *msg, uint16_t size, uint16_t mbx_id)
{
	int ret_val = -E1000_ERR_MBX;

	ASSERT(mutex_owned(&igb->gen_lock));

	ret_val = igb_poll_for_msg(igb, mbx_id);

	/* if ack received read message, otherwise we timed out */
	if (!ret_val)
		ret_val = igb_read_mbx_pf(igb, msg, size, mbx_id);
out:
	return (ret_val);
}

/*
 * igb_write_posted_mbx - Write a message to the mailbox, wait for ack
 */
static int
igb_write_posted_mbx(igb_t *igb, uint32_t *msg, uint16_t size, uint16_t mbx_id)
{
	int ret_val = -E1000_ERR_MBX;

	ASSERT(mutex_owned(&igb->gen_lock));

	/* send msg */
	ret_val = igb_write_mbx_pf(igb, msg, size, mbx_id);

	/* if msg sent wait until we receive an ack */
	if (!ret_val)
		ret_val = igb_poll_for_ack(igb, mbx_id);
out:
	return (ret_val);
}

/*
 * igb_obtain_mbx_lock_pf - obtain mailbox lock
 *
 * return SUCCESS if we obtained the mailbox lock
 */
static int
igb_obtain_mbx_lock_pf(igb_t *igb, uint16_t vf_number)
{
	struct e1000_hw *hw = &igb->hw;
	uint32_t p2v_mailbox;
	uint32_t countdown = 200;	/* 1 msec */
	int ret_val = -E1000_ERR_MBX;

	while (countdown) {
		/* Take ownership of the buffer */
		E1000_WRITE_REG(hw,
		    E1000_P2VMAILBOX(vf_number), E1000_P2VMAILBOX_PFU);

		/* reserve mailbox for vf use */
		p2v_mailbox = E1000_READ_REG(hw, E1000_P2VMAILBOX(vf_number));
		if (p2v_mailbox & E1000_P2VMAILBOX_PFU)
			return (E1000_SUCCESS);

		usec_delay(5);
		countdown--;
	}

	return (ret_val);
}

/*
 * igb_write_mbx_pf - Places a message in the mailbox
 *
 * returns SUCCESS if it successfully copied message into the buffer
 */
static int
igb_write_mbx_pf(igb_t *igb, uint32_t *msg, uint16_t size, uint16_t vf_number)
{
	struct e1000_hw *hw = &igb->hw;
	int ret_val;
	int i;

	/* limit size to the size of mailbox */
	if (size > hw->mbx.size)
		size = hw->mbx.size;

	/* lock the mailbox to prevent pf/vf race condition */
	ret_val = igb_obtain_mbx_lock_pf(igb, vf_number);
	if (ret_val)
		goto out_no_write;

	/* flush msg and acks as we are overwriting the message buffer */
	(void) hw->mbx.ops.check_for_msg(hw, vf_number);
	(void) hw->mbx.ops.check_for_ack(hw, vf_number);

	/* copy the caller specified message to the mailbox memory buffer */
	for (i = 0; i < size; i++)
		E1000_WRITE_REG_ARRAY(hw, E1000_VMBMEM(vf_number), i, msg[i]);

	/* Interrupt VF to tell it a message has been sent and release buffer */
	E1000_WRITE_REG(hw, E1000_P2VMAILBOX(vf_number), E1000_P2VMAILBOX_STS);

	/* update stats */
	hw->mbx.stats.msgs_tx++;

out_no_write:
	return (ret_val);

}

/*
 * igb_read_mbx_pf - Read a message from the mailbox
 *
 * This function copies a message from the mailbox buffer to the caller's
 * memory buffer.  The presumption is that the caller knows that there was
 * a message due to a VF request so no polling for message is needed.
 */
static int
igb_read_mbx_pf(igb_t *igb, uint32_t *msg, uint16_t size, uint16_t vf_number)
{
	struct e1000_hw *hw = &igb->hw;
	int ret_val;
	int i;

	/* limit size to the size of mailbox */
	if (size > hw->mbx.size)
		size = hw->mbx.size;

	/* lock the mailbox to prevent pf/vf race condition */
	ret_val = igb_obtain_mbx_lock_pf(igb, vf_number);
	if (ret_val)
		goto out_no_read;

	/* copy the message to the mailbox memory buffer */
	for (i = 0; i < size; i++)
		msg[i] = E1000_READ_REG_ARRAY(hw, E1000_VMBMEM(vf_number), i);

	/* Acknowledge the message and release buffer */
	E1000_WRITE_REG(hw, E1000_P2VMAILBOX(vf_number), E1000_P2VMAILBOX_ACK);

	/* update stats */
	hw->mbx.stats.msgs_rx++;

out_no_read:
	return (ret_val);
}
