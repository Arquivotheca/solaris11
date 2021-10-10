/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/cheetahregs.h>
#include <sys/memtest_u_asm.h>

/*
 * This routine is used to either update pre-existing data in memory
 * at the specified physical address with the calculated ECC that is
 * passed in, or to cause a system bus error.
 *
 * Register usage:
 *	%i0 - virtual address
 *	%i1 - corruption pattern
 *	%i2 - bit to set in E$ error control reg
 *
 *	%l0 - saved pstate
 *	%l1 - saved %fprs
 *	%l2 - ptr to saved fpregs
 *	%l3 - saved E$ error reg
 *	%l4 - scratch
 *	%l5 - scratch
 */
#if defined(lint)

/* ARGSUSED */
int
jg_wr_memory(caddr_t vaddr, uint_t ecc, uint64_t ctrl_reg)
{ return (0); }

#else	/* lint */

	ENTRY(jg_wr_memory)
	save	%sp, -SA(MINFRAME + 64), %sp	! get a new window w/room for fpregs

	rdpr	%pstate, %l0			! save pstate value
	andn	%l0, PSTATE_IE, %l5		! disable interrupts
	wrpr	%l5, %g0, %pstate		! .

	rd	%fprs, %l1			! save current state of fprs
	andcc	%l1, FPRS_FEF, %l5		! are fp regs in use ?
	bz,a,pt	%icc, 0f			! not in use, don't save regs
	wr	%g0, FPRS_FEF, %fprs		! enable fp regs

	! save in-use fpregs on stack
	add	%fp, STACK_BIAS - 65, %l2
	and	%l2, -64, %l2
	stda	%d32, [%l2]ASI_BLK_P
	membar	#Sync
0:

	ldda	[%i0]ASI_BLK_P, %d32		! load data into fpregs
1:
	! setup bad ECC
	ldxa	[%g0]ASI_ESTATE_ERR, %l5	! read E$ err reg
	mov	%l5, %l3			! save orig value

	/*
	 * Irrespective of the type of error we are
	 * injecting we set up the FDECC field. If
	 * its not a system bus data error, then these
	 * bits will never be used anyway.
	 */
	set	EN_REG_FDECC, %l4		! clear FDECC field
	andn	%l5, %l4, %l5			! .
	sll	%i1, EN_REG_FDECC_SHIFT, %i1	! shift ecc into position
	or	%i1, %l5, %l5			! and set FDECC field
	or	%l5, %i2, %l5			! set appropriate force bit
						!  depending on the type of error

	ba,pt	%icc, 4f			! branch to aligned section
	nop
2:
	stxa	%l5, [%g0]ASI_ESTATE_ERR	! store new ecc pattern
	membar	#Sync				! 	and control bits

	! do the block store commit which also
	! invalidates all cache copies.
	stda	%d32, [%i0]ASI_BLK_COMMIT_P
	membar	#Sync

	stxa	%l3, [%g0]ASI_ESTATE_ERR	! restore orig E$ reg
	membar	#Sync

	btst	FPRS_FEF, %l1
	bz	3f
	nop

	! restore fpregs from stack
	membar	#Sync
	ldda	[%l2]ASI_BLK_P, %d32
3:
	wr	%l1, 0, %fprs			! restore %fprs

	wrpr	%l0, %pstate			! restore pstate value.

	membar	#Sync
	ret					! return value 0
	restore	%g0, %g0, %o0
4:
	nop					! fill prefetch queue
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	ba,pt	%icc, 2b
	nop

	SET_SIZE(jg_wr_memory)

#endif	/* lint */


/*
 * This routine is used to corrupt the ECC bits which
 * protect the ecache tag.
 *
 * Register usage:
 *	%i0 - aligned physical address
 *	%i1 - corruption pattern
 *	%i2 - address shift for diagnostic asi access
 *		(i.e. 21 for 4MB, 22 for 8MB)
 *
 *	%l0 - way bit for way 1
 *	%l1 - tag addr for way 0
 *	%l2 - tagECC addr for way 0
 *	%l3 - masked PA for tag comparison.
 *	%l4 - scratch
 *	%l5 - saved %pstate value.
 */
#if defined(lint)

/* ARGSUSED */
void
jg_wr_ecache_tag_ecc(uint64_t paddr_aligned, uint64_t xorpat, uint32_t ec_shift)
{}

#else	/* lint */

	.align 256
	ENTRY(jg_wr_ecache_tag_ecc)
	save	%sp, -SA(MINFRAME), %sp

	/*
	 * Using the supplied PA generate the appropriate address
	 * to access the E$ tag for way 0. i.e. we must clear :
	 * 	bits <63:21> for 4MB
	 * 	bits <63:22> for 8MB
	 */
	set	1, %l0			! create mask for clearing upper
	sllx	%l0, %i2, %l0		!   bits and ec_way bit
					! %l0 - way bit for way 1
	sub	%l0, 1, %l1
	and	%i0, %l1, %l1		! %l1 - tag addr for way 0

	set	0x1, %l4		! VA<23> == 1 to access tagECC
	sllx	%l4, 23, %l4		! .
	or	%l1, %l4, %l2		! %l2 - tagECC addr for way 0

	srlx	%i0, %i2, %l3		! %l3 - adjusted PA for later
					!       comparison to tag.

	ldxa	[%i0]ASI_MEM, %g0	! read data to process any faults.

	/*
	 * Disable interrupts.
	 */
	rdpr	%pstate, %l5		! %l5 - saved %pstate value.
	andn	%l5, PSTATE_IE, %l4
	ba	1f
	wrpr	%l4, 0, %pstate

	/*
	 * To keep side-effects to a minimum we want all the following
	 * instructions to fit on one cache line (i.e. 16 instrs).
	 */
	.align	64	
