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
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <net/if.h>
#include <resolv.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <syslog.h>
#include <gssapi/gssapi.h>
#include <kerberosv5/krb5.h>

#include "res_update.h"

#include <smbns_dyndns.h>
#include <smbns_krb.h>

/*
 * The following can be removed once head/arpa/nameser_compat.h
 * defines BADSIG, BADKEY and BADTIME.
 */
#ifndef	BADSIG
#define	BADSIG ns_r_badsig
#endif /* BADSIG */

#ifndef	BADKEY
#define	BADKEY ns_r_badkey
#endif /* BADKEY */

#ifndef	BADTIME
#define	BADTIME ns_r_badtime
#endif /* BADTIME */

/*
 * DYNDNS_TTL is the time to live in DNS caches. Note that this
 * does not affect the entry in the authoritative DNS database.
 */
#define	DYNDNS_TTL	1200

/* Microsoft AD interoperability */
#define	DYNDNS_MSAD_GSS_ALG	"gss.microsoft.com"	/* algorithm name */
#define	DYNDNS_MSAD_KEY_EXPIRE	86400			/* 24 hours */

/* lint-free alternatives to NS_PUT16, NS_PUT32 */
#define	DYNDNS_PUT16(src, dst)	(ns_put16((src), (dst)), (dst) += 2)
#define	DYNDNS_PUT32(src, dst)	(ns_put32((src), (dst)), (dst) += 4)

/* add a host integer to a network short */
#define	DYNDNS_ADD16(x, y)	((x) = htons(ntohs((x)) + (y)))

static const char * const dyndns_hex_digits = "0123456789abcdef";

/*
 * Dynamic DNS update API for kclient.
 *
 * Returns 0 upon success.  Otherwise, returns -1.
 */
int
dyndns_update(char *fqdn)
{
	int rc;

	if (smb_nic_init() != SMB_NIC_SUCCESS)
		return (-1);

	(void) smb_strlwr(fqdn);
	rc = dyndns_zone_update(fqdn);
	smb_nic_fini();
	return (rc);
}

/*
 * Convert IPv4 or IPv6 sockaddr to hostname.
 * The value is lowercase per RFC 4120 section 6.2.1.
 */
static int
dyndns_getnameinfo(union res_sockaddr_union *sockaddr,
    char *hostname, int hostlen, int flags)
{
	socklen_t socklen;
	int rc;

	if (sockaddr->sin.sin_family == AF_INET)
		socklen = sizeof (struct sockaddr_in);
	else if (sockaddr->sin6.sin6_family == AF_INET)
		socklen = sizeof (struct sockaddr_in6);
	else
		socklen = 0;
	if ((rc = (getnameinfo((struct sockaddr *)sockaddr, socklen,
	    hostname, hostlen, NULL, 0, flags))) == 0)
		(void) smb_strlwr(hostname);

	return (rc);
}

/*
 * Log a DNS error message
 */
static void
dyndns_syslog(int severity, int errnum, const char *text)
{
	struct {
		int errnum;
		char *errmsg;
	} errtab[] = {
		{ FORMERR,  "message format error" },
		{ SERVFAIL, "server internal error" },
		{ NXDOMAIN, "entry should exist but does not exist" },
		{ NOTIMP,   "not supported" },
		{ REFUSED,  "operation refused" },
		{ YXDOMAIN, "entry should not exist but does exist" },
		{ YXRRSET,  "RRSet should not exist but does exist" },
		{ NXRRSET,  "RRSet should exist but does not exist" },
		{ NOTAUTH,  "server is not authoritative for specified zone" },
		{ NOTZONE,  "name not within specified zone" },
		{ BADSIG,   "bad transaction signature (TSIG)" },
		{ BADKEY,   "bad transaction key (TKEY)" },
		{ BADTIME,  "time not synchronized" },
	};

	char *errmsg = "unknown error";
	int i;

	if (errnum == NOERROR)
		return;

	for (i = 0; i < (sizeof (errtab) / sizeof (errtab[0])); ++i) {
		if (errtab[i].errnum == errnum) {
			errmsg = errtab[i].errmsg;
			break;
		}
	}

	syslog(severity, "dyndns: %s: %s: %d", text, errmsg, errnum);
}

/*
 * Log a DNS update description message
 */
static void
dyndns_log_update(int severity,
    dyndns_update_op_t update_op, dyndns_zone_dir_t update_zone,
    const char *hostname, int af, const void *addr)
{
	const char *zone_str;
	const char *af_str;
	char addr_str[INET6_ADDRSTRLEN];

	zone_str = (update_zone == DYNDNS_ZONE_FWD) ? "forward" : "reverse";
	af_str = (af == AF_INET) ? "IPv4" : "IPv6";
	if (addr != NULL)
		if (inet_ntop(af, addr, addr_str, sizeof (addr_str)) == NULL) {
			syslog(LOG_DEBUG, "dyndns: log_update: addr ntop");
			return;
		}

	switch (update_op) {
	case DYNDNS_UPDATE_ADD:
		syslog(severity, "dyndns: update started: "
		    "add hostname %s %s address %s in %s zone",
		    hostname, af_str, addr_str, zone_str);
		break;
	case DYNDNS_UPDATE_DEL_ALL:
		if (update_zone == DYNDNS_ZONE_FWD) {
			syslog(severity, "dyndns: update started: "
			    "delete all %s addresses for hostname %s "
			    "in forward zone", af_str, hostname);
		} else if (update_zone == DYNDNS_ZONE_REV) {
			syslog(severity, "dyndns: update started: "
			    "delete all hostnames for %s address %s "
			    "in reverse zone", af_str, addr_str);
		}
		break;
	case DYNDNS_UPDATE_DEL_CLEAR:
		if (update_zone == DYNDNS_ZONE_FWD) {
			syslog(severity, "dyndns: update started: "
			    "delete hostname %s in forward zone", hostname);
		} else if (update_zone == DYNDNS_ZONE_REV) {
			syslog(severity, "dyndns: update started: "
			    "delete %s address %s in reverse zone",
			    af_str, addr_str);
		}
		break;
	case DYNDNS_UPDATE_DEL_ONE:
		syslog(severity, "dyndns: update started: "
		    "delete hostname %s %s address %s in %s zone",
		    hostname, af_str, addr_str, zone_str);
		break;
	}
}

/*
 * display_stat
 * Display GSS error message from error code.  This routine is used to display
 * the mechanism independent and mechanism specific error messages for GSS
 * routines.  The major status error code is the mechanism independent error
 * code and the minor status error code is the mechanism specific error code.
 * Parameters:
 *   maj: GSS major status
 *   min: GSS minor status
 * Returns:
 *   None
 */
static void
display_stat(OM_uint32 maj, OM_uint32 min)
{
	gss_buffer_desc msg;
	OM_uint32 msg_ctx = 0;
	OM_uint32 status, min2;

	status = gss_display_status(&min2, maj, GSS_C_GSS_CODE, GSS_C_NULL_OID,
	    &msg_ctx, &msg);

	if ((status != GSS_S_COMPLETE) && (status != GSS_S_CONTINUE_NEEDED))
		return;

	if (msg.value == NULL)
		return;

	syslog(LOG_ERR, "dyndns (secure update): GSS major status error: %s",
	    (char *)msg.value);
	(void) gss_release_buffer(&min2, &msg);

	status =  gss_display_status(&min2, min, GSS_C_MECH_CODE,
	    GSS_C_NULL_OID, &msg_ctx, &msg);


	if ((status != GSS_S_COMPLETE) && (status != GSS_S_CONTINUE_NEEDED))
		return;

	if (msg.value == NULL)
		return;

	syslog(LOG_ERR, "dyndns (secure update): GSS minor status error: %s",
	    (char *)msg.value);
	(void) gss_release_buffer(&min2, &msg);
}

