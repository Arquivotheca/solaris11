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

	.file	"brk.s"

#include "SYS.h"

/*
 * _brk_unlocked() simply traps into the kernel to set the brk.  It
 * returns 0 if the break was successfully set, or -1 otherwise.
 * It doesn't enforce any alignment and it doesn't perform any locking.
 * _brk_unlocked() is only called from brk() and _sbrk_unlocked().
 */

	ENTRY_NP(_brk_unlocked)
	SYSTRAP_RVAL1(brk)
	SYSCERROR
	RETC
	SET_SIZE(_brk_unlocked)
