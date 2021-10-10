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
 * Copyright 2000 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2006, Oracle and/or its affiliates. All rights reserved.
 *
 * iSCSI Software Initiator
 */

#ifndef	_ISCSI_AUTHCLIENTGLUE_H
#define	_ISCSI_AUTHCLIENTGLUE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <md5.h>

typedef MD5_CTX IscsiAuthMd5Context;

extern int iscsiAuthIscsiServerHandle;
extern int iscsiAuthIscsiClientHandle;

#ifdef __cplusplus
}
#endif

#endif	/* _ISCSI_AUTHCLIENTGLUE_H */
