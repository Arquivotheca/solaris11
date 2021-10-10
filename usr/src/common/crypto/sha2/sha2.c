/*
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * The basic framework for this code came from the reference
 * implementation for MD5.  That implementation is Copyright (C)
 * 1991-2, RSA Data Security, Inc. Created 1991. All rights reserved.
 *
 * License to copy and use this software is granted provided that it
 * is identified as the "RSA Data Security, Inc. MD5 Message-Digest
 * Algorithm" in all material mentioning or referencing this software
 * or this function.
 *
 * License is also granted to make and use derivative works provided
 * that such works are identified as "derived from the RSA Data
 * Security, Inc. MD5 Message-Digest Algorithm" in all material
 * mentioning or referencing the derived work.
 *
 * RSA Data Security, Inc. makes no representations concerning either
 * the merchantability of this software or the suitability of this
 * software for any particular purpose. It is provided "as is"
 * without express or implied warranty of any kind.
 *
 * These notices must be retained in any copies of any part of this
 * documentation and/or software.
 *
 * NOTE: Cleaned-up and optimized, version of SHA2, based on the FIPS 180-2
 * standard, available at
 * http://csrc.nist.gov/publications/fips/fips180-2/fips180-2.pdf
 * Not as fast as one would like -- further optimizations are encouraged
 * and appreciated.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/byteorder.h>
#define	_SHA2_IMPL
#include <sys/sha2.h>
#include <sys/sha2_consts.h>

#ifdef _KERNEL
#include <sys/cmn_err.h>
#ifdef sun4v
#include <kernel_fp_use.h>
#include <sys/archsystm.h>
#endif /* sun4v */

#else	/* userland */
#include <stdint.h>
#include <strings.h>
#include <stdlib.h>
#include <errno.h>

#ifdef sun4v
#include <sys/auxv.h>		/* getisax() */
#include <sys/auxv_SPARC.h>
#endif /* sun4v */

#pragma weak SHA256Update = SHA2Update
#pragma weak SHA384Update = SHA2Update
#pragma weak SHA512Update = SHA2Update

#pragma weak SHA256Final = SHA2Final
#pragma weak SHA384Final = SHA2Final
#pragma weak SHA512Final = SHA2Final
#endif	/* _KERNEL */


static void Encode(uint8_t *_RESTRICT_KYWD output,
    const uint32_t *_RESTRICT_KYWD input, size_t len);
static void Encode64(uint8_t *_RESTRICT_KYWD output,
    const uint64_t *_RESTRICT_KYWD input, size_t len);

#ifdef sun4v
extern void yf_sha256(SHA2_CTX *ctx, const uint8_t *blk);
extern void yf_sha512(SHA2_CTX *ctx, const uint8_t *blk);
extern void yf_sha256_multiblock(SHA2_CTX *ctx, const uint8_t *input,
    size_t nr_blocks);
extern void yf_sha512_multiblock(SHA2_CTX *ctx, const uint8_t *input,
    size_t nr_blocks);

#ifdef	HWCAP_SHA256

#define	SHA256_MULTIBLOCK(ctx, input, len) yf_sha256_multiblock(ctx, input, len)

#endif

#ifdef	HWCAP_SHA512

#define	SHA512_MULTIBLOCK(ctx, input, len) yf_sha512_multiblock(ctx, input, len)

#endif

#ifdef _KERNEL
static boolean_t yf_sha256_instruction_present(void);
#define	SHA256_MULTIBLOCK(ctx, input, len)	\
	if (yf_sha256_instruction_present()) { \
		yf_sha256_multiblock(ctx, input, len); \
	} else { \
		base_sha256_multiblock(ctx, input, len); \
	}

static void SHA256Transform(SHA256_CTX *ctx, const uint8_t input[64]);
static void base_sha256_multiblock(SHA256_CTX *ctx, const uint8_t *input,
    unsigned int input_length_in_blocks);

static boolean_t yf_sha512_instruction_present(void);

#define	SHA512_MULTIBLOCK(ctx, input, len)	\
	if (yf_sha512_instruction_present()) { \
		yf_sha512_multiblock(ctx, input, len); \
	} else { \
		base_sha512_multiblock(ctx, input, len); \
	}

static void SHA512Transform(SHA512_CTX *ctx, const uint8_t input[64]);
static void base_sha512_multiblock(SHA512_CTX *ctx, const uint8_t *input,
    unsigned int input_length_in_blocks);
#endif

#elif	defined(__amd64)
#define	SHA512_MULTIBLOCK(ctx, input, len) \
			SHA512TransformBlocks(ctx, input, len)
#define	SHA256_MULTIBLOCK(ctx, input, len) \
			SHA256TransformBlocks(ctx, input, len)

void SHA512TransformBlocks(SHA2_CTX *ctx, const void *in, size_t num);
void SHA256TransformBlocks(SHA2_CTX *ctx, const void *in, size_t num);

#else	/* generic */
static void SHA256Transform(SHA2_CTX *ctx, const uint8_t *blk);
static void SHA512Transform(SHA2_CTX *ctx, const uint8_t *blk);
#endif	/* sun4v, __amd64 */

#ifndef SHA256_MULTIBLOCK
#define	SHA256_MULTIBLOCK(ctx, input, len) \
			base_sha256_multiblock(ctx, input, len)
static void base_sha256_multiblock(SHA256_CTX *ctx, const uint8_t *input,
    unsigned int input_length_in_blocks);
#endif

#ifndef SHA512_MULTIBLOCK
#define	SHA512_MULTIBLOCK(ctx, input, len) \
			base_sha512_multiblock(ctx, input, len)
static void base_sha512_multiblock(SHA512_CTX *ctx, const uint8_t *input,
    unsigned int input_length_in_blocks);
#endif

#if	!(defined(__amd64) || (defined(sun4v) && \
	(defined(HWCAP_SHA256) || defined(HWCAP_SHA512))))
/* Ch and Maj are the basic SHA2 functions. */
#define	Ch(b, c, d)	(((b) & (c)) ^ ((~b) & (d)))
#define	Maj(b, c, d)	(((b) & (c)) ^ ((b) & (d)) ^ ((c) & (d)))

/* Rotates x right n bits. */
#define	ROTR(x, n)	\
	(((x) >> (n)) | ((x) << ((sizeof (x) * NBBY)-(n))))

/* Shift x right n bits */
#define	SHR(x, n)	((x) >> (n))

/*
 * LOAD_BIG_32() and LOAD_BIG_64()
 * sparc optimization:
 * on the sparc, we can load big endian 32-bit data easily.  note that
 * special care must be taken to ensure the address is 32-bit aligned.
 * in the interest of speed, we don't check to make sure, since
 * careful programming can guarantee this for us.
 */
#define	LOAD_BIG_32(addr)	htonl(*((uint32_t *)(addr)))
#define	LOAD_BIG_64(addr)	htonll(*((uint64_t *)(addr)))

