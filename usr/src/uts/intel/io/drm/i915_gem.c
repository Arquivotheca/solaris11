/* BEGIN CSTYLED */

/*
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copyright (c) 2009, Intel Corporation.
 * All Rights Reserved.
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *
 */

#include <sys/x86_archext.h>
#include <sys/vfs_opreg.h>
#include "drmP.h"
#include "drm.h"
#include "i915_drm.h"
#include "i915_drv.h"
#include "intel_drv.h"

#ifndef roundup
#define	roundup(x, y)   ((((x)+((y)-1))/(y))*(y))
#endif /* !roundup */

#define I915_GEM_GPU_DOMAINS	(~(I915_GEM_DOMAIN_CPU | I915_GEM_DOMAIN_GTT))

static timeout_id_t worktimer_id = NULL;

static void i915_gem_object_flush_gpu_write_domain(struct drm_gem_object *obj);
static void i915_gem_object_flush_gtt_write_domain(struct drm_gem_object *obj);
static void i915_gem_object_flush_cpu_write_domain(struct drm_gem_object *obj);
static int i915_gem_object_set_to_cpu_domain(struct drm_gem_object *obj,
					     int write);
static int i915_gem_object_set_cpu_read_domain_range(struct drm_gem_object *obj,
						     uint64_t offset,
						     uint64_t size);
static void i915_gem_object_set_to_full_cpu_read_domain(struct drm_gem_object *obj);
static int i915_gem_object_wait_rendering(struct drm_gem_object *obj);
static int i915_gem_object_bind_to_gtt(struct drm_gem_object *obj,
					   uint32_t alignment);
static void i915_gem_clear_fence_reg(struct drm_gem_object *obj);
static int i915_gem_evict_something(struct drm_device *dev, int min_size);
static int i915_gem_evict_from_inactive_list(struct drm_device *dev, uint32_t type);
static int i915_gem_phys_pwrite(struct drm_device *dev, struct drm_gem_object *obj,
				struct drm_i915_gem_pwrite *args,
				struct drm_file *file_priv);


int i915_gem_do_init(struct drm_device *dev, unsigned long start,
		     unsigned long end)
{
	drm_i915_private_t *dev_priv = dev->dev_private;

	if (start >= end ||
	    (start & (PAGE_SIZE - 1)) != 0 ||
	    (end & (PAGE_SIZE - 1)) != 0) {
		return -EINVAL;
	}

	/* prevent re-init gtt space */
	if (dev->gtt_total != 0) {
		DRM_ERROR("gtt space already init 0x%x", dev->gtt_total);
		return 0;
	}

	(void) drm_mm_init(&dev_priv->mm.gtt_space, start,
		    end - start);

	dev->gtt_total = (uint32_t) (end - start);
	return 0;
}

int
/* LINTED */
i915_gem_init_ioctl(DRM_IOCTL_ARGS)
{
	struct drm_i915_gem_init *args = data;
	int ret;

	if (!drm_core_check_feature(dev, DRIVER_GEM))
		return -ENODEV;

	mutex_lock(&dev->struct_mutex);
	ret = i915_gem_do_init(dev, args->gtt_start, args->gtt_end);
	mutex_unlock(&dev->struct_mutex);

	return ret;
}

int
/* LINTED */
i915_gem_get_aperture_ioctl(DRM_IOCTL_ARGS)
{
	struct drm_i915_gem_get_aperture *args = data;

	if (!(dev->driver->driver_features & DRIVER_GEM))
		return -ENODEV;

	args->aper_size = dev->gtt_total;
	args->aper_available_size = (args->aper_size -
				     atomic_read(&dev->pin_memory));

	return 0;
}


/**
 * Creates a new mm object and returns a handle to it.
 */
int
/* LINTED */
i915_gem_create_ioctl(DRM_IOCTL_ARGS)
{
	struct drm_i915_gem_create *args = data;
	struct drm_gem_object *obj;
	int ret;
	u32 handle;

	args->size = roundup(args->size, PAGE_SIZE);

	/* Allocate the new object */
	obj = drm_gem_object_alloc(dev, args->size);
	if (obj == NULL)
		return -ENOMEM;

	ret = drm_gem_handle_create(file_priv, obj, &handle);
	mutex_lock(&dev->struct_mutex);
	drm_gem_object_handle_unreference(obj);
	mutex_unlock(&dev->struct_mutex);

	if (ret)
		return ret;

	args->handle = handle;

	return 0;
}

/**
 * Reads data from the object referenced by handle.
 *
 * On error, the contents of *data are undefined.
 */
int
/* LINTED */
i915_gem_pread_ioctl(DRM_IOCTL_ARGS)
{
	struct drm_i915_gem_pread *args = data;
	struct drm_gem_object *obj;
	int ret;

	obj = drm_gem_object_lookup(dev, file_priv, args->handle);
	if (obj == NULL)
		return -EBADF;

	/* Bounds check source.
	 *
	 * XXX: This could use review for overflow issues...
	 */
	if (args->offset > obj->size || args->size > obj->size ||
	    args->offset + args->size > obj->size) {
		drm_gem_object_unreference(obj);
		return -EINVAL;
	}

	spin_lock(&dev->struct_mutex);

	ret = i915_gem_object_set_cpu_read_domain_range(obj, args->offset, args->size);
	if (ret != 0) {
		drm_gem_object_unreference(obj);
		spin_unlock(&dev->struct_mutex);
		DRM_ERROR("pread failed to read domain range ret %d!!!", ret);
		return -EFAULT;
	}

	ret = DRM_COPY_TO_USER((uint32_t *)(uintptr_t)args->data_ptr,
	    obj->kaddr + args->offset, args->size);

	drm_gem_object_unreference(obj);
	spin_unlock(&dev->struct_mutex);

	return ret;
}

static int
i915_gem_gtt_pwrite(struct drm_device *dev, struct drm_gem_object *obj,
		    struct drm_i915_gem_pwrite *args,
		    /* LINTED */
		    struct drm_file *file_priv)
{
	uint32_t *user_data = (uint32_t *)(uintptr_t)args->data_ptr;
	int ret;

	spin_lock(&dev->struct_mutex);
	ret = i915_gem_object_pin(obj, 0);
	if (ret) {
		DRM_ERROR("i915_gem_object_pin failed, ret = %d", ret);
		spin_unlock(&dev->struct_mutex);
		return ret;
	}

	ret = i915_gem_object_set_to_gtt_domain(obj, 1);
	if (ret) {
		DRM_ERROR("i915_gem_object_set_to_gtt_domain failed, ret = %d", ret);
		goto err;
	}

	ret = DRM_COPY_FROM_USER(obj->kaddr + args->offset, user_data, args->size);
	if (ret) {
		DRM_ERROR("copy_from_user failed, ret = %d", ret);
		goto err;
	}

err:
	i915_gem_object_unpin(obj);
	spin_unlock(&dev->struct_mutex);

	return ret;
}

int
/* LINTED */
i915_gem_shmem_pwrite(struct drm_device *dev, struct drm_gem_object *obj,
			  /* LINTED */
		      struct drm_i915_gem_pwrite *args,
		      /* LINTED */
		      struct drm_file *file_priv)
{
	DRM_ERROR("not support");

	return -1;
}

/**
 * Writes data to the object referenced by handle.
 *
 * On error, the contents of the buffer that were to be modified are undefined.
 */
int
/* LINTED */
i915_gem_pwrite_ioctl(DRM_IOCTL_ARGS)
{
	struct drm_i915_gem_pwrite *args = data;
	struct drm_gem_object *obj;
	struct drm_i915_gem_object *obj_priv;
	int ret = 0;

	obj = drm_gem_object_lookup(dev, file_priv, args->handle);
	if (obj == NULL)
		return -EBADF;
	obj_priv = obj->driver_private;

	/* Bounds check destination.
	 *
	 * XXX: This could use review for overflow issues...
	 */
	if (args->offset > obj->size || args->size > obj->size ||
	    args->offset + args->size > obj->size) {
		drm_gem_object_unreference(obj);
		return -EINVAL;
	}

	/* We can only do the GTT pwrite on untiled buffers, as otherwise
	 * it would end up going through the fenced access, and we'll get
	 * different detiling behavior between reading and writing.
	 * pread/pwrite currently are reading and writing from the CPU
	 * perspective, requiring manual detiling by the client.
	 */
	if (obj_priv->phys_obj)
		ret = i915_gem_phys_pwrite(dev, obj, args, file_priv);
	else if (obj_priv->tiling_mode == I915_TILING_NONE &&
	    dev->gtt_total != 0)
		ret = i915_gem_gtt_pwrite(dev, obj, args, file_priv);
	else
		ret = i915_gem_shmem_pwrite(dev, obj, args, file_priv);

	if (ret)
		DRM_INFO("pwrite failed %d\n", ret);

	drm_gem_object_unreference(obj);

	return ret;
}

/**
 * Called when user space prepares to use an object with the CPU, either
 * through the mmap ioctl's mapping or a GTT mapping.
 */
int
/* LINTED */
i915_gem_set_domain_ioctl(DRM_IOCTL_ARGS)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_gem_set_domain *args = data;
	struct drm_gem_object *obj;
	struct drm_i915_gem_object *obj_priv;
	uint32_t read_domains = args->read_domains;
	uint32_t write_domain = args->write_domain;
	int ret;

	if (!(dev->driver->driver_features & DRIVER_GEM))
		return -ENODEV;

	/* Only handle setting domains to types used by the CPU. */
	if (write_domain & I915_GEM_GPU_DOMAINS)
		return -EINVAL;

	if (read_domains & I915_GEM_GPU_DOMAINS)
		return -EINVAL;

	/* Having something in the write domain implies it's in the read
	 * domain, and only that read domain.  Enforce that in the request.
	 */
	if (write_domain != 0 && read_domains != write_domain)
		return -EINVAL;

	obj = drm_gem_object_lookup(dev, file_priv, args->handle);
	if (obj == NULL)
		return -EBADF;
	obj_priv = obj->driver_private;

	mutex_lock(&dev->struct_mutex);

	intel_mark_busy(dev, obj);

	if (read_domains & I915_GEM_DOMAIN_GTT) {
		ret = i915_gem_object_set_to_gtt_domain(obj, write_domain != 0);

		/* Update the LRU on the fence for the CPU access that's
		 * about to occur.
		 */
		if (obj_priv->fence_reg != I915_FENCE_REG_NONE) {
			list_move_tail(&obj_priv->fence_list,
				       &dev_priv->mm.fence_list, (caddr_t)obj_priv);
		}

		/* Silently promote "you're not bound, there was nothing to do"
		 * to success, since the client was just asking us to
		 * make sure everything was done.
		 */
		if (ret == -EINVAL)
			ret = 0;
	} else {
		ret = i915_gem_object_set_to_cpu_domain(obj, write_domain != 0);
	}

	drm_gem_object_unreference(obj);
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

/**
 * Called when user space has done writes to this buffer
 */
int
/* LINTED */
i915_gem_sw_finish_ioctl(DRM_IOCTL_ARGS)
{
	struct drm_i915_gem_sw_finish *args = data;
	struct drm_gem_object *obj;
	struct drm_i915_gem_object *obj_priv;
	int ret = 0;

	if (!(dev->driver->driver_features & DRIVER_GEM))
		return -ENODEV;

	mutex_lock(&dev->struct_mutex);
	obj = drm_gem_object_lookup(dev, file_priv, args->handle);
	if (obj == NULL) {
		mutex_unlock(&dev->struct_mutex);
		return -EBADF;
	}

	DRM_DEBUG("%s: sw_finish %d (%p name %d size 0x%lx)\n",
		 __func__, args->handle, (void *)obj, obj->name, obj->size);

	obj_priv = obj->driver_private;

	/* Pinned buffers may be scanout, so flush the cache */
	if (obj_priv->pin_count)
		i915_gem_object_flush_cpu_write_domain(obj);

	drm_gem_object_unreference(obj);
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

/**
 * Maps the contents of an object, returning the address it is mapped
 * into.
 *
 * While the mapping holds a reference on the contents of the object, it doesn't
 * imply a ref on the object itself.
 */
int
/* LINTED */
i915_gem_mmap_ioctl(DRM_IOCTL_ARGS)
{
	struct drm_i915_gem_mmap *args = data;
	struct drm_gem_object *obj;
	caddr_t vvaddr = NULL;
	int ret;

	if (!(dev->driver->driver_features & DRIVER_GEM))
		return -ENODEV;

	obj = drm_gem_object_lookup(dev, file_priv, args->handle);
	if (obj == NULL)
		return -EBADF;

	ret = ddi_devmap_segmap(dev_id, (off_t)obj->maplist.user_token,
	    ttoproc(curthread)->p_as, &vvaddr, obj->maplist.map->size,
	    PROT_ALL, PROT_ALL, MAP_SHARED, credp);
	if (ret)
		return ret;

	mutex_lock(&dev->struct_mutex);
	drm_gem_object_unreference(obj);
	mutex_unlock(&dev->struct_mutex);

	args->addr_ptr = (uint64_t)(uintptr_t)vvaddr;

	return 0;
}

/**
 * i915_gem_create_mmap_offset - create a fake mmap offset for an object
 * @obj: obj in question
 *
 * GEM memory mapping works by handing back to userspace a fake mmap offset
 * it can use in a subsequent mmap(2) call.  The DRM core code then looks
 * up the object based on the offset and sets up the various memory mapping
 * structures.
 *
 * This routine allocates and attaches a fake offset for @obj.
 */
static int
i915_gem_create_mmap_offset(struct drm_gem_object *obj)
{
	struct drm_i915_gem_object *obj_priv = obj->driver_private;

	/* user_token is the fake offset 
	 * which create in drm_map_handle at alloc time 
	 */
	obj_priv->mmap_offset = obj->maplist.user_token;

	return 0;
}

/**
 * i915_gem_release_mmap - remove physical page mappings
 * @obj: obj in question
 *
 * Preserve the reservation of the mmaping with the DRM core code, but
 * relinquish ownership of the pages back to the system.
 *
 * It is vital that we remove the page mapping if we have mapped a tiled
 * object through the GTT and then lose the fence register due to
 * resource pressure. Similarly if the object has been moved out of the
 * aperture, than pages mapped into userspace must be revoked. Removing the
 * mapping will then trigger a page fault on the next user access, allowing
 * fixup by i915_gem_fault().
 */
void
i915_gem_release_mmap(struct drm_gem_object *obj)
{
	_NOTE(ARGUNUSED(obj))
}

static void
i915_gem_free_mmap_offset(struct drm_gem_object *obj)
{
	struct drm_i915_gem_object *obj_priv = obj->driver_private;
	obj_priv->mmap_offset = 0;
}

/**
 * i915_gem_get_gtt_alignment - return required GTT alignment for an object
 * @obj: object to check
 *
 * Return the required GTT alignment for an object, taking into account
 * potential fence register mapping if needed.
 */
static uint32_t
i915_gem_get_gtt_alignment(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;
	struct drm_i915_gem_object *obj_priv = obj->driver_private;
	int start, i;

	/*
	 * Minimum alignment is 4k (GTT page size), but might be greater
	 * if a fence register is needed for the object.
	 */
	if (IS_I965G(dev) || obj_priv->tiling_mode == I915_TILING_NONE)
		return 4096;

	/*
	 * Previous chips need to be aligned to the size of the smallest
	 * fence register that can contain the object.
	 */
	if (IS_I9XX(dev))
		start = 1024*1024;
	else
		start = 512*1024;

	for (i = start; i < obj->size; i <<= 1)
		;

	return i;
}

/**
 * i915_gem_mmap_gtt_ioctl - prepare an object for GTT mmap'ing
 * @dev: DRM device
 * @data: GTT mapping ioctl data
 * @file_priv: GEM object info
 *
 * Simply returns the fake offset to userspace so it can mmap it.
 * The mmap call will end up in drm_gem_mmap(), which will set things
 * up so we can get faults in the handler above.
 *
 * The fault handler will take care of binding the object into the GTT
 * (since it may have been evicted to make room for something), allocating
 * a fence register, and mapping the appropriate aperture address into
 * userspace.
 */
int
/* LINTED */
i915_gem_mmap_gtt_ioctl(DRM_IOCTL_ARGS)
{
	struct drm_i915_gem_mmap_gtt *args = data;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_gem_object *obj;
	struct drm_i915_gem_object *obj_priv;
	int ret;

	if (!(dev->driver->driver_features & DRIVER_GEM))
		return -ENODEV;

	obj = drm_gem_object_lookup(dev, file_priv, args->handle);
	if (obj == NULL)
		return -EBADF;

	mutex_lock(&dev->struct_mutex);

	obj_priv = obj->driver_private;

	if (!obj_priv->mmap_offset) {
		ret = i915_gem_create_mmap_offset(obj);
		if (ret) {
			drm_gem_object_unreference(obj);
			mutex_unlock(&dev->struct_mutex);
			return ret;
		}
	}

	args->offset = obj_priv->mmap_offset;

	/*
	 * Pull it into the GTT so that we have a page list (makes the
	 * initial fault faster and any subsequent flushing possible).
	 */
	if (obj_priv->agp_mem) {
		ret = i915_gem_object_bind_to_gtt(obj, 0);
		if (ret) {
			drm_gem_object_unreference(obj);
			mutex_unlock(&dev->struct_mutex);
			return ret;
		}
		list_add_tail(&obj_priv->list, &dev_priv->mm.inactive_list, (caddr_t)obj_priv);
	}
	/* Need a new fence register? */
	if (obj_priv->tiling_mode != I915_TILING_NONE) {
		ret = i915_gem_object_get_fence_reg(obj);
		if (ret != 0) {
				DRM_ERROR("Failure to install fence: %d\n",
					  ret);
			return ret;
		}
	}

	drm_gem_object_unreference(obj);
	mutex_unlock(&dev->struct_mutex);

	DRM_DEBUG("obj_priv->mmap_offset = 0x%lx", obj_priv->mmap_offset);

	return 0;
}

void
i915_gem_object_put_pages(struct drm_gem_object *obj)
{
	struct drm_i915_gem_object *obj_priv = obj->driver_private;

	if (--obj_priv->pages_refcount != 0)
		return;

	kmem_free(obj_priv->page_list,
	    btop(obj->size) * sizeof(caddr_t));
	obj_priv->page_list = NULL;
}

static void
i915_gem_object_move_to_active(struct drm_gem_object *obj, uint32_t seqno)
{
	struct drm_device *dev = obj->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj_priv = obj->driver_private;

	/* Add a reference if we're newly entering the active list. */
	if (!obj_priv->active) {
		drm_gem_object_reference(obj);
		obj_priv->active = 1;
	}
	/* Move from whatever list we were on to the tail of execution. */
	spin_lock(&dev_priv->mm.active_list_lock);
	list_move_tail(&obj_priv->list,
		       &dev_priv->mm.active_list, (caddr_t)obj_priv);
	spin_unlock(&dev_priv->mm.active_list_lock);
	obj_priv->last_rendering_seqno = seqno;
}

static void
i915_gem_object_move_to_flushing(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj_priv = obj->driver_private;

	list_move_tail(&obj_priv->list, &dev_priv->mm.flushing_list, (caddr_t)obj_priv);
	obj_priv->last_rendering_seqno = 0;
}

static void
i915_gem_object_move_to_inactive(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj_priv = obj->driver_private;

	if (obj_priv->pin_count != 0)
		list_del_init(&obj_priv->list);
	else
		list_move_tail(&obj_priv->list, &dev_priv->mm.inactive_list, (caddr_t)obj_priv);

	obj_priv->last_rendering_seqno = 0;
	if (obj_priv->active) {
		obj_priv->active = 0;
		drm_gem_object_unreference(obj);
	}
}

static void
i915_gem_process_flushing_list(struct drm_device *dev,
			       uint32_t flush_domains, uint32_t seqno)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj_priv, *next;

	list_for_each_entry_safe(obj_priv, next, struct drm_i915_gem_object,
				&dev_priv->mm.flushing_list,
				list) {
		struct drm_gem_object *obj = obj_priv->obj;

		if ((obj->write_domain & flush_domains) ==
		    obj->write_domain) {
		    /* LINTED */
			uint32_t old_write_domain = obj->write_domain;

			obj->write_domain = 0;
			i915_gem_object_move_to_active(obj, seqno);

			/* update the fence lru list */
			if (obj_priv->fence_reg != I915_FENCE_REG_NONE)
				list_move_tail(&obj_priv->fence_list,
						&dev_priv->mm.fence_list, (caddr_t)obj_priv);

		}
	}
}

