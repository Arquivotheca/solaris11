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
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */


#ifndef _KERNEL
#include <strings.h>
#include <limits.h>
#include <security/cryptoki.h>
#endif	/* !_KERNEL */

#include <sys/types.h>
#include <sys/kmem.h>
#include <modes/modes.h>
#include <sys/crypto/common.h>
#include <sys/crypto/impl.h>
#include <sys/byteorder.h>

#ifdef __amd64
#ifdef _KERNEL
#include <sys/cpuvar.h>		/* cpu_t, CPU */
#include <sys/x86_archext.h>	/* x86_featureset, X86FSET_*, CPUID_* */
#include <sys/disp.h>		/* kpreempt_disable(), kpreempt_enable */
/* Workaround for no XMM kernel thread save/restore */
#define	KPREEMPT_DISABLE	kpreempt_disable()
#define	KPREEMPT_ENABLE		kpreempt_enable()

#else
#include <sys/auxv.h>		/* getisax() */
#include <sys/auxv_386.h>	/* AV_386_PCLMULQDQ bit */
#define	KPREEMPT_DISABLE
#define	KPREEMPT_ENABLE
#endif	/* _KERNEL */

extern void gcm_mul_pclmulqdq(uint64_t *x_in, uint64_t *y, uint64_t *res);
static int intel_pclmulqdq_instruction_present(void);

#endif	/* __amd64 */

typedef struct aes_block {
	uint64_t a;
	uint64_t b;
} aes_block_t;

#if (defined(__sparcv9) && defined(_KERNEL)) || defined(HWCAP_VIS3)
extern void gcm_mul_vis3(uint64_t *x_in, uint64_t *y, uint64_t *res);
extern void ghash_multiblock_vis3(uint64_t *ghash, uint64_t *gcm_H,
    uint64_t *datap, int len);

#ifdef _KERNEL
#include <sys/archsystm.h>
#include <kernel_fp_use.h>
static int vis3_instructions_present(void);
#endif /* _KERNEL */
#endif /* (__sparcv9 && _KERNEL) || HWCAP_VIS3 */



/*
 * gcm_mul()
 * Perform a carry-less multiplication (that is, use XOR instead of the
 * multiply operator) on *x_in and *y and place the result in *res.
 *
 * Byte swap the input (*x_in and *y) and the output (*res).
 *
 * Note: x_in, y, and res all point to 16-byte numbers (an array of two
 * 64-bit integers).
 */
#if defined(HWCAP_VIS3) && !defined(_KERNEL)
/* Userland only version avoiding the check for VIS3 */
void
gcm_mul(uint64_t *x_in, uint64_t *y, uint64_t *res)
{
	gcm_mul_vis3(x_in, y, res);
}
#else
void
gcm_mul(uint64_t *x_in, uint64_t *y, uint64_t *res)
{
#ifdef __amd64
	if (intel_pclmulqdq_instruction_present()) {
		KPREEMPT_DISABLE;
		gcm_mul_pclmulqdq(x_in, y, res);
		KPREEMPT_ENABLE;
	} else
#endif /* __amd64 */

#if defined(__sparcv9) && defined(_KERNEL)
	if (vis3_instructions_present()) {
		gcm_mul_vis3(x_in, y, res);
	} else
#endif
	{
		static const uint64_t R = 0xe100000000000000ULL;
		aes_block_t z = {0, 0};
		aes_block_t v;
		uint64_t x;
		int i, j;

		v.a = ntohll(y[0]);
		v.b = ntohll(y[1]);

		for (j = 0; j < 2; j++) {
			x = ntohll(x_in[j]);
			for (i = 0; i < 64; i++, x <<= 1) {
				if (x & 0x8000000000000000ULL) {
					z.a ^= v.a;
					z.b ^= v.b;
				}
				if (v.b & 1ULL) {
					v.b = (v.a << 63)|(v.b >> 1);
					v.a = (v.a >> 1) ^ R;
				} else {
					v.b = (v.a << 63)|(v.b >> 1);
					v.a = v.a >> 1;
				}
			}
		}
		res[0] = htonll(z.a);
		res[1] = htonll(z.b);
	}
}
#endif /* HWCAP_VIS3 && !_KERNEL */


