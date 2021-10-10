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

/*
 * This file replaces the des_decrypt_contiguous_blocks() and
 * des_encrypt_contiguous_blocks() functions in des_modes.c with
 * Sparc T4 functions.  These functions execute Sparc T4 instructions
 * when it is detected that the processor has the capability.  Otherwise,
 * they execute generic hardware-independent software functions.
 *
 * I based the encrypt and decrypt functions here on similarly-named
 * functions in ../../modes/{ecb.c|cbc.c}, and added-in calls to optimized
 * assembly function calls.
 */

#include <sys/types.h>
#include <sys/byteorder.h>
#include <modes/modes.h>
#include "des_impl.h"

#ifdef _KERNEL
#include <kernel_fp_use.h>
#else
#include <strings.h>
#include <stdio.h>	/* printf */
#endif


/*
 *
 */
static int
des_crypt_contiguous_blocks(void *ctx, char *data, size_t length,
    crypto_data_t *out, yf_functions_t *funcs)
{
	common_ctx_t	*cctx = (common_ctx_t *)ctx;
	offset_t	offset = 0;
	uint8_t		*in;
	size_t		nr_bytes_written = 0, need, nbw;
	int		rv;
	boolean_t	need_loadkey = B_TRUE;
	boolean_t	out_bufs_allocated = B_FALSE;
	iovec_t		*out_bufs;
	uint_t		outbufnum, outbufind;
	iovec_t		outvecs[CRYPTO_DEFAULT_NUMBER_OF_IOVECS];

	if (length == 0)
		return (CRYPTO_SUCCESS);

	in = (uint8_t *)data;
	outbufind = 0;
	offset = 0;

	if (out != NULL) {
		if ((rv = crypto_init_outbufs(out, &outbufnum, &out_bufs,
		    outvecs, CRYPTO_DEFAULT_NUMBER_OF_IOVECS, &offset,
		    &out_bufs_allocated)) !=
		    CRYPTO_SUCCESS) {
			goto ret;
		}
	} else {
		out_bufs = outvecs;
		outvecs[0].iov_base = data;
		outvecs[0].iov_len = length;
		outbufnum = 1;
	}

	while (length > 0) {

		if (cctx->cc_remainder_len > 0) {

			if ((rv = funcs->process_initial_partial_block(ctx,
			    &in, out_bufs, &outbufind, &offset, &length,
			    funcs, &nr_bytes_written, &need_loadkey)) !=
			    CRYPTO_SUCCESS) {
				goto ret;
			}

			if (length == 0) {
				break;
			}
		}

		ASSERT(cctx->cc_remainder_len == 0);

		need = MIN(length, out_bufs[outbufind].iov_len - offset) /
		    DES_BLOCK_LEN;
		need = need * DES_BLOCK_LEN;

		if (need > 0) {
			if ((rv = funcs->process_contiguous_whole_blocks(
			    ctx, (uint8_t *)in,
			    (uint8_t *)(out_bufs[outbufind].iov_base) + offset,
			    need, funcs, &need_loadkey)) != CRYPTO_SUCCESS) {
				goto ret;
			}
			in += need;
			nr_bytes_written += need;
			length -= need;
			if (length == 0) {
				break;
			}
		}

		if ((offset + need) == out_bufs[outbufind].iov_len) {
			outbufind++;
			offset = 0;
			need = 0;
		} else {
			offset += need;
			need = out_bufs[outbufind].iov_len - offset;
		}

		if (need == 0) {
			if (length > DES_BLOCK_LEN) {
				continue;
			} else {
				need = length;
			}
		} else {
			need = MIN(need, length);
		}

		if (need != 0) {
			nbw = nr_bytes_written;
			if ((rv = funcs->process_last_partial_block(
			    ctx, (uint8_t *)in,
			    (uint8_t *)(out_bufs[outbufind].iov_base) + offset,
			    need, funcs, &nr_bytes_written, &need_loadkey)) !=
			    CRYPTO_SUCCESS) {
				goto ret;
			}
			nbw = nr_bytes_written - nbw;
			length -= need;
			in += need;
			offset += nbw;
			if (offset == out_bufs[outbufind].iov_len) {
				outbufind++;
				offset = 0;
			}
		}
	}

	if (out != NULL) {
		out->cd_offset += nr_bytes_written;
	}
	rv = CRYPTO_SUCCESS;

ret:
	if (out_bufs_allocated) {
		CRYPTO_FREE(out_bufs, sizeof (iovec_t) * outbufnum);
	}
	return (rv);
}

