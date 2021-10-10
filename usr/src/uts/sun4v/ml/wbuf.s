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
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/asm_linkage.h>
#include <sys/machthread.h>
#include <sys/privregs.h>
#include <sys/machasi.h>
#include <sys/trap.h>
#include <sys/mmu.h>
#include <sys/machparam.h>
#include <sys/machtrap.h>
#include <sys/traptrace.h>
#include <sys/hrt.h>

#if !defined(lint)
#include "assym.h"

	/*
	 * Spill fault handlers
	 *   sn0 - spill normal tl 0
	 *   sn1 - spill normal tl >0
	 *   so0 - spill other tl 0
	 *   so1 - spill other tl >0
	 */

	ENTRY_NP(fault_32bit_sn0)
	!
	FAULT_WINTRACE(%g1, %g2, %g3, TT_F32_SN0)
	!
	! Spill normal tl0 fault.
	! This happens when a user tries to spill to an unmapped or
	! misaligned stack. We handle an unmapped stack by simulating
	! a pagefault at the trap pc and a misaligned stack by generating
	! a user alignment trap.
	!
	! spill the window into wbuf slot 0
	! (we know wbuf is empty since we came from user mode)
	!
	! g5 = mmu trap type, g6 = tag access reg (g5 != T_ALIGNMENT) or
	! sfar (g5 == T_ALIGNMENT)
	!
	CPU_ADDR(%g4, %g1)
	ldn	[%g4 + CPU_MPCB], %g1
	stn	%sp, [%g1 + MPCB_SPBUF]
	ldn	[%g1 + MPCB_WBUF], %g2
	SAVE_V8WINDOW(%g2)
	mov	1, %g2
	st	%g2, [%g1 + MPCB_WBCNT]
	saved
	!
	! setup user_trap args
	!
	set	sfmmu_tsbmiss_exception, %g1
	mov	%g6, %g2			! arg2 = tagaccess
	mov	T_WIN_OVERFLOW, %g3		! arg3 = traptype
	cmp	%g5, T_ALIGNMENT
	bne	%icc, 1f
	nop
	set	trap, %g1
	mov	T_ALIGNMENT, %g3
