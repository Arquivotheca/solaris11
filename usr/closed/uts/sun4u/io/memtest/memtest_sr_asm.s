/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/cheetahregs.h>
#include <sys/memtest_u_asm.h>

/*
 * This routine is used to corrupt the ecache tag data
 * which are protected by ecc and ecc.
 *
 * Register usage:
 *
 *	%i0 - 32 byte aligned physical address to corrupt
 *	%i1 - corruption (xor) pattern
 *	%i2 - debug pointer to store information
 *	%i3 - corrupted tag data/ecc
 *	%i4 - loop counter
 *	%i5 - also used as debug pointer for storing info
 *
 *	%l0 - tag compare value for e$ tag based on paddr
 *	%l1 - mask for tag before comparing
 *	%l2 - way increment for asi addr
 *	%l3 - return value
 *	%l4 - asi address for e$ tag read
 *	%l5 - not used
 *	%l6 - saved pstate
 *
 *	%o0 - tmp
 *	%o1 - tmp
 *	%o2 - tmp
 * 	%o3 - tmp
 *	%o4 - tmp
 */
#if defined(lint)
/* ARGSUSED */
void
sr_wr_ecache_tag(uint64_t paddr, uint64_t xorpat, uint64_t *debug)
{}

#else	/* lint */

#ifdef	DEBUG_WR_ECACHE_TAG_DATA
#define	OFF_C_PADDR	0x00	/* paddr */
#define	OFF_C_XORPAT	0x08	/* xor pattern */
#define	OFF_C_DATA	0x10	/* unused */
#define	OFF_C_DEBUGP	0x18	/* debug pointer */
#define	OFF_C_TAG_CMP	0x20	/* tag compare */
#define	OFF_C_TAG_MASK	0x28	/* tag mask */
#define	OFF_C_WAY_INCR	0x30	/* way incr */
#define	OFF_C_FLUSH_B	0x38	/* disp flush bit */
#define	OFF_C_ASI_ADDR	0x40	/* asi addr for tag read */
#define	OFF_C_UNUSED_50	0x48	/* unused */
#define	OFF_C_PSTATE	0x50	/* saved %pstate */
#define	OFF_C_RET	0x58	/* return value */
#define	OFF_C_PA_MASK	0x60	/* paddr mask */
#define	OFF_C_LOOP_CNT	0x68	/* loop counter */
#define	OFF_C_LOOPS	0x70	/* # of loops */
#define	OFF_C_FLUSH	0x78	/* asi address for flush */
#define	OFF_C_TAGS	0x80	/* tag for way 0 */
/*
 *			0x88	compare value for way 0
 *			0x90	tag for way 1
 *			0x98	compare value for way 1
 *			0xa0	tag for way 2
 *			0xa8	compare value for way 2
 *			0xb0	tag for way 3
 *			0xb8	compare value for way 3
 */
#endif

	
	.align	256
	ENTRY(sr_wr_ecache_tag)

	save	%sp, -SA(MINFRAME), %sp

#ifdef	DEBUG_WR_ECACHE_TAG
	stx	%i0, [%i2 + OFF_C_PADDR]
	stx	%i1, [%i2 + OFF_C_XORPAT]
	stx	%i2, [%i2 + OFF_C_DEBUGP]
#endif
	/*
	 * Generate tag asi addresses to use below.
	 */
	set	1, %l2				! %l2 - way incr for asi addr
	sllx	%l2, JA_EC_IDX_WAY_SHIFT, %l2	! .
	set	0xfffc0, %o1			! mask for paddr
	and	%i0, %o1, %l4			! %l4 - asi addr for way 0 tag

	set	4, %i4				! %i4 - way loop counter below

	srlx	%i0, 18, %l0			! %l0 - tag compare value for pa
	set	0x1ffffff, %l1			! mask for tag

	ldxa	[%i0]ASI_MEM, %g0		! read data to process faults

	/*
	 * Save variables in debug location.
	 */
#ifdef	DEBUG_WR_ECACHE_TAG
	stx	%l0, [%i2 + OFF_C_TAG_CMP]
	stx	%l1, [%i2 + OFF_C_TAG_MASK]
	stx	%l2, [%i2 + OFF_C_WAY_INCR]
	stx	%l4, [%i2 + OFF_C_ASI_ADDR]
	stx	%l6, [%i2 + OFF_C_PSTATE]
	stx	%o1, [%i2 + OFF_C_PA_MASK]
	stx	%i4, [%i2 + OFF_C_LOOP_CNT]
	stx	%g0, [%i2 + OFF_C_LOOPS]
	add	%i2, OFF_C_TAGS, %i5
#endif

	rdpr	%pstate, %l6			! disable interrupts
	andn	%l6, PSTATE_IE, %o3		! .
	wrpr	%o3, 0, %pstate			! .

	mov	%g0, %l3			! return value = 0 (no error)

	ba	1f				! branch to aligned code
	nop

	.align	64
1:
	ldxa	[%i0]ASI_MEM, %g0		! get data into cache

	/*
	 * Try to find which way the data ended up in.
	 */
2:
#ifdef	DEBUG_WR_ECACHE_TAG
	ldx	[%i2 + OFF_C_LOOPS], %o4
	inc	%o4
	stx	%o4, [%i2 + OFF_C_LOOPS]
#endif
	ldxa	[%l4]ASI_EC_DIAG, %o0		! read tag for next way
