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
 *  Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copyright (c) 2010 Mellanox Technologies. All rights reserved.
 */



#ifndef	_MCXE_H
#define	_MCXE_H

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/debug.h>
#include <sys/stropts.h>
#include <sys/stream.h>
#include <sys/strsun.h>
#include <sys/strlog.h>
#include <sys/kmem.h>
#include <sys/stat.h>
#include <sys/kstat.h>
#include <sys/modctl.h>
#include <sys/errno.h>
#include <sys/dlpi.h>
#include <sys/mac_provider.h>
#include <sys/mac_ether.h>
#include <sys/vlan.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/pci.h>
#include <sys/pcie.h>
#include <sys/sdt.h>
#include <sys/ethernet.h>
#include <sys/pattr.h>
#include <sys/strsubr.h>
#include <sys/netlb.h>
#include <sys/random.h>
#include <inet/common.h>
#include <inet/tcp.h>
#include <inet/ip.h>
#include <inet/mi.h>
#include <inet/nd.h>
#include <sys/bitmap.h>
#include <sys/ddifm.h>
#include <sys/fm/protocol.h>
#include <sys/fm/util.h>
#include <sys/fm/io/ddi.h>
#include <sys/file.h>
#include <sys/open.h>
#include <sys/pci_cap.h>
#include <sys/policy.h>
#include <sys/param.h>

#include <io/mcxnex/mcxnex.h>

#define	MCXE_MOD		1

#define	MCXE_MODULE_NAME	"mcxe"	/* module name */


#define	MCXE_ETH_ADDR_LEN	6

#define	MCXE_IPHDR_ALIGN_ROOM	6

#define	MCXE_DEFAULT_MTU	ETHERMTU
#define	MCXE_MAX_MTU		9000

#define	MCXE_MAX_PORT_NUM	0x2
	/* each CX2 chip has maximum of two ports */

#define	PROP_DEFAULT_MTU	"default_mtu"