#endif

#if	!(defined(__amd64) || (defined(sun4v) && defined(HWCAP_SHA256)))

/* SHA256 Functions */
#define	BIGSIGMA0_256(x)	(ROTR((x), 2) ^ ROTR((x), 13) ^ ROTR((x), 22))
#define	BIGSIGMA1_256(x)	(ROTR((x), 6) ^ ROTR((x), 11) ^ ROTR((x), 25))
#define	SIGMA0_256(x)		(ROTR((x), 7) ^ ROTR((x), 18) ^ SHR((x), 3))
#define	SIGMA1_256(x)		(ROTR((x), 17) ^ ROTR((x), 19) ^ SHR((x), 10))

#define	SHA256ROUND(a, b, c, d, e, f, g, h, i, w)			\
	T1 = h + BIGSIGMA1_256(e) + Ch(e, f, g) + SHA256_CONST(i) + w;	\
	d += T1;							\
	T2 = BIGSIGMA0_256(a) + Maj(a, b, c);				\
	h = T1 + T2

/*
 * SHA256Transform()
 * Transform a 64-byte block of data into the digest,
 * updating the digest with the data block.
 */
static void
SHA256Transform(SHA2_CTX *ctx, const uint8_t *blk)
{
	uint32_t a = ctx->state.s32[0];
	uint32_t b = ctx->state.s32[1];
	uint32_t c = ctx->state.s32[2];
	uint32_t d = ctx->state.s32[3];
	uint32_t e = ctx->state.s32[4];
	uint32_t f = ctx->state.s32[5];
	uint32_t g = ctx->state.s32[6];
	uint32_t h = ctx->state.s32[7];

	uint32_t w0, w1, w2, w3, w4, w5, w6, w7;
	uint32_t w8, w9, w10, w11, w12, w13, w14, w15;
	uint32_t T1, T2;

#if	defined(__sparc)
	static const uint32_t sha256_consts[] = {
		SHA256_CONST_0, SHA256_CONST_1, SHA256_CONST_2,
		SHA256_CONST_3, SHA256_CONST_4, SHA256_CONST_5,
		SHA256_CONST_6, SHA256_CONST_7, SHA256_CONST_8,
		SHA256_CONST_9, SHA256_CONST_10, SHA256_CONST_11,
		SHA256_CONST_12, SHA256_CONST_13, SHA256_CONST_14,
		SHA256_CONST_15, SHA256_CONST_16, SHA256_CONST_17,
		SHA256_CONST_18, SHA256_CONST_19, SHA256_CONST_20,
		SHA256_CONST_21, SHA256_CONST_22, SHA256_CONST_23,
		SHA256_CONST_24, SHA256_CONST_25, SHA256_CONST_26,
		SHA256_CONST_27, SHA256_CONST_28, SHA256_CONST_29,
		SHA256_CONST_30, SHA256_CONST_31, SHA256_CONST_32,
		SHA256_CONST_33, SHA256_CONST_34, SHA256_CONST_35,
		SHA256_CONST_36, SHA256_CONST_37, SHA256_CONST_38,
		SHA256_CONST_39, SHA256_CONST_40, SHA256_CONST_41,
		SHA256_CONST_42, SHA256_CONST_43, SHA256_CONST_44,
		SHA256_CONST_45, SHA256_CONST_46, SHA256_CONST_47,
		SHA256_CONST_48, SHA256_CONST_49, SHA256_CONST_50,
		SHA256_CONST_51, SHA256_CONST_52, SHA256_CONST_53,
		SHA256_CONST_54, SHA256_CONST_55, SHA256_CONST_56,
		SHA256_CONST_57, SHA256_CONST_58, SHA256_CONST_59,
		SHA256_CONST_60, SHA256_CONST_61, SHA256_CONST_62,
		SHA256_CONST_63
	};
#endif	/* __sparc */

	if ((uintptr_t)blk & 0x3) {		/* not 4-byte aligned? */
		bcopy(blk, ctx->buf_un.buf32,  sizeof (ctx->buf_un.buf32));
		blk = (uint8_t *)ctx->buf_un.buf32;
	}

	/* LINTED E_BAD_PTR_CAST_ALIGN */
	w0 =  LOAD_BIG_32(blk + 4 * 0);
	SHA256ROUND(a, b, c, d, e, f, g, h, 0, w0);
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	w1 =  LOAD_BIG_32(blk + 4 * 1);
	SHA256ROUND(h, a, b, c, d, e, f, g, 1, w1);
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	w2 =  LOAD_BIG_32(blk + 4 * 2);
	SHA256ROUND(g, h, a, b, c, d, e, f, 2, w2);
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	w3 =  LOAD_BIG_32(blk + 4 * 3);
	SHA256ROUND(f, g, h, a, b, c, d, e, 3, w3);
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	w4 =  LOAD_BIG_32(blk + 4 * 4);
	SHA256ROUND(e, f, g, h, a, b, c, d, 4, w4);
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	w5 =  LOAD_BIG_32(blk + 4 * 5);
	SHA256ROUND(d, e, f, g, h, a, b, c, 5, w5);
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	w6 =  LOAD_BIG_32(blk + 4 * 6);
	SHA256ROUND(c, d, e, f, g, h, a, b, 6, w6);
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	w7 =  LOAD_BIG_32(blk + 4 * 7);
	SHA256ROUND(b, c, d, e, f, g, h, a, 7, w7);
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	w8 =  LOAD_BIG_32(blk + 4 * 8);
	SHA256ROUND(a, b, c, d, e, f, g, h, 8, w8);
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	w9 =  LOAD_BIG_32(blk + 4 * 9);
	SHA256ROUND(h, a, b, c, d, e, f, g, 9, w9);
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	w10 =  LOAD_BIG_32(blk + 4 * 10);
	SHA256ROUND(g, h, a, b, c, d, e, f, 10, w10);
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	w11 =  LOAD_BIG_32(blk + 4 * 11);
	SHA256ROUND(f, g, h, a, b, c, d, e, 11, w11);
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	w12 =  LOAD_BIG_32(blk + 4 * 12);
	SHA256ROUND(e, f, g, h, a, b, c, d, 12, w12);
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	w13 =  LOAD_BIG_32(blk + 4 * 13);
	SHA256ROUND(d, e, f, g, h, a, b, c, 13, w13);
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	w14 =  LOAD_BIG_32(blk + 4 * 14);
	SHA256ROUND(c, d, e, f, g, h, a, b, 14, w14);
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	w15 =  LOAD_BIG_32(blk + 4 * 15);
	SHA256ROUND(b, c, d, e, f, g, h, a, 15, w15);

	w0 = SIGMA1_256(w14) + w9 + SIGMA0_256(w1) + w0;
	SHA256ROUND(a, b, c, d, e, f, g, h, 16, w0);
	w1 = SIGMA1_256(w15) + w10 + SIGMA0_256(w2) + w1;
	SHA256ROUND(h, a, b, c, d, e, f, g, 17, w1);
	w2 = SIGMA1_256(w0) + w11 + SIGMA0_256(w3) + w2;
	SHA256ROUND(g, h, a, b, c, d, e, f, 18, w2);
	w3 = SIGMA1_256(w1) + w12 + SIGMA0_256(w4) + w3;
	SHA256ROUND(f, g, h, a, b, c, d, e, 19, w3);
	w4 = SIGMA1_256(w2) + w13 + SIGMA0_256(w5) + w4;
	SHA256ROUND(e, f, g, h, a, b, c, d, 20, w4);
	w5 = SIGMA1_256(w3) + w14 + SIGMA0_256(w6) + w5;
	SHA256ROUND(d, e, f, g, h, a, b, c, 21, w5);
	w6 = SIGMA1_256(w4) + w15 + SIGMA0_256(w7) + w6;
	SHA256ROUND(c, d, e, f, g, h, a, b, 22, w6);
	w7 = SIGMA1_256(w5) + w0 + SIGMA0_256(w8) + w7;
	SHA256ROUND(b, c, d, e, f, g, h, a, 23, w7);
	w8 = SIGMA1_256(w6) + w1 + SIGMA0_256(w9) + w8;
	SHA256ROUND(a, b, c, d, e, f, g, h, 24, w8);
	w9 = SIGMA1_256(w7) + w2 + SIGMA0_256(w10) + w9;
	SHA256ROUND(h, a, b, c, d, e, f, g, 25, w9);
	w10 = SIGMA1_256(w8) + w3 + SIGMA0_256(w11) + w10;
	SHA256ROUND(g, h, a, b, c, d, e, f, 26, w10);
	w11 = SIGMA1_256(w9) + w4 + SIGMA0_256(w12) + w11;
	SHA256ROUND(f, g, h, a, b, c, d, e, 27, w11);
	w12 = SIGMA1_256(w10) + w5 + SIGMA0_256(w13) + w12;
	SHA256ROUND(e, f, g, h, a, b, c, d, 28, w12);
	w13 = SIGMA1_256(w11) + w6 + SIGMA0_256(w14) + w13;
	SHA256ROUND(d, e, f, g, h, a, b, c, 29, w13);
	w14 = SIGMA1_256(w12) + w7 + SIGMA0_256(w15) + w14;
	SHA256ROUND(c, d, e, f, g, h, a, b, 30, w14);
	w15 = SIGMA1_256(w13) + w8 + SIGMA0_256(w0) + w15;
	SHA256ROUND(b, c, d, e, f, g, h, a, 31, w15);

	w0 = SIGMA1_256(w14) + w9 + SIGMA0_256(w1) + w0;
	SHA256ROUND(a, b, c, d, e, f, g, h, 32, w0);
	w1 = SIGMA1_256(w15) + w10 + SIGMA0_256(w2) + w1;
	SHA256ROUND(h, a, b, c, d, e, f, g, 33, w1);
	w2 = SIGMA1_256(w0) + w11 + SIGMA0_256(w3) + w2;
	SHA256ROUND(g, h, a, b, c, d, e, f, 34, w2);
	w3 = SIGMA1_256(w1) + w12 + SIGMA0_256(w4) + w3;
	SHA256ROUND(f, g, h, a, b, c, d, e, 35, w3);
	w4 = SIGMA1_256(w2) + w13 + SIGMA0_256(w5) + w4;
	SHA256ROUND(e, f, g, h, a, b, c, d, 36, w4);
	w5 = SIGMA1_256(w3) + w14 + SIGMA0_256(w6) + w5;
	SHA256ROUND(d, e, f, g, h, a, b, c, 37, w5);
	w6 = SIGMA1_256(w4) + w15 + SIGMA0_256(w7) + w6;
	SHA256ROUND(c, d, e, f, g, h, a, b, 38, w6);
	w7 = SIGMA1_256(w5) + w0 + SIGMA0_256(w8) + w7;
	SHA256ROUND(b, c, d, e, f, g, h, a, 39, w7);
	w8 = SIGMA1_256(w6) + w1 + SIGMA0_256(w9) + w8;
	SHA256ROUND(a, b, c, d, e, f, g, h, 40, w8);
	w9 = SIGMA1_256(w7) + w2 + SIGMA0_256(w10) + w9;
	SHA256ROUND(h, a, b, c, d, e, f, g, 41, w9);
	w10 = SIGMA1_256(w8) + w3 + SIGMA0_256(w11) + w10;
	SHA256ROUND(g, h, a, b, c, d, e, f, 42, w10);
	w11 = SIGMA1_256(w9) + w4 + SIGMA0_256(w12) + w11;
	SHA256ROUND(f, g, h, a, b, c, d, e, 43, w11);
	w12 = SIGMA1_256(w10) + w5 + SIGMA0_256(w13) + w12;
	SHA256ROUND(e, f, g, h, a, b, c, d, 44, w12);
	w13 = SIGMA1_256(w11) + w6 + SIGMA0_256(w14) + w13;
	SHA256ROUND(d, e, f, g, h, a, b, c, 45, w13);
	w14 = SIGMA1_256(w12) + w7 + SIGMA0_256(w15) + w14;
	SHA256ROUND(c, d, e, f, g, h, a, b, 46, w14);
	w15 = SIGMA1_256(w13) + w8 + SIGMA0_256(w0) + w15;
	SHA256ROUND(b, c, d, e, f, g, h, a, 47, w15);

	w0 = SIGMA1_256(w14) + w9 + SIGMA0_256(w1) + w0;
	SHA256ROUND(a, b, c, d, e, f, g, h, 48, w0);
	w1 = SIGMA1_256(w15) + w10 + SIGMA0_256(w2) + w1;
	SHA256ROUND(h, a, b, c, d, e, f, g, 49, w1);
	w2 = SIGMA1_256(w0) + w11 + SIGMA0_256(w3) + w2;
	SHA256ROUND(g, h, a, b, c, d, e, f, 50, w2);
	w3 = SIGMA1_256(w1) + w12 + SIGMA0_256(w4) + w3;
	SHA256ROUND(f, g, h, a, b, c, d, e, 51, w3);
	w4 = SIGMA1_256(w2) + w13 + SIGMA0_256(w5) + w4;
	SHA256ROUND(e, f, g, h, a, b, c, d, 52, w4);
	w5 = SIGMA1_256(w3) + w14 + SIGMA0_256(w6) + w5;
	SHA256ROUND(d, e, f, g, h, a, b, c, 53, w5);
	w6 = SIGMA1_256(w4) + w15 + SIGMA0_256(w7) + w6;
	SHA256ROUND(c, d, e, f, g, h, a, b, 54, w6);
	w7 = SIGMA1_256(w5) + w0 + SIGMA0_256(w8) + w7;
	SHA256ROUND(b, c, d, e, f, g, h, a, 55, w7);
	w8 = SIGMA1_256(w6) + w1 + SIGMA0_256(w9) + w8;
	SHA256ROUND(a, b, c, d, e, f, g, h, 56, w8);
	w9 = SIGMA1_256(w7) + w2 + SIGMA0_256(w10) + w9;
	SHA256ROUND(h, a, b, c, d, e, f, g, 57, w9);
	w10 = SIGMA1_256(w8) + w3 + SIGMA0_256(w11) + w10;
	SHA256ROUND(g, h, a, b, c, d, e, f, 58, w10);
	w11 = SIGMA1_256(w9) + w4 + SIGMA0_256(w12) + w11;
	SHA256ROUND(f, g, h, a, b, c, d, e, 59, w11);
	w12 = SIGMA1_256(w10) + w5 + SIGMA0_256(w13) + w12;
	SHA256ROUND(e, f, g, h, a, b, c, d, 60, w12);
	w13 = SIGMA1_256(w11) + w6 + SIGMA0_256(w14) + w13;
	SHA256ROUND(d, e, f, g, h, a, b, c, 61, w13);
	w14 = SIGMA1_256(w12) + w7 + SIGMA0_256(w15) + w14;
	SHA256ROUND(c, d, e, f, g, h, a, b, 62, w14);
	w15 = SIGMA1_256(w13) + w8 + SIGMA0_256(w0) + w15;
	SHA256ROUND(b, c, d, e, f, g, h, a, 63, w15);

	ctx->state.s32[0] += a;
	ctx->state.s32[1] += b;
	ctx->state.s32[2] += c;
	ctx->state.s32[3] += d;
	ctx->state.s32[4] += e;
	ctx->state.s32[5] += f;
	ctx->state.s32[6] += g;
	ctx->state.s32[7] += h;
}


