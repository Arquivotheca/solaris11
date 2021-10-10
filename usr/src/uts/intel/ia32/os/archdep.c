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

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

#include <sys/param.h>
#include <sys/types.h>
#include <sys/vmparam.h>
#include <sys/systm.h>
#include <sys/signal.h>
#include <sys/stack.h>
#include <sys/regset.h>
#include <sys/privregs.h>
#include <sys/frame.h>
#include <sys/proc.h>
#include <sys/psw.h>
#include <sys/siginfo.h>
#include <sys/cpuvar.h>
#include <sys/asm_linkage.h>
#include <sys/kmem.h>
#include <sys/errno.h>
#include <sys/bootconf.h>
#include <sys/archsystm.h>
#include <sys/debug.h>
#include <sys/elf.h>
#include <sys/spl.h>
#include <sys/time.h>
#include <sys/atomic.h>
#include <sys/sysmacros.h>
#include <sys/cmn_err.h>
#include <sys/modctl.h>
#include <sys/kobj.h>
#include <sys/panic.h>
#include <sys/reboot.h>
#include <sys/time.h>
#include <sys/fp.h>
#include <sys/x86_archext.h>
#include <sys/auxv.h>
#include <sys/auxv_386.h>
#include <sys/dtrace.h>
#include <sys/brand.h>
#include <sys/machbrand.h>
#include <sys/cmn_err.h>

extern const struct fnsave_state x87_initial;
extern const struct fxsave_state sse_initial;

/*
 * Map an fnsave-formatted save area into an fxsave-formatted save area.
 *
 * Most fields are the same width, content and semantics.  However
 * the tag word is compressed.
 */
static void
fnsave_to_fxsave(const struct fnsave_state *fn, struct fxsave_state *fx)
{
	uint_t i, tagbits;

	fx->fx_fcw = fn->f_fcw;
	fx->fx_fsw = fn->f_fsw;

	/*
	 * copy element by element (because of holes)
	 */
	for (i = 0; i < 8; i++)
		bcopy(&fn->f_st[i].fpr_16[0], &fx->fx_st[i].fpr_16[0],
		    sizeof (fn->f_st[0].fpr_16)); /* 80-bit x87-style floats */

	/*
	 * synthesize compressed tag bits
	 */
	fx->fx_fctw = 0;
	for (tagbits = fn->f_ftw, i = 0; i < 8; i++, tagbits >>= 2)
		if ((tagbits & 3) != 3)
			fx->fx_fctw |= (1 << i);

	fx->fx_fop = fn->f_fop;

	fx->fx_rip = (uint64_t)fn->f_eip;
	fx->fx_rdp = (uint64_t)fn->f_dp;
}

/*
 * Map from an fxsave-format save area to an fnsave-format save area.
 */
static void
fxsave_to_fnsave(const struct fxsave_state *fx, struct fnsave_state *fn)
{
	uint_t i, top, tagbits;

	fn->f_fcw = fx->fx_fcw;
	fn->__f_ign0 = 0;
	fn->f_fsw = fx->fx_fsw;
	fn->__f_ign1 = 0;

	top = (fx->fx_fsw & FPS_TOP) >> 11;

	/*
	 * copy element by element (because of holes)
	 */
	for (i = 0; i < 8; i++)
		bcopy(&fx->fx_st[i].fpr_16[0], &fn->f_st[i].fpr_16[0],
		    sizeof (fn->f_st[0].fpr_16)); /* 80-bit x87-style floats */

	/*
	 * synthesize uncompressed tag bits
	 */
	fn->f_ftw = 0;
	for (tagbits = fx->fx_fctw, i = 0; i < 8; i++, tagbits >>= 1) {
		uint_t ibit, expo;
		const uint16_t *fpp;
		static const uint16_t zero[5] = { 0, 0, 0, 0, 0 };

		if ((tagbits & 1) == 0) {
			fn->f_ftw |= 3 << (i << 1);	/* empty */
			continue;
		}

		/*
		 * (tags refer to *physical* registers)
		 */
		fpp = &fx->fx_st[(i - top + 8) & 7].fpr_16[0];
		ibit = fpp[3] >> 15;
		expo = fpp[4] & 0x7fff;

		if (ibit && expo != 0 && expo != 0x7fff)
			continue;			/* valid fp number */

		if (bcmp(fpp, &zero, sizeof (zero)))
			fn->f_ftw |= 2 << (i << 1);	/* NaN */
		else
			fn->f_ftw |= 1 << (i << 1);	/* fp zero */
	}

	fn->f_fop = fx->fx_fop;

	fn->__f_ign2 = 0;
	fn->f_eip = (uint32_t)fx->fx_rip;
	fn->f_cs = U32CS_SEL;
	fn->f_dp = (uint32_t)fx->fx_rdp;
	fn->f_ds = UDS_SEL;
	fn->__f_ign3 = 0;
}

/*
 * Map from an fpregset_t into an fxsave-format save area
 */
static void
fpregset_to_fxsave(const fpregset_t *fp, struct fxsave_state *fx)
{
	bcopy(fp, fx, sizeof (*fx));

	/*
	 * avoid useless #gp exceptions - mask reserved bits
	 */
	fx->fx_mxcsr &= sse_mxcsr_mask;
}