/*
 * dyndns_addr_to_ptrname
 *
 * Convert a network address structure into a domain name for reverse lookup.
 * IPv4 addresses are converted to in-addr.arpa names (RFC 1035 section 3.5).
 * IPv6 addresses are converted to ip6.arpa names (RFC 3596 section 2.5).
 *
 * Parameters:
 *   af:     address family of *addr, AF_INET or AF_INET6
 *   addr:   pointer to in_addr or in6_addr address structure
 *   buf:    buffer to hold reverse domain name
 *   buf_sz: length of buffer
 * Returns:
 *   0 on error, or length of domain name written to buf
 */
static size_t
dyndns_addr_to_ptrname(int af, const void *addr, char *buf, size_t buf_sz)
{
	union {
		const struct in_addr *in;
		const struct in6_addr *in6;
	} u;
	size_t len;
	uint8_t a, b, c, d;
	int i;

	switch (af) {
	case AF_INET:
		u.in = addr;
		a = (ntohl(u.in->s_addr) & 0xff000000) >> 24;
		b = (ntohl(u.in->s_addr) & 0x00ff0000) >> 16;
		c = (ntohl(u.in->s_addr) & 0x0000ff00) >> 8;
		d = (ntohl(u.in->s_addr) & 0x000000ff);
		len = snprintf(NULL, 0, "%d.%d.%d.%d.in-addr.arpa",
		    d, c, b, a);
		if (len >= buf_sz)
			return (0);
		(void) snprintf(buf, buf_sz, "%d.%d.%d.%d.in-addr.arpa",
		    d, c, b, a);
		break;
	case AF_INET6:
		u.in6 = addr;
		/*
		 * The IPv6 reverse lookup domain name format has
		 * 4 chars for each address byte, followed by "ip6.arpa".
		 */
		len = (4 * IPV6_ADDR_LEN) + (sizeof ("ip6.arpa") - 1);
		if (len >= buf_sz)
			return (0);
		for (i = (IPV6_ADDR_LEN - 1); i >= 0; i--) {
			*buf++ = dyndns_hex_digits[u.in6->s6_addr[i] & 0x0f];
			*buf++ = '.';
			*buf++ = dyndns_hex_digits[u.in6->s6_addr[i] >> 4];
			*buf++ = '.';
		}
		(void) strlcpy(buf, "ip6.arpa", sizeof ("ip6.arpa"));
		break;
	default:
		return (0);
	}

	return (len);
}

/*
 * dyndns_get_update_info
 *
 * Given a valid resolver context, update direction, and a host name and
 * address pair, return the information needed to perform a DNS update.
 * DNS zone and SOA nameservers are found by searching current DNS data.
 *
 * Example:
 *   dyndns_get_update_info(statp, DYNDNS_ZONE_FWD, type,
 *       "host.org.example.com", "10.0.0.1", ...) might return:
 *       type:     ns_t_a
 *       zone_buf: "example.com"
 *       name_buf: "host.org.example.com"
 *       data_buf: "10.0.0.1"
 *   dyndns_get_update_info(statp, DYNDNS_ZONE_REV, type,
 *       "host.org.example.com", "10.0.0.1", ...) might return:
 *       type:     ns_t_ptr
 *       zone_buf: "10.in-addr.arpa"
 *       name_buf: "1.0.0.10.in-addr.arpa"
 *       data_buf: "host.org.example.com"
 *
 * Parameters:
 *   statp:       resolver state
 *   update_zone: one of DYNDNS_ZONE_FWD or DYNDNS_ZONE_REV
 *   hostname:    host name, as a string
 *   af:          address family of *addr, AF_INET or AF_INET6
 *   addr:        pointer to in_addr or in6_addr address structure
 *   type:        type of DNS record to update
 *   zone_buf:    buffer to receive zone owning name
 *   zone_sz:     size of zone_buf buffer
 *   name_buf:    buffer to receive name to update
 *   name_sz:     size of name_buf buffer
 *   data_buf:    buffer to receive data to update for name
 *   data_sz:     size of data_buf buffer
 *   ns:          buffer to receive nameservers found for zone
 *   ns_cnt:      number of nameservers that fit in ns buffer
 * Returns:
 *   -1 on error, or number of nameservers returned in ns
 */
static int
dyndns_get_update_info(res_state statp, dyndns_zone_dir_t update_zone,
    const char *hostname, int af, const void *addr, ns_type *type,
    char *zone_buf, size_t zone_sz,
    char *name_buf, size_t name_sz,
    char *data_buf, size_t data_sz,
    union res_sockaddr_union *ns, int ns_cnt)
{
	switch (update_zone) {
	case DYNDNS_ZONE_FWD:
		switch (af) {
		case AF_INET:
			*type = ns_t_a;
			break;
		case AF_INET6:
			*type = ns_t_aaaa;
			break;
		default:
			return (-1);
		}
		if (strlcpy(name_buf, hostname, name_sz) >= name_sz)
			return (-1);
		if (addr != NULL)
			if (inet_ntop(af, addr, data_buf, data_sz) == NULL)
				return (-1);
		break;
	case DYNDNS_ZONE_REV:
		if (!(af == AF_INET || af == AF_INET6))
			return (-1);
		*type = ns_t_ptr;
		if (dyndns_addr_to_ptrname(af, addr, name_buf, name_sz) == 0)
			return (-1);
		if (hostname != NULL)
			if (strlcpy(data_buf, hostname, data_sz) >= data_sz)
				return (-1);
		break;
	default:
		return (-1);
	}

	return (res_findzonecut2(statp, name_buf, ns_c_in, RES_EXHAUSTIVE,
	    zone_buf, zone_sz, ns, ns_cnt));
}

/*
 * dyndns_build_tkey
 *
 * Build a TKEY RDATA section according to RFC 2930 section 2.
 *
 * May be called with buf == NULL and buf_sz == 0, in which case the
 * function does not actually build the TKEY RDATA, but only returns the
 * space needed for TKEY RDATA as specified by the tkey parameter.
 *
 * Parameters:
 *   buf:        buffer to store TKEY RDATA
 *   buf_sz:     buffer length
 *   tkey:       pointer to DNS TKEY RDATA structure
 * Returns:
 *   -1 on error, or length of data written (or that would be written) to buf
 */
static ssize_t
dyndns_build_tkey(uchar_t *buf, size_t buf_sz, const dyndns_tkey_rdata_t *tkey)
{
	ns_nname alg_buf;
	size_t alg_len;
	size_t tkey_len;

	if (tkey == NULL) {
		syslog(LOG_DEBUG, "dyndns: build_tkey: tkey == NULL");
		return (-1);
	}
	if (tkey->tk_alg_name == NULL) {
		syslog(LOG_DEBUG, "dyndns: build_tkey: tk_alg_name == NULL");
		return (-1);
	}

	if (ns_name_pton(tkey->tk_alg_name, alg_buf, sizeof (alg_buf)) == -1) {
		syslog(LOG_DEBUG, "dyndns: build_tkey: tk_alg_name pton");
		return (-1);
	}
	alg_len = ns_name_length(alg_buf, sizeof (alg_buf));

	tkey_len = alg_len + tkey->tk_key_size + tkey->tk_other_size + 16;

	if ((buf == NULL) && (buf_sz == 0))
		return (tkey_len);

	if (tkey_len > buf_sz) {
		syslog(LOG_DEBUG, "dyndns: build_tkey: buf too small");
		return (-1);
	}

	if (tkey->tk_key_size > 0 && tkey->tk_key_data == NULL) {
		syslog(LOG_DEBUG, "dyndns: build_tkey: "
		    "tk_key_size > 0 && tk_key_data == NULL");
		return (-1);
	}
	if (tkey->tk_other_size > 0 && tkey->tk_other_data == NULL) {
		syslog(LOG_DEBUG, "dyndns: build_tkey: "
		    "tk_other_size > 0 && tk_other_data == NULL");
		return (-1);
	}

	(void) memcpy(buf, alg_buf, alg_len);
	buf += alg_len;
	DYNDNS_PUT32(tkey->tk_incept_time, buf);
	DYNDNS_PUT32(tkey->tk_expire_time, buf);
	DYNDNS_PUT16(tkey->tk_mode, buf);
	DYNDNS_PUT16(tkey->tk_error, buf);
	DYNDNS_PUT16(tkey->tk_key_size, buf);
	if (tkey->tk_key_size > 0) {
		(void) memcpy(buf, tkey->tk_key_data, tkey->tk_key_size);
		buf += tkey->tk_key_size;
	}
	DYNDNS_PUT16(tkey->tk_other_size, buf);
	if (tkey->tk_other_size > 0)
		(void) memcpy(buf, tkey->tk_other_data, tkey->tk_other_size);

	return (tkey_len);
}

