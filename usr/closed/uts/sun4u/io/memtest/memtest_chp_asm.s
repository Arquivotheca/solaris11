/*
 * Copyright (c) 2001, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/memtest_u_asm.h>
#include <sys/cheetahregs.h>
#include <sys/machthread.h>

/*
 * This routine updates pre-existing data in memory at the specified
 * physical address with the calculated ECC that is passed in.
 *
 * Register usage:
 *
 *	%i0 - virtual address
 *	%i1 - corruption pattern
 *
 * 	%l0 - saved %pstate
 *	%l1 - saved %fprs
 *	%l2 - ptr to saved fpregs
 *	%l3 - saved E$ ctrl reg
 */
#if defined(lint)

/* ARGSUSED */
int
chp_wr_memory(caddr_t vaddr, uint_t ecc)
{
	return (0);
}

#else	/* lint */

	ENTRY(chp_wr_memory)
	save	%sp, -SA(MINFRAME + 64), %sp	! get a new window
						! with room for fpregs

	rdpr	%pstate, %l0			! save current state and
	andn	%l0, PSTATE_IE, %o1		!  disable interrupts
	wrpr	%o1, %g0, %pstate		! .

	rd	%fprs, %l1			! save current state of fprs
	andcc	%l1, FPRS_FEF, %o2		! is fp in use?
	bz,a,pt %icc, 0f			! not in use, don't save regs
	wr	%g0, FPRS_FEF, %fprs		! enable fprs

	! save in-use fpregs on stack
	add	%fp, STACK_BIAS - 65, %l2
	and	%l2, -64, %l2
	stda	%d32, [%l2]ASI_BLK_P
0:
	membar #Sync

	ldda	[%i0]ASI_BLK_P, %d32		! load data into fpregs
1:
	! now set up the bad ECC

	ldxa	[%g0]ASI_ESTATE_ERR, %l4	! read E$ err reg
	mov	%l4, %l3			! save orig value

	set	EN_REG_FDECC, %o4		! clear FDECC field
	andn	%l4, %o4, %l4			! .
	sll	%i1, EN_REG_FDECC_SHIFT, %i1	! shift ecc into position
	or	%i1, %l4, %l4			! and set FDECC field
	set	EN_REG_FMD, %o3			! set FMD bit
	or	%o3, %l4, %l4			! .
						! %l4 - new E$ reg value

	ba,pt	%icc, 4f			! branch to aligned section
	nop
2:
	stxa	%l4, [%g0]ASI_ESTATE_ERR	! store new ecc pattern
	membar  #Sync

	! do the block store commit which also
	! invalidates all cache copies.
	stda	%d32, [%i0]ASI_BLK_COMMIT_P
	membar  #Sync

	stxa	%l3, [%g0]ASI_ESTATE_ERR	! restore original E$ reg value
	membar  #Sync

	btst    FPRS_FEF, %l1
	bz	3f
	nop

	! restore fpregs from stack
	membar  #Sync
	ldda	[%l2]ASI_BLK_P, %d32

3:	wr	%l1, 0, %fprs			! restore %fprs

	wrpr	%l0, %pstate			! restore interrupts

	membar  #Sync
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

	SET_SIZE(chp_wr_memory)

#endif	/* lint */

/*
 * This routine updates pre-existing mtags in memory at the specified
 * physical address with the calculated Mtag ECC that is passed in.
 *
 * Register usage:
 *
 *	%i0 - virtual address
 *	%i1 - corruption pattern
 *
 * 	%l0 - saved %pstate
 *	%l1 - saved %fprs
 *	%l2 - ptr to saved fpregs
 *	%l3 - saved E$ ctrl reg
 */
#if defined(lint)

/* ARGSUSED */
int
chp_wr_mtag(caddr_t vaddr, uint_t ecc)
{
	return (0);
}

#else	/* lint */

	/*
	 * This code must not cross a page boundary in order to
	 * avoid unwanted page faults.
	 */
	.align	256

	ENTRY(chp_wr_mtag)
	save	%sp, -SA(MINFRAME + 64), %sp	! get a new window
						! with room for fpregs

	rdpr	%pstate, %l0			! save current state and
	andn	%l0, PSTATE_IE, %o1		!  disable interrupts
	wrpr	%o1, %g0, %pstate		! .

	rd	%fprs, %l1			! save current state of fprs
	andcc	%l1, FPRS_FEF, %o2		! is fp in use?
	bz,a,pt %icc, 0f			! not in use, don't save regs
	wr	%g0, FPRS_FEF, %fprs		! enable fprs

	! save in-use fpregs on stack
	add	%fp, STACK_BIAS - 65, %l2
	and	%l2, -64, %l2
	stda	%d32, [%l2]ASI_BLK_P

0:
	membar #Sync

	ldda	[%i0]ASI_BLK_P, %d32		! load data into fpregs

1:
	! now set up the bad ECC

	ldxa	[%g0]ASI_ESTATE_ERR, %l4	! read E$ err reg
	mov	%l4, %l3			! save orig value

	set	EN_REG_FMECC, %o4		! clear FMECC field
	andn	%l4, %o4, %l4			! .
	sll	%i1, EN_REG_FMECC_SHIFT, %i1	! shift ecc into position
	or	%i1, %l4, %l4			! and set FMECC field
	set	EN_REG_FMT, %o3			! set FMT bit
	or	%o3, %l4, %l4			! .
						! %l4 - new E$ reg value

	ba,pt	%icc, 4f			! branch to aligned section
	nop
2:
	stxa	%l4, [%g0]ASI_ESTATE_ERR	! store new ecc pattern
	membar  #Sync

	! do the block store commit which also
	! invalidates all cache copies.
	stda    %d32, [%i0]ASI_BLK_COMMIT_P
	membar  #Sync

	stxa	%l3, [%g0]ASI_ESTATE_ERR	! restore original E$ reg value
	membar  #Sync

	btst    FPRS_FEF, %l1
	bz	3f
	nop

	! restore fpregs from stack
	membar  #Sync
	ldda	[%l2]ASI_BLK_P, %d32

