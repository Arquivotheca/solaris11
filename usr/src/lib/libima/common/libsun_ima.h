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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/* header file for iSCSI tunable parameters properties function */

#ifndef	_LIBSUN_IMA_H
#define	_LIBSUN_IMA_H

#define	ISCSI_TUNABLE_MAX	3600
#define	ISCSI_TUNABLE_MIN	0
#ifdef __cplusplus
extern "C" {
#endif

#include <ima.h>

typedef enum {
	ISCSI_RX_TIMEOUT_VALUE = 1,
	ISCSI_CONN_DEFAULT_LOGIN_MAX = 2,
	ISCSI_LOGIN_POLLING_DELAY = 3
} ISCSI_TUNABLE_OBJECT_TYPE;

typedef struct _ISCSI_TUNABLE_PARAM {
	ISCSI_TUNABLE_OBJECT_TYPE tunable_objectType;
	IMA_CHAR *tunable_objectValue;
} ISCSI_TUNABLE_PARAM;

IMA_API IMA_STATUS SUN_IMA_SetTunableProperties(
		IMA_OID oid,
		ISCSI_TUNABLE_PARAM *param
);

IMA_API IMA_STATUS SUN_IMA_GetTunableProperties(
		IMA_OID oid,
		ISCSI_TUNABLE_PARAM *param
);

#ifdef __cplusplus
}
#endif

#endif /* _LIBSUN_IMA_H */
