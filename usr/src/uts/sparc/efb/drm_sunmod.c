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
 * the efb driver for sparc currently builds this into the efb module
 * instead of as a separately loadable misc module like x86 does.
 */

#include "drm_sunmod.h"
#include <sys/modctl.h>
#include <sys/kmem.h>
#include <vm/seg_kmem.h>

/* Identifier of this driver */
static const struct vis_identifier text_ident = { "SUNWdrm" };

static drm_inst_list_t	*drm_inst_head;
static kmutex_t	drm_inst_list_lock;

static int drm_sun_open(dev_t *, int, int, cred_t *);
static int drm_sun_close(dev_t, int, int, cred_t *);
static int drm_sun_ioctl(dev_t, int, intptr_t, int, cred_t *, int *);
static int drm_sun_devmap(dev_t, devmap_cookie_t, offset_t, size_t,
    size_t *, uint_t);

#ifdef __x86
/*
 * devmap callbacks for AGP and PCI GART
 */
static int drm_devmap_map(devmap_cookie_t, dev_t,
    uint_t, offset_t, size_t, void **);
static int drm_devmap_dup(devmap_cookie_t, void *,
    devmap_cookie_t, void **);
static void drm_devmap_unmap(devmap_cookie_t, void *,
    offset_t, size_t, devmap_cookie_t, void **, devmap_cookie_t, void **);

static struct devmap_callback_ctl drm_devmap_callbacks = {
		DEVMAP_OPS_REV,			/* devmap_rev */
		drm_devmap_map,				/* devmap_map */
		NULL,			/* devmap_access */
		drm_devmap_dup,			/* devmap_dup */
		drm_devmap_unmap		/* devmap_unmap */
};
#endif

static drm_inst_list_t *drm_supp_alloc_drv_entry(dev_info_t *);
extern drm_inst_state_t *drm_sup_devt_to_state(dev_t);
static void drm_supp_free_drv_entry(dev_info_t *);


/*
 * Common device operations structure for all DRM drivers
 */
struct cb_ops drm_cb_ops = {
	drm_sun_open,			/* cb_open */
	drm_sun_close,			/* cb_close */
	nodev,					/* cb_strategy */
	nodev,					/* cb_print */
	nodev,					/* cb_dump */
	nodev,					/* cb_read */
	nodev,					/* cb_write */
	drm_sun_ioctl,		/* cb_ioctl */
	drm_sun_devmap,		/* cb_devmap */
	nodev,					/* cb_mmap */
	NULL,			/* cb_segmap */
	nochpoll,				/* cb_chpoll */
	ddi_prop_op,		/* cb_prop_op */
	0,					/* cb_stream */
	D_NEW | D_MTSAFE |D_DEVMAP	/* cb_flag */
};


void *
drm_supp_register(dev_info_t *dip, drm_device_t *dp)
{
	int		error;
	char		buf[16];
	int		instance = ddi_get_instance(dip);
	ddi_acc_handle_t	pci_cfg_handle;
	drm_inst_state_t	*mstate;
	drm_inst_list_t		*entry;

	ASSERT(dip != NULL);

	entry = drm_supp_alloc_drv_entry(dip);
	if (entry == NULL) {
		cmn_err(CE_WARN, "drm_supp_register: failed to get softstate");
		return (NULL);
	}
	mstate = &entry->disl_state;

	/* create a minor node for common graphics ops */
	(void) snprintf(buf, sizeof (buf), "%s%d", "efb", instance);

	error = ddi_create_minor_node(dip, buf, S_IFCHR,
	    INST2NODE0(instance), DDI_NT_DISPLAY, NULL);
	if (error != DDI_SUCCESS) {
		DRM_ERROR("drm_supp_register: "
		    "failed to create minor node for gfx");
		goto exit2;
	}

	/* setup mapping for later PCI config space access */
	error = pci_config_setup(dip, &pci_cfg_handle);
	if (error != DDI_SUCCESS) {
		DRM_ERROR("drm_supp_register: "
		    "PCI configuration space setup failed");
		goto exit2;
	}

	mutex_enter(&mstate->mis_lock);
	mstate->mis_major = ddi_driver_major(dip);
	mstate->mis_dip = dip;
	mstate->mis_gfxp = NULL;
	mstate->mis_agpm = NULL;
	mstate->mis_cfg_hdl = pci_cfg_handle;
	mstate->mis_devp = dp;
	mutex_exit(&mstate->mis_lock);

	return ((void *)mstate);

exit3:
	pci_config_teardown(&pci_cfg_handle);
exit2:
	drm_supp_free_drv_entry(dip);
	ddi_remove_minor_node(dip, NULL);

	return (NULL);
}


