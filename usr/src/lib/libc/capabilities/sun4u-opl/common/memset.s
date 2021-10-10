/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 1995, 2011, Oracle and/or its affiliates. All rights reserved.
 */

	.file	"memset.s"

/*
 * char *memset(sp, c, n)
 *
 * Set an array of n chars starting at sp to the character c.
 * Return sp.
 *
 * Fast assembler language version of the following C-program for memset
 * which represents the `standard' for the C-library.
 *
 *	void *
 *	memset(void *sp1, int c, size_t n)
 *	{
 *	    if (n != 0) {
 *		char *sp = sp1;
 *		do {
 *		    *sp++ = (char)c;
 *		} while (--n != 0);
 *	    }
 *	    return (sp1);
 *	}
 *
 * The version here is optimized for M-series -- SPARC64-VI/VII processors
 * Using ordinary stores instead of block stores allows 256 byte transfers
 * instead of 64 byte transfers between memory and processor cache.
 */

#include <sys/asm_linkage.h>
#include <sys/sun4asi.h>

	ANSI_PRAGMA_WEAK(memset,function)

#define	ALIGN8(X)	(((X) + 7) & ~7)
#define	BLK_ST_SIZE 65536


	ENTRY(memset)
	cmp	%o2, 12			! if small counts, just write bytes
	bgeu,pt	%ncc, .wrbig
	mov	%o0, %o5		! copy sp1 before using it
	nop				! align next loop on 4-word boundary
.wrchar:
	deccc	%o2			! byte clearing loop
	add	%o5, 1, %o5
	bgeu,a,pt %ncc, .wrchar
	stb	%o1, [%o5 + -1]		! we've already incremented the address

	retl
	nop				! %o0 still preserved

	.align 16
.wrbig:
	andcc	%o5, 7, %o3		! is sp1 aligned on a 8 byte bound
	bz,pt	%ncc, .blkchk		! already long aligned
	and	%o1, 0xff, %o1		! o1 is (char)c
	sub	%o3, 8, %o3		! -(bytes till long aligned)
	add	%o2, %o3, %o2		! update o2 with new count

	! Set -(%o3) bytes till sp1 long aligned
1:	stb	%o1, [%o5]		! there is at least 1 byte to set
	inccc	%o3			! byte clearing loop 
	bl,pt	%ncc, 1b
	inc	%o5 

	! Now sp1 is long aligned (sp1 is found in %o5)
.blkchk:
	sll	%o1, 8, %o3
	or	%o1, %o3, %o1		! now o1 has 2 bytes of c

	sll	%o1, 16, %o3
	or	%o1, %o3, %o1		! now o1 has 4 bytes of c
	
	sllx	%o1, 32, %o3
	set	BLK_ST_SIZE, %o4
	cmp	%o2, %o4
	bgu,pt	%ncc, .wrblkst
	or	%o1, %o3, %o1		! now o1 has 8 bytes of c
	

	and	%o2, 56, %o3		! o3 is {0, 8, 16, 24, 32, 40, 48, 56}

2:	subcc	%o3, 8, %o3		! long-word loop
	add	%o5, 8, %o5
	bgeu,a,pt %ncc, 2b
	stx	%o1, [%o5 - 8]		! already incremented the address

	! %o0 = original sp1
	! %o1 = 8 bytes of c
	! %o2 = remaining count, multiple of 64 + odd bytes
	! %o3,%o4 = unused
	! %o5 = 8-byte aligned dest, 8 bytes past beginning of memset data
	andncc	%o2, 63, %o4		! o4 has 64 byte multiple of count
	bz,pn	%ncc, .wrfinal
	nop

3:					! main loop, 64 bytes per iteration
	stx	%o1, [%o5 - 8]
	subcc	%o4, 64, %o4
	stx	%o1, [%o5]
	add	%o5, 64, %o5
	stx	%o1, [%o5 - 56]
	stx	%o1, [%o5 - 48]
	stx	%o1, [%o5 - 40]
	stx	%o1, [%o5 - 32]
	stx	%o1, [%o5 - 24]
	bgt,pt	%ncc, 3b
	stx	%o1, [%o5 - 16]

