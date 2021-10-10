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
#endif

#include <sys/types.h>
#include <modes/modes.h>
#include <sys/crypto/common.h>
#include <sys/crypto/impl.h>
#include <sys/byteorder.h>
#ifdef sun4v
#include <aes/aes_impl.h>
#endif

/* increment up to 128 bit counter depending on the mask */
static void
increment_counter(ctr_ctx_t *ctx)
{
	uint64_t lower_counter, upper_counter, ctr_lower_mask, ctr_upper_mask;

	ctr_lower_mask = ctx->ctr_lower_mask;
	lower_counter = ntohll(ctx->ctr_cb[1] & ctr_lower_mask);
	lower_counter = htonll(lower_counter + 1) & ctr_lower_mask;
	ctx->ctr_cb[1] = (ctx->ctr_cb[1] & ~ctr_lower_mask) | lower_counter;

	/* wrap around */
	if ((lower_counter == 0) && ((ctx->ctr_lower_mask + 1) == 0)) {
		ctr_upper_mask = ctx->ctr_upper_mask;
		upper_counter = ntohll(ctx->ctr_cb[0] & ctr_upper_mask);
		upper_counter = htonll(upper_counter + 1) & ctr_upper_mask;
		ctx->ctr_cb[0] = (ctx->ctr_cb[0] & ~ctr_upper_mask) |
		    upper_counter;
	}
}

/*
 * XOR less than block size bytes and copy to output.
 */
static void
xor_and_and_copy(void *iov_or_mp, offset_t *offset, size_t block_size,
    crypto_data_t *out, uint8_t *datap, uint8_t *counter_block, size_t amt,
    void (*xor_block)(uint8_t *, uint8_t *))
{
	uint8_t *out_data_1;
	uint8_t *out_data_2;
	uint8_t *src, *dst;
	size_t out_data_1_len;
	int i;

	src = (out == NULL) ? counter_block : datap;
	dst = (out == NULL) ? datap : counter_block;

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
 * Encrypt and decrypt multiple blocks of data in counter mode.
 * CTR is a stream cipher: output length is always equal to input length.
 */
int
ctr_mode_contiguous_blocks(ctr_ctx_t *ctx, char *data, size_t length,
    crypto_data_t *out, size_t block_size,
    int (*cipher)(const void *ks, const uint8_t *pt, uint8_t *ct),
    void (*xor_block)(uint8_t *, uint8_t *))
{
	size_t remainder = length;
	uint8_t *datap = (uint8_t *)data;
	uint8_t *counter_block;
	void *iov_or_mp;
	offset_t offset;

	if (length == 0)
		return (CRYPTO_SUCCESS);

	if (out != NULL)
		crypto_init_ptrs(out, &iov_or_mp, &offset);

	/*
	 * Check for previously encrypted counter block bytes.
	 * XOR input with these bytes to produce output.
	 */
	if (ctx->ctr_remainder_len > 0) {
		size_t need;

		need = MIN(length, ctx->ctr_remainder_len);
		counter_block = (uint8_t *)ctx->ctr_remainder;
		counter_block += (block_size - ctx->ctr_remainder_len);
		xor_and_and_copy(&iov_or_mp, &offset, block_size,
		    out, datap, counter_block, need, xor_block);

		ctx->ctr_remainder_len -= need;
		datap += need;
		remainder -= need;
		if (remainder == 0)
			return (CRYPTO_SUCCESS);
	}

	/*
	 * At this point, all previously encrypted counter block
	 * bytes should be consumed.
	 */
	do {
		if (remainder > 0 && remainder < block_size) {
			cipher(ctx->ctr_keysched, (uint8_t *)ctx->ctr_cb,
			    (uint8_t *)ctx->ctr_remainder);
			increment_counter(ctx);

			xor_and_and_copy(&iov_or_mp, &offset, block_size,
			    out, datap, (uint8_t *)ctx->ctr_remainder,
			    remainder, xor_block);

			ctx->ctr_remainder_len = (block_size - remainder);
			return (CRYPTO_SUCCESS);
		}

		/* encrypt counter block and store results in ctr_remainder */
		cipher(ctx->ctr_keysched, (uint8_t *)ctx->ctr_cb,
		    (uint8_t *)ctx->ctr_remainder);
		increment_counter(ctx);

		xor_and_and_copy(&iov_or_mp, &offset, block_size, out, datap,
		    (uint8_t *)ctx->ctr_remainder, block_size, xor_block);

		datap += block_size;
		remainder -= block_size;
	} while (remainder > 0);
	return (CRYPTO_SUCCESS);
}

int
ctr_init_ctx(ctr_ctx_t *ctr_ctx, ulong_t count, uint8_t *cb,
void (*copy_block)(uint8_t *, uint8_t *))
{
	uint64_t upper_mask = 0;
	uint64_t lower_mask = 0;

	if (count == 0 || count > 128) {
		return (CRYPTO_MECHANISM_PARAM_INVALID);
	}
	/* upper 64 bits of the mask */
	if (count >= 64) {
		count -= 64;
		upper_mask = (count == 64) ? UINT64_MAX : (1ULL << count) - 1;
		lower_mask = UINT64_MAX;
	} else {
		/* now the lower 63 bits */
		lower_mask = (1ULL << count) - 1;
	}
	ctr_ctx->ctr_lower_mask = htonll(lower_mask);
	ctr_ctx->ctr_upper_mask = htonll(upper_mask);

	copy_block(cb, (uchar_t *)ctr_ctx->ctr_cb);
	ctr_ctx->ctr_lastp = (uint8_t *)&ctr_ctx->ctr_cb[0];
	ctr_ctx->ctr_flags |= CTR_MODE;
	return (CRYPTO_SUCCESS);
}


/*
 * ctr_alloc_ctx()
 *
 * Allocate memory for ctr_ctx_t.
 *
 * Note: For AMD64, this structure SHOULD be aligned on a 0 mod 16 address.
 * This is NOT a requirement, but optimizes MMX assembly instructions, such
 * as MOVDQU. Userland calloc() aligns 0 mod 16, but kernel function
 * kmem_zalloc() does not align.
 */
/* ARGSUSED */
void *
ctr_alloc_ctx(int kmflag)
{
	ctr_ctx_t *ctr_ctx;

	if ((ctr_ctx = CRYPTO_ZALLOC(sizeof (ctr_ctx_t), kmflag)) == NULL)
		return (NULL);

#ifdef	__amd64
	ASSERT(((size_t)ctr_ctx & (CTX_ALIGN16 - 1ULL)) == 0ULL);
#endif

	ctr_ctx->ctr_flags = CTR_MODE;
	return (ctr_ctx);
}
