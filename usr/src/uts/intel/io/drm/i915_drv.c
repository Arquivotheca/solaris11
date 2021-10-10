/* BEGIN CSTYLED */

/*
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * i915_drv.c -- Intel i915 driver -*- linux-c -*-
 * Created: Wed Feb 14 17:10:04 2001 by gareth@valinux.com
 */

/*
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas.
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
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Gareth Hughes <gareth@valinux.com>
 *
 */

/*
 * I915 DRM Driver for Solaris
 *
 * This driver provides the hardware 3D acceleration support for Intel
 * integrated video devices (e.g. i8xx/i915/i945 series chipsets), under the
 * DRI (Direct Rendering Infrastructure). DRM (Direct Rendering Manager) here
 * means the kernel device driver in DRI.
 *
 * I915 driver is a device dependent driver only, it depends on a misc module
 * named drm for generic DRM operations.
 */

#include "drmP.h"
#include "i915_drm.h"
#include "i915_drv.h"
#include "drm_crtc_helper.h"

unsigned int i915_powersave = 1;

unsigned int i915_lvds_downclock = 0;

static void *i915_statep;

static int i915_info(dev_info_t *, ddi_info_cmd_t, void *, void **);
static int i915_attach(dev_info_t *, ddi_attach_cmd_t);
static int i915_detach(dev_info_t *, ddi_detach_cmd_t);
static int i915_quiesce(dev_info_t *);

extern struct cb_ops drm_cb_ops;

static struct dev_ops i915_dev_ops = {
	DEVO_REV,		/* devo_rev */
	0,			/* devo_refcnt */
	i915_info,		/* devo_getinfo */
	nulldev,		/* devo_identify */
	nulldev,		/* devo_probe */
	i915_attach,		/* devo_attach */
	i915_detach,		/* devo_detach */
	nodev,			/* devo_reset */
	&drm_cb_ops,		/* devo_cb_ops */
	NULL,			/* devo_bus_ops */
	NULL,			/* power */
	i915_quiesce,		/* devo_quiesce */
};

static struct modldrv modldrv = {
	&mod_driverops,		/* drv_modops */
	"I915 DRM driver",	/* drv_linkinfo */
	&i915_dev_ops,		/* drv_dev_ops */
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *) &modldrv, NULL
};

#define INTEL_VGA_DEVICE(id, info) {		\
	.vendor = 0x8086,			\
	.device = id,				\
	.driver_data = (unsigned long) info }

static const struct intel_device_info intel_i830_info = {
	.gen = 2, .is_i8xx = 1, .is_mobile = 1, .cursor_needs_physical = 1,
};

static const struct intel_device_info intel_845g_info = {
	.gen = 2, .is_i8xx = 1,
};

static const struct intel_device_info intel_i85x_info = {
	.gen = 2, .is_i8xx = 1, .is_i85x = 1, .is_mobile = 1,
	.cursor_needs_physical = 1,
};

static const struct intel_device_info intel_i865g_info = {
	.gen = 2, .is_i8xx = 1,
};

static const struct intel_device_info intel_i915g_info = {
	.gen = 3, .is_i915g = 1, .is_i9xx = 1, .cursor_needs_physical = 1,
};
static const struct intel_device_info intel_i915gm_info = {
	.gen = 3, .is_i9xx = 1,  .is_mobile = 1,
	.cursor_needs_physical = 1,
};
static const struct intel_device_info intel_i945g_info = {
	.gen = 3, .is_i9xx = 1, .has_hotplug = 1, .cursor_needs_physical = 1,
};
static const struct intel_device_info intel_i945gm_info = {
	.gen = 3, .is_i945gm = 1, .is_i9xx = 1, .is_mobile = 1,
	.has_hotplug = 1, .cursor_needs_physical = 1,
};

static const struct intel_device_info intel_i965g_info = {
	.gen = 4, .is_i965g = 1, .is_i9xx = 1, .has_hotplug = 1,
};

static const struct intel_device_info intel_i965gm_info = {
	.gen = 4, .is_i965g = 1, .is_mobile = 1, .is_i965gm = 1, .is_i9xx = 1,
	.is_mobile = 1, .has_fbc = 1, .has_rc6 = 1,
	.has_hotplug = 1,
};

static const struct intel_device_info intel_g33_info = {
	.gen = 3, .is_g33 = 1, .is_i9xx = 1, .need_gfx_hws = 1,
	.has_hotplug = 1,
};

static const struct intel_device_info intel_g45_info = {
	.gen = 4, .is_i965g = 1, .is_g4x = 1, .is_i9xx = 1, .need_gfx_hws = 1,
	.has_pipe_cxsr = 1,
	.has_hotplug = 1,
};

