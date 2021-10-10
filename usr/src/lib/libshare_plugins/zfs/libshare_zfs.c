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

#include <strings.h>
#include <dirent.h>
#include <libnvpair.h>
#include <libzfs.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <synch.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <libshare.h>
#include <libshare_impl.h>

#define	SA_SHARE_DIR	".zfs/shares"

#define	LOCK_LIBZFS_HDL()	VERIFY(mutex_lock(&zfs_hdl_lock) == 0)
#define	UNLOCK_LIBZFS_HDL()	VERIFY(mutex_unlock(&zfs_hdl_lock) == 0)

static libzfs_handle_t *zfs_hdl;
static mutex_t zfs_hdl_lock;
static boolean_t zfs_mnttab_cache_enabled;

static int sa_zfs_init(void);
static void sa_zfs_fini(void);
static int sa_zfs_share_write(nvlist_t *);
static int sa_zfs_share_read(const char *, const char *, nvlist_t **);
static int sa_zfs_share_read_init(sa_read_hdl_t *);
static int sa_zfs_share_read_next(sa_read_hdl_t *, nvlist_t **);
static int sa_zfs_share_read_fini(sa_read_hdl_t *);
static int sa_zfs_share_remove(const char *, const char *);
static int sa_zfs_share_get_acl(const char *, const char *, acl_t **);
static int sa_zfs_share_set_acl(const char *, const char *, acl_t *);
static int sa_zfs_get_mntpnt_for_path(const char *, char *, size_t,
    char *, size_t, char *, size_t);
static void sa_zfs_mnttab_cache(boolean_t);
static int sa_zfs_sharing_enabled(const char *, sa_proto_t *);
static int sa_zfs_sharing_get_prop(const char *, sa_proto_t, char **);
static int sa_zfs_sharing_set_prop(const char *, sa_proto_t, char *);
static int sa_zfs_is_legacy(const char *, boolean_t *);
static int sa_zfs_is_zoned(const char *, boolean_t *);

sa_fs_ops_t sa_plugin_ops = {
	.saf_hdr = {
		.pi_ptype = SA_PLUGIN_FS,
		.pi_type = SA_FS_ZFS,
		.pi_name = "zfs",
		.pi_version = SA_LIBSHARE_VERSION,
		.pi_flags = 0,
		.pi_init = sa_zfs_init,
		.pi_fini = sa_zfs_fini
	},
	.saf_share_write = sa_zfs_share_write,
	.saf_share_read = sa_zfs_share_read,
	.saf_share_read_init = sa_zfs_share_read_init,
	.saf_share_read_next = sa_zfs_share_read_next,
	.saf_share_read_fini = sa_zfs_share_read_fini,
	.saf_share_remove = sa_zfs_share_remove,
	.saf_share_get_acl = sa_zfs_share_get_acl,
	.saf_share_set_acl = sa_zfs_share_set_acl,
	.saf_get_mntpnt_for_path = sa_zfs_get_mntpnt_for_path,
	.saf_mnttab_cache = sa_zfs_mnttab_cache,
	.saf_sharing_enabled = sa_zfs_sharing_enabled,
	.saf_sharing_get_prop = sa_zfs_sharing_get_prop,
	.saf_sharing_set_prop = sa_zfs_sharing_set_prop,
	.saf_is_legacy = sa_zfs_is_legacy,
	.saf_is_zoned = sa_zfs_is_zoned
};

/*
 * shares are stored in /mountpoint/.zfs/shares
 *
 * Returns a buffer containing the share directory name
 * The buffer MUST be freed by caller.
 */
static char *
make_share_dirname(const char *mntpnt)
{
	char *dname;
	int dname_len;

	dname_len = strlen(mntpnt) + strlen("/") + strlen(SA_SHARE_DIR) + 1;

	if ((dname = malloc(dname_len)) == NULL)
		return (NULL);

	(void) snprintf(dname, dname_len, "%s/%s", mntpnt, SA_SHARE_DIR);

	return (dname);
}

/*
 * shares are stored in /mountpoint/.zfs/shares/sharename
 *
 * Returns a buffer containing the share path name
 * The buffer MUST be freed by caller.
 */
