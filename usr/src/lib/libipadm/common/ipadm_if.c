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

#include <errno.h>
#include <sys/sockio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <stropts.h>
#include <strings.h>
#include <libdlpi.h>
#include <libdllink.h>
#include <libinetutil.h>
#include <ipmp_mpathd.h>
#include <ipmp_admin.h>
#include <inet/ip.h>
#include <limits.h>
#include <zone.h>
#include <ipadm_ndpd.h>
#include "libipadm_impl.h"
#include "libdlvnic.h"

static const ipadm_appflags_t ipadm_appflags_tbl[] = {
	{ "dhcpagent(1M)", IFF_DHCPRUNNING, IPADM_UNDERIF_DHCP_MANAGED, 1 },
	{ "in.ndpd(1M)",   IFF_ADDRCONF,    IPADM_UNDERIF_NDPD_MANAGED, 3 },
	{ NULL,		0,			0,			0 }
};

static ipadm_status_t	i_ipadm_slifname_arp(char *, uint64_t, int);
static ipadm_status_t	i_ipadm_slifname(ipadm_handle_t, char *, char *,
			    uint64_t, int, uint32_t);
static ipadm_status_t	i_ipadm_create_ipmp_peer(ipadm_handle_t, const char *,
			    sa_family_t);
static ipadm_status_t	i_ipadm_create_ipmp(ipadm_handle_t, char *,
			    sa_family_t, boolean_t, uint32_t);
extern ipadm_status_t	i_ipadm_remove_ipmp(ipadm_handle_t, const char *,
			    const char *, const char *, uint32_t);
extern ipadm_status_t	i_ipadm_update_ipmp(ipadm_handle_t, const char *,
			    const char *, ipadm_ipmpop_t, uint32_t);
static ipadm_status_t	i_ipadm_persist_if(ipadm_handle_t, const char *,
			    sa_family_t, ipadm_if_class_t);
static ipadm_status_t	i_ipadm_persist_update_ipmp(ipadm_handle_t,
			    const char *, const char *, ipadm_ipmpop_t);
static ipadm_status_t	i_ipadm_mark_testaddrs_common(ipadm_handle_t,
			    const char *, boolean_t);
static boolean_t	i_ipadm_is_dataaddr(ipadm_addr_info_t *);

static ipadm_status_t
dlpi2ipadm_status(int error)
{
	switch (error) {
	case DLPI_EBADLINK:
	case DLPI_ENOLINK:
	case DLPI_ENOTSTYLE2:
		return (IPADM_DLPI_NOLINK);
	case DL_SYSERR:
		return (ipadm_errno2status(errno));
	default:
		return (IPADM_DLPI_FAILURE);
	}
}

struct {
	uint64_t ii_kflags;
	uint64_t ii_uflags;
} ipadm_ifflags_tbl[] = {
	{ IFF_BROADCAST, IPADM_IFF_BROADCAST },
	{ IFF_INACTIVE, IPADM_IFF_INACTIVE },
	{ IFF_IPV4, IPADM_IFF_IPV4 },
	{ IFF_IPV6, IPADM_IFF_IPV6 },
	{ IFF_L3PROTECT, IPADM_IFF_L3PROTECT },
	{ IFF_MULTICAST, IPADM_IFF_MULTICAST },
	{ IFF_NOACCEPT, IPADM_IFF_NOACCEPT },
	{ IFF_POINTOPOINT, IPADM_IFF_POINTOPOINT },
	{ IFF_STANDBY, IPADM_IFF_STANDBY },
	{ IFF_VIRTUAL, IPADM_IFF_VIRTUAL },
	{ IFF_VRRP, IPADM_IFF_VRRP },
	{ 0, 0 }
};

/*
 * Function that maps IFF_* flags from kernel to IPADM_IFF_* flags.
 */
uint64_t
i_ipadm_kflags2uflags(uint64_t ifflags)
{
	int		i;
	uint64_t	uflags = 0;

	for (i = 0; ipadm_ifflags_tbl[i].ii_kflags != 0; i++)
		if (ifflags & ipadm_ifflags_tbl[i].ii_kflags)
			uflags |= ipadm_ifflags_tbl[i].ii_uflags;

	return (uflags);
}

/*
 * Returns B_FALSE if the interface in `ifname' has at least one address that is
 * IFF_UP in the addresses in `ifa'.
 */
boolean_t
i_ipadm_is_if_down(char *ifname, struct ifaddrs *ifa)
{
	struct ifaddrs	*ifap;
	char		cifname[LIFNAMSIZ];
	char		*sep;

	for (ifap = ifa; ifap != NULL; ifap = ifap->ifa_next) {
		(void) strlcpy(cifname, ifap->ifa_name, sizeof (cifname));
		if ((sep = strrchr(cifname, IPADM_LOGICAL_SEP)) != NULL)
			*sep = '\0';
		/*
		 * If this condition is true, there is at least one
		 * address that is IFF_UP. So, we need to return B_FALSE.
		 */
		if (strcmp(cifname, ifname) == 0 &&
		    (ifap->ifa_flags & IFF_UP)) {
			return (B_FALSE);
		}
	}
	/* We did not find any IFF_UP addresses. */
	return (B_TRUE);
}

/*
 * Allocate a structure of type `ipadm_ifname_t'.
 */
ipadm_status_t
i_ipadm_alloc_ifname(ipadm_ifname_t **ifp, const char *ifname)
{
	*ifp = calloc(1, sizeof (**ifp));
	if (*ifp == NULL)
		return (ipadm_errno2status(errno));
	(void) strlcpy((*ifp)->ifn_name, ifname, sizeof ((*ifp)->ifn_name));
	return (IPADM_SUCCESS);
}

/*
 * Allocate a structure of type `ipadm_if_info_t'.
 */
ipadm_status_t
i_ipadm_alloc_if_info(ipadm_if_info_t **ifp, const char *ifname)
{
	*ifp = calloc(1, sizeof (ipadm_if_info_t));
	if (*ifp == NULL)
		return (ipadm_errno2status(errno));
	list_create(&(*ifp)->ifi_unders, sizeof (ipadm_ifname_t),
	    offsetof(ipadm_ifname_t, ifn_link));
	list_create(&(*ifp)->ifi_punders, sizeof (ipadm_ifname_t),
	    offsetof(ipadm_ifname_t, ifn_link));
	(void) strlcpy((*ifp)->ifi_name, ifname, sizeof ((*ifp)->ifi_name));
	return (IPADM_SUCCESS);
}

/*
 * Returns B_TRUE if `ifname' in the given list of structures of type
 * `ipadm_ifname_t', B_FALSE otherwise.
 */
boolean_t
i_ipadm_ifname_in_list(list_t *ifnames, const char *ifname)
{
	ipadm_ifname_t	*ifp;

	ifp = list_head(ifnames);
	for (; ifp != NULL; ifp = list_next(ifnames, ifp))
		if (strcmp(ifname, ifp->ifn_name) == 0)
			return (B_TRUE);
	return (B_FALSE);
}

/*
 * Gets all the underlying interfaces for the interface `ifname', using
 * `ifa' which contains all the addresses on the system.  The underlying
 * interface names will be added to the list `unders'.
 */
static ipadm_status_t
i_ipadm_get_underifs(ipadm_handle_t iph, const char *ifname, list_t *unders,
    struct ifaddrs *ifa)
{
	ipadm_ifname_t	*ifp;
	ipadm_status_t	status;
	char		i_grname[LIFGRNAMSIZ];
	char		u_grname[LIFGRNAMSIZ];
	uint_t		bufsize = sizeof (i_grname);

	status = i_ipadm_get_groupname(iph, ifname, i_grname, bufsize);
	if (status != IPADM_SUCCESS)
		return (status);
	for (; ifa != NULL; ifa = ifa->ifa_next) {
		if (i_ipadm_get_lnum(ifa->ifa_name) != 0 ||
		    strcmp(ifa->ifa_name, ifname) == 0)
			continue;
		if (i_ipadm_ifname_in_list(unders, ifa->ifa_name) ||
		    i_ipadm_get_groupname(iph, ifa->ifa_name, u_grname,
		    bufsize) != IPADM_SUCCESS ||
		    strcmp(u_grname, i_grname) != 0)
			continue;

		status = i_ipadm_alloc_ifname(&ifp, ifa->ifa_name);
		if (status != IPADM_SUCCESS)
			goto fail;
		list_insert_tail(unders, ifp);
	}

	return (IPADM_SUCCESS);
fail:
	while ((ifp = list_remove_head(unders)) != NULL)
		free(ifp);
	return (status);
}

/*
 * Retrieves the information for the interface `ifname' from active
 * config if `ifname' is specified and returns the result in the list `if_info'.
 * Otherwise, it retrieves the information for all the interfaces in
 * the active config and returns the result in the list `if_info'.
 */
static ipadm_status_t
i_ipadm_active_if_info(ipadm_handle_t iph, const char *ifname,
    list_t *if_info, int64_t lifc_flags)
{
	struct lifreq	*buf;
	struct lifreq	*lifrp;
	ipadm_if_info_t	*ifp;
	struct ifaddrs	*ifa;
	int		n;
	int		numifs;
	ipadm_status_t	status;
	sa_family_t	af;
	uint64_t	ifflags;

retry:
	list_create(if_info, sizeof (ipadm_if_info_t),
	    offsetof(ipadm_if_info_t, ifi_link));
	/*
	 * Get information for all interfaces.
	 */
	if (getallifs(iph->ih_sock, 0, &buf, &numifs, lifc_flags) != 0)
		return (ipadm_errno2status(errno));

	/*
	 * We need to retrieve the addresses on the interface to determine
	 * the interface's state. For e.g., the interface state is "ok",
	 * if it has at least one IFF_UP IP address, else the state is "down".
	 * One point to note here is that we cannot use the `lifc_flags'
	 * input parameter to get addresses here, because it may not give us
	 * all the addresses.  We provide all the LIFC flags to retrieve all
	 * configured addresses.
	 */
	if (getallifaddrs(AF_UNSPEC, &ifa, (LIFC_NOXMIT|LIFC_TEMPORARY|
	    LIFC_ALLZONES|LIFC_UNDER_IPMP), 0) != 0) {
		return (ipadm_errno2status(errno));
	}
	lifrp = buf;
	for (n = 0; n < numifs; n++, lifrp++) {
		/* Skip interfaces with logical num != 0 */
		if (i_ipadm_get_lnum(lifrp->lifr_name) != 0)
			continue;
		/*
		 * Skip the current interface if a specific `ifname' has
		 * been requested and current interface does not match
		 * `ifname'.
		 */
		if (ifname != NULL && strcmp(lifrp->lifr_name, ifname) != 0)
			continue;
		/*
		 * There is no need to run the loop more than once for
		 * an interface, even though it appears twice in `buf', once
		 * for IPv4 and again for IPv6. We can get all the information
		 * for the interface in just one iteration. So, we continue
		 * if the interface already exists in our list.
		 */
		ifp = list_head(if_info);
		for (; ifp != NULL; ifp = list_next(if_info, ifp)) {
			if (strcmp(lifrp->lifr_name, ifp->ifi_name) == 0)
				break;
		}
		if (ifp != NULL)
			continue;
		status = i_ipadm_alloc_if_info(&ifp, lifrp->lifr_name);
		if (status != IPADM_SUCCESS)
			goto fail;
		list_insert_tail(if_info, ifp);

		/*
		 * Retrieve the flags for the interface to populate the
		 * fields `ifi_cflags',`ifi_class', `ifi_active', and
		 * `ifi_unders'.
		 */
		af = lifrp->lifr_addr.ss_family;
		status = i_ipadm_get_flags(iph, ifp->ifi_name, af, &ifflags);
		if (status != IPADM_SUCCESS) {
			if (status == IPADM_NOSUCH_IF) {
				/*
				 * The interface might have been removed
				 * from kernel. Retry getting all the active
				 * interfaces.
				 */
				freeifaddrs(ifa);
				ipadm_free_if_info(if_info);
				goto retry;
			}
			goto fail;
		}
		if (!(ifflags & IFF_RUNNING) ||
		    (ifflags & IFF_FAILED)) {
			ifp->ifi_state = IPADM_IFS_FAILED;
		} else if (ifflags & IFF_OFFLINE) {
			ifp->ifi_state = IPADM_IFS_OFFLINE;
		} else if (i_ipadm_is_if_down(ifp->ifi_name, ifa)) {
			ifp->ifi_state = IPADM_IFS_DOWN;
		} else {
			ifp->ifi_state = IPADM_IFS_OK;
			if (!(ifflags & IFF_INACTIVE))
				ifp->ifi_active = B_TRUE;
		}
		ifp->ifi_cflags = i_ipadm_kflags2uflags(ifflags);
		if (ifflags & IFF_IPMP) {
			ifp->ifi_class = IPADMIF_CLASS_IPMP;
			status = i_ipadm_get_underifs(iph, ifp->ifi_name,
			    &ifp->ifi_unders, ifa);
			if (status != IPADM_SUCCESS)
				goto fail;
		} else if (ipadm_is_vni(ifp->ifi_name)) {
			ifp->ifi_class = IPADMIF_CLASS_VNI;
		} else if (ipadm_is_loopback(ifp->ifi_name)) {
			ifp->ifi_class = IPADMIF_CLASS_LOOPBACK;
		} else {
			ifp->ifi_class = IPADMIF_CLASS_IP;
			if (i_ipadm_is_under_ipmp(iph, ifp->ifi_name))
				ifp->ifi_cflags |= IPADM_IFF_UNDERIF;
		}
		/*
		 * Some interface flags are address family specific. Since
		 * the loop runs only once for an interface, we also get
		 * the flags it is plumbed for the other address family,
		 * so that we have all the flags for the interface.
		 */
		status = i_ipadm_get_flags(iph, ifp->ifi_name,
		    IPADM_OTHER_AF(af), &ifflags);
		if (status == IPADM_SUCCESS)
			ifp->ifi_cflags |= i_ipadm_kflags2uflags(ifflags);

	}
	freeifaddrs(ifa);
	free(buf);
	if (ifname != NULL && list_is_empty(if_info)) {
		list_destroy(if_info);
		return (IPADM_NOSUCH_IF);
	}
	return (IPADM_SUCCESS);
fail:
	free(buf);
	ipadm_free_if_info(if_info);
	return (status);
}

