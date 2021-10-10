/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Panther (UltraSPARC-IV+) error injector assembly routines.
 */

#include <sys/cheetahregs.h>
#include <sys/cheetahasm.h>
#include <sys/cmpregs.h>
#include <sys/machthread.h>
#include <sys/mmu.h>
#include <sys/memtest_u_asm.h>

/*
 * Retain these:
 * PN_L2_IDX_DISP_FLUSH & PN_L3_IDX_DISP_FLUSH are defined
 * under the -DCHEETAHPLUS in sys/cheetahregs.h but that
 * define/flag breaks others in memtest.
 */
#define	PN_L2_IDX_DISP_FLUSH		INT64_C(0x0000000000800000)
#define	PN_L3_IDX_DISP_FLUSH		INT64_C(0x0000000004000000)
#define	PN_L3_DATA_RD_MASK		0x7fffe0 /* l3_tag = PA<22:5> */

/*
 * The following cache flushall routines use MACROs defined outside
 * the injector codebase.
 */
#if defined(lint)
/*ARGSUSED*/
void
pn_flushall_l3()
{}
#else
        ENTRY(pn_flushall_l3)

	set 	PN_L3_SIZE, %o0;
	set 	PN_L3_LINESIZE, %o1;
	GET_CPU_IMPL(%o2);
	CHP_ECACHE_FLUSHALL(%o0, %o1, %o2);

        retl
        nop
        SET_SIZE(pn_flushall_l3)
#endif  /* lint */

#if defined(lint)
/*ARGSUSED*/
void
pn_flushall_l2()
{}
#else
        ENTRY(pn_flushall_l2)

	/*
	 * call macro in cheetahasm.h which takes care of setting
	 * l2size/linesize via scratch regs we pass in.
	 */
	PN_L2_FLUSHALL(%o0, %o1, %o2);

        retl
        nop
        SET_SIZE(pn_flushall_l2)
#endif  /* lint */

/*
 * Flush a single L2$ line at index matching the physical address inpar.
 */
#if defined(lint)
/*ARGSUSED*/
void
pn_flush_l2_line(uint64_t paddr)
{}
#else
        ENTRY(pn_flush_l2_line)
        set 	PN_L2_INDEX_MASK, %o1	! index =  PA<18:6> 
	and	%o0, %o1, %o2		! extract index from <PA>
        set 	PN_L2_IDX_DISP_FLUSH, %o3 ! set l2-flush-bit bit[23]
        or 	%o2, %o3, %o2		  ! .
	clr	%o4			! way counter (start at zero)
	set 	PN_L2_WAY_INCR, %o5	! way bits are PA[20:19]

	! %o2 : ASI_VA
	! %o4 : way counter
	! %o5 : way increment
1:
        ldxa	[%o2]ASI_L2_TAG, %g0	! disp flush
	add 	%o4, 1, %o4		! increment way counter
	cmp	%o4, PN_L2_NWAYS
	blt,a	1b
	  add	%o5, %o2, %o2		! add increment for next way

        retl
          nop
        SET_SIZE(pn_flush_l2_line)
#endif  /* lint */

#if defined(lint)
/*ARGSUSED*/
int
pn_flushall_dc(int cache_size, int linesize)
{
	return (0);
}
#else
        .align  64
        ENTRY(pn_flushall_dc)

        rdpr    %pstate, %o5                    ! save the processor state
        andn    %o5, PSTATE_IE, %o4             ! shut the interrupts off
        wrpr    %o4, 0, %pstate                 ! .
	CH_DCACHE_FLUSHALL(%o0, %o1, %o2);	! cheetahasm.h
        wrpr    %o5, %pstate                    ! turn the interrupts back on
        retl
        mov     %g0, %o0                        ! return value 0
        SET_SIZE(pn_flushall_dc)
#endif  /* lint */

/*
 * pn_flush_wc()
 * Stubbed as flushing W$ is not needed in Panther (included in L2 flush).
 */
#if defined(lint)
/*ARGSUSED*/
void
pn_flush_wc(uint64_t paddr)
{}
#else
        .align  64
        ENTRY(pn_flush_wc)
        retl
        SET_SIZE(pn_flush_wc)
#endif  /* lint */

/*
 * The code for Panther I$ flush should be the same as Cheetahplus
 * with the cache sizes/line size being different.
 * The ic_tag bits are the same <4:3> and the valid accessmode = <4:3>=0x10
 */
#if defined(lint)
/*ARGSUSED*/
int
pn_flushall_ic(int cache_size, int linesize)
{
	return (0);
}
#else
        .align  64
        ENTRY(pn_flushall_ic)

	CH_ICACHE_FLUSHALL(%o0, %o1, %o2, %o3);

        retl
        mov     %g0, %o0                        ! return value 0

        SET_SIZE(pn_flushall_ic)
#endif  /* lint */

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
 *	%o5 - scratch
 *
 * Interrupts are assumed to be already disabled.
 */
#if defined(lint)

/* ARGSUSED */
int
pn_wr_dcache_data_parity(uint64_t tag_addr, uint64_t data_addr,
		uint64_t tag_value, uint64_t xorpat, caddr_t vaddr)
{
	return (0);
}
#else	/* lint */

	ENTRY(pn_wr_dcache_data_parity)
	ldx	[%o4], %g0			! load the data
	membar	#Sync

	mov	1, %o5				! set dc_data_parity for
	sllx	%o5, PN_DC_DATA_PARITY_BIT_SHIFT, %o5	! parity fetch
	or	%o1, %o5, %o1			! .
1:
	ldxa	[%o0]ASI_DC_TAG, %o5		! read the tag
	sllx	%o5, 34, %o5			! format the tag contents
	srlx	%o5, 35, %o5			! 	for comparison
	cmp	%o5, %o2			! compare to expected value
	bne	2f				! no match
	nop

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
	SET_SIZE(pn_wr_dcache_data_parity)
#endif	/* lint */

/*
 * This routine plants an error into the dcache data parity bits at
 * the specified byte offset without modifying the cache state.  
 *
 * Register usage:
 *
 *	%o0 = 8 byte aligned d-cache offset to modify
 *	%o1 = xor pattern to use for corruption
 *	%o2 = parity bits from specified offset
 *	%o3 = unused
 *	%o4 = temp
 *	%o5 = saved pstate reg
 */
#if defined(lint)

/*ARGSUSED*/
void
pn_wr_dphys_parity(uint64_t offset_aligned, uint64_t xorpat)
{}
#else	/* lint */
	.align	128
	ENTRY(pn_wr_dphys_parity)

	rdpr	%pstate, %o5			! disable interrupts
	andn	%o5, PSTATE_IE, %o4		! .
	wrpr	%o4, 0, %pstate			! .

	set	0xfff8, %o4			! mask offset for ASI access
	and	%o0, %o4, %o0			! .

	mov	1, %o4				! set dc_data_parity bit
	sllx	%o4, PN_DC_DATA_PARITY_BIT_SHIFT, %o4
	or	%o0, %o4, %o0			! .

	membar	#Sync				! required before ASI_DC_DATA
	ldxa	[%o0]ASI_DC_DATA, %o2		! get d$ parity bits
	membar	#Sync				! required after ASI_DC_DATA
	xor	%o2, %o1, %o2			! corrupt parity
	stxa	%o2, [%o0]ASI_DC_DATA		! write mod'd parity bits to d$
	membar	#Sync				! required after ASI_DC_DATA

	retl					! return
	  wrpr	%o5, %pstate			! restore interrupts
	SET_SIZE(pn_wr_dphys_parity)
#endif	/* lint */

/*
 * Inject Panther DTLB parity errors.
 */
#if defined(lint)
/*ARGSUSED*/
uint64_t
pn_wr_dtlb_parity_idx(uint32_t index, uint64_t va, uint64_t pa,
		 uint64_t mask, uint64_t xorpat, uint32_t ctxnum)
{
	return (0);
}
#else
	ENTRY(pn_wr_dtlb_parity_idx)
	save	%sp, -SA(MINFRAME + 64), %sp
	srlx	%i1, 21, %l1		! l1 = VA<63:21>
	sllx	%i2, 21, %l2		! l2 = PA<42:13> 
	srlx	%l2, 34, %l2		! .
	
	mov	%i0, %l0		! l0 = index
	mov	%i5, %l3		! l3 = ctxnum

	sllx	%l0, 3, %l0
	or	%l0, %i3, %l0		! or in mask for 512_{0,1}

	clr	%i3			! reuse i3 now for way-ctr

	! l0 = index for access
	! l1 = VA<63:21>
	! l2 = PA<42:13>
	! l3 = ctxnum
	! i3 = ctr
	! l4, l5 = scratch
	
