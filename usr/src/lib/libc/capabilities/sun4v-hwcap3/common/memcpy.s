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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

	.file	"memcpy.s"

/*
 * memcpy(s1, s2, len)
 *
 * Copy s2 to s1, always copy n bytes.
 * Note: this C code does not work for overlapped copies.
 *       Memmove() and bcopy() do.
 *
 * Added entry __align_cpy_1 is generally for use of the compilers.
 *
 * Fast assembler language version of the following C-program for memcpy
 * which represents the `standard' for the C-library.
 *
 *	void *
 *	memcpy(void *s, const void *s0, size_t n)
 *	{
 *		if (n != 0) {
 *		    char *s1 = s;
 *		    const char *s2 = s0;
 *		    do {
 *			*s1++ = *s2++;
 *		    } while (--n != 0);
 *		}
 *		return (s);
 *	}
 *
 *
 *
 * SPARC T4 Flow :
 *
 * if (count < SMALL_MAX) {
 *   if count < SHORTCOPY              (SHORTCOPY=3)
 *	copy bytes; exit with dst addr
 *   if src & dst aligned on word boundary but not long word boundary,
 *     copy with ldw/stw; branch to finish_up
 *   if src & dst aligned on long word boundary
 *     copy with ldx/stx; branch to finish_up
 *   if src & dst not aligned and length <= SHORTCHECK   (SHORTCHECK=14)
 *     copy bytes; exit with dst addr
 *   move enough bytes to get src to word boundary
 *   if dst now on word boundary
 * move_words:
 *     copy words; branch to finish_up
 *   if dst now on half word boundary
 *     load words, shift half words, store words; branch to finish_up
 *   if dst on byte 1
 *     load words, shift 3 bytes, store words; branch to finish_up
 *   if dst on byte 3
 *     load words, shift 1 byte, store words; branch to finish_up
 * finish_up:
 *     copy bytes; exit with dst addr
 * } else {                                         More than SMALL_MAX bytes
 *   move bytes until dst is on long word boundary
 *   if( src is on long word boundary ) {
 *     if (count < MED_MAX) {
 * finish_long:					   src/dst aligned on 8 bytes
 *       copy with ldx/stx in 8-way unrolled loop;
 *       copy final 0-63 bytes; exit with dst addr
 *     } else {				     src/dst aligned; count > MED_MAX
 *       align dst on 64 byte boundary; prefetch src data to L1 cache
 *       on loads and block store on stores in 64-byte loop.
 *       loadx8, block-init-store, block-init-store, block-store, prefetch
 *       then go to finish_long.
 *     }
 *   } else {                                   src/dst not aligned on 8 bytes
 *     if src is word aligned and count < MED_WMAX
 *       move words in 8-way unrolled loop
 *       move final 0-31 bytes; exit with dst addr
 *     if count < MED_UMAX
 *       use alignaddr/faligndata combined with ldd/std in 8-way
 *       unrolled loop to move data.
 *       go to unalign_done
 *     else
 *       setup alignaddr for faligndata instructions
 *       align dst on 64 byte boundary; prefetch src data to L1 cache
 *       loadx8, falign, block-store, prefetch loop
 *	 (only use block-init-store when src/dst on 8 byte boundaries.)
 * unalign_done:
 *       move remaining bytes for unaligned cases. exit with dst addr.
 * }
 *
 * Comment on SPARC T4 memmove and memcpy common code and block-store-init:
 *   In the man page for memmove, it specifies that copying will take place
 *   correctly between objects that overlap.  For memcpy, behavior is
 *   undefined for objects that overlap.
 *
 *   In rare cases, some multi-threaded applications may attempt to examine
 *   the copy destination buffer during the copy. Using the block-store-init
 *   instruction allows those applications to observe zeros in some
 *   cache lines of the destination buffer for narrow windows. But the
 *   the block-store-init provides memory throughput advantages for many
 *   common applications. To meet both needs, those applications which need
 *   the destination buffer to retain meaning during the copy should use
 *   memmove instead of memcpy.  The memmove version duplicates the memcpy
 *   algorithms except the memmove version does not use block-store-init
 *   in those cases where memcpy does use block-store-init. Otherwise, when
 *   memmove can determine the source and destination do not overlap,
 *   memmove shares the memcpy code.
 */

#include <sys/asm_linkage.h>
#include <sys/niagaraasi.h>
#include <sys/asi.h>
#include <sys/trap.h>

#define ASI_PNF		0x82	/* primary no fault */
#define ASI_BLK_P	0xF0	/* block primary */
#define ASI_BLK_INIT_ST_QUAD_LDD_P	0xE2

/* documented name for primary block initializing store */
#define	ASI_STBI_P	ASI_BLK_INIT_ST_QUAD_LDD_P
#define ASI_STBIMRU_P	0xF2

#define	BLOCK_SIZE	64
#define	FPRS_FEF	0x4

#define	SHORTCOPY	3
#define	SHORTCHECK	14
#define	SHORT_LONG	64	/* max copy for short longword-aligned case */
				/* must be at least 64 */
#define	SMALL_MAX	128
#define	MED_UMAX	1024	/* max copy for medium un-aligned case */
#define	MED_WMAX	1024	/* max copy for medium word-aligned case */
#define	MED_MAX		1024	/* max copy for medium longword-aligned case */

/* For T4, prefetch 20 is a strong prefetch to L1 and L2 data cache */
/* For T4, prefetch 21 is a strong prefetch to L2 data cache */

#include <sys/sun4asi.h>

	ANSI_PRAGMA_WEAK(memmove,function)
	ANSI_PRAGMA_WEAK(memcpy,function)

	ENTRY(memmove)
	cmp	%o1, %o0	! if from address is >= to use forward copy
	bgeu,pn	%ncc, .forcpy	! else use backward if ...
	sub	%o0, %o1, %o4	! get difference of two addresses
	cmp	%o2, %o4	! compare size and difference of addresses
	bleu,pn	%ncc, .forcpy	! if size is bigger, do overlapped copy
	add	%o1, %o2, %o5	! get to end of source space

	!
	! an overlapped copy that must be done "backwards"
	!
