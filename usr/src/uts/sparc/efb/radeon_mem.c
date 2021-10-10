/*
 * Copyright (c) 2008, Oracle and/or its affiliates. All rights reserved.
 */

/* radeon_mem.c -- Simple GART/fb memory manager for radeon -*- linux-c -*- */
/*
 * Copyright (C) The Weather Channel, Inc.  2002.  All Rights Reserved.
 *
 * The Weather Channel (TM) funded Tungsten Graphics to develop the
 * initial release of the Radeon 8500 driver under the XFree86 license.
 * This notice must be preserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Keith Whitwell <keith@tungstengraphics.com>
 */

#include "drmP.h"
#include "drm.h"
#include "radeon_drm.h"
#include "radeon_drv.h"
#include "radeon_io32.h"

/*
 * Very simple allocator for GART memory, working on a static range
 * already mapped into each client's address space.
 */

static struct mem_block *
split_block(struct mem_block *p, int start, int size, drm_file_t *filp)
{
	/* Maybe cut off the start of an existing block */
	if (start > p->start) {
		struct mem_block *newblock =
		    drm_alloc(sizeof (*newblock), DRM_MEM_BUFS);
		if (!newblock)
			goto out;
		newblock->start = start;
		newblock->size = p->size - (start - p->start);
		newblock->filp = NULL;
		newblock->next = p->next;
		newblock->prev = p;
		p->next->prev = newblock;
		p->next = newblock;
		p->size -= newblock->size;
		p = newblock;
	}

	/* Maybe cut off the end of an existing block */
	if (size < p->size) {
		struct mem_block *newblock =
		    drm_alloc(sizeof (*newblock), DRM_MEM_BUFS);
		if (!newblock)
			goto out;
		newblock->start = start + size;
		newblock->size = p->size - size;
		newblock->filp = NULL;
		newblock->next = p->next;
		newblock->prev = p;
		p->next->prev = newblock;
		p->next = newblock;
		p->size = size;
	}

out:
	/* Our block is in the middle */
	p->filp = filp;
	return (p);
}

static struct mem_block *
alloc_block(struct mem_block *heap, int size, int align2, drm_file_t *filp)
{
	struct mem_block *p;
	int mask = (1 << align2) - 1;

	for (p = heap->next; p != heap; p = p->next) {
		int start = (p->start + mask) & ~mask;
		if (p->filp == 0 && start + size <= p->start + p->size)
			return (split_block(p, start, size, filp));
	}

	return (NULL);
}

static struct mem_block *
find_block(struct mem_block *heap, int start)
{
	struct mem_block *p;

	for (p = heap->next; p != heap; p = p->next)
		if (p->start == start)
			return (p);

	return (NULL);
}

static void
free_block(struct mem_block *p)
{
	p->filp = NULL;

	/*
	 * Assumes a single contiguous range.  Needs a special filp in
	 * 'heap' to stop it being subsumed.
	 */
	if (p->next->filp == 0) {
		struct mem_block *q = p->next;
		p->size += q->size;
		p->next = q->next;
		p->next->prev = p;
		drm_free(q, sizeof (*q), DRM_MEM_BUFS);
	}

	if (p->prev->filp == 0) {
		struct mem_block *q = p->prev;
		q->size += p->size;
		q->next = p->next;
		q->next->prev = q;
		drm_free(p, sizeof (*q), DRM_MEM_BUFS);
	}
}

/*
 * Initialize.  How to check for an uninitialized heap?
 */
static int
init_heap(struct mem_block **heap, int start, int size)
{
	struct mem_block *blocks = drm_alloc(sizeof (*blocks), DRM_MEM_BUFS);

	if (!blocks)
		return (ENOMEM);

	*heap = drm_alloc(sizeof (**heap), DRM_MEM_BUFS);
	if (!*heap) {
		drm_free(blocks, sizeof (*blocks), DRM_MEM_BUFS);
		return (ENOMEM);
	}

	blocks->start = start;
	blocks->size = size;
	blocks->filp = NULL;
	blocks->next = blocks->prev = *heap;

	(void) memset(*heap, 0, sizeof (**heap));
	(*heap)->filp = (drm_file_t *)-1;
	(*heap)->next = (*heap)->prev = blocks;
	return (0);
}

/*
 * Free all blocks associated with the releasing file.
 */
