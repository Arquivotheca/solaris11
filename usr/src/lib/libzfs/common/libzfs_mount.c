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
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Routines to manage ZFS mounts.  We separate all the nasty routines that have
 * to deal with the OS.  The following functions are the main entry points --
 * they are used by mount and unmount and when changing a filesystem's
 * mountpoint.
 *
 * 	zfs_is_mounted()
 * 	zfs_mount()
 * 	zfs_mountall()
 * 	zfs_unmount()
 * 	zfs_unmountall()
 *
 * This file also contains the functions used to manage sharing filesystems via
 * NFS and iSCSI:
 *
 * 	zfs_is_shared()
 * 	zfs_share()
 * 	zfs_unshare()
 *
 * 	zfs_is_shared_nfs()
 * 	zfs_is_shared_smb()
 * 	zfs_share_proto()
 * 	zfs_shareall();
 * 	zfs_unshare_nfs()
 * 	zfs_unshare_smb()
 * 	zfs_unshareall_nfs()
 *	zfs_unshareall_smb()
 *	zfs_unshareall()
 *	zfs_unshareall_bypath()
 *
 * The following functions are available for pool consumers, and will
 * mount/unmount and share/unshare all datasets within pool:
 *
 * 	zpool_enable_datasets()
 * 	zpool_disable_datasets()
 */

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <libgen.h>
#include <libintl.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <zone.h>
#include <sys/mntent.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <string.h>

#include <libzfs.h>

#include "libzfs_impl.h"

#include <sys/systeminfo.h>
#define	MAXISALEN	257	/* based on sysinfo(2) man page */

static int zfs_share_proto(zfs_handle_t *, zfs_share_proto_t *);
zfs_share_type_t zfs_is_shared_proto(zfs_handle_t *, char **,
    zfs_share_proto_t);
static boolean_t zfs_share_prop_get(zfs_handle_t *, zfs_share_proto_t,
    char **);

/*
 * The share protocols table must be in the same order as the zfs_share_prot_t
 * enum in libzfs_impl.h
 */
typedef struct {
	zfs_prop_t p_prop;
	char *p_name;
	sa_proto_t p_sa_type;
} proto_table_t;

proto_table_t proto_table[PROTO_END] = {
	{ZFS_PROP_SHARENFS, "nfs", SA_PROT_NFS},
	{ZFS_PROP_SHARESMB, "smb", SA_PROT_SMB},
};

zfs_share_proto_t nfs_only[] = {
	PROTO_NFS,
	PROTO_END
};

zfs_share_proto_t smb_only[] = {
	PROTO_SMB,
	PROTO_END
};
zfs_share_proto_t share_all_proto[] = {
	PROTO_NFS,
	PROTO_SMB,
	PROTO_END
};

/*
 * search the sharetab cache for the given mountpoint and protocol, returning
 * a zfs_share_type_t value.
 */
static zfs_share_type_t
is_shared(const char *mountpoint, zfs_share_proto_t proto)
{
	void *sa_hdl;
	nvlist_t *share;
	boolean_t found = B_FALSE;
	sa_proto_t sa_prot;
	sa_proto_t status;

	sa_prot = proto_table[proto].p_sa_type;

	if (sa_share_find_init(mountpoint, sa_prot, &sa_hdl) != SA_OK)
		return (SHARED_NOT_SHARED);

	while (!found && sa_share_find_next(sa_hdl, &share) == SA_OK) {
		status = sa_share_get_status(share);
		if (status & sa_prot)
			found = B_TRUE;
		sa_share_free(share);
	}
	sa_share_find_fini(sa_hdl);

	if (!found)
		return (SHARED_NOT_SHARED);

	switch (proto) {
	case PROTO_NFS:
		return (SHARED_NFS);
	case PROTO_SMB:
		return (SHARED_SMB);
	default:
		return (0);
	}
}

/*
 * Returns true if the specified directory is empty.  If we can't open the
 * directory at all, return true so that the mount can fail with a more
 * informative error message.
 */
static boolean_t
dir_is_empty(const char *dirname)
{
	DIR *dirp;
	struct dirent64 *dp;

	if ((dirp = opendir(dirname)) == NULL)
		return (B_TRUE);

	while ((dp = readdir64(dirp)) != NULL) {

		if (strcmp(dp->d_name, ".") == 0 ||
		    strcmp(dp->d_name, "..") == 0)
			continue;

		(void) closedir(dirp);
		return (B_FALSE);
	}

	(void) closedir(dirp);
	return (B_TRUE);
}

/*
 * Checks to see if the mount is active.  If the filesystem is mounted, we fill
 * in 'where' with the current mountpoint, and return 1.  Otherwise, we return
 * 0.
 */
boolean_t
is_mounted(libzfs_handle_t *zfs_hdl, const char *special, char **where)
{
	struct mnttab entry;

	if (libzfs_mnttab_find(zfs_hdl, special, &entry) != 0)
		return (B_FALSE);

	if (where != NULL)
		*where = zfs_strdup(zfs_hdl, entry.mnt_mountp);

	return (B_TRUE);
}

boolean_t
zfs_is_mounted(zfs_handle_t *zhp, char **where)
{
	return (is_mounted(zhp->zfs_hdl, zfs_get_name(zhp), where));
}

/*
 * Returns true if the given dataset is mountable, false otherwise.  Returns the
 * mountpoint in 'buf'.
 */
