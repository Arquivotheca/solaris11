/*
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Rainbow Falls (UltraSPARC-RF aka KT) error injector assembly and
 * hyperprivileged assembly routines.
 *
 * NOTE: the functions in this file are grouped according to type and
 *	 are therefore not in alphabetical order unlike other files.
 */

#include <sys/memtest_v_asm.h>
#include <sys/memtest_kt_asm.h>

/*
 * Coding rules for sun4v routines that run in hypervisor mode:
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
 *	   L2 cache) so flushing L2$ also invalidates d$ and i$.
 */

/*
 * K/T cache details (ALL are PIPT) and per core (shared by 8 strands) except
 * the L2-cache which is shared across all 16 cores.
 *
 *	Cache	Size	Ways	sz/way	linesz	instrs/line	num bytes
 *	------------------------------------------------------------------
 *	L2	6MB	24	256KB	64B	16		0x60.0000
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
 * L2_FLUSH_BASEADDR - get a 6MB-aligned DRAM address for L2$ flushing.
 * The assumption is that %htba contains a valid DRAM address
 * for the current machine configuration.  Round it down to a 6MB
 * boundary to use as a base address for L2$ flushing.
 *
 * NOTE: the macro is actually using an 8MB aligned region since
 *	 6MB is not a power of two.
 */
#define	L2_FLUSH_BASEADDR(addr, scr)		\
	rdhpr	%htba, addr			;\
	set	(8 * 1024 * 1024) - 1, scr	;\
	andn	addr, scr, addr

/*
 * This macro performs the L2 index hashing (IDX) on a provided
 * physical address.  The hashed address will be returned in the
 * first variable (containing the original address).
 *
 *	 Index hashing performs the following operations:
 *		PA[17:13] = PA[32:28] xor PA[17:13]
 *		PA[12:11] = PA[19:18] xor PA[12:11]
 *
 * NOTE: KT/RF uses the same hashing algorithm as N2/VF.
 *	 Could use a common MACRO instead of separate ones.
 */
#define	KT_PERFORM_IDX_HASH(addr, lomask, himask)	\
	mov	0x1f, himask				;\
	sllx	himask, 28, himask			;\
							;\
	mov	0x3, lomask				;\
	sllx	lomask, KT_L2_WAY_SHIFT, lomask		;\
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
 * NOTE: the K/T park and unpark routines are based on the N2 versions,
 *	 but K/T has twice as many cores (16) with 8 strands and therefore
 *	 a number of the ASIs have two regs at an offset of 0x80 compared
 *	 to the single register that N2 had (0x80 is core_ID MSB).
 *
 * Register usage:
 *
 *	scr0 - temp for ASI VAs
 *	scr1 - temp then value read from status reg to check running strands
 *	scr2 - the bit representing the current strand
 *	scr3 - the strands to park (all local strands but current)
 *	scr4 - mask of the eight strands on current core
 */
#define	KT_PARK_SIBLING_STRANDS(scr0, scr1, scr2, scr3, scr4)	\
	mov	ASI_CMT_STRAND_ID, scr0		;\
	ldxa	[scr0]ASI_CMT_CORE_REG, scr1	;\
	and	scr1, 0x3f, scr1		;\
	mov	1, scr2				;\
	sllx	scr2, scr1, scr2		;\
	orn	%g0, scr2, scr3			;\
						;\
	and	scr1, 0x38, scr1		;\
	mov	KT_LOCAL_STRAND_MASK, scr4	;\
	sllx	scr4, scr1, scr4		;\
	and	scr3, scr4, scr3		;\
						;\
	ldxa	[scr0]ASI_CMT_CORE_REG, scr1	;\
	and	scr1, 0x80, scr1		;\
	add	scr1, ASI_CORE_RUNNING_W1C, scr0;\
	stxa	scr3, [scr0]ASI_CMT_REG		;\
						;\
	add	scr1, ASI_CORE_RUNNING_STS, scr0;\
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
 *	scr0 - temp for ASI VAs
 *	scr1 - temp then value read from status reg to check running strands
 *	scr2 - the strands to unpark (all local strands but current)
 *	scr3 - mask of the eight strands on current core
 */
#define	KT_UNPARK_SIBLING_STRANDS(scr0, scr1, scr2, scr3)	\
	mov	ASI_CMT_STRAND_ID, scr0		;\
	ldxa	[scr0]ASI_CMT_CORE_REG, scr1	;\
	and	scr1, 0x3f, scr1		;\
	mov	1, scr2				;\
	sllx	scr2, scr1, scr2		;\
	orn	%g0, scr2, scr2			;\
						;\
	and	scr1, 0x38, scr1		;\
	mov	KT_LOCAL_STRAND_MASK, scr3	;\
	sllx	scr3, scr1, scr3		;\
	and	scr2, scr3, scr2		;\
						;\
	ldxa	[scr0]ASI_CMT_CORE_REG, scr1	;\
	and	scr1, 0x80, scr1		;\
	add	scr1, ASI_CORE_RUNNING_W1S, scr0;\
	stxa	scr2, [scr0]ASI_CMT_REG		;\
						;\
	add	scr1, ASI_CORE_RUNNING_STS, scr0;\
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
 * There is one DRAM_ERROR_INJECT_REG in each of the two DRAM branches,
 * (aka channels, MCUs) the bank register sets are available at an offset
 * of 4096 (0x1000 based on PA[6]).
 *
 * NOTE: the entire L2$ should be flushed immediately before this function.
 *
 * NOTE: this routine is immune to the L2-cache IDX setting since no
 *	 indexes are used only actual addresses for the displacement.
 *	 This is true even when using the prefetch-ICE instruction because
 *	 the HW automatically applies the hashing for prefetch-ICE.
 *
 * NOTE: this routine does not take the enabled L2 banks (and therefore
 *	 the enabled MCUs) into account for the prefetch-ICE index/address
 *	 and it also assumes that IDX hashing is enabled.
 *	 This is a good assumption (all banks available and hashing enabled)
 *	 for systems produced close to RR but not necessarily for a debug
 *	 routine.  So only use on a system with all banks enabled.
 *
 * NOTE: this routine is based on the Niagara-II version n2_inj_memory().
 *
 * DRAM_ERROR_INJ_REG (0x430)
 *
 * +-----------+-----+-------+-----------+-------+----------+
 * | Resverved | Enb | SShot | Direction | Chunk | ECC Mask |
 * +-----------+-----+-------+-----------+-------+----------+
 *     63:37     36     35        34       33:32     31:0
 */

/*
 * Prototype:
 *
 * int kt_inj_memory_debug(uint64_t paddr, uint64_t eccmask, uint64_t offset,
 *				uint64_t *debug);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - eccmask, sshot, direction, and chunk
 *	%o3 - register offset and cpu node ID
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
kt_inj_memory_debug(uint64_t paddr, uint64_t eccmask, uint64_t offset,
			uint64_t *debug)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(kt_inj_memory_debug)

	/*
	 * Build DRAM EI register address.
	 */
	mov	KT_DRAM_CSR_BASE_MSB, %g4	! set DRAM select bits
	sllx	%g4, 32, %g4			! .
	or	%g4, KT_DRAM_ERROR_INJ_REG, %g4	! add injection offset
	add	%g4, %o3, %g6			! add register offset
						! %g6 = complete EI reg addr
#ifdef MEM_DEBUG_BUFFER
	stx	%o1, [%o4 + 0x0]		! paddr
	stx	%g6, [%o4 + 0x8]		! addr of DRAM_EI_REG
#endif

#ifdef MEM_DEBUG_BUFFER
	/*
	 * NOTE: using the IDX flush method in the debug code.
	 *
	 * Determine the L2 flush-ICE index for the specified paddr.
	 * Since the cache will be in DM-mode only need the one index.
	 *
	 * NOTE: the effect of the plane_flip bit is ignored here.
	 */
	sllx	%o1, 31, %g3			! remove MSB bits but keep all
	srlx	%g3, 31, %g3			!   bits used by idx (PA[32:6])

	mov	KT_L2_PREFETCHICE_MSB, %g1	! set the key field of ICE-index
	sllx	%g1, 32, %g1			! .
	or	%g3, %g1, %g3			! %g3 = ICE flush index to use
#else
	/*
	 * Determine the L2 flush addr for the specified paddr.
	 */
	set	0x7ffff8, %g3			! PA mask for bank, set, way
	and	%g3, %o1, %g3			! PA[22:6] req'd, is PA[22:3]

	L2_FLUSH_BASEADDR(%g4, %g5)		! put flush BA into a reg (%g4)
	or	%g3, %g4, %g3			! %g3 = the flush addr to use
#endif
	/*
	 * Put L2 into direct mapped mode, this bank only.
	 */
	mov	KT_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0x829.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, KT_L2_BANK_MASK, %g2	! get reg offset from paddr
	or	%g1, %g2, %g1			! include offset in reg addr

	ldx	[%g1], %g2			! %g2=prev L2_CTL_REG contents
	or	%g2, KT_L2CR_DMMODE, %g5	! .
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
	 *
	 * NOTE: actually don't IDX-ify it if we are in a reduced
	 *	 bank mode since hashing can't be enabled.
	 *	 Comment out the next 10 instrs and substitute
	 *	 %o1 for %g5 (the adjusted addr) in instr 14 below
	 *	 to remove the IDX hashing for the debug output.
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

	mov	KT_L2_DIAG_DATA_MSB, %g4	! set select bits
	sllx	%g4, 32, %g4			! .
	set	0x7ffff8, %g7			! mask paddr to get $line
	and	%g5, %g7, %g7			!  way, set, bank (is PA[22:3])
	or	%g7, %g4, %g7			! %g7 = L2_DIAG_DATA
#endif
	ldx	[%o1], %g4			! touch the data

#ifdef MEM_DEBUG_BUFFER
	stx	%g4, [%o4 + 0x28]		! contents of mem location
	stx	%o2, [%o4 + 0x30]		! value to write to DRAM_EI_REG
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

	mov	1, %g4				! flip the OddEven field
	sllx	%g4, 23, %g4			!   to get other half of data
	xor	%g7, %g4, %g7			!   .
	ldx	[%g7], %g4			! read the data at this location
	sllx	%g4, 1, %g4			! shift it so it's readable
	stx	%g4, [%o4 + 0x48]		! data at this tag location
#endif
	stx	%o2, [%g6]			! write value to DRAM EI reg
	membar	#Sync				!  (error on next DRAM store)

	nop; nop; nop; nop;			! req'd for HW slow path

	ldx	[%g6], %o2			! read back reg to ensure it
	membar	#Sync				!  completed (as per PRM)

#ifdef	MEM_DEBUG_BUFFER
	/*
	 * Using the prefetch-ICE write-back method for debug case.
	 *
	 * NOTE: this routine is not checking the number of enabled L2 banks
	 *	 which may or may not affect the address to use with the
	 *	 prefetch-ICE instruction.
	 */
	prefetch [%g3], INVALIDATE_CACHE_LINE	! use prefetch-ICE to flush
	membar	#Sync				!  data out to DRAM with error
#else
	ldx	[%g3], %g5			! access flush addr to flush
	membar	#Sync				!  data out to DRAM with error
#endif

#ifdef	MEM_DEBUG_BUFFER
	ldx	[%g6], %o2			! read inj reg to see if clear
	membar	#Sync				!   but likely NOT clear yet
#endif
	/*
	 * Clear ecc mask if sshot mode to reduce the possibility that
	 * an error will be injected elsewhere if not triggered above.
	 */
	mov	1, %g4				! create mask for sshot bit
	sllx	%g4, 35, %g4			! .
	and	%g4, %o2, %g4			! check if sshot is set
	cmp	%g4, %g0			! .
	bnz,a	%xcc, 4f			! if it is set then
	  stx	%g0, [%g6]			!   clear the DRAM EI reg
4:

#ifdef	MEM_DEBUG_BUFFER
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

	SET_SIZE(kt_inj_memory_debug)
#endif	/* lint */

/*
 * This routine is used to corrupt the ecc bits of external memory (DRAM).
 * It is similar to the above kt_inj_memory_debug() routine but a new inpar
 * is used in the first location of the debug buffer to accept different
 * address masks for the registers which depend on the available DRAM
 * and L2 banks.
 *
 * This routine uses adjusts the address used with prefetch-ICE to do
 * the flush based on the number of available banks.  The original
 * routine (above) does not do this adjustment.
 */

/*
 * Prototype:
 *
 * int kt_inj_memory_debug_ice(uint64_t paddr, uint64_t eccmask,
 *			uint64_t offset, uint64_t *debug);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - eccmask and sshot
 *	%o3 - register offset and cpu node ID
 *	%o4 - debug buffer (first location has address mask for enabled banks)
 *	%g1 - temp then address of L2_CTL_REG
 *	%g2 - temp then saved L2_CTL_REG contents
 *	%g3 - temp then PA mask for the flush
 *	%g4 - temp then data read addr (data not used)
 *	%g5 - temp
 *	%g6 - addr of matching DRAM_ERROR_INJECT_REG
 *	%g7 - saved l2_bank_mask from first location of debug buffer
 */
#if defined(lint)
/* ARGSUSED */
int
kt_inj_memory_debug_ice(uint64_t paddr, uint64_t eccmask, uint64_t offset,
			uint64_t *debug)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(kt_inj_memory_debug_ice)

	/*
	 * Build DRAM EI register address.
	 */
	mov	KT_DRAM_CSR_BASE_MSB, %g4	! set DRAM select bits
	sllx	%g4, 32, %g4			! .
	or	%g4, KT_DRAM_ERROR_INJ_REG, %g4	! add injection offset
	add	%g4, %o3, %g6			! add register offset
						! %g6 = complete EI reg addr
	ldx	[%o4 + 0x0], %g7		! %g7 = saved l2_bank_mask var

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
	 *
	 * NOTE: the effect of the plane_flip bit is ignored here.
	 */
	sllx	%o1, 31, %g3			! remove MSB bits but keep all
	srlx	%g3, 31, %g3			!   bits used by idx (PA[32:6])

	mov	KT_L2_PREFETCHICE_MSB, %g1	! set the key field of ICE-index
	sllx	%g1, 32, %g1			! .
	or	%g3, %g1, %g3			! %g3 = ICE flush index to use
2:

#else
	/*
	 * Determine the L2 flush addr for the specified paddr.
	 */
	set	0x7ffff8, %g3			! PA mask for bank, set, way
	and	%g3, %o1, %g3			! PA[22:6] req'd, is PA[22:3]

	L2_FLUSH_BASEADDR(%g4, %g5)		! put flush BA into a reg (%g4)
	or	%g3, %g4, %g3			! %g3 = the flush addr to use
#endif
	/*
	 * Put L2 into direct mapped mode, this bank only.
	 */
	mov	KT_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0x829.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, %g7, %g2			! get reg offset from paddr
	or	%g1, %g2, %g1			! include offset in reg addr

	ldx	[%g1], %g2			! %g2=prev L2_CTL_REG contents
	or	%g2, KT_L2CR_DMMODE, %g5	! .
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
	 *
	 * NOTE: actually don't IDX-ify it if we are in a reduced
	 *	 bank mode since hashing can't be enabled.
	 *	 Comment out the next 10 instrs and substitute
	 *	 %o1 for %g5 (the adjusted addr) in instr 14 below
	 *	 to remove the IDX hashing for the debug output.
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

	mov	KT_L2_DIAG_DATA_MSB, %g4	! set select bits
	sllx	%g4, 32, %g4			! .
	set	0x7ffff8, %g7			! mask paddr to get $line
	and	%g5, %g7, %g7			!  way, set, bank (is PA[22:3])
	or	%g7, %g4, %g7			! %g7 = L2_DIAG_DATA
#endif
	ldx	[%o1], %g4			! touch the data

#ifdef MEM_DEBUG_BUFFER
	stx	%g4, [%o4 + 0x28]		! contents of mem location
	stx	%o2, [%o4 + 0x30]		! value to write to DRAM_EI_REG
#endif
	ba	6f				! branch to fill prefetch buf
	  mov	%g0, %o0			! set return value to PASS

	/*
	 * Critical section, must fit on single L2$ line.
	 */
	.align	128
4:
	ldx	[%o1], %g4			! load addr to ensure is in $s
	stx	%g4, [%o1]			! store to put line in M state
	membar	#Sync				! .

#ifdef	MEM_DEBUG_BUFFER
	stx	%g7, [%o4 + 0x38]		! addr for L2_DIAG_DATA
	ldx	[%g7], %g4			! read back L2 data (via reg)
	sllx	%g4, 1, %g4			! shift it so it's readable
	stx	%g4, [%o4 + 0x40]		!  should not invoke the error

	mov	1, %g4				! flip the OddEven field
	sllx	%g4, 23, %g4			!   to get other half of data
	xor	%g7, %g4, %g7			!   .
	ldx	[%g7], %g4			! read the data at this location
	sllx	%g4, 1, %g4			! shift it so it's readable
	stx	%g4, [%o4 + 0x48]		! data at this tag location
#endif
	stx	%o2, [%g6]			! write value to DRAM EI reg
	membar	#Sync				!  (error on next DRAM store)

	nop; nop; nop; nop;			! req'd for HW slow path

	ldx	[%g6], %o2			! read back reg to ensure it
	membar	#Sync				!  completed (as per PRM)

#ifdef	MEM_DEBUG_BUFFER
	/*
	 * Using the prefetch-ICE write-back method for debug case.
	 */
	prefetch [%g3], INVALIDATE_CACHE_LINE	! use prefetch-ICE to flush
	membar	#Sync				!  data out to DRAM with error
#else
	ldx	[%g3], %g5			! access flush addr to flush
	membar	#Sync				!  data out to DRAM with error
#endif

#ifdef	MEM_DEBUG_BUFFER
	ldx	[%g6], %o2			! read inj reg to see if clear
	membar	#Sync		
#endif
	/*
	 * Clear ecc mask if sshot still set to reduce the possibility that
	 * an error will be injected elsewhere if not triggered above.
	 */
	mov	1, %g4				! create mask for sshot bit
	sllx	%g4, 35, %g4			! .
	and	%g4, %o2, %g4			! check if sshot is set
	cmp	%g4, %g0			! .
	bnz,a	%xcc, 5f			! if it is set then
	  stx	%g0, [%g6]			!   clear the DRAM EI reg
5:

#ifdef	MEM_DEBUG_BUFFER
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
6:
	nop; nop; nop; nop; nop; nop; nop;
	nop; nop; nop; nop; nop; nop; nop;
	ba	4b
	  nop

	SET_SIZE(kt_inj_memory_debug_ice)
#endif	/* lint */

/*
 * This routine is used to corrupt the ecc bits of external memory (DRAM).
 * It is similar to the above kt_inj_memory_debug() routine but a new inpar
 * is used in the first location of the debug buffer to accept different
 * address masks for the registers which depend on the available DRAM
 * and L2 banks.
 *
 * This routine uses a displacement flush to push the specific data out
 * to DRAM and not the prefetch-ICE instruction.
 *
 * Using a displacement flush is especially useful on systems which are
 * running in a reduced bank mode because the prefetch-ICE addressing
 * adjustments are not working (in development) and this routine can
 * be used in place of the default debug injection routine.
 *
 * NOTE: this routine will ONLY work if L2 index hashing is disabled as
 *	 it must be on reduced bank systems since displacement address
 *	 is not being hashed.
 */

/*
 * Prototype:
 *
 * int kt_inj_memory_debug_disp(uint64_t paddr, uint64_t eccmask,
 *			uint64_t offset, uint64_t *debug);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - eccmask and sshot
 *	%o3 - register offset and cpu node ID
 *	%o4 - debug buffer (first location has address mask for enabled banks)
 *	%g1 - temp then address of L2_CTL_REG
 *	%g2 - temp then saved L2_CTL_REG contents
 *	%g3 - temp then PA mask for the flush
 *	%g4 - temp then data read addr (data not used)
 *	%g5 - temp
 *	%g6 - addr of matching DRAM_ERROR_INJECT_REG
 *	%g7 - saved l2_bank_mask from first location of debug buffer
 */
#if defined(lint)
/* ARGSUSED */
int
kt_inj_memory_debug_disp(uint64_t paddr, uint64_t eccmask, uint64_t offset,
			uint64_t *debug)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(kt_inj_memory_debug_disp)

	/*
	 * Build DRAM EI register address.
	 */
	mov	KT_DRAM_CSR_BASE_MSB, %g4	! set DRAM select bits
	sllx	%g4, 32, %g4			! .
	or	%g4, KT_DRAM_ERROR_INJ_REG, %g4	! add injection offset
	add	%g4, %o3, %g6			! add register offset
						! %g6 = complete EI reg addr
	ldx	[%o4 + 0x0], %g7		! %g7 = saved l2_bank_mask var

#ifdef MEM_DEBUG_BUFFER
	stx	%o1, [%o4 + 0x0]		! paddr
	stx	%g6, [%o4 + 0x8]		! addr of DRAM_EI_REG
#endif

	/*
	 * Determine the L2 flush addr for the specified paddr.
	 */
	set	0x7ffff8, %g3			! PA mask for bank, set, way
	and	%g3, %o1, %g3			! PA[22:6] req'd, is PA[22:3]

	L2_FLUSH_BASEADDR(%g4, %g5)		! put flush BA into a reg (%g4)
	or	%g3, %g4, %g3			! %g3 = the flush addr to use

	/*
	 * Put L2 into direct mapped mode, this bank only.
	 */
	mov	KT_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0x829.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, %g7, %g2			! get reg offset from paddr
	or	%g1, %g2, %g1			! include offset in reg addr

	ldx	[%g1], %g2			! %g2=prev L2_CTL_REG contents
	or	%g2, KT_L2CR_DMMODE, %g5	! .
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
	 *
	 * NOTE: actually don't IDX-ify it since this routine is
	 *	 most useful for testing on a reduced bank mode
	 *	 system which can't enable index hashing.
	 */
	mov	KT_L2_DIAG_DATA_MSB, %g4	! set select bits
	sllx	%g4, 32, %g4			! .
	set	0x7ffff8, %g7			! mask paddr to get $line
	and	%o1, %g7, %g7			! .
	or	%g7, %g4, %g7			! %g7 = L2_DIAG_DATA
#endif
	ldx	[%o1], %g4			! touch the data

#ifdef MEM_DEBUG_BUFFER
	stx	%g4, [%o4 + 0x28]		! contents of mem location
	stx	%o2, [%o4 + 0x30]		! value to write to DRAM_EI_REG
#endif
	ba	6f				! branch to fill prefetch buf
	  mov	%g0, %o0			! set return value to PASS

	/*
	 * Critical section, must fit on single L2$ line.
	 */
	.align	64
4:
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

	ldx	[%g3], %g5			! access flush addr to flush
	membar	#Sync				!  data out to DRAM with error

#ifdef	MEM_DEBUG_BUFFER
	ldx	[%g6], %o2			! read inj reg to see if clear
	membar	#Sync		
#endif
	/*
	 * Clear ecc mask if sshot mode to reduce the possibility that
	 * an error will be injected elsewhere if not triggered above.
	 */
	mov	1, %g4				! create mask for sshot bit
	sllx	%g4, 35, %g4			! .
	and	%g4, %o2, %g4			! check if sshot is set
	cmp	%g4, %g0			! .
	bnz,a	%xcc, 5f			! if it is set then
	  stx	%g0, [%g6]			!   clear the DRAM EI reg
5:

#ifdef	MEM_DEBUG_BUFFER
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
6:
	nop; nop; nop; nop; nop; nop; nop;
	nop; nop; nop; nop; nop; nop; nop;
	ba	4b
	  nop

	SET_SIZE(kt_inj_memory_debug_disp)
#endif	/* lint */

/*
 * This routine is used to corrupt the ecc bits of external memory (DRAM).
 * It is similar to the above kt_inj_memory_debug() routine but the debug
 * code is removed and a new inpar added to accept differnt address masks
 * for the registers which depend on the available DRAM and L2 banks.
 *
 * This routine uses a displacement flush to push the specific data out
 * to DRAM and not the prefetch-ICE instruction.
 */

/*
 * Prototype:
 *
 * int kt_inj_memory_disp(uint64_t paddr, uint64_t eccmask, uint64_t offset,
 *			uint64_t bankmask);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - eccmask and sshot
 *	%o3 - register offset and cpu node ID
 *	%o4 - address mask for enabled banks
 *	%g1 - temp then address of L2_CTL_REG
 *	%g2 - temp then saved L2_CTL_REG contents
 *	%g3 - temp then PA mask for the flush
 *	%g4 - temp then data read addr (data not used)
 *	%g5 - temp
 *	%g6 - addr of matching DRAM_ERROR_INJECT_REG
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
kt_inj_memory_disp(uint64_t paddr, uint64_t eccmask, uint64_t offset,
			uint64_t bankmask)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(kt_inj_memory_disp)

	/*
	 * Build DRAM EI register address.
	 */
	mov	KT_DRAM_CSR_BASE_MSB, %g4	! set DRAM select bits
	sllx	%g4, 32, %g4			! .
	or	%g4, KT_DRAM_ERROR_INJ_REG, %g4	! add injection offset
	add	%g4, %o3, %g6			! add register offset
						!   %g6 = complete EI reg addr
	/*
	 * Determine the L2 flush addr for the specified paddr.
	 */
	set	0x7ffff8, %g3			! PA mask for bank, set, way
	and	%g3, %o1, %g3			! PA[22:6] req'd, is PA[22:3]

	L2_FLUSH_BASEADDR(%g4, %g5)		! put flush BA into a reg (%g4)
	or	%g3, %g4, %g3			! %g3 = the flush addr to use

	/*
	 * Put L2 into direct mapped mode, this bank only.
	 */
	mov	KT_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0x829.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, %o4, %g2			! get L2 reg offset from paddr
	or	%g1, %g2, %g1			! include offset in reg addr

	ldx	[%g1], %g2			! %g2=prev L2_CTL_REG contents
	or	%g2, KT_L2CR_DMMODE, %g5	! .
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
	 * Clear ecc mask if sshot mode to reduce the possibility that
	 * an error will be injected elsewhere if not triggered above.
	 */
	mov	1, %g4				! create mask for sshot bit
	sllx	%g4, 35, %g4			! .
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

	SET_SIZE(kt_inj_memory_disp)
