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
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * System includes
 */
#include <assert.h>
#include <errno.h>
#include <libintl.h>
#include <libnvpair.h>
#include <libzfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mntent.h>
#include <sys/mnttab.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/vfstab.h>
#include <unistd.h>

#include <libbe.h>
#include <libbe_priv.h>

typedef struct active_zone_root_data {
	uuid_t	parent_uuid;
	char	*zoneroot_ds;
} active_zone_root_data_t;

typedef struct mounted_zone_root_data {
	char	*zone_altroot;
	char	*zoneroot_ds;
} mounted_zone_root_data_t;

/* Private function prototypes */
static int be_find_active_zone_root_callback(zfs_handle_t *, void *);
static int be_find_mounted_zone_root_callback(zfs_handle_t *, void *);

/* ******************************************************************** */
/*			Semi-Private Functions				*/
/* ******************************************************************** */

/*
 * Function:	be_make_zoneroot
 * Description:	Generate a string for a zone's zoneroot given the
 *		zone's zonepath.
 * Parameters:
 *		zonepath - pointer to zonepath
 *		zoneroot - pointer to buffer to return zoneroot in.
 *		zoneroot_size - size of zoneroot
 * Returns:
 *		None
 * Scope:
 *		Semi-private (library wise use only)
 */
void
be_make_zoneroot(char *zonepath, char *zoneroot, int zoneroot_size)
{
	(void) snprintf(zoneroot, zoneroot_size, "%s/root", zonepath);
}

/*
 * Function:	be_zone_get_zpool_analog
 * Description: This function gets the zpool 'analog' for the currently
 *              booted Solaris Container.  The zpool 'analog' is the
 *		solaris container equivalent to the zpool portion of a global
 *		zone BE's root dataset.  For a non-global zone BE dataset
 *		of rpool/export/zones/zone0/rpool/ROOT/zbe-xxx the
 *		'zpool analog' is rpool/export/zones/zone0/rpool.
 * Parameters:
 *              p_analog - pointer to a char * to return the zpool analog
 * Returns:
 *		BE_SUCCESS - Success
 *		be_errno_t - Failure
 * Scope:
 *		Private
 */
int
be_zone_get_zpool_analog(char *p_analog)
{
	char *root_ds = NULL;
	char *ptr = NULL;
	char be_name[ZFS_MAXNAMELEN];
	size_t pool_len;

	root_ds = be_get_ds_from_dir("/");
	if (root_ds == NULL) { /* we must be in a scratch mounted zone */
		be_print_err(gettext("be_zone_get_zpool_analog: Operation not "
		    "supported in a scratch mounted zone.\n"));
		return (BE_ERR_ZONE_NOTSUP);
	}

	ptr = strrchr(root_ds, '/');
	(void) strcpy(be_name, ptr);
	pool_len = strlen(root_ds) - (strlen(BE_CONTAINER_DS_NAME) +
	    strlen(be_name));
	(void) strlcpy(p_analog, root_ds, pool_len);

	return (BE_SUCCESS);
}

/*
 * Function:	be_zone_toggle_active
 * Description: This function sets or unsets the active property of
 *              a zone root dataset.
 * Parameters:
 *              root_ds - Root dataset of the nested BE to set active
 * Returns:
 *              be_errno_t - Failure
 *              BE_SUCCESS - Success
 * Scope:
 *		Private
 */
int
be_zone_toggle_active(char *root_ds)
{
	zfs_handle_t    *zhp = NULL;
	int		ret = BE_SUCCESS;

	/* Get handle to the BE's root dataset */
	if ((zhp = zfs_open(g_zfs, root_ds, ZFS_TYPE_FILESYSTEM)) == NULL) {
		be_print_err(gettext("be_zone_toggle_active: failed to open BE "
		    "root dataset (%s): %s\n"), root_ds,
		    libzfs_error_description(g_zfs));
		return (zfs_err_to_be_err(g_zfs));
	}

	if (be_zone_is_active(zhp)) {
		if (zfs_prop_set(zhp, BE_ZONE_ACTIVE_PROPERTY, "off") != 0) {
			be_print_err(gettext("be_zone_toggle_active: failed to "
			    "unset active property for BE: %s\n"),
			    zfs_get_name(zhp));
			ret = zfs_err_to_be_err(g_zfs);
			goto done;
		}
	} else {
		if (zfs_prop_set(zhp, BE_ZONE_ACTIVE_PROPERTY, "on") != 0) {
			be_print_err(gettext("be_zone_toggle_active: failed to "
			    "set active property for BE: %s\n"),
			    zfs_get_name(zhp));
			ret = zfs_err_to_be_err(g_zfs);
		}
	}

done:
	ZFS_CLOSE(zhp);
	return (ret);
}