static const struct intel_device_info intel_gm45_info = {
	.gen = 4, .is_i965g = 1, .is_mobile = 1, .is_g4x = 1, .is_i9xx = 1,
	.is_mobile = 1, .need_gfx_hws = 1, .has_fbc = 1, .has_rc6 = 1,
	.has_pipe_cxsr = 1,
	.has_hotplug = 1,
};

static const struct intel_device_info intel_pineview_info = {
	.gen = 3, .is_g33 = 1, .is_pineview = 1, .is_mobile = 1, .is_i9xx = 1,
	.need_gfx_hws = 1,
	.has_hotplug = 1,
};

static const struct intel_device_info intel_ironlake_d_info = {
	.gen = 5, .is_ironlake = 1, .is_i965g = 1, .is_i9xx = 1, .need_gfx_hws = 1,
	.has_pipe_cxsr = 1,
	.has_hotplug = 1,
};

static const struct intel_device_info intel_ironlake_m_info = {
	.gen = 5, .is_ironlake = 1, .is_mobile = 1, .is_i965g = 1, .is_i9xx = 1,
	.need_gfx_hws = 1, .has_rc6 = 1,
	.has_hotplug = 1,
};

static const struct intel_device_info intel_sandybridge_d_info = {
	.gen = 6, .is_i965g = 1, .is_i9xx = 1, .need_gfx_hws = 1,
	.has_hotplug = 1,
};

static const struct intel_device_info intel_sandybridge_m_info = {
	.gen = 6, .is_i965g = 1, .is_mobile = 1, .is_i9xx = 1, .need_gfx_hws = 1,
	.has_hotplug = 1,
};

static struct drm_pci_id_list pciidlist[] = {
	INTEL_VGA_DEVICE(0x3577, &intel_i830_info),
	INTEL_VGA_DEVICE(0x2562, &intel_845g_info),
	INTEL_VGA_DEVICE(0x3582, &intel_i85x_info),
	INTEL_VGA_DEVICE(0x358e, &intel_i85x_info),
	INTEL_VGA_DEVICE(0x2572, &intel_i865g_info),
	INTEL_VGA_DEVICE(0x2582, &intel_i915g_info),
	INTEL_VGA_DEVICE(0x258a, &intel_i915g_info),
	INTEL_VGA_DEVICE(0x2592, &intel_i915gm_info),
	INTEL_VGA_DEVICE(0x2772, &intel_i945g_info),
	INTEL_VGA_DEVICE(0x27a2, &intel_i945gm_info),
	INTEL_VGA_DEVICE(0x27ae, &intel_i945gm_info),
	INTEL_VGA_DEVICE(0x2972, &intel_i965g_info),
	INTEL_VGA_DEVICE(0x2982, &intel_i965g_info),
	INTEL_VGA_DEVICE(0x2992, &intel_i965g_info),
	INTEL_VGA_DEVICE(0x29a2, &intel_i965g_info),
	INTEL_VGA_DEVICE(0x29b2, &intel_g33_info),
	INTEL_VGA_DEVICE(0x29c2, &intel_g33_info),
	INTEL_VGA_DEVICE(0x29d2, &intel_g33_info),
	INTEL_VGA_DEVICE(0x2a02, &intel_i965gm_info),
	INTEL_VGA_DEVICE(0x2a12, &intel_i965gm_info),
	INTEL_VGA_DEVICE(0x2a42, &intel_gm45_info),
	INTEL_VGA_DEVICE(0x2e02, &intel_g45_info),
	INTEL_VGA_DEVICE(0x2e12, &intel_g45_info),
	INTEL_VGA_DEVICE(0x2e22, &intel_g45_info),
	INTEL_VGA_DEVICE(0x2e32, &intel_g45_info),
	INTEL_VGA_DEVICE(0x2e42, &intel_g45_info),
	INTEL_VGA_DEVICE(0xa001, &intel_pineview_info),
	INTEL_VGA_DEVICE(0xa011, &intel_pineview_info),
	INTEL_VGA_DEVICE(0x0042, &intel_ironlake_d_info),
	INTEL_VGA_DEVICE(0x0046, &intel_ironlake_m_info),
	INTEL_VGA_DEVICE(0x0102, &intel_sandybridge_d_info),
	INTEL_VGA_DEVICE(0x0112, &intel_sandybridge_d_info),
	INTEL_VGA_DEVICE(0x0122, &intel_sandybridge_d_info),
	INTEL_VGA_DEVICE(0x0106, &intel_sandybridge_m_info),
	INTEL_VGA_DEVICE(0x0116, &intel_sandybridge_m_info),
	INTEL_VGA_DEVICE(0x0126, &intel_sandybridge_m_info),
	INTEL_VGA_DEVICE(0x010A, &intel_sandybridge_d_info),
	{0, 0, 0}
};

