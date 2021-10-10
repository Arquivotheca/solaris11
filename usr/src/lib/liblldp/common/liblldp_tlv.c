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

#include <string.h>
#include <strings.h>
#include <lldp.h>
#include <liblldp.h>
#include <sys/types.h>
#include <sys/vlan.h>
#include <unistd.h>
#include <netinet/in.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <liblldp_lldpd.h>

void
lldp_firsttlv(uint8_t *pdu, int pdulen, lldp_tlv_t *tlv)
{
	uint8_t		*end = pdu + pdulen;

	tlv->lt_value = NULL;
	if (pdu + 2 > end)
		return;

	tlv->lt_type = LLDP_TLV_TYPE(pdu);
	tlv->lt_len = LLDP_TLV_LEN(pdu);
	if (pdu + 2 + tlv->lt_len > end)
		return;
	tlv->lt_value = pdu + 2;
}

void
lldp_nexttlv(uint8_t *pdu, int pdulen, lldp_tlv_t *old, lldp_tlv_t *new)
{
	uint8_t		*cur = old->lt_value;
	uint8_t		*end = pdu + pdulen;

	new->lt_value = NULL;
	if (cur + old->lt_len + 2 > end)
		return;
	cur += old->lt_len;
	new->lt_type = LLDP_TLV_TYPE(cur);
	new->lt_len = LLDP_TLV_LEN(cur);
	if (cur + 2 + new->lt_len > end)
		return;
	new->lt_value = cur + 2;
}

void
lldp_set_typelen(uint8_t *lldpdu, uint8_t type, uint16_t len)
{
	uint16_t	tl = type;

	/*
	 * `len' always includes 7 bits of type and 9 bits of length of
	 * TLV, i.e., 2 bytes of type and length, so subtract 2 from `len'
	 * to get the actual length of the value.
	 */
	len -= LLDP_TLVHDR_SZ;
	tl = tl << 9;
	tl |= (len & 0x01FF);

	*(uint16_t *)(void *)lldpdu = htons(tl);
}

void
lldp_set_orgspecid_subtype(uint8_t *lldpdu, uint8_t subtype, uint32_t oui,
    uint16_t len)
{
	uint32_t	ouistype = subtype;

	/* first set the org. specific tlv type */
	lldp_set_typelen(lldpdu, LLDP_ORGSPECIFIC_TLVTYPE, len);
	lldpdu += LLDP_TLVHDR_SZ;

	ouistype |= (oui << 8);
	*(uint32_t *)(void *)lldpdu = htonl(ouistype);
}

int
lldp_end2pdu(uint8_t *lldpdu, size_t pdusize, size_t *msglen)
{
	size_t	tlvlen = LLDP_TLVHDR_SZ;

	if (tlvlen > pdusize)
		return (ENOBUFS);
	lldp_set_typelen(lldpdu, LLDP_TLVTYPE_END, tlvlen);
	*msglen += tlvlen;
	return (0);
}

/*
 *  +--------+----------------+--------------+-------------+
 *  |	TLV  | TLV information|  Chassis ID  | Chassis ID  |
 *  |   Type |   string len   |  TLV subtype |             |
 *  +--------+----------------+--------------+-------------+
 *	7bits      9 bits        8 bits      1 to 255 octets
 */

/*
 * Depending on the value of the chassis ID subtype, the chassis ID can
 * contain alphanumeric data or binary data.
 */
char *
lldp_chassisID2str(lldp_chassisid_t *cid, char *cstr, size_t clen)
{
	int	i = 0;
	int	len = 0;
	char	*cp = cstr;

	bzero(cstr, clen);
	switch (cid->lc_subtype) {
	case LLDP_CHASSIS_ID_IFALIAS:
	case LLDP_CHASSIS_ID_IFNAME:
	case LLDP_CHASSIS_ID_LOCAL:
		(void) strncpy(cstr, (char *)(cid->lc_cid), cid->lc_cidlen);
		break;
	case LLDP_CHASSIS_ID_CHASSIS_COMPONENT:
	case LLDP_CHASSIS_ID_PORT_COMPONENT:
	case LLDP_CHASSIS_ID_MACADDRESS:
		/* convert the series of bytes into numeric string */
		for (i = 0; i < cid->lc_cidlen; i++) {
			if (i == 0) {
				len = snprintf(cp, clen, "%02x",
				    cid->lc_cid[i]);
			} else {
				len = snprintf(cp, clen, ":%02x",
				    cid->lc_cid[i]);
			}
			cp += len;
			clen -= 2;
		}
		break;
	case LLDP_CHASSIS_ID_IPADDRESS:
		cstr = (char *)inet_ntop(cid->lc_cid[i], &cid->lc_cid[i+1],
		    cstr, clen);
		break;
	default:
		return (NULL);
	}
	return (cstr);
}

char *
lldp_chassis_subtype2str(uint8_t type)
{
	switch (type) {
	case LLDP_CHASSIS_ID_CHASSIS_COMPONENT:
		return ("ChassisComponent");
	case LLDP_CHASSIS_ID_IFALIAS:
		return ("InterfaceAlias");
	case LLDP_CHASSIS_ID_IFNAME:
		return ("InterfaceName");
	case LLDP_CHASSIS_ID_LOCAL:
		return ("Local");
	case LLDP_CHASSIS_ID_PORT_COMPONENT:
		return ("PortComponent");
	case LLDP_CHASSIS_ID_MACADDRESS:
		return ("MacAddress");
	case LLDP_CHASSIS_ID_IPADDRESS:
		return ("NetworkAddress");
	}
	return ("Unknown");
}

int
lldp_nvlist2chassisid(nvlist_t *tlv_nvl, lldp_chassisid_t *cid)
{
	int		err;
	nvlist_t	*nvl;
	uint8_t		*cidarr;

	if ((err = nvlist_lookup_nvlist(tlv_nvl, LLDP_NVP_CHASSISID,
	    &nvl)) != 0) {
		return (err);
	}

	if ((err = nvlist_lookup_uint8(nvl, LLDP_NVP_CHASSISID_TYPE,
	    &cid->lc_subtype)) != 0) {
		return (err);
	}
	if ((err = nvlist_lookup_byte_array(nvl, LLDP_NVP_CHASSISID_VALUE,
	    &cidarr, &cid->lc_cidlen)) != 0) {
		return (err);
	}
	(void) memcpy(cid->lc_cid, cidarr, cid->lc_cidlen);

	return (err);
}

int
lldp_tlv2chassisid(lldp_tlv_t *tlv, lldp_chassisid_t *cid)
{
	char	cidstr[LLDP_MAX_CHASSISIDSTRLEN];

	if (tlv->lt_len < 2 || tlv->lt_len > 256)
		return (EPROTO);

	cid->lc_subtype = *tlv->lt_value;
	cid->lc_cidlen = tlv->lt_len - 1;
	(void) memcpy(cid->lc_cid, tlv->lt_value + 1, cid->lc_cidlen);

	/* check to see if chassis ID is framed correctly */
	if (lldp_chassisID2str(cid, cidstr, sizeof (cidstr)) == NULL)
		return (EPROTO);

	return (0);
}

int
lldp_chassisid2pdu(lldp_chassisid_t *cid, uint8_t *lldpdu, size_t pdusize,
    size_t *msglen)
{
	size_t			tlvlen;

	tlvlen = LLDP_TLVHDR_SZ + cid->lc_cidlen + sizeof (cid->lc_subtype);
	if (tlvlen > pdusize)
		return (ENOBUFS);
	lldp_set_typelen(lldpdu, LLDP_TLVTYPE_CHASSIS_ID, tlvlen);
	lldpdu += LLDP_TLVHDR_SZ;
	bcopy(&cid->lc_subtype, lldpdu, sizeof (cid->lc_subtype));
	lldpdu += sizeof (cid->lc_subtype);
	bcopy(cid->lc_cid, lldpdu, cid->lc_cidlen);
	*msglen += tlvlen;
	return (0);
}

/*
 *  +--------+----------------+--------------+-------------+
 *  |	TLV  | TLV information|  Port ID     | Port ID     |
 *  |   Type |   string len   |  TLV subtype |             |
 *  +--------+----------------+--------------+-------------+
 *    7bits      9 bits        8 bits      1 to 255 octets
 */

/*
 * Depending on the value of the port ID subtype, the port ID can
 * contain alphanumeric data or binary data.
 */