.chksize:
	cmp	%o2, 8			! less than 8 byte do byte copy
	blu,pn %ncc, 2f			! else continue

	! Now size is bigger than 8
.dbalign:
	add	%o0, %o2, %g1		! get to end of dest space
	andcc	%g1, 7, %o3		! %o3 has bytes till dst 8 bytes aligned
	bz,a,pn	%ncc, .dbbck		! if dst is not 8 byte aligned: align it
	andn	%o2, 7, %o3		! %o3 count is multiple of 8 bytes size
	sub	%o2, %o3, %o2		! update o2 with new count

1:	dec	%o5			! decrement source
	ldub	[%o5], %g1		! load one byte
	deccc	%o3			! decrement count
	bgu,pt	%ncc, 1b		! if not done keep copying
	stb	%g1, [%o5+%o4]		! store one byte into dest
	andncc	%o2, 7, %o3		! %o3 count is multiple of 8 bytes size
	bz,pn	%ncc, 2f		! if size < 8, move to byte copy

	! Now Destination is 8 byte aligned
.dbbck:
	andcc	%o5, 7, %o0		! %o0 has src offset
	bz,a,pn	%ncc, .dbcopybc		! if src is aligned to fast mem move
	sub	%o2, %o3, %o2		! Residue bytes in %o2

.cpy_dbwdbc:				! alignment of src is needed
	sub	%o2, 8, %o2		! set size one loop ahead
	sll	%o0, 3, %g1		! %g1 is left shift
	mov	64, %g5			! init %g5 to be 64
	sub	%g5, %g1, %g5		! %g5 right shift = (64 - left shift)
	sub	%o5, %o0, %o5		! align the src at 8 bytes.
	add	%o4, %o0, %o4		! increase difference between src & dst
	ldx	[%o5], %o1		! load first 8 bytes
	srlx	%o1, %g5, %o1
1:	sub	%o5, 8, %o5		! subtract 8 from src
	ldx	[%o5], %o0		! load 8 byte
	sllx	%o0, %g1, %o3		! shift loaded 8 bytes left into tmp reg
	or	%o1, %o3, %o3		! align data
	stx	%o3, [%o5+%o4]		! store 8 byte
	subcc	%o2, 8, %o2		! subtract 8 byte from size
	bg,pt	%ncc, 1b		! if size > 0 continue
	srlx	%o0, %g5, %o1		! move extra byte for the next use

	srl	%g1, 3, %o0		! retsote %o0 value for alignment
	add	%o5, %o0, %o5		! restore src alignment
	sub	%o4, %o0, %o4		! restore difference between src & dest

	ba	2f			! branch to the trailing byte copy
	add	%o2, 8, %o2		! restore size value

.dbcopybc:				! alignment of src is not needed
1:	sub	%o5, 8, %o5		! subtract from src
	ldx	[%o5], %g1		! load 8 bytes
	subcc	%o3, 8, %o3		! subtract from size
	bgu,pt	%ncc, 1b		! if size is bigger 0 continue
	stx	%g1, [%o5+%o4]		! store 8 bytes to destination

	ba	2f
	nop

.bcbyte:
1:	ldub	[%o5], %g1		! load one byte
	stb	%g1, [%o5+%o4]		! store one byte
2:	deccc	%o2			! decrement size
	bgeu,a,pt %ncc, 1b		! if size is >= 0 continue
	dec	%o5			! decrement from address

.exitbc:				! exit from backward copy
	retl
	add	%o5, %o4, %o0		! restore dest addr

	!
	! Check to see if memmove is large aligned copy
	! If so, use special version of copy that avoids
	! use of block store init
	!
.forcpy:
	cmp	%o2, SMALL_MAX		! check for not small case
	blt,pn	%ncc, .mv_short		! merge with memcpy
	mov	%o0, %g1		! save %o0
	neg	%o0, %o5
	andcc	%o5, 7, %o5		! bytes till DST 8 byte aligned
	brz,pt	%o5, .mv_dst_aligned_on_8

	! %o5 has the bytes to be written in partial store.
	sub	%o2, %o5, %o2
	sub	%o1, %o0, %o1		! %o1 gets the difference
7:					! dst aligning loop
	ldub	[%o1+%o0], %o4		! load one byte
	subcc	%o5, 1, %o5
	stb	%o4, [%o0]
	bgu,pt	%ncc, 7b
	add	%o0, 1, %o0		! advance dst
	add	%o1, %o0, %o1		! restore %o1
.mv_dst_aligned_on_8:
	andcc	%o1, 7, %o5
	brnz,pn	%o5, .src_dst_unaligned_on_8
	prefetch [%o1 + (1 * BLOCK_SIZE)], 20

.mv_src_dst_aligned_on_8:
	! check if we are copying MED_MAX or more bytes
	cmp	%o2, MED_MAX		! limit to store buffer size
	bleu,pt	%ncc, .medlong
	prefetch [%o1 + (2 * BLOCK_SIZE)], 20
/*
 * The following memmove code mimics the memcpy code for large aligned copies,
 * but does not use the ASI_STBI_P (block initializing store) performance
 * optimization. See memmove rationale section in documentation
 */
.mv_large_align8_copy:			! Src and dst share 8 byte alignment
	! align dst to 64 byte boundary
	andcc	%o0, 0x3f, %o3		! %o3 == 0 means dst is 64 byte aligned
	brz,pn	%o3, .mv_aligned_on_64
	sub	%o3, 64, %o3		! %o3 has negative bytes to move
	add	%o2, %o3, %o2		! adjust remaining count
