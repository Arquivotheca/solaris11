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
 * Copyright (c) 1996, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SYS_PROCFS_ISA_H
#define	_SYS_PROCFS_ISA_H

/*
 * Instruction Set Architecture specific component of <sys/procfs.h>
 * i386 version
 */

#include <sys/regset.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Possible values of pr_dmodel.
 * This isn't isa-specific, but it needs to be defined here for other reasons.
 */
#define	PR_MODEL_UNKNOWN 0
#define	PR_MODEL_ILP32	1	/* process data model is ILP32 */
#define	PR_MODEL_LP64	2	/* process data model is LP64 */

/*
 * To determine whether application is running native.
 */
#if defined(_LP64)
#define	PR_MODEL_NATIVE	PR_MODEL_LP64
#elif defined(_ILP32)
#define	PR_MODEL_NATIVE	PR_MODEL_ILP32
#else
#error "No DATAMODEL_NATIVE specified"
#endif	/* _LP64 || _ILP32 */

#if defined(__i386) || defined(__amd64)
/*
 * Holds one i386 or amd64 instruction
 */
typedef	uchar_t instr_t;
#endif

#define	NPRGREG		_NGREG
#define	prgreg_t	greg_t
#define	prgregset_t	gregset_t
#define	prfpregset	fpu
#define	prfpregset_t	fpregset_t

#if defined(_SYSCALL32)
/*
 * kernel view of the ia32 register set
 */
typedef	uchar_t		instr32_t;
#if defined(__amd64)
#define	NPRGREG32	_NGREG32
#define	prgreg32_t	greg32_t
#define	prgregset32_t	gregset32_t
#define	prfpregset32	fpu32
#define	prfpregset32_t	fpregset32_t
#else
#define	NPRGREG32	_NGREG
#define	prgreg32_t	greg_t
#define	prgregset32_t	gregset_t
#define	prfpregset32	fpu
#define	prfpregset32_t	fpregset_t
#endif
#endif	/* _SYSCALL32 */

#if defined(__amd64)
/*
 * The following defines are for portability (see <sys/regset.h>).
 */
#define	R_PC	REG_RIP
#define	R_PS	REG_RFL
#define	R_SP	REG_RSP
#define	R_FP	REG_RBP
#define	R_R0	REG_RAX
#define	R_R1	REG_RDX
#elif defined(__i386)
/*
 * The following defines are for portability (see <sys/regset.h>).
 */
#define	R_PC	EIP
#define	R_PS	EFL
#define	R_SP	UESP
#define	R_FP	EBP
#define	R_R0	EAX
#define	R_R1	EDX
#endif

#define	XR_TYPE_XSAVE  0x101

typedef struct prxregset {
	uint32_t pr_type;
	uint32_t pr_align;
	uint32_t pr_xsize;	/* sizeof struct pr_xsave */
	uint32_t pr_pad;
	union {
		struct pr_xsave {
			uint16_t pr_fcw;
			uint16_t pr_fsw;
			uint16_t pr_fctw;
			uint16_t pr_fop;
#if defined(__amd64)
			uint64_t pr_rip;
			uint64_t pr_rdp;
#else
			uint32_t pr_eip;
			uint16_t pr_cs;
			uint16_t __pr_ign0;
			uint32_t pr_dp;
			uint16_t pr_ds;
			uint16_t __pr_ign1;
#endif
			uint32_t pr_mxcsr;
			uint32_t pr_mxcsr_mask;
			union {
				uint16_t pr_fpr_16[5];
				u_longlong_t pr_fpr_mmx;
				uint32_t __pr_fpr_pad[4];
			} pr_st[8];
#if defined(__amd64)
			upad128_t pr_xmm[16];
			upad128_t __pr_ign2[3];
#else
			upad128_t pr_xmm[8];
			upad128_t __pr_ign2[11];
#endif
			union {
				struct {
					uint64_t pr_xcr0;
					uint64_t pr_mbz[2];
				} pr_xsave_info;
				upad128_t __pr_pad[3];
			} pr_sw_avail;
			uint64_t pr_xstate_bv;
			uint64_t pr_rsv_mbz[2];
			uint64_t pr_reserved[5];
#if defined(__amd64)
			upad128_t pr_ymm[16];
#else
			upad128_t pr_ymm[8];
			upad128_t __pr_ign3[8];
#endif
		} pr_xsave;
	} pr_un;
} prxregset_t;

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_PROCFS_ISA_H */