3:	wr	%l1, 0, %fprs			! restore %fprs

	wrpr	%l0, %pstate			! restore interrupts

	membar  #Sync
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

	SET_SIZE(chp_wr_mtag)

#endif	/* lint */

/*
 * This routine is used to corrupt the ECC bits which
 * protect the ecache tag.
 *
 * Register usage:
 *
 *	%o0 - aligned physical address
 *	%o1 - corruption pattern
 *	%o2 - address shift for diagnostic asi access
 *		(i.e. 19 for 1MB, 21 for 4MB, 22 for 8MB)
 *
 *	%o3 - modified address to read tag
 *	%o4 - tmp
 *	%o5 - tmp
 */
#if defined(lint)

/* ARGSUSED */
void
chp_wr_ecache_tag_ecc(uint64_t paddr_aligned, uint64_t xorpat,
			uint32_t ec_shift)
{}

#else	/* lint */

	.align	256
	ENTRY(chp_wr_ecache_tag_ecc)
	/*
	 * Generate e$ tag access address based on PA.
	 * i.e. we must clear bits :
	 *	63:19 for 1MB
	 *	63:21 for 4MB
	 *	63:22 for 8MB
	 */
	set	1, %o4			! generate mask for clearing upper bits
	sllx	%o4, %o2, %o4		! 	and ec_way
	sub	%o4, 0x1, %o5		! .
	and	%o0, %o5, %o3		! %o3 now has asi addr to read tag

	rdpr	%pstate, %o4		! disable interrupts
	andn	%o4, PSTATE_IE, %o4	! .
	wrpr	%o4, 0, %pstate		! .
	ba	1f
	add	%o2, 0x2, %o4		! %o4 has ec_shift + 2 to save a couple
					! of instrs below

	.align	64
1:
	ldxa	[%o0]ASI_MEM, %g0	! load data into e$

	/*
	 * Read the tag for way 0 and shift out unneeded bits
	 * to generate a compare value. According to PPG bits
	 * <63:44> will be zero.
	 */
	ldxa	[%o3]ASI_EC_DIAG, %o5	! read the e$ tag
	srlx	%o5, %o4, %o5		! shift out unneeded lower bits
					! (%o4 is now available for reuse)

	/*
	 * Generate compare value from PA and do comparison
	 * to determine which way contains data.
	 */
	srlx	%o0, %o2, %o4		! shift out unneeded lower bits of PA
	cmp	%o4, %o5		! compare tag to PA
	bz	%icc, 2f		! data is in way 0
	nop				! (%o5 is now available for reuse)

	/*
	 * Data is in way 1
	 */
	set	0x1, %o5		! set way bit to 1
	sllx	%o5, %o2, %o5		! .
	or	%o5, %o3, %o3		! .
2:
	set	0x1, %o5		! VA<23> == 1 to access tagECC
	sllx	%o5, 23, %o5		! .
	or	%o5, %o3, %o3		! .

	ldxa	[%o3]ASI_EC_DIAG, %o4	! read tagECC
	xor	%o4, %o1, %o4		! corrupt tagECC
	stxa	%o4, [%o3]ASI_EC_DIAG	! write back tagECC
	membar	#Sync

	/*
	 * Restore interrupts
	 */
	rdpr	%pstate, %o5
	or	%o5, PSTATE_IE, %o5
	retl
	wrpr	%o5, 0, %pstate

	SET_SIZE(chp_wr_ecache_tag_ecc)

#endif	/* lint */

/*
 * Register usage:
 *
 *	%i0 - aligned physical address
 *	%i1 - corruption pattern
 *	%i2 - address shift for diagnostic asi access
 *		(i.e. 19 for 1MB, 21 for 4MB, 22 for 8MB)
 *
 *	%o0 - scratch/saved %pstate
 *	%o1 - tagECC address
 *	%o2 - tag address
 *	%o3 - scratch
 *	%o4 - scratch
 *	%o5 - scratch
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
chp_wr_ecache_tag_data(uint64_t paddr_aligned, uint64_t xorpat,
			uint32_t ec_shift)
{}

#else	/* lint */

	ENTRY(chp_wr_ecache_tag_data)
	save	%sp, -SA(MINFRAME), %sp

	set	0x1, %o0		! generate mask for clearing upper bits
	sllx	%o0, %i2, %o4		! 	and ec_way
	sub	%o4, 0x1, %o0		! .
	and	%o0, %i0, %o2		! %o2 - Tag addr for way 0

	/*
	 * Only difference between tag addr and tagECC addr is
	 * that the later has bit <23> set.
	 */
	set	0x1, %o0
	sllx	%o0, 23, %o0
	or	%o2, %o0, %o1		! %o1 - TagECC addr for way 0

	rdpr	%pstate, %o0		! disable interrupts
	andn	%o0, PSTATE_IE, %o5	! .
	wrpr	%o5, 0, %pstate		! .

	ba	1f
	add	%i2, 0x2, %o3		! %o3 - ec_shift + 2

	.align	64
1:
	ldxa	[%i0]ASI_MEM, %g0	! load data

	/*
	 * According to PPG bit <63:44> of data from ASI_EC_DIAG
	 * will be set to zero by HW.
	 */
	ldxa	[%o2]ASI_EC_DIAG, %o5	! read tag
	srlx	%o5, %o3, %o5		! shift out unneeded lower bits
					! (%o3 - free for reuse)

	srlx	%i0, %i2, %o3		! shift out lower bits of address
					! 	to allow for comparison to tag
	cmp	%o3, %o5		! compare tag to modified PA
	be	2f			! data is in way 0
	nop

	/*
	 * Data is in way 1
	 */
	or	%o2, %o4, %o2		! set way bit for way 1
	or	%o1, %o4, %o1		! .
					! (%o4 - free for reuse)