.mv_align_to_64:
	ldx	[%o1], %o4
	add	%o1, 8, %o1		! increment src ptr
	addcc	%o3, 8, %o3
	stx	%o4, [%o0]
	brnz,pt	%o3, .mv_align_to_64
	add	%o0, 8, %o0		! increment dst ptr

.mv_aligned_on_64:
	prefetch [%o1 + (3 * BLOCK_SIZE)], 20
	andn	%o2, 0x3f, %o5		! %o5 is multiple of block size
	prefetch [%o1 + (4 * BLOCK_SIZE)], 20
	and	%o2, 0x3f, %o2		! residue bytes in %o2
	prefetch [%o1 + (5 * BLOCK_SIZE)], 20
.mv_align_loop:
	ldx	[%o1],%o4
	stx	%o4,[%o0]
	subcc	%o5, 64, %o5
	ldx	[%o1+8],%o4
	stx	%o4,[%o0+8]
	ldx	[%o1+16],%o4
	stx	%o4,[%o0+16]
	ldx	[%o1+24],%o4
	stx	%o4,[%o0+24]
	ldx	[%o1+32],%o4
	stx	%o4,[%o0+32]
	ldx	[%o1+40],%o4
	stx	%o4,[%o0+40]
	ldx	[%o1+48],%o4
	stx	%o4,[%o0+48]
	add	%o1, 64, %o1		! increment src
	ldx	[%o1-8],%o4
	add	%o0, 64, %o0
	stx	%o4,[%o0-8]
	bgt,pt	%ncc, .mv_align_loop
	prefetch [%o1 + (5 * BLOCK_SIZE)], 20
	ba	.medlong
	nop

	! END OF mv_align

	SET_SIZE(memmove)

	ENTRY(memcpy)
	ENTRY(__align_cpy_1)
	cmp	%o2, SMALL_MAX		! check for not small case
	bgeu,pn	%ncc, .medium		! go to larger cases
	mov	%o0, %g1		! save %o0
.mv_short:
	cmp	%o2, SHORTCOPY		! check for really short case
	ble,pn	%ncc, .smallfin
	or	%o0, %o1, %o4		! prepare alignment check
	andcc	%o4, 0x3, %o5		! test for alignment
	bnz,pn	%ncc, .smallunalign	! branch to word aligned case
	nop
	subcc	%o2, 7, %o2		! adjust count
	ble,pn	%ncc, .smallwordx
	andcc	%o4, 0x7, %o5		! test for long alignment
! 8 or more bytes, src and dest start on word boundary
! %o4 contains or %o0, %o1;
.smalllong:
	bnz,pn	%ncc, .smallwords	! branch to word aligned case
	cmp	%o2, SHORT_LONG-7
	bge,a	%ncc, .medl64		! if we branch
	sub	%o2,56,%o2		! adjust %o2 to -63 off count
	sub	%o1, %o0, %o1		! %o1 gets the difference
.small_long_l:
	ldx	[%o1+%o0], %o3
	subcc	%o2, 8, %o2
	add	%o0, 8, %o0
	bgu,pn	%ncc, .small_long_l	! loop until done
	stx	%o3, [%o0-8]		! write word
	addcc	%o2, 7, %o2		! restore %o2 to correct count
	bnz,pn	%ncc, .small_long_x	! check for completion
	add	%o1, %o0, %o1		! restore %o1
	retl
	mov	%g1, %o0		! restore %o0
.small_long_x:
	cmp	%o2, 4			! check for 4 or more bytes left
	blt,pn	%ncc, .smallleft3	! if not, go to finish up
	nop
	lduw	[%o1], %o3
	add	%o1, 4, %o1
	subcc	%o2, 4, %o2
	stw	%o3, [%o0]
	bnz,pn	%ncc, .smallleft3
	add	%o0, 4, %o0
	retl
	mov	%g1, %o0		! restore %o0

	.align 32
! src and dest start on word boundary; 7 or fewer bytes
.smallwordx:
	lduw	[%o1], %o3		! read word
	addcc	%o2, 3, %o2		! restore count
	bz,pt	%ncc, .smallexit
	stw	%o3, [%o0]		! write word
	deccc	%o2			! reduce count for cc test
	ldub	[%o1+4], %o3		! load one byte
	bz,pt	%ncc, .smallexit
	stb	%o3, [%o0+4]		! store one byte
	ldub	[%o1+5], %o3		! load second byte
	deccc	%o2
	bz,pt	%ncc, .smallexit
	stb	%o3, [%o0+5]		! store second byte
	ldub	[%o1+6], %o3		! load third byte
	stb	%o3, [%o0+6]		! store third byte
.smallexit:
	retl
	mov	%g1, %o0		! restore %o0


	.align 32
.smallunalign:
	cmp	%o2, SHORTCHECK
	ble,pn	%ncc, .smallrest
	andcc	%o1, 0x3, %o5		! is src word aligned
	bz,pn	%ncc, .aldst
	cmp	%o5, 2			! is src half-word aligned
	be,pt	%ncc, .s2algn
	cmp	%o5, 3			! src is byte aligned
.s1algn:ldub	[%o1], %o3		! move 1 or 3 bytes to align it
	inc	1, %o1
	stb	%o3, [%o0]		! move a byte to align src
	inc	1, %o0
	bne,pt	%ncc, .s2algn
	dec	%o2
	b	.ald			! now go align dest
	andcc	%o0, 0x3, %o5

.s2algn:lduh	[%o1], %o3		! know src is 2 byte aligned
	inc	2, %o1
	srl	%o3, 8, %o4
	stb	%o4, [%o0]		! have to do bytes,
	stb	%o3, [%o0 + 1]		! don't know dst alignment
	inc	2, %o0
	dec	2, %o2

