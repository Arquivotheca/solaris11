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
 * Copyright (c) 1992, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 *  Copyright (c) 1990, 1991 UNIX System Laboratories, Inc.
 *  Copyright (c) 1984, 1986, 1987, 1988, 1989, 1990 AT&T
 *    All Rights Reserved
 */

/*
 * Copyright (c) 2009, Intel Corporation.
 * All rights reserved.
 */

/*
 * General assembly language routines.
 * It is the intent of this file to contain routines that are
 * independent of the specific kernel architecture, and those that are
 * common across kernel architectures.
 * As architectures diverge, and implementations of specific
 * architecture-dependent routines change, the routines should be moved
 * from this file into the respective ../`arch -k`/subr.s file.
 */

#include <sys/asm_linkage.h>
#include <sys/asm_misc.h>
#include <sys/panic.h>
#include <sys/ontrap.h>
#include <sys/regset.h>
#include <sys/privregs.h>
#include <sys/reboot.h>
#include <sys/psw.h>
#include <sys/x86_archext.h>

#if defined(__lint)
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/thread.h>
#include <sys/archsystm.h>
#include <sys/byteorder.h>
#include <sys/dtrace.h>
#include <sys/ftrace.h>
#else	/* __lint */
#include "assym.h"
#endif	/* __lint */
#include <sys/dditypes.h>

/*
 * on_fault()
 * Catch lofault faults. Like setjmp except it returns one
 * if code following causes uncorrectable fault. Turned off
 * by calling no_fault().
 */

#if defined(__lint)

/* ARGSUSED */
int
on_fault(label_t *ljb)
{ return (0); }

void
no_fault(void)
{}

#else	/* __lint */

	ENTRY(on_fault)
	movq	%gs:CPU_THREAD, %rsi
	leaq	catch_fault(%rip), %rdx
	movq	%rdi, T_ONFAULT(%rsi)		/* jumpbuf in t_onfault */
	movq	%rdx, T_LOFAULT(%rsi)		/* catch_fault in t_lofault */
	jmp	setjmp				/* let setjmp do the rest */

catch_fault:
	movq	%gs:CPU_THREAD, %rsi
	movq	T_ONFAULT(%rsi), %rdi		/* address of save area */
	xorl	%eax, %eax
	movq	%rax, T_ONFAULT(%rsi)		/* turn off onfault */
	movq	%rax, T_LOFAULT(%rsi)		/* turn off lofault */
	jmp	longjmp				/* let longjmp do the rest */
	SET_SIZE(on_fault)

	ENTRY(no_fault)
	movq	%gs:CPU_THREAD, %rsi
	xorl	%eax, %eax
	movq	%rax, T_ONFAULT(%rsi)		/* turn off onfault */
	movq	%rax, T_LOFAULT(%rsi)		/* turn off lofault */
	ret
	SET_SIZE(no_fault)

#endif	/* __lint */

/*
 * Default trampoline code for on_trap() (see <sys/ontrap.h>).  We just
 * do a longjmp(&curthread->t_ontrap->ot_jmpbuf) if this is ever called.
 */

#if defined(lint)

void
on_trap_trampoline(void)
{}

#else	/* __lint */

	ENTRY(on_trap_trampoline)
	movq	%gs:CPU_THREAD, %rsi
	movq	T_ONTRAP(%rsi), %rdi
	addq	$OT_JMPBUF, %rdi
	jmp	longjmp
	SET_SIZE(on_trap_trampoline)

#endif	/* __lint */

/*
 * Push a new element on to the t_ontrap stack.  Refer to <sys/ontrap.h> for
 * more information about the on_trap() mechanism.  If the on_trap_data is the
 * same as the topmost stack element, we just modify that element.
 */
#if defined(lint)

/*ARGSUSED*/
int
on_trap(on_trap_data_t *otp, uint_t prot)
{ return (0); }

#else	/* __lint */

	ENTRY(on_trap)
	movw	%si, OT_PROT(%rdi)		/* ot_prot = prot */
	movw	$0, OT_TRAP(%rdi)		/* ot_trap = 0 */
	leaq	on_trap_trampoline(%rip), %rdx	/* rdx = &on_trap_trampoline */
	movq	%rdx, OT_TRAMPOLINE(%rdi)	/* ot_trampoline = rdx */
	xorl	%ecx, %ecx
	movq	%rcx, OT_HANDLE(%rdi)		/* ot_handle = NULL */
	movq	%rcx, OT_PAD1(%rdi)		/* ot_pad1 = NULL */
	movq	%gs:CPU_THREAD, %rdx		/* rdx = curthread */
	movq	T_ONTRAP(%rdx), %rcx		/* rcx = curthread->t_ontrap */
	cmpq	%rdi, %rcx			/* if (otp == %rcx)	*/
	je	0f				/*	don't modify t_ontrap */

	movq	%rcx, OT_PREV(%rdi)		/* ot_prev = t_ontrap */
	movq	%rdi, T_ONTRAP(%rdx)		/* curthread->t_ontrap = otp */

0:	addq	$OT_JMPBUF, %rdi		/* &ot_jmpbuf */
	jmp	setjmp
	SET_SIZE(on_trap)

#endif	/* __lint */

/*
 * Setjmp and longjmp implement non-local gotos using state vectors
 * type label_t.
 */

#if defined(__lint)

/* ARGSUSED */
int
setjmp(label_t *lp)
{ return (0); }

/* ARGSUSED */
void
longjmp(label_t *lp)
{}

#else	/* __lint */

#if LABEL_PC != 0
#error LABEL_PC MUST be defined as 0 for setjmp/longjmp to work as coded
#endif	/* LABEL_PC != 0 */

	ENTRY(setjmp)
	movq	%rsp, LABEL_SP(%rdi)
	movq	%rbp, LABEL_RBP(%rdi)
	movq	%rbx, LABEL_RBX(%rdi)
	movq	%r12, LABEL_R12(%rdi)
	movq	%r13, LABEL_R13(%rdi)
	movq	%r14, LABEL_R14(%rdi)
	movq	%r15, LABEL_R15(%rdi)
	movq	(%rsp), %rdx		/* return address */
	movq	%rdx, (%rdi)		/* LABEL_PC is 0 */
	xorl	%eax, %eax		/* return 0 */
	ret
	SET_SIZE(setjmp)

	ENTRY(longjmp)
	movq	LABEL_SP(%rdi), %rsp
	movq	LABEL_RBP(%rdi), %rbp
	movq	LABEL_RBX(%rdi), %rbx
	movq	LABEL_R12(%rdi), %r12
	movq	LABEL_R13(%rdi), %r13
	movq	LABEL_R14(%rdi), %r14
	movq	LABEL_R15(%rdi), %r15
	movq	(%rdi), %rdx		/* return address; LABEL_PC is 0 */
	movq	%rdx, (%rsp)
	xorl	%eax, %eax
	incl	%eax			/* return 1 */
	ret
	SET_SIZE(longjmp)

#endif	/* __lint */

/*
 * if a() calls b() calls caller(),
 * caller() returns return address in a().
 * (Note: We assume a() and b() are C routines which do the normal entry/exit
 *  sequence.)
 */

#if defined(__lint)

caddr_t
caller(void)
{ return (0); }

#else	/* __lint */

	ENTRY(caller)
	movq	8(%rbp), %rax		/* b()'s return pc, in a() */
	ret
	SET_SIZE(caller)

#endif	/* __lint */

/*
 * if a() calls callee(), callee() returns the
 * return address in a();
 */

#if defined(__lint)

caddr_t
callee(void)
{ return (0); }

#else	/* __lint */

	ENTRY(callee)
	movq	(%rsp), %rax		/* callee()'s return pc, in a() */
	ret
	SET_SIZE(callee)

#endif	/* __lint */

/*
 * return the current frame pointer
 */

#if defined(__lint)

greg_t
getfp(void)
{ return (0); }

#else	/* __lint */

	ENTRY(getfp)
	movq	%rbp, %rax
	ret
	SET_SIZE(getfp)

