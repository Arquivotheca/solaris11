/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "mtst_asm.h"

/*
 * This is a user land version of the asmld() routine found
 * in the driver. A different instruction was chosen here
 * in order to distinguish between user and kernel errors.
 * See asmld() in memtest_asm.s for more info.
 */

#if defined(lint)

/*ARGSUSED*/
void
asmld(caddr_t addr)
{}

/*ARGSUSED*/
void
asmld_quick(caddr_t addr)
{}

#else	/* lint */

	.align	256

	ENTRY(asmld)
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
	nop
	nop
	nop
	nop
	ENTRY(asmld_quick)
	ldub	[%o0], %g0
	membar	#Sync

	retl
	nop
	SET_SIZE(asmld_quick)
	SET_SIZE(asmld)

#endif /* lint */

/*
 * This routine is similar to asmld() except that it also stores
 * the specified data to the specified virtual address. It is
 * used in the copy-back test to invoke the error and set the
 * synchronization variable at the same time.
 */
#if defined(lint)

/*ARGSUSED*/
void
asmldst(caddr_t ld_addr, caddr_t st_addr, int st_data)
{}

#else	/* lint */

	.align	256

	ENTRY(asmldst)
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
	nop
	nop
	nop
	nop
	ldub	[%o0], %g0		! leave these 2 instructions
	st	%o2, [%o1]		! next to each other
	retl
	nop				! delay slot intentionally not used
	SET_SIZE(asmldst)

#endif /* lint */

/*
 * This routine does a block load from the specified virtual address.
 */
#if defined(lint)

/*ARGSUSED*/
void
blkld(caddr_t addr)
{}

#else
	ENTRY(blkld)
	andn	%o0, 0x3F, %o0			! align addr to 64 byte

	rd	%fprs, %o1			! enable fp
	wr	%g0, FPRS_FEF, %fprs		! .
	ldda	[%o0]ASI_BLK_P, %d0		! do the block load
	membar	#Sync

	retl
	wr	%o1, 0, %fprs			! restore fprs
	SET_SIZE(blkld)

#endif  /* lint */
