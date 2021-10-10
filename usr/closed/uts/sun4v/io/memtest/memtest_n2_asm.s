/*
 * Copyright (c) 2006, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Niagara-II (UltraSPARC-T2) error injector assembly and hyperprivileged
 * assembly routines.
 *
 * NOTE: the functions in this file are grouped according to type and
 *	 are therefore not in alphabetical order unlike other files.
 */

#include <sys/memtest_v_asm.h>
#include <sys/memtest_n2_asm.h>

/*
 * Coding rules for Niagara-II routines that run in hypervisor mode:
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
 * Niagara-II cache details (ALL are PIPT)
 *
 *	Cache	Size	Ways	sz/way	linesz	instrs/line	num bytes
 *	------------------------------------------------------------------
 *	L2	4MB	16	256KB	64B	16		0x40.0000
 *	i$	16KB	8	2KB	32B	8		   0x4000
 *	d$	8KB	4	2KB	16B	4		   0x2000
 *	iTLB	-	fully	64 entries
 *	dTLB	-	fully	128 entries
 */

/*
 * Enable/disable the storing of DEBUG values into the debug mem buffer
 * for different test types.
 */
/* #define	MEM_DEBUG_BUFFER	1 */
/* #define	L1_DEBUG_BUFFER		1 */
/* #define	L2_DEBUG_BUFFER		1 */

#define	HV_UART	0xfff0c2c000	/* DEBUG (unsafe) define for the print macros */

/*
 * MACROs to use while running in HV mode.  All these must be defined
 * as MACROs (not global functions or labels) and must not call or branch
 * to other routines or labels symbolically.
 *
 * Remember to be careful with the relative branches when using these MACROs
 * in code which also has relative branch labels (can get number overlap).
 */

/*
 * L2_FLUSH_BASEADDR - get a 4MB-aligned DRAM address for L2$ flushing.
 * The assumption is that %htba contains a valid DRAM address
 * for the current machine configuration.  Round it down to a 4MB
 * boundary to use as a base address for L2$ flushing.
 */
#define	L2_FLUSH_BASEADDR(addr, scr)		\
	rdhpr	%htba, addr			;\
	set	(4 * 1024 * 1024) - 1, scr	;\
	andn	addr, scr, addr

/*
 * This macro performs the L2 index hashing (IDX) on a provided
 * physical address.  The hashed address will be returned in the
 * first variable (containing the original address).
 *
 *	 Index hashing performs the following operations:
 *		PA[17:13] = PA[32:28] xor PA[17:13]
 *		PA[12:11] = PA[19:18] xor PA[12:11]
 */
#define	N2_PERFORM_IDX_HASH(addr, lomask, himask)	\
	mov	0x1f, himask				;\
	sllx	himask, 28, himask			;\
							;\
	mov	0x3, lomask				;\
	sllx	lomask, N2_L2_WAY_SHIFT, lomask		;\
							;\
	and	addr, himask, himask			;\
	and	addr, lomask, lomask			;\
							;\
	srlx	himask, 15, himask			;\
	xor	addr, himask, addr			;\
							;\
	srlx	lomask, 7, lomask			;\
	xor	addr, lomask, addr

/*
 * Park the sibling strands of the current running cpu. The current running
 * strand is determined by reading the strand status reg, then the sibling
 * strands are parked via the CORE_RUNNING regs.
 *
 * NOTE: this busy wait to ensure the core running status register updates
 *	 is unsafe because there is no timeout!
 *
 * See above comment about MACRO rules.
 *
 * Register usage:
 *
 *	src0 - temp for ASI VAs
 *	src1 - temp then value read from status reg to check running strands
 *	src2 - the bit representing the current strand
 *	src3 - the strands to park (all local strands but current)
 *	src4 - mask of the eight strands on current core
 */
#define	N2_PARK_SIBLING_STRANDS(scr0, scr1, scr2, scr3, scr4)	\
	mov	ASI_CMT_STRAND_ID, scr0		;\
	ldxa	[scr0]ASI_CMT_CORE_REG, scr1	;\
	and	scr1, 0x3f, scr1		;\
	mov	1, scr2				;\
	sllx	scr2, scr1, scr2		;\
	orn	%g0, scr2, scr3			;\
						;\
	and	scr1, 0x38, scr1		;\
	mov	N2_LOCAL_CORE_MASK, scr0	;\
	sllx	scr0, scr1, scr4		;\
	and	scr3, scr4, scr3		;\
						;\
	mov	ASI_CORE_RUNNING_W1C, scr0	;\
	stxa	scr3, [scr0]ASI_CMT_REG		;\
						;\
	mov	ASI_CORE_RUNNING_STS, scr0	;\
8:						;\
	ldxa	[scr0]ASI_CMT_REG, scr1		;\
	and	scr1, scr4, scr1		;\
	cmp	scr1, scr2			;\
	bne	%xcc, 8b			;\
	  nop

/*
 * Unpark the sibling strands of the current running cpu. This is very similar
 * to the above parking MACRO.
 *
 * NOTE: this busy wait to ensure the core running status register updates
 *	 is unsafe because there is no timeout!
 *
 * See above comment about MACRO rules.
 *
 * Register usage:
 *
 *	src0 - temp for ASI VAs
 *	src1 - temp then value read from status reg to check running strands
 *	src2 - the strands to unpark (all local strands but current)
 *	src3 - mask of the eight strands on current core
 */
#define	N2_UNPARK_SIBLING_STRANDS(scr0, scr1, scr2, scr3)	\
	mov	ASI_CMT_STRAND_ID, scr0		;\
	ldxa	[scr0]ASI_CMT_CORE_REG, scr1	;\
	and	scr1, 0x3f, scr1		;\
	mov	1, scr2				;\
	sllx	scr2, scr1, scr2		;\
	orn	%g0, scr2, scr2			;\
						;\
	and	scr1, 0x38, scr1		;\
	mov	N2_LOCAL_CORE_MASK, scr0	;\
	sllx	scr0, scr1, scr3		;\
	and	scr2, scr3, scr2		;\
						;\
	mov	ASI_CORE_RUNNING_W1S, scr0	;\
	stxa	scr2, [scr0]ASI_CMT_REG		;\
						;\
	mov	ASI_CORE_RUNNING_STS, scr0	;\
9:						;\
	ldxa	[scr0]ASI_CMT_REG, scr1		;\
	and	scr1, scr3, scr1		;\
	cmp	scr1, scr3			;\
	bne	%xcc, 9b			;\
	  nop

/*
 * Print an ascii string.  See above comment about MACRO rules.
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
 * Print a hexadecimal value.  See above comment about MACRO rules.
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
 * There is one DRAM_ERROR_INJECT_REG in each of the four DRAM branches,
 * (channels) the bank register sets are available at offsets of 4096
 * (based on PA[7:6] just like on Niagara-I).
 *
 * NOTE: the entire L2$ should be flushed immediately before this function.
 *
 * NOTE: this routine is immune to the L2-cache IDX setting since no
 *	 indexes are used only actual addresses for the displacement.
 *
 * NOTE: this routine is based on the Niagara-I version ni_inj_memory().
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
 * int n2_inj_memory_debug(uint64_t paddr, uint_t eccmask, uint_t offset,
 *			uint64_t *debug);
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
 *	%g5 - temp (scratch)
 *	%g6 - addr of matching DRAM_ERROR_INJECT_REG
 *	%g7 - L2$ access values (debug)
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_memory_debug(uint64_t paddr, uint_t eccmask, uint_t offset,
			uint64_t *debug)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(n2_inj_memory_debug)

	/*
	 * Build DRAM EI register address.
	 */
	mov	N2_DRAM_CSR_BASE_MSB, %g4	! set DRAM select bits
	sllx	%g4, 32, %g4			! .
	or	%g4, N2_DRAM_ERROR_INJ_REG, %g4	! add injection offset
	add	%g4, %o3, %g6			! add register offset
						! %g6 = complete EI reg addr

#ifdef MEM_DEBUG_BUFFER
	stx	%o1, [%o4 + 0x0]		! paddr
	stx	%g6, [%o4 + 0x8]		! addr of DRAM_EI_REG
#endif

#ifdef MEM_DEBUG_BUFFER
	/*
	 * Note: using the IDX flush method in the debug code.
	 *
	 * Determine the L2 flush-ICE index for the specified paddr.
	 * Since the cache will be in DM-mode only need the one index.
	 */
	sllx	%o1, 31, %g3			! remove MSB bits but keep all
	srlx	%g3, 31, %g3			!   bits used by idx (PA[32:6])

	mov	N2_L2_PREFETCHICE_MSB, %g1	! set the key field of addr
	sllx	%g1, 32, %g1			! .
	or	%g3, %g1, %g3			! %g3 = ICE flush index to use
#else
	/*
	 * Determine the L2 flush addr for the specified paddr.
	 */
	set	0x3ffff8, %g3			! PA mask for bank, set, way
	and	%g3, %o1, %g3			! PA[21:6] req'd, is PA[21:3]

	L2_FLUSH_BASEADDR(%g4, %g5)		! put flush BA into a reg (%g4)
	add	%g3, %g4, %g3			! %g3 = the flush addr to use
#endif

	/*
	 * Put L2 into direct mapped mode, this bank only.
	 */
	mov	N2_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0xa9.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, N2_L2_BANK_MASK, %g2	! get reg offset from paddr
	or	%g1, %g2, %g1			! include offset in reg addr

	ldx	[%g1], %g2			! %g2=prev L2_CTL_REG contents
	or	%g2, N2_L2CR_DMMODE, %g5	! .
	stx	%g5, [%g1]			! .
	membar	#Sync				! (PRM says not needed)

#ifdef MEM_DEBUG_BUFFER
	stx	%g1, [%o4 + 0x10]		! addr of L2_CTL_REG
	stx	%g5, [%o4 + 0x18]		! new contents of L2_CTL_REG
	stx	%g3, [%o4 + 0x20]		! addr of flush value to use!
#endif

#ifdef MEM_DEBUG_BUFFER
	/*
	 * DEBUG: Build a register for L2$ DEBUG accesses.
	 *	  First need to IDX'ify the physical address.
	 */
	mov	0x1f, %g4			! make mask for IDX top
	sllx	%g4, 28, %g4			!   bits[32:28]
	and	%o1, %g4, %g4			! get the top address bits
	srlx	%g4, 15, %g4			! shift 'em down
	xor	%o1, %g4, %g5			! %g5 = result of first xor

	mov	0x3, %g4			! make mask for IDX bottom
	sllx	%g4, 18, %g4			!   bits[19:18]
	and	%o1, %g4, %g4			! get bottom address bits
	srlx	%g4, 7, %g4			! shift 'em down
	xor	%g5, %g4, %g5			! %g5 = IDX'd address

	mov	N2_L2_DIAG_DATA_MSB, %g4	! set select bits
	sllx	%g4, 32, %g4			! .
	set	0x3ffff8, %g7			! mask paddr to get $line
	and	%g5, %g7, %g7			!  way, set, bank (is PA[21:3])
	or	%g7, %g4, %g7			! %g7 = L2_DIAG_DATA
#endif
	ldx	[%o1], %g4			! touch the data
	
#ifdef MEM_DEBUG_BUFFER
	stx	%g4, [%o4 + 0x28]		! contents of mem location
	stx	%o2, [%o4 + 0x30]		! value written to DRAM_EI_REG
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

#ifdef	MEM_DEBUG_BUFFER
	stx	%g7, [%o4 + 0x38]		! addr for L2_DIAG_DATA
	ldx	[%g7], %g4			! read back L2 data (via reg)
	sllx	%g4, 1, %g4			! shift it so it's readable
	stx	%g4, [%o4 + 0x40]		!  should not invoke the error
#endif
	stx	%o2, [%g6]			! write value to DRAM EI reg
	membar	#Sync				!  (error on next DRAM store)
	ldx	[%g6], %g0			! read back reg to ensure it
	membar	#Sync				!  completed (as per PRM)

#ifdef	MEM_DEBUG_BUFFER
	/*
	 * Using the ICE method for debug case.
	 */
	prefetch [%g3], INVALIDATE_CACHE_LINE	! use prefetch-ICE to flush
	membar	#Sync				!  data out to DRAM with error
#else
	ldx	[%g3], %g5			! access flush addr to flush
	membar	#Sync				!  data out to DRAM with error
#endif

#ifndef	MEM_DEBUG_BUFFER
	/*
	 * Due to 2-channel mode HW limitation, clear ecc mask if sshot mode.
	 */
	mov	1, %g4				! create mask for sshot bit
	sllx	%g4, 30, %g4			! .
	and	%g4, %o2, %g4			! check if sshot is set
	cmp	%g4, %g0			! .
	bnz,a	%xcc, 4f			! if it is set then
	  stx	%g0, [%g6]			!   clear the DRAM EI reg
#endif

4:

#ifdef	MEM_DEBUG_BUFFER
	ldx	[%g6], %o2			! read inj reg to see if clear
	membar	#Sync		
	stx	%o2, [%o4 + 0x60]		! new val read from DRAM_EI_REG
	ldx	[%g7], %g4			! read back L2 data (via reg)
	sllx	%g4, 1, %g4			! shift it so it's readable
	stx	%g4, [%o4 + 0x68]		!  should not invoke the error
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

	SET_SIZE(n2_inj_memory_debug)
#endif	/* lint */

/*
 * This routine is used to corrupt the ecc bits of external memory (DRAM).
 * It is similar to the above n2_inj_memory() routine but the debug code
 * is removed and a new inpar added to accept differnt address masks for
 * the registers which depend on the available DRAM and L2 banks.
 *
 * This routine uses a displacement flush to push the specific data out
 * to DRAM.
 */

/*
 * Prototype:
 *
 * int n2_inj_memory_disp(uint64_t paddr, uint_t eccmask, uint_t offset,
 *			uint64_t bankmask);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - eccmask and sshot
 *	%o3 - register offset
 *	%o4 - address mask for enabled banks
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
n2_inj_memory_disp(uint64_t paddr, uint_t eccmask, uint_t offset,
			uint64_t bankmask)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(n2_inj_memory_disp)

	/*
	 * Build DRAM EI register address.
	 */
	mov	N2_DRAM_CSR_BASE_MSB, %g4	! set DRAM select bits
	sllx	%g4, 32, %g4			! .
	or	%g4, N2_DRAM_ERROR_INJ_REG, %g4	! add injection offset
	add	%g4, %o3, %g6			! add register offset
						!   %g6 = complete EI reg addr
	/*
	 * Determine the L2 flush addr for the specified paddr.
	 */
	set	0x3ffff8, %g3			! PA mask for bank, set, way
	and	%g3, %o1, %g3			! PA[21:6] req'd, is PA[21:3]

	L2_FLUSH_BASEADDR(%g4, %g5)		! put flush BA into a reg (%g4)
	add	%g3, %g4, %g3			! %g3 = the flush addr to use

	/*
	 * Put L2 into direct mapped mode, this bank only.
	 */
	mov	N2_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0xa9.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, %o4, %g2			! get L2 reg offset from paddr
	or	%g1, %g2, %g1			! include offset in reg addr

	ldx	[%g1], %g2			! %g2=prev L2_CTL_REG contents
	or	%g2, N2_L2CR_DMMODE, %g5	! .
	stx	%g5, [%g1]			! .
	membar	#Sync				! (PRM says not needed)

	ldx	[%o1], %g4			! touch the data

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

	stx	%o2, [%g6]			! write value to DRAM EI reg
	membar	#Sync				!  (error on next DRAM store)
	ldx	[%g6], %o2			! read back reg to ensure it
	membar	#Sync				!  completed (as per PRM)

	ldx	[%g3], %g5			! access flush addr to flush
	membar	#Sync				!  data out to DRAM with error

	/*
	 * Due to reduced bank/branch/channel mode HW limitation,
	 * clear ecc mask if sshot mode.
	 */
	mov	1, %g4				! create mask for sshot bit
	sllx	%g4, 30, %g4			! .
	and	%g4, %o2, %g4			! check if sshot is set
	cmp	%g4, %g0			! .
	bnz,a	%xcc, 4f			! if it is set then
	  stx	%g0, [%g6]			!   clear the DRAM EI reg
4:
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

	SET_SIZE(n2_inj_memory_disp)
#endif	/* lint */

/*
 * This routine is used to corrupt the ecc bits of external memory (DRAM).
 * It is similar to the above n2_inj_memory_disp() routine but the
 * displacement flush is replaced by the Niagara-II prefectch-ICE
 * instruction in order to flush the specific data out to DRAM.
 * This is used because the displacement flush may not work correctly when
 * IDX mode (index hashing is enabled).
 */

/*
 * Prototype:
 *
 * int n2_inj_memory(uint64_t paddr, uint_t eccmask, uint_t offset,
 *			uint64_t bankmask);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - eccmask and sshot
 *	%o3 - register offset
 *	%o4 - address mask for enabled banks
 *	%g1 - temp then address of L2_CTL_REG
 *	%g2 - temp then saved L2_CTL_REG contents
 *	%g3 - temp then ICE-index for the flush
 *	%g4 - temp then data read addr (data not used)
 *	%g5 - temp
 *	%g6 - addr of matching DRAM_ERROR_INJECT_REG
 *	%g7 - L2$ access values (debug)
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_memory(uint64_t paddr, uint_t eccmask, uint_t offset, uint64_t bankmask)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(n2_inj_memory)

	/*
	 * Build DRAM EI register address.
	 */
	mov	N2_DRAM_CSR_BASE_MSB, %g4	! set DRAM select bits
	sllx	%g4, 32, %g4			! .
	or	%g4, N2_DRAM_ERROR_INJ_REG, %g4	! add injection offset
	add	%g4, %o3, %g6			! add register offset
						!   %g6 = complete EI reg addr
	/*
	 * Determine the L2 flush-ICE index for the specified paddr.
	 * Since the cache will be in DM-mode only need the one index,
	 * and since the flush-ICE goes through the hash-logic it's just
	 * a masked PA.
	 */
	sllx	%o1, 31, %g3			! remove MSB bits but keep all
	srlx	%g3, 31, %g3			!   bits used by idx (PA[32:6])

	mov	N2_L2_PREFETCHICE_MSB, %g1	! set the key field of ICE-index
	sllx	%g1, 32, %g1			! .
	or	%g3, %g1, %g3			! %g3 = ICE flush index to use

	/*
	 * Put L2 into direct mapped mode, this bank only.
	 */
	mov	N2_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0xa9.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, %o4, %g2			! get reg offset from paddr
	or	%g1, %g2, %g1			! include offset in reg addr

	ldx	[%g1], %g2			! %g2=prev L2_CTL_REG contents
	or	%g2, N2_L2CR_DMMODE, %g5	! .
	stx	%g5, [%g1]			! .
	membar	#Sync				! (PRM says not needed)

	ldx	[%o1], %g4			! touch the data

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

	stx	%o2, [%g6]			! write value to DRAM EI reg
	membar	#Sync				!  (error on next DRAM store)
	ldx	[%g6], %o2			! read back reg to ensure it
	membar	#Sync				!  completed (as per PRM)

	prefetch [%g3], INVALIDATE_CACHE_LINE	! use prefetch-ICE to flush
	membar	#Sync				!  data out to DRAM with error

	/*
	 * Due to 2-channel mode HW limitation, clear ecc mask if sshot mode.
	 */
	mov	1, %g4				! create mask for sshot bit
	sllx	%g4, 30, %g4			! .
	and	%g4, %o2, %g4			! check if sshot is set
	cmp	%g4, %g0			! .
	bnz,a	%xcc, 4f			! if it is set then
	  stx	%g0, [%g6]			!   clear the DRAM EI reg
4:
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

	SET_SIZE(n2_inj_memory)
#endif	/* lint */

/*
 * This routine is used to corrupt the ecc bits of external memory (DRAM).
 * It is very similar to the above n2_inj_memory() routine but it
 * has been simplified to expect that the data has already been
 * installed in the L2 cache (in DM mode).  This means that the following
 * steps have been removed:
 *	- the L2 cache is not placed (and later restored) into DM mode
 *	- the data is not accessed (which would bring it into the caches)
 *
 * The step which puts the data in the modified (M) state was also planned
 * to be removed (do not think it's needed for the ICE flush) but found
 * that the system was slightly more likely to HANG with it removed.
 * These are the three lines after the "1:" marker.
 */

/*
 * Prototype:
 *
 * int n2_inj_memory_quick(uint64_t paddr, uint_t eccmask, uint_t offset,
 *			uint64_t bankmask);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - eccmask and sshot
 *	%o3 - register offset
 *	%o4 - address mask for enabled banks
 *	%g1 - temp
 *	%g2 - unused
 *	%g3 - temp then ICE-index for the flush
 *	%g4 - temp then data read addr (data not used)
 *	%g5 - temp
 *	%g6 - addr of matching DRAM_ERROR_INJECT_REG
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_memory_quick(uint64_t paddr, uint_t eccmask, uint_t offset,
			uint64_t bankmask)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(n2_inj_memory_quick)

	/*
	 * Build DRAM EI register address.
	 */
	mov	N2_DRAM_CSR_BASE_MSB, %g4	! set DRAM select bits
	sllx	%g4, 32, %g4			! .
	or	%g4, N2_DRAM_ERROR_INJ_REG, %g4	! add injection offset
	add	%g4, %o3, %g6			! add register offset
						!   %g6 = complete EI reg addr
	/*
	 * Determine the L2 flush-ICE index for the specified paddr.
	 * Since the cache will be in DM-mode only need the one index,
	 * and since the flush-ICE goes through the hash-logic it's just
	 * a masked PA.
	 */
	sllx	%o1, 31, %g3			! remove MSB bits but keep all
	srlx	%g3, 31, %g3			!   bits used by idx (PA[32:6])

	mov	N2_L2_PREFETCHICE_MSB, %g1	! set the key field of ICE-index
	sllx	%g1, 32, %g1			! .
	or	%g3, %g1, %g3			! %g3 = ICE flush index to use

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

	stx	%o2, [%g6]			! write value to DRAM EI reg
	membar	#Sync				!  (error on next DRAM store)
	ldx	[%g6], %o2			! read back reg to ensure it
	membar	#Sync				!  completed (as per PRM)

	prefetch [%g3], INVALIDATE_CACHE_LINE	! use prefetch-ICE to flush
	membar	#Sync				!  data out to DRAM with error

	/*
	 * Due to 2-channel mode HW limitation, clear ecc mask if sshot mode.
	 */
	mov	1, %g4				! create mask for sshot bit
	sllx	%g4, 30, %g4			! .
	and	%g4, %o2, %g4			! check if sshot is set
	cmp	%g4, %g0			! .
	bnz,a	%xcc, 4f			! if it is set then
	  stx	%g0, [%g6]			!   clear the DRAM EI reg
4:
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

	SET_SIZE(n2_inj_memory_quick)
#endif	/* lint */

/*--------------------- end of mem / start of L2$ routines -----------------*/

/*
 * This routine is used to corrupt the data or the data ecc bits of an L2
 * cache line determined by the paddr ONLY when the cache line is not for
 * an instruction access.  The L2_DIAG_DATA register address is built by
 * this routine, bits [8:6] of the paddr select the cache bank.
 *
 * Note that this routine expects a 64-bit aligned xorpat and shifts it
 * down if the corruption is to be in the upper 32-bit word.  This means
 * that the upper and lower words cannot be corrupted at the same time
 * (and the lower value takes precidence).  This is due to HW limitations.
 *
 * The L2 cache is 4MB with a linesize of 64B which yeilds 65536 (0x10000)
 * lines split into 4096 (0x1000) lines per each of the 16 ways.
 *
 * Note also that by setting and restoring the L2$ DM mode has the effect
 * of clearing all the L2$ write buffers.  This occurs even if the mode does
 * not actually change since it is triggered by the control reg write.
 *
 * NOTE: this routine is based on the Niagara-I version ni_inj_l2cache_data().
 *
 * L2_DIAG_DATA (offset 0x0) Access (stride 8, plus use of odd/even field) 
 *
 * +-------+--------+-------+---------+-----+-----+------+------+-------+
 * | Rsvd0 | Select | Rsvd1 | OddEven | Way | Set | Bank | Word | Rsvd2 |
 * +-------+--------+-------+---------+-----+-----+------+------+-------+
 *   63:40   39:32    31:23     22     21:18  17:9  8:6    5:3     2:0
 *
 * Data returned (32-bit half word) as DATA[38:7], ECC[6:0] (same as Niagara-I)
 */

/*
 * Prototype:
 *
 * int n2_inj_l2cache_data(uint64_t paddr, uint64_t xorpat, uint_t checkflag,
 *				uint64_t idx_paddr);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - xorpat (clobbered)
 *	%o3 - check-bit flag _or_ debug buffer (debug)
 *	%o4 - idx_paddr, the paddr after IDX index hashing
 *	%g1 - temp then address of L2_CTL_REG
 *	%g2 - temp then address for data read/write
 *	%g3 - temp then data read from cache
 *	%g4 - saved L2_CTL_REG contents
 *	%g5 - counter for L1$ clear
 *	%g6 - tag to use for L1 cache flush
 *	%g7 - L1 way increment value
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_l2cache_data(uint64_t paddr, uint64_t xorpat, uint_t checkflag,
				uint64_t idx_paddr)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(n2_inj_l2cache_data)

	/*
	 * Generate register addresses to use below.
	 */
	cmp	%o2, %g0			! if xorpat in upper word shift
	bnz	%icc, 1f			!   down since 32-bit access
	  nop					! .
	srlx	%o2, 32, %o2			! corrupt next 32-bit entry
	add	%o4, 4, %o4			! .
1:
	and	%o4, 4, %g3			! extract 32B paddr boundary
	sllx	%g3, 20, %g3			! move result to OddEven field
	andn	%o1, 0x7, %o1			! align paddr for asi access
	andn	%o4, 0x7, %o4			! align idx_paddr for asi access

#ifdef	L2_DEBUG_BUFFER
	stx	%o1, [%o3 + 0x0]		! aligned paddr
	stx	%o4, [%o3 + 0x8]		! aligned idx_paddr
	stx	%o2, [%o3 + 0x10]		! xor pat
#else
	cmp	%o3, %g0			! if check-bit flag == 0
	bz,a,pt	%xcc, 2f			! shift the xorpat to data bits
	  sllx	%o2, 7, %o2			! .
#endif

	/*
	 * Build the register values for L1$ access.
	 * The d$ way bits are bits 12:11, the set bits are 10:4.
	 */
2:
	and	%o1, 0x7f0, %g6			! %g6 = PA[10:4] for L1 tag asi
	mov	1, %g7				! %g7 = L1 way inc. value
	sllx	%g7, 11, %g7			! .
	mov	4, %g5				! %g5 = number of L1 ways

	/*
	 * Build the register values for L2$ accesses.
	 */
	mov	N2_L2_DIAG_DATA_MSB, %g4	! set reg select bits
	sllx	%g4, 32, %g4			! .
	set	0x3ffff8, %g2			! mask idx_paddr to get $line
	and	%o4, %g2, %g2			!  way, set, bank (is PA[21:3])
	or	%g2, %g3, %g2			! or in the OddEven bit
	or	%g2, %g4, %g2			! %g2 = L2_DIAG_DATA

#ifdef	L2_DEBUG_BUFFER
	stx	%g2, [%o3 + 0x20]		! addr for L2_DIAG_DATA
#endif

	/*
	 * Put L2 into direct mapped mode, this bank only.
	 */
	mov	N2_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0xa9.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, N2_L2_BANK_MASK, %g3	! get reg offset from paddr
	or	%g1, %g3, %g1			! include offset in reg addr

	ldx	[%g1], %g4			! %g4=prev L2_CTL_REG contents
	or	%g4, N2_L2CR_DMMODE, %g3	! .
	stx	%g3, [%g1]			! .
	membar	#Sync				! (PRM says not needed)

#ifdef	L2_DEBUG_BUFFER
	stx	%g1, [%o3 + 0x28]		! addr of L2_CTL_REG
	stx	%g3, [%o3 + 0x30]		! new contents of L2_CTL_REG
#endif

	ba	3f				! branch to aligned code
	  nop					! .

	.align	64
3:
	ldx	[%o1], %g3			! get data into L2$ and L1$
	membar	#Sync				! .

#ifdef	L2_DEBUG_BUFFER
	stx	%g3, [%o3 + 0x40]		! contents of mem location
#endif

	ldx	[%g2], %g3			! read L2$ data to corrupt
						!   via L2_DIAG_DATA reg
	xor	%g3, %o2, %g3			! corrupt data or ecc bits

#ifdef	L2_DEBUG_BUFFER
	stx	%g3, [%o3 + 0x48]		! data after bit(s) flipped
#endif

	stx	%g3, [%g2]			! write the data back to L2$
	membar	#Sync				!   via L2_DIAG_DATA reg

#ifdef	L2_DEBUG_BUFFER
	ldx	[%g2], %g3			! read back the data (via reg)
	sllx	%g3, 1, %g3			! shift it so it's readable
	stx	%g3, [%o3 + 0x50]		!  should not invoke the error
#endif

	/*
	 * Clear the the L1$ entry so access does not hit there.
	 */
4:
#ifdef	L2_DEBUG_BUFFER
	ldxa	[%g6]ASI_DC_TAG, %o2		! look at the d$ tags
	sllx	%o2, 9, %o2			! move it up for compare
	sllx	%g5, 3, %o0			! try to get all four
	add	%o3, %o0, %o0			! add offset to debug_buffer
	stx	%o2, [%o0 + 0x60]		! store the tag
#endif
	stxa	%g0, [%g6]ASI_DC_TAG		! clear all four ways of L1
	sub	%g5, 1, %g5			! .
	brnz,a	%g5, 4b
	  add	%g6, %g7, %g6			! go to next way

	stx	%g4, [%g1]			! restore this banks L2$ mode
						!   which will flush L2$ buffers
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_inj_l2cache_data)
#endif	/* lint */

/*
 * This routine is used to corrupt the data or the data ecc bits of an L2
 * cache line which is an instruciton determined by its paddr.
 * The L2_DIAG_DATA register address is built by this routine, bits [8:6]
 * of the paddr select the cache bank.
 *
 * Note that this routine expects a 64-bit aligned xorpat and shifts it
 * down if the corruption is to be in the upper 32-bit word.  This means
 * that the upper and lower words cannot be corrupted at the same time
 * (and the lower value takes precidence).  This is due to HW limitations.
 *
 * The method used is similar to the above n2_inj_l2cache_data() routine,
 * but note that the calling routine must place the L2 into DM mode.
 */

/*
 * Prototype:
 *
 * int n2_inj_l2cache_instr_data(uint64_t paddr, uint64_t xorpat,
 *					uint_t checkflag, uint64_t idx_paddr);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - xorpat (clobbered)
 *	%o3 - checkbit flag
 *	%o4 - idx_paddr, the paddr after IDX index hashing
 *	%g1 - temp then address of L2_CTL_REG
 *	%g2 - temp then address for data read/write
 *	%g3 - temp then data read from cache
 *	%g4 - temp then saved L2_CTL_REG contents
 *	%g5 - counter for L1$ clear
 *	%g6 - tag to use for L1 cache flush
 *	%g7 - L1 way increment value
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_l2cache_instr_data(uint64_t paddr, uint64_t xorpat, uint_t checkflag,
				uint64_t idx_paddr)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(n2_inj_l2cache_instr_data)

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
	 * The i$ way bits are bits 14:12 (8-way), and index bits are PA[10:5].
	 */
	and	%o1, 0x7e0, %g6			! mask paddr for L1 asi
	sllx	%g6, 1, %g6			! %g6 = L1 asi access addr
	mov	1, %g7				! set way inc. value
	sllx	%g7, 12, %g7			! %g7 = L1 way inc. value
	mov	8, %g5				! %g5 = number of L1 ways

	/*
	 * Build the register values for L2$ accesses.
	 */
	mov	N2_L2_DIAG_DATA_MSB, %g4	! set reg select bits
	sllx	%g4, 32, %g4			! .
	set	0x3ffff8, %g2			! mask paddr to get $line
	and	%o4, %g2, %g2			!  way, set, bank (is PA[21:3])
	add	%g2, %g3, %g2			! combine into complete reg
	add	%g2, %g4, %g2			! %g2 = L2_DIAG_DATA

	/*
	 * Build L2 Control Register for this bank and read value.
	 */
	mov	N2_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0xa9.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, N2_L2_BANK_MASK, %g3	! get reg offset from paddr
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
	stxa	%g0, [%g6]ASI_ICACHE_TAG	! clear all eight ways of L1
	sub	%g5, 1, %g5			! .
	brnz,a	%g5, 4b
	  add	%g6, %g7, %g6			! go to next way

	stx	%g4, [%g1]			! write to banks L2_CTL_REG
						!   to flush write buffers
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_inj_l2cache_instr_data)
#endif	/* lint */

/*
 * This routine is used to corrupt the data or the data ecc bits of an L2
 * cache line determined by a physical address.  The L2_DIAG_DATA register
 * address is built by this routine, bits [8:6] of the paddr select the
 * cache bank.
 *
 * The method used is similar to the above n2_inj_l2cache_data() routine,
 * but it has been simplified to expect that the data has already been
 * installed in the cache (in DM mode).  This means that the following
 * steps have been removed:
 *	- the L2 cache is not placed (and later restored) into DM mode
 *	- the data is not accessed (which would bring it into the caches)
 */

/*
 * Prototype:
 *
 * int n2_inj_l2cache_data_quick(uint64_t offset, uint64_t xorpat,
 *				uint_t checkflag, uint64_t idx_paddr);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - xorpat (clobbered)
 *	%o3 - checkbit flag
 *	%o4 - idx_paddr, the paddr after IDX index hashing
 *	%g1 - temp then address of L2_CTL_REG
 *	%g2 - temp then address for data read/write
 *	%g3 - temp then data read from cache
 *	%g4 - saved L2_CTL_REG contents
 *	%g5 - counter for L1$ clear
 *	%g6 - tag to use for L1 cache flush
 *	%g7 - L1 way increment value
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_l2cache_data_quick(uint64_t offset, uint64_t xorpat, uint_t checkflag,
						uint64_t idx_paddr)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(n2_inj_l2cache_data_quick)

	cmp	%o2, %g0			! if xorpat in upper word shift
	bnz	%icc, 1f			!   down since 32-bit access
	  nop					! .
	srlx	%o2, 32, %o2			! corrupt next 32-bits
	add	%o1, 4, %o1			! .
1:
	and	%o1, 4, %g3			! extract 32B paddr boundary
	sllx	%g3, 20, %g3			! move result to OddEven field
	andn	%o4, 0x7, %o4			! align idx_paddr for asi access

	/*
	 * Build the register values for L1$ access.
	 * The d$ way bits are bits 12:11, the set bits are 10:4.
	 */