#pragma inline(base_sha256_multiblock)
static void
base_sha256_multiblock(SHA256_CTX *ctx, const uint8_t *input,
    unsigned int input_length_in_blocks)
{
	int	i;

	for (i = 0; i < input_length_in_blocks; i++)
		SHA256Transform(ctx, &input[i << 6]);

}

#endif	/* !(__amd64 || (sunv && HWCAP_SHA256)) */

#if !(defined(__amd64) || (defined(sun4v) && defined(HWCAP_SHA512)))

/* SHA384/512 Functions */
#define	BIGSIGMA0(x)	(ROTR((x), 28) ^ ROTR((x), 34) ^ ROTR((x), 39))
#define	BIGSIGMA1(x)	(ROTR((x), 14) ^ ROTR((x), 18) ^ ROTR((x), 41))
#define	SIGMA0(x)	(ROTR((x), 1) ^ ROTR((x), 8) ^ SHR((x), 7))
#define	SIGMA1(x)	(ROTR((x), 19) ^ ROTR((x), 61) ^ SHR((x), 6))
#define	SHA512ROUND(a, b, c, d, e, f, g, h, i, w)			\
	T1 = h + BIGSIGMA1(e) + Ch(e, f, g) + SHA512_CONST(i) + w;	\
	d += T1;							\
	T2 = BIGSIGMA0(a) + Maj(a, b, c);				\
	h = T1 + T2