.wrfinal:				! cleanup final bytes
	andcc	%o2, 7, %o2		! o2 has the remaining bytes (<8)
	bz,pt	%ncc, 6f
	cmp	%o2, 4			! check for 4 bytes or more
	bl,pt	%ncc, 4f
	and	%o2, 3, %o2		! o2 has the remaining bytes (<4)
	stw	%o1, [%o5 - 8]
	add	%o5, 4, %o5
4:
5:
	deccc	%o2			! byte clearing loop
	add	%o5, 1, %o5
	bge,a,pt %ncc, 5b
	stb	%o1, [%o5 - 9]		! already incremented the address
6:
	retl
	nop				! %o0 still preserved

	.align 16
.wrblkst:
	! %o0 = original sp1
	! %o1 = 8 bytes of c
	! %o2 = remaining count
	! %o3,%o4 = unused
	! %o5 = 8-byte aligned dest
	!
	andcc	%o5, 63, %o3
	bz,pn	%ncc, .blalign		! if zero, dest is blk aligned
	sub	%o3, 64, %o3		! o3 is -(bytes til block aligned)
	add	%o2, %o3, %o2		! o2 is the remainder

	! Store -(%o3) bytes til dst is block aligned; dst is long word aligned
.wralg:
	stx	%o1, [%o5]
	addcc	%o3, 8, %o3
	bl,pt	%ncc, 1b
	add	%o5, 8, %o5

	! %o5 is block aligned
.blalign:
	rd	%fprs, %g1		! save fprs

	and	%o2, 63, %o3		! save bytes left after blk store
	andcc	%g1, 0x4, %g1		! fprs.du = fpsr.dl = 0
	bz,a	%ncc, 7f		! is fpsr.fef == 0?
	wr	%g0, 0x4, %fprs		! fprs.fef = 1
7:
	brnz,pn	%o1, 8f			! special case bzero
	andn	%o2, 63, %o4
	fzero	%d0
	fzero	%d2
	fzero	%d4
	fzero	%d6
	ba	9f
	fzero	%d8
8:
	! allocate 8 bytes of scratch space on stack to move %o1 to FP reg
	add	%sp, -SA(16), %sp
	stx	%o1, [%sp + STACK_BIAS + ALIGN8(MINFRAME)]
	ldd	[%sp + STACK_BIAS + ALIGN8(MINFRAME)], %d0
	fmovd	%d0, %d2
	add	%sp, SA(16), %sp	! deallocate scratch space
	fmovd	%d0, %d4
	fmovd	%d0, %d6
	fmovd	%d0, %d8
9:
	fmovd	%d0, %d10
	fmovd	%d0, %d12
	fmovd	%d0, %d14

! 	%d0-%d14 has 64 bytes of c
!	loop is aligned on 16 byte boundary
.wrblk:
	stda	%d0, [%o5]ASI_BLK_P
	subcc	%o4, 64, %o4
	bgu,pt	%ncc, .wrblk
	add	%o5, 64, %o5

	! Set remaining long words
	subcc	%o3, 8, %o3		! Can we store any long words
	blu,pn	%ncc, .wrbytes
	and	%o2, 7, %o2		! Bytes left after long words

.wrlong:
	std	%d0, [%o5]
	subcc	%o3, 8, %o3
	bgeu,pt	%ncc, .wrlong
	add	%o5, 8, %o5

.wrbytes:
	brz	%o2, .exit		! safe to check all 64-bits
.wrbyte:
	deccc	%o2
	stb	%o1, [%o5]
	bgu,pt	%ncc, .wrbyte
	add	%o5, 1, %o5
.exit:
	membar	#StoreLoad|#StoreStore
	retl				! %o0 still preserved
	wr	%g1, %g0, %fprs		! restore fprs

	SET_SIZE(memset)
