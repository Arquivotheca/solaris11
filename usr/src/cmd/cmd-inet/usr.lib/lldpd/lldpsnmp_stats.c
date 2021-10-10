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

/*
 * Block comment that describes the contents of this file.
 */
#include <assert.h>
#include <libdladm.h>
#include <libdllink.h>
#include <liblldp.h>
#include "lldpsnmp_impl.h"
#include "lldp_impl.h"

/* indexes for elements in lldpV2statistics group */
#define	LLDPV2STATS_RTLASTCHANGETIME	1
#define	LLDPV2STATS_RTINSERTS		2
#define	LLDPV2STATS_RTDELETES		3
#define	LLDPV2STATS_RTDROPS		4
#define	LLDPV2STATS_RTAGEOUTS		5
#define	LLDPV2STATS_TXTABLE		6
#define	LLDPV2STATS_RXTABLE		7

/* column number definitions for table lldpV2StatsTxPortTable */
#define	LLDPV2STATS_COL_TXIFINDEX		1
#define	LLDPV2STATS_COL_TXDESTMACADDRESS	2
#define	LLDPV2STATS_COL_TXPORTFRAMESTOTAL	3
#define	LLDPV2STATS_COL_TXLLDPDULENGTHERRORS	4

/* column number definitions for table lldpV2StatsRxPortTable */
#define	LLDPV2STATS_COL_RXDESTIFINDEX			1
#define	LLDPV2STATS_COL_RXDESTMACADDRESS		2
#define	LLDPV2STATS_COL_RXPORTFRAMESDISCARDEDTOTAL	3
#define	LLDPV2STATS_COL_RXPORTFRAMESERRORS		4
#define	LLDPV2STATS_COL_RXPORTFRAMESTOTAL		5
#define	LLDPV2STATS_COL_RXPORTTLVSDISCARDEDTOTAL	6
#define	LLDPV2STATS_COL_RXPORTTLVSUNRECOGNIZEDTOTAL	7
#define	LLDPV2STATS_COL_RXPORTAGEOUTSTOTAL		8

#define	LLDPV2STATS_OID		1, 3, 111, 2, 802, 1, 1, 13, 1, 2

extern lldp_remtable_stats_t remtable_stats;
extern pthread_mutex_t remtable_stats_lock;

static oid_register_t	register_statsTable;
static oid_handler_t	statsScalar_handler, rxtxPortTable_handler;

static lldpv2_oid_desc_t stats_oid_table[] = {
	{ "lldpV2StatsRemTablesLastChangeTime", LLDPV2STATS_RTLASTCHANGETIME,
	    ASN_TIMETICKS, HANDLER_CAN_RONLY, register_scalar,
	    statsScalar_handler, 11, {LLDPV2STATS_OID, 1}, NULL, NULL, NULL },
	{ "lldpV2StatsRemTablesInserts", LLDPV2STATS_RTINSERTS,
	    ASN_GAUGE, HANDLER_CAN_RONLY, register_scalar, statsScalar_handler,
	    11, {LLDPV2STATS_OID, 2}, NULL, NULL, NULL },
	{ "lldpV2StatsRemTablesDeletes", LLDPV2STATS_RTDELETES,
	    ASN_GAUGE, HANDLER_CAN_RONLY, register_scalar, statsScalar_handler,
	    11, {LLDPV2STATS_OID, 3}, NULL, NULL, NULL },
	{ "lldpV2StatsRemTablesDrops", LLDPV2STATS_RTDROPS,
	    ASN_GAUGE, HANDLER_CAN_RONLY, register_scalar, statsScalar_handler,
	    11, {LLDPV2STATS_OID, 4}, NULL, NULL, NULL },
	{ "lldpV2StatsRemTablesAgeouts", LLDPV2STATS_RTAGEOUTS,
	    ASN_GAUGE, HANDLER_CAN_RONLY, register_scalar, statsScalar_handler,
	    11, {LLDPV2STATS_OID, 5}, NULL, NULL, NULL },
	{ "lldpV2StatsTxPortTable", LLDPV2STATS_TXTABLE,
	    0, HANDLER_CAN_RONLY, register_statsTable, rxtxPortTable_handler,
	    11, {LLDPV2STATS_OID, 6}, NULL, NULL, NULL },
	{ "lldpV2StatsRxPortTable", LLDPV2STATS_RXTABLE,
	    0, HANDLER_CAN_RONLY, register_statsTable, rxtxPortTable_handler,
	    11, {LLDPV2STATS_OID, 7}, NULL, NULL, NULL },
	{ NULL, 0, 0, 0, NULL, NULL, 1, {0}, NULL, NULL, NULL }
};

