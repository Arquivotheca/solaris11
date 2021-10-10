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
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <modes/modes.h>
#include "aes_impl.h"

#if defined(sun4v) && defined(_KERNEL)
#include <kernel_fp_use.h>
extern boolean_t yf_aes_instructions_present(void);
#endif /* sun4v && _KERNEL */

#ifndef	_KERNEL
#include <strings.h>
#include <security/cryptoki.h>
#include <sys/crypto/common.h>
#include <sys/crypto/spi.h>
#include <assert.h>
#include <libsoftcrypto.h>
#endif	/* _KERNEL */

#if !defined(__i386) && !defined(__amd64)
/* Implement aes_copy_block() for unaligned data. */
#define	AES_COPY_BLOCK(src, dst) \
	(dst)[0] = (src)[0]; \
	(dst)[1] = (src)[1]; \
	(dst)[2] = (src)[2]; \
	(dst)[3] = (src)[3]; \
	(dst)[4] = (src)[4]; \
	(dst)[5] = (src)[5]; \
	(dst)[6] = (src)[6]; \
	(dst)[7] = (src)[7]; \
	(dst)[8] = (src)[8]; \
	(dst)[9] = (src)[9]; \
	(dst)[10] = (src)[10]; \
	(dst)[11] = (src)[11]; \
	(dst)[12] = (src)[12]; \
	(dst)[13] = (src)[13]; \
	(dst)[14] = (src)[14]; \
	(dst)[15] = (src)[15]

/* Implement aes_xor_block() for unaligned data. */
#define	AES_XOR_BLOCK(src, dst) \
	(dst)[0] ^= (src)[0]; \
	(dst)[1] ^= (src)[1]; \
	(dst)[2] ^= (src)[2]; \
	(dst)[3] ^= (src)[3]; \
	(dst)[4] ^= (src)[4]; \
	(dst)[5] ^= (src)[5]; \
	(dst)[6] ^= (src)[6]; \
	(dst)[7] ^= (src)[7]; \
	(dst)[8] ^= (src)[8]; \
	(dst)[9] ^= (src)[9]; \
	(dst)[10] ^= (src)[10]; \
	(dst)[11] ^= (src)[11]; \
	(dst)[12] ^= (src)[12]; \
	(dst)[13] ^= (src)[13]; \
	(dst)[14] ^= (src)[14]; \
	(dst)[15] ^= (src)[15]
#endif	/* __amd64 */

/*
 * Since we check that the output buffer is not null, null_crypto_data needs
 * a valid base pointer. This is a result of mac_verify with GMAC.
 */
static char null_crypto_data_base = 0;
static crypto_data_t null_crypto_data = {
	CRYPTO_DATA_RAW, 0, 0, NULL, &null_crypto_data_base, 0
};


/*
 * Copy a 16-byte AES block from "in" to "out".
 * X86 has no alignment requirement, but SPARC does for 32-bit copy.
 */
void
aes_copy_block(uint8_t *in, uint8_t *out)
{
#if !defined(__i386) && !defined(__amd64)
	if (IS_P2ALIGNED2(in, out, sizeof (uint32_t))) {
#endif
		/* LINTED: pointer alignment */
		*(uint32_t *)&out[0] = *(uint32_t *)&in[0];
		/* LINTED: pointer alignment */
		*(uint32_t *)&out[4] = *(uint32_t *)&in[4];
		/* LINTED: pointer alignment */
		*(uint32_t *)&out[8] = *(uint32_t *)&in[8];
		/* LINTED: pointer alignment */
		*(uint32_t *)&out[12] = *(uint32_t *)&in[12];

#if !defined(__i386) && !defined(__amd64)
	} else {
		AES_COPY_BLOCK(in, out);
	}
#endif
}


static void
aes_copy_block64(uint8_t *in, uint64_t *out)
{
#if !defined(__i386) && !defined(__amd64)
	if (IS_P2ALIGNED(in, sizeof (uint64_t))) {
#endif
		/* LINTED: pointer alignment */
		out[0] = *(uint64_t *)&in[0];
		/* LINTED: pointer alignment */
		out[1] = *(uint64_t *)&in[8];

#if !defined(__i386) && !defined(__amd64)
	} else {
		uint8_t *iv8 = (uint8_t *)&out[0];

		AES_COPY_BLOCK(in, iv8);
	}
#endif
}


/*
 * XOR a 16-byte AES block of "data" into "dst".
 * X86 has no alignment requirement, but SPARC does for 32-bit operations.
 */
void
aes_xor_block(uint8_t *data, uint8_t *dst)
{
#if !defined(__i386) && !defined(__amd64)
	if (IS_P2ALIGNED2(dst, data, sizeof (uint32_t))) {
#endif
		/* LINTED: pointer alignment */
		*(uint32_t *)&dst[0] ^= *(uint32_t *)&data[0];
		/* LINTED: pointer alignment */
		*(uint32_t *)&dst[4] ^= *(uint32_t *)&data[4];
		/* LINTED: pointer alignment */
		*(uint32_t *)&dst[8] ^= *(uint32_t *)&data[8];
		/* LINTED: pointer alignment */
		*(uint32_t *)&dst[12] ^= *(uint32_t *)&data[12];

#if !defined(__i386) && !defined(__amd64)
	} else {
		AES_XOR_BLOCK(data, dst);
	}
#endif
}

#if !defined(sun4v)

void
aes_cbcmac_multiblock(void *ctx, uint64_t *input, size_t len)
{
	ccm_ctx_t	*cctx = (ccm_ctx_t *)ctx;
	uint8_t		*mac_buf = (uint8_t *)(cctx->ccm_mac_buf);
	int		i;

	for (i = 0; i < len; i++) {
		cctx->ccm_mac_buf[0] ^= input[2 * i];
		cctx->ccm_mac_buf[1] ^= input[2 * i + 1];
		(void) aes_encrypt_block(cctx->ccm_keysched, mac_buf, mac_buf);
	}
}

#endif

#if !defined(__amd64) && !defined(sun4v)

/*
 * Encrypt multiple blocks of data with AES according to mode.
 */