#define	GHASH(c, d, t) \
	xor_block((uint8_t *)(d), (uint8_t *)(c)->gcm_ghash); \
	gcm_mul((uint64_t *)(void *)(c)->gcm_ghash, (c)->gcm_H, \
	(uint64_t *)(void *)(t));


/*
 * Encrypt multiple blocks of data in GCM mode.  Decrypt for GCM mode
 * is done in another function.
 */
int
gcm_mode_encrypt_contiguous_blocks(gcm_ctx_t *ctx, char *data, size_t length,
    crypto_data_t *out, size_t block_size,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *))
{
	size_t remainder = length;
	size_t need;
	uint8_t *datap = (uint8_t *)data;
	uint8_t *blockp;
	uint8_t *lastp;
	void *iov_or_mp;
	offset_t offset;
	uint8_t *out_data_1;
	uint8_t *out_data_2;
	size_t out_data_1_len;
	uint64_t counter;
	uint64_t counter_mask = ntohll(0x00000000ffffffffULL);

	/* Check to see if we have a full block */
	if (length + ctx->gcm_remainder_len < block_size) {
		/* accumulate bytes here and return */
		bcopy(datap,
		    (uint8_t *)ctx->gcm_remainder + ctx->gcm_remainder_len,
		    length);
		ctx->gcm_remainder_len += length;
		ctx->gcm_copy_to = datap;
		return (CRYPTO_SUCCESS);
	}

	lastp = (uint8_t *)ctx->gcm_cb;
	if (out != NULL)
		crypto_init_ptrs(out, &iov_or_mp, &offset);

	do {
		if (ctx->gcm_remainder_len > 0) {
			/* Unprocessed data from last call. */
			need = block_size - ctx->gcm_remainder_len;

			if (need > remainder)
				return (CRYPTO_DATA_LEN_RANGE);

			bcopy(datap, &((uint8_t *)ctx->gcm_remainder)
			    [ctx->gcm_remainder_len], need);

			blockp = (uint8_t *)ctx->gcm_remainder;
		} else {
			blockp = datap;
		}

		encrypt_block(ctx->gcm_keysched, (uint8_t *)ctx->gcm_cb,
		    (uint8_t *)ctx->gcm_tmp);
		/*
		 * Increment counter. Counter bits are confined
		 * to the bottom 32 bits of the counter block.
		 */
		counter = ntohll(ctx->gcm_cb[1] & counter_mask);
		counter = htonll(counter + 1);
		counter &= counter_mask;
		ctx->gcm_cb[1] = (ctx->gcm_cb[1] & ~counter_mask) | counter;

		xor_block(blockp, (uint8_t *)ctx->gcm_tmp);

		lastp = (uint8_t *)ctx->gcm_tmp;

		ctx->gcm_processed_data_len += block_size;

		if (out == NULL) { /* in-place (no output buffer) */
			if (ctx->gcm_remainder_len > 0) {
				bcopy(blockp, ctx->gcm_copy_to,
				    ctx->gcm_remainder_len);
				bcopy(blockp + ctx->gcm_remainder_len, datap,
				    need);
			}
		} else { /* 2 buffers */
			crypto_get_ptrs(out, &iov_or_mp, &offset, &out_data_1,
			    &out_data_1_len, &out_data_2, block_size);

			/* copy block to where it belongs */
			if (out_data_1_len == block_size) {
				copy_block(lastp, out_data_1);
			} else {
				bcopy(lastp, out_data_1, out_data_1_len);
				if (out_data_2 != NULL) {
					bcopy(lastp + out_data_1_len,
					    out_data_2,
					    block_size - out_data_1_len);
				}
			}
			/* update offset */
			out->cd_offset += block_size;
		}

		/* add ciphertext to the hash */
		GHASH(ctx, ctx->gcm_tmp, ctx->gcm_ghash);

		/* Update pointer to next block of data to be processed. */
		if (ctx->gcm_remainder_len != 0) {
			datap += need;
			ctx->gcm_remainder_len = 0;
		} else {
			datap += block_size;
		}

		remainder = (size_t)&data[length] - (size_t)datap;

		/* Incomplete last block. */
		if (remainder > 0 && remainder < block_size) {
			bcopy(datap, ctx->gcm_remainder, remainder);
			ctx->gcm_remainder_len = remainder;
			ctx->gcm_copy_to = datap;
			break;
		}
		ctx->gcm_copy_to = NULL;

	} while (remainder > 0);

	return (CRYPTO_SUCCESS);
}


