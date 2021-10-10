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

#include <assert.h>
#include <boot_utils.h>
#include <libintl.h>
#include <libnvpair.h>
#include <libzfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/mnttab.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/systeminfo.h>
#include <unistd.h>

#include <libbe.h>
#include <libbe_priv.h>

char	*mnttab = MNTTAB;

/*
 * Private function prototypes
 */
static int set_bootfs(char *boot_rpool, char *be_root_ds);
static int set_canmount(be_node_list_t *, char *);
static int be_promote_zone_ds(char *, char *);
static int be_promote_ds_callback(zfs_handle_t *, void *);

/* ******************************************************************** */
/*			Public Functions				*/
/* ******************************************************************** */

/*
 * Function:	be_activate
 * Description:	Calls _be_activate which activates the BE named in the
 *		attributes passed in through be_attrs. The process of
 *		activation sets the bootfs property of the root pool, and resets
 *		the canmount property to noauto.  Any boot menu handling must be
 *		performed by the caller.
 * Parameters:
 *		be_attrs - pointer to nvlist_t of attributes being passed in.
 *			The follow attribute values are used by this function:
 *
 *			BE_ATTR_ORIG_BE_NAME		*required
 * Return:
 *		BE_SUCCESS - Success
 *		be_errno_t - Failure
 * Scope:
 *		Public
 */
int
be_activate(nvlist_t *be_attrs)
{
	int	ret = BE_SUCCESS;
	char	*be_name = NULL;

	if (getzoneid() != GLOBAL_ZONEID) {
		/*
		 * Check to see if we have write access to the root filesystem
		 */
		ret = be_check_rozr();
		if (ret != BE_SUCCESS)
			return (ret);
	}

	/* Initialize libzfs handle */
	if (!be_zfs_init())
		return (BE_ERR_INIT);

	/* Get the BE name to activate */
	if (nvlist_lookup_string(be_attrs, BE_ATTR_ORIG_BE_NAME, &be_name)
	    != 0) {
		be_print_err(gettext("be_activate: failed to "
		    "lookup BE_ATTR_ORIG_BE_NAME attribute\n"));
		be_zfs_fini();
		return (BE_ERR_INVAL);
	}

	/* Validate BE name */
	if (!be_valid_be_name(be_name)) {
		be_print_err(gettext("be_activate: invalid BE name %s\n"),
		    be_name);
		be_zfs_fini();
		return (BE_ERR_INVAL);
	}

	ret = _be_activate(be_name);

	be_zfs_fini();

	return (ret);
}

/* ******************************************************************** */
/*			Semi Private Functions				*/
/* ******************************************************************** */

/*
 * Function:	_be_activate
 * Description:	This does the actual work described in be_activate.
 * Parameters:
 *		be_name - pointer to the name of BE to activate.
 *
 * Return:
 *		BE_SUCCESS - Success
 *		be_errnot_t - Failure
 * Scope:
 *		Public
 */
int
_be_activate(char *be_name)
{
	be_transaction_data_t cb = { 0 };
	char		root_ds[MAXPATHLEN];
	int		ret = BE_SUCCESS;
	int		zret = 0;

	/*
	 * TODO: The BE needs to be validated to make sure that it is actually
	 * a bootable BE.
	 */
	if (be_name == NULL)
		return (BE_ERR_INVAL);

	/* Set obe_name to be_name in the cb structure */
	cb.obe_name = be_name;

	/* find which zpool the be is in */
	if ((zret = zpool_iter(g_zfs, be_find_zpool_callback, &cb)) == 0) {
		be_print_err(gettext("be_activate: failed to "
		    "find zpool for BE (%s)\n"), cb.obe_name);
		return (BE_ERR_BE_NOENT);
	} else if (zret < 0) {
		be_print_err(gettext("be_activate: "
		    "zpool_iter failed: %s\n"),
		    libzfs_error_description(g_zfs));
		ret = zfs_err_to_be_err(g_zfs);
		return (ret);
	}

	be_make_root_ds(cb.obe_zpool, cb.obe_name, root_ds, sizeof (root_ds));
	cb.obe_root_ds = strdup(root_ds);

	/*
	 * Check to see if this is an attempt to activate a zone BE
	 * that is not associated with the currently active global
	 * zone (ie. an unbootable BE).
	 */
	if (getzoneid() != GLOBAL_ZONEID) {
		if (!be_zone_is_bootable(cb.obe_root_ds)) {
			be_print_err(gettext("be_activate: Operation not "
			    "supported on unbootable BE\n"));
			ret = BE_ERR_ZONE_NOTSUP;
			return (ret);
		}
	}

	return (_be_activate_bh(cb.obe_name, cb.obe_root_ds, cb.obe_zpool));
}