static char *
make_share_filename(const char *mntpnt, const char *sh_name)
{
	char *fname;
	int fname_len;

	fname_len = strlen(mntpnt) + strlen("/") + strlen(SA_SHARE_DIR)
	    + strlen("/") + strlen(sh_name) + 1;

	if ((fname = malloc(fname_len)) == NULL)
		return (NULL);

	(void) snprintf(fname, fname_len, "%s/%s/%s",
	    mntpnt, SA_SHARE_DIR, sh_name);

	return (fname);
}

/*
 * Called when the library is loaded.
 * Calls libzfs_init() and saves the handle (zfs_hdl)
 * until the library is unloaded.
 */
static int
sa_zfs_init(void)
{
	int rc = SA_OK;

	LOCK_LIBZFS_HDL();
	VERIFY(zfs_hdl == NULL);
	if ((zfs_hdl = libzfs_init()) == NULL) {
		salog_error(0,
		    "libshare_zfs: failed to initialize libzfs");
		rc = SA_INTERNAL_ERR;
	}
	zfs_mnttab_cache_enabled = B_FALSE;
	UNLOCK_LIBZFS_HDL();

	return (rc);
}

static void
sa_zfs_fini(void)
{
	LOCK_LIBZFS_HDL();
	if (zfs_hdl != NULL) {
		if (zfs_mnttab_cache_enabled) {
			libzfs_mnttab_cache(zfs_hdl, B_FALSE);
			zfs_mnttab_cache_enabled = B_FALSE;
		}
		libzfs_fini(zfs_hdl);
		zfs_hdl = NULL;
	}
	UNLOCK_LIBZFS_HDL();
}

/*
 * sazfs_strip_mntpnt
 *
 * Update the share with the relative share path by removing
 * the mountpoint portion from the sh_path.
 */
static int
sazfs_strip_mntpnt(nvlist_t *share, const char *sh_path,
    const char *mntpnt)
{
	int rc;
	char *new_path;

	if (strstr(sh_path, mntpnt) != sh_path)
		return (SA_INVALID_SHARE_PATH);

	new_path = (char *)(sh_path + strlen(mntpnt));

	if ((rc = sa_share_set_path(share, new_path)) != SA_OK)
		return (rc);

	return (SA_OK);
}

/*
 * sazfs_add_mntpnt
 *
 * Add the mountpoint to the share path.
 * This is called after the share has been read from disk
 * since share paths are stored relative to the mountpoint
 * incase the dataset mountpoint is modified after the
 * share has been created.
 *
 * The mountpoint itself is also added to the share in the
 * SA_PROP_MNTPNT ("mntpnt") property.
 */
static int
sazfs_add_mntpnt(nvlist_t *share, const char *mntpnt)
{
	int rc;
	char *sh_path;
	char new_path[MAXPATHLEN];

	if ((sh_path = sa_share_get_path(share)) == NULL)
		return (SA_NO_SHARE_PATH);

	(void) snprintf(new_path, MAXPATHLEN, "%s%s", mntpnt, sh_path);
	(void) sa_fixup_path(new_path);

	/*
	 * store the full path to share
	 */
	if ((rc = sa_share_set_path(share, new_path)) != SA_OK)
		return (rc);
	/*
	 * add mountpoint property to share
	 */
	if ((rc = sa_share_set_mntpnt(share, mntpnt)) != SA_OK)
		return (rc);

	return (SA_OK);
}