/*
 * Function:	be_find_active_zone_root
 * Description:	This function will find the active zone root of a zone for
 *		a given global BE when called in a global zone context.
 *		In a local zone context it will find the active zone BE.
 *		It will iterate all of the zone roots under a zonepath, find the
 *		zone roots that belong to the specified global BE, and return
 *		the one that is active.
 * Parameters:
 *		be_zhp - zfs handle to global BE root dataset.
 *		zonepath_ds - pointer to zone's zonepath dataset.  In a local
 *		zone context, this is equivalent to the zone's 'zpool analog'.
 *		zoneroot_ds - pointer to a buffer to store the dataset name of
 *			the zone's zoneroot that's currently active for this
 *			given global zone BE.
 *		zoneroot-ds_size - size of zoneroot_ds.
 * Returns:
 *		BE_SUCCESS - Success
 *		be_errno_t - Failure
 * Scope:
 *		Semi-private (library wide use only)
 */
int
be_find_active_zone_root(zfs_handle_t *be_zhp, char *zonepath_ds,
    char *zoneroot_ds, int zoneroot_ds_size)
{
	active_zone_root_data_t		azr_data = { 0 };
	zfs_handle_t			*zhp;
	char				zone_container_ds[MAXPATHLEN];
	char				zone_rpool_ds[MAXPATHLEN];
	int				ret = BE_SUCCESS;

	if (getzoneid() == GLOBAL_ZONEID) {
		/* Get the uuid of the parent global BE */
		if ((ret = be_get_uuid(zfs_get_name(be_zhp),
		    &azr_data.parent_uuid)) != BE_SUCCESS) {
			be_print_err(gettext("be_find_active_zone_root: failed "
			    "to get uuid for BE root dataset %s\n"),
			    zfs_get_name(be_zhp));
			return (ret);
		}

		/*
		 * Generate string for the root container dataset for this
		 * zone.
		 */
		be_make_nested_container_ds(zonepath_ds, zone_rpool_ds,
		    sizeof (zone_rpool_ds));
		be_make_container_ds(zone_rpool_ds, zone_container_ds,
		    sizeof (zone_container_ds));
	} else {
		if ((ret = be_zone_get_parent_id(zfs_get_name(be_zhp),
		    &azr_data.parent_uuid)) != BE_SUCCESS) {
			be_print_err(gettext("be_find_active_zone_root: "
			    "failed to get parentbe uuid for BE root "
			    "dataset %s\n"), zfs_get_name(be_zhp));
			return (ret);
		}

		be_make_container_ds(zonepath_ds, zone_container_ds,
		    sizeof (zone_container_ds));
	}

	/* Get handle to this zone's root container dataset */
	if ((zhp = zfs_open(g_zfs, zone_container_ds, ZFS_TYPE_FILESYSTEM))
	    == NULL) {
		be_print_err(gettext("be_find_active_zone_root: failed to "
		    "open zone root container dataset (%s): %s\n"),
		    zone_container_ds, libzfs_error_description(g_zfs));
		return (zfs_err_to_be_err(g_zfs));
	}

	/*
	 * Iterate through all of this zone's BEs, looking for ones
	 * that belong to the parent global BE, and finding the one
	 * that is marked active.
	 */
	if ((ret = zfs_iter_filesystems(zhp, be_find_active_zone_root_callback,
	    &azr_data)) != 0) {
		be_print_err(gettext("be_find_active_zone_root: failed to "
		    "find active zone root in zonepath dataset %s: %s\n"),
		    zonepath_ds, be_err_to_str(ret));
		goto done;
	}

	if (azr_data.zoneroot_ds != NULL) {
		(void) strlcpy(zoneroot_ds, azr_data.zoneroot_ds,
		    zoneroot_ds_size);
		free(azr_data.zoneroot_ds);
	} else {
		be_print_err(gettext("be_find_active_zone_root: failed to "
		    "find active zone root in zonepath dataset %s\n"),
		    zonepath_ds);
		ret = BE_ERR_ZONE_NO_ACTIVE_ROOT;
	}

done:
	ZFS_CLOSE(zhp);
	return (ret);
}