#endif	/* lint */

/*
 * This routine is used to corrupt the ecc bits of external memory (DRAM).
 * It is similar to the above kt_inj_memory_disp() routine but the
 * displacement flush is replaced by the prefectch-ICE instruction 
 * in order to flush the specific data out to DRAM.
 *
 * This is used because the displacement flush may not work correctly when
 * IDX mode (index hashing is enabled).
 */

/*
 * Prototype:
 *
 * int kt_inj_memory(uint64_t paddr, uint64_t eccmask, uint64_t offset,
 *			uint64_t bankmask);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - eccmask and sshot
 *	%o3 - register offset and cpu node ID (for DRAM bank/MCU channel)
 *	%o4 - address mask for enabled banks
 *	%g1 - temp then address of L2_CTL_REG
 *	%g2 - temp then saved L2_CTL_REG contents
 *	%g3 - temp then ICE-index for the flush
 *	%g4 - temp then data read addr (data not used)
 *	%g5 - temp
 *	%g6 - addr of matching DRAM_ERROR_INJECT_REG
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
kt_inj_memory(uint64_t paddr, uint64_t eccmask, uint64_t offset,
		uint64_t bankmask)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(kt_inj_memory)

	/*
	 * Build DRAM EI register address.
	 */
	mov	KT_DRAM_CSR_BASE_MSB, %g4	! set DRAM select bits
	sllx	%g4, 32, %g4			! .
	or	%g4, KT_DRAM_ERROR_INJ_REG, %g4	! add injection offset
	add	%g4, %o3, %g6			! add register offset
						!   %g6 = complete EI reg addr
	/*
	 * Determine the L2 flush-ICE index for the specified paddr.
	 * Since the cache will be in DM-mode only need the one index,
	 * and since the flush-ICE goes through the hash-logic it's simply
	 * the PA masked.
	 *
	 * NOTE: the effect of the plane_flip bit is ignored here.
	 */
	sllx	%o1, 31, %g3			! remove MSB bits but keep all
	srlx	%g3, 31, %g3			!   bits used by idx (PA[32:6])

	mov	KT_L2_PREFETCHICE_MSB, %g1	! set the key field of ICE-index
	sllx	%g1, 32, %g1			! .
	or	%g3, %g1, %g3			! %g3 = ICE flush index to use
2:
	/*
	 * Put L2 into direct mapped mode, this bank only.
	 */
	mov	KT_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0x829.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, %o4, %g2			! get reg offset from paddr
	or	%g1, %g2, %g1			! include offset in reg addr

	ldx	[%g1], %g2			! %g2=prev L2_CTL_REG contents
	or	%g2, KT_L2CR_DMMODE, %g5	! .
	stx	%g5, [%g1]			! .
	membar	#Sync				! (PRM says not needed)

	ldx	[%o1], %g4			! touch the data

	ba	4f				! branch to fill prefetch buf
	  mov	%g0, %o0			! set return value to PASS

	/*
	 * Critical section, must fit on single L2$ line.
	 */
	.align	128
3:
	ldx	[%o1], %g4			! load addr to ensure is in $s
	stx	%g4, [%o1]			! store to put line in M state
	membar	#Sync				! .

	stx	%o2, [%g6]			! write value to DRAM EI reg
	membar	#Sync				!  (error on next DRAM store)

	nop; nop; nop; nop;			! req'd for HW slow path

	ldx	[%g6], %o2			! read back reg to ensure it
	membar	#Sync				!  completed (as per PRM)

	prefetch [%g3], INVALIDATE_CACHE_LINE	! use prefetch-ICE to flush
	membar	#Sync				!  data out to DRAM with error

	/*
	 * Clear ecc mask if sshot mode to reduce the possibility that
	 * an error will be injected elsewhere if not triggered above.
	 */
	mov	1, %g4				! create mask for sshot bit
	sllx	%g4, 35, %g4			! .
	and	%g4, %o2, %g4			! check if sshot is set
	cmp	%g4, %g0			! .
	bnz,a	%xcc, 5f			! if it is set then
	  stx	%g0, [%g6]			!   clear the DRAM EI reg
5:
	stx	%g2, [%g1]			! restore this banks L2$ mode
	membar	#Sync				!  (will flush L2$ buffers)

	done					! return PASS

	/*
	 * Fill up the prefetch buffer (full L2$ line).
	 */
	.align	64
4:
	nop; nop; nop; nop; nop; nop; nop;
	nop; nop; nop; nop; nop; nop; nop;
	ba	3b
	  nop

	SET_SIZE(kt_inj_memory)
#endif	/* lint */

/*
 * This routine is used to corrupt the ecc bits of external memory (DRAM).
 * It is very similar to the above kt_inj_memory() routine but it
 * has been simplified to expect that the data has already been
 * installed in the L2 cache (in DM mode).  This means that the following
 * steps have been removed:
 *	- the L2 cache is not placed (and later restored) into DM mode
 *	- the data is not accessed (which would bring it into the caches)
 *
 * The step which puts the data in the modified (M) state was planned
 * to be removed but of course the line needs to be in the M state in
 * order to be flushed out to DRAM (with the error).
 */

/*
 * Prototype:
 *
 * int kt_inj_memory_quick(uint64_t paddr, uint64_t eccmask, uint64_t offset,
 *			uint64_t bankmask);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - eccmask and sshot
 *	%o3 - register offset and cpu node ID (for DRAM bank/MCU channel)
 *	%o4 - bankmask
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
kt_inj_memory_quick(uint64_t paddr, uint64_t eccmask, uint64_t offset,
			uint64_t bankmask)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(kt_inj_memory_quick)

	/*
	 * Build DRAM EI register address.
	 */
	mov	KT_DRAM_CSR_BASE_MSB, %g4	! set DRAM select bits
	sllx	%g4, 32, %g4			! .
	or	%g4, KT_DRAM_ERROR_INJ_REG, %g4	! add injection offset
	add	%g4, %o3, %g6			! add register offset
						!   %g6 = complete EI reg addr
	/*
	 * Determine the L2 flush-ICE index for the specified paddr.
	 * Since the cache will be in DM-mode only need the one index,
	 * and since the flush-ICE goes through the hash-logic it's just
	 * a masked PA.
	 *
	 * NOTE: the effect of the plane_flip bit is ignored here.
	 */
	sllx	%o1, 31, %g3			! remove MSB bits but keep all
	srlx	%g3, 31, %g3			!   bits used by idx (PA[32:6])

	mov	KT_L2_PREFETCHICE_MSB, %g1	! set the key field of ICE-index
	sllx	%g1, 32, %g1			! .
	or	%g3, %g1, %g3			! %g3 = full ICE flush index

	ba	3f				! branch to fill prefetch buf
	  mov	%g0, %o0			! set return value to PASS

	/*
	 * Critical section, must fit on single L2$ line.
	 */
	.align	64
2:
	ldx	[%o1], %g4			! load addr to ensure is in $s
	stx	%g4, [%o1]			! store to put line in M state
	membar	#Sync				! .

	stx	%o2, [%g6]			! write value to DRAM EI reg
	membar	#Sync				!  (error on next DRAM store)

	nop; nop; nop; nop;			! req'd for HW slow path

	ldx	[%g6], %o2			! read back reg to ensure it
	membar	#Sync				!  completed (as per PRM)

	prefetch [%g3], INVALIDATE_CACHE_LINE	! use prefetch-ICE to flush
	membar	#Sync				!  data out to DRAM with error

	/*
	 * Clear ecc mask if sshot still set to reduce the possibility that
	 * an error will be injected elsewhere if not triggered above.
	 */
	mov	1, %g4				! create mask for sshot bit
	sllx	%g4, 35, %g4			! .
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
	ba	2b
	  nop

	SET_SIZE(kt_inj_memory_quick)
#endif	/* lint */

/*
 * This routine is used to corrupt the ecc bits of external memory (DRAM)
 * in a way that the HW should interperet as an intermittent error.  K/T
 * provides a direction bit in it's DRAM injection register which allows
 * errors to be detected only on the read cycle.
 *
 * The routine is very similar to the above kt_inj_memory() routine but it
 * does not bring the data into the caches since a cache miss is required
 * to read the data from DRAM.  This routine expects that the caches have
 * been flushed immediately prior to being called.
 */

/*
 * Prototype:
 *
 * int kt_inj_memory_int(uint64_t paddr, uint64_t eccmask, uint64_t reg_offset,
 *				uint64_t *debug);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - eccmask and sshot
 *	%o3 - register offset and cpu node ID (for DRAM bank/MCU channel)
 *	%o4 - pointer to debug buffer (debug)
 *	%g1 - temp
 *	%g2 - unused
 *	%g3 - unused
 *	%g4 - temp then data read at addr (data not used)
 *	%g5 - temp
 *	%g6 - addr of matching DRAM_ERROR_INJECT_REG
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
kt_inj_memory_int(uint64_t paddr, uint64_t eccmask, uint64_t reg_offset,
			uint64_t *debug)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(kt_inj_memory_int)

	/*
	 * Build DRAM EI register address.
	 */
	mov	KT_DRAM_CSR_BASE_MSB, %g4	! set DRAM select bits
	sllx	%g4, 32, %g4			! .
	or	%g4, KT_DRAM_ERROR_INJ_REG, %g4	! add injection offset
	add	%g4, %o3, %g6			! add register offset
						!   %g6 = complete EI reg addr
#ifdef MEM_DEBUG_BUFFER
	stx	%o1, [%o4 + 0x0]		! paddr
	stx	%g6, [%o4 + 0x8]		! addr of DRAM_EI_REG
	stx	%o2, [%o4 + 0x18]		! value to write to DRAM_EI_REG
#endif
	ba	2f				! branch to fill prefetch buf
	  mov	%g0, %o0			! set return value to PASS

	/*
	 * Critical section, must fit on single L2$ line.
	 */
	.align	64
1:
	stx	%o2, [%g6]			! write value to DRAM EI reg
	membar	#Sync				!  (error on next DRAM load)

	nop; nop; nop; nop;			! req'd for HW slow path

	ldx	[%g6], %o2			! read back reg to ensure it
	membar	#Sync				!  completed (as per PRM)

	ldx	[%o1], %g4			! load addr to produce error
	membar	#Sync				! .

#ifdef	MEM_DEBUG_BUFFER
	ldx	[%g6], %o2			! read inj reg to see if clear
	membar	#Sync		
#endif
	/*
	 * Clear ecc mask if sshot mode to reduce the possibility that
	 * an error will be injected elsewhere if not triggered above.
	 */
	mov	1, %g4				! create mask for sshot bit
	sllx	%g4, 35, %g4			! .
	and	%g4, %o2, %g4			! check if sshot is set
	cmp	%g4, %g0			! .
	bnz,a	%xcc, 3f			! if it is set then
	  stx	%g0, [%g6]			!   clear the DRAM EI reg
3:
#ifdef	MEM_DEBUG_BUFFER
	stx	%o2, [%o4 + 0x20]		! new val read from DRAM_EI_REG
#endif
	done					! return PASS

	/*
	 * Fill up the prefetch buffer (full L2$ line).
	 */
	.align	64
2:
	nop; nop; nop; nop; nop; nop; nop;
	nop; nop; nop; nop; nop; nop; nop;
	ba	1b
	  nop

	SET_SIZE(kt_inj_memory_int)
#endif	/* lint */

/*--------------------- end of mem / start of L2$ routines -----------------*/

/*
 * This routine is used to corrupt the data or the data ecc bits of an L2
 * cache line determined by the paddr ONLY when the cache line is not for
 * an instruction access.  The L2_DIAG_DATA register address is built by
 * this routine, bits [9:6] of the paddr select the cache bank.
 *
 * Note that this routine expects a 64-bit aligned xorpat and shifts it
 * down if the corruption is to be in the upper 32-bit word.  This means
 * that the upper and lower words cannot be corrupted at the same time
 * (and the lower value takes precidence).  This is due to HW limitations.
 *
 * The L2 cache is 6MB with a linesize of 64B which yeilds 98304 (0x18000)
 * lines split into 4096 (0x1000) lines per each of the 24 ways.
 *
 * Note also that setting and restoring the L2$ DM mode has the effect
 * of clearing all the L2$ write buffers.  This occurs even if the mode does
 * not actually change since it is triggered by the control reg write.
 *
 * NOTE: this routine is based on the Niagara-II version n2_inj_l2cache_data().
 *
 * L2_DIAG_DATA (offset 0x0) Access (stride 8, plus use of odd/even field) 
 *
 * +-------+--------+-------+---------+-----+-----+------+------+-------+
 * | Rsvd0 | Select | Rsvd1 | OddEven | Way | Set | Bank | Word | Rsvd2 |
 * +-------+--------+-------+---------+-----+-----+------+------+-------+
 *   63:40   39:32    31:23     23     22:18 17:10  9:6    5:3     2:0
 *
 * Data returned (32-bit half word) as DATA[38:7], ECC[6:0] (same as Niagara-II)
 */

/*
 * Prototype:
 *
 * int kt_inj_l2cache_data(uint64_t paddr, uint64_t xorpat, uint_t checkflag,
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
kt_inj_l2cache_data(uint64_t paddr, uint64_t xorpat, uint_t checkflag,
			uint64_t idx_paddr)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(kt_inj_l2cache_data)

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
	sllx	%g3, 21, %g3			! move result to OddEven field
	andn	%o1, 0x7, %o1			! align paddr for asi access
	andn	%o4, 0x7, %o4			! align idx_paddr for asi access

#ifdef	L2_DEBUG_BUFFER
	stx	%o1, [%o3 + 0x0]		! aligned paddr
	stx	%o4, [%o3 + 0x8]		! aligned idx_paddr
	stx	%o2, [%o3 + 0x10]		! xor pat
#else
	cmp	%o3, %g0			! if check-bit flag == 0
	bz,a,pt	%xcc, 2f			! shift the xorpat to data bits
	  sllx	%o2, 7, %o2			!   (otherwise inj to ecc[6:0])
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
	mov	KT_L2_DIAG_DATA_MSB, %g4	! set reg select bits
	sllx	%g4, 32, %g4			! .
	set	0x7ffff8, %g2			! mask idx_paddr to get $line
	and	%o4, %g2, %g2			!  way, set, bank (is PA[22:3])
	or	%g2, %g3, %g2			! or in the OddEven bit
	or	%g2, %g4, %g2			! %g2 = L2_DIAG_DATA

#ifdef	L2_DEBUG_BUFFER
	stx	%g2, [%o3 + 0x20]		! addr for L2_DIAG_DATA
#endif

	/*
	 * Put L2 into direct mapped mode, this bank only.
	 *
	 * NOTE: although the C-level routine is setting DM mode now,
	 *	 it can be left in so the write buffers can be flushed.
	 */
	mov	KT_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0x829.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, KT_L2_BANK_MASK, %g3	! get reg offset from paddr
	or	%g1, %g3, %g1			! include offset in reg addr

	ldx	[%g1], %g4			! %g4=prev L2_CTL_REG contents
	or	%g4, KT_L2CR_DMMODE, %g3	! .
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
	SET_SIZE(kt_inj_l2cache_data)
#endif	/* lint */

/*
 * This routine is used to corrupt the data or the data ecc bits of an L2
 * cache line which is an instruciton determined by its paddr.
 * The L2_DIAG_DATA register address is built by this routine, bits [9:6]
 * of the paddr select the cache bank.
 *
 * Note that this routine expects a 64-bit aligned xorpat and shifts it
 * down if the corruption is to be in the upper 32-bit word.  This means
 * that the upper and lower words cannot be corrupted at the same time
 * (and the lower value takes precidence).  This is due to HW limitations.
 *
 * The method used is similar to the above kt_inj_l2cache_data() routine,
 * but note that the calling routine must place the L2 into DM mode.
 */

/*
 * Prototype:
 *
 * int kt_inj_l2cache_instr_data(uint64_t paddr, uint64_t xorpat,
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
kt_inj_l2cache_instr_data(uint64_t paddr, uint64_t xorpat, uint_t checkflag,
				uint64_t idx_paddr)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(kt_inj_l2cache_instr_data)

	/*
	 * Generate register addresses to use below.
	 */
	cmp	%o2, %g0			! if xorpat in upper word shift
	bnz	%icc, 1f			!   down since 32-bit access
	  nop					! .
	srlx	%o2, 32, %o2			! corrupt next 32-bits
	add	%o4, 4, %o4			! .
1:
	and	%o4, 4, %g3			! extract 32B paddr boundary
	sllx	%g3, 21, %g3			! move result to OddEven field
	andn	%o1, 0x7, %o1			! align paddr for asi access
	andn	%o4, 0x7, %o4			! align idx_paddr for asi access

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
	mov	KT_L2_DIAG_DATA_MSB, %g4	! set reg select bits
	sllx	%g4, 32, %g4			! .
	set	0x7ffff8, %g2			! mask paddr to get $line
	and	%o4, %g2, %g2			!  way, set, bank (is PA[22:3])
	add	%g2, %g3, %g2			! combine into complete reg
	add	%g2, %g4, %g2			! %g2 = L2_DIAG_DATA

	/*
	 * Build L2 Control Register for this bank and read value.
	 */
	mov	KT_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0x829.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, KT_L2_BANK_MASK, %g3	! get reg offset from paddr
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
	  sllx	%o2, 7, %o2			!   (otherwise inj to ecc[6:0])
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
	SET_SIZE(kt_inj_l2cache_instr_data)
#endif	/* lint */

/*
 * This routine is used to corrupt the data or the data ecc bits of an L2
 * cache line determined by a physical address.  The L2_DIAG_DATA register
 * address is built by this routine, bits [9:6] of the paddr select the
 * cache bank.
 *
 * The method used is similar to the above kt_inj_l2cache_data() routine,
 * but it has been simplified to expect that the data has already been
 * installed in the cache (in DM mode).  This means that the following
 * steps have been removed:
 *	- the L2 cache is not placed (and later restored) into DM mode
 *	- the data is not accessed (which would bring it into the caches)
 */

/*
 * Prototype:
 *
 * int kt_inj_l2cache_data_quick(uint64_t offset, uint64_t xorpat,
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
kt_inj_l2cache_data_quick(uint64_t offset, uint64_t xorpat, uint_t checkflag,
				uint64_t idx_paddr)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(kt_inj_l2cache_data_quick)

	cmp	%o2, %g0			! if xorpat in upper word shift
	bnz	%icc, 1f			!   down since 32-bit access
	  nop					! .
	srlx	%o2, 32, %o2			! corrupt next 32-bits
	add	%o4, 4, %o4			! .
1:
	and	%o4, 4, %g3			! extract 32B paddr boundary
	sllx	%g3, 21, %g3			! move result to OddEven field
	andn	%o1, 0x7, %o1			! align paddr for asi access
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
	mov	KT_L2_DIAG_DATA_MSB, %g4	! set reg select bits
	sllx	%g4, 32, %g4			! .
	set	0x7ffff8, %g2			! mask idx_paddr to get $line
	and	%o4, %g2, %g2			!  way, set, bank (is PA[22:3])
	or	%g2, %g3, %g2			! or in the OddEven bit
	or	%g2, %g4, %g2			! %g2 = L2_DIAG_DATA

	/*
	 * Build L2 Control Register for this bank and read value.
	 */
	mov	KT_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0x829.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, KT_L2_BANK_MASK, %g3	! get reg offset from offset
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
	  sllx	%o2, 7, %o2			!   (otherwise inj to ecc[6:0])
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
	SET_SIZE(kt_inj_l2cache_data_quick)
#endif	/* lint */

/*
 * This routine is used to corrupt the data or the data ecc bits of an L2
 * cache line determined by a byte offset.  The L2_DIAG_DATA register address
 * is built by this routine, bits [9:6] of the offset select the cache bank.
 *
 * Note that this routine expects a 64-bit aligned xorpat and shifts it
 * down if the corruption is to be in the upper 32-bit word.  This means
 * that the upper and lower words cannot be corrupted at the same time
 * (and the lower value takes precidence).  This is due to HW limitations.
 *
 * The method used is similar to the above kt_inj_l2cache_data() routine,
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
 * int kt_inj_l2phys_data(uint64_t offset, uint64_t xorpat, uint_t checkflag,
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
kt_inj_l2phys_data(uint64_t offset, uint64_t xorpat, uint_t checkflag,
			uint64_t *debug)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(kt_inj_l2phys_data)

	cmp	%o2, %g0			! if xorpat in upper word shift
	bnz	%icc, 1f			!   down since 32-bit access
	  nop					! .
	srlx	%o2, 32, %o2			! corrupt next 32-bits
	add	%o1, 4, %o1			! .
1:
	and	%o1, 4, %g3			! extract 32B paddr boundary
	sllx	%g3, 21, %g3			! move result to OddEven field

	/*
	 * Build the register values for L2$ accesses.
	 */
	mov	KT_L2_DIAG_DATA_MSB, %g4	! set reg select bits
	sllx	%g4, 32, %g4			! .
	set	0x7ffff8, %g2			! mask offset to get $line
	and	%o1, %g2, %g2			!   way, set, bank (is PA[22:3])
	or	%g2, %g3, %g2			! combine into complete reg
	or	%g2, %g4, %g2			! %g2 = L2_DIAG_DATA

	/*
	 * Build L2 Control Register for this bank and read value.
	 */
	mov	KT_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0x829.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, KT_L2_BANK_MASK, %g3	! get reg offset from offset
	add	%g1, %g3, %g1			! include offset in reg addr
	ldx	[%g1], %g4			! %g4=prev L2_CTL_REG contents

	ba	2f				! branch to aligned code
	  nop					! .

	.align	64
2:
	ldx	[%g2], %g3			! read L2$ data to corrupt
						!   via L2_DIAG_DATA reg
	cmp	%o3, %g0			! if check-bit flag == 0
	bz,a,pt	%xcc, 3f			! shift the xorpat to data bits
	  sllx	%o2, 7, %o2			!   (otherwise inj to ecc[6:0])
3:
	xor	%g3, %o2, %g3			! corrupt data or ecc bits
	stx	%g3, [%g2]			! write the data back to L2$
	membar	#Sync				! .

	stx	%g4, [%g1]			! restore this banks L2$ mode
						!   to flush write buffers
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(kt_inj_l2phys_data)
#endif	/* lint */

/*
 * This routine is used to corrupt the tag or the tag ecc bits of an L2
 * cache line determined by the paddr.  The L2_DIAG_TAG register address
 * is built by this routine, bits [9:6] of the paddr select the cache bank
 *
 * The L2 cache is 6MB with a linesize of 64B which yeilds 98304 (0x18000)
 * lines split into 4096 (0x1000) lines per each of the 24 ways.
 *
 * L2_DIAG_TAG (offset 0x824.0000.0000) Access (stride 64)
 *
 * +-------+--------+-------+-----+-----+------+-------+-------+
 * | Rsvd0 | Select | Rsvd1 | Way | Set | Bank | Rsvd2 | Rsvd3 |
 * +-------+--------+-------+-----+-----+------+-------+-------+
 *   63:40   39:32    31:23  22:18 17:10  9:6     5:3     2:0
 *
 * Data returned as TAG[31:6], ECC[5:0] (where the TAG bits are PA[43:18]
 * which means the normal 16-bank mode is IDX independent though reduced
 * bank modes are not because the index window shifts right).
 *
 * NOTE: unlike N2/VF all 24 tags are not checked on each access.
 *	 KT/RF requires a FULL match to detect an error.  This may
 *	 make tag errors slightly less reliable due to evictions.
 *
 * NOTE:in a meeting it was mentioned that check-bits C0-C3 are part
 *	of the tag match on early versions of the KT chips.  This
 *	means that certain ECC errors will not tag match.
 */

/*
 * Prototype:
 *
 * int kt_inj_l2cache_tag(uint64_t paddr, uint64_t xorpat, uint_t checkflag,
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
 *	%g6 - VA for L1 ASI
 *	%g7 - L1 way increment value
 */
#if defined(lint)
/* ARGSUSED */
int
kt_inj_l2cache_tag(uint64_t paddr, uint64_t xorpat, uint_t checkflag,
			uint64_t idx_paddr)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(kt_inj_l2cache_tag)

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
	mov	KT_L2_DIAG_TAG_MSB, %g4		! set reg select bits
	sllx	%g4, 32, %g4			! .
	set	0x7fffc0, %g2			! mask idx_paddr to get $line
	and	%o4, %g2, %g2			!   way, set, bank (is PA[22:6])
	add	%g2, %g4, %g2			! %g2 = L2_DIAG_TAG