#define PIPE_CONTROL_FLUSH(addr)					\
	OUT_RING(GFX_OP_PIPE_CONTROL | PIPE_CONTROL_QW_WRITE |		\
		 PIPE_CONTROL_DEPTH_STALL | 2);				\
	OUT_RING(addr | PIPE_CONTROL_GLOBAL_GTT);			\
	OUT_RING(0);							\
	OUT_RING(0);							\

/**
 * Creates a new sequence number, emitting a write of it to the status page
 * plus an interrupt, which will trigger i915_user_interrupt_handler.
 *
 * Must be called with struct_lock held.
 *
 * Returned sequence numbers are nonzero on success.
 */
uint32_t
i915_add_request(struct drm_device *dev, struct drm_file *file_priv,
		 uint32_t flush_domains)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	/* LINTED */
	struct drm_i915_file_private *i915_file_priv = NULL;
	struct drm_i915_gem_request *request;
	uint32_t seqno;
	int was_empty;
	RING_LOCALS;

	if (file_priv != NULL)
		i915_file_priv = file_priv->driver_priv;

	request = kzalloc(sizeof(*request), GFP_KERNEL);
	if (request == NULL)
		return 0;

	/* Grab the seqno we're going to make this request be, and bump the
	 * next (skipping 0 so it can be the reserved no-seqno value).
	 */
	seqno = dev_priv->mm.next_gem_seqno;
	dev_priv->mm.next_gem_seqno++;
	if (dev_priv->mm.next_gem_seqno == 0)
		dev_priv->mm.next_gem_seqno++;

	if (IS_GEN6(dev)) {
		BEGIN_LP_RING(6);
		OUT_RING(GFX_OP_PIPE_CONTROL | 3);
		OUT_RING(PIPE_CONTROL_QW_WRITE |
			 PIPE_CONTROL_WC_FLUSH | PIPE_CONTROL_IS_FLUSH |
			 PIPE_CONTROL_NOTIFY);
		OUT_RING(dev_priv->seqno_gfx_addr | PIPE_CONTROL_GLOBAL_GTT);
		OUT_RING(seqno);
		OUT_RING(0);
		OUT_RING(0);
		ADVANCE_LP_RING();
	} else if (HAS_PIPE_CONTROL(dev)) {
		u32 scratch_addr = dev_priv->seqno_gfx_addr + 128;

		/*
		 * Workaround qword write incoherence by flushing the
		 * PIPE_NOTIFY buffers out to memory before requesting
		 * an interrupt.
		 */
		BEGIN_LP_RING(32);
		OUT_RING(GFX_OP_PIPE_CONTROL | PIPE_CONTROL_QW_WRITE |
			 PIPE_CONTROL_WC_FLUSH | PIPE_CONTROL_TC_FLUSH);
		OUT_RING(dev_priv->seqno_gfx_addr | PIPE_CONTROL_GLOBAL_GTT);
		OUT_RING(seqno);
		OUT_RING(0);
		PIPE_CONTROL_FLUSH(scratch_addr);
		scratch_addr += 128; /* write to separate cachelines */
		PIPE_CONTROL_FLUSH(scratch_addr);
		scratch_addr += 128;
		PIPE_CONTROL_FLUSH(scratch_addr);
		scratch_addr += 128;
		PIPE_CONTROL_FLUSH(scratch_addr);
		scratch_addr += 128;
		PIPE_CONTROL_FLUSH(scratch_addr);
		scratch_addr += 128;
		PIPE_CONTROL_FLUSH(scratch_addr);
		OUT_RING(GFX_OP_PIPE_CONTROL | PIPE_CONTROL_QW_WRITE |
			 PIPE_CONTROL_WC_FLUSH | PIPE_CONTROL_TC_FLUSH |
			 PIPE_CONTROL_NOTIFY);
		OUT_RING(dev_priv->seqno_gfx_addr | PIPE_CONTROL_GLOBAL_GTT);
		OUT_RING(seqno);
		OUT_RING(0);
		ADVANCE_LP_RING();
	} else {
		BEGIN_LP_RING(4);
		OUT_RING(MI_STORE_DWORD_INDEX);
		OUT_RING(I915_GEM_HWS_INDEX << MI_STORE_DWORD_INDEX_SHIFT);
		OUT_RING(seqno);
		OUT_RING(MI_USER_INTERRUPT);
		ADVANCE_LP_RING();


		if (IS_I965GM(dev)) {
			(void) READ_GEM_HWS(dev_priv);
			BEGIN_LP_RING(2);
			OUT_RING(0);
			OUT_RING(0);
			ADVANCE_LP_RING();
			(void) READ_GEM_HWS(dev_priv);
		}

	}

	DRM_DEBUG("%d\n", seqno);

	request->seqno = seqno;
	request->emitted_jiffies = jiffies;
	was_empty = list_empty(&dev_priv->mm.request_list);
	list_add_tail(&request->list, &dev_priv->mm.request_list, (caddr_t)request);

	/* Associate any objects on the flushing list matching the write
	 * domain we're flushing with our flush.
	 */
	if (flush_domains != 0) 
		i915_gem_process_flushing_list(dev, flush_domains, seqno);

	if (!dev_priv->mm.suspended) {
		mod_timer(&dev_priv->hangcheck_timer, jiffies + DRM_I915_HANGCHECK_PERIOD);
		if (was_empty) {
			/* change to delay HZ and then run work (not insert to workqueue of Linux) */ 
			worktimer_id = timeout(i915_gem_retire_work_handler, (void *) dev, DRM_HZ);
			DRM_DEBUG("i915_gem: schedule_delayed_work");
		}
	}
	return seqno;
}

/**
 * Command execution barrier
 *
 * Ensures that all commands in the ring are finished
 * before signalling the CPU
 */
static uint32_t
i915_retire_commands(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	uint32_t cmd = MI_FLUSH | MI_NO_WRITE_FLUSH;
	uint32_t flush_domains = 0;
	RING_LOCALS;

	/* The sampler always gets flushed on i965 (sigh) */
	if (IS_I965G(dev))
		flush_domains |= I915_GEM_DOMAIN_SAMPLER;
	BEGIN_LP_RING(2);
	OUT_RING(cmd);
	OUT_RING(0); /* noop */
	ADVANCE_LP_RING();
	return flush_domains;
}

/**
 * Moves buffers associated only with the given active seqno from the active
 * to inactive list, potentially freeing them.
 */
static void
i915_gem_retire_request(struct drm_device *dev,
			struct drm_i915_gem_request *request)
{
	drm_i915_private_t *dev_priv = dev->dev_private;

	/* Move any buffers on the active list that are no longer referenced
	 * by the ringbuffer to the flushing/inactive lists as appropriate.
	 */
	spin_lock(&dev_priv->mm.active_list_lock);
	while (!list_empty(&dev_priv->mm.active_list)) {
		struct drm_gem_object *obj;
		struct drm_i915_gem_object *obj_priv;

		obj_priv = list_first_entry(&dev_priv->mm.active_list,
					    struct drm_i915_gem_object,
					    list);

		obj = obj_priv->obj;

		/* If the seqno being retired doesn't match the oldest in the
		 * list, then the oldest in the list must still be newer than
		 * this seqno.
		 */
		if (obj_priv->last_rendering_seqno != request->seqno)
			goto out;

		DRM_DEBUG("%s: retire %d moves to inactive list %p\n",
			 __func__, request->seqno, (void *)obj);

		if (obj->write_domain != 0)
			i915_gem_object_move_to_flushing(obj);
		else {
			/* Take a reference on the object so it won't be
			 * freed while the spinlock is held.  The list
			 * protection for this spinlock is safe when breaking
			 * the lock like this since the next thing we do
			 * is just get the head of the list again.
			 */
			drm_gem_object_reference(obj);
			i915_gem_object_move_to_inactive(obj);
			spin_unlock(&dev_priv->mm.active_list_lock);
			drm_gem_object_unreference(obj);
			spin_lock(&dev_priv->mm.active_list_lock);
		}
	}
out:
	spin_unlock(&dev_priv->mm.active_list_lock);
}

/**
 * Returns true if seq1 is later than seq2.
 */
bool
i915_seqno_passed(uint32_t seq1, uint32_t seq2)
{
	return (int32_t)(seq1 - seq2) >= 0;
}

uint32_t
i915_get_gem_seqno(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;

	if (HAS_PIPE_CONTROL(dev))
		return ((volatile u32 *)(dev_priv->seqno_page))[0];
	else
		return READ_HWSP(dev_priv, I915_GEM_HWS_INDEX);
}

/**
 * This function clears the request list as sequence numbers are passed.
 */
void
i915_gem_retire_requests(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	uint32_t seqno;

	if (!dev_priv->hw_status_page || list_empty(&dev_priv->mm.request_list))
		return;

	seqno = i915_get_gem_seqno(dev);

	while (!list_empty(&dev_priv->mm.request_list)) {
		struct drm_i915_gem_request *request;
		uint32_t retiring_seqno;

		request = list_first_entry(&dev_priv->mm.request_list,
					   struct drm_i915_gem_request,
					   list);
		retiring_seqno = request->seqno;

		if (i915_seqno_passed(seqno, retiring_seqno) ||
		    atomic_read(&dev_priv->mm.wedged)) {
			i915_gem_retire_request(dev, request);

			list_del(&request->list);
			kfree(request, sizeof(*request));
		} else
			break;
	}
}

void
i915_gem_retire_work_handler(void *device)
{
	struct drm_device *dev = (struct drm_device *)device;	
	drm_i915_private_t *dev_priv = dev->dev_private;

	mutex_lock(&dev->struct_mutex);

	/* Return if gem idle */
	if (worktimer_id == NULL) {
		spin_unlock(&dev->struct_mutex);
		return;
	}

	i915_gem_retire_requests(dev);
	if (!dev_priv->mm.suspended && !list_empty(&dev_priv->mm.request_list))
	{	
		DRM_DEBUG("i915_gem: schedule_delayed_work");
		worktimer_id = timeout(i915_gem_retire_work_handler, (void *) dev, DRM_HZ);
	}
	mutex_unlock(&dev->struct_mutex);
}

static int
/* LINTED */
i915_do_wait_request(struct drm_device *dev, uint32_t seqno, int interruptible)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	u32 ier;
	int ret = 0;

	BUG_ON(seqno == 0);

	if (atomic_read(&dev_priv->mm.wedged))
		return -EIO;

	if (!i915_seqno_passed(i915_get_gem_seqno(dev), seqno)) {
		if (HAS_PCH_SPLIT(dev))
			ier = I915_READ(DEIER) | I915_READ(GTIER);
		else
			ier = I915_READ(IER);
		if (!ier) {
			DRM_ERROR("something (likely vbetool) disabled "
				  "interrupts, re-enabling\n");
			(void) i915_driver_irq_preinstall(dev);
			i915_driver_irq_postinstall(dev);
		}
		dev_priv->mm.waiting_gem_seqno = seqno;
		i915_user_irq_get(dev);
		DRM_WAIT(ret, &dev_priv->irq_queue,
					       i915_seqno_passed(i915_get_gem_seqno(dev),
								 seqno) ||
					       atomic_read(&dev_priv->mm.wedged));
		i915_user_irq_put(dev);
		dev_priv->mm.waiting_gem_seqno = 0;
	}
	if (atomic_read(&dev_priv->mm.wedged))
		ret = -EIO;

	if (ret)
		DRM_ERROR("%s returns %d (awaiting %d at %d)\n",
			  __func__, ret, seqno, i915_get_gem_seqno(dev));

	/* Directly dispatch request retiring.  While we have the work queue
	 * to handle this, the waiter on a request often wants an associated
	 * buffer to have made it to the inactive list, and we would need
	 * a separate wait queue to handle that.
	 */
	if (ret == 0)
		i915_gem_retire_requests(dev);

	return ret;
}

/**
 * Waits for a sequence number to be signaled, and cleans up the
 * request and object lists appropriately for that event.
 */
static int
i915_wait_request(struct drm_device *dev, uint32_t seqno)
{
	return i915_do_wait_request(dev, seqno, 1);
}