/*
 * Function:	be_find_mounted_zone_root
 * Description:	This function will find the dataset mounted as the zoneroot
 *		of a zone for a given mounted global BE.
 * Parameters:
 *		zone_altroot - path of zoneroot wrt the mounted global BE.
 *		zonepath_ds - dataset of the zone's zonepath.
 *		zoneroot_ds - pointer to a buffer to store the dataset of
 *			the zoneroot that currently mounted for this zone
 *			in the mounted global BE.
 *		zoneroot_ds_size - size of zoneroot_ds
 * Returns:
 *		BE_SUCCESS - Success
 *		be_errno_t - Failure
 * Scope:
 *		Semi-private (library wide use only)
 */
int
be_find_mounted_zone_root(char *zone_altroot, char *zonepath_ds,
    char *zoneroot_ds, int zoneroot_ds_size)
{
	mounted_zone_root_data_t	mzr_data = { 0 };
	zfs_handle_t	*zhp = NULL;
	char		zone_container_ds[MAXPATHLEN];
	char		zone_rpool_ds[MAXPATHLEN];
	int		ret = BE_SUCCESS;
	int		zret = 0;

	/* Generate string for the root container dataset for this zone. */
	be_make_nested_container_ds(zonepath_ds, zone_rpool_ds,
	    sizeof (zone_rpool_ds));
	be_make_container_ds(zone_rpool_ds, zone_container_ds,
	    sizeof (zone_container_ds));

	/* Get handle to this zone's root container dataset. */
	if ((zhp = zfs_open(g_zfs, zone_container_ds, ZFS_TYPE_FILESYSTEM))
	    == NULL) {
		be_print_err(gettext("be_find_mounted_zone_root: failed to "
		    "open zone root container dataset (%s): %s\n"),
		    zone_container_ds, libzfs_error_description(g_zfs));
		return (zfs_err_to_be_err(g_zfs));
	}

	mzr_data.zone_altroot = zone_altroot;

	/*
	 * Iterate through all of the zone's BEs, looking for the one
	 * that is currently mounted at the zone altroot in the mounted
	 * global BE.
	 */
	if ((zret = zfs_iter_filesystems(zhp,
	    be_find_mounted_zone_root_callback, &mzr_data)) == 0) {
		be_print_err(gettext("be_find_mounted_zone_root: did not "
		    "find mounted zone under altroot zonepath %s\n"),
		    zonepath_ds);
		ret = BE_ERR_NO_MOUNTED_ZONE;
		goto done;
	} else if (zret < 0) {
		be_print_err(gettext("be_find_mounted_zone_root: "
		    "zfs_iter_filesystems failed: %s\n"),
		    libzfs_error_description(g_zfs));
		ret = zfs_err_to_be_err(g_zfs);
		goto done;
	}

	if (mzr_data.zoneroot_ds != NULL) {
		(void) strlcpy(zoneroot_ds, mzr_data.zoneroot_ds,
		    zoneroot_ds_size);
		free(mzr_data.zoneroot_ds);
	}

done:
	ZFS_CLOSE(zhp);
	return (ret);
}

/*
 * Function:	be_zone_supported
 * Description:	This function will determine if a zone is supported
 *		based on its zonepath dataset.  The zonepath dataset
 *		must:
 *		   - not be under any global BE root dataset.
 *		   - have a root container dataset underneath it.
 *
 * Parameters:
 *		zonepath_ds - name of dataset of the zonepath of the
 *		zone to check.
 * Returns:
 *		B_TRUE - zone is supported
 *		B_FALSE - zone is not supported
 * Scope:
 *		Semi-private (library wide use only)
 */