0:
	! Data compare
	ldxa	[%l0]ASI_DTLB_ACCESS, %l5
	sllx	%l5, 21, %l5
	srlx	%l5, 34, %l5		! PA<42:13>
	cmp  	%l5, %l2		! cmp. with PA<42:13> passed in
	bne,a	2f		
	nop

	! Tag compare
	ldxa	[%l0]ASI_DTLB_TAGREAD, %l5 	! fetch tag
	srlx	%l5, 21, %l4			! l4 = VA<63:21>
	cmp	%l4, %l1			! cmp with arg:VA<63:21>
	bne,a	2f
	nop

	! ctxnum compare
	! l5 = tag
	sllx	%l5, 51, %l4
	srlx	%l4, 51, %l4			! l4 = ctxnum=VA<12:0>
	cmp	%l4, %l3			! cmp with ctxnum
	bne,a	2f
	nop

	! l5 = tag
	
dtlb_flip_parity1:
	! <Tag, Data> compare succeeded
	! ctxnum compare succeeded
	! l5 = Tag
	! l0 = VA for STLB Data Access
	! i4 = xorpat
	
	! Corrupt using xorpat
	set	PN_TLB_DIAGACC_OFFSET, %l4
	add	%l4, %l0, %l0			! Diag Mask for 512_{0,1}
	ldxa	[%l0]ASI_DTLB_ACCESS, %l4
	xor	%l4, %i4, %i4			! corrupt
	mov	MMU_TAG_ACCESS, %l4		! VA=0x30
	stxa	%l5, [%l4]ASI_DMMU		! store match tag into DTLB
						!   Tag Access Reg
	stxa    %i4, [%l0]ASI_DTLB_ACCESS	! Write bad data	
						! Tag from Tag Access Reg used

	! Data/TTE has been corrupted
	! Load at tag to induce error
	ba	3f
	nop

2:
	set	PN_TLB_ACC_WAY_BIT, %l5		! Bit<11>:Way0/1 selector
	or	%l5, %l0, %l0
	add	%i3, 1, %i3			! add to ctr: %i3
	cmp	%i3, PN_DTLB_NWAYS
	blt,a	0b
	nop
	
	set 	0xfeecf, %i4 			! fail pattern

3:
	ret
	restore	%g0, %i4, %o0
	SET_SIZE(pn_wr_dtlb_parity_idx)
#endif 	/* lint */

/*
 * I-TLB injector for Panther
 */
#if defined(lint)
/*ARGSUSED*/
uint64_t
pn_wr_itlb_parity_idx(uint32_t index, uint64_t va, uint64_t pa,
			 uint64_t mask, uint64_t xorpat, uint32_t ctxnum)
{
	return (0);
}
#else
	ENTRY(pn_wr_itlb_parity_idx)
	save	%sp, -SA(MINFRAME + 64), %sp
	srlx	%i1, 21, %l1		! l1 = VA<63:21>
	sllx	%i2, 21, %l2		! l2 = PA<42:13> 
	srlx	%l2, 34, %l2		! .

	mov	%i0, %l0		! l0 = index
	mov	%i5, %l3		! l3 = ctxnum

	sllx	%l0, 3, %l0
	or	%l0, %i3, %l0		! or in mask for 512_{0,1}

	clr	%i3			! reuse i3 now for way-ctr

	! l0 = index for access
	! l1 = VA<63:21>
	! l2 = PA<42:13>
	! l3 = ctxnum
	! i3 = ctr
	! l4, l5 = scratch
0:
	! Data compare
	ldxa	[%l0]ASI_ITLB_ACCESS, %l5
	sllx	%l5, 21, %l5
	srlx	%l5, 34, %l5		! PA<42:13>
	cmp  	%l5, %l2		! cmp. with PA<42:13> passed in
	bne,a	2f		
	nop

	! Tag compare
	ldxa	[%l0]ASI_ITLB_TAGREAD, %l5 	! fetch tag
	srlx	%l5, 21, %l4			! l4 = VA<63:21>
	cmp	%l4, %l1			! cmp with arg:VA<63:21>
	bne,a	2f
	nop

	! ctxnum compare
	! l5 = tag
	sllx	%l5, 51, %l4
	srlx	%l4, 51, %l4			! l4 = ctxnum=VA<12:0>
	cmp	%l4, %l3			! cmp with ctxnum
	bne,a	2f
	nop

	! l5 = tag
	
1:
	! <Tag, Data> compare succeeded
	! ctxnum compare succeeded
	! l5 = Tag
	! l0 = VA for ITLB Data Access
	! i4 = xorpat
	
	! Corrupt using xorpat
	
	set	PN_TLB_DIAGACC_OFFSET, %l4
	add	%l4, %l0, %l0			! Diag Mask for 512_{0,1}
	ldxa	[%l0]ASI_ITLB_ACCESS, %l4
	xor	%l4, %i4, %i4			! corrupt
	mov	MMU_TAG_ACCESS, %l4		! VA=0x30
	stxa	%l5, [%l4]ASI_IMMU		! store match tag into DTLB
						!   Tag Access Reg
	stxa    %i4, [%l0]ASI_ITLB_ACCESS	! Write bad data	
						! Tag from Tag Access Reg used

	! Data/TTE has been corrupted
	! Load at tag to induce error
	
	ba	3f
	nop

2:
	set	PN_TLB_ACC_WAY_BIT, %l5		! Bit<11>:Way0/1 selector
	or	%l5, %l0, %l0
	add	%i3, 1, %i3			! add to ctr: %i3
	cmp	%i3, PN_ITLB_NWAYS
	blt,a	0b
	nop
	
	set 	0xfeecf, %i4 			! fail pattern
3:
	ret
	restore	%g0, %i4, %o0
	SET_SIZE(pn_wr_itlb_parity_idx)
#endif /* lint */

/*
 * Fetch tag/data from specified index/DTLB_512{0,1}
 * The DTLB_512{0,1} is specifed by the appropriate mask for that DTLB array.
 */
#if defined(lint)
/*ARGSUSED*/
int
pn_get_dtlb_entry(int index, int way, uint64_t mask, uint64_t *tagp,
			uint64_t *datap)
{
	return (0);
}
#else

!
! NOTE : The index passed in needs to take into account that this is 2-way.
! If computed from VA<20:13> == 8 bits == 256 entries needs to shift in 
! way<0:1> into index as well.
!
        ENTRY(pn_get_dtlb_entry)
	sllx	%o0, 3, %o0		! shift index into VA<11:3>
	sllx 	%o1, 11, %o1		! way 
	or	%o0, %o1, %o0
	or	%o0, %o2, %o1		! set up VA for access

        ldxa    [%o1]0x5E, %o5          ! fetch Tag
	stx	%o5, [%o3]		! store Tag
        ldxa    [%o1]0x5D, %o5          ! fetch Data
	stx	%o5, [%o4]		! store Data

        retl
        nop
        SET_SIZE(pn_get_dtlb_entry)
#endif  /* lint */

/*
 * Fetch tag/data from specified index/ITLB_512{0,1}
 * The ITLB_512{0,1} is specifed by the appropriate mask for that DTLB array.
 */
#if defined(lint)
/*ARGSUSED*/
int
pn_get_itlb_entry(int index, int way, uint64_t mask, uint64_t *tagp,
			uint64_t *datap)
{
	return (0);
}
#else

/*
 * NOTE : The index passed in needs to take into account that this is 2-way.
 * If computed from VA<20:13> == 8 bits == 256 entries needs to shift in 
 * way<0:1> into index as well.
 */
	ENTRY(pn_get_itlb_entry)
	sllx	%o0, 3, %o0		! shift index into VA<11:3>
	sllx 	%o1, 11, %o1		! way 
	or	%o0, %o1, %o0
	or	%o0, %o2, %o1		! set up VA for access

        ldxa    [%o1]0x56, %o5          ! fetch Tag
	stx	%o5, [%o3]		! store Tag
        ldxa    [%o1]0x55, %o5          ! fetch Data
	stx	%o5, [%o4]		! store Data

        retl
        nop
        SET_SIZE(pn_get_itlb_entry)
#endif  /* lint */

#define DCU_SPE_SHIFT 43		/* bit <32> = SPE in DCU */

#if defined(lint)
/*ARGSUSED*/
uint64_t
pn_pcache_load(uint64_t va)
{
	return (0);
}
#else
        ENTRY(pn_pcache_load)
