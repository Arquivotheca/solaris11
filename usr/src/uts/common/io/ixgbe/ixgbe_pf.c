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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include "ixgbe_sw.h"

/*
 * hold all the special functions to work as a PF driver.
 * This file may or may not survive.
 */

/*
 * have at least "nsec" seconds elapsed since the "before" timestamp
 */
#define	seconds_later(before, nsec)	\
	((ddi_get_time() - before) >= nsec ? 1 : 0)

extern ddi_device_acc_attr_t ixgbe_regs_acc_attr;

static void ixgbe_vf_reset_event(ixgbe_t *, int);
static void ixgbe_clear_vf_unicst(ixgbe_t *, int);
static void ixgbe_vf_reset(ixgbe_t *, int);
static void ixgbe_clear_vf_vfta(ixgbe_t *, uint32_t);
static void ixgbe_vf_reset_msg(ixgbe_t *, uint32_t);
static void ixgbe_rcv_ack_from_vf(ixgbe_t *, int);
void ixgbe_rcv_msg_vf_api20(ixgbe_t *, int);
static int ixgbe_vf_negotiate(ixgbe_t *, uint32_t *, uint16_t);
static void ixgbe_get_vf_queues(ixgbe_t *, uint32_t *, uint16_t);
static int ixgbe_enable_vf_mac_addr(ixgbe_t *, uint32_t *, uint16_t);
static int ixgbe_disable_vf_mac_addr(ixgbe_t *, uint32_t *, uint16_t);
static int ixgbe_get_vf_mac_addrs(ixgbe_t *, uint32_t *, uint32_t *, uint16_t);
static void ixgbe_set_vf_multicasts(ixgbe_t *, uint32_t *, uint32_t);
static void ixgbe_set_vf_mcast_promisc(ixgbe_t *, uint32_t *, uint16_t);
static void ixgbe_get_vf_mtu(ixgbe_t *, uint32_t *, uint16_t);
static int ixgbe_set_vf_mtu(ixgbe_t *, uint32_t, uint16_t);

/*
 * Array of function pointers, each one providing support for one version of
 * the mailbox API.  Subscript needs to match the ixgbe_pfvf_api_rev enum.
 *
 * Current driver support:
 *   API 1.0: no support
 *   API 2.0: solaris Phase1 VF driver
 */
#define	MAX_API_SUPPORT	3
msg_api_implement_t ixgbe_mbx_api[MAX_API_SUPPORT] = {
	NULL,
	ixgbe_rcv_msg_vf_api20
};

/*
 * Routines that implement the Physical Function driver under SR-IOV using
 * Intel mailbox mechanism.
 */

void
ixgbe_init_function_pointer_pf(ixgbe_t *ixgbe)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	vf_data_t *vf;
	int i;

	for (i = 0; i < ixgbe->num_vfs; i++) {
		vf = &ixgbe->vf[i];

		vf->max_mtu = ETHERMTU;
		vf->vf_api = ixgbe_mbx_api[ixgbe_mbox_api_20];
	}

	hw->mbx.ops.init_params = ixgbe_init_mbx_params_pf;
}

/*
 * Enable the virtual functions
 */
void
ixgbe_enable_vf(ixgbe_t *ixgbe)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	uint32_t ctrl_ext;

	/* Enable mailbox interrupt, disable rx/tx interrupts */
	ixgbe_enable_sriov_interrupt(ixgbe);

	/* allow VFs to use their mailbox */
	IXGBE_WRITE_REG(hw, IXGBE_MBVFIMR(0), 0xFFFFFFFF);
	IXGBE_WRITE_REG(hw, IXGBE_MBVFIMR(1), 0xFFFFFFFF);

	/* notify VFs that reset has been completed */
	ctrl_ext = IXGBE_READ_REG(hw, IXGBE_CTRL_EXT);
	ctrl_ext |= IXGBE_CTRL_EXT_PFRSTD;
	IXGBE_WRITE_REG(hw, IXGBE_CTRL_EXT, ctrl_ext);
	IXGBE_WRITE_FLUSH(hw);
}

/*
 * Check for and dispatch any mailbox message from any Virtual Function
 */
