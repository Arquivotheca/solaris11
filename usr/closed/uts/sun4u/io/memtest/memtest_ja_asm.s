/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/cheetahregs.h>
#include <sys/memtest_u_asm.h>

/*
 * Register usage:
 *
 *	%i0 - physical memory address to write to
 *	%i1 - ecc to write with data (also used for e$ err en reg)
 *	%i2 - ecache set size (also used for return value)
 *	%i3 - debug pointer to store information
 *	%i4 - loop counter
 *	%i5 - also used as debug pointer for storing info
 *
 *	%l0 - tag compare value for e$ tag based on paddr
 *	%l1 - mask for tag before comparing
 *	%l2 - way increment for asi addr
 *	%l3 - disp flush bit
 *	%l4 - asi address for e$ tag read
 *	%l5 - saved E$ error reg
 *	%l6 - saved pstate
 *
 *	%o0 - tmp
 *	%o1 - tmp
 */
#if defined(lint)

/* ARGSUSED */
uint64_t
ja_wr_memory(uint64_t paddr, uint_t ecc, uint64_t ec_set_size, uint64_t *debug)
{ return (0); }

#else	/* lint */

#ifdef	DEBUG_WR_MEMORY
/*
 * Offsets to store variables at.
 */
#define	OFF_A_PADDR	0x00	/* paddr */
#define	OFF_A_ECC	0x08	/* ecc pattern in ja_wr_memory() */
#define	OFF_A_DEBUGP	0x10	/* debug pointer */
#define	OFF_A_NEW_EER	0x18	/* e$ err enable reg */
#define	OFF_A_TAG_CMP	0x20	/* tag compare */
#define	OFF_A_TAG_MASK	0x28	/* tag mask */
#define	OFF_A_WAY_INCR	0x30	/* way incr */
#define	OFF_A_FLUSH_B	0x38	/* disp flush bit */
#define	OFF_A_ASI_ADDR	0x40	/* asi addr for tag read */
#define	OFF_A_OLD_EER	0x48	/* saved e$ err en reg */
#define	OFF_A_PSTATE	0x50	/* saved %pstate */
#define	OFF_A_RET	0x58	/* return value */
#define	OFF_A_PA_MASK	0x60	/* paddr mask */
#define	OFF_A_LOOP_CNT	0x68	/* loop counter */
#define	OFF_A_LOOPS	0x70	/* # of loops */
#define	OFF_A_FLUSH	0x78	/* asi address for flush */
#define	OFF_A_TAGS	0x80	/* tag for way 0 */
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
	ENTRY(ja_wr_memory)
	save    %sp, -SA(MINFRAME), %sp		! get a new window

#ifdef	DEBUG_WR_MEMORY
	stx	%i0, [%i3 + OFF_A_PADDR]
	stx	%i1, [%i3 + OFF_A_ECC]
	stx	%i3, [%i3 + OFF_A_DEBUGP]
#endif
	/*
	 * Generate new/diag e$ error enable reg to use below.
	 */
	ldxa	[%g0]ASI_ESTATE_ERR, %l5	! read E$ err reg
	set	EN_REG_FMED, %o0		! clear FMED field
	andn	%l5, %o0, %o0			! .
	sll	%i1, EN_REG_FMED_SHIFT, %i1	! shift ecc into position
	or	%i1, %o0, %i1			! and set FMED field
	set	EN_REG_FME, %o0			! set FME bit
	or	%i1, %o0, %i1			! %i1 - new e$ err enable reg

	/*
	 * Generate tag asi addresses to use below.
	 */
	set	JA_EC_IDX_DISP_FLUSH, %l3	! %l3 - displacement flush bit
	set	1, %l2				! %l2 - way incr for asi addr
	sllx	%l2, JA_EC_IDX_WAY_SHIFT, %l2	! .
	subcc	%i2, 1, %o1			! mask for paddr
	andn	%o1, 0x3f, %o1			! clear bit [5:0]
	and	%i0, %o1, %l4			! %l4 - asi addr for way 0 tag

	set	4, %i4				! %i4 - way loop counter below

	srlx	%i0, 18, %l0			! %l0 - tag compare value for pa
	set	0x1ffffff, %l1			! mask for tag

	/*
	 * Save variables in debug location.
	 */