1:
	sub	%g0, 1, %g4
	!
	! spill traps increment %cwp by 2,
	! but user_trap wants the trap %cwp
	! 
	rdpr	%tstate, %g5
	and	%g5, TSTATE_CWP, %g5
	ba,pt	%xcc, user_trap
	wrpr	%g0, %g5, %cwp	
	SET_SIZE(fault_32bit_sn0)

	!
	! Spill normal tl1 fault.
	! This happens when sys_trap's save spills to an unmapped stack.
	! We handle it by spilling the window to the wbuf and trying
	! sys_trap again.
	!
	! spill the window into wbuf slot 0
	! (we know wbuf is empty since we came from user mode)
	!
	ENTRY_NP(fault_32bit_sn1)
	FAULT_WINTRACE(%g5, %g6, %g7, TT_F32_SN1)
	CPU_PADDR(%g5, %g6)
	mov	ASI_MEM, %asi
	ldxa	[%g5 + CPU_MPCB_PA]%asi, %g6
	ldxa	[%g6 + MPCB_WBUF_PA]%asi, %g5
	stna	%sp, [%g6 + MPCB_SPBUF]%asi
	SAVE_V8WINDOW_ASI(%g5)
	mov	1, %g5
	sta	%g5, [%g6 + MPCB_WBCNT]%asi
	saved
	set	sys_trap, %g5
	wrpr	%g5, %tnpc
	done
	SET_SIZE(fault_32bit_sn1)

	ENTRY_NP(fault_32bit_so0)
	!
	FAULT_WINTRACE(%g5, %g6, %g1, TT_F32_SO0)
	!
	! Spill other tl0 fault.
	! This happens when the kernel spills a user window and that
	! user's stack has been unmapped.
	! We handle it by spilling the window into the user's wbuf.
	!
	! find lwp & increment wbcnt
	!
	CPU_ADDR(%g5, %g6)
	ldn	[%g5 + CPU_MPCB], %g1
	ld	[%g1 + MPCB_WBCNT], %g2
	add	%g2, 1, %g3
	st	%g3, [%g1 + MPCB_WBCNT]
	!
	! use previous wbcnt to spill new spbuf & wbuf
	!
	sll	%g2, CPTRSHIFT, %g4		! spbuf size is sizeof (caddr_t)
	add	%g1, MPCB_SPBUF, %g3
	stn	%sp, [%g3 + %g4]
	sll	%g2, RWIN32SHIFT, %g4
	ldn	[%g1 + MPCB_WBUF], %g3
	add	%g3, %g4, %g3
	SAVE_V8WINDOW(%g3)
	saved
	retry
	SET_SIZE(fault_32bit_so0)

	!
	! Spill other tl1 fault.
	! This happens when priv_trap spills a user window and that
	! user's stack has been unmapped.
	! We handle it by spilling the window to the wbuf and retrying
	! the save.
	!
	ENTRY_NP(fault_32bit_so1)
	FAULT_WINTRACE(%g5, %g6, %g7, TT_F32_SO1)
	CPU_PADDR(%g5, %g6)
	!
	! find lwp & increment wbcnt
	!
	mov	ASI_MEM, %asi
	ldxa	[%g5 + CPU_MPCB_PA]%asi, %g6
	lda	[%g6 + MPCB_WBCNT]%asi, %g5
	add	%g5, 1, %g7
	sta	%g7, [%g6 + MPCB_WBCNT]%asi
	!
	! use previous wbcnt to spill new spbuf & wbuf
	!
	sll	%g5, CPTRSHIFT, %g7		! spbuf size is sizeof (caddr_t)
	add	%g6, %g7, %g7
	stna	%sp, [%g7 + MPCB_SPBUF]%asi
	sll	%g5, RWIN32SHIFT, %g7
	ldxa	[%g6 + MPCB_WBUF_PA]%asi, %g5
	add	%g5, %g7, %g7
	SAVE_V8WINDOW_ASI(%g7)
	saved
	set	sys_trap, %g5
	wrpr	%g5, %tnpc
	done
	SET_SIZE(fault_32bit_so1)

	ENTRY_NP(fault_64bit_sn0)
	!
	FAULT_WINTRACE(%g1, %g2, %g3, TT_F64_SN0)
	!
	! Spill normal tl0 fault.
	! This happens when a user tries to spill to an unmapped or
	! misaligned stack. We handle an unmapped stack by simulating
	! a pagefault at the trap pc and a misaligned stack by generating
	! a user alignment trap.
	!
	! spill the window into wbuf slot 0
	! (we know wbuf is empty since we came from user mode)
	!
	! g5 = mmu trap type, g6 = tag access reg (g5 != T_ALIGNMENT) or
	! sfar (g5 == T_ALIGNMENT)
	!
	CPU_ADDR(%g4, %g1)
	ldn	[%g4 + CPU_MPCB], %g1
	stn	%sp, [%g1 + MPCB_SPBUF]
	ldn	[%g1 + MPCB_WBUF], %g2
	SAVE_V9WINDOW(%g2)
	mov	1, %g2
	st	%g2, [%g1 + MPCB_WBCNT]
	saved
	!
	! setup user_trap args
	!
	set	sfmmu_tsbmiss_exception, %g1
	mov	%g6, %g2			! arg2 = tagaccess
	mov	%g5, %g3			! arg3 = traptype
	cmp	%g5, T_ALIGNMENT
	bne	%icc, 1f
	nop
	set	trap, %g1
	mov	T_ALIGNMENT, %g3
1:
	sub	%g0, 1, %g4
	!
	! spill traps increment %cwp by 2,
	! but user_trap wants the trap %cwp
	! 
	rdpr	%tstate, %g5
	and	%g5, TSTATE_CWP, %g5
	ba,pt	%xcc, user_trap
	  wrpr	%g0, %g5, %cwp	
	SET_SIZE(fault_64bit_sn0)

	!
	! Spill normal tl1 fault.
	! This happens when sys_trap's save spills to an unmapped stack.
	! We handle it by spilling the window to the wbuf and trying
	! sys_trap again.
	!
	! spill the window into wbuf slot 0
	! (we know wbuf is empty since we came from user mode)
	!
	ENTRY_NP(fault_64bit_sn1)
	FAULT_WINTRACE(%g5, %g6, %g7, TT_F64_SN1)
	CPU_PADDR(%g5, %g6)
	mov	ASI_MEM, %asi
	ldxa	[%g5 + CPU_MPCB_PA]%asi, %g6
	ldxa	[%g6 + MPCB_WBUF_PA]%asi, %g5
	stna	%sp, [%g6 + MPCB_SPBUF]%asi
	SAVE_V9WINDOW_ASI(%g5)
	mov	1, %g5
	sta	%g5, [%g6 + MPCB_WBCNT]%asi
	saved
	set	sys_trap, %g5
	wrpr	%g5, %tnpc
	done
	SET_SIZE(fault_64bit_sn1)

	!
	! Spill normal kernel tl1.
	!
	! spill the kernel window into kwbuf
	!
	ENTRY_NP(fault_32bit_sk)
	FAULT_WINTRACE(%g5, %g6, %g7, TT_F32_NT1)
	CPU_PADDR(%g5, %g6)
	set	CPU_KWBUF_SP, %g6
	add	%g5, %g6, %g6
	mov	ASI_MEM, %asi
	stna	%sp, [%g6]%asi
	set	CPU_KWBUF, %g6
	add	%g5, %g6, %g6
	SAVE_V8WINDOW_ASI(%g6)
	mov	1, %g6
	add	%g5, CPU_MCPU, %g5
