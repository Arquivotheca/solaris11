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
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _THR_INLINES_H
#define	_THR_INLINES_H

#if !defined(__lint) && defined(__GNUC__)

/* inlines for gcc */

extern __inline__ ulwp_t *
_curthread(void)
{
#if defined(__amd64)
	ulwp_t *__value;
	__asm__ __volatile__("movq %%fs:0, %0" : "=r" (__value));
#elif defined(__i386)
	ulwp_t *__value;
	__asm__ __volatile__("movl %%gs:0, %0" : "=r" (__value));
#elif defined(__sparc)
	register ulwp_t *__value __asm__("g7");
#else
#error	"port me"
#endif
	return (__value);
}

extern __inline__ ulwp_t *
__curthread(void)
{
	ulwp_t *__value;
	__asm__ __volatile__(
#if defined(__amd64)
	    "movq %%fs:0, %0\n\t"
#elif defined(__i386)
	    "movl %%gs:0, %0\n\t"
#elif defined(__sparcv9)
	    ".register %%g7, #scratch\n\t"
	    "ldx [%%g7 + 80], %0\n\t"
#elif defined(__sparc)
	    ".register %%g7, #scratch\n\t"
	    "ld [%%g7 + 80], %0\n\t"
#else
#error	"port me"
#endif
	    "1:"
	    : "=r" (__value)
	    : : "cc");
	return (__value);
}

extern __inline__ greg_t
stkptr(void)
{
#if defined(__amd64)
	register greg_t __value __asm__("rsp");
#elif defined(__i386)
	register greg_t __value __asm__("esp");
#elif defined(__sparc)
	register greg_t __value __asm__("sp");
#else
#error	"port me"
#endif
	return (__value);
}

#if defined(__sparcv9) || defined(__sparc)
extern __inline__ hrtime_t
gethrtime(void)		/* note: caller-saved registers are trashed */
{
#if defined(__sparcv9)
	register hrtime_t __value __asm__("o0");
	__asm__ __volatile__(
	    "ta 0x24\n\t"
	    "sllx %%o0, 32, %0\n\t"
	    "or %%o1, %0, %0"
	    : "=r" (__value)
	    : : "o1", "o2", "o3", "o4", "o5", "cc");
#elif defined(__sparc)
	register hrtime_t __value __asm__("o0");
	__asm__ __volatile__(
	    "ta 0x24"
	    : "=r" (__value)
	    : : "o2", "o3", "o4", "o5", "cc");
#endif
	return (__value);
}
#elif !defined(__x86)
#error	"port me"
#endif

#if defined(__x86)
extern __inline__ void *
gethrt(void)
{
#if defined(__amd64)
	void *__hrtp;
	__asm__ __volatile__(
	    "movl $7, %%eax\n\t"
	    "int $0xd2"
	    : "=a" (__hrtp)
	    : : "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11", "cc");
#elif defined(__i386)
	void *__hrtp;
	__asm__ __volatile__(
	    "movl $7, %%eax\n\t"
	    "int $0xd2"
	    : "=a" (__hrtp)
	    : : "ecx", "cc");
#endif
	return (__hrtp);
}
#endif

extern __inline__ int
set_lock_byte(volatile uint8_t *__lockp)
{
	int __value;
#if defined(__x86)
	__asm__ __volatile__(
	    "movl $1, %0\n\t"
	    "xchgb %%dl, %1"
	    : "+d" (__value), "+m" (*__lockp));
#elif defined(__sparc)
	__asm__ __volatile__(
	    "ldstub %1, %0\n\t"
	    "membar #LoadLoad"
	    : "=r" (__value), "+m" (*__lockp));
#else
#error	"port me"
#endif
	return (__value);
}

extern __inline__ uint32_t
atomic_swap_32(volatile uint32_t *__memory, uint32_t __value)
{
#if defined(__x86)
	__asm__ __volatile__(
	    "xchgl %0, %1"
	    : "+q" (__value), "+m" (*__memory));
	return (__value);
#elif defined(__sparc)
	uint32_t __tmp1, __tmp2;
	__asm__ __volatile__(
	    "ld [%3], %0\n\t"
	    "1:\n\t"
	    "mov %4, %1\n\t"
	    "cas [%3], %0, %1\n\t"
	    "cmp %0, %1\n\t"
	    "bne,a,pn %%icc, 1b\n\t"
	    "  mov %1, %0"
	    : "=&r" (__tmp1), "=&r" (__tmp2), "=m" (*__memory)
	    : "r" (__memory), "r" (__value)
	    : "cc");
	return (__tmp2);
#else
#error	"port me"
#endif
}

