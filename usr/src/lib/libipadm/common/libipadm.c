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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stropts.h>
#include <sys/sockio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <net/route.h>
#include <netinet/in.h>
#include <inet/ip.h>
#include <arpa/inet.h>
#include <libintl.h>
#include <libdlpi.h>
#include <libinetutil.h>
#include <libdladm.h>
#include <libdllink.h>
#include <libdliptun.h>
#include <strings.h>
#include <zone.h>
#include <ctype.h>
#include <limits.h>
#include <assert.h>
#include <netdb.h>
#include <pwd.h>
#include <auth_attr.h>
#include <secdb.h>
#include <nss_dbdefs.h>
#include "libipadm_impl.h"

static ipadm_status_t	i_ipadm_ipmpif_from_nvl(nvlist_t *, const char *,
			    char *, size_t);

/* error codes and text description */
static struct ipadm_error_info {
	ipadm_status_t	error_code;
	const char	*error_desc;
} ipadm_errors[] = {
	{ IPADM_SUCCESS,	"Operation succeeded" },
	{ IPADM_FAILURE,	"Operation failed" },
	{ IPADM_INSUFF_AUTH,	"Insufficient user authorizations" },
	{ IPADM_PERM_DENIED,		"Permission denied" },
	{ IPADM_NO_BUFS,	"No buffer space available" },
	{ IPADM_NO_MEMORY,	"Insufficient memory" },
	{ IPADM_BAD_ADDR,	"Invalid address" },
	{ IPADM_INVALID_IFID,	"Invalid interface id" },
	{ IPADM_BAD_PROTOCOL,	"Incorrect protocol family for operation" },
	{ IPADM_DAD_FOUND,	"Duplicate address detected" },
	{ IPADM_OBJ_EXISTS,	"Object already exists" },
	{ IPADM_IF_EXISTS,	"Interface already exists" },
	{ IPADM_ADDROBJ_EXISTS, "Address object already exists" },
	{ IPADM_ADDRCONF_EXISTS, "Addrconf already in progress" },
	{ IPADM_NOSUCH_IF,	"No such interface" },
	{ IPADM_GRP_NOTEMPTY,	"IPMP group is not empty" },
	{ IPADM_INVALID_ARG,	"Invalid argument provided" },
	{ IPADM_INVALID_IFNAME,	"Invalid interface name" },
	{ IPADM_DLPI_FAILURE,	"Could not open DLPI link" },
	{ IPADM_DLPI_NOLINK,	"Datalink does not exist" },
	{ IPADM_DLADM_FAILURE,	"Datalink does not exist" },
	{ IPADM_PROP_UNKNOWN,   "Unknown property" },
	{ IPADM_VAL_OUT_OF_RANGE, "Value is outside the allowed range" },
	{ IPADM_VAL_NOTEXISTS,	"Value does not exist" },
	{ IPADM_VAL_OVERFLOW,	"Number of values exceeds the allowed limit" },
	{ IPADM_OBJ_NOTFOUND,	"Object not found" },
	{ IPADM_IF_INUSE,	"Interface already in use" },
	{ IPADM_ADDR_INUSE,	"Address already in use" },
	{ IPADM_HOSTNAME_TO_MULTADDR,
	    "Hostname maps to multiple IP addresses" },
	{ IPADM_CANNOT_ASSIGN_ADDR, "Can't assign requested address" },
	{ IPADM_NDPD_NOT_RUNNING, "IPv6 autoconf daemon in.ndpd not running" },
	{ IPADM_NDPD_TIMEOUT,	"Communication with in.ndpd timed out" },
	{ IPADM_NDPD_IO_FAILURE, "I/O error with in.ndpd" },
	{ IPADM_CANNOT_START_DHCP, "Could not start dhcpagent" },
	{ IPADM_DHCP_IPC_FAILURE, "Communication with dhcpagent failed" },
	{ IPADM_DHCP_INVALID_IF, "DHCP client could not run on the interface" },
	{ IPADM_DHCP_IPC_TIMEOUT, "Communication with dhcpagent timed out" },
	{ IPADM_TEMPORARY_OBJ, "Persistent operation on temporary object" },
	{ IPADM_DAEMON_IPC_FAILURE, "Communication with daemon failed" },
	{ IPADM_OP_DISABLE_OBJ, "Operation not supported on disabled object" },
	{ IPADM_OP_NOTSUP,	"Operation not supported" },
	{ IPADM_INVALID_EXCH,	"Invalid data exchange with daemon" },
	{ IPADM_NOT_IPMPIF,	"Not an IPMP interface"},
	{ IPADM_NOSUCH_IPMPIF,	"No such IPMP interface" },
	{ IPADM_NOSUCH_UNDERIF,	"No such underlying interface" },
	{ IPADM_ALREADY_IN_GRP,	"Already in an IPMP group" },
	{ IPADM_NOT_IN_GROUP,	"Interface not in given IPMP group" },
	{ IPADM_IPMPIF_NOT_ENABLED, "IPMP interface is not enabled" },
	{ IPADM_IPMPIF_DHCP_NOT_ENABLED, "DHCP data address is not enabled" },
	{ IPADM_IF_NOT_FULLY_ENABLED, "Interface could not be fully enabled" },
	{ IPADM_UNDERIF_APP_ADDRS,
	    "Underlying interface has addresses managed by external"
	    "applications" },
	{ IPADM_UNDERIF_DHCP_MANAGED,
	    "Underlying interface has addresses managed by dhcpagent(1M)" },
	{ IPADM_UNDERIF_NDPD_MANAGED,
	    "Underlying interface has addresses managed by in.ndpd(1M)" },
	{ IPADM_UNDERIF_UP_ADDRS,
	    "Underlying interface has addresses marked up" },
	{ IPADM_IPMPIF_MISSING_AF,
	    "IPMP interface missing address families configured on "
	    "underlying interface" },
	{ IPADM_ADDROBJ_NOT_CREATED,
	    "Address objects could not be created for all of the migrated "
	    "addresses" }
};