static boolean_t
zfs_is_mountable(zfs_handle_t *zhp, char *buf, size_t buflen,
    zprop_source_t *source, boolean_t is_temp)
{
	char sourceloc[ZFS_MAXNAMELEN];
	zprop_source_t sourcetype;

	if (!zfs_prop_valid_for_type(ZFS_PROP_MOUNTPOINT, zhp->zfs_type))
		return (B_FALSE);

	verify(zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, buf, buflen,
	    &sourcetype, sourceloc, sizeof (sourceloc), B_FALSE) == 0);

	if ((strcmp(buf, ZFS_MOUNTPOINT_NONE) == 0 && !is_temp) ||
	    strcmp(buf, ZFS_MOUNTPOINT_LEGACY) == 0)
		return (B_FALSE);

	if (zfs_prop_get_int(zhp, ZFS_PROP_CANMOUNT) == ZFS_CANMOUNT_OFF)
		return (B_FALSE);

	if (!is_temp && zfs_prop_get_int(zhp, ZFS_PROP_ZONED) &&
	    getzoneid() == GLOBAL_ZONEID)
		return (B_FALSE);

	if (zfs_prop_get_int(zhp, ZFS_PROP_KEYSTATUS) ==
	    ZFS_CRYPT_KEY_UNAVAILABLE)
		return (B_FALSE);

	if (source)
		*source = sourcetype;

	return (B_TRUE);
}


/*
 * Mount the given filesystem.
 */
