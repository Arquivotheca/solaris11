/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This is the sun4v assembly and hyperprivileged routine file
 * for the CPU/Memory Error Injector driver.
 */

#include <sys/memtest_v_asm.h>

/*
 * Coding rules for routines that run in hypervisor mode:
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
 *	8) all routines and macros CANNOT use absolute labels.	All labels
 *	   must be relative.
 */

/*
 * The following set of hv_asi_* routines are to be run in hypervisor mode
 * via the hcall API.
 */

/*
 * This routine does an 8-bit load using ASI bypass.  The data is
 * returned via %o1 (hcall convention).
 *
 * Prototype:
 *
 * int hv_asi_load8(int asi, uint64_t addr, uint64_t *debug);
 */
#if defined(lint)
/* ARGSUSED */
int
hv_asi_load8(int asi, uint64_t addr, uint64_t *debug)
{
	return 0;
}
#else
	.align	32
	ENTRY_NP(hv_asi_load8)
	rd	%asi, %g1			! save asi reg
	wr	%g0, %o1, %asi
	
	lduba	[%o2]%asi, %o1			! perform access

	wr	%g0, %g1, %asi			! restore asi reg
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(hv_asi_load8)
#endif	/* lint */

/*
 * This routine does an 8-bit store using ASI bypass.
 *
 * Prototype:
 *
 * int hv_asi_store8(int asi, uint64_t addr, uint8_t value, uint64_t *debug);
 */
#if defined(lint)
/* ARGSUSED */
int
hv_asi_store8(int asi, uint64_t addr, uint8_t value, uint64_t *debug)
{
	return 0;
}
#else
	.align	32
	ENTRY_NP(hv_asi_store8)
	rd	%asi, %g1			! save asi reg
	wr	%g0, %o1, %asi

	stuba	%o3, [%o2]%asi			! perform access

	wr	%g0, %g1, %asi			! restore asi reg
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(hv_asi_store8)
#endif	/* lint */

/*
 * This routine does a 16-bit load using ASI bypass.
 *
 * Prototype:
 *
 * int hv_asi_load16(int asi, uint64_t addr, uint64_t *debug);
 */
#if defined(lint)
/* ARGSUSED */
int
hv_asi_load16(int asi, uint64_t addr, uint64_t *debug)
{
	return 0;
}
#else
	.align	32
	ENTRY_NP(hv_asi_load16)
	rd	%asi, %g1			! save asi reg
	wr	%g0, %o1, %asi
	
	lduwa	[%o2]%asi, %o1			! perform access

	wr	%g0, %g1, %asi			! restore asi reg
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(hv_asi_load16)
#endif	/* lint */

/*
 * This routine does a 16-bit store using ASI bypass.
 *
 * Prototype:
 *
 * int hv_asi_store16(int asi, uint64_t addr, uint16_t value, uint64_t *debug);
 */
#if defined(lint)
/* ARGSUSED */
int
hv_asi_store16(int asi, uint64_t addr, uint16_t value, uint64_t *debug)
{
	return 0;
}
#else
	.align	32
	ENTRY_NP(hv_asi_store16)
	rd	%asi, %g1			! save asi reg
	wr	%g0, %o1, %asi

	stuwa	%o3, [%o2]%asi			! perform access

	wr	%g0, %g1, %asi			! restore asi reg
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(hv_asi_store16)
#endif	/* lint */

/*
 * This routine does a 32-bit load using ASI bypass.
 *
 * Prototype:
 *
 * int hv_asi_load32(int asi, uint64_t addr, uint64_t *debug);
 */
#if defined(lint)
/* ARGSUSED */
int
hv_asi_load32(int asi, uint64_t addr, uint64_t *debug)
{
	return 0;
}
#else
	.align	32
	ENTRY_NP(hv_asi_load32)
	rd	%asi, %g1			! save asi reg
	wr	%g0, %o1, %asi
	
	lda	[%o2]%asi, %o1			! perform access

	wr	%g0, %g1, %asi			! restore asi reg
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(hv_asi_load32)
#endif	/* lint */

/*
 * This routine does a 32-bit store using ASI bypass.
 *
 * Prototype:
 *
 * int hv_asi_store32(int asi, uint64_t addr, uint32_t value, uint64_t *debug);
 */