int
_be_activate_bh(char *be_name, char *root_ds, char *be_zpool)
{
	be_node_list_t	*be_nodes = NULL;
	uuid_t		uu = {0};
	zfs_handle_t	*zhp = NULL;
	int		ret;
	int		zfs_init = 0;
	boolean_t	in_ngz = B_FALSE;
	char		cur_root_ds[MAXPATHLEN];

	/*
	 * Check to see if we're operating inside a Solaris Container
	 * or the Global Zone.
	 */
	if (getzoneid() != GLOBAL_ZONEID)
		in_ngz = B_TRUE;

	if (g_zfs == NULL) {
		if (!be_zfs_init())
			return (BE_ERR_INIT);
		zfs_init = 1;
	}

	if ((ret = _be_list(be_name, &be_nodes)) != BE_SUCCESS) {
		return (ret);
	}

	if ((ret = set_canmount(be_nodes, "noauto")) != BE_SUCCESS) {
		be_print_err(gettext("be_activate: failed to set "
		    "canmount dataset property\n"));
		goto done;
	}

	if (!in_ngz) {
		if ((ret = set_bootfs(be_nodes->be_rpool, root_ds))
		    != BE_SUCCESS) {
			be_print_err(gettext("be_activate: failed to set "
			    "bootfs pool property for %s\n"), root_ds);
			goto done;
		}
	}

	if ((zhp = zfs_open(g_zfs, root_ds, ZFS_TYPE_FILESYSTEM)) != NULL) {
		/*
		 * We don't need to close the zfs handle at this
		 * point because The callback funtion
		 * be_promote_ds_callback() will close it for us.
		 */
		if (be_promote_ds_callback(zhp, NULL) != 0) {
			be_print_err(gettext("be_activate: "
			    "failed to activate the "
			    "datasets for %s: %s\n"),
			    root_ds,
			    libzfs_error_description(g_zfs));
			ret = BE_ERR_PROMOTE;
			goto done;
		}
	} else {
		be_print_err(gettext("be_activate:: failed to open "
		    "dataset (%s): %s\n"), root_ds,
		    libzfs_error_description(g_zfs));
		ret = zfs_err_to_be_err(g_zfs);
		goto done;
	}

	if (!in_ngz && be_get_uuid(root_ds, &uu) == BE_SUCCESS &&
	    (ret = be_promote_zone_ds(be_name, root_ds))
	    != BE_SUCCESS) {
		be_print_err(gettext("be_activate: failed to promote "
		    "the active zonepath datasets for zones in BE %s\n"),
		    be_name);
	}

	if (in_ngz) {
		zhp = zfs_open(g_zfs, root_ds, ZFS_TYPE_FILESYSTEM);

		if ((ret = be_find_active_zone_root(zhp, be_zpool, cur_root_ds,
		    sizeof (cur_root_ds))) != BE_SUCCESS) {
			be_print_err(gettext("be_activate: failed to find "
			    "active zone root\n"));
			ZFS_CLOSE(zhp);
			goto done;
		}

		ZFS_CLOSE(zhp);

		/*
		 * If we are trying to activate the currently
		 * active BE, we just fall through and return
		 * BE_SUCCESS.
		 */
		if (strcmp(root_ds, cur_root_ds) != 0) {
			/* Set the active property on the 'new' BE */
			if ((ret = be_zone_toggle_active(root_ds)) !=
			    BE_SUCCESS) {
				be_print_err(gettext("be_activate: failed "
				    "to activate BE %s\n"), be_name);
				goto done;
			}

			/* Unset the active property on the 'old' BE */
			if ((ret = be_zone_toggle_active(cur_root_ds)) !=
			    BE_SUCCESS) {
				/*
				 * Find the BE name for the currently active
				 * zone root dataset
				 */
				char	*ds = NULL;
				if ((ds = be_make_name_from_ds(cur_root_ds,
				    be_zpool)) == NULL) {
					be_print_err(gettext("be_activate: "
					    "failed to de-activate dataset "
					    "%s\n"), cur_root_ds);
				} else {
					be_print_err(gettext("be_activate: "
					    "failed to de-activate BE "
					    "%s\n"), ds);
				}

				/* Try to un-activate the 'new' BE */
				if ((ret =
				    be_zone_toggle_active(root_ds))
				    != BE_SUCCESS) {
					be_print_err(gettext("be_activate: "
					    "failed to un-activate BE "
					    "%s\n"), be_name);
				}
				free(ds);
				goto done;
			}
		}
		ret = BE_SUCCESS;
	}

done:
	be_free_list(be_nodes);
	if (zfs_init)
		be_zfs_fini();
	return (ret);
}