/*
 * SHA384Transform() and SHA512Transform()
 * Transform a 128-byte block of data into the digest,
 * updating the digest with the data block.
 */
static void
SHA512Transform(SHA2_CTX *ctx, const uint8_t *blk)
{

	uint64_t a = ctx->state.s64[0];
	uint64_t b = ctx->state.s64[1];
	uint64_t c = ctx->state.s64[2];
	uint64_t d = ctx->state.s64[3];
	uint64_t e = ctx->state.s64[4];
	uint64_t f = ctx->state.s64[5];
	uint64_t g = ctx->state.s64[6];
	uint64_t h = ctx->state.s64[7];

	uint64_t w0, w1, w2, w3, w4, w5, w6, w7;
	uint64_t w8, w9, w10, w11, w12, w13, w14, w15;
	uint64_t T1, T2;

#if	defined(__sparc)
	static const uint64_t sha512_consts[] = {
		SHA512_CONST_0, SHA512_CONST_1, SHA512_CONST_2,
		SHA512_CONST_3, SHA512_CONST_4, SHA512_CONST_5,
		SHA512_CONST_6, SHA512_CONST_7, SHA512_CONST_8,
		SHA512_CONST_9, SHA512_CONST_10, SHA512_CONST_11,
		SHA512_CONST_12, SHA512_CONST_13, SHA512_CONST_14,
		SHA512_CONST_15, SHA512_CONST_16, SHA512_CONST_17,
		SHA512_CONST_18, SHA512_CONST_19, SHA512_CONST_20,
		SHA512_CONST_21, SHA512_CONST_22, SHA512_CONST_23,
		SHA512_CONST_24, SHA512_CONST_25, SHA512_CONST_26,
		SHA512_CONST_27, SHA512_CONST_28, SHA512_CONST_29,
		SHA512_CONST_30, SHA512_CONST_31, SHA512_CONST_32,
		SHA512_CONST_33, SHA512_CONST_34, SHA512_CONST_35,
		SHA512_CONST_36, SHA512_CONST_37, SHA512_CONST_38,
		SHA512_CONST_39, SHA512_CONST_40, SHA512_CONST_41,
		SHA512_CONST_42, SHA512_CONST_43, SHA512_CONST_44,
		SHA512_CONST_45, SHA512_CONST_46, SHA512_CONST_47,
		SHA512_CONST_48, SHA512_CONST_49, SHA512_CONST_50,
		SHA512_CONST_51, SHA512_CONST_52, SHA512_CONST_53,
		SHA512_CONST_54, SHA512_CONST_55, SHA512_CONST_56,
		SHA512_CONST_57, SHA512_CONST_58, SHA512_CONST_59,
		SHA512_CONST_60, SHA512_CONST_61, SHA512_CONST_62,
		SHA512_CONST_63, SHA512_CONST_64, SHA512_CONST_65,
		SHA512_CONST_66, SHA512_CONST_67, SHA512_CONST_68,
		SHA512_CONST_69, SHA512_CONST_70, SHA512_CONST_71,
		SHA512_CONST_72, SHA512_CONST_73, SHA512_CONST_74,
		SHA512_CONST_75, SHA512_CONST_76, SHA512_CONST_77,
		SHA512_CONST_78, SHA512_CONST_79
	};
#endif	/* __sparc */


	if ((uintptr_t)blk & 0x7) {		/* not 8-byte aligned? */
		bcopy(blk, ctx->buf_un.buf64,  sizeof (ctx->buf_un.buf64));
		blk = (uint8_t *)ctx->buf_un.buf64;
	}

	/* LINTED E_BAD_PTR_CAST_ALIGN */
	w0 =  LOAD_BIG_64(blk + 8 * 0);
	SHA512ROUND(a, b, c, d, e, f, g, h, 0, w0);
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	w1 =  LOAD_BIG_64(blk + 8 * 1);
	SHA512ROUND(h, a, b, c, d, e, f, g, 1, w1);
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	w2 =  LOAD_BIG_64(blk + 8 * 2);
	SHA512ROUND(g, h, a, b, c, d, e, f, 2, w2);
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	w3 =  LOAD_BIG_64(blk + 8 * 3);
	SHA512ROUND(f, g, h, a, b, c, d, e, 3, w3);
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	w4 =  LOAD_BIG_64(blk + 8 * 4);
	SHA512ROUND(e, f, g, h, a, b, c, d, 4, w4);
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	w5 =  LOAD_BIG_64(blk + 8 * 5);
	SHA512ROUND(d, e, f, g, h, a, b, c, 5, w5);
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	w6 =  LOAD_BIG_64(blk + 8 * 6);
	SHA512ROUND(c, d, e, f, g, h, a, b, 6, w6);
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	w7 =  LOAD_BIG_64(blk + 8 * 7);
	SHA512ROUND(b, c, d, e, f, g, h, a, 7, w7);
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	w8 =  LOAD_BIG_64(blk + 8 * 8);
	SHA512ROUND(a, b, c, d, e, f, g, h, 8, w8);
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	w9 =  LOAD_BIG_64(blk + 8 * 9);
	SHA512ROUND(h, a, b, c, d, e, f, g, 9, w9);
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	w10 =  LOAD_BIG_64(blk + 8 * 10);
	SHA512ROUND(g, h, a, b, c, d, e, f, 10, w10);
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	w11 =  LOAD_BIG_64(blk + 8 * 11);
	SHA512ROUND(f, g, h, a, b, c, d, e, 11, w11);
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	w12 =  LOAD_BIG_64(blk + 8 * 12);
	SHA512ROUND(e, f, g, h, a, b, c, d, 12, w12);
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	w13 =  LOAD_BIG_64(blk + 8 * 13);
	SHA512ROUND(d, e, f, g, h, a, b, c, 13, w13);
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	w14 =  LOAD_BIG_64(blk + 8 * 14);
	SHA512ROUND(c, d, e, f, g, h, a, b, 14, w14);
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	w15 =  LOAD_BIG_64(blk + 8 * 15);
	SHA512ROUND(b, c, d, e, f, g, h, a, 15, w15);

	w0 = SIGMA1(w14) + w9 + SIGMA0(w1) + w0;
	SHA512ROUND(a, b, c, d, e, f, g, h, 16, w0);
	w1 = SIGMA1(w15) + w10 + SIGMA0(w2) + w1;
	SHA512ROUND(h, a, b, c, d, e, f, g, 17, w1);
	w2 = SIGMA1(w0) + w11 + SIGMA0(w3) + w2;
	SHA512ROUND(g, h, a, b, c, d, e, f, 18, w2);
	w3 = SIGMA1(w1) + w12 + SIGMA0(w4) + w3;
	SHA512ROUND(f, g, h, a, b, c, d, e, 19, w3);
	w4 = SIGMA1(w2) + w13 + SIGMA0(w5) + w4;
	SHA512ROUND(e, f, g, h, a, b, c, d, 20, w4);
	w5 = SIGMA1(w3) + w14 + SIGMA0(w6) + w5;
	SHA512ROUND(d, e, f, g, h, a, b, c, 21, w5);
	w6 = SIGMA1(w4) + w15 + SIGMA0(w7) + w6;
	SHA512ROUND(c, d, e, f, g, h, a, b, 22, w6);
	w7 = SIGMA1(w5) + w0 + SIGMA0(w8) + w7;
	SHA512ROUND(b, c, d, e, f, g, h, a, 23, w7);
	w8 = SIGMA1(w6) + w1 + SIGMA0(w9) + w8;
	SHA512ROUND(a, b, c, d, e, f, g, h, 24, w8);
	w9 = SIGMA1(w7) + w2 + SIGMA0(w10) + w9;
	SHA512ROUND(h, a, b, c, d, e, f, g, 25, w9);
	w10 = SIGMA1(w8) + w3 + SIGMA0(w11) + w10;
	SHA512ROUND(g, h, a, b, c, d, e, f, 26, w10);
	w11 = SIGMA1(w9) + w4 + SIGMA0(w12) + w11;
	SHA512ROUND(f, g, h, a, b, c, d, e, 27, w11);
	w12 = SIGMA1(w10) + w5 + SIGMA0(w13) + w12;
	SHA512ROUND(e, f, g, h, a, b, c, d, 28, w12);
	w13 = SIGMA1(w11) + w6 + SIGMA0(w14) + w13;
	SHA512ROUND(d, e, f, g, h, a, b, c, 29, w13);
	w14 = SIGMA1(w12) + w7 + SIGMA0(w15) + w14;
	SHA512ROUND(c, d, e, f, g, h, a, b, 30, w14);
	w15 = SIGMA1(w13) + w8 + SIGMA0(w0) + w15;
	SHA512ROUND(b, c, d, e, f, g, h, a, 31, w15);

	w0 = SIGMA1(w14) + w9 + SIGMA0(w1) + w0;
	SHA512ROUND(a, b, c, d, e, f, g, h, 32, w0);
	w1 = SIGMA1(w15) + w10 + SIGMA0(w2) + w1;
	SHA512ROUND(h, a, b, c, d, e, f, g, 33, w1);
	w2 = SIGMA1(w0) + w11 + SIGMA0(w3) + w2;
	SHA512ROUND(g, h, a, b, c, d, e, f, 34, w2);
	w3 = SIGMA1(w1) + w12 + SIGMA0(w4) + w3;
	SHA512ROUND(f, g, h, a, b, c, d, e, 35, w3);
	w4 = SIGMA1(w2) + w13 + SIGMA0(w5) + w4;
	SHA512ROUND(e, f, g, h, a, b, c, d, 36, w4);
	w5 = SIGMA1(w3) + w14 + SIGMA0(w6) + w5;
	SHA512ROUND(d, e, f, g, h, a, b, c, 37, w5);
	w6 = SIGMA1(w4) + w15 + SIGMA0(w7) + w6;
	SHA512ROUND(c, d, e, f, g, h, a, b, 38, w6);
	w7 = SIGMA1(w5) + w0 + SIGMA0(w8) + w7;
	SHA512ROUND(b, c, d, e, f, g, h, a, 39, w7);
	w8 = SIGMA1(w6) + w1 + SIGMA0(w9) + w8;
	SHA512ROUND(a, b, c, d, e, f, g, h, 40, w8);
	w9 = SIGMA1(w7) + w2 + SIGMA0(w10) + w9;
	SHA512ROUND(h, a, b, c, d, e, f, g, 41, w9);
	w10 = SIGMA1(w8) + w3 + SIGMA0(w11) + w10;
	SHA512ROUND(g, h, a, b, c, d, e, f, 42, w10);
	w11 = SIGMA1(w9) + w4 + SIGMA0(w12) + w11;
	SHA512ROUND(f, g, h, a, b, c, d, e, 43, w11);
	w12 = SIGMA1(w10) + w5 + SIGMA0(w13) + w12;
	SHA512ROUND(e, f, g, h, a, b, c, d, 44, w12);
	w13 = SIGMA1(w11) + w6 + SIGMA0(w14) + w13;
	SHA512ROUND(d, e, f, g, h, a, b, c, 45, w13);
	w14 = SIGMA1(w12) + w7 + SIGMA0(w15) + w14;
	SHA512ROUND(c, d, e, f, g, h, a, b, 46, w14);
	w15 = SIGMA1(w13) + w8 + SIGMA0(w0) + w15;
	SHA512ROUND(b, c, d, e, f, g, h, a, 47, w15);

	w0 = SIGMA1(w14) + w9 + SIGMA0(w1) + w0;
	SHA512ROUND(a, b, c, d, e, f, g, h, 48, w0);
	w1 = SIGMA1(w15) + w10 + SIGMA0(w2) + w1;
	SHA512ROUND(h, a, b, c, d, e, f, g, 49, w1);
	w2 = SIGMA1(w0) + w11 + SIGMA0(w3) + w2;
	SHA512ROUND(g, h, a, b, c, d, e, f, 50, w2);
	w3 = SIGMA1(w1) + w12 + SIGMA0(w4) + w3;
	SHA512ROUND(f, g, h, a, b, c, d, e, 51, w3);
	w4 = SIGMA1(w2) + w13 + SIGMA0(w5) + w4;
	SHA512ROUND(e, f, g, h, a, b, c, d, 52, w4);
	w5 = SIGMA1(w3) + w14 + SIGMA0(w6) + w5;
	SHA512ROUND(d, e, f, g, h, a, b, c, 53, w5);
	w6 = SIGMA1(w4) + w15 + SIGMA0(w7) + w6;
	SHA512ROUND(c, d, e, f, g, h, a, b, 54, w6);
	w7 = SIGMA1(w5) + w0 + SIGMA0(w8) + w7;
	SHA512ROUND(b, c, d, e, f, g, h, a, 55, w7);
	w8 = SIGMA1(w6) + w1 + SIGMA0(w9) + w8;
	SHA512ROUND(a, b, c, d, e, f, g, h, 56, w8);
	w9 = SIGMA1(w7) + w2 + SIGMA0(w10) + w9;
	SHA512ROUND(h, a, b, c, d, e, f, g, 57, w9);
	w10 = SIGMA1(w8) + w3 + SIGMA0(w11) + w10;
	SHA512ROUND(g, h, a, b, c, d, e, f, 58, w10);
	w11 = SIGMA1(w9) + w4 + SIGMA0(w12) + w11;
	SHA512ROUND(f, g, h, a, b, c, d, e, 59, w11);
	w12 = SIGMA1(w10) + w5 + SIGMA0(w13) + w12;
	SHA512ROUND(e, f, g, h, a, b, c, d, 60, w12);
	w13 = SIGMA1(w11) + w6 + SIGMA0(w14) + w13;
	SHA512ROUND(d, e, f, g, h, a, b, c, 61, w13);
	w14 = SIGMA1(w12) + w7 + SIGMA0(w15) + w14;
	SHA512ROUND(c, d, e, f, g, h, a, b, 62, w14);
	w15 = SIGMA1(w13) + w8 + SIGMA0(w0) + w15;
	SHA512ROUND(b, c, d, e, f, g, h, a, 63, w15);

	w0 = SIGMA1(w14) + w9 + SIGMA0(w1) + w0;
	SHA512ROUND(a, b, c, d, e, f, g, h, 64, w0);
	w1 = SIGMA1(w15) + w10 + SIGMA0(w2) + w1;
	SHA512ROUND(h, a, b, c, d, e, f, g, 65, w1);
	w2 = SIGMA1(w0) + w11 + SIGMA0(w3) + w2;
	SHA512ROUND(g, h, a, b, c, d, e, f, 66, w2);
	w3 = SIGMA1(w1) + w12 + SIGMA0(w4) + w3;
	SHA512ROUND(f, g, h, a, b, c, d, e, 67, w3);
	w4 = SIGMA1(w2) + w13 + SIGMA0(w5) + w4;
	SHA512ROUND(e, f, g, h, a, b, c, d, 68, w4);
	w5 = SIGMA1(w3) + w14 + SIGMA0(w6) + w5;
	SHA512ROUND(d, e, f, g, h, a, b, c, 69, w5);
	w6 = SIGMA1(w4) + w15 + SIGMA0(w7) + w6;
	SHA512ROUND(c, d, e, f, g, h, a, b, 70, w6);
	w7 = SIGMA1(w5) + w0 + SIGMA0(w8) + w7;
	SHA512ROUND(b, c, d, e, f, g, h, a, 71, w7);
	w8 = SIGMA1(w6) + w1 + SIGMA0(w9) + w8;
	SHA512ROUND(a, b, c, d, e, f, g, h, 72, w8);
	w9 = SIGMA1(w7) + w2 + SIGMA0(w10) + w9;
	SHA512ROUND(h, a, b, c, d, e, f, g, 73, w9);
	w10 = SIGMA1(w8) + w3 + SIGMA0(w11) + w10;
	SHA512ROUND(g, h, a, b, c, d, e, f, 74, w10);
	w11 = SIGMA1(w9) + w4 + SIGMA0(w12) + w11;
	SHA512ROUND(f, g, h, a, b, c, d, e, 75, w11);
	w12 = SIGMA1(w10) + w5 + SIGMA0(w13) + w12;
	SHA512ROUND(e, f, g, h, a, b, c, d, 76, w12);
	w13 = SIGMA1(w11) + w6 + SIGMA0(w14) + w13;
	SHA512ROUND(d, e, f, g, h, a, b, c, 77, w13);
	w14 = SIGMA1(w12) + w7 + SIGMA0(w15) + w14;
	SHA512ROUND(c, d, e, f, g, h, a, b, 78, w14);
	w15 = SIGMA1(w13) + w8 + SIGMA0(w0) + w15;
	SHA512ROUND(b, c, d, e, f, g, h, a, 79, w15);

	ctx->state.s64[0] += a;
	ctx->state.s64[1] += b;
	ctx->state.s64[2] += c;
	ctx->state.s64[3] += d;
	ctx->state.s64[4] += e;
	ctx->state.s64[5] += f;
	ctx->state.s64[6] += g;
	ctx->state.s64[7] += h;

}

