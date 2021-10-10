/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Niagara (UltraSPARC-T1) error injector assembly and hyperprivileged
 * assembly routines.
 *
 * NOTE: the functions in this file are grouped according to type and
 *	 are therefore not in alphabetical order unlike other files.
 */

#include <sys/memtest_v_asm.h>
#include <sys/memtest_ni_asm.h>

/*
 * Coding rules for Niagara routines that run in hypervisor mode:
 *	1) routines may not block (since hypervisor may not block).
 *	2) must be single-threaded.
 *	3) may not damage guest state (the out/local/in registers)
 *	4) may use global reg set (MAXPGL=2 for SV, MAXGL=3 for HV)
 *	5) may use both hypervisor scratchpad regs (via ASI_HSCRATCHPAD)
 *		- except HSCRATCH0 contains the hstruct addr by convention.
 *	6) if an hcall trap is used can use %o0-%o5 like a leaf routine
 *	   since the %o regs are considered volatile across the call.
 *		- the trap to hv disables interrupts in pstate automatically
 *	7) all STORES to ASIs must be followed by a membar #Sync
 *		- CSR regs do NOT require this but still won't hurt.
 *	8) all routines and macros CANNOT use absolute labels.  All labels
 *	   must be relative.
 *	9) L1 caches are inclusive (all L1 cache-lines are also found in the
 *	   L2 cache) so scrubbing L2$ also invalidates d$ and i$.
 */

/*
 * Niagara cache details (ALL are PIPT)
 *
 *	Cache	Size	Ways	sz/way	linesz	instrs/line
 *	---------------------------------------------------
 *	L2	3MB	12	256KB	64B	16
 *	i$	16KB	4	4KB	32B	8
 *	d$	8KB	4	2KB	16B	4
 */

/*
 * Enable/disable the storing of DEBUG values into the debug mem buffer
 * for different test types.
 */
/* #define	MEMDEBUG_BUFFER	1 */
/* #define	L1_DEBUG_BUFFER	1 */
/* #define	L2_DEBUG_BUFFER	1 */

/*
 * Print MACROs to use while running in HV mode.  All these must be defined
 * as MACROs and must not call or branch to other routines symbolically.
 *
 * Remember to be careful with the relative branches when using these MACROs
 * in code which also has relative branches (can get number overlap).
 *
 *	%g1-%g3 (clobbered)
 */
#define	PRINT(s)			\
	ba	1f			;\
	  rd	%pc, %g1		;\
2:	.asciz	s			;\
	.align	4			;\
1:					;\
	add	%g1, 4, %g1		;\
	setx	HV_UART, %g3, %g2	;\
3:					;\
	ldub	[%g2 + LSR_ADDR], %g3	;\
	btst	LSR_THRE, %g3		;\
	bz	3b			;\
	  nop				;\
4:					;\
	ldub	[%g1], %g3		;\
	cmp	%g3, 0			;\
	inc	%g1			;\
	bne,a,pt %icc, 5f		;\
	  stb	%g3, [%g2]		;\
	ba,a	6f			;\
	  nop				;\
5:					;\
	ldub	[%g2 + LSR_ADDR], %g3	;\
	btst	LSR_TEMT, %g3		;\
	bz	5b			;\
	  nop				;\
	ba,a	4b			;\
	  nop				;\
6:

/*
 * See above comment.
 *
 *	%g1-%g5 (clobbered)
 */
#define	PRINTX(x)			\
	mov	x, %g1			;\
	setx	HV_UART, %g3, %g2	;\
1:					;\
	ldub	[%g2 + LSR_ADDR], %g4	;\
	btst	LSR_THRE, %g4		;\
	bz	1b			;\
	  nop				;\
	mov	60, %g3			;\
	ba	2f			;\
	  rd	%pc, %g4		;\
	.ascii	"0123456789abcdef"	;\
	.align	4			;\
2:					;\
	add	%g4, 4, %g4		;\
1:					;\
	srlx	%g1, %g3, %g5		;\
	and	%g5, 0xf, %g5		;\
	ldub	[%g4 + %g5], %g5	;\
	stb	%g5, [%g2]		;\
	subcc	%g3, 4, %g3		;\
	bnz	2f			;\
	  nop				;\
					;\
	and	%g1, 0xf, %g5		;\
	ldub	[%g4 + %g5], %g5	;\
	stb	%g5, [%g2]		;\
	ba,a	3f			;\
	  nop				;\
2:					;\
	ldub	[%g2 + LSR_ADDR], %g5	;\
	btst	LSR_TEMT, %g5		;\
	bz	2b			;\
	  nop				;\
	ba,a	1b			;\
	  nop				;\
3:

/*--------------------- end of general / start of mem routines -------------*/

/*
 * This routine is used to corrupt the ecc bits of external memory (DRAM).
 *
 * The ECC bits are corrupted by using the ECC XOR mask available
 * in the DRAM_ERROR_INJECT_REG which when enabled will XOR the bits
 * set in the ECCMASK field with the HW computed ECC on write.
 *
 * There is one DRAM_ERROR_INJECT_REG in each of the four DRAM banks,
 * (channels) the bank register sets are available at offsets of 4096.
 *
 * NOTE: the entire L2$ should be flushed immediately before this function.
 *
 * NOTE: paddrs with bits <21:18> set to 0xc-0xf do not need to be handled
 *	 specially.  PRM table 18.5.1 indicates ways 0xc-0xf map to 0x4-0x7
 *	 (HW clears the 8 bit which makes sense).
 *
 * NOTE: to use the ASI_REAL* which are non-allocating in L1$ we would need to
 *	 get our corruption address into the TLB (with R bit set) b/c it uses
 *	 the TLB translation even in HPRIV mode.  This is no good because the
 *	 translation will bring lines into the L2$.  Since the caches are
 *	 inclusive (anything in L1$ is in L2$) we can just do a normal address
 *	 access for this test (we don't care if it allocates in the L1$).
 *
 * DRAM_ERROR_INJ_REG (0x290)
 *
 * +-----------+-----+-------+----------+----------+
 * | Resverved | Enb | SShot | Reserved | ECC Mask |
 * +-----------+-----+-------+----------+----------+
 *     63:32     31     30      29:16       15:0
 */

/*
 * Prototype:
 *
 * int ni_inj_memory(uint64_t paddr, uint_t eccmask, uint64_t *debug);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - eccmask and sshot
 *	%o3 - register offset
 *	%o4 - pointer to debug buffer (debug)
 *	%g1 - temp then address of L2_CTL_REG
 *	%g2 - temp then saved L2_CTL_REG contents
 *	%g3 - temp then PA mask for the flush
 *	%g4 - temp then data read addr (data not used)
 *	%g5 - temp
 *	%g6 - addr of matching DRAM_ERROR_INJECT_REG
 *	%g7 - L2$ access values (debug)
 */
#if defined(lint)
/* ARGSUSED */
int
ni_inj_memory(uint64_t paddr, uint_t eccmask, uint64_t *debug)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(ni_inj_memory)

	/*
	 * Build DRAM EI register address.
	 */
	mov	0x97, %g4			! set DRAM select bits
	sllx	%g4, 32, %g4			! .
	or	%g4, DRAM_ERROR_INJ_REG, %g4	! add injection offset
	add	%g4, %o3, %g6			! add register offset
						!   %g6 = complete EI reg addr

#ifdef MEMDEBUG_BUFFER
	stx	%o1, [%o4 + 0x0]		! paddr
	stx	%g6, [%o4 + 0x8]		! addr of DRAM_EI_REG
#endif
	/*
	 * Determine the L2 flush addr for the specified paddr.
	 */
	set	0x3ffff8, %g3			! PA mask for bank, set, way
	and	%g3, %o1, %g3			! PA[21:6] req'd, is PA[21:3]

	setx	L2FLUSH_BASEADDR, %g5, %g4	! put flush BA into a reg
	add	%g3, %g4, %g3			! %g3 = the flush addr to use

	/*
	 * Put L2 into direct mapped mode, this bank only.
	 */
	mov	0xa9, %g1			! %g1=L2_CTL_REG=0xa9.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, 0xc0, %g2			! get reg offset from paddr
	or	%g1, %g2, %g1			! include offset in reg addr

	ldx	[%g1], %g2			! %g2=prev L2_CTL_REG contents
	or	%g2, L2CR_DMMODE, %g5		! .
	stx	%g5, [%g1]			! .
	membar	#Sync				! (PRM says not needed)

#ifdef MEMDEBUG_BUFFER
	stx	%g1, [%o4 + 0x18]		! addr of L2_CTL_REG
	stx	%g5, [%o4 + 0x20]		! new contents of L2_CTL_REG
	stx	%g3, [%o4 + 0x28]		! addr of flush value to use!
#endif

#ifdef MEMDEBUG_BUFFER
	/*
	 * DEBUG: Build a register for L2$ DEBUG accesses.
	 */
	mov	0xa1, %g4			! set select bits
	sllx	%g4, 32, %g4			! .
	set	0x3ffff8, %g7			! mask paddr to get $line
	and	%o1, %g7, %g7			!  is PA[21:3]
	or	%g7, %g4, %g7			! %g7 = L2_DIAG_DATA
#endif

	ldx	[%o1], %g4			! touch the data
	
#ifdef MEMDEBUG_BUFFER
	stx	%g4, [%o4 + 0x30]		! contents of mem location
	stx	%o2, [%o4 + 0x38]		! value written to DRAM_EI_REG
#endif

	ba	3f				! branch to fill prefetch buf
	  mov	%g0, %o0			! set return value to PASS

	/*
	 * Critical section, must fit on single L2$ line.
	 */
	.align	64
1:
	ldx	[%o1], %g4			! load addr to ensure is in $s
	stx	%g4, [%o1]			! store to put line in M state
	membar	#Sync				! .

#ifdef	MEMDEBUG_BUFFER
	ldx	[%g7], %g4			! read back L2 data (via reg)
	sllx	%g4, 1, %g4			! shift it so it's readable
	stx	%g4, [%o4 + 0x40]		!  should not invoke the error
#endif

	stx	%o2, [%g6]			! write value to DRAM EI reg
	membar	#Sync				!  (error on next DRAM store)
	ldx	[%g6], %o2			! read back reg to ensure it
	membar	#Sync				!  completed (as per PRM)

	ldx	[%g3], %g5			! access flush addr to flush
	membar	#Sync				!  data out to DRAM with error

	/*
	 * Due to 2-channel mode HW limitation, clear ecc mask if sshot mode.
	 */
	mov	1, %g4				! create mask for sshot bit
	sllx	%g4, EI_REG_SSHOT_ENB_SHIFT, %g4 ! .
	and	%g4, %o2, %g4			! check if sshot is set
	cmp	%g4, %g0			! .
	bnz,a	%xcc, 4f			! if it is set then
	  stx	%g0, [%g6]			!   clear the DRAM EI reg
4:

#ifdef	MEMDEBUG_BUFFER
	ldx	[%g6], %o2			! read back reg to see if clear
	membar	#Sync		
	stx	%o2, [%o4 + 0x48]		! new val read from DRAM_EI_REG
	ldx	[%g7], %g4			! read back L2 data (via reg)
	sllx	%g4, 1, %g4			! shift it so it's readable
	stx	%g4, [%o4 + 0x50]		!  should not invoke the error
#endif

	stx	%g2, [%g1]			! restore this banks L2$ mode
	membar	#Sync				!  (will flush L2$ buffers)

	done					! return PASS

	/*
	 * Fill up the prefetch buffer (full L2$ line).
	 */
	.align	64
3:
	nop; nop; nop; nop; nop; nop; nop;
	nop; nop; nop; nop; nop; nop; nop;
	ba	1b
	  nop

	SET_SIZE(ni_inj_memory)
#endif	/* lint */

/*--------------------- end of mem / start of L2$ routines -----------------*/

/*
 * This routine is used to corrupt the data or the data ecc bits of an L2
 * cache line determined by the paddr ONLY when the cache line is not for
 * an instruction access.  The L2_DIAG_DATA register address is built by
 * this routine, bits [7:6] of the paddr select the cache bank.
 *
 * Note that this routine expects a 64-bit aligned xorpat and shifts it
 * down if the corruption is to be in the upper 32-bit word.  This means
 * that the upper and lower words cannot be corrupted at the same time
 * (and the lower value takes precidence).  This is due to HW limitations.
 *
 * The L2 cache is 3MB with a linesize of 64B which yeilds 49152 (0xC000)
 * lines split into 4096 (0x1000) lines per each of the 12 ways.
 *
 * Note also that by setting and restoring the L2$ DM mode has the effect
 * of clearing all the L2$ write buffers.  This occurs even if the mode does
 * not actually change since it is triggered by the control reg write.
 *
 * NOTE	there are a number of problems involved with these L2$ errors b/c
 *	the normal method of loading a value into the L2$ and have it be
 *	non-allocating in the L1$ is gone (ASI_MEM is now ASI_REAL_MEM).
 *	So we have three options:
 *		1) Use the ASI_REAL* but since this requires a R->P trans
 *		   preload the TLB with the value we are going to use.
 *		   (either in HV code or have the SV do an access since the
 *		   SV is supposed to be running in real mode anyway, this
 *		   however means we will have to remove the L1$ entry).
 *		2) Do a normal access that bypasses the TLB (since in HV) but
 *		   allocates in the L1$.  Then go and invalidate that line in
 *		   the L1$.  See if turning the L1$ off will work.  This is
 *		   complicated by the $ directory... see PRM sec. 18.3.2
 *		   but that shouldn't matter since we are just invalidating.
 *		   (see PRM B.2 at the top)
 *		3) Use BLK ld/st accesses to do it, but these require the FP
 *		   unit to be on and needs accesses to the fp regs which is
 *		   annoying.  The SV code could set this up for us though.
 *		   (would use asi's such as ASI_BLK_P)
 *
 * Method 2 was chosen for this routine and the cache directory is being
 * ignored.  The tests are generally reliable using this method.
 *
 * L2_DIAG_DATA (offset 0x0) Access (stride 64)
 *
 * +-------+--------+-------+---------+-----+-----+------+------+-------+
 * | Rsvd0 | Select | Rsvd1 | OddEven | Way | Set | Bank | Word | Rsvd2 |
 * +-------+--------+-------+---------+-----+-----+------+------+-------+
 *   63:40   39:32    31:23     22     21:18  17:8  7:6    5:3     2:0
 *
 * Data returned (32-bit half word) as DATA[38:7], ECC[6:0]
 */

/*
 * Prototype:
 *
 * int ni_inj_l2cache_data(uint64_t paddr, uint64_t xorpat, uint_t checkflag);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - xorpat (clobbered)
 *	%o3 - check-bit flag _or_ debug buffer (debug)
 *	%o4 - way increment value (not an inpar)
 *	%g1 - temp then address of L2_CTL_REG
 *	%g2 - temp then address for data read/write
 *	%g3 - temp then data read from cache
 *	%g4 - saved L2_CTL_REG contents
 *	%g5 - counter for L1$ clear
 *	%g6 - tag to use for L1 cache flush
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
ni_inj_l2cache_data(uint64_t paddr, uint64_t xorpat, uint_t checkflag)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(ni_inj_l2cache_data)

	/*
	 * Generate register addresses to use below.
	 */
	cmp	%o2, %g0			! if xorpat in upper word shift
	bnz	%icc, 1f			!   down since 32-bit access
	  nop					! .
	srlx	%o2, 32, %o2			! corrupt next 32-bits
	add	%o1, 4, %o1			! .
1:
	and	%o1, 4, %g3			! extract 32B paddr boundary
	sllx	%g3, 20, %g3			! move result to OddEven field
	andn	%o1, 0x7, %o1			! align paddr for asi access

#ifdef	L2_DEBUG_BUFFER
	stx	%o1, [%o3 + 0x0]		! aligned paddr
	stx	%o2, [%o3 + 0x8]		! xor pat
#else
	cmp	%o3, %g0			! if check-bit flag == 0
	bz,a,pt	%xcc, 2f			! shift the xorpat to data bits
	  sllx	%o2, 7, %o2			! .
#endif

	/*
	 * Build the register values for L1$ access.
	 * The d$ way bits are bits 12:11.
	 */
2:
	and	%o1, 0x7f0, %g6			! %g6 = PA[10:4] for L1 tag asi
	mov	1, %o4				! %o4 = L1 way inc. value
	sllx	%o4, 11, %o4			! .
	mov	4, %g5				! %g5 = number of L1 ways

	/*
	 * Build the register values for L2$ accesses.
	 */
	mov	0xa1, %g4			! set select bits
	sllx	%g4, 32, %g4			! .
	set	0x3ffff8, %g2			! mask paddr to get $line
	and	%o1, %g2, %g2			!  is PA[21:3]
	or	%g2, %g3, %g2			! or in the OddEven bit
	or	%g2, %g4, %g2			! %g2 = L2_DIAG_DATA

#ifdef	L2_DEBUG_BUFFER
	stx	%g2, [%o3 + 0x10]		! addr for L2_DIAG_DATA
#endif

	/*
	 * Put L2 into direct mapped mode, this bank only.
	 */
	mov	0xa9, %g1			! %g1=L2_CTL_REG=0xa9.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, 0xc0, %g3			! get reg offset from paddr
	or	%g1, %g3, %g1			! include offset in reg addr

	ldx	[%g1], %g4			! %g4=prev L2_CTL_REG contents
	or	%g4, L2CR_DMMODE, %g3		! .
	stx	%g3, [%g1]			! .
	membar	#Sync				! (PRM says not needed)

#ifdef	L2_DEBUG_BUFFER
	stx	%g1, [%o3 + 0x18]		! addr of L2_CTL_REG
	stx	%g3, [%o3 + 0x20]		! new contents of L2_CTL_REG
#endif

	ba	3f				! branch to aligned code
	  nop					! .

	.align	64
3:
	ldx	[%o1], %g3			! get data into L2$ and L1$
	membar	#Sync				! .

#ifdef	L2_DEBUG_BUFFER
	stx	%g3, [%o3 + 0x28]		! contents of mem location
#endif

	ldx	[%g2], %g3			! read L2$ data to corrupt
						!   via L2_DIAG_DATA reg
	xor	%g3, %o2, %g3			! corrupt data or ecc bits

#ifdef	L2_DEBUG_BUFFER
	stx	%g3, [%o3 + 0x30]		! data after bit(s) flipped
#endif

	stx	%g3, [%g2]			! write the data back to L2$
	membar	#Sync				!   via L2_DIAG_DATA reg

#ifdef	L2_DEBUG_BUFFER
	ldx	[%g2], %g3			! read back the data (via reg)
	sllx	%g3, 1, %g3			! shift it so it's readable
	stx	%g3, [%o3 + 0x40]		!  should not invoke the error
#endif

	/*
	 * Clear the the L1$ entry so access does not hit there.
	 */
4:
#ifdef	L2_DEBUG_BUFFER
	ldxa	[%g6]ASI_DC_TAG, %o2		! look at the d$ tags
	sllx	%o2, 10, %o2			! move it up for compare
	sllx	%g5, 3, %o0			! try to get all four
	add	%o3, %o0, %o0			! add offset to debug_buffer
	stx	%o2, [%o0 + 0x50]		! store the tag
#endif
	stxa	%g0, [%g6]ASI_DC_TAG		! clear all four ways of L1
	sub	%g5, 1, %g5			! .
	brnz,a	%g5, 4b
	  add	%g6, %o4, %g6			! go to next way

	stx	%g4, [%g1]			! restore this banks L2$ mode
						!   which will flush L2$ buffers
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_inj_l2cache_data)
#endif	/* lint */

/*
 * This routine is used to corrupt the data or the data ecc bits of an L2
 * cache line which is an instruciton determined by its paddr.
 * The L2_DIAG_DATA register address is built by this routine, bits [7:6]
 * of the paddr select the cache bank.
 *
 * Note that this routine expects a 64-bit aligned xorpat and shifts it
 * down if the corruption is to be in the upper 32-bit word.  This means
 * that the upper and lower words cannot be corrupted at the same time
 * (and the lower value takes precidence).  This is due to HW limitations.
 *
 * The method used is similar to the above ni_inj_l2cache_data() routine.
 */

/*
 * Prototype:
 *
 * int ni_inj_l2cache_instr_data(uint64_t paddr, uint64_t xorpat,
 *					uint_t checkflag);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - xorpat (clobbered)
 *	%o3 - checkbit flag
 *	%o4 - way increment value (not an inpar)
 *	%g1 - temp then address of L2_CTL_REG
 *	%g2 - temp then address for data read/write
 *	%g3 - temp then data read from cache
 *	%g4 - temp then saved L2_CTL_REG contents
 *	%g5 - counter for L1$ clear
 *	%g6 - tag to use for L1 cache flush
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
ni_inj_l2cache_instr_data(uint64_t paddr, uint64_t xorpat, uint_t checkflag)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(ni_inj_l2cache_instr_data)

	/*
	 * Generate register addresses to use below.
	 */
	cmp	%o2, %g0			! if xorpat in upper word shift
	bnz	%icc, 1f			!   down since 32-bit access
	  nop					! .
	srlx	%o2, 32, %o2			! corrupt next 32-bits
	add	%o1, 4, %o1			! .
1:
	and	%o1, 4, %g3			! extract 32B paddr boundary
	sllx	%g3, 20, %g3			! move result to OddEven field
	andn	%o1, 0x7, %o1			! align paddr for asi access

	/*
	 * Build the register values for L1$ access.
	 * The i$ way bits are bits 17:16.
	 */
	and	%o1, 0xfe0, %g6			! mask/shift paddr for L1 asi
	sllx	%g6, 1, %g6			! %g6 = L1 asi access addr
	mov	1, %o4				! %o4 = put way inc. value
	sllx	%o4, 16, %o4			! .
	mov	4, %g5				! %g5 = number of L1 ways

	/*
	 * Build the register values for L2$ accesses.
	 */
	mov	0xa0, %g4			! set select bits
	sllx	%g4, 32, %g4			! .
	set	0x3ffff8, %g2			! mask paddr to get $line
	and	%o1, %g2, %g2			! .
	add	%g2, %g3, %g2			! combine into complete reg
	add	%g2, %g4, %g2			! %g2 = L2_DIAG_DATA

	/*
	 * Build L2 Control Register for this bank and read value.
	 */
	mov	0xa9, %g1			! %g1=L2_CTL_REG=0xa9.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, 0xc0, %g3			! get reg offset from paddr
	add	%g1, %g3, %g1			! include offset in reg addr
	ldx	[%g1], %g4			! %g4=L2_CTL_REG contents

	ba	2f				! branch to aligned code
	  nop					! .

	.align	64
