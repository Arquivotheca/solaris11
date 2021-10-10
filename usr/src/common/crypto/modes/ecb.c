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

/*
 * Algorithm independent ECB functions.
 */
int
ecb_cipher_contiguous_blocks(ecb_ctx_t *ctx, char *data, size_t length,
    crypto_data_t *out, size_t block_size,
    int (*cipher)(const void *ks, const uint8_t *pt, uint8_t *ct))
{
	size_t remainder = length;
	size_t need;
	uint8_t *datap = (uint8_t *)data;
	uint8_t *blockp;
	uint8_t *outbufp;
	void *iov_or_mp;
	offset_t offset;
	uint8_t *out_data_1;
	uint8_t *out_data_2;
	size_t out_data_1_len;

	/* Check to see if we have a full block */
	if (length + ctx->ecb_remainder_len < block_size) {
		/* accumulate bytes here and return */
		bcopy(datap,
		    (uint8_t *)ctx->ecb_remainder + ctx->ecb_remainder_len,
		    length);
		ctx->ecb_remainder_len += length;
		ctx->ecb_copy_to = datap;
		return (CRYPTO_SUCCESS);
	}

	outbufp = (uint8_t *)ctx->ecb_iv;
	if (out != NULL)
		crypto_init_ptrs(out, &iov_or_mp, &offset);

	do {
		if (ctx->ecb_remainder_len > 0) {
			/* Unprocessed data from last call. */
			need = block_size - ctx->ecb_remainder_len;

			if (need > remainder)
				return (CRYPTO_DATA_LEN_RANGE);

			bcopy(datap, &((uint8_t *)ctx->ecb_remainder)
			    [ctx->ecb_remainder_len], need);

			blockp = (uint8_t *)ctx->ecb_remainder;
		} else {
			blockp = datap;
		}

		if (out == NULL) { /* in-place (no output buffer) */
			cipher(ctx->ecb_keysched, blockp, blockp);
			ctx->ecb_lastp = blockp;

			if (ctx->ecb_remainder_len > 0) {
				bcopy(blockp, ctx->ecb_copy_to,
				    ctx->ecb_remainder_len);
				bcopy(blockp + ctx->ecb_remainder_len, datap,
				    need);
			}
		} else { /* 2 buffers */
			cipher(ctx->ecb_keysched, blockp, outbufp);
			crypto_get_ptrs(out, &iov_or_mp, &offset, &out_data_1,
			    &out_data_1_len, &out_data_2, block_size);

			/* copy block to where it belongs */
			bcopy(outbufp, out_data_1, out_data_1_len);
			if (out_data_2 != NULL) {
				bcopy(outbufp + out_data_1_len, out_data_2,
				    block_size - out_data_1_len);
			}
			/* update offset */
			out->cd_offset += block_size;
		}

		/* Update pointer to next block of data to be processed. */
		if (ctx->ecb_remainder_len != 0) {
			datap += need;
			ctx->ecb_remainder_len = 0;
		} else {
			datap += block_size;
		}

		remainder = (size_t)&data[length] - (size_t)datap;

		/* Incomplete last block. */
		if (remainder > 0 && remainder < block_size) {
			bcopy(datap, ctx->ecb_remainder, remainder);
			ctx->ecb_remainder_len = remainder;
			ctx->ecb_copy_to = datap;
			return (CRYPTO_SUCCESS);
		}
		ctx->ecb_copy_to = NULL;

	} while (remainder > 0);

	return (CRYPTO_SUCCESS);
}


/*
 * ecb_alloc_ctx()
 *
 * Allocate memory for ecb_ctx_t.
 *
 * Note: For AMD64, this structure SHOULD be aligned on a 0 mod 16 address.
 * This is NOT a requirement, but optimizes MMX assembly instructions, such
 * as MOVDQU.  Userland calloc() aligns 0 mod 16, but kernel function
 * kmem_zalloc() does not align.
 */
/* ARGSUSED */
void *
ecb_alloc_ctx(int kmflag)
{
	ecb_ctx_t *ecb_ctx;

	if ((ecb_ctx = CRYPTO_ZALLOC(sizeof (ecb_ctx_t), kmflag)) == NULL)
		return (NULL);

#ifdef	__amd64
	ASSERT(((size_t)ecb_ctx & (CTX_ALIGN16 - 1ULL)) == 0ULL);
#endif
	ecb_ctx->ecb_flags = ECB_MODE;
	return (ecb_ctx);
}