#ifdef	DEBUG_WR_MEMORY
	stx	%i1, [%i3 + OFF_A_NEW_EER]
	stx	%l0, [%i3 + OFF_A_TAG_CMP]
	stx	%l1, [%i3 + OFF_A_TAG_MASK]
	stx	%l2, [%i3 + OFF_A_WAY_INCR]
	stx	%l3, [%i3 + OFF_A_FLUSH_B]
	stx	%l4, [%i3 + OFF_A_ASI_ADDR]
	stx	%l5, [%i3 + OFF_A_OLD_EER]
	stx	%o1, [%i3 + OFF_A_PA_MASK]
	stx	%i4, [%i3 + OFF_A_LOOP_CNT]
	stx	%g0, [%i3 + OFF_A_LOOPS]
	add	%i3, OFF_A_TAGS, %i5
#endif
	rdpr	%pstate, %l6			! get/save %pstate
#ifdef	DEBUG_WR_MEMORY
	stx	%l6, [%i3 + OFF_A_PSTATE]
#endif
	andn	%l6, PSTATE_IE, %o0		! and disable interrupts
	wrpr	%o0, %g0, %pstate		! .

	call	cpu_flush_ecache		! flush the entire e$ after
	nop					! disabling interrupts

	mov	%g0, %i2			! return value = 0 (no error)

	ba,pt	%icc, 5f			! branch to code that fills
	lduba	[%i0]ASI_MEM, %o0		! prefetch buf and
						! load data to get it into e$

	/*
	 * Code from here to when we restore E$ error reg must
	 * fit on one cache line (16 instrs) to avoid unwanted
	 * side-effects.
	 */
	.align	64
1:
	stuba	%o0, [%i0]ASI_MEM		! write data (with bad ecc)
	membar	#Sync

	/*
	 * Try to find which way the data ended up in.
	 */
#ifdef	DEBUG_WR_MEMORY
	stx	%i4, [%i3 + OFF_A_LOOP_CNT]
#endif
2:
#ifdef	DEBUG_WR_MEMORY
	ldx	[%i3 + OFF_A_LOOPS], %o4
	inc	%o4
	stx	%o4, [%i3 + OFF_A_LOOPS]
#endif
	ldxa	[%l4]ASI_EC_DIAG, %o0		! read tag for next way
#ifdef	DEBUG_WR_MEMORY
	stx	%o0, [%i5]
	add	%i5, 8, %i5
#endif
	and	%o0, %l1, %o0			! format tag for comparison
#ifdef	DEBUG_WR_MEMORY
	stx	%o0, [%i5]
	add	%i5, 8, %i5
#endif
	cmp	%l0, %o0			! is the data in this way?
	bz	%icc, 3f			! .
	subcc	%i4, 1, %i4			! decrement loop counter
	bz,a	4f				! if not found in any way then
	mov	1, %i2				! return value = 1 (error)
	ba	2b
	add	%l4, %l2, %l4			! generate next asi addr
						! for tag read

	/*
	 * Found it, now flush it.
	 */
3:
	or	%l4, %l3, %l4			! add disp flush bit to addr
#ifdef	DEBUG_WR_MEMORY
	stx	%l4, [ %i3 + OFF_A_FLUSH]
#endif

	stxa	%i1, [%g0]ASI_ESTATE_ERR	! enable bad ecc generation
	membar	#Sync

	ldxa	[%l4]ASI_EC_DIAG, %g0		! flush the way
	membar	#Sync
4:
	stxa	%l5, [%g0]ASI_ESTATE_ERR	! restore E$ err reg
	membar	#Sync

#ifdef	DEBUG_WR_MEMORY
	stx	%i2, [%i3 + OFF_A_RET]
#endif
	wrpr	%l6, %pstate			! restore interrupt state
	ret
	restore	%g0, %i2, %o0
5:
	/*
	 * Fill up the prefetch buffer.
	 */
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
	nop
	ba,pt	%icc, 1b
	nop

	SET_SIZE(ja_wr_memory)

