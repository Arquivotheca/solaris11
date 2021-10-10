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

#ifndef	_SYS_MACHPRIVREGS_H
#define	_SYS_MACHPRIVREGS_H

#include <sys/hypervisor.h>

/*
 * Platform dependent instruction sequences for manipulating
 * privileged state
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * CLI and STI are quite complex to virtualize!
 */

#define	CURVCPU(r)					\
	movq	%gs:CPU_VCPU_INFO, r

#define	CURTHREAD(r)					\
	movq	%gs:CPU_THREAD, r

#define	XEN_TEST_EVENT_PENDING(r)			\
	testb	$0xff, VCPU_INFO_EVTCHN_UPCALL_PENDING(r)

#define	XEN_SET_UPCALL_MASK(r)				\
	movb	$1, VCPU_INFO_EVTCHN_UPCALL_MASK(r)

#define	XEN_GET_UPCALL_MASK(r, mask)			\
	movb	VCPU_INFO_EVTCHN_UPCALL_MASK(r), mask

#define	XEN_TEST_UPCALL_MASK(r)				\
	testb	$1, VCPU_INFO_EVTCHN_UPCALL_MASK(r)

#define	XEN_CLEAR_UPCALL_MASK(r)			\
	ASSERT_UPCALL_MASK_IS_SET;			\
	movb	$0, VCPU_INFO_EVTCHN_UPCALL_MASK(r)

#ifdef DEBUG

/*
 * Much logic depends on the upcall mask being set at
 * various points in the code; use this macro to validate.
 *
 * Need to use CURVCPU(r) to establish the vcpu pointer.
 */
#define	ASSERT_UPCALL_MASK_IS_SET			\
	pushq	%r11;					\
	CURVCPU(%r11);					\
	XEN_TEST_UPCALL_MASK(%r11);			\
	jne	6f;					\
	cmpl	$0, stistipanic(%rip);			\
	jle	6f;					\
	movl	$-1, stistipanic(%rip);			\
	movq	stistimsg(%rip), %rdi;			\
	xorl	%eax, %eax;				\
	call	panic;					\
6:	pushq	%rax;					\
	pushq	%rbx;					\
	movl	%gs:CPU_ID, %eax;			\
	leaq	.+0(%rip), %r11;			\
	leaq	laststi(%rip), %rbx;			\
	movq	%r11, (%rbx, %rax, 8);			\
	popq	%rbx;					\
	popq	%rax;					\
	popq	%r11

#define	SAVE_CLI_LOCATION				\
	pushq	%rax;					\
	pushq	%rbx;					\
	pushq	%rcx;					\
	movl	%gs:CPU_ID, %eax;			\
	leaq	.+0(%rip), %rcx;			\
	leaq	lastcli, %rbx;				\
	movq	%rcx, (%rbx, %rax, 8);			\
	popq	%rcx;					\
	popq	%rbx;					\
	popq	%rax;					\

#else	/* DEBUG */

#define	ASSERT_UPCALL_MASK_IS_SET	/* empty */
#define	SAVE_CLI_LOCATION		/* empty */

#endif	/* DEBUG */

#define	KPREEMPT_DISABLE(t)				\
	addb	$1, T_PREEMPT(t)

#define	KPREEMPT_ENABLE_NOKP(t)				\
	subb	$1, T_PREEMPT(t)

#define	CLI(r)						\
	CURTHREAD(r);					\
	KPREEMPT_DISABLE(r);				\
	CURVCPU(r);					\
	XEN_SET_UPCALL_MASK(r);				\
	SAVE_CLI_LOCATION;				\
	CURTHREAD(r);					\
	KPREEMPT_ENABLE_NOKP(r)

#define	CLIRET(r, ret)					\
	CURTHREAD(r);					\
	KPREEMPT_DISABLE(r);				\
	CURVCPU(r);					\
	XEN_GET_UPCALL_MASK(r, ret);			\
	XEN_SET_UPCALL_MASK(r);				\
	SAVE_CLI_LOCATION;				\
	CURTHREAD(r);					\
	KPREEMPT_ENABLE_NOKP(r)