boolean_t
be_zone_supported(char *zonepath_ds)
{
	char	zone_container_ds[MAXPATHLEN];
	char	zone_rpool_ds[MAXPATHLEN];
	int	ret = 0;

	/*
	 * Make sure the dataset for the zonepath is not hierarchically
	 * under any reserved BE root container dataset of any pool.
	 */
	if ((ret = zpool_iter(g_zfs, be_check_be_roots_callback,
	    zonepath_ds)) > 0) {
		be_print_err(gettext("be_zone_supported: "
		    "zonepath dataset %s not supported\n"), zonepath_ds);
		return (B_FALSE);
	} else if (ret < 0) {
		be_print_err(gettext("be_zone_supported: "
		"zpool_iter failed: %s\n"),
		    libzfs_error_description(g_zfs));
		return (B_FALSE);
	}

	/*
	 * Make sure the zonepath has a zone root container dataset
	 * underneath it.
	 */
	be_make_nested_container_ds(zonepath_ds, zone_rpool_ds,
	    sizeof (zone_rpool_ds));
	be_make_container_ds(zone_rpool_ds, zone_container_ds,
	    sizeof (zone_container_ds));

	if (!zfs_dataset_exists(g_zfs, zone_container_ds,
	    ZFS_TYPE_FILESYSTEM)) {
		be_print_err(gettext("be_zone_supported: "
		    "zonepath dataset (%s) does not have a zone root container "
		    "dataset, zone is not supported, skipping ...\n"),
		    zonepath_ds);
		return (B_FALSE);
	}

	return (B_TRUE);
}

/*
 * Function:	be_get_supported_brandlist
 * Desciption:	This functions retuns a list of supported brands in
 *		a zoneBrandList_t object.
 * Parameters:
 *		None
 * Returns:
 *		Failure - NULL if no supported brands found.
 *		Success - pointer to zoneBrandList structure.
 * Scope:
 *		Semi-private (library wide use only)
 */
zoneBrandList_t *
be_get_supported_brandlist(void)
{
	return (z_make_brand_list(BE_ZONE_SUPPORTED_BRANDS,
	    BE_ZONE_SUPPORTED_BRANDS_DELIM));
}

/*
 * Function:	be_zone_is_bootable
 * Description:	This function checks to see if a BE root dataset
 *		is bootable (ie is associated with the current
 *		active global zone).
 * Parameters:
 *		root_ds - dataset name of a zone root dataset
 * Returns:
 *		B_TRUE - Success, BE is bootable
 *		B_FALSE - Failure, BE is not bootable
 * Scope:
 *		Private
 */
boolean_t
be_zone_is_bootable(char *root_ds) {
	uuid_t		cbe_parentbe_id = { 0 };
	uuid_t		nbe_parentbe_id = { 0 };

	if (be_zone_get_parent_id(be_get_ds_from_dir("/"),
		&cbe_parentbe_id) != BE_SUCCESS) {
		be_print_err(gettext("be_zone_is_bootable: failed to get "
			"current be parent id\n"));
		return (B_FALSE);
	}

	if (be_zone_get_parent_id(root_ds, &nbe_parentbe_id) != BE_SUCCESS) {
		be_print_err(gettext("be_zone_is_bootable: failed to get "
			"new be parent id\n"));
		return (B_FALSE);
	}

	if (uuid_compare(nbe_parentbe_id, cbe_parentbe_id) != 0) {
		return (B_FALSE);
	}

	return (B_TRUE);
}

/*
 * Function:	be_zone_get_parent_id
 * Description:	This function gets the parentbe property of a zone root
 *		dataset, parsed it into internal uuid format, and returns
 *		it in the uuid_t reference pointer passed in.
 * Parameters:
 *		root_ds - dataset name of a zone root dataset
 *		uu - pointer to a uuid_t to return the parentbe uuid in
 * Returns:
 *		BE_SUCCESS - Success
 *		be_errno_t - Failure
 * Scope:
 *		Private
 */