#ifdef	L2_DEBUG_BUFFER
	stx	%o1, [%o3 + 0x0]		! paddr
	stx	%o4, [%o3 + 0x8]		! idx_paddr
	stx	%o2, [%o3 + 0x10]		! xor pat
	stx	%g2, [%o3 + 0x18]		! VA value for L2_DIAG_TAG

	/*
	 * If debug then check the data at this location, note the
	 * data ASI access will only go to the intended location if
	 * ALL L2-cache banks are enabled (is using IDX'd paddr).
	 *
	 * For testing on reduced banks (IDX mode must be off) then
	 * use %o1 (the unadjusted PA) instead of %o4 (the adjusted PA)
	 * for the "and" operation below.
	 */
	mov	KT_L2_DIAG_DATA_MSB, %g4	! set reg select bits
	sllx	%g4, 32, %g4			! .
	set	0x7ffff8, %g1			! mask paddr to get $line
	and	%o4, %g1, %g1			!   way, set, bank (is PA[22:3])
	or	%g1, %g4, %g1			! %g1 = L2_DIAG_DATA
	ldx	[%g1], %g3			! read the data at this location
						! note that OddEven is ignored
	sllx	%g3, 1, %g3			! shift it so it's readable
	stx	%g3, [%o3 + 0x20]		! data at this tag location

	mov	1, %g4				! flip the OddEven field
	sllx	%g4, 23, %g4			!   to get other half of data
	xor	%g1, %g4, %g1			!   .
	ldx	[%g1], %g3			! read the data at this location
	sllx	%g3, 1, %g3			! shift it so it's readable
	stx	%g3, [%o3 + 0x28]		! data at this tag location
#else
	cmp	%o3, %g0			! if check-bit flag == 0
	bz,a,pt	%xcc, 2f			! shift the xorpat to tag bits
	  sllx	%o2, 6, %o2			!   (otherwise inj to ecc[5:0])
#endif

	/*
	 * Put L2 into direct mapped mode, this bank only.
	 *
	 * NOTE: though the C-level routine is doing this now it has
	 *	 been left in here so that the L2 buffers get flushed
	 *	 (could mod it to only save and restore).
	 */
2:
	mov	KT_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0x829.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, KT_L2_BANK_MASK, %g3	! get reg offset from paddr
	add	%g1, %g3, %g1			! include offset in reg addr

	ldx	[%g1], %g4			! %g4=prev L2_CTL_REG contents
	or	%g4, KT_L2CR_DMMODE, %g3	! .
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
	sllx	%g3, 10, %g3			! shift it by 10 (for 4ch)
	stx	%g3, [%o3 + 0x60]		! .
	sllx	%g3, 1, %g3			! shift it by 11 (for 8ch)
	stx	%g3, [%o3 + 0x68]		! .
	sllx	%g3, 1, %g3			! shift it by 12 (for 16ch)
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
	SET_SIZE(kt_inj_l2cache_tag)
#endif	/* lint */

/*
 * This routine is used to corrupt the tag or the tag ecc bits of an L2
 * cache line containing an instruction accessed by it's paddr.  The
 * L2_DIAG_TAG register address is built by this routine, bits [9:6] of the
 * paddr select the cache bank.
 *
 * The method used is similar to the above kt_inj_l2cache_tag() routine,
 * but note that the calling routine must place the L2 into DM mode.
 */

/*
 * Prototype:
 *
 * int kt_inj_l2cache_instr_tag(uint64_t paddr, uint64_t xorpat,
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
kt_inj_l2cache_instr_tag(uint64_t paddr, uint64_t xorpat, uint_t checkflag,
				uint64_t idx_paddr)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(kt_inj_l2cache_instr_tag)

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
	mov	KT_L2_DIAG_TAG_MSB, %g4		! set select bits
	sllx	%g4, 32, %g4			! .
	set	0x7fffc0, %g2			! mask paddr to get $line
	and	%o4, %g2, %g2			! combine into complete reg
	add	%g2, %g4, %g2			! %g2 = L2_DIAG_TAG

	/*
	 * Build L2 Control Register for this bank and read value.
	 */
	mov	KT_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0x829.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, KT_L2_BANK_MASK, %g3	! get reg offset from paddr
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
	  sllx	%o2, 6, %o2			!   (otherwise inj to ecc[5:0])
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
	SET_SIZE(kt_inj_l2cache_instr_tag)
#endif	/* lint */

/*
 * This routine is used to corrupt the tag or the tag ecc bits of an L2
 * cache line determined by a byte offset.  The L2_DIAG_TAG register address
 * is built by this routine, bits [9:6] of the offset select the cache bank.
 *
 * The method used is similar to the above kt_inj_l2cache_tag() routine.
 *
 * NOTE: this routine is immune to the L2-cache IDX setting since a
 *	 byte offset is used for the injection and not an address.
 */

/*
 * Prototype:
 *
 * int kt_inj_l2phys_tag(uint64_t offset, uint64_t xorpat, uint_t checkflag,
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
kt_inj_l2phys_tag(uint64_t offset, uint64_t xorpat, uint_t checkflag,
					uint64_t *debug)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(kt_inj_l2phys_tag)

	/*
	 * Generate register addresses to use below.
	 */
	mov	KT_L2_DIAG_TAG_MSB, %g4		! set reg select bits
	sllx	%g4, 32, %g4			! .
	set	0x7fffc0, %g2			! mask offset to get $line
	and	%o1, %g2, %g2			! combine into complete reg
	add	%g2, %g4, %g2			! %g2 = L2_DIAG_TAG

	/*
	 * Put L2 into direct mapped mode, this bank only.
	 */
	mov	KT_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0x829.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, KT_L2_BANK_MASK, %g3	! get reg offset from offset
	add	%g1, %g3, %g1			! include offset in reg addr
	
	ldx	[%g1], %g4			! %g4=prev L2_CTL_REG contents
	or	%g4, KT_L2CR_DMMODE, %g3	! .
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
	SET_SIZE(kt_inj_l2phys_tag)
#endif	/* lint */

/*
 * This routine is used to place "NotData" into the L2 cache line 
 * determined by the paddr when the cache line is for a data
 * access.  The L2_DIAG_DATA register address is built by
 * this routine, bits [9:6] of the paddr select the cache bank.
 *
 * Niagara-II only implemented NotData on data as it entered the L2 cache.
 * Though it is detectable on 4-byte chunks of data, it will generally
 * always be on 16-byte chunks of data since that is how data is brought
 * into the L2 cache.
 *
 * For fills, the data received from COU maybe marked as qND or sND with
 * doubleword (8-byte) granularity.  In either case, two ND words (4-bytes
 * each) are written to the L2 cache. It is also possible for data to be UE
 * with word granularity. In this case, a single ND word (4-bytes) is written
 * to L2 cache.
 *
 * For stores, the data received from a core may already be marked by the
 * core as bad.  It is also possible for the store data to get a parity
 * error while waiting in the L2 Miss buffer.  In either case, and regardless
 * of the store width, two ND words (4-bytes each) are written into the
 * L2-cache.
 *
 * Data returned to the core has granularity of 16-bytes.  If reading L2
 * reveals a ND in any of the included words (words are 4-bytes), the
 * entire 16-bytes of return data is marked as ND.
 * 
 * NotData is signalled by specific syndromes since K/T has two different
 * types of NotData, signal and quiet.  Quiet is the same as N2/VF NotData
 * (indicated by synd=0x7f or 0x7e), the signal NotData does not invert the
 * two low order ECC bits (synd = 0x7d).  Note that SOC NotData is same as
 * L2.
 *
 * Since the L2_DIAG_DATA register only allows 32-bits (4-bytes) of data
 * to be written at a time, this routine will write two adjacent (aligned)
 * 4-byte chunks to produce one 8-byte chunk of NotData.
 *
 * Note also that by setting and restoring the L2$ DM mode has the effect
 * of clearing all the L2$ write buffers.  This occurs even if the mode does
 * not actually change since it is triggered by the control reg write.
 *
 * The method used is similar to the above kt_inj_l2cache_data() routine.
 */

/*
 * Prototype:
 *
 * int kt_inj_l2nd(uint64_t paddr, uint64_t xorpat, uint64_t idx_paddr,
 *			uint64_t *debug)
 *
 * Register usage:
 *
 *	%o1 - paddr (should be 8-byte aligned)
 *	%o2 - xorpat (must be 0x7f, 0x7e, or 0x7d, note that it is clobbered)
 *	%o3 - idx_paddr, the paddr after IDX index hashing
 *	%o4 - debug buffer (debug)
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
kt_inj_l2nd(uint64_t paddr, uint64_t xorpat, uint64_t idx_paddr,
		uint64_t *debug)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(kt_inj_l2nd)

	/*
	 * Generate register addresses to use below.
	 */
	andn	%o1, 0x7, %o1			! align paddr to 8-bytes
	andn	%o3, 0x7, %o3			!   and idx_paddr

#ifdef	L2_DEBUG_BUFFER
	stx	%o1, [%o4 + 0x0]		! aligned paddr
	stx	%o3, [%o4 + 0x0]		! aligned idx_paddr
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
	mov	KT_L2_DIAG_DATA_MSB, %g4	! set reg select bits
	sllx	%g4, 32, %g4			! .
	set	0x7ffff8, %g2			! mask paddr to get $line
	and	%o3, %g2, %g2			!  way, set, bank (is PA[22:3])
	or	%g2, %g4, %g2			! %g2 = first L2_DIAG_DATA

#ifdef	L2_DEBUG_BUFFER
	stx	%g2, [%o4 + 0x10]		! first addr for L2_DIAG_DATA
#endif
	/*
	 * Put L2 into direct mapped mode, this bank only.
	 */
	mov	KT_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0x829.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, KT_L2_BANK_MASK, %g3	! get reg offset from paddr
	or	%g1, %g3, %g1			! include offset in reg addr

	ldx	[%g1], %g4			! %g4=prev L2_CTL_REG contents
	or	%g4, KT_L2CR_DMMODE, %g3	! .
	stx	%g3, [%g1]			! .
	membar	#Sync				! (PRM says not needed)

#ifdef	L2_DEBUG_BUFFER
	stx	%g1, [%o4 + 0x18]		! addr of L2_CTL_REG
	stx	%g3, [%o4 + 0x20]		! new contents of L2_CTL_REG
#endif
	ba	1f				! branch to aligned code
	  nop					! .

	.align	128	
1:
	ldx	[%o1], %g3			! get data into L2$ and L1$
	membar	#Sync				! .

#ifdef	L2_DEBUG_BUFFER
	stx	%g3, [%o4 + 0x28]		! contents of mem location
#endif
	ldx	[%g2], %g3			! read L2$ data to corrupt
	xor	%g3, %o2, %g3			! flip ecc bits for ND
	stx	%g3, [%g2]			! write the data back to L2$

	mov	1, %g3				! set OddEven field (bit 23)
	sllx	%g3, 23, %g3			! .
	or	%g2, %g3, %g2			! or in the OddEven bit
	ldx	[%g2], %g3			! read L2$ data to corrupt
	xor	%g3, %o2, %g3			! flip ecc bits for ND
	stx	%g3, [%g2]			! write the data back to L2$
	membar	#Sync				! .

#ifdef	L2_DEBUG_BUFFER
	stx	%g3, [%o4 + 0x30]		! data after bit(s) flipped
	ldx	[%g2], %g3			! read back the data (via reg)
	sllx	%g3, 1, %g3			! shift it so it's readable
	stx	%g3, [%o4 + 0x40]		!  should not invoke the error
#endif

	/*
	 * Clear the the L1$ entry so access does not hit there.
	 *
	 * Since each d$ entry is 16-bytes wide, it will cover all of
	 * the potential NotData no matter where the access is aligned.
	 */
2:
#ifdef	L2_DEBUG_BUFFER
	ldxa	[%g6]ASI_DC_TAG, %o2		! look at the d$ tags
	sllx	%o2, 9, %o2			! move it up for compare
	sllx	%g5, 3, %o0			! try to get all four
	add	%o4, %o0, %o0			! add offset to debug_buffer
	stx	%o2, [%o0 + 0x50]		! store the tag
#endif
	stxa	%g0, [%g6]ASI_DC_TAG		! clear all four ways of L1
	sub	%g5, 1, %g5			! .
	brnz,a	%g5, 2b
	  add	%g6, %g7, %g6			! go to next way

	stx	%g4, [%g1]			! restore this banks L2$ mode
						!   which will flush L2$ buffers
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(kt_inj_l2nd)
#endif	/* lint */

/*
 * This routine is used to place "NotData" into the L2 cache line 
 * determined by the paddr when the cache line is for a data
 * access.  The L2_DIAG_DATA register address is built by
 * this routine, bits [9:6] of the paddr select the cache bank.
 *
 * The method used is similar to the above kt_inj_l2nd() routine,
 * but it has been simplified to expect that the data has already been
 * installed in the cache (in DM mode).  This means that the following
 * steps have been removed:
 *	- the L2 cache is not placed (and later restored) into DM mode
 *	- the data is not accessed (which would bring it into the caches)
 */

/*
 * Prototype:
 *
 * int kt_inj_l2nd_quick(uint64_t paddr, uint64_t xorpat, uint64_t idx_paddr,
 *				uint64_t *debug)
 *
 * Register usage:
 *
 *	%o1 - paddr (should be 8-byte aligned)
 *	%o2 - xorpat (must be 0x7f, 0x7e, or 0x7d, note that it is clobbered)
 *	%o3 - idx_paddr, the paddr after IDX index hashing
 *	%o4 - debug buffer (debug)
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
kt_inj_l2nd_quick(uint64_t paddr, uint64_t xorpat, uint64_t idx_paddr,
			uint64_t *debug)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(kt_inj_l2nd_quick)

	/*
	 * Generate register addresses to use below.
	 */
	andn	%o1, 0x7, %o1			! align paddr to 8-bytes
	andn	%o3, 0x7, %o3			!   and idx_paddr

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
	mov	KT_L2_DIAG_DATA_MSB, %g4	! set reg select bits
	sllx	%g4, 32, %g4			! .
	set	0x7ffff8, %g2			! mask paddr to get $line
	and	%o3, %g2, %g2			!  way, set, bank (is PA[22:3])
	or	%g2, %g4, %g2			! %g2 = first L2_DIAG_DATA

	/*
	 * Build L2 Control Register for this bank and read value.
	 */
	mov	KT_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0x829.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, KT_L2_BANK_MASK, %g3	! get reg offset from paddr
	or	%g1, %g3, %g1			! include offset in reg addr
	ldx	[%g1], %g4			! %g4=prev L2_CTL_REG contents

	ba	1f				! branch to aligned code
	  nop					! .

	.align	128	
1:
	ldx	[%g2], %g3			! read L2$ data to corrupt
	xor	%g3, %o2, %g3			! flip ecc bits for ND
	stx	%g3, [%g2]			! write the data back to L2$

	mov	1, %g3				! set OddEven field (bit 23)
	sllx	%g3, 23, %g3			! .
	or	%g2, %g3, %g2			! or in the OddEven bit
	ldx	[%g2], %g3			! read L2$ data to corrupt
	xor	%g3, %o2, %g3			! flip ecc bits for ND
	stx	%g3, [%g2]			! write the data back to L2$
	membar	#Sync				! .

	/*
	 * Clear the the L1$ entry so access does not hit there.
	 *
	 * Since each d$ entry is 16-bytes wide, it will cover all of
	 * the potential NotData no matter where the access is aligned.
	 */
2:
	stxa	%g0, [%g6]ASI_DC_TAG		! clear all four ways of L1
	sub	%g5, 1, %g5			! .
	brnz,a	%g5, 2b
	  add	%g6, %g7, %g6			! go to next way

	stx	%g4, [%g1]			! store to flush write buffers
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(kt_inj_l2nd_quick)
#endif	/* lint */

/*
 * This routine is used to place "NotData" into the L2 cache line 
 * determined by the paddr when the cache line is for an instruction
 * access.  The L2_DIAG_DATA register address is built by
 * this routine, bits [9:6] of the paddr select the cache bank.
 *
 * The method used is similar to the above kt_inj_l2nd() routine,
 * but note that the calling routine must place the L2 into DM mode.
 */

/*
 * Prototype:
 *
 * int kt_inj_l2nd_instr(uint64_t paddr, uint64_t xorpat, uint64_t idx_paddr,
 *				uint64_t *debug)
 *
 * Register usage:
 *
 *	%o1 - paddr (should be 8-byte aligned)
 *	%o2 - xorpat (must be 0x7f, 0x7e, or 0x7d, note that it is clobbered)
 *	%o3 - idx_paddr, the paddr after IDX index hashing
 *	%o4 - debug buffer (debug)
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
kt_inj_l2nd_instr(uint64_t paddr, uint64_t xorpat, uint64_t idx_paddr,
			uint64_t *debug)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(kt_inj_l2nd_instr)

	/*
	 * Generate register addresses to use below.
	 */
	andn	%o1, 0x7, %o1			! align paddr to 8-bytes
	andn	%o3, 0x7, %o3			!   and idx_paddr

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
	mov	KT_L2_DIAG_DATA_MSB, %g4	! set reg select bits
	sllx	%g4, 32, %g4			! .
	set	0x7ffff8, %g2			! mask paddr to get $line
	and	%o3, %g2, %g2			!  way, set, bank (is PA[22:3])
	or	%g2, %g4, %g2			! %g2 = first L2_DIAG_DATA

	/*
	 * Put L2 into direct mapped mode, this bank only.
	 */
	mov	KT_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0x829.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, KT_L2_BANK_MASK, %g3	! get reg offset from paddr
	add	%g1, %g3, %g1			! include offset in reg addr

	ldx	[%g1], %g4			! %g4=prev L2_CTL_REG contents
	or	%g4, KT_L2CR_DMMODE, %g3	! .
	stx	%g3, [%g1]			! .
	membar	#Sync				! (PRM says not needed)

	ba	1f				! branch to aligned code
	  nop					! .

	.align	128	
1:
	ldx	[%g2], %g3			! read L2$ data to corrupt
	xor	%g3, %o2, %g3			! flip ecc bits for ND
	stx	%g3, [%g2]			! write the data back to L2$

	mov	1, %g3				! set OddEven field (bit 23)
	sllx	%g3, 23, %g3			! .
	or	%g2, %g3, %g2			! or in the OddEven bit
	ldx	[%g2], %g3			! read L2$ data to corrupt
	xor	%g3, %o2, %g3			! flip ecc bits for ND
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

	stx	%g4, [%g1]			! restore this banks L2$ mode
						!   which will flush L2$ buffers
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(kt_inj_l2nd_instr)
#endif	/* lint */

/*
 * This routine is used to place "NotData" into the L2 cache line 
 * determined by a byte offset. The L2_DIAG_DATA register address is
 * built by this routine, bits [9:6] of the paddr select the cache bank.
 *
 * The method used is similar to the above kt_inj_l2nd() routine,
 * but note that the data is not removed from the L1-cache.
 *
 * NOTE: this routine is immune to the L2-cache IDX setting since a
 *	 byte offset is used for the injection and not an address.
 */

/*
 * Prototype:
 *
 * int kt_inj_l2nd_phys(uint64_t offset, uint64_t xorpat, uint64_t *debug)
 *
 * Register usage:
 *
 *	%o1 - byte offset (should be 16-byte aligned)
 *	%o2 - xorpat (must be 0x7f, 0x7e, or 0x7d)
 *	%o3 - debug buffer (debug)
 *	%o4 - OddEven bit (not an inpar)
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
kt_inj_l2nd_phys(uint64_t offset, uint64_t xorpat, uint64_t *debug)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(kt_inj_l2nd_phys)

	/*
	 * Generate register addresses to use below.
	 */
	andn	%o1, 0x7, %o1			! align offset to 8-bytes
	mov	1, %o4				! put OddEven in reg (bit 23)
	sllx	%o4, 23, %o4			! %o4 = OddEven bit

	/*
	 * Build the register values for L2$ accesses.
	 */
	mov	KT_L2_DIAG_DATA_MSB, %g4	! set reg select bits
	sllx	%g4, 32, %g4			! .
	set	0x7ffff8, %g2			! mask paddr to get $line
	and	%o1, %g2, %g2			!  way, set, bank (is PA[22:3])
	or	%g2, %g4, %g2			! %g2 = first L2_DIAG_DATA

	/*
	 * Build L2 Control Register for this bank and read value.
	 */
	mov	KT_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0x829.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, KT_L2_BANK_MASK, %g3	! get reg offset from paddr
	add	%g1, %g3, %g1			! include offset in reg addr
	ldx	[%g1], %g4			! %g4=L2_CTL_REG contents

	ba	1f				! branch to aligned code
	  nop					! .

	.align	128	
1:
	ldx	[%g2], %g3			! read L2$ data to corrupt
	xor	%g3, %o2, %g3			! flip ecc bits for ND
	stx	%g3, [%g2]			! write the data back to L2$

	or	%g2, %o4, %g2			! set the OddEven bit
	ldx	[%g2], %g3			! read L2$ data to corrupt
	xor	%g3, %o2, %g3			! flip ecc bits for ND
	stx	%g3, [%g2]			! write the data back to L2$
	membar	#Sync				! .

	stx	%g4, [%g1]			! write to banks L2_CTL_REG
						!   to flush write buffers
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(kt_inj_l2nd_phys)
#endif	/* lint */

/*
 * This routine is used to corrupt the V(U)ADS or the V(U)ADS ecc bits of
 * an L2$ line determined by the paddr.  ECC is checked on a tag/way match
 * though an error in any of the 24 bits (corresponding to each way) may
 * may be detected on amy L2 access to an index (the Used bit of VUADS is
 * not protected since it only affects performance, not correctness).
 *
 * This routine expects that the caller will determine the bits to be
 * corrupted so the caller must be aware of the format of these regs.
 *
 * Similar to other L2$ tests the method of setting and restoring the L2$ DM
 * mode to clear the L2$ write buffers is used in the VAD type tests.
 *
 * L2_DIAG_V (offset 0x6.0000.0000) Access (stride 64)
 * L2_DIAG_A (offset 0x6.0040.0000) Access (stride 64)
 * L2_DIAG_D (offset 0x6.0080.0000) Access (stride 64)
 * L2_DIAG_S (offset 0x6.00c0.0000) Access (stride 64)
 *
 * +-------+--------+-------+------------+-------+-----+------+-----------+
 * | Rsvd0 | Select | Rsvd1 | CSA Select | Rsvd2 | Set | Bank | Rsvd3 + 4 |
 * +-------+--------+-------+------------+-------+-----+------+-----------+
 *   63:40   39:32    31:25    24:22       21:18  17:10  9:6    5:3 + 2:0
 *
 * Data returned as:
 *
 * +-------+------------+--------------+
 * | Rsvd0 | target ECC | target array |
 * +-------+------------+--------------+
 *   63:30     29:24          23:0
 */

/*
 * Prototype:
 *
 * int kt_inj_l2vads(uint64_t paddr, uint_t xorpat, uint_t csa_select,
 *			uint64_t idx_paddr);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - xorpat
 *	%o3 - CSA select to choose injection target
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
kt_inj_l2vads(uint64_t paddr, uint_t xorpat, uint_t csa_select,
		uint64_t idx_paddr)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(kt_inj_l2vads)

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
	mov	KT_L2_DIAG_VADS_MSB, %g4	! set select bits
	sllx	%g4, 32, %g4			! .
	set	0x3ffc0, %g2			! mask paddr to get $line (17:6)
	and	%o4, %g2, %g2			! combine into complete reg
	sllx	%o3, 22, %o3			! include the CSA select bits
	or	%g2, %o3, %g2			! .
	or	%g2, %g4, %g2			! %g2 = L2_DIAG_V/A/D/S

	/*
	 * Get the contents of this banks control register.
	 */
	mov	KT_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0x829.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, KT_L2_BANK_MASK, %g3	! get reg offset from paddr
	add	%g1, %g3, %g1			! include offset in reg addr
	ldx	[%g1], %g4			! %g4=L2_CTL_REG contents

	ba	1f				! branch to aligned code
	  nop					! .

	.align	64
1:
	ldx	[%o1], %g3			! get data into L2$ and L1$
	membar	#Sync				!.

	ldx	[%g2], %g3			! read value to corrupt
	xor	%g3, %o2, %g3			! corrupt VADS or ecc bits
	stx	%g3, [%g2]			! write the value back to L2$
	membar	#Sync				!   via L2_DIAG_V/A/D/S reg

	/*
	 * Clear the the L1$ entry so access does not hit there.
	 */
2:
	stxa	%g0, [%g6]ASI_DC_TAG		! clear all four ways of L1
	sub	%g5, 1, %g5			! .
	brnz	%g5, 2b
	  add	%g6, %g7, %g6			! go to next way

	stx	%g4, [%g1]			! restore this banks L2$ mode
						!   which will flush L2$ bufs
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(kt_inj_l2vads)
#endif	/* lint */

/*
 * This routine is used to corrupt the V(U)ADS or the V(U)ADS ecc bits of
 * an L2 cache line that maps to an instruction determined by the paddr.
 * This routine is similar to the above kt_inj_l2vads() routine.
 *
 * This routine expects that the caller will determine the bits to be
 * corrupted so the caller must be aware of the format of these regs.
 */

