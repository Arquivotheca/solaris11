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

/* IntelVersion: 1.8 */

#ifndef _IGBVF_MBX_H_
#define	_IGBVF_MBX_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "igbvf_vf.h"

/* Define mailbox specific registers */
#define	E1000_V2PMAILBOX(_n)	(0x00C40 + (4 * (_n)))
#define	E1000_VMBMEM(_n)	(0x00800 + (64 * (_n)))

/* Define mailbox register bits */
#define	E1000_V2PMAILBOX_REQ	0x00000001 /* Request for PF Ready bit */
#define	E1000_V2PMAILBOX_ACK	0x00000002 /* Ack PF message received */
#define	E1000_V2PMAILBOX_VFU	0x00000004 /* VF owns the mailbox buffer */
#define	E1000_V2PMAILBOX_PFU	0x00000008 /* PF owns the mailbox buffer */
#define	E1000_V2PMAILBOX_PFSTS	0x00000010 /* PF wrote a message in the MB */
#define	E1000_V2PMAILBOX_PFACK	0x00000020 /* PF ack the previous VF msg */
#define	E1000_V2PMAILBOX_RSTI	0x00000040 /* PF has reset indication */
#define	E1000_V2PMAILBOX_RSTD	0x00000080 /* PF has indicated reset done */
#define	E1000_V2PMAILBOX_R2C_BITS 0x000000B0 /* All read to clear bits */

#define	E1000_VFMAILBOX_SIZE	16 /* 16 32 bit words - 64 bytes */

/*
 * If it's a E1000_VF_* msg then it originates in the VF and is sent to the
 * PF.  The reverse is true if it is E1000_PF_*.
 * Message ACK's are the value or'd with 0xF0000000
 */
/* Messages below or'd with this are the ACK */
#define	E1000_VT_MSGTYPE_ACK	0x80000000
/* Messages below or'd with this are the NACK */
#define	E1000_VT_MSGTYPE_NACK	0x40000000
/* Indicates that VF is still clear to send requests */
#define	E1000_VT_MSGTYPE_CTS	0x20000000

#define	E1000_VT_MSGINFO_SHIFT	16
/* bits 23:16 are used for exra info for certain messages */
#define	E1000_VT_MSGINFO_MASK	(0xFF << E1000_VT_MSGINFO_SHIFT)

/* definitions to support mailbox API version negotiation */

/*
 * each element denotes a version of the API; existing numbers may not
 * change; any additions must go at the end
 */
enum e1000_pfvf_api_rev {
	e1000_mbox_api_10,	/* API version 1.0, linux VF driver */
	e1000_mbox_api_20	/* API version 2.0, solaris Phase1 VF driver */
};

/* mailbox API, version 1.0 VF requests */
#define	E1000_VF_RESET		0x01 /* VF requests reset */
#define	E1000_VF_SET_MAC_ADDR	0x02 /* VF requests to set MAC addr */
#define	E1000_VF_SET_MULTICAST	0x03 /* VF requests to set MC addr */
#define	E1000_VF_SET_MULTICAST_COUNT_MASK (0x1F << E1000_VT_MSGINFO_SHIFT)
#define	E1000_VF_SET_MULTICAST_OVERFLOW	(0x80 << E1000_VT_MSGINFO_SHIFT)
#define	E1000_VF_SET_VLAN	0x04 /* VF requests to set VLAN */
#define	E1000_VF_SET_VLAN_ADD		(0x01 << E1000_VT_MSGINFO_SHIFT)
#define	E1000_VF_SET_LPE	0x05 /* VF requests to set VMOLR.LPE */
#define	E1000_VF_SET_PROMISC	0x06 /* VF requests to clear VMOLR.ROPE/MPME */
#define	E1000_VF_SET_PROMISC_UNICAST	(0x01 << E1000_VT_MSGINFO_SHIFT)
#define	E1000_VF_SET_PROMISC_MULTICAST	(0x02 << E1000_VT_MSGINFO_SHIFT)

#define	E1000_PF_CONTROL_MSG		0x0100 /* PF control message */

/* mailbox API, version 2.0 VF requests */
#define	E1000_VF_API_NEGOTIATE		0x08 /* negotiate API version */
#define	E1000_VF_GET_QUEUES		0x09 /* get queue configuration */
#define	E1000_VF_ENABLE_MACADDR		0x0A /* enable MAC address */
#define	E1000_VF_DISABLE_MACADDR	0x0B /* disable MAC address */
#define	E1000_VF_GET_MACADDRS		0x0C /* get all configured MAC addrs */
#define	E1000_VF_SET_MCAST_PROMISC	0x0D /* enable multicast promiscuous */
#define	E1000_VF_GET_MTU		0x0E /* get bounds on MTU */
#define	E1000_VF_SET_MTU		0x0F /* set a specific MTU */

/* mailbox API, version 2.0 PF requests */
#define	E1000_PF_TRANSPARENT_VLAN	0x0101 /* enable transparent vlan */

#define	E1000_VF_MBX_INIT_TIMEOUT	2000 /* number of retries on mailbox */
#define	E1000_VF_MBX_INIT_DELAY		500  /* microseconds between retries */

s32 e1000_init_mbx_params_vf(struct e1000_hw *);
u32 e1000_read_v2p_mailbox(struct e1000_hw *);

#ifdef __cplusplus
}
#endif

#endif	/* _IGBVF_MBX_H_ */