#pragma inline(base_sha512_multiblock)
static void
base_sha512_multiblock(SHA512_CTX *ctx, const uint8_t *input,
    unsigned int input_length_in_blocks)
{
	int	i;

	for (i = 0; i < input_length_in_blocks; i++)
		SHA512Transform(ctx, &input[i << 7]);

}

#endif	/* !(__amd64 || (sun4v && HWCAP_SHA512)) */


#pragma inline(Encode, Encode64)
/*
 * Encode(), Encode64()
 *
 * purpose: to convert a list of numbers from little endian to big endian
 *   input: input	: place to get numbers to convert
 *          len		: length of input, in bytes
 *  output: output	: place to store the converted big endian numbers
 */
static void
Encode(uint8_t *_RESTRICT_KYWD output, const uint32_t *_RESTRICT_KYWD input,
    size_t len)
{
	size_t		i, j;

#if	!defined(__i386) && !defined(__amd64)
	if (IS_P2ALIGNED(output, sizeof (uint32_t))) {
#endif
		for (i = 0, j = 0; j < len; i++, j += sizeof (uint32_t)) {
			/* LINTED E_BAD_PTR_CAST_ALIGN */
			*((uint32_t *)(output + j)) = htonl(input[i]);
		}
#if	!defined(__i386) && !defined(__amd64)
	} else {
		/* Big and little endian independent, but slower */
		for (i = 0, j = 0; j < len; i++, j += 4) {
			output[j]	= (input[i] >> 24) & 0xff;
			output[j + 1]	= (input[i] >> 16) & 0xff;
			output[j + 2]	= (input[i] >>  8) & 0xff;
			output[j + 3]	= input[i] & 0xff;
		}
	}
#endif	/* !__i386 && !__amd64 */
}