2:
	ldx	[%g2], %g3			! read L2$ data to corrupt
						!   via L2_DIAG_DATA reg
	cmp	%o3, %g0			! if check-bit flag == 0
	bz,a,pt	%xcc, 3f			! shift the xorpat to data bits
	  sllx	%o2, 7, %o2			! .
3:
	xor	%g3, %o2, %g3			! corrupt data or ecc bits
	stx	%g3, [%g2]			! write the data back to L2$
	membar	#Sync				!   via L2_DIAG_DATA reg

	/*
	 * Clear the the L1$ entry so access does not hit there.
	 */
4:
	stxa	%g0, [%g6]ASI_ICACHE_TAG	! clear all four ways of L1
	sub	%g5, 1, %g5			! .
	brnz,a	%g5, 4b
	  add	%g6, %o4, %g6			! go to next way

	stx	%g4, [%g1]			! write to banks L2_CTL_REG
						!   to flush write buffers
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_inj_l2cache_instr_data)
#endif	/* lint */

/*
 * This routine is used to corrupt the data or the data ecc bits of an L2
 * cache line determined by a byte offset.  The L2_DIAG_DATA register address
 * is built by this routine, bits [7:6] of the paddr select the cache bank.
 *
 * Note that this routine expects a 64-bit aligned xorpat and shifts it
 * down if the corruption is to be in the upper 32-bit word.  This means
 * that the upper and lower words cannot be corrupted at the same time
 * (and the lower value takes precidence).  This is due to HW limitations.
 *
 * The method used is similar to the above ni_inj_l2cache_data() routine.
 */

/*
 * Prototype:
 *
 * int ni_inj_l2phys_data(uint64_t offset, uint64_t xorpat, uint_t checkflag,
 *				uint64_t *debug);
 *
 * Register usage:
 *
 *	%o1 - byte offset
 *	%o2 - xorpat (clobbered)
 *	%o3 - checkbit flag
 *	%o4 - pointer to debug buffer (debug)
 *	%g1 - temp then address of L2_CTL_REG
 *	%g2 - temp then address for data read/write
 *	%g3 - temp then data read from cache
 *	%g4 - saved L2_CTL_REG contents
 *	%g5 - unused
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
ni_inj_l2phys_data(uint64_t offset, uint64_t xorpat, uint_t checkflag,
						uint64_t *debug)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(ni_inj_l2phys_data)

	cmp	%o2, %g0			! if xorpat in upper word shift
	bnz	%icc, 1f			!   down since 32-bit access
	  nop					! .
	srlx	%o2, 32, %o2			! corrupt next 32-bits
	add	%o1, 4, %o1			! .
1:
	and	%o1, 4, %g3			! extract 32B paddr boundary
	sllx	%g3, 20, %g3			! move result to OddEven field

	/*
	 * Build the register values for L2$ accesses.
	 */
	mov	0xa0, %g4			! set select bits
	sllx	%g4, 32, %g4			! .
	set	0x3ffff8, %g2			! mask offset to get $line
	and	%o1, %g2, %g2			! .
	or	%g2, %g3, %g2			! combine into complete reg
	or	%g2, %g4, %g2			! %g2 = L2_DIAG_DATA

	/*
	 * Put L2 into direct mapped mode, this bank only (based on offset).
	 */
	mov	0xa9, %g1			! %g1=L2_CTL_REG=0xa9.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, 0xc0, %g3			! get reg offset from offset
	add	%g1, %g3, %g1			! include offset in reg addr

	ldx	[%g1], %g4			! %g4=prev L2_CTL_REG contents
	or	%g4, L2CR_DMMODE, %g3		! .
	stx	%g3, [%g1]			! .

	ba	2f				! branch to aligned code
	  nop					! .

	.align	64
2:
	ldx	[%g2], %g3			! read data to corrupt
						!   via L2_DIAG_DATA reg
	cmp	%o3, %g0			! if check-bit flag == 0
	bz,a,pt	%xcc, 3f			! shift the xorpat to data bits
	  sllx	%o2, 7, %o2			! .
3:
	xor	%g3, %o2, %g3			! corrupt data or ecc bits
	stx	%g3, [%g2]			! write the data back to L2$
	membar	#Sync				!.

	stx	%g4, [%g1]			! restore this banks L2$ mode
						!   to flush write buffers
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_inj_l2phys_data)
#endif	/* lint */

/*
 * This routine is used to corrupt the tag or the tag ecc bits of an L2
 * cache line determined by the paddr.  The L2_DIAG_TAG register address
 * is built by this routine, bits [7:6] of the paddr select the cache bank.
 *
 * The L2 cache is 3MB with a linesize of 64B which yeilds 49152 (0xC000)
 * lines split into 4096 (0x1000) lines per each of the 12 ways.
 *
 * L2_DIAG_TAG (offset 0x4.0000.0000) Access (stride 64)
 *
 * +-------+--------+-------+-----+-----+------+------+-------+
 * | Rsvd0 | Select | Rsvd1 | Way | Set | Bank | Word | Rsvd2 |
 * +-------+--------+-------+-----+-----+------+------+-------+
 *   63:40   39:32    31:22  21:18  17:8  7:6    5:3     2:0
 *
 * Data returned as TAG[27:6], ECC[5:0] (where the TAG bits are ADDR[39:18]
 */

/*
 * Prototype:
 *
 * int ni_inj_l2cache_tag(uint64_t paddr, uint64_t xorpat, uint_t checkflag,
 *				uint64_t *debug);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - xorpat (clobbered)
 *	%o3 - checkbit flag
 *	%o4 - pointer to debug buffer (debug) and L1 increment
 *	%g1 - temp then address of L2_CTL_REG
 *	%g2 - temp then address for data read/write
 *	%g3 - temp then data read from cache
 *	%g4 - saved L2_CTL_REG contents
 *	%g5 - counter for L1$ clear
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
ni_inj_l2cache_tag(uint64_t paddr, uint64_t xorpat, uint_t checkflag,
					uint64_t *debug)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(ni_inj_l2cache_tag)

	/*
	 * Build the register values for L1$ access.
	 * The d$ way bits are bits 12:11.
	 */
	and	%o1, 0x7f0, %g6			! %g6 = PA[10:4] for L1 tag asi
	mov	1, %o4				! %o4 = L1 way inc. value
	sllx	%o4, 11, %o4			! .
	mov	4, %g5				! %g5 = number of L1 ways

	/*
	 * Build the register values for L2$ accesses.
	 */
	mov	0xa4, %g4			! set reg select bits
	sllx	%g4, 32, %g4			! .
	set	0x3fffc0, %g2			! mask paddr to get $line
	and	%o1, %g2, %g2			! combine into complete reg
	add	%g2, %g4, %g2			! %g2 = L2_DIAG_TAG

	/*
	 * Put L2 into direct mapped mode, this bank only.
	 */
	mov	0xa9, %g1			! %g1=L2_CTL_REG=0xa9.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, 0xc0, %g3			! get reg offset from paddr
	add	%g1, %g3, %g1			! include offset in reg addr

	ldx	[%g1], %g4			! %g4=prev L2_CTL_REG contents
	or	%g4, L2CR_DMMODE, %g3		! .
	stx	%g3, [%g1]			! .

	ldx	[%o1], %g3			! access to take page fault now

	ba	1f				! branch to aligned code
	  mov	%g0, %o0			! set return value to PASS

	.align	64
1:
	ldx	[%o1], %g3			! get data into L2$ and L1$
	membar	#Sync				! .

	ldx	[%g2], %g3			! read tag to corrupt
	cmp	%o3, %g0			! if check-bit flag == 0
	bz,a,pt	%xcc, 2f			! shift the xorpat to tag bits
	  sllx	%o2, 6, %o2			! .
2:
	xor	%g3, %o2, %g3			! corrupt tag or tag ecc bits
	stx	%g3, [%g2]			! write the tag back to L2$
	membar	#Sync				!.

#ifdef	L2_DEBUG_BUFFER
	ldx	[%g2], %g3			! read back the data (via reg)
	stx	%g3, [%o3 + 0x20]		!  should not invoke the error
#endif

	/*
	 * Clear the the L1$ entry so access does not hit there.
	 */
3:
	stxa	%g0, [%g6]ASI_DC_TAG		! clear all four ways of L1
	sub	%g5, 1, %g5			! .
	brnz,a	%g5, 3b
	  add	%g6, %o4, %g6			! go to next way

	stx	%g4, [%g1]			! restore this banks L2$ mode
						!   to flush write buffers
	done					! return PASS
	SET_SIZE(ni_inj_l2cache_tag)
#endif	/* lint */

/*
 * This routine is used to corrupt the tag or the tag ecc bits of an L2
 * cache line containing an instruction accessed by it's paddr.  The
 * L2_DIAG_TAG register address is built by this routine, bits [7:6] of the
 * paddr select the cache bank.
 *
 * The method used is similar to the above ni_inj_l2cache_tag() routine.
 */

/*
 * Prototype:
 *
 * int ni_inj_l2cache_instr_tag(uint64_t paddr, uint64_t xorpat,
 *					uint_t checkflag);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - xorpat (clobbered)
 *	%o3 - checkbit flag
 *	%o4 - way increment value (not an inpar)
 *	%g1 - temp then address of L2_CTL_REG
 *	%g2 - temp then address for data read/write
 *	%g3 - temp then data read from cache
 *	%g4 - saved L2_CTL_REG contents
 *	%g5 - counter for L1$ clear
 *	%g6 - tag to use for L1 cache flush
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
ni_inj_l2cache_instr_tag(uint64_t paddr, uint64_t xorpat, uint_t checkflag)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(ni_inj_l2cache_instr_tag)

	/*
	 * Build the register values for L1$ access.
	 * The i$ way bits are bits 17:16.
	 */
	and	%o1, 0xfe0, %g6			! mask/shift paddr for L1 asi
	sllx	%g6, 1, %g6			! %g6 = L1 asi access addr
	mov	1, %o4				! %o4 = L1 way inc. value
	sllx	%o4, 16, %o4			! .
	mov	4, %g5				! %g5 = number of L1 ways

	/*
	 * Build the register values for L2$ accesses.
	 */
	mov	0xa4, %g4			! set select bits
	sllx	%g4, 32, %g4			! .
	set	0x3fffc0, %g2			! mask paddr to get $line
	and	%o1, %g2, %g2			! combine into complete reg
	add	%g2, %g4, %g2			! %g2 = L2_DIAG_TAG

	/*
	 * Build L2 Control Register for this bank and read value.
	 */
	mov	0xa9, %g1			! %g1=L2_CTL_REG=0xa9.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, 0xc0, %g3			! get reg offset from paddr
	add	%g1, %g3, %g1			! include offset in reg addr
	ldx	[%g1], %g4			! %g4=L2_CTL_REG contents

	ba	1f				! branch to aligned code
	  mov	%g0, %o0			! set return value PASS

	.align	64
1:
	ldx	[%g2], %g3			! read tag to corrupt
	cmp	%o3, %g0			! if check-bit flag == 0
	bz,a,pt	%xcc, 2f			! shift the xorpat to tag bits
	  sllx	%o2, 6, %o2			! .
2:
	xor	%g3, %o2, %g3			! corrupt tag or tag ecc bits
	stx	%g3, [%g2]			! write the tag back to L2$
	membar	#Sync				! .

	/*
	 * Clear the the L1$ entry so access does not hit there.
	 */
3:
	stxa	%g0, [%g6]ASI_ICACHE_TAG	! clear all four ways of L1
	sub	%g5, 1, %g5			! .
	brnz,a	%g5, 3b
	  add	%g6, %o4, %g6			! go to next way

	stx	%g4, [%g1]			! write to banks L2_CTL_REG
						!   to flush write buffers
	done					! return PASS
	SET_SIZE(ni_inj_l2cache_instr_tag)
#endif	/* lint */

/*
 * This routine is used to corrupt the tag or the tag ecc bits of an L2
 * cache line determined by a byte offset.  The L2_DIAG_TAG register address
 * is built by this routine, bits [7:6] of the paddr select the cache bank.
 *
 * The method used is similar to the above ni_inj_l2cache_tag() routine.
 */

/*
 * Prototype:
 *
 * int ni_inj_l2phys_tag(uint64_t offset, uint64_t xorpat, uint_t checkflag,
 *				uint64_t *debug);
 *
 * Register usage:
 *
 *	%o1 - byte offset
 *	%o2 - xorpat (clobbered)
 *	%o3 - checkbit flag
 *	%o4 - pointer to debug buffer (debug)
 *	%g1 - temp then address of L2_CTL_REG
 *	%g2 - temp then address for data read/write
 *	%g3 - data read from cache
 *	%g4 - temp then saved L2_CTL_REG contents
 *	%g5 - unused
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
ni_inj_l2phys_tag(uint64_t offset, uint64_t xorpat, uint_t checkflag,
					uint64_t *debug)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(ni_inj_l2phys_tag)

	/*
	 * Generate register addresses to use below.
	 */
	mov	0xa4, %g4			! set reg select bits
	sllx	%g4, 32, %g4			! .
	set	0x3fffc0, %g2			! mask offset to get $line
	and	%o1, %g2, %g2			! combine into complete reg
	add	%g2, %g4, %g2			! %g2 = L2_DIAG_TAG

	/*
	 * Put L2 into direct mapped mode, this bank only.
	 */
	mov	0xa9, %g1			! %g1=L2_CTL_REG=0xa9.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, 0xc0, %g3			! get reg offset from offset
	add	%g1, %g3, %g1			! include offset in reg addr
	
	ldx	[%g1], %g4			! %g4=prev L2_CTL_REG contents
	or	%g4, L2CR_DMMODE, %g3		! .
	stx	%g3, [%g1]			! .

	ba	1f				! branch to aligned code
	  mov	%g0, %o0			! set return value PASS

	.align	64
1:
	ldx	[%g2], %g3			! read tag to corrupt
	cmp	%o3, %g0			! if check-bit flag == 0
	bz,a,pt	%xcc, 2f			! shift the xorpat to tag bits
	  sllx	%o2, 6, %o2			! .
2:
	xor	%g3, %o2, %g3			! corrupt tag or ecc bits
	stx	%g3, [%g2]			! write the tag back to L2$
	membar	#Sync				!.

	stx	%g4, [%g1]			! restore this banks L2$ mode
						!   to flush write buffers
	done					! return PASS
	SET_SIZE(ni_inj_l2phys_tag)
#endif	/* lint */

/*
 * This routine is used to corrupt the VAUD or the VAUD parity bits of an L2
 * cache line determined by the paddr.  Parity is checked for all 12 VAD bits
 * on every L2 access (the Used bit of VUAD is not covered by parity since it
 * only affects performance, not correctness).  This means that the L2 does
 * not need to be put into DM mode and that errors can be detected even
 * without the explicit access.
 *
 * This routine expects that the caller will determine the bits to be
 * corrupted so the caller must be aware of the format of these regs.
 *
 * Similar to other l2$ tests the method of setting and restoring the L2$ DM
 * mode to clear the L2$ write buffers is used in the VAD type tests.
 *
 * L2_DIAG_VD (offset 0x6.0040.0000) Access (stride 64)
 * L2_DIAG_UA (offset 0x6.0000.0000) Access (stride 64)
 *
 * +-------+--------+-------+-------+-------+-----+------+-----------+
 * | Rsvd0 | Select | Rsvd1 | VDSel | Rsvd2 | Set | Bank | Rsvd3 + 4 |
 * +-------+--------+-------+-------+-------+-----+------+-----------+
 *   63:40   39:32    31:23    22     21:18  17:8   7:6    5:3 + 2:0
 *
 * Data returned as:
 *
 *     +-------+---------+---------+-------+-------+
 * VD: | Rsvd0 | VParity | DParity | Valid | Dirty |
 *     +-------+---------+---------+-------+-------+
 *     +-------+---------+---------+-------+-------+
 * UA: | Rsvd0 |  Rsvd1  | AParity | Used  | Alloc |
 *     +-------+---------+---------+-------+-------+
 *       63:26     25        24      23:12   11:0
 */

/*
 * Prototype:
 *
 * int ni_inj_l2vad(uint64_t paddr, uint_t xorpat, uint_t vdflag);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - xorpat
 *	%o3 - VD flag (must be 0x40.0000 or 0, avoids a shift instr)
 *	%o4 - way increment value (not an inpar)
 *	%g1 - temp then address of L2_CTL_REG
 *	%g2 - temp then address for data read/write
 *	%g3 - temp then data read from cache
 *	%g4 - temp then saved L2_CTL_REG contents
 *	%g5 - counter for L1$ clear
 *	%g6 - tag to use for L1 cache flush
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
ni_inj_l2vad(uint64_t paddr, uint_t xorpat, uint_t vdflag)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(ni_inj_l2vad)

	/*
	 * Build the register values for L1$ access.
	 * The d$ way bits are bits 12:11.
	 */
	and	%o1, 0x7f0, %g6			! mask paddr for L1 asi access
	mov	1, %o4				! %o4 = put way inc. value
	sllx	%o4, 11, %o4			! .
	mov	4, %g5				! %g5 = number of L1 ways

	/*
	 * Build the register values for L2$ accesses.
	 */
	mov	0xa6, %g4			! set select bits
	sllx	%g4, 32, %g4			! .
	set	0x3ffc0, %g2			! mask paddr to get $line
	and	%o1, %g2, %g2			! combine into complete reg
	cmp	%o3, %g0			! if VD flag != 0
	bnz,a,pt %icc, 1f			!   set the VDSel bit
	  or	%g2, %o3, %g2			! .
1:
	add	%g2, %g4, %g2			! %g2 = L2_DIAG_VD/UA

	/*
	 * Get the contents of this banks control register.
	 */
	mov	0xa9, %g1			! %g1=L2_CTL_REG=0xa9.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, 0xc0, %g3			! get reg offset from paddr
	add	%g1, %g3, %g1			! include offset in reg addr
	ldx	[%g1], %g4			! %g4=L2_CTL_REG contents

	ba	2f				! branch to aligned code
	  nop					! .

	.align	64
2:
	ldx	[%o1], %g3			! get data into L2$ and L1$
	membar	#Sync				!.
	ldx	[%g2], %g3			! read value to corrupt
	xor	%g3, %o2, %g3			! corrupt VAUD or parity bits
	stx	%g3, [%g2]			! write the value back to L2$
	membar	#Sync				!.

	/*
	 * Clear the the L1$ entry so access does not hit there.
	 */
3:
	stxa	%g0, [%g6]ASI_DC_TAG		! clear all four ways of L1
	sub	%g5, 1, %g5			! .
	brnz	%g5, 3b
	  add	%g6, %o4, %g6			! go to next way

	stx	%g4, [%g1]			! write to control reg

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_inj_l2vad)
#endif	/* lint */

/*
 * This routine is used to corrupt the VAUD or the VAUD parity bits of an L2
 * cache line that maps to an instruction determined by the paddr.  This
 * routine is similar to the above ni_inj_l2vad() routine.
 *
 * This routine expects that the caller will determine the bits to be
 * corrupted so the caller must be aware of the format of these regs.
 */

/*
 * Prototype:
 *
 * int ni_inj_l2vad_instr(uint64_t paddr, uint_t xorpat, uint_t vdflag);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - xorpat
 *	%o3 - VD flag (must be 0x40.0000 or 0, avoids a shift instr)
 *	%o4 - way increment value (not an inpar)
 *	%g1 - temp then address of L2_CTL_REG
 *	%g2 - temp then address for data read/write
 *	%g3 - temp then data read from cache
 *	%g4 - temp then saved L2_CTL_REG contents
 *	%g5 - counter for L1$ clear
 *	%g6 - tag to use for L1 cache flush
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
ni_inj_l2vad_instr(uint64_t paddr, uint_t xorpat, uint_t vdflag)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(ni_inj_l2vad_instr)

	/*
	 * Build the register values for L1$ access.
	 * The i$ way bits are bits 17:16.
	 */
	and	%o1, 0xfe0, %g6			! mask paddr for L1 asi access
	sllx	%g6, 1, %g6			! shift for L1 asi access
	mov	1, %o4				! %o4 = put way inc. value
	sllx	%o4, 16, %o4			! .
	mov	4, %g5				! %g5 = number of L1 ways

	/*
	 * Build the register values for L2$ accesses.
	 */
	mov	0xa6, %g4			! set select bits
	sllx	%g4, 32, %g4			! .
	set	0x3ffc0, %g2			! mask paddr to get $line
	and	%o1, %g2, %g2			! combine into complete reg
	cmp	%o3, %g0			! if VD flag != 0
	bnz,a,pt %icc, 1f			!   set the VDSel bit
	  or	%g2, %o3, %g2			! .
1:
	add	%g2, %g4, %g2			! %g2 = L2_DIAG_VD/UA

	/*
	 * Get the contents of this banks control register.
	 */
	mov	0xa9, %g1			! %g1=L2_CTL_REG=0xa9.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, 0xc0, %g3			! get reg offset from paddr
	add	%g1, %g3, %g1			! include offset in reg addr
	ldx	[%g1], %g4			! %g4=L2_CTL_REG contents

	ba	2f				! branch to aligned code
	  nop					! .

	.align	64
