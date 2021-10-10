/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/memtest_u_asm.h>
#include <sys/cheetahregs.h>
#include <sys/cmpregs.h>

/*
 * CMP core related routines.
 */

/*
 * Simple routine to return core id: ASI_CORE_ID VA:0x10
 */
#if defined(lint)
/*ARGSUSED*/
uint64_t
cmp_get_core_id()
{
	return (0);
}
#else
        ENTRY(cmp_get_core_id)
        mov     ASI_CORE_ID, %o1
        ldxa    [%o1]ASI_CMP_PER_CORE, %o0

        retl
          nop
        SET_SIZE(cmp_get_core_id)
#endif  /* lint */

/*
 * Simple routine to return general core status register:
 *      %o0 = ASI value
 *      %o1 = ASI VA
 *      %o2 = save asi - scratch
 */
#if defined(lint)
/*ARGSUSED*/
uint64_t
cmp_get_core_reg(uint64_t asi, uint64_t va)
{
	return (0);
}
#else
        ENTRY(cmp_get_core_reg)
        mov     %asi, %o2               ! save %asi
        mov     %o0, %asi
        ldxa    [%o1]%asi, %o0
        mov     %o2, %asi               ! restore %asi

        retl
          nop
        SET_SIZE(cmp_get_core_reg)
#endif  /* lint */

/*
 * Park a core in a CMP processor.
 *
 * Parks core by writing value to CORE RUNNING REGISTER
 * with core_id == 0 or 1.
 */
#if defined(lint)
/*ARGSUSED*/
uint64_t
cmp_park_core(int core_id)
{
	return (0);
}
#else
	ENTRY(cmp_park_core)

	mov     1, %o1				! convert core id to bitmask
	sllx    %o1, %o0, %o1			! .

	mov     ASI_CORE_RUNNING_W1C, %o2	! park core by clearing bit
	stxa    %o1, [%o2]ASI_CMP_SHARED	! .
	membar #Sync

	retl
	  nop
	SET_SIZE(cmp_park_core)
#endif  /* lint */

/*
 * Unpark previously parked core in CMP processor.
 */
#if defined(lint)
/*ARGSUSED*/
uint64_t
cmp_unpark_core(int core_id)
{
	return (0);
}
#else
	ENTRY(cmp_unpark_core)

	mov     1, %o1				! convert core id to bitmask
	sllx    %o1, %o0, %o1			! .

	mov     ASI_CORE_RUNNING_W1S, %o2	! unpark core by setting bit
	stxa    %o1, [%o2]ASI_CMP_SHARED	! .
	membar #Sync

	retl
	  nop
	SET_SIZE(cmp_unpark_core)
#endif  /* lint */