! 	load va again to set in TLB
	ldx	[%o0], %o1

	! Turn ON SPE -- should be on 
        ldxa    [%g0]ASI_DCU, %o2
        mov     1, %o1
        sllx    %o1, DCU_SPE_SHIFT, %o1
        or      %o2, %o1, %o3
        stxa    %o3, [%g0]ASI_DCU
        membar  #Sync

	! strong prefetch in P$ only
	prefetch	[%o0],21

	! long delay that moves instr. through MS
	mov	1, %o3
	udivcc	%o3, 1, %o3

	! Turn off SPE in DCU so other prefetches don't interfere
        ldxa    [%g0]ASI_DCU, %o2
        mov     1, %o1
        sllx    %o1, DCU_SPE_SHIFT, %o1
        andn    %o2, %o1, %o3
        stxa    %o3, [%g0]ASI_DCU
        membar  #Sync

        retl
        nop
        SET_SIZE(pn_pcache_load)
#endif  /* lint */

/* 
 * Set up a data segment for P$ tests
 * align on 64 byte line
 * can use to fill to a pattern if need be
 *
 * NOTE: This comes in especially useful for the P$ which is
 * quite unpredictable in error injection.
 */
#if !defined(lint)
.reserve pc_dmp, 512, ".data", 512
.global  pc_dmp

.reserve dbg_seg, 512, ".data", 512
.global  dbg_seg
#endif /* lint */

/*
 * Load a P$ status line and also dump Data, Tag into pc_dmp
 * area for diagnosis.
 * This gets used often for P$ errors because of the uncertain 
 * results with P$. Hence the need for the above debug segments
 * as well.	 
 */
#if defined(lint)
/*ARGSUSED*/
uint64_t
pn_pcache_status_entry(uint64_t va)
{
	return (0);
}
#else
        ENTRY(pn_pcache_status_entry)

        set     PN_PCACHE_ADDR_MASK, %o1
        and     %o0, %o1, %o2                   ! pc_addr=<8:6>
        set     pc_dmp, %o3
        clr     %o4                             ! way

1:
	! store way
        stx     %o4, [%o3]
        add     %o3, 8, %o3
	
	! store pc_status_reg val
        ldxa    [%o2]ASI_PC_STATUS_DATA, %o5
        stx     %o5, [%o3]
        add     %o3, 8, %o3

        ! debug stores of first 64 bit data word and virtual tag
        ! store data for that way -- first double word only
        ldxa    [%o2]ASI_PC_DATA, %o5
        stx     %o5, [%o3]
        add     %o3, 8, %o3

        ! store virtual tag for line
        ldxa    [%o2]ASI_PC_TAG, %o5
        stx     %o5, [%o3]
        add     %o3, 8, %o3

        !next way
        add     %o4, 1, %o4
        add     %o2, PN_PCACHE_WAY_INCR, %o2       ! set up addr for next way
        cmp     %o4, PN_PCACHE_NWAYS
        blt,a   1b
        nop

        retl
        nop
        SET_SIZE(pn_pcache_status_entry)
#endif  /* lint */

/*
 * Load a P$ status line and compare tag. If match corrupt parity
 */
#if defined(lint)
/*ARGSUSED*/
uint64_t
pn_pcache_write_parity(uint64_t va, uint64_t pa)
{
	return (0);
}
#else
        ENTRY(pn_pcache_write_parity)

        set     PN_PCACHE_ADDR_MASK, %o2
        and     %o0, %o2, %o2                   ! o2 = pc_addr=<8:6>
        srlx    %o0, 6, %o0                     ! o0 == VA<63:6> for cmp.
        srlx    %o1, 6, %o1                     ! o1= PA<42:6> for cmp.

        clr     %o4                             ! way-ctr

1:
        ldxa    [%o2]ASI_PC_STATUS_DATA, %o3
        srlx    %o3, 1, %o5
        sllx    %o5, 27, %o5
        srlx    %o5, 27, %o5            ! o5 = PA<42:6> from reg bits PA<37:1>
        cmp     %o5, %o1                ! cmp. PA match
        bne,a   2f                      ! next way
        nop

        ldxa    [%o2]ASI_PC_TAG, %o5    ! fetch VA Tag Value

        sllx    %o5, 6, %o5
        srlx    %o5, 6, %o5             ! o5 = VA tag<63:6> from reg <57:0>
        cmp     %o5, %o0                ! cmp VA tag value
        bne,a   2f
        nop

        ! match on VA tag and PA; corrupt data parity
        ! o3 contains reg val for match
        mov     1, %o5
        sllx    %o5, 57, %o5
        xor     %o3, %o5, %o3                   ! corrupt parity for bytes[7:0]
        stxa    %o3, [%o2]ASI_PC_STATUS_DATA    ! store back
        ba      3f                              ! exit; success

2:      ! next way
        add     %o4, 1, %o4
        add     %o2, PN_PCACHE_WAY_INCR, %o2	! set up addr for next way
        cmp     %o4, PN_PCACHE_NWAYS
        blt,a   1b
        nop

        set     0xfeecf, %o0

3:
        retl
        nop
        SET_SIZE(pn_pcache_write_parity)
#endif  /* lint */

/*
 * This routine Loads using FP regs
 */
#if defined(lint)
/*ARGSUSED*/
uint64_t
pn_load_fp(uint64_t va)
{
	return (0);
}
#else   /* lint */
#define FPRS_FEF 0x4
        ENTRY(pn_load_fp)
        ! make sure fprs is set for floating point instr.
        ! turn on %FPs
        set FPRS_FEF, %o1
        wr %o1, %fprs

        mov     %asi, %o2               ! save %asi
        mov     ASI_P, %asi

       	ldda    [%o0]%asi, %d0
        mov     %o2, %asi               ! restore asi

        ! debug dump of fp load at addr: (pc_dmp+0x100)
        set     pc_dmp, %o2
        add     %o2, 0x100, %o2
        std     %d0, [%o2]

        retl
        nop
        SET_SIZE(pn_load_fp)
#endif /* lint */ 

#define DCU_PE_SHIFT 45

#if defined(lint)
/*ARGSUSED*/
uint64_t
pn_disable_pcache()
{
	return (0);
}
#else
        ENTRY(pn_disable_pcache)
        ldxa    [%g0]ASI_DCU, %o2
        mov     1, %o1
        sllx    %o1, DCU_PE_SHIFT, %o1
        andn    %o2, %o1, %o3
        stxa    %o3, [%g0]ASI_DCU
        membar  #Sync
      
        retl
        ldxa    [%g0]ASI_DCU, %o0               ! return new settings
        SET_SIZE(pn_disable_pcache)
#endif  /* lint */

#if defined(lint)
/*ARGSUSED*/
uint64_t
pn_enable_pcache()
{
	return (0);
}
#else
        ENTRY(pn_enable_pcache)
        ldxa    [%g0]ASI_DCU, %o2
        mov     1, %o1
        sllx    %o1, DCU_PE_SHIFT, %o1
        or      %o2, %o1, %o3
        stxa    %o3, [%g0]ASI_DCU
        membar  #Sync
      
        retl
        ldxa    [%g0]ASI_DCU, %o0               ! return new settings
        nop

        SET_SIZE(pn_enable_pcache)
#endif  /* lint */

/*
 * pn_wr_l3_data()
 * Inject error in L3 data or data ecc (depending on inpar).
 *
 * Given a data PA formatted for L3 search through tag indexes to find 
 * index that has the matching tag.
 *
 * l3_tag_addr = PA<22:6> of associated data
 * l3_tag_val = PA<42:23> of associated data -- used for comparison.
 *
 * Then get the data associated with that tag.
 * We assume the line has been loaded into the L3 cache by now.
 *
 * This function either corrupts the data or the ecc
 * depending on the reg_select arg passed in.
 */

/* Macro for E$ (L3$) Data Corruption */
#define EC_SET_WAYADDR(r0, r1, r2, incr, lim)       \
        set     incr, r2;       \
        add     r2, r0, r0;     \
        add     r2, r1, r1;     \
        set     lim, r2

#if defined (lint)
/*ARGSUSED*/
uint64_t pn_wr_l3_data(uint64_t tag_addr, uint64_t data_addr, 
                uint64_t tag_cmp, uint64_t xorpat, int reg_select)
{
	return (0);
}
#else
        .align  64
        ENTRY(pn_wr_l3_data)

	!o0 = PA<22:6>
	!o1 = PA<22:5>
