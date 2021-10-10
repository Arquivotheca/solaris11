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

#include <libdllink.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <inttypes.h>
#include "lldpsnmp_impl.h"
#include "lldp_impl.h"

/*
 * register scalar handlers. Respective handlers will be called
 * whenever `snmpd' receives requests for the OID's.
 */
int
register_scalar(void *arg)
{
	int			err;
	lldpv2_oid_desc_t	*lodp = arg;
	netsnmp_handler_registration *reg;

	reg = netsnmp_create_handler_registration(lodp->lod_name,
	    lodp->lod_handler, lodp->lod_oid, lodp->lod_oidlen,
	    lodp->lod_acl);
	if (reg == NULL)
		return (MIB_REGISTRATION_FAILED);
	if ((err = netsnmp_register_scalar(reg)) != MIB_REGISTERED_OK) {
		netsnmp_handler_registration_free(reg);
		return (err);
	}

	lodp->lod_reghandler = reg;
	return (MIB_REGISTERED_OK);
}

lldpv2_oid_desc_t *
lldpv2_oname2desc(const char *name, lldpv2_oid_desc_t *table)
{
	lldpv2_oid_desc_t	*lodp = table;

	for (; lodp->lod_name != NULL; lodp++)
		if (strcmp(name, lodp->lod_name) == 0)
			return (lodp);
	return (NULL);
}

void
init_mibgroup(lldpv2_oid_desc_t *table)
{
	lldpv2_oid_desc_t	*lodp = table;
	int			err;

	for (; lodp->lod_name != NULL; lodp++) {
		err = lodp->lod_register(lodp);
		switch (err) {
		case MIB_REGISTERED_OK:
			DEBUGMSGTL((LLDPV2_AGENTNAME, "registered oid "
			    "%s\n", lodp->lod_name));
			break;
		case MIB_DUPLICATE_REGISTRATION:
			syslog(LOG_ERR, LLDPV2_AGENTNAME
			    ": %s registration failed: duplicate "
			    "registration\n", lodp->lod_name);
			break;
		case MIB_REGISTRATION_FAILED:
			syslog(LOG_ERR, LLDPV2_AGENTNAME
			    ": %s registration failed: agent "
			    "registration failure\n", lodp->lod_name);
			break;
		default:
			syslog(LOG_ERR, LLDPV2_AGENTNAME
			    ": table %s initialization failed: "
			    "unknown reason\n", lodp->lod_name);
			break;
		}
	}
}

void
uninit_mibgroup(lldpv2_oid_desc_t *table)
{
	lldpv2_oid_desc_t *lodp = table;
	netsnmp_table_registration_info *table_info;

	for (; lodp->lod_name != NULL; lodp++) {
		if (lodp->lod_reghandler == NULL)
			continue;
		(void) netsnmp_unregister_handler(lodp->lod_reghandler);
		lodp->lod_reghandler = NULL;
		if ((table_info = lodp->lod_table_info) != NULL) {
			if (table_info->indexes != NULL)
				snmp_free_varbind(table_info->indexes);
			SNMP_FREE(table_info);
			lodp->lod_table_info = NULL;
		}
		if (lodp->lod_iter_info != NULL) {
			netsnmp_iterator_delete_table(lodp->lod_iter_info);
			lodp->lod_iter_info = NULL;
		}
	}
}

static void
free_linkids(lldp_linkid_list_t *arg)
{
	lldp_linkid_list_t	*next, *cur = arg;

	while (cur != NULL) {
		next = cur->ll_next;
		free(cur);
		cur = next;
	}
}

/* ARGSUSED */
static int
i_retrieve_linkids(dladm_handle_t dh, datalink_id_t linkid, void *arg)
{
	lldp_linkid_list_t *new, **llpp = arg;

	if ((new = calloc(1, sizeof (lldp_linkid_list_t))) == NULL) {
		syslog(LOG_ERR, LLDPV2_AGENTNAME
		    ": failed to retrieve linkid list");
		free_linkids(*llpp);
		return (DLADM_WALK_TERMINATE);
	}
	new->ll_linkid = linkid;
	new->ll_next = *llpp;
	*llpp = new;

	return (DLADM_WALK_CONTINUE);
}

/*
 * retrieves all the linkid's on which LLDP is enabled
 */
int
retrieve_lldp_linkids(lldp_linkid_list_t **llpp)
{
	lldp_agent_t	*lap;
	int		err = 0;

	*llpp = NULL;
	lldp_rw_lock(&lldp_agents_list_rwlock, LLDP_RWLOCK_READER);
	for (lap = list_head(&lldp_agents); lap != NULL;
	    lap = list_next(&lldp_agents, lap)) {
		if (i_retrieve_linkids(NULL, lap->la_linkid, llpp) ==
		    DLADM_WALK_TERMINATE) {
			err = ENOMEM;
			break;
		}
	}
	lldp_rw_unlock(&lldp_agents_list_rwlock);
	return (err);
}

/*
 * retrieve all the linkid's on the system
 */
int
retrieve_all_linkids(lldp_linkid_list_t **llpp)
{
	if (dladm_walk_datalink_id(i_retrieve_linkids, dld_handle, llpp,
	    DATALINK_CLASS_PHYS | DATALINK_CLASS_VLAN | DATALINK_CLASS_AGGR |
	    DATALINK_CLASS_VNIC, DATALINK_ANY_MEDIATYPE,
	    DLADM_OPT_ACTIVE) != DLADM_STATUS_OK) {
		return (-1);
	}
	return (0);
}

/* ARGSUSED */
void
free_context(void *arg, netsnmp_iterator_info *iinfo)
{
	free(arg);
}

/*
 * reverses the bits
 */
void
reverse_bits(void *arg, int len)
{
	uint8_t		*u8;
	uint16_t	*u16;
	uint32_t	*u32, res = 0;
	int		i;

	switch (len) {
	case 1:
		u8 = arg;
		for (i = 7; i >= 0; i--) {
			res |= ((*u8 & 0x1) << i);
			*u8 >>= 1;
		}
		*u8 = res;
		break;
	case 2:
		u16 = arg;
		for (i = 15; i >= 0; i--) {
			res |= ((*u16 & 0x1) << i);
			*u16 >>= 1;
		}
		*u16 = ntohs(res);
		break;
	case 4:
		u32 = arg;
		for (i = 31; i >= 0; i--) {
			res |= ((*u32 & 0x1) << i);
			*u32 >>= 1;
		}
		*u32 = ntohl(res);
		break;
	}
}
