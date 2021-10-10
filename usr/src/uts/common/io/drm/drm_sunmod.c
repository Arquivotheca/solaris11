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

/*
 * Common misc module interfaces of DRM under Solaris
 */

/*
 * This module calls into gfx and agpmaster misc modules respectively
 * for generic graphics operations and AGP master device support.
 */

#include "drm_sunmod.h"
#include "drm_sun_idr.h"
#include <sys/modctl.h>
#include <sys/kmem.h>
#include <vm/seg_kmem.h>

int drm_debug_flag = 0;

/* Identifier of this driver */
static struct vis_identifier text_ident = { "SUNWdrm" };

static ddi_device_acc_attr_t dev_attr = {
	DDI_DEVICE_ATTR_V0,
	DDI_NEVERSWAP_ACC,
	DDI_STRICTORDER_ACC,
};

static ddi_device_acc_attr_t gem_dev_attr = {
	DDI_DEVICE_ATTR_V0,
	DDI_NEVERSWAP_ACC,
	DDI_MERGING_OK_ACC
};

extern int __init drm_core_init(void);
extern void __exit drm_core_exit(void);
extern int drm_get_pci_index_reg(dev_info_t *, uint_t, uint_t, off_t *);

struct find_gem_object {
	offset_t offset;
	struct drm_gem_object *obj;
};

static int
drm_devmap_map(devmap_cookie_t dhc, dev_t dev_id, uint_t flags,
    offset_t offset, size_t len, void **new_priv)
{
	_NOTE(ARGUNUSED(offset, len))

	devmap_handle_t *dhp;
	struct ddi_umem_cookie 	*cp;
	struct drm_minor *minor;
	struct drm_device *dev;
	int minor_id;

	minor_id = DRM_DEV2MINOR(dev_id);
	minor = idr_find(&drm_minors_idr, minor_id);
	if (!minor)
		return (ENODEV);
	dev = minor->dev;

	/*
	 * This driver only supports MAP_SHARED,
	 * and doesn't support MAP_PRIVATE
	 */
	if (flags & MAP_PRIVATE) {
		DRM_ERROR("Not support MAP_PRIVATE");
		return (EINVAL);
	}

	mutex_enter(&dev->struct_mutex);
	dhp = (devmap_handle_t *)dhc;
	cp = (struct ddi_umem_cookie *)dhp->dh_cookie;
	cp->cook_refcnt = 1;
	mutex_exit(&dev->struct_mutex);

	*new_priv = dev;
	return (0);
}

static int
drm_devmap_dup(devmap_cookie_t dhc, void *pvtp, devmap_cookie_t new_dhc,
    void **new_pvtp)
{
	_NOTE(ARGUNUSED(new_dhc))

	struct drm_device *dev = (struct drm_device *)pvtp;
	devmap_handle_t *dhp;
	struct ddi_umem_cookie *cp;

	mutex_enter(&dev->struct_mutex);
	dhp = (devmap_handle_t *)dhc;
	cp = (struct ddi_umem_cookie *)dhp->dh_cookie;
	cp->cook_refcnt++;
	mutex_exit(&dev->struct_mutex);

	*new_pvtp = dev;
	return (0);
}

static void
drm_devmap_unmap(devmap_cookie_t dhc, void *pvtp, offset_t off, size_t len,
    devmap_cookie_t new_dhp1, void **new_pvtp1, devmap_cookie_t new_dhp2,
    void **new_pvtp2)
{
	_NOTE(ARGUNUSED(off, len))

	struct drm_device *dev;
	devmap_handle_t *dhp;
	devmap_handle_t	*ndhp;
	struct ddi_umem_cookie	*cp;
	struct ddi_umem_cookie	*ncp;

	dhp = (devmap_handle_t *)dhc;
	dev = (struct drm_device *)pvtp;

	mutex_enter(&dev->struct_mutex);

	cp = (struct ddi_umem_cookie *)dhp->dh_cookie;
	if (new_dhp1 != NULL) {
		ndhp = (devmap_handle_t *)new_dhp1;
		ncp = (struct ddi_umem_cookie *)ndhp->dh_cookie;
		ncp->cook_refcnt++;
		*new_pvtp1 = dev;
		ASSERT(ncp == cp);
	}

	if (new_dhp2 != NULL) {
		ndhp = (devmap_handle_t *)new_dhp2;
		ncp = (struct ddi_umem_cookie *)ndhp->dh_cookie;
		ncp->cook_refcnt++;
		*new_pvtp2 = dev;
		ASSERT(ncp == cp);
	}

	/* FIXME: dh_cookie should not be released here. */
#if 0
	cp->cook_refcnt--;
	if (cp->cook_refcnt == 0) {
		gfxp_umem_cookie_destroy(dhp->dh_cookie);
		dhp->dh_cookie = NULL;
	}
#endif

	mutex_exit(&dev->struct_mutex);
}

