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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <modes/modes.h>
#include "des_impl.h"
#ifndef	_KERNEL
#include <stdlib.h>
#endif	/* !_KERNEL */

#if defined(sun4v) && defined(_KERNEL)
#include <kernel_fp_use.h>
#endif

/* Copy a 8-byte DES block from "in" to "out" */
void
des_copy_block(uint8_t *in, uint8_t *out)
{
	if (IS_P2ALIGNED(in, sizeof (uint32_t)) &&
	    IS_P2ALIGNED(out, sizeof (uint32_t))) {
		/* LINTED: pointer alignment */
		*(uint32_t *)&out[0] = *(uint32_t *)&in[0];
		/* LINTED: pointer alignment */
		*(uint32_t *)&out[4] = *(uint32_t *)&in[4];
	} else {
		DES_COPY_BLOCK(in, out);
	}
}

/* XOR block of data into dest */
void
des_xor_block(uint8_t *data, uint8_t *dst)
{
	if (IS_P2ALIGNED(dst, sizeof (uint32_t)) &&
	    IS_P2ALIGNED(data, sizeof (uint32_t))) {
		/* LINTED: pointer alignment */
		*(uint32_t *)&dst[0] ^=
		    /* LINTED: pointer alignment */
		    *(uint32_t *)&data[0];
		    /* LINTED: pointer alignment */
		*(uint32_t *)&dst[4] ^=
		    /* LINTED: pointer alignment */
		    *(uint32_t *)&data[4];
	} else {
		DES_XOR_BLOCK(data, dst);
	}
}

#ifndef sun4v

/*
 * Encrypt multiple blocks of data according to mode.
 */
int
des_encrypt_contiguous_blocks(void *ctx, char *data, size_t length,
    crypto_data_t *out)
{
	des_ctx_t *des_ctx = ctx;
	int rv;

	if (des_ctx->dc_flags & DES3_STRENGTH) {
		if (des_ctx->dc_flags & CBC_MODE) {
			rv = cbc_encrypt_contiguous_blocks(ctx, data,
			    length, out, DES_BLOCK_LEN, des3_encrypt_block,
			    des_copy_block, des_xor_block);
		} else {
			rv = ecb_cipher_contiguous_blocks(ctx, data, length,
			    out, DES_BLOCK_LEN, des3_encrypt_block);
		}
	} else {
		if (des_ctx->dc_flags & CBC_MODE) {
			rv = cbc_encrypt_contiguous_blocks(ctx, data,
			    length, out, DES_BLOCK_LEN, des_encrypt_block,
			    des_copy_block, des_xor_block);
		} else {
			rv = ecb_cipher_contiguous_blocks(ctx, data, length,
			    out, DES_BLOCK_LEN, des_encrypt_block);
		}
	}

	return (rv);
}

/*
 * Decrypt multiple blocks of data according to mode.
 */
int
des_decrypt_contiguous_blocks(void *ctx, char *data, size_t length,
    crypto_data_t *out)
{
	des_ctx_t *des_ctx = ctx;
	int rv;

	if (des_ctx->dc_flags & DES3_STRENGTH) {
		if (des_ctx->dc_flags & CBC_MODE) {
			rv = cbc_decrypt_contiguous_blocks(ctx, data,
			    length, out, DES_BLOCK_LEN, des3_decrypt_block,
			    des_copy_block, des_xor_block);
		} else {
			rv = ecb_cipher_contiguous_blocks(ctx, data, length,
			    out, DES_BLOCK_LEN, des3_decrypt_block);
			if (rv == CRYPTO_DATA_LEN_RANGE)
				rv = CRYPTO_ENCRYPTED_DATA_LEN_RANGE;
		}
	} else {
		if (des_ctx->dc_flags & CBC_MODE) {
			rv = cbc_decrypt_contiguous_blocks(ctx, data,
			    length, out, DES_BLOCK_LEN, des_decrypt_block,
			    des_copy_block, des_xor_block);
		} else {
			rv = ecb_cipher_contiguous_blocks(ctx, data, length,
			    out, DES_BLOCK_LEN, des_decrypt_block);
			if (rv == CRYPTO_DATA_LEN_RANGE)
				rv = CRYPTO_ENCRYPTED_DATA_LEN_RANGE;
		}
	}

	return (rv);
}
#endif /* !sun4v */