/*
 * Function:	be_activate_current_be
 * Description:	Set the currently "active" BE to be "active on boot"
 * Paramters:
 *		none
 * Returns:
 *		BE_SUCCESS - Success
 *		be_errnot_t - Failure
 * Scope:
 *		Semi-private (library wide use only)
 */
int
be_activate_current_be(void)
{
	int ret = BE_SUCCESS;
	be_transaction_data_t bt = { 0 };

	if ((ret = be_find_current_be(&bt)) != BE_SUCCESS) {
		return (ret);
	}

	if ((ret = _be_activate(bt.obe_name)) != BE_SUCCESS) {
		be_print_err(gettext("be_activate_current_be: failed to "
		    "activate %s\n"), bt.obe_name);
		return (ret);
	}

	return (BE_SUCCESS);
}

/*
 * Function:	be_is_active_on_boot
 * Description:	Checks if the BE name passed in has the "active on boot"
 *		property set to B_TRUE.
 * Paramters:
 *		be_name - the name of the BE to check
 * Returns:
 *		B_TRUE - if active on boot.
 *		B_FALSE - if not active on boot.
 * Scope:
 *		Semi-private (library wide use only)
 */
boolean_t
be_is_active_on_boot(char *be_name)
{
	be_node_list_t *be_node = NULL;

	if (be_name == NULL) {
		be_print_err(gettext("be_is_active_on_boot: "
		    "be_name must not be NULL\n"));
		return (B_FALSE);
	}

	if (_be_list(be_name, &be_node) != BE_SUCCESS) {
		return (B_FALSE);
	}

	if (be_node == NULL) {
		return (B_FALSE);
	}

	if (be_node->be_active_on_boot) {
		be_free_list(be_node);
		return (B_TRUE);
	} else {
		be_free_list(be_node);
		return (B_FALSE);
	}
}

/* ******************************************************************** */
/*			Private Functions				*/
/* ******************************************************************** */

/*
 * Function:	set_bootfs
 * Description:	Sets the bootfs property on the boot pool to be the
 *		root dataset of the activated BE.
 * Parameters:
 *		boot_pool - The pool we're setting bootfs in.
 *		be_root_ds - The main dataset for the BE.
 * Return:
 *		BE_SUCCESS - Success
 *		be_errno_t - Failure
 * Scope:
 *		Private
 */
