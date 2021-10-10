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
#include <libdladm.h>
#include <libdllink.h>
#include <liblldp.h>
#include <net/if.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <strings.h>
#include "lldpsnmp_impl.h"
#include "lldp_impl.h"

#define	LOOPBACK_INTERFACE	"lo0"

/* indexes for elements in lldpV2Configuration group */
#define	LLDPV2CONFIG_MSGTXINTERVAL	1
#define	LLDPV2CONFIG_MSGTXHOLDMUL	2
#define	LLDPV2CONFIG_REINITDELAY	3
#define	LLDPV2CONFIG_NOTIFYINTERVAL	4
#define	LLDPV2CONFIG_TXCREDITMAX	5
#define	LLDPV2CONFIG_MSGFASTTX		6
#define	LLDPV2CONFIG_TXFASTINIT		7
#define	LLDPV2CONFIG_PORTCFGTABLE	8
#define	LLDPV2CONFIG_DSTADDRTABLE	9
#define	LLDPV2CONFIG_MANADDRCFGTABLE	10

/* column number definitions for table lldpV2PortConfigTable */
#define	LLDPV2_COL_PORTCFG_IFINDEX		1
#define	LLDPV2_COL_PORTCFG_DESTADDRESSINDEX	2
#define	LLDPV2_COL_PORTCFG_ADMINSTATUS		3
#define	LLDPV2_COL_PORTCFG_NOTIFICATIONENABLE	4
#define	LLDPV2_COL_PORTCFG_TLVSTXENABLE		5

/* column number definitions for table lldpV2DestAddressTable */
#define	LLDPV2_COL_DEST_ADDRESSTABLEINDEX	1
#define	LLDPV2_COL_DEST_MACADDRESS		2

/* column number definitions for table lldpV2ManAddrConfigTxPortsTable */
#define	LLDPV2_COL_MANADDRCFG_IFINDEX		1
#define	LLDPV2_COL_MANADDRCFG_DESTADDRESSINDEX	2
#define	LLDPV2_COL_MANADDRCFG_LOCMANADDRSUBTYPE	3
#define	LLDPV2_COL_MANADDRCFG_LOCMANADDR	4
#define	LLDPV2_COL_MANADDRCFG_TXENABLE		5
#define	LLDPV2_COL_MANADDRCFG_ROWSTATUS		6

/* LLDP configuration group OID */
#define	LLDPV2CONFIG_OID	1, 3, 111, 2, 802, 1, 1, 13, 1, 1

static uint8_t	lldp_dest_addr[ETHERADDRL] = LLDP_GROUP_ADDRESS;

static oid_register_t	register_configTable;
static oid_handler_t	configScalar_handler, portCfgTable_handler,
			destAddrTable_handler, mgmtAddrTable_handler;

