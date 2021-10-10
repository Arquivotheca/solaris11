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
 * This file contains routines that are used manage TLVs (for e.g., initialize
 * TLVS, parse TLVs, write TLVs into LLDPDU, et al,.) It holds all the
 * supported tLVs in `lldp_tlv_info_t'. Please see lldp_impl.h for more
 * information on individual fields of the structure.
 */
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <unistd.h>
#include <netinet/in.h>
#include <inttypes.h>
#include <sys/vlan.h>
#include "lldp_impl.h"
#include "lldp_provider.h"
#include "dcbx_impl.h"
#include "dcbx_appln.h"
#include "dcbx_pfc.h"

static lldp_tlv_parsef_t	i_lldp_parse_end, i_lldp_parse_chassisid,
				i_lldp_parse_portid, i_lldp_parse_ttl,
				i_lldp_parse_portdescr,	i_lldp_parse_sysname,
				i_lldp_parse_sysdescr, i_lldp_parse_syscapab,
				i_lldp_parse_mgmtaddr, i_lldp_parse_maxfsz,
				i_lldp_parse_aggr, i_lldp_parse_pvid,
				i_lldp_parse_vlan, i_lldp_parse_vnic,
				i_lldp_parse_reserved_tlvs, i_lldp_parse_pfc,
				i_lldp_parse_appln, i_lldp_parse_unrec_orgspec;

static lldp_tlv_writef_t	i_lldp_write_chassisid2pdu,
				i_lldp_write_portid2pdu, i_lldp_write_ttl2pdu,
				i_lldp_write_portdescr2pdu,
				i_lldp_write_sysname2pdu,
				i_lldp_write_sysdescr2pdu,
				i_lldp_write_syscapab2pdu,
				i_lldp_write_mgmtaddr2pdu,
				i_lldp_write_maxfsz2pdu,
				i_lldp_write_end2pdu, i_lldp_write_vlan2pdu,
				i_lldp_write_vnic2pdu, i_lldp_write_pvid2pdu,
				i_lldp_write_aggr2pdu, i_lldp_write_pfc2pdu,
				i_lldp_write_appln2pdu;

static lldp_tlv_initf_t		i_lldp_init_sysdescr, i_lldp_init_sysname,
				i_lldp_init_syscapab, i_lldp_init_mgmtaddr,
				i_lldp_init_portdescr, i_lldp_init_pvid,
				i_lldp_init_aggr, i_lldp_init_vlan,
				i_lldp_init_vnic, i_lldp_init_maxfsz;

static lldp_tlv_finif_t		i_lldp_fini_sysdescr, i_lldp_fini_sysname,
				i_lldp_fini_syscapab, i_lldp_fini_mgmtaddr,
				i_lldp_fini_portdescr, i_lldp_fini_pvid,
				i_lldp_fini_aggr, i_lldp_fini_vlan,
				i_lldp_fini_vnic, i_lldp_fini_maxfsz;

/*
 * Section 9.2.7.7.4
 *
 * Check if the incoming LLDPDU changes or deletes the status/value of one or
 * more TLVs associated with a particular MSAP. We have to go through
 * all the TLVs in the PDU to check if they have changed and include them
 * in the somethingchanged nvlist, else we could have just returned when
 * we encounter the 1st change.
 */
static lldp_tlv_cmpf_t		i_lldp_cmp_sysname, i_lldp_cmp_sysdescr,
				i_lldp_cmp_portdescr, i_lldp_cmp_syscapab,
				i_lldp_cmp_mgmtaddr, i_lldp_cmp_pvid,
				i_lldp_cmp_vlan, i_lldp_cmp_aggr,
				i_lldp_cmp_vnic, i_lldp_cmp_reserved_tlvs,
				i_lldp_cmp_pfc, i_lldp_cmp_appln,
				i_lldp_cmp_maxfsz, i_lldp_cmp_ttl,
				i_lldp_cmp_unrec_orgspec_tlvs;

static int	i_lldp_cmp_unrec_tlvs(const char *, nvlist_t *, nvlist_t *,
		    nvlist_t *, nvlist_t *, nvlist_t *);

static lldp_tlv_info_t	lldp_tlv_table[] = {
	{ NULL, NULL, LLDP_TLVTYPE_END, 0, 0, NULL, NULL,
	    i_lldp_parse_end, i_lldp_write_end2pdu, NULL },

	{ NULL, LLDP_NVP_CHASSISID, LLDP_TLVTYPE_CHASSIS_ID, 0, 0, NULL, NULL,
	    i_lldp_parse_chassisid, i_lldp_write_chassisid2pdu, NULL },

	{ NULL, LLDP_NVP_PORTID, LLDP_TLVTYPE_PORT_ID, 0, 0, NULL, NULL,
	    i_lldp_parse_portid, i_lldp_write_portid2pdu, NULL },

	{ NULL, LLDP_NVP_TTL, LLDP_TLVTYPE_TTL, 0, 0, NULL, NULL,
	    i_lldp_parse_ttl, i_lldp_write_ttl2pdu, i_lldp_cmp_ttl },

	{ LLDP_BASIC_PORTDESC_TLVNAME, LLDP_NVP_PORTDESC,
	    LLDP_TLVTYPE_PORT_DESC, 0, 0, i_lldp_init_portdescr,
	    i_lldp_fini_portdescr, i_lldp_parse_portdescr,
	    i_lldp_write_portdescr2pdu, i_lldp_cmp_portdescr },

	{ LLDP_BASIC_SYSNAME_TLVNAME, LLDP_NVP_SYSNAME, LLDP_TLVTYPE_SYS_NAME,
	    0, 0, i_lldp_init_sysname, i_lldp_fini_sysname,
	    i_lldp_parse_sysname, i_lldp_write_sysname2pdu,
	    i_lldp_cmp_sysname },

	{ LLDP_BASIC_SYSDESC_TLVNAME, LLDP_NVP_SYSDESCR,
	    LLDP_TLVTYPE_SYS_DESC, 0, 0, i_lldp_init_sysdescr,
	    i_lldp_fini_sysdescr, i_lldp_parse_sysdescr,
	    i_lldp_write_sysdescr2pdu, i_lldp_cmp_sysdescr },

	{ LLDP_BASIC_SYSCAPAB_TLVNAME, LLDP_NVP_SYSCAPAB,
	    LLDP_TLVTYPE_SYS_CAPAB, 0, 0, i_lldp_init_syscapab,
	    i_lldp_fini_syscapab, i_lldp_parse_syscapab,
	    i_lldp_write_syscapab2pdu, i_lldp_cmp_syscapab },

	{ LLDP_BASIC_MGMTADDR_TLVNAME, LLDP_NVP_MGMTADDR,
	    LLDP_TLVTYPE_MGMT_ADDR, 0, 0, i_lldp_init_mgmtaddr,
	    i_lldp_fini_mgmtaddr, i_lldp_parse_mgmtaddr,
	    i_lldp_write_mgmtaddr2pdu, i_lldp_cmp_mgmtaddr },

	{ LLDP_8023_MAXFRAMESZ_TLVNAME, LLDP_NVP_MAXFRAMESZ,
	    LLDP_ORGSPECIFIC_TLVTYPE, LLDP_802dot3_OUI,
	    LLDP_802dot3OUI_MAXFRAMESZ_SUBTYPE, i_lldp_init_maxfsz,
	    i_lldp_fini_maxfsz, i_lldp_parse_maxfsz, i_lldp_write_maxfsz2pdu,
	    i_lldp_cmp_maxfsz },

	{ LLDP_8021_LINK_AGGR_TLVNAME, LLDP_NVP_AGGR, LLDP_ORGSPECIFIC_TLVTYPE,
	    LLDP_802dot1_OUI, LLDP_802dot1OUI_LINK_AGGR_SUBTYPE,
	    i_lldp_init_aggr, i_lldp_fini_aggr,
	    i_lldp_parse_aggr, i_lldp_write_aggr2pdu, i_lldp_cmp_aggr },

	{ LLDP_8021_VLAN_NAME_TLVNAME, LLDP_NVP_VLANNAME,
	    LLDP_ORGSPECIFIC_TLVTYPE, LLDP_802dot1_OUI,
	    LLDP_802dot1OUI_VLAN_NAME_SUBTYPE, i_lldp_init_vlan,
	    i_lldp_fini_vlan, i_lldp_parse_vlan, i_lldp_write_vlan2pdu,
	    i_lldp_cmp_vlan },

	{ LLDP_8021_PFC_TLVNAME, LLDP_NVP_PFC,
	    LLDP_ORGSPECIFIC_TLVTYPE, LLDP_802dot1_OUI,
	    LLDP_802dot1OUI_PFC_SUBTYPE, dcbx_pfc_tlv_init, dcbx_pfc_tlv_fini,
	    i_lldp_parse_pfc, i_lldp_write_pfc2pdu, i_lldp_cmp_pfc },

	{ LLDP_8021_APPLN_TLVNAME, LLDP_NVP_APPLN,
	    LLDP_ORGSPECIFIC_TLVTYPE, LLDP_802dot1_OUI,
	    LLDP_802dot1OUI_APPLN_SUBTYPE, dcbx_appln_tlv_init,
	    dcbx_appln_tlv_fini, i_lldp_parse_appln, i_lldp_write_appln2pdu,
	    i_lldp_cmp_appln },

	{ LLDP_8021_PVID_TLVNAME, LLDP_NVP_PVID, LLDP_ORGSPECIFIC_TLVTYPE,
	    LLDP_802dot1_OUI, LLDP_802dot1OUI_PVID_SUBTYPE,
	    i_lldp_init_pvid, i_lldp_fini_pvid,
	    i_lldp_parse_pvid, i_lldp_write_pvid2pdu, i_lldp_cmp_pvid },

	{ LLDP_VIRT_VNIC_TLVNAME, LLDP_NVP_VNICNAME,
	    LLDP_ORGSPECIFIC_TLVTYPE, LLDP_ORACLE_OUI,
	    LLDP_ORACLEOUI_VNIC_SUBTYPE, i_lldp_init_vnic, i_lldp_fini_vnic,
	    i_lldp_parse_vnic, i_lldp_write_vnic2pdu, i_lldp_cmp_vnic },
};

/*
 * handles TLV's that are in the range (9, 126), i.e. reserved TLV's
 */
static lldp_tlv_info_t	lldp_reserved_tlv = \
	{ NULL, NULL, LLDP_TLVTYPE_RESERVED, 0, 0, NULL, NULL,
	    i_lldp_parse_reserved_tlvs, NULL, i_lldp_cmp_reserved_tlvs };

static lldp_tlv_info_t	lldp_unrec_orgspec_tlv = \
	{ NULL, NULL, LLDP_ORGSPECIFIC_TLVTYPE, 0, 0, NULL, NULL,
	    i_lldp_parse_unrec_orgspec, NULL, i_lldp_cmp_unrec_orgspec_tlvs };

lldp_tlv_info_t *
lldp_get_tlvinfo(uint8_t tlvtype, uint32_t oui, uint8_t stype)
{
	lldp_tlv_info_t	*infop;
	int i;

	if (tlvtype >= 9 && tlvtype <= 126) {
		/* special TLV's from future handle with care */
		return (&lldp_reserved_tlv);
	} else {
		for (i = 0; i < A_CNT(lldp_tlv_table); i++) {
			infop = &lldp_tlv_table[i];
			if (tlvtype == infop->lti_type &&
			    oui == infop->lti_oui &&
			    stype == infop->lti_stype) {
				return (infop);
			}
		}
		if (tlvtype == LLDP_ORGSPECIFIC_TLVTYPE)
			return (&lldp_unrec_orgspec_tlv);
	}
	return (NULL);
}

int
lldp_add_chassisid2nvlist(lldp_chassisid_t *cid, nvlist_t *tlv_nvl)
{
	nvlist_t	*chassisid_nvl;
	int		err;

	if ((err = nvlist_alloc(&chassisid_nvl, NV_UNIQUE_NAME, 0)) != 0)
		return (err);

	if ((err = nvlist_add_uint8(chassisid_nvl, LLDP_NVP_CHASSISID_TYPE,
	    cid->lc_subtype)) != 0) {
		goto ret;
	}
	if ((err = nvlist_add_byte_array(chassisid_nvl,
	    LLDP_NVP_CHASSISID_VALUE, cid->lc_cid, cid->lc_cidlen)) != 0) {
		goto ret;
	}
	err = nvlist_add_nvlist(tlv_nvl, LLDP_NVP_CHASSISID, chassisid_nvl);
ret:
	nvlist_free(chassisid_nvl);
	return (err);
}

/*
 * Chassis ID TLV
 *  +--------+----------------+--------------+-------------+
 *  |	TLV  | TLV information|  Chassis ID  | Chassis ID  |
 *  |   Type |   string len   |  TLV subtype |             |
 *  +--------+----------------+--------------+-------------+
 *	7bits      9 bits        8 bits      1 to 255 octets
 */
/* ARGSUSED */
static int
i_lldp_parse_chassisid(lldp_agent_t *lap, lldp_tlv_t *tlv, nvlist_t *tlv_nvl)
{
	lldp_chassisid_t	cid;
	int			err;

	if (nvlist_exists(tlv_nvl, LLDP_NVP_CHASSISID))
		return (EPROTO);

	if ((err = lldp_tlv2chassisid(tlv, &cid)) != 0)
		return (err);

	return (lldp_add_chassisid2nvlist(&cid, tlv_nvl));
}

static int
i_lldp_write_chassisid2pdu(void *argp, uint8_t *lldpdu, size_t pdusize,
    size_t *msglen)
{
	lldp_agent_t		*lap = argp;
	lldp_chassisid_t	cid;
	nvlist_t		*nvl = NULL;
	size_t			tlvlen = *msglen;
	int			err;

	(void) nvlist_lookup_nvlist(lap->la_local_mib, lap->la_msap, &nvl);
	if ((err = lldp_nvlist2chassisid(nvl, &cid)) == 0)
		err = lldp_chassisid2pdu(&cid, lldpdu, pdusize, msglen);
	if (err == 0) {
		LLDP_TLV_SEND(LLDP_TLVTYPE_CHASSIS_ID, *msglen - tlvlen, 0, 0);
	}
	return (err);
}

int
lldp_add_portid2nvlist(lldp_portid_t *pid, nvlist_t *tlv_nvl)
{
	nvlist_t	*portid_nvl;
	int		err;

	if ((err = nvlist_alloc(&portid_nvl, NV_UNIQUE_NAME, 0)) != 0)
		return (err);

	if ((err = nvlist_add_uint8(portid_nvl, LLDP_NVP_PORTID_TYPE,
	    pid->lp_subtype)) != 0) {
		goto ret;
	}
	if ((err = nvlist_add_byte_array(portid_nvl, LLDP_NVP_PORTID_VALUE,
	    pid->lp_pid, pid->lp_pidlen)) != 0) {
		goto ret;
	}
	err = nvlist_add_nvlist(tlv_nvl, LLDP_NVP_PORTID, portid_nvl);
ret:
	nvlist_free(portid_nvl);
	return (err);
}

/*
 * Port ID TLV
 *  +--------+----------------+--------------+-------------+
 *  |	TLV  | TLV information|  Port ID     | Port ID     |
 *  |   Type |   string len   |  TLV subtype |             |
 *  +--------+----------------+--------------+-------------+
 *    7bits      9 bits           8 bits      1 to 255 octets
 */
/* ARGSUSED */
int
i_lldp_parse_portid(lldp_agent_t *lap, lldp_tlv_t *tlv, nvlist_t *tlv_nvl)
{
	lldp_portid_t	pid;
	int		err;

	if (nvlist_exists(tlv_nvl, LLDP_NVP_PORTID))
		return (EPROTO);

	if ((err = lldp_tlv2portid(tlv, &pid)) != 0)
		return (err);

	return (lldp_add_portid2nvlist(&pid, tlv_nvl));
}

static int
i_lldp_write_portid2pdu(void *argp, uint8_t *lldpdu, size_t pdusize,
    size_t *msglen)
{
	lldp_portid_t	pid;
	lldp_agent_t	*lap = argp;
	nvlist_t	*nvl = NULL;
	int		err;
	size_t		tlvlen = *msglen;

	(void) nvlist_lookup_nvlist(lap->la_local_mib, lap->la_msap,
	    &nvl);
	if ((err = lldp_nvlist2portid(nvl, &pid)) == 0)
		err = lldp_portid2pdu(&pid, lldpdu, pdusize, msglen);
	if (err == 0) {
		LLDP_TLV_SEND(LLDP_TLVTYPE_PORT_ID, *msglen - tlvlen, 0, 0);
	}
	return (err);
}

int
lldp_add_sysname2nvlist(char *sysname, nvlist_t *tlv_nvl)
{
	return (nvlist_add_string(tlv_nvl, LLDP_NVP_SYSNAME, sysname));
}

int
lldp_add_sysdescr2nvlist(char *sysdesc, nvlist_t *tlv_nvl)
{
	return (nvlist_add_string(tlv_nvl, LLDP_NVP_SYSDESCR, sysdesc));
}

int
lldp_add_portdescr2nvlist(char *portdesc, nvlist_t *tlv_nvl)
{
	return (nvlist_add_string(tlv_nvl, LLDP_NVP_PORTDESC, portdesc));
}

int
lldp_add_ttl2nvlist(uint16_t ttl, nvlist_t *tlv_nvl)
{
	return (nvlist_add_uint16(tlv_nvl, LLDP_NVP_TTL, ttl));
}

/*
 * Time to Live TLV
 *  +--------+----------------+----------------------------+
 *  |	TLV  | TLV information|   time to live (TTL)       |
 *  |   Type |   string len   |                            |
 *  +--------+----------------+----------------------------+
 *	7bits      9 bits            2 octets
 */
/* ARGSUSED */
static int
i_lldp_parse_ttl(lldp_agent_t *lap, lldp_tlv_t *tlv, nvlist_t *tlv_nvl)
{
	uint16_t	rxTTL;
	int		err;

	if (nvlist_exists(tlv_nvl, LLDP_NVP_TTL))
		return (EPROTO);

	if ((err = lldp_tlv2ttl(tlv, &rxTTL)) != 0)
		return (err);

	if (nvlist_add_uint16(tlv_nvl, LLDP_NVP_TTL, rxTTL) != 0)
		return (ENOMEM);

	return (0);
}

/* ARGSUSED */
static int
i_lldp_cmp_ttl(nvlist_t *cur_nvl, nvlist_t *tlv_nvl,
    nvlist_t *added_tlvnvl, nvlist_t *deleted_tlvnvl, nvlist_t *modified_tlvnvl)
{
	uint16_t	cur_ttl, new_ttl;
	int		ret = 1;

	/*
	 * ttl is a mandatory TLV and is guranteed to be present in
	 * `cur_nvl' and `tlv_nvl', when we are here.
	 */
	(void) lldp_nvlist2ttl(cur_nvl, &cur_ttl);
	(void) lldp_nvlist2ttl(tlv_nvl, &new_ttl);
	if (cur_ttl != new_ttl) {
		if (lldp_add_ttl2nvlist(new_ttl, modified_tlvnvl) != 0)
			ret = -1;
	} else {
		ret = 0;
	}

	return (ret);
}