static int
sa_zfs_share_write(nvlist_t *share)
{
	int rc;
	char *sh_path;
	char *sh_name;
	char *sh_fname = NULL;
	char *saved_path = NULL;
	size_t buflen;
	char *bufp = NULL;
	char mntpnt[MAXPATHLEN];
	char ds_name[MAXPATHLEN];

	if ((sh_name = sa_share_get_name(share)) == NULL) {
		rc = SA_NO_SHARE_NAME;
		salog_error(rc, "libshare_zfs");
		goto out;
	}

	if ((sh_path = sa_share_get_path(share)) == NULL) {
		rc = SA_NO_SHARE_PATH;
		salog_error(rc, "libshare_zfs: %s", sh_name);
		goto out;
	}

	LOCK_LIBZFS_HDL();
	rc = zfs_path_to_mntpnt(zfs_hdl, sh_path, mntpnt, sizeof (mntpnt),
	    ds_name, sizeof (ds_name), NULL, 0);
	UNLOCK_LIBZFS_HDL();
	if (rc != 0) {
		rc = SA_MNTPNT_NOT_FOUND;
		salog_error(rc, "libshare_zfs: %s", sh_path);
		goto out;
	}

	if ((sh_fname = make_share_filename(mntpnt, sh_name)) == NULL) {
		rc = SA_NO_MEMORY;
		salog_error(rc, "libshare_zfs: %s", sh_name);
		goto out;
	}

	/*
	 * make sure the share file exists before writing to it.
	 */
	LOCK_LIBZFS_HDL();
	rc = zfs_share_resource_add(zfs_hdl, ds_name, mntpnt, sh_name);
	UNLOCK_LIBZFS_HDL();
	if (rc < 0 && errno != EEXIST) {
		switch (errno) {
		case EPERM:
			rc = SA_NO_PERMISSION;
			break;
		case EROFS:
			rc = SA_READ_ONLY;
			break;
		default:
			rc = SA_SYSTEM_ERR;
			salog_error(0, "libshare_zfs: "
			    "error creating share file: %s: %s",
			    sh_fname, strerror(errno));
			break;
		}
		goto out;
	}

	/*
	 * shares are stored with paths relative to the mountpoint.
	 * So strip the mountpoint from the share path and update share.
	 */
	if ((saved_path = strdup(sh_path)) == NULL) {
		rc = SA_NO_MEMORY;
		salog_error(rc, "libshare_zfs: %s", sh_path);
		goto out;
	}

	if ((rc = sazfs_strip_mntpnt(share, sh_path, mntpnt)) != SA_OK) {
		salog_error(rc, "libshare_zfs: %s", sh_path);
		goto out;
	}

	/*
	 * sh_path is no longer valid, because it was
	 * replaced when the relative share name was set.
	 */
	sh_path = NULL;

	/*
	 * we do not want to store the mntpnt property with the share
	 * so remove if from the list if it exists.
	 */
	(void) nvlist_remove(share, SA_PROP_MNTPNT, DATA_TYPE_STRING);

	if (nvlist_pack(share, &bufp, &buflen, NV_ENCODE_XDR, 0) != 0) {
		/*
		 * restore path, and mntpnt properties
		 */
		(void) sa_share_set_path(share, saved_path);
		(void) sa_share_set_mntpnt(share, mntpnt);
		rc = SA_XDR_ENCODE_ERR;
		salog_error(rc, "libshare_zfs: %s", sh_name);
		goto out;
	}

	/*
	 * restore path, and mntpnt properties
	 */
	(void) sa_share_set_path(share, saved_path);
	(void) sa_share_set_mntpnt(share, mntpnt);

	LOCK_LIBZFS_HDL();
	rc = zfs_share_resource_write(zfs_hdl, ds_name, mntpnt,
	    sh_name, bufp, buflen);
	UNLOCK_LIBZFS_HDL();

	if (rc < 0) {
		switch (errno) {
		case EPERM:
			rc = SA_NO_PERMISSION;
			break;
		case EROFS:
			rc = SA_READ_ONLY;
			break;
		default:
			rc = SA_SYSTEM_ERR;
			salog_error(0, "libshare_zfs: "
			    "error writing share '%s': %s",
			    sh_name, strerror(errno));
			break;
		}
		goto out;
	} else {
		rc = SA_OK;
	}

out:
	if (sh_fname != NULL)
		free(sh_fname);
	if (saved_path != NULL)
		free(saved_path);
	if (bufp != NULL)
		free(bufp);

	return (rc);
}