/*
 * Prototype:
 *
 * int kt_inj_l2vads_instr(uint64_t paddr, uint_t xorpat, uint_t csa_select,
 *			uint64_t idx_paddr);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - xorpat
 *	%o3 - CSA select to choose injection target
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
kt_inj_l2vads_instr(uint64_t paddr, uint_t xorpat, uint_t csa_select,
			uint64_t idx_paddr)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(kt_inj_l2vads_instr)

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
	mov	KT_L2_DIAG_VADS_MSB, %g4	! set select bits
	sllx	%g4, 32, %g4			! .
	set	0x3ffc0, %g2			! mask paddr to get $line (17:6)
	and	%o4, %g2, %g2			! combine into complete reg
	sllx	%o3, 22, %o3			! include the CSA select bits
	or	%g2, %o3, %g2			! .
	or	%g2, %g4, %g2			! %g2 = L2_DIAG_V/A/D/S

	/*
	 * Get the contents of this banks control register.
	 */
	mov	KT_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0x829.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, KT_L2_BANK_MASK, %g3	! get reg offset from paddr
	add	%g1, %g3, %g1			! include offset in reg addr
	ldx	[%g1], %g4			! %g4=L2_CTL_REG contents

	ba	1f				! branch to aligned code
	  nop					! .

	.align	64
1:
	ldx	[%o1], %g3			! get data into L2$ and L1$
	membar	#Sync				! (may not be req'd for instr)

	ldx	[%g2], %g3			! read value to corrupt
	xor	%g3, %o2, %g3			! corrupt VADS or ecc bits
	stx	%g3, [%g2]			! write the value back to L2$
	membar	#Sync				!   via L2_DIAG_V/A/D/S reg

	/*
	 * Clear the the L1$ entry so access does not hit there.
	 */
2:
	stxa	%g0, [%g6]ASI_ICACHE_TAG	! clear all eight ways of L1
	sub	%g5, 1, %g5			! .
	brnz	%g5, 2b
	  add	%g6, %g7, %g6			! go to next way

	stx	%g4, [%g1]			! write to banks L2_CTL_REG
						!   which will flush L2$ bufs
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(kt_inj_l2vads_instr)
#endif	/* lint */

/*
 * This routine is used to corrupt the V(U)ADS or the V(U)ADS ecc bits of an
 * L2 cache line determined by a byte offset.  This routine is similar to the
 * above kt_inj_l2vads() except it takes an offset instead of a paddr and
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
 * int kt_inj_l2vads_phys(uint64_t offset, uint_t xorpat, uint_t csa_select,
 *				uint64_t *debug);
 *
 * Register usage:
 *
 *	%o1 - byte offset (only bits [17:6] used)
 *	%o2 - xorpat (clobbered)
 *	%o3 - CSA select to choose injection target
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
kt_inj_l2vads_phys(uint64_t offset, uint_t xorpat, uint_t csa_select,
				uint64_t *debug)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(kt_inj_l2vads_phys)

	/*
	 * Generate register addresses to use below.
	 */
	mov	KT_L2_DIAG_VADS_MSB, %g4	! set select bits
	sllx	%g4, 32, %g4			! .
	set	0x3ffc0, %g2			! mask paddr to get $line (17:6)
	and	%o1, %g2, %g2			! combine into complete reg
	sllx	%o3, 22, %o3			! include the CSA select bits
	or	%g2, %o3, %g2			! .
	or	%g2, %g4, %g2			! %g2 = L2_DIAG_V/A/D/S

	/*
	 * Get the contents of this banks control register.
	 */
	mov	KT_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0x829.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, KT_L2_BANK_MASK, %g3	! get reg offset from offset
	add	%g1, %g3, %g1			! include offset in reg addr
	ldx	[%g1], %g4			! %g4=L2_CTL_REG contents

	ldx	[%g2], %g3			! read value to corrupt
	xor	%g3, %o2, %g3			! corrupt VADS or ecc bits
	stx	%g3, [%g2]			! write the value back to L2$
	membar	#Sync				!   via L2_DIAG_V/A/D/S reg

	stx	%g4, [%g1]			! write to L2$ control reg
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(kt_inj_l2vads_phys)
#endif	/* lint */

/*
 * This routine is used to inject an error into one of the L2 buffers
 * which is available through the L2_ERROR_INJECT register.  These
 * buffers incude:
 *	L2 directory (parity protection)
 *	L2 fill buffer (ecc protection)
 *	L2 miss buffer (parity protection)
 *	L2 write buffer (ecc protection)
 * 
 * Errors are planted via the per bank L2_ERROR_INJECT_REG (0x82d.0000.0000).
 * Bits [9:6] of the paddr select the cache bank.
 *
 * NOTE: this routine is immune to the L2-cache IDX setting since no
 *	 indexes are used only actual addresses.  However if the L2$
 *	 is in a reduced bank mode, the bank selection may not work.
 */

/*
 * Prototype:
 *
 * int kt_inj_l2buf(uint64_t paddr, uint_t sdshot_enb, uint_t hv_flag,
 *			uint64_t l2_bank_mask);
 *
 * Register usage:
 *
 *	%o1 - paddr (64-bit aligned)
 *	%o2 - sshot_enb (the combination of enable and sshot to use)
 *	%o3 - flag for HV mode vs. kernel mode _or_ debug buffer (debug)
 *	%o4 - mask for L2 bank
 *	%g1 - temp
 *	%g2 - temp then address for data read/write
 *	%g3 - dummy target reg for error inject
 *	%g4 - unused except for park/unpark
 *	%g5 - counter for L1$ clear
 *	%g6 - VA for L1 ASI
 *	%g7 - L1 way increment value
 */
#if defined(lint)
/* ARGSUSED */
int
kt_inj_l2buf(uint64_t paddr, uint_t sdshot_enb, uint_t hv_flag,
			uint64_t l2_bank_mask)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(kt_inj_l2buf)

	/*
	 * Park all sibling strands via the CORE_RUNNING regs so that the
	 * injection will only occur on this strand.
	 */
	KT_PARK_SIBLING_STRANDS(%g7, %g6, %g5, %g4, %g3)

	/*
	 * Build the register values for L1$ access.
	 * The d$ way bits are bits 12:11, the set bits are 10:4.
	 */
	and	%o1, 0x7f0, %g6			! %g6 = PA[10:4] for L1 tag asi
	mov	1, %g7				! %g7 = L1 way inc. value
	sllx	%g7, 11, %g7			! .
	mov	4, %g5				! %g5 = number of L1 ways

	/*
	 * Generate register addresses to use below.
	 */
	mov	KT_L2_ERR_INJ_REG_MSB, %g4	! set select bits
	sllx	%g4, 32, %g4			! .
	and	%o1, %o4, %g2			! mask paddr to get L2$ bank
	add	%g2, %g4, %g2			! %g2 = L2_ERROR_INJECT_REG

#ifdef	L2_DEBUG_BUFFER
	stx	%o1, [%o3 + 0x0]		! aligned paddr
	stx	%o2, [%o3 + 0x8]		! xor pat + options
	stx	%o4, [%o3 + 0x10]		! bank mask
#endif
	ba	1f				! branch to aligned code
	  nop					! .

	.align	64
1:
	stx	%o2, [%g2]			! enable dir/buf error injection
	ldx	[%g2], %g0			! ensure write completes

	ldx	[%o1], %g3			! do first bad write

	/*
	 * Clear the the L1$ entry so access does not hit there.
	 */
2:
	stxa	%g0, [%g6]ASI_DC_TAG		! clear all four ways of L1
	sub	%g5, 1, %g5			! .
	brnz,a	%g5, 2b
	  add	%g6, %g7, %g6			! go to next way

	/*
	 * If the error specified HV mode then perform the
	 * access right here HV mode, otherwise return to kernel.
	 * If debug is enabled then always return to kernel mode.
	 */
#ifndef	L2_DEBUG_BUFFER
	cmp	%o3, %g0			! if HV flag != 0
	bnz,a,pt %icc, 3f			!   perform access in HV mode
	  ldx	[%o1], %g0			!   .
#endif
3:
	/*
	 * Unpark the sibling cores that were parked above.
	 */
	KT_UNPARK_SIBLING_STRANDS(%g7, %g6, %g4, %g3)

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(kt_inj_l2buf)
#endif	/* lint */

/*
 * This routine is used to inject an instruction error into one of the
 * L2 buffers which is available through the L2_ERROR_INJECT register.
 *
 * This routine is similar to the above kt_inj_l2buf() routine which
 * injects a data error.
 */

/*
 * Prototype:
 *
 * int kt_inj_l2buf_instr(uint64_t paddr, uint_t sdshot_enb, uint_t hv_flag,
 *			uint64_t l2_bank_mask);
 *
 * Register usage:
 *
 *	%o0 - saved paddr of routine to corrupt/call (not an inpar)
 *	%o1 - paddr (64-bit aligned)
 *	%o2 - sshot_enb (the combination of enable and sshot to use)
 *	%o3 - flag for HV mode vs. kernel mode _or_ debug buffer (debug)
 *	%o4 - mask for L2 bank
 *	%g1 - temp
 *	%g2 - temp then address for data read/write
 *	%g3 - dummy target reg for error inject
 *	%g4 - L1 way increment value
 *	%g5 - counter for L1$ clear
 *	%g6 - VA for L1 ASI
 *	%g7 - return address for called routine
 */
#if defined(lint)
/* ARGSUSED */
int
kt_inj_l2buf_instr(uint64_t paddr, uint_t sdshot_enb, uint_t hv_flag,
			uint64_t l2_bank_mask)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(kt_inj_l2buf_instr)

	/*
	 * Park all sibling strands via the CORE_RUNNING regs so that the
	 * injection will only occur on this strand.
	 */
	KT_PARK_SIBLING_STRANDS(%g7, %g6, %g5, %g4, %g3)

	/*
	 * Build the register values for L1$ access.
	 * The i$ way bits are bits 14:12 (8-way), and index bits are PA[10:5].
	 */
	and	%o1, 0x7e0, %g6			! mask paddr for L1 asi
	sllx	%g6, 1, %g6			! %g6 = L1 asi access addr
	mov	1, %g4				! set way inc. value
	sllx	%g4, 12, %g4			! %g4 = L1 way inc. value
	mov	8, %g5				! %g5 = number of L1 ways

	/*
	 * Generate register addresses to use below.
	 */
	mov	%o1, %o0			! copy routine paddr so it can
						!   be used and called below

	mov	KT_L2_ERR_INJ_REG_MSB, %g4	! set select bits
	sllx	%g4, 32, %g4			! .
	and	%o1, %o4, %g2			! mask paddr to get L2$ bank
	add	%g2, %g4, %g2			! %g2 = L2_ERROR_INJECT_REG

#ifdef	L2_DEBUG_BUFFER
	stx	%o1, [%o3 + 0x0]		! aligned paddr
	stx	%o2, [%o3 + 0x8]		! xor pat + options
	stx	%o4, [%o3 + 0x10]		! bank mask
#endif
	stx	%o2, [%g2]			! enable dir/buf error injection
	ldx	[%g2], %g0			! ensure write completes

	/*
	 * Run the routine to corrupt to install it with the error.
	 * Must hope/ensure that it's not too far away for the jmp.
	 * 
	 * Note that the call (jmp) to the primed asmld clobbers %o1.
	 */
	jmp	%o0				! do first bad write
          rd    %pc, %g7

	/*
	 * Clear the the L1$ entry so access does not hit there.
	 */
1:
	stxa	%g0, [%g6]ASI_ICACHE_TAG	! clear all eight ways of L1
	sub	%g5, 1, %g5			! .
	brnz,a	%g5, 1b
	  add	%g6, %g4, %g6			! go to next way

	/*
	 * If the error specified HV mode then perform the
	 * access right here HV mode by re-running the instruction buffer,
	 * otherwise return to kernel.
	 *
	 * If debug is enabled then always return to kernel mode.
	 */
#ifndef	L2_DEBUG_BUFFER
	cmp	%o3, %g0			! if HV flag != 0
	bz,a	%icc, 2f			!   perform access in HV mode
	  nop					!   .

	jmp	%o0				! run routine again to trigger
          rd    %pc, %g7			! .
#endif
2:
	/*
	 * Unpark the sibling cores that were parked above.
	 */
	KT_UNPARK_SIBLING_STRANDS(%g7, %g6, %g4, %g3)

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(kt_inj_l2buf_instr)
#endif	/* lint */

/*
 * This routine is used to inject an error into the L2 write buffer
 * which is available through the L2_ERROR_INJECT register.  The
 * write buffer is protected with ecc.
 * 
 * Errors are planted via the per bank L2_ERROR_INJECT_REG (0x82d.0000.0000).
 * Bits [9:6] of the paddr select the cache bank.  This routine is similar
 * to the above kt_inj_l2buf() routine.
 *
 * NOTE: this routine is immune to the L2-cache IDX setting since no
 *	 indexes are used only actual addresses.  However if the L2$
 *	 is in a reduced bank mode, the bank selection may not work.
 */

/*
 * Prototype:
 *
 * int kt_inj_l2wbuf(uint64_t paddr, uint_t sdshot_enb, uint_t hv_flag,
 *			uint64_t l2_bank_mask);
 *
 * Register usage:
 *
 *	%o1 - paddr (64-bit aligned)
 *	%o2 - sshot_enb (the combination of enable and sshot to use)
 *	%o3 - flag for HV mode vs. kernel mode _or_ debug buffer (debug)
 *	%o4 - mask for L2 bank
 *	%g1 - temp
 *	%g2 - temp then address for data read/write
 *	%g3 - dummy target reg for error inject
 *	%g4 - flush address for use with prefetch-ICE
 *	%g5 - counter for L1$ clear
 *	%g6 - VA for L1 ASI
 *	%g7 - L1 way increment value
 */
#if defined(lint)
/* ARGSUSED */
int
kt_inj_l2wbuf(uint64_t paddr, uint_t sdshot_enb, uint_t hv_flag,
			uint64_t l2_bank_mask)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(kt_inj_l2wbuf)

	/*
	 * Build the register values for L1$ access.
	 * The d$ way bits are bits 12:11, the set bits are 10:4.
	 */
	and	%o1, 0x7f0, %g6			! %g6 = PA[10:4] for L1 tag asi
	mov	1, %g7				! %g7 = L1 way inc. value
	sllx	%g7, 11, %g7			! .
	mov	4, %g5				! %g5 = number of L1 ways

	/*
	 * Determine the L2 flush-ICE index for the specified paddr.
	 * Since the cache will be in DM-mode only need the one index.
	 */
	sllx	%o1, 31, %g4			! remove MSB bits but keep all
	srlx	%g4, 31, %g4			!   bits used by idx (PA[32:6])

	mov	KT_L2_PREFETCHICE_MSB, %g1	! set the key field of ICE-index
	sllx	%g1, 32, %g1			! .
	or	%g4, %g1, %g4			! %g4 = ICE flush index to use

	/*
	 * Generate register addresses to use below.
	 */
	mov	KT_L2_ERR_INJ_REG_MSB, %g4	! set select bits
	sllx	%g4, 32, %g4			! .
	and	%o1, %o4, %g2			! mask paddr to get L2$ bank
	add	%g2, %g4, %g2			! %g2 = L2_ERROR_INJECT_REG

#ifdef	L2_DEBUG_BUFFER
	stx	%o1, [%o3 + 0x0]		! aligned paddr
	stx	%o2, [%o3 + 0x8]		! xor pat + options
	stx	%o4, [%o3 + 0x10]		! bank mask
#endif
	ba	1f				! branch to aligned code
	  nop					! .

	.align	64
1:
	ldx	[%o1], %g3			! get data into L2$ and L1$
	membar	#Sync				! .

	stx	%o2, [%g2]			! enable dir/buf error injection
	ldx	[%g2], %g0			! ensure write completes

	stx	%g3, [%o1]			! place into modified state
	membar	#Sync				! and into write buffer

	/*
	 * Clear the the L1$ entry so access does not hit there.
	 */
2:
	stxa	%g0, [%g6]ASI_DC_TAG		! clear all four ways of L1
	sub	%g5, 1, %g5			! .
	brnz,a	%g5, 2b
	  add	%g6, %g7, %g6			! go to next way

	/*
	 * If the error specified HV mode then perform the
	 * access right here HV mode, otherwise return to kernel.
	 * If debug is enabled then always return to kernel mode.
	 */
#ifndef	L2_DEBUG_BUFFER
	cmp	%o3, %g0			! if HV flag != 0
	bnz,a,pt %icc, 3f			!   perform access in HV mode
	  prefetch [%g4], INVALIDATE_CACHE_LINE	! use prefetch-ICE to flush
#endif						!   data out to DRAM
3:
	membar	#Sync				! for above flush
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(kt_inj_l2wbuf)
#endif	/* lint */

/*
 * This routine is used to corrupt the L2$ directory parity or an entry
 * in one of the L2-cache buffers (see the kt_inj_l2buf routine above)
 * at a location determined by a byte offset.
 *
 * During directory scrub, parity is checked for the directory entry.
 * Directory parity or L2 buffer errors can be planted via the per
 * bank L2_ERROR_INJECT_REG (0x82d.0000.0000).  Bits [9:6] of the offset select
 * the cache bank register, the rest of the bits are ignored.
 *
 * Due to the way the injection register was implemented we actually can't
 * insert an error into a specific location via an offset.  This is a HW
 * limitation.  The next update to the directory or buffer will be in error.
 */

/*
 * Prototype:
 *
 * int kt_inj_l2buf_phys(uint64_t offset, uint_t sdshot_enb, uint64_t *debug,
 *			uint64_t l2_bank_mask);
 *
 * Register usage:
 *
 *	%o1 - byte offset (only bits [9:6] used)
 *	%o2 - sdshot_enb (the combination of enable and sdshot to use)
 *	%o3 - pointer to debug buffer (debug)
 *	%o4 - mask for L2 bank
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
kt_inj_l2buf_phys(uint64_t offset, uint_t sdshot_enb, uint64_t *debug,
			uint64_t l2_bank_mask)
{
	return 0;
}
#else
	.align	64
	ENTRY_NP(kt_inj_l2buf_phys)

	/*
	 * Generate register addresses to use below.
	 */
	mov	KT_L2_ERR_INJ_REG_MSB, %g4	! set select bits
	sllx	%g4, 32, %g4			! .
	and	%o1, %o4, %g2			! mask paddr to get L2$ bank
	add	%g2, %g4, %g2			! %g2 = L2_ERROR_INJECT_REG

	stx	%o2, [%g2]			! enable dir/buf error injection
	ldx	[%g2], %g0			! ensure write completes

	ldx	[%o1], %g3			! do first bad write

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(kt_inj_l2buf_phys)
#endif	/* lint */

/*------------------ end of the L2$ / start of D$ functions ----------------*/

/*
 * The N2 data cache routines in memtest_n2_asm.s are being used
 * with no modifications for KT.
 */

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
 * (this is true for the non-hyperpriv mode routines only though).
 *
 * K/T calculates the i$ parity for ASI stores as well as normal fault-ins.
 * This means i$ data errors can only be injected by setting the parity bit
 * when doing ASI store accesses.  In effect the xor pattern inpar is ignored.
 *
 * NOTE: this routine does not do a tag match but expects that the cache
 *	 has been placed into direct mapped mode before the accesses to
 *	 bring the instructions into the cache.
 *
 * ASI_ICACHE_INSTR (0x66)
 *
 * +-------+-----+-------+------+-------+
 * | Rsvd0 | Way | Index | Word | Rsvd1 |
 * +-------+-----+-------+------+-------+
 *   63:15  14:12  11:6    5:3     2:0
 *
 * Data returned as INSTR[31:0], PARITY[32]
 *
 * NOTE: the diagnostic VA is shifted left one bit from what would normally
 *	 be used to index the cache to fetch an instruction (ASI VA bits[11:3]
 *	 come from PA[10:2]).
 */

/*
 * Prototype:
 *
 * int kt_inj_icache_instr(uint64_t paddr, uint64_t xorpat, uint64_t *debug);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - xorpat (unused)
 *	%o3 - pointer to debug buffer (debug)
 *	%o4 - mask for parity bit (for parity flip, not an inpar)
 *	%g1 - temp
 *	%g2 - unsued
 *	%g3 - temp
 *	%g4 - instr read from cache
 *	%g5 - paddr then addr bits for asi access
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
kt_inj_icache_instr(uint64_t paddr, uint64_t xorpat, uint64_t *debug)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(kt_inj_icache_instr)

	/*
	 * Generate register addresses to use below.
	 */
	mov	%o1, %g5			! save paddr in %g5

#ifdef	L1_DEBUG_BUFFER
	stx	%o1, [%o3]			! paddr
#endif

	mov	1, %g1				! set parity flip bit
	sllx	%g1, 32, %o4			! %o4 = parity flip bit

	mov	0xfff, %g3			! mask paddr for asi access
	sllx	%g3, 2, %g3			! .
	and	%g5, %g3, %g5			! PA[13:2] -> ASI_VA[14:3]
	sllx	%g5, 1, %g5			!  %g5 = instr asi set/way bits

#ifdef	L1_DEBUG_BUFFER
	stx	%o4, [%o3 + 0x10]		! parity bit
	stx	%g5, [%o3 + 0x18]		! asi index for both asis
#endif

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

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(kt_inj_icache_instr)
#endif	/* lint */

/*
 * This routine is used to corrupt the instruction parity of the L1 instruction
 * cache line determined by the paddr.
 *
 * This routine is similar to the above kt_inj_icache_instr() test but here the
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
 * int kt_inj_icache_hvinstr(uint64_t paddr, uint64_t xorpat, uint64_t *debug);
 *
 * Register usage:
 *
 *	%o0 - paddr of routine to corrupt/call
 *	%o1 - paddr (clobbered)
 *	%o2 - xorpat (unused)
 *	%o3 - pointer to debug buffer (debug)
 *	%o4 - mask for parity bit (for parity flip)
 *	%g1 - temp
 *	%g2 - VA for LSU_DIAG_ASI access
 *	%g3 - temp
 *	%g4 - instr read from cache
 *	%g5 - paddr then addr bits for asi access
 *	%g6 - unused
 *	%g7 - return address for called routine
 */
#if defined(lint)
/* ARGSUSED */
int
kt_inj_icache_hvinstr(uint64_t paddr, uint64_t xorpat, uint64_t *debug)
{
	return 0;
}
#else
	.align	256
	ENTRY_NP(kt_inj_icache_hvinstr)

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
	mov	1, %g1				! set parity flip bit
	sllx	%g1, 32, %o4			! %o4 = parity flip bit

	mov	0xfff, %g3			! mask paddr for asi access
	sllx	%g3, 2, %g3			! .
	and	%g5, %g3, %g5			! PA[13:2] -> ASI_VA[14:3]
	sllx	%g5, 1, %g5			!  %g5 = instr asi set/way bits

#ifdef	L1_DEBUG_BUFFER
	stx	%g5, [%o3 + 0x10]		! asi access compare values
#endif
	/*
	 * Place the cache into DM replacement mode prior to access.
	 */
	mov	0x10, %g2			! set i$ direct mapped mode
	ldxa	[%g2]ASI_LSU_DIAG_REG, %g3	!  (preserve d$ mode)
	or	%g3, 0x1, %g3			! .
	stxa	%g3, [%g2]ASI_LSU_DIAG_REG	! .
	membar	#Sync				! .

	/*
	 * Run the (access) routine to corrupt to bring it into cache.
	 * Must ensure that it's not too far away for the jmp.
	 */
	jmp	%o0
	  rd	%pc, %g7

	ba	1f				! branch to aligned code
	  nop					! .

	.align	64
1:
	ldxa	[%g5]ASI_ICACHE_INSTR, %g4	! read the instr to corrupt

#ifdef	L1_DEBUG_BUFFER
	stx	%g5, [%o3 + 0x20]		! the asi/paddr to corrupt
	stx	%g4, [%o3 + 0x28]		! instr data from cache
#endif
	or	%g4, %o4, %g4			! corrupt via parity flip
	stxa	%g4, [%g5]ASI_ICACHE_INSTR	! write it back to i$
	membar	#Sync				! .

	ldxa	[%g2]ASI_LSU_DIAG_REG, %g3	! restore i$ replacement mode
	andn	%g3, 0x1, %g3			! .
	stxa	%g3, [%g2]ASI_LSU_DIAG_REG	! .
	membar	#Sync				! .

	/*
	 * Trigger the planted error by calling the routine that was
	 * corrupted above w/o leaving hypervisor context.
	 */
	jmp	%o0
	  rd	%pc, %g7

	mov	%g0, %o0			! set return value to PASS
	done					! return
	SET_SIZE(kt_inj_icache_hvinstr)
#endif	/* lint */

/*
 * Version of the asmld routine which is to be called while running in
 * hyperpriv mode ONLY from the above kt_inj_icache_hvinstr() function.
 */
#if defined(lint)
void
kt_ic_hvaccess(void)
{}
#else
	ENTRY(kt_ic_hvaccess)
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
	SET_SIZE(kt_ic_hvaccess)
#endif	/* lint */

/*
 * This routine is used to corrupt the tag parity (including valid bit parity)
 * of the L1 instruction cache line determined by the paddr.  This routine is
 * similar to the above kt_inj_icache_instr() test but the tag is the target
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
 * Data stored as TAG[34:2] = PA[43:11], VM[1]
 * Data returned as TAG[34:2] = PA[43:11], PARITY[35], VM[1], VS[0]
 *
 * NOTE: this routine does a tag match but expects that the cache
 *	 has been placed into direct mapped mode before the accesses to
 *	 bring the instructions into the cache.
 */

