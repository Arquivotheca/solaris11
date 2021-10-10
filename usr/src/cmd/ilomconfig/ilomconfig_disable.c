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
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "ilomconfig.h"
#include <libnvpair.h>
#include "cli_common_log.h"

cli_errno_t
disable_interconnect() {
	cli_errno_t ret = SSM_CLI_OK;
	char value[16];
	char hostmacaddress[32];

	/* Check to make sure this feature is supported on ILOM */
	if (does_target_exist("/SP/network/interconnect state") == 0)
		return (SSM_CLI_SUBCOMMAND_NOT_SUPPORTED);

	ret = get_property_ilom("/SP/network/interconnect",
		"hostmacaddress", hostmacaddress, sizeof (hostmacaddress));
	if (ret != 0)
		return (ret);

	ret = unset_host_interconnect(hostmacaddress);
	if (ret != SSM_CLI_OK) {
		(void) debug_log("Error disabling interconnect\n");
	}

	/*
	 * If failure occurs on host side, continue to disable
	 * on ILOM side.
	 */

	(void) snprintf(value, sizeof (value), "%s", "disabled");
	return (set_property("/SP/network",
			    "interconnect", "state", value, 0));
}
