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
#include <ctype.h>
#include <errno.h>
#include <libgen.h>
#include <libintl.h>
#include <libnvpair.h>
#include <libzfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mnttab.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <libbe.h>
#include <libbe_priv.h>

/* Library wide variables */
libzfs_handle_t *g_zfs = NULL;

/* Private function prototypes */
static int _be_destroy(const char *, be_destroy_data_t *, boolean_t);
static int be_destroy_zones(char *, char *, be_destroy_data_t *);
static int be_destroy_zone_roots(char *, be_destroy_data_t *);
static int be_destroy_zone_roots_callback(zfs_handle_t *, void *);
static int be_copy_zones(char *, char *, char *);
static int be_clone_fs_callback(zfs_handle_t *, void *);
static int be_destroy_callback(zfs_handle_t *, void *);
static int be_send_fs_callback(zfs_handle_t *, void *);
static int be_demote_callback(zfs_handle_t *, void *);
static int be_demote_find_clone_callback(zfs_handle_t *, void *);
static int be_demote_get_one_clone(zfs_handle_t *, void *);
static int be_get_snap(char *, char **);
static int be_prep_clone_send_fs(zfs_handle_t *, be_transaction_data_t *,
    char *, int);
static boolean_t be_create_container_ds(char *);
static char *be_get_zone_be_name(char *root_ds, char *container_ds);
static int be_zone_root_exists_callback(zfs_handle_t *, void *);

/* ********************************************************************	*/
/*			Public Functions				*/
/* ********************************************************************	*/

/*
 * Function:	be_init
 * Description:	Creates the initial datasets for a BE and leaves them
 *		unpopulated.  The resultant BE can be mounted but can't
 *		yet be activated or booted.
 *
 *		To initialize a nested BE, the optional BE_ATTR_NEW_BE_NESTED_BE
 *		attribute must be passed in and must have a boolean value of
 *		true.  When initializing a nested BE, the BE_ATTR_NEW_BE_POOL
 *		attribute will be interpreted as the nested BE's alternate
 *		"pool" area.
 *
 * Parameters:
 *		be_attrs - pointer to nvlist_t of attributes being passed in.
 *			The following attributes are used by this function:
 *
 *			BE_ATTR_NEW_BE_NAME			*required
 *			BE_ATTR_NEW_BE_POOL			*required
 *			BE_ATTR_NEW_BE_NESTED_BE		*optional
 *			BE_ATTR_NEW_BE_ALLOW_AUTO_NAMING	*optional
 *			BE_ATTR_NEW_BE_PARENTBE			*optional
 *			BE_ATTR_ZFS_PROPERTIES			*optional
 *			BE_ATTR_FS_NAMES			*optional
 *			BE_ATTR_FS_ZFS_PROPERTIES		*optional
 *			BE_ATTR_FS_NUM				*optional
 *			BE_ATTR_SHARED_FS_NAMES			*optional
 *			BE_ATTR_SHARED_FS_ZFS_PROPERTIES	*optional
 *			BE_ATTR_SHARED_FS_NUM			*optional
 * Return:
 *		BE_SUCCESS - Success
 *		be_errno_t - Failure
 * Scope:
 *		Public
 */
int
be_init(nvlist_t *be_attrs)
{
	be_transaction_data_t	bt = { 0 };
	zpool_handle_t	*zlp;
	zfs_handle_t	*zhp;
	boolean_t	nested_be = B_FALSE;
	boolean_t	allow_auto_naming = B_FALSE;
	char		parentbe[UUID_PRINTABLE_STRING_LENGTH] = { 0 };
	nvlist_t	*zfs_props = NULL;
	char		nbe_root_ds[MAXPATHLEN];
	char		child_fs[MAXPATHLEN];
	char		**fs_names = NULL;
	char		**shared_fs_names = NULL;
	nvlist_t	**fs_zfs_props = NULL;
	nvlist_t	**shared_fs_zfs_props = NULL;
	uint16_t	fs_num = 0;
	uint16_t	shared_fs_num = 0;
	int		nelem, nprops;
	int		i;
	int		nvret = 0, zret = 0, ret = BE_SUCCESS;

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

	/* Get new BE name */
	if (nvlist_lookup_string(be_attrs, BE_ATTR_NEW_BE_NAME, &bt.nbe_name)
	    != 0) {
		be_print_err(gettext("be_init: failed to lookup "
		    "BE_ATTR_NEW_BE_NAME attribute\n"));
		return (BE_ERR_INVAL);
	}

	/* Validate new BE name */
	if (!be_valid_be_name(bt.nbe_name)) {
		be_print_err(gettext("be_init: invalid BE name %s\n"),
		    bt.nbe_name);
		return (BE_ERR_INVAL);
	}

	/* Get zpool name */
	if (nvlist_lookup_string(be_attrs, BE_ATTR_NEW_BE_POOL, &bt.nbe_zpool)
	    != 0) {
		be_print_err(gettext("be_init: failed to lookup "
		    "BE_ATTR_NEW_BE_POOL attribute\n"));
		return (BE_ERR_INVAL);
	}

	/* Get whether or not we're initializing a global or nested BE. */
	if ((nvret = nvlist_lookup_boolean_value(be_attrs,
	    BE_ATTR_NEW_BE_NESTED_BE, &nested_be)) != 0 && nvret != ENOENT) {
		be_print_err(gettext("be_init: failed to lookup "
		    "BE_ATTR_NEW_BE_NESTED_BE attribute\n"));
		return (BE_ERR_INVAL);
	}

	/*
	 * Get whether or not auto-naming will be
	 * allowed in case of BE name conflict.
	 */
	if ((nvret = nvlist_lookup_boolean_value(be_attrs,
	    BE_ATTR_NEW_BE_ALLOW_AUTO_NAMING, &allow_auto_naming)) != 0 &&
	    nvret != ENOENT) {
		be_print_err(gettext("be_init: failed to lookup "
		    "BE_ATTR_NEW_BE_ALLOW_AUTO_NAMING attribute\n"));
		return (BE_ERR_INVAL);
	}

	/* Get file system attributes */
	nelem = 0;
	if (nvlist_lookup_pairs(be_attrs, NV_FLAG_NOENTOK,
	    BE_ATTR_FS_NUM, DATA_TYPE_UINT16, &fs_num,
	    BE_ATTR_FS_NAMES, DATA_TYPE_STRING_ARRAY, &fs_names, &nelem,
	    BE_ATTR_FS_ZFS_PROPERTIES, DATA_TYPE_NVLIST_ARRAY, &fs_zfs_props,
	    &nprops, NULL) != 0) {
		be_print_err(gettext("be_init: failed to lookup fs "
		    "attributes\n"));
		return (BE_ERR_INVAL);
	}
	if (nelem != fs_num) {
		be_print_err(gettext("be_init: size of FS_NAMES array (%d) "
		    "does not match FS_NUM (%d)\n"), nelem, fs_num);
		return (BE_ERR_INVAL);
	}
	if (fs_zfs_props != NULL && nprops != fs_num) {
		be_print_err(gettext("be_init: size of FS_ZFS_PROPERTIES "
		    "array (%d) does not match FS_NUM (%d)\n"), nprops, fs_num);
		return (BE_ERR_INVAL);
	}

	/* Get shared file system attributes */
	nelem = 0;
	if (nvlist_lookup_pairs(be_attrs, NV_FLAG_NOENTOK,
	    BE_ATTR_SHARED_FS_NUM, DATA_TYPE_UINT16, &shared_fs_num,
	    BE_ATTR_SHARED_FS_NAMES, DATA_TYPE_STRING_ARRAY, &shared_fs_names,
	    &nelem, BE_ATTR_SHARED_FS_ZFS_PROPERTIES, DATA_TYPE_NVLIST_ARRAY,
	    &shared_fs_zfs_props, &nprops, NULL) != 0) {
		be_print_err(gettext("be_init: failed to lookup "
		    "shared fs attributes\n"));
		return (BE_ERR_INVAL);
	}
	if (nelem != shared_fs_num) {
		be_print_err(gettext("be_init: size of SHARED_FS_NAMES "
		    "array does not match SHARED_FS_NUM\n"));
		return (BE_ERR_INVAL);
	}
	if (shared_fs_zfs_props != NULL && nprops != shared_fs_num) {
		be_print_err(gettext("be_init: size of "
		    "SHARED_FS_ZFS_PROPERTIES array (%d) does not match "
		    "SHARED_FS_NUM (%d)\n"), nprops, shared_fs_num);
		return (BE_ERR_INVAL);
	}

	/* Generate string for BE's root dataset */
	be_make_root_ds(bt.nbe_zpool, bt.nbe_name, nbe_root_ds,
	    sizeof (nbe_root_ds));

	if (!nested_be) {
		/* Verify that nbe_zpool exists */
		if ((zlp = zpool_open(g_zfs, bt.nbe_zpool)) == NULL) {
			be_print_err(gettext("be_init: failed to "
			    "find existing zpool (%s): %s\n"), bt.nbe_zpool,
			    libzfs_error_description(g_zfs));
			return (zfs_err_to_be_err(g_zfs));
		}
		zpool_close(zlp);

		/*
		 * Verify BE container dataset in nbe_zpool exists.
		 * If not, create it.
		 */
		if (!be_create_container_ds(bt.nbe_zpool))
			return (BE_ERR_CREATDS);

		/*
		 * Verify that nbe_name doesn't already exist in some pool.
		 */
		if ((zret = zpool_iter(g_zfs, be_exists_callback, bt.nbe_name))
		    > 0) {
			/*
			 * nbe_name already exists.  Make an attampt at
			 * initializing an auto-named BE based off of the
			 * requested BE name.
			 */
			if (allow_auto_naming) {
				if ((bt.nbe_name = be_auto_be_name(bt.nbe_name))
				    == NULL) {
					be_print_err(gettext("be_init: failed "
					    "to generate auto BE name\n"));
					return (BE_ERR_AUTONAME);
				}

				/*
				 * Regenerate string for new BE's
				 * root dataset name.
				 */
				be_make_root_ds(bt.nbe_zpool, bt.nbe_name,
				    nbe_root_ds, sizeof (nbe_root_ds));

				/*
				 * Return the auto generated name
				 * to the caller.
				 */
				if (nvlist_add_string(be_attrs,
				    BE_ATTR_NEW_BE_NAME, strdup(bt.nbe_name))
				    != 0) {
					be_print_err(gettext("be_init: failed "
					    "to add new BE name to "
					    "be_attrs\n"));
					return (BE_ERR_INVAL);
				}
			} else {
				be_print_err(gettext("be_init: BE (%s) already "
				    "exists\n"), bt.nbe_name);
				return (BE_ERR_BE_EXISTS);
			}
		} else if (zret < 0) {
			be_print_err(gettext("be_init: zpool_iter failed: "
			    "%s\n"), libzfs_error_description(g_zfs));
			return (zfs_err_to_be_err(g_zfs));
		}
	} else {
		/*
		 * Verify that nbe_zpool (the nested BE's alternate
		 * "pool" area) exists.
		 */
		if (!zfs_dataset_exists(g_zfs, bt.nbe_zpool,
		    ZFS_TYPE_FILESYSTEM)) {
			be_print_err(gettext("be_init: failed to "
			    "find existing dataset (%s)\n"), bt.nbe_zpool);
			return (BE_ERR_NOENT);
		}

		/*
		 * Verify BE container dataset in nbe_zpool exists.
		 * If not, create it.
		 */
		if (!be_create_container_ds(bt.nbe_zpool))
			return (BE_ERR_CREATDS);

		/*
		 * Verify nbe_name doesn't already exist in this nested BE's
		 * alternate "pool" area.  If it does, and we've been passed
		 * in the flag to allow auto-naming, then generate a new name
		 * based on the given BE name.  We do this when initializing
		 * a nested BE because we're essentially creating into an
		 * existing "pool" area which has existing BEs, so we provide
		 * a way to react to this case by optionally generating an auto
		 * named BE.
		 */
		if (zfs_dataset_exists(g_zfs, nbe_root_ds,
		    ZFS_TYPE_FILESYSTEM)) {
			if (allow_auto_naming) {
				char	be_container_ds[MAXPATHLEN];

				be_make_container_ds(bt.nbe_zpool,
				    be_container_ds, sizeof (be_container_ds));

				if ((bt.nbe_name = be_auto_zone_be_name(
				    be_container_ds, bt.nbe_name)) == NULL) {
					be_print_err(gettext("be_init: failed "
					    "to generate auto BE name\n"));
					return (BE_ERR_AUTONAME);
				}

				/*
				 * Regenerate string for new BE's
				 * root dataset name.
				 */
				be_make_root_ds(bt.nbe_zpool, bt.nbe_name,
				    nbe_root_ds, sizeof (nbe_root_ds));

				/*
				 * Return the auto generated name
				 * to the caller.
				 */
				if (nvlist_add_string(be_attrs,
				    BE_ATTR_NEW_BE_NAME, strdup(bt.nbe_name))
				    != 0) {
					be_print_err(gettext("be_init: failed "
					    "to add new BE name to "
					    "be_attrs\n"));
					return (BE_ERR_INVAL);
				}
			} else {
				be_print_err(gettext("be_init: BE root "
				    "already exists (%s)"), nbe_root_ds);
				return (BE_ERR_BE_EXISTS);
			}
		}
	}

	/*
	 * Create property list for new BE root dataset.  If some
	 * zfs properties were already provided by the caller, dup
	 * that list.  Otherwise initialize a new property list.
	 */
	if (nvlist_lookup_pairs(be_attrs, NV_FLAG_NOENTOK,
	    BE_ATTR_ZFS_PROPERTIES, DATA_TYPE_NVLIST, &zfs_props, NULL)
	    != 0) {
		be_print_err(gettext("be_init: failed to lookup "
		    "BE_ATTR_ZFS_PROPERTIES attribute\n"));
		return (BE_ERR_INVAL);
	}
	if (zfs_props != NULL) {
		/* Make sure its a unique nvlist */
		if (!(zfs_props->nvl_nvflag & NV_UNIQUE_NAME) &&
		    !(zfs_props->nvl_nvflag & NV_UNIQUE_NAME_TYPE)) {
			be_print_err(gettext("be_init: ZFS property list "
			    "not unique\n"));
			return (BE_ERR_INVAL);
		}

		/* Dup the list */
		if (nvlist_dup(zfs_props, &bt.nbe_zfs_props, 0) != 0) {
			be_print_err(gettext("be_init: failed to dup ZFS "
			    "property list\n"));
			return (BE_ERR_NOMEM);
		}
	} else {
		/* Initialize new nvlist */
		if (nvlist_alloc(&bt.nbe_zfs_props, NV_UNIQUE_NAME, 0) != 0) {
			be_print_err(gettext("be_init: internal "
			    "error: out of memory\n"));
			return (BE_ERR_NOMEM);
		}
	}

	/* Set the mountpoint property for the root dataset */
	if (nvlist_add_string(bt.nbe_zfs_props,
	    zfs_prop_to_name(ZFS_PROP_MOUNTPOINT), "/") != 0) {
		be_print_err(gettext("be_init: internal error "
		    "out of memory\n"));
		ret = BE_ERR_NOMEM;
		goto done;
	}

	/* Set the 'canmount' property */
	if (nvlist_add_string(bt.nbe_zfs_props,
	    zfs_prop_to_name(ZFS_PROP_CANMOUNT), "noauto") != 0) {
		be_print_err(gettext("be_init: internal error "
		    "out of memory\n"));
		ret = BE_ERR_NOMEM;
		goto done;
	}

	/*
	 * If this is a nested BE, add the parentbe and active properties
	 * to the list of properties for the root dataset.
	 */
	if (nested_be) {
		char	*pbe = NULL;

		/* Get parentbe uuid provided by the caller. */
		if (nvlist_lookup_pairs(be_attrs, NV_FLAG_NOENTOK,
		    BE_ATTR_NEW_BE_PARENTBE, DATA_TYPE_STRING, &pbe, NULL)
		    != 0) {
			be_print_err(gettext("be_init: failed to lookup "
			    "BE_ATTR_NEW_BE_PARENTBE attribute\n"));
			ret = BE_ERR_INVAL;
			goto done;
		}

		/*
		 * If parentbe uuid not provided, use current
		 * global BE's uuid.
		 */
		if (pbe == NULL) {
			be_transaction_data_t	gz_bt = { 0 };
			uuid_t			gz_uuid = { 0 };

			if ((ret = be_find_current_be(&gz_bt)) != BE_SUCCESS)
				goto done;

			if (be_get_uuid(gz_bt.obe_root_ds, &gz_uuid) !=
			    BE_SUCCESS) {
				be_print_err(gettext("be_init: failed to get "
				    "current BE's uuid\n"));
				ret = BE_ERR_NO_UUID;
				goto done;
			}
			uuid_unparse(gz_uuid, parentbe);
		} else {
			(void) strlcpy(parentbe, pbe, sizeof (parentbe));
		}

		if (nvlist_add_string(bt.nbe_zfs_props,
		    BE_ZONE_PARENTBE_PROPERTY, parentbe) != 0) {
			be_print_err(gettext("be_init: internal error "
			    "out of memory\n"));
			ret = BE_ERR_NOMEM;
			goto done;
		}

		if (nvlist_add_string(bt.nbe_zfs_props,
		    BE_ZONE_ACTIVE_PROPERTY, "on") != 0) {
			be_print_err(gettext("be_init: internal error "
			    "out of memory\n"));
			ret = BE_ERR_NOMEM;
			goto done;
		}
	}

	/* Create BE root dataset for the new BE */
	if (zfs_create(g_zfs, nbe_root_ds, ZFS_TYPE_FILESYSTEM,
	    bt.nbe_zfs_props) != 0) {
		be_print_err(gettext("be_init: failed to "
		    "create BE root dataset (%s): %s\n"), nbe_root_ds,
		    libzfs_error_description(g_zfs));
		ret = zfs_err_to_be_err(g_zfs);
		goto done;
	}

	if (!nested_be) {
		/* For a global zone BE, set a UUID on its root dataset */
		if ((ret = be_set_uuid(nbe_root_ds)) != BE_SUCCESS) {
			be_print_err(gettext("be_init: failed to "
			    "set uuid for new BE\n"));
		}
	} else {
		/*
		 * For a nested BE, make sure the nbe_handle property is set
		 * on its base dataset.
		 */
		/* Get handle to the nested BE's rpool dataset. */
		if ((zhp = zfs_open(g_zfs, bt.nbe_zpool, ZFS_TYPE_FILESYSTEM))
		    == NULL) {
			be_print_err(gettext("be_init: failed to "
			    "open nested BE's rpool dataset (%s): %s\n"),
			    bt.nbe_zpool, libzfs_error_description(g_zfs));
			ret = zfs_err_to_be_err(g_zfs);
			goto done;
		}

		/* Set nbe_handle property */
		if (zfs_prop_set(zhp, BE_NBE_HANDLE_PROPERTY, "on") != 0) {
			be_print_err(gettext("be_init: failed to "
			    "set nbe_handle property on nested BEs rpool "
			    "dataset: %s\n"), libzfs_error_description(g_zfs));
			ret = zfs_err_to_be_err(g_zfs);
			ZFS_CLOSE(zhp);
			goto done;
		}

		ZFS_CLOSE(zhp);
	}

	/* Create the new BE's non-shared file systems */
	for (i = 0; i < fs_num && fs_names[i]; i++) {
		nvlist_t	*props = NULL;

		/*
		 * If fs == "/", skip it;
		 * we already created the root dataset
		 */
		if (strcmp(fs_names[i], "/") == 0)
			continue;

		/*
		 * Generate string for file system.  Support names that are
		 * are passed in with a preceding '/' (the old installer) and
		 * names without.
		 */
		if (fs_names[i][0] == '/') {
			(void) snprintf(child_fs, sizeof (child_fs), "%s%s",
			    nbe_root_ds, fs_names[i]);
		} else {
			(void) snprintf(child_fs, sizeof (child_fs), "%s/%s",
			    nbe_root_ds, fs_names[i]);
		}

		if (fs_zfs_props && fs_zfs_props[i]) {
			/* Make sure its a unique nvlist */
			if (!((fs_zfs_props[i])->nvl_nvflag & NV_UNIQUE_NAME) &&
			    !((fs_zfs_props[i])->nvl_nvflag &
			    NV_UNIQUE_NAME_TYPE)) {
				be_print_err(gettext("be_init: ZFS property "
				    "list not unique\n"));
				ret = BE_ERR_INVAL;
				goto done;
			}

			/* Dup the list */
			if (nvlist_dup(fs_zfs_props[i], &props, 0) != 0) {
				be_print_err(gettext("be_init: failed to dup "
				    "ZFS property list\n"));
				ret = BE_ERR_NOMEM;
				goto done;
			}
		} else {
			/* Initialize new nvlist */
			if (nvlist_alloc(&props, NV_UNIQUE_NAME, 0) != 0) {
				be_print_err(gettext("be_init: internal "
				    "error: out of memory\n"));
				ret = BE_ERR_NOMEM;
				goto done;
			}
		}

		/* Set the 'canmount' property */
		if (nvlist_add_string(props,
		    zfs_prop_to_name(ZFS_PROP_CANMOUNT), "noauto") != 0) {
			be_print_err(gettext("be_init: internal error "
			    "out of memory\n"));
			ret = BE_ERR_NOMEM;
			goto done;
		}

		/* Create file system */
		if (zfs_create(g_zfs, child_fs, ZFS_TYPE_FILESYSTEM, props)
		    != 0) {
			be_print_err(gettext("be_init: failed to create "
			    "BE's child dataset (%s): %s\n"), child_fs,
			    libzfs_error_description(g_zfs));
			ret = zfs_err_to_be_err(g_zfs);
			goto done;
		}

		nvlist_free(props);
	}

	/* Create the new BE's shared file systems */
	for (i = 0; i < shared_fs_num && shared_fs_names[i]; i++) {
		nvlist_t	*props = NULL;
		char		*mp = NULL;

		/*
		 * Generate string for file system.  Support names that are
		 * are passed in with a preceding '/' (the old installer) and
		 * names without.
		 */
		if (shared_fs_names[i][0] == '/') {
			(void) snprintf(child_fs, sizeof (child_fs), "%s%s",
			    bt.nbe_zpool, shared_fs_names[i]);
		} else {
			(void) snprintf(child_fs, sizeof (child_fs), "%s/%s",
			    bt.nbe_zpool, shared_fs_names[i]);
		}

		if (shared_fs_zfs_props && shared_fs_zfs_props[i]) {
			/* Make sure its a unique nvlist */
			if (!((shared_fs_zfs_props[i])->nvl_nvflag &
			    NV_UNIQUE_NAME) &&
			    !((shared_fs_zfs_props[i])->nvl_nvflag &
			    NV_UNIQUE_NAME_TYPE)) {
				be_print_err(gettext("be_init: ZFS property "
				    "list not unique\n"));
				ret = BE_ERR_INVAL;
				goto done;
			}

			/* Dup the list */
			if (nvlist_dup(shared_fs_zfs_props[i], &props, 0)
			    != 0) {
				be_print_err(gettext("be_init: failed to dup "
				    "ZFS property list\n"));
				ret = BE_ERR_NOMEM;
				goto done;
			}
		} else {
			/* Initialize new nvlist */
			if (nvlist_alloc(&props, NV_UNIQUE_NAME, 0) != 0) {
				be_print_err(gettext("be_init: internal "
				    "error: out of memory\n"));
				ret = BE_ERR_NOMEM;
				goto done;
			}
		}

		/*
		 * If the mountpoint property isn't specified in this shared
		 * filesystem's zfs properties AND this dataset has no other
		 * parent dataset between it and the pool dataset, we
		 * implicitly set its mountpoint to its own name.
		 */
		if (nvlist_lookup_string(props,
		    zfs_prop_to_name(ZFS_PROP_MOUNTPOINT), &mp) != 0) {
			int	ds_tok = 0;
			char	*tok = NULL, *fs = NULL;

			if ((fs = strdup(shared_fs_names[i])) == NULL) {
				be_print_err(gettext("be_init: "
				    "memory allocation failed\n"));
				ret = BE_ERR_NOMEM;
				goto done;
			}

			tok = strtok(fs, "/");
			while (tok != NULL) {
				ds_tok++;
				tok = strtok(NULL, "/");
			}

			if (ds_tok == 1) {
				char	mountpoint[MAXPATHLEN];

				if (shared_fs_names[i][0] == '/') {
					(void) strlcpy(mountpoint,
					    shared_fs_names[i],
					    sizeof (mountpoint));
				} else {
					(void) snprintf(mountpoint,
					    sizeof (mountpoint), "/%s",
					    shared_fs_names[i]);
				}
				if (nvlist_add_string(props,
				    zfs_prop_to_name(ZFS_PROP_MOUNTPOINT),
				    mountpoint) != 0) {
					be_print_err(gettext("be_init: "
					    "internal error: out of memory\n"));
					nvlist_free(props);
					ret = BE_ERR_NOMEM;
					goto done;
				}
			}
		}

		/* Create file system if it doesn't already exist */
		if (zfs_dataset_exists(g_zfs, child_fs,
		    ZFS_TYPE_DATASET)) {
			be_print_err(gettext("be_init: warning: shared "
			    "filesystem already exists, not creating (%s)\n"),
			    child_fs);
		} else {
			if (zfs_create(g_zfs, child_fs, ZFS_TYPE_FILESYSTEM,
			    props) != 0) {
				be_print_err(gettext("be_init: failed to "
				    "create BE's shared dataset (%s): %s\n"),
				    child_fs, libzfs_error_description(g_zfs));
				ret = zfs_err_to_be_err(g_zfs);
				nvlist_free(props);
				goto done;
			}
		}

		nvlist_free(props);
	}

done:
	if (bt.nbe_zfs_props != NULL)
		nvlist_free(bt.nbe_zfs_props);

	be_zfs_fini();

	return (ret);
}