struct mcxe_hw_stats {
	/* Received frames with a length of 64 octets */
	uint64_t		R64_prio_0;
	uint64_t		R64_prio_1;
	uint64_t		R64_prio_2;
	uint64_t		R64_prio_3;
	uint64_t		R64_prio_4;
	uint64_t		R64_prio_5;
	uint64_t		R64_prio_6;
	uint64_t		R64_prio_7;
	uint64_t		R64_novlan;
	/* Received frames with a length of 127 octets */
	uint64_t		R127_prio_0;
	uint64_t		R127_prio_1;
	uint64_t		R127_prio_2;
	uint64_t		R127_prio_3;
	uint64_t		R127_prio_4;
	uint64_t		R127_prio_5;
	uint64_t		R127_prio_6;
	uint64_t		R127_prio_7;
	uint64_t		R127_novlan;
	/* Received frames with a length of 255 octets */
	uint64_t		R255_prio_0;
	uint64_t		R255_prio_1;
	uint64_t		R255_prio_2;
	uint64_t		R255_prio_3;
	uint64_t		R255_prio_4;
	uint64_t		R255_prio_5;
	uint64_t		R255_prio_6;
	uint64_t		R255_prio_7;
	uint64_t		R255_novlan;
	/* Received frames with a length of 511 octets */
	uint64_t		R511_prio_0;
	uint64_t		R511_prio_1;
	uint64_t		R511_prio_2;
	uint64_t		R511_prio_3;
	uint64_t		R511_prio_4;
	uint64_t		R511_prio_5;
	uint64_t		R511_prio_6;
	uint64_t		R511_prio_7;
	uint64_t		R511_novlan;
	/* Received frames with a length of 1023 octets */
	uint64_t		R1023_prio_0;
	uint64_t		R1023_prio_1;
	uint64_t		R1023_prio_2;
	uint64_t		R1023_prio_3;
	uint64_t		R1023_prio_4;
	uint64_t		R1023_prio_5;
	uint64_t		R1023_prio_6;
	uint64_t		R1023_prio_7;
	uint64_t		R1023_novlan;
	/* Received frames with a length of 1518 octets */
	uint64_t		R1518_prio_0;
	uint64_t		R1518_prio_1;
	uint64_t		R1518_prio_2;
	uint64_t		R1518_prio_3;
	uint64_t		R1518_prio_4;
	uint64_t		R1518_prio_5;
	uint64_t		R1518_prio_6;
	uint64_t		R1518_prio_7;
	uint64_t		R1518_novlan;
	/* Received frames with a length of 1522 octets */
	uint64_t		R1522_prio_0;
	uint64_t		R1522_prio_1;
	uint64_t		R1522_prio_2;
	uint64_t		R1522_prio_3;
	uint64_t		R1522_prio_4;
	uint64_t		R1522_prio_5;
	uint64_t		R1522_prio_6;
	uint64_t		R1522_prio_7;
	uint64_t		R1522_novlan;
	/* Received frames with a length of 1548 octets */
	uint64_t		R1548_prio_0;
	uint64_t		R1548_prio_1;
	uint64_t		R1548_prio_2;
	uint64_t		R1548_prio_3;
	uint64_t		R1548_prio_4;
	uint64_t		R1548_prio_5;
	uint64_t		R1548_prio_6;
	uint64_t		R1548_prio_7;
	uint64_t		R1548_novlan;
	/* Received frames with a length of 1548 < octets < MTU */
	uint64_t		R2MTU_prio_0;
	uint64_t		R2MTU_prio_1;
	uint64_t		R2MTU_prio_2;
	uint64_t		R2MTU_prio_3;
	uint64_t		R2MTU_prio_4;
	uint64_t		R2MTU_prio_5;
	uint64_t		R2MTU_prio_6;
	uint64_t		R2MTU_prio_7;
	uint64_t		R2MTU_novlan;
	/* Received frames with a length of MTU < octets and good CRC */
	uint64_t		RGIANT_prio_0;
	uint64_t		RGIANT_prio_1;
	uint64_t		RGIANT_prio_2;
	uint64_t		RGIANT_prio_3;
	uint64_t		RGIANT_prio_4;
	uint64_t		RGIANT_prio_5;
	uint64_t		RGIANT_prio_6;
	uint64_t		RGIANT_prio_7;
	uint64_t		RGIANT_novlan;
	/* Received broadcast frames with good CRC */
	uint64_t		RBCAST_prio_0;
	uint64_t		RBCAST_prio_1;
	uint64_t		RBCAST_prio_2;
	uint64_t		RBCAST_prio_3;
	uint64_t		RBCAST_prio_4;
	uint64_t		RBCAST_prio_5;
	uint64_t		RBCAST_prio_6;
	uint64_t		RBCAST_prio_7;
	uint64_t		RBCAST_novlan;
	/* Received multicast frames with good CRC */
	uint64_t		MCAST_prio_0;
	uint64_t		MCAST_prio_1;
	uint64_t		MCAST_prio_2;
	uint64_t		MCAST_prio_3;
	uint64_t		MCAST_prio_4;
	uint64_t		MCAST_prio_5;
	uint64_t		MCAST_prio_6;
	uint64_t		MCAST_prio_7;
	uint64_t		MCAST_novlan;
	/* Received unicast not short or GIANT frames with good CRC */
	uint64_t		RTOTG_prio_0;
	uint64_t		RTOTG_prio_1;
	uint64_t		RTOTG_prio_2;
	uint64_t		RTOTG_prio_3;
	uint64_t		RTOTG_prio_4;
	uint64_t		RTOTG_prio_5;
	uint64_t		RTOTG_prio_6;
	uint64_t		RTOTG_prio_7;
	uint64_t		RTOTG_novlan;

	/* Count of total octets of received frames, includes framing chars */
	uint64_t		RTTLOCT_prio_0;

	/*
	 * Count of total octets of received frames, not including
	 * framing characters
	 */
	uint64_t		RTTLOCT_NOFRM_prio_0;
	/*
	 * Count of Total number of octets received
	 * (only for frames without errors)
	 */
	uint64_t		ROCT_prio_0;

	uint64_t		RTTLOCT_prio_1;
	uint64_t		RTTLOCT_NOFRM_prio_1;
	uint64_t		ROCT_prio_1;

	uint64_t		RTTLOCT_prio_2;
	uint64_t		RTTLOCT_NOFRM_prio_2;
	uint64_t		ROCT_prio_2;

	uint64_t		RTTLOCT_prio_3;
	uint64_t		RTTLOCT_NOFRM_prio_3;
	uint64_t		ROCT_prio_3;

	uint64_t		RTTLOCT_prio_4;
	uint64_t		RTTLOCT_NOFRM_prio_4;
	uint64_t		ROCT_prio_4;

	uint64_t		RTTLOCT_prio_5;
	uint64_t		RTTLOCT_NOFRM_prio_5;
	uint64_t		ROCT_prio_5;

	uint64_t		RTTLOCT_prio_6;
	uint64_t		RTTLOCT_NOFRM_prio_6;
	uint64_t		ROCT_prio_6;

