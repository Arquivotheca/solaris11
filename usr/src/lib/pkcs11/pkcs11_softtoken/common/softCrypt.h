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

#ifndef _SOFTCRYPT_H
#define	_SOFTCRYPT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <security/pkcs11t.h>
#include <modes/modes.h>
#include <aes/aes_impl.h>
#include <blowfish/blowfish_impl.h>
#include <des/des_impl.h>
#include "softObject.h"
#include "softSession.h"

#define	DES_MAC_LEN	(DES_BLOCK_LEN / 2)

typedef struct soft_aes_ctx {
	void *key_sched;		/* pointer to key schedule */
	size_t keysched_len;		/* Length of the key schedule */
	uint8_t ivec[AES_BLOCK_LEN];	/* initialization vector */
	uint8_t data[AES_BLOCK_LEN];	/* for use by update */
	size_t remain_len;		/* for use by update */
	void *mode_ctx;			/* context for various modes */
} soft_aes_ctx_t;

typedef struct soft_blowfish_ctx {
	void *key_sched;		/* pointer to key schedule */
	size_t keysched_len;		/* Length of the key schedule */
	uint8_t ivec[BLOWFISH_BLOCK_LEN];	/* initialization vector */
	uint8_t data[BLOWFISH_BLOCK_LEN];	/* for use by update */
	size_t remain_len;			/* for use by update */
	void *mode_ctx;			/* context for CBC mode */
} soft_blowfish_ctx_t;

typedef struct soft_des_ctx {
	void *key_sched;		/* pointer to key schedule */
	size_t keysched_len;		/* Length of the key schedule */
	uint8_t ivec[DES_BLOCK_LEN];	/* initialization vector */
	uint8_t data[DES_BLOCK_LEN];	/* for use by update */
	size_t remain_len;		/* for use by update */
	void *mode_ctx;			/* context for CBC mode */
	CK_KEY_TYPE key_type;		/* used to determine DES or DES3 */
	size_t mac_len;			/* digest len in bytes */
} soft_des_ctx_t;

/*
 * Function Prototypes.
 */

/* AES */
void *aes_ecb_ctx_init(void *key_sched, size_t size);
void *aes_cbc_ctx_init(void *key_sched, size_t size, uint8_t *ivec);
void *aes_ctr_ctx_init(void *key_sched, size_t size, uint8_t *ivec);

#ifdef __amd64
int soft_intel_aes_instructions_present(void);
#endif

CK_RV soft_aes_crypt_init_common(soft_session_t *session_p,
    CK_MECHANISM_PTR pMechanism, soft_object_t *key_p, soft_session_op_t op);

CK_RV soft_aes_encrypt_common(soft_session_t *session_p,
    CK_BYTE_PTR pData, CK_ULONG ulDataLen,
    CK_BYTE_PTR pEncrypted, CK_ULONG_PTR pulEncryptedLen,
    boolean_t update);

CK_RV soft_aes_decrypt_common(soft_session_t *session_p,
    CK_BYTE_PTR pEncrypted, CK_ULONG ulEncryptedLen,
    CK_BYTE_PTR pData, CK_ULONG_PTR pulDataLen,
    boolean_t update);

/* Arcfour (RC4) */
CK_RV soft_arcfour_crypt_init(soft_session_t *session_p,
    CK_MECHANISM_PTR pMechanism, soft_object_t *key_p, soft_session_op_t op);

CK_RV soft_arcfour_crypt(crypto_active_op_t *active_op,
    CK_BYTE_PTR input, CK_ULONG inputlen,
    CK_BYTE_PTR output, CK_ULONG_PTR outputlenp);

/* Blowfish */
void *blowfish_cbc_ctx_init(void *key_sched, size_t size, uint8_t *ivec);

CK_RV soft_blowfish_crypt_init_common(soft_session_t *session_p,
    CK_MECHANISM_PTR pMechanism, soft_object_t *key_p, soft_session_op_t op);

CK_RV soft_blowfish_encrypt_common(soft_session_t *session_p,
    CK_BYTE_PTR pData, CK_ULONG ulDataLen,
    CK_BYTE_PTR pEncrypted, CK_ULONG_PTR pulEncryptedLen,
    boolean_t update);

CK_RV soft_blowfish_decrypt_common(soft_session_t *session_p,
    CK_BYTE_PTR pEncrypted, CK_ULONG ulEncryptedLen,
    CK_BYTE_PTR pData, CK_ULONG_PTR pulDataLen,
    boolean_t update);

/* DES */
void *des_ecb_ctx_init(void *key_sched, size_t size, CK_KEY_TYPE type);
void *des_cbc_ctx_init(void *key_sched, size_t size, uint8_t *ivec,
    CK_KEY_TYPE type);

CK_RV soft_des_crypt_init_common(soft_session_t *session_p,
    CK_MECHANISM_PTR pMechanism, soft_object_t *key_p, soft_session_op_t op);

CK_RV soft_des_encrypt_common(soft_session_t *session_p,
    CK_BYTE_PTR pData, CK_ULONG ulDataLen,
    CK_BYTE_PTR pEncrypted, CK_ULONG_PTR pulEncryptedLen,
    boolean_t update);

CK_RV soft_des_decrypt_common(soft_session_t *session_p,
    CK_BYTE_PTR pEncrypted, CK_ULONG ulEncryptedLen,
    CK_BYTE_PTR pData, CK_ULONG_PTR pulDataLen,
    boolean_t update);

CK_RV soft_des_sign_verify_init_common(soft_session_t *session_p,
    CK_MECHANISM_PTR pMechanism, soft_object_t *key_p, boolean_t sign_op);

CK_RV soft_des_sign_verify_common(soft_session_t *session_p,
    CK_BYTE_PTR pData, CK_ULONG ulDataLen,
    CK_BYTE_PTR pSigned, CK_ULONG_PTR pulSignedLen,
    boolean_t sign_op, boolean_t Final);

CK_RV soft_des_mac_sign_verify_update(soft_session_t *session_p,
    CK_BYTE_PTR pPart, CK_ULONG ulPartLen);

/* Miscellaneous */

void soft_add_pkcs7_padding(CK_BYTE *buf, int block_size, CK_ULONG data_len);

CK_RV soft_remove_pkcs7_padding(CK_BYTE *pData, CK_ULONG padded_len,
    CK_ULONG *pulDataLen);

#ifdef	__cplusplus
}
#endif

#endif /* _SOFTCRYPT_H */
