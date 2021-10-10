/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#if defined(lint)

#include <sys/memtestio.h>
#include <sys/memtestio_u.h>
#include <sys/memtestio_ch.h>
#include <sys/memtest_u.h>
#include <sys/memtest_ch.h>

#else	/* lint */

#include <sys/memtest_u_asm.h>

#endif	/* lint */

/*
 * This routine updates pre-existing data in memory at the specified
 * physical address with the calculated ECC that is passed in.
 *
 * Register usage:
 *
 *	%i0 = memory physical address to write
 *	%i1 = ecache size
 *	%i2 = ecc to write along with data
 *
 *	%l2 = flush offset
 *	%l3 = flush base
 *	%l4 = data
 *	%l5 = saved e$ control reg
 *	%l6 = saved pstate reg
 *
 *	%o0 = temp
 *	%o1 = temp
 *
 */
#if defined(lint)

/*ARGSUSED*/
int
ch_wr_memory(uint64_t paddr, uint_t ec_size, uint_t ecc)
{
	return (0);
}

#else	/* lint */

	/*
	 * This code must not cross a page boundary in order to
	 * avoid unwanted page faults.
	 */
	.align	256

	ENTRY(ch_wr_memory)
	save    %sp, -SA(MINFRAME), %sp		! get a new window

	andn	%i0, 0x7, %i0			! align pa to 8 byte boundary

	xor	%i0, %i1, %l2			! calculate alias address
	add	%i1, %i1, %l3			! 2 * ecachesize in case
						! addr == ecache_flushaddr
	sub	%l3, 1, %l3			! subtract 1 to make mask

	and	%l2, %l3, %l2			! generate flush offset

	set	ecache_flushaddr, %l3		! get flush base addr
	ldx	[%l3], %l3			! .

	ldxa	[%g0]ASI_ESTATE_ERR, %l5	! get/save e$ err enable reg

	set	EN_REG_FDECC, %o0		! clear FDECC field
	andn	%l5, %o0, %o1			! .
	sll	%i2, 4, %i2			! shift ecc into position
	or	%i2, %o1, %i2			! and set FDECC field
	set	EN_REG_FMD, %o0			! set FMD bit
	or	%i2, %o0, %i2			! .

	rdpr	%pstate, %l6			! save current state and
	andn	%l6, PSTATE_IE,  %o0		! disable interrupts
	wrpr	%o0, %g0, %pstate		! .

	call	cpu_flush_ecache		! flush the entire e$ after
	nop					! disabling interrupts

	ba,pt	%icc, 2f			! branch to aligned code
	lduba	[%i0]ASI_MEM, %l4		! get the value so we can
						! write it back with our ECC

	/*
	 * The following instructions (up until error generation is disabled)
	 * should be on the same ecache line in order to prevent misses and
	 * unwanted side effects.
	 */
	.align	64
1:
	stxa	%i2, [%g0]ASI_ESTATE_ERR	! write e$ control register
	membar	#Sync				! .
	stuba	%l4, [%i0]ASI_MEM		! store the value with our ECC
	membar	#Sync				! needed in between stxa/ldxa
	lduba	[%l2 + %l3]ASI_MEM, %g0		! displace data from e$ and
	membar	#Sync				! force data+ecc to memory

	stxa	%l5, [%g0]ASI_ESTATE_ERR	! restore e$ control register
	membar	#Sync				! .
	wrpr	%l6, %pstate			! restore interrupt state

	ret					! return value 0
	restore %g0, %g0, %o0
2:
	nop					! fill up the prefetch buffer
	nop					! 8 nops should be enough
	nop					! .
	nop					! .
	nop					! .
	nop					! .
	nop					! .
	nop					! .
	ba,pt   %icc, 1b
	nop
	SET_SIZE(ch_wr_memory)

#endif	/* lint */