/*
 * Map from an fxsave-format save area into a fpregset_t
 */
static void
fxsave_to_fpregset(const struct fxsave_state *fx, fpregset_t *fp)
{
	bcopy(fx, fp, sizeof (*fx));
}

#if defined(_SYSCALL32_IMPL)
static void
fpregset32_to_fxsave(const fpregset32_t *fp, struct fxsave_state *fx)
{
	const struct fpchip32_state *fc = &fp->fp_reg_set.fpchip_state;

	fnsave_to_fxsave((const struct fnsave_state *)fc, fx);
	/*
	 * avoid useless #gp exceptions - mask reserved bits
	 */
	fx->fx_mxcsr = sse_mxcsr_mask & fc->mxcsr;
	bcopy(&fc->xmm[0], &fx->fx_xmm[0], sizeof (fc->xmm));
}

static void
fxsave_to_fpregset32(const struct fxsave_state *fx, fpregset32_t *fp)
{
	struct fpchip32_state *fc = &fp->fp_reg_set.fpchip_state;

	fxsave_to_fnsave(fx, (struct fnsave_state *)fc);
	fc->mxcsr = fx->fx_mxcsr;
	bcopy(&fx->fx_xmm[0], &fc->xmm[0], sizeof (fc->xmm));
}

static void
fpregset_nto32(const fpregset_t *src, fpregset32_t *dst)
{
	fxsave_to_fpregset32((struct fxsave_state *)src, dst);
	dst->fp_reg_set.fpchip_state.status =
	    src->fp_reg_set.fpchip_state.status;
	dst->fp_reg_set.fpchip_state.xstatus =
	    src->fp_reg_set.fpchip_state.xstatus;
}

static void
fpregset_32ton(const fpregset32_t *src, fpregset_t *dst)
{
	fpregset32_to_fxsave(src, (struct fxsave_state *)dst);
	dst->fp_reg_set.fpchip_state.status =
	    src->fp_reg_set.fpchip_state.status;
	dst->fp_reg_set.fpchip_state.xstatus =
	    src->fp_reg_set.fpchip_state.xstatus;
}
#endif

/*
 * Set floating-point registers from a native fpregset_t.
 */
void
setfpregs(klwp_t *lwp, fpregset_t *fp)
{
	struct fpu_ctx *fpu = &lwp->lwp_pcb.pcb_fpu;

	kpreempt_disable();

	if (fpu->fpu_flags & FPU_EN) {
		if (!(fpu->fpu_flags & FPU_VALID)) {
			/*
			 * get the current FPU state
			 */
			if (fp_save_mech == FP_XSAVE &&
			    fpu_exists && ttolwp(curthread) == lwp) {
				/*
				 * With XSAVE format, we may have state other
				 * than FP and SSE, and we need to save these
				 * extended state here. For now we just save
				 * everything, then overwrite FP and SSE.
				 */
				fp_save(fpu);
			}
			/*
			 * FPU context is still active, release the
			 * ownership.
			 */
			fp_free(fpu, 0);
		}
	}
	/*
	 * Else: if we are trying to change the FPU state of a thread which
	 * hasn't yet initialized floating point, store the state in
	 * the pcb and indicate that the state is valid.  When the
	 * thread enables floating point, it will use this state instead
	 * of the default state.
	 */

	switch (fp_save_mech) {
	case FP_FXSAVE:
		fpregset_to_fxsave(fp, &fpu->fpu_regs.kfpu_u.kfpu_fx);
		fpu->fpu_regs.kfpu_xstatus =
		    fp->fp_reg_set.fpchip_state.xstatus;
		break;

	case FP_XSAVE:
		fpregset_to_fxsave(fp,
		    &fpu->fpu_regs.kfpu_u.kfpu_xs.xs_fxsave);
		fpu->fpu_regs.kfpu_xstatus =
		    fp->fp_reg_set.fpchip_state.xstatus;
		fpu->fpu_regs.kfpu_u.kfpu_xs.xs_xstate_bv |=
		    (XFEATURE_LEGACY_FP | XFEATURE_SSE);
		break;
	default:
		panic("Invalid fp_save_mech");
		/*NOTREACHED*/
	}

	fpu->fpu_regs.kfpu_status = fp->fp_reg_set.fpchip_state.status;
	fpu->fpu_flags |= FPU_VALID;

	kpreempt_enable();
}

/*
 * Get floating-point registers into a native fpregset_t.
 */