#if defined(lint)
/* ARGSUSED */
int
hv_asi_store32(int asi, uint64_t addr, uint32_t value, uint64_t *debug)
{
	return 0;
}
#else
	.align	32
	ENTRY_NP(hv_asi_store32)
	rd	%asi, %g1			! save asi reg
	wr	%g0, %o1, %asi

	sta	%o3, [%o2]%asi			! perform access

	wr	%g0, %g1, %asi			! restore asi reg
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(hv_asi_store32)
#endif	/* lint */

/*
 * This routine does a 64-bit load using ASI bypass.
 *
 * Prototype:
 *
 * int hv_asi_load64(int asi, uint64_t addr, uint64_t *debug);
 */
#if defined(lint)
/* ARGSUSED */
int
hv_asi_load64(int asi, uint64_t addr, uint64_t *debug)
{
	return 0;
}
#else
	.align	32
	ENTRY_NP(hv_asi_load64)
	rd	%asi, %g1			! save asi reg
	wr	%g0, %o1, %asi
	
	ldxa	[%o2]%asi, %o1			! perform access

	wr	%g0, %g1, %asi			! restore asi reg
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(hv_asi_load64)
#endif	/* lint */

/*
 * This routine does a 64-bit store using ASI bypass.
 *
 * Prototype:
 *
 * int hv_asi_store64(int asi, uint64_t addr, uint64_t value, uint64_t *debug);
 */
#if defined(lint)
/* ARGSUSED */
int
hv_asi_store64(int asi, uint64_t addr, uint64_t value, uint64_t *debug)
{
	return 0;
}
#else
	.align	32
	ENTRY_NP(hv_asi_store64)
	rd	%asi, %g1			! save asi reg
	wr	%g0, %o1, %asi

	stxa	%o3, [%o2]%asi			! perform access

	wr	%g0, %g1, %asi			! restore asi reg
	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(hv_asi_store64)
#endif	/* lint */

/*
 * The following set of hv_paddr_* routines are to be run in hypervisor mode
 * via the hcall API.
 */

/*
 * This routine does an 8-bit load from a physical address.  The data is
 * returned via %o1 (hcall convention).
 *
 * Prototype:
 *
 * int hv_paddr_load8(uint64_t paddr, uint64_t *debug);
 */
#if defined(lint)
/* ARGSUSED */
int
hv_paddr_load8(uint64_t paddr, uint64_t *debug)
{
	return 0;
}
#else
	.align	32
	ENTRY_NP(hv_paddr_load8)
	ldub	[%o1], %o1			! perform access

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(hv_paddr_load8)
#endif	/* lint */

/*
 * This routine does an 8-bit store to a physical address.
 *
 * Prototype:
 *
 * int hv_paddr_store8(uint64_t paddr, uint8_t value, uint64_t *debug);
 */
#if defined(lint)
/* ARGSUSED */
int
hv_paddr_store8(uint64_t paddr, uint8_t value, uint64_t *debug)
{
	return 0;
}
#else
	.align	32
	ENTRY_NP(hv_paddr_store8)
	stub	%o2, [%o1]			! perform access

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(hv_paddr_store8)
#endif	/* lint */

/*
 * This routine does a 16-bit load from a physical address.
 *
 * Prototype:
 *
 * int hv_paddr_load16(uint64_t paddr, uint64_t *debug);
 */
#if defined(lint)
/* ARGSUSED */
int
hv_paddr_load16(uint64_t paddr, uint64_t *debug)
{
	return 0;
}
#else
	.align	32
	ENTRY_NP(hv_paddr_load16)
	lduw	[%o1], %o1			! perform access

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(hv_paddr_load16)
#endif	/* lint */

/*
 * This routine does a 16-bit store to a physical address.
 *
 * Prototype:
 *
 * int hv_paddr_store16(uint64_t paddr, uint16_t value, uint64_t *debug);
 */
#if defined(lint)
/* ARGSUSED */
int
hv_paddr_store16(uint64_t paddr, uint16_t value, uint64_t *debug)
{
	return 0;
}
#else
	.align	32
	ENTRY_NP(hv_paddr_store16)
	stuw	%o2, [%o1]			! perform access

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(hv_paddr_store16)
#endif	/* lint */

/*
 * This routine does a 32-bit load from a physical address.
 *
 * Prototype:
 *
 * int hv_paddr_load32(uint64_t paddr, uint64_t *debug);
 */