extern __inline__ uint32_t
atomic_cas_32(volatile uint32_t *__memory, uint32_t __cmp, uint32_t __newvalue)
{
	uint32_t __oldvalue;
#if defined(__x86)
	__asm__ __volatile__(
	    "lock; cmpxchgl %3, %0"
	    : "=m" (*__memory), "=a" (__oldvalue)
	    : "a" (__cmp), "r" (__newvalue));
#elif defined(__sparc)
	__asm__ __volatile__(
	    "cas [%2], %3, %1"
	    : "=m" (*__memory), "=&r" (__oldvalue)
	    : "r" (__memory), "r" (__cmp), "1" (__newvalue));
#else
#error	"port me"
#endif
	return (__oldvalue);
}

extern __inline__ void
atomic_inc_32(volatile uint32_t *__memory)
{
#if defined(__x86)
	__asm__ __volatile__(
	    "lock; incl %0"
	    : "+m" (*__memory));
#elif defined(__sparc)
	uint32_t __tmp1, __tmp2;
	__asm__ __volatile__(
	    "ld [%3], %0\n\t"
	    "1:\n\t"
	    "add %0, 1, %1\n\t"
	    "cas [%3], %0, %1\n\t"
	    "cmp %0, %1\n\t"
	    "bne,a,pn %%icc, 1b\n\t"
	    "  mov %1, %0"
	    : "=&r" (__tmp1), "=&r" (__tmp2), "=m" (*__memory)
	    : "r" (__memory)
	    : "cc");
#else
#error	"port me"
#endif
}

extern __inline__ void
atomic_dec_32(volatile uint32_t *__memory)
{
#if defined(__x86)
	__asm__ __volatile__(
	    "lock; decl %0"
	    : "+m" (*__memory));
#elif defined(__sparc)
	uint32_t __tmp1, __tmp2;
	__asm__ __volatile__(
	    "ld [%3], %0\n\t"
	    "1:\n\t"
	    "sub %0, 1, %1\n\t"
	    "cas [%3], %0, %1\n\t"
	    "cmp %0, %1\n\t"
	    "bne,a,pn %%icc, 1b\n\t"
	    "  mov %1, %0"
	    : "=&r" (__tmp1), "=&r" (__tmp2), "=m" (*__memory)
	    : "r" (__memory)
	    : "cc");
#else
#error	"port me"
#endif
}

extern __inline__ void
atomic_and_32(volatile uint32_t *__memory, uint32_t __bits)
{
#if defined(__x86)
	__asm__ __volatile__(
	    "lock; andl %1, %0"
	    : "+m" (*__memory)
	    : "r" (__bits));
#elif defined(__sparc)
	uint32_t __tmp1, __tmp2;
	__asm__ __volatile__(
	    "ld [%3], %0\n\t"
	    "1:\n\t"
	    "and %0, %4, %1\n\t"
	    "cas [%3], %0, %1\n\t"
	    "cmp %0, %1\n\t"
	    "bne,a,pn %%icc, 1b\n\t"
	    "  mov %1, %0"
	    : "=&r" (__tmp1), "=&r" (__tmp2), "=m" (*__memory)
	    : "r" (__memory), "r" (__bits)
	    : "cc");
#else
#error	"port me"
#endif
}

extern __inline__ void
atomic_or_32(volatile uint32_t *__memory, uint32_t __bits)
{
#if defined(__x86)
	__asm__ __volatile__(
	    "lock; orl %1, %0"
	    : "+m" (*__memory)
	    : "r" (__bits));
#elif defined(__sparc)
	uint32_t __tmp1, __tmp2;
	__asm__ __volatile__(
	    "ld [%3], %0\n\t"
	    "1:\n\t"
	    "or %0, %4, %1\n\t"
	    "cas [%3], %0, %1\n\t"
	    "cmp %0, %1\n\t"
	    "bne,a,pn %%icc, 1b\n\t"
	    "  mov %1, %0"
	    : "=&r" (__tmp1), "=&r" (__tmp2), "=m" (*__memory)
	    : "r" (__memory), "r" (__bits)
	    : "cc");
#else
#error	"port me"
#endif
}

#if defined(__sparc)	/* only needed on sparc */

extern __inline__ ulong_t
caller(void)
{
	register ulong_t __value __asm__("i7");
	return (__value);
}

extern __inline__ ulong_t
getfp(void)
{
	register ulong_t __value __asm__("fp");
	return (__value);
}

#endif	/* __sparc */

#if defined(__x86)	/* only needed on x86 */

extern __inline__ void
ht_pause(void)
{
	__asm__ __volatile__("rep; nop");
}

#endif	/* __x86 */

#endif	/* !__lint && __GNUC__ */

#endif	/* _THR_INLINES_H */