int
be_zone_get_parent_id(const char *root_ds, uuid_t *uu)
{
	zfs_handle_t	*zhp = NULL;
	nvlist_t	*userprops = NULL;
	nvlist_t	*propname = NULL;
	char		*uu_string = NULL;
	int		ret = BE_SUCCESS;

	/* Get handle to zone root dataset */
	if ((zhp = zfs_open(g_zfs, root_ds, ZFS_TYPE_FILESYSTEM)) == NULL) {
		be_print_err(gettext("be_zone_get_parent_id: failed to "
		    "open zone root dataset (%s): %s\n"), root_ds,
		    libzfs_error_description(g_zfs));
		return (zfs_err_to_be_err(g_zfs));
	}

	/* Get user properties for zone root dataset */
	if ((userprops = zfs_get_user_props(zhp)) == NULL) {
		be_print_err(gettext("be_zone_get_parent_id: "
		    "failed to get user properties for zone root "
		    "dataset (%s): %s\n"), root_ds,
		    libzfs_error_description(g_zfs));
		ret = zfs_err_to_be_err(g_zfs);
		goto done;
	}

	/* Get UUID string from zone's root dataset user properties */
	if (nvlist_lookup_nvlist(userprops, BE_ZONE_PARENTBE_PROPERTY,
	    &propname) != 0 || nvlist_lookup_string(propname, ZPROP_VALUE,
	    &uu_string) != 0) {
		be_print_err(gettext("be_zone_get_parent_id: failed to "
		    "get parent uuid property from zone root dataset user "
		    "properties.\n"));
		ret = BE_ERR_ZONE_NO_PARENTBE;
		goto done;
	}

	/* Parse the uuid string into internal format */
	if (uuid_parse(uu_string, *uu) != 0 || uuid_is_null(*uu)) {
		be_print_err(gettext("be_zone_get_parent_id: failed to "
		    "parse parentuuid\n"));
		ret = BE_ERR_PARSE_UUID;
	}

done:
	ZFS_CLOSE(zhp);
	return (ret);
}

/*
 * Function:	be_zone_set_parent_id
 * Description: This function sets the parent uuid property of a zone root
 *		dataset.
 * Parameters:
 *              root_ds - Root dataset of the BE to set active
 *              parent_uuid - Value for parent uuid property
 * Returns:
 *              be_errno_t - Failure
 *              BE_SUCCESS - Success
 * Scope:
 *		Private
 */
int
be_zone_set_parent_id(const char *root_ds, uuid_t pud)
{
	zfs_handle_t    *zhp = NULL;
	int		ret = BE_SUCCESS;
	char		pud_string[UUID_PRINTABLE_STRING_LENGTH] = { 0 };

	/* Get handle to the BE's root dataset */
	if ((zhp = zfs_open(g_zfs, root_ds, ZFS_TYPE_FILESYSTEM)) == NULL) {
		be_print_err(gettext("be_zone_set_parent_id: failed to open "
		    "BE root dataset (%s): %s\n"), root_ds,
		    libzfs_error_description(g_zfs));
		return (zfs_err_to_be_err(g_zfs));
	}

	uuid_unparse(pud, pud_string);

	if (zfs_prop_set(zhp, BE_ZONE_PARENTBE_PROPERTY, pud_string) != 0) {
		be_print_err(gettext("be_zone_set_parent_id: failed to set "
		    "parent id property for BE: %s\n"),
		    libzfs_error_description(g_zfs));
		ret = zfs_err_to_be_err(g_zfs);
	}

	ZFS_CLOSE(zhp);
	return (ret);
}
/* ******************************************************************** */
/*			Private Functions				*/
/* ******************************************************************** */

/*
 * Function:	be_find_active_zone_root_callback
 * Description: This function is used as a callback to iterate over all of
 *		a zone's root datasets, finding the one that is marked active
 *		for the parent BE specified in the data passed in.  The name
 *		of the zone's active root dataset is returned in heap storage
 *		in the active_zone_root_data_t structure passed in, so the
 *		caller is responsible for freeing it.
 * Parameters:
 *		zhp - zfs_handle_t pointer to current dataset being processed
 *		data - active_zone_root_data_t pointer
 * Returns:
 *		0 - Success
 *		>0 - Failure
 * Scope:
 *		Private
 */