static int
set_bootfs(char *boot_rpool, char *be_root_ds)
{
	zpool_handle_t *zhp;
	int err = BE_SUCCESS;

	if ((zhp = zpool_open(g_zfs, boot_rpool)) == NULL) {
		be_print_err(gettext("set_bootfs: failed to open pool "
		    "(%s): %s\n"), boot_rpool, libzfs_error_description(g_zfs));
		err = zfs_err_to_be_err(g_zfs);
		return (err);
	}

	err = zpool_set_prop(zhp, "bootfs", be_root_ds);
	if (err) {
		be_print_err(gettext("set_bootfs: failed to set "
		    "bootfs property for pool %s: %s\n"), boot_rpool,
		    libzfs_error_description(g_zfs));
		err = zfs_err_to_be_err(g_zfs);
		zpool_close(zhp);
		return (err);
	}

	zpool_close(zhp);
	return (BE_SUCCESS);
}

/*
 * Function:	set_canmount
 * Description:	Sets the canmount property on the datasets of the
 *		activated BE.
 * Parameters:
 *		be_nodes - The be_node_t returned from be_list
 *		value - The value of canmount we setting, on|off|noauto.
 * Return:
 *		BE_SUCCESS - Success
 *		be_errno_t - Failure
 * Scope:
 *		Private
 */
static int
set_canmount(be_node_list_t *be_nodes, char *value)
{
	char		ds_path[MAXPATHLEN];
	zfs_handle_t	*zhp = NULL;
	be_node_list_t	*list = be_nodes;
	int		err = BE_SUCCESS;

	while (list != NULL) {
		be_dataset_list_t *datasets = list->be_node_datasets;

		be_make_root_ds(list->be_rpool, list->be_node_name, ds_path,
		    sizeof (ds_path));

		if ((zhp = zfs_open(g_zfs, ds_path, ZFS_TYPE_DATASET)) ==
		    NULL) {
			be_print_err(gettext("set_canmount: failed to open "
			    "dataset (%s): %s\n"), ds_path,
			    libzfs_error_description(g_zfs));
			err = zfs_err_to_be_err(g_zfs);
			return (err);
		}
		if (zfs_prop_get_int(zhp, ZFS_PROP_MOUNTED)) {
			/*
			 * it's already mounted so we can't change the
			 * canmount property anyway.
			 */
			err = BE_SUCCESS;
		} else {
			err = zfs_prop_set(zhp,
			    zfs_prop_to_name(ZFS_PROP_CANMOUNT), value);
			if (err) {
				ZFS_CLOSE(zhp);
				be_print_err(gettext("set_canmount: failed to "
				    "set dataset property (%s): %s\n"),
				    ds_path, libzfs_error_description(g_zfs));
				err = zfs_err_to_be_err(g_zfs);
				return (err);
			}
		}
		ZFS_CLOSE(zhp);

		while (datasets != NULL) {
			be_make_root_ds(list->be_rpool,
			    datasets->be_dataset_name, ds_path,
			    sizeof (ds_path));

			if ((zhp = zfs_open(g_zfs, ds_path, ZFS_TYPE_DATASET))
			    == NULL) {
				be_print_err(gettext("set_canmount: failed to "
				    "open dataset %s: %s\n"), ds_path,
				    libzfs_error_description(g_zfs));
				err = zfs_err_to_be_err(g_zfs);
				return (err);
			}
			if (zfs_prop_get_int(zhp, ZFS_PROP_MOUNTED)) {
				/*
				 * it's already mounted so we can't change the
				 * canmount property anyway.
				 */
				err = BE_SUCCESS;
				ZFS_CLOSE(zhp);
				break;
			}
			err = zfs_prop_set(zhp,
			    zfs_prop_to_name(ZFS_PROP_CANMOUNT), value);
			if (err) {
				ZFS_CLOSE(zhp);
				be_print_err(gettext("set_canmount: "
				    "Failed to set property value %s "
				    "for dataset %s: %s\n"), value, ds_path,
				    libzfs_error_description(g_zfs));
				err = zfs_err_to_be_err(g_zfs);
				return (err);
			}
			ZFS_CLOSE(zhp);
			datasets = datasets->be_next_dataset;
		}
		list = list->be_next_node;
	}
	return (err);
}

