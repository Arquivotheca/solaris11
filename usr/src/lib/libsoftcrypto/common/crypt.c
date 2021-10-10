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

#include <modes/modes.h>
#include <sys/crypto/common.h>
#include <sys/crypto/spi.h>
#include <aes/aes_impl.h>
#include <rsa/rsa_impl.h>
#include "libsoftcrypto.h"

/*
 * IMPORTANT: Each feature enhancement must be accommpanied by an increment
 * in the version.
 */
#define	UCRYPTO_VERSION 3

typedef enum ucrypto_op {
	UCRYPTO_NOOP = 0,
	UCRYPTO_ENCRYPT,
	UCRYPTO_DECRYPT,
	UCRYPTO_SIGN,
	UCRYPTO_VERIFY
} ucrypto_op_t;

typedef struct ucrypto_ctx {
	ucrypto_mech_t mech;
} ucrypto_ctx_t;

static int ucrypto_common_init(crypto_ctx_t *context, ucrypto_mech_t mech_type,
    uchar_t *key_str, size_t key_len, void *iv, size_t iv_len,
    ucrypto_op_t op_type);
static int ucrypto_common_update(crypto_ctx_t *context, uchar_t *in,
    size_t in_len, uchar_t *out, size_t *out_len, ucrypto_op_t op_type);
static int ucrypto_common_final(crypto_ctx_t *context, uchar_t *out,
    size_t *out_len, ucrypto_op_t op_type);
static int ucrypto_atomic(ucrypto_mech_t mech_type, uchar_t *key_str,
    size_t key_len, void *iv, size_t iv_len, uchar_t *in, size_t in_len,
    uchar_t *out, size_t *out_len, ucrypto_op_t op_type);

static int create_context(crypto_ctx_t *ctx, crypto_key_t key,
    crypto_mechanism_t mech, ucrypto_op_t op_type);
static void free_context(crypto_ctx_t *ctx);
static int set_key(crypto_key_t *key, uchar_t *key_data, size_t key_len,
    ucrypto_mech_t mech_type);
static int set_mechanism(crypto_mechanism_t *mech, ucrypto_mech_t mech_type,
    void *iv, size_t iv_len);


#pragma inline(create_context)
static int
create_context(crypto_ctx_t *ctx, crypto_key_t key, crypto_mechanism_t mech,
    ucrypto_op_t op_type)
{
	int rv;
	ucrypto_ctx_t *uctx = NULL;

	switch (mech.cm_type) {

	case CRYPTO_AES_CBC:
	case CRYPTO_AES_ECB:
	case CRYPTO_AES_CFB128:
	case CRYPTO_AES_CTR:
	case CRYPTO_AES_CCM:
	case CRYPTO_AES_GCM:
		/* UCRYPTO_SIGN and UCRYPTO_VERIFY ops are invalid here */
		if (op_type == UCRYPTO_SIGN || op_type == UCRYPTO_VERIFY) {
			rv = CRYPTO_MECH_NOT_SUPPORTED;
			break;
		}

		rv = aes_common_init(ctx, &mech, &key, NULL, NULL,
		    (op_type == UCRYPTO_ENCRYPT) ? B_TRUE : B_FALSE);
		break;

	case CRYPTO_RSA_PKCS:
	case CRYPTO_RSA_X_509:
	case CRYPTO_MD5_RSA_PKCS:
	case CRYPTO_SHA1_RSA_PKCS:
	case CRYPTO_SHA256_RSA_PKCS:
	case CRYPTO_SHA384_RSA_PKCS:
	case CRYPTO_SHA512_RSA_PKCS:
		/* UCRYPTO_ENCRYPT and UCRYPTO_DECRYPT ops are invalid here */
		if (op_type == UCRYPTO_ENCRYPT || op_type == UCRYPTO_DECRYPT) {
			rv = CRYPTO_MECH_NOT_SUPPORTED;
			break;
		}

		rv = rsaprov_sign_verify_common_init(ctx, &mech, &key,
		    NULL, NULL);
		break;

	default:
		rv = CRYPTO_MECH_NOT_SUPPORTED;
	}

	if (rv != CRYPTO_SUCCESS)
		return (rv);

	/* Create framework context */
	uctx = malloc(sizeof (ucrypto_ctx_t));
	if (uctx == NULL)
		return (CRYPTO_HOST_MEMORY);

	uctx->mech = mech.cm_type;
	ctx->cc_framework_private = uctx;

	return (CRYPTO_SUCCESS);
}