1:
	ldxa	[%i0]ASI_MEM, %g0	! load data into E$.

	
	/*
	 * Read tag for way 0 and mask off appropriate
	 * bits to generate compare value.
	 */
	ldxa	[%l1]ASI_EC_DIAG, %l4
	sllx	%l4, 22, %l4		! shift out LRU bit
	srlx	%l4, 22, %l4		! .
	srlx	%l4, %i2, %l4		! shift out lower bits

	/*
	 * Compare tag for way 0 to adjusted PA to determine
	 * if the required data is in that way. If not we
	 * set the bit for way 1.
	 */
	cmp	%l3, %l4
	bnz,a	%icc, 2f		
	or	%l0, %l2, %l2		! set bit for way 1

2:
	ldxa	[%l2]ASI_EC_DIAG, %l4	! read tagECC
	xor	%l4, %i1, %l4		! corrupt tagECC
	stxa	%l4, [%l2]ASI_EC_DIAG	! write back tagECC
	membar	#Sync

	/*
	 * Restore interrupts.
	 */
	wrpr	%l5, 0, %pstate

	ret
	restore
	SET_SIZE(jg_wr_ecache_tag_ecc)

#endif	/* lint */

/*
 * This routine is used to corrupt the data bits of the Ecache tag.
 *
 * Register usage:
 *	%i0 - aligned physical address
 *	%i1 - corruption pattern
 *	%i2 - address shift for diagnostic asi access
 *		(i.e. 21 for 4MB, 22 for 8MB)
 *
 *	%l0 - way bit for way 1
 *	%l1 - tag addr for way 0
 *	%l2 - tagECC addr for way 0
 *	%l3 - masked PA for tag comparison.
 *	%l4 - scratch
 *	%l5 - saved %pstate value.
 *
 *	%o2 - shift value to remove unwanted
 *		lower bits in E$ tag
 *
 * All stores to the e$ tag (including those done using diagnostic asi) will
 * result in the correct tag ECC bits also being generated and stored. So to
 * create a corrupt tag, we first read the tag and its ECC, corrupt the tag
 * and store it back (which will generate correct matching ECC bits) and
 * then overwrite those newly created ECC bits with the original (and now
 * non-matching) ECC bits.
 */
#if defined(lint)

/* ARGSUSED */
void
jg_wr_ecache_tag_data(uint64_t paddr_aligned, uint64_t xorpat,
		uint32_t ec_shift)
{}

#else	/* lint */

	.align 256
	ENTRY(jg_wr_ecache_tag_data)
	save	%sp, -SA(MINFRAME), %sp
	/*
	 * 63:21 for 4MB
	 * 63:22 for 8MB
	 */
	set	1, %l0			! create mask for clearing upper
	sllx	%l0, %i2, %l0		!   bits and ec_way bit
					! %l0 - way bit for way 1
	sub	%l0, 1, %l1
	and	%i0, %l1, %l1		! %l1 - tag addr for way 0

	set	0x1, %l4		! VA<23> == 1 to access tagECC
	sllx	%l4, 23, %l4		! .
	or	%l1, %l4, %l2		! %l2 - tagECC addr for way 0

	srlx	%i0, %i2, %l3		! %l3 - adjusted PA for later
					!       comparison to tag.

	add	%i2, 22, %o2		! %o2 - shift used later to adjust
					! tag value for comparison.

	ldxa	[%i0]ASI_MEM, %g0	! read data to process any faults.

	/*
	 * Disable interrupts.
	 */
	rdpr	%pstate, %l5		! %l5 - saved %pstate value.
	andn	%l5, PSTATE_IE, %l4
	ba	1f
	wrpr	%l4, 0, %pstate

	/*
	 * Want 16 or less instructions from where we do the load
	 * to the final membar #Sync to try and minimise side-effects.
	 */
	.align	64	
1:
	ldxa	[%i0]ASI_MEM, %g0	! load data into E$.

	
	/*
	 * Read tag for way 0 and mask off appropriate
	 * bits to generate compare value.
	 */
	ldxa	[%l1]ASI_EC_DIAG, %l4
	sllx	%l4, 22, %l4		! shift out LRU bit
	srlx	%l4, %o2, %l4		! shift out lower bits

	/*
	 * Compare tag for way 0 to adjusted PA to determine
	 * if the required data is in that way. If not we
	 * set the bit for way 1.
	 */
	cmp	%l3, %l4		! (%l3, %l4 free for re-use)
	bz	%icc, 2f		
	nop

	or	%l1, %l0, %l1		! set bit for way 1
	or	%l2, %l0, %l2		! .