1:
        membar  #Sync
        ldxa    [%o0]ASI_EC_DIAG, %o5           ! fetch tag for comparison
        membar  #Sync

	/*
	 * There can be duplicate tags in different ways with differing cache
	 * line states where one is valid but another is not.
	 * Skip to next way if state is invalid.
	 */
	andcc	%o5, 0x7, %g0			! lower 3 bits = State
	bz	2f				! 3'b000 = Invalid, next
	  nop					! non-zero = Valid, cont

        sllx    %o5, 20, %o5                    ! format the tag data content
        srlx    %o5, 44, %o5                    !       for comparison
        cmp     %o5, %o2
        bne,a   2f                              ! not equal? : fetch next way
          nop

	/*
	 * Comparison succeeded; fetch and corrupt the data or ecc bits.
	 */
	membar  #Sync
	ldxa    [%o1]ASI_EC_R, %g0              ! load staging registers
	membar  #Sync				!   with data read

	sllx    %o4, 3, %o4			! shift the selct bits up
	ldxa    [%o4]ASI_EC_DATA, %o5           ! fetch data_N:<reg_select>
	membar  #Sync

	xor     %o5, %o3, %o3                   ! (data OR data_ecc) ^ xorpat
	stxa    %o3, [%o4]ASI_EC_DATA           ! store back to staging regs
	membar  #Sync

	stxa    %g0, [%o1]ASI_EC_W              ! write back staging regs 
	membar  #Sync
	ba      3f                              ! exit/success
	  nop

2:      ! fetch next way
        EC_SET_WAYADDR(%o0, %o1, %o5, PN_L3_WAY_INCR, PN_L3_WAY_LIM)
        cmp     %o0, %o5                        ! reached max-ways?
        bl,a    1b                              ! no; check next way
          nop

        set     0xfeccf, %o0                    ! end of way-loop; return fail
3:
        retl
          nop

        SET_SIZE(pn_wr_l3_data)
#endif  /* lint */

/*
 * pn_wr_l2_data()
 * Inject error in L2 data or data ecc (depending on inpar).
 */
#if defined (lint)
/*ARGSUSED*/
uint64_t pn_wr_l2_data(uint64_t tag_addr, uint64_t data_addr, 
                uint64_t tag_cmp, uint64_t xorpat, int reg_select)
{
	return (0);
}
#else
        .align  64
        ENTRY(pn_wr_l2_data)

	!o0 = PA<18:6>
	!o1 = PA<18:6>
1:
        membar  #Sync
        ldxa    [%o0]ASI_L2_TAG, %o5           ! fetch tag for comparison
        membar  #Sync

	/*
	 * There can be duplicate tags in different ways with differing cache
	 * line states where one is valid but another is not.
	 * Skip to next way if state is invalid
	 */
	andcc	%o5, 0x7, %g0			! lower 3 bits = State
	bz	2f				! 3'b000 = Invalid, next
	  nop					! non-zero = Valid, cont

        sllx    %o5, 21, %o5                    ! format the tag data content
        srlx    %o5, 40, %o5                    !       for comparison
        cmp     %o5, %o2
        bne,a   2f                              ! not equal? : fetch next way
          nop

        /*
         * Comparison succeeded; fetch then corrupt data or ecc bits.
         */
	cmp	%o4, 8				! value of 8 means ecc
	bne,a	l2_data_sel			! .
          sllx    %o4, 3, %o4			! o4 = xw_offset<5:3>
	mov	1, %o4				! if must corrupt ecc
	sllx	%o4, 21, %o4			!   set bit 21 to read ecc

l2_data_sel:
	or	%o4, %o1, %o1			! o1 = complete vaddr for ASI
	membar 	#Sync				! .

        ldxa    [%o1]ASI_L2_DATA, %o5           ! load data read
        membar  #Sync
        xor     %o5, %o3, %o3                   ! (data OR data_ecc) ^ xorpat
        stxa    %o3, [%o1]ASI_L2_DATA           ! store back to L2
        membar  #Sync

        ba      3f                              ! exit/success
          nop

2:      ! fetch next way
        EC_SET_WAYADDR(%o0, %o1, %o5, PN_L2_WAY_INCR, PN_L2_WAY_LIM)
        cmp     %o0, %o5                        ! reached max-ways?
        bl,a    1b                              ! no; check next way
	  nop

        set     0xfeccf, %o0                    ! end default/fail return
3:
        retl
          nop

        SET_SIZE(pn_wr_l2_data)
#endif  /* lint */

/*
 * pn_wr_l3_tag()
 * Inject error in L3 tag data or tag ecc (depending on xor pattern).
 */
#if defined(lint)
/*ARGSUSED*/
uint64_t
pn_wr_l3_tag(uint64_t paddr, uint64_t xorpat)
{
	return (0);
}
#else
        .align  64
        ENTRY(pn_wr_l3_tag)

	mov 	0, %o4				! o4 = way-ctr 

	! format paddr for tag fetch
	set 	PN_L3_TAG_RD_MASK, %o2
	and 	%o0, %o2, %o2			! o2 = PA<22:6> :
						!   l3_tag_addr, way:0

	! format paddr for tag comparison PA<42:23>
	sllx	%o0, 21, %o3
	srlx	%o3, 44, %o3			! o3 = PA<42:23> :
						!   tag compare value
1:
	membar 	#Sync
	ldxa   	[%o2]ASI_EC_DIAG, %o5		! fetch tag for comparison
	membar 	#Sync

	! There can be duplicate tags in different ways with differing cache
	! line states where one is valid but another is not.
	! Skip to next way if state is invalid
	andcc	%o5, 0x7, %g0			! lower 3 bits = State
	bz	2f				! 3'b000 = Invalid, next
	  nop					! non-zero = Valid, cont

        sllx    %o5, 20, %o5                    ! format the tag data content
        srlx    %o5, 44, %o5                    !       for comparison
	cmp    	%o5, %o3
	bne,a 	2f				! not equal? : fetch  next way
	  nop

	!
	! Comparison succeeded; fetch & corrupt tag/ecc
	! We assume xorpat has been correctly set for either tag or ecc.
	! This allows this code to be shared for tag/ecc corruption 
 	! as Panther L3 tag/state register has tag and ecc values together.
	! In either case, this would result in mis-matched {tag/ecc} pair
	! stored back as the original {tag|ecc} pair is replaced with 
	! newly corrupted {tag|ecc} pair.
	!
	ldxa	[%o2]ASI_EC_DIAG, %o5		! reload tag/ecc
	xor	%o5, %o1, %o5			! apply xorpat passed in 
	stxa	%o5, [%o2]ASI_EC_DIAG		! write back corrupted tag/ecc
	membar	#Sync

	ba 	3f				! exit/success
	  nop

2:	! fetch next way
	set 	PN_L3_WAY_INCR, %o5		! add mask for next way
	add 	%o2, %o5, %o2			! o2 = l3 tag addr for next way

	add	%o4, 1, %o4
	cmp	%o4, PN_L3_NWAYS		! reached max-ways? 
	bl,a 	1b				! no; check next way
	  nop

	set     0xfeccf, %o0			! end of way-loop;
						!   default/fail return
3:
	retl
	  nop

        SET_SIZE(pn_wr_l3_tag)
#endif  /* lint */

/*
 * pn_wr_l2_tag()
 * Inject error in L2 tag data or tag ecc (depending on xor pattern).
 */
#if defined(lint)
/*ARGSUSED*/
uint64_t
pn_wr_l2_tag(uint64_t paddr, uint64_t xorpat)
{
	return (0);
}
#else
        .align  64
        ENTRY(pn_wr_l2_tag)
	mov 	0, %o4				! o4 = way-ctr 

	! format paddr for tag fetch
	set 	PN_L2_INDEX_MASK, %o2		! o2 = PA<18:6> :
	and 	%o0, %o2, %o2			!   l2_tag_addr, way:0

	! format paddr for tag comparison PA<42:19>
	sllx	%o0, 21, %o3
	srlx	%o3, 40, %o3			! o3 = PA<42:23> :
						!   tag compare value
1:
	membar 	#Sync
	ldxa   	[%o2]ASI_L2_TAG, %o5		! fetch tag for comparison
	membar 	#Sync

	/*
	 * There can be duplicate tags in different ways with differing cache
	 * line states where one is valid but another is not.
	 * Skip to next way if state is invalid.
	 */
	andcc	%o5, 0x7, %g0			! lower 3 bits = State
	bz	2f				! 3'b000 = Invalid, next
	  nop					! non-zero = Valid, cont

        sllx    %o5, 21, %o5                    ! format the tag data content
        srlx    %o5, 40, %o5                    !       for comparison
	cmp    	%o5, %o3
	bne,a 	2f				! not equal? : fetch  next way
	  nop

	/*
	 * Comparison succeeded; fetch & corrupt tag/ecc
	 *
	 * We assume xorpat has been correctly set for either tag or ecc.
	 * This allows this code to be shared for tag data/ecc corruption 
 	 * as Panther L2 tag/state register has tag and ecc values together.
	 * In either case, this would result in mis-matched {tag/ecc} pair
	 * stored back as the original {tag|ecc} pair is replaced with 
	 * newly corrupted {tag|ecc} pair.
	 */
	ldxa	[%o2]ASI_L2_TAG, %o5		! reload tag/ecc
	xor	%o5, %o1, %o5			! apply xorpat passed in 
	stxa	%o5, [%o2]ASI_L2_TAG		! write back corrupted tag/ecc
	membar	#Sync

	ba 	3f				! exit/success
	  nop