2:
	and	%o1, 0x7f0, %g6			! %g6 = PA[10:4] for L1 tag asi
	mov	1, %g7				! %g7 = L1 way inc. value
	sllx	%g7, 11, %g7			! .
	mov	4, %g5				! %g5 = number of L1 ways

	/*
	 * Build the register values for L2$ accesses.
	 */
	mov	N2_L2_DIAG_DATA_MSB, %g4	! set reg select bits
	sllx	%g4, 32, %g4			! .
	set	0x3ffff8, %g2			! mask idx_paddr to get $line
	and	%o4, %g2, %g2			!  way, set, bank (is PA[21:3])
	or	%g2, %g3, %g2			! or in the OddEven bit
	or	%g2, %g4, %g2			! %g2 = L2_DIAG_DATA

	/*
	 * Build L2 Control Register for this bank and read value.
	 */
	mov	N2_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0xa9.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, N2_L2_BANK_MASK, %g3	! get reg offset from offset
	add	%g1, %g3, %g1			! include offset in reg addr
	ldx	[%g1], %g4			! %g4=prev L2_CTL_REG contents

	ba	3f				! branch to aligned code
	  nop					! .

	.align	64
3:
	ldx	[%g2], %g3			! read L2$ data to corrupt
						!   via L2_DIAG_DATA reg
	cmp	%o3, %g0			! if check-bit flag == 0
	bz,a,pt	%xcc, 4f			! shift the xorpat to data bits
	  sllx	%o2, 7, %o2			! .
4:
	xor	%g3, %o2, %g3			! corrupt data or ecc bits
	stx	%g3, [%g2]			! write the data back to L2$
	membar	#Sync				! .

	/*
	 * Clear the the L1$ entry so access does not hit there.
	 */
5:
	stxa	%g0, [%g6]ASI_DC_TAG		! clear all four ways of L1
	sub	%g5, 1, %g5			! .
	brnz,a	%g5, 5b
	  add	%g6, %g7, %g6			! go to next way

	stx	%g4, [%g1]			! store to flush write buffers
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_inj_l2cache_data_quick)
#endif	/* lint */

/*
 * This routine is used to corrupt the data or the data ecc bits of an L2
 * cache line determined by a byte offset.  The L2_DIAG_DATA register address
 * is built by this routine, bits [8:6] of the offset select the cache bank.
 *
 * Note that this routine expects a 64-bit aligned xorpat and shifts it
 * down if the corruption is to be in the upper 32-bit word.  This means
 * that the upper and lower words cannot be corrupted at the same time
 * (and the lower value takes precidence).  This is due to HW limitations.
 *
 * The method used is similar to the above n2_inj_l2cache_data() routine,
 * but note that the data is not removed from the L1-cache.
 *
 * NOTE: this routine is immune to the L2-cache IDX setting since a
 *	 byte offset is used for the injection and not an address.
 *
 * NOTE: Could make a version that does the L1 scrub which would likely
 *	 produce more errors but then there is no real difference from the
 *	 regular test using "-n" with a "-a" address and this routine.
 */

/*
 * Prototype:
 *
 * int n2_inj_l2phys_data(uint64_t offset, uint64_t xorpat, uint_t checkflag,
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
n2_inj_l2phys_data(uint64_t offset, uint64_t xorpat, uint_t checkflag,
						uint64_t *debug)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(n2_inj_l2phys_data)

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
	mov	N2_L2_DIAG_DATA_MSB, %g4	! set reg select bits
	sllx	%g4, 32, %g4			! .
	set	0x3ffff8, %g2			! mask offset to get $line
	and	%o1, %g2, %g2			! .
	or	%g2, %g3, %g2			! combine into complete reg
	or	%g2, %g4, %g2			! %g2 = L2_DIAG_DATA

	/*
	 * Build L2 Control Register for this bank and read value.
	 */
	mov	N2_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0xa9.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, N2_L2_BANK_MASK, %g3	! get reg offset from offset
	add	%g1, %g3, %g1			! include offset in reg addr
	ldx	[%g1], %g4			! %g4=prev L2_CTL_REG contents

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
	SET_SIZE(n2_inj_l2phys_data)
#endif	/* lint */

/*
 * This routine is used to corrupt the tag or the tag ecc bits of an L2
 * cache line determined by the paddr.  The L2_DIAG_TAG register address
 * is built by this routine, bits [8:6] of the paddr select the cache bank.
 *
 * The L2 cache is 4MB with a linesize of 64B which yeilds 65536 (0x10000)
 * lines split into 4096 (0x1000) lines per each of the 16 ways.
 *
 * L2_DIAG_TAG (offset 0x4.0000.0000) Access (stride 64)
 *
 * +-------+--------+-------+-----+-----+------+-------+-------+
 * | Rsvd0 | Select | Rsvd2 | Way | Set | Bank | Rsvd1 | Rsvd0 |
 * +-------+--------+-------+-----+-----+------+-------+-------+
 *   63:40   39:32    31:22  21:18  17:9  8:6     5:3     2:0
 *
 * Data returned as TAG[27:6], ECC[5:0] (where the TAG bits are ADDR[39:18]
 * which means the normal/supported 8-bank mode is IDX independent).
 */

/*
 * Prototype:
 *
 * int n2_inj_l2cache_tag(uint64_t paddr, uint64_t xorpat, uint_t checkflag,
 *				uint64_t idx_paddr);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - xorpat (clobbered)
 *	%o3 - checkbit flag _or_ debug buffer (debug)
 *	%o4 - idx_paddr, the paddr after IDX index hashing adjusted for
 *	      reduced L2-cache bank modes
 *	%g1 - temp then address of L2_CTL_REG
 *	%g2 - temp then address for data read/write
 *	%g3 - temp then data read from cache
 *	%g4 - saved L2_CTL_REG contents
 *	%g5 - counter for L1$ clear
 *	%g6 - unused
 *	%g7 - L1 way increment value
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_l2cache_tag(uint64_t paddr, uint64_t xorpat, uint_t checkflag,
					uint64_t idx_paddr)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(n2_inj_l2cache_tag)

	/*
	 * Build the register values for L1$ access.
	 * The d$ way bits are bits 12:11, the set bits are 10:4.
	 */
	and	%o1, 0x7f0, %g6			! %g6 = PA[10:4] for L1 tag asi
	mov	1, %g7				! %g7 = L1 way inc. value
	sllx	%g7, 11, %g7			! .
	mov	4, %g5				! %g5 = number of L1 ways

	/*
	 * Build the register values for L2$ tag accesses.
	 */
	mov	N2_L2_DIAG_TAG_MSB, %g4		! set reg select bits
	sllx	%g4, 32, %g4			! .
	set	0x3fffc0, %g2			! mask idx_paddr to get $line
	and	%o4, %g2, %g2			!  way, set, bank (is PA[21:6])
	add	%g2, %g4, %g2			! %g2 = L2_DIAG_TAG

#ifdef	L2_DEBUG_BUFFER
	stx	%o1, [%o3 + 0x0]		! paddr
	stx	%o4, [%o3 + 0x8]		! idx_paddr
	stx	%o2, [%o3 + 0x10]		! xor pat
	stx	%g2, [%o3 + 0x18]		! VA value for L2_DIAG_TAG

	/*
	 * If debug then check the data at this location,
	 * note that because of the way the tag ASI addressing is
	 * affected by reduced L2-cache bank modes the below data
	 * ASI access will only go to the indended location if
	 * all L2-cache banks are enabled.
	 */
	mov	N2_L2_DIAG_DATA_MSB, %g4	! set reg select bits
	sllx	%g4, 32, %g4			! .
	set	0x3ffff8, %g1			! mask paddr to get $line
	and	%o4, %g1, %g1			!  way, set, bank (is PA[21:3])
	or	%g1, %g4, %g1			! %g1 = L2_DIAG_DATA
	ldx	[%g1], %g3			! read the data at this location
						! note that OddEven is ignored
	sllx	%g3, 1, %g3			! shift it so it's readable
	stx	%g3, [%o3 + 0x20]		! data at this tag location

	mov	1, %g4				! flip the OddEven field
	sllx	%g4, 22, %g4			!  to get other half of data
	xor	%g1, %g4, %g1			!  .
	ldx	[%g1], %g3			! read the data at this location
	sllx	%g3, 1, %g3			! shift it so it's readable
	stx	%g3, [%o3 + 0x28]		! data at this tag location
#else
	cmp	%o3, %g0			! if check-bit flag == 0
	bz,a,pt	%xcc, 2f			! shift the xorpat to tag bits
	  sllx	%o2, 6, %o2			! .
#endif

	/*
	 * Put L2 into direct mapped mode, this bank only.
	 */
2:
	mov	N2_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0xa9.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, N2_L2_BANK_MASK, %g3	! get reg offset from paddr
	add	%g1, %g3, %g1			! include offset in reg addr

	ldx	[%g1], %g4			! %g4=prev L2_CTL_REG contents
	or	%g4, N2_L2CR_DMMODE, %g3	! .
	stx	%g3, [%g1]			! .
	membar	#Sync				! (PRM says not needed)

#ifdef	L2_DEBUG_BUFFER
	stx	%g1, [%o3 + 0x30]		! addr of L2_CTL_REG
	stx	%g3, [%o3 + 0x38]		! new contents of L2_CTL_REG
#endif

	ldx	[%o1], %g3			! access to take page fault now

	ba	1f				! branch to aligned code
	  mov	%g0, %o0			! set return value to PASS

	.align	64
1:
	ldx	[%o1], %g3			! get data into L2$ and L1$
	membar	#Sync				! .

#ifdef	L2_DEBUG_BUFFER
	stx	%g3, [%o3 + 0x40]		! contents of mem location
#endif

	ldx	[%g2], %g3			! read tag to corrupt

#ifdef	L2_DEBUG_BUFFER
	stx	%g3, [%o3 + 0x48]		! tag before bit(s) flipped
#endif
	xor	%g3, %o2, %g3			! corrupt tag or tag ecc bits

	stx	%g3, [%g2]			! write the tag back to L2$
	membar	#Sync				!   via L2_DIAG_TAG reg

#ifdef	L2_DEBUG_BUFFER
	stx	%g3, [%o3 + 0x50]		! tag after bit(s) flipped
	ldx	[%g2], %g3			! read back the tag (via reg)
	stx	%g3, [%o3 + 0x58]		!  should not invoke the error
	sllx	%g3, 10, %g3			! shift it by 10 (for 2ch)
	stx	%g3, [%o3 + 0x60]		! .
	sllx	%g3, 1, %g3			! shift it by 11 (for 4ch)
	stx	%g3, [%o3 + 0x68]		! .
	sllx	%g3, 1, %g3			! shift it by 12 (for 8ch)
	stx	%g3, [%o3 + 0x70]		! .
#endif

	/*
	 * Clear the the L1$ entry so access does not hit there.
	 */
3:
	stxa	%g0, [%g6]ASI_DC_TAG		! clear all four ways of L1
	sub	%g5, 1, %g5			! .
	brnz,a	%g5, 3b
	  add	%g6, %g7, %g6			! go to next way

	stx	%g4, [%g1]			! restore this banks L2$ mode
						!   to flush write buffers
	done					! return PASS
	SET_SIZE(n2_inj_l2cache_tag)
#endif	/* lint */

/*
 * This routine is used to corrupt the tag or the tag ecc bits of an L2
 * cache line containing an instruction accessed by it's paddr.  The
 * L2_DIAG_TAG register address is built by this routine, bits [8:6] of the
 * paddr select the cache bank.
 *
 * The method used is similar to the above n2_inj_l2cache_tag() routine,
 * but note that the calling routine must place the L2 into DM mode.
 */

/*
 * Prototype:
 *
 * int n2_inj_l2cache_instr_tag(uint64_t paddr, uint64_t xorpat,
 *					uint_t checkflag, uint64_t idx_paddr);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - xorpat (clobbered)
 *	%o3 - checkbit flag
 *	%o4 - idx_paddr, the paddr after IDX index hashing adjusted for
 *	      reduced L2-cache bank modes
 *	%g1 - temp then address of L2_CTL_REG
 *	%g2 - temp then address for data read/write
 *	%g3 - temp then data read from cache
 *	%g4 - saved L2_CTL_REG contents
 *	%g5 - counter for L1$ clear
 *	%g6 - tag to use for L1 cache flush
 *	%g7 - L1 way increment value
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_l2cache_instr_tag(uint64_t paddr, uint64_t xorpat, uint_t checkflag,
				uint64_t idx_paddr)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(n2_inj_l2cache_instr_tag)

	/*
	 * Build the register values for L1$ access.
	 * The i$ way bits are bits 14:12 (8-way), and index bits are PA[10:5].
	 */
	and	%o1, 0x7e0, %g6			! mask paddr for L1 asi
	sllx	%g6, 1, %g6			! %g6 = L1 asi access addr
	mov	1, %g7				! set way inc. value
	sllx	%g7, 12, %g7			! %g7 = L1 way inc. value
	mov	8, %g5				! %g5 = number of L1 ways

	/*
	 * Build the register values for L2$ tag accesses.
	 */
	mov	N2_L2_DIAG_TAG_MSB, %g4		! set select bits
	sllx	%g4, 32, %g4			! .
	set	0x3fffc0, %g2			! mask paddr to get $line
	and	%o4, %g2, %g2			! combine into complete reg
	add	%g2, %g4, %g2			! %g2 = L2_DIAG_TAG

	/*
	 * Build L2 Control Register for this bank and read value.
	 */
	mov	N2_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0xa9.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, N2_L2_BANK_MASK, %g3	! get reg offset from paddr
	add	%g1, %g3, %g1			! include offset in reg addr
	ldx	[%g1], %g4			! %g4=L2_CTL_REG contents

	ba	1f				! branch to aligned code
	  mov	%g0, %o0			! set return value PASS

	.align	64
1:
	ldx	[%g2], %g3			! read tag to corrupt
						!   via L2_DIAG_TAG reg
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
	stxa	%g0, [%g6]ASI_ICACHE_TAG	! clear all eight ways of L1
	sub	%g5, 1, %g5			! .
	brnz,a	%g5, 3b
	  add	%g6, %g7, %g6			! go to next way

	stx	%g4, [%g1]			! write to banks L2_CTL_REG
						!   to flush write buffers
	done					! return PASS
	SET_SIZE(n2_inj_l2cache_instr_tag)
#endif	/* lint */

/*
 * This routine is used to corrupt the tag or the tag ecc bits of an L2
 * cache line determined by a byte offset.  The L2_DIAG_TAG register address
 * is built by this routine, bits [8:6] of the offset select the cache bank.
 *
 * The method used is similar to the above n2_inj_l2cache_tag() routine.
 *
 * NOTE: this routine is immune to the L2-cache IDX setting since a
 *	 byte offset is used for the injection and not an address.
 */

/*
 * Prototype:
 *
 * int n2_inj_l2phys_tag(uint64_t offset, uint64_t xorpat, uint_t checkflag,
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
n2_inj_l2phys_tag(uint64_t offset, uint64_t xorpat, uint_t checkflag,
					uint64_t *debug)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(n2_inj_l2phys_tag)

	/*
	 * Generate register addresses to use below.
	 */
	mov	N2_L2_DIAG_TAG_MSB, %g4		! set reg select bits
	sllx	%g4, 32, %g4			! .
	set	0x3fffc0, %g2			! mask offset to get $line
	and	%o1, %g2, %g2			! combine into complete reg
	add	%g2, %g4, %g2			! %g2 = L2_DIAG_TAG

	/*
	 * Put L2 into direct mapped mode, this bank only.
	 */
	mov	N2_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0xa9.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, N2_L2_BANK_MASK, %g3	! get reg offset from offset
	add	%g1, %g3, %g1			! include offset in reg addr
	
	ldx	[%g1], %g4			! %g4=prev L2_CTL_REG contents
	or	%g4, N2_L2CR_DMMODE, %g3	! .
	stx	%g3, [%g1]			! .

	ba	1f				! branch to aligned code
	  mov	%g0, %o0			! set return value PASS

	.align	64
1:
	ldx	[%g2], %g3			! read tag to corrupt
						!   via L2_DIAG_TAG reg
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
	SET_SIZE(n2_inj_l2phys_tag)
#endif	/* lint */

/*
 * This routine is used to place "NotData" into the L2 cache line 
 * determined by the paddr when the cache line is for a data
 * access.  The L2_DIAG_DATA register address is built by
 * this routine, bits [8:6] of the paddr select the cache bank.
 *
 * Niagara-II only implements NotData on data as it enters the L2 cache.
 * Though it is detectable on 4-byte chunks of data, it will generally
 * always be on 16-byte chunks of data since that is how data is brought
 * into the L2 cache.  NotData is signalled by inverting all of the ECC
 * bits for the specified data.
 *
 * Since the L2_DIAG_DATA register only allows 32-bits (4-bytes) of data
 * to be written at a time, this routine will write four adjacent (aligned)
 * 4-byte chunks to produce one 16-byte chunk of NotData.
 *
 * Note also that by setting and restoring the L2$ DM mode has the effect
 * of clearing all the L2$ write buffers.  This occurs even if the mode does
 * not actually change since it is triggered by the control reg write.
 *
 * The method used is similar to the above n2_inj_l2cache_data() routine.
 */

/*
 * Prototype:
 *
 * int n2_inj_l2nd(uint64_t paddr, uint64_t idx_paddr, uint64_t *debug)
 *
 * Register usage:
 *
 *	%o1 - paddr (should be 16-byte aligned)
 *	%o2 - idx_paddr, the paddr after IDX index hashing
 *	%o3 - debug buffer (debug)
 *	%o4 - OddEven bit then temp for debug (not an inpar)
 *	%g1 - temp then address of L2_CTL_REG
 *	%g2 - temp then address for data read/write
 *	%g3 - temp then data read from cache
 *	%g4 - saved L2_CTL_REG contents
 *	%g5 - counter for L1$ clear
 *	%g6 - tag to use for L1 cache flush
 *	%g7 - L1 way increment value
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_l2nd(uint64_t paddr, uint64_t idx_paddr, uint64_t *debug)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(n2_inj_l2nd)

	/*
	 * Generate register addresses to use below.
	 */
	andn	%o1, 0xf, %o1			! align paddr to 16-bytes
	mov	1, %o4				! set OddEven field (bit 22)
	sllx	%o4, 22, %o4			! .

#ifdef	L2_DEBUG_BUFFER
	stx	%o1, [%o3 + 0x0]		! aligned paddr
#endif

	/*
	 * Build the register values for L1$ access.
	 * The d$ way bits are bits 12:11, the set bits are 10:4.
	 */
	and	%o1, 0x7f0, %g6			! %g6 = PA[10:4] for L1 tag asi
	mov	1, %g7				! %g7 = L1 way inc. value
	sllx	%g7, 11, %g7			! .
	mov	4, %g5				! %g5 = number of L1 ways

	/*
	 * Build the register values for L2$ accesses.
	 */
	mov	N2_L2_DIAG_DATA_MSB, %g4	! set reg select bits
	sllx	%g4, 32, %g4			! .
	set	0x3ffff8, %g2			! mask paddr to get $line
	and	%o2, %g2, %g2			!  way, set, bank (is PA[21:3])
	or	%g2, %g4, %g2			! %g2 = first L2_DIAG_DATA

#ifdef	L2_DEBUG_BUFFER
	stx	%g2, [%o3 + 0x10]		! first addr for L2_DIAG_DATA
#endif

	/*
	 * Put L2 into direct mapped mode, this bank only.
	 */
	mov	N2_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0xa9.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, N2_L2_BANK_MASK, %g3	! get reg offset from paddr
	or	%g1, %g3, %g1			! include offset in reg addr

	ldx	[%g1], %g4			! %g4=prev L2_CTL_REG contents
	or	%g4, N2_L2CR_DMMODE, %g3	! .
	stx	%g3, [%g1]			! .
	membar	#Sync				! (PRM says not needed)

#ifdef	L2_DEBUG_BUFFER
	stx	%g1, [%o3 + 0x18]		! addr of L2_CTL_REG
	stx	%g3, [%o3 + 0x20]		! new contents of L2_CTL_REG
#endif

	ba	1f				! branch to aligned code
	  nop					! .

	.align	128	
1:
	ldx	[%o1], %g3			! get data into L2$ and L1$
	membar	#Sync				! .

#ifdef	L2_DEBUG_BUFFER
	stx	%g3, [%o3 + 0x28]		! contents of mem location
#endif

	ldx	[%g2], %g3			! read L2$ data to corrupt
	xor	%g3, 0x7f, %g3			! flip all 7 ecc bits
	stx	%g3, [%g2]			! write the data back to L2$

	or	%g2, %o4, %g2			! or in the OddEven bit
	ldx	[%g2], %g3			! read L2$ data to corrupt
	xor	%g3, 0x7f, %g3			! flip all 7 ecc bits
	stx	%g3, [%g2]			! write the data back to L2$
	membar	#Sync				! .

	add	%g2, 0x8, %g2			! inc to next 8-byte word
	ldx	[%g2], %g3			! read L2$ data to corrupt
	xor	%g3, 0x7f, %g3			! flip all 7 ecc bits
	stx	%g3, [%g2]			! write the data back to L2$

	andn	%g2, %o4, %g2			! clear the OddEven bit
	ldx	[%g2], %g3			! read L2$ data to corrupt
	xor	%g3, 0x7f, %g3			! flip all 7 ecc bits
	stx	%g3, [%g2]			! write the data back to L2$
	membar	#Sync				! .

#ifdef	L2_DEBUG_BUFFER
	stx	%g3, [%o3 + 0x30]		! data after bit(s) flipped
	ldx	[%g2], %g3			! read back the data (via reg)
	sllx	%g3, 1, %g3			! shift it so it's readable
	stx	%g3, [%o3 + 0x40]		!  should not invoke the error
#endif

	/*
	 * Clear the the L1$ entry so access does not hit there.
	 *
	 * Since each d$ entry is 16-bytes wide, it will cover all
	 * of the NotData no matter where the access is aligned.
	 */
2:
#ifdef	L2_DEBUG_BUFFER
	ldxa	[%g6]ASI_DC_TAG, %o4		! look at the d$ tags
	sllx	%o4, 9, %o4			! move it up for compare
	sllx	%g5, 3, %o0			! try to get all four
	add	%o3, %o0, %o0			! add offset to debug_buffer
	stx	%o4, [%o0 + 0x50]		! store the tag
#endif
	stxa	%g0, [%g6]ASI_DC_TAG		! clear all four ways of L1
	sub	%g5, 1, %g5			! .
	brnz,a	%g5, 2b
	  add	%g6, %g7, %g6			! go to next way

	stx	%g4, [%g1]			! restore this banks L2$ mode
						!   which will flush L2$ buffers
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_inj_l2nd)
#endif	/* lint */

/*
 * This routine is used to place "NotData" into the L2 cache line 
 * determined by the paddr when the cache line is for a data
 * access.  The L2_DIAG_DATA register address is built by
 * this routine, bits [8:6] of the paddr select the cache bank.
 *
 * The method used is similar to the above n2_inj_l2nd() routine,
 * but it has been simplified to expect that the data has already been
 * installed in the cache (in DM mode).  This means that the following
 * steps have been removed:
 *	- the L2 cache is not placed (and later restored) into DM mode
 *	- the data is not accessed (which would bring it into the caches)
 */

/*
 * Prototype:
 *
 * int n2_inj_l2nd_quick(uint64_t paddr, uint64_t idx_paddr, uint64_t *debug)
 *
 * Register usage:
 *
 *	%o1 - paddr (should be 16-byte aligned)
 *	%o2 - idx_paddr, the paddr after IDX index hashing
 *	%o3 - debug buffer (debug)
 *	%o4 - OddEven bit then temp for debug (not an inpar)
 *	%g1 - temp then address of L2_CTL_REG
 *	%g2 - temp then address for data read/write
 *	%g3 - temp then data read from cache
 *	%g4 - saved L2_CTL_REG contents
 *	%g5 - counter for L1$ clear
 *	%g6 - tag to use for L1 cache flush
 *	%g7 - L1 way increment value
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_l2nd_quick(uint64_t paddr, uint64_t idx_paddr, uint64_t *debug)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(n2_inj_l2nd_quick)

	/*
	 * Generate register addresses to use below.
	 */
	andn	%o1, 0xf, %o1			! align paddr to 16-bytes
	mov	1, %o4				! set OddEven field (bit 22)
	sllx	%o4, 22, %o4			! .

	/*
	 * Build the register values for L1$ access.
	 * The d$ way bits are bits 12:11, the set bits are 10:4.
	 */
	and	%o1, 0x7f0, %g6			! %g6 = PA[10:4] for L1 tag asi
	mov	1, %g7				! %g7 = L1 way inc. value
	sllx	%g7, 11, %g7			! .
	mov	4, %g5				! %g5 = number of L1 ways

	/*
	 * Build the register values for L2$ accesses.
	 */
	mov	N2_L2_DIAG_DATA_MSB, %g4	! set reg select bits
	sllx	%g4, 32, %g4			! .
	set	0x3ffff8, %g2			! mask paddr to get $line
	and	%o2, %g2, %g2			!  way, set, bank (is PA[21:3])
	or	%g2, %g4, %g2			! %g2 = first L2_DIAG_DATA

	/*
	 * Put L2 into direct mapped mode, this bank only.
	 */
	mov	N2_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0xa9.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, N2_L2_BANK_MASK, %g3	! get reg offset from paddr
	or	%g1, %g3, %g1			! include offset in reg addr

	ldx	[%g1], %g4			! %g4=prev L2_CTL_REG contents
	or	%g4, N2_L2CR_DMMODE, %g3	! .
	stx	%g3, [%g1]			! .
	membar	#Sync				! (PRM says not needed)

	ba	1f				! branch to aligned code
	  nop					! .

	.align	128	
1:
	ldx	[%o1], %g3			! get data into L2$ and L1$
	membar	#Sync				! .

	ldx	[%g2], %g3			! read L2$ data to corrupt
	xor	%g3, 0x7f, %g3			! flip all 7 ecc bits
	stx	%g3, [%g2]			! write the data back to L2$

	or	%g2, %o4, %g2			! or in the OddEven bit
	ldx	[%g2], %g3			! read L2$ data to corrupt
	xor	%g3, 0x7f, %g3			! flip all 7 ecc bits
	stx	%g3, [%g2]			! write the data back to L2$
	membar	#Sync				! .

	add	%g2, 0x8, %g2			! inc to next 8-byte word
	ldx	[%g2], %g3			! read L2$ data to corrupt
	xor	%g3, 0x7f, %g3			! flip all 7 ecc bits
	stx	%g3, [%g2]			! write the data back to L2$

	andn	%g2, %o4, %g2			! clear the OddEven bit
	ldx	[%g2], %g3			! read L2$ data to corrupt
	xor	%g3, 0x7f, %g3			! flip all 7 ecc bits
	stx	%g3, [%g2]			! write the data back to L2$
	membar	#Sync				! .

	/*
	 * Clear the the L1$ entry so access does not hit there.
	 *
	 * Since each d$ entry is 16-bytes wide, it will cover all
	 * of the NotData no matter where the access is aligned.
	 */
2:
	stxa	%g0, [%g6]ASI_DC_TAG		! clear all four ways of L1
	sub	%g5, 1, %g5			! .
	brnz,a	%g5, 2b
	  add	%g6, %g7, %g6			! go to next way

	stx	%g4, [%g1]			! restore this banks L2$ mode
						!   which will flush L2$ buffers
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_inj_l2nd_quick)
#endif	/* lint */

/*
 * This routine is used to place "NotData" into the L2 cache line 
 * determined by the paddr when the cache line is for an instruction
 * access.  The L2_DIAG_DATA register address is built by
 * this routine, bits [8:6] of the paddr select the cache bank.
 *
 * The method used is similar to the above n2_inj_l2nd() routine,
 * but note that the calling routine must place the L2 into DM mode.
 */

/*
 * Prototype:
 *
 * int n2_inj_l2nd_instr(uint64_t paddr, uint64_t idx_paddr, uint64_t *debug)
 *
 * Register usage:
 *
 *	%o1 - paddr (should be 16-byte aligned)
 *	%o2 - idx_paddr, the paddr after IDX index hashing
 *	%o3 - debug buffer (debug)
 *	%o4 - OddEven bit (not an inpar)
 *	%g1 - temp then address of L2_CTL_REG
 *	%g2 - temp then address for data read/write
 *	%g3 - temp then data read from cache
 *	%g4 - saved L2_CTL_REG contents
 *	%g5 - counter for L1$ clear
 *	%g6 - tag to use for L1 cache flush
 *	%g7 - L1 way increment value
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_l2nd_instr(uint64_t paddr, uint64_t idx_paddr, uint64_t *debug)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(n2_inj_l2nd_instr)

	/*
	 * Generate register addresses to use below.
	 */
	andn	%o1, 0xf, %o1			! align paddr to 16-bytes
	mov	1, %o4				! set OddEven field (bit 22)
	sllx	%o4, 22, %o4			! .

	/*
	 * Build the register values for L1$ access.
	 * The i$ way bits are bits 14:12 (8-way), and index bits are PA[10:5].
	 */
	and	%o1, 0x7e0, %g6			! mask paddr for L1 asi
	sllx	%g6, 1, %g6			! %g6 = L1 asi access addr
	mov	1, %g7				! set way inc. value
	sllx	%g7, 12, %g7			! %g7 = L1 way inc. value
	mov	8, %g5				! %g5 = number of L1 ways

	/*
	 * Build the register values for L2$ accesses.
	 */
	mov	N2_L2_DIAG_DATA_MSB, %g4	! set reg select bits
	sllx	%g4, 32, %g4			! .
	set	0x3ffff8, %g2			! mask paddr to get $line
	and	%o2, %g2, %g2			!  way, set, bank (is PA[21:3])
	or	%g2, %g4, %g2			! %g2 = first L2_DIAG_DATA

	/*
	 * Build L2 Control Register for this bank and read value.
	 */
	mov	N2_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0xa9.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, N2_L2_BANK_MASK, %g3	! get reg offset from paddr
	add	%g1, %g3, %g1			! include offset in reg addr
	ldx	[%g1], %g4			! %g4=L2_CTL_REG contents

	ba	1f				! branch to aligned code
	  nop					! .

	.align	128	
1:
	ldx	[%g2], %g3			! read L2$ data to corrupt
	xor	%g3, 0x7f, %g3			! flip all 7 ecc bits
	stx	%g3, [%g2]			! write the data back to L2$

	or	%g2, %o4, %g2			! or in the OddEven bit
	ldx	[%g2], %g3			! read L2$ data to corrupt
	xor	%g3, 0x7f, %g3			! flip all 7 ecc bits
	stx	%g3, [%g2]			! write the data back to L2$
	membar	#Sync				! .

	add	%g2, 0x8, %g2			! inc to next 8-byte word
	ldx	[%g2], %g3			! read L2$ data to corrupt
	xor	%g3, 0x7f, %g3			! flip all 7 ecc bits
	stx	%g3, [%g2]			! write the data back to L2$

	andn	%g2, %o4, %g2			! clear the OddEven bit
	ldx	[%g2], %g3			! read L2$ data to corrupt
	xor	%g3, 0x7f, %g3			! flip all 7 ecc bits
	stx	%g3, [%g2]			! write the data back to L2$
	membar	#Sync				! .

	/*
	 * Clear the the L1$ entry so access does not hit there.
	 *
	 * Since each i$ entry is 32-bytes wide, it will cover all
	 * of the NotData no matter where the access is aligned.
	 */
2:
	stxa	%g0, [%g6]ASI_ICACHE_TAG	! clear all eight ways of L1
	sub	%g5, 1, %g5			! .
	brnz,a	%g5, 2b
	  add	%g6, %g7, %g6			! go to next way

	stx	%g4, [%g1]			! write to banks L2_CTL_REG
						!   to flush write buffers
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_inj_l2nd_instr)
#endif	/* lint */

/*
 * This routine is used to place "NotData" into the L2 cache line 
 * determined by a byte offset. The L2_DIAG_DATA register address is
 * built by this routine, bits [8:6] of the paddr select the cache bank.
 *
 * The method used is similar to the above n2_inj_l2nd() routine,
 * but note that the data is not removed from the L1-cache.
 *
 * NOTE: this routine is immune to the L2-cache IDX setting since a
 *	 byte offset is used for the injection and not an address.
 */

/*
 * Prototype:
 *
 * int n2_inj_l2nd_phys(uint64_t offset, uint64_t scratch, uint64_t *debug)
 *
 * Register usage:
 *
 *	%o1 - byte offset (should be 16-byte aligned)
 *	%o2 - OddEven bit (not an inpar)
 *	%o3 - debug buffer (debug)
 *	%o4 - unused
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
n2_inj_l2nd_phys(uint64_t offset, uint64_t scratch, uint64_t *debug)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(n2_inj_l2nd_phys)

	/*
	 * Generate register addresses to use below.
	 */
	andn	%o1, 0xf, %o1			! align paddr to 16-bytes
	mov	1, %o2				! set OddEven field (bit 22)
	sllx	%o2, 22, %o2			! .

	/*
	 * Build the register values for L2$ accesses.
	 */
	mov	N2_L2_DIAG_DATA_MSB, %g4	! set reg select bits
	sllx	%g4, 32, %g4			! .
	set	0x3ffff8, %g2			! mask paddr to get $line
	and	%o1, %g2, %g2			!  way, set, bank (is PA[21:3])
	or	%g2, %g4, %g2			! %g2 = first L2_DIAG_DATA

	/*
	 * Build L2 Control Register for this bank and read value.
	 */
	mov	N2_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0xa9.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, N2_L2_BANK_MASK, %g3	! get reg offset from paddr
	add	%g1, %g3, %g1			! include offset in reg addr
	ldx	[%g1], %g4			! %g4=L2_CTL_REG contents

	ba	1f				! branch to aligned code
	  nop					! .

	.align	128	
