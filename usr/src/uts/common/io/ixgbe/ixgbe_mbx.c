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

/* IntelVersion: 1.13 scm_011511_003853 */

#include "ixgbe_type.h"
#include "ixgbe_mbx.h"

/*
 *  ixgbe_read_mbx - Reads a message from the mailbox
 *  @hw: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 *  @mbx_id: id of mailbox to read
 *
 *  returns SUCCESS if it successfuly read message from buffer
 */
s32
ixgbe_read_mbx(struct ixgbe_hw *hw, u32 *msg, u16 size, u16 mbx_id)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;
	s32 ret_val = IXGBE_ERR_MBX;

	DEBUGFUNC("ixgbe_read_mbx");

	/* limit read to size of mailbox */
	if (size > mbx->size)
		size = mbx->size;

	if (mbx->ops.read)
		ret_val = mbx->ops.read(hw, msg, size, mbx_id);

	return (ret_val);
}

/*
 *  ixgbe_write_mbx - Write a message to the mailbox
 *  @hw: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 *  @mbx_id: id of mailbox to write
 *
 *  returns SUCCESS if it successfully copied message into the buffer
 */
s32
ixgbe_write_mbx(struct ixgbe_hw *hw, u32 *msg, u16 size, u16 mbx_id)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;
	s32 ret_val = IXGBE_SUCCESS;

	DEBUGFUNC("ixgbe_write_mbx");

	if (size > mbx->size)
		ret_val = IXGBE_ERR_MBX;

	else if (mbx->ops.write)
		ret_val = mbx->ops.write(hw, msg, size, mbx_id);

	return (ret_val);
}

/*
 *  ixgbe_check_for_msg - checks to see if someone sent us mail
 *  @hw: pointer to the HW structure
 *  @mbx_id: id of mailbox to check
 *
 *  returns SUCCESS if the Status bit was found or else ERR_MBX
 */
s32
ixgbe_check_for_msg(struct ixgbe_hw *hw, u16 mbx_id)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;
	s32 ret_val = IXGBE_ERR_MBX;

	DEBUGFUNC("ixgbe_check_for_msg");

	if (mbx->ops.check_for_msg)
		ret_val = mbx->ops.check_for_msg(hw, mbx_id);

	return (ret_val);
}

/*
 *  ixgbe_check_for_ack - checks to see if someone sent us ACK
 *  @hw: pointer to the HW structure
 *  @mbx_id: id of mailbox to check
 *
 *  returns SUCCESS if the Status bit was found or else ERR_MBX
 */
s32
ixgbe_check_for_ack(struct ixgbe_hw *hw, u16 mbx_id)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;
	s32 ret_val = IXGBE_ERR_MBX;

	DEBUGFUNC("ixgbe_check_for_ack");

	if (mbx->ops.check_for_ack)
		ret_val = mbx->ops.check_for_ack(hw, mbx_id);

	return (ret_val);
}

/*
 *  ixgbe_check_for_rst - checks to see if other side has reset
 *  @hw: pointer to the HW structure
 *  @mbx_id: id of mailbox to check
 *
 *  returns SUCCESS if the Status bit was found or else ERR_MBX
 */
s32
ixgbe_check_for_rst(struct ixgbe_hw *hw, u16 mbx_id)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;
	s32 ret_val = IXGBE_ERR_MBX;

	DEBUGFUNC("ixgbe_check_for_rst");

	if (mbx->ops.check_for_rst)
		ret_val = mbx->ops.check_for_rst(hw, mbx_id);

	return (ret_val);
}

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
s32
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
s32
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
 *  ixgbe_init_mbx_ops_generic - Initialize MB function pointers
 *  @hw: pointer to the HW structure
 *
 *  Setups up the mailbox read and write message function pointers
 */
void
ixgbe_init_mbx_ops_generic(struct ixgbe_hw *hw)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;

	mbx->ops.read_posted = ixgbe_read_posted_mbx;
	mbx->ops.write_posted = ixgbe_write_posted_mbx;
}

static s32
ixgbe_check_for_bit_pf(struct ixgbe_hw *hw, u32 mask, s32 index)
{
	u32 mbvficr = IXGBE_READ_REG(hw, IXGBE_MBVFICR(index));
	s32 ret_val = IXGBE_ERR_MBX;

	if (mbvficr & mask) {
		ret_val = IXGBE_SUCCESS;
		IXGBE_WRITE_REG(hw, IXGBE_MBVFICR(index), mask);
	}

	return (ret_val);
}