2:
	ldxa	[%o2]ASI_EC_DIAG, %o4	! read the tag
	ldxa	[%o1]ASI_EC_DIAG, %o3	! read the tagECC
	xor	%o4, %i1, %o4		! corrupt tag
	stxa	%o4, [%o2]ASI_EC_DIAG	! write back corrupted tag
	membar	#Sync
	stxa	%o3, [%o1]ASI_EC_DIAG	! write back original tagECC
	membar	#Sync

	wrpr	%o0, %pstate		! restore interrupts

	ret
	restore	%g0, %g0, %o0
	SET_SIZE(chp_wr_ecache_tag_data)

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
 *
 *	%i0 - aligned physical address
 *	%i1 - corruption pattern
 *	%i2 - address shift for diagnostic asi access
 *		(i.e. 19 for 1MB, 21 for 4MB, 22 for 8MB)
 *	%i3 - staging register selection
 *
 *	%l5 - saved pstate
 *
 *	Returns 0 on success, or 0xfeccf on failure.
 */
#if defined(lint)

/*ARGSUSED*/
int
chp_wr_ecache(uint64_t paddr_aligned, uint64_t xorpat, uint32_t ec_shift,
		int reg_select)
{ return (0); }

#else	/* lint */

	ENTRY(chp_wr_ecache)
	save	%sp, -SA(MINFRAME), %sp

	set	0x1, %l0		! generate mask for clearing upper bits
	sllx	%l0, %i2, %l0		!	and ec_way field
	sub	%l0, 0x1, %l1		! .
	and	%i0, %l1, %l0		! %l0 - modified PA for tag access

	! set the way bit which can be OR'ed with
	! the address later on to allow access to way 1 if
	! necessary.
	! 1MB E$ - way bit <19>
	! 4MB E$ - way bit <21>
	! 8MB E$ - way bit <22>
	set	0x1, %l1
	sllx	%l1, %i2, %l1

	sll	%i3, 3, %i3		! generate asi vaddr from offset

	ldxa	[%i0]ASI_MEM, %g0	! read data to process any faults

	rdpr	%pstate, %l5		! disable interrupts
	andn	%l5, PSTATE_IE, %l4	! .
	wrpr	%l4, 0, %pstate		! .

	srlx	%i0, %i2, %l4		! format PA for comparison with tag
	set	2, %i4			! initialise loop counter
	mov	%g0, %i5		! return value = 0 (no error)

	add	%i2, 0x2, %l2		! %l2 = ec_shift + 2 which will save us
					! 	a couple of instrs later.
	set	0xfeccf, %i2		! %i2 - error return code
	ba	1f			! branch to aligned section
	nop

	.align	64
1:
	ldxa	[%i0]ASI_MEM, %g0	! load data into cache

	/*
	 * Try to determine into which way the
	 * data landed.
	 */
2:
	ldxa	[%l0]ASI_EC_DIAG, %l3	! read tag for way 0
	srlx	%l3, %l2, %l3		! format tag contents for comparison
					! (%l2 - free for reuse)

	cmp	%l4, %l3		! is data in this way ?
	bz	%icc, 3f		! .
	subcc	%i4, 1, %i4		! decrement loop counter
	bz,a	4f			! if not found in any way then
	mov	%i2, %i5		! return value = 0xfeccf (error)
	ba	2b
	add	%l0, %l1, %l0		! format address for way 1 tag access

	/*
	 * Found data, so now corrupt it.
	 */
3:
	ldxa	[%l0]ASI_EC_R, %g0	! read data into staging reg from e$
	ldxa	[%i3]ASI_EC_DATA, %l2	! read data from appropiate staging reg
	xor	%l2, %i1, %l2
	stxa	%l2, [%g0 + %i3]ASI_EC_DATA
	stxa	%g0, [%l0]ASI_EC_W	! store data from staging regs to e$
	membar	#Sync
4:
	wrpr	%l5, %pstate		! restore saved pstate
	ret
	restore	%g0, %i5, %o0
	SET_SIZE(chp_wr_ecache)

#endif	/* lint */

/*
 * This routine injects an error into the ecache at
 * the specified offset.
 *
 * When we store back the corrupted data correct ECC
 * will be attached to it. So to create an error we first
 * record the existing ECC which is protecting the existing data
 * then corrupt this data and write back the original (and now
 * incorrect) ECC.
 *
 * Register usage:
 *
 *	%o0 = ecache tag offset to modify
 *	%o1 = xor bit pattern to use for corruption
 *	%o2 = data to write to tags
 *	%o3 = addr to access tagECC
 *	%o4 = scratch
 *	%o5 = saved pstate reg
 */
#if defined(lint)

/* ARGSUSED */
void
chp_wr_etphys(uint64_t paddr, uint64_t xorpat, uint64_t data)
{}

#else	/* lint */

	ENTRY(chp_wr_etphys)
	andn	%o0, 0x1f, %o0		! align address on 32 byte
					! boundary

	/*
	 * Only difference between tag addr and tagECC addr is
	 * that the later has bit <23> set.
	 */
	set	0x1, %o3
	sllx	%o3, 23, %o3
	or	%o0, %o3, %o3		! %o3 - TagECC addr

	rdpr	%pstate, %o5		! disable interrupts
	andn	%o5, PSTATE_IE, %o4	! .
	wrpr	%o4, 0, %pstate		! .

	cmp	%o1, %g0		! check if xorpat was specified
	be,pn	%xcc, 2f		! and branch if not
	nop

	ba	1f
	nop

	.align	64
1:
	ldxa	[%o0]ASI_EC_DIAG, %o2	! read the tag
	xor	%o1, %o2, %o2		! corrupt tag