/*
 * dyndns_build_tsig
 *
 * Build a complete TSIG RR section according to RFC 2845 section 2.3,
 * including either all fields, or only those fields used in computation
 * of the message signature data.
 *
 * Because the TSIG RR is added to an existing DNS message for message
 * digest computation, this function takes as parameters a buffer
 * containing an existing message and its length.  The TSIG RR is
 * appended to the existing message in the same buffer.
 *
 * May be called with buf == NULL and buf_sz == 0, in which case the
 * function does not actually build the TSIG RR, but only returns the
 * space needed for a TSIG RR as specified by the other parameters.
 *
 * Parameters:
 *   buf:         buffer to store TSIG RR
 *   buf_sz:      buffer length
 *   msg_len:     length of existing message in buffer
 *   key_name:    key name, in C string format
 *   tsig:        pointer to DNS TSIG RDATA structure
 *   digest_data: one of DYNDNS_DIGEST_SIGNED or DYNDNS_DIGEST_UNSIGNED
 * Returns:
 *   -1 on error, or length of data written (or that would be written) to buf
 */
static ssize_t
dyndns_build_tsig(uchar_t *buf, size_t buf_sz, size_t msg_len,
    const char *key_name, const dyndns_tsig_rdata_t *tsig,
    dyndns_digest_data_t digest_data)
{
	ns_nname key_buf;
	size_t key_len;
	ns_nname alg_buf;
	size_t alg_len;
	size_t tsig_rrlen, tsig_rdlen;
	uint32_t sign, fudge;

	if (tsig == NULL) {
		syslog(LOG_DEBUG, "dyndns: build_tsig: tsig == NULL");
		return (-1);
	}
	if (tsig->ts_alg_name == NULL) {
		syslog(LOG_DEBUG, "dyndns: build_tsig: ts_alg_name == NULL");
		return (-1);
	}

	if (ns_name_pton(key_name, key_buf, sizeof (key_buf)) == -1) {
		syslog(LOG_DEBUG, "dyndns: build_tsig: key_name pton");
		return (-1);
	}
	key_len = ns_name_length(key_buf, sizeof (key_buf));

	if (ns_name_pton(tsig->ts_alg_name, alg_buf, sizeof (alg_buf)) == -1) {
		syslog(LOG_DEBUG, "dyndns: build_tsig: ts_alg_name pton");
		return (-1);
	}
	alg_len = ns_name_length(alg_buf, sizeof (alg_buf));

	switch (digest_data) {
	case DYNDNS_DIGEST_SIGNED:
		tsig_rrlen = key_len + NS_RRFIXEDSZ;
		tsig_rdlen = alg_len + tsig->ts_other_size +
		    tsig->ts_mac_size + 16;
		break;
	case DYNDNS_DIGEST_UNSIGNED:
		tsig_rrlen = key_len + NS_RRFIXEDSZ - 4;
		tsig_rdlen = alg_len + tsig->ts_other_size + 12;
		break;
	default:
		syslog(LOG_DEBUG, "dyndns: build_tsig: bad digest_data");
		return (-1);
	}

	if ((buf == NULL) && (buf_sz == 0))
		return (tsig_rrlen + tsig_rdlen);

	if (msg_len + tsig_rrlen + tsig_rdlen > buf_sz) {
		syslog(LOG_DEBUG, "dyndns: build_tsig: buf too small");
		return (-1);
	}

	if (tsig->ts_mac_size > 0 && tsig->ts_mac_data == NULL) {
		syslog(LOG_DEBUG, "dyndns: build_tsig: "
		    "ts_mac_size > 0 && ts_mac_data == NULL");
		return (-1);
	}
	if (tsig->ts_other_size > 0 && tsig->ts_other_data == NULL) {
		syslog(LOG_DEBUG, "dyndns: build_tsig: "
		    "ts_other_size > 0 && ts_other_data == NULL");
		return (-1);
	}

	sign = tsig->ts_sign_time >> 16;
	fudge = (tsig->ts_sign_time << 16) | tsig->ts_fudge_time;

	buf += msg_len;

	/*
	 * TSIG RR fields:
	 * name [type] class ttl [rdlen]
	 */
	(void) memcpy(buf, key_buf, key_len);
	buf += key_len;
	if (digest_data == DYNDNS_DIGEST_SIGNED)
		DYNDNS_PUT16(ns_t_tsig, buf);
	DYNDNS_PUT16(ns_c_any, buf);
	DYNDNS_PUT32(0, buf);
	if (digest_data == DYNDNS_DIGEST_SIGNED)
		DYNDNS_PUT16(tsig_rdlen, buf);

	/*
	 * TSIG RDATA fields:
	 * alg_name sign_time fudge_time [mac_size] [mac_data] [orig_id] error
	 * other_size other_data
	 */
	(void) memcpy(buf, alg_buf, alg_len);
	buf += alg_len;
	DYNDNS_PUT32(sign, buf);
	DYNDNS_PUT32(fudge, buf);
	if (digest_data == DYNDNS_DIGEST_SIGNED) {
		DYNDNS_PUT16(tsig->ts_mac_size, buf);
		if (tsig->ts_mac_size > 0) {
			(void) memcpy(buf, tsig->ts_mac_data,
			    tsig->ts_mac_size);
			buf += tsig->ts_mac_size;
		}
		DYNDNS_PUT16(tsig->ts_orig_id, buf);
	}
	DYNDNS_PUT16(tsig->ts_error, buf);
	DYNDNS_PUT16(tsig->ts_other_size, buf);
	if (tsig->ts_other_size > 0)
		(void) memcpy(buf, tsig->ts_other_data, tsig->ts_other_size);

	return (tsig_rrlen + tsig_rdlen);
}

/*
 * dyndns_build_tkey_msg
 *
 * This routine is used to build the TKEY message to transmit GSS tokens
 * during GSS security context establishment for secure DNS update.  The
 * TKEY message format uses the DNS query message format.  The TKEY section
 * is the answer section of the query message format.
 *
 * Parameters:
 *   statp:       resolver state
 *   buf:         buffer to store TKEY message
 *   buf_sz:      size of buffer
 *   key_name:    TKEY key name
 *                (this same key name must be also be used in the TSIG message)
 *   tkey:        pointer to DNS TKEY RDATA structure
 * Returns:
 *   -1 on error, or length of message written to buf
 */
