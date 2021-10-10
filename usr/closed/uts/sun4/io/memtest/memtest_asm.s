/*
 * Copyright (c) 2006, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/memtest_asm.h>

/*
 * This is the common assembly routine file for the CPU/Memory Error
 * Injector driver.
 */

/*
 * This routine is used to generate both data load errors and instruction
 * fetch errors.  Either an error is injected into this routine or it is
 * uses to access a location that has an error in it.  This routine rather
 * than C code is used to access data in order to avoid possibly having
 * loads optimized out.
 *
 * This code needs to be e$ line size aligned and contain at least e$ line
 * size identical double words (same constraint as for the data buffer),
 * since the wr_memory() routines forces the same ecc check bits for all
 * double words in an e$ line.  This is how commands like kice work without
 * also generating a UE.
 *
 * The reason there are more than 16 identical instructions here is to reduce
 * the chances of US-III setting the multiple errors bit (ME) in tests like
 * kiucc. The instruction error is injected into the second 64 byte block to
 * reduce the chances of ME getting set.
 *
 * The reason unique instructions are used (rather than "nop"s) is so one can
 * visually tell whether the correct routine was called/corrupted from the
 * data that gets dumped by the kernel.
 */
#if defined(lint)
/*ARGSUSED*/
void
memtest_asmld(caddr_t addr)
{}

/*ARGSUSED*/
void
memtest_asmld_quick(caddr_t addr)
{}
#else
	.align	128	
	ENTRY(memtest_asmld)
	clr	%o1				! = 0x92100000
	clr	%o1
	clr	%o1
	clr	%o1
	clr	%o1
	clr	%o1
	clr	%o1
	clr	%o1

	clr	%o1
	clr	%o1
	clr	%o1
	clr	%o1
	clr	%o1
	clr	%o1
	clr	%o1
	clr	%o1

	clr	%o1
	clr	%o1
	clr	%o1
	clr	%o1
	clr	%o1
	clr	%o1
	clr	%o1
	clr	%o1

	clr	%o1
	clr	%o1
	clr	%o1
	clr	%o1
	ENTRY(memtest_asmld_quick)
	ldub	[%o0], %g0
	membar	#Sync

	retl
	nop
	SET_SIZE(memtest_asmld_quick)
	SET_SIZE(memtest_asmld)
#endif  /* lint */

/*
 * This routine is similar to memtest_asmld_quick() except that it uses
 * a full 64-bit instruction to load the data.
 */
#if defined(lint)
/*ARGSUSED*/
void
memtest_asmldx_quick(caddr_t addr)
{}
#else
	.align	64
	ENTRY(memtest_asmldx_quick)
	ldx	[%o0], %g0
	membar	#Sync

	retl
	nop
	SET_SIZE(memtest_asmldx_quick)
#endif  /* lint */

/*
 * This routine is similar to memtest_asmld() except that it also stores
 * the specified data to the specified virtual address. It is
 * used in the copy-back test to invoke the error and set the
 * synchronization variable at the same time.
 */
#if defined(lint)
/*ARGSUSED*/
void
memtest_asmldst(caddr_t ld_addr, caddr_t st_addr, int st_data)
{}
#else	/* lint */
	.align	128
	ENTRY(memtest_asmldst)
	clr	%o3				! = 0x96100000
	clr	%o3
	clr	%o3
	clr	%o3
	clr	%o3
	clr	%o3
	clr	%o3
	clr	%o3

	clr	%o3
	clr	%o3
	clr	%o3
	clr	%o3
	clr	%o3
	clr	%o3
	clr	%o3
	clr	%o3

	clr	%o3
	clr	%o3
	clr	%o3
	clr	%o3
	clr	%o3
	clr	%o3
	clr	%o3
	clr	%o3

	clr	%o3
	clr	%o3
	clr	%o3
	clr	%o3
	clr	%o3
	ldub	[%o0], %g0
	retl
	st	%o2, [%o1]
	SET_SIZE(memtest_asmldst)
#endif /* lint */

/*
 * This routine is identical in function to asmld(), but is used to generate
 * errors at trap level 1.  Since this routine is executed at TL1 the
 * argument(s) are passed in via the global registers.
 *
 *	%g1 = addr
 */