/*
 * This routine updates pre-existing mtags in memory at the specified
 * physical address with the calculated Mtag ECC that is passed in.
 *
 * Register usage:
 *
 *	%i0 = 8 byte aligned mtag physical address to write
 *	%i1 = ecache size
 *	%i2 = ecc to write along with data
 *
 *	%l2 = flush offset
 *	%l3 = flush base
 *	%l4 = data
 *	%l5 = saved e$ control reg
 *	%l6 = saved pstate reg
 *
 *	%o0 = temp
 *	%o1 = temp
 *
 */
#if defined(lint)

/*ARGSUSED*/
void
ch_wr_mtag(uint64_t paddr, uint_t ec_size, uint_t ecc)
{}

#else	/* lint */

	/*
	 * This code must not cross a page boundary in order to
	 * avoid unwanted page faults.
	 */
	.align	256

	ENTRY(ch_wr_mtag)
	save    %sp, -SA(MINFRAME), %sp		! get a new window

	andn	%i0, 0x7, %i0			! align pa to 8 byte boundary

	xor	%i0, %i1, %l2			! calculate alias address
	add	%i1, %i1, %l3			! 2 * ecachesize in case
						! addr == ecache_flushaddr
	sub	%l3, 1, %l3			! subtract 1 to make mask

	and	%l2, %l3, %l2			! generate flush offset

	set	ecache_flushaddr, %l3		! get flush base addr
	ldx	[%l3], %l3			! .

	ldxa	[%g0]ASI_ESTATE_ERR, %l5	! get/save e$ err enable reg

	set	EN_REG_FMECC, %o0		! clear FMECC field
	andn	%l5, %o0, %o1			! .
	sll	%i2, 14, %i2			! shift ecc into position
	or	%i2, %o1, %i2			! and set FMECC field
	set	EN_REG_FMT, %o0			! set FMT bit
	or	%i2, %o0, %i2			! .

	rdpr	%pstate, %l6			! save current state and
	andn	%l6, PSTATE_IE,  %o0		! disable interrupts
	wrpr	%o0, %g0, %pstate		! .

	call	cpu_flush_ecache		! flush the entire e$ after
	nop					! disabling interrupts

	ba,pt	%icc, 2f			! branch to aligned code
	lduba	[%i0]ASI_MEM, %l4		! get the value so we can
						! write it back with our ECC

	/*
	 * The following instructions (up until error generation is disabled)
	 * should be on the same ecache line in order to prevent misses and
	 * unwanted side effects.
	 */
	.align	64
1:
	stxa	%i2, [%g0]ASI_ESTATE_ERR	! write e$ control register
	membar	#Sync				! .
	stuba	%l4, [%i0]ASI_MEM		! store the value with our ECC
	membar	#Sync				! needed in between stxa/ldxa
	lduba	[%l2 + %l3]ASI_MEM, %g0		! displace data from e$ and
	membar	#Sync				! force data+ecc to memory

	stxa	%l5, [%g0]ASI_ESTATE_ERR	! restore e$ control register
	membar	#Sync				! .
	wrpr	%l6, %pstate			! restore interrupt state

	ret
	restore
2:
	nop					! fill up the prefetch buffer
	nop					! 8 nops should be enough
	nop					! .
	nop					! .
	nop					! .
	nop					! .
	nop					! .
	nop					! .
	ba,pt   %icc, 1b
	nop
	SET_SIZE(ch_wr_mtag)

#endif	/* lint */


/*
 * This routine reads 32 bytes of data plus the corresponding ECC check
 * bits from the E$ using diagnostic ASI access into a data structure.
 *
 * Register usage:
 *
 *	%o0 = 32 byte aligned ecache offset to read
 *	%o1 = struct address to save ecache data to
 *	%o2 = offset into staging regs
 *	%o3 = staging reg data
 *	%o4 = temp
 *	%o5 = saved pstate reg
 */

#if defined(lint)

/*ARGSUSED*/
void
ch_rd_ecache(uint64_t offset, ch_ec_t *regs)
{}

#else	/* lint */

	/*
	 * This code must not cross a page boundary to avoid unwanted
	 * page faults.
	 */
	.align	64

	ENTRY(ch_rd_ecache)
	rdpr	%pstate, %o5			! disable interrupts
	andn	%o5, PSTATE_IE, %o4		! .
	wrpr	%o4, 0, %pstate			! .

	ldxa	[%o0]ASI_EC_R, %g0		! read e$ into staging regs

	clr	%o2