static int
i_lldp_write_ttl2pdu(void *argp, uint8_t *lldpdu, size_t pdusize,
    size_t *msglen)
{
	uint16_t	ttl;
	size_t		tlvlen = *msglen;
	int		err;

	ttl = htons(*(uint16_t *)argp);
	if ((err = lldp_ttl2pdu(ttl, lldpdu, pdusize, msglen)) == 0) {
		LLDP_TLV_SEND(LLDP_TLVTYPE_TTL, *msglen - tlvlen, 0, 0);
	}
	return (err);
}

static int
i_lldp_cmp_strtlv(const char *nvpname, nvlist_t *cur_nvl, nvlist_t *tlv_nvl,
    nvlist_t *added_tlvnvl, nvlist_t *deleted_tlvnvl, nvlist_t *modified_tlvnvl)
{
	char	*cur_str = NULL, *new_str = NULL;
	int	ret = 1;

	(void) nvlist_lookup_string(cur_nvl, nvpname, &cur_str);
	(void) nvlist_lookup_string(tlv_nvl, nvpname, &new_str);

	assert(cur_str != NULL || new_str != NULL);
	if (cur_str == NULL) {
		if (nvlist_add_string(added_tlvnvl, nvpname, new_str) != 0)
			ret = -1;
	} else if (new_str == NULL) {
		if (nvlist_add_string(deleted_tlvnvl, nvpname, "") != 0)
			ret = -1;
	} else if (strcmp(cur_str, new_str) != 0) {
		if (nvlist_add_string(modified_tlvnvl, nvpname, new_str) != 0)
			ret = -1;
	} else {
		ret = 0;
	}

	return (ret);
}

/*
 * Port Description TLV
 *  +--------+----------------+----------------------------+
 *  |	TLV  | TLV information|   port description	   |
 *  |   Type |   string len   |                            |
 *  +--------+----------------+----------------------------+
 *	7bits      9 bits          0 to 255 octets
 */
/* ARGSUSED */
static int
i_lldp_parse_portdescr(lldp_agent_t *lap, lldp_tlv_t *tlv, nvlist_t *tlv_nvl)
{
	char	portdescr[LLDP_MAX_PORTDESCLEN];
	int	err;

	if (nvlist_exists(tlv_nvl, LLDP_NVP_PORTDESC))
		return (EEXIST);

	(void) memset(portdescr, 0, sizeof (portdescr));
	if ((err = lldp_tlv2portdescr(tlv, portdescr)) != 0)
		return (err);

	return (lldp_add_portdescr2nvlist(portdescr, tlv_nvl));
}

static int
i_lldp_cmp_portdescr(nvlist_t *cur_nvl, nvlist_t *tlv_nvl,
    nvlist_t *added_tlvnvl, nvlist_t *deleted_tlvnvl, nvlist_t *modified_tlvnvl)
{
	return (i_lldp_cmp_strtlv(LLDP_NVP_PORTDESC, cur_nvl, tlv_nvl,
	    added_tlvnvl, deleted_tlvnvl, modified_tlvnvl));
}

static int
i_lldp_write_portdescr2pdu(void *argp, uint8_t *lldpdu, size_t pdusize,
    size_t *msglen)
{
	lldp_agent_t	*lap = argp;
	nvlist_t	*nvl = NULL;
	char		*descr;
	int		err;
	size_t		tlvlen = *msglen;

	(void) nvlist_lookup_nvlist(lap->la_local_mib, lap->la_msap, &nvl);
	if ((err = nvlist_lookup_string(nvl, LLDP_NVP_PORTDESC, &descr)) == 0)
		err = lldp_portdescr2pdu(descr, lldpdu, pdusize, msglen);
	if (err == 0) {
		LLDP_TLV_SEND(LLDP_TLVTYPE_PORT_DESC, *msglen - tlvlen, 0, 0);
	}
	return (err);
}

static int
i_lldp_init_portdescr(lldp_agent_t *lap, nvlist_t *nvl)
{
	return (lldp_add_portdescr2nvlist(lap->la_linkname, nvl));
}

static void
i_lldp_fini_portdescr(lldp_agent_t *lap)
{
	nvlist_t	*nvl;

	if (lldp_get_nested_nvl(lap->la_local_mib, lap->la_msap, NULL, NULL,
	    &nvl) == 0) {
		(void) nvlist_remove(nvl, LLDP_NVP_PORTDESC, DATA_TYPE_STRING);
	}
}

/*
 * System Name TLV
 *  +--------+----------------+----------------------------+
 *  |	TLV  | TLV information|   system name              |
 *  |   Type |   string len   |                            |
 *  +--------+----------------+----------------------------+
 *	7bits      9 bits          0 to 255 octets
 */
/* ARGSUSED */
static int
i_lldp_parse_sysname(lldp_agent_t *lap, lldp_tlv_t *tlv, nvlist_t *tlv_nvl)
{
	char	sysname[LLDP_MAX_SYSNAMELEN];
	int	err;

	if (nvlist_exists(tlv_nvl, LLDP_NVP_SYSNAME))
		return (EEXIST);

	(void) memset(sysname, 0, sizeof (sysname));
	if ((err = lldp_tlv2sysname(tlv, sysname)) != 0)
		return (err);

	return (lldp_add_sysname2nvlist(sysname, tlv_nvl));
}

/* ARGSUSED */
static int
i_lldp_init_sysname(lldp_agent_t *lap, nvlist_t *nvl)
{
	char	sysname[LLDP_MAX_SYSNAMELEN];

	lldp_get_sysname(sysname, sizeof (sysname));
	return (lldp_add_sysname2nvlist(sysname, nvl));
}