void
getfpregs(klwp_t *lwp, fpregset_t *fp)
{
	struct fpu_ctx *fpu = &lwp->lwp_pcb.pcb_fpu;

	kpreempt_disable();
	if (fpu->fpu_flags & FPU_EN) {
		/*
		 * If we have FPU hw and the thread's pcb doesn't have
		 * a valid FPU state then get the state from the hw.
		 */
		if (fpu_exists && ttolwp(curthread) == lwp &&
		    !(fpu->fpu_flags & FPU_VALID))
			fp_save(fpu); /* get the current FPU state */
	}

	/*
	 * There are 3 possible cases we have to be aware of here:
	 *
	 * 1. FPU is enabled.  FPU state is stored in the current LWP.
	 *
	 * 2. FPU is not enabled, and there have been no intervening /proc
	 *    modifications.  Return initial FPU state.
	 *
	 * 3. FPU is not enabled, but a /proc consumer has modified FPU state.
	 *    FPU state is stored in the current LWP.
	 */
	if ((fpu->fpu_flags & FPU_EN) || (fpu->fpu_flags & FPU_VALID)) {
		/*
		 * Cases 1 and 3.
		 */
		switch (fp_save_mech) {
		case FP_FXSAVE:
			fxsave_to_fpregset(&fpu->fpu_regs.kfpu_u.kfpu_fx, fp);
			fp->fp_reg_set.fpchip_state.xstatus =
			    fpu->fpu_regs.kfpu_xstatus;
			break;
		case FP_XSAVE:
			fxsave_to_fpregset(
			    &fpu->fpu_regs.kfpu_u.kfpu_xs.xs_fxsave, fp);
			fp->fp_reg_set.fpchip_state.xstatus =
			    fpu->fpu_regs.kfpu_xstatus;
			break;
		default:
			panic("Invalid fp_save_mech");
			/*NOTREACHED*/
		}
		fp->fp_reg_set.fpchip_state.status = fpu->fpu_regs.kfpu_status;
	} else {
		/*
		 * Case 2.
		 */
		switch (fp_save_mech) {
		case FP_FXSAVE:
		case FP_XSAVE:
			/*
			 * For now, we don't have any AVX specific field in ABI.
			 * If we add any in the future, we need to initial them
			 * as well.
			 */
			fxsave_to_fpregset(&sse_initial, fp);
			fp->fp_reg_set.fpchip_state.xstatus =
			    fpu->fpu_regs.kfpu_xstatus;
			break;
		default:
			panic("Invalid fp_save_mech");
			/*NOTREACHED*/
		}
		fp->fp_reg_set.fpchip_state.status = fpu->fpu_regs.kfpu_status;
	}
	kpreempt_enable();
}

#if defined(_SYSCALL32_IMPL)

/*
 * Set floating-point registers from an fpregset32_t.
 */
void
setfpregs32(klwp_t *lwp, fpregset32_t *fp)
{
	fpregset_t fpregs;

	fpregset_32ton(fp, &fpregs);
	setfpregs(lwp, &fpregs);
}

/*
 * Get floating-point registers into an fpregset32_t.
 */
void
getfpregs32(klwp_t *lwp, fpregset32_t *fp)
{
	fpregset_t fpregs;

	getfpregs(lwp, &fpregs);
	fpregset_nto32(&fpregs, fp);
}

#endif	/* _SYSCALL32_IMPL */

/*
 * Return the general registers
 */
void
getgregs(klwp_t *lwp, gregset_t grp)
{
	struct regs *rp = lwptoregs(lwp);
	struct pcb *pcb = &lwp->lwp_pcb;
	int thisthread = lwptot(lwp) == curthread;

	grp[REG_RDI] = rp->r_rdi;
	grp[REG_RSI] = rp->r_rsi;
	grp[REG_RDX] = rp->r_rdx;
	grp[REG_RCX] = rp->r_rcx;
	grp[REG_R8] = rp->r_r8;
	grp[REG_R9] = rp->r_r9;
	grp[REG_RAX] = rp->r_rax;
	grp[REG_RBX] = rp->r_rbx;
	grp[REG_RBP] = rp->r_rbp;
	grp[REG_R10] = rp->r_r10;
	grp[REG_R11] = rp->r_r11;
	grp[REG_R12] = rp->r_r12;
	grp[REG_R13] = rp->r_r13;
	grp[REG_R14] = rp->r_r14;
	grp[REG_R15] = rp->r_r15;
	grp[REG_FSBASE] = pcb->pcb_fsbase;
	grp[REG_GSBASE] = pcb->pcb_gsbase;
	if (thisthread)
		kpreempt_disable();
	if (pcb->pcb_rupdate == 1) {
		grp[REG_DS] = pcb->pcb_ds;
		grp[REG_ES] = pcb->pcb_es;
		grp[REG_FS] = pcb->pcb_fs;
		grp[REG_GS] = pcb->pcb_gs;
	} else {
		grp[REG_DS] = rp->r_ds;
		grp[REG_ES] = rp->r_es;
		grp[REG_FS] = rp->r_fs;
		grp[REG_GS] = rp->r_gs;
	}
	if (thisthread)
		kpreempt_enable();
	grp[REG_TRAPNO] = rp->r_trapno;
	grp[REG_ERR] = rp->r_err;
	grp[REG_RIP] = rp->r_rip;
	grp[REG_CS] = rp->r_cs;
	grp[REG_SS] = rp->r_ss;
	grp[REG_RFL] = rp->r_rfl;
	grp[REG_RSP] = rp->r_rsp;
}

#if defined(_SYSCALL32_IMPL)

