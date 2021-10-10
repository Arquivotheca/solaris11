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

#pragma ident	"@(#)libzfs_shadow.c	1.2	08/11/18 SMI"

/*
 * ZFS shadow data migration
 *
 * ZFS supports native integration with the 'shadow' mount property used to
 * migrate data from one filesystem to another.  The kernel implementation of
 * this is quite generic, and allows for 'shadow=<path>' to be set on any
 * filesystem.  To make life easier for ZFS consumers, we integrate this with a
 * native 'shadow' property, and support a friendlier way of specifying
 * remote filesystems.
 *
 * The 'shadow' ZFS property does not take a filesystem path, but a URI
 * indicating where the filesystem can be found.  This URL can take one of
 * the following forms:
 *
 * 	file:///path
 * 	nfs://host/path
 *	smb://[workgroup:][user[:password]@]server/share
 *
 * ZFS will then automatically mount this filesystem locally using the
 * appropriate protocol, and pass down the path to the shadow filesystem.
 * Using this model, the kernel is blissfully unaware that we are copying data
 * from a remote filesystem, but the user can specify the location in a
 * higher-level form.
 *
 * This file handles mounting and unmounting this transient mount before it is
 * passed to the kernel.
 */

#include <libgen.h>
#include <libintl.h>
#include <libzfs.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <paths.h>
#include <sys/mntent.h>
#include <sys/mount.h>
#include <sys/wait.h>

#include "libzfs_impl.h"

#define	ZFS_SHADOW_DIR	_PATH_SYSVOL "/zfs/shadow"
#define	MOUNT_COMMAND	"/usr/sbin/mount"

static int
zfs_shadow_parse(libzfs_handle_t *hdl, const char *shadow,
    const char **protocol, const char **mntopts, char **path)
{
	char fullpath[PATH_MAX];
	int len;
	const char *errmsg, *slash;

	errmsg = dgettext(TEXT_DOMAIN, "cannot create shadow directory");

	*mntopts = "ro";

	if (strncmp(shadow, "file://", 7) == 0) {
		char *checkpath, *checkend;

		*protocol = MNTTYPE_LOFS;
		shadow += 7;

		/*
		 * For file targets, we need to make sure the directory exists
		 * and is not a parent of /var/ak/run.
		 */
		if (shadow[0] != '/') {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "shadow file path must be absolute"));
			return (zfs_error(hdl, EZFS_BADSHADOW, errmsg));
		}

		if ((len = resolvepath(shadow, fullpath,
		    sizeof (fullpath))) < 0) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "failed to resolve path: %s"), strerror(errno));
			return (zfs_error(hdl, EZFS_BADSHADOW, errmsg));
		}
		fullpath[len] = '\0';

		/*
		 * Don't accept a path within our shadow mount directory.
		 * Also disallow any path that's one of the directories along
		 * the way to the shadow mount directory.
		 */
		if ((checkpath = zfs_strdup(hdl, ZFS_SHADOW_DIR)) == NULL)
			return (-1);

		if (strncmp(fullpath, ZFS_SHADOW_DIR,
		    strlen(ZFS_SHADOW_DIR)) == 0 &&
		    (fullpath[strlen(checkpath)] == '\0' ||
		    fullpath[strlen(checkpath)] == '/')) {
			free(checkpath);
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "path may not include %s"), ZFS_SHADOW_DIR);
			return (zfs_error(hdl, EZFS_BADSHADOW, errmsg));
		}

		checkend = checkpath;
		while ((checkend = strrchr(checkend, '/')) != NULL) {
			if (checkend != checkpath)
				*checkend = '\0';
			if (strcmp(fullpath, checkpath) == 0) {
				free(checkpath);
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "path may not be along the path to %s"),
				    ZFS_SHADOW_DIR);
				return (zfs_error(hdl,
				    EZFS_BADSHADOW, errmsg));
			}
			if (checkend == checkpath)
				break;
		}
		free(checkpath);

		if ((*path = zfs_strdup(hdl, fullpath)) == NULL)
			return (-1);

	} else if (strncmp(shadow, "nfs://", 6) == 0) {
		*protocol = MNTTYPE_NFS;
		shadow += 6;

		/*
		 * For NFS mounts, we need to convert the first slash into a
		 * colon (foo/bar -> foo:/bar).
		 */
		if (shadow[0] == '/' || shadow[0] == '\0') {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "missing host name"));
			return (zfs_error(hdl, EZFS_BADPROP, errmsg));
		}

		if ((slash = strchr(shadow, '/')) == NULL) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "missing path"));
			return (zfs_error(hdl, EZFS_BADPROP, errmsg));
		}

		if ((*path = zfs_alloc(hdl, strlen(shadow) + 2)) == NULL)
			return (-1);

		len = slash - shadow;
		(void) strncpy(*path, shadow, len);
		(*path)[len] = ':';
		(void) strcpy(*path + len + 1, shadow + len);

		/*
		 * We never want to hang trying to mount the underlying source,
		 * so specify no retries.
		 */
		*mntopts = "ro,retry=0";

	} else if (strncmp(shadow, "smb://", 6) == 0) {
		*protocol = MNTTYPE_SMBFS;
		shadow += 6;

		/*
		 * For SMB mounts, there is nothing we need to do, though we
		 * check for obvious mistakes.
		 */
		if (strchr(shadow, '/') == NULL) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "incomplete path"));
			return (zfs_error(hdl, EZFS_BADPROP, errmsg));
		}

		if ((*path = zfs_strdup(hdl, shadow)) == NULL)
			return (-1);

	} else {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "unknown shadow protocol"));
		return (zfs_error(hdl, EZFS_BADPROP, errmsg));
	}

	return (0);
}