static lldpv2_oid_desc_t config_oid_table[] = {
	{ "lldpV2MessageTxInterval", LLDPV2CONFIG_MSGTXINTERVAL,
	    ASN_UNSIGNED, HANDLER_CAN_RONLY, register_scalar,
	    configScalar_handler, 11, {LLDPV2CONFIG_OID, 1}, NULL, NULL, NULL },
	{ "lldpV2MessageTxHoldMultiplier", LLDPV2CONFIG_MSGTXHOLDMUL,
	    ASN_UNSIGNED, HANDLER_CAN_RONLY, register_scalar,
	    configScalar_handler, 11, {LLDPV2CONFIG_OID, 2}, NULL, NULL, NULL },
	{ "lldpV2ReinitDelay", LLDPV2CONFIG_REINITDELAY,
	    ASN_UNSIGNED, HANDLER_CAN_RONLY, register_scalar,
	    configScalar_handler, 11, {LLDPV2CONFIG_OID, 3}, NULL, NULL, NULL },
	{ "lldpV2NotificationInterval", LLDPV2CONFIG_NOTIFYINTERVAL,
	    ASN_UNSIGNED, HANDLER_CAN_RONLY, register_scalar,
	    configScalar_handler, 11, {LLDPV2CONFIG_OID, 4}, NULL, NULL, NULL },
	{ "lldpV2TxCreditMax", LLDPV2CONFIG_TXCREDITMAX,
	    ASN_UNSIGNED, HANDLER_CAN_RONLY, register_scalar,
	    configScalar_handler, 11, {LLDPV2CONFIG_OID, 5}, NULL, NULL, NULL },
	{ "lldpV2MessageFastTx", LLDPV2CONFIG_MSGFASTTX,
	    ASN_UNSIGNED, HANDLER_CAN_RONLY, register_scalar,
	    configScalar_handler, 11, {LLDPV2CONFIG_OID, 6}, NULL, NULL, NULL },
	{ "lldpV2TxFastInit", LLDPV2CONFIG_TXFASTINIT,
	    ASN_UNSIGNED, HANDLER_CAN_RONLY, register_scalar,
	    configScalar_handler, 11, {LLDPV2CONFIG_OID, 7}, NULL, NULL, NULL },
	{ "lldpV2PortConfigTable", LLDPV2CONFIG_PORTCFGTABLE,
	    0, HANDLER_CAN_RONLY, register_configTable, portCfgTable_handler,
	    11, {LLDPV2CONFIG_OID, 8}, NULL, NULL, NULL },
	{ "lldpV2DestAddressTable", LLDPV2CONFIG_DSTADDRTABLE,
	    0, HANDLER_CAN_RONLY, register_configTable, destAddrTable_handler,
	    11, {LLDPV2CONFIG_OID, 9}, NULL, NULL, NULL },
	{ "lldpV2ManAddrConfigTxPortsTable", LLDPV2CONFIG_MANADDRCFGTABLE,
	    0, HANDLER_CAN_RONLY, register_configTable, mgmtAddrTable_handler,
	    11, {LLDPV2CONFIG_OID, 10}, NULL, NULL, NULL },
	{ NULL, 0, 0, 0, NULL, NULL, 1, {0}, NULL, NULL, NULL }
};

static int
i_lldpv2_get_configScalar_value(netsnmp_request_info *request,
    lldpv2_oid_desc_t *lodp)
{
	uchar_t		*val;
	uint_t		valsize = sizeof (int);

	switch (lodp->lod_index) {
	case LLDPV2CONFIG_MSGTXINTERVAL:
		val = (uchar_t *)&lldp_msgTxInterval;
		break;
	case LLDPV2CONFIG_MSGTXHOLDMUL:
		val = (uchar_t *)&lldp_msgTxHold;
		break;
	case LLDPV2CONFIG_REINITDELAY:
		val = (uchar_t *)&lldp_reinitDelay;
		break;
	case LLDPV2CONFIG_NOTIFYINTERVAL:
		val = (uchar_t *)&lldp_txNotifyInterval;
		break;
	case LLDPV2CONFIG_TXCREDITMAX:
		val = (uchar_t *)&lldp_txCreditMax;
		break;
	case LLDPV2CONFIG_MSGFASTTX:
		val = (uchar_t *)&lldp_msgFastTx;
		break;
	case LLDPV2CONFIG_TXFASTINIT:
		val = (uchar_t *)&lldp_txFastInit;
		break;
	default:
		syslog(LOG_ERR, LLDPV2_AGENTNAME": invalid scalar specified");
		return (-1);
	}
	if (snmp_set_var_typed_value(request->requestvb, lodp->lod_type,
	    val, valsize) != 0) {
		syslog(LOG_ERR, LLDPV2_AGENTNAME": unable to set the "
		    "value to request structure");
		return (-1);
	}
	return (0);
}

/* ARGSUSED */
static int
configScalar_handler(netsnmp_mib_handler *handler,
    netsnmp_handler_registration *reginfo, netsnmp_agent_request_info *reqinfo,
    netsnmp_request_info *request)
{
	lldpv2_oid_desc_t *lodp;

	lodp = lldpv2_oname2desc(handler->handler_name, config_oid_table);

	switch (reqinfo->mode) {
	case MODE_GET:
		if (i_lldpv2_get_configScalar_value(request, lodp) != 0)
			return (SNMP_ERR_GENERR);
		break;
	default:
		/* we do not support SNMP writes yet, so throw an warning */
		syslog(LOG_WARNING, "modification of this OID is not "
		    "supported");
	}
	return (SNMP_ERR_NOERROR);
}