2:	! fetch next way
	set 	PN_L2_WAY_INCR, %o5		! add mask for next way
	add 	%o2, %o5, %o2			! o2 = l3 tag addr for next way

	add	%o4, 1, %o4
	cmp	%o4, PN_L2_NWAYS		! reached max-ways? 
	bl,a 	1b				! no; check next way
	  nop

	set     0xfeccf, %o0			! end of way-loop
						!   default/fail return
3:
	retl
	  nop

        SET_SIZE(pn_wr_l2_tag)
#endif  /* lint */

/*
 * The following four routines are "phys" versions of the above L2 and L3
 * data and tag corruption routines.  The routines are used to corrupt the
 * data, data ecc bits, tag, or tag ecc bits of an L2 or L3 cache line
 * determined by a byte offset.
 *
 * These routines are simpler than those above because there is no allocated
 * EI buffer and so no data is brought into the cache.  The cacheline specified
 * by the offset is used regardless of it's state and the data is not owned
 * by the injector.
 *
 * Note that the PHYS routines disable interrupts during the injection since
 * the interrupts were not disabled prior to calling these routines.
 */
#if defined (lint)
/*ARGSUSED*/
uint64_t pn_wr_l3phys_data(uint64_t offset, uint64_t xorpat, int reg_select)
{
	return (0);
}
#else
        .align  64
        ENTRY(pn_wr_l3phys_data)

	/*
	 * Mask the offset/address for data ASI access, including the way
	 * bits in the mask.  %o3 = offset[24:5]
	 */
	mov	3, %o3				! mask offset for tag fetch
	sllx	%o3, PN_L3_WAY_SHIFT, %o3	!   (index + way bits)
	set	PN_L3_DATA_RD_MASK, %o4		! .
	or	%o3, %o4, %o3			! complete mask
	and 	%o0, %o3, %o3			! %o3 = PA[24:5]

	rdpr    %pstate, %o5			! disable interrupts
	andn    %o5, PSTATE_IE, %o4		! .
	wrpr    %o4, 0, %pstate			! .

        ldxa    [%o3]ASI_EC_R, %g0              ! load data into staging regs
        membar  #Sync				! .

        sllx    %o2, 3, %o2			! select data or ecc
        ldxa    [%o2]ASI_EC_DATA, %o4           ! fetch data_N:<reg_select>
        membar  #Sync

        xor     %o4, %o1, %o4			! (data OR data_ecc) ^ xorpat
        stxa    %o4, [%o2]ASI_EC_DATA           ! store back to staging regs
        membar  #Sync

        stxa    %g0, [%o3]ASI_EC_W              ! write back staging regs 
        membar  #Sync

	mov	%g0, %o0			! return success
        retl					! exit
	  wrpr	%o5, %pstate			! restore interrupts

        SET_SIZE(pn_wr_l3phys_data)
#endif  /* lint */

/*
 * pn_wr_l2phys_data()
 * Inject error in L2 data or data ecc (depending on inpar) at byte offset.
 */
#if defined (lint)
/*ARGSUSED*/
uint64_t pn_wr_l2phys_data(uint64_t offset, uint64_t xorpat, int reg_select)
{
	return (0);
}
#else
        .align  64
	ENTRY(pn_wr_l2phys_data)

	/*
	 * Mask the offset/address for data ASI access, including the way
	 * bits in the mask.  %o3 = offset[20:6]
	 */
	mov	3, %o3				! mask offset for tag fetch
	sllx	%o3, PN_L2_WAY_SHIFT, %o3	!   (index + way bits)
	set	PN_L2_INDEX_MASK, %o4		! .
	or	%o3, %o4, %o3			! complete mask
	and 	%o0, %o3, %o3			! %o3 = PA[20:6]

	rdpr    %pstate, %o5			! disable interrupts
	andn    %o5, PSTATE_IE, %o4		! .
	wrpr    %o4, 0, %pstate			! .

	cmp	%o2, 8				! select data or ecc
	bne,a	1f				! .
          sllx    %o2, 3, %o2			! %o4 = xw_offset<5:3>
	mov	1, %o2
	sllx	%o2, 21, %o2

1: ! data_select
	or	%o3, %o2, %o3
	membar 	#Sync
        ldxa    [%o3]ASI_L2_DATA, %o4           ! load data at location
        membar  #Sync
        xor     %o4, %o1, %o4                   ! (data OR data_ecc) ^ xorpat
        stxa    %o4, [%o3]ASI_L2_DATA           ! store back to L2
        membar  #Sync

	mov	%g0, %o0			! return success
        retl					! exit
	  wrpr	%o5, %pstate			! restore interrupts

        SET_SIZE(pn_wr_l2phys_data)
#endif  /* lint */

/*
 * pn_wr_l3phys_tag()
 * Inject error in L3 tag data or tag ecc (depending on xor pattern).
 */
#if defined(lint)
/*ARGSUSED*/
uint64_t
pn_wr_l3phys_tag(uint64_t offset, uint64_t xorpat)
{
	return (0);
}
#else
        .align  64
        ENTRY(pn_wr_l3phys_tag)

	/*
	 * The following is a tortuous way to make the mask for the index
	 * and way bits but doing this we don't have to add a new define
	 * to a kernel header file (it's only two more instructions).
	 */
	mov	3, %o2				! mask offset for tag fetch
	sllx	%o2, PN_L3_WAY_SHIFT, %o2	!   (index + way bits)
	set	PN_L3_TAG_RD_MASK, %o3		! .
	or	%o2, %o3, %o2			! .
	and 	%o0, %o2, %o2			! %o2 = PA[24:6]

	rdpr    %pstate, %o5			! disable interrupts
	andn    %o5, PSTATE_IE, %o4		! .
	wrpr    %o4, 0, %pstate			! .

	/*
	 * We assume the xorpat has been correctly set for either tag or ecc.
	 * This allows the code to be shared for tag/ecc corruption since
	 * the Panther L3 tag/state register contains both the ecc values.
	 */
	ldxa	[%o2]ASI_EC_DIAG, %o4		! reload tag/ecc
	xor	%o4, %o1, %o4			! apply xorpat passed in 
	stxa	%o4, [%o2]ASI_EC_DIAG		! write back corrupted tag/ecc
	membar	#Sync

	mov	%g0, %o0			! return success
        retl					! exit
	  wrpr	%o5, %pstate			! restore interrupts

        SET_SIZE(pn_wr_l3phys_tag)
#endif  /* lint */

/*
 * pn_wr_l2phys_tag()
 * Inject error in L2 tag data or tag ecc (depending on xor pattern).
 */
#if defined(lint)
/*ARGSUSED*/
uint64_t
pn_wr_l2phys_tag(uint64_t offset, uint64_t xorpat)
{
	return (0);
}
#else
        .align  64
	ENTRY(pn_wr_l2phys_tag)

	/*
	 * The following is a tortuous way to make the mask for the index
	 * and way bits but doing this we don't have to add a new define
	 * to a kernel header file (it's only two more instructions).
	 */
	mov	3, %o2				! mask offset for tag fetch
	sllx	%o2, PN_L2_WAY_SHIFT, %o2	!   (index + way bits)
	set 	PN_L2_INDEX_MASK, %o3		! .
	or 	%o2, %o3, %o2			! .
	and 	%o0, %o2, %o2			! %o2 = PA[20:6]

	rdpr    %pstate, %o5			! disable interrupts
	andn    %o5, PSTATE_IE, %o4		! .
	wrpr    %o4, 0, %pstate			! .

	/*
	 * We assume the xorpat has been correctly set for either tag or ecc.
	 * This allows the code to be shared for tag/ecc corruption since
	 * the Panther L3 tag/state register contains both the ecc values.
	 */
	ldxa	[%o2]ASI_L2_TAG, %o4		! read tag/ecc at location
	xor	%o4, %o1, %o4			! apply xorpat passed in 
	stxa	%o4, [%o2]ASI_L2_TAG		! write back corrupted tag/ecc
	membar	#Sync

	mov	%g0, %o0			! return success
        retl					! exit
	  wrpr	%o5, %pstate			! restore interrupts

        SET_SIZE(pn_wr_l2phys_tag)
