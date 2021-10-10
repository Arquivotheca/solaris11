/* BEGIN CSTYLED */

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
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include "drm_linux.h"

void
kref_init(struct kref *kref)
{
	atomic_set(&kref->refcount, 1);
}

void
kref_get(struct kref *kref)
{
	atomic_inc(&kref->refcount);
}

void
kref_put(struct kref *kref, void (*release)(struct kref *kref))
{
	if (!atomic_dec_uint_nv(&kref->refcount))
		release(kref);
}

unsigned int
hweight16(unsigned int w)
{
	w = (w & 0x5555) + ((w >> 1) & 0x5555);
	w = (w & 0x3333) + ((w >> 2) & 0x3333);
	w = (w & 0x0F0F) + ((w >> 4) & 0x0F0F);
	w = (w & 0x00FF) + ((w >> 8) & 0x00FF);
	return (w);
}

long
IS_ERR(const void *ptr)
{
	return ((unsigned long)ptr >= (unsigned long)-255);
}

