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


/*
 * file system specific functions
 */
#include <strings.h>
#include <dirent.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <link.h>
#include <libintl.h>
#include <libnvpair.h>
#include <zone.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/mntent.h>
#include <sys/systeminfo.h>

#include "libshare.h"
#include "libshare_impl.h"

int
safs_init(void)
{
	return (SA_OK);
}

int
safs_fini(void)
{
	return (SA_OK);
}

/*
 * safs_path_to_fsytpe
 *
 * Return file system type by referencing st_fsytpe returned from
 * stat() system call.  If sa_fstype determines that the path does
 * not exist, return SA_FS_NOTFOUND.
 * SA_FS_NONE is returned if the file system type can not be determined
 * or it is not supported for sharing.
 * ZFS datasets with 'legacy' mountpoints are treated like other non-zfs
 * legacy file systems. So we must check the mountpoint property for all
 * ZFS datasets.
 */
static sa_fs_t
safs_path_to_fstype(const char *path)
{
	int ret;
	char *fs_type;
	sa_fs_ops_t *ops;
	boolean_t legacy;

	ret = sa_fstype(path, &fs_type);
	if (ret == SA_PATH_NOT_FOUND)
		return (SA_FS_NOTFOUND);
	if (ret != SA_OK)
		return (SA_FS_NONE);

	if (strcmp(fs_type, MNTTYPE_ZFS) == 0) {
		ops = (sa_fs_ops_t *)saplugin_find_ops(SA_PLUGIN_FS, SA_FS_ZFS);
		if ((ops == NULL) || (ops->saf_is_legacy == NULL)) {
			ret = SA_FS_NONE;
		} else {
			ret = ops->saf_is_legacy(path, &legacy);
			if (ret == SA_OK) {
				if (legacy)
					ret = SA_FS_LEGACY;
				else
					ret = SA_FS_ZFS;
			} else {
				/*
				 * the path exists but we cannot access the
				 * zfs mountpoint property. This is most
				 * likely because the zfs dataset was added
				 * to a non-global zone and is not accessible.
				 * Treat it as a legacy file system.
				 */
				if ((ret == SA_PATH_NOT_FOUND ||
				    ret == SA_MNTPNT_NOT_FOUND) &&
				    getzoneid() != GLOBAL_ZONEID)
					ret = SA_FS_LEGACY;
				else
					ret = SA_FS_NONE;
			}
		}
	} else if (sa_fstype_is_shareable(fs_type)) {
		ret = SA_FS_LEGACY;
	} else {
		ret = SA_FS_NONE;
	}
	sa_free_fstype(fs_type);

	return (ret);
}

/*
 * safs_real_fstype
 *
 * This is similar to safs_path_to_fsype, except it does not
 * check for legacy zfs datasets. This is used for routines that
 * need to check zfs properties regardless of whether it is
 * 'legacy' or not. For example safs_is_zoned, needs to read the
 * zfs 'zoned' property even on legacy mountpoints.
 */
static sa_fs_t
safs_real_fstype(const char *path)
{
	int ret;
	char *fs_type;

	ret = sa_fstype(path, &fs_type);
	if (ret == SA_PATH_NOT_FOUND)
		return (SA_FS_NOTFOUND);
	if (ret != SA_OK)
		return (SA_FS_NONE);

	if (strcmp(fs_type, MNTTYPE_ZFS) == 0)
		ret = SA_FS_ZFS;
	else if (sa_fstype_is_shareable(fs_type))
		ret = SA_FS_LEGACY;
	else
		ret = SA_FS_NONE;

	sa_free_fstype(fs_type);

	return (ret);
}

sa_proto_t
safs_sharing_enabled(const char *mntpnt)
{
	int rc;
	sa_fs_ops_t *ops;
	sa_fs_t fstype;
	sa_proto_t protos;

	fstype = safs_path_to_fstype(mntpnt);
	if (fstype == SA_FS_NOTFOUND)
		return (SA_PROT_NONE);
	ops = (sa_fs_ops_t *)saplugin_find_ops(SA_PLUGIN_FS, fstype);
	if ((ops == NULL) || (ops->saf_sharing_enabled == NULL))
		return (SA_PROT_NONE);

	rc = ops->saf_sharing_enabled(mntpnt, &protos);
	if (rc == SA_OK)
		return (protos);
	else
		return (SA_PROT_NONE);
}

