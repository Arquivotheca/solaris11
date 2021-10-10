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
#include <sys/types.h>
#include <sys/errno.h>
#include <liblldp.h>
#include <lldp.h>
#include "snoop.h"

static void
print_maxframesz_info(lldp_tlv_t *tlv)
{
	uint16_t	fsz;

	if (lldp_tlv2maxfsz(tlv, &fsz) != 0) {
		(void) snprintf(get_line(0, 0), get_line_remain(),
		    "Corrupt Max Frame Size TLV");
		return;
	}
	(void) snprintf(get_line(0, 0), get_line_remain(),
	    "Max Frame Size: %u", fsz);
}

static void
print_pfc_info(lldp_tlv_t *tlv)
{
	lldp_pfc_t	pfc;

	bzero(&pfc, sizeof (pfc));
	if (lldp_tlv2pfc(tlv, &pfc) != 0) {
		(void) snprintf(get_line(0, 0), get_line_remain(),
		    "Corrupt PFC TLV");
		return;
	}
	(void) snprintf(get_line(0, 0), get_line_remain(),
	    "PFC Willing: %s", pfc.lp_willing == 1 ? "True" : "False");
	(void) snprintf(get_line(0, 0), get_line_remain(),
	    "PFC MBC: %s", pfc.lp_mbc == 1 ? "True" : "False");
	(void) snprintf(get_line(0, 0), get_line_remain(),
	    "PFC Cap: %u", pfc.lp_cap);
	(void) snprintf(get_line(0, 0), get_line_remain(),
	    "PFC Enable: %u", pfc.lp_enable);
}

static void
print_appln_info(lldp_tlv_t *tlv)
{
	lldp_appln_t	*appln;
	lldp_appln_t	*app;
	uint_t		nappln;
	int		i;

	if (lldp_tlv2appln(tlv, &appln, &nappln) != 0) {
		(void) snprintf(get_line(0, 0), get_line_remain(),
		    "Corrupt Application TLV");
		return;
	}
	app = appln;
	for (i = 0; i < nappln; i++) {
		(void) snprintf(get_line(0, 0), get_line_remain(),
		    "Application ID: %x", app->la_id);
		(void) snprintf(get_line(0, 0), get_line_remain(),
		    "Application priority: %u", app->la_pri);
		(void) snprintf(get_line(0, 0), get_line_remain(),
		    "Application SF: %u (%s)", app->la_sel,
		    dcbx_appln_sel2str(app->la_sel));
		app++;
	}
	free(appln);
}

static void
print_vlan_info(lldp_tlv_t *tlv)
{
	lldp_vlan_info_t	lvi;

	bzero(&lvi, sizeof (lvi));
	if (lldp_tlv2vlan(tlv, &lvi) != 0) {
		(void) snprintf(get_line(0, 0), get_line_remain(),
		    "Corrupt VLAN TLV");
		return;
	}
	(void) snprintf(get_line(0, 0), get_line_remain(),
	    "VLAN Name: %s", lvi.lvi_name);
	(void) snprintf(get_line(0, 0), get_line_remain(),
	    "VLAN ID: %u", lvi.lvi_vid);
}

static void
print_pvid_info(lldp_tlv_t *tlv)
{
	uint16_t	pvid;

	if (lldp_tlv2pvid(tlv, &pvid) != 0) {
		(void) snprintf(get_line(0, 0), get_line_remain(),
		    "Corrupt PVID TLV");
		return;
	}
	(void) snprintf(get_line(0, 0), get_line_remain(),
	    "PVID: %u", pvid);
}

static void
print_vnic_info(lldp_tlv_t *tlv)
{
	lldp_vnic_info_t	lvi;
	lldp_portid_t		*pid;
	char			pidstr[LLDP_MAX_PORTIDSTRLEN];

	bzero(&lvi, sizeof (lvi));
	if (lldp_tlv2vnic(tlv, &lvi) != 0) {
		(void) snprintf(get_line(0, 0), get_line_remain(),
		    "Corrupt VNIC TLV");
		return;
	}
	pid = &lvi.lvni_portid;
	(void) snprintf(get_line(0, 0), get_line_remain(),
	    "VNIC Port ID Subtype: %d (%s)", pid->lp_subtype,
	    lldp_port_subtype2str(pid->lp_subtype));
	(void) lldp_portID2str(pid, pidstr, sizeof (pidstr));
	(void) snprintf(get_line(0, 0), get_line_remain(),
	    "VNIC Port ID: %s", pidstr);
	(void) snprintf(get_line(0, 0), get_line_remain(),
	    "VNIC VLAN ID: %u", lvi.lvni_vid);
}

static void
print_aggr_info(lldp_tlv_t *tlv)
{
	lldp_aggr_t	aggr;

	bzero(&aggr, sizeof (aggr));
	if (lldp_tlv2aggr(tlv, &aggr) != 0) {
		(void) snprintf(get_line(0, 0), get_line_remain(),
		    "Corrupt Aggregation TLV");
		return;
	}

	if (aggr.la_status & LLDP_AGGR_MEMBER) {
		(void) snprintf(get_line(0, 0), get_line_remain(),
		    "Aggregation Status: Capable, Aggregated");
	} else if (aggr.la_status & LLDP_AGGR_CAPABLE) {
		(void) snprintf(get_line(0, 0), get_line_remain(),
		    "Aggregation Status: Capable");
	}
	(void) snprintf(get_line(0, 0), get_line_remain(),
	    "Aggregation ID: %u", aggr.la_id);
}