void
ixgbe_msg_task(ixgbe_t *ixgbe)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	uint16_t vf;

	for (vf = 0; vf < ixgbe->num_vfs; vf++) {
		/* process any reset requests */
		if (!ixgbe_check_for_rst(hw, vf)) {
			ixgbe_vf_reset_event(ixgbe, (int)vf);
			IXGBE_DEBUGLOG_0(ixgbe, "ixgbe_vf_reset_event");
		}

		/* process any messages pending */
		if (!ixgbe_check_for_msg(hw, vf))
			ixgbe->vf[vf].vf_api(ixgbe, (int)vf);

		/* process any acks */
		if (!ixgbe_check_for_ack(hw, vf))
			ixgbe_rcv_ack_from_vf(ixgbe, (int)vf);
	}
}

/*
 * Put all VFs into a holding state
 */
void
ixgbe_hold_vfs(ixgbe_t *ixgbe)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	uint32_t msg;
	uint32_t reg_id, bit;
	int i;

	for (i = 0; i < ixgbe->num_vfs; i++) {
		/* clear any previous data */
		ixgbe->vf[i].flags = 0;

		/* send control message */
		msg = IXGBE_PF_CONTROL_MSG | IXGBE_VT_MSGTYPE_CTS;
		if (ixgbe_write_mbx(hw, &msg, 1, i) != IXGBE_SUCCESS)
			ixgbe_log(ixgbe, "Writing to the mailbox fails");
	}

	/* disable all VF transmits and receives; leave PF enabled */
	bit = ixgbe->pf_grp % 32;
	reg_id = ixgbe->pf_grp / 32;
	IXGBE_WRITE_REG(hw, IXGBE_VFRE(0), 0);
	IXGBE_WRITE_REG(hw, IXGBE_VFRE(1), 0);
	IXGBE_WRITE_REG(hw, IXGBE_VFTE(0), 0);
	IXGBE_WRITE_REG(hw, IXGBE_VFTE(1), 0);

	/* Enable only the PF's pool for Tx/Rx */
	IXGBE_WRITE_REG(hw, IXGBE_VFRE(reg_id), (1 << bit));
	IXGBE_WRITE_REG(hw, IXGBE_VFTE(reg_id), (1 << bit));
	IXGBE_WRITE_REG(hw, IXGBE_PFDTXGSWC, IXGBE_PFDTXGSWC_VT_LBEN);
	ixgbe_set_vmolr(ixgbe, ixgbe->pf_grp);
}

/*
 * Handle a function-level reset (FLR) of virtual function
 */
static void
ixgbe_vf_reset_event(ixgbe_t *ixgbe, int vf)
{
	/* do the rest of reset processing */
	ixgbe_vf_reset(ixgbe, vf);
}

void
ixgbe_clear_vf_unicst(ixgbe_t *ixgbe, int vf)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	int slot;

	for (slot = 0; slot < ixgbe->unicst_total; slot++) {
		if ((ixgbe->unicst_addr[slot].group_index == vf) &&
		    (ixgbe->unicst_addr[slot].flags & IXGBE_ADDRESS_ENABLED)) {
			/* Clear the MAC address from adapter registers */
			(void) ixgbe_clear_rar(hw, slot);

			/* Disable the MAC address from software state */
			ixgbe->unicst_addr[slot].flags &=
			    ~IXGBE_ADDRESS_ENABLED;
		}
	}
}

static void
ixgbe_clear_vf_vfta(ixgbe_t *ixgbe, uint32_t vf)
{
	/*
	 * EngineerNote: not sure solaris driver needs to do anything
	 * for this mmessage
	 */
	ixgbe_log(ixgbe, "ixgbe_clear_vf_vfta do nothing vf: %d", vf);
}