static void
i_lldp_fini_sysname(lldp_agent_t *lap)
{
	nvlist_t	*nvl;

	if (lldp_get_nested_nvl(lap->la_local_mib, lap->la_msap, NULL, NULL,
	    &nvl) == 0) {
		(void) nvlist_remove(nvl, LLDP_NVP_SYSNAME, DATA_TYPE_STRING);
	}
}

static int
i_lldp_cmp_sysname(nvlist_t *cur_nvl, nvlist_t *tlv_nvl,
    nvlist_t *added_tlvnvl, nvlist_t *deleted_tlvnvl, nvlist_t *modified_tlvnvl)
{
	return (i_lldp_cmp_strtlv(LLDP_NVP_SYSNAME, cur_nvl, tlv_nvl,
	    added_tlvnvl, deleted_tlvnvl, modified_tlvnvl));
}

static int
i_lldp_write_sysname2pdu(void *argp, uint8_t *lldpdu, size_t pdusize,
    size_t *msglen)
{
	lldp_agent_t	*lap = argp;
	nvlist_t	*nvl = NULL;
	char		*sysname;
	int		err;
	size_t		tlvlen = *msglen;

	(void) nvlist_lookup_nvlist(lap->la_local_mib, lap->la_msap, &nvl);
	if ((err = nvlist_lookup_string(nvl, LLDP_NVP_SYSNAME, &sysname)) == 0)
		err = lldp_sysname2pdu(sysname, lldpdu, pdusize, msglen);
	if (err == 0) {
		LLDP_TLV_SEND(LLDP_TLVTYPE_SYS_NAME, *msglen - tlvlen, 0, 0);
	}
	return (err);
}

/*
 * System Description TLV
 *  +--------+----------------+----------------------------+
 *  |	TLV  | TLV information|   system description       |
 *  |   Type |   string len   |                            |
 *  +--------+----------------+----------------------------+
 *	7bits      9 bits          0 to 255 octets
 */
/* ARGSUSED */
static int
i_lldp_parse_sysdescr(lldp_agent_t *lap, lldp_tlv_t *tlv, nvlist_t *tlv_nvl)
{
	char	sysdescr[LLDP_MAX_SYSDESCLEN];
	int	err;

	if (nvlist_exists(tlv_nvl, LLDP_NVP_SYSDESCR))
		return (EEXIST);

	(void) memset(sysdescr, 0, sizeof (sysdescr));
	if ((err = lldp_tlv2sysdescr(tlv, sysdescr)) != 0)
		return (err);

	return (lldp_add_sysdescr2nvlist(sysdescr, tlv_nvl));
}

/* ARGSUSED */
static int
i_lldp_init_sysdescr(lldp_agent_t *lap, nvlist_t *nvl)
{
	char	sysdescr[LLDP_MAX_SYSDESCLEN];

	lldp_get_sysdesc(sysdescr, sizeof (sysdescr));
	return (lldp_add_sysdescr2nvlist(sysdescr, nvl));
}

static void
i_lldp_fini_sysdescr(lldp_agent_t *lap)
{
	nvlist_t	*nvl;

	if (lldp_get_nested_nvl(lap->la_local_mib, lap->la_msap, NULL, NULL,
	    &nvl) == 0) {
		(void) nvlist_remove(nvl, LLDP_NVP_SYSDESCR, DATA_TYPE_STRING);
	}
}

static int
i_lldp_cmp_sysdescr(nvlist_t *cur_nvl, nvlist_t *tlv_nvl,
    nvlist_t *added_tlvnvl, nvlist_t *deleted_tlvnvl, nvlist_t *modified_tlvnvl)

{
	return (i_lldp_cmp_strtlv(LLDP_NVP_SYSDESCR, cur_nvl, tlv_nvl,
	    added_tlvnvl, deleted_tlvnvl, modified_tlvnvl));
}

static int
i_lldp_write_sysdescr2pdu(void *argp, uint8_t *lldpdu, size_t pdusize,
    size_t *msglen)
{
	lldp_agent_t	*lap = argp;
	nvlist_t	*nvl = NULL;
	char		*descr;
	int		err;
	size_t		tlvlen = *msglen;

	(void) nvlist_lookup_nvlist(lap->la_local_mib, lap->la_msap, &nvl);
	if ((err = nvlist_lookup_string(nvl, LLDP_NVP_SYSDESCR, &descr)) == 0)
		err = lldp_sysdescr2pdu(descr, lldpdu, pdusize, msglen);
	if (err == 0) {
		LLDP_TLV_SEND(LLDP_TLVTYPE_SYS_DESC, *msglen - tlvlen, 0, 0);
	}
	return (err);
}

int
lldp_add_syscapab2nvlist(lldp_syscapab_t *scp, nvlist_t *tlv_nvl)
{
	nvlist_t	*sys_capab_nvl;
	int		err;

	if ((err = nvlist_alloc(&sys_capab_nvl, NV_UNIQUE_NAME, 0)) != 0)
		return (err);

	if ((err = nvlist_add_uint16(sys_capab_nvl, LLDP_NVP_SUPPORTED_SYSCAPAB,
	    scp->ls_sup_syscapab)) != 0) {
		goto ret;
	}
	if ((err = nvlist_add_uint16(sys_capab_nvl, LLDP_NVP_ENABLED_SYSCAPAB,
	    scp->ls_enab_syscapab)) != 0) {
		goto ret;
	}
	err = nvlist_add_nvlist(tlv_nvl, LLDP_NVP_SYSCAPAB, sys_capab_nvl);
ret:
	nvlist_free(sys_capab_nvl);
	return (err);
}

/*
 * System Capabilities TLV
 *  +--------+----------------+--------------+---------------+
 *  |	TLV  | TLV information| system       | enabled       |
 *  |   Type |   string len   | capabilities | capabilities  |
 *  +--------+----------------+--------------+---------------+
 *	7bits      9 bits        2 octets         2 octets
 */
/* ARGSUSED */
static int
i_lldp_parse_syscapab(lldp_agent_t *lap, lldp_tlv_t *tlv, nvlist_t *tlv_nvl)
{
	lldp_syscapab_t	sc;
	int		err;

	if (nvlist_exists(tlv_nvl, LLDP_NVP_SYSCAPAB))
		return (EEXIST);

	if ((err = lldp_tlv2syscapab(tlv, &sc)) != 0)
		return (err);

	/*
	 * Note: Enabled capablities must be subset of supported system
	 * capabilities and if StationOnly capability is enabled no other
	 * capability should be enabled
	 */
	if ((sc.ls_sup_syscapab|sc.ls_enab_syscapab) != sc.ls_sup_syscapab ||
	    ((sc.ls_enab_syscapab & LLDP_SYSCAPAB_STATION_ONLY) &&
	    sc.ls_enab_syscapab != LLDP_SYSCAPAB_STATION_ONLY)) {
		/* We discard the TLV */
		return (EINVAL);
	}
	return (lldp_add_syscapab2nvlist(&sc, tlv_nvl));
}

/* ARGSUSED */
static int
i_lldp_init_syscapab(lldp_agent_t *lap, nvlist_t *nvl)
{
	nvlist_t	*scnvl = NULL;
	int		err;

	lldp_rw_lock(&lldp_sysinfo_rwlock, LLDP_RWLOCK_READER);
	err = nvlist_lookup_nvlist(lldp_sysinfo, LLDP_NVP_SYSCAPAB, &scnvl);
	lldp_rw_unlock(&lldp_sysinfo_rwlock);
	if (err == 0)
		err = nvlist_add_nvlist(nvl, LLDP_NVP_SYSCAPAB, scnvl);
	return (err);
}

static void
i_lldp_fini_syscapab(lldp_agent_t *lap)
{
	nvlist_t	*nvl;

	if (nvlist_lookup_nvlist(lap->la_local_mib, lap->la_msap, &nvl) == 0)
		(void) nvlist_remove(nvl, LLDP_NVP_SYSCAPAB, DATA_TYPE_NVLIST);
}

static int
i_lldp_cmp_syscapab(nvlist_t *cur_nvl, nvlist_t *tlv_nvl,
    nvlist_t *added_tlvnvl, nvlist_t *deleted_tlvnvl, nvlist_t *modified_tlvnvl)
{
	lldp_syscapab_t	cur_sc, new_sc;
	int		cur_err, new_err;
	int		ret = 1;

	cur_err = lldp_nvlist2syscapab(cur_nvl, &cur_sc);
	if (cur_err != 0 && cur_err != ENOENT)
		return (-1);
	new_err = lldp_nvlist2syscapab(tlv_nvl, &new_sc);
	if (new_err != 0 && new_err != ENOENT)
		return (-1);

	assert(cur_err != ENOENT || new_err != ENOENT);
	if (cur_err == ENOENT) {
		if (lldp_add_syscapab2nvlist(&new_sc, added_tlvnvl) != 0)
			ret = -1;
	} else if (new_err == ENOENT) {
		if (nvlist_add_string(deleted_tlvnvl,
		    LLDP_NVP_SYSCAPAB, "") != 0)
			ret = -1;
	} else if (cur_sc.ls_sup_syscapab != new_sc.ls_sup_syscapab ||
	    cur_sc.ls_enab_syscapab != new_sc.ls_enab_syscapab) {
		if (lldp_add_syscapab2nvlist(&new_sc, modified_tlvnvl) != 0)
			ret = -1;
	} else {
		ret = 0;
	}

	return (ret);
}

/* ARGSUSED */
static int
i_lldp_write_syscapab2pdu(void *argp, uint8_t *lldpdu, size_t pdusize,
    size_t *msglen)
{
	lldp_agent_t	*lap = argp;
	lldp_syscapab_t	sc;
	nvlist_t	*nvl = NULL;
	size_t		tlvlen = *msglen;
	int		err;

	(void) nvlist_lookup_nvlist(lap->la_local_mib, lap->la_msap, &nvl);
	if ((err = lldp_nvlist2syscapab(nvl, &sc)) != 0)
		return (err);

	/*
	 * if the administrator has not selected the capabilities then
	 * there is nothing to send.
	 */
	if (sc.ls_sup_syscapab == 0)
		return (0);
	if ((err = lldp_syscapab2pdu(&sc, lldpdu, pdusize, msglen)) == 0) {
		LLDP_TLV_SEND(LLDP_TLVTYPE_SYS_CAPAB, *msglen - tlvlen, 0, 0);
	}
	return (err);
}

int
lldp_add_mgmtaddr2nvlist(lldp_mgmtaddr_t *maddr, nvlist_t *tlv_nvl)
{
	char		name[LLDP_STRSIZE];
	int		err;
	nvlist_t	*nvl, *mgmtnvl;

	(void) lldp_bytearr2hexstr(maddr->lm_addr, maddr->lm_addrlen, name,
	    sizeof (name));
	if ((err = lldp_create_nested_nvl(tlv_nvl, LLDP_NVP_MGMTADDR, NULL,
	    NULL, &nvl)) != 0) {
		return (err);
	}

	if (nvlist_exists(nvl, name))
		return (EEXIST);
	if ((err = nvlist_alloc(&mgmtnvl, NV_UNIQUE_NAME, 0)) != 0)
		return (err);
	if ((err = nvlist_add_uint8(mgmtnvl, LLDP_NVP_MGMTADDRTYPE,
	    maddr->lm_subtype)) != 0)
		goto ret;
	if ((err = nvlist_add_byte_array(mgmtnvl, LLDP_NVP_MGMTADDRVALUE,
	    maddr->lm_addr, maddr->lm_addrlen)) != 0)
		goto ret;
	if ((err = nvlist_add_uint8(mgmtnvl, LLDP_NVP_MGMTADDR_IFTYPE,
	    maddr->lm_iftype)) != 0)
		goto ret;
	if ((err = nvlist_add_uint32(mgmtnvl, LLDP_NVP_MGMTADDR_IFNUM,
	    maddr->lm_ifnumber)) != 0)
		goto ret;
	if ((err = nvlist_add_byte_array(mgmtnvl, LLDP_NVP_MGMTADDR_OIDSTR,
	    maddr->lm_oid, maddr->lm_oidlen)) != 0)
		goto ret;
	err = nvlist_add_nvlist(nvl, name, mgmtnvl);
ret:
	nvlist_free(mgmtnvl);
	return (err);
}

/*
 * Management Address TLV
 *  +-----+-------+-------+-------+--------+-----------+----------+------+----+
 *  | TLV | TLV   | mgmt. | mgmt  | mgmt   | interface |interface | OID  |    |
 *  | Type| info. |address|address|address | numbering | number   |length| OID|
 *  |     |length |length |subtype|        |  subtype  |          |      |    |
 *  +-----+-------+-------+-------+--------+-----------+----------+------+----+
 *   7bits  9bits    1        1      1-31        1          4         1   0-128
 */