/*
 * Function:	_be_get_boot_device_list
 * Description:	This function gathers the list of devices where bootblocks
 * 		should be installed, based on the name of a zpool.
 * Parameters:
 * 		zpool - the name of the ZFS pool for which we want to know the
 *	 		boot devices
 * 		boot_devices - a pointer to a list of strings which on return
 * 			from the function will be populated with the list of
 * 			boot devices.  The memory for the list and each element
 * 			will be allocated here and must be freed by the caller.
 * 		num_boot_devices - a pointer to an integer which on return from
 * 			the function will point to the number of devices in the
 * 			boot_devices list.
 */
int
_be_get_boot_device_list(char *zpool, char ***boot_devices,
    int *num_boot_devices)
{
	zpool_handle_t  *zphp = NULL;
	int ret = BE_SUCCESS;
	char *vname = NULL;
	uint_t c;
	uint_t children = 0;
	nvlist_t **child, *config, *nv;
	int num_devices = 0, max_devices = 16;
	int zfs_init = 0;

	if (boot_devices) {
		if ((*boot_devices =
		    calloc(max_devices, sizeof(**boot_devices))) == NULL) {
			be_print_err(gettext("_be_get_boot_device_list: memory "
			    "allocation failed"));
			return (BE_ERR_NOMEM);
		}
	}

	if (g_zfs == NULL) {
		if (!be_zfs_init())
			return (BE_ERR_INIT);
		zfs_init = 1;
	}

	if ((zphp = zpool_open(g_zfs, zpool)) == NULL) {
		be_print_err(gettext("_be_get_boot_device_list: failed to open "
		    "pool (%s): %s\n"), zpool,
		    libzfs_error_description(g_zfs));
		return (zfs_err_to_be_err(g_zfs));
	}

	if ((config = zpool_get_config(zphp, NULL)) == NULL) {
		be_print_err(gettext("_be_get_boot_device_list: failed to get "
		    "zpool configuration information. %s\n"),
		    libzfs_error_description(g_zfs));
		return (zfs_err_to_be_err(g_zfs));
	}

	/*
	 * Get the vdev tree
	 */
	if (nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE, &nv) != 0) {
		be_print_err(gettext("_be_get_boot_device_list: failed to get "
		    "vdev tree: %s\n"), libzfs_error_description(g_zfs));
		return (zfs_err_to_be_err(g_zfs));
	}

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN, &child,
	    &children) != 0) {
		be_print_err(gettext("_be_get_boot_device_list: failed to "
		    "traverse the vdev tree: %s\n"),
		    libzfs_error_description(g_zfs));
		return (zfs_err_to_be_err(g_zfs));
	}

	for (c = 0; c < children; c++) {
		uint_t i, nchildren = 0;
		nvlist_t **nvchild;
		vname = zpool_vdev_name(g_zfs, zphp, child[c],
		    B_FALSE, B_FALSE);
		if (vname == NULL) {
			be_print_err(gettext(
			    "_be_get_boot_device_list: "
			    "failed to get device name: %s\n"),
			    libzfs_error_description(g_zfs));
			return (zfs_err_to_be_err(g_zfs));
		}
		if (strcmp(vname, "mirror") == 0 || vname[0] != 'c') {
			free(vname);

			if (nvlist_lookup_nvlist_array(child[c],
			    ZPOOL_CONFIG_CHILDREN, &nvchild, &nchildren) != 0) {
				be_print_err(
				    gettext("_be_get_boot_device_list: "
				    "failed to traverse the vdev tree: %s\n"),
				    libzfs_error_description(g_zfs));
				return (zfs_err_to_be_err(g_zfs));
			}

			for (i = 0; i < nchildren; i++) {
				vname = zpool_vdev_name(g_zfs, zphp,
				    nvchild[i], B_FALSE, B_FALSE);
				if (vname == NULL) {
					be_print_err(gettext(
					    "_be_get_boot_device_list: "
					    "failed to get device name: %s\n"),
					    libzfs_error_description(g_zfs));
					return (zfs_err_to_be_err(g_zfs));
				}

				if (((*boot_devices)[num_devices++] =
				    strdup(vname)) == NULL) {
					be_print_err(gettext(
					    "_be_get_boot_device_list: memory "
					    "allocation failed"));
					free(vname);
					return (BE_ERR_NOMEM);
				}
				if (num_devices > max_devices - 1) {
					max_devices *= 2;
					if ((*boot_devices = realloc(
					    *boot_devices, max_devices)) ==
					    NULL) {
						be_print_err(gettext(
						    "_be_get_boot_device_list: "
						    "memory allocation "
						    "failed"));
						free(vname);
						return (BE_ERR_NOMEM);
					}
				}
				free(vname);
			}
		} else {
			if (((*boot_devices)[num_devices++] = strdup(vname)) ==
			    NULL) {
				free(vname);
				be_print_err(gettext("_be_get_boot_device_list:"
				    " memory allocation failed"));
				return (BE_ERR_NOMEM);
			}
			if (num_devices > max_devices - 1) {
				max_devices *= 2;
				if ((*boot_devices = realloc(*boot_devices,
				    max_devices)) == NULL) {
					free(vname);
					be_print_err(gettext(
					    "_be_get_boot_device_list: memory "
					    "allocation failed"));
					return (BE_ERR_NOMEM);
				}
			}
			free(vname);
		}
	}

