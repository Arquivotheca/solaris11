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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This file contains routines that are used to modify/retrieve protocol or
 * interface property values. It also holds all the supported properties for
 * both IP interface and protocols in `ipadm_prop_desc_t'. Following protocols
 * are supported: IP, IPv4, IPv6, TCP, SCTP, UDP and ICMP.
 *
 * This file also contains walkers, which walks through the property table and
 * calls the callback function, of the form `ipadm_prop_wfunc_t' , for every
 * property in the table.
 */

#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <strings.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/sockio.h>
#include <assert.h>
#include <libdllink.h>
#include <zone.h>
#include "libipadm_impl.h"
#include <inet/tunables.h>

#define	IPADM_NONESTR		"none"
#define	DEF_METRIC_VAL		0	/* default metric value */

#define	A_CNT(arr)	(sizeof (arr) / sizeof (arr[0]))

static ipadm_status_t	i_ipadm_validate_if(ipadm_handle_t, const char *,
			    uint_t, uint_t);

/*
 * Callback functions to retrieve property values from the kernel. These
 * functions, when required, translate the values from the kernel to a format
 * suitable for printing. For example: boolean values will be translated
 * to on/off. They also retrieve DEFAULT, PERM and POSSIBLE values for
 * a given property.
 */
static ipadm_pd_getf_t	i_ipadm_get_prop, i_ipadm_get_ifprop_flags,
			i_ipadm_get_mtu, i_ipadm_get_metric,
			i_ipadm_get_usesrc, i_ipadm_get_forwarding,
			i_ipadm_get_ecnsack, i_ipadm_get_hostmodel,
			i_ipadm_get_group, i_ipadm_get_cong_default_range,
			i_ipadm_get_cong_enabled;

/*
 * Callback function to set property values. These functions translate the
 * values to a format suitable for kernel consumption, allocates the necessary
 * ioctl buffers and then invokes ioctl().
 */
static ipadm_pd_setf_t	i_ipadm_set_prop, i_ipadm_set_mtu,
			i_ipadm_set_ifprop_flags,
			i_ipadm_set_metric, i_ipadm_set_usesrc,
			i_ipadm_set_forwarding, i_ipadm_set_eprivport,
			i_ipadm_set_ecnsack, i_ipadm_set_hostmodel,
			i_ipadm_set_group, i_ipadm_set_cong_enabled;

/* array of protocols we support */
static int protocols[] = { MOD_PROTO_IP, MOD_PROTO_RAWIP,
			    MOD_PROTO_TCP, MOD_PROTO_UDP,
			    MOD_PROTO_SCTP };

/*
 * Supported IP protocol properties.
 */
static ipadm_prop_desc_t ipadm_ip_prop_table[] = {
	{ "arp", IPADMPROP_CLASS_IF, MOD_PROTO_IPV4, 0,
	    i_ipadm_set_ifprop_flags, i_ipadm_get_onoff,
	    i_ipadm_get_ifprop_flags },

	{ "forwarding", IPADMPROP_CLASS_MODIF, MOD_PROTO_IPV4, 0,
	    i_ipadm_set_forwarding, i_ipadm_get_onoff,
	    i_ipadm_get_forwarding },

	{ "metric", IPADMPROP_CLASS_IF, MOD_PROTO_IPV4, 0,
	    i_ipadm_set_metric, NULL, i_ipadm_get_metric },

	{ "mtu", IPADMPROP_CLASS_IF, MOD_PROTO_IPV4, 0,
	    i_ipadm_set_mtu, i_ipadm_get_mtu, i_ipadm_get_mtu },

	{ "exchange_routes", IPADMPROP_CLASS_IF, MOD_PROTO_IPV4, 0,
	    i_ipadm_set_ifprop_flags, i_ipadm_get_onoff,
	    i_ipadm_get_ifprop_flags },

	{ "usesrc", IPADMPROP_CLASS_IF, MOD_PROTO_IPV4, 0,
	    i_ipadm_set_usesrc, NULL, i_ipadm_get_usesrc },

	{ "ttl", IPADMPROP_CLASS_MODULE, MOD_PROTO_IPV4, 0,
	    i_ipadm_set_prop, i_ipadm_get_prop, i_ipadm_get_prop },

	{ "forwarding", IPADMPROP_CLASS_MODIF, MOD_PROTO_IPV6, 0,
	    i_ipadm_set_forwarding, i_ipadm_get_onoff,
	    i_ipadm_get_forwarding },

	{ "hoplimit", IPADMPROP_CLASS_MODULE, MOD_PROTO_IPV6, 0,
	    i_ipadm_set_prop, i_ipadm_get_prop, i_ipadm_get_prop },

	{ "metric", IPADMPROP_CLASS_IF, MOD_PROTO_IPV6, 0,
	    i_ipadm_set_metric, NULL, i_ipadm_get_metric },

	{ "mtu", IPADMPROP_CLASS_IF, MOD_PROTO_IPV6, 0,
	    i_ipadm_set_mtu, i_ipadm_get_mtu, i_ipadm_get_mtu },

	{ "nud", IPADMPROP_CLASS_IF, MOD_PROTO_IPV6, 0,
	    i_ipadm_set_ifprop_flags, i_ipadm_get_onoff,
	    i_ipadm_get_ifprop_flags },

	{ "exchange_routes", IPADMPROP_CLASS_IF, MOD_PROTO_IPV6, 0,
	    i_ipadm_set_ifprop_flags, i_ipadm_get_onoff,
	    i_ipadm_get_ifprop_flags },

	{ "usesrc", IPADMPROP_CLASS_IF, MOD_PROTO_IPV6, 0,
	    i_ipadm_set_usesrc, NULL, i_ipadm_get_usesrc },

	{ "hostmodel", IPADMPROP_CLASS_MODULE, MOD_PROTO_IPV6, 0,
	    i_ipadm_set_hostmodel, i_ipadm_get_hostmodel,
	    i_ipadm_get_hostmodel },

	{ "hostmodel", IPADMPROP_CLASS_MODULE, MOD_PROTO_IPV4, 0,
	    i_ipadm_set_hostmodel, i_ipadm_get_hostmodel,
	    i_ipadm_get_hostmodel },

	{ "group", IPADMPROP_CLASS_IF, MOD_PROTO_IP, IPADMPROP_GETPERSIST,
	    i_ipadm_set_group, NULL, i_ipadm_get_group },

	{ "standby", IPADMPROP_CLASS_IF, MOD_PROTO_IP, 0,
	    i_ipadm_set_ifprop_flags, i_ipadm_get_onoff,
	    i_ipadm_get_ifprop_flags },

	{ NULL, 0, 0, 0, NULL, NULL, NULL }
};

/* possible values for TCP properties `ecn' and `sack' */
static const char *ecn_sack_vals[] = {"never", "passive", "active", NULL};

/* Supported TCP protocol properties */
static ipadm_prop_desc_t ipadm_tcp_prop_table[] = {
	{ "cong_default", IPADMPROP_CLASS_MODULE, MOD_PROTO_TCP, 0,
	    i_ipadm_set_prop, i_ipadm_get_cong_default_range,
	    i_ipadm_get_prop },

	{ "cong_enabled", IPADMPROP_CLASS_MODULE, MOD_PROTO_TCP,
	    IPADMPROP_MULVAL | IPADMPROP_GETPERSIST | IPADMPROP_SMFPERSIST,
	    i_ipadm_set_cong_enabled, i_ipadm_get_cong_enabled,
	    i_ipadm_get_cong_enabled },

	{ "ecn", IPADMPROP_CLASS_MODULE, MOD_PROTO_TCP, 0,
	    i_ipadm_set_ecnsack, i_ipadm_get_ecnsack, i_ipadm_get_ecnsack },

	{ "extra_priv_ports", IPADMPROP_CLASS_MODULE, MOD_PROTO_TCP,
	    IPADMPROP_MULVAL, i_ipadm_set_eprivport, i_ipadm_get_prop,
	    i_ipadm_get_prop },

	{ "largest_anon_port", IPADMPROP_CLASS_MODULE, MOD_PROTO_TCP, 0,
	    i_ipadm_set_prop, i_ipadm_get_prop, i_ipadm_get_prop },

	{ "max_buf", IPADMPROP_CLASS_MODULE, MOD_PROTO_TCP, 0,
	    i_ipadm_set_prop, i_ipadm_get_prop, i_ipadm_get_prop },

	{ "recv_buf", IPADMPROP_CLASS_MODULE, MOD_PROTO_TCP, 0,
	    i_ipadm_set_prop, i_ipadm_get_prop, i_ipadm_get_prop },

	{ "sack", IPADMPROP_CLASS_MODULE, MOD_PROTO_TCP, 0,
	    i_ipadm_set_ecnsack, i_ipadm_get_ecnsack, i_ipadm_get_ecnsack },

	{ "send_buf", IPADMPROP_CLASS_MODULE, MOD_PROTO_TCP, 0,
	    i_ipadm_set_prop, i_ipadm_get_prop, i_ipadm_get_prop },

	{ "smallest_anon_port", IPADMPROP_CLASS_MODULE, MOD_PROTO_TCP, 0,
	    i_ipadm_set_prop, i_ipadm_get_prop, i_ipadm_get_prop },

	{ "smallest_nonpriv_port", IPADMPROP_CLASS_MODULE, MOD_PROTO_TCP, 0,
	    i_ipadm_set_prop, i_ipadm_get_prop, i_ipadm_get_prop },

	{ NULL, 0, 0, 0, NULL, NULL, NULL }
};

/* Supported UDP protocol properties */
static ipadm_prop_desc_t ipadm_udp_prop_table[] = {
	{ "extra_priv_ports", IPADMPROP_CLASS_MODULE, MOD_PROTO_UDP,
	    IPADMPROP_MULVAL, i_ipadm_set_eprivport, i_ipadm_get_prop,
	    i_ipadm_get_prop },

	{ "largest_anon_port", IPADMPROP_CLASS_MODULE, MOD_PROTO_UDP, 0,
	    i_ipadm_set_prop, i_ipadm_get_prop, i_ipadm_get_prop },

	{ "max_buf", IPADMPROP_CLASS_MODULE, MOD_PROTO_UDP, 0,
	    i_ipadm_set_prop, i_ipadm_get_prop, i_ipadm_get_prop },

	{ "recv_buf", IPADMPROP_CLASS_MODULE, MOD_PROTO_UDP, 0,
	    i_ipadm_set_prop, i_ipadm_get_prop, i_ipadm_get_prop },

	{ "send_buf", IPADMPROP_CLASS_MODULE, MOD_PROTO_UDP, 0,
	    i_ipadm_set_prop, i_ipadm_get_prop, i_ipadm_get_prop },

	{ "smallest_anon_port", IPADMPROP_CLASS_MODULE, MOD_PROTO_UDP, 0,
	    i_ipadm_set_prop, i_ipadm_get_prop, i_ipadm_get_prop },

	{ "smallest_nonpriv_port", IPADMPROP_CLASS_MODULE, MOD_PROTO_UDP, 0,
	    i_ipadm_set_prop, i_ipadm_get_prop, i_ipadm_get_prop },

	{ NULL, 0, 0, 0, NULL, NULL, NULL }
};

