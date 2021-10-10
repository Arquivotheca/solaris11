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

#ifndef _SOFTRSA_H
#define	_SOFTRSA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <security/pkcs11t.h>
#include <padding/padding.h>
#include <rsa/rsa_impl.h>
#include "softObject.h"
#include "softSession.h"


typedef struct soft_rsa_ctx {
	soft_object_t *key;
} soft_rsa_ctx_t;

/*
 * Function Prototypes.
 */

CK_RV soft_rsa_genkey_pair(soft_object_t *pubkey, soft_object_t *prikey);

CK_RV soft_rsa_crypt_init_common(soft_session_t *session_p,
    CK_MECHANISM_PTR pMechanism, soft_object_t *key_p, soft_session_op_t op);

CK_RV soft_rsa_encrypt(soft_object_t *key, CK_BYTE_PTR in, uint32_t in_len,
    CK_BYTE_PTR out, int realpublic);

CK_RV soft_rsa_decrypt(soft_object_t *key, CK_BYTE_PTR in, uint32_t in_len,
    CK_BYTE_PTR out);

CK_RV soft_rsa_encrypt_common(soft_session_t *session_p,
    CK_BYTE_PTR pData, CK_ULONG ulDataLen,
    CK_BYTE_PTR pEncrypted, CK_ULONG_PTR pulEncryptedLen,
    CK_MECHANISM_TYPE mechanism);

CK_RV soft_rsa_decrypt_common(soft_session_t *session_p,
    CK_BYTE_PTR pEncrypted, CK_ULONG ulEncryptedLen,
    CK_BYTE_PTR pData, CK_ULONG_PTR pulDataLen,
    CK_MECHANISM_TYPE mechanism);

CK_RV soft_rsa_sign_verify_init_common(soft_session_t *session_p,
    CK_MECHANISM_PTR pMechanism, soft_object_t *key_p, boolean_t sign);

CK_RV soft_rsa_sign_common(soft_session_t *session_p,
    CK_BYTE_PTR pData, CK_ULONG ulDataLen,
    CK_BYTE_PTR pSigned, CK_ULONG_PTR pulSignedLen,
    CK_MECHANISM_TYPE mechanism);

CK_RV soft_rsa_verify_common(soft_session_t *session_p,
    CK_BYTE_PTR pData, CK_ULONG ulDataLen,
    CK_BYTE_PTR pSignature, CK_ULONG ulSignatureLen,
    CK_MECHANISM_TYPE mechanism);

CK_RV soft_rsa_digest_sign_common(soft_session_t *session_p,
    CK_BYTE_PTR pData, CK_ULONG ulDataLen,
    CK_BYTE_PTR pSigned, CK_ULONG_PTR pulSignedLen,
    CK_MECHANISM_TYPE mechanism, boolean_t Final);

CK_RV soft_rsa_digest_verify_common(soft_session_t *session_p,
    CK_BYTE_PTR pData, CK_ULONG ulDataLen,
    CK_BYTE_PTR pSigned, CK_ULONG ulSignedLen,
    CK_MECHANISM_TYPE mechanism, boolean_t Final);

CK_RV soft_rsa_verify_recover(soft_session_t *session_p,
    CK_BYTE_PTR pSignature, CK_ULONG ulSignatureLen,
    CK_BYTE_PTR pData, CK_ULONG_PTR pulDataLen);


#ifdef	__cplusplus
}
#endif

#endif /* _SOFTRSA_H */