/* ARGSUSED */
int
gcm_encrypt_final(gcm_ctx_t *ctx, crypto_data_t *out, size_t block_size,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *))
{
	uint8_t *ghash, *macp;
	int rv;

	if (out->cd_length <
	    (ctx->gcm_remainder_len + ctx->gcm_tag_len)) {
		return (CRYPTO_DATA_LEN_RANGE);
	}

	ghash = (uint8_t *)ctx->gcm_ghash;

	if (ctx->gcm_remainder_len > 0) {
		uint8_t		*tmpp = (uint8_t *)ctx->gcm_tmp;
		int		i;
		macp = (uint8_t *)ctx->gcm_remainder;

		if ((ctx->gcm_flags & NO_CTR_FINAL) == 0) {
			/*
			 * Here is where we deal with data that is not a
			 * multiple of the block size.
			 */
			encrypt_block(ctx->gcm_keysched, (uint8_t *)ctx->gcm_cb,
			    (uint8_t *)ctx->gcm_tmp);

			/* XOR with counter block */
			for (i = 0; i < ctx->gcm_remainder_len; i++) {
				macp[i] ^= tmpp[i];
			}
			ctx->gcm_processed_data_len += ctx->gcm_remainder_len;
		}

		bzero(macp + ctx->gcm_remainder_len,
		    block_size - ctx->gcm_remainder_len);

		/* add ciphertext to the hash */
		GHASH(ctx, macp, ghash);
	}

	ctx->gcm_len_a_len_c[1] =
	    htonll(CRYPTO_BYTES2BITS(ctx->gcm_processed_data_len));
	GHASH(ctx, ctx->gcm_len_a_len_c, ghash);
	encrypt_block(ctx->gcm_keysched, (uint8_t *)ctx->gcm_J0,
	    (uint8_t *)ctx->gcm_J0);
	xor_block((uint8_t *)ctx->gcm_J0, ghash);

	if ((ctx->gcm_flags & NO_CTR_FINAL) == 0) {
		if (ctx->gcm_remainder_len > 0) {
			rv = crypto_put_output_data(macp,
			    out, ctx->gcm_remainder_len);
			if (rv != CRYPTO_SUCCESS)
				return (rv);
		}
		out->cd_offset += ctx->gcm_remainder_len;
	}

	ctx->gcm_remainder_len = 0;
	rv = crypto_put_output_data(ghash, out, ctx->gcm_tag_len);
	if (rv != CRYPTO_SUCCESS)
		return (rv);
	out->cd_offset += ctx->gcm_tag_len;

	return (CRYPTO_SUCCESS);
}


