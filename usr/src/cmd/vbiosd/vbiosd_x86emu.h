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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _H_VBIOSD_X86EMU
#define	_H_VBIOSD_X86EMU

#ifdef  __cplusplus
extern "C" {
#endif

#include <x86emu.h>

#define	X86_EAX M.x86.R_EAX
#define	X86_EBX M.x86.R_EBX
#define	X86_ECX M.x86.R_ECX
#define	X86_EDX M.x86.R_EDX
#define	X86_ESI M.x86.R_ESI
#define	X86_EDI M.x86.R_EDI
#define	X86_EBP M.x86.R_EBP
#define	X86_EIP M.x86.R_EIP
#define	X86_ESP M.x86.R_ESP
#define	X86_EFLAGS M.x86.R_EFLG

#define	X86_FLAGS M.x86.R_FLG
#define	X86_AX M.x86.R_AX
#define	X86_BX M.x86.R_BX
#define	X86_CX M.x86.R_CX
#define	X86_DX M.x86.R_DX
#define	X86_SI M.x86.R_SI
#define	X86_DI M.x86.R_DI
#define	X86_BP M.x86.R_BP
#define	X86_IP M.x86.R_IP
#define	X86_SP M.x86.R_SP
#define	X86_CS M.x86.R_CS
#define	X86_DS M.x86.R_DS
#define	X86_ES M.x86.R_ES
#define	X86_SS M.x86.R_SS
#define	X86_FS M.x86.R_FS
#define	X86_GS M.x86.R_GS

#define	X86_AL M.x86.R_AL
#define	X86_BL M.x86.R_BL
#define	X86_CL M.x86.R_CL
#define	X86_DL M.x86.R_DL

#define	X86_AH M.x86.R_AH
#define	X86_BH M.x86.R_BH
#define	X86_CH M.x86.R_CH
#define	X86_DH M.x86.R_DH

#define	X86_TF_MASK	0x00000100
#define	X86_IF_MASK	0x00000200
#define	X86_IOPL_MASK	0x00003000
#define	X86_NT_MASK	0x00004000
#define	X86_VM_MASK	0x00020000
#define	X86_AC_MASK	0x00040000
#define	X86_VIF_MASK	0x00080000	/* virtual interrupt flag */
#define	X86_VIP_MASK	0x00100000	/* virtual interrupt pending */
#define	X86_ID_MASK	0x00200000

#define	DEFAULT_VBIOSD_FLAGS  (X86_IF_MASK | X86_IOPL_MASK)

#ifdef  __cplusplus
}
#endif

#endif /* _H_VBIOSD_X86EMU */