static int
des_block_process_initial_partial_block(void *ctx, uint64_t *ks, uint8_t **in,
    iovec_t *out_bufs, uint_t *outbufind, offset_t *offset, size_t *length,
    yf_functions_t *funcs, size_t *nr_bytes_written, boolean_t *need_loadkey)
{
	common_ctx_t	*cctx = (common_ctx_t *)ctx;
	uint64_t	*outbuf = cctx->cc_remainder;
	uint64_t	*asm_out;
	uint64_t	*iv = cctx->cc_iv;
	size_t		startpos, need, outneed, partlen;
	uint8_t		*out;
	boolean_t	direct_out;

	/*
	 * now cctx->cc_remainder contains the last few bytes of the
	 * input collected into a partial block and
	 * cctx->cc_remainder_len contains the length of this partial block
	 */
	startpos = cctx->cc_remainder_len;
	need = MIN(*length,  DES_BLOCK_LEN - startpos);
	bcopy(*in, (uchar_t *)outbuf + startpos, need);

	if (startpos + need == DES_BLOCK_LEN) {
		partlen = out_bufs[*outbufind].iov_len - *offset;
		out = (uint8_t *)(out_bufs[*outbufind].iov_base) + *offset;
		direct_out =
		    (((size_t)out & 7) == 0) && (partlen >= DES_BLOCK_LEN);
		if (direct_out) {
			/* LINTED E_BAD_PTR_CAST_ALIGN */
			asm_out = (uint64_t *)(out);
		} else {
			asm_out = outbuf;
		}

		if (*need_loadkey) {
			funcs->load_keys(ks);
		}

		funcs->contig_blocks(ks, outbuf, asm_out, DES_BLOCK_LEN, iv);

		if (direct_out) {
			*offset += DES_BLOCK_LEN;
		} else {
			outneed = DES_BLOCK_LEN;
			while (outneed >= partlen) {
				bcopy((uchar_t *)
				    outbuf + DES_BLOCK_LEN - outneed,
				    out, partlen);
				outneed -= partlen;
				(*outbufind)++;
				partlen = out_bufs[*outbufind].iov_len;
				out =
				    (uint8_t *)(out_bufs[*outbufind].iov_base);
				*offset = 0;
			}
			if (outneed > 0) {
				bcopy((uchar_t *)
				    outbuf + DES_BLOCK_LEN - outneed,
				    out, outneed);
				*offset = outneed;
			}
		}

		cctx->cc_remainder_len = 0;
		out += DES_BLOCK_LEN;
		*nr_bytes_written += DES_BLOCK_LEN;
		*need_loadkey = B_FALSE;
	} else {
		cctx->cc_remainder_len += need;
	}
	*length -= need;
	*in += need;

	return (CRYPTO_SUCCESS);
}


static int
des_encrypt_process_initial_partial_block(void *ctx, uint8_t **in,
    iovec_t *out_bufs, uint_t *outbufind, offset_t *offset, size_t *length,
    yf_functions_t *funcs, size_t *nr_bytes_written, boolean_t *need_loadkey)
{
	des_keysched_t	*des_key = ((common_ctx_t *)ctx)->cc_keysched;
	uint64_t	*ks = &(des_key->ksch_encrypt[0]);

	return (des_block_process_initial_partial_block(ctx,
	    ks, in, out_bufs, outbufind, offset, length, funcs,
	    nr_bytes_written, need_loadkey));
}


static int
des_decrypt_process_initial_partial_block(void *ctx, uint8_t **in,
    iovec_t *out_bufs, uint_t *outbufind, offset_t *offset, size_t *length,
    yf_functions_t *funcs, size_t *nr_bytes_written, boolean_t *need_loadkey)
{
	des_keysched_t	*des_key = ((common_ctx_t *)ctx)->cc_keysched;
	uint64_t	*ks = &(des_key->ksch_decrypt[0]);

	return (des_block_process_initial_partial_block(ctx,
	    ks, in, out_bufs, outbufind, offset, length, funcs,
	    nr_bytes_written, need_loadkey));
}


static int
des3_encrypt_process_initial_partial_block(void *ctx, uint8_t **in,
    iovec_t *out_bufs, uint_t *outbufind, offset_t *offset, size_t *length,
    yf_functions_t *funcs, size_t *nr_bytes_written, boolean_t *need_loadkey)
{
	des3_keysched_t	*des_key = ((common_ctx_t *)ctx)->cc_keysched;
	uint64_t	*ks = &(des_key->ksch_encrypt[0]);

	return (des_block_process_initial_partial_block(ctx,
	    ks, in, out_bufs, outbufind, offset, length, funcs,
	    nr_bytes_written, need_loadkey));
}