int
ixgbe_vlvf_set(ixgbe_t *ixgbe, uint16_t vid, boolean_t add, uint16_t vf)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	uint32_t reg, i;
	int ret = 0;

	ASSERT(mutex_owned(&ixgbe->gen_lock));
	ASSERT(vf < ixgbe->pf_grp);

	/* The PFVLVF table only exists on 82599 hardware and newer */
	if (hw->mac.type < ixgbe_mac_82599EB)
		return (ENODEV);

	/* Find the vlan filter for this id */
	for (i = 0; i < IXGBE_VLVF_ENTRIES; i++) {
		reg = IXGBE_READ_REG(hw, IXGBE_VLVF(i));
		if ((reg & 0x0FFF) == vid)
			break;
	}

	if (add) {
		if (i >= IXGBE_VLVF_ENTRIES) {
			/*
			 * Did not find a matching VLAN ID entry that was
			 * enabled.  Search for a free filter entry, i.e.
			 * one without the enable bit set
			 */
			for (i = 0; i < IXGBE_VLVF_ENTRIES; i++) {
				reg = IXGBE_READ_REG(hw, IXGBE_VLVF(i));
				if (!(reg & IXGBE_VLVF_VIEN))
					break;
			}
		}

		if (i < IXGBE_VLVF_ENTRIES) {
			/* set the vf group bit */
			if (vf < 32) {
				reg = IXGBE_READ_REG(hw, IXGBE_VLVFB(i * 2));
				reg |= (1 << vf);
				IXGBE_WRITE_REG(hw, IXGBE_VLVFB(i * 2), reg);
			} else {
				reg = IXGBE_READ_REG(hw,
				    IXGBE_VLVFB((i* 2) + 1));
				reg |= (1 << (vf - 32));
				IXGBE_WRITE_REG(hw,
				    IXGBE_VLVFB((i * 2) + 1), reg);
			}
			/* set the pf group bit */
			if (ixgbe->pf_grp < 32) {
				reg = IXGBE_READ_REG(hw, IXGBE_VLVFB(i * 2));
				reg |= (1 << ixgbe->pf_grp);
				IXGBE_WRITE_REG(hw, IXGBE_VLVFB(i * 2), reg);
			} else {
				reg = IXGBE_READ_REG(hw,
				    IXGBE_VLVFB((i* 2) + 1));
				reg |= (1 << (ixgbe->pf_grp - 32));
				IXGBE_WRITE_REG(hw,
				    IXGBE_VLVFB((i * 2) + 1), reg);
			}
			/* set the vlan id */
			IXGBE_WRITE_REG(hw, IXGBE_VLVF(i),
			    (IXGBE_VLVF_VIEN | vid));
		} else {
			ret = ENOSPC;
		}
	} else {
		if (i < IXGBE_VLVF_ENTRIES) {
			/* clear the vf group bit */
			if (vf < 32) {
				reg = IXGBE_READ_REG(hw, IXGBE_VLVFB(i * 2));
				reg &= ~(1 << vf);
				IXGBE_WRITE_REG(hw, IXGBE_VLVFB(i * 2), reg);
				reg |= IXGBE_READ_REG(hw,
				    IXGBE_VLVFB((i * 2) + 1));
			} else {
				reg = IXGBE_READ_REG(hw,
				    IXGBE_VLVFB((i * 2) + 1));
				reg &= ~(1 << (vf - 32));
				IXGBE_WRITE_REG(hw,
				    IXGBE_VLVFB((i * 2) + 1), reg);
				reg |= IXGBE_READ_REG(hw, IXGBE_VLVFB(i * 2));
			}
			/* clear the pf group bit */
			if (ixgbe->pf_grp < 32) {
				reg = IXGBE_READ_REG(hw, IXGBE_VLVFB(i * 2));
				reg &= ~(1 << ixgbe->pf_grp);
				IXGBE_WRITE_REG(hw, IXGBE_VLVFB(i * 2), reg);
				reg |= IXGBE_READ_REG(hw,
				    IXGBE_VLVFB((i * 2) + 1));
			} else {
				reg = IXGBE_READ_REG(hw,
				    IXGBE_VLVFB((i * 2) + 1));
				reg &= ~(1 << (ixgbe->pf_grp - 32));
				IXGBE_WRITE_REG(hw,
				    IXGBE_VLVFB((i * 2) + 1), reg);
				reg |= IXGBE_READ_REG(hw, IXGBE_VLVFB(i * 2));
			}
			/* clear the vlan id */
			IXGBE_WRITE_REG(hw, IXGBE_VLVF(i), 0);
		}
	}

	return (ret);
}


/*
 * Do a Function Level Reset (FLR) of the given virtual function
 */
static void
ixgbe_vf_reset(ixgbe_t *ixgbe, int vf)
{
	/* clear all flags */
	ixgbe->vf[vf].flags = 0;
	ixgbe->vf[vf].last_nack = ddi_get_time();

	/* reset offloads to defaults */
	ixgbe_set_vmolr(ixgbe, vf);

	/* reset vlans for device */
	ixgbe_clear_vf_vfta(ixgbe, vf);

	/* reset multicast table array for vf */
	ixgbe->vf[vf].num_vf_mc_hashes = 0;

	/* flush and reset the MTA with new values */
	ixgbe_setup_multicst(ixgbe);

	/* clear unicast addresses for vf */
	ixgbe_clear_vf_unicst(ixgbe, vf);
}

