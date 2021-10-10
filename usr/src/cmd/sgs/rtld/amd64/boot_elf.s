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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#if	defined(lint)

#include	<sys/types.h>
#include	<_rtld.h>
#include	<_audit.h>
#include	<_elf.h>
#include	<sys/regset.h>

/* ARGSUSED0 */
int
elf_plt_trace()
{
	return (0);
}
#else

#include	<link.h>
#include	<_audit.h>
#include	<sys/asm_linkage.h>

	.file	"boot_elf.s"
	.text

/*
 * On entry the 'glue code' has already  done the following:
 *
 *	pushq	%rbp
 *	movq	%rsp, %rbp
 *	subq	$0x10, %rsp
 *	leaq	trace_fields(%rip), %r11
 *	movq	%r11, -0x8(%rbp)
 *	movq	$elf_plt_trace, %r11
 *	jmp	*%r11
 *
 * so - -8(%rbp) contains the dyndata ptr
 *
 *	0x0	Addr		*reflmp
 *	0x8	Addr		*deflmp
 *	0x10	Word		symndx
 *	0x14	Word		sb_flags
 *	0x18	Sym		symdef.st_name
 *	0x1c			symdef.st_info
 *	0x1d			symdef.st_other
 *	0x1e			symdef.st_shndx
 *	0x20			symdef.st_value
 *	0x28			symdef.st_size
 *
 * Also note - on entry 16 bytes have already been subtracted
 * from the %rsp.  The first 8 bytes is for the dyn_data_ptr,
 * the second 8 bytes are to align the stack and are available
 * for use.
 */
#define	REFLMP_OFF		0x0	
#define	DEFLMP_OFF		0x8	
#define	SYMNDX_OFF		0x10
#define	SBFLAGS_OFF		0x14
#define	SYMDEF_OFF		0x18
#define	SYMDEF_VALUE_OFF	0x20
/*
 * Local stack space storage for elf_plt_trace is allocated
 * as follows:
 *
 *  First - before we got here - %rsp has been decremented
 *  by 0x10 to make space for the dyndata ptr (and another
 *  free word).  In addition to that, we create space
 *  for the following:
 *
 *	La_amd64_regs	    8 * 8:	64
 *	prev_stack_size	    8		 8
 *	Saved regs:
 *	    %rdi			 8
 *	    %rsi			 8
 *	    %rdx			 8
 *	    %rcx			 8
 *	    %r8				 8
 *	    %r9				 8
 *	    %r10			 8
 *	    %r11			 8
 *	    %rax			 8
 *				    =======
 *			    Subtotal:	144 (16byte aligned)
 *
 *	Saved Media Regs (used to pass floating point args):
 *	    %xmm0 - %xmm7   16 * 8:	128
 *				    =======
 *			    Total:	272 (16byte aligned)
 *  
 *  So - will subtract the following to create enough space
 *
 *	-8(%rbp)	store dyndata ptr
 *	-16(%rbp)	store call destination
 *	-80(%rbp)	space for La_amd64_regs
 *	-88(%rbp)	prev stack size
 *  The next %rbp offsets are only true if the caller had correct stack
 *  alignment.  See note above SPRDIOFF for why we use %rsp alignment to
 *  access these stack fields.
 *	-96(%rbp)	entering %rdi
 *	-104(%rbp)	entering %rsi
 *	-112(%rbp)	entering %rdx
 *	-120(%rbp)	entering %rcx
 *	-128(%rbp)	entering %r8
 *	-136(%rbp)	entering %r9
 *	-144(%rbp)	entering %r10
 *	-152(%rbp)	entering %r11
 *	-160(%rbp)	entering %rax
 *	-176(%rbp)	entering %xmm0
 *	-192(%rbp)	entering %xmm1
 *	-208(%rbp)	entering %xmm2
 *	-224(%rbp)	entering %xmm3
 *	-240(%rbp)	entering %xmm4
 *	-256(%rbp)	entering %xmm5
 *	-272(%rbp)	entering %xmm6
 *	-288(%rbp)	entering %xmm7
 *
 */
#define	SPDYNOFF    -8
#define	SPDESTOFF   -16
#define	SPLAREGOFF  -80
#define	SPPRVSTKOFF -88

