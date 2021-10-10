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

#include <strings.h>
#include <stdlib.h>
#include "lldpsnmp_impl.h"
#include "lldp_impl.h"

/* indexes for elements in lldpV2RemoteSystemsData group */
#define	LLDPV2REM_TABLE				1
#define	LLDPV2REM_MANADDR_TABLE			2
#define	LLDPV2REM_UNKNOWNTLV_TABLE		3
#define	LLDPV2REM_ORGDEFINFO_TABLE		4

/* column number definitions for table lldpV2RemTable */
#define	LLDPV2_COL_REM_TIMEMARK			1
#define	LLDPV2_COL_REM_LOCALIFINDEX		2
#define	LLDPV2_COL_REM_LOCALDESTMACADDRESS	3
#define	LLDPV2_COL_REM_INDEX			4
#define	LLDPV2_COL_REM_CHASSISIDSUBTYPE		5
#define	LLDPV2_COL_REM_CHASSISID		6
#define	LLDPV2_COL_REM_PORTIDSUBTYPE		7
#define	LLDPV2_COL_REM_PORTID			8
#define	LLDPV2_COL_REM_PORTDESC			9
#define	LLDPV2_COL_REM_SYSNAME			10
#define	LLDPV2_COL_REM_SYSDESC			11
#define	LLDPV2_COL_REM_SYSCAPSUPPORTED		12
#define	LLDPV2_COL_REM_SYSCAPENABLED		13
#define	LLDPV2_COL_REM_REMOTECHANGES		14
#define	LLDPV2_COL_REM_TOOMANYNEIGHBORS		15

/* column number definitions for table lldpV2RemManAddrTable */
#define	LLDPV2_COL_REM_MANADDR_SUBTYPE		1
#define	LLDPV2_COL_REM_MANADDR			2
#define	LLDPV2_COL_REM_MANADDR_IFSUBTYPE	3
#define	LLDPV2_COL_REM_MANADDR_IFID		4
#define	LLDPV2_COL_REM_MANADDR_OID		5

/* column number definitions for table lldpV2RemUnknownTLVTable */
#define	LLDPV2_COL_REM_UNKNOWNTLV_TYPE		1
#define	LLDPV2_COL_REM_UNKNOWNTLV_INFO		2

/* column number definitions for table lldpV2RemOrgDefInfoTable */
#define	LLDPV2_COL_REM_ORGDEFINFO_OUI		1
#define	LLDPV2_COL_REM_ORGDEFINFO_SUBTYPE	2
#define	LLDPV2_COL_REM_ORGDEFINFO_INDEX		3
#define	LLDPV2_COL_REM_ORGDEFINFO		4

/* RemoteSystemsData Group OID */
#define	LLDPV2REMSYS_OID	1, 3, 111, 2, 802, 1, 1, 13, 1, 4

static oid_register_t	register_remSysTable;
static oid_handler_t	remTable_handler, remManAddrTable_handler,
			remUnknownTlvTable_handler, remOrgDefTable_handler;

static lldpv2_oid_desc_t remsys_oid_table[] = {
	{ "lldpV2RemTable", LLDPV2REM_TABLE,
	    0, HANDLER_CAN_RONLY, register_remSysTable, remTable_handler,
	    11, {LLDPV2REMSYS_OID, 1}, NULL, NULL, NULL },
	{ "lldpV2RemManAddrTable", LLDPV2REM_MANADDR_TABLE,
	    0, HANDLER_CAN_RONLY, register_remSysTable, remManAddrTable_handler,
	    11, {LLDPV2REMSYS_OID, 2}, NULL, NULL, NULL },
	{ "lldpV2RemUnknownTLVTable", LLDPV2REM_UNKNOWNTLV_TABLE,
	    0, HANDLER_CAN_RONLY, register_remSysTable,
	    remUnknownTlvTable_handler, 11, {LLDPV2REMSYS_OID, 3},
	    NULL, NULL, NULL },
	{ "lldpV2RemOrgDefInfoTable", LLDPV2REM_ORGDEFINFO_TABLE,
	    0, HANDLER_CAN_RONLY, register_remSysTable,
	    remOrgDefTable_handler, 11, {LLDPV2REMSYS_OID, 4},
	    NULL, NULL, NULL },
	{ NULL, 0, 0, 0, NULL, NULL, 1, {0}, NULL, NULL, NULL }
};

