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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SXGE_MBX_PROTO_H
#define	_SXGE_MBX_PROTO_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SOL Host to EPS Protocol.
 */

/*
 * Header for all mailbox messages.  This header is always the 0th
 * entry of the mailbox payload and per HW specification it is the
 * last entry that is written to the hardware when posting a message
 * in either the outbound or inbound NIU mailboxes.
 *
 * A mailbox payload will consist of this 64-bit header
 * plus upto to seven more 64-bit words.
 */

typedef union _niu_mb_tag {
	uint64_t	tag;
	struct {
#if defined(_BIG_ENDIAN)
		uint16_t	_seq;		/* Sequence Number */
		uint16_t	_request;	/* Request ID */
		uint16_t	_mtype;		/* Message Type */
		uint8_t		_resv;
		uint8_t		_length;	/* Number of 64-bit words. */
#else
		uint8_t		_length;	/* Number of 64-bit words. */
		uint8_t		_resv;
		uint16_t	_mtype;		/* Message Type */
		uint16_t	_request;	/* Request ID */
		uint16_t	_seq;		/* Sequence Number */
#endif
	} _hdr;
} niu_mb_tag_t;

#define	NIU_MB_REQUEST	0x01
#define	NIU_MB_RESPONSE	0x02

#define	mb_request	_hdr._request
#define	mb_type		_hdr._mtype
#define	mb_seq		_hdr._seq
#define	mb_len		_hdr._length

/*
 * Generic Mailbox Message.
 */
typedef struct _niu_mb_msg_t {
	uint64_t	length;
	uint64_t	msg_data[NIU_MB_MAX_LEN];
} niu_mb_msg_t;

/*
 * Capabiities MBox Types.
 */
#define	NIU_MB_GET_CAPAB			0x100
#define	NIU_MB_GET_L2_ADDRESS_CAPABILITIES	(NIU_MB_GET_CAPAB + 0x01)
#define	NIU_MB_GET_TCAM_CAPABILITIES		(NIU_MB_GET_CAPAB + 0x02)

/*
 * L2 Unicast Address Capabilities
 *	Enables host to get the number of L2 entries for unicast and multicast
 *	available to it.
 *	- 64-bit Header
 *	- 64-bit word in response with the number of unicast
 *	  addresses that can be programmed by this function.
 *	- 64-bit word in response with the number of multicast
 *	  addresses that can be programmed by this function.
 */

typedef struct _l2_address_capability {
	niu_mb_tag_t	hdr;
	uint64_t	n_u_addrs;
	uint64_t	n_m_addrs;
	uint64_t	link_speed;
	uint64_t	reserve1;
	uint64_t	reserve2;
	uint64_t	reserve3;
} l2_address_capability_t;

#define	NIU_MB_L2_ADDRESS_CAP_SZ \
	sizeof (l2_address_capability_t) / sizeof (uint64_t)

#define	L2_ADDRESS_RESERVE_BITS			(16)
#define	NIU_MB_PCS_MODE		0x30000
#define	NIU_MB_PCS_MODE_INDEX	4

/*
 * L3/L4 TCAM capabilities
 *	Enables host to get the number of TCAM entries available to it.
 *
 *	- 64-bit Header.
 *	- 64-bit Number of TCAM Entries.
 */
typedef struct _l3l4_tcam_capability {
	niu_mb_tag_t	hdr;
	uint64_t	n_tcam_entries;
	uint64_t	reserve1;
	uint64_t	reserve2;
} l3l4_tcam_capability_t;

/*
 * PFC Mbox Types.
 */
#define	NIU_MB_CLS_OPS				0x200
#define	NIU_MB_L2_ADDRESS_ADD			(NIU_MB_CLS_OPS + 0x0)
#define	NIU_MB_L2_ADDRESS_REMOVE		(NIU_MB_CLS_OPS + 0x1)
#define	NIU_MB_L2_MULTICAST_ADD			(NIU_MB_CLS_OPS + 0x2)
#define	NIU_MB_L2_MULTICAST_REMOVE		(NIU_MB_CLS_OPS + 0x3)
#define	NIU_MB_VLAN_ADD				(NIU_MB_CLS_OPS + 0x4)
#define	NIU_MB_VLAN_REMOVE			(NIU_MB_CLS_OPS + 0x5)
#define	NIU_MB_L3L4_TCAM_ADD			(NIU_MB_CLS_OPS + 0x6)
#define	NIU_MB_L3L4_TCAM_REMOVE			(NIU_MB_CLS_OPS + 0x7)
#define	NIU_MB_RSS_HASH				(NIU_MB_CLS_OPS + 0x8)
#define	NIU_MB_LINK_SPEED			(NIU_MB_CLS_OPS + 0x9)

/*
 * L2 Address Add/Remove
 *
 *	This is used both programming L2 unicast and multicast addresses
 *	by the host.  It will used with NIU_MB_L2_ADDRESS_ADD/REMOVE and
 *	NIU_MB_L2_MULTICAST_ADD/REMOVE.
 *
 *	- 64-bit header
 *	- 64-bit address
 *	- 64-bit mask
 */
typedef struct _l2_address_t {
	niu_mb_tag_t	hdr;
	uint64_t	address;
	uint64_t	mask;
	uint8_t		slot[8];
	uint64_t	reserve1;
	uint64_t	reserve2;
} l2_address_req_t;

#define	NIU_MB_L2_ADDRESS_REQUEST_SZ \
	sizeof (l2_address_req_t) / sizeof (uint64_t)

/*
 * VLAN ID add/remove
 */
typedef struct _vlan_t {
	niu_mb_tag_t	hdr;
	uint64_t	vlan_id;
	uint64_t	reserve1;
	uint64_t	reserve2;
} vlan_req_t;

#define	NIU_MB_VLAN_REQUEST_SZ \
	sizeof (vlan_req_t) / sizeof (uint64_t)

/*
 * For Host Programing of NIU blocks -- primarily used by iodiag
 */
#define	NIU_MB_PIO			0x800
#define	NIU_MB_PIO_READ			(NIU_MB_PIO + 0x0)
#define	NIU_MB_PIO_WRITE		(NIU_MB_PIO + 0x1)

/*
 * PIO Read/Write mailbox message.
 *	Allows for request/response exchange for doing PIO reads and
 *	writes.
 *
 *	- 64-bit header
 *	- 64-bit offset (address for the PIO)
 *	- 64-bit data	(data to be written or read)
 */
typedef struct _niu_mb_pio {
	niu_mb_tag_t	hdr;
	uint64_t	offset;
	uint64_t	data;
	uint64_t	reserve1;
	uint64_t	reserve2;
} niu_mb_pio_t;

#define	NIU_MB_PIO_SZ \
	sizeof (niu_mb_pio_t) / sizeof (uint64_t)

typedef struct _l3_tcam_req_t {
	niu_mb_tag_t	hdr;
	uint64_t	slot;
	uint64_t	key0;
	uint64_t	key1;
	uint64_t	mask0;
	uint64_t	mask1;
	uint64_t	reserve1;
} l3_tcam_req_t;

#define	NIU_MB_L3_TCAM_REQUEST_SZ \
	sizeof	(l3_tcam_req_t) / sizeof (uint64_t)

#ifdef __cplusplus
}
#endif

#endif /* !_SXGE_MBX_PROTO_H */