.aldst:	andcc	%o0, 0x3, %o5		! align the destination address
.ald:	bz,pn	%ncc, .w4cp
	cmp	%o5, 2
	be,pn	%ncc, .w2cp
	cmp	%o5, 3
.w3cp:	lduw	[%o1], %o4
	inc	4, %o1
	srl	%o4, 24, %o5
	stb	%o5, [%o0]
	bne,pt	%ncc, .w1cp
	inc	%o0
	dec	1, %o2
	andn	%o2, 3, %o3		! %o3 is aligned word count
	dec	4, %o3			! avoid reading beyond tail of src
	sub	%o1, %o0, %o1		! %o1 gets the difference

1:	sll	%o4, 8, %g5		! save residual bytes
	lduw	[%o1+%o0], %o4
	deccc	4, %o3
	srl	%o4, 24, %o5		! merge with residual
	or	%o5, %g5, %g5
	st	%g5, [%o0]
	bnz,pt	%ncc, 1b
	inc	4, %o0
	sub	%o1, 3, %o1		! used one byte of last word read
	and	%o2, 3, %o2
	b	7f
	inc	4, %o2

.w1cp:	srl	%o4, 8, %o5
	sth	%o5, [%o0]
	inc	2, %o0
	dec	3, %o2
	andn	%o2, 3, %o3		! %o3 is aligned word count
	dec	4, %o3			! avoid reading beyond tail of src
	sub	%o1, %o0, %o1		! %o1 gets the difference

2:	sll	%o4, 24, %g5		! save residual bytes
	lduw	[%o1+%o0], %o4
	deccc	4, %o3
	srl	%o4, 8, %o5		! merge with residual
	or	%o5, %g5, %g5
	st	%g5, [%o0]
	bnz,pt	%ncc, 2b
	inc	4, %o0
	sub	%o1, 1, %o1		! used three bytes of last word read
	and	%o2, 3, %o2
	b	7f
	inc	4, %o2

.w2cp:	lduw	[%o1], %o4
	inc	4, %o1
	srl	%o4, 16, %o5
	sth	%o5, [%o0]
	inc	2, %o0
	dec	2, %o2
	andn	%o2, 3, %o3		! %o3 is aligned word count
	dec	4, %o3			! avoid reading beyond tail of src
	sub	%o1, %o0, %o1		! %o1 gets the difference

3:	sll	%o4, 16, %g5		! save residual bytes
	lduw	[%o1+%o0], %o4
	deccc	4, %o3
	srl	%o4, 16, %o5		! merge with residual
	or	%o5, %g5, %g5
	st	%g5, [%o0]
	bnz,pt	%ncc, 3b
	inc	4, %o0
	sub	%o1, 2, %o1		! used two bytes of last word read
	and	%o2, 3, %o2
	b	7f
	inc	4, %o2

.w4cp:	andn	%o2, 3, %o3		! %o3 is aligned word count
	sub	%o1, %o0, %o1		! %o1 gets the difference

1:	lduw	[%o1+%o0], %o4		! read from address
	deccc	4, %o3			! decrement count
	st	%o4, [%o0]		! write at destination address
	bgu,pt	%ncc, 1b
	inc	4, %o0			! increment to address
	and	%o2, 3, %o2		! number of leftover bytes, if any

	! simple finish up byte copy, works with any alignment
7:
	add	%o1, %o0, %o1		! restore %o1
.smallrest:
	tst	%o2
	bz,pt	%ncc, .smallx
	cmp	%o2, 4
	blt,pn	%ncc, .smallleft3
	nop
	sub	%o2, 3, %o2
.smallnotalign4:
	ldub	[%o1], %o3		! read byte
	subcc	%o2, 4, %o2		! reduce count by 4
	stb	%o3, [%o0]		! write byte
	ldub	[%o1+1], %o3		! repeat for total of 4 bytes
	add	%o1, 4, %o1		! advance SRC by 4
	stb	%o3, [%o0+1]
	ldub	[%o1-2], %o3
	add	%o0, 4, %o0		! advance DST by 4
	stb	%o3, [%o0-2]
	ldub	[%o1-1], %o3
	bgu,pt	%ncc, .smallnotalign4	! loop til 3 or fewer bytes remain
	stb	%o3, [%o0-1]
	addcc	%o2, 3, %o2		! restore count
	bz,pt	%ncc, .smallx
.smallleft3:				! 1, 2, or 3 bytes remain
	subcc	%o2, 1, %o2
	ldub	[%o1], %o3		! load one byte
	bz,pt	%ncc, .smallx
	stb	%o3, [%o0]		! store one byte
	ldub	[%o1+1], %o3		! load second byte
	subcc	%o2, 1, %o2
	bz,pt	%ncc, .smallx
	stb	%o3, [%o0+1]		! store second byte
	ldub	[%o1+2], %o3		! load third byte
	stb	%o3, [%o0+2]		! store third byte
.smallx:
	retl
	mov	%g1, %o0		! restore %o0

.smallfin:
	tst	%o2
	bnz,pn	%ncc, .smallleft3
	nop
	retl
	mov	%g1, %o0		! restore %o0

	.align 16
.smallwords:
	lduw	[%o1], %o3		! read word
	subcc	%o2, 8, %o2		! update count
	stw	%o3, [%o0]		! write word
	add	%o1, 8, %o1		! update SRC
	lduw	[%o1-4], %o3		! read word
	add	%o0, 8, %o0		! update DST
	bgu,pt	%ncc, .smallwords	! loop until done
	stw	%o3, [%o0-4]		! write word
	addcc	%o2, 7, %o2		! restore count
	bz,pt	%ncc, .smallexit	! check for completion
	cmp	%o2, 4			! check for 4 or more bytes left
	blt	%ncc, .smallleft3	! if not, go to finish up
	nop
	lduw	[%o1], %o3
	add	%o1, 4, %o1
	subcc	%o2, 4, %o2
	add	%o0, 4, %o0
	bnz,pn	%ncc, .smallleft3
	stw	%o3, [%o0-4]
	retl
	mov	%g1, %o0		! restore %o0

	.align 16
