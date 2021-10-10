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
 * This file contains the handlers for the supported VESA commands.
 * This file will grow reflecting the commands that we need at kernel level.
 */

#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>

#include "vbiosd.h"

#define	VESA_FAIL(x)	((x.eax & 0xffff) != 0x004f)

static void
vbiosd_parse_VESA_fail(vbiosd_res_blob_t *blob, uint32_t error)
{
	if ((error & 0x00ff) != 0x004f) {
		blob->res->ret = VBIOS_VCALL_ENOTSUP;
	} else {
		int	error_code = (error >> 8) & 0xff;
		/*
		 * VBIOS_VCALL_* error codes in sys/vbios.h are a direct
		 * mapping of VESA calls return values.
		 */
		blob->res->ret = error_code;
	}
}

static void
vbiosd_VESA_set_mode(uint16_t mode, uint16_t flags, vbiosd_res_blob_t *blob)
{
	struct vbiosd_ctx 	regs = {0};
	uint32_t		size;

	/* Prepare registers for the VBIOS call.  4F02h - SET VBE MODE */
	regs.eax = 0x4f02;
	regs.ebx = mode;
	regs.ebx |= flags;

	if (vbiosd_exec(&regs) == VBIOSD_ERROR) {
		vbiosd_error(NO_ERRNO, "function 02h - Set VBE Mode FAILED\n");
		return;
	}

	/*
	 * The interrupt execution succeeded, we need now to check the VESA call
	 * return value. Since this function does not need to pass any extra
	 * information back, we allocate a common 'res' structure.
	 */
	size = sizeof (vbios_call_results_t);

	/* If allocation fails, just bail out. */
	if (vbiosd_blob_res_alloc(blob, size) == VBIOSD_ERROR)
		return;

	/* Parse VESA call results. */
	if (VESA_FAIL(regs)) {
		/* SETMODE VESA call failed. */
		vbiosd_parse_VESA_fail(blob, regs.eax);
	} else {
		/* SEMODE VESA call succeeded. */
		blob->res->ret = VBIOS_VCALL_SUCCESS;
		blob->res->size = 0;
	}
}

void
vbiosd_handle_vesa_call(vbios_cmd_req_t *cmd, vbiosd_res_blob_t *blob)
{
	uint32_t	command = cmd->cmd;

	switch (command) {
	case VBIOS_CMD_SETMODE:
	{
		uint16_t	mode = cmd->args.mode.vesa.val;
		uint16_t	flags = cmd->args.mode.vesa.flags;

		vbiosd_debug(NO_ERRNO, "command is SETMODE, mode 0x%x, flags"
		    " %x", mode, flags);
		vbiosd_VESA_set_mode(mode, flags, blob);
		break;
	}
	default:
		/* Log something. */
		break;
	}
}