/*
 * The next set of offsets are relative to %rsp.
 * We guarantee %rsp is ABI compliant 16-byte aligned.  This guarantees the
 * xmm registers are saved to 16-byte aligned addresses.
 * %rbp may only be 8 byte aligned if we came in from non-ABI compliant code.
 */ 
#define	SPRDIOFF	192
#define	SPRSIOFF	184
#define	SPRDXOFF	176
#define	SPRCXOFF	168
#define	SPR8OFF		160
#define	SPR9OFF		152
#define	SPR10OFF	144
#define	SPR11OFF	136
#define	SPRAXOFF	128
#define	SPXMM0OFF	112
#define	SPXMM1OFF	96
#define	SPXMM2OFF	80
#define	SPXMM3OFF	64
#define	SPXMM4OFF	48
#define	SPXMM5OFF	32
#define	SPXMM6OFF	16
#define	SPXMM7OFF	0

	.globl	elf_plt_trace
	.type	elf_plt_trace,@function
	.align 16
elf_plt_trace:
	/*
	 * Enforce ABI 16-byte stack alignment here.
	 * The next andq instruction does this pseudo code:
	 * If %rsp is 8 byte aligned then subtract 8 from %rsp.
	 */
	andq    $-16, %rsp	/* enforce ABI 16-byte stack alignment */
	subq	$272,%rsp	/ create some local storage

	movq	%rdi, SPRDIOFF(%rsp)
	movq	%rsi, SPRSIOFF(%rsp)
	movq	%rdx, SPRDXOFF(%rsp)
	movq	%rcx, SPRCXOFF(%rsp)
	movq	%r8, SPR8OFF(%rsp)
	movq	%r9, SPR9OFF(%rsp)
	movq	%r10, SPR10OFF(%rsp)
	movq	%r11, SPR11OFF(%rsp)
	movq	%rax, SPRAXOFF(%rsp)
	movdqa	%xmm0, SPXMM0OFF(%rsp)
	movdqa	%xmm1, SPXMM1OFF(%rsp)
	movdqa	%xmm2, SPXMM2OFF(%rsp)
	movdqa	%xmm3, SPXMM3OFF(%rsp)
	movdqa	%xmm4, SPXMM4OFF(%rsp)
	movdqa	%xmm5, SPXMM5OFF(%rsp)
	movdqa	%xmm6, SPXMM6OFF(%rsp)
	movdqa	%xmm7, SPXMM7OFF(%rsp)

	movq	SPDYNOFF(%rbp), %rax			/ %rax = dyndata
	testb	$LA_SYMB_NOPLTENTER, SBFLAGS_OFF(%rax)	/ <link.h>
	je	.start_pltenter
	movq	SYMDEF_VALUE_OFF(%rax), %rdi
	movq	%rdi, SPDESTOFF(%rbp)		/ save destination address
	jmp	.end_pltenter

.start_pltenter:
	/*
	 * save all registers into La_amd64_regs
	 */
	leaq	SPLAREGOFF(%rbp), %rsi	/ %rsi = &La_amd64_regs
	leaq	8(%rbp), %rdi
	movq	%rdi, 0(%rsi)		/ la_rsp
	movq	0(%rbp), %rdi
	movq	%rdi, 8(%rsi)		/ la_rbp
	movq	SPRDIOFF(%rsp), %rdi
	movq	%rdi, 16(%rsi)		/ la_rdi
	movq	SPRSIOFF(%rsp), %rdi
	movq	%rdi, 24(%rsi)		/ la_rsi
	movq	SPRDXOFF(%rsp), %rdi
	movq	%rdi, 32(%rsi)		/ la_rdx
	movq	SPRCXOFF(%rsp), %rdi
	movq	%rdi, 40(%rsi)		/ la_rcx
	movq	SPR8OFF(%rsp), %rdi
	movq	%rdi, 48(%rsi)		/ la_r8
	movq	SPR9OFF(%rsp), %rdi
	movq	%rdi, 56(%rsi)		/ la_r9

	/*
	 * prepare for call to la_pltenter
	 */
	movq	SPDYNOFF(%rbp), %r11		/ %r11 = &dyndata
	leaq	SBFLAGS_OFF(%r11), %r9		/ arg6 (&sb_flags)
	leaq	SPLAREGOFF(%rbp), %r8		/ arg5 (&La_amd64_regs)
	movl	SYMNDX_OFF(%r11), %ecx		/ arg4 (symndx)
	leaq	SYMDEF_OFF(%r11), %rdx		/ arg3 (&Sym)
	movq	DEFLMP_OFF(%r11), %rsi		/ arg2 (dlmp)
	movq	REFLMP_OFF(%r11), %rdi		/ arg1 (rlmp)
	call	audit_pltenter@PLT
	movq	%rax, SPDESTOFF(%rbp)		/ save calling address