#endif	/* __lint */

/*
 * Invalidate a single page table entry in the TLB
 */

#if defined(__lint)

/* ARGSUSED */
void
mmu_tlbflush_entry(caddr_t m)
{}

#else	/* __lint */

	ENTRY(mmu_tlbflush_entry)
	invlpg	(%rdi)
	ret
	SET_SIZE(mmu_tlbflush_entry)

#endif	/* __lint */


/*
 * Get/Set the value of various control registers
 */

#if defined(__lint)

ulong_t
getcr0(void)
{ return (0); }

/* ARGSUSED */
void
setcr0(ulong_t value)
{}

ulong_t
getcr2(void)
{ return (0); }

ulong_t
getcr3(void)
{ return (0); }

#if !defined(__xpv)
/* ARGSUSED */
void
setcr3(ulong_t val)
{}

void
reload_cr3(void)
{}
#endif

ulong_t
getcr4(void)
{ return (0); }

/* ARGSUSED */
void
setcr4(ulong_t val)
{}

ulong_t
getcr8(void)
{ return (0); }

/* ARGSUSED */
void
setcr8(ulong_t val)
{}

#else	/* __lint */

	ENTRY(getcr0)
	movq	%cr0, %rax
	ret
	SET_SIZE(getcr0)

	ENTRY(setcr0)
	movq	%rdi, %cr0
	ret
	SET_SIZE(setcr0)

        ENTRY(getcr2)
#if defined(__xpv)
	movq	%gs:CPU_VCPU_INFO, %rax
	movq	VCPU_INFO_ARCH_CR2(%rax), %rax
#else
        movq    %cr2, %rax
#endif
        ret
	SET_SIZE(getcr2)

	ENTRY(getcr3)
	movq    %cr3, %rax
	ret
	SET_SIZE(getcr3)

#if !defined(__xpv)

        ENTRY(setcr3)
        movq    %rdi, %cr3
        ret
	SET_SIZE(setcr3)

	ENTRY(reload_cr3)
	movq	%cr3, %rdi
	movq	%rdi, %cr3
	ret
	SET_SIZE(reload_cr3)

#endif	/* __xpv */

	ENTRY(getcr4)
	movq	%cr4, %rax
	ret
	SET_SIZE(getcr4)

	ENTRY(setcr4)
	movq	%rdi, %cr4
	ret
	SET_SIZE(setcr4)

	ENTRY(getcr8)
	movq	%cr8, %rax
	ret
	SET_SIZE(getcr8)

	ENTRY(setcr8)
	movq	%rdi, %cr8
	ret
	SET_SIZE(setcr8)

#endif	/* __lint */

#if defined(__lint)

/*ARGSUSED*/
uint32_t
__cpuid_insn(struct cpuid_regs *regs)
{ return (0); }

#else	/* __lint */

	ENTRY(__cpuid_insn)
	movq	%rbx, %r8
	movq	%rcx, %r9
	movq	%rdx, %r11
	movl	(%rdi), %eax		/* %eax = regs->cp_eax */
	movl	0x4(%rdi), %ebx		/* %ebx = regs->cp_ebx */
	movl	0x8(%rdi), %ecx		/* %ecx = regs->cp_ecx */
	movl	0xc(%rdi), %edx		/* %edx = regs->cp_edx */
	cpuid
	movl	%eax, (%rdi)		/* regs->cp_eax = %eax */
	movl	%ebx, 0x4(%rdi)		/* regs->cp_ebx = %ebx */
	movl	%ecx, 0x8(%rdi)		/* regs->cp_ecx = %ecx */
	movl	%edx, 0xc(%rdi)		/* regs->cp_edx = %edx */
	movq	%r8, %rbx
	movq	%r9, %rcx
	movq	%r11, %rdx
	ret
	SET_SIZE(__cpuid_insn)

#endif	/* __lint */

#if defined(__lint)

/*ARGSUSED*/
void
i86_monitor(volatile uint32_t *addr, uint32_t extensions, uint32_t hints)
{}

#else   /* __lint */

	ENTRY_NP(i86_monitor)
	pushq	%rbp
	movq	%rsp, %rbp
	movq	%rdi, %rax		/* addr */
	movq	%rsi, %rcx		/* extensions */
	/* rdx contains input arg3: hints */
	clflush	(%rax)
	.byte	0x0f, 0x01, 0xc8	/* monitor */
	leave
	ret
	SET_SIZE(i86_monitor)

#endif	/* __lint */

#if defined(__lint)

/*ARGSUSED*/
void
i86_mwait(uint32_t data, uint32_t extensions)
{}

#else	/* __lint */

	ENTRY_NP(i86_mwait)
	pushq	%rbp
	movq	%rsp, %rbp
	movq	%rdi, %rax		/* data */
	movq	%rsi, %rcx		/* extensions */
	.byte	0x0f, 0x01, 0xc9	/* mwait */
	leave
	ret
	SET_SIZE(i86_mwait)

#endif	/* __lint */

#if defined(__xpv)
	/*
	 * Defined in C
	 */
#else

#if defined(__lint)

hrtime_t
tsc_read(void)
{
	return (0);
}

#else	/* __lint */

	ENTRY_NP(tsc_read)
	movq	%rbx, %r11
	movl	$0, %eax
	cpuid
	rdtsc
	movq	%r11, %rbx
	shlq	$32, %rdx
	orq	%rdx, %rax
	ret
	.globl _tsc_mfence_start
_tsc_mfence_start:
	mfence
	rdtsc
	shlq	$32, %rdx
	orq	%rdx, %rax
	ret
	.globl _tsc_mfence_end
_tsc_mfence_end:
	.globl _tscp_start
_tscp_start:
	.byte	0x0f, 0x01, 0xf9	/* rdtscp instruction */
	shlq	$32, %rdx
	orq	%rdx, %rax
	ret
	.globl _tscp_end
_tscp_end:
	.globl _no_rdtsc_start
_no_rdtsc_start:
	xorl	%edx, %edx
	xorl	%eax, %eax
	ret
	.globl _no_rdtsc_end
_no_rdtsc_end:
	.globl _tsc_lfence_start
_tsc_lfence_start:
	lfence
	rdtsc
	shlq	$32, %rdx
	orq	%rdx, %rax
	ret
	.globl _tsc_lfence_end
_tsc_lfence_end:
	SET_SIZE(tsc_read)

#endif	/* __lint */


#endif	/* __xpv */

#ifdef __lint
/*
 * Do not use this function for obtaining clock tick.  This
 * is called by callers who do not need to have a guarenteed
 * correct tick value.  The proper routine to use is tsc_read().
 */
u_longlong_t
randtick(void)
{
	return (0);
}
#else

	ENTRY_NP(randtick)
	rdtsc
	shlq    $32, %rdx
	orq     %rdx, %rax
	ret
	SET_SIZE(randtick)

#endif /* __lint */
/*
 * Insert entryp after predp in a doubly linked list.
 */

#if defined(__lint)

/*ARGSUSED*/
void
_insque(caddr_t entryp, caddr_t predp)
{}

#else	/* __lint */

	ENTRY(_insque)
	movq	(%rsi), %rax		/* predp->forw			*/
	movq	%rsi, CPTRSIZE(%rdi)	/* entryp->back = predp		*/
	movq	%rax, (%rdi)		/* entryp->forw = predp->forw	*/
	movq	%rdi, (%rsi)		/* predp->forw = entryp		*/
	movq	%rdi, CPTRSIZE(%rax)	/* predp->forw->back = entryp	*/
	ret
	SET_SIZE(_insque)

#endif	/* __lint */

/*
 * Remove entryp from a doubly linked list
 */

#if defined(__lint)

/*ARGSUSED*/
void
_remque(caddr_t entryp)
{}

#else	/* __lint */

	ENTRY(_remque)
	movq	(%rdi), %rax		/* entry->forw */
	movq	CPTRSIZE(%rdi), %rdx	/* entry->back */
	movq	%rax, (%rdx)		/* entry->back->forw = entry->forw */
	movq	%rdx, CPTRSIZE(%rax)	/* entry->forw->back = entry->back */
	ret
	SET_SIZE(_remque)