char *
lldp_portID2str(lldp_portid_t *pid, char *pstr, size_t plen)
{
	int	i = 0;
	int	len = 0;
	char	*cp = pstr;

	bzero(pstr, plen);
	switch (pid->lp_subtype) {
	case LLDP_PORT_ID_IFALIAS:
	case LLDP_PORT_ID_PORT_COMPONENT:
	case LLDP_PORT_ID_IFNAME:
	case LLDP_PORT_ID_LOCAL:
		(void) strncpy(pstr, (char *)(pid->lp_pid), pid->lp_pidlen);
		break;
	case LLDP_PORT_ID_MACADDRESS:
	case LLDP_PORT_ID_AGENT_CICRUITID:
		/* convert the series of bytes into numeric string */
		for (i = 0; i < pid->lp_pidlen; i++) {
			if (i == 0) {
				len = snprintf(cp, plen, "%02x",
				    pid->lp_pid[i]);
			} else {
				len = snprintf(cp, plen, ":%02x",
				    pid->lp_pid[i]);
			}
			cp += len;
			plen -= 2;
		}
		break;
	case LLDP_PORT_ID_IPADDRESS:
		pstr = (char *)inet_ntop(pid->lp_pid[i], &pid->lp_pid[i+1],
		    pstr, plen);
		break;
	default:
		return (NULL);
	}
	return (pstr);
}

char *
lldp_port_subtype2str(uint8_t type)
{
	switch (type) {
	case LLDP_PORT_ID_IFALIAS:
		return ("InterfaceAlias");
	case LLDP_PORT_ID_PORT_COMPONENT:
		return ("PortComponent");
	case LLDP_PORT_ID_IFNAME:
		return ("InterfaceName");
	case LLDP_PORT_ID_LOCAL:
		return ("Local");
	case LLDP_PORT_ID_MACADDRESS:
		return ("MacAddress");
	case LLDP_PORT_ID_AGENT_CICRUITID:
		return ("AgentCircuitId");
	case LLDP_PORT_ID_IPADDRESS:
		return ("NetworkAddress");
	}
	return ("Unknown");
}

/* Get the  port ID TLV */
int
lldp_nvlist2portid(nvlist_t *tlv_nvl, lldp_portid_t *pid)
{
	int		err = 0;
	nvlist_t	*nvl;
	uint8_t		*pidarr;

	if ((err = nvlist_lookup_nvlist(tlv_nvl, LLDP_NVP_PORTID,
	    &nvl)) != 0) {
		return (err);
	}
	if ((err = nvlist_lookup_uint8(nvl, LLDP_NVP_PORTID_TYPE,
	    &pid->lp_subtype)) != 0) {
		return (err);
	}

	if ((err = nvlist_lookup_byte_array(nvl, LLDP_NVP_PORTID_VALUE,
	    &pidarr, &pid->lp_pidlen)) != 0) {
		return (err);
	}
	(void) memcpy(pid->lp_pid, pidarr, pid->lp_pidlen);

	return (err);
}

int
lldp_tlv2portid(lldp_tlv_t *tlv, lldp_portid_t *pid)
{
	char	pidstr[LLDP_MAX_PORTIDSTRLEN];

	if (tlv->lt_len < 2 || tlv->lt_len > 256)
		return (EPROTO);

	pid->lp_subtype = *tlv->lt_value;
	pid->lp_pidlen = tlv->lt_len - 1;
	(void) memcpy(pid->lp_pid, tlv->lt_value + 1, pid->lp_pidlen);

	/* check to see if portID is framed correctly */
	if (lldp_portID2str(pid, pidstr, sizeof (pidstr)) == NULL)
		return (EPROTO);

	return (0);
}

int
lldp_portid2pdu(lldp_portid_t *pid, uint8_t *lldpdu, size_t pdusize,
    size_t *msglen)
{
	size_t		tlvlen;

	tlvlen = LLDP_TLVHDR_SZ + pid->lp_pidlen + sizeof (pid->lp_subtype);
	if (tlvlen > pdusize)
		return (ENOBUFS);

	lldp_set_typelen(lldpdu, LLDP_TLVTYPE_PORT_ID, tlvlen);
	lldpdu += LLDP_TLVHDR_SZ;
	bcopy(&pid->lp_subtype, lldpdu, sizeof (pid->lp_subtype));
	lldpdu += sizeof (pid->lp_subtype);
	bcopy(pid->lp_pid, lldpdu, pid->lp_pidlen);
	*msglen += tlvlen;
	return (0);
}

/*
 *  +--------+----------------+----------------------------+
 *  |	TLV  | TLV information|   time to live (TTL)       |
 *  |   Type |   string len   |                            |
 *  +--------+----------------+----------------------------+
 *	7bits      9 bits            2 octets
 */

/* Get the TTL TLV */
int
lldp_nvlist2ttl(nvlist_t *tlv_nvl, uint16_t *ttl)
{
	return (nvlist_lookup_uint16(tlv_nvl, LLDP_NVP_TTL, ttl));
}

int
lldp_tlv2ttl(lldp_tlv_t *tlv, uint16_t *ttl)
{
	/*
	 * As per 9.2.7.7.2 of IEEE802.1AB, it's fine if the length
	 * is greater than two. However we copy only the first 2 bytes
	 */
	if (tlv->lt_len < 2)
		return (EPROTO);

	(void) memcpy(ttl, tlv->lt_value, sizeof (uint16_t));
	*ttl = ntohs(*ttl);
	return (0);
}

int
lldp_ttl2pdu(uint16_t ttl, uint8_t *lldpdu, size_t pdusize, size_t *msglen)
{
	size_t		tlvlen;

	tlvlen = LLDP_TLVHDR_SZ + sizeof (uint16_t);
	if (tlvlen > pdusize)
		return (ENOBUFS);
	lldp_set_typelen(lldpdu, LLDP_TLVTYPE_TTL, tlvlen);
	lldpdu += LLDP_TLVHDR_SZ;
	bcopy(&ttl, lldpdu, sizeof (ttl));
	*msglen += tlvlen;
	return (0);
}

/* General routine to add a string to an LLDPDU */
static int
lldp_str2pdu(uint_t tlvtype, const char *str, uint8_t *lldpdu, size_t pdusize,
    size_t *msglen)
{
	size_t	tlvlen;

	tlvlen = strlen(str) + LLDP_TLVHDR_SZ;
	/* check to see if we can accomodate this info in the pdu */
	if (tlvlen > pdusize)
		return (ENOBUFS);
	lldp_set_typelen(lldpdu, tlvtype, tlvlen);
	lldpdu += LLDP_TLVHDR_SZ;
	bcopy(str, lldpdu, tlvlen - LLDP_TLVHDR_SZ);
	*msglen += tlvlen;
	return (0);
}

/* General routine to parse a string from LLDPDU */
static int
lldp_tlv2str(lldp_tlv_t *tlv, char *str)
{
	(void) memcpy(str, tlv->lt_value, tlv->lt_len);
	str[tlv->lt_len] = '\0';
	/*
	 * If the TLV information string length value is not exactly equal to
	 * the sum of the lengths of all fields contained in the TLV
	 * information string then we discard the LLDPDU.
	 */
	if (strlen(str) != tlv->lt_len)
		return (EPROTO);
	return (0);
}

/*
 * Port Description TLV
 *  +--------+----------------+----------------------------+
 *  |	TLV  | TLV information|   port description	   |
 *  |   Type |   string len   |                            |
 *  +--------+----------------+----------------------------+
 *	7bits      9 bits          0 to 255 octets
 */

/* Get the port desc TLV */
int
lldp_nvlist2portdescr(nvlist_t *tlv_nvl, char **portdescr)
{
	return (nvlist_lookup_string(tlv_nvl, LLDP_NVP_PORTDESC, portdescr));
}

int
lldp_tlv2portdescr(lldp_tlv_t *tlv, char *pdescr)
{
	if (tlv->lt_len > (LLDP_MAX_PORTDESCLEN - 1))
		return (EPROTO);

	return (lldp_tlv2str(tlv, pdescr));
}

int
lldp_portdescr2pdu(const char *pdesc, uint8_t *lldpdu, size_t pdusize,
    size_t *msglen)
{
	return (lldp_str2pdu(LLDP_TLVTYPE_PORT_DESC, pdesc, lldpdu,
	    pdusize, msglen));
}