#endif  /* lint */

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
pn_wr_icache_stag(uint64_t addr, uint64_t tag_addr, uint64_t utag_val,
		uint64_t xorpat, uint64_t store_addr, uint64_t *ptr)
{}
#else	/* lint */

	ENTRY(pn_wr_icache_stag)
	save	%sp, -SA(MINFRAME), %sp
	set	PN_ICACHE_IDX_INCR, %l1
	set	CHP_ICACHE_FN_ADDR_ICR*2, %l2
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
	set	PN_ICACHE_IDX_LIMIT, %l2

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
	SET_SIZE(pn_wr_icache_stag)
#endif /* lint */

/*
 * NOTE: DO NOT alter the location of the 2 following functions:
 * pn_wr_ipb() and pn_ipb_asmld().
 * 
 * pn_wr_ipb() corrupts the instr. block that will get loaded into the IPB
 * on entry into this function. We have carefully aligned pn_ipb_asmld() below
 * to be 192 byte offset from this function and expect IPS to be set 
 * to:2'b10(192 byte stride) so that upon entry into this function pn_ipb_asmld
 * will get loaded into the IPB.
 * The paddr passed in as the arg#0 is the paddr of pn_ipb_asmld().
 * We use this to search all 8 entries in the IPB for a matching IPB_tag and
 * corrupt the instruction line and return to the calling code which should
 * then reexec the line to trigger the parity(if IPE is enabled).
 *
 */
#if defined(lint)
/*ARGSUSED*/
uint64_t
pn_wr_ipb(uint64_t paddr, int xorpat)
{
	return (0);
}
#else
        .align  64
        ENTRY(pn_wr_ipb)

	lda	[%o0]ASI_MEM, %o3	! fetch 4 byte instr at paddr for cmp.
	srlx	%o0, 9, %o0		! PA tag <41:9> == cmp_tag
	clr	%o2			! index:IPB_addr<9:6>

	! o0 = PA<41:9> tag cmp
	! o2 = IPB index <9:6>
	! o3 = 4byte instr

1:	! compare IPB tag
	!
	sllx	%o2, 6, %o4 
	ldxa    [%o4]ASI_IPB_TAG, %o5	! get IPB Tag+Valid
	sllx	%o5, 23, %o5		! extract tag
	srlx	%o5, 31, %o5		! o5 = IPB_tag<41:9>
	cmp	%o5, %o0		! cmp tag
	bne	3f
	nop
2:
	! %o2 = ctr
	! %o3 = 4 byte instr.
	! match : corrupt if instr matches as well
        ldxa    [%o4]ASI_IPB_DATA, %o4	! get IPB instr+predecode+parity.
	sllx	%o4, 32, %o5		! get 4 byte instr.
	srlx	%o5, 32, %o5		! get lower 32 bits - instr.
	cmp	%o5, %o3		! comp. instr as well
	bne	3f
	nop

	xor	%o1, %o4, %o1		! IPB (instr+pre+par) ^= xorpat
	! regenerate index -- we clobbered %o4 above
	sllx	%o2, 6, %o4
        membar  #Sync
        stxa    %o1, [%o4]ASI_IPB_DATA  ! store corrupted IPB:IC instr+pre+par 
        membar  #Sync
	flush 	0
	mov	%o1, %o0		! return instr. fetched/corrupted

	retl
	nop

3:	! increment ctr
	add	%o2, 1, %o2
        cmp	%o2, 16
	blt,a	1b
	nop

	set 	0xfeccf, %o0		! returned FAIL pattern
4:
        retl
	nop

	.align 64
        ALTENTRY(pn_ipb_asmld)
        clr     %o1                             ! = 0x92100000
        clr     %o1
        clr     %o1
        clr     %o1
        clr     %o1
        clr     %o1
        clr     %o1
        clr     %o1
        clr     %o1
        clr     %o1
        clr     %o1
        clr     %o1
        clr     %o1
        clr     %o1
        clr     %o1
        clr     %o1
        clr     %o1
        clr     %o1
        clr     %o1
        clr     %o1
        clr     %o1
        clr     %o1
        clr     %o1
        clr     %o1
        clr     %o1
        clr     %o1
        clr     %o1
        clr     %o1
        clr     %o1
        clr     %o1
        clr     %o1
        clr     %o1
        retl
	nop
        SET_SIZE(pn_ipb_asmld)
        SET_SIZE(pn_wr_ipb)
#endif	/* lint */

/*
 * pn_wr_dup_l2_tag()
 * Induce L2 duplicate tag protocol error(IERR).
 * Given a PA, find matching tag in one of 4 ways.
 * Duplicate this in next way modulo 4 (no# of ways).
 * Register Usage:
 *	%o0 = paddr
 */
#if defined(lint)
/*ARGSUSED*/
uint64_t
pn_wr_dup_l2_tag(uint64_t paddr)
{
	return (0);
}
#else
	.align  256
	ENTRY(pn_wr_dup_l2_tag)

	mov 	0, %o4				! %o4 = way-ctr 
	! format way0 tag-index from paddr
	!
	set 	PN_L2_INDEX_MASK, %o2
	and 	%o0, %o2, %o2			! %o2=PA<18:6>:l2 way0 tag-index

	! format l2 tag comparison PA<42:19>
	sllx	%o0, 21, %o3
	srlx	%o3, 40, %o3			! %o3=PA<42:19>:tag cmp value

	! %o0 = paddr
	! %o2 = l2 tag-index for way0
	! %o3 = tag compare value
	! %o4 = way-ctr
	! %o1 = scratch
	! %o5 = scratch
	
1:	! fetch & compare tag
	membar 	#Sync
	ldxa   	[%o2]ASI_L2_TAG, %o1		! %o1 = tag-data regval
	membar 	#Sync				! for current way.	
	sllx    %o1, 21, %o5                    ! format the tag data content
	srlx    %o5, 40, %o5                    ! for tag comparison
	cmp    	%o5, %o3
	bne	3f				! not equal - fetch next way
	nop

	! tag-match:
	! %o1 = current-way tag data 
	! %o2 = current-way tag-index
	! If tag-match in way3 (way-ctr == max-ways-1)
	!    set %o2 = dup-tag for way0 in branch delay slot. 
	! Else, annul delay slot && set %o2 = next way tag-index.
	!
	set 	PN_L2_INDEX_MASK, %o5		! %o5=way0 tag-index.
	cmp	%o4, PN_L2_NWAYS-1		! match in (max-ways-1)?
	be,a	2f				! set %o2=way0 tag index.
	and 	%o5, %o2, %o2			! .

	! match in way{0,1,2} : set %o2=way{1,2,3} tag-index
	set 	PN_L2_WAY_INCR, %o5		! add mask for next way
	add 	%o2, %o5, %o2			! %o2 = tag-index 

2:
	! We have matched on a tag in a way & fetched next way index
	! modulo max-ways, where we intend to dup tag.
	! Dup the original tag contents in next way.
	! Store complete reg val (tag+ecc+lru+state) 
 	! that we saved aside earlier.
	! Also, turn on hw_ecc_gen_en bit<22> in ASI addr.
	! This is needed to generate a 'clean hit' in the L2.
	! Otherwise, we just end up with a tag UE/CE.	 
	! Subsequent load/store should trigger dup tag IERR.
	!
	! %o1 = tag-data content for way we matched tag
	! %o2 = tag-index for next-way%4(way we intend to dup tag)
	! %o3 = scratch
	!
	mov	1, %o3
	sllx	%o3, 22, %o3			! set l2:hw_gen_ecc<22>
	or	%o2, %o3, %o2			! in tag-index.
	stxa	%o1, [%o2]ASI_L2_TAG		! store regval from previous
	membar 	#Sync				! way,
	ba	4f				! and exit.
	nop

3:	! increment way ; set tag-index for next way.
	set 	PN_L2_WAY_INCR, %o5		! set tag-index for next way
	add	%o4, 1, %o4			! incr. way-ctr.
	cmp	%o4, PN_L2_NWAYS		! reached max-ways? 
	bl,a 	1b
	add 	%o2, %o5, %o2			! %o2 = next-way tag index.

	! failed to match in any way; set default fail pattern.
	set     0xfeccf, %o0

4:
	retl
	nop
        SET_SIZE(pn_wr_dup_l2_tag)
#endif  /* lint */

/*
 * pn_wr_ill_l2_tag()
 * Induce Illegal State Internal error in L2 tag.
 * Find a matching tag in a way for PA & force state
 * to illegal: 3'b111 (Reserved).
 * Next load/store should result in protocol error.
 * Register Usage:
 *	%o0 = paddr
 */
