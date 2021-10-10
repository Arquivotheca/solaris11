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
 * This file contains functions for address management such as creating
 * an address, deleting an address, enabling an address, disabling an
 * address, bringing an address down or up, setting/getting properties
 * on an address object and listing address information
 * for all addresses in active as well as persistent configuration.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <inet/ip.h>
#include <string.h>
#include <strings.h>
#include <assert.h>
#include <sys/sockio.h>
#include <errno.h>
#include <unistd.h>
#include <stropts.h>
#include <zone.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <ctype.h>
#include <libdlpi.h>
#include <dhcp_inittab.h>
#include <dhcpagent_util.h>
#include <dhcpagent_ipc.h>
#include <ipadm_ndpd.h>
#include <libdladm.h>
#include <libdllink.h>
#include <libdliptun.h>
#include <ifaddrs.h>
#include "libipadm_impl.h"

#define	SIN6(a)		((struct sockaddr_in6 *)a)
#define	SIN(a)		((struct sockaddr_in *)a)

#define	TOKEN_PREFIXLEN	64

static ipadm_status_t	i_ipadm_create_addr(ipadm_handle_t, ipadm_addrobj_t,
			    uint32_t);
static ipadm_status_t	i_ipadm_create_dhcp(ipadm_handle_t, ipadm_addrobj_t,
			    uint32_t);
static ipadm_status_t	i_ipadm_delete_dhcp(ipadm_handle_t, ipadm_addrobj_t,
			    boolean_t);
static ipadm_status_t	i_ipadm_get_db_addr(ipadm_handle_t, const char *,
			    const char *, nvlist_t **);
static ipadm_status_t	i_ipadm_op_dhcp(ipadm_addrobj_t, dhcp_ipc_type_t,
			    int *, dhcp_ipc_reply_t **);
static ipadm_status_t	i_ipadm_validate_create_addr(ipadm_handle_t,
			    ipadm_addrobj_t, uint32_t);
static ipadm_status_t	i_ipadm_addr_persist_nvl(ipadm_handle_t, nvlist_t *,
			    uint32_t);
static ipadm_status_t	i_ipadm_get_default_prefixlen(struct sockaddr_storage *,
			    uint32_t *);
static ipadm_status_t	i_ipadm_get_static_addr_db(ipadm_handle_t,
			    ipadm_addrobj_t, boolean_t *);
static ipadm_status_t	i_ipadm_get_addrconf_db(ipadm_handle_t,
			    ipadm_addrobj_t);
static ipadm_status_t	i_ipadm_nvl2addrconf_addrobj(nvlist_t *,
			    ipadm_addrobj_t);
static boolean_t	i_ipadm_is_user_aobjname_valid(const char *);
static ipadm_status_t	i_ipadm_up_addr_underif(ipadm_handle_t,
			    ipadm_addrobj_t, uint64_t);
static ipadm_status_t	i_ipadm_ipmpif(ipadm_handle_t, const char *, char *,
			    size_t);
static ipadm_status_t	i_ipadm_get_dhcp_info(ipadm_addrobj_t,
			    ipadm_addr_info_t *);
static ipadm_status_t	i_ipadm_parse_legacy_cid(uchar_t *, size_t, char **);
static ipadm_status_t	i_ipadm_parse_rfc3315_duid(dhcp_symbol_t *, uchar_t *,
			    size_t, char **);

/*
 * Callback functions to retrieve property values from the kernel. These
 * functions, when required, translate the values from the kernel to a format
 * suitable for printing. They also retrieve DEFAULT, PERM and POSSIBLE values
 * for a given property.
 */
static ipadm_pd_getf_t	i_ipadm_get_prefixlen, i_ipadm_get_addr_flag,
			i_ipadm_get_zone, i_ipadm_get_broadcast,
			i_ipadm_get_reqhost;

/*
 * Callback functions to set property values. These functions translate the
 * values to a format suitable for kernel consumption, allocate the necessary
 * ioctl buffers and then invoke ioctl().
 */
static ipadm_pd_setf_t	i_ipadm_set_prefixlen, i_ipadm_set_addr_flag,
			i_ipadm_set_zone;

/* address properties description table */
ipadm_prop_desc_t ipadm_addrprop_table[] = {
	{ "broadcast", IPADMPROP_CLASS_ADDR, MOD_PROTO_NONE, 0,
	    NULL, NULL, i_ipadm_get_broadcast },

	{ "deprecated", IPADMPROP_CLASS_ADDR, MOD_PROTO_NONE, 0,
	    i_ipadm_set_addr_flag, i_ipadm_get_onoff,
	    i_ipadm_get_addr_flag },

	{ "prefixlen", IPADMPROP_CLASS_ADDR, MOD_PROTO_NONE, 0,
	    i_ipadm_set_prefixlen, i_ipadm_get_prefixlen,
	    i_ipadm_get_prefixlen },

	{ "private", IPADMPROP_CLASS_ADDR, MOD_PROTO_NONE, 0,
	    i_ipadm_set_addr_flag, i_ipadm_get_onoff, i_ipadm_get_addr_flag },

	{ "reqhost", IPADMPROP_CLASS_ADDR, MOD_PROTO_NONE, 0,
	    NULL, NULL, i_ipadm_get_reqhost },

	{ "transmit", IPADMPROP_CLASS_ADDR, MOD_PROTO_NONE, 0,
	    i_ipadm_set_addr_flag, i_ipadm_get_onoff, i_ipadm_get_addr_flag },

	{ "zone", IPADMPROP_CLASS_ADDR, MOD_PROTO_NONE, 0,
	    i_ipadm_set_zone, NULL, i_ipadm_get_zone },

	{ NULL, 0, 0, 0, NULL, NULL, NULL }
};

static ipadm_prop_desc_t up_addrprop = { "up", IPADMPROP_CLASS_ADDR,
					MOD_PROTO_NONE, 0, NULL, NULL, NULL };

/*
 * Helper function that initializes the `ipadm_ifname', `ipadm_aobjname', and
 * `ipadm_atype' fields of the given `ipaddr'.
 */
void
i_ipadm_init_addr(ipadm_addrobj_t ipaddr, const char *ifname,
    const char *aobjname, ipadm_addr_type_t atype)
{
	bzero(ipaddr, sizeof (struct ipadm_addrobj_s));
	(void) strlcpy(ipaddr->ipadm_ifname, ifname,
	    sizeof (ipaddr->ipadm_ifname));
	(void) strlcpy(ipaddr->ipadm_aobjname, aobjname,
	    sizeof (ipaddr->ipadm_aobjname));
	ipaddr->ipadm_atype = atype;
}

/*
 * Determine the permission of the property depending on whether it has a
 * set() and/or get() callback functions.
 */
ipadm_status_t
i_ipadm_pd2permstr(ipadm_prop_desc_t *pdp, char *buf, uint_t *bufsize)
{
	uint_t	perm = 0;

	if (pdp->ipd_set != NULL)
		perm |= MOD_PROP_PERM_WRITE;
	if (pdp->ipd_get != NULL)
		perm |= MOD_PROP_PERM_READ;

	*bufsize = snprintf(buf, *bufsize, "%c%c",
	    ((perm & MOD_PROP_PERM_READ) != 0) ? 'r' : '-',
	    ((perm & MOD_PROP_PERM_WRITE) != 0) ? 'w' : '-');

	return (IPADM_SUCCESS);
}

/*
 * Retrieves the address object information from persistent db for the
 * address object name in `ipaddr->ipadm_aobjname' and fills the type, flags,
 * and address family fields of `ipaddr'.
 */
static ipadm_status_t
i_ipadm_get_persistent_addrobj(ipadm_handle_t iph, ipadm_addrobj_t ipaddr)
{
	ipadm_status_t		status;
	nvlist_t		*onvl;
	nvlist_t		*anvl = NULL;
	nvpair_t		*nvp;
	/*
	 * Get the address line in the nvlist `onvl' from ipmgmtd daemon.
	 */
	status = i_ipadm_get_db_addr(iph, NULL, ipaddr->ipadm_aobjname, &onvl);
	if (status != IPADM_SUCCESS)
		return (status);
	/*
	 * Fill the relevant fields of `ipaddr' using info from the nvlist.
	 */
	if ((nvp = nvlist_next_nvpair(onvl, NULL)) == NULL ||
	    nvpair_value_nvlist(nvp, &anvl) != 0)
		goto fail;
	if (nvlist_exists(anvl, IPADM_NVP_IPV4ADDR)) {
		ipaddr->ipadm_atype = IPADM_ADDR_STATIC;
		ipaddr->ipadm_af = AF_INET;
	} else if (nvlist_exists(anvl, IPADM_NVP_IPV6ADDR)) {
		ipaddr->ipadm_atype = IPADM_ADDR_STATIC;
		ipaddr->ipadm_af = AF_INET6;
	} else if (nvlist_exists(anvl, IPADM_NVP_DHCP)) {
		ipaddr->ipadm_atype = IPADM_ADDR_DHCP;
		ipaddr->ipadm_af = AF_INET;
	} else if (nvlist_exists(anvl, IPADM_NVP_INTFID)) {
		ipaddr->ipadm_atype = IPADM_ADDR_IPV6_ADDRCONF;
		ipaddr->ipadm_af = AF_INET6;
	} else {
		goto fail;
	}
	ipaddr->ipadm_flags = IPMGMT_PERSIST;
	nvlist_free(onvl);
	return (IPADM_SUCCESS);
fail:
	nvlist_free(onvl);
	return (IPADM_OBJ_NOTFOUND);
}

/*
 * Given an addrobj with `ipadm_aobjname' filled in, i_ipadm_get_addrobj()
 * retrieves the information necessary for any operation on the object,
 * such as delete-addr, enable-addr, disable-addr, up-addr, down-addr,
 * refresh-addr, get-addrprop or set-addrprop. The information include
 * the logical interface number, address type, address family,
 * the interface id (if the address type is IPADM_ADDR_IPV6_ADDRCONF) and
 * the ipadm_flags that indicate if the address is present in
 * active configuration or persistent configuration or both. If the address
 * is not found, IPADM_OP_NOTSUP is returned.
 */
ipadm_status_t
i_ipadm_get_addrobj(ipadm_handle_t iph, ipadm_addrobj_t ipaddr)
{
	ipmgmt_aobjop_arg_t	larg;
	ipmgmt_aobjop_rval_t	rval;
	int			err = 0;

	/* populate the door_call argument structure */
	larg.ia_cmd = IPMGMT_CMD_AOBJNAME2ADDROBJ;
	(void) strlcpy(larg.ia_aobjname, ipaddr->ipadm_aobjname,
	    sizeof (larg.ia_aobjname));

	err = ipadm_door_call(iph, &larg, sizeof (larg), &rval, sizeof (rval));
	if (err == 0) {
		(void) strlcpy(ipaddr->ipadm_ifname, rval.ir_ifname,
		    sizeof (ipaddr->ipadm_ifname));
		ipaddr->ipadm_lifnum = rval.ir_lnum;
		ipaddr->ipadm_atype = rval.ir_atype;
		ipaddr->ipadm_af = rval.ir_family;
		ipaddr->ipadm_flags = rval.ir_flags;
		if (rval.ir_atype == IPADM_ADDR_IPV6_ADDRCONF) {
			(void) memcpy(&ipaddr->ipadm_intfid, &rval.ir_ifid,
			    sizeof (ipaddr->ipadm_intfid));
			ipaddr->ipadm_intfidlen = rval.ir_ifidplen;
		}
	} else if (err == ENOENT) {
		ipadm_status_t	status;

		/*
		 * We will get ENOENT, if the addrobj was created persistently
		 * but is disabled and is also not available in the aobjmap
		 * of ipmgmtd daemon. For this case, we attempt to retrieve it
		 * from persistent db.
		 */
		status = i_ipadm_get_persistent_addrobj(iph, ipaddr);
		if (status != IPADM_SUCCESS)
			return (status);
	} else {
		return (ipadm_errno2status(err));
	}

	return (IPADM_SUCCESS);
}

/*
 * Retrieves the static address (IPv4 or IPv6) for the given address object
 * in `ipaddr' from persistent DB.
 */
static ipadm_status_t
i_ipadm_get_static_addr_db(ipadm_handle_t iph, ipadm_addrobj_t ipaddr,
    boolean_t *is_up)
{
	ipadm_status_t		status = IPADM_SUCCESS;
	nvlist_t		*onvl;
	nvlist_t		*anvl = NULL;
	nvlist_t		*nvladdr;
	nvpair_t		*nvp;
	char			*name;
	char			*aobjname = ipaddr->ipadm_aobjname;
	char			*hname;
	char			*up_str;
	sa_family_t		af = AF_UNSPEC;
	int			err;

	/*
	 * Get the address line in the nvlist `onvl' from ipmgmtd daemon.
	 */
	status = i_ipadm_get_db_addr(iph, NULL, aobjname, &onvl);
	if (status != IPADM_SUCCESS)
		return (status);
	/*
	 * Walk through the nvlist `onvl' to extract the IPADM_NVP_IPV4ADDR
	 * or the IPADM_NVP_IPV6ADDR name-value pair.
	 */
	for (nvp = nvlist_next_nvpair(onvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(onvl, NULL)) {
		if (nvpair_value_nvlist(nvp, &anvl) != 0)
			continue;
		if (nvlist_exists(anvl, IPADM_NVP_IPV4ADDR) ||
		    nvlist_exists(anvl, IPADM_NVP_IPV6ADDR))
			break;
	}
	if (nvp == NULL) {
		status = IPADM_OBJ_NOTFOUND;
		goto done;
	}
	for (nvp = nvlist_next_nvpair(anvl, NULL);
	    nvp != NULL; nvp = nvlist_next_nvpair(anvl, nvp)) {
		name = nvpair_name(nvp);
		if (strcmp(name, IPADM_NVP_IPV4ADDR) == 0) {
			af = AF_INET;
			break;
		} else if (strcmp(name, IPADM_NVP_IPV6ADDR) == 0) {
			af = AF_INET6;
			break;
		}
	}
	assert(af != AF_UNSPEC);
	if ((err = nvpair_value_nvlist(nvp, &nvladdr)) != 0 ||
	    (err = nvlist_lookup_string(nvladdr, IPADM_NVP_IPADDRHNAME,
	    &hname)) != 0) {
		status = ipadm_errno2status(err);
		goto done;
	}
	if ((status = ipadm_set_addr(ipaddr, hname, af)) != IPADM_SUCCESS)
		goto done;
	if (nvlist_lookup_string(nvladdr, IPADM_NVP_IPDADDRHNAME,
	    &hname) == 0) {
		status = ipadm_set_dst_addr(ipaddr, hname, af);
		if (status != IPADM_SUCCESS)
			goto done;
	}
	if (is_up != NULL) {
		if ((err = nvlist_lookup_string(anvl, "up", &up_str)) != 0) {
			status = ipadm_errno2status(err);
			goto done;
		}
		*is_up = (strcmp(up_str, "yes") == 0);
	}

done:
	nvlist_free(onvl);
	return (status);
}

/*
 * Retrieves the addrconf address object for the address object name
 * in `ipaddr' from persistent DB.
 */
static ipadm_status_t
i_ipadm_get_addrconf_db(ipadm_handle_t iph, ipadm_addrobj_t ipaddr)
{
	ipadm_status_t		status;
	nvlist_t		*onvl;
	nvlist_t		*anvl = NULL;
	nvpair_t		*nvp;
	char			*aobjname = ipaddr->ipadm_aobjname;

	/*
	 * Get the address line in the nvlist `onvl' from ipmgmtd daemon.
	 */
	status = i_ipadm_get_db_addr(iph, NULL, aobjname, &onvl);
	if (status != IPADM_SUCCESS)
		return (status);
	/*
	 * Walk through the nvlist `onvl' to extract the IPADM_NVP_INTFID
	 * name-value pair.
	 */
	for (nvp = nvlist_next_nvpair(onvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(onvl, NULL)) {
		if (nvpair_value_nvlist(nvp, &anvl) != 0)
			continue;
		if (nvlist_exists(anvl, IPADM_NVP_INTFID))
			break;
	}
	if (nvp == NULL)
		goto fail;
	status = i_ipadm_nvl2addrconf_addrobj(anvl, ipaddr);
	if (status != IPADM_SUCCESS)
		return (status);
	ipaddr->ipadm_af = AF_INET6;

	nvlist_free(onvl);
	return (IPADM_SUCCESS);
fail:
	nvlist_free(onvl);
	return (IPADM_OBJ_NOTFOUND);
}

/*
 * For the given `addrobj->ipadm_lifnum' and `addrobj->ipadm_af', this function
 * fills in the address objname, the address type and the ipadm_flags.
 */
ipadm_status_t
i_ipadm_get_lif2addrobj(ipadm_handle_t iph, const char *lifname,
    sa_family_t af, ipadm_addrobj_t addrobj)
{
	ipmgmt_aobjop_arg_t	larg;
	ipmgmt_aobjop_rval_t	rval;
	int			lifnum = i_ipadm_get_lnum(lifname);
	char			tifname[LIFNAMSIZ];
	int			err;

	larg.ia_cmd = IPMGMT_CMD_LIF2ADDROBJ;
	i_ipadm_get_ifname(lifname, tifname, sizeof (tifname));
	(void) strlcpy(larg.ia_ifname, tifname, sizeof (larg.ia_ifname));
	larg.ia_lnum = lifnum;
	larg.ia_family = af;

	err = ipadm_door_call(iph, &larg, sizeof (larg), &rval, sizeof (rval));
	if (err != 0)
		return (ipadm_errno2status(err));
	(void) strlcpy(addrobj->ipadm_ifname, tifname,
	    sizeof (addrobj->ipadm_ifname));
	addrobj->ipadm_lifnum = lifnum;
	addrobj->ipadm_af = af;
	(void) strlcpy(addrobj->ipadm_aobjname, rval.ir_aobjname,
	    sizeof (addrobj->ipadm_aobjname));
	addrobj->ipadm_atype = rval.ir_atype;
	addrobj->ipadm_flags = rval.ir_flags;

	return (IPADM_SUCCESS);
}

/*
 * Adds an addrobj to ipmgmtd daemon's aobjmap (active configuration).
 * with the given name and logical interface number.
 * This API is called by in.ndpd to add addrobjs when new prefixes or
 * dhcpv6 addresses are configured.
 */
ipadm_status_t
ipadm_add_aobjname(ipadm_handle_t iph, const char *ifname, sa_family_t af,
    const char *aobjname, ipadm_addr_type_t atype, int lnum)
{
	ipmgmt_aobjop_arg_t	larg;
	int			err;

	larg.ia_cmd = IPMGMT_CMD_ADDROBJ_ADD;
	(void) strlcpy(larg.ia_ifname, ifname, sizeof (larg.ia_ifname));
	(void) strlcpy(larg.ia_aobjname, aobjname, sizeof (larg.ia_aobjname));
	larg.ia_atype = atype;
	larg.ia_lnum = lnum;
	larg.ia_family = af;
	err = ipadm_door_call(iph, &larg, sizeof (larg), NULL, 0);
	return (ipadm_errno2status(err));
}

/*
 * Deletes an address object with given name and logical number from ipmgmtd
 * daemon's aobjmap (active configuration). This API is called by in.ndpd to
 * remove addrobjs when auto-configured prefixes or dhcpv6 addresses are
 * removed.
 */
ipadm_status_t
ipadm_delete_aobjname(ipadm_handle_t iph, const char *ifname, sa_family_t af,
    const char *aobjname, ipadm_addr_type_t atype, int lnum)
{
	struct ipadm_addrobj_s	aobj;

	i_ipadm_init_addr(&aobj, ifname, aobjname, atype);
	aobj.ipadm_af = af;
	aobj.ipadm_lifnum = lnum;
	return (i_ipadm_delete_addrobj(iph, &aobj, IPADM_OPT_ACTIVE));
}

/*
 * Gets all the addresses from active configuration and populates the
 * address information in `addrinfo'.
 */
static ipadm_status_t
i_ipadm_active_addr_info(ipadm_handle_t iph, const char *ifname,
    ipadm_addr_info_t **addrinfo, uint32_t ipadm_flags, int64_t lifc_flags)
{
	ipadm_status_t		status;
	struct ifaddrs		*ifap, *ifa;
	ipadm_addr_info_t	*curr, *prev = NULL;
	struct ifaddrs		*cifaddr;
	struct lifreq		lifr;
	int			sock;
	uint64_t		flags;
	char			cifname[LIFNAMSIZ];
	struct sockaddr_in6	*sin6;
	struct ipadm_addrobj_s	ipaddr;

retry:
	*addrinfo = NULL;

	/* Get all the configured addresses */
	if (getallifaddrs(AF_UNSPEC, &ifa, lifc_flags, 0) < 0)
		return (ipadm_errno2status(errno));
	/* Return if there is nothing to process. */
	if (ifa == NULL)
		return (IPADM_SUCCESS);
	bzero(&lifr, sizeof (lifr));
	for (ifap = ifa; ifap != NULL; ifap = ifap->ifa_next) {
		struct sockaddr_storage data;

		i_ipadm_get_ifname(ifap->ifa_name, cifname, sizeof (cifname));
		if (ifname != NULL && strcmp(cifname, ifname) != 0)
			continue;
		if (!(ipadm_flags & IPADM_OPT_ZEROADDR) &&
		    sockaddrunspec(ifap->ifa_addr) &&
		    !(ifap->ifa_flags & IFF_DHCPRUNNING))
			continue;

		/* Allocate and populate the current node in the list. */
		if ((curr = calloc(1, sizeof (ipadm_addr_info_t))) == NULL) {
			status = IPADM_NO_MEMORY;
			goto fail;
		}

		/* Link to the list in `addrinfo'. */
		if (prev != NULL)
			prev->ia_ifa.ifa_next = &curr->ia_ifa;
		else
			*addrinfo = curr;
		prev = curr;

		cifaddr = &curr->ia_ifa;
		if ((cifaddr->ifa_name = strdup(ifap->ifa_name)) == NULL) {
			status = IPADM_NO_MEMORY;
			goto fail;
		}
		cifaddr->ifa_flags = ifap->ifa_flags;
		cifaddr->ifa_addr = malloc(sizeof (struct sockaddr_storage));
		if (cifaddr->ifa_addr == NULL) {
			status = IPADM_NO_MEMORY;
			goto fail;
		}
		(void) memcpy(cifaddr->ifa_addr, ifap->ifa_addr,
		    sizeof (struct sockaddr_storage));
		cifaddr->ifa_netmask = malloc(sizeof (struct sockaddr_storage));
		if (cifaddr->ifa_netmask == NULL) {
			status = IPADM_NO_MEMORY;
			goto fail;
		}
		(void) memcpy(cifaddr->ifa_netmask, ifap->ifa_netmask,
		    sizeof (struct sockaddr_storage));
		if (ifap->ifa_flags & IFF_POINTOPOINT) {
			cifaddr->ifa_dstaddr = malloc(
			    sizeof (struct sockaddr_storage));
			if (cifaddr->ifa_dstaddr == NULL) {
				status = IPADM_NO_MEMORY;
				goto fail;
			}
			(void) memcpy(cifaddr->ifa_dstaddr, ifap->ifa_dstaddr,
			    sizeof (struct sockaddr_storage));
		} else if (ifap->ifa_flags & IFF_BROADCAST) {
			cifaddr->ifa_broadaddr = malloc(
			    sizeof (struct sockaddr_storage));
			if (cifaddr->ifa_broadaddr == NULL) {
				status = IPADM_NO_MEMORY;
				goto fail;
			}
			(void) memcpy(cifaddr->ifa_broadaddr,
			    ifap->ifa_broadaddr,
			    sizeof (struct sockaddr_storage));
		}
		/* Get the addrobj name stored for this logical interface. */
		status = i_ipadm_get_lif2addrobj(iph, ifap->ifa_name,
		    ifap->ifa_addr->sa_family, &ipaddr);

		/*
		 * Find address type from ifa_flags, if we could not get it
		 * from daemon.
		 */
		(void) memcpy(&data, ifap->ifa_addr,
		    sizeof (struct sockaddr_in6));
		sin6 = SIN6(&data);
		flags = ifap->ifa_flags;
		if (status == IPADM_SUCCESS) {
			(void) strlcpy(curr->ia_aobjname, ipaddr.ipadm_aobjname,
			    sizeof (curr->ia_aobjname));
			curr->ia_atype = ipaddr.ipadm_atype;
		} else if ((flags & IFF_DHCPRUNNING) && (!(flags & IFF_IPV6) ||
		    !IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr))) {
			curr->ia_atype = IPADM_ADDR_DHCP;
		} else if (flags & IFF_ADDRCONF) {
			curr->ia_atype = IPADM_ADDR_IPV6_ADDRCONF;
		} else {
			curr->ia_atype = IPADM_ADDR_STATIC;
		}
		/*
		 * Populate the flags for the active configuration from the
		 * `ifa_flags'.
		 */
		if (!(flags & IFF_UP)) {
			if (flags & IFF_DUPLICATE)
				curr->ia_state = IPADM_ADDRS_DUPLICATE;
			else
				curr->ia_state = IPADM_ADDRS_DOWN;
		} else {
			curr->ia_cflags |= IPADM_ADDRF_UP;
			if (flags & IFF_RUNNING) {
				(void) strlcpy(lifr.lifr_name, ifap->ifa_name,
				    sizeof (lifr.lifr_name));
				sock = IPADM_SOCK(iph,
				    ifap->ifa_addr->sa_family);
				if (ioctl(sock, SIOCGLIFDADSTATE,
				    (caddr_t)&lifr) < 0) {
					if (errno == ENXIO) {
						freeifaddrs(ifa);
						ipadm_free_addr_info(*addrinfo);
						goto retry;
					}
					status = ipadm_errno2status(errno);
					goto fail;
				}
				if (lifr.lifr_dadstate == DAD_IN_PROGRESS)
					curr->ia_state = IPADM_ADDRS_TENTATIVE;
				else
					curr->ia_state = IPADM_ADDRS_OK;
			} else {
				curr->ia_state = IPADM_ADDRS_INACCESSIBLE;
			}
		}
		if (flags & IFF_UNNUMBERED)
			curr->ia_cflags |= IPADM_ADDRF_UNNUMBERED;
		if (flags & IFF_PRIVATE)
			curr->ia_cflags |= IPADM_ADDRF_PRIVATE;
		if (flags & IFF_TEMPORARY)
			curr->ia_cflags |= IPADM_ADDRF_TEMPORARY;
		if (flags & IFF_DEPRECATED)
			curr->ia_cflags |= IPADM_ADDRF_DEPRECATED;

		if (curr->ia_state != IPADM_ADDRS_DISABLED &&
		    (curr->ia_atype == IPADM_ADDR_DHCP ||
		    (curr->ia_atype == IPADM_ADDR_IPV6_ADDRCONF &&
		    (flags & IFF_DHCPRUNNING) &&
		    !IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr)))) {
			status = i_ipadm_get_dhcp_info(&ipaddr, curr);
			if (status != IPADM_SUCCESS)
				goto fail;
		}
	}

	freeifaddrs(ifa);
	return (IPADM_SUCCESS);

fail:
	/* On error, cleanup everything and return. */
	ipadm_free_addr_info(*addrinfo);
	*addrinfo = NULL;
	freeifaddrs(ifa);
	return (status);
}

