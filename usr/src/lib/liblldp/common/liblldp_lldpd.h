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

#ifndef _LIBLLDP_LLDPD_H
#define	_LIBLLDP_LLDPD_H

/*
 * Includes data structures and functions needed for library - lldpd
 * interactions.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <lldp.h>
#include <liblldp.h>
#include <libscf.h>
#include <libscf_priv.h>

#define	LLDPD_DOOR		"/etc/svc/volatile/dladm/lldpd_door"

#define	LLDP_SVC_NAME			"network/lldp"
#define	LLDP_SVC_DEFAULT_INSTANCE	"default"

/*
 * nvpair names which holds information.
 */
#define	LLDP_NVP_CHASSISID		"chassisid"
#define	LLDP_NVP_CHASSISID_TYPE		"chassisid_type"
#define	LLDP_NVP_CHASSISID_VALUE	"chassisid_value"
#define	LLDP_NVP_PORTID			"portid"
#define	LLDP_NVP_PORTID_TYPE		"portid_type"
#define	LLDP_NVP_PORTID_VALUE		"portid_value"
#define	LLDP_NVP_TTL			"ttl"
#define	LLDP_NVP_RXINFOVALID_FOR	"rxinfovalid"
#define	LLDP_NVP_NEXTTX_IN		"nexttx"
#define	LLDP_NVP_AGGR			"aggregation"
#define	LLDP_NVP_AGGR_ID		"aggr_id"
#define	LLDP_NVP_AGGR_STATUS		"aggr_status"
#define	LLDP_NVP_PVID			"pvid"
#define	LLDP_NVP_MAXFRAMESZ		"maxframesz"
#define	LLDP_NVP_PORTDESC		"portdesc"
#define	LLDP_NVP_SYSNAME		"sysname"
#define	LLDP_NVP_SYSDESCR		"sysdescr"
#define	LLDP_NVP_SYSCAPAB		"syscapab"
#define	LLDP_NVP_SUPPORTED_SYSCAPAB	"supcapab"
#define	LLDP_NVP_ENABLED_SYSCAPAB	"enabcapab"
#define	LLDP_NVP_MGMTADDR		"mgmtaddress"
#define	LLDP_NVP_MGMTADDRTYPE		"mgmtaddrtype"
#define	LLDP_NVP_MGMTADDRVALUE		"mgmtaddrvalue"
#define	LLDP_NVP_MGMTADDR_IFTYPE	"mgmtaddriftype"
#define	LLDP_NVP_MGMTADDR_IFNUM		"mgmtaddrifnumber"
#define	LLDP_NVP_MGMTADDR_OIDSTR	"mgmtaddroidstr"
#define	LLDP_NVP_RESERVED		"reserved"
#define	LLDP_NVP_ORGANIZATION		"organization"
#define	LLDP_NVP_UNREC_ORGANIZATION	"unrec_organization"
#define	LLDP_NVP_VLANID			"vlanid"
#define	LLDP_NVP_VLANNAME		"vlanname"
#define	LLDP_NVP_VNIC_PORTID		"vnicportid"
#define	LLDP_NVP_VNIC_PORTIDLEN		"vnicportidlen"
#define	LLDP_NVP_VNIC_LINKID		"vniclinkid"
#define	LLDP_NVP_VNICNAME		"vnicname"
#define	LLDP_NVP_VNIC_VLANID		"vnicvlanid"
#define	LLDP_NVP_PFC			"pfc"
#define	LLDP_NVP_WILLING		"willing"
#define	LLDP_NVP_PFC_MBC		"mbc"
#define	LLDP_NVP_PFC_CAP		"pfccap"
#define	LLDP_NVP_PFC_LCAP		"pfclcap"
#define	LLDP_NVP_PFC_ENABLE		"pfcenable"
#define	LLDP_NVP_PFC_LENABLE		"pfclenable"
#define	LLDP_NVP_APPLN			"appln"

#define	LLDP_BASIC_OUI_LIST		"basicOUI"
#define	LLDP_8021_OUI_LIST		"8021OUI"
#define	LLDP_8023_OUI_LIST		"8023OUI"
#define	LLDP_ORACLE_OUI_LIST		"oracleOUI"

/*
 * NOTE: Please maintain the order. All the Global properites should come
 * before the agent properties.
 */