/*
 * Prototype:
 *
 * int kt_inj_icache_tag(uint64_t paddr, uint64_t xorpat, uint64_t *debug);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - xorpat (parity[16], valid[15])
 *	%o3 - pointer to debug buffer (debug)
 *	%o4 - unused
 *	%g1 - unused
 *	%g2 - tag for compare
 *	%g3 - temp
 *	%g4 - tag read from cache
 *	%g5 - addr bits for asi access
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
kt_inj_icache_tag(uint64_t paddr, uint64_t xorpat, uint64_t *debug)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(kt_inj_icache_tag)

	/*
	 * Generate register addresses to use below.
	 */
	mov	%o1, %g5			! copy paddr to build asi addr
	sllx	%g5, 20, %g2			! shift paddr for tag compare
	srlx	%g2, 31, %g2			! .
	sllx	%g2, 2, %g2			! %g2 = tag asi tag cmp value

	mov	0x1ff, %g3			! mask paddr for asi accesses
	sllx	%g3, 5, %g3			! .
	and	%g5, %g3, %g5			! PA[13:5] -> ASI_VA[14:6]
	sllx	%g5, 1, %g5			!  %g5 = tag asi set/way bits

#ifdef	L1_DEBUG_BUFFER
	stx	%o1, [%o3 + 0x0]		! paddr
	stx	%g2, [%o3 + 0x8]		! tag compare value
	stx	%g5, [%o3 + 0x10]		! asi vaddr for read/write
#endif
	/*
	 * Read tag to ensure that target data has not been evicted.
	 */
	ldxa	[%g5]ASI_ICACHE_TAG, %g4	! read tag for this way

#ifdef	L1_DEBUG_BUFFER
	stx	%g4, [%o3 + 0x20]		! raw tag value read
#endif

	sllx	%g4, 29, %g4			! remove the parity bit [35]
	srlx	%g4, 31, %g4			!   and the valid bits by
	sllx	%g4, 2, %g4			!   shifting them out of reg
	cmp	%g4, %g2			! does the tag match target?
	bne,a	%xcc, 2f			! if not found then
	  mov	KT_DATA_NOT_FOUND, %o1		!   return value == error

	/*
	 * Corrupt the line.
	 */
1:
	or	%g5, %o2, %g5			! corrupt parity or valid bit
	or	%g4, 3, %g4			! force the valid bits
	stxa	%g4, [%g5]ASI_ICACHE_TAG	! write tag back to i$
	membar	#Sync				! .
2:

#ifdef	L1_DEBUG_BUFFER
	stx	%g4, [%o3 + 0x28]		! cmp value read from tag
						!   with lower bits forced set
	ldxa	[%g5]ASI_ICACHE_TAG, %g4	! re-read tag
	stx	%g4, [%o3 + 0x30]		! current written tag value
#endif

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(kt_inj_icache_tag)
#endif	/* lint */

/*
 * This routine is used to corrupt the tag parity of the L1 instruction
 * cache line determined by the paddr.
 *
 * This routine is similar to the above kt_inj_icache_tag() test but here the
 * routine to corrupt is called, corrupted, and called again (to trigger the
 * error) all while in hyperpriv mode.
 */

/*
 * Prototype:
 *
 * int kt_inj_icache_hvtag(uint64_t paddr, uint64_t xorpat, uint64_t *debug);
 *
 * Register usage:
 *
 *	%o0 - saved paddr of routine to corrupt/call (not an inpar)
 *	%o1 - paddr (clobbered)
 *	%o2 - xorpat (parity[16], valid[15])
 *	%o3 - pointer to debug buffer (debug)
 *	%o4 - mask for parity bit
 *	%g1 - unused
 *	%g2 - VA for LSU_DIAG_ASI access
 *	%g3 - temp
 *	%g4 - tag read from cache
 *	%g5 - addr bits for asi access
 *	%g6 - unused
 *	%g7 - return address for called routine
 */
#if defined(lint)
/* ARGSUSED */
int
kt_inj_icache_hvtag(uint64_t paddr, uint64_t xorpat, uint64_t *debug)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(kt_inj_icache_hvtag)

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
	mov	0x1ff, %g3			! mask paddr for asi accesses
	sllx	%g3, 5, %g3			! .
	and	%g5, %g3, %g5			! PA[13:5] -> ASI_VA[14:6]
	sllx	%g5, 1, %g5			!  %g5 = tag asi set/way bits

	/*
	 * Place the cache into DM replacement mode prior to access.
	 */
	mov	0x10, %g2			! set i$ direct mapped mode
	ldxa	[%g2]ASI_LSU_DIAG_REG, %g3	!  (preserve d$ mode)
	or	%g3, 0x1, %g3			! .
	stxa	%g3, [%g2]ASI_LSU_DIAG_REG	! .
	membar	#Sync				! .

	/*
	 * Run the (access) routine to corrupt to bring it into cache.
	 * Must ensure that it's not too far away for the jmp.
	 */
	jmp	%o0
          rd    %pc, %g7

	ba	1f				! branch to aligned code
	  nop					! .

	.align	64
1:
	ldxa	[%g5]ASI_ICACHE_TAG, %g4	! read tag to corrupt
	or	%g5, %o2, %g5			! corrupt parity or valid bit
	stxa	%g4, [%g5]ASI_ICACHE_TAG	! write tag back to i$
	membar	#Sync				! .

	ldxa	[%g2]ASI_LSU_DIAG_REG, %g3	! restore i$ replacement mode
	andn	%g3, 0x1, %g3			! .
	stxa	%g3, [%g2]ASI_LSU_DIAG_REG	! .
	membar	#Sync				! .

	/*
	 * Trigger the planted error by calling the routine that was
	 * corrupted above while still in hypervisor context.
	 */
	jmp	%o0
	  rd	%pc, %g7

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(kt_inj_icache_hvtag)
#endif	/* lint */

/*
 * This routine is used to place multiple copies of the same tag into the
 * L1 instruction cache at locations determined by a paddr argument.
 *
 * The critical section of each i$ routine must fit on one L2$ line to
 * avoid unwanted side effects.
 *
 * Note that the priority of i$ errors is ICVP -> ICTP -> ICTM -> ICDP.
 *
 * NOTE: this routine does a tag match but expects that the cache
 *	 has been placed into direct mapped mode before the accesses to
 *	 bring the instructions into the cache.
 */

/*
 * Prototype:
 *
 * int kt_inj_icache_mult(uint64_t paddr, uint64_t xorpat, uint64_t *debug);
 *
 * Register usage:
 *
 *	%o1 - paddr
 *	%o2 - xorpat inpar (unused), way bit mask for copy
 *	%o3 - pointer to debug buffer (debug)
 *	%o4 - unused
 *	%g1 - unused
 *	%g2 - tag for compare
 *	%g3 - temp
 *	%g4 - tag read from cache
 *	%g5 - addr bits for asi access
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
int
kt_inj_icache_mult(uint64_t paddr, uint64_t xorpat, uint64_t *debug)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(kt_inj_icache_mult)

	/*
	 * Generate register addresses to use below.
	 */
	mov	%o1, %g5			! copy paddr to build asi addr
	sllx	%g5, 20, %g2			! shift paddr for tag compare
	srlx	%g2, 31, %g2			! .
	sllx	%g2, 2, %g2			! %g2 = tag asi tag cmp value

	mov	0x1ff, %g3			! mask paddr for asi accesses
	sllx	%g3, 5, %g3			! .
	and	%g5, %g3, %g5			! PA[13:5] -> ASI_VA[14:6]
	sllx	%g5, 1, %g5			!  %g5 = tag asi set/way bits

	/*
	 * Read tag to ensure that target data has not been evicted.
	 */
	ldxa	[%g5]ASI_ICACHE_TAG, %g4	! read tag for this way
	sllx	%g4, 29, %g4			! remove the parity bit [35]
	srlx	%g4, 31, %g4			!   and the valid bits by
	sllx	%g4, 2, %g4			!   shifting them out of reg
	cmp	%g4, %g2			! does the tag match target?
	bne,a	%xcc, 2f			! if not found then
	  mov	KT_DATA_NOT_FOUND, %o1		!   return value == error

	mov	7, %o2				! %o2 = way mask for xor
	sllx	%o2, 12, %o2			! .

	/*
	 * Insert a copy of the matching tag in another way.
	 */
1:
	or	%g4, 3, %g4			! force the valid bits
	stxa	%g4, [%g5]ASI_ICACHE_TAG	! re-write original tag to i$
	xor	%g5, %o2, %g5			! invert way bits for new way
	stxa	%g4, [%g5]ASI_ICACHE_TAG	! write tag copy to i$
	membar	#Sync				! .
2:
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(kt_inj_icache_mult)
#endif	/* lint */

/*
 * This routine is used to place multiple copies of the same tag into the
 * L1 instruction cache at locations determined by a paddr or byte offset
 * argument.
 *
 * This routine is similar to the above kt_inj_icache_mult() test but here the
 * routine to corrupt is accessed, the tags are written, and then 
 * accessed again (to trigger the error) all while in hyperpriv mode.
 *
 * NOTE: this is using the existing K/T i$ access routine for the corruption.
 * 	 A new routine may need to be created specifically for this routine
 *	 if code moves/changes and this routine stops working.
 */

/*
 * Prototype:
 *
 * int kt_inj_icache_hvmult(uint64_t paddr, uint64_t xorpat, uint64_t *debug);
 *
 * Register usage:
 *
 *	%o0 - saved paddr of routine to corrupt/call (not an inpar)
 *	%o1 - paddr (clobbered)
 *	%o2 - xorpat (unused) then xor mask to use on way bits
 *	%o3 - pointer to debug buffer (debug)
 *	%o4 - unused
 *	%g1 - unused
 *	%g2 - VA for LSU_DIAG_ASI access
 *	%g3 - temp
 *	%g4 - tag read from cache
 *	%g5 - addr bits for asi access
 *	%g6 - unused
 *	%g7 - return address for called routine
 */
#if defined(lint)
/* ARGSUSED */
int
kt_inj_icache_hvmult(uint64_t paddr, uint64_t xorpat, uint64_t *debug)
{
	return 0;
}
#else
	.align	128
	ENTRY_NP(kt_inj_icache_hvmult)

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
	mov	0x1ff, %g3			! mask paddr for asi accesses
	sllx	%g3, 5, %g3			! .
	and	%g5, %g3, %g5			! PA[13:5] -> ASI_VA[14:6]
	sllx	%g5, 1, %g5			!  %g5 = tag asi set/way bits

	mov	7, %o2				! %o2 = way mask for xor
	sllx	%o2, 12, %o2			! .

	/*
	 * Place the cache into DM replacement mode prior to access.
	 */
	mov	0x10, %g2			! set i$ direct mapped mode
	ldxa	[%g2]ASI_LSU_DIAG_REG, %g3	!  (preserve d$ mode)
	or	%g3, 0x1, %g3			! .
	stxa	%g3, [%g2]ASI_LSU_DIAG_REG	! .
	membar	#Sync				! .

	/*
	 * Run the routine to corrupt to bring it into cache.
	 * Must ensure that it's not too far away for the jmp.
	 */
	jmp	%o0
	  rd	%pc, %g7

	ba	1f				! branch to aligned code
	  nop					! .

	.align	64
1:
	/*
	 * Read tag, then insert a copy of it in another way.
	 */
	ldxa	[%g5]ASI_ICACHE_TAG, %g4	! read tag for this way
	xor	%g5, %o2, %g5			! invert way bits for new way
	stxa	%g4, [%g5]ASI_ICACHE_TAG	! write tag copy to i$
	membar	#Sync				! .

	ldxa	[%g2]ASI_LSU_DIAG_REG, %g3	! restore i$ replacement mode
	andn	%g3, 0x1, %g3			! .
	stxa	%g3, [%g2]ASI_LSU_DIAG_REG	! .
	membar	#Sync				! .

	/*
	 * Trigger the planted error by calling the routine that was
	 * corrupted above w/o leaving hypervisor context.
	 */
	jmp	%o0
	  rd	%pc, %g7

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(kt_inj_icache_hvmult)
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
 * NOTE: this routine is almost identical to the N2 version in file
 *	 memtest_n2_asm.s but the parking MACROs had to change for
 *	 KT to account for the additional strands.  See the comments
 *	 above the N2 version (n2_inj_ireg_file()) for more information.
 */

/*
 * Prototype:
 *
 * int kt_inj_ireg_file(uint64_t buf_paddr, uint64_t enable, uint64_t eccmask,
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
kt_inj_ireg_file(uint64_t buf_paddr, uint64_t enable, uint64_t eccmask,
				uint64_t offset)
{
	return 0;
}
#else
	.align	512
	ENTRY_NP(kt_inj_ireg_file)

	/*
	 * Park all sibling strands via the CORE_RUNNING regs so that the
	 * injection will only occur on this strand.
	 */
	KT_PARK_SIBLING_STRANDS(%g7, %g6, %g5, %g4, %g3)

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
	KT_UNPARK_SIBLING_STRANDS(%g7, %g6, %g4, %g3)

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(kt_inj_ireg_file)
#endif	/* lint */

/*
 * This routine is used to corrupt the ecc of the integer register file
 * using the per core error injection register.  Because a single error
 * injection register is shared by all strands only a single strand should
 * be active for this routine to produce deterministic results.
 *
 * The method used is similar to the above kt_inj_ireg_file() routine,
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
 *
 * NOTE: this routine is almost identical to the N2 version in file
 *	 memtest_n2_asm.s but the parking MACROs had to change for
 *	 KT to account for the additional strands.  See the comments above
 *	 the N2 version (n2_inj_ireg_hvfile_global()) for more information.
 */

/*
 * Prototype:
 *
 * int kt_inj_ireg_hvfile_global(uint64_t buf_paddr, uint64_t enable,
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
kt_inj_ireg_hvfile_global(uint64_t buf_paddr, uint64_t enable, uint64_t eccmask,
				uint64_t offset)
{
	return 0;
}
#else
	.align	512
	ENTRY_NP(kt_inj_ireg_hvfile_global)

	/*
	 * Park all sibling strands via the CORE_RUNNING regs so that the
	 * injection will only occur on this strand.
	 */
	KT_PARK_SIBLING_STRANDS(%g7, %g6, %g5, %g4, %g3)

	/*
	 * The number of instructions after the read of the %pc must
	 * remain constant (based on EI_IRF_ERR_STRIDE) and match the
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
	KT_UNPARK_SIBLING_STRANDS(%o5, %o4, %o3, %o2)

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
	SET_SIZE(kt_inj_ireg_hvfile_global)
#endif	/* lint */

/*
 * This routine is used to corrupt the ecc of the integer register file
 * using the per core error injection register.  Because a single error
 * injection register is shared by all strands this routine parks the
 * sibling strands during the error injection in order to produce
 * deterministic results.
 *
 * The method used is similar to the above kt_inj_ireg_hvfile_global() routine,
 * but this routine has the ability to inject an error into any of the
 * eight "in" registers.  Injection into other register sets are handled by
 * separate routines.
 *
 * NOTE: this routine is almost identical to the N2 version in file
 *	 memtest_n2_asm.s but the parking MACROs had to change for
 *	 KT to account for the additional strands.  See the comments above
 *	 the N2 version (n2_inj_ireg_hvfile_in()) for more information.
 */

/*
 * Prototype:
 *
 * int kt_inj_ireg_hvfile_in(uint64_t buf_paddr, uint64_t enable,
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
kt_inj_ireg_hvfile_in(uint64_t buf_paddr, uint64_t enable, uint64_t eccmask,
				uint64_t offset)
{
	return 0;
}
#else
	.align	512
	ENTRY_NP(kt_inj_ireg_hvfile_in)

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
	KT_PARK_SIBLING_STRANDS(%g7, %g6, %g5, %g4, %g3)

	/*
	 * The number of instructions after the read of the %pc must
	 * remain constant (based on EI_IRF_ERR_STRIDE) and match the
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
	KT_UNPARK_SIBLING_STRANDS(%g7, %g6, %g4, %g3)

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
	SET_SIZE(kt_inj_ireg_hvfile_in)
#endif	/* lint */

/*
 * This routine is used to corrupt the ecc of the integer register file
 * using the per core error injection register.  Because a single error
 * injection register is shared by all strands this routine parks the
 * sibling strands during the error injection in order to produce
 * deterministic results.
 *
 * The method used is similar to the above kt_inj_ireg_hvfile_global() routine,
 * but this routine has the ability to inject an error into any of the
 * eight "local" registers.  Injection into other register sets are handled
 * by separate routines.
 *
 * NOTE: this routine is almost identical to the N2 version in file
 *	 memtest_n2_asm.s but the parking MACROs had to change for
 *	 KT to account for the additional strands.  See the comments above
 *	 the N2 version (n2_inj_ireg_hvfile_local()) for more information.
 */

/*
 * Prototype:
 *
 * int kt_inj_ireg_hvfile_local(uint64_t buf_paddr, uint64_t enable,
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
kt_inj_ireg_hvfile_local(uint64_t buf_paddr, uint64_t enable, uint64_t eccmask,
				uint64_t offset)
{
	return 0;
}
#else
	.align	512
	ENTRY_NP(kt_inj_ireg_hvfile_local)

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
	KT_PARK_SIBLING_STRANDS(%g7, %g6, %g5, %g4, %g3)

	/*
	 * The number of instructions after the read of the %pc must
	 * remain constant (based on EI_IRF_ERR_STRIDE) and match the
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
	KT_UNPARK_SIBLING_STRANDS(%g7, %g6, %g4, %g3)

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
	SET_SIZE(kt_inj_ireg_hvfile_local)
#endif	/* lint */

/*
 * This routine is used to corrupt the ecc of the integer register file
 * using the per core error injection register.  Because a single error
 * injection register is shared by all strands this routine parks the
 * sibling strands during the error injection in order to produce
 * deterministic results.
 *
 * The method used is similar to the above kt_inj_ireg_hvfile_global() routine,
 * but this routine has the ability to inject an error into any of the
 * eight "out" registers.  Injection into other register sets are handled
 * by separate routines.  Note that this routine is structured a little
 * differently than the others because it's using the out registers.
 *
 * NOTE: this routine is almost identical to the N2 version in file
 *	 memtest_n2_asm.s but the parking MACROs had to change for
 *	 KT to account for the additional strands.  See the comments above
 *	 the N2 version (n2_inj_ireg_hvfile_out()) for more information.
 */

/*
 * Prototype:
 *
 * int kt_inj_ireg_hvfile_out(uint64_t buf_paddr, uint64_t enable,
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
kt_inj_ireg_hvfile_out(uint64_t buf_paddr, uint64_t enable, uint64_t eccmask,
				uint64_t offset)
{
	return 0;
}
#else
	.align	512
	ENTRY_NP(kt_inj_ireg_hvfile_out)

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
	KT_PARK_SIBLING_STRANDS(%g7, %g6, %g5, %g4, %g3)

	/*
	 * The number of instructions after the read of the %pc must
	 * remain constant (based on EI_IRF_ERR_STRIDE) and match the
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
	KT_UNPARK_SIBLING_STRANDS(%g7, %g6, %g4, %g3)

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
	SET_SIZE(kt_inj_ireg_hvfile_out)
#endif	/* lint */

/*
 * This routine is used to corrupt the ecc of the floating-point register
 * file using the per core error injection register.  Because a single error
 * injection register is shared by all strands only a single strand should
 * be active for this routine to produce deterministic results.
 *
 * The instrs to use for the floating point ops are:
 *	ld	%f0 - %f31	! 32 bit ops
 *	ldd	%f0 - %f62	! 64 bit ops (aliased to even fregs, are 32)
 *	ldq	%f0 - %f60	! 128 bit ops (aliased to 4th freg, are 16)
 *
 * XXX	currently blowing away the target floating point reg
 *	which may affect other processes if they are being used.
 *	Also unlike previous unjection code there is a larger window
 *	for other processes to trip the armed injection reg.  Can add
 *	parking macros to this routine if this test is to be run
 *	in a stress environment.  Park before arm, and unpark before access
 *	like the integer register file routines do.
 */

/*
 * Prototype:
 *
 * int kt_inj_freg_file(uint64_t buf_paddr, uint64_t enable, uint64_t eccmask,
 *				uint64_t offset);
 *
 * Register usage:
 *
 *	%o0 - holds extra flags from top half of enable (not an inpar)
 *	%o1 - paddr of mem scratch buffer
 *	%o2 - enable bit and single shot (clobbered)
 *	%o3 - ecc mask
 *	%o4 - offset to select the register to access
 *	%g1 - temp
 *	%g2 - temp
 *	%g3 - temp
 *	%g4 - saved fprs register contents
 *	%g5 - saved pstate register contents
 *	%g6 - holds single-shot flag
 *	%g7 - temp
 */
#if defined(lint)
/* ARGSUSED */
int
kt_inj_freg_file(uint64_t buf_paddr, uint64_t enable, uint64_t eccmask,
				uint64_t offset)
{
	return 0;
}
#else
	.align	8192
	ENTRY_NP(kt_inj_freg_file)

	/*
	 * Enable the floating point unit.
	 */
	rdpr	%pstate, %g5			! save the processor state
	or	%g5, PSTATE_PEF, %g4		! and enable the FPU
	wrpr	%g4, %g0, %pstate		! .

	rd	%fprs, %g4			! save the fp state
	or	%g4, FPRS_FEF, %g3		! and enable the FPU
	wr	%g0, %g3, %fprs			! .

	srlx	%o2, EI_REG_SSHOT_ENB_SHIFT, %g6 ! %g6 = saved SSHOT flag
	and	%g6, 1, %g6			! .

	srlx	%o2, 32, %o0			! %o0 = access flags

	/*
	 * Save the contents of the target register in case it needs
	 * to be restored after the injection.
	 *
	 * NOTE: this routine does not restore any FP registers yet!
	 */
	cmp   %o4, 0;  be,a %icc, 1f; st  %f0,  [%o1 + 0]
	cmp   %o4, 1;  be,a %icc, 1f; st  %f1,  [%o1 + 0]
	cmp   %o4, 2;  be,a %icc, 1f; st  %f2,  [%o1 + 0]
	cmp   %o4, 3;  be,a %icc, 1f; st  %f3,  [%o1 + 0]
	cmp   %o4, 4;  be,a %icc, 1f; st  %f4,  [%o1 + 0]
	cmp   %o4, 5;  be,a %icc, 1f; st  %f5,  [%o1 + 0]
	cmp   %o4, 6;  be,a %icc, 1f; st  %f6,  [%o1 + 0]
	cmp   %o4, 7;  be,a %icc, 1f; st  %f7,  [%o1 + 0]
	cmp   %o4, 8;  be,a %icc, 1f; st  %f8,  [%o1 + 0]
	cmp   %o4, 9;  be,a %icc, 1f; st  %f9,  [%o1 + 0]
	cmp   %o4, 10; be,a %icc, 1f; st  %f10, [%o1 + 0]
	cmp   %o4, 11; be,a %icc, 1f; st  %f11, [%o1 + 0]
	cmp   %o4, 12; be,a %icc, 1f; st  %f12, [%o1 + 0]
	cmp   %o4, 13; be,a %icc, 1f; st  %f13, [%o1 + 0]
	cmp   %o4, 14; be,a %icc, 1f; st  %f14, [%o1 + 0]
	cmp   %o4, 15; be,a %icc, 1f; st  %f15, [%o1 + 0]

	cmp   %o4, 16; be,a %icc, 1f; st  %f16, [%o1 + 0]
	cmp   %o4, 17; be,a %icc, 1f; st  %f17, [%o1 + 0]
	cmp   %o4, 18; be,a %icc, 1f; st  %f18, [%o1 + 0]
	cmp   %o4, 19; be,a %icc, 1f; st  %f19, [%o1 + 0]
	cmp   %o4, 20; be,a %icc, 1f; st  %f20, [%o1 + 0]
	cmp   %o4, 21; be,a %icc, 1f; st  %f21, [%o1 + 0]
	cmp   %o4, 22; be,a %icc, 1f; st  %f22, [%o1 + 0]
	cmp   %o4, 23; be,a %icc, 1f; st  %f23, [%o1 + 0]
	cmp   %o4, 24; be,a %icc, 1f; st  %f24, [%o1 + 0]
	cmp   %o4, 25; be,a %icc, 1f; st  %f25, [%o1 + 0]
	cmp   %o4, 26; be,a %icc, 1f; st  %f26, [%o1 + 0]
	cmp   %o4, 27; be,a %icc, 1f; st  %f27, [%o1 + 0]
	cmp   %o4, 28; be,a %icc, 1f; st  %f28, [%o1 + 0]
	cmp   %o4, 29; be,a %icc, 1f; st  %f29, [%o1 + 0]
	cmp   %o4, 30; be,a %icc, 1f; st  %f30, [%o1 + 0]
	cmp   %o4, 31; be,a %icc, 1f; st  %f31, [%o1 + 0]

	cmp   %o4, 32; be,a %icc, 1f; std %f0,  [%o1 + 0]
	cmp   %o4, 33; be,a %icc, 1f; std %f2,  [%o1 + 0]
	cmp   %o4, 34; be,a %icc, 1f; std %f4,  [%o1 + 0]
	cmp   %o4, 35; be,a %icc, 1f; std %f6,  [%o1 + 0]
	cmp   %o4, 36; be,a %icc, 1f; std %f8,  [%o1 + 0]
	cmp   %o4, 37; be,a %icc, 1f; std %f10, [%o1 + 0]
	cmp   %o4, 38; be,a %icc, 1f; std %f12, [%o1 + 0]
	cmp   %o4, 39; be,a %icc, 1f; std %f14, [%o1 + 0]
	cmp   %o4, 40; be,a %icc, 1f; std %f16, [%o1 + 0]
	cmp   %o4, 41; be,a %icc, 1f; std %f18, [%o1 + 0]
	cmp   %o4, 42; be,a %icc, 1f; std %f20, [%o1 + 0]
	cmp   %o4, 43; be,a %icc, 1f; std %f22, [%o1 + 0]
	cmp   %o4, 44; be,a %icc, 1f; std %f24, [%o1 + 0]
	cmp   %o4, 45; be,a %icc, 1f; std %f26, [%o1 + 0]
	cmp   %o4, 46; be,a %icc, 1f; std %f28, [%o1 + 0]
	cmp   %o4, 47; be,a %icc, 1f; std %f30, [%o1 + 0]

	cmp   %o4, 48; be,a %icc, 1f; std %f32, [%o1 + 0]
	cmp   %o4, 49; be,a %icc, 1f; std %f34, [%o1 + 0]
	cmp   %o4, 50; be,a %icc, 1f; std %f36, [%o1 + 0]
	cmp   %o4, 51; be,a %icc, 1f; std %f38, [%o1 + 0]
	cmp   %o4, 52; be,a %icc, 1f; std %f40, [%o1 + 0]
	cmp   %o4, 53; be,a %icc, 1f; std %f42, [%o1 + 0]
	cmp   %o4, 54; be,a %icc, 1f; std %f44, [%o1 + 0]
	cmp   %o4, 55; be,a %icc, 1f; std %f46, [%o1 + 0]
	cmp   %o4, 56; be,a %icc, 1f; std %f48, [%o1 + 0]
	cmp   %o4, 57; be,a %icc, 1f; std %f50, [%o1 + 0]
	cmp   %o4, 58; be,a %icc, 1f; std %f52, [%o1 + 0]
	cmp   %o4, 59; be,a %icc, 1f; std %f54, [%o1 + 0]
	cmp   %o4, 60; be,a %icc, 1f; std %f56, [%o1 + 0]
	cmp   %o4, 61; be,a %icc, 1f; std %f58, [%o1 + 0]
	cmp   %o4, 62; be,a %icc, 1f; std %f60, [%o1 + 0]
	cmp   %o4, 63; be,a %icc, 1f; std %f62, [%o1 + 0]
