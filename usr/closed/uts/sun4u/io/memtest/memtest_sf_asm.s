/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * This file contains support for US-I and US-II family of processors.
 * This includes Spitfire (US-I), Blackbird (US-IIs), Sabre (US-IIi),
 * and Hummingbird (US-IIe).
 */

#include <sys/memtest_u_asm.h>

/*
 * This routine injects an ecache tag parity error at a physical address.
 * This routine assumes that kernel preemption has been disabled.
 *
 * Register usage:
 *
 *	%o0 = input argument : paddr
 *	%o1 = input argument : ecache_size ... changed to way mask
 *	%o2 = E$ set size
 *	%o3 = input argument : xor pattern
 *	%o4 = temp
 *	%o5 = saved pstate
 *	%l1 = EC_tag mask (0xFFFF)
 *	%l2 = EC_tag from PA <31:16>
 *	%l3 = set size mask ... changed to diag. address reg.
 *	%l4 = State field mask (0x30000)
 *	%l5 = EC_tag from E$ tag/state/rand field register.
 *	%l6 = E$ tag/state data.
 */
#if defined(lint)

/*ARGSUSED*/
int
hb_wr_ecache_tag(uint64_t paddr, uint_t ecache_size, uint64_t xorpat)
{
	return (0);
}

#else	/* lint */

	/*
	 * This code must not cross a page boundary to avoid unwanted
	 * page faults.
	 */
	.align 128

	ENTRY(hb_wr_ecache_tag)
	save    %sp, -SA(MINFRAME), %sp         ! Get a new window

	mov	%i0, %o0		! Put PA into %o0
	mov	%i1, %o1		! Put E$ size into %o1
	mov	%i2, %o3		! xor pattern to corrupt ec_parity[0]

	srlx	%o1, HB_EC_SET_SIZE_SHIFT, %o2	 ! %o2 contains ec_set_size
	sub	%o1, %o2, %o1		! EC way mask

	set	HB_EC_TAG_MASK, %l1	! EC_TAG needed later
	srlx	%o0, HB_ECTAG_SHIFT, %l2
	and	%l1, %l2, %l2		! %l2 contains PA <31:16> EC_tag

	sub	%o2, 0x1, %l3		! EC set size mask
	and	%o0, %l3, %l3		! starting E$ index for set 0

	set	0x1, %o4		! and bits 40/39 = 10
	sllx	%o4, 40, %o4		! .
	or	%l3, %o4, %l3		! .

	set	0x3, %l4
	sllx	%l4, HB_ECSTATE_SHIFT, %l4	! EC state mask <17:16>

	rdpr	%pstate, %o5		! save pstate
	andn	%o5, PSTATE_AM|PSTATE_IE, %o4	! and disable interrupts
	wrpr	%o4, 0, %pstate		! .

	ba	1f
	nop

	.align 128
1:

	lduba	[%o0]ASI_MEM, %o4	! get the data into the cache

2:
	ldxa	[%l3]0x7E, %g0		! get the E$ tag data
	ldxa	[%g0]0x4E, %l6		!.

	and	%l6, %l1, %l5		! get EC_tag field
	cmp	%l5, %l2		! compare EC_tag
	bnz,pn	%xcc, 3f		! branch if tag does not match
	andcc	%l6, %l4, %g0		! check state bits
	bnz,pn	%xcc, 4f		! branch if valid
	nop

3:
	add	%l3, %o2, %l3		! next EC index
	andcc	%l3, %o1, %g0		! check way bits
	bnz,pt	%xcc, 2b
	nop

	ba	5f
	mov	1, %l5			! return error

4:
	xor	%l6, %o3, %l6		! corrupt the tag
	stxa	%l6, [%g0]0x4E		! write the new E$ tag back
	membar	#Sync			! .
	stxa	%g0, [%l3]0x76		! .
	membar	#Sync

	mov	%g0, %l5		! return success

5:
	wrpr	%o5, %pstate		! turn the interrupts back on
	ret
	restore	%g0, %l5, %o0
	SET_SIZE(hb_wr_ecache_tag)

#endif	/* lint */