1:
	ldx	[%g2], %g3			! read L2$ data to corrupt
	xor	%g3, 0x7f, %g3			! flip all 7 ecc bits
	stx	%g3, [%g2]			! write the data back to L2$

	or	%g2, %o2, %g2			! or in the OddEven bit
	ldx	[%g2], %g3			! read L2$ data to corrupt
	xor	%g3, 0x7f, %g3			! flip all 7 ecc bits
	stx	%g3, [%g2]			! write the data back to L2$
	membar	#Sync				! .

	add	%g2, 0x8, %g2			! inc to next 8-byte word
	ldx	[%g2], %g3			! read L2$ data to corrupt
	xor	%g3, 0x7f, %g3			! flip all 7 ecc bits
	stx	%g3, [%g2]			! write the data back to L2$

	andn	%g2, %o2, %g2			! clear the OddEven bit
	ldx	[%g2], %g3			! read L2$ data to corrupt
	xor	%g3, 0x7f, %g3			! flip all 7 ecc bits
	stx	%g3, [%g2]			! write the data back to L2$
	membar	#Sync				! .

	stx	%g4, [%g1]			! write to banks L2_CTL_REG
						!   to flush write buffers
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_inj_l2nd_phys)
#endif	/* lint */

/*
 * This routine is used to corrupt the V(U)AD or the V(U)AD parity bits of an
 * L2$ line determined by the paddr.  Parity is checked for all 16 VAD bits
 * on every L2 access (the Used bit of VUAD is not covered by parity since it
 * only affects performance, not correctness).  This means that the L2 does
 * not need to be put into DM mode and that errors can be detected even
 * without the explicit access.
 *
 * This routine expects that the caller will determine the bits to be
 * corrupted so the caller must be aware of the format of these regs.
 *
 * Similar to other L2$ tests the method of setting and restoring the L2$ DM
 * mode to clear the L2$ write buffers is used in the VAD type tests.
 *
 * L2_DIAG_VD (offset 0x6.0040.0000) Access (stride 64)
 * L2_DIAG_UA (offset 0x6.0000.0000) Access (stride 64)
 *
 * +-------+--------+-------+-------+-------+-----+------+-----------+
 * | Rsvd0 | Select | Rsvd1 | VDSel | Rsvd2 | Set | Bank | Rsvd3 + 4 |
 * +-------+--------+-------+-------+-------+-----+------+-----------+
 *   63:40   39:32    31:23    22     21:18  17:9   8:6    5:3 + 2:0
 *
 * Data returned as:
 *
 *     +-------+--------+-------+-------+
 * VD: | Rsvd0 | VD ECC | Valid | Dirty |
 *     +-------+--------+-------+-------+
 *     +-------+--------+-------+-------+
 * UA: | Rsvd0 | UA ECC | Used  | Alloc |
 *     +-------+--------+-------+-------+
 *       63:39   38:32    31:16   15:0
 */

/*
 * Prototype:
 *
 * int n2_inj_l2vad(uint64_t paddr, uint_t xorpat, uint_t vdflag,
 *			uint64_t idx_paddr);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - xorpat
 *	%o3 - VD flag (must be 0x40.0000 or 0, avoids a shift instr)
 *	%o4 - idx_paddr, the paddr after IDX index hashing
 *	%g1 - temp then address of L2_CTL_REG
 *	%g2 - temp then address for data read/write
 *	%g3 - temp then data read from cache
 *	%g4 - temp then saved L2_CTL_REG contents
 *	%g5 - counter for L1$ clear
 *	%g6 - tag to use for L1 cache flush
 *	%g7 - L1 way increment value
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_l2vad(uint64_t paddr, uint_t xorpat, uint_t vdflag, uint64_t idx_paddr)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(n2_inj_l2vad)

	/*
	 * Build the register values for L1$ access.
	 * The d$ way bits are bits 12:11, the set bits are 10:4.
	 */
	and	%o1, 0x7f0, %g6			! %g6 = PA[10:4] for L1 tag asi
	mov	1, %g7				! %g7 = put way inc. value
	sllx	%g7, 11, %g7			! .
	mov	4, %g5				! %g5 = number of L1 ways

	/*
	 * Build the register values for L2$ accesses.
	 */
	mov	N2_L2_DIAG_VUAD_MSB, %g4	! set select bits
	sllx	%g4, 32, %g4			! .
	set	0x3ffc0, %g2			! mask paddr to get $line (17:6)
	and	%o4, %g2, %g2			! combine into complete reg
	cmp	%o3, %g0			! if VD flag != 0
	bnz,a,pt %icc, 1f			!   set the VDSel bit
	  or	%g2, %o3, %g2			! .
1:
	add	%g2, %g4, %g2			! %g2 = L2_DIAG_VD/UA

	/*
	 * Get the contents of this banks control register.
	 */
	mov	N2_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0xa9.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, N2_L2_BANK_MASK, %g3	! get reg offset from paddr
	add	%g1, %g3, %g1			! include offset in reg addr
	ldx	[%g1], %g4			! %g4=L2_CTL_REG contents

	ba	2f				! branch to aligned code
	  nop					! .

	.align	64
2:
	ldx	[%o1], %g3			! get data into L2$ and L1$
	membar	#Sync				!.

	ldx	[%g2], %g3			! read value to corrupt
	xor	%g3, %o2, %g3			! corrupt VUAD or parity bits
	stx	%g3, [%g2]			! write the value back to L2$
	membar	#Sync				!.

	/*
	 * Clear the the L1$ entry so access does not hit there.
	 */
3:
	stxa	%g0, [%g6]ASI_DC_TAG		! clear all four ways of L1
	sub	%g5, 1, %g5			! .
	brnz	%g5, 3b
	  add	%g6, %g7, %g6			! go to next way

	stx	%g4, [%g1]			! restore this banks L2$ mode
						!   which will flush L2$ bufs
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_inj_l2vad)
#endif	/* lint */

/*
 * This routine is used to corrupt the V(U)AD or the V(U)AD parity bits of
 * an L2 cache line that maps to an instruction determined by the paddr.
 * This routine is similar to the above n2_inj_l2vad() routine.
 *
 * This routine expects that the caller will determine the bits to be
 * corrupted so the caller must be aware of the format of these regs.
 */

/*
 * Prototype:
 *
 * int n2_inj_l2vad_instr(uint64_t paddr, uint_t xorpat, uint_t vdflag,
 *			uint64_t idx_paddr);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - xorpat
 *	%o3 - VD flag (must be 0x40.0000 or 0, avoids a shift instr)
 *	%o4 - idx_paddr, the paddr after IDX index hashing
 *	%g1 - temp then address of L2_CTL_REG
 *	%g2 - temp then address for data read/write
 *	%g3 - temp then data read from cache
 *	%g4 - temp then saved L2_CTL_REG contents
 *	%g5 - counter for L1$ clear
 *	%g6 - tag to use for L1 cache flush
 *	%g7 - L1 way increment value
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_l2vad_instr(uint64_t paddr, uint_t xorpat, uint_t vdflag,
			uint64_t idx_paddr)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(n2_inj_l2vad_instr)

	/*
	 * Build the register values for L1$ access.
	 * The i$ way bits are bits 14:12 (8-way), and index bits are PA[10:5].
	 */
	and	%o1, 0x7e0, %g6			! mask paddr for L1 asi access
	sllx	%g6, 1, %g6			! %g6 = L1 asi access addr
	mov	1, %g7				! set the way inc. value
	sllx	%g7, 12, %g7			! %g7 = L1 way inc. value
	mov	8, %g5				! %g5 = number of L1 ways

	/*
	 * Build the register values for L2$ accesses.
	 */
	mov	N2_L2_DIAG_VUAD_MSB, %g4	! set select bits
	sllx	%g4, 32, %g4			! .
	set	0x3ffc0, %g2			! mask paddr to get $line
	and	%o4, %g2, %g2			! combine into complete reg
	cmp	%o3, %g0			! if VD flag != 0
	bnz,a,pt %icc, 1f			!   set the VDSel bit
	  or	%g2, %o3, %g2			! .
1:
	add	%g2, %g4, %g2			! %g2 = L2_DIAG_VD/UA

	/*
	 * Get the contents of this banks control register.
	 */
	mov	N2_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0xa9.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, N2_L2_BANK_MASK, %g3	! get reg offset from paddr
	add	%g1, %g3, %g1			! include offset in reg addr
	ldx	[%g1], %g4			! %g4=L2_CTL_REG contents

	ba	2f				! branch to aligned code
	  nop					! .

	.align	64
2:
	ldx	[%o1], %g3			! get data into L2$ and L1$
	membar	#Sync				!.

	ldx	[%g2], %g3			! read value to corrupt
	xor	%g3, %o2, %g3			! corrupt VUAD or parity bits
	stx	%g3, [%g2]			! write the value back to L2$
	membar	#Sync				!.

	/*
	 * Clear the the L1$ entry so access does not hit there.
	 */
3:
	stxa	%g0, [%g6]ASI_ICACHE_TAG	! clear all eight ways of L1
	sub	%g5, 1, %g5			! .
	brnz	%g5, 3b
	  add	%g6, %g7, %g6			! go to next way

	stx	%g4, [%g1]			! write to control reg
						!   which will flush L2$ bufs
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_inj_l2vad_instr)
#endif	/* lint */

/*
 * This routine is used to corrupt the V(U)AD or the V(U)AD parity bits of an
 * L2 cache line determined by a byte offset.  This routine is similar to the
 * above n2_inj_l2vad() except it takes an offset instead of a paddr and
 * note that the data is not removed from the L1-cache.
 *
 * NOTE: Could make a version that does the L1 scrub which would likely
 *	 produce more errors but then there is no real difference from the
 *	 regular test using "-n" and a "-a" address.  Note that would need
 *	 an instr and a data version of this routine if that was the case.
 *
 * NOTE: this routine is immune to the L2-cache IDX setting since a
 *	 byte offset is used for the injection and not an address.
 */

/*
 * Prototype:
 *
 * int n2_inj_l2vad_phys(uint64_t offset, uint_t xorpat, uint_t vdflag,
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
n2_inj_l2vad_phys(uint64_t offset, uint_t xorpat, uint_t vdflag,
				uint64_t *debug)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(n2_inj_l2vad_phys)

	/*
	 * Generate register addresses to use below.
	 */
	mov	N2_L2_DIAG_VUAD_MSB, %g4	! set select bits
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
	mov	N2_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0xa9.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, N2_L2_BANK_MASK, %g3	! get reg offset from offset
	add	%g1, %g3, %g1			! include offset in reg addr
	ldx	[%g1], %g4			! %g4=L2_CTL_REG contents

	ba	2f				! branch to aligned code
	  nop					! .

	.align	64
2:
	ldx	[%g2], %g3			! read value to corrupt
	xor	%g3, %o2, %g3			! corrupt VUAD or parity bits
	stx	%g3, [%g2]			! write the value back to L2$
	membar	#Sync				!.

	stx	%g4, [%g1]			! write to control reg

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_inj_l2vad_phys)
#endif	/* lint */

/*
 * This routine is used to corrupt the L2$ directory parity at a location
 * determined by the paddr.  During directory scrub, parity is checked for
 * each directory entry.  Directory parity errors are planted via the per
 * bank L2_ERROR_INJECT_REG (0xd.0000.0000).  Bits [8:6] of the paddr select
 * the cache bank.
 *
 * NOTE: this routine is immune to the L2-cache IDX setting since no
 *	 indexes are used only actual addresses.  However if the L2$
 *	 is in a reduced bank mode, the bank selection will not work.
 *
 * NOTE: this routine is two i$ lines in size, one would be preferable
 *	 but even this minimizes the chances of side effects.
 */

/*
 * Prototype:
 *
 * int n2_inj_l2dir(uint64_t paddr, uint_t sdshot_enb, uint_t data_flag,
 *			uint64_t l2_bank_mask);
 *
 * Register usage:
 *
 *	%o1 - paddr (64-bit aligned)
 *	%o2 - sdshot_enb (the combination of enable and sdshot to use)
 *	%o3 - flag for DATA vs. instruction _or_ debug buffer (debug)
 *	%o4 - mask for L2 bank
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
n2_inj_l2dir(uint64_t paddr, uint_t sdshot_enb, uint_t data_flag,
			uint64_t l2_bank_mask)
{
	return 0;
}
#else
	.align	64
	ENTRY_NP(n2_inj_l2dir)

	/*
	 * Generate register addresses to use below.
	 */
	mov	N2_L2_ERR_INJ_REG_MSB, %g4	! set select bits
	sllx	%g4, 32, %g4			! .
	and	%o1, %o4, %g2			! mask paddr to get L2$ bank
	add	%g2, %g4, %g2			! %g2 = L2_ERROR_INJECT_REG

	stx	%o2, [%g2]			! enable dir error injection
	ldx	[%g2], %g0			! ensure write completes

#ifdef	L2_DEBUG_BUFFER
	ldx	[%o1], %g3			! do first bad write to dir

	stx	%o1, [%o3 + 0x0]		! aligned paddr
	stx	%o2, [%o3 + 0x8]		! xor pat + options
	stx	%o4, [%o3 + 0x10]		! bank mask

#else
	cmp	%o3, %g0			! if DATA flag != 0
	bnz,a,pt %icc, 1f			!   write first bad par to dir
	  ldx	[%o1], %g3			!   (else wait for C-routine)
#endif

1:
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_inj_l2dir)
#endif	/* lint */

/*
 * This routine is used to corrupt the L2$ directory parity at a location
 * determined by a byte offset.  During directory scrub, parity is checked for
 * the directory entry.  Directory parity errors can be planted via the per
 * bank L2_ERROR_INJECT_REG (0xd.0000.0000).  Bits [8:6] of the offset select
 * the cache bank register, the rest of the bits are ignored.
 *
 * Due to the way the injection register was implemented we actually can't
 * insert an error into a specific location via an offset.  This is a HW
 * limitation.  The next update to the directory will be in error.  This
 * limitation is not unreasonable since all directory errors are FATAL and
 * produce no verifiable error output anyway.
 *
 * NOTE: this routine is exactly one i$ line in size, which minimizes the
 *	 chances of side effects.
 */

/*
 * Prototype:
 *
 * int n2_inj_l2dir_phys(uint64_t offset, uint_t sdshot_enb, uint64_t *debug);
 *
 * Register usage:
 *
 *	%o1 - byte offset (only bits [8:6] used)
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
n2_inj_l2dir_phys(uint64_t offset, uint_t sdshot_enb, uint64_t *debug)
{
	return 0;
}
#else
	.align	32
	ENTRY_NP(n2_inj_l2dir_phys)

	/*
	 * Generate register addresses to use below.
	 */
	mov	N2_L2_ERR_INJ_REG_MSB, %g4	! set select bits
	sllx	%g4, 32, %g4			! .
	and	%o1, N2_L2_BANK_MASK, %g2	! mask paddr to get L2$ bank
	add	%g2, %g4, %g2			! %g2 = L2_ERROR_INJECT_REG

	stx	%o2, [%g2]			! enable dir error injection
	ldx	[%g2], %g0			! ensure write completes

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_inj_l2dir_phys)
#endif	/* lint */

/*------------------ end of the L2$ / start of D$ functions ----------------*/

/*
 * This routine is used to corrupt the data or the data parity of the L1
 * data cache line determined by the paddr.  The ASI_LSU_DIAG_REG (0x42) is
 * used to put the data cache into paddr replacement policy mode.
 *
 * The critical section of each d$ routine must fit on one L2$ line to
 * avoid unwanted side effects.
 *
 * NOTE: Niagara-II calculates the d$ parity for ASI stores as well as normal
 *	 fault-ins.  This means d$ errors can only be injected by setting the
 *	 parity bit (bit 32) when doing ASI store accesses.
 *
 * ASI_DC_DATA (0x46)
 *
 *         +-------+----------+-----+-----+------+-------+
 * Stores: | Rsvd0 | PErrMask | Way | Set | Word | Rsvd2 |
 *         +-------+----------+-----+-----+------+-------+
 *           63:21    20:13    12:11 10:4    3      2:0
 *
 *         +-------+---------+-----+-----+------+-------+
 * Loads:  | Rsvd0 | DataPar | Way | Set | Word | Rsvd2 |
 *         +-------+---------+-----+-----+------+-------+
 *           63:14     13     12:11 10:4    3      2:0
 *
 * Data returned as DATA[63:0] (for data) or PAR[7:0] (for parity)
 */

/*
 * Prototype:
 *
 * int n2_inj_dcache_data(uint64_t paddr, uint64_t xorpat, uint_t checkflag,
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
 *	%g6 - asi access for load
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_dcache_data(uint64_t paddr, uint64_t xorpat, uint_t checkflag,
				uint64_t *debug)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(n2_inj_dcache_data)

	/*
	 * Generate register addresses to use below.
	 */
	mov	0x10, %g2			! set d$ direct mapped mode
	ldxa	[%g2]N2_ASI_LSU_DIAG_REG, %g3	!  (preserve i$ mode)
	or	%g3, 0x2, %g3			! .
	stxa	%g3, [%g2]N2_ASI_LSU_DIAG_REG	! .
	membar	#Sync				! .

	mov	0xffc, %g3			! bitmask for asi store access 
	sllx	%g3, 1, %g3			! .
	and	%o1, %g3, %g4			! %g4 = PA[12:3] for store

	mov	1, %g1				! set data bit for load access
	sllx	%g1, 13, %g1			! .
	or	%g4, %g1, %g6			! %g6 = addr for data load
						!   same PA bitmask as store
#ifdef	L1_DEBUG_BUFFER
	stx	%o1, [%o4 + 0x0]		! paddr
	stx	%g4, [%o4 + 0x8]		! asi store addr
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
	membar #Sync				! required to complete store

#ifdef	L1_DEBUG_BUFFER
	stx	%g3, [%o4 + 0x18]		! cache data (can be corrupted)
	stx	%g4, [%o4 + 0x20]		! asi store access addr (")
#endif

	ldxa	[%g2]N2_ASI_LSU_DIAG_REG, %g3	! restore d$ replacement mode
	andn	%g3, 0x2, %g3			! .
	stxa	%g3, [%g2]N2_ASI_LSU_DIAG_REG	! .
	membar	#Sync				! .

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_inj_dcache_data)
#endif	/* lint */

/*
 * This routine is used to corrupt the data or the data parity of the L1
 * data cache line determined by the paddr.  The ASI_LSU_DIAG_REG (0x42) is
 * used to put the data cache into paddr replacement policy mode.
 *
 * This routine is similar to the above n2_inj_dcache_data() test but here the
 * address to corrupt is accessed, corrupted, and accessed again (to trigger
 * the error) all while in hyperpriv mode.
 */

/*
 * Prototype:
 *
 * int n2_inj_dcache_hvdata(uint64_t paddr, uint64_t xorpat, uint_t checkflag,
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
 *	%g6 - asi access for load
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_dcache_hvdata(uint64_t paddr, uint64_t xorpat, uint_t checkflag,
				uint_t access_type)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(n2_inj_dcache_hvdata)

	/*
	 * Generate register addresses to use below.
	 */
	mov	0x10, %g2			! set d$ direct mapped mode
	ldxa	[%g2]N2_ASI_LSU_DIAG_REG, %g3	!  (preserve i$ mode)
	or	%g3, 0x2, %g3			! .
	stxa	%g3, [%g2]N2_ASI_LSU_DIAG_REG	! .
	membar	#Sync				! .

	mov	0xffc, %g3			! bitmask for asi store access 
	sllx	%g3, 1, %g3			! .
	and	%o1, %g3, %g4			! %g4 = PA[12:3] for store

	mov	1, %g1				! set data bit for load access
	sllx	%g1, 13, %g1			! .
	or	%g4, %g1, %g6			! %g6 = addr for data load
						!   same PA bitmask as store

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
	membar #Sync				! required to complete store

	ldxa	[%g2]N2_ASI_LSU_DIAG_REG, %g3	! restore d$ replacement mode
	andn	%g3, 0x2, %g3			! .
	stxa	%g3, [%g2]N2_ASI_LSU_DIAG_REG	! .
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
	SET_SIZE(n2_inj_dcache_hvdata)
#endif	/* lint */

/*
 * This routine is used to corrupt the data or the data parity of the L1
 * data cache line determined by a cache byte offset.
 *
 * The method used is similar to the above n2_inj_dcache_data() routine
 * except it takes an offset instead of a paddr.
 */

/*
 * Prototype:
 *
 * int n2_inj_dphys_data(uint64_t offset, uint64_t xorpat, uint_t checkflag,
 *				uint64_t *debug);
 *
 * Register usage:
 *
 *	%o1 - byte offset
 *	%o2 - xorpat (must be only [20:13] for check-bit test)
 *	%o3 - check-bit flag
 *	%o4 - pointer to debug buffer (debug)
 *	%g1 - temp
 *	%g2 - asi access for load
 *	%g3 - temp then data read from cache
 *	%g4 - asi access for store
 *	%g5 - unused
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_dphys_data(uint64_t offset, uint64_t xorpat, uint_t checkflag,
				uint64_t *debug)
{
	return 0;
}
#else
	.align	64
	ENTRY_NP(n2_inj_dphys_data)

	/*
	 * Generate register addresses to use below.
	 */
	mov	0xffc, %g3			! bitmask for asi store access 
	sllx	%g3, 1, %g3			! .
	and	%o1, %g3, %g4			! %g4 = PA[12:3] for store

	mov	1, %g1				! set data bit for load access
	sllx	%g1, 13, %g1			! .
	or	%g4, %g1, %g2			! %g6 = addr for data load
						!   same PA bitmask as store

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
	SET_SIZE(n2_inj_dphys_data)
#endif	/* lint */

/*
 * This routine is used to corrupt the tag, the tag parity, or the tag
 * valid bits of the L1 data cache line determined by the paddr.
 *
 * The ASI_LSU_DIAG_REG is used to put the data cache into paddr
 * replacement policy mode.
 *
 * If the checkbit flag is set the xorpat must be 0x2000 (only bit 13 set)
 * for tag parity, or 0x4000 (only bit 14 set) for valid-bit mismatch
 * since the single parity or valid bit is inverted using the built-in asi
 * mechanism.
 *
 * The critical section of each d$ routine must fit on one L2$ line to
 * avoid unwanted side effects.
 *
 * ASI_DC_TAG (0x47)
 *
 * +-------+---------+--------+-----+-----+-------+
 * | Rsvd0 | VBErren | PErren | Way | Set | Rsvd1 |
 * +-------+---------+--------+-----+-----+-------+
 *   63:15     14        13    12:11 10:4    3:0
 *
 * Data stored as TAG[30:2], VM[1] (valid slave ignored, tag is PA[39:11])
 * Data returned as PARITY[31], TAG[30:2], VM[1], VS[0] (tag is PA[39:11])
 *
 * NOTE: this is almost the same on KT, the address is the same but the
 *	 return is different.
 *
 * Data stored as TAG[30:2], VM[1] (valid slave ignored, tag is PA[39:11])
 * Data returned as PARITY[31], TAG[30:2], VM[1], VS[0] (tag is PA[39:11])
 *
 * NOTE: even though the tag is larger on KT (because of KT's increased PA
 *	 space) this routine does NOT have to change because the parity bit
 *	 is not accessed directly.  Valid tag xor bits are [34:2] on KT
 */

/*
 * Prototype:
 *
 * int n2_inj_dcache_tag(uint64_t paddr, uint64_t xorpat, uint_t checkflag,
 *				uint64_t *debug);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - xorpat (valid tag bits[30:2], parity[13], valid[14])
 *	%o3 - check-bit flag
 *	%o4 - pointer to debug buffer (debug)
 *	%g1 - unused
 *	%g2 - addr for LSU_DIAG_REG
 *	%g3 - temp then tag read from cache
 *	%g4 - asi access addr
 *	%g5 - unused
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_dcache_tag(uint64_t paddr, uint64_t xorpat, uint_t checkflag,
				uint64_t *debug)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(n2_inj_dcache_tag)

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
	ldxa	[%g2]N2_ASI_LSU_DIAG_REG, %g3	!  (preserve i$ mode)
	or	%g3, 0x2, %g3			! .
	stxa	%g3, [%g2]N2_ASI_LSU_DIAG_REG	! .
	membar	#Sync				! .

	ba	1f				! branch to aligned code
	  nop

	.align	64
1:
	ldx	[%o1], %g0			! read addr to get it into d$
	membar	#Sync				! ensure data is in d$

	ldxa	[%g4]ASI_DC_TAG, %g3		! read the tag to corrupt
	cmp	%o3, %g0			! if check-bit flag != 0
	bnz,a,pt %icc, 2f			!   set valid|parity flip bit
	  or	%g4, %o2, %g4			!   .
	xor	%g3, %o2, %g3			! else corrupt the tag
2:
	stxa	%g3, [%g4]ASI_DC_TAG		! write it back to d$
	membar	#Sync				! required

#ifdef	L1_DEBUG_BUFFER
	stx	%g3, [%o4 + 0x18]		! cache tag (can be corrupted)
	stx	%g4, [%o4 + 0x20]		! asi store access addr (")
#endif

	ldxa	[%g2]N2_ASI_LSU_DIAG_REG, %g3	! restore d$ replacement mode
	andn	%g3, 0x2, %g3			! .
	stxa	%g3, [%g2]N2_ASI_LSU_DIAG_REG	! .
	membar	#Sync				! .

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_inj_dcache_tag)
#endif	/* lint */

/*
 * This routine is used to corrupt the tag, the tag parity, or the tag
 * valid bits of the L1 data cache line determined by the paddr.
 *
 * The ASI_LSU_DIAG_REG is used to put the data cache into paddr
 * replacement policy mode.
 *
 * This routine is similar to the above n2_inj_dcache_tag() test but here the
 * address to corrupt is accessed, corrupted, and accessed again (to trigger
 * the error) all while in hyperpriv mode.
 *
 * NOTE: this N2 routine can be used by KT/RF with no modifications but
 *	 note that the tag address bits are [34:2] on KT/RF.
 */

/*
 * Prototype:
 *
 * int n2_inj_dcache_hvtag(uint64_t paddr, uint64_t xorpat, uint_t checkflag,
 *				uint_t access_type);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - xorpat (valid tag bits[30:2], parity[13], valid[14])
 *	%o3 - check-bit flag
 *	%o4 - access type (LOAD or STORE)
 *	%g1 - temp
 *	%g2 - temp
 *	%g3 - temp then tag read from cache
 *	%g4 - asi access addr
 *	%g5 - unused
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_dcache_hvtag(uint64_t paddr, uint64_t xorpat, uint_t checkflag,
				uint_t access_type)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(n2_inj_dcache_hvtag)

	/*
	 * Generate register addresses to use below.
	 */
	mov	0xff8, %g3			! bitmask for asi access 
	sllx	%g3, 1, %g3			! .
	and	%o1, %g3, %g4			! %g4 = PA[12:4] for asi

	mov	0x10, %g2			! set d$ direct mapped mode
	ldxa	[%g2]N2_ASI_LSU_DIAG_REG, %g3	!  (preserve i$ mode)
	or	%g3, 0x2, %g3			! .
	stxa	%g3, [%g2]N2_ASI_LSU_DIAG_REG	! .
	membar	#Sync				! .

	ba	1f				! branch to aligned code
	  nop

	.align	64
1:
	ldx	[%o1], %g0			! read addr to get it into d$
	membar	#Sync				! ensure data is in d$

	ldxa	[%g4]ASI_DC_TAG, %g3		! read the tag to corrupt
	cmp	%o3, %g0			! if check-bit flag != 0
	bnz,a,pt %icc, 2f			!   set valid|parity flip bit
	  or	%g4, %o2, %g4			!   .
	xor	%g3, %o2, %g3			! else corrupt the tag
2:
	stxa	%g3, [%g4]ASI_DC_TAG		! write it back to d$
	membar	#Sync				! required

	ldxa	[%g2]N2_ASI_LSU_DIAG_REG, %g3	! restore d$ replacement mode
	andn	%g3, 0x2, %g3			! .
	stxa	%g3, [%g2]N2_ASI_LSU_DIAG_REG	! .
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
	SET_SIZE(n2_inj_dcache_hvtag)
#endif	/* lint */

/*
 * This routine is used to corrupt the tag, the tag parity, or the tag
 * valid bits of the L1 data cache line determined by the cache offset.
 *
 * The method used is similar to the above n2_inj_dcache_tag() routine.
 *
 * NOTE: this N2 routine can be used by KT/RF with no modifications but
 *	 note that the tag address bits are [34:2] on KT/RF.
 */

/*
 * Prototype:
 *
 * int n2_inj_dphys_tag(uint64_t offset, uint64_t xorpat, uint_t checkflag,
 *				uint64_t *debug);
 *
 * Register usage:
 *
 *	%o1 - byte offset
 *	%o2 - xorpat (valid tag bits[30:2], parity[13], valid[14])
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
n2_inj_dphys_tag(uint64_t offset, uint64_t xorpat, uint_t checkflag,
				uint64_t *debug)
{
	return 0;
}
#else
	.align	64
	ENTRY_NP(n2_inj_dphys_tag)

	mov	0xff8, %g3			! bitmask for asi access 
	sllx	%g3, 1, %g3			! .
	and	%o1, %g3, %g4			! %g4 = PA[12:4] for asi

	ldxa	[%g4]ASI_DC_TAG, %g3		! read the tag to corrupt
	cmp	%o3, %g0			! if check-bit flag != 0
	bnz,a,pt %icc, 1f			!   set valid|parity flip bit
	  or	%g4, %o2, %g4			!   .
	xor	%g3, %o2, %g3			! else corrupt the tag
1:
	stxa	%g3, [%g4]ASI_DC_TAG		! write it back to d$
	membar	#Sync				! required

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_inj_dphys_tag)
#endif	/* lint */

/*
 * This routine is used to place multiple copies of the same tag into the
 * L1 data cache at locations determined by the paddr.
 *
 * The critical section of each d$ routine must fit on one L2$ line to
 * avoid unwanted side effects.
 *
 * Note that the priority of d$ errors is DCVP -> DCTP -> DCTM -> DCDP.
 *
 * NOTE: this N2 routine can be used by KT/RF with no modifications but
 *	 note that the tag address bits are [34:2] on KT/RF.
 */

/*
 * Prototype:
 *
 * int n2_inj_dcache_mult(uint64_t paddr, uint64_t xorpat, uint64_t *debug);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - xorpat (unused)
 *	%o3 - pointer to debug buffer (debug)
 *	%o4 - unused
 *	%g1 - temp then way mask for xor
 *	%g2 - unused
 *	%g3 - temp then tag read from cache
 *	%g4 - asi access addr
 *	%g5 - unused
 *	%g6 - addr for LSU_DIAG_REG
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_dcache_mult(uint64_t paddr, uint64_t xorpat, uint64_t *debug)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(n2_inj_dcache_mult)

	/*
	 * Generate register addresses to use below.
	 */
	mov	0xff8, %g3			! bitmask for asi access
	sllx	%g3, 1, %g3			! .
	and	%o1, %g3, %g4			! %g4 = PA[12:4] for asi

	mov	3, %g1				! %g1 = way bit mask [12:11]
	sllx	%g1, 11, %g1			! .

	mov	0x10, %g6			! set d$ direct mapped mode
	ldxa	[%g6]N2_ASI_LSU_DIAG_REG, %g3	!  (preserve i$ mode)
	or	%g3, 0x2, %g3			! .
	stxa	%g3, [%g6]N2_ASI_LSU_DIAG_REG	! .
	membar	#Sync				! .

#ifdef	L1_DEBUG_BUFFER
	stx	%o1, [%o3]			! paddr
	stx	%g4, [%o3 + 8]			! first asi load/store addr
#endif

	ba	1f				! branch to aligned code
	  nop

	.align	64
1:
	/*
	 * This routine actually brings the line (the injector buffer)
	 * into the cache in DM mode and then copies that tag to
	 * insert it into another way of the cache.  The tag could be
	 * made from scratch (using the paddr) but this way there is a
	 * valid tag (and it will be in L2) which better simulates a
	 * real system situation.
	 */
	ldx	[%o1], %g0			! read paddr to get it into d$
	membar	#Sync				! ensure data is in d$

	ldxa	[%g4]ASI_DC_TAG, %g3		! read original tag to copy

#ifdef	L1_DEBUG_BUFFER
	stx	%g3, [%o3 + 0x20]		! original cache tag
#endif

	/*
	 * Write the same tag to another way (invert the way bits).
	 * Note that by inverting both way bits this code will not trip
	 * over HW errata #65 (way bits are part of parity caclulation).
	 */
	xor	%g4, %g1, %g4			! invert the way bits
	stxa	%g3, [%g4]ASI_DC_TAG		! write the same tag to new way
	membar	#Sync				! required

#ifdef	L1_DEBUG_BUFFER
	ldxa	[%g4]ASI_DC_TAG, %g3		! read last tag written
	stx	%g3, [%o3 + 0x30]		! cache tag
	stx	%g4, [%o3 + 0x38]		! asi store access addr
#endif

	ldxa	[%g6]N2_ASI_LSU_DIAG_REG, %g3	! restore d$ replacement mode
	andn	%g3, 0x2, %g3			! .
	stxa	%g3, [%g6]N2_ASI_LSU_DIAG_REG	! .
	membar	#Sync				! .

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_inj_dcache_mult)
#endif	/* lint */

/*
 * This routine is used to place multiple copies of the same tag into the
 * L1 data cache at locations determined by the paddr.
 *
 * This routine is similar to the above n2_inj_dcache_mult() test but here the
 * address passed in is accessed, the tags are written, and the address is
 * accessed again (to trigger the error) all while in hyperpriv mode.
 *
 * NOTE: this N2 routine can be used by KT/RF with no modifications but
 *	 note that the tag address bits are [34:2] on KT/RF.
 */

/*
 * Prototype:
 *
 * int n2_inj_dcache_hvmult(uint64_t paddr, uint64_t xorpat, uint64_t *debug);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - xorpat (unused)
 *	%o3 - access type (LOAD or STORE)
 *	%o4 - unused
 *	%g1 - temp then way mask for xor
 *	%g2 - unused
 *	%g3 - temp then tag read from cache
 *	%g4 - asi access addr
 *	%g5 - unused
 *	%g6 - addr for LSU_DIAG_REG
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_dcache_hvmult(uint64_t paddr, uint64_t xorpat, uint_t access_type)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(n2_inj_dcache_hvmult)

	/*
	 * Generate register addresses to use below.
	 */
	mov	0xff8, %g3			! bitmask for asi access
	sllx	%g3, 1, %g3			! .
	and	%o1, %g3, %g4			! %g4 = PA[12:4] for asi

	mov	3, %g1				! %g1 = way bit mask [12:11]
	sllx	%g1, 11, %g1			! .

	mov	0x10, %g6			! set d$ direct mapped mode
	ldxa	[%g6]N2_ASI_LSU_DIAG_REG, %g3	!  (preserve i$ mode)
	or	%g3, 0x2, %g3			! .
	stxa	%g3, [%g6]N2_ASI_LSU_DIAG_REG	! .
	membar	#Sync				! .

	ba	1f				! branch to aligned code
	  nop

	.align	64