static int
i_lldpv2_get_scalarStats_value(netsnmp_request_info *request,
    lldpv2_oid_desc_t *lodp)
{
	uchar_t	*val;
	uint_t 	valsize;
	lldp_remtable_stats_t *lrs;

	lldp_mutex_lock(&remtable_stats_lock);
	lrs = &remtable_stats;
	switch (lodp->lod_index) {
	case LLDPV2STATS_RTLASTCHANGETIME:
		val = (uchar_t *)&lrs->lrs_stats_RemTablesLastChangeTime;
		valsize = sizeof (lrs->lrs_stats_RemTablesLastChangeTime);
		break;
	case LLDPV2STATS_RTINSERTS:
		val = (uchar_t *)&lrs->lrs_stats_RemTablesInserts;
		valsize = sizeof (lrs->lrs_stats_RemTablesInserts);
		break;
	case LLDPV2STATS_RTDELETES:
		val = (uchar_t *)&lrs->lrs_stats_RemTablesDeletes;
		valsize = sizeof (lrs->lrs_stats_RemTablesDeletes);
		break;
	case LLDPV2STATS_RTDROPS:
		val = (uchar_t *)&lrs->lrs_stats_RemTablesDrops;
		valsize = sizeof (lrs->lrs_stats_RemTablesDrops);
		break;
	case LLDPV2STATS_RTAGEOUTS:
		val = (uchar_t *)&lrs->lrs_stats_RemTablesAgeouts;
		valsize = sizeof (lrs->lrs_stats_RemTablesAgeouts);
		break;
	default:
		lldp_mutex_unlock(&remtable_stats_lock);
		syslog(LOG_ERR, LLDPV2_AGENTNAME"invalid scalar specified");
		return (-1);
	}
	if (snmp_set_var_typed_value(request->requestvb,
	    lodp->lod_type, val, valsize) != 0) {
		lldp_mutex_unlock(&remtable_stats_lock);
		syslog(LOG_ERR, LLDPV2_AGENTNAME"unable to set the value to "
		    "request structure");
		return (-1);
	}
	lldp_mutex_unlock(&remtable_stats_lock);
	return (0);
}

/* ARGSUSED */
static int
statsScalar_handler(netsnmp_mib_handler *handler,
    netsnmp_handler_registration *reginfo, netsnmp_agent_request_info *reqinfo,
    netsnmp_request_info *request)
{
	lldpv2_oid_desc_t *lodp;

	lodp = lldpv2_oname2desc(handler->handler_name, stats_oid_table);
	assert(lodp != NULL);

	switch (reqinfo->mode) {
	case MODE_GET:
		if (i_lldpv2_get_scalarStats_value(request, lodp) != 0)
			return (SNMP_ERR_GENERR);
		break;
	default:
		/* we should never get here, so this is a really bad error */
		syslog(LOG_ERR, "unknown mode (%d) in statsScalar_handler\n",
		    reqinfo->mode);
		return (SNMP_ERR_GENERR);
	}
	return (SNMP_ERR_NOERROR);
}

/* ARGSUSED */
static void *
rxtxtable_get_data(void *arg, netsnmp_iterator_info *iinfo)
{
	lldp_linkid_list_t	*lidlist = (lldp_linkid_list_t *)arg;
	lldp_stats_t		*statp = NULL;
	lldp_agent_t		*lap;
	int			err;

	lldp_rw_lock(&lldp_agents_list_rwlock, LLDP_RWLOCK_READER);
	/* Retrieve lldp_agent_t for the given datalink, if any. */
	lap = lldp_agent_get(lidlist->ll_linkid, &err);
	lldp_rw_unlock(&lldp_agents_list_rwlock);
	if (err != 0)
		return (statp);
	if ((statp = malloc(sizeof (lldp_stats_t))) != NULL)
		*statp = lap->la_stats;
	lldp_agent_refcnt_decr(lap);
	return (statp);
}

