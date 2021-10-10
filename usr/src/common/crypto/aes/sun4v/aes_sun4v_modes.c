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
 * This file replaces the aes_decrypt_contiguous_blocks() and
 * aes_encrypt_contiguous_blocks() functions in aes_modes.c with
 * Sparc T4 functions.  These functions execute Sparc T4 instructions
 * when it is detected that the processor has the capability.  Otherwise,
 * they execute generic hardware-independent software functions.
 *
 * I based the encrypt and decrypt functions here on similarly-named
 * functions in ../../modes/{ecb.c|cbc.c|ctr.c}, and added-in
 * calls to optimized assembly function calls.
 */

#include <sys/types.h>
#include <sys/byteorder.h>
#include <modes/modes.h>
#include "aes_impl.h"

#ifndef _KERNEL
#include <strings.h>
#endif


void
aes_cbcmac_multiblock(void *ctx, uint64_t *input, size_t len)
{
	ccm_ctx_t	*cctx = (ccm_ctx_t *)ctx;
	aes_key_t	*aes_key = cctx->ccm_keysched;
	uint64_t	*ks = &(aes_key->encr_ks.ks64[0]);
#ifndef HWCAP_AES
	int		i;
#endif

#ifdef HWCAP_AES
	switch (aes_key->nr) {
	case 10:
		yf_aes128_load_keys_for_encrypt(ks);
		yf_aes128_cbc_mac(ks, input, cctx->ccm_mac_buf, len);
		break;
	case 12:
		yf_aes192_load_keys_for_encrypt(ks);
		yf_aes192_cbc_mac(ks, input, cctx->ccm_mac_buf, len);
		break;
	case 14:
		yf_aes256_load_keys_for_encrypt(ks);
		yf_aes256_cbc_mac(ks, input, cctx->ccm_mac_buf, len);
		break;
	default:
		return;
	}
#else

#ifdef	_KERNEL
	if (yf_aes_instructions_present()) {
		switch (aes_key->nr) {
		case 10:
			yf_aes128_load_keys_for_encrypt(ks);
			yf_aes128_cbc_mac(ks, input, cctx->ccm_mac_buf, len);
			break;
		case 12:
			yf_aes192_load_keys_for_encrypt(ks);
			yf_aes192_cbc_mac(ks, input, cctx->ccm_mac_buf, len);
			break;
		case 14:
			yf_aes256_load_keys_for_encrypt(ks);
			yf_aes256_cbc_mac(ks, input, cctx->ccm_mac_buf, len);
			break;
		default:
			return;
		}
	} else {
#endif
		for (i = 0; i < len; i++) {
			uint8_t	*mac_buf = (uint8_t *)(cctx->ccm_mac_buf);

			cctx->ccm_mac_buf[0] ^= input[2 * i];
			cctx->ccm_mac_buf[1] ^= input[2 * i + 1];
			(void) aes_encrypt_block(cctx->ccm_keysched,
			    mac_buf, mac_buf);
		}
#ifdef	_KERNEL
	}
#endif
#endif /* HWCAP_AES */
}


/*
 *
 */