/*
 * From the given `name', i_ipadm_name2atype() deduces the address type
 * and address family. If the `name' implies an address, it returns B_TRUE.
 * Else, returns B_FALSE and leaves the output parameters unchanged.
 */
boolean_t
i_ipadm_name2atype(const char *name, sa_family_t *af, ipadm_addr_type_t *type)
{
	boolean_t	is_addr = B_TRUE;

	if (strcmp(name, IPADM_NVP_IPV4ADDR) == 0) {
		*af = AF_INET;
		*type = IPADM_ADDR_STATIC;
	} else if (strcmp(name, IPADM_NVP_IPV6ADDR) == 0) {
		*af = AF_INET6;
		*type = IPADM_ADDR_STATIC;
	} else if (strcmp(name, IPADM_NVP_DHCP) == 0) {
		*af = AF_INET;
		*type = IPADM_ADDR_DHCP;
	} else if (strcmp(name, IPADM_NVP_INTFID) == 0) {
		*af = AF_INET6;
		*type = IPADM_ADDR_IPV6_ADDRCONF;
	} else {
		is_addr = B_FALSE;
	}

	return (is_addr);
}

/*
 * Parses the given nvlist `nvl' for an address or an address property.
 * The input nvlist must contain either an address or an address property.
 * `ainfo' is an input as well as output parameter. When an address or an
 * address property is found, `ainfo' is updated with the information found.
 * Some of the fields may be already filled in by the calling function.
 *
 * The fields that will be filled/updated by this function are `ia_pflags',
 * `ia_sname' and `ia_dname'. Values for `ia_pflags' are obtained if the `nvl'
 * contains an address property. `ia_sname', `ia_dname', and `ia_pflags' are
 * obtained if `nvl' contains an address.
 */