/* captures the indexes for all tables in remSystemsData group */
typedef struct lldp_remsysdata_loopctx_s {
	struct lldp_remsysdata_loopctx_s *lrl_next;
	datalink_id_t	lrl_linkid;
	uint32_t	lrl_remidx;
	uint64_t	lrl_timemark;
	uint32_t	lrl_addrtype;
	uint8_t		lrl_addrlen;
	uint8_t		lrl_addr[32];
	uint32_t	lrl_tlvtype;
	uint8_t		lrl_oui[3];
	uint32_t	lrl_orgsubtype;
	uint32_t	lrl_orgidx;
} lldp_remsysdata_loopctx_t;

/* ARGSUSED */
static void
free_data_context(void *arg, netsnmp_iterator_info *iinfo)
{
	lldp_agent_t	*lap = arg;

	if (lap != NULL)
		lldp_agent_refcnt_decr(lap);
}

/* ARGSUSED */
static void *
remsysdata_get_data(void *arg, netsnmp_iterator_info *iinfo)
{
	lldp_remsysdata_loopctx_t *lctx = arg;
	lldp_agent_t		*lap;

	lldp_rw_lock(&lldp_agents_list_rwlock, LLDP_RWLOCK_READER);
	/* Retrieve lldp_agent_t for the given datalink, if any. */
	lap = lldp_agent_get(lctx->lrl_linkid, NULL);
	lldp_rw_unlock(&lldp_agents_list_rwlock);
	return (lap);
}

/* ARGSUSED */
static netsnmp_variable_list *
remsysdata_getnext(void **loop_context, void **data_context,
    netsnmp_variable_list *put_index_data, netsnmp_iterator_info *mydata)
{
	lldp_remsysdata_loopctx_t	*lctx;
	netsnmp_variable_list	*idx = put_index_data;
	uint_t			tabletype = (uint_t)mydata->myvoid;
	int			err;

	lctx = (lldp_remsysdata_loopctx_t *)*loop_context;
	if (lctx == NULL || lctx->lrl_next == NULL)
		return (NULL);
	lctx = lctx->lrl_next;
	if (snmp_set_var_typed_integer(idx, ASN_TIMETICKS,
	    lctx->lrl_timemark) != 0) {
		return (NULL);
	}
	idx = idx->next_variable;
	if (snmp_set_var_typed_integer(idx, ASN_INTEGER, lctx->lrl_linkid) != 0)
		return (NULL);
	idx = idx->next_variable;
	if (snmp_set_var_typed_integer(idx, ASN_UNSIGNED, 0) != 0)
		return (NULL);
	idx = idx->next_variable;
	if (snmp_set_var_typed_integer(idx, ASN_UNSIGNED,
	    lctx->lrl_remidx) != 0) {
		return (NULL);
	}
	idx = idx->next_variable;
	switch (tabletype) {
	case LLDPV2REM_MANADDR_TABLE:
		err = snmp_set_var_typed_integer(idx, ASN_INTEGER,
		    lctx->lrl_addrtype);
		if (err != 0)
			break;
		idx = idx->next_variable;
		err = snmp_set_var_typed_value(idx, ASN_OCTET_STR,
		    (uchar_t *)&lctx->lrl_addr, lctx->lrl_addrlen);
		break;
	case LLDPV2REM_UNKNOWNTLV_TABLE:
		err = snmp_set_var_typed_integer(idx, ASN_UNSIGNED,
		    lctx->lrl_tlvtype);
		break;
	case LLDPV2REM_ORGDEFINFO_TABLE:
		err = snmp_set_var_typed_value(idx, ASN_OCTET_STR,
		    (uchar_t *)&lctx->lrl_oui, sizeof (lctx->lrl_oui));
		if (err != 0)
			break;
		idx = idx->next_variable;
		err = snmp_set_var_typed_integer(idx, ASN_UNSIGNED,
		    lctx->lrl_orgsubtype);
		if (err != 0)
			break;
		idx = idx->next_variable;
		err = snmp_set_var_typed_integer(idx, ASN_UNSIGNED,
		    lctx->lrl_orgidx);
		break;
	default:
		err = -1;
		break;
	}
	if (err != 0)
		return (NULL);
	*loop_context = lctx;
	return (put_index_data);
}

static void
free_remsysdata_index(void *arg)
{
	lldp_remsysdata_loopctx_t	*next, *cur = arg;

	while (cur != NULL) {
		next = cur->lrl_next;
		free(cur);
		cur = next;
	}
}