/*
 * This routine updates pre-existing data in memory with the driver calculated
 * ECC that was passed in.  This is done by programming the UDBs to accept our
 * ECC bits then performing the write.  This routine will affect an entire
 * cache line and will change the state to modified.
 *
 * Register usage:
 *
 *      %i0 = paddr (temp)
 *      %i1 = ec_size (temp)
 *      %i2 = ecc
 *      %i3 = ecc on 32bit kernel (temp)
 *      %i5 = temp
 *
 *      %l0 = 64bit physical address
 *      %l1 = flush offset
 *      %l2 = displacement flush base address
 *      %l3 = UDB hi reg ASI offset
 *      %l4 = UDB lo reg ASI offset
 *      %l5 = saved pstate register
 *      %l6 = saved UPA config
 *
 *      %o0 = temp
 *      %o1 = temp
 */
#if defined(lint)

/*ARGSUSED*/
int
hb_wr_memory(uint64_t paddr, uint_t ec_size, uint_t ecc)
{
	return (0);
}

#else   /* lint */

	/*
	 * This code must not cross a page boundary to avoid unwanted
	 * page faults.
	 */
	.align  512

	ENTRY(hb_wr_memory)
	save	%sp, -SA(MINFRAME), %sp		! Get a new window

	mov	%i0, %l0			! put 64 bit physaddr in %l0
	andn	%l0, 0x7, %l0			! align pa to 8 byte boundary

	xor	%l0, %i1, %l1			! calculate alias address
	add	%i1, %i1, %l2			! 2 * ecachesize in case
						! addr == ecache_flushaddr
	sub	%l2, 1, %l2			! -1 == mask
	and	%l1, %l2, %l1			! and with xor'd address

	set	ecache_flushaddr, %l2		! displacement flush baseaddr
	ldx	[%l2], %l2

	mov	P_DCR_H, %l3			! UDB hi reg ASI offset
	mov	P_DCR_L, %l4			! UDB lo reg ASI offset
	ba	1f
	or	%i2, SDB_FMD, %i2		! add force mode bit to ecc

	/*
	 * The following instructions (up until error generation is disabled)
	 * should be on the same ecache line in order to prevent ecache misses
	 * and unwanted side effects.
	 */
	.align  64
1:
	rdpr	%pstate, %l5			! clear address mask bit,
	andn	%l5, PSTATE_AM|PSTATE_IE,  %o1	! disable interrupts,
	wrpr	%o1, %g0, %pstate		! and save current state

	call	cpu_flush_ecache		! flush the entire e$ after
	nop					! disabling interrupts

	! Place E$ in direct map mode for data access
	or	%g0, 1, %i5
	sllx	%i5, HB_UPA_DMAP_DATA_BIT, %i5
	ldxa	[%g0]ASI_UPA_CONFIG, %l6	! current UPA config
						! (restored later)
	or	%l6, %i5, %i5
	membar	#Sync
	stxa	%i5, [%g0]ASI_UPA_CONFIG	! enable direct map for
						! data access
	membar	#Sync

	lduba	[%l0]ASI_MEM, %o0		! get the value so we can
						! write it back with our ECC

	stxa	%i2, [%l3]ASI_SDB_INTR_W	! store our check bits
	stxa	%i2, [%l4]ASI_SDB_INTR_W	! store our check bits
	membar	#Sync

	stba	%o0, [%l0]ASI_MEM		! store the value with our ECC

	lduba	[%l1 + %l2]ASI_MEM, %g0		! displace this line
	membar	#Sync				! wait for store buf to clear

	stxa	%g0, [%l3]ASI_SDB_INTR_W	! clear UBH and UDBL regs to
	stxa	%g0, [%l4]ASI_SDB_INTR_W	! disable forcing errors
	membar	#Sync

	stxa	%l6, [%g0]ASI_UPA_CONFIG	! restore UPA config (DM bits)
	membar	#Sync

	call	cpu_flush_ecache		! flush the entire e$ after
	nop					! switching back to asso mode

	wrpr	%l5, %pstate			! restore AM and IE state

	ret					! return value 0
	restore	%g0, %g0, %o0
	SET_SIZE(hb_wr_memory)

#endif  /* lint */

/*
 * This routine injects an ecache parity error at a physical address.
 * This routine assumes that kernel preemption has been disabled.
 *
 * Register usage:
 *
 *	%o0 = input argument: paddr
 *	%o1 = data
 *	%o2 = saved LSU register
 *	%o3 = new LSU register
 *	%o4 = temp
 *	%o5 = saved pstate
 */
#if defined(lint)