static int
des3_decrypt_process_initial_partial_block(void *ctx, uint8_t **in,
    iovec_t *out_bufs, uint_t *outbufind, offset_t *offset, size_t *length,
    yf_functions_t *funcs, size_t *nr_bytes_written, boolean_t *need_loadkey)
{
	des3_keysched_t	*des_key = ((common_ctx_t *)ctx)->cc_keysched;
	uint64_t	*ks = &(des_key->ksch_decrypt[0]);

	return (des_block_process_initial_partial_block(ctx,
	    ks, in, out_bufs, outbufind, offset, length, funcs,
	    nr_bytes_written, need_loadkey));
}


static int
des_block_process_contiguous_whole_blocks(void *ctx,
    uint64_t *ks, uint8_t *in, uint8_t *out, size_t length,
    yf_functions_t *funcs, boolean_t *need_loadkey)
{
	common_ctx_t	*cctx = (common_ctx_t *)ctx;
	uint64_t	*iv, *tmp = NULL, *asm_in, *asm_out;

	if ((((size_t)(in) & 7) != 0) || (((size_t)(out) & 7) != 0)) {
		if ((tmp = (uint64_t *)CRYPTO_ALLOC(length, KM_NOSLEEP))
		    == NULL) {
			return (CRYPTO_HOST_MEMORY);
		}
	}

	/* make sure that the buffers are 64-bit aligned */
	if (((size_t)(in) & 7) != 0) {
		asm_in = tmp;
		bcopy(in, asm_in, length);
		/* bcopy may overwrite the FP registers */
		*need_loadkey = B_TRUE;
	} else {
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		asm_in = (uint64_t *)(in);
	}
	if (((size_t)(out) & 7) != 0) {
		asm_out = tmp;
	} else {
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		asm_out = (uint64_t *)(out);
	}

	iv = cctx->cc_iv;

	/* load the keys into the floating point registers if needed */
	if (*need_loadkey) {
		funcs->load_keys(ks);
	}

	funcs->contig_blocks(ks, asm_in, asm_out, length, iv);

	if (((size_t)(out) & 7) != 0) {
		bcopy(asm_out, out, length);
		/* bcopy may overwrite the FP registers */
		*need_loadkey = B_TRUE;
	} else {
		*need_loadkey = B_FALSE;
	}

	if (tmp != NULL) {
		CRYPTO_FREE(tmp, length);
	}

	return (CRYPTO_SUCCESS);
}

static int
des_encrypt_process_contiguous_whole_blocks(void *ctx,
    uint8_t *in, uint8_t *out, size_t length, yf_functions_t *funcs,
    boolean_t *need_loadkey)
{
	des_keysched_t	*des_key = ((common_ctx_t *)ctx)->cc_keysched;
	uint64_t	*ks = &(des_key->ksch_encrypt[0]);

	return (des_block_process_contiguous_whole_blocks(ctx,
	    ks, in, out, length, funcs, need_loadkey));
}

static int
des_decrypt_process_contiguous_whole_blocks(void *ctx,
    uint8_t *in, uint8_t *out, size_t length, yf_functions_t *funcs,
    boolean_t *need_loadkey)
{
	des_keysched_t	*des_key = ((common_ctx_t *)ctx)->cc_keysched;
	uint64_t	*ks = &(des_key->ksch_decrypt[0]);

	return (des_block_process_contiguous_whole_blocks(ctx,
	    ks, in, out, length, funcs, need_loadkey));
}


static int
des3_encrypt_process_contiguous_whole_blocks(void *ctx,
    uint8_t *in, uint8_t *out, size_t length, yf_functions_t *funcs,
    boolean_t *need_loadkey)
{
	des3_keysched_t	*des_key = ((common_ctx_t *)ctx)->cc_keysched;
	uint64_t	*ks = &(des_key->ksch_encrypt[0]);

	return (des_block_process_contiguous_whole_blocks(ctx,
	    ks, in, out, length, funcs, need_loadkey));
}

static int
des3_decrypt_process_contiguous_whole_blocks(void *ctx,
    uint8_t *in, uint8_t *out, size_t length, yf_functions_t *funcs,
    boolean_t *need_loadkey)
{
	des3_keysched_t	*des_key = ((common_ctx_t *)ctx)->cc_keysched;
	uint64_t	*ks = &(des_key->ksch_decrypt[0]);

	return (des_block_process_contiguous_whole_blocks(ctx,
	    ks, in, out, length, funcs, need_loadkey));
}


