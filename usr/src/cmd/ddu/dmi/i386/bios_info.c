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
 * Print BIOS information
 * pls refer SMBIOS 2.4 spec
 */

#include <stdlib.h>
#include <stdio.h>
#include "libddudev.h"
#include "dmi.h"
#include "bios_info.h"

/*
 * Print BIOS information from SMBIOS
 * Get BIOS table, the table is 0.
 * Get and print BIOS Vendor, Version,
 * Release Date, and Revision Information
 *
 * smb_hdl: point to SMBIOS structure
 */
void
print_bios_info(smbios_hdl_t smb_hdl)
{
	smbios_node_t		node;
	bios_info_t		info;
	char 			*str;

	/* Get BIOS table from SMBIOS, the table type is 0 */
	node = smb_get_node_by_type(smb_hdl, NULL, BIOS_INFO);

	if (node == NULL) {
		/* No bios table found */
		return;
	}

	info = (bios_info_t)node->info;

	PRINTF(" Vendor:");
	/* Get BIOS vendor name */
	str = smb_get_node_str(node, info->vendor);
	if (str) {
		PRINTF("%s", str);
	}
	PRINTF("\n");

	PRINTF(" Version:");
	/* Get BIOS version */
	str = smb_get_node_str(node, info->version);
	if (str) {
		PRINTF("%s", str);
	}
	PRINTF("\n");

	PRINTF(" Release Date:");
	/* Get BIOS release data information */
	str = smb_get_node_str(node, info->release_date);
	if (str) {
		PRINTF("%s", str);
	}
	PRINTF("\n");

	/* Get BIOS Revision information */
	PRINTF(" BIOS Revision:");
	if ((info->bios_major_rev != 0xff) && (info->bios_minor_rev != 0xff)) {
		PRINTF(" BIOS Revision:%u.%u", info->bios_major_rev,
		    info->bios_minor_rev);
	}
	PRINTF("\n");

	PRINTF(" Firmware Revision:");
	if ((info->firm_major_rel != 0xff) && (info->firm_minor_rel != 0xff)) {
		PRINTF(" Firmware Revision:%u.%u", info->firm_major_rel,
		    info->firm_minor_rel);
	}
	PRINTF("\n");
}