int
safs_sharing_get_prop(const char *mntpnt, sa_proto_t proto, char **props)
{
	sa_fs_ops_t *ops;
	sa_fs_t fstype;

	fstype = safs_path_to_fstype(mntpnt);
	if (fstype == SA_FS_NOTFOUND)
		return (SA_PATH_NOT_FOUND);
	ops = (sa_fs_ops_t *)saplugin_find_ops(SA_PLUGIN_FS, fstype);
	if ((ops == NULL) || (ops->saf_sharing_get_prop == NULL))
		return (SA_NOT_SUPPORTED);

	return (ops->saf_sharing_get_prop(mntpnt, proto, props));
}

/*
 * safs_sharing_set_prop
 *
 * call saf_sharing_set op for all file system plugins.
 */
int
safs_sharing_set_prop(const char *mntpnt, sa_proto_t proto, char *props)
{
	sa_fs_t fstype;
	sa_fs_ops_t *ops;

	fstype = safs_path_to_fstype(mntpnt);
	if (fstype == SA_FS_NOTFOUND)
		return (SA_PATH_NOT_FOUND);
	ops = (sa_fs_ops_t *)saplugin_find_ops(SA_PLUGIN_FS, fstype);
	if ((ops == NULL) || (ops->saf_sharing_set_prop == NULL))
		return (SA_NOT_SUPPORTED);

	return (ops->saf_sharing_set_prop(mntpnt, proto, props));
}

int
safs_share_write(nvlist_t *share)
{
	char *path;
	sa_fs_t fstype;
	sa_fs_ops_t *ops;

	if ((path = sa_share_get_path(share)) == NULL)
		return (SA_NO_SHARE_PATH);

	fstype = safs_path_to_fstype(path);
	if (fstype == SA_FS_NOTFOUND)
		return (SA_PATH_NOT_FOUND);
	ops = (sa_fs_ops_t *)saplugin_find_ops(SA_PLUGIN_FS, fstype);
	if ((ops == NULL) || (ops->saf_share_write == NULL))
		return (SA_NOT_SUPPORTED);

	return (ops->saf_share_write(share));
}

int
safs_share_read(const char *fs_name, const char *sh_name, nvlist_t **share)
{
	sa_fs_ops_t *ops;
	sa_fs_t fstype;

	fstype = safs_path_to_fstype(fs_name);
	if (fstype == SA_FS_NOTFOUND)
		return (SA_PATH_NOT_FOUND);
	ops = (sa_fs_ops_t *)saplugin_find_ops(SA_PLUGIN_FS, fstype);
	if ((ops == NULL) || (ops->saf_share_read == NULL))
		return (SA_NOT_SUPPORTED);

	return (ops->saf_share_read(fs_name, sh_name, share));
}

int
safs_share_read_init(sa_read_hdl_t *srhp)
{
	sa_fs_t fstype;
	sa_fs_ops_t *ops;

	fstype = safs_path_to_fstype(srhp->srh_mntpnt);
	if (fstype == SA_FS_NOTFOUND)
		return (SA_PATH_NOT_FOUND);
	ops = (sa_fs_ops_t *)saplugin_find_ops(SA_PLUGIN_FS, fstype);
	if ((ops == NULL) || (ops->saf_share_read_init == NULL))
		return (SA_NOT_SUPPORTED);

	return (ops->saf_share_read_init(srhp));
}

int
safs_share_read_next(sa_read_hdl_t *srhp, nvlist_t **share)
{
	sa_fs_t fstype;
	sa_fs_ops_t *ops;

	fstype = safs_path_to_fstype(srhp->srh_mntpnt);
	if (fstype == SA_FS_NOTFOUND)
		return (SA_PATH_NOT_FOUND);
	ops = (sa_fs_ops_t *)saplugin_find_ops(SA_PLUGIN_FS, fstype);
	if ((ops == NULL) || (ops->saf_share_read_next == NULL))
		return (SA_NOT_SUPPORTED);

	return (ops->saf_share_read_next(srhp, share));
}