/* ARGSUSED */
static int
i_lldp_parse_mgmtaddr(lldp_agent_t *lap, lldp_tlv_t *tlv, nvlist_t *tlv_nvl)
{
	lldp_mgmtaddr_t	maddr;
	int		err;

	if ((err = lldp_tlv2mgmtaddr(tlv, &maddr)) != 0)
		return (err);

	/*
	 * In a properly formed Management Address TLV, the TLV information
	 * string length is equal to: (management address string length +
	 * OID string length + 8)
	 */
	if (tlv->lt_len != (maddr.lm_addrlen + maddr.lm_oidlen + 8))
		return (EPROTO);

	return (lldp_add_mgmtaddr2nvlist(&maddr, tlv_nvl));
}

/* ARGSUSED */
static int
i_lldp_init_mgmtaddr(lldp_agent_t *lap, nvlist_t *nvl)
{
	nvlist_t	*mnvl = NULL;
	int		err;

	lldp_rw_lock(&lldp_sysinfo_rwlock, LLDP_RWLOCK_READER);
	err = nvlist_lookup_nvlist(lldp_sysinfo, LLDP_NVP_MGMTADDR, &mnvl);
	lldp_rw_unlock(&lldp_sysinfo_rwlock);
	if (err == 0)
		err = nvlist_add_nvlist(nvl, LLDP_NVP_MGMTADDR, mnvl);
	return (err);
}

static void
i_lldp_fini_mgmtaddr(lldp_agent_t *lap)
{
	nvlist_t	*nvl = NULL;

	if (nvlist_lookup_nvlist(lap->la_local_mib, lap->la_msap, &nvl) == 0)
		(void) nvlist_remove(nvl, LLDP_NVP_MGMTADDR, DATA_TYPE_NVLIST);
}

static int
i_lldp_cmp_mgmtaddr(nvlist_t *cur_nvl, nvlist_t *tlv_nvl,
    nvlist_t *added_tlvnvl, nvlist_t *deleted_tlvnvl, nvlist_t *modified_tlvnvl)
{
	int		cur_count = 0, new_count = 0, ret = 1;
	nvlist_t	*cmnvl, *nmnvl;

	if (nvlist_lookup_nvlist(cur_nvl, LLDP_NVP_MGMTADDR, &cmnvl) == 0)
		cur_count = lldp_nvlist_nelem(cmnvl);

	if (nvlist_lookup_nvlist(tlv_nvl, LLDP_NVP_MGMTADDR, &nmnvl) == 0)
		new_count = lldp_nvlist_nelem(nmnvl);

	if (cur_count == 0 && new_count == 0)
		return (-1);
	if (cur_count == 0) {
		if (nvlist_add_nvlist(added_tlvnvl, LLDP_NVP_MGMTADDR,
		    nmnvl) != 0) {
			ret = -1;
		}
	} else if (new_count == 0) {
		if (nvlist_add_string(deleted_tlvnvl, LLDP_NVP_MGMTADDR,
		    "") != 0) {
			ret = -1;
		}
	} else if (cur_count != new_count || !lldp_nvl_similar(cmnvl, nmnvl)) {
		if (nvlist_add_nvlist(modified_tlvnvl, LLDP_NVP_MGMTADDR,
		    nmnvl) != 0) {
			ret = -1;
		}
	} else {
		ret = 0;
	}
	return (ret);
}

static int
i_lldp_write_mgmtaddr2pdu(void *argp, uint8_t *lldpdu, size_t pdusize,
    size_t *msglen)
{
	lldp_agent_t	*lap = argp;
	nvlist_t	*nvl = NULL;
	size_t		tlvlen = *msglen;
	lldp_mgmtaddr_t	*maddr, *maddrp;
	int		 i, count;
	int		err;

	(void) nvlist_lookup_nvlist(lap->la_local_mib, lap->la_msap, &nvl);
	err = lldp_nvlist2mgmtaddr(nvl, NULL, &maddr, &count);
	if (err != 0 && err != ENOENT)
		return (err);

	/*
	 * Frome IEEE 802.1AB-2009, section 8.5.9.4, if no management
	 * address is available, the MAC address for the port need to
	 * be sent.
	 */
	if (err == ENOENT) {
		err = lldp_sysport_mgmtaddr2pdu(lap->la_physaddr,
		    lap->la_physaddrlen, lap->la_linkid,
		    lldpdu, pdusize, msglen);
	} else {
		size_t	used;

		maddrp = maddr;
		for (i = 0; i < count; i++) {
			err = lldp_mgmtaddr2pdu(maddrp, lldpdu, pdusize,
			    msglen);
			if (err != 0)
				break;
			used = *msglen - tlvlen;
			lldpdu += used;
			pdusize -= used;
			LLDP_TLV_SEND(LLDP_TLVTYPE_MGMT_ADDR, used, 0, 0);
			tlvlen = *msglen;
			maddrp++;
		}
	}
	free(maddr);
	return (err);
}

/*
 * End of LLDPDU TLV
 *  +--------+----------------+
 *  |	TLV  | TLV information|
 *  |   Type |   string len   |
 *  +--------+----------------+
 *	7bits      9 bits
 */
/* ARGSUSED */
static int
i_lldp_parse_end(lldp_agent_t *lap, lldp_tlv_t *tlv, nvlist_t *tlv_nvl)
{
	return (0);
}

/* ARGSUSED */
static int
i_lldp_write_end2pdu(void *argp, uint8_t *lldpdu, size_t pdusize,
    size_t *msglen)
{
	size_t	tlvlen = *msglen;
	int	err;

	if (tlvlen > pdusize)
		return (ENOBUFS);

	if ((err = lldp_end2pdu(lldpdu, pdusize, msglen)) == 0) {
		LLDP_TLV_SEND(LLDP_TLVTYPE_END, *msglen - tlvlen, 0, 0);
	}
	return (err);
}

/*
 * Maximum Frame Size TLV
 *  +--------+----------------+--------------+-------------+---------------+
 *  |	TLV  | TLV information|  802.3OUI    |    802.3    | Maximum 802.3 |
 *  |   Type |   string len   |  00-12-0F    |   subtype   |   Frame size  |
 *  +--------+----------------+--------------+-------------+---------------+
 *    7bits      9 bits          3 octets         1 octet      2 octets
 */
/* ARGSUSED */
static int
i_lldp_parse_maxfsz(lldp_agent_t *lap, lldp_tlv_t *tlv, nvlist_t *tlv_nvl)
{
	uint16_t	fsz;
	nvlist_t	*fnvl;
	int		err;

	if ((err = lldp_get_nested_nvl(tlv_nvl, LLDP_NVP_ORGANIZATION,
	    LLDP_8023_OUI_LIST, LLDP_NVP_MAXFRAMESZ, &fnvl)) == 0) {
		/* Duplicate TLV. We discard it and make progress */
		return (EEXIST);
	}

	if ((err = lldp_tlv2maxfsz(tlv, &fsz)) == 0)
		err = lldp_add_maxfsz2nvlist(fsz, tlv_nvl);
	return (err);
}

static int
i_lldp_cmp_maxfsz(nvlist_t *cur_nvl, nvlist_t *tlv_nvl,
    nvlist_t *added_tlvnvl, nvlist_t *deleted_tlvnvl, nvlist_t *modified_tlvnvl)
{
	uint16_t	cur_fsz, new_fsz;
	int		cur_err, new_err, ret = 1;

	cur_err = lldp_nvlist2maxfsz(cur_nvl, &cur_fsz);
	if (cur_err != 0 && cur_err != ENOENT)
		return (-1);
	new_err = lldp_nvlist2maxfsz(tlv_nvl, &new_fsz);
	if (new_err != 0 && new_err != ENOENT)
		return (-1);

	assert(cur_err != ENOENT || new_err != ENOENT);
	if (cur_err == ENOENT) {
		if (lldp_add_maxfsz2nvlist(new_fsz, added_tlvnvl) != 0)
			ret = -1;
	} else if (new_err == ENOENT) {
		if (nvlist_add_string(deleted_tlvnvl, LLDP_NVP_MAXFRAMESZ,
		    "") != 0)
			ret = -1;
	} else if (cur_fsz != new_fsz) {
		if (lldp_add_maxfsz2nvlist(new_fsz, modified_tlvnvl) != 0)
			ret = -1;
	} else {
		ret = 0;
	}

	return (ret);
}

static int
i_lldp_write_maxfsz2pdu(void *argp, uint8_t *lldpdu, size_t pdusize,
    size_t *msglen)
{
	lldp_agent_t	*lap = argp;
	size_t		tlvlen = *msglen;
	nvlist_t	*nvl = NULL;
	uint16_t	fsz;
	int		err;

	(void) nvlist_lookup_nvlist(lap->la_local_mib, lap->la_msap, &nvl);
	if ((err = lldp_nvlist2maxfsz(nvl, &fsz)) == 0)
		err = lldp_maxfsz2pdu(fsz, lldpdu, pdusize, msglen);
	if (err == 0) {
		LLDP_TLV_SEND(LLDP_ORGSPECIFIC_TLVTYPE, *msglen - tlvlen,
		    LLDP_802dot3_OUI, LLDP_802dot3OUI_MAXFRAMESZ_SUBTYPE);
	}
	return (err);
}

static int
i_lldp_init_maxfsz(lldp_agent_t *lap, nvlist_t *nvl)
{
	return (lldp_add_maxfsz2nvlist(lap->la_maxfsz, nvl));
}

static void
i_lldp_fini_maxfsz(lldp_agent_t *lap)
{
	nvlist_t	*ouinvl;

	if (lldp_get_nested_nvl(lap->la_local_mib, lap->la_msap,
	    LLDP_NVP_ORGANIZATION, LLDP_8023_OUI_LIST, &ouinvl) == 0) {
		(void) nvlist_remove(ouinvl, LLDP_NVP_MAXFRAMESZ,
		    DATA_TYPE_NVLIST);
	}
}

int
lldp_add_maxfsz2nvlist(uint16_t fsz, nvlist_t *tlv_nvl)
{
	nvlist_t	*fnvl = NULL, *ouinvl = NULL;
	int		err;

	if ((err = lldp_create_nested_nvl(tlv_nvl, LLDP_NVP_ORGANIZATION,
	    LLDP_8023_OUI_LIST, NULL, &ouinvl)) != 0)
		return (err);

	if ((err = nvlist_alloc(&fnvl, NV_UNIQUE_NAME, 0)) != 0)
		return (err);

	if ((err = nvlist_add_uint16(fnvl, LLDP_NVP_MAXFRAMESZ, fsz)) == 0)
		err = nvlist_add_nvlist(ouinvl, LLDP_NVP_MAXFRAMESZ, fnvl);
	nvlist_free(fnvl);
	return (err);
}

/*
 * Link Aggregation TLV
 *  +--------+----------------+----------+--------+------------+------------+
 *  |	TLV  | TLV information|  802.1OUI|  802.1 |aggregation | aggregated |
 *  |   Type |   string len   |  00-80-C2| subtype|   status   | port ID    |
 *  +--------+----------------+----------+--------+------------+------------+
 *	7bits      9 bits       3 octets   1 octet   1 octet     4 octets
 */
/* ARGSUSED */
static int
i_lldp_parse_aggr(lldp_agent_t *lap, lldp_tlv_t *tlv, nvlist_t *tlv_nvl)
{
	lldp_aggr_t	ainfo;
	nvlist_t	*anvl;
	int		err;

	if ((err = lldp_get_nested_nvl(tlv_nvl, LLDP_NVP_ORGANIZATION,
	    LLDP_8021_OUI_LIST, LLDP_NVP_AGGR, &anvl)) == 0) {
		/* Duplicate TLV. We discard it and make progress */
		return (EEXIST);
	}

	if ((err = lldp_tlv2aggr(tlv, &ainfo)) == 0)
		err = lldp_add_aggr2nvlist(&ainfo, tlv_nvl);
	return (err);
}

