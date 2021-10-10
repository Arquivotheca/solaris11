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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <libipadm.h>
#include <libdladm.h>
#include <libdllink.h>
#include "ilomconfig.h"
#include <libnvpair.h>

extern disp_options_t dopt;

#define	LIFC_DEFAULT	(LIFC_NOXMIT | LIFC_TEMPORARY | LIFC_ALLZONES)

static dladm_handle_t dld_handle;

static char dld_intf_name[DLPI_LINKNAME_MAX];

#define	INTF_NAME_UNKNOWN "unknown"

static void
v4sockaddr2str(struct sockaddr *sp, char *buf, uint_t bufsize)
{
	struct sockaddr_in *v4_sp;

	if (sp->sa_family == AF_INET) {
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		v4_sp = (struct sockaddr_in *)sp;
		(void) inet_ntop(AF_INET, (void *)&v4_sp->sin_addr, buf,
		    bufsize);
	} else {
		(void) strlcpy(buf, "?", bufsize);
	}

}

/* dladm_walk callback */
static int
find_macaddr_match(const char *name, void *data)
{
	char	dladm_mac[DLADM_PROP_VAL_MAX];
	dladm_status_t status;
	char	*valptr[1];
	uint_t	valcnt = 1;
	datalink_id_t linkid;

	if (dladm_name2info(dld_handle, name, &linkid, NULL, NULL, NULL) !=
	    DLADM_STATUS_OK) {
		return (DLADM_WALK_CONTINUE);
	}

	/* Get mac address link property and compare to mac passed in */
	valptr[0] = dladm_mac;
	status = dladm_get_linkprop(dld_handle, linkid, DLADM_PROP_VAL_CURRENT,
	    "mac-address", (char **)valptr, &valcnt);
	if (status == DLADM_STATUS_OK) {
		if (strncmp(dladm_mac, (char *)data, DLPI_LINKNAME_MAX) == 0) {

			(void) strncpy(dld_intf_name, name,
			    sizeof (dld_intf_name));

			return (DLADM_WALK_TERMINATE);
		}
	}

	return (DLADM_WALK_CONTINUE);
}

static void
non_zero_pad_octet(char *in, char *out, int olen)  {
	int octet;

	octet = strtoul(in, NULL, 16);
	(void) snprintf(out, olen, "%x", octet);
}

/*
 * get_interface_name()
 *
 * Get the interface name from MAC Address
 */

static cli_errno_t
get_interface_name(char *macaddress) {
	int i;
	char *tmpptr;
	char tmpmac[50];
	char intf_mac[50];
	char octet_str[5];

	/*
	 * In order to check mac address, the address must NOT
	 * be zero-padded, since that is how dladm returns it.
	 * Break apart address and recreate it as non-zero padded.
	 */
	(void) strncpy(tmpmac, macaddress, sizeof (tmpmac));
	tmpptr = strtok(tmpmac, ":");

	if (strlen(tmpptr) > 2) {
		(void) debug_log("Invalid mac address detected\n");
	}

	(void) memset(intf_mac, 0, sizeof (intf_mac));

	for (i = 0; tmpptr && (i < 6); i++) {

		if (i > 0)
			(void) strncat(intf_mac, ":", sizeof (intf_mac));

		non_zero_pad_octet(tmpptr, octet_str,
		    sizeof (octet_str));
		(void) strncat(intf_mac, octet_str, sizeof (intf_mac));

		tmpptr = strtok(NULL, ":");
	}

	if (dladm_open(&dld_handle) != DLADM_STATUS_OK) {
		(void) debug_log("dladm open failed\n");
		return (SSM_CLI_INTERNAL_ERROR);
	}

	(void) strncpy(dld_intf_name, INTF_NAME_UNKNOWN,
	    sizeof (dld_intf_name));

	/*
	 * Walk all data links with call back
	 * Run command equivalent to find interface name:
	 * "dladm show-linkprop -o link,value -p mac-address | grep <mac>"
	 */
	if (dladm_walk(find_macaddr_match, dld_handle, intf_mac,
			DATALINK_CLASS_ALL, DATALINK_ANY_MEDIATYPE,
			DLADM_OPT_ACTIVE) != DLADM_STATUS_OK) {
		(void) debug_log("dladm walk failed\n");
		return (SSM_CLI_INTERNAL_ERROR);
	}

	(void) dladm_close(dld_handle);

	(void) debug_log("Interface name is %s\n", dld_intf_name);

	/* Verify name was set */
	if (strcmp(dld_intf_name, INTF_NAME_UNKNOWN) == 0) {
		return (SSM_CLI_INTERNAL_ERROR);
	}

	return (SSM_CLI_OK);
}