static void
Encode64(uint8_t *_RESTRICT_KYWD output, const uint64_t *_RESTRICT_KYWD input,
    size_t len)
{
	size_t		i, j;

#if	!defined(__i386) && !defined(__amd64)
	if (IS_P2ALIGNED(output, sizeof (uint64_t))) {
#endif
		for (i = 0, j = 0; j < len; i++, j += sizeof (uint64_t)) {
			/* LINTED E_BAD_PTR_CAST_ALIGN */
			*((uint64_t *)(output + j)) = htonll(input[i]);
		}
#if	!defined(__i386) && !defined(__amd64)
	} else {
		/* Big and little endian independent, but slower */
		for (i = 0, j = 0; j < len; i++, j += 8) {
			output[j]	= (input[i] >> 56) & 0xff;
			output[j + 1]	= (input[i] >> 48) & 0xff;
			output[j + 2]	= (input[i] >> 40) & 0xff;
			output[j + 3]	= (input[i] >> 32) & 0xff;
			output[j + 4]	= (input[i] >> 24) & 0xff;
			output[j + 5]	= (input[i] >> 16) & 0xff;
			output[j + 6]	= (input[i] >>  8) & 0xff;
			output[j + 7]	= input[i] & 0xff;
		}
	}
#endif	/* !__i386 && !__amd64 */
}


