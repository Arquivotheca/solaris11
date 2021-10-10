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

#ifndef _LIBLLDP_H
#define	_LIBLLDP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <libdladm.h>
#include <libnvpair.h>
#include <lldp.h>
#include <sys/types.h>

#define	LLDP_ALLSTR		"all"
#define	LLDP_NONESTR		"none"
#define	LLDP_MAXPROPNAMELEN	128
#define	LLDP_MAXPROPVALLEN	512

typedef enum lldp_propclass_e {
	LLDP_PROPCLASS_NONE		= 0x00,
	LLDP_PROPCLASS_AGENT		= 0x01,
	LLDP_PROPCLASS_SYSCAPAB_TLV	= 0x02,
	LLDP_PROPCLASS_MGMTADDR_TLV	= 0x04,
	LLDP_PROPCLASS_PFC_TLV		= 0x08,
	LLDP_PROPCLASS_APPLN_TLV	= 0x10
} lldp_propclass_t;

#define	LLDP_PROPCLASS_AGENT_TLVS	\
	(LLDP_PROPCLASS_PFC_TLV | LLDP_PROPCLASS_APPLN_TLV)

#define	LLDP_PROPCLASS_GLOBAL_TLVS	\
	(LLDP_PROPCLASS_SYSCAPAB_TLV | LLDP_PROPCLASS_MGMTADDR_TLV)

/*
 * one per LLDP agent and it records the count of significant events
 * in the transmit and recieve state machines.
 */
typedef struct lldp_stats_s {
	uint32_t	ls_stats_AgeoutsTotal;
	uint32_t	ls_stats_FramesDiscardedTotal;
	uint32_t	ls_stats_FramesInErrorsTotal;
	uint32_t	ls_stats_FramesInTotal;
	uint32_t	ls_stats_FramesOutTotal;
	uint32_t	ls_stats_TLVSDiscardedTotal;
	uint32_t	ls_stats_TLVSUnrecognizedTotal;
	uint32_t	ls_stats_lldpduLengthErrors;
} lldp_stats_t;

/*
 * captures inserts/deletes/drops/ageouts/modified time for
 * remote systems table. This stats table is shared by all
 * lldp agents running on the system.
 */
typedef struct lldp_remtable_stats_s {
	uint32_t	lrs_stats_RemTablesLastChangeTime;
	uint32_t	lrs_stats_RemTablesInserts;
	uint32_t	lrs_stats_RemTablesDeletes;
	uint32_t	lrs_stats_RemTablesDrops;
	uint32_t	lrs_stats_RemTablesAgeouts;
	uint32_t	__padding;
} lldp_remtable_stats_t;

/* Unknown LLDP TLV */
typedef struct lldp_unknowntlv_s {
	uint32_t	lu_type;
	uint32_t	lu_len;
	uint8_t		lu_value[512];	/* raw bytes */
} lldp_unknowntlv_t;

/* Unrecognized Organization specific TLV */
typedef struct lldp_unrec_orginfo_s {
	uint8_t		lo_oui[3];
	uint8_t		lo_subtype;
	uint32_t	lo_index;
	uint32_t	lo_len;
	uint8_t		lo_value[508];
} lldp_unrec_orginfo_t;

typedef struct lldp_desc_s {
	uchar_t 	*ld_desclen;
	uint_t  	ld_desc;
} lldp_desc_t;

typedef enum {
	LLDP_STATUS_OK = 0,
	LLDP_STATUS_EXIST,
	LLDP_STATUS_BADARG,
	LLDP_STATUS_FAILED,
	LLDP_STATUS_TOOSMALL,
	LLDP_STATUS_NOTSUP,
	LLDP_STATUS_PROPUNKNOWN,
	LLDP_STATUS_BADVAL,
	LLDP_STATUS_NOMEM,
	LLDP_STATUS_LINKINVAL,
	LLDP_STATUS_LINKBUSY,
	LLDP_STATUS_PERSISTERR,
	LLDP_STATUS_BADRANGE,
	LLDP_STATUS_DISABLED,
	LLDP_STATUS_TEMPONLY,
	LLDP_STATUS_NOTDEFINED,
	LLDP_STATUS_NOTFOUND,
	LLDP_STATUS_UNKNOWN,
	LLDP_STATUS_PERMDENIED
} lldp_status_t;

/* Events */
#define	LLDP_VIRTUAL_LINK_UPDATE	1