int
zfs_shadow_validate(libzfs_handle_t *hdl, const char *shadow)
{
	const char *protocol;
	const char *mntopts;
	char *path;

	if (strcmp(shadow, "none") == 0)
		return (0);

	if (zfs_shadow_parse(hdl, shadow, &protocol, &mntopts, &path) != 0)
		return (-1);

	free(path);

	return (0);
}

int
zfs_shadow_mount(zfs_handle_t *zhp, char *mountopts, boolean_t *is_shadow)
{
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	boolean_t noprop = B_FALSE;
	uint64_t guid;
	char path[PATH_MAX];
	char shadow[ZFS_MAXPROPLEN];
	const char *mnttype, *mntopts;
	char *arg, *prevshadow;
	char *argv[9];
	struct mnttab entry;
	size_t len;

	*is_shadow = B_FALSE;

	if (zfs_prop_get(zhp, ZFS_PROP_SHADOW, shadow, sizeof (shadow),
	    NULL, NULL, 0, B_FALSE) != 0 ||
	    strcmp(shadow, "none") == 0)
		noprop = B_TRUE;

	entry.mnt_mntopts = mountopts;

	/* strip any 'shadow=standby' from a mount that doesn't need it */
	if (noprop) {
		char *shadopt, *nextopt;
		if ((shadopt = hasmntopt(&entry, "shadow")) == NULL)
			return (0);
		nextopt = shadopt;
		(void) mntopt(&nextopt);
		(void) memmove(shadopt, nextopt, strlen(nextopt) + 1);
		return (0);
	}

	/*
	 * If this is a remount request, then check to see if the shadow
	 * property is changing.  If not, there's nothing left to do.
	 */
	if (hasmntopt(&entry, "remount") != NULL &&
	    libzfs_mnttab_find(hdl, zhp->zfs_name, &entry) == 0 &&
	    (prevshadow = hasmntopt(&entry, "shadow")) != NULL) {
		assert(strncmp(prevshadow, "shadow=", 7) == 0);
		prevshadow += 7;
		len = strlen(shadow);

		if (strncmp(prevshadow, shadow, len) == 0 &&
		    (prevshadow[len] == ',' || prevshadow[len] == '\0'))
			return (0);
	}

	*is_shadow = B_TRUE;

	/*
	 * Each directory is named after the GUID of the underlying filesystem.
	 */
	guid = zfs_prop_get_int(zhp, ZFS_PROP_GUID);
	(void) snprintf(path, sizeof (path), "%s/%llx", ZFS_SHADOW_DIR,
	    guid);

	/*
	 * Make sure the shadow directory exists.
	 */
	if (mkdirp(path, 0700) != 0 && errno != EEXIST) {
		zfs_error_aux(hdl, "%s", strerror(errno));
		return (zfs_error(hdl, EZFS_MOUNTFAILED,
		    dgettext(TEXT_DOMAIN,
		    "failed to create shadow directory")));
	}

	/*
	 * Attempt to unmount anything that may be there.  In general, this
	 * shouldn't be possible, but we do it just in case we died and left
	 * the system in a strange state.
	 */
	(void) umount(path);
	(void) umount2(path, MS_FORCE);

	/*
	 * If the user has manually specified a shadow setting (typically only
	 * allowed with "shadow=standby"), then don't do anything else.
	 */
	if (strstr(mountopts, "shadow=") != NULL)
		return (0);

	/*
	 * Ideally, we'd just like to call mount(2) here, but the mount(1m)
	 * command does a significant amount of additional work (particularly
	 * for smbfs) that we need in order to pass private information to the
	 * kernel.
	 *
	 * We need to exec the following command:
	 *
	 * mount -F <mnttype> -O -o ro <arg> <path>
	 *
	 * We always specify an overlay mount just in case
	 * there is a stale mount there, or someone happens to
	 * be in that current directory.  While this shouldn't
	 * happen, it shouldn't cause us to fail.
	 */
	if (zfs_shadow_parse(hdl, shadow, &mnttype, &mntopts, &arg) != 0)
		return (-1);

	argv[0] = "mount";
	argv[1] = "-F";
	argv[2] = (char *)mnttype;
	argv[3] = "-O";
	argv[4] = "-o";
	argv[5] = (char *)mntopts;
	argv[6] = arg;
	argv[7] = path;
	argv[8] = NULL;

	if (hdl->libzfs_spawn(MOUNT_COMMAND, argv,
	    hdl->libzfs_spawn_data) != 0) {
		zfs_error_aux(hdl, "mount(1m) command failed");
		free(arg);
		return (zfs_error(hdl, EZFS_SHADOWMOUNTFAILED,
		    dgettext(TEXT_DOMAIN, "failed to mount shadow directory")));
	}

	free(arg);
	if (mountopts[0] != '\0')
		(void) strcat(mountopts, ",");
	(void) strcat(mountopts, "shadow=");
	(void) strcat(mountopts, path);

	return (0);
}

