/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * This file contains support for the SPARC64-VI Olympus-C processor.
 */

#include <sys/mmu.h>
#include <sys/pte.h>
#include <sys/memtest_asm.h>
#include <sys/opl_olympus_regs.h>
#include <sys/machparam.h>

/*
 * suspend macro
 */
#define	SUSP_30		0x2
#define	SUSP_19		0x36
#define	SUSP_5		0x082

#define	SUSPEND		\
	.section ".text"	;\
	.align 4		;\
	.word	((SUSP_30<<30) | (SUSP_19<<19) | (SUSP_5<<5))

/*
 * This routine suspends the strand executing it.
 */

#if defined(lint)

/*ARGSUSED*/
void
oc_susp(void)
{}

#else	/* lint */

	ENTRY(oc_susp)
	retl
	SUSPEND
	SET_SIZE(oc_susp)

#endif	/* lint */

#if defined(lint)

/*ARGSUSED*/
void
oc_sys_trap(uint64_t i1, uint64_t i2)
{}

#else	/* lint */

	ENTRY(oc_sys_trap)
	mov	-1, %g4			! run at current pil
	set	sys_trap, %g5
	jmp	%g5			! goto sys_trap
	 nop
	SET_SIZE(oc_sys_trap)

#endif	/* lint */

/* ASI_ERR_INJCT: Olympus-C Error Injection Register (ASI 0x76, VA 0x0)
 *
 * +---------------------+------+-------+------+
 * |   reserved[63:3]    |  SX  |  L1D  |  GR  |
 * +---------------------+------+-------+------+
 *                           2      1       0
 */

#define	ASI_ERR_INJCT		 0x76
#define	ASI_ERR_INJCT_GR	 INT64_C(0x0000000000000001)
#define	ASI_ERR_INJCT_L1D	 INT64_C(0x0000000000000002)
#define	ASI_ERR_INJCT_SX	 INT64_C(0x0000000000000004)

#if defined(lint)

/*ARGSUSED*/
void
oc_inj_err_rn(void)
{}

#else /* lint */

	ENTRY(oc_inj_err_rn)
	mov	ASI_ERR_INJCT_GR, %o1
	stxa	%o1, [%g0]ASI_ERR_INJCT
	membar	#Sync
	mov	0x1, %g1
	flush	%o0
	retl
	nop
	SET_SIZE(oc_inj_err_rn)

#endif /* lint */

#if defined(lint)

/*ARGSUSED*/
void
oc_inv_err_rn(void)
{}

#else /* lint */

	ENTRY(oc_inv_err_rn)
	std	%g0, [%sp]
	retl
	nop
	SET_SIZE(oc_inv_err_rn)

#endif /* lint */

#if defined(lint)

/*ARGSUSED*/
void
oc_set_err_injct_l1d(void)
{}

#else /* lint */

	ENTRY(oc_set_err_injct_l1d)
	mov	ASI_ERR_INJCT_L1D, %o1
	stxa	%o1, [%g0]ASI_ERR_INJCT
	retl
	nop
	SET_SIZE(oc_set_err_injct_l1d)

#endif /* lint */

#if defined(lint)

/*ARGSUSED*/
void
oc_set_err_injct_sx(void)
{}

#else /* lint */

	ENTRY(oc_set_err_injct_sx)
	mov	ASI_ERR_INJCT_SX, %o1
	stxa	%o1, [%g0]ASI_ERR_INJCT
	retl
	nop
	SET_SIZE(oc_set_err_injct_sx)

#endif /* lint */

/*
 * This routine invokes L1D$/L2$ UEs at TL1.
 *
 * Register usage:
 *
 *	%g1 = input argument: vaddr
 *	%g2 = input argument: L1D$/L2$ size
 *	%g3 = temp: delta
 */
#if defined(lint)

/*ARGSUSED*/
void
oc_inv_l12uetl1(caddr_t va, uint_t cache_size)
{}

#else   /* lint */

        .align  32

	ENTRY(oc_inv_l12uetl1)
	brz	%g2, 2f
	mov	0x8, %g3
1:
	ldx	[%g1], %g0			! access va by a load
	add	%g1, %g3, %g1                   ! .
	subcc	%g2, %g3, %g2
	bne,pt	%icc, 1b                        ! .
	nop
2:
	retl
	nop					! return
	SET_SIZE(oc_inv_l12uetl1)

#endif  /* lint */

/*
 * Insert a dtlb entry.
 */
#if defined(lint)

/* ARGSUSED */
void
oc_dtlb_ld(caddr_t vaddr, int ctxnum, tte_t *tte)
{}

#else   /* lint */
	ENTRY_NP(oc_dtlb_ld)
	rdpr	%pstate, %o3
	wrpr	%o3, PSTATE_IE, %pstate		! disable interrupts
	srln	%o0, MMU_PAGESHIFT, %o0
	slln	%o0, MMU_PAGESHIFT, %o0		! clear page offset
	or	%o0, %o1, %o0			! or in ctx to form tagacc
	ldx	[%o2], %g1
	set	MMU_TAG_ACCESS, %o5
	membar	#Sync

	andcc	%g1, TTE_LCK_INT, %g0		! Locked entries require
	bnz,pn	%icc, 2f			! special handling
	  sethi	%hi(dtlb_resv_ttenum), %g3
	stxa	%o0,[%o5]ASI_DMMU		! Load unlocked TTE
	stxa	%g1,[%g0]ASI_DTLB_IN		! via DTLB_IN
	membar	#Sync
	retl
	  wrpr	%g0, %o3, %pstate		! enable interrupts
