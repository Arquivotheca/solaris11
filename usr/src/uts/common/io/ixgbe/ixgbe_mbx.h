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
/* IntelVersion: 1.7 scm_011511_003853 */

#ifndef _IXGBE_MBX_H
#define	_IXGBE_MBX_H

#include "ixgbe_type.h"

#define	IXGBE_VFMAILBOX_SIZE	16 /* 16 32 bit words - 64 bytes */
#define	IXGBE_ERR_MBX		-100

#define	IXGBE_VFMAILBOX		0x002FC
#define	IXGBE_VFMBMEM		0x00200

#define	IXGBE_PFMAILBOX(x)	  (0x04B00 + (4 * x))
#define	IXGBE_PFMBMEM(vfn)	  (0x13000 + (64 * vfn))

#define	IXGBE_PFMAILBOX_STS   0x00000001 /* Initiate message send to VF */
#define	IXGBE_PFMAILBOX_ACK   0x00000002 /* Ack message recv'd from VF */
#define	IXGBE_PFMAILBOX_VFU   0x00000004 /* VF owns the mailbox buffer */
#define	IXGBE_PFMAILBOX_PFU   0x00000008 /* PF owns the mailbox buffer */
#define	IXGBE_PFMAILBOX_RVFU  0x00000010 /* Reset VFU - used when VF stuck */

#define	IXGBE_MBVFICR_VFREQ_MASK 0x0000FFFF /* bits for VF messages */
#define	IXGBE_MBVFICR_VFREQ_VF1  0x00000001 /* bit for VF 1 message */
#define	IXGBE_MBVFICR_VFACK_MASK 0xFFFF0000 /* bits for VF acks */
#define	IXGBE_MBVFICR_VFACK_VF1  0x00010000 /* bit for VF 1 ack */

/*
 * If it's a IXGBE_VF_* msg then it originates in the VF and is sent to the
 * PF.  The reverse is true if it is IXGBE_PF_*.
 * Message ACK's are the value or'd with 0xF0000000
 */
/* Messages below or'd with this are the ACK */
#define	IXGBE_VT_MSGTYPE_ACK	0x80000000
/* Messages below or'd with this are the NACK */
#define	IXGBE_VT_MSGTYPE_NACK	0x40000000
/* Indicates that VF is still clear to send requests */
#define	IXGBE_VT_MSGTYPE_CTS	0x20000000
#define	IXGBE_VT_MSGINFO_SHIFT	16
/* bits 23:16 are used for exra info for certain messages */
#define	IXGBE_VT_MSGINFO_MASK	(0xFF << IXGBE_VT_MSGINFO_SHIFT)

/* definitions to support mailbox API version negotiation */

/*
 * each element denotes a version of the API; existing numbers may not
 * change; any additions must go at the end
 */
enum ixgbe_pfvf_api_rev {
	ixgbe_mbox_api_10,	/* API version 1.0, linux VF driver */
	ixgbe_mbox_api_20	/* API version 2.0, solaris Phase1 VF driver */
};

/* mailbox API, version 1.0 VF requests */
#define	IXGBE_VF_RESET		0x01 /* VF requests reset */
#define	IXGBE_VF_SET_MAC_ADDR	0x02 /* VF requests to set MAC addr */
#define	IXGBE_VF_SET_MULTICAST	0x03 /* VF requests to set MC addr */
#define	IXGBE_VF_SET_MULTICAST_COUNT_MASK (0x1F << IXGBE_VT_MSGINFO_SHIFT)
#define	IXGBE_VF_SET_MULTICAST_OVERFLOW	(0x80 << IXGBE_VT_MSGINFO_SHIFT)
#define	IXGBE_VF_SET_VLAN	0x04 /* VF requests to set VLAN */
#define	IXGBE_VF_SET_VLAN_ADD		(0x01 << IXGBE_VT_MSGINFO_SHIFT)
#define	IXGBE_VF_SET_LPE	0x05 /* VF requests to set VMOLR.LPE */
#define	IXGBE_VF_SET_PROMISC	0x06 /* VF requests to clear VMOLR.ROPE/MPME */
#define	IXGBE_VF_SET_PROMISC_UNICAST	(0x01 << IXGBE_VT_MSGINFO_SHIFT)
#define	IXGBE_VF_SET_PROMISC_MULTICAST	(0x02 << IXGBE_VT_MSGINFO_SHIFT)

/* mailbox API, version 2.0 VF requests */
#define	IXGBE_VF_API_NEGOTIATE		0x08 /* negotiate API version */
#define	IXGBE_VF_GET_QUEUES		0x09 /* get queue configuration */
#define	IXGBE_VF_ENABLE_MACADDR		0x0A /* enable MAC address */
#define	IXGBE_VF_DISABLE_MACADDR	0x0B /* disable MAC address */
#define	IXGBE_VF_GET_MACADDRS		0x0C /* get all configured MAC addrs */
#define	IXGBE_VF_SET_MCAST_PROMISC	0x0D /* enable multicast promiscuous */
#define	IXGBE_VF_GET_MTU		0x0E /* get bounds on MTU */
#define	IXGBE_VF_SET_MTU		0x0F /* set a specific MTU */

/* mailbox API, version 2.0 PF request */
#define	IXGBE_PF_TRANSPARENT_VLAN	0x0101 /* set VLAN Stripping */

/* length of permanent address message returned from PF */
#define	IXGBE_VF_PERMADDR_MSG_LEN	4
/* word in permanent address message with the current multicast type */
#define	IXGBE_VF_MC_TYPE_WORD	3

#define	IXGBE_PF_CONTROL_MSG	0x0100 /* PF control message */

#define	IXGBE_VF_MBX_INIT_TIMEOUT	2000 /* number of retries on mailbox */
#define	IXGBE_VF_MBX_INIT_DELAY		500  /* microseconds between retries */

s32 ixgbe_read_mbx(struct ixgbe_hw *, u32 *, u16, u16);
s32 ixgbe_write_mbx(struct ixgbe_hw *, u32 *, u16, u16);
s32 ixgbe_read_posted_mbx(struct ixgbe_hw *, u32 *, u16, u16);
s32 ixgbe_write_posted_mbx(struct ixgbe_hw *, u32 *, u16, u16);
s32 ixgbe_check_for_msg(struct ixgbe_hw *, u16);
s32 ixgbe_check_for_ack(struct ixgbe_hw *, u16);
s32 ixgbe_check_for_rst(struct ixgbe_hw *, u16);
void ixgbe_init_mbx_ops_generic(struct ixgbe_hw *hw);
void ixgbe_init_mbx_params_pf(struct ixgbe_hw *);

#endif /* _IXGBE_MBX_H */