#if defined(lint)
/*ARGSUSED*/
uint64_t
pn_wr_ill_l2_tag(uint64_t paddr)
{
	return (0);
}
#else
	.align  64
	ENTRY(pn_wr_ill_l2_tag)
	mov 	0, %o4				! %o4 = way-ctr 
	! format paddr for tag fetch
	!
	set 	PN_L2_INDEX_MASK, %o2
	and 	%o0, %o2, %o2			! %o2=PA<18:6>:l2_tag_addr,way0

	! format paddr for tag comparison PA<42:19>
	sllx	%o0, 21, %o3
	srlx	%o3, 40, %o3			! %o3 = PA<42:23>:tag cmp value
1:
	membar 	#Sync
	ldxa   	[%o2]ASI_L2_TAG, %o5		! fetch tag for comparison
	membar 	#Sync
	sllx    %o5, 21, %o5                    ! format the tag data content
	srlx    %o5, 40, %o5                    ! tag-data<42:19> for tag
	cmp    	%o5, %o3			! compare.
	bne,a 	2f				! not equal? : fetch  next way   
	nop

	! Comparison succeeded; fetch & corrupt state
	! set hw_gen_ecc bit<22> so we get a 'clean hit'
	! on subsequent load/store to trigger the error
	!
	ldxa	[%o2]ASI_L2_TAG, %o5		! reload tag/ecc
	mov	1, %o3
	sllx	%o3, 22, %o3			! set hw_gen_ecc<22>
	or	%o2, %o3, %o2
	or 	%o5, 0x7, %o5			! set state bits<2:0> to 3'b111
	stxa	%o5, [%o2]ASI_L2_TAG		! write back regval
	membar 	#Sync
	ba	3f
	nop
	
2:	! fetch next way
	
	set 	PN_L2_WAY_INCR, %o5		! add mask for next way
	add 	%o2, %o5, %o2			! %o2=l2 tag addr for next way

	add	%o4, 1, %o4
	cmp	%o4, PN_L2_NWAYS		! reached max-ways? 
	bl,a 	1b				! no; check next way
	nop

	set     0xfeccf, %o0			! default/fail return
3:
	retl
	nop
	SET_SIZE(pn_wr_ill_l2_tag)
#endif  /* lint */

/*
 * pn_wr_dup_l3_tag()
 * Induce L3 duplicate tag protocol error(IERR).
 * Given a PA, find matching tag in one of 4 ways.
 * Duplicate this in another way modulo 4 (no# of ways).
 * Register Usage:
 *	%o0 = paddr
 */
#if defined(lint)
/*ARGSUSED*/
uint64_t
pn_wr_dup_l3_tag(uint64_t paddr)
{
	return (0);
}
#else
	.align  256
	ENTRY(pn_wr_dup_l3_tag)

	mov 	0, %o4				! %o4 = way-ctr 
	! setup tag-index from paddr
	!
	set 	PN_L3_TAG_RD_MASK, %o2
	and 	%o0, %o2, %o2			! %o2=PA<22:6>:l3 way0 tag-index

	! format paddr for l3 tag comparison PA<42:23>
	sllx	%o0, 21, %o3
	srlx	%o3, 44, %o3			! %o3=PA<42:23>:tag cmp value

	! %o0 = paddr
	! %o2 = l3 tag-index for way0
	! %o3 = tag compare value
	! %o4 = way-ctr
	! %o1 = scratch
	! %o5 = scratch
	
1:	! fetch & compare tag
	membar 	#Sync
	ldxa   	[%o2]ASI_EC_DIAG, %o1		! %o1 = tag-data regval
	membar 	#Sync				! for current way.	
	sllx    %o1, 20, %o5                    ! format the tag data content
	srlx    %o5, 44, %o5                    ! for tag comparison
	cmp    	%o5, %o3
	bne	3f				! not equal - fetch next way
	nop

	! tag-match:  check way-ctr to see if we've reached
	! (max ways-1): if so, roll back to way0
	! %o1 = current-way tag data 
	! %o2 = current-way tag-index
	! If tag-match in way3, set %o2 = dup-tag for way0
	!    in branch delay slot. 
	! Else, annul delay slot && set %o2 = next way tag-index.
	!
	set 	PN_L3_TAG_RD_MASK, %o5		! %o5=way0 tag-index.
	cmp	%o4, PN_L3_NWAYS-1		! match in (max-ways-1)?
	be,a	2f				! set %o2=way0 tag index.
	and 	%o5, %o2, %o2			! .

	! match in way{0,1,2} : set %o2=way{1,2,3} tag-index
	set 	PN_L3_WAY_INCR, %o5		! add mask for next way
	add 	%o2, %o5, %o2			! %o2 = tag-index 

2:
	! We have matched on a tag in a way & fetched next way index
	! modulo max-ways, where we intend to dup tag.
	! Dup the original tag contents in next way.
	! Store complete reg val (tag+ecc+lru+state) 
 	! that we saved aside earlier.
	! Also, turn on hw_ecc_gen_en bit<22> in ASI addr.
	! This is needed to generate a 'clean hit' in the L3.
	! Otherwise we just end up with a tag UE/CE.	 
	! 
	! %o1 = tag-data content for way we matched tag
	! %o2 = tag-index for next-way%4(way we intend to dup tag)
	! %o3 = scratch
	!
	mov	1, %o3
	sllx	%o3, 25, %o3			! set l3:hw_gen_ecc<25>
	or	%o2, %o3, %o2			! in tag-index.
	stxa	%o1, [%o2]ASI_EC_DIAG		! store regval from previous
	membar 	#Sync				! way,
	ba	4f				! and exit.
	nop
	
3:	! increment way ; set tag-index for next way.
	set 	PN_L3_WAY_INCR, %o5		! set tag-index for next way
	add	%o4, 1, %o4			! incr. way-ctr.
	cmp	%o4, PN_L3_NWAYS		! reached max-ways? 
	bl,a 	1b
	add 	%o2, %o5, %o2			! %o2 = next-way tag index.

	! failed to match in any way; set default fail pattern.
	set     0xfeccf, %o0
4:
	retl
	nop
        SET_SIZE(pn_wr_dup_l3_tag)
#endif  /* lint */

/*
 * pn_wr_ill_l3_tag()
 * Induce Illegal State Internal error in L3 tag.
 * Find a matching tag in a way for PA & force state
 * to illegal: 3'b111 (Reserved).
 * Next load/store should result in protocol error.
 * Register Usage:
 *	%o0 = paddr
 */
#if defined(lint)
/*ARGSUSED*/
uint64_t
pn_wr_ill_l3_tag(uint64_t paddr)
{
	return (0);
}
#else
	.align  64
	ENTRY(pn_wr_ill_l3_tag)
	mov 	0, %o4				! %o4=way-ctr 
	! format paddr for tag fetch
	!
	set 	PN_L3_TAG_RD_MASK, %o2
	and 	%o0, %o2, %o2			! %o2=PA<22:6>:l3_tag_addr,way:0

	! format paddr for tag comparison PA<42:23>
	sllx	%o0, 21, %o3
	srlx	%o3, 44, %o3			! %o3=PA<42:23>:tag cmp value
1:
	membar 	#Sync
	ldxa   	[%o2]ASI_EC_DIAG, %o5		! fetch tag for compare
	membar 	#Sync
	sllx    %o5, 20, %o5                    ! format tag data content
	srlx    %o5, 44, %o5                    ! reg<43:24> for tag compare
	cmp    	%o5, %o3
	bne,a 	2f				! not equal?:fetch next way   
	nop

	! Comparison succeeded; fetch & corrupt state
	! set hw_gen_ecc bit<22> so we get a 'clean hit'
	! subsequent load/store will trigger the 
	! l3 illegal state IERR.
	!
	ldxa	[%o2]ASI_EC_DIAG, %o5		! reload tag/ecc
	mov	1, %o3
	sllx	%o3, 25, %o3			! set hw_gen_ecc<25>
	or	%o2, %o3, %o2
	or 	%o5, 0x7, %o5			! state bits<2:0> = 3'b111
	stxa	%o5, [%o2]ASI_EC_DIAG		! write back regval
	membar 	#Sync
	ba	3f
	nop
	
2:	! fetch next way
	
	set 	PN_L3_WAY_INCR, %o5		! add mask for next way
	add 	%o2, %o5, %o2			! %o2=l3 tag addr;next way

	add	%o4, 1, %o4
	cmp	%o4, PN_L3_NWAYS		! reached max-ways? 
	bl,a 	1b				! no; check next way
	nop

	set     0xfeccf, %o0			! default/fail return
3:
	retl
	nop
	SET_SIZE(pn_wr_ill_l3_tag)
#endif /* lint */