int
drm_supp_unregister(void *handle)
{
	drm_inst_list_t		*list;
	drm_inst_state_t	*mstate;

	list = (drm_inst_list_t *)handle;
	mstate = &list->disl_state;

	mutex_enter(&mstate->mis_lock);

	/* free PCI config access handle */
	if (mstate->mis_cfg_hdl)
		pci_config_teardown(&mstate->mis_cfg_hdl);

	mstate->mis_devp = NULL;

	/* remove all minor nodes */
	ddi_remove_minor_node(mstate->mis_dip, NULL);
	mutex_exit(&mstate->mis_lock);
	drm_supp_free_drv_entry(mstate->mis_dip);

	return (DDI_SUCCESS);
}


static int
drm_sun_open(dev_t *devp, int flag, int otyp, cred_t *credp)
{
	drm_inst_state_t	*mstate;
	drm_cminor_t	*mp, *newp;
	drm_device_t	*dp;
	minor_t		minor;
	int		newminor;
	int		instance;
	int		err;


	mstate = drm_sup_devt_to_state(*devp);
	/*
	 * return ENXIO for deferred attach so that system can
	 * attach us again.
	 */
	if (mstate == NULL)
		return (ENXIO);

	/*
	 * The least significant 15 bits are used for minor_number, and
	 * the mid 3 bits are used for instance number. All minor numbers
	 * are used as follows:
	 * 0 -- gfx
	 * 1 -- agpmaster
	 * 2 -- drm
	 * (3, MAX_CLONE_MINOR) -- drm minor node for clone open.
	 */
	minor = DEV2MINOR(*devp);
	instance = DEV2INST(*devp);

	ASSERT(minor <= MAX_CLONE_MINOR);

#ifdef __x86
	/*
	 * No operations for VGA & AGP master devices, always return OK.
	 */
	if ((minor == GFX_MINOR) || (minor == AGPMASTER_MINOR))
		return (0);
#endif

	/*
	 * From here, we start to process drm
	 */

	dp = mstate->mis_devp;

	if (!dp)
		return (ENXIO);

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
	 * Another issue brought up by inheritance is the process PID
	 * that calls the drm close() entry point may not be the same
	 * as the one who called open(). Per-process struct is allocated
	 * when a process first open() drm, and released when the process
	 * last close() drm. Since open()/close() may be not the same
	 * process, PID cannot be used for key to lookup per-process
	 * struct. So, we associate minor number with per-process struct
	 * during open()'ing, and find corresponding process struct
	 * via minor number when close() is called.
	 */
	newp = kmem_zalloc(sizeof (drm_cminor_t), KM_SLEEP);
	mutex_enter(&dp->dev_lock);
	for (newminor = DRM_MIN_CLONEMINOR; newminor < MAX_CLONE_MINOR;
	    newminor ++) {
		TAILQ_FOREACH(mp, &dp->minordevs, link) {
			if (mp->minor == newminor)
				break;
		}
		if (mp == NULL)
			goto gotminor;
	}

	mutex_exit(&dp->dev_lock);
	return (EMFILE);

gotminor:
	TAILQ_INSERT_TAIL(&dp->minordevs, newp, link);
	newp->minor = newminor;
	mutex_exit(&dp->dev_lock);
	err = drm_open(dp, newp, flag, otyp, credp);
	if (err) {
		mutex_enter(&dp->dev_lock);
		TAILQ_REMOVE(&dp->minordevs, newp, link);
		(void) kmem_free(newp, sizeof (drm_cminor_t));
		mutex_exit(&dp->dev_lock);

		return (err);
	}

	/* return a clone minor */
	newminor = newminor | (instance << NBITSMNODE);
	*devp = makedevice(getmajor(*devp), newminor);
	return (err);
}

