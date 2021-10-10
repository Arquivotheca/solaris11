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

#include <assert.h>
#include <strings.h>
#include <libdladm.h>
#include <libdllink.h>
#include <liblldp.h>
#include "lldpsnmp_impl.h"
#include "lldp_impl.h"

/* indexes for elements in lldpV2LocSystemData group */
#define	LLDPV2LOC_CHASSISIDSUBTYPE	1
#define	LLDPV2LOC_CHASSISID		2
#define	LLDPV2LOC_SYSNAME		3
#define	LLDPV2LOC_SYSDESC		4
#define	LLDPV2LOC_SYSCAPSUPPORTED	5
#define	LLDPV2LOC_SYSCAPENABLED		6
#define	LLDPV2LOC_PORTTABLE		7

/* column number definitions for table lldpV2LocPortTable */
#define	LLDPV2_COL_LOCPORTIFINDEX	1
#define	LLDPV2_COL_LOCPORTIDSUBTYPE	2
#define	LLDPV2_COL_LOCPORTID		3
#define	LLDPV2_COL_LOCPORTDESC		4

/* LocalSystemsData Group OID */
#define	LLDPV2LOCSYS_OID	1, 3, 111, 2, 802, 1, 1, 13, 1, 3

static oid_register_t	register_locTable;
static oid_handler_t	locSysScalar_handler, locPortTable_handler;

static lldpv2_oid_desc_t locsys_oid_table[] = {
	{ "lldpV2LocChassisIdSubtype", LLDPV2LOC_CHASSISIDSUBTYPE,
	    ASN_INTEGER, HANDLER_CAN_RONLY, register_scalar,
	    locSysScalar_handler, 11, {LLDPV2LOCSYS_OID, 1}, NULL, NULL, NULL },
	{ "lldpV2LocChassisId", LLDPV2LOC_CHASSISID,
	    ASN_OCTET_STR, HANDLER_CAN_RONLY, register_scalar,
	    locSysScalar_handler, 11, {LLDPV2LOCSYS_OID, 2}, NULL, NULL, NULL },
	{ "lldpV2LocSysName", LLDPV2LOC_SYSNAME,
	    ASN_OCTET_STR, HANDLER_CAN_RONLY, register_scalar,
	    locSysScalar_handler, 11, {LLDPV2LOCSYS_OID, 3}, NULL, NULL, NULL },
	{ "lldpV2LocSysDesc", LLDPV2LOC_SYSDESC,
	    ASN_OCTET_STR, HANDLER_CAN_RONLY, register_scalar,
	    locSysScalar_handler, 11, {LLDPV2LOCSYS_OID, 4}, NULL, NULL, NULL },
	{ "lldpV2LocSysCapSupported", LLDPV2LOC_SYSCAPSUPPORTED,
	    ASN_OCTET_STR, HANDLER_CAN_RONLY, register_scalar,
	    locSysScalar_handler, 11, {LLDPV2LOCSYS_OID, 5}, NULL, NULL, NULL },
	{ "lldpV2LocSysCapEnabled", LLDPV2LOC_SYSCAPENABLED,
	    ASN_OCTET_STR, HANDLER_CAN_RONLY, register_scalar,
	    locSysScalar_handler, 11, {LLDPV2LOCSYS_OID, 6}, NULL, NULL, NULL },
	{ "lldpV2LocPortTable", LLDPV2LOC_PORTTABLE,
	    0, HANDLER_CAN_RONLY, register_locTable, locPortTable_handler,
	    11, {LLDPV2LOCSYS_OID, 7}, NULL, NULL, NULL },
	{ NULL, 0, 0, 0, NULL, NULL, 1, {0}, NULL, NULL, NULL }
};

typedef struct lldpsnmp_port_info_s {
	lldp_portid_t	lpi_pid;
	char		lpi_pdesc[LLDP_MAX_PORTDESCLEN];
} lldpsnmp_port_info_t;