2:
	ld	[%g3 + %lo(dtlb_resv_ttenum)], %g3
	sll	%g3, 3, %g3			! First reserved idx in TLB 0
	sub	%g3, (1 << 3), %g3		! Decrement idx
3:
	ldxa	[%g3]ASI_DTLB_ACCESS, %g4	! Load TTE from TLB 0
	!
	! If this entry isn't valid, we'll choose to displace it (regardless
	! of the lock bit).
	!
	brgez,pn %g4, 4f			! TTE is > 0 iff not valid
	  nop
	andcc	%g4, TTE_LCK_INT, %g0		! Check for lock bit
	bz,pn	%icc, 4f			! If unlocked, go displace
	  nop
	sub	%g3, (1 << 3), %g3		! Decrement idx
	brgez	%g3, 3b			
	  nop
	sethi	%hi(sfmmu_panic5), %o0		! We searched all entries and
	call	panic				! found no unlocked TTE so
	  or	%o0, %lo(sfmmu_panic5), %o0	! give up.
4:
	stxa	%o0,[%o5]ASI_DMMU		! Setup tag access
	stxa	%g1,[%g3]ASI_DTLB_ACCESS	! Displace entry at idx
	membar	#Sync
	retl
	  wrpr	%g0, %o3, %pstate		! enable interrupts
	SET_SIZE(oc_dtlb_ld)

#endif  /* lint */

#if defined(lint)

/*
 * Insert an itlb entry.
 */
/* ARGSUSED */
void
oc_itlb_ld(caddr_t vaddr, int ctxnum, tte_t *tte)
{}

#else   /* lint */
	ENTRY_NP(oc_itlb_ld)
	rdpr	%pstate, %o3
	wrpr	%o3, PSTATE_IE, %pstate		! disable interrupts
	srln	%o0, MMU_PAGESHIFT, %o0
	slln	%o0, MMU_PAGESHIFT, %o0		! clear page offset
	or	%o0, %o1, %o0			! or in ctx to form tagacc
	ldx	[%o2], %g1
	set	MMU_TAG_ACCESS, %o5
	membar	#Sync

	andcc	%g1, TTE_LCK_INT, %g0		! Locked entries require
	bnz,pn	%icc, 2f			! special handling
	  sethi	%hi(dtlb_resv_ttenum), %g3
	stxa	%o0,[%o5]ASI_IMMU		! Load unlocked TTE
	stxa	%g1,[%g0]ASI_ITLB_IN		! via ITLB_IN
	membar	#Sync
	retl
	  wrpr	%g0, %o3, %pstate		! enable interrupts
2:
	ld	[%g3 + %lo(dtlb_resv_ttenum)], %g3
	sll	%g3, 3, %g3			! First reserved idx in TLB 0
	sub	%g3, (1 << 3), %g3		! Decrement idx
3:
	ldxa	[%g3]ASI_ITLB_ACCESS, %g4	! Load TTE from TLB 0
	!
	! If this entry isn't valid, we'll choose to displace it (regardless
	! of the lock bit).
	!
	brgez,pn %g4, 4f			! TTE is > 0 iff not valid
	  nop
	andcc	%g4, TTE_LCK_INT, %g0		! Check for lock bit
	bz,pn	%icc, 4f			! If unlocked, go displace
	  nop
	sub	%g3, (1 << 3), %g3		! Decrement idx
	brgez	%g3, 3b			
	  nop
	sethi	%hi(sfmmu_panic5), %o0		! We searched all entries and
	call	panic				! found no unlocked TTE so
	  or	%o0, %lo(sfmmu_panic5), %o0	! give up.
4:
	stxa	%o0,[%o5]ASI_IMMU		! Setup tag access
	stxa	%g1,[%g3]ASI_ITLB_ACCESS	! Displace entry at idx
	membar	#Sync
	retl
	  wrpr	%g0, %o3, %pstate		! enable interrupts
	SET_SIZE(oc_itlb_ld)

#endif  /* lint */

#if defined(lint)

/* ARGSUSED */
int
oc_get_sec_ctx()
{
	return (0);
}

#else   /* lint */
	ENTRY_NP(oc_get_sec_ctx)
	set	MMU_SCONTEXT, %o1
	ldxa	[%o1]ASI_MMU_CTX, %o0
	sllx	%o0, TAGACC_CTX_LSHIFT, %o0	! get the ctx
	srlx	%o0, TAGACC_CTX_LSHIFT, %o0
	retl
	 nop
	SET_SIZE(oc_get_sec_ctx)

#endif  /* lint */

/*
 * This routine loads the specified number of bytes from
 * the specified buffer.
 *
 * Register usage:
 *
 *	%o0 = input argument: buf addr
 *	%o1 = input argument: buf size in bytes
 */
#if defined(lint)

/* ARGSUSED */
void
oc_load(caddr_t i1, uint_t i2)
{}

#else   /* lint */
        .align  32

	ENTRY_NP(oc_load)
	brz	%o1, 2f
	mov	0x8, %l1
1:
	ldx	[%o0], %g0			! access va by a load
	add	%o0, %l1, %o0
	subcc	%o1, %l1, %o1
	bg,pt	%icc, 1b
	 nop
	membar	#Sync
2:
	retl
	 nop					! return
	SET_SIZE(oc_load)

#endif  /* lint */
