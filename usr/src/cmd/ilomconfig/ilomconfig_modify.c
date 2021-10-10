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
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "ilomconfig.h"
#include <libnvpair.h>
#include "cli_common_log.h"

extern disp_options_t dopt;

static int
is_valid_ipaddress_str(char *address) {
	struct in_addr ipv4;

	if ((inet_pton(AF_INET, address, &ipv4) <= 0)) {
		return (0);
	} else {
		return (1);
	}
}

cli_errno_t
modify_interconnect(nvlist_t *option_list, int enable) {
	char *spipaddress = NULL;
	char *hostipaddress = NULL;
	char *netmask = NULL;
	char valuestr[16];
	cli_errno_t ret = SSM_CLI_OK;
	char state[16];
	char hostmacaddress[32];
	char currenthostip[32];
	char currentilomip[32];
	char currentnetmask[32];
	struct in_addr binip;
	int ip_net_changed = 0;
	char command[128];

	/* Check to make sure this feature is supported on ILOM */
	if (does_target_exist("/SP/network/interconnect state") == 0)
		return (SSM_CLI_SUBCOMMAND_NOT_SUPPORTED);

	/* Lookup what user entered */
	(void) nvlist_lookup_string(option_list, "ipaddress",
		&spipaddress);
	(void) nvlist_lookup_string(option_list, "netmask",
		&netmask);
	(void) nvlist_lookup_string(option_list, "hostipaddress",
		&hostipaddress);

	if (!enable && !spipaddress &&
		!netmask && !hostipaddress) {
		if (enable) usage(OPT_ENABLE, OPT_INTERCONNECT);
		else usage(OPT_MODIFY, OPT_INTERCONNECT);
		return (ILOM_MUST_SPECIFY_OPTION);
	}

	/* Get current ILOM settings */
	ret = get_property_ilom("/SP/network/interconnect",
		"ipaddress", currentilomip, sizeof (currentilomip));
	if (ret != 0)
		return (ret);
	ret = get_property_ilom("/SP/network/interconnect",
		"ipnetmask", currentnetmask, sizeof (currentnetmask));
	if (ret != 0)
		return (ret);
	ret = get_property_ilom("/SP/network/interconnect",
		"hostmacaddress", hostmacaddress, sizeof (hostmacaddress));
	if (ret != 0)
		return (ret);
	ret = get_property_ilom(
		"/SP/network/interconnect state", "state", state,
		sizeof (state));
	if (ret != 0)
		return (ret);

	/* Handle enable case where no options are passed */
	if (enable && (!spipaddress ||
			!netmask || !hostipaddress)) {

		/* Choose defaults based on current ILOM settings */
		(void) inet_pton(AF_INET, currentilomip, &binip);

		/* Choose the IP address that is one greater */
		binip.s_addr = htonl(ntohl(binip.s_addr) + 1);
		hostipaddress = inet_ntoa(binip);
	}

	/* Interconnect must already be enabled before modifying it */
	if (!enable) {

		if (strcmp(state, "disabled") == 0) {
			return (ILOM_INTERCONNECT_DISABLED);
		}
	}

	if (spipaddress) {
		if (is_valid_ipaddress_str(spipaddress) == 0) {
			return (ILOM_INVALID_IPADDRESS_VALUE);
		}
		ret = set_property("/SP/network",
			"interconnect", "pendingipaddress", spipaddress, 0);
		if (ret != 0)
			return (ret);
	}
	if (netmask) {
		ret = set_property("/SP/network",
			"interconnect", "pendingipnetmask", netmask, 0);
		if (ret != 0)
			return (ret);
	}

	if (spipaddress || netmask) {
		(void) snprintf(valuestr, 10, "true");
		ret = set_property("/SP/network",
			"interconnect", "commitpending", valuestr, 0);
		if (ret != 0)
			return (ret);

		ip_net_changed = 1;
	}

	if (enable && strcmp(state, "disabled") == 0) {
		(void) debug_log("Enabling interconnect\n");
		(void) snprintf(valuestr, 10, "enabled");
		ret = set_property("/SP/network",
			"interconnect", "state", valuestr, 0);
		if (ret != 0)
			return (ret);
		(void) debug_log("Enable complete\n");
		(void) sleep(2);
	} else {
		(void) debug_log("Interconnect already enabled\n");
	}


	/* Now change the host-side settings */
	if (hostipaddress) {
		if (is_valid_ipaddress_str(hostipaddress) == 0) {
			return (ILOM_INVALID_IPADDRESS_VALUE);
		}

		/*
		 * If a netmask was passed in use it,
		 * otherwise use the current one
		 */
		if (netmask) ret = set_host_interconnect(hostmacaddress,
			hostipaddress, netmask);
		else ret = set_host_interconnect(hostmacaddress,
			hostipaddress, currentnetmask);
		if (ret != 0)
			return (ret);

		if (netmask) {
			(void) iprintf("Set host ipaddress to '%s'\n",
				hostipaddress);
			(void) iprintf("Set host netmask to '%s'\n", netmask);
		} else {
			(void) iprintf("Set host ipaddress to '%s'\n",
				hostipaddress);
		}

		ip_net_changed = 1;

	} else if (netmask) {

		/*
		 * If we are only changing netmask,
		 * look up the current IP address
		 */
		ret = get_host_interconnect(hostmacaddress,
			currenthostip, sizeof (currenthostip),
			currentnetmask, sizeof (currentnetmask));
		if (ret != 0)
			return (ret);

		ret = set_host_interconnect(hostmacaddress,
			currenthostip, netmask);
		if (ret != 0)
			return (ret);

		(void) iprintf("Set host netmask to '%s'\n", netmask);

		ip_net_changed = 1;
	}

	/*
	 * If the IP address or netmask was explicitly
	 * changed, reload any modules that rely on this
	 * interconnect
	 */
	if (ip_net_changed) {
		(void) snprintf(command, sizeof (command),
			"/usr/sbin/fmadm unload ip-transport > /dev/null");
		if (system(command) == -1) {
			(void) debug_log(
			"Failure unloading module after IP address change\n");
		}

		(void) snprintf(command, sizeof (command),
"/usr/sbin/fmadm load /usr/lib/fm/fmd/plugins/ip-transport.so > /dev/null");

		if (system(command) == -1) {
			(void) debug_log(
"Failure loading module after IP address change\n");
		}
	}

	/* Ping ILOM to ensure the interface is up */
	(void) debug_log("Pinging ILOM\n");

	if (!spipaddress) {
		ret = ping_ilom(currentilomip);
	} else {
		ret = ping_ilom(spipaddress);
	}

	return (ret);
}
