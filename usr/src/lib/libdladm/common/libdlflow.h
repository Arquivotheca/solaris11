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
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _LIBDLFLOW_H
#define	_LIBDLFLOW_H

/*
 * This file includes strcutures, macros and routines used by general
 * flow administration
 */
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/mac_flow.h>
#include <sys/dld.h>
#include <sys/param.h>
#include <sys/mac.h>
#include <libdladm.h>
#include <libdladm_impl.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct dladm_flow_attr {
	datalink_id_t		fa_linkid;
	char			fa_flowname[MAXFLOWNAMELEN];
	flow_desc_t		fa_flow_desc;
	mac_resource_props_t	fa_resource_props;
	uint64_t		fa_mask;
	int			fa_nattr;
} dladm_flow_attr_t;

extern dladm_status_t	dladm_flow_add(dladm_handle_t, datalink_id_t,
			    dladm_arg_list_t *, dladm_arg_list_t *, char *,
			    boolean_t, const char *);
extern dladm_status_t	dladm_flow_remove(dladm_handle_t, char *, boolean_t,
			    const char *);
extern dladm_status_t	dladm_flow_init(dladm_handle_t);

extern dladm_status_t	dladm_walk_flow(int (*)(dladm_handle_t,
			    dladm_flow_attr_t *, void *), dladm_handle_t,
			    datalink_id_t, void *, boolean_t);
extern dladm_status_t	dladm_flow_info(dladm_handle_t, const char *,
			    dladm_flow_attr_t *);
extern dladm_status_t	dladm_get_persist_flow(dladm_handle_t, const char *,
			    dladm_flow_attr_t *, int *);

extern dladm_status_t	dladm_set_flowprop(dladm_handle_t, const char *,
			    const char *, char **, uint_t, uint_t, char **,
			    const char *);
extern dladm_status_t	dladm_get_flowprop(dladm_handle_t, const char *,
			    uint32_t, const char *, char **, uint_t *);
extern dladm_status_t	dladm_walk_flowprop(int (*)(void *, const char *),
			    const char *, void *);

extern void		dladm_flow_attr_mask(uint64_t, dladm_flow_attr_t *);
extern dladm_status_t	dladm_flow_attr_check(dladm_arg_list_t *);
extern dladm_status_t	dladm_prefixlen2mask(int, int, uchar_t *);
extern dladm_status_t	dladm_mask2prefixlen(in6_addr_t *, int, int *);
extern char		*dladm_proto2str(uint8_t);
extern uint8_t		dladm_str2proto(const char *);

extern void		dladm_flow_attr_ip2str(dladm_flow_attr_t *,
			    char *, size_t);
extern void		dladm_flow_attr_proto2str(dladm_flow_attr_t *,
			    char *, size_t);
extern void		dladm_flow_attr_port2str(dladm_flow_attr_t *,
			    char *, size_t);
extern void		dladm_flow_attr_dsfield2str(dladm_flow_attr_t *,
			    char *, size_t);
extern dladm_status_t	dladm_get_flowconf_field(dladm_handle_t, const char *,
			    const char *, zoneid_t, void *, size_t);
extern dladm_status_t	dladm_get_flownext(dladm_handle_t, const char *,
			    zoneid_t, dld_flowinfo_t *, int *);
extern dladm_status_t	dladm_get_flownextzone(dladm_handle_t, zoneid_t,
			    zoneid_t *, int *);
extern dladm_status_t	dladm_set_flowconf_field(dladm_handle_t, const char *,
			    const char *, dladm_datatype_t, const void *);
extern dladm_status_t	dladm_read_flowconf(dladm_handle_t, const char *,
			    boolean_t *);
extern dladm_status_t	dladm_check_ip_addr(char *, boolean_t, flow_desc_t *);
extern dladm_status_t	dladm_check_dsfield(char *, flow_desc_t *);
extern dladm_status_t	dladm_remove_flowconf(dladm_handle_t, const char *,
			    const char *);
extern dladm_status_t	dladm_create_flowconf(dladm_handle_t, const char *,
			    datalink_id_t);
extern dladm_status_t	dladm_write_flowconf(dladm_handle_t, const char *,
			    const char *);
extern void		dladm_destroy_flowconf(dladm_handle_t, const char *);
extern dladm_status_t	dladm_check_ip_addr(char *, boolean_t, flow_desc_t *);
extern dladm_status_t	dladm_check_dsfield(char *, flow_desc_t *);
extern dladm_status_t	dladm_flow_create_ngz_kstat(dladm_handle_t, char *,
			    zoneid_t);

#ifdef	__cplusplus
}
#endif

#endif	/* _LIBDLFLOW_H */