static ssize_t
dyndns_build_tkey_msg(res_state statp, uchar_t *buf, int buf_sz,
    const char *key_name, const dyndns_tkey_rdata_t *tkey)
{
	ssize_t buf_len;
	uchar_t *tkey_buf;
	size_t tkey_sz;
	ns_newmsg newmsg;
	ns_nname key_nname;

	tkey_sz = dyndns_build_tkey(NULL, 0, tkey);
	if ((tkey_buf = malloc(tkey_sz)) == NULL) {
		syslog(LOG_ERR, "dyndns: %m");
		return (-1);
	}
	if (dyndns_build_tkey(tkey_buf, tkey_sz, tkey) == -1) {
		free(tkey_buf);
		return (-1);
	}

	if (ns_newmsg_init(buf, buf_sz, &newmsg) != 0) {
		syslog(LOG_DEBUG, "dyndns: build_tkey_msg: ns_newmsg_init");
		free(tkey_buf);
		return (-1);
	}
	if (ns_name_pton(key_name, key_nname, sizeof (key_nname)) < 0) {
		syslog(LOG_DEBUG, "dyndns: build_tkey_msg: key_name pton");
		free(tkey_buf);
		return (-1);
	}
	ns_newmsg_id(&newmsg, statp->id = res_nrandomid(statp));
	ns_newmsg_flag(&newmsg, ns_f_opcode, ns_o_query);
	if (ns_newmsg_q(&newmsg, key_nname, ns_t_tkey, ns_c_in) != 0) {
		syslog(LOG_DEBUG, "dyndns: build_tkey_msg: ns_newmsg_q");
		free(tkey_buf);
		return (-1);
	}
	if (ns_newmsg_rr(&newmsg, ns_s_an, key_nname,
	    ns_t_tkey, ns_c_any, 0, tkey_sz, tkey_buf) != 0) {
		syslog(LOG_DEBUG, "dyndns: build_tkey_msg: ns_newmsg_rr");
		free(tkey_buf);
		return (-1);
	}
	buf_len = ns_newmsg_done(&newmsg);

	free(tkey_buf);

	return (buf_len);
}

/*
 * Construct a service name as follows:
 *
 * <svctype>/<fqhostname>@<realm>
 *
 * and convert it to GSS-API internal format.
 */
static int
dyndns_getkrb5svcname(const char *svctype, const char *fqhostname,
    const char *realm, gss_name_t *output_name)
{
	gss_buffer_desc svcbuf;
	OM_uint32 minor, major;
	char *name = NULL;
	int num_bytes;

	if ((num_bytes = asprintf(&name, "%s/%s@%s", svctype,
	    fqhostname, realm)) == -1)
		return (-1);

	svcbuf.length = num_bytes;
	svcbuf.value = name;

	if ((major = gss_import_name(&minor, &svcbuf,
	    GSS_C_NO_OID, output_name)) != GSS_S_COMPLETE) {
		display_stat(major, minor);
		(void) free(svcbuf.value);
		return (-1);
	}

	(void) free(svcbuf.value);
	return (0);
}

/*
 * dyndns_establish_sec_ctx
 *
 * This routine is used to establish a security context with the DNS server
 * by building TKEY messages and sending them to the DNS server.  TKEY messages
 * are also received from the DNS server for processing.   The security context
 * establishment is done with the GSS client on the system producing a token
 * and sending the token within the TKEY message to the GSS server on the DNS
 * server.  The GSS server then processes the token and then send a TKEY reply
 * message with a new token to be processed by the GSS client.  The GSS client
 * processes the new token and then generates a new token to be sent to the
 * GSS server.  This cycle is continued until the security establishment is
 * done.
 *
 * See RFC 3645 section 3 for standards information.
 *
 * Parameters:
 *   statp:      : resolver state
 *   gss_context : handle to security context
 *   cred_handle : handle to credential
 *   target      : server principal
 *   key_name    : TKEY key name
 *                 (this same key name must be also be used in the TSIG message)
 * Returns:
 *   gss_context : handle to security context
 *   -1 on failure, 0 on success
 */
static int
dyndns_establish_sec_ctx(res_state statp,
    gss_ctx_id_t *gss_context, gss_cred_id_t cred_handle, gss_name_t target,
    const char *key_name)
{
	int buf_sz = NS_MAXMSG;
	int rbuf_sz = NS_MAXMSG;
	uchar_t *buf, *rbuf;
	int buf_len, rbuf_len;
	int rcode;
	OM_uint32 min, maj;
	gss_buffer_desc in_tok, out_tok;
	gss_buffer_desc *inputptr;
	int gss_flags;
	OM_uint32 ret_flags;
	ns_msg msg;
	ns_rr rr;
	dyndns_tkey_rdata_t tkey;
	struct timeval tv;
	int tkey_alg_len;

	if ((buf = malloc(buf_sz)) == NULL) {
		syslog(LOG_ERR, "dyndns: %m");
		return (-1);
	}
	if ((rbuf = malloc(rbuf_sz)) == NULL) {
		syslog(LOG_ERR, "dyndns: %m");
		free(buf);
		return (-1);
	}

	tkey.tk_alg_name = DYNDNS_MSAD_GSS_ALG;
	tkey.tk_mode = DYNDNS_TKEY_MODE_GSS;
	tkey.tk_error = ns_r_noerror;
	tkey.tk_other_size = 0;
	tkey.tk_other_data = NULL;

	inputptr = GSS_C_NO_BUFFER;
	*gss_context = GSS_C_NO_CONTEXT;
	gss_flags = GSS_C_MUTUAL_FLAG | GSS_C_DELEG_FLAG | GSS_C_REPLAY_FLAG |
	    GSS_C_SEQUENCE_FLAG | GSS_C_CONF_FLAG | GSS_C_INTEG_FLAG;
	do {
		(void) gettimeofday(&tv, 0);
		maj = gss_init_sec_context(&min, cred_handle, gss_context,
		    target, GSS_C_NO_OID, gss_flags, 0, NULL, inputptr, NULL,
		    &out_tok, &ret_flags, NULL);

		if (maj != GSS_S_COMPLETE && maj != GSS_S_CONTINUE_NEEDED) {
			display_stat(maj, min);
			free(buf);
			free(rbuf);
			return (-1);
		}

		if ((maj == GSS_S_COMPLETE) &&
		    !(ret_flags & GSS_C_REPLAY_FLAG)) {
			syslog(LOG_ERR, "dyndns: secure update error: "
			    "no replay attack detection available");
			if (out_tok.length > 0)
				(void) gss_release_buffer(&min, &out_tok);
			free(buf);
			free(rbuf);
			return (-1);
		}

		if ((maj == GSS_S_COMPLETE) &&
		    !(ret_flags & GSS_C_MUTUAL_FLAG)) {
			syslog(LOG_ERR, "dyndns: secure update error: "
			    "remote peer did not authenticate itself");
			if (out_tok.length > 0)
				(void) gss_release_buffer(&min, &out_tok);
			free(buf);
			free(rbuf);
			return (-1);
		}

		if (out_tok.length > 0) {
			tkey.tk_incept_time = tv.tv_sec;
			tkey.tk_expire_time = tv.tv_sec +
			    DYNDNS_MSAD_KEY_EXPIRE;
			tkey.tk_key_size = out_tok.length;
			tkey.tk_key_data = out_tok.value;
			if ((buf_len = dyndns_build_tkey_msg(statp,
			    buf, buf_sz, key_name, &tkey)) <= 0) {
				syslog(LOG_ERR, "dyndns: secure update error: "
				    "could not build key exchange message");
				(void) gss_release_buffer(&min, &out_tok);
				free(buf);
				free(rbuf);
				return (-1);
			}

			(void) gss_release_buffer(&min, &out_tok);

			if ((rbuf_len = res_nsend(statp, buf, buf_len,
			    rbuf, rbuf_sz)) <= 0) {
				syslog(LOG_ERR, "dyndns: secure update error: "
				    "could not send key exchange message");
				free(buf);
				free(rbuf);
				return (-1);
			}

			if (ns_initparse(rbuf, rbuf_len, &msg) != 0) {
				syslog(LOG_ERR, "dyndns: secure update error: "
				    "could not parse key exchange message");
				free(buf);
				free(rbuf);
				return (-1);
			}

			rcode = ns_msg_getflag(msg, ns_f_rcode);

			if (rcode != NOERROR) {
				dyndns_syslog(LOG_ERR, rcode,
				    "secure update error: key exchange error");
				free(buf);
				free(rbuf);
				return (-1);
			}

			if (ns_parserr(&msg, ns_s_an, 0, &rr) != 0) {
				syslog(LOG_ERR, "dyndns: secure update error: "
				    "could not parse peer key data");
				free(buf);
				free(rbuf);
				return (-1);
			}

			/* Extract token length and value from TKEY RDATA */
			tkey_alg_len =
			    ns_name_length(&rr.rdata[0], rr.rdlength);
			in_tok.length = ns_get16(&rr.rdata[tkey_alg_len +
			    DYNDNS_TKEY_OFFSET_KEYSIZE]);
			in_tok.value = (void *)&rr.rdata[tkey_alg_len +
			    DYNDNS_TKEY_OFFSET_KEYDATA];

			inputptr = &in_tok;
		}

	} while (maj != GSS_S_COMPLETE);

	free(buf);
	free(rbuf);

	return (0);
}