#ifdef DEBUG
	lda	[%g5 + MCPU_KWBUF_FULL]%asi, %g7
	tst	%g7
	bnz,a,pn %icc, ptl1_panic
	  mov	PTL1_BAD_WTRAP, %g1
#endif /* DEBUG */
	sta	%g6, [%g5 + MCPU_KWBUF_FULL]%asi
	saved
	retry
	SET_SIZE(fault_32bit_sk)

	!
	! Spill normal kernel tl1.
	!
	! spill the kernel window into kwbuf
	!
	ENTRY_NP(fault_64bit_sk)
	!
	FAULT_WINTRACE(%g5, %g6, %g7, TT_F64_NT1)
	CPU_PADDR(%g5, %g6)
	set	CPU_KWBUF_SP, %g6
	add	%g5, %g6, %g6
	mov	ASI_MEM, %asi
	stna	%sp, [%g6]%asi
	set	CPU_KWBUF, %g6
	add	%g5, %g6, %g6
	SAVE_V9WINDOW_ASI(%g6)
	mov	1, %g6
	add	%g5, CPU_MCPU, %g5
#ifdef DEBUG
	lda	[%g5 + MCPU_KWBUF_FULL]%asi, %g7
	tst	%g7
	bnz,a,pn %icc, ptl1_panic
	  mov	PTL1_BAD_WTRAP, %g1
#endif /* DEBUG */
	sta	%g6, [%g5 + MCPU_KWBUF_FULL]%asi
	saved
	retry
	SET_SIZE(fault_64bit_sk)

	ENTRY_NP(fault_64bit_so0)
	!
	FAULT_WINTRACE(%g5, %g6, %g1, TT_F64_SO0)
	!
	! Spill other tl0 fault.
	! This happens when the kernel spills a user window and that
	! user's stack has been unmapped.
	! We handle it by spilling the window into the user's wbuf.
	!
	! find lwp & increment wbcnt
	!
	CPU_ADDR(%g5, %g6)
	ldn	[%g5 + CPU_MPCB], %g1
	ld	[%g1 + MPCB_WBCNT], %g2
	add	%g2, 1, %g3
	st	%g3, [%g1 + MPCB_WBCNT]
	!
	! use previous wbcnt to spill new spbuf & wbuf
	!
	sll	%g2, CPTRSHIFT, %g4		! spbuf size is sizeof (caddr_t)
	add	%g1, MPCB_SPBUF, %g3
	stn	%sp, [%g3 + %g4]
	sll	%g2, RWIN64SHIFT, %g4
	ldn	[%g1 + MPCB_WBUF], %g3
	add	%g3, %g4, %g3
	SAVE_V9WINDOW(%g3)
	saved
	retry
	SET_SIZE(fault_64bit_so0)

	!
	! Spill other tl1 fault.
	! This happens when priv_trap spills a user window and that
	! user's stack has been unmapped.
	! We handle it by spilling the window to the wbuf and retrying
	! the save.
	!
	ENTRY_NP(fault_64bit_so1)
	FAULT_WINTRACE(%g5, %g6, %g7, TT_F64_SO1)
	CPU_PADDR(%g5, %g6)
	!
	! find lwp & increment wbcnt
	!
	mov	ASI_MEM, %asi
	ldxa	[%g5 + CPU_MPCB_PA]%asi, %g6
	lda	[%g6 + MPCB_WBCNT]%asi, %g5
	add	%g5, 1, %g7
	sta	%g7, [%g6 + MPCB_WBCNT]%asi
	!
	! use previous wbcnt to spill new spbuf & wbuf
	!
	sll	%g5, CPTRSHIFT, %g7		! spbuf size is sizeof (caddr_t)
	add	%g6, %g7, %g7
	stna	%sp, [%g7 + MPCB_SPBUF]%asi
	sll	%g5, RWIN64SHIFT, %g7
	ldxa	[%g6 + MPCB_WBUF_PA]%asi, %g5
	add	%g5, %g7, %g7
	SAVE_V9WINDOW_ASI(%g7)
	saved
	set	sys_trap, %g5
	wrpr	%g5, %tnpc
	done
	SET_SIZE(fault_64bit_so1)

	/*
	 * Fill fault handlers
	 *   fn0 - fill normal tl 0
	 *   fn1 - fill normal tl 1
	 */

	ENTRY_NP(fault_32bit_fn0)
	!
	FAULT_WINTRACE(%g1, %g2, %g3, TT_F32_FN0)
	!