void
radeon_mem_release(drm_file_t *filp, struct mem_block *heap)
{
	struct mem_block *p;

	if (!heap || !heap->next)
		return;

	for (p = heap->next; p != heap; p = p->next) {
		if (p->filp == filp)
			p->filp = NULL;
	}

	/*
	 * Assumes a single contiguous range.  Needs a special filp in
	 * 'heap' to stop it being subsumed.
	 */
	for (p = heap->next; p != heap; p = p->next) {
		while (p->filp == 0 && p->next->filp == 0) {
			struct mem_block *q = p->next;
			p->size += q->size;
			p->next = q->next;
			p->next->prev = p;
			drm_free(q, sizeof (*q), DRM_MEM_DRIVER);
		}
	}
}

/*
 * Shutdown.
 */
void
radeon_mem_takedown(struct mem_block **heap)
{
	struct mem_block *p;

	if (!*heap)
		return;

	for (p = (*heap)->next; p != *heap; ) {
		struct mem_block *q = p;
		p = p->next;
		drm_free(q, sizeof (*q), DRM_MEM_DRIVER);
	}

	drm_free(*heap, sizeof (**heap), DRM_MEM_DRIVER);
	*heap = NULL;
}

/* IOCTL HANDLERS */

static struct mem_block **
get_heap(drm_radeon_private_t *dev_priv, int region)
{
	switch (region) {
	case RADEON_MEM_REGION_GART:
		return (&dev_priv->gart_heap);
	case RADEON_MEM_REGION_FB:
		return (&dev_priv->fb_heap);
	default:
		return (NULL);
	}
}

/*ARGSUSED*/
int
radeon_mem_alloc(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_radeon_mem_alloc_t alloc;
	struct mem_block *block, **heap;

	if (!dev_priv) {
		DRM_ERROR("%s called with no initialization\n", __FUNCTION__);
		return (EINVAL);
	}

#ifdef _MULTI_DATAMODEL
	if (ddi_model_convert_from(mode & FMODELS) == DDI_MODEL_ILP32) {
		drm_radeon_mem_alloc_32_t alloc32;

		DRM_COPYFROM_WITH_RETURN(&alloc32, (void *) data,
		    sizeof (alloc32));
		alloc.region = alloc32.region;
		alloc.alignment = alloc32.alignment;
		alloc.size = alloc32.size;
		alloc.region_offset = (void *)(uintptr_t)alloc32.region_offset;
	} else {
#endif
		DRM_COPYFROM_WITH_RETURN(&alloc, (void *) data, sizeof (alloc));
#ifdef _MULTI_DATAMODEL
	}
#endif

	heap = get_heap(dev_priv, alloc.region);
	if (!heap || !*heap)
		return (EFAULT);

	/*
	 * Make things easier on ourselves: all allocations at least
	 * 4k aligned.
	 */
	if (alloc.alignment < 12)
		alloc.alignment = 12;

	block = alloc_block(*heap, alloc.size, alloc.alignment, fpriv);

	if (!block)
		return (ENOMEM);

	if (DRM_COPY_TO_USER(alloc.region_offset, &block->start,
	    sizeof (int))) {
		DRM_ERROR("copy_to_user\n");
		return (EFAULT);
	}

	return (0);
}

/*ARGSUSED*/
int
radeon_mem_free(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_radeon_mem_free_t memfree;
	struct mem_block *block, **heap;

	if (!dev_priv) {
		DRM_ERROR("%s called with no initialization\n", __FUNCTION__);
		return (EINVAL);
	}

	DRM_COPYFROM_WITH_RETURN(&memfree, (void *) data, sizeof (memfree));

	heap = get_heap(dev_priv, memfree.region);
	if (!heap || !*heap)
		return (EFAULT);

	block = find_block(*heap, memfree.region_offset);
	if (!block)
		return (EFAULT);

	if (block->filp != fpriv)
		return (EPERM);

	free_block(block);
	return (0);
}

/*ARGSUSED*/
int
radeon_mem_init_heap(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_radeon_mem_init_heap_t initheap;
	struct mem_block **heap;

	if (!dev_priv) {
		DRM_ERROR("%s called with no initialization\n", __FUNCTION__);
		return (EINVAL);
	}

	DRM_COPYFROM_WITH_RETURN(&initheap, (void *) data, sizeof (initheap));

	heap = get_heap(dev_priv, initheap.region);
	if (!heap)
		return (EFAULT);

	if (*heap) {
		DRM_ERROR("heap already initialized?");
		return (EFAULT);
	}

	return (init_heap(heap, initheap.start, initheap.size));
}
