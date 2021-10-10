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
/*
 * This file contains the handlers for the supported VGA commands.
 * This file will grow reflecting the commands that we need at kernel level.
 */

#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>

#include "vbiosd.h"

static void
vbiosd_vga_set_mode(uint8_t mode, vbiosd_res_blob_t *blob)
{
	struct vbiosd_ctx 	regs = {0};
	uint32_t		size;

	size = sizeof (vbios_call_results_t);

	/* If allocation fails, just bail out. */
	if (vbiosd_blob_res_alloc(blob, size) == VBIOSD_ERROR)
		return;

	/*
	 * Prepare registers for the VBIOS VGA call.
	 * AH = 0x0 [set mode]
	 * AL = mode
	 */
	regs.eax = mode;

	if (vbiosd_exec(&regs) == VBIOSD_ERROR) {
		vbiosd_error(NO_ERRNO, "set VGA mode %x failed", mode);
		return;
	}

	/* We always succeed. */
	blob->res->ret = VBIOS_VCALL_SUCCESS;
	blob->res->size = 0;
}

void
vbiosd_handle_vga_call(vbios_cmd_req_t *cmd, vbiosd_res_blob_t *blob)
{
	uint32_t	command = cmd->cmd;

	switch (command) {
	case VBIOS_CMD_SETMODE:
	{
		uint8_t		mode = cmd->args.mode.vga.val;

		vbiosd_debug(NO_ERRNO, "command is SETMODE, mode 0x%x", mode);
		vbiosd_vga_set_mode(mode, blob);
		break;
	}
	default:
		/* Log something. */
		break;
	}
}