static int
i_remsysdata_index_alloc_buf(lldp_remsysdata_loopctx_t **lctxpp,
    lldp_remsysdata_loopctx_t *lctxp)
{
	lldp_remsysdata_loopctx_t *new;

	new = calloc(1, sizeof (lldp_remsysdata_loopctx_t));
	if (new == NULL)
		return (ENOMEM);
	*new = *lctxp;
	new->lrl_next = *lctxpp;
	*lctxpp = new;
	return (0);
}

static int
i_retrieve_remsysdata_index(lldp_agent_t *lap,
    lldp_remsysdata_loopctx_t **lctxpp, uint_t tabletype)
{
	nvlist_t	*nvl = lap->la_remote_mib;
	nvlist_t	*rnei, *invl, *mgmtnvl;
	nvpair_t	*onvp, *invp;
	uint8_t		*arr;
	uint_t		arrsz;
	int		err = 0;
	lldp_remsysdata_loopctx_t lctx;

	/* hold the remote mib lock */
	*lctxpp = NULL;
	lldp_rw_lock(&lap->la_rxmib_rwlock, LLDP_RWLOCK_READER);
	for (onvp = nvlist_next_nvpair(nvl, NULL); onvp != NULL;
	    onvp = nvlist_next_nvpair(nvl, onvp)) {
		bzero(&lctx, sizeof (lctx));
		/* Following nvpair/nvlist lookup will never fail */
		(void) nvpair_value_nvlist(onvp, &rnei);
		(void) nvlist_lookup_uint32(rnei, LLDP_NVP_REMIFINDEX,
		    &lctx.lrl_remidx);
		(void) nvlist_lookup_uint64(rnei, LLDP_NVP_REMSYSDATA_UPDTIME,
		    &lctx.lrl_timemark);
		lctx.lrl_linkid = lap->la_linkid;

		switch (tabletype) {
		case LLDPV2REM_TABLE:
			err = i_remsysdata_index_alloc_buf(lctxpp, &lctx);
			if (err != 0)
				goto ret;
			break;
		case LLDPV2REM_MANADDR_TABLE:
			if (nvlist_lookup_nvlist(rnei, LLDP_NVP_MGMTADDR,
			    &invl) != 0)
				break;
			invp = nvlist_next_nvpair(invl, NULL);
			for (; invp != NULL;
			    invp = nvlist_next_nvpair(invl, invp)) {
				(void) nvpair_value_nvlist(invp, &mgmtnvl);
				(void) nvlist_lookup_uint8(mgmtnvl,
				    LLDP_NVP_MGMTADDRTYPE,
				    (uint8_t *)&lctx.lrl_addrtype);
				(void) nvlist_lookup_byte_array(mgmtnvl,
				    LLDP_NVP_MGMTADDRVALUE, &arr, &arrsz);
				lctx.lrl_addrlen = arrsz;
				bcopy(arr, lctx.lrl_addr, lctx.lrl_addrlen);
				err = i_remsysdata_index_alloc_buf(lctxpp,
				    &lctx);
				if (err != 0)
					goto ret;
			}
			break;
		case LLDPV2REM_UNKNOWNTLV_TABLE:
			if (nvlist_lookup_nvlist(rnei, LLDP_NVP_RESERVED,
			    &invl) != 0)
				break;
			invp = nvlist_next_nvpair(invl, NULL);
			for (; invp != NULL;
			    invp = nvlist_next_nvpair(invl, invp)) {
				lctx.lrl_tlvtype = atoi(nvpair_name(invp));
				err = i_remsysdata_index_alloc_buf(lctxpp,
				    &lctx);
				if (err != 0)
					goto ret;
			}
			break;
		case LLDPV2REM_ORGDEFINFO_TABLE: {
			lldp_unrec_orginfo_t	*orgp, *tmp;
			uint_t	len, i;

			if (nvlist_lookup_nvlist(rnei,
			    LLDP_NVP_UNREC_ORGANIZATION, &invl) != 0)
				break;
			if (lldp_nvlist2unrec_orginfo(invl, NULL, &orgp,
			    &len) != 0 || len == 0)
				break;
			tmp = orgp;
			for (i = 0; i < len; i++, orgp++) {
				bcopy(orgp->lo_oui, lctx.lrl_oui, LLDP_OUI_LEN);
				lctx.lrl_orgsubtype = orgp->lo_subtype;
				lctx.lrl_orgidx = orgp->lo_index;
				err = i_remsysdata_index_alloc_buf(lctxpp,
				    &lctx);
				if (err != 0) {
					free(tmp);
					goto ret;
				}
			}
			free(tmp);
			break;
		}
		}
	}
ret:
	lldp_rw_unlock(&lap->la_rxmib_rwlock);
	if (err != 0) {
		free_remsysdata_index(*lctxpp);
		*lctxpp = NULL;
	}
	return (err);
}


