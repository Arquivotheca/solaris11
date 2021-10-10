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
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_MAC_FLOW_H
#define	_MAC_FLOW_H

/*
 * Main structure describing a flow of packets, for classification use
 */

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <netinet/in.h>		/* for IPPROTO_* constants */
#include <sys/ethernet.h>
#include <sys/modhash.h>
#include <sys/zone.h>

#define	MAX_RINGS_PER_GROUP	256
#define	MAX_GROUPS_PER_NIC	128

/*
 * MAXFLOWNAMELEN defines the longest possible permitted flow name, plus the
 * zone name length (ZONENAME_MAX), including the terminating NUL.
 */
#define	MAXFLOWNAMELEN		128 + ZONENAME_MAX

/* need to use MAXMACADDRLEN from dld.h instead of this one */
#define	MAXMACADDR		20

/* Bit-mask for the selectors carried in the flow descriptor */
typedef	uint64_t		flow_mask_t;

#define	FLOW_LINK_DST		0x00000001	/* Destination MAC addr */
#define	FLOW_LINK_SRC		0x00000002	/* Source MAC address */
#define	FLOW_LINK_VID		0x00000004	/* VLAN ID */
#define	FLOW_LINK_SAP		0x00000008	/* SAP value */

#define	FLOW_IP_VERSION		0x00000010	/* V4 or V6 */
#define	FLOW_IP_PROTOCOL	0x00000020	/* Protocol type */
#define	FLOW_IP_LOCAL		0x00000040	/* Local address */
#define	FLOW_IP_REMOTE		0x00000080	/* Remote address */
#define	FLOW_IP_DSFIELD		0x00000100	/* DSfield value */

#define	FLOW_ULP_PORT_LOCAL	0x00001000	/* ULP local port */
#define	FLOW_ULP_PORT_REMOTE	0x00002000	/* ULP remote port */

#if _LONG_LONG_ALIGNMENT == 8 && _LONG_LONG_ALIGNMENT_32 == 4
#pragma pack(4)
#endif

typedef struct flow_desc_s {
	flow_mask_t			fd_mask;
	uint32_t			fd_mac_len;
	uint8_t				fd_dst_mac[MAXMACADDR];
	uint8_t				fd_src_mac[MAXMACADDR];
	uint16_t			fd_vid;
	uint32_t			fd_sap;
	uint8_t				fd_ipversion;
	uint8_t				fd_protocol;
	in6_addr_t			fd_local_addr;
	in6_addr_t			fd_local_netmask;
	in6_addr_t			fd_remote_addr;
	in6_addr_t			fd_remote_netmask;
	in_port_t			fd_local_port;
	in_port_t			fd_remote_port;
	uint8_t				fd_dsfield;
	uint8_t				fd_dsfield_mask;
	zoneid_t			fd_zoneid;
} flow_desc_t;

#ifdef _KERNEL
/* per zone mac flow table */
typedef struct flow_stack {
	zoneid_t	zoneid;
	mod_hash_t	*flow_hash;
	krwlock_t	flow_tab_lock;
	kmem_cache_t	*flow_cache;
	kmem_cache_t	*flow_tab_cache;
} flow_zone_tab_t;
#endif /* _KERNEL */

#define	MRP_NCPUS	128

typedef struct mac_cpus_props_s {
	uint32_t		mc_ncpus;		/* num of cpus */
	uint32_t		mc_cpus[MRP_NCPUS]; 	/* cpu list */
} mac_cpus_t;

/* Priority values */
typedef enum {
	MPL_LOW,
	MPL_MEDIUM,
	MPL_HIGH,
	MPL_RESET
} mac_priority_level_t;

/* Protection types */
#define	MPT_MACNOSPOOF		0x00000001
#define	MPT_RESTRICTED		0x00000002
#define	MPT_IPNOSPOOF		0x00000004
#define	MPT_DHCPNOSPOOF		0x00000008
#define	MPT_ALL			0x0000000f
#define	MPT_RESET		0xffffffff
#define	MPT_MAXCNT		32
#define	MPT_MAXIPADDR		MPT_MAXCNT
#define	MPT_MAXCID		MPT_MAXCNT
#define	MPT_MAXCIDLEN		256

typedef struct mac_ipaddr_s {
	uint32_t	ip_version;
	in6_addr_t	ip_addr;
} mac_ipaddr_t;