#endif	/* __lint */

/*
 * Returns the number of
 * non-NULL bytes in string argument.
 */

#if defined(__lint)

/* ARGSUSED */
size_t
strlen(const char *str)
{ return (0); }

#else	/* __lint */

/*
 * This is close to a simple transliteration of a C version of this
 * routine.  We should either just -make- this be a C version, or
 * justify having it in assembler by making it significantly faster.
 *
 * size_t
 * strlen(const char *s)
 * {
 *	const char *s0;
 * #if defined(DEBUG)
 *	if ((uintptr_t)s < KERNELBASE)
 *		panic(.str_panic_msg);
 * #endif
 *	for (s0 = s; *s; s++)
 *		;
 *	return (s - s0);
 * }
 */

	ENTRY(strlen)
#ifdef DEBUG
	movq	postbootkernelbase(%rip), %rax
	cmpq	%rax, %rdi
	jae	str_valid
	pushq	%rbp
	movq	%rsp, %rbp
	leaq	.str_panic_msg(%rip), %rdi
	xorl	%eax, %eax
	call	panic
#endif	/* DEBUG */
str_valid:
	cmpb	$0, (%rdi)
	movq	%rdi, %rax
	je	.null_found
	.align	4
.strlen_loop:
	incq	%rdi
	cmpb	$0, (%rdi)
	jne	.strlen_loop
.null_found:
	subq	%rax, %rdi
	movq	%rdi, %rax
	ret
	SET_SIZE(strlen)


#ifdef DEBUG
	.text
.str_panic_msg:
	.string "strlen: argument below kernelbase"
#endif /* DEBUG */

#endif	/* __lint */

	/*
	 * Berkeley 4.3 introduced symbolically named interrupt levels
	 * as a way deal with priority in a machine independent fashion.
	 * Numbered priorities are machine specific, and should be
	 * discouraged where possible.
	 *
	 * Note, for the machine specific priorities there are
	 * examples listed for devices that use a particular priority.
	 * It should not be construed that all devices of that
	 * type should be at that priority.  It is currently were
	 * the current devices fit into the priority scheme based
	 * upon time criticalness.
	 *
	 * The underlying assumption of these assignments is that
	 * IPL 10 is the highest level from which a device
	 * routine can call wakeup.  Devices that interrupt from higher
	 * levels are restricted in what they can do.  If they need
	 * kernels services they should schedule a routine at a lower
	 * level (via software interrupt) to do the required
	 * processing.
	 *
	 * Examples of this higher usage:
	 *	Level	Usage
	 *	14	Profiling clock (and PROM uart polling clock)
	 *	12	Serial ports
	 *
	 * The serial ports request lower level processing on level 6.
	 *
	 * Also, almost all splN routines (where N is a number or a
	 * mnemonic) will do a RAISE(), on the assumption that they are
	 * never used to lower our priority.
	 * The exceptions are:
	 *	spl8()		Because you can't be above 15 to begin with!
	 *	splzs()		Because this is used at boot time to lower our
	 *			priority, to allow the PROM to poll the uart.
	 *	spl0()		Used to lower priority to 0.
	 */

#if defined(__lint)

int spl0(void)		{ return (0); }
int spl6(void)		{ return (0); }
int spl7(void)		{ return (0); }
int spl8(void)		{ return (0); }
int splhigh(void)	{ return (0); }
int splhi(void)		{ return (0); }
int splzs(void)		{ return (0); }

/* ARGSUSED */
void
splx(int level)
{}

#else	/* __lint */

#define	SETPRI(level) \
	movl	$/**/level, %edi;	/* new priority */		\
	jmp	do_splx			/* redirect to do_splx */

#define	RAISE(level) \
	movl	$/**/level, %edi;	/* new priority */		\
	jmp	splr			/* redirect to splr */

	/* locks out all interrupts, including memory errors */
	ENTRY(spl8)
	SETPRI(15)
	SET_SIZE(spl8)

	/* just below the level that profiling runs */
	ENTRY(spl7)
	RAISE(13)
	SET_SIZE(spl7)

	/* sun specific - highest priority onboard serial i/o asy ports */
	ENTRY(splzs)
	SETPRI(12)	/* Can't be a RAISE, as it's used to lower us */
	SET_SIZE(splzs)

	ENTRY(splhi)
	ALTENTRY(splhigh)
	ALTENTRY(spl6)
	ALTENTRY(i_ddi_splhigh)

	RAISE(DISP_LEVEL)

	SET_SIZE(i_ddi_splhigh)
	SET_SIZE(spl6)
	SET_SIZE(splhigh)
	SET_SIZE(splhi)

	/* allow all interrupts */
	ENTRY(spl0)
	SETPRI(0)
	SET_SIZE(spl0)


	/* splx implementation */
	ENTRY(splx)
	jmp	do_splx		/* redirect to common splx code */
	SET_SIZE(splx)

#endif	/* __lint */

#if defined(__lint)

void
pc_reset(void)
{}

void
efi_reset(void)
{}

#else	/* __lint */

	ENTRY(wait_500ms)
	pushq	%rbx
	movl	$50000, %ebx
1:
	call	tenmicrosec
	decl	%ebx
	jnz	1b
	popq	%rbx
	ret	
	SET_SIZE(wait_500ms)