/* Supported SCTP protocol properties */
static ipadm_prop_desc_t ipadm_sctp_prop_table[] = {
	{ "cong_default", IPADMPROP_CLASS_MODULE, MOD_PROTO_SCTP, 0,
	    i_ipadm_set_prop, i_ipadm_get_cong_default_range,
	    i_ipadm_get_prop },

	{ "cong_enabled", IPADMPROP_CLASS_MODULE, MOD_PROTO_SCTP,
	    IPADMPROP_MULVAL | IPADMPROP_GETPERSIST | IPADMPROP_SMFPERSIST,
	    i_ipadm_set_cong_enabled, i_ipadm_get_cong_enabled,
	    i_ipadm_get_cong_enabled },

	{ "extra_priv_ports", IPADMPROP_CLASS_MODULE, MOD_PROTO_SCTP,
	    IPADMPROP_MULVAL, i_ipadm_set_eprivport, i_ipadm_get_prop,
	    i_ipadm_get_prop },

	{ "largest_anon_port", IPADMPROP_CLASS_MODULE, MOD_PROTO_SCTP, 0,
	    i_ipadm_set_prop, i_ipadm_get_prop, i_ipadm_get_prop },

	{ "max_buf", IPADMPROP_CLASS_MODULE, MOD_PROTO_SCTP, 0,
	    i_ipadm_set_prop, i_ipadm_get_prop, i_ipadm_get_prop },

	{ "recv_buf", IPADMPROP_CLASS_MODULE, MOD_PROTO_SCTP, 0,
	    i_ipadm_set_prop, i_ipadm_get_prop, i_ipadm_get_prop },

	{ "send_buf", IPADMPROP_CLASS_MODULE, MOD_PROTO_SCTP, 0,
	    i_ipadm_set_prop, i_ipadm_get_prop, i_ipadm_get_prop },

	{ "smallest_anon_port", IPADMPROP_CLASS_MODULE, MOD_PROTO_SCTP, 0,
	    i_ipadm_set_prop, i_ipadm_get_prop, i_ipadm_get_prop },

	{ "smallest_nonpriv_port", IPADMPROP_CLASS_MODULE, MOD_PROTO_SCTP, 0,
	    i_ipadm_set_prop, i_ipadm_get_prop, i_ipadm_get_prop },

	{ NULL, 0, 0, 0, NULL, NULL, NULL }
};

/* Supported ICMP protocol properties */
static ipadm_prop_desc_t ipadm_icmp_prop_table[] = {
	{ "max_buf", IPADMPROP_CLASS_MODULE, MOD_PROTO_RAWIP, 0,
	    i_ipadm_set_prop, i_ipadm_get_prop, i_ipadm_get_prop },

	{ "recv_buf", IPADMPROP_CLASS_MODULE, MOD_PROTO_RAWIP, 0,
	    i_ipadm_set_prop, i_ipadm_get_prop, i_ipadm_get_prop },

	{ "send_buf", IPADMPROP_CLASS_MODULE, MOD_PROTO_RAWIP, 0,
	    i_ipadm_set_prop, i_ipadm_get_prop, i_ipadm_get_prop },

	{ NULL, 0, 0, 0, NULL, NULL, NULL }
};

/*
 * A dummy private property structure, used while handling private
 * protocol properties (properties not yet supported by libipadm).
 */
static ipadm_prop_desc_t	ipadm_privprop =\
	{ NULL, IPADMPROP_CLASS_MODULE, MOD_PROTO_NONE, 0,
	    i_ipadm_set_prop, i_ipadm_get_prop, i_ipadm_get_prop };

/*
 * Returns the property description table, for the given protocol
 */
static ipadm_prop_desc_t *
i_ipadm_get_propdesc_table(uint_t proto)
{
	switch (proto) {
	case MOD_PROTO_IP:
	case MOD_PROTO_IPV4:
	case MOD_PROTO_IPV6:
		return (ipadm_ip_prop_table);
	case MOD_PROTO_RAWIP:
		return (ipadm_icmp_prop_table);
	case MOD_PROTO_TCP:
		return (ipadm_tcp_prop_table);
	case MOD_PROTO_UDP:
		return (ipadm_udp_prop_table);
	case MOD_PROTO_SCTP:
		return (ipadm_sctp_prop_table);
	}

	return (NULL);
}

static ipadm_prop_desc_t *
i_ipadm_get_prop_desc(const char *pname, uint_t proto, int *errp)
{
	int		err = 0;
	boolean_t	matched_name = B_FALSE;
	ipadm_prop_desc_t *ipdp = NULL, *ipdtbl;

	if ((ipdtbl = i_ipadm_get_propdesc_table(proto)) == NULL) {
		err = EINVAL;
		goto ret;
	}
	for (ipdp = ipdtbl; ipdp->ipd_name != NULL; ipdp++) {
		if (strcmp(pname, ipdp->ipd_name) == 0) {
			matched_name = B_TRUE;
			if (ipdp->ipd_proto == proto)
				break;
		}
	}
	if (ipdp->ipd_name == NULL) {
		err = ENOENT;
		/* if we matched name, but failed protocol check */
		if (matched_name)
			err = EPROTO;
		ipdp = NULL;
	}
ret:
	if (errp != NULL)
		*errp = err;
	return (ipdp);
}

char *
ipadm_proto2str(uint_t proto)
{
	switch (proto) {
	case MOD_PROTO_IP:
		return ("ip");
	case MOD_PROTO_IPV4:
		return ("ipv4");
	case MOD_PROTO_IPV6:
		return ("ipv6");
	case MOD_PROTO_RAWIP:
		return ("icmp");
	case MOD_PROTO_TCP:
		return ("tcp");
	case MOD_PROTO_UDP:
		return ("udp");
	case MOD_PROTO_SCTP:
		return ("sctp");
	}

	return (NULL);
}

uint_t
ipadm_str2proto(const char *protostr)
{
	if (protostr == NULL)
		return (MOD_PROTO_NONE);
	if (strcmp(protostr, "tcp") == 0)
		return (MOD_PROTO_TCP);
	else if (strcmp(protostr, "udp") == 0)
		return (MOD_PROTO_UDP);
	else if (strcmp(protostr, "ip") == 0)
		return (MOD_PROTO_IP);
	else if (strcmp(protostr, "ipv4") == 0)
		return (MOD_PROTO_IPV4);
	else if (strcmp(protostr, "ipv6") == 0)
		return (MOD_PROTO_IPV6);
	else if (strcmp(protostr, "icmp") == 0)
		return (MOD_PROTO_RAWIP);
	else if (strcmp(protostr, "sctp") == 0)
		return (MOD_PROTO_SCTP);
	else if (strcmp(protostr, "arp") == 0)
		return (MOD_PROTO_IP);

	return (MOD_PROTO_NONE);
}

static ipadm_status_t
i_ipadm_set_mtu(ipadm_handle_t iph, const void *arg,
    ipadm_prop_desc_t *pdp, const void *pval, uint_t proto, uint_t flags)
{
	struct lifreq	lifr;
	char		*endp;
	uint_t		mtu;
	int		s;
	const char	*ifname = arg;
	char		val[MAXPROPVALLEN];

	/* to reset MTU first retrieve the default MTU and then set it */
	if (flags & IPADM_OPT_DEFAULT) {
		ipadm_status_t	status;
		uint_t		size = MAXPROPVALLEN;

		status = i_ipadm_get_prop(iph, arg, pdp, val, &size,
		    proto, MOD_PROP_DEFAULT);
		if (status != IPADM_SUCCESS)
			return (status);
		pval = val;
	}

	errno = 0;
	mtu = (uint_t)strtol(pval, &endp, 10);
	if (errno != 0 || *endp != '\0')
		return (IPADM_INVALID_ARG);

	bzero(&lifr, sizeof (lifr));
	(void) strlcpy(lifr.lifr_name, ifname, sizeof (lifr.lifr_name));
	lifr.lifr_mtu = mtu;

	s = (proto == MOD_PROTO_IPV6 ? iph->ih_sock6 : iph->ih_sock);
	if (ioctl(s, SIOCSLIFMTU, (caddr_t)&lifr) < 0)
		return (ipadm_errno2status(errno));

	return (IPADM_SUCCESS);
}

/* ARGSUSED */
static ipadm_status_t
i_ipadm_set_metric(ipadm_handle_t iph, const void *arg,
    ipadm_prop_desc_t *pdp, const void *pval, uint_t proto, uint_t flags)
{
	struct lifreq	lifr;
	char		*endp;
	int		metric;
	const char	*ifname = arg;
	int		s;

	/* if we are resetting, set the value to its default value */
	if (flags & IPADM_OPT_DEFAULT) {
		metric = DEF_METRIC_VAL;
	} else {
		errno = 0;
		metric = (uint_t)strtol(pval, &endp, 10);
		if (errno != 0 || *endp != '\0')
			return (IPADM_INVALID_ARG);
	}

	bzero(&lifr, sizeof (lifr));
	(void) strlcpy(lifr.lifr_name, ifname, sizeof (lifr.lifr_name));
	lifr.lifr_metric = metric;

	s = (proto == MOD_PROTO_IPV6 ? iph->ih_sock6 : iph->ih_sock);

	if (ioctl(s, SIOCSLIFMETRIC, (caddr_t)&lifr) < 0)
		return (ipadm_errno2status(errno));

	return (IPADM_SUCCESS);
}

/* ARGSUSED */
static ipadm_status_t
i_ipadm_set_usesrc(ipadm_handle_t iph, const void *arg,
    ipadm_prop_desc_t *pdp, const void *pval, uint_t proto, uint_t flags)
{
	struct lifreq	lifr;
	const char	*ifname = arg;
	int		s;
	uint_t		ifindex = 0;

	/* if we are resetting, set the value to its default value */
	if (flags & IPADM_OPT_DEFAULT)
		pval = IPADM_NONESTR;

	/*
	 * cannot specify logical interface name. We can also filter out other
	 * bogus interface names here itself through i_ipadm_validate_ifname().
	 */
	if (strcmp(pval, IPADM_NONESTR) != 0 &&
	    !i_ipadm_validate_ifname(iph, pval))
		return (IPADM_INVALID_IFNAME);

	bzero(&lifr, sizeof (lifr));
	(void) strlcpy(lifr.lifr_name, ifname, sizeof (lifr.lifr_name));

	s = (proto == MOD_PROTO_IPV6 ? iph->ih_sock6 : iph->ih_sock);

	if (strcmp(pval, IPADM_NONESTR) != 0) {
		if ((ifindex = if_nametoindex(pval)) == 0)
			return (ipadm_errno2status(errno));
		lifr.lifr_index = ifindex;
	} else {
		if (ioctl(s, SIOCGLIFUSESRC, (caddr_t)&lifr) < 0)
			return (ipadm_errno2status(errno));
		lifr.lifr_index = 0;
	}
	if (ioctl(s, SIOCSLIFUSESRC, (caddr_t)&lifr) < 0)
		return (ipadm_errno2status(errno));

	return (IPADM_SUCCESS);
}