/*
 * This function frees the context for ucrypto, not for the provider
 * the module code handles that.
 */
#pragma inline(free_context)
static void
free_context(crypto_ctx_t *ctx)
{
	if (ctx->cc_framework_private != NULL)
		free(ctx->cc_framework_private);
}

#pragma inline(set_key)
static int
set_key(crypto_key_t *key, uchar_t *key_data, size_t key_len,
    ucrypto_mech_t mech_type)
{
	switch (mech_type) {

	case CRYPTO_AES_CBC:
	case CRYPTO_AES_ECB:
	case CRYPTO_AES_CFB128:
	case CRYPTO_AES_CTR:
	case CRYPTO_AES_CCM:
	case CRYPTO_AES_GCM:
		key->ck_format = CRYPTO_KEY_RAW;
		key->ck_data = key_data;
		key->ck_length = CRYPTO_BYTES2BITS(key_len);
		break;

	case CRYPTO_RSA_PKCS:
	case CRYPTO_RSA_X_509:
	case CRYPTO_MD5_RSA_PKCS:
	case CRYPTO_SHA1_RSA_PKCS:
	case CRYPTO_SHA256_RSA_PKCS:
	case CRYPTO_SHA384_RSA_PKCS:
	case CRYPTO_SHA512_RSA_PKCS:
		key->ck_format = CRYPTO_KEY_ATTR_LIST;
		key->ck_attrs = (crypto_object_attribute_t *)key_data;
		key->ck_count = key_len;
		break;

	default:
		return (CRYPTO_MECH_NOT_SUPPORTED);
	}

	return (CRYPTO_SUCCESS);
}

/*
 * If the mechanism is defined in this function, then it is supported.
 * If it is supported, sets up the initialization vector in crypto_mechanism_t.
 */
#pragma inline(set_mechanism)
static int
set_mechanism(crypto_mechanism_t *mech, ucrypto_mech_t mech_type,
    void *iv, size_t iv_len)
{
	if (mech_type == CRYPTO_AES_ECB && iv_len != 0)
		return (CRYPTO_MECHANISM_PARAM_INVALID);

	mech->cm_type = mech_type;
	mech->cm_param = (char *)iv;
	mech->cm_param_len = iv_len;
	return (CRYPTO_SUCCESS);
}

/* Return the version of this library */
int
ucrypto_version()
{
	return (UCRYPTO_VERSION);
}

/*
 * UCRYPTO common internal functions.
 */

static int
ucrypto_common_init(crypto_ctx_t *context, ucrypto_mech_t mech_type,
    uchar_t *key_str, size_t key_len, void *iv, size_t iv_len,
    ucrypto_op_t op_type)
{
	int rv;
	crypto_key_t key = { 0 };
	crypto_mechanism_t mech = { 0 };

	rv = set_mechanism(&mech, mech_type, iv, iv_len);
	if (rv != CRYPTO_SUCCESS)
		return (rv);

	rv = set_key(&key, key_str, key_len, mech_type);
	if (rv != CRYPTO_SUCCESS)
		return (rv);

	rv = create_context(context, key, mech, op_type);
	if (rv != CRYPTO_SUCCESS)
		return (rv);

	return (CRYPTO_SUCCESS);
}