typedef struct lldpv2_portcfg_entry_s {
	lldp_admin_status_t	lpe_status;
	boolean_t		lpe_notify;
	uint16_t		lpe_tlvs;
} lldpv2_portcfg_entry_t;

/* ARGSUSED */
static void *
portcfgtable_get_data(void *arg, netsnmp_iterator_info *iinfo)
{
	lldp_linkid_list_t	*lidlist = arg;
	lldp_agent_t		*lap;
	lldp_write2pdu_t	*wp;
	lldpv2_portcfg_entry_t	pcfg, *pcfgp;
	uint16_t		tlvmask = 0;

	lldp_rw_lock(&lldp_agents_list_rwlock, LLDP_RWLOCK_READER);
	/* Retrieve lldp_agent_t for the given datalink, if any. */
	lap = lldp_agent_get(lidlist->ll_linkid, NULL);
	lldp_rw_unlock(&lldp_agents_list_rwlock);
	if (lap == NULL) {
		pcfg.lpe_status = LLDP_MODE_DISABLE;
		pcfg.lpe_notify = B_FALSE;
		pcfg.lpe_tlvs = tlvmask;
	} else {
		pcfg.lpe_status = lap->la_adminStatus;
		pcfg.lpe_notify = lap->la_notify;
		wp = i_lldp_get_write2pdu(lap, LLDP_BASIC_PORTDESC_TLVNAME);
		if (wp != NULL)
			tlvmask |= LLDP_BASIC_PORTDESC_TLV;
		wp = i_lldp_get_write2pdu(lap, LLDP_BASIC_SYSNAME_TLVNAME);
		if (wp != NULL)
			tlvmask |= LLDP_BASIC_SYSNAME_TLV;
		wp = i_lldp_get_write2pdu(lap, LLDP_BASIC_SYSDESC_TLVNAME);
		if (wp != NULL)
			tlvmask |= LLDP_BASIC_SYSDESC_TLV;
		wp = i_lldp_get_write2pdu(lap, LLDP_BASIC_SYSCAPAB_TLVNAME);
		if (wp != NULL)
			tlvmask |= LLDP_BASIC_SYSCAPAB_TLV;
		pcfg.lpe_tlvs = tlvmask;
		lldp_agent_refcnt_decr(lap);
	}

	if ((pcfgp = malloc(sizeof (lldpv2_portcfg_entry_t))) != NULL)
		*pcfgp = pcfg;
	return (pcfgp);
}