1:
	/*
	 * This routine actually brings the line (the injector buffer)
	 * into the cache in DM mode and then copies that tag to
	 * insert it into another way of the cache.  The tag could be
	 * made from scratch (using the paddr) but this way there is a
	 * valid tag (and it will be in L2) which better simulates a
	 * real system situation.
	 */
	ldx	[%o1], %g0			! read paddr to get it into d$
	membar	#Sync				! ensure data is in d$

	ldxa	[%g4]ASI_DC_TAG, %g3		! read original tag to copy

	/*
	 * Write the same tag to another way (invert the way bits).
	 * Note that by inverting both way bits this code will not
	 * trip over errata #65 (way bits are part of parity caclulation).
	 */
	xor	%g4, %g1, %g4			! invert the way bits
	stxa	%g3, [%g4]ASI_DC_TAG		! write the same tag to new way
	membar	#Sync				! required

	ldxa	[%g6]N2_ASI_LSU_DIAG_REG, %g3	! restore d$ replacement mode
	andn	%g3, 0x2, %g3			! .
	stxa	%g3, [%g6]N2_ASI_LSU_DIAG_REG	! .
	membar	#Sync				! .

	/*
	 * Trigger the injected error while still in hypervisor context.
	 */
	cmp	%o3, %g0			! if access_type != 0
	bnz,a,pt %xcc, 2f			!   trigger error via a store
	  stuw	%g0, [%o1] 			!   .
	ldx	[%o1], %g0			! else trigger via a load
2:
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_inj_dcache_hvmult)
#endif	/* lint */

/*
 * This routine is used to place multiple copies of the same tag into the
 * L1 data cache at locations determined by the cache offset.
 *
 * The method used is similar to the above n2_inj_dcache_mult() routine.
 *
 * NOTE: this N2 routine can be used by KT/RF with no modifications but
 *	 note that the tag address bits are [34:2] on KT/RF.
 */

/*
 * Prototype:
 *
 * int n2_inj_dphys_mult(uint64_t offset, uint64_t xorpat, uint64_t *debug);
 *
 * Register usage:
 *
 *	%o1 - offset
 *	%o2 - xorpat (unused)
 *	%o3 - pointer to debug buffer (debug)
 *	%o4 - unused
 *	%g1 - temp then way mask for xor
 *	%g2 - unused
 *	%g3 - temp then tag read from cache
 *	%g4 - asi access addr
 *	%g5 - unused
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_dphys_mult(uint64_t offset, uint64_t xorpat, uint64_t *debug)
{
	return 0;
}
#else
	.align	64
	ENTRY_NP(n2_inj_dphys_mult)

	/*
	 * Generate register addresses to use below.
	 */
	mov	0xff8, %g3			! bitmask for asi access
	sllx	%g3, 1, %g3			! .
	and	%o1, %g3, %g4			! %g4 = PA[12:4] for asi

	mov	3, %g1				! %g1 = way bit mask [12:11]
	sllx	%g1, 11, %g1			! .

	ba	1f				! branch to aligned code
	  nop

	.align	64
1:
	ldxa	[%g4]ASI_DC_TAG, %g3		! read original tag to copy

	/*
	 * Write the same tag to another way (invert the way bits).
	 * Note that by inverting both way bits this code will not
	 * trip over errata #65 (way bits are part of parity caclulation).
	 *
	 * Also note that because this a semi-random line in the cache that
	 * it may be in the invalid state.  This routine will not set the
	 * valid bits as this might cause other code execution errors.
	 */
	xor	%g4, %g1, %g4			! invert the way bits
	stxa	%g3, [%g4]ASI_DC_TAG		! write the same tag to new way
	membar	#Sync				! required

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_inj_dphys_mult)
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
 * Although the PRM does not state it Niagara-II calculates the i$ parity
 * for ASI stores as well as normal fault-ins.  This means i$ data errors
 * can only be injected by setting the parity bit (bit 32) when doing ASI
 * store accesses.  In effect the xor pattern inpar is ignored.
 *
 * The ASI_LSU_DIAG_REG is not used to disable the way replacement since
 * on Niagara-I it simply disabled 3 of the 4 ways reducing the i$ size.
 * Since this limitation has been removed on Niagara-II these routines
 * could be re-written to place the i$ into DM mode before the fault-in.
 * The valid bit(s) are checked during the tag match to ensure a proper hit.
 * This would potentially reduce the number of instructions these routines
 * require.
 *
 * ASI_ICACHE_INSTR (0x66)
 *
 * +-------+-----+-------+------+-------+
 * | Rsvd0 | Way | Index | Word | Rsvd1 |
 * +-------+-----+-------+------+-------+
 *   63:15  14:12  11:6    5:3     2:0
 *
 * Data returned as INSTR[31:0], PARITY[32]
 */

/*
 * Prototype:
 *
 * int n2_inj_icache_instr(uint64_t paddr, uint64_t xorpat, uint64_t *debug);
 *
 * Register usage:
 *
 *	%o1 - paddr then temp
 *	%o2 - xorpat (unused) then set bits for ASI instr access, then way bits
 *	%o3 - pointer to debug buffer (debug)
 *	%o4 - mask for parity bit (for parity flip, not an inpar)
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
n2_inj_icache_instr(uint64_t paddr, uint64_t xorpat, uint64_t *debug)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(n2_inj_icache_instr)

	/*
	 * Generate register addresses to use below.
	 */
	mov	%o1, %g5			! save paddr in %g5

#ifdef	L1_DEBUG_BUFFER
	stx	%o1, [%o3]			! paddr
#endif

	mov	1, %g1				! set way inc value and par bit
	sllx	%g1, 12, %g3			! %g3 = way increment value
	sllx	%g1, 32, %o4			! %o4 = parity flip bit

	srlx	%g5, 9, %g2			! shift paddr for tag compare
	or	%g2, 3, %g2			! set the valid bits for cmp
						!  %g2 = tag asi tag cmp value
	and	%g5, 0x7fc, %g5			! mask paddr for asi accesses
	sllx	%g5, 1, %g5			!  %g5 = instr asi set bits
	andn	%g5, 0x3f, %o2			!  %o2 = tag asi set bits

#ifdef	L1_DEBUG_BUFFER
	stx	%g2, [%o3 + 0x10]		! asi access compare values
	stx	%o2, [%o3 + 0x18]		! parity bit
	stx	%g5, [%o3 + 0x20]		! asi index for both asis
#endif

	mov	8, %g1				! %g1 = way loop counter
	ba	1f				! branch to aligned code
	  mov	%g0, %o0			! set return value to PASS

	/*
	 * Try to find which way the instructions ended up in.
	 */
	.align	64
1:
	ldxa	[%o2]ASI_ICACHE_TAG, %g6	! read tag for this way
	sllx	%g6, 33, %g6			! remove the parity bit [31]
	srlx	%g6, 33, %g6			!   by shifting it out of reg
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
	mov	0x7, %o1			! extract way bits [14:12]
	sllx	%o1, 12, %o1			!   from the tag that matched
	and	%o2, %o1, %o2			!   .
	or	%g5, %o2, %g5			! combine index and way bits
	ldxa	[%g5]ASI_ICACHE_INSTR, %g4	! read the instr to corrupt

#ifdef	L1_DEBUG_BUFFER
	stx	%g5, [%o3 + 0x70]		! the asi/paddr to corrupt
	stx	%g4, [%o3 + 0x78]		! instr data from cache
#endif

	or	%g4, %o4, %g4			! corrupt via parity flip
	stxa	%g4, [%g5]ASI_ICACHE_INSTR	! write it back to i$
	membar	#Sync				! .

#ifdef	L1_DEBUG_BUFFER
	ldxa	[%g5]ASI_ICACHE_INSTR, %g4	! read it back to check it
	stx	%g4, [%o3 + 0x80]		!   was corrupted right
#endif

3:
	done					! return (value set above)
	SET_SIZE(n2_inj_icache_instr)
#endif	/* lint */

/*
 * This routine is used to corrupt the instruction parity of the L1 instruction
 * cache line determined by the paddr.
 *
 * This routine is similar to the above n2_inj_icache_instr() test but here the
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
 * int n2_inj_icache_hvinstr(uint64_t paddr, uint64_t xorpat, uint64_t *debug);
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
n2_inj_icache_hvinstr(uint64_t paddr, uint64_t xorpat, uint64_t *debug)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(n2_inj_icache_hvinstr)

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
	sllx	%g1, 12, %g3			! %g3 = way increment value
	sllx	%g1, 32, %o4			! %o4 = parity flip bit

	srlx	%g5, 9, %g2			! shift paddr for tag compare
	or	%g2, 3, %g2			! set the valid bits for cmp
						!  %g2 = tag asi tag cmp value
	and	%g5, 0x7fc, %g5			! mask paddr for asi accesses
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

	mov	8, %g1				! %g1 = way loop counter
	ba	1f				! branch to aligned code
	  nop					! .

	/*
	 * Try to find which way the instructions ended up in.
	 */
	.align	64
1:
	ldxa	[%o2]ASI_ICACHE_TAG, %g6	! read tag for this way
	sllx	%g6, 33, %g6			! remove the parity bit [31]
	srlx	%g6, 33, %g6			!   by shifting it out of reg
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
	mov	0x7, %o1			! extract way bits [14:12]
	sllx	%o1, 12, %o1			!   from the tag that matched
	and	%o2, %o1, %o2			!   .
	or	%g5, %o2, %g5			! combine index and way bits
	ldxa	[%g5]ASI_ICACHE_INSTR, %g4	! read the instr to corrupt

#ifdef	L1_DEBUG_BUFFER
	stx	%g5, [%o3 + 0x70]		! the asi/paddr to corrupt
	stx	%g4, [%o3 + 0x78]		! instr data from cache
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
	SET_SIZE(n2_inj_icache_hvinstr)
#endif	/* lint */

/*
 * Version of the asmld routine which is to be called while running in
 * hyperpriv mode ONLY from the above n2_inj_icache_hvinstr() function.
 */
#if defined(lint)
void
n2_ic_hvaccess(void)
{}
#else
	ENTRY(n2_ic_hvaccess)
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
        SET_SIZE(n2_ic_hvaccess)
#endif	/* lint */

/*
 * This routine is used to corrupt the instr or the instr parity of the L1
 * instruction cache line word determined a byte offset into the cache.
 * Valid offsets are in the range 0x0 to 0x3ffc (16K I-cache consisting of
 * 4-byte instrs).
 *
 * The method used is similar to the above n2_inj_icache_instr() routine.
 */

/*
 * Prototype:
 *
 * int n2_inj_iphys_instr(uint64_t offset, uint64_t xorpat, uint64_t *debug);
 *
 * Register usage:
 *
 *	%o1 - byte offset
 *	%o2 - xorpat (unused)
 *	%o3 - pointer to debug buffer (debug)
 *	%o4 - mask for parity bit
 *	%g1 - temp then mask for offset
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
n2_inj_iphys_instr(uint64_t offset, uint64_t xorpat, uint64_t *debug)
{
	return 0;
}
#else
	.align	64
	ENTRY_NP(n2_inj_iphys_instr)

	/*
	 * Generate register addresses to use below.
	 */
	mov	1, %g1				! set the parity bit for asi
	sllx	%g1, 32, %o4			! %o4 = parity bit flip

	mov	0xfff, %g1			! build mask for offset
	sllx	%g1, 2, %g1			! %g1 = mask [13:2]

	and	%o1, %g1, %o1			! mask offset for asi access
	sllx	%o1, 1, %o1			! shift offset for asi access

	ldxa	[%o1]ASI_ICACHE_INSTR, %g4	! read the instr to corrupt
	or	%g4, %o4, %g4			! corrupt instr via parity flip
	stxa	%g4, [%o1]ASI_ICACHE_INSTR	! write it back to i$
	membar	#Sync				! .

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_inj_iphys_instr)
#endif	/* lint */

/*
 * This routine is used to corrupt the tag parity (including valid bit parity)
 * of the L1 instruction cache line determined by the paddr.  This routine is
 * similar to the above n2_inj_icache_instr() test but the tag is the target
 * of the corruption not the instruction.
 *
 * The xorpat must be 0x10000 (only bit 16 set) for tag parity,
 * or 0x8000 (only bit 15 set) for valid-bit mismatch since the single
 * parity or (slave) valid bit is inverted using the built-in asi mechanism.
 *
 * ASI_ICACHE_TAG (0x67)
 *
 * +-------+--------+---------+-----+-----+-------+
 * | Rsvd0 | Perren | VBerren | Way | Set | Rsvd1 |
 * +-------+--------+---------+-----+-----+-------+
 *   63:17     16       15     14:12 11:6    5:0
 *
 * Data stored as TAG[30:2] = PA[39:11], VM[1]
 * Data returned as TAG[30:2] = PA[39:11], PARITY[31], VM[1], VS[0]
 */

/*
 * Prototype:
 *
 * int n2_inj_icache_tag(uint64_t paddr, uint64_t xorpat, uint64_t *debug);
 *
 * Register usage:
 *
 *	%o1 - paddr (then clobbered)
 *	%o2 - xorpat (parity[16], valid[15])
 *	%o3 - pointer to debug buffer (debug)
 *	%o4 - unused
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
n2_inj_icache_tag(uint64_t paddr, uint64_t xorpat, uint64_t *debug)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(n2_inj_icache_tag)

	/*
	 * Generate register addresses to use below.
	 */
	mov	1, %g1				! set way inc value and par bit
	sllx	%g1, 12, %g3			! %g3 = way increment value

	mov	%o1, %g5			! copy paddr to build asi addr
	srlx	%g5, 9, %g2			! shift paddr for tag compare
	or	%g2, 3, %g2			! set the valid bits for cmp
						!  %g2 = tag asi tag cmp value
	and	%g5, 0x7e0, %g5			! mask paddr for asi accesses
	sllx	%g5, 1, %g5			!  %g5 = tag asi set bits

	mov	8, %g1				! %g1 = way loop counter
	ba	1f				! branch to aligned code
	  mov	%g0, %o0			! set return value to PASS

	/*
	 * Try to find which way the data ended up in.
	 */
	.align	64
1:
	ldxa	[%g5]ASI_ICACHE_TAG, %g4	! read tag for this way
	sllx	%g4, 33, %g4			! remove the parity bit [31]
	srlx	%g4, 33, %g4			!   by shifting it out of reg
	cmp	%g4, %g2			! is the data in this way?
	be	%icc, 2f			!   (yes if lower 32-bits match)
	  sub	%g1, 1, %g1			! decrement loop counter
	brz,a	%g1, 3f				! if not found in any way then
	  mov	0xded, %o1			!   return value == error
	ba	1b				! loop again
	  add	%g5, %g3, %g5			! generate next asi addr
						!   for tag read (incr way)
	/*
	 * Found the matching tag, now corrupt the tag.
	 */
2:
	or	%g5, %o2, %g5			! corrupt parity or valid bit
	stxa	%g4, [%g5]ASI_ICACHE_TAG	! write tag back to i$
	membar	#Sync				! .
3:
	done					! return (value set above)
	SET_SIZE(n2_inj_icache_tag)
#endif	/* lint */

/*
 * This routine is used to corrupt the tag parity of the L1 instruction
 * cache line determined by the paddr.
 *
 * This routine is similar to the above n2_inj_icache_tag() test but here the
 * routine to corrupt is called, corrupted, and called again (to trigger the
 * error) all while in hyperpriv mode.
 */

/*
 * Prototype:
 *
 * int n2_inj_icache_hvtag(uint64_t paddr, uint64_t xorpat, uint64_t *debug);
 *
 * Register usage:
 *
 *	%o0 - saved paddr of routine to corrupt/call (not an inpar)
 *	%o1 - paddr (clobbered)
 *	%o2 - xorpat (parity[16], valid[15])
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
n2_inj_icache_hvtag(uint64_t paddr, uint64_t xorpat, uint64_t *debug)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(n2_inj_icache_hvtag)

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
	sllx	%g1, 12, %g3			! %g3 = way increment value

	srlx	%g5, 9, %g2			! shift paddr for tag compare
	or	%g2, 3, %g2			! set the valid bits for cmp
						!  %g2 = tag asi tag bits
	and	%g5, 0x7e0, %g5			! mask paddr for asi accesses
	sllx	%g5, 1, %g5			!  %g5 = tag asi set bits

	/*
	 * Run the routine to corrupt to bring it into cache.
	 * Must ensure that it's not too far away for the jmp.
	 */
	jmp	%o0
          rd    %pc, %g7

	ba	1f				! branch to aligned code
	  mov	8, %g1				!   %g1 = way loop counter

	/*
	 * Try to find which way the data ended up in.
	 */
	.align	64
1:
	ldxa	[%g5]ASI_ICACHE_TAG, %g4	! read tag for this way
	sllx	%g4, 33, %g4			! remove the parity bit [31]
	srlx	%g4, 33, %g4			!   by shifting it out of reg
	cmp	%g4, %g2			! is the data in this way?
	be	%icc, 2f			!   (yes if lower 32-bits match)
	  sub	%g1, 1, %g1			! decrement loop counter
	brz,a	%g1, 3f				! if not found in any way then
	  mov	0xded, %o1			!   return value = error
	ba	1b				! loop again
	  add	%g5, %g3, %g5			! generate next asi addr
						!   for tag read (incr way)
	/*
	 * Found the matching tag, now corrupt it.
	 */
2:
	or	%g5, %o2, %g5			! corrupt parity or valid bit
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
	SET_SIZE(n2_inj_icache_hvtag)
#endif	/* lint */

/*
 * This routine is used to corrupt the tag or the tag parity of the L1
 * instruction cache line determined a byte offset into the cache.
 *
 * The method used is similar to the above n2_inj_icache_tag() and
 * n2_inj_iphys_instr() routines.
 */

/*
 * Prototype:
 *
 * int n2_inj_iphys_tag(uint64_t offset, uint64_t xorpat, uint64_t *debug);
 *
 * Register usage:
 *
 *	%o1 - byte offset
 *	%o2 - xorpat (parity[16], valid[15])
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
n2_inj_iphys_tag(uint64_t offset, uint64_t xorpat, uint64_t *debug)
{
	return 0;
}
#else
	.align	64
	ENTRY_NP(n2_inj_iphys_tag)

	/*
	 * Generate register addresses to use below.
	 */
	mov	0xfff, %g1			! build mask for offset
	sllx	%g1, 2, %g1			! %g1 = mask [13:2]

	and	%o1, %g1, %o1			! mask offset for asi access
	sllx	%o1, 1, %o1			! shift offset for asi access

	ldxa	[%o1]ASI_ICACHE_TAG, %g4	! read the tag to corrupt
	or	%g4, %o2, %g4			! corrupt tag via parity flip
	stxa	%g4, [%o1]ASI_ICACHE_TAG	! write it back to i$
	membar	#Sync				! .

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_inj_iphys_tag)
#endif	/* lint */

/*
 * This routine is used to place multiple copies of the same tag into the
 * L1 instruction cache at locations determined by a paddr argument.
 *
 * The critical section of each i$ routine must fit on one L2$ line to
 * avoid unwanted side effects.
 *
 * Note that the priority of i$ errors is ICVP -> ICTP -> ICTM -> ICDP.
 */

/*
 * Prototype:
 *
 * int n2_inj_icache_mult(uint64_t paddr, uint64_t xorpat, uint64_t *debug);
 *
 * Register usage:
 *
 *	%o1 - paddr (then clobbered)
 *	%o2 - xorpat inpar (unused), way bit mask for copy
 *	%o3 - pointer to debug buffer (debug)
 *	%o4 - unused
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
n2_inj_icache_mult(uint64_t paddr, uint64_t xorpat, uint64_t *debug)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(n2_inj_icache_mult)

	/*
	 * Generate register addresses to use below.
	 */
	mov	1, %g1				! set way inc value and par bit
	sllx	%g1, 12, %g3			! %g3 = way increment value

	mov	%o1, %g5			! copy paddr to build asi addr
	srlx	%g5, 9, %g2			! shift paddr for tag compare
	or	%g2, 3, %g2			! set the valid bits for cmp
						!  %g2 = tag asi tag cmp value
	and	%g5, 0x7e0, %g5			! mask paddr for asi accesses
	sllx	%g5, 1, %g5			!  %g5 = tag asi set bits[11:6]

	mov	7, %o2				! %o2 = way mask for xor
	sllx	%o2, 12, %o2			! .

	mov	8, %g1				! %g1 = way loop counter
	ba	1f				! branch to aligned code
	  mov	%g0, %o0			! set return value to PASS

	/*
	 * Try to find which way the data ended up in.
	 */
	.align	64
1:
	ldxa	[%g5]ASI_ICACHE_TAG, %g4	! read tag for this way
	sllx	%g4, 33, %g4			! remove the parity bit [31]
	srlx	%g4, 33, %g4			!   by shifting it out of reg
	cmp	%g4, %g2			! is the data in this way?
	be	%icc, 2f			!   (yes if lower 32-bits match)
	  sub	%g1, 1, %g1			! decrement loop counter
	brz,a	%g1, 3f				! if not found in any way then
	  mov	0xded, %o1			!   return value == error
	ba	1b				! loop again
	  add	%g5, %g3, %g5			! generate next asi addr
						!   for tag read (incr way)
	/*
	 * Found the matching tag, now insert a copy of it in another way.
	 */
2:
	xor	%g5, %o2, %g5			! invert way bits for new way
	stxa	%g4, [%g5]ASI_ICACHE_TAG	! write tag copy to i$
	membar	#Sync				! .
3:
	done					! return (value set above)
	SET_SIZE(n2_inj_icache_mult)
#endif	/* lint */

/*
 * This routine is used to place multiple copies of the same tag into the
 * L1 instruction cache at locations determined by a paddr or byte offset
 * argument.
 *
 * This routine is similar to the above n2_inj_icache_mult() test but here the
 * routine to corrupt is accessed, the tags are written, and then 
 * accessed again (to trigger the error) all while in hyperpriv mode.
 *
 * NOTE: this is using the existing N2 i$ access routine for the corruption.
 * 	 A new routine may need to be created specifically for this routine
 *	 if code moves/changes and this routine stops working.
 */

/*
 * Prototype:
 *
 * int n2_inj_icache_hvmult(uint64_t paddr, uint64_t xorpat, uint64_t *debug);
 *
 * Register usage:
 *
 *	%o0 - saved paddr of routine to corrupt/call (not an inpar)
 *	%o1 - paddr (clobbered)
 *	%o2 - xorpat (unused) then xor mask to use on way bits
 *	%o3 - pointer to debug buffer (debug)
 *	%o4 - unused
 *	%g1 - temp then way increment
 *	%g2 - valid bit flip bit (and valid bit)
 *	%g3 - temp then tag read from cache
 *	%g4 - asi access addr
 *	%g5 - way counter
 *	%g6 - unused
 *	%g7 - return address for called routine
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_icache_hvmult(uint64_t paddr, uint64_t xorpat, uint64_t *debug)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(n2_inj_icache_hvmult)

	/*
	 * Calling the routine to be corrupted from this routine gives us a
	 * more regular "critical section" to trigger errors from hyperpriv.
	 *
	 * Note that the call (jmp) to the below hv_ic_hvaccess clobbers %o1.
	 */
	mov	%o1, %g5			! copy routine paddr so it can
	mov	%g5, %o0			!   be used and called below

	/*
	 * Generate register addresses to use below.
	 */
	mov	1, %g1				! set way inc value and par bit
	sllx	%g1, 12, %g3			! %g3 = way increment value

	srlx	%g5, 9, %g2			! shift paddr for tag compare
	or	%g2, 3, %g2			! set the valid bits for cmp
						!  %g2 = tag asi tag cmp value
	and	%g5, 0x7e0, %g5			! mask paddr for asi accesses
	sllx	%g5, 1, %g5			!  %g5 = tag asi set bits

	mov	7, %o2				! %o2 = way mask for xor
	sllx	%o2, 12, %o2			! .

	/*
	 * Run the routine to corrupt to bring it into cache.
	 * Must ensure that it's not too far away for the jmp.
	 */
	jmp	%o0
	  rd	%pc, %g7

	ba	1f				! branch to aligned code
	  mov	8, %g1				!   %g1 = way loop counter

	/*
	 * Try to find which way the data ended up in.
	 */
	.align	64
1:
	ldxa	[%g5]ASI_ICACHE_TAG, %g4	! read tag for this way
	sllx	%g4, 33, %g4			! remove the parity bit [31]
	srlx	%g4, 33, %g4			!   by shifting it out of reg
	cmp	%g4, %g2			! is the data in this way?
	be	%icc, 2f			!   (yes if lower 32-bits match)
	  sub	%g1, 1, %g1			! decrement loop counter
	brz,a	%g1, 3f				! if not found in any way then
	  mov	0xded, %o1			!   return value == error
	ba	1b				! loop again
	  add	%g5, %g3, %g5			! generate next asi addr
						!   for tag read (incr way)
	/*
	 * Found the matching tag, now insert a copy of it in another way.
	 */
2:
	xor	%g5, %o2, %g5			! invert way bits for new way
	stxa	%g4, [%g5]ASI_ICACHE_TAG	! write tag copy to i$
	membar	#Sync				! .
3:
	/*
	 * Trigger the planted error by calling the routine that was
	 * corrupted above w/o leaving hypervisor context.
	 */
	jmp	%o0
	  rd	%pc, %g7

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_inj_icache_hvmult)
#endif	/* lint */

/*
 * This routine is used to place multiple copies of the same tag into the
 * L1 instruction cache line determined a byte offset into the cache.
 *
 * The method used is similar to the above n2_inj_icache_mult() and
 * n2_inj_iphys_tag() routines.
 */

/*
 * Prototype:
 *
 * int n2_inj_iphys_mult(uint64_t offset, uint64_t xorpat, uint64_t *debug);
 *
 * Register usage:
 *
 *	%o1 - byte offset
 *	%o2 - xorpat inpar (unused) then way bit mask for invert
 *	%o3 - pointer to debug buffer (debug)
 *	%o4 - unused
 *	%g1 - mask then the way bits from offset
 *	%g2 - unused
 *	%g3 - unused
 *	%g4 - tag read from cache
 *	%g5 - addr bits for asi access
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_iphys_mult(uint64_t offset, uint64_t xorpat, uint64_t *debug)
{
	return 0;
}
#else
	.align	64
	ENTRY_NP(n2_inj_iphys_mult)

	/*
	 * Generate register addresses to use below.
	 */
	mov	0xfff, %g1			! build mask for offset
	sllx	%g1, 2, %g1			! %g1 = mask [13:2]

	and	%o1, %g1, %g5			! mask offset for asi access
	sllx	%g5, 1, %g5			! shift offset for asi access

	mov	7, %o2				! %o2 = way mask for xor
	sllx	%o2, 12, %o2			! .

	ldxa	[%g5]ASI_ICACHE_TAG, %g4	! read the tag to copy
	xor	%g5, %o2, %g5			! invert way bits for new way
	stxa	%g4, [%g5]ASI_ICACHE_TAG	! write tag copy to i$
	membar	#Sync				! .

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_inj_iphys_mult)
#endif	/* lint */

/*------------------ end of the I$ / start of Internal functions -----------*/

/*
 * This routine is used to corrupt the ecc of the integer register file
 * using the per core error injection register.  Because a single error
 * injection register is shared by all strands only a single strand should
 * be active for this routine to produce deterministic results.
 *
 * ASI_INJECT_ERROR_REG (0x43, VA=0x0)
 *
 * +-------+-----+--------+----------+-----+-----+-------+----------+---------+
 * | Rsvd0 | Enb | Rrsvd1 + TLB bits | IRC | FRC | Rsvd2 + Int bits | ECCMask |
 * +-------+-----+--------+----------+-----+-----+-------+----------+---------+
 *   63:32   31    30   29    28    27    26    25    24    23:8      7:0
 *
 * NOTE: on Niagara-I the ECCMask bits must be written once before any of the
 *	 other register bits are set for correct error injection.  This is
 *	 no longer required on Niagara-II.
 *
 * NOTE: Niagara-I had a single-shot mode which would inject one error only
 *	 and reset the injection mechanism (and clear the enable bit).
 *	 This feature has been removed on Niagara-II and so the code must
 *	 clear the register after the injection if a single error is desired.
 *
 * NOTE: the PRM warns that "Writes to ASI_ERROR_INJECT are actually post-
 *	 synchronizing, so no MEMBAR #Sync is required after the write.
 *	 However, there are cases where writes to ASI_ERROR_INJECT are not
 *	 presynchronizing (that is, an instruction before the write could see
 *	 the effect). To guard against this, software needs to put a MEMBAR
 *	 #Sync before a write to ASI_ERROR_INJECT."
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
 * int n2_inj_ireg_file(uint64_t buf_paddr, uint64_t enable, uint64_t eccmask,
 *				uint64_t offset);
 *
 * Register usage:
 *
 *	%o1 - paddr of mem scratch buffer
 *	%o2 - enable bit and single shot (clobbered)
 *	%o3 - ecc mask
 *	%o4 - pc relative offset for register access
 *	%g1 - temp
 *	%g2 - holds single-shot flag
 *	%g3 - temp for park/unpark
 *	%g4 - temp for park/unpark
 *	%g5 - temp for park/unpark
 *	%g6 - temp for park/unpark
 *	%g7 - temp for park/unpark
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_ireg_file(uint64_t buf_paddr, uint64_t enable, uint64_t eccmask,
				uint64_t offset)
{
	return 0;
}
#else
	.align	512
	ENTRY_NP(n2_inj_ireg_file)

	/*
	 * Park all sibling strands via the CORE_RUNNING regs so that the
	 * injection will only occur on this strand.
	 */
	N2_PARK_SIBLING_STRANDS(%g7, %g6, %g5, %g4, %g3)

	/*
	 * The number of instructions after the read of the %pc must
	 * remain constant (at five) otherwise the offsets used below
	 * need to be changed.
	 */
	rd	%pc, %g1			! find current pc
	membar	#Sync				! required
	srlx	%o2, EI_REG_SSHOT_ENB_SHIFT, %g2 ! move sshot bit down for cmp
	and	%g2, 1, %g2			!   and mask it
	jmp	%g1 + %o4			! jump ahead to reg access
	  add	%o2, %o3, %o2			! combine mask and enable

	/*
	 * Start of the specific register access section.
	 */
	stx	%i0, [%o1]			! %i0 - offset 24
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	ldx	[%o1], %i0
	ba	1f
	  nop

	stx	%l6, [%o1]			! %l6 - offset 48 (24 + 24)
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	ldx	[%o1], %l6
	ba	1f
	  nop

	stx	%o3, [%o1]			! %o3 - offset 72 (48 + 24)
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	ldx	[%o1], %o3
	ba	1f
	  nop

	/*
	 * Keep this section last due to its bigger size.
	 */
	wrpr    %g0, 0, %gl			! change %gl for correct %g1
	stx	%g1, [%o1]			! %g1 - offset 96 (72 + 24)
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	ldx	[%o1], %g1
	wrpr    %g0, 1, %gl			! restore %gl
	ba	1f
	  nop

	/*
	 * XXX will have to decide how many different registers to
	 * provide access points (offsets) for above, and the invoke
	 * routines require this as well... (same as Niagara-I).
	 */
1:
	/*
	 * If single shot mode was specified, then clear the injection reg.
	 *
	 * Because %gl may have changed, %g2 must be reloaded to ensure its
	 * validity.  However, the injection register must be cleared first
	 * in order to avoid injecting an unwanted error into %g2.  If
	 * single-shot mode was _not_ specified, then error injection will
	 * be re-enabled.  This is okay because the register access to trip
	 * the error is not expected to take place until after this routine
	 * completes.
	 */
	membar	#Sync				! required
	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg
	srlx	%o2, EI_REG_SSHOT_ENB_SHIFT, %g2 ! can't trust %g2 value after
	and	%g2, 1, %g2			!   %gl change.  Reload it.
	brnz,a	%g2, 2f				! check if sshot mode set
	  nop
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! re-enable injection mode
2:
	/*
	 * Unpark the sibling cores that were parked above.
	 */
	N2_UNPARK_SIBLING_STRANDS(%g7, %g6, %g4, %g3)

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_inj_ireg_file)
#endif	/* lint */

/*
 * This routine is used to corrupt the ecc of the integer register file
 * using the per core error injection register.  Because a single error
 * injection register is shared by all strands only a single strand should
 * be active for this routine to produce deterministic results.
 *
 * The method used is similar to the above n2_inj_ireg_file() routine,
 * but this routine has the ability to inject an error into any of the
 * seven "global" registers.  Injection into other register sets are handled
 * by separate routines.
 *
 * This version of the routine also accesses the corrupted register without
 * leaving hyperpriv mode in order to produce errors more reliably and
 * deterministically.
 *
 * NOTE: due to limitations of the injection register the sibling cores
 *	 are parked for a short period in this routine.  This is done
 *	 without protection so only one strand per core can safely run
 *	 an IRF injection routine at a time.  The risk is for a hard hang.
 */

