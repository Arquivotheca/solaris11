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
 * Copyright (c) 1994, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copyright (c) 2009, 2010 Intel Corporation.
 * All rights reserved.
 */

#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/klwp.h>
#include <sys/ucontext.h>
#include <sys/procfs.h>
#include <sys/privregs.h>
#include <sys/cpuvar.h>
#include <sys/cmn_err.h>
#include <sys/systm.h>
#include <sys/archsystm.h>
#include <sys/machsystm.h>
#include <sys/fp.h>
#include <sys/x86_archext.h>
#include <sys/sysmacros.h>

#define	YMMSIZE_32BIT		128
#define	YMMSIZE_64BIT		256

/*
 * Association of extra register state with a struct ucontext is
 * done by placing an xrs_t within the ucontext filler area.
 *
 * The following routines provide an interface for this association.
 */

/*
 * clear the struct ucontext extra register state pointer
 */
/* ARGSUSED */
void
xregs_clrptr(klwp_t *lwp, ucontext_t *uc)
{
	uc->uc_flags &= ~UC_XREGS;
	uc->uc_xrs.xrs_id = 0;
	uc->uc_xrs.xrs_ptr = NULL;
}

/*
 * indicate whether or not an extra register state
 * pointer is associated with a struct ucontext
 */
/* ARGSUSED */
int
xregs_hasptr(klwp_t *lwp, ucontext_t *uc)
{
	return ((uc->uc_flags & UC_XREGS) && uc->uc_xrs.xrs_id == XRS_ID);
}

/*
 * get the struct ucontext extra register state pointer field
 */
/* ARGSUSED */
caddr_t
xregs_getptr(klwp_t *lwp, ucontext_t *uc)
{
	if (xregs_hasptr(lwp, uc)) {
		return (uc->uc_xrs.xrs_ptr);
	} else {
		return (NULL);
	}
}

/*
 * set the struct ucontext extra register state pointer field
 */
/* ARGSUSED */
void
xregs_setptr(klwp_t *lwp, ucontext_t *uc, caddr_t xrp)
{
	uc->uc_flags |= UC_XREGS;
	uc->uc_xrs.xrs_id = XRS_ID;
	uc->uc_xrs.xrs_ptr = xrp;
}

#ifdef _SYSCALL32_IMPL

/* ARGSUSED */
void
xregs_clrptr32(klwp_t *lwp, ucontext32_t *uc)
{
	uc->uc_flags &= ~UC_XREGS;
	uc->uc_xrs.xrs_id = 0;
	uc->uc_xrs.xrs_ptr = NULL;
}

/* ARGSUSED */
int
xregs_hasptr32(klwp_t *lwp, ucontext32_t *uc)
{
	return ((uc->uc_flags & UC_XREGS) && uc->uc_xrs.xrs_id == XRS_ID);
}

/* ARGSUSED */
caddr32_t
xregs_getptr32(klwp_t *lwp, ucontext32_t *uc)
{
	if (xregs_hasptr32(lwp, uc)) {
		return (uc->uc_xrs.xrs_ptr);
	} else {
		return (NULL);
	}
}

/* ARGSUSED */
void
xregs_setptr32(klwp_t *lwp, ucontext32_t *uc, caddr32_t xrp)
{
	uc->uc_flags |= UC_XREGS;
	uc->uc_xrs.xrs_id = XRS_ID;
	uc->uc_xrs.xrs_ptr = xrp;
}

#endif /* _SYSCALL32_IMPL */

/*
 * Extra register state manipulation routines.
 * NOTE:  'lwp' might not correspond to 'curthread' in any of the
 * functions below since they are called from code in /proc to get
 * or set the extra registers of another lwp.
 */

int xregs_exists = 0;

extern const struct fnsave_state x87_initial;
extern const struct fxsave_state sse_initial;
extern const struct xsave_state avx_initial;

