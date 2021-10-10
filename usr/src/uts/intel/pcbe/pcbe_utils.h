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

/*
 * Performance Counter Back-End for x86 processors supporting Architectural
 * Performance Monitoring.
 */

#ifndef _SYS_PCBE_UTILS_H
#define	_SYS_PCBE_UTILS_H

#ifdef	__cplusplus
extern "C" {
#endif

#define	ATTR_CMP(name, attr)						\
	(strncmp(name, attr, sizeof (attr)) == 0)

#define	WRMSR(msr, value)						\
	wrmsr((msr), (value));						\
	DTRACE_PROBE2(pcbe_wrmsr, uint64_t, (msr), uint64_t, (value));

#define	RDMSR(msr, value)						\
	(value) = rdmsr((msr));						\
	DTRACE_PROBE2(pcbe_rdmsr, uint64_t, (msr), uint64_t, (value));

#define	REAX(field) ((cp->cp_eax >> field) & field ## _MASK)
#define	REBX(field) ((cp->cp_ebx >> field) & field ## _MASK)
#define	RECX(field) ((cp->cp_ecx >> field) & field ## _MASK)
#define	REDX(field) ((cp->cp_edx >> field) & field ## _MASK)

#define	CPUID_FUNC_BASIC	0x0	/* Basic CPUID Information */

typedef enum pcbe_type {
	PCBE_UNKNOWN,
	PCBE_ARCH,
	PCBE_RAW,
	PCBE_FFC,
	PCBE_GPC,
	PCBE_UNC
} pcbe_type_t;

#define	PCBE_TABLE_SIZE(n) (sizeof (n) / sizeof (n[0]))

#define	BITMASK_XBITS(x)	((1ull << (x)) - 1ull)

/* Used to describe which counters support an event */
#define	C(x) (1 << (x))
#define	C_ALL 0xFFFFFFFF

/* Generic CPU information */
int pcbe_family_get();
int pcbe_model_get();

void pcbe_init();

int pcbe_name_compare(char *names, char *event);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_PCBE_UTILS_H */