static struct devmap_callback_ctl drm_devmap_callbacks = {
	DEVMAP_OPS_REV, 		/* devmap_rev */
	drm_devmap_map,			/* devmap_map */
	NULL,				/* devmap_access */
	drm_devmap_dup,			/* devmap_dup */
	drm_devmap_unmap 		/* devmap_unmap */
};

static struct drm_local_map *
__find_local_map(struct drm_device *dev, offset_t offset)
{
	struct drm_map_list *entry;

	entry = idr_find(&dev->map_idr, offset >> PAGE_SHIFT);
	if (entry)
		return (entry->map);

	return (NULL);
}

static int
__devmap_general(struct drm_device *dev, devmap_cookie_t dhp,
    struct drm_local_map *map, size_t len, size_t *maplen)
{
	off_t regoff;
	int regno, ret;

	regno = drm_get_pci_index_reg(dev->devinfo,
	    map->offset, (uint_t)len, &regoff);
	if (regno < 0) {
		DRM_ERROR("drm_get_pci_index_reg() failed");
		return (-EINVAL);
	}

	ret = devmap_devmem_setup(dhp, dev->devinfo, NULL,
	    regno, (offset_t)regoff, len, PROT_ALL,
	    0, &dev_attr);
	if (ret != DDI_SUCCESS) {
		DRM_ERROR("devmap_devmem_setup failed, ret=%d", ret);
		return (-EFAULT);
	}

	*maplen = len;
	return (0);
}

static int
__devmap_shm(struct drm_device *dev, devmap_cookie_t dhp,
    struct drm_local_map *map, size_t len, size_t *maplen)
{
	int ret;

	if (!map->umem_cookie)
		return (-EINVAL);

	len = ptob(btopr(map->size));

	ret = devmap_umem_setup(dhp, dev->devinfo,
	    NULL, map->umem_cookie, 0, len, PROT_ALL,
	    IOMEM_DATA_CACHED, NULL);
	if (ret != DDI_SUCCESS) {
		DRM_ERROR("devmap_umem_setup failed, ret=%d", ret);
		return (-EFAULT);
	}

	*maplen = len;
	return (0);
}

static int
__devmap_agp(struct drm_device *dev, devmap_cookie_t dhp,
    struct drm_local_map *map, size_t len, size_t *maplen)
{
	int ret;

	if (dev->agp == NULL) {
		DRM_ERROR("attempted to mmap AGP"
		    "memory before AGP support is enabled");
		return (-ENODEV);
	}

	len = ptob(btopr(len));

	ret = devmap_umem_setup(dhp, dev->devinfo,
	    &drm_devmap_callbacks, map->umem_cookie, 0, len, PROT_ALL,
	    IOMEM_DATA_UNCACHED | DEVMAP_ALLOW_REMAP, &dev_attr);
	if (ret != DDI_SUCCESS) {
		DRM_ERROR("devmap_umem_setup() failed, ret=%d", ret);
		return (-EFAULT);
	}

	*maplen = len;
	return (0);
}

static int
__devmap_sg(struct drm_device *dev, devmap_cookie_t dhp,
    struct drm_local_map *map, size_t len, size_t *maplen)
{
	int ret;

	len = ptob(btopr(len));
	if (len > map->size) {
		DRM_ERROR("offset=0x%lx, virtual=0x%p, "
		    "mapsize=0x%lx, len=0x%lx",
		    map->offset, dev->sg->virtual, map->size, len);
		return (-EINVAL);
	}

	ret = devmap_umem_setup(dhp, dev->devinfo,
	    &drm_devmap_callbacks, map->umem_cookie, 0, len, PROT_ALL,
	    IOMEM_DATA_UNCACHED | DEVMAP_ALLOW_REMAP, &dev_attr);
	if (ret != DDI_SUCCESS) {
		DRM_ERROR("devmap_umem_setup() fail");
		return (-EFAULT);
	}

	*maplen = len;
	return (0);
}

static int
__devmap_gem(struct drm_device *dev, devmap_cookie_t dhp,
    struct drm_local_map *map, size_t *maplen)
{
	int ret;