static int
retrieve_remsysdata_index(lldp_remsysdata_loopctx_t **lctxpp, uint_t tabletype)
{
	lldp_agent_t	*lap;
	int		err = 0;

	*lctxpp = NULL;
	lldp_rw_lock(&lldp_agents_list_rwlock, LLDP_RWLOCK_READER);
	for (lap = list_head(&lldp_agents); lap != NULL;
	    lap = list_next(&lldp_agents, lap)) {
		if ((err = i_retrieve_remsysdata_index(lap, lctxpp,
		    tabletype)) != 0)
			break;
	}
	lldp_rw_unlock(&lldp_agents_list_rwlock);
	return (err);
}

/* ARGSUSED */
static netsnmp_variable_list *
remsysdata_getfirst(void **loop_context, void **data_context,
    netsnmp_variable_list *put_index_data, netsnmp_iterator_info *mydata)
{
	lldp_remsysdata_loopctx_t *lctx;
	netsnmp_variable_list	*idx = put_index_data;
	uint_t			tabletype = (uint_t)mydata->myvoid;
	int			err;

	if (retrieve_remsysdata_index(&lctx, tabletype) != 0 || lctx == NULL)
		return (NULL);
	if (snmp_set_var_typed_integer(idx, ASN_TIMETICKS,
	    lctx->lrl_timemark) != 0) {
		return (NULL);
	}
	idx = idx->next_variable;
	if (snmp_set_var_typed_integer(idx, ASN_INTEGER, lctx->lrl_linkid) != 0)
		return (NULL);
	idx = idx->next_variable;
	if (snmp_set_var_typed_integer(idx, ASN_UNSIGNED, 0) != 0)
		return (NULL);
	idx = idx->next_variable;
	if (snmp_set_var_typed_integer(idx, ASN_UNSIGNED,
	    lctx->lrl_remidx) != 0) {
		return (NULL);
	}
	idx = idx->next_variable;
	switch (tabletype) {
	case LLDPV2REM_MANADDR_TABLE:
		err = snmp_set_var_typed_integer(idx, ASN_INTEGER,
		    lctx->lrl_addrtype);
		if (err != 0)
			break;
		idx = idx->next_variable;
		err = snmp_set_var_typed_value(idx, ASN_OCTET_STR,
		    (uchar_t *)&lctx->lrl_addr, lctx->lrl_addrlen);
		break;
	case LLDPV2REM_UNKNOWNTLV_TABLE:
		err = snmp_set_var_typed_integer(idx, ASN_UNSIGNED,
		    lctx->lrl_tlvtype);
		break;
	case LLDPV2REM_ORGDEFINFO_TABLE:
		err = snmp_set_var_typed_value(idx, ASN_OCTET_STR,
		    (uchar_t *)&lctx->lrl_oui, sizeof (lctx->lrl_oui));
		if (err != 0)
			break;
		idx = idx->next_variable;
		err = snmp_set_var_typed_integer(idx, ASN_UNSIGNED,
		    lctx->lrl_orgsubtype);
		if (err != 0)
			break;
		idx = idx->next_variable;
		err = snmp_set_var_typed_integer(idx, ASN_UNSIGNED,
		    lctx->lrl_orgidx);
		break;
	default:
		err = -1;
		break;
	}
	if (err != 0)
		return (NULL);
	*loop_context = lctx;
	return (put_index_data);
}

/* helper function for all the table handlers below */
static nvlist_t *
i_get_remote_nvl(lldp_agent_t *lap, netsnmp_table_request_info *table_info)
{
	netsnmp_variable_list	*indexes;
	uint_t			remidx, nvl_remidx, count = 1;
	nvlist_t		*nvl;
	nvpair_t		*nvp;

	/*
	 * find the remote index from indexes, it's the fourth
	 * node in the list.
	 */
	indexes = table_info->indexes;
	while (count != 4) {
		indexes = indexes->next_variable;
		++count;
	}

	remidx = *indexes->val.integer;
	lldp_rw_lock(&lap->la_rxmib_rwlock, LLDP_RWLOCK_READER);
	for (nvp = nvlist_next_nvpair(lap->la_remote_mib, NULL);
	    nvp != NULL; nvp = nvlist_next_nvpair(lap->la_remote_mib, nvp)) {
		if (nvpair_value_nvlist(nvp, &nvl) != 0 ||
		    nvlist_lookup_uint32(nvl, LLDP_NVP_REMIFINDEX,
		    &nvl_remidx) != 0 || nvl_remidx == remidx) {
			break;
		}
	}
	lldp_rw_unlock(&lap->la_rxmib_rwlock);
	if (nvp == NULL)
		return (NULL);
	return (nvl);
}

