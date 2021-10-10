/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright (c) 2001 by Sun Microsystems, Inc.
 * All rights reserved.
 */

/*
 * These crt*.o modules are provided as the bare minimum required
 * from a crt*.o for inclusion in building low level system
 * libraries.  The are only be to included in libraries which
 * contain *no* C++ code and want to avoid the startup code
 * that the C++ runtime has introduced into the crt*.o modules.
 *
 * For further details - see bug#4433015
 */

	.ident	"%Z%%M%	%I%	%E% SMI"
	.file	"crti.s"

/*
 * _init function prologue
 */
	.section	.init,"ax"
	.globl	_init
	.type	_init,@function
	.align	16
_init:
	pushl	%ebp
	movl	%esp, %ebp
	pushl	%ebx
	call	.L1
.L1:	popl	%ebx
	addl	$_GLOBAL_OFFSET_TABLE_+[.-.L1], %ebx

/*
 * _fini function prologue
 */
	.section	.fini,"ax"
	.globl	_fini
	.type	_fini,@function
	.align	16
_fini:
	pushl	%ebp
	movl	%esp, %ebp
	pushl	%ebx
	call	.L2
.L2:	popl	%ebx
	addl	$_GLOBAL_OFFSET_TABLE_+[.-.L2], %ebx
