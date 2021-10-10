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

/*
 * AES provider for the Kernel Cryptographic Framework (KCF)
 */
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/modctl.h>
#include <sys/cmn_err.h>
#include <sys/ddi.h>
#include <sys/crypto/common.h>
#include <sys/crypto/impl.h>
#include <sys/crypto/spi.h>
#include <sys/sysmacros.h>
#include <sys/strsun.h>
#include <modes/modes.h>
#define	_AES_FIPS_POST
#define	_AES_IMPL
#include <aes/aes_impl.h>

#ifdef sun4v
#include <kernel_fp_use.h>
#endif /* sun4v */

extern struct mod_ops mod_cryptoops;

/*
 * Module linkage information for the kernel.
 */
static struct modlcrypto modlcrypto = {
	&mod_cryptoops,
	"AES Kernel SW Provider"
};

static struct modlinkage modlinkage = {
	MODREV_1,
	(void *)&modlcrypto,
	NULL
};

/*
 * The following definitions are to keep EXPORT_SRC happy.
 */
#ifndef AES_MIN_KEY_BYTES
#define	AES_MIN_KEY_BYTES		0
#endif

#ifndef AES_MAX_KEY_BYTES
#define	AES_MAX_KEY_BYTES		0
#endif

/*
 * Mechanism info structure passed to KCF during registration.
 */
static crypto_mech_info_t aes_mech_info_tab[] = {
	/* AES_ECB */
	{SUN_CKM_AES_ECB, CRYPTO_AES_ECB,
	    CRYPTO_FG_ENCRYPT | CRYPTO_FG_ENCRYPT_ATOMIC |
	    CRYPTO_FG_DECRYPT | CRYPTO_FG_DECRYPT_ATOMIC,
	    AES_MIN_KEY_BYTES, AES_MAX_KEY_BYTES, CRYPTO_KEYSIZE_UNIT_IN_BYTES},
	/* AES_CBC */
	{SUN_CKM_AES_CBC, CRYPTO_AES_CBC,
	    CRYPTO_FG_ENCRYPT | CRYPTO_FG_ENCRYPT_ATOMIC |
	    CRYPTO_FG_DECRYPT | CRYPTO_FG_DECRYPT_ATOMIC,
	    AES_MIN_KEY_BYTES, AES_MAX_KEY_BYTES, CRYPTO_KEYSIZE_UNIT_IN_BYTES},
	/* AES_CTR */
	{SUN_CKM_AES_CTR, CRYPTO_AES_CTR,
	    CRYPTO_FG_ENCRYPT | CRYPTO_FG_ENCRYPT_ATOMIC |
	    CRYPTO_FG_DECRYPT | CRYPTO_FG_DECRYPT_ATOMIC,
	    AES_MIN_KEY_BYTES, AES_MAX_KEY_BYTES, CRYPTO_KEYSIZE_UNIT_IN_BYTES},
	/* AES_CCM */
	{SUN_CKM_AES_CCM, CRYPTO_AES_CCM,
	    CRYPTO_FG_ENCRYPT | CRYPTO_FG_ENCRYPT_ATOMIC |
	    CRYPTO_FG_DECRYPT | CRYPTO_FG_DECRYPT_ATOMIC,
	    AES_MIN_KEY_BYTES, AES_MAX_KEY_BYTES, CRYPTO_KEYSIZE_UNIT_IN_BYTES},
	/* AES_GCM */
	{SUN_CKM_AES_GCM, CRYPTO_AES_GCM,
	    CRYPTO_FG_ENCRYPT | CRYPTO_FG_ENCRYPT_ATOMIC |
	    CRYPTO_FG_DECRYPT | CRYPTO_FG_DECRYPT_ATOMIC,
	    AES_MIN_KEY_BYTES, AES_MAX_KEY_BYTES, CRYPTO_KEYSIZE_UNIT_IN_BYTES},
	/* AES_GMAC */
	{SUN_CKM_AES_GMAC, CRYPTO_AES_GMAC,
	    CRYPTO_FG_ENCRYPT | CRYPTO_FG_ENCRYPT_ATOMIC |
	    CRYPTO_FG_DECRYPT | CRYPTO_FG_DECRYPT_ATOMIC |
	    CRYPTO_FG_MAC | CRYPTO_FG_MAC_ATOMIC |
	    CRYPTO_FG_SIGN | CRYPTO_FG_SIGN_ATOMIC |
	    CRYPTO_FG_VERIFY | CRYPTO_FG_VERIFY_ATOMIC,
	    AES_MIN_KEY_BYTES, AES_MAX_KEY_BYTES, CRYPTO_KEYSIZE_UNIT_IN_BYTES},
	/* AES_CFB128 */
	{SUN_CKM_AES_CFB128, CRYPTO_AES_CFB128,
	    CRYPTO_FG_ENCRYPT | CRYPTO_FG_ENCRYPT_ATOMIC |
	    CRYPTO_FG_DECRYPT | CRYPTO_FG_DECRYPT_ATOMIC,
	    AES_MIN_KEY_BYTES, AES_MAX_KEY_BYTES, CRYPTO_KEYSIZE_UNIT_IN_BYTES},
};