1:
	ldxa	[%o2]ASI_EC_DATA, %o3		! save staging regs data
	stx	%o3, [%o1 + %o2] 		! into data structure
	add	%o2, 8, %o2			! .
	cmp	%o2, 0x28			! .
	bl	1b				! .
	nop					! .

	ldxa	[%o0]ASI_EC_DIAG, %o3		! read tag for specified addr
	stx	%o3, [%o1 + %o2]		! and save into struct

	retl
	wrpr	%o5, %pstate			! turn interrupts back on
	SET_SIZE(ch_rd_ecache)

#endif	/* lint */

/*
 * This routine returns 8 bytes of data from the D$ offset.
 *
 * Register usage:
 *
 *	%o0 = 8 byte aligned dcache offset to read
 *	%o4 = temp
 *	%o5 = saved pstate reg
 */

#if defined(lint)

/*ARGSUSED*/
uint64_t
ch_rd_dcache(uint64_t offset)
{
	return (0);
}

#else	/* lint */

	/*
	 * This code must not cross a page boundary to avoid unwanted
	 * page faults.
	 */
	.align	64

	ENTRY(ch_rd_dcache)
	rdpr	%pstate, %o5			! disable interrupts
	andn	%o5, PSTATE_IE, %o4		! .
	wrpr	%o4, 0, %pstate			! .

	membar	#Sync				! required before ASI_DC_DATA
	ldxa	[%o0]ASI_DC_DATA, %o0		! read d$
	membar	#Sync				! required after  ASI_DC_DATA

	retl
	wrpr	%o5, %pstate			! turn interrupts back on
	SET_SIZE(ch_rd_dcache)

#endif	/* lint */

/*
 * This routine returns 8 bytes of data from the I$ offset.
 *
 * Register usage:
 *
 *	%o0 = 8 byte aligned icache offset to read
 */

#if defined(lint)

/*ARGSUSED*/
uint64_t
ch_rd_icache(uint64_t offset)
{
	return (0);
}

#else	/* lint */

	/*
	 * This code must not cross a page boundary to avoid unwanted
	 * page faults.
	 */
	.align	64

	ENTRY(ch_rd_icache)
	retl
	ldxa	[%o0]ASI_IC_DATA, %o0		! read i$
	SET_SIZE(ch_rd_icache)

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
 *	%o0 = 32 byte aligned ecache physical address to modify
 *	%o1 = staging reg offset (0-4) to plant an error at
 *	%o2 = xor bit pattern to use for corruption
 *	%o4 = temp
 *	%o5 = saved pstate reg
 */

#if defined(lint)

/*ARGSUSED*/
void
ch_wr_ecache(uint64_t paddr_aligned, int reg_offset, uint64_t xor_pat)
{}

#else	/* lint */

	/*
	 * This code must not cross a page boundary to avoid unwanted
	 * page faults.
	 */
	.align	128

	ENTRY(ch_wr_ecache)

	rdpr	%pstate, %o5			! disable interrupts
	andn	%o5, PSTATE_IE, %o4		! .
	wrpr	%o4, 0, %pstate			! .

	ba	1f				! branch to i$ aligned code
	sll	%o1, 3, %o1			! generate asi addr from offset

	/*
	 * The code between the ldxa and the stxa inclusive should all
	 * be on the same 32 bytes I$ line. Making it be the first 32
	 * bytes of an E$ line also guarantees that the data comes
	 * from memory only.
	 */
	.align 64

1:
	ldxa	[%o0]ASI_MEM, %g0		! read data to get it into e$
	ldxa	[%o0]ASI_EC_R, %g0		! read e$ into staging regs
	ldxa	[%o1]ASI_EC_DATA, %o4		! read data from staging reg
	xor	%o2, %o4, %o4			! corrupt data
	stxa	%o4, [%o1]ASI_EC_DATA		! write data back to staging reg
	stxa	%g0, [%o0]ASI_EC_W		! write e$ from staging regs
	membar	#Sync

	retl
	wrpr	%o5, %pstate			! turn the interrupts back on
	SET_SIZE(ch_wr_ecache)