void
getgregs32(klwp_t *lwp, gregset32_t grp)
{
	struct regs *rp = lwptoregs(lwp);
	struct pcb *pcb = &lwp->lwp_pcb;
	int thisthread = lwptot(lwp) == curthread;

	if (thisthread)
		kpreempt_disable();
	if (pcb->pcb_rupdate == 1) {
		grp[GS] = (uint16_t)pcb->pcb_gs;
		grp[FS] = (uint16_t)pcb->pcb_fs;
		grp[DS] = (uint16_t)pcb->pcb_ds;
		grp[ES] = (uint16_t)pcb->pcb_es;
	} else {
		grp[GS] = (uint16_t)rp->r_gs;
		grp[FS] = (uint16_t)rp->r_fs;
		grp[DS] = (uint16_t)rp->r_ds;
		grp[ES] = (uint16_t)rp->r_es;
	}
	if (thisthread)
		kpreempt_enable();
	grp[EDI] = (greg32_t)rp->r_rdi;
	grp[ESI] = (greg32_t)rp->r_rsi;
	grp[EBP] = (greg32_t)rp->r_rbp;
	grp[ESP] = 0;
	grp[EBX] = (greg32_t)rp->r_rbx;
	grp[EDX] = (greg32_t)rp->r_rdx;
	grp[ECX] = (greg32_t)rp->r_rcx;
	grp[EAX] = (greg32_t)rp->r_rax;
	grp[TRAPNO] = (greg32_t)rp->r_trapno;
	grp[ERR] = (greg32_t)rp->r_err;
	grp[EIP] = (greg32_t)rp->r_rip;
	grp[CS] = (uint16_t)rp->r_cs;
	grp[EFL] = (greg32_t)rp->r_rfl;
	grp[UESP] = (greg32_t)rp->r_rsp;
	grp[SS] = (uint16_t)rp->r_ss;
}

void
ucontext_32ton(const ucontext32_t *src, ucontext_t *dst)
{
	mcontext_t *dmc = &dst->uc_mcontext;
	const mcontext32_t *smc = &src->uc_mcontext;

	bzero(dst, sizeof (*dst));
	dst->uc_flags = src->uc_flags;
	dst->uc_link = (ucontext_t *)(uintptr_t)src->uc_link;

	bcopy(&src->uc_sigmask, &dst->uc_sigmask, sizeof (dst->uc_sigmask));

	dst->uc_stack.ss_sp = (void *)(uintptr_t)src->uc_stack.ss_sp;
	dst->uc_stack.ss_size = (size_t)src->uc_stack.ss_size;
	dst->uc_stack.ss_flags = src->uc_stack.ss_flags;

	dmc->gregs[REG_GS] = (greg_t)(uint32_t)smc->gregs[GS];
	dmc->gregs[REG_FS] = (greg_t)(uint32_t)smc->gregs[FS];
	dmc->gregs[REG_ES] = (greg_t)(uint32_t)smc->gregs[ES];
	dmc->gregs[REG_DS] = (greg_t)(uint32_t)smc->gregs[DS];
	dmc->gregs[REG_RDI] = (greg_t)(uint32_t)smc->gregs[EDI];
	dmc->gregs[REG_RSI] = (greg_t)(uint32_t)smc->gregs[ESI];
	dmc->gregs[REG_RBP] = (greg_t)(uint32_t)smc->gregs[EBP];
	dmc->gregs[REG_RBX] = (greg_t)(uint32_t)smc->gregs[EBX];
	dmc->gregs[REG_RDX] = (greg_t)(uint32_t)smc->gregs[EDX];
	dmc->gregs[REG_RCX] = (greg_t)(uint32_t)smc->gregs[ECX];
	dmc->gregs[REG_RAX] = (greg_t)(uint32_t)smc->gregs[EAX];
	dmc->gregs[REG_TRAPNO] = (greg_t)(uint32_t)smc->gregs[TRAPNO];
	dmc->gregs[REG_ERR] = (greg_t)(uint32_t)smc->gregs[ERR];
	dmc->gregs[REG_RIP] = (greg_t)(uint32_t)smc->gregs[EIP];
	dmc->gregs[REG_CS] = (greg_t)(uint32_t)smc->gregs[CS];
	dmc->gregs[REG_RFL] = (greg_t)(uint32_t)smc->gregs[EFL];
	dmc->gregs[REG_RSP] = (greg_t)(uint32_t)smc->gregs[UESP];
	dmc->gregs[REG_SS] = (greg_t)(uint32_t)smc->gregs[SS];

	/*
	 * A valid fpregs is only copied in if uc.uc_flags has UC_FPU set
	 * otherwise there is no guarantee that anything in fpregs is valid.
	 */
	if (src->uc_flags & UC_FPU)
		fpregset_32ton(&src->uc_mcontext.fpregs,
		    &dst->uc_mcontext.fpregs);
}

#endif	/* _SYSCALL32_IMPL */

/*
 * Return the user-level PC.
 * If in a system call, return the address of the syscall trap.
 */