#pragma inline(ucrypto_common_update)
static int
ucrypto_common_update(crypto_ctx_t *context, uchar_t *in, size_t in_len,
    uchar_t *out, size_t *out_len, ucrypto_op_t op_type)
{
	int rv;
	crypto_data_t idata = { 0 };
	crypto_data_t odata = { 0 };

	CRYPTO_SET_RAW_DATA(idata, in, in_len);

	switch (((ucrypto_ctx_t *)(context->cc_framework_private))->mech) {

	case CRYPTO_AES_CBC:
	case CRYPTO_AES_ECB:
	case CRYPTO_AES_CFB128:
	case CRYPTO_AES_CTR:
	case CRYPTO_AES_CCM:
	case CRYPTO_AES_GCM:
		if (out_len == NULL) {
			rv = CRYPTO_ARGUMENTS_BAD;
			goto cleanup;
		}

		CRYPTO_SET_RAW_DATA(odata, out, *out_len);

		if (op_type == UCRYPTO_ENCRYPT)
			rv = aes_encrypt_update(context, &idata, &odata, NULL);
		else if (op_type == UCRYPTO_DECRYPT)
			rv = aes_decrypt_update(context, &idata, &odata, NULL);
		else
			rv = CRYPTO_MECH_NOT_SUPPORTED;
		if (rv != CRYPTO_SUCCESS)
			goto cleanup;

		*out_len = odata.cd_length;

		break;

	case CRYPTO_RSA_PKCS:
	case CRYPTO_RSA_X_509:
	case CRYPTO_MD5_RSA_PKCS:
	case CRYPTO_SHA1_RSA_PKCS:
	case CRYPTO_SHA256_RSA_PKCS:
	case CRYPTO_SHA384_RSA_PKCS:
	case CRYPTO_SHA512_RSA_PKCS:
		if (op_type == UCRYPTO_SIGN)
			rv = rsaprov_sign_update(context, &idata, NULL);
		else if (op_type == UCRYPTO_VERIFY)
			rv = rsaprov_verify_update(context, &idata, NULL);
		else
			rv = CRYPTO_MECH_NOT_SUPPORTED;
		if (rv != CRYPTO_SUCCESS)
			goto cleanup;
		break;

	default:
		rv = CRYPTO_MECH_NOT_SUPPORTED;
		goto cleanup;
	}

	return (CRYPTO_SUCCESS);

cleanup:
	free_context(context);
	return (rv);
}

#pragma inline(ucrypto_common_final)
static int
ucrypto_common_final(crypto_ctx_t *context, uchar_t *out, size_t *out_len,
    ucrypto_op_t op_type)
{
	int rv;
	crypto_data_t odata = { 0 };

	if (out_len == NULL) {
		rv = CRYPTO_ARGUMENTS_BAD;
		goto cleanup;
	}

	CRYPTO_SET_RAW_DATA(odata, out, *out_len);

	switch (((ucrypto_ctx_t *)(context->cc_framework_private))->mech) {

	case CRYPTO_AES_CBC:
	case CRYPTO_AES_ECB:
	case CRYPTO_AES_CFB128:
	case CRYPTO_AES_CTR:
	case CRYPTO_AES_CCM:
	case CRYPTO_AES_GCM:
		if (op_type == UCRYPTO_ENCRYPT)
			rv = aes_encrypt_final(context, &odata, NULL);
		else if (op_type == UCRYPTO_DECRYPT)
			rv = aes_decrypt_final(context, &odata, NULL);
		else
			rv = CRYPTO_MECH_NOT_SUPPORTED;
		if (rv != CRYPTO_SUCCESS)
			goto cleanup;
		break;

	case CRYPTO_RSA_PKCS:
	case CRYPTO_RSA_X_509:
	case CRYPTO_MD5_RSA_PKCS:
	case CRYPTO_SHA1_RSA_PKCS:
	case CRYPTO_SHA256_RSA_PKCS:
	case CRYPTO_SHA384_RSA_PKCS:
	case CRYPTO_SHA512_RSA_PKCS:
		if (op_type == UCRYPTO_SIGN)
			rv = rsaprov_sign_final(context, &odata, NULL);
		else if (op_type == UCRYPTO_VERIFY)
			rv = rsaprov_verify_final(context, &odata, NULL);
		else
			rv = CRYPTO_MECH_NOT_SUPPORTED;
		if (rv != CRYPTO_SUCCESS)
			goto cleanup;
		break;

	default:
		rv = CRYPTO_MECH_NOT_SUPPORTED;
		goto cleanup;
	}

	*out_len = odata.cd_length;

cleanup:

	free_context(context);
	return (rv);
}