static int
drm_sun_close(dev_t dev, int flag, int otyp, cred_t *credp)
{
	drm_inst_state_t	*mstate;
	drm_device_t		*dp;
	minor_t		minor;
	int		ret;

	mstate = drm_sup_devt_to_state(dev);
	if (mstate == NULL)
		return (EBADF);

	minor = DEV2MINOR(dev);
	ASSERT(minor <= MAX_CLONE_MINOR);
	if ((minor == GFX_MINOR) || (minor == AGPMASTER_MINOR))
		return (0);

	dp = mstate->mis_devp;
	if (dp == NULL) {
		DRM_ERROR("drm_sun_close: NULL soft state");
		return (ENXIO);
	}

	ret = drm_close(dp, minor, flag, otyp, credp);

	return (ret);
}

static int
drm_sun_ioctl(dev_t dev, int cmd, intptr_t arg, int mode,
    cred_t *credp, int *rvalp)
{
	_NOTE(ARGUNUSED(rvalp))

	extern drm_ioctl_desc_t drm_ioctls[];

	drm_inst_state_t	*mstate;
	drm_device_t		*dp;
	drm_ioctl_desc_t	*ioctl;
	drm_ioctl_t		*func;
	drm_file_t		*fpriv;
#ifdef __x86
	minor_t		minor;
#endif
	int		retval;
	int		nr;

	if (cmd == VIS_GETIDENTIFIER) {
		if (ddi_copyout(&text_ident, (void *)arg,
		    sizeof (struct vis_identifier), mode))
			return (EFAULT);
	}

	mstate = drm_sup_devt_to_state(dev);
	if (mstate == NULL) {
		return (EIO);
	}

#ifdef __x86
	minor = DEV2MINOR(dev);
	ASSERT(minor <= MAX_CLONE_MINOR);

	switch (minor) {
	case GFX_MINOR:
		retval = gfxp_vgatext_ioctl(dev, cmd, arg,
		    mode, credp, rvalp, mstate->mis_gfxp);
		return (retval);

	case AGPMASTER_MINOR:
		retval = agpmaster_ioctl(dev, cmd, arg, mode,
		    credp, rvalp, mstate->mis_agpm);
		return (retval);

	case DRM_MINOR:
	default:	/* DRM cloning minor nodes */
		break;
	}
#endif /* x86 */

	dp = mstate->mis_devp;
	ASSERT(dp != NULL);

	nr = DRM_IOCTL_NR(cmd);
	ioctl = &drm_ioctls[nr];
	atomic_inc_32(&dp->counts[_DRM_STAT_IOCTLS]);

	/* It's not a core DRM ioctl, try driver-specific. */
	if (ioctl->func == NULL && nr >= DRM_COMMAND_BASE) {
		/* The array entries begin at DRM_COMMAND_BASE ioctl nr */
		nr -= DRM_COMMAND_BASE;
		if (nr > dp->driver->max_driver_ioctl) {
			DRM_ERROR("Bad driver ioctl number, 0x%x (of 0x%x)",
			    nr, dp->driver->max_driver_ioctl);
			return (EINVAL);
		}
		ioctl = &dp->driver->driver_ioctls[nr];
	}

	func = ioctl->func;
	if (func == NULL) {
		return (ENOTSUP);
	}

	mutex_enter(&dp->dev_lock);
	fpriv = drm_find_file_by_proc(dp, credp);
	mutex_exit(&dp->dev_lock);
	if (fpriv == NULL) {
		DRM_ERROR("drm_sun_ioctl : can't find authenticator");
		return (EACCES);
	}

	if (((ioctl->flags & DRM_ROOT_ONLY) && !DRM_SUSER(credp)) ||
	    ((ioctl->flags & DRM_AUTH) && !fpriv->authenticated) ||
	    ((ioctl->flags & DRM_MASTER) && !fpriv->master))
		return (EACCES);

	retval = func(dp, arg, fpriv, mode);

	return (retval);
}

