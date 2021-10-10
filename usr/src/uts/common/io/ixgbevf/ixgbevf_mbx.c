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

/* IntelVersion: 1.12  */

#include "ixgbevf_type.h"
#include "ixgbevf_mbx.h"
#include "ixgbevf_osdep.h"

/*
 *  ixgbe_poll_for_msg - Wait for message notification
 *  @hw: pointer to the HW structure
 *  @mbx_id: id of mailbox to write
 *
 *  returns SUCCESS if it successfully received a message notification
 */
static s32
ixgbe_poll_for_msg(struct ixgbe_hw *hw, u16 mbx_id)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;
	int countdown = mbx->timeout;

	DEBUGFUNC("ixgbe_poll_for_msg");

	if (!countdown || !mbx->ops.check_for_msg)
		goto out;

	while (countdown && mbx->ops.check_for_msg(hw, mbx_id)) {
		countdown--;
		if (!countdown)
			break;
		usec_delay(mbx->usec_delay);
	}

out:
	return (countdown ? IXGBE_SUCCESS : IXGBE_ERR_MBX);
}

/*
 *  ixgbe_poll_for_ack - Wait for message acknowledgement
 *  @hw: pointer to the HW structure
 *  @mbx_id: id of mailbox to write
 *
 *  returns SUCCESS if it successfully received a message acknowledgement
 */
static s32
ixgbe_poll_for_ack(struct ixgbe_hw *hw, u16 mbx_id)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;
	int countdown = mbx->timeout;

	DEBUGFUNC("ixgbe_poll_for_ack");

	if (!countdown || !mbx->ops.check_for_ack)
		goto out;

	while (countdown && mbx->ops.check_for_ack(hw, mbx_id)) {
		countdown--;
		if (!countdown)
			break;
		usec_delay(mbx->usec_delay);
	}

out:
	return (countdown ? IXGBE_SUCCESS : IXGBE_ERR_MBX);
}

/*
 *  ixgbe_read_posted_mbx - Wait for message notification and receive message
 *  @hw: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 *  @mbx_id: id of mailbox to write
 *
 *  returns SUCCESS if it successfully received a message notification and
 *  copied it into the receive buffer.
 */
static s32
ixgbe_read_posted_mbx(struct ixgbe_hw *hw, u32 *msg, u16 size, u16 mbx_id)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;
	s32 ret_val = IXGBE_ERR_MBX;

	DEBUGFUNC("ixgbe_read_posted_mbx");

	if (!mbx->ops.read)
		goto out;

	ret_val = ixgbe_poll_for_msg(hw, mbx_id);

	/* if ack received read message, otherwise we timed out */
	if (!ret_val)
		ret_val = mbx->ops.read(hw, msg, size, mbx_id);
out:
	return (ret_val);
}

/*
 *  ixgbe_write_posted_mbx - Write a message to the mailbox, wait for ack
 *  @hw: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 *  @mbx_id: id of mailbox to write
 *
 *  returns SUCCESS if it successfully copied message into the buffer and
 *  received an ack to that message within delay * timeout period
 */
static s32
ixgbe_write_posted_mbx(struct ixgbe_hw *hw, u32 *msg, u16 size, u16 mbx_id)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;
	s32 ret_val = IXGBE_ERR_MBX;

	DEBUGFUNC("ixgbe_write_posted_mbx");

	/* exit if either we can't write or there isn't a defined timeout */
	if (!mbx->ops.write || !mbx->timeout)
		goto out;

	/* send msg */
	ret_val = mbx->ops.write(hw, msg, size, mbx_id);

	/* if msg sent wait until we receive an ack */
	if (!ret_val)
		ret_val = ixgbe_poll_for_ack(hw, mbx_id);
out:
	return (ret_val);
}

/*
 *  ixgbe_read_v2p_mailbox - read v2p mailbox
 *  @hw: pointer to the HW structure
 *
 *  This function is used to read the v2p mailbox without losing the read to
 *  clear status bits.
 */
static u32
ixgbe_read_v2p_mailbox(struct ixgbe_hw *hw)
{
	u32 v2p_mailbox = IXGBE_READ_REG(hw, IXGBE_VFMAILBOX);

	v2p_mailbox |= hw->mbx.v2p_mailbox;
	hw->mbx.v2p_mailbox |= v2p_mailbox & IXGBE_VFMAILBOX_R2C_BITS;

	return (v2p_mailbox);
}

