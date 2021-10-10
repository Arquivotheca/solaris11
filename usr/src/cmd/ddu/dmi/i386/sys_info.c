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
 * Print system information
 * pls refer SMBIOS 2.4 spec
 */

#include <stdlib.h>
#include <stdio.h>
#include "libddudev.h"
#include "dmi.h"
#include "sys_info.h"

/*
 * Print system information
 * From SMBIOS check system table(table type is 1)
 * and list system information
 *
 * smb_hdl: point to SMBIOS structure
 */
void
print_system_info(smbios_hdl_t smb_hdl)
{
	smbios_node_t		node;
	system_info_t		info;
	char 			*str;

	/* Get system table from SMBIOS */
	node = smb_get_node_by_type(smb_hdl, NULL, SYSTEM_INFO);

	if (node == NULL) {
		return;
	}

	info = (system_info_t)node->info;

	PRINTF(" Manufacturer:");
	str = smb_get_node_str(node, info->manu);
	if (str) {
		PRINTF("%s", str);
	}
	PRINTF("\n");

	PRINTF(" Product:");
	str = smb_get_node_str(node, info->product);
	if (str) {
		PRINTF("%s", str);
	}
	PRINTF("\n");
}