2:
	ldxa	[%l1]ASI_EC_DIAG, %l3	! read the tag
	ldxa	[%l2]ASI_EC_DIAG, %l4	! read tagECC
	xor	%l3, %i1, %l3		! corrupt tag
	stxa	%l3, [%l1]ASI_EC_DIAG	! write back corrupted tag
	membar	#Sync
	stxa	%l4, [%l2]ASI_EC_DIAG	! write back original tagECC
	membar	#Sync

	/*
	 * Restore interrupts.
	 */
	wrpr	%l5, 0, %pstate

	ret
	restore
	SET_SIZE(jg_wr_ecache_tag_data)

#endif	/* lint */

/*
 * This routine reads 32 bytes of data plus the corresponding ECC check
 * bits from the E$, corrupts data or check-bits and then writes the data
 * back to the E$ using diagnostic ASI access.
 *
 * Since this routine references the data before corrupting it, this
 * guarantees that the cache line can not be in the invalid state.
 *
 * Register usage:
 *	%i0 - aligned physical address
 *	%i1 - corruption pattern
 *	%i2 - address shift for diagnostic asi access
 *		(i.e. 21 for 4MB, 22 for 8MB)
 *	%i3 - staging register selection
 *
 *	%l0 - way bit for way 1
 *	%l1 - tag addr for way 0
 *	%l2 - adjusted PA for later comparison to tag
 *	%l3 - scratch
 *	%l4 - scratch
 *	%l5 - saved pstate
 *
 *	%o0 - return value
 * 	%o2 - shift value to adjust tag contents for comparison.
 *	%i3 - staging reg selection
 *	%i4 - loop counter
 *	%i5 - test success (pass/fail)
 *
 * Returns 0 on success, or 0xfeccf on failure.
 */
#if defined(lint)

/* ARGSUSED */
int
jg_wr_ecache(uint64_t paddr_aligned, uint64_t xorpat, uint32_t ec_shift, int reg_select)
{ return (0); }

#else	/* lint */

	ENTRY(jg_wr_ecache)
	save	%sp, -SA(MINFRAME), %sp

	set	0x1, %l4		! generate mask for clearing upper bits
	sllx	%l4, %i2, %l0		! 	and ec_way field.
	sub	%l0, 0x1, %l4		! .
	and	%i0, %l4, %l1		! %l1 - tag addr for way 0

	srlx	%i0, %i2, %l2		! %l2 - adjusted PA for later comparison
					!	to tag.

	sll	%i3, 3, %i3		! generate asi vaddr from offset

	add	%i2, 22, %o2		! shift value to later adjust tag

	ldxa	[%i0]ASI_MEM, %g0	! read data to process any faults.

	rdpr	%pstate, %l5		! disable interrupts
	andn	%l5, PSTATE_IE, %l4	! .
	wrpr	%l4, 0, %pstate		! .

	set	2, %i4			! initialise loop counter
	set	0xfeccf, %l3		!  set error return value.
	ba	1f			! branch to aligned section
	mov	%g0, %i5		! return value = 0 (no error)

	.align	64
1:
	ldxa	[%i0]ASI_MEM, %g0	! load data into cache.

	/*
	 * Determine into which way the data landed.
	 */
2:
	ldxa	[%l1]ASI_EC_DIAG, %l4	! read tag for way 0
	sllx	%l4, 22, %l4		! shift out upper LRU bit
	srlx	%l4, %o2, %l4		! shift out lower bits

	cmp	%l4, %l2		! is data in this way ?
	bz	%icc, 3f		! .
	subcc	%i4, 1, %i4		! decrement loop counter 
	bz,a	4f			! if not found in any way then
	mov	%l3, %i5		!  set error return value.
	ba	2b
	add	%l1, %l0, %l1		! format address for way 1 tag access

	/*
	 * Found data, so now corrupt it.
	 */
3:
	ldxa	[%l1]ASI_EC_R, %g0	! read data into staging regs from e$
	ldxa	[%i3]ASI_EC_DATA, %l4	! read data from appropriate staging reg.
	xor	%l4, %i1, %l4
	stxa	%l4, [%g0 + %i3]ASI_EC_DATA
	stxa	%g0, [%l1]ASI_EC_W	! store data from staging regs back to e$.
	membar	#Sync
4:
	wrpr	%l5, %pstate		! restore saved pstate.
	ret
	restore	%g0, %i5, %o0		! return success or failure
	SET_SIZE(jg_wr_ecache) 

#endif	/* lint */
 
/*
 * This routine reads the shared jaguar ecache control register.
 */
#if defined(lint)

/*ARGSUSED*/
uint64_t
jg_get_secr(void)
{
	return (0);
}

#else	/* lint */

	ENTRY(jg_get_secr)
	retl
	ldxa	[%g0]ASI_EC_CFG_TIMING, %o0
	SET_SIZE(jg_get_secr)

#endif	/* lint */
