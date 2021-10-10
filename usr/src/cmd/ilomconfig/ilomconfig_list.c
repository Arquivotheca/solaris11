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
#include "ilomconfig.h"
#include <libnvpair.h>
#include "cli_common_log.h"

extern disp_options_t dopt;
static sunoem_handle_t *printhandle;
static char last_target[LUAPI_MAX_OBJ_PATH_LEN];

static cli_errno_t
print_ilomversion() {
	int j;
	int printed = 0;

	/* Retry up to five times to workaround sunoem bugs */
	for (j = 0; j < 5 && !printed; j++) {
		if (printhandle) {
			sunoem_cleanup(printhandle);
			printhandle = NULL;
		}
		if (sunoem_init(&printhandle, "version") == -1) {
			return (ILOM_CANNOT_CONNECT_BMC);
		}
		if (sunoem_print_ilomversion(printhandle)) printed = 1;
	}

	return (SSM_CLI_OK);
}

static cli_errno_t
print_property(char *target, char *property, char *label, int isrequired) {
	char command[LUAPI_MAX_OBJ_PATH_LEN];
	int i, j;
	int printed = 0;

	/* Retry up to five times to workaround sunoem bugs */
	for (i = 0; i < 5 && !printed; i++) {
		if (strcmp(target, last_target) != 0 || i > 0) {
			if (printhandle) {
				sunoem_cleanup(printhandle);
				printhandle = NULL;
			}

			(void) snprintf(command, LUAPI_MAX_OBJ_PATH_LEN,
				"show %s", target);
			if (sunoem_init(&printhandle, command) == -1) {
				(void) debug_log(
				"print_property sunoem init failed %d!\n", i);
				continue;
				}
			(void) debug_log(
"print_property sunoem success %s %s %d\n", target, property, i);
			sunoem_parse_properties(printhandle);
			(void) strncpy(last_target, target,
				LUAPI_MAX_OBJ_PATH_LEN);
		}
		for (j = 0; j < printhandle->numprops; j++) {
			if (strcmp(printhandle->properties[j].property,
					property)
				== 0) {
				if (label) (void) printf("%s: %s\n",
					label,
					printhandle->properties[j].value);
				else {
					(void) printf("%s\n",
					printhandle->properties[j].value);
				}
				printed = 1;
				break;
			}
		}
	}
	if (isrequired && !printed) {
		(void) debug_log("Error getting target %s, prop %s",
			target ? target : "",
			property ? property : "");
		return (ILOM_CANNOT_CONNECT_BMC);
	}

	return (SSM_CLI_OK);
}

static void
print_underline(char c, int len) {
	int i;
	for (i = 0; i < len; i++) (void) printf("%c", c);
	(void) printf("\n");
}


cli_errno_t
list_systemsummary() {
	cli_errno_t ret = SSM_CLI_OK;
	int headerlen = 0;

	headerlen = printf("System Summary\n");
	print_underline('=', headerlen-1);

	ret = print_property("/SYS", "product_name",
		"Product Name", 1);
	if (ret != SSM_CLI_OK)
		return (ret);
	ret = print_property("/SYS", "product_part_number",
		"Product Part Number", 1);
	if (ret != SSM_CLI_OK)
		return (ret);
	ret = print_property("/SYS", "product_serial_number",
		"Product Serial Number", 1);
	if (ret != SSM_CLI_OK)
		return (ret);
	ret = print_property("/SYS", "product_manufacturer",
		"Product Manufacturer", 1);
	if (ret != SSM_CLI_OK)
		return (ret);
	ret = print_property("/SYS/MB/BIOS", "fru_version",
		"BIOS Version", 0);
	if (ret != SSM_CLI_OK)
		return (ret);
	ret = print_property("/SYS/BIOS", "fru_version",
		"BIOS Version", 0);
	if (ret != SSM_CLI_OK)
		return (ret);
	ret = print_property("/SP", "hostname", "ILOM Hostname", 1);
	if (ret != SSM_CLI_OK)
		return (ret);
	ret = print_property("/SP/clock", "uptime", "Uptime", 0);
	if (ret != SSM_CLI_OK)
		return (ret);
	ret = print_property("/SP/network", "ipaddress",
		"ILOM IP Address", 1);
	if (ret != SSM_CLI_OK)
		return (ret);
	ret = print_ilomversion();
	if (ret != SSM_CLI_OK)
		return (ret);

	sunoem_cleanup(printhandle);
	printhandle = NULL;

	return (ret);
}

cli_errno_t
list_interconnect() {
	int headerlen = 0;
	cli_errno_t ret = SSM_CLI_OK;
	char hostmacaddress[32];
	char hostipaddress[32];
	char hostnetmask[32];


	/* Check to make sure this feature is supported on ILOM */
	if (does_target_exist("/SP/network/interconnect state") == 0)
		return (SSM_CLI_SUBCOMMAND_NOT_SUPPORTED);

	headerlen = printf("Interconnect\n");
	print_underline('=', headerlen-1);

	ret = print_property("/SP/network/interconnect state", "state",
		"State", 1);
	if (ret != SSM_CLI_OK)
		return (ret);
	ret = print_property("/SP/network/interconnect", "type",
		"Type", 1);
	if (ret != SSM_CLI_OK)
		return (ret);
	ret = print_property("/SP/network/interconnect", "ipaddress",
		"SP Interconnect IP Address", 1);
	if (ret != SSM_CLI_OK)
		return (ret);

	ret = get_property_ilom("/SP/network/interconnect hostmacaddress",
		"hostmacaddress", hostmacaddress, sizeof (hostmacaddress));
	if (ret != 0)
		return (ret);

	ret = get_host_interconnect(hostmacaddress, hostipaddress,
		sizeof (hostipaddress), hostnetmask, sizeof (hostnetmask));
	if (ret != SSM_CLI_OK) {
		(void) iprintf("Host Interconnect IP Address: (none)\n");
	} else {
		(void) iprintf("Host Interconnect IP Address: %s\n",
			hostipaddress);
	}

	ret = print_property("/SP/network/interconnect", "ipnetmask",
		"Interconnect Netmask", 1);
	if (ret != SSM_CLI_OK)
		return (ret);
	ret = print_property("/SP/network/interconnect", "spmacaddress",
		"SP Interconnect MAC Address", 1);
	if (ret != SSM_CLI_OK)
		return (ret);
	ret = print_property("/SP/network/interconnect", "hostmacaddress",
		"Host Interconnect MAC Address", 1);
	if (ret != SSM_CLI_OK)
		return (ret);


	return (SSM_CLI_OK);
}