static void
print_bytearray(uint8_t *ba, int balen, char *str, size_t slen, char *pstr)
{
	char	*cp = str;
	int	i = 0;

	for (i = 0; i < balen; i++) {
		(void) snprintf(cp, slen, "%02x.", ba[i]);
		cp += 3;
		slen -= 3;
	}
	(void) snprintf(get_line(0, 0), get_line_remain(), "%s : %s", pstr,
	    str);
}

void
interpret_lldp(int flags, uint8_t *lp, int llen)
{
	uint8_t			*pdu = lp;
	char			*line;
	lldp_tlv_t		tlv;
	uint8_t			*end;
	lldp_chassisid_t	cid;
	lldp_portid_t		pid;
	uint16_t		ttl;
	char			sysname[LLDP_MAX_SYSNAMELEN];
	char			sysdesc[LLDP_MAX_SYSDESCLEN];
	char			portdesc[LLDP_MAX_PORTDESCLEN];
	lldp_syscapab_t		sc;
	char			unknown[LLDP_MAX_PDULEN];
	char			moidstr[2 * LLDP_STRSIZE];
	char			cidstr[LLDP_MAX_CHASSISIDSTRLEN];
	char			pidstr[LLDP_MAX_PORTIDSTRLEN];
	char			syscapabstr[LLDP_STRSIZE];
	char			addrstr[INET6_ADDRSTRLEN];
	lldp_mgmtaddr_t		maddr;
	uint16_t		tlvlen;
	int			len = 0;
	uint32_t		oui;
	uint32_t		stype;

	bzero(&cid, sizeof (cid));
	bzero(&pid, sizeof (pid));
	bzero(&sc, sizeof (sc));
	bzero(&maddr, sizeof (maddr));

	if (flags & F_SUM) {
		line = get_sum_line();
		len = snprintf(line, MAXLINE, "LLDP PDU");
	} else {
		show_header("LLDP:  ", "Link Layer Discovery Protocol Frame",
		    llen);
		show_space();
	}
	lldp_firsttlv(pdu, llen, &tlv);

	while (tlv.lt_value != NULL) {
		if (flags & F_SUM) {
			switch (tlv.lt_type) {
			case LLDP_TLVTYPE_CHASSIS_ID:
				if (lldp_tlv2chassisid(&tlv, &cid) != 0)
					break;
				(void) lldp_chassisID2str(&cid, cidstr,
				    sizeof (cidstr));
				len += snprintf(line + len, MAXLINE - len,
				    " Chassis ID = %s ", cidstr);
				break;
			case LLDP_TLVTYPE_PORT_ID:
				if (lldp_tlv2portid(&tlv, &pid) != 0)
					break;
				(void) lldp_portID2str(&pid, pidstr,
				    sizeof (pidstr));
				len += snprintf(line + len, MAXLINE - len,
				    " Port ID = %s ", pidstr);
				break;
			case LLDP_TLVTYPE_TTL:
				if (lldp_tlv2ttl(&tlv, &ttl) != 0)
					break;
				len += snprintf(line + len, MAXLINE - len,
				    " TTL = %u", ttl);
				break;
			case LLDP_TLVTYPE_END:
				return;
			default:
				break;
			}
			lldp_nexttlv(pdu, llen, &tlv, &tlv);
			continue;
		}

		/* F_DTAIL */
		switch (tlv.lt_type) {
		case LLDP_TLVTYPE_CHASSIS_ID:
			if (lldp_tlv2chassisid(&tlv, &cid) != 0)
				break;

			(void) lldp_chassisID2str(&cid, cidstr,
			    sizeof (cidstr));
			(void) snprintf(get_line(0, 0), get_line_remain(),
			    "Chassis ID Subtype: %d (%s)", cid.lc_subtype,
			    lldp_chassis_subtype2str(cid.lc_subtype));
			(void) snprintf(get_line(0, 0), get_line_remain(),
			    "Chassis ID: %s", cidstr);
			break;

		case LLDP_TLVTYPE_PORT_ID:
			if (lldp_tlv2portid(&tlv, &pid) != 0)
				break;

			(void) lldp_portID2str(&pid, pidstr, sizeof (pidstr));
			(void) snprintf(get_line(0, 0), get_line_remain(),
			    "Port ID Subtype: %d (%s)", pid.lp_subtype,
			    lldp_port_subtype2str(pid.lp_subtype));
			(void) snprintf(get_line(0, 0), get_line_remain(),
			    "Port ID: %s", pidstr);
			break;

		case LLDP_TLVTYPE_TTL:
			if (lldp_tlv2ttl(&tlv, &ttl) != 0)
				break;

			(void) snprintf(get_line(0, 0), get_line_remain(),
			    "TTL: %u (seconds)", ttl);
			break;

		case LLDP_TLVTYPE_PORT_DESC:
			if (lldp_tlv2portdescr(&tlv, portdesc) != 0)
				break;

			(void) snprintf(get_line(0, 0), get_line_remain(),
			    "Port Description: %s", portdesc);
			break;

		case LLDP_TLVTYPE_SYS_NAME:
			if (lldp_tlv2sysname(&tlv, sysname) != 0)
				break;

			(void) snprintf(get_line(0, 0), get_line_remain(),
			    "System Name: %s", sysname);
			break;

		case LLDP_TLVTYPE_SYS_DESC:
			if (lldp_tlv2sysdescr(&tlv, sysdesc) != 0)
				break;

			(void) snprintf(get_line(0, 0), get_line_remain(),
			    "System Description: %s", sysdesc);
			break;

		case LLDP_TLVTYPE_SYS_CAPAB:
			if (lldp_tlv2syscapab(&tlv, &sc) != 0)
				break;

			lldp_syscapab2str(sc.ls_sup_syscapab, syscapabstr,
			    LLDP_STRSIZE);
			(void) snprintf(get_line(0, 0), get_line_remain(),
			    "System Capabilities: (%x) %s", sc.ls_sup_syscapab,
			    syscapabstr);

			lldp_syscapab2str(sc.ls_enab_syscapab, syscapabstr,
			    LLDP_STRSIZE);
			(void) snprintf(get_line(0, 0), get_line_remain(),
			    "Enabled Capabilities: (%x) %s",
			    sc.ls_enab_syscapab, syscapabstr);
			break;
		case LLDP_TLVTYPE_MGMT_ADDR:
			if (lldp_tlv2mgmtaddr(&tlv, &maddr) != 0)
				break;

			(void) snprintf(get_line(0, 0), get_line_remain(),
			    "Management Address Length: %u", maddr.lm_addrlen);
			(void) snprintf(get_line(0, 0), get_line_remain(),
			    "Management Address Subtype: %u (%s)",
			    maddr.lm_subtype,
			    lldp_maddr_subtype2str(maddr.lm_subtype));
			lldp_mgmtaddr2str(&maddr, addrstr, INET6_ADDRSTRLEN);
			(void) snprintf(get_line(0, 0),
			    get_line_remain(), "Management Address: %s",
			    addrstr);
			(void) snprintf(get_line(0, 0), get_line_remain(),
			    "Interface Numbering Subtype: %u (%s)",
			    maddr.lm_iftype,
			    lldp_maddr_ifsubtype2str(maddr.lm_iftype));
			(void) snprintf(get_line(0, 0), get_line_remain(),
			    "Interface Number: %u", maddr.lm_ifnumber);
			(void) snprintf(get_line(0, 0), get_line_remain(),
			    "OID String Length: %u", maddr.lm_oidlen);
			if (maddr.lm_oidlen > 0) {
				print_bytearray(maddr.lm_oid, maddr.lm_oidlen,
				    moidstr, 2 * LLDP_STRSIZE,
				    "Object Identifier");
			}
			break;
		case LLDP_ORGSPECIFIC_TLVTYPE:
			lldp_get_ouistype(&tlv, &oui, &stype);
			switch (oui) {
			case LLDP_802dot1_OUI:
				switch (stype) {
				case LLDP_802dot1OUI_VLAN_NAME_SUBTYPE:
					print_vlan_info(&tlv);
					break;
				case LLDP_802dot1OUI_PVID_SUBTYPE:
					print_pvid_info(&tlv);
					break;
				case LLDP_802dot1OUI_PFC_SUBTYPE:
					print_pfc_info(&tlv);
					break;
				case LLDP_802dot1OUI_APPLN_SUBTYPE:
					print_appln_info(&tlv);
					break;
				case LLDP_802dot1OUI_LINK_AGGR_SUBTYPE:
					print_aggr_info(&tlv);
					break;
				default:
					goto deftlv;
				}
				break;
			case LLDP_802dot3_OUI:
				switch (stype) {
				case LLDP_802dot3OUI_MAXFRAMESZ_SUBTYPE:
					print_maxframesz_info(&tlv);
					break;
				default:
					goto deftlv;
				}
				break;
			case LLDP_ORACLE_OUI:
				switch (stype) {
				case LLDP_ORACLEOUI_VNIC_SUBTYPE:
					print_vnic_info(&tlv);
					break;
				default:
					goto deftlv;
				}
				break;
			default:
				goto deftlv;
			}
			break;
		case LLDP_TLVTYPE_END:
			(void) snprintf(get_line(0, 0), get_line_remain(),
			    "End of LLDP PDU TLV");
			return;
		default:
	deftlv:
			if (lldp_tlv2unknown(&tlv, unknown,
			    LLDP_MAX_PDULEN) != 0) {
				break;
			}
			(void) snprintf(get_line(0, 0), get_line_remain(),
			    "Unknown TLV: %s", unknown);
			break;
		}
		lldp_nexttlv(pdu, llen, &tlv, &tlv);
	}
}
