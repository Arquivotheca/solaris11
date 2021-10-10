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
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Print processor information
 * pls refer SMBIOS 2.4 spec
 */

#include <stdlib.h>
#include <stdio.h>
#include "libddudev.h"
#include "dmi.h"
#include "processor_info.h"

/*
 * Print processor socket information from SMBIOS
 * Get processor table, the table type is 4.
 *
 * smb_node: point to processor table
 * index: record number of processor socket
 */
void
print_processor_node_info(smbios_node_t smb_node, int index)
{
	psocket_info_t	info;
	char 		*str;

	info = (psocket_info_t)smb_node->info;

	PRINTF(" Processor %d:\n", index);

	PRINTF("  Processor Socket Type:");
	str = smb_get_node_str(smb_node, info->socket_type);
	if (str) {
		PRINTF("%s", str);
	}

	PRINTF("\n");

	/* If cur_speed is 0, this socket not installed processor */
	if (info->cur_speed == 0) {
		PRINTF("  Processor Manufacturer:\n");

		PRINTF("  Current Voltage:\n");

		PRINTF("  External Clock:\n");

		PRINTF("  Max Speed:\n");

		PRINTF("  Current Speed:\n");

		return;
	}

	PRINTF("  Processor Manufacturer:");
	str = smb_get_node_str(smb_node, info->processor_manu);
	if (str) {
		PRINTF("%s", str);
	}
	PRINTF("\n");

	/* If voltage bit7 is set, get processor voltage */
	if (info->voltage & (1 << 7)) {
		PRINTF("  Current Voltage:%.1fV\n",
		    (float)(info->voltage - 0x80)/10);
	} else {
		PRINTF("  Current Voltage:\n");
	}

	PRINTF("  External Clock:%uMHZ\n", info->external_clock);

	PRINTF("  Max Speed:%uMHZ\n", info->max_speed);

	PRINTF("  Current Speed:%uMHZ\n", info->cur_speed);
}

/*
 * Print system processor socket information from SMBIOS
 * Lookup each processor table, and print information
 *
 * smb_hdl: point to SMBIOS structure
 */
void
print_processor_info(smbios_hdl_t smb_hdl)
{
	smbios_node_t		node;
	int			i;

	node = smb_get_node_by_type(smb_hdl, NULL, PROCESSOR_INFO);

	if (node == NULL) {
		return;
	}

	i = 0;

	while (node != NULL) {
		if (i) {
			PRINTF("\n");
		}
		print_processor_node_info(node, i);
		node = smb_get_node_by_type(smb_hdl, node, PROCESSOR_INFO);
		i++;
	}
}