	if (!map->umem_cookie)
		return (-EINVAL);

	ret = gfxp_devmap_umem_setup(dhp, dev->devinfo, NULL,
	    map->umem_cookie, 0, map->size, PROT_ALL,
	    IOMEM_DATA_UC_WR_COMBINE | DEVMAP_ALLOW_REMAP, &gem_dev_attr);
	if (ret != DDI_SUCCESS) {
		DRM_ERROR("gfxp_devmap_umem_setup failed, ret=%d", ret);
		return (-EFAULT);
	}

	*maplen = map->size;
	return (0);
}

static int
drm_sun_open(dev_t *dev_id, int flag, int otyp, cred_t *credp)
{
	_NOTE(ARGUNUSED(otyp))

	int minor_id = DRM_DEV2MINOR(*dev_id);
	struct drm_minor *minor;
	int clone_id;
	int ret;

	minor = idr_find(&drm_minors_idr, minor_id);
	if (!minor)
		return (ENODEV);
	if (!minor->dev)
		return (ENODEV);

	/*
	 * No operations for VGA & AGP mater devices, always return OK.
	 */
	if (DRM_MINOR_IS_VGATEXT(minor_id))
		return (0);

	if (DRM_MINOR_IS_AGPMASTER(minor_id))
		return (0);

	/*
	 * Drm driver implements a software lock to serialize access
	 * to graphics hardware based on per-process granulation. Before
	 * operating graphics hardware, all clients, including kernel
	 * and applications, must acquire this lock via DRM_IOCTL_LOCK
	 * ioctl, and release it via DRM_IOCTL_UNLOCK after finishing
	 * operations. Drm driver will grant r/w permission to the
	 * process which acquires this lock (Kernel is assumed to have
	 * process ID 0).
	 *
	 * A process might be terminated without releasing drm lock, in
	 * this case, drm driver is responsible for clearing the holding.
	 * To be informed of process exiting, drm driver uses clone open
	 * to guarantee that each call to open(9e) have one corresponding
	 * call to close(9e). In most cases, a process will close drm
	 * during process termination, so that drm driver could have a
	 * chance to release drm lock.
	 *
	 * In fact, a driver cannot know exactly when a process exits.
	 * Clone open doesn't address this issue completely: Because of
	 * inheritance, child processes inherit file descriptors from
	 * their parent. As a result, if the parent exits before its
	 * children, drm close(9e) entrypoint won't be called until all
	 * of its children terminate.
	 *
	 * Another issue brought up by inhertance is the process PID
	 * that calls the drm close() entry point may not be the same
	 * as the one who called open(). Per-process struct is allocated
	 * when a process first open() drm, and released when the process
	 * last close() drm. Since open()/close() may be not the same
	 * process, PID cannot be used for key to lookup per-process
	 * struct. So, we associate minor number with per-process struct
	 * during open()'ing, and find corresponding process struct
	 * via minor number when close() is called.
	 */
	ret = idr_get_new_above(&minor->clone_idr, NULL, 0, &clone_id);
	if (ret)
		return (EMFILE);

	if (clone_id > DRM_CLONEID_MAX) {
		(void) idr_remove(&minor->clone_idr, clone_id);
		return (EMFILE);
	}

	ret = drm_open(minor, clone_id, flag, credp);
	if (ret) {
		(void) idr_remove(&minor->clone_idr, clone_id);
		return (-ret);
	}

	*dev_id = DRM_MAKEDEV(getmajor(*dev_id), minor_id, clone_id);

	return (-ret);
}

static int
drm_sun_close(dev_t dev_id, int flag, int otyp, cred_t *credp)
{
	_NOTE(ARGUNUSED(flag, otyp, credp))

	struct drm_minor *minor;
	struct drm_file *file_priv;
	int minor_id = DRM_DEV2MINOR(dev_id);
	int clone_id = DRM_DEV2CLONEID(dev_id);
	int ret = 0;

	minor = idr_find(&drm_minors_idr, minor_id);
	if (!minor)
		return (ENODEV);
	if (!minor->dev)
		return (ENODEV);

	/*
	 * No operations for VGA & AGP mater devices, always return OK.
	 */
	if (DRM_MINOR_IS_VGATEXT(minor_id))
		return (0);

	if (DRM_MINOR_IS_AGPMASTER(minor_id))
		return (0);

	file_priv = idr_find(&minor->clone_idr, clone_id);
	if (!file_priv)
		return (EBADF);

	ret = drm_release(file_priv);
	if (ret)
		return (-ret);

	(void) idr_remove(&minor->clone_idr, clone_id);

	return (0);
}