/* ARGSUSED */
static netsnmp_variable_list *
rxtxtable_getnext(void **loop_context, void **data_context,
    netsnmp_variable_list *put_index_data, netsnmp_iterator_info *mydata)
{
	lldp_linkid_list_t	*lidlist = (lldp_linkid_list_t *)*loop_context;
	netsnmp_variable_list	*idx = put_index_data;

	if (lidlist == NULL || lidlist->ll_next == NULL)
		return (NULL);
	lidlist = lidlist->ll_next;
	if (snmp_set_var_typed_integer(idx, ASN_INTEGER,
	    lidlist->ll_linkid) != 0) {
		return (NULL);
	}
	idx = idx->next_variable;
	if (snmp_set_var_typed_integer(idx, ASN_UNSIGNED, 0) != 0)
		return (NULL);
	*loop_context = lidlist;
	return (put_index_data);
}

/* ARGSUSED */
static netsnmp_variable_list *
rxtxtable_getfirst(void **loop_context, void **data_context,
    netsnmp_variable_list *put_index_data, netsnmp_iterator_info *mydata)
{
	lldp_linkid_list_t	*lidlist = NULL;
	netsnmp_variable_list	*idx = put_index_data;

	if (retrieve_lldp_linkids(&lidlist) != 0 || lidlist == NULL)
		return (NULL);
	if (snmp_set_var_typed_integer(idx, ASN_INTEGER,
	    lidlist->ll_linkid) != 0) {
		return (NULL);
	}
	idx = idx->next_variable;
	if (snmp_set_var_typed_integer(idx, ASN_UNSIGNED, 0) != 0)
		return (NULL);
	*loop_context = lidlist;
	return (put_index_data);
}

static void
txPortTable_handler(netsnmp_request_info *request,
    netsnmp_table_request_info *table_info, lldp_stats_t *statp)
{
	int	err = 0;

	switch (table_info->colnum) {
	case LLDPV2STATS_COL_TXPORTFRAMESTOTAL:
		err = snmp_set_var_typed_integer(request->requestvb,
		    ASN_COUNTER, statp->ls_stats_FramesOutTotal);
		break;
	case LLDPV2STATS_COL_TXLLDPDULENGTHERRORS:
		err = snmp_set_var_typed_integer(request->requestvb,
		    ASN_COUNTER, statp->ls_stats_lldpduLengthErrors);
		break;
	default:
		(void) netsnmp_request_set_error(request, SNMP_NOSUCHOBJECT);
		break;
	}
	if (err != 0)
		(void) netsnmp_request_set_error(request, SNMP_ERR_GENERR);
}

static void
rxPortTable_handler(netsnmp_request_info *request,
    netsnmp_table_request_info *table_info, lldp_stats_t *statp)
{
	int	err = 0;

	switch (table_info->colnum) {
	case LLDPV2STATS_COL_RXPORTFRAMESDISCARDEDTOTAL:
		err = snmp_set_var_typed_integer(request->requestvb,
		    ASN_COUNTER, statp->ls_stats_FramesDiscardedTotal);
		break;
	case LLDPV2STATS_COL_RXPORTFRAMESERRORS:
		err = snmp_set_var_typed_integer(request->requestvb,
		    ASN_COUNTER, statp->ls_stats_FramesInErrorsTotal);
		break;
	case LLDPV2STATS_COL_RXPORTFRAMESTOTAL:
		err = snmp_set_var_typed_integer(request->requestvb,
		    ASN_COUNTER, statp->ls_stats_FramesInTotal);
		break;
	case LLDPV2STATS_COL_RXPORTTLVSDISCARDEDTOTAL:
		err = snmp_set_var_typed_integer(request->requestvb,
		    ASN_COUNTER, statp->ls_stats_TLVSDiscardedTotal);
		break;
	case LLDPV2STATS_COL_RXPORTTLVSUNRECOGNIZEDTOTAL:
		err = snmp_set_var_typed_integer(request->requestvb,
		    ASN_COUNTER, statp->ls_stats_TLVSUnrecognizedTotal);
		break;
	case LLDPV2STATS_COL_RXPORTAGEOUTSTOTAL:
		err = snmp_set_var_typed_integer(request->requestvb,
		    ASN_COUNTER, statp->ls_stats_AgeoutsTotal);
		break;
	default:
		(void) netsnmp_request_set_error(request, SNMP_NOSUCHOBJECT);
		break;
	}
	if (err != 0)
		(void) netsnmp_request_set_error(request, SNMP_ERR_GENERR);
}

