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
 * Print motherboard information
 * pls refer SMBIOS 2.4 spec
 */

#include <stdlib.h>
#include <stdio.h>
#include "libddudev.h"
#include "dmi.h"
#include "mb_info.h"

/* Onboard Device Types */
static const char *device_type[] = {
	"Other",
	"Unknown",
	"Video",
	"SCSI Controller",
	"Ethernet",
	"Token Ring",
	"Sound",
	"PATA Controller",
	"SATA Controller",
	"SAS Controller"
};

/*
 * Print Motherboard Onboard devices
 * From SMBIOS check onboard table(table type is 10)
 * and list onboard devices information
 */
void
print_onboard_devs_info(smbios_node_t smb_node)
{
	int		ndev;
	uint8_t 	*info;
	uint8_t		dev;
	uint8_t		type;
	char 		*str;
	int		i;

	PRINTF(" Onboard Devices:");

	info = (uint8_t *)smb_node->info;
	/* Caculate number of devices onboard */
	ndev = (smb_node->header_len - 4) / 2;

	for (i = 0; i < ndev; i++) {
		dev = info[i * 2];

		PRINTF("[");
		type = dev & 0x7f;
		if ((type > 0) && (type < 0xb)) {
			PRINTF("%s", device_type[type - 1]);
		}

		if (dev & 0x80) {
			PRINTF(",Enabled]");
		} else {
			PRINTF(",Disabled]");
		}

		str = smb_get_node_str(smb_node, info[i * 2 + 1]);
		if (str) {
			PRINTF("%s", str);
		}

		if (i < (ndev - 1)) {
			PRINTF("|");
		} else {
			PRINTF("\n");
		}
	}
}

/*
 * Print Motherboard information
 * From SMBIOS check motherboard table(table type is 2)
 * and list motherboard information
 */
void
print_motherboard_info(smbios_hdl_t smb_hdl)
{
	smbios_node_t		node;
	motherboard_info_t	info;
	char 			*str;

	/* Get motherboard table from SMBIOS */
	node = smb_get_node_by_type(smb_hdl, NULL, MOTHERBOARD_INFO);

	if (node == NULL) {
		return;
	}

	info = (motherboard_info_t)node->info;

	PRINTF(" Product:");
	str = smb_get_node_str(node, info->product);
	if (str) {
		PRINTF("%s", str);
	}
	PRINTF("\n");

	PRINTF(" Manufacturer:");
	str = smb_get_node_str(node, info->manu);
	if (str) {
		PRINTF("%s", str);
	}
	PRINTF("\n");

	PRINTF(" Version:");
	str = smb_get_node_str(node, info->version);
	if (str) {
		PRINTF("%s", str);
	}
	PRINTF("\n");

	/* Get onboard table from SMBIOS */
	node = smb_get_node_by_type(smb_hdl, NULL, ONBOARD_DEVICES_INFO);

	if (node) {
		print_onboard_devs_info(node);
	} else {
		PRINTF(" Onboard Devices:\n");
	}
}