static int
sazfs_common_read(const char *ds_name, const char *mntpnt,
    const char *sh_name, nvlist_t **share)
{
	int rc;
	char *sh_fname;
	char *buf;
	struct stat stbuf;
	nvlist_t *nvl;

	if ((sh_fname = make_share_filename(mntpnt, sh_name)) == NULL) {
		salog_error(SA_NO_MEMORY, "libshare_zfs: %s", sh_name);
		return (SA_NO_MEMORY);
	}

	if (stat(sh_fname, &stbuf) < 0) {
		salog_debug(SA_SHARE_NOT_FOUND, "libshare_zfs: %s: %s",
		    sh_fname, strerror(errno));
		free(sh_fname);
		return (SA_SHARE_NOT_FOUND);
	}

	if (stbuf.st_size == 0) {
		salog_debug(0, "libshare_zfs: "
		    "ignoring zero length share file: %s",
		    sh_fname);
		free(sh_fname);
		return (SA_SHARE_NOT_FOUND);
	}
	free(sh_fname);

	if ((buf = malloc(stbuf.st_size)) == NULL) {
		salog_error(SA_NO_MEMORY, "libshare_zfs: %s", sh_name);
		return (SA_NO_MEMORY);
	}

	LOCK_LIBZFS_HDL();
	rc = zfs_share_resource_read(zfs_hdl, (char *)ds_name, (char *)mntpnt,
	    (char *)sh_name, buf, stbuf.st_size);
	UNLOCK_LIBZFS_HDL();

	if (rc < 0) {
		free(buf);
		switch (errno) {
		case EPERM:
			return (SA_NO_PERMISSION);
		default:
			salog_error(0, "libshare_zfs: "
			    "error reading share: %s: %s",
			    sh_name, strerror(errno));
			return (SA_SYSTEM_ERR);
		}
	}

	if ((rc = nvlist_unpack(buf, stbuf.st_size, &nvl, 0)) != 0) {
		free(buf);
		salog_error(0, "libshare_zfs: error XDR decoding share: %s: %s",
		    sh_name, strerror(rc));
		switch (rc) {
		case ENOMEM:
			return (SA_NO_MEMORY);
		case ENOTSUP:
			return (SA_NOT_SUPPORTED);
		case EFAULT:
			return (SA_XDR_DECODE_ERR);
		case EINVAL:
		default:
			return (SA_INTERNAL_ERR);
		}
	}

	free(buf);

	/*
	 * shares are stored with paths relative to the mountpoint.
	 * So prefix mountpoint to beginning of share path and update share.
	 */
	if ((rc = sazfs_add_mntpnt(nvl, mntpnt)) != SA_OK) {
		salog_error(rc, "libshare_zfs: error reading share %s",
		    sh_name);
		return (rc);
	}

	*share = nvl;
	return (SA_OK);
}

static int
sa_zfs_share_read(const char *fs_name, const char *sh_name, nvlist_t **share)
{
	int rc;
	char mntpnt[MAXPATHLEN];
	char ds_name[MAXPATHLEN];

	LOCK_LIBZFS_HDL();
	rc = zfs_path_to_mntpnt(zfs_hdl, fs_name, mntpnt, sizeof (mntpnt),
	    ds_name, sizeof (ds_name), NULL, 0);
	UNLOCK_LIBZFS_HDL();
	if (rc != 0) {
		rc = SA_MNTPNT_NOT_FOUND;
		salog_error(rc, "libshare_zfs: %s", fs_name);
		return (rc);
	}

	rc = sazfs_common_read(ds_name, mntpnt, sh_name, share);

	return (rc);
}