/*
 *  ixgbe_check_for_msg_pf - checks to see if the VF has sent mail
 *  @hw: pointer to the HW structure
 *  @vf_number: the VF index
 *
 *  returns SUCCESS if the VF has set the Status bit or else ERR_MBX
 */
static s32
ixgbe_check_for_msg_pf(struct ixgbe_hw *hw, u16 vf_number)
{
	s32 ret_val = IXGBE_ERR_MBX;
	s32 index = IXGBE_MBVFICR_INDEX(vf_number);
	u32 vf_bit = vf_number % 16;

	DEBUGFUNC("ixgbe_check_for_msg_pf");

	if (!ixgbe_check_for_bit_pf(hw, IXGBE_MBVFICR_VFREQ_VF1 << vf_bit,
	    index)) {
		ret_val = IXGBE_SUCCESS;
		hw->mbx.stats.reqs++;
	}

	return (ret_val);
}

/*
 *  ixgbe_check_for_ack_pf - checks to see if the VF has ACKed
 *  @hw: pointer to the HW structure
 *  @vf_number: the VF index
 *
 *  returns SUCCESS if the VF has set the Status bit or else ERR_MBX
 */
static s32
ixgbe_check_for_ack_pf(struct ixgbe_hw *hw, u16 vf_number)
{
	s32 ret_val = IXGBE_ERR_MBX;
	s32 index = IXGBE_MBVFICR_INDEX(vf_number);
	u32 vf_bit = vf_number % 16;

	DEBUGFUNC("ixgbe_check_for_ack_pf");

	if (!ixgbe_check_for_bit_pf(hw, IXGBE_MBVFICR_VFACK_VF1 << vf_bit,
	    index)) {
		ret_val = IXGBE_SUCCESS;
		hw->mbx.stats.acks++;
	}

	return (ret_val);
}

/*
 *  ixgbe_check_for_rst_pf - checks to see if the VF has reset
 *  @hw: pointer to the HW structure
 *  @vf_number: the VF index
 *
 *  returns SUCCESS if the VF has set the Status bit or else ERR_MBX
 */
static s32
ixgbe_check_for_rst_pf(struct ixgbe_hw *hw, u16 vf_number)
{
	u32 reg_offset = (vf_number < 32) ? 0 : 1;
	u32 vf_shift = vf_number % 32;
	u32 vflre = 0;
	s32 ret_val = IXGBE_ERR_MBX;

	DEBUGFUNC("ixgbe_check_for_rst_pf");

	switch (hw->mac.type) {
	case ixgbe_mac_82599EB:
		vflre = IXGBE_READ_REG(hw, IXGBE_VFLRE(reg_offset));
		break;
	case ixgbe_mac_X540:
		vflre = IXGBE_READ_REG(hw, IXGBE_VFLREC(reg_offset));
		break;
	default:
		goto out;
	}

	if (vflre & (1 << vf_shift)) {
		ret_val = IXGBE_SUCCESS;
		IXGBE_WRITE_REG(hw, IXGBE_VFLREC(reg_offset), (1 << vf_shift));
		hw->mbx.stats.rsts++;
	}

out:
	return (ret_val);
}

/*
 *  ixgbe_obtain_mbx_lock_pf - obtain mailbox lock
 *  @hw: pointer to the HW structure
 *  @vf_number: the VF index
 *
 *  return SUCCESS if we obtained the mailbox lock
 */
static s32
ixgbe_obtain_mbx_lock_pf(struct ixgbe_hw *hw, u16 vf_number)
{
	s32 ret_val = IXGBE_ERR_MBX;
	u32 p2v_mailbox;

	DEBUGFUNC("ixgbe_obtain_mbx_lock_pf");

	/* Take ownership of the buffer */
	IXGBE_WRITE_REG(hw, IXGBE_PFMAILBOX(vf_number), IXGBE_PFMAILBOX_PFU);

	/* reserve mailbox for vf use */
	p2v_mailbox = IXGBE_READ_REG(hw, IXGBE_PFMAILBOX(vf_number));
	if (p2v_mailbox & IXGBE_PFMAILBOX_PFU)
		ret_val = IXGBE_SUCCESS;

	return (ret_val);
}