/*
 * Clear fields in prxregset_t where pr_xstate_bv says it is in "init" state.
 * Because XSAVEOPT does not write out the state when a component is in INIT
 * state (see Section 3.2.6 of Intel AVX Programming Reference), the OS need
 * to clear corresponding area when the state is passed to user space.
 */
static void
xregs_sync_with_hdr(struct pr_xsave *xsave)
{
	/* Everything before XMM is part of legacy FPU state */
	const size_t sz_legacy_fp = offsetof(struct fxsave_state, fx_xmm[0]);

	uint64_t bv = xsave->pr_xstate_bv;

	/*
	 * Get bits that are present in xsave_bv_all, but not in pr_xstate_bv.
	 * These bits represent areas that are in INIT state.
	 */
	bv = bv & xsave_bv_all;
	bv = bv ^ xsave_bv_all;

	if (bv & XFEATURE_LEGACY_FP) {
		/*
		 * Instead of zero everything, we copy from FPU init state
		 * because the initial state of legacy FPU is not all zero.
		 */
		bcopy(&avx_initial, &xsave, sz_legacy_fp);
	}

	if (bv & XFEATURE_SSE) {
		bzero(&xsave->pr_xmm, sizeof (xsave->pr_xmm));
	}

	if (bv & XFEATURE_AVX) {
		bzero(&xsave->pr_ymm, sizeof (xsave->pr_ymm));
	}

	xsave->pr_xstate_bv = xsave_bv_all;
}

/*
 * fill in the extra register state area specified with the
 * specified lwp's floating-point extra register state information
 */
void
xregs_getfpregs(klwp_t *lwp, caddr_t xrp)
{
	prxregset_t *xregs = (prxregset_t *)xrp;
	struct fpu_ctx *fpu = &lwp->lwp_pcb.pcb_fpu;
	size_t copy_size;

	if (xregs == NULL) {
		return;
	}

	if (!xregs_exists) {
		return;
	}

	ASSERT(fp_save_mech == FP_XSAVE);

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

	xregs->pr_type = XR_TYPE_XSAVE;
	xregs->pr_xsize = sizeof (xregs->pr_un.pr_xsave);
	xregs->pr_align = xregs->pr_pad = 0;
	/*
	 * TODO: For now we can assume xregs and xsave have the same size.
	 * When we expand xsave state in the future, we may want to determine
	 * how much we need to copy to user space.
	 */
	copy_size = sizeof (struct xsave_state);

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
		bcopy(&fpu->fpu_regs.kfpu_u.kfpu_xs,
		    &xregs->pr_un.pr_xsave, copy_size);
	} else {
		bcopy(&avx_initial, &xregs->pr_un.pr_xsave, copy_size);
	}

	/*
	 * If the xsave header indicates some part are in the "init" state,
	 * clear that part to prevent user space from getting garbage.
	 *
	 * NOTE: At this point pr_xsave.pr_xstate_bv has been copied from
	 * fpu->fpu_regs.kfpu_u.kfpu_xs or avx_initial.
	 */
	if (xregs->pr_un.pr_xsave.pr_xstate_bv != xsave_bv_all) {
		xregs_sync_with_hdr(
		    (struct pr_xsave *)&xregs->pr_un.pr_xsave);
	}

	/*
	 * Store XCR0 info. Must be placed after xregs_sync_with_hdr() because
	 * that function may clear SW available bytes.
	 */
	bzero(&xregs->pr_un.pr_xsave.pr_sw_avail,
	    sizeof (xregs->pr_un.pr_xsave.pr_sw_avail));
	xregs->pr_un.pr_xsave.pr_sw_avail.pr_xsave_info.pr_xcr0 = xsave_bv_all;

	kpreempt_enable();
}

/*
 * fill in the extra register state area specified with
 * the specified lwp's extra register state information
 */