static int
drm_sun_ioctl(dev_t dev_id, int cmd, intptr_t arg, int mode, cred_t *credp,
    int *rvalp)
{
	struct drm_minor *minor;
	struct drm_file *file_priv;
	int minor_id = DRM_DEV2MINOR(dev_id);
	int clone_id = DRM_DEV2CLONEID(dev_id);

	minor = idr_find(&drm_minors_idr, minor_id);
	if (!minor)
		return (ENODEV);
	if (!minor->dev)
		return (ENODEV);

	if (cmd == VIS_GETIDENTIFIER) {
		if (ddi_copyout(&text_ident, (void *)arg,
		    sizeof (struct vis_identifier), mode))
			return (EFAULT);
	}

	if (DRM_MINOR_IS_VGATEXT(minor_id))
		return (gfxp_vgatext_ioctl(dev_id, cmd, arg, mode, credp,
		    rvalp, minor->private));

	if (DRM_MINOR_IS_AGPMASTER(minor_id))
		return (agpmaster_ioctl(dev_id, cmd, arg, mode, credp,
		    rvalp, minor->private));

	file_priv = idr_find(&minor->clone_idr, clone_id);
	if (!file_priv)
		return (EBADF);

	return (-(drm_ioctl(dev_id, file_priv, cmd, arg, mode, credp)));
}

static int
drm_sun_devmap(dev_t dev_id, devmap_cookie_t dhp, offset_t offset,
    size_t len, size_t *maplen, uint_t model)
{
	struct drm_device *dev;
	struct drm_minor *minor;
	struct drm_file *file_priv;
	int minor_id = DRM_DEV2MINOR(dev_id);
	int clone_id = DRM_DEV2CLONEID(dev_id);
	drm_local_map_t *map = NULL;

	minor = idr_find(&drm_minors_idr, minor_id);
	if (!minor)
		return (ENODEV);
	if (!minor->dev)
		return (ENODEV);

	dev = minor->dev;

	if (DRM_MINOR_IS_VGATEXT(minor_id))
		return (gfxp_vgatext_devmap(dev_id, dhp, offset, len,
		    maplen, model, minor->private));

	if (DRM_MINOR_IS_AGPMASTER(minor_id))
		return (ENOTSUP);

	file_priv = idr_find(&minor->clone_idr, clone_id);
	if (!file_priv)
		return (EBADF);

	mutex_enter(&dev->struct_mutex);
	map = __find_local_map(dev, offset);
	if (!map) {
		mutex_exit(&dev->struct_mutex);
		return (EFAULT);
	}
	if (map->flags & _DRM_RESTRICTED) {
		mutex_exit(&dev->struct_mutex);
		return (ENOTSUP);
	}
	mutex_exit(&dev->struct_mutex);

	switch (map->type) {
	case _DRM_FRAME_BUFFER:
	case _DRM_REGISTERS:
		return (__devmap_general(dev, dhp, map, len, maplen));

	case _DRM_SHM:
		return (__devmap_shm(dev, dhp, map, len, maplen));

	case _DRM_AGP:
		return (__devmap_agp(dev, dhp, map, len, maplen));

	case _DRM_SCATTER_GATHER:
		return (__devmap_sg(dev, dhp, map, len, maplen));

	case _DRM_GEM:
		return (__devmap_gem(dev, dhp, map, maplen));
	}

	return (ENOTSUP);
}

static int
drm_sun_read(dev_t dev_id, struct uio *uiop, cred_t *credp)
{
	_NOTE(ARGUNUSED(credp))

	struct drm_minor *minor;
	struct drm_file *file_priv;
	int minor_id = DRM_DEV2MINOR(dev_id);
	int clone_id = DRM_DEV2CLONEID(dev_id);

	minor = idr_find(&drm_minors_idr, minor_id);
	if (!minor)
		return (ENODEV);
	if (!minor->dev)
		return (ENODEV);

	/*
	 * No operations for VGA & AGP master devices, always return OK.
	 */
	if (DRM_MINOR_IS_VGATEXT(minor_id))
		return (0);

	if (DRM_MINOR_IS_AGPMASTER(minor_id))
		return (0);

	file_priv = idr_find(&minor->clone_idr, clone_id);
	if (!file_priv)
		return (EBADF);

	(void) drm_read(file_priv, uiop);
	return (0);
}

