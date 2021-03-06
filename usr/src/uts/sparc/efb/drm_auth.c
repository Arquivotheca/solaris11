/*
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * drm_auth.h -- IOCTLs for authentication -*- linux-c -*-
 * Created: Tue Feb  2 08:37:54 1999 by faith@valinux.com
 */
/*
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
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
 *    Rickard E. (Rik) Faith <faith@valinux.com>
 *    Gareth Hughes <gareth@valinux.com>
 *
 */

#include "drmP.h"

static int
drm_hash_magic(drm_magic_t magic)
{
	return (magic & (DRM_HASH_SIZE-1));
}

drm_file_t *
drm_find_file(drm_device_t *dev, drm_magic_t magic)
{
	drm_file_t	  *retval = NULL;
	drm_magic_entry_t *pt;
	int		  hash;

	hash = drm_hash_magic(magic);
	for (pt = dev->magiclist[hash].head; pt; pt = pt->next) {
		if (pt->magic == magic) {
			retval = pt->priv;
			break;
		}
	}

	return (retval);
}

static int
drm_add_magic(drm_device_t *dev, drm_file_t *priv, drm_magic_t magic)
{
	int		  hash;
	drm_magic_entry_t *entry;

	hash = drm_hash_magic(magic);
	entry = drm_alloc(sizeof (*entry), DRM_MEM_MAGIC);
	if (!entry)
		return (ENOMEM);
	entry->magic = magic;
	entry->priv  = priv;
	entry->next  = NULL;

	DRM_LOCK();
	if (dev->magiclist[hash].tail) {
		dev->magiclist[hash].tail->next = entry;
		dev->magiclist[hash].tail	= entry;
	} else {
		dev->magiclist[hash].head	= entry;
		dev->magiclist[hash].tail	= entry;
	}
	DRM_UNLOCK();

	return (0);
}

int
drm_remove_magic(drm_device_t *dev, drm_magic_t magic)
{
	drm_magic_entry_t *prev = NULL;
	drm_magic_entry_t *pt;
	int		  hash;

	DRM_DEBUG("drm_remove_magic : %d", magic);
	hash = drm_hash_magic(magic);

	DRM_LOCK();
	for (pt = dev->magiclist[hash].head; pt; prev = pt, pt = pt->next) {
		if (pt->magic == magic) {
			if (dev->magiclist[hash].head == pt) {
				dev->magiclist[hash].head = pt->next;
			}
			if (dev->magiclist[hash].tail == pt) {
				dev->magiclist[hash].tail = prev;
			}
			if (prev) {
				prev->next = pt->next;
			}
			DRM_UNLOCK();
			drm_free(pt, sizeof (*pt), DRM_MEM_MAGIC);
			return (0);
		}
	}
	DRM_UNLOCK();

	return (EINVAL);
}

/*ARGSUSED*/
int
drm_getmagic(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	static drm_magic_t sequence = 0;
	drm_auth_t auth;

	/* Find unique magic */
	if (fpriv->magic) {
		auth.magic = fpriv->magic;
	} else {
		do {
			int old = sequence;
			auth.magic = old+1;
			if (!atomic_cmpset_int(&sequence, old, auth.magic))
				continue;
		} while (drm_find_file(dev, auth.magic));
		fpriv->magic = auth.magic;
		(void) drm_add_magic(dev, fpriv, auth.magic);
	}


	DRM_DEBUG("drm_getmagic: %u", auth.magic);

	DRM_COPYTO_WITH_RETURN((void *)data, &auth, sizeof (auth));

	return (0);
}

/*ARGSUSED*/
int
drm_authmagic(DRM_IOCTL_ARGS)
{
	drm_auth_t	   auth;
	drm_file_t	   *file;
	DRM_DEVICE;

	DRM_COPYFROM_WITH_RETURN(&auth, (void *)data, sizeof (auth));

	if ((file = drm_find_file(dev, auth.magic))) {
		file->authenticated = 1;
		(void) drm_remove_magic(dev, auth.magic);
		return (0);
	}
	return (EINVAL);
}