static void
zfs_shadow_rmdir(zfs_handle_t *zhp)
{
	char path[PATH_MAX];
	uint64_t guid;

	guid = zfs_prop_get_int(zhp, ZFS_PROP_GUID);
	(void) snprintf(path, sizeof (path), "%s/%llx", ZFS_SHADOW_DIR,
	    guid);

	/*
	 * Attempt to unmount and remove the directory.
	 */
	(void) umount(path);
	(void) umount2(path, MS_FORCE);
	(void) rmdir(path);
}

int
zfs_shadow_unmount(zfs_handle_t *zhp)
{
	char shadow[ZFS_MAXPROPLEN];

	if (zfs_prop_get(zhp, ZFS_PROP_SHADOW, shadow, sizeof (shadow),
	    NULL, NULL, 0, B_FALSE) != 0 ||
	    strcmp(shadow, "none") == 0)
		return (0);

	zfs_shadow_rmdir(zhp);

	return (0);
}

static int
zfs_shadow_clear(zfs_handle_t *zhp)
{
	char mountpoint[PATH_MAX];

	if (zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, mountpoint,
	    sizeof (mountpoint), NULL, NULL, 0, B_FALSE) != 0)
		return (-1);

	(void) mount(zfs_get_name(zhp), mountpoint, MS_REMOUNT, MNTTYPE_ZFS,
	    NULL, 0, NULL, 0);

	zfs_shadow_rmdir(zhp);

	return (0);
}

int
zfs_shadow_change(zfs_handle_t *zhp)
{
	char shadow[ZFS_MAXPROPLEN];

	if (zfs_prop_get(zhp, ZFS_PROP_SHADOW, shadow,
	    sizeof (shadow), NULL, NULL, 0, B_FALSE) != 0)
		return (-1);

	if (strcmp(shadow, "none") == 0)
		return (zfs_shadow_clear(zhp));
	else
		return (zfs_mount(zhp, "remount", 0));
}
