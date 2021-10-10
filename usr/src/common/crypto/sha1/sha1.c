/*
 * Copyright (c) 1998, 2011, Oracle and/or its affiliates. All rights reserved.
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
 * NOTE: Cleaned-up and optimized, version of SHA1, based on the FIPS 180-1
 * standard, available at http://www.itl.nist.gov/fipspubs/fip180-1.htm
 * Not as fast as one would like -- further optimizations are encouraged
 * and appreciated.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/byteorder.h>
#include <sys/sha1.h>
#include <sys/sha1_consts.h>

#ifdef _KERNEL
#ifdef sun4v
#include <kernel_fp_use.h>
#include <sys/archsystm.h>

#elif	defined(__amd64)
#include <sys/cpuvar.h>		/* cpu_t, CPU */
#include <sys/x86_archext.h>	/* is_x86_feature(), X86FSET_SSSE3 */
#include <sys/disp.h>		/* kpreempt_disable(), kpreempt_enable */

/* Workaround for no XMM kernel thread save/restore */
#define	KPREEMPT_DISABLE	kpreempt_disable()
#define	KPREEMPT_ENABLE		kpreempt_enable()
#endif  /* sun4v, __amd64 */

#else	/* userland */
#include <stdint.h>
#include <strings.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/systeminfo.h>

#include <sys/auxv.h>		/* getisax() */
#ifdef sun4v
#include <sys/auxv_SPARC.h>

#elif	defined(__amd64)
#include <sys/auxv_386.h>	/* AV_386_SSSE3 bit */
#define	KPREEMPT_DISABLE
#define	KPREEMPT_ENABLE
#endif  /* sun4v, __amd64 */
#endif	/* _KERNEL */

static void Encode(uint8_t *_RESTRICT_KYWD output,
    const uint32_t *_RESTRICT_KYWD input, size_t len);

#if !(defined(__amd64) || (defined(sun4v) && defined(HWCAP_SHA1)))
static void SHA1Transform(SHA1_CTX *ctx, const uint8_t blk[64]);
static void base_sha1_multiblock(SHA1_CTX *ctx, const uint8_t *input,
    unsigned int input_length_in_blocks);
#endif

#ifdef __sparc

#ifdef sun4v
extern void yf_sha1(SHA1_CTX *ctx, const uint8_t *blk);
extern void yf_sha1_multiblock(SHA1_CTX *ctx, const uint8_t *input,
    size_t nr_blocks);

#ifdef	HWCAP_SHA1

#define	SHA1_MULTIBLOCK(ctx, input, len) yf_sha1_multiblock(ctx, input, len)

#elif defined(_KERNEL)
static boolean_t yf_sha1_instruction_present(void);
#define	SHA1_MULTIBLOCK(ctx, input, len)	\
	if (yf_sha1_instruction_present()) { \
		yf_sha1_multiblock(ctx, input, len); \
	} else { \
		base_sha1_multiblock(ctx, input, len); \
	}
#else	/* sun4v && !HWCAP_SHA1 && !_KERNEL */

#define	SHA1_MULTIBLOCK(ctx, input, len) base_sha1_multiblock(ctx, input, len)

#endif /* HWCAP_SHA1 */

#else

#define	SHA1_MULTIBLOCK(ctx, input, len) base_sha1_multiblock(ctx, input, len)

#endif	/* sun4v */

#ifdef VIS_SHA1

#ifdef _KERNEL
#include <sys/regset.h>
#include <sys/vis.h>
#include <sys/fpu/fpusystm.h>

/* the alignment for block stores to save fp registers */
#define	VIS_ALIGN	64

extern int sha1_savefp(kfpu_t *fpu, int svfp_ok);
extern void sha1_restorefp(kfpu_t *fpu);

uint32_t	vis_sha1_svfp_threshold = 128;
#endif /* _KERNEL */

extern void SHA1TransformVIS(uint64_t *X0, uint32_t *blk, uint32_t *cstate,
    uint64_t *VIS);
#endif /* VIS_SHA1 */

#elif	defined(__amd64)
/* AMD64 assembly language functions */
typedef void (*sha1_1block_data_order_t)(SHA1_CTX *ctx, const void *inpp);
typedef void (*sha1_block_data_order_t)(SHA1_CTX *ctx, const void *inpp,
	size_t num_blocks);
extern void sha1_block_data_order_amd64(SHA1_CTX *ctx, const void *inpp,
	size_t num_blocks);
extern void sha1_1block_data_order_ssse3(SHA1_CTX *ctx, const void *inpp);
extern void sha1_block_data_order_ssse3(SHA1_CTX *ctx, const void *inpp,
	size_t num_blocks);
extern void sha1_1block_data_order_avx(SHA1_CTX *ctx, const void *inpp);
extern void sha1_block_data_order_avx(SHA1_CTX *ctx, const void *inpp,
	size_t num_blocks);

static uint32_t ssse3_avx_instructions_present(void);

/* Processor feature flags returned by ssse3_avx_instructions_present() */
#define	SHA1_ON_GENERIC		0
#define	SHA1_ON_UNKNOWN		1
#define	SHA1_ON_SSSE3		2
#define	SHA1_ON_AVX		4

#else	/* generic */
static void SHA1Transform(SHA1_CTX *ctx, const uint8_t blk[64]);
#define	SHA1_MULTIBLOCK(ctx, input, len) base_sha1_multiblock(ctx, input, len)
#endif	/* __sparc, __amd64 */

#ifndef __amd64
/*
 * F, G, and H are the basic SHA1 functions.
 */
#define	F(b, c, d)	(((b) & (c)) | ((~b) & (d)))
#define	G(b, c, d)	((b) ^ (c) ^ (d))
#define	H(b, c, d)	(((b) & (c)) | (((b)|(c)) & (d)))

/*
 * ROTATE_LEFT rotates x left n bits.
 */
#if	defined(__GNUC__) && defined(_LP64)
static __inline__ uint64_t
ROTATE_LEFT(uint64_t value, uint32_t n)
{
	uint32_t t32 = (uint32_t)value;

	return ((t32 << n) | (t32 >> (32 - n)));
}

#else
#define	ROTATE_LEFT(x, n)	\
	(((x) << (n)) | ((x) >> ((sizeof (x) * NBBY)-(n))))
#endif	/* __GNUC && _LP64 */


/*
 * LOAD_BIG_32()
 * SPARC optimization:
 * On SPARC, we can load big endian 32-bit data easily.  Note that
 * special care must be taken to ensure the address is 32-bit aligned.
 * in the interest of speed, we don't check to make sure, since
 * careful programming can guarantee this for us.
 */