static void
i915_gem_flush(struct drm_device *dev,
	       uint32_t invalidate_domains,
	       uint32_t flush_domains)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	uint32_t cmd;
	RING_LOCALS;

	DRM_DEBUG("%s: invalidate %08x flush %08x\n", __func__,
				     invalidate_domains, flush_domains);

	if (flush_domains & I915_GEM_DOMAIN_CPU)
		drm_agp_chipset_flush(dev);

	if ((invalidate_domains | flush_domains) & I915_GEM_GPU_DOMAINS) {
		/*
		 * read/write caches:
		 *
		 * I915_GEM_DOMAIN_RENDER is always invalidated, but is
		 * only flushed if MI_NO_WRITE_FLUSH is unset.  On 965, it is
		 * also flushed at 2d versus 3d pipeline switches.
		 *
		 * read-only caches:
		 *
		 * I915_GEM_DOMAIN_SAMPLER is flushed on pre-965 if
		 * MI_READ_FLUSH is set, and is always flushed on 965.
		 *
		 * I915_GEM_DOMAIN_COMMAND may not exist?
		 *
		 * I915_GEM_DOMAIN_INSTRUCTION, which exists on 965, is
		 * invalidated when MI_EXE_FLUSH is set.
		 *
		 * I915_GEM_DOMAIN_VERTEX, which exists on 965, is
		 * invalidated with every MI_FLUSH.
		 *
		 * TLBs:
		 *
		 * On 965, TLBs associated with I915_GEM_DOMAIN_COMMAND
		 * and I915_GEM_DOMAIN_CPU in are invalidated at PTE write and
		 * I915_GEM_DOMAIN_RENDER and I915_GEM_DOMAIN_SAMPLER
		 * are flushed at any MI_FLUSH.
		 */

		cmd = MI_FLUSH | MI_NO_WRITE_FLUSH;
		if ((invalidate_domains|flush_domains) &
		    I915_GEM_DOMAIN_RENDER)
			cmd &= ~MI_NO_WRITE_FLUSH;
		if (!IS_I965G(dev)) {
			/*
			 * On the 965, the sampler cache always gets flushed
			 * and this bit is reserved.
			 */
			if (invalidate_domains & I915_GEM_DOMAIN_SAMPLER)
				cmd |= MI_READ_FLUSH;
		}
		if (invalidate_domains & I915_GEM_DOMAIN_INSTRUCTION)
			cmd |= MI_EXE_FLUSH;

		DRM_DEBUG("%s: queue flush %08x to ring\n", __func__, cmd);

		BEGIN_LP_RING(2);
		OUT_RING(cmd);
		OUT_RING(MI_NOOP);
		ADVANCE_LP_RING();
	}
}

/**
 * Ensures that all rendering to the object has completed and the object is
 * safe to unbind from the GTT or access from the CPU.
 */
static int
i915_gem_object_wait_rendering(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;
	struct drm_i915_gem_object *obj_priv = to_intel_bo(obj);
	int ret;

	/* This function only exists to support waiting for existing rendering,
	 * not for emitting required flushes.
	 */
	if ((obj->write_domain & I915_GEM_GPU_DOMAINS) != 0) {
		DRM_ERROR("write domain should not be GPU DOMAIN %d", obj_priv->active);
		return 0;
	}

	/* If there is rendering queued on the buffer being evicted, wait for
	 * it.
	 */
	if (obj_priv->active) {
		DRM_DEBUG("%s: object %d %p wait for seqno %08x\n",
			  __func__, obj->name, (void *)obj, obj_priv->last_rendering_seqno);

		if (!obj_priv->last_rendering_seqno) {
			DRM_DEBUG("last rendering maybe finished");
			return 0;
		}

		ret = i915_wait_request(dev, obj_priv->last_rendering_seqno);
		if (ret != 0) {
			DRM_ERROR("%s: i915_wait_request now %d\n", __func__, i915_get_gem_seqno(dev));
			return ret;
		}
	}

	return 0;
}

/**
 * Unbinds an object from the GTT aperture.
 */
int
i915_gem_object_unbind(struct drm_gem_object *obj, uint32_t type)
{
	struct drm_device *dev = obj->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj_priv = to_intel_bo(obj);
	int ret = 0;

	if (obj_priv->gtt_space == NULL)
		return 0;

	if (obj_priv->pin_count != 0) {
		DRM_ERROR("Attempting to unbind pinned buffer\n");
		return -EINVAL;
	}

	/* blow away mappings if mapped through GTT */
	i915_gem_release_mmap(obj);

	/* Move the object to the CPU domain to ensure that
	 * any possible CPU writes while it's not in the GTT
	 * are flushed when we go to remap it. This will
	 * also ensure that all pending GPU writes are finished
	 * before we unbind.
	 */
	ret = i915_gem_object_set_to_cpu_domain(obj, 1);
	if (ret) {
			DRM_ERROR("set_domain failed: %d\n", ret);
		return ret;
	}

	BUG_ON(obj_priv->active);

	/* release the fence reg _after_ flushing */
	if (obj_priv->fence_reg != I915_FENCE_REG_NONE)
		i915_gem_clear_fence_reg(obj);

	if (!obj_priv->agp_mem) {
		(void) drm_agp_unbind_pages(dev, obj->size / PAGE_SIZE, obj_priv->gtt_offset, type);
		obj_priv->agp_mem = -1;
	}

	ASSERT(!obj_priv->active);

	i915_gem_object_put_pages(obj);

	if (obj_priv->gtt_space) {
		atomic_dec(&dev->gtt_count);
		atomic_sub(obj->size, &dev->gtt_memory);

		drm_mm_put_block(obj_priv->gtt_space);
		obj_priv->gtt_space = NULL;
	}

	spin_lock(&dev_priv->mm.active_list_lock);
	/* Remove ourselves from the LRU list if present. */
	if (!list_empty(&obj_priv->list))
		list_del_init(&obj_priv->list);
	spin_unlock(&dev_priv->mm.active_list_lock);

	return 0;
}

static struct drm_gem_object *
i915_gem_find_inactive_object(struct drm_device *dev, int min_size)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj_priv;
	struct drm_gem_object *best = NULL;
	struct drm_gem_object *first = NULL;

	/* Try to find the smallest clean object */
	list_for_each_entry(obj_priv, struct drm_i915_gem_object, &dev_priv->mm.inactive_list, list) {
		struct drm_gem_object *obj = obj_priv->obj;
		if (obj->size >= min_size) {
			if (!obj_priv->dirty &&
			    (!best || obj->size < best->size)) {
				best = obj;
				if (best->size == min_size)
					return best;
			}
			if (!first)
			    first = obj;
		}
	}

	return best ? best : first;
}

static int
i915_gem_evict_everything(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	uint32_t seqno;
	int ret;
	int lists_empty;

	spin_lock(&dev_priv->mm.active_list_lock);
	lists_empty = (list_empty(&dev_priv->mm.inactive_list) &&
		       list_empty(&dev_priv->mm.flushing_list) &&
		       list_empty(&dev_priv->mm.active_list));
	spin_unlock(&dev_priv->mm.active_list_lock);

	if (lists_empty)
		return -ENOSPC;

	/* Flush everything (on to the inactive lists) and evict */
	i915_gem_flush(dev, I915_GEM_GPU_DOMAINS, I915_GEM_GPU_DOMAINS);
	seqno = i915_add_request(dev, NULL, I915_GEM_GPU_DOMAINS);
	if (seqno == 0)
		return -ENOMEM;

	ret = i915_wait_request(dev, seqno);
	if (ret)
		return ret;

	ret = i915_gem_evict_from_inactive_list(dev, 1);
	if (ret)
		return ret;

	spin_lock(&dev_priv->mm.active_list_lock);
	lists_empty = (list_empty(&dev_priv->mm.inactive_list) &&
		       list_empty(&dev_priv->mm.flushing_list) &&
		       list_empty(&dev_priv->mm.active_list));
	spin_unlock(&dev_priv->mm.active_list_lock);
	BUG_ON(!lists_empty);

	return 0;
}

static int
i915_gem_evict_something(struct drm_device *dev, int min_size)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_gem_object *obj;
	int ret;

	for (;;) {
		i915_gem_retire_requests(dev);

		/* If there's an inactive buffer available now, grab it
		 * and be done.
		 */
		obj = i915_gem_find_inactive_object(dev, min_size);
		if (obj) {
			struct drm_i915_gem_object *obj_priv;

#if WATCH_LRU
			DRM_INFO("%s: evicting %p\n", __func__, obj);
#endif
			obj_priv = to_intel_bo(obj);
			BUG_ON(obj_priv->pin_count != 0);
			BUG_ON(obj_priv->active);

			/* Wait on the rendering and unbind the buffer. */
			return i915_gem_object_unbind(obj, 1);
		}

		/* If we didn't get anything, but the ring is still processing
		 * things, wait for the next to finish and hopefully leave us
		 * a buffer to evict.
		 */
		if (!list_empty(&dev_priv->mm.request_list)) {
			struct drm_i915_gem_request *request;

			request = list_first_entry(&dev_priv->mm.request_list,
						   struct drm_i915_gem_request,
						   list);
			ret = i915_wait_request(dev, request->seqno);
			if (ret)
				return ret;
			continue;
		}

		/* If we didn't have anything on the request list but there
		 * are buffers awaiting a flush, emit one and try again.
		 * When we wait on it, those buffers waiting for that flush
		 * will get moved to inactive.
		 */
		if (!list_empty(&dev_priv->mm.flushing_list)) {
			struct drm_i915_gem_object *obj_priv;

			/* Find an object that we can immediately reuse */
			list_for_each_entry(obj_priv, struct drm_i915_gem_object, &dev_priv->mm.flushing_list, list) {
				obj = obj_priv->obj;
				if (obj->size >= min_size)
					break;

				obj = NULL;
			}

			if (obj != NULL) {
				uint32_t seqno;

				i915_gem_flush(dev,
					       obj->write_domain,
					       obj->write_domain);
				seqno = i915_add_request(dev, NULL, obj->write_domain);
				if (seqno == 0)
					return -ENOMEM;
				continue;
			}
		}

		/* If we didn't do any of the above, there's no single buffer
		 * large enough to swap out for the new one, so just evict
		 * everything and start again. (This should be rare.)
		 */
		if (!list_empty (&dev_priv->mm.inactive_list))
			return i915_gem_evict_from_inactive_list(dev, 1);
		else
			return i915_gem_evict_everything(dev);
	}
}

int
i915_gem_object_get_pages(struct drm_gem_object *obj)
{
	struct drm_i915_gem_object *obj_priv = obj->driver_private;
	pgcnt_t np = btop(obj->size);
	caddr_t va;
	long i;

	if (obj_priv->pages_refcount++ != 0)
		return 0;

	obj_priv->page_list = kmem_zalloc(np * sizeof(caddr_t), KM_SLEEP);
	if (obj_priv->page_list == NULL) {
		DRM_ERROR("Failed to allocate page list. size = %ld", np * sizeof(caddr_t));
		return -ENOMEM;
	}

	for (i = 0, va = obj->kaddr; i < np; i++, va += PAGESIZE) {
		obj_priv->page_list[i] = va;
	}
	return 0;
}

static void sandybridge_write_fence_reg(struct drm_i915_fence_reg *reg)
{
	struct drm_gem_object *obj = reg->obj;
	struct drm_device *dev = obj->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj_priv = to_intel_bo(obj);
	int regnum = obj_priv->fence_reg;
	uint64_t val;

	val = (uint64_t)((obj_priv->gtt_offset + obj->size - 4096) &
		    0xfffff000) << 32;
	val |= obj_priv->gtt_offset & 0xfffff000;
	val |= (uint64_t)((obj_priv->stride / 128) - 1) <<
		SANDYBRIDGE_FENCE_PITCH_SHIFT;

	if (obj_priv->tiling_mode == I915_TILING_Y)
		val |= 1 << I965_FENCE_TILING_Y_SHIFT;
	val |= I965_FENCE_REG_VALID;

	I915_WRITE64(FENCE_REG_SANDYBRIDGE_0 + (regnum * 8), val);
}

static void i965_write_fence_reg(struct drm_i915_fence_reg *reg)
{
	struct drm_gem_object *obj = reg->obj;
	struct drm_device *dev = obj->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj_priv = to_intel_bo(obj);
	int regnum = obj_priv->fence_reg;
	uint64_t val;

	val = (uint64_t)((obj_priv->gtt_offset + obj->size - 4096) &
		    0xfffff000) << 32;
	val |= obj_priv->gtt_offset & 0xfffff000;
	val |= ((obj_priv->stride / 128) - 1) << I965_FENCE_PITCH_SHIFT;
	if (obj_priv->tiling_mode == I915_TILING_Y)
		val |= 1 << I965_FENCE_TILING_Y_SHIFT;
	val |= I965_FENCE_REG_VALID;

	I915_WRITE64(FENCE_REG_965_0 + (regnum * 8), val);
}

static void i915_write_fence_reg(struct drm_i915_fence_reg *reg)
{
	struct drm_gem_object *obj = reg->obj;
	struct drm_device *dev = obj->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj_priv = to_intel_bo(obj);
	int regnum = obj_priv->fence_reg;
	int tile_width;
	uint32_t fence_reg, val;
	uint32_t pitch_val;

	if ((obj_priv->gtt_offset & ~I915_FENCE_START_MASK) ||
	    (obj_priv->gtt_offset & (obj->size - 1))) {
		DRM_DEBUG("%s: object 0x%08x not 1M or size (0x%lx) aligned\n",
		     __func__, obj_priv->gtt_offset, obj->size);
		return;
	}

	if (obj_priv->tiling_mode == I915_TILING_Y &&
	    HAS_128_BYTE_Y_TILING(dev))
		tile_width = 128;
	else
		tile_width = 512;

	/* Note: pitch better be a power of two tile widths */
	pitch_val = obj_priv->stride / tile_width;
	pitch_val = ffs(pitch_val) - 1;

	val = obj_priv->gtt_offset;
	if (obj_priv->tiling_mode == I915_TILING_Y)
		val |= 1 << I830_FENCE_TILING_Y_SHIFT;
	val |= I915_FENCE_SIZE_BITS(obj->size);
	val |= pitch_val << I830_FENCE_PITCH_SHIFT;
	val |= I830_FENCE_REG_VALID;

	if (regnum < 8)
		fence_reg = FENCE_REG_830_0 + (regnum * 4);
	else
		fence_reg = FENCE_REG_945_8 + ((regnum - 8) * 4);
	I915_WRITE(fence_reg, val);
}

static void i830_write_fence_reg(struct drm_i915_fence_reg *reg)
{
	struct drm_gem_object *obj = reg->obj;
	struct drm_device *dev = obj->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj_priv = to_intel_bo(obj);
	int regnum = obj_priv->fence_reg;
	uint32_t val;
	uint32_t pitch_val;
	uint32_t fence_size_bits;


	if ((obj_priv->gtt_offset & ~I830_FENCE_START_MASK) ||
	    (obj_priv->gtt_offset & (obj->size - 1))) {
		DRM_DEBUG("%s: object 0x%08x not 512K or size aligned\n",
		     __func__, obj_priv->gtt_offset);
		return;
	}

	pitch_val = (obj_priv->stride / 128) - 1;

	val = obj_priv->gtt_offset;
	if (obj_priv->tiling_mode == I915_TILING_Y)
		val |= 1 << I830_FENCE_TILING_Y_SHIFT;
	fence_size_bits = I830_FENCE_SIZE_BITS(obj->size);
	val |= fence_size_bits;
	val |= pitch_val << I830_FENCE_PITCH_SHIFT;
	val |= I830_FENCE_REG_VALID;

	I915_WRITE(FENCE_REG_830_0 + (regnum * 4), val);

}

static int i915_find_fence_reg(struct drm_device *dev)
{
	struct drm_i915_fence_reg *reg = NULL;
	struct drm_i915_gem_object *obj_priv = NULL;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_gem_object *obj = NULL;
	int i, avail, ret;

	/* First try to find a free reg */
	avail = 0;
	for (i = dev_priv->fence_reg_start; i < dev_priv->num_fence_regs; i++) {
		reg = &dev_priv->fence_regs[i];
		if (!reg->obj)
			return i;

		obj_priv = to_intel_bo(reg->obj);
		if (!obj_priv->pin_count)
		    avail++;
	}

	if (avail == 0)
		return -ENOSPC;

	/* None available, try to steal one or wait for a user to finish */
	i = I915_FENCE_REG_NONE;
	list_for_each_entry(obj_priv, struct drm_i915_gem_object,
				&dev_priv->mm.fence_list, fence_list) {
		obj = obj_priv->obj;

		if (obj_priv->pin_count)
			continue;

		/* found one! */
		i = obj_priv->fence_reg;
		break;
	}

	BUG_ON(i == I915_FENCE_REG_NONE);

	/* We only have a reference on obj from the active list. put_fence_reg
	 * might drop that one, causing a use-after-free in it. So hold a
	 * private reference to obj like the other callers of put_fence_reg
	 * (set_tiling ioctl) do. */
	drm_gem_object_reference(obj);
	ret = i915_gem_object_put_fence_reg(obj);
	drm_gem_object_unreference(obj);
	if (ret != 0)
		return ret;

	return i;
}

/**
 * i915_gem_object_get_fence_reg - set up a fence reg for an object
 * @obj: object to map through a fence reg
 *
 * When mapping objects through the GTT, userspace wants to be able to write
 * to them without having to worry about swizzling if the object is tiled.
 *
 * This function walks the fence regs looking for a free one for @obj,
 * stealing one if it can't find any.
 *
 * It then sets up the reg based on the object's properties: address, pitch
 * and tiling format.
 */
