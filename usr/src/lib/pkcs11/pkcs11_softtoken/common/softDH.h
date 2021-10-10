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

#ifndef _SOFTDH_H
#define	_SOFTDH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <security/pkcs11t.h>
#include <dh/dh_impl.h>
#include "softObject.h"
#include "softSession.h"


/*
 * Function Prototypes.
 */
CK_RV soft_dh_genkey_pair(soft_object_t *pubkey, soft_object_t *prikey);
CK_RV soft_dh_key_derive(soft_object_t *basekey, soft_object_t *secretkey,
    void *publicvalue, size_t publicvaluelen);


#ifdef	__cplusplus
}
#endif

#endif /* _SOFTDH_H */
