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
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_SYS_AUXV_SPARC_H
#define	_SYS_AUXV_SPARC_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Flags used to describe various instruction set extensions available
 * on different SPARC processors.
 *
 * [The first four are legacy descriptions.]
 */
#define	AV_SPARC_MUL32	0x00000001 /* 32x32-bit smul/umul is efficient */
#define	AV_SPARC_DIV32	0x00000002 /* 32x32-bit sdiv/udiv is efficient */
#define	AV_SPARC_FSMULD	0x00000004 /* fsmuld is efficient */
#define	AV_SPARC_V8PLUS	0x00000008 /* V9 instructions avail to 32-bit apps */
#define	AV_SPARC_POPC	0x00000010 /* popc is efficient */
#define	AV_SPARC_VIS	0x00000020 /* VIS instruction set supported */
#define	AV_SPARC_VIS2	0x00000040 /* VIS2 instruction set supported */
#define	AV_SPARC_ASI_BLK_INIT \
			0x00000080 /* ASI_BLK_INIT_xxx ASI */
#define	AV_SPARC_FMAF	0x00000100 /* Fused Multiply-Add */
#define	AV_SPARC_VIS3	0x00000400 /* VIS3 instruction set extensions */
#define	AV_SPARC_HPC	0x00000800 /* High Performance Computing instrs */
#define	AV_SPARC_RANDOM	0x00001000 /* random instruction */
#define	AV_SPARC_TRANS	0x00002000 /* transactions supported */
#define	AV_SPARC_FJFMAU	0x00004000 /* Fujitsu Unfused Multiply-Add */
#define	AV_SPARC_IMA	0x00008000 /* Integer Multiply-add */
#define	AV_SPARC_ASI_CACHE_SPARING \
			0x00010000
#define	AV_SPARC_AES	0x00020000 /* AES instructions */
#define	AV_SPARC_DES	0x00040000 /* DES instructions */
#define	AV_SPARC_KASUMI	0x00080000 /* Kasumi instructions */
#define	AV_SPARC_CAMELLIA \
			0x00100000 /* Camellia instructions */
#define	AV_SPARC_MD5	0x00200000 /* MD5 instruction */
#define	AV_SPARC_SHA1	0x00400000 /* SHA1 instruction */
#define	AV_SPARC_SHA256	0x00800000 /* SHA256 instruction */
#define	AV_SPARC_SHA512	0x01000000 /* SHA512 instruction */
#define	AV_SPARC_MPMUL	0x02000000 /* multiple precision multiply */
#define	AV_SPARC_MONT	0x04000000 /* Montgomery mult/sqr instructions */
#define	AV_SPARC_PAUSE	0x08000000 /* pause instruction */
#define	AV_SPARC_CBCOND	0x10000000 /* compare and branch instructions */
#define	AV_SPARC_CRC32C	0x20000000 /* crc32c instruction */

#define	FMT_AV_SPARC	\
	"\20" \
	"\36crc32c\35cbcond\34pause\33mont\32mpmul\31sha512"	\
	"\30sha256\27sha1\26md5\25camellia\24kasumi\23des\22aes\21cspare" \
	"\20ima\17fjfmau\16trans\15random\14hpc\13vis3\12-\11fmaf" 	\
	"\10ASIBlkInit\7vis2\6vis\5popc\4v8plus\3fsmuld\2div32\1mul32"

/*
 * compatibility defines: Obsolete
 */
#define	AV_SPARC_HWMUL_32x32	AV_SPARC_MUL32
#define	AV_SPARC_HWDIV_32x32	AV_SPARC_DIV32
#define	AV_SPARC_HWFSMULD	AV_SPARC_FSMULD

#ifdef __cplusplus
}
#endif

#endif	/* !_SYS_AUXV_SPARC_H */
