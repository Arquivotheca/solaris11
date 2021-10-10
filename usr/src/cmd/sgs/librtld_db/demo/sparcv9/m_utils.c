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
 * Copyright (c) 1995, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <stdio.h>
#include <string.h>

#include "rdb.h"

void
print_mach_varstring(struct ps_prochandle *ph, const char *varname)
{
	if (strcmp(varname, "ins") == 0) {
		display_in_regs(ph, NULL);
		return;
	}
	if (strcmp(varname, "globs") == 0) {
		display_global_regs(ph, NULL);
		return;
	}
	if (strcmp(varname, "outs") == 0) {
		display_out_regs(ph, NULL);
		return;
	}
	if (strcmp(varname, "locs") == 0) {
		display_local_regs(ph, NULL);
		return;
	}
	if (strcmp(varname, "specs") == 0) {
		display_special_regs(ph, NULL);
		return;
	}
	(void) printf("print: unknown variable given ($%s)\n", varname);
}