/*
 * We use the fact that HYPERVISOR_block will clear the upcall mask
 * for us and then give us an upcall if there is a pending event
 * to achieve getting a callback on this cpu without the danger of
 * being preempted and migrating to another cpu between the upcall
 * enable and the callback delivery.
 */
#define	STI_CLOBBER		/* clobbers %rax, %rdi, %r11 */		\
	CURVCPU(%r11);							\
	ASSERT_UPCALL_MASK_IS_SET;					\
	movw	$0x100, %ax;	/* assume mask set, pending clear */	\
	movw	$0, %di;	/* clear mask and pending */		\
	lock;								\
	cmpxchgw %di, VCPU_INFO_EVTCHN_UPCALL_PENDING(%r11);		\
	jz	7f;		/* xchg worked, we're done */		\
	movl	$__HYPERVISOR_sched_op, %eax; /* have pending upcall */	\
	movl	$SCHEDOP_block, %edi;					\
	pushq	%rsi;	/* hypercall clobbers C param regs plus r10 */	\
	pushq	%rcx;							\
	pushq	%rdx;							\
	pushq	%r8;							\
	pushq	%r9;							\
	pushq	%r10;							\
	TRAP_INSTR;	/* clear upcall mask, force upcall */ 		\
	popq	%r10;							\
	popq	%r9;							\
	popq	%r8;							\
	popq	%rdx;							\
	popq	%rcx;							\
	popq	%rsi;							\
7:

#define	STI								\
	pushq	%r11;							\
	pushq	%rdi;							\
	pushq	%rax;							\
	STI_CLOBBER;	/* clobbers %r11, %rax, %rdi */			\
	popq	%rax;							\
	popq	%rdi;							\
	popq	%r11

/*
 * Map the PS_IE bit to the hypervisor's event mask bit
 * To -set- the event mask, we have to do a CLI
 * To -clear- the event mask, we have to do a STI
 * (with all the accompanying pre-emption and callbacks, ick)
 *
 * And vice versa.
 */

#define	IE_TO_EVENT_MASK(rtmp, rfl)		\
	testq	$PS_IE, rfl;			\
	jnz	4f;				\
	CLI(rtmp);				\
	jmp	5f;				\
4:	STI;					\
5:

#define	EVENT_MASK_TO_IE(rtmp, rfl)		\
	andq	$_BITNOT(PS_IE), rfl;		\
	CURVCPU(rtmp);				\
	XEN_TEST_UPCALL_MASK(rtmp);		\
	jnz	1f;				\
	orq	$PS_IE, rfl;			\
1:

/*
 * Used to re-enable interrupts in the body of exception handlers
 */

#define	ENABLE_INTR_FLAGS		\
	pushq	$F_ON;			\
	popfq;				\
	STI

/*
 * Virtualize IRET and SYSRET
 */

#if defined(DEBUG)

/*
 * Die nastily with a #ud trap if we are about to switch to user
 * mode in HYPERVISOR_IRET and RUPDATE_PENDING is set.
 */
#define	__ASSERT_NO_RUPDATE_PENDING			\
	pushq	%r15;					\
	cmpw	$KCS_SEL, 0x10(%rsp);			\
	je	1f;					\
	movq	%gs:CPU_THREAD, %r15;			\
	movq	T_LWP(%r15), %r15;			\
	testb	$0x1, PCB_RUPDATE(%r15);		\
	je	1f;					\
	ud2;						\
1:	popq	%r15

#else	/* DEBUG */

#define	__ASSERT_NO_RUPDATE_PENDING

#endif	/* DEBUG */

/*
 * Switching from guest kernel to user mode.
 * flag == VGCF_IN_SYSCALL => return via sysret
 * flag == 0 => return via iretq
 *
 * See definition in public/arch-x86_64.h. Stack going in must be:
 * rax, r11, rcx, flags, rip, cs, rflags, rsp, ss.
 */
#define	HYPERVISOR_IRET(flag)			\
	__ASSERT_NO_RUPDATE_PENDING;		\
	pushq	$flag;				\
	pushq	%rcx;				\
	pushq	%r11;				\
	pushq	%rax;				\
	movl	$__HYPERVISOR_iret, %eax;	\
	syscall;				\
	ud2	/* die nastily if we return! */