#define	IPADM_NUM_ERRORS	(sizeof (ipadm_errors) / sizeof (*ipadm_errors))

ipadm_status_t
ipadm_errno2status(int error)
{
	switch (error) {
	case 0:
		return (IPADM_SUCCESS);
	case ENXIO:
		return (IPADM_NOSUCH_IF);
	case ENOMEM:
		return (IPADM_NO_MEMORY);
	case ENOBUFS:
		return (IPADM_NO_BUFS);
	case EINVAL:
		return (IPADM_INVALID_ARG);
	case EBUSY:
		return (IPADM_IF_INUSE);
	case EEXIST:
		return (IPADM_OBJ_EXISTS);
	case EADDRNOTAVAIL:
		return (IPADM_CANNOT_ASSIGN_ADDR);
	case EADDRINUSE:
		return (IPADM_ADDR_INUSE);
	case ENOENT:
		return (IPADM_OBJ_NOTFOUND);
	case ERANGE:
		return (IPADM_VAL_OUT_OF_RANGE);
	case EACCES:
	case EPERM:
		return (IPADM_PERM_DENIED);
	case ENOTSUP:
	case EOPNOTSUPP:
		return (IPADM_OP_NOTSUP);
	case EBADF:
		return (IPADM_DAEMON_IPC_FAILURE);
	case EBADE:
		return (IPADM_INVALID_EXCH);
	case ESRCH:
		return (IPADM_VAL_NOTEXISTS);
	case EOVERFLOW:
		return (IPADM_VAL_OVERFLOW);
	default:
		return (IPADM_FAILURE);
	}
}

/*
 * Returns a message string for the given libipadm error status.
 */
const char *
ipadm_status2str(ipadm_status_t status)
{
	int	i;

	for (i = 0; i < IPADM_NUM_ERRORS; i++) {
		if (status == ipadm_errors[i].error_code)
			return (dgettext(TEXT_DOMAIN,
			    ipadm_errors[i].error_desc));
	}

	return (dgettext(TEXT_DOMAIN, "<unknown error>"));
}

static ipadm_status_t
dladm2ipadm_status(dladm_status_t dlstatus)
{
	switch (dlstatus) {
	case DLADM_STATUS_DENIED:
		return (IPADM_PERM_DENIED);
	case DLADM_STATUS_NOMEM:
		return (IPADM_NO_MEMORY);
	default:
		return (IPADM_DLADM_FAILURE);
	}
}

/*
 * Opens a handle to libipadm.
 * Possible values for flags:
 *  IH_VRRP:	Used by VRRP daemon to set the socket option SO_VRRP.
 *  IH_LEGACY:	This is used whenever an application needs to provide a
 *		logical interface name while creating or deleting
 *		interfaces and static addresses.
 *  IH_INIT:    Used by ipadm_init_prop(), to initialize protocol properties
 *		on reboot.
 */
