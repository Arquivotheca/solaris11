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
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */


#include <sys/asm_linkage.h>
#include <sys/asm_misc.h>
#include <sys/regset.h>
#include <sys/panic.h>
#include <sys/ontrap.h>
#include <sys/privregs.h>
#include <sys/segments.h>
#include <sys/trap.h>

#if defined(__lint)
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/thread.h>
#include <sys/archsystm.h>
#include <sys/byteorder.h>
#include <sys/dtrace.h>
#include <sys/x86_archext.h>
#else   /* __lint */
#include "assym.h"
#endif  /* __lint */

#if defined(__lint)

/*ARGSUSED*/
void
rd_idtr(desctbr_t *idtr)
{}

/*ARGSUSED*/
void
wr_idtr(desctbr_t *idtr)
{}

#else	/* __lint */

	ENTRY_NP(rd_idtr)
	sidt	(%rdi)
	ret
	SET_SIZE(rd_idtr)

	ENTRY_NP(wr_idtr)
	lidt	(%rdi)
	ret
	SET_SIZE(wr_idtr)

#endif	/* __lint */

#if defined(__lint)

/*ARGSUSED*/
void
rd_gdtr(desctbr_t *gdtr)
{}

/*ARGSUSED*/
void
wr_gdtr(desctbr_t *gdtr)
{}

#else	/* __lint */

	ENTRY_NP(rd_gdtr)
	pushq	%rbp
	movq	%rsp, %rbp
	sgdt	(%rdi)
	leave
	ret
	SET_SIZE(rd_gdtr)

	ENTRY_NP(wr_gdtr)
	pushq	%rbp
	movq	%rsp, %rbp
	lgdt	(%rdi)
	jmp	1f
	nop
1:
	leave
	ret
	SET_SIZE(wr_gdtr)

#endif	/* __lint */

#if defined(__lint)

/*ARGSUSED*/
void
load_segment_registers(selector_t cs, selector_t fs, selector_t gs,
    selector_t ss)
{}

selector_t
get_cs_register()
{ return (0); }

#else	/* __lint */

	/*
	 * loads zero selector for ds and es.
	 */
	ENTRY_NP(load_segment_registers)
	pushq	%rbp
	movq	%rsp, %rbp
	pushq	%rdi
	pushq	$.newcs
	lretq
.newcs:
	/*
	 * zero %ds and %es - they're ignored anyway
	 */
	xorl	%eax, %eax
	movw	%ax, %ds
	movw	%ax, %es
	movl	%esi, %eax
	movw	%ax, %fs
	movl	%edx, %eax
	movw	%ax, %gs
	movl	%ecx, %eax
	movw	%ax, %ss
	leave
	ret
	SET_SIZE(load_segment_registers)

	ENTRY_NP(get_cs_register)
	movq	$0, %rax
	movw	%cs, %rax
	ret
	SET_SIZE(get_cs_register)

#endif	/* __lint */

#if defined(__lint)

/*ARGSUSED*/
void
wr_ldtr(selector_t ldtsel)
{}

selector_t
rd_ldtr(void)
{ return (0); }

#else	/* __lint */

	ENTRY_NP(wr_ldtr)
	movq	%rdi, %rax
	lldt	%ax
	ret
	SET_SIZE(wr_ldtr)

	ENTRY_NP(rd_ldtr)
	xorl	%eax, %eax
	sldt	%ax
	ret
	SET_SIZE(rd_ldtr)

#endif	/* __lint */

#if defined(__lint)

/*ARGSUSED*/
void
wr_tsr(selector_t tsssel)
{}

#else	/* __lint */

	ENTRY_NP(wr_tsr)
	movq	%rdi, %rax
	ltr	%ax
	ret
	SET_SIZE(wr_tsr)

#endif	/* __lint */