.end_pltenter:

	/*
	 * If *no* la_pltexit() routines exist
	 * we do not need to keep the stack frame
	 * before we call the actual routine.  Instead we
	 * jump to it and remove our stack from the stack
	 * at the same time.
	 */
	movl	audit_flags(%rip), %eax
	andl	$AF_PLTEXIT, %eax		/ value of audit.h:AF_PLTEXIT
	cmpl	$0, %eax
	je	.bypass_pltexit
	/*
	 * Has the *nopltexit* flag been set for this entry point
	 */
	movq	SPDYNOFF(%rbp), %r11		/ %r11 = &dyndata
	testb	$LA_SYMB_NOPLTEXIT, SBFLAGS_OFF(%r11)
	je	.start_pltexit

.bypass_pltexit:
	/*
	 * No PLTEXIT processing required.
	 */
	movq	0(%rbp), %r11
	movq	%r11, -8(%rbp)			/ move prev %rbp
	movq	SPDESTOFF(%rbp), %r11		/ r11 == calling destination
	movq	%r11, 0(%rbp)			/ store destination at top

	/
	/ Restore registers
	/
	movq	SPRDIOFF(%rsp), %rdi
	movq	SPRSIOFF(%rsp), %rsi
	movq	SPRDXOFF(%rsp), %rdx
	movq	SPRCXOFF(%rsp), %rcx
	movq	SPR8OFF(%rsp), %r8
	movq	SPR9OFF(%rsp), %r9
	movq	SPR10OFF(%rsp), %r10
	movq	SPR11OFF(%rsp), %r11
	movq	SPRAXOFF(%rsp), %rax
	movdqa	SPXMM0OFF(%rsp), %xmm0
	movdqa	SPXMM1OFF(%rsp), %xmm1
	movdqa	SPXMM2OFF(%rsp), %xmm2
	movdqa	SPXMM3OFF(%rsp), %xmm3
	movdqa	SPXMM4OFF(%rsp), %xmm4
	movdqa	SPXMM5OFF(%rsp), %xmm5
	movdqa	SPXMM6OFF(%rsp), %xmm6
	movdqa	SPXMM7OFF(%rsp), %xmm7

	subq	$8, %rbp			/ adjust %rbp for 'ret'
	movq	%rbp, %rsp			/
	/*
	 * At this point, after a little doctoring, we should
	 * have the following on the stack:
	 *
	 *	16(%rsp):  ret addr
	 *	8(%rsp):  dest_addr
	 *	0(%rsp):  Previous %rbp
	 *
	 * So - we pop the previous %rbp, and then
	 * ret to our final destination.
	 */
	popq	%rbp				/
	ret					/ jmp to final destination
						/ and clean up stack :)

.start_pltexit:
	/*
	 * In order to call the destination procedure and then return
	 * to audit_pltexit() for post analysis we must first grow
	 * our stack frame and then duplicate the original callers
	 * stack state.  This duplicates all of the arguements
	 * that were to be passed to the destination procedure.
	 */
	movq	%rbp, %rdi			/
	addq	$16, %rdi			/    %rdi = src
	movq	(%rbp), %rdx			/
	subq	%rdi, %rdx			/    %rdx == prev frame sz
	/*
	 * If audit_argcnt > 0 then we limit the number of
	 * arguements that will be duplicated to audit_argcnt.
	 *
	 * If (prev_stack_size > (audit_argcnt * 8))
	 *	prev_stack_size = audit_argcnt * 8;
	 */
	movl	audit_argcnt(%rip),%eax		/   %eax = audit_argcnt
	cmpl	$0, %eax
	jle	.grow_stack
	leaq	(,%rax,8), %rax			/    %eax = %eax * 4
	cmpq	%rax,%rdx
	jle	.grow_stack
	movq	%rax, %rdx
	/*
	 * Grow the stack and duplicate the arguements of the
	 * original caller.
	 *
	 * We save %rsp in %r11 since we need to use the current rsp for
	 * accessing the registers saved in our stack frame.
	 */