#endif	/* lint */

/*
 * This routine is basically the same as ch_wr_ecache(), except that it does
 * not modify the cache line state by bringing the data into the cache.
 *
 * Register usage:
 *
 *	%o0 = 32 byte aligned ecache offset to modify
 *	%o1 = staging reg offset (0-4) to plant error at
 *	%o2 = xor bit pattern to use for corruption
 *	%o4 = temp
 *	%o5 = saved pstate reg
 */

#if defined(lint)

/*ARGSUSED*/
void
ch_wr_ephys(uint64_t offset_aligned, int reg_offset, uint64_t xor_pat)
{}

#else	/* lint */

	/*
	 * This code must not cross a page boundary to avoid unwanted
	 * page faults.
	 */
	.align	128

	ENTRY(ch_wr_ephys)

	rdpr	%pstate, %o5			! disable interrupts
	andn	%o5, PSTATE_IE, %o4		! .
	wrpr	%o4, 0, %pstate			! .

	ba	1f				! branch to i$ aligned code
	sll	%o1, 3, %o1			! generate asi addr from offset

	.align	64
1:
	ldxa	[%o0]ASI_EC_R, %g0		! read e$ into staging regs
	ldxa	[%o1]ASI_EC_DATA, %o4		! read data from staging reg
	xor	%o2, %o4, %o4			! corrupt data
	stxa	%o4, [%o1]ASI_EC_DATA		! write data back to staging reg
	stxa	%g0, [%o0]ASI_EC_W		! write e$ from staging regs
	membar	#Sync

	retl
	wrpr	%o5, %pstate			! turn the interrupts back on
	SET_SIZE(ch_wr_ephys)

#endif	/* lint */

/*
 * This routine is similar in function to ch_wr_ephys(), but instead
 * of injecting an error into the ecache data, it injects an error
 * into the ecache tags. Since cheetah does not have real protection
 * on the ecache tags, the error is simulated by flipping one or more
 * of the tag bits including potentially one of the state bits or by
 * overwriting the tag with the specified data.
 *
 * Register usage:
 *
 *	%o0 = 32 byte aligned ecache tag offset to modify
 *	%o1 = xor bit pattern to use for corruption
 *	%o2 = data to write to tags
 *	%o4 = temp
 *	%o5 = saved pstate reg
 */
#if defined(lint)

/*ARGSUSED*/
void
ch_wr_etphys(uint64_t offset_aligned, uint64_t xor_pat, uint64_t data)
{}

#else	/* lint */

	/*
	 * This code must not cross a page boundary to avoid unwanted
	 * page faults.
	 */
	.align	128

	ENTRY(ch_wr_etphys)

	rdpr	%pstate, %o5			! disable interrupts
	andn	%o5, PSTATE_IE, %o4		! .
	wrpr	%o4, 0, %pstate			! .

	cmp	%o1, %g0			! check if xorpat was specified
	be,pn	%xcc, 2f			! and branch if not
	nop

	ba	1f
	nop

	.align	64
1:
	ldxa	[%o0]ASI_EC_DIAG, %o2		! read tag for specified addr
	xor	%o1, %o2, %o2			! corrupt tag
2:
	stxa	%o2, [%o0]ASI_EC_DIAG		! write data to e$ tag
	membar	#Sync

	retl
	wrpr	%o5, %pstate			! turn the interrupts back on
	SET_SIZE(ch_wr_etphys)

#endif	/* lint */

/*
 * This routine plants an error into the dcache at the specified offset
 * without modifying the cache state. Since the cheetah dcache is not
 * really protected, we simulate corruption by flipping one or more of
 * the data bits or by overwriting the data with the specified data.
 *
 * Register usage:
 *
 *	%o0 = 8 byte aligned dcache offset to modify
 *	%o1 = xor pattern to use for corruption
 *	%o2 = data to put into the cache
 *	%o3 = temp
 *	%o4 = temp
 *	%o5 = saved pstate reg
 */