2:
	ldxa	[%o3]ASI_EC_DIAG, %o4	! read the tagECC
	stxa	%o2, [%o0]ASI_EC_DIAG	! write back corrupted tag
	membar	#Sync
	stxa	%o4, [%o3]ASI_EC_DIAG	! write back original tagECC
	membar	#Sync

	retl
	wrpr	%o5, %pstate		! restore interrupts
	SET_SIZE(chp_wr_etphys)

#endif	/* lint */

/*
 * This routine is used to corrupt the physical tag in the
 * data cache at a predetermined address.
 *
 * Register usage:
 *
 *	%o0 - address for tag asi access & status return value
 *	%o1 - expected tag value
 *	%o2 - corruption pattern
 *	%o3 - virtual address of data
 *
 *	%o5 - scratch
 *
 * Interrupts are assumed to be already disabled.
 */
#if defined(lint)

/* ARGSUSED */
int
chp_wr_dcache_ptag(uint64_t paddr, uint64_t tag_val, uint64_t xorpat,
				caddr_t vaddr)
{
	return (0);
}

#else	/* lint */

	ENTRY(chp_wr_dcache_ptag)
	ldx	[%o3], %g0
	membar	#Sync
1:
	ldxa	[%o0]ASI_DC_TAG, %o5
	sllx	%o5, 34, %o5			! format tag contents
	srlx	%o5, 35, %o5			! 	for comparison.
	cmp	%o5, %o1
	bne	2f
	nop

	membar	#Sync
	ldxa	[%o0]ASI_DC_TAG, %o5		! reload matching tag
	membar	#Sync
	xor	%o5, %o2, %o5			! corrupt appropiate bit(s).
	membar	#Sync
	stxa	%o5, [%o0]ASI_DC_TAG		! store back corrupt tag
	membar	#Sync
	ba	3f
	mov	%g0, %o0
2:
	set	CHP_DCACHE_IDX_INCR, %o5	! increment address for next way
	add	%o0, %o5, %o0			! .
	set	CH_DCACHE_IDX_LIMIT, %o5	! check if we have read all ways
	cmp	%o0, %o5			! .
	blt	1b				! no, so continue
	nop

	/*
	 * Fall through here if can't find data in any of
	 * the four ways. Set %o0 to 0xfeccf so calling function
	 * knows we had a problem.
	 */
	set	0xfeccf, %o0
3:
	retl
	nop
	SET_SIZE(chp_wr_dcache_ptag)
#endif	/* lint */

/*
 * This routine is used to corrupt data in the data cache at
 * a predetermined address.
 *
 * Register usage:
 *
 *	%o0 - address for tag asi access & status return value
 *	%o1 - address for data asi access
 *	%o2 - expected tag value
 *	%o3 - corruption pattern
 *	%o4 - virtual address of data
 *
 *	%o5 - scratch
 *
 * Interrupts are assumed to be already disabled.
 */
#if defined(lint)

/* ARGSUSED */
int
chp_wr_dcache_data(uint64_t tag_addr, uint64_t data_addr,
		uint64_t tag_value, uint64_t xorpat, caddr_t vaddr)
{
	return (0);
}

#else	/* lint */

	ENTRY(chp_wr_dcache_data)
	ldx	[%o4], %g0			! load the data
	membar	#Sync
1:
	ldxa	[%o0]ASI_DC_TAG, %o5		! read the tag
	sllx	%o5, 34, %o5			! format the tag contents
	srlx	%o5, 35, %o5			! 	for comparison
	cmp	%o5, %o2			! compare to expected value
	bne	2f				! no match
	nop

	! match; corrupt data
	membar	#Sync				! required
	ldxa	[%o1]ASI_DC_DATA, %o5		! read data from diag asi
	membar	#Sync				! required
	xor	%o5, %o3, %o5			! corrupt appropiate bit(s)
	membar	#Sync				! required
	stxa	%o5, [%o1]ASI_DC_DATA		! write back corrupted data
	membar	#Sync
	ba	3f
	mov	%g0, %o0
2:
	set	CHP_DCACHE_IDX_INCR, %o5	! increment address for next way
	add	%o0, %o5, %o0			! .
	add	%o1, %o5, %o1			! .
	set	CH_DCACHE_IDX_LIMIT, %o5	! check if we have read all ways
	cmp	%o0, %o5			! .
	blt	1b				! no, so continue
	nop

	/*
	 * Fall through here if can't find data in any of the
	 * four ways. Set %o0 to 0xfeccf so calling function knows
	 * we had a problem.
	 */
	set	0xfeccf, %o0
3:
	retl
	nop
	SET_SIZE(chp_wr_dcache_data)
#endif	/* lint */


/*
 * This routine is used to corrupt the parity bit(s) protecting
 * a specified data block in the data cache.
 *
 * Register usage:
 *
 *	%o0 - address for tag asi access & status return value
 *	%o1 - expected tag value
 *	%o2 - corruption pattern
 *	%o3 - virtual address of data
 *
 *	%o5 - scratch
 *
 * Interrupts are assumed to be already disabled.
 */
#if defined(lint)

/* ARGSUSED */
int
chp_wr_dcache_data_parity(uint64_t tag_addr, uint64_t tag_value,
				uint64_t xorpat, caddr_t vaddr)
{
	return (0);
}

#else	/* lint */

	ENTRY(chp_wr_dcache_data_parity)
	ldx	[%o3], %g0
	membar	#Sync
1:
	ldxa	[%o0]ASI_DC_TAG, %o5		! read the tag
	sllx	%o5, 34, %o5			! format tag contents for
	srlx	%o5, 35, %o5			! 	later comparison
	cmp	%o5, %o1
	bne	2f
	nop

	membar	#Sync
	ldxa	[%o0]ASI_DC_UTAG, %o5		! load matching uTag
	membar	#Sync
	xor	%o5, %o2, %o5			! corrupt appropiate bit(s)
	membar	#Sync
	stxa	%o5, [%o0]ASI_DC_UTAG		! write back
	membar  #Sync
	ba	3f
	mov	%g0, %o0