#if defined(lint)
/*ARGSUSED*/
void
memtest_asmld_tl1(caddr_t addr)
{}
#else	/* lint */
	.align	128
	ENTRY_NP(memtest_asmld_tl1)
	clr %g5					! 0x8a100000
	clr %g5
	clr %g5
	clr %g5
	clr %g5
	clr %g5
	clr %g5
	clr %g5

	clr %g5
	clr %g5
	clr %g5
	clr %g5
	clr %g5
	clr %g5
	clr %g5
	clr %g5

	clr %g5
	clr %g5
	clr %g5
	clr %g5
	clr %g5
	clr %g5
	clr %g5
	clr %g5

	clr %g5
	clr %g5
	clr %g5
	clr %g5
	clr %g5
	clr %g5
	ldub	[%g1], %g4
	retry
	SET_SIZE(memtest_asmld_tl1)
#endif	/* lint */

/*
 * This routine does a store to a specified virtual address of the
 * specfied data at trap level 1.
 */
#if defined(lint)
/*ARGSUSED*/
void
memtest_asmst_tl1(caddr_t vaddr, uchar_t data)
{}
#else
	.align	64
	ENTRY(memtest_asmst_tl1)
	stub	%g2, [%g1]
	nop
	retry
	SET_SIZE(memtest_asmst_tl1)
#endif  /* lint */

/*
 * This routine does a block load from the specified virtual address.
 */
#if defined(lint)
/*ARGSUSED*/
void
memtest_blkld(caddr_t addr)
{}
#else
	ENTRY(memtest_blkld)
	andn	%o0, 0x3F, %o0			! align addr to 64 byte

	rd	%fprs, %o1			! enable fp
	wr	%g0, FPRS_FEF, %fprs		! .
	ldda	[%o0]ASI_BLK_P, %d0		! do the block load
	membar	#Sync

	retl
	wr	%o1, 0, %fprs			! restore fprs
	SET_SIZE(memtest_blkld)
#endif  /* lint */

/*
 * This routine is identical in function to blkld(), but is used to generate
 * errors at trap level 1.  Since this routine is executed at TL1 the
 * argument(s) are passed in via the global registers.
 *
 *	%g1 = addr
 */
#if defined(lint)
/*ARGSUSED*/
void
memtest_blkld_tl1(caddr_t addr)
{}
#else
	ENTRY(memtest_blkld_tl1)
	andn	%o0, 0x3F, %o0			! align addr to 64 byte
	rd	%fprs, %g2			! enable fp
	wr	%g0, FPRS_FEF, %fprs		! .

	ldda	[%g1]ASI_BLK_P, %d0		! do the block load
	membar	#Sync

	retl
	wr	%g2, 0, %fprs			! restore fprs
	SET_SIZE(memtest_blkld_tl1)
#endif  /* lint */

/*
 * This and the following routine are used to disable/enable
 * system interrupts. They are called before/after routines
 * which can only accomodate a very limited number of instructions
 * (such as the icache routines).
 */
#if defined(lint)
void
memtest_disable_intrs()
{}
#else
	ENTRY(memtest_disable_intrs)
	rdpr	%pstate, %o0
	andn	%o0, PSTATE_IE, %o1
	retl
	wrpr	%o1, 0, %pstate
	SET_SIZE(memtest_disable_intrs)
#endif  /* lint */

#if defined(lint)
void
memtest_enable_intrs()
{}
#else
	ENTRY(memtest_enable_intrs)
	rdpr	%pstate, %o0
	or	%o0, PSTATE_IE, %o1
	retl
	wrpr	%o1, 0, %pstate
	SET_SIZE(memtest_enable_intrs)
#endif  /* lint */

/*
 * get user space addr value from kernel using ASI USER SECONDARY access.
 */
#if defined(lint)
/*ARGSUSED*/
uint32_t
memtest_get_uval(uint64_t uva)
{
	return (0);
}
#else
        ENTRY(memtest_get_uval)
        lda    [%o0]ASI_USER, %o1 ! Switch to ASI user secondary
        mov     %o1, %o0          ! NOTE: ASI_AIUP will panic!!

        retl
        nop
        SET_SIZE(memtest_get_uval)
#endif  /* lint */

/*
 * This routine is used to allow us to corrupt %pc
 * relative instructions.
 */
#if defined(lint)
/*ARGSUSED*/
void
memtest_pcrel(void)
{}
#else
	.align	32
	ENTRY(memtest_pcrel)
	ba	1f		! %pc relative target instruction
	nop
1:
	retl
	nop
	SET_SIZE(memtest_pcrel)
#endif	/* lint */

/*
 * This routine performs a prefetch read access
 * to the specified address.
 */
#if defined(lint)
/* ARGSUSED */
void
memtest_prefetch_rd_access(caddr_t vaddr)
{}
#else
	ENTRY(memtest_prefetch_rd_access)
	prefetch	[%o0], #n_reads

	retl
	nop
	SET_SIZE(memtest_prefetch_rd_access)
#endif	/* lint */

