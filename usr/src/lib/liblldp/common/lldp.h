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

#ifndef _LLDP_H
#define	_LLDP_H

/*
 * Block comment that describes the contents of this file.
 */
#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>

/*
 * Multicast addresses used by LLDP agents. These MAC addresses define the
 * transmission scope of the LLDPDU packets sent.
 */
#define	LLDP_GROUP_ADDRESS	{ 0x01, 0x80, 0xc2, 0x00, 0x00, 0x0e }
#define	LLDP_NEAREST_NONTPMR_BRIDGE_MCAST_ADDR \
				{ 0x01, 0x80, 0xc2, 0x00, 0x00, 0x03 }
#define	LLDP_CUSTOMER_BRIDGE_MCAST_ADDR \
				{ 0x01, 0x80, 0xc2, 0x00, 0x00, 0x00 }

/* LLDP Agent operation modes */
typedef enum lldp_admin_status_e {
	LLDP_MODE_UNKNOWN,
	LLDP_MODE_TXONLY,
	LLDP_MODE_RXONLY,
	LLDP_MODE_RXTX,
	LLDP_MODE_DISABLE
} lldp_admin_status_t;

typedef enum {
	LLDP_TLVTYPE_END,
	LLDP_TLVTYPE_CHASSIS_ID,
	LLDP_TLVTYPE_PORT_ID,
	LLDP_TLVTYPE_TTL,
	LLDP_TLVTYPE_PORT_DESC,
	LLDP_TLVTYPE_SYS_NAME,
	LLDP_TLVTYPE_SYS_DESC,
	LLDP_TLVTYPE_SYS_CAPAB,
	LLDP_TLVTYPE_MGMT_ADDR,
	/* types 9-126 are reserved */
	LLDP_TLVTYPE_RESERVED,
	LLDP_ORGSPECIFIC_TLVTYPE = 127
} lldp_tlv_type_t;

typedef enum {
	LLDP_CHASSIS_ID_CHASSIS_COMPONENT = 1,
	LLDP_CHASSIS_ID_IFALIAS,
	LLDP_CHASSIS_ID_PORT_COMPONENT,
	LLDP_CHASSIS_ID_MACADDRESS,
	LLDP_CHASSIS_ID_IPADDRESS,
	LLDP_CHASSIS_ID_IFNAME,
	LLDP_CHASSIS_ID_LOCAL
} lldp_chassis_subtype_t;

typedef enum {
	LLDP_PORT_ID_IFALIAS = 1,	/* RFC 2863 - DisplayString */
	LLDP_PORT_ID_PORT_COMPONENT,	/* RFC 4133 - DisplayString */
	LLDP_PORT_ID_MACADDRESS,	/* IEEE Std 802 */
	LLDP_PORT_ID_IPADDRESS,		/* IPv4 or IPv6 address */
	LLDP_PORT_ID_IFNAME,		/* RFC 2863 - DisaplayString */
	LLDP_PORT_ID_AGENT_CICRUITID,	/* RFC 3046 - NumericString */
	LLDP_PORT_ID_LOCAL		/* alpha-numeric string */
} lldp_port_subtype_t;

#define	LLDP_SYSCAPAB_OTHER		0x0001
#define	LLDP_SYSCAPAB_REPEATER		0x0002
#define	LLDP_SYSCAPAB_MACBRIDGE		0x0004
#define	LLDP_SYSCAPAB_WLAN_AP		0x0008
#define	LLDP_SYSCAPAB_ROUTER		0x0010
#define	LLDP_SYSCAPAB_TELEPHONE		0x0020
#define	LLDP_SYSCAPAB_DOCSIS_CDEV	0x0040
#define	LLDP_SYSCAPAB_STATION_ONLY	0x0080
#define	LLDP_SYSCAPAB_CVLAN		0x0100
#define	LLDP_SYSCAPAB_SVLAN		0x0200
#define	LLDP_SYSCAPAB_TPMR		0x0400

/* Change this number if we add/remove capabilities */
#define	LLDP_MAX_SYSCAPAB_TYPE	11

#define	LLDP_TLV_TYPE(typelen)	((ntohs(*(uint16_t *)(void *)typelen)) >> 9)
#define	LLDP_TLV_LEN(typelen)	((ntohs(*(uint16_t *)(void *)typelen)) & 0x01ff)

#define	LLDP_TLVHDR_SZ		(sizeof (uint16_t))
#define	LLDP_ORGSPECHDR_SZ	(sizeof (uint32_t))
#define	LLDP_OUI_LEN		3

#define	LLDP_ORGTLV_OUI(ouistype)	\
	(ntohl(*(uint32_t *)(void *)(ouistype)) >> 8)
#define	LLDP_ORGTLV_STYPE(ouistype)	\
	(ntohl(*(uint32_t *)(void *)(ouistype)) & 0x000000FF)

#define	LLDP_802dot1_OUI	0x0080C2
#define	LLDP_802dot3_OUI	0x00120F
#define	LLDP_MED_OUI		0x0012BB
#define	LLDP_ORACLE_OUI		0x0003BA

/* IEEE 802.1 Organizationally Specific TLV Subtypes */
#define	LLDP_802dot1OUI_PVID_SUBTYPE		1
#define	LLDP_802dot1OUI_PPVID_SUBTYPE		2
#define	LLDP_802dot1OUI_VLAN_NAME_SUBTYPE	3
#define	LLDP_802dot1OUI_PROTOCOLID_SUBTYPE	4
#define	LLDP_802dot1OUI_LINK_AGGR_SUBTYPE	7
#define	LLDP_802dot1OUI_PFC_SUBTYPE		11
#define	LLDP_802dot1OUI_APPLN_SUBTYPE		12