2:
	ldx	[%o1], %g3			! get data into L2$ and L1$
	membar	#Sync				!.

	ldx	[%g2], %g3			! read value to corrupt
	xor	%g3, %o2, %g3			! corrupt VAUD or parity bits
	stx	%g3, [%g2]			! write the value back to L2$
	membar	#Sync				!.

	/*
	 * Clear the the L1$ entry so access does not hit there.
	 */
3:
	stxa	%g0, [%g6]ASI_ICACHE_TAG	! clear all four ways of L1
	sub	%g5, 1, %g5			! .
	brnz	%g5, 4b
	  add	%g6, %o4, %g6			! go to next way

	stx	%g4, [%g1]			! write to control reg

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_inj_l2vad_instr)
#endif	/* lint */

/*
 * This routine is used to corrupt the VAUD or the VAUD parity bits of an L2
 * cache line determined by a byte offset.  This routine is similar to the
 * above ni_inj_l2cache_vad() except it takes an offset instead of a paddr.
 *
 * This routine expects that the caller will determine the bits to be
 * corrupted so the caller must be aware of the format of these regs.
 */

/*
 * Prototype:
 *
 * int ni_inj_l2vad_phys(uint64_t offset, uint_t xorpat, uint_t vdflag,
 *				uint64_t *debug);
 *
 * Register usage:
 *
 *	%o1 - byte offset (only bits [17:6] used)
 *	%o2 - xorpat (clobbered)
 *	%o3 - VD flag (must be 0x40.0000 or 0, avoids a shift instr)
 *	%o4 - pointer to debug buffer (debug)
 *	%g1 - address of L2_CTL_REG
 *	%g2 - register address for data read/write
 *	%g3 - data read from cache
 *	%g4 - temp then saved L2_CTL_REG contents
 *	%g5 - unused
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
ni_inj_l2vad_phys(uint64_t offset, uint_t xorpat, uint_t vdflag,
				uint64_t *debug)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(ni_inj_l2vad_phys)

	/*
	 * Generate register addresses to use below.
	 */
	mov	0xa6, %g4			! set select bits
	sllx	%g4, 32, %g4			! .
	set	0x3ffc0, %g2			! mask paddr to get $line
	and	%o1, %g2, %g2			! combine into complete reg
	cmp	%o3, %g0			! if VD flag != 0
	bnz,a,pt %icc, 1f			!   set the VDSel bit
	  or	%g2, %o3, %g2			! .
1:
	add	%g2, %g4, %g2			! %g2 = L2_DIAG_VD/UA

	/*
	 * Get the contents of this banks control register.
	 */
	mov	0xa9, %g1			! %g1=L2_CTL_REG=0xa9.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, 0xc0, %g3			! get reg offset from offset
	add	%g1, %g3, %g1			! include offset in reg addr
	ldx	[%g1], %g4			! %g4=L2_CTL_REG contents

	ba	2f				! branch to aligned code
	  nop					! .

	.align	64
2:
	ldx	[%g2], %g3			! read value to corrupt
	xor	%g3, %o2, %g3			! corrupt VAUD or parity bits
	stx	%g3, [%g2]			! write the value back to L2$
	membar	#Sync				!.

	stx	%g4, [%g1]			! write to control reg

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_inj_l2vad_phys)
#endif	/* lint */

/*
 * This routine is used to corrupt the L2$ directory parity at a location
 * determined by the paddr.  During directory scrub, parity is checked for
 * each directory entry.  Directory parity errors are planted via the per
 * bank L2_ERROR_INJECT_REG (0xd.0000.0000).  Bits [7:6] of the paddr select
 * the cache bank.
 *
 * NOTE: this routins is two i$ lines in size, one would be preferable
 *	 but even this minimizes the chances of side effects.
 */

/*
 * Prototype:
 *
 * int ni_inj_l2dir(uint64_t paddr, uint_t sdshot_enb, uint_t data_flag,
 *			uint64_t *debug);
 *
 * Register usage:
 *
 *	%o1 - paddr (64-bit aligned)
 *	%o2 - sdshot_enb (the combination of enable and sdshot to use)
 *	%o3 - flag for DATA version of test
 *	%o4 - pointer to debug buffer (debug)
 *	%g1 - temp
 *	%g2 - temp then address for data read/write
 *	%g3 - unused
 *	%g4 - unused
 *	%g5 - unused
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
ni_inj_l2dir(uint64_t paddr, uint_t sdshot_enb, uint_t data_flag,
			uint64_t *debug)
{
	return 0;
}
#else
	.align	64
	ENTRY_NP(ni_inj_l2dir)

	/*
	 * Generate register addresses to use below.
	 */
	mov	0xad, %g4			! set select bits
	sllx	%g4, 32, %g4			! .
	and	%o1, 0xc0, %g2			! mask paddr to get L2$ bank
	add	%g2, %g4, %g2			! %g2 = L2_ERROR_INJECT_REG

	stx	%o2, [%g2]			! enable dir error injection
	ldx	[%g2], %g0			! ensure write completes

	cmp	%o3, %g0			! if DATA flag != 0
	bnz,a,pt %icc, 1f			!   write first bad par to dir
	  ldx	[%o1], %g3			!   (else wait for C-routine)
1:
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_inj_l2dir)
#endif	/* lint */

/*
 * This routine is used to corrupt the L2$ directory parity at a location
 * determined by a byte offset.  During directory scrub, parity is checked for
 * the directory entry.  Directory parity errors can be planted via the per
 * bank L2_ERROR_INJECT_REG (0xd.0000.0000).  Bits [7:6] of the offset select
 * the cache bank register, the rest of the bits are ignored.
 *
 * Due to the way the injection register was implemented we actually can't
 * insert an error into a specific location via an offset.  This is a HW
 * limitaion.  The next update to the directory will be in error.  This
 * limitaion is not unreasonable since all directory errors are FATAL and
 * produce no verifiable error output anyway.
 *
 * NOTE: this routine is exactly one i$ line in size, which minimizes the
 *	 chances of side effects.
 */

/*
 * Prototype:
 *
 * int ni_inj_l2dir_phys(uint64_t offset, uint_t sdshot_enb, uint64_t *debug);
 *
 * Register usage:
 *
 *	%o1 - byte offset (only bits [7:6] used)
 *	%o2 - sdshot_enb (the combination of enable and sdshot to use)
 *	%o3 - pointer to debug buffer (debug)
 *	%o4 - unused
 *	%g1 - unused
 *	%g2 - register address
 *	%g3 - unused
 *	%g4 - unused
 *	%g5 - unused
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
ni_inj_l2dir_phys(uint64_t offset, uint_t sdshot_enb, uint64_t *debug)
{
	return 0;
}
#else
	.align	32
	ENTRY_NP(ni_inj_l2dir_phys)

	/*
	 * Generate register addresses to use below.
	 */
	mov	0xad, %g4			! set select bits
	sllx	%g4, 32, %g4			! .
	and	%o1, 0xc0, %g2			! mask paddr to get L2$ bank
	add	%g2, %g4, %g2			! %g2 = L2_ERROR_INJECT_REG

	stx	%o2, [%g2]			! enable dir error injection
	ldx	[%g2], %g0			! ensure write completes

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_inj_l2dir_phys)
#endif	/* lint */

/*------------------ end of the L2$ / start of D$ functions ----------------*/

/*
 * This routine is used to corrupt the data or the data parity of the L1
 * data cache line determined by the paddr.  The ASI_LSU_DIAG_REG is used
 * to put the data cache into paddr replacement policy mode.
 *
 * The critical section of each d$ routine must fit on one L2$ line to
 * avoid unwanted side effects.
 *
 * NOTE: Niagara calculates the d$ parity for ASI stores as well as normal
 *	 fault-ins.  This means d$ errors can only be injected by setting the
 *	 parity bit (bit 32) when doing ASI store accesses.
 *
 * ASI_DC_DATA (0x46)
 *
 *         +-------+----------+-----+-----+------+-------+
 * Stores: | Rsvd0 | PErrMask | Way | Set | Word | Rsvd2 |
 *         +-------+----------+-----+-----+------+-------+
 *           63:21    20:13    12:11 10:4    3      2:0
 *         +-------+-----+-----+------+-------+
 * Loads:  | Rsvd0 | Tag | Set | Word | Rsvd2 |
 *         +-------+-----+-----+------+-------+
 *           63:39  38:11 10:4    3      2:0
 *
 *	NOTE: the tag bits are one bit shorter than the actual
 *	      tag which is PA[39:11], perhaps a PRM typo, or ??
 *
 * Data returned as DATA[63:0]
 */

/*
 * Prototype:
 *
 * int ni_inj_dcache_data(uint64_t paddr, uint64_t xorpat, uint_t checkflag,
 *				uint64_t *debug);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - xorpat (must be within [20:13] for check-bit test)
 *	%o3 - check-bit flag
 *	%o4 - pointer to debug buffer (debug)
 *	%g1 - temp
 *	%g2 - temp
 *	%g3 - temp then data read from cache
 *	%g4 - asi access for store
 *	%g5 - unused
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
ni_inj_dcache_data(uint64_t paddr, uint64_t xorpat, uint_t checkflag,
				uint64_t *debug)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(ni_inj_dcache_data)

	/*
	 * Generate register addresses to use below.
	 */
	mov	0x10, %g2			! set d$ direct mapped mode
	ldxa	[%g2]ASI_LSU_DIAG_REG, %g3	!  (preserve i$ mode)
	or	%g3, 0x2, %g3			! .
	stxa	%g3, [%g2]ASI_LSU_DIAG_REG	! .
	membar	#Sync				! .

	mov	0xffc, %g3			! bitmask for asi store access 
	sllx	%g3, 1, %g3			! .
	and	%o1, %g3, %g4			! %g4 = PA[12:3] for store
						!  asi accesses
#ifdef	L1_DEBUG_BUFFER
	stx	%o1, [%o4 + 0]			! paddr
	stx	%g4, [%o4 + 8]			! asi store addr
#endif

	setx	0x7ffffffff8 , %g3, %g2		! paddr mask for load access
						!   (not actually required)
	and	%o1, %g2, %g6			! %g6 = va for asi load access

#ifdef	L1_DEBUG_BUFFER
	stx	%g6, [%o4 + 0x10]		! asi load addr
#endif

	ba	1f				! branch to aligned code
	  nop

	.align	64
1:
	ldx	[%o1], %g0			! read data to get it into d$
	membar	#Sync				! ensure data is in d$

	ldxa	[%g6]ASI_DC_DATA, %g3		! read the data to corrupt
	cmp	%o3, %g0			! if check-bit flag != 0
	bnz,a,pt %icc, 2f			!   set the parity bit mask
	  or	%g4, %o2, %g4			!   .
	xor	%g3, %o2, %g3			! else corrupt the data
2:
	stxa	%g3, [%g4]ASI_DC_DATA		! write it back to d$ data
	membar	#Sync				! required

#ifdef	L1_DEBUG_BUFFER
	stx	%g3, [%o4 + 0x18]		! cache data (can be corrupted)
	stx	%g4, [%o4 + 0x20]		! asi store access addr (")
#endif

	ldxa	[%g2]ASI_LSU_DIAG_REG, %g3	! restore d$ replacement mode
	andn	%g3, 0x2, %g3			! .
	stxa	%g3, [%g2]ASI_LSU_DIAG_REG	! .
	membar	#Sync				!.

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_inj_dcache_data)
#endif	/* lint */

/*
 * This routine is used to corrupt the data or the data parity of the L1
 * data cache line determined by the paddr.  The ASI_LSU_DIAG_REG is used
 * to put the data cache into paddr replacement policy mode.
 *
 * This routine is similar to the above ni_inj_dcache_data() test but here the
 * address to corrupt is accessed, corrupted, and accessed again (to trigger
 * the error) all while in hyperpriv mode.
 */

/*
 * Prototype:
 *
 * int ni_inj_dcache_hvdata(uint64_t paddr, uint64_t xorpat, uint_t checkflag,
 *				uint_t access_type);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - xorpat (must be within [20:13] for check-bit test)
 *	%o3 - check-bit flag
 *	%o4 - access type (LOAD or STORE)
 *	%g1 - temp
 *	%g2 - temp
 *	%g3 - temp then data read from cache
 *	%g4 - asi access for store
 *	%g5 - unused
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
ni_inj_dcache_hvdata(uint64_t paddr, uint64_t xorpat, uint_t checkflag,
				uint_t access_type)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(ni_inj_dcache_hvdata)

	/*
	 * Generate register addresses to use below.
	 */
	mov	0x10, %g2			! set d$ direct mapped mode
	ldxa	[%g2]ASI_LSU_DIAG_REG, %g3	!  (preserve i$ mode)
	or	%g3, 0x2, %g3			! .
	stxa	%g3, [%g2]ASI_LSU_DIAG_REG	! .
	membar	#Sync				! .

	mov	0xffc, %g3			! bitmask for asi store access 
	sllx	%g3, 1, %g3			! .
	and	%o1, %g3, %g4			! %g4 = PA[12:3] for store
						!  asi accesses
	setx	0x7ffffffff8 , %g3, %g2		! paddr mask for load access
						!   (not actually required)
	and	%o1, %g2, %g6			! %g6 = va for asi load access

	ba	1f				! branch to aligned code
	  nop

	.align	64
1:
	ldx	[%o1], %g0			! read data to get it into d$
	membar	#Sync				! ensure data is in d$

	ldxa	[%g6]ASI_DC_DATA, %g3		! read the data to corrupt
	cmp	%o3, %g0			! if check-bit flag != 0
	bnz,a,pt %icc, 2f			!   set the parity bit mask
	  or	%g4, %o2, %g4			!   .
	xor	%g3, %o2, %g3			! else corrupt the data
2:
	stxa	%g3, [%g4]ASI_DC_DATA		! write it back to d$ data
	membar	#Sync				! required

	ldxa	[%g2]ASI_LSU_DIAG_REG, %g3	! restore d$ replacement mode
	andn	%g3, 0x2, %g3			! .
	stxa	%g3, [%g2]ASI_LSU_DIAG_REG	! .
	membar	#Sync				!.

	/*
	 * Trigger the injected error while still in hypervisor context.
	 */
	cmp	%o4, %g0			! if access_type != 0
	bnz,a,pt %xcc, 3f			!   trigger error via a store
	  stx	%g0, [%o1] 			!   .
	ldx	[%o1], %g0			! else trigger via a load
3:
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_inj_dcache_hvdata)
#endif	/* lint */

/*
 * This routine is used to corrupt the data or the data parity of the L1
 * data cache line determined by a cache byte offset.
 *
 * The method used is similar to the above ni_inj_dcache_data() routine but
 * is complicated be the fact that the data access ASI requires a valid tag
 * to perform a match in HW.  To achieve this we read the tag at the specified
 * offset first in order to build the data ASI access value.
 */

/*
 * Prototype:
 *
 * int ni_inj_dphys_data(uint64_t offset, uint64_t xorpat, uint_t checkflag,
 *				uint64_t *debug);
 *
 * Register usage:
 *
 *	%o1 - byte offset
 *	%o2 - xorpat (must be only [20:13] for check-bit test)
 *	%o3 - check-bit flag
 *	%o4 - pointer to debug buffer (debug)
 *	%g1 - temp
 *	%g2 - tag read from cache
 *	%g3 - temp then data read from cache
 *	%g4 - asi access for store
 *	%g5 - unused
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
ni_inj_dphys_data(uint64_t offset, uint64_t xorpat, uint_t checkflag,
				uint64_t *debug)
{
	return 0;
}
#else
	.align	64
	ENTRY_NP(ni_inj_dphys_data)

	/*
	 * Generate register addresses to use below.
	 */
	mov	0xffc, %g3			! bitmask for asi store access 
	sllx	%g3, 1, %g3			! .
	and	%o1, %g3, %g4			! %g4 = PA[12:3] for store

	ldxa	[%g4]ASI_DC_TAG, %g2		! read tag for this offset
	srlx	%g2, 1, %g2			! remove the valid bit
	sllx	%g2, 11, %g2			! shift tag for asi load access
	and	%o1, 0x7f8, %g1			! bitmask for asi load access
	or	%g2, %g1, %g2			! complete asi load access

	ldxa	[%g2]ASI_DC_DATA, %g3		! read the data to corrupt
	cmp	%o3, %g0			! if check-bit flag != 0
	bnz,a,pt %icc, 1f			!   set the parity bit mask
	  or	%g4, %o2, %g4			!   .
	xor	%g3, %o2, %g3			! else corrupt the data
1:
	stxa	%g3, [%g4]ASI_DC_DATA		! write it back to d$ data
	membar #Sync				! required to complete store

	mov	%g0, %o0			! return PASS
	done					! return (no value)
	SET_SIZE(ni_inj_dphys_data)
#endif	/* lint */

/*
 * This routine is used to corrupt the tag data or the tag parity of the
 * L1 data cache line determined by the paddr.  The ASI_LSU_DIAG_REG is used
 * to put the data cache into paddr replacement policy mode (AKA direct map).
 *
 * If the checkbit flag is set the xorpat must be 0x2000 (only bit 13 set)
 * since the single parity bit is inverted using the built-in asi mechanism.
 *
 * The critical section of each d$ routine must fit on one L2$ line to
 * avoid unwanted side effects.
 *
 * ASI_DC_TAG (0x47)
 *
 * +-------+--------+-----+-----+-------+
 * | Rsvd0 | PErren | Way | Set | Rsvd1 |
 * +-------+--------+-----+-----+-------+
 *   63:14     13    12:11 10:4    3:0
 *
 * Data returned as PARITY[30], TAG[29:1], VALID[0] (tag is PA[39:11])
 */

/*
 * Prototype:
 *
 * int ni_inj_dcache_tag(uint64_t paddr, uint64_t xorpat, uint_t checkflag,
 *				uint64_t *debug);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - xorpat (valid tag bits [29:1], parity bit [13])
 *	%o3 - check-bit flag
 *	%o4 - pointer to debug buffer (debug)
 *	%g1 - temp
 *	%g2 - temp
 *	%g3 - temp then tag read from cache
 *	%g4 - asi access
 *	%g5 - unused
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
ni_inj_dcache_tag(uint64_t paddr, uint64_t xorpat, uint_t checkflag,
				uint64_t *debug)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(ni_inj_dcache_tag)

	/*
	 * Generate register addresses to use below.
	 */
	mov	0xff8, %g3			! bitmask for asi access 
	sllx	%g3, 1, %g3			! .
	and	%o1, %g3, %g4			! %g4 = PA[12:4] for asi

#ifdef	L1_DEBUG_BUFFER
	stx	%o1, [%o4]			! paddr
	stx	%g4, [%o4 + 8]			! asi store addr
#endif

	mov	0x10, %g2			! set d$ direct mapped mode
	ldxa	[%g2]ASI_LSU_DIAG_REG, %g3	!  (preserve i$ mode)
	or	%g3, 0x2, %g3			! .
	stxa	%g3, [%g2]ASI_LSU_DIAG_REG	! .
	membar	#Sync				! .

	ba	1f				! branch to aligned code
	  nop

	.align	64
1:
	ldx	[%o1], %g0			! read addr to get it into d$
	membar	#Sync				! ensure data is in d$

	ldxa	[%g4]ASI_DC_TAG, %g3		! read the tag to corrupt
	cmp	%o3, %g0			! if check-bit flag != 0
	bnz,a,pt %icc, 2f			!   set the parity flip bit
	  or	%g4, %o2, %g4			!   .
	xor	%g3, %o2, %g3			! else corrupt the tag
2:
	stxa	%g3, [%g4]ASI_DC_TAG		! write it back to d$
	membar	#Sync				! required

#ifdef	L1_DEBUG_BUFFER
	stx	%g3, [%o4 + 0x18]		! cache tag (can be corrupted)
	stx	%g4, [%o4 + 0x20]		! asi store access addr (")
#endif

	ldxa	[%g2]ASI_LSU_DIAG_REG, %g3	! restore d$ replacement mode
	andn	%g3, 0x2, %g3			! .
	stxa	%g3, [%g2]ASI_LSU_DIAG_REG	! .
	membar	#Sync				! .

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_inj_dcache_tag)
#endif	/* lint */

/*
 * This routine is used to corrupt the tag or the tag parity of the L1
 * data cache line determined by the paddr.  The ASI_LSU_DIAG_REG is used
 * to put the data cache into paddr replacement policy mode.
 *
 * This routine is similar to the above ni_inj_dcache_tag() test but here the
 * address to corrupt is accessed, corrupted, and accessed again (to trigger
 * the error) all while in hyperpriv mode.
 */

/*
 * Prototype:
 *
 * int ni_inj_dcache_hvtag(uint64_t paddr, uint64_t xorpat, uint_t checkflag,
 *				uint_t access_type);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - xorpat (valid tag bits [29:1], parity bit [13])
 *	%o3 - check-bit flag
 *	%o4 - access type (LOAD or STORE)
 *	%g1 - temp
 *	%g2 - temp
 *	%g3 - temp then tag read from cache
 *	%g4 - asi access
 *	%g5 - unused
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
ni_inj_dcache_hvtag(uint64_t paddr, uint64_t xorpat, uint_t checkflag,
				uint_t access_type)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(ni_inj_dcache_hvtag)

	/*
	 * Generate register addresses to use below.
	 */
	mov	0xff8, %g3			! bitmask for asi access 
	sllx	%g3, 1, %g3			! .
	and	%o1, %g3, %g4			! %g4 = PA[12:4] for asi

	mov	0x10, %g2			! set d$ direct mapped mode
	ldxa	[%g2]ASI_LSU_DIAG_REG, %g3	!  (preserve i$ mode)
	or	%g3, 0x2, %g3			! .
	stxa	%g3, [%g2]ASI_LSU_DIAG_REG	! .
	membar	#Sync				! .

	ba	1f				! branch to aligned code
	  nop

	.align	64
1:
	ldx	[%o1], %g0			! read addr to get it into d$
	membar	#Sync				! ensure data is in d$

	ldxa	[%g4]ASI_DC_TAG, %g3		! read the tag to corrupt
	cmp	%o3, %g0			! if check-bit flag != 0
	bnz,a,pt %icc, 2f			!   set the parity flip bit
	  or	%g4, %o2, %g4			!   .
	xor	%g3, %o2, %g3			! else corrupt the tag