.grow_stack:
	movq	%rsp, %r11
	subq	%rdx, %rsp			/    grow the stack 
	movq	%rdx, SPPRVSTKOFF(%rbp)		/    -88(%rbp) == prev frame sz
	movq	%rsp, %rcx			/    %rcx = dest
	addq	%rcx, %rdx			/    %rdx == tail of dest
.while_base:
	cmpq	%rdx, %rcx			/   while (base+size >= src++) {
	jge	.end_while			/
	movq	(%rdi), %rsi
	movq	%rsi,(%rcx)			/        *dest = *src
	addq	$8, %rdi			/	 src++
	addq	$8, %rcx			/        dest++
	jmp	.while_base			/    }

	/*
	 * The above stack is now an exact duplicate of
	 * the stack of the original calling procedure.
	 */
.end_while:
	/
	/ Restore registers using %r11 which contains our old %rsp value
	/ before growing the stack.
	/
	movq	SPRDIOFF(%r11), %rdi
	movq	SPRSIOFF(%r11), %rsi
	movq	SPRDXOFF(%r11), %rdx
	movq	SPRCXOFF(%r11), %rcx
	movq	SPR8OFF(%r11), %r8
	movq	SPR9OFF(%r11), %r9
	movq	SPR10OFF(%r11), %r10
	movq	SPRAXOFF(%r11), %rax
	movdqa	SPXMM0OFF(%r11), %xmm0
	movdqa	SPXMM1OFF(%r11), %xmm1
	movdqa	SPXMM2OFF(%r11), %xmm2
	movdqa	SPXMM3OFF(%r11), %xmm3
	movdqa	SPXMM4OFF(%r11), %xmm4
	movdqa	SPXMM5OFF(%r11), %xmm5
	movdqa	SPXMM6OFF(%r11), %xmm6
	movdqa	SPXMM7OFF(%r11), %xmm7
	movq	SPR11OFF(%r11), %r11		/ retore %r11 last

	/*
	 * Call to desitnation function - we'll return here
	 * for pltexit monitoring.
	 */
	call	*SPDESTOFF(%rbp)

	addq	SPPRVSTKOFF(%rbp), %rsp	/ cleanup dupped stack

	/
	/ prepare for call to audit_pltenter()
	/
	movq	SPDYNOFF(%rbp), %r11		/ %r11 = &dyndata
	movq	SYMNDX_OFF(%r11), %r8		/ arg5 (symndx)
	leaq	SYMDEF_OFF(%r11), %rcx		/ arg4 (&Sym)
	movq	DEFLMP_OFF(%r11), %rdx		/ arg3 (dlmp)
	movq	REFLMP_OFF(%r11), %rsi		/ arg2 (rlmp)
	movq	%rax, %rdi			/ arg1 (returnval)
	call	audit_pltexit@PLT
	
	/*
	 * Clean up after ourselves and return to the
	 * original calling procedure.
	 */

	/
	/ Restore registers
	/
	movq	SPRDIOFF(%rsp), %rdi
	movq	SPRSIOFF(%rsp), %rsi
	movq	SPRDXOFF(%rsp), %rdx
	movq	SPRCXOFF(%rsp), %rcx
	movq	SPR8OFF(%rsp), %r8
	movq	SPR9OFF(%rsp), %r9
	movq	SPR10OFF(%rsp), %r10
	movq	SPR11OFF(%rsp), %r11
	// rax already contains return value
	movdqa	SPXMM0OFF(%rsp), %xmm0
	movdqa	SPXMM1OFF(%rsp), %xmm1
	movdqa	SPXMM2OFF(%rsp), %xmm2
	movdqa	SPXMM3OFF(%rsp), %xmm3
	movdqa	SPXMM4OFF(%rsp), %xmm4
	movdqa	SPXMM5OFF(%rsp), %xmm5
	movdqa	SPXMM6OFF(%rsp), %xmm6
	movdqa	SPXMM7OFF(%rsp), %xmm7

	movq	%rbp, %rsp			/
	popq	%rbp				/
	ret					/ return to caller
	.size	elf_plt_trace, .-elf_plt_trace