static void
ixgbe_vf_reset_msg(ixgbe_t *ixgbe, uint32_t vf)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	uint8_t *vf_mac = NULL;
	uint32_t reg, msgbuf[IXGBE_VFMAILBOX_SIZE];
	uint32_t bit, reg_id, i;
	u8 *addr = (u8 *)(&msgbuf[1]);

	for (i = 0; i < ixgbe->unicst_total; i++) {
		if ((ixgbe->unicst_addr[i].group_index == vf) &&
		    (ixgbe->unicst_addr[i].flags & IXGBE_ADDRESS_SET)) {
			vf_mac = ixgbe->unicst_addr[i].addr;
			break;
		}
	}

	/* process all the same items cleared in a function level reset */
	ixgbe_vf_reset(ixgbe, vf);

	bit = vf % 32;
	reg_id = vf / 32;

	/* enable transmit and receive for vf */
	reg = IXGBE_READ_REG(hw, IXGBE_VFTE(reg_id));
	IXGBE_WRITE_REG(hw, IXGBE_VFTE(reg_id), reg | (1 << bit));
	reg = IXGBE_READ_REG(hw, IXGBE_VFRE(reg_id));
	IXGBE_WRITE_REG(hw, IXGBE_VFRE(reg_id), reg | (1 << bit));

	ixgbe->vf[vf].flags = IXGBE_VF_FLAG_CTS;

	if (vf_mac != NULL) {
		/* reply to reset with ack and vf mac address */
		msgbuf[0] = IXGBE_VF_RESET | IXGBE_VT_MSGTYPE_ACK;
		(void) memcpy(addr, vf_mac, ETHERADDRL);
	} else {
		/* No VF primary mac address available */
		msgbuf[0] = IXGBE_VF_RESET | IXGBE_VT_MSGTYPE_NACK;
	}

	/* set the multicast filter type for the VF */
	msgbuf[IXGBE_VF_MC_TYPE_WORD] = (uint32_t)hw->mac.mc_filter_type;

	if (ixgbe_write_mbx(hw, msgbuf, IXGBE_VF_PERMADDR_MSG_LEN, vf)
	    != IXGBE_SUCCESS)
		ixgbe_log(ixgbe, "Writing to the mailbox fails");
}

static void
ixgbe_rcv_ack_from_vf(ixgbe_t *ixgbe, int vf)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	vf_data_t *vf_data = &ixgbe->vf[vf];
	uint32_t msg = IXGBE_VT_MSGTYPE_NACK;

	/* if device isn't clear to send it shouldn't be reading either */
	if (!(vf_data->flags & IXGBE_VF_FLAG_CTS) &&
	    seconds_later(vf_data->last_nack, 2)) {
		if (ixgbe_write_mbx(hw, &msg, 1, (uint16_t)vf) != IXGBE_SUCCESS)
			ixgbe_log(ixgbe, "Writing to the mailbox fails");
		vf_data->last_nack = ddi_get_time();
	}
}

/*
 * Receive a message from a virtual function, message API 2.0
 * 1. read message from mailbox
 * 2. dispatch to a handler routine to perform the action
 * 3. write an ack message back to virtual function
 */