static void aes_provider_status(crypto_provider_handle_t, uint_t *);
static int aes_encrypt(crypto_ctx_t *ctx, crypto_data_t *plaintext,
    crypto_data_t *ciphertext, crypto_req_handle_t req);
static int aes_decrypt(crypto_ctx_t *ctx, crypto_data_t *ciphertext,
    crypto_data_t *plaintext, crypto_req_handle_t req);

static crypto_control_ops_t aes_control_ops = {
	aes_provider_status
};

static crypto_cipher_ops_t aes_cipher_ops = {
	aes_encrypt_init,
	aes_encrypt,
	aes_encrypt_update,
	aes_encrypt_final,
	aes_encrypt_atomic,
	aes_decrypt_init,
	aes_decrypt,
	aes_decrypt_update,
	aes_decrypt_final,
	aes_decrypt_atomic
};

static crypto_mac_ops_t aes_mac_ops = {
	NULL,
	NULL,
	NULL,
	NULL,
	aes_mac_atomic,
	aes_mac_verify_atomic
};

static crypto_ctx_ops_t aes_ctx_ops = {
	aes_create_ctx_template,
	aes_free_context
};

static void aes_POST(int *);

static crypto_fips140_ops_t aes_fips140_ops = {
	aes_POST
};

static crypto_ops_t aes_crypto_ops = {
	&aes_control_ops,
	NULL,
	&aes_cipher_ops,
	&aes_mac_ops,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	&aes_ctx_ops,
	NULL,
	NULL,
	&aes_fips140_ops
};

static crypto_provider_info_t aes_prov_info = {
	CRYPTO_SPI_VERSION_4,
	"AES Software Provider",
	CRYPTO_SW_PROVIDER,
	{&modlinkage},
	NULL,
	&aes_crypto_ops,
	sizeof (aes_mech_info_tab)/sizeof (crypto_mech_info_t),
	aes_mech_info_tab
};

static crypto_kcf_provider_handle_t aes_prov_handle;
static boolean_t	is_aes_registered = B_FALSE;

int aes_decrypt_update_internal(crypto_ctx_t *, crypto_data_t *,
    crypto_data_t *, crypto_req_handle_t, boolean_t);
int aes_encrypt_update_internal(crypto_ctx_t *, crypto_data_t *,
    crypto_data_t *, crypto_req_handle_t, boolean_t);