#define	LLDP_NV_LINKID	"LLDP_Event_LINKID"
#define	LLDP_NV_VLINKID	"LLDP_Event_VLINKID"
#define	LLDP_NV_VID	"LLDP_Event_VID"
#define	LLDP_NV_ISPRIM	"LLDP_EVENT_ISPRIM"
#define	LLDP_NV_ADDED	"LLDP_EVENT_ADDED"

/*
 * following are the link properties that determine the TLV's
 * that a lldp agent can advertise.
 */
#define	LLDP_BASICTLV_GRPNAME		"basic-tlv"
#define	LLDP_8021TLV_GRPNAME		"dot1-tlv"
#define	LLDP_8023TLV_GRPNAME		"dot3-tlv"
#define	LLDP_VIRTTLV_GRPNAME		"virt-tlv"

/* numerical equivalent of above groups */
#define	LLDP_BASIC_TLVGRP		0x01
#define	LLDP_8021_TLVGRP		0x02
#define	LLDP_8023_TLVGRP		0x04
#define	LLDP_VIRT_TLVGRP		0x08

/* various possible values in 'basic-tlv' group */
#define	LLDP_BASIC_NONE_TLVNAME		LLDP_NONESTR
#define	LLDP_BASIC_PORTDESC_TLVNAME	"portdesc"
#define	LLDP_BASIC_SYSNAME_TLVNAME	"sysname"
#define	LLDP_BASIC_SYSDESC_TLVNAME	"sysdesc"
#define	LLDP_BASIC_SYSCAPAB_TLVNAME	"syscapab"
#define	LLDP_BASIC_MGMTADDR_TLVNAME	"mgmtaddr"
#define	LLDP_BASIC_ALL_TLVNAME		LLDP_ALLSTR

/* numerical equivalent of the above 'basic-tlv' values */
#define	LLDP_BASIC_NONE_TLV		0x00
#define	LLDP_BASIC_PORTDESC_TLV		0x01
#define	LLDP_BASIC_SYSNAME_TLV		0x02
#define	LLDP_BASIC_SYSDESC_TLV		0x04
#define	LLDP_BASIC_SYSCAPAB_TLV		0x08
#define	LLDP_BASIC_MGMTADDR_TLV		0x10
				/* following are the tlv's we support */
#define	LLDP_BASIC_ALL_TLV		\
	(LLDP_BASIC_PORTDESC_TLV | LLDP_BASIC_SYSNAME_TLV |\
	LLDP_BASIC_SYSDESC_TLV|LLDP_BASIC_SYSCAPAB_TLV|\
	LLDP_BASIC_MGMTADDR_TLV)

/* various possible values in 'dot1-tlv' group */
#define	LLDP_8021_NONE_TLVNAME		LLDP_NONESTR
#define	LLDP_8021_VLAN_NAME_TLVNAME	"vlanname"
#define	LLDP_8021_PVID_TLVNAME		"pvid"
#define	LLDP_8021_LINK_AGGR_TLVNAME	"linkaggr"
#define	LLDP_8021_PFC_TLVNAME		"pfc"
#define	LLDP_8021_APPLN_TLVNAME		"appln"
#define	LLDP_8021_ALL_TLVNAME		LLDP_ALLSTR

/* numerical equivalent of the above '8021-tlv' values */
#define	LLDP_8021_NONE_TLV		0x0000
#define	LLDP_8021_VLAN_NAME_TLV		0x0001
#define	LLDP_8021_PVID_TLV		0x0002
#define	LLDP_8021_LINK_AGGR_TLV		0x0004
#define	LLDP_8021_PFC_TLV		0x0008
#define	LLDP_8021_APPLN_TLV		0x0010
					/* following are the tlv's we support */
#define	LLDP_8021_ALL_TLV		(LLDP_8021_VLAN_NAME_TLV |\
					LLDP_8021_PVID_TLV | \
					LLDP_8021_LINK_AGGR_TLV | \
					LLDP_8021_PFC_TLV | \
					LLDP_8021_APPLN_TLV)

/* various possible values in 'dot3-tlv' group */
#define	LLDP_8023_NONE_TLVNAME		LLDP_NONESTR
#define	LLDP_8023_MAXFRAMESZ_TLVNAME	"max-framesize"
#define	LLDP_8023_MACPHY_TLVNAME	"macphy"
#define	LLDP_8023_POWERMDI_TLVNAME	"powermdi"
#define	LLDP_8023_ALL_TLVNAME		LLDP_ALLSTR