#define	IRET	HYPERVISOR_IRET(0)

/*
 * XXPV: Normally we would expect to use sysret to return from kernel to
 *       user mode when using the syscall instruction. The iret hypercall
 *       does support both iret and sysret semantics. For us to use sysret
 *	 style would require that we use the hypervisor's private descriptors
 *	 that obey syscall instruction's imposed segment selector ordering.
 *	 With iret we can use whatever %cs value we choose. We should fix
 *	 this to use sysret one day.
 */
#define	SYSRETQ	HYPERVISOR_IRET(0)
#define	SYSRETL	ud2		/* 32-bit syscall/sysret not supported */
#define	SWAPGS	/* empty - handled in hypervisor */


/*
 * Xen 3.x wedges the current value of upcall_mask into unused byte of
 * saved %cs on stack at the time of passing through a trap or interrupt
 * gate.  Since Xen also updates PS_IE in %[e,r]lags as well, we always
 * mask off the saved upcall mask so the kernel and/or tools like debuggers
 * will not be confused about bits set in reserved portions of %cs slot.
 *
 * See xen/include/public/arch-x86_[32,64].h:cpu_user_regs_t for details.
 */
#define	CLEAN_CS	movb	$0, REGOFF_CS+4(%rsp)

/*
 * All exceptions for amd64 have %r11 and %rcx on the stack.
 * Just pop them back into their appropriate registers and
 * let it get saved as is running native.
 */
#define	XPV_TRAP_POP	\
	popq	%rcx;	\
	popq	%r11

#define	XPV_TRAP_PUSH	\
	pushq	%r11;	\
	pushq	%rcx


/*
 * Macros for saving the original segment registers and restoring them
 * for fast traps.
 */

/*
 * Smaller versions of INTR_PUSH and INTR_POP for fast traps.
 * The following registers have been pushed onto the stack by
 * hardware at this point:
 *
 *	greg_t	r_rip;
 *	greg_t	r_cs;
 *	greg_t	r_rfl;
 *	greg_t	r_rsp;
 *	greg_t	r_ss;
 *
 * This handler is executed both by 32-bit and 64-bit applications.
 * 64-bit applications allow us to treat the set (%rdi, %rsi, %rdx,
 * %rcx, %r8, %r9, %r10, %r11, %rax) as volatile across function calls.
 * However, 32-bit applications only expect (%eax, %edx, %ecx) to be volatile
 * across a function call -- in particular, %esi and %edi MUST be saved!
 *
 * We could do this differently by making a FAST_INTR_PUSH32 for 32-bit
 * programs, and FAST_INTR_PUSH for 64-bit programs, but it doesn't seem
 * particularly worth it.
 *
 */
#define	FAST_INTR_PUSH			\
	INTGATE_INIT_KERNEL_FLAGS;	\
	popq	%rcx;			\
	popq	%r11;			\
	subq    $REGOFF_RIP, %rsp;	\
	movq    %rsi, REGOFF_RSI(%rsp);	\
	movq    %rdi, REGOFF_RDI(%rsp);	\
	CLEAN_CS

#define	FAST_INTR_POP			\
	movq    REGOFF_RSI(%rsp), %rsi;	\
	movq    REGOFF_RDI(%rsp), %rdi;	\
	addq    $REGOFF_RIP, %rsp

#define	FAST_INTR_RETURN		\
	ASSERT_UPCALL_MASK_IS_SET;	\
	HYPERVISOR_IRET(0)


/*
 * Handling the CR0.TS bit for floating point handling.
 *
 * When the TS bit is *set*, attempts to touch the floating
 * point hardware will result in a #nm trap.
 */
#define	STTS(rtmp)				\
	pushq	%rdi;				\
	movl	$1, %edi;			\
	call	HYPERVISOR_fpu_taskswitch;	\
	popq	%rdi

#define	CLTS					\
	pushq	%rdi;				\
	xorl	%edi, %edi;			\
	call	HYPERVISOR_fpu_taskswitch;	\
	popq	%rdi

#ifdef __cplusplus
}
#endif

#endif	/* _SYS_MACHPRIVREGS_H */