1:
	/*
	 * Set up and arm the injection register.
	 */
	membar	#Sync				! required
	stxa	%o3, [%g0]ASI_INJECT_ERROR_REG	! set the eccmask bits
	or	%o2, %o3, %o2			! combine mask and enable

	KT_PARK_SIBLING_STRANDS(%g1, %g2, %g3, %g7, %o3)

	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode

	cmp   %o4, 0;  be,a %icc, 2f; ld	[%o1], %f0
	cmp   %o4, 1;  be,a %icc, 2f; fnot2s	%f1, %f1
	cmp   %o4, 2;  be,a %icc, 2f; fmovs 	%f2, %f2
	cmp   %o4, 3;  be,a %icc, 2f; fzeros	%f3
	cmp   %o4, 4;  be,a %icc, 2f; ld	[%o1], %f4
	cmp   %o4, 5;  be,a %icc, 2f; fnot2s	%f5, %f5
	cmp   %o4, 6;  be,a %icc, 2f; fmovs 	%f6, %f6
	cmp   %o4, 7;  be,a %icc, 2f; fzeros	%f7
	cmp   %o4, 8;  be,a %icc, 2f; ld	[%o1], %f8
	cmp   %o4, 9;  be,a %icc, 2f; fnot2s	%f9, %f9
	cmp   %o4, 10; be,a %icc, 2f; fmovs 	%f10, %f10
	cmp   %o4, 11; be,a %icc, 2f; fzeros	%f11
	cmp   %o4, 12; be,a %icc, 2f; ld	[%o1], %f12
	cmp   %o4, 13; be,a %icc, 2f; fnot2s	%f13, %f13
	cmp   %o4, 14; be,a %icc, 2f; fmovs 	%f14, %f14
	cmp   %o4, 15; be,a %icc, 2f; fzeros	%f15

	cmp   %o4, 16; be,a %icc, 2f; ld	[%o1], %f16
	cmp   %o4, 17; be,a %icc, 2f; fnot2s	%f17, %f17
	cmp   %o4, 18; be,a %icc, 2f; fmovs	%f18, %f18
	cmp   %o4, 19; be,a %icc, 2f; fzeros	%f19
	cmp   %o4, 20; be,a %icc, 2f; ld	[%o1], %f20
	cmp   %o4, 21; be,a %icc, 2f; fnot2s	%f21, %f21
	cmp   %o4, 22; be,a %icc, 2f; fmovs	%f22, %f22
	cmp   %o4, 23; be,a %icc, 2f; fzeros	%f23
	cmp   %o4, 24; be,a %icc, 2f; ld	[%o1], %f24
	cmp   %o4, 25; be,a %icc, 2f; fnot2s	%f25, %f25
	cmp   %o4, 26; be,a %icc, 2f; fmovs	%f26, %f26
	cmp   %o4, 27; be,a %icc, 2f; fzeros	%f27
	cmp   %o4, 28; be,a %icc, 2f; ld	[%o1], %f28
	cmp   %o4, 29; be,a %icc, 2f; fnot2s	%f29, %f29
	cmp   %o4, 30; be,a %icc, 2f; fmovs	%f30, %f30
	cmp   %o4, 31; be,a %icc, 2f; fzeros	%f31

	cmp   %o4, 32; be,a %icc, 2f; ldd	[%o1], %f0
	cmp   %o4, 33; be,a %icc, 2f; fnot2	%f2, %f2
	cmp   %o4, 34; be,a %icc, 2f; fmovd	%f4, %f4
	cmp   %o4, 35; be,a %icc, 2f; fzero	%f6
	cmp   %o4, 36; be,a %icc, 2f; ldd	[%o1], %f8
	cmp   %o4, 37; be,a %icc, 2f; fnot2	%f10, %f10
	cmp   %o4, 38; be,a %icc, 2f; fmovd	%f12, %f12
	cmp   %o4, 39; be,a %icc, 2f; fzero	%f14
	cmp   %o4, 40; be,a %icc, 2f; ldd	[%o1], %f16
	cmp   %o4, 41; be,a %icc, 2f; fnot2	%f18, %f18
	cmp   %o4, 42; be,a %icc, 2f; fmovd	%f20, %f20
	cmp   %o4, 43; be,a %icc, 2f; fzero	%f22
	cmp   %o4, 44; be,a %icc, 2f; ldd	[%o1], %f24
	cmp   %o4, 45; be,a %icc, 2f; fnot2	%f26, %f26
	cmp   %o4, 46; be,a %icc, 2f; fmovd	%f28, %f28
	cmp   %o4, 47; be,a %icc, 2f; fzero	%f30

	cmp   %o4, 48; be,a %icc, 2f; ldd	[%o1], %f32
	cmp   %o4, 49; be,a %icc, 2f; fnot2	%f34, %f34
	cmp   %o4, 50; be,a %icc, 2f; fmovd	%f36, %f36
	cmp   %o4, 51; be,a %icc, 2f; fzero	%f38
	cmp   %o4, 52; be,a %icc, 2f; ldd	[%o1], %f40
	cmp   %o4, 53; be,a %icc, 2f; fnot2	%f42, %f42
	cmp   %o4, 54; be,a %icc, 2f; fmovd	%f44, %f44
	cmp   %o4, 55; be,a %icc, 2f; fzero	%f46
	cmp   %o4, 56; be,a %icc, 2f; ldd	[%o1], %f48
	cmp   %o4, 57; be,a %icc, 2f; fnot2	%f50, %f50
	cmp   %o4, 58; be,a %icc, 2f; fmovd	%f52, %f52
	cmp   %o4, 59; be,a %icc, 2f; fzero	%f54
	cmp   %o4, 60; be,a %icc, 2f; ldd	[%o1], %f56
	cmp   %o4, 61; be,a %icc, 2f; fnot2	%f58, %f58
	cmp   %o4, 62; be,a %icc, 2f; fmovd	%f60, %f60
	cmp   %o4, 63; be,a %icc, 2f; fzero	%f62
2:
	/*
	 * If single shot mode was specified, then clear the injection reg.
	 */
	membar	#Sync				! required
	brnz,a	%g6, 3f				! check if sshot mode set
	  stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	!   if so clear injection reg
3:
	KT_UNPARK_SIBLING_STRANDS(%g1, %g2, %g3, %g6)

	/*
	 * Perform the access right here in HV mode, the access type is
	 * based on the value placed in the top half of the enable value.
	 */
	cmp	%o0, EI_REG_CMP_NOERR		! if NOERR flag bit set
	bz,a	%icc, 6f			!   skip access section
	  nop					! .

	cmp	%o0, EI_REG_CMP_OP		! if OP flag bit set
	bz,a	%icc, 4f			!   goto OP access section
	  nop					! .

	cmp	%o0, EI_REG_CMP_LOAD		! if LOAD flag bit set
	bz,a	%icc, 5f			!   goto LOAD access section
	  nop					! .

	/*
	 * Invoke the error via a STORE (default)
	 */
	cmp   %o4, 0;  be,a %icc, 6f; st  %f0,  [%o1 + 16]
	cmp   %o4, 1;  be,a %icc, 6f; st  %f1,  [%o1 + 16]
	cmp   %o4, 2;  be,a %icc, 6f; st  %f2,  [%o1 + 16]
	cmp   %o4, 3;  be,a %icc, 6f; st  %f3,  [%o1 + 16]
	cmp   %o4, 4;  be,a %icc, 6f; st  %f4,  [%o1 + 16]
	cmp   %o4, 5;  be,a %icc, 6f; st  %f5,  [%o1 + 16]
	cmp   %o4, 6;  be,a %icc, 6f; st  %f6,  [%o1 + 16]
	cmp   %o4, 7;  be,a %icc, 6f; st  %f7,  [%o1 + 16]
	cmp   %o4, 8;  be,a %icc, 6f; st  %f8,  [%o1 + 16]
	cmp   %o4, 9;  be,a %icc, 6f; st  %f9,  [%o1 + 16]
	cmp   %o4, 10; be,a %icc, 6f; st  %f10, [%o1 + 16]
	cmp   %o4, 11; be,a %icc, 6f; st  %f11, [%o1 + 16]
	cmp   %o4, 12; be,a %icc, 6f; st  %f12, [%o1 + 16]
	cmp   %o4, 13; be,a %icc, 6f; st  %f13, [%o1 + 16]
	cmp   %o4, 14; be,a %icc, 6f; st  %f14, [%o1 + 16]
	cmp   %o4, 15; be,a %icc, 6f; st  %f15, [%o1 + 16]

	cmp   %o4, 16; be,a %icc, 6f; st  %f16, [%o1 + 16]
	cmp   %o4, 17; be,a %icc, 6f; st  %f17, [%o1 + 16]
	cmp   %o4, 18; be,a %icc, 6f; st  %f18, [%o1 + 16]
	cmp   %o4, 19; be,a %icc, 6f; st  %f19, [%o1 + 16]
	cmp   %o4, 20; be,a %icc, 6f; st  %f20, [%o1 + 16]
	cmp   %o4, 21; be,a %icc, 6f; st  %f21, [%o1 + 16]
	cmp   %o4, 22; be,a %icc, 6f; st  %f22, [%o1 + 16]
	cmp   %o4, 23; be,a %icc, 6f; st  %f23, [%o1 + 16]
	cmp   %o4, 24; be,a %icc, 6f; st  %f24, [%o1 + 16]
	cmp   %o4, 25; be,a %icc, 6f; st  %f25, [%o1 + 16]
	cmp   %o4, 26; be,a %icc, 6f; st  %f26, [%o1 + 16]
	cmp   %o4, 27; be,a %icc, 6f; st  %f27, [%o1 + 16]
	cmp   %o4, 28; be,a %icc, 6f; st  %f28, [%o1 + 16]
	cmp   %o4, 29; be,a %icc, 6f; st  %f29, [%o1 + 16]
	cmp   %o4, 30; be,a %icc, 6f; st  %f30, [%o1 + 16]
	cmp   %o4, 31; be,a %icc, 6f; st  %f31, [%o1 + 16]

	cmp   %o4, 32; be,a %icc, 6f; std %f0,  [%o1 + 16]
	cmp   %o4, 33; be,a %icc, 6f; std %f2,  [%o1 + 16]
	cmp   %o4, 34; be,a %icc, 6f; std %f4,  [%o1 + 16]
	cmp   %o4, 35; be,a %icc, 6f; std %f6,  [%o1 + 16]
	cmp   %o4, 36; be,a %icc, 6f; std %f8,  [%o1 + 16]
	cmp   %o4, 37; be,a %icc, 6f; std %f10, [%o1 + 16]
	cmp   %o4, 38; be,a %icc, 6f; std %f12, [%o1 + 16]
	cmp   %o4, 39; be,a %icc, 6f; std %f14, [%o1 + 16]
	cmp   %o4, 40; be,a %icc, 6f; std %f16, [%o1 + 16]
	cmp   %o4, 41; be,a %icc, 6f; std %f18, [%o1 + 16]
	cmp   %o4, 42; be,a %icc, 6f; std %f20, [%o1 + 16]
	cmp   %o4, 43; be,a %icc, 6f; std %f22, [%o1 + 16]
	cmp   %o4, 44; be,a %icc, 6f; std %f24, [%o1 + 16]
	cmp   %o4, 45; be,a %icc, 6f; std %f26, [%o1 + 16]
	cmp   %o4, 46; be,a %icc, 6f; std %f28, [%o1 + 16]
	cmp   %o4, 47; be,a %icc, 6f; std %f30, [%o1 + 16]

	cmp   %o4, 48; be,a %icc, 6f; std %f32, [%o1 + 16]
	cmp   %o4, 49; be,a %icc, 6f; std %f34, [%o1 + 16]
	cmp   %o4, 50; be,a %icc, 6f; std %f36, [%o1 + 16]
	cmp   %o4, 51; be,a %icc, 6f; std %f38, [%o1 + 16]
	cmp   %o4, 52; be,a %icc, 6f; std %f40, [%o1 + 16]
	cmp   %o4, 53; be,a %icc, 6f; std %f42, [%o1 + 16]
	cmp   %o4, 54; be,a %icc, 6f; std %f44, [%o1 + 16]
	cmp   %o4, 55; be,a %icc, 6f; std %f46, [%o1 + 16]
	cmp   %o4, 56; be,a %icc, 6f; std %f48, [%o1 + 16]
	cmp   %o4, 57; be,a %icc, 6f; std %f50, [%o1 + 16]
	cmp   %o4, 58; be,a %icc, 6f; std %f52, [%o1 + 16]
	cmp   %o4, 59; be,a %icc, 6f; std %f54, [%o1 + 16]
	cmp   %o4, 60; be,a %icc, 6f; std %f56, [%o1 + 16]
	cmp   %o4, 61; be,a %icc, 6f; std %f58, [%o1 + 16]
	cmp   %o4, 62; be,a %icc, 6f; std %f60, [%o1 + 16]
	cmp   %o4, 63; be,a %icc, 6f; std %f62, [%o1 + 16]
4:
	/*
	 * Invoke the error via an OP (operation).
	 */
	cmp   %o4, 0;  be,a %icc, 6f; fmuls	%f0, %f20, %f0
	cmp   %o4, 1;  be,a %icc, 6f; fadds	%f1, %f20, %f1
	cmp   %o4, 2;  be,a %icc, 6f; fmuls 	%f2, %f20, %f2
	cmp   %o4, 3;  be,a %icc, 6f; fadds	%f3, %f20, %f3
	cmp   %o4, 4;  be,a %icc, 6f; fmuls	%f4, %f20, %f4
	cmp   %o4, 5;  be,a %icc, 6f; fadds	%f5, %f20, %f5
	cmp   %o4, 6;  be,a %icc, 6f; fmuls 	%f6, %f20, %f6
	cmp   %o4, 7;  be,a %icc, 6f; fadds	%f7, %f20, %f7
	cmp   %o4, 8;  be,a %icc, 6f; fmuls	%f8, %f20, %f8
	cmp   %o4, 9;  be,a %icc, 6f; fadds	%f9, %f20, %f9
	cmp   %o4, 10; be,a %icc, 6f; fmuls 	%f10, %f20, %f10
	cmp   %o4, 11; be,a %icc, 6f; fadds	%f11, %f20, %f11
	cmp   %o4, 12; be,a %icc, 6f; fmuls	%f12, %f20, %f12
	cmp   %o4, 13; be,a %icc, 6f; fadds	%f13, %f20, %f13
	cmp   %o4, 14; be,a %icc, 6f; fmuls 	%f14, %f20, %f14
	cmp   %o4, 15; be,a %icc, 6f; fadds	%f15, %f20, %f15

	cmp   %o4, 16; be,a %icc, 6f; fmuls	%f16, %f20, %f16
	cmp   %o4, 17; be,a %icc, 6f; fadds	%f17, %f20, %f17
	cmp   %o4, 18; be,a %icc, 6f; fmuls	%f18, %f20, %f18
	cmp   %o4, 19; be,a %icc, 6f; fadds	%f19, %f20, %f19
	cmp   %o4, 20; be,a %icc, 6f; fmuls	%f20, %f20, %f20
	cmp   %o4, 21; be,a %icc, 6f; fadds	%f21, %f20, %f21
	cmp   %o4, 22; be,a %icc, 6f; fmuls	%f22, %f20, %f22
	cmp   %o4, 23; be,a %icc, 6f; fadds	%f23, %f20, %f23
	cmp   %o4, 24; be,a %icc, 6f; fmuls	%f24, %f20, %f24
	cmp   %o4, 25; be,a %icc, 6f; fadds	%f25, %f20, %f25
	cmp   %o4, 26; be,a %icc, 6f; fmuls	%f26, %f20, %f26
	cmp   %o4, 27; be,a %icc, 6f; fadds	%f27, %f20, %f27
	cmp   %o4, 28; be,a %icc, 6f; fmuls	%f28, %f20, %f28
	cmp   %o4, 29; be,a %icc, 6f; fadds	%f29, %f20, %f29
	cmp   %o4, 30; be,a %icc, 6f; fmuls	%f30, %f20, %f30
	cmp   %o4, 31; be,a %icc, 6f; fadds	%f31, %f20, %f31

	cmp   %o4, 32; be,a %icc, 6f; fmuld	%f0, %f20, %f0
	cmp   %o4, 33; be,a %icc, 6f; faddd	%f2, %f20, %f2
	cmp   %o4, 34; be,a %icc, 6f; fmuld	%f4, %f20, %f4
	cmp   %o4, 35; be,a %icc, 6f; faddd	%f6, %f20, %f6
	cmp   %o4, 36; be,a %icc, 6f; fmuld	%f8, %f20, %f8
	cmp   %o4, 37; be,a %icc, 6f; fadds	%f10, %f20, %f10
	cmp   %o4, 38; be,a %icc, 6f; fmuld	%f12, %f20, %f12
	cmp   %o4, 39; be,a %icc, 6f; faddd	%f14, %f20, %f14
	cmp   %o4, 40; be,a %icc, 6f; fmuld	%f16, %f20, %f16
	cmp   %o4, 41; be,a %icc, 6f; faddd	%f18, %f20, %f18
	cmp   %o4, 42; be,a %icc, 6f; fmuld	%f20, %f20, %f20
	cmp   %o4, 43; be,a %icc, 6f; faddd	%f22, %f20, %f22
	cmp   %o4, 44; be,a %icc, 6f; fmuld	%f24, %f20, %f24
	cmp   %o4, 45; be,a %icc, 6f; faddd	%f26, %f20, %f26
	cmp   %o4, 46; be,a %icc, 6f; fmuld	%f28, %f20, %f28
	cmp   %o4, 47; be,a %icc, 6f; faddd	%f30, %f20, %f30

	cmp   %o4, 48; be,a %icc, 6f; fmuld	%f32, %f20, %f32
	cmp   %o4, 49; be,a %icc, 6f; faddd	%f34, %f20, %f34
	cmp   %o4, 50; be,a %icc, 6f; fmuld	%f36, %f20, %f36
	cmp   %o4, 51; be,a %icc, 6f; faddd	%f38, %f20, %f48
	cmp   %o4, 52; be,a %icc, 6f; fmuld	%f40, %f20, %f40
	cmp   %o4, 53; be,a %icc, 6f; faddd	%f42, %f20, %f42
	cmp   %o4, 54; be,a %icc, 6f; fmuld	%f44, %f20, %f44
	cmp   %o4, 55; be,a %icc, 6f; faddd	%f46, %f20, %f46
	cmp   %o4, 56; be,a %icc, 6f; fmuld	%f48, %f20, %f48
	cmp   %o4, 57; be,a %icc, 6f; faddd	%f50, %f20, %f50
	cmp   %o4, 58; be,a %icc, 6f; fmuld	%f52, %f20, %f52
	cmp   %o4, 59; be,a %icc, 6f; faddd	%f54, %f20, %f54
	cmp   %o4, 60; be,a %icc, 6f; fmuld	%f56, %f20, %f56
	cmp   %o4, 61; be,a %icc, 6f; faddd	%f58, %f20, %f58
	cmp   %o4, 62; be,a %icc, 6f; fmuld	%f60, %f20, %f60
	cmp   %o4, 63; be,a %icc, 6f; faddd	%f62, %f20, %f62
5:
	/*
	 * Invoke the error via a LOAD (load will not trigger error).
	 */
	cmp   %o4, 0;  be,a %icc, 6f; ld  [%o1 + 0], %f0
	cmp   %o4, 1;  be,a %icc, 6f; ld  [%o1 + 0], %f1
	cmp   %o4, 2;  be,a %icc, 6f; ld  [%o1 + 0], %f2
	cmp   %o4, 3;  be,a %icc, 6f; ld  [%o1 + 0], %f3
	cmp   %o4, 4;  be,a %icc, 6f; ld  [%o1 + 0], %f4
	cmp   %o4, 5;  be,a %icc, 6f; ld  [%o1 + 0], %f5
	cmp   %o4, 6;  be,a %icc, 6f; ld  [%o1 + 0], %f6
	cmp   %o4, 7;  be,a %icc, 6f; ld  [%o1 + 0], %f7
	cmp   %o4, 8;  be,a %icc, 6f; ld  [%o1 + 0], %f8
	cmp   %o4, 9;  be,a %icc, 6f; ld  [%o1 + 0], %f9
	cmp   %o4, 10; be,a %icc, 6f; ld  [%o1 + 0], %f10
	cmp   %o4, 11; be,a %icc, 6f; ld  [%o1 + 0], %f11
	cmp   %o4, 12; be,a %icc, 6f; ld  [%o1 + 0], %f12
	cmp   %o4, 13; be,a %icc, 6f; ld  [%o1 + 0], %f13
	cmp   %o4, 14; be,a %icc, 6f; ld  [%o1 + 0], %f14
	cmp   %o4, 15; be,a %icc, 6f; ld  [%o1 + 0], %f15

	cmp   %o4, 16; be,a %icc, 6f; ld  [%o1 + 0], %f16
	cmp   %o4, 17; be,a %icc, 6f; ld  [%o1 + 0], %f17
	cmp   %o4, 18; be,a %icc, 6f; ld  [%o1 + 0], %f18
	cmp   %o4, 19; be,a %icc, 6f; ld  [%o1 + 0], %f19
	cmp   %o4, 20; be,a %icc, 6f; ld  [%o1 + 0], %f20
	cmp   %o4, 21; be,a %icc, 6f; ld  [%o1 + 0], %f21
	cmp   %o4, 22; be,a %icc, 6f; ld  [%o1 + 0], %f22
	cmp   %o4, 23; be,a %icc, 6f; ld  [%o1 + 0], %f23
	cmp   %o4, 24; be,a %icc, 6f; ld  [%o1 + 0], %f24
	cmp   %o4, 25; be,a %icc, 6f; ld  [%o1 + 0], %f25
	cmp   %o4, 26; be,a %icc, 6f; ld  [%o1 + 0], %f26
	cmp   %o4, 27; be,a %icc, 6f; ld  [%o1 + 0], %f27
	cmp   %o4, 28; be,a %icc, 6f; ld  [%o1 + 0], %f28
	cmp   %o4, 29; be,a %icc, 6f; ld  [%o1 + 0], %f29
	cmp   %o4, 30; be,a %icc, 6f; ld  [%o1 + 0], %f30
	cmp   %o4, 31; be,a %icc, 6f; ld  [%o1 + 0], %f31

	cmp   %o4, 32; be,a %icc, 6f; ldd [%o1 + 0], %f0
	cmp   %o4, 33; be,a %icc, 6f; ldd [%o1 + 0], %f2
	cmp   %o4, 34; be,a %icc, 6f; ldd [%o1 + 0], %f4
	cmp   %o4, 35; be,a %icc, 6f; ldd [%o1 + 0], %f6
	cmp   %o4, 36; be,a %icc, 6f; ldd [%o1 + 0], %f8
	cmp   %o4, 37; be,a %icc, 6f; ldd [%o1 + 0], %f10
	cmp   %o4, 38; be,a %icc, 6f; ldd [%o1 + 0], %f12
	cmp   %o4, 39; be,a %icc, 6f; ldd [%o1 + 0], %f14
	cmp   %o4, 40; be,a %icc, 6f; ldd [%o1 + 0], %f16
	cmp   %o4, 41; be,a %icc, 6f; ldd [%o1 + 0], %f18
	cmp   %o4, 42; be,a %icc, 6f; ldd [%o1 + 0], %f20
	cmp   %o4, 43; be,a %icc, 6f; ldd [%o1 + 0], %f22
	cmp   %o4, 44; be,a %icc, 6f; ldd [%o1 + 0], %f24
	cmp   %o4, 45; be,a %icc, 6f; ldd [%o1 + 0], %f26
	cmp   %o4, 46; be,a %icc, 6f; ldd [%o1 + 0], %f28
	cmp   %o4, 47; be,a %icc, 6f; ldd [%o1 + 0], %f30

	cmp   %o4, 48; be,a %icc, 6f; ldd [%o1 + 0], %f32
	cmp   %o4, 49; be,a %icc, 6f; ldd [%o1 + 0], %f34
	cmp   %o4, 50; be,a %icc, 6f; ldd [%o1 + 0], %f36
	cmp   %o4, 51; be,a %icc, 6f; ldd [%o1 + 0], %f38
	cmp   %o4, 52; be,a %icc, 6f; ldd [%o1 + 0], %f40
	cmp   %o4, 53; be,a %icc, 6f; ldd [%o1 + 0], %f42
	cmp   %o4, 54; be,a %icc, 6f; ldd [%o1 + 0], %f44
	cmp   %o4, 55; be,a %icc, 6f; ldd [%o1 + 0], %f46
	cmp   %o4, 56; be,a %icc, 6f; ldd [%o1 + 0], %f48
	cmp   %o4, 57; be,a %icc, 6f; ldd [%o1 + 0], %f50
	cmp   %o4, 58; be,a %icc, 6f; ldd [%o1 + 0], %f52
	cmp   %o4, 59; be,a %icc, 6f; ldd [%o1 + 0], %f54
	cmp   %o4, 60; be,a %icc, 6f; ldd [%o1 + 0], %f56
	cmp   %o4, 61; be,a %icc, 6f; ldd [%o1 + 0], %f58
	cmp   %o4, 62; be,a %icc, 6f; ldd [%o1 + 0], %f60
	cmp   %o4, 63; be,a %icc, 6f; ldd [%o1 + 0], %f62