#if defined(lint)

/*ARGSUSED*/
void
ch_wr_dphys(uint64_t offset_aligned, uint64_t xorpat, uint64_t data)
{}

#else	/* lint */

	/*
	 * This code must not cross a page boundary to avoid unwanted
	 * page faults.
	 */
	.align	128

	ENTRY(ch_wr_dphys)

	rdpr	%pstate, %o5			! disable interrupts
	andn	%o5, PSTATE_IE, %o4		! .
	wrpr	%o4, 0, %pstate			! .

	cmp	%o1, %g0			! check if xorpat was specified
	be,pn	%xcc, 2f			! and branch if not
	nop

	ba	1f
	nop

	.align	64
1:
	membar	#Sync				! required before ASI_DC_DATA
	ldxa	[%o0]ASI_DC_DATA, %o3		! get existing d$ data
	membar	#Sync				! required after ASI_DC_DATA
	xor	%o1, %o3, %o2			! xor with specified pattern
2:
	stxa	%o2, [%o0]ASI_DC_DATA		! write data to d$
	membar	#Sync				! required after ASI_DC_DATA

	retl
	wrpr	%o5, %pstate			! turn the interrupts back on

	SET_SIZE(ch_wr_dphys)

#endif	/* lint */

/*
 * This routine plants an error into the icache at the specified offset
 * without modifying the cache state. Since the cheetah icache is not
 * really protected, we simulate corruption by flipping one or more of
 * the data bits or by overwriting the data with the specified data.
 *
 * Register usage:
 *
 *	%o0 = 8 byte aligned icache offset to modify
 *	%o1 = xor pattern to use for corruption
 *	%o2 = data to put into the cache
 *	%o3 = temp
 *	%o4 = temp
 *	%o5 = saved pstate reg
 */
#if defined(lint)

/*ARGSUSED*/
void
ch_wr_iphys(uint64_t offset_aligned, uint64_t xorpat, uint64_t data)
{}

#else	/* lint */

	/*
	 * This code must not cross a page boundary to avoid unwanted
	 * page faults.
	 */
	.align	64

	ENTRY(ch_wr_iphys)

	rdpr	%pstate, %o5			! disable interrupts
	andn	%o5, PSTATE_IE, %o4		! .
	wrpr	%o4, 0, %pstate			! .

	cmp	%o1, %g0			! check if xorpat was specified
	be,pn	%xcc, 2f			! and branch if not
	nop

	ba	1f
	nop

	.align	64
1:
	ldxa	[%o0]ASI_IC_DATA, %o3		! get existing i$ data
	xor	%o1, %o3, %o2			! xor with specified pattern

2:
	stxa	%o2, [%o0]ASI_IC_DATA		! write data to i$

	retl
	wrpr	%o5, %pstate			! turn the interrupts back on

	SET_SIZE(ch_wr_iphys)

#endif	/* lint */

/*
 * This routine invalidates a dcache entry.
 */
#if defined(lint)

/*ARGSUSED*/
int
ch_flush_dc(caddr_t vaddr)
{
	return (0);
}

#else	/* lint */

	ENTRY(ch_flush_dc)
	set	0x3fe0, %o3			! mask and align the addr
	and	%o0, %o3, %o0			! .

	mov	%g0, %o3			! init loop counter

	rdpr	%pstate, %o5			! save the processor state
	andn	%o5, PSTATE_IE, %o4		! and
	wrpr	%o4, 0, %pstate			! shut the interrupts off

	sll	%o3, 14, %o2
1:
	or	%o0, %o2, %o4
	stxa	%g0, [%o4]ASI_DC_TAG		! invalidate the dcache
	membar	#Sync				! needed in between stxa/ldxa
	ldxa	[%o4]ASI_DC_TAG, %g0		! copied from sf_flush_dc()

	inc	%o3				! loop for 4 way dcache
	cmp	%o3, 4				! .
	bl	1b				! .
	sll	%o3, 14, %o2

	wrpr	%o5, %pstate			! turn the interrupts back on
	retl
	mov	%g0, %o0			! return value 0

	SET_SIZE(ch_flush_dc)

