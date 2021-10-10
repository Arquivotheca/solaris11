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
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SYS_NIAGARA2REGS_H
#define	_SYS_NIAGARA2REGS_H

#ifdef __cplusplus
extern "C" {
#endif

#define	MB(n)	((n) * 1024 * 1024)

#define	L2CACHE_SIZE		MB(4)
#define	L2CACHE_LINESIZE	64
#define	L2CACHE_ASSOCIATIVITY	16

#define	NIAGARA2_HSVC_MAJOR	1
#define	NIAGARA2_HSVC_MINOR	0

#define	VFALLS_HSVC_MAJOR	1
#define	VFALLS_HSVC_MINOR	0

#define	KT_HSVC_MAJOR		1
#define	KT_HSVC_MINOR		0

#define	VT_HSVC_MAJOR		1
#define	VT_HSVC_MINOR		0

#ifdef KT_IMPL

/* Sample PIC overflow range is -2 to -1 */
#define	SAMPLE_PIC_IN_OV_RANGE(x)	(((uint32_t)x >= 0xfffffffe) ? 1 : 0)

#endif

/* PIC overflow range is -16 to -1 */
#define	PIC_IN_OV_RANGE(x)	(((uint32_t)x >= 0xfffffff0) ? 1 : 0)

#define	VT_PERFREG_SHIFT	3

#ifdef VT_IMPL

/*
 * SPARC Performance Instrumentation Counter
 */
#define	PIC_MASK	(((uint64_t)1 << 32) - 1)	/* pic in bits 31:0 */

/*
 * SPARC Performance Control Register
 */
#define	CPC_PCR_OV_SHIFT	0
#define	CPC_PCR_OV_MASK		UINT64_C(0x1)

#define	CPC_PCR_TOE_SHIFT	1
#define	CPC_PCR_TOE		(1ull << CPC_PCR_TOE_SHIFT)

#define	CPC_PCR_UT_SHIFT	2
#define	CPC_PCR_UT		(1ull << CPC_PCR_UT_SHIFT)

#define	CPC_PCR_ST_SHIFT	3
#define	CPC_PCR_ST		(1ull << CPC_PCR_ST_SHIFT)

#define	CPC_PCR_HT_SHIFT	4
#define	CPC_PCR_HT		(1ull << CPC_PCR_HT_SHIFT)

#define	CPC_PCR_MASK_SHIFT	5
#define	CPC_PCR_MASK		UINT64_C(0x3f)

#define	CPC_PCR_SL_SHIFT	11
#define	CPC_PCR_SL		UINT64_C(0x1f)

#define	CPC_PCR_PICNPT_SHIFT	16
#define	CPC_PCR_PICNHT_SHIFT	17

#define	CPC_PCR_EVSEL_MASK	UINT64_C(0x7ff)	/* mask[10:5] sl[15:11] */

#else

/*
 * SPARC Performance Instrumentation Counter
 */
#define	PIC0_MASK	(((uint64_t)1 << 32) - 1)	/* pic0 in bits 31:0 */
#define	PIC1_SHIFT	32				/* pic1 in bits 64:32 */

/*
 * SPARC Performance Control Register
 */
#define	CPC_PCR_PRIV_SHIFT	0
#define	CPC_PCR_ST_SHIFT	1
#define	CPC_PCR_UT_SHIFT	2

#define	CPC_PCR_HT_SHIFT	3
#define	CPC_PCR_HT		(1ull << CPC_PCR_HT_SHIFT)

#define	CPC_PCR_TOE0_SHIFT	4
#define	CPC_PCR_TOE1_SHIFT	5
#define	CPC_PCR_TOE0		(1ull << CPC_PCR_TOE0_SHIFT)
#define	CPC_PCR_TOE1		(1ull << CPC_PCR_TOE1_SHIFT)

#define	CPC_PCR_PIC0_SHIFT	6
#define	CPC_PCR_PIC1_SHIFT	19
#define	CPC_PCR_PIC0_MASK	UINT64_C(0xfff)
#define	CPC_PCR_PIC1_MASK	UINT64_C(0xfff)

#define	CPC_PCR_OV0_SHIFT	18
#define	CPC_PCR_OV1_SHIFT	30
#define	CPC_PCR_OV0_MASK	UINT64_C(0x40000)
#define	CPC_PCR_OV1_MASK	UINT64_C(0x80000000)

#define	CPC_PCR_HOLDOV0_SHIFT	62
#define	CPC_PCR_HOLDOV1_SHIFT	63
#define	CPC_PCR_HOLDOV0		(1ull << CPC_PCR_HOLDOV0_SHIFT)
#define	CPC_PCR_HOLDOV1		(1ull << CPC_PCR_HOLDOV1_SHIFT)

#endif

#if defined(KT_IMPL)

#define	CPC_PCR_SAMPLE_MODE_SHIFT	32
#define	CPC_PCR_SAMPLE_MODE_MASK	(1ull << CPC_PCR_SAMPLE_MODE_SHIFT)

#endif

/*
 * Hypervisor FAST_TRAP API function numbers to get/set DRAM
 * performance counters for Niagara2
 */
#define	HV_NIAGARA2_GETPERF		0x104
#define	HV_NIAGARA2_SETPERF		0x105

/*
 * Hypervisor FAST_TRAP API function numbers to get/set DRAM
 * performance counters for Victoria Falls
 */
#define	HV_VFALLS_GETPERF		0x106
#define	HV_VFALLS_SETPERF		0x107

/*
 * Hypervisor FAST_TRAP API function numbers to get/set DRAM
 * performance counters for KT
 */
#define	HV_KT_GETPERF			0x122
#define	HV_KT_SETPERF			0x123

/*
 * Hypervisor FAST_TRAP API function numbers to get/set DRAM
 * performance counters for VT
 */
#define	HV_VT_GETPERF			0x184
#define	HV_VT_SETPERF			0x185

#if defined(KT_IMPL) || defined(VT_IMPL)

/*
 * KT DRAM performance counters
 */
#define	DRAM_PIC0_SEL_SHIFT	0x0
#define	DRAM_PIC1_SEL_SHIFT	0x4

#define	DRAM_PIC0_SHIFT		0x0
#define	DRAM_PIC0_MASK		0x7fffffff
#define	DRAM_PIC1_SHIFT		0x20
#define	DRAM_PIC1_MASK		0x7fffffff

#else

/*
 * Niagara2 and VF DRAM performance counters
 */
#define	DRAM_PIC0_SEL_SHIFT	0x4
#define	DRAM_PIC1_SEL_SHIFT	0x0

#define	DRAM_PIC0_SHIFT		0x20
#define	DRAM_PIC0_MASK		0x7fffffff
#define	DRAM_PIC1_SHIFT		0x0
#define	DRAM_PIC1_MASK		0x7fffffff

#endif

#if defined(NIAGARA2_IMPL)
/*
 * SPARC/DRAM performance counter register numbers for HV_NIAGARA2_GETPERF
 * and HV_NIAGARA2_SETPERF for Niagara2
 */
#define	DRAM_BANKS		0x4

#define	HV_SPARC_CTL		0x0
#define	HV_DRAM_CTL0		0x1
#define	HV_DRAM_COUNT0		0x2
#define	HV_DRAM_CTL1		0x3
#define	HV_DRAM_COUNT1		0x4
#define	HV_DRAM_CTL2		0x5
#define	HV_DRAM_COUNT2		0x6
#define	HV_DRAM_CTL3		0x7
#define	HV_DRAM_COUNT3		0x8

#elif defined(VFALLS_IMPL)
/*
 * SPARC/DRAM performance counter register numbers for HV_VFALLS_GETPERF
 * and HV_VFALLS_SETPERF for Victoria Falls
 * Support for 4-node configuration
 */
#define	DRAM_BANKS		0x8

#define	HV_SPARC_CTL		0x0
#define	HV_L2_CTL		0x1
#define	HV_DRAM_CTL0		0x2
#define	HV_DRAM_COUNT0		0x3
#define	HV_DRAM_CTL1		0x4
#define	HV_DRAM_COUNT1		0x5
#define	HV_DRAM_CTL2		0x6
#define	HV_DRAM_COUNT2		0x7
#define	HV_DRAM_CTL3		0x8
#define	HV_DRAM_COUNT3		0x9
#define	HV_DRAM_CTL4		0xa
#define	HV_DRAM_COUNT4		0xb
#define	HV_DRAM_CTL5		0xc
#define	HV_DRAM_COUNT5		0xd
#define	HV_DRAM_CTL6		0xe
#define	HV_DRAM_COUNT6		0xf
#define	HV_DRAM_CTL7		0x10
#define	HV_DRAM_COUNT7		0x11

#define	L2_CTL_MASK		0x3
#define	SL3_MASK		0x300
#define	SL_MASK			0xf00

#elif defined(KT_IMPL)
/*
 * SPARC/DRAM performance counter register numbers for HV_KT_GETPERF
 * and HV_KT_SETPERF for KT
 * Support for 4-node configuration
 */

#define	DRAM_BANKS		0x8

#define	HV_SPARC_CTL		0x0
#define	HV_L2_CTL		0x1
#define	HV_DRAM_CTL0		0x2
#define	HV_DRAM_COUNT0		0x3
#define	HV_DRAM_CTL1		0x5
#define	HV_DRAM_COUNT1		0x6
#define	HV_DRAM_CTL2		0x8
#define	HV_DRAM_COUNT2		0x9
#define	HV_DRAM_CTL3		0xb
#define	HV_DRAM_COUNT3		0xc
#define	HV_DRAM_CTL4		0xe
#define	HV_DRAM_COUNT4		0xf
#define	HV_DRAM_CTL5		0x11
#define	HV_DRAM_COUNT5		0x12
#define	HV_DRAM_CTL6		0x14
#define	HV_DRAM_COUNT6		0x15
#define	HV_DRAM_CTL7		0x17
#define	HV_DRAM_COUNT7		0x18

#define	L2_CTL_MASK		0x3
#define	SL3_MASK		0x300
#define	SL_MASK			0xf00

#elif defined(VT_IMPL)
/*
 * SPARC/DRAM performance counter register numbers for HV_VT_GETPERF
 * and HV_VT_SETPERF for VT
 * Support for 4-node configuration
 */

#define	DRAM_BANKS		0x8

#define	HV_SPARC_CTL0		0x0
#define	HV_SPARC_CTL1		0x1
#define	HV_SPARC_CTL2		0x2
#define	HV_SPARC_CTL3		0x3
#define	HV_DRAM_CTL0		0x4
#define	HV_DRAM_COUNT0		0x5
#define	HV_DRAM_CTL1		0x7
#define	HV_DRAM_COUNT1		0x8
#define	HV_DRAM_CTL2		0xa
#define	HV_DRAM_COUNT2		0xb
#define	HV_DRAM_CTL3		0xd
#define	HV_DRAM_COUNT3		0xe
#define	HV_DRAM_CTL4		0x10
#define	HV_DRAM_COUNT4		0x11
#define	HV_DRAM_CTL5		0x13
#define	HV_DRAM_COUNT5		0x14
#define	HV_DRAM_CTL6		0x16
#define	HV_DRAM_COUNT6		0x17
#define	HV_DRAM_CTL7		0x19
#define	HV_DRAM_COUNT7		0x1a

#endif

#ifdef VFALLS_IMPL
/*
 * Performance counters for Zambezi.  Zambezi is only supported with
 * Victoria Falls (UltraSPARC-T2+).
 */

#define	ZAMBEZI_PIC0_SEL_SHIFT		0x0
#define	ZAMBEZI_PIC1_SEL_SHIFT		0x8

#define	ZAMBEZI_LPU_COUNTERS		0x10
#define	ZAMBEZI_GPD_COUNTERS		0x4
#define	ZAMBEZI_ASU_COUNTERS		0x4

#define	HV_ZAM0_LPU_A_PCR		0x12
#define	HV_ZAM0_LPU_A_PIC0		0x13
#define	HV_ZAM0_LPU_A_PIC1		0x14
#define	HV_ZAM0_LPU_B_PCR		0x15
#define	HV_ZAM0_LPU_B_PIC0		0x16
#define	HV_ZAM0_LPU_B_PIC1		0x17
#define	HV_ZAM0_LPU_C_PCR		0x18
#define	HV_ZAM0_LPU_C_PIC0		0x19
#define	HV_ZAM0_LPU_C_PIC1		0x1a
#define	HV_ZAM0_LPU_D_PCR		0x1b
#define	HV_ZAM0_LPU_D_PIC0		0x1c
#define	HV_ZAM0_LPU_D_PIC1		0x1d
#define	HV_ZAM0_GPD_PCR			0x1e
#define	HV_ZAM0_GPD_PIC0		0x1f
#define	HV_ZAM0_GPD_PIC1		0x20
#define	HV_ZAM0_ASU_PCR			0x21
#define	HV_ZAM0_ASU_PIC0		0x22
#define	HV_ZAM0_ASU_PIC1		0x23

#define	HV_ZAM1_LPU_A_PCR		0x24
#define	HV_ZAM1_LPU_A_PIC0		0x25
#define	HV_ZAM1_LPU_A_PIC1		0x26
#define	HV_ZAM1_LPU_B_PCR		0x27
#define	HV_ZAM1_LPU_B_PIC0		0x28
#define	HV_ZAM1_LPU_B_PIC1		0x29
#define	HV_ZAM1_LPU_C_PCR		0x2a
#define	HV_ZAM1_LPU_C_PIC0		0x2b
#define	HV_ZAM1_LPU_C_PIC1		0x2c
#define	HV_ZAM1_LPU_D_PCR		0x2d
#define	HV_ZAM1_LPU_D_PIC0		0x2e
#define	HV_ZAM1_LPU_D_PIC1		0x2f
#define	HV_ZAM1_GPD_PCR			0x30
#define	HV_ZAM1_GPD_PIC0		0x31
#define	HV_ZAM1_GPD_PIC1		0x32
#define	HV_ZAM1_ASU_PCR			0x33
#define	HV_ZAM1_ASU_PIC0		0x34
#define	HV_ZAM1_ASU_PIC1		0x35

#define	HV_ZAM2_LPU_A_PCR		0x36
#define	HV_ZAM2_LPU_A_PIC0		0x37
#define	HV_ZAM2_LPU_A_PIC1		0x38
#define	HV_ZAM2_LPU_B_PCR		0x39
#define	HV_ZAM2_LPU_B_PIC0		0x3a
#define	HV_ZAM2_LPU_B_PIC1		0x3b
#define	HV_ZAM2_LPU_C_PCR		0x3c
#define	HV_ZAM2_LPU_C_PIC0		0x3d
#define	HV_ZAM2_LPU_C_PIC1		0x3e
#define	HV_ZAM2_LPU_D_PCR		0x3f
#define	HV_ZAM2_LPU_D_PIC0		0x40
#define	HV_ZAM2_LPU_D_PIC1		0x41
#define	HV_ZAM2_GPD_PCR			0x42
#define	HV_ZAM2_GPD_PIC0		0x43
#define	HV_ZAM2_GPD_PIC1		0x44
#define	HV_ZAM2_ASU_PCR			0x45
#define	HV_ZAM2_ASU_PIC0		0x46
#define	HV_ZAM2_ASU_PIC1		0x47

#define	HV_ZAM3_LPU_A_PCR		0x48
#define	HV_ZAM3_LPU_A_PIC0		0x49
#define	HV_ZAM3_LPU_A_PIC1		0x4a
#define	HV_ZAM3_LPU_B_PCR		0x4b
#define	HV_ZAM3_LPU_B_PIC0		0x4c
#define	HV_ZAM3_LPU_B_PIC1		0x4d
#define	HV_ZAM3_LPU_C_PCR		0x4e
#define	HV_ZAM3_LPU_C_PIC0		0x4f
#define	HV_ZAM3_LPU_C_PIC1		0x50
#define	HV_ZAM3_LPU_D_PCR		0x51
#define	HV_ZAM3_LPU_D_PIC0		0x52
#define	HV_ZAM3_LPU_D_PIC1		0x53
#define	HV_ZAM3_GPD_PCR			0x54
#define	HV_ZAM3_GPD_PIC0		0x55
#define	HV_ZAM3_GPD_PIC1		0x56
#define	HV_ZAM3_ASU_PCR			0x57
#define	HV_ZAM3_ASU_PIC0		0x58
#define	HV_ZAM3_ASU_PIC1		0x59

#endif

#ifndef _ASM
/*
 * prototypes for hypervisor interface to get/set SPARC and DRAM
 * performance counters
 */
extern uint64_t hv_niagara_setperf(uint64_t regnum, uint64_t val);
extern uint64_t hv_niagara_getperf(uint64_t regnum, uint64_t *val);
#endif

#ifdef __cplusplus
}
#endif

#endif /* _SYS_NIAGARA2REGS_H */