#endif	/* lint */

/*
 * This routine is used to corrupt the ecache tags
 * which are protected by parity.
 *
 * Register usage:
 *
 *	%i0 - 32 byte aligned physical address to corrupt
 *	%i1 - corruption (xor) pattern
 *	%i2 - debug pointer to store information
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
 *	%o1 - tmp
 *	%o2 - tmp
 * 	%o3 - tmp
 *
 */
#if defined(lint)
/* ARGSUSED */
void
ja_wr_ecache_tag(uint64_t paddr, uint64_t xorpat, uint64_t *debug)
{}

#else	/* lint */

#ifdef	DEBUG_WR_ECACHE_TAG
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
	ENTRY(ja_wr_ecache_tag)

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
	set	0x3ffff, %o1			! mask for paddr
	and	%i0, %o1, %l4			! %l4 - asi addr for way 0 tag

	set	4, %i4				! %i4 - way loop counter below

	srlx	%i0, 18, %l0			! %l0 - tag compare value for pa
	set	0x1fffff, %l1			! mask for tag

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
#ifdef	DEBUG_WR_ECACHE_TAG
	stx	%i4, [%i2 + OFF_C_LOOP_CNT]
#endif
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
	ret					! and
	restore	%g0, %l3, %o0			! return status
	SET_SIZE(ja_wr_ecache_tag)

#endif	/* lint */

/*
 * This routine is used to corrupt the ecache data which is
 * protected by ECC.
 *
 * Register usage:
 *
 *	%i0 - 32 byte aligned physical address to corrupt
 *	%i1 - corruption (xor) pattern
 *	%i2 - staging register selection (later converted to offset)
 *	%i3 - ecache set size (used to generate address mask)
 *	%i4 - debug pointer to store information
 *	%i5 - also used as debug pointer for storing info
 *
 *	%l0 - tag compare value for e$ tag based on paddr
 *	%l1 - mask for tag before comparing
 *	%l2 - way increment for asi addr
 *	%l3 - return value
 *	%l4 - asi address for e$ tag read
 *	%l5 - way loop counter
 *	%l6 - saved pstate
 *
 *	%o1 - tmp
 *	%o2 - tmp
 * 	%o3 - tmp
 *
 */
#if defined(lint)
/* ARGSUSED */
int
ja_wr_ecache(uint64_t paddr, uint64_t xorpat, uint32_t reg_select,
		uint64_t ec_set_size, uint64_t *debug)
{ return (0); }

#else	/* lint */

#ifdef	DEBUG_WR_ECACHE
#define	OFF_B_PADDR	0x00	/* paddr */
#define	OFF_B_XORPAT	0x08	/* xor pattern */
#define	OFF_B_REG_SEL	0x10	/* register select */
#define	OFF_B_DEBUGP	0x18	/* debug pointer */
#define	OFF_B_SET_SIZE	0x20	/* ecache set size */
#define	OFF_B_TAG_CMP	0x28	/* tag compare */
#define	OFF_B_TAG_MASK	0x30	/* tag mask */
#define	OFF_B_WAY_INCR	0x38	/* way incr */
#define	OFF_B_UNUSED_40	0x40	/* unused */
#define	OFF_B_ASI_ADDR	0x48	/* asi addr for tag read */
#define	OFF_B_PSTATE	0x50	/* saved %pstate */
#define	OFF_B_RET	0x58	/* return value */
#define	OFF_B_PA_MASK	0x60	/* paddr mask */
#define	OFF_B_LOOP_CNT	0x68	/* loop counter */
#define	OFF_B_LOOPS	0x70	/* # of loops */
#define	OFF_B_UNUSED_78	0x78	/* unused */
#define	OFF_B_TAGS	0x80	/* tag for way 0 */
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
	ENTRY(ja_wr_ecache)
	save	%sp, -SA(MINFRAME), %sp

#ifdef	DEBUG_WR_ECACHE
	stx	%i0, [%i4 + OFF_B_PADDR]
	stx	%i1, [%i4 + OFF_B_XORPAT]
	stx	%i2, [%i4 + OFF_B_REG_SEL]
	stx	%i3, [%i4 + OFF_B_SET_SIZE]
	stx	%i4, [%i4 + OFF_B_DEBUGP]
