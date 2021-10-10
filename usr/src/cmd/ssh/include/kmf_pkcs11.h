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

#ifndef	_KMF_PKCS11_H
#define	_KMF_PKCS11_H

#ifdef __cplusplus
extern "C" {
#endif

#include <kmftypes.h>
#include "kmfdef.h"

int pkcs11_set_sign_attrs(KMF_ATTRIBUTE *, KMF_KEYSTORE_TYPE *, kmf_key_t *);
int pkcs11_set_find_attrs(KMF_ATTRIBUTE *, KMF_KEYSTORE_TYPE *, uint32_t *);

int pkcs11_configure_keystore(KMF_HANDLE_T, uchar_t *);
int pkcs11_get_privkey(kmf_key_t *, int, char *pin);
int pkcs11_load_certificate(kmf_key_t *, char *);
KMF_RETURN token_auth_needed(KMF_HANDLE_T, char *, int *);

#ifdef __cplusplus
}
#endif

#endif /* _KMF_PKCS11_H */
