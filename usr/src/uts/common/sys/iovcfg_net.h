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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_SYS_IOVCFG_NET_H
#define	_SYS_IOVCFG_NET_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/mac.h>
#include <sys/mac_client.h>

#ifdef	_KERNEL

/*
 * Update bitmask used by reconfig tasks.
 */
typedef enum {
	IOV_UPD_NONE = 0x0,		/* No updates */
	IOV_UPD_MACADDR = 0x1,	/* Primary macaddr update */
	IOV_UPD_ALT_MACADDR = 0x2,	/* Alternate macaddr update */
	IOV_UPD_PVID = 0x4,		/* PVID update */
	IOV_UPD_VIDS = 0x8,		/* VLAN IDs update */
	IOV_UPD_MTU = 0x10		/* MTU update */
} iov_upd_t;

#define	IOVCFG_MAC_FLAGS_DEFAULT	MAC_UNICAST_HW
#define	IOVCFG_MAC_FLAGS_PVID	(MAC_UNICAST_HW | MAC_UNICAST_VLAN_TRANSPARENT)
#define	IOVCFG_MAC_FLAGS_VID	(MAC_UNICAST_HW | MAC_UNICAST_TAG_DISABLE | \
    MAC_UNICAST_STRIP_DISABLE)

typedef struct iov_pf_net	iov_pf_net_t;
typedef struct iov_vf_net	iov_vf_net_t;
typedef struct iov_alt_muh	iov_alt_muh_t;
typedef struct iov_vlan		iov_vlan_t;
typedef struct iov_net_tsk_arg	iov_net_tsk_arg_t;

/*
 * Bandwidth control not specified for the VF
 */
#define	IOVCFG_MAXBW_UNSPECIFIED	-1ULL

/*
 * MTU not specified for the VF
 */
#define	IOVCFG_MTU_UNSPECIFIED		0

/*
 * Network class specific PF data.
 */
struct iov_pf_net {
	char		ipf_macname[MAXPATHLEN];	/* mac name */
	mac_handle_t	ipf_mh;				/* mac handle */
};

/*
 * Network class specific VF data.
 */
struct iov_vf_net {
	/*	mac client handle */
	mac_client_handle_t	ivf_mch;

	/*	VF bind done ? */
	boolean_t		ivf_bound;

	/* primary-macaddr default (VLAN_ID_NONE) unicast handle */
	mac_unicast_handle_t	ivf_pri_muh;

	/* primary-macaddr pvid unicast handle */
	mac_unicast_handle_t	ivf_pri_pvid_muh;

	/* alternate-macaddr default (VLAN_ID_NONE) unicast handles table */
	iov_alt_muh_t		*ivf_def_altp;

	/* alternate-macaddr pvid unicast handles table */
	iov_alt_muh_t		*ivf_pvid_altp;

	/* primary macaddr */
	struct ether_addr	ivf_macaddr;

	/* # alternate macaddrs */
	int			ivf_num_alt_macaddr;

	/* alternate macaddrs */
	struct ether_addr	*ivf_alt_macaddr;

	/* Port VLAN ID (pvid)	*/
	uint16_t		ivf_pvid;

	/* # VLAN IDs */
	uint_t			ivf_nvids;

	/* VLAN IDs table */
	iov_vlan_t		*ivf_vids;

	/* Bandwidth */
	uint64_t		ivf_bandwidth;

	/* MTU */
	uint32_t		ivf_mtu;

	/* Original MTU */
	uint32_t		ivf_mtu_orig;
};

/*
 * Alternate unicast mac address handle; there is one instance of this
 * for every combination of pvid/vid and alternate unicast address.
 */
struct iov_alt_muh {
	uint_t			alt_index; /* alternate mac addr slot/index */
	mac_unicast_handle_t	alt_muh;	/* alt unicast handle */
	boolean_t		alt_added;	/* programmed */
};

/*
 * VLAN ID structure; one such for each VLAN ID configured for a VF/
 */
struct iov_vlan {
	uint16_t		ivl_vid;		/* vlan id */
	mac_unicast_handle_t	ivl_muh;		/* unicast handle */
	boolean_t		ivl_added;		/* programmed in mac */
	iov_alt_muh_t		*ivl_altp;		/* alt-mac-hdl tbl */
};

/*
 * VF reconfig task arg used in network class.
 */
struct iov_net_tsk_arg {
	iov_vf_t		*arg_vfp;	/* VF being reconfigured */

	/* New props to update */
	iov_upd_t		arg_upd;	/* bitmask of props updated */
	struct ether_addr	arg_macaddr;		/* mac addr */
	uint_t			arg_num_alt_macaddr;	/* # alt mac addrs */
	struct ether_addr	*arg_alt_macaddr;	/* alt mac addrs */
	uint16_t		arg_pvid;		/* pvid */
	uint_t			arg_nvids;		/* # vids */
	iov_vlan_t		*arg_vids;		/* vids */
	uint64_t		arg_bw;			/* bandwidth */
	uint32_t		arg_mtu;		/* mtu */
};

/* Exported functions */
void iovcfg_alloc_pf_net(iov_pf_t *);
void iovcfg_free_pf_net(iov_pf_t *);
void iovcfg_alloc_vf_net(iov_vf_t *);
void iovcfg_free_vf_net(iov_vf_t *);
int iovcfg_config_pf_net(iov_pf_t *);
void iovcfg_reconfig_reg_pf_net(iov_pf_t *);
#ifdef IOVCFG_UNCONFIG_SUPPORTED
void iovcfg_unconfig_pf_net(iov_pf_t *);
void iovcfg_reconfig_unreg_pf_net(iov_pf_t *);
#endif
int iovcfg_read_props_net(iov_vf_t *vfp, void *arg2, void *arg3);
void iovcfg_alloc_pvid_alt_table(iov_vf_t *);
void iovcfg_alloc_def_alt_table(iov_vf_t *);
void iovcfg_alloc_vid_alt_table(iov_vf_t *);
boolean_t iovcfg_cmp_vids(iov_vlan_t *, iov_vlan_t *, int nvids);
boolean_t iovcfg_cmp_macaddr(struct ether_addr *, struct ether_addr *, int cnt);
int iovcfg_dispatch_reconfig_task(iov_vf_t *, iov_net_tsk_arg_t *);

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_IOVCFG_NET_H */
