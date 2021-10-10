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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include "shadow_impl.h"

/*
 * Removes an entire directory tree.  This should really be using openat() and
 * friends, but there is (annoyingly) no rmdirat().
 */
static void
shadow_remove_tree(const char *path)
{
	char child[PATH_MAX];
	struct stat64 statbuf;
	DIR *dir;
	struct dirent *dp;

	if (stat64(path, &statbuf) != 0)
		return;

	if (S_ISDIR(statbuf.st_mode)) {
		if ((dir = opendir(path)) != NULL) {
			while ((dp = readdir(dir)) != NULL) {
				if (strcmp(dp->d_name, ".") == 0 ||
				    strcmp(dp->d_name, "..") == 0)
					continue;

				(void) snprintf(child, sizeof (child),
				    "%s/%s", path, dp->d_name);
				shadow_remove_tree(child);
			}
			(void) closedir(dir);
		}

		(void) rmdir(path);
	} else {
		(void) unlink(path);
	}
}

/*
 * Cancel any current shadow migration.  This involves clearing the shadow
 * mount option, clearing the ZFS shadow property if a ZFS dataset, and
 * removing the global extended attributes associated with the migration.
 */
int
shadow_cancel(shadow_handle_t *shp)
{
	libzfs_handle_t *zhdl;
	zfs_handle_t *zhp;
	char path[PATH_MAX];

	/*
	 * Disable shadow migration.  If this is a ZFS dataset, then we clear
	 * the corresponding ZFS property, which will tear down the underlying
	 * mount.  If this is a regular filesystem, then we just do a remount
	 * without the corresponding mount option.
	 */
	if (shp->sh_dataset != NULL) {
		if ((zhdl = libzfs_init()) == NULL) {
			(void) shadow_error(ESHADOW_ZFS_NOENT,
			    dgettext(TEXT_DOMAIN, "failed to load libzfs"));
			return (-1);
		}

		if ((zhp = zfs_open(zhdl, shp->sh_dataset,
		    ZFS_TYPE_FILESYSTEM)) == NULL) {
			(void) shadow_zfs_error(zhdl);
			libzfs_fini(zhdl);
			return (-1);
		}

		if (zfs_prop_set(zhp, zfs_prop_to_name(ZFS_PROP_SHADOW),
		    "none") != 0) {
			(void) shadow_zfs_error(zhdl);
			zfs_close(zhp);
			libzfs_fini(zhdl);
			return (-1);
		}

		zfs_close(zhp);
		libzfs_fini(zhdl);
	} else {
		if (mount(shp->sh_special, shp->sh_mountpoint, MS_REMOUNT,
		    shp->sh_fstype, NULL, 0, NULL, 0) != 0) {
			return (shadow_error(ESHADOW_MNT_CLEAR,
			    "failed to clear shadow mount option: %s",
			    strerror(errno)));
		}
	}

	/*
	 * Now that the shadow setting has been cleared, go through the private
	 * .SUNWshadow directory and remove any internal data.  If this fails
	 * for any reason, we ignore it and drive on, as there is no way to
	 * resume the migration, and the filesystem data itself is intact.
	 */
	(void) snprintf(path, sizeof (path), "%s/%s", shp->sh_mountpoint,
	    VFS_SHADOW_PRIVATE_DIR);
	shadow_remove_tree(path);

	return (0);
}