/*
 * System Name TLV
 *  +--------+----------------+----------------------------+
 *  |	TLV  | TLV information|   system name              |
 *  |   Type |   string len   |                            |
 *  +--------+----------------+----------------------------+
 *	7bits      9 bits          0 to 255 octets
 */

/* Get the system Name TLV */
int
lldp_nvlist2sysname(nvlist_t *tlv_nvl, char **sysname)
{
	return (nvlist_lookup_string(tlv_nvl, LLDP_NVP_SYSNAME, sysname));
}

int
lldp_tlv2sysname(lldp_tlv_t *tlv, char *sysname)
{
	if (tlv->lt_len > (LLDP_MAX_SYSNAMELEN - 1))
		return (EPROTO);

	return (lldp_tlv2str(tlv, sysname));
}

int
lldp_sysname2pdu(const char *sname, uint8_t *lldpdu, size_t pdusize,
    size_t *msglen)
{
	return (lldp_str2pdu(LLDP_TLVTYPE_SYS_NAME, sname, lldpdu,
	    pdusize, msglen));
}

/*
 *  +--------+----------------+----------------------------+
 *  |	TLV  | TLV information|   system description       |
 *  |   Type |   string len   |                            |
 *   +--------+----------------+----------------------------+
 *	7bits      9 bits          0 to 255 octets
 */

/* Get the system description TLV */
int
lldp_nvlist2sysdescr(nvlist_t *tlv_nvl, char **sysdescr)
{
	return (nvlist_lookup_string(tlv_nvl, LLDP_NVP_SYSDESCR, sysdescr));
}

int
lldp_tlv2sysdescr(lldp_tlv_t *tlv, char *sysdescr)
{
	if (tlv->lt_len > (LLDP_MAX_SYSDESCLEN - 1))
		return (EPROTO);

	return (lldp_tlv2str(tlv, sysdescr));
}

int
lldp_sysdescr2pdu(const char *sdesc, uint8_t *lldpdu, size_t pdusize,
    size_t *msglen)
{
	return (lldp_str2pdu(LLDP_TLVTYPE_SYS_DESC, sdesc, lldpdu,
	    pdusize, msglen));
}

/*
 *  +--------+----------------+--------------+---------------+
 *  |	TLV  | TLV information| system       | enabled       |
 *  |   Type |   string len   | capabilities | capabilities  |
 *  +--------+----------------+--------------+---------------+
 *	7bits      9 bits        2 octets         2 octets
 */
void
lldp_syscapab2str(uint16_t capab, char *buf, size_t sz)
{
	int	i;
	uint_t	mask = 1;
	size_t	nbytes = 0;
	char	*syscapab[] = 	{LLDP_SYSCAPAB_OTHER_NAME,
				LLDP_SYSCAPAB_REPEATER_NAME,
				LLDP_SYSCAPAB_MACBRIDGE_NAME,
				LLDP_SYSCAPAB_WLAN_AP_NAME,
				LLDP_SYSCAPAB_ROUTER_NAME,
				LLDP_SYSCAPAB_TELEPHONE_NAME,
				LLDP_SYSCAPAB_DOCSIS_CD_NAME,
				LLDP_SYSCAPAB_STATION_NAME,
				LLDP_SYSCAPAB_CVLAN_NAME,
				LLDP_SYSCAPAB_SVLAN_NAME,
				LLDP_SYSCAPAB_TPMR_NAME};

	bzero(buf, sz);
	if (capab == 0)
		return;
	for (i = 0; i < LLDP_MAX_SYSCAPAB_TYPE; i++, mask <<= 1) {
		if (capab & mask) {
			if (nbytes > 0)
				(void) strlcat(buf, ",", sz);
			nbytes = strlcat(buf, syscapab[i], sz);
			if (nbytes >= sz)
				return;
		}
	}
}

/* Get the  sys capab TLV */
int
lldp_nvlist2syscapab(nvlist_t *tlv_nvl, lldp_syscapab_t *sc)
{
	nvlist_t	*nvl;
	int		err = 0;

	if ((err = nvlist_lookup_nvlist(tlv_nvl, LLDP_NVP_SYSCAPAB, &nvl)) != 0)
		return (err);

	if ((err = nvlist_lookup_uint16(nvl, LLDP_NVP_SUPPORTED_SYSCAPAB,
	    &sc->ls_sup_syscapab)) != 0) {
		return (err);
	}
	err = nvlist_lookup_uint16(nvl, LLDP_NVP_ENABLED_SYSCAPAB,
	    &sc->ls_enab_syscapab);
	return (err);
}

int
lldp_tlv2syscapab(lldp_tlv_t *tlv, lldp_syscapab_t *sc)
{
	if (tlv->lt_len != 4)
		return (EPROTO);

	(void) memcpy(sc, tlv->lt_value, sizeof (lldp_syscapab_t));
	sc->ls_sup_syscapab = ntohs(sc->ls_sup_syscapab);
	sc->ls_enab_syscapab = ntohs(sc->ls_enab_syscapab);

	return (0);
}

int
lldp_syscapab2pdu(lldp_syscapab_t *sc, uint8_t *lldpdu, size_t pdusize,
    size_t *msglen)
{
	size_t		tlvlen;

	tlvlen = LLDP_TLVHDR_SZ + sizeof (uint16_t) + sizeof (uint16_t);
	if (tlvlen > pdusize)
		return (ENOBUFS);
	lldp_set_typelen(lldpdu, LLDP_TLVTYPE_SYS_CAPAB, tlvlen);
	lldpdu += LLDP_TLVHDR_SZ;

	sc->ls_sup_syscapab = htons(sc->ls_sup_syscapab);
	sc->ls_enab_syscapab = htons(sc->ls_enab_syscapab);

	bcopy(&sc->ls_sup_syscapab, lldpdu, sizeof (uint16_t));
	lldpdu += sizeof (uint16_t);

	bcopy(&sc->ls_enab_syscapab, lldpdu, sizeof (uint16_t));

	*msglen += tlvlen;
	return (0);
}
/*
 *  +-----+-------+-------+-------+--------+-----------+----------+------+----+
 *  | TLV | TLV   | mgmt. | mgmt  | mgmt   | interface |interface | OID  |    |
 *  | Type| info. |address|address|address | numbering | number   |length| OID|
 *  |     |length |length |subtype|        |  subtype  |          |      |    |
 *  +-----+-------+-------+-------+--------+-----------+----------+------+----+
 *   7bits  9bits    1        1      1-31        1          4         1   0-128
 */
int
lldp_tlv2mgmtaddr(lldp_tlv_t *tlv, lldp_mgmtaddr_t *maddr)
{
	uint8_t	*value = tlv->lt_value;

	if (tlv->lt_len < 9 || tlv->lt_len > 167)
		return (EPROTO);

	bzero(maddr, sizeof (lldp_mgmtaddr_t));
	maddr->lm_addrlen = *value - 1;
	maddr->lm_subtype = *++value;

	(void) memcpy(&maddr->lm_addr, ++value, maddr->lm_addrlen);
	value += maddr->lm_addrlen;
	maddr->lm_iftype = *value;
	(void) memcpy(&maddr->lm_ifnumber, ++value,
	    sizeof (maddr->lm_ifnumber));
	maddr->lm_ifnumber = ntohl(maddr->lm_ifnumber);
	value += sizeof (maddr->lm_ifnumber);
	maddr->lm_oidlen = *value;
	(void) memcpy(&maddr->lm_oid, ++value, maddr->lm_oidlen);

	return (0);
}

int
lldp_sysport_mgmtaddr2pdu(uint8_t *macaddr, size_t macaddrlen, uint32_t pid,
    uint8_t *lldpdu, size_t pdusize, size_t *msglen)
{
	size_t		tlvlen;

	if (macaddr == NULL || macaddrlen == 0)
		return (EINVAL);

	tlvlen = LLDP_TLVHDR_SZ + macaddrlen + 8;
	if (tlvlen > pdusize)
		return (ENOBUFS);
	lldp_set_typelen(lldpdu, LLDP_TLVTYPE_MGMT_ADDR, tlvlen);
	lldpdu += LLDP_TLVHDR_SZ;
	*lldpdu++ = macaddrlen + 1;
	*lldpdu++ = LLDP_MGMTADDR_TYPE_ALL802;
	bcopy(macaddr, lldpdu, macaddrlen);
	lldpdu += macaddrlen;
	*lldpdu++ = LLDP_MGMTADDR_IFTYPE_SYSPORT;
	*(uint32_t *)(void *)lldpdu = htonl(pid);
	lldpdu += sizeof (uint32_t);
	*lldpdu = 0;
	*msglen += tlvlen;

	return (0);
}