int
aes_encrypt_contiguous_blocks(void *ctx, char *data, size_t length,
    crypto_data_t *out)
{
	aes_ctx_t *aes_ctx = ctx;
	int rv;

	switch (aes_ctx->ac_flags & MODE_MASK) {
	case CTR_MODE:
		rv = ctr_mode_contiguous_blocks(ctx, data, length, out,
		    AES_BLOCK_LEN, aes_encrypt_block, aes_xor_block);
		break;

	case CCM_MODE:
		rv = ccm_mode_encrypt_contiguous_blocks(ctx, data, length,
		    out, AES_BLOCK_LEN, aes_encrypt_block, aes_copy_block,
		    aes_xor_block);
		break;

	case GCM_MODE:
	case GMAC_MODE:
		rv = gcm_mode_encrypt_contiguous_blocks(ctx, data, length,
		    out, AES_BLOCK_LEN, aes_encrypt_block, aes_copy_block,
		    aes_xor_block);
		break;

	case CFB128_MODE:
		rv = cfb_mode_contiguous_blocks(ctx, data, length, out,
		    AES_BLOCK_LEN, aes_encrypt_block, aes_copy_block,
		    aes_xor_block, B_TRUE /* is_encrypt */);
		break;

	case CBC_MODE:
		rv = cbc_encrypt_contiguous_blocks(ctx,
		    data, length, out, AES_BLOCK_LEN, aes_encrypt_block,
		    aes_copy_block, aes_xor_block);
		break;

	case ECB_MODE:
	default:
		rv = ecb_cipher_contiguous_blocks(ctx, data, length, out,
		    AES_BLOCK_LEN, aes_encrypt_block);
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
	aes_ctx_t *aes_ctx = ctx;
	int rv;

	switch (aes_ctx->ac_flags & MODE_MASK) {
	case CTR_MODE:
		rv = ctr_mode_contiguous_blocks(ctx, data, length, out,
		    AES_BLOCK_LEN, aes_encrypt_block, aes_xor_block);
		if (rv == CRYPTO_DATA_LEN_RANGE)
			rv = CRYPTO_ENCRYPTED_DATA_LEN_RANGE;
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
		rv = cfb_mode_contiguous_blocks(ctx, data, length, out,
		    AES_BLOCK_LEN, aes_encrypt_block, aes_copy_block,
		    aes_xor_block, B_FALSE /* is_encrypt */);
		break;

	case CBC_MODE:
		rv = cbc_decrypt_contiguous_blocks(ctx, data, length, out,
		    AES_BLOCK_LEN, aes_decrypt_block, aes_copy_block,
		    aes_xor_block);
		break;

	case ECB_MODE:
	default:
		rv = ecb_cipher_contiguous_blocks(ctx, data, length, out,
		    AES_BLOCK_LEN, aes_decrypt_block);
		if (rv == CRYPTO_DATA_LEN_RANGE)
			rv = CRYPTO_ENCRYPTED_DATA_LEN_RANGE;
		break;
	}

	return (rv);
}
#endif	/* !sun4v && !__amd64 */


/*
 * Sanity check arguments in crypto_mechanism_t, based on AES mode.
 * Allocate AES context if NULL.
 */
static int
aes_check_mech_param(crypto_mechanism_t *mechanism, aes_ctx_t **ctx, int kmflag)
{
	void *p = NULL;
	boolean_t param_required = B_TRUE;
	size_t param_len;
	void *(*alloc_fun)(int);
	int rv = CRYPTO_SUCCESS;

	switch (mechanism->cm_type) {
	case CRYPTO_AES_ECB:
		param_required = B_FALSE;
		alloc_fun = ecb_alloc_ctx;
		break;
	case CRYPTO_AES_CBC:
		/* IV can be obtained via crypto_data_t instead */
		param_required = B_FALSE;
		param_len = AES_BLOCK_LEN;
		alloc_fun = cbc_alloc_ctx;
		break;
	case CRYPTO_AES_CTR:
		param_len = sizeof (CK_AES_CTR_PARAMS);
		alloc_fun = ctr_alloc_ctx;
		break;
	case CRYPTO_AES_CCM:
		param_len = sizeof (CK_AES_CCM_PARAMS);
		alloc_fun = ccm_alloc_ctx;
		break;
	case CRYPTO_AES_GCM:
		param_len = sizeof (CK_AES_GCM_PARAMS);
		alloc_fun = gcm_alloc_ctx;
		break;
	case CRYPTO_AES_GMAC:
		param_len = sizeof (CK_AES_GMAC_PARAMS);
		alloc_fun = gmac_alloc_ctx;
		break;
	case CRYPTO_AES_CFB128:
		param_len = AES_BLOCK_LEN;
		alloc_fun = cfb_alloc_ctx;
		break;
	default:
		param_required = B_FALSE;
		rv = CRYPTO_MECHANISM_INVALID;
	}

	if (param_required && (mechanism->cm_param == NULL ||
	    mechanism->cm_param_len != param_len)) {
		rv = CRYPTO_MECHANISM_PARAM_INVALID;
	}

	/* Allocate context if needed */
	if ((ctx != NULL) && (*ctx == NULL) && (rv == CRYPTO_SUCCESS)) {
		p = (alloc_fun)(kmflag);
		*ctx = p;
		if (p == NULL) {
			rv = CRYPTO_HOST_MEMORY;
		}
	}
	return (rv);
}

/* EXPORT DELETE START */

/*
 * Initialize key schedules for AES from the key.
 */
static int
init_keysched(crypto_key_t *key, void *newbie)
{
	/*
	 * Only keys by value are supported by this module.
	 */
	switch (key->ck_format) {
	case CRYPTO_KEY_RAW:
		if (key->ck_data == NULL)
			return (CRYPTO_KEY_NEEDED);

		if ((key->ck_length != 128) && (key->ck_length != 192) &&
		    (key->ck_length != 256))
			return (CRYPTO_KEY_SIZE_RANGE);

		break;
	default:
		return (CRYPTO_KEY_TYPE_INCONSISTENT);
	}

	aes_init_keysched(key->ck_data, key->ck_length, newbie);
	return (CRYPTO_SUCCESS);
}

/* EXPORT DELETE END */


/*
 * Initialize the AES Context (CTX) structure for all modes.
 * This function assumes aes_ctx has been allocated, whether on the heap,
 * stack, or a static variable, and has been zeroed.
 */
int
aes_common_init_ctx(aes_ctx_t *aes_ctx, crypto_spi_ctx_template_t *template,
    crypto_mechanism_t *mechanism, crypto_key_t *key, int kmflag,
    boolean_t is_encrypt_init, boolean_t save_fp)
{
	int rv = CRYPTO_SUCCESS;

/* EXPORT DELETE START */

	void *keysched;
	size_t size;

	ASSERT(mechanism != NULL);
	ASSERT(aes_ctx != NULL);
	ASSERT((aes_ctx->ac_keysched == NULL) && (aes_ctx->ac_lastp == NULL));
	ASSERT((aes_ctx->ac_keysched_len | aes_ctx->ac_remainder_len) == 0);

	if (template == NULL) {
		if ((keysched = aes_alloc_keysched(&size, kmflag)) == NULL)
			return (CRYPTO_HOST_MEMORY);
		/*
		 * Initialize key schedule.
		 * Key length is stored in the key.
		 */
		ASSERT(key != NULL);
		if ((rv = init_keysched(key, keysched)) != CRYPTO_SUCCESS) {
			CRYPTO_FREE(keysched, size);
			return (rv);
		}

		aes_ctx->ac_flags |= PROVIDER_OWNS_KEY_SCHEDULE;
		aes_ctx->ac_keysched_len = size;
	} else {
		keysched = template;
	}
	aes_ctx->ac_keysched = keysched;

	switch (mechanism->cm_type) {
	case CRYPTO_AES_CBC:
		rv = cbc_init_ctx((cbc_ctx_t *)aes_ctx, mechanism->cm_param,
		    mechanism->cm_param_len, AES_BLOCK_LEN, aes_copy_block64);
		break;
	case CRYPTO_AES_CTR: {
		CK_AES_CTR_PARAMS *pp;

		if (mechanism->cm_param == NULL ||
		    mechanism->cm_param_len != sizeof (CK_AES_CTR_PARAMS)) {
			rv = CRYPTO_MECHANISM_PARAM_INVALID;
			goto out;
		}
		pp = (CK_AES_CTR_PARAMS *)(void *)mechanism->cm_param;
		rv = ctr_init_ctx((ctr_ctx_t *)aes_ctx, pp->ulCounterBits,
		    pp->cb, aes_copy_block);
		break;
	}
	case CRYPTO_AES_CCM:
		if (mechanism->cm_param == NULL ||
		    mechanism->cm_param_len != sizeof (CK_AES_CCM_PARAMS)) {
			rv = CRYPTO_MECHANISM_PARAM_INVALID;
			goto out;
		}
#ifdef HWCAP_AES
		aes_ctx->ac_flags |= NO_CTR_FINAL;
#elif defined(sun4v) && defined(_KERNEL)
		if (save_fp) {
			aes_ctx->ac_flags |= NO_CTR_FINAL;
		}
#endif
		rv = ccm_init_ctx((ccm_ctx_t *)aes_ctx, mechanism->cm_param,
		    kmflag, is_encrypt_init, AES_BLOCK_LEN, aes_encrypt_block,
		    aes_xor_block, save_fp);
		break;
	case CRYPTO_AES_GCM:
		if (mechanism->cm_param == NULL ||
		    mechanism->cm_param_len != sizeof (CK_AES_GCM_PARAMS)) {
			rv = CRYPTO_MECHANISM_PARAM_INVALID;
			goto out;
		}
#ifdef HWCAP_AES
		aes_ctx->ac_flags |= NO_CTR_FINAL;
#elif defined(sun4v) && defined(_KERNEL)
		if (save_fp) {
			aes_ctx->ac_flags |= NO_CTR_FINAL;
		}
#endif
		rv = gcm_init_ctx((gcm_ctx_t *)aes_ctx, mechanism->cm_param,
		    AES_BLOCK_LEN, aes_encrypt_block, aes_copy_block,
		    aes_xor_block, save_fp);
		break;
	case CRYPTO_AES_GMAC:
		if (mechanism->cm_param == NULL ||
		    mechanism->cm_param_len != sizeof (CK_AES_GMAC_PARAMS)) {
			rv = CRYPTO_MECHANISM_PARAM_INVALID;
			goto out;
		}
#ifdef HWCAP_AES
		aes_ctx->ac_flags |= NO_CTR_FINAL;
#elif defined(sun4v) && defined(_KERNEL)
		if (save_fp) {
			aes_ctx->ac_flags |= NO_CTR_FINAL;
		}
#endif
		rv = gmac_init_ctx((gcm_ctx_t *)aes_ctx, mechanism->cm_param,
		    AES_BLOCK_LEN, aes_encrypt_block, aes_copy_block,
		    aes_xor_block, save_fp);
		break;
	case CRYPTO_AES_CFB128:
		rv = cfb_init_ctx((cfb_ctx_t *)aes_ctx, mechanism->cm_param,
		    mechanism->cm_param_len, AES_BLOCK_LEN, aes_copy_block64);
		break;
	case CRYPTO_AES_ECB:
		aes_ctx->ac_flags |= ECB_MODE;
		break;
	default:
		rv = CRYPTO_MECHANISM_INVALID;
	}

out:
	if (rv != CRYPTO_SUCCESS) {
		if (aes_ctx->ac_flags & PROVIDER_OWNS_KEY_SCHEDULE) {
			CRYPTO_ZFREE(keysched, size);
		}
	}

/* EXPORT DELETE END */

	return (rv);
}


/*
 * Encrypt/decrypt entry points.
 */

/*
 * Allocate and initialize the AES mode context for both
 * encryption and decryption.
 */
#ifndef _KERNEL
/*ARGSUSED*/
#endif
int
aes_common_init(crypto_ctx_t *ctx, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_spi_ctx_template_t template,
    crypto_req_handle_t req, boolean_t is_encrypt_init)
{

/* EXPORT DELETE START */

	aes_ctx_t *aes_ctx = NULL; /* allocated on the heap */
	int rv;
	int kmflag = 0;
	boolean_t save_fp = B_FALSE;

	ASSERT(ctx != NULL);

	/*
	 * Only keys by value are supported by this module.
	 */
	if (key->ck_format != CRYPTO_KEY_RAW) {
		return (CRYPTO_KEY_TYPE_INCONSISTENT);
	}

	/* Check parameters and allocate AES mode context */
	kmflag = CRYPTO_KMFLAG(req);
	if ((rv = aes_check_mech_param(mechanism, &aes_ctx, kmflag))
	    != CRYPTO_SUCCESS) {
		crypto_free_mode_ctx(aes_ctx);
		return (rv);
	}

#if defined(sun4v) && defined(_KERNEL)
	save_fp = yf_aes_instructions_present();
#endif
	/* Initialize AES mode context */
	rv = aes_common_init_ctx(aes_ctx, template, mechanism, key, kmflag,
	    is_encrypt_init, save_fp);
	if (rv != CRYPTO_SUCCESS) {
		crypto_free_mode_ctx(aes_ctx);
		return (rv);
	}
	((crypto_ctx_t *)ctx)->cc_provider_private = aes_ctx;

/* EXPORT DELETE END */

	return (CRYPTO_SUCCESS);
}

int
aes_encrypt_init(crypto_ctx_t *ctx, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_spi_ctx_template_t template,
    crypto_req_handle_t req) {
	return (aes_common_init(ctx, mechanism, key, template, req, B_TRUE));
}

int
aes_decrypt_init(crypto_ctx_t *ctx, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_spi_ctx_template_t template,
    crypto_req_handle_t req)
{
	return (aes_common_init(ctx, mechanism, key, template, req, B_FALSE));
}

/* ARGSUSED */
int
aes_encrypt_update_internal(crypto_ctx_t *ctx, crypto_data_t *plaintext,
    crypto_data_t *ciphertext, crypto_req_handle_t req, boolean_t save_fp)
{
	off_t saved_offset;
	size_t saved_length, out_len;
	int ret = CRYPTO_SUCCESS;
	aes_ctx_t *aes_ctx;

#if defined(sun4v) && defined(_KERNEL)
	fp_save_t fp_save_buf;
#endif

	ASSERT(ctx->cc_provider_private != NULL);
	aes_ctx = ctx->cc_provider_private;

	CRYPTO_ARG_INPLACE(plaintext, ciphertext);

	/*
	 * Verify there is enough ciphertext buffer for the output based on
	 * how the particular mode operates
	 */
	switch (aes_ctx->ac_flags & MODE_MASK) {
	case CTR_MODE:
	case CFB128_MODE:
		if (ciphertext->cd_length < plaintext->cd_length) {
			ciphertext->cd_length = plaintext->cd_length;
			return (CRYPTO_BUFFER_TOO_SMALL);
		}
		break;

	case CCM_MODE:
	case GCM_MODE:
	case GMAC_MODE:
		if (aes_ctx->ac_flags & NO_CTR_FINAL) {
			if (ciphertext->cd_length < plaintext->cd_length) {
				ciphertext->cd_length = plaintext->cd_length;
				return (CRYPTO_BUFFER_TOO_SMALL);
			}
		} else {
			out_len = aes_ctx->ac_remainder_len;
			out_len += plaintext->cd_length;
			out_len &= ~(AES_BLOCK_LEN - 1);

			/* return length needed to store the output */
			if (ciphertext->cd_length < out_len) {
				plaintext->cd_length = out_len;
				return (CRYPTO_BUFFER_TOO_SMALL);
			}
		}
		break;
	default:
		/* compute number of bytes that will hold the ciphertext */
		out_len = aes_ctx->ac_remainder_len;
		out_len += plaintext->cd_length;
		out_len &= ~(AES_BLOCK_LEN - 1);

		/* return length needed to store the output */
		if (ciphertext->cd_length < out_len) {
			ciphertext->cd_length = out_len;
			return (CRYPTO_BUFFER_TOO_SMALL);
		}
	};

	saved_offset = ciphertext->cd_offset;
	saved_length = ciphertext->cd_length;

	/*
	 * Do the AES update on the specified input data.
	 */
#if defined(sun4v) && defined(_KERNEL)
	SAVE_FP;
#endif
	switch (plaintext->cd_format) {
	case CRYPTO_DATA_RAW:
		ret = crypto_update_iov(ctx->cc_provider_private,
		    plaintext, ciphertext, aes_encrypt_contiguous_blocks,
		    aes_copy_block64);
		break;
#ifdef _KERNEL
	case CRYPTO_DATA_UIO:
		ret = crypto_update_uio(ctx->cc_provider_private,
		    plaintext, ciphertext, aes_encrypt_contiguous_blocks,
		    aes_copy_block64);
		break;
	case CRYPTO_DATA_MBLK:
		ret = crypto_update_mp(ctx->cc_provider_private,
		    plaintext, ciphertext, aes_encrypt_contiguous_blocks,
		    aes_copy_block64);
		break;
#endif /* _KERNEL */
	default:
		ret = CRYPTO_ARGUMENTS_BAD;
	}

#if defined(sun4v) && defined(_KERNEL)
	RESTORE_FP;
#endif

	if (ret == CRYPTO_SUCCESS) {
		ciphertext->cd_length = ciphertext->cd_offset - saved_offset;
	} else {
		ciphertext->cd_length = saved_length;
	}
	ciphertext->cd_offset = saved_offset;

	return (ret);
}

int
aes_encrypt_update(crypto_ctx_t *ctx, crypto_data_t *plaintext,
    crypto_data_t *ciphertext, crypto_req_handle_t req)
{
	boolean_t save_fp = B_FALSE;

#if defined(sun4v) && defined(_KERNEL)
	save_fp = yf_aes_instructions_present();
#endif
	return (aes_encrypt_update_internal(ctx, plaintext, ciphertext,
	    req, save_fp));
}

/*ARGSUSED*/
int
aes_decrypt_update_internal(crypto_ctx_t *ctx, crypto_data_t *ciphertext,
    crypto_data_t *plaintext,
    crypto_req_handle_t req, boolean_t save_fp)
{
	off_t saved_offset;
	size_t saved_length, out_len;
	int ret = CRYPTO_SUCCESS;
	aes_ctx_t *aes_ctx;

#if defined(sun4v) && defined(_KERNEL)
	fp_save_t fp_save_buf;
#endif

	ASSERT(ctx->cc_provider_private != NULL);
	aes_ctx = ctx->cc_provider_private;

	CRYPTO_ARG_INPLACE(ciphertext, plaintext);

	/*
	 * Compute number of bytes that will hold the plaintext.
	 * This is not necessary for CCM, GCM, and GMAC since these
	 * mechanisms never return plaintext for update operations.
	 */
	switch (aes_ctx->ac_flags & MODE_MASK) {
	case CTR_MODE:
	case CFB128_MODE:
		if (plaintext->cd_length < ciphertext->cd_length) {
			plaintext->cd_length = ciphertext->cd_length;
			return (CRYPTO_BUFFER_TOO_SMALL);
		}
		break;

	case CCM_MODE:
	case GCM_MODE:
	case GMAC_MODE:
		break;

	default:
		/* compute number of bytes that will hold the plaintext */
		out_len = aes_ctx->ac_remainder_len;
		out_len += ciphertext->cd_length;
		out_len &= ~(AES_BLOCK_LEN - 1);

		/* return length needed to store the output */
		if (plaintext->cd_length < out_len) {
			plaintext->cd_length = out_len;
			return (CRYPTO_BUFFER_TOO_SMALL);
		}
	}

	saved_offset = plaintext->cd_offset;
	saved_length = plaintext->cd_length;

#ifdef _KERNEL
	if (aes_ctx->ac_flags & (GCM_MODE|GMAC_MODE))
		gcm_set_kmflag((gcm_ctx_t *)aes_ctx, CRYPTO_KMFLAG(req));
#endif /* _KERNEL */

	/*
	 * Do the AES update on the specified input data.
	 */
#if defined(sun4v) && defined(_KERNEL)
	SAVE_FP;
#endif
	switch (ciphertext->cd_format) {
	case CRYPTO_DATA_RAW:
		ret = crypto_update_iov(ctx->cc_provider_private,
		    ciphertext, plaintext, aes_decrypt_contiguous_blocks,
		    aes_copy_block64);
		break;
#ifdef _KERNEL
	case CRYPTO_DATA_UIO:
		ret = crypto_update_uio(ctx->cc_provider_private,
		    ciphertext, plaintext, aes_decrypt_contiguous_blocks,
		    aes_copy_block64);
		break;
	case CRYPTO_DATA_MBLK:
		ret = crypto_update_mp(ctx->cc_provider_private,
		    ciphertext, plaintext, aes_decrypt_contiguous_blocks,
		    aes_copy_block64);
		break;
#endif /* _KERNEL */
	default:
		ret = CRYPTO_ARGUMENTS_BAD;
	}

#if defined(sun4v) && defined(_KERNEL)
	RESTORE_FP;
#endif
	if (ret == CRYPTO_SUCCESS) {
		plaintext->cd_length = plaintext->cd_offset - saved_offset;
	} else {
		plaintext->cd_length = saved_length;
	}
	plaintext->cd_offset = saved_offset;

	return (ret);
}

int
aes_decrypt_update(crypto_ctx_t *ctx, crypto_data_t *ciphertext,
    crypto_data_t *plaintext, crypto_req_handle_t req)
{
	boolean_t save_fp = B_FALSE;

#if defined(sun4v) && defined(_KERNEL)
	save_fp = yf_aes_instructions_present();
#endif
	return (aes_decrypt_update_internal(ctx, ciphertext, plaintext,
	    req, save_fp));
}


/*
 * Process the final AES encryption and free the context.
 */
/* ARGSUSED */
int
aes_encrypt_final(crypto_ctx_t *ctx, crypto_data_t *data,
    crypto_req_handle_t req)
{
	int ret = CRYPTO_SUCCESS;
/* EXPORT DELETE START */
	aes_ctx_t *aes_ctx;
	size_t saved_offset = data->cd_offset;

#if defined(sun4v) && defined(_KERNEL)
	fp_save_t fp_save_buf;
	boolean_t save_fp;
#endif

	ASSERT(ctx->cc_provider_private != NULL);
	aes_ctx = ctx->cc_provider_private;

#ifdef _KERNEL
	if (data->cd_format != CRYPTO_DATA_RAW &&
	    data->cd_format != CRYPTO_DATA_UIO &&
	    data->cd_format != CRYPTO_DATA_MBLK) {
#else
	if (data->cd_format != CRYPTO_DATA_RAW) {
#endif
		ret = CRYPTO_ARGUMENTS_BAD;
		goto free_context;
	}

	switch (aes_ctx->ac_flags & MODE_MASK) {
	case CTR_MODE:
	case CFB128_MODE:
		data->cd_length = 0;
		break;
	case CCM_MODE:

#if defined(sun4v) && defined(_KERNEL)
		save_fp = yf_aes_instructions_present();
		SAVE_FP;
#endif
		ret = ccm_encrypt_final((ccm_ctx_t *)aes_ctx, data,
		    AES_BLOCK_LEN, aes_encrypt_block, aes_xor_block);
		data->cd_length = data->cd_offset - saved_offset;
		data->cd_offset = saved_offset;

#if defined(sun4v) && defined(_KERNEL)
		RESTORE_FP;
#endif
		break;
	case GCM_MODE:
	case GMAC_MODE:
#if defined(sun4v) && defined(_KERNEL)
		save_fp = yf_aes_instructions_present();
		SAVE_FP;
#endif

		ret = gcm_encrypt_final((gcm_ctx_t *)aes_ctx, data,
		    AES_BLOCK_LEN, aes_encrypt_block, aes_copy_block,
		    aes_xor_block);
#if defined(sun4v) && defined(_KERNEL)
		RESTORE_FP;
#endif
		if (ret != CRYPTO_SUCCESS) {
			goto free_context;
		}
		data->cd_length = data->cd_offset - saved_offset;
		data->cd_offset = saved_offset;
		break;
	default:
		/*
		 * There must be no unprocessed plaintext.
		 * This happens if the length of the last data is
		 * not a multiple of the AES block length.
		 */
		if (aes_ctx->ac_remainder_len > 0) {
			ret = CRYPTO_DATA_LEN_RANGE;
			goto free_context;
		}
		data->cd_length = 0;
	}

free_context:
	(void) aes_free_context(ctx);
/* EXPORT DELETE END */
	return (ret);
}


/*
 * Process the final AES decryption and free the context.
 */
/* ARGSUSED */
int
aes_decrypt_final(crypto_ctx_t *ctx, crypto_data_t *data,
    crypto_req_handle_t req)
{
	int ret = CRYPTO_SUCCESS;
/* EXPORT DELETE START */
	aes_ctx_t *aes_ctx;
	off_t saved_offset;
	size_t saved_length;

#if defined(sun4v) && defined(_KERNEL)
	fp_save_t fp_save_buf;
	boolean_t save_fp;
#endif

	ASSERT(ctx->cc_provider_private != NULL);
	aes_ctx = ctx->cc_provider_private;

#ifdef _KERNEL
	if (data->cd_format != CRYPTO_DATA_RAW &&
	    data->cd_format != CRYPTO_DATA_UIO &&
	    data->cd_format != CRYPTO_DATA_MBLK) {
#else
	if (data->cd_format != CRYPTO_DATA_RAW) {
#endif
		ret = CRYPTO_ARGUMENTS_BAD;
		goto free_context;
	}

	if (aes_ctx->ac_flags & (CFB128_MODE | CTR_MODE)) {
		data->cd_length = 0;
		ret = CRYPTO_SUCCESS;
		goto free_context;
	}

	/*
	 * There must be no unprocessed ciphertext.
	 * This happens if the length of the last ciphertext is
	 * not a multiple of the AES block length.
	 */
	if (aes_ctx->ac_remainder_len > 0) {
		ret = CRYPTO_ENCRYPTED_DATA_LEN_RANGE;
		goto free_context;
	}

	switch (aes_ctx->ac_flags & MODE_MASK) {
	case CCM_MODE:
	{
		/*
		 * This is where all the plaintext is returned, make sure
		 * the plaintext buffer is big enough
		 */
		size_t pt_len = aes_ctx->ac_data_len;
		if (data->cd_length < pt_len) {
			data->cd_length = pt_len;
			ret = CRYPTO_BUFFER_TOO_SMALL;
			goto free_context;
		}

		saved_offset = data->cd_offset;
		saved_length = data->cd_length;
#if defined(sun4v) && defined(_KERNEL)
		save_fp = yf_aes_instructions_present();
		SAVE_FP;
#endif
		ret = ccm_decrypt_final((ccm_ctx_t *)aes_ctx, data,
		    AES_BLOCK_LEN, aes_encrypt_block,
		    aes_decrypt_contiguous_blocks, aes_cbcmac_multiblock);
#if defined(sun4v) && defined(_KERNEL)
		RESTORE_FP;
#endif
		if (ret == CRYPTO_SUCCESS) {
			data->cd_length = data->cd_offset - saved_offset;
		} else {
			data->cd_length = saved_length;
		}

		data->cd_offset = saved_offset;
		if (ret != CRYPTO_SUCCESS) {
			goto free_context;
		}
		break;
	}
	case GCM_MODE:
	case GMAC_MODE:
	{
		/*
		 * This is where all the plaintext is returned, make sure
		 * the plaintext buffer is big enough
		 */
		gcm_ctx_t *gcm_ctx = (gcm_ctx_t *)aes_ctx;
		size_t pt_len = gcm_ctx->gcm_processed_data_len
		    - gcm_ctx->gcm_tag_len;

		if (data->cd_length < pt_len) {
			data->cd_length = pt_len;
			ret = CRYPTO_BUFFER_TOO_SMALL;
			goto free_context;
		}

		saved_offset = data->cd_offset;
		saved_length = data->cd_length;
#if defined(sun4v) && defined(_KERNEL)
		save_fp = yf_aes_instructions_present();
		SAVE_FP;
#endif
		ret = gcm_decrypt_final(gcm_ctx, data, AES_BLOCK_LEN,
		    aes_encrypt_block, aes_xor_block,
		    aes_decrypt_contiguous_blocks);
#if defined(sun4v) && defined(_KERNEL)
		RESTORE_FP;
#endif
		if (ret == CRYPTO_SUCCESS) {
			data->cd_length = data->cd_offset - saved_offset;
		} else {
			data->cd_length = saved_length;
		}

		data->cd_offset = saved_offset;
		if (ret != CRYPTO_SUCCESS) {
			goto free_context;
		}
		break;
	}
	default:
		break;
	}

	if ((aes_ctx->ac_flags &
	    (CTR_MODE|CCM_MODE|GCM_MODE|GMAC_MODE)) == 0) {
		data->cd_length = 0;
	}

free_context:
	(void) aes_free_context(ctx);
/* EXPORT DELETE END */
	return (ret);
}


/* ARGSUSED */
int
aes_encrypt_atomic(crypto_provider_handle_t provider,
    crypto_session_id_t session_id, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_data_t *plaintext, crypto_data_t *ciphertext,
    crypto_spi_ctx_template_t template, crypto_req_handle_t req)
{
	aes_ctx_t aes_ctx;	/* on the stack */
	off_t saved_offset;
	size_t saved_length;
	size_t length_needed;
	int ret;
#if defined(sun4v) && defined(_KERNEL)
	fp_save_t fp_save_buf;
#endif
	boolean_t save_fp = B_FALSE;

	CRYPTO_ARG_INPLACE(plaintext, ciphertext);

	/*
	 * CTR, CCM, GCM, GMAC and CFB128 modes do not require that plaintext
	 * be a multiple of AES block size.
	 */
	switch (mechanism->cm_type) {
	case CRYPTO_AES_CTR:
	case CRYPTO_AES_CCM:
	case CRYPTO_AES_GCM:
	case CRYPTO_AES_GMAC:
	case CRYPTO_AES_CFB128:
		break;
	default:
		if ((plaintext->cd_length & (AES_BLOCK_LEN - 1)) != 0)
			return (CRYPTO_DATA_LEN_RANGE);
	}

	if ((ret = aes_check_mech_param(mechanism, NULL, 0)) != CRYPTO_SUCCESS)
		return (ret);

	bzero(&aes_ctx, sizeof (aes_ctx_t));

#if defined(sun4v) && defined(_KERNEL)
	save_fp = yf_aes_instructions_present();
#endif
	ret = aes_common_init_ctx(&aes_ctx, template, mechanism, key,
	    CRYPTO_KMFLAG(req), B_TRUE, save_fp);
	if (ret != CRYPTO_SUCCESS)
		return (ret);

	switch (mechanism->cm_type) {
	case CRYPTO_AES_CCM:
		length_needed = plaintext->cd_length + aes_ctx.ac_mac_len;
		break;
	case CRYPTO_AES_GMAC:
		if (plaintext->cd_length != 0)
			return (CRYPTO_ARGUMENTS_BAD);
		/* FALLTHRU */
	case CRYPTO_AES_GCM:
		length_needed = plaintext->cd_length + aes_ctx.ac_tag_len;
		break;
	default:
		length_needed = plaintext->cd_length;
	}

	/* return size of buffer needed to store output */
	if (ciphertext->cd_length < length_needed) {
		ciphertext->cd_length = length_needed;
		ret = CRYPTO_BUFFER_TOO_SMALL;
		goto out;
	}

	saved_offset = ciphertext->cd_offset;
	saved_length = ciphertext->cd_length;

	/*
	 * Do an update on the specified input data.
	 */
#if defined(sun4v) && defined(_KERNEL)
	SAVE_FP;
#endif
	switch (plaintext->cd_format) {
	case CRYPTO_DATA_RAW:
		ret = crypto_update_iov(&aes_ctx, plaintext, ciphertext,
		    aes_encrypt_contiguous_blocks, aes_copy_block64);
		break;
#ifdef _KERNEL
	case CRYPTO_DATA_UIO:
		ret = crypto_update_uio(&aes_ctx, plaintext, ciphertext,
		    aes_encrypt_contiguous_blocks, aes_copy_block64);
		break;
	case CRYPTO_DATA_MBLK:
		ret = crypto_update_mp(&aes_ctx, plaintext, ciphertext,
		    aes_encrypt_contiguous_blocks, aes_copy_block64);
		break;
#endif /* _KERNEL */
	default:
		ret = CRYPTO_ARGUMENTS_BAD;
	}

	if (ret == CRYPTO_SUCCESS) {
		switch (mechanism->cm_type) {
		case CRYPTO_AES_CCM:
			ret = ccm_encrypt_final((ccm_ctx_t *)&aes_ctx,
			    ciphertext, AES_BLOCK_LEN, aes_encrypt_block,
			    aes_xor_block);
			if (ret != CRYPTO_SUCCESS)
				goto out;
			ASSERT(aes_ctx.ac_remainder_len == 0);
			break;
		case CRYPTO_AES_GCM:
		case CRYPTO_AES_GMAC:
			ret = gcm_encrypt_final((gcm_ctx_t *)&aes_ctx,
			    ciphertext, AES_BLOCK_LEN, aes_encrypt_block,
			    aes_copy_block, aes_xor_block);
			if (ret != CRYPTO_SUCCESS)
				goto out;
			ASSERT(aes_ctx.ac_remainder_len == 0);
			break;
		case CRYPTO_AES_CTR:
		case CRYPTO_AES_CFB128:
			break;
		default:
			ASSERT(aes_ctx.ac_remainder_len == 0);
		}
		ciphertext->cd_length = ciphertext->cd_offset - saved_offset;
	} else {
		ciphertext->cd_length = saved_length;
	}
	ciphertext->cd_offset = saved_offset;

out:
#if defined(sun4v) && defined(_KERNEL)
	RESTORE_FP;
#endif
	if (aes_ctx.ac_flags & PROVIDER_OWNS_KEY_SCHEDULE) {
		CRYPTO_ZFREE(aes_ctx.ac_keysched, aes_ctx.ac_keysched_len);
	}

	return (ret);
}

/* ARGSUSED */
int
aes_decrypt_atomic(crypto_provider_handle_t provider,
    crypto_session_id_t session_id, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_data_t *ciphertext, crypto_data_t *plaintext,
    crypto_spi_ctx_template_t template, crypto_req_handle_t req)
{
	aes_ctx_t aes_ctx;	/* on the stack */
	off_t saved_offset;
	size_t saved_length;
	size_t length_needed;
	int ret;
#if defined(sun4v) && defined(_KERNEL)
	fp_save_t fp_save_buf;
#endif
	boolean_t save_fp = B_FALSE;

	CRYPTO_ARG_INPLACE(ciphertext, plaintext);

	/*
	 * CCM, GCM, CTR, GMAC and CFB128 modes do not require that ciphertext
	 * be a multiple of AES block size.
	 */
	switch (mechanism->cm_type) {
	case CRYPTO_AES_CTR:
	case CRYPTO_AES_CCM:
	case CRYPTO_AES_GCM:
	case CRYPTO_AES_GMAC:
	case CRYPTO_AES_CFB128:
		break;
	default:
		if ((ciphertext->cd_length & (AES_BLOCK_LEN - 1)) != 0)
			return (CRYPTO_ENCRYPTED_DATA_LEN_RANGE);
	}

	if ((ret = aes_check_mech_param(mechanism, NULL, 0)) != CRYPTO_SUCCESS)
		return (ret);

	bzero(&aes_ctx, sizeof (aes_ctx_t));

#if defined(sun4v) && defined(_KERNEL)
	save_fp = yf_aes_instructions_present();
#endif
	ret = aes_common_init_ctx(&aes_ctx, template, mechanism, key,
	    CRYPTO_KMFLAG(req), B_FALSE, save_fp);
	if (ret != CRYPTO_SUCCESS)
		return (ret);

	switch (mechanism->cm_type) {
	case CRYPTO_AES_CCM:
		length_needed = aes_ctx.ac_data_len;
		break;
	case CRYPTO_AES_GCM:
		length_needed = ciphertext->cd_length - aes_ctx.ac_tag_len;
		break;
	case CRYPTO_AES_GMAC:
		if (plaintext->cd_length != 0)
			return (CRYPTO_ARGUMENTS_BAD);
		length_needed = 0;
		break;
	default:
		length_needed = ciphertext->cd_length;
	}

	/* return size of buffer needed to store output */
	if (plaintext->cd_length < length_needed) {
		plaintext->cd_length = length_needed;
		ret = CRYPTO_BUFFER_TOO_SMALL;
		goto out;
	}

	saved_offset = plaintext->cd_offset;
	saved_length = plaintext->cd_length;


#ifdef _KERNEL
	if (mechanism->cm_type == CRYPTO_AES_GCM ||
	    mechanism->cm_type == CRYPTO_AES_GMAC)
		gcm_set_kmflag((gcm_ctx_t *)&aes_ctx, CRYPTO_KMFLAG(req));
#endif /* _KERNEL */

	/*
	 * Do an update on the specified input data.
	 */
#if defined(sun4v) && defined(_KERNEL)
	SAVE_FP;
#endif
	switch (ciphertext->cd_format) {
	case CRYPTO_DATA_RAW:
		ret = crypto_update_iov(&aes_ctx, ciphertext, plaintext,
		    aes_decrypt_contiguous_blocks, aes_copy_block64);
		break;
#ifdef _KERNEL
	case CRYPTO_DATA_UIO:
		ret = crypto_update_uio(&aes_ctx, ciphertext, plaintext,
		    aes_decrypt_contiguous_blocks, aes_copy_block64);
		break;
	case CRYPTO_DATA_MBLK:
		ret = crypto_update_mp(&aes_ctx, ciphertext, plaintext,
		    aes_decrypt_contiguous_blocks, aes_copy_block64);
		break;
#endif /* _KERNEL */
	default:
		ret = CRYPTO_ARGUMENTS_BAD;
	}

	if (ret == CRYPTO_SUCCESS) {
		switch (mechanism->cm_type) {
		case CRYPTO_AES_CCM:
			ASSERT(aes_ctx.ac_processed_data_len
			    == aes_ctx.ac_data_len + aes_ctx.ac_mac_len);
			ret = ccm_decrypt_final((ccm_ctx_t *)&aes_ctx,
			    plaintext, AES_BLOCK_LEN, aes_encrypt_block,
			    aes_decrypt_contiguous_blocks,
			    aes_cbcmac_multiblock);
			ASSERT(aes_ctx.ac_remainder_len == 0);
			if (ret == CRYPTO_SUCCESS) {
				plaintext->cd_length =
				    plaintext->cd_offset - saved_offset;
			} else {
				plaintext->cd_length = saved_length;
			}
			break;
		case CRYPTO_AES_GCM:
		case CRYPTO_AES_GMAC:
			ret = gcm_decrypt_final((gcm_ctx_t *)&aes_ctx,
			    plaintext, AES_BLOCK_LEN, aes_encrypt_block,
			    aes_xor_block, aes_decrypt_contiguous_blocks);
			ASSERT(aes_ctx.ac_remainder_len == 0);
			if (ret == CRYPTO_SUCCESS) {
				plaintext->cd_length =
				    plaintext->cd_offset - saved_offset;
			} else {
				plaintext->cd_length = saved_length;
			}
			break;
		case CRYPTO_AES_CTR:
		case CRYPTO_AES_CFB128:
			plaintext->cd_length =
			    plaintext->cd_offset - saved_offset;
			break;
		default:
			ASSERT(aes_ctx.ac_remainder_len == 0);
			plaintext->cd_length =
			    plaintext->cd_offset - saved_offset;
		}
	} else {
		plaintext->cd_length = saved_length;
	}
	plaintext->cd_offset = saved_offset;

out:
#if defined(sun4v) && defined(_KERNEL)
	RESTORE_FP;
#endif
	if (aes_ctx.ac_flags & PROVIDER_OWNS_KEY_SCHEDULE) {
		CRYPTO_ZFREE(aes_ctx.ac_keysched, aes_ctx.ac_keysched_len);
	}
	if (aes_ctx.ac_flags & CCM_MODE) {
		if (aes_ctx.ac_pt_buf != NULL) {
			CRYPTO_FREE(aes_ctx.ac_pt_buf,
			    aes_ctx.ac_data_len + aes_ctx.ac_mac_len);
		}
	} else if (aes_ctx.ac_flags & (GCM_MODE|GMAC_MODE)) {
		if (((gcm_ctx_t *)&aes_ctx)->gcm_pt_buf != NULL) {
			CRYPTO_FREE(((gcm_ctx_t *)&aes_ctx)->gcm_pt_buf,
			    ((gcm_ctx_t *)&aes_ctx)->gcm_pt_buf_len);
		}
	}
	return (ret);
}

static int
process_gmac_mech(crypto_mechanism_t *mech, crypto_data_t *data,
    CK_AES_GCM_PARAMS *gcm_params)
{
	/* LINTED: pointer alignment */
	CK_AES_GMAC_PARAMS *params = (CK_AES_GMAC_PARAMS *)mech->cm_param;

	if (mech->cm_type != CRYPTO_AES_GMAC)
		return (CRYPTO_MECHANISM_INVALID);

	if (mech->cm_param_len != sizeof (CK_AES_GMAC_PARAMS))
		return (CRYPTO_MECHANISM_PARAM_INVALID);

	if (params->pIv == NULL)
		return (CRYPTO_MECHANISM_PARAM_INVALID);

	gcm_params->pIv = params->pIv;
	gcm_params->ulIvLen = AES_GMAC_IV_LEN;
	gcm_params->ulTagBits = AES_GMAC_TAG_BITS;

	if (data == NULL)
		return (CRYPTO_SUCCESS);

	if (data->cd_format != CRYPTO_DATA_RAW)
		return (CRYPTO_ARGUMENTS_BAD);

	gcm_params->pAAD = (uchar_t *)data->cd_raw.iov_base;
	gcm_params->ulAADLen = data->cd_length;
	return (CRYPTO_SUCCESS);
}

int
aes_mac_atomic(crypto_provider_handle_t provider,
    crypto_session_id_t session_id, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_data_t *data, crypto_data_t *mac,
    crypto_spi_ctx_template_t template, crypto_req_handle_t req)
{
	CK_AES_GCM_PARAMS gcm_params;
	crypto_mechanism_t gcm_mech;
	int rv;

	if ((rv = process_gmac_mech(mechanism, data, &gcm_params))
	    != CRYPTO_SUCCESS)
		return (rv);

	gcm_mech.cm_type = CRYPTO_AES_GCM;
	gcm_mech.cm_param_len = sizeof (CK_AES_GCM_PARAMS);
	gcm_mech.cm_param = (char *)&gcm_params;

	return (aes_encrypt_atomic(provider, session_id, &gcm_mech,
	    key, &null_crypto_data, mac, template, req));
}

int
aes_mac_verify_atomic(crypto_provider_handle_t provider,
    crypto_session_id_t session_id, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_data_t *data, crypto_data_t *mac,
    crypto_spi_ctx_template_t template, crypto_req_handle_t req)
{
	CK_AES_GCM_PARAMS gcm_params;
	crypto_mechanism_t gcm_mech;
	int rv;

	if ((rv = process_gmac_mech(mechanism, data, &gcm_params))
	    != CRYPTO_SUCCESS)
		return (rv);

	gcm_mech.cm_type = CRYPTO_AES_GCM;
	gcm_mech.cm_param_len = sizeof (CK_AES_GCM_PARAMS);
	gcm_mech.cm_param = (char *)&gcm_params;

	return (aes_decrypt_atomic(provider, session_id, &gcm_mech,
	    key, mac, &null_crypto_data, template, req));
}

/*
 * KCF software provider context template entry points.
 */

/*
 * Initialize key schedules for AES from the key.
 */
/* ARGSUSED */
int
aes_create_ctx_template(crypto_provider_handle_t provider,
    crypto_mechanism_t *mechanism, crypto_key_t *key,
    crypto_spi_ctx_template_t *tmpl, size_t *tmpl_size, crypto_req_handle_t req)
{

/* EXPORT DELETE START */

	void *keysched;
	size_t size;
	int rv;

	if (mechanism->cm_type != CRYPTO_AES_ECB &&
	    mechanism->cm_type != CRYPTO_AES_CBC &&
	    mechanism->cm_type != CRYPTO_AES_CTR &&
	    mechanism->cm_type != CRYPTO_AES_CCM &&
	    mechanism->cm_type != CRYPTO_AES_GCM &&
	    mechanism->cm_type != CRYPTO_AES_GMAC &&
	    mechanism->cm_type != CRYPTO_AES_CFB128)
		return (CRYPTO_MECHANISM_INVALID);

	if ((keysched = aes_alloc_keysched(&size,
	    CRYPTO_KMFLAG(req))) == NULL) {
		return (CRYPTO_HOST_MEMORY);
	}

	/*
	 * Initialize key schedule.  Key length information is stored
	 * in the key.
	 */
	if ((rv = init_keysched(key, keysched)) != CRYPTO_SUCCESS) {
		CRYPTO_ZFREE(keysched, size);
		return (rv);
	}

	*tmpl = keysched;
	*tmpl_size = size;

/* EXPORT DELETE END */

	return (CRYPTO_SUCCESS);
}


/*
 * Free the AES key schedule, and AES mode context,
 * but not the crypto context itself.
 */
int
aes_free_context(crypto_ctx_t *ctx)
{

/* EXPORT DELETE START */

	aes_ctx_t *aes_ctx = ctx->cc_provider_private;

	if (aes_ctx != NULL) {
		if (aes_ctx->ac_flags & PROVIDER_OWNS_KEY_SCHEDULE) {
			ASSERT(aes_ctx->ac_keysched_len != 0);
			CRYPTO_ZFREE(aes_ctx->ac_keysched,
			    aes_ctx->ac_keysched_len);
		}
		crypto_free_mode_ctx(aes_ctx);
		ctx->cc_provider_private = NULL;
	}

/* EXPORT DELETE END */

	return (CRYPTO_SUCCESS);
}
