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
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SMBSRV_MLSVC_H
#define	_SMBSRV_MLSVC_H

#include <smbsrv/smb_share.h>
#include <smbsrv/ndl/netlogon.ndl>

#ifdef __cplusplus
extern "C" {
#endif

void dssetup_initialize(void);
void srvsvc_initialize(void);
void wkssvc_initialize(void);
void lsarpc_initialize(void);
void logr_initialize(void);
void netr_initialize(void);
void samr_initialize(void);
void svcctl_initialize(void);
void winreg_initialize(void);
int srvsvc_gettime(unsigned long *);
void msgsvcsend_initialize(void);
void netdfs_initialize(void);

void logr_finalize(void);
void svcctl_finalize(void);
void netdfs_finalize(void);

/* Generic functions to get/set windows Security Descriptors */
uint32_t srvsvc_sd_get(smb_share_t *, uint8_t *, uint32_t *);
uint32_t srvsvc_sd_set(smb_share_t *, uint8_t *);

#ifdef __cplusplus
}
#endif


#endif /* _SMBSRV_MLSVC_H */
