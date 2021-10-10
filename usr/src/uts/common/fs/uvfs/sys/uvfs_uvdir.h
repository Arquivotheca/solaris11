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

#ifndef _SYS_UVFS_UVDIR_H
#define	_SYS_UVFS_UVDIR_H

#include <sys/isa_defs.h>
#include <sys/types32.h>
#include <sys/uvfs_vfsops.h>
#ifdef	__cplusplus
extern "C" {
#endif

extern int uvfs_dirlook(uvnode_t *uvdp, char *name, vnode_t **vpp, cred_t *);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_UVFS_UVDIR_H */