#endif

/*
 * We got here because a call to a function resolved to a procedure
 * linkage table entry.  That entry did a JMPL to the first PLT entry, which
 * in turn did a call to elf_rtbndr.
 *
 * the code sequence that got us here was:
 *
 * .PLT0:
 *	pushq	GOT+8(%rip)	#GOT[1]
 *	jmp	*GOT+16(%rip)	#GOT[2]
 *	nop
 *	nop
 *	nop
 *	nop
 *	...
 * PLT entry for foo:
 *	jmp	*name1@GOTPCREL(%rip)
 *	pushl	$rel.plt.foo
 *	jmp	PLT0
 *
 * At entry, the stack looks like this:
 *
 *	return address			16(%rsp)
 *	$rel.plt.foo	(plt index)	8(%rsp)
 *	lmp				0(%rsp)
 *
 */
#if defined(lint)

extern unsigned long	elf_bndr(Rt_map *, unsigned long, caddr_t);

void
elf_rtbndr(Rt_map * lmp, unsigned long reloc, caddr_t pc)
{
	(void) elf_bndr(lmp, reloc, pc);
}

#else

/*
 * The PLT code that landed us here placed 2 arguments on the stack as
 * arguments to elf_rtbndr.
 * Additionally the pc of caller is below these 2 args.
 * Our stack will look like this after we establish a stack frame with
 * push %rbp; movq %rsp, %rbp sequence:
 *
 *	8(%rbp)			arg1 - *lmp
 *	16(%rbp), %rsi		arg2 - reloc index
 *	24(%rbp), %rdx		arg3 - pc of caller
 */
#define	LBPLMPOFF	8	/* arg1 - *lmp */
#define	LBPRELOCOFF	16	/* arg2 - reloc index */
#define	LBRPCOFF	24	/* arg3 - pc of caller */

/*
 * Possible arguments for the resolved function are in registers as per
 * the AMD64 ABI.  We must save on the local stack all possible register
 * arguments before interposing functions to resolve the called function. 
 * Possible arguments must be restored before invoking the resolved function.
 *
 * Local stack space storage for elf_rtbndr is allocated as follows:
 *
 *	Saved regs:
 *	    %rax			 8
 *	    %rdi			 8
 *	    %rsi			 8
 *	    %rdx			 8
 *	    %rcx			 8
 *	    %r8				 8
 *	    %r9				 8
 *	    %r10			 8
 *				    =======
 *			    Subtotal:   64 (16byte aligned)
 *
 *	Saved Media Regs (used to pass floating point args):
 *	    %xmm0 - %xmm7   16 * 8:    128
 *				    =======
 *			    Total:     192 (16byte aligned)
 *  
 *  So - will subtract the following to create enough space
 *
 *	0(%rsp)		save %rax
 *	8(%rsp)		save %rdi
 *	16(%rsp)	save %rsi
 *	24(%rsp)	save %rdx
 *	32(%rsp)	save %rcx
 *	40(%rsp)	save %r8
 *	48(%rsp)	save %r9
 *	56(%rsp)	save %r10
 *	64(%rsp)	save %xmm0
 *	80(%rsp)	save %xmm1
 *	96(%rsp)	save %xmm2
 *	112(%rsp)	save %xmm3
 *	128(%rsp)	save %xmm4
 *	144(%rsp)	save %xmm5
 *	160(%rsp)	save %xmm6
 *	176(%rsp)	save %xmm7
 *
 * Note: Some callers may use 8-byte stack alignment instead of the
 * ABI required 16-byte alignment.  We use %rsp offsets to save/restore
 * registers because %rbp may not be 16-byte aligned.  We guarantee %rsp
 * is 16-byte aligned in the function preamble.
 */