int
lldp_mgmtaddr2pdu(lldp_mgmtaddr_t *maddr, uint8_t *lldpdu, size_t pdusize,
    size_t *msglen)
{
	size_t		tlvlen;

	tlvlen = LLDP_TLVHDR_SZ + maddr->lm_addrlen +
	    maddr->lm_oidlen + 8;
	if (tlvlen > pdusize)
		return (ENOBUFS);
	lldp_set_typelen(lldpdu, LLDP_TLVTYPE_MGMT_ADDR, tlvlen);
	lldpdu += LLDP_TLVHDR_SZ;
	*lldpdu++ = maddr->lm_addrlen + 1;
	*lldpdu++ = maddr->lm_subtype;
	bcopy(maddr->lm_addr, lldpdu, maddr->lm_addrlen);
	lldpdu += maddr->lm_addrlen;
	*lldpdu++ = maddr->lm_iftype;
	*(uint32_t *)(void *)lldpdu = htonl(maddr->lm_ifnumber);
	lldpdu += sizeof (uint32_t);
	*lldpdu++ = maddr->lm_oidlen;
	if (maddr->lm_oidlen != 0)
		bcopy(maddr->lm_oid, lldpdu, maddr->lm_oidlen);
	*msglen += tlvlen;

	return (0);
}

/* Get the organization specific OUI and subtype info */
void
lldp_get_ouistype(lldp_tlv_t *tlv, uint32_t *oui, uint32_t *subtype)
{
	uint32_t	ouistype;

	/* 3 bytes OUI + a byte of subtype */
	ouistype = ntohl(*(uint32_t *)(void *)tlv->lt_value);
	*oui = ((ouistype & 0xFFFFFF00) >> 8);
	*subtype = (ouistype & 0x000000FF);
}

/*
 *  +--------+----------------+----------+--------+------------+------------+
 *  |	TLV  | TLV information|  802.1OUI|  802.1 |aggregation | aggregated |
 *  |   Type |   string len   |  00-80-C2| subtype|   status   | port ID    |
 *  +--------+----------------+----------+--------+------------+------------+
 *	7bits      9 bits       3 octets   1 octet   1 octet     4 octets
 */
int
lldp_nvlist2aggr(nvlist_t *tlv_nvl, lldp_aggr_t *aggr)
{
	nvlist_t	*anvl;
	int		err;

	if ((err = lldp_get_nested_nvl(tlv_nvl, LLDP_NVP_ORGANIZATION,
	    LLDP_8021_OUI_LIST, LLDP_NVP_AGGR, &anvl)) != 0)
		return (err);

	if ((err = nvlist_lookup_uint8(anvl, LLDP_NVP_AGGR_STATUS,
	    &aggr->la_status)) != 0) {
		return (err);
	}
	return (nvlist_lookup_uint32(anvl, LLDP_NVP_AGGR_ID, &aggr->la_id));
}

int
lldp_tlv2aggr(lldp_tlv_t *tlv, lldp_aggr_t *ainfop)
{
	uint8_t	*value = tlv->lt_value;

	/*
	 * Should have the status and aggregation id.
	 */
	if (tlv->lt_len != (LLDP_ORGSPECHDR_SZ + sizeof (uint8_t) +
	    sizeof (uint32_t))) {
		return (EPROTO);
	}

	/* Move past the OUI and subtype */
	value += LLDP_ORGSPECHDR_SZ;

	bzero(ainfop, sizeof (lldp_aggr_t));
	(void) memcpy(&ainfop->la_status, value, sizeof (ainfop->la_status));
	value++;
	(void) memcpy(&ainfop->la_id, value, sizeof (ainfop->la_id));
	ainfop->la_id = ntohl(ainfop->la_id);

	return (0);
}

int
lldp_aggr2pdu(lldp_aggr_t *aggrp, uint8_t *lldpdu, size_t pdusize,
    size_t *msglen)
{
	size_t		tlvlen;

	tlvlen = LLDP_TLVHDR_SZ + LLDP_ORGSPECHDR_SZ + sizeof (uint8_t) +
	    sizeof (uint32_t);
	if (tlvlen > pdusize)
		return (ENOBUFS);
	lldp_set_orgspecid_subtype(lldpdu, LLDP_802dot1OUI_LINK_AGGR_SUBTYPE,
	    LLDP_802dot1_OUI, tlvlen);
	lldpdu += LLDP_TLVHDR_SZ + LLDP_ORGSPECHDR_SZ;

	*lldpdu = aggrp->la_status;
	lldpdu += sizeof (aggrp->la_status);

	*(uint32_t *)(void *)lldpdu = htonl(aggrp->la_id);
	*msglen += tlvlen;
	return (0);
}

/*
 *  +--------+----------------+--------------+-------------+---------------+
 *  |	TLV  | TLV information|  802.3OUI    |    802.3    | Maximum 802.3 |
 *  |   Type |   string len   |  00-12-0F    |   subtype   |   Frame size  |
 *  +--------+----------------+--------------+-------------+---------------+
 *    7bits      9 bits          3 octets         1 octet      2 octets
 */
int
lldp_nvlist2maxfsz(nvlist_t *tlv_nvl, uint16_t *fsz)
{
	nvlist_t	*fnvl;
	int		err;

	if ((err  = lldp_get_nested_nvl(tlv_nvl, LLDP_NVP_ORGANIZATION,
	    LLDP_8023_OUI_LIST, LLDP_NVP_MAXFRAMESZ, &fnvl)) != 0)
		return (err);
	return (nvlist_lookup_uint16(fnvl, LLDP_NVP_MAXFRAMESZ, fsz));
}

int
lldp_tlv2maxfsz(lldp_tlv_t *tlv, uint16_t *fsz)
{
	uint8_t	*value = tlv->lt_value;

	/* frame size is 2 bytes */
	if (tlv->lt_len != (LLDP_ORGSPECHDR_SZ + sizeof (uint16_t)))
		return (EPROTO);

	/* Move past the OUI and subtype */
	value += LLDP_ORGSPECHDR_SZ;

	/* extract the frame size */
	(void) memcpy(fsz, value, sizeof (uint16_t));
	*fsz = ntohs(*fsz);
	return (0);
}

int
lldp_maxfsz2pdu(uint16_t fsz, uint8_t *lldpdu, size_t pdusize, size_t *msglen)
{
	size_t		tlvlen;

	tlvlen = LLDP_TLVHDR_SZ + LLDP_ORGSPECHDR_SZ + sizeof (fsz);
	if (tlvlen > pdusize)
		return (ENOBUFS);
	lldp_set_orgspecid_subtype(lldpdu, LLDP_802dot3OUI_MAXFRAMESZ_SUBTYPE,
	    LLDP_802dot3_OUI, tlvlen);
	lldpdu += LLDP_TLVHDR_SZ + LLDP_ORGSPECHDR_SZ;
	*(uint16_t *)(void *)lldpdu = htons(fsz);
	*msglen += tlvlen;
	return (0);
}

/*
 *  +--------+----------------+--------------+-------------+-------------+
 *  |	TLV  | TLV information|  802.1OUI    |    802.1    |port VLAN ID |
 *  |   Type |   string len   |  00-80-C2    |   subtype   |   (PVID)    |
 *  +--------+----------------+--------------+-------------+-------------+
 *    7bits      9 bits        3 octets      1 octet         2 octets
 */
int
lldp_nvlist2pvid(nvlist_t *tlv_nvl, uint16_t *pvid)
{
	nvlist_t	*pnvl;
	int		err;

	if ((err  = lldp_get_nested_nvl(tlv_nvl, LLDP_NVP_ORGANIZATION,
	    LLDP_8021_OUI_LIST, LLDP_NVP_PVID, &pnvl)) != 0)
		return (err);
	return (nvlist_lookup_uint16(pnvl, LLDP_NVP_PVID, pvid));
}

int
lldp_tlv2pvid(lldp_tlv_t *tlv, uint16_t *pvid)
{
	uint8_t	*value = tlv->lt_value;

	/* sizeof pvid is 2 bytes */
	if (tlv->lt_len != (LLDP_ORGSPECHDR_SZ + sizeof (uint16_t)))
		return (EPROTO);

	/* Move past the OUI and subtype */
	value += LLDP_ORGSPECHDR_SZ;

	(void) memcpy(pvid, value, sizeof (uint16_t));
	*pvid = ntohs(*pvid);
	return (0);
}