#if defined(lint)
/* ARGSUSED */
int
hv_paddr_load32(uint64_t paddr, uint64_t *debug)
{
	return 0;
}
#else
	.align	32
	ENTRY_NP(hv_paddr_load32)
	ld	[%o1], %o1			! perform access

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(hv_paddr_load32)
#endif	/* lint */

/*
 * This routine does a 32-bit store to a physical address.
 *
 * Prototype:
 *
 * int hv_paddr_store32(uint64_t paddr, uint32_t value, uint64_t *debug);
 */
#if defined(lint)
/* ARGSUSED */
int
hv_paddr_store32(uint64_t paddr, uint32_t value, uint64_t *debug)
{
	return 0;
}
#else
	.align	32
	ENTRY_NP(hv_paddr_store32)
	st	%o2, [%o1]			! perform access

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(hv_paddr_store32)
#endif	/* lint */

/*
 * This routine does a 64-bit load from a physical address.
 *
 * Prototype:
 *
 * int hv_paddr_load64(uint64_t paddr, uint64_t *debug);
 */
#if defined(lint)
/* ARGSUSED */
int
hv_paddr_load64(uint64_t paddr, uint64_t *debug)
{
	return 0;
}
#else
	.align	32
	ENTRY_NP(hv_paddr_load64)
	ldx	[%o1], %o1			! perform access

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(hv_paddr_load64)
#endif	/* lint */

/*
 * This routine does a 64-bit store to a physical address.
 *
 * Prototype:
 *
 * int hv_paddr_store64(uint64_t paddr, uint64_t value, uint64_t *debug);
 */
#if defined(lint)
/* ARGSUSED */
int
hv_paddr_store64(uint64_t paddr, uint64_t value, uint64_t *debug)
{
	return 0;
}
#else
	.align	32	
	ENTRY_NP(hv_paddr_store64)
	stx	%o2, [%o1]			! perform store access

	mov	%g0, %o0			! return PASS
	done					! .
	SET_SIZE(hv_paddr_store64)
#endif	/* lint */

/*
 * This routine attempts to place an SP failed epacket onto the resumable
 * error queue for the system to act upon.
 *
 * Prototype:
 *
 * int hv_queue_resumable_epkt(uint64_t epkt_paddr, uint64_t rq_base);
 */
#if defined(lint)
/* ARGSUSED */
int
hv_queue_resumable_epkt(uint64_t epkt_paddr, uint64_t rq_base)
{
	return (0);
}
#else
	.align	256
	ENTRY_NP(hv_queue_resumable_epkt)
	mov	CPU_RQ_HD, %g4
	ldxa	[%g4]ASI_QUEUE, %g2		! %g2 = Q head offset
	mov	CPU_RQ_TL, %g4
	ldxa	[%g4]ASI_QUEUE, %g3		! %g3 = Q tail offset

	cmp	%g2, %g3
	bne,pn	%xcc, 1f			! head != tail, Q in use.
	  nop

	add	%g3, Q_ENTRY_SIZE, %g6		! %g6 = new Q tail
	stxa	%g6, [%g4]ASI_QUEUE

	add	%g3, %o2, %o3			! %o3 = Q base + tail

	/* Move 64 bytes from buf to queue. */
	set	0, %g5
	ldx	[%o1 + %g5], %g1
	stx	%g1, [%o3 + %g5]		! byte 0 - 7
	add	%g5, 8, %g5
	ldx	[%o1 + %g5], %g1
	stx	%g1, [%o3 + %g5]		! byte 8 - 15
	add	%g5, 8, %g5
	ldx	[%o1 + %g5], %g1
	stx	%g1, [%o3 + %g5]		! byte 16 - 23
	add	%g5, 8, %g5
	ldx	[%o1 + %g5], %g1
	stx	%g1, [%o3 + %g5]		! byte 24 - 31
	add	%g5, 8, %g5
	ldx	[%o1 + %g5], %g1
	stx	%g1, [%o3 + %g5]		! byte 32 - 39
	add	%g5, 8, %g5
	ldx	[%o1 + %g5], %g1
	stx	%g1, [%o3 + %g5]		! byte 40 - 47
	add	%g5, 8, %g5
	ldx	[%o1 + %g5], %g1
	stx	%g1, [%o3 + %g5]		! byte 48 - 55
	add	%g5, 8, %g5
	ldx	[%o1 + %g5], %g1
	stx	%g1, [%o3 + %g5]		! byte 56 - 63
