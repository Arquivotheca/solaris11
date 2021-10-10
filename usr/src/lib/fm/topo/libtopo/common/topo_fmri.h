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

#ifndef _TOPO_FMRI_H
#define	_TOPO_FMRI_H

#include <sys/nvpair.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

extern nvlist_t *topo_fmri_create(topo_hdl_t *, const char *, const char *,
    topo_instance_t, nvlist_t *, int *);
extern ulong_t topo_fmri_strhash_one(const char *, size_t);

#ifdef __cplusplus
}
#endif

#endif /* _TOPO_FMRI_H */