static struct hostmodel_strval {
	char *esm_str;
	ip_hostmodel_t esm_val;
} esm_arr[] = {
	{"weak", IP_WEAK_ES},
	{"src-priority", IP_SRC_PRI_ES},
	{"strong", IP_STRONG_ES},
	{"custom", IP_MAXVAL_ES}
};

static ip_hostmodel_t
i_ipadm_hostmodel_str2val(const char *pval)
{
	int i;

	for (i = 0; i < A_CNT(esm_arr); i++) {
		if (esm_arr[i].esm_str != NULL &&
		    strcmp(pval, esm_arr[i].esm_str) == 0) {
			return (esm_arr[i].esm_val);
		}
	}
	return (IP_MAXVAL_ES);
}

static char *
i_ipadm_hostmodel_val2str(ip_hostmodel_t pval)
{
	int i;

	for (i = 0; i < A_CNT(esm_arr); i++) {
		if (esm_arr[i].esm_val == pval)
			return (esm_arr[i].esm_str);
	}
	return (NULL);
}

/* ARGSUSED */
static ipadm_status_t
i_ipadm_set_hostmodel(ipadm_handle_t iph, const void *arg,
    ipadm_prop_desc_t *pdp, const void *pval, uint_t proto, uint_t flags)
{
	ip_hostmodel_t hostmodel;
	char val[11]; /* covers uint32_max as a string */

	if ((flags & IPADM_OPT_DEFAULT) == 0) {
		hostmodel = i_ipadm_hostmodel_str2val(pval);
		if (hostmodel == IP_MAXVAL_ES)
			return (IPADM_INVALID_ARG);
		(void) snprintf(val, sizeof (val), "%d", hostmodel);
		pval = val;
	}
	return (i_ipadm_set_prop(iph, NULL, pdp, pval, proto, flags));
}

/* ARGSUSED */
static ipadm_status_t
i_ipadm_get_hostmodel(ipadm_handle_t iph, const void *arg,
    ipadm_prop_desc_t *pdp, char *buf, uint_t *bufsize, uint_t proto,
    uint_t valtype)
{
	ip_hostmodel_t hostmodel;
	char *cp;
	ipadm_status_t status;

	switch (valtype) {
	case MOD_PROP_PERM:
		*bufsize = snprintf(buf, *bufsize, "%d", MOD_PROP_PERM_RW);
		break;
	case MOD_PROP_DEFAULT:
		*bufsize = snprintf(buf, *bufsize, "weak");
		break;
	case MOD_PROP_ACTIVE: {
		uint_t	psize = *bufsize;

		status = i_ipadm_get_prop(iph, arg, pdp, buf, bufsize, proto,
		    valtype);
		/* return if the provided buffer size is insufficient */
		if (status != IPADM_SUCCESS || *bufsize >= psize)
			return (status);
		bcopy(buf, &hostmodel, sizeof (hostmodel));
		cp = i_ipadm_hostmodel_val2str(hostmodel);
		*bufsize = snprintf(buf, psize, "%s",
		    (cp != NULL ? cp : "?"));
		break;
	}
	case MOD_PROP_POSSIBLE:
		*bufsize = snprintf(buf, *bufsize, "strong,src-priority,weak");
		break;
	default:
		return (IPADM_INVALID_ARG);
	}
	return (IPADM_SUCCESS);
}

/* ARGSUSED */
static ipadm_status_t
i_ipadm_set_group(ipadm_handle_t iph, const void *arg,
    ipadm_prop_desc_t *pdp, const void *pval, uint_t proto, uint_t flags)
{
	struct lifreq	lifr;
	const char	*ifname = arg;
	int		s;

	if (!ipadm_is_ipmp(iph, ifname))
		return (IPADM_OP_NOTSUP);

	/* if we are resetting, set the value to its default value */
	if (flags & IPADM_OPT_DEFAULT)
		pval = ifname;

	bzero(&lifr, sizeof (lifr));
	(void) strlcpy(lifr.lifr_name, ifname, sizeof (lifr.lifr_name));
	(void) strlcpy(lifr.lifr_groupname, pval, sizeof (lifr.lifr_groupname));
	s = IPADM_SOCK(iph, i_ipadm_get_active_af(iph, ifname));
	if (ioctl(s, SIOCSLIFGROUPNAME, (caddr_t)&lifr) < 0)
		return (ipadm_errno2status(errno));
	return (IPADM_SUCCESS);
}

/* ARGSUSED */
static ipadm_status_t
i_ipadm_get_group(ipadm_handle_t iph, const void *arg,
    ipadm_prop_desc_t *pdp, char *buf, uint_t *bufsize, uint_t proto,
    uint_t valtype)
{
	int		s;
	struct lifreq	lifr;
	const char	*ifname = arg;
	ipadm_status_t	status;
	char		ipmpif[LIFNAMSIZ];

	switch (valtype) {
	case MOD_PROP_PERM:
		if (ipadm_is_ipmp(iph, ifname)) {
			*bufsize = snprintf(buf, *bufsize, "%d",
			    MOD_PROP_PERM_RW);
		} else {
			*bufsize = snprintf(buf, *bufsize, "%d",
			    MOD_PROP_PERM_READ);
		}
		break;
	case MOD_PROP_DEFAULT:
		if (ipadm_is_ipmp(iph, ifname) ||
		    i_ipadm_ipmp_pexists(iph, ifname)) {
			*bufsize = snprintf(buf, *bufsize, "%s", ifname);
		} else {
			buf[0] = '\0';
			*bufsize = 1;
		}
		break;
	case MOD_PROP_ACTIVE:
		bzero(&lifr, sizeof (lifr));
		(void) strlcpy(lifr.lifr_name, ifname, sizeof (lifr.lifr_name));
		s = IPADM_SOCK(iph, i_ipadm_get_active_af(iph, ifname));
		if (ioctl(s, SIOCGLIFGROUPNAME, (caddr_t)&lifr) < 0)
			return (ipadm_errno2status(errno));
		*bufsize = snprintf(buf, *bufsize, "%s", lifr.lifr_groupname);
		break;
	case MOD_PROP_POSSIBLE:
		buf[0] = '\0';
		*bufsize = 1;
		break;
	case MOD_PROP_PERSIST:
		/*
		 * Get the persistent IPMP interface if `ifname' is an
		 * underlying interface and then return its group name.
		 */
		status = i_ipadm_get_persist_ipmpif(iph, ifname, ipmpif,
		    LIFNAMSIZ);
		if (status != IPADM_SUCCESS && status != IPADM_OBJ_NOTFOUND)
			return (status);
		if (status == IPADM_SUCCESS) {
			/* Get the persistent group for `ipmpif' */
			status = i_ipadm_get_persist_propval(iph, pdp, buf,
			    bufsize, ipmpif);
			if (status == IPADM_OBJ_NOTFOUND) {
				*bufsize = snprintf(buf, *bufsize, "%s",
				    ipmpif);
				status = IPADM_SUCCESS;
			}
		} else {
			/* `ifname' could be an IPMP interface. */
			status = i_ipadm_get_persist_propval(iph, pdp, buf,
			    bufsize, ifname);
			if (status == IPADM_OBJ_NOTFOUND) {
				buf[0] = '\0';
				*bufsize = 1;
				status = IPADM_SUCCESS;
			}
		}
		if (status != IPADM_SUCCESS)
			return (status);
		break;
	default:
		return (IPADM_INVALID_ARG);
	}
	return (IPADM_SUCCESS);
}

/* ARGSUSED */
static ipadm_status_t
i_ipadm_set_ifprop_flags(ipadm_handle_t iph, const void *arg,
    ipadm_prop_desc_t *pdp, const void *pval, uint_t proto, uint_t flags)
{
	ipadm_status_t	status = IPADM_SUCCESS;
	const char	*ifname = arg;
	uint64_t	on_flags = 0, off_flags = 0;
	boolean_t	on = B_FALSE;
	sa_family_t	af = (proto == MOD_PROTO_IPV6 ? AF_INET6 : AF_INET);

	/* if we are resetting, set the value to its default value */
	if (flags & IPADM_OPT_DEFAULT) {
		if (strcmp(pdp->ipd_name, "exchange_routes") == 0 ||
		    strcmp(pdp->ipd_name, "arp") == 0 ||
		    strcmp(pdp->ipd_name, "nud") == 0) {
			pval = IPADM_ONSTR;
		} else if (strcmp(pdp->ipd_name, "forwarding") == 0 ||
		    strcmp(pdp->ipd_name, "standby") == 0) {
			pval = IPADM_OFFSTR;
		} else {
			return (IPADM_PROP_UNKNOWN);
		}
	}

	if (strcmp(pval, IPADM_ONSTR) == 0)
		on = B_TRUE;
	else if (strcmp(pval, IPADM_OFFSTR) == 0)
		on = B_FALSE;
	else
		return (IPADM_INVALID_ARG);

	if (strcmp(pdp->ipd_name, "exchange_routes") == 0) {
		if (on)
			off_flags = IFF_NORTEXCH;
		else
			on_flags = IFF_NORTEXCH;
	} else if (strcmp(pdp->ipd_name, "arp") == 0) {
		if (on)
			off_flags = IFF_NOARP;
		else
			on_flags = IFF_NOARP;
	} else if (strcmp(pdp->ipd_name, "nud") == 0) {
		if (on)
			off_flags = IFF_NONUD;
		else
			on_flags = IFF_NONUD;
	} else if (strcmp(pdp->ipd_name, "forwarding") == 0) {
		if (on)
			on_flags = IFF_ROUTER;
		else
			off_flags = IFF_ROUTER;
	} else if (strcmp(pdp->ipd_name, "standby") == 0) {
		if (on)
			on_flags = IFF_STANDBY;
		else
			off_flags = IFF_STANDBY;

	}

	if (on_flags || off_flags)  {
		if (proto == MOD_PROTO_IP) {
			if (ipadm_if_enabled(iph, ifname, AF_INET))
				af = AF_INET;
			else
				af = AF_INET6;
		}
		status = i_ipadm_set_flags(iph, ifname, af, on_flags,
		    off_flags);
	}
	return (status);
}