int
zfs_mount(zfs_handle_t *zhp, const char *options, int flags)
{
	struct stat buf;
	char mountpoint[ZFS_MAXPROPLEN];
	char mntopts[MNT_LINE_MAX];
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	char *tmpmount = NULL;
	boolean_t is_tmpmount = B_FALSE;
	boolean_t is_shadow;
	zprop_source_t source;
	zfs_handle_t *parent_zhp;
	char *tmpopts, *opts;

	if (options == NULL)
		mntopts[0] = '\0';
	else
		(void) strlcpy(mntopts, options, sizeof (mntopts));

	/*
	 * If the pool is imported read-only then all mounts must be read-only
	 */
	if (zpool_get_prop_int(zhp->zpool_hdl, ZPOOL_PROP_READONLY, NULL))
		flags |= MS_RDONLY;

	/*
	 * Load encryption key if required and not already present.
	 * Don't need to check ZFS_PROP_ENCRYPTION because encrypted
	 * datasets have keystatus of ZFS_CRYPT_KEY_NONE.
	 */
	if (zfs_prop_get_int(zhp, ZFS_PROP_KEYSTATUS) ==
	    ZFS_CRYPT_KEY_UNAVAILABLE) {
		(void) zfs_key_load(zhp, B_FALSE, B_FALSE, B_FALSE);
	}

	/*
	 * See if there's a temporary mount point defined in the
	 * mount options.
	 */
	tmpopts = zfs_strdup(zhp->zfs_hdl, mntopts);
	opts = tmpopts;
	while (*opts != '\0') {
		static char *type_subopts[] = { "mountpoint", NULL };
		char *value;

		switch (getsubopt(&opts, type_subopts, &value)) {
			case 0:
				is_tmpmount = B_TRUE;
				tmpmount = zfs_strdup(zhp->zfs_hdl, value);
			}
	}
	free(tmpopts);

	/*
	 * zfs_is_mountable() checks to see whether the dataset's
	 * "mountpoint", "canmount", and "zoned" properties are consistent
	 * with being mounted.  It does not do a full evaluation of all
	 * possible obstacles to mounting.
	 */
	if (!zfs_is_mountable(zhp, mountpoint, sizeof (mountpoint), &source,
	    is_tmpmount)) {
		if (tmpmount)
			free(tmpmount);
		return (0);
	}

	if (is_tmpmount) {
		(void) strlcpy(mountpoint, tmpmount, ZFS_MAXPROPLEN);
		free(tmpmount);
	}

	/*
	 * If a dataset being mounted with a regular (i.e., not
	 * temporary) mount inherits its mountpoint from its
	 * parent, make sure the parent isn't temp-mounted.
	 */
	if (!is_tmpmount &&
	    (source == ZPROP_SRC_INHERITED || source == ZPROP_SRC_DEFAULT) &&
	    strrchr(zfs_get_name(zhp), '/') != NULL) {

		char *cpt, *parentname;
		char *where = NULL;

		/* lop off last component to construct the parent name */
		parentname = zfs_strdup(hdl, zfs_get_name(zhp));
		cpt = strrchr(parentname, '/');
		*cpt = '\0';

		if ((parent_zhp = zfs_open(zhp->zfs_hdl, parentname,
		    ZFS_TYPE_FILESYSTEM)) == NULL) {
			free(parentname);
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "failure opening parent"));
			return (zfs_error_fmt(hdl, EZFS_MOUNTFAILED,
			    dgettext(TEXT_DOMAIN, "cannot mount '%s'"),
			    zfs_get_name(zhp)));
		}

		/*
		 * Don't mount parent datasets or check for a temp-mounted
		 * parent dataset if we're operating in a local
		 * zone and the parent of the dataset is not zoned.
		 * In other words, if the parent dataset is across the "zone
		 * boundary" from the child dataset, the parent zone
		 * can't be mounted anyway, so no point in trying.  Nor is
		 * there a problem if the parent is temp-mounted.  A
		 * temporary mount in the global zone doesn't interfere
		 * with the mount in local zone.
		 */
		if (getzoneid() == GLOBAL_ZONEID ||
		    zfs_prop_get_int(parent_zhp, ZFS_PROP_ZONED)) {

			if (is_mounted(zhp->zfs_hdl, parentname, &where)) {
				free(where);
				if (zfs_prop_get_int(parent_zhp,
				    ZFS_PROP_TMPMOUNTED)) {
					zfs_close(parent_zhp);
					free(parentname);
					zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
					    "parent dataset has a temporary "
					    "mountpoint"));
					return (zfs_error_fmt(hdl,
					    EZFS_MOUNTFAILED,
					    dgettext(TEXT_DOMAIN,
					    "cannot mount '%s'"),
					    zfs_get_name(zhp)));
				}

			} else {
				/*
				 * Mount the parent dataset.  We do this to
				 * lessen the likelihood that mounts will be
				 * done out of order, which can cause
				 * mountpoint directories to contain
				 * intermediate directories, which prevents
				 * mounts from succeeding (because zfs won't do
				 * a mount on a non-empty directory).
				 *
				 * We call zfs_mount() recursively to accomplish
				 * this, working our way up the dataset
				 * hierarchy.  The recursion will stop when a
				 * dataset is reached which is already mounted
				 * or which has a "local" (i.e., not inherited)
				 * mountpoint or which has no parent (i.e.,
				 * the root of the hierarchy).  Then the
				 * recursion will unwind, mounting each dataset
				 * as it goes down the tree.  If a temporarily-
				 * mounted dataset is encountered above the one
				 * we're trying to mount, report an error and
				 * don't mount anything. We don't permit regular
				 * zfs mounts under temporary mounts.
				 */

				int tflag = 0;

				if (flags & MS_RDONLY)
					tflag = MS_RDONLY;
				if (zfs_mount(parent_zhp, NULL, tflag) != 0) {
					zfs_close(parent_zhp);
					free(parentname);
					zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
					    "failure mounting parent dataset"));
					return (zfs_error_fmt(hdl,
					    EZFS_MOUNTFAILED,
					    dgettext(TEXT_DOMAIN,
					    "cannot mount '%s'"),
					    zfs_get_name(zhp)));
				}
			}
		}
		zfs_close(parent_zhp);
		free(parentname);
	}

	/* Create the directory if it doesn't already exist */
	if (lstat(mountpoint, &buf) != 0) {
		/* temp mounts require that the mount point already exist */
		if (is_tmpmount) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "mountpoint does not exist"));
			return (zfs_error_fmt(hdl, EZFS_MOUNTFAILED,
			    dgettext(TEXT_DOMAIN, "cannot mount '%s'"),
			    mountpoint));
		}
		if (mkdirp(mountpoint, 0755) != 0) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "failed to create mountpoint"));
			return (zfs_error_fmt(hdl, EZFS_MOUNTFAILED,
			    dgettext(TEXT_DOMAIN, "cannot mount '%s'"),
			    mountpoint));
		}
	}

	/*
	 * Determine if the mountpoint is empty.  If so, refuse to perform the
	 * mount.  We don't perform this check if MS_OVERLAY is specified, which
	 * would defeat the point.  We also avoid this check if 'remount' is
	 * specified.
	 */
	if ((flags & MS_OVERLAY) == 0 &&
	    strstr(mntopts, MNTOPT_REMOUNT) == NULL &&
	    !dir_is_empty(mountpoint)) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "directory is not empty"));
		return (zfs_error_fmt(hdl, EZFS_MOUNTFAILED,
		    dgettext(TEXT_DOMAIN, "cannot mount '%s'"), mountpoint));
	}

	/*
	 * Check to see if this a shadow mount and adjust the mount options as
	 * necessary.
	 */
	if (zfs_shadow_mount(zhp, mntopts, &is_shadow) != 0)
		return (-1);

	/* perform the mount */
	if (mount(zfs_get_name(zhp), mountpoint, MS_OPTIONSTR | flags,
	    MNTTYPE_ZFS, NULL, 0, mntopts, sizeof (mntopts)) != 0) {
		/*
		 * Generic errors are nasty, but there are just way too many
		 * from mount(), and they're well-understood.  We pick a few
		 * common ones to improve upon.
		 */
		if (errno == EBUSY) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "mountpoint or dataset is busy"));
		} else if (errno == EEXIST) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "Can't mount file system, FSID conflicts "
			    "with another mounted file system"));
		} else if (errno == EPERM) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "Insufficient privileges"));
		} else if (errno == ENOTSUP) {
			char buf[256];
			int spa_version;

			VERIFY(zfs_spa_version(zhp, &spa_version) == 0);
			(void) snprintf(buf, sizeof (buf),
			    dgettext(TEXT_DOMAIN, "Can't mount a version %lld "
			    "file system on a version %d pool. Pool must be"
			    " upgraded to mount this file system."),
			    (u_longlong_t)zfs_prop_get_int(zhp,
			    ZFS_PROP_VERSION), spa_version);
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN, buf));
		} else {
			zfs_error_aux(hdl, strerror(errno));
		}

		if (is_shadow)
			(void) zfs_shadow_unmount(zhp);

		return (zfs_error_fmt(hdl, EZFS_MOUNTFAILED,
		    dgettext(TEXT_DOMAIN, "cannot mount '%s'"),
		    zhp->zfs_name));
	}

	/*
	 * Add the mounted entry into our cache.  Because it may
	 * be a remount, remove it first.
	 */
	libzfs_mnttab_remove(hdl, zfs_get_name(zhp));
	libzfs_mnttab_add(hdl, zfs_get_name(zhp), mountpoint,
	    mntopts);

	return (0);
}