2:
	set	CHP_DCACHE_IDX_INCR, %o5	! increment address for next way
	add	%o0, %o5, %o0			! .
	set	CH_DCACHE_IDX_LIMIT, %o5	! check if we have read all ways
	cmp	%o0, %o5			! .
	blt	1b				! no, so continue
	nop

	/*
	 * Fall through here if can't find data in any of
	 * the four ways. Set %o0 to 0xfeccf so calling function
	 * knows we had a problem.
	 */
	set	0xfeccf, %o0
3:
	retl
	nop
	SET_SIZE(chp_wr_dcache_data_parity)
#endif	/* lint */

/*
 * This function corrupts the dcache physical tag
 * at the specified physical offset.
 *
 * Register usage:
 *	%o0 - physical address
 *	%o1 - corruption pattern
 *	%o2 - replacement tag value
 *
 *	%o3, %o4, %o5 - scratch
 */
#if defined(lint)

/* ARGSUSED */
void
chp_wr_dtphys(uint64_t aligned_paddr, uint64_t xorpat, uint64_t tag)
{}

#else	/* lint */

	/*
	 * This code must not cross a page boundary to avoid
	 * unwanted page faults.
	 */
	.align	128

	ENTRY(chp_wr_dtphys)
	andn	%o0, 0x7, %o0		! align physical address on 8
					! byte boundary

	rdpr	%pstate, %o5		! disable interrupts
	andn	%o5, PSTATE_IE, %o4	! .
	wrpr	%o4, 0, %pstate		! .

	cmp	%g0, %o1		! check if xorpat was specified
	be,pn	%xcc, 2f		! and branch if not
	nop

	ba	1f
	nop

	.align	64
1:
	ldxa	[%o0]ASI_DC_TAG, %o3	! get existing tag
	xor	%o3, %o1, %o2		! corrupt it
2:
	stxa	%o2, [%o0]ASI_DC_TAG
	membar	#Sync

	retl
	wrpr	%o5, %pstate		! restore interrupts
	SET_SIZE(chp_wr_dtphys)

#endif	/* lint */

/*
 * This function is used to cause a snoop error by writing
 * to a location which maps to a cache index whose snoop tag
 * contains an error and doesn't match the stored data.
 */
#if defined(lint)

/* ARGSUSED */
void
chp_wr_dcache_snoop(uint64_t data, caddr_t addr)
{}

#else	/* lint */

	ENTRY(chp_wr_dcache_snoop)
	stx	%o0, [%o1]
	membar	#Sync
	retl
	nop
	SET_SIZE(chp_wr_dcache_snoop)

#endif	/* lint */

/*
 * This function tests the D$ snoop mechanism.
 *
 * Due to the pseudo-random nature of the line replacement algorithm
 * in the D$, loading 4 pieces of data that map to the same cache index does
 * not guarantee that the specified line in each way will be filled with
 * this data. However if the micro-tag of the fill line matches one of
 * the existing micro-tags in the set the matching line *must* be
 * replaced (Ch Micro-Arch Instr Issue Unit Sect 2.2.1.1, the same
 * mechanism works for both I$ and D$).
 *
 * So we in effect prime one line in each way so that when we load
 * the data we are guaranteed that they will be placed into the
 * appropiate line.
 *
 * Once we have the four lines filled with known valid data, we
 * insert a parity error into one of the snoop tags using diagnostic
 * access. We then have another processor perform a write to an address
 * which maps to the same D$ tag index, but doens't match any of the
 * entries in the cache.
 *
 * Register usage:
 *
 *	%o0 - addr for tag asi access
 *	%o1 - utag value for way 0
 *	%o2 - address of start of data buffer
 *	%o3 - corruption pattern
 *
 *	%o4 - scratch
 *	%o5 - scratch
 *
 * Interrupts are assumed to be already disabled.
 */
#if defined(lint)

/* ARGSUSED */
void
chp_wr_dcache_stag(uint64_t tag_addr, uint64_t utag_val,
			uint64_t start_addr, uint64_t xorpat)
{}

#else	/* lint */

	.align	64
	ENTRY(chp_wr_dcache_stag)
	set	0x4, %o5			! initialise counter
1:
	stxa	%g0, [%o0]ASI_DC_TAG		! clear tag and valid bit

	membar	#Sync
	ldxa	[%o0]ASI_DC_UTAG, %o4		! read utag
	membar	#Sync
	srlx	%o4, 8, %o4
	sllx	%o4, 8, %o4			! zero out utag value
	or	%o4, %o1, %o4			! or in new utag value
	membar	#Sync
	stxa	%o4, [%o0]ASI_DC_UTAG		! prime line
	membar	#Sync
	ldx	[%o2], %g0			! load data into line

	set	0x1, %o4			! incr utag for next way
	add	%o4, %o1, %o1			! .
	set	CHP_DCACHE_IDX_INCR, %o4	! incr data addr for next way
	add	%o2, %o4, %o2			! .
	add	%o0, %o4, %o0			! incr asi addr for next way

	dec	%o5
	cmp	%o5, %g0
	bgt,pt	%icc, 1b
	nop

	sub	%o0, %o4, %o0			! adjust address back
	ldxa	[%o0]ASI_DC_SNP_TAG, %o4	! read snoop tag
	xor	%o4, %o3, %o4			! corrupt snoop tag
	stxa	%o4, [%o0]ASI_DC_SNP_TAG	! store back
	membar	#Sync

	retl
	nop
	SET_SIZE(chp_wr_dcache_stag)

#endif	/* lint */

/*
 * This routine returns the physical tag and data for each way
 * in the data cache at a predetermined address.
 *
 * Register usage:
 *
 *	%o0 - data asi addr
 *	%o1 - tag asi addr
 *	%o2 - data struct ptr
 *
 *	%o3 - scratch
 *	%o4 - scratch
 *	%o5 - scratch
 *
 * Interrupts are assumed to be already disabled.
 */