/*
 * handles requests for the lldpV2RemTable
 */
/* ARGSUSED */
int
remTable_handler(netsnmp_mib_handler *handler,
    netsnmp_handler_registration *reginfo, netsnmp_agent_request_info *reqinfo,
    netsnmp_request_info *requests)
{
	netsnmp_request_info		*request;
	netsnmp_table_request_info	*table_info;
	lldp_agent_t			*lap;
	nvlist_t			*nvl;
	lldp_syscapab_t			scapab;
	lldp_chassisid_t		cid;
	lldp_portid_t			pid;
	char				*str;
	int				err;

	/* it's a read-only table and we don't support any other requests */
	if (reqinfo->mode != MODE_GET)
		return (SNMP_ERR_NOERROR);

	/* this case also covers GetNext requests */
	for (request = requests; request != NULL; request = request->next) {
		if (request->processed != 0)
			continue;
		if ((table_info = netsnmp_extract_table_info(request)) == NULL)
			continue;
		lap = (lldp_agent_t *)netsnmp_extract_iterator_context(
		    request);
		if (lap == NULL) {
			(void) netsnmp_request_set_error(request,
			    SNMP_NOSUCHINSTANCE);
			continue;
		}
		lldp_rw_lock(&lap->la_rxmib_rwlock, LLDP_RWLOCK_READER);
		if ((nvl = i_get_remote_nvl(lap, table_info)) == NULL) {
			lldp_rw_unlock(&lap->la_rxmib_rwlock);
			continue;
		}
		err = 0;
		switch (table_info->colnum) {
		case LLDPV2_COL_REM_CHASSISIDSUBTYPE:
			if (lldp_nvlist2chassisid(nvl, &cid) != 0)
				break;
			err = snmp_set_var_typed_integer(request->requestvb,
			    ASN_INTEGER, cid.lc_subtype);
			break;
		case LLDPV2_COL_REM_CHASSISID:
			if (lldp_nvlist2chassisid(nvl, &cid) != 0)
				break;
			err = snmp_set_var_typed_value(request->requestvb,
			    ASN_OCTET_STR, (uchar_t *)&cid.lc_cid,
			    cid.lc_cidlen);
			break;
		case LLDPV2_COL_REM_PORTIDSUBTYPE:
			if (lldp_nvlist2portid(nvl, &pid) != 0)
				break;
			err = snmp_set_var_typed_integer(request->requestvb,
			    ASN_INTEGER, pid.lp_subtype);
			break;
		case LLDPV2_COL_REM_PORTID:
			if (lldp_nvlist2portid(nvl, &pid) != 0)
				break;
			err = snmp_set_var_typed_value(request->requestvb,
			    ASN_OCTET_STR, (uchar_t *)&pid.lp_pid,
			    pid.lp_pidlen);
			break;
		case LLDPV2_COL_REM_PORTDESC:
			if (lldp_nvlist2portdescr(nvl, &str) != 0)
				str = "";
			err = snmp_set_var_typed_value(request->requestvb,
			    ASN_OCTET_STR, (uchar_t *)str, strlen(str));
			break;
		case LLDPV2_COL_REM_SYSNAME:
			if (lldp_nvlist2sysname(nvl, &str) != 0)
				str = "";
			err = snmp_set_var_typed_value(request->requestvb,
			    ASN_OCTET_STR, (uchar_t *)str, strlen(str));
			break;
		case LLDPV2_COL_REM_SYSDESC:
			if (lldp_nvlist2sysdescr(nvl, &str) != 0)
				str = "";
			err = snmp_set_var_typed_value(request->requestvb,
			    ASN_OCTET_STR, (uchar_t *)str, strlen(str));
			break;
		case LLDPV2_COL_REM_SYSCAPSUPPORTED:
			if (lldp_nvlist2syscapab(nvl, &scapab) != 0)
				break;
			reverse_bits(&scapab.ls_sup_syscapab,
			    sizeof (scapab.ls_sup_syscapab));
			err = snmp_set_var_typed_value(request->requestvb,
			    ASN_OCTET_STR, (uchar_t *)&scapab.ls_sup_syscapab,
			    sizeof (scapab.ls_sup_syscapab));
			break;
		case LLDPV2_COL_REM_SYSCAPENABLED:
			if (lldp_nvlist2syscapab(nvl, &scapab) != 0)
				break;
			reverse_bits(&scapab.ls_enab_syscapab,
			    sizeof (scapab.ls_enab_syscapab));
			err = snmp_set_var_typed_value(request->requestvb,
			    ASN_OCTET_STR, (uchar_t *)&scapab.ls_enab_syscapab,
			    sizeof (scapab.ls_enab_syscapab));
			break;
		case LLDPV2_COL_REM_REMOTECHANGES:
			err = snmp_set_var_typed_integer(request->requestvb,
			    ASN_INTEGER, lap->la_remoteChanges);
			break;
		case LLDPV2_COL_REM_TOOMANYNEIGHBORS:
			err = snmp_set_var_typed_integer(request->requestvb,
			    ASN_INTEGER, lap->la_tooManyNeighbors);
			break;
		default:
			(void) netsnmp_request_set_error(request,
			    SNMP_NOSUCHOBJECT);
			break;
		}
		lldp_rw_unlock(&lap->la_rxmib_rwlock);
		if (err != 0) {
			(void) netsnmp_request_set_error(request,
			    SNMP_ERR_GENERR);
		}
	}
	return (SNMP_ERR_NOERROR);
}