typedef enum {
	CIDFORM_TYPED = 1,
	CIDFORM_HEX,
	CIDFORM_STR
} mac_dhcpcid_form_t;

typedef struct mac_dhcpcid_s {
	uchar_t			dc_id[MPT_MAXCIDLEN];
	uint32_t		dc_len;
	mac_dhcpcid_form_t	dc_form;
} mac_dhcpcid_t;

typedef struct mac_protect_s {
	uint32_t	mp_types;
	uint32_t	mp_ipaddrcnt;
	mac_ipaddr_t	mp_ipaddrs[MPT_MAXIPADDR];
	uint32_t	mp_cidcnt;
	mac_dhcpcid_t	mp_cids[MPT_MAXCID];
} mac_protect_t;

/* The default priority for links */
#define	MPL_LINK_DEFAULT		MPL_HIGH

/* The default priority for flows */
#define	MPL_SUBFLOW_DEFAULT		MPL_MEDIUM

#define	MRP_MAXBW		0x00000001 	/* Limit set */
#define	MRP_CPUS		0x00000002 	/* CPU set */
#define	MRP_CPUS_USERSPEC	0x00000004 	/* CPU from user */
#define	MRP_PRIORITY		0x00000008 	/* Priority set */
#define	MRP_PROTECT		0x00000010	/* Protection set */
#define	MRP_RX_RINGS		0x00000020	/* Rx rings */
#define	MRP_TX_RINGS		0x00000040	/* Tx rings */
#define	MRP_RXRINGS_UNSPEC	0x00000080	/* unspecified rings */
#define	MRP_TXRINGS_UNSPEC	0x00000100	/* unspecified rings */
#define	MRP_PROP_RESET		0x00000200	/* resetting rings */
#define	MRP_POOL		0x00000400	/* CPU pool */
#define	MRP_USRPRI		0x00000800	/* 802.1p user priority */
#define	MRP_RXFANOUT		0x00001000 	/* rxfanout set */

#define	MRP_THROTTLE		MRP_MAXBW

/* 3 levels - low, medium, high */
#define	MRP_PRIORITY_LEVELS		3

/* Special value denoting no bandwidth control */
#define	MRP_MAXBW_RESETVAL		-1ULL

/* Special value denoting no rxfanout configured */
#define	MRP_RXFANOUT_RESETVAL		-1U

#define	MRP_MAXBW_MINVAL		10000

typedef	struct mac_resource_props_s {
	/*
	 * Bit-mask for the network resource control types
	 */
	uint32_t		mrp_mask;
	uint64_t		mrp_maxbw;	/* bandwidth limit in bps */
	mac_priority_level_t	mrp_priority;	/* relative flow priority */
	mac_cpus_t		mrp_cpus;
	uint32_t		mrp_rxfanout;
	mac_protect_t		mrp_protect;
	uint32_t		mrp_nrxrings;
	uint32_t		mrp_ntxrings;
	uint8_t			mrp_usrpri;
	char			mrp_pool[MAXPATHLEN];	/* CPU pool */
} mac_resource_props_t;

#define	mrp_ncpus		mrp_cpus.mc_ncpus
#define	mrp_cpu			mrp_cpus.mc_cpus

#define	MAC_COPY_CPUS(mrp, fmrp) {					\
	int	ncpus;							\
	(fmrp)->mrp_ncpus = (mrp)->mrp_ncpus;				\
	if ((mrp)->mrp_ncpus == 0) {					\
		(fmrp)->mrp_mask &= ~MRP_CPUS;				\
		(fmrp)->mrp_mask &= ~MRP_CPUS_USERSPEC;			\
	} else {							\
		for (ncpus = 0; ncpus < (fmrp)->mrp_ncpus; ncpus++)	\
			(fmrp)->mrp_cpu[ncpus] = (mrp)->mrp_cpu[ncpus];\
		(fmrp)->mrp_mask |= MRP_CPUS;				\
		if ((mrp)->mrp_mask & MRP_CPUS_USERSPEC)		\
			(fmrp)->mrp_mask |= MRP_CPUS_USERSPEC;		\
	}								\
}

#if _LONG_LONG_ALIGNMENT == 8 && _LONG_LONG_ALIGNMENT_32 == 4
#pragma pack()
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _MAC_FLOW_H */