6:
	membar	#Sync				! required
	wr	%g4, %g0, %fprs			! restore the fprs
	wrpr	%g5, %pstate			! restore the processor state
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(kt_inj_freg_file)
#endif	/* lint */

/*
 * This routine is used to corrupt the data ecc or the tag (CAM) parity
 * of the internal Store Buffer array on the K/T processor via the
 * per core ASI_INJECT_ERROR_REG.
 *
 * This routine is very similar to the n2_inj_sb_io() routine, the only
 * reason that there needs to be a specific KT version is due to the
 * park/unpark macros which are different on KT because of the larger
 * number of cpus on each chip.
 *
 * NOTE: is seems that ASI_REAL_IO does not need an RA->PA translation
 *	 iotte although the ASI_REAL_MEM accesses do.  If an iotte is
 *	 required then an iotte installer function will have to be written
 *	 (similar to the existing memory tte installer).
 */

/*
 * Prototype:
 *
 * int kt_inj_sb_io(uint64_t paddr, uint64_t enable, uint64_t eccmask,
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
kt_inj_sb_io(uint64_t raddr, uint64_t enable, uint64_t eccmask,
		uint64_t count)
{
	return 0;
}
#else
	.align	1024
	ENTRY_NP(kt_inj_sb_io)

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
	KT_PARK_SIBLING_STRANDS(%g1, %g2, %g3, %g4, %g6)

	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	stxa	%g5, [%o1]ASI_REAL_IO		! perform write to inject
	add	%o1, 0x10, %o1
	stxa	%g5, [%o1]ASI_REAL_IO		! perform write to inject
	add	%o1, 0x10, %o1
	stxa	%g5, [%o1]ASI_REAL_IO		! perform write to inject
	add	%o1, 0x10, %o1
	stxa	%g5, [%o1]ASI_REAL_IO		! perform write to inject

	KT_UNPARK_SIBLING_STRANDS(%g1, %g2, %g3, %g4)

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
	KT_PARK_SIBLING_STRANDS(%g1, %g2, %g3, %g4, %g6)

	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	stxa	%g5, [%o1]ASI_REAL_IO		! perform write to inject
	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg

	KT_UNPARK_SIBLING_STRANDS(%g1, %g2, %g3, %g4)

	ba	3f
	  mov	%g0, %o0			! set return value to PASS
2:
	/*
	 * Inject one error and access it with a load or membar (see below).
	 *
	 * NOTE: to alternate access methods can comment out or
	 *	 uncomment the lda or the membar below it.
	 */
	KT_PARK_SIBLING_STRANDS(%g1, %g2, %g3, %g4, %g6)

	membar	#Sync				! required
	stxa	%o2, [%g0]ASI_INJECT_ERROR_REG	! enable injection mode
	stxa	%g5, [%o1]ASI_REAL_IO		! perform write to inject

	KT_UNPARK_SIBLING_STRANDS(%g1, %g2, %g3, %g4)

!	lda	[%o1]ASI_REAL_IO, %o4		! read raddr to invoke

	membar	#Sync				! membar to drain and invoke

	stxa	%g0, [%g0]ASI_INJECT_ERROR_REG	! clear injection reg after acc
	ba	3f
	  mov	%g0, %o0			! set return value to PASS
3:
	done					! .
	SET_SIZE(kt_inj_sb_io)
#endif	/* lint */

/*---------- end of test functions - start of support functions ----------*/

/*
 * This is a utility routine that puts the L1 I-cache into it's (normal)
 * multi-way LRU replacement mode.
 */
#if defined(lint)
/*ARGSUSED*/
void
kt_icache_disable_DM(void)
{}
#else
	.align	32
	ENTRY(kt_icache_disable_DM)
	mov	0x10, %g2			! VA for LSU asi access
	ldxa	[%g2]ASI_LSU_DIAG_REG, %g3	! read current reg value
	andn	%g3, 0x1, %g3			! put i$ into LFSR
	stxa	%g3, [%g2]ASI_LSU_DIAG_REG	!   replacement mode
	membar	#Sync				! .

	mov	%g0, %o0			! return PASS
	done
	SET_SIZE(kt_icache_disable_DM)
#endif	/* lint */

/*
 * This is a utility routine that puts the L1 I-cache into direct mapped
 * (DM) way replacement mode.
 */
#if defined(lint)
/*ARGSUSED*/
void
kt_icache_enable_DM(void)
{}
#else
	.align	32
	ENTRY(kt_icache_enable_DM)
	mov	0x10, %g2			! VA for LSU asi access
	ldxa	[%g2]ASI_LSU_DIAG_REG, %g3	! read current reg value
	or	%g3, 0x1, %g3			! put i$ into DM
	stxa	%g3, [%g2]ASI_LSU_DIAG_REG	!   replacement mode
	membar	#Sync				! .

	mov	%g0, %o0			! return PASS
	done
	SET_SIZE(kt_icache_enable_DM)
#endif	/* lint */

/*
 * This is a utility routine that puts all sixteen banks of the L2 cache into
 * their (normal) 24-way replacement mode.
 *
 * NOTE: this routine does not save or restore the current cache state.
 *
 * Prototype:
 *
 * void kt_l2_disable_DM(void);
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
kt_l2_disable_DM(void)
{}
#else
     	.align	64
	ENTRY(kt_l2_disable_DM)

	mov	KT_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0x829.0000.0000
	sllx	%g1, 32, %g1			! .
	mov	KT_L2CR_DMMODE, %g5		! %g5 = direct map mode bit
	mov	KT_NUM_L2_BANKS, %g3		! must access all eight regs

1: ! kt_l2_DM_off:
	ldx	[%g1], %g4			! disable DM cache mode for
	andn	%g4, %g5, %g4			!   all banks which also
	stx	%g4, [%g1]			!   flushes the buffers.
	sub	%g3, 1, %g3
	brnz,a	%g3, 1b
	  add	%g1, KT_L2_BANK_OFFSET, %g1

	membar	#Sync				! (PRM says not needed)
	mov	%g0, %o0			! return PASS
	done
	SET_SIZE(kt_l2_disable_DM)
#endif	/* lint */

/*
 * This is a utility routine that puts all sixteen banks of the L2 cache into
 * direct-map (DM) replacement mode, for flushing and other special purposes
 * that require we know where in the cache a value is installed.
 *
 * NOTE: this routine does not save or restore the current cache state.
 *
 * Prototype:
 *
 * void kt_l2_enable_DM(void);
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
kt_l2_enable_DM(void)
{}
#else
     	.align	64
	ENTRY(kt_l2_enable_DM)

	! put L2 into direct mapped. Must to this to all 8 banks separately
	mov	KT_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0x829.0000.0000
	sllx	%g1, 32, %g1			! .
	mov	KT_L2CR_DMMODE, %g5		! %g5 = direct map mode bit
	mov	KT_NUM_L2_BANKS, %g3		! must access all eight regs

1: ! kt_l2_DM_on:
	ldx	[%g1], %g4			! must do all banks
	or	%g4, %g5, %g4
	stx	%g4, [%g1]
	sub	%g3, 1, %g3
	brnz,a	%g3, 1b
	  add	%g1, KT_L2_BANK_OFFSET, %g1

	membar	#Sync				! (PRM says not needed)
	mov	%g0, %o0			! return PASS
	done
        SET_SIZE(kt_l2_enable_DM)
#endif	/* lint */

/*
 * This is a utility routine that returns the base address (PA) of the
 * contiguous 6 MB address range used by the system for displacement
 * flushing the L2 cache.
 *
 * Prototype:
 *
 * uint64_t kt_l2_get_flushbase(void);
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
kt_l2_get_flushbase(void)
{
	return 0;
}
#else
     	.align	64
	ENTRY(kt_l2_get_flushbase)

	L2_FLUSH_BASEADDR(%o1, %g1)		! args are BASE, scratch
	mov	%g0, %o0			! return PASS
	done
        SET_SIZE(kt_l2_get_flushbase)
#endif	/* lint */

/*
 * This routine does a displacement flush of the full 6MB K/T L2-cache
 * in hypervisor mode.
 *
 * NOTE: a displacement flush is not effective on K/T if IDX mode
 *	 (L2-cache index hashing) is enabled and so the flush routines which
 *	 use the PREFETCH_ICE flush method should be used when IDX is enabled.
 *
 * Prototype:
 *
 * void kt_l2_flushall(void);
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
kt_l2_flushall(caddr_t flushbase)
{}
#else
	.align	256
	ENTRY_NP(kt_l2_flushall)

	! put L2 into direct mapped. Must to this to all 16 banks separately
	mov	KT_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0x829.0000.0000
	sllx	%g1, 32, %g1			! .
	mov	KT_L2CR_DMMODE, %g5		! %g5 = direct map mode bit
	mov	KT_NUM_L2_BANKS, %g3		! 16 separate regs
	ldx	[%g1], %g4			! read the base reg
	andcc	%g4, %g5, %g0			! check l2$ mode (w/o writing)
	be	%xcc, 1f
	  nop
	mov	%g0, %g5			! already in DMMODE, don't
						!   change, but write anyway
1: ! kt_flush_l2_DM_on:
	ldx	[%g1], %g4			! must do all banks
	or	%g4, %g5, %g4
	stx	%g4, [%g1]
	sub	%g3, 1, %g3
	brnz,a	%g3, 1b
	  add	%g1, KT_L2_BANK_OFFSET, %g1

2: ! kt_flush_PA_setup:

	L2_FLUSH_BASEADDR(%g6, %g1)		! put flush BA into a reg (%g6)

	srlx	%g6, 20, %g6			! align flush addr to 1 MB
	sllx	%g6, 20, %g6			! .
	mov	KT_NUM_L2_WAYS, %g2		! %g2 = way counter (24 ways)

3: ! kt_flush_ways:
	add	%g0, 256, %g3			! lines per way, per bank

4: ! kt_flush_rows:
	ldx	[%g6+0x00], %g0			! must flush all 16 banks
	ldx	[%g6+0x40], %g0
	ldx	[%g6+0x80], %g0
	ldx	[%g6+0xc0], %g0

	ldx	[%g6+0x100], %g0
	ldx	[%g6+0x140], %g0
	ldx	[%g6+0x180], %g0
	ldx	[%g6+0x1c0], %g0

	ldx	[%g6+0x200], %g0
	ldx	[%g6+0x240], %g0
	ldx	[%g6+0x280], %g0
	ldx	[%g6+0x2c0], %g0

	ldx	[%g6+0x300], %g0
	ldx	[%g6+0x340], %g0
	ldx	[%g6+0x380], %g0
	ldx	[%g6+0x3c0], %g0

	sub	%g3, 1, %g3			! dec to next line
	brnz	%g3, 4b
	  add	%g6, 0x400, %g6
	sub	%g2, 1, %g2			! dec to next way
	brnz,a	%g2, 3b
	  nop

	/*
	 * Once through the 6Mb area is necessary but NOT sufficient. If the
	 * last lines in the cache were dirty, then the writeback buffers may
	 * still be active. Need to always write the L2CSR at the end, whether
	 * it was changed or not to flush the cache buffers.
	 */
	mov	KT_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0x829.0000.0000
	sllx	%g1, 32, %g1			! .
	mov	KT_NUM_L2_BANKS, %g3		! must access all 16 regs

5: ! kt_flush_l2_DM_off:
	ldx	[%g1], %g4			! restore the cache mode for
	xor	%g4, %g5, %g4			!   all 16 banks which also
	stx	%g4, [%g1]			!   flushes the buffers.
	sub	%g3, 1, %g3
	brnz,a	%g3, 5b
	  add	%g1, KT_L2_BANK_OFFSET, %g1

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(kt_l2_flushall)
#endif	/* lint */

/*
 * This routine does a displacement flush of an entry specified by it's
 * physical address, from the K/T L2-cache (in hypervisor mode).
 *
 * NOTE: this routine is used when IDX index hashing is disabled.
 *
 * Prototype:
 *
 * void kt_l2_flushentry(caddr_t paddr2flush);
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
kt_l2_flushentry(caddr_t paddr2flush)
{}
#else
	.align	128
	ENTRY_NP(kt_l2_flushentry)

	/*
	 * Determine the initial L2 flush addr for the specified paddr.
	 */
	set	0x7ffc0, %g3			! PA mask for bank and set
	and	%g3, %o1, %g3			! PA[17:6] (note way=PA[22:18])

	L2_FLUSH_BASEADDR(%g4, %g1)		! put flush BA into a reg (%g4)
	or	%g3, %g4, %g3			! %g3 = the flush addr to use

	mov	KT_NUM_L2_WAYS, %o2		! %o2 = number of L2 ways
	mov	1, %o3				! %o3 = L2 way inc. value
	sllx	%o3, KT_L2_WAY_SHIFT, %o3	!   (note shift = 18)

	/*
	 * Put L2 into direct mapped mode, this bank only.
	 */
	mov	KT_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0x829.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, KT_L2_BANK_MASK, %g2	! get reg offset from paddr
	or	%g1, %g2, %g1			! include offset in reg addr

	ldx	[%g1], %g2			! %g2=prev L2_CTL_REG contents
	or	%g2, KT_L2CR_DMMODE, %g5	! .
	stx	%g5, [%g1]			! .
	membar	#Sync				! (PRM says not needed)

	/*
	 * Flush all 24 ways (all possible locations of the data).
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
	SET_SIZE(kt_l2_flushentry)
#endif	/* lint */

/*
 * This routine does a displacement flush of an entry specified by it's
 * physical address, from the K/T L2-cache (in hypervisor mode).
 *
 * A normal (LRU replacement) L2-cache line install goes as follows:
 *
 *	PA[32:6] -> [hash-logic] -> [LRU picks way bits] -> L2-index[21:6]
 *
 * In order to choose addresses within the displacement buffer that
 * correspond to the 24 possible locations of the line to be displaced
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
 *	 in both cases are similar (see the kt_l2_flushidx_ice() routine).
 *
 * Prototype:
 *
 * void kt_l2_flushidx(caddr_t paddr2flush);
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
kt_l2_flushidx(caddr_t paddr2flush)
{}
#else
	.align	256
	ENTRY_NP(kt_l2_flushidx)

	/*
	 * Mask IDX'd addr so only the index bits remain, then add flush BA.
	 */
	set	0x7ffc0, %g5			! PA mask for bank and set
	and	%o1, %g5, %g3			! PA[17:6] req'd way = PA[22:18]
	
	L2_FLUSH_BASEADDR(%g6, %g1)		! put flush BA into a reg (%g6)
	or	%g3, %g6, %g3			! %g3 = first flush addr to use

	/*
	 * Now the initial (way bits = 0) flush address is IDX'd here
	 * in order to cancel out the IDX which occurs when it goes into
	 * the L2 as a normal load access.
	 *
	 * In practice the flush BA is zero so this step is not strictly
	 * necessary, but is performed here for future-proofing.
	 */
	mov	0x1f, %g7			! %g7 = mask of all way bits
	sllx	%g7, KT_L2_WAY_SHIFT, %g7	! .
	andn	%g3, %g7, %g3			! clear way bits from flush addr

	KT_PERFORM_IDX_HASH(%g3, %g2, %g1)	! %g3 = IDX'd flush addr

	mov	0x3, %g4			! %g4 = way mask for IDX [19:18]
	sllx	%g4, KT_L2_WAY_SHIFT, %g4	!   (used in loop below)

	mov	(KT_NUM_L2_WAYS - 1), %o2	! %o2 = number of L2 ways
	mov	1, %o3				! %o3 = L2 way inc. value
	sllx	%o3, KT_L2_WAY_SHIFT, %o3	!   (way shift = 18)

	/*
	 * Put L2 into direct mapped mode, this bank only.
	 */
	mov	KT_L2_CTL_REG_MSB, %g1		! %g1=L2_CTL_REG=0x829.0000.0000
	sllx	%g1, 32, %g1			! .
	and	%o1, KT_L2_BANK_MASK, %g2	! get reg offset from paddr
	or	%g1, %g2, %g1			! include offset in reg addr

	ldx	[%g1], %g2			! %g2=prev L2_CTL_REG contents
	or	%g2, KT_L2CR_DMMODE, %g5	! .
	stx	%g5, [%g1]			! .
	membar	#Sync				! (PRM says not needed)

	/*
	 * Flush all 24 ways (all possible locations of the data).
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
	SET_SIZE(kt_l2_flushidx)
#endif	/* lint */

/*
 * This routine does a flush of the full 6MB K/T L2-cache in hypervisor
 * mode using the prefectch-ICE (Invalidate Cache Entry) instruction.
 *
 * NOTE: this routine is immune to the L2-cache IDX setting since all
 *	 indexes are run through to do the full flush.
 *
 * Prototype:
 *
 * void kt_l2_flushall_ice(void);
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
kt_l2_flushall_ice(void)
{}
#else
	.align	128
	ENTRY_NP(kt_l2_flushall_ice)

	mov	KT_L2_PREFETCHICE_MSB, %g1	! set the key field of addr
	sllx	%g1, 32, %g6			! .

	add	%g0, KT_NUM_L2_WAYS, %g2	! %g2 = way counter (24 ways)

1: ! kt_flush_ways:
	add	%g0, 256, %g3			! %g3 = lines per way, per bank

2: ! kt_flush_rows:
	prefetch [%g6+0x00], INVALIDATE_CACHE_LINE ! must flush all 16 banks
	prefetch [%g6+0x40], INVALIDATE_CACHE_LINE
	prefetch [%g6+0x80], INVALIDATE_CACHE_LINE
	prefetch [%g6+0xc0], INVALIDATE_CACHE_LINE

	prefetch [%g6+0x100], INVALIDATE_CACHE_LINE
	prefetch [%g6+0x140], INVALIDATE_CACHE_LINE
	prefetch [%g6+0x180], INVALIDATE_CACHE_LINE
	prefetch [%g6+0x1c0], INVALIDATE_CACHE_LINE

	prefetch [%g6+0x200], INVALIDATE_CACHE_LINE
	prefetch [%g6+0x240], INVALIDATE_CACHE_LINE
	prefetch [%g6+0x280], INVALIDATE_CACHE_LINE
	prefetch [%g6+0x2c0], INVALIDATE_CACHE_LINE

	prefetch [%g6+0x300], INVALIDATE_CACHE_LINE
	prefetch [%g6+0x340], INVALIDATE_CACHE_LINE
	prefetch [%g6+0x380], INVALIDATE_CACHE_LINE
	prefetch [%g6+0x3c0], INVALIDATE_CACHE_LINE
        membar #Sync
	
	/*
	 * XXX	really this routine should do load from each
	 *	bank to ensure completion (a diag load works too).
	 *	Only need to do this once for each bank at the end...
	 */

	sub	%g3, 1, %g3			! dec to next line set
	brnz	%g3, 2b
	  add	%g6, 0x400, %g6
	sub	%g2, 1, %g2			! dec to next way
	brnz,a	%g2, 1b
	  nop

	mov	%g0, %o0			! all done, return PASS
	done					! .
	SET_SIZE(kt_l2_flushall_ice)
#endif	/* lint */

/*
 * This routine does a displacement flush of the full 6MB K/T L2-cache but
 * unlike most of the routines in this file it is run in kernel mode (not
 * hyperprivileged mode).
 *
 * NOTE: this routine is the same as the N2 version, could make generic
 *	 and move to the common code (memtest_v_asm.s).
 *
 * Prototype:
 *
 * void kt_l2_flushall_kmode_asm(caddr_t flushbase, uint64_t cachesize,
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
kt_l2_flushall_kmode_asm(caddr_t flushbase, uint64_t cachesize,
				uint64_t linesize)
{}
#else
	.align	64
	ENTRY_NP(kt_l2_flushall_kmode_asm)
1:
	subcc	%o1, %o2, %o1			! go through flush region
	bg,pt	%xcc, 1b			!   accessing each address	
	  ldxa	[%o0 + %o1]ASI_REAL_MEM, %g0

	retl					! return PASS
	  mov %g0, %o0				! .
	SET_SIZE(kt_l2_flushall_kmode_asm)
#endif	/* lint */

/*
 * This routine does a flush of L2$ an entry specified by it's physical
 * address in hypervisor mode using the prefectch-ICE
 * (Invalidate Cache Entry) instruction.
 *
 * NOTE: this ICE flush routine does NOT take into account the IDX hashing
 *	 and so should only be called/used if index hashing is disabled.
 *	 The kt_l2_flushidx_ice() routine is to be used when IDX is enabled.
 *
 * Prototype:
 *
 * void kt_l2_flushentry_ice(caddr_t paddr2flush);
 *
 * Register usage:
 *
 *	%o1 - paddr to flush from the cache
 *	%o2 - counter for way access (not an inpar)
 *	%o3 - way increment value (not an inpar)
 *	%o4 - L2 bank enable bits (not an inpar)
 *	%g1 - temp
 *	%g2 - value read from the DRAM_CTL_REG
 *	%g3 - address values for the prefetch invalidate
 *	%g4 - temp
 *	%g5 - bit counter for enabled banks
 *	%g6 - unused
 *	%g7 - unused
 */
#if defined(lint)
/* ARGSUSED */
void
kt_l2_flushentry_ice(caddr_t paddr2flush)
{}
#else
	.align	64
	ENTRY_NP(kt_l2_flushentry_ice)

	/*
	 * Determine the initial L2 flush index for the specified paddr.
	 */
	set	0x3ffc0, %g3			! PA mask for bank and set
	and	%g3, %o1, %g3			! PA[17:6] req'd way = PA[22:18]

	mov	KT_L2_PREFETCHICE_MSB, %g1	! set the key field of addr
	sllx	%g1, 32, %g1			! .
	or	%g3, %g1, %g3			! %g3 = full ICE flush addr

	/*
	 * If the chip is in a reduced bank mode then the address used
	 * with prefectch-ICE needs to be modified as per PRM instructions.
	 *
	 * First the number of enabled banks is determined.
	 *
	 * NOTE: it seems the PRM was wrong about the need to adjust the
	 *	 prefetch-ICE index for reduced bank modes.  However this
	 *	 code will be left here as a reference (it was removed from
	 *	 all other routines that used prefetch-ICE) in case this
	 *	 method is needed on future processors.
	 */
#if 0
	mov	KT_L2_BANK_CSR_BASE_MSB, %g4	! build address for enable reg
	sllx	%g4, 32, %g4			! .
	mov	0x102, %g5			! KT_L2_BANK_EN = 0x1020
	sllx	%g5, 4, %g5			! .
	or	%g4, %g5, %g4			! %g4 = bank enable reg
	ldx	[%g4], %o4			! %o4 = bank enable bitfield

	mov	%g0, %g5			! clear bit counter
1:
	and	%o4, 1, %g4			! count LSB
	add	%g5, %g4, %g5			! add bit to total
	brnz,a	%o4, 1b				! counted all 16 bits?
	  srlx	%o4, 1, %o4			!   shift bank enable down

	/*
	 * Only three L2 modes are possible, all 16 banks, 8 banks (half mode)
	 * and 4 banks (quarter mode).
	 */
	cmp	%g5, 8				! check how many banks enabled
	bg	%icc, 3f			! if all enabled, addr ok as-is
	  nop					!   .
	bl	%icc, 2f			! if qtr enabled, shift by 2
	  mov	0xff, %g4			!   initial mask for PA set

	/*
	 * Otherwise (else) handle the 8 bank (half) mode case.
	 * Set prefetch-ICE address bits 17:10 to bits 16:9 from paddr.
	 */
	sllx	%g4, 9, %g4			! mask for PA[16:9]
	and	%g3, %g4, %g5			! %g5 = captured PA[16:9]
	sllx	%g5, 1, %g5			! shift bits up to [17:10]

	sllx	%g4, 1, %g4			! clear [17:10] from ICE addr
	andn	%g3, %g4, %g3			! .
	or	%g3, %g5, %g3			! %g3 = complete half ICE addr

	ba 3f					! continue
	  mov	%g0, %o0			! set return value to PASS
