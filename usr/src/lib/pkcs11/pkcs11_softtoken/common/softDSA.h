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

#ifndef _SOFTDSA_H
#define	_SOFTDSA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <security/pkcs11t.h>
#include <padding/padding.h>
#define	_DSA_FIPS_POST
#include <dsa/dsa_impl.h>
#include "softObject.h"
#include "softSession.h"


typedef struct soft_dsa_ctx {
	soft_object_t *key;
} soft_dsa_ctx_t;


/*
 * Function Prototypes.
 */

CK_RV soft_dsa_genkey_pair(soft_object_t *pubkey, soft_object_t *prikey);

CK_RV soft_dsa_sign_verify_init_common(soft_session_t *session_p,
    CK_MECHANISM_PTR pMechanism, soft_object_t *key_p, boolean_t sign);

CK_RV soft_dsa_sign(soft_session_t *session_p,
    CK_BYTE_PTR pData, CK_ULONG ulDataLen,
    CK_BYTE_PTR pSigned, CK_ULONG_PTR pulSignedLen);

CK_RV soft_dsa_verify(soft_session_t *session_p,
    CK_BYTE_PTR pData, CK_ULONG ulDataLen,
    CK_BYTE_PTR pSignature, CK_ULONG ulSignatureLen);

CK_RV soft_dsa_digest_sign_common(soft_session_t *session_p,
    CK_BYTE_PTR pData, CK_ULONG ulDataLen,
    CK_BYTE_PTR pSigned, CK_ULONG_PTR pulSignedLe, boolean_t Final);

CK_RV soft_dsa_digest_verify_common(soft_session_t *session_p,
    CK_BYTE_PTR pData, CK_ULONG ulDataLen,
    CK_BYTE_PTR pSigned, CK_ULONG ulSignedLen, boolean_t Final);

#ifdef	__cplusplus
}
#endif

#endif /* _SOFTDSA_H */