/* numerical equivalent of the above '8023-tlv' values */
#define	LLDP_8023_NONE_TLV		0x00
#define	LLDP_8023_MAXFRAMESZ_TLV	0x01
#define	LLDP_8023_MACPHY_TLV		0x02
#define	LLDP_8023_POWERMDI_TLV		0x04
					/* following are the tlv's we support */
#define	LLDP_8023_ALL_TLV		LLDP_8023_MAXFRAMESZ_TLV

/* various possible values in 'virt-tlv' group */
#define	LLDP_VIRT_NONE_TLVNAME		LLDP_NONESTR
#define	LLDP_VIRT_VNIC_TLVNAME		"vnic"
#define	LLDP_VIRT_ALL_TLVNAME		LLDP_ALLSTR

/* numerical equivalent of the above 'virt-tlv' values */
#define	LLDP_VIRT_NONE_TLV		0x00
#define	LLDP_VIRT_VNIC_TLV		0x01
#define	LLDP_VIRT_ALL_TLV		LLDP_VIRT_VNIC_TLV

/*
 * System capability properties.
 *
 * We do not have LLDP_ALLSTR because it's an invalid value. A system that has
 * 'station' capability cannot have any other capability at the same time.
 *
 * We do not have LLDP_NONESTR because a system should support atleast one of
 * capability below.
 */
#define	LLDP_SYSCAPAB_OTHER_NAME	"other"
#define	LLDP_SYSCAPAB_REPEATER_NAME	"repeater"
#define	LLDP_SYSCAPAB_MACBRIDGE_NAME	"bridge"
#define	LLDP_SYSCAPAB_WLAN_AP_NAME	"wlan-ap"
#define	LLDP_SYSCAPAB_ROUTER_NAME	"router"
#define	LLDP_SYSCAPAB_TELEPHONE_NAME	"telephone"
#define	LLDP_SYSCAPAB_DOCSIS_CD_NAME	"docsis-cd"
#define	LLDP_SYSCAPAB_STATION_NAME	"station"
#define	LLDP_SYSCAPAB_CVLAN_NAME	"cvlan"
#define	LLDP_SYSCAPAB_SVLAN_NAME	"svlan"
#define	LLDP_SYSCAPAB_TPMR_NAME		"tpmr"

/*
 * option flags passed to liblldp functions
 *
 *  - LLDP_OPT_ACTIVE:
 *	indicates the current value of a property
 *
 *  - LLDP_OPT_DEFAULT:
 *	indicatest the default value of a property
 *
 *  - LLDP_OPT_PERM
 *	indicates the permission of a property
 *
 *  - LLDP_OPT_POSSIBLE
 *	indicates range of values for a given property
 *
 *  - LLDP_OPT_APPEND
 *	for multi-valued properties, append a new value.
 *
 *  - LLDP_OPT_REMOVE
 *	for multi-valued properties, remove the specified value
 */
#define	LLDP_OPT_ACTIVE		0x00000001
#define	LLDP_OPT_DEFAULT	0x00000002
#define	LLDP_OPT_PERM		0x00000004
#define	LLDP_OPT_POSSIBLE	0x00000008
#define	LLDP_OPT_APPEND		0x00000010
#define	LLDP_OPT_REMOVE		0x00000020

#define	LLDP_NVP_LOCAL_CFG	"localcfg"
#define	LLDP_NVP_OPER_CFG	"opercfg"
#define	DCBX_NVP_PENDING	"pending"

#define	DCBX_MAPSIZE		15
#define	DCBX_MAX_PFC_TCS	8
#define	DCBX_MIN_PFC_TCS	0
#define	DCBX_MAX_MAP		255
#define	DCBX_MIN_MAP		0
#define	DCBX_MAX_APPLN_PRI	7
#define	DCBX_MIN_APPLN_PRI	0

extern const char	*lldp_status2str(lldp_status_t, char *);
extern lldp_status_t    lldp_errno2status(int);

/*
 * Property management functions
 */
typedef boolean_t	lldp_prop_wfunc_t(const char *, const char *, void *);
extern lldp_status_t	lldp_walk_prop(lldp_prop_wfunc_t *, void *,
			    lldp_propclass_t);

