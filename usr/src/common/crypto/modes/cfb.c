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

#ifndef _KERNEL
#include <strings.h>
#include <limits.h>
#include <security/cryptoki.h>
#endif

#include <sys/types.h>
#include <modes/modes.h>
#include <sys/crypto/common.h>
#include <sys/crypto/impl.h>

/*
 * XOR less than or equal to block size bytes and copy to output.
 */
static void
xor_and_and_copy(void *iov_or_mp, offset_t *offset, size_t block_size,
    crypto_data_t *out, uint8_t *datap, uint8_t *encrypted_feedback, size_t amt,
    void (*xor_block)(uint8_t *, uint8_t *))
{
	uint8_t *out_data_1;
	uint8_t *out_data_2;
	uint8_t *src, *dst;
	size_t out_data_1_len;
	int i;

	src = (out == NULL) ? encrypted_feedback : datap;
	dst = (out == NULL) ? datap : encrypted_feedback;

	if (amt == block_size) {
		xor_block(src, dst);
	} else {
		for (i = 0; i < amt; i++)
			dst[i] ^= src[i];
	}
	if (out != NULL) {
		crypto_get_ptrs(out, iov_or_mp, offset, &out_data_1,
		    &out_data_1_len, &out_data_2, amt);

		bcopy(dst, out_data_1, out_data_1_len);
		if (out_data_2 != NULL) {
			bcopy(dst + out_data_1_len, out_data_2,
			    amt - out_data_1_len);
		}
		out->cd_offset += amt;
	}
}

/*
 * Encrypt and decrypt multiple blocks of data in CFB mode.
 * CFB is a stream cipher: output length is always equal to input length.
 */
int
cfb_mode_contiguous_blocks(cfb_ctx_t *ctx, char *data, size_t length,
    crypto_data_t *out, size_t block_size,
    int (*cipher)(const void *ks, const uint8_t *pt, uint8_t *ct),
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *),
    boolean_t is_encrypt)
{
	ssize_t remainder = length;
	uint8_t *datap = (uint8_t *)data;
	uint8_t *cooked_bytes;
	void *iov_or_mp;
	offset_t offset;
	uint8_t tmp[16];

	if (length == 0)
		return (CRYPTO_SUCCESS);

	if (out != NULL)
		crypto_init_ptrs(out, &iov_or_mp, &offset);

	/*
	 * Check for unused encrypted feedback bytes.
	 * XOR input with these bytes to produce output.
	 */
	if (ctx->cfb_unused_bytes > 0) {
		size_t need;

		need = MIN(length, ctx->cfb_unused_bytes);
		cooked_bytes = (uint8_t *)ctx->cfb_iv;
		cooked_bytes += (block_size - ctx->cfb_unused_bytes);

		/* make copy before we overwrite */
		if (out == NULL && !is_encrypt)
			bcopy(datap, tmp, need);

		xor_and_and_copy(&iov_or_mp, &offset, block_size,
		    out, datap, cooked_bytes, need, xor_block);

		if (is_encrypt) {
			if (out == NULL)
				bcopy(datap, cooked_bytes, need);
		} else {
			bcopy((out == NULL) ? tmp : datap, cooked_bytes, need);
		}

		ctx->cfb_unused_bytes -= need;
		datap += need;
		remainder -= need;
		if (remainder <= 0)
			return (CRYPTO_SUCCESS);
	}

	/*
	 * At this point, all previously encrypted ciphertext
	 * bytes should be consumed.
	 */
	do {
		if (remainder > 0 && remainder < block_size) {
			cipher(ctx->cfb_keysched, (uint8_t *)ctx->cfb_iv,
			    (uint8_t *)ctx->cfb_iv);

			/* make copy before we overwrite */
			if (out == NULL && !is_encrypt)
				bcopy(datap, tmp, remainder);

			xor_and_and_copy(&iov_or_mp, &offset, block_size,
			    out, datap, (uint8_t *)ctx->cfb_iv, remainder,
			    xor_block);

			if (is_encrypt) {
				if (out == NULL)
					bcopy(datap, (uint8_t *)ctx->cfb_iv,
					    remainder);
			} else {
				bcopy((out == NULL) ? tmp : datap,
				    (uint8_t *)ctx->cfb_iv, remainder);
			}

			ctx->cfb_unused_bytes = (block_size - remainder);
			return (CRYPTO_SUCCESS);
		}

		/* encrypt previous ciphertext and store results in cfb_iv */
		cipher(ctx->cfb_keysched, (uint8_t *)ctx->cfb_iv,
		    (uint8_t *)ctx->cfb_iv);

		/* make copy before we overwrite */
		if (out == NULL && !is_encrypt)
			copy_block(datap, tmp);

		xor_and_and_copy(&iov_or_mp, &offset, block_size, out, datap,
		    (uint8_t *)ctx->cfb_iv, block_size, xor_block);

		if (is_encrypt) {
			if (out == NULL)
				copy_block(datap, (uint8_t *)ctx->cfb_iv);
		} else {
			copy_block((out == NULL) ? tmp : datap,
			    (uint8_t *)ctx->cfb_iv);
		}

		datap += block_size;
		remainder -= block_size;
	} while (remainder > 0);
	return (CRYPTO_SUCCESS);
}

int
cfb_init_ctx(cfb_ctx_t *cfb_ctx, char *param, size_t param_len,
    size_t block_size, void (*copy_block)(uint8_t *, uint64_t *))
{
	/*
	 * Copy IV into context.
	 *
	 * If cm_param == NULL then the IV comes from the
	 * cd_miscdata field in the crypto_data structure.
	 */
	if (param != NULL) {
		ASSERT(param_len == block_size);
		copy_block((uchar_t *)param, cfb_ctx->cfb_iv);
	}

	cfb_ctx->cfb_lastp = (uint8_t *)&cfb_ctx->cfb_iv[0];
	cfb_ctx->cfb_flags |= CFB128_MODE;
	return (CRYPTO_SUCCESS);
}

/* ARGSUSED */
void *
cfb_alloc_ctx(int kmflag)
{
	cfb_ctx_t *cfb_ctx;

	if ((cfb_ctx = CRYPTO_ZALLOC(sizeof (cfb_ctx_t), kmflag)) == NULL)
		return (NULL);

	cfb_ctx->cfb_flags = CFB128_MODE;
	return (cfb_ctx);
}