/*
 * get_host_interconnect()
 *
 * IN: macaddress - macaddress of the host-side of interconnect
 * OUT: hostipaddress - current IP address of the host-side of interconnect
 *	  ipbuflen - length of buffer for returning the hostipaddress
 *	  hostnetmask - current netmask of the host-side interconnect interface
 *	  nmbuflen - length of buffer for returning the hostnetmask
 *
 * Use the passed in MAC address to look up the interface.  For this network
 * interface, return the IP address and netmask.
 *
 */

cli_errno_t
get_host_interconnect(char *macaddress, char *hostipaddress,
	int ipbuflen, char *hostnetmask, int nmbuflen) {
	char interface_obj[DLPI_LINKNAME_MAX+4];
	cli_errno_t ret;
	ipadm_handle_t	iph = NULL;
	ipadm_status_t status;
	ipadm_addr_info_t *ainfo = NULL;
	ipadm_addr_info_t *ptr;
	boolean_t found = B_FALSE;

	if ((ret = get_interface_name(macaddress)) != SSM_CLI_OK) {
		return (ret);
	}

	/* Create address object name */
	(void) snprintf(interface_obj, sizeof (interface_obj),
		"%s/v4", dld_intf_name);

	if ((status = ipadm_open(&iph, 0)) != IPADM_SUCCESS) {
		(void) debug_log("Error %s opening ipadm handle\n",
			ipadm_status2str(status));
		return (SSM_CLI_INTERNAL_ERROR);
	}

	/* Get address info */
	status = ipadm_addr_info(iph, NULL,
		&ainfo, 0, 0);
	if (status != IPADM_SUCCESS) {
		(void) debug_log("Error getting ipadm addr info %s\n",
			ipadm_status2str(status));
		ipadm_close(iph);
		return (SSM_CLI_INTERNAL_ERROR);
	} else {
		if (ainfo == NULL) {
			(void) debug_log("NULL address info from ipadm\n");
		} else {
			for (ptr = ainfo; ptr != NULL; ptr = IA_NEXT(ptr)) {
				if (strcmp(ptr->ia_aobjname,
						interface_obj) == 0) {
					/* Found address, convert to string */
					v4sockaddr2str(ptr->ia_ifa.ifa_addr,
						hostipaddress, ipbuflen);

					v4sockaddr2str(ptr->ia_ifa.ifa_netmask,
						hostnetmask, nmbuflen);
					found = B_TRUE;
					break;
				}
			}
		}
	}

	if (ainfo)
		ipadm_free_addr_info(ainfo);

	ipadm_close(iph);

	if (found == B_TRUE)
		return (SSM_CLI_OK);
	else
		return (SSM_CLI_INTERNAL_ERROR);

}


/*
 * set_host_interconnect()
 *
 * IN: macaddress - macaddress of the host-side of the ILOM-host interconnect
 * OUT: hostipaddress - IP address of the host-side interconnect interface
 *	  hostnetmask - Netmask of the host-side interconnect interface
 *
 * Use the passed in MAC address to look up the interface.  For this
 * network interface, set the IP address and netmask in a persistent
 * way (across reboots) and bring the interface up.
 *
 */

