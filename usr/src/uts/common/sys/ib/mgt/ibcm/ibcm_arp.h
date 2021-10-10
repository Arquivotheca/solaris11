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
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SYS_IB_MGT_IBCM_IBCM_ARP_H
#define	_SYS_IB_MGT_IBCM_IBCM_ARP_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/ib/mgt/ibcm/ibcm_impl.h>
#include <sys/ib/clients/ibd/ibd.h>
#include <inet/ip2mac.h>
#include <inet/ip6.h>
#include <inet/ip.h>
#include <inet/ip_ire.h>
#include <inet/ip_if.h>
#include <sys/dls.h>

#define	IBCM_H2N_GID(gid) \
{ \
	uint32_t	*ptr; \
	ptr = (uint32_t *)&gid.gid_prefix; \
	gid.gid_prefix = (uint64_t)(((uint64_t)ntohl(ptr[0]) << 32) | \
			(ntohl(ptr[1]))); \
	ptr = (uint32_t *)&gid.gid_guid; \
	gid.gid_guid = (uint64_t)(((uint64_t)ntohl(ptr[0]) << 32) | \
			(ntohl(ptr[1]))); \
}

#define	IBCM_ARP_PR_RT_PENDING		0x01
#define	IBCM_ARP_PR_RESOLVE_PENDING	0x02

/*
 * Path record wait queue node definition
 */
typedef struct ibcm_arp_prwqn {
	struct ibcm_arp_streams_s *ib_str;
	uint8_t			flags;
	ibt_ip_addr_t		usrc_addr;	/* user supplied src address */
	ibt_ip_addr_t		dst_addr;	/* user supplied dest address */
	ibt_ip_addr_t		src_addr;	/* rts's view of src address */
	ibt_ip_addr_t		gateway;	/* rts returned gateway addr */
	ibt_ip_addr_t		netmask;	/* rts returned netmask */
	char			ifname[MAXLINKNAMELEN];
	uint16_t		ifproto;
	ipoib_mac_t		src_mac;
	ipoib_mac_t		dst_mac;
	ib_gid_t		sgid;
	ib_gid_t		dgid;
	ip2mac_id_t		ip2mac_id;
} ibcm_arp_prwqn_t;

typedef struct ibcm_arp_streams_s {
	kmutex_t		lock;
	kcondvar_t		cv;
	int			status;
	boolean_t		done;
	ibcm_arp_prwqn_t	*wqnp;
} ibcm_arp_streams_t;

typedef struct ibcm_ibaddr_s {
	ib_gid_t	sgid;
	ib_gid_t	dgid;
	ibt_ip_addr_t	src_ip;
	char		ifname[MAXLINKNAMELEN];
	uint8_t		src_mismatch;
} ibcm_ibaddr_t;


ibt_status_t ibcm_arp_get_ibaddr(zoneid_t zoneid, ibt_ip_addr_t srcip,
    ibt_ip_addr_t destip, ibcm_ibaddr_t *ibaddrp);
ibt_status_t ibcm_arp_get_ibds(ibt_srcip_attr_t *sattr,
    ibt_srcip_info_t **sip, uint_t *ent);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IB_MGT_IBCM_IBCM_ARP_H */