/*
 * Prototype:
 *
 * int n2_inj_ireg_hvfile_global(uint64_t buf_paddr, uint64_t enable,
 *				uint64_t eccmask, uint64_t offset);
 *
 * Register usage:
 *
 *	%o0 - holds extra flags from top half of enable (not an inpar)
 *	%o1 - paddr of mem scratch buffer
 *	%o2 - enable bit and single shot, and NOERR (clobbered)
 *	%o3 - ecc mask
 *	%o4 - pc relative offset for register access
 *	%o5 - anchor pc used for branches
 *	%g1 - potential injection target
 *	%g2 - potential injection target
 *	%g3 - potential injection target
 *	%g4 - potential injection target
 *	%g5 - potential injection target
 *	%g6 - potential injection target
 *	%g7 - potential injection target
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_ireg_hvfile_global(uint64_t buf_paddr, uint64_t enable, uint64_t eccmask,
				uint64_t offset)
{
	return 0;
}
#else
	.align	512
	ENTRY_NP(n2_inj_ireg_hvfile_global)

	/*
	 * Park all sibling strands via the CORE_RUNNING regs so that the
	 * injection will only occur on this strand.
	 */
	N2_PARK_SIBLING_STRANDS(%g7, %g6, %g5, %g4, %g3)

	/*
	 * The number of instructions after the read of the %pc must
	 * remain constant (based on N2_IRF_ERR_STRIDE) and match the
	 * number of instructions in each group below (currently 8).
	 */
	rd	%pc, %o5			! %o5 = current pc

	srlx	%o2, 32, %o0			! %o0 = access flags
	add	%o2, %o3, %o2			! combine mask and enable
	nop
	nop
	nop

	jmp	%o5 + %o4			! jump ahead to reg access
	  nop

	/*
	 * Start of the specific register access section.
	 *
	 * reg %g0 = offset 32 (8 instrs * 1)
	 * Register %g0 is read only so this will NOT inject an error, but
	 * this case is included here for completeness and so the offsets
	 * will be the same as the other IRF injection routines.
	 */
	stx	%g0, [%o1]			! save the contents of reg
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	ldx	[%o1], %g0			! inject error via "load to"
	membar	#Sync				! required
	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg
	ba	1f
	  nop

	/* reg %g1 = offset 64 (8 instrs * 2) */
	stx	%g1, [%o1 + 8]			! save the contents of reg
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	ldx	[%o1 + 8], %g1			! inject error via "load to"
	membar	#Sync				! required
	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg
	ba	1f
	  nop

	/* reg %g2 = offset 96 (8 instrs * 3) */
	stx	%g2, [%o1 + 0x10]		! save the contents of reg
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	ldx	[%o1 + 0x10], %g2		! inject error via "load to"
	membar	#Sync				! required
	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg
	ba	1f
	  nop

	/* reg %g3 = offset 128 (8 instrs * 4) */
	stx	%g3, [%o1 + 0x18]		! save the contents of reg
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	ldx	[%o1 + 0x18], %g3		! inject error via "load to"
	membar	#Sync				! required
	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg
	ba	1f
	  nop

	/* reg %g4 = offset 160 (8 instrs * 5) */
	stx	%g4, [%o1 + 0x20]		! save the contents of reg
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	ldx	[%o1 + 0x20], %g4		! inject error via "load to"
	membar	#Sync				! required
	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg
	ba	1f
	  nop

	/* reg %g5 = offset 192 (8 instrs * 6) */
	stx	%g5, [%o1 + 0x28]		! save the contents of reg
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	ldx	[%o1 + 0x28], %g5		! inject error via "load to"
	membar	#Sync				! required
	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg
	ba	1f
	  nop

	/* reg %g6 = offset 224 (8 instrs * 7) */
	stx	%g6, [%o1 + 0x30]		! save the contents of reg
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	ldx	[%o1 + 0x30], %g6		! inject error via "load to"
	membar	#Sync				! required
	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg
	ba	1f
	  nop

	/* reg %g7 = offset 256 (8 instrs * 8) */
	stx	%g7, [%o1 + 0x38]		! save the contents of reg
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	ldx	[%o1 + 0x38], %g7		! inject error via "load to"
	membar	#Sync				! required
	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg
1:
	/*
	 * Unpark the sibling cores that were parked above, before the access.
	 *
	 * Note that the global regs cannot be used before the access and
	 * some of the out regs need to be preserved as well.  This is why
	 * regs %o5-%o2 are used.
	 */
	N2_UNPARK_SIBLING_STRANDS(%o5, %o4, %o3, %o2)

	/*
	 * Perform the access right here in HV mode, the access type is
	 * based on the value placed in the top half of the enable value.
	 */
	cmp	%o0, EI_REG_CMP_NOERR		! if NOERR flag bit set
	bz,a	%icc, 4f			!   skip access section
	  nop					! .

	cmp	%o0, EI_REG_CMP_OP		! if OP flag bit set
	bz,a	%icc, 2f			!   goto OP access section
	  nop					! .

	cmp	%o0, EI_REG_CMP_LOAD		! if OP flag bit set
	bz,a	%icc, 3f			!   goto LOAD access section
	  nop					! .

	/*
	 * Invoke the error via a STORE (default)
	 */
	stub	%g0, [%o1]
	stuw	%g1, [%o1 + 0x08]
	st	%g2, [%o1 + 0x10]
	stx	%g3, [%o1 + 0x18]

	stub	%g4, [%o1 + 0x20]
	stuw	%g5, [%o1 + 0x28]
	st	%g6, [%o1 + 0x30]
	stx	%g7, [%o1 + 0x38]

	ba	4f				! exit
	  nop
2:
	/*
	 * Invoke the error via an OP (operation).
	 */
	or	%g0, 0x5a, %g0
	sllx	%g1, 0x4, %g0
	add	%g2, 0x66, %g0
	cmp	%g3, %g0

	or	%g4, 0x5a, %g0
	sllx	%g5, 0x4, %g0
	add	%g6, 0x66, %g0
	cmp	%g7, %g0

	ba	4f				! exit
	  nop
3:
	/*
	 * Don't invoke the error via a LOAD (load will not trigger error).
	 * ldx instruction is used so this does not clobber the reg contents.
	 */
	ldx	[%o1], %g0
	ldx	[%o1 + 0x08], %g1
	ldx	[%o1 + 0x10], %g2
	ldx	[%o1 + 0x18], %g3

	ldx	[%o1 + 0x20], %g4
	ldx	[%o1 + 0x28], %g5
	ldx	[%o1 + 0x30], %g6
	ldx	[%o1 + 0x38], %g7
4:
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_inj_ireg_hvfile_global)
#endif	/* lint */

/*
 * This routine is used to corrupt the ecc of the integer register file
 * using the per core error injection register.  Because a single error
 * injection register is shared by all strands this routine parks the
 * sibling strands during the error injection in order to produce
 * deterministic results.
 *
 * The method used is similar to the above n2_inj_ireg_hvfile_global() routine,
 * but this routine has the ability to inject an error into any of the
 * eight "in" registers.  Injection into other register sets are handled by
 * separate routines.
 */

/*
 * Prototype:
 *
 * int n2_inj_ireg_hvfile_in(uint64_t buf_paddr, uint64_t enable,
 *				uint64_t eccmask, uint64_t offset);
 *
 * Register usage:
 *
 *	%o0 - holds extra flags from top half of enable (not an inpar)
 *	%o1 - paddr of mem scratch buffer
 *	%o2 - enable bit and single shot, and NOERR (clobbered)
 *	%o3 - ecc mask
 *	%o4 - pc relative offset for register access
 *	%o5 - anchor pc used for branches
 *	%g1 - unused
 *	%g2 - unused
 *	%g3 - temp for park/unpark
 *	%g4 - temp for park/unpark
 *	%g5 - temp for park/unpark
 *	%g6 - temp for park/unpark
 *	%g7 - temp for park/unpark
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_ireg_hvfile_in(uint64_t buf_paddr, uint64_t enable, uint64_t eccmask,
				uint64_t offset)
{
	return 0;
}
#else
	.align	512
	ENTRY_NP(n2_inj_ireg_hvfile_in)

	/*
	 * Save all the in registers in the scratch buffer so that the load
	 * access section will not clobber them (they belong to the caller).
	 */
	stx	%i0, [%o1]
	stx	%i1, [%o1 + 0x08]
	stx	%i2, [%o1 + 0x10]
	stx	%i3, [%o1 + 0x18]

	stx	%i4, [%o1 + 0x20]
	stx	%i5, [%o1 + 0x28]
	stx	%i6, [%o1 + 0x30]
	stx	%i7, [%o1 + 0x38]

	/*
	 * Park all sibling strands via the CORE_RUNNING regs so that the
	 * injection will only occur on this strand.
	 */
	N2_PARK_SIBLING_STRANDS(%g7, %g6, %g5, %g4, %g3)

	/*
	 * The number of instructions after the read of the %pc must
	 * remain constant (based on N2_IRF_ERR_STRIDE) and match the
	 * number of instructions in each group below (currently 8).
	 */
	rd	%pc, %o5			! %o5 = current pc

	srlx	%o2, 32, %o0			! %o0 = access flags
	add	%o2, %o3, %o2			! combine mask and enable
	nop
	nop
	nop

	jmp	%o5 + %o4			! jump ahead to reg access
	  nop

	/*
	 * Start of the specific register access section.
	 *
	 * reg %i0 = offset 32 (8 instrs * 1)
	 */
	stx	%i0, [%o1]			! save the contents of reg
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	ldx	[%o1], %i0			! inject error via "load to"
	membar	#Sync				! required
	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg
	ba	1f
	  nop

	/* reg %i1 = offset 64 (8 instrs * 2) */
	stx	%i1, [%o1 + 8]			! save the contents of reg
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	ldx	[%o1 + 8], %i1			! inject error via "load to"
	membar	#Sync				! required
	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg
	ba	1f
	  nop

	/* reg %i2 = offset 96 (8 instrs * 3) */
	stx	%i2, [%o1 + 0x10]		! save the contents of reg
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	ldx	[%o1 + 0x10], %i2		! inject error via "load to"
	membar	#Sync				! required
	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg
	ba	1f
	  nop

	/* reg %i3 = offset 128 (8 instrs * 4) */
	stx	%i3, [%o1 + 0x18]		! save the contents of reg
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	ldx	[%o1 + 0x18], %i3		! inject error via "load to"
	membar	#Sync				! required
	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg
	ba	1f
	  nop

	/* reg %i4 = offset 160 (8 instrs * 5) */
	stx	%i4, [%o1 + 0x20]		! save the contents of reg
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	ldx	[%o1 + 0x20], %i4		! inject error via "load to"
	membar	#Sync				! required
	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg
	ba	1f
	  nop

	/* reg %i5 = offset 192 (8 instrs * 6) */
	stx	%i5, [%o1 + 0x28]		! save the contents of reg
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	ldx	[%o1 + 0x28], %i5		! inject error via "load to"
	membar	#Sync				! required
	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg
	ba	1f
	  nop

	/* reg %i6 = offset 224 (8 instrs * 7) */
	stx	%i6, [%o1 + 0x30]		! save the contents of reg
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	ldx	[%o1 + 0x30], %i6		! inject error via "load to"
	membar	#Sync				! required
	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg
	ba	1f
	  nop

	/* reg %i7 = offset 256 (8 instrs * 8) */
	stx	%i7, [%o1 + 0x38]		! save the contents of reg
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	ldx	[%o1 + 0x38], %i7		! inject error via "load to"
	membar	#Sync				! required
	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg
1:
	/*
	 * Unpark the sibling cores that were parked above, before the access.
	 */
	N2_UNPARK_SIBLING_STRANDS(%g7, %g6, %g4, %g3)

	/*
	 * Perform the access right here in HV mode, the access type is
	 * based on the value placed in the top half of the enable value.
	 */
	cmp	%o0, EI_REG_CMP_NOERR		! if NOERR flag bit set
	bz,a	%icc, 4f			!   skip access section
	  nop					! .

	cmp	%o0, EI_REG_CMP_OP		! if OP flag bit set
	bz,a	%icc, 2f			!   goto OP access section
	  nop					! .

	cmp	%o0, EI_REG_CMP_LOAD		! if OP flag bit set
	bz,a	%icc, 3f			!   goto LOAD access section
	  nop					! .

	/*
	 * Invoke the error via a STORE (default)
	 */
	stub	%i0, [%o1]
	stuw	%i1, [%o1 + 0x08]
	st	%i2, [%o1 + 0x10]
	stx	%i3, [%o1 + 0x18]

	stub	%i4, [%o1 + 0x20]
	stuw	%i5, [%o1 + 0x28]
	st	%i6, [%o1 + 0x30]
	stx	%i7, [%o1 + 0x38]

	ba	4f				! exit
	  nop
2:
	/*
	 * Invoke the error via an OP (operation).
	 */
	or	%i0, 0x5a, %g0
	sllx	%i1, 0x4, %g0
	add	%i2, 0x66, %g0
	cmp	%i3, %g0

	or	%i4, 0x5a, %g0
	sllx	%i5, 0x4, %g0
	add	%i6, 0x66, %g0
	cmp	%i7, %g0

	ba	4f				! exit
	  nop
3:
	/*
	 * Don't invoke the error via a LOAD (load will not trigger error),
	 * ldx instruction is used so this does not clobber the reg contents.
	 */
	ldx	[%o1], %i0
	ldx	[%o1 + 0x08], %i1
	ldx	[%o1 + 0x10], %i2
	ldx	[%o1 + 0x18], %i3

	ldx	[%o1 + 0x20], %i4
	ldx	[%o1 + 0x28], %i5
	ldx	[%o1 + 0x30], %i6
	ldx	[%o1 + 0x38], %i7
4:
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_inj_ireg_hvfile_in)
#endif	/* lint */

/*
 * This routine is used to corrupt the ecc of the integer register file
 * using the per core error injection register.  Because a single error
 * injection register is shared by all strands this routine parks the
 * sibling strands during the error injection in order to produce
 * deterministic results.
 *
 * The method used is similar to the above n2_inj_ireg_hvfile_global() routine,
 * but this routine has the ability to inject an error into any of the
 * eight "local" registers.  Injection into other register sets are handled
 * by separate routines.
 */

/*
 * Prototype:
 *
 * int n2_inj_ireg_hvfile_local(uint64_t buf_paddr, uint64_t enable,
 *				uint64_t eccmask, uint64_t offset);
 *
 * Register usage:
 *
 *	%o0 - holds extra flags from top half of enable (not an inpar)
 *	%o1 - paddr of mem scratch buffer
 *	%o2 - enable bit and single shot, and NOERR (clobbered)
 *	%o3 - ecc mask
 *	%o4 - pc relative offset for register access
 *	%o5 - anchor pc used for branches
 *	%g1 - unused
 *	%g2 - unused
 *	%g3 - temp for park/unpark
 *	%g4 - temp for park/unpark
 *	%g5 - temp for park/unpark
 *	%g6 - temp for park/unpark
 *	%g7 - temp for park/unpark
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_ireg_hvfile_local(uint64_t buf_paddr, uint64_t enable, uint64_t eccmask,
				uint64_t offset)
{
	return 0;
}
#else
	.align	512
	ENTRY_NP(n2_inj_ireg_hvfile_local)

	/*
	 * Save all the local registers in the scratch buffer so that the load
	 * access section will not clobber them (they belong to the caller).
	 */
	stx	%l0, [%o1]
	stx	%l1, [%o1 + 0x08]
	stx	%l2, [%o1 + 0x10]
	stx	%l3, [%o1 + 0x18]

	stx	%l4, [%o1 + 0x20]
	stx	%l5, [%o1 + 0x28]
	stx	%l6, [%o1 + 0x30]
	stx	%l7, [%o1 + 0x38]

	/*
	 * Park all sibling strands via the CORE_RUNNING regs so that the
	 * injection will only occur on this strand.
	 */
	N2_PARK_SIBLING_STRANDS(%g7, %g6, %g5, %g4, %g3)

	/*
	 * The number of instructions after the read of the %pc must
	 * remain constant (based on N2_IRF_ERR_STRIDE) and match the
	 * number of instructions in each group below (currently 8).
	 */
	rd	%pc, %o5			! %o5 = current pc

	srlx	%o2, 32, %o0			! %o0 = access flags
	add	%o2, %o3, %o2			! combine mask and enable
	nop
	nop
	nop

	jmp	%o5 + %o4			! jump ahead to reg access
	  nop

	/*
	 * Start of the specific register access section.
	 *
	 * reg %l0 = offset 32 (8 instrs * 1)
	 */
	stx	%l0, [%o1]			! save the contents of reg
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	ldx	[%o1], %l0			! inject error via "load to"
	membar	#Sync				! required
	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg
	ba	1f
	  nop

	/* reg %l1 = offset 64 (8 instrs * 2) */
	stx	%l1, [%o1 + 8]			! save the contents of reg
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	ldx	[%o1 + 8], %l1			! inject error via "load to"
	membar	#Sync				! required
	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg
	ba	1f
	  nop

	/* reg %l2 = offset 96 (8 instrs * 3) */
	stx	%l2, [%o1 + 0x10]		! save the contents of reg
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	ldx	[%o1 + 0x10], %l2		! inject error via "load to"
	membar	#Sync				! required
	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg
	ba	1f
	  nop

	/* reg %l3 = offset 128 (8 instrs * 4) */
	stx	%l3, [%o1 + 0x18]		! save the contents of reg
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	ldx	[%o1 + 0x18], %l3		! inject error via "load to"
	membar	#Sync				! required
	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg
	ba	1f
	  nop

	/* reg %l4 = offset 160 (8 instrs * 5) */
	stx	%l4, [%o1 + 0x20]		! save the contents of reg
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	ldx	[%o1 + 0x20], %l4		! inject error via "load to"
	membar	#Sync				! required
	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg
	ba	1f
	  nop

	/* reg %l5 = offset 192 (8 instrs * 6) */
	stx	%l5, [%o1 + 0x28]		! save the contents of reg
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	ldx	[%o1 + 0x28], %l5		! inject error via "load to"
	membar	#Sync				! required
	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg
	ba	1f
	  nop

	/* reg %l6 = offset 224 (8 instrs * 7) */
	stx	%l6, [%o1 + 0x30]		! save the contents of reg
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	ldx	[%o1 + 0x30], %l6		! inject error via "load to"
	membar	#Sync				! required
	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg
	ba	1f
	  nop

	/* reg %l7 = offset 256 (8 instrs * 8) */
	stx	%l7, [%o1 + 0x38]		! save the contents of reg
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	ldx	[%o1 + 0x38], %l7		! inject error via "load to"
	membar	#Sync				! required
	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg
1:
	/*
	 * Unpark the sibling cores that were parked above, before the access.
	 */
	N2_UNPARK_SIBLING_STRANDS(%g7, %g6, %g4, %g3)

	/*
	 * Perform the access right here in HV mode, the access type is
	 * based on the value placed in the top half of the enable value.
	 */
	cmp	%o0, EI_REG_CMP_NOERR		! if NOERR flag bit set
	bz,a	%icc, 4f			!   skip access section
	  nop					! .

	cmp	%o0, EI_REG_CMP_OP		! if OP flag bit set
	bz,a	%icc, 2f			!   goto OP access section
	  nop					! .

	cmp	%o0, EI_REG_CMP_LOAD		! if OP flag bit set
	bz,a	%icc, 3f			!   goto LOAD access section
	  nop					! .

	/*
	 * Invoke the error via a STORE (default)
	 */
	stub	%l0, [%o1]
	stuw	%l1, [%o1 + 0x08]
	st	%l2, [%o1 + 0x10]
	stx	%l3, [%o1 + 0x18]

	stub	%l4, [%o1 + 0x20]
	stuw	%l5, [%o1 + 0x28]
	st	%l6, [%o1 + 0x30]
	stx	%l7, [%o1 + 0x38]

	ba	4f				! exit
	  nop
2:
	/*
	 * Invoke the error via an OP (operation).
	 */
	or	%l0, 0x5a, %g0
	sllx	%l1, 0x4, %g0
	add	%l2, 0x66, %g0
	cmp	%l3, %g0

	or	%l4, 0x5a, %g0
	sllx	%l5, 0x4, %g0
	add	%l6, 0x66, %g0
	cmp	%l7, %g0

	ba	4f				! exit
	  nop
3:
	/*
	 * Don't invoke the error via a LOAD (load will not trigger error).
	 * ldx instruction is used so this does not clobber the reg contents.
	 */
	ldx	[%o1], %l0
	ldx	[%o1 + 0x08], %l1
	ldx	[%o1 + 0x10], %l2
	ldx	[%o1 + 0x18], %l3

	ldx	[%o1 + 0x20], %l4
	ldx	[%o1 + 0x28], %l5
	ldx	[%o1 + 0x30], %l6
	ldx	[%o1 + 0x38], %l7
4:
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_inj_ireg_hvfile_local)
#endif	/* lint */

/*
 * This routine is used to corrupt the ecc of the integer register file
 * using the per core error injection register.  Because a single error
 * injection register is shared by all strands this routine parks the
 * sibling strands during the error injection in order to produce
 * deterministic results.
 *
 * The method used is similar to the above n2_inj_ireg_hvfile_global() routine,
 * but this routine has the ability to inject an error into any of the
 * eight "out" registers.  Injection into other register sets are handled
 * by separate routines.  Note that this routine is structured a little
 * differently than the others because it's using the out registers.
 */

/*
 * Prototype:
 *
 * int n2_inj_ireg_hvfile_out(uint64_t buf_paddr, uint64_t enable,
 *				uint64_t eccmask, uint64_t offset);
 *
 * Register usage:
 *
 *	%o1 - paddr of mem scratch buffer
 *	%o2 - enable bit, and NOERR
 *	%o3 - ecc mask
 *	%o4 - pc relative offset for register access
 *	%o5 - anchor pc used for branches
 *	%g1 - copy of %o1
 *	%g2 - combined enable bit and ecc mask
 *	%g3 - temp for park/unpark
 *	%g4 - temp for park/unpark
 *	%g5 - temp for park/unpark AND holds flags from top half of enable
 *	%g6 - temp for park/unpark
 *	%g7 - temp for park/unpark
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_ireg_hvfile_out(uint64_t buf_paddr, uint64_t enable, uint64_t eccmask,
				uint64_t offset)
{
	return 0;
}
#else
	.align	512
	ENTRY_NP(n2_inj_ireg_hvfile_out)

	/*
	 * Save the out registers in the scratch buffer so that the load
	 * access section will not clobber them, though most are volatile
	 * across the hvcall, the system will use %o6 and %o7.
	 *
	 * Note %o0 is being set to zero so this routine returns a PASS.
	 */
	stx	%g0, [%o1]
	stx	%o1, [%o1 + 0x08]
	stx	%o2, [%o1 + 0x10]
	stx	%o3, [%o1 + 0x18]

	stx	%o4, [%o1 + 0x20]
	stx	%o5, [%o1 + 0x28]
	stx	%o6, [%o1 + 0x30]
	stx	%o7, [%o1 + 0x38]

	/*
	 * Park all sibling strands via the CORE_RUNNING regs so that the
	 * injection will only occur on this strand.
	 */
	N2_PARK_SIBLING_STRANDS(%g7, %g6, %g5, %g4, %g3)

	/*
	 * The number of instructions after the read of the %pc must
	 * remain constant (based on N2_IRF_ERR_STRIDE) and match the
	 * number of instructions in each group below (currently 8).
	 */
	rd	%pc, %g6			! %g6 = current pc

	srlx	%o2, 32, %g5			! %g5 = access flags
	add	%o2, %o3, %g2			! %g2 = combined mask and enable
	mov	%o1, %g1			! %g1 = scratch buffer address
	nop					!   so %o1 can be a target
	nop

	jmp	%g6 + %o4			! jump ahead to reg access
	  mov	%g0, %o0			! return PASS

	/*
	 * Start of the specific register access section.
	 *
	 * reg %o0 = offset 32 (8 instrs * 1)
	 */
	stx	%o0, [%g1]			! save the contents of reg
	membar	#Sync				! required
	stxa	%g2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	ldx	[%g1], %o0			! inject error via "load to"
	membar	#Sync				! required
	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg
	ba	1f
	  nop

	/* reg %o1 = offset 64 (8 instrs * 2) */
	stx	%o1, [%g1 + 8]			! save the contents of reg
	membar	#Sync				! required
	stxa	%g2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	ldx	[%g1 + 8], %o1			! inject error via "load to"
	membar	#Sync				! required
	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg
	ba	1f
	  nop

	/* reg %o2 = offset 96 (8 instrs * 3) */
	stx	%o2, [%g1 + 0x10]		! save the contents of reg
	membar	#Sync				! required
	stxa	%g2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	ldx	[%g1 + 0x10], %o2		! inject error via "load to"
	membar	#Sync				! required
	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg
	ba	1f
	  nop

	/* reg %o3 = offset 128 (8 instrs * 4) */
	stx	%o3, [%g1 + 0x18]		! save the contents of reg
	membar	#Sync				! required
	stxa	%g2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	ldx	[%g1 + 0x18], %o3		! inject error via "load to"
	membar	#Sync				! required
	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg
	ba	1f
	  nop

	/* reg %o4 = offset 160 (8 instrs * 5) */
	stx	%o4, [%g1 + 0x20]		! save the contents of reg
	membar	#Sync				! required
	stxa	%g2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	ldx	[%g1 + 0x20], %o4		! inject error via "load to"
	membar	#Sync				! required
	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg
	ba	1f
	  nop

	/* reg %o5 = offset 192 (8 instrs * 6) */
	stx	%o5, [%g1 + 0x28]		! save the contents of reg
	membar	#Sync				! required
	stxa	%g2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	ldx	[%g1 + 0x28], %o5		! inject error via "load to"
	membar	#Sync				! required
	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg
	ba	1f
	  nop

	/* reg %o6 = offset 224 (8 instrs * 7) */
	stx	%o6, [%g1 + 0x30]		! save the contents of reg
	membar	#Sync				! required
	stxa	%g2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	ldx	[%g1 + 0x30], %o6		! inject error via "load to"
	membar	#Sync				! required
	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg
	ba	1f
	  nop

	/* reg %o7 = offset 256 (8 instrs * 8) */
	stx	%o7, [%g1 + 0x38]		! save the contents of reg
	membar	#Sync				! required
	stxa	%g2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	ldx	[%g1 + 0x38], %o7		! inject error via "load to"
	membar	#Sync				! required
	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg
1:
	/*
	 * Unpark the sibling cores that were parked above, before the access.
	 */
	N2_UNPARK_SIBLING_STRANDS(%g7, %g6, %g4, %g3)

	/*
	 * Perform the access right here in HV mode, the access type is
	 * based on the value placed in the top half of the enable value.
	 */
	cmp	%g5, EI_REG_CMP_NOERR		! if NOERR flag bit set
	bz,a	%icc, 4f			!   skip access section
	  nop					! .

	cmp	%g5, EI_REG_CMP_OP		! if OP flag bit set
	bz,a	%icc, 2f			!   goto OP access section
	  nop					! .

	cmp	%g5, EI_REG_CMP_LOAD		! if OP flag bit set
	bz,a	%icc, 3f			!   goto LOAD access section
	  nop					! .

	/*
	 * Invoke the error via a STORE (default)
	 */
	stub	%o0, [%g1]
	stuw	%o1, [%g1 + 0x08]
	st	%o2, [%g1 + 0x10]
	stx	%o3, [%g1 + 0x18]

	stub	%o4, [%g1 + 0x20]
	stuw	%o5, [%g1 + 0x28]
	st	%o6, [%g1 + 0x30]
	stx	%o7, [%g1 + 0x38]

	done					! exit
2:
	/*
	 * Invoke the error via an OP (operation).
	 */
	or	%o0, 0x5a, %g0
	sllx	%o1, 0x4, %g0
	add	%o2, 0x66, %g0
	cmp	%o3, %g0

	or	%o4, 0x5a, %g0
	sllx	%o5, 0x4, %g0
	add	%o6, 0x66, %g0
	cmp	%o7, %g0

	done					! exit
3:
	/*
	 * Don't invoke the error via a LOAD (load will not trigger error).
	 * ldx instruction is used so this does not clobber the reg contents.
	 */
	ldx	[%g1], %o0
	ldx	[%g1 + 0x08], %o1
	ldx	[%g1 + 0x10], %o2
	ldx	[%g1 + 0x18], %o3

	ldx	[%g1 + 0x20], %o4
	ldx	[%g1 + 0x28], %o5
	ldx	[%g1 + 0x30], %o6
	ldx	[%g1 + 0x38], %o7
4:
	done					! exit
	SET_SIZE(n2_inj_ireg_hvfile_out)
#endif	/* lint */

/*
 * This routine is used to corrupt the ecc of the floating-point register
 * file using the per core error injection register.  Because a single error
 * injection register is shared by all strands only a single strand should
 * be active for this routine to produce deterministic results.
 *
 * The method used is similar to the above n2_inj_ireg_file() routine.
 *
 * The instrs to use for the floating point ops are:
 *	ld	%f0 - %f31	! 32 bit ops
 *	ldd	%f0 - %f62	! 64 bit ops (aliased to even fregs, are 32)
 *	ldq	%f0 - %f60	! 128 bit ops (aliased to 4th freg, are 16)
 *
 * NOTE: Niagara-I emulated some floating point instructions in SW, these
 *	 are now handled natively by Niagara-II.  Also Niagara-I had only
 *	 one shared FPU per processor, Niagara-II has an FPU per core.
 * 
 * XXX	currently blowing away random floating point regs
 *	which may affect other processes if they are being used.
 */

/*
 * Prototype:
 *
 * int n2_inj_freg_file(uint64_t buf_paddr, uint64_t enable, uint64_t eccmask,
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
 *	%g6 - holds single-shot flag
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_freg_file(uint64_t buf_paddr, uint64_t enable, uint64_t eccmask,
				uint64_t offset)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(n2_inj_freg_file)

	/*
	 * Enable the floating point unit.
	 */
	rdpr	%pstate, %g5			! save the processor state
	or	%g5, PSTATE_PEF, %g4		! and enable the FPU
	wrpr	%g4, %g0, %pstate		! .

	rd	%fprs, %g4			! save the fp state
	or	%g4, FPRS_FEF, %g3		! and enable the FPU
	wr	%g0, %g3, %fprs			! .

	srlx	%o2, EI_REG_SSHOT_ENB_SHIFT, %g6 ! %g6 = SSHOT flag
	and	%g6, 1, %g6			! .

	/*
	 * The number of instructions after the read of the %pc must
	 * remain constant (at five) otherwise the offsets used below
	 * need to be changed.
	 */
	rd	%pc, %g1			! find current pc
	membar	#Sync				! required
	stxa	%o3, [%g0]ASI_INJECT_ERROR_REG	! set the eccmask bits
	add	%o2, %o3, %o2			! combine mask and enable
	jmp	%g1 + %o4			! jump ahead to reg access
	  nop

	/*
	 * Start of the specific register access section.
	 *
	 * XXX will have to decide how many different registers to
	 * provide access points (offsets) for in the finished version.
	 */
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	ld	[%o1], %f0			! %f0 - offset 24
	nop					! to keep code aligned
	ba	1f
	  nop

	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	ldd	[%o1], %f32			! %f32 - offset 48
	nop					! to keep code aligned
	ba	1f
	  nop

	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	ldq	[%o1], %f56			! %f56 - offset 72
	nop					! to keep code aligned
	ba	1f
	  nop
1:
	/*
	 * If single shot mode was specified, then clear the injection reg.
	 */
	membar	#Sync				! required
	brnz,a	%g6, 2f				! check if sshot mode set
	  stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	!   if so clear injection reg
2:
	membar	#Sync				! required
	wr	%g4, %g0, %fprs			! restore the fprs
	wrpr	%g5, %pstate			! restore the processor state
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_inj_freg_file)
#endif	/* lint */

/*
 * This routine uses the per core ASI_INJECT_ERROR_REG (0x43)) to inject a
 * data TLB data or tag parity error for a privileged or user mode error.
 *
 * Because a single data TLB (of 128 entries, where N1 had 64) and error
 * injection register are shared by all strands on a core only a single
 * strand should be active for this routine to produce deterministic results.
 *
 * TLB errors are handled and corrected by the hypervisor. These errors are
 * not possible when executing in the hypervisor unless hypervisor uses
 * ASI_REAL.  These errors send a corrected resumable error to the sun4v
 * guest which triggered the error.
 *
 * NOTE: from PRM "a 3-bit PID (partition ID) field is included in each TLB
 *	 entry.", this is checked even if the real bit set.  It is used
 *	 as-is (it is not checked or set) by the routines below.
 *
 * NOTE: the KT PRM mentions that the injection register is post-synchronizing
 *	 but not pre-synchronizing.  Ensure that "membar #Sync"s remain
 *	 before any use of the register for any required state updates.
 *
 * NOTE: the bit formerly used for sw locking of TLB entries (61) is used by
 *	 the KT/RF chip to store parity.  So it should not be used.
 *	 The code below only uses this bit for non-priv mode injections
 *	 which are not supported on KT anyway.
 */

/*
 * Prototype:
 *
 * int n2_inj_dtlb(uint64_t paddr, uint64_t vaddr, uint64_t sshot_enb,
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
 *	%g6 - offset for the tablewalk pending register
 *	%g7 - holds single-shot flag (SW use only)
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_dtlb(uint64_t paddr, uint64_t vaddr, uint64_t sshot_enb,
				uint32_t ctxnum)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(n2_inj_dtlb)

	/*
	 * Flush the dtlb (not required).
	 */
!	set	(N2_MMU_DEMAP_ALL << 6), %g1
!	stxa	%g0, [%g1]ASI_DMMU_DEMAP

	/*
	 * Build the tag (for tag access reg) for the TLB load.
	 * Note that context = 0 = KCONTEXT.
	 */
	set	((1 << 13) - 1), %g2		! VA/ctx mask for tag access
	and	%o4, %g2, %o4			! mask ctx
	andn	%o2, %g2, %o2			! mask VA
	or	%o2, %o4, %g2			! %g2 = complete tag
	mov	MMU_TAG_ACCESS, %g1		! %g1 = tag access reg

	srlx	%o2, EI_REG_SSHOT_ENB_SHIFT, %g7 ! %g7 = SSHOT flag
	and	%g7, 1, %g7			! .

	/*
	 * Build sun4v format tte - valid, cp, write, (priv or lock), size = 8k.
	 * Note that this code assumes that VA bits [63:40] are zero.
	 */
	mov	1, %g3				! set valid bit
	sllx	%g3, 63, %g5			! .
	sllx	%g3, NI_TTE4V_L_SHIFT, %g3	! and lock bit for later
	or	%g5, TTE4V_CP | TTE4V_CV | TTE4V_W, %g5
	brz,a	%o4, 1f				! if ctx==0 set priv bit
	  or	%g5, TTE4V_P, %g5		!   else set lock bit
	or	%g5, %g3, %g5			! .
1:
	srlx	%o1, 13, %g4			! shift paddr to clear lower
	sllx	%g4, 13, %g4			!   13 bits for tte
	or	%g5, %g4, %g5			! %g5 = complete tte

	/*
	 * Use data-in register virtual translation to load TLB.
	 */
	mov	MMU_TBLWLK_CTL, %g6		! offset for tablewalk reg
	mov	1, %g3				! set tablewalk pending reg
	stxa	%g3, [%g6]ASI_PEND_TBLWLK	! .

	stxa	%g2, [%g1]ASI_DMMU		! load the tag access reg
	membar	#Sync				! required before inject

	stxa	%o3, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	membar	#Sync				!   and options

	stxa	%g5, [%g0]ASI_DTLB_DATA_IN	! do TLB load
	membar	#Sync				! .

	/*
	 * If single shot mode was specified, then clear the injection reg.
	 */
	brnz,a	%g7, 2f				! check if sshot mode set
	  stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	!   if so clear injection reg
