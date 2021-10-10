/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/memtest_u_asm.h>
#include <sys/machthread.h>

/*
 * This is the sun4u assembly routine file for the CPU/Memory Error
 * Injector driver.
 */

/*
 * This routine was copied from "gen_flush_ec" in "sun4u/cpu/spitfire_asm.s".
 * Its pretty generic and should work for any non-associative L2 or L3$.
 */
#if defined(lint)
/*ARGSUSED*/
int
gen_flush_l2(uint64_t paddr, int ecache_size)
{
	return (0);
}
#else	/* lint */
	ENTRY(gen_flush_l2)
	or	%o1, %g0, %o2			! put ecache size in %o2
	xor	%o0, %o2, %o1			! calculate alias address
	add	%o2, %o2, %o3			! 2 * ecachesize in case
						! addr == ecache_flushaddr
	sub	%o3, 1, %o3			! -1 == mask
	and	%o1, %o3, %o1			! and with xor'd address
	set	ecache_flushaddr, %o3
	ldx	[%o3], %o3

	rdpr	%pstate, %o4
	andn	%o4, PSTATE_IE | PSTATE_AM, %o5
	wrpr	%o5, %g0, %pstate		! clear IE, AM bits

	ldxa	[%g0]ASI_ESTATE_ERR, %g1
	stxa	%g0, [%g0]ASI_ESTATE_ERR	! disable errors
	membar	#Sync

	ldxa	[%o1 + %o3]ASI_MEM, %g0		! load ecache_flushaddr + alias
	membar	#Sync
	stxa	%g1, [%g0]ASI_ESTATE_ERR	! restore error enable
	membar	#Sync

	mov	%g0, %o0
	retl
	wrpr	%g0, %o4, %pstate
	SET_SIZE(gen_flush_l2)
#endif	/* lint */

/*
 * This routine returns the asynchronous fault address register.
 */
#if defined(lint)
uint64_t
memtest_get_afar(void)
{
	return ((uint64_t)0);
}
#else
	ENTRY(memtest_get_afar)
	retl
	ldxa	[%g0]ASI_AFAR, %o0	! read AFAR
	SET_SIZE(memtest_get_afar)
#endif  /* lint */

/*
 * This routine returns the AFSR.
 */
#if defined(lint)
uint64_t
memtest_get_afsr(void)
{
	return ((uint64_t)0);
}
#else
	ENTRY(memtest_get_afsr)
	retl
	ldxa	[%g0]ASI_AFSR, %o0	! read AFSR
	SET_SIZE(memtest_get_afsr)
#endif  /* lint */

/*
 * This routine returns the AFSR_EXT.
 * Panther has an "Extension AFSR" to capture L3 cache errors.
 */
#if defined(lint)
uint64_t
memtest_get_afsr_ext(void)
{
	return ((uint64_t)0);
}
#else
	ENTRY(memtest_get_afsr_ext)
	set ASI_AFSR_EXT_VA, %o1
	retl
	ldxa	[%o1]ASI_AFSR, %o0	! read AFSR_EXT
	SET_SIZE(memtest_get_afsr_ext)
#endif  /* lint */

/*
 * This routine reads the DCUCR.
 */
#if defined(lint)
/*ARGSUSED*/
uint64_t
memtest_get_dcr(void)
{
	return (0);
}
#else	/* lint */
	ENTRY(memtest_get_dcr)
	retl
	rd	%asr18, %o0
	SET_SIZE(memtest_get_dcr)
#endif	/* lint */

/*
 * This routine returns the cache control register (LSU/DCU).
 */
#if defined(lint)
uint64_t
memtest_get_dcucr(void)
{
	return ((uint64_t)0);
}
#else

	ENTRY(memtest_get_dcucr)
	GET_CPU_IMPL(%g1)		! get cpu impl
        cmp     %g1, SPITFIRE_IMPL
        bl	1f			! Not applicable to FJ processors
	 nop				! FJ cpu IMPL is < SPITFIRE_IMPL

	ldxa	[%g0]ASI_LSU, %o0	! read LSU/DCU control register
	retl
	nop
1:
	retl
	mov	%g0, %o0
	SET_SIZE(memtest_get_dcucr)
#endif  /* lint */

/*
 * This routine reads the E$ Error Enable register.
 */
#if defined(lint)
/*ARGSUSED*/
uint64_t
memtest_get_eer(void)
{
	return (0);
}
#else	/* lint	*/
	ENTRY(memtest_get_eer)
	GET_CPU_IMPL(%g1)		! get cpu impl
        cmp     %g1, SPITFIRE_IMPL
        bl	1f			! Not applicable to FJ processors
	 nop				! FJ cpu IMPL is < SPITFIRE_IMPL
	retl
	ldxa    [%g0]ASI_ESTATE_ERR, %o0	! read ecache error enable reg
1:
	retl
	mov	%g0, %o0
	SET_SIZE(memtest_get_eer)
#endif	/* lint */

/*
 * This routine reads the CPU version register.
 */