/*
 * Unmount a single filesystem.
 */
static int
unmount_one(libzfs_handle_t *hdl, const char *mountpoint, int flags)
{
	if (umount2(mountpoint, flags) != 0) {
		zfs_error_aux(hdl, strerror(errno));
		return (zfs_error_fmt(hdl, EZFS_UMOUNTFAILED,
		    dgettext(TEXT_DOMAIN, "cannot unmount '%s'"),
		    mountpoint));
	}

	return (0);
}

/*
 * Unmount the given filesystem.
 */
int
zfs_unmount(zfs_handle_t *zhp, const char *mountpoint, int flags)
{
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	struct mnttab entry;
	char *mntpt = NULL;

	/* check to see if we need to unmount the filesystem */
	if (mountpoint != NULL || ((zfs_get_type(zhp) == ZFS_TYPE_FILESYSTEM) &&
	    libzfs_mnttab_find(hdl, zhp->zfs_name, &entry) == 0)) {
		/*
		 * mountpoint may have come from a call to
		 * getmnt/getmntany if it isn't NULL. If it is NULL,
		 * we know it comes from libzfs_mnttab_find which can
		 * then get freed later. We strdup it to play it safe.
		 */
		if (mountpoint == NULL)
			mntpt = zfs_strdup(hdl, entry.mnt_mountp);
		else
			mntpt = zfs_strdup(hdl, mountpoint);

		/*
		 * Unshare and unmount the filesystem
		 */
		if (zfs_unshare_proto(zhp, mntpt, share_all_proto) != 0) {
			free(mntpt);
			return (-1);
		}

		if (unmount_one(hdl, mntpt, flags) != 0) {
			free(mntpt);
			(void) zfs_shareall(zhp);
			return (-1);
		}

		/*
		 * Unmount the shadow filesystem if necessary.
		 */
		if (zfs_shadow_unmount(zhp) != 0) {
			free(mntpt);
			return (-1);
		}

		free(mntpt);

		libzfs_mnttab_remove(hdl, zhp->zfs_name);
		zhp->zfs_mntcheck = B_FALSE;
		free(zhp->zfs_mntopts);
		zhp->zfs_mntopts = NULL;
		free(zhp->zfs_mountp);
		zhp->zfs_mountp = NULL;
	}

	return (0);
}

/*
 * mount this filesystem and any children inheriting the mountpoint property.
 * To do this, just act like we're changing the mountpoint property, but don't
 * unmount the filesystems first.
 */
int
zfs_mountall(zfs_handle_t *zhp, int mflags)
{
	prop_changelist_t *clp;
	int ret;

	clp = changelist_gather(zhp, ZFS_PROP_MOUNTPOINT,
	    CL_GATHER_MOUNT_ALWAYS, mflags);
	if (clp == NULL)
		return (-1);

	ret = changelist_postfix(clp);
	changelist_free(clp);

	return (ret);
}

/*
 * Unmount this filesystem and any children inheriting the mountpoint property.
 * To do this, just act like we're changing the mountpoint property, but don't
 * remount the filesystems afterwards.
 */
int
zfs_unmountall(zfs_handle_t *zhp, int flags)
{
	prop_changelist_t *clp;
	int ret;

	if (zfs_prop_get_int(zhp, ZFS_PROP_TMPMOUNTED))
		return (zfs_unmount(zhp, NULL, flags));

	clp = changelist_gather(zhp, ZFS_PROP_MOUNTPOINT, 0, flags);
	if (clp == NULL)
		return (-1);

	ret = changelist_prefix(clp);
	changelist_free(clp);

	return (ret);
}

boolean_t
zfs_is_shared(zfs_handle_t *zhp)
{
	zfs_share_type_t rc = 0;
	zfs_share_proto_t *curr_proto;

	if (ZFS_IS_VOLUME(zhp))
		return (B_FALSE);

	for (curr_proto = share_all_proto; *curr_proto != PROTO_END;
	    curr_proto++)
		rc |= zfs_is_shared_proto(zhp, NULL, *curr_proto);

	return (rc ? B_TRUE : B_FALSE);
}

int
zfs_share(zfs_handle_t *zhp)
{
	assert(!ZFS_IS_VOLUME(zhp));
	return (zfs_share_proto(zhp, share_all_proto));
}

int
zfs_unshare(zfs_handle_t *zhp)
{
	assert(!ZFS_IS_VOLUME(zhp));
	return (zfs_unshareall(zhp));
}

/*
 * Check to see if the filesystem is currently shared.
 */
