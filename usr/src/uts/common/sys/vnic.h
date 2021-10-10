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

#ifndef	_SYS_VNIC_H
#define	_SYS_VNIC_H

#include <sys/types.h>
#include <sys/ethernet.h>
#include <sys/param.h>
#include <sys/mac.h>
#include <sys/mac_flow.h>
#include <sys/dld_ioc.h>
#include <sys/vnic_mgmt.h>

#ifdef	__cplusplus
extern "C" {
#endif

#if _LONG_LONG_ALIGNMENT == 8 && _LONG_LONG_ALIGNMENT_32 == 4
#pragma pack(4)
#endif

#define	VNIC_IOC_CREATE		VNICIOC(1)

#define	VNIC_IOC_CREATE_NODUPCHECK		0x00000001
#define	VNIC_IOC_CREATE_ANCHOR			0x00000002

/*
 * Force creation of VLAN based VNIC without checking if the
 * undelying MAC supports the margin size.
 */
#define	VNIC_IOC_CREATE_FORCE			0x00000004

/*
 * Default random MAC address prefix (locally administered).
 */
#define	VNIC_DEF_PREFIX	{0x02, 0x08, 0x20}

typedef struct vnic_ioc_create {
	datalink_id_t	vc_vnic_id;
	datalink_id_t	vc_link_id;
	zoneid_t	vc_zone_id;
	vnic_mac_addr_type_t vc_mac_addr_type;
	uint_t		vc_mac_len;
	uchar_t		vc_mac_addr[MAXMACADDRLEN];
	uint_t		vc_mac_prefix_len;
	int		vc_mac_slot;
	uint16_t	vc_vid;
	vrid_t		vc_vrid;
	int		vc_af;
	uint_t		vc_status;
	uint_t		vc_flags;
	vnic_ioc_diag_t	vc_diag;
	mac_resource_props_t vc_resource_props;
} vnic_ioc_create_t;

#define	VNIC_IOC_DELETE		VNICIOC(2)

typedef struct vnic_ioc_delete {
	datalink_id_t	vd_vnic_id;
} vnic_ioc_delete_t;

#define	VNIC_IOC_INFO		VNICIOC(3)

typedef struct vnic_info {
	datalink_id_t	vn_vnic_id;
	datalink_id_t	vn_link_id;
	zoneid_t	vn_owner_zone_id;
	zoneid_t	vn_zone_id;
	boolean_t	vn_onloan;
	vnic_mac_addr_type_t vn_mac_addr_type;
	uint_t		vn_mac_len;
	uchar_t		vn_mac_addr[MAXMACADDRLEN];
	uint_t		vn_mac_slot;
	uint32_t	vn_mac_prefix_len;
	uint16_t	vn_vid;
	vrid_t		vn_vrid;
	int		vn_af;
	boolean_t	vn_force;
	mac_resource_props_t vn_resource_props;
} vnic_info_t;

typedef struct vnic_ioc_info {
	vnic_info_t	vi_info;
} vnic_ioc_info_t;

#define	VNIC_IOC_MODIFY		VNICIOC(4)

#define	VNIC_IOC_MODIFY_ADDR		0x01
#define	VNIC_IOC_MODIFY_RESOURCE_CTL	0x02

typedef struct vnic_ioc_modify {
	datalink_id_t	vm_vnic_id;
	uint_t		vm_modify_mask;
	int		vm_mac_len;
	int		vm_mac_slot;
	uchar_t		vm_mac_addr[MAXMACADDRLEN];
	uint_t		vm_mac_prefix_len;
	vnic_mac_addr_type_t vm_mac_addr_type;
	mac_resource_props_t vm_resource_props;
	vnic_ioc_diag_t	vm_diag;
} vnic_ioc_modify_t;

#if _LONG_LONG_ALIGNMENT == 8 && _LONG_LONG_ALIGNMENT_32 == 4
#pragma pack()
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_VNIC_H */