cli_errno_t
set_host_interconnect(char *macaddress, char *hostipaddress,
	char *hostnetmask) {
	char interface_obj[IPADM_AOBJSIZ];
	char addrstr[32];
	cli_errno_t ret;
	struct in_addr mask;
	int prefix;
	ipadm_status_t	status;
	uint32_t flags;
	ipadm_addrobj_t	ipaddr = NULL;
	ipadm_handle_t	iph = NULL;

	if ((ret = get_interface_name(macaddress)) != SSM_CLI_OK) {
		return (ret);
	}

	if ((status = ipadm_open(&iph, 0)) != IPADM_SUCCESS) {
		(void) debug_log("Error %s opening ipadm handle\n",
			ipadm_status2str(status));
		return (SSM_CLI_INTERNAL_ERROR);
	}

	/* Create address object name */
	(void) snprintf(interface_obj, sizeof (interface_obj),
		"%s/v4", dld_intf_name);

	flags = IPADM_OPT_PERSIST|IPADM_OPT_ACTIVE;

	/* Configure the interface for immediate use */
	status = ipadm_create_ip(iph, dld_intf_name, AF_UNSPEC, flags);
	if (status != IPADM_SUCCESS) {
		(void) debug_log("Cannot create interface %s: %s\n",
			dld_intf_name, ipadm_status2str(status));
		if ((status !=  IPADM_IF_EXISTS) &&
			(status != IPADM_OP_DISABLE_OBJ)) {
			ipadm_close(iph);
			return (SSM_CLI_INTERNAL_ERROR);
		} else {
			/*
			 * We may get IPADM_OP_DISABLE_OBJ error if using
			 * an ncp other than defaultFixed or IPADM_IF_EXISTS
			 * if using defaultFixed.  In order to ensure the
			 * interface gets set up properly, delete the
			 * interface and recreate it.
			 */
			status = ipadm_delete_ip(iph, dld_intf_name,
			    AF_UNSPEC, flags);
			if (status != IPADM_SUCCESS) {
				(void) debug_log(
					"Cannot delete interface %s: %s\n",
					dld_intf_name,
					ipadm_status2str(status));
			}

			status = ipadm_create_ip(iph, dld_intf_name,
			    AF_UNSPEC, flags);
			if (status != IPADM_SUCCESS) {
				(void) debug_log(
					"Cannot recreate interface %s: %s\n",
					dld_intf_name,
					ipadm_status2str(status));
				ipadm_close(iph);
				return (SSM_CLI_INTERNAL_ERROR);
			}
		}
	}

	/* Convert netmask to prefix */
	(void) inet_pton(AF_INET, hostnetmask, &mask);

	for (prefix = 0; mask.s_addr;  mask.s_addr >>= 1)
	{
		prefix +=  mask.s_addr & 1;
	}

	/*
	 * Create address, run command:
	 * "ipadm create-addr -T static -a addr/mask <addrobj>",
	 */
	status = ipadm_create_addrobj(IPADM_ADDR_STATIC,
		interface_obj, &ipaddr);
	if (status != IPADM_SUCCESS) {
		(void) debug_log("Cannot create addrobj for %s : %s\n",
			interface_obj, ipadm_status2str(status));
		if (status != IPADM_ADDROBJ_EXISTS) {
			ipadm_close(iph);
			return (SSM_CLI_INTERNAL_ERROR);
		}
	}

	(void) snprintf(addrstr, sizeof (addrstr), "%s/%d", hostipaddress,
		prefix);

	status = ipadm_set_addr(ipaddr, addrstr, AF_UNSPEC);
	if (status != IPADM_SUCCESS) {
		(void) debug_log("Cannot set address for %s : %s\n",
			interface_obj, ipadm_status2str(status));
		ipadm_close(iph);
		return (SSM_CLI_INTERNAL_ERROR);
	}

	flags = IPADM_OPT_PERSIST|IPADM_OPT_ACTIVE|IPADM_OPT_UP;

	status = ipadm_create_addr(iph, ipaddr, flags);
	if (status != IPADM_SUCCESS) {
		(void) debug_log("Cannot create address for %s : %s\n",
			interface_obj, ipadm_status2str(status));
		if (status != IPADM_ADDROBJ_EXISTS)
		{
			ipadm_close(iph);
			return (SSM_CLI_INTERNAL_ERROR);
		}
	}

	ipadm_close(iph);

	(void) debug_log("%s should be up\n", dld_intf_name);

	return (SSM_CLI_OK);
}


/*
 * ping_ilom()
 *
 * Ping ILOM using the internal LAN network address.  Return SSM_CLI_OK
 * if ILOM can be reached.  Otherwise, return ILOM_NOT_REACHABLE_NETWORK.
 *
 */

cli_errno_t
ping_ilom(char *ipaddress) {
	char command[128];
	int sysret = -1;

	(void) snprintf(command, sizeof (command),
		"ping -c 2 %s > /dev/null", ipaddress);
	sysret = system(command);

	if (sysret == -1) {
		return (SSM_CLI_INTERNAL_ERROR);
	} else if (sysret != 0) {
		return (ILOM_NOT_REACHABLE_NETWORK);
	} else {
		(void) iprintf(
			"Host-to-ILOM interconnect successfully configured.\n");
		return (SSM_CLI_OK);
	}
}

/*
 * unset_host_interconnect()
 *
 * IN: macaddress - macaddress of the host-side of the ILOM-host
 * interconnect
 *
 */

cli_errno_t
unset_host_interconnect(char *macaddress) {
	cli_errno_t ret;
	ipadm_status_t status;
	ipadm_handle_t	iph = NULL;
	uint32_t	flags = IPADM_OPT_ACTIVE|IPADM_OPT_PERSIST;

	if ((ret = get_interface_name(macaddress)) != SSM_CLI_OK) {
		return (ret);
	}

	if ((status = ipadm_open(&iph, 0)) != IPADM_SUCCESS) {
		(void) debug_log("Error %s opening ipadm handle\n",
			ipadm_status2str(status));
		return (SSM_CLI_INTERNAL_ERROR);
	}

	/*
	 * Run command:
	 * "ipadm delete-ip <interface>"
	 *
	 */

	status = ipadm_delete_ip(iph, dld_intf_name, AF_UNSPEC, flags);
	if (status != IPADM_SUCCESS) {
		(void) debug_log("cannot delete interface %s: %s\n",
		    dld_intf_name, ipadm_status2str(status));
		ipadm_close(iph);
		return (SSM_CLI_INTERNAL_ERROR);
	}

	ipadm_close(iph);

	(void) iprintf("Host-to-ILOM interconnect disabled.\n");

	return (SSM_CLI_OK);
}