static int
i_lldpv2_get_locSysScalar_value(netsnmp_request_info *request,
    lldpv2_oid_desc_t *lodp)
{
	char		sysname[LLDP_MAX_SYSNAMELEN];
	char		sysdesc[LLDP_MAX_SYSDESCLEN];
	uchar_t		*val;
	uint_t		valsize;
	int		err;
	lldp_syscapab_t	syscapab;
	lldp_chassisid_t cid;

	assert(lldp_sysinfo != NULL);
	switch (lodp->lod_index) {
	case LLDPV2LOC_CHASSISIDSUBTYPE:
	case LLDPV2LOC_CHASSISID:
		lldp_get_chassisid(&cid);
		if (lodp->lod_index == LLDPV2LOC_CHASSISIDSUBTYPE) {
			val = (uchar_t *)&cid.lc_subtype;
			valsize = sizeof (cid.lc_subtype);
		} else {
			val = (uchar_t *)&cid.lc_cid;
			valsize = cid.lc_cidlen;
		}
		break;
	case LLDPV2LOC_SYSNAME:
		lldp_get_sysname(sysname, sizeof (sysname));
		val = (uchar_t *)sysname;
		valsize = strlen(sysname);
		break;
	case LLDPV2LOC_SYSDESC:
		lldp_get_sysdesc(sysdesc, sizeof (sysdesc));
		val = (uchar_t *)sysdesc;
		valsize = strlen(sysdesc);
		break;
	case LLDPV2LOC_SYSCAPSUPPORTED:
	case LLDPV2LOC_SYSCAPENABLED:
		lldp_rw_lock(&lldp_sysinfo_rwlock, LLDP_RWLOCK_READER);
		if ((err = lldp_nvlist2syscapab(lldp_sysinfo,
		    &syscapab)) != 0) {
			lldp_rw_unlock(&lldp_sysinfo_rwlock);
			break;
		}
		if (lodp->lod_index  == LLDPV2LOC_SYSCAPSUPPORTED) {
			valsize = sizeof (syscapab.ls_sup_syscapab);
			reverse_bits(&syscapab.ls_sup_syscapab, valsize);
			val = (uchar_t *)&syscapab.ls_sup_syscapab;
		} else {
			valsize = sizeof (syscapab.ls_enab_syscapab);
			reverse_bits(&syscapab.ls_enab_syscapab, valsize);
			val = (uchar_t *)&syscapab.ls_enab_syscapab;
		}
		lldp_rw_unlock(&lldp_sysinfo_rwlock);
		break;
	default:
		syslog(LOG_ERR, LLDPV2_AGENTNAME": invalid scalar specified");
		return (-1);
	}

	if (err != 0) {
		syslog(LOG_WARNING, LLDPV2_AGENTNAME
		    ": could not retrieve local system information (%s) from "
		    "lldp daemon", lodp->lod_name);
	} else {
		if (snmp_set_var_typed_value(request->requestvb,
		    lodp->lod_type, val, valsize) != 0) {
			syslog(LOG_ERR, LLDPV2_AGENTNAME": unable to set the "
			    "value to request structure");
			return (-1);
		}
	}
	return (0);
}

/* ARGSUSED */
static int
locSysScalar_handler(netsnmp_mib_handler *handler,
    netsnmp_handler_registration *reginfo, netsnmp_agent_request_info *reqinfo,
    netsnmp_request_info *request)
{
	lldpv2_oid_desc_t *lodp;

	lodp = lldpv2_oname2desc(handler->handler_name, locsys_oid_table);

	switch (reqinfo->mode) {
	case MODE_GET:
		if (i_lldpv2_get_locSysScalar_value(request, lodp) != 0)
			return (SNMP_ERR_GENERR);
		break;
	default:
		/* we should never get here, so this is a really bad error */
		syslog(LOG_ERR, "unknown mode (%d) in locSysScalar_handler\n",
		    reqinfo->mode);
		return (SNMP_ERR_GENERR);
	}
	return (SNMP_ERR_NOERROR);
}

/* ARGSUSED */
static void *
locPortTable_get_data(void *arg, netsnmp_iterator_info *iinfo)
{
	lldp_linkid_list_t	*lidlist = (lldp_linkid_list_t *)arg;
	lldpsnmp_port_info_t	*pinfop = NULL;
	lldp_agent_t		*lap;
	nvlist_t		*nvl;
	int			err;
	char			*str;

	lldp_rw_lock(&lldp_agents_list_rwlock, LLDP_RWLOCK_READER);
	/* Retrieve lldp_agent_t for the given datalink, if any. */
	lap = lldp_agent_get(lidlist->ll_linkid, &err);
	lldp_rw_unlock(&lldp_agents_list_rwlock);
	if (err != 0)
		return (pinfop);

	if ((pinfop = calloc(1, sizeof (lldpsnmp_port_info_t))) != NULL) {
		lldp_rw_lock(&lap->la_txmib_rwlock, LLDP_RWLOCK_READER);
		if (nvlist_lookup_nvlist(lap->la_local_mib, lap->la_msap,
		    &nvl) == 0) {
			(void) lldp_nvlist2portid(nvl, &pinfop->lpi_pid);
			if (lldp_nvlist2portdescr(nvl, &str) == 0)
				bcopy(str, pinfop->lpi_pdesc, strlen(str));
		}
		lldp_rw_unlock(&lap->la_txmib_rwlock);
	}
	lldp_agent_refcnt_decr(lap);
	return (pinfop);
}

/* ARGSUSED */
static netsnmp_variable_list *
locPortTable_getnext(void **loop_context, void **data_context,
    netsnmp_variable_list *put_index_data, netsnmp_iterator_info *mydata)
{
	lldp_linkid_list_t	*lidlist = (lldp_linkid_list_t *)*loop_context;

	if (lidlist == NULL || lidlist->ll_next == NULL)
		return (NULL);
	lidlist = lidlist->ll_next;
	if (snmp_set_var_typed_integer(put_index_data, ASN_INTEGER,
	    lidlist->ll_linkid) != 0) {
		return (NULL);
	}
	*loop_context = lidlist;
	return (put_index_data);
}

