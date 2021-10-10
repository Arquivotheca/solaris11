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
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SYS_FAM6_PCBE_H
#define	_SYS_FAM6_PCBE_H

#ifdef __cplusplus
extern "C" {
#endif

#define	FALSE	0
#define	TRUE	1
#define	NT_END	0xFF

/*
 * Only the lower 32-bits can be written to in the general-purpose
 * counters.  The higher bits are extended from bit 31; all ones if
 * bit 31 is one and all zeros otherwise.
 *
 * The fixed-function counters do not have this restriction.
 */
#define	BITMASK_XBITS(x)	((1ull << (x)) - 1ull)
#define	BITS_EXTENDED_FROM_31	(BITMASK_XBITS(width_gpc) & ~BITMASK_XBITS(31))

#define	WRMSR(msr, value)						\
	wrmsr((msr), (value));						\
	DTRACE_PROBE2(wrmsr, uint64_t, (msr), uint64_t, (value));

#define	RDMSR(msr, value)						\
	(value) = rdmsr((msr));						\
	DTRACE_PROBE2(rdmsr, uint64_t, (msr), uint64_t, (value));

/* Counter Type */
#define	CORE_GPC	0	/* General-Purpose Counter (GPC) */
#define	CORE_FFC	1	/* Fixed-Function Counter (FFC) */

/*
 * Configuration data for Intel Architectural Performance
 * Monitoring. See Appendix A of the "Intel 64 and IA-32
 * Architectures Software Developer's Manual, Volume 3B:
 * System Programming Guide, Part 2."
 */

/* MSR Addresses */
#define	GPC_BASE_PMC		0x00c1	/* First GPC */
#define	GPC_BASE_PES		0x0186	/* First GPC Event Select register */
#define	FFC_BASE_PMC		0x0309	/* First FFC */
#define	PERF_FIXED_CTR_CTRL	0x038d	/* Used to enable/disable FFCs */
#define	PERF_GLOBAL_STATUS	0x038e	/* Overflow status register */
#define	PERF_GLOBAL_CTRL	0x038f	/* Used to enable/disable counting */
#define	PERF_GLOBAL_OVF_CTRL	0x0390	/* Used to clear overflow status */

/*
 * Processor Event Select register fields
 */
#define	CORE_USR	(1ULL << 16)	/* Count while not in ring 0 */
#define	CORE_OS		(1ULL << 17)	/* Count while in ring 0 */
#define	CORE_EDGE	(1ULL << 18)	/* Enable edge detection */
#define	CORE_PC		(1ULL << 19)	/* Enable pin control */
#define	CORE_INT	(1ULL << 20)	/* Enable interrupt on overflow */
#define	CORE_EN		(1ULL << 22)	/* Enable counting */
#define	CORE_INV	(1ULL << 23)	/* Invert the CMASK */
#define	CORE_ANYTHR	(1ULL << 21)	/* Count event for any thread on core */

#define	CORE_UMASK_SHIFT	8
#define	CORE_UMASK_MASK		0xffu
#define	CORE_CMASK_SHIFT	24
#define	CORE_CMASK_MASK		0xffu

/*
 * Fixed-function counter attributes
 */
#define	CORE_FFC_OS_EN	(1ULL << 0)	/* Count while not in ring 0 */
#define	CORE_FFC_USR_EN	(1ULL << 1)	/* Count while in ring 1 */
#define	CORE_FFC_ANYTHR	(1ULL << 2)	/* Count event for any thread on core */
#define	CORE_FFC_PMI	(1ULL << 3)	/* Enable interrupt on overflow */

/*
 * Number of bits for specifying each FFC's attributes in the control register
 */
#define	CORE_FFC_ATTR_SIZE	4

/*
 * CondChgd and OvfBuffer fields of global status and overflow control registers
 */
#define	CONDCHGD	(1ULL << 63)
#define	OVFBUFFER	(1ULL << 62)
#define	MASK_CONDCHGD_OVFBUFFER	(CONDCHGD | OVFBUFFER)

#define	ALL_STOPPED	0ULL

struct events_table_t {
	uint8_t		eventselect;
	uint8_t		unitmask;
	uint64_t	supported_counters;
	const char	*name;
};

/* Used to describe which counters support an event */
#define	C(x) (1 << (x))
#define	C0 C(0)
#define	C1 C(1)
#define	C2 C(2)
#define	C3 C(3)
#define	C_ALL 0xFFFFFFFFFFFFFFFF

/* Architectural events */
#define	ARCH_EVENTS_COMMON					\
	{ 0xc0, 0x00, C_ALL, "inst_retired.any_p" },		\
	{ 0x3c, 0x01, C_ALL, "cpu_clk_unhalted.ref_p" },	\
	{ 0x2e, 0x4f, C_ALL, "longest_lat_cache.reference" },	\
	{ 0x2e, 0x41, C_ALL, "longest_lat_cache.miss" },	\
	{ 0xc4, 0x00, C_ALL, "br_inst_retired.all_branches" },	\
	{ 0xc5, 0x00, C_ALL, "br_misp_retired.all_branches" }

#ifdef __cplusplus
}
#endif

#endif /* _SYS_FAM6_PCBE_H */