/*
 * This routine performs a prefetch write access
 * to the specified address.
 */
#if defined(lint)
/* ARGSUSED */
void
memtest_prefetch_wr_access(caddr_t vaddr)
{}
#else
	ENTRY(memtest_prefetch_wr_access)
	prefetch	[%o0], #n_writes

	retl
	nop
	SET_SIZE(memtest_prefetch_wr_access)
#endif	/* lint */

/*
 * This routine does an 8bit peek using ASI bypass.
 */
#if defined(lint)
/* ARGSUSED */
uint8_t
peek_asi8(int asi, uint64_t addr)
{
	return (0);
}
#else
	ENTRY(peek_asi8)
	rd	%asi, %o3
	wr	%g0, %o0, %asi

	membar  #Sync
	lduba	[%o1]%asi, %o0
	membar  #Sync
	retl
	wr	%g0, %o3, %asi
	SET_SIZE(peek_asi8)
#endif	/* lint */

/*
 * This routine does an 16bit peek using ASI bypass.
 */
#if defined(lint)
/* ARGSUSED */
uint16_t
peek_asi16(int asi, uint64_t addr)
{
	return (0);
}
#else
	ENTRY(peek_asi16)
	rd	%asi, %o3
	wr	%g0, %o0, %asi

	membar  #Sync
	lduwa	[%o1]%asi, %o0
	membar  #Sync
	retl
	wr	%g0, %o3, %asi
	SET_SIZE(peek_asi16)
#endif	/* lint */

/*
 * This routine does an 32bit peek using ASI bypass.
 */
#if defined(lint)
/* ARGSUSED */
uint32_t
peek_asi32(int asi, uint64_t addr)
{
	return (0);
}
#else
	ENTRY(peek_asi32)
	rd	%asi, %o3
	wr	%g0, %o0, %asi

	membar  #Sync
	lda	[%o1]%asi, %o0
	membar  #Sync
	retl
	wr	%g0, %o3, %asi
	SET_SIZE(peek_asi32)
#endif	/* lint */

/*
 * This routine does an 64bit peek using ASI bypass.
 */
#if defined(lint)
/* ARGSUSED */
uint64_t
peek_asi64(int asi, uint64_t addr)
{
	return (0);
}
#else
	ENTRY(peek_asi64)
	rd	%asi, %o3
	wr	%g0, %o0, %asi

	membar  #Sync
	ldxa	[%o1]%asi, %o0
	membar  #Sync
	retl
	wr	%g0, %o3, %asi
	SET_SIZE(peek_asi64)
#endif	/* lint */

/*
 * This routine does an 8bit poke using ASI bypass.
 */
#if defined(lint)
/* ARGSUSED */
void
poke_asi8(int asi, uint64_t addr, uint8_t data)
{}
#else
	ENTRY(poke_asi8)
	rd	%asi, %o3
	wr	%g0, %o0, %asi

	membar  #Sync
	stuba	%o2, [%o1]%asi
	membar  #Sync
	retl
	wr	%g0, %o3, %asi
	SET_SIZE(poke_asi8)
#endif	/* lint */

/*
 * This routine does an 16bit poke using ASI bypass.
 */
#if defined(lint)
/* ARGSUSED */
void
poke_asi16(int asi, uint64_t addr, uint16_t data)
{}
#else
	ENTRY(poke_asi16)
	rd	%asi, %o3
	wr	%g0, %o0, %asi

	membar  #Sync
	stuwa	%o2, [%o1]%asi
	membar  #Sync
	retl
	wr	%g0, %o3, %asi
	SET_SIZE(poke_asi16)
#endif	/* lint */

/*
 * This routine does an 32bit poke using ASI bypass.
 */
#if defined(lint)
/* ARGSUSED */
void
poke_asi32(int asi, uint64_t addr, uint32_t data)
{}
#else
	ENTRY(poke_asi32)
	rd	%asi, %o3
	wr	%g0, %o0, %asi

	membar  #Sync
	sta	%o2, [%o1]%asi
	membar  #Sync
	retl
	wr	%g0, %o3, %asi
	SET_SIZE(poke_asi32)
#endif	/* lint */

/*
 * This routine does an 64bit poke using ASI bypass.
 */
#if defined(lint)
/* ARGSUSED */
void
poke_asi64(int asi, uint64_t addr, uint64_t data)
{}
#else
	ENTRY(poke_asi64)
	rd	%asi, %o3
	wr	%g0, %o0, %asi

	membar  #Sync
	stxa	%o2, [%o1]%asi
	membar  #Sync
	retl
	wr	%g0, %o3, %asi
	SET_SIZE(poke_asi64)
#endif	/* lint */