zfs_share_type_t
zfs_is_shared_proto(zfs_handle_t *zhp, char **where, zfs_share_proto_t proto)
{
	char *mountpoint;
	zfs_share_type_t rc;

	if (!zfs_is_mounted(zhp, &mountpoint))
		return (SHARED_NOT_SHARED);

	if (rc = is_shared(mountpoint, proto)) {
		if (where != NULL)
			*where = mountpoint;
		else
			free(mountpoint);
		return (rc);
	} else {
		free(mountpoint);
		return (SHARED_NOT_SHARED);
	}
}

boolean_t
zfs_is_shared_nfs(zfs_handle_t *zhp, char **where)
{
	return (zfs_is_shared_proto(zhp, where,
	    PROTO_NFS) != SHARED_NOT_SHARED);
}

boolean_t
zfs_is_shared_smb(zfs_handle_t *zhp, char **where)
{
	return (zfs_is_shared_proto(zhp, where,
	    PROTO_SMB) != SHARED_NOT_SHARED);
}

/*
 * return the value of the share property for specified protocol
 * in optstr (if non NULL). Memory for optstr is allocated and
 * must be free'd by the caller.
 *
 * Returns B_FALSE if shareopts is set to "off", B_TRUE otherwise
 */
static boolean_t
zfs_share_prop_get(zfs_handle_t *zhp, zfs_share_proto_t proto, char **optstr)
{
	int rc;
	char shareopts[ZFS_MAXPROPLEN];
	zprop_source_t sourcetype;

	rc = zfs_prop_get(zhp, proto_table[proto].p_prop,
	    shareopts, sizeof (shareopts), &sourcetype,
	    NULL, 0, B_FALSE);

	if (rc != 0) {
		if (optstr != NULL)
			*optstr = strdup("off");
		return (B_FALSE);
	} else {
		if (optstr != NULL)
			*optstr = strdup(shareopts);
		if (strcmp(shareopts, "off") == 0)
			return (B_FALSE);
		else
			return (B_TRUE);
	}
}

boolean_t
zfs_sharenfs_prop_get(zfs_handle_t *zhp, char **optstr)
{
	return (zfs_share_prop_get(zhp, PROTO_NFS, optstr));
}

boolean_t
zfs_sharesmb_prop_get(zfs_handle_t *zhp, char **optstr)
{
	return (zfs_share_prop_get(zhp, PROTO_SMB, optstr));
}

/*
 * Set sharesmb/sharenfs properties on a dataset, only if this is a Solaris box.
 *
 * This routine will not update the properties if the akd library is present.
 * If the adk library is present, the properties will remain unmodified until
 * akd updates them during the deferred updates process. This will allow
 * for a system to be rolled back to the previous os version retaining the
 * original property values.
 */
static int
zfs_share_prop_set(zfs_handle_t *zhp, zfs_prop_t prop, char *optstr)
{
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	int ret;

	if (sa_is_akd_present() || optstr == NULL)
		return (-1);

	ret = zfs_prop_set(zhp, zfs_prop_to_name(prop), optstr);
	if (ret != 0)
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "Unable to set %s property for %s"),
		    zfs_prop_to_name(prop), zfs_get_name(zhp));

	return (ret);
}

int
zfs_sharenfs_prop_set(zfs_handle_t *zhp, char *optstr)
{
	return (zfs_share_prop_set(zhp, ZFS_PROP_SHARENFS, optstr));
}

int
zfs_sharesmb_prop_set(zfs_handle_t *zhp, char *optstr)
{
	return (zfs_share_prop_set(zhp, ZFS_PROP_SHARESMB, optstr));
}

/*
 * Share the given filesystem according to the options in the specified
 * protocol specific properties (sharenfs, sharesmb).  We rely
 * on "libshare" to do the dirty work for us.
 */
static int
zfs_share_proto(zfs_handle_t *zhp, zfs_share_proto_t *proto)
{
	char mountpoint[ZFS_MAXPROPLEN];
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	zfs_share_proto_t *curr_proto;
	sa_proto_t share_prot;
	int zoned;
	int share2;
	int rc;

	if (!zfs_is_shareable(zhp, mountpoint, sizeof (mountpoint)))
		return (0);

	zoned = zfs_prop_get_int(zhp, ZFS_PROP_ZONED);

	share_prot = SA_PROT_NONE;
	for (curr_proto = proto; *curr_proto != PROTO_END; curr_proto++) {
		/*
		 * If the 'zoned' property is set, then zfs_is_shareable()
		 * will have already bailed out if we are in the global zone.
		 * NFS can be a server in a local zone, but nothing else
		 */
		if (zoned && (*curr_proto != PROTO_NFS))
			continue;

		/*
		 * Skip this protocol, if protocol is not supported
		 */
		if (sa_protocol_valid(proto_table[*curr_proto].p_name)
		    != SA_OK) {
			if (zfs_share_prop_get(zhp, *curr_proto, NULL)) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN, "%s"),
				    sa_strerror(SA_PROTO_NOT_INSTALLED));

				(void) zfs_error_fmt(hdl,
				    EZFS_SHAREFAILED, dgettext(TEXT_DOMAIN,
				    "cannot share '%s' for %s"),
				    zfs_get_name(zhp),
				    proto_table[*curr_proto].p_name);
			}
		} else {
			share_prot |=
			    proto_table[*curr_proto].p_sa_type;
		}
	}

	/*
	 * only convert share properties once
	 * create default shares as needed.
	 */
	share2 = zfs_prop_get_int(zhp, ZFS_PROP_SHARE2);
	if (share2 < ZPROP_SHARE2_CONVERTED) {
		rc = zfs_share_create_defaults(zhp, share_prot, mountpoint);

		if (rc == SA_NO_PERMISSION) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN, "%s"),
			    sa_strerror(rc));

			(void) zfs_error_fmt(hdl, EZFS_SHAREFAILED,
			    dgettext(TEXT_DOMAIN, "cannot share '%s'"),
			    zfs_get_name(zhp));
			return (-1);
		}

		if (rc == SA_OK) {
			(void) zfs_prop_set_int(zhp, ZFS_PROP_SHARE2,
			    ZPROP_SHARE2_CONVERTED);
		}
	}

	if (share_prot != SA_PROT_NONE) {
		/*
		 * Now publish all shares on this mountpoint/dataset.
		 * Wait for completion.
		 */
		if ((rc = sa_fs_publish(mountpoint, share_prot, 1)) != SA_OK) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "%s"), sa_strerror(rc));

			(void) zfs_error_fmt(hdl, EZFS_SHAREFAILED,
			    dgettext(TEXT_DOMAIN, "cannot share '%s'"),
			    zfs_get_name(zhp));
			return (-1);
		}
	}

	return (0);
}

