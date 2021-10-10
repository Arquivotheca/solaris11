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

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/conf.h>
#include <sys/kmem.h>
#include <sys/visual_io.h>
#include <sys/font.h>
#include <sys/fbio.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/open.h>
#include <sys/modctl.h>
#include <sys/vgareg.h>
#include <sys/vgasubr.h>
#include <sys/pci.h>
#include <sys/kd.h>
#include <sys/ddi_impldefs.h>
#include <sys/sunldi.h>
#include <sys/mkdev.h>
#include <gfx_private.h>
#include <sys/agpgart.h>
#include <sys/agp/agpdefs.h>
#include <sys/agp/agpmaster_io.h>
#include "drmP.h"

/**
 * drm_sysfs_device_add - adds a class device to sysfs for a character driver
 * @dev: DRM device to be added
 * @head: DRM head in question
 *
 * Add a DRM device to the DRM's device model class.  We use @dev's PCI device
 * as the parent for the Linux device, and make sure it has a file containing
 * the driver we're using (for userspace compatibility).
 */
int drm_sysfs_device_add(struct drm_minor *minor)
{
	struct drm_device *dev = minor->dev;
	gfxp_vgatext_softc_ptr_t gfxp;
	int ret;

	switch (minor->type) {
	case DRM_MINOR_AGPMASTER:
		ret = agpmaster_attach(dev->devinfo, 
		    (agp_master_softc_t **)&minor->private,
		    dev->pdev->pci_cfg_acc_handle, minor->index);
		if (ret != DDI_SUCCESS) {
			DRM_ERROR("agpmaster_attach failed");
			return (ret);
		}
		return (0);

	case DRM_MINOR_VGATEXT:
		/* Generic graphics initialization */
		gfxp = gfxp_vgatext_softc_alloc();
		ret = gfxp_vgatext_attach(dev->devinfo, DDI_ATTACH, gfxp);
		if (ret != DDI_SUCCESS) {
			DRM_ERROR("gfxp_vgatext_attach failed");
			return (EFAULT);
		}
		minor->private = gfxp;

		ret = ddi_create_minor_node(dev->devinfo,
		    minor->name, S_IFCHR, minor->index, DDI_NT_DISPLAY, NULL);
		if (ret != DDI_SUCCESS) {
			DRM_ERROR("ddi_create_minor_node failed");
			return (EFAULT);
		}
		return (0);

	case DRM_MINOR_LEGACY:
	case DRM_MINOR_CONTROL:
	case DRM_MINOR_RENDER:
		ret = ddi_create_minor_node(dev->devinfo,
		    minor->name, S_IFCHR, minor->index, DDI_NT_DISPLAY_DRM, NULL);
		if (ret != DDI_SUCCESS) {
			DRM_ERROR("ddi_create_minor_node failed");
			return (EFAULT);
		}
		return (0);
	}

	return (ENOTSUP);
}

/**
 * drm_sysfs_device_remove - remove DRM device
 * @dev: DRM device to remove
 *
 * This call unregisters and cleans up a class device that was created with a
 * call to drm_sysfs_device_add()
 */
void drm_sysfs_device_remove(struct drm_minor *minor)
{
	switch (minor->type) {
	case DRM_MINOR_AGPMASTER:
		if (minor->private) {
			agpmaster_detach(
			    (agp_master_softc_t **)&minor->private);
			minor->private = NULL;
		}
		break;

	case DRM_MINOR_VGATEXT:
		if (minor->private) {
			(void) gfxp_vgatext_detach(minor->dev->devinfo,
			    DDI_DETACH, minor->private);
			gfxp_vgatext_softc_free(minor->private);
			minor->private = NULL;
		}

	/* LINTED */
	case DRM_MINOR_LEGACY:
	case DRM_MINOR_CONTROL:
	case DRM_MINOR_RENDER:	
		ddi_remove_minor_node(minor->dev->devinfo, minor->name);
	}
}