static int
drm_sun_devmap(dev_t dev, devmap_cookie_t dhp, offset_t offset,
    size_t len, size_t *maplen, uint_t model)
{
#ifndef __x86
	_NOTE(ARGUNUSED(model))
#endif

	drm_inst_state_t	*mstate;
	drm_device_t		*dp;
	drm_local_map_t		*map;
	u_offset_t		handle;
#ifdef __x86
	ddi_umem_cookie_t	cookie;
	unsigned long		aperbase;
	offset_t		koff;
	caddr_t			kva;
#endif
	minor_t			minor;
	size_t			length;
	int			ret;

	void *cb = NULL;

	static ddi_device_acc_attr_t dev_attr_le = {
		DDI_DEVICE_ATTR_V0,
		DDI_STRUCTURE_LE_ACC,
		DDI_STRICTORDER_ACC,
	};

	static ddi_device_acc_attr_t dev_attr_noswap = {
		DDI_DEVICE_ATTR_V0,
		DDI_NEVERSWAP_ACC,
		DDI_STRICTORDER_ACC,
	};

	ddi_device_acc_attr_t *dev_attr = NULL;

	mstate = drm_sup_devt_to_state(dev);
	if (mstate == NULL)
		return (ENXIO);

	minor = DEV2MINOR(dev);
	switch (minor) {
#ifdef __x86
	case GFX_MINOR:
		ret = gfxp_vgatext_devmap(dev, dhp, offset, len, maplen, model,
		    mstate->mis_gfxp);
		return (ret);

	case AGPMASTER_MINOR:
		return (ENOTSUP);
#endif /* x86 */

	case DRM_MINOR:
		break;

	default:
		/* DRM cloning nodes */
		if (minor > MAX_CLONE_MINOR)
			return (EBADF);
		break;
	}


	dp = mstate->mis_devp;
	if (dp == NULL) {
		DRM_ERROR("drm_sun_devmap: NULL soft state");
		return (EINVAL);
	}

	/*
	 * We will solve 32-bit application on 64-bit kernel
	 * issue later, now, we just use low 32-bit
	 */
	handle = (u_offset_t)offset;
	handle &= 0xffffffff;
	mutex_enter(&dp->dev_lock);
	TAILQ_FOREACH(map, &dp->maplist, link) {
		if (handle ==
		    ((u_offset_t)((uintptr_t)map->handle) & 0xffffffff))
			break;
	}

	/*
	 * Temporarily, because offset is phys_addr for register
	 * and framebuffer, is kernel virtual_addr for others
	 * Maybe we will use hash table to solve this issue later.
	 */
	if (map == NULL) {
		TAILQ_FOREACH(map, &dp->maplist, link) {
			if (handle == (map->offset & 0xffffffff))
				break;
		}
	}

	if (map == NULL) {
		u_offset_t	tmp;

		mutex_exit(&dp->dev_lock);
		cmn_err(CE_WARN, "Can't find map, offset=0x%llx, len=%x\n",
		    offset, (int)len);
		cmn_err(CE_WARN, "Current mapping:\n");
		TAILQ_FOREACH(map, &dp->maplist, link) {
		tmp = (u_offset_t)((uintptr_t)map->handle) & 0xffffffff;
		cmn_err(CE_WARN, "map(handle=0x%p, size=0x%lx,type=%d,"
		    "offset=0x%lx), handle=%llx, tmp=%lld", map->handle,
		    map->size, map->type, map->offset, handle, tmp);
		}
		return (-1);
	}

	if (map->flags & _DRM_RESTRICTED) {
		mutex_exit(&dp->dev_lock);
		cmn_err(CE_WARN, "restricted map\n");
		return (-1);
	}

	mutex_exit(&dp->dev_lock);
	switch (map->type) {

	case _DRM_REGISTERS:
#ifdef _BIG_ENDIAN
			dev_attr = &dev_attr_le;
#else
			dev_attr = &dev_attr_noswap;
#endif /* _BIG_ENDIAN */

			if (dp->driver->set_devmap_callbacks) {
				cb = (dp->driver->set_devmap_callbacks)
				    (map->type);
			}
			/*FALLTHROUGH*/

	case _DRM_FRAME_BUFFER:
		{
			int	regno;
			off_t	regoff;

			regno = drm_get_pci_index_reg(dp->dip,
			    map->offset, (uint_t)len, &regoff);

			if (regno < 0) {
				DRM_ERROR("devmap: failed to get register"
				    " offset=0x%llx, len=0x%lx", handle, len);
				return (EINVAL);
			}

			if (dev_attr == NULL) {
				dev_attr = &dev_attr_noswap;
			}

			ret = devmap_devmem_setup(dhp, dp->dip, cb,
			    regno, (offset_t)regoff, len, PROT_ALL,
			    0, dev_attr);
			if (ret != 0) {
				*maplen = 0;
				DRM_ERROR("devmap: failed, regno=%d,type=%d,"
				    " handle=0x%lx, offset=0x%llx, len=0x%lx",
				    regno, map->type, (unsigned long) handle,
				    offset, len);
				return (ret);
			}
			*maplen = len;
			return (ret);
		}

	case _DRM_SHM:

		if (map->drm_umem_cookie == NULL)
			return (EINVAL);
		length = ptob(btopr(map->size));
		ret = devmap_umem_setup(dhp, dp->dip, NULL,
		    map->drm_umem_cookie, 0, length,
		    PROT_ALL, IOMEM_DATA_CACHED, NULL);
		if (ret != 0) {
			*maplen = 0;
			return (ret);
		}
		*maplen = length;

		return (DDI_SUCCESS);

#ifdef __x86
	case _DRM_AGP:
		if (dp->agp == NULL) {
			cmn_err(CE_WARN, "drm_sun_devmap: attempted to mmap AGP"
			    "memory before AGP support is enabled");
			return (DDI_FAILURE);
		}

		aperbase = dp->agp->base;
		koff = map->offset - aperbase;
		length = ptob(btopr(len));
		kva = map->dev_addr;
		cookie = gfxp_umem_cookie_init(kva, length);
		if (cookie == NULL) {
			cmn_err(CE_WARN, "devmap:failed to get umem_cookie");
			return (DDI_FAILURE);
		}

		dev_attr = &dev_attr_noswap;
		if ((ret = devmap_umem_setup(dhp, dp->dip,
		    &drm_devmap_callbacks, cookie, 0, length, PROT_ALL,
		    IOMEM_DATA_UNCACHED | DEVMAP_ALLOW_REMAP, dev_attr)) < 0) {
			gfxp_umem_cookie_destroy(cookie);
			cmn_err(CE_WARN, "devmap:failed, retval=%d", ret);
			return (DDI_FAILURE);
		}
		*maplen = length;
		break;

	case _DRM_SCATTER_GATHER:
		koff = map->offset - (unsigned long)(caddr_t)dp->sg->virtual;
		kva = map->dev_addr + koff;
		length = ptob(btopr(len));
		if (length > map->size) {
			cmn_err(CE_WARN, "offset=0x%lx, virtual=0x%p,"
			    "mapsize=0x%lx,len=0x%lx", map->offset,
			    dp->sg->virtual, map->size, len);
			return (DDI_FAILURE);
		}
		cookie = gfxp_umem_cookie_init(kva, length);
		if (cookie == NULL) {
			cmn_err(CE_WARN, "devmap:failed to get umem_cookie");
			return (DDI_FAILURE);
		}

		dev_attr = &dev_attr_noswap;
		ret = devmap_umem_setup(dhp, dp->dip,
		    &drm_devmap_callbacks, cookie, 0, length, PROT_ALL,
		    IOMEM_DATA_UNCACHED | DEVMAP_ALLOW_REMAP, dev_attr);
		if (ret != 0) {
			cmn_err(CE_WARN, "sun_devmap: umem_setup fail");
			gfxp_umem_cookie_destroy(cookie);
			return (DDI_FAILURE);
		}
		*maplen = length;
		break;
#endif /* x86 */

	default:
		return (DDI_FAILURE);
	}

#ifdef __x86
	return (DDI_SUCCESS);
#endif
}