#define	LOAD_BIG_32(addr)	htonl(*((uint32_t *)(addr)))

/* W(n), n = 0 to 15, indexes into an array of 16 32-bit words: */
#ifdef W_ARRAY
#define	W(n)	w[n]
#else
#define	W(n)	w_ ## n
#endif	/* W_ARRAY */

#endif	/* !__amd64 */


/*
 * SHA1Init()
 * Initialize the SHA1 context and begins and SHA1 digest operation.
 *  output: SHA2_CTX *	: the initialized context
 */
void
SHA1Init(SHA1_CTX *ctx)
{
	ctx->count[0] = ctx->count[1] = 0;

	/*
	 * load magic initialization constants. Tell lint
	 * that these constants are unsigned by using U.
	 */

	ctx->state[0] = 0x67452301U;
	ctx->state[1] = 0xefcdab89U;
	ctx->state[2] = 0x98badcfeU;
	ctx->state[3] = 0x10325476U;
	ctx->state[4] = 0xc3d2e1f0U;
}


#ifdef VIS_SHA1
/*
 * Acceleration of SHA-1 with SPARC VIS 1.0 instructions.
 */

/*
 * SHA1Update()
 *
 * purpose: continues an sha1 digest operation, using the message block
 *          to update the context.
 *   input: SHA1_CTX *	: the context to update
 *          void *	: the message block
 *          size_t    : the length of the message block in bytes
 *  output: void
 */
void
SHA1Update(SHA1_CTX *ctx, const void *inptr, size_t input_len)
{
	/* VIS SHA-1 consts. */
	static uint64_t VIS[] = {
		0x8000000080000000ULL,
		0x0002000200020002ULL,
		0x5a8279996ed9eba1ULL,
		0x8f1bbcdcca62c1d6ULL,
		0x012389ab456789abULL};
	size_t i;
	uint32_t buf_index, buf_len;
	uint64_t X0[40], input64[8];
	const uint8_t *input = inptr;
#ifdef _KERNEL
	int usevis = 0;
#else
	int usevis = 1;
#endif /* _KERNEL */

	/* check for noop */
	if (input_len == 0)
		return;

	/* compute number of bytes mod 64 */
	buf_index = (ctx->count[1] >> 3) & 0x3F;

	/* update number of bits */
	if ((ctx->count[1] += (input_len << 3)) < (input_len << 3))
		ctx->count[0]++;

	ctx->count[0] += (input_len >> 29);

	buf_len = 64 - buf_index;

	/* transform as many times as possible */
	i = 0;
	if (input_len >= buf_len) {
#ifdef _KERNEL
		kfpu_t *fpu;
		if (fpu_exists) {
			uint8_t fpua[sizeof (kfpu_t) + GSR_SIZE + VIS_ALIGN];
			uint32_t len = (input_len + buf_index) & ~0x3f;
			int svfp_ok;

			fpu = (kfpu_t *)P2ROUNDUP((uintptr_t)fpua, 64);
			svfp_ok = ((len >= vis_sha1_svfp_threshold) ? 1 : 0);
			usevis = fpu_exists && sha1_savefp(fpu, svfp_ok);
		} else {
			usevis = 0;
		}
#endif /* _KERNEL */

		/*
		 * general optimization:
		 *
		 * only do initial bcopy() and SHA1Transform() if
		 * buf_index != 0.  if buf_index == 0, we're just
		 * wasting our time doing the bcopy() since there
		 * wasn't any data left over from a previous call to
		 * SHA1Update().
		 */
		if (buf_index) {
			bcopy(input, &ctx->buf_un.buf8[buf_index], buf_len);
			if (usevis) {
				SHA1TransformVIS(X0,
				    ctx->buf_un.buf32,
				    &ctx->state[0], VIS);
			} else {
				SHA1Transform(ctx, ctx->buf_un.buf8);
			}
			i = buf_len;
		}

		/*
		 * VIS SHA-1: uses the VIS 1.0 instructions to accelerate
		 * SHA-1 processing. This is achieved by "offloading" the
		 * computation of the message schedule (MS) to the VIS units.
		 * This allows the VIS computation of the message schedule
		 * to be performed in parallel with the standard integer
		 * processing of the remainder of the SHA-1 computation.
		 * performance by up to around 1.37X, compared to an optimized
		 * integer-only implementation.
		 *
		 * The VIS implementation of SHA1Transform has a different API
		 * to the standard integer version:
		 *
		 * void SHA1TransformVIS(
		 *	 uint64_t *, // Pointer to MS for ith block
		 *	 uint32_t *, // Pointer to ith block of message data
		 *	 uint32_t *, // Pointer to SHA state i.e ctx->state
		 *	 uint64_t *, // Pointer to various VIS constants
		 * )
		 *
		 * Note: the message data must by 4-byte aligned.
		 *
		 * Function requires VIS 1.0 support.
		 *
		 * Handling is provided to deal with arbitrary byte alingment
		 * of the input data but the performance gains are reduced
		 * for alignments other than 4-bytes.
		 */
		if (usevis) {
			if (!IS_P2ALIGNED(&input[i], sizeof (uint32_t))) {
				/*
				 * Main processing loop - input misaligned
				 */
				for (; i + 63 < input_len; i += 64) {
					bcopy(&input[i], input64, 64);
					SHA1TransformVIS(X0,
					    (uint32_t *)input64,
					    &ctx->state[0], VIS);
				}
			} else {
				/*
				 * Main processing loop - input 8-byte aligned
				 */
				for (; i + 63 < input_len; i += 64) {
					SHA1TransformVIS(X0,
					    /* LINTED E_BAD_PTR_CAST_ALIGN */
					    (uint32_t *)&input[i],
					    &ctx->state[0], VIS);
				}

			}
#ifdef _KERNEL
			sha1_restorefp(fpu);
#endif /* _KERNEL */
		} else {
			for (; i + 63 < input_len; i += 64) {
				SHA1Transform(ctx, &input[i]);
			}
		}

		/*
		 * general optimization:
		 *
		 * if i and input_len are the same, return now instead
		 * of calling bcopy(), since the bcopy() in this case
		 * will be an expensive nop.
		 */
		if (input_len == i)
			return;

		buf_index = 0;
	}

	/* buffer remaining input */
	bcopy(&input[i], &ctx->buf_un.buf8[buf_index], input_len - i);
}


#else