static int
be_find_active_zone_root_callback(zfs_handle_t *zhp, void *data)
{
	active_zone_root_data_t	*azr_data = data;
	uuid_t			parent_uuid = { 0 };
	int			iret = 0;
	int			ret = 0;

	if ((iret = be_zone_get_parent_id(zfs_get_name(zhp), &parent_uuid))
	    != BE_SUCCESS) {
		be_print_err(gettext("be_find_active_zone_root_callback: "
		    "skipping zone root dataset (%s): %s\n"),
		    zfs_get_name(zhp), be_err_to_str(iret));
		goto done;
	}

	if (uuid_compare(azr_data->parent_uuid, parent_uuid) == 0) {
		/*
		 * Found a zone root dataset belonging to the right parent,
		 * check if its active.
		 */
		if (be_zone_is_active(zhp)) {
			/*
			 * Found active zone root dataset, if its already
			 * set in the callback data, that means this
			 * is the second one we've found.  Return error.
			 */
			if (azr_data->zoneroot_ds != NULL) {
				ret = BE_ERR_ZONE_MULTIPLE_ACTIVE;
				goto done;
			}

			azr_data->zoneroot_ds = strdup(zfs_get_name(zhp));
			if (azr_data->zoneroot_ds == NULL) {
				ret = BE_ERR_NOMEM;
			}
		}
	}

done:
	ZFS_CLOSE(zhp);
	return (ret);
}

/*
 * Function:	be_find_mounted_zone_root_callback
 * Description:	This function is used as a callback to iterate over all of
 *		a zone's root datasets, find the one that is currently
 *		mounted for the parent BE specified in the data passed in.
 *		The name of the zone's mounted root dataset is returned in
 *		heap storage the mounted_zone_data_t structure passed in,
 *		so the caller is responsible for freeing it.
 * Parameters:
 *		zhp - zfs_handle_t pointer to the current dataset being
 *			processed
 *		data - mounted_zone_data_t pointer
 * Returns:
 *		0 - not mounted as zone's root
 *		1 - this dataset is mounted as zone's root
 * Scope:
 *		Private
 */
static int
be_find_mounted_zone_root_callback(zfs_handle_t *zhp, void *data)
{
	mounted_zone_root_data_t	*mzr_data = data;
	char				*mp = NULL;

	if (zfs_is_mounted(zhp, &mp) && mp != NULL &&
	    strcmp(mp, mzr_data->zone_altroot) == 0) {
		mzr_data->zoneroot_ds = strdup(zfs_get_name(zhp));
		free(mp);
		return (1);
	}

	free(mp);
	return (0);
}

/*
 * Function:	be_zone_is_active
 * Description: This function gets the active property of a zone root
 *		dataset, and returns true if active property is on.
 * Parameters:
 *		zfs - zfs_handle_t pointer to zone root dataset to check
 * Returns:
 *		B_TRUE - zone root dataset is active
 *		B_FALSE - zone root dataset is not active
 * Scope:
 *		Private
 */
boolean_t
be_zone_is_active(zfs_handle_t *zhp)
{
	nvlist_t	*userprops = NULL;
	nvlist_t	*propname = NULL;
	char		*active_str = NULL;

	/* Get user properties for the zone root dataset */
	if ((userprops = zfs_get_user_props(zhp)) == NULL) {
		be_print_err(gettext("be_zone_is_active: "
		    "failed to get user properties for zone root "
		    "dataset (%s): %s\n"), zfs_get_name(zhp),
		    libzfs_error_description(g_zfs));
		return (B_FALSE);
	}

	/* Get active property from the zone root dataset user properties */
	if (nvlist_lookup_nvlist(userprops, BE_ZONE_ACTIVE_PROPERTY, &propname)
	    != 0 || nvlist_lookup_string(propname, ZPROP_VALUE, &active_str)
	    != 0) {
		/* BE_ZONE_ACTIVE_PROPERTY was not found */
		return (B_FALSE);
	}

	if (strcmp(active_str, "on") == 0)
		return (B_TRUE);

	/* BE_ZONE_ACTIVE_PROPERTY was found but not set to 'on' */
	return (B_FALSE);
}