static int
i_lldp_cmp_aggr(nvlist_t *cur_nvl, nvlist_t *tlv_nvl,
    nvlist_t *added_tlvnvl, nvlist_t *deleted_tlvnvl, nvlist_t *modified_tlvnvl)
{
	lldp_aggr_t	caggr, naggr;
	int		cur_err, new_err, ret = 1;

	cur_err = lldp_nvlist2aggr(cur_nvl, &caggr);
	if (cur_err != 0 && cur_err != ENOENT)
		return (-1);
	new_err = lldp_nvlist2aggr(tlv_nvl, &naggr);
	if (new_err != 0 && new_err != ENOENT)
		return (-1);

	assert(cur_err != ENOENT || new_err != ENOENT);
	if (cur_err == ENOENT) {
		if (lldp_add_aggr2nvlist(&naggr, added_tlvnvl) != 0)
			ret = -1;
	} else if (new_err == ENOENT) {
		if (nvlist_add_string(deleted_tlvnvl, LLDP_NVP_AGGR, "") != 0)
			ret = -1;
	} else if (caggr.la_status != naggr.la_status ||
	    caggr.la_id != naggr.la_id) {
		if (lldp_add_aggr2nvlist(&naggr, modified_tlvnvl) != 0)
			ret = -1;
	} else {
		ret = 0;
	}

	return (ret);
}

static int
i_lldp_write_aggr2pdu(void *argp, uint8_t *lldpdu, size_t pdusize,
    size_t *msglen)
{
	lldp_agent_t	*lap = argp;
	lldp_aggr_t	aggr;
	size_t		tlvlen = *msglen;
	int		err;

	aggr.la_id = lap->la_aggr_linkid;
	aggr.la_status = LLDP_AGGR_CAPABLE | LLDP_AGGR_MEMBER;
	if ((err = lldp_aggr2pdu(&aggr, lldpdu, pdusize, msglen)) == 0) {
		LLDP_TLV_SEND(LLDP_ORGSPECIFIC_TLVTYPE, *msglen - tlvlen,
		    LLDP_802dot1_OUI, LLDP_802dot1OUI_LINK_AGGR_SUBTYPE);
	}
	return (err);
}

static int
i_lldp_init_aggr(lldp_agent_t *lap, nvlist_t *nvl)
{
	lldp_aggr_t	ainfo;

	bzero(&ainfo, sizeof (ainfo));
	ainfo.la_status = LLDP_AGGR_CAPABLE;
	if (lap->la_aggr_linkid != DATALINK_INVALID_LINKID) {
		ainfo.la_status |= LLDP_AGGR_MEMBER;
		ainfo.la_id = lap->la_aggr_linkid;
	}

	return (lldp_add_aggr2nvlist(&ainfo, nvl));
}

static void
i_lldp_fini_aggr(lldp_agent_t *lap)
{
	nvlist_t	*ouinvl = NULL;

	if (lldp_get_nested_nvl(lap->la_local_mib, lap->la_msap,
	    LLDP_NVP_ORGANIZATION, LLDP_8021_OUI_LIST, &ouinvl) == 0) {
		(void) nvlist_remove(ouinvl, LLDP_NVP_AGGR, DATA_TYPE_NVLIST);
	}
}

/*
 * Port VLAN ID TLV
 *  +--------+----------------+--------------+-------------+-------------+
 *  |	TLV  | TLV information|  802.1OUI    |    802.1    |port VLAN ID |
 *  |   Type |   string len   |  00-80-C2    |   subtype   |   (PVID)    |
 *  +--------+----------------+--------------+-------------+-------------+
 *    7bits      9 bits        3 octets      1 octet         2 octets
 */
/* ARGSUSED */
static int
i_lldp_parse_pvid(lldp_agent_t *lap, lldp_tlv_t *tlv, nvlist_t *tlv_nvl)
{
	uint16_t	pvid;
	nvlist_t	*pnvl;
	int		err;

	if ((err = lldp_get_nested_nvl(tlv_nvl, LLDP_NVP_ORGANIZATION,
	    LLDP_8021_OUI_LIST, LLDP_NVP_PVID, &pnvl)) == 0) {
		/* Duplicate TLV. We discard it and make progress */
		return (EEXIST);
	}

	if ((err = lldp_tlv2pvid(tlv, &pvid)) == 0)
		err = lldp_add_pvid2nvlist(pvid, tlv_nvl);
	return (err);
}

static int
i_lldp_cmp_pvid(nvlist_t *cur_nvl, nvlist_t *tlv_nvl,
    nvlist_t *added_tlvnvl, nvlist_t *deleted_tlvnvl, nvlist_t *modified_tlvnvl)
{
	uint16_t	cur_pvid, new_pvid;
	int		cur_err, new_err, ret = 1;

	cur_err = lldp_nvlist2pvid(cur_nvl, &cur_pvid);
	if (cur_err != 0 && cur_err != ENOENT)
		return (-1);
	new_err = lldp_nvlist2pvid(tlv_nvl, &new_pvid);
	if (new_err != 0 && new_err != ENOENT)
		return (-1);

	assert(cur_err != ENOENT || new_err != ENOENT);
	if (cur_err == ENOENT) {
		if (lldp_add_pvid2nvlist(new_pvid, added_tlvnvl) != 0)
			ret = -1;
	} else if (new_err == ENOENT) {
		if (nvlist_add_string(deleted_tlvnvl, LLDP_NVP_PVID, "") != 0)
			ret = -1;
	} else if (cur_pvid != new_pvid) {
		if (lldp_add_pvid2nvlist(new_pvid, modified_tlvnvl) != 0)
			ret = -1;
	} else {
		ret = 0;
	}

	return (ret);
}

static int
i_lldp_write_pvid2pdu(void *argp, uint8_t *lldpdu, size_t pdusize,
    size_t *msglen)
{
	lldp_agent_t	*lap = argp;
	nvlist_t	*nvl = NULL;
	uint16_t	pvid;
	size_t		tlvlen = *msglen;
	int		err;

	(void) nvlist_lookup_nvlist(lap->la_local_mib, lap->la_msap, &nvl);
	if ((err = lldp_nvlist2pvid(nvl, &pvid)) == 0)
		err = lldp_pvid2pdu(pvid, lldpdu, pdusize, msglen);
	if (err == 0) {
		LLDP_TLV_SEND(LLDP_ORGSPECIFIC_TLVTYPE, *msglen - tlvlen,
		    LLDP_802dot1_OUI, LLDP_802dot1OUI_PVID_SUBTYPE);
	}
	return (err);
}

static int
i_lldp_init_pvid(lldp_agent_t *lap, nvlist_t *nvl)
{
	char	propval[DLADM_PROP_VAL_MAX];
	char	*valptr[1];
	uint_t	valcnt = 1;
	int	err = -1;

	/* Get the PVID for this link */
	valptr[0] = propval;
	if (dladm_get_linkprop(dld_handle, lap->la_linkid,
	    DLADM_PROP_VAL_CURRENT, "default_tag", (char **)valptr,
	    &valcnt) == DLADM_STATUS_OK) {
		uint16_t	pvid;

		pvid = strtol(propval, NULL, 10);
		err = lldp_add_pvid2nvlist(pvid, nvl);
	}
	return (err);
}

static void
i_lldp_fini_pvid(lldp_agent_t *lap)
{
	nvlist_t	*ouinvl;

	if (lldp_get_nested_nvl(lap->la_local_mib, lap->la_msap,
	    LLDP_NVP_ORGANIZATION, LLDP_8021_OUI_LIST, &ouinvl) == 0) {
		(void) nvlist_remove(ouinvl, LLDP_NVP_PVID, DATA_TYPE_NVLIST);
	}
}

/*
 * VLAN Name TLV
 *  +--------+----------------+----------+--------+-----+----------+------+
 *  |	TLV  | TLV information|  802.1OUI|  802.1 |VLAN |VLAN name | VLAN |
 *  |   Type |   string len   |  00-80-C2| subtype| ID  | Len      | name |
 *  +--------+----------------+----------+--------+-----+----------+------+
 *	7bits      9 bits       3 octets   1 octet 2oct  1 octet    0-32 octets
 */
/* ARGSUSED */
static int
i_lldp_parse_vlan(lldp_agent_t *lap, lldp_tlv_t *tlv, nvlist_t *tlv_nvl)
{
	lldp_vlan_info_t	lvi;
	int			err;

	if ((err = lldp_tlv2vlan(tlv, &lvi)) == 0)
		err = lldp_add_vlan2nvlist(&lvi, tlv_nvl);
	return (err);
}

static int
i_lldp_cmp_vlan(nvlist_t *cur_nvl, nvlist_t *tlv_nvl,
    nvlist_t *added_tlvnvl, nvlist_t *deleted_tlvnvl, nvlist_t *modified_tlvnvl)
{
	int		cur_count = 0, new_count = 0, ret = 1;
	nvlist_t	*cvnvl, *nvnvl;

	if (lldp_get_nested_nvl(cur_nvl, LLDP_NVP_ORGANIZATION,
	    LLDP_8021_OUI_LIST, LLDP_NVP_VLANNAME, &cvnvl) == 0)
		cur_count = lldp_nvlist_nelem(cvnvl);

	if (lldp_get_nested_nvl(tlv_nvl, LLDP_NVP_ORGANIZATION,
	    LLDP_8021_OUI_LIST, LLDP_NVP_VLANNAME, &nvnvl) == 0)
		new_count = lldp_nvlist_nelem(nvnvl);

	if (cur_count == 0 && new_count == 0)
		return (-1);
	if (cur_count == 0) {
		if (lldp_merge_nested_nvl(added_tlvnvl, tlv_nvl,
		    LLDP_NVP_ORGANIZATION, LLDP_8021_OUI_LIST,
		    LLDP_NVP_VLANNAME) != 0) {
			ret = -1;
		}
	} else if (new_count == 0) {
		if (nvlist_add_string(deleted_tlvnvl, LLDP_NVP_VLANNAME,
		    "") != 0)
			ret = -1;
	} else if (cur_count != new_count || !lldp_nvl_similar(cvnvl, nvnvl)) {
		if (lldp_merge_nested_nvl(modified_tlvnvl, tlv_nvl,
		    LLDP_NVP_ORGANIZATION, LLDP_8021_OUI_LIST,
		    LLDP_NVP_VLANNAME) != 0) {
			ret = -1;
		}
	} else {
		ret = 0;
	}

	return (ret);
}

static int
i_lldp_write_vlan2pdu(void *argp, uint8_t *lldpdu, size_t pdusize,
    size_t *msglen)
{
	lldp_agent_t		*lap = argp;
	nvlist_t		*nvl = NULL;
	size_t			used, tlvlen = *msglen;
	int			err, count, cnt = 0;
	lldp_vlan_info_t	*lvi = NULL, *tlvi = NULL;

	(void) nvlist_lookup_nvlist(lap->la_local_mib, lap->la_msap, &nvl);
	(void) lldp_nvlist2vlan(nvl, &lvi, &cnt);
	if (cnt == 0)
		return (0);

	tlvi = lvi;
	for (count = 0; count < cnt; count++) {
		if ((err = lldp_vlan2pdu(tlvi, lldpdu, pdusize, msglen)) != 0)
			break;
		used = *msglen - tlvlen;
		pdusize -= used;
		lldpdu += used;
		LLDP_TLV_SEND(LLDP_ORGSPECIFIC_TLVTYPE, used,
		    LLDP_802dot1_OUI, LLDP_802dot1OUI_VLAN_NAME_SUBTYPE);
		tlvlen = *msglen;
		tlvi++;
	}
	free(lvi);
	return (err);
}

static int
i_lldp_addvlan_cbfunc(dladm_handle_t dh, datalink_id_t vlan_linkid, void *arg)
{
	lldp_agent_t	*lap = arg;

	if (lldp_add_vlan_info(dh, vlan_linkid, arg) != 0) {
		syslog(LOG_ERR, "Error adding VLAN to local mib for agent %s",
		    lap->la_linkname);
	}
	return (DLADM_WALK_CONTINUE);
}

/* ARGSUSED */
static int
i_lldp_init_vlan(lldp_agent_t *lap, nvlist_t *nvl)
{
	dladm_status_t	dlstatus;

	dlstatus = dladm_walk_datalink_id(i_lldp_addvlan_cbfunc, dld_handle,
	    lap, DATALINK_CLASS_VLAN, DL_ETHER, DLADM_OPT_ACTIVE);
	return (dlstatus == DLADM_STATUS_OK ? 0 : -1);
}

/* ARGSUSED */
static void
i_lldp_fini_vlan(lldp_agent_t *lap)
{
	nvlist_t	*ouinvl = NULL;

	if (lldp_get_nested_nvl(lap->la_local_mib, lap->la_msap,
	    LLDP_NVP_ORGANIZATION, LLDP_8021_OUI_LIST, &ouinvl) == 0) {
		(void) nvlist_remove(ouinvl, LLDP_NVP_VLANNAME,
		    DATA_TYPE_NVLIST);
	}
}