#endif	/* lint */

/*
 * This routine invalidates the entire dcache.
 *
 * Register usage:
 *
 *	%o0 = size of dcache
 *	%o1 = size of a dcache line
 *	%o2 = used as an index
 *	%o4 = temp
 *	%o5 = saved pstate reg
 */
#if defined(lint)

/*ARGSUSED*/
int
ch_flushall_dc(int cache_size, int line_size)
{
	return (0);
}

#else	/* lint */

	.align	64

	ENTRY(ch_flushall_dc)
	rdpr	%pstate, %o5			! save the processor state
	andn	%o5, PSTATE_IE, %o4		! shut the interrupts off
	wrpr	%o4, 0, %pstate			! .

	sub	%o0, %o1, %o2
1:
	stxa	%g0, [%o2]ASI_DC_TAG
	membar	#Sync
	cmp	%g0, %o2
	bne,pt	%icc, 1b
	sub	%o2, %o1, %o2

	wrpr	%o5, %pstate			! turn the interrupts back on
	retl
	mov	%g0, %o0			! return value 0
	SET_SIZE(ch_flushall_dc)

#endif	/* lint */

/*
 * This routine invalidates the entire icache.
 * This routine and the following comment were copied from "cheetah_asm.s".
 *
 * Note that we cannot access ASI 0x67 (ASI_IC_TAG) with the Icache on,
 * because accesses to ASI 0x67 interfere with Icache coherency.  We
 * must make sure the Icache is off, then turn it back on after the entire
 * cache has been invalidated.  If the Icache is originally off, we'll just
 * clear the tags but not turn the Icache on.
 *
 * Register usage:
 *
 *	%o0 = size of icache
 *	%o1 = size of a icache line
 *	%o2 = saved dcu register
 *	%o3 = temp
 *	%o4 = temp
 *	%o5 = saved pstate reg
 */
#if defined(lint)

/*ARGSUSED*/
int
ch_flushall_ic(int cache_size, int line_size)
{
	return (0);
}

#else	/* lint */

	.align	64

	ENTRY(ch_flushall_ic)
	rdpr	%pstate, %o5			! save the processor state
	andn	%o5, PSTATE_IE, %o4		! shut the interrupts off
	wrpr	%o4, 0, %pstate			! .

	ldxa	[%g0]ASI_DCU, %o2		! %o2 = original dcu register
	andn	%o2, DCU_IC, %o3		! disable i$ while flushing
	stxa	%o3, [%g0]ASI_DCU		! .
	flush	%g0				! flush is required
	sllx	%o1, 1, %o1			! %o1 = linesize * 2
	sllx	%o0, 1, %o0			! %o0 = icache_size * 2
	sub	%o0, %o1, %o0
	or	%o0, CH_ICTAG_LOWER, %o0	! "write" tag
1:
	stxa	%g0, [%o0]ASI_IC_TAG
	membar	#Sync				! required
	cmp	%o0, CH_ICTAG_LOWER
	bne,pt	%icc, 1b
	sub	%o0, %o1, %o0
	stxa	%o2, [%g0]ASI_DCU		! restore dcu register
	flush	%g0				! flush is required

	wrpr	%o5, %pstate			! turn the interrupts back on
	retl
	mov	%g0, %o0			! return value 0
	SET_SIZE(ch_flushall_ic)

#endif	/* lint */