	uint64_t		RTTLOCT_prio_7;
	uint64_t		RTTLOCT_NOFRM_prio_7;
	uint64_t		ROCT_prio_7;

	uint64_t		RTTLOCT_novlan;
	uint64_t		RTTLOCT_NOFRM_novlan;
	uint64_t		ROCT_novlan;

	/* Count of Total received frames including bad frames */
	uint64_t		RTOT_prio_0;
	/* Count of  Total num of received frames with 802.1Q encapsulation */
	uint64_t		R1Q_prio_0;
	uint64_t		reserved1;

	uint64_t		RTOT_prio_1;
	uint64_t		R1Q_prio_1;
	uint64_t		reserved2;

	uint64_t		RTOT_prio_2;
	uint64_t		R1Q_prio_2;
	uint64_t		reserved3;

	uint64_t		RTOT_prio_3;
	uint64_t		R1Q_prio_3;
	uint64_t		reserved4;

	uint64_t		RTOT_prio_4;
	uint64_t		R1Q_prio_4;
	uint64_t		reserved5;

	uint64_t		RTOT_prio_5;
	uint64_t		R1Q_prio_5;
	uint64_t		reserved6;

	uint64_t		RTOT_prio_6;
	uint64_t		R1Q_prio_6;
	uint64_t		reserved7;

	uint64_t		RTOT_prio_7;
	uint64_t		R1Q_prio_7;
	uint64_t		reserved8;

	uint64_t		RTOT_novlan;
	uint64_t		R1Q_novlan;
	uint64_t		reserved9;

	/* Total number of Successfully Received Control Frames */
	uint64_t		RCNTL;
	uint64_t		reserved10;
	uint64_t		reserved11;
	uint64_t		reserved12;
	/*
	 * Count of received frames with a length/type field  value between 46
	 * (42 for VLANtagged frames) and 1500 (also 1500 for VLAN-tagged
	 * frames),
	 * inclusive
	 */
	uint64_t		RInRangeLengthErr;
	/*
	 * Count of received frames with length/type field between 1501 and
	 * 1535 decimal, inclusive
	 *
	 */
	uint64_t		ROutRangeLengthErr;
	/*
	 * Count of received frames that are longer than max allowed size for
	 * 802.3 frames (1518/1522)
	 *
	 */
	uint64_t		RFrmTooLong;
	/* Count frames received with PCS error */
	uint64_t		PCS;