int
i915_gem_object_get_fence_reg(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj_priv = to_intel_bo(obj);
	struct drm_i915_fence_reg *reg = NULL;
	int ret;

	/* Just update our place in the LRU if our fence is getting used. */
	if (obj_priv->fence_reg != I915_FENCE_REG_NONE) {
		list_move_tail(&obj_priv->fence_list, &dev_priv->mm.fence_list, (caddr_t)obj_priv);
		return 0;
	}

	switch (obj_priv->tiling_mode) {
	case I915_TILING_NONE:
		DRM_DEBUG("allocating a fence for non-tiled object?\n");
		break;
	case I915_TILING_X:
		if (!obj_priv->stride)
			return -EINVAL;
		     DRM_DEBUG("object 0x%08x is X tiled but has non-512B pitch\n",
		     obj_priv->gtt_offset);
		break;
	case I915_TILING_Y:
		if (!obj_priv->stride)
			return -EINVAL;
		     DRM_DEBUG("object 0x%08x is Y tiled but has non-128B pitch\n",
		     obj_priv->gtt_offset);
		break;
	}

	ret = i915_find_fence_reg(dev);
	if (ret < 0)
		return ret;

	obj_priv->fence_reg = ret;
	reg = &dev_priv->fence_regs[obj_priv->fence_reg];
	list_add_tail(&obj_priv->fence_list, &dev_priv->mm.fence_list, (caddr_t)obj_priv);

	reg->obj = obj;

	if (IS_GEN6(dev))
		sandybridge_write_fence_reg(reg);
	else if (IS_I965G(dev))
		i965_write_fence_reg(reg);
	else if (IS_I9XX(dev))
		i915_write_fence_reg(reg);
	else
		i830_write_fence_reg(reg);

	return 0;
}

/**
 * i915_gem_clear_fence_reg - clear out fence register info
 * @obj: object to clear
 *
 * Zeroes out the fence register itself and clears out the associated
 * data structures in dev_priv and obj_priv.
 */
static void
i915_gem_clear_fence_reg(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj_priv = obj->driver_private;

	if (IS_GEN6(dev)) {
		I915_WRITE64(FENCE_REG_SANDYBRIDGE_0 +
			     (obj_priv->fence_reg * 8), 0);
	} else if (IS_I965G(dev)) {
		I915_WRITE64(FENCE_REG_965_0 + (obj_priv->fence_reg * 8), 0);
	} else {
		uint32_t fence_reg;

		if (obj_priv->fence_reg < 8)
			fence_reg = FENCE_REG_830_0 + obj_priv->fence_reg * 4;
		else
			fence_reg = FENCE_REG_945_8 + (obj_priv->fence_reg -
						       8) * 4;

		I915_WRITE(fence_reg, 0);
	}

	dev_priv->fence_regs[obj_priv->fence_reg].obj = NULL;
	obj_priv->fence_reg = I915_FENCE_REG_NONE;
	list_del_init(&obj_priv->fence_list);
}

/**
 * i915_gem_object_put_fence_reg - waits on outstanding fenced access
 * to the buffer to finish, and then resets the fence register.
 * @obj: tiled object holding a fence register.
 *
 * Zeroes out the fence register itself and clears out the associated
 * data structures in dev_priv and obj_priv.
 */
int
i915_gem_object_put_fence_reg(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;
	struct drm_i915_gem_object *obj_priv = to_intel_bo(obj);

	if (obj_priv->fence_reg == I915_FENCE_REG_NONE)
		return 0;

	/* If we've changed tiling, GTT-mappings of the object
	 * need to re-fault to ensure that the correct fence register
	 * setup is in place.
	 */
	i915_gem_release_mmap(obj);

	/* On the i915, GPU access to tiled buffers is via a fence,
	 * therefore we must wait for any outstanding access to complete
	 * before clearing the fence.
	 */
	if (!IS_I965G(dev)) {
		int ret;

		i915_gem_object_flush_gpu_write_domain(obj);
		ret = i915_gem_object_wait_rendering(obj);
		if (ret != 0)
			return ret;
	}

	i915_gem_object_flush_gtt_write_domain(obj);
	i915_gem_clear_fence_reg (obj);

	return 0;
}

/**
 * Finds free space in the GTT aperture and binds the object there.
 */
static int
i915_gem_object_bind_to_gtt(struct drm_gem_object *obj, unsigned alignment)
{
	struct drm_device *dev = obj->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj_priv = obj->driver_private;
	struct drm_mm_node *free_space;
	bool retry_alloc = false;
	int ret;

	if (alignment == 0)
		alignment = i915_gem_get_gtt_alignment(obj);
	if (alignment & (i915_gem_get_gtt_alignment(obj) - 1)) {
		DRM_ERROR("Invalid object alignment requested %u\n", alignment);
		return -EINVAL;
	}

	if (obj_priv->gtt_space) {
		DRM_ERROR("Already bind!!");
		return 0;
	}
search_free:
	free_space = drm_mm_search_free(&dev_priv->mm.gtt_space,
					(unsigned long) obj->size, alignment, 0);
	if (free_space != NULL) {
		obj_priv->gtt_space = drm_mm_get_block(free_space, (unsigned long) obj->size,
						       alignment);
		if (obj_priv->gtt_space != NULL) {
			obj_priv->gtt_space->private = obj;
			obj_priv->gtt_offset = obj_priv->gtt_space->start;
		}
	}
	if (obj_priv->gtt_space == NULL) {
		/* If the gtt is empty and we're still having trouble
		 * fitting our object in, we're out of memory.
		 */
		ret = i915_gem_evict_something(dev, obj->size);
		if (ret) {
			DRM_ERROR("Failed to evict a buffer %d", ret);
			return ret;
		}
		goto search_free;
	}

	ret = i915_gem_object_get_pages(obj);
	if (ret) {
		drm_mm_put_block(obj_priv->gtt_space);
		obj_priv->gtt_space = NULL;

		if (ret == -ENOMEM) {
			/* first try to clear up some space from the GTT */
			ret = i915_gem_evict_something(dev, obj->size);
			if (ret) {
				/* now try to shrink everyone else */
				if (! retry_alloc) {
				    retry_alloc = true;
				    goto search_free;
				}

				return ret;
			}

			goto search_free;
		}

		return ret;
	}

	/* Create an AGP memory structure pointing at our pages, and bind it
	 * into the GTT.
	 */
	obj_priv->agp_mem = drm_agp_bind_pages(dev,
					       obj->pfnarray,
					       obj->size >> PAGE_SHIFT,
					       obj_priv->gtt_offset);
	if (obj_priv->agp_mem) {
		i915_gem_object_put_pages(obj);
		drm_mm_put_block(obj_priv->gtt_space);
		obj_priv->gtt_space = NULL;

		ret = i915_gem_evict_something(dev, obj->size);
		if (ret)
			return ret;

		goto search_free;
	}
	atomic_inc(&dev->gtt_count);
	atomic_add(obj->size, &dev->gtt_memory);

	/* Assert that the object is not currently in any GPU domain. As it
	 * wasn't in the GTT, there shouldn't be any way it could have been in
	 * a GPU cache
	 */
	ASSERT(!(obj->read_domains & I915_GEM_GPU_DOMAINS));
	ASSERT(!(obj->write_domain & I915_GEM_GPU_DOMAINS));

	return 0;
}

void
i915_gem_clflush_object(struct drm_gem_object *obj)
{
	struct drm_i915_gem_object	*obj_priv = to_intel_bo(obj);

	/* If we don't have a page list set up, then we're not pinned
	 * to GPU, and we can ignore the cache flush because it'll happen
	 * again at bind time.
	 */
	if (obj_priv->page_list == NULL)
		return;
	drm_clflush_pages(obj_priv->page_list, obj->size / PAGE_SIZE);
}

/** Flushes any GPU write domain for the object if it's dirty. */
static void
i915_gem_object_flush_gpu_write_domain(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;

	if ((obj->write_domain & I915_GEM_GPU_DOMAINS) == 0)
		return;

	/* Queue the GPU write cache flushing we need. */
	i915_gem_flush(dev, 0, obj->write_domain);
	(void)i915_add_request(dev, NULL, obj->write_domain);
	obj->write_domain = 0;
}

/** Flushes the GTT write domain for the object if it's dirty. */
static void
i915_gem_object_flush_gtt_write_domain(struct drm_gem_object *obj)
{
	if (obj->write_domain != I915_GEM_DOMAIN_GTT)
		return;

	/* No actual flushing is required for the GTT write domain.   Writes
	 * to it immediately go to main memory as far as we know, so there's
	 * no chipset flush.  It also doesn't land in render cache.
	 */
	obj->write_domain = 0;
}

/** Flushes the CPU write domain for the object if it's dirty. */
static void
i915_gem_object_flush_cpu_write_domain(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;

	if (obj->write_domain != I915_GEM_DOMAIN_CPU)
		return;

	i915_gem_clflush_object(obj);
	drm_agp_chipset_flush(dev);
	obj->write_domain = 0;
}

void
i915_gem_object_flush_write_domain(struct drm_gem_object *obj)
{
	switch (obj->write_domain) {
	case I915_GEM_DOMAIN_GTT:
		i915_gem_object_flush_gtt_write_domain(obj);
		break;
	case I915_GEM_DOMAIN_CPU:
		i915_gem_object_flush_cpu_write_domain(obj);
		break;
	default:
		i915_gem_object_flush_gpu_write_domain(obj);
		break;
	}
}

/**
 * Moves a single object to the GTT read, and possibly write domain.
 *
 * This function returns when the move is complete, including waiting on
 * flushes to occur.
 */
int
i915_gem_object_set_to_gtt_domain(struct drm_gem_object *obj, int write)
{
	struct drm_i915_gem_object *obj_priv = to_intel_bo(obj);
	int ret;

	/* Not valid to be called on unbound objects. */
	if (obj_priv->gtt_space == NULL)
		return -EINVAL;

	i915_gem_object_flush_gpu_write_domain(obj);
	/* Wait on any GPU rendering and flushing to occur. */
	ret = i915_gem_object_wait_rendering(obj);
	if (ret != 0) {
		DRM_ERROR("set_to_gtt_domain wait_rendering ret %d", ret);
		return ret;
	}

	/* If we're writing through the GTT domain, then CPU and GPU caches
	 * will need to be invalidated at next use.
	 */
	if (write)
		obj->read_domains &= I915_GEM_DOMAIN_GTT;

	i915_gem_object_flush_cpu_write_domain(obj);

	DRM_DEBUG("i915_gem_object_set_to_gtt_domain obj->read_domains %x ", obj->read_domains);
	/* It should now be out of any other write domains, and we can update
	 * the domain values for our changes.
	 */
	BUG_ON((obj->write_domain & ~I915_GEM_DOMAIN_GTT) != 0);
	obj->read_domains |= I915_GEM_DOMAIN_GTT;
	if (write) {
		obj->write_domain = I915_GEM_DOMAIN_GTT;
		obj_priv->dirty = 1;
	}

	return 0;
}

/*
 * Prepare buffer for display plane. Use uninterruptible for possible flush
 * wait, as in modesetting process we're not supposed to be interrupted.
 */
int
i915_gem_object_set_to_display_plane(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;
	struct drm_i915_gem_object *obj_priv = obj->driver_private;
	/* LINTED */
	uint32_t old_write_domain, old_read_domains;
	int ret;

	/* Not valid to be called on unbound objects. */
	if (obj_priv->gtt_space == NULL)
		return -EINVAL;

	i915_gem_object_flush_gpu_write_domain(obj);

	/* Wait on any GPU rendering and flushing to occur. */
	if (obj_priv->active) {
#if WATCH_BUF
		DRM_INFO("%s: object %p wait for seqno %08x\n",
			  __func__, obj, obj_priv->last_rendering_seqno);
#endif
		ret = i915_wait_request(dev, obj_priv->last_rendering_seqno);
		if (ret != 0)
			return ret;
	}

	old_write_domain = obj->write_domain;
	old_read_domains = obj->read_domains;

	obj->read_domains &= I915_GEM_DOMAIN_GTT;

	i915_gem_object_flush_cpu_write_domain(obj);

	/* It should now be out of any other write domains, and we can update
	 * the domain values for our changes.
	 */
	BUG_ON((obj->write_domain & ~I915_GEM_DOMAIN_GTT) != 0);
	obj->read_domains |= I915_GEM_DOMAIN_GTT;
	obj->write_domain = I915_GEM_DOMAIN_GTT;
	obj_priv->dirty = 1;

	return 0;
}

/**
 * Moves a single object to the CPU read, and possibly write domain.
 *
 * This function returns when the move is complete, including waiting on
 * flushes to occur.
 */
static int
i915_gem_object_set_to_cpu_domain(struct drm_gem_object *obj, int write)
{
	/* LINTED */
	struct drm_device *dev = obj->dev;
	int ret;

	i915_gem_object_flush_gpu_write_domain(obj);
	/* Wait on any GPU rendering and flushing to occur. */
	ret = i915_gem_object_wait_rendering(obj);
	if (ret != 0)
		return ret;

	i915_gem_object_flush_gtt_write_domain(obj);

	/* If we have a partially-valid cache of the object in the CPU,
	 * finish invalidating it and free the per-page flags.
	 */
	i915_gem_object_set_to_full_cpu_read_domain(obj);

	/* Flush the CPU cache if it's still invalid. */
	if ((obj->read_domains & I915_GEM_DOMAIN_CPU) == 0) {
		i915_gem_clflush_object(obj);

		obj->read_domains |= I915_GEM_DOMAIN_CPU;
	}

	/* It should now be out of any other write domains, and we can update
	 * the domain values for our changes.
	 */
	BUG_ON((obj->write_domain & ~I915_GEM_DOMAIN_CPU) != 0);

	/* If we're writing through the CPU, then the GPU read domains will
	 * need to be invalidated at next use.
	 */
	if (write) {
		obj->read_domains &= I915_GEM_DOMAIN_CPU;
		obj->write_domain = I915_GEM_DOMAIN_CPU;
	}

	return 0;
}

/*
 * Set the next domain for the specified object. This
 * may not actually perform the necessary flushing/invaliding though,
 * as that may want to be batched with other set_domain operations
 *
 * This is (we hope) the only really tricky part of gem. The goal
 * is fairly simple -- track which caches hold bits of the object
 * and make sure they remain coherent. A few concrete examples may
 * help to explain how it works. For shorthand, we use the notation
 * (read_domains, write_domain), e.g. (CPU, CPU) to indicate the
 * a pair of read and write domain masks.
 *
 * Case 1: the batch buffer
 *
 *	1. Allocated
 *	2. Written by CPU
 *	3. Mapped to GTT
 *	4. Read by GPU
 *	5. Unmapped from GTT
 *	6. Freed
 *
 *	Let's take these a step at a time
 *
 *	1. Allocated
 *		Pages allocated from the kernel may still have
 *		cache contents, so we set them to (CPU, CPU) always.
 *	2. Written by CPU (using pwrite)
 *		The pwrite function calls set_domain (CPU, CPU) and
 *		this function does nothing (as nothing changes)
 *	3. Mapped by GTT
 *		This function asserts that the object is not
 *		currently in any GPU-based read or write domains
 *	4. Read by GPU
 *		i915_gem_execbuffer calls set_domain (COMMAND, 0).
 *		As write_domain is zero, this function adds in the
 *		current read domains (CPU+COMMAND, 0).
 *		flush_domains is set to CPU.
 *		invalidate_domains is set to COMMAND
 *		clflush is run to get data out of the CPU caches
 *		then i915_dev_set_domain calls i915_gem_flush to
 *		emit an MI_FLUSH and drm_agp_chipset_flush
 *	5. Unmapped from GTT
 *		i915_gem_object_unbind calls set_domain (CPU, CPU)
 *		flush_domains and invalidate_domains end up both zero
 *		so no flushing/invalidating happens
 *	6. Freed
 *		yay, done
 *
 * Case 2: The shared render buffer
 *
 *	1. Allocated
 *	2. Mapped to GTT
 *	3. Read/written by GPU
 *	4. set_domain to (CPU,CPU)
 *	5. Read/written by CPU
 *	6. Read/written by GPU
 *
 *	1. Allocated
 *		Same as last example, (CPU, CPU)
 *	2. Mapped to GTT
 *		Nothing changes (assertions find that it is not in the GPU)
 *	3. Read/written by GPU
 *		execbuffer calls set_domain (RENDER, RENDER)
 *		flush_domains gets CPU
 *		invalidate_domains gets GPU
 *		clflush (obj)
 *		MI_FLUSH and drm_agp_chipset_flush
 *	4. set_domain (CPU, CPU)
 *		flush_domains gets GPU
 *		invalidate_domains gets CPU
 *		wait_rendering (obj) to make sure all drawing is complete.
 *		This will include an MI_FLUSH to get the data from GPU
 *		to memory
 *		clflush (obj) to invalidate the CPU cache
 *		Another MI_FLUSH in i915_gem_flush (eliminate this somehow?)
 *	5. Read/written by CPU
 *		cache lines are loaded and dirtied
 *	6. Read written by GPU
 *		Same as last GPU access
 *
 * Case 3: The constant buffer
 *
 *	1. Allocated
 *	2. Written by CPU
 *	3. Read by GPU
 *	4. Updated (written) by CPU again
 *	5. Read by GPU
 *
 *	1. Allocated
 *		(CPU, CPU)
 *	2. Written by CPU
 *		(CPU, CPU)
 *	3. Read by GPU
 *		(CPU+RENDER, 0)
 *		flush_domains = CPU
 *		invalidate_domains = RENDER
 *		clflush (obj)
 *		MI_FLUSH
 *		drm_agp_chipset_flush
 *	4. Updated (written) by CPU again
 *		(CPU, CPU)
 *		flush_domains = 0 (no previous write domain)
 *		invalidate_domains = 0 (no new read domains)
 *	5. Read by GPU
 *		(CPU+RENDER, 0)
 *		flush_domains = CPU
 *		invalidate_domains = RENDER
 *		clflush (obj)
 *		MI_FLUSH
 *		drm_agp_chipset_flush
 */