1:
	mov	%g0, %o0			! return PASS
	mov	%g0, %o1			! .
	done
	SET_SIZE(hv_queue_resumable_epkt)
#endif /* lint */

/*
 * Routine to return the cpu version on sun4v systems.  To be run in
 * hyperpriv mode through hcall API.
 */
#if defined(lint)
/*ARGSUSED*/
uint64_t
memtest_get_cpu_ver_asm(void)
{
	return 0;
}
#else
	.align	64
	ENTRY(memtest_get_cpu_ver_asm)
	rdhpr    %hver, %o1

	mov	%g0, %o0
	done
	SET_SIZE(memtest_get_cpu_ver_asm)
#endif	/* lint */

/*
 * This routine is used by the hyperpriv i-cache tests to trip a planted
 * instruction error.  This is why no actual operation is performed.
 */
#if defined(lint)
/*ARGSUSED*/
void
memtest_hv_asm_access(void)
{}
#else
     	.align	64
	ENTRY(memtest_hv_asm_access)
	clr	%o1				! = 0x92100000
	clr	%o1
	clr	%o1
	clr	%o1
	clr	%o1
	clr	%o1
	clr	%o1
	clr	%o1

! 	PRINT("In the hv access routine, ret addr = ");	! DEBUG, clobbers all
!	PRINTX(%g7);					! %g regs of caller
!	PRINT("\n\r");

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
	jmp	%g7 + 4
	  nop
        SET_SIZE(memtest_hv_asm_access)
#endif	/* lint */

/*
 * This routine is a simple function to determine if the hypervisor
 * DIAG traps used by the injector both exist and are enabled so that
 * routines can subsequently use the hypervisors back door DIAG_HEXEC
 * trap to access hyperprivileged resources.
 *
 * The first instruction is the value to look for when dumping memory.
 *
 *		%o0-%o3 - arg0 to arg3
 *		---
 *		%o0 - return status (PASS = 0)
 *		%o1 - return value (0xa55 payload is a PASS)
 */
#if defined(lint)
/* ARGSUSED */
uint64_t
memtest_hv_trap_check_asm(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3)
{
	return 0;
}
#else
.align  64
ENTRY_NP(memtest_hv_trap_check_asm)
	clr     %o1                             ! = 0x92100000

	mov     0xa55, %o1                      ! put values in the regs
	mov     0xa56, %o2
	mov     0xa57, %o3
	mov     0xa58, %o4

	mov     %g0, %o0                        ! return PASS
	done                                    ! .
	SET_SIZE(memtest_hv_trap_check_asm)
#endif  /* lint */

/*
 * Routine to perform an hcall to find the paddr of an raddr.
 *
 *		%o0	- raddr
 *		---
 *		%o0	- paddr (-1 on FAIL)
 */
#if defined(lint)
/* ARGSUSED */
uint64_t
memtest_ra_to_pa(uint64_t raddr)
{
	return 0;
}
#else
     	.align	64
	ENTRY(memtest_ra_to_pa)
	mov	DIAG_RA2PA, %o5		! hcall type
	ta	FAST_TRAP		! FAST_TRAP=0x80
	tst	%o0			! ensure it "passed"
	movz	%xcc, %o1, %o0		! if so get the returned paddr
	retl
	  movnz	%xcc, -1, %o0
	SET_SIZE(memtest_ra_to_pa)
#endif	/* lint */

/*
 * Routine to run a routine as hyperpriv via the hypervisor diagnostic API.
 *
 *		%o0	- paddr of routine to run in hypervisor mode
 *		%o1-%o4	- arg1 to arg4
 *		---
 *		%o0 - return status (PASS = 0)
 *		%o1 - optional return value (generally PASS if != -1)
 */
#if defined(lint)
/* ARGSUSED */
uint64_t
memtest_run_hpriv(uint64_t paddr, uint64_t a1, uint64_t a2, uint64_t a3,
					uint64_t a4)
{
	return 0;
}
#else
     	.align	64
	ENTRY(memtest_run_hpriv)
	mov	DIAG_HEXEC, %o5		! hcall type
	ta	FAST_TRAP		! FAST_TRAP=0x80
	tst	%o0			! ensure it "passed"
	movz	%xcc, %o1, %o0		! if so get return value
	retl
	  movnz	%xcc, -1, %o0
	SET_SIZE(memtest_run_hpriv)
#endif	/* lint */