	/* Transmit frames with a length of 64 octets */
	uint64_t		T64_prio_0;
	uint64_t		T64_prio_1;
	uint64_t		T64_prio_2;
	uint64_t		T64_prio_3;
	uint64_t		T64_prio_4;
	uint64_t		T64_prio_5;
	uint64_t		T64_prio_6;
	uint64_t		T64_prio_7;
	uint64_t		T64_novlan;
	uint64_t		T64_loopbk;
	/* Transmit frames with a length of 65 to 127 octets. */
	uint64_t		T127_prio_0;
	uint64_t		T127_prio_1;
	uint64_t		T127_prio_2;
	uint64_t		T127_prio_3;
	uint64_t		T127_prio_4;
	uint64_t		T127_prio_5;
	uint64_t		T127_prio_6;
	uint64_t		T127_prio_7;
	uint64_t		T127_novlan;
	uint64_t		T127_loopbk;
	/* Transmit frames with a length of 128 to 255 octets */
	uint64_t		T255_prio_0;
	uint64_t		T255_prio_1;
	uint64_t		T255_prio_2;
	uint64_t		T255_prio_3;
	uint64_t		T255_prio_4;
	uint64_t		T255_prio_5;
	uint64_t		T255_prio_6;
	uint64_t		T255_prio_7;
	uint64_t		T255_novlan;
	uint64_t		T255_loopbk;
	/* Transmit frames with a length of 256 to 511 octets */
	uint64_t		T511_prio_0;
	uint64_t		T511_prio_1;
	uint64_t		T511_prio_2;
	uint64_t		T511_prio_3;
	uint64_t		T511_prio_4;
	uint64_t		T511_prio_5;
	uint64_t		T511_prio_6;
	uint64_t		T511_prio_7;
	uint64_t		T511_novlan;
	uint64_t		T511_loopbk;
	/* Transmit frames with a length of 512 to 1023 octets */
	uint64_t		T1023_prio_0;
	uint64_t		T1023_prio_1;
	uint64_t		T1023_prio_2;
	uint64_t		T1023_prio_3;
	uint64_t		T1023_prio_4;
	uint64_t		T1023_prio_5;
	uint64_t		T1023_prio_6;
	uint64_t		T1023_prio_7;
	uint64_t		T1023_novlan;
	uint64_t		T1023_loopbk;
	/* Transmit frames with a length of 1024 to 1518 octets */
	uint64_t		T1518_prio_0;
	uint64_t		T1518_prio_1;
	uint64_t		T1518_prio_2;
	uint64_t		T1518_prio_3;
	uint64_t		T1518_prio_4;
	uint64_t		T1518_prio_5;
	uint64_t		T1518_prio_6;
	uint64_t		T1518_prio_7;
	uint64_t		T1518_novlan;
	uint64_t		T1518_loopbk;
	/* Counts transmit frames with a length of 1519 to 1522 bytes */
	uint64_t		T1522_prio_0;
	uint64_t		T1522_prio_1;
	uint64_t		T1522_prio_2;
	uint64_t		T1522_prio_3;
	uint64_t		T1522_prio_4;
	uint64_t		T1522_prio_5;
	uint64_t		T1522_prio_6;
	uint64_t		T1522_prio_7;
	uint64_t		T1522_novlan;
	uint64_t		T1522_loopbk;
	/* Transmit frames with a length of 1523 to 1548 octets */
	uint64_t		T1548_prio_0;
	uint64_t		T1548_prio_1;
	uint64_t		T1548_prio_2;
	uint64_t		T1548_prio_3;
	uint64_t		T1548_prio_4;
	uint64_t		T1548_prio_5;
	uint64_t		T1548_prio_6;
	uint64_t		T1548_prio_7;
	uint64_t		T1548_novlan;
	uint64_t		T1548_loopbk;
	/* Counts transmit frames with a length of 1549 to MTU bytes */
	uint64_t		T2MTU_prio_0;
	uint64_t		T2MTU_prio_1;
	uint64_t		T2MTU_prio_2;
	uint64_t		T2MTU_prio_3;
	uint64_t		T2MTU_prio_4;
	uint64_t		T2MTU_prio_5;
	uint64_t		T2MTU_prio_6;
	uint64_t		T2MTU_prio_7;
	uint64_t		T2MTU_novlan;
	uint64_t		T2MTU_loopbk;
	/* Transmit frames with length larger than MTU octets and a good CRC */
	uint64_t		TGIANT_prio_0;
	uint64_t		TGIANT_prio_1;
	uint64_t		TGIANT_prio_2;
	uint64_t		TGIANT_prio_3;
	uint64_t		TGIANT_prio_4;
	uint64_t		TGIANT_prio_5;
	uint64_t		TGIANT_prio_6;
	uint64_t		TGIANT_prio_7;
	uint64_t		TGIANT_novlan;
	uint64_t		TGIANT_loopbk;
	/* Transmit broadcast frames with a good CRC */
	uint64_t		TBCAST_prio_0;
	uint64_t		TBCAST_prio_1;
	uint64_t		TBCAST_prio_2;
	uint64_t		TBCAST_prio_3;
	uint64_t		TBCAST_prio_4;
	uint64_t		TBCAST_prio_5;
	uint64_t		TBCAST_prio_6;
	uint64_t		TBCAST_prio_7;
	uint64_t		TBCAST_novlan;
	uint64_t		TBCAST_loopbk;
	/* Transmit multicast frames with a good CRC */
	uint64_t		TMCAST_prio_0;
	uint64_t		TMCAST_prio_1;
	uint64_t		TMCAST_prio_2;
	uint64_t		TMCAST_prio_3;
	uint64_t		TMCAST_prio_4;
	uint64_t		TMCAST_prio_5;
	uint64_t		TMCAST_prio_6;
	uint64_t		TMCAST_prio_7;
	uint64_t		TMCAST_novlan;
	uint64_t		TMCAST_loopbk;
	/* Transmit good frames that are neither broadcast nor multicast */
	uint64_t		TTOTG_prio_0;
	uint64_t		TTOTG_prio_1;
	uint64_t		TTOTG_prio_2;
	uint64_t		TTOTG_prio_3;
	uint64_t		TTOTG_prio_4;
	uint64_t		TTOTG_prio_5;
	uint64_t		TTOTG_prio_6;
	uint64_t		TTOTG_prio_7;
	uint64_t		TTOTG_novlan;
	uint64_t		TTOTG_loopbk;

	/* total octets of transmitted frames, including framing characters */
	uint64_t		TTTLOCT_prio_0;
	/* total octets of transmitted frames, not including framing chars */
	uint64_t		TTTLOCT_NOFRM_prio_0;
	/* ifOutOctets */
	uint64_t		TOCT_prio_0;