static void
i915_gem_object_set_to_gpu_domain(struct drm_gem_object *obj)
{
	struct drm_device		*dev = obj->dev;
	struct drm_i915_gem_object	*obj_priv = to_intel_bo(obj);
	uint32_t			invalidate_domains = 0;
	uint32_t			flush_domains = 0;

	BUG_ON(obj->pending_read_domains & I915_GEM_DOMAIN_CPU);
	BUG_ON(obj->pending_write_domain == I915_GEM_DOMAIN_CPU);

	intel_mark_busy(dev, obj);

	/*
	 * If the object isn't moving to a new write domain,
	 * let the object stay in multiple read domains
	 */
	if (obj->pending_write_domain == 0)
		obj->pending_read_domains |= obj->read_domains;
	else
		obj_priv->dirty = 1;

	/*
	 * Flush the current write domain if
	 * the new read domains don't match. Invalidate
	 * any read domains which differ from the old
	 * write domain
	 */
	if (obj->write_domain &&
	    obj->write_domain != obj->pending_read_domains) {
		flush_domains |= obj->write_domain;
		invalidate_domains |=
			obj->pending_read_domains & ~obj->write_domain;
	}
	/*
	 * Invalidate any read caches which may have
	 * stale data. That is, any new read domains.
	 */
	invalidate_domains |= obj->pending_read_domains & ~obj->read_domains;
	if ((flush_domains | invalidate_domains) & I915_GEM_DOMAIN_CPU) {
		DRM_DEBUG("%s: CPU domain flush %08x invalidate %08x\n",
			 __func__, flush_domains, invalidate_domains);
		i915_gem_clflush_object(obj);
	}


	/* The actual obj->write_domain will be updated with
	 * pending_write_domain after we emit the accumulated flush for all
	 * of our domain changes in execbuffers (which clears objects'
	 * write_domains).  So if we have a current write domain that we
	 * aren't changing, set pending_write_domain to that.
	 */
	if (flush_domains == 0 && obj->pending_write_domain == 0)
		obj->pending_write_domain = obj->write_domain;
	obj->read_domains = obj->pending_read_domains;

	dev->invalidate_domains |= invalidate_domains;
	dev->flush_domains |= flush_domains;

	DRM_DEBUG("%s: read %08x write %08x invalidate %08x flush %08x\n",
		 __func__,
		 obj->read_domains, obj->write_domain,
		 dev->invalidate_domains, dev->flush_domains);

}

/**
 * Moves the object from a partially CPU read to a full one.
 *
 * Note that this only resolves i915_gem_object_set_cpu_read_domain_range(),
 * and doesn't handle transitioning from !(read_domains & I915_GEM_DOMAIN_CPU).
 */
static void
i915_gem_object_set_to_full_cpu_read_domain(struct drm_gem_object *obj)
{
	struct drm_i915_gem_object *obj_priv = to_intel_bo(obj);

	if (!obj_priv->page_cpu_valid)
		return;

	/* If we're partially in the CPU read domain, finish moving it in.
	 */
	if (obj->read_domains & I915_GEM_DOMAIN_CPU) {
		int i;

		for (i = 0; i <= (obj->size - 1) / PAGE_SIZE; i++) {
			if (obj_priv->page_cpu_valid[i])
				continue;
			drm_clflush_pages(obj_priv->page_list + i, 1);
		}
	}

	/* Free the page_cpu_valid mappings which are now stale, whether
	 * or not we've got I915_GEM_DOMAIN_CPU.
	 */
	kfree(obj_priv->page_cpu_valid, obj->size / PAGE_SIZE);
	obj_priv->page_cpu_valid = NULL;
}

/**
 * Set the CPU read domain on a range of the object.
 *
 * The object ends up with I915_GEM_DOMAIN_CPU in its read flags although it's
 * not entirely valid.  The page_cpu_valid member of the object flags which
 * pages have been flushed, and will be respected by
 * i915_gem_object_set_to_cpu_domain() if it's called on to get a valid mapping
 * of the whole object.
 *
 * This function returns when the move is complete, including waiting on
 * flushes to occur.
 */
static int
i915_gem_object_set_cpu_read_domain_range(struct drm_gem_object *obj,
					  uint64_t offset, uint64_t size)
{
	struct drm_i915_gem_object *obj_priv = to_intel_bo(obj);
	int i, ret;

	if (offset == 0 && size == obj->size)
		return i915_gem_object_set_to_cpu_domain(obj, 0);

	i915_gem_object_flush_gpu_write_domain(obj);
	/* Wait on any GPU rendering and flushing to occur. */
	ret = i915_gem_object_wait_rendering(obj);
	if (ret != 0)
		return ret;
	i915_gem_object_flush_gtt_write_domain(obj);

	/* If we're already fully in the CPU read domain, we're done. */
	if (obj_priv->page_cpu_valid == NULL &&
	    (obj->read_domains & I915_GEM_DOMAIN_CPU) != 0)
		return 0;

	/* Otherwise, create/clear the per-page CPU read domain flag if we're
	 * newly adding I915_GEM_DOMAIN_CPU
	 */
	if (obj_priv->page_cpu_valid == NULL) {
		obj_priv->page_cpu_valid = kzalloc(obj->size / PAGE_SIZE,
						   GFP_KERNEL);
		if (obj_priv->page_cpu_valid == NULL)
			return -ENOMEM;
	} else if ((obj->read_domains & I915_GEM_DOMAIN_CPU) == 0)
		(void) memset(obj_priv->page_cpu_valid, 0, obj->size / PAGE_SIZE);

	/* Flush the cache on any pages that are still invalid from the CPU's
	 * perspective.
	 */
	for (i = offset / PAGE_SIZE; i <= (offset + size - 1) / PAGE_SIZE;
	     i++) {
		if (obj_priv->page_cpu_valid[i])
			continue;

		drm_clflush_pages(obj_priv->page_list + i, 1);

		obj_priv->page_cpu_valid[i] = 1;
	}

	/* It should now be out of any other write domains, and we can update
	 * the domain values for our changes.
	 */
	BUG_ON((obj->write_domain & ~I915_GEM_DOMAIN_CPU) != 0);

	obj->read_domains |= I915_GEM_DOMAIN_CPU;

	return 0;
}

/**
 * Pin an object to the GTT and evaluate the relocations landing in it.
 */
static int
i915_gem_object_pin_and_relocate(struct drm_gem_object *obj,
				 struct drm_file *file_priv,
				 struct drm_i915_gem_exec_object2 *entry,
				 struct drm_i915_gem_relocation_entry *relocs)
{
	struct drm_device *dev = obj->dev;
	/* LINTED */
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj_priv = to_intel_bo(obj);
	int i, ret;
	bool need_fence;

	need_fence = entry->flags & EXEC_OBJECT_NEEDS_FENCE &&
	             obj_priv->tiling_mode != I915_TILING_NONE;

	/* Check fence reg constraints and rebind if necessary */
	if (need_fence && !i915_gem_object_fence_offset_ok(obj,
	    obj_priv->tiling_mode))
		(void) i915_gem_object_unbind(obj, 1);

	/* Choose the GTT offset for our buffer and put it there. */
	ret = i915_gem_object_pin(obj, (uint32_t) entry->alignment);
	if (ret) {
		DRM_ERROR("failed to pin. ret=%d", ret);
		return ret;
	}
	/*
	 * Pre-965 chips need a fence register set up in order to
	 * properly handle blits to/from tiled surfaces.
	 */
	if (need_fence) {
		ret = i915_gem_object_get_fence_reg(obj);
		if (ret != 0) {
			DRM_ERROR("Failure to install fence: %d\n",
					  ret);
			i915_gem_object_unpin(obj);
			return ret;
		}
	}
	entry->offset = obj_priv->gtt_offset;

	/* Apply the relocations, using the GTT aperture to avoid cache
	 * flushing requirements.
	 */
	for (i = 0; i < entry->relocation_count; i++) {
		struct drm_i915_gem_relocation_entry *reloc= &relocs[i];
		struct drm_gem_object *target_obj;
		struct drm_i915_gem_object *target_obj_priv;
		uint32_t reloc_val, reloc_offset;
		uint32_t *reloc_entry;

		target_obj = drm_gem_object_lookup(obj->dev, file_priv,
						   reloc->target_handle);
		if (target_obj == NULL) {
			i915_gem_object_unpin(obj);
			return -EBADF;
		}
		target_obj_priv = to_intel_bo(target_obj);

		/* The target buffer should have appeared before us in the
		 * exec_object list, so it should have a GTT space bound by now.
		 */
		if (target_obj_priv->gtt_space == NULL) {
			DRM_ERROR("No GTT space found for object %d\n",
				  reloc->target_handle);
			drm_gem_object_unreference(target_obj);
			i915_gem_object_unpin(obj);
			return -EINVAL;
		}

		/* Validate that the target is in a valid r/w GPU domain */
		if (reloc->write_domain & (reloc->write_domain - 1)) {
			DRM_ERROR("reloc with multiple write domains: "
				  "obj %p target %d offset %d "
				  "read %08x write %08x",
				  (void *)obj, reloc->target_handle,
				  (int) reloc->offset,
				  reloc->read_domains,
				  reloc->write_domain);
			return -EINVAL;
		}
		if (reloc->write_domain & I915_GEM_DOMAIN_CPU ||
		    reloc->read_domains & I915_GEM_DOMAIN_CPU) {
			DRM_ERROR("reloc with read/write CPU domains: "
				  "obj %p target %d offset %d "
				  "read %08x write %08x",
				  (void *)obj, reloc->target_handle,
				  (int) reloc->offset,
				  reloc->read_domains,
				  reloc->write_domain);
			drm_gem_object_unreference(target_obj);
			i915_gem_object_unpin(obj);
			return -EINVAL;
		}
		if (reloc->write_domain && target_obj->pending_write_domain &&
		    reloc->write_domain != target_obj->pending_write_domain) {
			DRM_ERROR("Write domain conflict: "
				  "obj %p target %d offset %d "
				  "new %08x old %08x\n",
				  (void *)obj, reloc->target_handle,
				  (int) reloc->offset,
				  reloc->write_domain,
				  target_obj->pending_write_domain);
			drm_gem_object_unreference(target_obj);
			i915_gem_object_unpin(obj);
			return -EINVAL;
		}

		target_obj->pending_read_domains |= reloc->read_domains;
		target_obj->pending_write_domain |= reloc->write_domain;

		/* If the relocation already has the right value in it, no
		 * more work needs to be done.
		 */
		if (target_obj_priv->gtt_offset == reloc->presumed_offset) {
			drm_gem_object_unreference(target_obj);
			continue;
		}

		/* Check that the relocation address is valid... */
		if (reloc->offset > obj->size - 4) {
			DRM_ERROR("Relocation beyond object bounds: "
				  "obj %p target %d offset %d size %d.\n",
				  (void *)obj, reloc->target_handle,
				  (int) reloc->offset, (int) obj->size);
			drm_gem_object_unreference(target_obj);
			i915_gem_object_unpin(obj);
			return -EINVAL;
		}
		if (reloc->offset & 3) {
			DRM_ERROR("Relocation not 4-byte aligned: "
				  "obj %p target %d offset %d.\n",
				  (void *)obj, reloc->target_handle,
				  (int) reloc->offset);
			drm_gem_object_unreference(target_obj);
			i915_gem_object_unpin(obj);
			return -EINVAL;
		}

		/* and points to somewhere within the target object. */
		if (reloc->delta >= target_obj->size) {
			DRM_ERROR("Relocation beyond target object bounds: "
				  "obj %p target %d delta %d size %d.\n",
				  (void *)obj, reloc->target_handle,
				  (int) reloc->delta, (int) target_obj->size);
			drm_gem_object_unreference(target_obj);
			i915_gem_object_unpin(obj);
			return -EINVAL;
		}

		ret = i915_gem_object_set_to_gtt_domain(obj, 1);
		if (ret != 0) {
			drm_gem_object_unreference(target_obj);
			i915_gem_object_unpin(obj);
			return -EINVAL;
		}

		/* Map the page containing the relocation we're going to
		 * perform.
		 */
		int reloc_base = (reloc->offset & ~(PAGE_SIZE-1));
		reloc_offset = reloc->offset & (PAGE_SIZE-1);
		reloc_entry = (uint32_t *)(uintptr_t)(obj_priv->page_list[reloc_base/PAGE_SIZE] + reloc_offset);
		reloc_val = target_obj_priv->gtt_offset + reloc->delta;

		*reloc_entry = reloc_val;

		/* The updated presumed offset for this entry will be
		 * copied back out to the user.
		 */
		reloc->presumed_offset = target_obj_priv->gtt_offset;

		drm_gem_object_unreference(target_obj);
	}

	return 0;
}

/** Dispatch a batchbuffer to the ring
 */
static int
i915_dispatch_gem_execbuffer(struct drm_device *dev,
			      struct drm_i915_gem_execbuffer2 *exec,
			      struct drm_clip_rect *cliprects,
			      uint64_t exec_offset)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int nbox = exec->num_cliprects;
	int i = 0, count;
	uint32_t exec_start, exec_len;
	RING_LOCALS;

	exec_start = (uint32_t) exec_offset + exec->batch_start_offset;
	exec_len = (uint32_t) exec->batch_len;

	count = nbox ? nbox : 1;

	for (i = 0; i < count; i++) {
		if (i < nbox) {
			int ret = i915_emit_box(dev, cliprects, i,
						exec->DR1, exec->DR4);
			if (ret) {
				DRM_ERROR("i915_emit_box %d DR1 0x%x DRI2 0x%x", ret, exec->DR1, exec->DR4);
				return ret;
			}
		}
		if (IS_I830(dev) || IS_845G(dev)) {
			BEGIN_LP_RING(4);
			OUT_RING(MI_BATCH_BUFFER);
			OUT_RING(exec_start | MI_BATCH_NON_SECURE);
			OUT_RING(exec_start + exec_len - 4);
			OUT_RING(0);
			ADVANCE_LP_RING();
		} else {
			BEGIN_LP_RING(2);
			if (IS_I965G(dev)) {
				OUT_RING(MI_BATCH_BUFFER_START |
					 (2 << 6) |
					 MI_BATCH_NON_SECURE_I965);
				OUT_RING(exec_start);
			} else {
				OUT_RING(MI_BATCH_BUFFER_START |
					 (2 << 6));
				OUT_RING(exec_start | MI_BATCH_NON_SECURE);
			}
			ADVANCE_LP_RING();
		}
	}

	/* XXX breadcrumb */
	return 0;
}

/* Throttle our rendering by waiting until the ring has completed our requests
 * emitted over 20 msec ago.
 *
 * Note that if we were to use the current jiffies each time around the loop,
 * we wouldn't escape the function with any frames outstanding if the time to
 * render a frame was over 20ms.
 *
 * This should get us reasonable parallelism between CPU and GPU but also
 * relatively low latency when blocking on a particular request to finish.
 */
static int
i915_gem_ring_throttle(struct drm_device *dev, struct drm_file *file_priv)
{
	struct drm_i915_file_private *i915_file_priv = file_priv->driver_priv;
	int ret = 0;
	uint32_t seqno;

	spin_lock(&dev->struct_mutex);
	seqno = i915_file_priv->mm.last_gem_throttle_seqno;
	i915_file_priv->mm.last_gem_throttle_seqno =
		i915_file_priv->mm.last_gem_seqno;
	if (seqno) {
		ret = i915_wait_request(dev, seqno);
		if (ret != 0)
			DRM_ERROR("%s: i915_wait_request request->seqno %d now %d\n", __func__, seqno, i915_get_gem_seqno(dev));
	}
	spin_unlock(&dev->struct_mutex);
	return ret;
}