/*
 * Virtualization TLV (includes VNICs information)
 *  +------+------------+-----------+---------+---------+-------+------+------+
 *  | TLV  | TLV info   | OracleOUI | Orcale  | Reser- |Vlan ID |PortID |Port |
 *  | Type | string len | 00-03-BA  | subtype |   ved  |        |subtye | ID  |
 *  +------+------------+-----------+---------+--------+--------+------+------+
 *    7 bits  9 bits     3 octets     8 bits   4 octets  2octets  8bits  1-255
 *							                 octets
 */
/* ARGSUSED */
static int
i_lldp_parse_vnic(lldp_agent_t *lap, lldp_tlv_t *tlv, nvlist_t *tlv_nvl)
{
	lldp_vnic_info_t	lvi;
	int			err;

	if ((err = lldp_tlv2vnic(tlv, &lvi)) == 0)
		err = lldp_add_vnic2nvlist(&lvi, tlv_nvl);
	return (err);
}

static int
i_lldp_cmp_vnic(nvlist_t *cur_nvl, nvlist_t *tlv_nvl,
    nvlist_t *added_tlvnvl, nvlist_t *deleted_tlvnvl, nvlist_t *modified_tlvnvl)
{
	int		cur_count = 0, new_count = 0, ret = 1;
	nvlist_t	*cvnvl, *nvnvl;

	if (lldp_get_nested_nvl(cur_nvl, LLDP_NVP_ORGANIZATION,
	    LLDP_ORACLE_OUI_LIST, LLDP_NVP_VNICNAME, &cvnvl) == 0)
		cur_count = lldp_nvlist_nelem(cvnvl);

	if (lldp_get_nested_nvl(tlv_nvl, LLDP_NVP_ORGANIZATION,
	    LLDP_ORACLE_OUI_LIST, LLDP_NVP_VNICNAME, &nvnvl) == 0)
		new_count = lldp_nvlist_nelem(nvnvl);

	if (cur_count == 0 && new_count == 0)
		return (-1);

	if (cur_count == 0) {
		if (lldp_merge_nested_nvl(added_tlvnvl, tlv_nvl,
		    LLDP_NVP_ORGANIZATION, LLDP_ORACLE_OUI_LIST,
		    LLDP_NVP_VNICNAME) != 0) {
			ret = -1;
		}
	} else if (new_count == 0) {
		if (nvlist_add_string(deleted_tlvnvl, LLDP_NVP_VNICNAME,
		    "") != 0)
			ret = -1;
	} else if (cur_count != new_count || !lldp_nvl_similar(cvnvl, nvnvl)) {
		if (lldp_merge_nested_nvl(modified_tlvnvl, tlv_nvl,
		    LLDP_NVP_ORGANIZATION, LLDP_ORACLE_OUI_LIST,
		    LLDP_NVP_VNICNAME) != 0) {
			ret = -1;
		}
	} else {
		ret = 0;
	}

	return (ret);
}

static int
i_lldp_write_vnic2pdu(void *argp, uint8_t *lldpdu, size_t pdusize,
    size_t *msglen)
{
	lldp_agent_t		*lap = argp;
	nvlist_t		*nvl;
	size_t			used, tlvlen = *msglen;
	int			err, count, cnt = 0;
	lldp_vnic_info_t	*lvi = NULL, *tlvi = NULL;

	(void) nvlist_lookup_nvlist(lap->la_local_mib, lap->la_msap, &nvl);
	(void) lldp_nvlist2vnic(nvl, &lvi, &cnt);
	if (cnt == 0)
		return (0);

	tlvi = lvi;
	for (count = 0; count < cnt; count++) {
		if ((err = lldp_vnic2pdu(tlvi, lldpdu, pdusize, msglen)) != 0)
			break;
		used = *msglen - tlvlen;
		pdusize -= used;
		lldpdu += used;
		LLDP_TLV_SEND(LLDP_ORGSPECIFIC_TLVTYPE, used, LLDP_ORACLE_OUI,
		    LLDP_ORACLEOUI_VNIC_SUBTYPE);
		tlvlen = *msglen;
		tlvi++;
	}
	free(lvi);

	return (err);
}

static int
i_lldp_addvnic_cbfunc(dladm_handle_t dh, datalink_id_t vnic_linkid, void *arg)
{
	lldp_agent_t	*lap = arg;

	if (lldp_add_vnic_info(dh, vnic_linkid, arg) != 0) {
		syslog(LOG_ERR, "Error adding VNIC to local mib for agent %s",
		    lap->la_linkname);
	}
	return (DLADM_WALK_CONTINUE);
}

/* ARGSUSED */
static int
i_lldp_init_vnic(lldp_agent_t *lap, nvlist_t *nvl)
{
	dladm_status_t	dlstatus;

	dlstatus = dladm_walk_datalink_id(i_lldp_addvnic_cbfunc, dld_handle,
	    lap, DATALINK_CLASS_VNIC, DL_ETHER, DLADM_OPT_ACTIVE);
	return (dlstatus == DLADM_STATUS_OK ? 0 : -1);
}

/* ARGSUSED */
static void
i_lldp_fini_vnic(lldp_agent_t *lap)
{
	nvlist_t	*ouinvl = NULL;

	if (lldp_get_nested_nvl(lap->la_local_mib, lap->la_msap,
	    LLDP_NVP_ORGANIZATION, LLDP_ORACLE_OUI_LIST, &ouinvl) == 0) {
		(void) nvlist_remove(ouinvl, LLDP_NVP_VNICNAME,
		    DATA_TYPE_NVLIST);
	}
}

/* ARGSUSED */
static int
i_lldp_parse_pfc(lldp_agent_t *lap, lldp_tlv_t *tlv, nvlist_t *tlv_nvl)
{
	lldp_pfc_t	pfc;
	nvlist_t	*pnvl;
	int		err;

	if ((err = lldp_get_nested_nvl(tlv_nvl, LLDP_NVP_ORGANIZATION,
	    LLDP_8021_OUI_LIST, LLDP_NVP_PFC, &pnvl)) == 0) {
		/* Duplicate TLV. We discard it and make progress */
		return (EEXIST);
	}

	if ((err = lldp_tlv2pfc(tlv, &pfc)) == 0)
		err = lldp_add_pfc2nvlist(&pfc, tlv_nvl);
	return (err);
}

static int
i_lldp_write_pfc2pdu(void *argp, uint8_t *lldpdu, size_t pdusize,
    size_t *msglen)
{
	lldp_agent_t		*lap = argp;
	nvlist_t		*nvl = NULL;
	size_t			tlvlen = *msglen;
	lldp_pfc_t		pfc;
	int			err;

	(void) nvlist_lookup_nvlist(lap->la_local_mib, lap->la_msap, &nvl);
	if ((err = lldp_nvlist2pfc(nvl, &pfc)) == 0)
		err = lldp_pfc2pdu(&pfc, lldpdu, pdusize, msglen);
	if (err == 0) {
		LLDP_TLV_SEND(LLDP_ORGSPECIFIC_TLVTYPE, *msglen - tlvlen,
		    LLDP_802dot1_OUI, LLDP_802dot1OUI_PFC_SUBTYPE);
	}
	return (err);
}

static int
i_lldp_cmp_pfc(nvlist_t *cur_nvl, nvlist_t *tlv_nvl,
    nvlist_t *added_tlvnvl, nvlist_t *deleted_tlvnvl, nvlist_t *modified_tlvnvl)
{
	lldp_pfc_t	cpfc;
	lldp_pfc_t	npfc;
	int		cur_err, new_err, ret = 1;

	cur_err = lldp_nvlist2pfc(cur_nvl, &cpfc);
	if (cur_err != 0 && cur_err != ENOENT)
		return (-1);
	new_err = lldp_nvlist2pfc(tlv_nvl, &npfc);
	if (new_err != 0 && new_err != ENOENT) {
		return (-1);
	}
	assert(cur_err != ENOENT || new_err != ENOENT);
	if (cur_err == ENOENT) {
		if (lldp_add_pfc2nvlist(&npfc, added_tlvnvl) != 0)
			ret = -1;
	} else if (new_err == ENOENT) {
		if (nvlist_add_string(deleted_tlvnvl, LLDP_NVP_PFC, "") != 0)
			ret = -1;
	} else if (cpfc.lp_enable != npfc.lp_enable ||
	    cpfc.lp_cap != npfc.lp_cap || cpfc.lp_willing != npfc.lp_willing) {
		if (lldp_add_pfc2nvlist(&npfc, modified_tlvnvl) != 0)
			ret = -1;
	} else {
		ret = 0;
	}

	return (ret);
}

/* ARGSUSED */
static int
i_lldp_parse_appln(lldp_agent_t *lap, lldp_tlv_t *tlv, nvlist_t *tlv_nvl)
{
	lldp_appln_t	*appln = NULL;
	uint_t		nappln;
	nvlist_t	*anvl;
	int		err;

	if ((err = lldp_get_nested_nvl(tlv_nvl, LLDP_NVP_ORGANIZATION,
	    LLDP_8021_OUI_LIST, LLDP_NVP_APPLN, &anvl)) == 0) {
		/* Duplicate TLV. We discard it and make progress */
		return (EEXIST);
	}

	if ((err = lldp_tlv2appln(tlv, &appln, &nappln)) == 0)
		err = lldp_add_appln2nvlist(appln, nappln, tlv_nvl);
	free(appln);
	return (err);
}

static int
i_lldp_write_appln2pdu(void *argp, uint8_t *lldpdu, size_t pdusize,
    size_t *msglen)
{
	lldp_agent_t		*lap = argp;
	nvlist_t		*nvl = NULL;
	size_t			tlvlen = *msglen;
	lldp_appln_t		*appln = NULL;
	uint_t			nappln;
	int			err;

	(void) nvlist_lookup_nvlist(lap->la_local_mib, lap->la_msap, &nvl);
	if ((err = lldp_nvlist2appln(nvl, &appln, &nappln)) == 0)
		err = lldp_appln2pdu(appln, nappln, lldpdu, pdusize, msglen);
	if (err == 0) {
		LLDP_TLV_SEND(LLDP_ORGSPECIFIC_TLVTYPE, *msglen - tlvlen,
		    LLDP_802dot1_OUI, LLDP_802dot1OUI_APPLN_SUBTYPE);
	}

	free(appln);
	return (err);
}

static int
i_lldp_cmp_appln(nvlist_t *cur_nvl, nvlist_t *tlv_nvl,
    nvlist_t *added_tlvnvl, nvlist_t *deleted_tlvnvl, nvlist_t *modified_tlvnvl)
{
	int		cur_count = 0, new_count = 0, ret = 1;
	nvlist_t	*canvl, *nanvl;

	if (lldp_get_nested_nvl(cur_nvl, LLDP_NVP_ORGANIZATION,
	    LLDP_8021_OUI_LIST, LLDP_NVP_APPLN, &canvl) == 0)
		cur_count = lldp_nvlist_nelem(canvl);

	if (lldp_get_nested_nvl(tlv_nvl, LLDP_NVP_ORGANIZATION,
	    LLDP_8021_OUI_LIST, LLDP_NVP_APPLN, &nanvl) == 0)
		new_count = lldp_nvlist_nelem(nanvl);

	if (cur_count == 0 && new_count == 0)
		return (-1);
	if (cur_count == 0) {
		if (lldp_merge_nested_nvl(added_tlvnvl, tlv_nvl,
		    LLDP_NVP_ORGANIZATION, LLDP_8021_OUI_LIST,
		    LLDP_NVP_APPLN) != 0) {
			ret = -1;
		}
	} else if (new_count == 0) {
		if (nvlist_add_string(deleted_tlvnvl, LLDP_NVP_APPLN, "") != 0)
			ret = -1;
	} else if (cur_count != new_count || !lldp_nvl_similar(canvl, nanvl)) {
		if (lldp_merge_nested_nvl(modified_tlvnvl, tlv_nvl,
		    LLDP_NVP_ORGANIZATION, LLDP_8021_OUI_LIST,
		    LLDP_NVP_APPLN) != 0) {
			ret = -1;
		}
	} else {
		ret = 0;
	}

	return (ret);
}

/*
 * For those organizational specific TLVs that we do not support,
 * increment the ls_stats_TLVSUnrecognizedTotal counter and store the TLV
 * according to the organizationally specific TLV format. They are indexed
 * by the OUI+subytpe.
 */