done:
	if (zphp != NULL)
		zpool_close(zphp);

	if (zfs_init)
		be_zfs_fini();

	*num_boot_devices = num_devices;

	return (ret);
}

/*
 * Function:	be_promote_zone_ds
 * Description:	This function finds the zones for the BE being activated
 *              and the active zonepath dataset for each zone. Then each
 *              active zonepath dataset is promoted.
 *
 * Parameters:
 *              be_name - the name of the global zone BE that we need to
 *                       find the zones for.
 *              be_root_ds - the root dataset for be_name.
 * Return:
 *		BE_SUCCESS - Success
 *		be_errno_t - Failure
 *
 * Scope:
 *		Private
 */
static int
be_promote_zone_ds(char *be_name, char *be_root_ds)
{
	char		*zone_ds = NULL;
	char		*temp_mntpt = NULL;
	char		origin[MAXPATHLEN];
	char		zoneroot_ds[MAXPATHLEN];
	zfs_handle_t	*zhp = NULL;
	zfs_handle_t	*z_zhp = NULL;
	zoneList_t	zone_list = NULL;
	zoneBrandList_t *brands = NULL;
	boolean_t	be_mounted = B_FALSE;
	int		zone_index = 0;
	int		err = BE_SUCCESS;
	uint16_t	umnt_flags = BE_UNMOUNT_FLAG_NULL;

	/*
	 * Get the supported zone brands so we can pass that
	 * to z_get_nonglobal_zone_list_by_brand. Currently
	 * only the solaris and labeled brand zones are supported
	 *
	 */
	if ((brands = be_get_supported_brandlist()) == NULL) {
		be_print_err(gettext("be_promote_zone_ds: no supported "
		    "brands\n"));
		return (BE_SUCCESS);
	}

	if ((zhp = zfs_open(g_zfs, be_root_ds,
	    ZFS_TYPE_FILESYSTEM)) == NULL) {
		be_print_err(gettext("be_promote_zone_ds: Failed to open "
		    "dataset (%s): %s\n"), be_root_ds,
		    libzfs_error_description(g_zfs));
		err = zfs_err_to_be_err(g_zfs);
		z_free_brand_list(brands);
		return (err);
	}

	if (!zfs_is_mounted(zhp, &temp_mntpt)) {
		if ((err = _be_mount(be_name, NULL, &temp_mntpt,
		    BE_MOUNT_FLAG_NO_ZONES)) != BE_SUCCESS) {
			be_print_err(gettext("be_promote_zone_ds: failed to "
			    "mount the BE for zones procesing.\n"));
			ZFS_CLOSE(zhp);
			z_free_brand_list(brands);
			return (err);
		}
		be_mounted = B_TRUE;
		umnt_flags |= BE_UNMOUNT_FLAG_NO_ZONES;
	}

	/*
	 * Set the zone root to the temp mount point for the BE we just mounted.
	 */
	z_set_zone_root(temp_mntpt);

	/*
	 * Get all the zones based on the brands we're looking for. If no zones
	 * are found that we're interested in unmount the BE and move on.
	 */
	if ((zone_list = z_get_nonglobal_zone_list_by_brand(brands)) == NULL) {
		if (be_mounted)
			(void) _be_unmount(be_name, NULL, umnt_flags);
		ZFS_CLOSE(zhp);
		z_free_brand_list(brands);
		free(temp_mntpt);
		return (BE_SUCCESS);
	}
	for (zone_index = 0; z_zlist_get_zonename(zone_list, zone_index)
	    != NULL; zone_index++) {
		char *zone_path = NULL;

		/* Skip zones that aren't at least installed */
		if (z_zlist_get_current_state(zone_list, zone_index) <
		    ZONE_STATE_INSTALLED)
			continue;

		if (((zone_path =
		    z_zlist_get_zonepath(zone_list, zone_index)) == NULL) ||
		    ((zone_ds = be_get_ds_from_dir(zone_path)) == NULL) ||
		    !be_zone_supported(zone_ds))
			continue;

		if (be_find_active_zone_root(zhp, zone_ds,
		    zoneroot_ds, sizeof (zoneroot_ds)) != 0) {
			be_print_err(gettext("be_promote_zone_ds: "
			    "Zone does not have an active root "
			    "dataset, skipping this zone.\n"));
			continue;
		}

		if ((z_zhp = zfs_open(g_zfs, zoneroot_ds,
		    ZFS_TYPE_FILESYSTEM)) == NULL) {
			be_print_err(gettext("be_promote_zone_ds: "
			    "Failed to open dataset "
			    "(%s): %s\n"), zoneroot_ds,
			    libzfs_error_description(g_zfs));
			err = zfs_err_to_be_err(g_zfs);
			goto done;
		}

		if (zfs_prop_get(z_zhp, ZFS_PROP_ORIGIN, origin,
		    sizeof (origin), NULL, NULL, 0, B_FALSE) != 0) {
			ZFS_CLOSE(z_zhp);
			continue;
		}

		/*
		 * We don't need to close the zfs handle at this
		 * point because the callback funtion
		 * be_promote_ds_callback() will close it for us.
		 */
		if (be_promote_ds_callback(z_zhp, NULL) != 0) {
			be_print_err(gettext("be_promote_zone_ds: "
			    "failed to activate the "
			    "datasets for %s: %s\n"),
			    zoneroot_ds,
			    libzfs_error_description(g_zfs));
			err = BE_ERR_PROMOTE;
			goto done;
		}
	}
done:
	if (be_mounted)
		(void) _be_unmount(be_name, NULL, umnt_flags);
	ZFS_CLOSE(zhp);
	free(temp_mntpt);
	z_free_brand_list(brands);
	z_free_zone_list(zone_list);
	return (err);
}