greg_t
getuserpc()
{
	greg_t upc = lwptoregs(ttolwp(curthread))->r_pc;
	uint32_t insn;

	if (curthread->t_sysnum == 0)
		return (upc);

	/*
	 * We might've gotten here from sysenter (0xf 0x34),
	 * syscall (0xf 0x5) or lcall (0x9a 0 0 0 0 0x27 0).
	 *
	 * Go peek at the binary to figure it out..
	 */
	if (fuword32((void *)(upc - 2), &insn) != -1 &&
	    (insn & 0xffff) == 0x340f || (insn & 0xffff) == 0x050f)
		return (upc - 2);
	return (upc - 7);
}

/*
 * Protect segment registers from non-user privilege levels and GDT selectors
 * other than USER_CS, USER_DS and lwp FS and GS values.  If the segment
 * selector is non-null and not USER_CS/USER_DS, we make sure that the
 * TI bit is set to point into the LDT and that the RPL is set to 3.
 *
 * Since struct regs stores each 16-bit segment register as a 32-bit greg_t, we
 * also explicitly zero the top 16 bits since they may be coming from the
 * user's address space via setcontext(2) or /proc.
 *
 * Note about null selector. When running on the hypervisor if we allow a
 * process to set its %cs to null selector with RPL of 0 the hypervisor will
 * crash the domain. If running on bare metal we would get a #gp fault and
 * be able to kill the process and continue on. Therefore we make sure to
 * force RPL to SEL_UPL even for null selector when setting %cs.
 */

#if defined(IS_CS) || defined(IS_NOT_CS)
#error	"IS_CS and IS_NOT_CS already defined"
#endif

#define	IS_CS		1
#define	IS_NOT_CS	0

/*ARGSUSED*/
static greg_t
fix_segreg(greg_t sr, int iscs, model_t datamodel)
{
	switch (sr &= 0xffff) {

	case 0:
		if (iscs == IS_CS)
			return (0 | SEL_UPL);
		else
			return (0);

	/*
	 * If lwp attempts to switch data model then force their
	 * code selector to be null selector.
	 */
	case U32CS_SEL:
		if (datamodel == DATAMODEL_NATIVE)
			return (0 | SEL_UPL);
		else
			return (sr);

	case UCS_SEL:
		if (datamodel == DATAMODEL_ILP32)
			return (0 | SEL_UPL);

	/*FALLTHROUGH*/
	case UDS_SEL:
	case LWPFS_SEL:
	case LWPGS_SEL:
	case SEL_UPL:
		return (sr);
	default:
		break;
	}

	/*
	 * Force it into the LDT in ring 3 for 32-bit processes, which by
	 * default do not have an LDT, so that any attempt to use an invalid
	 * selector will reference the (non-existant) LDT, and cause a #gp
	 * fault for the process.
	 *
	 * 64-bit processes get the null gdt selector since they
	 * are not allowed to have a private LDT.
	 */
	if (datamodel == DATAMODEL_ILP32) {
		return (sr | SEL_TI_LDT | SEL_UPL);
	} else {
		if (iscs == IS_CS)
			return (0 | SEL_UPL);
		else
			return (0);
	}
}

/*
 * Set general registers.
 */