/* ARGSUSED */
int
gcm_mode_decrypt_contiguous_blocks(gcm_ctx_t *ctx, char *data, size_t length,
    crypto_data_t *out, size_t block_size,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *))
{
	size_t new_len;
	uint64_t *new;

	/*
	 * Copy contiguous ciphertext input blocks to plaintext buffer.
	 * Ciphertext will be decrypted in the final.
	 */
	if (length > 0) {
		new_len = ctx->gcm_pt_buf_len + length;
		new = (uint64_t *)CRYPTO_ALLOC(new_len, ctx->gcm_kmflag);
		if (new == NULL)
			return (CRYPTO_HOST_MEMORY);

		if (ctx->gcm_pt_buf_len == 0) {
			ASSERT3P(ctx->gcm_pt_buf, ==, NULL);
		}
		if (ctx->gcm_pt_buf != NULL) { /* copy and free old buffer */
			bcopy(ctx->gcm_pt_buf, new, ctx->gcm_pt_buf_len);
			CRYPTO_FREE(ctx->gcm_pt_buf, ctx->gcm_pt_buf_len);
		}

		ctx->gcm_pt_buf = new;
		ctx->gcm_pt_buf_len = new_len;
		bcopy(data, (&((uchar_t *)
		    ctx->gcm_pt_buf)[ctx->gcm_processed_data_len]), length);
		ctx->gcm_processed_data_len += length;
	}

	ctx->gcm_remainder_len = 0;
	return (CRYPTO_SUCCESS);
}

void
ghash_multiblock(gcm_ctx_t *ctx, uint64_t *datap, int len)
{
	int i;
	uint64_t *ghash = ctx->gcm_ghash;
	uint64_t *gcm_H = ctx->gcm_H;

#if defined(HWCAP_VIS3) && !defined(_KERNEL)
	ghash_multiblock_vis3(ghash, gcm_H, datap, len);
#else
	for (i = 0; i < len; i++) {
		ghash[0] ^= datap[0];
		ghash[1] ^= datap[1];
		gcm_mul(ghash, gcm_H, ghash);
		datap += 2;
	}
#endif
}

int
gcm_decrypt_final(gcm_ctx_t *ctx, crypto_data_t *out, size_t block_size,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *),
    int (*decrypt_contiguous_blocks)(void *ctx, char *data, size_t length,
    crypto_data_t *out))
{
	size_t pt_len;
	size_t remainder;
	uint8_t *ghash;
	uint64_t *blockp, *authp;
	size_t processed = 0;
	uint32_t flags;

	ASSERT(ctx->gcm_processed_data_len == ctx->gcm_pt_buf_len);

	pt_len = ctx->gcm_processed_data_len - ctx->gcm_tag_len;
	ghash = (uint8_t *)ctx->gcm_ghash;
	blockp = ctx->gcm_pt_buf;
	remainder = pt_len;

	if (remainder >= block_size) {
		size_t num_blocks = remainder / block_size;

		ghash_multiblock(ctx, blockp, num_blocks);
		processed = num_blocks * block_size;
		remainder -= processed;
	}

	if (remainder > 0) {
		/*
		 * There's not a block full of data, pad rest of
		 * buffer with zero
		 */
		authp = ctx->gcm_tmp;
		bzero(authp, block_size);
		bcopy(&(blockp[processed / sizeof (uint64_t)]),
		    authp, remainder);
		GHASH(ctx, authp, ghash);
	}

	ctx->gcm_len_a_len_c[1] = htonll(CRYPTO_BYTES2BITS(pt_len));
	GHASH(ctx, ctx->gcm_len_a_len_c, ghash);
	encrypt_block(ctx->gcm_keysched, (uint8_t *)ctx->gcm_J0,
	    (uint8_t *)ctx->gcm_J0);
	xor_block((uint8_t *)ctx->gcm_J0, ghash);

	/* compare the input authentication tag with what we calculated */
	if (bcmp(&(((uint8_t *)ctx->gcm_pt_buf)[pt_len]),
	    ghash, ctx->gcm_tag_len)) {
		/* They don't match */
		return (CRYPTO_INVALID_MAC);
	} else {
		flags = ctx->gcm_flags;
		blockp = ctx->gcm_pt_buf;
		ctx->gcm_flags = (ctx->gcm_flags & (~MODE_MASK)) | CTR_MODE;
		decrypt_contiguous_blocks(ctx, (char *)blockp, pt_len, out);
		ctx->gcm_flags = flags;
		ctx->gcm_remainder_len = 0;
	}
	return (CRYPTO_SUCCESS);
}


