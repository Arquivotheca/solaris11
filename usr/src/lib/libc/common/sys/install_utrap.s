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
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

	.file	"install_utrap.s"

#include <sys/asm_linkage.h>

	ANSI_PRAGMA_WEAK(install_utrap,function)

#include "SYS.h"

/*
 * int install_utrap(utrap_entry_t type, utrap_handler_t new_handler,
 *			utrap_handler_t *old_handlerp)
 */
	SYSCALL_RVAL1(install_utrap)
	RET
	SET_SIZE(install_utrap)