int
_init(void)
{
	int	ret;

	if ((ret = mod_install(&modlinkage)) != 0)
		return (ret);

	/* Register with KCF.  If the registration fails, remove the module. */
	if (ret = crypto_register_provider(&aes_prov_info, &aes_prov_handle)) {
		(void) mod_remove(&modlinkage);
		if (ret == CRYPTO_MECH_NOT_SUPPORTED) {
			return (ENOTSUP);
		} else {
			return (EACCES);
		}
	}

	is_aes_registered = B_TRUE;

	return (0);
}

int
_fini(void)
{
	/* Unregister from KCF if module is registered */
	if (is_aes_registered) {
		if (crypto_unregister_provider(aes_prov_handle))
			return (EBUSY);

		is_aes_registered = B_FALSE;
	}

	return (mod_remove(&modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

/*
 * KCF software provider control entry points.
 */
/* ARGSUSED */
static void
aes_provider_status(crypto_provider_handle_t provider, uint_t *status)
{
	*status = CRYPTO_PROVIDER_READY;
}

/*
 * aes_encrypt and aes_decrypt primarily remain in this file because
 * the full kernel lint global crosschecks has an error with wanboot.
 * Since userland does not need these two functions, they have remained
 * in this file
 */
static int
aes_encrypt(crypto_ctx_t *ctx, crypto_data_t *plaintext,
    crypto_data_t *ciphertext, crypto_req_handle_t req)
{
	int ret = CRYPTO_FAILED;

/* EXPORT DELETE START */

	aes_ctx_t *aes_ctx;
	size_t saved_length, saved_offset, length_needed;
#if defined(sun4v) && defined _KERNEL
	fp_save_t fp_save_buf;
	boolean_t save_fp;
#endif
	ASSERT(ctx->cc_provider_private != NULL);
	aes_ctx = ctx->cc_provider_private;

	/*
	 * For block ciphers, plaintext must be a multiple of AES block size.
	 * This test is only valid for ciphers whose blocksize is a power of 2.
	 */
	if (((aes_ctx->ac_flags &
	    (CTR_MODE|CCM_MODE|GCM_MODE|GMAC_MODE|CFB128_MODE)) == 0) &&
	    (plaintext->cd_length & (AES_BLOCK_LEN - 1)) != 0)
		return (CRYPTO_DATA_LEN_RANGE);

	CRYPTO_ARG_INPLACE(plaintext, ciphertext);

	/*
	 * We need to just return the length needed to store the output.
	 * We should not destroy the context for the following case.
	 */
	switch (aes_ctx->ac_flags & (CCM_MODE|GCM_MODE|GMAC_MODE)) {
	case CCM_MODE:
		length_needed = plaintext->cd_length + aes_ctx->ac_mac_len;
		break;
	case GCM_MODE:
		length_needed = plaintext->cd_length + aes_ctx->ac_tag_len;
		break;
	case GMAC_MODE:
		if (plaintext->cd_length != 0)
			return (CRYPTO_ARGUMENTS_BAD);

		length_needed = aes_ctx->ac_tag_len;
		break;
	default:
		length_needed = plaintext->cd_length;
	}

	if (ciphertext->cd_length < length_needed) {
		ciphertext->cd_length = length_needed;
		return (CRYPTO_BUFFER_TOO_SMALL);
	}

	saved_length = ciphertext->cd_length;
	saved_offset = ciphertext->cd_offset;

	/*
	 * Do an update on the specified input data.
	 */
#if defined(sun4v) && defined _KERNEL
	save_fp = yf_aes_instructions_present();
	SAVE_FP;
#endif
	ret = aes_encrypt_update_internal(ctx, plaintext, ciphertext, req,
	    B_FALSE /* don't save fp registers */);
	if (ret != CRYPTO_SUCCESS) {
#if defined(sun4v) && defined _KERNEL
		RESTORE_FP;
#endif
		return (ret);
	}

	/*
	 * For CCM mode, aes_ccm_encrypt_final() will take care of any
	 * left-over unprocessed data, and compute the MAC
	 */
	if (aes_ctx->ac_flags & CCM_MODE) {
		/*
		 * ccm_encrypt_final() will compute the MAC and append
		 * it to existing ciphertext. So, need to adjust the left over
		 * length value accordingly
		 */

		/* order of following 2 lines MUST not be reversed */
		ciphertext->cd_offset = ciphertext->cd_length;
		ciphertext->cd_length = saved_length - ciphertext->cd_length;
		ret = ccm_encrypt_final((ccm_ctx_t *)aes_ctx, ciphertext,
		    AES_BLOCK_LEN, aes_encrypt_block, aes_xor_block);
		if (ret != CRYPTO_SUCCESS) {
#if defined(sun4v) && defined _KERNEL
			RESTORE_FP;
#endif
			return (ret);
		}

		if (plaintext != ciphertext) {
			ciphertext->cd_length =
			    ciphertext->cd_offset - saved_offset;
		}
		ciphertext->cd_offset = saved_offset;

	} else if (aes_ctx->ac_flags & (GCM_MODE|GMAC_MODE)) {
		/*
		 * gcm_encrypt_final() will compute the MAC and append
		 * it to existing ciphertext. So, need to adjust the left over
		 * length value accordingly
		 */

		/* order of following 2 lines MUST not be reversed */
		ciphertext->cd_offset = ciphertext->cd_length;
		ciphertext->cd_length = saved_length - ciphertext->cd_length;
		ret = gcm_encrypt_final((gcm_ctx_t *)aes_ctx, ciphertext,
		    AES_BLOCK_LEN, aes_encrypt_block, aes_copy_block,
		    aes_xor_block);
		if (ret != CRYPTO_SUCCESS) {
#if defined(sun4v) && defined _KERNEL
			RESTORE_FP;
#endif
			return (ret);
		}

		if (plaintext != ciphertext) {
			ciphertext->cd_length =
			    ciphertext->cd_offset - saved_offset;
		}
		ciphertext->cd_offset = saved_offset;
	}
#if defined(sun4v) && defined _KERNEL
	RESTORE_FP;
#endif

	ASSERT(aes_ctx->ac_remainder_len == 0);
	(void) aes_free_context(ctx);

/* EXPORT DELETE END */

	return (ret);
}


static int
aes_decrypt(crypto_ctx_t *ctx, crypto_data_t *ciphertext,
    crypto_data_t *plaintext, crypto_req_handle_t req)
{
	int ret = CRYPTO_FAILED;

/* EXPORT DELETE START */

	aes_ctx_t *aes_ctx;
	off_t saved_offset;
	size_t saved_length, length_needed;
#if defined(sun4v) && defined _KERNEL
	fp_save_t fp_save_buf;
	boolean_t save_fp;
#endif

	ASSERT(ctx->cc_provider_private != NULL);
	aes_ctx = ctx->cc_provider_private;

	/*
	 * For block ciphers, plaintext must be a multiple of AES block size.
	 * This test is only valid for ciphers whose blocksize is a power of 2.
	 */
	if (((aes_ctx->ac_flags &
	    (CTR_MODE|CCM_MODE|GCM_MODE|GMAC_MODE|CFB128_MODE)) == 0) &&
	    (ciphertext->cd_length & (AES_BLOCK_LEN - 1)) != 0) {
		return (CRYPTO_ENCRYPTED_DATA_LEN_RANGE);
	}

	CRYPTO_ARG_INPLACE(ciphertext, plaintext);

	/*
	 * Return length needed to store the output.
	 * Do not destroy context when plaintext buffer is too small.
	 *
	 * CCM:  plaintext is MAC len smaller than cipher text
	 * GCM:  plaintext is TAG len smaller than cipher text
	 * GMAC: plaintext length must be zero
	 */
	switch (aes_ctx->ac_flags & (CCM_MODE|GCM_MODE|GMAC_MODE)) {
	case CCM_MODE:
		length_needed = aes_ctx->ac_processed_data_len;
		break;
	case GCM_MODE:
		length_needed = ciphertext->cd_length - aes_ctx->ac_tag_len;
		break;
	case GMAC_MODE:
		if (plaintext->cd_length != 0)
			return (CRYPTO_ARGUMENTS_BAD);

		length_needed = 0;
		break;
	default:
		length_needed = ciphertext->cd_length;
	}

	if (plaintext->cd_length < length_needed) {
		plaintext->cd_length = length_needed;
		return (CRYPTO_BUFFER_TOO_SMALL);
	}

	saved_offset = plaintext->cd_offset;
	saved_length = plaintext->cd_length;

	/*
	 * Do an update on the specified input data.
	 */
#if defined(sun4v) && defined _KERNEL
	save_fp = yf_aes_instructions_present();
	SAVE_FP;
#endif
	ret = aes_decrypt_update_internal(ctx, ciphertext, plaintext, req,
	    B_FALSE /* don't save fp registers */);
	if (ret != CRYPTO_SUCCESS) {
		goto cleanup;
	}

	if (aes_ctx->ac_flags & CCM_MODE) {
		ASSERT(aes_ctx->ac_processed_data_len == aes_ctx->ac_data_len);
		ASSERT(aes_ctx->ac_processed_mac_len == aes_ctx->ac_mac_len);

		/* order of following 2 lines MUST not be reversed */
		plaintext->cd_offset = plaintext->cd_length;
		plaintext->cd_length = saved_length - plaintext->cd_length;

		ret = ccm_decrypt_final((ccm_ctx_t *)aes_ctx, plaintext,
		    AES_BLOCK_LEN, aes_encrypt_block,
		    aes_decrypt_contiguous_blocks, aes_cbcmac_multiblock);
		if (ret == CRYPTO_SUCCESS) {
			if (plaintext != ciphertext) {
				plaintext->cd_length =
				    plaintext->cd_offset - saved_offset;
			}
		} else {
			plaintext->cd_length = saved_length;
		}

		plaintext->cd_offset = saved_offset;
	} else if (aes_ctx->ac_flags & (GCM_MODE|GMAC_MODE)) {
		/* order of following 2 lines MUST not be reversed */
		plaintext->cd_offset = plaintext->cd_length;
		plaintext->cd_length = saved_length - plaintext->cd_length;

		ret = gcm_decrypt_final((gcm_ctx_t *)aes_ctx, plaintext,
		    AES_BLOCK_LEN, aes_encrypt_block, aes_xor_block,
		    aes_decrypt_contiguous_blocks);
		if (ret == CRYPTO_SUCCESS) {
			if (plaintext != ciphertext) {
				plaintext->cd_length =
				    plaintext->cd_offset - saved_offset;
			}
		} else {
			plaintext->cd_length = saved_length;
		}

		plaintext->cd_offset = saved_offset;
	}

	ASSERT(aes_ctx->ac_remainder_len == 0);

cleanup:
#if defined(sun4v) && defined _KERNEL
	RESTORE_FP;
#endif
	(void) aes_free_context(ctx);

/* EXPORT DELETE END */

	return (ret);
}


/*
 * AES Power-Up Self-Test
 */
void
aes_POST(int *rc)
{

	int ret;

	/* AES Power-Up Self-Test for 128-bit key. */
	ret = fips_aes_post(FIPS_AES_128_KEY_SIZE);

	if (ret != CRYPTO_SUCCESS)
		goto out;

	/* AES Power-Up Self-Test for 192-bit key. */
	ret = fips_aes_post(FIPS_AES_192_KEY_SIZE);

	if (ret != CRYPTO_SUCCESS)
		goto out;

	/* AES Power-Up Self-Test for 256-bit key. */
	ret = fips_aes_post(FIPS_AES_256_KEY_SIZE);

out:
	*rc = ret;

}