extern lldp_status_t	lldp_get_agent_info(const char *, boolean_t,
			    nvlist_t **);
extern lldp_status_t	lldp_get_agent_stats(const char *, lldp_stats_t *,
			    uint32_t);

extern int		lldp_str2nvlist(const char *, nvlist_t **, boolean_t);
extern lldp_status_t	lldp_get_agentprop(const char *, const char *, char *,
			    uint_t *, uint_t);
extern lldp_status_t	lldp_get_agent_tlvprop(const char *, const char *,
			    const char *, char *, uint_t *, uint_t);
extern lldp_status_t	lldp_get_global_tlvprop(const char *, const char *,
			    char *, uint_t *, uint_t);
extern lldp_status_t	lldp_set_agentprop(const char *, const char *, char *,
			    uint_t);
extern lldp_status_t	lldp_set_global_tlvprop(const char *, const char *,
			    char *, uint_t);
extern lldp_status_t	lldp_set_agent_tlvprop(const char *, const char *,
			    const char *, char *, uint_t);

extern char 		*lldp_portID2str(lldp_portid_t *, char *, size_t);
extern char		*lldp_chassisID2str(lldp_chassisid_t *, char *, size_t);
extern char		*lldp_port_subtype2str(uint8_t);
extern char		*lldp_maddr_subtype2str(uint8_t);
extern char		*lldp_maddr_ifsubtype2str(uint8_t);
extern char		*lldp_chassis_subtype2str(uint8_t);
extern void		lldp_syscapab2str(uint16_t, char *, size_t);
extern lldp_status_t	lldp_str2syscapab(const char *, uint16_t *);
extern void		lldp_mgmtaddr2str(lldp_mgmtaddr_t *, char *, size_t);
extern lldp_status_t	lldp_str2mask(const char *, char *, uint32_t *);
extern lldp_status_t	lldp_mask2str(const char *, uint32_t, char *,
			    uint_t *, boolean_t);
extern const char	*lldp_mode2str(lldp_admin_status_t);
extern void		lldp_bitmap2str(uint8_t, char *, uint_t);
extern	boolean_t	lldp_is_enabled(const char *);

extern int		lldp_nvlist2chassisid(nvlist_t *, lldp_chassisid_t *);
extern int		lldp_nvlist2portid(nvlist_t *, lldp_portid_t *);
extern int		lldp_nvlist2sysname(nvlist_t *, char **);
extern int		lldp_nvlist2sysdescr(nvlist_t *, char **);
extern int		lldp_nvlist2portdescr(nvlist_t *, char **);
extern int		lldp_nvlist2ttl(nvlist_t *, uint16_t *);
extern int		lldp_nvlist2syscapab(nvlist_t *, lldp_syscapab_t *);
extern int		lldp_nvlist2mgmtaddr(nvlist_t *, const char *,
			    lldp_mgmtaddr_t **, int *);
extern int		lldp_nvlist2maxfsz(nvlist_t *, uint16_t *);
extern int		lldp_nvlist2vlan(nvlist_t *, lldp_vlan_info_t **,
			    int *);
extern int		lldp_nvlist2vnic(nvlist_t *, lldp_vnic_info_t **,
			    int *);
extern int		lldp_nvlist2aggr(nvlist_t *, lldp_aggr_t *);
extern int		lldp_nvlist2pvid(nvlist_t *, uint16_t *);
extern int		lldp_nvlist2pfc(nvlist_t *, lldp_pfc_t *);
extern int		lldp_nvlist2appln(nvlist_t *, lldp_appln_t **,
			    uint_t *);
extern int		lldp_nvlist2app(nvlist_t *, uint16_t, uint8_t,
			    lldp_appln_t *);

extern int		lldp_nvlist2fcoepri(nvlist_t *, uint8_t *);
extern int		lldp_nvlist2unknowntlv(nvlist_t *, int,
			    lldp_unknowntlv_t **, uint_t *);
extern int		lldp_nvlist2unrec_orginfo(nvlist_t *, const char *,
			    lldp_unrec_orginfo_t **, uint_t *);
extern int		lldp_nvlist2infovalid(nvlist_t *, uint16_t *);
extern int		lldp_nvlist2nexttx(nvlist_t *, uint16_t *);

extern void		lldp_firsttlv(uint8_t *, int, lldp_tlv_t *);
extern void		lldp_nexttlv(uint8_t *, int, lldp_tlv_t *,
			    lldp_tlv_t *);