2:
	stxa	%g3, [%g4]ASI_DC_TAG		! write it back to d$
	membar	#Sync				! required

	ldxa	[%g2]ASI_LSU_DIAG_REG, %g3	! restore d$ replacement mode
	andn	%g3, 0x2, %g3			! .
	stxa	%g3, [%g2]ASI_LSU_DIAG_REG	! .
	membar	#Sync				! .

	/*
	 * Trigger the injected error while still in hypervisor context.
	 */
	cmp	%o4, %g0			! if access_type != 0
	bnz,a,pt %xcc, 3f			!   trigger error via a store
	  stx	%g0, [%o1] 			!   .
	ldx	[%o1], %g0			! else trigger via a load
3:
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_inj_dcache_hvtag)
#endif	/* lint */

/*
 * This routine is used to corrupt the tag or the tag parity of the L1
 * data cache line determined by the cache offset.
 *
 * The method used is similar to the above ni_inj_dcache_tag() routine.
 */

/*
 * Prototype:
 *
 * int ni_inj_dphys_tag(uint64_t offset, uint64_t xorpat, uint_t checkflag,
 *				uint64_t *debug);
 *
 * Register usage:
 *
 *	%o1 - byte offset
 *	%o2 - xorpat (valid tag bits [29:1], parity bit [13])
 *	%o3 - check-bit flag
 *	%o4 - pointer to debug buffer (debug)
 *	%g1 - temp
 *	%g2 - tag read from cache
 *	%g3 - data read from cache
 *	%g4 - asi access for store
 *	%g5 - unused
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
ni_inj_dphys_tag(uint64_t offset, uint64_t xorpat, uint_t checkflag,
				uint64_t *debug)
{
	return 0;
}
#else
	.align	64
	ENTRY_NP(ni_inj_dphys_tag)

	mov	0xff8, %g3			! bitmask for asi access 
	sllx	%g3, 1, %g3			! .
	and	%o1, %g3, %g4			! %g4 = PA[12:4] for asi

	ldxa	[%g4]ASI_DC_TAG, %g3		! read the tag to corrupt
	cmp	%o3, %g0			! if check-bit flag != 0
	bnz,a,pt %icc, 1f			!   set the parity flip bit
	  or	%g4, %o2, %g4			!   .
	xor	%g3, %o2, %g3			! else corrupt the tag
1:
	stxa	%g3, [%g4]ASI_DC_TAG		! write it back to d$
	membar	#Sync				! required

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_inj_dphys_tag)
#endif	/* lint */

/*------------------- end of the D$ / start of I$ functions ----------------*/

/*
 * This routine is used to corrupt the instruction parity of the L1
 * instruction cache line word determined by the paddr.
 *
 * The critical section of each i$ routine must fit on one i$ line to
 * avoid unwanted side effects and the entire routine should fit on one
 * L2$ line.  These i$ routines do NOT achieve this goal and instead fit
 * on two L2$ lines.  The instruction to corrupt is already present in i$
 * before this routine so the entire routine is the critical section
 * (for the non-hyperpriv mode routines).
 *
 * Although the PRM does not state it Niagara calculates the i$ parity
 * for ASI stores as well as normal fault-ins.  This means i$ errors can
 * only be injected by setting the parity bit (bit 32) when doing ASI
 * store accesses.  An effect of this is the xor pattern inpar is ignored.
 *
 * The ASI_LSU_DIAG_REG is not used to disable the way replacement since
 * it only disables 3 of the 4 ways reducing the already small i$ size.
 *
 * ASI_ICACHE_INSTR (0x66)
 *
 * +-------+-----+-------+-----+------+-------+
 * | Rsvd0 | Way | Rsvd1 | Set | Word | Rsvd2 |
 * +-------+-----+-------+-----+------+-------+
 *   63:18  17:16  15:13  12:6   5:3     2:0
 *
 * Data returned as INSTR[31:0], PARITY[32]
 */

/*
 * Prototype:
 *
 * int ni_inj_icache_instr(uint64_t paddr, uint64_t xorpat, uint64_t *debug);
 *
 * Register usage:
 *
 *	%o1 - paddr then temp
 *	%o2 - xorpat (unused) then set bits for ASI instr access, then way bits
 *	%o3 - pointer to debug buffer (debug)
 *	%o4 - mask for parity bit (for parity flip)
 *	%g1 - temp then loop counter
 *	%g2 - temp then tag for compare
 *	%g3 - temp then way increment value
 *	%g4 - instr read from cache
 *	%g5 - paddr then addr bits for asi access
 *	%g6 - tag read from cache
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
ni_inj_icache_instr(uint64_t paddr, uint64_t xorpat, uint64_t *debug)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(ni_inj_icache_instr)

	/*
	 * Generate register addresses to use below.
	 */
	mov	%o1, %g5			! save paddr in %g5

#ifdef	L1_DEBUG_BUFFER
	stx	%o1, [%o3]			! paddr
#endif

	mov	1, %g1				! set way inc value and par bit
	sllx	%g1, 16, %g3			! %g3 = way increment value
	sllx	%g1, 32, %o4			! %o4 = parity flip bit

	srlx	%g5, 12, %g2			! shift paddr for tag compare
						!  %g2 = tag asi tag bits
	and	%g5, 0xffc, %g5			! mask paddr for asi accesses
	sllx	%g5, 1, %g5			!  %g5 = instr asi set bits
	andn	%g5, 0x3f, %o2			!  %o2 = tag asi set bits

#ifdef	L1_DEBUG_BUFFER
	stx	%g2, [%o3 + 0x10]		! asi access compare values
	stx	%o2, [%o3 + 0x18]		! parity bit
	stx	%g5, [%o3 + 0x20]		! asi index for both asis
#endif

	mov	4, %g1				! %g1 = way loop counter
	ba	1f				! branch to aligned code
	  mov	%g0, %o0			! set return value to PASS

	/*
	 * Try to find which way the instructions ended up in.
	 */
	.align	64
1:
	ldxa	[%o2]ASI_ICACHE_TAG, %g6	! read tag for this way
	cmp	%g6, %g2			! is the data in this way?

#ifdef	L1_DEBUG_BUFFER
	sllx	%g1, 3, %o1			! mult loop counter by 8
	add	%o1, 0x28, %o1			! give it an offset
	stx	%g6, [%o3 + %o1]		! tag value of this way
#endif

	be	%icc, 2f			!   (yes if lower 32-bits match)
	  sub	%g1, 1, %g1			! decrement loop counter
	brz,a	%g1, 3f				! if not found in any way then
	  mov	0xded, %o1			!   return value = error
	ba	1b				! loop again
	  add	%o2, %g3, %o2			! generate next asi addr
						!   for tag read (incr way)
	/*
	 * Found the matching tag, now corrupt the instr.
	 */
2:
	mov	0xfff, %o1			! extract the way bits VA[17:16]
	sllx	%o1, 1, %o1			!   above and below are rsvd
	andn	%o2, %o1, %o2			!   so this mask OK
	add	%g5, %o2, %g5			! combine set and way bits
	ldxa	[%g5]ASI_ICACHE_INSTR, %g4	! read the instr to corrupt

#ifdef	L1_DEBUG_BUFFER
	stx	%g5, [%o3 + 0x50]		! the asi/paddr to corrupt
	stx	%g4, [%o3 + 0x58]		! instr data from cache
#endif

	or	%g4, %o4, %g4			! corrupt via parity flip
	stxa	%g4, [%g5]ASI_ICACHE_INSTR	! write it back to i$
	membar	#Sync				! .

#ifdef	L1_DEBUG_BUFFER
	ldxa	[%g5]ASI_ICACHE_INSTR, %g4	! read it back to check it
	stx	%g4, [%o3 + 0x68]		!   was corrupted right
#endif

3:
	done					! return (value set above)
	SET_SIZE(ni_inj_icache_instr)
#endif	/* lint */

/*
 * This routine is used to corrupt the instruction parity of the L1 instruction
 * cache line determined by the paddr.
 *
 * This routine is similar to the above ni_inj_icache_instr() test but here the
 * routine to corrupt is called, corrupted, and called again (to trigger the
 * error) all while in hyperpriv mode.
 *
 * NOTE: the routine to call must be loaded in to mem so it is located below
 *	 this routine and the alignment of this routine takes the access
 *	 routines size into account also (so they are on the same page).
 */

/*
 * Prototype:
 *
 * int ni_inj_icache_hvinstr(uint64_t paddr, uint64_t xorpat, uint64_t *debug);
 *
 * Register usage:
 *
 *	%o0 - paddr of routine to corrupt/call
 *	%o1 - paddr (clobbered)
 *	%o2 - xorpat (unused) then set bits for ASI instr access, then way bits
 *	%o3 - pointer to debug buffer (debug)
 *	%o4 - mask for parity bit (for parity flip)
 *	%g1 - temp then loop counter
 *	%g2 - temp then tag for compare
 *	%g3 - temp then way increment value
 *	%g4 - instr read from cache
 *	%g5 - paddr then addr bits for asi access
 *	%g6 - tag read from cache
 *	%g7 - return address for called routine
 */
#if defined(lint)
/* ARGSUSED */
int
ni_inj_icache_hvinstr(uint64_t paddr, uint64_t xorpat, uint64_t *debug)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(ni_inj_icache_hvinstr)

	/*
	 * Calling the routine to be corrupted from this routine gives us a
	 * more regular "critical section" to trigger errors from hyperpriv.
	 *
	 * Note that the call (jmp) to the below hv_ic_hvaccess clobbers %o1.
	 */
	mov	%o1, %g5			! copy routine paddr so it can
	mov	%g5, %o0			!   be used and called below

#ifdef	L1_DEBUG_BUFFER
	stx	%o1, [%o3]			! paddr
#endif
	/*
	 * Generate register addresses to use below.
	 */
	mov	1, %g1				! set way inc value and par bit
	sllx	%g1, 16, %g3			! %g3 = way increment value
	sllx	%g1, 32, %o4			! %o4 = parity flip bit

	srlx	%g5, 12, %g2			! shift paddr for tag compare
						!  %g2 = tag asi tag bits
	and	%g5, 0xffc, %g5			! mask paddr for asi accesses
	sllx	%g5, 1, %g5			!  %g5 = instr asi set bits
	andn	%g5, 0x3f, %o2			!  %o2 = tag asi set bits

#ifdef	L1_DEBUG_BUFFER
	stx	%g2, [%o3 + 0x10]		! asi access compare values
	stx	%o2, [%o3 + 0x18]		!
	stx	%g5, [%o3 + 0x20]		!
#endif
	/*
	 * Run the routine to corrupt to bring it into cache.
	 * Must ensure that it's not too far away for the jmp.
	 */
	jmp	%o0
	  rd	%pc, %g7

	mov	4, %g1				! %g1 = way loop counter
	ba	1f				! branch to aligned code
	  nop					! .

	/*
	 * Try to find which way the instructions ended up in.
	 */
	.align	64
1:
	ldxa	[%o2]ASI_ICACHE_TAG, %g6	! read tag for this way
	cmp	%g6, %g2			! is the data in this way?

#ifdef	L1_DEBUG_BUFFER
	sllx	%g1, 3, %o1			! mult loop counter by 8
	add	%o1, 0x28, %o1			! give it an offset
	stx	%g6, [%o3 + %o1]		! tag value of this way
#endif

	be	%icc, 2f			!   (yes if lower 32-bits match)
	  sub	%g1, 1, %g1			! decrement loop counter
	brz,a	%g1, 3f				! if not found in any way then
	  mov	0xded, %o1			!   return value = error
	ba	1b				! loop again
	  add	%o2, %g3, %o2			! generate next asi addr
						!   for tag read (incr way)
	/*
	 * Found the matching tag, now corrupt the instr.
	 */
2:
	mov	0xfff, %o1			! extract the way bits VA[17:16]
	sllx	%o1, 1, %o1			!   above and below are rsvd
	andn	%o2, %o1, %o2			!   so this mask OK
	add	%g5, %o2, %g5			! combine instr set and way bits
	ldxa	[%g5]ASI_ICACHE_INSTR, %g4	! read the instr to corrupt

#ifdef	L1_DEBUG_BUFFER
	stx	%g5, [%o3 + 0x50]		! the asi/paddr to corrupt
	stx	%g4, [%o3 + 0x58]		! instr data from cache
#endif

	or	%g4, %o4, %g4			! corrupt via parity flip
	stxa	%g4, [%g5]ASI_ICACHE_INSTR	! write it back to i$
	membar	#Sync				! .
3:
	/*
	 * Trigger the planted error by calling the routine that was
	 * corrupted above w/o leaving hypervisor context.
	 */
	jmp	%o0
	  rd	%pc, %g7

	mov	%g0, %o0			! set return value to PASS
	done					! return
	SET_SIZE(ni_inj_icache_hvinstr)
#endif	/* lint */

/*
 * Version of the asmld routine which is to be called while running in
 * hyperpriv mode ONLY from the above ni_inj_icache_hvinstr() function.
 */
#if defined(lint)
/*ARGSUSED*/
void
ni_ic_hvaccess(void)
{}
#else
	ENTRY(ni_ic_hvaccess)
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
	jmp	%g7 + 4
	  nop
        SET_SIZE(ni_ic_hvaccess)
#endif	/* lint */

/*
 * This routine is used to corrupt the instr or the instr parity of the L1
 * instruction cache line word determined a byte offset into the cache.
 * Valid offsets are in the range 0x0 to 0x3ffc (16K I-cache consisting of
 * 4-byte instrs).
 *
 * The method used is similar to the above ni_inj_icache_instr() routine.
 */

/*
 * Prototype:
 *
 * int ni_inj_iphys_instr(uint64_t offset, uint64_t xorpat, uint64_t *debug);
 *
 * Register usage:
 *
 *	%o1 - byte offset
 *	%o2 - xorpat (unused)
 *	%o3 - pointer to debug buffer (debug)
 *	%o4 - mask for parity bit
 *	%g1 - mask then the way bits from offset
 *	%g2 - unused
 *	%g3 - unused
 *	%g4 - instr read from cache
 *	%g5 - unused
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
ni_inj_iphys_instr(uint64_t offset, uint64_t xorpat, uint64_t *debug)
{
	return 0;
}
#else
	.align	64
	ENTRY_NP(ni_inj_iphys_instr)

	/*
	 * Generate register addresses to use below.
	 */
	mov	1, %g1				! set the parity bit for asi
	sllx	%g1, 32, %o4			! %o4 = parity bit flip

	mov	3, %g1				! mask and shift way bits of
	sllx	%g1, 12, %g1			!   offset
	and	%o1, %g1, %g1			!   .
	sllx	%g1, 4, %g1			! %g1 = shifted way bits

	and	%o1, 0xffc, %o1			! mask offset for asi access
	sllx	%o1, 1, %o1			! shift offset for asi access
	or	%o1, %g1, %o1			! add the way bits for access

	ldxa	[%o1]ASI_ICACHE_INSTR, %g4	! read the instr to corrupt
	or	%g4, %o4, %g4			! corrupt instr via parity flip
	stxa	%g4, [%o1]ASI_ICACHE_INSTR	! write it back to i$
	membar	#Sync				! .

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_inj_iphys_instr)
#endif	/* lint */

/*
 * This routine is used to corrupt the tag parity of the L1 instruction
 * cache line determined by the paddr.  This routine is similar to the
 * above ni_inj_icache_instr() test.
 *
 * ASI_ICACHE_TAG (0x67)
 *
 * +-------+-----+-------+-----+-------+
 * | Rsvd0 | Way | Rsvd1 | Set | Rsvd2 |
 * +-------+-----+-------+-----+-------+
 *   63:18  17:16  15:13  12:6    5:0
 *
 * Data returned as TAG[27:0] = PA[39:12], *RSVD2[31:28], PARITY[32], VALID[34]
 *
 * *NOTE: on a write, the value of the RSVD2 field is included in the tag
 *	  parity calculation.
 */

/*
 * Prototype:
 *
 * int ni_inj_icache_tag(uint64_t paddr, uint64_t xorpat, uint64_t *debug);
 *
 * Register usage:
 *
 *	%o1 - paddr (then clobbered)
 *	%o2 - xorpat (unused)
 *	%o3 - pointer to debug buffer (debug)
 *	%o4 - mask for parity bit
 *	%g1 - temp then loop counter
 *	%g2 - temp then tag for compare
 *	%g3 - way increment value
 *	%g4 - tag read from cache
 *	%g5 - addr bits for asi access
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
ni_inj_icache_tag(uint64_t paddr, uint64_t xorpat, uint64_t *debug)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(ni_inj_icache_tag)

	/*
	 * Generate register addresses to use below.
	 */
	mov	1, %g1				! set way inc value and par bit
	sllx	%g1, 16, %g3			! %g3 = way increment value
	sllx	%g1, 32, %o4			! %o4 = parity flip bit

	mov	%o1, %g5			! copy paddr to build asi addr
	srlx	%g5, 12, %g2			! shift paddr for tag compare
						!  %g2 = tag asi tag bits
	and	%g5, 0xfe0, %g5			! mask paddr for asi accesses
	sllx	%g5, 1, %g5			!  %g5 = tag asi set bits

	mov	4, %g1				! %g1 = way loop counter
	ba	1f				! branch to aligned code
	  mov	%g0, %o0			! set return value to PASS

	/*
	 * Try to find which way the data ended up in.
	 */
	.align	64
1:
	ldxa	[%g5]ASI_ICACHE_TAG, %g4	! read tag for this way
	cmp	%g4, %g2			! is the data in this way?
	be	%icc, 2f			!   (yes if lower 32-bits match)
	  sub	%g1, 1, %g1			! decrement loop counter
	brz,a	%g1, 3f				! if not found in any way then
	  mov	0xded, %o1			!   return value == error
	ba	1b				! loop again
	  add	%g5, %g3, %g5			! generate next asi addr
						!   for tag read (incr way)
	/*
	 * Found the matching tag, now corrupt the instr.
	 */
2:
	or	%g4, %o4, %g4			! corrupt via parity flip
	stxa	%g4, [%g5]ASI_ICACHE_TAG	! write tag back to i$
	membar	#Sync				! .
3:
	done					! return (value set above)
	SET_SIZE(ni_inj_icache_tag)
#endif	/* lint */

/*
 * This routine is used to corrupt the tag parity of the L1 instruction
 * cache line determined by the paddr.
 *
 * This routine is similar to the above ni_inj_icache_tag() test but here the
 * routine to corrupt is called, corrupted, and called again (to trigger the
 * error) all while in hyperpriv mode.
 */

/*
 * Prototype:
 *
 * int ni_inj_icache_hvtag(uint64_t paddr, uint64_t xorpat, uint64_t *debug);
 *
 * Register usage:
 *
 *	%o0 - paddr of routine to corrupt/call
 *	%o1 - paddr (clobbered)
 *	%o2 - xorpat (unused)
 *	%o3 - pointer to debug buffer (debug)
 *	%o4 - mask for parity bit
 *	%g1 - temp then loop counter
 *	%g2 - temp then tag for compare
 *	%g3 - way increment value
 *	%g4 - tag read from cache
 *	%g5 - addr bits for asi access
 *	%g6 - unused
 *	%g7 - return address for called routine
 */
#if defined(lint)
/* ARGSUSED */
int
ni_inj_icache_hvtag(uint64_t paddr, uint64_t xorpat, uint64_t *debug)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(ni_inj_icache_hvtag)

	/*
	 * Calling the routine to be corrupted from this routine gives us a
	 * more regular "critical section" to trigger errors from hyperpriv.
	 *
	 * Note that the call (jmp) to hv_asmld clobbers %o1.
	 */
	mov	%o1, %g5			! copy routine paddr so it can
	mov	%g5, %o0			!   be used and called below

	/*
	 * Generate register addresses to use below.
	 */
	mov	1, %g1				! set way inc value and par bit
	sllx	%g1, 16, %g3			! %g3 = way increment value
	sllx	%g1, 32, %o4			! %o4 = parity bit flip

	srlx	%g5, 12, %g2			! shift paddr for tag compare
						!  %g2 = tag asi tag bits
	and	%g5, 0xfe0, %g5			! mask paddr for asi accesses
	sllx	%g5, 1, %g5			!  %g5 = tag asi set bits

	/*
	 * Run the routine to corrupt to bring it into cache.
	 * Must ensure that it's not too far away for the jmp.
	 */
	jmp	%o0
          rd    %pc, %g7

	ba	1f				! branch to aligned code
	  mov	4, %g1				!   %g1 = way loop counter

	/*
	 * Try to find which way the data ended up in.
	 */
	.align	64
1:
	ldxa	[%g5]ASI_ICACHE_TAG, %g4	! read tag for this way
	cmp	%g4, %g2			! is the data in this way?
	be	%icc, 2f			!   (yes if lower 32-bits match)
	  sub	%g1, 1, %g1			! decrement loop counter
	brz,a	%g1, 3f				! if not found in any way then
	  mov	0xded, %o1			!   return value = error
	ba	1b				! loop again
	  add	%g5, %g3, %g5			! generate next asi addr
						!   for tag read (incr way)
	/*
	 * Found the matching tag, now corrupt the instr.
	 */
2:
	or	%g4, %o4, %g4			! corrupt via parity flip
	stxa	%g4, [%g5]ASI_ICACHE_TAG	! write tag back to i$
	membar	#Sync				! .
3:
	/*
	 * Trigger the planted error by calling the routine that was
	 * corrupted above while still in hypervisor context.
	 */
	jmp	%o0
	  rd	%pc, %g7

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_inj_icache_hvtag)
#endif	/* lint */

/*
 * This routine is used to corrupt the tag or the tag parity of the L1
 * instruction cache line determined a byte offset into the cache.
 *
 * The method used is similar to the above ni_inj_icache_tag() and
 * ni_inj_iphys_instr() routines.
 */