.fault_fn0_common:
	!
	! Fill normal tl0 fault.
	! This happens when a user tries to fill to an unmapped or
	! misaligned stack. We handle an unmapped stack by simulating
	! a pagefault at the trap pc and a misaligned stack by generating
	! a user alignment trap.
	!
	! setup user_trap args
	!
	! g5 = mmu trap type, g6 = tag access reg (g5 != T_ALIGNMENT) or
	! sfar (g5 == T_ALIGNMENT)
	!
	set	sfmmu_tsbmiss_exception, %g1
	mov	%g6, %g2			! arg2 = tagaccess
	mov	T_WIN_UNDERFLOW, %g3
	cmp	%g5, T_ALIGNMENT
	bne	%icc, 1f
	nop
	set	trap, %g1
	mov	T_ALIGNMENT, %g3
1:
	sub	%g0, 1, %g4
	!
	! sys_trap wants %cwp to be the same as when the trap occured,
	! so set it from %tstate
	!
	rdpr	%tstate, %g5
	and	%g5, TSTATE_CWP, %g5
	ba,pt	%xcc, user_trap
	wrpr	%g0, %g5, %cwp
	SET_SIZE(fault_32bit_fn0)

	ENTRY_NP(fault_32bit_fn1)
	!
	FAULT_WINTRACE(%g1, %g2, %g3, TT_F32_FN1)
	!
	wrpr	%g0, 1, %gl
	srl	%sp, 0, %g7
	!
.fault_fn1_common:	
	!
	! Fill normal tl1 fault.
	! This happens when user_rtt's restore fills from an unmapped or
	! misaligned stack. We handle an unmapped stack by simulating
	! a pagefault at user_rtt and a misaligned stack by generating
	! a RTT alignment trap.
	!
	! save fault addr & fix %cwp
	!
	rdpr	%tstate, %g1
	and	%g1, TSTATE_CWP, %g1
	wrpr	%g0, %g1, %cwp
	!
	! fake tl1 traps regs so that after pagefault runs, we
	! re-execute at user_rtt.
	!
	wrpr	%g0, 1, %tl
	set	TSTATE_KERN | TSTATE_IE, %g1
	wrpr	%g0, %g1, %tstate
	set	user_rtt, %g1
	wrpr	%g0, %g1, %tpc
	add	%g1, 4, %g1
	wrpr	%g0, %g1, %tnpc
	!
	! setup sys_trap args
	!
	! g5 = mmu trap type, g6 = tag access reg (g5 != T_ALIGNMENT) or
	! sfar (g5 == T_ALIGNMENT)
	!
	set	sfmmu_tsbmiss_exception, %g1
	mov	%g6, %g2			! arg2 = tagaccess
	set	T_USER | T_SYS_RTT_PAGE, %g3	! arg3 = traptype
	cmp	%g5, T_ALIGNMENT
	bne	%icc, 1f
	nop
	set	trap, %g1
	set	T_USER | T_SYS_RTT_ALIGN, %g3
1:
	sub	%g0, 1, %g4
	!
	! setup to run kernel again by setting THREAD_REG, %wstate
	! and the mmu to their kernel values.
	!
	! sun4v cannot safely lower %gl then raise it again
	! so ktl0 must restore THREAD_REG
	rdpr	%wstate, %l1
	sllx	%l1, WSTATE_SHIFT, %l1
	wrpr	%l1, WSTATE_K64, %wstate
	mov	KCONTEXT, %g5
	mov	MMU_PCONTEXT, %g6
	stxa	%g5, [%g6]ASI_MMU_CTX
	membar	#Sync

	ba,pt	%xcc, priv_trap
	nop
	SET_SIZE(fault_32bit_fn1)

	ENTRY_NP(fault_64bit_fn0)
	FAULT_WINTRACE(%g1, %g2, %g3, TT_F64_FN0)
	b	.fault_fn0_common
	  nop
	SET_SIZE(fault_64bit_fn0)

	ENTRY_NP(fault_64bit_fn1)
	FAULT_WINTRACE(%g1, %g2, %g3, TT_F64_FN1)
	wrpr	%g0, 1, %gl
	b	.fault_fn1_common
	  nop
	SET_SIZE(fault_64bit_fn1)

	ENTRY_NP(fault_rtt_fn1)
	FAULT_WINTRACE(%g1, %g2, %g3, TT_RTT_FN1)
	wrpr	%g0, 1, %gl
	b	.fault_fn1_common
	  nop
	SET_SIZE(fault_rtt_fn1)

	/*
	 * Kernel fault handlers
	 */
	ENTRY_NP(fault_32bit_not)
	ENTRY_NP(fault_64bit_not)
	ba,pt	%xcc, ptl1_panic
	mov	PTL1_BAD_WTRAP, %g1
	SET_SIZE(fault_32bit_not)
	SET_SIZE(fault_64bit_not)
#endif /* !lint */