/* ARGSUSED */
static ipadm_status_t
i_ipadm_set_eprivport(ipadm_handle_t iph, const void *arg,
    ipadm_prop_desc_t *pdp, const void *pval, uint_t proto, uint_t flags)
{
	nvlist_t	*portsnvl = NULL;
	nvpair_t	*nvp;
	ipadm_status_t	status = IPADM_SUCCESS;
	int		err;
	uint_t		count = 0;

	if (flags & IPADM_OPT_DEFAULT) {
		assert(pval == NULL);
		return (i_ipadm_set_prop(iph, arg, pdp, pval, proto, flags));
	}

	if ((err = ipadm_str2nvlist(pval, &portsnvl, IPADM_NORVAL)) != 0)
		return (ipadm_errno2status(err));

	/* count the number of ports */
	for (nvp = nvlist_next_nvpair(portsnvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(portsnvl, nvp)) {
		++count;
	}

	if (iph->ih_flags & IH_INIT) {
		flags |= IPADM_OPT_APPEND;
	} else if (count > 1) {
		/*
		 * We allow only one port to be added, removed or
		 * assigned at a time.
		 *
		 * However on reboot, while initializing protocol
		 * properties, extra_priv_ports might have multiple
		 * values. Only in that case we allow setting multiple
		 * values.
		 */
		nvlist_free(portsnvl);
		return (IPADM_INVALID_ARG);
	}

	for (nvp = nvlist_next_nvpair(portsnvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(portsnvl, nvp)) {
		status = i_ipadm_set_prop(iph, arg, pdp, nvpair_name(nvp),
		    proto, flags);
		if (status != IPADM_SUCCESS)
			break;
	}
	nvlist_free(portsnvl);
	return (status);
}

/* ARGSUSED */
static ipadm_status_t
i_ipadm_set_cong_enabled(ipadm_handle_t iph, const void *arg,
    ipadm_prop_desc_t *pdp, const void *pval, uint_t proto, uint_t flags)
{
	nvlist_t	*algsnvl = NULL;
	nvpair_t	*nvp;
	ipadm_status_t	status = IPADM_SUCCESS;
	int		err;
	char		*alg;
	uint_t		count = 0;
	char		defval[MAXPROPVALLEN];
	uint_t		defsize = sizeof (defval);
	boolean_t	init = (iph->ih_flags & IH_INIT) != 0;

	if (flags & IPADM_OPT_DEFAULT) {
		assert(pval == NULL);
		status = i_ipadm_set_prop(iph, arg, pdp, pval, proto, flags);

		/* Disable SMF services of non-default algorithms */
		if (status == IPADM_SUCCESS &&
		    i_ipadm_get_cong_enabled(iph, arg, pdp, defval, &defsize,
		    proto, MOD_PROP_DEFAULT) == IPADM_SUCCESS) {
			ipadm_cong_smf_disable_nondef(defval, proto, flags);
		}
		return (status);
	}

	if ((err = ipadm_str2nvlist(pval, &algsnvl, IPADM_NORVAL)) != 0)
		return (ipadm_errno2status(err));

	/* count the number of algorithms */
	for (nvp = nvlist_next_nvpair(algsnvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(algsnvl, nvp)) {
		++count;
	}

	/*
	 * We allow only one alg to be added or removed at a time.
	 * Except on boot or service refresh, when cong_enabled is
	 * initialized to potentially multiple values.
	 */
	if (init) {
		flags = IPADM_OPT_ACTIVE | IPADM_OPT_APPEND;
	} else if (count != 1) {
		status = IPADM_INVALID_ARG;
		goto ret;
	}

	for (nvp = nvlist_next_nvpair(algsnvl, NULL);
	    nvp != NULL && status == IPADM_SUCCESS;
	    nvp = nvlist_next_nvpair(algsnvl, nvp)) {
		alg = nvpair_name(nvp);

		/*
		 * Change SMF service state before pushing value into kernel,
		 * and if that fails, change it back.
		 */
		if (!init)
			status = ipadm_cong_smf_set_state(alg, proto, flags);
		if (status == IPADM_SUCCESS) {
			status = i_ipadm_set_prop(iph, arg, pdp, alg, proto,
			    flags);
			if (status != IPADM_SUCCESS && !init)
				(void) ipadm_cong_smf_set_state(alg, proto,
				    flags ^ IPADM_OPT_APPEND);
		}
	}
ret:
	nvlist_free(algsnvl);
	return (status);
}

/* ARGSUSED */
static ipadm_status_t
i_ipadm_get_cong_enabled(ipadm_handle_t iph, const void *arg,
    ipadm_prop_desc_t *pdp, char *buf, uint_t *bufsize, uint_t proto,
    uint_t valtype)
{
	ipadm_status_t	status;

	switch (valtype) {
	case MOD_PROP_POSSIBLE:
		status = ipadm_cong_get_algs(buf, bufsize, proto, B_FALSE);
		break;
	case MOD_PROP_PERSIST:
		status = ipadm_cong_get_algs(buf, bufsize, proto, B_TRUE);
		break;
	case MOD_PROP_PERM:
	case MOD_PROP_DEFAULT:
		status = i_ipadm_get_prop(iph, arg, pdp, buf, bufsize, proto,
		    valtype);
		break;
	case MOD_PROP_ACTIVE:
		status = i_ipadm_get_prop(iph, arg, pdp, buf, bufsize, proto,
		    valtype);
		if (status == IPADM_SUCCESS)
			ipadm_cong_list_sort(buf);
		break;
	default:
		status = IPADM_INVALID_ARG;
	}
	return (status);
}

/* ARGSUSED */
static ipadm_status_t
i_ipadm_get_cong_default_range(ipadm_handle_t iph, const void *arg,
    ipadm_prop_desc_t *pdp, char *buf, uint_t *bufsize, uint_t proto,
    uint_t valtype)
{
	return (ipadm_cong_get_algs(buf, bufsize, proto, B_TRUE));
}

/* ARGSUSED */
static ipadm_status_t
i_ipadm_set_forwarding(ipadm_handle_t iph, const void *arg,
    ipadm_prop_desc_t *pdp, const void *pval, uint_t proto, uint_t flags)
{
	const char	*ifname = arg;
	ipadm_status_t	status;

	/*
	 * if interface name is provided, then set forwarding using the
	 * IFF_ROUTER flag
	 */
	if (ifname != NULL) {
		status = i_ipadm_set_ifprop_flags(iph, ifname, pdp, pval,
		    proto, flags);
	} else {
		char	*val = NULL;

		/*
		 * if the caller is IH_LEGACY, `pval' already contains
		 * numeric values.
		 */
		if (!(flags & IPADM_OPT_DEFAULT) &&
		    !(iph->ih_flags & IH_LEGACY)) {

			if (strcmp(pval, IPADM_ONSTR) == 0)
				val = "1";
			else if (strcmp(pval, IPADM_OFFSTR) == 0)
				val = "0";
			else
				return (IPADM_INVALID_ARG);
			pval = val;
		}

		status = i_ipadm_set_prop(iph, ifname, pdp, pval, proto, flags);
	}

	return (status);
}

/* ARGSUSED */
static ipadm_status_t
i_ipadm_set_ecnsack(ipadm_handle_t iph, const void *arg,
    ipadm_prop_desc_t *pdp, const void *pval, uint_t proto, uint_t flags)
{
	uint_t		i;
	char		val[MAXPROPVALLEN];

	/* if IH_LEGACY is set, `pval' already contains numeric values */
	if (!(flags & IPADM_OPT_DEFAULT) && !(iph->ih_flags & IH_LEGACY)) {
		for (i = 0; ecn_sack_vals[i] != NULL; i++) {
			if (strcmp(pval, ecn_sack_vals[i]) == 0)
				break;
		}
		if (ecn_sack_vals[i] == NULL)
			return (IPADM_INVALID_ARG);
		(void) snprintf(val, MAXPROPVALLEN, "%d", i);
		pval = val;
	}

	return (i_ipadm_set_prop(iph, arg, pdp, pval, proto, flags));
}

/* ARGSUSED */
ipadm_status_t
i_ipadm_get_ecnsack(ipadm_handle_t iph, const void *arg,
    ipadm_prop_desc_t *pdp, char *buf, uint_t *bufsize, uint_t proto,
    uint_t valtype)
{
	ipadm_status_t	status = IPADM_SUCCESS;
	uint_t		i, nbytes = 0, psize = *bufsize;

	switch (valtype) {
	case MOD_PROP_POSSIBLE: {
		char	*cp = buf;
		uint_t	tbytes = 0;

		for (i = 0; ecn_sack_vals[i] != NULL; i++) {
			if (i == 0)
				nbytes = snprintf(cp, psize, "%s",
				    ecn_sack_vals[i]);
			else
				nbytes = snprintf(cp, psize, ",%s",
				    ecn_sack_vals[i]);
			tbytes += nbytes;
			if (tbytes >= *bufsize) {
				/*
				 * insufficient buffer space, lets determine
				 * how much buffer is actually needed
				 */
				cp = NULL;
				psize = 0;
			} else {
				cp += nbytes;
				psize -= nbytes;
			}
		}
		*bufsize = tbytes;
		break;
	}
	case MOD_PROP_PERM:
	case MOD_PROP_DEFAULT:
	case MOD_PROP_ACTIVE:
		status = i_ipadm_get_prop(iph, arg, pdp, buf, bufsize, proto,
		    valtype);
		if (status != IPADM_SUCCESS)
			break;
		/* break if the provided buffer size is insufficient */
		if (*bufsize >= psize)
			break;
		/*
		 * If IH_LEGACY is set or valtype is MOD_PROP_PERM, do not
		 * convert the value returned from kernel.
		 */
		if ((iph->ih_flags & IH_LEGACY) || valtype == MOD_PROP_PERM)
			break;
		/*
		 * For current and default value, convert the value returned
		 * from kernel to more discrete representation.
		 */
		if (buf != NULL) {
			i = atoi(buf);
			assert(i < 3);
			*bufsize = snprintf(buf, psize, "%s", ecn_sack_vals[i]);
		}
		break;
	default:
		return (IPADM_INVALID_ARG);
	}
	return (status);
}

/* ARGSUSED */
static ipadm_status_t
i_ipadm_get_forwarding(ipadm_handle_t iph, const void *arg,
    ipadm_prop_desc_t *pdp, char *buf, uint_t *bufsize, uint_t proto,
    uint_t valtype)
{
	const char	*ifname = arg;
	ipadm_status_t	status = IPADM_SUCCESS;

	/*
	 * if interface name is provided, then get forwarding status using
	 * SIOCGLIFFLAGS
	 */
	if (ifname != NULL) {
		status = i_ipadm_get_ifprop_flags(iph, ifname, pdp,
		    buf, bufsize, pdp->ipd_proto, valtype);
	} else {
		uint_t	psize = *bufsize;

		status = i_ipadm_get_prop(iph, ifname, pdp, buf,
		    bufsize, proto, valtype);
		/*
		 * return if the provided buffer size is insufficient or
		 * if IH_LEGACY is set or valtype is MOD_PROP_PERM, do not
		 * convert the value returned from kernel.
		 */
		if (status != IPADM_SUCCESS || *bufsize >= psize ||
		    (iph->ih_flags & IH_LEGACY) || valtype == MOD_PROP_PERM)
			return (status);
		/*
		 * For current and default value, convert the value returned
		 * from kernel to more discrete representation.
		 */
		if (buf != NULL) {
			uint_t	val = atoi(buf);

			*bufsize = snprintf(buf, psize, "%s",
			    (val == 1 ? IPADM_ONSTR : IPADM_OFFSTR));
		}
	}
	return (status);
}

/* ARGSUSED */
static ipadm_status_t
i_ipadm_get_mtu(ipadm_handle_t iph, const void *arg,
    ipadm_prop_desc_t *pdp, char *buf, uint_t *bufsize, uint_t proto,
    uint_t valtype)
{
	struct lifreq	lifr;
	const char	*ifname = arg;
	int		s;

	switch (valtype) {
	case MOD_PROP_DEFAULT:
	case MOD_PROP_POSSIBLE:
		return (i_ipadm_get_prop(iph, arg, pdp, buf, bufsize,
		    proto, valtype));
	case MOD_PROP_ACTIVE:
		bzero(&lifr, sizeof (lifr));
		(void) strlcpy(lifr.lifr_name, ifname, sizeof (lifr.lifr_name));
		s = (proto == MOD_PROTO_IPV6 ? iph->ih_sock6 : iph->ih_sock);

		if (ioctl(s, SIOCGLIFMTU, (caddr_t)&lifr) < 0)
			return (ipadm_errno2status(errno));
		*bufsize = snprintf(buf, *bufsize, "%u", lifr.lifr_mtu);
		break;
	default:
		return (IPADM_INVALID_ARG);
	}
	return (IPADM_SUCCESS);
}

/* ARGSUSED */
static ipadm_status_t
i_ipadm_get_metric(ipadm_handle_t iph, const void *arg,
    ipadm_prop_desc_t *pdp, char *buf, uint_t *bufsize, uint_t proto,
    uint_t valtype)
{
	struct lifreq	lifr;
	const char	*ifname = arg;
	int		s, val;

	switch (valtype) {
	case MOD_PROP_DEFAULT:
		val = DEF_METRIC_VAL;
		break;
	case MOD_PROP_ACTIVE:
		bzero(&lifr, sizeof (lifr));
		(void) strlcpy(lifr.lifr_name, ifname, sizeof (lifr.lifr_name));

		s = (proto == MOD_PROTO_IPV6 ? iph->ih_sock6 : iph->ih_sock);
		if (ioctl(s, SIOCGLIFMETRIC, (caddr_t)&lifr) < 0)
			return (ipadm_errno2status(errno));
		val = lifr.lifr_metric;
		break;
	default:
		return (IPADM_INVALID_ARG);
	}
	*bufsize = snprintf(buf, *bufsize, "%d", val);
	return (IPADM_SUCCESS);
}

/* ARGSUSED */
static ipadm_status_t
i_ipadm_get_usesrc(ipadm_handle_t iph, const void *arg,
    ipadm_prop_desc_t *ipd, char *buf, uint_t *bufsize, uint_t proto,
    uint_t valtype)
{
	struct lifreq	lifr;
	const char	*ifname = arg;
	int		s;
	char 		if_name[IF_NAMESIZE];

	switch (valtype) {
	case MOD_PROP_DEFAULT:
		*bufsize = snprintf(buf, *bufsize, "%s", IPADM_NONESTR);
		break;
	case MOD_PROP_ACTIVE:
		bzero(&lifr, sizeof (lifr));
		(void) strlcpy(lifr.lifr_name, ifname, sizeof (lifr.lifr_name));

		s = (proto == MOD_PROTO_IPV6 ? iph->ih_sock6 : iph->ih_sock);
		if (ioctl(s, SIOCGLIFUSESRC, (caddr_t)&lifr) < 0)
			return (ipadm_errno2status(errno));
		if (lifr.lifr_index == 0) {
			/* no src address was set, so print 'none' */
			(void) strlcpy(if_name, IPADM_NONESTR,
			    sizeof (if_name));
		} else if (if_indextoname(lifr.lifr_index, if_name) == NULL) {
			return (ipadm_errno2status(errno));
		}
		*bufsize = snprintf(buf, *bufsize, "%s", if_name);
		break;
	default:
		return (IPADM_INVALID_ARG);
	}
	return (IPADM_SUCCESS);
}

/* ARGSUSED */
static ipadm_status_t
i_ipadm_get_ifprop_flags(ipadm_handle_t iph, const void *arg,
    ipadm_prop_desc_t *pdp, char *buf, uint_t *bufsize, uint_t proto,
    uint_t valtype)
{
	uint64_t 	intf_flags;
	char 		*val;
	const char	*ifname = arg;
	sa_family_t	af;
	ipadm_status_t	status = IPADM_SUCCESS;

	switch (valtype) {
	case MOD_PROP_DEFAULT:
		if (strcmp(pdp->ipd_name, "exchange_routes") == 0 ||
		    strcmp(pdp->ipd_name, "arp") == 0 ||
		    strcmp(pdp->ipd_name, "nud") == 0) {
			val = IPADM_ONSTR;
		} else if (strcmp(pdp->ipd_name, "forwarding") == 0 ||
		    strcmp(pdp->ipd_name, "standby") == 0) {
			val = IPADM_OFFSTR;
		} else {
			return (IPADM_PROP_UNKNOWN);
		}
		*bufsize = snprintf(buf, *bufsize, "%s", val);
		break;
	case MOD_PROP_ACTIVE:
		if (proto == MOD_PROTO_IP) {
			if (ipadm_if_enabled(iph, ifname, AF_INET))
				af = AF_INET;
			else
				af = AF_INET6;
		} else {
			af = (proto == MOD_PROTO_IPV6 ? AF_INET6 : AF_INET);
		}
		status = i_ipadm_get_flags(iph, ifname, af, &intf_flags);
		if (status != IPADM_SUCCESS)
			return (status);

		val = IPADM_OFFSTR;
		if (strcmp(pdp->ipd_name, "exchange_routes") == 0) {
			if (!(intf_flags & IFF_NORTEXCH))
				val = IPADM_ONSTR;
		} else if (strcmp(pdp->ipd_name, "forwarding") == 0) {
			if (intf_flags & IFF_ROUTER)
				val = IPADM_ONSTR;
		} else if (strcmp(pdp->ipd_name, "arp") == 0) {
			if (!(intf_flags & IFF_NOARP))
				val = IPADM_ONSTR;
		} else if (strcmp(pdp->ipd_name, "nud") == 0) {
			if (!(intf_flags & IFF_NONUD))
				val = IPADM_ONSTR;
		} else if (strcmp(pdp->ipd_name, "standby") == 0) {
			if (intf_flags & IFF_STANDBY)
				val = IPADM_ONSTR;
		}
		*bufsize = snprintf(buf, *bufsize, "%s", val);
		break;
	default:
		return (IPADM_INVALID_ARG);
	}
	return (status);
}

static void
i_ipadm_perm2str(char *buf, uint_t *bufsize)
{
	uint_t perm = atoi(buf);

	*bufsize = snprintf(buf, *bufsize, "%c%c",
	    ((perm & MOD_PROP_PERM_READ) != 0) ? 'r' : '-',
	    ((perm & MOD_PROP_PERM_WRITE) != 0) ? 'w' : '-');
}

/* ARGSUSED */
static ipadm_status_t
i_ipadm_get_prop(ipadm_handle_t iph, const void *arg,
    ipadm_prop_desc_t *pdp, char *buf, uint_t *bufsize, uint_t proto,
    uint_t valtype)
{
	ipadm_status_t	status = IPADM_SUCCESS;
	const char	*ifname = arg;
	mod_ioc_prop_t	*mip;
	char 		*pname = pdp->ipd_name;
	uint_t		iocsize;

	/* allocate sufficient ioctl buffer to retrieve value */
	iocsize = sizeof (mod_ioc_prop_t) + *bufsize - 1;
	if ((mip = calloc(1, iocsize)) == NULL)
		return (IPADM_NO_MEMORY);

	mip->mpr_version = MOD_PROP_VERSION;
	mip->mpr_flags = valtype;
	mip->mpr_proto = proto;
	if (ifname != NULL) {
		(void) strlcpy(mip->mpr_ifname, ifname,
		    sizeof (mip->mpr_ifname));
	}
	(void) strlcpy(mip->mpr_name, pname, sizeof (mip->mpr_name));
	mip->mpr_valsize = *bufsize;

	if (i_ipadm_strioctl(iph->ih_sock, SIOCGETPROP, (char *)mip,
	    iocsize) < 0) {
		if (errno == ENOENT)
			status = IPADM_PROP_UNKNOWN;
		else if (errno == ENOBUFS)
			*bufsize = mip->mpr_valsize;
		else
			status = ipadm_errno2status(errno);
	} else {
		assert(mip->mpr_valsize < *bufsize);
		bcopy(mip->mpr_val, buf, *bufsize);
		*bufsize = mip->mpr_valsize;
	}

	free(mip);
	return (status);
}

/*
 * Populates the ipmgmt_prop_arg_t based on the class of property.
 *
 * For private protocol properties, while persisting information in ipadm
 * data store, to ensure there is no collision of namespace between ipadm
 * private nvpair names (which also starts with '_', see ipadm_ipmgmt.h)
 * and private protocol property names, we will prepend IPADM_PRIV_PROP_PREFIX
 * to property names.
 */
static void
i_ipadm_populate_proparg(ipmgmt_prop_arg_t *pargp, ipadm_prop_desc_t *pdp,
    const char *pval, const void *object)
{
	const struct ipadm_addrobj_s *ipaddr;
	uint_t		class = pdp->ipd_class;
	uint_t		proto = pdp->ipd_proto;

	(void) strlcpy(pargp->ia_pname, pdp->ipd_name,
	    sizeof (pargp->ia_pname));
	if (pval != NULL)
		(void) strlcpy(pargp->ia_pval, pval, sizeof (pargp->ia_pval));

	switch (class) {
	case IPADMPROP_CLASS_MODULE:
		/* if it's a private property then add the prefix. */
		if (pdp->ipd_name[0] == '_') {
			(void) snprintf(pargp->ia_pname,
			    sizeof (pargp->ia_pname), "_%s", pdp->ipd_name);
		}
		(void) strlcpy(pargp->ia_module, object,
		    sizeof (pargp->ia_module));
		break;
	case IPADMPROP_CLASS_MODIF:
		/* check if object is protostr or an ifname */
		if (ipadm_str2proto(object) != MOD_PROTO_NONE) {
			(void) strlcpy(pargp->ia_module, object,
			    sizeof (pargp->ia_module));
			break;
		}
		/* it's an interface property, fall through */
		/* FALLTHRU */
	case IPADMPROP_CLASS_IF:
		(void) strlcpy(pargp->ia_ifname, object,
		    sizeof (pargp->ia_ifname));
		(void) strlcpy(pargp->ia_module, ipadm_proto2str(proto),
		    sizeof (pargp->ia_module));
		break;
	case IPADMPROP_CLASS_ADDR:
		ipaddr = object;
		(void) strlcpy(pargp->ia_ifname, ipaddr->ipadm_ifname,
		    sizeof (pargp->ia_ifname));
		(void) strlcpy(pargp->ia_aobjname, ipaddr->ipadm_aobjname,
		    sizeof (pargp->ia_aobjname));
		break;
	}
}

/*
 * Common function to retrieve property value for a given interface `ifname' or
 * for a given protocol `proto'. The property name is in `pname'.
 *
 * `valtype' determines the type of value that will be retrieved.
 * 	IPADM_OPT_ACTIVE -	current value of the property (active config)
 *	IPADM_OPT_PERSIST -	value of the property from persistent store
 *	IPADM_OPT_DEFAULT -	default hard coded value (boot-time value)
 *	IPADM_OPT_PERM -	read/write permissions for the value
 *	IPADM_OPT_POSSIBLE -	range of values
 *
 * The framework checks to see if the given `buf' of size `bufsize' is big
 * enough to fit the result. All the callback functions return IPADM_SUCCESS
 * regardless of whether it could fit everything into the buffer it was
 * provided. In cases where the buffer was insufficient, the callback
 * functions return the required size to fit the result via `psize'.
 */
ipadm_status_t
i_ipadm_getprop_common(ipadm_handle_t iph, const char *ifname,
    const char *pname, char *buf, uint_t *bufsize, uint_t proto,
    uint_t valtype)
{
	ipadm_status_t		status = IPADM_SUCCESS;
	ipadm_prop_desc_t	*pdp;
	char			priv_propname[MAXPROPNAMELEN];
	boolean_t		is_cong_priv = B_FALSE;
	boolean_t		is_if = (ifname != NULL);
	int			err = 0;
	uint_t			psize;
	char			*proto_str;
	char			*cong_alg = NULL;

	proto_str = ipadm_proto2str(proto);

	pdp = i_ipadm_get_prop_desc(pname, proto, &err);
	if (err == EPROTO)
		return (IPADM_BAD_PROTOCOL);
	/* there are no private interface properties */
	if (is_if && err == ENOENT)
		return (IPADM_PROP_UNKNOWN);

	if (pdp != NULL) {
		/*
		 * check whether the property can be
		 * applied on an interface
		 */
		if (is_if && !(pdp->ipd_class & IPADMPROP_CLASS_IF))
			return (IPADM_INVALID_ARG);
		/*
		 * check whether the property can be
		 * applied on a module
		 */
		if (!is_if && !(pdp->ipd_class & IPADMPROP_CLASS_MODULE))
			return (IPADM_INVALID_ARG);

	} else {
		/* private protocol properties, pass it to kernel directly */
		pdp = &ipadm_privprop;
		(void) strlcpy(priv_propname, pname, sizeof (priv_propname));
		pdp->ipd_name = priv_propname;

		is_cong_priv = ipadm_cong_is_privprop(priv_propname, &cong_alg);
	}

	psize = *bufsize;
	switch (valtype) {
	case IPADM_OPT_PERM:
		if (is_if) {
			status = i_ipadm_pd2permstr(pdp, buf, &psize);
		} else {
			status = pdp->ipd_get(iph, ifname, pdp, buf, &psize,
			    proto, MOD_PROP_PERM);
			if (status == IPADM_SUCCESS && psize < *bufsize) {
				psize = *bufsize;
				i_ipadm_perm2str(buf, &psize);
			}
		}
		break;
	case IPADM_OPT_ACTIVE:
		status = pdp->ipd_get(iph, ifname, pdp, buf, &psize, proto,
		    MOD_PROP_ACTIVE);
		break;
	case IPADM_OPT_DEFAULT:
		status = pdp->ipd_get(iph, ifname, pdp, buf, &psize, proto,
		    MOD_PROP_DEFAULT);
		break;
	case IPADM_OPT_POSSIBLE:
		if (pdp->ipd_get_range != NULL) {
			status = pdp->ipd_get_range(iph, ifname, pdp, buf,
			    &psize, proto, MOD_PROP_POSSIBLE);
			break;
		}
		buf[0] = '\0';
		psize = 1;
		break;
	case IPADM_OPT_PERSIST:
		/*
		 * Retrieve from database, except for congestion
		 * control private properties, which are stored in SMF.
		 */
		if (pdp->ipd_flags & IPADMPROP_GETPERSIST) {
			status = pdp->ipd_get(iph, ifname, pdp, buf, &psize,
			    proto, MOD_PROP_PERSIST);
		} else {
			if (is_if) {
				status = i_ipadm_get_persist_propval(iph, pdp,
				    buf, &psize, ifname);
			} else if (!is_cong_priv) {
				status = i_ipadm_get_persist_propval(iph, pdp,
				    buf, &psize, ipadm_proto2str(proto));
			} else {
				assert(cong_alg != NULL);
				status =
				    ipadm_cong_get_persist_propval(cong_alg,
				    priv_propname, buf, &psize, proto_str);
			}
		}
		break;
	default:
		free(cong_alg);
		return (IPADM_INVALID_ARG);
	}
	free(cong_alg);

	/*
	 * Check whether the provided buffer was of sufficient size to
	 * hold the property value.
	 */
	if (status == IPADM_SUCCESS && psize >= *bufsize) {
		*bufsize = psize + 1;
		status = IPADM_NO_BUFS;
	}
	return (status);
}

/*
 * Get protocol property of the specified protocol.
 */
ipadm_status_t
ipadm_get_prop(ipadm_handle_t iph, const char *pname, char *buf,
    uint_t *bufsize, uint_t proto, uint_t valtype)
{
	/*
	 * Validate the arguments of the function. Allow `buf' to be NULL
	 * only when `*bufsize' is zero as it can be used by the callers to
	 * determine the actual buffer size required.
	 */
	if (iph == NULL || pname == NULL || bufsize == NULL ||
	    (buf == NULL && *bufsize != 0)) {
		return (IPADM_INVALID_ARG);
	}
	/*
	 * Do we support this proto, if not return error.
	 */
	if (ipadm_proto2str(proto) == NULL)
		return (IPADM_OP_NOTSUP);

	return (i_ipadm_getprop_common(iph, NULL, pname, buf, bufsize,
	    proto, valtype));
}

/*
 * Get interface property of the specified interface.
 */
ipadm_status_t
ipadm_get_ifprop(ipadm_handle_t iph, const char *ifname, const char *pname,
    char *buf, uint_t *bufsize, uint_t proto, uint_t valtype)
{
	/*
	 * Validate the arguments of the function. Allow `buf' to be NULL
	 * only when `*bufsize' is zero as it can be used by the callers to
	 * determine the actual buffer size required.
	 */
	if (iph == NULL || pname == NULL || bufsize == NULL ||
	    (buf == NULL && *bufsize != 0)) {
		return (IPADM_INVALID_ARG);
	}

	/* Do we support this proto, if not return error. */
	if (ipadm_proto2str(proto) == NULL)
		return (IPADM_OP_NOTSUP);

	/*
	 * check if interface name is provided for interface property and
	 * is valid.
	 */
	if (!i_ipadm_validate_ifname(iph, ifname))
		return (IPADM_INVALID_IFNAME);

	return (i_ipadm_getprop_common(iph, ifname, pname, buf, bufsize,
	    proto, valtype));
}

/*
 * Allocates sufficient ioctl buffers and copies property name and the
 * value, among other things. If the flag IPADM_OPT_DEFAULT is set, then
 * `pval' will be NULL and it instructs the kernel to reset the current
 * value to property's default value.
 */
static ipadm_status_t
i_ipadm_set_prop(ipadm_handle_t iph, const void *arg,
    ipadm_prop_desc_t *pdp, const void *pval, uint_t proto, uint_t flags)
{
	ipadm_status_t	status = IPADM_SUCCESS;
	const char	*ifname = arg;
	mod_ioc_prop_t 	*mip;
	char 		*pname = pdp->ipd_name;
	uint_t 		valsize, iocsize;
	uint_t		iocflags = 0;

	if (flags & IPADM_OPT_DEFAULT) {
		iocflags |= MOD_PROP_DEFAULT;
	} else if (flags & IPADM_OPT_ACTIVE) {
		iocflags |= MOD_PROP_ACTIVE;
		if (flags & IPADM_OPT_APPEND)
			iocflags |= MOD_PROP_APPEND;
		else if (flags & IPADM_OPT_REMOVE)
			iocflags |= MOD_PROP_REMOVE;
	}

	if (pval != NULL) {
		valsize = strlen(pval);
		iocsize = sizeof (mod_ioc_prop_t) + valsize - 1;
	} else {
		valsize = 0;
		iocsize = sizeof (mod_ioc_prop_t);
	}

	if ((mip = calloc(1, iocsize)) == NULL)
		return (IPADM_NO_MEMORY);

	mip->mpr_version = MOD_PROP_VERSION;
	mip->mpr_flags = iocflags;
	mip->mpr_proto = proto;
	if (ifname != NULL) {
		(void) strlcpy(mip->mpr_ifname, ifname,
		    sizeof (mip->mpr_ifname));
	}

	(void) strlcpy(mip->mpr_name, pname, sizeof (mip->mpr_name));
	mip->mpr_valsize = valsize;
	if (pval != NULL)
		bcopy(pval, mip->mpr_val, valsize);

	if (i_ipadm_strioctl(iph->ih_sock, SIOCSETPROP, (char *)mip,
	    iocsize) < 0) {
		if (errno == ENOENT)
			status = IPADM_PROP_UNKNOWN;
		else
			status = ipadm_errno2status(errno);
	}
	free(mip);
	return (status);
}

/*
 * Common function for modifying both protocol/interface property.
 *
 * If:
 *   IPADM_OPT_PERSIST is set then the value is persisted.
 *   IPADM_OPT_DEFAULT is set then the default value for the property will
 *		       be applied.
 */
static ipadm_status_t
i_ipadm_setprop_common(ipadm_handle_t iph, const char *ifname,
    const char *pname, const char *buf, uint_t proto, uint_t pflags)
{
	ipadm_status_t		status = IPADM_SUCCESS;
	boolean_t 		persist = (pflags & IPADM_OPT_PERSIST);
	boolean_t		reset = (pflags & IPADM_OPT_DEFAULT);
	ipadm_prop_desc_t	*pdp;
	boolean_t		is_if = (ifname != NULL);
	char			priv_propname[MAXPROPNAMELEN];
	boolean_t		is_cong_priv = B_FALSE;
	int			err = 0;
	char			*proto_str;
	char			*cong_alg = NULL;

	/* Check that property value is within the allowed size */
	if (!reset && strnlen(buf, MAXPROPVALLEN) >= MAXPROPVALLEN)
		return (IPADM_INVALID_ARG);

	proto_str = ipadm_proto2str(proto);

	pdp = i_ipadm_get_prop_desc(pname, proto, &err);
	if (err == EPROTO)
		return (IPADM_BAD_PROTOCOL);
	/* there are no private interface properties */
	if (is_if && err == ENOENT)
		return (IPADM_PROP_UNKNOWN);

	if (pdp != NULL) {
		/* do some sanity checks */
		if (is_if) {
			if (!(pdp->ipd_class & IPADMPROP_CLASS_IF))
				return (IPADM_INVALID_ARG);
		} else {
			if (!(pdp->ipd_class & IPADMPROP_CLASS_MODULE))
				return (IPADM_INVALID_ARG);
		}
		/*
		 * if the property is not multi-valued and IPADM_OPT_APPEND or
		 * IPADM_OPT_REMOVE is specified, return IPADM_INVALID_ARG.
		 */
		if (!(pdp->ipd_flags & IPADMPROP_MULVAL) && (pflags &
		    (IPADM_OPT_APPEND|IPADM_OPT_REMOVE))) {
			return (IPADM_INVALID_ARG);
		}
		if (pdp->ipd_flags & IPADMPROP_SMFPERSIST)
			persist = B_FALSE;
	} else {
		/* private protocol property, pass it to kernel directly */
		pdp = &ipadm_privprop;
		(void) strlcpy(priv_propname, pname, sizeof (priv_propname));
		pdp->ipd_name = priv_propname;

		is_cong_priv = ipadm_cong_is_privprop(priv_propname, &cong_alg);
	}

	/*
	 * if we were asked to just persist the value, do not apply to
	 * current configuration.
	 */
	if (pflags != IPADM_OPT_PERSIST) {
		status = pdp->ipd_set(iph, ifname, pdp, buf, proto, pflags);
		if (status != IPADM_SUCCESS) {
			free(cong_alg);
			return (status);
		}
	}

	if (persist) {
		/*
		 * Store in the common database, except for congestion
		 * control private properties, which are stored in SMF.
		 */
		if (is_if) {
			status = i_ipadm_persist_propval(iph, pdp, buf, ifname,
			    pflags);
		} else if (!is_cong_priv) {
			status = i_ipadm_persist_propval(iph, pdp, buf,
			    proto_str, pflags);
		} else {
			assert(cong_alg != NULL);
			status = ipadm_cong_persist_propval(cong_alg,
			    priv_propname, buf, proto_str, pflags);
		}
	}
	free(cong_alg);
	return (status);
}

/*
 * Sets the property value of the specified interface
 */
ipadm_status_t
ipadm_set_ifprop(ipadm_handle_t iph, const char *ifname, const char *pname,
    const char *buf, uint_t proto, uint_t pflags)
{
	boolean_t	reset = (pflags & IPADM_OPT_DEFAULT);
	ipadm_status_t	status;

	/* check for solaris.network.interface.config authorization */
	if (!ipadm_check_auth())
		return (IPADM_INSUFF_AUTH);
	/*
	 * validate the arguments of the function.
	 */
	if (iph == NULL || pname == NULL || (!reset && buf == NULL) ||
	    pflags == 0 || pflags == IPADM_OPT_PERSIST ||
	    (pflags & ~(IPADM_COMMON_OPT_MASK|IPADM_OPT_DEFAULT))) {
		return (IPADM_INVALID_ARG);
	}

	/*
	 * Do we support this protocol, if not return error.
	 */
	if (ipadm_proto2str(proto) == NULL)
		return (IPADM_OP_NOTSUP);

	/*
	 * Validate the interface and check if a persistent
	 * operation is performed on a temporary object.
	 */
	status = i_ipadm_validate_if(iph, ifname, proto, pflags);
	if (status != IPADM_SUCCESS)
		return (status);

	return (i_ipadm_setprop_common(iph, ifname, pname, buf, proto, pflags));
}

/*
 * Sets the property value of the specified protocol.
 */
ipadm_status_t
ipadm_set_prop(ipadm_handle_t iph, const char *pname, const char *buf,
    uint_t proto, uint_t pflags)
{
	boolean_t	reset = (pflags & IPADM_OPT_DEFAULT);

	/* check for solaris.network.interface.config authorization */
	if (!ipadm_check_auth())
		return (IPADM_INSUFF_AUTH);
	/*
	 * validate the arguments of the function.
	 */
	if (iph == NULL || pname == NULL ||(!reset && buf == NULL) ||
	    pflags == 0 || (pflags & ~(IPADM_COMMON_OPT_MASK|IPADM_OPT_DEFAULT|
	    IPADM_OPT_APPEND|IPADM_OPT_REMOVE))) {
		return (IPADM_INVALID_ARG);
	}

	/*
	 * Do we support this proto, if not return error.
	 */
	if (ipadm_proto2str(proto) == NULL)
		return (IPADM_OP_NOTSUP);

	return (i_ipadm_setprop_common(iph, NULL, pname, buf, proto,
	    pflags));
}

/* helper function for ipadm_walk_proptbl */
static void
i_ipadm_walk_proptbl(ipadm_prop_desc_t *pdtbl, uint_t proto, uint_t class,
    ipadm_prop_wfunc_t *func, void *arg)
{
	ipadm_prop_desc_t	*pdp;

	for (pdp = pdtbl; pdp->ipd_name != NULL; pdp++) {
		if (!(pdp->ipd_class & class))
			continue;

		if (proto != MOD_PROTO_NONE && !(pdp->ipd_proto & proto))
			continue;

		/*
		 * we found a class specific match, call the
		 * user callback function.
		 */
		if (func(arg, pdp->ipd_name, pdp->ipd_proto) == B_FALSE)
			break;
	}
}

/*
 * Walks through all the properties, for a given protocol and property class
 * (protocol or interface).
 *
 * Further if proto == MOD_PROTO_NONE, then it walks through all the supported
 * protocol property tables.
 */
ipadm_status_t
ipadm_walk_proptbl(uint_t proto, uint_t class, ipadm_prop_wfunc_t *func,
    void *arg)
{
	ipadm_prop_desc_t	*pdtbl;
	ipadm_status_t		status = IPADM_SUCCESS;
	int			i;
	int			count = A_CNT(protocols);

	if (func == NULL)
		return (IPADM_INVALID_ARG);

	switch (class) {
	case IPADMPROP_CLASS_ADDR:
		pdtbl = ipadm_addrprop_table;
		break;
	case IPADMPROP_CLASS_IF:
	case IPADMPROP_CLASS_MODULE:
		pdtbl = i_ipadm_get_propdesc_table(proto);
		if (pdtbl == NULL && proto != MOD_PROTO_NONE)
			return (IPADM_INVALID_ARG);
		break;
	default:
		return (IPADM_INVALID_ARG);
	}

	if (pdtbl != NULL) {
		/*
		 * proto will be MOD_PROTO_NONE in the case of
		 * IPADMPROP_CLASS_ADDR.
		 */
		i_ipadm_walk_proptbl(pdtbl, proto, class, func, arg);
	} else {
		/* Walk thru all the protocol tables, we support */
		for (i = 0; i < count; i++) {
			pdtbl = i_ipadm_get_propdesc_table(protocols[i]);
			i_ipadm_walk_proptbl(pdtbl, protocols[i], class, func,
			    arg);
		}
	}
	return (status);
}

/*
 * Given a property name, walks through all the instances of a property name.
 * Some properties have two instances one for v4 interfaces and another for v6
 * interfaces. For example: MTU. MTU can have different values for v4 and v6.
 * Therefore there are two properties for 'MTU'.
 *
 * This function invokes `func' for every instance of property `pname'
 */
ipadm_status_t
ipadm_walk_prop(const char *pname, uint_t proto, uint_t class,
    ipadm_prop_wfunc_t *func, void *arg)
{
	ipadm_prop_desc_t	*pdtbl, *pdp;
	ipadm_status_t		status = IPADM_SUCCESS;
	boolean_t		matched = B_FALSE;

	if (pname == NULL || func == NULL)
		return (IPADM_INVALID_ARG);

	switch (class) {
	case IPADMPROP_CLASS_ADDR:
		pdtbl = ipadm_addrprop_table;
		break;
	case IPADMPROP_CLASS_IF:
	case IPADMPROP_CLASS_MODULE:
		pdtbl = i_ipadm_get_propdesc_table(proto);
		break;
	default:
		return (IPADM_INVALID_ARG);
	}

	if (pdtbl == NULL)
		return (IPADM_INVALID_ARG);

	for (pdp = pdtbl; pdp->ipd_name != NULL; pdp++) {
		if (strcmp(pname, pdp->ipd_name) != 0)
			continue;
		if (!(pdp->ipd_proto & proto))
			continue;
		matched = B_TRUE;
		/* we found a match, call the callback function */
		if (func(arg, pdp->ipd_name, pdp->ipd_proto) == B_FALSE)
			break;
	}
	if (!matched)
		status = IPADM_PROP_UNKNOWN;
	return (status);
}

/* ARGSUSED */
ipadm_status_t
i_ipadm_get_onoff(ipadm_handle_t iph, const void *arg, ipadm_prop_desc_t *dp,
    char *buf, uint_t *bufsize, uint_t proto, uint_t valtype)
{
	*bufsize = snprintf(buf, *bufsize, "%s,%s", IPADM_ONSTR, IPADM_OFFSTR);
	return (IPADM_SUCCESS);
}

/*
 * Makes a door call to ipmgmtd to retrieve the persisted property value
 */
ipadm_status_t
i_ipadm_get_persist_propval(ipadm_handle_t iph, ipadm_prop_desc_t *pdp,
    char *gbuf, uint_t *gbufsize, const void *object)
{
	ipmgmt_prop_arg_t	parg;
	ipmgmt_getprop_rval_t	rval;
	int			err = 0;

	bzero(&parg, sizeof (parg));
	parg.ia_cmd = IPMGMT_CMD_GETPROP;
	i_ipadm_populate_proparg(&parg, pdp, NULL, object);

	err = ipadm_door_call(iph, &parg, sizeof (parg), &rval, sizeof (rval));
	/* `ir_pval' contains the property value */
	if (err == 0)
		*gbufsize = snprintf(gbuf, *gbufsize, "%s", rval.ir_pval);
	return (ipadm_errno2status(err));
}

/*
 * Persists the property value for a given property in the data store
 */
ipadm_status_t
i_ipadm_persist_propval(ipadm_handle_t iph, ipadm_prop_desc_t *pdp,
    const char *pval, const void *object, uint_t flags)
{
	ipmgmt_prop_arg_t	parg;
	int			err = 0;

	bzero(&parg, sizeof (parg));
	i_ipadm_populate_proparg(&parg, pdp, pval, object);
	/*
	 * Check if value to be persisted need to be appended or removed. This
	 * is required for multi-valued property.
	 */
	if (flags & IPADM_OPT_APPEND)
		parg.ia_flags |= IPMGMT_APPEND;
	if (flags & IPADM_OPT_REMOVE)
		parg.ia_flags |= IPMGMT_REMOVE;

	if (flags & (IPADM_OPT_DEFAULT|IPADM_OPT_REMOVE))
		parg.ia_cmd = IPMGMT_CMD_RESETPROP;
	else
		parg.ia_cmd = IPMGMT_CMD_SETPROP;

	err = ipadm_door_call(iph, &parg, sizeof (parg), NULL, 0);

	/*
	 * its fine if there were no entry in the DB to delete. The user
	 * might be changing property value, which was not changed
	 * persistently.
	 */
	if (err == ENOENT)
		err = 0;
	return (ipadm_errno2status(err));
}

/*
 * This is called from ipadm_set_ifprop() to validate the set operation.
 * It does the following steps:
 * 1. Validates the interface name.
 * 2. In case of a persistent operation, verifies that the
 *	interface is persistent.
 */
static ipadm_status_t
i_ipadm_validate_if(ipadm_handle_t iph, const char *ifname,
    uint_t proto, uint_t flags)
{
	sa_family_t	af = AF_UNSPEC;
	boolean_t	p_exists;
	boolean_t	a_exists;

	/* Check if the interface name is valid. */
	if (!i_ipadm_validate_ifname(iph, ifname))
		return (IPADM_INVALID_IFNAME);

	if (proto != MOD_PROTO_IP)
		af = (proto == MOD_PROTO_IPV6 ? AF_INET6 : AF_INET);

	p_exists = i_ipadm_if_pexists(iph, ifname, af);
	a_exists = ipadm_if_enabled(iph, ifname, AF_UNSPEC);
	if (!a_exists && p_exists)
		return (IPADM_OP_DISABLE_OBJ);
	if (!ipadm_if_enabled(iph, ifname, af))
		return (IPADM_NOSUCH_IF);

	/*
	 * If a persistent operation is requested, check if the underlying
	 * IP interface is persistent.
	 */
	if ((flags & IPADM_OPT_PERSIST) && !p_exists)
		return (IPADM_TEMPORARY_OBJ);
	return (IPADM_SUCCESS);
}

/*
 * Private protocol properties namespace scheme:
 *
 * PSARC 2010/080 identified the private protocol property names to be the
 * leading protocol names. For e.g. tcp_strong_iss, ip_strict_src_multihoming,
 * et al,. However to be consistent with private data-link property names,
 * which starts with '_', private protocol property names will start with '_'.
 * For e.g. _strong_iss, _strict_src_multihoming, et al,.
 */

/* maps new private protocol property name to the old private property name */
typedef struct ipadm_oname2nname_map {
	char	*iom_oname;
	char	*iom_nname;
	uint_t	iom_proto;
} ipadm_oname2nname_map_t;

/*
 * IP is a special case. It isn't straight forward to derive the legacy name
 * from the new name and vice versa. No set standard was followed in naming
 * the properties and hence we need a table to capture the mapping.
 */
static ipadm_oname2nname_map_t ip_name_map[] = {
	{ "arp_probe_delay",		"_arp_probe_delay",
	    MOD_PROTO_IP },
	{ "arp_fastprobe_delay",	"_arp_fastprobe_delay",
	    MOD_PROTO_IP },
	{ "arp_probe_interval",		"_arp_probe_interval",
	    MOD_PROTO_IP },
	{ "arp_fastprobe_interval",	"_arp_fastprobe_interval",
	    MOD_PROTO_IP },
	{ "arp_probe_count",		"_arp_probe_count",
	    MOD_PROTO_IP },
	{ "arp_fastprobe_count",	"_arp_fastprobe_count",
	    MOD_PROTO_IP },
	{ "arp_defend_interval",	"_arp_defend_interval",
	    MOD_PROTO_IP },
	{ "arp_defend_rate",		"_arp_defend_rate",
	    MOD_PROTO_IP },
	{ "arp_defend_period",		"_arp_defend_period",
	    MOD_PROTO_IP },
	{ "ndp_defend_interval",	"_ndp_defend_interval",
	    MOD_PROTO_IP },
	{ "ndp_defend_rate",		"_ndp_defend_rate",
	    MOD_PROTO_IP },
	{ "ndp_defend_period",		"_ndp_defend_period",
	    MOD_PROTO_IP },
	{ "igmp_max_version",		"_igmp_max_version",
	    MOD_PROTO_IP },
	{ "mld_max_version",		"_mld_max_version",
	    MOD_PROTO_IP },
	{ "ipsec_override_persocket_policy", "_ipsec_override_persocket_policy",
	    MOD_PROTO_IP },
	{ "ipsec_policy_log_interval",	"_ipsec_policy_log_interval",
	    MOD_PROTO_IP },
	{ "icmp_accept_clear_messages",	"_icmp_accept_clear_messages",
	    MOD_PROTO_IP },
	{ "igmp_accept_clear_messages",	"_igmp_accept_clear_messages",
	    MOD_PROTO_IP },
	{ "pim_accept_clear_messages",	"_pim_accept_clear_messages",
	    MOD_PROTO_IP },
	{ "ip_respond_to_echo_multicast", "_respond_to_echo_multicast",
	    MOD_PROTO_IPV4 },
	{ "ip_send_redirects",		"_send_redirects",
	    MOD_PROTO_IPV4 },
	{ "ip_forward_src_routed",	"_forward_src_routed",
	    MOD_PROTO_IPV4 },
	{ "ip_icmp_return_data_bytes",	"_icmp_return_data_bytes",
	    MOD_PROTO_IPV4 },
	{ "ip_ignore_redirect",		"_ignore_redirect",
	    MOD_PROTO_IPV4 },
	{ "ip_strict_dst_multihoming",	"_strict_dst_multihoming",
	    MOD_PROTO_IPV4 },
	{ "ip_reasm_timeout",		"_reasm_timeout",
	    MOD_PROTO_IPV4 },
	{ "ip_strict_src_multihoming",	"_strict_src_multihoming",
	    MOD_PROTO_IPV4 },
	{ "ipv4_dad_announce_interval",	"_dad_announce_interval",
	    MOD_PROTO_IPV4 },
	{ "ipv4_icmp_return_pmtu",	"_icmp_return_pmtu",
	    MOD_PROTO_IPV4 },
	{ "ipv6_dad_announce_interval",	"_dad_announce_interval",
	    MOD_PROTO_IPV6 },
	{ "ipv6_icmp_return_pmtu",	"_icmp_return_pmtu",
	    MOD_PROTO_IPV6 },
	{ NULL, NULL, MOD_PROTO_NONE }
};

static ipadm_oname2nname_map_t tcp_name_map[] = {
	{ "send_maxbuf", "send_buf", MOD_PROTO_TCP },
	{ "recv_maxbuf", "recv_buf", MOD_PROTO_TCP },
	{ "tcp_max_buf", "max_buf", MOD_PROTO_TCP },
	{ "_max_buf",	"max_buf", MOD_PROTO_TCP },
	{ NULL, NULL, MOD_PROTO_NONE }
};

static ipadm_oname2nname_map_t udp_name_map[] = {
	{ "send_maxbuf", "send_buf", MOD_PROTO_UDP },
	{ "recv_maxbuf", "recv_buf", MOD_PROTO_UDP },
	{ "udp_max_buf", "max_buf", MOD_PROTO_UDP },
	{ "_max_buf",	"max_buf", MOD_PROTO_UDP },
	{ NULL, NULL, MOD_PROTO_NONE }
};

static ipadm_oname2nname_map_t sctp_name_map[] = {
	{ "send_maxbuf", "send_buf", MOD_PROTO_SCTP },
	{ "recv_maxbuf", "recv_buf", MOD_PROTO_SCTP },
	{ "sctp_max_buf", "max_buf", MOD_PROTO_SCTP },
	{ "_max_buf",	"max_buf", MOD_PROTO_SCTP },
	{ NULL, NULL, MOD_PROTO_NONE }
};

static ipadm_oname2nname_map_t rawip_name_map[] = {
	{ "send_maxbuf", "send_buf", MOD_PROTO_RAWIP },
	{ "recv_maxbuf", "recv_buf", MOD_PROTO_RAWIP },
	{ "icmp_max_buf", "max_buf", MOD_PROTO_RAWIP },
	{ "_max_buf",	"max_buf", MOD_PROTO_RAWIP },
	{ NULL, NULL, MOD_PROTO_NONE }
};

static ipadm_oname2nname_map_t *
i_ipadm_get_name_map(ipadm_oname2nname_map_t *name_map, const char *oname)
{
	ipadm_oname2nname_map_t *ionmp;

	for (ionmp = name_map; ionmp->iom_oname != NULL; ionmp++) {
		if (strcmp(oname, ionmp->iom_oname) == 0)
			return (ionmp);
	}
	return (NULL);
}

/*
 * Following API returns a new property name in `nname' for the given legacy
 * property name in `oname'.
 */
int
ipadm_legacy2new_propname(const char *oname, char *nname, uint_t nnamelen,
    uint_t *proto)
{
	const char	*str = NULL;
	ipadm_oname2nname_map_t *ionmp = NULL;

	/* if it's a public property, there is nothing to return */
	if (i_ipadm_get_prop_desc(oname, *proto, NULL) != NULL)
		return (-1);

	/*
	 * we didn't find the `oname' in the table, check if the property
	 * name begins with a leading protocol.
	 */
	str = oname;
	switch (*proto) {
	case MOD_PROTO_TCP:
		ionmp = i_ipadm_get_name_map(tcp_name_map, oname);
		if (ionmp == NULL && strstr(oname, "tcp_") == oname)
			str += strlen("tcp");
		break;
	case MOD_PROTO_SCTP:
		ionmp = i_ipadm_get_name_map(sctp_name_map, oname);
		if (ionmp == NULL && strstr(oname, "sctp_") == oname)
			str += strlen("sctp");
		break;
	case MOD_PROTO_UDP:
		ionmp = i_ipadm_get_name_map(udp_name_map, oname);
		if (ionmp == NULL && strstr(oname, "udp_") == oname)
			str += strlen("udp");
		break;
	case MOD_PROTO_RAWIP:
		ionmp = i_ipadm_get_name_map(rawip_name_map, oname);
		if (ionmp == NULL && strstr(oname, "icmp_") == oname)
			str += strlen("icmp");
		break;
	case MOD_PROTO_IP:
	case MOD_PROTO_IPV4:
	case MOD_PROTO_IPV6:
		if (strstr(oname, "ip6_") == oname) {
			*proto = MOD_PROTO_IPV6;
			str += strlen("ip6");
		} else {
			ionmp = i_ipadm_get_name_map(ip_name_map, oname);
			if (ionmp == NULL && strstr(oname, "ip_") == oname) {
				*proto = MOD_PROTO_IP;
				str += strlen("ip");
			}
		}
		break;
	default:
		return (-1);
	}
	if (ionmp != NULL) {
		str = ionmp->iom_nname;
		*proto = ionmp->iom_proto;
	}

	(void) snprintf(nname, nnamelen, "%s", str);

	/* if newname and the oldname are different, return 0 */
	return (strcmp(nname, oname) != 0 ? 0 : -1);
}

/*
 * Following API is required for ndd.c alone. To maintain backward
 * compatibility with ndd output, we need to print the legacy name
 * for the new name.
 */
int
ipadm_new2legacy_propname(const char *oname, char *nname,
    uint_t nnamelen, uint_t proto)
{
	char	*prefix;
	ipadm_oname2nname_map_t *ionmp;

	/* if it's a public property, there is nothing to prepend */
	if (i_ipadm_get_prop_desc(oname, proto, NULL) != NULL)
		return (-1);

	switch (proto) {
	case MOD_PROTO_TCP:
		prefix = "tcp";
		break;
	case MOD_PROTO_SCTP:
		prefix = "sctp";
		break;
	case MOD_PROTO_UDP:
		prefix = "udp";
		break;
	case MOD_PROTO_RAWIP:
		prefix = "icmp";
		break;
	case MOD_PROTO_IP:
	case MOD_PROTO_IPV4:
	case MOD_PROTO_IPV6:
		/* handle special case for IP */
		for (ionmp = ip_name_map; ionmp->iom_oname != NULL; ionmp++) {
			if (strcmp(oname, ionmp->iom_nname) == 0 &&
			    ionmp->iom_proto == proto) {
				(void) strlcpy(nname, ionmp->iom_oname,
				    nnamelen);
				return (0);
			}
		}
		if (proto == MOD_PROTO_IPV6)
			prefix = "ip6";
		else
			prefix = "ip";
		break;
	default:
		return (-1);
	}
	(void) snprintf(nname, nnamelen, "%s%s", prefix, oname);
	return (0);
}