void
ixgbe_rcv_msg_vf_api20(ixgbe_t *ixgbe, int vf)
{
	uint32_t msgbuf[IXGBE_VFMAILBOX_SIZE];
	struct ixgbe_hw *hw = &ixgbe->hw;
	vf_data_t *vf_data = &ixgbe->vf[vf];
	s32 retval;
	uint32_t msgsiz;

	/* read message from mailbox */
	retval = ixgbe_read_mbx(hw, msgbuf,
	    (uint16_t)IXGBE_VFMAILBOX_SIZE, (uint16_t)vf);
	if (retval) {
		ixgbe_log(ixgbe, "Error receiving message from VF %d\n", vf);
		return;
	}

	/* this is a message we already processed, do nothing */
	if (msgbuf[0] & (IXGBE_VT_MSGTYPE_ACK | IXGBE_VT_MSGTYPE_NACK))
		return;

	/*
	 * until the vf completes a reset it should not be
	 * allowed to start any configuration.
	 */
	if (msgbuf[0] == IXGBE_VF_RESET) {
		ixgbe_vf_reset_msg(ixgbe, vf);
		return;
	}

	if (!(vf_data->flags & IXGBE_VF_FLAG_CTS)) {
		msgbuf[0] = IXGBE_VT_MSGTYPE_NACK;
		if (seconds_later(vf_data->last_nack, 2)) {
			if (ixgbe_write_mbx(hw, msgbuf, 1, (uint16_t)vf)
			    != IXGBE_SUCCESS)
				ixgbe_log(ixgbe,
				    "Writing to the mailbox fails");
			vf_data->last_nack = ddi_get_time();
		}
		return;
	}

	/*
	 * Lower 16 bits of the first 4 bytes indicate message type.
	 * Some message types use the upper 16 bits; otherwise,
	 * message starts in the second 4-byte dword.
	 */
	switch ((msgbuf[0] & 0xFFFF)) {
	case IXGBE_VF_API_NEGOTIATE:
		IXGBE_DEBUGLOG_0(ixgbe, "IXGBE_VF_API_NEGOTIATE");
		retval = ixgbe_vf_negotiate(ixgbe, msgbuf, vf);
		msgsiz = 2;
		break;
	case IXGBE_VF_GET_QUEUES:
		IXGBE_DEBUGLOG_0(ixgbe, "IXGBE_VF_GET_QUEUES");
		ixgbe_get_vf_queues(ixgbe, msgbuf, vf);
		msgsiz = 4;
		retval = IXGBE_SUCCESS;
		break;
	case IXGBE_VF_ENABLE_MACADDR:
		IXGBE_DEBUGLOG_0(ixgbe, "IXGBE_VF_ENABLE_MACADDR");
		retval = ixgbe_enable_vf_mac_addr(ixgbe, msgbuf, vf);
		msgsiz = 1;
		break;
	case IXGBE_VF_DISABLE_MACADDR:
		IXGBE_DEBUGLOG_0(ixgbe, "IXGBE_VF_DISABLE_MACADDR");
		retval = ixgbe_disable_vf_mac_addr(ixgbe, msgbuf, vf);
		msgsiz = 1;
		break;
	case IXGBE_VF_GET_MACADDRS:
		IXGBE_DEBUGLOG_0(ixgbe, "IXGBE_VF_GET_MACADDRS");
		retval = ixgbe_get_vf_mac_addrs(ixgbe, msgbuf, &msgsiz, vf);
		break;
	case IXGBE_VF_SET_MULTICAST:
		IXGBE_DEBUGLOG_0(ixgbe, "IXGBE_SET_VF_MULTICASTS");
		ixgbe_set_vf_multicasts(ixgbe, &msgbuf[0], vf);
		msgsiz = 1;
		retval = IXGBE_SUCCESS;
		break;
	case IXGBE_VF_SET_MCAST_PROMISC:
		IXGBE_DEBUGLOG_0(ixgbe, "IXGBE_VF_SET_MCAST_PROMISC");
		ixgbe_set_vf_mcast_promisc(ixgbe, msgbuf, vf);
		msgsiz = 1;
		retval = IXGBE_SUCCESS;
		break;
	case IXGBE_VF_GET_MTU:
		IXGBE_DEBUGLOG_0(ixgbe, "IXGBE_VF_GET_MTU");
		ixgbe_get_vf_mtu(ixgbe, msgbuf, vf);
		msgsiz = 3;
		retval = IXGBE_SUCCESS;
		break;
	case IXGBE_VF_SET_MTU:
		IXGBE_DEBUGLOG_0(ixgbe, "IXGBE_VF_SET_MTU");
		retval = ixgbe_set_vf_mtu(ixgbe, msgbuf[1], vf);
		msgsiz = 1;
		break;
	default:
		ixgbe_log(ixgbe, "Unhandled Msg %08x from VF %d\n",
		    msgbuf[0], vf);
		retval = -IXGBE_ERR_MBX;
		break;
	}

	/* notify the VF of the results of what it sent us */
	if (retval)
		msgbuf[0] |= IXGBE_VT_MSGTYPE_NACK;
	else
		msgbuf[0] |= IXGBE_VT_MSGTYPE_ACK;

	msgbuf[0] |= IXGBE_VT_MSGTYPE_CTS;

	if (ixgbe_write_mbx(hw, msgbuf, msgsiz, (uint16_t)vf) != IXGBE_SUCCESS)
		ixgbe_log(ixgbe, "Writing to the mailbox fails");
}