/* IEEE 802.3 Organizationally Specific TLV Subtypes */
#define	LLDP_802dot3OUI_MACPHYS_SUBTYPE		1
#define	LLDP_802dot3OUI_POWVIAMDI_SUBTYPE	2
#define	LLDP_802dot3OUI_MAXFRAMESZ_SUBTYPE	4

/* Oracle OUI subtype */
#define	LLDP_ORACLEOUI_VNIC_SUBTYPE		1

#define	LLDP_MAX_PORTIDLEN		255
#define	LLDP_MAX_CHASSISIDLEN		255
#define	LLDP_MAX_MSAPLEN		\
	(LLDP_MAX_PORTIDLEN + LLDP_MAX_CHASSISIDLEN)
#define	LLDP_MAX_PORTIDSTRLEN		512 /* octets represented as string */
#define	LLDP_MAX_CHASSISIDSTRLEN	512 /* octets represented as string */
#define	LLDP_MAX_MSAPSTRLEN		1024
#define	LLDP_MAX_PORTDESCLEN		256 /* includes NUL terminating char */
#define	LLDP_MAX_SYSNAMELEN		256 /* includes NUL terminating char */
#define	LLDP_MAX_SYSDESCLEN		256
#define	LLDP_MAX_VLANNAMELEN		33 /* includes NUL terminating char */
#define	LLDP_MAX_VNICNAMELEN		33 /* includes NUL terminating char */
#define	LLDP_MIN_VNICTLV_LEN		12
#define	LLDP_MAX_VNICTLV_LEN		266
#define	LLDP_MGMTADDR_ADDRLEN		31
#define	LLDP_MGMTADDR_OIDLEN		128
#define	LLDP_STRSIZE			256

#define	LLDP_MAX_PDULEN			1500

/* variaous ianaAddressFamilyNumbers from (www.iana.org) */
#define	LLDP_MGMTADDR_TYPE_IPV4		0x1
#define	LLDP_MGMTADDR_TYPE_IPV6		0x2
#define	LLDP_MGMTADDR_TYPE_ALL802	0x6

#define	LLDP_MGMTADDR_IFTYPE_UNKNOWN	0x1
#define	LLDP_MGMTADDR_IFTYPE_IFINDEX	0x2
#define	LLDP_MGMTADDR_IFTYPE_SYSPORT	0x3

typedef struct lldp_tlv_s {
	uint8_t		lt_type;
	uint16_t	lt_len;
	uint8_t		*lt_value;
} lldp_tlv_t;

typedef struct lldp_chassid_s {
	uint8_t	lc_subtype;
	uint_t	lc_cidlen;
	uint8_t	lc_cid[LLDP_MAX_CHASSISIDLEN];
} lldp_chassisid_t;

typedef struct lldp_portid_s {
	uint8_t	lp_subtype;
	uint_t	lp_pidlen;
	uint8_t	lp_pid[LLDP_MAX_PORTIDLEN];
} lldp_portid_t;

typedef struct lldp_syscapab_s {
	uint16_t	ls_sup_syscapab;
	uint16_t	ls_enab_syscapab;
} lldp_syscapab_t;

typedef struct lldp_vlan_info_s {
	uint16_t		lvi_vid;
	uint8_t			lvi_vlen;
	char			lvi_name[LLDP_MAX_VLANNAMELEN];
} lldp_vlan_info_t;

typedef struct lldp_vnic_info_s {
	uint32_t		lvni_linkid;
	uint16_t		lvni_vid;
	lldp_portid_t		lvni_portid;
	char			lvni_name[LLDP_MAX_VNICNAMELEN];
} lldp_vnic_info_t;

typedef struct lldp_aggr_s {
	uint8_t		la_status;
	uint32_t	la_id;
} lldp_aggr_t;

typedef struct lldp_pfc_s {
	boolean_t	lp_willing;
	boolean_t	lp_mbc;
	uint8_t		lp_cap;
	uint8_t		lp_enable;
} lldp_pfc_t;

typedef struct lldp_appln_s {
	uint8_t		la_pri;
	uint8_t		la_sel;
	uint16_t	la_id;
} lldp_appln_t;

#define	LLDP_AGGR_CAPABLE	0x1
#define	LLDP_AGGR_MEMBER	0x2

typedef struct lldp_mgmtaddr_s {
	uint8_t		lm_subtype;
	uint8_t		lm_addrlen;
	uint8_t		lm_addr[LLDP_MGMTADDR_ADDRLEN];
	uint8_t		lm_iftype;
	uint32_t	lm_ifnumber;
	uint8_t		lm_oidlen;
	uint8_t		lm_oid[LLDP_MGMTADDR_OIDLEN];
} lldp_mgmtaddr_t;

/* DCBX Types */
typedef enum dcbx_protocol_type_s {
	DCBX_TYPE_PFC = 1,
	DCBX_TYPE_APPLICATION
} dcbx_protocol_type_t;

/* DCB Application TLV Selector Field types */
#define	DCB_APPLICATION_SF_ETHERTYPE		0x01
#define	DCB_APPLICATION_SF_TCP_SCTP		0x02
#define	DCB_APPLICATION_SF_UDP_DCCP		0x03
#define	DCB_APPLICATION_SF_TCP_SCTP_UDP_DCCP	0x04

#define	DCBX_FCOE_APPLICATION_ID1	0x8906
#define	DCBX_FCOE_APPLICATION_ID2	0x8914
#define	DCBX_FCOE_APPLICATION_SF	DCB_APPLICATION_SF_ETHERTYPE

#ifdef __cplusplus
}
#endif

#endif /* _LLDP_H */
