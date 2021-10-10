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
 * Copyright (c) 2004, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _ASM_HTABLE_H
#define	_ASM_HTABLE_H

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

#if !defined(__lint) && defined(__GNUC__)

#if defined(__i386) || defined(__amd64)

/*
 * This set of atomic operations are designed primarily
 * for some ia32 hat layer operations.
 */

extern __inline__ void atomic_orb(uint8_t *addr, uint8_t value)
{
	__asm__ __volatile__(
	    "lock; orb %%dl,%0"
	    : "=m" (*addr)
	    : "d" (value), "m" (*addr)
	    : "cc");
}

extern __inline__ void atomic_andb(uint8_t *addr, uint8_t value)
{
	__asm__ __volatile__(
	    "lock; andb %%dl,%0"
	    : "=m" (*addr)
	    : "d" (value), "m" (*addr)
	    : "cc");
}

extern __inline__ void atomic_inc16(uint16_t *addr)
{
	__asm__ __volatile__(
	    "lock; incw %0"
	    : "=m" (*addr)
	    : "m" (*addr)
	    : "cc");
}

extern __inline__ void atomic_dec16(uint16_t *addr)
{
	__asm__ __volatile__(
	    "lock; decw %0"
	    : "=m" (*addr)
	    : "m" (*addr)
	    : "cc");
}

extern __inline__ void mmu_tlbflush_entry(caddr_t addr)
{
	__asm__ __volatile__(
	    "invlpg %0"
	    : "=m" (*addr)
	    : "m" (*addr));
}

#endif	/* __i386 || __amd64 */

#endif	/* !__lint && __GNUC__ */

#ifdef	__cplusplus
}
#endif

#endif	/* _ASM_HTABLE_H */