/*
 * Sanity check arguments in gcm_param.
 */
static int
gcm_validate_args(CK_AES_GCM_PARAMS *gcm_param)
{
	size_t tag_len;

	/*
	 * Check the length of the authentication tag (in bits).
	 */
	tag_len = gcm_param->ulTagBits;
	switch (tag_len) {
	case 32:
	case 64:
	case 96:
	case 104:
	case 112:
	case 120:
	case 128:
		break;
	default:
		return (CRYPTO_MECHANISM_PARAM_INVALID);
	}

	if (gcm_param->ulIvLen == 0)
		return (CRYPTO_MECHANISM_PARAM_INVALID);

	return (CRYPTO_SUCCESS);
}

static void
gcm_format_initial_blocks(uchar_t *iv, ulong_t iv_len,
    gcm_ctx_t *ctx, size_t block_size,
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *))
{
	uint8_t *cb;
	ssize_t remainder = iv_len;
	ulong_t processed = 0;
	uint8_t *datap, *ghash;
	uint64_t len_a_len_c[2];

	ASSERT(iv != NULL);

	ghash = (uint8_t *)ctx->gcm_ghash;
	cb = (uint8_t *)ctx->gcm_cb;
	if (iv_len == 12) {
		bcopy(iv, cb, 12);
		cb[12] = 0;
		cb[13] = 0;
		cb[14] = 0;
		cb[15] = 1;
		/* J0 will be used again in the final */
		copy_block(cb, (uint8_t *)ctx->gcm_J0);
	} else {
		/* GHASH the IV */
		do {
			if (remainder < block_size) {
				bzero(cb, block_size);
				bcopy(&(iv[processed]), cb, remainder);
				datap = (uint8_t *)cb;
				remainder = 0;
			} else {
				datap = (uint8_t *)(&(iv[processed]));
				processed += block_size;
				remainder -= block_size;
			}
			GHASH(ctx, datap, ghash);
		} while (remainder > 0);

		len_a_len_c[0] = 0;
		len_a_len_c[1] = htonll(CRYPTO_BYTES2BITS(iv_len));
		GHASH(ctx, len_a_len_c, ctx->gcm_J0);

		/* J0 will be used again in the final */
		copy_block((uint8_t *)ctx->gcm_J0, (uint8_t *)cb);
	}
}


/*
 * The following function is called at encrypt or decrypt init time
 * for AES GCM mode.
 */