	uint64_t		TTTLOCT_prio_1;
	uint64_t		TTTLOCT_NOFRM_prio_1;
	uint64_t		TOCT_prio_1;

	uint64_t		TTTLOCT_prio_2;
	uint64_t		TTTLOCT_NOFRM_prio_2;
	uint64_t		TOCT_prio_2;

	uint64_t		TTTLOCT_prio_3;
	uint64_t		TTTLOCT_NOFRM_prio_3;
	uint64_t		TOCT_prio_3;

	uint64_t		TTTLOCT_prio_4;
	uint64_t		TTTLOCT_NOFRM_prio_4;
	uint64_t		TOCT_prio_4;

	uint64_t		TTTLOCT_prio_5;
	uint64_t		TTTLOCT_NOFRM_prio_5;
	uint64_t		TOCT_prio_5;

	uint64_t		TTTLOCT_prio_6;
	uint64_t		TTTLOCT_NOFRM_prio_6;
	uint64_t		TOCT_prio_6;

	uint64_t		TTTLOCT_prio_7;
	uint64_t		TTTLOCT_NOFRM_prio_7;
	uint64_t		TOCT_prio_7;

	uint64_t		TTTLOCT_novlan;
	uint64_t		TTTLOCT_NOFRM_novlan;
	uint64_t		TOCT_novlan;

	uint64_t		TTTLOCT_loopbk;
	uint64_t		TTTLOCT_NOFRM_loopbk;
	uint64_t		TOCT_loopbk;

	/* Total frames transmitted with a good CRC that are not aborted  */
	uint64_t		TTOT_prio_0;
	/* Total number of frames transmitted with 802.1Q encapsulation */
	uint64_t		T1Q_prio_0;
	uint64_t		reserved13;

	uint64_t		TTOT_prio_1;
	uint64_t		T1Q_prio_1;
	uint64_t		reserved14;

	uint64_t		TTOT_prio_2;
	uint64_t		T1Q_prio_2;
	uint64_t		reserved15;

	uint64_t		TTOT_prio_3;
	uint64_t		T1Q_prio_3;
	uint64_t		reserved16;

	uint64_t		TTOT_prio_4;
	uint64_t		T1Q_prio_4;
	uint64_t		reserved17;

	uint64_t		TTOT_prio_5;
	uint64_t		T1Q_prio_5;
	uint64_t		reserved18;

	uint64_t		TTOT_prio_6;
	uint64_t		T1Q_prio_6;
	uint64_t		reserved19;

	uint64_t		TTOT_prio_7;
	uint64_t		T1Q_prio_7;
	uint64_t		reserved20;

	uint64_t		TTOT_novlan;
	uint64_t		T1Q_novlan;
	uint64_t		reserved21;

	uint64_t		TTOT_loopbk;
	uint64_t		T1Q_loopbk;
	uint64_t		reserved22;

#ifdef _LITTLE_ENDIAN
	/* Received frames longer than MTU octets and a bad CRC */
	uint32_t		RJBBR;
	/*
	 * Received frames with a bad CRC that are not runts,
	 * jabbers, or alignment errors
	 */
	uint32_t		RCRC;
	/*
	 * Received frames with SFD with a length of less than 64
	 * octets and a bad CRC
	 */
	uint32_t		RRUNT;
	/* Received frames with a length less than 64 octets and a good CRC */
	uint32_t		RSHORT;
	/* Total Number of Received Packets Dropped */
	uint32_t		RDROP;
	/* Drop due to overflow  */
	uint32_t		RdropOvflw;
	/* Drop due to overflow */
	uint32_t		RdropLength;
	/*
	 * Total of good frames. Does not include frames received with
	 * frame-too-long, FCS, or length errors
	 */
	uint32_t		RTOTFRMS;
	/* Total dropped Xmited packets */
	uint32_t		TDROP;
	uint32_t		reserved23;
#else /* BIG ENDIAN */
	uint32_t		RCRC;
	uint32_t		RJBBR;
	uint32_t		RSHORT;
	uint32_t		RRUNT;
	uint32_t		RdropOvflw;
	uint32_t		RDROP;
	uint32_t		RTOTFRMS;
	uint32_t		RdropLength;
	uint32_t		reserved23;
	uint32_t		TDROP;
#endif
};

