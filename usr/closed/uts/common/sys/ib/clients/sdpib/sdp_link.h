/*
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SYS_IB_CLIENTS_SDP_LINK_H
#define	_SYS_IB_CLIENTS_SDP_LINK_H

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef _KERNEL
#include <sys/ib/ib_pkt_hdrs.h>
#include <sys/modhash.h>
#include <sys/ib/clients/ibd/ibd.h>
#include <sys/strsun.h>
#include <sys/strsubr.h>
#include <sys/socket.h>
#include <sys/stat.h>	/* for S_IFCHR */
#include <inet/common.h>
#include <inet/ip.h>
#include <inet/ip_if.h>
#include <inet/ip_ire.h>
#include <inet/ip_rts.h>
#include <sys/dlpi.h>
#include <net/route.h>
#include <inet/ip_ndp.h>

typedef uint64_t sdp_lookup_id_t;

/*
 * Place holder for ipv4 or ipv6 address
 */
typedef struct {
	sa_family_t family;
	union {
		in_addr_t ip4_addr;
		in6_addr_t ip6_addr;
	} un;
} sdp_ipx_addr_t;
#define	ip4addr		un.ip4_addr
#define	ip6addr		un.ip6_addr

/*
 * Path record lookup completion function
 */
typedef int (*sdp_pr_comp_func_t) (sdp_lookup_id_t plid,
    uint32_t status, sdp_ipx_addr_t *src_addr,
    sdp_ipx_addr_t *dst_addr, ibt_path_info_t *path_info,
    ibt_path_attr_t *path_attr, uint8_t hw_port,
    uint16_t pkey, ibt_hca_hdl_t *ca, void *usr_arg);

/*
 * Path record cache node definition
 */
typedef struct sdp_prcn {
	sdp_ipx_addr_t dst_addr;	/* requested address */
	sdp_ipx_addr_t src_addr;
	sdp_ipx_addr_t gateway;	/* gateway to use */
	clock_t last_used_time;	/* last used */
	uint32_t hw_port;	/* source port */
	ibt_hca_hdl_t hca_hdl;	/* hca handle */
	uint16_t pkey;
	ibt_path_info_t path_info;
	ibt_path_attr_t path_attr;
	struct sdp_prcn *next;
	struct sdp_prcn **p_next;
} sdp_prcn_t;

#define	SDP_PR_CACHE_REAPING_AGE	10	/* in seconds */
#define	SDP_PR_CACHE_REAPING_AGE_USECS	(SDP_PR_CACHE_REAPING_AGE * 1000000)

enum {
	SDP_PR_RT_PENDING = 0x01,
	SDP_PR_RESOLVE_PENDING = 0x02
};

typedef struct {
	ib_guid_t hca_guid;
	ibt_hca_hdl_t hca_hdl;
	uint8_t nports;
	int opened;
} sdp_hca_info_t;

/*
 * Path record wait queue node definition
 */
typedef struct sdp_prwqn {
	sdp_lookup_id_t id;		/* lookup id */
	sdp_pr_comp_func_t func;	/* user callback function */
	void *arg;			/* callback function arg */
	timeout_id_t timeout_id;
	uint8_t flags;
	sdp_ipx_addr_t usrc_addr;	/* user supplied src address */
	sdp_ipx_addr_t dst_addr;	/* user supplied dest address */
	sdp_ipx_addr_t src_addr;	/* rts's view  of source address */
	sdp_ipx_addr_t gateway;		/* rts returned gateway address */
	sdp_ipx_addr_t netmask;		/* rts returned netmask */
	char ifname[MAXLINKNAMELEN];
	uint16_t ifproto;
	ipoib_mac_t src_mac;
	ipoib_mac_t dst_mac;
	uint32_t localroute;		/* user option */
	uint32_t bound_dev_if;		/* user option */
	ib_gid_t sgid;
	ib_gid_t dgid;
	ibt_path_info_t path_info;
	ibt_path_attr_t path_attr;
	ibt_hca_hdl_t hca_hdl;
	uint8_t hw_port;
	uint16_t pkey;
	ip2mac_id_t ip2mac_id;
	kcondvar_t ip2mac_cv;
	zoneid_t wqn_zoneid;

	struct sdp_prwqn *next;
	struct sdp_prwqn **p_next;
	ip2mac_t *ip2macp;
} sdp_prwqn_t;

#define	SDP_IPV4_ADDR(a)	(a->ip4addr)
#define	SDP_IPV6_ADDR(a)	(a->ip6addr)

#define	SDP_IS_V4_ADDR(a)	((a)->family == AF_INET)
#define	SDP_IS_V6_ADDR(a)	((a)->family == AF_INET6)

#endif /* _KERNEL */

#define	SDP_IOCTL		((('P' & 0xff) << 8) | (('R' & 0xff) << 16))

#define	SDP_PR_LOOKUP		(SDP_IOCTL + 1)
#define	IB_HW_LEN		20

typedef struct {
	int family;
	union {
		in_addr_t ip4_addr;
		in6_addr_t ip6_addr;
	} un;

	uint8_t hwaddr[IB_HW_LEN];
} sdp_prreq_t;

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IB_CLIENTS_SDP_LINK_H */