/*
 * SHA2Init(), SHA256Init(), SHA384Init(), SHA512Init()
 * Initialize the SHA2 context and begins and SHA2 digest operation.
 *   input: mech	: the mechanism to use (SHA256*, SHA384*, SHA512*)
 *  output: SHA2_CTX *	: the initialized context
 */
void
SHA2Init(uint64_t mech, SHA2_CTX *ctx)
{

	switch (mech) {
	case SHA256_MECH_INFO_TYPE:
	case SHA256_HMAC_MECH_INFO_TYPE:
	case SHA256_HMAC_GEN_MECH_INFO_TYPE:
		ctx->state.s32[0] = 0x6a09e667U;
		ctx->state.s32[1] = 0xbb67ae85U;
		ctx->state.s32[2] = 0x3c6ef372U;
		ctx->state.s32[3] = 0xa54ff53aU;
		ctx->state.s32[4] = 0x510e527fU;
		ctx->state.s32[5] = 0x9b05688cU;
		ctx->state.s32[6] = 0x1f83d9abU;
		ctx->state.s32[7] = 0x5be0cd19U;
		break;
	case SHA384_MECH_INFO_TYPE:
	case SHA384_HMAC_MECH_INFO_TYPE:
	case SHA384_HMAC_GEN_MECH_INFO_TYPE:
		ctx->state.s64[0] = 0xcbbb9d5dc1059ed8ULL;
		ctx->state.s64[1] = 0x629a292a367cd507ULL;
		ctx->state.s64[2] = 0x9159015a3070dd17ULL;
		ctx->state.s64[3] = 0x152fecd8f70e5939ULL;
		ctx->state.s64[4] = 0x67332667ffc00b31ULL;
		ctx->state.s64[5] = 0x8eb44a8768581511ULL;
		ctx->state.s64[6] = 0xdb0c2e0d64f98fa7ULL;
		ctx->state.s64[7] = 0x47b5481dbefa4fa4ULL;
		break;
	case SHA512_MECH_INFO_TYPE:
	case SHA512_HMAC_MECH_INFO_TYPE:
	case SHA512_HMAC_GEN_MECH_INFO_TYPE:
		ctx->state.s64[0] = 0x6a09e667f3bcc908ULL;
		ctx->state.s64[1] = 0xbb67ae8584caa73bULL;
		ctx->state.s64[2] = 0x3c6ef372fe94f82bULL;
		ctx->state.s64[3] = 0xa54ff53a5f1d36f1ULL;
		ctx->state.s64[4] = 0x510e527fade682d1ULL;
		ctx->state.s64[5] = 0x9b05688c2b3e6c1fULL;
		ctx->state.s64[6] = 0x1f83d9abfb41bd6bULL;
		ctx->state.s64[7] = 0x5be0cd19137e2179ULL;
		break;
#ifdef _KERNEL
	default:
		cmn_err(CE_PANIC,
		    "sha2_init: failed to find a supported algorithm: 0x%x",
		    (uint32_t)mech);

#endif /* _KERNEL */
	}

	ctx->algotype = (uint32_t)mech;
	ctx->count.c64[0] = ctx->count.c64[1] = 0;
}

#ifndef _KERNEL

#pragma inline(SHA256Init, SHA384Init, SHA512Init)
void
SHA256Init(SHA256_CTX *ctx)
{
	SHA2Init(SHA256, ctx);
}

void
SHA384Init(SHA384_CTX *ctx)
{
	SHA2Init(SHA384, ctx);
}

void
SHA512Init(SHA512_CTX *ctx)
{
	SHA2Init(SHA512, ctx);
}

#endif /* _KERNEL */