static int
sa_zfs_share_read_init(sa_read_hdl_t *srhp)
{
	int rc;
	char *sh_dname;
	char dataset[MAXPATHLEN];

	LOCK_LIBZFS_HDL();
	rc = zfs_path_to_mntpnt(zfs_hdl, srhp->srh_mntpnt, NULL, 0,
	    dataset, sizeof (dataset), NULL, 0);
	UNLOCK_LIBZFS_HDL();
	if (rc != 0) {
		salog_error(0, "libshare_zfs: "
		    "failed to obtain zfs dataset name for %s",
		    srhp->srh_mntpnt);
		return (SA_INTERNAL_ERR);
	}

	if ((srhp->srh_dataset = strdup(dataset)) == NULL) {
		rc = SA_NO_MEMORY;
		salog_error(rc, "libshare_zfs");
		return (rc);
	}

	if ((sh_dname = make_share_dirname(srhp->srh_mntpnt)) == NULL) {
		rc = SA_NO_MEMORY;
		salog_error(rc, "libshare_zfs");
		free(srhp->srh_dataset);
		srhp->srh_dataset = NULL;
		return (rc);
	}

	srhp->srh_dirp = opendir(sh_dname);
	if (srhp->srh_dirp == NULL) {
		free(sh_dname);
		free(srhp->srh_dataset);
		srhp->srh_dataset = NULL;
		return (SA_SHARE_NOT_FOUND);
	}
	free(sh_dname);

	return (SA_OK);
}

static int
sa_zfs_share_read_next(sa_read_hdl_t *srhp, nvlist_t **share)
{
	int rc;
	struct dirent64	*dp;

	if (srhp == NULL || srhp->srh_dirp == NULL ||
	    srhp->srh_mntpnt == NULL || srhp->srh_dataset == NULL) {
		rc = SA_INVALID_READ_HDL;
		salog_error(rc, "libshare_zfs");
		return (rc);
	}

	rc = SA_SHARE_NOT_FOUND;
	while ((dp = readdir64(srhp->srh_dirp)) != NULL) {
		if (strcmp(dp->d_name, ".") == 0 ||
		    strcmp(dp->d_name, "..") == 0) {
			continue;
		}

		rc = sazfs_common_read(srhp->srh_dataset, srhp->srh_mntpnt,
		    dp->d_name, share);
		if (rc == SA_OK) {
			return (SA_OK);
		}
	}
	(void) closedir(srhp->srh_dirp);
	srhp->srh_dirp = NULL;

	free(srhp->srh_dataset);
	srhp->srh_dataset = NULL;

	return (rc);
}

static int
sa_zfs_share_read_fini(sa_read_hdl_t *srhp)
{
	if (srhp == NULL)
		return (SA_OK);

	if (srhp->srh_dirp != NULL) {
		(void) closedir(srhp->srh_dirp);
		srhp->srh_dirp = NULL;
	}

	if (srhp->srh_dataset != NULL) {
		free(srhp->srh_dataset);
		srhp->srh_dataset = NULL;
	}

	return (SA_OK);
}

static int
sa_zfs_share_remove(const char *fs_name, const char *sh_name)
{
	int rc;
	char mntpnt[MAXPATHLEN];
	char ds_name[MAXPATHLEN];

	LOCK_LIBZFS_HDL();
	rc = zfs_path_to_mntpnt(zfs_hdl, fs_name, mntpnt, sizeof (mntpnt),
	    ds_name, sizeof (ds_name), NULL, 0);
	UNLOCK_LIBZFS_HDL();
	if (rc != 0) {
		rc = SA_MNTPNT_NOT_FOUND;
		salog_error(rc, "libshare_zfs: %s", fs_name);
		return (rc);
	}

	LOCK_LIBZFS_HDL();
	rc = zfs_share_resource_remove(zfs_hdl, ds_name, mntpnt,
	    (char *)sh_name);
	UNLOCK_LIBZFS_HDL();

	if (rc < 0) {
		switch (errno) {
		case EPERM:
			return (SA_NO_PERMISSION);
		default:
			salog_error(0, "libshare_zfs: "
			    "error removing share file for %s: %s",
			    sh_name, strerror(errno));
			return (SA_SYSTEM_ERR);
		}
	}

	return (SA_OK);
}

