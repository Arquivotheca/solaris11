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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_SYS_VNIC_MGMT_H
#define	_SYS_VNIC_MGMT_H

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Extended diagnostic codes that can be returned.
 */
typedef enum {
	VNIC_IOC_DIAG_NONE,
	VNIC_IOC_DIAG_MACADDR_NIC,
	VNIC_IOC_DIAG_MACADDR_INUSE,
	VNIC_IOC_DIAG_MACADDR_INVALID,
	VNIC_IOC_DIAG_MACADDRLEN_INVALID,
	VNIC_IOC_DIAG_MACFACTORYSLOTINVALID,
	VNIC_IOC_DIAG_MACFACTORYSLOTUSED,
	VNIC_IOC_DIAG_MACFACTORYSLOTALLUSED,
	VNIC_IOC_DIAG_MACFACTORYNOTSUP,
	VNIC_IOC_DIAG_MACPREFIX_INVALID,
	VNIC_IOC_DIAG_MACPREFIXLEN_INVALID,
	VNIC_IOC_DIAG_MACMARGIN_INVALID,
	VNIC_IOC_DIAG_NO_HWRINGS,
	VNIC_IOC_DIAG_VLANID_INVALID,
	VNIC_IOC_DIAG_VNIC_EXISTS
} vnic_ioc_diag_t;

/*
 * Allowed VNIC MAC address types.
 *
 * - VNIC_MAC_ADDR_TYPE_FIXED, VNIC_MAC_ADDR_TYPE_RANDOM:
 *   The MAC address is specified by value by the caller, which
 *   itself can obtain it from the user directly,
 *   or pick it in a random fashion. Which method is used by the
 *   caller is irrelevant to the VNIC driver. However two different
 *   types are provided so that the information can be made available
 *   back to user-space when listing the kernel defined VNICs.
 *
 *   When a VNIC is created, the address in passed through the
 *   vc_mac_addr and vc_mac_len fields of the vnic_ioc_create_t
 *   structure.
 *
 * - VNIC_MAC_ADDR_TYPE_FACTORY: the MAC address is obtained from
 *   one of the MAC factory MAC addresses of the underyling NIC.
 *
 * - VNIC_MAC_ADDR_TYPE_AUTO: the VNIC driver attempts to
 *   obtain the address from one of the factory MAC addresses of
 *   the underlying NIC. If none is available, the specified
 *   MAC address value is used.
 *
 * - VNIC_MAC_ADDR_TYPE_PRIMARY: this is a VNIC based VLAN. The
 *   address for this is the address of the primary MAC client.
 *
 */

typedef enum {
	VNIC_MAC_ADDR_TYPE_UNKNOWN = -1,
	VNIC_MAC_ADDR_TYPE_FIXED,
	VNIC_MAC_ADDR_TYPE_RANDOM,
	VNIC_MAC_ADDR_TYPE_FACTORY,
	VNIC_MAC_ADDR_TYPE_AUTO,
	VNIC_MAC_ADDR_TYPE_PRIMARY,
	VNIC_MAC_ADDR_TYPE_VRID
} vnic_mac_addr_type_t;

#ifdef _KERNEL
#define	MAC_VLAN	0x01	/* create a VLAN data link */

extern int vnic_create(const char *vnic_name, const char *link_name,
    vnic_mac_addr_type_t *vnic_addr_type, int *mac_len,
    uchar_t *mac_addr, int *mac_slot, uint_t mac_prefix_len,
    uint16_t vid, uint32_t flags,
    datalink_id_t *vnic_linkid_out, vnic_ioc_diag_t *diag, void *reserved);

extern int vnic_delete(datalink_id_t vnic_linkid, uint32_t flags);

extern int vnic_modify_addr(datalink_id_t vnic_linkid,
    vnic_mac_addr_type_t *vnic_addr_type, int *mac_len,
    uchar_t *mac_addr, int *mac_slot, uint_t mac_prefix_len,
    vnic_ioc_diag_t *diag);


#endif  /* _KERNEL */


#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_VNIC_MGMT_H */