#ifdef _LITTLE_ENDIAN
typedef struct mcxe_hw_set_port_gen_s {
	uint32_t	flags		:3;
	uint32_t			:29;

	uint32_t	mtu		:16;
	uint32_t			:16;

	uint32_t			:16;
	uint32_t	pfctx		:8;
	uint32_t			:7;
	uint32_t	pptx		:1;

	uint32_t			:16;
	uint32_t	pfcrx		:8;
	uint32_t			:7;
	uint32_t	pprx		:1;

	uint32_t	rsrd0[4];
} mcxe_hw_set_port_gen_t;
#else /* BIG ENDIAN */
typedef struct mcxe_hw_set_port_gen_s {
	uint32_t			:16;
	uint32_t	mtu		:16;

	uint32_t			:29;
	uint32_t	flags		:3;

	uint32_t	pprx		:1;
	uint32_t			:7;
	uint32_t	pfcrx		:8;
	uint32_t			:16;

	uint32_t	pptx		:1;
	uint32_t			:7;
	uint32_t	pfctx		:8;
	uint32_t			:16;

	uint32_t	rsrd0[4];
} mcxe_hw_set_port_gen_t;
#endif

#ifdef _LITTLE_ENDIAN
typedef struct mcxe_hw_set_port_rqp_calc_cx_s {
	uint32_t	base_qpn;
	uint32_t	n_mvp;

	uint32_t	mac_miss	:8;
	uint32_t			:24;

	uint32_t	vlan_miss	:7;
	uint32_t			:8;
	uint32_t	intra_vlan_miss	:1;
	uint32_t	no_vlan		:7;
	uint32_t			:8;
	uint32_t	intra_no_vlan	:1;

	uint32_t	no_vlan_prio	:3;
	uint32_t			:29;

	uint32_t	promisc;

	uint32_t	mcast;
	uint32_t	rsrd0;
} mcxe_hw_set_port_rqp_calc_cx_t;
#else /* BIG ENDIAN */
typedef struct mcxe_hw_set_port_rqp_calc_cx_s {
	uint32_t	n_mvp;
	uint32_t	base_qpn;

	uint32_t	intra_no_vlan	:1;
	uint32_t			:8;
	uint32_t	no_vlan		:7;
	uint32_t	intra_vlan_miss	:1;
	uint32_t			:8;
	uint32_t	vlan_miss	:7;

	uint32_t			:24;
	uint32_t	mac_miss	:8;

	uint32_t	promisc;

	uint32_t			:29;
	uint32_t	no_vlan_prio	:3;

	uint32_t	rsrd0;
	uint32_t	mcast;
} mcxe_hw_set_port_rqp_calc_cx_t;
#endif

enum {
	/* set port opcode modifiers */
	MCXE_SET_PORT_GENERAL = 0x0,
	MCXE_SET_PORT_RQP_CALC = 0x1,
	MCXE_SET_PORT_MAC_TABLE = 0x2,
	MCXE_SET_PORT_VLAN_TABLE = 0x3,
	MCXE_SET_PORT_PRIO_MAP = 0x4,
};

#define	SET_PORT_GEN_ALL_VALID	0x7
#define	SET_PORT_PROMISC_SHIFT	31

#define	MCXE_MAX_MAC_NUM	128
#define	MCXE_MAC_TABLE_SIZE	(MCXE_MAX_MAC_NUM << 3)
#define	MCXE_MAC_VALID_SHIFT	63

#define	MCXE_MAX_VLAN_NUM	128
#define	MCXE_VLAN_TABLE_SIZE	(MLX4_MAX_VLAN_NUM << 2)

typedef struct mcxe_vlan_table {
	uint32_t	entries[MCXE_MAX_VLAN_NUM];
} mcxe_vlan_table_t;

enum {
	MCXE_NO_VLAN_IDX = 0,
	MCXE_VLAN_MISS_IDX,
	MCXE_VLAN_REGULAR
};

#define	VLAN_FLTR_SIZE		128
typedef struct mcxe_hw_vlan_fltr_s {
	uint32_t	entry[VLAN_FLTR_SIZE];
} mcxe_hw_vlan_fltr_t;


/*
 * DMA attributes for transmit.
 */
#define	MCXE_MAX_COOKIE		18
#define	MCXE_MAX_FRAGS		48

#define	MCXE_RX_SLOTS		2048
#define	MCXE_RX_SLOTS_MIN	(MCXE_RX_SLOTS/2)
#define	MCXE_TX_SLOTS		2048

#define	MCXE_RX_RING_NUM	1
#define	MCXE_TX_RING_NUM	1
#define	MCXE_RX_RING_MAX	8
#define	MCXE_TX_RING_MAX	8

#define	MCXE_LSO_MAXLEN		65535