/*
 * This routine attempts to flush a W$ entry without also
 * flushing the corresponding E$ entry.
 *
 * Cheetah/Cheetah+ have a 2KB/4way associative W$ which uses a 5-bit
 * pseudo random LFSR replacement algorithm.  Therefore, 32 writes to
 * the same index are required to guarantee that a particular line is
 * pushed out.  Bits 8:6 are used to index the w$.
 * This routine also:
 * - Avoids writing to the same pa that we are trying to flush.
 * - Avoids crossing a page boundary.
 *
 * The algorithm below:
 *	- Keeps pa bits 8:0 constant to keep the w$ index constant.
 *	- Flips pa bit 12 and keeps it constant to avoid accessing the
 *	  pa that is being flushed.
 *	- Keeps bits 63:13 constant to avoid crosing a page boundary.
 *	- Does 32 read/writes while incrementing pa bits 11:9 (0x200).
 *
 * Register usage:
 *
 *	%o0 = physical address to flush from w$
 *	%o1 = offset added to paddr
 *	%o2 = mask
 *	%o3 = offset
 *	%o4 = temp
 *	%o5 = saved pstate reg
 */
#if defined(lint)

/*ARGSUSED*/
void
ch_flush_wc(uint64_t paddr)
{}

#else	/* lint */

	.align	64

	ENTRY(ch_flush_wc)

	set	0x3e00, %o1			! 32 x 0x200
	set	0x0e00, %o2			! mask for bits 11:9

	set	0x1000, %o4			! flip bit 12 to move pa to
	xor	%o0, %o4, %o0			! the other half of the page
	andn	%o0, %o2, %o0			! and clear pa bits 11:9

	rdpr	%pstate, %o5			! save the processor state
	andn	%o5, PSTATE_IE, %o4		! shut the interrupts off
	wrpr	%o4, 0, %pstate			! .

1:
	and	%o1, %o2, %o3			! mask all but offset bits 11:9
	lduba	[%o0+%o3]ASI_MEM, %o4		! read/write data at the index
	stuba	%o4, [%o0+%o3]ASI_MEM		! .

	subcc	%o1, 0x200, %o1			! generate the next offset that
	bge	1b				! maps to the same index
	nop

	retl
	wrpr	%o5, %pstate			! turn the interrupts back on

	SET_SIZE(ch_flush_wc)

#endif	/* lint */

/*
 * This routine is basically identical to ch_flush_wc() except
 * that it takes a virtual address instead of a physical one.
 */
#if defined(lint)

/*ARGSUSED*/
void
ch_flush_wc_va(caddr_t vaddr)
{}

#else	/* lint */

	.align	64

	ENTRY(ch_flush_wc_va)

	set	0x3e00, %o1			! 32 x 0x200
	set	0x0e00, %o2			! mask for bits 11:9

	set	0x1000, %o4			! flip bit 12 to move va to
	xor	%o0, %o4, %o0			! the other half of the page
	andn	%o0, %o2, %o0			! and clear pa bits 11:9

	rdpr	%pstate, %o5			! save the processor state
	andn	%o5, PSTATE_IE, %o4		! shut the interrupts off
	wrpr	%o4, 0, %pstate			! .

1:
	and	%o1, %o2, %o3			! mask all but offset bits 11:9
	ldub	[%o0+%o3], %o4			! read/write data at the index
	stub	%o4, [%o0+%o3]			! .

	subcc	%o1, 0x200, %o1			! generate the next offset that
	bge	1b				! maps to the same index
	nop

	retl
	wrpr	%o5, %pstate			! turn the interrupts back on

	SET_SIZE(ch_flush_wc_va)

#endif	/* lint */

/*
 * This routine reads the cheetah ecache control register.
 */
#if defined(lint)

/*ARGSUSED*/
uint64_t
ch_get_ecr(void)
{
	return (0);
}

#else	/* lint */

	ENTRY(ch_get_ecr)
	retl
	ldxa	[%g0]ASI_EC_CTRL, %o0
	SET_SIZE(ch_get_ecr)

#endif	/* lint */

/*
 * This routine writes the cheetah ecache control register.
 */
#if defined(lint)

/*ARGSUSED*/
void
ch_set_ecr(uint64_t data)
{}

#else	/* lint */

	ENTRY(ch_set_ecr)
	stxa	%o0, [%g0]ASI_EC_CTRL
	membar	#Sync

	retl
	nop
	SET_SIZE(ch_set_ecr)

#endif	/* lint */