/*
 * handles requests for the lldpV2RemManAddrTable
 */
/* ARGSUSED */
int
remManAddrTable_handler(netsnmp_mib_handler *handler,
    netsnmp_handler_registration *reginfo, netsnmp_agent_request_info *reqinfo,
    netsnmp_request_info *requests)
{
	netsnmp_request_info		*request;
	netsnmp_variable_list		*indexes;
	netsnmp_table_request_info	*table_info;
	lldp_agent_t			*lap;
	lldp_mgmtaddr_t			*maddrp = NULL;
	nvlist_t			*mgmtnvl;
	int				count, err;
	char				str[LLDP_STRSIZE];

	/* this case also covers GetNext requests */
	for (request = requests; request != NULL; request = request->next) {
		if (request->processed != 0)
			continue;
		if ((table_info = netsnmp_extract_table_info(request)) == NULL)
			continue;
		lap = (lldp_agent_t *)netsnmp_extract_iterator_context(
		    request);
		if (lap == NULL) {
			(void) netsnmp_request_set_error(request,
			    SNMP_NOSUCHINSTANCE);
			continue;
		}
		lldp_rw_lock(&lap->la_rxmib_rwlock, LLDP_RWLOCK_READER);
		if ((mgmtnvl = i_get_remote_nvl(lap, table_info)) == NULL) {
			lldp_rw_unlock(&lap->la_rxmib_rwlock);
			continue;
		}

		/* get the address index */
		indexes = table_info->indexes;
		while (indexes->next_variable != NULL)
			indexes = indexes->next_variable;
		(void) lldp_bytearr2hexstr(indexes->val.string,
		    indexes->val_len, str, LLDP_STRSIZE);
		if (lldp_nvlist2mgmtaddr(mgmtnvl, str, &maddrp, &count) != 0) {
			lldp_rw_unlock(&lap->la_rxmib_rwlock);
			continue;
		}
		assert(count == 1);
		lldp_rw_unlock(&lap->la_rxmib_rwlock);
		err = 0;
		switch (table_info->colnum) {
		case LLDPV2_COL_REM_MANADDR_IFSUBTYPE:
			err = snmp_set_var_typed_integer(request->requestvb,
			    ASN_INTEGER, maddrp->lm_iftype);
			break;
		case LLDPV2_COL_REM_MANADDR_IFID:
			err = snmp_set_var_typed_integer(request->requestvb,
			    ASN_UNSIGNED, maddrp->lm_ifnumber);
			break;
		case LLDPV2_COL_REM_MANADDR_OID:
			err = snmp_set_var_typed_value(request->requestvb,
			    ASN_OCTET_STR, (uchar_t *)maddrp->lm_oid,
			    maddrp->lm_oidlen);
			break;
		default:
			(void) netsnmp_request_set_error(request,
			    SNMP_NOSUCHOBJECT);
			break;
		}
		free(maddrp);
		if (err != 0) {
			(void) netsnmp_request_set_error(request,
			    SNMP_ERR_GENERR);
		}
	}
	return (SNMP_ERR_NOERROR);
}

/*
 * handles requests for the lldpV2RemUnknownTLVTable
 */