int
lldp_pvid2pdu(uint16_t pvid, uint8_t *lldpdu, size_t pdusize, size_t *msglen)
{
	size_t		tlvlen;

	tlvlen = LLDP_TLVHDR_SZ + LLDP_ORGSPECHDR_SZ + sizeof (pvid);
	if (tlvlen > pdusize)
		return (ENOBUFS);
	lldp_set_orgspecid_subtype(lldpdu, LLDP_802dot1OUI_PVID_SUBTYPE,
	    LLDP_802dot1_OUI, tlvlen);
	lldpdu += LLDP_TLVHDR_SZ + LLDP_ORGSPECHDR_SZ;
	*(uint16_t *)(void *)lldpdu = htons(pvid);
	*msglen += tlvlen;
	return (0);
}

/*
 *  +--------+----------------+----------+--------+-----+----------+------+
 *  |	TLV  | TLV information|  802.1OUI|  802.1 |VLAN |VLAN name | VLAN |
 *  |   Type |   string len   |  00-80-C2| subtype| ID  | Len      | name |
 *  +--------+----------------+----------+--------+-----+----------+------+
 *	7bits      9 bits       3 octets   1 octet 2oct  1 octet    0-32 octets
 */
/* Caller frees vlan memory */
int
lldp_nvlist2vlan(nvlist_t *tlv_nvl, lldp_vlan_info_t **vinfo, int *count)
{
	nvlist_t		*vnvl;
	nvpair_t		*nvp;
	lldp_vlan_info_t	*vinfop;
	char			vlanname[LLDP_MAX_VLANNAMELEN];
	char			*vidstr;
	int			cnt = 0;

	*count = 0;
	*vinfo = NULL;
	if (lldp_get_nested_nvl(tlv_nvl, LLDP_NVP_ORGANIZATION,
	    LLDP_8021_OUI_LIST, LLDP_NVP_VLANNAME, &vnvl) != 0)
		return (ENOENT);

	for (nvp = nvlist_next_nvpair(vnvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(vnvl, nvp)) {
		cnt++;
	}
	if (cnt == 0)
		return (ENOENT);

	if ((*vinfo = calloc(cnt, sizeof (lldp_vlan_info_t))) == NULL)
		return (ENOMEM);

	*count = cnt;
	vinfop = *vinfo;
	for (nvp = nvlist_next_nvpair(vnvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(vnvl, nvp)) {
		(void) strlcpy(vlanname, nvpair_name(nvp), sizeof (vlanname));
		if ((vidstr = strrchr(vlanname, '_')) == NULL)
			continue;
		*vidstr++ = '\0';
		(void) strlcpy(vinfop->lvi_name, vlanname,
		    sizeof (vinfop->lvi_name));
		vinfop->lvi_vlen = strlen(vlanname);
		vinfop->lvi_vid = atoi(vidstr);
		vinfop++;
	}
	return (0);
}

int
lldp_tlv2vlan(lldp_tlv_t *tlv, lldp_vlan_info_t *lvip)
{
	uint8_t		*value = tlv->lt_value;

	if (tlv->lt_len < 7 || tlv->lt_len > 39)
		return (EPROTO);

	/* Move past the OUI and subtype */
	value += LLDP_ORGSPECHDR_SZ;

	bzero(lvip, sizeof (lldp_vlan_info_t));
	(void) memcpy(&lvip->lvi_vid, value, sizeof (lvip->lvi_vid));
	lvip->lvi_vid = ntohs(lvip->lvi_vid);
	value += sizeof (uint16_t);

	lvip->lvi_vlen = *value;

	if (tlv->lt_len != (LLDP_ORGSPECHDR_SZ + sizeof (lvip->lvi_vid) +
	    sizeof (lvip->lvi_vlen) + lvip->lvi_vlen)) {
		return (EPROTO);
	}

	value++;
	(void) memcpy(lvip->lvi_name, value, lvip->lvi_vlen);

	return (0);
}

int
lldp_vlan2pdu(lldp_vlan_info_t *lvi, uint8_t *lldpdu, size_t pdusize,
    size_t *msglen)
{
	size_t			tlvlen;

	tlvlen = LLDP_TLVHDR_SZ + LLDP_ORGSPECHDR_SZ +
	    sizeof (uint16_t) + sizeof (uint8_t) + lvi->lvi_vlen;
	if (tlvlen > pdusize)
		return (ENOBUFS);
	lldp_set_orgspecid_subtype(lldpdu,
	    LLDP_802dot1OUI_VLAN_NAME_SUBTYPE, LLDP_802dot1_OUI,
	    tlvlen);
	lldpdu += LLDP_TLVHDR_SZ + LLDP_ORGSPECHDR_SZ;

	*(uint16_t *)(void *)lldpdu = htons(lvi->lvi_vid);
	lldpdu += sizeof (uint16_t);
	*lldpdu = lvi->lvi_vlen;
	lldpdu += sizeof (lvi->lvi_vlen);
	bcopy(lvi->lvi_name, lldpdu, lvi->lvi_vlen);
	*msglen += tlvlen;

	return (0);
}

/*
 *  +------+------------+-----------+---------+---------+-------+------+------+
 *  | TLV  | TLV info   | OracleOUI | Orcale  | Reser- |Vlan ID |PortID |Port |
 *  | Type | string len | 00-03-BA  | subtype |   ved  |        |subtye | ID  |
 *  +------+------------+-----------+---------+--------+--------+------+------+
 *    7 bits  9 bits     3 octets     8 bits   4 octets  2octets  8bits  1-255
 *							                 octets
 */
/* Caller frees vnic memory */
int
lldp_nvlist2vnic(nvlist_t *tlv_nvl, lldp_vnic_info_t **vinfo, int *count)
{
	nvlist_t		*vnvl;
	nvlist_t		*vlnvl;
	nvpair_t		*nvp;
	int			cnt = 0;
	lldp_vnic_info_t	*vnic;
	char			*name;

	*count = 0;
	*vinfo = NULL;
	if (lldp_get_nested_nvl(tlv_nvl, LLDP_NVP_ORGANIZATION,
	    LLDP_ORACLE_OUI_LIST, LLDP_NVP_VNICNAME, &vnvl) != 0)
		return (ENOENT);

	for (nvp = nvlist_next_nvpair(vnvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(vnvl, nvp)) {
		cnt++;
	}
	if (cnt == 0)
		return (ENOENT);

	if ((*vinfo = calloc(cnt, sizeof (lldp_vnic_info_t))) == NULL)
		return (ENOMEM);

	*count = cnt;
	vnic = *vinfo;
	for (nvp = nvlist_next_nvpair(vnvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(vnvl, nvp)) {

		name = nvpair_name(nvp);
		(void) nvpair_value_nvlist(nvp, &vlnvl);
		(void) memcpy(vnic->lvni_name, name, LLDP_MAX_VLANNAMELEN);
		vnic->lvni_linkid = DATALINK_INVALID_LINKID;
		vnic->lvni_vid = VLAN_ID_NONE;
		(void) nvlist_lookup_uint16(vlnvl, LLDP_NVP_VNIC_VLANID,
		    &vnic->lvni_vid);
		(void) nvlist_lookup_uint32(vlnvl, LLDP_NVP_VNIC_LINKID,
		    &vnic->lvni_linkid);
		(void) lldp_nvlist2portid(vlnvl, &vnic->lvni_portid);
		vnic++;
	}
	return (0);
}

int
lldp_tlv2vnic(lldp_tlv_t *tlv, lldp_vnic_info_t *lvip)
{
	lldp_portid_t	*pidp;
	uint8_t		*value = tlv->lt_value;

	if (tlv->lt_len < LLDP_MIN_VNICTLV_LEN ||
	    tlv->lt_len > LLDP_MAX_VNICTLV_LEN) {
		return (EPROTO);
	}

	/* Move past the OUI and subtype */
	value += LLDP_ORGSPECHDR_SZ;

	/* Move past the reserved bits */
	value += sizeof (uint32_t);

	bzero(lvip, sizeof (lldp_vnic_info_t));
	/* Get the VLAN ID */
	(void) memcpy(&lvip->lvni_vid, value, sizeof (lvip->lvni_vid));
	lvip->lvni_vid = ntohs(lvip->lvni_vid);
	value += sizeof (lvip->lvni_vid);

	/* get the port id info */
	pidp = &lvip->lvni_portid;
	pidp->lp_subtype = *value;
	value += sizeof (pidp->lp_subtype);
	pidp->lp_pidlen = tlv->lt_len - 11;
	(void) memcpy(pidp->lp_pid, value, pidp->lp_pidlen);

	return (0);
}

int
lldp_vnic2pdu(lldp_vnic_info_t *lvi, uint8_t *lldpdu, size_t pdusize,
    size_t *msglen)
{
	lldp_portid_t		*pid;
	size_t			tlvlen;

	pid = &lvi->lvni_portid;
	tlvlen = LLDP_TLVHDR_SZ + LLDP_ORGSPECHDR_SZ + sizeof (uint32_t) +
	    sizeof (uint16_t) + sizeof (uint8_t) + pid->lp_pidlen;

	if (tlvlen > pdusize)
		return (ENOBUFS);
	lldp_set_orgspecid_subtype(lldpdu,
	    LLDP_ORACLEOUI_VNIC_SUBTYPE, LLDP_ORACLE_OUI, tlvlen);
	lldpdu += LLDP_TLVHDR_SZ + LLDP_ORGSPECHDR_SZ;

	/* Reserved bits */
	lldpdu += sizeof (uint32_t);

	/* Vlan ID */
	*(uint16_t *)(void *)lldpdu = htons(lvi->lvni_vid);
	lldpdu += sizeof (uint16_t);

	/* Port ID subtype */
	*lldpdu = pid->lp_subtype;
	lldpdu += sizeof (uint8_t);

	/* Port ID */
	bcopy(pid->lp_pid, lldpdu, pid->lp_pidlen);

	*msglen += tlvlen;
	return (0);
}

/*
 *  +--------+----------------+----------+--------+------------------------+
 *  |	TLV  | TLV information|  802.1OUI| 802.1  |Reserved| Application   |
 *  |   Type |   string len   |  00-80-C2|subtype |        | Priority Table|
 *  +--------+----------------+----------+--------+------------------------+
 *	7bits      9 bits       3-octets  1-octet  8-bits   Multiple of 3 octet
 * Caller frees memory.
 */
int
lldp_nvlist2appln(nvlist_t *tlv_nvl, lldp_appln_t **appln, uint_t *nappln)
{
	nvlist_t	*anvl;
	nvpair_t	*nvp;
	lldp_appln_t	*app;
	char		idstr[LLDP_STRSIZE], *selstr;
	int		cnt = 0;

	*nappln = 0;
	*appln = NULL;
	if (lldp_get_nested_nvl(tlv_nvl, LLDP_NVP_ORGANIZATION,
	    LLDP_8021_OUI_LIST, LLDP_NVP_APPLN, &anvl) != 0) {
		return (ENOENT);
	}

	for (nvp = nvlist_next_nvpair(anvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(anvl, nvp)) {
		cnt++;
	}
	if (cnt == 0)
		return (ENOENT);

	if ((*appln = calloc(cnt, sizeof (lldp_appln_t))) == NULL)
		return (ENOMEM);

	*nappln = cnt;
	app = *appln;
	for (nvp = nvlist_next_nvpair(anvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(anvl, nvp)) {
		(void) strlcpy(idstr, nvpair_name(nvp), sizeof (idstr));
		if ((selstr = strchr(idstr, '_')) == NULL)
			continue;
		*selstr++ = '\0';
		app->la_id = atoi(idstr);
		app->la_sel = atoi(selstr);
		(void) nvpair_value_uint8(nvp, &app->la_pri);
		app++;
	}
	return (0);
}

/*
 * Specifically get a specific application info from application TLV.
 */
int
lldp_nvlist2app(nvlist_t *nvl, uint16_t id, uint8_t sel, lldp_appln_t *ainfop)
{
	lldp_appln_t	*app;
	lldp_appln_t	*appln;
	uint_t		nappln;
	int		err = 0;
	int		count;

	if ((err = lldp_nvlist2appln(nvl, &appln, &nappln)) != 0)
		return (err);

	app = appln;
	for (count = 0; count < nappln; count++) {
		if (app->la_id == id && app->la_sel == sel) {
			bcopy(app, ainfop, sizeof (lldp_appln_t));
			free(appln);
			return (0);
		}
	}
	free(appln);
	return (-1);
}

/* Specifically get FCoE application priority */
int
lldp_nvlist2fcoepri(nvlist_t *nvl, uint8_t *pri)
{
	lldp_appln_t	appln;
	int		err = 0;

	if ((err = lldp_nvlist2app(nvl, DCBX_FCOE_APPLICATION_ID1,
	    DCBX_FCOE_APPLICATION_SF, &appln)) == 0 ||
	    (err = lldp_nvlist2app(nvl, DCBX_FCOE_APPLICATION_ID2,
	    DCBX_FCOE_APPLICATION_SF, &appln)) == 0) {
		*pri = appln.la_pri;
	}

	return (err);
}

/* Caller frees memory */
int
lldp_tlv2appln(lldp_tlv_t *tlv, lldp_appln_t **appln, uint_t *nappln)
{
	int		i, cnt;
	lldp_appln_t	*app;
	uint8_t		u8;
	uint8_t		*value = tlv->lt_value;

	/* Should have at least one application priority specified. */
	if (tlv->lt_len < (LLDP_ORGSPECHDR_SZ + sizeof (uint8_t) + 3))
		return (EPROTO);

	/* Move past the OUI and subtype */
	value += LLDP_ORGSPECHDR_SZ;

	/*
	 * Get the number of applications, 5 is the OUI, Subtype and Reserved,
	 * 3 is the size of one application priority.
	 */
	if (((tlv->lt_len - (LLDP_ORGSPECHDR_SZ + 1)) % 3) != 0)
		return (EPROTO);

	cnt = (tlv->lt_len - (LLDP_ORGSPECHDR_SZ + 1)) / 3;
	if ((*appln = calloc(cnt, sizeof (lldp_appln_t))) == NULL)
		return (ENOMEM);

	/* move past the reserved byte */
	value++;
	*nappln = cnt;
	app = *appln;
	for (i = 0; i < cnt; i++, app++) {
		u8 = *value;
		app->la_pri = (u8 & 0xE0) >> 5;
		app->la_sel = u8 & 0x3;
		value++;
		app->la_id = ntohs(*(uint16_t *)(void *)value);
		value += sizeof (app->la_id);
	}
	return (0);
}

int
lldp_appln2pdu(lldp_appln_t *appln, uint_t nappln, uint8_t *lldpdu,
    size_t pdusize, size_t *msglen)
{
	size_t		tlvlen;
	size_t		len;
	lldp_appln_t	*app;
	int		i;

	len = nappln * (3 * sizeof (uint8_t));
	tlvlen = LLDP_TLVHDR_SZ + LLDP_ORGSPECHDR_SZ + sizeof (uint8_t) + len;
	if (tlvlen > pdusize)
		return (ENOBUFS);

	lldp_set_orgspecid_subtype(lldpdu, LLDP_802dot1OUI_APPLN_SUBTYPE,
	    LLDP_802dot1_OUI, tlvlen);
	lldpdu += LLDP_TLVHDR_SZ + LLDP_ORGSPECHDR_SZ;

	/* Reserved */
	*(uint8_t *)(void *)lldpdu = 0;
	lldpdu++;

	app = appln;
	for (i = 0; i < nappln; i++) {
		*(uint8_t *)(void *)lldpdu = (app->la_pri << 5) | app->la_sel;
		lldpdu++;
		*(uint16_t *)(void *)lldpdu = htons(app->la_id);
		lldpdu += sizeof (uint16_t);
		app++;
	}
	*msglen += tlvlen;
	return (0);
}

/*
 *  +-----+---------------+----------+--------+-----+----+-----+---+------+
 *  |TLV  |TLV information|  802.1OUI|  802.1 |Will-|MBC |Reser|PFC|PFC   |
 *  |Type |  string len   |  00-80-C2| subtype|ing  |    |ved  |cap|enable|
 *  +-----+---------------+----------+--------+-----+----+-----+---+------+
 *   7-bits     9 bits       3 octets  1 octet  1b   1b    2b   4b  1-octet
 */
int
lldp_nvlist2pfc(nvlist_t *tlv_nvl, lldp_pfc_t *pfc)
{
	nvlist_t	*pnvl;
	int		err;

	if ((err  = lldp_get_nested_nvl(tlv_nvl, LLDP_NVP_ORGANIZATION,
	    LLDP_8021_OUI_LIST, LLDP_NVP_PFC, &pnvl)) != 0) {
		return (err);
	}
	if ((err = nvlist_lookup_boolean_value(pnvl, LLDP_NVP_WILLING,
	    &pfc->lp_willing)) != 0) {
		return (err);
	}
	if ((err = nvlist_lookup_boolean_value(pnvl, LLDP_NVP_PFC_MBC,
	    &pfc->lp_mbc)) != 0) {
		return (err);
	}
	if ((err = nvlist_lookup_uint8(pnvl, LLDP_NVP_PFC_CAP,
	    &pfc->lp_cap)) != 0) {
		return (err);
	}
	if ((err = nvlist_lookup_uint8(pnvl, LLDP_NVP_PFC_ENABLE,
	    &pfc->lp_enable)) != 0) {
		return (err);
	}

	return (0);
}

int
lldp_tlv2pfc(lldp_tlv_t *tlv, lldp_pfc_t *pfc)
{
	uint8_t	u8;
	uint8_t	*value = tlv->lt_value;

	/*
	 * Should have the Willing(1b), MBC(1b), Reserved (2b),  PFC Cap(4b),
	 * PFC enable (8b).
	 */
	if (tlv->lt_len != (LLDP_ORGSPECHDR_SZ + sizeof (uint16_t)))
		return (EPROTO);

	/* Move past the OUI and subtype */
	value += LLDP_ORGSPECHDR_SZ;

	u8 = *value;
	pfc->lp_willing = u8 >> 7;
	pfc->lp_mbc = (u8 >> 6) & 0x1;
	pfc->lp_cap = u8 & 0xF;
	value++;
	pfc->lp_enable = *value;

	/* Validate PFC : PFC can't be enabled for TCs not supported */
	if ((pfc->lp_enable >> pfc->lp_cap) > 0)
		return (EINVAL);

	return (0);
}

/* ARGSUSED */
int
lldp_pfc2pdu(lldp_pfc_t *pfc, uint8_t *lldpdu, size_t pdusize,
    size_t *msglen)
{
	size_t		tlvlen;
	uint8_t		val;

	tlvlen = LLDP_TLVHDR_SZ + LLDP_ORGSPECHDR_SZ + sizeof (uint16_t);
	if (tlvlen > pdusize)
		return (ENOBUFS);
	lldp_set_orgspecid_subtype(lldpdu, LLDP_802dot1OUI_PFC_SUBTYPE,
	    LLDP_802dot1_OUI, tlvlen);
	lldpdu += LLDP_TLVHDR_SZ + LLDP_ORGSPECHDR_SZ;

	val = pfc->lp_willing << 7 | pfc->lp_mbc << 6 | pfc->lp_cap;
	*(uint8_t *)(void *)lldpdu = val;
	lldpdu += sizeof (uint8_t);
	*(uint8_t *)(void *)lldpdu = pfc->lp_enable;

	*msglen += tlvlen;
	return (0);
}

/*
 * For unknown TLVs, just return the byte array as a string
 */
int
lldp_tlv2unknown(lldp_tlv_t *tlv, char *bstr, size_t blen)
{
	uint8_t	*pdu = tlv->lt_value - 2; /* Print from the TLV start */
	int	pdulen = tlv->lt_len + 2;
	char	*cp = bstr;
	int	len = 0;
	int	i;

	if (blen < tlv->lt_len + 2)
		return (ENOSPC);

	/* convert the series of bytes into numeric string */
	for (i = 0; i < pdulen; i++)
		len += snprintf(cp + len, blen - len, "%02x", pdu[i]);

	return (0);
}

/*
 *  +-----+-------+-------+-------+--------+-----------+----------+------+----+
 *  | TLV | TLV   | mgmt. | mgmt  | mgmt   | interface |interface | OID  |    |
 *  | Type| info. |address|address|address | numbering | number   |length| OID|
 *  |     |length |length |subtype|        |  subtype  |          |      |    |
 *  +-----+-------+-------+-------+--------+-----------+----------+------+----+
 *   7bits  9bits    1        1      1-31        1          4         1   0-128
 */
/* caller frees the memory */
int
lldp_nvlist2mgmtaddr(nvlist_t *nvl, const char *str, lldp_mgmtaddr_t **maddrpp,
    int *count)
{
	nvlist_t	*mnvl = NULL, *mgmtnvl = NULL;
	nvpair_t	*nvp;
	uint_t		arrsz;
	uint8_t		*arr;
	lldp_mgmtaddr_t	*maddrp;

	*maddrpp = NULL;
	*count = 0;
	if (nvlist_lookup_nvlist(nvl, LLDP_NVP_MGMTADDR, &mnvl) != 0)
		return (ENOENT);
	for (nvp = nvlist_next_nvpair(mnvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(mnvl, nvp)) {
		if (str == NULL) {
			++*count;
		} else if (strcmp(str, nvpair_name(nvp)) == 0) {
			++*count;
			break;
		}
	}
	if (*count == 0)
		return (ENOENT);
	if ((*maddrpp = calloc(*count, sizeof (lldp_mgmtaddr_t))) == NULL)
		return (ENOMEM);
	maddrp = *maddrpp;
	for (nvp = nvlist_next_nvpair(mnvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(mnvl, nvp)) {
		if (str != NULL && strcmp(str, nvpair_name(nvp)) != 0)
			continue;
		(void) nvpair_value_nvlist(nvp, &mgmtnvl);
		(void) nvlist_lookup_uint8(mgmtnvl, LLDP_NVP_MGMTADDRTYPE,
		    &maddrp->lm_subtype);
		(void) nvlist_lookup_byte_array(mgmtnvl,
		    LLDP_NVP_MGMTADDRVALUE, &arr, &arrsz);
		maddrp->lm_addrlen = arrsz;
		bcopy(arr, maddrp->lm_addr, maddrp->lm_addrlen);
		(void) nvlist_lookup_uint8(mgmtnvl, LLDP_NVP_MGMTADDR_IFTYPE,
		    &maddrp->lm_iftype);
		(void) nvlist_lookup_uint32(mgmtnvl, LLDP_NVP_MGMTADDR_IFNUM,
		    &maddrp->lm_ifnumber);
		(void) nvlist_lookup_byte_array(mgmtnvl,
		    LLDP_NVP_MGMTADDR_OIDSTR, &arr, &arrsz);
		maddrp->lm_oidlen = arrsz;
		bcopy(arr, maddrp->lm_oid, maddrp->lm_oidlen);
		maddrp++;
	}
	return (0);
}

char *
lldp_maddr_subtype2str(uint8_t type)
{
	switch (type) {
	case LLDP_MGMTADDR_TYPE_IPV4:
		return ("IPv4 Address");
	case LLDP_MGMTADDR_TYPE_IPV6:
		return ("IPv6 Address");
	case LLDP_MGMTADDR_TYPE_ALL802:
		return ("MAC address");
	}
	return ("Unknown");
}

char *
lldp_maddr_ifsubtype2str(uint8_t type)
{
	switch (type) {
	case LLDP_MGMTADDR_IFTYPE_UNKNOWN:
		return ("Unknown");
	case LLDP_MGMTADDR_IFTYPE_IFINDEX:
		return ("IfIndex");
	case LLDP_MGMTADDR_IFTYPE_SYSPORT:
		return ("System Port Number");
	}
	return ("Unknown");
}

void
lldp_mgmtaddr2str(lldp_mgmtaddr_t *map, char *buf, size_t bufsize)
{
	uint_t nbytes = 0, i;

	*buf = '\0';
	switch (map->lm_subtype) {
	case LLDP_MGMTADDR_TYPE_IPV4:
		if (inet_ntop(AF_INET, map->lm_addr, buf, bufsize) != NULL)
			return;
		break;
	case LLDP_MGMTADDR_TYPE_IPV6:
		if (inet_ntop(AF_INET6, map->lm_addr, buf, bufsize) != NULL)
			return;
		break;
	case LLDP_MGMTADDR_TYPE_ALL802:
		for (i = 0; i < map->lm_addrlen; i++) {
			nbytes += snprintf(buf + nbytes, bufsize - nbytes,
			    "%02x", map->lm_addr[i]);
		}
		return;
	}
}

int
lldp_nvlist2unknowntlv(nvlist_t *nvl, int type, lldp_unknowntlv_t **ukpp,
    uint_t *len)
{
	nvpair_t	*nvp;
	uint8_t		*arr;
	uint_t		tlvtype;
	lldp_unknowntlv_t *ukp;

	*ukpp = NULL;
	*len = 0;
	for (nvp = nvlist_next_nvpair(nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {
		tlvtype = atoi(nvpair_name(nvp));
		if (type == -1) {
			++*len;
		} else if (tlvtype == type) {
			++*len;
			break;
		}
	}
	if (*len == 0)
		return (0);
	if ((*ukpp = calloc(*len, sizeof (lldp_unknowntlv_t))) == NULL)
		return (ENOMEM);
	ukp = *ukpp;
	for (nvp = nvlist_next_nvpair(nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {
		tlvtype = atoi(nvpair_name(nvp));
		if (type != -1 && tlvtype != type)
			continue;
		ukp->lu_type = tlvtype;
		(void) nvpair_value_byte_array(nvp, &arr, &ukp->lu_len);
		bcopy(arr, ukp->lu_value, ukp->lu_len);
		ukp++;
	}
	return (0);
}

/*
 * If `str' is non-NULL, searches the `nvl' for a particular organization
 * specific tlv (i.e. tlv with a specific OUI and subtype. On the other hand
 * if `str' is NULL, it returns all the unrecognized org. specific tlv in a
 * `orgpp' of length `len'.
 */
int
lldp_nvlist2unrec_orginfo(nvlist_t *nvl, const char *str,
    lldp_unrec_orginfo_t **orgpp, uint_t *len)
{
	nvpair_t	*nvp;
	uint8_t		*arr;
	lldp_unrec_orginfo_t *orgp;

	*orgpp = NULL;
	*len = 0;
	for (nvp = nvlist_next_nvpair(nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {
		if (str == NULL) {
			++*len;
		} else if (strcmp(str, nvpair_name(nvp)) == 0) {
			++*len;
			break;
		}
	}
	if (*len == 0)
		return (0);
	if ((*orgpp = calloc(*len, sizeof (lldp_unrec_orginfo_t))) == NULL)
		return (ENOMEM);
	orgp = *orgpp;
	for (nvp = nvlist_next_nvpair(nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {
		char		*name, *index_str;

		name = nvpair_name(nvp);
		if (str != NULL && strcmp(str, name) != 0)
			continue;

		/* `arr' containes the entire 'org. specific tlv' */
		(void) nvpair_value_byte_array(nvp, &arr, &orgp->lo_len);
		bcopy(arr, orgp->lo_value, orgp->lo_len);
		/* copy over the oui */
		bcopy(arr + 2, orgp->lo_oui, LLDP_OUI_LEN);
		orgp->lo_subtype = *(arr + 5);

		index_str = strrchr(name, '_');
		if (index_str == NULL)
			continue;
		orgp->lo_index = atoi(++index_str);
		orgp++;
	}
	return (0);
}

int
lldp_del_nested_nvl(nvlist_t *invl, const char *name1, const char *name2,
    const char *name3)
{
	nvlist_t	*nvl1 = NULL;
	nvlist_t	*nvl2 = NULL;

	if (invl == NULL || name1 == NULL)
		return (EINVAL);

	if (name2 == NULL && name3 == NULL)
		return (nvlist_remove(invl, name1, DATA_TYPE_NVLIST));

	if (nvlist_lookup_nvlist(invl, name1, &nvl1) != 0)
		return (ENOENT);

	if (name3 == NULL)
		return (nvlist_remove(nvl1, name2, DATA_TYPE_NVLIST));

	if (nvlist_lookup_nvlist(nvl1, name2, &nvl2) != 0)
		return (ENOENT);

	return (nvlist_remove(nvl2, name3, DATA_TYPE_NVLIST));
}

int
lldp_get_nested_nvl(nvlist_t *invl, const char *name1, const char *name2,
    const char *name3, nvlist_t **onvl)
{
	nvlist_t	*nvl1 = NULL, *nvl2 = NULL, *nvl3 = NULL;

	if (name1 == NULL || (name2 == NULL && name3 != NULL))
		return (EINVAL);
	*onvl = NULL;
	if (nvlist_lookup_nvlist(invl, name1, &nvl1) != 0)
		return (ENOENT);
	if (name2 == NULL) {
		*onvl = nvl1;
		return (0);
	}
	if (nvlist_lookup_nvlist(nvl1, name2, &nvl2) != 0)
		return (ENOENT);
	if (name3 == NULL) {
		*onvl = nvl2;
		return (0);
	}
	if (nvlist_lookup_nvlist(nvl2, name3, &nvl3) != 0)
		return (ENOENT);
	*onvl = nvl3;
	return (0);
}

int
lldp_merge_nested_nvl(nvlist_t *dst, nvlist_t *src, const char *name1,
    const char *name2, const char *name3)
{
	nvlist_t	*mdnvl, *msnvl;
	int		err;

	err = lldp_create_nested_nvl(dst, name1, name2, name3, &mdnvl);
	if (err == 0)
		err = lldp_get_nested_nvl(src, name1, name2, name3, &msnvl);
	if (err != 0)
		return (err);

	return (nvlist_merge(mdnvl, msnvl, 0));
}

int
lldp_create_nested_nvl(nvlist_t *invl, const char *name1, const char *name2,
    const char *name3, nvlist_t **onvl)
{
	nvlist_t	*nvl1 = NULL;
	nvlist_t	*nvl2 = NULL;
	nvlist_t	*nvl3 = NULL;

	*onvl = NULL;
	if (nvlist_lookup_nvlist(invl, name1, &nvl1) != 0) {
		if (nvlist_alloc(&nvl1, NV_UNIQUE_NAME, 0) != 0)
			return (ENOMEM);
		if (nvlist_add_nvlist(invl, name1, nvl1) != 0) {
			nvlist_free(nvl1);
			return (ENOMEM);
		}
		nvlist_free(nvl1);
		(void) nvlist_lookup_nvlist(invl, name1, &nvl1);
	}
	if (name2 == NULL) {
		*onvl = nvl1;
		return (0);
	}
	/* Get the OUI list for `name2' */
	if (nvlist_lookup_nvlist(nvl1, name2, &nvl2) != 0) {
		if (nvlist_alloc(&nvl2, NV_UNIQUE_NAME, 0) != 0)
			return (ENOMEM);
		if (nvlist_add_nvlist(nvl1, name2, nvl2) != 0) {
			nvlist_free(nvl2);
			return (ENOMEM);
		}
		nvlist_free(nvl2);
		(void) nvlist_lookup_nvlist(nvl1, name2, &nvl2);
	}
	if (name3 == NULL) {
		*onvl = nvl2;
		return (0);
	}

	if (nvlist_lookup_nvlist(nvl2, name3, &nvl3) != 0) {
		if (nvlist_alloc(&nvl3, NV_UNIQUE_NAME, 0) != 0)
			return (ENOMEM);
		if (nvlist_add_nvlist(nvl2, name3, nvl3) != 0) {
			nvlist_free(nvl3);
			return (ENOMEM);
		}
		nvlist_free(nvl3);
		(void) nvlist_lookup_nvlist(nvl2, name3, &nvl3);
	}
	*onvl = nvl3;
	return (0);
}

int
lldp_nvlist2infovalid(nvlist_t *tlv_nvl, uint16_t *time)
{
	return (nvlist_lookup_uint16(tlv_nvl, LLDP_NVP_RXINFOVALID_FOR, time));
}

int
lldp_nvlist2nexttx(nvlist_t *tlv_nvl, uint16_t *time)
{
	return (nvlist_lookup_uint16(tlv_nvl, LLDP_NVP_NEXTTX_IN, time));
}

/* DCBx Appln to String functions */
char *
dcbx_appln_sel2str(int sel)
{
	switch (sel) {
	case DCB_APPLICATION_SF_ETHERTYPE:
		return ("Ethertype");
	case DCB_APPLICATION_SF_TCP_SCTP:
		return ("Port Number over TCP/SCTP");
	case DCB_APPLICATION_SF_UDP_DCCP:
		return ("Port Number over UDP/DCCP");
	case DCB_APPLICATION_SF_TCP_SCTP_UDP_DCCP:
		return ("Port Number over TCP/SCTP/UDP/DCCP");
	default:
		return ("Reserved");
	}
}