/*
 * Prototype:
 *
 * int ni_inj_iphys_tag(uint64_t offset, uint64_t xorpat, uint64_t *debug);
 *
 * Register usage:
 *
 *	%o1 - byte offset
 *	%o2 - xorpat (unused)
 *	%o3 - pointer to debug buffer (debug)
 *	%o4 - mask for parity bit
 *	%g1 - mask then the way bits from offset
 *	%g2 - unused
 *	%g3 - unused
 *	%g4 - tag read from cache
 *	%g5 - unused
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
ni_inj_iphys_tag(uint64_t offset, uint64_t xorpat, uint64_t *debug)
{
	return 0;
}
#else
	.align	64
	ENTRY_NP(ni_inj_iphys_tag)

	/*
	 * Generate register addresses to use below.
	 */
	mov	1, %g1				! initial mask
	sllx	%g1, 32, %o4			! %o4 = parity bit flip

	mov	3, %g1				! mask and shift way bits of
	sllx	%g1, 12, %g1			!   offset
	and	%o1, %g1, %g1			!   .
	sllx	%g1, 4, %g1			! %g1 = shifted way bits

	and	%o1, 0xfe0, %o1			! mask offset for asi access
	sllx	%o1, 1, %o1			! shift offset for asi access
	or	%o1, %g1, %o1			! add the way bits for access

	ldxa	[%o1]ASI_ICACHE_TAG, %g4	! read the tag to corrupt
	or	%g4, %o4, %g4			! corrupt tag via parity flip
	stxa	%g4, [%o1]ASI_ICACHE_TAG	! write it back to i$
	membar	#Sync				! .

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_inj_iphys_tag)
#endif	/* lint */

/*------------------ end of the I$ / start of Internal functions -----------*/

/*
 * This routine is used to corrupt the ecc of the integer register file
 * using the per core error injection register.  Because a single error
 * injection register is shared by all strands only a single strand should
 * be active for this routine to produce deterministic results.
 *
 * ASI_INJECT_ERROR_REG (0x43)
 *
 * +-------+-----+----+-----+-----+-----+-----+-----+-----+-------+---------+
 * | Rsvd0 | Enb | SS | IMD | IMT | DMD | DMT | IRC | FRC | Rsvd1 | ECCMask |
 * +-------+-----+----+-----+-----+-----+-----+-----+-----+-------+---------+
 *   63:32   31    30   29    28    27    26    25    24    23:8      7:0
 *
 * NOTE: the ECCMask bits must be written once before any of the other
 * 	 register bits are set for correct error injection.
 *
 * NOTE: if there are problems in getting the errors, make sure we are using
 *	 same register window.  Check/set using rdpr/wrpr %cwp.
 *	 This can be done by having the injection routine return the %cwp at
 *	 the time of injection.  The access routine would be passed this %cwp
 *	 to set the %cwp to the correct value for the access.
 *	 In practice the integer register file errs are detected by the
 *	 running system SW before the access takes place anyway (NWINDOWS=8),
 *	 so this is not an issue.
 */

/*
 * Prototype:
 *
 * int ni_inj_ireg_file(uint64_t buf_paddr, uint64_t enable, uint64_t eccmask,
 *				uint64_t offset);
 *
 * Register usage:
 *
 *	%o1 - paddr of mem scratch buffer
 *	%o2 - enable bit and single shot (clobbered)
 *	%o3 - ecc mask
 *	%o4 - pc relative offset for register access
 *	%g1 - temp
 *	%g2 - unused
 *	%g3 - unused
 *	%g4 - unused
 *	%g5 - unused
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
ni_inj_ireg_file(uint64_t buf_paddr, uint64_t enable, uint64_t eccmask,
				uint64_t offset)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(ni_inj_ireg_file)

	rd	%pc, %g1			! find current pc
	membar	#Sync				! . (can also be a nop)
	stxa	%o3, [%g0]ASI_INJECT_ERROR_REG	! set the eccmask bits
	add	%o2, %o3, %o2			! combine mask enable and sshot	
	jmp	%g1 + %o4			! jump ahead to reg access
	  nop

	/*
	 * Start of the specific register access section.
	 */
	stx	%i0, [%o1]			! %i0 - offset 24
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	membar	#Sync				! .
	ldx	[%o1], %i0
	ba	1f
	  nop

	stx	%l6, [%o1]			! %l6 - offset 48 (24 + 24)
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	membar	#Sync				! .
	ldx	[%o1], %l6
	ba	1f
	  nop

	stx	%o3, [%o1]			! %o3 - offset 72 (48 + 24)
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	membar	#Sync				! .
	ldx	[%o1], %o3
	ba	1f
	  nop

	/*
	 * Keep this section last due to its bigger size.
	 */
	wrpr	%g0, 0, %gl			! change %gl for correct %g1
	stx	%g1, [%o1]			! %g1 - offset 96 (72 + 24)
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	membar	#Sync				! .
	ldx	[%o1], %g1
	wrpr	%g0, 1, %gl			! restore %gl
	ba	1f
	  nop

	/*
	 * XXX will have to decide how many different registers to
	 * provide access points (offsets) for above, and the invoke
	 * routines require this as well...
	 */
1:
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_inj_ireg_file)
#endif	/* lint */

/*
 * This routine is used to corrupt the ecc of the integer register file
 * using the per core error injection register.  Because a single error
 * injection register is shared by all strands only a single strand should
 * be active for this routine to produce deterministic results.
 *
 * The method used is similar to the above ni_inj_ireg_file() routine.
 *
 * This version of the routine also accesses the corrupted register without
 * leaving hyperpriv mode in order to produce errors more reliably and
 * deterministically.
 *
 */

/*
 * Prototype:
 *
 * int ni_inj_ireg_hvfile(uint64_t buf_paddr, uint64_t enable, uint64_t eccmask,
 *				uint64_t offset);
 *
 * Register usage:
 *
 *	%o1 - paddr of mem scratch buffer
 *	%o2 - enable bit and single shot, and NOERR (clobbered)
 *	%o3 - ecc mask
 *	%o4 - pc relative offset for register access
 *	%g1 - temp
 *	%g2 - holds NOERR flag
 *	%g3 - holds OP flag
 *	%g4 - holds LOAD flag
 *	%g5 - value at target register (cannot be a target)
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
ni_inj_ireg_hvfile(uint64_t buf_paddr, uint64_t enable, uint64_t eccmask,
				uint64_t offset)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(ni_inj_ireg_hvfile)

	srlx	%o2, 32, %g2			! %g2 = NOERR access flag
	and	%g2, 1, %g2			! .

	srlx	%o2, 33, %g3			! %g3 = OP access flag
	and	%g3, 1, %g3			! .

	srlx	%o2, 34, %g4			! %g4 = LOAD access flag
	and	%g4, 1, %g4			! .

	rd	%pc, %g1			! find current pc
	membar	#Sync				! . (can also be a nop)
	stxa	%o3, [%g0]ASI_INJECT_ERROR_REG	! set the eccmask bits
	add	%o2, %o3, %o2			! combine mask enable and sshot	
	jmp	%g1 + %o4			! jump ahead to reg access
	  nop

	/*
	 * Start of the specific register access section.
	 */
	stx	%i0, [%o1]			! %i0 - offset 24
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	membar	#Sync				! .
	ldx	[%o1], %i0
	ba	1f
	  nop

	stx	%l0, [%o1]			! %l0 - offset 48 (24 + 24)
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	membar	#Sync				! .
	ldx	[%o1], %l0
	ba	1f
	  nop

	stx	%o3, [%o1]			! %o3 - offset 72 (48 + 24)
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	membar	#Sync				! .
	ldx	[%o1], %o3
	ba	1f
	  nop

	stx	%g1, [%o1]			! %g1 - offset 96 (72 + 24)
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	membar	#Sync				! .
	ldx	[%o1], %g1
	ba	1f
	  nop

	/*
	 * XXX will have to decide how many different registers to
	 * provide access points (offsets) for above, and the invoke
	 * routines require this as well... same as other reg routines.
	 */
1:
	/*
	 * New feature: perform the access right here.
	 */
	cmp	%g2, %g0			! if NOERR flag bit set
	bnz	%icc, 4f			!   skip access section
	  nop					! .

	cmp	%g3, %g0			! if OP flag bit set
	bnz	%icc, 2f			!   goto OP access section
	  nop					! .

	cmp	%g4, %g0			! if LOAD flag bit set
	bnz	%icc, 3f			!   goto LOAD access section
	  nop					! .

	/*
	 * Invoke the error via a STORE (default).
	 */
	stub	%i0, [%o1]			! %i0

	stuw	%l0, [%o1]			! %l0

	st	%o3, [%o1]			! %o3

	stx	%g1, [%o1]			! %g1

	ba	4f				! exit
	  nop
2:
	/*
	 * Invoke the error via an OP (operation).
	 */
	or	%i0, 0x5a, %i0			! %i0 - clobebred

	sllx	%l0, 0x4, %l0			! %l0 - clobbered

	add	%o3, 0x66, %o3			! %o3

	cmp	%g1, %g0			! %g1

	ba	4f				! exit
	  nop
3:
	/*
	 * Don't invoke the error via a LOAD (load will not trigger error).
	 */
	ldub	[%o1], %i0			! %i0 - clobbered

	lduw	[%o1], %l0			! %l0 - clobbered

	ld	[%o1], %o3			! %o3

	ldx	[%o1], %g1			! %g1
4:
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_inj_ireg_hvfile)
#endif	/* lint */

/*
 * This routine is used to corrupt the ecc of the floating-point register
 * file using the per core error injection register.  Because a single error
 * injection register is shared by all strands only a single strand should
 * be active for this routine to produce deterministic results.
 *
 * The method used is similar to the above ni_inj_ireg_file() routine.
 *
 * The instrs to use for the floating point ops are:
 *	ld	%f0 - %f31	! 32 bit ops
 *	ldd	%f0 - %f62	! 64 bit ops (aliased to even fregs, are 32)
 *	ldq	%f0 - %f60	! 128 bit ops (aliased to 4th freg, are 16)
 *
 * XXX	currently blowing away random floating point regs
 *	which may affect other processes if they are being used.
 */

/*
 * Prototype:
 *
 * int ni_inj_freg_file(uint64_t buf_paddr, uint64_t enable, uint64_t eccmask,
 *				uint64_t offset);
 *
 * Register usage:
 *
 *	%o1 - paddr of mem scratch buffer
 *	%o2 - enable bit and single shot (clobbered)
 *	%o3 - ecc mask
 *	%o4 - pc relative offset for register access
 *	%g1 - temp
 *	%g2 - temp
 *	%g3 - temp
 *	%g4 - saved fprs register contents
 *	%g5 - saved pstate register contents
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
ni_inj_freg_file(uint64_t buf_paddr, uint64_t enable, uint64_t eccmask,
				uint64_t offset)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(ni_inj_freg_file)

	/*
	 * Enable the floating point unit.
	 */
	rdpr	%pstate, %g5			! save the processor state
	or	%g5, PSTATE_PEF, %g4		! and enable the FPU
	wrpr	%g4, %g0, %pstate		! .

	rd	%fprs, %g4			! save the fp state
	or	%g4, FPRS_FEF, %g3		! and enable the FPU
	wr	%g0, %g3, %fprs			! .

	rd	%pc, %g1			! find current pc
	membar	#Sync				! (can also be a nop)
	stxa	%o3, [%g0]ASI_INJECT_ERROR_REG	! set the eccmask bits
	add	%o2, %o3, %o2			! combine mask enable and sshot	
	jmp	%g1 + %o4			! jump ahead to reg access
	  nop

	/*
	 * Start of the specific register access section.
	 *
	 * XXX will have to decide how many different registers to
	 * provide access points (offsets) for in the finished version.
	 */
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	membar	#Sync				! .
	ld	[%o1], %f0			! %f0 - offset 24
	nop					! to keep code aligned
	ba	1f
	  nop

	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	membar	#Sync				! .
	ldd	[%o1], %f32			! %f32 - offset 48
	nop					! to keep code aligned
	ba	1f
	  nop

	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	membar	#Sync				! .
	ldq	[%o1], %f56			! %f56 - offset 72
	nop					! to keep code aligned
	ba	1f
	  nop
1:
	wr	%g4, %g0, %fprs			! restore the fprs
	wrpr	%g5, %pstate			! restore the processor state
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_inj_freg_file)
#endif	/* lint */

/*
 * This routine uses the per core ASI_INJECT_ERROR_REG to inject a data
 * TLB data or tag parity error for a privileged or user mode error.
 *
 * Because a single data TLB (of 64 entries) and error injection register are
 * shared by all strands on a core only a single strand should be active for
 * this routine to produce deterministic results.
 *
 * IMDU, DMDU, and DMSU are corrected by the hypervisor. These errors are
 * not possible when executing in the hypervisor unless hypervisor uses
 * ASI_REAL.  These errors send a corrected resumable error to the sun4v
 * guest which triggered the error.
 *
 * IMTU and DMTU are tag parity errors detectable by the hypervisor only via
 * ASI_(D/I)TLB_TAG_READ_REG.  The memory region in error due to these errors
 * cannot be accurately determined and so they are unrecoverable.
 *
 * NOTE: Niagara errata #40 states that locked entries in slot 63 can get
 *	 overwritten.  This routine does not account for this possibility.
 *
 * NOTE: from PRM "a 3-bit PID (partition ID) field is included in each TLB
 *	 entry.", this is checked even if the real bit set.  It is used
 *	 as-is (it is not checked or set) by the routines below.
 */

/*
 * Prototype:
 *
 * int ni_inj_dtlb(uint64_t paddr, uint64_t vaddr, uint64_t sshot_enb,
 *					uint32_t ctxnum);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - kvaddr or uvaddr
 *	%o3 - single shot, select and enable  (caller picks tag/data)
 *	%o4 - context (0 for kernel/priv)
 *	%g1 - MMU_TAG_ACCESS VA
 *	%g2 - temp then tag for DATA_IN
 *	%g3 - temp
 *	%g4 - masked paddr for tte
 *	%g5 - built tte
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
ni_inj_dtlb(uint64_t paddr, uint64_t vaddr, uint64_t sshot_enb,
					uint32_t ctxnum)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(ni_inj_dtlb)

	/*
	 * Flush the dtlb except for locked entries, this is not
	 * needed for the regular DMDU errors but it is required for
	 * the DMSU store errors (and it does not hurt).
	 */
	set	(DEMAP_ALL << 6), %g1
	stxa	%g0, [%g1]ASI_DMMU_DEMAP

	/*
	 * Build the tag (for tag access reg) for the TLB load.
	 * Note that context = 0 = KCONTEXT.
	 */
	set	((1 << 13) - 1), %g2		! VA/ctx mask for tag access
	and	%o4, %g2, %o4			! mask ctx
	andn	%o2, %g2, %o2			! mask VA
	or	%o2, %o4, %g2			! %g2 = complete tag
	mov	MMU_TAG_ACCESS, %g1		! %g1 = tag access reg

	/*
	 * Build sun4u format tte - valid, cp, write, (priv or lock), size = 8k.
	 * Note that this code assumes that VA bits [63:40] are zero.
	 */
	mov	1, %g5				! set Valid bit
	sllx	%g5, 63, %g5			! .
	or	%g5, TTE4U_CP | TTE4U_CV | TTE4U_W, %g5
	brz,a	%o4, 1f				! if ctx==0 set priv bit
	  or	%g5, TTE4U_P, %g5		!   else set lock bit
	or	%g5, TTE4U_L, %g5		! .
1:
	srlx	%o1, 13, %g4			! shift paddr to clear lower
	sllx	%g4, 13, %g4			!   13 bits for tte
	or	%g5, %g4, %g5			! %g5 = complete tte

	/*
	 * Use data-in register virtual translation to load TLB.
	 */
	stxa	%g2, [%g1]ASI_DMMU		! load the tag access reg
	membar	#Sync				! .

	stxa	%o3, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	membar	#Sync				!   and options

	stxa	%g5, [%g0]ASI_DTLB_DATA_IN	! do TLB load
	membar	#Sync				! .

	mov	%g5, %o1			! return built tte as value
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_inj_dtlb)
#endif	/* lint */

/*
 * This routine uses the per core ASI_INJECT_ERROR_REG to produce a data
 * TLB data error on store (always in privileged mode).
 *
 * The method used is similar to the above ni_inj_dtlb() routine, see the
 * comments (and warnings) in the above header.
 *
 * The difference between this routine and the one above is that this one
 * injects an error into a tte then writes a new tte over the existing
 * one in order to produce a DMSU error.
 */

/*
 * Prototype:
 *
 * int ni_inj_dtlb_store(uint64_t paddr, uint64_t vaddr, uint64_t sshot_enb,
 *					uint32_t ctxnum);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - kvaddr or uvaddr
 *	%o3 - single shot, select and enable  (caller picks tag/data)
 *	%o4 - context (0 for kernel/priv)
 *	%g1 - MMU_TAG_ACCESS VA
 *	%g2 - temp then tag for DATA_IN
 *	%g3 - temp
 *	%g4 - masked paddr for tte
 *	%g5 - built tte
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
ni_inj_dtlb_store(uint64_t paddr, uint64_t vaddr, uint64_t sshot_enb,
					uint32_t ctxnum)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(ni_inj_dtlb_store)

	/*
	 * Flush the dtlb except for locked entries.
	 */
	set	(DEMAP_ALL << 6), %g1
	stxa	%g0, [%g1]ASI_DMMU_DEMAP

	/*
	 * Build the tag (for tag access reg) for the TLB load.
	 * Note that context = 0 = KCONTEXT.
	 */
	set	((1 << 13) - 1), %g2		! VA/ctx mask for tag access
	and	%o4, %g2, %o4			! mask ctx
	andn	%o2, %g2, %o2			! mask VA
	or	%o2, %o4, %g2			! %g2 = complete tag
	mov	MMU_TAG_ACCESS, %g1		! %g1 = tag access reg

	/*
	 * Build sun4u format tte - valid, cp, write, (priv or lock), size = 8k.
	 * Note that this code assumes that VA bits [63:40] are zero.
	 */
	mov	1, %g5				! set Valid bit
	sllx	%g5, 63, %g5			! .
	or	%g5, TTE4U_CP | TTE4U_CV | TTE4U_W, %g5
	brz,a	%o4, 1f				! if ctx==0 set priv bit
	  or	%g5, TTE4U_P, %g5		!   else set lock bit
	or	%g5, TTE4U_L, %g5		! .
1:
	srlx	%o1, 13, %g4			! shift paddr to clear lower
	sllx	%g4, 13, %g4			!   13 bits for tte
	or	%g5, %g4, %g5			! %g5 = complete tte

	/*
	 * Use data-in register virtual translation to load TLB.
	 */
	stxa	%g2, [%g1]ASI_DMMU		! load the tag access reg
	membar	#Sync				! .

	stxa	%o3, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	membar	#Sync				!   and options

	stxa	%g5, [%g0]ASI_DTLB_DATA_IN	! do TLB load
	membar	#Sync				! .

	/*
	 * Load in a number of other TLB entries to overwrite
	 * the previous tte (with the error) and trigger the DMSU.
	 * Note that loading an entry with the same addr will cause the
	 * HW to auto-demap (remove) the entry with the error.
	 */
	mov	0x40, %g3			! set count to 64 (all entries)
	mov	1, %g4				! load pagesize=8k into a reg
	sllx	%g4, 13, %g4			! .
2:
	add	%g5, %g4, %g5			! load entries 8k apart to
	add	%g2, %g4, %g2			!   hit the one with the error
	stxa	%g2, [%g1]ASI_DMMU		! load the tag access reg too
	stxa	%o3, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	sub	%g3, 1, %g3			! load another TLB entry
	brnz	%g3, 2b				!   done?
	  stxa	%g5, [%g0]ASI_DTLB_DATA_IN	! do TLB load
	membar	#Sync				! .

	/*
	 * Flush the dtlb except for locked entries, so that the MMU is not
	 * in an inconsistent state (from the above translations).
	 */
	nop
	nop
	set	(DEMAP_ALL << 6), %g1
	stxa	%g0, [%g1]ASI_DMMU_DEMAP

	mov	%g5, %o1			! return new tte as value
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_inj_dtlb_store)
#endif	/* lint */

/*
 * This routine uses the per core ASI_INJECT_ERROR_REG to inject a data
 * TLB data or tag parity error for a privileged or user mode error.
 *
 * The method used is similar to the above ni_inj_dtlb() routine, see the
 * comments (and warnings) in the above header.
 *
 * The difference between this routine and the one above is that this one
 * uses a sun4v format tte which is built by the kernel routines and passed
 * in directly.  Also this routine assumes kernel context (KCONTEXT = 0).
 */

/*
 * Prototype:
 *
 * int ni_inj_dtlb_v(uint64_t paddr, uint64_t vaddr, uint64_t sshot_enb,
 *					uint64_t tte);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - kvaddr or uvaddr
 *	%o3 - single shot, select and enable  (caller picks tag/data)
 *	%o4 - sun4v format tte
 *	%g1 - MMU_TAG_ACCESS VA
 *	%g2 - temp then tag for DATA_IN
 *	%g3 - temp
 *	%g4 - masked paddr for tte
 *	%g5 - temp
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
ni_inj_dtlb_v(uint64_t paddr, uint64_t vaddr, uint64_t sshot_enb,
					uint64_t tte)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(ni_inj_dtlb_v)

	/*
	 * Flush the dtlb except for locked entries (or don't).
	 */
