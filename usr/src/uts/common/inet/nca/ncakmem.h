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
 * Copyright (c) 1999, 2000, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _NCAKMEM_H
#define	_NCAKMEM_H

#include <sys/types.h>
#include <sys/vmem.h>
#include <vm/page.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef _KERNEL

extern void nca_vmem_init(void);
extern void nca_vmem_fini(void);

extern page_t **kmem_phys_alloc(size_t, int, char **);
extern void kmem_phys_free(page_t **);
extern void *kmem_phys_mapin(page_t **, void *, int);
extern void kmem_phys_mapout(page_t **, void *);

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _NCAKMEM_H */