/* ARGSUSED */
static int
gcm_init(gcm_ctx_t *ctx, unsigned char *iv, size_t iv_len,
    unsigned char *auth_data, size_t auth_data_len, size_t block_size,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *),
    boolean_t save_fp)
{
	uint8_t		*ghash;
	uint64_t	*authp;
	ssize_t		remainder;
#ifdef __sparc
	boolean_t	authp_alloced = B_FALSE;
#ifdef _KERNEL
	fp_save_t	fp_save_buf;
#endif
#endif
	size_t		processed;

	ASSERT(ctx->gcm_pt_buf == NULL);
	ASSERT3U(ctx->gcm_pt_buf_len, ==, 0);

	/* encrypt zero block to get subkey H */
	bzero(ctx->gcm_H, sizeof (ctx->gcm_H));

#if defined(__sparc) && defined(_KERNEL)
	SAVE_FP;
#endif
	encrypt_block(ctx->gcm_keysched, (uint8_t *)ctx->gcm_H,
	    (uint8_t *)ctx->gcm_H);

#if defined(__sparc) && defined(_KERNEL)
	RESTORE_FP;
#endif

	gcm_format_initial_blocks(iv, iv_len, ctx, block_size,
	    copy_block, xor_block);

	authp = ctx->gcm_tmp;
	ghash = (uint8_t *)ctx->gcm_ghash;
	bzero(ghash, block_size);

	processed = 0;
	remainder = auth_data_len;

	if (remainder >= block_size) {
		int num_blocks = remainder / block_size;

#ifdef __sparc
		if ((((size_t)auth_data) & 7) == 0) {
			/* LINTED E_BAD_PTR_CAST_ALIGN */
			authp = (uint64_t *)auth_data;
		} else {
			authp = (uint64_t *)CRYPTO_ALLOC(
			    num_blocks * block_size, ctx->gcm_kmflag);
			if (authp == NULL) {
				return (CRYPTO_HOST_MEMORY);
			}
			authp_alloced = B_TRUE;
			bcopy(auth_data, authp, num_blocks * block_size);
		}
#else
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		authp = (uint64_t *)auth_data;
#endif
		ghash_multiblock(ctx, authp, num_blocks);

#ifdef __sparc
		if (authp_alloced) {
			CRYPTO_FREE(authp, num_blocks * block_size);

		}
#endif

		processed = num_blocks * block_size;
		remainder -= processed;
	}

	if (remainder > 0) {
		/*
		 * There's not a block full of data, pad rest of
		 * buffer with zero
		 */
		authp = ctx->gcm_tmp;
		bzero(authp, block_size);
		ASSERT(auth_data != NULL);
		bcopy(&(auth_data[processed]), authp, remainder);
		GHASH(ctx, authp, ghash);
	}

	return (CRYPTO_SUCCESS);
}

int
gcm_init_ctx(gcm_ctx_t *gcm_ctx, char *param, size_t block_size,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *),
    boolean_t save_fp)
{
	int rv;
	CK_AES_GCM_PARAMS *gcm_param;
	uint64_t counter_mask = ntohll(0x00000000ffffffffULL);
	uint64_t counter;

	if (param != NULL) {
		gcm_param = (CK_AES_GCM_PARAMS *)(void *)param;

		if ((rv = gcm_validate_args(gcm_param)) != 0) {
			return (rv);
		}

		gcm_ctx->gcm_tag_len = gcm_param->ulTagBits;
		gcm_ctx->gcm_tag_len >>= 3;
		gcm_ctx->gcm_processed_data_len = 0;
		gcm_ctx->gcm_ctr.ctr_lower_mask = counter_mask;
		gcm_ctx->gcm_ctr.ctr_upper_mask = 0;

		/* these values are in bits */
		gcm_ctx->gcm_len_a_len_c[0]
		    = htonll(CRYPTO_BYTES2BITS(gcm_param->ulAADLen));

		rv = CRYPTO_SUCCESS;
		gcm_ctx->gcm_flags |= GCM_MODE;

		if (gcm_init(gcm_ctx, gcm_param->pIv, gcm_param->ulIvLen,
		    gcm_param->pAAD, gcm_param->ulAADLen, block_size,
		    encrypt_block, copy_block, xor_block, save_fp) != 0) {
			rv = CRYPTO_MECHANISM_PARAM_INVALID;
		} else {
			/*
			 * Increment counter. Counter bits are confined
			 * to the bottom 32 bits of the counter block.
			 */
			counter = ntohll(gcm_ctx->gcm_cb[1] & counter_mask);
			counter = htonll(counter + 1);
			counter &= counter_mask;
			gcm_ctx->gcm_cb[1] =
			    (gcm_ctx->gcm_cb[1] & ~counter_mask) | counter;
		}
	} else {
		rv = CRYPTO_MECHANISM_PARAM_INVALID;
	}

	return (rv);
}