#ifdef __x86
/*ARGSUSED*/
static int
drm_devmap_map(devmap_cookie_t dhc, dev_t dev, uint_t flags,
    offset_t offset, size_t len, void **new_priv)
{
	devmap_handle_t			*dhp;
	drm_inst_state_t		*statep;
	struct ddi_umem_cookie	*cp;

	statep = drm_sup_devt_to_state(dev);
	ASSERT(statep != NULL);

	/*
	 * This driver only supports MAP_SHARED,
	 * and doesn't support MAP_PRIVATE
	 */
	if (flags & MAP_PRIVATE) {
		cmn_err(CE_WARN, "!DRM driver doesn't support MAP_PRIVATE");
		return (EINVAL);
	}

	mutex_enter(&statep->dis_ctxlock);
	dhp = (devmap_handle_t *)dhc;
	cp = (struct ddi_umem_cookie *)dhp->dh_cookie;
	cp->cook_refcnt = 1;
	mutex_exit(&statep->dis_ctxlock);
	*new_priv = statep;

	return (0);
}

/*ARGSUSED*/
static void
drm_devmap_unmap(devmap_cookie_t dhc, void *pvtp, offset_t off, size_t len,
    devmap_cookie_t new_dhp1, void **new_pvtp1, devmap_cookie_t new_dhp2,
    void **new_pvtp2)
{
	devmap_handle_t		*dhp;
	devmap_handle_t		*ndhp;
	drm_inst_state_t		*statep;
	struct ddi_umem_cookie	*cp;
	struct ddi_umem_cookie	*ncp;

	dhp = (devmap_handle_t *)dhc;
	statep = (drm_inst_state_t *)pvtp;

	mutex_enter(&statep->dis_ctxlock);
	cp = (struct ddi_umem_cookie *)dhp->dh_cookie;
	if (new_dhp1 != NULL) {
		ndhp = (devmap_handle_t *)new_dhp1;
		ncp = (struct ddi_umem_cookie *)ndhp->dh_cookie;
		ncp->cook_refcnt ++;
		*new_pvtp1 = statep;
		ASSERT(ncp == cp);
	}

	if (new_dhp2 != NULL) {
		ndhp = (devmap_handle_t *)new_dhp2;
		ncp = (struct ddi_umem_cookie *)ndhp->dh_cookie;
		ncp->cook_refcnt ++;
		*new_pvtp2 = statep;
		ASSERT(ncp == cp);
	}

	cp->cook_refcnt --;

	if (cp->cook_refcnt == 0) {
		gfxp_umem_cookie_destroy(dhp->dh_cookie);
		dhp->dh_cookie = NULL;
	}

	mutex_exit(&statep->dis_ctxlock);
}