#endif

	sll	%i2, 3, %i2			! %i2 - staging reg offset

	/*
	 * Generate tag asi addresses to use below.
	 */
	set	1, %l2				! %l2 - way incr for asi addr
	sllx	%l2, JA_EC_IDX_WAY_SHIFT, %l2	! .
	subcc	%i3, 1, %o1			! mask for paddr
	and	%i0, %o1, %l4			! %l4 - asi addr for way 0 tag

	set	4, %l5				! %l5 - way loop counter below

	srlx	%i0, 18, %l0			! %l0 - tag compare value for pa
	set	0x1ffffff, %l1			! mask for tag

	ldxa	[%i0]ASI_MEM, %g0		! read data to process faults

	/*
	 * Save variables in debug location.
	 */
#ifdef	DEBUG_WR_ECACHE
	stx	%l0, [%i4 + OFF_B_TAG_CMP]
	stx	%l1, [%i4 + OFF_B_TAG_MASK]
	stx	%l2, [%i4 + OFF_B_WAY_INCR]
	stx	%l4, [%i4 + OFF_B_ASI_ADDR]
	stx	%l6, [%i4 + OFF_B_PSTATE]
	stx	%o1, [%i4 + OFF_B_PA_MASK]
	stx	%l5, [%i4 + OFF_B_LOOP_CNT]
	stx	%g0, [%i4 + OFF_B_LOOPS]
	add	%i4, OFF_B_TAGS, %i5
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
#ifdef	DEBUG_WR_ECACHE
	stx	%l5, [%i4 + OFF_B_LOOP_CNT]
#endif
2:
#ifdef	DEBUG_WR_ECACHE
	ldx	[%i4 + OFF_B_LOOPS], %o4
	inc	%o4
	stx	%o4, [%i4 + OFF_B_LOOPS]
#endif
	ldxa	[%l4]ASI_EC_DIAG, %o0		! read tag for next way
#ifdef	DEBUG_WR_ECACHE
	stx	%o0, [%i5]
	add	%i5, 8, %i5
#endif
	and	%o0, %l1, %o0			! format tag for comparison
#ifdef	DEBUG_WR_ECACHE
	stx	%o0, [%i5]
	add	%i5, 8, %i5
#endif
	cmp	%l0, %o0			! is the data in this way?
	bz	%icc, 3f			! .
	subcc	%l5, 1, %l5			! decrement loop counter
	bz,a	4f				! if not found in any way then
	mov	1, %l3				! return value = 1 (error)
	ba	2b
	add	%l4, %l2, %l4			! generate next asi addr
						! for tag read
	/*
	 * Found it, now corrupt it.
	 */
3:
	ldxa	[%l4]ASI_EC_R, %g0		! fill staging regs from e$
	ldxa	[%i2]ASI_EC_DATA, %o2		! get data to corrupt
	xor	%o2, %i1, %o2			! corrupt data
	stxa	%o2, [%i2]ASI_EC_DATA		! .
	stxa	%g0, [%l4]ASI_EC_W		! write staging regs back to e$
	membar	#Sync

4:
	wrpr	%l6, %pstate			! restore saved pstate
	ret					! return value = 0 (ok)
	restore	%g0, %l3, %o0			! .
	SET_SIZE(ja_wr_ecache)

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
ja_wr_ephys(uint64_t offset_aligned, int reg_offset, uint64_t xorpat,
	uint64_t data)
{}

#else	/* lint */


	.align	128

	ENTRY(ja_wr_ephys)

	/*
	 * Since the way bits in the asi address are not contiguous
	 * with the rest of the addr, they must be isolated in the
	 * specified offset and moved to the proper location.
	 */
	set	0xc0000, %o4			! mask for way bits in offset
	and	%o0, %o4, %o5			! isolate the bits and
	sllx	%o5, 14, %o5			! move them to new location

	andn	%o0, %o4, %o0			! clear bits 19,18 in offset
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
	SET_SIZE(ja_wr_ephys)

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
ja_wr_etphys(uint64_t paddr, uint64_t xorpat, uint64_t data)
{}