#define	LS_SIZE	$192	/* local stack space to save all possible arguments */
#define	LSRAXOFF	0	/* for SSE register count */
#define	LSRDIOFF	8	/* arg 0 ... */
#define	LSRSIOFF	16
#define	LSRDXOFF	24
#define	LSRCXOFF	32
#define	LSR8OFF		40
#define	LSR9OFF		48
#define	LSR10OFF	56	/* ... arg 5 */
#define	LSXMM0OFF	64	/* SSE arg 0 ... */
#define	LSXMM1OFF	80
#define	LSXMM2OFF	96
#define	LSXMM3OFF	112
#define	LSXMM4OFF	128
#define	LSXMM5OFF	144
#define	LSXMM6OFF	160
#define	LSXMM7OFF	176	/* ... SSE arg 7 */

	.weak	_elf_rtbndr
	_elf_rtbndr = elf_rtbndr

	ENTRY(elf_rtbndr)

	pushq	%rbp
	movq	%rsp, %rbp

	/*
	 * Some libraries may (incorrectly) use non-ABI compliant 8-byte stack
	 * alignment.  Enforce ABI 16-byte stack alignment here.
	 * The next andq instruction does this pseudo code:
	 * If %rsp is 8 byte aligned then subtract 8 from %rsp.
	 */
	andq	$-16, %rsp	/* enforce ABI 16-byte stack alignment */

	subq	LS_SIZE, %rsp	/* save all ABI defined argument registers */

	movq	%rax, LSRAXOFF(%rsp)	/* for SSE register count */
	movq	%rdi, LSRDIOFF(%rsp)	/*  arg 0 .. */
	movq	%rsi, LSRSIOFF(%rsp)
	movq	%rdx, LSRDXOFF(%rsp)
	movq	%rcx, LSRCXOFF(%rsp)
	movq	%r8, LSR8OFF(%rsp)
	movq	%r9, LSR9OFF(%rsp)	/* .. arg 5 */
	movq	%r10, LSR10OFF(%rsp)	/* call chain reg */

	movdqa	%xmm0, LSXMM0OFF(%rsp)	/* SSE arg 0 ... */
	movdqa	%xmm1, LSXMM1OFF(%rsp)
	movdqa	%xmm2, LSXMM2OFF(%rsp)
	movdqa	%xmm3, LSXMM3OFF(%rsp)
	movdqa	%xmm4, LSXMM4OFF(%rsp)
	movdqa	%xmm5, LSXMM5OFF(%rsp)
	movdqa	%xmm6, LSXMM6OFF(%rsp)
	movdqa	%xmm7, LSXMM7OFF(%rsp)	/* ... SSE arg 7 */

	movq	LBPLMPOFF(%rbp), %rdi	/* arg1 - *lmp */
	movq	LBPRELOCOFF(%rbp), %rsi	/* arg2 - reloc index */
	movq	LBRPCOFF(%rbp), %rdx	/* arg3 - pc of caller */
	call	elf_bndr@PLT		/* call elf_rtbndr(lmp, relndx, pc) */
	movq	%rax, LBPRELOCOFF(%rbp)	/* store final destination */

	/* restore possible arguments before invoking resolved function */
	movq	LSRAXOFF(%rsp), %rax
	movq	LSRDIOFF(%rsp), %rdi
	movq	LSRSIOFF(%rsp), %rsi
	movq	LSRDXOFF(%rsp), %rdx
	movq	LSRCXOFF(%rsp), %rcx
	movq	LSR8OFF(%rsp), %r8
	movq	LSR9OFF(%rsp), %r9
	movq	LSR10OFF(%rsp), %r10

	movdqa	LSXMM0OFF(%rsp), %xmm0
	movdqa	LSXMM1OFF(%rsp), %xmm1
	movdqa	LSXMM2OFF(%rsp), %xmm2
	movdqa	LSXMM3OFF(%rsp), %xmm3
	movdqa	LSXMM4OFF(%rsp), %xmm4
	movdqa	LSXMM5OFF(%rsp), %xmm5
	movdqa	LSXMM6OFF(%rsp), %xmm6
	movdqa	LSXMM7OFF(%rsp), %xmm7

	movq	%rbp, %rsp
	popq	%rbp

	addq	$8, %rsp	/* pop 1st plt-pushed args */
				/* the second arguement is used */
				/* for the 'return' address to our */
				/* final destination */

	ret			/* invoke resolved function */
	.size 	elf_rtbndr, .-elf_rtbndr
#endif