#define	RESET_METHOD_KBC	1
#define	RESET_METHOD_PORT92	2
#define RESET_METHOD_PCI	4

	DGDEF3(pc_reset_methods, 4, 8)
	.long RESET_METHOD_KBC|RESET_METHOD_PORT92|RESET_METHOD_PCI;

	ENTRY(pc_reset)

	testl	$RESET_METHOD_KBC, pc_reset_methods(%rip)
	jz	1f

	/
	/ Try the classic keyboard controller-triggered reset.
	/
	movw	$0x64, %dx
	movb	$0xfe, %al
	outb	(%dx)

	/ Wait up to 500 milliseconds here for the keyboard controller
	/ to pull the reset line.  On some systems where the keyboard
	/ controller is slow to pull the reset line, the next reset method
	/ may be executed (which may be bad if those systems hang when the
	/ next reset method is used, e.g. Ferrari 3400 (doesn't like port 92),
	/ and Ferrari 4000 (doesn't like the cf9 reset method))

	call	wait_500ms

1:
	testl	$RESET_METHOD_PORT92, pc_reset_methods(%rip)
	jz	3f

	/
	/ Try port 0x92 fast reset
	/
	movw	$0x92, %dx
	inb	(%dx)
	cmpb	$0xff, %al	/ If port's not there, we should get back 0xFF
	je	1f
	testb	$1, %al		/ If bit 0
	jz	2f		/ is clear, jump to perform the reset
	andb	$0xfe, %al	/ otherwise,
	outb	(%dx)		/ clear bit 0 first, then
2:
	orb	$1, %al		/ Set bit 0
	outb	(%dx)		/ and reset the system
1:

	call	wait_500ms

3:
	testl	$RESET_METHOD_PCI, pc_reset_methods(%rip)
	jz	4f

	/ Try the PCI (soft) reset vector (should work on all modern systems,
	/ but has been shown to cause problems on 450NX systems, and some newer
	/ systems (e.g. ATI IXP400-equipped systems))
	/ When resetting via this method, 2 writes are required.  The first
	/ targets bit 1 (0=hard reset without power cycle, 1=hard reset with
	/ power cycle).
	/ The reset occurs on the second write, during bit 2's transition from
	/ 0->1.
	movw	$0xcf9, %dx
	movb	$0x2, %al	/ Reset mode = hard, no power cycle
	outb	(%dx)
	movb	$0x6, %al
	outb	(%dx)

	call	wait_500ms

4:
	/
	/ port 0xcf9 failed also.  Last-ditch effort is to
	/ triple-fault the CPU.
	/ Also, use triple fault for EFI firmware
	/
	ENTRY(efi_reset)
	pushq	$0x0
	pushq	$0x0		/ IDT base of 0, limit of 0 + 2 unused bytes
	lidt	(%rsp)
	int	$0x0		/ Trigger interrupt, generate triple-fault

	cli
	hlt			/ Wait forever
	/*NOTREACHED*/
	SET_SIZE(efi_reset)
	SET_SIZE(pc_reset)

#endif	/* __lint */

/*
 * C callable in and out routines
 */

#if defined(__lint)

/* ARGSUSED */
void
outl(int port_address, uint32_t val)
{}

#else	/* __lint */

	ENTRY(outl)
	movw	%di, %dx
	movl	%esi, %eax
	outl	(%dx)
	ret
	SET_SIZE(outl)

#endif	/* __lint */

#if defined(__lint)

/* ARGSUSED */
void
outw(int port_address, uint16_t val)
{}

#else	/* __lint */

	ENTRY(outw)
	movw	%di, %dx
	movw	%si, %ax
	D16 outl (%dx)		/* XX64 why not outw? */
	ret
	SET_SIZE(outw)

#endif	/* __lint */

#if defined(__lint)

/* ARGSUSED */
void
outb(int port_address, uint8_t val)
{}

#else	/* __lint */

	ENTRY(outb)
	movw	%di, %dx
	movb	%sil, %al
	outb	(%dx)
	ret
	SET_SIZE(outb)

#endif	/* __lint */

#if defined(__lint)

/* ARGSUSED */
uint32_t
inl(int port_address)
{ return (0); }

#else	/* __lint */

	ENTRY(inl)
	xorl	%eax, %eax
	movw	%di, %dx
	inl	(%dx)
	ret
	SET_SIZE(inl)

#endif	/* __lint */

#if defined(__lint)

/* ARGSUSED */
uint16_t
inw(int port_address)
{ return (0); }

#else	/* __lint */

	ENTRY(inw)
	xorl	%eax, %eax
	movw	%di, %dx
	D16 inl	(%dx)
	ret
	SET_SIZE(inw)

#endif	/* __lint */


#if defined(__lint)

/* ARGSUSED */
uint8_t
inb(int port_address)
{ return (0); }

#else	/* __lint */

	ENTRY(inb)
	xorl	%eax, %eax
	movw	%di, %dx
	inb	(%dx)
	ret
	SET_SIZE(inb)

#endif	/* __lint */


#if defined(__lint)

/* ARGSUSED */
void
repoutsw(int port, uint16_t *addr, int cnt)
{}

#else	/* __lint */

	ENTRY(repoutsw)
	movl	%edx, %ecx
	movw	%di, %dx
	rep
	  D16 outsl
	ret
	SET_SIZE(repoutsw)

#endif	/* __lint */


#if defined(__lint)

/* ARGSUSED */
void
repinsw(int port_addr, uint16_t *addr, int cnt)
{}

#else	/* __lint */

	ENTRY(repinsw)
	movl	%edx, %ecx
	movw	%di, %dx
	rep
	  D16 insl
	ret
	SET_SIZE(repinsw)

#endif	/* __lint */


#if defined(__lint)

/* ARGSUSED */
void
repinsb(int port, uint8_t *addr, int count)
{}

#else	/* __lint */

	ENTRY(repinsb)
	movl	%edx, %ecx	
	movw	%di, %dx
	movq	%rsi, %rdi
	rep
	  insb
	ret		
	SET_SIZE(repinsb)

#endif	/* __lint */


/*
 * Input a stream of 32-bit words.
 * NOTE: count is a DWORD count.
 */
#if defined(__lint)

/* ARGSUSED */
void
repinsd(int port, uint32_t *addr, int count)
{}

#else	/* __lint */

	ENTRY(repinsd)
	movl	%edx, %ecx
	movw	%di, %dx
	movq	%rsi, %rdi
	rep
	  insl
	ret
	SET_SIZE(repinsd)

#endif	/* __lint */

/*
 * Output a stream of bytes
 * NOTE: count is a byte count
 */
#if defined(__lint)

/* ARGSUSED */
void
repoutsb(int port, uint8_t *addr, int count)
{}

#else	/* __lint */

	ENTRY(repoutsb)
	movl	%edx, %ecx
	movw	%di, %dx
	rep
	  outsb
	ret	
	SET_SIZE(repoutsb)

#endif	/* __lint */

/*
 * Output a stream of 32-bit words
 * NOTE: count is a DWORD count
 */
#if defined(__lint)

/* ARGSUSED */
void
repoutsd(int port, uint32_t *addr, int count)
{}

#else	/* __lint */

	ENTRY(repoutsd)
	movl	%edx, %ecx
	movw	%di, %dx
	rep
	  outsl
	ret	
	SET_SIZE(repoutsd)

#endif	/* __lint */

/*
 * void int3(void)
 * void int18(void)
 * void int20(void)
 * void int_cmci(void)
 */

#if defined(__lint)

void
int3(void)
{}

void
int18(void)
{}

void
int20(void)
{}

void
int_cmci(void)
{}

#else	/* __lint */

	ENTRY(int3)
	int	$T_BPTFLT
	ret
	SET_SIZE(int3)

	ENTRY(int18)
	int	$T_MCE
	ret
	SET_SIZE(int18)

	ENTRY(int20)
	movl	boothowto, %eax
	andl	$RB_DEBUG, %eax
	jz	1f

	int	$T_DBGENTR
1:
	rep;	ret	/* use 2 byte return instruction when branch target */
			/* AMD Software Optimization Guide - Section 6.2 */
	SET_SIZE(int20)

	ENTRY(int_cmci)
	int	$T_ENOEXTFLT
	ret
	SET_SIZE(int_cmci)

#endif	/* __lint */

#if defined(__lint)

/* ARGSUSED */
int
scanc(size_t size, uchar_t *cp, uchar_t *table, uchar_t mask)
{ return (0); }

#else	/* __lint */

	ENTRY(scanc)
					/* rdi == size */
					/* rsi == cp */
					/* rdx == table */
					/* rcx == mask */
	addq	%rsi, %rdi		/* end = &cp[size] */
.scanloop:
	cmpq	%rdi, %rsi		/* while (cp < end */
	jnb	.scandone
	movzbq	(%rsi), %r8		/* %r8 = *cp */
	incq	%rsi			/* cp++ */
	testb	%cl, (%r8, %rdx)
	jz	.scanloop		/*  && (table[*cp] & mask) == 0) */
	decq	%rsi			/* (fix post-increment) */
.scandone:
	movl	%edi, %eax
	subl	%esi, %eax		/* return (end - cp) */
	ret
	SET_SIZE(scanc)

#endif	/* __lint */

/*
 * Replacement functions for ones that are normally inlined.
 * In addition to the copy in i86.il, they are defined here just in case.
 */

#if defined(__lint)

ulong_t
intr_clear(void)
{ return (0); }

ulong_t
clear_int_flag(void)
{ return (0); }

#else	/* __lint */

	ENTRY(intr_clear)
	ENTRY(clear_int_flag)
	pushfq
	popq	%rax
#if defined(__xpv)
	CLIRET(%rdi, %dl)	/* returns event mask in %dl */
	/*
	 * Synthesize the PS_IE bit from the event mask bit
	 */
	andq    $_BITNOT(PS_IE), %rax
	testb	$1, %dl
	jnz	1f
	orq	$PS_IE, %rax
1:
	ret
#endif
	CLI(%rdi)
	ret
	SET_SIZE(clear_int_flag)
	SET_SIZE(intr_clear)

#endif	/* __lint */

#if defined(__lint)

struct cpu *
curcpup(void)
{ return 0; }

#else	/* __lint */

	ENTRY(curcpup)
	movq	%gs:CPU_SELF, %rax
	ret
	SET_SIZE(curcpup)

#endif	/* __lint */

/* htonll(), ntohll(), htonl(), ntohl(), htons(), ntohs()
 * These functions reverse the byte order of the input parameter and returns
 * the result.  This is to convert the byte order from host byte order
 * (little endian) to network byte order (big endian), or vice versa.
 */

#if defined(__lint)

uint64_t
htonll(uint64_t i)
{ return (i); }

uint64_t
ntohll(uint64_t i)
{ return (i); }

uint32_t
htonl(uint32_t i)
{ return (i); }

uint32_t
ntohl(uint32_t i)
{ return (i); }

uint16_t
htons(uint16_t i)
{ return (i); }

uint16_t
ntohs(uint16_t i)
{ return (i); }

#else	/* __lint */

	ENTRY(htonll)
	ALTENTRY(ntohll)
	movq	%rdi, %rax
	bswapq	%rax
	ret
	SET_SIZE(ntohll)
	SET_SIZE(htonll)

	/* XX64 there must be shorter sequences for this */
	ENTRY(htonl)
	ALTENTRY(ntohl)
	movl	%edi, %eax
	bswap	%eax
	ret
	SET_SIZE(ntohl)
	SET_SIZE(htonl)

	/* XX64 there must be better sequences for this */
	ENTRY(htons)
	ALTENTRY(ntohs)
	movl	%edi, %eax
	bswap	%eax
	shrl	$16, %eax
	ret
	SET_SIZE(ntohs)
	SET_SIZE(htons)

#endif	/* __lint */


#if defined(__lint)

/* ARGSUSED */
void
intr_restore(ulong_t i)
{ return; }

/* ARGSUSED */
void
restore_int_flag(ulong_t i)
{ return; }

#else	/* __lint */

	ENTRY(intr_restore)
	ENTRY(restore_int_flag)
	testq	$PS_IE, %rdi
	jz	1f
#if defined(__xpv)
	/*
	 * Since we're -really- running unprivileged, our attempt
	 * to change the state of the IF bit will be ignored.
	 * The virtual IF bit is tweaked by CLI and STI.
	 */
	IE_TO_EVENT_MASK(%rsi, %rdi)
#else
	sti
#endif
1:
	ret
	SET_SIZE(restore_int_flag)
	SET_SIZE(intr_restore)

#endif	/* __lint */

#if defined(__lint)

void
sti(void)
{}

void
cli(void)
{}

#else	/* __lint */

	ENTRY(sti)
	STI
	ret
	SET_SIZE(sti)

	ENTRY(cli)
	CLI(%rax)
	ret
	SET_SIZE(cli)

#endif	/* __lint */

#if defined(__lint)

dtrace_icookie_t
dtrace_interrupt_disable(void)
{ return (0); }

#else   /* __lint */

	ENTRY(dtrace_interrupt_disable)
	pushfq
	popq	%rax
#if defined(__xpv)
	CLIRET(%rdi, %dl)	/* returns event mask in %dl */
	/*
	 * Synthesize the PS_IE bit from the event mask bit
	 */
	andq    $_BITNOT(PS_IE), %rax
	testb	$1, %dl
	jnz	.dtrace_interrupt_disable_done
	orq	$PS_IE, %rax
#else
	CLI(%rdx)
#endif
.dtrace_interrupt_disable_done:
	ret
	SET_SIZE(dtrace_interrupt_disable)

#endif	/* __lint */

#if defined(__lint)

/*ARGSUSED*/
void
dtrace_interrupt_enable(dtrace_icookie_t cookie)
{}

#else	/* __lint */

	ENTRY(dtrace_interrupt_enable)
	pushq	%rdi
	popfq
#if defined(__xpv)
	/*
	 * Since we're -really- running unprivileged, our attempt
	 * to change the state of the IF bit will be ignored. The
	 * virtual IF bit is tweaked by CLI and STI.
	 */
	IE_TO_EVENT_MASK(%rdx, %rdi)
#endif
.dtrace_interrupt_enable_done:
	ret
	SET_SIZE(dtrace_interrupt_enable)

#endif	/* __lint */


#if defined(lint)

void
dtrace_membar_producer(void)
{}

void
dtrace_membar_consumer(void)
{}

#else	/* __lint */

	ENTRY(dtrace_membar_producer)
	rep;	ret	/* use 2 byte return instruction when branch target */
			/* AMD Software Optimization Guide - Section 6.2 */
	SET_SIZE(dtrace_membar_producer)

	ENTRY(dtrace_membar_consumer)
	rep;	ret	/* use 2 byte return instruction when branch target */
			/* AMD Software Optimization Guide - Section 6.2 */
	SET_SIZE(dtrace_membar_consumer)

#endif	/* __lint */

#if defined(__lint)

kthread_id_t
threadp(void)
{ return ((kthread_id_t)0); }

#else	/* __lint */

	ENTRY(threadp)
	movq	%gs:CPU_THREAD, %rax
	ret
	SET_SIZE(threadp)

#endif	/* __lint */

/*
 *   Checksum routine for Internet Protocol Headers
 */

#if defined(__lint)

/* ARGSUSED */
unsigned int
ip_ocsum(
	ushort_t *address,	/* ptr to 1st message buffer */
	int halfword_count,	/* length of data */
	unsigned int sum)	/* partial checksum */
{ 
	int		i;
	unsigned int	psum = 0;	/* partial sum */

	for (i = 0; i < halfword_count; i++, address++) {
		psum += *address;
	}

	while ((psum >> 16) != 0) {
		psum = (psum & 0xffff) + (psum >> 16);
	}

	psum += sum;

	while ((psum >> 16) != 0) {
		psum = (psum & 0xffff) + (psum >> 16);
	}

	return (psum);
}

#else	/* __lint */

	ENTRY(ip_ocsum)
	pushq	%rbp
	movq	%rsp, %rbp
#ifdef DEBUG
	movq	postbootkernelbase(%rip), %rax
	cmpq	%rax, %rdi
	jnb	1f
	xorl	%eax, %eax
	movq	%rdi, %rsi
	leaq	.ip_ocsum_panic_msg(%rip), %rdi
	call	panic
	/*NOTREACHED*/
.ip_ocsum_panic_msg:
	.string	"ip_ocsum: address 0x%p below kernelbase\n"
1:
#endif
	movl	%esi, %ecx	/* halfword_count */
	movq	%rdi, %rsi	/* address */
				/* partial sum in %edx */
	xorl	%eax, %eax
	testl	%ecx, %ecx
	jz	.ip_ocsum_done
	testq	$3, %rsi
	jnz	.ip_csum_notaligned
.ip_csum_aligned:	/* XX64 opportunities for 8-byte operations? */
.next_iter:
	/* XX64 opportunities for prefetch? */
	/* XX64 compute csum with 64 bit quantities? */
	subl	$32, %ecx
	jl	.less_than_32

	addl	0(%rsi), %edx
.only60:
	adcl	4(%rsi), %eax
.only56:
	adcl	8(%rsi), %edx
.only52:
	adcl	12(%rsi), %eax
.only48:
	adcl	16(%rsi), %edx
.only44:
	adcl	20(%rsi), %eax
.only40:
	adcl	24(%rsi), %edx
.only36:
	adcl	28(%rsi), %eax
.only32:
	adcl	32(%rsi), %edx
.only28:
	adcl	36(%rsi), %eax
.only24:
	adcl	40(%rsi), %edx
.only20:
	adcl	44(%rsi), %eax
.only16:
	adcl	48(%rsi), %edx
.only12:
	adcl	52(%rsi), %eax
.only8:
	adcl	56(%rsi), %edx
.only4:
	adcl	60(%rsi), %eax	/* could be adding -1 and -1 with a carry */
.only0:
	adcl	$0, %eax	/* could be adding -1 in eax with a carry */
	adcl	$0, %eax

	addq	$64, %rsi
	testl	%ecx, %ecx
	jnz	.next_iter

.ip_ocsum_done:
	addl	%eax, %edx
	adcl	$0, %edx
	movl	%edx, %eax	/* form a 16 bit checksum by */
	shrl	$16, %eax	/* adding two halves of 32 bit checksum */
	addw	%dx, %ax
	adcw	$0, %ax
	andl	$0xffff, %eax
	leave
	ret

.ip_csum_notaligned:
	xorl	%edi, %edi
	movw	(%rsi), %di
	addl	%edi, %edx
	adcl	$0, %edx
	addq	$2, %rsi
	decl	%ecx
	jmp	.ip_csum_aligned

.less_than_32:
	addl	$32, %ecx
	testl	$1, %ecx
	jz	.size_aligned
	andl	$0xfe, %ecx
	movzwl	(%rsi, %rcx, 2), %edi
	addl	%edi, %edx
	adcl	$0, %edx
.size_aligned:
	movl	%ecx, %edi
	shrl	$1, %ecx
	shl	$1, %edi
	subq	$64, %rdi
	addq	%rdi, %rsi
	leaq    .ip_ocsum_jmptbl(%rip), %rdi
	leaq	(%rdi, %rcx, 8), %rdi
	xorl	%ecx, %ecx
	clc
	jmp 	*(%rdi)

	.align	8
.ip_ocsum_jmptbl:
	.quad	.only0, .only4, .only8, .only12, .only16, .only20
	.quad	.only24, .only28, .only32, .only36, .only40, .only44
	.quad	.only48, .only52, .only56, .only60
	SET_SIZE(ip_ocsum)

#endif	/* __lint */

#if defined(notused)
#if defined(__lint)
/* ARGSUSED */
void
load_pte64(uint64_t *pte, uint64_t pte_value)
{}
#else	/* __lint */
	.globl load_pte64
load_pte64:
	movl	4(%esp), %eax
	movl	8(%esp), %ecx
	movl	12(%esp), %edx
	movl	%edx, 4(%eax)
	movl	%ecx, (%eax)
	ret
#endif	/* __lint */
#endif	/* notused */

#if defined(__lint)

/*ARGSUSED*/
void
scan_memory(caddr_t addr, size_t size)
{}

#else	/* __lint */

	ENTRY(scan_memory)
	shrq	$3, %rsi	/* convert %rsi from byte to quadword count */
	jz	.scanm_done
	movq	%rsi, %rcx	/* move count into rep control register */
	movq	%rdi, %rsi	/* move addr into lodsq control reg. */
	rep lodsq		/* scan the memory range */
.scanm_done:
	rep;	ret	/* use 2 byte return instruction when branch target */
			/* AMD Software Optimization Guide - Section 6.2 */
	SET_SIZE(scan_memory)

#endif	/* __lint */


#if defined(__lint)

/*ARGSUSED */
int
lowbit(ulong_t i)
{ return (0); }

#else	/* __lint */

	ENTRY(lowbit)
	movl	$-1, %eax
	bsfq	%rdi, %rax
	incl	%eax
	ret
	SET_SIZE(lowbit)

#endif	/* __lint */

#if defined(__lint)

/*ARGSUSED*/
int
highbit(ulong_t i)
{ return (0); }

#else	/* __lint */

	ENTRY(highbit)
	movl	$-1, %eax
	bsrq	%rdi, %rax
	incl	%eax
	ret
	SET_SIZE(highbit)

#endif	/* __lint */

#if defined(__lint)

/*ARGSUSED*/
uint64_t
rdmsr(uint_t r)
{ return (0); }

/*ARGSUSED*/
void
wrmsr(uint_t r, const uint64_t val)
{}

/*ARGSUSED*/
uint64_t
xrdmsr(uint_t r)
{ return (0); }

/*ARGSUSED*/
void
xwrmsr(uint_t r, const uint64_t val)
{}

void
invalidate_cache(void)
{}

/*ARGSUSED*/
uint64_t
get_xcr(uint_t r)
{ return (0); }

/*ARGSUSED*/
void
set_xcr(uint_t r, const uint64_t val)
{}

#else  /* __lint */

#define	XMSR_ACCESS_VAL		$0x9c5a203a

	ENTRY(rdmsr)
	movl	%edi, %ecx
	rdmsr
	shlq	$32, %rdx
	orq	%rdx, %rax
	ret
	SET_SIZE(rdmsr)

	ENTRY(wrmsr)
	movq	%rsi, %rdx
	shrq	$32, %rdx
	movl	%esi, %eax
	movl	%edi, %ecx
	wrmsr
	ret
	SET_SIZE(wrmsr)

	ENTRY(xrdmsr)
	pushq	%rbp
	movq	%rsp, %rbp
	movl	%edi, %ecx
	movl	XMSR_ACCESS_VAL, %edi	/* this value is needed to access MSR */
	rdmsr
	shlq	$32, %rdx
	orq	%rdx, %rax
	leave
	ret
	SET_SIZE(xrdmsr)

	ENTRY(xwrmsr)
	pushq	%rbp
	movq	%rsp, %rbp
	movl	%edi, %ecx
	movl	XMSR_ACCESS_VAL, %edi	/* this value is needed to access MSR */
	movq	%rsi, %rdx
	shrq	$32, %rdx
	movl	%esi, %eax
	wrmsr
	leave
	ret
	SET_SIZE(xwrmsr)

	ENTRY(get_xcr)
	movl	%edi, %ecx
	#xgetbv
	.byte	0x0f,0x01,0xd0
	shlq	$32, %rdx
	orq	%rdx, %rax
	ret
	SET_SIZE(get_xcr)

	ENTRY(set_xcr)
	movq	%rsi, %rdx
	shrq	$32, %rdx
	movl	%esi, %eax
	movl	%edi, %ecx
	#xsetbv
	.byte	0x0f,0x01,0xd1
	ret
	SET_SIZE(set_xcr)

	ENTRY(invalidate_cache)
	wbinvd
	ret
	SET_SIZE(invalidate_cache)

#endif	/* __lint */

#if defined(__lint)

/*ARGSUSED*/
void
getcregs(struct cregs *crp)
{}

#else	/* __lint */

	ENTRY_NP(getcregs)
#if defined(__xpv)
	/*
	 * Only a few of the hardware control registers or descriptor tables
	 * are directly accessible to us, so just zero the structure.
	 *
	 * XXPV	Perhaps it would be helpful for the hypervisor to return
	 *	virtualized versions of these for post-mortem use.
	 *	(Need to reevaluate - perhaps it already does!)
	 */
	pushq	%rdi		/* save *crp */
	movq	$CREGSZ, %rsi
	call	bzero
	popq	%rdi

	/*
	 * Dump what limited information we can
	 */
	movq	%cr0, %rax
	movq	%rax, CREG_CR0(%rdi)	/* cr0 */
	movq	%cr2, %rax
	movq	%rax, CREG_CR2(%rdi)	/* cr2 */
	movq	%cr3, %rax
	movq	%rax, CREG_CR3(%rdi)	/* cr3 */
	movq	%cr4, %rax
	movq	%rax, CREG_CR4(%rdi)	/* cr4 */

#else	/* __xpv */

#define	GETMSR(r, off, d)	\
	movl	$r, %ecx;	\
	rdmsr;			\
	movl	%eax, off(d);	\
	movl	%edx, off+4(d)

	xorl	%eax, %eax
	movq	%rax, CREG_GDT+8(%rdi)
	sgdt	CREG_GDT(%rdi)		/* 10 bytes */
	movq	%rax, CREG_IDT+8(%rdi)
	sidt	CREG_IDT(%rdi)		/* 10 bytes */
	movq	%rax, CREG_LDT(%rdi)
	sldt	CREG_LDT(%rdi)		/* 2 bytes */
	movq	%rax, CREG_TASKR(%rdi)
	str	CREG_TASKR(%rdi)	/* 2 bytes */
	movq	%cr0, %rax
	movq	%rax, CREG_CR0(%rdi)	/* cr0 */
	movq	%cr2, %rax
	movq	%rax, CREG_CR2(%rdi)	/* cr2 */
	movq	%cr3, %rax
	movq	%rax, CREG_CR3(%rdi)	/* cr3 */
	movq	%cr4, %rax
	movq	%rax, CREG_CR4(%rdi)	/* cr4 */
	movq	%cr8, %rax
	movq	%rax, CREG_CR8(%rdi)	/* cr8 */
	GETMSR(MSR_AMD_KGSBASE, CREG_KGSBASE, %rdi)
	GETMSR(MSR_AMD_EFER, CREG_EFER, %rdi)
#endif	/* __xpv */
	ret
	SET_SIZE(getcregs)

#undef GETMSR

#endif	/* __lint */


/*
 * A panic trigger is a word which is updated atomically and can only be set
 * once.  We atomically store 0xDEFACEDD and load the old value.  If the
 * previous value was 0, we succeed and return 1; otherwise return 0.
 * This allows a partially corrupt trigger to still trigger correctly.  DTrace
 * has its own version of this function to allow it to panic correctly from
 * probe context.
 */
#if defined(__lint)

/*ARGSUSED*/
int
panic_trigger(int *tp)
{ return (0); }

/*ARGSUSED*/
int
dtrace_panic_trigger(int *tp)
{ return (0); }

#else	/* __lint */

	ENTRY_NP(panic_trigger)
	xorl	%eax, %eax
	movl	$0xdefacedd, %edx
	lock
	  xchgl	%edx, (%rdi)
	cmpl	$0, %edx
	je	0f 
	movl	$0, %eax
	ret
0:	movl	$1, %eax
	ret
	SET_SIZE(panic_trigger)
	
	ENTRY_NP(dtrace_panic_trigger)
	xorl	%eax, %eax
	movl	$0xdefacedd, %edx
	lock
	  xchgl	%edx, (%rdi)
	cmpl	$0, %edx
	je	0f
	movl	$0, %eax
	ret
0:	movl	$1, %eax
	ret
	SET_SIZE(dtrace_panic_trigger)

#endif	/* __lint */

/*
 * The panic() and cmn_err() functions invoke vpanic() as a common entry point
 * into the panic code implemented in panicsys().  vpanic() is responsible
 * for passing through the format string and arguments, and constructing a
 * regs structure on the stack into which it saves the current register
 * values.  If we are not dying due to a fatal trap, these registers will
 * then be preserved in panicbuf as the current processor state.  Before
 * invoking panicsys(), vpanic() activates the first panic trigger (see
 * common/os/panic.c) and switches to the panic_stack if successful.  Note that
 * DTrace takes a slightly different panic path if it must panic from probe
 * context.  Instead of calling panic, it calls into dtrace_vpanic(), which
 * sets up the initial stack as vpanic does, calls dtrace_panic_trigger(), and
 * branches back into vpanic().
 */
#if defined(__lint)

/*ARGSUSED*/
void
vpanic(const char *format, va_list alist)
{}

/*ARGSUSED*/
void
dtrace_vpanic(const char *format, va_list alist)
{}

#else	/* __lint */

	ENTRY_NP(vpanic)			/* Initial stack layout: */
	
	pushq	%rbp				/* | %rip | 	0x60	*/
	movq	%rsp, %rbp			/* | %rbp |	0x58	*/
	pushfq					/* | rfl  |	0x50	*/
	pushq	%r11				/* | %r11 |	0x48	*/
	pushq	%r10				/* | %r10 |	0x40	*/
	pushq	%rbx				/* | %rbx |	0x38	*/
	pushq	%rax				/* | %rax |	0x30	*/
	pushq	%r9				/* | %r9  |	0x28	*/
	pushq	%r8				/* | %r8  |	0x20	*/
	pushq	%rcx				/* | %rcx |	0x18	*/
	pushq	%rdx				/* | %rdx |	0x10	*/
	pushq	%rsi				/* | %rsi |	0x8 alist */
	pushq	%rdi				/* | %rdi |	0x0 format */

	movq	%rsp, %rbx			/* %rbx = current %rsp */

	leaq	panic_quiesce(%rip), %rdi	/* %rdi = &panic_quiesce */
	call	panic_trigger			/* %eax = panic_trigger() */

vpanic_common:
	/*
	 * The panic_trigger result is in %eax from the call above, and
	 * dtrace_panic places it in %eax before branching here.
	 * The rdmsr instructions that follow below will clobber %eax so
	 * we stash the panic_trigger result in %r11d.
	 */
	movl	%eax, %r11d
	cmpl	$0, %r11d
	je	0f

	/*
	 * If panic_trigger() was successful, we are the first to initiate a
	 * panic: we now switch to the reserved panic_stack before continuing.
	 */
	leaq	panic_stack(%rip), %rsp
	addq	$PANICSTKSIZE, %rsp
0:	subq	$REGSIZE, %rsp
	/*
	 * Now that we've got everything set up, store the register values as
	 * they were when we entered vpanic() to the designated location in
	 * the regs structure we allocated on the stack.
	 */
	movq	0x0(%rbx), %rcx
	movq	%rcx, REGOFF_RDI(%rsp)
	movq	0x8(%rbx), %rcx
	movq	%rcx, REGOFF_RSI(%rsp)
	movq	0x10(%rbx), %rcx
	movq	%rcx, REGOFF_RDX(%rsp)
	movq	0x18(%rbx), %rcx
	movq	%rcx, REGOFF_RCX(%rsp)
	movq	0x20(%rbx), %rcx

	movq	%rcx, REGOFF_R8(%rsp)
	movq	0x28(%rbx), %rcx
	movq	%rcx, REGOFF_R9(%rsp)
	movq	0x30(%rbx), %rcx
	movq	%rcx, REGOFF_RAX(%rsp)
	movq	0x38(%rbx), %rcx
	movq	%rcx, REGOFF_RBX(%rsp)
	movq	0x58(%rbx), %rcx

	movq	%rcx, REGOFF_RBP(%rsp)
	movq	0x40(%rbx), %rcx
	movq	%rcx, REGOFF_R10(%rsp)
	movq	0x48(%rbx), %rcx
	movq	%rcx, REGOFF_R11(%rsp)
	movq	%r12, REGOFF_R12(%rsp)

	movq	%r13, REGOFF_R13(%rsp)
	movq	%r14, REGOFF_R14(%rsp)
	movq	%r15, REGOFF_R15(%rsp)

	xorl	%ecx, %ecx
	movw	%ds, %cx
	movq	%rcx, REGOFF_DS(%rsp)
	movw	%es, %cx
	movq	%rcx, REGOFF_ES(%rsp)
	movw	%fs, %cx
	movq	%rcx, REGOFF_FS(%rsp)
	movw	%gs, %cx
	movq	%rcx, REGOFF_GS(%rsp)

	movq	$0, REGOFF_TRAPNO(%rsp)

	movq	$0, REGOFF_ERR(%rsp)
	leaq	vpanic(%rip), %rcx
	movq	%rcx, REGOFF_RIP(%rsp)
	movw	%cs, %cx
	movzwq	%cx, %rcx
	movq	%rcx, REGOFF_CS(%rsp)
	movq	0x50(%rbx), %rcx
	movq	%rcx, REGOFF_RFL(%rsp)
	movq	%rbx, %rcx
	addq	$0x60, %rcx
	movq	%rcx, REGOFF_RSP(%rsp)
	movw	%ss, %cx
	movzwq	%cx, %rcx
	movq	%rcx, REGOFF_SS(%rsp)

	/*
	 * panicsys(format, alist, rp, on_panic_stack) 
	 */	
	movq	REGOFF_RDI(%rsp), %rdi		/* format */
	movq	REGOFF_RSI(%rsp), %rsi		/* alist */
	movq	%rsp, %rdx			/* struct regs */
	movl	%r11d, %ecx			/* on_panic_stack */
	call	panicsys
	addq	$REGSIZE, %rsp
	popq	%rdi
	popq	%rsi
	popq	%rdx
	popq	%rcx
	popq	%r8
	popq	%r9
	popq	%rax
	popq	%rbx
	popq	%r10
	popq	%r11
	popfq
	leave
	ret
	SET_SIZE(vpanic)

	ENTRY_NP(dtrace_vpanic)			/* Initial stack layout: */

	pushq	%rbp				/* | %rip | 	0x60	*/
	movq	%rsp, %rbp			/* | %rbp |	0x58	*/
	pushfq					/* | rfl  |	0x50	*/
	pushq	%r11				/* | %r11 |	0x48	*/
	pushq	%r10				/* | %r10 |	0x40	*/
	pushq	%rbx				/* | %rbx |	0x38	*/
	pushq	%rax				/* | %rax |	0x30	*/
	pushq	%r9				/* | %r9  |	0x28	*/
	pushq	%r8				/* | %r8  |	0x20	*/
	pushq	%rcx				/* | %rcx |	0x18	*/
	pushq	%rdx				/* | %rdx |	0x10	*/
	pushq	%rsi				/* | %rsi |	0x8 alist */
	pushq	%rdi				/* | %rdi |	0x0 format */

	movq	%rsp, %rbx			/* %rbx = current %rsp */

	leaq	panic_quiesce(%rip), %rdi	/* %rdi = &panic_quiesce */
	call	dtrace_panic_trigger	/* %eax = dtrace_panic_trigger() */
	jmp	vpanic_common

	SET_SIZE(dtrace_vpanic)

#endif	/* __lint */

/*
 * void prefetch_smap_w(void *)
 *
 * Prefetch ahead within a linear list of smap structures.
 * Not implemented for ia32.  Stub for compatibility.
 */

#if defined(__lint)

/*ARGSUSED*/
void prefetch_smap_w(void *smp)
{}

#else	/* __lint */

	ENTRY(prefetch_smap_w)
	rep;	ret	/* use 2 byte return instruction when branch target */
			/* AMD Software Optimization Guide - Section 6.2 */
	SET_SIZE(prefetch_smap_w)

#endif	/* __lint */

/*
 * prefetch_page_r(page_t *)
 * issue prefetch instructions for a page_t
 */
#if defined(__lint)

/*ARGSUSED*/
void
prefetch_page_r(void *pp)
{}

#else	/* __lint */

	ENTRY(prefetch_page_r)
	rep;	ret	/* use 2 byte return instruction when branch target */
			/* AMD Software Optimization Guide - Section 6.2 */
	SET_SIZE(prefetch_page_r)

#endif	/* __lint */

#if defined(__lint)

/*ARGSUSED*/
int
bcmp(const void *s1, const void *s2, size_t count)
{ return (0); }

#else   /* __lint */

	ENTRY(bcmp)
	pushq	%rbp
	movq	%rsp, %rbp
#ifdef DEBUG
	movq	postbootkernelbase(%rip), %r11
	cmpq	%r11, %rdi
	jb	0f
	cmpq	%r11, %rsi
	jnb	1f
0:	leaq	.bcmp_panic_msg(%rip), %rdi
	xorl	%eax, %eax
	call	panic
1:
#endif	/* DEBUG */
	call	memcmp
	testl	%eax, %eax
	setne	%dl
	leave
	movzbl	%dl, %eax
	ret
	SET_SIZE(bcmp)
	
#ifdef DEBUG
	.text
.bcmp_panic_msg:
	.string "bcmp: arguments below kernelbase"
#endif	/* DEBUG */

#endif	/* __lint */

#if defined(__lint)

uint_t
bsrw_insn(uint16_t mask)
{
	uint_t index = sizeof (mask) * NBBY - 1;

	while ((mask & (1 << index)) == 0)
		index--;
	return (index);
}

#else	/* __lint */

	ENTRY_NP(bsrw_insn)
	xorl	%eax, %eax
	bsrw	%di, %ax
	ret
	SET_SIZE(bsrw_insn)

#endif	/* __lint */

#if defined(__lint)

/*ARGSUSED*/
void
switch_sp_and_call(void *newsp, void (*func)(uint_t, uint_t), uint_t arg1,
	    uint_t arg2)
{}

#else	/* __lint */

	ENTRY_NP(switch_sp_and_call)
	pushq	%rbp
	movq	%rsp, %rbp		/* set up stack frame */
	movq	%rdi, %rsp		/* switch stack pointer */
	movq	%rdx, %rdi		/* pass func arg 1 */
	movq	%rsi, %r11		/* save function to call */
	movq	%rcx, %rsi		/* pass func arg 2 */
	call	*%r11			/* call function */
	leave				/* restore stack */
	ret
	SET_SIZE(switch_sp_and_call)

#endif	/* __lint */

#if defined(__lint)

void
kmdb_enter(void)
{}

#else	/* __lint */

	ENTRY_NP(kmdb_enter)
	pushq	%rbp
	movq	%rsp, %rbp

	/*
	 * Save flags, do a 'cli' then return the saved flags
	 */
	call	intr_clear

	int	$T_DBGENTR

	/*
	 * Restore the saved flags
	 */
	movq	%rax, %rdi
	call	intr_restore

	leave
	ret	
	SET_SIZE(kmdb_enter)

#endif	/* __lint */

#if defined(__lint)

void
return_instr(void)
{}

#else	/* __lint */

	ENTRY_NP(return_instr)
	rep;	ret	/* use 2 byte instruction when branch target */
			/* AMD Software Optimization Guide - Section 6.2 */
	SET_SIZE(return_instr)

#endif	/* __lint */

#if defined(__lint)

ulong_t
getflags(void)
{
	return (0);
}

#else	/* __lint */

	ENTRY(getflags)
	pushfq
	popq	%rax
#if defined(__xpv)
	CURTHREAD(%rdi)
	KPREEMPT_DISABLE(%rdi)
	/*
	 * Synthesize the PS_IE bit from the event mask bit
	 */
	CURVCPU(%r11)
	andq    $_BITNOT(PS_IE), %rax
	XEN_TEST_UPCALL_MASK(%r11)
	jnz	1f
	orq	$PS_IE, %rax
1:
	KPREEMPT_ENABLE_NOKP(%rdi)
#endif
	ret
	SET_SIZE(getflags)

#endif	/* __lint */

#if defined(__lint)

ftrace_icookie_t
ftrace_interrupt_disable(void)
{ return (0); }

#else   /* __lint */

	ENTRY(ftrace_interrupt_disable)
	pushfq
	popq	%rax
	CLI(%rdx)
	ret
	SET_SIZE(ftrace_interrupt_disable)

#endif	/* __lint */

#if defined(__lint)

/*ARGSUSED*/
void
ftrace_interrupt_enable(ftrace_icookie_t cookie)
{}

#else	/* __lint */

	ENTRY(ftrace_interrupt_enable)
	pushq	%rdi
	popfq
	ret
	SET_SIZE(ftrace_interrupt_enable)

#endif	/* __lint */

#if defined (__lint)

/*ARGSUSED*/
void
clflush_insn(caddr_t addr)
{}

#else /* __lint */

	ENTRY(clflush_insn)
	clflush (%rdi)
	ret
	SET_SIZE(clflush_insn)

#endif /* __lint */

#if defined (__lint)
/*ARGSUSED*/
void
mfence_insn(void)
{}

#else /* __lint */

	ENTRY(mfence_insn)
	mfence
	ret
	SET_SIZE(mfence_insn)

#endif /* __lint */

/*
 * This is how VMware lets the guests figure that they are running
 * on top of VMWare platform :
 * Write 0xA in the ECX register and put the I/O port address value of
 * 0x564D5868 in the EAX register. Then read a word from port 0x5658.
 * If VMWare is installed than this code will be executed correctly and
 * the EBX register will contain the same I/O port address value of 0x564D5868.
 * If VMWare is not installed then OS will return an exception on port access. 
 */
#if defined(__lint)

int
vmware_platform(void) { return (1); }

#else

	ENTRY(vmware_platform)
	pushq	%rbx
	xorl	%ebx, %ebx
	movl	$0x564d5868, %eax
	movl	$0xa, %ecx
	movl	$0x5658, %edx
	inl	(%dx)
	movl	$0x564d5868, %ecx
	xorl	%eax, %eax
	cmpl	%ecx, %ebx
	jne	1f
	incl	%eax
1:
	popq	%rbx
	ret
	SET_SIZE(vmware_platform)

#endif /* __lint */