/*
 * Function:	be_promote_ds_callback
 * Description:	This function is used to promote the datasets for the BE
 *		being activated as well as the datasets for the zones BE
 *		being activated.
 *
 * Parameters:
 *              zhp - the zfs handle for zone BE being activated.
 *		data - not used.
 * Return:
 *		0 - Success
 *		be_errno_t - Failure
 *
 * Scope:
 *		Private
 */
static int
/* LINTED */
be_promote_ds_callback(zfs_handle_t *zhp, void *data)
{
	char	origin[MAXPATHLEN];
	char	*sub_dataset = NULL;
	int	ret = 0;

	if (zhp != NULL) {
		sub_dataset = strdup(zfs_get_name(zhp));
		if (sub_dataset == NULL) {
			ret = BE_ERR_NOMEM;
			goto done;
		}
	} else {
		be_print_err(gettext("be_promote_ds_callback: "
		    "Invalid zfs handle passed into function\n"));
		ret = BE_ERR_INVAL;
		goto done;
	}

	/*
	 * This loop makes sure that we promote the dataset to the
	 * top of the tree so that it is no longer a decendent of any
	 * dataset. The ZFS close and then open is used to make sure that
	 * the promotion is updated before we move on.
	 */
	while (zfs_prop_get(zhp, ZFS_PROP_ORIGIN, origin,
	    sizeof (origin), NULL, NULL, 0, B_FALSE) == 0) {
		/*
		 * If we're inside a zone, we need to guard against
		 * attempting to promote origins that live outside of
		 * the zone's 'zpool analog'.
		 */
		if (getzoneid() != GLOBAL_ZONEID) {
			char	*active_ds = NULL;
			char	*ptr = NULL;
			char	be_name[ZFS_MAXNAMELEN];
			char	zpool_analog[MAXPATHLEN];
			size_t	pool_len;

			active_ds = strdup(zfs_get_name(zhp));

			ptr = strrchr(active_ds, '/');
			(void) strcpy(be_name, ptr);
			pool_len = strlen(active_ds) -
			    (strlen(BE_CONTAINER_DS_NAME) +
			    strlen(be_name));
			(void) strlcpy(zpool_analog, active_ds, pool_len);

			if (strncmp(zpool_analog, origin, strlen(zpool_analog))
			    != 0) {
				/*
				 * The origin's zpool analog does not match the
				 * zpool analog that the dataset lives in which
				 * indicates perhaps it was generated via a
				 * zoneadm clone of another zone.  So, we can't
				 * promote it since it's not part of this zone's
				 * BE namespace and so we don't.
				 */
				goto done;
			}
		}
		if (zfs_promote(zhp) != 0) {
			if (libzfs_errno(g_zfs) != EZFS_EXISTS) {
				be_print_err(gettext("be_promote_ds_callback: "
				    "promote of %s failed: %s\n"),
				    zfs_get_name(zhp),
				    libzfs_error_description(g_zfs));
				ret = zfs_err_to_be_err(g_zfs);
				goto done;
			} else {
				/*
				 * If the call to zfs_promote returns the
				 * error EZFS_EXISTS we've hit a snapshot name
				 * collision. This means we're probably
				 * attemping to promote a zone dataset above a
				 * parent dataset that belongs to another zone
				 * which this zone was cloned from.
				 *
				 * TODO: If this is a zone dataset at some
				 * point we should skip this if the zone
				 * paths for the dataset and the snapshot
				 * don't match.
				 */
				be_print_err(gettext("be_promote_ds_callback: "
				    "promote of %s failed due to snapshot "
				    "name collision: %s\n"), zfs_get_name(zhp),
				    libzfs_error_description(g_zfs));
				ret = zfs_err_to_be_err(g_zfs);
				goto done;
			}
		}
		ZFS_CLOSE(zhp);
		if ((zhp = zfs_open(g_zfs, sub_dataset,
		    ZFS_TYPE_FILESYSTEM)) == NULL) {
			be_print_err(gettext("be_promote_ds_callback: "
			    "Failed to open dataset (%s): %s\n"), sub_dataset,
			    libzfs_error_description(g_zfs));
			ret = zfs_err_to_be_err(g_zfs);
			goto done;
		}
	}

	/* Iterate down this dataset's children and promote them */
	ret = zfs_iter_filesystems(zhp, be_promote_ds_callback, NULL);

done:
	free(sub_dataset);
	ZFS_CLOSE(zhp);
	return (ret);
}