extern void		lldp_set_typelen(uint8_t *, uint8_t, uint16_t);

extern void		lldp_set_orgspecid_subtype(uint8_t *, uint8_t,
			    uint32_t, uint16_t);

extern int		lldp_tlv2chassisid(lldp_tlv_t *, lldp_chassisid_t *);
extern int		lldp_tlv2portid(lldp_tlv_t *, lldp_portid_t *);
extern int		lldp_tlv2ttl(lldp_tlv_t *, uint16_t *);
extern int		lldp_tlv2portdescr(lldp_tlv_t *, char *);
extern int		lldp_tlv2sysname(lldp_tlv_t *, char *);
extern int		lldp_tlv2sysdescr(lldp_tlv_t *, char *);
extern int		lldp_tlv2syscapab(lldp_tlv_t *, lldp_syscapab_t *);
extern int		lldp_tlv2mgmtaddr(lldp_tlv_t *, lldp_mgmtaddr_t *);
extern int		lldp_tlv2maxfsz(lldp_tlv_t *, uint16_t *);
extern int		lldp_tlv2aggr(lldp_tlv_t *, lldp_aggr_t *);
extern int		lldp_tlv2pvid(lldp_tlv_t *, uint16_t *);
extern int		lldp_tlv2vlan(lldp_tlv_t *, lldp_vlan_info_t *);
extern int		lldp_tlv2vnic(lldp_tlv_t *, lldp_vnic_info_t *);
extern int		lldp_tlv2pfc(lldp_tlv_t *, lldp_pfc_t *);
extern int		lldp_tlv2appln(lldp_tlv_t *, lldp_appln_t **,
			    uint_t *);
extern int		lldp_tlv2unknown(lldp_tlv_t *, char *, size_t);
extern void		lldp_get_ouistype(lldp_tlv_t *, uint32_t *, uint32_t *);

extern int		lldp_end2pdu(uint8_t *, size_t, size_t *);
extern int		lldp_chassisid2pdu(lldp_chassisid_t *, uint8_t *,
			    size_t, size_t *);
extern int		lldp_portid2pdu(lldp_portid_t *, uint8_t *, size_t,
			    size_t *);
extern int		lldp_ttl2pdu(uint16_t, uint8_t *, size_t, size_t *);
extern int		lldp_portdescr2pdu(const char *, uint8_t *, size_t,
			    size_t *);
extern int		lldp_sysname2pdu(const char *, uint8_t *, size_t,
			    size_t *);
extern int		lldp_sysdescr2pdu(const char *, uint8_t *, size_t,
			    size_t *);
extern int		lldp_syscapab2pdu(lldp_syscapab_t *, uint8_t *, size_t,
			    size_t *);
extern int		lldp_sysport_mgmtaddr2pdu(uint8_t *, size_t, uint32_t,
			    uint8_t *, size_t, size_t *);
extern int		lldp_mgmtaddr2pdu(lldp_mgmtaddr_t *, uint8_t *,
			    size_t, size_t *);
extern int		lldp_maxfsz2pdu(uint16_t, uint8_t *, size_t, size_t *);
extern int		lldp_aggr2pdu(lldp_aggr_t *, uint8_t *, size_t,
			    size_t *);
extern int		lldp_pvid2pdu(uint16_t, uint8_t *, size_t, size_t *);
extern int		lldp_vlan2pdu(lldp_vlan_info_t *, uint8_t *,
			    size_t, size_t *);
extern int		lldp_vnic2pdu(lldp_vnic_info_t *, uint8_t *,
			    size_t, size_t *);
extern int		lldp_pfc2pdu(lldp_pfc_t *, uint8_t *, size_t, size_t *);
extern int		lldp_appln2pdu(lldp_appln_t *, uint_t, uint8_t *,
			    size_t, size_t *);

extern int		lldp_door_call(void *, size_t, void *, size_t);
extern int		lldp_door_dyncall(void *, size_t, void **, size_t);

extern lldp_status_t	lldp_notify_events(int, nvlist_t *);

extern char		*dcbx_appln_sel2str(int);
extern uint_t		lldp_tlvname2pclass(const char *);
extern char		*lldp_pclass2tlvname(lldp_propclass_t);

#ifdef __cplusplus
}
#endif

#endif /* _LIBLLDP_H */