#else	/* lint */

	ENTRY(ja_wr_etphys)

	/*
	 * Since the way bits in the adi address are not contiguous
	 * with the rest of the addr, they must be isolated in the
	 * specified offset and moved to the proper location.
	 */
	set	0xc0000, %o3			! mask for way bits in offset
	and	%o0, %o3, %o4			! isolate the bits and
	sllx	%o4, 14, %o4			! move them to new location

	andn	%o0, %o3, %o0			! clear bits 19,18 in offset
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
	SET_SIZE(ja_wr_etphys)

#endif	/* lint */

/*
 * This is a special trap table entry that gets installed for
 * certain tests. It gets executed when the error goes off
 * and restores the EER so additional errors do not happen.
 */
#if defined(lint)
void
ja_tt63()
{}

#else	/* lint */

	.global	ce_err
	.align	32

	ENTRY(ja_tt63)
	ldxa	[%g0]ASI_ESTATE_ERR, %g1	! clear FME and FSP
	andn	%g1, 0x420, %g1			! .
	stxa	%g1, [%g0]ASI_ESTATE_ERR	! .
	membar	#Sync				! required
	set	ce_err, %g1
	jmpl	%g1, %g0
	nop
	SET_SIZE(ja_tt63)

#endif	/* lint */

/*
 * This routine causes a writeback parity error (WBP).
 * This is done by modifying an entry in the E$, updating the
 * Error Enable register to force bad parity and then flushing
 * the E$ line to cause a write-back.
 *
 * Register usage:
 *
 *	%i0 - physical memory address to write to
 *	%i1 - parity to write with data (also used for e$ err en reg)
 *	%i2 - debug pointer to store information
 *	%i3 - return value
 *	%i4 - loop counter
 *	%i5 - also used as debug pointer for storing info
 *
 *	%l0 - tag compare value for e$ tag based on paddr
 *	%l1 - mask for tag before comparing
 *	%l2 - way increment for asi addr
 *	%l3 - disp flush bit
 *	%l4 - asi address for e$ tag read
 *	%l5 - saved E$ error reg
 *	%l6 - saved pstate
 *
 *	%o0 - tmp
 *	%o1 - tmp
 */
#if defined(lint)

/* ARGSUSED */
void
ja_wbp(uint64_t paddr, uint_t parity, uint64_t *debug)
{}

#else	/* lint */

#ifdef	DEBUG_WBP
/*
 * Offsets to store variables at.
 */
#define	OFF_D_PADDR	0x00	/* paddr */
#define	OFF_D_PARITY	0x08	/* parity pattern in ja_wbp() */
#define	OFF_D_DEBUGP	0x10	/* debug pointer */
#define	OFF_D_NEW_EER	0x18	/* new e$ err enable reg */
#define	OFF_D_TAG_CMP	0x20	/* tag compare */
#define	OFF_D_TAG_MASK	0x28	/* tag mask */
#define	OFF_D_WAY_INCR	0x30	/* way incr */
#define	OFF_D_FLUSH_B	0x38	/* disp flush bit */
#define	OFF_D_ASI_ADDR	0x40	/* asi addr for tag read */
#define	OFF_D_OLD_EER	0x48	/* saved e$ err en reg */
#define	OFF_D_PSTATE	0x50	/* saved %pstate */
#define	OFF_D_RET	0x58	/* return value */
#define	OFF_D_PA_MASK	0x60	/* paddr mask */
#define	OFF_D_LOOP_CNT	0x68	/* loop counter */
#define	OFF_D_LOOPS	0x70	/* # of loops */
#define	OFF_D_FLUSH	0x78	/* asi address for flush */
#define	OFF_D_TAGS	0x80	/* tag for way 0 */
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
	ENTRY(ja_wbp)
	save    %sp, -SA(MINFRAME), %sp		! get a new window

#ifdef	DEBUG_WBP
	stx	%i0, [%i2 + OFF_D_PADDR]
	stx	%i1, [%i2 + OFF_D_PARITY]
	stx	%i2, [%i2 + OFF_D_DEBUGP]