.medium:
	neg	%o0, %o5
	andcc	%o5, 7, %o5		! bytes till DST 8 byte aligned
	brz,pt	%o5, .dst_aligned_on_8

	! %o5 has the bytes to be written in partial store.
	sub	%o2, %o5, %o2
	sub	%o1, %o0, %o1		! %o1 gets the difference
7:					! dst aligning loop
	ldub	[%o1+%o0], %o4		! load one byte
	subcc	%o5, 1, %o5
	stb	%o4, [%o0]
	bgu,pt	%ncc, 7b
	add	%o0, 1, %o0		! advance dst
	add	%o1, %o0, %o1		! restore %o1
.dst_aligned_on_8:
	andcc	%o1, 7, %o5
	brnz,pt	%o5, .src_dst_unaligned_on_8
	prefetch [%o1 + (1 * BLOCK_SIZE)], 20

.src_dst_aligned_on_8:
	! check if we are copying MED_MAX or more bytes
	cmp	%o2, MED_MAX		! limit to store buffer size
	bgu,pn	%ncc, .large_align8_copy
	prefetch [%o1 + (2 * BLOCK_SIZE)], 20
/*
 * Special case for handling when src and dest are both long word aligned
 * and total data to move is less than MED_MAX bytes
 */
.medlong:
	subcc	%o2, 63, %o2		! adjust length to allow cc test
	ble,pn	%ncc, .medl63		! skip big loop if less than 64 bytes
	prefetch [%o1 + (3 * BLOCK_SIZE)], 20 ! into the l1, l2 cache
.medl64:
	prefetch [%o1 + (4 * BLOCK_SIZE)], 20 ! into the l1, l2 cache
	ldx	[%o1], %o4		! load
	subcc	%o2, 64, %o2		! decrement length count
	stx	%o4, [%o0]		! and store
	ldx	[%o1+8], %o3		! a block of 64 bytes
	stx	%o3, [%o0+8]
	ldx	[%o1+16], %o4
	stx	%o4, [%o0+16]
	ldx	[%o1+24], %o3
	stx	%o3, [%o0+24]
	ldx	[%o1+32], %o4		! load
	stx	%o4, [%o0+32]		! and store
	ldx	[%o1+40], %o3		! a block of 64 bytes
	add	%o1, 64, %o1		! increase src ptr by 64
	stx	%o3, [%o0+40]
	ldx	[%o1-16], %o4
	add	%o0, 64, %o0		! increase dst ptr by 64
	stx	%o4, [%o0-16]
	ldx	[%o1-8], %o3
	bgu,pt	%ncc, .medl64		! repeat if at least 64 bytes left
	stx	%o3, [%o0-8]
.medl63:
	addcc	%o2, 32, %o2		! adjust remaining count
	ble,pt	%ncc, .medl31		! to skip if 31 or fewer bytes left
	nop
	ldx	[%o1], %o4		! load
	sub	%o2, 32, %o2		! decrement length count
	stx	%o4, [%o0]		! and store
	ldx	[%o1+8], %o3		! a block of 32 bytes
	add	%o1, 32, %o1		! increase src ptr by 32
	stx	%o3, [%o0+8]
	ldx	[%o1-16], %o4
	add	%o0, 32, %o0		! increase dst ptr by 32
	stx	%o4, [%o0-16]
	ldx	[%o1-8], %o3
	stx	%o3, [%o0-8]
.medl31:
	addcc	%o2, 16, %o2		! adjust remaining count
	ble,pt	%ncc, .medl15		! skip if 15 or fewer bytes left
	nop				!
	ldx	[%o1], %o4		! load and store 16 bytes
	add	%o1, 16, %o1		! increase src ptr by 16
	stx	%o4, [%o0]		!
	sub	%o2, 16, %o2		! decrease count by 16
	ldx	[%o1-8], %o3		!
	add	%o0, 16, %o0		! increase dst ptr by 16
	stx	%o3, [%o0-8]
.medl15:
	addcc	%o2, 15, %o2		! restore count
	bz,pt	%ncc, .smallexit	! exit if finished
	cmp	%o2, 8
	blt,pt	%ncc, .medw7		! skip if 7 or fewer bytes left
	tst	%o2
	ldx	[%o1], %o4		! load 8 bytes
	add	%o1, 8, %o1		! increase src ptr by 8
	add	%o0, 8, %o0		! increase dst ptr by 8
	subcc	%o2, 8, %o2		! decrease count by 8
	bnz,pn	%ncc, .medw7
	stx	%o4, [%o0-8]		! and store 8 bytes
	retl
	mov	%g1, %o0		! restore %o0

	.align 16
.src_dst_unaligned_on_8:
	! DST is 8-byte aligned, src is not
2:
	andcc	%o1, 0x3, %o5		! test word alignment
	bnz,pt	%ncc, .unalignsetup	! branch to skip if not word aligned
	prefetch [%o1 + (2 * BLOCK_SIZE)], 20

/*
 * Handle all cases where src and dest are aligned on word
 * boundaries. Use unrolled loops for better performance.
 * This option wins over standard large data move when
 * source and destination is in cache for medium
 * to short data moves.
 */
	cmp	%o2, MED_WMAX		! limit to store buffer size
	bge,pt	%ncc, .unalignrejoin	! otherwise rejoin main loop
	prefetch [%o1 + (3 * BLOCK_SIZE)], 20

	subcc	%o2, 31, %o2		! adjust length to allow cc test
					! for end of loop
	ble,pt	%ncc, .medw31		! skip big loop if less than 16