int
safs_share_read_fini(sa_read_hdl_t *srhp)
{
	sa_fs_t fstype;
	sa_fs_ops_t *ops;

	fstype = safs_path_to_fstype(srhp->srh_mntpnt);
	if (fstype == SA_FS_NOTFOUND)
		return (SA_PATH_NOT_FOUND);
	ops = (sa_fs_ops_t *)saplugin_find_ops(SA_PLUGIN_FS, fstype);
	if ((ops == NULL) || (ops->saf_share_read_fini == NULL))
		return (SA_NOT_SUPPORTED);

	return (ops->saf_share_read_fini(srhp));
}

int
safs_share_remove(const char *sh_name, const char *fs_name)
{
	sa_fs_t fstype;
	sa_fs_ops_t *ops;

	fstype = safs_path_to_fstype(fs_name);
	if (fstype == SA_FS_NOTFOUND)
		return (SA_PATH_NOT_FOUND);
	ops = (sa_fs_ops_t *)saplugin_find_ops(SA_PLUGIN_FS, fstype);
	if ((ops == NULL) || (ops->saf_share_remove == NULL))
		return (SA_NOT_SUPPORTED);

	return (ops->saf_share_remove(fs_name, sh_name));
}

int
safs_share_get_acl(const char *sh_name, const char *sh_path, acl_t **aclp)
{
	sa_fs_t fstype;
	sa_fs_ops_t *ops;

	fstype = safs_path_to_fstype(sh_path);
	if (fstype == SA_FS_NOTFOUND)
		return (SA_PATH_NOT_FOUND);
	ops = (sa_fs_ops_t *)saplugin_find_ops(SA_PLUGIN_FS, fstype);
	if ((ops == NULL) || (ops->saf_share_get_acl == NULL))
		return (SA_NOT_SUPPORTED);

	return (ops->saf_share_get_acl(sh_name, sh_path, aclp));
}

int
safs_share_set_acl(const char *sh_name, const char *sh_path, acl_t *acl)
{
	sa_fs_t fstype;
	sa_fs_ops_t *ops;

	fstype = safs_path_to_fstype(sh_path);
	if (fstype == SA_FS_NOTFOUND)
		return (SA_PATH_NOT_FOUND);
	ops = (sa_fs_ops_t *)saplugin_find_ops(SA_PLUGIN_FS, fstype);
	if ((ops == NULL) || (ops->saf_share_set_acl == NULL))
		return (SA_NOT_SUPPORTED);

	return (ops->saf_share_set_acl(sh_name, sh_path, acl));
}

int
safs_get_mntpnt_for_path(const char *sh_path, char *mntpnt, size_t mp_len,
    char *dataset, size_t ds_len, char *mntopts, size_t opt_len)
{
	sa_fs_t fstype;
	sa_fs_ops_t *ops;

	fstype = safs_path_to_fstype(sh_path);
	if (fstype == SA_FS_NOTFOUND)
		return (SA_PATH_NOT_FOUND);
	ops = (sa_fs_ops_t *)saplugin_find_ops(SA_PLUGIN_FS, fstype);
	if ((ops == NULL) || (ops->saf_get_mntpnt_for_path == NULL))
		return (SA_NOT_SUPPORTED);

	return (ops->saf_get_mntpnt_for_path(sh_path, mntpnt, mp_len,
	    dataset, ds_len, mntopts, opt_len));
}

/*
 * enable/disable the mnttab cache for all fs plugins
 */
void
safs_mnttab_cache(boolean_t enable)
{
	sa_fs_ops_t *ops = NULL;

	while ((ops = (sa_fs_ops_t *)saplugin_next_ops(SA_PLUGIN_FS,
	    (sa_plugin_ops_t *)ops)) != NULL) {
		if (ops->saf_mnttab_cache != NULL)
			ops->saf_mnttab_cache(enable);
	}
}

boolean_t
safs_is_zoned(const char *mntpnt)
{
	sa_fs_ops_t *ops;
	sa_fs_t fstype;
	boolean_t zoned;

	fstype = safs_real_fstype(mntpnt);
	if (fstype == SA_FS_NOTFOUND)
		return (B_FALSE);
	ops = (sa_fs_ops_t *)saplugin_find_ops(SA_PLUGIN_FS, fstype);
	if ((ops == NULL) || (ops->saf_is_zoned == NULL))
		return (B_FALSE);

	if (ops->saf_is_zoned(mntpnt, &zoned) == SA_OK)
		return (zoned);
	else
		return (B_FALSE);
}