int
gmac_init_ctx(gcm_ctx_t *gcm_ctx, char *param, size_t block_size,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *),
    boolean_t save_fp)
{
	int rv;
	CK_AES_GMAC_PARAMS *gmac_param;

	if (param != NULL) {
		gmac_param = (CK_AES_GMAC_PARAMS *)(void *)param;

		gcm_ctx->gcm_tag_len = CRYPTO_BITS2BYTES(AES_GMAC_TAG_BITS);
		gcm_ctx->gcm_processed_data_len = 0;

		/* these values are in bits */
		gcm_ctx->gcm_len_a_len_c[0]
		    = htonll(CRYPTO_BYTES2BITS(gmac_param->ulAADLen));

		rv = CRYPTO_SUCCESS;
		gcm_ctx->gcm_flags |= GMAC_MODE;

		if (gcm_init(gcm_ctx, gmac_param->pIv, AES_GMAC_IV_LEN,
		    gmac_param->pAAD, gmac_param->ulAADLen, block_size,
		    encrypt_block, copy_block, xor_block, save_fp) != 0) {
			rv = CRYPTO_MECHANISM_PARAM_INVALID;
		}

	} else {
		rv = CRYPTO_MECHANISM_PARAM_INVALID;
	}

	return (rv);
}


/*
 * gcm_alloc_ctx()
 *
 * Allocate memory for gcm_ctx_t.
 */
#ifndef _KERNEL
/* LINTED E_FUNC_ARG_UNUSED */
#endif
void *
gcm_alloc_ctx(int kmflag)
{
	gcm_ctx_t *gcm_ctx;

	if ((gcm_ctx = CRYPTO_ZALLOC(sizeof (gcm_ctx_t), kmflag)) == NULL)
		return (NULL);

	gcm_ctx->gcm_flags = GCM_MODE;
	return (gcm_ctx);
}


/*
 * gmac_alloc_ctx()
 *
 * Allocate memory for gcm_ctx_t.
 * Same as gcm_alloc_ctx(), except set gcm_flags to GMAC_MODE.
 */
#ifndef _KERNEL
/* LINTED E_FUNC_ARG_UNUSED */
#endif
void *
gmac_alloc_ctx(int kmflag)
{
	gcm_ctx_t *gcm_ctx;

	if ((gcm_ctx = CRYPTO_ZALLOC(sizeof (gcm_ctx_t), kmflag)) == NULL)
		return (NULL);

	gcm_ctx->gcm_flags = GMAC_MODE;
	return (gcm_ctx);
}


void
gcm_set_kmflag(gcm_ctx_t *ctx, int kmflag)
{
	ctx->gcm_kmflag = kmflag;
}


#ifdef __amd64
/*
 * Return 1 if executing on Intel with PCLMULQDQ instructions,
 * otherwise 0 (i.e., Intel without PCLMULQDQ or AMD64).
 * Cache the result, as the CPU can't change.
 *
 * Note: the userland version uses getisax().  The kernel version uses
 * is_x86_featureset().
 */
static int
intel_pclmulqdq_instruction_present(void)
{
	static int	cached_result = -1;

	if (cached_result == -1) { /* first time */
#ifdef _KERNEL
		cached_result =
		    is_x86_feature(x86_featureset, X86FSET_PCLMULQDQ);
#else
		uint_t		ui = 0;

		(void) getisax(&ui, 1);
		cached_result = (ui & AV_386_PCLMULQDQ) != 0;
#endif	/* _KERNEL */
	}

	return (cached_result);
}

#endif /* __amd64 */

#if defined(__sparcv9) && defined(_KERNEL)

/*
 * Return 1 if vis3 instructions available, otherwise 0.
 * Cache the result, as the CPU can't change.
 *
 * Note: the userland version uses getisax().  The kernel version uses
 * global variable cpu_hwcap_flags.
 */
static int
vis3_instructions_present(void)
{
	return ((cpu_hwcap_flags & AV_SPARC_VIS3) != 0);
}
#endif