#if defined(lint)

/* ARGSUSED */
void
chp_rd_dcache(uint64_t addr, uint64_t tag_addr, uint64_t *ptr)
{}

#else	/* lint */

	.align	64
	ENTRY(chp_rd_dcache)
	set	0x4, %o5			! initialise counter
	set	CHP_DCACHE_IDX_INCR, %o4
1:
	membar	#Sync				! required
	ldxa	[%o0]ASI_DC_DATA, %o3
	membar	#Sync				! required
	stx	%o3, [%o2]			! store data into struct
	add	%o2, 8, %o2			! increment struct pointer
	ldxa	[%o1]ASI_DC_TAG, %o3		! read the ptag
	stx	%o3, [%o2]			! store tag into struct
	add	%o2, 8, %o2			! increment struct pointer

	add	%o0, %o4, %o0			! increment data asi addr
	add	%o1, %o4, %o1			! increment tag asi addr

	dec	%o5
	cmp	%o5, %g0
	bgt,pt	%icc, 1b
	nop

	retl
	nop
	SET_SIZE(chp_rd_dcache)
#endif


/* Macro for I$ Data Corruption */
#define ICDATA_SET_WAYADDR(r0, r1, r2, incr, lim)	\
	set 	incr, r2;	\
	add	r2, r0, r0;	\
	add	r2, r1, r1;	\
	set	lim, r2

/* Macro for I$ Tag Corruption */
#define ICTAG_SET_WAYADDR(r0, r1, r2, incr, lim)	\
	set 	incr, r2;	\
	add	r2, r0, r0;	\
	set	lim, r2

/*
 * This routine locates and corrupts an instruction
 * in the icache.
 *
 * Register usage:
 *	%o0 - VA to read tag
 * 	%o1 - VA to read instr
 * 	%o2 - PA for tag comparison
 *	%o3 - corruption pattern
 *	%o4 - cpu implementation id
 *	%o5 - scratch
 *
 * Interrupts are disabled before calling this routine to
 * reduce the immediate code footprint.
 */
#if defined(lint)
/*ARGSUSED*/
int
chp_wr_icache_instr(uint64_t ic_taddr, uint64_t ic_addr, uint64_t ic_paddr, 
			uint64_t xorpat, int impl)
{
	return (0);
}

#else
        .align  128
        ENTRY(chp_wr_icache_instr)

1:      
	ldxa	[%o0]ASI_IC_TAG, %o5		! fetch instruction tag
	sllx 	%o5, 27, %o5			! PA <41:13>
	srlx 	%o5, 35, %o5
	cmp 	%o5, %o2			! compare with tag formed
						!	from instr pa
	bne 	2f				! next way
	nop

	ldxa	[%o1]ASI_IC_DATA, %o5		! fetch instr to corrupt
	xor     %o5, %o3, %o5                   ! corrupt  instr
	stxa 	%o5, [%o1]ASI_IC_DATA		! store back
	membar 	#Sync
	flush 	0

	ba 	4f
	nop

2:
	! Next way match
	! switch to CHP or PN limits 
	cmp 	%o4, PANTHER_IMPL
	bne,a 	3f
	nop

	! PN: fetch next way
	ICDATA_SET_WAYADDR(%o0, %o1, %o5, PN_ICACHE_IDX_INCR, PN_ICACHE_IDX_LIMIT);
	cmp 	%o0, %o5
	blt 	1b
	nop
	set 	0xfeccf, %o0			! return fail pattern
	ba 	4f
	nop

3:	! Cheetah 
	! fetch next way
	ICDATA_SET_WAYADDR(%o0, %o1, %o5, CH_ICACHE_IDX_INCR, CH_ICACHE_IDX_LIMIT);
	cmp 	%o0, %o5
	blt 	1b
	nop
	set 	0xfeccf, %o0			! return fail pattern

4:
	retl
	nop
	SET_SIZE(chp_wr_icache_instr)
#endif  /* lint */

/*
 * This function is used to corrupt the physical
 * tag of a line in the icache.
 *
 * Register usage:
 *
 *	%o0 - VA to read tag
 *	%o1 - PA for tag comparison
 *	%o2 - corruption pattern
 *	%o3 - cpu imeplementation id
 *
 * Interrupts are disabled before calling this routine to
 * reduce the immediate code footprint.
 */
#if defined(lint)

/* ARGSUSED */
int
chp_wr_icache_ptag(uint64_t tag_addr, uint64_t paddr, uint64_t xorpat, 
				int impl)
{
	return (0);
}

#else
	.align	128
	ENTRY(chp_wr_icache_ptag)
1:
	ldxa	[%o0]ASI_IC_TAG, %o5		! read the tag
	sllx	%o5, 27, %o5			! format tag contents for
	srlx	%o5, 35, %o5			!	for comparison
	cmp	%o5, %o1			! compare to PA passed in
	bne	2f				! match ? no then branch
	nop

	! Match: corrupt tag
	ldxa	[%o0]ASI_IC_TAG, %o5		! reload tag
	xor	%o5, %o2, %o5			! apply corruption pattern
	stxa	%o5, [%o0]ASI_IC_TAG
	membar	#Sync
	flush	0				! flush the pipeline so that
						! we reload the corrupted instr
	ba	4f				! exit with corruption
	nop

2:	! Look in next way 

	!Panther check
	cmp 	%o3, PANTHER_IMPL		! impl. is Panther?
	bne,a 	3f				! no: br. to CHP
	nop

	! Panther next way load
	ICTAG_SET_WAYADDR(%o0, %o1, %o5, PN_ICACHE_IDX_INCR, PN_ICACHE_IDX_LIMIT);
	cmp 	%o0, %o5
	blt 	1b				! retry corruption
	nop
	set 	0xfeccf, %o0			! fail pattern
	ba 	4f
	nop