int
zfs_share_nfs(zfs_handle_t *zhp)
{
	return (zfs_share_proto(zhp, nfs_only));
}

int
zfs_share_smb(zfs_handle_t *zhp)
{
	return (zfs_share_proto(zhp, smb_only));
}

int
zfs_shareall(zfs_handle_t *zhp)
{
	return (zfs_share_proto(zhp, share_all_proto));
}

/*
 * Unshare a filesystem by mountpoint.
 */
static int
unshare_one(libzfs_handle_t *hdl, const char *name, const char *mountpoint,
    zfs_share_proto_t *proto)
{
	zfs_share_proto_t *curr_proto;
	sa_proto_t sa_prot = SA_PROT_NONE;
	int rc;

	for (curr_proto = proto; *curr_proto != PROTO_END; curr_proto++) {

		sa_prot |= proto_table[*curr_proto].p_sa_type;
	}

	if (sa_prot == SA_PROT_NONE)
		return (0);

	if ((rc = sa_fs_unpublish(mountpoint, sa_prot, 1)) != SA_OK) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "%s"), sa_strerror(rc));

		return (zfs_error_fmt(hdl, EZFS_UNSHAREFAILED,
		    dgettext(TEXT_DOMAIN, "cannot unshare '%s'"),
		    name));
	}

	return (0);
}

/*
 * Unshare the given filesystem.
 */
int
zfs_unshare_proto(zfs_handle_t *zhp, const char *mountpoint,
    zfs_share_proto_t *proto)
{
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	struct mnttab entry;
	char *mntpt = NULL;

	/* check to see if need to unmount the filesystem */
	rewind(zhp->zfs_hdl->libzfs_mnttab);
	if (mountpoint != NULL)
		mountpoint = mntpt = zfs_strdup(hdl, mountpoint);

	if (mountpoint != NULL || ((zfs_get_type(zhp) == ZFS_TYPE_FILESYSTEM) &&
	    libzfs_mnttab_find(hdl, zfs_get_name(zhp), &entry) == 0)) {
		if (mountpoint == NULL)
			mntpt = zfs_strdup(zhp->zfs_hdl, entry.mnt_mountp);

		if (unshare_one(hdl, zhp->zfs_name, mntpt, proto) != 0) {
			if (mntpt != NULL)
				free(mntpt);
			return (-1);
		}
	}
	if (mntpt != NULL)
		free(mntpt);

	return (0);
}

int
zfs_unshare_nfs(zfs_handle_t *zhp, const char *mountpoint)
{
	return (zfs_unshare_proto(zhp, mountpoint, nfs_only));
}

int
zfs_unshare_smb(zfs_handle_t *zhp, const char *mountpoint)
{
	return (zfs_unshare_proto(zhp, mountpoint, smb_only));
}

/*
 * Same as zfs_unmountall(), but for NFS and SMB unshares.
 */
int
zfs_unshareall_proto(zfs_handle_t *zhp, zfs_share_proto_t *proto)
{
	prop_changelist_t *clp;
	int ret;

	clp = changelist_gather(zhp, ZFS_PROP_SHARENFS, 0, 0);
	if (clp == NULL)
		return (-1);

	ret = changelist_unshare(clp, proto);
	changelist_free(clp);

	return (ret);
}

int
zfs_unshareall_nfs(zfs_handle_t *zhp)
{
	return (zfs_unshareall_proto(zhp, nfs_only));
}

int
zfs_unshareall_smb(zfs_handle_t *zhp)
{
	return (zfs_unshareall_proto(zhp, smb_only));
}

int
zfs_unshareall(zfs_handle_t *zhp)
{
	return (zfs_unshareall_proto(zhp, share_all_proto));
}

int
zfs_unshareall_bypath(zfs_handle_t *zhp, const char *mountpoint)
{
	return (zfs_unshare_proto(zhp, mountpoint, share_all_proto));
}

/*
 * Remove the mountpoint associated with the current dataset, if necessary.
 * We only remove the underlying directory if:
 *
 *	- The mountpoint is not 'none' or 'legacy'
 *	- The mountpoint is non-empty
 *	- The mountpoint is the default or inherited
 *	- The 'zoned' property is set, or we're in a local zone
 *
 * Any other directories we leave alone.
 */