#endif
	/*
	 * Generate new/diag e$ error enable reg to use below.
	 */
	ldxa	[%g0]ASI_ESTATE_ERR, %l5	! %l5 = saved E$ err reg
	set	EN_REG_FPD, %o0			! clear FPD field
	andn	%l5, %o0, %o0			! .
	sll	%i1, EN_REG_FPD_SHIFT, %i1	! shift parity into position
	or	%i1, %o0, %i1			! and set FPD field
	set	EN_REG_FSP, %o0			! set FSP bit
	or	%i1, %o0, %i1			! %i1 - new e$ err enable reg

	/*
	 * Generate tag asi addresses to use below.
	 */
	set	JA_EC_IDX_DISP_FLUSH, %l3	! %l3 = displacement flush bit
	set	1, %l2				! %l2 = way incr for asi addr
	sllx	%l2, JA_EC_IDX_WAY_SHIFT, %l2	! .
	set	0x3ffff, %o1			! mask for paddr
	and	%i0, %o1, %l4			! %l4 = asi addr for way 0 tag

	set	4, %i4				! %i4 = way loop counter below

	srlx	%i0, 18, %l0			! %l0 = tag compare value for pa
	set	0x1fffff, %l1			! %l1 = mask for tag

	/*
	 * Save variables in debug location.
	 */
#ifdef	DEBUG_WBP
	stx	%i1, [%i2 + OFF_D_NEW_EER]
	stx	%l0, [%i2 + OFF_D_TAG_CMP]
	stx	%l1, [%i2 + OFF_D_TAG_MASK]
	stx	%l2, [%i2 + OFF_D_WAY_INCR]
	stx	%l3, [%i2 + OFF_D_FLUSH_B]
	stx	%l4, [%i2 + OFF_D_ASI_ADDR]
	stx	%l5, [%i2 + OFF_D_OLD_EER]
	stx	%o1, [%i2 + OFF_D_PA_MASK]
	stx	%i4, [%i2 + OFF_D_LOOP_CNT]
	stx	%g0, [%i2 + OFF_D_LOOPS]
	add	%i2, OFF_D_TAGS, %i5
#endif
	rdpr	%pstate, %l6			! get/save %pstate
#ifdef	DEBUG_WBP
	stx	%l6, [%i2 + OFF_D_PSTATE]
#endif
	andn	%l6, PSTATE_IE, %o0		! and disable interrupts
	wrpr	%o0, %g0, %pstate		! .

	call	cpu_flush_ecache		! flush the entire e$ after
	nop					! disabling interrupts

	mov	%g0, %i3			! return value = 0 (no error)

	ba,pt	%icc, 5f			! branch to code that fills
	lduba	[%i0]ASI_MEM, %o0		! prefetch buf and
						! load data to get it into e$

	/*
	 * Code from here to when we restore E$ error reg must
	 * fit on one cache line (16 instrs) to avoid unwanted
	 * side-effects.
	 */
	.align	64
1:
	stuba	%o0, [%i0]ASI_MEM		! write data (with bad parity)
	membar	#Sync

	/*
	 * Try to find which way the data ended up in.
	 */
#ifdef	DEBUG_WBP
	stx	%i4, [%i2 + OFF_D_LOOP_CNT]
#endif
2:
#ifdef	DEBUG_WBP
	ldx	[%i2 + OFF_D_LOOPS], %o4
	inc	%o4
	stx	%o4, [%i2 + OFF_D_LOOPS]
#endif
	ldxa	[%l4]ASI_EC_DIAG, %o0		! read tag for next way
#ifdef	DEBUG_WBP
	stx	%o0, [%i5]
	add	%i5, 8, %i5
#endif
	and	%o0, %l1, %o0			! format tag for comparison
#ifdef	DEBUG_WBP
	stx	%o0, [%i5]
	add	%i5, 8, %i5
#endif
	cmp	%l0, %o0			! is the data in this way?
	bz	%icc, 3f			! .
	subcc	%i4, 1, %i4			! decrement loop counter
	bz,a	4f				! if not found in any way then
	mov	1, %i3				! return value = 1 (error)
	ba	2b
	add	%l4, %l2, %l4			! generate next asi addr
						! for tag read

	/*
	 * Found it, now enable bad parity generation
	 * and flush it to cause the WBP.
	 */