3:	! Cheetah+ default next way load
	ICTAG_SET_WAYADDR(%o0, %o1, %o5, CH_ICACHE_IDX_INCR, CH_ICACHE_IDX_LIMIT);
	cmp 	%o0, %o5
	blt 	1b
	nop
	set 	0xfeccf, %o0			! fall through to exit

	/*
	 * Fall through here if can't find instr in any of the four
	 * ways. Set %o0 to 0xfeccf so calling function knows we had
	 * a problem.
	 */
4:
	retl
	nop
	SET_SIZE(chp_wr_icache_ptag)
#endif	/* lint */

/*
 * This function is used to corrupt the icache physical tag
 * at the specified physical offset.
 *
 * Register usage :
 *	%o0 - address to read tag
 *	%o1 - corruption pattern
 *	%o2 - replacement tag value
 *
 *	%o3 - scratch
 */
#if defined(lint)

/* ARGSUSED */
void
chp_wr_itphys(uint64_t tag_addr, uint64_t xorpat, uint64_t tag)
{}

#else
	.align 32
	ENTRY(chp_wr_itphys)
	andn	%o0, 0x1f, %o0		! align address and clear <4:0>
					! to access ptag
	cmp	%o1, %g0		! test if xorpat is set
	be,pn	%xcc, 1f		! if not, branch

	ldxa	[%o0]ASI_IC_TAG, %o3	! get existing tag
	xor	%o3, %o1, %o2		! xor with specified pattern
1:
	stxa	%o2, [%o0]ASI_IC_TAG	! write back tag
	membar	#Sync
	retl
	nop
	SET_SIZE(chp_wr_itphys)
#endif	/* lint */


/*
 * This function tests the I$ snoop mechanism.
 *
 * Warning : if this routine is altered in size then CHP_SNP_SZ
 * in memtest_chp.c will need to be updated with the new size.
 *
 * Due to the pseudo-random nature of the line replacement algorithm
 * in the I$, executing 4 instrs that map to the same cache index does
 * not guarantee that the specified line in each way will be filled with
 * these instructions. However if the micro-tag of the fill line matches
 * one of the existing micro-tags in the set the matching line *must* be
 * replaced (Ch Micro-Arch Instr Issue Unit Sect 2.2.1.1).
 *
 * So we in effect prime one line in each way so that when we execute
 * our target instructions we are guaranteed that they will be placed
 * into that specified line.
 *
 * We then corrupt the snoop tag for one of these lines and perform
 * a store to an address which has the same index but different tag,
 * which results in the one of the lines being invalidated or displaced
 * by instructions from other addresses that happen to map to the same index.
 *
 * Register usage:
 *	%i0 - address of 1'st target function
 *	%i1 - address of tag for line in way 0
 *	%i2 - utag value for line in way 0
 *	%i3 - corruption pattern
 *	%i4 - address to which store will be performed
 *		to trigger snooping
 *	%i5 - ptr to data struct for return values
 *
 * Interrupts are disabled before calling this routine to
 * reduce the immediate code footprint.
 */
#if defined(lint)

/* ARGSUSED */
void
chp_wr_icache_stag(uint64_t addr, uint64_t tag_addr, uint64_t utag_val,
		uint64_t xorpat, uint64_t store_addr, uint64_t *ptr)
{}

#else	/* lint */

	ENTRY(chp_wr_icache_stag)
	save	%sp, -SA(MINFRAME), %sp
	set	CH_ICACHE_IDX_INCR, %l1
	set	CHP_ICACHE_FN_ADDR_ICR, %l2
	mov	%i1, %l3			! save inital value for later
	set	0x4, %l5			! initialise loop counter
1:
	set	0x2, %l0			! select valid bit
	sllx	%l0, 3, %l0			! .
	or	%l0, %i1, %l0			! .
	stxa	%g0, [%l0]ASI_IC_TAG		! clear valid bit
	membar	#Sync

	set	CH_ICTAG_UTAG, %l0		! select utag
	or	%l0, %i1, %l0			! .
	stxa	%i2, [%l0]ASI_IC_TAG		! 'prime' the line
	membar	#Sync

	call	%i0				! call target function
	nop

	add	%l2, %i0, %i0			! select next fn to call
	add	%l1, %i1, %i1			! increment address for next way
	set	0x1, %l0			! increment utag for next way
	sllx	%l0, 38, %l0			! .
	add	%i2, %l0, %i2			! .

	dec	%l5
	cmp	%l5, %g0
	bgt,pt	%icc, 1b
	nop

	mov	%l3, %i1			! restore previously saved value

	! corrupt the snoop tag
	ldxa	[%i1]ASI_IC_SNP_TAG, %l2
	xor	%i3, %l2, %l2
	stxa	%l2, [%i1]ASI_IC_SNP_TAG
	membar	#Sync

	! do the store to cause the snoop error
	stx	%g0, [%i4]
	membar	#Sync

	clr	%l0				! clear %l0 for later use
	set	CH_ICACHE_IDX_LIMIT, %l2

	mov	%i1, %l4			! form address for valid
	set	0x2, %l5			!	tag asi access
	sllx	%l5, 3, %l5			! .
	or	%l5, %l4, %l4			! .
2:
	ldxa	[%i1]ASI_IC_DATA, %l3		! read the instr
	stx	%l3, [%i5 + %l0]		! store instr into data struct
	add	%l0, 8, %l0			! increment ptr into struct

	ldxa	[%l4]ASI_IC_TAG, %l3		! read valid tag for line
	stx	%l3, [%i5 + %l0]		! store valid bit into struct
	add	%l0, 8, %l0			! increment ptr into struct

	add	%i1, %l1, %i1			! increment address for next way
	add	%l4, %l1, %l4			! .
	cmp	%i1, %l2			! check if we have read all ways
	blt,pt	%icc, 2b
	nop

	ret
	restore	%g0, %g0, %o0
	SET_SIZE(chp_wr_icache_stag)