/*
 *  ixgbe_write_mbx_pf - Places a message in the mailbox
 *  @hw: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 *  @vf_number: the VF index
 *
 *  returns SUCCESS if it successfully copied message into the buffer
 */
static s32
ixgbe_write_mbx_pf(struct ixgbe_hw *hw, u32 *msg, u16 size,
    u16 vf_number)
{
	s32 ret_val;
	u16 i;

	DEBUGFUNC("ixgbe_write_mbx_pf");

	/* lock the mailbox to prevent pf/vf race condition */
	ret_val = ixgbe_obtain_mbx_lock_pf(hw, vf_number);
	if (ret_val)
		goto out_no_write;

	/* flush msg and acks as we are overwriting the message buffer */
	(void) ixgbe_check_for_msg_pf(hw, vf_number);
	(void) ixgbe_check_for_ack_pf(hw, vf_number);

	/* copy the caller specified message to the mailbox memory buffer */
	for (i = 0; i < size; i++)
		IXGBE_WRITE_REG_ARRAY(hw, IXGBE_PFMBMEM(vf_number), i, msg[i]);

	/* Interrupt VF to tell it a message has been sent and release buffer */
	IXGBE_WRITE_REG(hw, IXGBE_PFMAILBOX(vf_number), IXGBE_PFMAILBOX_STS);

	/* update stats */
	hw->mbx.stats.msgs_tx++;

out_no_write:
	return (ret_val);
}

/*
 *  ixgbe_read_mbx_pf - Read a message from the mailbox
 *  @hw: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 *  @vf_number: the VF index
 *
 *  This function copies a message from the mailbox buffer to the caller's
 *  memory buffer.  The presumption is that the caller knows that there was
 *  a message due to a VF request so no polling for message is needed.
 */
static s32
ixgbe_read_mbx_pf(struct ixgbe_hw *hw, u32 *msg, u16 size,
    u16 vf_number)
{
	s32 ret_val;
	u16 i;

	DEBUGFUNC("ixgbe_read_mbx_pf");

	/* lock the mailbox to prevent pf/vf race condition */
	ret_val = ixgbe_obtain_mbx_lock_pf(hw, vf_number);
	if (ret_val)
		goto out_no_read;

	/* copy the message to the mailbox memory buffer */
	for (i = 0; i < size; i++)
		msg[i] = IXGBE_READ_REG_ARRAY(hw, IXGBE_PFMBMEM(vf_number), i);

	/* Acknowledge the message and release buffer */
	IXGBE_WRITE_REG(hw, IXGBE_PFMAILBOX(vf_number), IXGBE_PFMAILBOX_ACK);

	/* update stats */
	hw->mbx.stats.msgs_rx++;

out_no_read:
	return (ret_val);
}

/*
 *  ixgbe_init_mbx_params_pf - set initial values for pf mailbox
 *  @hw: pointer to the HW structure
 *
 *  Initializes the hw->mbx struct to correct values for pf mailbox
 */
void
ixgbe_init_mbx_params_pf(struct ixgbe_hw *hw)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;

	if (hw->mac.type != ixgbe_mac_82599EB &&
	    hw->mac.type != ixgbe_mac_X540)
		return;

	mbx->timeout =  IXGBE_VF_MBX_INIT_TIMEOUT;
	mbx->usec_delay = IXGBE_VF_MBX_INIT_DELAY;

	mbx->size = IXGBE_VFMAILBOX_SIZE;

	mbx->ops.read = ixgbe_read_mbx_pf;
	mbx->ops.write = ixgbe_write_mbx_pf;
	mbx->ops.read_posted = ixgbe_read_posted_mbx;
	mbx->ops.write_posted = ixgbe_write_posted_mbx;
	mbx->ops.check_for_msg = ixgbe_check_for_msg_pf;
	mbx->ops.check_for_ack = ixgbe_check_for_ack_pf;
	mbx->ops.check_for_rst = ixgbe_check_for_rst_pf;

	mbx->stats.msgs_tx = 0;
	mbx->stats.msgs_rx = 0;
	mbx->stats.reqs = 0;
	mbx->stats.acks = 0;
	mbx->stats.rsts = 0;
}