2:
	stxa	%g0, [%g6]ASI_PEND_TBLWLK	! clear tablewalk pending reg
	mov	%g5, %o1			! return built tte as value
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_inj_dtlb)
#endif	/* lint */

/*
 * This routine uses the per core ASI_INJECT_ERROR_REG to inject a data
 * TLB data or tag parity error for a privileged or user mode error.
 *
 * The method used is similar to the above n2_inj_dtlb() routine, see the
 * comments (and warnings) in the above header.
 *
 * The difference between this routine and the one above is that this one
 * uses a tte which is built by the kernel routines and passed in directly.
 * Also this routine assumes kernel context (KCONTEXT = 0).
 */

/*
 * Prototype:
 *
 * int n2_inj_dtlb_v(uint64_t paddr, uint64_t vaddr, uint64_t sshot_enb,
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
 *	%g4 - unsued
 *	%g5 - unused
 *	%g6 - temp then offset for the tablewalk pending register
 *	%g7 - holds single-shot flag
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_dtlb_v(uint64_t paddr, uint64_t vaddr, uint64_t sshot_enb,
				uint64_t tte)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(n2_inj_dtlb_v)

	/*
	 * Flush the dtlb (or don't).
	 */
!	set	(N2_MMU_DEMAP_ALL << 6), %g1
!	stxa	%g0, [%g1]ASI_DMMU_DEMAP

	/*
	 * Build the tag (for tag access reg) for the TLB load.
	 * Note that context = 0 = KCONTEXT.
	 */
	set	((1 << 13) - 1), %g2		! VA/ctx mask for tag access
	andn	%o2, %g2, %g2			! %g2 = tag (ctx = 0)
	mov	MMU_TAG_ACCESS, %g1		! %g1 = tag access reg

	srlx	%o3, EI_REG_SSHOT_ENB_SHIFT, %g7 ! %g7 = SSHOT flag
	and	%g7, 1, %g7			! .

	/*
	 * Use sun4v format tte (built by kernel routine), note that
	 * the kernel uses an RA instead of a PA which must be swapped
	 * out in the C routine.
	 */
!	mov	1, %g6				! clear sun4v tte lock bit
!	sllx	%g6, NI_TTE4V_L_SHIFT, %g6	! (or don't)
!	andn	%o4, %g6, %o4			! %o4 = tte (no lock bit)

	/*
	 * Use data-in register virtual translation to load TLB.
	 */
	mov	MMU_TBLWLK_CTL, %g6		! offset for tablewalk reg
	mov	1, %g3				! set tablewalk pending reg
	stxa	%g3, [%g6]ASI_PEND_TBLWLK	! .

	stxa	%g2, [%g1]ASI_DMMU		! load the tag access reg
	membar	#Sync				! required before inject

	stxa	%o3, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	membar	#Sync				!   and options

	stxa	%o4, [%g0]ASI_DTLB_DATA_IN	! do TLB load
	membar	#Sync				! .

	/*
	 * If single shot mode was specified, then clear the injection reg.
	 */
	brnz,a	%g7, 1f				! check if sshot mode set
	  stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	!   if so clear injection reg
1:
	stxa	%g0, [%g6]ASI_PEND_TBLWLK	! clear tablewalk pending reg
	mov	%g2, %o1			! put tag into return value
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_inj_dtlb_v)
#endif	/* lint */

/*
 * This routine is used to place multiple copies of the same tte mapping
 * into the D-TLB using a tte that is passed in from the calling routine.
 *
 * From the PRM: "The auto-demap feature of the TLB, described in
 * Section 12.10.15, prevents translations with identical page sizes, VA,
 * and context from existing in the TLB simultaneously. However, software
 * can generate multiple matches by inserting overlapping translations of
 * differing page sizes, or by inserting translations that differ only in
 * context and then programming the context 0 and context 1 registers to
 * match the pair of translations."
 *
 * What this routines does specifically is load tte's of sizes 4M, 64k and
 * 8k with overlapping tag VAs which are the original 8k aligned address
 * and the same address aligned to 64k then 4M.
 *
 * NOTE: this routine really should check if the paddr and/or vaddr passed
 * in are already 4M aligned b/c then this code will not work since the ttes
 * will get demapped.  Since this case is rare (and hard to get around) it
 * is not handled.
 *
 * The smallest mapping must be loaded first because the auto-demap will
 * remove any entries that are the same size or larger from the TLB.
 * Note that the sizes are:
 *	4M  = 0x40.0000
 *	64k =  0x1.0000
 *	8k  =    0x2000
 *
 * This routine assumes kernel context (KCONTEXT = 0).
 */

/*
 * Prototype:
 *
 * int n2_inj_dtlb_mult(uint64_t paddr, uint64_t vaddr, uint64_t *debug,
 *				uint64_t tte);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - kvaddr or uvaddr
 *	%o3 - pointer to debug buffer (debug)
 *	%o4 - sun4v format tte
 *	%g1 - MMU_TAG_ACCESS VA then temp
 *	%g2 - temp then tag for DATA_IN
 *	%g3 - temp
 *	%g4 - used for debug
 *	%g5 - used for debug
 *	%g6 - temp then offset for the tablewalk pending register
 *	%g7 - used as offset because of debug
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_dtlb_mult(uint64_t paddr, uint64_t vaddr, uint64_t *debug,
					uint64_t tte)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(n2_inj_dtlb_mult)

	/*
	 * Flush the dtlb (no don't do this as it can cause TLB miss panics).
	 */
!	set	(N2_MMU_DEMAP_ALL << 6), %g1
!	stxa	%g0, [%g1]ASI_DMMU_DEMAP

	/*
	 * Build the tag (for tag access reg) for the TLB load.
	 * Note that context = 0 = KCONTEXT.
	 */
	set	((1 << 13) - 1), %g2		! VA/ctx mask for tag access
	andn	%o2, %g2, %g2			! %g2 = first tag VA (ctx = 0)

	/*
	 * Use sun4v format tte (built by kernel routine), note that
	 * the kernel uses an RA instead of a PA which must be swapped
	 * out in the C routine.
	 */
!	mov	1, %g6				! clear sun4v tte lock bit
!	sllx	%g6, NI_TTE4V_L_SHIFT, %g6	! (or don't)
!	andn	%o4, %g6, %o4			! .
	mov	1, %g6				! force valid bit to be set
	sllx	%g6, 63, %g6			! .
	or	%o4, %g6, %o4			! .
	andn	%o4, TTE4V_SZ_MASK, %o4		! %o4 = tte (no lock/sz bits)

	/*
	 * So that the tte address regions overlap and so the smallest
	 * pagesize tte goes in first (required) place the 8k mapping
	 * at the original paddr (in EI buffer) then just align the other
	 * mappings so they will be different (will not get removed).
	 */

	/*
	 * Hold the MMU tablewalks.
	 */
	mov	MMU_TBLWLK_CTL, %g6		! offset for tablewalk reg
	mov	1, %g3				! set tablewalk pending reg
	stxa	%g3, [%g6]ASI_PEND_TBLWLK	! .

	/*
	 * Use data-in register virtual translation to load the ttes.
	 */
	mov	MMU_TAG_ACCESS, %g1		! %g1 = tag access reg
	stxa	%g2, [%g1]ASI_DMMU		! load the tag access reg
	membar	#Sync				! .

	stxa	%o4, [%g0]ASI_DTLB_DATA_IN	! do first TLB load (8k=0)
	membar	#Sync				! .

	/*
	 * Insert another 8k page, but align the tte PA and the tag VA to
	 * a 64k boundary.  This one should be demapped when the 64k tte
	 * is installed (next operation).
	 */
	mov	0x7, %g7			! align PA in tte to 64k
	sllx	%g7, 13, %g7			! .
	andn	%o4, %g7, %o4			! .
	andn	%g2, %g7, %g2			! also align the tag VA
	stxa	%g2, [%g1]ASI_DMMU		! reload value to the tag
	membar	#Sync				! .
	stxa	%o4, [%g0]ASI_DTLB_DATA_IN	! do TLB load
	membar	#Sync				! .

	or	%o4, TTE4V_SZ_64K, %o4		! set size bits to 64k (0x1)
	mov	0x7, %g7			! align PA in tte to 64k
	sllx	%g7, 13, %g7			! .
	andn	%o4, %g7, %o4			! .
	andn	%g2, %g7, %g2			! also align the tag VA
	stxa	%g2, [%g1]ASI_DMMU		! reload value to the tag
	membar	#Sync				! .
	stxa	%o4, [%g0]ASI_DTLB_DATA_IN	! do TLB load
	membar	#Sync				! .

	andn	%o4, TTE4V_SZ_MASK, %o4		! set size bits to 4M (0x3)
	or	%o4, TTE4V_SZ_4M, %o4		! .
	mov	0x1ff, %g7			! align PA in tte to 4M
	sllx	%g7, 13, %g7			! .
	andn	%o4, %g7, %o4			! .
	andn	%g2, %g7, %g2			! also align the tag VA
	stxa	%g2, [%g1]ASI_DMMU		! reload value to the tag
	membar	#Sync				! .
	stxa	%o4, [%g0]ASI_DTLB_DATA_IN	! do TLB load
	membar	#Sync				! .

#ifdef	L2_DEBUG_BUFFER
	/*
	 * Dump out TLB entries that match the vaddr that is used above
	 * to see if they really are in the TLB.
	 */
	mov	%o3, %g1			! start with debug buf entry 0
	stx	%g2, [%g1]			! store tag VA to debug buf 0
	srlx	%g2, 22, %g2			! shift out low bits for cmp
	sllx	%g2, 22, %g2			! .

	mov	%g0, %g4			! start with TLB entry 0
1:
	ldxa	[%g4]ASI_DTLB_TAG, %g5		! read tag at this index
	srlx	%g5, 22, %g5			! shift out high and low bits
	sllx	%g5, 16 + 22, %g5		! .
	srlx	%g5, 16, %g5			! .
	stx	%g5, [%g1 + 0x80]		! store last tag to debug buf
	cmp	%g5, %g2			! does this tag VA match ours?
	bnz	%xcc, 2f			! if so save the tte
	  nop			
	add	%g1, 0x8, %g1			! inc the debug buf entry
	ldxa	[%g4]ASI_DTLB_DATA_ACC, %g5	! read the tte (data)
	stx	%g5, [%g1]			! store tte to debug buf
2:
	cmp	%g4, 0x3e8			! is this the last TLB entry?
	bnz	1b				! if not keep going
	  add	%g4, 0x8, %g4			!   inc TLB entry
#endif

	stxa	%g0, [%g6]ASI_PEND_TBLWLK	! clear tablewalk pending reg
	mov	%g2, %o1			! put tag into return value
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_inj_dtlb_mult)
#endif	/* lint */

/*
 * This routine uses the per core ASI_INJECT_ERROR_REG to inject an instruction 
 * TLB data or tag parity error for a privileged or user mode error.
 *
 * Because a single instruction TLB (of 64 entries) and error injection
 * register are shared by all strands on a core only a single strand should
 * be active for this routine to produce deterministic results.
 *
 * The method used is similar to the above n2_inj_dtlb() routine, see the
 * comments (and warnings) in the above header.
 */

/*
 * Prototype:
 *
 * int n2_inj_itlb(uint64_t paddr, uint64_t vaddr, uint64_t sshot_enb,
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
 *	%g6 - offset for the tablewalk pending register
 *	%g7 - holds single-shot flag
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_itlb(uint64_t paddr, uint64_t vaddr, uint64_t sshot_enb,
						uint32_t ctxnum)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(n2_inj_itlb)

	/*
	 * Flush the itlb (or don't).
	 */
!	set	(N2_MMU_DEMAP_ALL << 6), %g1
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

	srlx	%o2, EI_REG_SSHOT_ENB_SHIFT, %g7 ! %g7 = SSHOT flag
	and	%g7, 1, %g7			! .

	/*
	 * Build sun4v format tte - valid, cp, (priv or lock), size = 64k.
	 * Note that this code assumes that VA bits [63:40] are zero.
	 */
	mov	1, %g3				! set valid bit
	sllx	%g3, 63, %g5			! .
	sllx	%g3, NI_TTE4V_L_SHIFT, %g3	! and lock bit for later
	or	%g5, TTE4V_SZ_64K, %g5		! set size and other bits
	or	%g5, TTE4V_CP | TTE4V_CV, %g5
	brz,a	%o4, 1f				! if ctx==0 set priv bit
	  or	%g5, TTE4V_P, %g5		!   else set lock bit
	or	%g5, %g3, %g5			! .
1:
	srlx	%o1, 13, %g4			! shift paddr to clear lower
	sllx	%g4, 13, %g4			!   13 bits for tte
	or	%g5, %g4, %g5			! %g5 = complete tte

	/*
	 * Use data-in register virtual translation to load TLB.
	 */
	mov	MMU_TBLWLK_CTL, %g6		! offset for tablewalk reg
	mov	1, %g3				! set tablewalk pending reg
	stxa	%g3, [%g6]ASI_PEND_TBLWLK	! .

	stxa	%g2, [%g1]ASI_IMMU		! load the tag access reg
	membar	#Sync				! required before inject

	stxa	%o3, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	membar	#Sync				!   and options

	stxa	%g5, [%g0]ASI_ITLB_DATA_IN	! do TLB load
	membar	#Sync				! .

	/*
	 * If single shot mode was specified, then clear the injection reg.
	 */
	brnz,a	%g7, 2f				! check if sshot mode set
	  stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	!   if so clear injection reg
2:
	stxa	%g0, [%g6]ASI_PEND_TBLWLK	! clear tablewalk pending reg
	mov	%g5, %o1			! return built tte as value
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_inj_itlb)
#endif	/* lint */

/*
 * This routine uses the per core ASI_INJECT_ERROR_REG to inject an instruction
 * TLB data or tag parity error for a privileged or user mode error.
 *
 * The method used is similar to the above n2_inj_dtlb_v() routine, see the
 * comments (and warnings) in the above header.
 */

/*
 * Prototype:
 *
 * int n2_inj_itlb_v(uint64_t paddr, uint64_t vaddr, uint64_t sshot_enb,
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
 *	%g4 - unused
 *	%g5 - unused
 *	%g6 - temp then offset for the tablewalk pending register
 *	%g7 - holds single-shot flag
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_itlb_v(uint64_t paddr, uint64_t vaddr, uint64_t sshot_enb,
					uint64_t tte)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(n2_inj_itlb_v)

	/*
	 * Flush the itlb (or don't).
	 */
!	set	(N2_MMU_DEMAP_ALL << 6), %g1
!	stxa	%g0, [%g1]ASI_IMMU_DEMAP

	/*
	 * Build the tag (for tag access reg) for the TLB load.
	 * Note that context = 0 = KCONTEXT.
	 */
	set	((1 << 13) - 1), %g2		! VA/ctx mask for tag access
	andn	%o2, %g2, %g2			! %g2 = tag (ctx = 0)
	mov	MMU_TAG_ACCESS, %g1		! %g1 = tag access reg

	srlx	%o3, EI_REG_SSHOT_ENB_SHIFT, %g7 ! %g7 = SSHOT flag
	and	%g7, 1, %g7			! .

	/*
	 * Use sun4v format tte (built by kernel routine), note that
	 * the kernel uses an RA instead of a PA which must be swapped
	 * out in the C routine.
	 */
!	mov	1, %g6				! clear sun4v tte lock bit
!	sllx	%g6, NI_TTE4V_L_SHIFT, %g6	! (or don't)
!	andn	%o4, %g6, %o4			! %o4 = tte (no lock bit)

	/*
	 * Use data-in register virtual translation to load TLB.
	 */
	mov	MMU_TBLWLK_CTL, %g6		! offset for tablewalk reg
	mov	1, %g3				! set tablewalk pending reg
	stxa	%g3, [%g6]ASI_PEND_TBLWLK	! .

	stxa	%g2, [%g1]ASI_IMMU		! load the tag access reg
	membar	#Sync				! required before inject

	stxa	%o3, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	membar	#Sync				!   and options

	stxa	%o4, [%g0]ASI_ITLB_DATA_IN	! do TLB load
	membar	#Sync				! .

	/*
	 * If single shot mode was specified, then clear the injection reg.
	 */
	brnz,a	%g7, 1f				! check if sshot mode set
	  stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	!   if so clear injection reg
1:
	stxa	%g0, [%g6]ASI_PEND_TBLWLK	! clear tablewalk pending reg
	mov	%g2, %o1			! put tag into return value
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_inj_itlb_v)
#endif	/* lint */

/*
 * This routine is used to place multiple copies of the same tte mapping
 * into the I-TLB using a tte that is passed in from the calling routine.
 *
 * The method used is similar to the above n2_inj_dtlb_mult() routine, see
 * the comments (and warnings) in the above header.
 *
 * This routine assumes kernel context (KCONTEXT = 0).
 */

/*
 * Prototype:
 *
 * int n2_inj_itlb_mult(uint64_t paddr, uint64_t vaddr, uint64_t *debug,
 *				uint64_t tte);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - kvaddr or uvaddr
 *	%o3 - pointer to debug buffer (debug)
 *	%o4 - sun4v format tte
 *	%g1 - MMU_TAG_ACCESS VA
 *	%g2 - temp then tag for DATA_IN
 *	%g3 - temp
 *	%g4 - unused
 *	%g5 - unused
 *	%g6 - temp then offset for the tablewalk pending register
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_itlb_mult(uint64_t paddr, uint64_t vaddr, uint64_t *debug,
					uint64_t tte)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(n2_inj_itlb_mult)

	/*
	 * Flush the itlb (no don't do this as it can cause TLB miss panics).
	 */
!	set	(N2_MMU_DEMAP_ALL << 6), %g1
!	stxa	%g0, [%g1]ASI_IMMU_DEMAP

	/*
	 * Build the tag (for tag access reg) for the TLB load.
	 * Note that context = 0 = KCONTEXT.
	 */
	set	((1 << 13) - 1), %g2		! VA/ctx mask for tag access
	andn	%o2, %g2, %g2			! %g2 = first tag VA (ctx = 0)

	/*
	 * Use sun4v format tte (built by kernel routine), note that
	 * the kernel uses an RA instead of a PA which must be swapped
	 * out in the C routine.
	 */
	mov	1, %g6				! force valid bit to be set
	sllx	%g6, 63, %g6			! .
	or	%o4, %g6, %o4			! .
	andn	%o4, TTE4V_SZ_MASK, %o4		! %o4 = tte (no lock/sz bits)

	/*
	 * So that the tte address regions overlap and so the smallest
	 * pagesize tte goes in first (required) place the 8k mapping
	 * at the original paddr (in EI buffer) then just align the other
	 * mappings so they will be different (will not get removed).
	 */

	/*
	 * Hold the MMU tablewalks.
	 */
	mov	MMU_TBLWLK_CTL, %g6		! offset for tablewalk reg
	mov	1, %g3				! set tablewalk pending reg
	stxa	%g3, [%g6]ASI_PEND_TBLWLK	! .

	/*
	 * Use data-in register virtual translation to load the ttes.
	 */
	mov	MMU_TAG_ACCESS, %g1		! %g1 = tag access reg
	stxa	%g2, [%g1]ASI_IMMU		! load the tag access reg
	membar	#Sync				! .

	stxa	%o4, [%g0]ASI_ITLB_DATA_IN	! do first TLB load (8k=0)
	membar	#Sync				! .

	/*
	 * Insert another 8k page, but align the tte PA and the tag VA to
	 * a 64k boundary.  This one should be demapped when the 64k tte
	 * is installed (next operation).
	 */
	mov	0x7, %g7			! align PA in tte to 64k
	sllx	%g7, 13, %g7			! .
	andn	%o4, %g7, %o4			! .
	andn	%g2, %g7, %g2			! also align the tag VA
	stxa	%g2, [%g1]ASI_IMMU		! reload value to the tag
	membar	#Sync				! .
	stxa	%o4, [%g0]ASI_ITLB_DATA_IN	! do TLB load
	membar	#Sync				! .

	or	%o4, TTE4V_SZ_64K, %o4		! set size bits to 64k (0x1)
	mov	0x7, %g7			! align PA in tte to 64k
	sllx	%g7, 13, %g7			! .
	andn	%o4, %g7, %o4			! .
	andn	%g2, %g7, %g2			! also align the tag VA
	stxa	%g2, [%g1]ASI_IMMU		! reload value to the tag
	membar	#Sync				! .
	stxa	%o4, [%g0]ASI_ITLB_DATA_IN	! do TLB load
	membar	#Sync				! .

	andn	%o4, TTE4V_SZ_MASK, %o4		! set size bits to 4M (0x3)
	or	%o4, TTE4V_SZ_4M, %o4		! .
	mov	0x1ff, %g7			! align PA in tte to 4M
	sllx	%g7, 13, %g7			! .
	andn	%o4, %g7, %o4			! .
	andn	%g2, %g7, %g2			! also align the tag VA
	stxa	%g2, [%g1]ASI_IMMU		! reload value to the tag
	membar	#Sync				! .
	stxa	%o4, [%g0]ASI_ITLB_DATA_IN	! do TLB load
	membar	#Sync				! .

#ifdef	L2_DEBUG_BUFFER
	/*
	 * Dump out TLB entries that match the vaddr that is used above
	 * to see if they really are in the TLB.
	 */
	mov	%o3, %g1			! start with debug buf entry 0
	stx	%g2, [%g1]			! store tag VA to debug buf 0
	srlx	%g2, 22, %g2			! shift out low bits for cmp
	sllx	%g2, 22, %g2			! .

	mov	%g0, %g4			! start with TLB entry 0
1:
	ldxa	[%g4]ASI_ITLB_TAG, %g5		! read tag at this index
	srlx	%g5, 22, %g5			! shift out high and low bits
	sllx	%g5, 16 + 22, %g5		! .
	srlx	%g5, 16, %g5			! .
	stx	%g5, [%g1 + 0x80]		! store last tag to debug buf
	cmp	%g5, %g2			! does this tag VA match ours?
	bnz	%xcc, 2f			! if so save the tte
	  nop			
	add	%g1, 0x8, %g1			! inc the debug buf entry
	ldxa	[%g4]ASI_ITLB_DATA_ACC, %g5	! read the tte (data)
	stx	%g5, [%g1]			! store tte to debug buf
2:
	cmp	%g4, 0x3e8			! is this the last TLB entry?
	bnz	1b				! if not keep going
	  add	%g4, 0x8, %g4			!   inc TLB entry
#endif

	stxa	%g0, [%g6]ASI_PEND_TBLWLK	! clear tablewalk pending reg
	mov	%g2, %o1			! put tag into return value
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_inj_itlb_mult)
#endif	/* lint */

/*
 * This routine corrupts the parity of an entry in the internal MMU Register
 * array on the Niagara-II processor via the per core ASI_INJECT_ERROR_REG.
 * This error is detected by a HW tablewalk that is triggered by accessing
 * a vaddr (in priv mode) which causes an MMU miss, the miss will use the
 * MMU regs to attempt a lookup in the TSB but will find the error instead.
 *
 * This routine ensures that tablewalks are enabled and that they will begin
 * at the lowest TSB config register.  Since this requires setting MMU config
 * registers, this may put the MMU into a different state than the
 * system expects.  No attempt is made to restore these registers,
 * but since this error is NR this should not be an issue.
 *
 * The registers needed to setup the tablewalks are:
 *	- ASI_HWTW_CONFIG
 *		set it to 0 so that the HWTW begin with TSB reg 0
 *	- MMU TSB Config Register(s)
 *		described in PRM section 12.10.11 (set enable bit)
 */

/*
 * Prototype:
 *
 * int n2_inj_tlb_mmu(uint64_t vaddr, uint64_t enable, uint64_t data_flag,
 *			uint64_t asi_vaddr);
 *
 * Register usage:
 *
 *	%o1 - vaddr
 *	%o2 - enable bit, single-shot, and NOERR flag
 *	%o3 - flag for DATA vs. instruction
 *	%o4 - asi_vaddr (for the ASI access, expects MMU_ZERO_CTX_TSB_CFG_0)
 *	%g1 - value for demap page operation
 *	%g2 - holds single-shot flag
 *	%g3 - temp
 *	%g4 - temp
 *	%g5 - contents of the register used for injection
 *	%g6 - contents (will be modified) of MMU config/status reg
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_tlb_mmu(uint64_t vaddr, uint64_t enable, uint64_t eccmask,
			uint64_t asi_vaddr)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(n2_inj_tlb_mmu)

	srlx	%o2, EI_REG_SSHOT_ENB_SHIFT, %g2 ! %g2 = sshot flag
	and	%g2, 1, %g2			! .

	ldxa	[%o4]ASI_MMU_TSB_CFG, %g5	! %g5 = TSB register contents

	/*
	 * Build address for the demap page operation.
	 */
	set	(N2_MMU_DEMAP_PAGE << 6), %g1	! set demap select
	andn	%o1, 0xfff, %o1			! clear lower 12 bits
	or	%g1, %o1, %g1			! %g1 = demap asi addr

	/*
	 * Ensure HW tablewalks are enabled in all req'd regs.
	 */
	mov	MMU_TBLWLK_CTL, %g3		! clear the HWTW CTL reg
	stxa	%g0, [%g3]ASI_PEND_TBLWLK	!   so walks begin at TSB 0

	mov	1, %g3				! force HWTW to be enabled
	sllx	%g3, 63, %g3			!   in specified TSB reg
	or	%g5, %g3, %g5			! .
	stxa	%g5, [%o4]ASI_MMU_TSB_CFG	! .

	/*
	 * Inject the error via the injection register.
	 */
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	stxa	%g5, [%o4]ASI_MMU_TSB_CFG	! perform write to inject

	/*
	 * If single shot mode was specified, then clear the injection reg.
	 */
	membar	#Sync				! required
	brnz,a	%g2, 1f				! check if sshot mode set
	  stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	!   if so clear injection reg
1:
	/*
	 * Flush the itlb _or_ the dtlb entry (this routine handles both).
	 */
	cmp	%o3, %g0			! if DATA flag != 0
	bnz,a,pn %icc, 2f			!   clear data so it will miss
	  stxa	%g0, [%g1]ASI_DMMU_DEMAP	!   (else clear instr)
	stxa	%g0, [%g1]ASI_IMMU_DEMAP	!   .
2:
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_inj_tlb_mmu)
#endif	/* lint */

/*
 * This routine is used to corrupt the ecc of the internal Scratchpad
 * array on the Niagara-II processor via the per core ASI_INJECT_ERROR_REG.
 *
 * Note that the different internal error types each have their own routines
 * based on this one (except the TLB and register file errors which have more
 * complex routines).
 *
 * Because a single error injection register is shared by all strands only
 * a single strand should be active for this routine to produce deterministic
 * results.
 *
 * ASI_INJECT_ERROR_REG (0x43, VA=0x0)
 *
 * +------+-----+-------+-----+----+-----+-----+-----+------+-----+------+-----+
 * | Rsvd | Enb | Rsvd+ | SCA | TC | TSA | MRA | STA | Rsvd | STD | Rsvd | ECC |
 * +------+-----+-------+-----+----+-----+-----+-----+------+-----+------+-----+
 *   63:32   31   30:24   23    22   21    20    19    28     17    16:8   7:0
 *
 * NOTE: Niagara-I had a single-shot mode which would inject one error only
 *	 and reset the injection mechanism (and clear the enable bit).
 *	 This feature has been removed on Niagara-II and so the code must
 *	 clear the register after the injection if a single error is desired.
 *
 * NOTE: the PRM warns that "Writes to ASI_ERROR_INJECT are actually post-
 *	 synchronizing, so no MEMBAR #Sync is required after the write.
 *	 However, there are cases where writes to ASI_ERROR_INJECT are not
 *	 presynchronizing (that is, an instruction before the write could see
 *	 the effect). To guard against this, software needs to put a MEMBAR
 *	 #Sync before a write to ASI_ERROR_INJECT."
 */

/*
 * Prototype:
 *
 * int n2_inj_sca(uint64_t vaddr, uint64_t enable, uint64_t eccmask);
 *
 * Register usage:
 *
 *	%o1 - vaddr for the asi access (can be 0x0-0x38, step 8)
 *	%o2 - enable bit, single-shot, and NOERR flag
 *	%o3 - ecc mask
 *	%o4 - unused
 *	%g1 - NOERR flag
 *	%g2 - holds single-shot flag
 *	%g3 - holds NOERR flag
 *	%g4 - unused
 *	%g5 - contents of the scratchpad register
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_sca(uint64_t vaddr, uint64_t enable, uint64_t eccmask)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(n2_inj_sca)

	add	%o2, %o3, %o2			! combine mask and enable
	srlx	%o2, EI_REG_SSHOT_ENB_SHIFT, %g2 ! %g2 = sshot flag
	and	%g2, 1, %g2			! .

	srlx	%o2, EI_REG_NOERR_SHIFT, %g3	! %g3 = NOERR access flag
	and	%g3, 1, %g3			! .

	ldxa	[%o1]ASI_HSCRATCHPAD, %g5	! %g5 = contents of scratch reg

	/*
	 * Inject the error via the injection register.
	 */
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	stxa	%g5, [%o1]ASI_HSCRATCHPAD	! perform write to inject

	/*
	 * If single shot mode was specified, then clear the injection reg.
	 */
	membar	#Sync				! required
	brnz,a	%g2, 1f				! check if sshot mode set
	  stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	!   if so clear injection reg
1:
	cmp	%g3, %g0			! if NOERR flag bit set
	bnz	%icc, 2f			!   skip access section
	  nop					! .

	ldxa	[%o1]ASI_HSCRATCHPAD, %g0	! perform load to invoke err
	membar	#Sync				! .
2:
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_inj_sca)
#endif	/* lint */

/*
 * This routine is used to corrupt the ecc of the internal Tick Compare
 * array on the Niagara-II processor via the per core ASI_INJECT_ERROR_REG.
 *
 * The Tick Compare array stores only a few regs, this routine can inject
 * an error(s) into any one of them based on the offset argument:
 *	offset 0 = TICK_CMPR (asr 0x17, PRIV, tick_cmpr)
 *	offset 1 = STICK_CMPR (asr 0x19, PRIV, sys_tick_cmpr)
 *	offset 2 = HSTICK_CMPR (asr 0x1f, HPRIV, hstick_cmpr)
 *
 * Note that Tick Compare array errors can be Precise or Disrupting
 * depending on the type of access that hits the error.
 *	ASR access =	Precise (TCUP, TCCP)
 *	HW access  =	Disrupting (TCUD, TCCD)
 */

/*
 * Prototype:
 *
 * int n2_inj_tca(uint64_t offset, uint64_t enable, uint64_t eccmask);
 *
 * Register usage:
 *
 *	%o1 - offset (used to specify register type)
 *	%o2 - enable bit, single-shot, and NOERR flag
 *	%o3 - ecc mask
 *	%o4 - unused
 *	%g1 - NOERR flag
 *	%g2 - holds single-shot flag
 *	%g3 - holds NOERR flag
 *	%g4 - unused
 *	%g5 - contents of the tick_cmpr register
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_tca(uint64_t offset, uint64_t enable, uint64_t eccmask)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(n2_inj_tca)

	add	%o2, %o3, %o2			! combine mask and enable
	srlx	%o2, EI_REG_SSHOT_ENB_SHIFT, %g2 ! %g2 = sshot flag
	and	%g2, 1, %g2			! .

	srlx	%o2, EI_REG_NOERR_SHIFT, %g3	! %g3 = NOERR access flag
	and	%g3, 1, %g3			! .

	/*
	 * Inject the error via the injection register into the type of
	 * register that is specified by the offset arguemnt.
	 */
	brz	%o1, 1f
	  rd	%tick_cmpr, %g5			! %g5 = tick compare value

	cmp	%o1, 1
	be	2f
	  rd	%sys_tick_cmpr, %g5		! %g5 = sys_tick compare value

	/*
	 * Otherwise inject error(s) into the hstick register.
	 */
	rdhpr	%hstick_cmpr, %g5		! %g5 = tick compare value

	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode

	wrhpr	%g5, %hstick_cmpr		! perform write to inject
	ba	3f				! go to common code
	  nop					! .
1:
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode

	wr	%g5, %g0, %tick_cmpr		! perform write to inject
	ba	3f				! go to common code
	  nop					! .
2:
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode

	wr	%g5, %g0, %sys_tick_cmpr	! perform write to inject
3:
	/*
	 * If single shot mode was specified, then clear the injection reg.
	 */
	membar	#Sync				! required?
	brnz,a	%g2, 4f				! check if sshot mode set
	  stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	!   if so clear injection reg
4:
	/*
	 * NOERR in this case means that the HW will run over the error
	 * in the normal course of it's operation (producing a DIS error).
	 */
	cmp	%g3, %g0			! if NOERR flag bit set
	bnz	%icc, 5f			!   skip access section
	  nop					! .

	rd	%tick_cmpr, %g5			! otherwise perform accesses
	rd	%sys_tick_cmpr, %g5		!  to invoke the error
	rdhpr	%hstick_cmpr, %g5		!  .
	membar	#Sync				!  .
5:
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_inj_tca)
#endif	/* lint */

/*
 * This routine is used to corrupt the ecc of the internal Trap Stack
 * array on the Niagara-II processor via the per core ASI_INJECT_ERROR_REG.
 * The Trap Stack array contains the V9 trap stack registers and the mondo
 * interrupt queue registers.
 *
 * The method used is similar to the above n2_inj_sca() routine.
 *
 * Note that Trap Stack array errors can be detected via priv loads and
 * stores (since writes are read-modify-write) and also on "done" and
 * "retry" instructions.  So even if NOERR is set, this routine will trip
 * the error with the "done" instruction at the end.
 *
 * NOTE: the TSA has 16-bits of ecc but the injection registers ecc field is
 *	 only 8-bits wide.  So not all ecc bits can be accessed.
 */

/*
 * Prototype:
 *
 * int n2_inj_tsa(uint64_t vaddr, uint64_t enable, uint64_t eccmask);
 *
 * Register usage:
 *
 *	%o1 - vaddr (unused)
 *	%o2 - enable bit, single-shot, and NOERR flag
 *	%o3 - ecc mask
 *	%o4 - unused
 *	%g1 - NOERR flag
 *	%g2 - holds single-shot flag
 *	%g3 - holds NOERR flag
 *	%g4 - unused
 *	%g5 - contents of the register used for injection
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_tsa(uint64_t vaddr, uint64_t enable, uint64_t eccmask)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(n2_inj_tsa)

	add	%o2, %o3, %o2			! combine mask and enable
	srlx	%o2, EI_REG_SSHOT_ENB_SHIFT, %g2 ! %g2 = sshot flag
	and	%g2, 1, %g2			! .

	srlx	%o2, EI_REG_NOERR_SHIFT, %g3	! %g3 = NOERR access flag
	and	%g3, 1, %g3			! .

	rdpr	%tstate, %g5			! %g5 = tstate value

	/*
	 * Inject the error via the injection register.
	 */
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	wrpr	%g5, %g0, %tstate		! perform write to inject

	/*
	 * If single shot mode was specified, then clear the injection reg.
	 */
	membar	#Sync				! required
	brnz,a	%g2, 1f				! check if sshot mode set
	  stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	!   if so clear injection reg
