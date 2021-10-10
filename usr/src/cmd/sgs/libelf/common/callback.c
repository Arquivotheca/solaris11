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
 * Copyright (c) 1990, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

#include <libelf.h>
#include "decl.h"


void
elf_fill(int fill)
{
	ELFACCESSDATA(_elf_byte, fill)
}

void
_elf_execfill(_elf_execfill_func_t *execfill_func)
{
	ELFACCESSDATA(_elf_execfill_func, execfill_func)
}

void
_elf_imgcalloc(_elf_imgcalloc_func_t *imgcalloc_func)
{
	ELFACCESSDATA(_elf_imgcalloc_func, imgcalloc_func)
}
