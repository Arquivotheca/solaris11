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

#include "drm.h"
#include "drmP.h"
#include "i915_drm.h"

int 
copyin32_i915_batchbuffer(void * dest, void * src)
{
	struct drm_i915_batchbuffer *dest64 = dest;
	struct drm_i915_batchbuffer32 dest32;

	DRM_COPYFROM_WITH_RETURN(&dest32, (void *)src, sizeof (dest32));

	dest64->start = dest32.start;
	dest64->used = dest32.used;
	dest64->DR1 = dest32.DR1;
	dest64->DR4 = dest32.DR4;
	dest64->num_cliprects = dest32.num_cliprects;
	dest64->cliprects = (drm_clip_rect_t __user *)(uintptr_t)dest32.cliprects;
	
	return (0);
}

int 
copyin32_i915_irq_emit(void *dest, void *src)
{
	struct drm_i915_irq_emit *dest64 = dest;
	struct drm_i915_irq_emit32 dest32;

	DRM_COPYFROM_WITH_RETURN(&dest32, (void *)src, sizeof (dest32));
	
	dest64->irq_seq = (int __user *)(uintptr_t)dest32.irq_seq;

	return (0);
}

int 
copyin32_i915_getparam(void *dest, void *src)
{
	struct drm_i915_getparam *dest64 = dest;
	struct drm_i915_getparam32 dest32;

	DRM_COPYFROM_WITH_RETURN(&dest32, (void *)src, sizeof (dest32));

	dest64->param = dest32.param;
	dest64->value = (int __user *)(uintptr_t)dest32.value;

	return (0);
}

int 
copyin32_i915_cmdbuffer(void * dest, void * src)
{
	struct _drm_i915_cmdbuffer *dest64 = dest;
	struct drm_i915_cmdbuffer32 dest32;

	DRM_COPYFROM_WITH_RETURN(&dest32, (void *)src, sizeof (dest32));
	
	dest64->buf = (char __user *)(uintptr_t)dest32.buf;
	dest64->sz = dest32.sz;
	dest64->DR1 = dest32.DR1;
	dest64->DR4 = dest32.DR4;
	dest64->num_cliprects = dest32.num_cliprects;
	dest64->cliprects = (drm_clip_rect_t __user *)(uintptr_t)dest32.cliprects;
	
	return (0);
}

int 
copyin32_i915_mem_alloc(void *dest, void *src)
{
	struct drm_i915_mem_alloc *dest64 = dest;
	struct drm_i915_mem_alloc32 dest32;

	DRM_COPYFROM_WITH_RETURN(&dest32, (void *)src, sizeof (dest32));

	dest64->region = dest32.region;
	dest64->alignment = dest32.alignment;
	dest64->size = dest32.size;
	dest64->region_offset = (int *)(uintptr_t)dest32.region_offset;

	return (0);
}