void
setgregs(klwp_t *lwp, gregset_t grp)
{
	struct regs *rp = lwptoregs(lwp);
	model_t	datamodel = lwp_getdatamodel(lwp);

	struct pcb *pcb = &lwp->lwp_pcb;
	int thisthread = lwptot(lwp) == curthread;

	if (datamodel == DATAMODEL_NATIVE) {

		if (thisthread)
			(void) save_syscall_args();	/* copy the args */

		rp->r_rdi = grp[REG_RDI];
		rp->r_rsi = grp[REG_RSI];
		rp->r_rdx = grp[REG_RDX];
		rp->r_rcx = grp[REG_RCX];
		rp->r_r8 = grp[REG_R8];
		rp->r_r9 = grp[REG_R9];
		rp->r_rax = grp[REG_RAX];
		rp->r_rbx = grp[REG_RBX];
		rp->r_rbp = grp[REG_RBP];
		rp->r_r10 = grp[REG_R10];
		rp->r_r11 = grp[REG_R11];
		rp->r_r12 = grp[REG_R12];
		rp->r_r13 = grp[REG_R13];
		rp->r_r14 = grp[REG_R14];
		rp->r_r15 = grp[REG_R15];
		rp->r_trapno = grp[REG_TRAPNO];
		rp->r_err = grp[REG_ERR];
		rp->r_rip = grp[REG_RIP];
		/*
		 * Setting %cs or %ss to anything else is quietly but
		 * quite definitely forbidden!
		 */
		rp->r_cs = UCS_SEL;
		rp->r_ss = UDS_SEL;
		rp->r_rsp = grp[REG_RSP];

		if (thisthread)
			kpreempt_disable();

		pcb->pcb_ds = UDS_SEL;
		pcb->pcb_es = UDS_SEL;

		/*
		 * 64-bit processes -are- allowed to set their fsbase/gsbase
		 * values directly, but only if they're using the segment
		 * selectors that allow that semantic.
		 *
		 * (32-bit processes must use lwp_set_private().)
		 */
		pcb->pcb_fsbase = grp[REG_FSBASE];
		pcb->pcb_gsbase = grp[REG_GSBASE];
		pcb->pcb_fs = fix_segreg(grp[REG_FS], IS_NOT_CS, datamodel);
		pcb->pcb_gs = fix_segreg(grp[REG_GS], IS_NOT_CS, datamodel);

		/*
		 * Ensure that we go out via update_sregs
		 */
		pcb->pcb_rupdate = 1;
		lwptot(lwp)->t_post_sys = 1;
		if (thisthread)
			kpreempt_enable();
#if defined(_SYSCALL32_IMPL)
	} else {
		rp->r_rdi = (uint32_t)grp[REG_RDI];
		rp->r_rsi = (uint32_t)grp[REG_RSI];
		rp->r_rdx = (uint32_t)grp[REG_RDX];
		rp->r_rcx = (uint32_t)grp[REG_RCX];
		rp->r_rax = (uint32_t)grp[REG_RAX];
		rp->r_rbx = (uint32_t)grp[REG_RBX];
		rp->r_rbp = (uint32_t)grp[REG_RBP];
		rp->r_trapno = (uint32_t)grp[REG_TRAPNO];
		rp->r_err = (uint32_t)grp[REG_ERR];
		rp->r_rip = (uint32_t)grp[REG_RIP];

		rp->r_cs = fix_segreg(grp[REG_CS], IS_CS, datamodel);
		rp->r_ss = fix_segreg(grp[REG_DS], IS_NOT_CS, datamodel);

		rp->r_rsp = (uint32_t)grp[REG_RSP];

		if (thisthread)
			kpreempt_disable();

		pcb->pcb_ds = fix_segreg(grp[REG_DS], IS_NOT_CS, datamodel);
		pcb->pcb_es = fix_segreg(grp[REG_ES], IS_NOT_CS, datamodel);

		/*
		 * (See fsbase/gsbase commentary above)
		 */
		pcb->pcb_fs = fix_segreg(grp[REG_FS], IS_NOT_CS, datamodel);
		pcb->pcb_gs = fix_segreg(grp[REG_GS], IS_NOT_CS, datamodel);

		/*
		 * Ensure that we go out via update_sregs
		 */
		pcb->pcb_rupdate = 1;
		lwptot(lwp)->t_post_sys = 1;
		if (thisthread)
			kpreempt_enable();
#endif
	}

	/*
	 * Only certain bits of the flags register can be modified.
	 */
	rp->r_rfl = (rp->r_rfl & ~PSL_USERMASK) |
	    (grp[REG_RFL] & PSL_USERMASK);
}

/*
 * Get a pc-only stacktrace.  Used for kmem_alloc() buffer ownership tracking.
 * Returns MIN(current stack depth, pcstack_limit).
 */
int
getpcstack(pc_t *pcstack, int pcstack_limit)
{
	struct frame *fp = (struct frame *)getfp();
	struct frame *nextfp, *minfp, *stacktop;
	int depth = 0;
	int on_intr;
	uintptr_t pc;

	if ((on_intr = CPU_ON_INTR(CPU)) != 0)
		stacktop = (struct frame *)(CPU->cpu_intr_stack + SA(MINFRAME));
	else
		stacktop = (struct frame *)curthread->t_stk;
	minfp = fp;

	pc = ((struct regs *)fp)->r_pc;

	while (depth < pcstack_limit) {
		nextfp = (struct frame *)fp->fr_savfp;
		pc = fp->fr_savpc;
		if (nextfp <= minfp || nextfp >= stacktop) {
			if (on_intr) {
				/*
				 * Hop from interrupt stack to thread stack.
				 */
				stacktop = (struct frame *)curthread->t_stk;
				minfp = (struct frame *)curthread->t_stkbase;
				on_intr = 0;
				continue;
			}
			break;
		}
		pcstack[depth++] = (pc_t)pc;
		fp = nextfp;
		minfp = fp;
	}
	return (depth);
}

/*
 * The following ELF header fields are defined as processor-specific
 * in the V8 ABI:
 *
 *	e_ident[EI_DATA]	encoding of the processor-specific
 *				data in the object file
 *	e_machine		processor identification
 *	e_flags			processor-specific flags associated
 *				with the file
 */

/*
 * The value of at_flags reflects a platform's cpu module support.
 * at_flags is used to check for allowing a binary to execute and
 * is passed as the value of the AT_FLAGS auxiliary vector.
 */
int at_flags = 0;

/*
 * Check the processor-specific fields of an ELF header.
 *
 * returns 1 if the fields are valid, 0 otherwise
 */
/*ARGSUSED2*/
int
elfheadcheck(
	unsigned char e_data,
	Elf32_Half e_machine,
	Elf32_Word e_flags)
{
	if (e_data != ELFDATA2LSB)
		return (0);
	if (e_machine == EM_AMD64)
		return (1);
	return (e_machine == EM_386);
}

uint_t auxv_hwcap_include = 0;	/* patch to enable unrecognized features */
uint_t auxv_hwcap_exclude = 0;	/* patch for broken cpus, debugging */
#if defined(_SYSCALL32_IMPL)
uint_t auxv_hwcap32_include = 0;	/* ditto for 32-bit apps */
uint_t auxv_hwcap32_exclude = 0;	/* ditto for 32-bit apps */
#endif