void
SHA1Update(SHA1_CTX *ctx, const void *inptr, size_t input_len)
{
	size_t		i;
	uint32_t	buf_index, remaining_len;
	const uint8_t	*input = inptr;
	size_t		block_count;
#if defined(sun4v) && defined(_KERNEL)
	fp_save_t	fp_save_buf;
	boolean_t	save_fp;
#endif
#ifdef __amd64
	static int	ssse3_avx_instructions = SHA1_ON_UNKNOWN;
	static sha1_1block_data_order_t	sha1_1block_data_order;
	static sha1_block_data_order_t	sha1_block_data_order;

	if (ssse3_avx_instructions == SHA1_ON_UNKNOWN) {
		ssse3_avx_instructions = ssse3_avx_instructions_present();
		if ((ssse3_avx_instructions & SHA1_ON_SSSE3) != 0) {
			if ((ssse3_avx_instructions & SHA1_ON_AVX) != 0) {
				sha1_1block_data_order =
				    sha1_1block_data_order_avx;
				sha1_block_data_order =
				    sha1_block_data_order_avx;
			} else { /* SSSE3, but no AVX */
				sha1_1block_data_order =
				    sha1_1block_data_order_ssse3;
				sha1_block_data_order =
				    sha1_block_data_order_ssse3;
			}
		}
	}
#endif

	/* check for noop */
	if (input_len == 0)
		return;

	/* compute number of bytes mod 64 */
	buf_index = (ctx->count[1] >> 3) & 0x3F;

	/* update number of bits */
	if ((ctx->count[1] += (input_len << 3)) < (input_len << 3))
		ctx->count[0]++;

	ctx->count[0] += (input_len >> 29);

	remaining_len = 64 - buf_index;

	/* transform as many times as possible */
	i = 0;
	if (input_len >= remaining_len) {
#if defined(sun4v) && defined(_KERNEL)
		save_fp = yf_sha1_instruction_present();
		SAVE_FP;
#endif

#ifdef __amd64
		if ((ssse3_avx_instructions & SHA1_ON_SSSE3) != 0) {
			if (buf_index != 0) {
				bcopy(input, &ctx->buf_un.buf8[buf_index],
				    remaining_len);
				KPREEMPT_DISABLE;
				sha1_1block_data_order(ctx, ctx->buf_un.buf8);
				KPREEMPT_ENABLE;
				i = remaining_len;
			}

			/* Process all 64-byte blocks in 1 function call */
			block_count = (input_len - i) >> 6;
			if (block_count > 0) {
				KPREEMPT_DISABLE;
				if (block_count != 1) {
					sha1_block_data_order(ctx, &input[i],
					    block_count);
				} else {
					sha1_1block_data_order(ctx, &input[i]);
				}
				KPREEMPT_ENABLE;
				i += block_count << 6;
			}

		} else { /* no SSSE3, no AVX */
			if (buf_index != 0) {
				bcopy(input, &ctx->buf_un.buf8[buf_index],
				    remaining_len);
				sha1_block_data_order_amd64(ctx,
				    ctx->buf_un.buf8, 1);
				i = remaining_len;
			}

			/* Process all 64-byte blocks in 1 function call */
			block_count = (input_len - i) >> 6;
			if (block_count > 0) {
				sha1_block_data_order_amd64(ctx, &input[i],
				    block_count);
				i += block_count << 6;
			}
		}
#else

		if (buf_index) {
			bcopy(input, &ctx->buf_un.buf8[buf_index],
			    remaining_len);

			SHA1_MULTIBLOCK(ctx, ctx->buf_un.buf8, 1);
			i = remaining_len;
		}

		block_count = (input_len - i) >> 6;
		if (block_count > 0) {
			SHA1_MULTIBLOCK(ctx, &input[i], block_count);
			i += block_count << 6;
		}
#endif

#if defined(sun4v) && defined(_KERNEL)
		RESTORE_FP;
#endif
		/*
		 * general optimization:
		 *
		 * if i and input_len are the same, return now instead
		 * of calling bcopy(), since the bcopy() in this case
		 * will be an expensive nop.
		 */

		if (input_len == i)
			return;

		buf_index = 0;
	}

	/* buffer remaining input */
	bcopy(&input[i], &ctx->buf_un.buf8[buf_index], input_len - i);
}

#endif /* VIS_SHA1 */

/*
 * SHA1Final()
 *
 * purpose: ends an sha1 digest operation, finalizing the message digest and
 *          zeroing the context.
 *   input: uchar_t *	: A buffer to store the digest.
 *			: The function actually uses void* because many
 *			: callers pass things other than uchar_t here.
 *          SHA1_CTX *  : the context to finalize, save, and zero
 *  output: void
 */
void
SHA1Final(void *digest, SHA1_CTX *ctx)
{
	static uint8_t	PADDING[64] = { 0x80, /* all zeros */ };
	uint8_t		bitcount_be[sizeof (ctx->count)];
	uint32_t	index = (ctx->count[1] >> 3) & 0x3f;

	/* store bit count, big endian */
	Encode(bitcount_be, ctx->count, sizeof (bitcount_be));

	/* pad out to 56 mod 64 */
	SHA1Update(ctx, PADDING, ((index < 56) ? 56 : 120) - index);

	/* append length (before padding) */
	SHA1Update(ctx, bitcount_be, sizeof (bitcount_be));

	/* store state in digest */
	Encode(digest, ctx->state, sizeof (ctx->state));

	/* zeroize sensitive information */
	bzero(ctx, sizeof (*ctx));
}


#if !(defined(__amd64) || (defined(sun4v) && defined(HWCAP_SHA1)))
/*
 * SHA1Transform()
 * Transform a 64-byte block of data into the digest,
 * updating the digest with the data block.
 * purpose: sha1 transformation -- updates the digest based on `block'
 *   input: SHA1_CTX *	: the context to update
 *          uint8_t [64]: the block to use to update the digest
 *  output: void
 */