/*
 *  ixgbe_check_for_bit_vf - Determine if a status bit was set
 *  @hw: pointer to the HW structure
 *  @mask: bitmask for bits to be tested and cleared
 *
 *  This function is used to check for the read to clear bits within
 *  the V2P mailbox.
 */
static s32
ixgbe_check_for_bit_vf(struct ixgbe_hw *hw, u32 mask)
{
	u32 v2p_mailbox = ixgbe_read_v2p_mailbox(hw);
	s32 ret_val = IXGBE_ERR_MBX;

	if (v2p_mailbox & mask)
		ret_val = IXGBE_SUCCESS;

	hw->mbx.v2p_mailbox &= ~mask;

	return (ret_val);
}

/*
 *  ixgbe_check_for_msg_vf - checks to see if the PF has sent mail
 *  @hw: pointer to the HW structure
 *  @mbx_id: id of mailbox to check
 *
 *  returns SUCCESS if the PF has set the Status bit or else ERR_MBX
 */
static s32
ixgbe_check_for_msg_vf(struct ixgbe_hw *hw, u16 mbx_id)
{
	s32 ret_val = IXGBE_ERR_MBX;

	UNREFERENCED_PARAMETER(mbx_id);
	DEBUGFUNC("ixgbe_check_for_msg_vf");

	if (!ixgbe_check_for_bit_vf(hw, IXGBE_VFMAILBOX_PFSTS)) {
		ret_val = IXGBE_SUCCESS;
		hw->mbx.stats.reqs++;
	}

	return (ret_val);
}

/*
 *  ixgbe_check_for_ack_vf - checks to see if the PF has ACK'd
 *  @hw: pointer to the HW structure
 *  @mbx_id: id of mailbox to check
 *
 *  returns SUCCESS if the PF has set the ACK bit or else ERR_MBX
 */
static s32
ixgbe_check_for_ack_vf(struct ixgbe_hw *hw, u16 mbx_id)
{
	s32 ret_val = IXGBE_ERR_MBX;

	UNREFERENCED_PARAMETER(mbx_id);
	DEBUGFUNC("ixgbe_check_for_ack_vf");

	if (!ixgbe_check_for_bit_vf(hw, IXGBE_VFMAILBOX_PFACK)) {
		ret_val = IXGBE_SUCCESS;
		hw->mbx.stats.acks++;
	}

	return (ret_val);
}

/*
 *  ixgbe_check_for_rst_vf - checks to see if the PF has reset
 *  @hw: pointer to the HW structure
 *  @mbx_id: id of mailbox to check
 *
 *  returns true if the PF has set the reset done bit or else false
 */
static s32
ixgbe_check_for_rst_vf(struct ixgbe_hw *hw, u16 mbx_id)
{
	s32 ret_val = IXGBE_ERR_MBX;

	UNREFERENCED_PARAMETER(mbx_id);
	DEBUGFUNC("ixgbe_check_for_rst_vf");

	if (!ixgbe_check_for_bit_vf(hw, (IXGBE_VFMAILBOX_RSTD |
	    IXGBE_VFMAILBOX_RSTI))) {
		ret_val = IXGBE_SUCCESS;
		hw->mbx.stats.rsts++;
	}

	return (ret_val);
}

/*
 *  ixgbe_obtain_mbx_lock_vf - obtain mailbox lock
 *  @hw: pointer to the HW structure
 *
 *  return SUCCESS if we obtained the mailbox lock
 */
static s32
ixgbe_obtain_mbx_lock_vf(struct ixgbe_hw *hw)
{
	s32 ret_val = IXGBE_ERR_MBX;

	DEBUGFUNC("ixgbe_obtain_mbx_lock_vf");

	/* Take ownership of the buffer */
	IXGBE_WRITE_REG(hw, IXGBE_VFMAILBOX, IXGBE_VFMAILBOX_VFU);

	/* reserve mailbox for vf use */
	if (ixgbe_read_v2p_mailbox(hw) & IXGBE_VFMAILBOX_VFU)
		ret_val = IXGBE_SUCCESS;

	return (ret_val);
}