/*
 * dyndns_get_sec_context
 *
 * Get security context for secure dynamic DNS update.  This routine opens
 * a socket to the DNS server and establishes a security context with
 * the DNS server using host principal to perform secure dynamic DNS update.
 *
 * Parameters:
 *   statp:      resolver state
 *   hostname:   fully qualified hostname
 *   dns_srv_ip: ip address of hostname in network byte order
 * Returns:
 *   0 on failure, or gss credential context
 */
static gss_ctx_id_t
dyndns_get_sec_context(res_state statp,
    const char *hostname, union res_sockaddr_union *dns_srv_ip)
{
	gss_name_t	initiator = GSS_C_NO_NAME, target = GSS_C_NO_NAME;
	gss_cred_id_t	cred_handle = GSS_C_NO_CREDENTIAL;
	gss_ctx_id_t	gss_context = GSS_C_NO_CONTEXT;
	OM_uint32	minor, major;
	char		dns_srv_hostname[MAXHOSTNAMELEN];
	char		*host_spn, *realm;
	char		ad_domain[MAXHOSTNAMELEN];
	const char	*key_name = hostname;

	if (dyndns_getnameinfo(dns_srv_ip, dns_srv_hostname,
	    sizeof (dns_srv_hostname), 0))
		return (NULL);

	(void) smb_getdomainname_ad(ad_domain, sizeof (ad_domain));
	if ((realm = smb_krb5_domain2realm(ad_domain)) == NULL)
		return (NULL);

	if (dyndns_getkrb5svcname(SMB_PN_SVC_HOST, hostname, realm,
	    &initiator) != 0)
		goto cleanup;

	if (dyndns_getkrb5svcname(SMB_PN_SVC_DNS, dns_srv_hostname, realm,
	    &target) != 0)
		goto cleanup;

	if ((host_spn = smb_krb5_get_pn_by_id(SMB_KRB5_PN_ID_HOST_FQHN,
	    SMB_PN_KEYTAB_ENTRY, ad_domain)) == NULL)
		goto cleanup;

	if (smb_kinit(host_spn, NULL) != 0)
		goto cleanup;

	major = gss_acquire_cred(&minor, initiator, 0, GSS_C_NULL_OID_SET,
	    GSS_C_INITIATE, &cred_handle, NULL, NULL);

	if (GSS_ERROR(major)) {
		display_stat(major, minor);
		goto cleanup;
	}

	if (dyndns_establish_sec_ctx(statp, &gss_context, cred_handle, target,
	    key_name) != 0) {
		if (gss_context != GSS_C_NO_CONTEXT)
			(void) gss_delete_sec_context(&minor, &gss_context,
			    NULL);
		gss_context = NULL;
	}

cleanup:
	free(realm);
	free(host_spn);
	(void) gss_release_name(&minor, &initiator);
	(void) gss_release_name(&minor, &target);
	(void) gss_release_cred(&minor, &cred_handle);
	return (gss_context);
}

/*
 * dyndns_build_update_msg
 *
 * This routine builds the update request message for adding and removing DNS
 * entries which is used for non-secure and secure DNS update.
 *
 * Parameters:
 *   statp:       resolver state
 *   buf:         buffer to build message
 *   buf_sz:      buffer size
 *   zone:        zone owning name
 *   name:        name to update
 *   type:        type of data to update for name
 *   data:        value of data to update for name
 *   ttl:         time to live of new DNS RR
 *                (if update_op == DYNDNS_UPDATE_ADD)
 *   update_op:   one of:
 *                  DYNDNS_UPDATE_ADD       adds entries
 *                  DYNDNS_UPDATE_DEL_ALL   deletes entries of one name & type.
 *                  DYNDNS_UPDATE_DEL_CLEAR deletes entries of one name.
 *                  DYNDNS_UPDATE_DEL_ONE   deletes one entry.
 *
 * Returns:
 *   -1 on error, or length of message written to buf
 */
static ssize_t
dyndns_build_update_msg(res_state statp, uchar_t *buf, size_t buf_sz,
    const char *zone, const char *name, ns_type type, const char *data,
    uint32_t ttl, dyndns_update_op_t update_op)
{
	ns_updque updque;
	ns_updrec *updrec_zn, *updrec_ud;
	ssize_t msg_len;

	if ((updrec_zn = res_mkupdrec(ns_s_zn, zone,
	    ns_c_in, ns_t_soa, 0)) == NULL)
		return (-1);

	if ((updrec_ud = res_mkupdrec(ns_s_ud, name,
	    ns_c_invalid, ns_t_invalid, 0)) == NULL)
		return (-1);

	switch (update_op) {
	case DYNDNS_UPDATE_ADD:
		/* Add to an RRset */
		updrec_ud->r_opcode = ns_uop_add;
		updrec_ud->r_class = ns_c_in;
		updrec_ud->r_type = type;
		updrec_ud->r_ttl = ttl;
		updrec_ud->r_data = (uchar_t *)data;
		updrec_ud->r_size = strlen(data) + 1;
		break;
	case DYNDNS_UPDATE_DEL_ALL:
		/* Delete an RRset */
		updrec_ud->r_opcode = ns_uop_delete;
		updrec_ud->r_class = ns_c_any;
		updrec_ud->r_type = type;
		updrec_ud->r_ttl = 0;
		updrec_ud->r_data = NULL;
		updrec_ud->r_size = 0;
		break;
	case DYNDNS_UPDATE_DEL_CLEAR:
		/* Delete all RRsets from a name */
		updrec_ud->r_opcode = ns_uop_delete;
		updrec_ud->r_class = ns_c_any;
		updrec_ud->r_type = ns_t_any;
		updrec_ud->r_ttl = 0;
		updrec_ud->r_data = NULL;
		updrec_ud->r_size = 0;
		break;
	case DYNDNS_UPDATE_DEL_ONE:
		/* Delete an RR from an RRset */
		updrec_ud->r_opcode = ns_uop_delete;
		updrec_ud->r_class = ns_c_none;
		updrec_ud->r_type = type;
		updrec_ud->r_ttl = 0;
		updrec_ud->r_data = (uchar_t *)data;
		updrec_ud->r_size = strlen(data) + 1;
		break;
	default:
		return (-1);
	}

	/*LINTED*/
	INIT_LIST(updque);
	/*LINTED*/
	APPEND(updque, updrec_zn, r_glink);
	/*LINTED*/
	APPEND(updque, updrec_ud, r_glink);

	if ((msg_len = res_nmkupdate(statp, HEAD(updque), buf, buf_sz)) < 0)
		return (-1);

	res_freeupdrec(updrec_zn);
	res_freeupdrec(updrec_ud);

	return (msg_len);
}

/*
 * dyndns_search_entry
 * Query DNS server for entry.  This routine can indicate if an entry exist
 * or not during forward or reverse lookup.  Also can indicate if the data
 * of the entry matched.  For example, for forward lookup, the entry is
 * searched using the hostname and the data is the IP address.  For reverse
 * lookup, the entry is searched using the IP address and the data is the
 * hostname.
 * Parameters:
 *   update_zone: one of DYNDNS_ZONE_FWD or DYNDNS_ZONE_REV
 *   hostname   : fully qualified hostname
 *   family     : address family of *addr, AF_INET or AF_INET6
 *   addr       : address of hostname in network format
 * Returns:
 *   is_match: is 1 for found matching entry, otherwise 0
 *   1       : an entry exist but not necessarily match
 *   0       : an entry does not exist
 *   -1      : an error occurred
 */