/*
 * The following macro sets either the L2 or L3 into split mode where
 * one core sees and uses only it's half of the shared L2 or L3 cache.
 * The one below places the cache into the normal fully enabled  mode.
 *
 * When split core 0 uses ways 0 and 1, and core 1 uses ways 2 and 3
 * for all line replacements in the L2 or L3 caches.
 */
#define PN_L2L3_SPLIT_EN(r0, r1, asi, shift)       \
        ldxa	[%g0]asi, r0;   \
	mov	1, r1;		\
	sllx	r1, shift, r1;	\
	or	r0, r1, r0;	\
        stxa	r0, [%g0]asi

#define PN_L2L3_UNSPLIT_EN(r0, r1, asi, shift)       \
        ldxa	[%g0]asi, r0;   \
	mov	1, r1;		\
	sllx	r1, shift, r1;	\
	andn	r0, r1, r0;	\
        stxa	r0, [%g0]asi

/*
 * The following two routines are access entry points for the above macros.
 * They allow the correct values to be sent to the macros from the C-level
 * calling code to set the specified cache into split or full modes.
 */
#if defined(lint)
/*ARGSUSED*/
void
pn_split_cache(int l2l3_flag)
{}
#else
        .align  64
        ENTRY(pn_split_cache)
	mov	1, %o1			! set l2 or l3 cache?
	btst	%o0, %o1		! .
	bnz	pn_split_l3		! l2l3_flag == 0 ? l2 : l3
	  nop				! .

pn_split_l2:
	membar	#Sync
	PN_L2L3_SPLIT_EN(%o3, %o4, ASI_L2CACHE_CTRL, PN_L2_SPLIT_EN_SHIFT)
	ba	1f
	  nop

pn_split_l3:
	membar	#Sync
	PN_L2L3_SPLIT_EN(%o3, %o4, ASI_EC_CTRL, PN_L3_SPLIT_EN_SHIFT)
1:
	membar	#Sync
	retl
	  nop
        SET_SIZE(pn_split_cache)
#endif  /* lint */

#if defined(lint)
/*ARGSUSED*/
void
pn_unsplit_cache(int l2l3_flag)
{}
#else
        .align  64
        ENTRY(pn_unsplit_cache)
	mov	1, %o1			! set l2 or l3 cache?
	btst	%o0, %o1		! .
	bnz	pn_unsplit_l3		! l2l3_flag == 0 ? l2 : l3
	  nop				! .

pn_unsplit_l2:
	membar	#Sync
	PN_L2L3_UNSPLIT_EN(%o3, %o4, ASI_L2CACHE_CTRL, PN_L2_SPLIT_EN_SHIFT)
	ba	1f
	  nop

pn_unsplit_l3:
	membar	#Sync
	PN_L2L3_UNSPLIT_EN(%o3, %o4, ASI_EC_CTRL, PN_L3_SPLIT_EN_SHIFT)
1:
	membar	#Sync
	retl
	  nop
        SET_SIZE(pn_unsplit_cache)
#endif	/* lint */

/*
 * Utility routine to set the state bits of an L2 cacheline.
 * The state can be set to NA (not available) in order to take the line
 * out of active use in the cache.  By setting other cachelines to NA
 * target data can be installed into the cache in a specific way (location).
 * Other states can also be set via this routine since the state is an inpar.
 *
 * Also note that the tag is returned by this routine so that cachelines
 * which are already set to NA are not returned to use by the EI.
 *
 * Register usage:
 *
 *	%o0 - paddr
 *	%o1 - target way to set or unset NA
 *	%o2 - value to set tag state bits to (5 = NA, 0 = INV, etc.)
 *	%o3 - tmp
 *	%o4 - address to use with tag ASI
 *	%o5 - unused
 */
#if defined(lint)
/*ARGSUSED*/
uint64_t
pn_set_l2_na(uint64_t paddr, uint_t targ_way, uint_t state_value)
{
        return (0);
}
#else
        .align  64
        ENTRY(pn_set_l2_na)

	set	PN_L2_INDEX_MASK, %o3		! mask the paddr to get index
	and	%o0, %o3, %o4			! %o4 = masked paddr

	sllx	%o1, PN_L2_WAY_SHIFT, %o3	! shift target way to way bits
	or	%o4, %o3, %o4			! combine index and way bits

	mov	1, %o3				! set the ecc gen bit
	sllx	%o3, PN_L2_HW_ECC_SHIFT, %o3	! .
	or	%o4, %o3, %o4			! %o4 = combined vaddr for ASI
	membar	#Sync

	/*
	 * Also clear the tag when setting it NA or INV to avoid ECC errors.
	 */
	ldxa	[%o4]ASI_L2_TAG, %o0		! return existing tag
	stxa	%o2, [%o4]ASI_L2_TAG		! write tag with intended state
	membar	#Sync

	retl
	  nop
	SET_SIZE(pn_set_l2_na)
#endif	/* lint */

/*
 * This routine is similar to the above pn_set_l2_na() routine but sets a
 * cacheline in the L3 to NA (not available) or to the state passed in.
 * To return a line to actice service it is set to INV.
 *
 * Register usage:
 *
 *	%o0 - paddr
 *	%o1 - target way to set or unset NA
 *	%o2 - value to set tag state bits to (5 = NA, 0 = INV, etc.)
 *	%o3 - tmp
 *	%o4 - address to use with tag ASI
 *	%o5 - unused
 */
#if defined(lint)
/*ARGSUSED*/
uint64_t
pn_set_l3_na(uint64_t paddr, uint_t targ_way, uint_t state_value)
{
        return (0);
}
#else
        .align  64
        ENTRY(pn_set_l3_na)

	set	PN_L3_TAG_RD_MASK, %o3		! mask the paddr to get index
	and	%o0, %o3, %o4			! %o4 = masked paddr

	sllx	%o1, PN_L3_WAY_SHIFT, %o3	! shift target way to way bits
	or	%o4, %o3, %o4			! combine index and way bits

	mov	1, %o3				! set the ecc gen bit
	sllx	%o3, PN_L3_HW_ECC_SHIFT, %o3	! .
	or	%o4, %o3, %o4			! %o4 = combined vaddr for ASI
	membar	#Sync

	/*
	 * Also clear the tag when setting it NA or INV to avoid ECC errors.
	 */
	ldxa	[%o4]ASI_EC_DIAG, %o0		! return existing tag
	stxa	%o2, [%o4]ASI_EC_DIAG		! write tag with intended state
	membar	#Sync

	retl
	  nop
	SET_SIZE(pn_set_l3_na)
#endif	/* lint */

/*
 * Utility routine to read and return the tag of an L2 cacheline.
 *
 * Register usage:
 *
 *	%o0 - paddr
 *	%o1 - target way to read
 *	%o2 - unused
 *	%o3 - tmp
 *	%o4 - address to use with tag ASI
 *	%o5 - unused
 */
#if defined(lint)
/*ARGSUSED*/
uint64_t
pn_rd_l2_tag(uint64_t paddr, uint_t targ_way)
{
        return (0);
}
#else
        .align  32
        ENTRY(pn_rd_l2_tag)

	set	PN_L2_INDEX_MASK, %o3		! mask the paddr to get index
	and	%o0, %o3, %o4			! %o4 = masked paddr

	sllx	%o1, PN_L2_WAY_SHIFT, %o3	! shift target way to way bits
	or	%o4, %o3, %o4			! combine index and way bits

	ldxa	[%o4]ASI_L2_TAG, %o0		! return existing tag
	membar	#Sync

	retl
	  nop
	SET_SIZE(pn_rd_l2_tag)
#endif	/* lint */

/*
 * This routine is similar to the above pn_rd_l2_tag() routine but reads a
 * cacheline in the L3 cache.
 *
 * Register usage:
 *
 *	%o0 - paddr
 *	%o1 - target way to read
 *	%o2 - unused
 *	%o3 - tmp
 *	%o4 - address to use with tag ASI
 *	%o5 - unused
 */
#if defined(lint)
/*ARGSUSED*/
uint64_t
pn_rd_l3_tag(uint64_t paddr, uint_t targ_way)
{
        return (0);
}
#else
        .align  64
        ENTRY(pn_rd_l3_tag)

	set	PN_L3_TAG_RD_MASK, %o3		! mask the paddr to get index
	and	%o0, %o3, %o4			! %o4 = masked paddr

	sllx	%o1, PN_L3_WAY_SHIFT, %o3	! shift target way to way bits
	or	%o4, %o3, %o4			! combine index and way bits

	ldxa	[%o4]ASI_EC_DIAG, %o0		! return existing tag
	membar	#Sync

	retl
	  nop
	SET_SIZE(pn_rd_l3_tag)
#endif	/* lint */