/*
 * Returns the interface information for `ifname' in `if_info' from persistent
 * config if `ifname' is non-null. Otherwise, it returns all the interfaces
 * from persistent config in `if_info'.
 */
ipadm_status_t
i_ipadm_persist_if_info(ipadm_handle_t iph, const char *ifname,
    list_t *if_info)
{
	int			err = 0;
	ipadm_status_t		status = IPADM_SUCCESS;
	ipmgmt_getif_arg_t	getif;
	ipmgmt_get_rval_t	*rvalp;
	ipadm_if_info_t		*ifp;
	nvlist_t		*nvlif, *onvl = NULL;
	nvpair_t		*nvp, *nvp2;
	char			*onvlbuf;
	size_t			nvlsize;
	char			*nvname;
	sa_family_t		af;
	ipadm_ifname_t		*underif;
	char			*af_str, *class, *ipmpif, *unders, *standby;
	char			*cp, *lasts;

	bzero(&getif, sizeof (getif));
	if (ifname != NULL)
		(void) strlcpy(getif.ia_ifname, ifname, LIFNAMSIZ);
	getif.ia_cmd = IPMGMT_CMD_GETIF;

	list_create(if_info, sizeof (ipadm_if_info_t),
	    offsetof(ipadm_if_info_t, ifi_link));
	if ((rvalp = malloc(sizeof (ipmgmt_get_rval_t))) == NULL) {
		status = ipadm_errno2status(errno);
		goto fail;
	}
	err = ipadm_door_dyncall(iph, &getif, sizeof (getif), (void **)&rvalp,
	    sizeof (*rvalp));
	if (err != 0) {
		free(rvalp);
		if (err == ENOENT && ifname == NULL)
			return (IPADM_SUCCESS);
		status = ipadm_errno2status(err);
		goto fail;
	}
	nvlsize = rvalp->ir_nvlsize;
	onvlbuf = (char *)rvalp + sizeof (ipmgmt_get_rval_t);
	err = nvlist_unpack(onvlbuf, nvlsize, &onvl, NV_ENCODE_NATIVE);
	free(rvalp);
	if (err != 0) {
		status = ipadm_errno2status(err);
		goto fail;
	}
	for (nvp = nvlist_next_nvpair(onvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(onvl, nvp)) {
		if (nvpair_value_nvlist(nvp, &nvlif) != 0)
			continue;

		status = i_ipadm_alloc_if_info(&ifp, nvpair_name(nvp));
		if (status != IPADM_SUCCESS)
			goto fail;
		list_insert_tail(if_info, ifp);
		class = NULL;
		af_str = NULL;
		ipmpif = NULL;
		unders = NULL;
		standby = NULL;
		for (nvp2 = nvlist_next_nvpair(nvlif, NULL); nvp2 != NULL;
		    nvp2 = nvlist_next_nvpair(nvlif, nvp2)) {
			nvname = nvpair_name(nvp2);
			if (strcmp(nvname, IPADM_NVP_FAMILY) == 0)
				(void) nvpair_value_string(nvp2, &af_str);
			else if (strcmp(nvname, IPADM_NVP_IFCLASS) == 0)
				(void) nvpair_value_string(nvp2, &class);
			else if (strcmp(nvname, IPADM_NVP_UNDERIF) == 0)
				(void) nvpair_value_string(nvp2, &unders);
			else if (strcmp(nvname, IPADM_NVP_IPMPIF) == 0)
				(void) nvpair_value_string(nvp2, &ipmpif);
			else if (strcmp(nvname, "standby") == 0)
				(void) nvpair_value_string(nvp2, &standby);
		}
		assert(class != NULL);
		assert(af_str != NULL);
		ifp->ifi_class = atoi(class);
		if (ipmpif != NULL && ipmpif[0] != '\0')
			ifp->ifi_pflags |= IPADM_IFF_UNDERIF;
		if (standby != NULL && strcmp(standby, "on") == 0)
			ifp->ifi_pflags |= IPADM_IFF_STANDBY;
		for (cp = strtok_r(af_str, ",", &lasts); cp != NULL;
		    cp = strtok_r(NULL, ",", &lasts)) {
			af = atoi(cp);
			if (af == AF_INET) {
				ifp->ifi_pflags |= IPADM_IFF_IPV4;
			} else {
				assert(af == AF_INET6);
				ifp->ifi_pflags |= IPADM_IFF_IPV6;
			}
		}
		if (unders != NULL) {
			for (cp = strtok_r(unders, ",", &lasts); cp != NULL;
			    cp = strtok_r(NULL, ",", &lasts)) {
				status = i_ipadm_alloc_ifname(&underif, cp);
				if (status != IPADM_SUCCESS)
					goto fail;
				list_insert_tail(&ifp->ifi_punders, underif);
			}
		}
	}
	nvlist_free(onvl);
	return (IPADM_SUCCESS);
fail:
	ipadm_free_if_info(if_info);
	nvlist_free(onvl);
	return (status);
}

/*
 * Collects information for `ifname' if one is specified from both
 * active and persistent config in `if_info'. If no `ifname' is specified,
 * this returns all the interfaces in active and persistent config in
 * `if_info'.
 */
ipadm_status_t
i_ipadm_get_all_if_info(ipadm_handle_t iph, const char *ifname,
    list_t *if_info, int64_t lifc_flags)
{
	ipadm_status_t	status, a_status;
	list_t		pifinfo;
	ipadm_if_info_t	*aifp;
	ipadm_if_info_t	*pifp, *pifp_next;

	/*
	 * Retrieve the information for the requested `ifname' or all
	 * interfaces from active configuration.
	 */
	a_status = i_ipadm_active_if_info(iph, ifname, if_info, lifc_flags);
	if (a_status != IPADM_SUCCESS && a_status != IPADM_NOSUCH_IF)
		return (a_status);
	/*
	 * Get the persistent interface information in `pifinfo'.
	 */
	status = i_ipadm_persist_if_info(iph, ifname, &pifinfo);
	/*
	 * If no persistent information is found for `ifname', return
	 * the `if_info' from the active configuration, if any.
	 * In this case, we return the status from the call to
	 * i_ipadm_active_if_info(), which would be either IPADM_SUCCESS or
	 * IPADM_NOSUCH_IF.
	 */
	if (status == IPADM_OBJ_NOTFOUND)
		return (a_status);
	if (status != IPADM_SUCCESS) {
		if (a_status == IPADM_SUCCESS)
			goto fail;
		return (status);
	}
	if (a_status == IPADM_NOSUCH_IF) {
		/*
		 * We are going to append the persistent interface
		 * configuration later in this function, to the list `if_info'
		 * that was created by i_ipadm_active_if_info().
		 * However, when no interface was in the active config,
		 * the list would not be available in `if_info'.
		 * To proceed, we create the list in `if_info'.
		 */
		list_create(if_info, sizeof (ipadm_if_info_t),
		    offsetof(ipadm_if_info_t, ifi_link));
	}
	/*
	 * If a persistent interface is also found in `if_info', update
	 * its entry in `if_info' with the persistent information from
	 * `pifinfo'. If an interface is found in `pifinfo', but not in
	 * `if_info', it means that this interface was disabled. We should
	 * add this interface to `if_info' and set it state to
	 * IPADM_IFF_DISABLED.
	 */
	for (pifp = list_head(&pifinfo); pifp != NULL; pifp = pifp_next) {
		pifp_next = list_next(&pifinfo, pifp);
		aifp = list_head(if_info);
		for (; aifp != NULL; aifp = list_next(if_info, aifp)) {
			if (strcmp(aifp->ifi_name, pifp->ifi_name) == 0)
				break;
		}
		if (aifp == NULL) {
			list_remove(&pifinfo, pifp);
			pifp->ifi_state = IPADM_IFS_DISABLED;
			list_insert_tail(if_info, pifp);
		} else {
			aifp->ifi_pflags = pifp->ifi_pflags;
			/*
			 * Move the list of underlying interfaces from
			 * `pifp' to `aifp'.
			 */
			list_move_tail(&aifp->ifi_punders, &pifp->ifi_punders);
		}
	}
	ipadm_free_if_info(&pifinfo);
	return (IPADM_SUCCESS);
fail:
	ipadm_free_if_info(if_info);
	return (status);
}

int
i_ipadm_get_lnum(const char *ifname)
{
	char *num = strrchr(ifname, IPADM_LOGICAL_SEP);

	if (num == NULL)
		return (0);

	return (atoi(++num));
}

void
i_ipadm_get_ifname(const char *lifname, char *ifname, size_t len)
{
	char	*cp;

	(void) strlcpy(ifname, lifname, len);
	if ((cp = strrchr(ifname, IPADM_LOGICAL_SEP)) != NULL)
		*cp = '\0';
}

/*
 * Returns B_TRUE or B_FALSE based on whether any persistent configuration
 * is available for `ifname' for the address family `af'. If `af' = AF_UNSPEC,
 * it returns B_TRUE if at least one of AF_INET or AF_INET6 exists.
 */
boolean_t
i_ipadm_if_pexists(ipadm_handle_t iph, const char *ifname, sa_family_t af)
{
	ipadm_if_info_t	*ifp;
	ipadm_status_t	status;
	list_t		ifinfo;
	boolean_t	isv4, isv6;
	boolean_t	exists = B_FALSE;

	/*
	 * if IH_IPMGMTD is set, we know that the caller (ipmgmtd) already
	 * knows about persistent configuration in the first place, so we
	 * just return B_FALSE.
	 */
	if (iph->ih_flags & IH_IPMGMTD)
		return (B_FALSE);
	status = i_ipadm_persist_if_info(iph, ifname, &ifinfo);
	if (status == IPADM_SUCCESS) {
		ifp = list_head(&ifinfo);
		isv4 = ((ifp->ifi_pflags & IPADM_IFF_IPV4) != 0);
		isv6 = ((ifp->ifi_pflags & IPADM_IFF_IPV6) != 0);
		exists = ((af == AF_INET && isv4) ||
		    (af == AF_INET6 && isv6) ||
		    (af == AF_UNSPEC && (isv4 || isv6)));
		ipadm_free_if_info(&ifinfo);
	}
	return (exists);
}

/*
 * Checks if the IPMP interface in `ifname' is persistent.
 */
boolean_t
i_ipadm_ipmp_pexists(ipadm_handle_t iph, const char *ifname)
{
	ipadm_if_info_t	*ifp;
	boolean_t	exists = B_FALSE;
	list_t		ifinfo;

	if (i_ipadm_persist_if_info(iph, ifname, &ifinfo) == IPADM_SUCCESS) {
		ifp = list_head(&ifinfo);
		if (ifp->ifi_class == IPADMIF_CLASS_IPMP)
			exists = B_TRUE;
		ipadm_free_if_info(&ifinfo);
	}
	return (exists);
}

/*
 * Open "/dev/udp{,6}" for use as a multiplexor to PLINK the interface stream
 * under. We use "/dev/udp" instead of "/dev/ip" since STREAMS will not let
 * you PLINK a driver under itself, and "/dev/ip" is typically the driver at
 * the bottom of the stream for tunneling interfaces.
 */
ipadm_status_t
ipadm_open_arp_on_udp(const char *udp_dev_name, int *fd)
{
	int err;

	if ((*fd = open(udp_dev_name, O_RDWR)) == -1)
		return (ipadm_errno2status(errno));

	/*
	 * Pop off all undesired modules (note that the user may have
	 * configured autopush to add modules above udp), and push the
	 * arp module onto the resulting stream. This is used to make
	 * IP+ARP be able to atomically track the muxid for the I_PLINKed
	 * STREAMS, thus it isn't related to ARP running the ARP protocol.
	 */
	while (ioctl(*fd, I_POP, 0) != -1)
		;
	if (errno == EINVAL && ioctl(*fd, I_PUSH, ARP_MOD_NAME) != -1)
		return (IPADM_SUCCESS);
	err = errno;
	(void) close(*fd);

	return (ipadm_errno2status(err));
}

/*
 * Checks if `ifname' is plumbed and in an IPMP group on its "other" address
 * family.  If so, create a matching IPMP group for address family `af'.
 */
static ipadm_status_t
i_ipadm_create_ipmp_peer(ipadm_handle_t iph, const char *ifname, sa_family_t af)
{
	lifgroupinfo_t	lifgr;
	struct lifreq	lifr;
	int 		other_af_sock;

	assert(af == AF_INET || af == AF_INET6);

	other_af_sock = IPADM_SOCK(iph, IPADM_OTHER_AF(af));

	/*
	 * iph is the handle for the interface that we are trying to plumb.
	 * other_af_sock is the socket for the "other" address family.
	 */
	bzero(&lifr, sizeof (lifr));
	(void) strlcpy(lifr.lifr_name, ifname, sizeof (lifr.lifr_name));
	if (ioctl(other_af_sock, SIOCGLIFGROUPNAME, &lifr) != 0)
		return (IPADM_SUCCESS);

	(void) strlcpy(lifgr.gi_grname, lifr.lifr_groupname, LIFGRNAMSIZ);
	if (ioctl(other_af_sock, SIOCGLIFGROUPINFO, &lifgr) != 0)
		return (IPADM_SUCCESS);

	/*
	 * If `ifname' *is* the IPMP group interface, or if the relevant
	 * address family is already configured, then there's nothing to do.
	 */
	if (strcmp(lifgr.gi_grifname, ifname) == 0 ||
	    (af == AF_INET && lifgr.gi_v4) || (af == AF_INET6 && lifgr.gi_v6)) {
		return (IPADM_SUCCESS);
	}

	return (i_ipadm_create_ipmp(iph, lifgr.gi_grifname, af, B_TRUE,
	    IPADM_OPT_ACTIVE));
}

/*
 * Issues the ioctl SIOCSLIFNAME to kernel on the given ARP stream fd.
 */
static ipadm_status_t
i_ipadm_slifname_arp(char *ifname, uint64_t flags, int fd)
{
	struct lifreq	lifr;
	ifspec_t	ifsp;

	bzero(&lifr, sizeof (lifr));
	(void) ifparse_ifspec(ifname, &ifsp);
	lifr.lifr_ppa = ifsp.ifsp_ppa;
	lifr.lifr_flags = flags;
	(void) strlcpy(lifr.lifr_name, ifname, sizeof (lifr.lifr_name));
	/*
	 * Tell ARP the name and unit number for this interface.
	 * Note that arp has no support for transparent ioctls.
	 */
	if (i_ipadm_strioctl(fd, SIOCSLIFNAME, (char *)&lifr,
	    sizeof (lifr)) == -1) {
		return (ipadm_errno2status(errno));
	}
	return (IPADM_SUCCESS);
}

/*
 * Issues the ioctl SIOCSLIFNAME to kernel. If IPADM_OPT_GENPPA is set in
 * `ipadm_flags', then a ppa will be generated. `newif' will be updated
 * with the generated ppa.
 */
static ipadm_status_t
i_ipadm_slifname(ipadm_handle_t iph, char *ifname, char *newif, uint64_t flags,
    int fd, uint32_t ipadm_flags)
{
	struct lifreq	lifr;
	ipadm_status_t	status = IPADM_SUCCESS;
	int		err = 0;
	sa_family_t	af;
	int		ppa;
	ifspec_t	ifsp;
	boolean_t	valid_if;

	bzero(&lifr, sizeof (lifr));
	if (ipadm_flags & IPADM_OPT_GENPPA) {
		/*
		 * We'd like to just set lifr_ppa to UINT_MAX and have the
		 * kernel pick a PPA.  Unfortunately, that would mishandle
		 * two cases:
		 *
		 *	1. If the PPA is available but the groupname is taken
		 *	   (e.g., the "ipmp2" IP interface name is available
		 *	   but the "ipmp2" groupname is taken) then the
		 *	   auto-assignment by the kernel will fail.
		 *
		 *	2. If we're creating (e.g.) an IPv6-only IPMP
		 *	   interface, and there's already an IPv4-only IPMP
		 *	   interface, the kernel will allow us to accidentally
		 *	   reuse the IPv6 IPMP interface name (since
		 *	   SIOCSLIFNAME uniqueness is per-interface-type).
		 *	   This will cause administrative confusion.
		 *
		 * Thus, we instead take a brute-force approach of checking
		 * whether the IPv4 or IPv6 name is already in-use before
		 * attempting the SIOCSLIFNAME.  As per (1) above, the
		 * SIOCSLIFNAME may still fail, in which case we just proceed
		 * to the next one.  If this approach becomes too slow, we
		 * can add a new SIOC* to handle this case in the kernel.
		 */
		for (ppa = 0; ppa < UINT_MAX; ppa++) {
			(void) snprintf(lifr.lifr_name, LIFNAMSIZ, "%s%d",
			    ifname, ppa);

			if (ioctl(iph->ih_sock, SIOCGLIFFLAGS, &lifr) != -1 ||
			    errno != ENXIO)
				continue;

			if (ioctl(iph->ih_sock6, SIOCGLIFFLAGS, &lifr) != -1 ||
			    errno != ENXIO)
				continue;

			lifr.lifr_ppa = ppa;
			lifr.lifr_flags = flags;

			err = ioctl(fd, SIOCSLIFNAME, &lifr);
			if (err != -1 || errno != EEXIST)
				break;
		}
		if (err == -1) {
			status = ipadm_errno2status(errno);
		} else {
			/*
			 * PPA has been successfully established.
			 * Update `newif' with the ppa.
			 */
			assert(newif != NULL);
			if (snprintf(newif, LIFNAMSIZ, "%s%d", ifname,
			    ppa) >= LIFNAMSIZ)
				return (IPADM_INVALID_ARG);
		}
	} else {
		/* We should have already validated the interface name. */
		valid_if = ifparse_ifspec(ifname, &ifsp);
		assert(valid_if);

		/*
		 * Before we call SIOCSLIFNAME, ensure that the IPMP group
		 * interface for this address family exists.  Otherwise, the
		 * kernel will kick the interface out of the group when we do
		 * the SIOCSLIFNAME.
		 *
		 * Example: suppose bge0 is plumbed for IPv4 and in group "a".
		 * If we're now plumbing bge0 for IPv6, but the IPMP group
		 * interface for "a" is not plumbed for IPv6, the SIOCSLIFNAME
		 * will kick bge0 out of group "a", which is undesired.
		 */
		if (flags & IFF_IPV4)
			af = AF_INET;
		else
			af = AF_INET6;
		status = i_ipadm_create_ipmp_peer(iph, ifname, af);
		if (status != IPADM_SUCCESS)
			return (status);
		lifr.lifr_ppa = ifsp.ifsp_ppa;
		lifr.lifr_flags = flags;
		(void) strlcpy(lifr.lifr_name, ifname, sizeof (lifr.lifr_name));
		if (ioctl(fd, SIOCSLIFNAME, &lifr) == -1)
			status = ipadm_errno2status(errno);
	}

	return (status);
}

static
boolean_t
i_ipadm_is_anet(ipadm_handle_t iph, datalink_id_t linkid)
{
	dladm_status_t		status;
	dladm_vnic_attr_t	vinfo;

	if (linkid != DATALINK_INVALID_LINKID) {
		status = dladm_vnic_info(iph->ih_dh, linkid,
		    &vinfo, DLADM_OPT_ACTIVE);
		if (status == DLADM_STATUS_OK &&
		    !vinfo.va_onloan &&
		    vinfo.va_owner_zone_id != vinfo.va_zone_id)
			return (B_TRUE);
	}
	return (B_FALSE);
}

/*
 * Plumbs the interface `ifname' for the address family `af'. It also persists
 * the interface for `af' if IPADM_OPT_PERSIST is set in `ipadm_flags'.
 */
ipadm_status_t
i_ipadm_plumb_if(ipadm_handle_t iph, char *ifname, sa_family_t af,
    ipadm_if_class_t class, uint32_t ipadm_flags)
{
	int		ip_muxid;
	int		mux_fd = -1, ip_fd, arp_fd;
	char		*udp_dev_name;
	dlpi_handle_t	dh_arp = NULL, dh_ip;
	uint64_t	ifflags;
	uint_t		dlpi_flags;
	ipadm_status_t	status = IPADM_SUCCESS;
	char		*linkname;
	char		newif[LIFNAMSIZ];
	char		lifname[LIFNAMSIZ];
	boolean_t	is_6to4 = B_FALSE;
	int		ret;
	boolean_t	is_persistent;
	boolean_t	legacy;
	uint32_t	del_flags = (ipadm_flags & IPADM_COMMON_OPT_MASK);

	if (ipadm_if_enabled(iph, ifname, af))
		return (IPADM_IF_EXISTS);

	is_persistent = ((ipadm_flags & IPADM_OPT_PERSIST) != 0);
	legacy = i_ipadm_is_legacy(iph);

	if (class == IPADMIF_CLASS_LOOPBACK) {
		status = i_ipadm_plumb_lif(iph, ifname, af, class);
		if (status == IPADM_OBJ_EXISTS)
			return (IPADM_IF_EXISTS);
		else if (status != IPADM_SUCCESS)
			return (status);
		if (is_persistent) {
			status = i_ipadm_persist_if(iph, ifname, af, class);
			if (status != IPADM_SUCCESS) {
				(void) i_ipadm_delete_if(iph, ifname, af,
				    class, IPADM_OPT_ACTIVE);
			}
		}
		return (status);
	}

	dlpi_flags = DLPI_NOATTACH;

	/*
	 * If the class is IPADMIF_CLASS_IPMP, then this is a request
	 * to create an IPMP interface atop /dev/ipmpstub0.  (We can't simply
	 * pass "ipmpstub0" as devname since an admin *could* have a normal
	 * vanity-named link named "ipmpstub0" that they'd like to plumb.)
	 */
	if (class == IPADMIF_CLASS_IPMP) {
		dlpi_flags |= DLPI_DEVONLY;
		linkname = "ipmpstub0";
	} else {
		linkname = ifname;
	}

	/*
	 * We use DLPI_NOATTACH because the ip module will do the attach
	 * itself for DLPI style-2 devices.
	 */
	if ((ret = dlpi_open(linkname, &dh_ip, dlpi_flags)) != DLPI_SUCCESS)
		return (dlpi2ipadm_status(ret));
	ip_fd = dlpi_fd(dh_ip);
	if (ioctl(ip_fd, I_PUSH, IP_MOD_NAME) == -1) {
		status = ipadm_errno2status(errno);
		goto done;
	}

	/*
	 * Set IFF_IPV4/IFF_IPV6 flags. The kernel only allows modifications
	 * to IFF_IPv4, IFF_IPV6, IFF_BROADCAST, IFF_XRESOLV, IFF_NOLINKLOCAL.
	 */
	ifflags = 0;

	/* Set the name string and the IFF_IPV* flag */
	if (af == AF_INET) {
		ifflags = IFF_IPV4;
	} else {
		ifflags = IFF_IPV6;
		/*
		 * With the legacy method, the link-local address should be
		 * configured as part of the interface plumb, using the default
		 * token. If IH_LEGACY is not specified, we want to set :: as
		 * the address and require the admin to explicitly call
		 * ipadm_create_addr() with the address object type set to
		 * IPADM_ADDR_IPV6_ADDRCONF to create the link-local address
		 * as well as the autoconfigured addresses. And we also
		 * want global addresses generated automatically for
		 * 6to4 tunnels. So set IFF_NOLINKLOCAL if the interface
		 * is not legacy or 6to4.
		 */
		is_6to4 = i_ipadm_is_6to4(iph, ifname);
		if (!legacy && !is_6to4)
			ifflags |= IFF_NOLINKLOCAL;
	}
	(void) strlcpy(newif, ifname, sizeof (newif));
	status = i_ipadm_slifname(iph, ifname, newif, ifflags, ip_fd,
	    ipadm_flags);
	if (status != IPADM_SUCCESS)
		goto done;

	/* Get the full set of existing flags for this stream */
	status = i_ipadm_get_flags(iph, newif, af, &ifflags);
	if (status != IPADM_SUCCESS)
		goto done;

	udp_dev_name = (af == AF_INET6 ? UDP6_DEV_NAME : UDP_DEV_NAME);
	status = ipadm_open_arp_on_udp(udp_dev_name, &mux_fd);
	if (status != IPADM_SUCCESS)
		goto done;

	/* Check if arp is not needed */
	if (ifflags & (IFF_NOARP|IFF_IPV6)) {
		/*
		 * PLINK the interface stream so that the application can exit
		 * without tearing down the stream.
		 */
		if ((ip_muxid = ioctl(mux_fd, I_PLINK, ip_fd)) == -1)
			status = ipadm_errno2status(errno);
		goto done;
	}

	/*
	 * This interface does use ARP, so set up a separate stream
	 * from the interface to ARP.
	 *
	 * We use DLPI_NOATTACH because the arp module will do the attach
	 * itself for DLPI style-2 devices.
	 */
	ret = dlpi_open(linkname, &dh_arp, dlpi_flags);
	if (ret != DLPI_SUCCESS) {
		status = dlpi2ipadm_status(ret);
		goto done;
	}

	arp_fd = dlpi_fd(dh_arp);
	if (ioctl(arp_fd, I_PUSH, ARP_MOD_NAME) == -1) {
		status = ipadm_errno2status(errno);
		goto done;
	}

	status = i_ipadm_slifname_arp(newif, ifflags, arp_fd);
	if (status != IPADM_SUCCESS)
		goto done;
	/*
	 * PLINK the IP and ARP streams so that ifconfig can exit
	 * without tearing down the stream.
	 */
	if ((ip_muxid = ioctl(mux_fd, I_PLINK, ip_fd)) == -1) {
		status = ipadm_errno2status(errno);
		goto done;
	}

	if (ioctl(mux_fd, I_PLINK, arp_fd) < 0) {
		status = ipadm_errno2status(errno);
		(void) ioctl(mux_fd, I_PUNLINK, ip_muxid);
	}

done:
	dlpi_close(dh_ip);
	if (dh_arp != NULL)
		dlpi_close(dh_arp);

	if (mux_fd != -1)
		(void) close(mux_fd);

	if (status != IPADM_SUCCESS)
		return (status);

	/*
	 * The ifname might have been appended with a ppa, when
	 * IPADM_OPT_GENPPA is set. Copy back the modified ifname.
	 */
	(void) strlcpy(ifname, newif, LIFNAMSIZ);

	/*
	 * If IPADM_OPT_PERSIST was set in flags, store the
	 * interface in persistent DB.
	 */
	if (is_persistent) {
		status = i_ipadm_persist_if(iph, ifname, af, class);
		if (status != IPADM_SUCCESS) {
			(void) i_ipadm_delete_if(iph, ifname, af,
			    class, IPADM_OPT_ACTIVE);
			return (status);
		}
	}

	if (af != AF_INET6)
		return (IPADM_SUCCESS);

	/*
	 * Disable autoconf in in.ndpd until the interface is
	 * either brought up or an addrconf address object is created.
	 * This does not have to be done for 6to4 tunnel interfaces,
	 * since in.ndpd will not autoconfigure those interfaces.
	 */
	if (!is_6to4)
		(void) i_ipadm_disable_autoconf(ifname);

	/*
	 * If it is a 6to4 tunnel or a legacy non-vni IPv6 interface,
	 * create a default addrobj name for the default address on the 0'th
	 * logical interface. Additionally, for the 6to4 tunnel,
	 * set IFF_UP in the interface flags.
	 */
	if ((legacy && (class != IPADMIF_CLASS_VNI)) || is_6to4) {
		struct ipadm_addrobj_s	addr;
		ipadm_addr_type_t	type;

		if (is_6to4)
			type = IPADM_ADDR_STATIC;
		else
			type = IPADM_ADDR_IPV6_ADDRCONF;
		i_ipadm_init_addr(&addr, ifname, "", type);
		addr.ipadm_af = af;
		status = i_ipadm_lookupadd_addrobj(iph, &addr);
		if (status != IPADM_SUCCESS)
			goto delete_if;
		if (is_6to4) {
			addr.ipadm_lifnum = 0;
			i_ipadm_addrobj2lifname(&addr, lifname,
			    sizeof (lifname));
			status = i_ipadm_set_flags(iph, lifname, af,
			    IFF_UP, 0);
			if (status != IPADM_SUCCESS)
				goto delete_if;
			ipadm_flags = (ipadm_flags & ~IPADM_OPT_PERSIST);
		} else {
			/*
			 * For IPv6 interfaces plumbed through
			 * ifconfig, send a request to in.ndpd
			 * to enable autoconfiguration.
			 */
			addr.ipadm_stateful = B_TRUE;
			addr.ipadm_stateless = B_TRUE;
			status = i_ipadm_send_ndpd_cmd(ifname, &addr,
			    IPADM_CREATE_ADDRS);
			if (status != IPADM_SUCCESS &&
			    status != IPADM_NDPD_NOT_RUNNING)
				goto delete_if;
		}

		status = i_ipadm_addr_persist(iph, &addr, B_FALSE, ipadm_flags);
		if (status != IPADM_SUCCESS)
			goto delete_if;
	}
	return (IPADM_SUCCESS);

delete_if:
	(void) i_ipadm_delete_if(iph, ifname, af, class, del_flags);
	return (status);
}

/*
 * Unplumbs the interface in `ifname' of family `af'.
 */
ipadm_status_t
i_ipadm_unplumb_if(ipadm_handle_t iph, const char *ifname, sa_family_t af,
    uint32_t ipadm_flags)
{
	int		ip_muxid, arp_muxid;
	int		mux_fd = -1;
	int		muxid_fd = -1;
	char		*udp_dev_name;
	uint64_t	flags;
	boolean_t	changed_arp_muxid = B_FALSE;
	int		save_errno;
	struct lifreq	lifr;
	ipadm_status_t	ret = IPADM_SUCCESS;
	int		sock;
	lifgroupinfo_t	lifgr;
	ifaddrlistx_t	*ifaddrs, *ifaddrp;
	boolean_t	v6 = (af == AF_INET6);

	/*
	 * Just do SIOCLIFREMOVEIF on loopback and non-zero
	 * logical interfaces.
	 */
	if (i_ipadm_get_lnum(ifname) != 0 || ipadm_is_loopback(ifname))
		return (i_ipadm_unplumb_lif(iph, ifname, af, ipadm_flags));

	/*
	 * We used /dev/udp or udp6 to set up the mux. So we have to use
	 * the same now for PUNLINK also.
	 */
	if (v6) {
		udp_dev_name = UDP6_DEV_NAME;
		sock = iph->ih_sock6;
	} else {
		udp_dev_name = UDP_DEV_NAME;
		sock = iph->ih_sock;
	}
	if ((muxid_fd = open(udp_dev_name, O_RDWR)) == -1) {
		ret = ipadm_errno2status(errno);
		goto done;
	}
	ret = ipadm_open_arp_on_udp(udp_dev_name, &mux_fd);
	if (ret != IPADM_SUCCESS)
		goto done;
	ret = i_ipadm_get_flags(iph, ifname, af, &flags);
	if (ret != IPADM_SUCCESS)
		goto done;
	bzero(&lifr, sizeof (lifr));
	(void) strlcpy(lifr.lifr_name, ifname, sizeof (lifr.lifr_name));
again:
	if (flags & IFF_IPMP) {
		/*
		 * There are two reasons the I_PUNLINK can fail with EBUSY:
		 * (1) if IP interfaces are in the group, or (2) if IPMP data
		 * addresses are administratively up.  For case (1), we fail
		 * here with a specific error message.  For case (2), we bring
		 * down the addresses prior to doing the I_PUNLINK.  If the
		 * I_PUNLINK still fails with EBUSY then the configuration
		 * must have changed after our checks, in which case we branch
		 * back up to `again' and rerun this logic.  The net effect is
		 * that unplumbing an IPMP interface will only fail with EBUSY
		 * if IP interfaces are in the group.
		 */
		if (ioctl(sock, SIOCGLIFGROUPNAME, &lifr) == -1) {
			ret = ipadm_errno2status(errno);
			goto done;
		}
		(void) strlcpy(lifgr.gi_grname, lifr.lifr_groupname,
		    LIFGRNAMSIZ);
		if (ioctl(sock, SIOCGLIFGROUPINFO, &lifgr) == -1) {
			ret = ipadm_errno2status(errno);
			goto done;
		}
		if ((v6 && lifgr.gi_nv6 != 0) || (!v6 && lifgr.gi_nv4 != 0)) {
			ret = IPADM_GRP_NOTEMPTY;
			goto done;
		}

		/*
		 * The kernel will fail the I_PUNLINK if the IPMP interface
		 * has administratively up addresses; bring them down.
		 */
		if (ifaddrlistx(ifname, IFF_UP|IFF_DUPLICATE,
		    0, &ifaddrs) == -1) {
			ret = ipadm_errno2status(errno);
			goto done;
		}
		ifaddrp = ifaddrs;
		for (; ifaddrp != NULL; ifaddrp = ifaddrp->ia_next) {
			int sock = (ifaddrp->ia_flags & IFF_IPV4) ?
			    iph->ih_sock : iph->ih_sock6;
			struct lifreq lifrl;

			if (((ifaddrp->ia_flags & IFF_IPV6) && !v6) ||
			    (!(ifaddrp->ia_flags & IFF_IPV6) && v6))
				continue;

			bzero(&lifrl, sizeof (lifrl));
			(void) strlcpy(lifrl.lifr_name, ifaddrp->ia_name,
			    sizeof (lifrl.lifr_name));
			if (ioctl(sock, SIOCGLIFFLAGS, &lifrl) < 0) {
				ret = ipadm_errno2status(errno);
				ifaddrlistx_free(ifaddrs);
				goto done;
			}
			if (lifrl.lifr_flags & IFF_UP) {
				ret = i_ipadm_set_flags(iph, lifrl.lifr_name,
				    ((lifrl.lifr_flags & IFF_IPV4) ? AF_INET :
				    AF_INET6), 0, IFF_UP);
				if (ret != IPADM_SUCCESS) {
					ifaddrlistx_free(ifaddrs);
					goto done;
				}
			} else if (lifrl.lifr_flags & IFF_DUPLICATE) {
				if (ioctl(sock, SIOCGLIFADDR, &lifrl) < 0 ||
				    ioctl(sock, SIOCSLIFADDR, &lifrl) < 0) {
					ret = ipadm_errno2status(errno);
					ifaddrlistx_free(ifaddrs);
					goto done;
				}
			}
		}
		ifaddrlistx_free(ifaddrs);
	}

	if (ioctl(muxid_fd, SIOCGLIFMUXID, (caddr_t)&lifr) < 0) {
		ret = ipadm_errno2status(errno);
		goto done;
	}
	arp_muxid = lifr.lifr_arp_muxid;
	ip_muxid = lifr.lifr_ip_muxid;

	/*
	 * We don't have a good way of knowing whether the arp stream is
	 * plumbed. We can't rely on IFF_NOARP because someone could
	 * have turned it off later using "ifconfig xxx -arp".
	 */
	if (arp_muxid != 0) {
		if (ioctl(mux_fd, I_PUNLINK, arp_muxid) < 0) {
			/*
			 * See the comment before the SIOCGLIFGROUPNAME call.
			 */
			if (errno == EBUSY && (flags & IFF_IPMP))
				goto again;

			if ((errno == EINVAL) &&
			    (flags & (IFF_NOARP | IFF_IPV6))) {
				/*
				 * Some plumbing utilities set the muxid to
				 * -1 or some invalid value to signify that
				 * there is no arp stream. Set the muxid to 0
				 * before trying to unplumb the IP stream.
				 * IP does not allow the IP stream to be
				 * unplumbed if it sees a non-null arp muxid,
				 * for consistency of IP-ARP streams.
				 */
				lifr.lifr_arp_muxid = 0;
				(void) ioctl(muxid_fd, SIOCSLIFMUXID,
				    (caddr_t)&lifr);
				changed_arp_muxid = B_TRUE;
			}
			/*
			 * In case of any other error, we continue with
			 * the unplumb.
			 */
		}
	}

	if (ioctl(mux_fd, I_PUNLINK, ip_muxid) < 0) {
		if (changed_arp_muxid) {
			/*
			 * Some error occurred, and we need to restore
			 * everything back to what it was.
			 */
			save_errno = errno;
			lifr.lifr_arp_muxid = arp_muxid;
			lifr.lifr_ip_muxid = ip_muxid;
			(void) ioctl(muxid_fd, SIOCSLIFMUXID, (caddr_t)&lifr);
			errno = save_errno;
		}
		/*
		 * See the comment before the SIOCGLIFGROUPNAME call.
		 */
		if (errno == EBUSY && (flags & IFF_IPMP))
			goto again;

		ret = ipadm_errno2status(errno);
	}
done:
	if (muxid_fd != -1)
		(void) close(muxid_fd);
	if (mux_fd != -1)
		(void) close(mux_fd);

	if (af == AF_INET6 && ret == IPADM_SUCCESS) {
		/*
		 * in.ndpd maintains the phyints in its memory even after
		 * the interface is plumbed, so that it can be reused when
		 * the interface gets plumbed again. The default behavior
		 * of in.ndpd is to start autoconfiguration for an interface
		 * that gets plumbed. We need to send the
		 * message IPADM_ENABLE_AUTOCONF to in.ndpd to restore this
		 * default behavior on replumb.
		 */
		(void) i_ipadm_enable_autoconf(ifname);
	}
	return (ret);
}

/*
 * Unplumbs the logical interface or the loopback interface in `ifname' of
 * address family `af', and deletes the address object from ipmgmtd if one
 * exists.
 */
ipadm_status_t
i_ipadm_unplumb_lif(ipadm_handle_t iph, const char *ifname, sa_family_t af,
    uint32_t ipadm_flags)
{
	ipadm_status_t	status;
	struct lifreq	lifr;
	struct ipadm_addrobj_s	ipaddr;

	assert(af != AF_UNSPEC);
	bzero(&lifr, sizeof (lifr));
	(void) strlcpy(lifr.lifr_name, ifname, sizeof (lifr.lifr_name));
	if (ioctl(IPADM_SOCK(iph, af), SIOCLIFREMOVEIF, (caddr_t)&lifr) < 0)
		return (ipadm_errno2status(errno));
	status = i_ipadm_get_lif2addrobj(iph, ifname, af, &ipaddr);
	if (status == IPADM_SUCCESS) {
		/*
		 * If an addrobj for this logical interface is found,
		 * remove it from ipmgmtd's memory.
		 */
		return (i_ipadm_delete_addrobj(iph, &ipaddr, ipadm_flags));
	} else if (status != IPADM_OBJ_NOTFOUND) {
		return (status);
	}
	return (IPADM_SUCCESS);
}

/*
 * Saves the given interface name `ifname' with address family `af' in
 * persistent DB.
 */
static ipadm_status_t
i_ipadm_persist_if(ipadm_handle_t iph, const char *ifname, sa_family_t af,
    ipadm_if_class_t class)
{
	ipmgmt_setif_arg_t	ifarg;
	int			err;

	(void) strlcpy(ifarg.ia_ifname, ifname, sizeof (ifarg.ia_ifname));
	ifarg.ia_family = af;
	ifarg.ia_class = class;
	ifarg.ia_cmd = IPMGMT_CMD_SETIF;
	ifarg.ia_flags = IPMGMT_PERSIST;
	err = ipadm_door_call(iph, &ifarg, sizeof (ifarg), NULL, 0);
	return (ipadm_errno2status(err));
}

/*
 * Resets all addresses on interface `ifname' with address family `af'
 * from ipmgmtd daemon. If is_persistent = B_TRUE, all interface properties
 * and address objects of `ifname' for `af' are also removed from the
 * persistent DB.
 */
ipadm_status_t
i_ipadm_delete_ifobj(ipadm_handle_t iph, const char *ifname, sa_family_t af,
    boolean_t is_persistent)
{
	ipmgmt_resetif_arg_t	ifarg;
	int			err;

	ifarg.ia_cmd = IPMGMT_CMD_RESETIF;
	ifarg.ia_flags = IPMGMT_ACTIVE;
	if (is_persistent)
		ifarg.ia_flags |= IPMGMT_PERSIST;
	ifarg.ia_family = af;
	(void) strlcpy(ifarg.ia_ifname, ifname, LIFNAMSIZ);

	err = ipadm_door_call(iph, &ifarg, sizeof (ifarg), NULL, 0);
	return (ipadm_errno2status(err));
}

/*
 * Creates the loopback interface in `ifname' for the given address family
 * `af' which is either AF_INET or AF_INET6. The support to create the
 * loopback interface by a non-legacy application is not provided, because
 * it is expected the loopback will always be available on boot and the
 * application will never have the need to do create it. The application is
 * also prevented from deleting the loopback as this will disable many of
 * the system services.
 *
 * The support to plumb loopback by ifconfig(1M) is retained for backward
 * compatibility, until the kernel is modified to plumb loopback automatically
 * on boot.  It is expected that the ability to plumb/unplumb the loopback by
 * any application will be removed by future work.
 */
ipadm_status_t
ipadm_create_loopback(ipadm_handle_t iph, const char *ifname, sa_family_t af,
    uint32_t flags)
{
	if (!ipadm_is_loopback(ifname) || (flags & IPADM_OPT_GENPPA) != 0)
		return (IPADM_INVALID_ARG);

	if (!i_ipadm_is_legacy(iph))
		return (IPADM_OP_NOTSUP);

	return (i_ipadm_create_if(iph, (char *)ifname, af,
	    IPADMIF_CLASS_LOOPBACK, flags));
}

/*
 * Function that adds a logical interface to `ifname'. On return,
 * the address on this logical interface is 0.0.0.0 for IPv4 or :: for IPv6
 * and is marked down.
 */
ipadm_status_t
i_ipadm_plumb_lif(ipadm_handle_t iph, const char *ifname, sa_family_t af,
    ipadm_if_class_t class)
{
	struct lifreq	lifr;
	int		sock;

	assert(af != AF_UNSPEC);
	bzero(&lifr, sizeof (lifr));
	(void) strlcpy(lifr.lifr_name, ifname, sizeof (lifr.lifr_name));
	sock = IPADM_SOCK(iph, af);
	if (ioctl(sock, SIOCGLIFADDR, (caddr_t)&lifr) >= 0)
		return (IPADM_IF_EXISTS);
	bzero(&lifr.lifr_addr, sizeof (lifr.lifr_addr));
	/*
	 * The address family is identified through the socket type for
	 * SIOCLIFADDIF. So, the af does not need to be set in lifr.lifr_addr.
	 */
	if (ioctl(sock, SIOCLIFADDIF, (caddr_t)&lifr) < 0)
		return (ipadm_errno2status(errno));
	/*
	 * By default, kernel configures 127.0.0.1 for IPv4 or ::1 for IPv6
	 * on the loopback interface. Replace this with 0.0.0.0 for IPv4 or
	 * :: for IPv6 to be consistent with interface creation on other
	 * interfaces, only for the non-legacy case.
	 */
	if (class == IPADMIF_CLASS_LOOPBACK && !i_ipadm_is_legacy(iph)) {
		bzero(&lifr.lifr_addr, sizeof (lifr.lifr_addr));
		lifr.lifr_addr.ss_family = af;
		if (ioctl(sock, SIOCSLIFADDR, (caddr_t)&lifr) < 0)
			return (ipadm_errno2status(errno));
	}
	return (IPADM_SUCCESS);
}

/*
 * Creates the VNI interface in `ifname' for the given address family `af'.
 * It creates both IPv4 and IPv6 interfaces when af is set to AF_UNSPEC.
 */
ipadm_status_t
ipadm_create_vni(ipadm_handle_t iph, const char *ifname, sa_family_t af,
    uint32_t flags)
{
	if (!ipadm_is_vni(ifname) || (flags & IPADM_OPT_GENPPA) != 0)
		return (IPADM_INVALID_ARG);
	return (i_ipadm_create_if(iph, (char *)ifname, af, IPADMIF_CLASS_VNI,
	    flags));
}

/*
 * Creates the IP interface in `ifname' for the given address family `af'.
 * It creates both IPv4 and IPv6 interfaces when af is set to AF_UNSPEC.
 */
ipadm_status_t
ipadm_create_ip(ipadm_handle_t iph, const char *ifname, sa_family_t af,
    uint32_t flags)
{
	datalink_id_t	linkid = DATALINK_INVALID_LINKID;
	uint32_t	dlflags;
	dladm_status_t	dlstatus;
	zoneid_t	zoneid;

	if (!ipadm_is_ip(iph, ifname) || (flags & IPADM_OPT_GENPPA) != 0)
		return (IPADM_INVALID_ARG);

	if (iph->ih_dh != NULL) {
		dlstatus = dladm_name2info(iph->ih_dh, ifname, &linkid,
		    &dlflags, NULL, NULL);
		/*
		 * If we're in the global zone and we're plumbing a datalink,
		 * make sure that the datalink is not assigned to a non-global
		 * zone. Note that the non-global zones don't need this check,
		 * because zoneadm has taken care of this when the zones boot.
		 */
		if (iph->ih_zoneid == GLOBAL_ZONEID &&
		    dlstatus == DLADM_STATUS_OK) {
			zoneid = ALL_ZONES;
			if (zone_check_datalink(&zoneid, linkid) == 0) {
				/* interface is in use by a non-global zone. */
				return (IPADM_IF_INUSE);
			}
		}
		/*
		 * Verify that the user is not creating a persistent
		 * IP interface on a non-persistent data-link.
		 * Exception: Creating IP interfaces over Auto VNICs (anet)
		 * is allowed even though they are non-persistent datalinks
		 * to support IP interface creation inside NGZs over anets.
		 */
		if (dlstatus == DLADM_STATUS_OK &&
		    (flags & IPADM_OPT_PERSIST) &&
		    !(dlflags & DLADM_OPT_PERSIST) &&
		    !i_ipadm_is_anet(iph, linkid)) {
			return (IPADM_TEMPORARY_OBJ);
		}
	}
	return (i_ipadm_create_if(iph, (char *)ifname, af, IPADMIF_CLASS_IP,
	    flags));
}

/*
 * Creates the IPMP interface in `ifname' for the given address family
 * `af'. It creates both IPv4 and IPv6 interfaces when af is set to AF_UNSPEC.
 * `ifname' must point to memory that can hold upto LIFNAMSIZ chars. It may
 * be over-written with the actual interface name when a PPA has to be
 * internally generated by the library.
 */
ipadm_status_t
ipadm_create_ipmp(ipadm_handle_t iph, char *ifname, sa_family_t af,
    uint32_t flags)
{
	return (i_ipadm_create_if(iph, ifname, af, IPADMIF_CLASS_IPMP, flags));
}

/*
 * Creates an interface. Creates both IPv4 and IPv6 interfaces by
 * default, unless a value in `af' is specified. The interface may be plumbed
 * only if there is no previously saved persistent configuration information
 * for the interface (in which case the ipadm_enable_if() function must
 * be used to enable the interface).
 *
 * Returns: IPADM_SUCCESS, IPADM_FAILURE, IPADM_IF_EXISTS, IPADM_DLPI_FAILURE,
 * IPADM_OP_DISABLE_OBJ, IPADM_TEMPORARY_OBJ,
 * or appropriate ipadm_status_t corresponding to the errno.
 *
 * `ifname' must point to memory that can hold upto LIFNAMSIZ chars. It may
 * be over-written with the actual interface name when a PPA has to be
 * internally generated by the library.
 */
ipadm_status_t
i_ipadm_create_if(ipadm_handle_t iph, char *ifname, sa_family_t af,
    ipadm_if_class_t class, uint32_t flags)
{
	ipadm_status_t	status;
	boolean_t	created_v4 = B_FALSE;
	boolean_t	created_v6 = B_FALSE;
	uint32_t	v4flags, v6flags;
	uint_t		pflags;

	status = i_ipadm_validate_ifcreate(iph, ifname, af, flags);
	if (status != IPADM_SUCCESS)
		return (status);

	if (i_ipadm_get_lnum(ifname) != 0)
		return (i_ipadm_plumb_lif(iph, ifname, af, class));
	if (i_ipadm_is_legacy(iph))
		return (i_ipadm_plumb_if(iph, ifname, af, class, flags));

	pflags = i_ipadm_get_pflags(iph, ifname);
	/*
	 * It is a disabled interface object if one of the address families
	 * is already configured persistently but none of them exist in the
	 * active configuration, in which case an error should be returned.
	 */
	if ((flags & IPADM_OPT_NWAM_OVERRIDE) == 0 &&
	    (pflags & (IPADM_IFF_IPV4|IPADM_IFF_IPV6)) != 0 &&
	    !ipadm_if_enabled(iph, ifname, AF_UNSPEC))
		return (IPADM_OP_DISABLE_OBJ);

	if ((af == AF_INET || af == AF_UNSPEC) &&
	    !i_ipadm_is_6to4(iph, ifname)) {
		/*
		 * Unset IPADM_OPT_PERSIST to avoid getting persisted again.
		 */
		v4flags = flags;
		if ((pflags & IPADM_IFF_IPV4) != 0)
			v4flags = (flags & ~IPADM_OPT_PERSIST);
		status = i_ipadm_plumb_if(iph, ifname, AF_INET, class, v4flags);
		if (status != IPADM_SUCCESS)
			return (status);
		created_v4 = B_TRUE;
		/*
		 * If a ppa was generated, we should reset it in the input
		 * flags, so that a ppa will not be generated again while
		 * plumbing IPv6.
		 */
		flags &= ~IPADM_OPT_GENPPA;
		/*
		 * If this is an underlying interface, this has to be
		 * brought up to make it usable in the group. If we did
		 * not do it, the admin will be forced to create a test
		 * address on it just to bring the interface up.
		 */
		if (i_ipadm_is_under_ipmp(iph, ifname)) {
			status = i_ipadm_set_flags(iph, ifname, AF_INET,
			    IFF_UP, 0);
			if (status != IPADM_SUCCESS)
				goto fail;
		}
	}
	if (af == AF_INET6 || af == AF_UNSPEC) {
		/*
		 * Unset IPADM_OPT_PERSIST to avoid getting persisted again.
		 */
		v6flags = flags;
		if ((pflags & IPADM_IFF_IPV6) != 0)
			v6flags = (flags & ~IPADM_OPT_PERSIST);
		status = i_ipadm_plumb_if(iph, ifname, AF_INET6, class,
		    v6flags);
		if (status != IPADM_SUCCESS)
			goto fail;
		created_v6 = B_TRUE;
		/*
		 * If this is an underlying interface, a link-local address
		 * has to be created on it to make it usable in the group. If
		 * we did not do it, the admin will be forced to create a test
		 * address on it just to bring the interface up.
		 */
		if (i_ipadm_is_under_ipmp(iph, ifname)) {
			status = i_ipadm_create_ipv6_on_underif(iph, ifname);
			if (status != IPADM_SUCCESS)
				goto fail;
		}
	}
	return (IPADM_SUCCESS);
fail:
	if (created_v4) {
		(void) i_ipadm_delete_if(iph, ifname, AF_INET, class,
		    (v4flags & IPADM_COMMON_OPT_MASK));
	}
	if (created_v6) {
		(void) i_ipadm_delete_if(iph, ifname, AF_INET6, class,
		    (v6flags & IPADM_COMMON_OPT_MASK));
	}
	return (status);
}

/*
 * Creates the IPMP interface `ifname' for address family `af', which is
 * either AF_INET or AF_INET6.
 *
 * When an underlying interface in an IPMP group G is plumbed for address
 * family `af' it is possible that the IPMP interface for `af' is not
 * yet plumbed. For this case, i_ipadm_create_ipmp() is called from
 * i_ipadm_create_ipmp_peer() with the argument `implicit' set to B_TRUE.
 * If `af' is IPv6, we need to bring up the link-local address for this
 * implicit case.
 */
static ipadm_status_t
i_ipadm_create_ipmp(ipadm_handle_t iph, char *ifname, sa_family_t af,
    boolean_t implicit, uint32_t ipadm_flags)
{
	ipadm_status_t	status;

	assert(af != AF_UNSPEC);
	if (implicit) {
		if (i_ipadm_if_pexists(iph, ifname, AF_UNSPEC))
			ipadm_flags |= IPADM_OPT_PERSIST;
	}
	status = i_ipadm_create_if(iph, ifname, af, IPADMIF_CLASS_IPMP,
	    ipadm_flags);
	if (status == IPADM_SUCCESS && implicit && i_ipadm_is_legacy(iph) &&
	    af == AF_INET6) {
		/*
		 * To preserve backward-compatibility in the legacy case,
		 * always bring up the link-local address for
		 * implicitly-created IPv6 IPMP interfaces.
		 */
		(void) i_ipadm_set_flags(iph, ifname, AF_INET6, IFF_UP, 0);
	}

	return (IPADM_SUCCESS);
}

/*
 * Deletes the loopback interface in `ifname' for the given address family
 * `af' which is either AF_INET or AF_INET6. The support to remove the
 * loopback interface by a non-legacy application is not provided, because
 * removing the loopback will make many of the system services that
 * communicate using the loopback sockets such as dhcpagent(1M) non-functional.
 * It is expected that the ability to plumb/unplumb the loopback will be
 * removed as part of future work.
 */
ipadm_status_t
ipadm_delete_loopback(ipadm_handle_t iph, const char *ifname, sa_family_t af,
    uint32_t flags)
{
	if (!ipadm_is_loopback(ifname) || af == AF_UNSPEC)
		return (IPADM_INVALID_ARG);
	if (!i_ipadm_is_legacy(iph))
		return (IPADM_OP_NOTSUP);
	return (i_ipadm_delete_if(iph, ifname, af, IPADMIF_CLASS_LOOPBACK,
	    flags));
}

/*
 * Deletes the interface of class IPADMIF_CLASS_VNI in `ifname'.
 * Removes both IPv4 and IPv6 interfaces when `af' = AF_UNSPEC.
 */
ipadm_status_t
ipadm_delete_vni(ipadm_handle_t iph, const char *ifname, sa_family_t af,
    uint32_t flags)
{
	if (!ipadm_is_vni(ifname))
		return (IPADM_INVALID_ARG);
	return (i_ipadm_delete_if(iph, ifname, af, IPADMIF_CLASS_VNI, flags));
}

/*
 * Deletes the interface of class IPADMIF_CLASS_IP in `ifname'.
 * Removes both IPv4 and IPv6 interfaces when `af' = AF_UNSPEC.
 */
ipadm_status_t
ipadm_delete_ip(ipadm_handle_t iph, const char *ifname, sa_family_t af,
    uint32_t flags)
{
	if (!ipadm_is_ip(iph, ifname) || i_ipadm_ipmp_pexists(iph, ifname))
		return (IPADM_INVALID_ARG);
	return (i_ipadm_delete_if(iph, ifname, af, IPADMIF_CLASS_IP, flags));
}

/*
 * Deletes the interface of class IPADMIF_CLASS_IPMP in `ifname'.
 * Removes both IPv4 and IPv6 interfaces when `af' = AF_UNSPEC.
 */
ipadm_status_t
ipadm_delete_ipmp(ipadm_handle_t iph, const char *ifname, sa_family_t af,
    uint32_t flags)
{
	if (!ipadm_is_ipmp(iph, ifname) && !i_ipadm_ipmp_pexists(iph, ifname))
		return (IPADM_NOSUCH_IPMPIF);
	return (i_ipadm_delete_if(iph, ifname, af, IPADMIF_CLASS_IPMP, flags));
}

/*
 * Finds the error status to return, given the error status values from
 * unplumbing IPv4 and IPv6 interfaces. The error status is obtained based
 * on the following rules:
 *
 * v4_status,		if v4_status == v6_status
 *
 * IPADM_SUCCESS,	if either of v4_status or v6_status is SUCCESS
 * 			and the other status is NOSUCH_IF
 *
 * IPADM_NOSUCH_IF,	if both v4_status and v6_status are NOSUCH_IF
 *
 * IPADM_GRP_NOTEMPTY,	if either of v4_status or v6_status is SUCCESS
 * 			and the other status is GRP_NOTEMPTY
 *			OR
 *			if either of v4_status or v6_status is NOSUCH_IF
 *			and the other status is GRP_NOTEMPTY
 *
 * IPADM_FAILURE	otherwise.
 */
static ipadm_status_t
i_ipadm_delete_if_status(ipadm_status_t v4_status, ipadm_status_t v6_status)
{
	ipadm_status_t	other;

	if (v4_status == v6_status) {
		/*
		 * covers the case when both v4_status and v6_status are
		 * IPADM_NOSUCH_IF.
		 */
		return (v4_status);
	} else if (v4_status == IPADM_SUCCESS || v6_status == IPADM_SUCCESS) {
		if (v4_status == IPADM_SUCCESS)
			other = v6_status;
		else
			other = v4_status;
		switch (other) {
		case IPADM_NOSUCH_IF:
			return (IPADM_SUCCESS);
		case IPADM_GRP_NOTEMPTY:
			return (IPADM_GRP_NOTEMPTY);
		default:
			return (IPADM_FAILURE);
		}
	} else if ((v4_status == IPADM_NOSUCH_IF &&
	    v6_status == IPADM_GRP_NOTEMPTY) || (v6_status == IPADM_NOSUCH_IF &&
	    v4_status == IPADM_GRP_NOTEMPTY)) {
		return (IPADM_GRP_NOTEMPTY);
	}
	return (IPADM_FAILURE);
}

/*
 * Deletes the IP interface in `ifname'. Removes both IPv4 and IPv6 interfaces
 * when `af' = AF_UNSPEC.
 */
ipadm_status_t
i_ipadm_delete_if(ipadm_handle_t iph, const char *ifname, sa_family_t af,
    ipadm_if_class_t class, uint32_t flags)
{
	ipadm_status_t	status1 = IPADM_SUCCESS;
	ipadm_status_t	status2 = IPADM_SUCCESS;
	ipadm_status_t	ret;
	boolean_t	is_persistent = ((flags & IPADM_OPT_PERSIST) != 0);

	/* Check for the required authorization */
	if (!ipadm_check_auth())
		return (IPADM_INSUFF_AUTH);

	if (!(flags & IPADM_OPT_ACTIVE) || (flags & ~IPADM_COMMON_OPT_MASK))
		return (IPADM_INVALID_ARG);

	if (!i_ipadm_validate_ifname(iph, ifname))
		return (IPADM_INVALID_IFNAME);

	if (class == IPADMIF_CLASS_IPMP && (flags & IPADM_OPT_FORCE)) {
		ipadm_ifname_t	*under;
		ipadm_if_info_t	*ifp;
		list_t		ifinfo;

		status1 = i_ipadm_active_if_info(iph, ifname, &ifinfo, 0);
		if (status1 == IPADM_SUCCESS) {
			ifp = list_head(&ifinfo);
			under = list_head(&ifp->ifi_unders);
			for (; under != NULL;
			    under = list_next(&ifp->ifi_unders, under)) {
				status1 = ipadm_remove_ipmp(iph, ifname,
				    under->ifn_name,
				    (flags & IPADM_COMMON_OPT_MASK));
				if (status1 != IPADM_SUCCESS) {
					ipadm_free_if_info(&ifinfo);
					return (status1);
				}
			}
			ipadm_free_if_info(&ifinfo);
		}
	}

	/*
	 * Once we unplumb, we have to make sure that, even if interface does
	 * not exist in active configuration, all its addresses and properties
	 * are removed from the persistent configuration. If interface does not
	 * exist both in active and persistent config, IPADM_NOSUCH_IF
	 * should be returned.
	 */
	if (af == AF_INET || af == AF_UNSPEC) {
		status1 = i_ipadm_unplumb_if(iph, ifname, AF_INET, flags);
		if ((status1 == IPADM_NOSUCH_IF && is_persistent) ||
		    status1 == IPADM_SUCCESS) {
			ret = i_ipadm_delete_ifobj(iph, ifname, AF_INET,
			    is_persistent);
			if (ret == IPADM_SUCCESS)
				status1 = IPADM_SUCCESS;
		}
	}
	if (af == AF_INET6 || af == AF_UNSPEC) {
		status2 = i_ipadm_unplumb_if(iph, ifname, AF_INET6, flags);
		if ((status2 == IPADM_NOSUCH_IF && is_persistent) ||
		    status2 == IPADM_SUCCESS) {
			ret = i_ipadm_delete_ifobj(iph, ifname, AF_INET6,
			    is_persistent);
			if (ret == IPADM_SUCCESS)
				status2 = IPADM_SUCCESS;
		}
	}
	/*
	 * If the family has been uniquely identified, we return the
	 * associated status, even if that is ENXIO. Calls from ifconfig
	 * which can only unplumb one of IPv4/IPv6 at any time fall under
	 * this category.
	 */
	if (af == AF_INET)
		return (status1);
	else if (af == AF_INET6)
		return (status2);
	else if (af != AF_UNSPEC)
		return (IPADM_INVALID_ARG);

	return (i_ipadm_delete_if_status(status1, status2));
}

/*
 * Adds an underlying interface `underif' to the IPMP interface `ipmpif'.
 */
ipadm_status_t
ipadm_add_ipmp(ipadm_handle_t iph, const char *ipmpif, const char *underif,
    uint32_t flags)
{
	return (i_ipadm_update_ipmp(iph, ipmpif, underif, IPADM_IPMPOP_ADD,
	    flags));
}

/*
 * Removes an underlying interface `underif' from the IPMP interface
 * `ipmpif'.
 */
ipadm_status_t
ipadm_remove_ipmp(ipadm_handle_t iph, const char *ipmpif, const char *underif,
    uint32_t flags)
{
	return (i_ipadm_update_ipmp(iph, ipmpif, underif, IPADM_IPMPOP_REMOVE,
	    flags));
}

/*
 * Adds/Removes an underlying interface `underif' to/from the IPMP
 * interface `ipmpif'.
 */
ipadm_status_t
i_ipadm_update_ipmp(ipadm_handle_t iph, const char *ipmpif,
    const char *underif, ipadm_ipmpop_t op, uint32_t flags)
{
	ipadm_status_t	status;
	boolean_t	ipmp_pexist = B_FALSE, under_pexist;
	char		grname[LIFGRNAMSIZ];
	boolean_t	legacy = i_ipadm_is_legacy(iph);

	/* Check for the required authorization */
	if (!ipadm_check_auth())
		return (IPADM_INSUFF_AUTH);

	if (!(flags & IPADM_OPT_ACTIVE) || (flags & ~IPADM_COMMON_OPT_MASK) ||
	    underif == NULL || ipmpif == NULL)
		return (IPADM_INVALID_ARG);

	/*
	 * A few cases below require us to know whether the IPMP and
	 * underlying interface are persistently configured; get that info.
	 */
	if (!legacy || (flags & IPADM_OPT_PERSIST) != 0) {
		ipmp_pexist = i_ipadm_if_pexists(iph, ipmpif, AF_UNSPEC);
		under_pexist = i_ipadm_if_pexists(iph, underif, AF_UNSPEC);
	}

	/*
	 * Validate IPMP interface.
	 */
	if (!ipadm_if_enabled(iph, ipmpif, AF_UNSPEC)) {
		if (!legacy && ipmp_pexist)
			return (IPADM_OP_DISABLE_OBJ);
		return (IPADM_NOSUCH_IPMPIF);
	}
	if (!ipadm_is_ipmp(iph, ipmpif))
		return (IPADM_NOT_IPMPIF);

	/*
	 * Validate underlying interface.
	 */
	if (!ipadm_if_enabled(iph, underif, AF_UNSPEC)) {
		if (!legacy && under_pexist)
			return (IPADM_OP_DISABLE_OBJ);
		return (IPADM_NOSUCH_UNDERIF);
	}

	/*
	 * Get the IPMP interface groupname and perform the operation.
	 */
	status = i_ipadm_get_groupname(iph, ipmpif, grname, LIFGRNAMSIZ);
	if (status != IPADM_SUCCESS)
		return (status);

	if (op == IPADM_IPMPOP_ADD) {
		if ((flags & IPADM_OPT_PERSIST) && !ipmp_pexist)
			return (IPADM_TEMPORARY_OBJ);
		status = i_ipadm_add_ipmp(iph, grname, ipmpif, underif, flags);
		if (status != IPADM_SUCCESS)
			goto fail;
		if (!i_ipadm_is_legacy(iph)) {
			/*
			 * Bring up the underlying interface to make it
			 * usable in the group.
			 */
			status = i_ipadm_add_ipmp_bringup_underif(iph, underif,
			    AF_UNSPEC);
			if (status != IPADM_SUCCESS)
				goto fail;
		}
		return (status);
	}
	assert(op == IPADM_IPMPOP_REMOVE);
	return (i_ipadm_remove_ipmp(iph, grname, ipmpif, underif, flags));
fail:
	(void) i_ipadm_remove_ipmp(iph, grname, ipmpif, underif, flags);
	return (status);
}

/*
 * Adds the given `underif' to the IPMP interface `ipmpif'.
 *
 * The following error codes are returned:
 *
 *  IPADM_SUCCESS, when `underif' is successfully added to `ipmpif',
 *  IPADM_NOSUCH_IPMPIF, when `ipmpif' does not exist.
 *	- ipadm_create_ipmp() should be used to create the IPMP interface
 *	      before retrying ipadm_add_ipmp().
 *  IPADM_NOSUCH_IF, when `underif' does not exist.
 *	- ipadm_create_ip() should be used to create the underlying interface
 *	      before retrying ipadm_add_ipmp().
 *  IPADM_IPMPIF_MISSING_AF, when `ipmpif' exists but is not plumbed for the
 *	address family that `underif' is configured for.
 *	- ipadm_create_ipmp_implicit() should be called to create the missing
 *	      af of the IPMP interface before retrying ipadm_add_ipmp().
 *  IPADM_ALREADY_IN_GRP, when `underif' is already in a different IPMP group.
 *	- ipadm_remove_ipmp() should be called to remove `underif' from its
 *	      current group, before retrying ipadm_add_ipmp().
 *  IPADM_UNDERIF_UP_ADDRS, when `underif' has addresses that are marked up.
 *	- ipadm_down_addrs() can be used to down the IFF_UP addresses
 *	      to retry ipadm_add_ipmp().
 *  IPADM_UNDERIF_APP_ADDRS, when `underif' has addresses managed by
 *	applications such as dhcpagent(1M) and in.ndpd(1M).
 *	- ipadm_wait_app_addrs() can be used to wait for the application
 *	      managed addresses to be taken down before retrying
 *	      ipadm_add_ipmp().
 *  IPADM_FAILURE, otherwise.
 *
 * Upon return, if there were any addresses brought down by the caller
 * in response to IPADM_UNDERIF_UP_ADDRS, those addresses should be brought
 * up. The utility function ipadm_up_addrs() can be used to do this.
 *
 * If the caller is a non-legacy application (e.g. ipadm(1M)), then
 * the existing addresses on the underlying interface should be converted
 * into test addresses. ipadm_mark_testaddrs() can be used to do this.
 */
ipadm_status_t
i_ipadm_add_ipmp(ipadm_handle_t iph, const char *grname, const char *ipmpif,
    const char *underif, uint32_t ipadm_flags)
{
	ipadm_status_t	status = IPADM_SUCCESS;
	struct lifreq	lifr;
	int		sock;

	bzero(&lifr, sizeof (lifr));
	(void) strlcpy(lifr.lifr_name, underif, sizeof (lifr.lifr_name));
	(void) strlcpy(lifr.lifr_groupname, grname,
	    sizeof (lifr.lifr_groupname));

	sock = IPADM_SOCK(iph, i_ipadm_get_active_af(iph, underif));
	if (ioctl(sock, SIOCSLIFGROUPNAME, (caddr_t)&lifr) < 0) {
		switch (errno) {
		case ENOENT:
			return (IPADM_NOSUCH_IPMPIF);
		case EALREADY:
			return (IPADM_ALREADY_IN_GRP);
		case EAFNOSUPPORT:
			return (IPADM_IPMPIF_MISSING_AF);
		case EADDRINUSE:
			return (IPADM_UNDERIF_UP_ADDRS);
		case EADDRNOTAVAIL:
			return (IPADM_UNDERIF_APP_ADDRS);
		default:
			return (ipadm_errno2status(errno));
		}
	}

	if (ipadm_flags & IPADM_OPT_PERSIST) {
		status = i_ipadm_persist_update_ipmp(iph, ipmpif, underif,
		    IPADM_IPMPOP_ADD);
		if (status != IPADM_SUCCESS)
			(void) i_ipadm_remove_ipmp(iph, grname, ipmpif,
			    underif, ipadm_flags);
	}
	return (status);
}

/*
 * Removes the underlying interface `ifname' from the IPMP interface with
 * groupname `grname'.
 *  Returns the following error codes:
 * 	IPADM_SUCCESS, upon success
 * 	IPADM_NOT_IN_GROUP, if `underif' is not in group `grname'
 *	errors while retrieving the group name from kernel,
 *	errors while persisting the removal.
 *
 * If the caller is a non-legacy application (e.g. ipadm(1M)), then
 * the test addresses on the underlying interface should be converted
 * into non-test addresses. ipadm_clear_testaddrs() can be used to do this.
 */
ipadm_status_t
i_ipadm_remove_ipmp(ipadm_handle_t iph, const char *grname, const char *ipmpif,
    const char *underif, uint32_t ipadm_flags)
{
	ipadm_status_t	status;
	struct lifreq	lifr;
	int		sock;
	char		c_grname[LIFGRNAMSIZ];
	uint_t		bufsize = sizeof (c_grname);

	/*
	 * Check if the given interface is indeed an underlying
	 * interface in the given IPMP interface.
	 */
	status = i_ipadm_get_groupname(iph, underif, c_grname, bufsize);
	if (status != IPADM_SUCCESS)
		return (status);
	if (strcmp(grname, c_grname) != 0)
		return (IPADM_NOT_IN_GROUP);
	bzero(&lifr, sizeof (lifr));
	(void) strlcpy(lifr.lifr_name, underif, sizeof (lifr.lifr_name));
	sock = IPADM_SOCK(iph, i_ipadm_get_active_af(iph, underif));
	lifr.lifr_groupname[0] = '\0';
	if (ioctl(sock, SIOCSLIFGROUPNAME, &lifr) < 0)
		return (ipadm_errno2status(errno));
	if (ipadm_flags & IPADM_OPT_PERSIST) {
		status = i_ipadm_persist_update_ipmp(iph, ipmpif, underif,
		    IPADM_IPMPOP_REMOVE);
		if (status == IPADM_OBJ_NOTFOUND)
			status = IPADM_SUCCESS;
	}
	return (status);
}

/*
 * Checks if an address exists on the 0th logical interface of `ifname' for
 * address family `af'.
 */
static boolean_t
i_ipadm_ifaddr_exists(ipadm_handle_t iph, const char *ifname, sa_family_t af)
{
	struct lifreq	lifr;

	bzero(&lifr, sizeof (lifr));
	(void) strlcpy(lifr.lifr_name, ifname, sizeof (lifr.lifr_name));
	if (ioctl(IPADM_SOCK(iph, af), SIOCGLIFADDR, &lifr) < 0)
		return (B_FALSE);
	return (!sockaddrunspec((struct sockaddr *)&lifr.lifr_addr));
}

/*
 * After adding an interface to an IPMP interface, this function is used
 * to bring up the underlying interface. For IPv6, we may also need to
 * configure a link-local on the underlying interface. When `af' is set to
 * AF_UNSPEC, the function brings up the address families that are
 * plumbed and does not return error if one of them is not plumbed.
 */
ipadm_status_t
i_ipadm_add_ipmp_bringup_underif(ipadm_handle_t iph, const char *underif,
    sa_family_t af)
{
	ipadm_status_t	status;
	boolean_t	v4_up = B_FALSE;

	if (af == AF_INET || af == AF_UNSPEC) {
		if (!i_ipadm_ifaddr_exists(iph, underif, AF_INET)) {
			status = i_ipadm_set_flags(iph, underif, AF_INET,
			    IFF_UP, 0);
			/*
			 * When `af' = AF_UNSPEC, it is expected that the
			 * IPv4 interface may not exist. We will ignore the
			 * resulting IPADM_NOSUCH_IF.
			 */
			if (status != IPADM_SUCCESS &&
			    (status != IPADM_NOSUCH_IF || af == AF_INET))
				return (status);
		}
		v4_up = B_TRUE;
	}
	if (af == AF_INET6 || af == AF_UNSPEC) {
		if (!i_ipadm_ifaddr_exists(iph, underif, AF_INET6)) {
			status = i_ipadm_create_ipv6_on_underif(iph, underif);
			/*
			 * When `af' = AF_UNSPEC, it is expected that the
			 * IPv6 interface may not exist. We will ignore the
			 * resulting IPADM_NOSUCH_IF.
			 */
			if (status != IPADM_SUCCESS &&
			    (status != IPADM_NOSUCH_IF || af == AF_INET6))
				return (status);
			/*
			 * If `af' = AF_UNSPEC and both IPv4 and IPv6
			 * interfaces cannot be brought up, the interface must
			 * have been unplumbed. Return IPADM_NOSUCH_IF in this
			 * case.
			 */
			if (status == IPADM_NOSUCH_IF && !v4_up)
				return (IPADM_NOSUCH_IF);
		}
	}
	return (IPADM_SUCCESS);
}

/*
 * Mark the addresses on `ifname' as test addresses. This is called by a
 * non-legacy application (e.g. ipadm(1M)) after adding `ifname' to an
 * IPMP group.
 */
ipadm_status_t
ipadm_mark_testaddrs(ipadm_handle_t iph, const char *ifname)
{
	return (i_ipadm_mark_testaddrs_common(iph, ifname, B_TRUE));
}

/*
 * Clear the test addresses on `ifname'. This is called by a non-legacy
 * application (e.g. ipadm(1M)) after removing `ifname' from an IPMP group.
 */
ipadm_status_t
ipadm_clear_testaddrs(ipadm_handle_t iph, const char *ifname)
{
	return (i_ipadm_mark_testaddrs_common(iph, ifname, B_FALSE));
}

static ipadm_status_t
i_ipadm_mark_testaddrs_common(ipadm_handle_t iph, const char *ifname,
    boolean_t mark)
{
	struct ifaddrs		*ifap;
	char			tmp_ifname[LIFNAMSIZ];
	sa_family_t		af;
	ipadm_status_t		status;
	ipadm_addr_info_t	*ainfo;
	ipadm_addr_info_t	*ainfop;
	uint64_t		set = 0, clear = 0;

	status = ipadm_addr_info(iph, ifname, &ainfo, 0, LIFC_UNDER_IPMP);
	if (status != IPADM_SUCCESS)
		return (status);
	if (mark)
		set = IFF_NOFAILOVER;
	else
		clear = IFF_NOFAILOVER|IFF_DEPRECATED;
	for (ainfop = ainfo; ainfop != NULL; ainfop = IA_NEXT(ainfop)) {
		ifap = &ainfop->ia_ifa;
		i_ipadm_get_ifname(ifap->ifa_name, tmp_ifname, LIFNAMSIZ);
		if (strcmp(tmp_ifname, ifname) != 0)
			continue;
		af = IPADM_IFF2AF(ifap->ifa_flags);
		if (mark) {
			if (!i_ipadm_is_dataaddr(ainfop))
				continue;
		} else {
			/*
			 * When IFF_NOFAILOVER is cleared, kernel also clears
			 * the IFF_DEPRECATED flag. But this has a side effect
			 * on test addresses managed by dhcpagent. Clearing
			 * the IFF_DEPRECATED flag leads the dhcpagent into
			 * dropping its control on the logical interface. So,
			 * we want to ignore DHCP-managed test addresses here.
			 */
			if (!(ifap->ifa_flags & IFF_NOFAILOVER) ||
			    (ifap->ifa_flags & IFF_DHCPRUNNING))
				continue;
		}
		status = i_ipadm_set_flags(iph, ifap->ifa_name, af, set, clear);
		if (status != IPADM_SUCCESS)
			break;
	}
	ipadm_free_addr_info(ainfo);
	return (status);
}

/*
 * Adds/removes the given underlying interface `underif' from/to the
 * IPMP interface `ipmpif' in the persistent DB.
 */
static ipadm_status_t
i_ipadm_persist_update_ipmp(ipadm_handle_t iph, const char *ipmpif,
    const char *underif, ipadm_ipmpop_t op)
{
	ipmgmt_ipmp_arg_t	ipmparg;
	int			err;

	bzero(&ipmparg, sizeof (ipmgmt_ipmp_arg_t));
	(void) strlcpy(ipmparg.ia_ipmpif, ipmpif, sizeof (ipmparg.ia_ipmpif));
	(void) strlcpy(ipmparg.ia_underif, underif,
	    sizeof (ipmparg.ia_underif));
	ipmparg.ia_cmd = IPMGMT_CMD_UPDATE_IPMP;
	if (op == IPADM_IPMPOP_ADD) {
		ipmparg.ia_flags = IPMGMT_APPEND;
	} else {
		assert(op == IPADM_IPMPOP_REMOVE);
		ipmparg.ia_flags = IPMGMT_REMOVE;
	}
	ipmparg.ia_flags |= IPMGMT_PERSIST;
	err = ipadm_door_call(iph, &ipmparg, sizeof (ipmparg), NULL, 0);
	return (ipadm_errno2status(err));
}

/*
 * Bring down the IFF_UP and IFF_DUPLICATE addresses on interface `ifname' and
 * return these addresses in `downaddrs'. When no IFF_UP addresses are found
 * on the interface, IPADM_SUCCESS is returned.
 */
ipadm_status_t
ipadm_down_addrs(ipadm_handle_t iph, const char *ifname,
    ipadm_addr_info_t **downaddrs)
{
	ipadm_status_t	status;
	sa_family_t	af;
	struct ifaddrs	*ifap;
	ipadm_addr_info_t *curr, *next, *last = NULL;

	status = ipadm_addr_info(iph, ifname, downaddrs, IPADM_OPT_ZEROADDR, 0);
	if (status == IPADM_OBJ_NOTFOUND)
		return (IPADM_SUCCESS);
	if (status != IPADM_SUCCESS)
		return (status);
	for (curr = *downaddrs; curr != NULL; curr = next) {
		next = IA_NEXT(curr);
		ifap = &curr->ia_ifa;
		if ((ifap->ifa_flags & (IFF_UP|IFF_DUPLICATE)) != 0) {
			af = IPADM_IFF2AF(ifap->ifa_flags);
			status = i_ipadm_set_flags(iph, ifap->ifa_name, af,
			    0, IFF_UP);
			if (status != IPADM_SUCCESS &&
			    (status != IPADM_NOSUCH_IF ||
			    !(ifap->ifa_flags & IFF_ADDRCONF)))
				goto fail;
			/*
			 * The address is successfully brought down.
			 * Keep the current node in the list and continue.
			 * Ignore the node if it an auto-configured address,
			 * as this would be removed by in.ndpd when the
			 * link-local was brought down.
			 */
			if (!(ifap->ifa_flags & IFF_ADDRCONF)) {
				last = curr;
				continue;
			}
		}
		/*
		 * The address is already down or it's an ADDRCONF address.
		 * Update pointers, remove `curr' from the list and free
		 * memory allocated for `curr'.
		 */
		if (last == NULL)
			*downaddrs = next;
		else
			last->ia_ifa.ifa_next = curr->ia_ifa.ifa_next;
		curr->ia_ifa.ifa_next = NULL;
		ipadm_free_addr_info(curr);
	}
	return (IPADM_SUCCESS);
fail:
	for (curr = *downaddrs; curr != NULL; curr = IA_NEXT(curr)) {
		af = IPADM_IFF2AF(curr->ia_ifa.ifa_flags);
		(void) i_ipadm_set_flags(iph, curr->ia_ifa.ifa_name, af,
		    IFF_UP, 0);
	}
	ipadm_free_addr_info(*downaddrs);
	*downaddrs = NULL;
	return (status);
}

/*
 * Bring up the addresses in `upaddrs'.
 */
ipadm_status_t
ipadm_up_addrs(ipadm_handle_t iph, ipadm_addr_info_t *upaddrs)
{
	ipadm_addr_info_t	*ainfop;
	sa_family_t		af;
	ipadm_status_t		status;

	for (ainfop = upaddrs; ainfop != NULL; ainfop = IA_NEXT(ainfop)) {
		af = IPADM_IFF2AF(ainfop->ia_ifa.ifa_flags);
		status = i_ipadm_set_flags(iph, ainfop->ia_ifa.ifa_name, af,
		    IFF_UP, 0);
		if (status != IPADM_SUCCESS)
			return (status);
	}
	return (IPADM_SUCCESS);
}

/*
 * Function to verify that there are no data addresses managed by
 * applications such as dhcpagent(1M) or in.ndpd(1M) for the
 * given interface. If such addresses are found, we retry a few times
 * before bailing out.
 *
 * The following errors are returned:
 * - IPADM_SUCCESS, when no data addresses managed by dhcpagent(1M) or
 *    in.ndpd(1M) are found,
 * - IPADM_UNDERIF_DHCP_MANAGED,  when a dhcp address is found,
 * - IPADM_UNDERIF_NDPD_MANAGED,  when an auto-configured IPv6 address is found,
 * - error from ipadm_addr_info(), otherwise.
 */
ipadm_status_t
ipadm_wait_app_addrs(ipadm_handle_t iph, const char *ifname)
{
	const ipadm_appflags_t	*iap;
	int			ntries;
	ipadm_addr_info_t	*ainfo = NULL;
	ipadm_addr_info_t	*aip;
	struct ifaddrs		*ifap;
	ipadm_status_t		status = IPADM_SUCCESS;

	for (iap = ipadm_appflags_tbl; iap->ia_flag != 0; iap++) {
		ntries = 0;
retry:
		ipadm_free_addr_info(ainfo);
		status = ipadm_addr_info(iph, ifname, &ainfo,
		    IPADM_OPT_ZEROADDR, 0);
		if (status != IPADM_SUCCESS)
			return (status);
		for (aip = ainfo; aip != NULL; aip = IA_NEXT(aip)) {
			ifap = &aip->ia_ifa;
			if ((ifap->ifa_flags & iap->ia_flag) &&
			    !(ifap->ifa_flags & IFF_NOFAILOVER)) {
				if (++ntries < iap->ia_tries) {
					(void) poll(NULL, 0, 100);
					goto retry;
				}
				status = iap->ia_status;
				goto out;
			}
		}
	}
out:
	ipadm_free_addr_info(ainfo);
	return (status);
}

/*
 * Function that creates one of the address families of the IPMP interface
 * `ifname' that is not already plumbed. If both IPv4 and IPv6 are already
 * plumbed, it returns IPADM_SUCCESS.
 */
ipadm_status_t
ipadm_create_ipmp_implicit(ipadm_handle_t iph, const char *ifname)
{
	sa_family_t	af;

	if (!ipadm_if_enabled(iph, ifname, AF_INET))
		af = AF_INET;
	else if (!ipadm_if_enabled(iph, ifname, AF_INET6))
		af = AF_INET6;
	else
		return (IPADM_SUCCESS);
	/*
	 * It is safe to cast the pointer here, because the flags passed
	 * do not contain IPADM_OPT_GENPPA and ifname will not be modified
	 * by i_ipadm_create_ipmp().
	 */
	return (i_ipadm_create_ipmp(iph, (char *)ifname, af, B_TRUE,
	    IPADM_OPT_ACTIVE));
}

/*
 * Returns information about all interfaces in both active and persistent
 * configuration in a linked list `if_info'. Each element in the linked
 * list is of type `ipadm_if_info_t'.
 * If `ifname' is not NULL, it returns only the interface identified by
 * `ifname'.
 *
 * Return values:
 * 	On success: IPADM_SUCCESS.
 * 	On error  : IPADM_INVALID_ARG, IPADM_NOSUCH_IF or IPADM_FAILURE.
 */
ipadm_status_t
ipadm_if_info(ipadm_handle_t iph, const char *ifname,
    list_t *if_info, uint32_t flags, int64_t lifc_flags)
{
	ipadm_status_t	status;
	ifspec_t	ifsp;

	if (if_info == NULL || iph == NULL || flags != 0)
		return (IPADM_INVALID_ARG);

	if (ifname != NULL &&
	    (!ifparse_ifspec(ifname, &ifsp) || ifsp.ifsp_lunvalid)) {
		return (IPADM_INVALID_ARG);
	}

	status = i_ipadm_get_all_if_info(iph, ifname, if_info, lifc_flags);
	if (status != IPADM_SUCCESS)
		return (status);
	return (IPADM_SUCCESS);
}

/*
 * Frees the linked list of underlying interfaces in `iflist'.
 */
void
i_ipadm_free_iflist(list_t *iflist)
{
	ipadm_ifname_t	*ifp;

	while ((ifp = list_remove_head(iflist)) != NULL)
		free(ifp);
	list_destroy(iflist);
}

/*
 * Frees the linked list allocated by ipadm_if_info().
 */
void
ipadm_free_if_info(list_t *ifinfo)
{
	ipadm_if_info_t *ifp;

	while ((ifp = list_remove_head(ifinfo)) != NULL) {
		i_ipadm_free_iflist(&ifp->ifi_unders);
		i_ipadm_free_iflist(&ifp->ifi_punders);
		free(ifp);
	}
	list_destroy(ifinfo);
}

/*
 * Sends command IPMGMT_CMD_INITIF to ipmgmtd daemon and retrieves the set of
 * all addresses, address properties and the interface properties into the
 * nvlist `onvl' for `ifname'.
 */
static ipadm_status_t
i_ipadm_get_db_initif(ipadm_handle_t iph, const char *ifname, nvlist_t **onvl)
{
	ipmgmt_getif_arg_t	getif;
	ipmgmt_get_rval_t	*rvalp;
	size_t			nvlsize;
	char			*nvlbuf;
	int			err = 0;

	bzero(&getif, sizeof (getif));
	(void) strlcpy(getif.ia_ifname, ifname, LIFNAMSIZ);
	getif.ia_cmd = IPMGMT_CMD_INITIF;

	if ((rvalp = malloc(sizeof (ipmgmt_get_rval_t))) == NULL)
		return (ipadm_errno2status(errno));
	err = ipadm_door_dyncall(iph, &getif, sizeof (getif), (void **)&rvalp,
	    sizeof (*rvalp));
	if (err != 0) {
		free(rvalp);
		*onvl = NULL;
		return (ipadm_errno2status(err));
	}
	nvlsize = rvalp->ir_nvlsize;
	nvlbuf = (char *)rvalp + sizeof (ipmgmt_get_rval_t);
	err = nvlist_unpack(nvlbuf, nvlsize, onvl, NV_ENCODE_NATIVE);
	free(rvalp);
	return (ipadm_errno2status(err));
}

/*
 * Re-enable the interface `ifname' based on the saved configuration
 * for `ifname'.
 */
ipadm_status_t
ipadm_enable_if(ipadm_handle_t iph, const char *ifname, uint32_t flags)
{
	nvlist_t	*ifnvl;
	ipadm_status_t	status, rstatus;
	ifspec_t	ifsp;

	/* Check for the required authorization */
	if (!ipadm_check_auth())
		return (IPADM_INSUFF_AUTH);

	/* Check for logical interfaces. */
	if (!ifparse_ifspec(ifname, &ifsp) || ifsp.ifsp_lunvalid)
		return (IPADM_INVALID_ARG);

	/* Enabling an interface persistently is not supported. */
	if (flags & IPADM_OPT_PERSIST)
		return (IPADM_OP_NOTSUP);

	/*
	 * Return early by checking if the interface is already enabled.
	 */
	if (ipadm_if_enabled(iph, ifname, AF_INET) &&
	    ipadm_if_enabled(iph, ifname, AF_INET6)) {
		return (IPADM_IF_EXISTS);
	}
	/*
	 * Enable the interface and restore all its interface properties
	 * and address objects.
	 */
	status = i_ipadm_get_db_initif(iph, ifname, &ifnvl);
	if (status != IPADM_SUCCESS)
		return (status);

	assert(ifnvl != NULL);
	/*
	 * We need to set IH_INIT because ipmgmtd daemon does not have to
	 * write the interface to persistent db. The interface is already
	 * available in persistent db and we are here to re-enable the
	 * persistent configuration.
	 */
	iph->ih_flags |= IH_INIT;
	rstatus = i_ipadm_enable_if(iph, ifname, ifnvl);
	iph->ih_flags &= ~IH_INIT;
	return (rstatus);
}

/*
 * Disable the interface `ifname' by removing it from the active configuration.
 * Error code return values follow the model in ipadm_delete_if()
 */
ipadm_status_t
ipadm_disable_if(ipadm_handle_t iph, const char *ifname, uint32_t flags)
{
	ifspec_t	ifsp;

	/* Check for the required authorization */
	if (!ipadm_check_auth())
		return (IPADM_INSUFF_AUTH);

	/* Check for logical interfaces. */
	if (!ifparse_ifspec(ifname, &ifsp) || ifsp.ifsp_lunvalid)
		return (IPADM_INVALID_ARG);

	/*
	 * Disabling an interface persistently is not supported.
	 * In addition, we don't let the loopback interface be disabled.
	 */
	if (ipadm_is_loopback(ifname) || (flags & IPADM_OPT_PERSIST))
		return (IPADM_OP_NOTSUP);

	return (i_ipadm_disable_if(iph, ifname, flags));
}

/* ARGSUSED */
ipadm_status_t
i_ipadm_disable_if(ipadm_handle_t iph, const char *ifname, uint32_t flags)
{
	ipadm_status_t	status1, status2;

	status1 = i_ipadm_unplumb_if(iph, ifname, AF_INET6, IPADM_OPT_ACTIVE);
	if (status1 == IPADM_SUCCESS)
		status1 = i_ipadm_delete_ifobj(iph, ifname, AF_INET6, B_FALSE);
	status2 = i_ipadm_unplumb_if(iph, ifname, AF_INET, IPADM_OPT_ACTIVE);
	if (status2 == IPADM_SUCCESS)
		status2 = i_ipadm_delete_ifobj(iph, ifname, AF_INET, B_FALSE);
	return (i_ipadm_delete_if_status(status1, status2));
}

/*
 * When an interface with addresses is put in an IPMP group, the data
 * addresses will be migrated to the IPMP interface in the kernel.
 * This function removes the address objects for those addresses in `upaddr'
 * from `underif', and recreates them on `ipmpif'.
 */
ipadm_status_t
ipadm_migrate_dataaddrs(ipadm_handle_t iph, const char *underif,
    ipadm_addr_info_t *addrs)
{
	ipadm_addr_info_t	*u_ptr;
	struct ifaddrs		*u_ifap;
	list_t			maddrs;
	ipadm_migrate_addr_t	*ptr;
	ipadm_status_t		status = IPADM_SUCCESS;
	lifgroupinfo_t		lifgr;
	struct lifreq		lifr;
	int			sock;

	sock = IPADM_SOCK(iph, i_ipadm_get_active_af(iph, underif));
	(void) strlcpy(lifr.lifr_name, underif, sizeof (lifr.lifr_name));
	if (ioctl(sock, SIOCGLIFGROUPNAME, &lifr) < 0)
		return (ipadm_errno2status(errno));
	(void) strlcpy(lifgr.gi_grname, lifr.lifr_groupname, LIFGRNAMSIZ);
	if (ioctl(sock, SIOCGLIFGROUPINFO, &lifgr) < 0)
		return (ipadm_errno2status(errno));
	/*
	 * Create a list of address objects to migrate.
	 * Do not include addresses:
	 *  - that have IFF_NOFAILOVER set,
	 *  - are unspecified, and
	 *  - are link-local addresses.
	 * This list is then sent to ipmgmtd daemon to be migrated.
	 */
	list_create(&maddrs, sizeof (ipadm_migrate_addr_t),
	    offsetof(ipadm_migrate_addr_t, im_link));
	for (u_ptr = addrs; u_ptr != NULL; u_ptr = IA_NEXT(u_ptr)) {
		u_ifap = &u_ptr->ia_ifa;
		if (!i_ipadm_is_dataaddr(u_ptr))
			continue;
		ptr = malloc(sizeof (struct ipadm_migrate_addr_s));
		if (ptr == NULL) {
			status = ipadm_errno2status(errno);
			goto out;
		}
		bcopy(u_ifap->ifa_addr, &ptr->im_addr,
		    sizeof (struct sockaddr_storage));
		(void) strlcpy(ptr->im_aobjname, u_ptr->ia_aobjname,
		    sizeof (ptr->im_aobjname));
		ptr->im_lifnum = i_ipadm_get_lnum(u_ifap->ifa_name);
		list_insert_tail(&maddrs, ptr);
	}
	status = i_ipadm_migrate_addrs(iph, underif, &maddrs,
	    lifgr.gi_grifname);
out:
	while ((ptr = list_remove_head(&maddrs)) != NULL)
		free(ptr);
	list_destroy(&maddrs);
	return (status);
}

/*
 * Retrieves the IPMP interface name for the given `underif' from the
 * persistent configuration.
 */
ipadm_status_t
i_ipadm_get_persist_ipmpif(ipadm_handle_t iph, const char *underif,
    char *ipmpif, size_t len)
{
	ipadm_status_t	status;
	ipadm_if_info_t	*ifp;
	list_t		ifinfo;

	status = i_ipadm_persist_if_info(iph, NULL, &ifinfo);
	if (status != IPADM_SUCCESS)
		return (status);
	ifp = list_head(&ifinfo);
	for (; ifp != NULL; ifp = list_next(&ifinfo, ifp)) {
		if (!i_ipadm_ifname_in_list(&ifp->ifi_punders, underif))
			continue;
		(void) strlcpy(ipmpif, ifp->ifi_name, len);
		ipadm_free_if_info(&ifinfo);
		return (IPADM_SUCCESS);
	}
	ipadm_free_if_info(&ifinfo);
	return (IPADM_OBJ_NOTFOUND);
}

/*
 * Checks if the given address is a data address.
 */
static boolean_t
i_ipadm_is_dataaddr(ipadm_addr_info_t *addr)
{
	struct ifaddrs		*ifap = &addr->ia_ifa;
	struct sockaddr_in6	*sin6;

	/* LINTED E_BAD_PTR_CAST_ALIGN */
	sin6 = (struct sockaddr_in6 *)ifap->ifa_addr;
	if ((ifap->ifa_flags & IFF_NOFAILOVER) ||
	    sockaddrunspec((struct sockaddr *)ifap->ifa_addr) ||
	    ((ifap->ifa_flags & IFF_IPV6) &&
	    IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr)))
		return (B_FALSE);
	return (B_TRUE);
}