/*
 * Function:	be_destroy
 * Description:	Destroy a BE and all of its children datasets, snapshots and
 *		zones that belong to the parent BE.  Any boot menu manipulation
 *		must be performed by the caller.
 * Parameters:
 *		be_attrs - pointer to nvlist_t of attributes being passed in.
 *			The following attributes are used by this function:
 *
 *			BE_ATTR_ORIG_BE_NAME		*required
 *			BE_ATTR_DESTROY_FLAGS		*optional
 * Return:
 *		BE_SUCCESS - Success
 *		be_errno_t - Failure
 * Scope:
 *		Public
 */
int
be_destroy(nvlist_t *be_attrs)
{
	zfs_handle_t		*zhp = NULL;
	be_transaction_data_t	bt = { 0 };
	be_transaction_data_t	cur_bt = { 0 };
	be_destroy_data_t	dd = { 0 };
	int			ret = BE_SUCCESS;
	uint16_t		flags = 0;
	int			zret;
	char			obe_root_ds[MAXPATHLEN];
	char			*mp = NULL;
	boolean_t		in_ngz = B_FALSE;

	/*
	 * Check to see if we're operating inside a Solaris Container
	 * or the Global Zone.
	 */
	if (getzoneid() != GLOBAL_ZONEID)
		in_ngz = B_TRUE;

	if (in_ngz) {
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

	/* Get name of BE to delete */
	if (nvlist_lookup_string(be_attrs, BE_ATTR_ORIG_BE_NAME, &bt.obe_name)
	    != 0) {
		be_print_err(gettext("be_destroy: failed to lookup "
		    "BE_ATTR_ORIG_BE_NAME attribute\n"));
		return (BE_ERR_INVAL);
	}

	/*
	 * Validate BE name. If valid, then check that the original BE is not
	 * the active BE. If it is the 'active' BE then return an error code
	 * since we can't destroy the active BE.
	 */
	if (!be_valid_be_name(bt.obe_name)) {
		be_print_err(gettext("be_destroy: invalid BE name %s\n"),
		    bt.obe_name);
		return (BE_ERR_INVAL);
	} else if (bt.obe_name != NULL) {
		if ((ret = be_find_current_be(&cur_bt)) != BE_SUCCESS) {
			return (ret);
		}
		if (strcmp(cur_bt.obe_name, bt.obe_name) == 0) {
			return (BE_ERR_DESTROY_CURR_BE);
		}
	}

	/* Get destroy flags if provided */
	if (nvlist_lookup_pairs(be_attrs, NV_FLAG_NOENTOK,
	    BE_ATTR_DESTROY_FLAGS, DATA_TYPE_UINT16, &flags, NULL)
	    != 0) {
		be_print_err(gettext("be_destroy: failed to lookup "
		    "BE_ATTR_DESTROY_FLAGS attribute\n"));
		return (BE_ERR_INVAL);
	}

	dd.destroy_snaps = flags & BE_DESTROY_FLAG_SNAPSHOTS;
	dd.force_unmount = flags & BE_DESTROY_FLAG_FORCE_UNMOUNT;

	/* Find which zpool obe_name lives in */
	if ((zret = zpool_iter(g_zfs, be_find_zpool_callback, &bt)) == 0) {
		be_print_err(gettext("be_destroy: failed to find zpool "
		    "for BE (%s)\n"), bt.obe_name);
		return (BE_ERR_BE_NOENT);
	} else if (zret < 0) {
		be_print_err(gettext("be_destroy: zpool_iter failed: %s\n"),
		    libzfs_error_description(g_zfs));
		return (zfs_err_to_be_err(g_zfs));
	}

	/* Generate string for obe_name's root dataset */
	be_make_root_ds(bt.obe_zpool, bt.obe_name, obe_root_ds,
	    sizeof (obe_root_ds));
	bt.obe_root_ds = obe_root_ds;

	/* Get handle to BE's root dataset */
	if ((zhp = zfs_open(g_zfs, bt.obe_root_ds, ZFS_TYPE_FILESYSTEM)) ==
	    NULL) {
		be_print_err(gettext("be_destroy: failed to "
		    "open BE root dataset (%s): %s\n"), bt.obe_root_ds,
		    libzfs_error_description(g_zfs));
		return (zfs_err_to_be_err(g_zfs));
	}

	if (!in_ngz) {
		/* Get the UUID of the global BE */
		if (be_get_uuid(zfs_get_name(zhp), &dd.gz_be_uuid) !=
		    BE_SUCCESS) {
			be_print_err(gettext("be_destroy: BE has no UUID "
			    "(%s)\n"), zfs_get_name(zhp));
		}
	}

	/*
	 * If the global BE is mounted, make sure we've been given the
	 * flag to forcibly unmount it.
	 */
	if (zfs_is_mounted(zhp, &mp)) {
		if (!(dd.force_unmount)) {
			be_print_err(gettext("be_destroy: "
			    "%s is currently mounted at %s, cannot destroy\n"),
			    bt.obe_name, mp != NULL ? mp : "<unknown>");

			free(mp);
			ZFS_CLOSE(zhp);
			return (BE_ERR_MOUNTED);
		}
		free(mp);
	}

	/*
	 * Detect if the BE to destroy is associated with the current active
	 * global zone.
	 */
	if (in_ngz) {
		if ((!be_zone_is_bootable(bt.obe_root_ds)) &&
		    (be_is_active_on_boot(bt.obe_name))) {
				be_print_err(gettext("be_destroy: Operation "
				    "not supported on active unbootable "
				    "BE\n"));
				return (BE_ERR_ZONE_NOTSUP);
		}
	}

	/*
	 * Detect if the BE to destroy has the 'active on boot' property set.
	 * If so, set the 'active on boot' property on the the 'active' BE.
	 */
	if (be_is_active_on_boot(bt.obe_name)) {
		if ((ret = be_activate_current_be()) != BE_SUCCESS) {
			be_print_err(gettext("be_destroy: failed to "
			    "make the current BE 'active on boot'\n"));
			return (ret);
		}
	}

	/*
	 * Destroy the non-global zone BE's if we are in the global zone
	 * and there is a UUID associated with the global zone BE
	 */
	if (!in_ngz && !uuid_is_null(dd.gz_be_uuid)) {
		if ((ret = be_destroy_zones(bt.obe_name, bt.obe_root_ds, &dd))
		    != BE_SUCCESS) {
			be_print_err(gettext("be_destroy: failed to "
			    "destroy one or more zones for BE %s\n"),
			    bt.obe_name);
			goto done;
		}
	}

	/* Unmount the BE if it was mounted */
	if (zfs_is_mounted(zhp, NULL)) {
		if ((ret = _be_unmount(bt.obe_name, NULL,
		    BE_UNMOUNT_FLAG_FORCE)) != BE_SUCCESS) {
			be_print_err(gettext("be_destroy: "
			    "failed to unmount %s\n"), bt.obe_name);
			ZFS_CLOSE(zhp);
			return (ret);
		}
	}
	ZFS_CLOSE(zhp);

	/* Destroy this BE */
	if ((ret = _be_destroy((const char *)bt.obe_root_ds, &dd, B_TRUE))
	    != BE_SUCCESS) {
		goto done;
	}

done:
	be_zfs_fini();

	return (ret);
}

/*
 * Function:	be_copy
 * Description:	This function makes a copy of an existing BE.  If the original
 *		BE and the new BE are in the same pool, it uses zfs cloning to
 *		create the new BE, otherwise it does a physical copy.
 *		If the original BE name isn't provided, it uses the currently
 *		booted BE.  If the new BE name isn't provided, it creates an
 *		auto named BE and returns that name to the caller.
 * Parameters:
 *		be_attrs - pointer to nvlist_t of attributes being passed in.
 *			The following attributes are used by this function:
 *
 *			BE_ATTR_ORIG_BE_NAME		*optional
 *			BE_ATTR_SNAP_NAME		*optional
 *			BE_ATTR_NEW_BE_NAME		*optional
 *			BE_ATTR_NEW_BE_POOL		*optional
 *			BE_ATTR_NEW_BE_DESC		*optional
 *			BE_ATTR_ZFS_PROPERTIES		*optional
 *			BE_ATTR_POLICY			*optional
 *
 *			If the BE_ATTR_NEW_BE_NAME was not passed in, upon
 *			successful BE creation, the following attribute values
 *			will be returned to the caller by setting them in the
 *			be_attrs parameter passed in:
 *
 *			BE_ATTR_SNAP_NAME
 *			BE_ATTR_NEW_BE_NAME
 * Return:
 *		BE_SUCCESS - Success
 *		be_errno_t - Failure
 * Scope:
 *		Public
 */
int
be_copy(nvlist_t *be_attrs)
{
	be_transaction_data_t	bt = { 0 };
	be_fs_list_data_t	fld = { 0 };
	zfs_handle_t	*zhp = NULL;
	nvlist_t	*zfs_props = NULL;
	uuid_t		uu = { 0 };
	char		obe_root_ds[MAXPATHLEN];
	char		nbe_root_ds[MAXPATHLEN];
	char		ss[MAXPATHLEN];
	char		*new_mp = NULL;
	boolean_t	autoname = B_FALSE;
	boolean_t	be_created = B_FALSE;
	int		i;
	int		zret;
	int		ret = BE_SUCCESS;
	uint16_t	umnt_flags = BE_UNMOUNT_FLAG_NULL;
	char		be_root_ds[MAXPATHLEN];
	uuid_t		parentbe_id = { 0 };
	boolean_t	in_ngz = B_FALSE;

	/*
	 * Check to see if we're operating inside a Solaris Container
	 * or the Global Zone.
	 */
	if (getzoneid() != GLOBAL_ZONEID)
		in_ngz = B_TRUE;

	if (in_ngz) {
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

	/* Get original BE name */
	if (nvlist_lookup_pairs(be_attrs, NV_FLAG_NOENTOK,
	    BE_ATTR_ORIG_BE_NAME, DATA_TYPE_STRING, &bt.obe_name, NULL) != 0) {
		be_print_err(gettext("be_copy: failed to lookup "
		    "BE_ATTR_ORIG_BE_NAME attribute\n"));
		return (BE_ERR_INVAL);
	}

	/* If original BE name not provided, use current BE */
	if (bt.obe_name == NULL) {
		if ((ret = be_find_current_be(&bt)) != BE_SUCCESS) {
			return (ret);
		}
	} else {
		/* Validate original BE name */
		if (!be_valid_be_name(bt.obe_name)) {
			be_print_err(gettext("be_copy: "
			    "invalid BE name %s\n"), bt.obe_name);
			return (BE_ERR_INVAL);
		}
	}

	/* Find which zpool obe_name lives in */
	if ((zret = zpool_iter(g_zfs, be_find_zpool_callback, &bt)) == 0) {
		be_print_err(gettext("be_copy: failed to "
		    "find zpool for BE (%s)\n"), bt.obe_name);
		return (BE_ERR_BE_NOENT);
	} else if (zret < 0) {
		be_print_err(gettext("be_copy: "
		    "zpool_iter failed: %s\n"),
		    libzfs_error_description(g_zfs));
		return (zfs_err_to_be_err(g_zfs));
	}

	/*
	 * Check to see if this is an attempt to create a new zone BE
	 * from a zone BE that is not associated with the
	 * currently active global zone.
	 */
	if (in_ngz) {
		be_make_root_ds(bt.obe_zpool, bt.obe_name, be_root_ds,
		    sizeof (be_root_ds));
		if (!be_zone_is_bootable(be_root_ds)) {
			be_print_err(gettext("be_copy: Operation not supported "
			    "on unbootable BE\n"));
			return (BE_ERR_ZONE_NOTSUP);
		}
	}

	/* Get snapshot name of original BE if one was provided */
	if (nvlist_lookup_pairs(be_attrs, NV_FLAG_NOENTOK,
	    BE_ATTR_SNAP_NAME, DATA_TYPE_STRING, &bt.obe_snap_name, NULL)
	    != 0) {
		be_print_err(gettext("be_copy: failed to lookup "
		    "BE_ATTR_SNAP_NAME attribute\n"));
		return (BE_ERR_INVAL);
	}

	/* Get new BE name */
	if (nvlist_lookup_pairs(be_attrs, NV_FLAG_NOENTOK,
	    BE_ATTR_NEW_BE_NAME, DATA_TYPE_STRING, &bt.nbe_name, NULL)
	    != 0) {
		be_print_err(gettext("be_copy: failed to lookup "
		    "BE_ATTR_NEW_BE_NAME attribute\n"));
		return (BE_ERR_INVAL);
	}

	/* Get zpool name to create new BE in */
	if (nvlist_lookup_pairs(be_attrs, NV_FLAG_NOENTOK,
	    BE_ATTR_NEW_BE_POOL, DATA_TYPE_STRING, &bt.nbe_zpool, NULL) != 0) {
		be_print_err(gettext("be_copy: failed to lookup "
		    "BE_ATTR_NEW_BE_POOL attribute\n"));
		return (BE_ERR_INVAL);
	} else {
		if (in_ngz && (bt.nbe_zpool != NULL)) {
			be_print_err(gettext("be_copy: Multiple pools not "
			    "supported in a zone\n"));
			return (BE_ERR_ZONE_MPOOL_NOTSUP);
		}
	}

	/* Get new BE's description if one was provided */
	if (nvlist_lookup_pairs(be_attrs, NV_FLAG_NOENTOK,
	    BE_ATTR_NEW_BE_DESC, DATA_TYPE_STRING, &bt.nbe_desc, NULL) != 0) {
		be_print_err(gettext("be_copy: failed to lookup "
		    "BE_ATTR_NEW_BE_DESC attribute\n"));
		return (BE_ERR_INVAL);
	}

	/* Get BE policy to create this snapshot under */
	if (nvlist_lookup_pairs(be_attrs, NV_FLAG_NOENTOK,
	    BE_ATTR_POLICY, DATA_TYPE_STRING, &bt.policy, NULL) != 0) {
		be_print_err(gettext("be_copy: failed to lookup "
		    "BE_ATTR_POLICY attribute\n"));
		return (BE_ERR_INVAL);
	}

	/*
	 * Create property list for new BE root dataset.  If some
	 * zfs properties were already provided by the caller, dup
	 * that list.  Otherwise initialize a new property list.
	 */
	if (nvlist_lookup_pairs(be_attrs, NV_FLAG_NOENTOK,
	    BE_ATTR_ZFS_PROPERTIES, DATA_TYPE_NVLIST, &zfs_props, NULL)
	    != 0) {
		be_print_err(gettext("be_copy: failed to lookup "
		    "BE_ATTR_ZFS_PROPERTIES attribute\n"));
		return (BE_ERR_INVAL);
	}
	if (zfs_props != NULL) {
		/* Make sure its a unique nvlist */
		if (!(zfs_props->nvl_nvflag & NV_UNIQUE_NAME) &&
		    !(zfs_props->nvl_nvflag & NV_UNIQUE_NAME_TYPE)) {
			be_print_err(gettext("be_copy: ZFS property list "
			    "not unique\n"));
			return (BE_ERR_INVAL);
		}

		/* Dup the list */
		if (nvlist_dup(zfs_props, &bt.nbe_zfs_props, 0) != 0) {
			be_print_err(gettext("be_copy: "
			    "failed to dup ZFS property list\n"));
			return (BE_ERR_NOMEM);
		}
	} else {
		/* Initialize new nvlist */
		if (nvlist_alloc(&bt.nbe_zfs_props, NV_UNIQUE_NAME, 0) != 0) {
			be_print_err(gettext("be_copy: internal "
			    "error: out of memory\n"));
			return (BE_ERR_NOMEM);
		}
	}

	/*
	 * If new BE name provided, validate the BE name and then verify
	 * that new BE name doesn't already exist in some pool.
	 */
	if (bt.nbe_name) {
		/* Validate original BE name */
		if (!be_valid_be_name(bt.nbe_name)) {
			be_print_err(gettext("be_copy: "
			    "invalid BE name %s\n"), bt.nbe_name);
			ret = BE_ERR_INVAL;
			goto done;
		}

		if (!in_ngz) {
			/* Verify it doesn't already exist */
			if ((zret = zpool_iter(g_zfs, be_exists_callback,
			    bt.nbe_name)) > 0) {
				be_print_err(gettext("be_copy: BE (%s) already "
				    "exists\n"), bt.nbe_name);
				ret = BE_ERR_BE_EXISTS;
				goto done;
			} else if (zret < 0) {
				be_print_err(gettext("be_copy: zpool_iter "
				    "failed: %s\n"),
				    libzfs_error_description(g_zfs));
				ret = zfs_err_to_be_err(g_zfs);
				goto done;
			}
		} else {
			/*
			 * if we're in a solaris container we don't need
			 * to iter over all zpools since all containers
			 * live under the same dataset.
			 */
			be_make_root_ds(bt.obe_zpool, bt.nbe_name, be_root_ds,
			    sizeof (be_root_ds));

			if (zfs_dataset_exists(g_zfs, be_root_ds,
			    ZFS_TYPE_FILESYSTEM)) {
				be_print_err(gettext("be_copy: BE (%s) already "
				    "exists\n"), bt.nbe_name);
				ret = BE_ERR_BE_EXISTS;
				goto done;
			}
		}
	} else {
		/*
		 * If an auto named BE is desired, it must be in the same
		 * pool is the original BE.
		 */
		if (bt.nbe_zpool != NULL) {
			be_print_err(gettext("be_copy: cannot specify pool "
			    "name when creating an auto named BE\n"));
			ret = BE_ERR_INVAL;
			goto done;
		}

		/*
		 * Generate auto named BE
		 */
		if ((bt.nbe_name = be_auto_be_name(bt.obe_name))
		    == NULL) {
			be_print_err(gettext("be_copy: "
			    "failed to generate auto BE name\n"));
			ret = BE_ERR_AUTONAME;
			goto done;
		}

		autoname = B_TRUE;
	}

	/*
	 * If zpool name to create new BE in is not provided,
	 * create new BE in original BE's pool.
	 */
	if (bt.nbe_zpool == NULL) {
		bt.nbe_zpool = bt.obe_zpool;
	}

	/* Get root dataset names for obe_name and nbe_name */
	be_make_root_ds(bt.obe_zpool, bt.obe_name, obe_root_ds,
	    sizeof (obe_root_ds));
	be_make_root_ds(bt.nbe_zpool, bt.nbe_name, nbe_root_ds,
	    sizeof (nbe_root_ds));

	bt.obe_root_ds = obe_root_ds;
	bt.nbe_root_ds = nbe_root_ds;

	/*
	 * If an existing snapshot name has been provided to create from,
	 * verify that it exists for the original BE's root dataset.
	 */
	if (bt.obe_snap_name != NULL) {
		/* Generate dataset name for snapshot to use. */
		(void) snprintf(ss, sizeof (ss), "%s@%s", bt.obe_root_ds,
		    bt.obe_snap_name);

		/* Verify snapshot exists */
		if (!zfs_dataset_exists(g_zfs, ss, ZFS_TYPE_SNAPSHOT)) {
			be_print_err(gettext("be_copy: "
			    "snapshot does not exist (%s): %s\n"), ss,
			    libzfs_error_description(g_zfs));
			ret = BE_ERR_SS_NOENT;
			goto done;
		}
	} else {
		/*
		 * Else snapshot name was not provided, generate an
		 * auto named snapshot to use as its origin.
		 */
		if ((ret = _be_create_snapshot(bt.obe_name, NULL,
		    &bt.obe_snap_name, bt.policy)) != BE_SUCCESS) {
			be_print_err(gettext("be_copy: "
			    "failed to create auto named snapshot\n"));
			goto done;
		}

		if (nvlist_add_string(be_attrs, BE_ATTR_SNAP_NAME,
		    bt.obe_snap_name) != 0) {
			be_print_err(gettext("be_copy: "
			    "failed to add snap name to be_attrs\n"));
			ret = BE_ERR_NOMEM;
			goto done;
		}
	}

	/* Get handle to original BE's root dataset. */
	if ((zhp = zfs_open(g_zfs, bt.obe_root_ds, ZFS_TYPE_FILESYSTEM))
	    == NULL) {
		be_print_err(gettext("be_copy: failed to "
		    "open BE root dataset (%s): %s\n"), bt.obe_root_ds,
		    libzfs_error_description(g_zfs));
		ret = zfs_err_to_be_err(g_zfs);
		goto done;
	}

	/* If original BE is currently mounted, record its altroot. */
	if (zfs_is_mounted(zhp, &bt.obe_altroot) && bt.obe_altroot == NULL) {
		be_print_err(gettext("be_copy: failed to "
		    "get altroot of mounted BE %s: %s\n"),
		    bt.obe_name, libzfs_error_description(g_zfs));
		ret = zfs_err_to_be_err(g_zfs);
		goto done;
	}

	if (strcmp(bt.obe_zpool, bt.nbe_zpool) == 0) {

		/* Do clone */

		/*
		 * Iterate through original BE's datasets and clone
		 * them to create new BE.  This call will end up closing
		 * the zfs handle passed in whether it succeeds for fails.
		 */
		if ((ret = be_clone_fs_callback(zhp, &bt)) != 0) {
			zhp = NULL;
			/* Creating clone BE failed */
			if (!autoname || ret != BE_ERR_BE_EXISTS) {
				be_print_err(gettext("be_copy: "
				    "failed to clone new BE (%s) from "
				    "orig BE (%s)\n"),
				    bt.nbe_name, bt.obe_name);
				ret = BE_ERR_CLONE;
				goto done;
			}

			/*
			 * We failed to create the new BE because a BE with
			 * the auto-name we generated above has since come
			 * into existence.  Regenerate a new auto-name
			 * and retry.
			 */
			for (i = 1; i < BE_AUTO_NAME_MAX_TRY; i++) {

				/* Sleep 1 before retrying */
				(void) sleep(1);

				/* Generate new auto BE name */
				free(bt.nbe_name);
				if ((bt.nbe_name = be_auto_be_name(bt.obe_name))
				    == NULL) {
					be_print_err(gettext("be_copy: "
					    "failed to generate auto "
					    "BE name\n"));
					ret = BE_ERR_AUTONAME;
					goto done;
				}

				/*
				 * Regenerate string for new BE's
				 * root dataset name
				 */
				be_make_root_ds(bt.nbe_zpool, bt.nbe_name,
				    nbe_root_ds, sizeof (nbe_root_ds));
				bt.nbe_root_ds = nbe_root_ds;

				/*
				 * Get handle to original BE's root dataset.
				 */
				if ((zhp = zfs_open(g_zfs, bt.obe_root_ds,
				    ZFS_TYPE_FILESYSTEM)) == NULL) {
					be_print_err(gettext("be_copy: "
					    "failed to open BE root dataset "
					    "(%s): %s\n"), bt.obe_root_ds,
					    libzfs_error_description(g_zfs));
					ret = zfs_err_to_be_err(g_zfs);
					goto done;
				}

				/*
				 * Try to clone the BE again.  This
				 * call will end up closing the zfs
				 * handle passed in whether it
				 * succeeds or fails.
				 */
				ret = be_clone_fs_callback(zhp, &bt);
				zhp = NULL;
				if (ret == 0) {
					break;
				} else if (ret != BE_ERR_BE_EXISTS) {
					be_print_err(gettext("be_copy: "
					    "failed to clone new BE "
					    "(%s) from orig BE (%s)\n"),
					    bt.nbe_name, bt.obe_name);
					ret = BE_ERR_CLONE;
					goto done;
				}
			}

			/*
			 * If we've exhausted the maximum number of
			 * tries, free the auto BE name and return
			 * error.
			 */
			if (i == BE_AUTO_NAME_MAX_TRY) {
				be_print_err(gettext("be_copy: failed "
				    "to create unique auto BE name\n"));
				free(bt.nbe_name);
				bt.nbe_name = NULL;
				ret = BE_ERR_AUTONAME;
				goto done;
			}
		}
		zhp = NULL;

	} else {
		if (in_ngz) {
			be_print_err(gettext("be_copy: zfs send/recv inside a "
			    "zone can not be supported because multiple pools "
			    "are not supported.\n"));
			ret = BE_ERR_ZONE_MPOOL_NOTSUP;
			goto done;
		}

		/* Do copy (i.e. send BE datasets via zfs_send/recv) */

		/*
		 * Verify BE container dataset in nbe_zpool exists.
		 * If not, create it.
		 */
		if (!be_create_container_ds(bt.nbe_zpool)) {
			ret = BE_ERR_CREATDS;
			goto done;
		}

		/*
		 * Iterate through original BE's datasets and send
		 * them to the other pool.  This call will end up closing
		 * the zfs handle passed in whether it succeeds or fails.
		 */
		if ((ret = be_send_fs_callback(zhp, &bt)) != 0) {
			be_print_err(gettext("be_copy: failed to "
			    "send BE (%s) to pool (%s)\n"), bt.obe_name,
			    bt.nbe_zpool);
			ret = BE_ERR_COPY;
			zhp = NULL;
			goto done;
		}
		zhp = NULL;
	}

	/*
	 * Set flag to note that the dataset(s) for the new BE have been
	 * successfully created so that if a failure happens from this point
	 * on, we know to cleanup these datasets.
	 */
	be_created = B_TRUE;

	/*
	 * Validate that the new BE is mountable.
	 * Do not attempt to mount non-global zone datasets
	 * since they are not cloned yet.
	 */
	if ((ret = _be_mount(bt.nbe_name, NULL, &new_mp,
	    BE_MOUNT_FLAG_NO_ZONES)) != BE_SUCCESS) {
		be_print_err(gettext("be_copy: failed to "
		    "mount newly created BE\n"));
		(void) _be_unmount(bt.nbe_name, NULL, BE_UNMOUNT_FLAG_NO_ZONES);
		goto done;
	}
	umnt_flags |= BE_UNMOUNT_FLAG_NO_ZONES;

	if (!in_ngz) {
		/* Set UUID for new BE */
		if (be_set_uuid(bt.nbe_root_ds) != BE_SUCCESS) {
			be_print_err(gettext("be_copy: failed to "
			    "set uuid for new BE\n"));
		}
	} else {
		if ((ret = be_zone_get_parent_id(bt.obe_root_ds,
		    &parentbe_id)) != BE_SUCCESS) {
			be_print_err(gettext("be_copy: failed to get parentbe "
			    "id for existing BE\n"));
			ret = BE_ERR_ZONE_NO_PARENTBE;
			goto done;
		} else {
			if ((ret = be_zone_set_parent_id(bt.nbe_root_ds,
			    parentbe_id)) != BE_SUCCESS) {
			be_print_err(gettext("be_copy: failed to set parentbe "
			    "id for new BE\n"));
			goto done;
			}
		}
	}

	/*
	 * Process zones outside of the private BE namespace.
	 * This has to be done here because we need the uuid set in the
	 * root dataset of the new BE. The uuid is use to set the parentbe
	 * property for the new zones datasets.
	 */
	if (!in_ngz && be_get_uuid(bt.obe_root_ds, &uu) == BE_SUCCESS) {
		if ((ret = be_copy_zones(bt.obe_name, bt.obe_root_ds,
		    bt.nbe_root_ds)) != BE_SUCCESS) {
			be_print_err(gettext("be_copy: failed to process "
			    "zones\n"));
			goto done;
		}
	}

	/*
	 * Generate a list of file systems from the original BE that are
	 * legacy mounted.  We use this list to determine which entries in
	 * vfstab we need to update for the new BE we've just created.
	 */
	if ((ret = be_get_legacy_fs(bt.obe_name, bt.obe_root_ds, NULL, NULL,
	    &fld)) != BE_SUCCESS) {
		be_print_err(gettext("be_copy: failed to "
		    "get legacy mounted file system list for %s\n"),
		    bt.obe_name);
		goto done;
	}

	/*
	 * Update new BE's vfstab.
	 */
	if ((ret = be_update_vfstab(bt.nbe_name, bt.obe_zpool, bt.nbe_zpool,
	    &fld, new_mp)) != BE_SUCCESS) {
		be_print_err(gettext("be_copy: failed to "
		    "update new BE's vfstab (%s)\n"), bt.nbe_name);
		goto done;
	}

	/* Unmount the new BE */
	if ((ret = _be_unmount(bt.nbe_name, NULL, umnt_flags)) != BE_SUCCESS) {
		be_print_err(gettext("be_copy: failed to "
		    "unmount newly created BE\n"));
		goto done;
	}

	/*
	 * If we succeeded in creating an auto named BE, set its policy
	 * type and return the auto generated name to the caller by storing
	 * it in the nvlist passed in by the caller.
	 */
	if (autoname) {
		/* Get handle to new BE's root dataset. */
		if ((zhp = zfs_open(g_zfs, bt.nbe_root_ds,
		    ZFS_TYPE_FILESYSTEM)) == NULL) {
			be_print_err(gettext("be_copy: failed to "
			    "open BE root dataset (%s): %s\n"), bt.nbe_root_ds,
			    libzfs_error_description(g_zfs));
			ret = zfs_err_to_be_err(g_zfs);
			goto done;
		}

		/*
		 * Set the policy type property into the new BE's root dataset
		 */
		if (bt.policy == NULL) {
			/* If no policy type provided, use default type */
			bt.policy = be_default_policy();
		}

		if (zfs_prop_set(zhp, BE_POLICY_PROPERTY, bt.policy) != 0) {
			be_print_err(gettext("be_copy: failed to "
			    "set BE policy for %s: %s\n"), bt.nbe_name,
			    libzfs_error_description(g_zfs));
			ret = zfs_err_to_be_err(g_zfs);
			goto done;
		}

		/*
		 * Return the auto generated name to the caller
		 */
		if (bt.nbe_name) {
			if (nvlist_add_string(be_attrs, BE_ATTR_NEW_BE_NAME,
			    bt.nbe_name) != 0) {
				be_print_err(gettext("be_copy: failed to "
				    "add snap name to be_attrs\n"));
			}
		}
	}

done:
	ZFS_CLOSE(zhp);
	be_free_fs_list(&fld);

	if (bt.nbe_zfs_props != NULL)
		nvlist_free(bt.nbe_zfs_props);

	free(bt.obe_altroot);
	free(new_mp);

	/*
	 * If a failure occurred and we already created the datasets for
	 * the new boot environment, destroy them.
	 */
	if (ret != BE_SUCCESS && be_created) {
		be_destroy_data_t	cdd = { 0 };
		boolean_t		d_snap = B_TRUE;

		cdd.force_unmount = B_TRUE;

		/*
		 * If we created this new BE via a snapshot, we don't
		 * want to destroy the snapshot on cleanup.
		 */
		if (bt.obe_snap_name != NULL)
			d_snap = B_FALSE;

		be_print_err(gettext("be_copy: "
		    "destroying partially created boot environment\n"));

		if (!in_ngz && be_get_uuid(bt.nbe_root_ds,
		    &cdd.gz_be_uuid) == 0)
			(void) be_destroy_zones(bt.nbe_name, bt.nbe_root_ds,
			    &cdd);

		(void) _be_destroy(bt.nbe_root_ds, &cdd, d_snap);
	}

	be_zfs_fini();

	return (ret);
}

/* ********************************************************************	*/
/*			Semi-Private Functions				*/
/* ******************************************************************** */

/*
 * Function:	be_find_zpool_by_bename
 * Description:	Find the name of the zpool containing the BE of the given name.
 * Parameters
 * 		bename - the name of the BE used to find the zpool
 * 		pool (return) - the name of the pool
 * 		zerr (return) - the zfs errno
 * 		zerrdesc (return) - the zfs error description
 * Returns:
 * 		BE_ERR_BE_NOENT - BE not found
 * 		0 - BE found
 * 		else - ZFS error
 * Scope:
 * 		Semi-private (library-wide use only)
 *
 * XXX Probably should move to be_utils.c, along with be_find_zpool_callback().
 */
int
be_find_zpool_by_bename(char *bename, char **pool, int *zerr, char **zerrdesc)
{
	be_transaction_data_t bt = { 0 };
	int zret, zfs_init = 0;

	if (!g_zfs) {
		if (!be_zfs_init())
			return (BE_ERR_INIT);
		zfs_init = 1;
	}

	bt.obe_name = bename;
	zret = zpool_iter(g_zfs, be_find_zpool_callback, &bt);

	*zerr = libzfs_errno(g_zfs);
	*zerrdesc = (char *)libzfs_error_description(g_zfs);

	if (zfs_init)
		be_zfs_fini();

	if (zret == 0)
		return (BE_ERR_BE_NOENT);
	else if (zret < 0)
		return (zfs_err_to_be_err(g_zfs));

	*pool = bt.obe_zpool;
	return (0);
}

/*
 * Function:	be_find_zpool_callback
 * Description:	Callback function used to find the pool that a BE lives in.
 * Parameters:
 *		zlp - zpool_handle_t pointer for the current pool being
 *			looked at.
 *		data - be_transaction_data_t pointer providing information
 *			about the BE that's being searched for.
 *			This function uses the obe_name member of this
 *			parameter to use as the BE name to search for.
 *			Upon successfully locating the BE, it populates
 *			obe_zpool with the pool name that the BE is found in.
 * Returns:
 *		1 - BE exists in this pool.
 *		0 - BE does not exist in this pool.
 * Scope:
 *		Semi-private (library wide use only)
 */
int
be_find_zpool_callback(zpool_handle_t *zlp, void *data)
{
	be_transaction_data_t	*bt = data;
	const char		*zpool;
	char			be_root_ds[MAXPATHLEN];
	char			zpool_analog[MAXPATHLEN];

	if (getzoneid() != GLOBAL_ZONEID) {
		/*
		 * This code determines what the zpool analog should be for
		 * zone BE datasets.  Because a zone BE dataset currently
		 * contains the zonepath in front of (what is essentially)
		 * the 'zpool' we have to derive it differently than we
		 * do in a global zone.  When dataset aliasing is introduced
		 * by the zones team we can revert to calling zpool_get_name
		 * since zone BE datasets will no longer have the zonepath
		 * as a part of the dataset (at least it won't be visible
		 * inside the zone).
		 * TODO: Look into removing the call to be_zone_get_zpool_analog
		 * when/if dataset aliasing becomes available inside a local
		 * zone.
		 */
		if (be_zone_get_zpool_analog(zpool_analog) == BE_SUCCESS) {
			zpool = strdup(zpool_analog);
		} else {
			zpool_close(zlp);
			return (0);
		}
	} else {
		zpool = zpool_get_name(zlp);
	}

	/*
	 * Generate string for the BE's root dataset
	 */
	be_make_root_ds(zpool, bt->obe_name, be_root_ds, sizeof (be_root_ds));

	/*
	 * Check if dataset exists
	 */
	if (zfs_dataset_exists(g_zfs, be_root_ds, ZFS_TYPE_FILESYSTEM)) {
		/* BE's root dataset exists in zpool */
		bt->obe_zpool = strdup(zpool);
		zpool_close(zlp);
		return (1);
	}

	zpool_close(zlp);
	return (0);
}

/*
 * Function:	be_exists_callback
 * Description:	Callback function used to find out if a BE exists.
 * Parameters:
 *		zlp - zpool_handle_t pointer to the current pool being
 *			looked at.
 *		data - BE name to look for.
 * Return:
 *		1 - BE exists in this pool.
 *		0 - BE does not exist in this pool.
 * Scope:
 *		Semi-private (library wide use only)
 */
int
be_exists_callback(zpool_handle_t *zlp, void *data)
{
	const char	*zpool = zpool_get_name(zlp);
	char		*be_name = data;
	char		be_root_ds[MAXPATHLEN];

	/*
	 * Generate string for the BE's root dataset
	 */
	be_make_root_ds(zpool, be_name, be_root_ds, sizeof (be_root_ds));

	/*
	 * Check if dataset exists
	 */
	if (zfs_dataset_exists(g_zfs, be_root_ds, ZFS_TYPE_FILESYSTEM)) {
		/* BE's root dataset exists in zpool */
		zpool_close(zlp);
		return (1);
	}

	zpool_close(zlp);
	return (0);
}

/*
 * Function:	be_set_uuid
 * Description:	This function generates a uuid, unparses it into
 *		string representation, and sets that string into
 *		a zfs user property for a root dataset of a BE.
 *		The name of the user property used to store the
 *		uuid is org.opensolaris.libbe:uuid
 *
 * Parameters:
 *		root_ds - Root dataset of the BE to set a uuid on.
 * Return:
 *		be_errno_t - Failure
 *		BE_SUCCESS - Success
 * Scope:
 *		Semi-private (library wide ues only)
 */
int
be_set_uuid(char *root_ds)
{
	zfs_handle_t	*zhp = NULL;
	uuid_t		uu = { 0 };
	char		uu_string[UUID_PRINTABLE_STRING_LENGTH] = { 0 };
	int		ret = BE_SUCCESS;

	/* Generate a UUID and unparse it into string form */
	uuid_generate(uu);
	if (uuid_is_null(uu) != 0) {
		be_print_err(gettext("be_set_uuid: failed to "
		    "generate uuid\n"));
		return (BE_ERR_GEN_UUID);
	}
	uuid_unparse(uu, uu_string);

	/* Get handle to the BE's root dataset. */
	if ((zhp = zfs_open(g_zfs, root_ds, ZFS_TYPE_FILESYSTEM)) == NULL) {
		be_print_err(gettext("be_set_uuid: failed to "
		    "open BE root dataset (%s): %s\n"), root_ds,
		    libzfs_error_description(g_zfs));
		return (zfs_err_to_be_err(g_zfs));
	}

	/* Set uuid property for the BE */
	if (zfs_prop_set(zhp, BE_UUID_PROPERTY, uu_string) != 0) {
		be_print_err(gettext("be_set_uuid: failed to "
		    "set uuid property for BE: %s\n"),
		    libzfs_error_description(g_zfs));
		ret = zfs_err_to_be_err(g_zfs);
	}

	ZFS_CLOSE(zhp);

	return (ret);
}

/*
 * Function:	be_get_uuid
 * Description:	This function gets the uuid string from a BE root
 *		dataset, parses it into internal format, and returns
 *		it the caller via a reference pointer passed in.
 *
 * Parameters:
 *		rootds - Root dataset of the BE to get the uuid from.
 *		uu - reference pointer to a uuid_t to return uuid in.
 * Return:
 *		be_errno_t - Failure
 *		BE_SUCCESS - Success
 * Scope:
 *		Semi-private (library wide use only)
 */
int
be_get_uuid(const char *root_ds, uuid_t *uu)
{
	zfs_handle_t	*zhp = NULL;
	nvlist_t	*userprops = NULL;
	nvlist_t	*propname = NULL;
	char		*uu_string = NULL;
	int		ret = BE_SUCCESS;

	/* Get handle to the BE's root dataset. */
	if ((zhp = zfs_open(g_zfs, root_ds, ZFS_TYPE_FILESYSTEM)) == NULL) {
		be_print_err(gettext("be_get_uuid: failed to "
		    "open BE root dataset (%s): %s\n"), root_ds,
		    libzfs_error_description(g_zfs));
		return (zfs_err_to_be_err(g_zfs));
	}

	/* Get user properties for BE's root dataset */
	if ((userprops = zfs_get_user_props(zhp)) == NULL) {
		be_print_err(gettext("be_get_uuid: failed to "
		    "get user properties for BE root dataset (%s): %s\n"),
		    root_ds, libzfs_error_description(g_zfs));
		ret = zfs_err_to_be_err(g_zfs);
		goto done;
	}

	/* Get UUID string from BE's root dataset user properties */
	if (nvlist_lookup_nvlist(userprops, BE_UUID_PROPERTY, &propname) != 0 ||
	    nvlist_lookup_string(propname, ZPROP_VALUE, &uu_string) != 0) {
		/*
		 * This probably just means that the BE is simply too old
		 * to have a uuid or that we haven't created a uuid for
		 * this BE yet.
		 */
		be_print_err(gettext("be_get_uuid: failed to "
		    "get uuid property from BE root dataset user "
		    "properties.\n"));
		ret = BE_ERR_NO_UUID;
		goto done;
	}
	/* Parse uuid string into internal format */
	if (uuid_parse(uu_string, *uu) != 0 || uuid_is_null(*uu)) {
		be_print_err(gettext("be_get_uuid: failed to "
		    "parse uuid\n"));
		ret = BE_ERR_PARSE_UUID;
		goto done;
	}

done:
	ZFS_CLOSE(zhp);
	return (ret);
}

/* ********************************************************************	*/
/*			Private Functions				*/
/* ********************************************************************	*/

/*
 * Function:	_be_destroy
 * Description:	Destroy a BE and all of its children datasets and snapshots.
 *		This function is called for both global BEs and non-global BEs.
 *		The root dataset of either the global BE or non-global BE to be
 *		destroyed is passed in.
 * Parameters:
 *		root_ds - pointer to the name of the root dataset of the
 *			BE to destroy.
 *		dd - pointer to a be_destroy_data_t structure.
 *		d_snap - boolean flag to delete BE origin or not.
 *			B_TRUE - destroy, B_FALSE - keep.
 *
 * Return:
 *		BE_SUCCESS - Success
 *		be_errno_t - Failure
 * Scope:
 *		Private
 */
static int
_be_destroy(const char *root_ds, be_destroy_data_t *dd, boolean_t d_snap)
{
	zfs_handle_t	*zhp = NULL;
	char		origin[MAXPATHLEN];
	char		parent[MAXPATHLEN];
	char		*snap = NULL;
	boolean_t	has_origin = B_FALSE;
	int		ret = BE_SUCCESS;

	/* Get handle to BE's root dataset */
	if ((zhp = zfs_open(g_zfs, root_ds, ZFS_TYPE_FILESYSTEM)) ==
	    NULL) {
		be_print_err(gettext("be_destroy: failed to "
		    "open BE root dataset (%s): %s\n"), root_ds,
		    libzfs_error_description(g_zfs));
		return (zfs_err_to_be_err(g_zfs));
	}

	/*
	 * Demote this BE in case it has dependent clones.  This call
	 * will end up closing the zfs handle passed in whether it
	 * succeeds or fails.
	 */
	if (be_demote_callback(zhp, NULL) != 0) {
		be_print_err(gettext("be_destroy: "
		    "failed to demote BE %s\n"), root_ds);
		return (BE_ERR_DEMOTE);
	}

	/* Get handle to BE's root dataset */
	if ((zhp = zfs_open(g_zfs, root_ds, ZFS_TYPE_FILESYSTEM)) ==
	    NULL) {
		be_print_err(gettext("be_destroy: failed to "
		    "open BE root dataset (%s): %s\n"), root_ds,
		    libzfs_error_description(g_zfs));
		return (zfs_err_to_be_err(g_zfs));
	}

	/*
	 * Get the origin of this BE's root dataset.  This will be used
	 * later to destroy the snapshots originally used to create this BE.
	 */
	if (zfs_prop_get(zhp, ZFS_PROP_ORIGIN, origin, sizeof (origin), NULL,
	    NULL, 0, B_FALSE) == 0) {
		(void) strlcpy(parent, origin, sizeof (parent));
		if (be_get_snap(parent, &snap) != BE_SUCCESS) {
			ZFS_CLOSE(zhp);
			be_print_err(gettext("be_destroy: failed to "
			    "get snapshot name from origin %s\n"), origin);
			return (BE_ERR_INVAL);
		}
		has_origin = B_TRUE;
	}

	/*
	 * Destroy the BE's root and its hierarchical children.  This call
	 * will end up closing the zfs handle passed in whether it succeeds
	 * or fails.
	 */
	if (be_destroy_callback(zhp, dd) != 0) {
		be_print_err(gettext("be_destroy: failed to "
		    "destroy BE %s\n"), root_ds);
		return (BE_ERR_DESTROY);
	}

	/* If BE has an origin */
	if (has_origin) {

		/*
		 * If the origin was an existing snapshot, we don't want
		 * to destroy it since that's not what the user would
		 * expect.
		 */
		if (d_snap == 0) {
			return (ret);
		}

		/*
		 * If origin snapshot doesn't have any other
		 * dependents, delete the origin.
		 */
		if ((zhp = zfs_open(g_zfs, origin, ZFS_TYPE_SNAPSHOT)) ==
		    NULL) {
			be_print_err(gettext("be_destroy: failed to "
			    "open BE's origin (%s): %s\n"), origin,
			    libzfs_error_description(g_zfs));
			ret = zfs_err_to_be_err(g_zfs);
			return (ret);
		}

		/* If origin has dependents, don't delete it. */
		if (zfs_prop_get_int(zhp, ZFS_PROP_NUMCLONES) != 0) {
			ZFS_CLOSE(zhp);
			return (ret);
		}
		ZFS_CLOSE(zhp);

		/* Get handle to BE's parent's root dataset */
		if ((zhp = zfs_open(g_zfs, parent, ZFS_TYPE_FILESYSTEM)) ==
		    NULL) {
			be_print_err(gettext("be_destroy: failed to "
			    "open BE's parent root dataset (%s): %s\n"), parent,
			    libzfs_error_description(g_zfs));
			ret = zfs_err_to_be_err(g_zfs);
			return (ret);
		}

		/* Destroy the snapshot origin used to create this BE. */
		/*
		 * The boolean set to B_FALSE and passed to zfs_destroy_snaps()
		 * tells zfs to process and destroy the snapshots now.
		 * Otherwise the call will potentially return where the
		 * snapshot isn't actually destroyed yet, and ZFS is waiting
		 * until all the references to the snapshot have been
		 * released before actually destroying the snapshot.
		 */
		if (zfs_destroy_snaps(zhp, snap, B_FALSE) != 0) {
			be_print_err(gettext("be_destroy: failed to "
			    "destroy original snapshots used to create "
			    "BE: %s\n"), libzfs_error_description(g_zfs));

			/*
			 * If a failure happened because a clone exists,
			 * don't return a failure to the user.  Above, we're
			 * only checking that the root dataset's origin
			 * snapshot doesn't have dependent clones, but its
			 * possible that a subordinate dataset origin snapshot
			 * has a clone.  We really need to check for that
			 * before trying to destroy the origin snapshot.
			 */
			if (libzfs_errno(g_zfs) != EZFS_EXISTS) {
				ret = zfs_err_to_be_err(g_zfs);
				ZFS_CLOSE(zhp);
				return (ret);
			}
		}
		ZFS_CLOSE(zhp);
	}

	return (ret);
}

/*
 * Function:	be_destroy_zones
 * Description:	Find valid zone's and call be_destroy_zone_roots to destroy its
 *		corresponding dataset and all of its children datasets
 *		and snapshots.
 * Parameters:
 *		be_name - name of global boot environment being destroyed
 *		be_root_ds - root dataset of global boot environment being
 *			destroyed.
 *		dd - be_destroy_data_t pointer
 * Return:
 *		BE_SUCCESS - Success
 *		be_errno_t - Failure
 * Scope:
 *		Private
 *
 * NOTES - Requires that the BE being deleted has no dependent BEs.  If it
 *	   does, the destroy will fail.
 */
static int
be_destroy_zones(char *be_name, char *be_root_ds, be_destroy_data_t *dd)
{
	int		i;
	int		ret = BE_SUCCESS;
	uint16_t	umnt_flags = BE_UNMOUNT_FLAG_NULL;
	char		*zonepath = NULL;
	char		*zonename = NULL;
	char		*zonepath_ds = NULL;
	char		*mp = NULL;
	zoneList_t	zlist = NULL;
	zoneBrandList_t	*brands = NULL;
	zfs_handle_t	*zhp = NULL;

	/* If zones are not implemented, then get out. */
	if (!z_zones_are_implemented()) {
		return (BE_SUCCESS);
	}

	/* Get list of supported brands */
	if ((brands = be_get_supported_brandlist()) == NULL) {
		be_print_err(gettext("be_destroy_zones: "
		    "no supported brands\n"));
		return (BE_SUCCESS);
	}

	/* Get handle to BE's root dataset */
	if ((zhp = zfs_open(g_zfs, be_root_ds, ZFS_TYPE_FILESYSTEM)) ==
	    NULL) {
		be_print_err(gettext("be_destroy_zones: failed to "
		    "open BE root dataset (%s): %s\n"), be_root_ds,
		    libzfs_error_description(g_zfs));
		z_free_brand_list(brands);
		return (zfs_err_to_be_err(g_zfs));
	}

	/*
	 * If the global BE is not mounted, we must mount it here to
	 * gather data about the non-global zones in it.
	 */
	if (!zfs_is_mounted(zhp, &mp)) {
		if ((ret = _be_mount(be_name, NULL, &mp,
		    BE_MOUNT_FLAG_NO_ZONES)) != BE_SUCCESS) {
			be_print_err(gettext("be_destroy_zones: failed to "
			    "mount the BE (%s) for zones processing.\n"),
			    be_name);
			ZFS_CLOSE(zhp);
			z_free_brand_list(brands);
			return (ret);
		}
		umnt_flags |= BE_UNMOUNT_FLAG_NO_ZONES;
	}
	ZFS_CLOSE(zhp);

	z_set_zone_root(mp);
	free(mp);

	/* Get list of supported zones. */
	if ((zlist = z_get_nonglobal_zone_list_by_brand(brands)) == NULL) {
		z_free_brand_list(brands);
		return (BE_SUCCESS);
	}

	/* Unmount the BE before destroying the zones in it. */
	if (dd->force_unmount)
		umnt_flags |= BE_UNMOUNT_FLAG_FORCE;
	if ((ret = _be_unmount(be_name, NULL, umnt_flags)) != BE_SUCCESS) {
		be_print_err(gettext("be_destroy_zones: failed to "
		    "unmount the BE (%s)\n"), be_name);
		goto done;
	}

	/* Iterate through the zones and destroy them. */
	for (i = 0; (zonename = z_zlist_get_zonename(zlist, i)) != NULL; i++) {

		/* Skip zones that aren't at least installed */
		if (z_zlist_get_current_state(zlist, i) < ZONE_STATE_INSTALLED)
			continue;

		zonepath = z_zlist_get_zonepath(zlist, i);

		/*
		 * Get the dataset of this zonepath.  If its not
		 * a dataset, skip it.
		 */
		if ((zonepath_ds = be_get_ds_from_dir(zonepath)) == NULL)
			continue;

		/*
		 * Check if this zone is supported based on the
		 * dataset of its zonepath.
		 */
		if (!be_zone_supported(zonepath_ds)) {
			free(zonepath_ds);
			continue;
		}

		/* Find the zone BE root datasets for this zone. */
		if ((ret = be_destroy_zone_roots(zonepath_ds, dd))
		    != BE_SUCCESS) {
			be_print_err(gettext("be_destroy_zones: failed to "
			    "find and destroy zone roots for zone %s\n"),
			    zonename);
			free(zonepath_ds);
			goto done;
		}
		free(zonepath_ds);
	}

done:
	z_free_brand_list(brands);
	z_free_zone_list(zlist);

	return (ret);
}

/*
 * Function:	be_destroy_zone_roots
 * Description:	This function will open the zone's root container dataset
 *		and iterate the datasets within, looking for roots that
 *		belong to the given global BE and destroying them.
 *		If no other zone roots remain in the zone's root container
 *		dataset, the function will destroy it and the zone's
 *		zonepath dataset as well.
 * Parameters:
 *		zonepath_ds - pointer to zone's zonepath dataset.
 *		dd - pointer to a linked destroy data.
 * Returns:
 *		BE_SUCCESS - Success
 *		be_errno_t - Failure
 * Scope:
 *		Private
 */
static int
be_destroy_zone_roots(char *zonepath_ds, be_destroy_data_t *dd)
{
	zfs_handle_t	*zhp;
	char		zone_container_ds[MAXPATHLEN];
	char		zone_rpool_ds[MAXPATHLEN];
	int		ret = BE_SUCCESS;

	/* Generate string for the zpool dataset for this zone. */
	be_make_nested_container_ds(zonepath_ds, zone_rpool_ds,
	    sizeof (zone_rpool_ds));

	/* Generate string for the root container dataset for this zone. */
	be_make_container_ds(zone_rpool_ds, zone_container_ds,
	    sizeof (zone_container_ds));

	/* Get handle to this zone's root container dataset. */
	if ((zhp = zfs_open(g_zfs, zone_container_ds, ZFS_TYPE_FILESYSTEM))
	    == NULL) {
		be_print_err(gettext("be_destroy_zone_roots: failed to "
		    "open zone root container dataset (%s): %s\n"),
		    zone_container_ds, libzfs_error_description(g_zfs));
		return (zfs_err_to_be_err(g_zfs));
	}

	/*
	 * Iterate through all of this zone's BEs, destroying the ones
	 * that belong to the parent global BE.
	 */
	if ((ret = zfs_iter_filesystems(zhp, be_destroy_zone_roots_callback,
	    dd)) != 0) {
		be_print_err(gettext("be_destroy_zone_roots: failed to "
		    "destroy zone roots under zonepath dataset %s: %s\n"),
		    zonepath_ds, libzfs_error_description(g_zfs));
		ZFS_CLOSE(zhp);
		return (ret);
	}
	ZFS_CLOSE(zhp);

	/* Get handle to this zone's root container dataset. */
	if ((zhp = zfs_open(g_zfs, zone_container_ds, ZFS_TYPE_FILESYSTEM))
	    == NULL) {
		be_print_err(gettext("be_destroy_zone_roots: failed to "
		    "open zone root container dataset (%s): %s\n"),
		    zone_container_ds, libzfs_error_description(g_zfs));
		return (zfs_err_to_be_err(g_zfs));
	}

	/*
	 * If there are no more zone roots in this zone's root container,
	 * dataset, destroy it and the zonepath dataset as well.
	 */
	if (zfs_iter_filesystems(zhp, be_zone_root_exists_callback, NULL)
	    == 0) {
		/* Destroy the zone root container dataset */
		if (zfs_unmount(zhp, NULL, MS_FORCE) != 0 ||
		    zfs_destroy(zhp, B_FALSE) != 0) {
			be_print_err(gettext("be_destroy_zone_roots: failed to "
			    "destroy zone root container dataset (%s): %s\n"),
			    zone_container_ds, libzfs_error_description(g_zfs));
			goto done;
		}
		ZFS_CLOSE(zhp);

		/* Get handle to zonepath dataset */
		if ((zhp = zfs_open(g_zfs, zonepath_ds, ZFS_TYPE_FILESYSTEM))
		    == NULL) {
			be_print_err(gettext("be_destroy_zone_roots: failed to "
			    "open zonepath dataset (%s): %s\n"),
			    zonepath_ds, libzfs_error_description(g_zfs));
			goto done;
		}

		/* Destroy zonepath dataset */
		if (zfs_unmount(zhp, NULL, MS_FORCE) != 0 ||
		    zfs_destroy(zhp, B_FALSE) != 0) {
			be_print_err(gettext("be_destroy_zone_roots: "
			    "failed to destroy zonepath dataest %s: %s\n"),
			    zonepath_ds, libzfs_error_description(g_zfs));
			goto done;
		}
	}

done:
	ZFS_CLOSE(zhp);
	return (ret);
}

/*
 * Function:	be_destroy_zone_roots_callback
 * Description: This function is used as a callback to iterate over all of
 *		a zone's root datasets, finding the one's that
 *		correspond to the current BE. The name's
 *		of the zone root datasets are then destroyed by _be_destroy().
 * Parameters:
 *		zhp - zfs_handle_t pointer to current dataset being processed
 *		data - be_destroy_data_t pointer
 * Returns:
 *		0 - Success
 *		be_errno_t - Failure
 * Scope:
 *		Private
 */
static int
be_destroy_zone_roots_callback(zfs_handle_t *zhp, void *data)
{
	be_destroy_data_t	*dd = data;
	uuid_t			parent_uuid = { 0 };
	int			ret = 0;

	if (be_zone_get_parent_id(zfs_get_name(zhp), &parent_uuid)
	    != BE_SUCCESS) {
		be_print_err(gettext("be_destroy_zone_roots_callback: "
		    "could not get parentuuid for zone root dataset %s\n"),
		    zfs_get_name(zhp));
		ZFS_CLOSE(zhp);
		return (0);
	}

	if (uuid_compare(dd->gz_be_uuid, parent_uuid) == 0) {
		/*
		 * Found a zone root dataset belonging to the parent
		 * BE being destroyed.  Destroy this zone BE.
		 */
		if ((ret = _be_destroy(zfs_get_name(zhp), dd, B_TRUE))
		    != BE_SUCCESS) {
			be_print_err(gettext("be_destroy_zone_root_callback: "
			    "failed to destroy zone root %s\n"),
			    zfs_get_name(zhp));
			ZFS_CLOSE(zhp);
			return (ret);
		}
	}
	ZFS_CLOSE(zhp);

	return (ret);
}

/*
 * Function:	be_copy_zones
 * Description:	Find valid zones and clone them to create their
 *		corresponding datasets for the BE being created.
 * Parameters:
 *		obe_name - name of source global BE being copied.
 *		obe_root_ds - root dataset of source global BE being copied.
 *		nbe_root_ds - root dataset of target global BE.
 * Return:
 *		BE_SUCCESS - Success
 *		be_errno_t - Failure
 * Scope:
 *		Private
 */
static int
be_copy_zones(char *obe_name, char *obe_root_ds, char *nbe_root_ds)
{
	int		i, num_retries;
	int		ret = BE_SUCCESS;
	int		iret = 0;
	char		*zonename = NULL;
	char		*zonepath = NULL;
	char		*zone_be_name = NULL;
	char		*temp_mntpt = NULL;
	char		*new_zone_be_name = NULL;
	char		zoneroot[MAXPATHLEN];
	char		zoneroot_ds[MAXPATHLEN];
	char		zone_container_ds[MAXPATHLEN];
	char		new_zoneroot_ds[MAXPATHLEN];
	char		ss[MAXPATHLEN];
	uuid_t		uu = { 0 };
	char		uu_string[UUID_PRINTABLE_STRING_LENGTH] = { 0 };
	be_transaction_data_t bt = { 0 };
	zfs_handle_t	*obe_zhp = NULL;
	zfs_handle_t	*nbe_zhp = NULL;
	zfs_handle_t	*z_zhp = NULL;
	zoneList_t	zlist = NULL;
	zoneBrandList_t	*brands = NULL;
	boolean_t	mounted_here = B_FALSE;
	char		*snap_name = NULL;

	/* If zones are not implemented, then get out. */
	if (!z_zones_are_implemented()) {
		return (BE_SUCCESS);
	}

	/* Get list of supported brands */
	if ((brands = be_get_supported_brandlist()) == NULL) {
		be_print_err(gettext("be_copy_zones: "
		    "no supported brands\n"));
		return (BE_SUCCESS);
	}

	/* Get handle to origin BE's root dataset */
	if ((obe_zhp = zfs_open(g_zfs, obe_root_ds, ZFS_TYPE_FILESYSTEM))
	    == NULL) {
		be_print_err(gettext("be_copy_zones: failed to open "
		    "the origin BE root dataset (%s) for zones processing: "
		    "%s\n"), obe_root_ds, libzfs_error_description(g_zfs));
		return (zfs_err_to_be_err(g_zfs));
	}

	/* Get handle to newly cloned BE's root dataset */
	if ((nbe_zhp = zfs_open(g_zfs, nbe_root_ds, ZFS_TYPE_FILESYSTEM))
	    == NULL) {
		be_print_err(gettext("be_copy_zones: failed to open "
		    "the new BE root dataset (%s): %s\n"), nbe_root_ds,
		    libzfs_error_description(g_zfs));
		ZFS_CLOSE(obe_zhp);
		return (zfs_err_to_be_err(g_zfs));
	}

	/* Get the uuid of the newly cloned parent BE. */
	if (be_get_uuid(zfs_get_name(nbe_zhp), &uu) != BE_SUCCESS) {
		be_print_err(gettext("be_copy_zones: "
		    "failed to get uuid for BE root "
		    "dataset %s\n"), zfs_get_name(nbe_zhp));
		ZFS_CLOSE(nbe_zhp);
		goto done;
	}
	ZFS_CLOSE(nbe_zhp);
	uuid_unparse(uu, uu_string);

	/*
	 * If the origin BE is not mounted, we must mount it here to
	 * gather data about the non-global zones in it.
	 */
	if (!zfs_is_mounted(obe_zhp, &temp_mntpt)) {
		if ((ret = _be_mount(obe_name, NULL, &temp_mntpt,
		    BE_MOUNT_FLAG_NULL)) != BE_SUCCESS) {
			be_print_err(gettext("be_copy_zones: failed to "
			    "mount the BE (%s) for zones procesing.\n"),
			    obe_name);
			goto done;
		}
		mounted_here = B_TRUE;
	}

	z_set_zone_root(temp_mntpt);

	/* Get list of supported zones. */
	if ((zlist = z_get_nonglobal_zone_list_by_brand(brands)) == NULL) {
		ret = BE_SUCCESS;
		goto done;
	}

	for (i = 0; (zonename = z_zlist_get_zonename(zlist, i)) != NULL; i++) {

		char			zonepath_ds[MAXPATHLEN];
		char			zone_rpool_ds[MAXPATHLEN];
		char			*ds = NULL;

		/* Get zonepath of zone */
		zonepath = z_zlist_get_zonepath(zlist, i);

		/* Skip zones that aren't at least installed */
		if (z_zlist_get_current_state(zlist, i) < ZONE_STATE_INSTALLED)
			continue;

		/*
		 * Get the dataset of this zonepath.  If its not
		 * a dataset, skip it.
		 */
		if ((ds = be_get_ds_from_dir(zonepath)) == NULL)
			continue;

		(void) strlcpy(zonepath_ds, ds, sizeof (zonepath_ds));
		free(ds);
		ds = NULL;

		/* Get zoneroot directory */
		be_make_zoneroot(zonepath, zoneroot, sizeof (zoneroot));

		/* If zonepath dataset not supported, skip it. */
		if (!be_zone_supported(zonepath_ds)) {
			continue;
		}

		if ((ret = be_find_active_zone_root(obe_zhp, zonepath_ds,
		    zoneroot_ds, sizeof (zoneroot_ds))) != BE_SUCCESS) {
			be_print_err(gettext("be_copy_zones: "
			    "failed to find active zone root for zone %s "
			    "in BE %s\n"), zonename, obe_name);
			goto done;
		}

		be_make_nested_container_ds(zonepath_ds, zone_rpool_ds,
		    sizeof (zone_rpool_ds));
		be_make_container_ds(zone_rpool_ds, zone_container_ds,
		    sizeof (zone_container_ds));

		if ((z_zhp = zfs_open(g_zfs, zoneroot_ds,
		    ZFS_TYPE_FILESYSTEM)) == NULL) {
			be_print_err(gettext("be_copy_zones: "
			    "failed to open zone root dataset (%s): %s\n"),
			    zoneroot_ds, libzfs_error_description(g_zfs));
			ret = zfs_err_to_be_err(g_zfs);
			goto done;
		}

		zone_be_name =
		    be_get_zone_be_name(zoneroot_ds, zone_container_ds);

		if ((new_zone_be_name = be_auto_zone_be_name(zone_container_ds,
		    zone_be_name)) == NULL) {
			be_print_err(gettext("be_copy_zones: failed "
			    "to generate auto name for zone BE.\n"));
			ret = BE_ERR_AUTONAME;
			goto done;
		}

		if ((snap_name = be_auto_snap_name()) == NULL) {
			be_print_err(gettext("be_copy_zones: failed to "
			    "generate snapshot name for zone BE.\n"));
			ret = BE_ERR_AUTONAME;
			goto done;
		}

		(void) snprintf(ss, sizeof (ss), "%s@%s", zoneroot_ds,
		    snap_name);

		if (zfs_snapshot(g_zfs, ss, B_TRUE, NULL) != 0) {
			be_print_err(gettext("be_copy_zones: "
			    "failed to snapshot zone BE (%s): %s\n"),
			    ss, libzfs_error_description(g_zfs));
			if (libzfs_errno(g_zfs) == EZFS_EXISTS)
				ret = BE_ERR_ZONE_SS_EXISTS;
			else
				ret = zfs_err_to_be_err(g_zfs);

			goto done;
		}

		(void) snprintf(new_zoneroot_ds, sizeof (new_zoneroot_ds),
		    "%s/%s", zone_container_ds, new_zone_be_name);

		bt.obe_name = zone_be_name;
		bt.obe_root_ds = zoneroot_ds;
		bt.obe_snap_name = snap_name;
		bt.obe_altroot = temp_mntpt;
		bt.nbe_name = new_zone_be_name;
		bt.nbe_root_ds = new_zoneroot_ds;
		bt.zoneroot = zoneroot;

		if (nvlist_alloc(&bt.nbe_zfs_props, NV_UNIQUE_NAME, 0) != 0) {
			be_print_err(gettext("be_copy_zones: "
			    "internal error: out of memory\n"));
			ret = BE_ERR_NOMEM;
			goto done;
		}

		/*
		 * The call to be_clone_fs_callback always closes the
		 * zfs_handle so there's no need to close z_zhp.
		 */
		if ((iret = be_clone_fs_callback(z_zhp, &bt)) != 0) {
			z_zhp = NULL;
			if (iret != BE_ERR_BE_EXISTS) {
				be_print_err(gettext("be_copy_zones: "
				    "failed to create zone BE clone for new "
				    "zone BE %s\n"), new_zone_be_name);
				ret = iret;
				if (bt.nbe_zfs_props != NULL)
					nvlist_free(bt.nbe_zfs_props);
				goto done;
			}
			/*
			 * We failed to create the new zone BE because a zone
			 * BE with the auto-name we generated above has since
			 * come into existence. Regenerate a new auto-name
			 * and retry.
			 */
			for (num_retries = 1;
			    num_retries < BE_AUTO_NAME_MAX_TRY;
			    num_retries++) {

				/* Sleep 1 before retrying */
				(void) sleep(1);

				/* Generate new auto zone BE name */
				free(new_zone_be_name);
				if ((new_zone_be_name = be_auto_zone_be_name(
				    zone_container_ds,
				    zone_be_name)) == NULL) {
					be_print_err(gettext("be_copy_zones: "
					    "failed to generate auto name "
					    "for zone BE.\n"));
					ret = BE_ERR_AUTONAME;
					if (bt.nbe_zfs_props != NULL)
						nvlist_free(bt.nbe_zfs_props);
					goto done;
				}

				(void) snprintf(new_zoneroot_ds,
				    sizeof (new_zoneroot_ds),
				    "%s/%s", zone_container_ds,
				    new_zone_be_name);
				bt.nbe_name = new_zone_be_name;
				bt.nbe_root_ds = new_zoneroot_ds;

				/*
				 * Get handle to original zone BE's root
				 * dataset.
				 */
				if ((z_zhp = zfs_open(g_zfs, zoneroot_ds,
				    ZFS_TYPE_FILESYSTEM)) == NULL) {
					be_print_err(gettext("be_copy_zones: "
					    "failed to open zone root "
					    "dataset (%s): %s\n"),
					    zoneroot_ds,
					    libzfs_error_description(g_zfs));
					ret = zfs_err_to_be_err(g_zfs);
					if (bt.nbe_zfs_props != NULL)
						nvlist_free(bt.nbe_zfs_props);
					goto done;
				}

				/*
				 * Try to clone the zone BE again. This
				 * call will end up closing the zfs
				 * handle passed in whether it
				 * succeeds or fails.
				 */
				iret = be_clone_fs_callback(z_zhp, &bt);
				z_zhp = NULL;
				if (iret == 0) {
					break;
				} else if (iret != BE_ERR_BE_EXISTS) {
					be_print_err(gettext("be_copy_zones: "
					    "failed to create zone BE clone "
					    "for new zone BE %s\n"),
					    new_zone_be_name);
					ret = iret;
					if (bt.nbe_zfs_props != NULL)
						nvlist_free(bt.nbe_zfs_props);
					goto done;
				}
			}
			/*
			 * If we've exhausted the maximum number of
			 * tries, free the auto zone BE name and return
			 * error.
			 */
			if (num_retries == BE_AUTO_NAME_MAX_TRY) {
				be_print_err(gettext("be_copy_zones: failed "
				    "to create a unique auto zone BE name\n"));
				free(bt.nbe_name);
				bt.nbe_name = NULL;
				ret = BE_ERR_AUTONAME;
				if (bt.nbe_zfs_props != NULL)
					nvlist_free(bt.nbe_zfs_props);
				goto done;
			}
		}

		if (bt.nbe_zfs_props != NULL)
			nvlist_free(bt.nbe_zfs_props);

		z_zhp = NULL;

		if ((z_zhp = zfs_open(g_zfs, new_zoneroot_ds,
		    ZFS_TYPE_FILESYSTEM)) == NULL) {
			be_print_err(gettext("be_copy_zones: "
			    "failed to open the new zone BE root dataset "
			    "(%s): %s\n"), new_zoneroot_ds,
			    libzfs_error_description(g_zfs));
			ret = zfs_err_to_be_err(g_zfs);
			goto done;
		}

		if (zfs_prop_set(z_zhp, BE_ZONE_PARENTBE_PROPERTY,
		    uu_string) != 0) {
			be_print_err(gettext("be_copy_zones: "
			    "failed to set parentbe property\n"));
			ZFS_CLOSE(z_zhp);
			ret = zfs_err_to_be_err(g_zfs);
			goto done;
		}

		if (zfs_prop_set(z_zhp, BE_ZONE_ACTIVE_PROPERTY, "on") != 0) {
			be_print_err(gettext("be_copy_zones: "
			    "failed to set active property\n"));
			ZFS_CLOSE(z_zhp);
			ret = zfs_err_to_be_err(g_zfs);
			goto done;
		}

		ZFS_CLOSE(z_zhp);
	}

done:
	free(snap_name);
	if (brands != NULL)
		z_free_brand_list(brands);
	if (zlist != NULL)
		z_free_zone_list(zlist);

	if (mounted_here)
		(void) _be_unmount(obe_name, NULL, 0);

	ZFS_CLOSE(obe_zhp);
	return (ret);
}

/*
 * Function:	be_clone_fs_callback
 * Description:	Callback function used to iterate through a BE's filesystems
 *		to clone them for the new BE.
 * Parameters:
 *		zhp - zfs_handle_t pointer for the filesystem being processed.
 *		data - be_transaction_data_t pointer providing information
 *			about original BE and new BE.
 * Return:
 *		0 - Success
 *		be_errno_t - Failure
 * Scope:
 *		Private
 */
static int
be_clone_fs_callback(zfs_handle_t *zhp, void *data)
{
	be_transaction_data_t	*bt = data;
	zfs_handle_t	*zhp_ss = NULL;
	char		zhp_name[ZFS_MAXNAMELEN];
	char		clone_ds[MAXPATHLEN];
	char		ss[MAXPATHLEN];
	int		ret = 0;

	/*
	 * Get a copy of the dataset name from the zfs handle
	 */
	(void) strlcpy(zhp_name, zfs_get_name(zhp), sizeof (zhp_name));

	/*
	 * Get the clone dataset name and prepare the zfs properties for it.
	 */
	if ((ret = be_prep_clone_send_fs(zhp, bt, clone_ds,
	    sizeof (clone_ds))) != BE_SUCCESS) {
		ZFS_CLOSE(zhp);
		return (ret);
	}

	/*
	 * Generate the name of the snapshot to use.
	 */
	(void) snprintf(ss, sizeof (ss), "%s@%s", zhp_name,
	    bt->obe_snap_name);

	/*
	 * Get handle to snapshot.
	 */
	if ((zhp_ss = zfs_open(g_zfs, ss, ZFS_TYPE_SNAPSHOT)) == NULL) {
		be_print_err(gettext("be_clone_fs_callback: "
		    "failed to get handle to snapshot (%s): %s\n"), ss,
		    libzfs_error_description(g_zfs));
		ret = zfs_err_to_be_err(g_zfs);
		ZFS_CLOSE(zhp);
		return (ret);
	}

	/*
	 * Clone the dataset.
	 */
	if (zfs_clone(zhp_ss, clone_ds, bt->nbe_zfs_props) != 0) {
		be_print_err(gettext("be_clone_fs_callback: "
		    "failed to create clone dataset (%s): %s\n"),
		    clone_ds, libzfs_error_description(g_zfs));

		ZFS_CLOSE(zhp_ss);
		ZFS_CLOSE(zhp);

		return (zfs_err_to_be_err(g_zfs));
	}

	ZFS_CLOSE(zhp_ss);

	/*
	 * Iterate through zhp's children datasets (if any)
	 * and clone them accordingly.
	 */
	if ((ret = zfs_iter_filesystems(zhp, be_clone_fs_callback, bt)) != 0) {
		/*
		 * Error occurred while processing a child dataset.
		 * Destroy this dataset and return error.
		 */
		zfs_handle_t	*d_zhp = NULL;

		ZFS_CLOSE(zhp);

		if ((d_zhp = zfs_open(g_zfs, clone_ds, ZFS_TYPE_FILESYSTEM))
		    == NULL) {
			return (ret);
		}

		(void) zfs_destroy(d_zhp, B_FALSE);
		ZFS_CLOSE(d_zhp);
		return (ret);
	}

	ZFS_CLOSE(zhp);
	return (0);
}

/*
 * Function:	be_send_fs_callback
 * Description: Callback function used to iterate through a BE's filesystems
 *		to copy them for the new BE.
 * Parameters:
 *		zhp - zfs_handle_t pointer for the filesystem being processed.
 *		data - be_transaction_data_t pointer providing information
 *			about original BE and new BE.
 * Return:
 *		0 - Success
 *		be_errnot_t - Failure
 * Scope:
 *		Private
 */
static int
be_send_fs_callback(zfs_handle_t *zhp, void *data)
{
	be_transaction_data_t	*bt = data;
	recvflags_t	flags = { 0 };
	char		zhp_name[ZFS_MAXNAMELEN];
	char		clone_ds[MAXPATHLEN];
	sendflags_t	send_flags = { 0 };
	int		pid, status, retval;
	int		srpipe[2];
	int		ret = 0;

	/*
	 * Get a copy of the dataset name from the zfs handle
	 */
	(void) strlcpy(zhp_name, zfs_get_name(zhp), sizeof (zhp_name));

	/*
	 * Get the clone dataset name and prepare the zfs properties for it.
	 */
	if ((ret = be_prep_clone_send_fs(zhp, bt, clone_ds,
	    sizeof (clone_ds))) != BE_SUCCESS) {
		ZFS_CLOSE(zhp);
		return (ret);
	}

	/*
	 * Create the new dataset.
	 */
	if (zfs_create(g_zfs, clone_ds, ZFS_TYPE_FILESYSTEM, bt->nbe_zfs_props)
	    != 0) {
		be_print_err(gettext("be_send_fs_callback: "
		    "failed to create new dataset '%s': %s\n"),
		    clone_ds, libzfs_error_description(g_zfs));
		ret = zfs_err_to_be_err(g_zfs);
		ZFS_CLOSE(zhp);
		return (ret);
	}

	/*
	 * Destination file system is already created
	 * hence we need to set the force flag on
	 */
	flags.force = B_TRUE;

	/*
	 * Initiate the pipe to be used for the send and recv
	 */
	if (pipe(srpipe) != 0) {
		int err = errno;
		be_print_err(gettext("be_send_fs_callback: failed to "
		    "open pipe\n"));
		ZFS_CLOSE(zhp);
		return (errno_to_be_err(err));
	}

	/*
	 * Fork off a child to send the dataset
	 */
	if ((pid = fork()) == -1) {
		int err = errno;
		be_print_err(gettext("be_send_fs_callback: failed to fork\n"));
		(void) close(srpipe[0]);
		(void) close(srpipe[1]);
		ZFS_CLOSE(zhp);
		return (errno_to_be_err(err));
	} else if (pid == 0) { /* child process */
		(void) close(srpipe[0]);

		/* Send dataset */
		if (zfs_send(zhp, NULL, bt->obe_snap_name, send_flags,
		    srpipe[1], NULL, NULL, NULL) != 0) {
			_exit(1);
		}
		ZFS_CLOSE(zhp);

		_exit(0);
	}

	(void) close(srpipe[1]);

	/* Receive dataset */
	if (zfs_receive(g_zfs, clone_ds, flags, NULL, srpipe[0], NULL) != 0) {
		be_print_err(gettext("be_send_fs_callback: failed to "
		    "recv dataset (%s)\n"), clone_ds);
	}
	(void) close(srpipe[0]);

	/* wait for child to exit */
	do {
		retval = waitpid(pid, &status, 0);
		if (retval == -1) {
			status = 0;
		}
	} while (retval != pid);

	if (WEXITSTATUS(status) != 0) {
		be_print_err(gettext("be_send_fs_callback: failed to "
		    "send dataset (%s)\n"), zhp_name);
		ZFS_CLOSE(zhp);
		return (BE_ERR_ZFS);
	}


	/*
	 * Iterate through zhp's children datasets (if any)
	 * and send them accordingly.
	 */
	if ((ret = zfs_iter_filesystems(zhp, be_send_fs_callback, bt)) != 0) {
		/*
		 * Error occurred while processing a child dataset.
		 * Destroy this dataset and return error.
		 */
		zfs_handle_t	*d_zhp = NULL;

		ZFS_CLOSE(zhp);

		if ((d_zhp = zfs_open(g_zfs, clone_ds, ZFS_TYPE_FILESYSTEM))
		    == NULL) {
			return (ret);
		}

		(void) zfs_destroy(d_zhp, B_FALSE);
		ZFS_CLOSE(d_zhp);
		return (ret);
	}

	ZFS_CLOSE(zhp);
	return (0);
}

/*
 * Function:	be_destroy_callback
 * Description:	Callback function used to destroy a BEs children datasets
 *		and snapshots.
 * Parameters:
 *		zhp - zfs_handle_t pointer to the filesystem being processed.
 *		data - Not used.
 * Returns:
 *		0 - Success
 *		be_errno_t - Failure
 * Scope:
 *		Private
 */
static int
be_destroy_callback(zfs_handle_t *zhp, void *data)
{
	be_destroy_data_t	*dd = data;
	int ret = 0;

	/*
	 * Iterate down this file system's hierarchical children
	 * and destroy them first.
	 */
	if ((ret = zfs_iter_filesystems(zhp, be_destroy_callback, dd)) != 0) {
		ZFS_CLOSE(zhp);
		return (ret);
	}

	if (dd->destroy_snaps) {
		/*
		 * Iterate through this file system's snapshots and
		 * destroy them before destroying the file system itself.
		 */
		if ((ret = zfs_iter_snapshots(zhp, be_destroy_callback, dd))
		    != 0) {
			ZFS_CLOSE(zhp);
			return (ret);
		}
	}

	/* Attempt to unmount the dataset before destroying it */
	if (dd->force_unmount) {
		if ((ret = zfs_unmount(zhp, NULL, MS_FORCE)) != 0) {
			be_print_err(gettext("be_destroy_callback: "
			    "failed to unmount %s: %s\n"), zfs_get_name(zhp),
			    libzfs_error_description(g_zfs));
			ret = zfs_err_to_be_err(g_zfs);
			ZFS_CLOSE(zhp);
			return (ret);
		}
	}

	if (zfs_destroy(zhp, B_FALSE) != 0) {
		be_print_err(gettext("be_destroy_callback: "
		    "failed to destroy %s: %s\n"), zfs_get_name(zhp),
		    libzfs_error_description(g_zfs));
		ret = zfs_err_to_be_err(g_zfs);
		ZFS_CLOSE(zhp);
		return (ret);
	}

	ZFS_CLOSE(zhp);
	return (0);
}

/*
 * Function:	be_demote_callback
 * Description:	This callback function is used to iterate through the file
 *		systems of a BE, looking for the right clone to promote such
 *		that this file system is left without any dependent clones.
 *		If the file system has no dependent clones, it doesn't need
 *		to get demoted, and the function will return success.
 *
 *		The demotion will be done in two passes.  The first pass
 *		will attempt to find the youngest snapshot that has a clone
 *		that is part of some other BE.  The second pass will attempt
 *		to find the youngest snapshot that has a clone that is not
 *		part of a BE.  Doing this helps ensure the aggregated set of
 *		file systems that compose a BE stay coordinated wrt BE
 *		snapshots and BE dependents.  It also prevents a random user
 *		generated clone of a BE dataset to become the parent of other
 *		BE datasets after demoting this dataset.
 *
 * Parameters:
 *		zhp - zfs_handle_t pointer to the current file system being
 *			processed.
 *		data - not used.
 * Return:
 *		0 - Success
 *		be_errno_t - Failure
 * Scope:
 *		Private
 */
static int
/* LINTED */
be_demote_callback(zfs_handle_t *zhp, void *data)
{
	be_demote_data_t	dd = { 0 };
	int			i, ret = 0;

	/*
	 * Initialize be_demote_data for the first pass - this will find a
	 * clone in another BE, if one exists.
	 */
	dd.find_in_BE = B_TRUE;

	for (i = 0; i < 2; i++) {

		if (zfs_iter_snapshots(zhp, be_demote_find_clone_callback, &dd)
		    != 0) {
			be_print_err(gettext("be_demote_callback: "
			    "failed to iterate snapshots for %s: %s\n"),
			    zfs_get_name(zhp), libzfs_error_description(g_zfs));
			ret = zfs_err_to_be_err(g_zfs);
			ZFS_CLOSE(zhp);
			return (ret);
		}
		if (dd.clone_zhp != NULL) {
			/* Found the clone to promote.  Promote it. */
			if (zfs_promote(dd.clone_zhp) != 0) {
				be_print_err(gettext("be_demote_callback: "
				    "failed to promote %s: %s\n"),
				    zfs_get_name(dd.clone_zhp),
				    libzfs_error_description(g_zfs));
				ret = zfs_err_to_be_err(g_zfs);
				ZFS_CLOSE(dd.clone_zhp);
				ZFS_CLOSE(zhp);
				return (ret);
			}

			ZFS_CLOSE(dd.clone_zhp);
		}

		/*
		 * Reinitialize be_demote_data for the second pass.
		 * This will find a user created clone outside of any BE
		 * namespace, if one exists.
		 */
		dd.clone_zhp = NULL;
		dd.origin_creation = 0;
		dd.snapshot = NULL;
		dd.find_in_BE = B_FALSE;
	}

	/* Iterate down this file system's children and demote them */
	if ((ret = zfs_iter_filesystems(zhp, be_demote_callback, NULL)) != 0) {
		ZFS_CLOSE(zhp);
		return (ret);
	}

	ZFS_CLOSE(zhp);
	return (0);
}

/*
 * Function:	be_demote_find_clone_callback
 * Description:	This callback function is used to iterate through the
 *		snapshots of a dataset, looking for the youngest snapshot
 *		that has a clone.  If found, it returns a reference to the
 *		clone back to the caller in the callback data.
 * Parameters:
 *		zhp - zfs_handle_t pointer to current snapshot being looked at
 *		data - be_demote_data_t pointer used to store the clone that
 *			is found.
 * Returns:
 *		0 - Successfully iterated through all snapshots.
 *		1 - Failed to iterate through all snapshots.
 * Scope:
 *		Private
 */
static int
be_demote_find_clone_callback(zfs_handle_t *zhp, void *data)
{
	be_demote_data_t	*dd = data;
	time_t			snap_creation;
	int			zret = 0;

	/* If snapshot has no clones, no need to look at it */
	if (zfs_prop_get_int(zhp, ZFS_PROP_NUMCLONES) == 0) {
		ZFS_CLOSE(zhp);
		return (0);
	}

	dd->snapshot = zfs_get_name(zhp);

	/* Get the creation time of this snapshot */
	snap_creation = (time_t)zfs_prop_get_int(zhp, ZFS_PROP_CREATION);

	/*
	 * If this snapshot's creation time is greater than (or younger than)
	 * the current youngest snapshot found, iterate this snapshot to
	 * check if it has a clone that we're looking for.
	 */
	if (snap_creation >= dd->origin_creation) {
		/*
		 * Iterate the dependents of this snapshot to find a
		 * a clone that's a direct dependent.
		 */
		if ((zret = zfs_iter_dependents(zhp, B_FALSE,
		    be_demote_get_one_clone, dd)) == -1) {
			be_print_err(gettext("be_demote_find_clone_callback: "
			    "failed to iterate dependents of %s\n"),
			    zfs_get_name(zhp));
			ZFS_CLOSE(zhp);
			return (1);
		} else if (zret == 1) {
			/*
			 * Found a clone, update the origin_creation time
			 * in the callback data.
			 */
			dd->origin_creation = snap_creation;
		}
	}

	ZFS_CLOSE(zhp);
	return (0);
}

/*
 * Function:	be_demote_get_one_clone
 * Description:	This callback function is used to iterate through a
 *		snapshot's dependencies to find a filesystem that is a
 *		direct clone of the snapshot being iterated.
 * Parameters:
 *		zhp - zfs_handle_t pointer to current dataset being looked at
 *		data - be_demote_data_t pointer used to store the clone
 *			that is found, and also provides flag to note
 *			whether or not the clone filesystem being searched
 *			for needs to be found in a BE dataset hierarchy.
 * Return:
 *		1 - Success, found clone and its also a BE's root dataset.
 *		0 - Failure, clone not found.
 * Scope:
 *		Private
 */
static int
be_demote_get_one_clone(zfs_handle_t *zhp, void *data)
{
	be_demote_data_t	*dd = data;
	char			origin[ZFS_MAXNAMELEN];
	char			ds_path[ZFS_MAXNAMELEN];

	if (zfs_get_type(zhp) != ZFS_TYPE_FILESYSTEM) {
		ZFS_CLOSE(zhp);
		return (0);
	}

	(void) strlcpy(ds_path, zfs_get_name(zhp), sizeof (ds_path));

	/*
	 * Make sure this is a direct clone of the snapshot
	 * we're iterating.
	 */
	if (zfs_prop_get(zhp, ZFS_PROP_ORIGIN, origin, sizeof (origin), NULL,
	    NULL, 0, B_FALSE) != 0) {
		be_print_err(gettext("be_demote_get_one_clone: "
		    "failed to get origin of %s: %s\n"), ds_path,
		    libzfs_error_description(g_zfs));
		ZFS_CLOSE(zhp);
		return (0);
	}
	if (strcmp(origin, dd->snapshot) != 0) {
		ZFS_CLOSE(zhp);
		return (0);
	}

	if (dd->find_in_BE) {
		if ((zpool_iter(g_zfs, be_check_be_roots_callback,
		    ds_path)) > 0) {
			if (dd->clone_zhp != NULL)
				ZFS_CLOSE(dd->clone_zhp);
			dd->clone_zhp = zhp;
			return (1);
		}

		ZFS_CLOSE(zhp);
		return (0);
	}

	if (dd->clone_zhp != NULL)
		ZFS_CLOSE(dd->clone_zhp);

	dd->clone_zhp = zhp;
	return (1);
}

/*
 * Function:	be_get_snap
 * Description:	This function takes a snapshot dataset name and separates
 *		out the parent dataset portion from the snapshot name.
 *		I.e. it finds the '@' in the snapshot dataset name and
 *		replaces it with a '\0'.
 * Parameters:
 *		origin - char pointer to a snapshot dataset name.  Its
 *			contents will be modified by this function.
 *		*snap - pointer to a char pointer.  Will be set to the
 *			snapshot name portion upon success.
 * Return:
 *		BE_SUCCESS - Success
 *		1 - Failure
 * Scope:
 *		Private
 */
static int
be_get_snap(char *origin, char **snap)
{
	char	*cp;

	/*
	 * Separate out the origin's dataset and snapshot portions by
	 * replacing the @ with a '\0'
	 */
	cp = strrchr(origin, '@');
	if (cp != NULL) {
		if (cp[1] != NULL && cp[1] != '\0') {
			cp[0] = '\0';
			*snap = cp+1;
		} else {
			return (1);
		}
	} else {
		return (1);
	}

	return (BE_SUCCESS);
}

/*
 * Function:	be_create_container_ds
 * Description:	This function checks that the zpool passed has the BE
 *		container dataset, and if not, then creates it.
 * Parameters:
 *		zpool - name of pool to create BE container dataset in.
 * Return:
 *		B_TRUE - Successfully created BE container dataset, or it
 *			already existed.
 *		B_FALSE - Failed to create container dataset.
 * Scope:
 *		Private
 */
static boolean_t
be_create_container_ds(char *zpool)
{
	nvlist_t	*props = NULL;
	char		be_container_ds[MAXPATHLEN];

	/* Generate string for BE container dataset for this pool */
	be_make_container_ds(zpool, be_container_ds,
	    sizeof (be_container_ds));

	if (!zfs_dataset_exists(g_zfs, be_container_ds, ZFS_TYPE_FILESYSTEM)) {

		if (nvlist_alloc(&props, NV_UNIQUE_NAME, 0) != 0) {
			be_print_err(gettext("be_create_container_ds: "
			    "nvlist_alloc failed\n"));
			return (B_FALSE);
		}

		if (nvlist_add_string(props,
		    zfs_prop_to_name(ZFS_PROP_MOUNTPOINT),
		    ZFS_MOUNTPOINT_LEGACY) != 0) {
			be_print_err(gettext("be_create_container_ds: "
			    "internal error: out of memory\n"));
			nvlist_free(props);
			return (B_FALSE);
		}

		if (nvlist_add_string(props,
		    zfs_prop_to_name(ZFS_PROP_CANMOUNT), "off") != 0) {
			be_print_err(gettext("be_create_container_ds: "
			    "internal error: out of memory\n"));
			nvlist_free(props);
			return (B_FALSE);
		}

		if (zfs_create(g_zfs, be_container_ds, ZFS_TYPE_FILESYSTEM,
		    props) != 0) {
			be_print_err(gettext("be_create_container_ds: "
			    "failed to create container dataset (%s): %s\n"),
			    be_container_ds, libzfs_error_description(g_zfs));
			nvlist_free(props);
			return (B_FALSE);
		}

		nvlist_free(props);
	}

	return (B_TRUE);
}

/*
 * Function:	be_prep_clone_send_fs
 * Description:	This function takes a zfs handle to a dataset from the
 *		original BE, and generates the name of the clone dataset
 *		to create for the new BE.  It also prepares the zfs
 *		properties to be used for the new BE.
 * Parameters:
 *		zhp - pointer to zfs_handle_t of the file system being
 *			cloned/copied.
 *		bt - be_transaction_data pointer providing information
 *			about the original BE and new BE.
 *		clone_ds - buffer to store the name of the dataset
 *			for the new BE.
 *		clone_ds_len - length of clone_ds buffer
 * Return:
 *		BE_SUCCESS - Success
 *		be_errno_t - Failure
 * Scope:
 *		Private
 */
static int
be_prep_clone_send_fs(zfs_handle_t *zhp, be_transaction_data_t *bt,
    char *clone_ds, int clone_ds_len)
{
	zprop_source_t	sourcetype;
	char		source[ZFS_MAXNAMELEN];
	char		zhp_name[ZFS_MAXNAMELEN];
	char		mountpoint[MAXPATHLEN];
	char		*child_fs = NULL;
	int		err = 0;

	/*
	 * Get a copy of the dataset name zfs_name from zhp
	 */
	(void) strlcpy(zhp_name, zfs_get_name(zhp), sizeof (zhp_name));

	/*
	 * Get file system name relative to the root.
	 */
	if (strncmp(zhp_name, bt->obe_root_ds, strlen(bt->obe_root_ds)) == 0) {
		child_fs = zhp_name + strlen(bt->obe_root_ds);

		/*
		 * if child_fs is NULL, this means we're processing the
		 * root dataset itself; set child_fs to the empty string.
		 */
		if (child_fs == NULL)
			child_fs = "";
	} else {
		return (BE_ERR_INVAL);
	}

	/*
	 * Generate the name of the clone file system.
	 */
	(void) snprintf(clone_ds, clone_ds_len, "%s%s", bt->nbe_root_ds,
	    child_fs);

	/*
	 * Get the mountpoint and source properties of the existing dataset.
	 * Be sure that the value returned is the persistent, not temporary,
	 * value.
	 */
	if (zfs_prop_get_persistent(zhp, ZFS_PROP_MOUNTPOINT, mountpoint,
	    sizeof (mountpoint), &sourcetype, source, sizeof (source),
	    B_FALSE, NULL) != 0) {
		be_print_err(gettext("be_prep_clone_send_fs: "
		    "failed to get mountpoint for (%s): %s\n"),
		    zhp_name, libzfs_error_description(g_zfs));
		return (zfs_err_to_be_err(g_zfs));
	}

	/*
	 * Workaround for 6668667 where a mountpoint property of "/" comes
	 * back as "".
	 */
	if (strcmp(mountpoint, "") == 0) {
		(void) snprintf(mountpoint, sizeof (mountpoint), "/");
	}

	/*
	 * Figure out what to set as the mountpoint for the new dataset.
	 * If the source of the mountpoint property is local or received,
	 * use the mountpoint value itself.  Otherwise, remove it from the
	 * zfs properties list so that it gets inherited.
	 */
	if (sourcetype & ZPROP_SRC_LOCAL || sourcetype & ZPROP_SRC_RECEIVED) {
		/*
		 * Since we got the persistent mountpoint value above,
		 * it doesn't matter if the BE that this dataset is a
		 * part of is currently mounted at some altroot.  We just
		 * have to set the mountopint for the new dataset with
		 * this same persistent value.
		 */
		if (nvlist_add_string(bt->nbe_zfs_props,
		    zfs_prop_to_name(ZFS_PROP_MOUNTPOINT),
		    mountpoint) != 0) {
			be_print_err(gettext("be_prep_clone_send_fs: "
			    "internal error: out of memory\n"));
			return (BE_ERR_NOMEM);
		}
	} else {
		err = nvlist_remove_all(bt->nbe_zfs_props,
		    zfs_prop_to_name(ZFS_PROP_MOUNTPOINT));
		if (err != 0 && err != ENOENT) {
			be_print_err(gettext("be_prep_clone_send_fs: "
			    "failed to remove mountpoint from "
			    "nvlist\n"));
			return (BE_ERR_INVAL);
		}
	}

	/*
	 * Set the 'canmount' property
	 */
	if (nvlist_add_string(bt->nbe_zfs_props,
	    zfs_prop_to_name(ZFS_PROP_CANMOUNT), "noauto") != 0) {
		be_print_err(gettext("be_prep_clone_send_fs: "
		    "internal error: out of memory\n"));
		return (BE_ERR_NOMEM);
	}

	return (BE_SUCCESS);
}

/*
 * Function:	be_get_zone_be_name
 * Description:	This function takes the zones root dataset, the container
 *		dataset and returns the zones BE name based on the zone
 *		root dataset.
 * Parameters:
 *		root_ds - the zones root dataset.
 *		container_ds - the container dataset for the zone.
 * Returns:
 *		char * - the BE name of this zone based on the root dataset.
 */
static char *
be_get_zone_be_name(char *root_ds, char *container_ds)
{
	return (root_ds + (strlen(container_ds) + 1));
}

/*
 * Function:	be_zone_root_exists_callback
 * Description:	This callback function is used to determine if a
 *		zone root container dataset has any children.  It always
 *		returns 1, signifying a hierarchical child of the zone
 *		root container dataset has been traversed and therefore
 *		it has children.
 * Parameters:
 *		zhp - zfs_handle_t pointer to current dataset being processed.
 *		data - not used.
 * Returns:
 *		1 - dataset exists
 * Scope:
 *		Private
 */
static int
/* LINTED */
be_zone_root_exists_callback(zfs_handle_t *zhp, void *data)
{
	ZFS_CLOSE(zhp);
	return (1);
}