static ipadm_status_t
i_ipadm_nvl2ainfo_common(nvlist_t *nvl, ipadm_addr_info_t *ainfo)
{
	nvlist_t		*nvladdr;
	char			*name;
	char			*propstr = NULL;
	char			*sname, *dname;
	nvpair_t		*nvp;
	sa_family_t		af;
	ipadm_addr_type_t	atype;
	boolean_t		is_addr = B_FALSE;
	int			err;

	for (nvp = nvlist_next_nvpair(nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {
		name = nvpair_name(nvp);
		if (i_ipadm_name2atype(name, &af, &atype)) {
			err = nvpair_value_nvlist(nvp, &nvladdr);
			is_addr = B_TRUE;
		} else if (strcmp(name, IPADM_NVP_DINTFID) == 0) {
			continue;
		} else if (IPADM_PRIV_NVP(name)) {
			continue;
		} else {
			err = nvpair_value_string(nvp, &propstr);
		}
		if (err != 0)
			return (ipadm_errno2status(err));
	}

	if (is_addr) {
		/*
		 * We got an address from the nvlist `nvl'.
		 * Parse `nvladdr' and populate relevant information
		 * in `ainfo'.
		 */
		switch (atype) {
		case IPADM_ADDR_STATIC:
			if (strcmp(name, "up") == 0 &&
			    strcmp(propstr, "yes") == 0) {
				ainfo->ia_pflags |= IPADM_ADDRF_UP;
			}
			/*
			 * For static addresses, we need to get the hostnames.
			 */
			err = nvlist_lookup_string(nvladdr,
			    IPADM_NVP_IPADDRHNAME, &sname);
			if (err != 0)
				return (ipadm_errno2status(err));
			(void) strlcpy(ainfo->ia_sname, sname,
			    sizeof (ainfo->ia_sname));
			err = nvlist_lookup_string(nvladdr,
			    IPADM_NVP_IPDADDRHNAME, &dname);
			if (err == 0) {
				(void) strlcpy(ainfo->ia_dname, dname,
				    sizeof (ainfo->ia_dname));
			}
			break;
		case IPADM_ADDR_DHCP:
		case IPADM_ADDR_IPV6_ADDRCONF:
			/*
			 * dhcp and addrconf address objects are always
			 * marked up when re-enabled.
			 */
			ainfo->ia_pflags |= IPADM_ADDRF_UP;
			break;
		default:
			return (IPADM_FAILURE);
		}
	} else {
		/*
		 * We got an address property from `nvl'. Parse the
		 * name and the property value. Update the `ainfo->ia_pflags'
		 * for the flags.
		 */
		if (strcmp(name, "deprecated") == 0) {
			if (strcmp(propstr, IPADM_ONSTR) == 0)
				ainfo->ia_pflags |= IPADM_ADDRF_DEPRECATED;
		} else if (strcmp(name, "private") == 0) {
			if (strcmp(propstr, IPADM_ONSTR) == 0)
				ainfo->ia_pflags |= IPADM_ADDRF_PRIVATE;
		}
	}

	return (IPADM_SUCCESS);
}

/*
 * Parses the given nvlist `nvl' for an address or an address property.
 * The input nvlist must contain either an address or an address property.
 * `ainfo' is an input as well as output parameter. When an address or an
 * address property is found, `ainfo' is updated with the information found.
 * Some of the fields may be already filled in by the calling function,
 * because of previous calls to i_ipadm_nvl2ainfo_active().
 *
 * Since the address object in `nvl' is also in the active configuration, the
 * fields that will be filled/updated by this function are `ia_pflags',
 * `ia_sname' and `ia_dname'.
 *
 * If this function returns an error, the calling function will take
 * care of freeing the fields in `ainfo'.
 */
static ipadm_status_t
i_ipadm_nvl2ainfo_active(nvlist_t *nvl, ipadm_addr_info_t *ainfo)
{
	return (i_ipadm_nvl2ainfo_common(nvl, ainfo));
}

/*
 * Parses the given nvlist `nvl' for an address or an address property.
 * The input nvlist must contain either an address or an address property.
 * `ainfo' is an input as well as output parameter. When an address or an
 * address property is found, `ainfo' is updated with the information found.
 * Some of the fields may be already filled in by the calling function,
 * because of previous calls to i_ipadm_nvl2ainfo_persist().
 *
 * All the relevant fields in `ainfo' will be filled by this function based
 * on what we find in `nvl'.
 *
 * If this function returns an error, the calling function will take
 * care of freeing the fields in `ainfo'.
 */
static ipadm_status_t
i_ipadm_nvl2ainfo_persist(nvlist_t *nvl, ipadm_addr_info_t *ainfo)
{
	nvlist_t		*nvladdr;
	struct ifaddrs		*ifa;
	char			*name;
	char			*ifname = NULL;
	char			*aobjname = NULL;
	char			*propstr = NULL;
	nvpair_t		*nvp;
	nvpair_t		*dstnvp = NULL;
	sa_family_t		af;
	ipadm_addr_type_t	atype;
	boolean_t		is_addr = B_FALSE;
	size_t			size = sizeof (struct sockaddr_storage);
	uint32_t		plen = 0;
	int			err;
	ipadm_status_t		status;

	status = i_ipadm_nvl2ainfo_common(nvl, ainfo);
	if (status != IPADM_SUCCESS)
		return (status);

	for (nvp = nvlist_next_nvpair(nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {
		name = nvpair_name(nvp);
		if (strcmp(name, IPADM_NVP_IFNAME) == 0) {
			err = nvpair_value_string(nvp, &ifname);
		} else if (strcmp(name, IPADM_NVP_AOBJNAME) == 0) {
			err = nvpair_value_string(nvp, &aobjname);
		} else if (i_ipadm_name2atype(name, &af, &atype)) {
			err = nvpair_value_nvlist(nvp, &nvladdr);
			is_addr = B_TRUE;
		} else if (strcmp(name, IPADM_NVP_DINTFID) == 0) {
			dstnvp = nvp;
		} else {
			err = nvpair_value_string(nvp, &propstr);
		}
		if (err != 0)
			return (ipadm_errno2status(err));
	}

	ifa = &ainfo->ia_ifa;
	(void) strlcpy(ainfo->ia_aobjname, aobjname,
	    sizeof (ainfo->ia_aobjname));
	if (ifa->ifa_name == NULL && (ifa->ifa_name = strdup(ifname)) == NULL)
		return (IPADM_NO_MEMORY);
	if (is_addr) {
		struct sockaddr_in6 data;
		struct sockaddr_in6 dstdata;

		/*
		 * We got an address from the nvlist `nvl'.
		 * Parse `nvladdr' and populate `ifa->ifa_addr'.
		 */
		ainfo->ia_atype = atype;
		if ((ifa->ifa_addr = calloc(1, size)) == NULL)
			return (IPADM_NO_MEMORY);
		switch (atype) {
		case IPADM_ADDR_STATIC:
			ifa->ifa_addr->sa_family = af;
			break;
		case IPADM_ADDR_DHCP:
			ifa->ifa_addr->sa_family = AF_INET;
			break;
		case IPADM_ADDR_IPV6_ADDRCONF:
			data.sin6_family = AF_INET6;
			if (i_ipadm_nvl2in6_addr(nvladdr, IPADM_NVP_IPNUMADDR,
			    &data.sin6_addr) != IPADM_SUCCESS)
				return (IPADM_NO_MEMORY);
			err = nvlist_lookup_uint32(nvladdr, IPADM_NVP_PREFIXLEN,
			    &plen);
			if (err != 0)
				return (ipadm_errno2status(err));
			if ((ifa->ifa_netmask = malloc(size)) == NULL)
				return (IPADM_NO_MEMORY);
			if ((err = plen2mask(plen, af, ifa->ifa_netmask)) != 0)
				return (ipadm_errno2status(err));
			(void) memcpy(ifa->ifa_addr, &data, sizeof (data));
			if (dstnvp != NULL) {
				in6_addr_t *in6_addr;
				uint8_t *addr6;
				uint_t n;

				ifa->ifa_dstaddr = calloc(1, size);
				if (ifa->ifa_dstaddr == NULL)
					return (IPADM_NO_MEMORY);
				dstdata.sin6_family = AF_INET6;
				in6_addr = &dstdata.sin6_addr;
				err = nvpair_value_uint8_array(dstnvp, &addr6,
				    &n);
				if (err != 0)
					return (ipadm_errno2status(err));
				bcopy(addr6, in6_addr->s6_addr, n);
				(void) memcpy(ifa->ifa_dstaddr, &dstdata,
				    sizeof (dstdata));
			}
			break;
		default:
			return (IPADM_FAILURE);
		}
	} else {
		if (strcmp(name, "prefixlen") == 0) {
			/*
			 * If a prefixlen was found, update the
			 * `ainfo->ia_ifa.ifa_netmask'.
			 */

			if ((ifa->ifa_netmask = malloc(size)) == NULL)
				return (IPADM_NO_MEMORY);
			/*
			 * Address property lines always follow the address
			 * line itself in the persistent db. We must have
			 * found a valid `ainfo->ia_ifa.ifa_addr' by now.
			 */
			assert(ifa->ifa_addr != NULL);
			err = plen2mask(atoi(propstr), ifa->ifa_addr->sa_family,
			    ifa->ifa_netmask);
			if (err != 0)
				return (ipadm_errno2status(err));
		}
	}

	return (IPADM_SUCCESS);
}

/*
 * Retrieves all addresses from active config and appends to it the
 * addresses that are found only in persistent config. In addition,
 * it updates the persistent fields for each address from information
 * found in persistent config. The output parameter `addrinfo' contains
 * complete information regarding all addresses in active as well as
 * persistent config.
 */
static ipadm_status_t
i_ipadm_get_all_addr_info(ipadm_handle_t iph, const char *ifname,
    ipadm_addr_info_t **addrinfo, uint32_t ipadm_flags, int64_t lifc_flags)
{
	nvlist_t		*nvladdr = NULL;
	nvlist_t		*onvl = NULL;
	nvpair_t		*nvp;
	ipadm_status_t		status;
	ipadm_addr_info_t	*ainfo = NULL;
	ipadm_addr_info_t	*curr;
	ipadm_addr_info_t	*last = NULL;
	char			*aobjname;

	/* Get all addresses from active config. */
	status = i_ipadm_active_addr_info(iph, ifname, &ainfo, ipadm_flags,
	    lifc_flags);
	if (status != IPADM_SUCCESS)
		goto fail;
	/* Get all addresses from persistent config. */
	status = i_ipadm_get_db_addr(iph, ifname, NULL, &onvl);
	/*
	 * If no address was found in persistent config, just
	 * return what we found in active config.
	 */
	if (status == IPADM_OBJ_NOTFOUND) {
		/*
		 * No address was found in active nor persistent config.
		 * If a specific interface was not provided in `ifname',
		 * we return IPADM_SUCCESS and let the caller loop through
		 * `addrinfo'. Else, we check if the interface is configured
		 * in active config before returning IPADM_ENXIO.
		 * This is to avoid throwing an error when an interface is
		 * plumbed with no addresses configured on it.
		 */
		if (ainfo == NULL && ifname != NULL &&
		    !ipadm_if_enabled(iph, ifname, AF_UNSPEC))
			return (IPADM_NOSUCH_IF);
		*addrinfo = ainfo;
		return (IPADM_SUCCESS);
	}
	/* In case of any other error, cleanup and return. */
	if (status != IPADM_SUCCESS)
		goto fail;
	/* we append to make sure, loopback addresses are first */
	if (ainfo != NULL) {
		for (curr = ainfo; IA_NEXT(curr) != NULL; curr = IA_NEXT(curr))
			;
		last = curr;
	}

	/*
	 * `onvl' will contain all the address lines from the db. Each line
	 * could contain the address itself or an address property. Addresses
	 * and address properties are found in separate lines.
	 *
	 * If an address A was found in active, we will already have `ainfo',
	 * and it is present in persistent configuration as well, we need to
	 * update `ainfo' with persistent information (`ia_pflags).
	 * For each address B found only in persistent configuration,
	 * append the address to the list with the address info for B from
	 * `onvl'.
	 */
	for (nvp = nvlist_next_nvpair(onvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(onvl, nvp)) {
		if (nvpair_value_nvlist(nvp, &nvladdr) != 0)
			continue;
		if (nvlist_lookup_string(nvladdr, IPADM_NVP_AOBJNAME,
		    &aobjname) != 0)
			continue;
		for (curr = ainfo; curr != NULL; curr = IA_NEXT(curr)) {
			if (strcmp(curr->ia_aobjname, aobjname) == 0)
				break;
		}
		if (curr == NULL) {
			/*
			 * We did not find this address object in `ainfo'.
			 * This means that the address object exists only
			 * in the persistent configuration. Get its
			 * details and append to `ainfo'.
			 */
			curr = calloc(1, sizeof (ipadm_addr_info_t));
			if (curr == NULL)
				goto fail;
			curr->ia_state = IPADM_ADDRS_DISABLED;
			if (last != NULL)
				last->ia_ifa.ifa_next = &curr->ia_ifa;
			else
				ainfo = curr;
			last = curr;
		}
		/*
		 * Fill relevant fields of `curr' from the persistent info
		 * in `nvladdr'. Call the appropriate function based on the
		 * `ia_state' value.
		 */
		if (curr->ia_state == IPADM_ADDRS_DISABLED)
			status = i_ipadm_nvl2ainfo_persist(nvladdr, curr);
		else
			status = i_ipadm_nvl2ainfo_active(nvladdr, curr);
		if (status != IPADM_SUCCESS)
			goto fail;
	}
	*addrinfo = ainfo;
	nvlist_free(onvl);
	return (status);
fail:
	/* On error, cleanup and return. */
	nvlist_free(onvl);
	ipadm_free_addr_info(ainfo);
	*addrinfo = NULL;
	return (status);
}

/*
 * Callback function that sets the property `prefixlen' on the address
 * object in `arg' to the value in `pval'.
 */
/* ARGSUSED */
static ipadm_status_t
i_ipadm_set_prefixlen(ipadm_handle_t iph, const void *arg,
    ipadm_prop_desc_t *pdp, const void *pval, uint_t af, uint_t flags)
{
	struct sockaddr_storage	netmask;
	struct lifreq		lifr;
	int			err, s;
	unsigned long		prefixlen, abits;
	char			*end;
	ipadm_addrobj_t		ipaddr = (ipadm_addrobj_t)arg;

	if (ipaddr->ipadm_atype == IPADM_ADDR_DHCP)
		return (IPADM_OP_NOTSUP);

	errno = 0;
	prefixlen = strtoul(pval, &end, 10);
	if (errno != 0 || *end != '\0')
		return (IPADM_INVALID_ARG);

	abits = (af == AF_INET ? IP_ABITS : IPV6_ABITS);
	if (prefixlen == 0 || prefixlen == (abits - 1))
		return (IPADM_INVALID_ARG);

	if ((err = plen2mask(prefixlen, af, (struct sockaddr *)&netmask)) != 0)
		return (ipadm_errno2status(err));

	s = (af == AF_INET ? iph->ih_sock : iph->ih_sock6);

	bzero(&lifr, sizeof (lifr));
	i_ipadm_addrobj2lifname(ipaddr, lifr.lifr_name,
	    sizeof (lifr.lifr_name));
	(void) memcpy(&lifr.lifr_addr, &netmask, sizeof (netmask));
	if (ioctl(s, SIOCSLIFNETMASK, (caddr_t)&lifr) < 0)
		return (ipadm_errno2status(errno));

	/* now, change the broadcast address to reflect the prefixlen */
	if (af == AF_INET) {
		/*
		 * get the interface address and set it, this should reset
		 * the broadcast address.
		 */
		(void) ioctl(s, SIOCGLIFADDR, (caddr_t)&lifr);
		(void) ioctl(s, SIOCSLIFADDR, (caddr_t)&lifr);
	}

	return (IPADM_SUCCESS);
}


/*
 * Callback function that sets the given value `pval' to one of the
 * properties among `deprecated', `private', and `transmit' as defined in
 * `pdp', on the address object in `arg'.
 */
/* ARGSUSED */
static ipadm_status_t
i_ipadm_set_addr_flag(ipadm_handle_t iph, const void *arg,
    ipadm_prop_desc_t *pdp, const void *pval, uint_t af, uint_t flags)
{
	char		lifname[LIFNAMSIZ];
	uint64_t	on_flags = 0, off_flags = 0;
	boolean_t	on;
	ipadm_addrobj_t	ipaddr = (ipadm_addrobj_t)arg;

	if (ipaddr->ipadm_atype == IPADM_ADDR_DHCP &&
	    strcmp(pdp->ipd_name, "deprecated") == 0)
		return (IPADM_OP_NOTSUP);

	if (strcmp(pval, IPADM_ONSTR) == 0)
		on = B_TRUE;
	else if (strcmp(pval, IPADM_OFFSTR) == 0)
		on = B_FALSE;
	else
		return (IPADM_INVALID_ARG);

	if (strcmp(pdp->ipd_name, "private") == 0) {
		if (on)
			on_flags = IFF_PRIVATE;
		else
			off_flags = IFF_PRIVATE;
	} else if (strcmp(pdp->ipd_name, "transmit") == 0) {
		if (on)
			off_flags = IFF_NOXMIT;
		else
			on_flags = IFF_NOXMIT;
	} else if (strcmp(pdp->ipd_name, "deprecated") == 0) {
		if (on)
			on_flags = IFF_DEPRECATED;
		else
			off_flags = IFF_DEPRECATED;
	} else {
		return (IPADM_PROP_UNKNOWN);
	}

	i_ipadm_addrobj2lifname(ipaddr, lifname, sizeof (lifname));
	return (i_ipadm_set_flags(iph, lifname, af, on_flags, off_flags));
}

/*
 * Callback function that sets the property `zone' on the address
 * object in `arg' to the value in `pval'.
 */
/* ARGSUSED */
static ipadm_status_t
i_ipadm_set_zone(ipadm_handle_t iph, const void *arg,
    ipadm_prop_desc_t *pdp, const void *pval, uint_t af, uint_t flags)
{
	struct lifreq	lifr;
	zoneid_t	zoneid;
	int		s;

	/*
	 * To modify the zone assignment such that it persists across
	 * reboots, zonecfg(1M) must be used. Trusted Extentions
	 * "all-zones" property value is an exception to that rule because
	 * it doesn't depend on the existence of any non-global zone; hence
	 * persisting all-zones property (and reverting it back to global
	 * zone default value) are allowed.
	 */
	if (flags & IPADM_OPT_PERSIST) {
		if (!is_system_labeled())
			return (IPADM_OP_NOTSUP);
		if (strcmp(pval, "all-zones") == 0)
			zoneid = ALL_ZONES;
		else if (strcmp(pval, GLOBAL_ZONENAME) == 0)
			zoneid = GLOBAL_ZONEID;
		else
			return (IPADM_OP_NOTSUP);
	} else if (flags & IPADM_OPT_ACTIVE) {
		/* put logical interface into all zones */
		if (strcmp(pval, "all-zones") == 0) {
			if (is_system_labeled())
				zoneid = ALL_ZONES;
			else
				return (IPADM_OP_NOTSUP);
		} else {
			/* zone must be ready or running */
			if ((zoneid = getzoneidbyname(pval)) == -1)
				return (ipadm_errno2status(errno));
		}
	} else {
		return (IPADM_INVALID_ARG);
	}

	s = (af == AF_INET ? iph->ih_sock : iph->ih_sock6);
	bzero(&lifr, sizeof (lifr));
	i_ipadm_addrobj2lifname((ipadm_addrobj_t)arg, lifr.lifr_name,
	    sizeof (lifr.lifr_name));
	lifr.lifr_zoneid = zoneid;
	if (ioctl(s, SIOCSLIFZONE, (caddr_t)&lifr) < 0)
		return (ipadm_errno2status(errno));

	return (IPADM_SUCCESS);
}

/*
 * Callback function that gets the property `broadcast' for the address
 * object in `arg'.
 */
/* ARGSUSED */
static ipadm_status_t
i_ipadm_get_broadcast(ipadm_handle_t iph, const void *arg,
    ipadm_prop_desc_t *pdp, char *buf, uint_t *bufsize, uint_t af,
    uint_t valtype)
{
	struct sockaddr_in	*sin;
	struct lifreq		lifr;
	char			lifname[LIFNAMSIZ];
	ipadm_addrobj_t		ipaddr = (ipadm_addrobj_t)arg;
	ipadm_status_t		status;
	uint64_t		ifflags = 0;

	i_ipadm_addrobj2lifname(ipaddr, lifname, sizeof (lifname));
	if (ipaddr->ipadm_flags & IPMGMT_ACTIVE) {
		status = i_ipadm_get_flags(iph, lifname, af, &ifflags);
		if (status != IPADM_SUCCESS)
			return (status);
		if (!(ifflags & IFF_BROADCAST)) {
			buf[0] = '\0';
			*bufsize = 1;
			return (IPADM_SUCCESS);
		}
	}

	switch (valtype) {
	case MOD_PROP_DEFAULT: {
		struct sockaddr_storage	mask;
		struct in_addr		broadaddr;
		uint_t			plen;
		in_addr_t		addr, maddr;
		char			val[MAXPROPVALLEN];
		uint_t			valsz = MAXPROPVALLEN;
		ipadm_status_t		status;
		int			err;
		struct sockaddr_in	*sin;

		if (!(ipaddr->ipadm_flags & IPMGMT_ACTIVE)) {
			/*
			 * Since the address is unknown we cannot
			 * obtain default prefixlen
			 */
			if (ipaddr->ipadm_atype == IPADM_ADDR_DHCP ||
			    ipaddr->ipadm_af == AF_INET6) {
				buf[0] = '\0';
				*bufsize = 1;
				return (IPADM_SUCCESS);
			}
			/*
			 * For the static address, we get the address from the
			 * persistent db.
			 */
			status = i_ipadm_get_static_addr_db(iph, ipaddr, NULL);
			if (status != IPADM_SUCCESS)
				return (status);
			sin = SIN(&ipaddr->ipadm_static_addr);
			addr = sin->sin_addr.s_addr;
		} else {
			/*
			 * If the address object is active, we retrieve the
			 * address from kernel.
			 */
			bzero(&lifr, sizeof (lifr));
			(void) strlcpy(lifr.lifr_name, lifname,
			    sizeof (lifr.lifr_name));
			if (ioctl(iph->ih_sock, SIOCGLIFADDR,
			    (caddr_t)&lifr) < 0)
				return (ipadm_errno2status(errno));

			addr = (SIN(&lifr.lifr_addr))->sin_addr.s_addr;
		}
		/*
		 * For default broadcast address, get the address and the
		 * default prefixlen for that address and then compute the
		 * broadcast address.
		 */
		status = i_ipadm_get_prefixlen(iph, arg, NULL, val, &valsz, af,
		    MOD_PROP_DEFAULT);
		if (status != IPADM_SUCCESS)
			return (status);

		plen = atoi(val);
		if ((err = plen2mask(plen, AF_INET,
		    (struct sockaddr *)&mask)) != 0)
			return (ipadm_errno2status(err));
		maddr = (SIN(&mask))->sin_addr.s_addr;
		broadaddr.s_addr = (addr & maddr) | ~maddr;
		*bufsize = snprintf(buf, *bufsize, "%s", inet_ntoa(broadaddr));
		break;
	}
	case MOD_PROP_ACTIVE:
		bzero(&lifr, sizeof (lifr));
		(void) strlcpy(lifr.lifr_name, lifname,
		    sizeof (lifr.lifr_name));
		if (ioctl(iph->ih_sock, SIOCGLIFBRDADDR,
		    (caddr_t)&lifr) < 0) {
			return (ipadm_errno2status(errno));
		} else {
			sin = SIN(&lifr.lifr_addr);
			*bufsize = snprintf(buf, *bufsize, "%s",
			    inet_ntoa(sin->sin_addr));
		}
		break;
	default:
		return (IPADM_INVALID_ARG);
	}
	return (IPADM_SUCCESS);
}

/*
 * Callback function that retrieves the value of the property `prefixlen'
 * for the address object in `arg'.
 */
/* ARGSUSED */
static ipadm_status_t
i_ipadm_get_prefixlen(ipadm_handle_t iph, const void *arg,
    ipadm_prop_desc_t *pdp, char *buf, uint_t *bufsize, uint_t af,
    uint_t valtype)
{
	struct lifreq	lifr;
	ipadm_addrobj_t	ipaddr = (ipadm_addrobj_t)arg;
	char		lifname[LIFNAMSIZ];
	int		s;
	uint32_t	prefixlen;
	ipadm_status_t	status;
	uint64_t	lifflags;

	i_ipadm_addrobj2lifname(ipaddr, lifname, sizeof (lifname));
	if (ipaddr->ipadm_flags & IPMGMT_ACTIVE) {
		status = i_ipadm_get_flags(iph, lifname, af, &lifflags);
		if (status != IPADM_SUCCESS) {
			return (status);
		} else if (lifflags & IFF_POINTOPOINT) {
			buf[0] = '\0';
			*bufsize = 1;
			return (status);
		}
	}

	s = (af == AF_INET ? iph->ih_sock : iph->ih_sock6);
	bzero(&lifr, sizeof (lifr));
	(void) strlcpy(lifr.lifr_name, lifname, sizeof (lifr.lifr_name));
	switch (valtype) {
	case MOD_PROP_POSSIBLE:
		if (af == AF_INET)
			*bufsize = snprintf(buf, *bufsize, "1-30,32");
		else
			*bufsize = snprintf(buf, *bufsize, "1-126,128");
		break;
	case MOD_PROP_DEFAULT:
		if (ipaddr->ipadm_flags & IPMGMT_ACTIVE) {
			/*
			 * For static addresses, we retrieve the address
			 * from kernel if it is active.
			 */
			if (ioctl(s, SIOCGLIFADDR, (caddr_t)&lifr) < 0)
				return (ipadm_errno2status(errno));
			status = i_ipadm_get_default_prefixlen(
			    &lifr.lifr_addr, &prefixlen);
			if (status != IPADM_SUCCESS)
				return (status);
		} else if ((ipaddr->ipadm_flags & IPMGMT_PERSIST) &&
		    ipaddr->ipadm_atype == IPADM_ADDR_DHCP) {
			/*
			 * Since the address is unknown we cannot
			 * obtain default prefixlen
			 */
			buf[0] = '\0';
			*bufsize = 1;
			return (IPADM_SUCCESS);
		} else {
			/*
			 * If not in active config, we use the address
			 * from persistent store.
			 */
			status = i_ipadm_get_static_addr_db(iph, ipaddr, NULL);
			if (status != IPADM_SUCCESS)
				return (status);
			status = i_ipadm_get_default_prefixlen(
			    &ipaddr->ipadm_static_addr, &prefixlen);
			if (status != IPADM_SUCCESS)
				return (status);
		}
		*bufsize = snprintf(buf, *bufsize, "%u", prefixlen);
		break;
	case MOD_PROP_ACTIVE:
		if (ioctl(s, SIOCGLIFNETMASK, (caddr_t)&lifr) < 0)
			return (ipadm_errno2status(errno));
		prefixlen = lifr.lifr_addrlen;
		*bufsize = snprintf(buf, *bufsize, "%u", prefixlen);
		break;
	default:
		return (IPADM_INVALID_ARG);
	}
	return (IPADM_SUCCESS);
}

/*
 * Callback function that retrieves the value of one of the properties
 * among `deprecated', `private', and `transmit' for the address object
 * in `arg'.
 */
/* ARGSUSED */
static ipadm_status_t
i_ipadm_get_addr_flag(ipadm_handle_t iph, const void *arg,
    ipadm_prop_desc_t *pdp, char *buf, uint_t *bufsize, uint_t af,
    uint_t valtype)
{
	boolean_t	on = B_FALSE;
	char		lifname[LIFNAMSIZ];
	ipadm_status_t	status = IPADM_SUCCESS;
	uint64_t	ifflags;
	ipadm_addrobj_t	ipaddr = (ipadm_addrobj_t)arg;

	switch (valtype) {
	case MOD_PROP_DEFAULT:
		if (strcmp(pdp->ipd_name, "private") == 0 ||
		    strcmp(pdp->ipd_name, "deprecated") == 0) {
			on = B_FALSE;
		} else if (strcmp(pdp->ipd_name, "transmit") == 0) {
			on = B_TRUE;
		} else {
			return (IPADM_PROP_UNKNOWN);
		}
		break;
	case MOD_PROP_ACTIVE:
		/*
		 * If the address is present in active configuration, we
		 * retrieve it from kernel to get the property value.
		 * Else, there is no value to return.
		 */
		i_ipadm_addrobj2lifname(ipaddr, lifname, sizeof (lifname));
		status = i_ipadm_get_flags(iph, lifname, af, &ifflags);
		if (status != IPADM_SUCCESS)
			return (status);
		if (strcmp(pdp->ipd_name, "private") == 0)
			on = (ifflags & IFF_PRIVATE);
		else if (strcmp(pdp->ipd_name, "transmit") == 0)
			on = !(ifflags & IFF_NOXMIT);
		else if (strcmp(pdp->ipd_name, "deprecated") == 0)
			on = (ifflags & IFF_DEPRECATED);
		break;
	default:
		return (IPADM_INVALID_ARG);
	}
	*bufsize = strlcpy(buf, (on ? IPADM_ONSTR : IPADM_OFFSTR), *bufsize);
	return (status);
}

/*
 * Callback function that retrieves the value of the property
 * `reqhost' for the address object in `arg'.
 */
/* ARGSUSED */
static ipadm_status_t
i_ipadm_get_reqhost(ipadm_handle_t iph, const void *arg,
    ipadm_prop_desc_t *pdp, char *buf, uint_t *bufsize, uint_t af,
    uint_t valtype)
{
	ipadm_addrobj_t 	ipaddr = (ipadm_addrobj_t)arg;
	dhcp_ipc_reply_t	*dhreply = NULL;
	ipadm_status_t		status;
	dhcp_status_t		*dhstatus;
	int			dherror;
	size_t			reply_size;

	buf[0] = '\0';

	switch (valtype) {
	case MOD_PROP_DEFAULT:
		/*
		 * By default, no hostname is requested.
		 */
		*bufsize = 1;
		break;
	case MOD_PROP_ACTIVE:
		/*
		 * The property only applies to DHCPv4 address object types.
		 */
		if (ipaddr->ipadm_af == AF_INET6 ||
		    ipaddr->ipadm_atype != IPADM_ADDR_DHCP) {
			*bufsize = 1;
			return (IPADM_SUCCESS);
		}

		/*
		 * If a hostname was requested, then it can be retrieved
		 * from the dhcpagent via the DHCP_STATUS IPC.
		 */
		status = i_ipadm_op_dhcp(ipaddr, DHCP_STATUS, &dherror,
		    &dhreply);
		if (status != IPADM_SUCCESS) {
			*bufsize = 1;
			free(dhreply);
			return (IPADM_SUCCESS);
		}

		dhstatus = dhcp_ipc_get_data(dhreply, &reply_size, NULL);
		if (reply_size < DHCP_STATUS_VER3_SIZE) {
			*bufsize = 1;
			free(dhreply);
			return (IPADM_SUCCESS);
		}
		*bufsize = strlcpy(buf, dhstatus->if_reqhost, *bufsize);
		free(dhreply);
		break;
	default:
		return (IPADM_INVALID_ARG);
	}
	return (IPADM_SUCCESS);
}

/*
 * Callback function that retrieves the value of the property `zone'
 * for the address object in `arg'.
 */
/* ARGSUSED */
static ipadm_status_t
i_ipadm_get_zone(ipadm_handle_t iph, const void *arg,
    ipadm_prop_desc_t *pdp, char *buf, uint_t *bufsize, uint_t af,
    uint_t valtype)
{
	struct lifreq	lifr;
	char		zone_name[ZONENAME_MAX];
	int		s;

	if (iph->ih_zoneid != GLOBAL_ZONEID) {
		buf[0] = '\0';
		*bufsize = 1;
		return (IPADM_SUCCESS);
	}

	/*
	 * we are in global zone. See if the lifname is assigned to shared-ip
	 * zone or global zone.
	 */
	switch (valtype) {
	case MOD_PROP_DEFAULT:
		if (getzonenamebyid(GLOBAL_ZONEID, zone_name,
		    sizeof (zone_name)) > 0)
			*bufsize = snprintf(buf, *bufsize, "%s", zone_name);
		else
			return (ipadm_errno2status(errno));
		break;
	case MOD_PROP_ACTIVE:
		bzero(&lifr, sizeof (lifr));
		i_ipadm_addrobj2lifname((ipadm_addrobj_t)arg, lifr.lifr_name,
		    sizeof (lifr.lifr_name));
		s = (af == AF_INET ? iph->ih_sock : iph->ih_sock6);

		if (ioctl(s, SIOCGLIFZONE, (caddr_t)&lifr) == -1)
			return (ipadm_errno2status(errno));

		if (lifr.lifr_zoneid == ALL_ZONES) {
			*bufsize = snprintf(buf, *bufsize, "%s", "all-zones");
		} else if (getzonenamebyid(lifr.lifr_zoneid, zone_name,
		    sizeof (zone_name)) < 0) {
			return (ipadm_errno2status(errno));
		} else {
			*bufsize = snprintf(buf, *bufsize, "%s", zone_name);
		}
		break;
	default:
		return (IPADM_INVALID_ARG);
	}
	return (IPADM_SUCCESS);
}

static ipadm_prop_desc_t *
i_ipadm_get_addrprop_desc(const char *pname)
{
	int i;

	for (i = 0; ipadm_addrprop_table[i].ipd_name != NULL; i++) {
		if (strcmp(pname, ipadm_addrprop_table[i].ipd_name) == 0)
			return (&ipadm_addrprop_table[i]);
	}
	return (NULL);
}

/*
 * Gets the value of the given address property `pname' for the address
 * object with name `aobjname'.
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
ipadm_get_addrprop(ipadm_handle_t iph, const char *pname, char *buf,
    uint_t *bufsize, const char *aobjname, uint_t valtype)
{
	struct ipadm_addrobj_s	ipaddr;
	ipadm_status_t		status = IPADM_SUCCESS;
	sa_family_t		af;
	ipadm_prop_desc_t	*pdp = NULL;
	uint_t			psize;

	/*
	 * Validate the arguments of the function. Allow `buf' to be NULL
	 * only when `*bufsize' is zero as it can be used by the callers to
	 * determine the actual buffer size required.
	 */
	if (iph == NULL || pname == NULL || aobjname == NULL ||
	    bufsize == NULL || (buf == NULL && *bufsize != 0)) {
		return (IPADM_INVALID_ARG);
	}

	/* find the property in the property description table */
	if ((pdp = i_ipadm_get_addrprop_desc(pname)) == NULL)
		return (IPADM_PROP_UNKNOWN);

	/*
	 * For the given aobjname, get the addrobj it represents and
	 * retrieve the property value for that object.
	 */
	i_ipadm_init_addr(&ipaddr, "", aobjname, IPADM_ADDR_NONE);
	if ((status = i_ipadm_get_addrobj(iph, &ipaddr)) != IPADM_SUCCESS)
		return (status);

	if (ipaddr.ipadm_atype == IPADM_ADDR_IPV6_ADDRCONF)
		return (IPADM_OP_NOTSUP);
	af = ipaddr.ipadm_af;

	/*
	 * Call the appropriate callback function to based on the field
	 * that was asked for.
	 */
	psize = *bufsize;
	switch (valtype) {
	case IPADM_OPT_PERM:
		status = i_ipadm_pd2permstr(pdp, buf, &psize);
		break;
	case IPADM_OPT_ACTIVE:
		if (!(ipaddr.ipadm_flags & IPMGMT_ACTIVE)) {
			buf[0] = '\0';
			psize = 1;
		} else {
			status = pdp->ipd_get(iph, &ipaddr, pdp, buf, &psize,
			    af, MOD_PROP_ACTIVE);
		}
		break;
	case IPADM_OPT_DEFAULT:
		status = pdp->ipd_get(iph, &ipaddr, pdp, buf, &psize,
		    af, MOD_PROP_DEFAULT);
		break;
	case IPADM_OPT_POSSIBLE:
		if (pdp->ipd_get_range != NULL) {
			status = pdp->ipd_get_range(iph, &ipaddr, pdp, buf,
			    &psize, af, MOD_PROP_POSSIBLE);
			break;
		}
		buf[0] = '\0';
		psize = 1;
		break;
	case IPADM_OPT_PERSIST:
		status = i_ipadm_get_persist_propval(iph, pdp, buf, &psize,
		    &ipaddr);
		break;
	default:
		return (IPADM_INVALID_ARG);
	}

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
 * Sets the value of the given address property `pname' to `pval' for the
 * address object with name `aobjname'.
 */
ipadm_status_t
ipadm_set_addrprop(ipadm_handle_t iph, const char *pname,
    const char *pval, const char *aobjname, uint_t pflags)
{
	struct ipadm_addrobj_s	ipaddr;
	sa_family_t		af;
	ipadm_prop_desc_t	*pdp = NULL;
	char			defbuf[MAXPROPVALLEN];
	uint_t			defbufsize = MAXPROPVALLEN;
	boolean_t 		reset = (pflags & IPADM_OPT_DEFAULT);
	ipadm_status_t		status = IPADM_SUCCESS;

	/* Check for solaris.network.interface.config authorization */
	if (!ipadm_check_auth())
		return (IPADM_INSUFF_AUTH);

	if (iph == NULL || pname == NULL || aobjname == NULL || pflags == 0 ||
	    pflags == IPADM_OPT_PERSIST ||
	    (pflags & ~(IPADM_COMMON_OPT_MASK|IPADM_OPT_DEFAULT)) ||
	    (!reset && pval == NULL)) {
		return (IPADM_INVALID_ARG);
	}

	/* find the property in the property description table */
	if ((pdp = i_ipadm_get_addrprop_desc(pname)) == NULL)
		return (IPADM_PROP_UNKNOWN);

	if (pdp->ipd_set == NULL || (reset && pdp->ipd_get == NULL))
		return (IPADM_OP_NOTSUP);

	if (!(pdp->ipd_flags & IPADMPROP_MULVAL) &&
	    (pflags & (IPADM_OPT_APPEND|IPADM_OPT_REMOVE))) {
		return (IPADM_INVALID_ARG);
	}

	/*
	 * For the given aobjname, get the addrobj it represents and
	 * set the property value for that object.
	 */
	i_ipadm_init_addr(&ipaddr, "", aobjname, IPADM_ADDR_NONE);
	if ((status = i_ipadm_get_addrobj(iph, &ipaddr)) != IPADM_SUCCESS)
		return (status);

	if (!(ipaddr.ipadm_flags & IPMGMT_ACTIVE))
		return (IPADM_OP_DISABLE_OBJ);

	/* Persistent operation not allowed on a temporary object. */
	if ((pflags & IPADM_OPT_PERSIST) &&
	    !(ipaddr.ipadm_flags & IPMGMT_PERSIST))
		return (IPADM_TEMPORARY_OBJ);

	/*
	 * Currently, setting an address property on an address object of type
	 * IPADM_ADDR_IPV6_ADDRCONF is not supported. Supporting it involves
	 * in.ndpd retrieving the address properties from ipmgmtd for given
	 * address object and then setting them on auto-configured addresses,
	 * whenever in.ndpd gets a new prefix. This will be supported in
	 * future releases.
	 */
	if (ipaddr.ipadm_atype == IPADM_ADDR_IPV6_ADDRCONF)
		return (IPADM_OP_NOTSUP);

	/*
	 * Setting an address property on an address object that is
	 * not present in active configuration is not supported.
	 */
	if (!(ipaddr.ipadm_flags & IPMGMT_ACTIVE))
		return (IPADM_OP_NOTSUP);

	af = ipaddr.ipadm_af;
	if (reset) {
		/*
		 * If we were asked to reset the value, we need to fetch
		 * the default value and set the default value.
		 */
		status = pdp->ipd_get(iph, &ipaddr, pdp, defbuf, &defbufsize,
		    af, MOD_PROP_DEFAULT);
		if (status != IPADM_SUCCESS)
			return (status);
		pval = defbuf;
	}
	/* set the user provided or default property value */
	status = pdp->ipd_set(iph, &ipaddr, pdp, pval, af, pflags);
	if (status != IPADM_SUCCESS)
		return (status);

	/*
	 * If IPADM_OPT_PERSIST was set in `flags', we need to store
	 * property and its value in persistent DB.
	 */
	if (pflags & IPADM_OPT_PERSIST) {
		status = i_ipadm_persist_propval(iph, pdp, pval, &ipaddr,
		    pflags);
	}

	return (status);
}

/*
 * This function is called after deleting an IPv4 test address, either static
 * or dhcp address. It makes sure that the interface is brought up, if the
 * deleted address was the last address on the interface, to make it usable
 * in the IPMP group. We don't do this for IPv6, because a :: IPv6 address can
 * not be brought up. To make the IPv6 half of the IPMP group usable again,
 * the admin has to configure a link-local on the underlying interface in
 * `addr->ipadm_ifname'.
 */
ipadm_status_t
i_ipadm_up_underif(ipadm_handle_t iph, ipadm_addrobj_t addr)
{
	struct ifaddrs	*ifa;
	ipadm_status_t	status = IPADM_SUCCESS;

	if (addr->ipadm_af != AF_INET)
		return (IPADM_SUCCESS);
	if (addr->ipadm_lifnum == 0) {
		status = i_ipadm_set_flags(iph, addr->ipadm_ifname,
		    AF_INET, 0, IFF_DEPRECATED|IFF_NOFAILOVER);
		if (status != IPADM_SUCCESS)
			return (status);
	}
	if (getallifaddrs(AF_INET, &ifa, LIFC_UNDER_IPMP, 0) != 0)
		return (ipadm_errno2status(errno));
	if (i_ipadm_is_if_down(addr->ipadm_ifname, ifa)) {
		status = i_ipadm_set_flags(iph, addr->ipadm_ifname,
		    AF_INET, IFF_UP, 0);
	}
	freeifaddrs(ifa);
	return (status);
}

/*
 * Remove the address specified by the address object in `addr'
 * from kernel. If the address is on a non-zero logical interface, we do a
 * SIOCLIFREMOVEIF, otherwise we set the address to INADDR_ANY for IPv4 or
 * :: for IPv6.
 */
ipadm_status_t
i_ipadm_delete_addr(ipadm_handle_t iph, ipadm_addrobj_t addr)
{
	struct lifreq	lifr;
	int		sock;
	ipadm_status_t	status;

	bzero(&lifr, sizeof (lifr));
	i_ipadm_addrobj2lifname(addr, lifr.lifr_name, sizeof (lifr.lifr_name));
	sock = IPADM_SOCK(iph, addr->ipadm_af);
	if (addr->ipadm_lifnum == 0) {
		/*
		 * Fake the deletion of the 0'th address by
		 * clearing IFF_UP and setting it to as 0.0.0.0 or ::.
		 * Note that setting the address to 0.0.0.0 or ::
		 * is not allowed for addresses created by ifconfig
		 * and will result in EADDRNOTAVAIL. We ignore this
		 * error and drive on.
		 */
		status = i_ipadm_set_flags(iph, addr->ipadm_ifname,
		    addr->ipadm_af, 0, IFF_UP);
		if (status != IPADM_SUCCESS)
			return (status);
		bzero(&lifr.lifr_addr, sizeof (lifr.lifr_addr));
		lifr.lifr_addr.ss_family = addr->ipadm_af;
		if (ioctl(sock, SIOCSLIFADDR, (caddr_t)&lifr) < 0 &&
		    errno != EADDRNOTAVAIL)
			return (ipadm_errno2status(errno));
		/*
		 * We do not zero the destination addresses for addrconf
		 * addresses, because we might not be able to bring them
		 * up again (in the case where default destination tokens
		 * are being used). Not zeroing out the address has no
		 * adverse effect.
		 */
		if (addr->ipadm_atype != IPADM_ADDR_IPV6_ADDRCONF) {
			if (ioctl(sock, SIOCSLIFDSTADDR, (caddr_t)&lifr) < 0)
				return (ipadm_errno2status(errno));
		}
	} else if (ioctl(sock, SIOCLIFREMOVEIF, (caddr_t)&lifr) < 0) {
		return (ipadm_errno2status(errno));
	}
	/*
	 * If it happens to be an underlying interface and there
	 * is no IFF_UP address left on it, bring up the interface
	 * to make it usable in the group.
	 */
	if (!i_ipadm_is_legacy(iph) &&
	    i_ipadm_is_under_ipmp(iph, addr->ipadm_ifname))
		return (i_ipadm_up_underif(iph, addr));

	return (IPADM_SUCCESS);
}

/*
 * Extracts the IPv6 address from the nvlist in `nvl'.
 */
ipadm_status_t
i_ipadm_nvl2in6_addr(nvlist_t *nvl, char *addr_type, in6_addr_t *in6_addr)
{
	uint8_t	*addr6;
	uint_t	n;

	if (nvlist_lookup_uint8_array(nvl, addr_type, &addr6, &n) != 0)
		return (IPADM_OBJ_NOTFOUND);
	assert(n == 16);
	bcopy(addr6, in6_addr->s6_addr, n);
	return (IPADM_SUCCESS);
}

/*
 * Used to validate the given addrobj name string. Length of `aobjname'
 * cannot exceed IPADM_AOBJ_USTRSIZ. `aobjname' should start with an
 * alphabetic character and it can only contain alphanumeric characters.
 */
static boolean_t
i_ipadm_is_user_aobjname_valid(const char *aobjname)
{
	const char	*cp;

	if (aobjname == NULL || strlen(aobjname) >= IPADM_AOBJ_USTRSIZ ||
	    !isalpha(*aobjname)) {
		return (B_FALSE);
	}
	for (cp = aobjname + 1; *cp && isalnum(*cp); cp++)
		;
	return (*cp == '\0');
}

/*
 * Computes the prefixlen for the given `addr' based on the netmask found using
 * the order specified in /etc/nsswitch.conf. If not found, then the
 * prefixlen is computed using the Classful subnetting semantics defined
 * in RFC 791 for IPv4 and RFC 4291 for IPv6.
 */
static ipadm_status_t
i_ipadm_get_default_prefixlen(struct sockaddr_storage *addr, uint32_t *plen)
{
	sa_family_t af = addr->ss_family;
	struct sockaddr_storage mask;
	struct sockaddr_in *m = (struct sockaddr_in *)&mask;
	struct sockaddr_in6 *sin6;
	struct sockaddr_in *sin;
	struct in_addr ia;
	uint32_t prefixlen = 0;

	switch (af) {
	case AF_INET:
		sin = SIN(addr);
		ia.s_addr = ntohl(sin->sin_addr.s_addr);
		get_netmask4(&ia, &m->sin_addr);
		m->sin_addr.s_addr = htonl(m->sin_addr.s_addr);
		m->sin_family = AF_INET;
		prefixlen = mask2plen((struct sockaddr *)&mask);
		assert((int)prefixlen >= 0);
		break;
	case AF_INET6:
		sin6 = SIN6(addr);
		if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr))
			prefixlen = 10;
		else
			prefixlen = 64;
		break;
	default:
		return (IPADM_INVALID_ARG);
	}
	*plen = prefixlen;
	return (IPADM_SUCCESS);
}

ipadm_status_t
i_ipadm_resolve_addr(const char *name, sa_family_t af,
    struct sockaddr_storage *ss)
{
	struct addrinfo hints, *ai;
	int rc;
	struct sockaddr_in6 *sin6;
	struct sockaddr_in *sin;
	boolean_t is_mapped;

	(void) memset(&hints, 0, sizeof (hints));
	hints.ai_family = af;
	hints.ai_flags = (AI_ALL | AI_V4MAPPED);
	rc = getaddrinfo(name, NULL, &hints, &ai);
	if (rc != 0) {
		if (rc == EAI_NONAME)
			return (IPADM_BAD_ADDR);
		else
			return (IPADM_FAILURE);
	}
	if (ai->ai_next != NULL) {
		/* maps to more than one hostname */
		freeaddrinfo(ai);
		return (IPADM_HOSTNAME_TO_MULTADDR);
	}
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	is_mapped = IN6_IS_ADDR_V4MAPPED(&(SIN6(ai->ai_addr))->sin6_addr);
	if (is_mapped) {
		sin = SIN(ss);
		sin->sin_family = AF_INET;
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		IN6_V4MAPPED_TO_INADDR(&(SIN6(ai->ai_addr))->sin6_addr,
		    &sin->sin_addr);
	} else {
		sin6 = SIN6(ss);
		sin6->sin6_family = AF_INET6;
		bcopy(ai->ai_addr, sin6, sizeof (*sin6));
	}
	freeaddrinfo(ai);
	return (IPADM_SUCCESS);
}

/*
 * This takes a static address string <addr>[/<mask>] or a hostname
 * and maps it to a single numeric IP address, consulting DNS if
 * hostname was provided. If a specific address family was requested,
 * an error is returned if the given hostname does not map to an address
 * of the given family. Note that this function returns failure
 * if the name maps to more than one IP address.
 */
ipadm_status_t
ipadm_set_addr(ipadm_addrobj_t ipaddr, const char *astr, sa_family_t af)
{
	char		*prefixlenstr;
	uint32_t	prefixlen = 0;
	char		*endp;
	/*
	 * We use (NI_MAXHOST + 5) because the longest possible
	 * astr will have (NI_MAXHOST + '/' + {a maximum of 32 for IPv4
	 * or a maximum of 128 for IPv6 + '\0') chars
	 */
	char		addrstr[NI_MAXHOST + 5];
	ipadm_status_t	status;

	(void) snprintf(addrstr, sizeof (addrstr), "%s", astr);
	if ((prefixlenstr = strchr(addrstr, '/')) != NULL) {
		*prefixlenstr++ = '\0';
		errno = 0;
		prefixlen = strtoul(prefixlenstr, &endp, 10);
		if (errno != 0 || *endp != '\0')
			return (IPADM_INVALID_ARG);
		if ((af == AF_INET && prefixlen > IP_ABITS) ||
		    (af == AF_INET6 && prefixlen > IPV6_ABITS))
			return (IPADM_INVALID_ARG);
	}

	status = i_ipadm_resolve_addr(addrstr, af, &ipaddr->ipadm_static_addr);
	if (status == IPADM_SUCCESS) {
		(void) strlcpy(ipaddr->ipadm_static_aname, addrstr,
		    sizeof (ipaddr->ipadm_static_aname));
		ipaddr->ipadm_af = ipaddr->ipadm_static_addr.ss_family;
		ipaddr->ipadm_static_prefixlen = prefixlen;
	}
	return (status);
}

/*
 * Gets the static source address from the address object in `ipaddr'.
 * Memory for `addr' should be already allocated by the caller.
 */
ipadm_status_t
ipadm_get_addr(const ipadm_addrobj_t ipaddr, struct sockaddr_storage *addr)
{
	if (ipaddr == NULL || ipaddr->ipadm_atype != IPADM_ADDR_STATIC ||
	    addr == NULL) {
		return (IPADM_INVALID_ARG);
	}
	*addr = ipaddr->ipadm_static_addr;

	return (IPADM_SUCCESS);
}
/*
 * Set up tunnel destination address in ipaddr by contacting DNS.
 * The function works similar to ipadm_set_addr().
 * The dst_addr must resolve to exactly one address. IPADM_BAD_ADDR is returned
 * if dst_addr resolves to more than one address. The caller has to verify
 * that ipadm_static_addr and ipadm_static_dst_addr have the same ss_family
 */
ipadm_status_t
ipadm_set_dst_addr(ipadm_addrobj_t ipaddr, const char *daddrstr, sa_family_t af)
{
	ipadm_status_t	status;

	/* mask lengths are not meaningful for point-to-point interfaces. */
	if (strchr(daddrstr, '/') != NULL)
		return (IPADM_BAD_ADDR);

	status = i_ipadm_resolve_addr(daddrstr, af,
	    &ipaddr->ipadm_static_dst_addr);
	if (status == IPADM_SUCCESS) {
		(void) strlcpy(ipaddr->ipadm_static_dname, daddrstr,
		    sizeof (ipaddr->ipadm_static_dname));
		if (ipaddr->ipadm_af == AF_UNSPEC)
			ipaddr->ipadm_af = af;
	}
	return (status);
}

/*
 * Private interface used by ifconfig(1M) to retrieve the interface id
 * for the specified aobjname.
 */
ipadm_status_t
ipadm_get_interface_id(ipadm_handle_t iph, const char *aobjname,
    struct sockaddr_in6 *sin6, int *prefixlen, uint32_t flags)
{
	ipadm_status_t		status;
	struct ipadm_addrobj_s	ipaddr;

	/* validate input */
	if (!(flags & IPADM_OPT_ACTIVE))
		return (IPADM_INVALID_ARG);

	bzero(&ipaddr, sizeof (ipaddr));
	if (aobjname == NULL || strlcpy(ipaddr.ipadm_aobjname, aobjname,
	    IPADM_AOBJSIZ) >= IPADM_AOBJSIZ)
		return (IPADM_INVALID_ARG);

	/* Retrieve the address object information from ipmgmtd. */
	status = i_ipadm_get_addrobj(iph, &ipaddr);
	if (status != IPADM_SUCCESS)
		return (status);

	if (!(ipaddr.ipadm_flags & IPMGMT_ACTIVE))
		return (IPADM_OBJ_NOTFOUND);

	if (ipaddr.ipadm_atype != IPADM_ADDR_IPV6_ADDRCONF)
		return (IPADM_OP_NOTSUP);

	bzero(sin6, sizeof (struct sockaddr_in6));
	sin6->sin6_addr = ipaddr.ipadm_intfid.sin6_addr;
	sin6->sin6_family = AF_INET6;
	*prefixlen = ipaddr.ipadm_intfidlen;
	return (IPADM_SUCCESS);
}

/*
 * Performs validation of the input flags and address object for
 * ipadm_update_addr(). It also retrieves the address object for the
 * input address object name in `mod_ipaddr' and if found, returns it
 * in `ipaddr'.
 */
static ipadm_status_t
i_ipadm_update_addr_common(ipadm_handle_t iph, ipadm_addrobj_t mod_ipaddr,
    ipadm_addrobj_t ipaddr, uint32_t flags)
{
	ipadm_status_t		status;
	char			lifname[LIFNAMSIZ];

	/* check for solaris.network.interface.config authorization */
	if (!ipadm_check_auth())
		return (IPADM_INSUFF_AUTH);

	if (!i_ipadm_is_legacy(iph))
		return (IPADM_OP_NOTSUP);

	/* validate input */
	if ((!(flags & IPADM_OPT_ACTIVE)) ||
	    (mod_ipaddr->ipadm_atype != IPADM_ADDR_IPV6_ADDRCONF &&
	    mod_ipaddr->ipadm_atype != IPADM_ADDR_STATIC) ||
	    (mod_ipaddr->ipadm_aobjname == NULL))
		return (IPADM_INVALID_ARG);

	/*  Get the addrobj for this aobjname */
	*ipaddr = *mod_ipaddr;
	if (mod_ipaddr->ipadm_lifnum > 0) {
		(void) snprintf(lifname, LIFNAMSIZ, "%s:%d",
		    mod_ipaddr->ipadm_ifname, mod_ipaddr->ipadm_lifnum);
	} else {
		(void) strlcpy(lifname, mod_ipaddr->ipadm_ifname, LIFNAMSIZ);
	}
	status = i_ipadm_get_lif2addrobj(iph, lifname, mod_ipaddr->ipadm_af,
	    ipaddr);
	if (status != IPADM_SUCCESS)
		return (IPADM_INVALID_ARG);

	/* Retrieve the address object information from ipmgmtd */
	status = i_ipadm_get_addrobj(iph, ipaddr);
	if (status != IPADM_SUCCESS)
		return (status);

	if (ipaddr->ipadm_atype != mod_ipaddr->ipadm_atype)
		return (IPADM_OP_NOTSUP);

	if ((flags & IPADM_OPT_PERSIST) &&
	    !(ipaddr->ipadm_flags & IPMGMT_PERSIST))
		return (IPADM_TEMPORARY_OBJ);
	return (IPADM_SUCCESS);
}

/*
 * Private interface used by ifconfig(1M) to modify the local and remote
 * address for the specified address object.
 */
ipadm_status_t
ipadm_update_addr(ipadm_handle_t iph, ipadm_addrobj_t mod_ipaddr,
    uint32_t flags)
{
	ipadm_status_t		status;
	struct ipadm_addrobj_s	ipaddr;
	struct lifreq		lifr;
	int			sock;
	boolean_t		lcl_set = B_FALSE;
	boolean_t		dst_set = B_FALSE;
	boolean_t		is_up;
	uint32_t		ipadm_flags;

	status = i_ipadm_update_addr_common(iph, mod_ipaddr, &ipaddr,
	    flags);
	if (status != IPADM_SUCCESS)
		return (status);
	if (mod_ipaddr->ipadm_static_addr.ss_family != AF_UNSPEC)
		lcl_set = B_TRUE;
	if (mod_ipaddr->ipadm_static_dst_addr.ss_family != AF_UNSPEC)
		dst_set = B_TRUE;
	if (lcl_set) {
		/*
		 * i_ipadm_create_addr() sets the local address
		 * and the destination address if one is available.
		 * We mask off the IPADM_OPT_PERSIST flag because we will
		 * handle persistence later in this function.
		 */
		ipadm_flags = (flags & ~IPADM_OPT_PERSIST);
		status = i_ipadm_create_addr(iph, &ipaddr, ipadm_flags);
		if (status != IPADM_SUCCESS)
			return (status);
	} else if (dst_set) {
		/*
		 * In this case, the local address is not being modified.
		 * We don't call i_ipadm_create_addr() because it will
		 * over-write the local address.
		 */
		i_ipadm_addrobj2lifname(&ipaddr, lifr.lifr_name,
		    sizeof (lifr.lifr_name));
		sock = IPADM_SOCK(iph, mod_ipaddr->ipadm_af);
		lifr.lifr_addr = mod_ipaddr->ipadm_static_dst_addr;
		if (ioctl(sock, SIOCSLIFDSTADDR, (caddr_t)&lifr) < 0)
			return (ipadm_errno2status(errno));
	} else {
		return (IPADM_INVALID_ARG);
	}

	/*
	 * If the change to the address object is persistent, we retrieve
	 * the persistent address object, delete it from the persistent
	 * store and persist it again with the updated address.
	 * Since there is no door call available to update a persistent
	 * address object in place, we are forced to do the above.
	 */
	if (flags & IPADM_OPT_PERSIST) {
		is_up = B_FALSE;
		status = i_ipadm_get_static_addr_db(iph, &ipaddr, &is_up);
		if (status != IPADM_SUCCESS)
			return (status);
		status = i_ipadm_delete_addrobj(iph, &ipaddr, flags);
		if (status != IPADM_SUCCESS)
			return (status);
		if (lcl_set) {
			(void) strlcpy(ipaddr.ipadm_static_aname,
			    mod_ipaddr->ipadm_static_aname,
			    sizeof (ipaddr.ipadm_static_aname));
			ipaddr.ipadm_static_addr =
			    mod_ipaddr->ipadm_static_addr;
		}
		if (dst_set) {
			(void) strlcpy(ipaddr.ipadm_static_dname,
			    mod_ipaddr->ipadm_static_dname,
			    sizeof (ipaddr.ipadm_static_dname));
			ipaddr.ipadm_static_dst_addr =
			    mod_ipaddr->ipadm_static_dst_addr;
		}
		ipaddr.ipadm_aobjname[0] = '\0';
		status = i_ipadm_lookupadd_addrobj(iph, &ipaddr);
		if (status != IPADM_SUCCESS)
			return (status);
		if (is_up)
			flags |= IPADM_OPT_UP;
		return (i_ipadm_addr_persist(iph, &ipaddr, B_TRUE, flags));
	}
	return (IPADM_SUCCESS);
}

/*
 * Private interface used by ifconfig(1M) to modify the interface id(s)
 * for the specified aobjname.
 */
ipadm_status_t
ipadm_update_interface_id(ipadm_handle_t iph, ipadm_addrobj_t mod_ipaddr,
    uint32_t flags)
{
	ipadm_status_t		status;
	struct ipadm_addrobj_s	ipaddr;

	status = i_ipadm_update_addr_common(iph, mod_ipaddr, &ipaddr, flags);
	if (status != IPADM_SUCCESS)
		return (status);

	/* Update the address object with updated ids */
	if (mod_ipaddr->ipadm_intfidlen != 0) {
		ipaddr.ipadm_intfid = mod_ipaddr->ipadm_intfid;
		ipaddr.ipadm_intfidlen = mod_ipaddr->ipadm_intfidlen;
	}
	if (mod_ipaddr->ipadm_dintfid.sin6_family != AF_UNSPEC)
		ipaddr.ipadm_dintfid = mod_ipaddr->ipadm_dintfid;

	status = i_ipadm_set_linklocal(iph, &ipaddr);
	if (status != IPADM_SUCCESS)
		return (status);

	status = i_ipadm_send_ndpd_cmd(ipaddr.ipadm_ifname, &ipaddr,
	    IPADM_MODIFY_ADDRS);
	if (status != IPADM_SUCCESS && status != IPADM_NDPD_NOT_RUNNING)
		return (status);
	/*
	 * If the change to the interface ID is persistent, we retrieve
	 * the persistent address object, delete it from the persistent
	 * store and persist it again with the updated interface ID.
	 * Since there is no door call available to update a persistent
	 * address object in place, we are forced to do the above.
	 */
	if (flags & IPADM_OPT_PERSIST) {
		status = i_ipadm_get_addrconf_db(iph, &ipaddr);
		if (status != IPADM_SUCCESS)
			return (status);
		status = i_ipadm_delete_addrobj(iph, &ipaddr, flags);
		if (status != IPADM_SUCCESS)
			return (status);
		if (mod_ipaddr->ipadm_intfidlen != 0) {
			ipaddr.ipadm_intfid = mod_ipaddr->ipadm_intfid;
			ipaddr.ipadm_intfidlen = mod_ipaddr->ipadm_intfidlen;
		}
		if (mod_ipaddr->ipadm_dintfid.sin6_family != AF_UNSPEC)
			ipaddr.ipadm_dintfid = mod_ipaddr->ipadm_dintfid;
		return (i_ipadm_addr_persist(iph, &ipaddr, B_FALSE,
		    flags));
	}
	/*
	 * The IPADM_OPT_MODIFY flag lets ipmgmtd know that it
	 * should only update the active configuration even though
	 * the addr is persistent.
	 */
	if (ipaddr.ipadm_flags & IPMGMT_PERSIST)
		flags |= IPADM_OPT_MODIFY;

	return (i_ipadm_addr_persist(iph, &ipaddr, B_FALSE, flags));
}

/*
 * Remove any prefix from the interface id.
 */
static boolean_t
i_ipadm_mask_interface_id(struct in6_addr *intfid)
{
	int i = 0;

	/*
	 * The only valid prefix is the linklocal prefix (or none at all).
	 */
	if (IN6_IS_ADDR_LINKLOCAL(intfid)) {
		for (i = 0; i < TOKEN_PREFIXLEN/8; i++) {
			intfid->s6_addr[i] = 0;
		}
	} else {
		for (i = 0; i < TOKEN_PREFIXLEN/8; i++) {
			if (intfid->s6_addr[i] != 0)
				return (B_FALSE);
		}
	}
	return (B_TRUE);
}

/*
 * Sets the interface ID in the address object `ipaddr' with the address
 * in the string `interface_id'. This interface ID will be used when
 * ipadm_create_addr() is called with `ipaddr' with address type
 * set to IPADM_ADDR_IPV6_ADDRCONF.
 */
ipadm_status_t
ipadm_set_interface_id(ipadm_addrobj_t ipaddr, const char *interface_id)
{
	struct sockaddr_in6	*sin6;
	char			*end;
	char			*cp;
	uint32_t		prefixlen;
	char			addrstr[INET6_ADDRSTRLEN + 1];

	if (ipaddr == NULL || interface_id == NULL ||
	    ipaddr->ipadm_atype != IPADM_ADDR_IPV6_ADDRCONF)
		return (IPADM_INVALID_ARG);

	(void) strlcpy(addrstr, interface_id, sizeof (addrstr));

	/*
	 * Interface ids should always have perfixlen of 64 bits. If provided,
	 * then verify it. Otherwise, default it.
	 */
	if ((cp = strchr(addrstr, '/')) != NULL) {
		*cp++ = '\0';
		errno = 0;
		prefixlen = strtoul(cp, &end, 10);
		if (errno != 0 || *end != '\0' || prefixlen != TOKEN_PREFIXLEN)
			return (IPADM_INVALID_IFID);
	} else {
		prefixlen = TOKEN_PREFIXLEN;
	}

	sin6 = &ipaddr->ipadm_intfid;
	if (inet_pton(AF_INET6, addrstr, &sin6->sin6_addr) == 1 &&
	    i_ipadm_mask_interface_id(&sin6->sin6_addr)) {
		sin6->sin6_family = AF_INET6;
		ipaddr->ipadm_intfidlen = prefixlen;
		return (IPADM_SUCCESS);
	}
	return (IPADM_INVALID_IFID);
}

/*
 * Sets the destination interface ID in the address object `ipaddr' with the
 * address in the string `interface_id'.
 */
ipadm_status_t
ipadm_set_dst_interface_id(ipadm_addrobj_t ipaddr, const char *interface_id)
{
	struct sockaddr_in6	*sin6;

	if (ipaddr == NULL || interface_id == NULL ||
	    ipaddr->ipadm_atype != IPADM_ADDR_IPV6_ADDRCONF)
		return (IPADM_INVALID_ARG);

	/* mask lengths are not meaningful for point-to-point interfaces */
	if (strchr(interface_id, '/') != NULL)
		return (IPADM_BAD_ADDR);

	sin6 = &ipaddr->ipadm_dintfid;
	if (inet_pton(AF_INET6, interface_id, &sin6->sin6_addr) == 1 &&
	    i_ipadm_mask_interface_id(&sin6->sin6_addr)) {
		sin6->sin6_family = AF_INET6;
		return (IPADM_SUCCESS);
	}
	return (IPADM_INVALID_IFID);
}

/*
 * Sets the value for the field `ipadm_stateless' in address object `ipaddr'.
 */
ipadm_status_t
ipadm_set_stateless(ipadm_addrobj_t ipaddr, boolean_t stateless)
{
	if (ipaddr == NULL ||
	    ipaddr->ipadm_atype != IPADM_ADDR_IPV6_ADDRCONF)
		return (IPADM_INVALID_ARG);
	ipaddr->ipadm_stateless = stateless;

	return (IPADM_SUCCESS);
}

/*
 * Sets the value for the field `ipadm_stateful' in address object `ipaddr'.
 */
ipadm_status_t
ipadm_set_stateful(ipadm_addrobj_t ipaddr, boolean_t stateful)
{
	if (ipaddr == NULL ||
	    ipaddr->ipadm_atype != IPADM_ADDR_IPV6_ADDRCONF)
		return (IPADM_INVALID_ARG);
	ipaddr->ipadm_stateful = stateful;

	return (IPADM_SUCCESS);
}

/*
 * Sets the dhcp parameter `ipadm_primary' in the address object `ipaddr'.
 * The field is used during the address creation with address
 * type IPADM_ADDR_DHCP. It specifies if the interface should be set
 * as a primary interface for getting dhcp global options from the DHCP server.
 */
ipadm_status_t
ipadm_set_primary(ipadm_addrobj_t ipaddr, boolean_t primary)
{
	if (ipaddr == NULL || ipaddr->ipadm_atype != IPADM_ADDR_DHCP)
		return (IPADM_INVALID_ARG);
	ipaddr->ipadm_primary = primary;

	return (IPADM_SUCCESS);
}

/*
 * Sets the dhcp parameter `ipadm_reqhost' in the address object `ipaddr'.
 * This field is used during the address creation with address type
 * IPADM_ADDR_DHCP. It specifies the DNS hostname that the client is
 * requesting to be updated with the IP address acquired by the dhcpagent.
 */
ipadm_status_t
ipadm_set_reqhost(ipadm_addrobj_t ipaddr, const char *hostname)
{
	int i;

	if (ipaddr == NULL || ipaddr->ipadm_atype != IPADM_ADDR_DHCP)
		return (IPADM_INVALID_ARG);

	for (i = 0; hostname[i] != '\0'; i++) {
		if (isalpha(hostname[i]) || isdigit(hostname[i]) ||
		    (((hostname[i] == '-') || (hostname[i] == '.')) &&
		    (i > 0)))
			continue;
		return (IPADM_INVALID_ARG);
	}
	if (i == 0 || i >= sizeof (ipaddr->ipadm_reqhost))
		return (IPADM_INVALID_ARG);

	(void) strlcpy(ipaddr->ipadm_reqhost, hostname,
	    sizeof (ipaddr->ipadm_reqhost));
	return (IPADM_SUCCESS);
}

/*
 * Sets the dhcp parameter `ipadm_wait' in the address object `ipaddr'.
 * This field is used during the address creation with address type
 * IPADM_ADDR_DHCP. It specifies how long the API ipadm_create_addr()
 * should wait before returning while the dhcp address is being acquired
 * by the dhcpagent.
 * Possible values:
 * - IPADM_DHCP_WAIT_FOREVER : Do not return until dhcpagent returns.
 * - IPADM_DHCP_WAIT_DEFAULT : Wait a default amount of time before returning.
 * - <integer>	   : Wait the specified number of seconds before returning.
 */
ipadm_status_t
ipadm_set_wait_time(ipadm_addrobj_t ipaddr, int32_t wait)
{
	if (ipaddr == NULL || ipaddr->ipadm_atype != IPADM_ADDR_DHCP)
		return (IPADM_INVALID_ARG);
	ipaddr->ipadm_wait = wait;
	return (IPADM_SUCCESS);
}

/*
 * Creates a placeholder for the `ipadm_aobjname' in the ipmgmtd `aobjmap'.
 * If the `aobjname' already exists in the daemon's `aobjmap' then
 * IPADM_ADDROBJ_EXISTS will be returned.
 *
 * If the libipadm consumer set `ipaddr.ipadm_aobjname[0]' to `\0', then the
 * daemon will generate an `aobjname' for the given `ipaddr'.
 */
ipadm_status_t
i_ipadm_lookupadd_addrobj(ipadm_handle_t iph, ipadm_addrobj_t ipaddr)
{
	ipmgmt_aobjop_arg_t	larg;
	ipmgmt_aobjop_rval_t	rval;
	int			err;

	bzero(&larg, sizeof (larg));
	larg.ia_cmd = IPMGMT_CMD_ADDROBJ_LOOKUPADD;
	(void) strlcpy(larg.ia_aobjname, ipaddr->ipadm_aobjname,
	    sizeof (larg.ia_aobjname));
	(void) strlcpy(larg.ia_ifname, ipaddr->ipadm_ifname,
	    sizeof (larg.ia_ifname));
	larg.ia_family = ipaddr->ipadm_af;
	larg.ia_atype = ipaddr->ipadm_atype;

	err = ipadm_door_call(iph, &larg, sizeof (larg), &rval, sizeof (rval));
	if (err == 0 && ipaddr->ipadm_aobjname[0] == '\0') {
		/* copy the daemon generated `aobjname' into `ipadddr' */
		(void) strlcpy(ipaddr->ipadm_aobjname, rval.ir_aobjname,
		    sizeof (ipaddr->ipadm_aobjname));
	}
	if (err == EEXIST)
		return (IPADM_ADDROBJ_EXISTS);
	return (ipadm_errno2status(err));
}

/*
 * Sets the logical interface number in the ipmgmtd's memory map for the
 * address object `ipaddr'. If another address object has the same
 * logical interface number, IPADM_ADDROBJ_EXISTS is returned.
 */
ipadm_status_t
i_ipadm_setlifnum_addrobj(ipadm_handle_t iph, ipadm_addrobj_t ipaddr)
{
	ipmgmt_aobjop_arg_t	larg;
	ipmgmt_retval_t		rval;
	int			err;

	if (iph->ih_flags & IH_IPMGMTD)
		return (IPADM_SUCCESS);

	bzero(&larg, sizeof (larg));
	larg.ia_cmd = IPMGMT_CMD_ADDROBJ_SETLIFNUM;
	(void) strlcpy(larg.ia_aobjname, ipaddr->ipadm_aobjname,
	    sizeof (larg.ia_aobjname));
	larg.ia_lnum = ipaddr->ipadm_lifnum;
	(void) strlcpy(larg.ia_ifname, ipaddr->ipadm_ifname,
	    sizeof (larg.ia_ifname));
	larg.ia_family = ipaddr->ipadm_af;

	err = ipadm_door_call(iph, &larg, sizeof (larg), &rval, sizeof (rval));
	if (err == EEXIST)
		return (IPADM_ADDROBJ_EXISTS);
	return (ipadm_errno2status(err));
}

/*
 * Creates the IPv4 or IPv6 address in the nvlist `nvl' on the interface
 * `ifname'. If a hostname is present, it is resolved before the address
 * is created.
 */
ipadm_status_t
i_ipadm_enable_static(ipadm_handle_t iph, const char *ifname, nvlist_t *nvl,
    sa_family_t af)
{
	char			*prefixlenstr = NULL;
	char			*upstr = NULL;
	char			*sname = NULL, *dname = NULL;
	struct ipadm_addrobj_s	ipaddr;
	char			*aobjname = NULL;
	nvlist_t		*nvaddr = NULL;
	nvpair_t		*nvp;
	char			*cidraddr;
	char			*name;
	ipadm_status_t		status;
	int			err = 0;
	uint32_t		flags = IPADM_OPT_ACTIVE;

	/* retrieve the address information */
	for (nvp = nvlist_next_nvpair(nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {
		name = nvpair_name(nvp);
		if (strcmp(name, IPADM_NVP_IPV4ADDR) == 0 ||
		    strcmp(name, IPADM_NVP_IPV6ADDR) == 0) {
			err = nvpair_value_nvlist(nvp, &nvaddr);
		} else if (strcmp(name, IPADM_NVP_AOBJNAME) == 0) {
			err = nvpair_value_string(nvp, &aobjname);
		} else if (strcmp(name, IPADM_NVP_PREFIXLEN) == 0) {
			err = nvpair_value_string(nvp, &prefixlenstr);
		} else if (strcmp(name, "up") == 0) {
			err = nvpair_value_string(nvp, &upstr);
		}
		if (err != 0)
			return (ipadm_errno2status(err));
	}
	for (nvp = nvlist_next_nvpair(nvaddr, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvaddr, nvp)) {
		name = nvpair_name(nvp);
		if (strcmp(name, IPADM_NVP_IPADDRHNAME) == 0)
			err = nvpair_value_string(nvp, &sname);
		else if (strcmp(name, IPADM_NVP_IPDADDRHNAME) == 0)
			err = nvpair_value_string(nvp, &dname);
		if (err != 0)
			return (ipadm_errno2status(err));
	}

	if (strcmp(upstr, "yes") == 0)
		flags |= IPADM_OPT_UP;

	/* build the address object from the above information */
	i_ipadm_init_addr(&ipaddr, ifname, aobjname, IPADM_ADDR_STATIC);
	if (prefixlenstr != NULL && atoi(prefixlenstr) > 0) {
		if (asprintf(&cidraddr, "%s/%s", sname, prefixlenstr) == -1)
			return (IPADM_NO_MEMORY);
		status = ipadm_set_addr(&ipaddr, cidraddr, af);
		free(cidraddr);
	} else {
		status = ipadm_set_addr(&ipaddr, sname, af);
	}
	if (status != IPADM_SUCCESS)
		return (status);

	if (dname != NULL) {
		status = ipadm_set_dst_addr(&ipaddr, dname, af);
		if (status != IPADM_SUCCESS)
			return (status);
	}
	return (i_ipadm_create_addr(iph, &ipaddr, flags));
}

/*
 * Creates a dhcp address on the interface `ifname' based on the
 * IPADM_ADDR_DHCP address object parameters from the nvlist `nvl'.
 */
ipadm_status_t
i_ipadm_enable_dhcp(ipadm_handle_t iph, const char *ifname, nvlist_t *nvl)
{
	int32_t			wait;
	boolean_t		primary;
	nvlist_t		*nvdhcp;
	nvpair_t		*nvp;
	char			*name;
	struct ipadm_addrobj_s	ipaddr;
	char			*aobjname;
	char			*hostname = NULL;
	char			*ewait;
	int			err = 0;

	/* Extract the dhcp parameters */
	for (nvp = nvlist_next_nvpair(nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {
		name = nvpair_name(nvp);
		if (strcmp(name, IPADM_NVP_DHCP) == 0)
			err = nvpair_value_nvlist(nvp, &nvdhcp);
		else if (strcmp(name, IPADM_NVP_AOBJNAME) == 0)
			err = nvpair_value_string(nvp, &aobjname);
		else if (strcmp(name, IPADM_NVP_REQHOST) == 0)
			err = nvpair_value_string(nvp, &hostname);
		if (err != 0)
			return (ipadm_errno2status(err));
	}
	for (nvp = nvlist_next_nvpair(nvdhcp, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvdhcp, nvp)) {
		name = nvpair_name(nvp);
		if (strcmp(name, IPADM_NVP_WAIT) == 0)
			err = nvpair_value_int32(nvp, &wait);
		else if (strcmp(name, IPADM_NVP_PRIMARY) == 0)
			err = nvpair_value_boolean_value(nvp, &primary);
		if (err != 0)
			return (ipadm_errno2status(err));
	}

	/* Build the address object */
	i_ipadm_init_addr(&ipaddr, ifname, aobjname, IPADM_ADDR_DHCP);
	ipaddr.ipadm_primary = primary;
	if (hostname != NULL)
		(void) strlcpy(ipaddr.ipadm_reqhost, hostname,
		    sizeof (ipaddr.ipadm_reqhost));
	/*
	 * When enabling the interface as part of the
	 * svc:/network/physical service being enabled, allow wait
	 * to be overridden by the environmental variable.
	 */
	if ((ewait = getenv("IPADM_NET_PHYSICAL_WAIT")) != NULL) {
		if ((wait = atoi(ewait)) < 0)
			wait = 0;
	}
	ipaddr.ipadm_wait = wait;
	ipaddr.ipadm_af = AF_INET;
	return (i_ipadm_create_dhcp(iph, &ipaddr, IPADM_OPT_ACTIVE));
}

/*
 * Parses an address object of type IPADM_ADDR_IPV6_ADDRCONF from input
 * nvlist `nvl' and store it in `ipaddr'.
 */
static ipadm_status_t
i_ipadm_nvl2addrconf_addrobj(nvlist_t *nvl, ipadm_addrobj_t ipaddr)
{
	nvlist_t	*nvaddr;
	nvpair_t	*nvp;
	char		*name, *aobjname;
	char		*stateful = NULL;
	char		*stateless = NULL;
	uint_t		na, nd;
	uint8_t		*addr6 = NULL;
	uint8_t		*daddr6 = NULL;
	uint32_t	intfidlen = 0;
	int		err = 0;

	for (nvp = nvlist_next_nvpair(nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {
		name = nvpair_name(nvp);
		if (strcmp(name, IPADM_NVP_INTFID) == 0)
			err = nvpair_value_nvlist(nvp, &nvaddr);
		else if (strcmp(name, IPADM_NVP_DINTFID) == 0)
			err = nvpair_value_uint8_array(nvp, &daddr6, &nd);
		else if (strcmp(name, IPADM_NVP_AOBJNAME) == 0)
			err = nvpair_value_string(nvp, &aobjname);
		if (err != 0)
			return (ipadm_errno2status(err));
	}
	for (nvp = nvlist_next_nvpair(nvaddr, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvaddr, nvp)) {
		name = nvpair_name(nvp);
		if (strcmp(name, IPADM_NVP_IPNUMADDR) == 0)
			err = nvpair_value_uint8_array(nvp, &addr6, &na);
		else if (strcmp(name, IPADM_NVP_PREFIXLEN) == 0)
			err = nvpair_value_uint32(nvp, &intfidlen);
		else if (strcmp(name, IPADM_NVP_STATELESS) == 0)
			err = nvpair_value_string(nvp, &stateless);
		else if (strcmp(name, IPADM_NVP_STATEFUL) == 0)
			err = nvpair_value_string(nvp, &stateful);
		if (err != 0)
			return (ipadm_errno2status(err));
	}
	if (intfidlen > 0) {
		ipaddr->ipadm_intfidlen = intfidlen;
		bcopy(addr6, &ipaddr->ipadm_intfid.sin6_addr.s6_addr, na);
	}
	if (daddr6 != NULL) {
		ipaddr->ipadm_dintfid.sin6_family = AF_INET6;
		bcopy(daddr6, &ipaddr->ipadm_dintfid.sin6_addr.s6_addr, nd);
	}
	(void) strlcpy(ipaddr->ipadm_aobjname, aobjname,
	    sizeof (ipaddr->ipadm_aobjname));
	ipaddr->ipadm_stateless = (strcmp(stateless, "yes") == 0);
	ipaddr->ipadm_stateful = (strcmp(stateful, "yes") == 0);
	return (IPADM_SUCCESS);
}

/*
 * Creates auto-configured addresses on the interface `ifname' based on
 * the IPADM_ADDR_IPV6_ADDRCONF address object parameters from the nvlist `nvl'.
 */
ipadm_status_t
i_ipadm_enable_addrconf(ipadm_handle_t iph, const char *ifname, nvlist_t *nvl)
{
	struct ipadm_addrobj_s	ipaddr;
	ipadm_status_t	status;

	i_ipadm_init_addr(&ipaddr, ifname, "", IPADM_ADDR_IPV6_ADDRCONF);
	status = i_ipadm_nvl2addrconf_addrobj(nvl, &ipaddr);
	if (status != IPADM_SUCCESS)
		return (status);
	return (i_ipadm_create_ipv6addrs(iph, &ipaddr, IPADM_OPT_ACTIVE));
}

/*
 * Allocates `ipadm_addrobj_t' and populates the relevant member fields based on
 * the provided `type'. `aobjname' represents the address object name, which
 * is of the form `<ifname>/<addressname>'.
 *
 * The caller has to minimally provide <ifname>. If <addressname> is not
 * provided, then a default one will be generated by the API.
 */
ipadm_status_t
ipadm_create_addrobj(ipadm_addr_type_t type, const char *aobjname,
    ipadm_addrobj_t *ipaddr)
{
	ipadm_addrobj_t	newaddr;
	ipadm_status_t	status;
	char		*aname, *cp;
	char		ifname[IPADM_AOBJSIZ];
	ifspec_t 	ifsp;

	if (ipaddr == NULL)
		return (IPADM_INVALID_ARG);
	*ipaddr = NULL;

	if (aobjname == NULL || aobjname[0] == '\0')
		return (IPADM_INVALID_ARG);

	if (strlcpy(ifname, aobjname, IPADM_AOBJSIZ) >= IPADM_AOBJSIZ)
		return (IPADM_INVALID_ARG);

	if ((aname = strchr(ifname, '/')) != NULL)
		*aname++ = '\0';

	/* Check if the interface name is valid. */
	if (!ifparse_ifspec(ifname, &ifsp))
		return (IPADM_INVALID_ARG);

	/* Check if the given addrobj name is valid. */
	if (aname != NULL && !i_ipadm_is_user_aobjname_valid(aname))
		return (IPADM_INVALID_ARG);

	if ((newaddr = calloc(1, sizeof (struct ipadm_addrobj_s))) == NULL)
		return (IPADM_NO_MEMORY);

	/*
	 * If the ifname has logical interface number, extract it and assign
	 * it to `ipadm_lifnum'. Only applications with IH_LEGACY set will do
	 * this today. We will check for the validity later in
	 * i_ipadm_validate_create_addr().
	 */
	if (ifsp.ifsp_lunvalid) {
		newaddr->ipadm_lifnum = ifsp.ifsp_lun;
		cp = strchr(ifname, IPADM_LOGICAL_SEP);
		*cp = '\0';
	}
	(void) strlcpy(newaddr->ipadm_ifname, ifname,
	    sizeof (newaddr->ipadm_ifname));

	if (aname != NULL) {
		(void) snprintf(newaddr->ipadm_aobjname,
		    sizeof (newaddr->ipadm_aobjname), "%s/%s", ifname, aname);
	}

	switch (type) {
	case IPADM_ADDR_IPV6_ADDRCONF:
		newaddr->ipadm_intfidlen = 0;
		newaddr->ipadm_stateful = B_TRUE;
		newaddr->ipadm_stateless = B_TRUE;
		newaddr->ipadm_af = AF_INET6;
		break;

	case IPADM_ADDR_DHCP:
		newaddr->ipadm_primary = B_FALSE;
		newaddr->ipadm_wait = IPADM_DHCP_WAIT_DEFAULT;
		newaddr->ipadm_af = AF_INET;
		break;

	case IPADM_ADDR_STATIC:
		newaddr->ipadm_af = AF_UNSPEC;
		newaddr->ipadm_static_prefixlen = 0;
		break;

	default:
		status = IPADM_INVALID_ARG;
		goto fail;
	}
	newaddr->ipadm_atype = type;
	*ipaddr = newaddr;
	return (IPADM_SUCCESS);
fail:
	free(newaddr);
	return (status);
}

/*
 * Returns `aobjname' from the address object in `ipaddr'.
 */
ipadm_status_t
ipadm_get_aobjname(ipadm_addrobj_t ipaddr, char *aobjname, size_t len)
{
	if (ipaddr == NULL || aobjname == NULL)
		return (IPADM_INVALID_ARG);
	if (strlcpy(aobjname, ipaddr->ipadm_aobjname, len) >= len)
		return (IPADM_INVALID_ARG);

	return (IPADM_SUCCESS);
}

/*
 * Frees the address object in `ipaddr'.
 */
void
ipadm_destroy_addrobj(ipadm_addrobj_t ipaddr)
{
	free(ipaddr);
}

/*
 * Retrieves the logical interface name from `ipaddr' and stores the
 * string in `lifname'.
 */
void
i_ipadm_addrobj2lifname(ipadm_addrobj_t ipaddr, char *lifname, int lifnamesize)
{
	if (ipaddr->ipadm_lifnum != 0) {
		(void) snprintf(lifname, lifnamesize, "%s:%d",
		    ipaddr->ipadm_ifname, ipaddr->ipadm_lifnum);
	} else {
		(void) snprintf(lifname, lifnamesize, "%s",
		    ipaddr->ipadm_ifname);
	}
}

/*
 * Checks if a non-zero static address is present on the 0th logical interface
 * of the given IPv4 or IPv6 physical interface. For an IPv4 interface, it
 * also checks if the interface is under DHCP control. If the condition is true,
 * the output argument `exists' will be set to B_TRUE. Otherwise, `exists'
 * is set to B_FALSE.
 *
 * Note that *exists will not be initialized if an error is encountered.
 */
static ipadm_status_t
i_ipadm_addr_exists_on_if(ipadm_handle_t iph, const char *ifname,
    sa_family_t af, boolean_t *exists)
{
	struct lifreq	lifr;
	int		sock;

	/* For IH_LEGACY, a new logical interface will never be added. */
	if (i_ipadm_is_legacy(iph)) {
		*exists = B_FALSE;
		return (IPADM_SUCCESS);
	}
	bzero(&lifr, sizeof (lifr));
	(void) strlcpy(lifr.lifr_name, ifname, sizeof (lifr.lifr_name));
	sock = IPADM_SOCK(iph, af);
	if (af == AF_INET) {
		if (ioctl(sock, SIOCGLIFFLAGS, (caddr_t)&lifr) < 0)
			return (ipadm_errno2status(errno));
		if (lifr.lifr_flags & IFF_DHCPRUNNING) {
			*exists = B_TRUE;
			return (IPADM_SUCCESS);
		}
	}
	if (ioctl(sock, SIOCGLIFADDR, (caddr_t)&lifr) < 0)
		return (ipadm_errno2status(errno));
	*exists = !sockaddrunspec((struct sockaddr *)&lifr.lifr_addr);

	return (IPADM_SUCCESS);
}

/*
 * Adds a new logical interface in the kernel for interface
 * `addr->ipadm_ifname', if there is a non-zero address on the 0th
 * logical interface or if the 0th logical interface is under DHCP
 * control. On success, it sets the lifnum in the address object `addr'.
 */
ipadm_status_t
i_ipadm_do_addif(ipadm_handle_t iph, ipadm_addrobj_t addr)
{
	ipadm_status_t	status;
	boolean_t	addif;
	struct lifreq	lifr;
	int		sock;

	addr->ipadm_lifnum = 0;
	status = i_ipadm_addr_exists_on_if(iph, addr->ipadm_ifname,
	    addr->ipadm_af, &addif);
	if (status != IPADM_SUCCESS) {
		addr->ipadm_lifnum = -1;
		return (status);
	}
	if (addif) {
		/*
		 * If there is an address on 0th logical interface,
		 * add a new logical interface.
		 */
		bzero(&lifr, sizeof (lifr));
		(void) strlcpy(lifr.lifr_name, addr->ipadm_ifname,
		    sizeof (lifr.lifr_name));
		sock = IPADM_SOCK(iph, addr->ipadm_af);
		if (ioctl(sock, SIOCLIFADDIF, (caddr_t)&lifr) < 0)
			return (ipadm_errno2status(errno));
		addr->ipadm_lifnum = i_ipadm_get_lnum(lifr.lifr_name);
	}
	return (IPADM_SUCCESS);
}

/*
 * Reads all the address lines from the persistent DB into the nvlist `onvl',
 * when both `ifname' and `aobjname' are NULL. If an `ifname' is provided,
 * it returns all the addresses for the given interface `ifname'.
 * If an `aobjname' is specified, then the address line corresponding to
 * that name will be returned.
 */
static ipadm_status_t
i_ipadm_get_db_addr(ipadm_handle_t iph, const char *ifname,
    const char *aobjname, nvlist_t **onvl)
{
	ipmgmt_getaddr_arg_t	garg;
	ipmgmt_get_rval_t	*rvalp;
	int			err;
	size_t			nvlsize;
	char			*nvlbuf;

	/* Populate the door_call argument structure */
	bzero(&garg, sizeof (garg));
	garg.ia_cmd = IPMGMT_CMD_GETADDR;
	if (aobjname != NULL)
		(void) strlcpy(garg.ia_aobjname, aobjname,
		    sizeof (garg.ia_aobjname));
	if (ifname != NULL)
		(void) strlcpy(garg.ia_ifname, ifname, sizeof (garg.ia_ifname));

	rvalp = malloc(sizeof (ipmgmt_get_rval_t));
	err = ipadm_door_dyncall(iph, &garg, sizeof (garg), (void **)&rvalp,
	    sizeof (*rvalp));
	if (err == 0) {
		nvlsize = rvalp->ir_nvlsize;
		nvlbuf = (char *)rvalp + sizeof (ipmgmt_get_rval_t);
		err = nvlist_unpack(nvlbuf, nvlsize, onvl, NV_ENCODE_NATIVE);
	}
	free(rvalp);
	return (ipadm_errno2status(err));
}

/*
 * Adds the IP address contained in the 'ipaddr' argument to the physical
 * interface represented by 'ifname' after doing the required validation.
 * If the interface does not exist, IPADM_ENXIO is returned, unless it
 * is the loopback interface.
 *
 * If IH_LEGACY is set in ih_flags, flags has to be IPADM_OPT_ACTIVE
 * and a default addrobj name will be generated. Input `addr->ipadm_aobjname',
 * if provided, will be ignored and replaced with the newly generated name.
 * The interface name provided has to be a logical interface name that
 * already exists. No new logical interface will be added in this function.
 */
ipadm_status_t
ipadm_create_addr(ipadm_handle_t iph, ipadm_addrobj_t addr, uint32_t flags)
{
	ipadm_status_t		status;
	sa_family_t		af;
	sa_family_t		daf;
	boolean_t		created_af = B_FALSE;
	ipadm_addr_type_t	type;
	char			*ifname = addr->ipadm_ifname;
	boolean_t		legacy = i_ipadm_is_legacy(iph);
	boolean_t		create_aobj = B_TRUE;
	boolean_t		is_6to4;
	uint64_t		ifflags;
	boolean_t		is_boot = (iph->ih_flags & IH_IPMGMTD);
	ipadm_if_class_t	class;

	/* check for solaris.network.interface.config authorization */
	if (!ipadm_check_auth())
		return (IPADM_INSUFF_AUTH);

	/* Validate the addrobj. This also fills in addr->ipadm_ifname. */
	status = i_ipadm_validate_create_addr(iph, addr, flags);
	if (status != IPADM_SUCCESS)
		return (status);

	/*
	 * For Legacy case, check if an addrobj already exists for the
	 * given logical interface name. If one does not exist,
	 * a default name will be generated and added to the daemon's
	 * aobjmap.
	 */
	if (legacy) {
		struct ipadm_addrobj_s	ipaddr;
		char			lifname[LIFNAMSIZ];

		if (addr->ipadm_lifnum > 0) {
			(void) snprintf(lifname, LIFNAMSIZ, "%s:%d",
			    addr->ipadm_ifname, addr->ipadm_lifnum);
		} else {
			(void) strlcpy(lifname, addr->ipadm_ifname, LIFNAMSIZ);
		}
		status = i_ipadm_get_lif2addrobj(iph, lifname, addr->ipadm_af,
		    &ipaddr);
		if (status == IPADM_SUCCESS) {
			/*
			 * With IH_LEGACY, modifying an address that is not
			 * a static address will return with an error.
			 */
			if (ipaddr.ipadm_atype != IPADM_ADDR_STATIC)
				return (IPADM_OP_NOTSUP);
			/*
			 * we found the addrobj in daemon, copy over the
			 * aobjname to `addr'.
			 */
			(void) strlcpy(addr->ipadm_aobjname,
			    ipaddr.ipadm_aobjname, IPADM_AOBJSIZ);
			create_aobj = B_FALSE;
		} else if (status != IPADM_OBJ_NOTFOUND) {
			return (status);
		}
		status = IPADM_SUCCESS;
	}

	af = addr->ipadm_af;
	/*
	 * Create a placeholder for this address object in the daemon.
	 * Skip this step if we are booting a zone (and therefore being called
	 * from ipmgmtd itself), and, for IH_LEGACY case if the
	 * addrobj already exists.
	 *
	 * Note that the placeholder is not needed in the NGZ boot case,
	 * when zoneadmd has itself applied the "allowed-ips" property to clamp
	 * down any interface configuration, so the namespace for the interface
	 * is fully controlled by the GZ.
	 */
	if (!is_boot && create_aobj) {
		status = i_ipadm_lookupadd_addrobj(iph, addr);
		if (status != IPADM_SUCCESS)
			return (status);
	}

	if (!ipadm_if_enabled(iph, ifname, af)) {
		uint32_t	iflags = IPADM_OPT_ACTIVE;

		if (ipadm_is_vni(ifname))
			class = IPADMIF_CLASS_VNI;
		else if (ipadm_is_loopback(ifname))
			class = IPADMIF_CLASS_LOOPBACK;
		else if (ipadm_is_ipmp(iph, ifname))
			class = IPADMIF_CLASS_IPMP;
		else
			class = IPADMIF_CLASS_IP;
		/* Plumb the interface if necessary */
		if (i_ipadm_if_pexists(iph, ifname, AF_UNSPEC)) {
			iflags |= IPADM_OPT_PERSIST;
		} else {
			if (class == IPADMIF_CLASS_LOOPBACK) {
				iflags = (flags &
				    (IPADM_OPT_ACTIVE|IPADM_OPT_PERSIST));
			}
		}
		status = i_ipadm_plumb_if(iph, ifname, af, class, iflags);
		if (status != IPADM_SUCCESS && status != IPADM_IF_EXISTS)
			goto fail;
		if (status == IPADM_SUCCESS)
			created_af = B_TRUE;
	}

	is_6to4 = i_ipadm_is_6to4(iph, ifname);
	/*
	 * Some input validation based on the interface flags:
	 * 1. in non-global zones, make sure that we are not persistently
	 *    creating addresses on interfaces that are acquiring
	 *    address from the global zone.
	 * 2. Validate static addresses for IFF_POINTOPOINT interfaces.
	 */
	if (addr->ipadm_atype == IPADM_ADDR_STATIC ||
	    addr->ipadm_atype == IPADM_ADDR_IPV6_ADDRCONF) {
		status = i_ipadm_get_flags(iph, ifname, af, &ifflags);
		if (status != IPADM_SUCCESS)
			goto fail;
	}

	if (addr->ipadm_atype == IPADM_ADDR_STATIC) {
		daf = addr->ipadm_static_dst_addr.ss_family;
		if (ifflags & IFF_POINTOPOINT) {
			if (is_6to4) {
				if (af != AF_INET6 || daf != AF_UNSPEC) {
					status = IPADM_INVALID_ARG;
					goto fail;
				}
			} else {
				if (daf != af) {
					status = IPADM_INVALID_ARG;
					goto fail;
				}
				/* Check for a valid dst address. */
				if (!legacy && sockaddrunspec(
				    (struct sockaddr *)
				    &addr->ipadm_static_dst_addr)) {
					status = IPADM_BAD_ADDR;
					goto fail;
				}
			}
		} else {
			/*
			 * Disallow setting of dstaddr when the link is not
			 * a point-to-point link.
			 */
			if (daf != AF_UNSPEC) {
				status = IPADM_INVALID_ARG;
				goto fail;
			}
		}
	} else if (addr->ipadm_atype == IPADM_ADDR_IPV6_ADDRCONF) {
		/*
		 * Disallow setting of dstaddr when the link is not
		 * a point-to-point link.
		 */
		daf = addr->ipadm_dintfid.sin6_family;
		if (!(ifflags & IFF_POINTOPOINT) && (daf != AF_UNSPEC))
			return (IPADM_INVALID_ARG);
	}

	/* Create the address. */
	type = addr->ipadm_atype;
	switch (type) {
	case IPADM_ADDR_STATIC:
		status = i_ipadm_create_addr(iph, addr, flags);
		break;
	case IPADM_ADDR_DHCP:
		status = i_ipadm_create_dhcp(iph, addr, flags);
		break;
	case IPADM_ADDR_IPV6_ADDRCONF:
		status = i_ipadm_create_ipv6addrs(iph, addr, flags);
		break;
	default:
		status = IPADM_INVALID_ARG;
		break;
	}

	/*
	 * If address was not created successfully, remove the
	 * addrobj created by the ipmgmtd daemon as a placeholder.
	 * If IH_LEGACY is set, then remove the addrobj only if it was
	 * created in this function.
	 */
fail:
	if (status != IPADM_DHCP_IPC_TIMEOUT &&
	    status != IPADM_SUCCESS) {
		if (created_af)
			(void) i_ipadm_delete_if(iph, ifname, af,
			    IPADMIF_CLASS_LOOPBACK, flags);
		else if (create_aobj)
			(void) i_ipadm_delete_addrobj(iph, addr, flags);
	}

	return (status);
}

/*
 * Creates the static address in `ipaddr' in kernel. After successfully
 * creating it, it updates the ipmgmtd daemon's aobjmap with the logical
 * interface information.
 */
static ipadm_status_t
i_ipadm_create_addr(ipadm_handle_t iph, ipadm_addrobj_t ipaddr, uint32_t flags)
{
	struct lifreq			lifr;
	ipadm_status_t			status = IPADM_SUCCESS;
	int				sock;
	struct sockaddr_storage		m, *mask = &m;
	const struct sockaddr_storage	*addr = &ipaddr->ipadm_static_addr;
	const struct sockaddr_storage	*daddr = &ipaddr->ipadm_static_dst_addr;
	sa_family_t			af;
	boolean_t			legacy;
	boolean_t			default_prefixlen = B_FALSE;
	boolean_t			bringup = B_FALSE;
	boolean_t			is_boot;
	boolean_t			is_under;

	is_under = i_ipadm_is_under_ipmp(iph, ipaddr->ipadm_ifname);
	is_boot = ((iph->ih_flags & IH_IPMGMTD) != 0);
	legacy = i_ipadm_is_legacy(iph);
	af = ipaddr->ipadm_af;
	sock = IPADM_SOCK(iph, af);

	/* If prefixlen was not provided, get default prefixlen */
	if (ipaddr->ipadm_static_prefixlen == 0) {
		/* prefixlen was not provided, get default prefixlen */
		status = i_ipadm_get_default_prefixlen(
		    &ipaddr->ipadm_static_addr,
		    &ipaddr->ipadm_static_prefixlen);
		if (status != IPADM_SUCCESS)
			return (status);
		default_prefixlen = B_TRUE;
	}
	(void) plen2mask(ipaddr->ipadm_static_prefixlen, af,
	    (struct sockaddr *)mask);

	/*
	 * Create a new logical interface if needed; otherwise, just
	 * use the 0th logical interface.
	 */
retry:
	if (!i_ipadm_is_legacy(iph)) {
		status = i_ipadm_do_addif(iph, ipaddr);
		if (status != IPADM_SUCCESS)
			return (status);
		/*
		 * If the address is to be created on an underlying
		 * interface and it happens to be on the 0th logical
		 * interface, make sure we bring the address down first
		 * to avoid the address migration onto the IPMP interface.
		 */
		if (ipaddr->ipadm_lifnum == 0 && is_under) {
			status = i_ipadm_set_flags(iph, ipaddr->ipadm_ifname,
			    af, 0, IFF_UP);
			if (status != IPADM_SUCCESS)
				return (status);
			bringup = B_TRUE;
		}
		/*
		 * We don't have to set the lifnum for IH_INIT case, because
		 * there is no placeholder created for the address object in
		 * this case. For IH_LEGACY, we don't do this because the
		 * lifnum is given by the caller and it will be set in the
		 * end while we call the i_ipadm_addr_persist().
		 */
		if (!(iph->ih_flags & IH_INIT)) {
			status = i_ipadm_setlifnum_addrobj(iph, ipaddr);
			if (status == IPADM_ADDROBJ_EXISTS)
				goto retry;
			if (status != IPADM_SUCCESS)
				return (status);
		}
	}
	i_ipadm_addrobj2lifname(ipaddr, lifr.lifr_name,
	    sizeof (lifr.lifr_name));
	lifr.lifr_addr = *mask;
	if (ioctl(sock, SIOCSLIFNETMASK, (caddr_t)&lifr) < 0) {
		status = ipadm_errno2status(errno);
		goto ret;
	}
	lifr.lifr_addr = *addr;
	if (ioctl(sock, SIOCSLIFADDR, (caddr_t)&lifr) < 0) {
		status = ipadm_errno2status(errno);
		goto ret;
	}
	/* Set the destination address, if one is given. */
	if (daddr->ss_family != AF_UNSPEC) {
		lifr.lifr_addr = *daddr;
		if (ioctl(sock, SIOCSLIFDSTADDR, (caddr_t)&lifr) < 0) {
			status = ipadm_errno2status(errno);
			goto ret;
		}
	}

	/*
	 * If this is an underlying interface in an IPMP group,
	 * make this a test address.
	 */
	if (!legacy && is_under && (af != AF_INET6 || !IN6_IS_ADDR_LINKLOCAL(
	    &SIN6(&ipaddr->ipadm_static_addr)->sin6_addr))) {
		status = i_ipadm_set_flags(iph, lifr.lifr_name, af,
		    IFF_NOFAILOVER|IFF_DEPRECATED, 0);
		if (status != IPADM_SUCCESS)
			goto ret;
	}
	if (flags & IPADM_OPT_UP) {
		status = i_ipadm_set_flags(iph, lifr.lifr_name, af, IFF_UP, 0);

		/*
		 * IPADM_DAD_FOUND is a soft-error for create-addr.
		 * No need to tear down the address.
		 */
		if (status == IPADM_DAD_FOUND)
			status = IPADM_SUCCESS;
	}

	if (status == IPADM_SUCCESS && !is_boot) {
		/*
		 * For IH_LEGACY, we might be modifying the address on
		 * an address object that already exists e.g. by doing
		 * "ifconfig bge0:1 <addr>; ifconfig bge0:1 <newaddr>"
		 * So, we need to store the object only if it does not
		 * already exist in ipmgmtd.
		 */
		if (legacy) {
			struct ipadm_addrobj_s		legacy_addr;

			bzero(&legacy_addr, sizeof (legacy_addr));
			(void) strlcpy(legacy_addr.ipadm_aobjname,
			    ipaddr->ipadm_aobjname,
			    sizeof (legacy_addr.ipadm_aobjname));
			status = i_ipadm_get_addrobj(iph, &legacy_addr);
			if (status == IPADM_SUCCESS &&
			    legacy_addr.ipadm_lifnum >= 0) {
				return (status);
			}
		}
		status = i_ipadm_addr_persist(iph, ipaddr, default_prefixlen,
		    flags);
	}
ret:
	if (status != IPADM_SUCCESS) {
		if (!legacy)
			(void) i_ipadm_delete_addr(iph, ipaddr);
		if (bringup) {
			(void) i_ipadm_set_flags(iph, ipaddr->ipadm_ifname, af,
			    IFF_UP, 0);
		}
	}
	return (status);
}

/*
 * Removes the address object identified by `aobjname' from both active and
 * persistent configuration. The address object will be removed from only
 * active configuration if IH_LEGACY is set in `iph->ih_flags'.
 *
 * If the address type is IPADM_ADDR_STATIC or IPADM_ADDR_DHCP, the address
 * in the address object will be removed from the physical interface.
 * If the address type is IPADM_ADDR_DHCP, the flag IPADM_OPT_RELEASE specifies
 * whether the lease should be released. If IPADM_OPT_RELEASE is not
 * specified, the lease will be dropped. This option is not supported
 * for other address types.
 *
 * If the address type is IPADM_ADDR_IPV6_ADDRCONF, the link-local address and
 * all the autoconfigured addresses will be removed.
 * Finally, the address object is also removed from ipmgmtd's aobjmap and from
 * the persistent DB.
 */
ipadm_status_t
ipadm_delete_addr(ipadm_handle_t iph, const char *aobjname, uint32_t flags)
{
	ipadm_status_t		status;
	struct ipadm_addrobj_s	ipaddr;
	boolean_t		release = ((flags & IPADM_OPT_RELEASE) != 0);

	/* check for solaris.network.interface.config authorization */
	if (!ipadm_check_auth())
		return (IPADM_INSUFF_AUTH);

	/* validate input */
	if (flags == 0 || ((flags & IPADM_OPT_PERSIST) &&
	    !(flags & IPADM_OPT_ACTIVE)) ||
	    (flags & ~(IPADM_COMMON_OPT_MASK|IPADM_OPT_RELEASE))) {
		return (IPADM_INVALID_ARG);
	}
	bzero(&ipaddr, sizeof (ipaddr));
	if (aobjname == NULL || strlcpy(ipaddr.ipadm_aobjname, aobjname,
	    IPADM_AOBJSIZ) >= IPADM_AOBJSIZ) {
		return (IPADM_INVALID_ARG);
	}

	/* Retrieve the address object information from ipmgmtd. */
	status = i_ipadm_get_addrobj(iph, &ipaddr);
	if (status != IPADM_SUCCESS)
		return (status);

	if (release && ipaddr.ipadm_atype != IPADM_ADDR_DHCP)
		return (IPADM_OP_NOTSUP);
	/*
	 * If requested to delete just from active config but the address
	 * is not in active config, return error.
	 */
	if (!(ipaddr.ipadm_flags & IPMGMT_ACTIVE) &&
	    (flags & IPADM_OPT_ACTIVE) && !(flags & IPADM_OPT_PERSIST)) {
		return (IPADM_OBJ_NOTFOUND);
	}

	/*
	 * If address is present in active config, remove it from
	 * kernel.
	 */
	if (ipaddr.ipadm_flags & IPMGMT_ACTIVE) {
		switch (ipaddr.ipadm_atype) {
		case IPADM_ADDR_STATIC:
			status = i_ipadm_delete_addr(iph, &ipaddr);
			break;
		case IPADM_ADDR_DHCP:
			status = i_ipadm_delete_dhcp(iph, &ipaddr, release);
			break;
		case IPADM_ADDR_IPV6_ADDRCONF:
			status = i_ipadm_delete_ipv6addrs(iph, &ipaddr);
			break;
		default:
			/*
			 * This is the case of address object name residing in
			 * daemon's aobjmap (added by ADDROBJ_LOOKUPADD). Fall
			 * through and delete that address object.
			 */
			break;
		}

		/*
		 * If the address was previously deleted from the active
		 * config, we will get an ENXIO from kernel.
		 * We will still proceed and purge the address information
		 * in the DB.
		 */
		if (status == IPADM_NOSUCH_IF)
			status = IPADM_SUCCESS;
		else if (status != IPADM_SUCCESS)
			return (status);
	}

	if (!(ipaddr.ipadm_flags & IPMGMT_PERSIST) &&
	    (flags & IPADM_OPT_PERSIST)) {
		flags &= ~IPADM_OPT_PERSIST;
	}
	status = i_ipadm_delete_addrobj(iph, &ipaddr, flags);
	if (status == IPADM_OBJ_NOTFOUND)
		return (status);
	return (IPADM_SUCCESS);
}

/*
 * Starts the dhcpagent and sends it the message DHCP_START to start
 * configuring a dhcp address on the given interface in `addr'.
 * After making the dhcpagent request, it also updates the
 * address object information in ipmgmtd's aobjmap and creates an
 * entry in persistent DB if IPADM_OPT_PERSIST is set in `flags'.
 */
static ipadm_status_t
i_ipadm_create_dhcp(ipadm_handle_t iph, ipadm_addrobj_t addr, uint32_t flags)
{
	ipadm_status_t	status;
	ipadm_status_t	dh_status;
	boolean_t	legacy = i_ipadm_is_legacy(iph);

	if (dhcp_start_agent(DHCP_IPC_MAX_WAIT) == -1)
		return (IPADM_CANNOT_START_DHCP);
	/*
	 * Create a new logical interface if needed; otherwise, just
	 * use the 0th logical interface.
	 */
retry:
	if (!legacy) {
		status = i_ipadm_do_addif(iph, addr);
		if (status != IPADM_SUCCESS)
			return (status);
		/*
		 * We don't have to set the lifnum for IH_INIT case, because
		 * there is no placeholder created for the address object in
		 * this case.
		 */
		if (!(iph->ih_flags & IH_INIT)) {
			status = i_ipadm_setlifnum_addrobj(iph, addr);
			if (status == IPADM_ADDROBJ_EXISTS)
				goto retry;
			if (status != IPADM_SUCCESS)
				return (status);
		}
	}
	/* Send DHCP_START to the dhcpagent. */
	status = i_ipadm_op_dhcp(addr, DHCP_START, NULL, NULL);
	/*
	 * We do not undo the create-addr operation for IPADM_DHCP_IPC_TIMEOUT
	 * since it is only a soft error to indicate the caller that the lease
	 * might be required after the function returns.
	 */
	if (status != IPADM_SUCCESS && status != IPADM_DHCP_IPC_TIMEOUT)
		goto fail;
	dh_status = status;

	/* Persist the address object information in ipmgmtd. */
	status = i_ipadm_addr_persist(iph, addr, B_FALSE, flags);
	if (status != IPADM_SUCCESS)
		goto fail;

	return (dh_status);
fail:
	/* In case of error, delete the dhcp address */
	(void) i_ipadm_delete_dhcp(iph, addr, B_TRUE);
	return (status);
}

/*
 * Releases/drops the dhcp lease on the logical interface in the address
 * object `addr'. If `release' is set to B_FALSE, the lease will be dropped.
 */
static ipadm_status_t
i_ipadm_delete_dhcp(ipadm_handle_t iph, ipadm_addrobj_t addr, boolean_t release)
{
	ipadm_status_t	status;
	int		dherr;

	/* Send DHCP_RELEASE or DHCP_DROP to the dhcpagent */
	if (release) {
		status = i_ipadm_op_dhcp(addr, DHCP_RELEASE, &dherr, NULL);
		/*
		 * If no lease was obtained on the object, we should
		 * drop the dhcp control on the interface.
		 */
		if (status != IPADM_SUCCESS && dherr == DHCP_IPC_E_OUTSTATE)
			status = i_ipadm_op_dhcp(addr, DHCP_DROP, NULL, NULL);
	} else {
		status = i_ipadm_op_dhcp(addr, DHCP_DROP, NULL, NULL);
	}
	if (status != IPADM_SUCCESS)
		return (status);

	if (!i_ipadm_is_legacy(iph)) {
		/*
		 * Delete the logical interface. Bring it down, if it is the 0th
		 * logical interface.
		 */
		if (addr->ipadm_lifnum != 0) {
			struct lifreq	lifr;

			bzero(&lifr, sizeof (lifr));
			i_ipadm_addrobj2lifname(addr, lifr.lifr_name,
			    sizeof (lifr.lifr_name));
			if (ioctl(iph->ih_sock, SIOCLIFREMOVEIF,
			    (caddr_t)&lifr) < 0)
				return (ipadm_errno2status(errno));
		} else {
			status = i_ipadm_set_flags(iph, addr->ipadm_ifname,
			    AF_INET, 0, IFF_UP);
			if (status != IPADM_SUCCESS)
				return (status);
		}
		/*
		 * For an underlying interface, we need to clear the
		 * IFF_NOFAILOVER and IFF_DEPRECATED flags that were set
		 * by the dhcpagent. Also, check if interfaces needs to be
		 * brought up to make it usable in the group.
		 */
		if (i_ipadm_is_under_ipmp(iph, addr->ipadm_ifname))
			return (i_ipadm_up_underif(iph, addr));
	}

	return (IPADM_SUCCESS);
}

/*
 * Communicates with the dhcpagent to send a dhcp message of type `type'.
 * It returns the dhcp error in `dhcperror' if a non-null pointer is provided
 * in `dhcperror'. If `type' is DHCP_STATUS, the function returns the
 * dhcp reply ipc structure in `dhreply', which should be freed by the caller.
 */
static ipadm_status_t
i_ipadm_op_dhcp(ipadm_addrobj_t addr, dhcp_ipc_type_t type, int *dhcperror,
    dhcp_ipc_reply_t **dhreply)
{
	dhcp_ipc_request_t	*request;
	dhcp_ipc_reply_t	*reply	= NULL;
	char			ifname[LIFNAMSIZ];
	int			error;
	int			dhcp_timeout;
	const void		*buffer = NULL;
	uint32_t		buffer_size = 0;
	dhcp_data_type_t	data_type = DHCP_TYPE_NONE;

	/* Construct a message to the dhcpagent. */
	bzero(&ifname, sizeof (ifname));
	i_ipadm_addrobj2lifname(addr, ifname, sizeof (ifname));
	if (type == DHCP_START) {
		buffer_size = strlen(addr->ipadm_reqhost);
		if (buffer_size != 0) {
			buffer = addr->ipadm_reqhost;
			data_type = DHCP_TYPE_REQHOST;
		}
	}
	if (addr->ipadm_primary)
		type |= DHCP_PRIMARY;
	if (addr->ipadm_af == AF_INET6)
		type |= DHCP_V6;
	request = dhcp_ipc_alloc_request(type, ifname, buffer, buffer_size,
	    data_type);
	if (request == NULL)
		return (IPADM_NO_MEMORY);

	if (addr->ipadm_wait == IPADM_DHCP_WAIT_FOREVER)
		dhcp_timeout = DHCP_IPC_WAIT_FOREVER;
	else if (addr->ipadm_wait == IPADM_DHCP_WAIT_DEFAULT)
		dhcp_timeout = DHCP_IPC_WAIT_DEFAULT;
	else
		dhcp_timeout = addr->ipadm_wait;
	/* Send the message to dhcpagent. */
	error = dhcp_ipc_make_request(request, &reply, dhcp_timeout);
	free(request);
	if (error == 0) {
		error = reply->return_code;
		if (error == 0 && (type & DHCP_STATUS) && dhreply != NULL)
			*dhreply = reply;
		else
			free(reply);
	}
	if (error != 0) {
		if (dhcperror != NULL)
			*dhcperror = error;
		if (error != DHCP_IPC_E_TIMEOUT) {
			if (error == DHCP_IPC_E_INVIF)
				return (IPADM_DHCP_INVALID_IF);
			if (error == DHCP_IPC_E_RUNNING)
				return (IPADM_OBJ_EXISTS);
			return (IPADM_DHCP_IPC_FAILURE);
		} else if (dhcp_timeout != 0) {
			return (IPADM_DHCP_IPC_TIMEOUT);
		}
	}

	return (IPADM_SUCCESS);
}

/*
 * Used to obtain dhcp information that includes the lease details, the
 * Client ID and its type, for the given `addr' object.
 */
ipadm_status_t
i_ipadm_get_dhcp_info(ipadm_addrobj_t addr, ipadm_addr_info_t *ainfo)
{
	dhcp_ipc_reply_t	*dhreply = NULL;
	dhcp_status_t		*dhstatus;
	int			dherror;
	size_t			reply_size;
	ipadm_status_t		status;
	dhcp_symbol_t		*entry = NULL;
	char			*typestr;
	char			*sep;
	int32_t			save_lifnum;
	boolean_t		isv6 = (addr->ipadm_af == AF_INET6);

	if (isv6) {
		save_lifnum = addr->ipadm_lifnum;
		addr->ipadm_lifnum = 0;
	}
	status = i_ipadm_op_dhcp(addr, DHCP_STATUS, &dherror, &dhreply);
	if (status != IPADM_SUCCESS) {
		status = IPADM_SUCCESS;
		goto ret;
	}

	dhstatus = dhcp_ipc_get_data(dhreply, &reply_size, NULL);
	if (reply_size < DHCP_STATUS_VER2_SIZE) {
		status = IPADM_DHCP_IPC_FAILURE;
		goto ret;
	}
	ainfo->ia_lease_begin = dhstatus->if_began;
	ainfo->ia_lease_expire = dhstatus->if_lease;
	ainfo->ia_lease_renew = dhstatus->if_t1;

	/*
	 * If no Client ID is returned by dhcpagent, this means that
	 * the MAC address of the interface is used as the default Client ID.
	 * When that happens, we retrieve the MAC address to construct the
	 * Client ID and set the Client ID type to -1 to indicate "default".
	 */
	if (dhstatus->if_cidlen == 0) {
		dlpi_handle_t	dh = NULL;
		dlpi_info_t	dlinfo;
		uchar_t		*hwaddr;
		priv_set_t	*pset;

		if ((pset = priv_allocset()) == NULL ||
		    getppriv(PRIV_EFFECTIVE, pset) != 0 ||
		    !priv_ismember(pset, "net_rawaccess")) {
			status = IPADM_SUCCESS;
			priv_freeset(pset);
			goto ret;
		}
		priv_freeset(pset);
		if (dlpi_open(addr->ipadm_ifname, &dh, 0) != DLPI_SUCCESS) {
			status = IPADM_DLPI_FAILURE;
			goto ret;
		}
		if (dlpi_bind(dh, ETHERTYPE_IP, NULL) != DLPI_SUCCESS ||
		    dlpi_info(dh, &dlinfo, DLPI_INFO_VERSION) != DLPI_SUCCESS) {
			dlpi_close(dh);
			status = IPADM_DLPI_FAILURE;
			goto ret;
		}
		dlpi_close(dh);
		if (dlinfo.di_physaddrlen > 0) {
			hwaddr = malloc(dlinfo.di_physaddrlen + 1);
			if (hwaddr == NULL) {
				status = IPADM_NO_MEMORY;
				goto ret;
			}
			hwaddr[0] = 1;
			(void) memcpy(hwaddr + 1, dlinfo.di_physaddr,
			    dlinfo.di_physaddrlen);
			status = i_ipadm_parse_legacy_cid(hwaddr,
			    dlinfo.di_physaddrlen + 1, &ainfo->ia_clientid);
			if (status == IPADM_SUCCESS)
				ainfo->ia_clientid_type = IPADM_CID_DEFAULT;
			free(hwaddr);
		}
		goto ret;
	}
	entry = inittab_getbyname(ITAB_CAT_STANDARD | (isv6 ? ITAB_CAT_V6 : 0),
	    ITAB_CONS_INFO, "clientid");
	if (entry == NULL) {
		status = IPADM_OBJ_NOTFOUND;
		goto ret;
	}

	/*
	 * Now we parse the Client ID returned by the dhcpagent. It could be
	 * of type DSYM_OCTET (only for IPv4) or DSYM_DUID (for both IPv4
	 * and IPv6).
	 */
	if (entry->ds_type == DSYM_OCTET) {
		uchar_t	*cp = dhstatus->if_cid;
		size_t	n_octets = dhstatus->if_cidlen;
		boolean_t legacy_cid = B_FALSE;

		assert(!isv6);
		/*
		 * Check to see if this is a RFC 4361 client identifier.
		 */
		if (dhstatus->if_cid[0] == 0xFF) {
			if (n_octets <= 5) {
				status = IPADM_OBJ_NOTFOUND;
				goto ret;
			}
			cp += 5;
			n_octets -= 5;
			entry->ds_type = DSYM_DUID;
			status = i_ipadm_parse_rfc3315_duid(entry, cp,
			    n_octets, &ainfo->ia_clientid);
			/*
			 * A DUID was not found. This should be a simple
			 * hex string that can be parsed by using
			 * i_ipadm_parse_legacy_cid().
			 */
			if (status == IPADM_OBJ_NOTFOUND)
				legacy_cid = B_TRUE;
			else if (status != IPADM_SUCCESS)
				goto ret;
		} else {
			legacy_cid = B_TRUE;
		}
		/*
		 * The client identifier is a simple hex string.
		 */
		if (legacy_cid) {
			status = i_ipadm_parse_legacy_cid(cp, n_octets,
			    &ainfo->ia_clientid);
			if (status != IPADM_SUCCESS)
				goto ret;
		}
	} else if (entry->ds_type == DSYM_DUID) {
		status = i_ipadm_parse_rfc3315_duid(entry, dhstatus->if_cid,
		    dhstatus->if_cidlen, &ainfo->ia_clientid);
		if (status != IPADM_SUCCESS)
			goto ret;
	}

	/*
	 * Get the Client ID type. If it is a DUID, the type is the first
	 * set of digits followed by the ',' separator. If not, we set it
	 * to 0 to indicate "other".
	 */
	sep = strchr(ainfo->ia_clientid, ',');
	if (sep != NULL) {
		ipadm_cid_type_t	type;

		typestr = malloc(sep - ainfo->ia_clientid + 1);
		if (typestr == NULL)
			goto ret;
		(void) strlcpy(typestr, ainfo->ia_clientid,
		    sep - ainfo->ia_clientid + 1);
		type = atoi(typestr);
		switch (type) {
		case DHCPV6_DUID_LLT:
			ainfo->ia_clientid_type = IPADM_CID_DUID_LLT;
			break;
		case DHCPV6_DUID_LL:
			ainfo->ia_clientid_type = IPADM_CID_DUID_LL;
			break;
		case DHCPV6_DUID_EN:
			ainfo->ia_clientid_type = IPADM_CID_DUID_EN;
			break;
		default:
			ainfo->ia_clientid_type = IPADM_CID_OTHER;
			break;
		}
		free(typestr);
	}
ret:
	if (isv6)
		addr->ipadm_lifnum = save_lifnum;
	free(entry);
	free(dhreply);
	return (status);
}

/*
 * From the given hex input, parse a legacy (RFC 2132) client identifier
 * which is a simple hex string.
 */
static ipadm_status_t
i_ipadm_parse_legacy_cid(uchar_t *inp, size_t n_octets, char **cid)
{
	char	tmp[3];
	size_t	cidlen;

	cidlen = sizeof ("0x") + n_octets * sizeof ("NN") + 1;
	if ((*cid = malloc(cidlen)) == NULL)
		return (IPADM_NO_MEMORY);
	(void) snprintf(*cid, cidlen, "0x");
	while (n_octets-- > 0) {
		(void) snprintf(tmp, sizeof (tmp), "%02X", *inp++);
		(void) strlcat(*cid, tmp, cidlen);
	}
	return (IPADM_SUCCESS);
}

/*
 * From the given hex input, parse a RFC3315 DUID. If no DUID is found,
 * return IPADM_OBJ_NOTFOUND.
 * The DUID is of the format
 *	decimal,data......
 *  where the value of the `decimal' ranges from 0-65536 and the `data'
 * interpreted based on the `decimal' value, by inittab_decode() function.
 */
static ipadm_status_t
i_ipadm_parse_rfc3315_duid(dhcp_symbol_t *ds, uchar_t *cid, size_t cidlen,
    char **duid)
{
	char		*value;
	uint16_t	duidtype = (cid[0] << 8) + cid[1];

	/*
	 * For DUID-LL type of DUID, libdhcpagent returns a duid_llt_t type
	 * after adding a timestamp with the current time value.
	 * We need to convert it into type duid_ll_t before passing to
	 * inittab_decode() to print it in DUID-LL format.
	 */
	if (duidtype == DHCPV6_DUID_LL) {
		duid_ll_t	*dll;
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		duid_llt_t	*dllt = (duid_llt_t *)cid;
		size_t		dll_size;
		uchar_t		*cp;

		dll_size = cidlen - sizeof (*dllt) + sizeof (*dll);
		dll = malloc(dll_size);
		if (dll == NULL)
			return (IPADM_NO_MEMORY);
		dll->dll_dutype = dllt->dllt_dutype;
		dll->dll_hwtype = dllt->dllt_hwtype;
		cp = (uchar_t *)dll;
		(void) memcpy(cp + sizeof (*dll), cid + sizeof (*dllt),
		    dll_size - sizeof (*dll));
		value = inittab_decode(ds, cp, dll_size, B_TRUE);
		free(dll);
	} else {
		value = inittab_decode(ds, cid, cidlen, B_TRUE);
	}
	if (value == NULL)
		return (IPADM_OBJ_NOTFOUND);
	*duid = malloc(strlen(value) + 1);
	if (*duid == NULL) {
		free(value);
		return (IPADM_NO_MEMORY);
	}
	(void) strlcpy(*duid, value, strlen(value) + 1);
	free(value);
	return (IPADM_SUCCESS);
}

/*
 * Returns the IP addresses of the specified interface in both the
 * active and the persistent configuration. If no
 * interface is specified, it returns all non-zero IP addresses
 * configured on all interfaces in active and persistent
 * configurations.
 * `addrinfo' will contain addresses that are
 * (1) in both active and persistent configuration (created persistently)
 * (2) only in active configuration (created temporarily)
 * (3) only in persistent configuration (disabled addresses)
 *
 * Address list that is returned by this function must be freed
 * using the ipadm_freeaddr_info() function.
 */
ipadm_status_t
ipadm_addr_info(ipadm_handle_t iph, const char *ifname,
    ipadm_addr_info_t **addrinfo, uint32_t flags, int64_t lifc_flags)
{
	ifspec_t	ifsp;

	if (addrinfo == NULL || iph == NULL)
		return (IPADM_INVALID_ARG);
	if (ifname != NULL &&
	    (!ifparse_ifspec(ifname, &ifsp) || ifsp.ifsp_lunvalid)) {
		return (IPADM_INVALID_ARG);
	}
	return (i_ipadm_get_all_addr_info(iph, ifname, addrinfo,
	    flags, lifc_flags));
}

/*
 * Frees the structure allocated by ipadm_addr_info().
 */
void
ipadm_free_addr_info(ipadm_addr_info_t *ainfo)
{
	ipadm_addr_info_t *a;

	if (ainfo == NULL)
		return;

	for (a = ainfo; a != NULL; a = IA_NEXT(a))
		free(a->ia_clientid);
	freeifaddrs((struct ifaddrs *)ainfo);
}

/*
 * Makes a door call to ipmgmtd to update its `aobjmap' with the address
 * object in `ipaddr'. This door call also updates the persistent DB to
 * remember address object to be recreated on next reboot or on an
 * ipadm_enable_addr()/ipadm_enable_if() call.
 */
ipadm_status_t
i_ipadm_addr_persist(ipadm_handle_t iph, const ipadm_addrobj_t ipaddr,
    boolean_t default_prefixlen, uint32_t flags)
{
	char			*aname = ipaddr->ipadm_aobjname;
	nvlist_t		*nvl;
	int			err = 0;
	ipadm_status_t		status;
	char			pval[MAXPROPVALLEN];
	uint_t			pflags = 0;
	ipadm_prop_desc_t	*pdp = NULL;

	/*
	 * Construct the nvl to send to the door.
	 */
	if (nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0) != 0)
		return (IPADM_NO_MEMORY);
	if ((err = nvlist_add_string(nvl, IPADM_NVP_IFNAME,
	    ipaddr->ipadm_ifname)) != 0 ||
	    (err = nvlist_add_string(nvl, IPADM_NVP_AOBJNAME, aname)) != 0 ||
	    (err = nvlist_add_int32(nvl, IPADM_NVP_LIFNUM,
	    ipaddr->ipadm_lifnum)) != 0) {
		status = ipadm_errno2status(err);
		goto ret;
	}
	switch (ipaddr->ipadm_atype) {
	case IPADM_ADDR_STATIC:
		status = i_ipadm_add_ipaddr2nvl(nvl, ipaddr);
		if (status != IPADM_SUCCESS)
			break;
		(void) snprintf(pval, sizeof (pval), "%d",
		    ipaddr->ipadm_static_prefixlen);
		if (flags & IPADM_OPT_UP)
			err = nvlist_add_string(nvl, "up", "yes");
		else
			err = nvlist_add_string(nvl, "up", "no");
		status = ipadm_errno2status(err);
		break;
	case IPADM_ADDR_DHCP:
		status = i_ipadm_add_dhcp2nvl(nvl, ipaddr->ipadm_primary,
		    ipaddr->ipadm_wait);
		(void) nvlist_add_string(nvl, IPADM_NVP_REQHOST,
		    ipaddr->ipadm_reqhost);
		break;
	case IPADM_ADDR_IPV6_ADDRCONF:
		status = i_ipadm_add_intfid2nvl(nvl, ipaddr);
		if (status != IPADM_SUCCESS)
			break;
		if (ipaddr->ipadm_dintfid.sin6_family != AF_UNSPEC) {
			err = nvlist_add_uint8_array(nvl, IPADM_NVP_DINTFID,
			    ipaddr->ipadm_dintfid.sin6_addr.s6_addr, 16);
			status = ipadm_errno2status(err);
		}
		break;
	}
	if (status != IPADM_SUCCESS)
		goto ret;

	if (iph->ih_flags & IH_INIT) {
		/*
		 * IPMGMT_MODIFY tells the ipmgmtd to set both IPMGMT_ACTIVE
		 * and IPMGMT_PERSIST on the address object in its `aobjmap'.
		 * For the callers ipadm_enable_if() and ipadm_enable_addr(),
		 * IPADM_OPT_PERSIST is not set in their flags. They send
		 * IH_INIT in ih_flags, so that the address object will be
		 * set as both IPMGMT_ACTIVE and IPMGMT_PERSIST.
		 */
		pflags |= IPMGMT_MODIFY;
	} else {
		if (flags & IPADM_OPT_ACTIVE)
			pflags |= IPMGMT_ACTIVE;
		if (flags & IPADM_OPT_MODIFY)
			pflags |= IPMGMT_MODIFY;
		if (flags & IPADM_OPT_PERSIST)
			pflags |= IPMGMT_PERSIST;
	}
	status = i_ipadm_addr_persist_nvl(iph, nvl, pflags);
	/*
	 * prefixlen is stored in a separate line in the DB and not along
	 * with the address itself, since it is also an address property and
	 * all address properties are stored in separate lines. We need to
	 * persist the prefixlen by calling the function that persists
	 * address properties.
	 */
	if (status == IPADM_SUCCESS && !default_prefixlen &&
	    ipaddr->ipadm_atype == IPADM_ADDR_STATIC &&
	    (flags & IPADM_OPT_PERSIST)) {
		for (pdp = ipadm_addrprop_table; pdp->ipd_name != NULL; pdp++) {
			if (strcmp("prefixlen", pdp->ipd_name) == 0)
				break;
		}
		assert(pdp != NULL);
		status = i_ipadm_persist_propval(iph, pdp, pval, ipaddr, flags);
	}
ret:
	nvlist_free(nvl);
	return (status);
}

/*
 * Makes the door call to ipmgmtd to store the address object in the
 * nvlist `nvl'.
 */
static ipadm_status_t
i_ipadm_addr_persist_nvl(ipadm_handle_t iph, nvlist_t *nvl, uint32_t flags)
{
	char			*buf = NULL, *nvlbuf = NULL;
	size_t			nvlsize, bufsize;
	ipmgmt_setaddr_arg_t	*sargp;
	int			err;

	err = nvlist_pack(nvl, &nvlbuf, &nvlsize, NV_ENCODE_NATIVE, 0);
	if (err != 0)
		return (ipadm_errno2status(err));
	bufsize = sizeof (*sargp) + nvlsize;
	buf = calloc(1, bufsize);
	sargp = (void *)buf;
	sargp->ia_cmd = IPMGMT_CMD_SETADDR;
	sargp->ia_flags = flags;
	sargp->ia_nvlsize = nvlsize;
	(void) bcopy(nvlbuf, buf + sizeof (*sargp), nvlsize);
	err = ipadm_door_call(iph, buf, bufsize, NULL, 0);
	free(buf);
	free(nvlbuf);
	return (ipadm_errno2status(err));
}

/*
 * Makes a door call to ipmgmtd to remove the address object in `ipaddr'
 * from its `aobjmap'. This door call also removes the address object and all
 * its properties from the persistent DB if IPADM_OPT_PERSIST is set in
 * `flags', so that the object will not be recreated on next reboot or on an
 * ipadm_enable_addr()/ipadm_enable_if() call.
 */
ipadm_status_t
i_ipadm_delete_addrobj(ipadm_handle_t iph, const ipadm_addrobj_t ipaddr,
    uint32_t flags)
{
	ipmgmt_addr_arg_t	arg;
	int			err;

	arg.ia_cmd = IPMGMT_CMD_RESETADDR;
	arg.ia_flags = 0;
	if (flags & IPADM_OPT_ACTIVE)
		arg.ia_flags |= IPMGMT_ACTIVE;
	if (flags & IPADM_OPT_PERSIST)
		arg.ia_flags |= IPMGMT_PERSIST;
	(void) strlcpy(arg.ia_aobjname, ipaddr->ipadm_aobjname,
	    sizeof (arg.ia_aobjname));
	arg.ia_lnum = ipaddr->ipadm_lifnum;
	err = ipadm_door_call(iph, &arg, sizeof (arg), NULL, 0);
	return (ipadm_errno2status(err));
}

/*
 * Checks if the caller is authorized for the up/down operation.
 * Retrieves the address object corresponding to `aobjname' from ipmgmtd
 * and retrieves the address flags for that object from kernel.
 * The arguments `ipaddr' and `ifflags' must be allocated by the caller.
 */
static ipadm_status_t
i_ipadm_updown_common(ipadm_handle_t iph, const char *aobjname,
    ipadm_addrobj_t ipaddr, uint32_t ipadm_flags, uint64_t *ifflags)
{
	ipadm_status_t	status;
	char		lifname[LIFNAMSIZ];

	/* check for solaris.network.interface.config authorization */
	if (!ipadm_check_auth())
		return (IPADM_INSUFF_AUTH);

	/* validate input */
	if (aobjname == NULL || strlcpy(ipaddr->ipadm_aobjname, aobjname,
	    IPADM_AOBJSIZ) >= IPADM_AOBJSIZ) {
		return (IPADM_INVALID_ARG);
	}

	/* Retrieve the address object information. */
	status = i_ipadm_get_addrobj(iph, ipaddr);
	if (status != IPADM_SUCCESS)
		return (status);

	if (!(ipaddr->ipadm_flags & IPMGMT_ACTIVE))
		return (IPADM_OP_DISABLE_OBJ);
	if ((ipadm_flags & IPADM_OPT_PERSIST) &&
	    !(ipaddr->ipadm_flags & IPMGMT_PERSIST))
		return (IPADM_TEMPORARY_OBJ);
	if (ipaddr->ipadm_atype == IPADM_ADDR_IPV6_ADDRCONF ||
	    (ipaddr->ipadm_atype == IPADM_ADDR_DHCP &&
	    (ipadm_flags & IPADM_OPT_PERSIST)))
		return (IPADM_OP_NOTSUP);

	i_ipadm_addrobj2lifname(ipaddr, lifname, sizeof (lifname));
	return (i_ipadm_get_flags(iph, lifname, ipaddr->ipadm_af, ifflags));
}

/*
 * Marks the address in the address object `aobjname' up. This operation is
 * not supported for an address object of type IPADM_ADDR_IPV6_ADDRCONF.
 * For an address object of type IPADM_ADDR_DHCP, this operation can
 * only be temporary and no updates will be made to the persistent DB.
 */
ipadm_status_t
ipadm_up_addr(ipadm_handle_t iph, const char *aobjname, uint32_t ipadm_flags)
{
	struct ipadm_addrobj_s ipaddr;
	ipadm_status_t		status;
	uint64_t		ifflags;
	char			lifname[LIFNAMSIZ];


	status = i_ipadm_updown_common(iph, aobjname, &ipaddr, ipadm_flags,
	    &ifflags);
	if (status != IPADM_SUCCESS)
		return (status);
	/*
	 * If the address is already a duplicate, then refresh-addr
	 * should be used to mark it up. For the legacy case, we should
	 * go ahead and try to mark it up to keep the backward
	 * compatibility.
	 */
	if (!i_ipadm_is_legacy(iph) && (ifflags & IFF_DUPLICATE))
		return (IPADM_DAD_FOUND);
	if (ifflags & IFF_UP)
		goto persist;
	if (i_ipadm_is_under_ipmp(iph, ipaddr.ipadm_ifname)) {
		status = i_ipadm_up_addr_underif(iph, &ipaddr, ifflags);
	} else {
		i_ipadm_addrobj2lifname(&ipaddr, lifname, sizeof (lifname));
		status = i_ipadm_set_flags(iph, lifname, ipaddr.ipadm_af,
		    IFF_UP, 0);
	}
	if (status != IPADM_SUCCESS)
		return (status);
persist:
	/* Update persistent DB. */
	if (ipadm_flags & IPADM_OPT_PERSIST) {
		status = i_ipadm_persist_propval(iph, &up_addrprop, "yes",
		    &ipaddr, 0);
	}
	return (status);
}

/*
 * Function called when an address on an underlying interface
 * is being brought up.
 *
 * If the address is a data address, this function handles the migration
 * of the data address from the underlying interface to the IPMP interface
 * that might happen in the kernel. It involves removing the address object
 * on the underif and recreating a new address object for the migrated address
 * on the IPMP interface.
 */
static ipadm_status_t
i_ipadm_up_addr_underif(ipadm_handle_t iph, ipadm_addrobj_t ipaddr,
    uint64_t ifflags)
{
	ipadm_status_t		status;
	char			lifname[LIFNAMSIZ];
	char			ipmpif[LIFNAMSIZ];
	struct sockaddr_storage	addr;
	int			sock;
	struct lifreq		lifr;
	uint64_t		ipmpflags;
	boolean_t		legacy = i_ipadm_is_legacy(iph);
	boolean_t		migrate = B_FALSE;

	status = i_ipadm_ipmpif(iph, ipaddr->ipadm_ifname, ipmpif, LIFNAMSIZ);
	if (status != IPADM_SUCCESS)
		return (status);
	/*
	 * If we are bringing up the IPv6 interface, we might need to
	 * bring up the IPv6 part of the IPMP interface.
	 */
	if (legacy && ipaddr->ipadm_lifnum == 0 &&
	    ipaddr->ipadm_af == AF_INET6) {
		status = i_ipadm_get_flags(iph, ipmpif, AF_INET6, &ipmpflags);
		if (status != IPADM_SUCCESS)
			return (status);
		if (!(ipmpflags & IFF_UP)) {
			status = i_ipadm_set_flags(iph, ipmpif, AF_INET6,
			    IFF_UP, 0);
			if (status != IPADM_SUCCESS)
				return (status);
		}
	}

	/*
	 * Get the data address to help find the new logical interface that
	 * will be created by kernel on the IPMP interface during migration.
	 */
	sock = IPADM_SOCK(iph, ipaddr->ipadm_af);
	i_ipadm_addrobj2lifname(ipaddr, lifname, sizeof (lifname));
	if (!(ifflags & IFF_NOFAILOVER)) {
		(void) strlcpy(lifr.lifr_name, lifname,
		    sizeof (lifr.lifr_name));
		if (ioctl(sock, SIOCGLIFADDR, &lifr) < 0)
			return (ipadm_errno2status(errno));
		if (!sockaddrunspec((struct sockaddr *)&lifr.lifr_addr)) {
			addr = lifr.lifr_addr;
			migrate = B_TRUE;
		}
	}

	/* Bring up the address */
	status = i_ipadm_set_flags(iph, lifname, ipaddr->ipadm_af, IFF_UP, 0);
	if (status != IPADM_SUCCESS || !migrate)
		return (status);

	if (migrate) {
		ipadm_migrate_addr_t	*ptr;
		list_t			maddr;

		list_create(&maddr, sizeof (ipadm_migrate_addr_t),
		    offsetof(ipadm_migrate_addr_t, im_link));

		ptr = malloc(sizeof (ipadm_migrate_addr_t));
		ptr->im_lifnum = ipaddr->ipadm_lifnum;
		(void) strlcpy(ptr->im_aobjname, ipaddr->ipadm_aobjname,
		    sizeof (ptr->im_aobjname));
		ptr->im_addr = addr;
		list_insert_head(&maddr, ptr);

		status = i_ipadm_migrate_addrs(iph, ipaddr->ipadm_ifname,
		    &maddr, ipmpif);

		list_remove(&maddr, ptr);
		list_destroy(&maddr);
		if (status != IPADM_SUCCESS && !legacy)
			return (IPADM_ADDROBJ_NOT_CREATED);
	}
	return (IPADM_SUCCESS);
}

/*
 * Marks the address in the address object `aobjname' down. This operation is
 * not supported for an address object of type IPADM_ADDR_IPV6_ADDRCONF.
 * For an address object of type IPADM_ADDR_DHCP, this operation can
 * only be temporary and no updates will be made to the persistent DB.
 */
ipadm_status_t
ipadm_down_addr(ipadm_handle_t iph, const char *aobjname, uint32_t ipadm_flags)
{
	struct ipadm_addrobj_s ipaddr;
	ipadm_status_t	status;
	struct lifreq	lifr;
	uint64_t	flags;

	status = i_ipadm_updown_common(iph, aobjname, &ipaddr, ipadm_flags,
	    &flags);
	if (status != IPADM_SUCCESS)
		return (status);
	i_ipadm_addrobj2lifname(&ipaddr, lifr.lifr_name,
	    sizeof (lifr.lifr_name));
	if (flags & IFF_UP) {
		status = i_ipadm_set_flags(iph, lifr.lifr_name,
		    ipaddr.ipadm_af, 0, IFF_UP);
		if (status != IPADM_SUCCESS)
			return (status);
	} else if (flags & IFF_DUPLICATE) {
		/*
		 * Clear the IFF_DUPLICATE flag.
		 */
		if (ioctl(iph->ih_sock, SIOCGLIFADDR, &lifr) < 0)
			return (ipadm_errno2status(errno));
		if (ioctl(iph->ih_sock, SIOCSLIFADDR, &lifr) < 0)
			return (ipadm_errno2status(errno));
	}

	/* Update persistent DB */
	if (ipadm_flags & IPADM_OPT_PERSIST) {
		status = i_ipadm_persist_propval(iph, &up_addrprop,
		    "no", &ipaddr, 0);
	}

	return (status);
}

/*
 * Refreshes the address in the address object `aobjname'. If the address object
 * is of type IPADM_ADDR_STATIC, DAD is re-initiated on the address. If
 * `ipadm_flags' has IPADM_OPT_INFORM set, a DHCP_INFORM message is sent to the
 * dhcpagent for this static address. If the address object is of type
 * IPADM_ADDR_DHCP, a DHCP_EXTEND message is sent to the dhcpagent.
 * If a dhcp address has not yet been acquired, a DHCP_START is sent to the
 * dhcpagent. This operation is not supported for an address object of
 * type IPADM_ADDR_IPV6_ADDRCONF.
 */
ipadm_status_t
ipadm_refresh_addr(ipadm_handle_t iph, const char *aobjname,
    uint32_t ipadm_flags)
{
	ipadm_status_t		status = IPADM_SUCCESS;
	uint64_t		flags;
	struct ipadm_addrobj_s	ipaddr;
	sa_family_t		af;
	char			lifname[LIFNAMSIZ];
	boolean_t		inform =
	    ((ipadm_flags & IPADM_OPT_INFORM) != 0);
	int			dherr;

	/* check for solaris.network.interface.config authorization */
	if (!ipadm_check_auth())
		return (IPADM_INSUFF_AUTH);

	bzero(&ipaddr, sizeof (ipaddr));
	/* validate input */
	if (aobjname == NULL || strlcpy(ipaddr.ipadm_aobjname, aobjname,
	    IPADM_AOBJSIZ) >= IPADM_AOBJSIZ) {
		return (IPADM_INVALID_ARG);
	}

	/* Retrieve the address object information. */
	status = i_ipadm_get_addrobj(iph, &ipaddr);
	if (status != IPADM_SUCCESS)
		return (status);

	if (!(ipaddr.ipadm_flags & IPMGMT_ACTIVE))
		return (IPADM_OP_DISABLE_OBJ);

	if (ipadm_is_vni(ipaddr.ipadm_ifname))
		return (IPADM_OP_NOTSUP);
	if (inform && ipaddr.ipadm_atype != IPADM_ADDR_STATIC)
		return (IPADM_INVALID_ARG);
	af = ipaddr.ipadm_af;
	if (ipaddr.ipadm_atype == IPADM_ADDR_STATIC) {
		i_ipadm_addrobj2lifname(&ipaddr, lifname, sizeof (lifname));
		status = i_ipadm_get_flags(iph, lifname, af, &flags);
		if (status != IPADM_SUCCESS)
			return (status);
		if (inform) {
			if (dhcp_start_agent(DHCP_IPC_MAX_WAIT) == -1)
				return (IPADM_CANNOT_START_DHCP);

			ipaddr.ipadm_wait = IPADM_DHCP_WAIT_DEFAULT;
			return (i_ipadm_op_dhcp(&ipaddr, DHCP_INFORM, NULL,
			    NULL));
		}
		if (!(flags & IFF_DUPLICATE))
			return (IPADM_SUCCESS);
		status = i_ipadm_set_flags(iph, lifname, af, IFF_UP, 0);
	} else if (ipaddr.ipadm_atype == IPADM_ADDR_DHCP) {
		status = i_ipadm_op_dhcp(&ipaddr, DHCP_EXTEND, &dherr, NULL);
		/*
		 * Restart the dhcp address negotiation with server if no
		 * address has been acquired yet.
		 */
		if (status != IPADM_SUCCESS && dherr == DHCP_IPC_E_OUTSTATE) {
			ipaddr.ipadm_wait = IPADM_DHCP_WAIT_DEFAULT;
			status = i_ipadm_op_dhcp(&ipaddr, DHCP_START, NULL,
			    NULL);
		}
	} else {
		status = IPADM_OP_NOTSUP;
	}
	return (status);
}

/*
 * This is called from ipadm_create_addr() to validate the address parameters.
 * It does the following steps:
 * 1. Validates the interface name.
 * 2. In case of a persistent operation, verifies that the interface
 *	is persistent. Returns error if interface is not enabled but
 *	is in persistent config.
 * 3. Verifies that the destination address is not set for a non-POINTOPOINT
 *	interface.
 * 4. Verifies that the address type is not DHCP or ADDRCONF when the
 *	interface is a vni or a loopback interface.
 * 5. Verifies that the address type is not DHCP or ADDRCONF when the interface
 *	has IFF_VRRP interface flag set.
 */
static ipadm_status_t
i_ipadm_validate_create_addr(ipadm_handle_t iph, ipadm_addrobj_t ipaddr,
    uint32_t flags)
{
	sa_family_t		af;
	sa_family_t		other_af;
	char			*ifname;
	ipadm_status_t		status;
	boolean_t		legacy = i_ipadm_is_legacy(iph);
	boolean_t		islo, isvni;
	uint64_t		ifflags = 0;
	boolean_t		p_exists;
	boolean_t		af_exists, other_af_exists, a_exists;

	if (ipaddr == NULL || flags == 0 || flags == IPADM_OPT_PERSIST ||
	    (flags & ~(IPADM_COMMON_OPT_MASK|IPADM_OPT_UP|
	    IPADM_OPT_NWAM_OVERRIDE))) {
		return (IPADM_INVALID_ARG);
	}

	if (ipaddr->ipadm_af == AF_UNSPEC)
		return (IPADM_BAD_ADDR);

	if (!legacy) {
		if (ipaddr->ipadm_lifnum != 0)
			return (IPADM_INVALID_ARG);
	} else {
		if (ipaddr->ipadm_atype != IPADM_ADDR_STATIC &&
		    ipaddr->ipadm_atype != IPADM_ADDR_DHCP)
			return (IPADM_OP_NOTSUP);
	}

	ifname = ipaddr->ipadm_ifname;

	af = ipaddr->ipadm_af;
	af_exists = ipadm_if_enabled(iph, ifname, af);
	/*
	 * For legacy case, interfaces are not implicitly plumbed. We need to
	 * check if the interface exists in the active configuration.
	 */
	if (legacy && !af_exists)
		return (IPADM_NOSUCH_IF);

	islo = ipadm_is_loopback(ifname);
	other_af = (af == AF_INET ? AF_INET6 : AF_INET);
	other_af_exists = ipadm_if_enabled(iph, ifname, other_af);
	/*
	 * Check if one of the v4 or the v6 interfaces exists in the
	 * active configuration. An interface is considered disabled only
	 * if both v4 and v6 are not active.
	 */
	a_exists = (af_exists || other_af_exists);

	/* Check if interface exists in the persistent configuration. */
	p_exists = i_ipadm_if_pexists(iph, ifname, AF_UNSPEC);
	if (!a_exists) {
		if (p_exists && !(flags & IPADM_OPT_NWAM_OVERRIDE))
			return (IPADM_OP_DISABLE_OBJ);
		if (!islo)
			return (IPADM_NOSUCH_IF);
	} else {
		if ((flags & IPADM_OPT_PERSIST) && !p_exists) {
			/*
			 * If address has to be created persistently,
			 * and the interface does not exist in the persistent
			 * store but in active config, fail.
			 */
			return (IPADM_TEMPORARY_OBJ);
		}
	}
	if (!islo) {
		if (af_exists) {
			status = i_ipadm_get_flags(iph, ifname, af, &ifflags);
		} else {
			status = i_ipadm_get_flags(iph, ifname, other_af,
			    &ifflags);
		}
		if (status != IPADM_SUCCESS)
			return (status);
	}

	/* Perform validation steps (4) and (5) */
	islo = ipadm_is_loopback(ifname);
	isvni = ipadm_is_vni(ifname);
	switch (ipaddr->ipadm_atype) {
	case IPADM_ADDR_STATIC:
		if ((islo || isvni) && ipaddr->ipadm_static_dname[0] != '\0')
			return (IPADM_INVALID_ARG);
		/* Check for a valid src address */
		if (!legacy && sockaddrunspec(
		    (struct sockaddr *)&ipaddr->ipadm_static_addr))
			return (IPADM_BAD_ADDR);
		break;
	case IPADM_ADDR_DHCP:
		if (islo || isvni || (ifflags & IFF_VRRP))
			return (IPADM_OP_NOTSUP);
		break;
	case IPADM_ADDR_IPV6_ADDRCONF:
		if (islo || isvni || (ifflags & IFF_VRRP) ||
		    i_ipadm_is_6to4(iph, ifname)) {
			return (IPADM_OP_NOTSUP);
		}
		break;
	default:
		return (IPADM_INVALID_ARG);
	}

	return (IPADM_SUCCESS);
}

ipadm_status_t
i_ipadm_merge_prefixlen_from_nvl(nvlist_t *invl, nvlist_t *onvl,
    const char *aobjname)
{
	nvpair_t	*nvp, *prefixnvp;
	nvlist_t	*tnvl;
	char		*aname;
	int		err;

	for (nvp = nvlist_next_nvpair(invl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(invl, nvp)) {
		if (nvpair_value_nvlist(nvp, &tnvl) == 0 &&
		    nvlist_exists(tnvl, IPADM_NVP_PREFIXLEN) &&
		    nvlist_lookup_string(tnvl, IPADM_NVP_AOBJNAME,
		    &aname) == 0 && strcmp(aname, aobjname) == 0) {
			/* prefixlen exists for given address object */
			(void) nvlist_lookup_nvpair(tnvl, IPADM_NVP_PREFIXLEN,
			    &prefixnvp);
			err = nvlist_add_nvpair(onvl, prefixnvp);
			if (err == 0) {
				err = nvlist_remove(invl, nvpair_name(nvp),
				    nvpair_type(nvp));
			}
			return (ipadm_errno2status(err));
		}
	}
	return (IPADM_SUCCESS);
}

/*
 * Re-enables the address object `aobjname' based on the saved
 * configuration for `aobjname'.
 */
ipadm_status_t
ipadm_enable_addr(ipadm_handle_t iph, const char *aobjname, uint32_t flags)
{
	nvlist_t	*addrnvl, *nvl;
	nvpair_t	*nvp;
	ipadm_status_t	status;
	struct ipadm_addrobj_s ipaddr;

	/* check for solaris.network.interface.config authorization */
	if (!ipadm_check_auth())
		return (IPADM_INSUFF_AUTH);

	/* validate input */
	if (flags & IPADM_OPT_PERSIST)
		return (IPADM_OP_NOTSUP);
	if (aobjname == NULL || strlcpy(ipaddr.ipadm_aobjname, aobjname,
	    IPADM_AOBJSIZ) >= IPADM_AOBJSIZ) {
		return (IPADM_INVALID_ARG);
	}

	/* Retrieve the address object information. */
	status = i_ipadm_get_addrobj(iph, &ipaddr);
	if (status != IPADM_SUCCESS)
		return (status);
	if (ipaddr.ipadm_flags & IPMGMT_ACTIVE)
		return (IPADM_ADDROBJ_EXISTS);

	status = i_ipadm_get_db_addr(iph, NULL, aobjname, &addrnvl);
	if (status != IPADM_SUCCESS)
		return (status);

	assert(addrnvl != NULL);

	for (nvp = nvlist_next_nvpair(addrnvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(addrnvl, nvp)) {
		if (nvpair_value_nvlist(nvp, &nvl) != 0)
			continue;

		if (nvlist_exists(nvl, IPADM_NVP_IPV4ADDR) ||
		    nvlist_exists(nvl, IPADM_NVP_IPV6ADDR)) {
			status = i_ipadm_merge_prefixlen_from_nvl(addrnvl, nvl,
			    aobjname);
			if (status != IPADM_SUCCESS)
				continue;
		}
		iph->ih_flags |= IH_INIT;
		status = i_ipadm_init_addrobj(iph, nvl);
		iph->ih_flags &= ~IH_INIT;
		if (status != IPADM_SUCCESS)
			break;
	}

	return (status);
}

/*
 * Disables the address object in `aobjname' from the active configuration.
 * Error code return values follow the model in ipadm_delete_addr().
 */
ipadm_status_t
ipadm_disable_addr(ipadm_handle_t iph, const char *aobjname, uint32_t flags)
{
	/* validate input */
	if (flags & IPADM_OPT_PERSIST)
		return (IPADM_OP_NOTSUP);

	return (ipadm_delete_addr(iph, aobjname, IPADM_OPT_ACTIVE));
}

/*
 * This function constructs an IPADM_ADDR_IPV6_ADDRCONF address object and
 * creates it on `underif'. This is called whenever the `underif' is added
 * to an IPMP group or when the `underif' is enabled.
 */
ipadm_status_t
i_ipadm_create_ipv6_on_underif(ipadm_handle_t iph, const char *underif)
{
	ipadm_status_t		status;
	struct ipadm_addrobj_s	addr;
	boolean_t		reset_flags = B_FALSE;

	i_ipadm_init_addr(&addr, underif, "", IPADM_ADDR_STATIC);
	addr.ipadm_af = AF_INET6;
	status = i_ipadm_lookupadd_addrobj(iph, &addr);
	if (status != IPADM_SUCCESS)
		return (status);
	/*
	 * This function may be called in the path of ipadm_enable_if(),
	 * for `underif', in which case IH_INIT will be set in the ih_flags.
	 * We turn it off to avoid misleading ipmgmtd that the addrconf object
	 * exists persistently.
	 */
	if (iph->ih_flags & IH_INIT) {
		iph->ih_flags &= ~IH_INIT;
		reset_flags = B_TRUE;
	}
	status = i_ipadm_create_linklocal(iph, &addr);
	if (status != IPADM_SUCCESS)
		goto out;
	status = i_ipadm_addr_persist(iph, &addr, B_FALSE, IPADM_OPT_ACTIVE);
out:
	if (status != IPADM_SUCCESS) {
		(void) i_ipadm_delete_addr(iph, &addr);
		(void) i_ipadm_delete_addrobj(iph, &addr, IPADM_OPT_ACTIVE);
	}
	if (reset_flags)
		iph->ih_flags |= IH_INIT;
	return (status);
}

/*
 * Function used to migrate the list of address objects in `maddrs' from
 * `underif' to `ipmpif'. This is called when an interface is added to an
 * IPMP interface in legacy mode and the underif happens to have data
 * addresses configured.
 */
ipadm_status_t
i_ipadm_migrate_addrs(ipadm_handle_t iph, const char *underif, list_t *maddrs,
    const char *ipmpif)
{
	nvlist_t	*nvl, *addr;
	size_t		nvlsize, bufsize;
	char		*buf = NULL, *nvlbuf = NULL;
	int		err;
	struct in6_addr	addr6;
	struct sockaddr_in *sin;
	struct sockaddr_in6 *sin6;
	ipmgmt_migrateaddr_arg_t *margp;
	ipadm_migrate_addr_t *ptr;

	if (list_is_empty(maddrs))
		return (IPADM_SUCCESS);
	if ((err = nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0)) != 0)
		return (ipadm_errno2status(err));
	ptr = list_head(maddrs);
	for (; ptr != NULL; ptr = list_next(maddrs, ptr)) {
		if ((err = nvlist_alloc(&addr, NV_UNIQUE_NAME, 0)) != 0)
			goto fail;
		if ((err = nvlist_add_uint32(addr, IPADM_NVP_LIFNUM,
		    ptr->im_lifnum)) != 0) {
			nvlist_free(addr);
			goto fail;
		}
		if (ptr->im_addr.ss_family == AF_INET) {
			sin = SIN(&ptr->im_addr);
			err = nvlist_add_uint32(addr, IPADM_NVP_IPV4ADDR,
			    sin->sin_addr.s_addr);
		} else {
			sin6 = SIN6(&ptr->im_addr);
			addr6 = sin6->sin6_addr;
			err = nvlist_add_uint8_array(addr, IPADM_NVP_IPV6ADDR,
			    addr6.s6_addr, 16);
		}
		if (err == 0)
			err = nvlist_add_nvlist(nvl, ptr->im_aobjname, addr);
		if (err != 0) {
			nvlist_free(addr);
			goto fail;
		}
		nvlist_free(addr);
	}
	if (nvlist_empty(nvl) ||
	    nvlist_pack(nvl, &nvlbuf, &nvlsize, NV_ENCODE_NATIVE, 0) != 0) {
		nvlist_free(nvl);
		return (IPADM_NO_MEMORY);
	}
	bufsize = sizeof (*margp) + nvlsize;
	buf = calloc(1, bufsize);
	if (buf == NULL)
		goto fail;
	margp = (void *)buf;
	margp->ia_cmd = IPMGMT_CMD_MOVE_ADDROBJ;
	margp->ia_nvlsize = nvlsize;
	(void) strlcpy(margp->ia_underif, underif, sizeof (margp->ia_underif));
	(void) strlcpy(margp->ia_ipmpif, ipmpif, sizeof (margp->ia_ipmpif));
	(void) bcopy(nvlbuf, buf + sizeof (*margp), nvlsize);
	err = ipadm_door_call(iph, buf, bufsize, NULL, 0);
fail:
	free(buf);
	free(nvlbuf);
	nvlist_free(nvl);
	return (ipadm_errno2status(err));
}

/*
 * Gets the IPMP interface name for the given underlying interface `ifname'.
 */
ipadm_status_t
i_ipadm_ipmpif(ipadm_handle_t iph, const char *ifname, char *ipmpif, size_t len)
{
	lifgroupinfo_t	lifgr;
	char		grname[LIFGRNAMSIZ];
	uint_t		bufsize = sizeof (grname);
	ipadm_status_t	status;

	status = i_ipadm_get_groupname(iph, ifname, grname, bufsize);
	if (status != IPADM_SUCCESS)
		return (status);

	(void) strlcpy(lifgr.gi_grname, grname, LIFGRNAMSIZ);
	if (ioctl(iph->ih_sock, SIOCGLIFGROUPINFO, &lifgr) < 0)
		return (ipadm_errno2status(errno));
	(void) strlcpy(ipmpif, lifgr.gi_grifname, len);

	return (IPADM_SUCCESS);
}
