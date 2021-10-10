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

#ifndef _LLDPSNMP_IMPL_H
#define	_LLDPSNMP_IMPL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>
#include <net-snmp/library/snmp_api.h>
#include <net-snmp/library/snmp_client.h>
#include <net-snmp/library/snmp_logging.h>

#define	LLDPV2_AGENTNAME	"oracleLLDPV2"

typedef int	oid_register_t(void *);
typedef	int	oid_handler_t(netsnmp_mib_handler *,
		    netsnmp_handler_registration *,
		    netsnmp_agent_request_info *, netsnmp_request_info *);

typedef struct lldpv2_oid_desc_s {
	char		*lod_name;
	uchar_t		lod_index;
	uchar_t		lod_type;
	uchar_t		lod_acl;
	oid_register_t	*lod_register;
	oid_handler_t	*lod_handler;
	uchar_t		lod_oidlen;
	oid		lod_oid[MAX_OID_LEN];
	/*
	 * following three members are required while
	 * performing unregistration
	 */
	netsnmp_handler_registration *lod_reghandler;
	netsnmp_iterator_info *lod_iter_info;
	netsnmp_table_registration_info *lod_table_info;
} lldpv2_oid_desc_t;

typedef struct lldp_linkid_list_s {
	datalink_id_t			ll_linkid;
	struct lldp_linkid_list_s	*ll_next;
} lldp_linkid_list_t;

extern void		init_oracleLLDPV2(void);
extern void		uninit_oracleLLDPV2(void);
extern void		init_lldpV2Configuration(void);
extern void		init_lldpV2Statistics(void);
extern void		init_lldpV2LocSystemData(void);
extern void		init_lldpV2RemSystemsData(void);
extern void		uninit_lldpV2Configuration(void);
extern void		uninit_lldpV2Statistics(void);
extern void		uninit_lldpV2LocSystemData(void);
extern void		uninit_lldpV2RemSystemsData(void);
extern void		init_mibgroup(lldpv2_oid_desc_t *);
extern void		uninit_mibgroup(lldpv2_oid_desc_t *);
extern int		register_scalar(void *);
extern lldpv2_oid_desc_t *lldpv2_oname2desc(const char *, lldpv2_oid_desc_t *);
extern int		retrieve_lldp_linkids(lldp_linkid_list_t **);
extern int		retrieve_all_linkids(lldp_linkid_list_t **);
extern void		free_context(void *, netsnmp_iterator_info *);
extern void		reverse_bits(void *, int);

#ifdef __cplusplus
}
#endif

#endif /* _LLDPSNMP_IMPL_H */