/*ARGSUSED*/
void
sf_wr_ecache(uint64_t paddr)
{}

#else	/* lint */

	/*
	 * This code must not cross a page boundary to avoid unwanted
	 * page faults. Since this code is currently <= 16 instructions
	 * an align 64 is sufficient.
	 */
	.align 64

	ENTRY(sf_wr_ecache)
	rdpr	%pstate, %o5			! save pstate
	andn	%o5, PSTATE_IE | PSTATE_AM, %o4	! and disable interrupts
	wrpr	%o4, 0, %pstate			! .

	lduba	[%o0]ASI_MEM, %o1

	ldxa    [%g0]ASI_LSU, %o2	! load LSU
	set	0xffff0, %o3		! build the parity mask
	or	%o2, %o3, %o3		! ...
	stxa    %o3, [%g0]ASI_LSU	! tell the LSU about the mask
	membar	#Sync

	stuba	%o1, [%o0]ASI_MEM	! send the value out with bad parity
	membar	#Sync

	stxa    %o2, [%g0]ASI_LSU	! clear the parity mask
	membar	#Sync

	retl
	wrpr	%o5, %pstate		! turn the interrupts back on
	SET_SIZE(sf_wr_ecache)

#endif	/* lint */

/*
 * This routine injects an ecache tag parity error at a physical address.
 * This routine assumes that kernel preemption has been disabled.
 *
 * Register usage:
 *
 *      %o0 = input argument: paddr
 *      %o1 = xor pattern
 *      %o2 = e$ tag/state data
 *      %o3 = asi address
 *      %o4 = temp
 *      %o5 = saved pstate
 */
#if defined(lint)

/*ARGSUSED*/
void
sf_wr_ecache_tag(uint64_t paddr, uint64_t xorpat)
{}

#else   /* lint */

	/*
	 * This code must not cross a page boundary to avoid unwanted
	 * page faults.
	 */
	.align 128

	ENTRY(sf_wr_ecache_tag)
	set	0x3FFFC0, %o4		! e$ addr mask
	and	%o0, %o4, %o3		! form asi address from e$ addr
	set	0x1, %o4		! and bits 40/39 = 1
	sllx	%o4, 40, %o4		! .
	or	%o3, %o4, %o3		! .

	rdpr	%pstate, %o5		! save pstate
	andn	%o5, PSTATE_AM | PSTATE_IE, %o4 
					! disable interrupts & set AM bit
	wrpr	%o4, 0, %pstate		! .

	ba	1f
	nop

	.align 64
1:

	lduba	[%o0]ASI_MEM, %o4	! get the data into the cache

	ldxa	[%o3]0x7E, %g0		! get the e$ tag data
	ldxa	[%g0]0x4E, %o2		! .
	xor	%o2, %o1, %o2		! corrupt the tag
	stxa	%o2, [%g0]0x4E		! write the new e$ tag back
	stxa	%g0, [%o3]0x76		! .
	membar	#Sync

	retl
	wrpr	%o5, %pstate		! turn the interrupts back on
	SET_SIZE(sf_wr_ecache_tag)

#endif  /* lint */

/*
 * This routine updates pre-existing data in memory with the driver calculated
 * ECC that was passed in.  This is done by programming the UDBs to accept our
 * ECC bits then performing the write.  This routine will affect an entire
 * cache line and will change the state to modified.
 *
 * Register usage:
 *
 *	%i0 = paddr (temp)
 *	%i1 = ec_size (temp)
 *	%i2 = ecc
 *	%i3 = ecc on 32bit kernel (temp)
 *
 *	%l0 = 64bit physical address
 *	%l1 = flush offset
 *	%l2 = displacement flush base address
 *	%l3 = UDB hi reg ASI offset
 *	%l4 = UDB lo reg ASI offset
 *	%l5 = saved pstate register
 *
 *	%o0 = temp
 *	%o1 = temp
 */

#if defined(lint)

/*ARGSUSED*/
int
sf_wr_memory(uint64_t paddr, uint_t ec_size, uint_t ecc)
{
	return (0);
}