void
remove_mountpoint(zfs_handle_t *zhp)
{
	char mountpoint[ZFS_MAXPROPLEN];
	zprop_source_t source;

	if (!zfs_is_mountable(zhp, mountpoint, sizeof (mountpoint),
	    &source, B_FALSE))
		return;

	if (source == ZPROP_SRC_DEFAULT ||
	    source == ZPROP_SRC_INHERITED) {
		/*
		 * Try to remove the directory, silently ignoring any errors.
		 * The filesystem may have since been removed or moved around,
		 * and this error isn't really useful to the administrator in
		 * any way.
		 */
		(void) rmdir(mountpoint);
	}
}

void
libzfs_add_handle(get_all_cb_t *cbp, zfs_handle_t *zhp)
{
	if (cbp->cb_alloc == cbp->cb_used) {
		size_t newsz;
		void *ptr;

		newsz = cbp->cb_alloc ? cbp->cb_alloc * 2 : 64;
		ptr = zfs_realloc(zhp->zfs_hdl,
		    cbp->cb_handles, cbp->cb_alloc * sizeof (void *),
		    newsz * sizeof (void *));
		cbp->cb_handles = ptr;
		cbp->cb_alloc = newsz;
	}
	cbp->cb_handles[cbp->cb_used++] = zhp;
}

static int
mount_cb(zfs_handle_t *zhp, void *data)
{
	get_all_cb_t *cbp = data;

	if (zfs_prop_get_int(zhp, ZFS_PROP_KEYSTATUS) ==
	    ZFS_CRYPT_KEY_UNAVAILABLE) {
		if (zfs_key_load(zhp, B_FALSE, B_FALSE, B_TRUE) != 0) {
			zfs_close(zhp);
			return (0);
		}
	}

	if (!(zfs_get_type(zhp) & ZFS_TYPE_FILESYSTEM)) {
		zfs_close(zhp);
		return (0);
	}

	if (zfs_prop_get_int(zhp, ZFS_PROP_CANMOUNT) == ZFS_CANMOUNT_NOAUTO) {
		zfs_close(zhp);
		return (0);
	}

	libzfs_add_handle(cbp, zhp);
	if (zfs_iter_filesystems(zhp, mount_cb, cbp) != 0) {
		zfs_close(zhp);
		return (-1);
	}
	return (0);
}

int
libzfs_dataset_cmp(const void *a, const void *b)
{
	zfs_handle_t **za = (zfs_handle_t **)a;
	zfs_handle_t **zb = (zfs_handle_t **)b;
	char mounta[MAXPATHLEN];
	char mountb[MAXPATHLEN];
	boolean_t gota, gotb;

	if ((gota = (zfs_get_type(*za) == ZFS_TYPE_FILESYSTEM)) != 0)
		verify(zfs_prop_get(*za, ZFS_PROP_MOUNTPOINT, mounta,
		    sizeof (mounta), NULL, NULL, 0, B_FALSE) == 0);
	if ((gotb = (zfs_get_type(*zb) == ZFS_TYPE_FILESYSTEM)) != 0)
		verify(zfs_prop_get(*zb, ZFS_PROP_MOUNTPOINT, mountb,
		    sizeof (mountb), NULL, NULL, 0, B_FALSE) == 0);

	if (gota && gotb)
		return (strcmp(mounta, mountb));

	if (gota)
		return (-1);
	if (gotb)
		return (1);

	return (strcmp(zfs_get_name(a), zfs_get_name(b)));
}

/*
 * Mount and share all datasets within the given pool.  This assumes that no
 * datasets within the pool are currently mounted.  Because users can create
 * complicated nested hierarchies of mountpoints, we first gather all the
 * datasets and mountpoints within the pool, and sort them by mountpoint.  Once
 * we have the list of all filesystems, we iterate over them in order and mount
 * and/or share each one.
 */
#pragma weak zpool_mount_datasets = zpool_enable_datasets
int
zpool_enable_datasets(zpool_handle_t *zhp, const char *mntopts, int flags)
{
	get_all_cb_t cb = { 0 };
	libzfs_handle_t *hdl = zhp->zpool_hdl;
	zfs_handle_t *zfsp;
	int i, ret = -1;
	int *good;

	/*
	 * Gather all non-snap datasets within the pool.
	 */
	if ((zfsp = zfs_open(hdl, zhp->zpool_name, ZFS_TYPE_DATASET)) == NULL)
		goto out;

	libzfs_add_handle(&cb, zfsp);
	/*
	 * If the top level dataset is encrypted load its keys.
	 */
	if (zfs_prop_get_int(zfsp, ZFS_PROP_KEYSTATUS) ==
	    ZFS_CRYPT_KEY_UNAVAILABLE) {
		(void) zfs_key_load(zfsp, B_FALSE, B_FALSE, B_TRUE);
	}

	if (zfs_iter_filesystems(zfsp, mount_cb, &cb) != 0)
		goto out;
	/*
	 * Sort the datasets by mountpoint.
	 */
	qsort(cb.cb_handles, cb.cb_used, sizeof (void *),
	    libzfs_dataset_cmp);

	/*
	 * And mount all the datasets, keeping track of which ones succeeded or
	 * failed. Treat an already mounted dataset the same as success.
	 */
	if ((good = zfs_alloc(zhp->zpool_hdl,
	    cb.cb_used * sizeof (int))) == NULL)
		goto out;

	ret = 0;
	for (i = 0; i < cb.cb_used; i++) {
		int err = zfs_mount(cb.cb_handles[i], mntopts, flags);

		if (err != 0 && errno == EBUSY &&
		    zfs_is_mounted(cb.cb_handles[i], NULL))
			good[i] = 1;
		else if (err == 0)
			good[i] = 1;
		else
			ret = -1;
	}

	/*
	 * Then share all the ones that need to be shared. This needs
	 * to be a separate pass in order to avoid excessive reloading
	 * of the configuration. Good should never be NULL since
	 * zfs_alloc is supposed to exit if memory isn't available.
	 */
	for (i = 0; i < cb.cb_used; i++) {
		if (good[i] && zfs_share(cb.cb_handles[i]) != 0)
			ret = -1;
	}

	free(good);

out:
	for (i = 0; i < cb.cb_used; i++)
		zfs_close(cb.cb_handles[i]);
	free(cb.cb_handles);

	return (ret);
}