/* ARGSUSED */
int
remUnknownTlvTable_handler(netsnmp_mib_handler *handler,
    netsnmp_handler_registration *reginfo, netsnmp_agent_request_info *reqinfo,
    netsnmp_request_info *requests)
{
	netsnmp_request_info		*request;
	netsnmp_variable_list		*indexes;
	netsnmp_table_request_info	*table_info;
	lldp_agent_t			*lap;
	lldp_unknowntlv_t		*ukp = NULL;
	nvlist_t			*nvl, *uknvl;
	uint_t				len, type;

	/* it's a read-only table and we don't support any other requests */
	if (reqinfo->mode != MODE_GET)
		return (SNMP_ERR_NOERROR);

	/* this case also covers GetNext requests */
	for (request = requests; request != NULL; request = request->next) {
		if (request->processed != 0)
			continue;
		if ((table_info = netsnmp_extract_table_info(request)) == NULL)
			continue;
		lap = (lldp_agent_t *)netsnmp_extract_iterator_context(
		    request);
		if (lap == NULL) {
			(void) netsnmp_request_set_error(request,
			    SNMP_NOSUCHINSTANCE);
			continue;
		}
		lldp_rw_lock(&lap->la_rxmib_rwlock, LLDP_RWLOCK_READER);
		if ((nvl = i_get_remote_nvl(lap, table_info)) == NULL ||
		    nvlist_lookup_nvlist(nvl, LLDP_NVP_RESERVED,
		    &uknvl) != 0) {
			lldp_rw_unlock(&lap->la_rxmib_rwlock);
			continue;
		}
		/* get the type index */
		indexes = table_info->indexes;
		while (indexes->next_variable != NULL)
			indexes = indexes->next_variable;
		type = *indexes->val.integer;
		if (lldp_nvlist2unknowntlv(uknvl, type, &ukp, &len) != 0 ||
		    len == 0) {
			lldp_rw_unlock(&lap->la_rxmib_rwlock);
			continue;
		}
		assert(len == 1);
		lldp_rw_unlock(&lap->la_rxmib_rwlock);
		switch (table_info->colnum) {
		case LLDPV2_COL_REM_UNKNOWNTLV_INFO:
			(void) snmp_set_var_typed_value(request->requestvb,
			    ASN_OCTET_STR, (uchar_t *)&ukp->lu_value,
			    ukp->lu_len);
			break;
		default:
			(void) netsnmp_request_set_error(request,
			    SNMP_NOSUCHOBJECT);
			break;
		}
		free(ukp);
	}
	return (SNMP_ERR_NOERROR);
}

/*
 * handles requests for the lldpV2RemOrgDefInfoTable
 */
/* ARGSUSED */
int
remOrgDefTable_handler(netsnmp_mib_handler *handler,
    netsnmp_handler_registration *reginfo, netsnmp_agent_request_info *reqinfo,
    netsnmp_request_info *requests)
{
	netsnmp_request_info		*request;
	netsnmp_variable_list		*indexes;
	netsnmp_table_request_info	*table_info;
	lldp_agent_t			*lap;
	lldp_unrec_orginfo_t		*orgp = NULL;
	nvlist_t			*nvl, *orgnvl;
	uint_t				len, count = 1;
	char				buf[LLDP_STRSIZE];
	uint32_t			oui = 0, stype, index;

	/* it's a read-only table and we don't support any other requests */
	if (reqinfo->mode != MODE_GET)
		return (SNMP_ERR_NOERROR);

	/* this case also covers GetNext requests */
	for (request = requests; request != NULL; request = request->next) {
		if (request->processed != 0)
			continue;
		if ((table_info = netsnmp_extract_table_info(request)) == NULL)
			continue;
		lap = (lldp_agent_t *)netsnmp_extract_iterator_context(
		    request);
		if (lap == NULL) {
			(void) netsnmp_request_set_error(request,
			    SNMP_NOSUCHINSTANCE);
			continue;
		}
		/* get the last index, which is local index, for this table */
		indexes = table_info->indexes;
		while (indexes != NULL) {
			if (count == 5) {
				bcopy(indexes->val.string, &oui,
				    indexes->val_len);
				oui = ntohl(oui);
				oui >>= 8;
			} else if (count == 6) {
				stype = *indexes->val.integer;
			} else if (count == 7) {
				index = *indexes->val.integer;
			}
			indexes = indexes->next_variable;
			++count;
		}
		lldp_rw_lock(&lap->la_rxmib_rwlock, LLDP_RWLOCK_READER);
		if ((nvl = i_get_remote_nvl(lap, table_info)) == NULL ||
		    nvlist_lookup_nvlist(nvl, LLDP_NVP_UNREC_ORGANIZATION,
		    &orgnvl) != 0) {
			lldp_rw_unlock(&lap->la_rxmib_rwlock);
			continue;
		}
		(void) snprintf(buf, sizeof (buf), "%u_%u_%u", oui, stype,
		    index);
		if (lldp_nvlist2unrec_orginfo(orgnvl, buf, &orgp,
		    &len) != 0 || len == 0) {
			lldp_rw_unlock(&lap->la_rxmib_rwlock);
			continue;
		}
		assert(len == 1);
		lldp_rw_unlock(&lap->la_rxmib_rwlock);
		switch (table_info->colnum) {
		case LLDPV2_COL_REM_ORGDEFINFO:
			(void) snmp_set_var_typed_value(request->requestvb,
			    ASN_OCTET_STR, (uchar_t *)&orgp->lo_value,
			    orgp->lo_len);
			break;
		default:
			(void) netsnmp_request_set_error(request,
			    SNMP_NOSUCHOBJECT);
			break;
		}
		free(orgp);
	}
	return (SNMP_ERR_NOERROR);
}

