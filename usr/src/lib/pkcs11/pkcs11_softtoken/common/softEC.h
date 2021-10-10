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
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SOFTEC_H
#define	_SOFTEC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <security/pkcs11t.h>
#include <ecc/ecc_impl.h>
#include "softObject.h"
#include "softSession.h"


typedef struct soft_ecc_ctx {
	soft_object_t *key;
	ECParams ecparams;
} soft_ecc_ctx_t;


extern CK_RV soft_ec_genkey_pair(soft_object_t *pubkey, soft_object_t *prikey);

extern CK_RV soft_ec_key_derive(soft_object_t *basekey,
    soft_object_t *secretkey, void *mech_params, size_t mech_params_len);

extern CK_RV soft_ecc_sign_verify_init_common(soft_session_t *session_p,
    CK_MECHANISM_PTR pMechanism, soft_object_t *key_p, boolean_t sign);

extern CK_RV soft_ecc_sign(soft_session_t *session_p,
    CK_BYTE_PTR pData, CK_ULONG ulDataLen,
    CK_BYTE_PTR pSigned, CK_ULONG_PTR pulSignedLen);

extern CK_RV soft_ecc_verify(soft_session_t *session_p,
    CK_BYTE_PTR pData, CK_ULONG ulDataLen,
    CK_BYTE_PTR pSignature, CK_ULONG ulSignatureLen);

extern CK_RV soft_ecc_digest_sign_common(soft_session_t *session_p,
    CK_BYTE_PTR pData, CK_ULONG ulDataLen,
    CK_BYTE_PTR pSigned, CK_ULONG_PTR pulSignedLen,
    boolean_t Final);

extern CK_RV soft_ecc_digest_verify_common(soft_session_t *session_p,
    CK_BYTE_PTR pData, CK_ULONG ulDataLen,
    CK_BYTE_PTR pSigned, CK_ULONG ulSignedLen,
    boolean_t Final);

/* This threesome of functions really belongs in softObject.h. */
extern CK_RV soft_get_public_key_attribute(soft_object_t *object_p,
    CK_ATTRIBUTE_PTR template);
extern CK_RV soft_get_private_key_attribute(soft_object_t *object_p,
    CK_ATTRIBUTE_PTR template);
extern CK_RV set_extra_attr_to_object(soft_object_t *object_p,
    CK_ATTRIBUTE_TYPE type, CK_ATTRIBUTE_PTR template);


#ifdef	__cplusplus
}
#endif

#endif /* _SOFTEC_H */