static int
drm_sun_chpoll(dev_t dev_id, short events, int anyyet, short *reventsp,
    struct pollhead **phpp)
{
	_NOTE(ARGUNUSED(anyyet, phpp))

	struct drm_minor *minor;
	struct drm_file *file_priv;
	int minor_id = DRM_DEV2MINOR(dev_id);
	int clone_id = DRM_DEV2CLONEID(dev_id);

	minor = idr_find(&drm_minors_idr, minor_id);
	if (!minor)
		return (ENODEV);
	if (!minor->dev)
		return (ENODEV);

	/*
	 * No operations for VGA & AGP master devices, always return OK.
	 */
	if (DRM_MINOR_IS_VGATEXT(minor_id))
		return (0);

	if (DRM_MINOR_IS_AGPMASTER(minor_id))
		return (0);

	file_priv = idr_find(&minor->clone_idr, clone_id);
	if (!file_priv)
		return (EBADF);

	*reventsp = drm_poll(file_priv, events);
	return (0);
}

/*
 * Common device operations structure for all DRM drivers
 */
struct cb_ops drm_cb_ops = {
	drm_sun_open,			/* cb_open */
	drm_sun_close,			/* cb_close */
	nodev,				/* cb_strategy */
	nodev,				/* cb_print */
	nodev,				/* cb_dump */
	drm_sun_read,			/* cb_read */
	nodev,				/* cb_write */
	drm_sun_ioctl,			/* cb_ioctl */
	drm_sun_devmap,			/* cb_devmap */
	nodev,				/* cb_mmap */
	NULL,				/* cb_segmap */
	drm_sun_chpoll,			/* cb_chpoll */
	ddi_prop_op,			/* cb_prop_op */
	0,				/* cb_stream */
	D_NEW | D_MTSAFE | D_DEVMAP	/* cb_flag */
};

static struct modlmisc modlmisc = {
	&mod_miscops, "DRM common interfaces"
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modlmisc, NULL
};

int
_init(void)
{
	int ret;

	ret = mod_install(&modlinkage);
	if (ret)
		return (ret);

	return (drm_core_init());
}

int
_fini(void)
{
	int ret;

	ret = mod_remove(&modlinkage);
	if (ret)
		return (ret);

	drm_core_exit();

	return (0);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

int
drm_core_check_feature(struct drm_device *dev, int feature)
{
	return ((dev->driver->driver_features & feature) ? 1 : 0);
}

int
drm_core_has_AGP(struct drm_device *dev)
{
	return (drm_core_check_feature(dev, DRIVER_USE_AGP));
}

void
drm_gem_object_reference(struct drm_gem_object *obj)
{
	atomic_inc(&obj->refcount);
}

void
drm_gem_object_unreference(struct drm_gem_object *obj)
{
	if (obj == NULL)
		return;

	atomic_sub(1, &obj->refcount);
	if (obj->refcount == 0)
		drm_gem_object_free(obj);
}

void
drm_gem_object_handle_reference(struct drm_gem_object *obj)
{
	drm_gem_object_reference(obj);
	atomic_inc(&obj->handlecount);
}

void
drm_gem_object_handle_unreference(struct drm_gem_object *obj)
{
	if (obj == NULL)
		return;

	/*
	 * Must bump handle count first as this may be the last
	 * ref, in which case the object would disappear before we
	 * checked for a name
	 */
	atomic_sub(1, &obj->handlecount);
	if (obj->handlecount == 0)
		drm_gem_object_handle_free(obj);
	drm_gem_object_unreference(obj);
}

void
drm_gem_object_unreference_unlocked(struct drm_gem_object *obj)
{
	if (obj != NULL)
		kref_put((struct kref *)&obj->refcount,
		    drm_gem_object_free_unlocked);
}

struct drm_local_map *
drm_core_findmap(struct drm_device *dev, unsigned int token)
{
	struct drm_map_list *_entry;

	list_for_each_entry(_entry, struct drm_map_list, &dev->maplist, head) {
		if (_entry->user_token == token)
			return (_entry->map);
	}

	return (NULL);
}

int
drm_device_is_agp(struct drm_device *dev)
{
	if (dev->driver->device_is_agp != NULL) {
		int err = (*dev->driver->device_is_agp) (dev);

		if (err != DRM_MIGHT_BE_AGP) {
			return (err);
		}
	}

	return (pci_find_capability(dev->pdev, PCIY_AGP));
}

int
drm_device_is_pcie(struct drm_device *dev)
{
	return (pci_find_capability(dev->pdev, PCIY_EXPRESS));
}