#pragma inline(ucrypto_atomic)
static int
ucrypto_atomic(ucrypto_mech_t mech_type, uchar_t *key_str, size_t key_len,
    void *iv, size_t iv_len, uchar_t *in, size_t in_len,
    uchar_t *out, size_t *out_len, ucrypto_op_t op_type)
{
	int			rv;
	crypto_mechanism_t	mech = { 0 };
	crypto_data_t		idata = { 0 };
	crypto_data_t		odata = { 0 };
	crypto_key_t		key = { 0 };

	if (out_len == NULL)
		return (CRYPTO_ARGUMENTS_BAD);

	rv = set_mechanism(&mech, mech_type, iv, iv_len);
	if (rv != CRYPTO_SUCCESS)
		return (rv);

	rv = set_key(&key, key_str, key_len, mech_type);
	if (rv != CRYPTO_SUCCESS)
		return (rv);

	CRYPTO_SET_RAW_DATA(idata, in, in_len);
	CRYPTO_SET_RAW_DATA(odata, out, *out_len);

	switch (mech_type) {

	case CRYPTO_AES_CBC:
	case CRYPTO_AES_ECB:
	case CRYPTO_AES_CFB128:
	case CRYPTO_AES_CTR:
	case CRYPTO_AES_CCM:
	case CRYPTO_AES_GCM:
		if (op_type == UCRYPTO_ENCRYPT)
			rv = aes_encrypt_atomic(NULL, NULL, &mech, &key,
			    &idata, &odata, NULL, NULL);
		else if (op_type == UCRYPTO_DECRYPT)
			rv = aes_decrypt_atomic(NULL, NULL, &mech, &key,
			    &idata, &odata, NULL, NULL);
		else
			rv = CRYPTO_MECH_NOT_SUPPORTED;
		break;

	case CRYPTO_RSA_PKCS:
	case CRYPTO_RSA_X_509:
	case CRYPTO_MD5_RSA_PKCS:
	case CRYPTO_SHA1_RSA_PKCS:
	case CRYPTO_SHA256_RSA_PKCS:
	case CRYPTO_SHA384_RSA_PKCS:
	case CRYPTO_SHA512_RSA_PKCS:
		if (op_type == UCRYPTO_ENCRYPT)
			rv = rsaprov_encrypt_atomic(NULL, NULL, &mech,
			    &key, &idata, &odata, NULL, NULL);
		else if (op_type == UCRYPTO_DECRYPT)
			rv = rsaprov_decrypt_atomic(NULL, NULL, &mech,
			    &key, &idata, &odata, NULL, NULL);
		else if (op_type == UCRYPTO_SIGN)
			rv = rsaprov_sign_atomic(NULL, NULL, &mech,
			    &key, &idata, &odata, NULL, NULL);
		else if (op_type == UCRYPTO_VERIFY)
			rv = rsaprov_verify_atomic(NULL, NULL, &mech,
			    &key, &idata, &odata, NULL, NULL);
		else
			rv = CRYPTO_MECH_NOT_SUPPORTED;
		break;

	default:
		return (CRYPTO_MECH_NOT_SUPPORTED);
	}

	*out_len = odata.cd_length;
	return (rv);
}


/*
 * UCRYPTO exported functions.
 */

/* Encrypt API */
#pragma inline(ucrypto_encrypt_init)
int
ucrypto_encrypt_init(crypto_ctx_t *context, ucrypto_mech_t mech_type,
    uchar_t *key_str, size_t key_len, void *iv, size_t iv_len)
{
	return (ucrypto_common_init(context, mech_type, key_str, key_len,
	    iv, iv_len, UCRYPTO_ENCRYPT));
}

#pragma inline(ucrypto_encrypt_update)
int
ucrypto_encrypt_update(crypto_ctx_t *context, uchar_t *in, size_t in_len,
    uchar_t *out, size_t *out_len)
{
	return (ucrypto_common_update(context, in, in_len, out, out_len,
	    UCRYPTO_ENCRYPT));
}

#pragma inline(ucrypto_encrypt_final)
int
ucrypto_encrypt_final(crypto_ctx_t *context, uchar_t *out, size_t *out_len)
{
	return (ucrypto_common_final(context, out, out_len, UCRYPTO_ENCRYPT));
}

#pragma inline(ucrypto_encrypt)
int
ucrypto_encrypt(ucrypto_mech_t mech_type, uchar_t *key_str, size_t key_len,
    void *iv, size_t iv_len, uchar_t *in, size_t in_len,
    uchar_t *out, size_t *out_len)
{
	return (ucrypto_atomic(mech_type, key_str, key_len, iv, iv_len, in,
	    in_len, out, out_len, UCRYPTO_ENCRYPT));
}