typedef enum lldp_proptype_e {
	LLDP_PROPTYPE_NONE,
	LLDP_PROPTYPE_MODE,
	LLDP_PROPTYPE_BASICTLV,
	LLDP_PROPTYPE_8021TLV,
	LLDP_PROPTYPE_8023TLV,
	LLDP_PROPTYPE_VIRTTLV,
	LLDP_PROPTYPE_SUP_SYSCAPAB,
	LLDP_PROPTYPE_ENAB_SYSCAPAB,
	LLDP_PROPTYPE_IPADDR,
	LLDP_PROPTYPE_PFCMAP,
	LLDP_PROPTYPE_WILLING,
	LLDP_PROPTYPE_APPLN
} lldp_proptype_t;

typedef struct lldp_val_desc {
	char		*lvd_name;
	uintptr_t	lvd_val;
} lldp_val_desc_t;

#define	LLDP_VALCNT(vals)	(sizeof ((vals)) / sizeof (lldp_val_desc_t))

/*
 * door commands to the lldpd daemon
 */
typedef enum {
	LLDPD_CMD_GET_INFO,
	LLDPD_CMD_GET_STATS,
	LLDPD_CMD_SET_PROP,
	LLDPD_CMD_GET_PROP,
	LLDPD_CMD_UPDATE_VLINKS
} lldp_door_cmd_type_t;

typedef struct lldpd_door_arg_s {
	int	ld_cmd;
} lldpd_door_arg_t;

typedef struct lldpd_door_lstats_s {
	int	ld_cmd;
	char	ld_laname[MAXLINKNAMELEN];
} lldpd_door_lstats_t;

typedef struct lldpd_lstats_retval_s {
	uint_t		lr_err;
	lldp_stats_t	lr_stat;
} lldpd_lstats_retval_t;

typedef struct lldpd_retval_s {
	uint_t		lr_err; /* return error code */
} lldpd_retval_t;

typedef union lldp_pval_u {
	uint32_t	lpv_u32;
	char		lpv_strval[LLDP_MAXPROPVALLEN];
} lldp_pval_t;

typedef struct lldpd_door_lprops_s {
	int		lp_cmd;
	lldp_propclass_t lp_pclass;
	lldp_proptype_t	lp_ptype;
	char		lp_laname[MAXLINKNAMELEN];
	uint32_t	lp_flags;
	lldp_pval_t	lp_pval;
} lldpd_door_lprops_t;

typedef struct lldpd_gprops_retval_s {
	int	lpr_err;
	char	lpr_pval[LLDP_MAXPROPVALLEN];
} lldpd_gprops_retval_t;

typedef struct lldpd_retval_s	lldpd_sprops_retval_t;

/* Update VLAN/VNIC information */
typedef struct lldpd_vinfo_s {
	int		lvi_cmd;
	datalink_id_t	lvi_plinkid;
	datalink_id_t	lvi_vlinkid;
	uint16_t	lvi_vid;
	boolean_t	lvi_isvnic;
	int		lvi_operation;
} lldp_vinfo_t;

#define	LLDP_ADD_OPERATION	0x1
#define	LLDP_DELETE_OPERATION	0x2

typedef struct lldpd_door_minfo_s {
	int		ldm_cmd;
	boolean_t	ldm_neighbor;
	char		ldm_laname[MAXLINKNAMELEN];
} lldpd_door_minfo_t;

typedef struct lldpd_minfo_retval_s {
	uint_t		lmr_err;
	uint_t		lmr_listsz;
} lldpd_minfo_retval_t;

extern char		*lldp_alloc_fmri(const char *, const char *);
extern boolean_t	lldp_check_valid_link(dladm_handle_t, datalink_id_t,
			    datalink_id_t *, uint8_t *);
extern int		lldp_create_nested_nvl(nvlist_t *, const char *,
			    const char *, const char *, nvlist_t **);
extern int		lldp_get_nested_nvl(nvlist_t *, const char *,
			    const char *, const char *, nvlist_t **);
extern int		lldp_merge_nested_nvl(nvlist_t *, nvlist_t *,
			    const char *, const char *, const char *);
extern int		lldp_del_nested_nvl(nvlist_t *, const char *,
			    const char *, const char *);
extern char		*lldp_ptype2pname(lldp_proptype_t);

#ifdef __cplusplus
}
#endif

#endif /* _LIBLLDP_LLDPD_H */