/*
 * Gather information about the processor and place it into auxv_hwcap
 * so that it can be exported to the linker via the aux vector.
 *
 * We use this seemingly complicated mechanism so that we can ensure
 * that /etc/system can be used to override what the system can or
 * cannot discover for itself.
 */
void
bind_hwcap(void)
{
	uint_t cpu_hwcap_flags = cpuid_pass4(NULL);

	auxv_hwcap = (auxv_hwcap_include | cpu_hwcap_flags) &
	    ~auxv_hwcap_exclude;

	/*
	 * On AMD processors, sysenter just doesn't work at all
	 * when the kernel is in long mode.  On IA-32e processors
	 * it does, but there's no real point in all the alternate
	 * mechanism when syscall works on both.
	 *
	 * Besides, the kernel's sysenter handler is expecting a
	 * 32-bit lwp ...
	 */
	auxv_hwcap &= ~AV_386_SEP;

	if (auxv_hwcap_include || auxv_hwcap_exclude)
		cmn_err(CE_CONT, "?user ABI extensions: %b\n",
		    auxv_hwcap, FMT_AV_386);

#if defined(_SYSCALL32_IMPL)
	auxv_hwcap32 = (auxv_hwcap32_include | cpu_hwcap_flags) &
	    ~auxv_hwcap32_exclude;

	/*
	 * If this is an amd64 architecture machine from Intel, then
	 * syscall -doesn't- work in compatibility mode, only sysenter does.
	 *
	 * Sigh.
	 */
	if (!cpuid_syscall32_insn(NULL))
		auxv_hwcap32 &= ~AV_386_AMD_SYSC;

	/*
	 * 32-bit processes can -always- use the lahf/sahf instructions
	 */
	auxv_hwcap32 |= AV_386_AHF;

	if (auxv_hwcap32_include || auxv_hwcap32_exclude)
		cmn_err(CE_CONT, "?32-bit user ABI extensions: %b\n",
		    auxv_hwcap32, FMT_AV_386);
#endif
}

/*
 *	sync_icache() - this is called
 *	in proc/fs/prusrio.c. x86 has an unified cache and therefore
 *	this is a nop.
 */
/* ARGSUSED */
void
sync_icache(caddr_t addr, uint_t len)
{
	/* Do nothing for now */
}

/*ARGSUSED*/
void
sync_data_memory(caddr_t va, size_t len)
{
	/* Not implemented for this platform */
}

int
__ipltospl(int ipl)
{
	return (ipltospl(ipl));
}

/*
 * The panic code invokes panic_saveregs() to record the contents of a
 * regs structure into the specified panic_data structure for debuggers.
 */
void
panic_saveregs(panic_data_t *pdp, struct regs *rp)
{
	panic_nv_t *pnv = PANICNVGET(pdp);

	struct cregs	creg;

	getcregs(&creg);

	PANICNVADD(pnv, "rdi", rp->r_rdi);
	PANICNVADD(pnv, "rsi", rp->r_rsi);
	PANICNVADD(pnv, "rdx", rp->r_rdx);
	PANICNVADD(pnv, "rcx", rp->r_rcx);
	PANICNVADD(pnv, "r8", rp->r_r8);
	PANICNVADD(pnv, "r9", rp->r_r9);
	PANICNVADD(pnv, "rax", rp->r_rax);
	PANICNVADD(pnv, "rbx", rp->r_rbx);
	PANICNVADD(pnv, "rbp", rp->r_rbp);
	PANICNVADD(pnv, "r10", rp->r_r10);
	PANICNVADD(pnv, "r10", rp->r_r10);
	PANICNVADD(pnv, "r11", rp->r_r11);
	PANICNVADD(pnv, "r12", rp->r_r12);
	PANICNVADD(pnv, "r13", rp->r_r13);
	PANICNVADD(pnv, "r14", rp->r_r14);
	PANICNVADD(pnv, "r15", rp->r_r15);
	PANICNVADD(pnv, "fsbase", rdmsr(MSR_AMD_FSBASE));
	PANICNVADD(pnv, "gsbase", rdmsr(MSR_AMD_GSBASE));
	PANICNVADD(pnv, "ds", rp->r_ds);
	PANICNVADD(pnv, "es", rp->r_es);
	PANICNVADD(pnv, "fs", rp->r_fs);
	PANICNVADD(pnv, "gs", rp->r_gs);
	PANICNVADD(pnv, "trapno", rp->r_trapno);
	PANICNVADD(pnv, "err", rp->r_err);
	PANICNVADD(pnv, "rip", rp->r_rip);
	PANICNVADD(pnv, "cs", rp->r_cs);
	PANICNVADD(pnv, "rflags", rp->r_rfl);
	PANICNVADD(pnv, "rsp", rp->r_rsp);
	PANICNVADD(pnv, "ss", rp->r_ss);
	PANICNVADD(pnv, "gdt_hi", (uint64_t)(creg.cr_gdt._l[3]));
	PANICNVADD(pnv, "gdt_lo", (uint64_t)(creg.cr_gdt._l[0]));
	PANICNVADD(pnv, "idt_hi", (uint64_t)(creg.cr_idt._l[3]));
	PANICNVADD(pnv, "idt_lo", (uint64_t)(creg.cr_idt._l[0]));

	PANICNVADD(pnv, "ldt", creg.cr_ldt);
	PANICNVADD(pnv, "task", creg.cr_task);
	PANICNVADD(pnv, "cr0", creg.cr_cr0);
	PANICNVADD(pnv, "cr2", creg.cr_cr2);
	PANICNVADD(pnv, "cr3", creg.cr_cr3);
	if (creg.cr_cr4)
		PANICNVADD(pnv, "cr4", creg.cr_cr4);

	PANICNVSET(pdp, pnv);
}