.medw32:
	prefetch [%o1 + (4 * BLOCK_SIZE)], 20
	ld	[%o1], %o4		! move a block of 32 bytes
	sllx	%o4, 32, %o5
	ld	[%o1+4], %o4
	or	%o4, %o5, %o5
	stx	%o5, [%o0]
	subcc	%o2, 32, %o2		! decrement length count
	ld	[%o1+8], %o4
	sllx	%o4, 32, %o5
	ld	[%o1+12], %o4
	or	%o4, %o5, %o5
	stx	%o5, [%o0+8]
	add	%o1, 32, %o1		! increase src ptr by 32
	ld	[%o1-16], %o4
	sllx	%o4, 32, %o5
	ld	[%o1-12], %o4
	or	%o4, %o5, %o5
	stx	%o5, [%o0+16]
	add	%o0, 32, %o0		! increase dst ptr by 32
	ld	[%o1-8], %o4
	sllx	%o4, 32, %o5
	ld	[%o1-4], %o4
	or	%o4, %o5, %o5
	bgu,pt	%ncc, .medw32		! repeat if at least 32 bytes left
	stx	%o5, [%o0-8]
.medw31:
	addcc	%o2, 31, %o2		! restore count

	bz,pt	%ncc, .smallexit	! exit if finished
	nop
	cmp	%o2, 16
	blt,pt	%ncc, .medw15
	nop
	ld	[%o1], %o4		! move a block of 16 bytes
	sllx	%o4, 32, %o5
	subcc	%o2, 16, %o2		! decrement length count
	ld	[%o1+4], %o4
	or	%o4, %o5, %o5
	stx	%o5, [%o0]
	add	%o1, 16, %o1		! increase src ptr by 16
	ld	[%o1-8], %o4
	add	%o0, 16, %o0		! increase dst ptr by 16
	sllx	%o4, 32, %o5
	ld	[%o1-4], %o4
	or	%o4, %o5, %o5
	stx	%o5, [%o0-8]
.medw15:
	bz,pt	%ncc, .smallexit	! exit if finished
	cmp	%o2, 8
	blt,pn	%ncc, .medw7		! skip if 7 or fewer bytes left
	tst	%o2
	ld	[%o1], %o4		! load 4 bytes
	subcc	%o2, 8, %o2		! decrease count by 8
	stw	%o4, [%o0]		! and store 4 bytes
	add	%o1, 8, %o1		! increase src ptr by 8
	ld	[%o1-4], %o3		! load 4 bytes
	add	%o0, 8, %o0		! increase dst ptr by 8
	stw	%o3, [%o0-4]		! and store 4 bytes
	bz,pt	%ncc, .smallexit	! exit if finished
.medw7:					! count is ge 1, less than 8
	cmp	%o2, 4			! check for 4 bytes left
	blt,pn	%ncc, .smallleft3	! skip if 3 or fewer bytes left
	nop				!
	ld	[%o1], %o4		! load 4 bytes
	add	%o1, 4, %o1		! increase src ptr by 4
	add	%o0, 4, %o0		! increase dst ptr by 4
	subcc	%o2, 4, %o2		! decrease count by 4
	bnz	.smallleft3
	stw	%o4, [%o0-4]		! and store 4 bytes
	retl
	mov	%g1, %o0		! restore %o0

	.align 16
.large_align8_copy:			! Src and dst share 8 byte alignment
	! align dst to 64 byte boundary
	andcc	%o0, 0x3f, %o3		! %o3 == 0 means dst is 64 byte aligned
	brz,pn	%o3, .aligned_to_64
	andcc	%o0, 8, %o3		! odd long words to move?
	brz,pt	%o3, .aligned_to_16
	nop
	ldx	[%o1], %o4
	sub	%o2, 8, %o2
	add	%o1, 8, %o1		! increment src ptr
	add	%o0, 8, %o0		! increment dst ptr
	stx	%o4, [%o0-8]
.aligned_to_16:
	andcc	%o0, 16, %o3		! pair of long words to move?
	brz,pt	%o3, .aligned_to_32
	nop
	ldx	[%o1], %o4
	sub	%o2, 16, %o2
	stx	%o4, [%o0]
	add	%o1, 16, %o1		! increment src ptr
	ldx	[%o1-8], %o4
	add	%o0, 16, %o0		! increment dst ptr
	stx	%o4, [%o0-8]
.aligned_to_32:
	andcc	%o0, 32, %o3		! four long words to move?
	brz,pt	%o3, .aligned_to_64
	nop
	ldx	[%o1], %o4
	sub	%o2, 32, %o2
	stx	%o4, [%o0]
	ldx	[%o1+8], %o4
	stx	%o4, [%o0+8]
	ldx	[%o1+16], %o4
	stx	%o4, [%o0+16]
	add	%o1, 32, %o1		! increment src ptr
	ldx	[%o1-8], %o4
	add	%o0, 32, %o0		! increment dst ptr
	stx	%o4, [%o0-8]
.aligned_to_64:
!
!	Following test is included to avoid issues where existing executables
!	incorrectly call memcpy with overlapping src and dest instead of memmove
!
!	if ( (src ge dst) and (dst+len > src)) go to overlap case
!	if ( (src lt dst) and (src+len > dst)) go to overlap case
!
	cmp	%o1,%o0
	bge,pt	%ncc, 1f
	nop
!				src+len > dst?
	add	%o1, %o2, %o4
	cmp	%o4, %o0
	bgt,pt	%ncc, .mv_aligned_on_64
	nop
	ba	2f
	nop
1:
!				dst+len > src?
	add	%o0, %o2, %o4
	cmp	%o4, %o1
	bgt,pt	%ncc, .mv_aligned_on_64
	nop