1:
	/*
	 * NOERR in this case means that the HW will run over the error
	 * at the "done" instruction below.
	 */
	cmp	%g3, %g0			! if NOERR flag bit set
	bnz	%icc, 2f			!   skip access section
	  nop					! .

	rdpr	%tstate, %g0			! perform load to invoke
	membar	#Sync				! .
2:
	mov	%g0, %o0			! return PASS
	done					! (done will also invoke error)
	SET_SIZE(n2_inj_tsa)
#endif	/* lint */

/*
 * This routine is used to corrupt the parity of the internal MMU Register
 * array on the Niagara-II processor via the per core ASI_INJECT_ERROR_REG.
 *
 * The method used is similar to the above n2_inj_sca() routine.
 *
 * Note that MMU Register array errors can be detected via asi loads
 * and also on HW tablewalks through the array.  So even if NOERR is set,
 * the system will likely trip the error with a tablewalk (HWTW).
 */

/*
 * Prototype:
 *
 * int n2_inj_mra(uint64_t vaddr, uint64_t enable, uint64_t eccmask);
 *
 * Register usage:
 *
 *	%o1 - vaddr used for the ASI access (expects MMU_ZERO_CTX_TSB_CFG_0)
 *	%o2 - enable bit, single-shot, and NOERR flag
 *	%o3 - ecc mask
 *	%o4 - unused
 *	%g1 - NOERR flag
 *	%g2 - holds single-shot flag
 *	%g3 - holds NOERR flag
 *	%g4 - unused
 *	%g5 - contents of the register used for injection
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_mra(uint64_t vaddr, uint64_t enable, uint64_t eccmask)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(n2_inj_mra)

	add	%o2, %o3, %o2			! combine mask and enable
	srlx	%o2, EI_REG_SSHOT_ENB_SHIFT, %g2 ! %g2 = sshot flag
	and	%g2, 1, %g2			! .

	srlx	%o2, EI_REG_NOERR_SHIFT, %g3	! %g3 = NOERR access flag
	and	%g3, 1, %g3			! .

	ldxa	[%o1]ASI_MMU_TSB_CFG, %g5	! %g5 = register contents

	/*
	 * Inject the error via the injection register.
	 */
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	stxa	%g5, [%o1]ASI_MMU_TSB_CFG	! perform write to inject

	/*
	 * If single shot mode was specified, then clear the injection reg.
	 */
	membar	#Sync				! required
	brnz,a	%g2, 1f				! check if sshot mode set
	  stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	!   if so clear injection reg
1:
	/*
	 * NOERR in this case means that the HW may run over the error
	 * during a hardware tablewalk (if enabled).
	 */
	cmp	%g3, %g0			! if NOERR flag bit set
	bnz	%icc, 2f			!   skip access section
	  nop					! .

	ldxa	[%o1]ASI_MMU_TSB_CFG, %g0	! perform load to invoke
	membar	#Sync				! .
2:
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_inj_mra)
#endif	/* lint */

/*
 * The following set of routines are used to corrupt the data ecc or the
 * tag (CAM) parity of the internal Store Buffer array on the Niagara-II
 * processor via the per core ASI_INJECT_ERROR_REG.
 *
 * There is no need to use a membar #Sync or do a read-back of the
 * injection register because it is post-synchronizing in hardware.
 * Also stores to the error injection register itself are immune to
 * error injection.
 *
 * The store buffer only holds eight entries and so very little code can
 * execute between the injection and the invocation, so going back to
 * kernel mode before accessing the error is unlikely to produce errors.
 *
 * Because compare (cmp), branch (bx) and other (?) instructions cause
 * registers to be written these instructions cannot be safely used
 * while the injection register is enabled.  Even leaving hypervisor mode
 * with the injection enabled can cause an error when hpriv gets cleared.
 *
 * Due to the above restrictions, instead of one intelligent routine, the
 * functionality has been broken down into a number of simple routines to
 * handle the different injection types, access types, and to produce both
 * single and multiple errors.
 *
 * Injection types:
 *	1) DRAM memory store (load or PCX access)
 *	2) I/O memory store (I/O load or PCX access)
 *	3) ASI store (ASI load or PCX access)
 *
 * Access types:
 *	1) no access
 *	2) single
 *	3) multiple (up to eight)
 *
 * A PCX read can be performed via a membar #Sync.
 *
 * An I/O error can be accomplished via a non-cacheable load.
 *
 * NOTE: it is possible that the write to turn off error injections
 *	 will get missed when the store buffer is flushed when an error
 *	 is detected, so it should not be done before the access unless
 *	 it is also done afterwards.  Otherwise NO error is detected/triggered
 *	 for certain error types (such as normal DRAM memory access).
 *	 This is why the injection register is being cleared AFTER the
 *	 access which will trigger the error in the following routines.
 *
 * NOTE: the SB only has 7-bits of ecc but the injection registers ecc field
 *	 is 8-bits wide.  So bits 0-6 are valid however using bit 6 alone
 *	 will NOT produce an error.  This may be because bit 6 is not a
 *	 normal ecc bit but acts a bit differently.
 *
 * NOTE: these routines expect that the hypervisor trap handler will
 *	 disable the error injection register to avoid continuous errors.
 *	 This is required otherwise so many errors can result that the system
 *	 wedges, and is recommended by the HW designers for production code.
 */

/*
 * Prototype:
 *
 * int n2_inj_sb_load(uint64_t paddr, uint64_t enable, uint64_t eccmask,
 *			uint64_t count);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - enable bit (previously also used for flags)
 *	%o3 - ecc mask
 *	%o4 - error count (how many errors to inject/invoke)
 *	%g1 - unused
 *	%g2 - unused
 *	%g3 - unused
 *	%g4 - unused
 *	%g5 - contents of the paddr used for the injection
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_sb_load(uint64_t paddr, uint64_t enable, uint64_t eccmask,
		uint64_t count)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(n2_inj_sb_load)

	add	%o2, %o3, %o2			! combine mask and enable

	ldx	[%o1], %g5			! read contents of paddr
						! note this brings line back
						! in the caches (no DAE trap)
	/*
	 * Based on the count value, do one of the following:
	 *	count = 0:	inject one error, do not invoke it
	 *	count = 1:	inject one error, then invoke it
	 *	count > 1:	inject four errors, then invoke them
	 */
	brz	%o4, 1f				! goto inject w/o invoke
	  nop					! .
	cmp	%o4, 1
	be	2f				! goto inject w/ invoke
	  nop

	/*
	 * Otherwise inject four errors and access them with loads.
	 */
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	stx	%g5, [%o1]			! perform write to inject
	stx	%g5, [%o1 + 0x10]		! perform write to inject
	stx	%g5, [%o1 + 0x20]		! perform write to inject
	stx	%g5, [%o1 + 0x30]		! perform write to inject

	ldub	[%o1], %o4			! use normal load to access
	ldub	[%o1 +0x10], %o4		! use normal load to access
	ldub	[%o1 +0x20], %o4		! use normal load to access
	ldub	[%o1 +0x30], %o4		! use normal load to access

	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg after acc
	ba	3f
	  mov	%g0, %o0			! set return value to PASS
1:	
	/*
	 * Inject one error but do not access it.
	 */
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	stx	%g5, [%o1]			! perform write to inject

	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg
	ba	3f
	  mov	%g0, %o0			! set return value to PASS
2:
	/*
	 * Inject one error and access it with a load.
	 */
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	stx	%g5, [%o1]			! perform write to inject

	ldub	[%o1], %o4			! use normal load to access

	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg after acc
	ba	3f
	  mov	%g0, %o0			! set return value to PASS
3:
	done					! .
	SET_SIZE(n2_inj_sb_load)
#endif	/* lint */

/*
 * Prototype:
 *
 * int n2_inj_sb_pcx(uint64_t paddr, uint64_t enable, uint64_t eccmask,
 *			uint64_t count);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - enable bit (previously also used for flags)
 *	%o3 - ecc mask
 *	%o4 - error count (how many errors to inject/invoke)
 *	%g1 - unused
 *	%g2 - unused
 *	%g3 - unused
 *	%g4 - unused
 *	%g5 - contents of the paddr used for the injection
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_sb_pcx(uint64_t paddr, uint64_t enable, uint64_t eccmask,
		uint64_t count)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(n2_inj_sb_pcx)

	add	%o2, %o3, %o2			! combine mask and enable

	ldx	[%o1], %g5			! read contents of paddr
						! note this brings line back
						! in the caches (no DAE trap)
	/*
	 * Based on the count value, do one of the following:
	 *	count = 0:	inject one error, do not invoke it
	 *	count = 1:	inject one error, then invoke it
	 *	count > 1:	inject four errors, then invoke them
	 */
	brz	%o4, 1f				! goto inject w/o invoke
	  nop					! .
	cmp	%o4, 1
	be	2f				! goto inject w/ invoke
	  nop

	/*
	 * Otherwise inject four errors and access them with a membar.
	 */
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	stx	%g5, [%o1]			! perform write to inject
	stx	%g5, [%o1 + 0x10]		! perform write to inject
	stx	%g5, [%o1 + 0x20]		! perform write to inject
	stx	%g5, [%o1 + 0x30]		! perform write to inject

	membar	#Sync				! membar to drain and invoke

	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg after acc
	ba	3f
	  mov	%g0, %o0			! set return value to PASS
1:	
	/*
	 * Inject one error but do not access it.
	 */
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	stx	%g5, [%o1]			! perform write to inject

	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg
	ba	3f
	  mov	%g0, %o0			! set return value to PASS
2:
	/*
	 * Inject one error and access it with a membar to drain buffer.
	 */
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	stx	%g5, [%o1]			! perform write to inject

	membar	#Sync				! membar to drain and invoke

	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg after acc
	ba	3f
	  mov	%g0, %o0			! set return value to PASS
3:
	done					! .
	SET_SIZE(n2_inj_sb_pcx)
#endif	/* lint */

/*
 * Prototype:
 *
 * int n2_inj_sb_asi(uint64_t asi_vaddr, uint64_t enable, uint64_t eccmask,
 *			uint64_t vaddr);
 *
 * Register usage:
 *
 *	%o1 - asi to use for the access
 *	%o2 - enable bit (previously also used for flags)
 *	%o3 - ecc mask
 *	%o4 - vaddr for asi (expected to be 0x10 since scratchpad asi used)
 *	%g1 - unused
 *	%g2 - unused
 *	%g3 - unused
 *	%g4 - unused
 *	%g5 - contents of the paddr used for the injection
 *	%g6 - saved asi reg value
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_sb_asi(uint64_t asi_vaddr, uint64_t enable, uint64_t eccmask,
			uint64_t vaddr)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(n2_inj_sb_asi)

	add	%o2, %o3, %o2			! combine mask and enable

	rd	%asi, %g6			! save asi reg
	wr      %g0, %o1, %asi			! use specified ASI for access
	ldxa	[%o4]%asi, %g5			! read register contents

	/*
	 * Inject the error via the injection register and normal store.
	 *
	 * NOTE: for an alternate access method can comment out the stxa
	 *	 instruction and uncomment the membar below it.
	 */
	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	stxa	%g5, [%o4]%asi			! perform write to inject

	stxa	%g5, [%o4]%asi			! perform store to invoke

!	membar	#Sync				! membar to drain and invoke

	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg after acc

	wr	%g0, %g6, %asi			! restore asi reg
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_inj_sb_asi)
#endif	/* lint */

/*
 * This routine is used to corrupt the data ecc or the tag (CAM) parity
 * of the internal Store Buffer array on the Niagara-II processor via the
 * per core ASI_INJECT_ERROR_REG.
 *
 * This routine is similar to the above n2_inj_sb_load() routine, except
 * that the address used is an IO address which is accessed via ASI_REAL_IO
 * in order to produce a non-cacheable access here in hyperpriv mode.
 * Also sibling core parking is used in this routine (it is not necessary
 * for the other store buffer error types).
 *
 * NOTE: is seems that ASI_REAL_IO does not need an RA->PA translation
 *	 iotte although the ASI_REAL_MEM accesses do.  If an iotte is
 *	 required then an iotte installer function will have to be written
 *	 (similar to the existing memory tte installer).
 */

/*
 * Prototype:
 *
 * int n2_inj_sb_io(uint64_t paddr, uint64_t enable, uint64_t eccmask,
 *			uint64_t count);
 *
 * Register usage:
 *
 *	%o1 - raddr
 *	%o2 - enable bit (previously also used for flags)
 *	%o3 - ecc mask
 *	%o4 - error count (how many errors to inject/invoke)
 *	%g1 - temp for park/unpark
 *	%g2 - temp for park/unpark
 *	%g3 - temp for park/unpark
 *	%g4 - temp for park/unpark
 *	%g5 - contents of the paddr used for the injection
 *	%g6 - temp for park
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_sb_io(uint64_t raddr, uint64_t enable, uint64_t eccmask,
		uint64_t count)
{
	return 0;
}
#else
	.align	1024
	ENTRY_NP(n2_inj_sb_io)

	add	%o2, %o3, %o2			! combine mask and enable

	lda	[%o1]ASI_REAL_IO, %g5		! read contents of raddr

	/*
	 * Based on the count value, do one of the following:
	 *	count = 0:	inject one error, do not invoke it
	 *	count = 1:	inject one error, then invoke it
	 *	count > 1:	inject four errors, then invoke them
	 */
	brz	%o4, 1f				! goto inject w/o invoke
	  nop					! .
	cmp	%o4, 1
	be	2f				! goto inject w/ invoke
	  nop

	/*
	 * Otherwise inject four errors and access them with loads.
	 */
	N2_PARK_SIBLING_STRANDS(%g1, %g2, %g3, %g4, %g6)

	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	stxa	%g5, [%o1]ASI_REAL_IO		! perform write to inject
	add	%o1, 0x10, %o1
	stxa	%g5, [%o1]ASI_REAL_IO		! perform write to inject
	add	%o1, 0x10, %o1
	stxa	%g5, [%o1]ASI_REAL_IO		! perform write to inject
	add	%o1, 0x10, %o1
	stxa	%g5, [%o1]ASI_REAL_IO		! perform write to inject

	N2_UNPARK_SIBLING_STRANDS(%g1, %g2, %g3, %g4)

	lda	[%o1]ASI_REAL_IO, %o4		! read raddr to invoke
	sub	%o1, 0x10, %o1
	lda	[%o1]ASI_REAL_IO, %o4		! read raddr to invoke
	sub	%o1, 0x10, %o1
	lda	[%o1]ASI_REAL_IO, %o4		! read raddr to invoke
	sub	%o1, 0x10, %o1
	lda	[%o1]ASI_REAL_IO, %o4		! read raddr to invoke

	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg after acc
	ba	3f
	  mov	%g0, %o0			! set return value to PASS
1:	
	/*
	 * Inject one error but do not access it.
	 */
	N2_PARK_SIBLING_STRANDS(%g1, %g2, %g3, %g4, %g6)

	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	stxa	%g5, [%o1]ASI_REAL_IO		! perform write to inject
	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg

	N2_UNPARK_SIBLING_STRANDS(%g1, %g2, %g3, %g4)

	ba	3f
	  mov	%g0, %o0			! set return value to PASS
2:
	/*
	 * Inject one error and access it with a load or membar (see below).
	 *
	 * NOTE: to alternate access methods can comment out or
	 *	 uncomment the lda or the membar below it.
	 */
	N2_PARK_SIBLING_STRANDS(%g1, %g2, %g3, %g4, %g6)

	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	stxa	%g5, [%o1]ASI_REAL_IO		! perform write to inject

	N2_UNPARK_SIBLING_STRANDS(%g1, %g2, %g3, %g4)

!	lda	[%o1]ASI_REAL_IO, %o4		! read raddr to invoke

	membar	#Sync				! membar to drain and invoke

	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg after acc
	ba	3f
	  mov	%g0, %o0			! set return value to PASS
3:
	done					! .
	SET_SIZE(n2_inj_sb_io)
#endif	/* lint */

/*
 * This routine uses the per core ASI_MA_CONTROL_REG to inject a Modular
 * Arithmetic parity error.
 *
 * The ASI_MA_SYNC_REG (ASI 0x40, VA 0xa0) is used to enforce completion by
 * a load access (so SPARC can synchronize with the completion of an op).
 *
 * NOTE: this routine is based on the Niagara-I version ni_inj_ma_parity().
 *
 * ASI_MA_CONTROL_REG (0x40, VA=0x80)
 *
 * +------+------+------+-----+-----+--------+-----+------+------+----+--------+
 * | Rsvd | PErr | Rsvd | HWE | Inv | Strand | Int | Busy | Rsvd | Op | Length |
 * +------+------+------+-----+-----+--------+-----+------+------+----+--------+
 *  63:26  25:24    23    22    21    20:18    17     16   15:13  12:8   7:0
 *
 * NOTE: the MA unit is very similar to the Niagara-I MA and has the
 *	 following properties:
 *	- MA memory on N1 was 1280B in size, N2 is the same but has two parity
 *	  bits for each entry (one for each 32-bit chunk).
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
 *	  the ASI_STREAM (0x40) register.
 *	- ASI_MA_MPA_REG (ASI 0x40, VA 0x88) bits 38:3 used to store paddr
 *	  of address to use for loads and stores.  Lower 8 bits are the word
 *	  offset (64-bits per word) into the MA memory array.
 *	- ASI_MA_ADDR_REG (ASI 0x40, VA 0x90) contains offsets for the various
 *	  operands and results of operations in the MA memory buffer.
 */

/*
 * Prototype:
 *
 * int n2_inj_mamem(uint64_t paddr, uint_t op, uint64_t xor);
 *
 * Register usage:
 *
 *	%o1 - paddr of mem buffer to use
 *	%o2 - operation to use to inject the error ([12:8] are valid)
 *	%o3 - xor mask for the parity bit injection ([25:24] are valid)
 *	%o4 - unused
 *	%g1 - unused
 *	%g2 - temp
 *	%g3 - settings for the MA_CTL register
 *	%g4 - vaddr for core asi reg
 *	%g5 - scratch for setx
 *	%g6 - temp to get the current strand
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
n2_inj_mamem(uint64_t paddr, uint_t op, uint64_t xor)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(n2_inj_mamem)

	mov	ASI_MA_MPA_REG, %g2		! point MPA to buffer paddr
	andn	%o1, 0x3, %o1			! align the paddr
	stxa	%o1, [%g2]ASI_STREAM		! store aligned paddr

	mov	ASI_MA_NP_REG, %g2		! set N prime value to 7
	mov	0x7, %g3			!   to be safe
	stxa	%g3, [%g2]ASI_STREAM		! store value

	mov	ASI_MA_ADDR_REG, %g2		! set operand MA offsets
	setx	MA_OP_ADDR_OFFSETS, %g5, %g3	! .
	stxa	%g3, [%g2]ASI_STREAM		! store offsets

	/*
	 * Perform the operation (STRAND from status reg, INT = 0, LENGTH = 3)
	 * Note that the length field represents one less than the length.
	 */
	mov	2, %g3				! set length to 3 8-byte words
	or	%g3, %o3, %g3			! set parity invert bit(s)
	or	%g3, %o2, %g3			! set operation type

	mov	ASI_CMT_STRAND_ID, %g4		! read the strand status reg
	ldxa	[%g4]ASI_CMT_CORE_REG, %g6	! .
	and	%g6, 0x7, %g6			! keep only the strand id bits
	sllx	%g6, 18, %g6			! shift id for MA reg [20:18]
	or	%g3, %g6, %g3			! combine with reg contents

	mov	ASI_MA_CONTROL_REG, %g2		! set VA for operation
	stxa	%g3, [%g2]ASI_STREAM		! store the value to do op

	mov	ASI_MA_SYNC_REG, %g2		! ensure operation completes
	ldxa	[%g2]ASI_STREAM, %g0		! .

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_inj_mamem)
#endif	/* lint */

/*
 * The following routine is used to inject an SOC MCU error using the SOC
 * error injection register.  There is no single-shot available for SOC
 * error injection so this routine is kept as simple as possible to avoid
 * injecting more than one error by turning on error injection, performing
 * a memory access to cause an error, and then immediately turning off error
 * injection (all while executing out of L2 cache to avoid memory instruction
 * fetches).
 *
 * Prototype:
 *
 * int n2_inj_soc_mcu(uint64_t paddr, uint64_t soc_reg,
 *				uint64_t soc_reg_val);
 *
 * Register usage:
 *
 *	%o1 - paddr to access to cause an error
 *	%o2 - addr of SOC error injection register
 *	%o3 - enable value for SOC error injection register
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
int
n2_inj_soc_mcu(uint64_t paddr, uint64_t soc_reg, uint64_t soc_reg_val)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(n2_inj_soc_mcu)

	mov	%g0, %o0		! set up PASS return value
	ba	2f
	  nop

	/*
	 * Critical section.  This will execute out of L2$ and must fit
	 * on single L2$ line.
	 */
	.align	64
1:
	stx	%o3, [%o2]			! turn on error injection
	membar	#Sync

	ldub    [%o1], %g0			! mem access to cause error
	membar	#Sync

	stx	%g0, [%o2]			! turn off error injection
	membar	#Sync

	done

	/*
	 * Fill up the prefetch buffer (full L2$ line).
	 */
	.align	64
2:
	nop; nop; nop; nop; nop; nop; nop;
	nop; nop; nop; nop; nop; nop; nop;
	ba	1b
	  nop

	SET_SIZE(n2_inj_soc_mcu)
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
 * NOTE: this routine is based on the Niagara-I version ni_acc_ma_memory().
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
 * int n2_acc_mamem(uint64_t paddr, uint_t op, uint_t int_flag);
 *
 * Register usage:
 *
 *	%o1 - paddr of the start of the memory buffer
 *	%o2 - operation to use to invoke existing error
 *	%o3 - int_flag to determine interrupt or polled mode (int if != 0)
 *	%o4 - unused
 *	%g1 - unused
 *	%g2 - temp
 *	%g3 - settings for the MA_CTL register
 *	%g4 - temp
 *	%g5 - scratch for setx
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
n2_acc_mamem(uint64_t paddr, uint_t op, uint_t int_flag)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(n2_acc_mamem)

	mov	ASI_MA_MPA_REG, %g2		! point MPA to buffer paddr
	andn	%o1, 0x3, %o1			! align the paddr
	stxa	%o1, [%g2]ASI_STREAM		! store aligned paddr

	mov	ASI_MA_NP_REG, %g2		! set N prime value to 7
	mov	0x7, %g3			!   to be safe
	stxa	%g3, [%g2]ASI_STREAM		! store value

	mov	ASI_MA_ADDR_REG, %g2		! set operand MA offsets
	setx	MA_OP_ADDR_OFFSETS, %g5, %g3	! .
	stxa	%g3, [%g2]ASI_STREAM		! store offsets

	/*
	 * Perform the operation (STRAND = 0, LENGTH = 3)
	 */
	mov	2, %g3				! set length to 3 8-byte words
	or	%g3, %o2, %g3			! set operation type

	mov	ASI_CMT_STRAND_ID, %g4		! read the strand status reg
	ldxa	[%g4]ASI_CMT_CORE_REG, %g6	! .
	and	%g6, 0x7, %g6			! keep only the strand id bits
	sllx	%g6, 18, %g6			! shift id for MA reg [20:18]
	or	%g3, %g6, %g3			! combine with reg contents

	cmp	%o3, %g0			! interrupt or poll mode?
	bz	%icc, 1f			! if poll don't set int bit
	  nop					! .
	mov	1, %g4				! set interrupt bit
	sllx	%g4, 17, %g4			! .
	or	%g3, %g4, %g3			! .
1:
	mov	ASI_MA_CONTROL_REG, %g2		! set VA for operation
	stxa	%g3, [%g2]ASI_STREAM		! store the value to do op

	mov	ASI_MA_SYNC_REG, %g2		! ensure operation completes
	ldxa	[%g2]ASI_STREAM, %g0		!   only req'd for poll mode
	membar	#Sync				! .

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_acc_mamem)
#endif	/* lint */

/*
 * This routine performs a Control Word Queue operation to invoke
 * a previously injected error in the L2 cache, or DRAM memory.
 *
 * Control Words are 64-Bytes long and are broken into a number of fields
 * (described in PRM v1.2 section 15.8.8 and onwards).
 *
 * The ASI_CWQ_SYNC_REG (ASI 0x40, VA 0x30) is used to enforce completion by
 * a load access (so SPARC can synchronize with the completion of an op).
 *
 * The CWQ head pointer will point to specific CWQ entries depending on
 * whether the error was in a Complete, Final, Initial, or Extension
 * control word.  However the error injector only uses a Complete control
 * word for simplicity (and the generated errors are the same in any case).
 *
 * NOTE: the CWQ (like the MA) bypasses the L1 caches.
 *
 * NOTE: software should issue a membar #Sync or equivalent after updating the
 *	 last CWQ in memory, before writing to the Tail register via an ASI.
 *	 This guarantees that the SPU will read the updated CWQ entries,
 *	 however if the CWQ is disabled during the writes, this is not needed.
 *
 * NOTE: this routine expects to be called with an op type of COPY, so
 *	 other op types may produce unexpected bahaviour (such as working).
 *
 * NOTE: this routine is used for KT/RF as well, the CWQ is the same except
 *	 for a "priv" bit in the control reg which is being ignored.
 */

#define	MIN_DATABUF_SIZE	8192	/* must match memtestio.h */

/*
 * Prototype:
 *
 * int n2_acc_cwq(uint64_t src_paddr, uint64_t dest_paddr, uint_t op,
 *			uint_t int_flag);
 *
 * Register usage:
 *
 *	%o1 - paddr of the source operands
 *	%o2 - paddr of the destination (to place results)
 *	%o3 - operation to use to invoke existing error (COPY expected)
 *	%o4 - int_flag to determine interrupt or polled mode (int if != 0) _OR_
 *	      a pointer to the DEBUG buffer if MEM_DEBUG is set.
 *	%g1 - head ptr paddr
 *	%g2 - temp for registers at VAs of ASI_STREAM
 *	%g3 - built first entry of the CW (using a Complete CW)
 *	%g4 - temp
 *	%g5 - new tail ptr paddr
 *	%g6 - temp
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
n2_acc_cwq(uint64_t src_paddr, uint64_t dest_paddr, uint_t op, uint_t int_flag)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(n2_acc_cwq)

	/*
	 * Ensure the SPU is not busy before configuring CWQ,
	 * first errors are cleared and CWQ is disabled, then a synchronous
	 * load is done to wait for any outstanding ops to drain.
	 */
	mov	ASI_CWQ_CSR_REG, %g2		! clear/disable CWQ
	stxa	%g0, [%g2]ASI_STREAM		! .

	mov	ASI_CWQ_SYNC_REG, %g2		! ensure any ops complete
	ldxa	[%g2]ASI_STREAM, %g0		! .

	/*
	 * Build the first reg of control word with specific attributes:
	 *	- set length (hardcode to 24 bytes)
	 *	- op from caller (COPY is expected)
	 *	- set sob and eob (for Complete CW)
	 *	- set the STRAND ID (read from reg)
	 *	- set INT if specified by inpar
	 */
	mov	23, %g3				! set length to 3 8-byte words
	sllx	%o3, N2_CWQ_OP_SHIFT, %o3	! set operation type
	or	%g3, %o3, %g3			! .

	mov	3, %g4				! set the eob and sob bits so
	sllx	%g4, N2_CWQ_EOB_SHIFT, %g4	!   this is a Complete CW
	or	%g3, %g4, %g3			! .

	mov	ASI_CMT_STRAND_ID, %g4		! read the strand status reg
	ldxa	[%g4]ASI_CMT_CORE_REG, %g6	! .
	and	%g6, 0x7, %g6			! keep only the strand id bits
	sllx	%g6, N2_CWQ_STRAND_SHIFT, %g6	! shift id for CWQ reg [39:37]
	or	%g3, %g6, %g3			! %g3 = initial CW word

#ifndef MEM_DEBUG_BUFFER
	cmp	%o4, %g0			! interrupt or poll mode?
	bz	%icc, 1f			! if poll don't set int bit
	  nop					!   %g3 = first CW word
	mov	1, %g4				! set interrupt bit
	sllx	%g4, N2_CWQ_INT_SHIFT, %g4	! .
	or	%g3, %g4, %g3			! %g3 = initial CW word (w/ int)
#endif

1:
	/*
	 * Write the control words into a new queue that we will place
	 * at an aligned address 1/4 of the way into the EI buffer
	 * (this routine expects that the injection address is near the
	 * beginning of the EI buffer, and the queue must be 64B aligned).
	 */
	setx	MIN_DATABUF_SIZE, %g5, %g1	! determine addr to put Q at
	srlx	%g1, 2, %g1			! (as described above)
	add	%o1, %g1, %g1			! .
	andn	%g1, 0x3f, %g1			! .

	mov	ASI_CWQ_HEAD_REG, %g2		! set the head ptr
	stxa	%g1, [%g2]ASI_STREAM		! %g1 = head ptr paddr
	stx	%g3, [%g1]			! write initial CW (8B) to CWQ
	stx	%o1, [%g1 + N2_CWQ_SRC_ADDR_OFFSET] ! write source paddr reg
	stx	%o2, [%g1 + N2_CWQ_DST_ADDR_OFFSET] ! write dest paddr reg
	membar	#Sync				! required before moving tail

#ifdef MEM_DEBUG_BUFFER
	stx	%g3, [%o4 + 0x0]		! value for inital CW
	stx	%o1, [%o4 + 0x8]		! source paddr
	stx	%o2, [%o4 + 0x10]		! dest paddr
	ldx	[%o1], %g4
	stx	%g4, [%o4 + 0x18]		! value at src paddr
	ldx	[%o2], %g4
	stx	%g4, [%o4 + 0x20]		! value at dest paddr
#endif

	/*
	 * Set the other queue control registers (first, last, and tail).
	 * There is a CWQ minumum of 2 queue entries, and our CW is complete
	 * so there is only one of them.
	 */
	mov	ASI_CWQ_FIRST_REG, %g2		! set first ptr = head ptr
	stxa	%g1, [%g2]ASI_STREAM		! .

	add	%g1, N2_CWQ_CW_SIZE, %g5	! set tail ptr = head + 1 CW
	mov	ASI_CWQ_TAIL_REG, %g2		! .
	stxa	%g5, [%g2]ASI_STREAM		! .

#ifdef MEM_DEBUG_BUFFER
	stx	%g1, [%o4 + 0x30]		! head ptr (set)
	stx	%g5, [%o4 + 0x38]		! tail ptr (set)
#endif

	add	%g5, N2_CWQ_CW_SIZE, %g5	! set last ptr = head + 2 CW
	mov	ASI_CWQ_LAST_REG, %g2		! .
	stxa	%g5, [%g2]ASI_STREAM		! .

	mov	ASI_CWQ_CSR_ENABLE_REG, %g2	! enable CWQ to perform op
	mov	1, %g4				! .
	stxa	%g4, [%g2]ASI_STREAM		! .

	mov	ASI_CWQ_SYNC_REG, %g2		! ensure operation completes
	ldxa	[%g2]ASI_STREAM, %g0		!   only req'd for poll mode
	membar	#Sync				! .

#ifdef MEM_DEBUG_BUFFER
	mov	ASI_CWQ_TAIL_REG, %g2		! read the current tail ptr
	ldxa	[%g2]ASI_STREAM, %g5		! .
	stx	%g5, [%o4 + 0x40]		! current tail ptr
	ldx	[%o2], %g1
	stx	%g1, [%o4 + 0x48]		! value at dest paddr (copied)
#endif

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_acc_cwq)
#endif	/* lint */

/*
 * This is another version of the CWQ access routine that is to be used
 * when there is aleady a queue setup (likely by te hypervisor).  This
 * way the head and tail pointers can be used in place.
 *
 * All the same caveats for the above n2_cwq_acc() routine apply here.
 *
 * NOTE: this routine is used for KT/RF as well, the CWQ is the same except
 *	 for a "priv" bit in the control reg which is being ignored.
 */
#if defined(lint)
/* ARGSUSED */
int
n2_acc_cwq_inplace(uint64_t src_paddr, uint64_t dest_paddr, uint_t op,
			uint_t int_flag)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(n2_acc_cwq_inplace)

	/*
	 * Ensure the SPU is not busy before configuring CWQ,
	 * first errors are cleared and CWQ is disabled, then a synchronous
	 * load is done to wait for any outstanding ops to drain.
	 */
	mov	ASI_CWQ_CSR_REG, %g2		! clear/disable CWQ
	stxa	%g0, [%g2]ASI_STREAM		! .

	mov	ASI_CWQ_SYNC_REG, %g2		! ensure ops complete
	ldxa	[%g2]ASI_STREAM, %g0		! .

	/*
	 * Build the first reg of control word with specific attributes:
	 *	- set length (hardcode to 24 bytes)
	 *	- op from caller (COPY is expected)
	 *	- set sob and eob (for Complete CW)
	 *	- set the STRAND ID (read from reg)
	 *	- set INT if specified by inpar
	 */
	mov	23, %g3				! set length to 3 8-byte words
	sllx	%o3, N2_CWQ_OP_SHIFT, %o3	! set operation type
	or	%g3, %o3, %g3			! .

	mov	3, %g4				! set the eob and sob bits so
	sllx	%g4, N2_CWQ_EOB_SHIFT, %g4	!   this is a Complete CW
	or	%g3, %g4, %g3			! .

	mov	ASI_CMT_STRAND_ID, %g4		! read the strand status reg
	ldxa	[%g4]ASI_CMT_CORE_REG, %g6	! .
	and	%g6, 0x7, %g6			! keep only the strand id bits
	sllx	%g6, N2_CWQ_STRAND_SHIFT, %g6	! shift id for CWQ reg [39:37]
	or	%g3, %g6, %g3			! %g3 = initial CW word