static int
i915_gem_get_relocs_from_user(struct drm_i915_gem_exec_object2 *exec_list,
			      uint32_t buffer_count,
			      struct drm_i915_gem_relocation_entry **relocs)
{
	uint32_t reloc_count = 0, reloc_index = 0, i;
	int ret;

	*relocs = NULL;
	for (i = 0; i < buffer_count; i++) {
		if (reloc_count + exec_list[i].relocation_count < reloc_count)
			return -EINVAL;
		reloc_count += exec_list[i].relocation_count;
	}

	/* only one buffer and no need to reloc*/
	if ((reloc_count == 0) && (buffer_count == 1))
		return 0;

	*relocs = drm_calloc(reloc_count, sizeof(**relocs),
			       DRM_MEM_DRIVER);
	if (*relocs == NULL) {
		DRM_ERROR("failed to alloc relocs, count %d\n", reloc_count);
		return -ENOMEM;
	}

	for (i = 0; i < buffer_count; i++) {
		struct drm_i915_gem_relocation_entry __user *user_relocs;

		user_relocs = (void __user *)(uintptr_t)exec_list[i].relocs_ptr;

		ret = DRM_COPY_FROM_USER(&(*relocs)[reloc_index],
				     user_relocs,
				     exec_list[i].relocation_count *
				     sizeof(**relocs));
		if (ret != 0) {
			drm_free(*relocs, reloc_count * sizeof(**relocs), DRM_MEM_DRIVER);
			*relocs = NULL;
			return -EFAULT;
		}

		reloc_index += exec_list[i].relocation_count;
	}

	return 0;
}

static int
i915_gem_put_relocs_to_user(struct drm_i915_gem_exec_object2 *exec_list,
			    uint32_t buffer_count,
			    struct drm_i915_gem_relocation_entry *relocs)
{
	uint32_t reloc_count = 0, i;
	int ret = 0;

	if (relocs == NULL)
	    return 0;

        /* only one buffer and no need to reloc*/
        if ((exec_list[0].relocation_count == 0) && (buffer_count == 1))
                return ret;

	for (i = 0; i < buffer_count; i++) {
		struct drm_i915_gem_relocation_entry __user *user_relocs;
		int unwritten;

		user_relocs = (void __user *)(uintptr_t)exec_list[i].relocs_ptr;

		unwritten = DRM_COPY_TO_USER(user_relocs,
					 &relocs[reloc_count],
					 exec_list[i].relocation_count *
					 sizeof(*relocs));

		if (unwritten) {
			ret = -EFAULT;
			goto err;
		}

		reloc_count += exec_list[i].relocation_count;
	}

err:
	drm_free(relocs, reloc_count * sizeof(*relocs), DRM_MEM_DRIVER);

	return ret;
}

static int
i915_gem_check_execbuffer(struct drm_i915_gem_execbuffer2 *exec,
			   uint64_t exec_offset)
{
	uint32_t exec_start, exec_len;

	exec_start = (uint32_t) exec_offset + exec->batch_start_offset;
	exec_len = (uint32_t) exec->batch_len;

	if ((exec_start | exec_len) & 0x7)
		return -EINVAL;

	if (!exec_start)
		return -EINVAL;

	return 0;
}

static int
i915_gem_wait_for_pending_flip(struct drm_device *dev,
			       struct drm_gem_object **object_list,
			       int count)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj_priv;
	int i, ret = 0;

	for (;;) {
		for (i = 0; i < count; i++) {
			obj_priv = to_intel_bo(object_list[i]);
			if (atomic_read(&obj_priv->pending_flip) > 0)
				break;
		}
		if (i == count)
			break;

		DRM_WAIT_ON(ret, &dev_priv->pending_flip_queue, 3 * DRM_HZ,
			(atomic_read(&obj_priv->pending_flip) == 0));

		if (ret != 0) {
			DRM_DEBUG("failed to wait for pending flip");
		}
		continue;	
	}

	return ret;
}

int
/* LINTED */
i915_gem_do_execbuffer(struct drm_device *dev, void *data,
		       struct drm_file *file_priv,
		       struct drm_i915_gem_execbuffer2 *args,
		       struct drm_i915_gem_exec_object2 *exec_list)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_file_private *i915_file_priv = file_priv->driver_priv;
	struct drm_gem_object **object_list = NULL;
	struct drm_gem_object *batch_obj;
	struct drm_i915_gem_object *obj_priv;
	struct drm_clip_rect *cliprects = NULL;
	struct drm_i915_gem_relocation_entry *relocs;
	int ret, ret2, i, pinned = 0;
	uint64_t exec_offset;
	uint32_t seqno, flush_domains, reloc_index;
	int pin_tries, flips;

#if WATCH_EXEC
	DRM_INFO("buffers_ptr %d buffer_count %d len %08x\n",
		  (int) args->buffers_ptr, args->buffer_count, args->batch_len);
#endif

	if (args->buffer_count < 1) {
		DRM_ERROR("execbuf with %d buffers\n", args->buffer_count);
		return -EINVAL;
	}
	object_list = drm_calloc(sizeof(*object_list), args->buffer_count, DRM_MEM_DRIVER);
	if (object_list == NULL) {
		DRM_ERROR("Failed to allocate exec or object list "
			  "for %d buffers\n",
			  args->buffer_count);
		ret = -ENOMEM;
		goto pre_mutex_err;
	}

	if (args->num_cliprects != 0) {
		cliprects = kcalloc(args->num_cliprects, sizeof(*cliprects),
				    GFP_KERNEL);
		if (cliprects == NULL) {
			ret = -ENOMEM;
			goto pre_mutex_err;
		}

		ret = DRM_COPY_FROM_USER(cliprects,
				     (struct drm_clip_rect __user *)
				     (uintptr_t) args->cliprects_ptr,
				     sizeof(*cliprects) * args->num_cliprects);
		if (ret != 0) {
			DRM_ERROR("copy %d cliprects failed: %d\n",
				  args->num_cliprects, ret);
			goto pre_mutex_err;
		}
	}

	ret = i915_gem_get_relocs_from_user(exec_list, args->buffer_count,
					    &relocs);
	if (ret != 0)
		goto pre_mutex_err;

	mutex_lock(&dev->struct_mutex);

	if (atomic_read(&dev_priv->mm.wedged)) {
		mutex_unlock(&dev->struct_mutex);
		ret = -EIO;
		goto pre_mutex_err;
	}

	if (dev_priv->mm.suspended) {
		mutex_unlock(&dev->struct_mutex);
		ret = -EBUSY;
		goto pre_mutex_err;
	}

	/* Look up object handles */
	flips = 0;
	for (i = 0; i < args->buffer_count; i++) {
		object_list[i] = drm_gem_object_lookup(dev, file_priv,
						       exec_list[i].handle);
		if (object_list[i] == NULL) {
			DRM_ERROR("Invalid object handle %d at index %d\n",
				   exec_list[i].handle, i);
			/* prevent error path from reading uninitialized data */
			args->buffer_count = i + 1;
			ret = -EBADF;
			goto err;
		}

		obj_priv = to_intel_bo(object_list[i]);
		if (obj_priv->in_execbuffer) {
			DRM_ERROR("Object[%d] (%d) %p appears more than once in object list in args->buffer_count %d \n",
				   i, object_list[i]->name, (void *)object_list[i], args->buffer_count);
			/* prevent error path from reading uninitialized data */
			args->buffer_count = i + 1;
			ret = -EBADF;
			goto err;
		}
		obj_priv->in_execbuffer = true;
		flips += atomic_read(&obj_priv->pending_flip);
	}

	if (flips > 0) {
		ret = i915_gem_wait_for_pending_flip(dev, object_list,
						     args->buffer_count);
		if (ret)
			goto err;
	}

	/* Pin and relocate */
	for (pin_tries = 0; ; pin_tries++) {
		ret = 0;
		reloc_index = 0;

		for (i = 0; i < args->buffer_count; i++) {
			object_list[i]->pending_read_domains = 0;
			object_list[i]->pending_write_domain = 0;
			ret = i915_gem_object_pin_and_relocate(object_list[i],
							       file_priv,
							       &exec_list[i],
							       &relocs[reloc_index]);
			if (ret) {
				DRM_ERROR("Not all object pinned");
				break;
			}
			pinned = i + 1;
			reloc_index += exec_list[i].relocation_count;
		}
		/* success */
		if (ret == 0)
		{
			DRM_DEBUG("gem_execbuffer pin_relocate success");	
			break;
		}
		/* error other than GTT full, or we've already tried again */
		if (ret != -ENOMEM || pin_tries >= 1) {
			if (ret != -ERESTART) {
				unsigned long long total_size = 0;
				for (i = 0; i < args->buffer_count; i++)
					total_size += object_list[i]->size;
				DRM_ERROR("Failed to pin buffer %d of %d, total %llu bytes: %d\n",
					  pinned+1, args->buffer_count,
					  total_size, ret);
				DRM_ERROR("%d objects [%d pinned], "
					  "%d object bytes [%d pinned], "
					  "%d/%d gtt bytes\n",
					  atomic_read(&dev->object_count),
					  atomic_read(&dev->pin_count),
					  atomic_read(&dev->object_memory),
					  atomic_read(&dev->pin_memory),
					  atomic_read(&dev->gtt_memory),
					  dev->gtt_total);
			}
			goto err;
		}

		/* unpin all of our buffers */
		for (i = 0; i < pinned; i++)
			i915_gem_object_unpin(object_list[i]);
		pinned = 0;

		/* evict everyone we can from the aperture */
		ret = i915_gem_evict_everything(dev);
		if (ret)
			goto err;
	}

	/* Set the pending read domains for the batch buffer to COMMAND */
	batch_obj = object_list[args->buffer_count-1];
	if (batch_obj->pending_write_domain) {
		DRM_ERROR("Attempting to use self-modifying batch buffer\n");
		ret = -EINVAL;
		goto err;
	}
	batch_obj->pending_read_domains |= I915_GEM_DOMAIN_COMMAND;

	/* Sanity check the batch buffer, prior to moving objects */
	exec_offset = exec_list[args->buffer_count - 1].offset;
	ret = i915_gem_check_execbuffer (args, exec_offset);
	if (ret != 0) {
		DRM_ERROR("execbuf with invalid offset/length\n");
		goto err;
	}

	/* Zero the global flush/invalidate flags. These
	 * will be modified as new domains are computed
	 * for each object
	 */
	dev->invalidate_domains = 0;
	dev->flush_domains = 0;

	for (i = 0; i < args->buffer_count; i++) {
		struct drm_gem_object *obj = object_list[i];

		/* Compute new gpu domains and update invalidate/flush */
		i915_gem_object_set_to_gpu_domain(obj);
	}

	if (dev->invalidate_domains | dev->flush_domains) {

		DRM_INFO("%s: invalidate_domains %08x flush_domains %08x\n",
			  __func__,
			 dev->invalidate_domains,
			 dev->flush_domains);
		i915_gem_flush(dev,
			       dev->invalidate_domains,
			       dev->flush_domains);
		if (dev->flush_domains)
			(void)i915_add_request(dev, file_priv,
					       dev->flush_domains);
	}

	for (i = 0; i < args->buffer_count; i++) {
		struct drm_gem_object *obj = object_list[i];
		obj->write_domain = obj->pending_write_domain;
	}

	/* Exec the batchbuffer */
	ret = i915_dispatch_gem_execbuffer(dev, args, cliprects, exec_offset);
	if (ret) {
		DRM_ERROR("dispatch failed %d\n", ret);
		goto err;
	}

	/*
	 * Ensure that the commands in the batch buffer are
	 * finished before the interrupt fires
	 */
	flush_domains = i915_retire_commands(dev);

	/*
	 * Get a seqno representing the execution of the current buffer,
	 * which we can wait on.  We would like to mitigate these interrupts,
	 * likely by only creating seqnos occasionally (so that we have
	 * *some* interrupts representing completion of buffers that we can
	 * wait on when trying to clear up gtt space).
	 */
	seqno = i915_add_request(dev, file_priv, flush_domains);
	ASSERT(!(seqno == 0));
	i915_file_priv->mm.last_gem_seqno = seqno;
	for (i = 0; i < args->buffer_count; i++) {
		struct drm_gem_object *obj = object_list[i];

		i915_gem_object_move_to_active(obj, seqno);
		DRM_INFO("%s: move to exec list %p\n", __func__, (void *)obj);
	}

err:
	for (i = 0; i < pinned; i++)
		i915_gem_object_unpin(object_list[i]);

	for (i = 0; i < args->buffer_count; i++) {
		if (object_list[i]) {
			obj_priv = to_intel_bo(object_list[i]);
			obj_priv->in_execbuffer = false;
		}
		drm_gem_object_unreference(object_list[i]);
	}

	mutex_unlock(&dev->struct_mutex);

pre_mutex_err:
	/* Copy the updated relocations out regardless of current error
	 * state.  Failure to update the relocs would mean that the next
	 * time userland calls execbuf, it would do so with presumed offset
	 * state that didn't match the actual object state.
	 */
	ret2 = i915_gem_put_relocs_to_user(exec_list, args->buffer_count,
					   relocs);
	if (ret2 != 0) {
		DRM_ERROR("Failed to copy relocations back out: %d\n", ret2);

		if (ret == 0)
			ret = ret2;
	}

	drm_free(object_list, sizeof(*object_list) * args->buffer_count,
		 DRM_MEM_DRIVER);
	drm_free(cliprects, args->num_cliprects * sizeof(*cliprects),
		 DRM_MEM_DRIVER);
	return ret;
}

/*
 * Legacy execbuffer just creates an exec2 list from the original exec object
 * list array and passes it to the real function.
 */
int
/* LINTED */
i915_gem_execbuffer(DRM_IOCTL_ARGS)
{
	struct drm_i915_gem_execbuffer *args = data;
	struct drm_i915_gem_execbuffer2 exec2;
	struct drm_i915_gem_exec_object *exec_list = NULL;
	struct drm_i915_gem_exec_object2 *exec2_list = NULL;
	int ret, i;

#if WATCH_EXEC
	DRM_INFO("buffers_ptr %d buffer_count %d len %08x\n",
		  (int) args->buffers_ptr, args->buffer_count, args->batch_len);
#endif

	if (args->buffer_count < 1) {
		DRM_ERROR("execbuf with %d buffers\n", args->buffer_count);
		return -EINVAL;
	}

	/* Copy in the exec list from userland */
	exec_list = drm_calloc(sizeof(*exec_list), args->buffer_count, DRM_MEM_DRIVER);
	exec2_list = drm_calloc(sizeof(*exec2_list), args->buffer_count, DRM_MEM_DRIVER);
	if (exec_list == NULL || exec2_list == NULL) {
		DRM_ERROR("Failed to allocate exec list for %d buffers\n",
			  args->buffer_count);
		drm_free(exec_list, args->buffer_count * sizeof(*exec_list), DRM_MEM_DRIVER);
		drm_free(exec2_list, args->buffer_count * sizeof(*exec2_list), DRM_MEM_DRIVER);
		return -ENOMEM;
	}
	ret = copy_from_user(exec_list,
			     (struct drm_i915_relocation_entry __user *)
			     (uintptr_t) args->buffers_ptr,
			     sizeof(*exec_list) * args->buffer_count);
	if (ret != 0) {
		DRM_ERROR("copy %d exec entries failed %d\n",
			  args->buffer_count, ret);
		drm_free(exec_list, args->buffer_count * sizeof(*exec_list), DRM_MEM_DRIVER);
		drm_free(exec2_list, args->buffer_count * sizeof(*exec2_list), DRM_MEM_DRIVER);
		return -EFAULT;
	}

	for (i = 0; i < args->buffer_count; i++) {
		exec2_list[i].handle = exec_list[i].handle;
		exec2_list[i].relocation_count = exec_list[i].relocation_count;
		exec2_list[i].relocs_ptr = exec_list[i].relocs_ptr;
		exec2_list[i].alignment = exec_list[i].alignment;
		exec2_list[i].offset = exec_list[i].offset;
		if (!IS_I965G(dev))
			exec2_list[i].flags = EXEC_OBJECT_NEEDS_FENCE;
		else
			exec2_list[i].flags = 0;
	}

	exec2.buffers_ptr = args->buffers_ptr;
	exec2.buffer_count = args->buffer_count;
	exec2.batch_start_offset = args->batch_start_offset;
	exec2.batch_len = args->batch_len;
	exec2.DR1 = args->DR1;
	exec2.DR4 = args->DR4;
	exec2.num_cliprects = args->num_cliprects;
	exec2.cliprects_ptr = args->cliprects_ptr;
	exec2.flags = 0;

	ret = i915_gem_do_execbuffer(dev, data, file_priv, &exec2, exec2_list);
	if (!ret) {
		/* Copy the new buffer offsets back to the user's exec list. */
		for (i = 0; i < args->buffer_count; i++)
			exec_list[i].offset = exec2_list[i].offset;
		/* ... and back out to userspace */
		ret = copy_to_user((struct drm_i915_relocation_entry __user *)
				   (uintptr_t) args->buffers_ptr,
				   exec_list,
				   sizeof(*exec_list) * args->buffer_count);
		if (ret) {
			ret = -EFAULT;
			DRM_ERROR("failed to copy %d exec entries "
				  "back to user (%d)\n",
				  args->buffer_count, ret);
		}
	}

	drm_free(exec_list, args->buffer_count * sizeof(*exec_list), DRM_MEM_DRIVER);
	drm_free(exec2_list, args->buffer_count * sizeof(*exec2_list), DRM_MEM_DRIVER);
	return ret;
}