/*
 * SHA2Update()
 *
 * purpose: continues an sha2 digest operation, using the message block
 *          to update the context.
 *   input: SHA2_CTX *	: the context to update
 *          void *	: the message block
 *          size_t      : the length of the message block, in bytes
 *  output: void
 */
void
SHA2Update(SHA2_CTX *ctx, const void *inptr, size_t input_len)
{
	size_t		i;
	uint32_t	buf_index, remaining_len, buf_limit;
	const uint8_t	*input = inptr;
	uint32_t	algotype = ctx->algotype;
	size_t		block_count;
#if defined(sun4v) && defined(_KERNEL)
	fp_save_t	fp_save_buf;
	boolean_t	save_fp;
#endif

	/* check for noop */
	if (input_len == 0)
		return;

	if (algotype <= SHA256_HMAC_GEN_MECH_INFO_TYPE) {
		buf_limit = 64;

		/* compute number of bytes mod 64 */
		buf_index = (ctx->count.c32[1] >> 3) & 0x3F;

		/* update number of bits */
		if ((ctx->count.c32[1] += (input_len << 3)) < (input_len << 3))
			ctx->count.c32[0]++;

		ctx->count.c32[0] += (input_len >> 29);

#if defined(sun4v) && defined(_KERNEL)
		save_fp = yf_sha256_instruction_present();
#endif

	} else {
		buf_limit = 128;

		/* compute number of bytes mod 128 */
		buf_index = (ctx->count.c64[1] >> 3) & 0x7F;

		/* update number of bits */
		if ((ctx->count.c64[1] += (input_len << 3)) < (input_len << 3))
			ctx->count.c64[0]++;

		ctx->count.c64[0] += (input_len >> 29);

#if defined(sun4v) && defined(_KERNEL)
		save_fp = yf_sha512_instruction_present();
#endif
	}

	remaining_len = buf_limit - buf_index;

	/* transform as many times as possible */
	i = 0;
	if (input_len >= remaining_len) {

#if defined(sun4v) && defined(_KERNEL)
		SAVE_FP;
#endif
		/*
		 * general optimization:
		 *
		 * only do initial bcopy() and SHA2Transform() if
		 * buf_index != 0.  if buf_index == 0, we're just
		 * wasting our time doing the bcopy() since there
		 * wasn't any data left over from a previous call to
		 * SHA2Update().
		 */
		if (buf_index) {
			bcopy(input, &ctx->buf_un.buf8[buf_index],
			    remaining_len);

			if (algotype <= SHA256_HMAC_GEN_MECH_INFO_TYPE) {
				SHA256_MULTIBLOCK(ctx, ctx->buf_un.buf8, 1);
			} else {
				SHA512_MULTIBLOCK(ctx, ctx->buf_un.buf8, 1);
			}

			i = remaining_len;
		}

		if (algotype <= SHA256_HMAC_GEN_MECH_INFO_TYPE) {
			block_count = (input_len - i) >> 6;
			if (block_count > 0) {
				SHA256_MULTIBLOCK(ctx, &input[i], block_count);
				i += block_count << 6;
			}
		} else {
			block_count = (input_len - i) >> 7;
			if (block_count > 0) {
				SHA512_MULTIBLOCK(ctx, &input[i], block_count);
				i += block_count << 7;
			}
		}

#if defined(sun4v) && defined(_KERNEL)
		RESTORE_FP;
#endif
		/*
		 * general optimization:
		 * if i and input_len are the same, return now instead
		 * of calling bcopy(), since the bcopy() in this case
		 * will be an expensive noop.
		 */
		if (input_len == i)
			return;

		buf_index = 0;
	}

	/* buffer remaining input */
	bcopy(&input[i], &ctx->buf_un.buf8[buf_index], input_len - i);
}


/*
 * SHA2Final()
 *
 * purpose: ends an sha2 digest operation, finalizing the message digest and
 *          zeroing the context.
 *   input: uchar_t *	: a buffer to store the digest
 *			: The function actually uses void* because many
 *			: callers pass things other than uchar_t here.
 *          SHA2_CTX *  : the context to finalize, save, and zero
 *  output: void
 */
void
SHA2Final(void *digest, SHA2_CTX *ctx)
{
	static uint8_t	PADDING[128] = { 0x80, /* all zeros */ };
	uint8_t		bitcount_be[sizeof (ctx->count.c32)];
	uint8_t		bitcount_be64[sizeof (ctx->count.c64)];
	uint32_t	index;
	uint32_t	algotype = ctx->algotype;

	if (algotype <= SHA256_HMAC_GEN_MECH_INFO_TYPE) {
		index  = (ctx->count.c32[1] >> 3) & 0x3f;
		Encode(bitcount_be, ctx->count.c32, sizeof (bitcount_be));
		SHA2Update(ctx, PADDING, ((index < 56) ? 56 : 120) - index);
		SHA2Update(ctx, bitcount_be, sizeof (bitcount_be));
		Encode(digest, ctx->state.s32, sizeof (ctx->state.s32));

	} else {
		index  = (ctx->count.c64[1] >> 3) & 0x7f;
		Encode64(bitcount_be64, ctx->count.c64,
		    sizeof (bitcount_be64));
		SHA2Update(ctx, PADDING, ((index < 112) ? 112 : 240) - index);
		SHA2Update(ctx, bitcount_be64, sizeof (bitcount_be64));
		if (algotype <= SHA384_HMAC_GEN_MECH_INFO_TYPE) {
			ctx->state.s64[6] = ctx->state.s64[7] = 0;
			Encode64(digest, ctx->state.s64,
			    sizeof (uint64_t) * 6);
		} else
			Encode64(digest, ctx->state.s64,
			    sizeof (ctx->state.s64));
	}

	/* zeroize sensitive information */
	bzero(ctx, sizeof (*ctx));
}

#if defined(sun4v) && defined(_KERNEL)
/*
 * Return 1 if executing on Yosemite Falls with sha256 instruction,
 * otherwise 0. Cache the result, as the CPU can't change.
 *
 * Note: the userland version uses getisax().  The kernel version uses
 * global variable cpu_hwcap_flags.
 */
#pragma inline(yf_sha256_instruction_present)
static boolean_t
yf_sha256_instruction_present(void)
{
	return ((cpu_hwcap_flags & AV_SPARC_SHA256) != 0);
}

/*
 * Return 1 if executing on Yosemite Falls with sha512 instructions,
 * otherwise 0. Cache the result, as the CPU can't change.
 *
 * Note: the userland version uses getisax().  The kernel version uses
 * global variable cpu_hwcap_flags.
 */
#pragma inline(yf_sha512_instruction_present)
static boolean_t
yf_sha512_instruction_present(void)
{
	return ((cpu_hwcap_flags & AV_SPARC_SHA512) != 0);
}
#endif /* sun4v */