/*
 * ixgbe_vf_negotiate - Respond to VF request to negotiate API version
 */
static int
ixgbe_vf_negotiate(ixgbe_t *ixgbe, uint32_t *msg, uint16_t vf)
{
	uint32_t api_req = msg[1];
	vf_data_t *vf_data = &ixgbe->vf[vf];
	int ret;

	/*
	 * If request matches a supported version, install the function pointer
	 * and return success.
	 * If requested version is not supported, don't change the default
	 * pointer.  Return failure and a hint of preferred version.
	 */
	switch (api_req) {
	case ixgbe_mbox_api_20:	/* solaris phase1 VF driver */
		vf_data->vf_api = ixgbe_mbx_api[ixgbe_mbox_api_20];
		ret = IXGBE_SUCCESS;
		break;

	case ixgbe_mbox_api_10:	/* linux/freebsd VF drivers not supported */
	default:		/* any other version: not supported */
		msg[1] = ixgbe_mbox_api_20;
		ret = IXGBE_FAILURE;
		break;
	}

	return (ret);
}

/*
 * ixgbe_get_vf_queues - Return the number of tx and rx queues available to
 * the VF
 */
/* ARGSUSED */
static void
ixgbe_get_vf_queues(ixgbe_t *ixgbe, uint32_t *msg, uint16_t vf)
{
	vf_data_t *vf_data = &ixgbe->vf[vf];

	/* maximum tx queues the vf may use */
	msg[1] = ixgbe->capab->max_tx_que_num / ixgbe->iov_mode;

	/* maximum rx queues the vf may use */
	msg[2] = ixgbe->capab->max_rx_que_num / ixgbe->iov_mode;

	/* the vlan stripping flag */
	msg[3] = vf_data->vlan_stripping;
}

/*
 * VF request to enable a unicast MAC address.
 */
static int
ixgbe_enable_vf_mac_addr(ixgbe_t *ixgbe, uint32_t *msg, uint16_t vf)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	uint8_t *mac_addr = (uint8_t *)&msg[1];
	int slot;

	ASSERT(mutex_owned(&ixgbe->gen_lock));

	/* is it a configured/allowed address? */
	slot = ixgbe_unicst_find(ixgbe, mac_addr, vf);
	if (slot < 0) {
		ixgbe_error(ixgbe, "MAC address no configured for VF%d", vf);
		return (IXGBE_FAILURE);
	}

	/* Program receive address register */
	(void) ixgbe_set_rar(hw, slot, mac_addr, vf, IXGBE_RAH_AV);

	/* Enable the MAC address in software state */
	ixgbe->unicst_addr[slot].flags |= IXGBE_ADDRESS_ENABLED;

	return (IXGBE_SUCCESS);
}

/*
 * VF request to disable a unicast MAC address.
 * This is only used by API 2.0
 */
static int
ixgbe_disable_vf_mac_addr(ixgbe_t *ixgbe, uint32_t *msg, uint16_t vf)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	uint8_t *mac_addr = (uint8_t *)&msg[1];
	int slot;

	ASSERT(mutex_owned(&ixgbe->gen_lock));

	/* is it a configured/allowed address? */
	slot = ixgbe_unicst_find(ixgbe, mac_addr, vf);
	if (slot < 0) {
		ixgbe_error(ixgbe, "MAC address no configured for VF%d", vf);
		return (IXGBE_FAILURE);
	}

	/* Clear the MAC address from adapter registers */
	(void) ixgbe_clear_rar(hw, slot);

	return (IXGBE_SUCCESS);
}

