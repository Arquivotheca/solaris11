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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include "shadow_impl.h"

/*
 * Open a handle to the given mountpoint.  If this is not a mountpoint, then
 * return an error.  If this is a ZFS dataset, then we record that fact.  We
 * don't bother opening the ZFS dataset, as it's only needed rarely (such as
 * canceling migration).
 */
shadow_handle_t *
shadow_open(const char *mountpoint)
{
	shadow_handle_t *shp;
	FILE *mnttab = NULL;
	struct mnttab mntent, search;
	struct statvfs64 vstat;

	if ((shp = shadow_zalloc(sizeof (shadow_handle_t))) == NULL)
		return (NULL);

	if ((shp->sh_mountpoint = shadow_strdup(mountpoint)) == NULL)
		goto error;

	if ((mnttab = fopen(MNTTAB, "r")) == NULL) {
		(void) shadow_error(ESHADOW_NOMOUNT,
		    dgettext(TEXT_DOMAIN, "failed to open /etc/mnttab"));
		goto error;
	}

	bzero(&search, sizeof (search));
	search.mnt_mountp = shp->sh_mountpoint;
	if (getmntany(mnttab, &mntent, &search) != 0) {
		(void) shadow_error(ESHADOW_NOMOUNT,
		    dgettext(TEXT_DOMAIN, "no such mountpoint"));
		goto error;
	}

	/*
	 * Keep track of the fsid so we can detect when we cross a filesystem
	 * boundary and need to stop the traversal.
	 */
	if (statvfs64(shp->sh_mountpoint, &vstat) != 0) {
		shp->sh_fsid = NODEV;
	} else {
		shp->sh_fsid = vstat.f_fsid;
	}

	if (strcmp(mntent.mnt_fstype, MNTTYPE_ZFS) == 0) {
		if ((shp->sh_dataset =
		    shadow_strdup(mntent.mnt_special)) == NULL)
			goto error;
	} else {
		if ((shp->sh_fstype =
		    shadow_strdup(mntent.mnt_fstype)) == NULL ||
		    (shp->sh_special =
		    shadow_strdup(mntent.mnt_special)) == NULL)
			goto error;
	}

	if (hasmntopt(&mntent, "shadow") == NULL) {
		(void) shadow_error(ESHADOW_NOSHADOW,
		    dgettext(TEXT_DOMAIN,
		    "mountpoint is not a shadow mount"));
		goto error;
	}

	(void) fclose(mnttab);

	if (shadow_pq_init(&shp->sh_queue, shadow_priority) != 0)
		goto error;

	shp->sh_start = gethrtime();

	return (shp);

error:
	if (mnttab != NULL)
		(void) fclose(mnttab);
	shadow_close(shp);
	return (NULL);
}

/*
 * Close the handle to this shadow migration mountpoint, freeing up any
 * resources.
 */
void
shadow_close(shadow_handle_t *shp)
{
	shadow_end(shp);
	shadow_pq_fini(&shp->sh_queue);
	free(shp->sh_dataset);
	free(shp->sh_special);
	free(shp->sh_fstype);
	free(shp->sh_mountpoint);
	free(shp);
}