static int
/* LINTED E_FUNC_ARG_UNUSED */
des_block_process_last_partial_block(void *ctx, uint8_t *in, uint8_t *out,
/* LINTED E_FUNC_ARG_UNUSED */
    size_t length, yf_functions_t *funcs, size_t *nr_bytes_written,
/* LINTED E_FUNC_ARG_UNUSED */
    boolean_t *need_loadkey)
{
	common_ctx_t	*cctx = (common_ctx_t *)ctx;
	uint64_t	*outbuf = cctx->cc_remainder;
	size_t		need;

	need = length;
	ASSERT(length < DES_BLOCK_LEN);
	bcopy(in, (uchar_t *)outbuf, need);

	cctx->cc_remainder_len = need;

	return (CRYPTO_SUCCESS);
}



#define	YF_DES_SET_FUNCS(key, mode)	funcs = &yf_##key##_##mode##_funcs;


static
yf_functions_t yf_des_ecb_encrypt_funcs = {
	des_encrypt_process_initial_partial_block,
	des_encrypt_process_contiguous_whole_blocks,
	des_block_process_last_partial_block,
	yf_des_load_keys,
	yf_des_ecb_crypt
};

static
yf_functions_t yf_des_ecb_decrypt_funcs = {
	des_decrypt_process_initial_partial_block,
	des_decrypt_process_contiguous_whole_blocks,
	des_block_process_last_partial_block,
	yf_des_load_keys,
	yf_des_ecb_crypt
};

static
yf_functions_t yf_des_cbc_encrypt_funcs = {
	des_encrypt_process_initial_partial_block,
	des_encrypt_process_contiguous_whole_blocks,
	des_block_process_last_partial_block,
	yf_des_load_keys,
	yf_des_cbc_encrypt
};

static
yf_functions_t yf_des_cbc_decrypt_funcs = {
	des_decrypt_process_initial_partial_block,
	des_decrypt_process_contiguous_whole_blocks,
	des_block_process_last_partial_block,
	yf_des_load_keys,
	yf_des_cbc_decrypt
};


static
yf_functions_t yf_des3_ecb_encrypt_funcs = {
	des3_encrypt_process_initial_partial_block,
	des3_encrypt_process_contiguous_whole_blocks,
	des_block_process_last_partial_block,
	yf_des3_load_keys,
	yf_des3_ecb_crypt
};


static
yf_functions_t yf_des3_ecb_decrypt_funcs = {
	des3_decrypt_process_initial_partial_block,
	des3_decrypt_process_contiguous_whole_blocks,
	des_block_process_last_partial_block,
	yf_des3_load_keys,
	yf_des3_ecb_crypt
};


static
yf_functions_t yf_des3_cbc_encrypt_funcs = {
	des3_encrypt_process_initial_partial_block,
	des3_encrypt_process_contiguous_whole_blocks,
	des_block_process_last_partial_block,
	yf_des3_load_keys,
	yf_des3_cbc_encrypt
};

static
yf_functions_t yf_des3_cbc_decrypt_funcs = {
	des3_decrypt_process_initial_partial_block,
	des3_decrypt_process_contiguous_whole_blocks,
	des_block_process_last_partial_block,
	yf_des3_load_keys,
	yf_des3_cbc_decrypt
};


/*
 * Encrypt multiple blocks of data with DES according to mode.
 */