#define	TR_ARG_MAX 6	/* Max args to print, same as SPARC */

/*
 * Print a stack backtrace using the specified frame pointer.  We delay two
 * seconds before continuing, unless this is the panic traceback.
 * If we are in the process of panicking, we also attempt to write the
 * stack backtrace to a staticly assigned buffer, to allow the panic
 * code to find it and write it in to uncompressed pages within the
 * system crash dump.
 * Note that the frame for the starting stack pointer value is omitted because
 * the corresponding %eip is not known.
 */

extern char *dump_stack_scratch;

void
traceback(caddr_t fpreg)
{
	struct frame	*fp = (struct frame *)fpreg;
	struct frame	*nextfp;
	uintptr_t	pc, nextpc;
	ulong_t		off;
	char		args[TR_ARG_MAX * 2 + 16], *sym;
	uint_t	  offset = 0;
	uint_t	  next_offset = 0;
	char	    stack_buffer[1024];

	if (!panicstr)
		printf("traceback: %%fp = %p\n", (void *)fp);

	if (panicstr && !dump_stack_scratch) {
		printf("Warning - stack not written to the dump buffer\n");
	}

	fp = (struct frame *)plat_traceback(fpreg);
	if ((uintptr_t)fp < KERNELBASE)
		goto out;

	pc = fp->fr_savpc;
	fp = (struct frame *)fp->fr_savfp;

	while ((uintptr_t)fp >= KERNELBASE) {
		/*
		 * XX64 Until port is complete tolerate 8-byte aligned
		 * frame pointers but flag with a warning so they can
		 * be fixed.
		 */
		if (((uintptr_t)fp & (STACK_ALIGN - 1)) != 0) {
			if (((uintptr_t)fp & (8 - 1)) == 0) {
				printf("  >> warning! 8-byte"
				    " aligned %%fp = %p\n", (void *)fp);
			} else {
				printf(
				    "  >> mis-aligned %%fp = %p\n", (void *)fp);
				break;
			}
		}

		args[0] = '\0';
		nextpc = (uintptr_t)fp->fr_savpc;
		nextfp = (struct frame *)fp->fr_savfp;
		if ((sym = kobj_getsymname(pc, &off)) != NULL) {
			printf("%016lx %s:%s+%lx (%s)\n", (uintptr_t)fp,
			    mod_containing_pc((caddr_t)pc), sym, off, args);
			(void) snprintf(stack_buffer, sizeof (stack_buffer),
			    "%s:%s+%lx (%s) | ",
			    mod_containing_pc((caddr_t)pc), sym, off, args);
		} else {
			printf("%016lx %lx (%s)\n",
			    (uintptr_t)fp, pc, args);
			(void) snprintf(stack_buffer, sizeof (stack_buffer),
			    "%lx (%s) | ", pc, args);
		}

		if (panicstr && dump_stack_scratch) {
			next_offset = offset + strlen(stack_buffer);
			if (next_offset < STACK_BUF_SIZE) {
				bcopy(stack_buffer, dump_stack_scratch + offset,
				    strlen(stack_buffer));
				offset = next_offset;
			} else {
				/*
				 * In attempting to save the panic stack
				 * to the dumpbuf we have overflowed that area.
				 * Print a warning and continue to printf the
				 * stack to the msgbuf
				 */
				printf("Warning: stack in the dump buffer"
				    " may be incomplete\n");
				offset = next_offset;
			}
		}

		pc = nextpc;
		fp = nextfp;
	}
out:
	if (!panicstr) {
		printf("end of traceback\n");
		DELAY(2 * MICROSEC);
	} else if (dump_stack_scratch) {
		dump_stack_scratch[offset] = '\0';
	}
}

/*
 * Generate a stack backtrace from a saved register set.
 */
void
traceregs(struct regs *rp)
{
	traceback((caddr_t)rp->r_fp);
}

void
exec_set_sp(size_t stksize)
{
	klwp_t *lwp = ttolwp(curthread);

	lwptoregs(lwp)->r_sp = (uintptr_t)curproc->p_usrstack - stksize;
}

/*
 * Wrapper functions to maintain backwards compability
 */
int
xcopyin(const void *uaddr, void *kaddr, size_t count)
{
	return (xcopyin_nta(uaddr, kaddr, count, UIO_COPY_CACHED));
}

int
xcopyout(const void *kaddr, void *uaddr, size_t count)
{
	return (xcopyout_nta(kaddr, uaddr, count, UIO_COPY_CACHED));
}