#ifdef	DEBUG_WR_ECACHE_TAG
	stx	%o0, [%i5]
	add	%i5, 8, %i5
#endif
	and	%o0, %l1, %o1			! format tag for comparison
#ifdef	DEBUG_WR_ECACHE_TAG
	stx	%o1, [%i5]
	add	%i5, 8, %i5
#endif
	cmp	%l0, %o1			! is the data in this way?
	bz	%icc, 3f			! .
	subcc	%i4, 1, %i4			! decrement loop counter
	bz,a	4f				! if not found in any way then
	mov	1, %l3				! return value = 1 (error)
	ba	2b
	add	%l4, %l2, %l4			! generate next asi addr
						! for tag read
	/*
	 * Found it, now corrupt it.
	 * (%o0 still has tag value to corrupt from above)
	 * (%i1 has xorpat)
	 */
3:
	xor	%o0, %i1, %i3			! corrupt data
	stxa	%i3, [%l4]ASI_EC_DIAG		! write corrupt data back
	membar	#Sync

4:
	wrpr	%l6, %pstate			! restore saved pstate
	ret					! return value = 0 (ok)
	restore	%g0, %g0, %o0			! .
	SET_SIZE(sr_wr_ecache_tag)

#endif	/* lint */
	
/*
 * This routine injects an error into the ecache data at
 * the specified ecache offset.
 *
 * Register usage:
 *
 *	%o0 = 32 byte aligned ecache offset to modify
 *	%o1 = staging reg offset (0-4) to corrupt
 *	%o2 = xor bit pattern to use for corruption
 *	%o3 = data to use if xorpat is 0
 *	%o4 = tmp
 *	%o5 = saved pstate reg
 */
#if defined(lint)
/* ARGSUSED */
void
sr_wr_ephys(uint64_t offset_aligned, int reg_offset, uint64_t xorpat,
	uint64_t data)
{}

#else	/* lint */


	.align	128

	ENTRY(sr_wr_ephys)

	/*
	 * Since the way bits in the asi address are not contiguous
	 * with the rest of the addr, they must be isolated in the
	 * specified offset and moved to the proper location.
	 */
	set	SR_L2_WAYS_MASK, %o4		! mask for way bits in offset
	and	%o0, %o4, %o5			! isolate the bits and
	sllx	%o5, SR_L2_WAYS_SHIFT, %o5	! move them to new location

	andn	%o0, %o4, %o0			! clear bits 20,21 in offset
	or	%o0, %o5, %o0			! and move them to bits 33,32

	rdpr    %pstate, %o5			! disable interrupts
	andn    %o5, PSTATE_IE, %o4		! .
	wrpr    %o4, 0, %pstate			! .

	ba	1f
	sll	%o1, 3, %o1			! generate asi addr from offset

	.align	64
1:
	ldxa	[%o0]ASI_EC_R, %g0		! read e$ into staging regs
	ldxa	[%o1]ASI_EC_DATA, %o4		! read data from staging reg

	cmp	%o2, %g0			! if xorpat != 0 then
	bnz,a	%xcc, 2f			! use it for corruption
	xor	%o4, %o2, %o3			! .

2:
	stxa	%o3, [%o1]ASI_EC_DATA		! write data back to staging reg
	stxa	%g0, [%o0]ASI_EC_W		! write e$ from staging regs
	membar	#Sync

	retl
	wrpr	%o5, %pstate			! restore interrupts
	SET_SIZE(sr_wr_ephys)

#endif	/* lint */

/*
 * This routine injects an error into the ecache tags at
 * the specified ecache offset.
 *
 * Register usage:
 *
 *	%o0 = 32 byte aligned ecache tag offset to modify
 *	%o1 = xor bit pattern to use for corruption
 *	%o2 = data to write to tags
 *	%o3 = tmp
 *	%o4 = tmp
 *	%o5 = saved pstate reg
 */
#if defined(lint)
/* ARGSUSED */
void
sr_wr_etphys(uint64_t paddr, uint64_t xorpat, uint64_t data)
{}

#else	/* lint */

	ENTRY(sr_wr_etphys)

	/*
	 * Since the way bits in the adi address are not contiguous
	 * with the rest of the addr, they must be isolated in the
	 * specified offset and moved to the proper location.
	 */
	set	SR_L2_WAYS_MASK, %o3		! mask for way bits in offset
	and	%o0, %o3, %o4			! isolate the bits and
	sllx	%o4, SR_L2_WAYS_SHIFT, %o4	! move them to new location

	andn	%o0, %o3, %o0			! clear bits 21,20 in offset
	or	%o0, %o4, %o0			! and move them to bits 33,32

	rdpr    %pstate, %o5			! disable interrupts
	andn    %o5, PSTATE_IE, %o4		! .
	wrpr    %o4, 0, %pstate			! .

	ba	1f
	nop

	.align	64
1:
	ldxa	[%o0]ASI_EC_DIAG, %o3		! read the tag

	cmp	%o1, %g0			! if xorpat != 0 then
	bnz,a	%xcc, 2f			! use it for corruption
	xor	%o3, %o1, %o2			! .

2:
	stxa	%o2, [%o0]ASI_EC_DIAG		! write corrupt data back
	membar	#Sync

	retl
	wrpr	%o5, %pstate			! restore interrupts
	SET_SIZE(sr_wr_etphys)

#endif	/* lint */