/* Decrypt API */
#pragma inline(ucrypto_decrypt_init)
int
ucrypto_decrypt_init(crypto_ctx_t *context, ucrypto_mech_t mech_type,
    uchar_t *key_str, size_t key_len, void *iv, size_t iv_len)
{
	return (ucrypto_common_init(context, mech_type, key_str, key_len,
	    iv, iv_len, UCRYPTO_DECRYPT));
}

#pragma inline(ucrypto_decrypt_update)
int
ucrypto_decrypt_update(crypto_ctx_t *context, uchar_t *in, size_t in_len,
    uchar_t *out, size_t *out_len)
{
	return (ucrypto_common_update(context, in, in_len, out, out_len,
	    UCRYPTO_DECRYPT));
}

#pragma inline(ucrypto_decrypt_final)
int
ucrypto_decrypt_final(crypto_ctx_t *context, uchar_t *out, size_t *out_len)
{
	return (ucrypto_common_final(context, out, out_len, UCRYPTO_DECRYPT));
}

#pragma inline(ucrypto_decrypt)
int
ucrypto_decrypt(ucrypto_mech_t mech_type, uchar_t *key_str, size_t key_len,
    void *iv, size_t iv_len, uchar_t *in, size_t in_len,
    uchar_t *out, size_t *out_len)
{
	return (ucrypto_atomic(mech_type, key_str, key_len, iv, iv_len, in,
	    in_len, out, out_len, UCRYPTO_DECRYPT));
}

/* Sign API */
#pragma inline(ucrypto_sign_init)
int
ucrypto_sign_init(crypto_ctx_t *context, ucrypto_mech_t mech_type,
    uchar_t *key_str, size_t key_len, void *iv, size_t iv_len)
{
	return (ucrypto_common_init(context, mech_type, key_str, key_len,
	    iv, iv_len, UCRYPTO_SIGN));
}

#pragma inline(ucrypto_sign_update)
int
ucrypto_sign_update(crypto_ctx_t *context, uchar_t *data_str, size_t data_len)
{
	return (ucrypto_common_update(context, data_str, data_len,
	    NULL, NULL, UCRYPTO_SIGN));
}

#pragma inline(ucrypto_sign_final)
int
ucrypto_sign_final(crypto_ctx_t *context, uchar_t *sig_str, size_t *sig_len)
{
	return (ucrypto_common_final(context, sig_str, sig_len,
	    UCRYPTO_SIGN));
}

#pragma inline(ucrypto_sign)
int
ucrypto_sign(ucrypto_mech_t mech_type, uchar_t *key_str, size_t key_len,
    void *iv, size_t iv_len, uchar_t *data_str, size_t data_len,
    uchar_t *sig_str, size_t *sig_len)
{
	return (ucrypto_atomic(mech_type, key_str, key_len, iv, iv_len,
	    data_str, data_len, sig_str, sig_len, UCRYPTO_SIGN));
}

/* Verify API */
#pragma inline(ucrypto_verify_init)
int
ucrypto_verify_init(crypto_ctx_t *context, ucrypto_mech_t mech_type,
    uchar_t *key_str, size_t key_len, void *iv, size_t iv_len)
{
	return (ucrypto_common_init(context, mech_type, key_str, key_len,
	    iv, iv_len, UCRYPTO_VERIFY));
}

#pragma inline(ucrypto_verify_update)
int
ucrypto_verify_update(crypto_ctx_t *context, uchar_t *data_str, size_t data_len)
{
	return (ucrypto_common_update(context, data_str, data_len,
	    NULL, NULL, UCRYPTO_VERIFY));
}

#pragma inline(ucrypto_verify_final)
int
ucrypto_verify_final(crypto_ctx_t *context, uchar_t *sig_str, size_t *sig_len)
{
	return (ucrypto_common_final(context, sig_str, sig_len,
	    UCRYPTO_VERIFY));
}

#pragma inline(ucrypto_verify)
int
ucrypto_verify(ucrypto_mech_t mech_type, uchar_t *key_str, size_t key_len,
    void *iv, size_t iv_len, uchar_t *data_str, size_t data_len,
    uchar_t *sig_str, size_t *sig_len)
{
	return (ucrypto_atomic(mech_type, key_str, key_len, iv, iv_len,
	    data_str, data_len, sig_str, sig_len, UCRYPTO_VERIFY));
}