typedef struct share_info {
	char		*si_mountpoint;
	zfs_handle_t	*si_zhp;
} share_info_t;

static int
mountpoint_compare(const void *a, const void *b)
{
	share_info_t *sa = (share_info_t *)a;
	share_info_t *sb = (share_info_t *)b;

	return (strcmp(sb->si_mountpoint, sa->si_mountpoint));
}

/* alias for 2002/240 */
#pragma weak zpool_unmount_datasets = zpool_disable_datasets
/*
 * Unshare and unmount all datasets within the given pool.  We don't want to
 * rely on traversing the DSL to discover the filesystems within the pool,
 * because this may be expensive (if not all of them are mounted), and can fail
 * arbitrarily (on I/O error, for example).  Instead, we walk /etc/mnttab and
 * gather all the filesystems that are currently mounted.
 */
int
zpool_disable_datasets(zpool_handle_t *zhp, boolean_t force)
{
	int used, alloc;
	struct mnttab entry;
	size_t namelen;
	share_info_t *shares;
	libzfs_handle_t *hdl = zhp->zpool_hdl;
	int i;
	int ret = -1;
	int flags = (force ? MS_FORCE : 0);

	namelen = strlen(zhp->zpool_name);

	rewind(hdl->libzfs_mnttab);
	shares = NULL;
	used = alloc = 0;
	while (getmntent(hdl->libzfs_mnttab, &entry) == 0) {
		/*
		 * Ignore non-ZFS entries.
		 */
		if (entry.mnt_fstype == NULL ||
		    strcmp(entry.mnt_fstype, MNTTYPE_ZFS) != 0)
			continue;

		/*
		 * Ignore filesystems not within this pool.
		 */
		if (entry.mnt_mountp == NULL ||
		    strncmp(entry.mnt_special, zhp->zpool_name, namelen) != 0 ||
		    (entry.mnt_special[namelen] != '/' &&
		    entry.mnt_special[namelen] != '\0'))
			continue;

		/*
		 * At this point we've found a filesystem within our pool.  Add
		 * it to our growing list.
		 */
		if (used == alloc) {
			if (alloc == 0) {
				if ((shares = zfs_alloc(hdl,
				    8 * sizeof (share_info_t))) == NULL)
					goto out;

				alloc = 8;
			} else {
				void *ptr;

				if ((ptr = zfs_realloc(hdl, shares,
				    alloc * sizeof (share_info_t),
				    alloc * 2 * sizeof (share_info_t))) == NULL)
					goto out;
				shares = ptr;

				alloc *= 2;
			}
		}

		if ((shares[used].si_mountpoint = zfs_strdup(hdl,
		    entry.mnt_mountp)) == NULL)
			goto out;

		/*
		 * This is allowed to fail, in case there is some I/O error.  It
		 * is only used to determine if we need to remove the underlying
		 * mountpoint, so failure is not fatal.
		 */
		shares[used].si_zhp = make_dataset_handle(hdl,
		    entry.mnt_special);

		used++;
	}

	/*
	 * At this point, we have the entire list of filesystems, so sort it by
	 * mountpoint.
	 */
	qsort(shares, used, sizeof (share_info_t), mountpoint_compare);

	/*
	 * Walk through and first unshare everything.
	 */
	for (i = 0; i < used; i++) {
		if (unshare_one(hdl, zfs_get_name(shares[i].si_zhp),
		    shares[i].si_mountpoint, share_all_proto) != 0)
			goto out;
	}

	/*
	 * Now unmount everything, removing the underlying directories as
	 * appropriate.
	 */
	for (i = 0; i < used; i++) {
		if (unmount_one(hdl, shares[i].si_mountpoint, flags) != 0)
			goto out;

		if (zfs_shadow_unmount(shares[i].si_zhp) != 0)
			goto out;
	}
	for (i = 0; i < used; i++) {
		if (shares[i].si_zhp != NULL)
			remove_mountpoint(shares[i].si_zhp);
	}

	ret = 0;
out:
	for (i = 0; i < used; i++) {
		if (shares[i].si_zhp)
			zfs_close(shares[i].si_zhp);
		free(shares[i].si_mountpoint);
	}
	free(shares);

	return (ret);
}