/*ARGSUSED*/
static int
drm_devmap_dup(devmap_cookie_t dhc, void *pvtp, devmap_cookie_t new_dhc,
    void **new_pvtp)
{
	devmap_handle_t			*dhp;
	drm_inst_state_t    *statep;
	struct ddi_umem_cookie *cp;

	statep = (drm_inst_state_t *)pvtp;
	mutex_enter(&statep->dis_ctxlock);
	dhp = (devmap_handle_t *)dhc;
	cp = (struct ddi_umem_cookie *)dhp->dh_cookie;
	cp->cook_refcnt ++;
	mutex_exit(&statep->dis_ctxlock);
	*new_pvtp = statep;

	return (0);
}
#endif /* x86 */

int
drm_dev_to_instance(dev_t dev)
{
	return (DEV2INST(dev));
}

/*
 * drm_supp_alloc_drv_entry()
 *
 * Description:
 *	Create a DRM entry and add it into the instance list (drm_inst_head).
 *	Note that we don't allow a duplicated entry
 */
static drm_inst_list_t *
drm_supp_alloc_drv_entry(dev_info_t *dip)
{
	drm_inst_list_t	**plist;
	drm_inst_list_t	*list;
	drm_inst_list_t	*entry;

	/* protect the driver list */
	mutex_enter(&drm_inst_list_lock);
	plist = &drm_inst_head;
	list = *plist;
	while (list) {
		if (list->disl_state.mis_dip == dip) {
			mutex_exit(&drm_inst_list_lock);
			cmn_err(CE_WARN, "%s%d already registered",
			    ddi_driver_name(dip), ddi_get_instance(dip));
			return (NULL);
		}
		plist = &list->disl_next;
		list = list->disl_next;
	}

	/* "dip" is not registered, create new one and add to list */

	entry = kmem_zalloc(sizeof (*entry), KM_SLEEP);
	*plist = entry;
	entry->disl_state.mis_dip = dip;
	mutex_init(&entry->disl_state.mis_lock, NULL, MUTEX_DRIVER, NULL);
	mutex_init(&entry->disl_state.dis_ctxlock, NULL, MUTEX_DRIVER, NULL);
	mutex_exit(&drm_inst_list_lock);

	return (entry);

}	/* drm_supp_alloc_drv_entry */