/*
 * ixgbe_get_vf_mac_addrs - return list of configured MAC addresses to the VF
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
ixgbe_get_vf_mac_addrs(ixgbe_t *ixgbe, uint32_t *msg, uint32_t *msgsiz,
    uint16_t vf)
{
	vf_data_t *vf_data = &ixgbe->vf[vf];
	int off = msg[1];	/* requested list offset */
	uint32_t total;		/* total number of addresses */
	uint32_t cnt;		/* count of addresses returned */
	uint8_t *pnt;		/* point to copied address */
	int i;

	/*
	 * check for address list change in the middle of a sequence
	 */
	/* offset 0 clears change flag */
	if (off == 0) {
		vf_data->mac_addr_chg = 0;

	/* non-0 offset and change flag set; fail the message */
	} else if (vf_data->mac_addr_chg) {
		IXGBE_DEBUGLOG_0(ixgbe, "MAC addresses changed");
		*msgsiz = 1;
		return (IXGBE_FAILURE);
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
		IXGBE_DEBUGLOG_0(ixgbe,
		    "No available alternative MAC addresses");
		msg[1] = 0;
		msg[2] = off;
		msg[3] = 0;
		*msgsiz = 4;
		return (IXGBE_SUCCESS);
	}

	total = vf_data->num_mac_addrs - 1;
	msg[1] = total;

	/* validate starting offset of returned addresses */
	if (off < total) {
		msg[2] = off;
	} else {
		IXGBE_DEBUGLOG_2(ixgbe, "Invalid offset to get MAC addresses: "
		    "offset = %d, total = %d", off, total);
		msg[2] = off;
		msg[3] = 0;
		*msgsiz = 4;
		return (IXGBE_SUCCESS);
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
	for (i = 0; i < ixgbe->unicst_total; i++) {
		if ((ixgbe->unicst_addr[i].group_index == vf) &&
		    (ixgbe->unicst_addr[i].flags & IXGBE_ADDRESS_SET))
			off--;
		else
			continue;

		if (off >= 0)
			continue;

		bcopy(ixgbe->unicst_addr[i].addr, pnt, ETHERADDRL);
		pnt += ETHERADDRL;

		if (--cnt == 0)
			break;
	}
	ASSERT(cnt == 0);

	return (IXGBE_SUCCESS);
}

static void
ixgbe_set_vf_multicasts(ixgbe_t *ixgbe, uint32_t *msgbuf, uint32_t vf)
{
	int n = (msgbuf[0] & IXGBE_VT_MSGINFO_MASK) >> IXGBE_VT_MSGINFO_SHIFT;
	uint16_t *hash_list = (uint16_t *)&msgbuf[1];
	vf_data_t *vf_data = &ixgbe->vf[vf];
	int i;

	ASSERT(mutex_owned(&ixgbe->gen_lock));

	/* only up to MAX_NUM_MULTICAST_ADDRESSES_VF hash values supported */
	if (n > MAX_NUM_MULTICAST_ADDRESSES_VF)
		n = MAX_NUM_MULTICAST_ADDRESSES_VF;

	/*
	 * keep the number of multicast addresses assigned
	 * to this VF for later use to restore when the PF multicast
	 * list changes
	 */
	vf_data->num_vf_mc_hashes = n;

	/* store the hashes for later use */
	for (i = 0; i < n; i++)
		vf_data->vf_mc_hashes[i] = hash_list[i];

	/* flush and reset the MTA with new values */
	ixgbe_setup_multicst(ixgbe);

	/*
	 * When the first multicast address is set, enable the multicast
	 * replication for the VF. When all the multicast addresses are
	 * cleared, disable the multicast replication.
	 */
	struct ixgbe_hw *hw = &ixgbe->hw;
	uint32_t vmolr;
	vmolr = IXGBE_READ_REG(hw, IXGBE_VMOLR(vf));
	if (n == 0) {
		vmolr &= ~(IXGBE_VMOLR_ROMPE | IXGBE_VMOLR_MPE);
		IXGBE_WRITE_REG(hw, IXGBE_VMOLR(vf), vmolr);
	} else if ((vmolr & IXGBE_VMOLR_ROMPE) == 0) {
		vmolr |= IXGBE_VMOLR_ROMPE | IXGBE_VMOLR_MPE;
		IXGBE_WRITE_REG(hw, IXGBE_VMOLR(vf), vmolr);

		IXGBE_WRITE_REG(&ixgbe->hw, IXGBE_FCTRL, IXGBE_FCTRL_MPE);
	}
}

/*
 * ixgbe_set_vf_mcast_promisc - Enable or disable multicast promiscuous as
 * requested in the message.
 */