!	set	(DEMAP_ALL << 6), %g1
!	stxa	%g0, [%g1]ASI_DMMU_DEMAP

	/*
	 * Build the tag (for tag access reg) for the TLB load.
	 * Note that context = 0 = KCONTEXT.
	 */
	set	((1 << 13) - 1), %g2		! VA/ctx mask for tag access
	andn	%o2, %g2, %g2			! %g2 = tag (ctx = 0)
	mov	MMU_TAG_ACCESS, %g1		! %g1 = tag access reg

	/*
	 * Use sun4v format tte (built by kernel routine), note that
	 * the kernel uses an RA instead of a PA which must be swapped
	 * out in the C routine.
	 */
	mov	1, %g6				! clear sun4v tte lock bit
	sllx	%g6, NI_TTE4V_L_SHIFT, %g6	! .
	andn	%o4, %g6, %o4			! %o4 = tte (no lock bit)
	set     NI_TLB_IN_4V_FORMAT, %g5	! %g5 sun4v-style tte selection

	/*
	 * Use data-in register virtual translation to load TLB.
	 */
	stxa	%g2, [%g1]ASI_DMMU		! load the tag access reg
	membar	#Sync				! .

	stxa	%o3, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	membar	#Sync				!   and options

	stxa	%o4, [%g5]ASI_DTLB_DATA_IN	! do TLB load (sun4v)
	membar	#Sync				! .

	mov	%g2, %o1			! put tag into return value
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_inj_dtlb_v)
#endif	/* lint */

/*
 * This routine uses the per core ASI_INJECT_ERROR_REG to inject an instruction 
 * TLB data or tag parity error for a privileged or user mode error.
 *
 * Because a single instruction TLB (of 64 entries) and error injection
 * register are shared by all strands on a core only a single strand should
 * be active for this routine to produce deterministic results.
 *
 * The method used is similar to the above ni_inj_dtlb() routine, see the
 * comments (and warnings) in the above header.
 */

/*
 * Prototype:
 *
 * int ni_inj_itlb(uint64_t paddr, uint64_t vaddr, uint64_t sshot_enb,
 *						uint32_t ctxnum);
 *
 * Register usage:
 *
 *	%o1 - paddr of instruction to corrupt
 *	%o2 - kvaddr or uvaddr of instruction to corrupt
 *	%o3 - single shot, select and enable  (caller picks tag/data)
 *	%o4 - context (0 for kernel/priv)
 *	%g1 - temp
 *	%g2 - temp
 *	%g3 - temp
 *	%g4 - masked paddr for tte
 *	%g5 - built tte
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
ni_inj_itlb(uint64_t paddr, uint64_t vaddr, uint64_t sshot_enb,
						uint32_t ctxnum)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(ni_inj_itlb)

	/*
	 * Flush the itlb except for locked entries (or don't).
	 */
!	set	(DEMAP_ALL << 6), %g1
!	stxa	%g0, [%g1]ASI_IMMU_DEMAP

	/*
	 * Build the tag (for tag access reg) for the TLB load.
	 * Note that context = 0 = KCONTEXT.
	 */
	set	((1 << 13) - 1), %g2		! VA/ctx mask for tag access
	and	%o4, %g2, %o4			! mask ctx
	andn	%o2, %g2, %o2			! mask VA
	or	%o2, %o4, %g2			! %g2 = complete tag
	mov	MMU_TAG_ACCESS, %g1		! %g1 = tag access reg

	/*
	 * Build sun4u format tte - valid, cp, (priv or lock), size = 64k.
	 * Note that this code assumes that VA bits [63:40] are zero.
	 */
	mov	5, %g5				! set Valid bit	and size
	sllx	%g5, 61, %g5			! .
	or	%g5, TTE4U_CP | TTE4U_CV, %g5
	brz,a	%o4, 1f				! if ctx==0 set priv bit
	  or	%g5, TTE4U_P, %g5		!   else set lock bit
	or	%g5, TTE4U_L, %g5		! .
1:
	srlx	%o1, 13, %g4			! shift paddr to clear lower
	sllx	%g4, 13, %g4			!   13 bits for tte
	or	%g5, %g4, %g5			! %g5 = complete tte

	/*
	 * Use data-in register virtual translation to load TLB.
	 */
	stxa	%g2, [%g1]ASI_IMMU		! load the tag access reg
	membar	#Sync				! .

	stxa	%o3, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	membar	#Sync				!   and options

	stxa	%g5, [%g0]ASI_ITLB_DATA_IN	! do TLB load
	membar	#Sync				! .

	mov	%g5, %o1			! return built tte as value
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_inj_itlb)
#endif	/* lint */

/*
 * This routine uses the per core ASI_INJECT_ERROR_REG to inject a data
 * or instruction TLB data or tag parity error at an unknown (random)
 * location in the TLB by not explicitly writing to the TLB.
 * This best simulates an error on a running system.
 */

/*
 * Prototype:
 *
 * int ni_inj_tlb_rand(uint64_t sshot_enb);
 *
 * Register usage:
 *
 *	%o1 - single shot, select and enable  (caller picks tag/data)
 *	%o2 - unused
 *	%o3 - unused
 *	%o4 - unused
 *	%g1 - holds single-shot flag
 *	%g2 - unused
 *	%g3 - unused
 *	%g4 - unused
 *	%g5 - unused
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
ni_inj_tlb_rand(uint64_t sshot_enb)
{
	return 0;
}
#else
	.align	64
	ENTRY_NP(ni_inj_tlb_rand)

	srlx	%o1, EI_REG_SSHOT_ENB_SHIFT, %g1 ! %g1 = SSHOT flag
	and	%g1, 1, %g1			! .

	stxa	%o1, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	membar	#Sync				!   and options

	/*
	 * If single shot mode was specified, then clear the injection reg.
	 */
	brnz,a	%g1, 1f				! check if sshot mode set
	  stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	!   if so clear injection reg
1:
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_inj_tlb_rand)
#endif	/* lint */

/*
 * The folowing function is for DEBUG purposes only.  It relies on offsets
 * into hypervisor structs which can change without warning.  It is used to
 * check what the hypervisor guest struct has for this threads partition ID.
 * This is important because the PARTID is used in all TLB tags and is
 * part of the lookup mechanism.
 *
 * Register usage:
 *
 *	%o1 - partition ID (return value)
 *	%o2 - unused
 *	%o3 - unused
 *	%o4 - unused
 *	%g1 - temp
 *	%g2 - partition ID
 *	%g3 - unused
 *	%g4 - unused
 *	%g5 - unused
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
uint64_t
ni_get_guest_partid(void)
{
	return 0;
}
#else
	.align	64
	ENTRY_NP(ni_get_guest_partid)

	PRINT("Getting partID\r\n")
	mov	HSCRATCH0, %g1			! get the guests hstruct
	ldxa	[%g1]ASI_HSCRATCHPAD, %g1	! .
	ldx	[%g1 + CPU_GUEST], %g1		! %g1 = guest struct base

	ldx	[%g1 + GUEST_PARTID], %g2	! %g2 = guest partition ID

	mov	%g2, %o1			! put partid into return value
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_get_guest_partid)
#endif	/* lint */

/*
 * This routine uses the per core ASI_MA_CONTROL_REG to inject a Modular
 * Arithmetic parity error.
 *
 * The ASI_MA_SYNC_REG (ASI 0x40, VA 0xa0) is used to enforce completion by
 * a load access (so SPARC can synchronize with the completion of an op).
 *
 * ASI_MA_CONTROL_REG (0x40, VA=0x80)
 *
 * +-------+------+--------+------+-----+----+--------+
 * | Rsvd0 | PErr | Strand | Busy | Int | Op | Length |
 * +-------+------+--------+------+-----+----+--------+
 *   63:14    13    12:11     10     9   8:6    5:0
 *
 * NOTE: the MA unit has the following properties:
 *	- MA memory is 1280B in size
 *	- Data transfers to and from MA memory bypass the L1 caches.
 *	- Parity is checked on an MA operation or an MA memory store (NOT
 *	  checked on MA memory load operations).
 *	- Load MA memory - used to load bytes into MA memory using paddr.
 *	- Store MA memory - used to store bytes from MA memory using paddr.
 *	- if Int bit not set and sync is issued an error will be sent to the
 *	  strand that initiated the sync.  But if it's an ECC error from the
 *	  MA load from memory the 'corrected_ECC_error' trap goes to the
 *	  strand in the Strand field (NOTE - err phil doc v12 differs here).
 *	- modular arithmetic state is maintained per core and is accessed via
 *	  ASI_STREAM_MA (0x40).
 *	- ASI_MA_MPA_REG (ASI 0x40, VA 0x88) bits 38:3 used to store paddr
 *	  of address to use for loads and stores.  Lower 8 bits are the word
 *	  offset (64-bits per word) into the MA memory array.
 *	- ASI_MA_ADDR_REG (ASI 0x40, VA 0x90) contains offsets for the various
 *	  operands and results of operations.
 */

/*
 * Prototype:
 *
 * int ni_inj_ma_parity(uint64_t paddr, uint_t op);
 *
 * Register usage:
 *
 *	%o1 - paddr of mem buffer to use
 *	%o2 - operation to use to inject the error
 *	%o3 - unused
 *	%o4 - unused
 *	%g1 - unused
 *	%g2 - temp
 *	%g3 - temp
 *	%g4 - unused
 *	%g5 - scratch
 *	%g6 - temp to get the current strand
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
ni_inj_ma_parity(uint64_t paddr, uint_t op)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(ni_inj_ma_parity)

	mov	ASI_MA_MPA_REG, %g2		! point MPA to buffer paddr
	andn	%o1, 0x3, %o1			! align the paddr
	stxa	%o1, [%g2]ASI_STREAM_MA		! store aligned paddr

	mov	ASI_MA_NP_REG, %g2		! set N prime value to 7
	mov	0x7, %g3			!   to be safe
	stxa	%g3, [%g2]ASI_STREAM_MA		! store value

	mov	ASI_MA_ADDR_REG, %g2		! set operand MA offsets
	setx	MA_OP_ADDR_OFFSETS, %g5, %g3	! .
	stxa	%g3, [%g2]ASI_STREAM_MA		! store offsets

	/*
	 * Perform the operation (STRAND from status reg, INT = 0, LENGTH = 3)
	 */
	mov	1, %g3				! set parity invert bit
	sllx	%g3, 13, %g3			! .
	or	%g3, 2, %g3			! set length to 3 8-byte words
	or	%g3, %o2, %g3			! set operation type

	rd	%asr26, %g6			! read the strand status reg
	and	%g6, 0x300, %g6			! keep only the strand bits
	sllx	%g6, 3, %g6			! shift to match MA reg
	or	%g3, %g6, %g3			! combine with reg contents

	mov	ASI_MA_CONTROL_REG, %g2		! set VA for operation
	stxa	%g3, [%g2]ASI_STREAM_MA		! store the value to do op

	mov	ASI_MA_SYNC_REG, %g2		! ensure operation completes
	ldxa	[%g2]ASI_STREAM_MA, %g0		! .

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_inj_ma_parity)
#endif	/* lint */

/*---------- end of test functions - start of support functions ----------*/

/*
 * This routine performs a Modular Arithmetic operation or sync to invoke
 * a previously injected error in the MA memory, L2 cache, or DRAM memory.
 *
 * MA memory errors require a store or arithmetic operation to be invoked,
 * loads to MA memory will not generate an error.  The mode of the operation
 * (interrupt or polled) determines if the error is non-resumable or resumable
 * because the type of trap will be disrupting or precise respectively.
 *
 * DRAM memory errors require that the load to MA does not hit in the L2 cache
 * so the L2 cache must be flushed prior to calling this routine.  Stores
 * from MA (that miss in L2) can also be used to invoke DRAM errors.
 *
 * L2 cache errors require that the load to MA operation hits in the L2 cache
 * at the location of the previously injected error.
 *
 * NOTE: this access routine is most useful when using an paddr that matches
 *	 the paddr used for the injection.  OR for an operation (OP) type
 *	 access, an paddr that makes use of the error which was planted in a
 *	 specific offset in the MA memory (using the addr reg offsets currently
 *	 set at 0x20).
 *
 * XXX	the inject and access routines are currently overwriting the MA
 *	setup registers for HW testing.  This is not a good thing for a real
 *	system and values must be saved/restored or even better just used
 *	in situ.  This would require some work...
 */

/*
 * Prototype:
 *
 * int ni_acc_ma_memory(uint64_t paddr, uint_t op, uint_t int_flag);
 *
 * Register usage:
 *
 *	%o1 - paddr of the start of the memory buffer
 *	%o2 - operation to use to invoke existing error
 *	%o3 - int_flag to determine interrupt or polled mode (int if != 0)
 *	%o4 - unused
 *	%g1 - unused
 *	%g2 - temp
 *	%g3 - temp
 *	%g4 - unused
 *	%g5 - scratch
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
ni_acc_ma_memory(uint64_t paddr, uint_t op, uint_t int_flag)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(ni_acc_ma_memory)

	mov	ASI_MA_MPA_REG, %g2		! point MPA to buffer paddr
	andn	%o1, 0x3, %o1			! align the paddr
	stxa	%o1, [%g2]ASI_STREAM_MA		! store aligned paddr

	mov	ASI_MA_NP_REG, %g2		! set N prime value to 7
	mov	0x7, %g3			!   to be safe
	stxa	%g3, [%g2]ASI_STREAM_MA		! store value

	mov	ASI_MA_ADDR_REG, %g2		! set operand MA offsets
	setx	MA_OP_ADDR_OFFSETS, %g5, %g3	! .
	stxa	%g3, [%g2]ASI_STREAM_MA		! store offsets

	/*
	 * Perform the operation (STRAND = 0, LENGTH = 3)
	 */
	mov	%g0, %g3			! clear register for use below
	cmp	%o3, %g0			! interrupt or poll mode?
	bz	%icc, 1f			! if poll don't set int bit
	  nop					! .
	mov	1, %g3				! set interrupt bit
	sllx	%g3, 9, %g3			! .
1:
	or	%g3, 2, %g3			! set length to 3 8-byte words
	or	%g3, %o2, %g3			! set operation type
	mov	ASI_MA_CONTROL_REG, %g2		! set VA for operation
	stxa	%g3, [%g2]ASI_STREAM_MA		! store the value to do op

	mov	ASI_MA_SYNC_REG, %g2		! ensure operation completes
	ldxa	[%g2]ASI_STREAM_MA, %g0		!   only req'd for poll mode
	membar	#Sync				! .

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_acc_ma_memory)
#endif	/* lint */

/*
 * These routines do a full clear of the Niagara L1 caches.  The L1 caches are
 * inclusive of the L2$ so flushing a line from the L2 will invalidate the
 * corresponding line in L1 however this will not clean parity errors. 
 *
 * These routines clear the data and instruction caches completely using
 * the diagnostic ASI access registers which includes correct parity.  The
 * method for these routines was taken from the hypervisor reset code.
 *
 * Prototypes:
 *
 * void ni_dcache_clear(void);
 * void ni_icache_clear(void);
 *
 * Register usage:
 *
 *	%o1 - unused
 *	%o2 - unused
 *	%o3 - unused
 *	%o4 - unused
 *	%g1 - temp
 *	%g2 - temp
 *	%g3 - temp
 *	%g4 - temp
 *	%g5 - saved LSU control register
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
void
ni_dcache_clear(void)
{}
#else
	.align	128
	ENTRY_NP(ni_dcache_clear)

	ldxa	[%g0]ASI_LSUCR, %g5		! g5 = saved the LSU cntl reg
	andn	%g5, LSUCR_DC, %g4		! disable the d$ during clear
	stxa	%g4, [%g0]ASI_LSUCR		! .
	membar	#Sync

        set	(1 << 13), %g1			! set index for data clear
1:      subcc	%g1, (1 << 3), %g1
        stxa	%g0, [%g1]ASI_DC_DATA
        bne,pt	%xcc, 1b
          nop

	set	(1 << 13), %g1                  ! set index for tag clear
2:	subcc	%g1, (1 << 4), %g1
	stxa	%g0, [%g1]ASI_DC_TAG
	bne,pt	%xcc, 2b
	  nop

	stxa	%g5, [%g0]ASI_LSUCR		! restore the LSU cntl reg
	membar	#Sync				! .

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_dcache_clear)
#endif	/* lint */

#if defined(lint)
/* ARGSUSED */
void
ni_icache_clear(void)
{}
#else
	.align	128
	ENTRY_NP(ni_icache_clear)

	ldxa	[%g0]ASI_LSUCR, %g5		! g5 = saved the LSU cntl reg
	andn	%g5, LSUCR_IC, %g4		! disable the i$ during clear
	stxa	%g4, [%g0]ASI_LSUCR		! .
	membar	#Sync

	mov	3, %g1				! set the way index
	sllx	%g1, 16, %g3
	set	(1 << 13), %g2

1:	subcc	%g2, (1 << 3), %g2
	stxa	%g0, [%g2+%g3]ASI_ICACHE_INSTR
	bne,pt	%xcc, 1b
	nop

	set	(1 << 13), %g2   
	subcc	%g1, 1, %g1
	bge,pt	%xcc, 1b
	sllx	%g1, 16, %g3

	mov	3, %g1				! set the way index
	set	(1 << 13), %g2
	sllx	%g1, 16, %g3

2:	subcc	%g2, (1 << 6), %g2
	stxa	%g0, [%g2+%g3]ASI_ICACHE_TAG
	bne,pt	%xcc, 2b
	nop

	set	(1 << 13), %g2
	subcc	%g1, 1, %g1
	bge,pt	%xcc, 2b
	sllx	%g1, 16, %g3

	stxa	%g5, [%g0]ASI_LSUCR		! restore the LSU cntl reg
	membar	#Sync				! .

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_icache_clear)
#endif	/* lint */

/*
 * This routine does a load access to a number of floating point registers
 * to invoke a previously injected register file error while in hypervisor
 * context.
 *
 * Note that the address to be loaded from must be aligned for floating point
 * accesses.
 *
 * XXX	maybe we shouldn't turn the fp unit off and on for the access, but
 *	not sure if there's an easy way to avoid it.  Perhaps could get the
 *	C-level routine to call a stub which turns the FPU on before the HV
 *	test then off when finished the access.  Of course if the access
 *	causes an unclean exit we have to ensure the FP unit is restored.
 *
 * XXX	maybe we should actually use the offset with branches to pick a
 *	specific register... see the comments in the ireg routine.
 *
 * Prototype:
 *
 * int ni_h_freg_load(uint64_t paddr, uint64_t offset);
 */
#if defined(lint)
/* ARGSUSED */
int
ni_h_freg_load(uint64_t paddr, uint64_t offset)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(ni_h_freg_load)

	/*
	 * Enable the floating point unit.
	 */
	rdpr	%pstate, %g5			! save the processor state
	or	%g5, PSTATE_PEF, %g4		! and enable the FPU
	wrpr	%g4, %g0, %pstate		! .

	rd	%fprs, %g4			! save the fp state
	or	%g4, FPRS_FEF, %g3		! and enable the FPU
	wr	%g3, %g0, %fprs			! .

/*
 * XXX	quad instructions are causing a TT-10 (illegal instruction) and the PRM
 *	notes that the FPU quad instructions are emulated in system software
 *	and so are perhaps unusable here (although ld/st _should_ be OK).
 *
 * Also note that these fload instructions don't trigger the error, the store
 * instrs do trigger the error.  Could be b/c the fregs here are targets
 * even though the PRM indicated that the ECC is checked anyway.  Maybe PRM is
 * not accurate for this case.
 */
	ld	[%o1], %f0			! %f0

	ldd	[%o1], %f32			! %f32

! XXX	ldq	[%o1], %f56			! %f56	- quads cause trap
1:
	wr	%g4, %g0, %fprs			! restore the fprs
	wrpr	%g5, %pstate			! restore the processor state
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_h_freg_load)
#endif	/* lint */

/*
 * This routine does an operation to a number of floating point registers
 * to invoke a previously injected register file error while in hypervisor
 * context.
 *
 * XXX	maybe we should actually use the offset with branches to pick a
 *	specific register... right now this is stepping all over the fregs.
 *
 * Prototype:
 *
 * int ni_h_freg_op(uint64_t offset);
 */
#if defined(lint)
/* ARGSUSED */
int
ni_h_freg_op(uint64_t offset)
{
	return 0;
}
#else
	.align	64
	ENTRY_NP(ni_h_freg_op)

	/*
	 * Enable the floating point unit.
	 */
	rdpr	%pstate, %g5			! save the processor state
	or	%g5, PSTATE_PEF, %g4		! and enable the FPU
	wrpr	%g4, %g0, %pstate		! .

	rd	%fprs, %g4			! save the fp state
	or	%g4, FPRS_FEF, %g3		! and enable the FPU
	wr	%g3, %g0, %fprs			! .

	fmuls	%f4, %f0, %f4			! %f0 - single

	faddd	%f32, %f40, %f32		! %f32 - double
! XXX	fsqrtd	%f32, %f32			! %f32 - sqrt emulated in sw

! XXX	fmulq	%f56, %f20, %f56		! %f56 - quads emulated in sw
1:
	wr	%g4, %g0, %fprs			! restore the fprs
	wrpr	%g5, %pstate			! restore the processor state
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_h_freg_op)
#endif	/* lint */

/*
 * This routine does a store access from a number of floating point registers
 * to invoke a previously injected register file error while in hypervisor
 * context (this is a store FROM the previously corrupted register).
 *
 * XXX	maybe we should actually use the offset with branches to pick a
 *	specific register like the injection routine... see above.
 *
 * Prototype:
 *
 * int ni_h_freg_store(uint64_t paddr, uint64_t offset);
 */
#if defined(lint)
/* ARGSUSED */
int
ni_h_freg_store(uint64_t paddr, uint64_t offset)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(ni_h_freg_store)

	/*
	 * Enable the floating point unit.
	 */
	rdpr	%pstate, %g5			! save the processor state
	or	%g5, PSTATE_PEF, %g4		! and enable the FPU
	wrpr	%g4, %g0, %pstate		! .

	rd	%fprs, %g4			! save the fp state
	or	%g4, FPRS_FEF, %g3		! and enable the FPU
	wr	%g3, %g0, %fprs			! .

	st	%f0, [%o1]			! %f0

	std	%f32, [%o1]			! %f32