int
/* LINTED */
i915_gem_execbuffer2(DRM_IOCTL_ARGS)
{
	struct drm_i915_gem_execbuffer2 *args = data;
	struct drm_i915_gem_exec_object2 *exec2_list = NULL;
	int ret;

#if WATCH_EXEC
	DRM_INFO("buffers_ptr %d buffer_count %d len %08x\n",
		  (int) args->buffers_ptr, args->buffer_count, args->batch_len);
#endif

	if (args->buffer_count < 1) {
		DRM_ERROR("execbuf2 with %d buffers\n", args->buffer_count);
		return -EINVAL;
	}

	exec2_list = drm_calloc(sizeof(*exec2_list), args->buffer_count, DRM_MEM_DRIVER);
	if (exec2_list == NULL) {
		DRM_ERROR("Failed to allocate exec list for %d buffers\n",
			  args->buffer_count);
		return -ENOMEM;
	}
	ret = copy_from_user(exec2_list,
			     (struct drm_i915_relocation_entry __user *)
			     (uintptr_t) args->buffers_ptr,
			     sizeof(*exec2_list) * args->buffer_count);
	if (ret != 0) {
		DRM_ERROR("copy %d exec entries failed %d\n",
			  args->buffer_count, ret);
		drm_free(exec2_list, args->buffer_count * sizeof(*exec2_list), DRM_MEM_DRIVER);
		return -EFAULT;
	}

	ret = i915_gem_do_execbuffer(dev, data, file_priv, args, exec2_list);
	if (!ret) {
		/* Copy the new buffer offsets back to the user's exec list. */
		ret = copy_to_user((struct drm_i915_relocation_entry __user *)
				   (uintptr_t) args->buffers_ptr,
				   exec2_list,
				   sizeof(*exec2_list) * args->buffer_count);
		if (ret) {
			ret = -EFAULT;
			DRM_ERROR("failed to copy %d exec entries "
				  "back to user (%d)\n",
				  args->buffer_count, ret);
		}
	}

	drm_free(exec2_list, args->buffer_count * sizeof(*exec2_list), DRM_MEM_DRIVER);
	return ret;
}

int
i915_gem_object_pin(struct drm_gem_object *obj, uint32_t alignment)
{
	struct drm_device *dev = obj->dev;
	struct drm_i915_gem_object *obj_priv = to_intel_bo(obj);
	int ret;

	if (obj_priv->gtt_space == NULL) {
		ret = i915_gem_object_bind_to_gtt(obj, alignment);
		if (ret) {
			DRM_ERROR("Failure to bind: %d", ret);
			return ret;
		}
	}
	obj_priv->pin_count++;

	/* If the object is not active and not pending a flush,
	 * remove it from the inactive list
	 */
	if (obj_priv->pin_count == 1) {
		atomic_inc(&dev->pin_count);
		atomic_add(obj->size, &dev->pin_memory);
		if (!obj_priv->active &&
		    (obj->write_domain & I915_GEM_GPU_DOMAINS) == 0 &&
		    !list_empty(&obj_priv->list))
			list_del_init(&obj_priv->list);
	}

	return 0;
}

void
i915_gem_object_unpin(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj_priv = to_intel_bo(obj);

	obj_priv->pin_count--;
	BUG_ON(obj_priv->pin_count < 0);
	BUG_ON(obj_priv->gtt_space == NULL);

	/* If the object is no longer pinned, and is
	 * neither active nor being flushed, then stick it on
	 * the inactive list
	 */
	if (obj_priv->pin_count == 0) {
		if (!obj_priv->active &&
		    (obj->write_domain & I915_GEM_GPU_DOMAINS) == 0)
			list_move_tail(&obj_priv->list,
				       &dev_priv->mm.inactive_list, (caddr_t)obj_priv);
		atomic_dec(&dev->pin_count);
		atomic_sub(obj->size, &dev->pin_memory);
	}
}

int
/* LINTED */
i915_gem_pin_ioctl(DRM_IOCTL_ARGS)
{
	struct drm_i915_gem_pin *args = data;
	struct drm_gem_object *obj;
	struct drm_i915_gem_object *obj_priv;
	int ret;

	mutex_lock(&dev->struct_mutex);

	obj = drm_gem_object_lookup(dev, file_priv, args->handle);
	if (obj == NULL) {
		DRM_ERROR("Bad handle in i915_gem_pin_ioctl(): %d\n",
			  args->handle);
		mutex_unlock(&dev->struct_mutex);
		return -EBADF;
	}
	obj_priv = to_intel_bo(obj);

	DRM_DEBUG("obj->name=%d, obj->maplist->user_token=%lu", obj->name, obj->maplist.user_token);

	if (obj_priv->pin_filp != NULL && obj_priv->pin_filp != file_priv) {
		DRM_ERROR("Already pinned in i915_gem_pin_ioctl(): %d\n",
			  args->handle);
		drm_gem_object_unreference(obj);
		mutex_unlock(&dev->struct_mutex);
		return -EINVAL;
	}

	obj_priv->user_pin_count++;
	obj_priv->pin_filp = file_priv;
	if (obj_priv->user_pin_count == 1) {
		ret = i915_gem_object_pin(obj, args->alignment);
		if (ret != 0) {
			drm_gem_object_unreference(obj);
			mutex_unlock(&dev->struct_mutex);
			return ret;
		}
	}

	/* XXX - flush the CPU caches for pinned objects
	 * as the X server doesn't manage domains yet
	 */
	i915_gem_object_flush_cpu_write_domain(obj);
	args->offset = obj_priv->gtt_offset;
	drm_gem_object_unreference(obj);
	mutex_unlock(&dev->struct_mutex);

	return 0;
}

int
/* LINTED */
i915_gem_unpin_ioctl(DRM_IOCTL_ARGS)
{
	struct drm_i915_gem_pin *args = data;
	struct drm_gem_object *obj;
	struct drm_i915_gem_object *obj_priv;

	mutex_lock(&dev->struct_mutex);

	obj = drm_gem_object_lookup(dev, file_priv, args->handle);
	if (obj == NULL) {
		DRM_ERROR("Bad handle in i915_gem_unpin_ioctl(): %d\n",
			  args->handle);
		mutex_unlock(&dev->struct_mutex);
		return -EBADF;
	}

	obj_priv = to_intel_bo(obj);
	if (obj_priv->pin_filp != file_priv) {
		DRM_ERROR("Not pinned by caller in i915_gem_pin_ioctl(): %d\n",
			  args->handle);
		drm_gem_object_unreference(obj);
		mutex_unlock(&dev->struct_mutex);
		return -EINVAL;
	}
	obj_priv->user_pin_count--;
	if (obj_priv->user_pin_count == 0) {
		obj_priv->pin_filp = NULL;
		i915_gem_object_unpin(obj);
	}

	drm_gem_object_unreference(obj);
	mutex_unlock(&dev->struct_mutex);
	return 0;
}

int
/* LINTED */
i915_gem_busy_ioctl(DRM_IOCTL_ARGS)
{
	struct drm_i915_gem_busy *args = data;
	struct drm_gem_object *obj;
	struct drm_i915_gem_object *obj_priv;

	obj = drm_gem_object_lookup(dev, file_priv, args->handle);
	if (obj == NULL) {
		DRM_ERROR("Bad handle in i915_gem_busy_ioctl(): %d\n",
			  args->handle);
		return -EBADF;
	}

	mutex_lock(&dev->struct_mutex);
	/* Update the active list for the hardware's current position.
	 * Otherwise this only updates on a delayed timer or when irqs are
	 * actually unmasked, and our working set ends up being larger than
	 * required.
	 */
	i915_gem_retire_requests(dev);

	obj_priv = to_intel_bo(obj);
	/* Don't count being on the flushing list against the object being
	 * done.  Otherwise, a buffer left on the flushing list but not getting
	 * flushed (because nobody's flushing that domain) won't ever return
	 * unbusy and get reused by libdrm's bo cache.  The other expected
	 * consumer of this interface, OpenGL's occlusion queries, also specs
	 * that the objects get unbusy "eventually" without any interference.
	 */
	args->busy = obj_priv->active && obj_priv->last_rendering_seqno != 0;

	drm_gem_object_unreference(obj);
	mutex_unlock(&dev->struct_mutex);
	return 0;
}

int
/* LINTED */
i915_gem_throttle_ioctl(DRM_IOCTL_ARGS)
{
    return i915_gem_ring_throttle(dev, file_priv);
}

int
/* LINTED */
i915_gem_madvise_ioctl(DRM_IOCTL_ARGS)
{
	struct drm_i915_gem_madvise *args = data;

	/* Don't enable buffer catch */
	args->retained = 0;
	return 0;
}

int i915_gem_init_object(struct drm_gem_object *obj)
{
	struct drm_i915_gem_object *obj_priv;

	obj_priv = kzalloc(sizeof(*obj_priv), GFP_KERNEL);
	if (obj_priv == NULL)
		return -ENOMEM;

	/*
	 * We've just allocated pages from the kernel,
	 * so they've just been written by the CPU with
	 * zeros. They'll need to be clflushed before we
	 * use them with the GPU.
	 */
	obj->write_domain = I915_GEM_DOMAIN_CPU;
	obj->read_domains = I915_GEM_DOMAIN_CPU;

	obj->driver_private = obj_priv;
	obj_priv->obj = obj;
	obj_priv->fence_reg = I915_FENCE_REG_NONE;
	INIT_LIST_HEAD(&obj_priv->list);
	INIT_LIST_HEAD(&obj_priv->fence_list);

	return 0;
}

void i915_gem_free_object(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;
	struct drm_i915_gem_object *obj_priv = obj->driver_private;

	while (obj_priv->pin_count > 0)
		i915_gem_object_unpin(obj);

	if (obj_priv->phys_obj)
		i915_gem_detach_phys_object(dev, obj);

	(void) i915_gem_object_unbind(obj, 1);

	if (obj_priv->mmap_offset)
		i915_gem_free_mmap_offset(obj);

	if (obj_priv->page_cpu_valid != NULL)
		kfree(obj_priv->page_cpu_valid, obj->size / PAGE_SIZE);
	kfree(obj->driver_private, sizeof(*obj_priv));
}