#else	/* lint */

	/*
	 * This code must not cross a page boundary to avoid unwanted
	 * page faults.
	 */
	.align	256

	ENTRY(sf_wr_memory)
	save    %sp, -SA(MINFRAME), %sp		! Get a new window
	mov	%i0, %l0			! put 64 bit physaddr in %l0
	andn	%l0, 0x7, %l0			! align pa to 8 byte boundary

	xor	%l0, %i1, %l1			! calculate alias address
	add	%i1, %i1, %l2			! 2 * ecachesize in case
						! addr == ecache_flushaddr
	sub	%l2, 1, %l2			! -1 == mask
	and	%l1, %l2, %l1			! and with xor'd address

	set	ecache_flushaddr, %l2		! displacement flush baseaddr
	ldx	[%l2], %l2

	mov	P_DCR_H, %l3			! UDB hi reg ASI offset
	mov	P_DCR_L, %l4			! UDB lo reg ASI offset
	ba	1f
	or	%i2, SDB_FMD, %i2		! add force mode bit to ecc

	/*
	 * The following instructions (up until error generation is disabled)
	 * should be on the same ecache line in order to prevent ecache misses
	 * and unwanted side effects.
	 */
	.align	64
1:
	rdpr	%pstate, %l5			! clear address mask bit,
	andn	%l5, PSTATE_IE | PSTATE_AM,  %o1! disable interrupts,
	wrpr	%o1, %g0, %pstate		! and save current state

	call	cpu_flush_ecache		! flush the entire e$ after
	nop					! disabling interrupts

	lduba	[%l0]ASI_MEM, %o0		! get the value so we can
						! write it back with our ECC

	stxa	%i2, [%l3]ASI_SDB_INTR_W	! store our check bits
	stxa	%i2, [%l4]ASI_SDB_INTR_W	! store our check bits
	membar	#Sync

	stba	%o0, [%l0]ASI_MEM		! store the value with our ECC
	lduba	[%l1 + %l2]ASI_MEM, %g0		! displace this line
	membar	#Sync				! wait for store buf to clear

	stxa	%g0, [%l3]ASI_SDB_INTR_W	! clear UBH and UDBL regs to
	stxa	%g0, [%l4]ASI_SDB_INTR_W	! disable forcing errors
	membar	#Sync

	wrpr	%l5, %pstate			! restore AM and IE state

	ret					! return value 0
	restore	%g0, %g0, %o0
	SET_SIZE(sf_wr_memory)

#endif	/* lint */

/*
 * This routine injects an ecache parity error at a specified offset
 * in the ecache using the diag ASI's. It does not modify the cacheline
 * state like sf_wr_ecache() does and is therefore useful for generating
 * totally random parity errors in the cache to more accurately simulate
 * a real error.
 *
 * In order for this routine to work correctly we must prevent ecache
 * misses triggered by instruction prefetch.  To achieve this we need
 * to set up the L1 predictors correctly.  That is, we need to build
 * determinism into the predictors. To do this we need to touch the
 * instructions that need to be executed and at least 12 instructions
 * (the instruction buffer depth) beyond them.
 *
 * Register usage:
 *
 *	%o0 = e$ offset and asi address
 *	%o1 = used to generate value written to LSU reg
 *	%o2 = temporary
 *	%o4 = used to save/restore LSU reg
 *	%o5 = used to save/restore pstate register
 */

#if defined(lint)

/*ARGSUSED*/
void
sf_wr_ephys(uint64_t offset)
{}

#else	/* lint */

	/*
	 * This code must not cross a page boundary in order to
	 * avoid page faults.
	 */
	.align	256

	ENTRY(sf_wr_ephys)
	rdpr	%pstate, %o5
	andn	%o5, PSTATE_IE | PSTATE_AM, %o4	! shut the interrupts off
	wrpr	%o4, 0, %pstate

	ldxa	[%g0]ASI_LSU, %o4		! get the LSU

	mov	0xf, %o2			! figure out the byte mask
	and	%o0, %o2, %o1			! .

	mov	1, %o2				! generate the byte mask ...
	sll	%o2, %o1, %o1			! shift left by byte position
	sll	%o1, 4, %o1			! shift for LSU parity mask
	or	%o4, %o1, %o1			! set the byte mask

	set	0xfffff8, %o2			! 16MB mask for e$ offset
	and	%o0, %o2, %o0			! align and mask e$ offset
	mov	1, %o2
	sllx	%o2, 39, %o2			! set bit 39 for E$ data access
	or	%o2, %o0, %o0			! %o0 now contains E$ asi addr

	ba,pt	%icc, 2f			! now the funkiness begins
	nop

	/*
	 * Align on an ecache boundary in order to force
	 * critical code section onto the same cache line.
	 */
	.align	64