static int
aes_crypt_contiguous_blocks(void *ctx, char *data, size_t length,
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
		    AES_BLOCK_LEN;
		need = need * AES_BLOCK_LEN;

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
			if (length > AES_BLOCK_LEN) {
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
aes_block_process_initial_partial_block(void *ctx, uint8_t **in,
    iovec_t *out_bufs, uint_t *outbufind, offset_t *offset, size_t *length,
    yf_functions_t *funcs, size_t *nr_bytes_written, boolean_t *need_loadkey)
{
	common_ctx_t	*cctx = (common_ctx_t *)ctx;
	uint64_t	*outbuf = cctx->cc_remainder;
	uint64_t	*asm_out;
	aes_key_t	*aes_key = cctx->cc_keysched;
	uint64_t	*ks = &(aes_key->encr_ks.ks64[0]);
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
	need = MIN(*length,  AES_BLOCK_LEN - startpos);
	bcopy(*in, (uchar_t *)outbuf + startpos, need);

	if (startpos + need == AES_BLOCK_LEN) {
		partlen = out_bufs[*outbufind].iov_len - *offset;
		out = (uint8_t *)(out_bufs[*outbufind].iov_base) + *offset;
		direct_out =
		    (((size_t)out & 7) == 0) && (partlen >= AES_BLOCK_LEN);
		if (direct_out) {
			/* LINTED E_BAD_PTR_CAST_ALIGN */
			asm_out = (uint64_t *)(out);
		} else {
			asm_out = outbuf;
		}

		if (*need_loadkey) {
			funcs->load_keys(ks);
			*need_loadkey = B_FALSE;
		}

		funcs->contig_blocks(ks, outbuf, asm_out, AES_BLOCK_LEN, iv);

		if (direct_out) {
			*offset += AES_BLOCK_LEN;
		} else {
			outneed = AES_BLOCK_LEN;
			while (outneed >= partlen) {
				bcopy((uchar_t *)
				    outbuf + AES_BLOCK_LEN - outneed,
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
				    outbuf + AES_BLOCK_LEN - outneed,
				    out, outneed);
				*offset = outneed;
			}
		}

		cctx->cc_remainder_len = 0;
		out += AES_BLOCK_LEN;
		*nr_bytes_written += AES_BLOCK_LEN;
	} else {
		cctx->cc_remainder_len += need;
	}
	*length -= need;
	*in += need;

	return (CRYPTO_SUCCESS);
}

static int
aes_block_process_contiguous_whole_blocks(void *ctx, uint8_t *in, uint8_t *out,
    size_t length, yf_functions_t *funcs, boolean_t *need_loadkey)
{
	common_ctx_t	*cctx = (common_ctx_t *)ctx;
	uint64_t	*iv, *tmp = NULL, *asm_in, *asm_out;
	aes_key_t	*aes_key = cctx->cc_keysched;
	uint64_t	*ks = &(aes_key->encr_ks.ks64[0]);

	if ((((size_t)(in) & 7) != 0) || (((size_t)(out) & 7) != 0)) {
		if ((tmp = (uint64_t *)CRYPTO_ALLOC(length, KM_NOSLEEP))
		    == NULL) {
			return (CRYPTO_HOST_MEMORY);
		}
	}

	/* LINTED E_BAD_PTR_CAST_ALIGN */
	asm_in = (uint64_t *)(in);
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	asm_out = (uint64_t *)(out);
	if (((size_t)(in) & 7) != 0) {
		asm_in = tmp;
		bcopy(in, asm_in, length);
		/* bcopy may overwrite the FP registers */
		*need_loadkey = B_TRUE;
	}
	if (((size_t)(out) & 7) != 0) {
		asm_out = tmp;
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
/* LINTED E_FUNC_ARG_UNUSED */
aes_block_process_last_partial_block(void *ctx, uint8_t *in, uint8_t *out,
/* LINTED E_FUNC_ARG_UNUSED */
    size_t length, yf_functions_t *funcs, size_t *nr_bytes_written,
/* LINTED E_FUNC_ARG_UNUSED */
    boolean_t *need_loadkey)
{
	common_ctx_t	*cctx = (common_ctx_t *)ctx;
	uint64_t	*outbuf = cctx->cc_remainder;
	size_t		need;

	need = length;
	ASSERT(length < AES_BLOCK_LEN);
	bcopy(in, (uchar_t *)outbuf, need);

	cctx->cc_remainder_len = need;

	return (CRYPTO_SUCCESS);
}


#define	IPB_CFB128_ENCR	1
#define	IPB_CFB128_DECR	2
#define	IPB_CTR		3
#define	IPB_GCM		4
#define	IPB_CCM		5

static int
aes_stream_process_initial_partial_block(void *ctx, uint8_t **in,
    iovec_t *out_bufs, uint_t *outbufind, offset_t *offset, size_t *length,
/* LINTED E_FUNC_ARG_UNUSED */
    size_t *nr_bytes_written, boolean_t *need_loadkey, int mode)
{
	common_ctx_t	*cctx = (common_ctx_t *)ctx;
	uint64_t	input_buf[2] = {0, 0};
	uint64_t	*outbuf = cctx->cc_remainder;
	size_t		startpos, need, partlen, outneed;
	uint8_t		*out;

	/*
	 * now cctx->cc_remainder contains the IV xored with the
	 * last partial block of the input so far and
	 * cctx->cc_remainder_len contains the length of this partial block
	 */
	startpos = cctx->cc_remainder_len;
	need = MIN(*length,  AES_BLOCK_LEN - startpos);

	if (mode == IPB_CFB128_DECR) {
		bcopy(*in, (uchar_t *)(cctx->cc_iv) + startpos, need);
		outbuf[0] ^= cctx->cc_iv[0];
		outbuf[1] ^= cctx->cc_iv[1];
	} else {
		bcopy(*in, (uchar_t *)input_buf + startpos, need);
		outbuf[0] ^= input_buf[0];
		outbuf[1] ^= input_buf[1];
	}

	outneed = need;
	partlen = out_bufs[*outbufind].iov_len - *offset;
	out = (uint8_t *)(out_bufs[*outbufind].iov_base) + *offset;

	while (outneed >= partlen) {
		bcopy((uchar_t *)outbuf + startpos, out, partlen);
		outneed -= partlen;
		startpos += partlen;
		(*outbufind)++;
		partlen = out_bufs[*outbufind].iov_len;
		out = (uint8_t *)(out_bufs[*outbufind].iov_base);
		*offset = 0;
	}

	if (outneed > 0) {
		bcopy((uchar_t *)
		    outbuf + startpos, out, outneed);
		*offset += outneed;
	}

	if (startpos + need == AES_BLOCK_LEN) {
		cctx->cc_remainder_len = 0;
		if (mode == IPB_CFB128_ENCR) {
			cctx->cc_iv[0] = cctx->cc_remainder[0];
			cctx->cc_iv[1] = cctx->cc_remainder[1];
		}
	} else {
		cctx->cc_remainder_len += need;
	}

	*length -= need;
	*in += need;
	*nr_bytes_written += need;

	return (CRYPTO_SUCCESS);
}

static int
aes_gcm_encr_process_initial_partial_block(void *ctx, uint8_t **in,
    iovec_t *out_bufs, uint_t *outbufind, offset_t *offset, size_t *length,
/* LINTED E_FUNC_ARG_UNUSED */
    yf_functions_t *funcs, size_t *nr_bytes_written, boolean_t *need_loadkey)
{
	common_ctx_t	*cctx = (common_ctx_t *)ctx;
	size_t		startpos, need;
	int		rv;

	startpos = cctx->cc_remainder_len;
	need = MIN(*length,  AES_BLOCK_LEN - startpos);

	rv = aes_stream_process_initial_partial_block(ctx, in, out_bufs,
	    outbufind, offset, length, nr_bytes_written, need_loadkey,
	    IPB_CTR);
	if (rv == CRYPTO_SUCCESS) {
		if (startpos + need == AES_BLOCK_LEN) {
			ghash_multiblock(ctx, cctx->cc_remainder, 1);
		}
	}

	return (rv);
}

static int
aes_ccm_encr_process_initial_partial_block(void *ctx, uint8_t **in,
    iovec_t *out_bufs, uint_t *outbufind, offset_t *offset, size_t *length,
    yf_functions_t *funcs, size_t *nr_bytes_written, boolean_t *need_loadkey)
{
	ccm_ctx_t	*cctx = (ccm_ctx_t *)ctx;
	aes_key_t	*aes_key = cctx->ccm_keysched;
	uint64_t	*ks = &(aes_key->encr_ks.ks64[0]);
	size_t		startpos, need;
	int		rv;

	startpos = cctx->ccm_remainder_len;
	need = MIN(*length,  AES_BLOCK_LEN - startpos);

	bcopy(*in, ((char *)(cctx->ccm_mac_input_buf)) + startpos, need);

	if (startpos + need == AES_BLOCK_LEN) {
		if (*need_loadkey) {
			funcs->load_keys(ks);
			*need_loadkey = B_FALSE;
		}
		funcs->mac(ks, cctx->ccm_mac_input_buf, cctx->ccm_mac_buf, 1);
	}

	rv = aes_stream_process_initial_partial_block(ctx, in, out_bufs,
	    outbufind, offset, length, nr_bytes_written, need_loadkey,
	    IPB_CTR);

	return (rv);
}

static int
aes_ctr_process_initial_partial_block(void *ctx, uint8_t **in,
    iovec_t *out_bufs, uint_t *outbufind, offset_t *offset, size_t *length,
/* LINTED E_FUNC_ARG_UNUSED */
    yf_functions_t *funcs, size_t *nr_bytes_written, boolean_t *need_loadkey)
{
	return (aes_stream_process_initial_partial_block(ctx, in, out_bufs,
	    outbufind, offset, length, nr_bytes_written, need_loadkey,
	    IPB_CTR));
}

static int
aes_cfb128_encr_process_initial_partial_block(void *ctx, uint8_t **in,
    iovec_t *out_bufs, uint_t *outbufind, offset_t *offset, size_t *length,
/* LINTED E_FUNC_ARG_UNUSED */
    yf_functions_t *funcs, size_t *nr_bytes_written, boolean_t *need_loadkey)
{
	return (aes_stream_process_initial_partial_block(ctx, in, out_bufs,
	    outbufind, offset, length, nr_bytes_written, need_loadkey,
	    IPB_CFB128_ENCR));
}

static int
aes_cfb128_decr_process_initial_partial_block(void *ctx, uint8_t **in,
    iovec_t *out_bufs, uint_t *outbufind, offset_t *offset, size_t *length,
/* LINTED E_FUNC_ARG_UNUSED */
    yf_functions_t *funcs, size_t *nr_bytes_written, boolean_t *need_loadkey)
{
	return (aes_stream_process_initial_partial_block(ctx, in, out_bufs,
	    outbufind, offset, length, nr_bytes_written, need_loadkey,
	    IPB_CFB128_DECR));
}

static void
aes_ctr_advance_cb(void *ctx)
{
	ctr_ctx_t	*ctr_ctx = (ctr_ctx_t *)ctx;
	uint64_t	lower_counter, upper_counter;
	uint64_t	mask;

	mask = ctr_ctx->ctr_lower_mask;
	lower_counter = ntohll(ctr_ctx->ctr_cb[1] & mask);
	lower_counter = htonll(lower_counter + 1) & mask;
	ctr_ctx->ctr_cb[1] = (ctr_ctx->ctr_cb[1] & ~mask) | lower_counter;

	/* wrap around */
	if ((lower_counter == 0) && ((ctr_ctx->ctr_lower_mask + 1) == 0)) {
		mask = ctr_ctx->ctr_upper_mask;
		upper_counter = ntohll(ctr_ctx->ctr_cb[0] & mask);
		upper_counter = htonll(upper_counter + 1) & mask;
		upper_counter &= ctr_ctx->ctr_upper_mask;
		ctr_ctx->ctr_cb[0] =
		    (ctr_ctx->ctr_cb[0] & ~mask) | upper_counter;
	}
}


static int
aes_ctr_gcm_ccm_process_contiguous_whole_blocks(void *ctx, uint8_t *in,
    uint8_t *out, size_t length, yf_functions_t *funcs,
    boolean_t *need_loadkey, int mode)
{
	ctr_ctx_t	*ctr_ctx = (ctr_ctx_t *)ctx;
	size_t		nr_blocks = length / AES_BLOCK_LEN;
	uint64_t	counter, mask, last_counter, run_len;
	uint64_t	*iv, *tmp = NULL, *asm_in, *asm_out;
	aes_key_t	*aes_key = ctr_ctx->ctr_keysched;
	uint64_t	*ks = &(aes_key->encr_ks.ks64[0]);

	if (length < AES_BLOCK_LEN) {
		return (CRYPTO_SUCCESS);
	}

	mask = ctr_ctx->ctr_lower_mask;

	if ((ctr_ctx->ctr_upper_mask == 0) && (nr_blocks  > mask)) {
		return (CRYPTO_DATA_LEN_RANGE);
	}

	if ((((size_t)(in) & 7) != 0) || (((size_t)(out) & 7) != 0)) {
		if ((tmp = (uint64_t *)CRYPTO_ALLOC(length, KM_NOSLEEP)) ==
		    NULL) {
			return (CRYPTO_HOST_MEMORY);
		}
	}

	/* LINTED E_BAD_PTR_CAST_ALIGN */
	asm_in = (uint64_t *)(in);
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	asm_out = (uint64_t *)(out);
	if (((size_t)(in) & 7) != 0) {
		asm_in = tmp;
		bcopy(in, asm_in, length);
		/* bcopy may overwrite the FP registers */
		*need_loadkey = B_TRUE;
	}
	if (((size_t)(out) & 7) != 0) {
		asm_out = tmp;
	}

	/* load the keys into the floating point registers if needed */
	if (*need_loadkey) {
		funcs->load_keys(ks);
		*need_loadkey = B_FALSE;
	}

	if (mode == IPB_CCM) {
		funcs->mac(ks,
		    asm_in, ((ccm_ctx_t *)ctx)->ccm_mac_buf, nr_blocks);
	}

	iv = ctr_ctx->ctr_cb;
	run_len = 0;

	/* compute whether the counter will wrap around */
	counter = ntohll(ctr_ctx->ctr_cb[1] & mask);
	last_counter = counter + nr_blocks;

	if ((last_counter > mask) || (last_counter < nr_blocks)) {
		run_len = (mask - counter + 1) * AES_BLOCK_LEN;
		funcs->contig_blocks(ks, asm_in, asm_out, run_len, iv);

		/* fix the next counter value */
		if (mask != ~(uint64_t)0) {
			iv[1] -= (mask + 1);
		} else if (ctr_ctx->ctr_upper_mask != 0) {
			mask = ctr_ctx->ctr_upper_mask;
			counter = iv[0];
			iv[0] = (counter & ~mask) |
			    ((counter + 1) & mask);
		}
	}

	if (length - run_len > 0) {
		/* this time the counter cannot wrap around */
		funcs->contig_blocks(ks, asm_in + (run_len / sizeof (uint64_t)),
		    asm_out + (run_len / sizeof (uint64_t)),
		    length - run_len, iv);
	}

	if (mode == IPB_GCM) {
		ghash_multiblock(ctx, asm_out, length / AES_BLOCK_LEN);
	}

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
aes_ctr_process_contiguous_whole_blocks(void *ctx, uint8_t *in, uint8_t *out,
    size_t length, yf_functions_t *funcs, boolean_t *need_loadkey)
{
	return (aes_ctr_gcm_ccm_process_contiguous_whole_blocks(ctx, in, out,
	    length, funcs, need_loadkey, IPB_CTR));
}

static int
aes_gcm_process_contiguous_whole_blocks(void *ctx, uint8_t *in, uint8_t *out,
    size_t length, yf_functions_t *funcs, boolean_t *need_loadkey)
{
	return (aes_ctr_gcm_ccm_process_contiguous_whole_blocks(ctx, in, out,
	    length, funcs, need_loadkey, IPB_GCM));
}


static int
aes_ccm_process_contiguous_whole_blocks(void *ctx, uint8_t *in, uint8_t *out,
    size_t length, yf_functions_t *funcs, boolean_t *need_loadkey)
{
	return (aes_ctr_gcm_ccm_process_contiguous_whole_blocks(ctx, in, out,
	    length, funcs, need_loadkey, IPB_CCM));
}


static int
aes_ctr_process_last_partial_block(void *ctx, uint8_t *in, uint8_t *out,
    size_t length, yf_functions_t *funcs, size_t *nr_bytes_written,
    boolean_t *need_loadkey)
{
	common_ctx_t	*cctx = (common_ctx_t *)ctx;
	uint64_t	input_buf[2] = {0, 0};
	uint64_t	tmp;
	uint64_t	*outbuf = cctx->cc_remainder;
	size_t		need;
	aes_key_t	*aes_key = cctx->cc_keysched;
	uint64_t	*ks = &(aes_key->encr_ks.ks64[0]);
	uint64_t	*cb = cctx->cc_iv;

	need = length;
	ASSERT(length < AES_BLOCK_LEN);
	bcopy(in, (uchar_t *)input_buf, need);

	tmp = cb[1];
	if (*need_loadkey) {
		funcs->load_keys(ks);
		*need_loadkey = B_FALSE;
	}
	funcs->contig_blocks(ks, input_buf, outbuf, AES_BLOCK_LEN, cb);
	cb[1] = tmp;
	aes_ctr_advance_cb(ctx);

	*need_loadkey = B_FALSE;
	bcopy((uchar_t *)outbuf, out, need);
	cctx->cc_remainder_len = need;
	/*
	 * now cctx->cc_remainder contains the IV xored with the
	 * last partial block of the input so far and
	 * cctx->cc_remainder_len contains the length of this partial block
	 */
	*nr_bytes_written += need;

	return (CRYPTO_SUCCESS);
}


static int
aes_ccm_process_last_partial_block(void *ctx, uint8_t *in, uint8_t *out,
    size_t length, yf_functions_t *funcs, size_t *nr_bytes_written,
    boolean_t *need_loadkey)
{
	ccm_ctx_t	*ccm_ctx = (ccm_ctx_t *)ctx;
	uint64_t	*input_buf = ccm_ctx->ccm_mac_input_buf;
	uint64_t	tmp;
	uint64_t	*outbuf = ccm_ctx->ccm_remainder;
	size_t		need;
	aes_key_t	*aes_key = ccm_ctx->ccm_keysched;
	uint64_t	*ks = &(aes_key->encr_ks.ks64[0]);
	uint64_t	*cb = ccm_ctx->ccm_cb;

	need = length;
	ASSERT(length < AES_BLOCK_LEN);
	input_buf[0] = input_buf[1] = 0;
	bcopy(in, (uchar_t *)input_buf, need);

	tmp = cb[1];
	if (*need_loadkey) {
		funcs->load_keys(ks);
		*need_loadkey = B_FALSE;
	}
	funcs->contig_blocks(ks, input_buf, outbuf, AES_BLOCK_LEN, cb);
	cb[1] = tmp;
	aes_ctr_advance_cb(ctx);

	bcopy((uchar_t *)outbuf, out, need);
	ccm_ctx->ccm_remainder_len = need;
	/*
	 * now cctx->cc_remainder contains the IV xored with the
	 * last partial block of the input so far and
	 * cctx->cc_remainder_len contains the length of this partial block
	 */
	*nr_bytes_written += need;

	return (CRYPTO_SUCCESS);
}


static int
aes_cfb128_process_last_partial_block(void *ctx, uint8_t *in, uint8_t *out,
    size_t length, yf_functions_t *funcs, size_t *nr_bytes_written,
    boolean_t *need_loadkey)
{
	common_ctx_t	*cctx = (common_ctx_t *)ctx;
	uint64_t	input_buf[2] = {0, 0};
	uint64_t	*outbuf = cctx->cc_remainder;
	size_t		need;
	aes_key_t	*aes_key = cctx->cc_keysched;
	uint64_t	*ks = &(aes_key->encr_ks.ks64[0]);
	uint64_t	*iv = cctx->cc_iv;

	need = length;
	ASSERT(length < AES_BLOCK_LEN);
	bcopy(in, (uchar_t *)input_buf, need);

	if (*need_loadkey) {
		funcs->load_keys(ks);
		*need_loadkey = B_FALSE;
	}
	funcs->contig_blocks(ks, input_buf, outbuf, AES_BLOCK_LEN, iv);

	bcopy((uchar_t *)outbuf, out, need);
	cctx->cc_remainder_len = need;
	/*
	 * now cctx->cc_remainder contains the IV xored with the
	 * last partial block of the input so far and
	 * cctx->cc_remainder_len contains the length of this partial block
	 */
	*nr_bytes_written += need;

	return (CRYPTO_SUCCESS);
}


static
yf_functions_t yf_aes128_ecb_encr_funcs = {
	aes_block_process_initial_partial_block,
	aes_block_process_contiguous_whole_blocks,
	aes_block_process_last_partial_block,
	yf_aes128_load_keys_for_encrypt,
	yf_aes128_ecb_encrypt,
	NULL
};

static
yf_functions_t yf_aes192_ecb_encr_funcs = {
	aes_block_process_initial_partial_block,
	aes_block_process_contiguous_whole_blocks,
	aes_block_process_last_partial_block,
	yf_aes192_load_keys_for_encrypt,
	yf_aes192_ecb_encrypt,
	NULL
};

static
yf_functions_t yf_aes256_ecb_encr_funcs = {
	aes_block_process_initial_partial_block,
	aes_block_process_contiguous_whole_blocks,
	aes_block_process_last_partial_block,
	yf_aes256_load_keys_for_encrypt,
	yf_aes256_ecb_encrypt,
	NULL
};


static
yf_functions_t yf_aes128_ecb_decr_funcs = {
	aes_block_process_initial_partial_block,
	aes_block_process_contiguous_whole_blocks,
	aes_block_process_last_partial_block,
	yf_aes128_load_keys_for_decrypt,
	yf_aes128_ecb_decrypt,
	NULL
};

static
yf_functions_t yf_aes192_ecb_decr_funcs = {
	aes_block_process_initial_partial_block,
	aes_block_process_contiguous_whole_blocks,
	aes_block_process_last_partial_block,
	yf_aes192_load_keys_for_decrypt,
	yf_aes192_ecb_decrypt,
	NULL
};

static
yf_functions_t yf_aes256_ecb_decr_funcs = {
	aes_block_process_initial_partial_block,
	aes_block_process_contiguous_whole_blocks,
	aes_block_process_last_partial_block,
	yf_aes256_load_keys_for_decrypt,
	yf_aes256_ecb_decrypt,
	NULL
};


static
yf_functions_t yf_aes128_cbc_encr_funcs = {
	aes_block_process_initial_partial_block,
	aes_block_process_contiguous_whole_blocks,
	aes_block_process_last_partial_block,
	yf_aes128_load_keys_for_encrypt,
	yf_aes128_cbc_encrypt,
	NULL
};

static
yf_functions_t yf_aes192_cbc_encr_funcs = {
	aes_block_process_initial_partial_block,
	aes_block_process_contiguous_whole_blocks,
	aes_block_process_last_partial_block,
	yf_aes192_load_keys_for_encrypt,
	yf_aes192_cbc_encrypt,
	NULL
};

static
yf_functions_t yf_aes256_cbc_encr_funcs = {
	aes_block_process_initial_partial_block,
	aes_block_process_contiguous_whole_blocks,
	aes_block_process_last_partial_block,
	yf_aes256_load_keys_for_encrypt,
	yf_aes256_cbc_encrypt,
	NULL
};


static
yf_functions_t yf_aes128_cbc_decr_funcs = {
	aes_block_process_initial_partial_block,
	aes_block_process_contiguous_whole_blocks,
	aes_block_process_last_partial_block,
	yf_aes128_load_keys_for_decrypt,
	yf_aes128_cbc_decrypt,
	NULL
};

static
yf_functions_t yf_aes192_cbc_decr_funcs = {
	aes_block_process_initial_partial_block,
	aes_block_process_contiguous_whole_blocks,
	aes_block_process_last_partial_block,
	yf_aes192_load_keys_for_decrypt,
	yf_aes192_cbc_decrypt,
	NULL
};

static
yf_functions_t yf_aes256_cbc_decr_funcs = {
	aes_block_process_initial_partial_block,
	aes_block_process_contiguous_whole_blocks,
	aes_block_process_last_partial_block,
	yf_aes256_load_keys_for_decrypt,
	yf_aes256_cbc_decrypt,
	NULL
};

static
yf_functions_t yf_aes128_cfb128_encr_funcs = {
	aes_cfb128_encr_process_initial_partial_block,
	aes_block_process_contiguous_whole_blocks,
	aes_cfb128_process_last_partial_block,
	yf_aes128_load_keys_for_encrypt,
	yf_aes128_cfb128_encrypt,
	NULL
};

static
yf_functions_t yf_aes192_cfb128_encr_funcs = {
	aes_cfb128_encr_process_initial_partial_block,
	aes_block_process_contiguous_whole_blocks,
	aes_cfb128_process_last_partial_block,
	yf_aes192_load_keys_for_encrypt,
	yf_aes192_cfb128_encrypt,
	NULL
};

static
yf_functions_t yf_aes256_cfb128_encr_funcs = {
	aes_cfb128_encr_process_initial_partial_block,
	aes_block_process_contiguous_whole_blocks,
	aes_cfb128_process_last_partial_block,
	yf_aes256_load_keys_for_encrypt,
	yf_aes256_cfb128_encrypt,
	NULL
};


static
yf_functions_t yf_aes128_cfb128_decr_funcs = {
	aes_cfb128_decr_process_initial_partial_block,
	aes_block_process_contiguous_whole_blocks,
	aes_cfb128_process_last_partial_block,
	yf_aes128_load_keys_for_encrypt,	/* cfb uses encrypt keys */
	yf_aes128_cfb128_decrypt,
	NULL
};

static
yf_functions_t yf_aes192_cfb128_decr_funcs = {
	aes_cfb128_decr_process_initial_partial_block,
	aes_block_process_contiguous_whole_blocks,
	aes_cfb128_process_last_partial_block,
	yf_aes192_load_keys_for_encrypt,	/* cfb uses encrypt keys */
	yf_aes192_cfb128_decrypt,
	NULL
};

static
yf_functions_t yf_aes256_cfb128_decr_funcs = {
	aes_cfb128_decr_process_initial_partial_block,
	aes_block_process_contiguous_whole_blocks,
	aes_cfb128_process_last_partial_block,
	yf_aes256_load_keys_for_encrypt,	/* cfb uses encrypt keys */
	yf_aes256_cfb128_decrypt,
	NULL
};

/* ctr encrypt is the same as decrypt */
static
yf_functions_t yf_aes128_ctr_funcs = {
	aes_ctr_process_initial_partial_block,
	aes_ctr_process_contiguous_whole_blocks,
	aes_ctr_process_last_partial_block,
	yf_aes128_load_keys_for_encrypt,
	yf_aes128_ctr_crypt,
	NULL
};

static
yf_functions_t yf_aes192_ctr_funcs = {
	aes_ctr_process_initial_partial_block,
	aes_ctr_process_contiguous_whole_blocks,
	aes_ctr_process_last_partial_block,
	yf_aes192_load_keys_for_encrypt,
	yf_aes192_ctr_crypt,
	NULL
};

static
yf_functions_t yf_aes256_ctr_funcs = {
	aes_ctr_process_initial_partial_block,
	aes_ctr_process_contiguous_whole_blocks,
	aes_ctr_process_last_partial_block,
	yf_aes256_load_keys_for_encrypt,
	yf_aes256_ctr_crypt,
	NULL
};


static
yf_functions_t yf_aes128_gcm_encr_funcs = {
	aes_gcm_encr_process_initial_partial_block,
	aes_gcm_process_contiguous_whole_blocks,
	aes_ctr_process_last_partial_block,
	yf_aes128_load_keys_for_encrypt,
	yf_aes128_ctr_crypt,
	NULL
};

static
yf_functions_t yf_aes192_gcm_encr_funcs = {
	aes_gcm_encr_process_initial_partial_block,
	aes_gcm_process_contiguous_whole_blocks,
	aes_ctr_process_last_partial_block,
	yf_aes192_load_keys_for_encrypt,
	yf_aes192_ctr_crypt,
	NULL
};

static
yf_functions_t yf_aes256_gcm_encr_funcs = {
	aes_gcm_encr_process_initial_partial_block,
	aes_gcm_process_contiguous_whole_blocks,
	aes_ctr_process_last_partial_block,
	yf_aes256_load_keys_for_encrypt,
	yf_aes256_ctr_crypt,
	NULL
};

static
yf_functions_t yf_aes128_ccm_encr_funcs = {
	aes_ccm_encr_process_initial_partial_block,
	aes_ccm_process_contiguous_whole_blocks,
	aes_ccm_process_last_partial_block,
	yf_aes128_load_keys_for_encrypt,
	yf_aes128_ctr_crypt,
	yf_aes128_cbc_mac
};

static
yf_functions_t yf_aes192_ccm_encr_funcs = {
	aes_ccm_encr_process_initial_partial_block,
	aes_ccm_process_contiguous_whole_blocks,
	aes_ccm_process_last_partial_block,
	yf_aes192_load_keys_for_encrypt,
	yf_aes192_ctr_crypt,
	yf_aes192_cbc_mac
};

static
yf_functions_t yf_aes256_ccm_encr_funcs = {
	aes_ccm_encr_process_initial_partial_block,
	aes_ccm_process_contiguous_whole_blocks,
	aes_ccm_process_last_partial_block,
	yf_aes256_load_keys_for_encrypt,
	yf_aes256_ctr_crypt,
	yf_aes256_cbc_mac
};

/*
 * Encrypt multiple blocks of data with AES according to mode.
 */

int
aes_encrypt_contiguous_blocks(void *ctx, char *data, size_t length,
    crypto_data_t *out)
{
	yf_functions_t	*funcs;
	aes_ctx_t	*aes_ctx = ctx;
	aes_key_t	*aes_key = aes_ctx->ac_keysched;
	int		rv;
	boolean_t	yf_aes;

#ifdef	_KERNEL
	yf_aes = yf_aes_instructions_present();
#else
#ifdef HWCAP_AES
	yf_aes = B_TRUE;
#else
	yf_aes = B_FALSE;
#endif
#endif
	switch (aes_ctx->ac_flags & MODE_MASK) {

	case CTR_MODE:
		if (yf_aes) {
#ifdef	_KERNEL
			aes_ctx->ac_flags |= NO_CTR_FINAL;
#endif
			switch (aes_key->nr) {
			case 10:
				funcs = &yf_aes128_ctr_funcs;
				break;
			case 12:
				funcs = &yf_aes192_ctr_funcs;
				break;
			case 14:
				funcs = &yf_aes256_ctr_funcs;
				break;
			default:
				return (CRYPTO_ARGUMENTS_BAD);
			}
			rv = aes_crypt_contiguous_blocks(ctx, data, length, out,
			    funcs);
		} else {
			rv = ctr_mode_contiguous_blocks(ctx, data, length, out,
			    AES_BLOCK_LEN, aes_encrypt_block, aes_xor_block);
		}
		break;

	case CCM_MODE:
		if (yf_aes) {
			switch (aes_key->nr) {
			case 10:
				funcs = &yf_aes128_ccm_encr_funcs;
				break;
			case 12:
				funcs = &yf_aes192_ccm_encr_funcs;
				break;
			case 14:
				funcs = &yf_aes256_ccm_encr_funcs;
				break;
			default:
				return (CRYPTO_ARGUMENTS_BAD);
			}
			rv = aes_crypt_contiguous_blocks(ctx,
			    data, length, out, funcs);
			if (rv == CRYPTO_SUCCESS) {
				((ccm_ctx_t *)ctx)->ccm_processed_data_len +=
				    length;
			}
		} else {
			rv = ccm_mode_encrypt_contiguous_blocks(ctx,
			    data, length, out, AES_BLOCK_LEN,
			    aes_encrypt_block, aes_copy_block, aes_xor_block);
		}
		break;

	case GCM_MODE:
	case GMAC_MODE:
		if (yf_aes) {
			switch (aes_key->nr) {
			case 10:
				funcs = &yf_aes128_gcm_encr_funcs;
				break;
			case 12:
				funcs = &yf_aes192_gcm_encr_funcs;
				break;
			case 14:
				funcs = &yf_aes256_gcm_encr_funcs;
				break;
			default:
				return (CRYPTO_ARGUMENTS_BAD);
			}
			rv = aes_crypt_contiguous_blocks(ctx, data, length, out,
			    funcs);
			if (rv == CRYPTO_SUCCESS) {
				((gcm_ctx_t *)ctx)->gcm_processed_data_len +=
				    length;
			}
		} else {
			rv = gcm_mode_encrypt_contiguous_blocks(ctx,
			    data, length, out, AES_BLOCK_LEN,
			    aes_encrypt_block, aes_copy_block, aes_xor_block);
		}
		break;

	case CFB128_MODE:
		if (yf_aes) {
			switch (aes_key->nr) {
			case 10:
				funcs = &yf_aes128_cfb128_encr_funcs;
				break;
			case 12:
				funcs = &yf_aes192_cfb128_encr_funcs;
				break;
			case 14:
				funcs = &yf_aes256_cfb128_encr_funcs;
				break;
			default:
				return (CRYPTO_ARGUMENTS_BAD);
			}
			rv = aes_crypt_contiguous_blocks(ctx, data, length, out,
			    funcs);
		} else {
			rv = cfb_mode_contiguous_blocks(ctx, data, length, out,
			    AES_BLOCK_LEN, aes_encrypt_block, aes_copy_block,
			    aes_xor_block, B_TRUE /* is_encrypt */);
		}
		break;

	case CBC_MODE:
		if (yf_aes) {
			switch (aes_key->nr) {
			case 10:
				funcs = &yf_aes128_cbc_encr_funcs;
				break;
			case 12:
				funcs = &yf_aes192_cbc_encr_funcs;
				break;
			case 14:
				funcs = &yf_aes256_cbc_encr_funcs;
				break;
			default:
				return (CRYPTO_ARGUMENTS_BAD);
			}
			rv = aes_crypt_contiguous_blocks(ctx, data, length, out,
			    funcs);
		} else {
			rv = cbc_encrypt_contiguous_blocks(ctx,
			    data, length, out, AES_BLOCK_LEN,
			    aes_encrypt_block, aes_copy_block,
			    aes_xor_block);
		}
		break;

	case ECB_MODE:
	default:
		if (yf_aes) {
			switch (aes_key->nr) {
			case 10:
				funcs = &yf_aes128_ecb_encr_funcs;
				break;
			case 12:
				funcs = &yf_aes192_ecb_encr_funcs;
				break;
			case 14:
				funcs = &yf_aes256_ecb_encr_funcs;
				break;
			default:
				return (CRYPTO_ARGUMENTS_BAD);
			}
			rv = aes_crypt_contiguous_blocks(ctx, data, length, out,
			    funcs);
		} else {
			rv = ecb_cipher_contiguous_blocks(ctx, data, length,
			    out, AES_BLOCK_LEN, aes_encrypt_block);
		}
		break;
	}

	return (rv);
}



/*
 * Decrypt multiple blocks of data with AES according to mode.
 */
int
aes_decrypt_contiguous_blocks(void *ctx, char *data, size_t length,
    crypto_data_t *out)
{
	yf_functions_t	*funcs;
	aes_ctx_t	*aes_ctx = ctx;
	aes_key_t	*aes_key = aes_ctx->ac_keysched;
	int		rv;
	boolean_t	yf_aes;

#ifdef	_KERNEL
	yf_aes = yf_aes_instructions_present();
#else
#ifdef HWCAP_AES
	yf_aes = B_TRUE;
#else
	yf_aes = B_FALSE;
#endif
#endif

	switch (aes_ctx->ac_flags & MODE_MASK) {
	case CTR_MODE:
		if (yf_aes) {
#ifdef	_KERNEL
			aes_ctx->ac_flags |= NO_CTR_FINAL;
#endif
			switch (aes_key->nr) {
			case 10:
				funcs = &yf_aes128_ctr_funcs;
				break;
			case 12:
				funcs = &yf_aes192_ctr_funcs;
				break;
			case 14:
				funcs = &yf_aes256_ctr_funcs;
				break;
			default:
				return (CRYPTO_ARGUMENTS_BAD);
			}
			rv = aes_crypt_contiguous_blocks(ctx, data, length, out,
			    funcs);
		} else {
			rv = ctr_mode_contiguous_blocks(ctx, data, length, out,
			    AES_BLOCK_LEN, aes_encrypt_block, aes_xor_block);
		}
		if (rv == CRYPTO_DATA_LEN_RANGE) {
			rv = CRYPTO_ENCRYPTED_DATA_LEN_RANGE;
		}
		break;

	case CCM_MODE:
		rv = ccm_mode_decrypt_contiguous_blocks(ctx, data, length,
		    out, AES_BLOCK_LEN, aes_encrypt_block, aes_copy_block,
		    aes_xor_block);
		break;

	case GCM_MODE:
	case GMAC_MODE:
		rv = gcm_mode_decrypt_contiguous_blocks(ctx, data, length,
		    out, AES_BLOCK_LEN, aes_encrypt_block, aes_copy_block,
		    aes_xor_block);
		break;

	case CFB128_MODE:
		if (yf_aes) {
			switch (aes_key->nr) {
			case 10:
				funcs = &yf_aes128_cfb128_decr_funcs;
				break;
			case 12:
				funcs = &yf_aes192_cfb128_decr_funcs;
				break;
			case 14:
				funcs = &yf_aes256_cfb128_decr_funcs;
				break;
			default:
				return (CRYPTO_ARGUMENTS_BAD);
			}
			rv = aes_crypt_contiguous_blocks(ctx, data, length, out,
			    funcs);
		} else {
			rv = cfb_mode_contiguous_blocks(ctx, data, length, out,
			    AES_BLOCK_LEN, aes_encrypt_block, aes_copy_block,
			    aes_xor_block, B_FALSE /* is_encrypt */);
		}
		break;

	case CBC_MODE:
		if (yf_aes) {
			switch (aes_key->nr) {
			case 10:
				funcs = &yf_aes128_cbc_decr_funcs;
				break;
			case 12:
				funcs = &yf_aes192_cbc_decr_funcs;
				break;
			case 14:
				funcs = &yf_aes256_cbc_decr_funcs;
				break;
			default:
				return (CRYPTO_ARGUMENTS_BAD);
			}
			rv = aes_crypt_contiguous_blocks(ctx, data, length, out,
			    funcs);
		} else {
			rv = cbc_decrypt_contiguous_blocks(ctx,
			    data, length, out, AES_BLOCK_LEN,
			    aes_decrypt_block, aes_copy_block,
			    aes_xor_block);
		}
		break;

	case ECB_MODE:
	default:
		if (yf_aes) {
			switch (aes_key->nr) {
			case 10:
				funcs = &yf_aes128_ecb_decr_funcs;
				break;
			case 12:
				funcs = &yf_aes192_ecb_decr_funcs;
				break;
			case 14:
				funcs = &yf_aes256_ecb_decr_funcs;
				break;
			default:
				return (CRYPTO_ARGUMENTS_BAD);
			}
			rv = aes_crypt_contiguous_blocks(ctx, data, length, out,
			    funcs);
		} else {
			rv = ecb_cipher_contiguous_blocks(ctx, data, length,
			    out, AES_BLOCK_LEN, aes_decrypt_block);
			if (rv == CRYPTO_DATA_LEN_RANGE)
				rv = CRYPTO_ENCRYPTED_DATA_LEN_RANGE;
			break;
		}
	}

	return (rv);
}