#if	defined(__sparc)
static void
SHA1Transform(SHA1_CTX *ctx, const uint8_t blk[64])
{
	uint32_t a = ctx->state[0];
	uint32_t b = ctx->state[1];
	uint32_t c = ctx->state[2];
	uint32_t d = ctx->state[3];
	uint32_t e = ctx->state[4];
	/*
	 * sparc optimization:
	 *
	 * while it is somewhat counter-intuitive, on sparc, it is
	 * more efficient to place all the constants used in this
	 * function in an array and load the values out of the array
	 * than to manually load the constants.  this is because
	 * setting a register to a 32-bit value takes two ops in most
	 * cases: a `sethi' and an `or', but loading a 32-bit value
	 * from memory only takes one `ld' (or `lduw' on v9).  while
	 * this increases memory usage, the compiler can find enough
	 * other things to do while waiting to keep the pipeline does
	 * not stall.  additionally, it is likely that many of these
	 * constants are cached so that later accesses do not even go
	 * out to the bus.
	 *
	 * this array is declared `static' to keep the compiler from
	 * having to bcopy() this array onto the stack frame of
	 * SHA1Transform() each time it is called -- which is
	 * unacceptably expensive.
	 *
	 * the `const' is to ensure that callers are good citizens and
	 * do not try to munge the array.  since these routines are
	 * going to be called from inside multithreaded kernelland,
	 * this is a good safety check. -- `sha1_consts' will end up in
	 * .rodata.
	 *
	 * unfortunately, loading from an array in this manner hurts
	 * performance under Intel.  So, there is a macro,
	 * SHA1_CONST(), used in SHA1Transform(), that either expands to
	 * a reference to this array, or to the actual constant,
	 * depending on what platform this code is compiled for.
	 */

	static const uint32_t sha1_consts[] = {
		SHA1_CONST_0, SHA1_CONST_1, SHA1_CONST_2, SHA1_CONST_3
	};

	/*
	 * general optimization:
	 *
	 * use individual integers instead of using an array.  this is a
	 * win, although the amount it wins by seems to vary quite a bit.
	 */

	uint32_t	w_0, w_1, w_2,  w_3,  w_4,  w_5,  w_6,  w_7;
	uint32_t	w_8, w_9, w_10, w_11, w_12, w_13, w_14, w_15;

	/*
	 * sparc optimization:
	 *
	 * if `block' is already aligned on a 4-byte boundary, use
	 * LOAD_BIG_32() directly.  otherwise, bcopy() into a
	 * buffer that *is* aligned on a 4-byte boundary and then do
	 * the LOAD_BIG_32() on that buffer.  benchmarks have shown
	 * that using the bcopy() is better than loading the bytes
	 * individually and doing the endian-swap by hand.
	 *
	 * even though it's quite tempting to assign to do:
	 *
	 * blk = bcopy(ctx->buf_un.buf32, blk, sizeof (ctx->buf_un.buf32));
	 *
	 * and only have one set of LOAD_BIG_32()'s, the compiler
	 * *does not* like that, so please resist the urge.
	 */

	if ((uintptr_t)blk & 0x3) {		/* not 4-byte aligned? */
		bcopy(blk, ctx->buf_un.buf32,  sizeof (ctx->buf_un.buf32));

		w_15 = LOAD_BIG_32(ctx->buf_un.buf32 + 15);
		w_14 = LOAD_BIG_32(ctx->buf_un.buf32 + 14);
		w_13 = LOAD_BIG_32(ctx->buf_un.buf32 + 13);
		w_12 = LOAD_BIG_32(ctx->buf_un.buf32 + 12);
		w_11 = LOAD_BIG_32(ctx->buf_un.buf32 + 11);
		w_10 = LOAD_BIG_32(ctx->buf_un.buf32 + 10);
		w_9  = LOAD_BIG_32(ctx->buf_un.buf32 +  9);
		w_8  = LOAD_BIG_32(ctx->buf_un.buf32 +  8);
		w_7  = LOAD_BIG_32(ctx->buf_un.buf32 +  7);
		w_6  = LOAD_BIG_32(ctx->buf_un.buf32 +  6);
		w_5  = LOAD_BIG_32(ctx->buf_un.buf32 +  5);
		w_4  = LOAD_BIG_32(ctx->buf_un.buf32 +  4);
		w_3  = LOAD_BIG_32(ctx->buf_un.buf32 +  3);
		w_2  = LOAD_BIG_32(ctx->buf_un.buf32 +  2);
		w_1  = LOAD_BIG_32(ctx->buf_un.buf32 +  1);
		w_0  = LOAD_BIG_32(ctx->buf_un.buf32 +  0);
	} else {
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		w_15 = LOAD_BIG_32(blk + 60);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		w_14 = LOAD_BIG_32(blk + 56);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		w_13 = LOAD_BIG_32(blk + 52);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		w_12 = LOAD_BIG_32(blk + 48);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		w_11 = LOAD_BIG_32(blk + 44);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		w_10 = LOAD_BIG_32(blk + 40);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		w_9  = LOAD_BIG_32(blk + 36);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		w_8  = LOAD_BIG_32(blk + 32);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		w_7  = LOAD_BIG_32(blk + 28);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		w_6  = LOAD_BIG_32(blk + 24);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		w_5  = LOAD_BIG_32(blk + 20);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		w_4  = LOAD_BIG_32(blk + 16);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		w_3  = LOAD_BIG_32(blk + 12);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		w_2  = LOAD_BIG_32(blk +  8);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		w_1  = LOAD_BIG_32(blk +  4);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		w_0  = LOAD_BIG_32(blk +  0);
	}

#else	/* !defined(__sparc) */

static void /* CSTYLED */
SHA1Transform(SHA1_CTX *ctx, const uint8_t blk[64])
{
	/* CSTYLED */
	typedef uint32_t sha1word;
	sha1word	a = ctx->state[0];
	sha1word	b = ctx->state[1];
	sha1word	c = ctx->state[2];
	sha1word	d = ctx->state[3];
	sha1word	e = ctx->state[4];
#if	defined(W_ARRAY)
	sha1word	w[16];
#else	/* !defined(W_ARRAY) */
	sha1word	w_0, w_1, w_2,  w_3,  w_4,  w_5,  w_6,  w_7;
	sha1word	w_8, w_9, w_10, w_11, w_12, w_13, w_14, w_15;
#endif	/* defined(W_ARRAY) */

	W(0)  = LOAD_BIG_32((void *)(blk +  0));
	W(1)  = LOAD_BIG_32((void *)(blk +  4));
	W(2)  = LOAD_BIG_32((void *)(blk +  8));
	W(3)  = LOAD_BIG_32((void *)(blk + 12));
	W(4)  = LOAD_BIG_32((void *)(blk + 16));
	W(5)  = LOAD_BIG_32((void *)(blk + 20));
	W(6)  = LOAD_BIG_32((void *)(blk + 24));
	W(7)  = LOAD_BIG_32((void *)(blk + 28));
	W(8)  = LOAD_BIG_32((void *)(blk + 32));
	W(9)  = LOAD_BIG_32((void *)(blk + 36));
	W(10) = LOAD_BIG_32((void *)(blk + 40));
	W(11) = LOAD_BIG_32((void *)(blk + 44));
	W(12) = LOAD_BIG_32((void *)(blk + 48));
	W(13) = LOAD_BIG_32((void *)(blk + 52));
	W(14) = LOAD_BIG_32((void *)(blk + 56));
	W(15) = LOAD_BIG_32((void *)(blk + 60));

#endif	/* defined(__sparc) */

	/*
	 * general optimization:
	 *
	 * even though this approach is described in the standard as
	 * being slower algorithmically, it is 30-40% faster than the
	 * "faster" version under SPARC, because this version has more
	 * of the constraints specified at compile-time and uses fewer
	 * variables (and therefore has better register utilization)
	 * than its "speedier" brother.  (i've tried both, trust me)
	 *
	 * for either method given in the spec, there is an "assignment"
	 * phase where the following takes place:
	 *
	 *	tmp = (main_computation);
	 *	e = d; d = c; c = rotate_left(b, 30); b = a; a = tmp;
	 *
	 * we can make the algorithm go faster by not doing this work,
	 * but just pretending that `d' is now `e', etc. this works
	 * really well and obviates the need for a temporary variable.
	 * however, we still explicitly perform the rotate action,
	 * since it is cheaper on SPARC to do it once than to have to
	 * do it over and over again.
	 */

	/* round 1 */
	e = ROTATE_LEFT(a, 5) + F(b, c, d) + e + W(0) + SHA1_CONST(0); /* 0 */
	b = ROTATE_LEFT(b, 30);

	d = ROTATE_LEFT(e, 5) + F(a, b, c) + d + W(1) + SHA1_CONST(0); /* 1 */
	a = ROTATE_LEFT(a, 30);

	c = ROTATE_LEFT(d, 5) + F(e, a, b) + c + W(2) + SHA1_CONST(0); /* 2 */
	e = ROTATE_LEFT(e, 30);

	b = ROTATE_LEFT(c, 5) + F(d, e, a) + b + W(3) + SHA1_CONST(0); /* 3 */
	d = ROTATE_LEFT(d, 30);

	a = ROTATE_LEFT(b, 5) + F(c, d, e) + a + W(4) + SHA1_CONST(0); /* 4 */
	c = ROTATE_LEFT(c, 30);

	e = ROTATE_LEFT(a, 5) + F(b, c, d) + e + W(5) + SHA1_CONST(0); /* 5 */
	b = ROTATE_LEFT(b, 30);

	d = ROTATE_LEFT(e, 5) + F(a, b, c) + d + W(6) + SHA1_CONST(0); /* 6 */
	a = ROTATE_LEFT(a, 30);

	c = ROTATE_LEFT(d, 5) + F(e, a, b) + c + W(7) + SHA1_CONST(0); /* 7 */
	e = ROTATE_LEFT(e, 30);

	b = ROTATE_LEFT(c, 5) + F(d, e, a) + b + W(8) + SHA1_CONST(0); /* 8 */
	d = ROTATE_LEFT(d, 30);

	a = ROTATE_LEFT(b, 5) + F(c, d, e) + a + W(9) + SHA1_CONST(0); /* 9 */
	c = ROTATE_LEFT(c, 30);

	e = ROTATE_LEFT(a, 5) + F(b, c, d) + e + W(10) + SHA1_CONST(0); /* 10 */
	b = ROTATE_LEFT(b, 30);

	d = ROTATE_LEFT(e, 5) + F(a, b, c) + d + W(11) + SHA1_CONST(0); /* 11 */
	a = ROTATE_LEFT(a, 30);

	c = ROTATE_LEFT(d, 5) + F(e, a, b) + c + W(12) + SHA1_CONST(0); /* 12 */
	e = ROTATE_LEFT(e, 30);

	b = ROTATE_LEFT(c, 5) + F(d, e, a) + b + W(13) + SHA1_CONST(0); /* 13 */
	d = ROTATE_LEFT(d, 30);

	a = ROTATE_LEFT(b, 5) + F(c, d, e) + a + W(14) + SHA1_CONST(0); /* 14 */
	c = ROTATE_LEFT(c, 30);

	e = ROTATE_LEFT(a, 5) + F(b, c, d) + e + W(15) + SHA1_CONST(0); /* 15 */
	b = ROTATE_LEFT(b, 30);

	W(0) = ROTATE_LEFT((W(13) ^ W(8) ^ W(2) ^ W(0)), 1);		/* 16 */
	d = ROTATE_LEFT(e, 5) + F(a, b, c) + d + W(0) + SHA1_CONST(0);
	a = ROTATE_LEFT(a, 30);

	W(1) = ROTATE_LEFT((W(14) ^ W(9) ^ W(3) ^ W(1)), 1);		/* 17 */
	c = ROTATE_LEFT(d, 5) + F(e, a, b) + c + W(1) + SHA1_CONST(0);
	e = ROTATE_LEFT(e, 30);

	W(2) = ROTATE_LEFT((W(15) ^ W(10) ^ W(4) ^ W(2)), 1);	/* 18 */
	b = ROTATE_LEFT(c, 5) + F(d, e, a) + b + W(2) + SHA1_CONST(0);
	d = ROTATE_LEFT(d, 30);

	W(3) = ROTATE_LEFT((W(0) ^ W(11) ^ W(5) ^ W(3)), 1);		/* 19 */
	a = ROTATE_LEFT(b, 5) + F(c, d, e) + a + W(3) + SHA1_CONST(0);
	c = ROTATE_LEFT(c, 30);

	/* round 2 */
	W(4) = ROTATE_LEFT((W(1) ^ W(12) ^ W(6) ^ W(4)), 1);		/* 20 */
	e = ROTATE_LEFT(a, 5) + G(b, c, d) + e + W(4) + SHA1_CONST(1);
	b = ROTATE_LEFT(b, 30);

	W(5) = ROTATE_LEFT((W(2) ^ W(13) ^ W(7) ^ W(5)), 1);		/* 21 */
	d = ROTATE_LEFT(e, 5) + G(a, b, c) + d + W(5) + SHA1_CONST(1);
	a = ROTATE_LEFT(a, 30);

	W(6) = ROTATE_LEFT((W(3) ^ W(14) ^ W(8) ^ W(6)), 1);		/* 22 */
	c = ROTATE_LEFT(d, 5) + G(e, a, b) + c + W(6) + SHA1_CONST(1);
	e = ROTATE_LEFT(e, 30);

	W(7) = ROTATE_LEFT((W(4) ^ W(15) ^ W(9) ^ W(7)), 1);		/* 23 */
	b = ROTATE_LEFT(c, 5) + G(d, e, a) + b + W(7) + SHA1_CONST(1);
	d = ROTATE_LEFT(d, 30);

	W(8) = ROTATE_LEFT((W(5) ^ W(0) ^ W(10) ^ W(8)), 1);		/* 24 */
	a = ROTATE_LEFT(b, 5) + G(c, d, e) + a + W(8) + SHA1_CONST(1);
	c = ROTATE_LEFT(c, 30);

	W(9) = ROTATE_LEFT((W(6) ^ W(1) ^ W(11) ^ W(9)), 1);		/* 25 */
	e = ROTATE_LEFT(a, 5) + G(b, c, d) + e + W(9) + SHA1_CONST(1);
	b = ROTATE_LEFT(b, 30);

	W(10) = ROTATE_LEFT((W(7) ^ W(2) ^ W(12) ^ W(10)), 1);	/* 26 */
	d = ROTATE_LEFT(e, 5) + G(a, b, c) + d + W(10) + SHA1_CONST(1);
	a = ROTATE_LEFT(a, 30);

	W(11) = ROTATE_LEFT((W(8) ^ W(3) ^ W(13) ^ W(11)), 1);	/* 27 */
	c = ROTATE_LEFT(d, 5) + G(e, a, b) + c + W(11) + SHA1_CONST(1);
	e = ROTATE_LEFT(e, 30);

	W(12) = ROTATE_LEFT((W(9) ^ W(4) ^ W(14) ^ W(12)), 1);	/* 28 */
	b = ROTATE_LEFT(c, 5) + G(d, e, a) + b + W(12) + SHA1_CONST(1);
	d = ROTATE_LEFT(d, 30);

	W(13) = ROTATE_LEFT((W(10) ^ W(5) ^ W(15) ^ W(13)), 1);	/* 29 */
	a = ROTATE_LEFT(b, 5) + G(c, d, e) + a + W(13) + SHA1_CONST(1);
	c = ROTATE_LEFT(c, 30);

	W(14) = ROTATE_LEFT((W(11) ^ W(6) ^ W(0) ^ W(14)), 1);	/* 30 */
	e = ROTATE_LEFT(a, 5) + G(b, c, d) + e + W(14) + SHA1_CONST(1);
	b = ROTATE_LEFT(b, 30);

	W(15) = ROTATE_LEFT((W(12) ^ W(7) ^ W(1) ^ W(15)), 1);	/* 31 */
	d = ROTATE_LEFT(e, 5) + G(a, b, c) + d + W(15) + SHA1_CONST(1);
	a = ROTATE_LEFT(a, 30);

	W(0) = ROTATE_LEFT((W(13) ^ W(8) ^ W(2) ^ W(0)), 1);		/* 32 */
	c = ROTATE_LEFT(d, 5) + G(e, a, b) + c + W(0) + SHA1_CONST(1);
	e = ROTATE_LEFT(e, 30);

	W(1) = ROTATE_LEFT((W(14) ^ W(9) ^ W(3) ^ W(1)), 1);		/* 33 */
	b = ROTATE_LEFT(c, 5) + G(d, e, a) + b + W(1) + SHA1_CONST(1);
	d = ROTATE_LEFT(d, 30);

	W(2) = ROTATE_LEFT((W(15) ^ W(10) ^ W(4) ^ W(2)), 1);	/* 34 */
	a = ROTATE_LEFT(b, 5) + G(c, d, e) + a + W(2) + SHA1_CONST(1);
	c = ROTATE_LEFT(c, 30);

	W(3) = ROTATE_LEFT((W(0) ^ W(11) ^ W(5) ^ W(3)), 1);		/* 35 */
	e = ROTATE_LEFT(a, 5) + G(b, c, d) + e + W(3) + SHA1_CONST(1);
	b = ROTATE_LEFT(b, 30);

	W(4) = ROTATE_LEFT((W(1) ^ W(12) ^ W(6) ^ W(4)), 1);		/* 36 */
	d = ROTATE_LEFT(e, 5) + G(a, b, c) + d + W(4) + SHA1_CONST(1);
	a = ROTATE_LEFT(a, 30);

	W(5) = ROTATE_LEFT((W(2) ^ W(13) ^ W(7) ^ W(5)), 1);		/* 37 */
	c = ROTATE_LEFT(d, 5) + G(e, a, b) + c + W(5) + SHA1_CONST(1);
	e = ROTATE_LEFT(e, 30);

	W(6) = ROTATE_LEFT((W(3) ^ W(14) ^ W(8) ^ W(6)), 1);		/* 38 */
	b = ROTATE_LEFT(c, 5) + G(d, e, a) + b + W(6) + SHA1_CONST(1);
	d = ROTATE_LEFT(d, 30);

	W(7) = ROTATE_LEFT((W(4) ^ W(15) ^ W(9) ^ W(7)), 1);		/* 39 */
	a = ROTATE_LEFT(b, 5) + G(c, d, e) + a + W(7) + SHA1_CONST(1);
	c = ROTATE_LEFT(c, 30);

	/* round 3 */
	W(8) = ROTATE_LEFT((W(5) ^ W(0) ^ W(10) ^ W(8)), 1);		/* 40 */
	e = ROTATE_LEFT(a, 5) + H(b, c, d) + e + W(8) + SHA1_CONST(2);
	b = ROTATE_LEFT(b, 30);

	W(9) = ROTATE_LEFT((W(6) ^ W(1) ^ W(11) ^ W(9)), 1);		/* 41 */
	d = ROTATE_LEFT(e, 5) + H(a, b, c) + d + W(9) + SHA1_CONST(2);
	a = ROTATE_LEFT(a, 30);

	W(10) = ROTATE_LEFT((W(7) ^ W(2) ^ W(12) ^ W(10)), 1);	/* 42 */
	c = ROTATE_LEFT(d, 5) + H(e, a, b) + c + W(10) + SHA1_CONST(2);
	e = ROTATE_LEFT(e, 30);

	W(11) = ROTATE_LEFT((W(8) ^ W(3) ^ W(13) ^ W(11)), 1);	/* 43 */
	b = ROTATE_LEFT(c, 5) + H(d, e, a) + b + W(11) + SHA1_CONST(2);
	d = ROTATE_LEFT(d, 30);

	W(12) = ROTATE_LEFT((W(9) ^ W(4) ^ W(14) ^ W(12)), 1);	/* 44 */
	a = ROTATE_LEFT(b, 5) + H(c, d, e) + a + W(12) + SHA1_CONST(2);
	c = ROTATE_LEFT(c, 30);

	W(13) = ROTATE_LEFT((W(10) ^ W(5) ^ W(15) ^ W(13)), 1);	/* 45 */
	e = ROTATE_LEFT(a, 5) + H(b, c, d) + e + W(13) + SHA1_CONST(2);
	b = ROTATE_LEFT(b, 30);

	W(14) = ROTATE_LEFT((W(11) ^ W(6) ^ W(0) ^ W(14)), 1);	/* 46 */
	d = ROTATE_LEFT(e, 5) + H(a, b, c) + d + W(14) + SHA1_CONST(2);
	a = ROTATE_LEFT(a, 30);

	W(15) = ROTATE_LEFT((W(12) ^ W(7) ^ W(1) ^ W(15)), 1);	/* 47 */
	c = ROTATE_LEFT(d, 5) + H(e, a, b) + c + W(15) + SHA1_CONST(2);
	e = ROTATE_LEFT(e, 30);

	W(0) = ROTATE_LEFT((W(13) ^ W(8) ^ W(2) ^ W(0)), 1);		/* 48 */
	b = ROTATE_LEFT(c, 5) + H(d, e, a) + b + W(0) + SHA1_CONST(2);
	d = ROTATE_LEFT(d, 30);

	W(1) = ROTATE_LEFT((W(14) ^ W(9) ^ W(3) ^ W(1)), 1);		/* 49 */
	a = ROTATE_LEFT(b, 5) + H(c, d, e) + a + W(1) + SHA1_CONST(2);
	c = ROTATE_LEFT(c, 30);

	W(2) = ROTATE_LEFT((W(15) ^ W(10) ^ W(4) ^ W(2)), 1);	/* 50 */
	e = ROTATE_LEFT(a, 5) + H(b, c, d) + e + W(2) + SHA1_CONST(2);
	b = ROTATE_LEFT(b, 30);

	W(3) = ROTATE_LEFT((W(0) ^ W(11) ^ W(5) ^ W(3)), 1);		/* 51 */
	d = ROTATE_LEFT(e, 5) + H(a, b, c) + d + W(3) + SHA1_CONST(2);
	a = ROTATE_LEFT(a, 30);

	W(4) = ROTATE_LEFT((W(1) ^ W(12) ^ W(6) ^ W(4)), 1);		/* 52 */
	c = ROTATE_LEFT(d, 5) + H(e, a, b) + c + W(4) + SHA1_CONST(2);
	e = ROTATE_LEFT(e, 30);

	W(5) = ROTATE_LEFT((W(2) ^ W(13) ^ W(7) ^ W(5)), 1);		/* 53 */
	b = ROTATE_LEFT(c, 5) + H(d, e, a) + b + W(5) + SHA1_CONST(2);
	d = ROTATE_LEFT(d, 30);

	W(6) = ROTATE_LEFT((W(3) ^ W(14) ^ W(8) ^ W(6)), 1);		/* 54 */
	a = ROTATE_LEFT(b, 5) + H(c, d, e) + a + W(6) + SHA1_CONST(2);
	c = ROTATE_LEFT(c, 30);

	W(7) = ROTATE_LEFT((W(4) ^ W(15) ^ W(9) ^ W(7)), 1);		/* 55 */
	e = ROTATE_LEFT(a, 5) + H(b, c, d) + e + W(7) + SHA1_CONST(2);
	b = ROTATE_LEFT(b, 30);

	W(8) = ROTATE_LEFT((W(5) ^ W(0) ^ W(10) ^ W(8)), 1);		/* 56 */
	d = ROTATE_LEFT(e, 5) + H(a, b, c) + d + W(8) + SHA1_CONST(2);
	a = ROTATE_LEFT(a, 30);

	W(9) = ROTATE_LEFT((W(6) ^ W(1) ^ W(11) ^ W(9)), 1);		/* 57 */
	c = ROTATE_LEFT(d, 5) + H(e, a, b) + c + W(9) + SHA1_CONST(2);
	e = ROTATE_LEFT(e, 30);

	W(10) = ROTATE_LEFT((W(7) ^ W(2) ^ W(12) ^ W(10)), 1);	/* 58 */
	b = ROTATE_LEFT(c, 5) + H(d, e, a) + b + W(10) + SHA1_CONST(2);
	d = ROTATE_LEFT(d, 30);

	W(11) = ROTATE_LEFT((W(8) ^ W(3) ^ W(13) ^ W(11)), 1);	/* 59 */
	a = ROTATE_LEFT(b, 5) + H(c, d, e) + a + W(11) + SHA1_CONST(2);
	c = ROTATE_LEFT(c, 30);

	/* round 4 */
	W(12) = ROTATE_LEFT((W(9) ^ W(4) ^ W(14) ^ W(12)), 1);	/* 60 */
	e = ROTATE_LEFT(a, 5) + G(b, c, d) + e + W(12) + SHA1_CONST(3);
	b = ROTATE_LEFT(b, 30);

	W(13) = ROTATE_LEFT((W(10) ^ W(5) ^ W(15) ^ W(13)), 1);	/* 61 */
	d = ROTATE_LEFT(e, 5) + G(a, b, c) + d + W(13) + SHA1_CONST(3);
	a = ROTATE_LEFT(a, 30);

	W(14) = ROTATE_LEFT((W(11) ^ W(6) ^ W(0) ^ W(14)), 1);	/* 62 */
	c = ROTATE_LEFT(d, 5) + G(e, a, b) + c + W(14) + SHA1_CONST(3);
	e = ROTATE_LEFT(e, 30);

	W(15) = ROTATE_LEFT((W(12) ^ W(7) ^ W(1) ^ W(15)), 1);	/* 63 */
	b = ROTATE_LEFT(c, 5) + G(d, e, a) + b + W(15) + SHA1_CONST(3);
	d = ROTATE_LEFT(d, 30);

	W(0) = ROTATE_LEFT((W(13) ^ W(8) ^ W(2) ^ W(0)), 1);		/* 64 */
	a = ROTATE_LEFT(b, 5) + G(c, d, e) + a + W(0) + SHA1_CONST(3);
	c = ROTATE_LEFT(c, 30);

	W(1) = ROTATE_LEFT((W(14) ^ W(9) ^ W(3) ^ W(1)), 1);		/* 65 */
	e = ROTATE_LEFT(a, 5) + G(b, c, d) + e + W(1) + SHA1_CONST(3);
	b = ROTATE_LEFT(b, 30);

	W(2) = ROTATE_LEFT((W(15) ^ W(10) ^ W(4) ^ W(2)), 1);	/* 66 */
	d = ROTATE_LEFT(e, 5) + G(a, b, c) + d + W(2) + SHA1_CONST(3);
	a = ROTATE_LEFT(a, 30);

	W(3) = ROTATE_LEFT((W(0) ^ W(11) ^ W(5) ^ W(3)), 1);		/* 67 */
	c = ROTATE_LEFT(d, 5) + G(e, a, b) + c + W(3) + SHA1_CONST(3);
	e = ROTATE_LEFT(e, 30);

	W(4) = ROTATE_LEFT((W(1) ^ W(12) ^ W(6) ^ W(4)), 1);		/* 68 */
	b = ROTATE_LEFT(c, 5) + G(d, e, a) + b + W(4) + SHA1_CONST(3);
	d = ROTATE_LEFT(d, 30);

	W(5) = ROTATE_LEFT((W(2) ^ W(13) ^ W(7) ^ W(5)), 1);		/* 69 */
	a = ROTATE_LEFT(b, 5) + G(c, d, e) + a + W(5) + SHA1_CONST(3);
	c = ROTATE_LEFT(c, 30);

	W(6) = ROTATE_LEFT((W(3) ^ W(14) ^ W(8) ^ W(6)), 1);		/* 70 */
	e = ROTATE_LEFT(a, 5) + G(b, c, d) + e + W(6) + SHA1_CONST(3);
	b = ROTATE_LEFT(b, 30);

	W(7) = ROTATE_LEFT((W(4) ^ W(15) ^ W(9) ^ W(7)), 1);		/* 71 */
	d = ROTATE_LEFT(e, 5) + G(a, b, c) + d + W(7) + SHA1_CONST(3);
	a = ROTATE_LEFT(a, 30);

	W(8) = ROTATE_LEFT((W(5) ^ W(0) ^ W(10) ^ W(8)), 1);		/* 72 */
	c = ROTATE_LEFT(d, 5) + G(e, a, b) + c + W(8) + SHA1_CONST(3);
	e = ROTATE_LEFT(e, 30);

	W(9) = ROTATE_LEFT((W(6) ^ W(1) ^ W(11) ^ W(9)), 1);		/* 73 */
	b = ROTATE_LEFT(c, 5) + G(d, e, a) + b + W(9) + SHA1_CONST(3);
	d = ROTATE_LEFT(d, 30);

	W(10) = ROTATE_LEFT((W(7) ^ W(2) ^ W(12) ^ W(10)), 1);	/* 74 */
	a = ROTATE_LEFT(b, 5) + G(c, d, e) + a + W(10) + SHA1_CONST(3);
	c = ROTATE_LEFT(c, 30);

	W(11) = ROTATE_LEFT((W(8) ^ W(3) ^ W(13) ^ W(11)), 1);	/* 75 */
	e = ROTATE_LEFT(a, 5) + G(b, c, d) + e + W(11) + SHA1_CONST(3);
	b = ROTATE_LEFT(b, 30);

	W(12) = ROTATE_LEFT((W(9) ^ W(4) ^ W(14) ^ W(12)), 1);	/* 76 */
	d = ROTATE_LEFT(e, 5) + G(a, b, c) + d + W(12) + SHA1_CONST(3);
	a = ROTATE_LEFT(a, 30);

	W(13) = ROTATE_LEFT((W(10) ^ W(5) ^ W(15) ^ W(13)), 1);	/* 77 */
	c = ROTATE_LEFT(d, 5) + G(e, a, b) + c + W(13) + SHA1_CONST(3);
	e = ROTATE_LEFT(e, 30);

	W(14) = ROTATE_LEFT((W(11) ^ W(6) ^ W(0) ^ W(14)), 1);	/* 78 */
	b = ROTATE_LEFT(c, 5) + G(d, e, a) + b + W(14) + SHA1_CONST(3);
	d = ROTATE_LEFT(d, 30);

	W(15) = ROTATE_LEFT((W(12) ^ W(7) ^ W(1) ^ W(15)), 1);	/* 79 */

	ctx->state[0] += ROTATE_LEFT(b, 5) + G(c, d, e) + a + W(15) +
	    SHA1_CONST(3);
	ctx->state[1] += b;
	ctx->state[2] += ROTATE_LEFT(c, 30);
	ctx->state[3] += d;
	ctx->state[4] += e;

	/* zeroize sensitive information */
	W(0) = W(1) = W(2) = W(3) = W(4) = W(5) = W(6) = W(7) = W(8) = 0;
	W(9) = W(10) = W(11) = W(12) = W(13) = W(14) = W(15) = 0;
}

#pragma inline(base_sha1_multiblock)
static void
base_sha1_multiblock(SHA1_CTX *ctx, const uint8_t *input,
    unsigned int input_length_in_blocks)
{
	int	i;

	for (i = 0; i < input_length_in_blocks; i++)
		SHA1Transform(ctx, &input[i << 6]);

}
#endif	/* !(__amd64 || (sun4v && HWCAP_SHA1)) */


/*
 * Encode()
 *
 * purpose: to convert a list of numbers from little endian to big endian
 *   input: input	: place to get numbers to convert
 *          len		: length of input, in bytes
 *  output: output	: place to store the converted big endian numbers
 */
#pragma inline(Encode)
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


#if defined(sun4v) && defined(_KERNEL)
/*
 * Return 1 if executing on Yosemite Falls with sha1 instruction,
 * otherwise 0. Cache the result, as the CPU can't change.
 *
 * Note: the userland version uses getisax().  The kernel version uses
 * global variable cpu_hwcap_flags.
 */
#pragma inline(yf_sha1_instruction_present)
static boolean_t
yf_sha1_instruction_present(void)
{
	return ((cpu_hwcap_flags & AV_SPARC_SHA1) != 0);
}


#elif	defined(__amd64)
/*
 * Return SHA1-relevant features on this processor:
 * SHA1_ON_SSSE3		if SSSE3 instructions supported
 * SHA1_ON_AVX			if and AVX instructions supported
 * SHA1_ON_SSSE3|SHA1_ON_AVX	if both SSSE3 and AVX instructions supported
 * SHA1_ON_NONE (0)		if SSSE3 and AVX instructions not supported
 *
 * SSSE3 instructions include palignr and pshufb, used here to optimize SHA1.
 * AVX instructions include vpalignr, vpxor, vpslld, vpaddd, and vmovdqu,
 * also used here to optimize SHA1, if available.
 *
 * Note: the userland version uses getisax().  The kernel version uses
 * is_x86_feature().
 */
static uint32_t
ssse3_avx_instructions_present(void)
{
	int	result = SHA1_ON_GENERIC;

#ifdef _KERNEL
	if (is_x86_feature(x86_featureset, X86FSET_SSSE3))
		result |= SHA1_ON_SSSE3;
	if (is_x86_feature(x86_featureset, X86FSET_AVX))
		result |= SHA1_ON_AVX;
#else
	uint_t		ui = 0;

	(void) getisax(&ui, 1);
	if ((ui & AV_386_SSSE3) != 0)
		result |= SHA1_ON_SSSE3;
	if ((ui & AV_386_AVX) != 0)
		result |= SHA1_ON_AVX;
#endif	/* _KERNEL */

	return (result);
}
#endif /* sun4v, __amd64 */