int
des_encrypt_contiguous_blocks(void *ctx, char *data, size_t length,
    crypto_data_t *out)
{
	yf_functions_t	*funcs;
	des_ctx_t	*des_ctx = ctx;
	int		rv = CRYPTO_MECH_NOT_SUPPORTED;
	boolean_t	yf_des;
#ifdef	_KERNEL
	fp_save_t fp_save_buf;
	boolean_t save_fp;
#endif

#ifdef	_KERNEL
	save_fp = yf_des = yf_des_instructions_present();
	SAVE_FP;
#else
#ifdef HWCAP_DES
	yf_des = B_TRUE;
#else
	yf_des = B_FALSE;
#endif
#endif

	if (des_ctx->dc_flags & CBC_MODE) {
		if (yf_des) {
			if (des_ctx->dc_flags & DES3_STRENGTH) {
				YF_DES_SET_FUNCS(des3, cbc_encrypt);
			} else {
				YF_DES_SET_FUNCS(des, cbc_encrypt);
			}
			rv = des_crypt_contiguous_blocks(ctx,
			    data, length, out, funcs);
		} else {
			if (des_ctx->dc_flags & DES3_STRENGTH) {
				rv = cbc_encrypt_contiguous_blocks(ctx,
				    data, length, out, DES_BLOCK_LEN,
				    des3_encrypt_block, des_copy_block,
				    des_xor_block);
			} else {
				rv = cbc_encrypt_contiguous_blocks(ctx,
				    data, length, out, DES_BLOCK_LEN,
				    des_encrypt_block, des_copy_block,
				    des_xor_block);
			}
		}
	} else if (des_ctx->dc_flags & ECB_MODE) {
		if (yf_des) {
			if (des_ctx->dc_flags & DES3_STRENGTH) {
				YF_DES_SET_FUNCS(des3, ecb_encrypt);
			} else {
				YF_DES_SET_FUNCS(des, ecb_encrypt);
			}
			rv = des_crypt_contiguous_blocks(ctx,
			    data, length, out, funcs);
		} else {
			if (des_ctx->dc_flags & DES3_STRENGTH) {
				rv = ecb_cipher_contiguous_blocks(ctx,
				    data, length, out, DES_BLOCK_LEN,
				    des3_encrypt_block);
			} else {
				rv = ecb_cipher_contiguous_blocks(ctx,
				    data, length, out, DES_BLOCK_LEN,
				    des_encrypt_block);
			}
		}
	}

#ifdef _KERNEL
	RESTORE_FP;
#endif

	return (rv);
}


/*
 * Decrypt multiple blocks of data with DES according to mode.
 */
int
des_decrypt_contiguous_blocks(void *ctx, char *data, size_t length,
    crypto_data_t *out)
{
	yf_functions_t	*funcs;
	des_ctx_t	*des_ctx = ctx;
	int		rv = CRYPTO_MECH_NOT_SUPPORTED;
	boolean_t	yf_des;
#ifdef	_KERNEL
	fp_save_t fp_save_buf;
	boolean_t save_fp;
#endif

#ifdef	_KERNEL
	save_fp = yf_des = yf_des_instructions_present();
	SAVE_FP;
#else
#ifdef HWCAP_DES
	yf_des = B_TRUE;
#else
	yf_des = B_FALSE;
#endif
#endif

	if (des_ctx->dc_flags & CBC_MODE) {
		if (yf_des) {
			if (des_ctx->dc_flags & DES3_STRENGTH) {
				YF_DES_SET_FUNCS(des3, cbc_decrypt);
			} else {
				YF_DES_SET_FUNCS(des, cbc_decrypt);
			}
			rv = des_crypt_contiguous_blocks(ctx,
			    data, length, out, funcs);
		} else {
			if (des_ctx->dc_flags & DES3_STRENGTH) {
				rv = cbc_decrypt_contiguous_blocks(ctx,
				    data, length, out, DES_BLOCK_LEN,
				    des3_decrypt_block, des_copy_block,
				    des_xor_block);
			} else {
				rv = cbc_decrypt_contiguous_blocks(ctx,
				    data, length, out, DES_BLOCK_LEN,
				    des_decrypt_block, des_copy_block,
				    des_xor_block);
			}
		}
	} else if (des_ctx->dc_flags & ECB_MODE) {
		if (yf_des) {
			if (des_ctx->dc_flags & DES3_STRENGTH) {
				YF_DES_SET_FUNCS(des3, ecb_decrypt);
			} else {
				YF_DES_SET_FUNCS(des, ecb_decrypt);
			}
			rv = des_crypt_contiguous_blocks(ctx,
			    data, length, out, funcs);
		} else {
			if (des_ctx->dc_flags & DES3_STRENGTH) {
				rv = ecb_cipher_contiguous_blocks(ctx,
				    data, length,
				    out, DES_BLOCK_LEN, des3_decrypt_block);
				if (rv == CRYPTO_DATA_LEN_RANGE)
					rv = CRYPTO_ENCRYPTED_DATA_LEN_RANGE;
			} else {
				rv = ecb_cipher_contiguous_blocks(ctx,
				    data, length,
				    out, DES_BLOCK_LEN, des_decrypt_block);
				if (rv == CRYPTO_DATA_LEN_RANGE)
					rv = CRYPTO_ENCRYPTED_DATA_LEN_RANGE;
			}
		}
	}

#ifdef _KERNEL
	RESTORE_FP;
#endif

	return (rv);
}