/* ARGSUSED */
static netsnmp_variable_list *
locPortTable_getfirst(void **loop_context, void **data_context,
    netsnmp_variable_list *put_index_data, netsnmp_iterator_info *mydata)
{
	lldp_linkid_list_t	*lidlist = NULL;

	if (retrieve_lldp_linkids(&lidlist) != 0 || lidlist == NULL)
		return (NULL);
	if (snmp_set_var_typed_integer(put_index_data, ASN_INTEGER,
	    lidlist->ll_linkid) != 0) {
		return (NULL);
	}
	*loop_context = lidlist;
	return (put_index_data);
}

/*
 * handles requests for the lldpV2LocPortTable table
 */
/* ARGSUSED */
static int
locPortTable_handler(netsnmp_mib_handler *handler,
    netsnmp_handler_registration *reginfo, netsnmp_agent_request_info *reqinfo,
    netsnmp_request_info *requests)
{
	netsnmp_request_info		*request;
	netsnmp_table_request_info	*table_info;
	lldpsnmp_port_info_t		*pinfop;
	int				err;

	/* it's a read-only table and we don't support any other requests */
	if (reqinfo->mode != MODE_GET)
		return (SNMP_ERR_NOERROR);

	/* this case also covers GetNext requests */
	for (request = requests; request != NULL; request = request->next) {
		pinfop = (lldpsnmp_port_info_t *)
		    netsnmp_extract_iterator_context(request);
		if (pinfop == NULL) {
			(void) netsnmp_request_set_error(request,
			    SNMP_NOSUCHINSTANCE);
			continue;
		}
		table_info = netsnmp_extract_table_info(request);
		err = 0;
		switch (table_info->colnum) {
		case LLDPV2_COL_LOCPORTIDSUBTYPE:
			err = snmp_set_var_typed_integer(request->requestvb,
			    ASN_INTEGER, pinfop->lpi_pid.lp_subtype);
			break;
		case LLDPV2_COL_LOCPORTID:
			err = snmp_set_var_typed_value(request->requestvb,
			    ASN_OCTET_STR, (uchar_t *)&pinfop->lpi_pid.lp_pid,
			    pinfop->lpi_pid.lp_pidlen);
			break;
		case LLDPV2_COL_LOCPORTDESC:
			err = snmp_set_var_typed_value(request->requestvb,
			    ASN_OCTET_STR, (uchar_t *)pinfop->lpi_pdesc,
			    strlen(pinfop->lpi_pdesc));
			break;
		default:
			(void) netsnmp_request_set_error(request,
			    SNMP_NOSUCHOBJECT);
			break;
		}
		if (err != 0) {
			(void) netsnmp_request_set_error(request,
			    SNMP_ERR_GENERR);
		}
	}
	return (SNMP_ERR_NOERROR);
}

/*
 * register table handlers. Respective handlers will be called
 * whenever `snmpd' receives requests for the OID's in the table.
 */
static int
register_locTable(void *arg)
{
	netsnmp_handler_registration    *reg = NULL;
	netsnmp_iterator_info		*iinfo = NULL;
	netsnmp_table_registration_info *table_info = NULL;
	lldpv2_oid_desc_t		*lodp = arg;

	table_info = SNMP_MALLOC_TYPEDEF(netsnmp_table_registration_info);
	if (table_info == NULL)
		return (MIB_REGISTRATION_FAILED);

	if ((iinfo = SNMP_MALLOC_TYPEDEF(netsnmp_iterator_info)) == NULL)
		goto failure;

	reg = netsnmp_create_handler_registration(lodp->lod_name,
	    lodp->lod_handler, lodp->lod_oid, lodp->lod_oidlen, lodp->lod_acl);
	if (reg == NULL)
		goto failure;

	switch (lodp->lod_index) {
	case LLDPV2LOC_PORTTABLE:
		/* index: lldpV2LocPortIfIndex */
		netsnmp_table_helper_add_indexes(table_info, ASN_INTEGER, 0);
		table_info->min_column = LLDPV2_COL_LOCPORTIDSUBTYPE;
		table_info->max_column = LLDPV2_COL_LOCPORTDESC;
		iinfo->get_first_data_point = locPortTable_getfirst;
		iinfo->get_next_data_point = locPortTable_getnext;
		iinfo->make_data_context = locPortTable_get_data;
		iinfo->free_data_context = free_context;
		iinfo->free_loop_context = free_context;
		break;
	default:
		goto failure;
	}
	iinfo->table_reginfo = table_info;
	if (netsnmp_register_table_iterator(reg, iinfo) == MIB_REGISTERED_OK) {
		lodp->lod_reghandler = reg;
		lodp->lod_iter_info = iinfo;
		lodp->lod_table_info = table_info;
		return (MIB_REGISTERED_OK);
	}
failure:
	SNMP_FREE(table_info);
	SNMP_FREE(iinfo);
	SNMP_FREE(reg);
	return (MIB_REGISTRATION_FAILED);
}

/* initialize the entire lldpV2LocSystemData group */
void
init_lldpV2LocSystemData(void)
{
	init_mibgroup(locsys_oid_table);
}

/* un-initialize the entire lldpV2LocSystemData group */
void
uninit_lldpV2LocSystemData(void)
{
	uninit_mibgroup(locsys_oid_table);
}