/*
 *  ixgbe_write_mbx_vf - Write a message to the mailbox
 *  @hw: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 *  @mbx_id: id of mailbox to write
 *
 *  returns SUCCESS if it successfully copied message into the buffer
 */
static s32
ixgbe_write_mbx_vf(struct ixgbe_hw *hw, u32 *msg, u16 size, u16 mbx_id)
{
	s32 ret_val;
	u16 i;

	UNREFERENCED_PARAMETER(mbx_id);

	DEBUGFUNC("ixgbe_write_mbx_vf");

	/* lock the mailbox to prevent pf/vf race condition */
	ret_val = ixgbe_obtain_mbx_lock_vf(hw);
	if (ret_val)
		goto out_no_write;

	/* flush msg and acks as we are overwriting the message buffer */
	(void) ixgbe_check_for_msg_vf(hw, 0);
	(void) ixgbe_check_for_ack_vf(hw, 0);

	/* copy the caller specified message to the mailbox memory buffer */
	for (i = 0; i < size; i++)
		IXGBE_WRITE_REG_ARRAY(hw, IXGBE_VFMBMEM, i, msg[i]);

	/* update stats */
	hw->mbx.stats.msgs_tx++;

	/* Drop VFU and interrupt the PF to tell it a message has been sent */
	IXGBE_WRITE_REG(hw, IXGBE_VFMAILBOX, IXGBE_VFMAILBOX_REQ);

out_no_write:
	return (ret_val);
}

/*
 *  ixgbe_read_mbx_vf - Reads a message from the inbox intended for vf
 *  @hw: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 *  @mbx_id: id of mailbox to read
 *
 *  returns SUCCESS if it successfuly read message from buffer
 */
static s32
ixgbe_read_mbx_vf(struct ixgbe_hw *hw, u32 *msg, u16 size, u16 mbx_id)
{
	s32 ret_val = IXGBE_SUCCESS;
	u16 i;

	DEBUGFUNC("ixgbe_read_mbx_vf");
	UNREFERENCED_PARAMETER(mbx_id);

	/* lock the mailbox to prevent pf/vf race condition */
	ret_val = ixgbe_obtain_mbx_lock_vf(hw);
	if (ret_val)
		goto out_no_read;

	/* copy the message from the mailbox memory buffer */
	for (i = 0; i < size; i++)
		msg[i] = IXGBE_READ_REG_ARRAY(hw, IXGBE_VFMBMEM, i);

	/* Acknowledge receipt and release mailbox, then we're done */
	IXGBE_WRITE_REG(hw, IXGBE_VFMAILBOX, IXGBE_VFMAILBOX_ACK);

	/* update stats */
	hw->mbx.stats.msgs_rx++;

out_no_read:
	return (ret_val);
}

/*
 *  ixgbe_init_mbx_params_vf - set initial values for vf mailbox
 *  @hw: pointer to the HW structure
 *
 *  Initializes the hw->mbx struct to correct values for vf mailbox
 */
void
ixgbe_init_mbx_params_vf(struct ixgbe_hw *hw)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;

	/*
	 * start mailbox as timed out and let the reset_hw call set the timeout
	 * value to begin communications
	 */
	mbx->timeout = 0;
	mbx->usec_delay = IXGBE_VF_MBX_INIT_DELAY;

	mbx->size = IXGBE_VFMAILBOX_SIZE;

	mbx->ops.read = ixgbe_read_mbx_vf;
	mbx->ops.write = ixgbe_write_mbx_vf;
	mbx->ops.read_posted = ixgbe_read_posted_mbx;
	mbx->ops.write_posted = ixgbe_write_posted_mbx;
	mbx->ops.check_for_msg = ixgbe_check_for_msg_vf;
	mbx->ops.check_for_ack = ixgbe_check_for_ack_vf;
	mbx->ops.check_for_rst = ixgbe_check_for_rst_vf;

	mbx->stats.msgs_tx = 0;
	mbx->stats.msgs_rx = 0;
	mbx->stats.reqs = 0;
	mbx->stats.acks = 0;
	mbx->stats.rsts = 0;
}