/*
 * drm_supp_free_drv_entry()
 */
static void
drm_supp_free_drv_entry(dev_info_t *dip)
{
	drm_inst_list_t		*list;
	drm_inst_list_t		**plist;
	drm_inst_state_t	*mstate;

	/* protect the driver list */
	mutex_enter(&drm_inst_list_lock);
	plist = &drm_inst_head;
	list = *plist;
	while (list) {
		if (list->disl_state.mis_dip == dip) {
			*plist = list->disl_next;
			mstate = &list->disl_state;
			mutex_destroy(&mstate->mis_lock);
			mutex_destroy(&mstate->dis_ctxlock);
			kmem_free(list, sizeof (*list));
			mutex_exit(&drm_inst_list_lock);
			return;
		}
		plist = &list->disl_next;
		list = list->disl_next;
	}
	mutex_exit(&drm_inst_list_lock);

}	/* drm_supp_free_drv_entry() */

/*
 * drm_sup_devt_to_state()
 *
 * description:
 *	Get the soft state of DRM instance by device number
 */
drm_inst_state_t *
drm_sup_devt_to_state(dev_t dev)
{
	drm_inst_list_t	*list;
	drm_inst_state_t	*mstate;
	major_t	major = getmajor(dev);
	int		instance = DEV2INST(dev);

	mutex_enter(&drm_inst_list_lock);
	list = drm_inst_head;
	while (list) {
		mstate = &list->disl_state;
		mutex_enter(&mstate->mis_lock);

		if ((mstate->mis_major == major) &&
		    (ddi_get_instance(mstate->mis_dip) == instance)) {
			mutex_exit(&mstate->mis_lock);
			mutex_exit(&drm_inst_list_lock);

			return (mstate);
		}

		list = list->disl_next;
		mutex_exit(&mstate->mis_lock);
	}

	mutex_exit(&drm_inst_list_lock);
	return (NULL);

}	/* drm_sup_devt_to_state() */

int
drm_supp_get_irq(void *handle)
{
	drm_inst_list_t *list;
	drm_inst_state_t    *mstate;
	int		irq;

	list = (drm_inst_list_t *)handle;
	mstate = &list->disl_state;
	ASSERT(mstate != NULL);
	irq = pci_config_get8(mstate->mis_cfg_hdl, PCI_CONF_ILINE);
	return (irq);
}

int
drm_supp_device_capability(void *handle, int capid)
{
	drm_inst_list_t *list;
	drm_inst_state_t    *mstate;
	uint8_t		cap = 0;
	uint16_t	caps_ptr;

	list = (drm_inst_list_t *)handle;
	mstate = &list->disl_state;
	ASSERT(mstate != NULL);

	/* has capabilities list ? */
	if ((pci_config_get16(mstate->mis_cfg_hdl, PCI_CONF_STAT) &
	    PCI_CONF_CAP_MASK) == 0)
		return (NULL);

	caps_ptr = pci_config_get8(mstate->mis_cfg_hdl, PCI_CONF_CAP_PTR);
	while (caps_ptr != PCI_CAP_NEXT_PTR_NULL) {
		cap = pci_config_get32(mstate->mis_cfg_hdl, caps_ptr);
		if ((cap & PCI_CONF_CAPID_MASK) == capid)
			return (cap);
		caps_ptr = pci_config_get8(mstate->mis_cfg_hdl,
		    caps_ptr + PCI_CAP_NEXT_PTR);
	}

	return (0);
}


drm_cminor_t *
drm_find_minordev(drm_inst_state_t *mstate, dev_t dev)
{
	drm_device_t 	*dp = mstate->mis_devp;
	drm_cminor_t 	*mp;
	minor_t		 minor;

	minor = DEV2MINOR(dev);

	mutex_enter(&dp->dev_lock);
	TAILQ_FOREACH(mp, &dp->minordevs, link) {
		if (mp->minor == minor)
			break;
	}
	mutex_exit(&dp->dev_lock);

	return (mp);
}
