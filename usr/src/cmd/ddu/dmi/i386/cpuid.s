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

#if defined(__lint)
#include <sys/types.h>
uint32_t
/* LINTED E_FUNC_ARG_UNUSED */
get_cpuid(uint32_t id, uint32_t *b, uint32_t *c, uint32_t *d)
{
	return (0);
}

uint32_t
/* LINTED E_FUNC_ARG_UNUSED */
get_cpuid4(uint32_t *b, uint32_t *c, uint32_t *d)
{
	return (0);
}
#else	/* __lint */
/*
 * get_cpuid function exec CPUID instruction.
 *
 * input:
 * arg1(ebp + 0x8): CPUID instruction code.
 *                 move arg1 to eax then execute CPUID
 *
 * output:
 * CPUID result will store in eax, ebx, ecx and edx.
 * - eax: is func return value.
 * - ebx: moved to func arg2(ebp + 0xc)
 * - ecx: moved to func arg3(ebp + 0x10)
 * - edx: moved to func arg4(ebp + 0x14)
 */
	.text
	.align  16
	.globl  get_cpuid
	.type   get_cpuid, @function
get_cpuid:
	pushl	%ebp
	movl	%esp, %ebp
	pushl	%ebx
	movl	8(%ebp), %eax
	cpuid
	pushl	%eax
	movl	0x0c(%ebp), %eax
	movl	%ebx, (%eax)
	movl	0x10(%ebp), %eax
	movl	%ecx, (%eax)
	movl	0x14(%ebp), %eax
	movl	%edx, (%eax)
	popl	%eax
	popl	%ebx
	popl	%ebp
	ret

/*
 * get_cpuid4 function exec CPUID instruction 0x4.
 *            set eax with 0x4 and reset ecx with 0 then execute CPUID
 *
 * output:
 * CPUID result will store in eax, ebx, ecx and edx.
 * - eax: is func return value.
 * - ebx: moved to func arg2(ebp + 0x8)
 * - ecx: moved to func arg3(ebp + 0xc)
 * - edx: moved to func arg4(ebp + 0x10)
 */
	.text
	.align  16
	.globl  get_cpuid4
	.type   get_cpuid4, @function
get_cpuid4:
	pushl	%ebp
	movl	%esp, %ebp
	pushl	%ebx
	movl	$4, %eax
	movl	$0, %ecx
	cpuid
	pushl	%eax
	movl	0x08(%ebp), %eax
	movl	%ebx, (%eax)
	movl	0x0c(%ebp), %eax
	movl	%ecx, (%eax)
	movl	0x10(%ebp), %eax
	movl	%edx, (%eax)
	popl	%eax
	popl	%ebx
	popl	%ebp
	ret
#endif	/* __lint */