static void
ixgbe_set_vf_mcast_promisc(ixgbe_t *ixgbe, uint32_t *msg, uint16_t vf)
{
	uint32_t enable = msg[1];
	vf_data_t *vf_data = &ixgbe->vf[vf];
	struct ixgbe_hw *hw = &ixgbe->hw;
	uint32_t vmolr;

	ASSERT(mutex_owned(&ixgbe->gen_lock));

	/*
	 * enable multicast promiscuous and set the flag bit
	 */
	vmolr = IXGBE_READ_REG(hw, IXGBE_VMOLR(vf));
	if (enable) {
		vmolr |= IXGBE_VMOLR_ROMPE;
		vf_data->flags |= IXGBE_VF_FLAG_MULTI_PROMISC;

	/*
	 * disable multicast promiscuous and clear the flag bit
	 */
	} else {
		vmolr &= ~IXGBE_VMOLR_ROMPE;
		vf_data->flags &= ~IXGBE_VF_FLAG_MULTI_PROMISC;
	}

	IXGBE_WRITE_REG(hw, IXGBE_VMOLR(vf), vmolr);
}

/*
 * ixgbe_get_vf_mtu - Return the upper and lower bounds on MTU in the message
 */
static void
ixgbe_get_vf_mtu(ixgbe_t *ixgbe, uint32_t *msg, uint16_t vf)
{
	/* lower bound on MTU */
	msg[1] = MIN_MTU;

	/* upper bound on VF MTU is the PF's current setting */
	msg[2] = ixgbe->vf[vf].max_mtu;
}

/*
 * ixgbe_set_vf_mtu - Set the requested MTU for the given vf.
 */
static int
ixgbe_set_vf_mtu(ixgbe_t *ixgbe, uint32_t mtu, uint16_t vf)
{
	ASSERT(mutex_owned(&ixgbe->gen_lock));

	/*
	 * validate requested mtu
	 */
	if ((mtu < MIN_MTU) || (mtu > ixgbe->vf[vf].max_mtu)) {
		IXGBE_DEBUGLOG_1(ixgbe, "Invalid MTU request: %d", mtu);
		return (IXGBE_FAILURE);
	}

	/* 82599 doesn't support VF MTU settings */
	/* ixgbe_set_vf_rlpml(ixgbe, max_frame, vf); */

	return (IXGBE_SUCCESS);
}

/*
 * Set the VM offload register for given virtual function
 */
void
ixgbe_set_vmolr(ixgbe_t *ixgbe, int vfn)
{
	struct ixgbe_hw *hw = &ixgbe->hw;
	uint32_t vmolr;

	if (hw->mac.type < ixgbe_mac_82599EB) {
		ixgbe_log(ixgbe, "ixgbe_set_vmolr: invalid mac type");
		return;
	}

	vmolr = IXGBE_READ_REG(hw, IXGBE_VMOLR(vfn));

	/* Accept broadcast */
	vmolr |= IXGBE_VMOLR_BAM;

	/* accept untagged packets */
	vmolr |= IXGBE_VMOLR_AUPE;

	/* accept multicast packets */
	vmolr |= IXGBE_VMOLR_ROMPE;

	IXGBE_WRITE_REG(hw, IXGBE_VMOLR(vfn), vmolr);
}

/*
 * request the given VF to enable or disable VLAN stripping
 */
int
ixgbe_vlan_stripping(ixgbe_t *ixgbe, uint16_t vf, boolean_t enable)
{
	vf_data_t *vf_data = &ixgbe->vf[vf];
	struct ixgbe_hw *hw = &ixgbe->hw;
	struct ixgbe_mbx_info *mbx = &hw->mbx;
	uint32_t msg[2];
	int32_t ret;

	/* set the vlan stripping flag */
	vf_data->vlan_stripping = enable;

	msg[0] = IXGBE_PF_TRANSPARENT_VLAN;

	msg[1] = enable;

	/* send message */
	ret = mbx->ops.write_posted(hw, msg, 2, vf);
	if (ret)
		return (IXGBE_FAILURE);

	/* read response */
	ret = mbx->ops.read_posted(hw, msg, 1, vf);
	if (ret)
		return (IXGBE_FAILURE);

	/*
	 * for success require ACK of the message
	 */
	if (!(msg[0] & IXGBE_VT_MSGTYPE_ACK))
		return (IXGBE_FAILURE);

	return (IXGBE_SUCCESS);
}