#if defined(lint)
/*ARGSUSED*/
uint64_t
memtest_get_cpu_ver_asm(void)
{
	return (0);
}
#else	/* lint */
	ENTRY(memtest_get_cpu_ver_asm)
	retl
	rdpr    %ver, %o0
	SET_SIZE(memtest_get_cpu_ver_asm)
#endif	/* lint */

/*
 * This routine sets the AFSR and returns the previous value.
 */
#if defined(lint)
/*ARGSUSED*/
uint64_t
memtest_set_afsr(uint64_t afsr)
{
	return (0);
}
#else
	ENTRY(memtest_set_afsr)
	GET_CPU_IMPL(%g1)		! get cpu impl
        cmp     %g1, SPITFIRE_IMPL
        bl	1f			! Not applicable to FJ processors
	 nop				! FJ cpu IMPL is < SPITFIRE_IMPL

	ldxa	[%g0]ASI_AFSR, %o1	! load prev 
	stxa	%o0, [%g0]ASI_AFSR	! store to AFSR
	membar	#Sync
	retl
	mov	%o1, %o0
1:
	retl
	mov	%g0, %o0
	SET_SIZE(memtest_set_afsr)
#endif  /* lint */

/*
 * This routine sets the AFSR_EXT and returns the previous value.
 * Panther has an "Extension AFSR" to capture L3 cache errors.
 */
#if defined(lint)
/*ARGSUSED*/
uint64_t
memtest_set_afsr_ext(uint64_t afsr)
{
	return (0);
}
#else
	ENTRY(memtest_set_afsr_ext)
	GET_CPU_IMPL(%g1)		! get cpu impl
        cmp     %g1, SPITFIRE_IMPL
        bl	1f			! Not applicable to FJ processors
	 nop				! FJ cpu IMPL is < SPITFIRE_IMPL

	set	ASI_AFSR_EXT_VA, %o2
	ldxa	[%o2]ASI_AFSR, %o1	! load prev
	stxa	%o0, [%o2]ASI_AFSR	! store to AFSR_EXT
	membar	#Sync
	retl
	mov	%o1, %o0
1:
	retl
	mov	%g0, %o0
	SET_SIZE(memtest_set_afsr_ext)
#endif  /* lint */

/*
 * This routine sets the DCUCR.
 */
#if defined(lint)
/*ARGSUSED*/
uint64_t
memtest_set_dcr(uint64_t dcr)
{
	return (0);
}
#else	/* lint */
	ENTRY(memtest_set_dcr)
	rd	%asr18, %o1
	wr	%o0, %g0, %asr18
	retl
	mov	%o1, %o0		! return previous DCR value
	SET_SIZE(memtest_set_dcr)
#endif	/* lint */

/*
 * This routine sets the cache control register (LSU/DCU).
 */
#if defined(lint)
/*ARGSUSED*/
uint64_t
memtest_set_dcucr(uint64_t ccreg)
{
	return (0);
}
#else

	ENTRY(memtest_set_dcucr)
	ldxa	[%g0]ASI_LSU, %o1	! get old value
	stxa	%o0, [%g0]ASI_LSU	! store to LSU/DCU register
	flush	%g0	/* flush required after changing the IC bit */
	retl
	mov	%o1, %o0		! return old value
	SET_SIZE(memtest_set_dcucr)
#endif  /* lint */

/*
 * This routine sets the E$ Control register.
 */
#if defined(lint)
/*ARGSUSED*/
uint64_t
memtest_set_ecr(uint64_t necr)
{
	return (0);
}
#else	/* lint	*/

	ENTRY(memtest_set_ecr)
	ldxa    [%g0]ASI_EC_CTRL, %o1		! read ecache control reg
	stxa    %o0, [%g0]ASI_EC_CTRL		! write ecache control reg
	membar  #Sync
	retl
	mov	%o1, %o0			! return prev value
	SET_SIZE(memtest_set_ecr)
#endif	/* lint */

/*
 * This routine sets the e$ error enable register.
 * Bits within this register enable/disable different eror traps
 * and also allow ecc bits to be forced.
 */
#if defined(lint)
/*ARGSUSED*/
uint64_t
memtest_set_eer(uint64_t neer)
{
	return (0);
}
#else	/* lint	*/
	/*
	 * Here's a nice little hack that forces the driver to be
	 * loaded into segkmem space rather than the nucleus. In
	 * segkmem we can write text addrs when injecting parity
	 * into the instruction stream rather than data. This allows
	 * us not to have to create alternate mappings and such.
	 */
	.skip 1000000

	ENTRY(memtest_set_eer)
	GET_CPU_IMPL(%g1)		! get cpu impl
        cmp     %g1, SPITFIRE_IMPL
        bl	1f			! Not applicable to FJ processors
	 nop				! FJ cpu IMPL is < SPITFIRE_IMPL

	ldxa    [%g0]ASI_ESTATE_ERR, %o1	! read  prev value
	stxa    %o0, [%g0]ASI_ESTATE_ERR	! write ecache error enable reg
	membar  #Sync
	retl
	mov	%o1, %o0
1:
	retl
	mov	%g0, %o0
	SET_SIZE(memtest_set_eer)
#endif	/* lint */