#endif	/* lint */

/*
 * This routine is copied to addresses that map to a line
 * in each of the different ways in the icache and then
 * executed to fill each line with the 'clr' instr.
 *
 * Called by chp_wr_icache_stag()
 */
#if defined(lint)
void
chp_ic_stag_tgt()
{}
#else	/* lint */
	ENTRY(chp_ic_stag_tgt)
	mov	%g0, %g0
	retl
	nop
	SET_SIZE(chp_ic_stag_tgt)
#endif	/* lint */

/*
 * chp_wr_dup_ecache_tag()
 * Induce a Duplicate Tag in E$ -- Internal Processor Error.
 * This function is called on either a Jag or Ch+ processor.
 *	See Ch+/Jag PRM 10.7
 *
 * Register usage:
 *	%o0 - PA of E$ line we wish to duplicate.
 *	%o1 - Appropriate E$ shift for (Ch+/Jag) E$ size.
 */

#if defined(lint)
/*ARGSUSED*/
void
chp_wr_dup_ecache_tag(uint64_t paddr, int ec_shift)
{
	return;
}
#else
        ENTRY(chp_wr_dup_ecache_tag)
	! format paddr for tag fetch
	! use ec_shift(%o1) to form tag-index address
	mov	1, %o2
	sllx	%o2, %o1, %o2
	sub	%o2, 1, %o2		! %o2 = tag-index mask
	and	%o0, %o2, %o2		! %o2 = w0 tag-index-addr ASI VA 

	! format paddr for tag comparison 
	! PA bits for tag compare depend on E$ size ~= ec_shift 
	! for both Jag And Ch+
	! assume upper bits of PA are zero'ed 
	srlx	%o0, %o1, %o3

	rdpr    %pstate, %o4            ! disable interrupts
	andn    %o4, PSTATE_IE, %o4     ! .
	ba	0f			! skip alignment padding
	wrpr    %o4, 0, %pstate         !

	.align  256
0:
	ldxa    [%o0]ASI_MEM, %g0       ! load data into e$

	! %o0 = reuse
	! %o1 = ec_shift
	! %o2 = way0 tag-index-addr
	! %o3 = way0 PA tag-compare bits
	! %o4, %o5 = scratch

	GET_CPU_IMPL(%o0)			! %o0 = cpu impl.
	membar 	#Sync
	ldxa   	[%o2]ASI_EC_DIAG, %o5		! fetch tag for comparison in this way
	membar 	#Sync
	mov	22, %o4				! CH+: EC_Tag = 4M<:43:23> or 8M:<43:24>
	cmp	%o0, CHEETAH_PLUS_IMPL		! Jag: EC_Tag = 4M<:41:21> or 8M:<43:22>
	move	%xcc, 20, %o4			! %o4 = tag cmp shift: Ch+:20, Jag:22
	
	sllx    %o5, %o4, %o5                   ! clear upper bits of tag content
	srlx    %o5, %o4, %o5                   ! for comparison, then lower bits
	mov	%o1, %o4			! initialize ec_shift value 
	cmp	%o0, CHEETAH_PLUS_IMPL		! EC_Tag in tagreg varies from Jag to Ch+
	be,a	1f				! Jag: EC_Tag = 4M<:41:21> or 8M:<43:22>
	add	%o4, 2, %o4			! CH+: EC_Tag = 4M<:43:23> or 8M:<43:24> 
1:
	! %o4 = Ch+ or Jag ec_shift value
	srlx	%o5, %o4, %o5			! use ec_shift for clearing lower bits

	! We're now set for tag compare in way 0
	mov	1, %o4				! way0 tag-match flag:1=match 0=no-match
	cmp    	%o5, %o3			! compare tag; if not equal
	movne	%xcc, %g0, %o4			! clear match-flag.

	! if NO tag match in way0, then tag-match-flag %o4=0 now
	! 	and we assume we'll get a match in way1 
	! else if match in way0, %o4=1
	ldxa   	[%o2]ASI_EC_DIAG, %o0		! reload way0 regval

	! %o0 = reg val for way0
	! %o1 = ec_shift
	! %o2 = way0 tag-index-addr
	! %o3 = way0 PA tag-compare bits
	! %o4 = match-flag: 0(no-match), 1(match) in way0
	! %o5 = scratch

	sllx	%o4, %o1, %o5			! depending on way0 match(%o4)
	or 	%o5, %o2, %o3			! %o3 - tag index for way0 or way1	
	
	! if way0 match, %o4=1 and %o3=tag-index for way1
	! else (way1 match), %o4=0 and %o3=tag-index addr for way0

	! fetch tag regval for way1, explicitly
	mov	1, %o5
	sllx	%o5, %o1, %o5			! <way1 index>|ec_way=bit<ec_shift>
	or 	%o5, %o2, %o2			! %o2 = index for way1
	membar	#Sync
	ldxa	[%o2]ASI_EC_DIAG, %o1		! %o1 = regval for way1

	! %o1 = regval for way1
	! %o0 = regval for way0
	cmp	%o4, 1
	movne	%xcc, %o1, %o0

	! If match in way0(%o4=1):
	!	%o0 = way0 regval; %o3 = way1 tag index addr.
	!	store/dup way0 regval in way1 tag index. 
	! else (no match in way0(%o4=0) -> match in way1):
	!	%o0 = way1 regval; %o3 = way0 tag index addr.
	! 	store/dup way1 regval in way0 tag-index.
	! Next load/store will result in Duplicate Tag Error.

	stxa	%o0, [%o3]ASI_EC_DIAG
	membar 	#Sync

	! restore interrupts; exit
	rdpr    %pstate, %o5
	or      %o5, PSTATE_IE, %o5
	retl
	wrpr    %o5, 0, %pstate
	SET_SIZE(chp_wr_dup_ecache_tag)
#endif /* lint */