static int
dyndns_search_entry(dyndns_zone_dir_t update_zone,
    const char *hostname, int family, const void *addr, int *is_match)
{
	union res_sockaddr_union ipaddr, dnsip;
	char dns_srv_hostname[NI_MAXHOST];
	struct addrinfo hints, *res = NULL;
	int salen;

	*is_match = 0;
	switch (family) {
	case AF_INET:
		salen = sizeof (ipaddr.sin);
		(void) memcpy(&ipaddr.sin, addr, salen);
		break;
	case AF_INET6:
		salen = sizeof (ipaddr.sin6);
		(void) memcpy(&ipaddr.sin6, addr, salen);
		break;
	default:
		return (-1);
	}
	if (update_zone == DYNDNS_ZONE_FWD) {
		bzero((char *)&hints, sizeof (hints));
		hints.ai_family = family;
		hints.ai_flags = AI_NUMERICHOST;
		if (getaddrinfo(hostname, NULL, &hints, &res)) {
			return (NULL);
		}
		if (res) {
			/*
			 * if both ips aren't the same family skip to
			 * the next record
			 */
			do {
				if ((res->ai_family == AF_INET) &&
				    (family == AF_INET)) {
					(void) memcpy(&dnsip.sin,
					    &res->ai_addr[0], salen);
					if (ipaddr.sin.sin_addr.s_addr ==
					    dnsip.sin.sin_addr.s_addr) {
						*is_match = 1;
						break;
					}
				} else if ((res->ai_family == AF_INET6) &&
				    (family == AF_INET6)) {
					(void) memcpy(&dnsip.sin6,
					    &res->ai_addr[0], salen);
					/* need compare macro here */
					if (!memcmp(&ipaddr.sin6.sin6_addr,
					    &dnsip.sin6.sin6_addr, IN6ADDRSZ)) {
						*is_match = 1;
						break;
					}
				}
			} while (res->ai_next);
			freeaddrinfo(res);
			return (1);
		}
	} else {
		if (dyndns_getnameinfo(&ipaddr, dns_srv_hostname, NI_MAXHOST,
		    0))
			return (NULL);

		if (strncasecmp(dns_srv_hostname, hostname,
		    strlen(hostname)) == 0) {
			*is_match = 1;
		}
		return (1);
	}

	/* entry does not exist */
	return (0);
}

/*
 * dyndns_update_nameaddr
 *
 * Perform non-secure dynamic DNS update.  On error, perform secure update
 * on behalf of the localhost's domain machine account if the localhost is a
 * domain member.
 *
 * Secure dynamic DNS update using host credentials requires the system to
 * be configured as a Kerberos client. That configuration is done as part of the
 * AD domain join.
 *
 * This routine creates a new resolver context, builds the update request
 * message, and sends the message to the DNS server.  The response is received
 * and check for error.  If there is no error then the local NSS cached is
 * purged.  DNS may be used to check to see if an entry already exist before
 * adding or to see if an entry does exist before removing it.  Adding
 * duplicate entries or removing non-existing entries does not cause any
 * problems.  DNS is not check when doing a delete all.
 *
 * Parameters:
 *   update_op:   one of:
 *                  DYNDNS_UPDATE_ADD       adds entries
 *                  DYNDNS_UPDATE_DEL_ALL   deletes entries of one name & type.
 *                  DYNDNS_UPDATE_DEL_CLEAR deletes entries of one name.
 *                  DYNDNS_UPDATE_DEL_ONE   deletes one entry.
 *   update_zone: one of DYNDNS_ZONE_FWD or DYNDNS_ZONE_REV
 *   hostname   : fully qualified hostname
 *   af         : address family of *addr, AF_INET or AF_INET6
 *   addr       : address of hostname in network format
 *   ttl        : cached time of this entry by others and not within DNS
 *                database
 *   do_check   : one of:
 *                  DYNDNS_CHECK_NONE for no DNS checking before update
 *                  DYNDNS_CHECK_EXIST to check first in DNS
 *   fqhn       : one of:
 *                  fully qualified host name; required for secure update
 *                  may be NULL if secure update not required
 * Returns:
 *   -1: error
 *    0: success
 */