/* ARGSUSED */
static netsnmp_variable_list *
portcfgtable_getnext(void **loop_context, void **data_context,
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
portcfgtable_getfirst(void **loop_context, void **data_context,
    netsnmp_variable_list *put_index_data, netsnmp_iterator_info *mydata)
{
	lldp_linkid_list_t	*lidlist = NULL;
	netsnmp_variable_list	*idx = put_index_data;

	if (retrieve_all_linkids(&lidlist) != 0 || lidlist == NULL)
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

/*
 * We support only one kind of destination mac address, which is
 * LLDP_GROUP_ADDRESS.
 */
/* ARGSUSED */
static netsnmp_variable_list *
destaddrtable_getnext(void **loop_context, void **data_context,
    netsnmp_variable_list *put_index_data, netsnmp_iterator_info *mydata)
{
	return (NULL);
}

/* ARGSUSED */
static netsnmp_variable_list *
destaddrtable_getfirst(void **loop_context, void **data_context,
    netsnmp_variable_list *put_index_data, netsnmp_iterator_info *mydata)
{
	if (snmp_set_var_typed_integer(put_index_data, ASN_INTEGER, 0) != 0)
		return (NULL);
	*data_context = lldp_dest_addr;
	return (put_index_data);
}

/*
 * handles requests for the lldpV2DestAddressTable
 */
/* ARGSUSED */
int
destAddrTable_handler(netsnmp_mib_handler *handler,
    netsnmp_handler_registration *reginfo, netsnmp_agent_request_info *reqinfo,
    netsnmp_request_info *requests)
{
	netsnmp_request_info		*request;
	netsnmp_table_request_info	*table_info;
	uint8_t				*dstaddr;
	int				err;

	/* we do not support writes to the table for now */
	if (reqinfo->mode != MODE_GET) {
		syslog(LOG_WARNING, "modification to this table is "
		    "not supported");
		return (SNMP_ERR_NOERROR);
	}
	/* this case also covers GetNext requests */
	for (request = requests; request != NULL; request = request->next) {
		dstaddr = (uint8_t *)
		    netsnmp_extract_iterator_context(request);
		if (dstaddr == NULL) {
			(void) netsnmp_request_set_error(request,
			    SNMP_NOSUCHINSTANCE);
			continue;
		}
		table_info = netsnmp_extract_table_info(request);
		err = 0;
		switch (table_info->colnum) {
		case LLDPV2_COL_DEST_MACADDRESS:
			/* print the only supported address */
			err = snmp_set_var_typed_value(request->requestvb,
			    ASN_OCTET_STR, (uchar_t *)&lldp_dest_addr,
			    ETHERADDRL);
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
 * handles requests for the lldpV2PortConfigTable
 */
/* ARGSUSED */
int
portCfgTable_handler(netsnmp_mib_handler *handler,
    netsnmp_handler_registration *reginfo, netsnmp_agent_request_info *reqinfo,
    netsnmp_request_info *requests)
{
	netsnmp_request_info		*request;
	netsnmp_table_request_info	*table_info;
	lldpv2_portcfg_entry_t		*pcfgp;
	int				err;

	/* we do not support writes to the table for now */
	if (reqinfo->mode != MODE_GET) {
		syslog(LOG_WARNING, "modification to this table is "
		    "not supported");
		return (SNMP_ERR_NOERROR);
	}
	/* this case also covers GetNext requests */
	for (request = requests; request != NULL; request = request->next) {
		pcfgp = (lldpv2_portcfg_entry_t *)
		    netsnmp_extract_iterator_context(request);
		if (pcfgp == NULL) {
			(void) netsnmp_request_set_error(request,
			    SNMP_NOSUCHINSTANCE);
			continue;
		}
		table_info = netsnmp_extract_table_info(request);
		err = 0;
		switch (table_info->colnum) {
		case LLDPV2_COL_PORTCFG_ADMINSTATUS:
			err = snmp_set_var_typed_integer(request->requestvb,
			    ASN_INTEGER, pcfgp->lpe_status);
			break;
		case LLDPV2_COL_PORTCFG_NOTIFICATIONENABLE:
			err = snmp_set_var_typed_integer(request->requestvb,
			    ASN_INTEGER, pcfgp->lpe_notify);
			break;
		case LLDPV2_COL_PORTCFG_TLVSTXENABLE: {
			uint16_t	tlvs = pcfgp->lpe_tlvs;

			reverse_bits(&tlvs, sizeof (tlvs));
			err = snmp_set_var_typed_value(request->requestvb,
			    ASN_OCTET_STR, (uchar_t *)&tlvs, sizeof (tlvs));
			break;
		}
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

/* index for the management address table */
typedef struct lldpv2_mgmtaddr_index_s {
	struct lldpv2_mgmtaddr_index_s *lmi_next;
	datalink_id_t	lmi_linkid;
	uint32_t	lmi_addrsubtype;
	uint8_t		lmi_addrlen;
	uint8_t		lmi_addr[LLDP_MGMTADDR_ADDRLEN];
} lldpv2_mgmtaddr_index_t;

/* ARGSUSED */
static netsnmp_variable_list *
mgmtaddrtable_getnext(void **loop_context, void **data_context,
    netsnmp_variable_list *put_index_data, netsnmp_iterator_info *mydata)
{
	lldpv2_mgmtaddr_index_t	*maddrlistp = *loop_context;
	netsnmp_variable_list	*idx = put_index_data;

	if (maddrlistp == NULL || maddrlistp->lmi_next == NULL)
		return (NULL);
	maddrlistp = maddrlistp->lmi_next;
	if (snmp_set_var_typed_integer(idx, ASN_INTEGER,
	    maddrlistp->lmi_linkid) != 0) {
		return (NULL);
	}
	idx = idx->next_variable;
	if (snmp_set_var_typed_integer(idx, ASN_UNSIGNED, 0) != 0)
		return (NULL);
	idx = idx->next_variable;
	if (snmp_set_var_typed_integer(idx, ASN_INTEGER,
	    maddrlistp->lmi_addrsubtype) != 0) {
		return (NULL);
	}

	idx = idx->next_variable;
	if (snmp_set_var_typed_value(idx, ASN_OCTET_STR,
	    maddrlistp->lmi_addr, maddrlistp->lmi_addrlen) != 0) {
		return (NULL);
	}
	*loop_context = maddrlistp;
	return (put_index_data);
}

/* ARGSUSED */
static netsnmp_variable_list *
mgmtaddrtable_getfirst(void **loop_context, void **data_context,
    netsnmp_variable_list *put_index_data, netsnmp_iterator_info *mydata)
{
	struct ifaddrs		*ifa, *ifap;
	struct sockaddr_in	*sin;
	struct sockaddr_in6	*sin6;
	datalink_id_t		linkid;
	netsnmp_variable_list	*idx = put_index_data;
	lldpv2_mgmtaddr_index_t	*new, *maddrlistp = NULL;

	if (getifaddrs(&ifa) != 0)
		return (NULL);

	/*
	 * Walk througth all the addresses defined on the system except for
	 * the loopback addresses.
	 */
	for (ifap = ifa; ifap != NULL; ifap = ifap->ifa_next) {
		if (strcmp(ifap->ifa_name, LOOPBACK_INTERFACE) == 0)
			continue;
		/* get the linkid for the given interface name */
		if (dladm_name2info(dld_handle, ifap->ifa_name, &linkid, NULL,
		    NULL, NULL) != DLADM_STATUS_OK) {
			continue;
		}

		if (ifap->ifa_addr->sa_family != AF_INET &&
		    ifap->ifa_addr->sa_family != AF_INET6) {
			continue;
		}
		new = calloc(1, sizeof (lldpv2_mgmtaddr_index_t));
		if (new == NULL) {
			while (maddrlistp != NULL) {
				new = maddrlistp->lmi_next;
				free(maddrlistp);
				maddrlistp = new;
			}
			break;
		}
		new->lmi_linkid = linkid;
		if (ifap->ifa_addr->sa_family == AF_INET) {
			sin = (struct sockaddr_in *)(void *)(ifap->ifa_addr);
			new->lmi_addrsubtype = LLDP_MGMTADDR_TYPE_IPV4;
			new->lmi_addrlen = 4;
			bcopy(&sin->sin_addr.s_addr, new->lmi_addr, 4);
		} else if (ifap->ifa_addr->sa_family == AF_INET6) {
			sin6 = (struct sockaddr_in6 *)(void *)(ifap->ifa_addr);
			new->lmi_addrsubtype = LLDP_MGMTADDR_TYPE_IPV6;
			new->lmi_addrlen = 16;
			bcopy(&sin6->sin6_addr.s6_addr, new->lmi_addr, 16);
		} else {
			assert(0);
		}
		if (maddrlistp == NULL) {
			maddrlistp = new;
		} else {
			new->lmi_next = maddrlistp;
			maddrlistp = new;
		}
	}
	freeifaddrs(ifa);
	if (maddrlistp == NULL)
		return (NULL);

	if (snmp_set_var_typed_integer(idx, ASN_INTEGER,
	    maddrlistp->lmi_linkid) != 0) {
		free(maddrlistp);
		return (NULL);
	}
	idx = idx->next_variable;
	if (snmp_set_var_typed_integer(idx, ASN_UNSIGNED, 0) != 0) {
		free(maddrlistp);
		return (NULL);
	}
	idx = idx->next_variable;
	if (snmp_set_var_typed_integer(idx, ASN_INTEGER,
	    maddrlistp->lmi_addrsubtype) != 0) {
		free(maddrlistp);
		return (NULL);
	}
	idx = idx->next_variable;
	if (snmp_set_var_typed_value(idx, ASN_OCTET_STR,
	    maddrlistp->lmi_addr, maddrlistp->lmi_addrlen) != 0) {
		free(maddrlistp);
		return (NULL);
	}
	*loop_context = maddrlistp;
	return (put_index_data);
}

/*
 * handles requests for the lldpV2ManAddrConfigTxPortsTable
 */
/* ARGSUSED */
int
mgmtAddrTable_handler(netsnmp_mib_handler *handler,
    netsnmp_handler_registration *reginfo, netsnmp_agent_request_info *reqinfo,
    netsnmp_request_info *requests)
{
	netsnmp_request_info		*request;
	netsnmp_variable_list		*indexes;
	netsnmp_table_request_info	*table_info;
	lldp_mgmtaddr_t			*maddrp = NULL;
	int				count, rowstatus, err;
	boolean_t			enabled;
	char				str[LLDP_STRSIZE];

	/* we do not support writes to the table for now */
	if (reqinfo->mode != MODE_GET) {
		syslog(LOG_WARNING, "modification to this table is "
		    "not supported");
		return (SNMP_ERR_NOERROR);
	}
	/* this case also covers GetNext requests */
	for (request = requests; request != NULL; request = request->next) {
		if (request->processed != 0)
			continue;
		if ((table_info = netsnmp_extract_table_info(request)) == NULL)
			continue;
		/* get the address, it's the last index */
		indexes = table_info->indexes;
		while (indexes->next_variable != NULL)
			indexes = indexes->next_variable;
		(void) lldp_bytearr2hexstr(indexes->val.string,
		    indexes->val_len, str, LLDP_STRSIZE);
		lldp_rw_lock(&lldp_sysinfo_rwlock, LLDP_RWLOCK_READER);
		if (lldp_nvlist2mgmtaddr(lldp_sysinfo, str, &maddrp,
		    &count) != 0) {
			enabled = B_FALSE;
			rowstatus = SNMP_ROW_NOTINSERVICE;
		} else {
			enabled = B_TRUE;
			rowstatus = SNMP_ROW_ACTIVE;
		}
		lldp_rw_unlock(&lldp_sysinfo_rwlock);
		err = 0;
		switch (table_info->colnum) {
		case LLDPV2_COL_MANADDRCFG_TXENABLE:
			err = snmp_set_var_typed_integer(request->requestvb,
			    ASN_INTEGER, enabled);
			break;
		case LLDPV2_COL_MANADDRCFG_ROWSTATUS:
			err = snmp_set_var_typed_integer(request->requestvb,
			    ASN_INTEGER, rowstatus);
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

static int
register_configTable(void *arg)
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
	case LLDPV2CONFIG_PORTCFGTABLE:
		netsnmp_table_helper_add_indexes(table_info,
		    ASN_INTEGER,	/* DestIfIndex */
		    ASN_UNSIGNED,	/* index from DestMACAddressTable */
		    0);
		table_info->min_column = LLDPV2_COL_PORTCFG_ADMINSTATUS;
		table_info->max_column = LLDPV2_COL_PORTCFG_TLVSTXENABLE;
		iinfo->get_first_data_point = portcfgtable_getfirst;
		iinfo->get_next_data_point = portcfgtable_getnext;
		iinfo->make_data_context = portcfgtable_get_data;
		iinfo->free_data_context = free_context;
		iinfo->free_loop_context = free_context;
		break;
	case LLDPV2CONFIG_DSTADDRTABLE:
		netsnmp_table_helper_add_indexes(table_info,
		    ASN_UNSIGNED,	/* DestIfIndex */
		    0);
		table_info->min_column = table_info->max_column =
		    LLDPV2_COL_DEST_MACADDRESS;
		iinfo->get_first_data_point = destaddrtable_getfirst;
		iinfo->get_next_data_point = destaddrtable_getnext;
		break;
	case LLDPV2CONFIG_MANADDRCFGTABLE:
		netsnmp_table_helper_add_indexes(table_info,
		    ASN_INTEGER,	/* IfIndex */
		    ASN_UNSIGNED,	/* DestAddressIndex */
		    ASN_INTEGER,	/* AddrSubtype */
		    ASN_OCTET_STR,	/* LocManAddr */
		    0);
		table_info->min_column = LLDPV2_COL_MANADDRCFG_TXENABLE;
		table_info->max_column = LLDPV2_COL_MANADDRCFG_ROWSTATUS;
		iinfo->get_first_data_point = mgmtaddrtable_getfirst;
		iinfo->get_next_data_point = mgmtaddrtable_getnext;
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
init_lldpV2Configuration(void)
{
	init_mibgroup(config_oid_table);
}

/* un-initialize the entire lldpV2LocSystemData group */
void
uninit_lldpV2Configuration(void)
{
	uninit_mibgroup(config_oid_table);
}