typedef struct mcxe_queue_item {
	struct mcxe_queue_item	*next;
	void			*item;
} mcxe_queue_item_t;

typedef struct mcxe_queue {
	mcxe_queue_item_t	*head;
	uint32_t		count;
	kmutex_t		*lock;
} mcxe_queue_t;

typedef struct dma_buffer {
	caddr_t			address;	/* Virtual address */
	uint64_t		dma_address;	/* DMA (Hardware) address */
	ddi_acc_handle_t	acc_handle;	/* Data access handle */
	ddi_dma_handle_t	dma_handle;	/* DMA handle */
	size_t			size;		/* Buffer size */
	size_t			len;		/* Data length in the buffer */
} dma_buffer_t;

typedef struct mcxe_txbuf {
	dma_buffer_t		dma_buf;
	uint32_t		copy_len;
	int			num_dma_seg;
	ddi_dma_handle_t	*dma_handle_table;
	int			num_sge;
	ibt_wr_ds_t		*dma_frags;
	mblk_t			*mp;
} mcxe_txbuf_t;

typedef struct mcxe_tx_ring {
	struct mcxe_port	*port;
	mcxnex_cqhdl_t		tx_cq;
	mcxnex_qphdl_t		tx_qp;

	/*
	 * Tx buffer queue
	 */
	mcxe_queue_t		txbuf_queue;
	mcxe_queue_t		freetxbuf_queue;
	mcxe_queue_t		*txbuf_push_queue;
	mcxe_queue_t		*txbuf_pop_queue;
	kmutex_t		txbuf_lock;
	kmutex_t		freetxbuf_lock;
	kmutex_t		tc_lock;	/* serialize recycle	*/
	mcxe_queue_item_t	*txbuf_head;
	mcxe_txbuf_t		*txbuf;
	uint32_t		tx_buffers;
	uint32_t		tx_buffers_low;

	uint32_t		tx_free;	/* # of pkt buffer available */
	uint32_t		tx_resched_needed;
	uint64_t		tx_resched;
	uint64_t		tx_bcopy;
	uint64_t		tx_nobd;
	uint64_t		tx_nobuf;
	uint64_t		tx_bindfail;
	uint64_t		tx_bindexceed;
	uint64_t		tx_alloc_fail;
	uint64_t		tx_pullup;
	uint64_t		tx_drop;
} mcxe_tx_ring_t;

typedef struct mcxe_rxbuf {
	struct mcxe_rx_ring	*rx_ring;
	dma_buffer_t		dma_buf;
	mblk_t			*mp;
	uint32_t		ref_cnt;
	frtn_t			rxb_free_rtn;
	uint32_t		index;
	uint32_t		flag;
} mcxe_rxbuf_t;

typedef struct mcxe_rx_ring {
	struct mcxe_port	*port;	/* Pointer to mcxe port struct */
	kmutex_t		rx_lock;
	krwlock_t		rc_rwlock;
	mcxnex_cqhdl_t		rx_cq;
	mcxnex_qphdl_t		rx_qp;
	uint64_t		rx_bind;
	uint64_t		rx_bcopy;
	uint64_t		rx_postfail;
	uint64_t		rx_allocfail;
	uint32_t		rx_free;
	struct mcxe_rxbuf	*rxbuf;
	uint32_t		rxb_pending;
} mcxe_rx_ring_t;

#define	MCXE_IF_STARTED		0x01
#define	MCXE_IF_ERROR		0x02

#define	MCXE_RXB_STOPPED	0x01
#define	MCXE_RXB_REUSED		0x02
#define	MCXE_RXB_STARTED	0x04

#define	MCXE_MAX_MCAST_NUM	0x1000

enum {
	MCXE_PRG_ALLOC_SOFTSTAT = 0x1,
	MCXE_PRG_MUTEX = 0x2,
	MCXE_PRG_ALLOC_RINGS = 0x4,
	MCXE_PRG_ALLOC_RSRC = 0x8,
	MCXE_PRG_MAC_REGISTER = 0x10,
	MCXE_PRG_SOFT_INTR = 0x20
};

#define	MCXE_HW_SPEED_MASK		0x3
#define	MCXE_HW_1G_SPEED		0x2
#define	MCXE_HW_LINK_UP_MASK		0x80
#define	MCXE_CYCLIC_PERIOD		(1000000000)	/* 1s */

#ifdef _LITTLE_ENDIAN
struct mcxe_hw_query_port {
	uint16_t		reserved1;
	uint8_t			link_speed;
	uint8_t			reserved2;

	uint16_t		mtu;
	uint8_t			reserved3;
	uint8_t			link_up;