! XXX	stq	%f56, [%o1]			! %f56  - quads cause trap 
1:
	wr	%g4, %g0, %fprs			! restore the fprs
	wrpr	%g5, %pstate			! restore the processor state
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_h_freg_store)
#endif	/* lint */

/*
 * This routine does a load access to a number of integer registers
 * to invoke a previously injected register file error while in hypervisor
 * context.
 *
 * XXX	maybe we should actually use the offset with branches to pick a
 *	specific register like in the injection routine... right now
 *	the callers %i and %l regs are being used, but this should be OK
 *	since the caller (trapper?) is the EI SV level routine.
 *
 * Prototype:
 *
 * int ni_h_ireg_load(uint64_t paddr, uint64_t offset);
 */
#if defined(lint)
/* ARGSUSED */
int
ni_h_ireg_load(uint64_t paddr, uint64_t offset)
{
	return 0;
}
#else
	.align	64
	ENTRY_NP(ni_h_ireg_load)

	ldub	[%o1], %i0			! %i0 - clobbered

	lduw	[%o1], %l0			! %l0 - clobbered

	ld	[%o1], %o3			! %o3

	ldx	[%o1], %g1			! %g1
1:
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_h_ireg_load)
#endif	/* lint */

/*
 * This routine does an operation to a number of integer registers
 * to invoke a previously injected register file error while in hypervisor
 * context.
 *
 * XXX	maybe we should actually use the offset with branches to pick a
 *	specific register... right now this is stepping on the callers
 *	%i and %l regs.
 *
 * Prototype:
 *
 * int ni_h_ireg_op(uint64_t offset);
 */
#if defined(lint)
/* ARGSUSED */
int
ni_h_ireg_op(uint64_t offset)
{
	return 0;
}
#else
	.align	64
	ENTRY_NP(ni_h_ireg_op)

	or	%i0, 0x5a, %i0			! %i0 - clobebred

	sllx	%l0, 0x4, %l0			! %l0 - clobbered

	add	%o3, 0x66, %o3			! %o3

	cmp	%g1, %g0			! %g1
1:
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_h_ireg_op)
#endif	/* lint */

/*
 * This routine does a store access from a number of integer registers
 * to invoke a previously injected register file error while in hypervisor
 * context.
 *
 * XXX	maybe we should actually use the offset with branches to pick a
 *	specific register like in the injection routine... this is stepping
 *	on the %i and %l regs right now... so yeah maybe we should...
 *
 * Prototype:
 *
 * int ni_h_ireg_store(uint64_t paddr, uint64_t offset);
 */
#if defined(lint)
/* ARGSUSED */
int
ni_h_ireg_store(uint64_t paddr, uint64_t offset)
{
	return 0;
}
#else
	.align	64
	ENTRY_NP(ni_h_ireg_store)

	stub	%i0, [%o1]			! %i0

	stuw	%l0, [%o1]			! %l0

	st	%o3, [%o1]			! %o3

	stx	%g1, [%o1]			! %g1
1:
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_h_ireg_store)
#endif	/* lint */

/*
 * This routine does a load access to a number of floating point registers
 * to invoke a previously injected register file error while in SUPERVISOR
 * context.
 *
 * This routine is similar to the above ni_h_freg_load routine.
 *
 * Prototype:
 *
 * int ni_k_freg_load(uint64_t kvaddr, uint64_t offset);
 */
#if defined(lint)
/* ARGSUSED */
int
ni_k_freg_load(uint64_t kvaddr, uint64_t offset)
{
	return 0;
}
#else
	.align	64
	ENTRY_NP(ni_k_freg_load)

	/*
	 * Enable the floating point unit.
	 */
	rdpr	%pstate, %o5			! save the processor state
	or	%o5, PSTATE_PEF, %o4		! and enable the FPU
	wrpr	%o4, %g0, %pstate		! .

	rd	%fprs, %o4			! save the fp state
	or	%o4, FPRS_FEF, %o3		! and enable the FPU
	wr	%o3, %g0, %fprs			! .

/*
 * XXX	quad instructions are causing a TT-10 (illegal instruction) and the PRM
 *	notes that the FPU quad instructions are emulated in system software
 *	and so are perhaps unusable here (although ld/st _should_ be OK).
 */
	ld	[%o0], %f0			! %f0

	ldd	[%o0], %f32			! %f32

! 	ldq	[%o0], %f56			! %f56	- quads cause trap
1:
	wr	%o4, %g0, %fprs			! restore the fprs
	wrpr	%o5, %pstate			! restore the processor state
	retl					! return PASS
	  mov %g0, %o0				! .
	SET_SIZE(ni_k_freg_load)
#endif	/* lint */

/*
 * This routine does an operation to a number of floating point registers
 * to invoke a previously injected register file error while in SUPERVISOR	
 * context.
 *
 * This routine is similar to the above ni_h_freg_op routine.
 *
 * Prototype:
 *
 * int ni_k_freg_op(uint64_t offset);
 */
#if defined(lint)
/* ARGSUSED */
int
ni_k_freg_op(uint64_t offset)
{
	return 0;
}
#else
	.align	64
	ENTRY_NP(ni_k_freg_op)

	/*
	 * Enable the floating point unit.
	 */
	rdpr	%pstate, %o5			! save the processor state
	or	%o5, PSTATE_PEF, %o4		! and enable the FPU
	wrpr	%o4, %g0, %pstate		! .

	rd	%fprs, %o4			! save the fp state
	or	%o4, FPRS_FEF, %o3		! and enable the FPU
	wr	%o3, %g0, %fprs			! .

	fmuls	%f4, %f0, %f4			! %f0 - single

	faddd	%f32, %f40, %f32		! %f32 - double
!	fsqrtd	%f32, %f32			! %f32 - sqrt emulated in sw

!	fmulq	%f56, %f20, %f56		! %f56 - quads emulated in sw
1:
	wr	%o4, %g0, %fprs			! restore the fprs
	wrpr	%o5, %pstate			! restore the processor state
	retl					! return PASS
	  mov %g0, %o0				! .
	SET_SIZE(ni_k_freg_op)
#endif	/* lint */

/*
 * This routine does a store access from a number of floating point registers
 * to invoke a previously injected register file error while in SUPERVIOR
 * context (this is a store FROM the previously corrupted register).
 *
 * This routine is similar to the above ni_h_freg_store routine.
 *
 * Prototype:
 *
 * int ni_k_freg_store(uint64_t kvaddr, uint64_t offset);
 */
#if defined(lint)
/* ARGSUSED */
int
ni_k_freg_store(uint64_t kvaddr, uint64_t offset)
{
	return 0;
}
#else
	.align	64
	ENTRY_NP(ni_k_freg_store)

	/*
	 * Enable the floating point unit.
	 */
	rdpr	%pstate, %o5			! save the processor state
	or	%o5, PSTATE_PEF, %o4		! and enable the FPU
	wrpr	%o4, %g0, %pstate		! .

	rd	%fprs, %o4			! save the fp state
	or	%o4, FPRS_FEF, %o3		! and enable the FPU
	wr	%o3, %g0, %fprs			! .

	st	%f0, [%o0]			! %f0

	std	%f32, [%o0]			! %f32

! XXX	stq	%f56, [%o0]			! %f56  - quads cause trap 
1:
	wr	%o4, %g0, %fprs			! restore the fprs
	wrpr	%o5, %pstate			! restore the processor state
	retl					! return PASS
	  mov %g0, %o0				! .
	SET_SIZE(ni_k_freg_store)
#endif	/* lint */

/*
 * This routine does a load access to a number of integer registers
 * to invoke a previously injected register file error while in SUPERVISOR
 * context.
 *
 * This is a leaf procedure that intentionally does not create a new register
 * window to ensure the correct register window is accessed (as determined by
 * the caller).  The following registers will be overwritten and care should
 * be taken to ensure the caller does not depend on their values to persist
 * across a call to this routine.
 *
 *	%l6 - overwritten
 *	%o3 - overwritten
 *	%g1 - overwritten
 *	%i0 - overwritten
 *
 * Prototype:
 *
 * int ni_k_ireg_load(uint64_t kvaddr);
 *
 */
#if defined(lint)
/* ARGSUSED */
int
ni_k_ireg_load(uint64_t kvaddr)
{
	return 0;
}
#else
	.align	64
	ENTRY_NP(ni_k_ireg_load)

	lduw	[%o0], %l6			! %l6

	ld	[%o0], %o3			! %o3

	ldx	[%o0], %g1			! %g1

	ldub	[%o0], %i0			! %i0
1:
	retl					! return PASS
	  mov %g0, %o0				! .
	SET_SIZE(ni_k_ireg_load)
#endif	/* lint */

/*
 * This routine does an operation to a number of integer registers
 * to invoke a previously injected register file error while in SUPERVISOR
 * context.
 *
 * This is a leaf procedure that intentionally does not create a new register
 * window to ensure the correct register window is accessed (as determined by
 * the caller).  The instructions here intentionally use %g0 as the destination
 * of each operation to avoid overwriting the caller's registers.
 *
 * Prototype:
 *
 * int ni_k_ireg_op(void);
 */
#if defined(lint)
/* ARGSUSED */
int
ni_k_ireg_op(void)
{
	return 0;
}
#else
	.align	64
	ENTRY_NP(ni_k_ireg_op)

	or	%i0, 0x5a, %g0			! %i0

	sllx	%l6, 0x4, %g0			! %l6

	add	%o3, 0x66, %g0			! %o3

	cmp	%g1, %g0			! %g1
1:
	retl					! return PASS
	  mov %g0, %o0				! .
	SET_SIZE(ni_k_ireg_op)
#endif	/* lint */

/*
 * This routine does a store access from a number of integer registers
 * to invoke a previously injected register file error while in SUPERVISOR
 * context.
 *
 * This is a leaf procedure that intentionally does not create a new register
 * window to ensure the correct register window is accessed (as determined by
 * the caller).
 *
 * Prototype:
 *
 * int ni_k_ireg_store(uint64_t kvaddr);
 */
#if defined(lint)
/* ARGSUSED */
int
ni_k_ireg_store(uint64_t kvaddr)
{
	return 0;
}
#else
	.align	64
	ENTRY_NP(ni_k_ireg_store)

	stub	%i0, [%o0]			! %i0

	stuw	%l6, [%o0]			! %l6

	st	%o3, [%o0]			! %o3

	stx	%g1, [%o0]			! %g1
1:
	retl					! return PASS
	  mov %g0, %o0				! .
	SET_SIZE(ni_k_ireg_store)
#endif	/* lint */

/*
 * This is a utility routine that puts the caches into their (normal)
 * multi-way replacement mode.
 *
 * NOTE: the i$ was not in 4-way mode by default on the bringup Niagara
 *	 systems.
 */
#if defined(lint)
/*ARGSUSED*/
void
ni_l1_disable_DM(void)
{}
#else
     	.align	32
	ENTRY(ni_l1_disable_DM)
	mov	0x10, %g2			! VA for LSU asi access
	stxa	%g0, [%g2]ASI_LSU_DIAG_REG	! put i$ and d$ into LFSR
	membar	#Sync				!   replacement mode

	mov	%g0, %o0			! return PASS
	done
        SET_SIZE(ni_l1_disable_DM)
#endif	/* lint */

/*
 * This is a utility routine that puts all four banks of the L2 cache into
 * it's (normal) 12-way replacement mode.
 *
 * NOTE: this routine does not save or restore the current cache state.
 *
 * Prototype:
 *
 * void ni_l2_disable_DM(caddr_t flushbase);
 *
 * Register usage:
 *
 *	%o1 - unused
 *	%o2 - unused
 *	%o3 - unused
 *	%o4 - unused
 *	%g1 - DRAM_CTL_REG addresses
 *	%g2 - temp
 *	%g3 - temp
 *	%g4 - values read from the DRAM_CTL_REGs
 *	%g5 - l2$ map mode value
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/*ARGSUSED*/
void
ni_l2_disable_DM(void)
{}
#else
     	.align	64
	ENTRY(ni_l2_disable_DM)

	mov	0xa9, %g1			! %g1=L2_CTL_REG=0xa9.0000.0000
	sllx	%g1, 32, %g1			! .
	mov	L2CR_DMMODE, %g5		! %g5 = direct map mode bit
	mov	4, %g3				! must access all four regs

1: ! ni_l2_DM_off:
	ldx	[%g1], %g4			! disable DM cache mode for
	andn	%g4, %g5, %g4			!   all four banks which also
	stx	%g4, [%g1]			!   flushes the buffers.
	sub	%g3, 1, %g3
	brnz,a	%g3, 1b
	  add	%g1, L2_BANK_STEP, %g1

	membar	#Sync				! (PRM says not needed)
	mov	%g0, %o0			! return PASS
	done
        SET_SIZE(ni_l2_disable_DM)
#endif	/* lint */

/*
 * This is a utility routine that puts all four banks of the L2 cache into
 * direct-map (DM) replacement mode, for flushing and other special purposes
 * that require we know where in the cache a value is installed.
 *
 * NOTE: this routine does not save or restore the current cache state.
 *
 * Prototype:
 *
 * void ni_l2_enable_DM(caddr_t flushbase);
 *
 * Register usage:
 *
 *	%o1 - unused
 *	%o2 - unused
 *	%o3 - unused
 *	%o4 - unused
 *	%g1 - DRAM_CTL_REG addresses
 *	%g2 - temp
 *	%g3 - temp
 *	%g4 - values read from the DRAM_CTL_REGs
 *	%g5 - l2$ map mode value
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/*ARGSUSED*/
void
ni_l2_enable_DM(void)
{}
#else
     	.align	64
	ENTRY(ni_l2_enable_DM)

	! put L2 into direct mapped. Must to this to all 4 banks separately
	mov	0xa9, %g1			! %g1=L2_CTL_REG=0xa9.0000.0000
	sllx	%g1, 32, %g1			! .
	mov	L2CR_DMMODE, %g5		! %g5 = direct map mode bit
	mov	4, %g3				! must access all four regs

1: ! ni_l2_DM_on:
	ldx	[%g1], %g4			! must do all four banks
	or	%g4, %g5, %g4
	stx	%g4, [%g1]
	sub	%g3, 1, %g3
	brnz,a	%g3, 1b
	  add	%g1, L2_BANK_STEP, %g1

	membar	#Sync				! (PRM says not needed)
	mov	%g0, %o0			! return PASS
	done
        SET_SIZE(ni_l2_enable_DM)
#endif	/* lint */

/*
 * This routine does a displacement flush of the full 3MB Niagara L2$.
 *
 * Prototype:
 *
 * void ni_l2_flushall(void);
 *
 * Register usage:
 *
 *	%o1 - unused
 *	%o2 - unused
 *	%o3 - unused
 *	%o4 - unused
 *	%g1 - temp
 *	%g2 - temp
 *	%g3 - temp
 *	%g4 - values read from the DRAM_CTL_REGs
 *	%g5 - l2$ map mode value
 *	%g6 - paddr of the flush buffer for displacement
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
void
ni_l2_flushall(caddr_t flushbase)
{}
#else
	.align	256
	ENTRY_NP(ni_l2_flushall)

	! put L2 into direct mapped. Must to this to all 4 banks separately
	mov	0xa9, %g1			! %g1=L2_CTL_REG=0xa9.0000.0000
	sllx	%g1, 32, %g1			! .
	mov	L2CR_DMMODE, %g5		! %g5 = direct map mode bit
	mov	4, %g3				! are 4 banks of regs
	ldx	[%g1], %g4			! read the base reg
	andcc	%g4, %g5, %g0			! check l2$ mode (w/o writing)
	be	%xcc, 1f
	  nop
	mov	%g0, %g5			! already in DMMODE, don't
						!   change, but write anyway
1: ! ni_flush_l2_DM_on:
	ldx	[%g1], %g4			! must do all four banks
	or	%g4, %g5, %g4
	stx	%g4, [%g1]
	sub	%g3, 1, %g3
	brnz,a	%g3, 1b
	  add	%g1, L2_BANK_STEP, %g1

2: ! ni_flush_PA_setup:

	setx	L2FLUSH_BASEADDR, %g1, %g6	! put flush BA into a reg

	srlx	%g6, 20, %g6			! align flush addr to 1 MB
	sllx	%g6, 20, %g6			! .
	add	%g0, 12, %g2			! %g2 = way counter (12 ways)

3: ! ni_flush_ways:
	add	%g0, 1024, %g3			! 1024 lines per way, per bank

4: ! ni_flush_rows:
	ldx	[%g6+0], %g0			! must flush all 4 banks
	ldx	[%g6+0x40], %g0
	ldx	[%g6+0x80], %g0
	ldx	[%g6+0xc0], %g0
	sub	%g3, 1, %g3
	brnz	%g3, 4b
	  add	%g6, 0x100, %g6
	sub	%g2, 1, %g2
	brnz,a	%g2, 3b
	  nop

	/*
	 * Once through the 3Mb area is necessary but NOT sufficient. If the
	 * last lines in the cache were dirty, then the writeback buffers may
	 * still be active. Need to always write the L2CSR at the end, whether
	 * it was changed or not to flush the cache buffers.
	 */
	mov	0xa9, %g1			! %g1=L2_CTL_REG=0xa9.0000.0000
	sllx	%g1, 32, %g1			! .
	mov	4, %g3				! must access all four regs

5: ! ni_flush_l2_DM_off:
	ldx	[%g1], %g4			! restore the cache mode for
	xor	%g4, %g5, %g4			!   all four banks which also
	stx	%g4, [%g1]			!   flushes the buffers.
	sub	%g3, 1, %g3
	brnz,a	%g3, 5b
	  add	%g1, L2_BANK_STEP, %g1

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_l2_flushall)
#endif	/* lint */

/*
 * This routine does a displacement flush of the full 3MB Niagara L2$ but
 * unlike most of the routines in this file it is run in kernel mode (not
 * hyperprivileged mode).
 *
 * Prototype:
 *
 * void ni_l2_flushall_kmode_asm(caddr_t flushbase);
 *
 * Register usage:
 *
 *	%o0 - displacement flush region base address (raddr)
 *	%o1 - cache size
 *	%o2 - cache line size
 *	%o3 - unused
 *	%o4 - unused
 *	%g1 - unused
 *	%g2 - unused
 *	%g3 - unused
 *	%g4 - unused
 *	%g5 - unused
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
void
ni_l2_flushall_kmode_asm(caddr_t flushbase, uint64_t cachesize,
				uint64_t linesize)
{}
#else
	.align	64
	ENTRY_NP(ni_l2_flushall_kmode_asm)
1:
	subcc	%o1, %o2, %o1			! go through flush region
	bg,pt	%xcc, 1b			!   accessing each address	
	  ldxa	[%o1 + %o0]ASI_REAL_MEM, %g0

	retl					! return PASS
	  mov %g0, %o0				! .
	SET_SIZE(ni_l2_flushall_kmode_asm)
#endif	/* lint */

/*
 * This routine does a displacement flush of an entry specified by it's
 * physical address, from the Niagara L2$.
 *
 * Prototype:
 *
 * void ni_l2_flushentry(caddr_t paddr2flush);
 *
 * Register usage:
 *
 *	%o1 - paddr to flush from the cache
 *	%o2 - counter for way access
 *	%o3 - way increment value
 *	%o4 - unused
 *	%g1 - temp
 *	%g2 - value read from the DRAM_CTL_REG
 *	%g3 - flushaddr
 *	%g4 - temp
 *	%g5 - DRAM_CTL_REG value l2$ map mode value
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
void
ni_l2_flushentry(caddr_t paddr2flush)
{}
#else
	.align	128
	ENTRY_NP(ni_l2_flushentry)

	/*
	 * Determine the initial L2 flush addr for the specified paddr.
	 */
	set	0x3ffc0, %g3			! PA mask for bank and set
	and	%g3, %o1, %g3			! PA[17:6] req'd way = PA[21:18]

	setx	L2FLUSH_BASEADDR, %g1, %g4	! put flush BA into a reg
	add	%g3, %g4, %g3			! %g3 = the flush addr to use

	mov	12, %o2				! %o2 = number of L2 ways
	mov	1, %o3				! %o3 = L2 way inc. value
	sllx	%o3, 18, %o3			! .

	/*
	 * Put L2 into direct mapped mode, this bank only.
	 */
	mov	0xa9, %g1			! %g1=L2_CTL_REG=0xa9.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, 0xc0, %g2			! get reg offset from paddr
	or	%g1, %g2, %g1			! include offset in reg addr

	ldx	[%g1], %g2			! %g2=prev L2_CTL_REG contents
	or	%g2, L2CR_DMMODE, %g5		! .
	stx	%g5, [%g1]			! .
	membar	#Sync				! (PRM says not needed)

	/*
	 * Flush all 12 ways (all possible locations of data).
	 */
1:
	ldx	[%g3], %g0			! access flush addr to flush
	membar	#Sync				!  data out to DRAM
	sub	%o2, 1, %o2			! decrement count
	brnz,a	%o2, 1b				! are we done all ways?
	  add	%g3, %o3, %g3			! go to next way

	stx	%g2, [%g1]			! restore this banks L2$ mode
	membar	#Sync				!  (will flush L2$ buffers)

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_l2_flushentry)
#endif	/* lint */

/*
 * This routine installs a tte into the data TLB so that the RA->PA
 * translation for the displacement flush address can be performed.
 * This allows kernel mode code do displacement flush the L2 cache
 * to generate write-back errors.
 *
 * NOTE: from PRM "a 3-bit PID (partition ID) field is included in each TLB
 *	 entry.", this is checked even if the real bit set.  It is used
 *	 as-is (it is not checked or set) by the routine below.
 */