/*
 * handles requests for the lldpV2StatsRxPortTable and lldpV2StatsTxPortTable.
 */
/* ARGSUSED */
static int
rxtxPortTable_handler(netsnmp_mib_handler *handler,
    netsnmp_handler_registration *reginfo, netsnmp_agent_request_info *reqinfo,
    netsnmp_request_info *requests)
{
	netsnmp_request_info		*request;
	netsnmp_table_request_info	*table_info;
	lldp_stats_t			*statp;

	/* it's a read-only table and we don't support any other requests */
	if (reqinfo->mode != MODE_GET)
		return (SNMP_ERR_NOERROR);

	/* this case also covers GetNext requests */
	for (request = requests; request != NULL; request = request->next) {
		statp = (lldp_stats_t *)
		    netsnmp_extract_iterator_context(request);
		if (statp == NULL) {
			(void) netsnmp_request_set_error(request,
			    SNMP_NOSUCHINSTANCE);
			continue;
		}
		table_info = netsnmp_extract_table_info(request);
		if (strcmp(handler->handler_name,
		    "lldpV2StatsRxPortTable") == 0) {
			rxPortTable_handler(request, table_info, statp);
		} else {
			txPortTable_handler(request, table_info, statp);
		}
	}
	return (SNMP_ERR_NOERROR);
}

static int
register_statsTable(void *arg)
{
	netsnmp_handler_registration    *reg = NULL;
	netsnmp_iterator_info		*iinfo = NULL;
	netsnmp_table_registration_info *table_info = NULL;
	lldpv2_oid_desc_t		*lodp = arg;

	/*
	 * register table handlers. Respective handlers will be called
	 * whenever `snmpd' receives requests for the OID's in the table.
	 */
	table_info = SNMP_MALLOC_TYPEDEF(netsnmp_table_registration_info);
	if (table_info == NULL)
		return (MIB_REGISTRATION_FAILED);

	if ((iinfo = SNMP_MALLOC_TYPEDEF(netsnmp_iterator_info)) == NULL)
		goto failure;

	reg = netsnmp_create_handler_registration(lodp->lod_name,
	    lodp->lod_handler, lodp->lod_oid, lodp->lod_oidlen, lodp->lod_acl);
	if (reg == NULL)
		goto failure;
	/* table indexes are same for both the tables */
	netsnmp_table_helper_add_indexes(table_info,
	    ASN_INTEGER,	/* index: DestIfIndex */
	    ASN_UNSIGNED,	/* index: index from DestMACAddressTable */
	    0);

	switch (lodp->lod_index) {
	case LLDPV2STATS_TXTABLE:
		table_info->min_column = LLDPV2STATS_COL_TXPORTFRAMESTOTAL;
		table_info->max_column = LLDPV2STATS_COL_TXLLDPDULENGTHERRORS;
		break;
	case LLDPV2STATS_RXTABLE:
		table_info->min_column =
		    LLDPV2STATS_COL_RXPORTFRAMESDISCARDEDTOTAL;
		table_info->max_column = LLDPV2STATS_COL_RXPORTAGEOUTSTOTAL;
		break;
	default:
		goto failure;
	}
	iinfo->table_reginfo = table_info;
	iinfo->get_first_data_point = rxtxtable_getfirst;
	iinfo->get_next_data_point = rxtxtable_getnext;
	iinfo->make_data_context = rxtxtable_get_data;
	iinfo->free_data_context = free_context;
	iinfo->free_loop_context = free_context;
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

/* initialize the entire lldpV2stats group */
void
init_lldpV2Statistics(void)
{
	init_mibgroup(stats_oid_table);
}

/* un-initialize the entire lldpV2stats group */
void
uninit_lldpV2Statistics(void)
{
	uninit_mibgroup(stats_oid_table);
}