2:
	/*
	 * Handle the 4 bank (quarter) mode case, then fall through.
	 * Set prefetch-ICE address bits 17:10 to bits 15:8 from paddr.
	 */
	sllx	%g4, 8, %g4			! mask for PA[15:8]
	and	%g3, %g4, %g5			! %g5 = captured PA[15:8]
	sllx	%g5, 2, %g5			! shift bits up to [17:10]

	sllx	%g4, 2, %g4			! clear [17:10] from ICE addr
	andn	%g3, %g4, %g3			! .
	or	%g3, %g5, %g3			! %g3 = complete qtr ICE addr
#endif
3:
	mov	KT_NUM_L2_WAYS, %o2		! %o2 = number of L2 ways
	mov	1, %o3				! %o3 = L2 way inc. value
	sllx	%o3, KT_L2_WAY_SHIFT, %o3	! .

	/*
	 * Flush all 24 ways (all possible locations of the data).
	 */
4:
	prefetch [%g3], INVALIDATE_CACHE_LINE	! use prefetch to clear line
        membar #Sync				! .

	sub	%o2, 1, %o2			! decrement count
	brnz,a	%o2, 4b				! are we done all ways?
	  add	%g3, %o3, %g3			! go to next way

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(kt_l2_flushentry_ice)
#endif	/* lint */

/*
 * This routine does a flush of L2-cache an entry specified by it's physical
 * address in hypervisor mode using the prefectch-ICE
 * (Invalidate Cache Entry) instruction.
 *
 * This routine is used in place of the above kt_l2_flushentry_ice() routine
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
 * So in order to access the correct 24 ways in the cache with the prefetch-ICE
 * instruction some work has to be done to the ICE-index used.  What needs
 * to be done is the original [19:18] bits need to be xor'd with [12:11]
 * since that is what happened to the data as it entered the cache, then
 * for each index the new [19:18] way bits need to be xor'd with [12:11] to
 * cancel their effect on the HW performed hash calculation.
 *
 * NOTE: the number of enabled L2 banks does NOT need to be checked in this
 *	 routine because IDX hashing can only be enabled when all banks are
 *	 available.  So no adjustment for reduced bank mode is required
 *	 for the prefetch-ICE instruction(s).
 *
 * Prototype:
 *
 * void kt_l2_flushidx_ice(caddr_t paddr2flush);
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
kt_l2_flushidx_ice(caddr_t paddr2flush)
{}
#else
	.align	128
	ENTRY_NP(kt_l2_flushidx_ice)

	/*
	 * Determine the initial L2 flush index for the specified paddr.
	 * Since the prefetch-ICE goes through the IDX logic the paddr inpar
	 * accepted by this routine must be the non IDX'd paddr.
	 */
	sllx	%o1, 31, %g3			! remove MSB bits but keep all
	srlx	%g3, 31, %g3			!   bits used by idx (PA[32:6])

	mov	0x3, %g4			! %g4 = way mask for IDX [19:18]
	sllx	%g4, KT_L2_WAY_SHIFT, %g4	! .

	and	%g3, %g4, %g5			! mask orig way bits for xor
	srlx	%g5, 7, %g5			! shift masked bits for xor
	xor	%g3, %g5, %g3			! do xor for orig [12:11]

	mov	0xf, %g7			! clear all way bits from PA
	sllx	%g7, KT_L2_WAY_SHIFT, %g7	! .
	andn	%g3, %g7, %g3			! .
	
	mov	KT_L2_PREFETCHICE_MSB, %g1	! set the key field of addr
	sllx	%g1, 32, %g1			! .
	or	%g3, %g1, %g3			! %g3 = first address to use

	mov	(KT_NUM_L2_WAYS - 1), %o2	! %o2 = number of L2 ways
	mov	1, %o3				! %o3 = L2 way inc. value
	sllx	%o3, KT_L2_WAY_SHIFT, %o3	!   (way shift = 18)

#ifdef	L2_DEBUG_BUFFER
	stx	%g3, [%o4 + 0xa0]		! first ICE index to use
	stx	%o2, [%o4 + 0xa8]		! num of L2 ways
	stx	%o3, [%o4 + 0xb0]		! the offset
#endif
	/*
	 * Flush all 24 ways (all possible locations of the data).
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

	mov	KT_L2_DIAG_DATA_MSB, %g7	! set select bits
	sllx	%g7, 32, %g7			! .
	set	0x7ffff8, %g1			! mask paddr to get $line
	and	%g2, %g1, %g1			!  way, set, bank (is PA[22:3])
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
	SET_SIZE(kt_l2_flushidx_ice)
#endif	/* lint */

/*
 * This routine does a load access to a specific floating point register
 * to invoke a previously injected register file error while in SUPERVISOR
 * context.
 *
 * Note that the address to be loaded from must be aligned for floating point
 * accesses.  Also quad instructions are trapping on Rock so they are
 * not used.
 *
 * Prototype:
 *
 * void kt_k_freg_load(uint64_t kvaddr, uint64_t target_reg);
 */
#if defined(lint)
/* ARGSUSED */
void
kt_k_freg_load(uint64_t kvaddr, uint64_t target_reg)
{}
#else
	.align	2048
	ENTRY_NP(kt_k_freg_load)

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
	 * Access a specific FP register.
	 */
	cmp   %o1, 0;  be,a %icc, 1f; ld  [%o0 + 0], %f0
	cmp   %o1, 1;  be,a %icc, 1f; ld  [%o0 + 0], %f1
	cmp   %o1, 2;  be,a %icc, 1f; ld  [%o0 + 0], %f2
	cmp   %o1, 3;  be,a %icc, 1f; ld  [%o0 + 0], %f3
	cmp   %o1, 4;  be,a %icc, 1f; ld  [%o0 + 0], %f4
	cmp   %o1, 5;  be,a %icc, 1f; ld  [%o0 + 0], %f5
	cmp   %o1, 6;  be,a %icc, 1f; ld  [%o0 + 0], %f6
	cmp   %o1, 7;  be,a %icc, 1f; ld  [%o0 + 0], %f7
	cmp   %o1, 8;  be,a %icc, 1f; ld  [%o0 + 0], %f8
	cmp   %o1, 9;  be,a %icc, 1f; ld  [%o0 + 0], %f9
	cmp   %o1, 10; be,a %icc, 1f; ld  [%o0 + 0], %f10
	cmp   %o1, 11; be,a %icc, 1f; ld  [%o0 + 0], %f11
	cmp   %o1, 12; be,a %icc, 1f; ld  [%o0 + 0], %f12
	cmp   %o1, 13; be,a %icc, 1f; ld  [%o0 + 0], %f13
	cmp   %o1, 14; be,a %icc, 1f; ld  [%o0 + 0], %f14
	cmp   %o1, 15; be,a %icc, 1f; ld  [%o0 + 0], %f15

	cmp   %o1, 16; be,a %icc, 1f; ld  [%o0 + 0], %f16
	cmp   %o1, 17; be,a %icc, 1f; ld  [%o0 + 0], %f17
	cmp   %o1, 18; be,a %icc, 1f; ld  [%o0 + 0], %f18
	cmp   %o1, 19; be,a %icc, 1f; ld  [%o0 + 0], %f19
	cmp   %o1, 20; be,a %icc, 1f; ld  [%o0 + 0], %f20
	cmp   %o1, 21; be,a %icc, 1f; ld  [%o0 + 0], %f21
	cmp   %o1, 22; be,a %icc, 1f; ld  [%o0 + 0], %f22
	cmp   %o1, 23; be,a %icc, 1f; ld  [%o0 + 0], %f23
	cmp   %o1, 24; be,a %icc, 1f; ld  [%o0 + 0], %f24
	cmp   %o1, 25; be,a %icc, 1f; ld  [%o0 + 0], %f25
	cmp   %o1, 26; be,a %icc, 1f; ld  [%o0 + 0], %f26
	cmp   %o1, 27; be,a %icc, 1f; ld  [%o0 + 0], %f27
	cmp   %o1, 28; be,a %icc, 1f; ld  [%o0 + 0], %f28
	cmp   %o1, 29; be,a %icc, 1f; ld  [%o0 + 0], %f29
	cmp   %o1, 30; be,a %icc, 1f; ld  [%o0 + 0], %f30
	cmp   %o1, 31; be,a %icc, 1f; ld  [%o0 + 0], %f31

	cmp   %o1, 32; be,a %icc, 1f; ldd [%o0 + 0], %f0
	cmp   %o1, 33; be,a %icc, 1f; ldd [%o0 + 0], %f2
	cmp   %o1, 34; be,a %icc, 1f; ldd [%o0 + 0], %f4
	cmp   %o1, 35; be,a %icc, 1f; ldd [%o0 + 0], %f6
	cmp   %o1, 36; be,a %icc, 1f; ldd [%o0 + 0], %f8
	cmp   %o1, 37; be,a %icc, 1f; ldd [%o0 + 0], %f10
	cmp   %o1, 38; be,a %icc, 1f; ldd [%o0 + 0], %f12
	cmp   %o1, 39; be,a %icc, 1f; ldd [%o0 + 0], %f14
	cmp   %o1, 40; be,a %icc, 1f; ldd [%o0 + 0], %f16
	cmp   %o1, 41; be,a %icc, 1f; ldd [%o0 + 0], %f18
	cmp   %o1, 42; be,a %icc, 1f; ldd [%o0 + 0], %f20
	cmp   %o1, 43; be,a %icc, 1f; ldd [%o0 + 0], %f22
	cmp   %o1, 44; be,a %icc, 1f; ldd [%o0 + 0], %f24
	cmp   %o1, 45; be,a %icc, 1f; ldd [%o0 + 0], %f26
	cmp   %o1, 46; be,a %icc, 1f; ldd [%o0 + 0], %f28
	cmp   %o1, 47; be,a %icc, 1f; ldd [%o0 + 0], %f30

	cmp   %o1, 48; be,a %icc, 1f; ldd [%o0 + 0], %f32
	cmp   %o1, 49; be,a %icc, 1f; ldd [%o0 + 0], %f34
	cmp   %o1, 50; be,a %icc, 1f; ldd [%o0 + 0], %f36
	cmp   %o1, 51; be,a %icc, 1f; ldd [%o0 + 0], %f38
	cmp   %o1, 52; be,a %icc, 1f; ldd [%o0 + 0], %f40
	cmp   %o1, 53; be,a %icc, 1f; ldd [%o0 + 0], %f42
	cmp   %o1, 54; be,a %icc, 1f; ldd [%o0 + 0], %f44
	cmp   %o1, 55; be,a %icc, 1f; ldd [%o0 + 0], %f46
	cmp   %o1, 56; be,a %icc, 1f; ldd [%o0 + 0], %f48
	cmp   %o1, 57; be,a %icc, 1f; ldd [%o0 + 0], %f50
	cmp   %o1, 58; be,a %icc, 1f; ldd [%o0 + 0], %f52
	cmp   %o1, 59; be,a %icc, 1f; ldd [%o0 + 0], %f54
	cmp   %o1, 60; be,a %icc, 1f; ldd [%o0 + 0], %f56
	cmp   %o1, 61; be,a %icc, 1f; ldd [%o0 + 0], %f58
	cmp   %o1, 62; be,a %icc, 1f; ldd [%o0 + 0], %f60
	cmp   %o1, 63; be,a %icc, 1f; ldd [%o0 + 0], %f62
1:
	wr	%o4, %g0, %fprs			! restore the fprs
	wrpr	%o5, %pstate			! restore the processor state
	retl					! return
	  nop
	SET_SIZE(kt_k_freg_load)
#endif	/* lint */

/*
 * This routine does an op access to a specific floating point register
 * to invoke a previously injected register file error while in SUPERVISOR
 * context.
 *
 * Note quad instructions are trapping so they are not used.
 *
 * Prototype:
 *
 * void kt_k_freg_op(uint64_t target_reg);
 */
#if defined(lint)
/* ARGSUSED */
void
kt_k_freg_op(uint64_t target_reg)
{}
#else
	.align	2048
	ENTRY_NP(kt_k_freg_op)

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
	 * Access a specific FP register.
	 */
	cmp   %o0, 0;  be,a %icc, 1f; fmuls	%f0, %f20, %f0
	cmp   %o0, 1;  be,a %icc, 1f; fadds	%f1, %f20, %f1
	cmp   %o0, 2;  be,a %icc, 1f; fmuls 	%f2, %f20, %f2
	cmp   %o0, 3;  be,a %icc, 1f; fadds	%f3, %f20, %f3
	cmp   %o0, 4;  be,a %icc, 1f; fmuls	%f4, %f20, %f4
	cmp   %o0, 5;  be,a %icc, 1f; fadds	%f5, %f20, %f5
	cmp   %o0, 6;  be,a %icc, 1f; fmuls 	%f6, %f20, %f6
	cmp   %o0, 7;  be,a %icc, 1f; fadds	%f7, %f20, %f7
	cmp   %o0, 8;  be,a %icc, 1f; fmuls	%f8, %f20, %f8
	cmp   %o0, 9;  be,a %icc, 1f; fadds	%f9, %f20, %f9
	cmp   %o0, 10; be,a %icc, 1f; fmuls 	%f10, %f20, %f10
	cmp   %o0, 11; be,a %icc, 1f; fadds	%f11, %f20, %f11
	cmp   %o0, 12; be,a %icc, 1f; fmuls	%f12, %f20, %f12
	cmp   %o0, 13; be,a %icc, 1f; fadds	%f13, %f20, %f13
	cmp   %o0, 14; be,a %icc, 1f; fmuls 	%f14, %f20, %f14
	cmp   %o0, 15; be,a %icc, 1f; fadds	%f15, %f20, %f15

	cmp   %o0, 16; be,a %icc, 1f; fmuls	%f16, %f20, %f16
	cmp   %o0, 17; be,a %icc, 1f; fadds	%f17, %f20, %f17
	cmp   %o0, 18; be,a %icc, 1f; fmuls	%f18, %f20, %f18
	cmp   %o0, 19; be,a %icc, 1f; fadds	%f19, %f20, %f19
	cmp   %o0, 20; be,a %icc, 1f; fmuls	%f20, %f20, %f20
	cmp   %o0, 21; be,a %icc, 1f; fadds	%f21, %f20, %f21
	cmp   %o0, 22; be,a %icc, 1f; fmuls	%f22, %f20, %f22
	cmp   %o0, 23; be,a %icc, 1f; fadds	%f23, %f20, %f23
	cmp   %o0, 24; be,a %icc, 1f; fmuls	%f24, %f20, %f24
	cmp   %o0, 25; be,a %icc, 1f; fadds	%f25, %f20, %f25
	cmp   %o0, 26; be,a %icc, 1f; fmuls	%f26, %f20, %f26
	cmp   %o0, 27; be,a %icc, 1f; fadds	%f27, %f20, %f27
	cmp   %o0, 28; be,a %icc, 1f; fmuls	%f28, %f20, %f28
	cmp   %o0, 29; be,a %icc, 1f; fadds	%f29, %f20, %f29
	cmp   %o0, 30; be,a %icc, 1f; fmuls	%f30, %f20, %f30
	cmp   %o0, 31; be,a %icc, 1f; fadds	%f31, %f20, %f31

	cmp   %o0, 32; be,a %icc, 1f; fmuld	%f0, %f20, %f0
	cmp   %o0, 33; be,a %icc, 1f; faddd	%f2, %f20, %f2
	cmp   %o0, 34; be,a %icc, 1f; fmuld	%f4, %f20, %f4
	cmp   %o0, 35; be,a %icc, 1f; faddd	%f6, %f20, %f6
	cmp   %o0, 36; be,a %icc, 1f; fmuld	%f8, %f20, %f8
	cmp   %o0, 37; be,a %icc, 1f; fadds	%f10, %f20, %f10
	cmp   %o0, 38; be,a %icc, 1f; fmuld	%f12, %f20, %f12
	cmp   %o0, 39; be,a %icc, 1f; faddd	%f14, %f20, %f14
	cmp   %o0, 40; be,a %icc, 1f; fmuld	%f16, %f20, %f16
	cmp   %o0, 41; be,a %icc, 1f; faddd	%f18, %f20, %f18
	cmp   %o0, 42; be,a %icc, 1f; fmuld	%f20, %f20, %f20
	cmp   %o0, 43; be,a %icc, 1f; faddd	%f22, %f20, %f22
	cmp   %o0, 44; be,a %icc, 1f; fmuld	%f24, %f20, %f24
	cmp   %o0, 45; be,a %icc, 1f; faddd	%f26, %f20, %f26
	cmp   %o0, 46; be,a %icc, 1f; fmuld	%f28, %f20, %f28
	cmp   %o0, 47; be,a %icc, 1f; faddd	%f30, %f20, %f30

	cmp   %o0, 48; be,a %icc, 1f; fmuld	%f32, %f20, %f32
	cmp   %o0, 49; be,a %icc, 1f; faddd	%f34, %f20, %f34
	cmp   %o0, 50; be,a %icc, 1f; fmuld	%f36, %f20, %f36
	cmp   %o0, 51; be,a %icc, 1f; faddd	%f38, %f20, %f48
	cmp   %o0, 52; be,a %icc, 1f; fmuld	%f40, %f20, %f40
	cmp   %o0, 53; be,a %icc, 1f; faddd	%f42, %f20, %f42
	cmp   %o0, 54; be,a %icc, 1f; fmuld	%f44, %f20, %f44
	cmp   %o0, 55; be,a %icc, 1f; faddd	%f46, %f20, %f46
	cmp   %o0, 56; be,a %icc, 1f; fmuld	%f48, %f20, %f48
	cmp   %o0, 57; be,a %icc, 1f; faddd	%f50, %f20, %f50
	cmp   %o0, 58; be,a %icc, 1f; fmuld	%f52, %f20, %f52
	cmp   %o0, 59; be,a %icc, 1f; faddd	%f54, %f20, %f54
	cmp   %o0, 60; be,a %icc, 1f; fmuld	%f56, %f20, %f56
	cmp   %o0, 61; be,a %icc, 1f; faddd	%f58, %f20, %f58
	cmp   %o0, 62; be,a %icc, 1f; fmuld	%f60, %f20, %f60
	cmp   %o0, 63; be,a %icc, 1f; faddd	%f62, %f20, %f62
1:
	wr	%o4, %g0, %fprs			! restore the fprs
	wrpr	%o5, %pstate			! restore the processor state
	retl					! return
	  nop
	SET_SIZE(kt_k_freg_op)
#endif	/* lint */

/*
 * This routine does a store access to a specific floating point register
 * to invoke a previously injected register file error while in SUPERVISOR
 * context.
 *
 * Note that the address to be loaded from must be aligned for floating point
 * accesses.  Also quad instructions are trapping so they are not used.
 *
 * Prototype:
 *
 * void kt_k_freg_store(uint64_t kvaddr, uint64_t target_reg);
 */
#if defined(lint)
/* ARGSUSED */
void
kt_k_freg_store(uint64_t kvaddr, uint64_t target_reg)
{}
#else
	.align	2048
	ENTRY_NP(kt_k_freg_store)

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
	 * Access a specific FP register.
	 */
	cmp   %o1, 0;  be,a %icc, 1f; st  %f0,  [%o0 + 16]
	cmp   %o1, 1;  be,a %icc, 1f; st  %f1,  [%o0 + 16]
	cmp   %o1, 2;  be,a %icc, 1f; st  %f2,  [%o0 + 16]
	cmp   %o1, 3;  be,a %icc, 1f; st  %f3,  [%o0 + 16]
	cmp   %o1, 4;  be,a %icc, 1f; st  %f4,  [%o0 + 16]
	cmp   %o1, 5;  be,a %icc, 1f; st  %f5,  [%o0 + 16]
	cmp   %o1, 6;  be,a %icc, 1f; st  %f6,  [%o0 + 16]
	cmp   %o1, 7;  be,a %icc, 1f; st  %f7,  [%o0 + 16]
	cmp   %o1, 8;  be,a %icc, 1f; st  %f8,  [%o0 + 16]
	cmp   %o1, 9;  be,a %icc, 1f; st  %f9,  [%o0 + 16]
	cmp   %o1, 10; be,a %icc, 1f; st  %f10, [%o0 + 16]
	cmp   %o1, 11; be,a %icc, 1f; st  %f11, [%o0 + 16]
	cmp   %o1, 12; be,a %icc, 1f; st  %f12, [%o0 + 16]
	cmp   %o1, 13; be,a %icc, 1f; st  %f13, [%o0 + 16]
	cmp   %o1, 14; be,a %icc, 1f; st  %f14, [%o0 + 16]
	cmp   %o1, 15; be,a %icc, 1f; st  %f15, [%o0 + 16]

	cmp   %o1, 16; be,a %icc, 1f; st  %f16, [%o0 + 16]
	cmp   %o1, 17; be,a %icc, 1f; st  %f17, [%o0 + 16]
	cmp   %o1, 18; be,a %icc, 1f; st  %f18, [%o0 + 16]
	cmp   %o1, 19; be,a %icc, 1f; st  %f19, [%o0 + 16]
	cmp   %o1, 20; be,a %icc, 1f; st  %f20, [%o0 + 16]
	cmp   %o1, 21; be,a %icc, 1f; st  %f21, [%o0 + 16]
	cmp   %o1, 22; be,a %icc, 1f; st  %f22, [%o0 + 16]
	cmp   %o1, 23; be,a %icc, 1f; st  %f23, [%o0 + 16]
	cmp   %o1, 24; be,a %icc, 1f; st  %f24, [%o0 + 16]
	cmp   %o1, 25; be,a %icc, 1f; st  %f25, [%o0 + 16]
	cmp   %o1, 26; be,a %icc, 1f; st  %f26, [%o0 + 16]
	cmp   %o1, 27; be,a %icc, 1f; st  %f27, [%o0 + 16]
	cmp   %o1, 28; be,a %icc, 1f; st  %f28, [%o0 + 16]
	cmp   %o1, 29; be,a %icc, 1f; st  %f29, [%o0 + 16]
	cmp   %o1, 30; be,a %icc, 1f; st  %f30, [%o0 + 16]
	cmp   %o1, 31; be,a %icc, 1f; st  %f31, [%o0 + 16]

	cmp   %o1, 32; be,a %icc, 1f; std %f0,  [%o0 + 16]
	cmp   %o1, 33; be,a %icc, 1f; std %f2,  [%o0 + 16]
	cmp   %o1, 34; be,a %icc, 1f; std %f4,  [%o0 + 16]
	cmp   %o1, 35; be,a %icc, 1f; std %f6,  [%o0 + 16]
	cmp   %o1, 36; be,a %icc, 1f; std %f8,  [%o0 + 16]
	cmp   %o1, 37; be,a %icc, 1f; std %f10, [%o0 + 16]
	cmp   %o1, 38; be,a %icc, 1f; std %f12, [%o0 + 16]
	cmp   %o1, 39; be,a %icc, 1f; std %f14, [%o0 + 16]
	cmp   %o1, 40; be,a %icc, 1f; std %f16, [%o0 + 16]
	cmp   %o1, 41; be,a %icc, 1f; std %f18, [%o0 + 16]
	cmp   %o1, 42; be,a %icc, 1f; std %f20, [%o0 + 16]
	cmp   %o1, 43; be,a %icc, 1f; std %f22, [%o0 + 16]
	cmp   %o1, 44; be,a %icc, 1f; std %f24, [%o0 + 16]
	cmp   %o1, 45; be,a %icc, 1f; std %f26, [%o0 + 16]
	cmp   %o1, 46; be,a %icc, 1f; std %f28, [%o0 + 16]
	cmp   %o1, 47; be,a %icc, 1f; std %f30, [%o0 + 16]

	cmp   %o1, 48; be,a %icc, 1f; std %f32, [%o0 + 16]
	cmp   %o1, 49; be,a %icc, 1f; std %f34, [%o0 + 16]
	cmp   %o1, 50; be,a %icc, 1f; std %f36, [%o0 + 16]
	cmp   %o1, 51; be,a %icc, 1f; std %f38, [%o0 + 16]
	cmp   %o1, 52; be,a %icc, 1f; std %f40, [%o0 + 16]
	cmp   %o1, 53; be,a %icc, 1f; std %f42, [%o0 + 16]
	cmp   %o1, 54; be,a %icc, 1f; std %f44, [%o0 + 16]
	cmp   %o1, 55; be,a %icc, 1f; std %f46, [%o0 + 16]
	cmp   %o1, 56; be,a %icc, 1f; std %f48, [%o0 + 16]
	cmp   %o1, 57; be,a %icc, 1f; std %f50, [%o0 + 16]
	cmp   %o1, 58; be,a %icc, 1f; std %f52, [%o0 + 16]
	cmp   %o1, 59; be,a %icc, 1f; std %f54, [%o0 + 16]
	cmp   %o1, 60; be,a %icc, 1f; std %f56, [%o0 + 16]
	cmp   %o1, 61; be,a %icc, 1f; std %f58, [%o0 + 16]
	cmp   %o1, 62; be,a %icc, 1f; std %f60, [%o0 + 16]
	cmp   %o1, 63; be,a %icc, 1f; std %f62, [%o0 + 16]
1:
	wr	%o4, %g0, %fprs			! restore the fprs
	wrpr	%o5, %pstate			! restore the processor state
	retl					! return
	  nop
	SET_SIZE(kt_k_freg_store)
#endif	/* lint */