static int
register_remSysTable(void *arg)
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

	netsnmp_table_helper_add_indexes(table_info,
	    ASN_TIMETICKS,	/* index: lldpV2RemTimeMark */
	    ASN_INTEGER,	/* index: lldpV2RemLocalIfIndex */
	    ASN_UNSIGNED,	/* index: lldpV2RemLocalDestMACAddress */
	    ASN_UNSIGNED,	/* index: lldpV2RemIndex */
	    0);
	switch (lodp->lod_index) {
	case LLDPV2REM_TABLE:
		table_info->min_column = LLDPV2_COL_REM_CHASSISIDSUBTYPE;
		table_info->max_column = LLDPV2_COL_REM_TOOMANYNEIGHBORS;
		iinfo->myvoid = (void *)LLDPV2REM_TABLE;
		break;
	case LLDPV2REM_MANADDR_TABLE:
		/* this table has more indexes, add them */
		netsnmp_table_helper_add_indexes(table_info,
		    ASN_INTEGER,	/* index: lldpV2RemManAddrSubtype */
		    ASN_OCTET_STR,	/* index: lldpV2RemManAddr */
		    0);
		table_info->min_column = LLDPV2_COL_REM_MANADDR_IFSUBTYPE;
		table_info->max_column = LLDPV2_COL_REM_MANADDR_OID;
		iinfo->myvoid = (void *)LLDPV2REM_MANADDR_TABLE;
		break;
	case LLDPV2REM_UNKNOWNTLV_TABLE:
		/* this table has more indexes, add them */
		netsnmp_table_helper_add_indexes(table_info,
		    ASN_UNSIGNED,	/* index: lldpV2RemUnknownTLVType */
		    0);
		table_info->min_column = table_info->max_column =
		    LLDPV2_COL_REM_UNKNOWNTLV_INFO;
		iinfo->myvoid = (void *)LLDPV2REM_UNKNOWNTLV_TABLE;
		break;
	case LLDPV2REM_ORGDEFINFO_TABLE:
		/* this table has more indexes, add them */
		netsnmp_table_helper_add_indexes(table_info,
		    ASN_OCTET_STR,	/* index: lldpV2RemOrgDefInfoOUI */
		    ASN_UNSIGNED,	/* index: lldpV2RemOrgDefInfoSubtype */
		    ASN_UNSIGNED,	/* index: lldpV2RemOrgDefInfoIndex */
		    0);
		table_info->min_column = table_info->max_column =
		    LLDPV2_COL_REM_ORGDEFINFO;
		iinfo->myvoid = (void *)LLDPV2REM_ORGDEFINFO_TABLE;
		break;
	default:
		goto failure;
	}
	iinfo->get_first_data_point = remsysdata_getfirst;
	iinfo->get_next_data_point = remsysdata_getnext;
	iinfo->make_data_context = remsysdata_get_data;
	iinfo->free_data_context = free_data_context;
	iinfo->free_loop_context = free_context;
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

/* initialize the entire lldpV2RemSystemsData group */
void
init_lldpV2RemSystemsData(void)
{
	init_mibgroup(remsys_oid_table);
}

/* un-initialize the entire lldpV2RemSystemsData group */
void
uninit_lldpV2RemSystemsData(void)
{
	uninit_mibgroup(remsys_oid_table);
}