#ifndef MEM_DEBUG_BUFFER
	cmp	%o4, %g0			! interrupt or poll mode?
	bz	%icc, 1f			! if poll don't set int bit
	  nop					!   %g3 = first CW word
	mov	1, %g4				! set interrupt bit
	sllx	%g4, N2_CWQ_INT_SHIFT, %g4	! .
	or	%g3, %g4, %g3			! %g3 = initial CW word (w/ int)
#endif

1:
	/*
	 * Write the control word into the queue (control, src, dest).
	 */
	mov	ASI_CWQ_HEAD_REG, %g2		! determine where head ptr is
	ldxa	[%g2]ASI_STREAM, %g1		! %g1 = head ptr paddr
	stx	%g3, [%g1]			! write first 8B into CWQ
	stx	%o1, [%g1 + N2_CWQ_SRC_ADDR_OFFSET] ! write source paddr reg
	stx	%o2, [%g1 + N2_CWQ_DST_ADDR_OFFSET] ! write dest paddr reg
	membar	#Sync				! required before moving tail

#ifdef MEM_DEBUG_BUFFER
	stx	%g3, [%o4 + 0x0]		! value for inital CW
	stx	%o1, [%o4 + 0x8]		! source paddr
	stx	%o2, [%o4 + 0x10]		! dest paddr
	ldx	[%o2], %g4
	stx	%g4, [%o4 + 0x18]		! value at dest paddr

	stx	%g1, [%o4 + 0x20]		! existing head ptr
#endif

	/*
	 * This version of the CWQ routine expects that there is an already
	 * setup queue that can be used/accessed.  This is not necessarily
	 * going to be the case, but this leaves the queue in a usable state
	 * if it did/does exist.
	 */
	mov	ASI_CWQ_LAST_REG, %g2		! determine where head ptr is
	ldxa	[%g2]ASI_STREAM, %g4		! %g4 = last CWQ paddr
	cmp	%g1, %g4			! does head = last?
	bne,a	%xcc, 2f			! if not then
	  add	%g1, N2_CWQ_CW_SIZE, %g5	!   %g5 = tail = head + CW_size
	mov	ASI_CWQ_FIRST_REG, %g2		! else wrap tail ptr to first
	ldxa	[%g2]ASI_STREAM, %g5		!   %g5 = tail = first CW paddr
2:
	mov	ASI_CWQ_TAIL_REG, %g2		! write to the tail ptr to
	stxa	%g5, [%g2]ASI_STREAM		! increment to next CW

#ifdef MEM_DEBUG_BUFFER
	stx	%g5, [%o4 + 0x28]		! tail ptr (moved)
#endif

	mov	ASI_CWQ_CSR_ENABLE_REG, %g2	! enable CWQ to perform op
	mov	1, %g4				! .
	stxa	%g4, [%g2]ASI_STREAM		! .

	mov	ASI_CWQ_SYNC_REG, %g2		! ensure operation completes
	ldxa	[%g2]ASI_STREAM, %g0		!   only req'd for poll mode
	membar	#Sync				! .

#ifdef MEM_DEBUG_BUFFER
	mov	ASI_CWQ_TAIL_REG, %g2		! read the current tail ptr
	ldxa	[%g2]ASI_STREAM, %g5		! .
	stx	%g5, [%o4 + 0x30]		! tail ptr (now)
	ldx	[%o2], %g1
	stx	%g1, [%o4 + 0x38]		! value at dest paddr
#endif

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_acc_cwq_inplace)
#endif	/* lint */

/*
 * This is a utility routine to clear a specific TTE from the data or
 * instruction TLBs.  It is used to force misses in the TLB that will
 * hit errors that were previously injected into the TSB buffer.
 */

/*
 * Prototype:
 *
 * void n2_clear_tlb_entry(uint64_t vaddr, uint64_t data_flag)
 *
 * Register usage:
 *
 *	%o1 - vaddr of the entry to demap (does not need to be aligned)
 *	%o2 - flag for DATA vs. instruction
 *	%o3 - unused
 *	%o4 - unused
 *	%g1 - value for demap page operation
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
n2_clear_tlb_entry(uint64_t vaddr, uint64_t data_flag)
{}
#else
	.align	64
	ENTRY_NP(n2_clear_tlb_entry)

	/*
	 * Build address for the demap page operation.
	 */
	set	(N2_MMU_DEMAP_PAGE << 6), %g1	! set demap select
	andn	%o1, 0xfff, %o1			! clear lower 12 bits
	or	%g1, %o1, %g1			! %g1 = demap asi addr

	/*
	 * Flush the itlb or dtlb entry (since this routine handles both).
	 * Actually a full flush of the DTLB is ok, though all the misses
	 * that a full ITLB flush will cause are likely not worth it.
	 */
	cmp	%o2, %g0			! if DATA flag != 0
	bnz,a,pn %icc, 1f			!   clear data so it will miss
	  stxa	%g0, [%g1]ASI_DMMU_DEMAP	!   (else clear instr)
	stxa	%g0, [%g1]ASI_IMMU_DEMAP	!   .
1:
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_clear_tlb_entry)
#endif	/* lint */

/*
 * These routines do a full clear of the Niagara-II L1 caches.  The L1 caches
 * are inclusive of the L2$ so flushing a line from the L2 will invalidate the
 * corresponding line in L1 however this will not clean parity errors. 
 *
 * These routines clear the data and instruction caches completely using
 * the diagnostic ASI access registers which includes correct parity.  The
 * method for these routines was taken from the original Niagara-I hypervisor
 * reset code.
 *
 * NOTE: on Niagara-II there is a redundant (slave) valid bit which
 *	 cannot be written via the tag diagnostic register.  So these
 *	 routines may cause a massive amount of valid-bit parity errors!
 *
 * NOTE: do NOT attempt to use the n2_icache_clear() routine as-is on
 *	 KT/RF though the cache is the same size and the LSU register
 *	 has the same lower bitfields.  KT/RF does not seem to like the
 *	 i$ to be disabled on the fly so if an i$ clear routine is needed
 *	 on KT/RF it may require sibling parking to be added.
 *
 * Prototypes:
 *
 * void n2_dcache_clear(void);
 * void n2_icache_clear(void);
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
n2_dcache_clear(void)
{}
#else
	.align	128
	ENTRY_NP(n2_dcache_clear)

	ldxa	[%g0]N2_ASI_LSU_CTL_REG, %g5	! g5 = saved the LSU cntl reg
	andn	%g5, LSUCR_DC, %g4		! disable the d$ during clear
	stxa	%g4, [%g0]N2_ASI_LSU_CTL_REG	! .
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

	stxa	%g5, [%g0]N2_ASI_LSU_CTL_REG	! restore the LSU cntl reg
	membar	#Sync				! .

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_dcache_clear)
#endif	/* lint */

#if defined(lint)
/* ARGSUSED */
void
n2_icache_clear(void)
{}
#else
	.align	128
	ENTRY_NP(n2_icache_clear)

	ldxa	[%g0]N2_ASI_LSU_CTL_REG, %g5	! g5 = saved the LSU cntl reg
	andn	%g5, LSUCR_IC, %g4		! disable the i$ during clear
	stxa	%g4, [%g0]N2_ASI_LSU_CTL_REG	! .
	membar	#Sync

        set	(1 << 14), %g1			! set index for instr clear
1:      subcc	%g1, (1 << 3), %g1
        stxa	%g0, [%g1]ASI_ICACHE_INSTR
        bne,pt	%xcc, 1b
          nop

	set	(1 << 14), %g1                  ! set index for tag clear
2:	subcc	%g1, (1 << 6), %g1
	stxa	%g0, [%g1]ASI_ICACHE_TAG
	bne,pt	%xcc, 2b
	  nop

	stxa	%g5, [%g0]N2_ASI_LSU_CTL_REG	! restore the LSU cntl reg
	membar	#Sync				! .

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_icache_clear)
#endif	/* lint */

/*
 * This is a utility routine that puts the L1 caches into their (normal)
 * multi-way replacement mode.
 */
#if defined(lint)
/*ARGSUSED*/
void
n2_l1_disable_DM(void)
{}
#else
     	.align	32
	ENTRY(n2_l1_disable_DM)
	mov	0x10, %g2			! VA for LSU asi access
	stxa	%g0, [%g2]N2_ASI_LSU_DIAG_REG	! put i$ and d$ into LFSR
	membar	#Sync				!   replacement mode

	mov	%g0, %o0			! return PASS
	done
        SET_SIZE(n2_l1_disable_DM)
#endif	/* lint */

/*
 * This is a utility routine that puts all eight banks of the L2 cache into
 * their (normal) 16-way replacement mode.
 *
 * NOTE: this routine does not save or restore the current cache state.
 *
 * Prototype:
 *
 * void n2_l2_disable_DM(void);
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
n2_l2_disable_DM(void)
{}
#else
     	.align	64
	ENTRY(n2_l2_disable_DM)

	mov	N2_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0xa9.0000.0000
	sllx	%g1, 32, %g1			! .
	mov	N2_L2CR_DMMODE, %g5		! %g5 = direct map mode bit
	mov	N2_NUM_L2_BANKS, %g3		! must access all eight regs

1: ! n2_l2_DM_off:
	ldx	[%g1], %g4			! disable DM cache mode for
	andn	%g4, %g5, %g4			!   all banks which also
	stx	%g4, [%g1]			!   flushes the buffers.
	sub	%g3, 1, %g3
	brnz,a	%g3, 1b
	  add	%g1, N2_L2_BANK_OFFSET, %g1

	membar	#Sync				! (PRM says not needed)
	mov	%g0, %o0			! return PASS
	done
        SET_SIZE(n2_l2_disable_DM)
#endif	/* lint */

/*
 * This is a utility routine that puts all eight banks of the L2 cache into
 * direct-map (DM) replacement mode, for flushing and other special purposes
 * that require we know where in the cache a value is installed.
 *
 * NOTE: this routine does not save or restore the current cache state.
 *
 * Prototype:
 *
 * void n2_l2_enable_DM(void);
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
n2_l2_enable_DM(void)
{}
#else
     	.align	64
	ENTRY(n2_l2_enable_DM)

	! put L2 into direct mapped. Must to this to all 8 banks separately
	mov	N2_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0xa9.0000.0000
	sllx	%g1, 32, %g1			! .
	mov	N2_L2CR_DMMODE, %g5		! %g5 = direct map mode bit
	mov	N2_NUM_L2_BANKS, %g3		! must access all eight regs

1: ! n2_l2_DM_on:
	ldx	[%g1], %g4			! must do all banks
	or	%g4, %g5, %g4
	stx	%g4, [%g1]
	sub	%g3, 1, %g3
	brnz,a	%g3, 1b
	  add	%g1, N2_L2_BANK_OFFSET, %g1

	membar	#Sync				! (PRM says not needed)
	mov	%g0, %o0			! return PASS
	done
        SET_SIZE(n2_l2_enable_DM)
#endif	/* lint */

/*
 * This is a utility routine that returns the base address (PA) of the
 * contiguous 4 MB address range used by the system for displacement
 * flushing the L2 cache.
 *
 * Prototype:
 *
 * uint64_t n2_l2_get_flushbase(void);
 *
 * Register usage:
 *
 *	%o1 - value to be returned to caller
 *	%o2 - unused
 *	%o3 - unused
 *	%o4 - unused
 *	%g1 - scratch
 *	%g2 - unused
 *	%g3 - unused
 *	%g4 - unused
 *	%g5 - unused
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/*ARGSUSED*/
uint64_t
n2_l2_get_flushbase(void)
{
	return 0;
}
#else
     	.align	64
	ENTRY(n2_l2_get_flushbase)

	L2_FLUSH_BASEADDR(%o1, %g1)		! args are BASE, scratch
	mov	%g0, %o0			! return PASS
	done
        SET_SIZE(n2_l2_get_flushbase)
#endif	/* lint */

/*
 * This routine does a displacement flush of the full 4MB Niagara-II L2$
 * in hypervisor mode.
 *
 * NOTE: a displacement flush is not effective on Niagara-II if IDX mode
 *	 (L2-cache index hashing) is enabled and so the flush routines which
 *	 use the PREFETCH_ICE flush method should be used when IDX is enabled.
 *
 * Prototype:
 *
 * void n2_l2_flushall(void);
 *
 * Register usage:
 *
 *	%o1 - displacement flush base address
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
n2_l2_flushall(caddr_t flushbase)
{}
#else
	.align	256
	ENTRY_NP(n2_l2_flushall)

	! put L2 into direct mapped. Must to this to all 8 banks separately
	mov	N2_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0xa9.0000.0000
	sllx	%g1, 32, %g1			! .
	mov	N2_L2CR_DMMODE, %g5		! %g5 = direct map mode bit
	mov	N2_NUM_L2_BANKS, %g3		! are 8 banks of regs
	ldx	[%g1], %g4			! read the base reg
	andcc	%g4, %g5, %g0			! check l2$ mode (w/o writing)
	be	%xcc, 1f
	  nop
	mov	%g0, %g5			! already in DMMODE, don't
						!   change, but write anyway
1: ! n2_flush_l2_DM_on:
	ldx	[%g1], %g4			! must do all banks
	or	%g4, %g5, %g4
	stx	%g4, [%g1]
	sub	%g3, 1, %g3
	brnz,a	%g3, 1b
	  add	%g1, N2_L2_BANK_OFFSET, %g1

2: ! n2_flush_PA_setup:

	L2_FLUSH_BASEADDR(%g6, %g1)		! put flush BA into a reg (%g6)

	srlx	%g6, 20, %g6			! align flush addr to 1 MB
	sllx	%g6, 20, %g6			! .
	add	%g0, N2_NUM_L2_WAYS, %g2	! %g2 = way counter (16 ways)

3: ! n2_flush_ways:
	add	%g0, 512, %g3			! 512 lines per way, per bank

4: ! n2_flush_rows:
	ldx	[%g6+0x00], %g0			! must flush all 8 banks
	ldx	[%g6+0x40], %g0
	ldx	[%g6+0x80], %g0
	ldx	[%g6+0xc0], %g0

	ldx	[%g6+0x100], %g0
	ldx	[%g6+0x140], %g0
	ldx	[%g6+0x180], %g0
	ldx	[%g6+0x1c0], %g0

	sub	%g3, 1, %g3			! dec to next line set
	brnz	%g3, 4b
	  add	%g6, 0x200, %g6
	sub	%g2, 1, %g2			! dec to next way
	brnz,a	%g2, 3b
	  nop

	/*
	 * Once through the 4Mb area is necessary but NOT sufficient. If the
	 * last lines in the cache were dirty, then the writeback buffers may
	 * still be active. Need to always write the L2CSR at the end, whether
	 * it was changed or not to flush the cache buffers.
	 */
	mov	N2_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0xa9.0000.0000
	sllx	%g1, 32, %g1			! .
	mov	N2_NUM_L2_BANKS, %g3		! must access all eight regs

5: ! n2_flush_l2_DM_off:
	ldx	[%g1], %g4			! restore the cache mode for
	xor	%g4, %g5, %g4			!   all eight banks which also
	stx	%g4, [%g1]			!   flushes the buffers.
	sub	%g3, 1, %g3
	brnz,a	%g3, 5b
	  add	%g1, N2_L2_BANK_OFFSET, %g1

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_l2_flushall)
#endif	/* lint */

/*
 * This routine does a displacement flush of an entry specified by it's
 * physical address, from the Niagara-II L2$ (in hypervisor mode).
 *
 * NOTE: this routine is used when IDX index hashing is disabled.
 *
 * Prototype:
 *
 * void n2_l2_flushentry(caddr_t paddr2flush);
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
n2_l2_flushentry(caddr_t paddr2flush)
{}
#else
	.align	128
	ENTRY_NP(n2_l2_flushentry)

	/*
	 * Determine the initial L2 flush addr for the specified paddr.
	 */
	set	0x3ffc0, %g3			! PA mask for bank and set
	and	%g3, %o1, %g3			! PA[17:6] (note way=PA[21:18])

	L2_FLUSH_BASEADDR(%g4, %g1)		! put flush BA into a reg (%g4)
	add	%g3, %g4, %g3			! %g3 = the flush addr to use

	mov	N2_NUM_L2_WAYS, %o2		! %o2 = number of L2 ways
	mov	1, %o3				! %o3 = L2 way inc. value
	sllx	%o3, N2_L2_WAY_SHIFT, %o3	!   (note shift = 18)

	/*
	 * Put L2 into direct mapped mode, this bank only.
	 */
	mov	N2_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0xa9.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, N2_L2_BANK_MASK, %g2	! get reg offset from paddr
	or	%g1, %g2, %g1			! include offset in reg addr

	ldx	[%g1], %g2			! %g2=prev L2_CTL_REG contents
	or	%g2, N2_L2CR_DMMODE, %g5	! .
	stx	%g5, [%g1]			! .
	membar	#Sync				! (PRM says not needed)

	/*
	 * Flush all 16 ways (all possible locations of the data).
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
	SET_SIZE(n2_l2_flushentry)
#endif	/* lint */

/*
 * This routine does a displacement flush of an entry specified by it's
 * physical address, from the Niagara-II L2$ (in hypervisor mode).
 *
 * A normal (LRU replacement) L2-cache line install goes as follows:
 *
 *	PA[32:6] -> [hash-logic] -> [LRU picks way bits] -> L2-index[21:6]
 *
 * In order to choose addresses within the displacement buffer that
 * correspond to the 16 possible locations of the line to be displaced
 * the effect of the way bits needs to be accounted for.
 *
 * Since hashing is just using xor operations, the original address bits
 * which are both way bits and part of the IDX, are xor'd.  Then the new
 * way bits for that location are xor'd as well to remove their effect when
 * the address passes through the hash logic.
 *
 * NOTE: this routine is used when IDX index hashing is enabled.
 *	 Since both the prefetch-ICE and a displacement flush address
 *	 go through the hash logic, the operations on the addresses to use
 *	 in both cases are similar (see the n2_l2_flushidx_ice() routine).
 *
 * Prototype:
 *
 * void n2_l2_flushidx(caddr_t paddr2flush);
 *
 * Register usage:
 *
 *	%o1 - IDX'd paddr (index) to flush from the cache
 *	%o2 - counter for way access
 *	%o3 - way increment value
 *	%o4 - pointer to debug buffer (otherwise unused)
 *	%g1 - temp
 *	%g2 - temp then value read from the L2_CTL_REG (for DM mode)
 *	%g3 - flushaddr
 *	%g4 - mask for IDX way bits
 *	%g5 - temp then DRAM_CTL_REG value l2$ map mode valu then temp
 *	%g6 - FLUSHBASE_ADDR
 *	%g7 - temp
 */
#if defined(lint)
/* ARGSUSED */
void
n2_l2_flushidx(caddr_t paddr2flush)
{}
#else
	.align	256
	ENTRY_NP(n2_l2_flushidx)

	/*
	 * Mask IDX'd addr so only the index bits remain, then add flush BA.
	 */
	set	0x3ffc0, %g5			! PA mask for bank and set
	and	%o1, %g5, %g3			! PA[17:6] req'd way = PA[21:18]
	
	L2_FLUSH_BASEADDR(%g6, %g1)		! put flush BA into a reg (%g6)
	add	%g3, %g6, %g3			! %g3 = first flush addr to use

	/*
	 * Now the initial (way bits = 0) flush address is IDX'd here
	 * in order to cancel out the IDX which occurs when it goes into
	 * the L2 as a normal load access.
	 *
	 * In practice the flush BA is zero so this step is not strictly
	 * necessary, but is performed here for future-proofing.
	 */
	mov	0xf, %g7			! %g7 = mask of all way bits
	sllx	%g7, N2_L2_WAY_SHIFT, %g7	! .
	andn	%g3, %g7, %g3			! clear way bits from flush addr

	N2_PERFORM_IDX_HASH(%g3, %g2, %g1)	! %g3 = IDX'd flush addr

	mov	0x3, %g4			! %g4 = way mask for IDX [19:18]
	sllx	%g4, N2_L2_WAY_SHIFT, %g4	!   (used in loop below)

	mov	(N2_NUM_L2_WAYS - 1), %o2	! %o2 = number of L2 ways
	mov	1, %o3				! %o3 = L2 way inc. value
	sllx	%o3, N2_L2_WAY_SHIFT, %o3	!   (way shift = 18)

	/*
	 * Put L2 into direct mapped mode, this bank only.
	 */
	mov	N2_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0xa9.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, N2_L2_BANK_MASK, %g2	! get reg offset from paddr
	or	%g1, %g2, %g1			! include offset in reg addr

	ldx	[%g1], %g2			! %g2=prev L2_CTL_REG contents
	or	%g2, N2_L2CR_DMMODE, %g5	! .
	stx	%g5, [%g1]			! .
	membar	#Sync				! (PRM says not needed)

	/*
	 * Flush all 16 ways (all possible locations of the data).
	 */
1:
	ldx	[%g3], %g0			! access flush addr to flush
	membar	#Sync				!  data out to DRAM

	add	%g3, %o3, %g3			! go to next way (add way inc)
	and	%g3, %g4, %g5			! %g5 = new PA[19:18]
	srlx	%g5, 7, %g5			! IDX xor the new way bits
	xor	%g3, %g5, %g3			! .

	brgz	%o2, 1b				! are we done all ways?
	  sub	%o2, 1, %o2			!   decrement count

	stx	%g2, [%g1]			! restore this banks L2$ mode
	membar	#Sync				!  (will flush L2$ buffers)

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_l2_flushidx)
#endif	/* lint */

/*
 * This routine does a flush of the full 4MB Niagara-II L2$ in hypervisor
 * mode using the (new for Niagara-II) prefectch-ICE (Invalidate Cache
 * Entry) instruction.
 *
 * NOTE: this routine is immune to the L2-cache IDX setting since all
 *	 indexes are run through to do the full flush.
 *
 * Prototype:
 *
 * void n2_l2_flushall_ice(void);
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
 *	%g4 - unused
 *	%g5 - unused
 *	%g6 - address to use for each bank
 *	%g7 - unused
 */
#if defined(lint)
void
n2_l2_flushall_ice(void)
{}
#else
	.align	128
	ENTRY_NP(n2_l2_flushall_ice)

	mov	N2_L2_PREFETCHICE_MSB, %g1	! set the key field of addr
	sllx	%g1, 32, %g6			! .

	add	%g0, N2_NUM_L2_WAYS, %g2	! %g2 = way counter (16 ways)

1: ! n2_flush_ways:
	add	%g0, 512, %g3			! %g3 = lines per way, per bank

2: ! n2_flush_rows:
	prefetch [%g6+0x00], INVALIDATE_CACHE_LINE ! must flush all 8 banks
	prefetch [%g6+0x40], INVALIDATE_CACHE_LINE
	prefetch [%g6+0x80], INVALIDATE_CACHE_LINE
	prefetch [%g6+0xc0], INVALIDATE_CACHE_LINE

	prefetch [%g6+0x100], INVALIDATE_CACHE_LINE
	prefetch [%g6+0x140], INVALIDATE_CACHE_LINE
	prefetch [%g6+0x180], INVALIDATE_CACHE_LINE
	prefetch [%g6+0x1c0], INVALIDATE_CACHE_LINE
        membar #Sync
	
	/*
	 * XXX	really this routine should do load from each
	 *	bank to ensure completion (a diag load works too).
	 *	Only need to do this once for each bank at the end...
	 */

	sub	%g3, 1, %g3			! dec to next line set
	brnz	%g3, 2b
	  add	%g6, 0x200, %g6
	sub	%g2, 1, %g2			! dec to next way
	brnz,a	%g2, 1b
	  nop

	mov	%g0, %o0			! all done, return PASS
	done					! .
	SET_SIZE(n2_l2_flushall_ice)
#endif	/* lint */

/*
 * This routine does a displacement flush of the full 4MB Niagara-II L2$ but
 * unlike most of the routines in this file it is run in kernel mode (not
 * hyperprivileged mode).
 *
 * Prototype:
 *
 * void n2_l2_flushall_kmode_asm(caddr_t flushbase, uint64_t cachesize,
 *					uint64_t linesize);
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
n2_l2_flushall_kmode_asm(caddr_t flushbase, uint64_t cachesize,
				uint64_t linesize)
{}
#else
	.align	64
	ENTRY_NP(n2_l2_flushall_kmode_asm)
1:
	subcc	%o1, %o2, %o1			! go through flush region
	bg,pt	%xcc, 1b			!   accessing each address	
	  ldxa	[%o0 + %o1]ASI_REAL_MEM, %g0

	retl					! return PASS
	  mov %g0, %o0				! .
	SET_SIZE(n2_l2_flushall_kmode_asm)
#endif	/* lint */

/*
 * This routine does a flush of L2$ an entry specified by it's physical
 * address in hypervisor mode using the (new for Niagara-II) prefectch-ICE
 * (Invalidate Cache Entry) instruction.
 *
 * NOTE: this ICE flush routine does NOT take into account the IDX hashing
 *	 and so should only be called/used if index hashing is disabled.
 *	 The n2_l2_flushidx_ice() routine is to be used when IDX is enabled.
 *
 * Prototype:
 *
 * void n2_l2_flushentry_ice(caddr_t paddr2flush);
 *
 * Register usage:
 *
 *	%o1 - paddr to flush from the cache
 *	%o2 - counter for way access
 *	%o3 - way increment value
 *	%o4 - unused
 *	%g1 - temp
 *	%g2 - value read from the DRAM_CTL_REG
 *	%g3 - address values for the prefetch invalidate
 *	%g4 - unused
 *	%g5 - unused
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
void
n2_l2_flushentry_ice(caddr_t paddr2flush)
{}
#else
	.align	64
	ENTRY_NP(n2_l2_flushentry_ice)

	/*
	 * Determine the initial L2 flush index for the specified paddr.
	 */
	set	0x3ffc0, %g3			! PA mask for bank and set
	and	%g3, %o1, %g3			! PA[17:6] req'd way = PA[21:18]

	mov	N2_L2_PREFETCHICE_MSB, %g1	! set the key field of addr
	sllx	%g1, 32, %g1			! .
	or	%g3, %g1, %g3			! %g3 = first address to use

	mov	N2_NUM_L2_WAYS, %o2		! %o2 = number of L2 ways
	mov	1, %o3				! %o3 = L2 way inc. value
	sllx	%o3, N2_L2_WAY_SHIFT, %o3	! .

	/*
	 * Flush all 16 ways (all possible locations of the data).
	 */
1:
	prefetch [%g3], INVALIDATE_CACHE_LINE	! use prefetch to clear line
        membar #Sync				! .

	sub	%o2, 1, %o2			! decrement count
	brnz,a	%o2, 1b				! are we done all ways?
	  add	%g3, %o3, %g3			! go to next way

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_l2_flushentry_ice)
#endif	/* lint */

/*
 * This routine does a flush of L2$ an entry specified by it's physical
 * address in hypervisor mode using the (new for Niagara-II) prefectch-ICE
 * (Invalidate Cache Entry) instruction.
 *
 * This routine is used in place of the above n2_l2_flushentry_ice() routine
 * when L2$ index hashing (IDX) is enabled.  The inpar is expected to be the
 * original non-IDX'd address, this is important because the way bits are part
 * or the IDX calculation (of course) and the prefetch-ICE instruction will
 * have it's bits IDX'd by the HW automatically.
 *
 * A normal (LRU replacement) L2-cache line install goes as follows:
 *
 *	PA[32:6] -> [hash-logic] -> [LRU picks way bits] -> L2-index[21:6]
 *
 * The prefectch-ICE instruction index gets hashed as well as follows:
 *
 *	PA[32:6] + ICE_KEY -> [hash-logic] -> L2-index[21:6]
 *
 * So in order to access the correct 16 ways in the cache with the prefetch-ICE
 * instruction some work has to be done to the ICE-index used.  What needs
 * to be done is the original [19:18] bits need to be xor'd with [12:11]
 * since that is what happened to the data as it entered the cache, then
 * for each index the new [19:18] way bits need to be xor'd with [12:11] to
 * cancel their effect on the HW performed hash calculation.
 *
 * Prototype:
 *
 * void n2_l2_flushidx_ice(caddr_t paddr2flush);
 *
 * Register usage:
 *
 *	%o1 - non-IDX'd paddr to flush from the cache
 *	%o2 - counter for way access
 *	%o3 - way increment value
 *	%o4 - pointer to debug buffer (otherwise unused)
 *	%g1 - temp
 *	%g2 - used for debug code only
 *	%g3 - index values for the prefetch invalidate
 *	%g4 - way mask for IDX (bits [19:18])
 *	%g5 - xor value for index[12:11] of IDX
 *	%g6 - unused
 *	%g7 - temp
 */
#if defined(lint)
/* ARGSUSED */
void
n2_l2_flushidx_ice(caddr_t paddr2flush)
{}
#else
	.align	128
	ENTRY_NP(n2_l2_flushidx_ice)

	/*
	 * Determine the initial L2 flush index for the specified paddr.
	 * Since the prefetch-ICE goes through the IDX logic the paddr inpar
	 * accepted by this routine must be the non IDX'd paddr.
	 */
	sllx	%o1, 31, %g3			! remove MSB bits but keep all
	srlx	%g3, 31, %g3			!   bits used by idx (PA[32:6])

	mov	0x3, %g4			! %g4 = way mask for IDX [19:18]
	sllx	%g4, N2_L2_WAY_SHIFT, %g4	! .

	and	%g3, %g4, %g5			! mask orig way bits for xor
	srlx	%g5, 7, %g5			! shift masked bits for xor
	xor	%g3, %g5, %g3			! do xor for orig [12:11]

	mov	0xf, %g7			! clear all way bits from PA
	sllx	%g7, N2_L2_WAY_SHIFT, %g7	! .
	andn	%g3, %g7, %g3			! .
	
	mov	N2_L2_PREFETCHICE_MSB, %g1	! set the key field of addr
	sllx	%g1, 32, %g1			! .
	or	%g3, %g1, %g3			! %g3 = first address to use

	mov	(N2_NUM_L2_WAYS - 1), %o2	! %o2 = number of L2 ways
	mov	1, %o3				! %o3 = L2 way inc. value
	sllx	%o3, N2_L2_WAY_SHIFT, %o3	!   (way shift = 18)

#ifdef	L2_DEBUG_BUFFER
	stx	%g3, [%o4 + 0xa0]		! first ICE index to use
	stx	%o2, [%o4 + 0xa8]		! num of L2 ways
	stx	%o3, [%o4 + 0xb0]		! the offset
#endif
	/*
	 * Flush all 16 ways (all possible locations of the data).
	 * Notice how the the incremented way value [19:18] affects the set.
	 * And that the 0th way does not need to be modified (xor of 0x0).
	 */
1:
	/*
	 * This bunch of code is to dump out the contents of each L2 line.
	 * The main thing is that the physical address must be IDX'd to
	 * find the index that we want to look at.
	 *
	 * %g3 contains the address for the ICE flush.  This IDX calculation
	 * does the extra step of removing the ICE-key value from the top.
	 */
#ifdef	L2_DEBUG_BUFFER
	mov	0x1f, %g7			! make mask for IDX top
	sllx	%g7, 28, %g7			!   bits[32:28]
	and	%g3, %g7, %g7			! get the top address bits
	srlx	%g7, 15, %g7			! shift 'em down
	xor	%g3, %g7, %g2			! %g2 = result of first xor

	sllx	%g3, 32, %g2			! shift key and top out of addr
	srlx	%g2, 32, %g2			! .

	mov	0x3, %g7			! make mask for IDX bottom
	sllx	%g7, 18, %g7			!   bits[19:18]
	and	%g3, %g7, %g7			! get bottom address bits
	srlx	%g7, 7, %g7			! shift 'em down
	xor	%g2, %g7, %g2			! %g2 = IDX'd address

	mov	N2_L2_DIAG_DATA_MSB, %g7	! set select bits
	sllx	%g7, 32, %g7			! .
	set	0x3ffff8, %g1			! mask paddr to get $line
	and	%g2, %g1, %g1			!  way, set, bank (is PA[21:3])
	or	%g1, %g7, %g1			! %g1 = L2_DIAG_DATA

	ldx	[%g1], %g2			! read back L2 data (via reg)
	sllx	%g2, 1, %g2			! shift it so it's readable

	sllx	%o2, 3, %o0			! use counter debug offset
	add	%o4, %o0, %g7			! %g7 = debug buffer offset
	stx	%g2, [%g7]			! the data at this index
#endif
	prefetch [%g3], INVALIDATE_CACHE_LINE	! use prefetch to clear line
        membar #Sync				! .

	add	%g3, %o3, %g3			! go to next way (add way inc)
	and	%g3, %g4, %g5			! %g5 = new PA[19:18]
	srlx	%g5, 7, %g5			! IDX xor the new way bits
	xor	%g3, %g5, %g3			! .

	brgz	%o2, 1b				! are we done all ways?
	  sub	%o2, 1, %o2			!   decrement count

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(n2_l2_flushidx_ice)
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
 * int n2_install_tlb_entry(uint64_t paddr, uint64_t raddr);
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
n2_install_tlb_entry(uint64_t paddr, uint64_t raddr)
{}
#else
	.align	64
	ENTRY_NP(n2_install_tlb_entry)

	/*
	 * Build the tag (for tag access reg) for the TLB load.
	 * Note that context = 0 = KCONTEXT.
	 */
	set	((1 << 13) - 1), %g2		! VA/ctx mask for tag access
	and	%g0, %g2, %g0			! mask ctx (force KCONTEXT)
	andn	%o2, %g2, %g2			! %g2 = complete tag
	mov	MMU_TAG_ACCESS, %g1		! %g1 = tag access reg

	/*
	 * Build sun4v format tte - valid, cacheable, write, priv, size = 4M
	 * (and set the lock bit which is used by SW only).
	 * Note that this code assumes that VA bits [63:40] are zero.
	 */
	mov	1, %g5				! set Valid bit	and size
	sllx	%g5, 63, %g5			! .
!	sllx	%g3, NI_TTE4V_L_SHIFT, %g3	! and NI lock bit for later
	or	%g5, TTE4V_CP | TTE4V_CV, %g5	! set cacheable bits
	or	%g5, TTE4V_P | TTE4V_W, %g5	! also set priv and write bits
	or	%g5, TTE4V_SZ_4M, %g5		! set the size to 4M
!	or	%g5, %g3, %g5			! finally set lock bit (sw)
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
	sllx	%g3, 10, %g3			! .
	stxa	%g5, [%g3]ASI_DTLB_DATA_IN	! do TLB load (REAL bit set)
	membar	#Sync				! .

	mov	%g5, %o1			! return built tte as value
	mov	%g0, %o0			! clear return status
	done					! .
	SET_SIZE(n2_install_tlb_entry)
#endif	/* lint */