int
dyndns_update_nameaddr(
    dyndns_update_op_t update_op, dyndns_zone_dir_t update_zone,
    const char *hostname, int af, const void *addr, uint32_t ttl,
    dyndns_check_opt_t do_check, const char *fqhn)
{
	int is_exist, is_match;
	int dyndns_security_modes;
	char ad_domain[MAXHOSTNAMELEN];
	char *ad_domainp;
	int buf_sz = NS_MAXMSG;
	int rbuf_sz = NS_MAXMSG;
	uchar_t *buf, *rbuf;
	int buf_len, rbuf_len;
	union {
		uchar_t *cp;
		HEADER *hp;
	} mu;
	res_state statp;
	int msg_len;
	ns_type type;
	char zone[NS_MAXDNAME];
	char name[NS_MAXDNAME];
	char data[NS_MAXDNAME];
	union res_sockaddr_union ns[MAXNS];
	int ns_cnt, ns_cur;
	char ns_addr[INET6_ADDRSTRLEN];
	int ret;
	boolean_t secure_update;

	if (update_zone == DYNDNS_ZONE_FWD && hostname == NULL) {
		syslog(LOG_DEBUG, "dyndns: update_nameaddr: "
		    "requested forward update but no hostname provided");
		return (-1);
	}
	if (update_zone == DYNDNS_ZONE_REV && addr == NULL) {
		syslog(LOG_DEBUG, "dyndns: update_nameaddr: "
		    "requested reverse update but no address provided");
		return (-1);
	}
	if (update_op == DYNDNS_UPDATE_ADD ||
	    update_op == DYNDNS_UPDATE_DEL_ONE)
		if (hostname == NULL && addr == NULL) {
			syslog(LOG_DEBUG, "dyndns: update_nameaddr: "
			    "requested update requires hostname and address");
			return (-1);
		}

	/* don't check af if it is not used by the update */
	if (!(update_op == DYNDNS_UPDATE_DEL_CLEAR &&
	    update_zone == DYNDNS_ZONE_FWD) &&
	    !(af == AF_INET || af == AF_INET6)) {
		syslog(LOG_DEBUG, "dyndns: update_nameaddr: "
		    "address family %d not supported", af);
		return (-1);
	}

	if (setlogmask(0) & LOG_MASK(LOG_INFO))
		dyndns_log_update(LOG_INFO, update_op, update_zone,
		    hostname, af, addr);

	if (do_check == DYNDNS_CHECK_EXIST &&
	    update_op != DYNDNS_UPDATE_DEL_ALL) {
		if ((is_exist = dyndns_search_entry(update_zone, hostname,
		    af, addr, &is_match)) < 0) {
			syslog(LOG_ERR,
			    "dyndns: error while checking current DNS entry");
			return (-1);
		}

		if (update_op == DYNDNS_UPDATE_ADD && is_exist && is_match) {
			syslog(LOG_INFO, "dyndns: update finished: "
			    "nothing to add");
			return (0);
		} else if (update_op != DYNDNS_UPDATE_ADD && !is_exist) {
			syslog(LOG_INFO, "dyndns: update finished: "
			    "nothing to delete");
			return (0);
		}
	}

	/*
	 * TBD: read a property to set which update mode(s) to attempt
	 */
	dyndns_security_modes = DYNDNS_SECURITY_NONE;
	if ((smb_config_get_secmode() == SMB_SECMODE_DOMAIN) && (fqhn != NULL))
		dyndns_security_modes |= DYNDNS_SECURITY_GSS;

	if (dyndns_security_modes & DYNDNS_SECURITY_GSS) {
		if (smb_getdomainname_ad(ad_domain, sizeof (ad_domain)) != 0) {
			if ((ad_domainp = strchr(fqhn, '.')) == NULL) {
				syslog(LOG_ERR, "dyndns: bad domain name");
				return (-1);
			}
			++ad_domainp;
		} else {
			ad_domainp = ad_domain;
		}

		if (!smb_krb5_kt_find(SMB_KRB5_PN_ID_HOST_FQHN, ad_domainp,
		    smb_krb5_kt_getpath())) {
			syslog(LOG_NOTICE, "dyndns: secure update unavailable: "
			    "cannot find host principal \"%s\" "
			    "for realm \"%s\" in local keytab file.",
			    fqhn, ad_domainp);
			dyndns_security_modes &= ~DYNDNS_SECURITY_GSS;
		}
	}

	secure_update = (dyndns_security_modes & DYNDNS_SECURITY_GSS);

	if ((buf = malloc(buf_sz)) == NULL) {
		syslog(LOG_ERR, "dyndns: %m");
		return (-1);
	}
	if ((rbuf = malloc(rbuf_sz)) == NULL) {
		syslog(LOG_ERR, "dyndns: %m");
		free(buf);
		return (-1);
	}

	mu.cp = buf;

	if ((statp = calloc(1, sizeof (struct __res_state))) == NULL) {
		syslog(LOG_ERR, "dyndns: %m");
		free(buf);
		free(rbuf);
		return (-1);
	}
	if (res_ninit(statp) != 0) {
		syslog(LOG_ERR, "dyndns: update error: "
		    "could not initialize resolver");
		free(buf);
		free(rbuf);
		free(statp);
		return (-1);
	}

	/*
	 * WORKAROUND:
	 * 7013458 libresolv2 res_nsend is sometimes inconsistent
	 *   for UDP vs. TCP queries
	 *
	 * Remove the next statement when 7013458 is fixed.
	 */
	statp->pfcode |= RES_PRF_HEAD2 | RES_PRF_HEAD1 | RES_PRF_HEADX |
	    RES_PRF_QUES | RES_PRF_ANS | RES_PRF_AUTH | RES_PRF_ADD;

	if ((ns_cnt = dyndns_get_update_info(statp,
	    update_zone, hostname, af, addr, &type,
	    zone, sizeof (zone),
	    name, sizeof (name),
	    data, sizeof (data),
	    ns, MAXNS)) == -1) {
		syslog(LOG_ERR, "dyndns: update error: "
		    "could not setup update information");
		free(buf);
		free(rbuf);
		res_ndestroy(statp);
		free(statp);
		return (-1);
	}

	if ((msg_len = dyndns_build_update_msg(statp, buf, buf_sz,
	    zone, name, type, data, ttl, update_op)) <= 0) {
		syslog(LOG_ERR, "dyndns: update error: "
		    "could not build update message");
		free(buf);
		free(rbuf);
		res_ndestroy(statp);
		free(statp);
		return (-1);
	}

	/*
	 * Updates are attempted using the servers listed in resolv.conf.
	 */
	ns_cnt = res_getservers(statp, ns, MAXNS);

	/*
	 * TBD: revise this algorithm to use the primary master nameserver
	 * (returned by dyndns_get_update_info above) and the authoritative
	 * nameservers found by searching the zone's NS RRset.
	 */

	for (ns_cur = 0; ns_cur < ns_cnt; ns_cur++) {

		*ns_addr = '\0';
		if (ns[ns_cur].sin.sin_family == AF_INET) {
			(void) inet_ntop(AF_INET, &ns[ns_cur].sin.sin_addr,
			    ns_addr, sizeof (ns_addr));
		} else if (ns[ns_cur].sin6.sin6_family == AF_INET6) {
			(void) inet_ntop(AF_INET6, &ns[ns_cur].sin6.sin6_addr,
			    ns_addr, sizeof (ns_addr));
		}
		if (strlen(ns_addr) > 0)
			syslog(LOG_INFO,
			    "dyndns: trying update on server at %s", ns_addr);

		(void) res_setservers(statp, &ns[ns_cur], 1);

		/* attempt non-secure update on this server */
		if (dyndns_security_modes & DYNDNS_SECURITY_NONE) {
			ns_msg msg;
			int rcode;

			/* set new message ID if needed */
			if (ns_cur > 0)
				mu.hp->id =
				    htons(statp->id = res_nrandomid(statp));

			if ((rbuf_len = res_nsend(statp, buf, msg_len,
			    rbuf, rbuf_sz)) <= 0) {
				ret = -1;
				continue;
			}

			if (ns_initparse(rbuf, rbuf_len, &msg) != 0) {
				ret = -1;
				continue;
			}
			rcode = ns_msg_getflag(msg, ns_f_rcode);

			if (rcode == NOERROR) {
				syslog(LOG_INFO,
				    "dyndns: non-secure update completed");
				ret = 0;
				break;
			}

			/*
			 * DNS servers typically return REFUSED error code when
			 * they allow secure updates only. If secure update
			 * will be attempted next, suppress the expected
			 * REFUSED error messages when performing
			 * non-secure updates. Otherwise, "REFUSED" error
			 * messages should be logged.
			 */
			if ((secure_update && (rcode != REFUSED)) ||
			    (!secure_update)) {
				dyndns_syslog(LOG_ERR, rcode,
				    "non-secure update response code");
				ret = -1;
				continue;
			}
		}

		/* attempt secure update on this server */
		if (secure_update) {
			dyndns_tsig_rdata_t tsig;
			struct timeval tv;
			const char *key_name;
			OM_uint32 min, maj;
			gss_buffer_desc in_mic, out_mic;
			gss_ctx_id_t gss_context;
			ssize_t tsig_len;
			ns_msg msg;
			int rcode;

			tsig.ts_alg_name = DYNDNS_MSAD_GSS_ALG;
			tsig.ts_fudge_time = NS_TSIG_FUDGE;
			tsig.ts_error = ns_r_noerror;
			tsig.ts_mac_size = 0;
			tsig.ts_mac_data = NULL;
			tsig.ts_other_size = 0;
			tsig.ts_other_data = NULL;

			/*
			 * TBD: per RFC 3645 section 3.1.2,
			 * the key name SHOULD be made globally unique
			 */
			key_name = fqhn;

			if ((gss_context = dyndns_get_sec_context(statp,
			    key_name, &ns[ns_cur])) == NULL) {
				ret = -1;
				continue;
			}

			/* set new message ID if needed */
			if (ns_cur > 0 ||
			    dyndns_security_modes & ~DYNDNS_SECURITY_GSS)
				mu.hp->id =
				    htons(statp->id = res_nrandomid(statp));

			tsig.ts_orig_id = statp->id;

			(void) gettimeofday(&tv, 0);
			tsig.ts_sign_time = tv.tv_sec;
			if ((tsig_len = dyndns_build_tsig(buf, buf_sz, msg_len,
			    key_name, &tsig, DYNDNS_DIGEST_UNSIGNED)) <= 0) {
				if (gss_context != GSS_C_NO_CONTEXT)
					(void) gss_delete_sec_context(&min,
					    &gss_context, NULL);
				ret = -1;
				continue;
			}

			in_mic.length = msg_len + tsig_len;
			in_mic.value = buf;

			/* sign update message */
			(void) gettimeofday(&tv, 0);
			tsig.ts_sign_time = tv.tv_sec;
			if ((maj = gss_get_mic(&min, gss_context, 0,
			    &in_mic, &out_mic)) != GSS_S_COMPLETE) {
				display_stat(maj, min);
				if (gss_context != GSS_C_NO_CONTEXT)
					(void) gss_delete_sec_context(&min,
					    &gss_context, NULL);
				ret = -1;
				continue;
			}

			tsig.ts_mac_size = out_mic.length;
			tsig.ts_mac_data = out_mic.value;

			if ((tsig_len = dyndns_build_tsig(buf, buf_sz, msg_len,
			    key_name, &tsig, DYNDNS_DIGEST_SIGNED)) == -1) {
				(void) gss_release_buffer(&min, &out_mic);
				if (gss_context != GSS_C_NO_CONTEXT)
					(void) gss_delete_sec_context(&min,
					    &gss_context, NULL);
				ret = -1;
				continue;
			}

			DYNDNS_ADD16(mu.hp->arcount, 1);

			(void) gss_release_buffer(&min, &out_mic);

			buf_len = msg_len + tsig_len;

			if ((rbuf_len = res_nsend(statp, buf, buf_len,
			    rbuf, rbuf_sz)) <= 0) {
				if (gss_context != GSS_C_NO_CONTEXT)
					(void) gss_delete_sec_context(&min,
					    &gss_context, NULL);
				DYNDNS_ADD16(mu.hp->arcount, -1);
				ret = -1;
				continue;
			}

			if (ns_initparse(rbuf, rbuf_len, &msg) != 0) {
				if (gss_context != GSS_C_NO_CONTEXT)
					(void) gss_delete_sec_context(&min,
					    &gss_context, NULL);
				DYNDNS_ADD16(mu.hp->arcount, -1);
				ret = -1;
				continue;
			}

			if (gss_context != GSS_C_NO_CONTEXT)
				(void) gss_delete_sec_context(&min,
				    &gss_context, NULL);

			rcode = ns_msg_getflag(msg, ns_f_rcode);

			/* check here for update request is successful */
			if (rcode == NOERROR) {
				syslog(LOG_INFO,
				    "dyndns: secure update completed");
				ret = 0;
				break;
			}

			dyndns_syslog(LOG_ERR, rcode,
			    "secure update response code");
			DYNDNS_ADD16(mu.hp->arcount, -1);
			ret = -1;
		}
	}

	free(buf);
	free(rbuf);

	res_ndestroy(statp);
	free(statp);

	if (ret != 0) {
		syslog(LOG_ERR, "dyndns: %s failed on all configured name "
		    "servers", (secure_update) ?
		    "both non-secure and secure updates" :
		    "non-secure update");
	}

	return (ret);
}