/* ARGSUSED */
static int
i_lldp_parse_unrec_orgspec(lldp_agent_t *lap, lldp_tlv_t *tlv,
    nvlist_t *tlv_nvl)
{
	nvlist_t	*orgnvl = NULL;
	uint32_t	oui, stype, arrsz;
	char		buf[LLDP_STRSIZE], *nvpname;
	char		msap[LLDP_MAX_MSAPSTRLEN];
	nvlist_t	*mibnvl = NULL, *rorgnvl = NULL;
	nvpair_t	*nvp = NULL;
	uint8_t		*arr;

	oui =  LLDP_ORGTLV_OUI(tlv->lt_value);
	stype = LLDP_ORGTLV_STYPE(tlv->lt_value);

	if (lldp_create_nested_nvl(tlv_nvl, LLDP_NVP_UNREC_ORGANIZATION,
	    NULL, NULL, &orgnvl) != 0) {
		return (ENOMEM);
	}

	/*
	 * From the spec. 9.2.7.7.3
	 * If more than one unrecognized organizationally specific TLV
	 * is received with the same OUI and subtype, we assign a
	 * temporary index and store it.
	 */
	(void) snprintf(buf, LLDP_STRSIZE, "%u_%u", oui, stype);
	lldp_nvlist2msap(tlv_nvl, msap, sizeof (msap));
	lldp_rw_lock(&lap->la_rxmib_rwlock, LLDP_RWLOCK_READER);
	if (nvlist_lookup_nvlist(lap->la_remote_mib, msap, &mibnvl) == 0 &&
	    nvlist_lookup_nvlist(mibnvl, LLDP_NVP_UNREC_ORGANIZATION,
	    &rorgnvl) == 0) {
		for (nvp = nvlist_next_nvpair(rorgnvl, NULL); nvp != NULL;
		    nvp = nvlist_next_nvpair(rorgnvl, nvp)) {
			nvpname = nvpair_name(nvp);
			if (LLDP_NVP_PRIVATE(nvpname))
				continue;
			if (strncmp(nvpname, buf, strlen(buf)) != 0)
				continue;
			assert(nvpair_type(nvp) == DATA_TYPE_BYTE_ARRAY);
			if (nvpair_value_byte_array(nvp, &arr, &arrsz) != 0)
				continue;
			/* `arr' includes 2 bytes of type and length */
			if (tlv->lt_len != arrsz - 2)
				continue;
			if (bcmp(arr + 2, tlv->lt_value, arrsz - 2) == 0)
				break;
		}
	}
	lldp_rw_unlock(&lap->la_rxmib_rwlock);
	if (nvp != NULL) {
		(void) strlcpy(buf, nvpname, sizeof (buf));
	} else {
		(void) snprintf(buf, LLDP_STRSIZE, "%u_%u_%u", oui,
		    stype, lap->la_unrec_orgspec_index++);
	}
	return (nvlist_add_byte_array(orgnvl, buf, tlv->lt_value - 2,
	    tlv->lt_len + 2));
}

static int
i_lldp_cmp_unrec_orgspec_tlvs(nvlist_t *rmib_nvl, nvlist_t *tlv_nvl,
    nvlist_t *added_tlvnvl, nvlist_t *deleted_tlvnvl, nvlist_t *modified_tlvnvl)
{
	return (i_lldp_cmp_unrec_tlvs(LLDP_NVP_UNREC_ORGANIZATION, rmib_nvl,
	    tlv_nvl, added_tlvnvl, deleted_tlvnvl, modified_tlvnvl));
}

/*
 * If the TLV is from a reserved TLV types, then they can be from a later
 * version of the basic management set and is stored according to the basic
 * TLV format and they are indexed by their TLV type
 */
/* ARGSUSED */
static int
i_lldp_parse_reserved_tlvs(lldp_agent_t *lap, lldp_tlv_t *tlv,
    nvlist_t *tlv_nvl)
{
	nvlist_t	*rnvl;
	char		nvpname[LLDP_STRSIZE];

	if (!nvlist_exists(tlv_nvl, LLDP_NVP_RESERVED)) {
		if (nvlist_alloc(&rnvl, NV_UNIQUE_NAME, 0) != 0)
			return (ENOMEM);
		if (nvlist_add_nvlist(tlv_nvl, LLDP_NVP_RESERVED, rnvl) != 0) {
			nvlist_free(rnvl);
			return (ENOMEM);
		}
		nvlist_free(rnvl);
	}
	(void) nvlist_lookup_nvlist(tlv_nvl, LLDP_NVP_RESERVED, &rnvl);
	(void) snprintf(nvpname, sizeof (nvpname), "%d", tlv->lt_type);
	return (nvlist_add_byte_array(rnvl, nvpname, tlv->lt_value - 2,
	    tlv->lt_len + 2));
}

int
i_lldp_cmp_reserved_tlvs(nvlist_t *rmib_nvl, nvlist_t *tlv_nvl,
    nvlist_t *added_tlvnvl, nvlist_t *deleted_tlvnvl, nvlist_t *modified_tlvnvl)
{
	return (i_lldp_cmp_unrec_tlvs(LLDP_NVP_RESERVED, rmib_nvl, tlv_nvl,
	    added_tlvnvl, deleted_tlvnvl, modified_tlvnvl));
}

int
lldp_add_aggr2nvlist(lldp_aggr_t *ainfo, nvlist_t *tlv_nvl)
{
	nvlist_t	*anvl = NULL, *ouinvl = NULL;
	int		err;

	if ((err = lldp_create_nested_nvl(tlv_nvl, LLDP_NVP_ORGANIZATION,
	    LLDP_8021_OUI_LIST, NULL, &ouinvl)) != 0) {
		return (err);
	}
	if ((err = nvlist_alloc(&anvl, NV_UNIQUE_NAME, 0)) != 0)
		return (err);
	if ((err = nvlist_add_uint8(anvl, LLDP_NVP_AGGR_STATUS,
	    ainfo->la_status)) != 0) {
		goto ret;
	}
	if ((err = nvlist_add_uint32(anvl, LLDP_NVP_AGGR_ID,
	    ainfo->la_id)) != 0) {
		goto ret;
	}
	err = nvlist_add_nvlist(ouinvl, LLDP_NVP_AGGR, anvl);
ret:
	nvlist_free(anvl);
	return (err);
}

int
lldp_add_pvid2nvlist(uint16_t pvid, nvlist_t *tlv_nvl)
{
	nvlist_t	*pnvl = NULL, *ouinvl = NULL;
	int		err;

	if ((err = lldp_create_nested_nvl(tlv_nvl, LLDP_NVP_ORGANIZATION,
	    LLDP_8021_OUI_LIST, NULL, &ouinvl)) != 0)
		return (err);

	if ((err = nvlist_alloc(&pnvl, NV_UNIQUE_NAME, 0)) != 0)
		return (err);

	if ((err = nvlist_add_uint16(pnvl, LLDP_NVP_PVID, pvid)) != 0)
		goto ret;
	err = nvlist_add_nvlist(ouinvl, LLDP_NVP_PVID, pnvl);
ret:
	nvlist_free(pnvl);
	return (err);
}

int
lldp_add_vlan2nvlist(lldp_vlan_info_t *lvi, nvlist_t *tlv_nvl)
{
	char		nvpname[LLDP_STRSIZE], *vlanstr;
	nvlist_t	*vnvl = NULL;

	if (lldp_create_nested_nvl(tlv_nvl, LLDP_NVP_ORGANIZATION,
	    LLDP_8021_OUI_LIST, LLDP_NVP_VLANNAME, &vnvl) != 0)
		return (ENOMEM);

	/*
	 * If more than one VLAN Name TLV is defined for a port, the VLAN ID
	 * and the associated VLAN name combination shall be different from
	 * any other VLAN ID and VLAN name combination defined for the port.
	 */
	(void) snprintf(nvpname, sizeof (nvpname), "%s_%d", lvi->lvi_name,
	    lvi->lvi_vid);
	if (nvlist_lookup_string(vnvl, nvpname, &vlanstr) == 0)
		return (EEXIST);
	return (nvlist_add_string(vnvl, nvpname, nvpname));
}

int
lldp_add_vnic2nvlist(lldp_vnic_info_t *lvi, nvlist_t *tlv_nvl)
{
	nvlist_t	*rnvl = NULL;
	nvlist_t	*vnvl = NULL;
	char		nvpname[LLDP_STRSIZE];
	lldp_portid_t	*pid;
	int		err;

	if (lldp_create_nested_nvl(tlv_nvl, LLDP_NVP_ORGANIZATION,
	    LLDP_ORACLE_OUI_LIST, LLDP_NVP_VNICNAME, &vnvl) != 0)
		return (ENOMEM);

	pid = &lvi->lvni_portid;
	/* We will use the VNIC MAC addr to distinguish between the entries */
	(void) lldp_bytearr2hexstr(pid->lp_pid, pid->lp_pidlen, nvpname,
	    sizeof (nvpname));

	if (nvlist_exists(vnvl, nvpname))
		return (EEXIST);
	/* Add each VNIC entry as a TLV list */
	if (nvlist_alloc(&rnvl, NV_UNIQUE_NAME, 0) != 0)
		return (ENOMEM);

	if ((err = nvlist_add_uint32(rnvl, LLDP_NVP_VNIC_LINKID,
	    lvi->lvni_linkid)) != 0) {
		goto ret;
	}
	if ((err = nvlist_add_uint16(rnvl, LLDP_NVP_VNIC_VLANID,
	    lvi->lvni_vid)) != 0) {
		goto ret;
	}

	if ((err = lldp_add_portid2nvlist(pid, rnvl)) != 0)
		goto ret;

	err = nvlist_add_nvlist(vnvl, nvpname, rnvl);
ret:
	nvlist_free(rnvl);
	return (err);
}

int
lldp_add_pfc2nvlist(lldp_pfc_t *pfc, nvlist_t *tlv_nvl)
{
	nvlist_t	*pnvl = NULL;
	int		err;

	if (lldp_create_nested_nvl(tlv_nvl, LLDP_NVP_ORGANIZATION,
	    LLDP_8021_OUI_LIST, LLDP_NVP_PFC, &pnvl) != 0) {
			return (ENOMEM);
	}
	if ((err = nvlist_add_boolean_value(pnvl, LLDP_NVP_WILLING,
	    pfc->lp_willing)) != 0) {
		return (err);
	}
	if ((err = nvlist_add_boolean_value(pnvl, LLDP_NVP_PFC_MBC,
	    pfc->lp_mbc)) != 0) {
		return (err);
	}
	if ((err = nvlist_add_uint8(pnvl, LLDP_NVP_PFC_CAP, pfc->lp_cap)) != 0)
		return (err);
	return (nvlist_add_uint8(pnvl, LLDP_NVP_PFC_ENABLE, pfc->lp_enable));
}

int
lldp_add_appln2nvlist(lldp_appln_t *appln, uint_t nappln, nvlist_t *tlv_nvl)
{
	nvlist_t	*anvl = NULL;
	char		nvpname[LLDP_STRSIZE];
	lldp_appln_t	*app;
	int		i;

	if (lldp_create_nested_nvl(tlv_nvl, LLDP_NVP_ORGANIZATION,
	    LLDP_8021_OUI_LIST, LLDP_NVP_APPLN, &anvl) != 0) {
		return (ENOMEM);
	}

	app = appln;
	for (i = 0; i < nappln; i++) {
		/*
		 * We will use the Appln ID and the selector to distinguish
		 * between the entries.
		 */
		(void) snprintf(nvpname, LLDP_STRSIZE, "%u_%u", app->la_id,
		    app->la_sel);
		if (nvlist_add_uint8(anvl, nvpname, app->la_pri) != 0)
			return (ENOMEM);
		app++;
	}
	return (0);
}

/*
 * This assumes that the TLVs will not be named similarly across
 * basic-tlv group, 802.1 group, 802.3 group and oracle-tlv group.
 */
lldp_tlv_info_t *
lldp_get_tlvinfo_from_tlvname(const char *tlvname)
{
	int	i;
	lldp_tlv_info_t	*infop;

	for (i = 0; i < A_CNT(lldp_tlv_table); i++) {
		infop = &lldp_tlv_table[i];
		if (infop->lti_name != NULL &&
		    strcmp(tlvname, infop->lti_name) == 0)
			return (&lldp_tlv_table[i]);
	}
	return (NULL);
}