#define PCI_VENDOR_ID_INTEL		0x8086
#define INTEL_PCH_DEVICE_ID_MASK	0xff00
#define INTEL_PCH_CPT_DEVICE_ID_TYPE	0x1c00

void intel_detect_pch (struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	dev_info_t	*isa_dip;
	int	vendor_id, device_id;
	/* LINTED */
	int	error;

	/*
	 * The reason to probe ISA bridge instead of Dev31:Fun0 is to
	 * make graphics device passthrough work easy for VMM, that only
	 * need to expose ISA bridge to let driver know the real hardware
	 * underneath. This is a requirement from virtualization team.
	 */
	isa_dip = ddi_find_devinfo("isa", -1, 0);

	if (isa_dip) {
		vendor_id = ddi_prop_get_int(DDI_DEV_T_ANY, isa_dip, DDI_PROP_DONTPASS,
			"vendor-id", -1);
		DRM_DEBUG("vendor_id 0x%x", vendor_id);

		if (vendor_id == PCI_VENDOR_ID_INTEL) {
			device_id = ddi_prop_get_int(DDI_DEV_T_ANY, isa_dip, DDI_PROP_DONTPASS,
				"device-id", -1);
			DRM_DEBUG("device_id 0x%x", device_id);
			device_id &= INTEL_PCH_DEVICE_ID_MASK;
			if (device_id == INTEL_PCH_CPT_DEVICE_ID_TYPE) {
				dev_priv->pch_type = PCH_CPT;
				DRM_DEBUG_KMS("Found CougarPoint PCH\n");
			}
		}
	}
}

static int
i915_suspend(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (!dev || !dev_priv) {
		DRM_ERROR("dev: %p, dev_priv: %p\n", (void *)dev, (void *)dev_priv);
		DRM_ERROR("DRM not initialized, aborting suspend.\n");
		return -ENODEV;
	}

	/* XXX FIXME: pci_save_state(dev->pdev); */

	/* If KMS is active, we do the leavevt stuff here */
	if (drm_core_check_feature(dev, DRIVER_MODESET) && dev->gtt_total !=0) {

		if (dev_priv->gfx_state_saved)
			(void) i915_restore_state(dev, &dev_priv->gfx_state);

		if (i915_gem_idle(dev, 0))
			DRM_ERROR("GEM idle failed, resume may fail\n");
		(void) drm_irq_uninstall(dev);
	}

	(void) i915_save_state(dev, &dev_priv->s3_state);

	/* Modeset on resume, not lid events */
	dev_priv->modeset_on_lid = 0;

	return 0;
}

static int
i915_resume(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret = 0;

	(void) i915_restore_state(dev, &dev_priv->s3_state);

	/* KMS EnterVT equivalent */
	if (drm_core_check_feature(dev, DRIVER_MODESET) && dev->gtt_total !=0) {
		mutex_lock(&dev->struct_mutex);
		dev_priv->mm.suspended = 0;

		ret = i915_gem_init_ringbuffer(dev);
		if (ret != 0)
			ret = -1;
		mutex_unlock(&dev->struct_mutex);

		(void) drm_irq_install(dev);

		if (!dev_priv->isX)
			(void) i915_restore_state(dev, &dev_priv->txt_state);
	}
	if (drm_core_check_feature(dev, DRIVER_MODESET) && dev_priv->isX) {
		/* Resume the modeset for every activated CRTC */
		(void) drm_helper_resume_force_mode(dev);
	}

	dev_priv->modeset_on_lid = 0;

	return ret;
}

/**
 * i965_reset - reset chip after a hang
 * @dev: drm device to reset
 * @flags: reset domains
 *
 * Reset the chip.  Useful if a hang is detected. Returns zero on successful
 * reset or otherwise an error code.
 *
 * Procedure is fairly simple:
 *   - reset the chip using the reset reg
 *   - re-init context state
 *   - re-init hardware status page
 *   - re-init ring buffer
 *   - re-init interrupt state
 *   - re-init display
 */