ipadm_status_t
ipadm_open(ipadm_handle_t *handle, uint32_t flags)
{
	ipadm_handle_t	iph;
	dladm_status_t	dlstatus;
	ipadm_status_t	status = IPADM_SUCCESS;
	zoneid_t	zoneid;
	ushort_t	zflags;
	int		on = B_TRUE;

	if (handle == NULL)
		return (IPADM_INVALID_ARG);
	*handle = NULL;

	if (flags & ~(IH_VRRP|IH_LEGACY|IH_INIT|IH_IPMGMTD))
		return (IPADM_INVALID_ARG);

	if ((iph = calloc(1, sizeof (struct ipadm_handle))) == NULL)
		return (IPADM_NO_MEMORY);
	iph->ih_sock = -1;
	iph->ih_sock6 = -1;
	iph->ih_door_fd = -1;
	iph->ih_rtsock = -1;
	iph->ih_flags = flags;
	(void) pthread_mutex_init(&iph->ih_lock, NULL);

	if ((iph->ih_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ||
	    (iph->ih_sock6 = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
		goto errnofail;
	}

	/*
	 * We open a handle to libdladm here, to facilitate some daemons (like
	 * nwamd) which opens handle to libipadm before devfsadmd installs the
	 * right device permissions into the kernel and requires "all"
	 * privileges to open DLD_CONTROL_DEV.
	 *
	 * In a non-global shared-ip zone there will be no DLD_CONTROL_DEV node
	 * and dladm_open() will fail. So, we avoid this by not calling
	 * dladm_open() for such zones.
	 */
	zoneid = getzoneid();
	iph->ih_zoneid = zoneid;
	if (zoneid != GLOBAL_ZONEID) {
		if (zone_getattr(zoneid, ZONE_ATTR_FLAGS, &zflags,
		    sizeof (zflags)) < 0) {
			goto errnofail;
		}
	}
	if ((zoneid == GLOBAL_ZONEID) || (zflags & ZF_NET_EXCL)) {
		if ((dlstatus = dladm_open(&iph->ih_dh)) != DLADM_STATUS_OK) {
			ipadm_close(iph);
			return (dladm2ipadm_status(dlstatus));
		}
		if (zoneid != GLOBAL_ZONEID) {
			iph->ih_rtsock = socket(PF_ROUTE, SOCK_RAW, 0);
			/*
			 * Failure to open rtsock is ignored as this is
			 * only used in non-global zones to initialize
			 * routing socket information.
			 */
		}
	} else {
		assert(zoneid != GLOBAL_ZONEID);
		iph->ih_dh = NULL;
	}
	if (flags & IH_VRRP) {
		if (setsockopt(iph->ih_sock6, SOL_SOCKET, SO_VRRP, &on,
		    sizeof (on)) < 0 || setsockopt(iph->ih_sock, SOL_SOCKET,
		    SO_VRRP, &on, sizeof (on)) < 0) {
			goto errnofail;
		}
	}
	*handle = iph;
	return (status);

errnofail:
	status = ipadm_errno2status(errno);
	ipadm_close(iph);
	return (status);
}

/*
 * Closes and frees the libipadm handle.
 */
void
ipadm_close(ipadm_handle_t iph)
{
	if (iph == NULL)
		return;
	if (iph->ih_sock != -1)
		(void) close(iph->ih_sock);
	if (iph->ih_sock6 != -1)
		(void) close(iph->ih_sock6);
	if (iph->ih_rtsock != -1)
		(void) close(iph->ih_rtsock);
	if (iph->ih_door_fd != -1)
		(void) close(iph->ih_door_fd);
	dladm_close(iph->ih_dh);
	(void) pthread_mutex_destroy(&iph->ih_lock);
	free(iph);
}

/*
 * Checks if the caller has the authorization to configure network
 * interfaces.
 */
boolean_t
ipadm_check_auth(void)
{
	struct passwd	pwd;
	char		buf[NSS_BUFLEN_PASSWD];

	/* get the password entry for the given user ID */
	if (getpwuid_r(getuid(), &pwd, buf, sizeof (buf)) == NULL)
		return (B_FALSE);

	/* check for presence of given authorization */
	return (chkauthattr(NETWORK_INTERFACE_CONFIG_AUTH, pwd.pw_name) != 0);
}

/*
 * Determines whether or not the given interface name represents a loopback
 * interface.
 *
 * Returns: B_TRUE if `ifname' is a loopback interface, B_FALSE if not.
 */
boolean_t
ipadm_is_loopback(const char *ifname)
{
	int len = strlen(LOOPBACK_IF);

	return (strncmp(ifname, LOOPBACK_IF, len) == 0 &&
	    (ifname[len] == '\0' || ifname[len] == IPADM_LOGICAL_SEP));
}

/*
 * Determines whether or not an interface name represents a vni interface.
 *
 * Returns: B_TRUE if vni, B_FALSE if not.
 */
boolean_t
ipadm_is_vni(const char *ifname)
{
	ifspec_t	ifsp;

	return (ifparse_ifspec(ifname, &ifsp) &&
	    strcmp(ifsp.ifsp_devnm, "vni") == 0);
}

/*
 * Determines if `ifname' is an interface of class IPADMIF_CLASS_IP.
 */
boolean_t
ipadm_is_ip(ipadm_handle_t iph, const char *ifname)
{
	if (ipadm_is_vni(ifname) || ipadm_is_loopback(ifname) ||
	    ipadm_is_ipmp(iph, ifname))
		return (B_FALSE);
	return (B_TRUE);
}

/*
 * Stores the index value of the interface in `ifname' for the address
 * family `af' into the buffer pointed to by `index'.
 */
static ipadm_status_t
i_ipadm_get_index(ipadm_handle_t iph, const char *ifname, sa_family_t af,
    int *index)
{
	struct lifreq	lifr;
	int		sock;

	bzero(&lifr, sizeof (lifr));
	(void) strlcpy(lifr.lifr_name, ifname, sizeof (lifr.lifr_name));
	sock = IPADM_SOCK(iph, af);

	if (ioctl(sock, SIOCGLIFINDEX, (caddr_t)&lifr) < 0)
		return (ipadm_errno2status(errno));
	*index = lifr.lifr_index;

	return (IPADM_SUCCESS);
}

/*
 * Maximum amount of time (in milliseconds) to wait for Duplicate Address
 * Detection to complete in the kernel.
 */
#define	DAD_WAIT_TIME		1000

/*
 * Any time that flags are changed on an interface where either the new or the
 * existing flags have IFF_UP set, we'll get a RTM_NEWADDR message to
 * announce the new address added and its flag status.
 * We wait here for that message and look for IFF_UP.
 * If something's amiss with the kernel, though, we don't wait forever.
 * (Note that IFF_DUPLICATE is a high-order bit, and we cannot see
 * it in the routing socket messages.)
 */
static ipadm_status_t
i_ipadm_dad_wait(ipadm_handle_t handle, const char *lifname, sa_family_t af,
    int rtsock)
{
	struct pollfd	fds[1];
	union {
		struct if_msghdr ifm;
		char buf[1024];
	} msg;
	int		index;
	ipadm_status_t	retv;
	uint64_t	flags;
	hrtime_t	starttime, now;

	fds[0].fd = rtsock;
	fds[0].events = POLLIN;
	fds[0].revents = 0;

	retv = i_ipadm_get_index(handle, lifname, af, &index);
	if (retv != IPADM_SUCCESS)
		return (retv);

	starttime = gethrtime();
	for (;;) {
		now = gethrtime();
		now = (now - starttime) / 1000000;
		if (now >= DAD_WAIT_TIME)
			break;
		if (poll(fds, 1, DAD_WAIT_TIME - (int)now) <= 0)
			break;
		if (read(rtsock, &msg, sizeof (msg)) <= 0)
			break;
		if (msg.ifm.ifm_type != RTM_NEWADDR)
			continue;
		/* Note that ifm_index is just 16 bits */
		if (index == msg.ifm.ifm_index && (msg.ifm.ifm_flags & IFF_UP))
			return (IPADM_SUCCESS);
	}

	retv = i_ipadm_get_flags(handle, lifname, af, &flags);
	if (retv != IPADM_SUCCESS)
		return (retv);
	if (flags & IFF_DUPLICATE)
		return (IPADM_DAD_FOUND);

	return (IPADM_SUCCESS);
}

/*
 * Sets the flags in `set' and clears the flags in `clear' for the logical
 * interface in `lifname'.
 *
 * If the new flags value will transition the interface from "down" to "up"
 * then duplicate address detection is performed by the kernel.  This routine
 * waits to get the outcome of that test.
 */
ipadm_status_t
i_ipadm_set_flags(ipadm_handle_t iph, const char *lifname, sa_family_t af,
    uint64_t set, uint64_t clear)
{
	struct lifreq	lifr;
	uint64_t	oflags;
	ipadm_status_t	status;
	int		rtsock = -1;
	int		sock, err;

	status = i_ipadm_get_flags(iph, lifname, af, &oflags);
	if (status != IPADM_SUCCESS)
		return (status);

	sock = IPADM_SOCK(iph, af);

	/*
	 * Any time flags are changed on an interface that has IFF_UP set,
	 * we get a routing socket message.  We care about the status,
	 * though, only when the new flags are marked "up."
	 */
	if (!(oflags & IFF_UP) && (set & IFF_UP))
		rtsock = socket(PF_ROUTE, SOCK_RAW, af);

	oflags |= set;
	oflags &= ~clear;
	bzero(&lifr, sizeof (lifr));
	(void) strlcpy(lifr.lifr_name, lifname, sizeof (lifr.lifr_name));
	lifr.lifr_flags = oflags;
	if (ioctl(sock, SIOCSLIFFLAGS, (caddr_t)&lifr) < 0) {
		err = errno;
		if (rtsock != -1)
			(void) close(rtsock);
		return (ipadm_errno2status(err));
	}
	if (rtsock == -1) {
		/*
		 * If the address is brought down, ensure that the DAD
		 * activity is stopped.
		 */
		if ((clear & IFF_UP) && (oflags & IFF_DUPLICATE) &&
		    (ioctl(sock, SIOCGLIFADDR, (caddr_t)&lifr) < 0 ||
		    ioctl(sock, SIOCSLIFADDR, (caddr_t)&lifr) < 0)) {
			err = errno;
			return (ipadm_errno2status(err));
		}
		return (IPADM_SUCCESS);
	} else {
		/*
		 * We don't wait for DAD to complete for migrated IPMP
		 * data addresses.
		 */
		if (!(oflags & IFF_NOFAILOVER)) {
			char	ifname[LIFNAMSIZ];

			i_ipadm_get_ifname(lifname, ifname, sizeof (ifname));
			if (i_ipadm_is_under_ipmp(iph, ifname))
				return (IPADM_SUCCESS);
		}
		/* Wait for DAD to complete. */
		status = i_ipadm_dad_wait(iph, lifname, af, rtsock);
		(void) close(rtsock);
		return (status);
	}
}

/*
 * Returns the flags value for the logical interface in `lifname'
 * in the buffer pointed to by `flags'.
 */
ipadm_status_t
i_ipadm_get_flags(ipadm_handle_t iph, const char *lifname, sa_family_t af,
    uint64_t *flags)
{
	struct lifreq	lifr;
	int		sock;

	bzero(&lifr, sizeof (lifr));
	(void) strlcpy(lifr.lifr_name, lifname, sizeof (lifr.lifr_name));
	sock = IPADM_SOCK(iph, af);

	if (ioctl(sock, SIOCGLIFFLAGS, (caddr_t)&lifr) < 0)
		return (ipadm_errno2status(errno));
	*flags = lifr.lifr_flags;

	return (IPADM_SUCCESS);
}

/*
 * Returns B_TRUE if `ifname' is an IP interface on a 6to4 tunnel.
 */
boolean_t
i_ipadm_is_6to4(ipadm_handle_t iph, const char *ifname)
{
	dladm_status_t		dlstatus;
	datalink_class_t	class;
	iptun_params_t		params;
	datalink_id_t		linkid;

	if (iph->ih_dh == NULL) {
		assert(iph->ih_zoneid != GLOBAL_ZONEID);
		return (B_FALSE);
	}
	dlstatus = dladm_name2info(iph->ih_dh, ifname, &linkid, NULL,
	    &class, NULL);
	if (dlstatus == DLADM_STATUS_OK && class == DATALINK_CLASS_IPTUN) {
		params.iptun_param_linkid = linkid;
		dlstatus = dladm_iptun_getparams(iph->ih_dh, &params,
		    DLADM_OPT_ACTIVE);
		if (dlstatus == DLADM_STATUS_OK &&
		    params.iptun_param_type == IPTUN_TYPE_6TO4) {
			return (B_TRUE);
		}
	}
	return (B_FALSE);
}

ipadm_status_t
i_ipadm_get_groupname(ipadm_handle_t iph, const char *ifname, char *grname,
    size_t len)
{
	struct lifreq	lifr;

	(void) strlcpy(lifr.lifr_name, ifname, sizeof (lifr.lifr_name));
	if (ioctl(iph->ih_sock, SIOCGLIFGROUPNAME, (caddr_t)&lifr) < 0 &&
	    ioctl(iph->ih_sock6, SIOCGLIFGROUPNAME, (caddr_t)&lifr) < 0)
		return (ipadm_errno2status(errno));
	(void) strlcpy(grname, lifr.lifr_groupname, len);
	return (IPADM_SUCCESS);
}

/*
 * Returns B_TRUE if `ifname' represents an IPMP underlying interface.
 */
boolean_t
i_ipadm_is_under_ipmp(ipadm_handle_t iph, const char *ifname)
{
	char	grname[LIFGRNAMSIZ];

	if (ipadm_is_ipmp(iph, ifname))
		return (B_FALSE);
	if (i_ipadm_get_groupname(iph, ifname, grname,
	    LIFGRNAMSIZ) == IPADM_SUCCESS)
		return (grname[0] != '\0');
	return (B_FALSE);
}

/*
 * Returns B_TRUE if `ifname' represents an IPMP meta-interface.
 */
boolean_t
ipadm_is_ipmp(ipadm_handle_t iph, const char *ifname)
{
	struct lifreq	lifr;

	bzero(&lifr, sizeof (lifr));
	(void) strlcpy(lifr.lifr_name, ifname, sizeof (lifr.lifr_name));
	if (ioctl(iph->ih_sock, SIOCGLIFFLAGS, (caddr_t)&lifr) < 0 &&
	    ioctl(iph->ih_sock6, SIOCGLIFFLAGS, (caddr_t)&lifr) < 0)
		return (B_FALSE);

	return ((lifr.lifr_flags & IFF_IPMP) != 0);
}

/*
 * For a given interface name, ipadm_if_enabled() checks if v4
 * or v6 or both IP interfaces exist in the active configuration.
 */
boolean_t
ipadm_if_enabled(ipadm_handle_t iph, const char *ifname, sa_family_t af)
{
	struct lifreq	lifr;
	int		s4 = iph->ih_sock;
	int		s6 = iph->ih_sock6;

	bzero(&lifr, sizeof (lifr));
	(void) strlcpy(lifr.lifr_name, ifname, sizeof (lifr.lifr_name));
	switch (af) {
	case AF_INET:
		if (ioctl(s4, SIOCGLIFFLAGS, (caddr_t)&lifr) == 0)
			return (B_TRUE);
		break;
	case AF_INET6:
		if (ioctl(s6, SIOCGLIFFLAGS, (caddr_t)&lifr) == 0)
			return (B_TRUE);
		break;
	case AF_UNSPEC:
		if (ioctl(s4, SIOCGLIFFLAGS, (caddr_t)&lifr) == 0 ||
		    ioctl(s6, SIOCGLIFFLAGS, (caddr_t)&lifr) == 0) {
			return (B_TRUE);
		}
	}
	return (B_FALSE);
}

boolean_t
i_ipadm_is_legacy(ipadm_handle_t iph)
{
	return ((iph->ih_flags & IH_LEGACY) != 0);
}

/*
 * Returns the address family for which `ifname' exists in the active
 * configuration. AF_INET is returned if the interface is configured for
 * both IPv4 and IPv6. AF_UNSPEC is returned if the interface is not
 * configured at all.
 */
sa_family_t
i_ipadm_get_active_af(ipadm_handle_t iph, const char *ifname)
{
	if (ipadm_if_enabled(iph, ifname, AF_INET))
		return (AF_INET);
	if (ipadm_if_enabled(iph, ifname, AF_INET6))
		return (AF_INET6);
	return (AF_UNSPEC);
}

/*
 * Apply the interface property by retrieving information from nvl.
 */
static ipadm_status_t
i_ipadm_init_ifprop(ipadm_handle_t iph, nvlist_t *nvl)
{
	nvpair_t	*nvp;
	char		*name, *pname = NULL;
	char		*protostr = NULL, *ifname = NULL, *pval = NULL;
	uint_t		proto;
	int		err = 0;

	for (nvp = nvlist_next_nvpair(nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {
		name = nvpair_name(nvp);
		if (strcmp(name, IPADM_NVP_IFNAME) == 0) {
			if ((err = nvpair_value_string(nvp, &ifname)) != 0)
				break;
		} else if (strcmp(name, IPADM_NVP_PROTONAME) == 0) {
			if ((err = nvpair_value_string(nvp, &protostr)) != 0)
				break;
		} else {
			assert(!IPADM_PRIV_NVP(name));
			pname = name;
			if ((err = nvpair_value_string(nvp, &pval)) != 0)
				break;
		}
	}
	if (err != 0)
		return (ipadm_errno2status(err));
	proto = ipadm_str2proto(protostr);
	return (ipadm_set_ifprop(iph, ifname, pname, pval, proto,
	    IPADM_OPT_ACTIVE));
}

/*
 * Instantiate the address object or set the address object property by
 * retrieving the configuration from the nvlist `nvl'.
 */
ipadm_status_t
i_ipadm_init_addrobj(ipadm_handle_t iph, nvlist_t *nvl)
{
	nvpair_t	*nvp;
	char		*name;
	char		*aobjname = NULL, *pval = NULL, *ifname = NULL;
	sa_family_t	af = AF_UNSPEC;
	ipadm_addr_type_t atype = IPADM_ADDR_NONE;
	int		err = 0;
	ipadm_status_t	status = IPADM_SUCCESS;

	for (nvp = nvlist_next_nvpair(nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {
		name = nvpair_name(nvp);
		if (strcmp(name, IPADM_NVP_IFNAME) == 0) {
			if ((err = nvpair_value_string(nvp, &ifname)) != 0)
				break;
		} else if (strcmp(name, IPADM_NVP_AOBJNAME) == 0) {
			if ((err = nvpair_value_string(nvp, &aobjname)) != 0)
				break;
		} else if (i_ipadm_name2atype(name, &af, &atype)) {
			break;
		} else {
			assert(!IPADM_PRIV_NVP(name));
			err = nvpair_value_string(nvp, &pval);
			break;
		}
	}
	if (err != 0)
		return (ipadm_errno2status(err));

	switch (atype) {
	case IPADM_ADDR_STATIC:
		status = i_ipadm_enable_static(iph, ifname, nvl, af);
		break;
	case IPADM_ADDR_DHCP:
		status = i_ipadm_enable_dhcp(iph, ifname, nvl);
		if (status == IPADM_DHCP_IPC_TIMEOUT)
			status = IPADM_SUCCESS;
		break;
	case IPADM_ADDR_IPV6_ADDRCONF:
		status = i_ipadm_enable_addrconf(iph, ifname, nvl);
		break;
	case IPADM_ADDR_NONE:
		status = ipadm_set_addrprop(iph, name, pval, aobjname,
		    IPADM_OPT_ACTIVE);
		break;
	}

	return (status);
}

/*
 * Instantiate the interface object by retrieving the configuration from
 * `ifnvl'. The nvlist `ifnvl' contains all the persistent configuration
 * (interface properties and address objects on that interface) for the
 * given `ifname'.
 */
ipadm_status_t
i_ipadm_enable_if(ipadm_handle_t iph, const char *ifname, nvlist_t *ifnvl)
{
	nvlist_t	*nvl = NULL;
	nvpair_t	*nvp;
	char		*afstr;
	ipadm_status_t	status;
	ipadm_status_t	ret_status = IPADM_SUCCESS;
	char		newifname[LIFNAMSIZ];
	char		*aobjstr;
	sa_family_t	af = AF_UNSPEC;
	boolean_t	is_ngz = (iph->ih_zoneid != GLOBAL_ZONEID);
	char		*db_if;
	char		ipmpif[LIFNAMSIZ];

	bzero(ipmpif, sizeof (ipmpif));
	(void) strlcpy(newifname, ifname, sizeof (newifname));
	/*
	 * First plumb the given interface and then apply all the persistent
	 * interface properties and then instantiate any persistent addresses
	 * objects on that interface.
	 */
	for (nvp = nvlist_next_nvpair(ifnvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(ifnvl, nvp)) {
		if (nvpair_value_nvlist(nvp, &nvl) != 0)
			continue;
		if (nvlist_lookup_string(nvl, IPADM_NVP_IFNAME, &db_if) != 0 ||
		    strcmp(db_if, ifname) != 0)
			continue;
		if (nvlist_lookup_string(nvl, IPADM_NVP_FAMILY, &afstr) == 0) {
			/*
			 * If `ifname' is an underlying interface, look for
			 * the IPMP interface now. It is better done now
			 * than later after many addresses have been added,
			 * to avoid migrating the data addresses.
			 */
			status = i_ipadm_ipmpif_from_nvl(ifnvl, ifname,
			    ipmpif, LIFNAMSIZ);
			if (status == IPADM_OBJ_NOTFOUND)
				ipmpif[0] = '\0';
			else if (status != IPADM_SUCCESS)
				continue;
			status = i_ipadm_init_ifobj(iph, ifname, ipmpif, nvl);

			if (is_ngz)
				af = atoi(afstr);
		} else if (nvlist_lookup_string(nvl, IPADM_NVP_AOBJNAME,
		    &aobjstr) == 0) {
			/*
			 * For a static address, we need to search for
			 * the prefixlen in the nvlist `ifnvl'.
			 */
			if (nvlist_exists(nvl, IPADM_NVP_IPV4ADDR) ||
			    nvlist_exists(nvl, IPADM_NVP_IPV6ADDR)) {
				status = i_ipadm_merge_prefixlen_from_nvl(ifnvl,
				    nvl, aobjstr);
				if (status != IPADM_SUCCESS)
					continue;
			}
			status = i_ipadm_init_addrobj(iph, nvl);
			/*
			 * If this address is in use on some other interface,
			 * we want to record an error to be returned as
			 * a soft error and continue processing the rest of
			 * the addresses.
			 */
			if (status == IPADM_CANNOT_ASSIGN_ADDR) {
				ret_status = IPADM_IF_NOT_FULLY_ENABLED;
				status = IPADM_SUCCESS;
			} else if (status == IPADM_DHCP_INVALID_IF) {
				ret_status = IPADM_IPMPIF_DHCP_NOT_ENABLED;
				status = IPADM_SUCCESS;
			}
		} else if (nvlist_exists(nvl, IPADM_NVP_PROTONAME)) {
			status = i_ipadm_init_ifprop(iph, nvl);
		}
		if (status != IPADM_SUCCESS)
			return (status);
	}

	if (ipmpif[0] != '\0') {
		status = i_ipadm_add_ipmp_bringup_underif(iph, ifname,
		    AF_UNSPEC);
		if (status != IPADM_SUCCESS)
			return (status);
	}

	if (is_ngz && af != AF_UNSPEC)
		ret_status = ipadm_init_net_from_gz(iph, newifname, NULL);
	return (ret_status);
}

/*
 * Recreates the interface in `ifname' with the information provided in the
 * nvlist `nvl'. If an IPMP interface is provided in `ipmpif', `ifname' is
 * added to the IPMP group.
 */
ipadm_status_t
i_ipadm_init_ifobj(ipadm_handle_t iph, const char *ifname, const char *ipmpif,
    nvlist_t *nvl)
{
	ipadm_status_t		status;
	char			*class_str;
	char			*afstr;
	sa_family_t		af;
	ipadm_if_class_t	class;
	struct lifreq		lifr;
	int			err;
	int			sock;

	err = nvlist_lookup_string(nvl, IPADM_NVP_IFCLASS, &class_str);
	assert(err == 0);
	class = atoi(class_str);

	err = nvlist_lookup_string(nvl, IPADM_NVP_FAMILY, &afstr);
	assert(err == 0);
	af = atoi(afstr);
	status = i_ipadm_plumb_if(iph, (char *)ifname, af, class,
	    IPADM_OPT_ACTIVE);
	/*
	 * If the interface is already plumbed, we should
	 * ignore this error because there might be address
	 * objects on that interface that need to be enabled again.
	 */
	if (status == IPADM_IF_EXISTS)
		status = IPADM_SUCCESS;
	if (status != IPADM_SUCCESS)
		return (status);
	/*
	 * Put it in the IPMP group, if an IPMP interface is found in the
	 * persistent config.
	 */
	if (ipmpif[0] != '\0') {
		/*
		 * The interface is an underlying interface for the
		 * IPMP interface in `ipmpif'.
		 */
		if (!ipadm_if_enabled(iph, ipmpif, AF_UNSPEC))
			return (IPADM_IPMPIF_NOT_ENABLED);
		bzero(&lifr, sizeof (lifr));
		(void) strlcpy(lifr.lifr_name, ipmpif, sizeof (lifr.lifr_name));
		sock = IPADM_SOCK(iph, af);
		if (ioctl(sock, SIOCGLIFGROUPNAME, &lifr) < 0)
			return (ipadm_errno2status(errno));
		status = i_ipadm_add_ipmp(iph, lifr.lifr_groupname, ipmpif,
		    ifname, IPADM_OPT_ACTIVE);
		if (status != IPADM_SUCCESS)
			return (status);
	}
	return (IPADM_SUCCESS);
}

/*
 * Look up the IPMP interface information from `invl' and copy it to `ipmpif'.
 * IPADM_OBJ_NOTFOUND is returned when one is not found.
 */
static ipadm_status_t
i_ipadm_ipmpif_from_nvl(nvlist_t *invl, const char *underif, char *ipmpif,
    size_t len)
{
	int		err;
	nvpair_t	*nvp;
	nvlist_t	*tnvl;
	char		*db_ifname, *under;

	for (nvp = nvlist_next_nvpair(invl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(invl, nvp)) {
		if (nvpair_value_nvlist(nvp, &tnvl) == 0 &&
		    nvlist_lookup_string(tnvl, IPADM_NVP_UNDERIF,
		    &under) == 0 && strcmp(under, underif) == 0) {
			err = nvlist_lookup_string(tnvl, IPADM_NVP_IFNAME,
			    &db_ifname);
			if (err != 0)
				return (ipadm_errno2status(err));
			/*
			 * `ifname' is an underlying interface for `db_ifname'.
			 */
			(void) strlcpy(ipmpif, db_ifname, len);
			return (IPADM_SUCCESS);
		}
	}
	return (IPADM_OBJ_NOTFOUND);
}

/*
 * Returns B_FALSE if
 * (1) `ifname' is NULL or has no string or has a string of invalid length
 * (2) ifname is a logical interface and IH_LEGACY is not set, or
 */
boolean_t
i_ipadm_validate_ifname(ipadm_handle_t iph, const char *ifname)
{
	ifspec_t ifsp;

	if (!ifparse_ifspec(ifname, &ifsp))
		return (B_FALSE);
	if (ifsp.ifsp_lunvalid)
		return (ifsp.ifsp_lun > 0 && (iph->ih_flags & IH_LEGACY));
	return (B_TRUE);
}

ipadm_status_t
i_ipadm_validate_ifcreate(ipadm_handle_t iph, const char *ifname,
    sa_family_t af, uint32_t flags)
{
	char		newifname[LIFNAMSIZ];
	boolean_t	legacy = ((iph->ih_flags & IH_LEGACY) != 0);

	/* Check for the required authorization */
	if (!ipadm_check_auth())
		return (IPADM_INSUFF_AUTH);

	if ((flags & IPADM_OPT_ACTIVE) == 0 ||
	    (flags & ~(IPADM_COMMON_OPT_MASK|IPADM_OPT_GENPPA|
	    IPADM_OPT_NWAM_OVERRIDE))) {
		return (IPADM_INVALID_ARG);
	}
	if (legacy && af == AF_UNSPEC)
		return (IPADM_INVALID_ARG);
	if (flags & IPADM_OPT_GENPPA) {
		/*
		 * Return error if the interface name already has a ppa.
		 * If no ppa is provided, make an interface name with
		 * a dummy ppa to be fed to i_ipadm_validate_ifname()
		 * later in this function, to avoid failure from
		 * ifparse_spec().
		 */
		if (isdigit(ifname[strlen(ifname) - 1]) ||
		    snprintf(newifname, LIFNAMSIZ, "%s0", ifname) >=
		    LIFNAMSIZ)
			return (IPADM_INVALID_ARG);
	} else if (strlcpy(newifname, ifname, LIFNAMSIZ) >= LIFNAMSIZ) {
		return (IPADM_INVALID_ARG);
	}

	if (!i_ipadm_validate_ifname(iph, newifname))
		return (IPADM_INVALID_IFNAME);

	return (IPADM_SUCCESS);
}

/*
 * Get the persistent flags for the interface `ifname'.
 */
uint_t
i_ipadm_get_pflags(ipadm_handle_t iph, const char *ifname)
{
	ipadm_status_t	status;
	list_t		ifinfo;
	ipadm_if_info_t	*ifp;
	uint_t	pflags = 0;

	status = i_ipadm_persist_if_info(iph, ifname, &ifinfo);
	if (status == IPADM_SUCCESS) {
		ifp = list_head(&ifinfo);
		pflags = ifp->ifi_pflags;
		ipadm_free_if_info(&ifinfo);
	}
	return (pflags);
}

/*
 * Wrapper for sending a non-transparent I_STR ioctl().
 * Returns: Result from ioctl().
 */
int
i_ipadm_strioctl(int s, int cmd, char *buf, int buflen)
{
	struct strioctl ioc;

	(void) memset(&ioc, 0, sizeof (ioc));
	ioc.ic_cmd = cmd;
	ioc.ic_timout = 0;
	ioc.ic_len = buflen;
	ioc.ic_dp = buf;

	return (ioctl(s, I_STR, (char *)&ioc));
}

/*
 * Make a door call to the server and checks if the door call succeeded or not.
 * `is_varsize' specifies that the data returned by ipmgmtd daemon is of
 * variable size and door will allocate buffer using mmap(). In such cases
 * we re-allocate the required memory and assign it to `rbufp', copy the data
 * to `rbufp' and then call munmap() (see below).
 *
 * It also checks to see if the server side procedure ran successfully by
 * checking for ir_err. Therefore, for some callers who just care about the
 * return status, should set `rbufp' to NULL and set `rsize' to 0.
 */
static int
i_ipadm_common_door_call(ipadm_handle_t iph, void *arg, size_t asize,
    void **rbufp, size_t rsize, boolean_t is_varsize)
{
	door_arg_t	darg;
	int		err;
	ipmgmt_retval_t	rval, *rvalp;
	boolean_t	reopen = B_FALSE;

	if (rbufp == NULL) {
		rvalp = &rval;
		rbufp = (void **)&rvalp;
		rsize = sizeof (rval);
	}

	darg.data_ptr = arg;
	darg.data_size = asize;
	darg.desc_ptr = NULL;
	darg.desc_num = 0;
	darg.rbuf = *rbufp;
	darg.rsize = rsize;

reopen:
	(void) pthread_mutex_lock(&iph->ih_lock);
	/* The door descriptor is opened if it isn't already */
	if (iph->ih_door_fd == -1) {
		if ((iph->ih_door_fd = open(IPMGMT_DOOR, O_RDONLY)) < 0) {
			err = errno;
			(void) pthread_mutex_unlock(&iph->ih_lock);
			return (err);
		}
	}
	(void) pthread_mutex_unlock(&iph->ih_lock);

	if (door_call(iph->ih_door_fd, &darg) == -1) {
		/*
		 * Stale door descriptor is possible if ipmgmtd was restarted
		 * since last ih_door_fd was opened, so try re-opening door
		 * descriptor.
		 */
		if (!reopen && errno == EBADF) {
			(void) close(iph->ih_door_fd);
			iph->ih_door_fd = -1;
			reopen = B_TRUE;
			goto reopen;
		}
		return (errno);
	}
	err = ((ipmgmt_retval_t *)(void *)(darg.rbuf))->ir_err;
	if (darg.rbuf != *rbufp) {
		/*
		 * if the caller is expecting the result to fit in specified
		 * buffer then return failure.
		 */
		if (!is_varsize)
			err = EBADE;
		/*
		 * The size of the buffer `*rbufp' was not big enough
		 * and the door itself allocated buffer, for us. We will
		 * hit this, on several occasion as for some cases
		 * we cannot predict the size of the return structure.
		 * Reallocate the buffer `*rbufp' and memcpy() the contents
		 * to new buffer.
		 */
		if (err == 0) {
			void *newp;

			/* allocated memory will be freed by the caller */
			if ((newp = realloc(*rbufp, darg.rsize)) == NULL) {
				err = ENOMEM;
			} else {
				*rbufp = newp;
				(void) memcpy(*rbufp, darg.rbuf, darg.rsize);
			}
		}
		/* munmap() the door buffer */
		(void) munmap(darg.rbuf, darg.rsize);
	} else {
		if (darg.rsize != rsize)
			err = EBADE;
	}
	return (err);
}

/*
 * Makes a door call to the server and `rbufp' is not re-allocated. If
 * the data returned from the server can't be accomodated in `rbufp'
 * that is of size `rsize' then an error will be returned.
 */
int
ipadm_door_call(ipadm_handle_t iph, void *arg, size_t asize,
    void *rbufp, size_t rsize)
{
	return (i_ipadm_common_door_call(iph, arg, asize,
	    (rbufp == NULL ? NULL : &rbufp), rsize, B_FALSE));
}

/*
 * Makes a door call to the server and `rbufp' always points to a
 * re-allocated memory and should be freed by the caller. This should be
 * used by callers who are not sure of the size of the data returned by
 * the server.
 */
int
ipadm_door_dyncall(ipadm_handle_t iph, void *arg, size_t asize,
    void **rbufp, size_t rsize)
{
	return (i_ipadm_common_door_call(iph, arg, asize, rbufp, rsize,
	    B_TRUE));
}