3:
	or	%l4, %l3, %l4			! add disp flush bit to addr
#ifdef	DEBUG_WBP
	stx	%l4, [ %i2 + OFF_D_FLUSH]
#endif

	stxa	%i1, [%g0]ASI_ESTATE_ERR	! enable bad parity generation

/*
 * XXX	The following instruction:
 *	Causes a JEIS if uncommented out.
 *	Causes only an ISAP if commented out.
 *	Sometimes causes an UMS?
 */
	ldxa	[%l4]ASI_EC_DIAG, %g0		! flush the way
	membar	#Sync
4:
	stxa	%l5, [%g0]ASI_ESTATE_ERR	! restore E$ err reg

#ifdef	DEBUG_WBP
	stx	%i3, [%i2 + OFF_D_RET]
#endif
	wrpr	%l6, %pstate			! restore interrupt state
	ret
	restore	%g0, %i3, %o0
5:
	/*
	 * Fill up the prefetch buffer.
	 */
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
	nop
	ba,pt	%icc, 1b
	nop

	SET_SIZE(ja_wbp)

#endif	/* lint */

/*
 * This routine causes a JBus address packet parity error.
 * This is done by updating the Error Enable register to force
 * the parity value passed in and then reading from the physical
 * address also passed in.  The higher level routine will have
 * calculated the parity based on the address such that an
 * address parity error should occur.
 *
 * Register usage:
 *
 *	%i0 - physical memory address to read
 *	%i1 - parity to use with address (also used for e$ err en reg)
 *	%i2 - debug pointer to store information
 *	%i3 - return value
 *	%i5 - also used as debug pointer for storing info
 *
 *	%l1 - mask for tag before comparing
 *	%l2 - way increment for asi addr
 *	%l5 - saved E$ error reg
 *	%l6 - saved pstate
 *
 *	%o0 - tmp
 *	%o1 - tmp
 */
#if defined(lint)
/* ARGSUSED */
void
ja_isap(uint64_t paddr, uint_t parity, uint64_t *debug)
{}

#else	/* lint */

	.align	256
	ENTRY(ja_isap)
	save    %sp, -SA(MINFRAME), %sp		! get a new window

	/*
	 * Generate new/diag e$ error enable reg to use below.
	 */
	ldxa	[%g0]ASI_ESTATE_ERR, %l5	! %l5 = saved E$ err reg
	set	EN_REG_FPD, %o0			! clear FPD field
	andn	%l5, %o0, %o0			! .
	sll	%i1, EN_REG_FPD_SHIFT, %i1	! shift parity into position
	or	%i1, %o0, %i1			! and set FPD field
	set	EN_REG_FSP, %o0			! set FSP bit
	or	%i1, %o0, %i1			! %i1 - new e$ err enable reg

	rdpr	%pstate, %l6			! get/save %pstate
	andn	%l6, PSTATE_IE, %o0		! and disable interrupts
	wrpr	%o0, %g0, %pstate		! .

	call	cpu_flush_ecache		! flush the entire e$ after
	nop					! disabling interrupts

	mov	%g0, %i3			! return value = 0 (no error)

	ba,pt	%icc, 5f			! branch to code that fills
	nop

	/*
	 * Code from here to when we restore E$ error reg must
	 * fit on one cache line (16 instrs) to avoid unwanted
	 * side-effects.
	 */
	.align	64
1:
	stxa	%i1, [%g0]ASI_ESTATE_ERR	! enable bad parity generation

	lduba	[%i0]ASI_MEM, %o0		! read data
	membar	#Sync				! and cause an ISAP
4:
	stxa	%l5, [%g0]ASI_ESTATE_ERR	! restore E$ err reg

	wrpr	%l6, %pstate			! restore interrupt state
	ret
	restore	%g0, %i3, %o0
5:
	/*
	 * Fill up the prefetch buffer.
	 */
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
	nop
	ba,pt	%icc, 1b
	nop

	SET_SIZE(ja_isap)

#endif	/* lint */