1:
	stxa	%o1, [%g0]ASI_LSU		! setup the LSU
	membar	#Sync

	ldxa	[%o0]ASI_EC_R, %o2		! read ecache line
	stxa	%o2, [%o0]ASI_EC_W		! write ecache line
	membar	#Sync

	stxa	%o4, [%g0]ASI_LSU		! restore LSU
	membar	#Sync

	retl
	wrpr	%o5, %pstate			! restore interrupts

2:
	nop					! fill up the prefetch buffer
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	ba,pt	%icc, 1b
	nop
	SET_SIZE(sf_wr_ephys)

#endif	/* lint */

/*
 * This routine is similar to sf_wr_ephys() but injects the
 * error into the ecache tag rather than the ecache data.
 *
 * Register usage:
 *
 * Register usage:
 *
 *	%o0 = input argument: paddr
 *	%o1 = input argument: xor pattern
 *	%o2 = e$ tag/state data
 *	%o3 = asi address
 *	%o4 = temp
 *	%o5 = saved pstate
 *
 */

#if defined(lint)

/*ARGSUSED*/
void
sf_wr_etphys(uint64_t offset, uint64_t xor_pat)
{}

#else	/* lint */

	/*
	 * This code must not cross a page boundary in order to
	 * avoid page faults.
	 */
	.align	256

	ENTRY(sf_wr_etphys)
	set	0x3FFFC0, %o4			! e$ addr mask
	and	%o0, %o4, %o3			! form asi address from e$ addr
	set	0x1, %o4			! and bits 40/39 = 1
	sllx	%o4, 40, %o4			! .
	or	%o3, %o4, %o3			! .

	rdpr	%pstate, %o5
	andn	%o5, PSTATE_IE | PSTATE_AM, %o4	! shut the interrupts off
	wrpr	%o4, 0, %pstate

	ba	1f
	nop

	/*
	 * Align on an ecache boundary in order to force
	 * critical code section onto the same cache line.
	 */
	.align	64

1:
	ldxa	[%o3]0x7E, %g0			! get the e$ tag data
	ldxa	[%g0]0x4E, %o2			! .
	xor	%o2, %o1, %o2			! corrupt the tag
	stxa	%o2, [%g0]0x4E			! write the new e$ tag back
	stxa	%g0, [%o3]0x76			! .
	membar	#Sync

	retl
	wrpr	%o5, %pstate			! restore interrupts
	SET_SIZE(sf_wr_etphys)

#endif	/* lint */

/*
 * This routine invalidates a dcache entry.
 */

#if defined(lint)

/*ARGSUSED*/
int
sf_flush_dc(caddr_t addr)
{
	return (0);
}

#else	/* lint */

	.align	64

	ENTRY(sf_flush_dc)
	andn	%o0, 0x1F, %o0			! align addr to 32 bytes

	rdpr	%pstate, %o5			! save the processor state
	andn	%o5, PSTATE_IE | PSTATE_AM, %o4	! .
	wrpr	%o4, 0, %pstate			! shut the interrupts off

	stxa	%g0, [%o0]ASI_DC_TAG		! invalidate the dcache
	membar	#Sync
	ldxa	[%o0]ASI_DC_TAG, %g0		! need to do the load to make
						! the flush effective

	mov	%g0, %o0			! return value 0
	retl
	wrpr	%o5, %pstate			! turn the interrupts back on
	SET_SIZE(sf_flush_dc)

#endif	/* lint */

/*
 * This routine invalidates the entire dcache.
 */

#if defined(lint)

/*ARGSUSED*/
int
sf_flushall_dc(int cache_size, int line_size)
{
	return (0);
}

#else	/* lint */

	.align	32

	ENTRY(sf_flushall_dc)
	sub	%o0, %o1, %o2
1:
	stxa	%g0, [%o2]ASI_DC_TAG		! flush next cache line
	membar	#Sync				! .
	cmp	%g0, %o2			! loop until we're done
	bne,pt	%icc, 1b			! .
	sub	%o2, %o1, %o2			! .

	retl
	mov	%g0, %o0			! return value 0
	SET_SIZE(sf_flushall_dc)

#endif	/* lint */