/*
 * Prototype:
 *
 * int ni_install_tte(uint64_t paddr, uint64_t raddr);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - raddr
 *	%o3 - unused
 *	%o4 - unused
 *	%g1 - MMU_TAG_ACCESS VA
 *	%g2 - temp then tag for DATA_IN
 *	%g3 - temp
 *	%g4 - masked paddr for tte
 *	%g5 - built tte
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
void
ni_install_tte(uint64_t paddr, uint64_t raddr)
{}
#else
	.align	64
	ENTRY_NP(ni_install_tte)

	/*
	 * Build the tag (for tag access reg) for the TLB load.
	 * Note that context = 0 = KCONTEXT.
	 */
	set	((1 << 13) - 1), %g2		! VA/ctx mask for tag access
	and	%g0, %g2, %g0			! mask ctx (force KCONTEXT)
	andn	%o2, %g2, %g2			! %g2 = complete tag
	mov	MMU_TAG_ACCESS, %g1		! %g1 = tag access reg

	/*
	 * Build sun4u format tte - valid, cacheable, lock, priv, size = 4M.
	 * Note that this code assumes that VA bits [63:40] are zero.
	 */
	mov	7, %g5				! set Valid bit	and size
	sllx	%g5, 61, %g5			! .
	or	%g5, TTE4U_CP | TTE4U_CV, %g5	! set cacheable bits
	or	%g5, TTE4U_P | TTE4U_L, %g5	! also set priv and lock bit
1:
	srlx	%o1, 13, %g4			! shift paddr to clear lower
	sllx	%g4, 13, %g4			!   13 bits for tte
	or	%g5, %g4, %g5			! %g5 = complete tte

	/*
	 * Use data-in register virtual translation to load TLB.
	 */
	stxa	%g2, [%g1]ASI_DMMU		! load the tag access reg
	membar	#Sync				! .

	mov	1, %g3				! set REAL bit for translation
	sllx	%g3, 9, %g3			! .
	stxa	%g5, [%g3]ASI_DTLB_DATA_IN	! do TLB load (REAL bit set)
	membar	#Sync				! .

	mov	%g5, %o1			! return built tte as value
	mov	%g0, %o0			! clear return status
	done					! .
	SET_SIZE(ni_install_tte)
#endif	/* lint */

/*
 * This MACRO is used to print out the contents of a section of memory
 * that is formatted as an eReport.
 *
 * The erpt pointer should be passed in %g6 since %g6 is preserved across
 * print routines. The second argument, reg1, should be %g1, which is
 * used as the argument to PRINTX.
 *
 * Arguments:
 *	%g1 - as reg1
 *	%g6 - as erpt - pointer to the cpu error buffer
 *
 * NOTE: %g1 - %g6 registers are used (clobbered), since the print MACROs
 *	 clobber most of the %g regs.
 *
 *	 ALSO this MACRO is massive because of all the contained print MACROs
 *	 and any routine using this MACRO must be aligned to a large boundary.
 */
#define	CONSOLE_PRINT_DIAG_ERPT(erpt, reg1)	\
	PRINT("----EI START----\r\n");		; \
	PRINT("ehdl = ");				; \
	ldx	[erpt + ERPT_EHDL], reg1		/* ehdl */	; \
	PRINTX(reg1)			; \
	PRINT("\r\n");			; \
	PRINT("stick = ");			; \
	ldx	[erpt + ERPT_STICK], reg1		/* stick */	; \
	PRINTX(reg1)			; \
	PRINT("\r\n");			; \
	PRINT("cpuver = ");			; \
	ldx	[erpt + ERPT_CPUVER], reg1		/* cpuver */	; \
	PRINTX(reg1)			; \
	PRINT("\r\n");			; \
	PRINT("sparc_afsr = ");			; \
	ldx	[erpt + ERPT_SPARC_AFSR], reg1		/* sparc afsr */ ; \
	PRINTX(reg1)			; \
	PRINT("\r\n");			; \
	PRINT("sparc_afar = ");			; \
	ldx	[erpt + ERPT_SPARC_AFAR], reg1		/* sparc afar */ ; \
	PRINTX(reg1)			; \
	PRINT("\r\n");			; \
	PRINT("L2 ESRs\r\n");			; \
	ldx	[erpt + ERPT_L2_AFSR], reg1			; \
	PRINTX(reg1);			; \
	PRINT("\r\n");			; \
	ldx	[erpt + ERPT_L2_AFSR + ERPT_L2_AFSR_INCR], reg1	; \
	PRINTX(reg1);	; \
	PRINT("\r\n");	; \
	ldx	[erpt + ERPT_L2_AFSR + 2*ERPT_L2_AFSR_INCR], reg1	; \
	PRINTX(reg1);	; \
	PRINT("\r\n");	; \
	ldx	[erpt + ERPT_L2_AFSR + 3*ERPT_L2_AFSR_INCR], reg1	; \
	PRINTX(reg1);	; \
	PRINT("\r\n");	; \
	PRINT("L2 EARs\r\n");	; \
	ldx	[erpt + ERPT_L2_AFAR], reg1	; \
	PRINTX(reg1);	; \
	PRINT("\r\n");	; \
	ldx	[erpt + ERPT_L2_AFAR + ERPT_L2_AFAR_INCR], reg1	; \
	PRINTX(reg1);	; \
	PRINT("\r\n");	; \
	ldx	[erpt + ERPT_L2_AFAR + 2*ERPT_L2_AFAR_INCR], reg1	; \
	PRINTX(reg1);	; \
	PRINT("\r\n");	; \
	ldx	[erpt + ERPT_L2_AFAR + 3*ERPT_L2_AFAR_INCR], reg1	; \
	PRINTX(reg1);	; \
	PRINT("\r\n");	; \
	PRINT("DRAM ESRs\r\n");	; \
	ldx	[erpt + ERPT_DRAM_AFSR], reg1	; \
	PRINTX(reg1);	; \
	PRINT("\r\n");	; \
	ldx	[erpt + ERPT_DRAM_AFSR + ERPT_DRAM_AFSR_INCR], reg1	; \
	PRINTX(reg1);	; \
	PRINT("\r\n");	; \
	ldx	[erpt + ERPT_DRAM_AFSR + 2*ERPT_DRAM_AFSR_INCR], reg1	; \
	PRINTX(reg1);	; \
	PRINT("\r\n");	; \
	ldx	[erpt + ERPT_DRAM_AFSR + 3*ERPT_DRAM_AFSR_INCR], reg1	; \
	PRINTX(reg1);	; \
	PRINT("\r\n");	; \
	PRINT("DRAM EARs\r\n");	; \
	ldx	[erpt + ERPT_DRAM_AFAR], reg1	; \
	PRINTX(reg1);	; \
	PRINT("\r\n");	; \
	ldx	[erpt + ERPT_DRAM_AFAR + ERPT_DRAM_AFAR_INCR], reg1	; \
	PRINTX(reg1);	; \
	PRINT("\r\n");	; \
	ldx	[erpt + ERPT_DRAM_AFAR + 2*ERPT_DRAM_AFAR_INCR], reg1	; \
	PRINTX(reg1);	; \
	PRINT("\r\n");	; \
	ldx	[erpt + ERPT_DRAM_AFAR + 3*ERPT_DRAM_AFAR_INCR], reg1	; \
	PRINTX(reg1);	; \
	PRINT("\r\n");	; \
	PRINT("DRAM ELRs\r\n");	; \
	ldx	[erpt + ERPT_DRAM_LOC], reg1	; \
	PRINTX(reg1);	; \
	PRINT("\r\n");	; \
	ldx	[erpt + ERPT_DRAM_LOC + ERPT_DRAM_LOC_INCR], reg1	; \
	PRINTX(reg1);	; \
	PRINT("\r\n");	; \
	ldx	[erpt + ERPT_DRAM_LOC + 2*ERPT_DRAM_LOC_INCR], reg1	; \
	PRINTX(reg1);	; \
	PRINT("\r\n");	; \
	ldx	[erpt + ERPT_DRAM_LOC + 3*ERPT_DRAM_LOC_INCR], reg1	; \
	PRINTX(reg1);	; \
	PRINT("\r\n");	; \
	PRINT("DRAM ECRs\r\n");	; \
	ldx	[erpt + ERPT_DRAM_CNTR], reg1	; \
	PRINTX(reg1);	; \
	PRINT("\r\n");	; \
	ldx	[erpt + ERPT_DRAM_CNTR + ERPT_DRAM_CNTR_INCR], reg1	; \
	PRINTX(reg1);	; \
	PRINT("\r\n");	; \
	ldx	[erpt + ERPT_DRAM_CNTR + 2*ERPT_DRAM_CNTR_INCR], reg1	; \
	PRINTX(reg1);	; \
	PRINT("\r\n");	; \
	ldx	[erpt + ERPT_DRAM_CNTR + 3*ERPT_DRAM_CNTR_INCR], reg1	; \
	PRINTX(reg1);	; \
	PRINT("\r\n");	; \
	PRINT("tstate = ");	; \
	ldx	[erpt + ERPT_TSTATE], reg1		/* tstate */	; \
	PRINTX(reg1)	; \
	PRINT("\r\n");	; \
	PRINT("htstate = ");	; \
	ldx	[erpt + ERPT_HTSTATE], reg1		/* htstate */ ; \
	PRINTX(reg1)	; \
	PRINT("\r\n");	; \
	PRINT("tpc = ");	; \
	ldx	[erpt + ERPT_TPC], reg1			/* tpc */	; \
	PRINTX(reg1)	; \
	PRINT("\r\n");	; \
	PRINT("cpuid = ");	; \
	lduh	[erpt + ERPT_CPUID], reg1		/* cpuid */	; \
	PRINTX(reg1)	; \
	PRINT("\r\n");	; \
	PRINT("TT = ");	; \
	lduh	[erpt + ERPT_TT], reg1			/* tt */	; \
	PRINTX(reg1)	; \
	PRINT("\r\n");	; \
	PRINT("TL = ");	; \
	ldub	[erpt + ERPT_TL], reg1			/* tl */	; \
	PRINTX(reg1)	; \
	PRINT("\r\n");	; \
	PRINT("-----EI END-----\r\n");

/*
 * The following two routines print to the console the current UE or CE
 * hypervisor error report.  They use the same macro to perform this
 * task (CONSOLE_PRINT_DIAG_ERPT defined above).
 *
 * The macro requires that the base of the report be un %g6, and
 * it expects the scratch register to be %g1.
 *
 * NOTE: these routines rely on offsets in the GUEST struct which contain
 *	 the base of the UE and CE error reports.  This means that these
 *	 rountines can get out of sync with the hypervisor version as the
 *	 guest struct changes, and will then produce unexpected results.
 *
 *	 An enhancement would be to take the base address as an inpar which
 *	 the memtest driver could query the hypervisor for.
 */
#if defined(lint)
/* ARGSUSED */
void
ni_print_ce_errs(void)
{}
#else
	.align	4096
	ENTRY_NP(ni_print_ce_errs)
 	mov	HSCRATCH0, %g2			! get guests hstruct
 	ldxa	[%g2]ASI_HSCRATCHPAD, %g2	! .
 	ldx	[%g2 + CPU_CE_RPT], %g6		! start of CE report

	CONSOLE_PRINT_DIAG_ERPT(%g6, %g1);	! %g1 = scratch (all %g clob'd)

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_print_ce_errs)
#endif	/* lint */

#if defined(lint)
/* ARGSUSED */
void
ni_print_ue_errs(void)
{}
#else
	.align	4096
	ENTRY_NP(ni_print_ue_errs)
 	mov	HSCRATCH0, %g2			! get guests hstruct base
 	ldxa	[%g2]ASI_HSCRATCHPAD, %g2	! .
 	ldx	[%g2 + CPU_UE_RPT], %g6		! start of UE report

	CONSOLE_PRINT_DIAG_ERPT(%g6, %g1);	! %g1 = scratch (all %g clob'd)

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_print_ue_errs)
#endif	/* lint */

/*
 * The following routines print out the contents of the instruction cache
 * by using the provided ASI access registers.  The icache has 512 (0x200)
 * 32 byte lines for a total size of 16KB.
 *
 * This routine prints out the cache data in reverse order, so index 0 is
 * the last line printed.
 *
 * NOTE: the print MACROs clobber most of the %g regs (PRINTX uses %g1-%g5)
 * 	  and they use most of the lower relative labels.  This is why the
 *	  label 9 is used, it must be relative and unique.
 *
 * Prototypes:
 *
 * void ni_print_icache(void);
 * void ni_print_itag(void);
 *
 * Register usage:
 *
 *	%o1 - temp
 *	%o2 - temp
 *	%o3 - temp
 *	%o4 - temp
 *	%g1 - temp (print)
 *	%g2 - temp (print)
 *	%g3 - temp (print)
 *	%g4 - temp (print)
 *	%g5 - temp (print)
 *	%g6 - saved LSU control register
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
void
ni_print_icache(void)
{}
#else
	.align	2048
	ENTRY_NP(ni_print_icache)
	PRINT("\n\r");

	ldxa	[%g0]ASI_LSUCR, %g6		! g6 = saved LSU cntl reg
	andn	%g6, LSUCR_IC, %g4		! disable the i$ during print
	stxa	%g4, [%g0]ASI_LSUCR		! .
	membar	#Sync

	mov	3, %o1				! set the way index
	sllx	%o1, 16, %o3			! .
	set	(1 << 13), %o2			! set the set index

9:	subcc	%o2, (1 << 3), %o2		! subtract LSB of word (in set)
	PRINTX(%o2); PRINT(": ");
	ldxa	[%o2+%o3]ASI_ICACHE_INSTR, %o4	! get and print index and data
	PRINTX(%o4); PRINT(" ");

	subcc	%o2, (1 << 3), %o2		! subtract LSB of word (in set)
	ldxa	[%o2+%o3]ASI_ICACHE_INSTR, %o4	! get and print index and data
	PRINTX(%o4); PRINT(" ");

	subcc	%o2, (1 << 3), %o2		! subtract LSB of word (in set)
	ldxa	[%o2+%o3]ASI_ICACHE_INSTR, %o4	! get and print index and data
	PRINTX(%o4); PRINT("\n\r");
	brgez,pt %o2, 9b			! while >=0 keep going
	  nop

	PRINT("\n\r ------Next way------ \n\n\r");

	set	(1 << 13), %o2			! set the set index
	subcc	%o1, 1, %o1			! go to next way
	bge,pt	%xcc, 9b
	  sllx	%o1, 16, %o3

	stxa	%g6, [%g0]ASI_LSUCR		! restore the LSU cntl reg
	membar	#Sync				! .

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_print_icache)
#endif	/* lint */

#if defined(lint)
/* ARGSUSED */
void
ni_print_itag(void)
{}
#else
	.align	2048
	ENTRY_NP(ni_print_itag)

	ldxa	[%g0]ASI_LSUCR, %g6		! g6 = saved LSU cntl reg
	andn	%g6, LSUCR_IC, %g4		! disable the i$ during print
	stxa	%g4, [%g0]ASI_LSUCR		! .
	membar	#Sync

	mov	3, %o1				! set the way index
	sllx	%o1, 16, %o3			! .
	set	(1 << 13), %o2			! set the set index

9:	subcc	%o2, (1 << 6), %o2		! subtract LSB of set
	ldxa	[%o2+%o3]ASI_ICACHE_TAG, %o4	! get and print index and tag
	PRINTX(%o2); PRINT(":");
	PRINTX(%o4); PRINT("\n\r");
	brnz,pt	%o2, 9b				! while !=0 keep going
	  nop

	PRINT("\n\r ------Next way------ \n\n\r");

	set	(1 << 13), %o2			! set the set index
	subcc	%o1, 1, %o1			! go to next way
	bge,pt	%xcc, 9b
	  sllx	%o1, 16, %o3

	stxa	%g6, [%g0]ASI_LSUCR		! restore the LSU cntl reg
	membar	#Sync				! .

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_print_itag)
#endif	/* lint */

/*
 * Routine to print the contents of the JBI registers in order to
 * determine if errors are being logged by the HW correctly.
 *
 * The buffer address passed in (%o1) is used to store copies of the
 * JBI register vals so the kernel can manipulate the values.
 *
 * This routine clobbers %g1 - %g4 inclusive.
 *
 * NOTE: %g1, %g2 and %g3 are clobbered by the put routines
 * 	 so they can't be used for any storage across PRINT macros.
 *	 ALSO the PRINTX macro clobbers %g1 - %g5.
 */
#if defined(lint)
/* ARGSUSED */
void
ni_print_jbi_regs(uint64_t *buffer)
{}
#else
	.align	8192
	ENTRY_NP(ni_print_jbi_regs)
	PRINT("JBI_ERR_CONFIG\r\n");
	setx	JBI_ERR_CONFIG, %g2, %g3
	ldx	[%g3], %g1
	stx	%g1, [%o1 + 0x0]
	PRINTX(%g1);
	PRINT("\r\n");

	PRINT("JBI_ERR_LOG\r\n");
	setx	JBI_ERR_LOG, %g2, %g3
	ldx	[%g3], %g1
	stx	%g1, [%o1 + 0x8]
	PRINTX(%g1);
	PRINT("\r\n");

	PRINT("JBI_ERR_OVF\r\n");
	setx	JBI_ERR_OVF, %g2, %g3
	ldx	[%g3], %g1
	stx	%g1, [%o1 + 0x10]
	PRINTX(%g1);
	PRINT("\r\n");

	PRINT("JBI_LOG_ENB\r\n");
	setx	JBI_LOG_ENB, %g2, %g3
	ldx	[%g3], %g1
	stx	%g1, [%o1 + 0x18]
	PRINTX(%g1);
	PRINT("\r\n");

	PRINT("JBI_SIG_ENB\r\n");
	setx	JBI_SIG_ENB, %g2, %g3
	ldx	[%g3], %g1
	stx	%g1, [%o1 + 0x20]
	PRINTX(%g1);
	PRINT("\r\n");

	PRINT("JBI_LOG_ADDR\r\n");
	setx	JBI_LOG_ADDR, %g2, %g3
	ldx	[%g3], %g1
	stx	%g1, [%o1 + 0x28]
	PRINTX(%g1);
	PRINT("\r\n");

	PRINT("JBI_LOG_DATA0\r\n");
	setx	JBI_LOG_DATA0, %g2, %g3
	ldx	[%g3], %g1
	stx	%g1, [%o1 + 0x30]
	PRINTX(%g1);
	PRINT("\r\n");

	PRINT("JBI_LOG_DATA1\r\n");
	setx	JBI_LOG_DATA1, %g2, %g3
	ldx	[%g3], %g1
	stx	%g1, [%o1 + 0x38]
	PRINTX(%g1);
	PRINT("\r\n");

	PRINT("JBI_LOG_CTRL\r\n");
	setx	JBI_LOG_CTRL, %g2, %g3
	ldx	[%g3], %g1
	stx	%g1, [%o1 + 0x40]
	PRINTX(%g1);
	PRINT("\r\n");

	PRINT("JBI_LOG_PAR\r\n");
	setx	JBI_LOG_PAR, %g2, %g3
	ldx	[%g3], %g1
	stx	%g1, [%o1 + 0x48]
	PRINTX(%g1);
	PRINT("\r\n");

	PRINT("JBI_LOG_NACK\r\n");
	setx	JBI_LOG_NACK, %g2, %g3
	ldx	[%g3], %g1
	stx	%g1, [%o1 + 0x50]
	PRINTX(%g1);
	PRINT("\r\n");

	PRINT("JBI_LOG_ARB\r\n");
	setx	JBI_LOG_ARB, %g2, %g3
	ldx	[%g3], %g1
	stx	%g1, [%o1 + 0x58]
	PRINTX(%g1);
	PRINT("\r\n");

	PRINT("JBI_MEMSIZE\r\n");
	setx	JBI_MEMSIZE, %g2, %g3
	ldx	[%g3], %g1
	stx	%g1, [%o1 + 0x60]
	PRINTX(%g1);
	PRINT("\r\n");

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_print_jbi_regs)
#endif	/* lint */

/*
 * This function is a utility to print out to the console
 * the contents of memory in 64-bit chunks.
 *
 * NOTE: %g1, %g2 and %g3 are clobbered by the put routines so
 *	 they can't be used for storage across PRINT calls.
 *
 * Prototype:
 *
 * void ni_print_mem(uint64_t paddr, uint64_t count);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - number of 8-byte mem locations to print
 *	%o3 - unused
 *	%o4 - unused
 *	%g1 - temp
 *	%g2 - temp
 *	%g3 - temp
 *	%g4 - unused
 *	%g5 - unused
 *	%g6 - saved counter
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
void
ni_print_mem(uint64_t paddr, uint64_t count)
{}
#else
	.align	2048
	ENTRY_NP(ni_print_mem)
	mov	%o2, %g6			! put counter in a safe place
	mov	%o1, %g1
	PRINTX(%g1);
	PRINT(":\t");
9:						! label must be unique+relative
	ldx	[%o1], %g2			! grab mem contents
	PRINTX(%g2);				! print the data
	PRINT(" ");

	sub	%g6, 1, %g6			! next location or finish
	brnz,a	%g6, 9b				! .
	  add	%o1, 8, %o1			! .

	PRINT("\r\n");				! end of the data
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_print_mem)
#endif	/* lint */

/*
 * This routine is a simple function to determine if the injector routines are
 * being called correctly through the hcall interface (API).
 *
 * The first instruction is the value to look for when dumping memory.
 *
 *		%o0-%o3	- arg0 to arg3
 *		---
 *		%o0 - return status (PASS = 0)
 *		%o1 - return value (0xa55 payload is a PASS)
 *
 * NOTE: the PRINT(X) macros clobber most of the %g registers.
 */
#if defined(lint)
/* ARGSUSED */
int
ni_test_case(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3)
{
	return 0;
}
#else
	.align	2048
	ENTRY_NP(ni_test_case)
	clr	%o1				! = 0x92100000

	/*
	 * Removed the direct print calls from HV since the UART can
	 * be in different locations on different systems.
	 */
!	PRINT("Injector running in hypervisor mode!\r\n")

	mov	0xa55, %o1			! put values in the regs
	mov	0xa56, %o2
	mov	0xa57, %o3
	mov	0xa58, %o4

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(ni_test_case)
#endif	/* lint */