int i965_reset(struct drm_device *dev, u8 flags)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	unsigned long timeout;
	u8 gdrst;
	/*
	 * We really should only reset the display subsystem if we actually
	 * need to
	 */
	bool need_display = true;

	mutex_lock(&dev->struct_mutex);

	/*
	 * Clear request list
	 */
	i915_gem_retire_requests(dev);

	if (need_display)
		i915_save_display(dev, &dev_priv->s3_state);

	if (IS_I965G(dev) || IS_G4X(dev)) {
		/*
		 * Set the domains we want to reset, then the reset bit (bit 0).
		 * Clear the reset bit after a while and wait for hardware status
		 * bit (bit 1) to be set
		 */
		(void) pci_read_config_byte(dev->pdev, GDRST, &gdrst);
		(void) pci_write_config_byte(dev->pdev, GDRST, gdrst | flags | ((flags == GDRST_FULL) ? 0x1 : 0x0));
		udelay(50);
		(void) pci_write_config_byte(dev->pdev, GDRST, gdrst & 0xfe);

		/* ...we don't want to loop forever though, 500ms should be plenty */
	       timeout = jiffies + msecs_to_jiffies(500);
		do {
			udelay(100);
			(void) pci_read_config_byte(dev->pdev, GDRST, &gdrst);
		} while ((gdrst & 0x1) && time_after(timeout, jiffies));

		if (gdrst & 0x1) {
			mutex_unlock(&dev->struct_mutex);
			return -EIO;
		}
	} else {
		DRM_ERROR("Error occurred. Don't know how to reset this chip.\n");
		return -ENODEV;
	}

	/* Ok, now get things going again... */

	/*
	 * Everything depends on having the GTT running, so we need to start
	 * there.  Fortunately we don't need to do this unless we reset the
	 * chip at a PCI level.
	 *
	 * Next we need to restore the context, but we don't use those
	 * yet either...
	 *
	 * Ring buffer needs to be re-initialized in the KMS case, or if X
	 * was running at the time of the reset (i.e. we weren't VT
	 * switched away).
	 */
	if (drm_core_check_feature(dev, DRIVER_MODESET) ||
	    !dev_priv->mm.suspended) {
		drm_i915_ring_buffer_t *ring = &dev_priv->ring;
		struct drm_gem_object *obj = ring->ring_obj;
		struct drm_i915_gem_object *obj_priv = obj->driver_private;
		dev_priv->mm.suspended = 0;

		/* Stop the ring if it's running. */
		I915_WRITE(PRB0_CTL, 0);
		I915_WRITE(PRB0_TAIL, 0);
		I915_WRITE(PRB0_HEAD, 0);

		/* Initialize the ring. */
		I915_WRITE(PRB0_START, obj_priv->gtt_offset);
		I915_WRITE(PRB0_CTL,
			   ((obj->size - 4096) & RING_NR_PAGES) |
			   RING_NO_REPORT |
			   RING_VALID);
		if (!drm_core_check_feature(dev, DRIVER_MODESET))
			i915_kernel_lost_context(dev);
		else {
			ring->head = I915_READ(PRB0_HEAD) & HEAD_ADDR;
			ring->tail = I915_READ(PRB0_TAIL) & TAIL_ADDR;
			ring->space = ring->head - (ring->tail + 8);
			if (ring->space < 0)
				ring->space += ring->Size;
		}

		mutex_unlock(&dev->struct_mutex);
		(void) drm_irq_uninstall(dev);
		(void) drm_irq_install(dev);
		mutex_lock(&dev->struct_mutex);
	}

	/*
	 * Display needs restore too...
	 */
	if (need_display)
		i915_restore_display(dev, &dev_priv->s3_state);

	mutex_unlock(&dev->struct_mutex);
	return 0;
}

static struct drm_driver driver = {
	/* don't use mtrr's here, the Xserver or user space app should
	 * deal with them for intel hardware.
	 */
	.driver_features =
	    DRIVER_USE_AGP | DRIVER_REQUIRE_AGP | /* DRIVER_USE_MTRR |*/
	    DRIVER_HAVE_IRQ | DRIVER_IRQ_SHARED | DRIVER_GEM,
	.load = i915_driver_load,
	.unload = i915_driver_unload,
	.firstopen = i915_driver_firstopen,
	.open = i915_driver_open,
	.lastclose = i915_driver_lastclose,
	.preclose = i915_driver_preclose,
	.postclose = i915_driver_postclose,
	/*.suspend = i915_suspend,*/
	/*.resume = i915_resume,*/
	.device_is_agp = i915_driver_device_is_agp,
	.enable_vblank = i915_enable_vblank,
	.disable_vblank = i915_disable_vblank,
	.irq_preinstall = i915_driver_irq_preinstall,
	.irq_postinstall = i915_driver_irq_postinstall,
	.irq_uninstall = i915_driver_irq_uninstall,
	.irq_handler = i915_driver_irq_handler,
	.reclaim_buffers = drm_core_reclaim_buffers,
	/*.get_map_ofs = drm_core_get_map_ofs,*/
	/*.get_reg_ofs = drm_core_get_reg_ofs,*/
	.master_create = i915_master_create,
	.master_destroy = i915_master_destroy,
	.entervt = i915_driver_entervt,
	.leavevt = i915_driver_leavevt,
#if defined(CONFIG_DEBUG_FS)
	.debugfs_init = i915_debugfs_init,
	.debugfs_cleanup = i915_debugfs_cleanup,
#endif
	.gem_init_object = i915_gem_init_object,
	.gem_free_object = i915_gem_free_object,
	/*.gem_vm_ops = &i915_gem_vm_ops,*/
	.ioctls = i915_ioctls,