/** Unbinds all inactive objects. */
static int
i915_gem_evict_from_inactive_list(struct drm_device *dev, uint32_t type)
{
	drm_i915_private_t *dev_priv = dev->dev_private;

	while (!list_empty(&dev_priv->mm.inactive_list)) {
		struct drm_gem_object *obj;
		int ret;

		obj = list_first_entry(&dev_priv->mm.inactive_list,
				       struct drm_i915_gem_object,
				       list)->obj;

		ret = i915_gem_object_unbind(obj, type);
		if (ret != 0) {
			DRM_ERROR("Error unbinding object: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

int
i915_gem_idle(struct drm_device *dev, uint32_t type)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	uint32_t seqno, cur_seqno, last_seqno;
	int stuck, ret;

	mutex_lock(&dev->struct_mutex);

	if (dev_priv->mm.suspended || dev_priv->ring.ring_obj == NULL) {
		mutex_unlock(&dev->struct_mutex);
		return 0;
	}

	/* Hack!  Don't let anybody do execbuf while we don't control the chip.
	 * We need to replace this with a semaphore, or something.
	 */
	dev_priv->mm.suspended = 1;
	del_timer(&dev_priv->hangcheck_timer);

	/* Cancel the retire work handler, wait for it to finish if running
	 */
	if (worktimer_id != NULL) {
		(void) untimeout(worktimer_id);
		worktimer_id = NULL;
	}

	i915_kernel_lost_context(dev);

	/* Flush the GPU along with all non-CPU write domains
	 */
	i915_gem_flush(dev, I915_GEM_GPU_DOMAINS, I915_GEM_GPU_DOMAINS);
	seqno = i915_add_request(dev, NULL, I915_GEM_GPU_DOMAINS);

	if (seqno == 0) {
		mutex_unlock(&dev->struct_mutex);
		return -ENOMEM;
	}

	dev_priv->mm.waiting_gem_seqno = seqno;
	last_seqno = 0;
	stuck = 0;
	for (;;) {
		cur_seqno = i915_get_gem_seqno(dev);
		if (i915_seqno_passed(cur_seqno, seqno))
			break;
		if (last_seqno == cur_seqno) {
			if (stuck++ > 100) {
				DRM_ERROR("hardware wedged\n");
				atomic_set(&dev_priv->mm.wedged, 1);
				DRM_WAKEUP(&dev_priv->irq_queue);
				break;
			}
		}
		DRM_UDELAY(10);
		last_seqno = cur_seqno;
	}
	dev_priv->mm.waiting_gem_seqno = 0;

	i915_gem_retire_requests(dev);

	spin_lock(&dev_priv->mm.active_list_lock);
	if (!atomic_read(&dev_priv->mm.wedged)) {
		/* Active and flushing should now be empty as we've
		 * waited for a sequence higher than any pending execbuffer
		 */
		/* Request should now be empty as we've also waited
		 * for the last request in the list
		 */

		/* For lint check */
		dev_priv->mm.waiting_gem_seqno = 0;
	}

	/* Empty the active and flushing lists to inactive.  If there's
	 * anything left at this point, it means that we're wedged and
	 * nothing good's going to happen by leaving them there.  So strip
	 * the GPU domains and just stuff them onto inactive.
	 */
	while (!list_empty(&dev_priv->mm.active_list)) {
		struct drm_gem_object *obj;

		obj = list_first_entry(&dev_priv->mm.active_list,
				       struct drm_i915_gem_object,
				       list)->obj;

		obj->write_domain &= ~I915_GEM_GPU_DOMAINS;
		i915_gem_object_move_to_inactive(obj);
	}
	spin_unlock(&dev_priv->mm.active_list_lock);

	while (!list_empty(&dev_priv->mm.flushing_list)) {
		struct drm_gem_object *obj;

		obj = list_first_entry(&dev_priv->mm.flushing_list,
				       struct drm_i915_gem_object,
				       list)->obj;

		obj->write_domain &= ~I915_GEM_GPU_DOMAINS;
		i915_gem_object_move_to_inactive(obj);
	}


	/* Move all inactive buffers out of the GTT. */
	ret = i915_gem_evict_from_inactive_list(dev, type);
	ASSERT(list_empty(&dev_priv->mm.inactive_list));
	if (ret) {
		mutex_unlock(&dev->struct_mutex);
		return ret;
	}

	i915_gem_cleanup_ringbuffer(dev);
	mutex_unlock(&dev->struct_mutex);

	return 0;
}

/*
 * 965+ support PIPE_CONTROL commands, which provide finer grained control
 * over cache flushing.
 */
static int
i915_gem_init_pipe_control(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_gem_object *obj;
	struct drm_i915_gem_object *obj_priv;
	int ret;

	obj = drm_gem_object_alloc(dev, 4096);
	if (obj == NULL) {
		DRM_ERROR("Failed to allocate seqno page\n");
		ret = -ENOMEM;
		goto err;
	}
	obj_priv = to_intel_bo(obj);

	ret = i915_gem_object_pin(obj, 4096);
	if (ret)
		goto err_unref;

	dev_priv->seqno_gfx_addr = obj_priv->gtt_offset;
	dev_priv->seqno_page_map.offset = dev->agp->agp_info.agpi_aperbase + obj_priv->gtt_offset;
	dev_priv->seqno_page_map.size = 4096;
	dev_priv->seqno_page_map.type = 0;
	dev_priv->seqno_page_map.flags = 0;
	dev_priv->seqno_page_map.mtrr = 0;

	drm_core_ioremap(&dev_priv->seqno_page_map, dev);
	if (dev_priv->seqno_page_map.handle == NULL) {
		DRM_ERROR("Failed to map status page.\n");
		(void) memset(&dev_priv->seqno_page_map, 0, sizeof(dev_priv->seqno_page_map));
		i915_gem_object_unpin(obj);
		drm_gem_object_unreference(obj);
		return -EINVAL;
	}
	dev_priv->seqno_page = dev_priv->seqno_page_map.handle;

	if (dev_priv->seqno_page == NULL)
		goto err_unpin;

	dev_priv->seqno_obj = obj;
	(void) memset(dev_priv->seqno_page, 0, PAGE_SIZE);

	return 0;

err_unpin:
	i915_gem_object_unpin(obj);
err_unref:
	drm_gem_object_unreference(obj);
err:
	return ret;
}

static int
i915_gem_init_hws(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_gem_object *obj;
	struct drm_i915_gem_object *obj_priv;
	int ret;

	/* If we need a physical address for the status page, it's already
	 * initialized at driver load time.
	 */
	if (!I915_NEED_GFX_HWS(dev))
		return 0;

	obj = drm_gem_object_alloc(dev, 4096);
	if (obj == NULL) {
		DRM_ERROR("Failed to allocate status page\n");
		ret = -ENOMEM;
		goto err;
	}
	obj_priv = to_intel_bo(obj);

	ret = i915_gem_object_pin(obj, 4096);
	if (ret != 0) {
		drm_gem_object_unreference(obj);
		goto err_unref;
	}

	dev_priv->status_gfx_addr = obj_priv->gtt_offset;
	dev_priv->hws_map.offset = dev->agp->agp_info.agpi_aperbase + obj_priv->gtt_offset;
	dev_priv->hws_map.size = 4096;
	dev_priv->hws_map.type = 0;
	dev_priv->hws_map.flags = 0;
	dev_priv->hws_map.mtrr = 0;

	drm_core_ioremap(&dev_priv->hws_map, dev);
	if (dev_priv->hws_map.handle == NULL) {
		DRM_ERROR("Failed to map status page.\n");
		(void) memset(&dev_priv->hws_map, 0, sizeof(dev_priv->hws_map));
		ret = -EINVAL;
		goto err_unpin;
	}
	dev_priv->hw_status_page = dev_priv->hws_map.handle;

	if (HAS_PIPE_CONTROL(dev)) {
		ret = i915_gem_init_pipe_control(dev);
		if (ret)
			goto err_unpin;
	}

	dev_priv->hws_obj = obj;

	(void) memset(dev_priv->hw_status_page, 0, PAGE_SIZE);

	if (IS_GEN6(dev)) {
		I915_WRITE(HWS_PGA_GEN6, dev_priv->status_gfx_addr);
		I915_READ(HWS_PGA_GEN6); /* posting read */
	} else {
		I915_WRITE(HWS_PGA, dev_priv->status_gfx_addr);
		I915_READ(HWS_PGA); /* posting read */
	}
	DRM_DEBUG_DRIVER("hws offset: 0x%08x\n", dev_priv->status_gfx_addr);

	return 0;

err_unpin:
	i915_gem_object_unpin(obj);
err_unref:
	drm_gem_object_unreference(obj);
err:
	return 0;
}

static void
i915_gem_cleanup_pipe_control(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_gem_object *obj;
	/* LINTED */
	struct drm_i915_gem_object *obj_priv;

	obj = dev_priv->seqno_obj;
	obj_priv = to_intel_bo(obj);
	drm_core_ioremapfree(&dev_priv->seqno_page_map, dev);
	i915_gem_object_unpin(obj);
	drm_gem_object_unreference(obj);
	dev_priv->seqno_obj = NULL;

	dev_priv->seqno_page = NULL;
}

static void
i915_gem_cleanup_hws(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_gem_object *obj;
	/* LINTED */
	struct drm_i915_gem_object *obj_priv;

	if (dev_priv->hws_obj == NULL)
		return;

	obj = dev_priv->hws_obj;
	obj_priv = to_intel_bo(obj);

	drm_core_ioremapfree(&dev_priv->hws_map, dev);
	i915_gem_object_unpin(obj);
	drm_gem_object_unreference(obj);
	dev_priv->hws_obj = NULL;

	(void) memset(&dev_priv->hws_map, 0, sizeof(dev_priv->hws_map));
	dev_priv->hw_status_page = NULL;

	if (HAS_PIPE_CONTROL(dev))
		i915_gem_cleanup_pipe_control(dev);

	/* Write high address into HWS_PGA when disabling. */
	I915_WRITE(HWS_PGA, 0x1ffff000);
}

int
i915_gem_init_ringbuffer(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_gem_object *obj;
	struct drm_i915_gem_object *obj_priv;
	drm_i915_ring_buffer_t *ring = &dev_priv->ring;
	int ret;
	u32 head;
	int mode;

	ret = i915_gem_init_hws(dev);
	if (ret != 0)
		return ret;

	obj = drm_gem_object_alloc(dev, 128 * 1024);
	if (obj == NULL) {
		DRM_ERROR("Failed to allocate ringbuffer\n");
		i915_gem_cleanup_hws(dev);
		return -ENOMEM;
	}
	obj_priv = to_intel_bo(obj);

	ret = i915_gem_object_pin(obj, 4096);
	if (ret != 0) {
		drm_gem_object_unreference(obj);
		i915_gem_cleanup_hws(dev);
		return ret;
	}

	/* Set up the kernel mapping for the ring. */
	ring->Size = obj->size;

	ring->map.offset = dev->agp->agp_info.agpi_aperbase + obj_priv->gtt_offset;
	ring->map.size = obj->size;
	ring->map.type = 0;
	ring->map.flags = 0;
	ring->map.mtrr = 0;

	drm_core_ioremap(&ring->map, dev);
	if (ring->map.handle == NULL) {
		DRM_ERROR("Failed to map ringbuffer.\n");
		(void) memset(&dev_priv->ring, 0, sizeof(dev_priv->ring));
		i915_gem_object_unpin(obj);
		drm_gem_object_unreference(obj);
		i915_gem_cleanup_hws(dev);
		return -EINVAL;
	}
	ring->ring_obj = obj;
	ring->virtual_start = (u8 *) ring->map.handle;

	/* Stop the ring if it's running. */
	I915_WRITE(PRB0_CTL, 0);
	I915_WRITE(PRB0_TAIL, 0);
	I915_WRITE(PRB0_HEAD, 0);

	/* Initialize the ring. */
	I915_WRITE(PRB0_START, obj_priv->gtt_offset);
	head = I915_READ(PRB0_HEAD) & HEAD_ADDR;

	/* G45 ring initialization fails to reset head to zero */
	if (head != 0) {
		DRM_ERROR("Ring head not reset to zero "
			  "ctl %08x head %08x tail %08x start %08x\n",
			  I915_READ(PRB0_CTL),
			  I915_READ(PRB0_HEAD),
			  I915_READ(PRB0_TAIL),
			  I915_READ(PRB0_START));
		I915_WRITE(PRB0_HEAD, 0);

		DRM_ERROR("Ring head forced to zero "
			  "ctl %08x head %08x tail %08x start %08x\n",
			  I915_READ(PRB0_CTL),
			  I915_READ(PRB0_HEAD),
			  I915_READ(PRB0_TAIL),
			  I915_READ(PRB0_START));
	}

	I915_WRITE(PRB0_CTL,
		   ((obj->size - 4096) & RING_NR_PAGES) |
		   RING_REPORT_64K |
		   RING_VALID);

	head = I915_READ(PRB0_HEAD) & HEAD_ADDR;

	/* If the head is still not zero, the ring is dead */
	if (head != 0) {
		DRM_ERROR("Ring initialization failed "
			  "ctl %08x head %08x tail %08x start %08x\n",
			  I915_READ(PRB0_CTL),
			  I915_READ(PRB0_HEAD),
			  I915_READ(PRB0_TAIL),
			  I915_READ(PRB0_START));
		return -EIO;
	}

	/* Update our cache of the ring state */
	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		i915_kernel_lost_context(dev);
	else {
		ring->head = I915_READ(PRB0_HEAD) & HEAD_ADDR;
		ring->tail = I915_READ(PRB0_TAIL) & TAIL_ADDR;
		ring->space = ring->head - (ring->tail + 8);
		if (ring->space < 0)
			ring->space += ring->Size;
	}

	if (IS_I9XX(dev) && !IS_GEN3(dev)) {
		mode = VS_TIMER_DISPATCH << 16 | VS_TIMER_DISPATCH;
		if (IS_GEN6(dev))
			mode |= MI_FLUSH_ENABLE << 16 | MI_FLUSH_ENABLE;
		I915_WRITE(MI_MODE, mode);
	}

	return 0;
}

void
i915_gem_cleanup_ringbuffer(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;

	if (dev_priv->ring.ring_obj == NULL)
		return;

	drm_core_ioremapfree(&dev_priv->ring.map, dev);

	i915_gem_object_unpin(dev_priv->ring.ring_obj);
	drm_gem_object_unreference(dev_priv->ring.ring_obj);
	dev_priv->ring.ring_obj = NULL;
	(void) memset(&dev_priv->ring, 0, sizeof(dev_priv->ring));

	i915_gem_cleanup_hws(dev);
}

int
/* LINTED */
i915_gem_entervt_ioctl(DRM_IOCTL_ARGS)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int ret;

	if (drm_core_check_feature(dev, DRIVER_MODESET))
		return 0;

	if (atomic_read(&dev_priv->mm.wedged)) {
		DRM_ERROR("Reenabling wedged hardware, good luck\n");
		atomic_set(&dev_priv->mm.wedged, 0);
	}

	mutex_lock(&dev->struct_mutex);
	dev_priv->mm.suspended = 0;

	ret = i915_gem_init_ringbuffer(dev);
	if (ret != 0) {
		mutex_unlock(&dev->struct_mutex);
		return ret;
	}

	mutex_unlock(&dev->struct_mutex);

	(void) drm_irq_install(dev);

	return 0;
}

int
/* LINTED */
i915_gem_leavevt_ioctl(DRM_IOCTL_ARGS)
{
	/* LINTED */
	drm_i915_private_t *dev_priv = dev->dev_private;
	int ret;

	if (drm_core_check_feature(dev, DRIVER_MODESET))
		return 0;

	ret = i915_gem_idle(dev, 0);
	(void) drm_irq_uninstall(dev);

	return ret;
}

void
i915_gem_lastclose(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int ret;

	if (drm_core_check_feature(dev, DRIVER_MODESET))
		return;

	ret = i915_gem_idle(dev, 1);
	if (ret)
		DRM_ERROR("failed to idle hardware: %d\n", ret);

	drm_mm_clean_ml(&dev_priv->mm.gtt_space);
}

void
i915_gem_load(struct drm_device *dev)
{
	int i;
	drm_i915_private_t *dev_priv = dev->dev_private;

	spin_lock_init(&dev_priv->mm.active_list_lock);
	INIT_LIST_HEAD(&dev_priv->mm.active_list);
	INIT_LIST_HEAD(&dev_priv->mm.flushing_list);
	INIT_LIST_HEAD(&dev_priv->mm.inactive_list);
	INIT_LIST_HEAD(&dev_priv->mm.request_list);
	INIT_LIST_HEAD(&dev_priv->mm.fence_list);
	dev_priv->mm.next_gem_seqno = 1;

	/* Old X drivers will take 0-2 for front, back, depth buffers */
	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		dev_priv->fence_reg_start = 3;

	if (IS_I965G(dev) || IS_I945G(dev) || IS_I945GM(dev) || IS_G33(dev))
		dev_priv->num_fence_regs = 16;
	else
		dev_priv->num_fence_regs = 8;

	/* Initialize fence registers to zero */
	if (IS_I965G(dev)) {
		for (i = 0; i < 16; i++)
			I915_WRITE64(FENCE_REG_965_0 + (i * 8), 0);
	} else {
		for (i = 0; i < 8; i++)
			I915_WRITE(FENCE_REG_830_0 + (i * 4), 0);
		if (IS_I945G(dev) || IS_I945GM(dev) || IS_G33(dev))
			for (i = 0; i < 8; i++)
				I915_WRITE(FENCE_REG_945_8 + (i * 4), 0);
	}

	i915_gem_detect_bit_6_swizzle(dev);
	DRM_INIT_WAITQUEUE(&dev_priv->pending_flip_queue, DRM_INTR_PRI(dev));
}

/*
 * Create a physically contiguous memory object for this object
 * e.g. for cursor + overlay regs
 */
int i915_gem_init_phys_object(struct drm_device *dev,
			      int id, int size)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_phys_object *phys_obj;
	int ret;

	if (dev_priv->mm.phys_objs[id - 1] || !size)
		return 0;

	phys_obj = kzalloc(sizeof(struct drm_i915_gem_phys_object), GFP_KERNEL);
	if (!phys_obj)
		return -ENOMEM;

	phys_obj->id = id;

	phys_obj->handle = drm_pci_alloc(dev, size, 0, 0xffffffff, 1);
	if (!phys_obj->handle) {
		ret = -ENOMEM;
		goto kfree_obj;
	}

	dev_priv->mm.phys_objs[id - 1] = phys_obj;

	return 0;
kfree_obj:
	kfree(phys_obj, sizeof (struct drm_i915_gem_phys_object));
	return ret;
}

void i915_gem_free_phys_object(struct drm_device *dev, int id)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_phys_object *phys_obj;

	if (!dev_priv->mm.phys_objs[id - 1])
		return;

	phys_obj = dev_priv->mm.phys_objs[id - 1];
	if (phys_obj->cur_obj) {
		i915_gem_detach_phys_object(dev, phys_obj->cur_obj);
	}

	drm_pci_free(phys_obj->handle);
	kfree(phys_obj, sizeof (struct drm_i915_gem_phys_object));
	dev_priv->mm.phys_objs[id - 1] = NULL;
}

void i915_gem_free_all_phys_object(struct drm_device *dev)
{
	int i;

	for (i = I915_GEM_PHYS_CURSOR_0; i <= I915_MAX_PHYS_OBJECT; i++)
		i915_gem_free_phys_object(dev, i);
}

void i915_gem_detach_phys_object(struct drm_device *dev,
				 struct drm_gem_object *obj)
{
	struct drm_i915_gem_object *obj_priv;
	int i;
	int ret;
	int page_count;

	obj_priv = to_intel_bo(obj);
	if (!obj_priv->phys_obj)
		return;

	ret = i915_gem_object_get_pages(obj);
	if (ret)
		goto out;

	page_count = obj->size / PAGE_SIZE;

	for (i = 0; i < page_count; i++) {
		char *dst = obj_priv->page_list[i];
		char *src = (caddr_t)(obj_priv->phys_obj->handle->vaddr + (i * PAGE_SIZE));

		(void) memcpy(dst, src, PAGE_SIZE);
	}
	drm_clflush_pages(obj_priv->page_list, page_count);
	drm_agp_chipset_flush(dev);

	i915_gem_object_put_pages(obj);
out:
	obj_priv->phys_obj->cur_obj = NULL;
	obj_priv->phys_obj = NULL;
}

int
i915_gem_attach_phys_object(struct drm_device *dev,
			    struct drm_gem_object *obj, int id)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj_priv;
	int ret = 0;
	int page_count;
	int i;

	if (id > I915_MAX_PHYS_OBJECT)
		return -EINVAL;

	obj_priv = to_intel_bo(obj);

	if (obj_priv->phys_obj) {
		if (obj_priv->phys_obj->id == id)
			return 0;
		i915_gem_detach_phys_object(dev, obj);
	}


	/* create a new object */
	if (!dev_priv->mm.phys_objs[id - 1]) {
		ret = i915_gem_init_phys_object(dev, id,
						obj->size);
		if (ret) {
			DRM_ERROR("failed to init phys object %d size: %lu\n", id, obj->size);
			goto out;
		}
	}

	/* bind to the object */
	obj_priv->phys_obj = dev_priv->mm.phys_objs[id - 1];
	obj_priv->phys_obj->cur_obj = obj;

	ret = i915_gem_object_get_pages(obj);
	if (ret) {
		DRM_ERROR("failed to get page list\n");
		goto out;
	}

	page_count = obj->size / PAGE_SIZE;

	for (i = 0; i < page_count; i++) {
                char *dst = obj_priv->page_list[i];
                char *src = (caddr_t)(obj_priv->phys_obj->handle->vaddr + (i * PAGE_SIZE));
                (void) memcpy(dst, src, PAGE_SIZE);

	}

	i915_gem_object_put_pages(obj);

	return 0;
out:
	return ret;
}

static int
i915_gem_phys_pwrite(struct drm_device *dev, struct drm_gem_object *obj,
		     struct drm_i915_gem_pwrite *args,
		     /* LINTED */
		     struct drm_file *file_priv)
{
	struct drm_i915_gem_object *obj_priv = obj->driver_private;
	void *obj_addr;
	int ret;
	char __user *user_data;

	user_data = (char __user *) (uintptr_t) args->data_ptr;
	obj_addr = (void *)(uintptr_t)(obj_priv->phys_obj->handle->vaddr + args->offset);

	DRM_DEBUG("obj_addr %p, %ld\n", obj_addr, args->size);
	ret = DRM_COPY_FROM_USER(obj_addr, user_data, args->size);
	if (ret)
		return -EFAULT;

	drm_agp_chipset_flush(dev);
	return 0;
}

/* LINTED */
void i915_gem_release(struct drm_device * dev, struct drm_file *file_priv)
{
}