lldp_tlv_info_t *
lldp_get_tlvinfo_from_nvpname(const char *nvpname)
{
	int	i;
	lldp_tlv_info_t	*infop;

	for (i = 0; i < A_CNT(lldp_tlv_table); i++) {
		infop = &lldp_tlv_table[i];
		if (infop->lti_nvpname != NULL &&
		    strcmp(nvpname, infop->lti_nvpname) == 0)
			return (&lldp_tlv_table[i]);
	}
	/*
	 * check if `nvpname' represents reserved tlvtype or
	 * unrecognized organization tlvtype.
	 */
	if (strcmp(nvpname, LLDP_NVP_RESERVED) == 0)
		return (&lldp_reserved_tlv);
	if (strcmp(nvpname, LLDP_NVP_UNREC_ORGANIZATION) == 0)
		return (&lldp_unrec_orgspec_tlv);

	return (NULL);
}

/*
 * We will add the callback function towards the end. We abort if the write
 * callback function is already registered and return EEXIST.
 *
 * Caller has lock to `la_txmib_rwlock'
 */
int
lldp_write2pdu_add(lldp_agent_t *lap, lldp_tlv_info_t *infop,
    lldp_tlv_writef_t wfunc, void *cbarg)
{
	lldp_write2pdu_t	*new, *wpdu;
	nvlist_t		*nvl = NULL;
	int			err;

	if (wfunc == NULL)
		return (EINVAL);

	for (wpdu = list_head(&lap->la_write2pdu); wpdu != NULL;
	    wpdu = list_next(&lap->la_write2pdu, wpdu)) {
		if (wpdu->ltp_writef == wfunc)
			return (EEXIST);
	}

	if ((new = calloc(1, sizeof (lldp_write2pdu_t))) == NULL)
		return (ENOMEM);

	/*
	 * Before we add the function that writes the TLV information into PDU,
	 * we call `init' function, if any.
	 */
	if (infop->lti_initf != NULL) {
		err = nvlist_lookup_nvlist(lap->la_local_mib, lap->la_msap,
		    &nvl);
		if (err == 0)
			err = infop->lti_initf(lap, nvl);
		if (err != 0) {
			free(new);
			return (err);
		}
	}

	new->ltp_infop = infop;
	new->ltp_writef = wfunc;
	new->ltp_cbarg = cbarg;
	list_insert_tail(&lap->la_write2pdu, new);
	return (0);
}

/* caller has lock to `la_txmib_rwlock' */
int
lldp_write2pdu_remove(lldp_agent_t *lap, lldp_tlv_writef_t wfunc)
{
	lldp_write2pdu_t	*wpdu;
	lldp_tlv_info_t		*infop;

	if (wfunc == NULL)
		return (EINVAL);

	for (wpdu = list_head(&lap->la_write2pdu); wpdu != NULL;
	    wpdu = list_next(&lap->la_write2pdu, wpdu)) {
		if (wpdu->ltp_writef == wfunc)
			break;
	}
	if (wpdu == NULL)
		return (ENOENT);
	list_remove(&lap->la_write2pdu, wpdu);

	/* call the fini function, once we are done with removing the tlv */
	infop = wpdu->ltp_infop;
	if (infop->lti_finif != NULL)
		infop->lti_finif(lap);

	free(wpdu);
	return (0);
}

static int
i_lldp_add_nvpname2nvlist(nvlist_t *nvl, nvlist_t *deleted_tlvnvl)
{
	nvpair_t	*nvp;
	char		*name;

	for (nvp = nvlist_next_nvpair(nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {
		name = nvpair_name(nvp);
		if (LLDP_NVP_PRIVATE(name))
			continue;
		if (nvlist_add_string(deleted_tlvnvl, name, "") != 0)
			return (ENOMEM);
	}
	return (0);
}

/*
 * Used to compare both the reserved (tlv type >= 9 or tlv type <= 126) and
 * unrecognized organisation specific tlv.
 */
static int
i_lldp_cmp_unrec_tlvs(const char *nvpname, nvlist_t *cur_nvl,
    nvlist_t *new_nvl, nvlist_t *added_tlvnvl, nvlist_t *deleted_tlvnvl,
    nvlist_t *modified_tlvnvl)
{
	nvpair_t	*nvp;
	nvlist_t	*cur_unvl = NULL, *new_unvl = NULL;
	nvlist_t	*added_unvl = NULL, *modified_unvl = NULL;
	uint_t		cur_nelem, new_nelem;
	char		*name;
	boolean_t	added = B_FALSE;
	int		ret = 1;

	(void) nvlist_lookup_nvlist(cur_nvl, nvpname, &cur_unvl);
	(void) nvlist_lookup_nvlist(new_nvl, nvpname, &new_unvl);

	cur_nelem = lldp_nvlist_nelem(cur_unvl);
	new_nelem = lldp_nvlist_nelem(new_unvl);

	if (cur_nelem == 0 && new_nelem == 0) {
		return (0);
	} else if (new_nelem == 0) {
		if (i_lldp_add_nvpname2nvlist(cur_unvl, deleted_tlvnvl) != 0)
			ret = -1;
		return (ret);
	} else if (cur_nelem == 0) {
		if (nvlist_add_nvlist(added_tlvnvl, nvpname, new_unvl) != 0)
			ret = -1;
		return (ret);
	}
	/*
	 * Create necessary place holders for `added_tlvnvl'/
	 * `modified_tlvnvl'.
	 */
	if (lldp_create_nested_nvl(added_tlvnvl, nvpname, NULL, NULL,
	    &added_unvl) != 0 ||
	    lldp_create_nested_nvl(modified_tlvnvl, nvpname, NULL, NULL,
	    &modified_unvl) != 0) {
		return (-1);
	}
	for (nvp = nvlist_next_nvpair(cur_unvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(cur_unvl, nvp)) {
		name = nvpair_name(nvp);
		if (LLDP_NVP_PRIVATE(name))
			continue;
		if (!nvlist_exists(new_unvl, name)) {
			if (nvlist_add_string(deleted_tlvnvl, name, "") != 0)
				return (-1);
		} else {
			uint8_t *cur_arr, *new_arr;

			cur_nelem = new_nelem = 0;
			cur_arr = new_arr = NULL;
			(void) nvpair_value_byte_array(nvp, &cur_arr,
			    &cur_nelem);
			(void) nvlist_lookup_byte_array(new_unvl, name,
			    &new_arr, &new_nelem);
			if (cur_nelem != new_nelem ||
			    bcmp(cur_arr, new_arr, cur_nelem) != 0) {
				if (nvlist_add_byte_array(modified_unvl, name,
				    new_arr, new_nelem) != 0) {
					return (-1);
				}
				added = B_TRUE;
			}
		}
		(void) nvlist_remove_all(new_unvl, name);
	}
	/* now walk through any remaining elements in new_unvl */
	for (nvp = nvlist_next_nvpair(new_unvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(new_unvl, nvp)) {
		name = nvpair_name(nvp);
		if (LLDP_NVP_PRIVATE(name))
			continue;
		if (nvlist_add_nvpair(added_unvl, nvp) != 0)
			return (-1);
		added = B_TRUE;

	}
	return ((added ? 1 : 0));
}

static int
i_lldp_cmp_organisation_tlvs(nvlist_t *cur_nvl, nvlist_t *new_nvl,
    nvlist_t *added_tlvnvl, nvlist_t *deleted_tlvnvl, nvlist_t *modified_tlvnvl)
{
	lldp_tlv_info_t	*infop;
	nvpair_t	*nvp, *onvp;
	nvlist_t	*cur_onvl = NULL, *new_onvl = NULL;
	nvlist_t	*cur_ouinvl = NULL, *new_ouinvl = NULL;
	nvlist_t	*added_ouinvl = NULL, *added_onvl = NULL;
	char		*stypename, *ouiname;
	uint_t		cur_nelem, new_nelem;
	boolean_t	change = B_FALSE;

	(void) nvlist_lookup_nvlist(cur_nvl, LLDP_NVP_ORGANIZATION, &cur_onvl);
	(void) nvlist_lookup_nvlist(new_nvl, LLDP_NVP_ORGANIZATION, &new_onvl);

	cur_nelem = lldp_nvlist_nelem(cur_onvl);
	new_nelem = lldp_nvlist_nelem(new_onvl);
	if (cur_nelem == 0 && new_nelem == 0)
		return (0);

	for (nvp = nvlist_next_nvpair(cur_onvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(cur_onvl, nvp)) {
		ouiname = nvpair_name(nvp);
		if (LLDP_NVP_PRIVATE(ouiname))
			continue;
		cur_ouinvl = new_ouinvl = NULL;
		(void) nvpair_value_nvlist(nvp, &cur_ouinvl);
		(void) lldp_get_nested_nvl(new_nvl, LLDP_NVP_ORGANIZATION,
		    ouiname, NULL, &new_ouinvl);
		for (onvp = nvlist_next_nvpair(cur_ouinvl, NULL); onvp != NULL;
		    onvp = nvlist_next_nvpair(cur_ouinvl, onvp)) {
			stypename = nvpair_name(onvp);
			if (LLDP_NVP_PRIVATE(stypename))
				continue;
			infop = lldp_get_tlvinfo_from_nvpname(stypename);
			if (infop != NULL && infop->lti_cmpf != NULL) {
				int ret;

				ret = infop->lti_cmpf(cur_nvl, new_nvl,
				    added_tlvnvl, deleted_tlvnvl,
				    modified_tlvnvl);
				if (ret < 0)
					return (-1);
				if (ret > 0)
					change = B_TRUE;
			}
			(void) nvlist_remove_all(new_ouinvl, stypename);
		}
		if (lldp_create_nested_nvl(added_tlvnvl, LLDP_NVP_ORGANIZATION,
		    ouiname, NULL, &added_ouinvl) != 0) {
			return (-1);
		}
		/* now walk through any remaining elements in new_ouinvl */
		for (onvp = nvlist_next_nvpair(new_ouinvl, NULL); onvp != NULL;
		    onvp = nvlist_next_nvpair(new_ouinvl, onvp)) {
			stypename = nvpair_name(onvp);
			if (LLDP_NVP_PRIVATE(stypename))
				continue;
			if (nvlist_add_nvpair(added_ouinvl, onvp) != 0)
				return (-1);
			change = B_TRUE;
		}
		(void) nvlist_remove_all(new_onvl, ouiname);
	}
	/* now walk through any remaining elements in new_onvl */
	if (lldp_create_nested_nvl(added_tlvnvl, LLDP_NVP_ORGANIZATION, NULL,
	    NULL, &added_onvl) != 0)
		return (-1);
	for (nvp = nvlist_next_nvpair(new_onvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(new_onvl, nvp)) {
		ouiname = nvpair_name(nvp);
		if (LLDP_NVP_PRIVATE(ouiname))
			continue;
		if (nvlist_add_nvpair(added_onvl, nvp) != 0)
			return (-1);
		change = B_TRUE;
	}
	return ((change ? 1 : 0));
}

int
lldp_cmp_rmib_objects(nvlist_t *cur_nvl, nvlist_t *new_nvl,
    nvlist_t *added_tlvnvl, nvlist_t *deleted_tlvnvl, nvlist_t *modified_tlvnvl)
{
	lldp_tlv_info_t	*infop;
	nvpair_t	*nvp;
	char		*name;
	boolean_t	change = B_FALSE;
	int		ret;

	for (nvp = nvlist_next_nvpair(cur_nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(cur_nvl, nvp)) {
		name = nvpair_name(nvp);
		if (LLDP_NVP_PRIVATE(name))
			continue;
		ret = 0;
		if (strcmp(name, LLDP_NVP_ORGANIZATION) != 0) {
			infop = lldp_get_tlvinfo_from_nvpname(name);
			if (infop != NULL && infop->lti_cmpf != NULL) {
				ret = infop->lti_cmpf(cur_nvl, new_nvl,
				    added_tlvnvl, deleted_tlvnvl,
				    modified_tlvnvl);
			}
		} else {
			ret = i_lldp_cmp_organisation_tlvs(cur_nvl, new_nvl,
			    added_tlvnvl, deleted_tlvnvl, modified_tlvnvl);
		}
		if (ret < 0)
			return (-1);
		if (ret > 0)
			change = B_TRUE;
		/* now lets delete this node from new_nvl */
		(void) nvlist_remove_all(new_nvl, name);
	}

	/* now walk through any remaining elements in new_nvl */
	for (nvp = nvlist_next_nvpair(new_nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(new_nvl, nvp)) {
		name = nvpair_name(nvp);
		if (LLDP_NVP_PRIVATE(name))
			continue;
		if (nvlist_add_nvpair(added_tlvnvl, nvp) != 0)
			return (-1);
		change = B_TRUE;
	}
	return ((change ? 1 : 0));
}