void
xregs_get(klwp_t *lwp, caddr_t xrp)
{
	if (!xregs_exists) {
		return;
	}

	if (xrp != NULL) {
		bzero(xrp, sizeof (prxregset_t));
		xregs_getfpregs(lwp, xrp);
	}
}

/*
 * set the specified lwp's floating-point extra
 * register state based on the specified input
 */
void
xregs_setfpregs(klwp_t *lwp, caddr_t xrp)
{
	prxregset_t *xregs = (prxregset_t *)xrp;
	struct fpu_ctx *fpu = &lwp->lwp_pcb.pcb_fpu;
	size_t copy_size;
	size_t size_sw = sizeof (xregs->pr_un.pr_xsave.pr_sw_avail);
	off_t off_sw = offsetof(struct pr_xsave, pr_sw_avail);

	if (xregs == NULL) {
		return;
	}

	if (!xregs_exists) {
		return;
	}

	ASSERT(fp_save_mech == FP_XSAVE);

	if (xregs->pr_type != XR_TYPE_XSAVE) {
		cmn_err(CE_WARN,
		    "xregs_setfpregs: pr_type is %d and should be %d",
		    xregs->pr_type, XR_TYPE_XSAVE);
		return;
	}
	if (xregs->pr_xsize != sizeof (xregs->pr_un.pr_xsave)) {
		cmn_err(CE_WARN,
		    "xregs_setfpregs: pr_xregs_size is %d and should be %d",
		    xregs->pr_xsize, (int)sizeof (xregs->pr_un.pr_xsave));
		return;
	}
	/*
	 * TODO: For now we can assume xregs and xsave have the same size.
	 * When we expand xsave state in the future, we may want to determine
	 * how much we need to copy to user space.
	 */
	copy_size = sizeof (struct xsave_state);

	kpreempt_disable();

	if (!(fpu->fpu_flags & FPU_VALID)) {
		if (fpu->fpu_flags & FPU_EN) {
			/*
			 * FPU context is still active, release the
			 * ownership.
			 */
			fp_free(fpu, 0);
		}
	}

	bcopy(&xregs->pr_un.pr_xsave, &fpu->fpu_regs.kfpu_u.kfpu_xs,
	    copy_size);
	/* Zero SW available bytes and must-be-zero bytes */
	bzero(((caddr_t)&fpu->fpu_regs.kfpu_u.kfpu_xs.xs_fxsave) + off_sw,
	    size_sw);
	bzero(&fpu->fpu_regs.kfpu_u.kfpu_xs.xs_rsv_mbz,
	    sizeof (fpu->fpu_regs.kfpu_u.kfpu_xs.xs_rsv_mbz));
	/* Make sure we don't have an invalid xstate_bv */
	fpu->fpu_regs.kfpu_u.kfpu_xs.xs_xstate_bv &= xsave_bv_all;

	fpu->fpu_flags |= FPU_VALID;

	kpreempt_enable();
}

/*
 * set the specified lwp's extra register
 * state based on the specified input
 */
void
xregs_set(klwp_t *lwp, caddr_t xrp)
{
	if (!xregs_exists) {
		return;
	}

	if (xrp != NULL) {
		xregs_setfpregs(lwp, xrp);
	}
}

/*
 * return the size of the extra register state
 */
/* ARGSUSED */
int
xregs_getsize(proc_t *p)
{
	if (!xregs_exists) {
		return (0);
	} else {
		return (sizeof (prxregset_t));
	}
}

/*
 * indicate whether or not a prxregset_t is valid
 */
int
xregs_isvalid(caddr_t xrp)
{
	prxregset_t *xregs = (prxregset_t *)xrp;

	return ((xregs != NULL) &&
	    (xregs->pr_type == XR_TYPE_XSAVE) &&
	    (xregs->pr_xsize >= sizeof (xregs->pr_un.pr_xsave)) &&
	    ((xregs->pr_un.pr_xsave.pr_xstate_bv & ~xsave_bv_all) == 0));
}