	uint32_t		reserved4[2];

	uint64_t		mac;
};
#else /* BIG ENDIAN */
struct mcxe_hw_query_port {
	uint8_t			link_up;
	uint8_t			reserved3;
	uint16_t		mtu;

	uint8_t			reserved2;
	uint8_t			link_speed;
	uint16_t		reserved1;

	uint32_t		reserved4[2];

	uint64_t		mac;
}
#endif

typedef struct mcxe_port {
	dev_info_t		*devinfo;
	int			instance;
	uint32_t		if_state;	/* interface GLD state */
	uint32_t		progress;	/* driver attach stages */

	/* port global locks */
	kmutex_t		port_lock;
	krwlock_t		port_rwlock;

	mac_handle_t		mac_hdl;
	mcxnex_state_t		*mcxnex_state;	/* parent mcxnex pointer */
	uint32_t		phys_port_num;
	uint32_t		rxb_pending;

	/*
	 * Receive Rings
	 */
	struct mcxe_rx_ring	*rx_rings;	/* Array of rx rings */
	uint32_t		num_rx_rings;	/* Number of rx rings in use */
	uint32_t		rx_ring_size;	/* rx ring QP size */
	uint32_t		rx_buff_size;	/* rx buffer size */
	uint32_t		rx_copy_thresh;
	uint32_t		default_mtu;
	uint32_t		max_frame_size;

	/*
	 * Transmit Rings
	 */
	struct mcxe_tx_ring	*tx_rings;	/* Array of tx rings */
	uint32_t		num_tx_rings;	/* Number of tx rings in use */
	uint32_t		tx_ring_next;	/* next tx ring to use */
	uint32_t		tx_ring_size;	/* tx ring QP size */
	uint32_t		tx_buff_size;	/* tx buffer size */
	uint32_t		max_tx_frags;
	uint32_t		tx_copy_thresh;

	/* soft intr for link handling */
	kmutex_t		link_softintr_lock;
	int			link_softintr_flag;
	ddi_softintr_t		link_softintr_id;
	ddi_periodic_t		periodic_id; /* for link check timer func */

	/* multicast table */
	uint32_t		mcast_count;
	struct ether_addr	mcast_table[MCXE_MAX_MCAST_NUM];

	/* MAC table */
	uint64_t		hw_mac;	/* primary MAC address */
	uint64_t		ucast_table[MCXE_MAX_MAC_NUM];

	/* vlan table */
	mcxe_vlan_table_t	vlan_table;
	mcxe_hw_vlan_fltr_t	vlan_fltr_table;

	/* port h/w statistics */
	struct mcxe_hw_stats	hw_stats;

	/* driver statistics */
	kstat_t			*mcxe_kstats;

	/* link state info */
	uint32_t		lp_mode;	/* loopback mode */
	link_state_t		link_state;	/* link status: up/down */
	uint32_t		link_speed;	/* link speed: 10G/1G */
	uint32_t		link_duplex;	/* link dumplex */
	uint32_t		param_en_10000fdx_cap:1,
				param_en_1000fdx_cap:1,
				param_en_100fdx_cap:1,
				param_adv_10000fdx_cap:1,
				param_adv_1000fdx_cap:1,
				param_adv_100fdx_cap:1,
				param_pause_cap:1,
				param_asym_pause_cap:1,
				param_rem_fault:1,
				param_adv_autoneg_cap:1,
				param_adv_pause_cap:1,
				param_adv_asym_pause_cap:1,
				param_adv_rem_fault:1,
				param_lp_10000fdx_cap:1,
				param_lp_1000fdx_cap:1,
				param_lp_100fdx_cap:1,
				param_lp_autoneg_cap:1,
				param_lp_pause_cap:1,
				param_lp_asym_pause_cap:1,
				param_lp_rem_fault:1,
				param_pad_to_32:12;
} mcxe_port_t;


/* mcxe.c */
extern int mcxe_verbose;
extern void mcxe_post_rxb_free(struct mcxe_rxbuf *);

/* mcxe_tx.c */
extern mblk_t *mcxe_tx(void *, mblk_t *);
extern void mcxe_tx_intr_handler(mcxnex_state_t *, mcxnex_cqhdl_t, void *);

/* mcxe_rx.c */
extern void mcxe_rx_intr_handler(mcxnex_state_t *, mcxnex_cqhdl_t, void *);
extern int mcxe_post_recv(struct mcxe_rxbuf *);
extern void mcxe_rxb_free_cb(caddr_t);

#endif /* _MCXE_H */