static int
sa_zfs_share_get_acl(const char *sh_name, const char *sh_path, acl_t **aclp)
{
	int rc;
	char *sh_fname = NULL;
	char mntpnt[MAXPATHLEN];


	LOCK_LIBZFS_HDL();
	rc = zfs_path_to_mntpnt(zfs_hdl, sh_path, mntpnt, sizeof (mntpnt),
	    NULL, 0, NULL, 0);
	UNLOCK_LIBZFS_HDL();
	if (rc != 0) {
		rc = SA_MNTPNT_NOT_FOUND;
		salog_error(rc, "libshare_zfs: %s", sh_path);
		return (rc);
	}

	if ((sh_fname = make_share_filename(mntpnt, sh_name)) == NULL) {
		rc = SA_NO_MEMORY;
		salog_error(rc, "libshare_zfs: %s", sh_name);
		return (rc);
	}

	rc = acl_get(sh_fname, 0, aclp);
	if (rc != 0) {
		switch (errno) {
		case EACCES:
			rc = SA_NO_PERMISSION;
			break;
		case ENOENT:
			rc = SA_INVALID_SHARE_PATH;
			break;
		case ENOTSUP:
			rc = SA_NOT_SUPPORTED;
			break;
		case EIO:
		case ENOSYS:
		default:
			rc = SA_SYSTEM_ERR;
			break;
		}
	} else {
		rc = SA_OK;
	}

	if (sh_fname != NULL)
		free(sh_fname);
	return (rc);
}

static int
sa_zfs_share_set_acl(const char *sh_name, const char *sh_path, acl_t *acl)
{
	int rc;
	char *sh_fname = NULL;
	char mntpnt[MAXPATHLEN];

	LOCK_LIBZFS_HDL();
	rc = zfs_path_to_mntpnt(zfs_hdl, sh_path, mntpnt, sizeof (mntpnt),
	    NULL, 0, NULL, 0);
	UNLOCK_LIBZFS_HDL();
	if (rc != 0) {
		rc = SA_MNTPNT_NOT_FOUND;
		salog_error(rc, "libshare_zfs: %s", sh_path);
		return (rc);
	}

	if ((sh_fname = make_share_filename(mntpnt, sh_name)) == NULL) {
		rc = SA_NO_MEMORY;
		salog_error(rc, "libshare_zfs: %s", sh_name);
		return (rc);
	}

	rc = acl_set(sh_fname, acl);
	if (rc != 0) {
		switch (errno) {
		case EACCES:
			rc = SA_NO_PERMISSION;
			break;
		case ENOENT:
			rc = SA_INVALID_SHARE_PATH;
			break;
		case ENOTSUP:
			rc = SA_NOT_SUPPORTED;
			break;
		case EIO:
		case ENOSYS:
		default:
			rc = SA_SYSTEM_ERR;
			break;
		}
	} else {
		rc = SA_OK;
	}

	if (sh_fname != NULL)
		free(sh_fname);

	return (rc);
}

/*
 * Return the mountpoint and optionally the dataset name from a
 * path.
 */
static int
sa_zfs_get_mntpnt_for_path(const char *sh_path, char *mntpnt, size_t mplen,
    char *dataset, size_t dslen, char *mntopt, size_t optlen)
{
	int rc;

	LOCK_LIBZFS_HDL();
	rc = zfs_path_to_mntpnt(zfs_hdl, sh_path, mntpnt, mplen,
	    dataset, dslen, mntopt, optlen);
	UNLOCK_LIBZFS_HDL();

	if (rc != 0)
		return (SA_MNTPNT_NOT_FOUND);
	else
		return (SA_OK);
}

static void
sa_zfs_mnttab_cache(boolean_t enabled)
{
	LOCK_LIBZFS_HDL();
	libzfs_mnttab_cache(zfs_hdl, enabled);
	zfs_mnttab_cache_enabled = enabled;
	UNLOCK_LIBZFS_HDL();
}

static int
sa_zfs_sharing_enabled(const char *sh_path, sa_proto_t *protos)
{
	zfs_handle_t *zhp;

	*protos = SA_PROT_NONE;

	LOCK_LIBZFS_HDL();
	zhp = zfs_path_to_zhandle(zfs_hdl, (char *)sh_path,
	    ZFS_TYPE_FILESYSTEM);
	UNLOCK_LIBZFS_HDL();

	if (zhp == NULL)
		return (SA_OK);

	if (zfs_sharenfs_prop_get(zhp, NULL))
		*protos |= SA_PROT_NFS;

	if (zfs_sharesmb_prop_get(zhp, NULL))
		*protos |= SA_PROT_SMB;

	zfs_close(zhp);
	return (SA_OK);
}

