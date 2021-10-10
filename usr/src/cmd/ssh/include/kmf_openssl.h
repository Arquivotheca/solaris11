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

#ifndef	_KMF_OPENSSL_H
#define	_KMF_OPENSSL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <kmftypes.h>
#include "key.h"
#include "kmfdef.h"

int kmf_openssl_set_verify_attrs(KMF_ATTRIBUTE *, KMF_KEYSTORE_TYPE *);
int kmf_openssl_set_validate_attrs(KMF_ATTRIBUTE *, KMF_KEYSTORE_TYPE *);
Key *kmf_openssl_get_key_from_cert(KMF_DATA *cert);

#ifdef __cplusplus
}
#endif

#endif /* _KMF_OPENSSL_H */