/*
 * dyndns_zone_update
 * Perform dynamic update on both forward and reverse lookup zone using
 * the specified hostname and IP addresses.  Before updating DNS, existing
 * host entries with the same hostname in the forward lookup zone are removed
 * and existing pointer entries with the same IP addresses in the reverse
 * lookup zone are removed.  After DNS update, host entries for current
 * hostname will show current IP addresses and pointer entries for current
 * IP addresses will show current hostname.
 * Parameters:
 *  fqdn - fully-qualified domain name (in lower case)
 *
 * Returns:
 *   -1: some dynamic DNS updates errors
 *    0: successful or DDNS disabled.
 */
int
dyndns_zone_update(const char *fqdn)
{
	int forw_update_ok, error;
	smb_niciter_t ni;
	int rc;
	char fqhn[MAXHOSTNAMELEN];

	if (fqdn == NULL || *fqdn == '\0')
		return (0);

	if (!smb_config_getbool(SMB_CI_DYNDNS_ENABLE))
		return (0);

	if (smb_gethostname(fqhn, MAXHOSTNAMELEN, SMB_CASE_LOWER) != 0)
		return (-1);

	/*
	 * To comply with RFC 4120 section 6.2.1, the fully-qualified hostname
	 * must be set to lower case.
	 */
	(void) snprintf(fqhn, MAXHOSTNAMELEN, "%s.%s", fqhn, fqdn);

	error = 0;
	forw_update_ok = 0;

	/*
	 * NULL IP is okay since we are removing all using the hostname.
	 */
	if (dyndns_update_nameaddr(DYNDNS_UPDATE_DEL_ALL,
	    DYNDNS_ZONE_FWD, fqhn, AF_INET, NULL, 0,
	    DYNDNS_CHECK_NONE, fqhn) == 0) {
		forw_update_ok = 1;
	} else {
		error++;
	}

	if (smb_config_getbool(SMB_CI_IPV6_ENABLE)) {
		forw_update_ok = 0;
		if (dyndns_update_nameaddr(DYNDNS_UPDATE_DEL_ALL,
		    DYNDNS_ZONE_FWD, fqhn, AF_INET6, NULL, 0,
		    DYNDNS_CHECK_NONE, fqhn) == 0) {
			forw_update_ok = 1;
		} else {
			error++;
		}
	}

	if (smb_nic_getfirst(&ni) != SMB_NIC_SUCCESS)
		return (-1);

	do {
		if (ni.ni_nic.nic_sysflags & IFF_PRIVATE)
			continue;
		if (forw_update_ok) {
			rc = dyndns_update_nameaddr(DYNDNS_UPDATE_ADD,
			    DYNDNS_ZONE_FWD, fqhn, ni.ni_nic.nic_ip.a_family,
			    &ni.ni_nic.nic_ip.au_addr, DYNDNS_TTL,
			    DYNDNS_CHECK_NONE, fqhn);

			if (rc == -1)
				error++;
		}

		/*
		 * NULL hostname is okay since we are removing all using the IP.
		 */
		rc = dyndns_update_nameaddr(DYNDNS_UPDATE_DEL_ALL,
		    DYNDNS_ZONE_REV, NULL, ni.ni_nic.nic_ip.a_family,
		    &ni.ni_nic.nic_ip.au_addr, 0, DYNDNS_CHECK_NONE, fqhn);
		if (rc == 0)
			rc = dyndns_update_nameaddr(DYNDNS_UPDATE_ADD,
			    DYNDNS_ZONE_REV, fqhn, ni.ni_nic.nic_ip.a_family,
			    &ni.ni_nic.nic_ip.au_addr, DYNDNS_TTL,
			    DYNDNS_CHECK_NONE, fqhn);

		if (rc == -1)
			error++;

	} while (smb_nic_getnext(&ni) == SMB_NIC_SUCCESS);

	return ((error == 0) ? 0 : -1);
}

/*
 * dyndns_zone_clear
 * Clear the rev zone records. Must be called to clear the OLD if list
 * of down records prior to updating the list with new information.
 *
 * Parameters:
 *   fqdn - fully-qualified domain name (in lower case)
 * Returns:
 *   -1: some dynamic DNS updates errors
 *    0: successful or DDNS disabled.
 */
int
dyndns_zone_clear(const char *fqdn)
{
	int error;
	smb_niciter_t ni;
	int rc;
	char fqhn[MAXHOSTNAMELEN];

	if (fqdn == NULL || *fqdn == '\0')
		return (0);

	if (!smb_config_getbool(SMB_CI_DYNDNS_ENABLE))
		return (0);

	if (smb_gethostname(fqhn, MAXHOSTNAMELEN, SMB_CASE_LOWER) != 0)
		return (-1);

	/*
	 * To comply with RFC 4120 section 6.2.1, the fully-qualified hostname
	 * must be set to lower case.
	 */
	(void) snprintf(fqhn, MAXHOSTNAMELEN, "%s.%s", fqhn, fqdn);

	error = 0;

	if (smb_nic_getfirst(&ni) != SMB_NIC_SUCCESS)
		return (-1);

	do {
		if (ni.ni_nic.nic_sysflags & IFF_PRIVATE)
			continue;
		/*
		 * NULL hostname is okay since we are removing all using the IP.
		 */
		rc = dyndns_update_nameaddr(DYNDNS_UPDATE_DEL_ALL,
		    DYNDNS_ZONE_REV, NULL, ni.ni_nic.nic_ip.a_family,
		    &ni.ni_nic.nic_ip.au_addr, 0, DYNDNS_CHECK_NONE, fqhn);
		if (rc != 0)
			error++;

	} while (smb_nic_getnext(&ni) == SMB_NIC_SUCCESS);

	return ((error == 0) ? 0 : -1);
}