static int
sa_zfs_sharing_get_prop(const char *sh_path, sa_proto_t proto, char **props)
{
	zfs_handle_t *zhp;
	int rc;

	LOCK_LIBZFS_HDL();
	zhp = zfs_path_to_zhandle(zfs_hdl, (char *)sh_path,
	    ZFS_TYPE_FILESYSTEM);
	UNLOCK_LIBZFS_HDL();

	if (zhp == NULL)
		return (SA_INTERNAL_ERR);

	rc = SA_OK;
	if (proto == SA_PROT_NFS)
		(void) zfs_sharenfs_prop_get(zhp, props);
	else if (proto == SA_PROT_SMB)
		(void) zfs_sharesmb_prop_get(zhp, props);
	else {
		zfs_close(zhp);
		return (SA_NO_SUCH_PROTO);
	}

	if (props && *props == NULL)
		rc = SA_NO_MEMORY;

	zfs_close(zhp);
	return (rc);
}

static int
sa_zfs_sharing_set_prop(const char *mntpnt, sa_proto_t proto, char *props)
{
	zfs_handle_t *zhp;
	int rc, err = 0;

	LOCK_LIBZFS_HDL();
	zhp = zfs_path_to_zhandle(zfs_hdl, (char *)mntpnt, ZFS_TYPE_FILESYSTEM);
	UNLOCK_LIBZFS_HDL();

	if (zhp == NULL)
		return (SA_INTERNAL_ERR);

	errno = 0;
	switch (proto) {
	case SA_PROT_NFS:
		rc = zfs_sharenfs_prop_set(zhp, props);
		break;
	case SA_PROT_SMB:
		rc = zfs_sharesmb_prop_set(zhp, props);
		break;
	default:
		rc = -1;
		errno = EINVAL;
		break;
	}

	if (rc != 0)
		err = errno;

	zfs_close(zhp);

	switch (err) {
	case 0:
		return (SA_OK);
	case ENOMEM:
		return (SA_NO_MEMORY);
	case EINVAL:
		return (SA_INVALID_PROP);
	case EPERM:
	default:
		return (SA_NO_PERMISSION);
	}
}

static int
sa_zfs_is_legacy(const char *sh_path, boolean_t *legacy)
{
	zfs_handle_t *zhp;
	char mountpoint[ZFS_MAXPROPLEN];
	int rc;

	LOCK_LIBZFS_HDL();
	zhp = zfs_path_to_zhandle(zfs_hdl, (char *)sh_path,
	    ZFS_TYPE_FILESYSTEM);
	UNLOCK_LIBZFS_HDL();

	if (zhp == NULL) {
		*legacy = B_TRUE;
		return (SA_PATH_NOT_FOUND);
	}

	if (zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, mountpoint,
	    sizeof (mountpoint), NULL, NULL, 0, B_FALSE) == 0) {
		if (strcmp(mountpoint, ZFS_MOUNTPOINT_LEGACY) == 0)
			*legacy = B_TRUE;
		else
			*legacy = B_FALSE;
		rc = SA_OK;
	} else {
		*legacy = B_TRUE;
		rc = SA_MNTPNT_NOT_FOUND;
	}

	zfs_close(zhp);
	return (rc);
}

static int
sa_zfs_is_zoned(const char *mntpnt, boolean_t *zoned)
{
	zfs_handle_t *zhp;

	LOCK_LIBZFS_HDL();
	zhp = zfs_path_to_zhandle(zfs_hdl, (char *)mntpnt, ZFS_TYPE_FILESYSTEM);
	UNLOCK_LIBZFS_HDL();

	if (zhp == NULL) {
		*zoned = B_FALSE;
		return (SA_MNTPNT_NOT_FOUND);
	}

	*zoned = zfs_prop_get_int(zhp, ZFS_PROP_ZONED);

	zfs_close(zhp);

	return (SA_OK);
}