2:
!	handle non-overlapped copies
!
	prefetch [%o1 + (3 * BLOCK_SIZE)], 20
	andn	%o2, 0x3f, %o5		! %o5 is multiple of block size
	prefetch [%o1 + (4 * BLOCK_SIZE)], 20
	and	%o2, 0x3f, %o2		! residue bytes in %o2
	prefetch [%o1 + (5 * BLOCK_SIZE)], 21
	sub	%o5, 64, %o5
	prefetch [%o1 + (6 * BLOCK_SIZE)], 21
	mov	%asi, %g5
	prefetch [%o1 + (7 * BLOCK_SIZE)], 21
	mov	ASI_STBI_P, %asi
	ldx	[%o1],%o4
	stxa	%o4,[%o0]%asi		! advance store of first cache line
	prefetch [%o1 + (8 * BLOCK_SIZE)], 21
	ldx	[%o1+32],%o4
	stxa	%o4,[%o0 + 32]%asi	! advance store; 32 byte L1 cache
.align_loop:
	prefetch [%o1 + (9 * BLOCK_SIZE)], 21	! several reads
	ldx	[%o1+64],%o4
	subcc	%o5, 64, %o5
	stxa	%o4,[%o0+64]%asi	! advance store
	prefetch [%o1 + (5 * BLOCK_SIZE)], 20	! several reads
	ldx	[%o1+8],%o4
	stxa	%o4,[%o0+8]%asi
	ldx	[%o1+16],%o4
	stxa	%o4,[%o0+16]%asi
	ldx	[%o1+24],%o4
	stxa	%o4,[%o0+24]%asi
	ldx	[%o1+64+32],%o4
	stxa	%o4,[%o0+64+32]%asi	! advance store
	ldx	[%o1+40],%o4
	stxa	%o4,[%o0+40]%asi
	ldx	[%o1+48],%o4
	add	%o1, 64, %o1		! increment src
	stxa	%o4,[%o0+48]%asi
	add	%o0, 64, %o0
	ldx	[%o1-8],%o4
	bgt,pt	%ncc, .align_loop
	stxa	%o4,[%o0-8]%asi

	! move remaining 48 bytes, don't repeat the advance store
	! for the first element and fifth element of final 64 bytes
	! then go to final cleanup
	
	ldx	[%o1+8], %o3		! a block of 64 bytes
	stxa	%o3, [%o0+8]%asi
	ldx	[%o1+16], %o4
	stxa	%o4, [%o0+16]%asi
	ldx	[%o1+24], %o3
	stxa	%o3, [%o0+24]%asi
	ldx	[%o1+40], %o3
	add	%o1, 64, %o1		! increase src ptr by 64
	stxa	%o3, [%o0+40]%asi
	ldx	[%o1-16], %o4
	add	%o0, 64, %o0		! increase dst ptr by 64
	stxa	%o4, [%o0-16]%asi
	ldx	[%o1-8], %o3
	stxa	%o3, [%o0-8]%asi
	sub	%o2, 63, %o2		! adjust length to allow cc test
	ba	.medl63			! in medl63
	mov	%g5, %asi

	.align 16
	! Dst is on 8 byte boundary; src is not; remaining count > SMALL_MAX
.unalignsetup:
	prefetch [%o1 + (3 * BLOCK_SIZE)], 20
.unalignrejoin:
	prefetch [%o1 + (4 * BLOCK_SIZE)], 20
	rd	%fprs, %g5		! check for unused fp
	! if fprs.fef == 0, set it.
	! Setting it when already set costs more than checking
	andcc	%g5, FPRS_FEF, %g5	! test FEF, fprs.du = fprs.dl = 0
	bz,a	%ncc, 1f
	wr	%g0, FPRS_FEF, %fprs	! fprs.fef = 1
1:
	cmp	%o2, MED_UMAX		! check for medium unaligned limit
	bge,pt	%ncc,.unalign_large
	prefetch [%o1 + (5 * BLOCK_SIZE)], 20
	andn	%o2, 0x3f, %o5		! %o5 is multiple of block size
	and	%o2, 0x3f, %o2		! residue bytes in %o2
	cmp	%o2, 8			! Insure we don't load beyond
	bgt	.unalign_adjust		! end of source buffer
	andn	%o1, 0x7, %o4		! %o4 has long word aligned src address
	add	%o2, 64, %o2		! adjust to leave loop
	sub	%o5, 64, %o5		! early if necessary
.unalign_adjust:
	alignaddr %o1, %g0, %g0		! generate %gsr
	add	%o1, %o5, %o1		! advance %o1 to after blocks
	ldd	[%o4], %d0
.unalign_loop:
	ldd	[%o4+8], %d2
	faligndata %d0, %d2, %d16
	ldd	[%o4+16], %d4
	subcc	%o5, BLOCK_SIZE, %o5
	std	%d16, [%o0]
	faligndata %d2, %d4, %d18
	ldd	[%o4+24], %d6
	std	%d18, [%o0+8]
	faligndata %d4, %d6, %d20
	ldd	[%o4+32], %d8
	std	%d20, [%o0+16]
	faligndata %d6, %d8, %d22
	ldd	[%o4+40], %d10
	std	%d22, [%o0+24]
	faligndata %d8, %d10, %d24
	ldd	[%o4+48], %d12
	std	%d24, [%o0+32]
	faligndata %d10, %d12, %d26
	ldd	[%o4+56], %d14
	add	%o4, BLOCK_SIZE, %o4
	std	%d26, [%o0+40]
	faligndata %d12, %d14, %d28
	ldd	[%o4], %d0
	std	%d28, [%o0+48]
	faligndata %d14, %d0, %d30
	std	%d30, [%o0+56]
	add	%o0, BLOCK_SIZE, %o0
	bgu,pt	%ncc, .unalign_loop
	prefetch [%o4 + (5 * BLOCK_SIZE)], 20
	ba	.unalign_done
	nop

