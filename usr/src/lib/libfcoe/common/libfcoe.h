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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_LIBFCOE_H
#define	_LIBFCOE_H

#include <time.h>
#include <wchar.h>
#include <sys/param.h>
#include <sys/ethernet.h>
#include <libnvpair.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * FCoE Port Type
 */
#define	FCOE_PORTTYPE_INITIATOR		0
#define	FCOE_PORTTYPE_TARGET		1

#define	FCOE_MAX_MAC_NAME_LEN		32

#define	FCOE_SCF_ADD		0
#define	FCOE_SCF_REMOVE		1

#define	FCOE_SUCCESS			0
#define	FCOE_ERROR			1
#define	FCOE_ERROR_EXISTS		2
#define	FCOE_ERROR_SERVICE_NOT_FOUND	3
#define	FCOE_ERROR_NOMEM		4
#define	FCOE_ERROR_MEMBER_NOT_FOUND	5
#define	FCOE_ERROR_BUSY			6

#define	FCOE_TARGET_SERVICE	"system/fcoe_target"
#define	FCOE_INITIATOR_SERVICE	"system/fcoe_initiator"
#define	FCOE_PG_NAME	"fcoe-port-list-pg"
#define	FCOE_PORT_LIST	"port_list_p"

#define	FCOE_PORT_LIST_LENGTH	255

typedef unsigned char	FCOE_UINT8;
typedef		 char	FCOE_INT8;
typedef unsigned short	FCOE_UINT16;
typedef		 short	FCOE_INT16;
typedef unsigned int	FCOE_UINT32;
typedef		 int	FCOE_INT32;

typedef unsigned int	FCOE_STATUS;

#define	FCOE_STATUS_OK				0
#define	FCOE_STATUS_ERROR			1
#define	FCOE_STATUS_ERROR_INVAL_ARG		2
#define	FCOE_STATUS_ERROR_BUSY			3
#define	FCOE_STATUS_ERROR_ALREADY		4
#define	FCOE_STATUS_ERROR_PERM			5
#define	FCOE_STATUS_ERROR_OPEN_DEV		6
#define	FCOE_STATUS_ERROR_WWN_SAME		7
#define	FCOE_STATUS_ERROR_MAC_LEN		8
#define	FCOE_STATUS_ERROR_PWWN_CONFLICTED	9
#define	FCOE_STATUS_ERROR_NWWN_CONFLICTED	10
#define	FCOE_STATUS_ERROR_NEED_JUMBO_FRAME	11
#define	FCOE_STATUS_ERROR_CREATE_MAC		12
#define	FCOE_STATUS_ERROR_OPEN_MAC		13
#define	FCOE_STATUS_ERROR_CREATE_PORT		14
#define	FCOE_STATUS_ERROR_MAC_NOT_FOUND		15
#define	FCOE_STATUS_ERROR_OFFLINE_DEV		16
#define	FCOE_STATUS_ERROR_MORE_DATA		17
#define	FCOE_STATUS_ERROR_CLASS_UNSUPPORT	18
#define	FCOE_STATUS_ERROR_GET_LINKINFO		19

typedef struct fcoe_port_wwn {
	uchar_t	wwn[8];
} FCOE_PORT_WWN, *PFCOE_PORT_WWN;

typedef struct fcoe_port_attr {
	FCOE_PORT_WWN	port_wwn;
	FCOE_UINT8	mac_link_name[MAXLINKNAMELEN];
	FCOE_UINT8	mac_factory_addr[ETHERADDRL];
	FCOE_UINT8	mac_current_addr[ETHERADDRL];
	FCOE_UINT8	port_type;
	FCOE_UINT32	mtu_size;
	FCOE_UINT8	mac_promisc;
} FCOE_PORT_ATTRIBUTE, *PFCOE_PORT_ATTRIBUTE;

/*
 * FCoE port instance in smf repository
 */
typedef struct fcoe_smf_port_instance {
	FCOE_UINT8	mac_link_name[MAXLINKNAMELEN];
	FCOE_UINT8	port_type;
	FCOE_PORT_WWN	port_pwwn;
	FCOE_PORT_WWN	port_nwwn;
	FCOE_UINT8	mac_promisc;
} FCOE_SMF_PORT_INSTANCE, *PFCOE_SMF_PORT_INSTANCE;

/*
 * FCoE port instance list
 */
typedef struct fcoe_smf_port_list {
	FCOE_UINT32	port_num;
	FCOE_SMF_PORT_INSTANCE	ports[1];
} FCOE_SMF_PORT_LIST, *PFCOE_SMF_PORT_LIST;

/*
 * FCoE port property set
 */
typedef struct fcoe_port_prop {
	FCOE_UINT32	port_pmask;
	FCOE_UINT8	port_priority;
} FCOE_PORT_PROP;

/*
 * macLinkName: mac name with maximum lenth 32
 * portType: 0 (Initiator)/ 1(Target)
 * pwwn: Port WWN
 * nwwn: Nodw WWN
 * promiscous: to enable promisc mode for mac interface
 */
FCOE_STATUS FCOE_CreatePort(
	const FCOE_UINT8	*macLinkName,	/* maximum len: 32 */
	FCOE_UINT8	portType,
	FCOE_PORT_WWN	pwwn,
	FCOE_PORT_WWN	nwwn,
	FCOE_UINT8	promiscusous
);

FCOE_STATUS FCOE_DeletePort(
    const FCOE_UINT8	*macLinkName
);

/*
 * Make sure to free the memory pointed by portlist
 */
FCOE_STATUS FCOE_GetPortList(
    FCOE_UINT32		*port_num,
    FCOE_PORT_ATTRIBUTE	**portlist
);

/*
 * Make sure to free the memory pointed by portlist
 */
FCOE_STATUS FCOE_LoadConfig(
    FCOE_UINT8		portType,
    FCOE_SMF_PORT_LIST **portlist
);

/* Sets the priority property in the structure FCOE_PORT_PROP */
FCOE_STATUS FCOE_Set_Priority(
    FCOE_PORT_PROP	*fpp,
    FCOE_UINT8		priority
);

/* used to set multiple FCOE port properites */
FCOE_STATUS FCOE_SetpropPort(
    const FCOE_UINT8 *macLinkName,
    const FCOE_PORT_PROP *
);

#ifdef	__cplusplus
}
#endif

#endif	/* _LIBFCOE_H */
