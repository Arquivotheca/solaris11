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
 * Copyright (c) 2004, 2005, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _ASM_SUNDDI_H
#define	_ASM_SUNDDI_H

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

#if !defined(__lint) && defined(__GNUC__)

#if defined(__i386) || defined(__amd64)

extern __inline__ uint8_t inb(int port)
{
	uint16_t port16 = (uint16_t)port;
	uint8_t value;

	__asm__ __volatile__(
		"inb (%1)"		/* value in %al */
		: "=a" (value)
		: "d" (port16));
	return (value);
}

extern __inline__ uint16_t inw(int port)
{
	uint16_t port16 = (uint16_t)port;
	uint16_t value;

	__asm__ __volatile__(
		"inw (%1)"		/* value in %ax */
		: "=a" (value)
		: "d" (port16));
	return (value);
}

extern __inline__ uint32_t inl(int port)
{
	uint16_t port16 = (uint16_t)port;
	uint32_t value;

	__asm__ __volatile__(
		"inl (%1)"		/* value in %eax */
		: "=a" (value)
		: "d" (port16));
	return (value);
}

extern __inline__ void outb(int port, uint8_t value)
{
	uint16_t port16 = (uint16_t)port;

	__asm__ __volatile__(
		"outb (%1)"
		: /* no output */
		: "a" (value), "d" (port16));
}

extern __inline__ void outw(int port, uint16_t value)
{
	uint16_t port16 = (uint16_t)port;

	__asm__ __volatile__(
		"outw (%1)"
		: /* no output */
		: "a" (value), "d" (port16));
}

extern __inline__ void outl(int port, uint32_t value)
{
	uint16_t port16 = (uint16_t)port;

	__asm__ __volatile__(
		"outl (%1)"
		: /* no output */
		: "a" (value), "d" (port16));
}

#if defined(_BOOT)

extern __inline__ void sync_instruction_memory(caddr_t v, size_t len)
{
	__asm__ __volatile__("nop");
}

#endif /* _BOOT */

#endif /* __i386 || __amd64 */

#endif /* !__lint && __GNUC__ */

#ifdef __cplusplus
}
#endif

#endif	/* _ASM_SUNDDI_H */