.unalign_large:
	andcc	%o0, 0x3f, %o3		! is dst 64-byte block aligned?
	bz	%ncc, .unalignsrc
	sub	%o3, 64, %o3		! %o3 will be multiple of 8
	neg	%o3			! bytes until dest is 64 byte aligned
	sub	%o2, %o3, %o2		! update cnt with bytes to be moved
	! Move bytes according to source alignment
	andcc	%o1, 0x1, %o5
	bnz	%ncc, .unalignbyte	! check for byte alignment
	nop
	andcc	%o1, 2, %o5		! check for half word alignment
	bnz	%ncc, .unalignhalf
	nop
	! Src is word aligned
.unalignword:
	ld	[%o1], %o4		! load 4 bytes
	add	%o1, 8, %o1		! increase src ptr by 8
	stw	%o4, [%o0]		! and store 4 bytes
	subcc	%o3, 8, %o3		! decrease count by 8
	ld	[%o1-4], %o4		! load 4 bytes
	add	%o0, 8, %o0		! increase dst ptr by 8
	bnz	%ncc, .unalignword
	stw	%o4, [%o0-4]		! and store 4 bytes
	ba	.unalignsrc
	nop

	! Src is half-word aligned
.unalignhalf:
	lduh	[%o1], %o4		! load 2 bytes
	sllx	%o4, 32, %o5		! shift left
	lduw	[%o1+2], %o4
	or	%o4, %o5, %o5
	sllx	%o5, 16, %o5
	lduh	[%o1+6], %o4
	or	%o4, %o5, %o5
	stx	%o5, [%o0]
	add	%o1, 8, %o1
	subcc	%o3, 8, %o3
	bnz	%ncc, .unalignhalf
	add	%o0, 8, %o0
	ba	.unalignsrc
	nop

	! Src is Byte aligned
.unalignbyte:
	sub	%o0, %o1, %o0		! share pointer advance
.unalignbyte_loop:
	ldub	[%o1], %o4
	sllx	%o4, 56, %o5
	lduh	[%o1+1], %o4
	sllx	%o4, 40, %o4
	or	%o4, %o5, %o5
	lduh	[%o1+3], %o4
	sllx	%o4, 24, %o4
	or	%o4, %o5, %o5
	lduh	[%o1+5], %o4
	sllx	%o4,  8, %o4
	or	%o4, %o5, %o5
	ldub	[%o1+7], %o4
	or	%o4, %o5, %o5
	stx	%o5, [%o0+%o1]
	subcc	%o3, 8, %o3
	bnz	%ncc, .unalignbyte_loop
	add	%o1, 8, %o1
	add	%o0,%o1, %o0 		! restore pointer

	! Destination is now block (64 byte aligned)
.unalignsrc:
	andn	%o2, 0x3f, %o5		! %o5 is multiple of block size
	and	%o2, 0x3f, %o2		! residue bytes in %o2
	add	%o2, 64, %o2		! Insure we don't load beyond
	sub	%o5, 64, %o5		! end of source buffer

	andn	%o1, 0x7, %o4		! %o4 has long word aligned src address
	alignaddr %o1, %g0, %g0		! generate %gsr
	add	%o1, %o5, %o1		! advance %o1 to after blocks

	ldd	[%o4], %d14
	add	%o4, 8, %o4
.unalign_sloop:
	ldd	[%o4], %d16
	faligndata %d14, %d16, %d0
	ldd	[%o4+8], %d18
	faligndata %d16, %d18, %d2
	ldd	[%o4+16], %d20
	faligndata %d18, %d20, %d4
	std	%d0, [%o0]
	subcc	%o5, 64, %o5
	ldd	[%o4+24], %d22
	faligndata %d20, %d22, %d6
	std	%d2, [%o0+8]
	ldd	[%o4+32], %d24
	faligndata %d22, %d24, %d8
	std	%d4, [%o0+16]
	ldd	[%o4+40], %d26
	faligndata %d24, %d26, %d10
	std	%d6, [%o0+24]
	ldd	[%o4+48], %d28
	faligndata %d26, %d28, %d12
	std	%d8, [%o0+32]
	add	%o4, 64, %o4
	ldd	[%o4-8], %d30
	faligndata %d28, %d30, %d14
	std	%d10, [%o0+40]
	std	%d12, [%o0+48]
	add	%o0, 64, %o0
	std	%d14, [%o0-8]
	fsrc2	%d30, %d14
	bgu,pt	%ncc, .unalign_sloop
	prefetch [%o4 + (6 * BLOCK_SIZE)], 20
	membar	#Sync

.unalign_done:
	! Handle trailing bytes, 64 to 127
	! Dest long word aligned, Src not long word aligned
	cmp	%o2, 15
	bleu	%ncc, .unalign_short

	andn	%o2, 0x7, %o5		! %o5 is multiple of 8
	and	%o2, 0x7, %o2		! residue bytes in %o2
	add	%o2, 8, %o2
	sub	%o5, 8, %o5		! insure we don't load past end of src
	andn	%o1, 0x7, %o4		! %o4 has long word aligned src address
	add	%o1, %o5, %o1		! advance %o1 to after multiple of 8
	ldd	[%o4], %d0		! fetch partial word
.unalign_by8:
	ldd	[%o4+8], %d2
	add	%o4, 8, %o4
	faligndata %d0, %d2, %d16
	subcc	%o5, 8, %o5
	std	%d16, [%o0]
	fsrc2	%d2, %d0
	bgu,pt	%ncc, .unalign_by8
	add	%o0, 8, %o0

.unalign_short:
	brnz	%g5, .smallrest
	nop
	ba	.smallrest
	wr	%g5, %g0, %fprs
	SET_SIZE(memcpy)
	SET_SIZE(__align_cpy_1)