	.id_table = pciidlist,

	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};

static int
i915_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	struct drm_device *dev;
	int ret, item;

	item = ddi_get_instance(dip);

	switch (cmd) {
	case DDI_ATTACH:
		if (ddi_soft_state_zalloc(i915_statep, item) != DDI_SUCCESS) {
			DRM_ERROR("failed to alloc softstate, item = %d", item);
			return (DDI_FAILURE);
		}

		dev = ddi_get_soft_state(i915_statep, item);
		if (!dev) {
			DRM_ERROR("cannot get soft state");
			return (DDI_FAILURE);
		}

		dev->devinfo = dip;

		ret = drm_init(dev, &driver);
		if (ret != DDI_SUCCESS)
			(void) ddi_soft_state_free(i915_statep, item);

		return (ret);

	case DDI_RESUME:
		dev = ddi_get_soft_state(i915_statep, item);
		if (!dev) {
			DRM_ERROR("cannot get soft state");
			return (DDI_FAILURE);
		}

		return (i915_resume(dev));
	}

	DRM_ERROR("only supports attach or resume");
	return (DDI_FAILURE);
}

static int
i915_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	struct drm_device *dev;
	int item;

	item = ddi_get_instance(dip);
	dev = ddi_get_soft_state(i915_statep, item);
	if (!dev) {
		DRM_ERROR("cannot get soft state");
		return (DDI_FAILURE);
	}

	switch (cmd) {
	case DDI_DETACH:
		drm_exit(dev);
		(void) ddi_soft_state_free(i915_statep, item);
		return (DDI_SUCCESS);

	case DDI_SUSPEND:
		return (i915_suspend(dev));
	}

	DRM_ERROR("only supports detach or suspend");
	return (DDI_FAILURE);
}

static int
/* LINTED */
i915_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	struct drm_minor *minor;

	minor = idr_find(&drm_minors_idr, DRM_DEV2MINOR((dev_t)arg));
	if (!minor)
		return (DDI_FAILURE);
	if (!minor->dev || !minor->dev->devinfo)
		return (DDI_FAILURE);

	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		*result = (void *)minor->dev->devinfo;
		return (DDI_SUCCESS);

	case DDI_INFO_DEVT2INSTANCE:
		*result = (void *)(uintptr_t)ddi_get_instance(minor->dev->devinfo);
		return (DDI_SUCCESS);
	}

	return (DDI_FAILURE);
}

static int
i915_quiesce(dev_info_t *dip)
{
	struct drm_device *dev;

	dev = ddi_get_soft_state(i915_statep, ddi_get_instance(dip));
	if (!dev)
		return (DDI_FAILURE);

	i915_driver_irq_uninstall(dev);

	return (DDI_SUCCESS);
}

static int __init i915_init(void)
{
	driver.num_ioctls = i915_max_ioctl;

	driver.driver_features |= DRIVER_MODESET;

	return 0;
}

static void __exit i915_exit(void)
{

}

int
_init(void)
{
	int ret;

	ret = ddi_soft_state_init(&i915_statep,
	    sizeof (struct drm_device), DRM_MAX_INSTANCES);
	if (ret)
		return (ret);

	ret = i915_init();
	if (ret) {
		ddi_soft_state_fini(&i915_statep);
		return (ret);
	}

	ret = mod_install(&modlinkage);
	if (ret) {
		i915_exit();
		ddi_soft_state_fini(&i915_statep);
		return (ret);
	}

	return (ret);
}

int
_fini(void)
{
	int ret;

	ret = mod_remove(&modlinkage);
	if (ret)
		return (ret);

	i915_exit();

	ddi_soft_state_fini(&i915_statep);

	return (ret);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}
