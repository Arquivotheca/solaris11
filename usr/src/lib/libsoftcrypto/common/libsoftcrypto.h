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

#ifndef _LIBSOFTCRYPTO_H
#define	_LIBSOFTCRYPTO_H
#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <strings.h>
#include <sys/crypto/spi.h>
#include <sys/crypto/common.h>
#include <security/cryptoki.h>

/*
 * Mechanism list for ucrypto.
 * The mechanism value of 0 is used for mechanism invalid.
 */
typedef enum ucrypto_mech {
	CRYPTO_AES_ECB = 1,
	CRYPTO_AES_CBC,
	CRYPTO_AES_CBC_PAD,	/* Unsupported */
	CRYPTO_AES_CTR,
	CRYPTO_AES_CCM,
	CRYPTO_AES_GCM,
	CRYPTO_AES_GMAC,	/* Unsupported */
	CRYPTO_AES_CFB128,

	CRYPTO_RSA_PKCS = 31,
	CRYPTO_RSA_X_509,
	CRYPTO_MD5_RSA_PKCS,
	CRYPTO_SHA1_RSA_PKCS,
	CRYPTO_SHA256_RSA_PKCS,
	CRYPTO_SHA384_RSA_PKCS,
	CRYPTO_SHA512_RSA_PKCS	/* = 37 */
} ucrypto_mech_t;

/*
 * This is a private interface to software crypto in the libsoftcrypto
 * library.
 *
 * Crypto context:
 *
 * crypto_ctx_t is a pointer to the crypto context that must be passed to
 * each of the functions to perform a multi-part operation.  The init
 * function will allocate and final function will the free the context.
 * On failure of any of the multi-part functions, the context will
 * be freed.
 * For atomic operations, the contexts in handled internally and no
 * user interaction is needed.
 *
 * Output length:
 * For functions that have an "out_len", this variable will be set to
 * the length of the data returned by the operation.  In case the function
 * failures the value is not guaranteed to be zero.
 */

/* Encrypt multi-part */
extern int ucrypto_encrypt_init(crypto_ctx_t *context,
    ucrypto_mech_t mech_type, uchar_t *key_str, size_t key_len,
    void *iv, size_t iv_len);

extern int ucrypto_encrypt_update(crypto_ctx_t *context, uchar_t *in,
    size_t in_len, uchar_t *out, size_t *out_len);

extern int ucrypto_encrypt_final(crypto_ctx_t *context, uchar_t *out,
    size_t *out_len);

/* Encrypt atomic */
extern int ucrypto_encrypt(ucrypto_mech_t mech_type, uchar_t *key_str,
	size_t key_len, void *iv, size_t iv_len, uchar_t *in,
	size_t in_len, uchar_t *out, size_t *out_len);

/* Decrypt multi-part */
extern int ucrypto_decrypt_init(crypto_ctx_t *context,
    ucrypto_mech_t mech_type, uchar_t *key_str, size_t key_len,
    void *iv, size_t iv_len);

extern int ucrypto_decrypt_update(crypto_ctx_t *context, uchar_t *in,
    size_t in_len, uchar_t *out, size_t *out_len);

extern int ucrypto_decrypt_final(crypto_ctx_t *context, uchar_t *out,
    size_t *out_len);

/* Decrypt atomic */
extern int ucrypto_decrypt(ucrypto_mech_t mech_type, uchar_t *key_str,
    size_t key_len, void *iv, size_t iv_len, uchar_t *in,
    size_t in_len, uchar_t *out, size_t *out_len);

/* Sign multi-part */
extern int ucrypto_sign_init(crypto_ctx_t *context, ucrypto_mech_t mech_type,
    uchar_t *key_str, size_t key_len, void *iv, size_t iv_len);

extern int ucrypto_sign_update(crypto_ctx_t *context,
    uchar_t *data_str, size_t data_len);

extern int ucrypto_sign_final(crypto_ctx_t *context,
    uchar_t *sig_str, size_t *sig_len);

/* Sign atomic */
extern int ucrypto_sign(ucrypto_mech_t mech_type,
    uchar_t *key_str, size_t key_len, void *iv, size_t iv_len,
    uchar_t *data_str, size_t data_len, uchar_t *sig_str, size_t *sig_len);

/* Verify multi-part */
extern int ucrypto_verify_init(crypto_ctx_t *context, ucrypto_mech_t mech_type,
    uchar_t *key_str, size_t key_len, void *iv, size_t iv_len);

extern int ucrypto_verify_update(crypto_ctx_t *context,
    uchar_t *data_str, size_t data_len);

extern int ucrypto_verify_final(crypto_ctx_t *context,
    uchar_t *sig_str, size_t *sig_len);

/* Verify atomic */
extern int ucrypto_verify(ucrypto_mech_t mech_type,
    uchar_t *key_str, size_t key_len, void *iv, size_t iv_len,
    uchar_t *data_str, size_t data_len, uchar_t *sig, size_t *sig_len);

/*
 * Sets in the given pointer a deliminated string of supported mechanisms
 * with their value number specifed in ucrypto_mech_t.  There are no
 * whitespaces in the string.  The return value is the length of the string.
 * If the pointer given is NULL, the function will return the length of the
 * string.
 *
 * The format is as below:
 *   < number of supported mechanisms >:                   \
 *   < name of mechanism >,< number of mechanism >;        \
 *   < name of mechanism >,< number of mechanism >;        \
 *   ... repeat until finished.
 */
extern int ucrypto_get_mechlist(char *str);

/*
 * Returns the mechanism string value for a given mechanism id number.
 * This will return NULL for invalid mechanisms.
 */
extern const char *ucrypto_id2mech(ucrypto_mech_t mech_type);

/*
 * Returns the mechanism id number for a given mechanism string.
 * This will return 0 for invalid mechanisms.
 */
extern ucrypto_mech_t ucrypto_mech2id(const char *str);

/* Returns the version of this library */
extern int ucrypto_version();

#ifdef __cplusplus
}
#endif

#endif	/* _LIBSOFTCRYPTO_H */
