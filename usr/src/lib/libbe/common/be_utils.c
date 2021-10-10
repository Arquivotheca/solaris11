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

/* Python sets _FILE_OFFSET_BITS unconditionally. */
#include <Python.h>

/*
 * System includes
 */
#include <assert.h>
#include <errno.h>
#include <libgen.h>
#include <libintl.h>
#include <libnvpair.h>
#include <libzfs.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/vfstab.h>
#include <sys/param.h>
#include <sys/systeminfo.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <wait.h>

#include <libbe.h>
#include <libbe_priv.h>

#define	INST_ICT "/usr/lib/python2.6/vendor-packages/osol_install/ict.py"

/* Private function prototypes */
static int update_dataset(char *, int, char *, char *, char *);
static int _update_vfstab(char *, char *, char *, char *, be_fs_list_data_t *);
static char *be_get_auto_name(char *, char *, boolean_t);

/*
 * Global error printing
 */
boolean_t do_print = B_FALSE;

/*
 * Private datatypes
 */
typedef struct zone_be_name_cb_data {
	char *base_be_name;
	int num;
} zone_be_name_cb_data_t;

/* ********************************************************************	*/
/*			Public Functions				*/
/* ******************************************************************** */

/*
 * Function:	be_max_avail
 * Description:	Returns the available size for the zfs dataset passed in.
 * Parameters:
 *		dataset - The dataset we want to get the available space for.
 *		ret - The available size will be returned in this.
 * Returns:
 *		The error returned by the zfs get property function.
 * Scope:
 *		Public
 */
int
be_max_avail(char *dataset, uint64_t *ret)
{
	zfs_handle_t *zhp;
	int err = 0;

	/* Initialize libzfs handle */
	if (!be_zfs_init())
		return (BE_ERR_INIT);

	zhp = zfs_open(g_zfs, dataset, ZFS_TYPE_DATASET);
	if (zhp == NULL) {
		/*
		 * The zfs_open failed return an error
		 */
		err = zfs_err_to_be_err(g_zfs);
	} else {
		err = be_maxsize_avail(zhp, ret);
	}
	ZFS_CLOSE(zhp);
	be_zfs_fini();
	return (err);
}

/*
 * Function:	libbe_print_errors
 * Description:	Turns on/off error output for the library.
 * Parameter:
 *		set_do_print - Boolean that turns library error
 *			       printing on or off.
 * Returns:
 *		None
 * Scope:
 *		Public;
 */
void
libbe_print_errors(boolean_t set_do_print)
{
	do_print = set_do_print;
}

/*
 * Function:	be_get_boot_args_and_mountpt
 * Description:	Get the kernel arguments for a given boot environment,
 *		mounts the BE, and returns the BE mountpoint.
 * Parameters:
 * 		entnum - zero-based number representing the BE (menu order)
 *                       -1 means "use the default boot entry"
 * 		kargs - caller-allocated string into which the boot arguments
 * 		will be placed
 * 		kargssz - size of the buffer kargs
 *		mountp - caller-allocated string into which the mountpoint
 *		of the BE will be copied
 *		mountpsz - size of the buffer mountp
 * Returns:
 * 		Success - 0; kargs will contain the boot arguments,
 *			     mountp will contain the mountpoint of the BE.
 * 		Failure - nonzero; kargs and mountp are undefined.
 * Scope:
 * 		Public
 */
int
be_get_boot_args_and_mountpt(int entnum, char *kargs, size_t kargssz,
    char *mountp, size_t mountpsz)
{
	PyObject *globals, *locals, *kargsobj, *entnumobj, *rcobj;
	PyObject *mountpobj;
	char *pycode;
	int rc = -1;

	if (Py_IsInitialized()) {
		return (BE_ERR_NESTED_PYTHON);
	}

	/* Set up and start the interpreter. */
	Py_SetProgramName("reboot");
	/* Py_NoSiteFlag = 1; */
	Py_InitializeEx(0);

	/* Set up the runtime context. */
	globals = PyDict_New();
	if (PyDict_SetItemString(globals, "__builtins__",
	    PyEval_GetBuiltins())) {
		Py_Finalize();
		return (BE_ERR_PYTHON);
	}
	locals = PyDict_New();
	entnumobj = PyInt_FromLong(entnum);
	if (PyDict_SetItemString(locals, "entnum", entnumobj)) {
		Py_Finalize();
		return (BE_ERR_PYTHON);
	}

	/* Define and run the python code. */
	pycode = "import libbe\n"
	    "rc, kargs, mountp = libbe.get_kargs_and_mountpt(entnum)\n";
	(void) PyRun_String(pycode, Py_file_input, globals, locals);

	/*
	 * Either we got an error, or we have some objects to look at.  The
	 * error processing could be more robust, but it's far more easily done
	 * in get_kargs().
	 */
	if (PyErr_Occurred()) {
		PyErr_PrintEx(0);
		PyErr_Clear();
		rc = BE_ERR_PYTHON_EXCEPT;
	} else {
		kargsobj = PyDict_GetItemString(locals, "kargs");
		mountpobj = PyDict_GetItemString(locals, "mountp");
		rcobj = PyDict_GetItemString(locals, "rc");

		/*
		 * PyInt_AsLong returns -1 on error, too, but -1 is a perfectly
		 * good return code for that situation.
		 */
		rc = PyInt_AsLong(rcobj);
		if (rc == 0) {
			if (strlcpy(kargs, PyString_AsString(kargsobj),
			    kargssz) >= kargssz) {
				if (kargssz > 0)
					kargs[0] = '\0';
				rc = E2BIG;
			}
			if (strlcpy(mountp, PyString_AsString(mountpobj),
			    mountpsz) >= mountpsz) {
				if (mountpsz > 0)
					mountp[0] = '\0';
				rc = E2BIG;
			}
		} else {
			if (kargssz > 0)
				kargs[0] = '\0';
			if (mountpsz > 0)
				mountp[0] = '\0';
		}
	}

	/* Close down the interpreter. */
	Py_Finalize();

	return (rc);
}

/* ********************************************************************	*/
/*			Semi-Private Functions				*/
/* ******************************************************************** */

/*
 * Function:	be_zfs_init
 * Description:	Initializes the libary global libzfs handle.
 * Parameters:
 *		None
 * Returns:
 *		B_TRUE - Success
 *		B_FALSE - Failure
 * Scope:
 *		Semi-private (library wide use only)
 */
boolean_t
be_zfs_init(void)
{
	be_zfs_fini();

	if ((g_zfs = libzfs_init()) == NULL) {
		be_print_err(gettext("be_zfs_init: failed to initialize ZFS "
		    "library\n"));
		return (B_FALSE);
	}

	return (B_TRUE);
}

/*
 * Function:	be_zfs_fini
 * Description:	Closes the library global libzfs handle if it currently open.
 * Parameter:
 *		None
 * Returns:
 *		None
 * Scope:
 *		Semi-private (library wide use only)
 */
void
be_zfs_fini(void)
{
	if (g_zfs)
		libzfs_fini(g_zfs);

	g_zfs = NULL;
}

/*
 * Function:	be_make_root_ds
 * Description:	Generate string for BE's root dataset given the pool
 *		it lives in and the BE name.
 * Parameters:
 *		zpool - pointer zpool name.
 *		be_name - pointer to BE name.
 *		be_root_ds - pointer to buffer to return BE root dataset in.
 *		be_root_ds_size - size of be_root_ds
 * Returns:
 *		None
 * Scope:
 *		Semi-private (library wide use only)
 */
void
be_make_root_ds(const char *zpool, const char *be_name, char *be_root_ds,
    int be_root_ds_size)
{
	(void) snprintf(be_root_ds, be_root_ds_size, "%s/%s/%s", zpool,
	    BE_CONTAINER_DS_NAME, be_name);
}

/*
 * Function:	be_make_nested_container_ds
 * Description:	Generate string for a zone's BE rpool dataset given a zonepath
 *              dataset.
 * Parameters:
 *		zonepath - pointer zpool name.
 *		rpool_ds - pointer to buffer to return the rpool dataset in.
 *		rpool_ds_size - size of rpool_ds
 * Returns:
 *		None
 * Scope:
 *		Semi-private (library wide use only)
 */
void
be_make_nested_container_ds(const char *zonepath, char *rpool_ds,
    int rpool_ds_size)
{
	(void) snprintf(rpool_ds, rpool_ds_size, "%s/%s", zonepath,
	    BE_ZONE_RPOOL_NAME);
}

/*
 * Function:	be_make_container_ds
 * Description:	Generate string for the BE container dataset given a pool name.
 * Parameters:
 *		zpool - pointer zpool name.
 *		container_ds - pointer to buffer to return BE container
 *			dataset in.
 *		container_ds_size - size of container_ds
 * Returns:
 *		None
 * Scope:
 *		Semi-private (library wide use only)
 */
void
be_make_container_ds(const char *zpool, char *container_ds,
    int container_ds_size)
{
	(void) snprintf(container_ds, container_ds_size, "%s/%s", zpool,
	    BE_CONTAINER_DS_NAME);
}

/*
 * Function:	be_make_name_from_ds
 * Description:	This function takes a dataset name and strips off the
 *		BE container dataset portion from the beginning.  The
 *		returned name is allocated in heap storage, so the caller
 *		is responsible for freeing it.
 * Parameters:
 *		dataset - dataset to get name from.
 *		rc_loc - dataset underwhich the root container dataset lives.
 * Returns:
 *		name of dataset relative to BE container dataset.
 *		NULL if dataset is not under a BE root dataset.
 * Scope:
 *		Semi-primate (library wide use only)
 */
char *
be_make_name_from_ds(const char *dataset, char *rc_loc)
{
	char	ds[ZFS_MAXNAMELEN];
	char	*tok = NULL;
	char	*name = NULL;

	/*
	 * First token is the location of where the root container dataset
	 * lives; it must match rc_loc.
	 */
	if (strncmp(dataset, rc_loc, strlen(rc_loc)) == 0 &&
	    dataset[strlen(rc_loc)] == '/') {
		(void) strlcpy(ds, dataset + strlen(rc_loc) + 1, sizeof (ds));
	} else {
		return (NULL);
	}

	/* Second token must be BE container dataset name */
	if ((tok = strtok(ds, "/")) == NULL ||
	    strcmp(tok, BE_CONTAINER_DS_NAME) != 0)
		return (NULL);

	/* Return the remaining token if one exists */
	if ((tok = strtok(NULL, "")) == NULL)
		return (NULL);

	if ((name = strdup(tok)) == NULL) {
		be_print_err(gettext("be_make_name_from_ds: "
		    "memory allocation failed\n"));
		return (NULL);
	}

	return (name);
}

/*
 * Function:	be_maxsize_avail
 * Description:	Returns the available size for the zfs handle passed in.
 * Parameters:
 *		zhp - A pointer to the open zfs handle.
 *		ret - The available size will be returned in this.
 * Returns:
 *		The error returned by the zfs get property function.
 * Scope:
 *		Semi-private (library wide use only)
 */
int
be_maxsize_avail(zfs_handle_t *zhp, uint64_t *ret)
{
	return ((*ret = zfs_prop_get_int(zhp, ZFS_PROP_AVAILABLE)));
}

/*
 * Function:	be_update_vfstab
 * Description:	This function digs into a BE's vfstab and updates all
 *		entries with file systems listed in be_fs_list_data_t.
 *		The entry's root container dataset and be_name will be
 *		updated with the parameters passed in.
 * Parameters:
 *		be_name - name of BE to update
 *		old_rc_loc - dataset under which the root container dataset
 *			of the old BE resides in.
 *		new_rc_loc - dataset under which the root container dataset
 *			of the new BE resides in.
 *		fld - be_fs_list_data_t pointer providing the list of
 *			file systems to look for in vfstab.
 *		mountpoint - directory of where BE is currently mounted.
 *			If NULL, then BE is not currently mounted.
 * Returns:
 *		BE_SUCCESS - Success
 *		be_errno_t - Failure
 * Scope:
 *		Semi-private (library wide use only)
 */
int
be_update_vfstab(char *be_name, char *old_rc_loc, char *new_rc_loc,
    be_fs_list_data_t *fld, char *mountpoint)
{
	char		*tmp_mountpoint = NULL;
	char		alt_vfstab[MAXPATHLEN];
	int		ret = BE_SUCCESS, err = BE_SUCCESS;
	uint16_t	umnt_flags = BE_UNMOUNT_FLAG_NULL;

	if (fld == NULL || fld->fs_list == NULL || fld->fs_num == 0)
		return (BE_SUCCESS);

	/* If BE not already mounted, mount the BE */
	if (mountpoint == NULL) {
		if ((ret = _be_mount(be_name, NULL, &tmp_mountpoint,
		    BE_MOUNT_FLAG_NO_ZONES)) != BE_SUCCESS) {
			be_print_err(gettext("be_update_vfstab: "
			    "failed to mount BE (%s)\n"), be_name);
			return (ret);
		}
		umnt_flags |= BE_UNMOUNT_FLAG_NO_ZONES;
	} else {
		tmp_mountpoint = mountpoint;
	}

	/* Get string for vfstab in the mounted BE. */
	(void) snprintf(alt_vfstab, sizeof (alt_vfstab), "%s/etc/vfstab",
	    tmp_mountpoint);

	/* Update the vfstab */
	ret = _update_vfstab(alt_vfstab, be_name, old_rc_loc, new_rc_loc,
	    fld);

	/* Unmount BE if we mounted it */
	if (mountpoint == NULL) {
		if ((err = _be_unmount(be_name, NULL, umnt_flags))
		    == BE_SUCCESS) {
			/* Remove temporary mountpoint */
			(void) rmdir(tmp_mountpoint);
		} else {
			be_print_err(gettext("be_update_vfstab: "
			    "failed to unmount BE %s mounted at %s\n"),
			    be_name, tmp_mountpoint);
			if (ret == BE_SUCCESS)
				ret = err;
		}

		free(tmp_mountpoint);
	}

	return (ret);
}

/*
 * Function:	be_auto_snap_name
 * Description:	Generate an auto snapshot name constructed based on the
 *		current date and time.  The auto snapshot name is of the form:
 *
 *			<date>-<time>
 *
 *		where <date> is in ISO standard format, so the resultant name
 *		is of the form:
 *
 *			%Y-%m-%d-%H:%M:%S
 *
 * Parameters:
 *		None
 * Returns:
 *		Success - pointer to auto generated snapshot name.  The name
 *			is allocated in heap storage so the caller is
 *			responsible for free'ing the name.
 *		Failure - NULL
 * Scope:
 *		Semi-private (library wide use only)
 */
char *
be_auto_snap_name(void)
{
	time_t		utc_tm = NULL;
	struct tm	*gmt_tm = NULL;
	char		gmt_time_str[64];
	char		*auto_snap_name = NULL;

	if (time(&utc_tm) == -1) {
		be_print_err(gettext("be_auto_snap_name: time() failed\n"));
		return (NULL);
	}

	if ((gmt_tm = gmtime(&utc_tm)) == NULL) {
		be_print_err(gettext("be_auto_snap_name: gmtime() failed\n"));
		return (NULL);
	}

	(void) strftime(gmt_time_str, sizeof (gmt_time_str), "%F-%T", gmt_tm);

	if ((auto_snap_name = strdup(gmt_time_str)) == NULL) {
		be_print_err(gettext("be_auto_snap_name: "
		    "memory allocation failed\n"));
		return (NULL);
	}

	return (auto_snap_name);
}

/*
 * Function:	be_auto_be_name
 * Description:	Generate an auto BE name constructed based on the BE name
 *		of the original BE being cloned.
 * Parameters:
 *		obe_name - name of the original BE being cloned.
 * Returns:
 *		Success - pointer to auto generated BE name.  The name
 *			is allocated in heap storage so the caller is
 *			responsible for free'ing the name.
 *		Failure - NULL
 * Scope:
 *		Semi-private (library wide use only)
 */
char *
be_auto_be_name(char *obe_name)
{
	return (be_get_auto_name(obe_name, NULL, B_FALSE));
}

/*
 * Function:	be_auto_zone_be_name
 * Description:	Generate an auto BE name for a zone constructed based on
 *              the BE name of the original zone BE being cloned.
 * Parameters:
 *              container_ds - container dataset for the zone.
 *		zbe_name - name of the original zone BE being cloned.
 * Returns:
 *		Success - pointer to auto generated BE name.  The name
 *			is allocated in heap storage so the caller is
 *			responsible for free'ing the name.
 *		Failure - NULL
 * Scope:
 *		Semi-private (library wide use only)
 */
char *
be_auto_zone_be_name(char *container_ds, char *zbe_name)
{
	return (be_get_auto_name(zbe_name, container_ds, B_TRUE));
}

/*
 * Function:	be_valid_be_name
 * Description:	Validates a BE name.
 * Parameters:
 *		be_name - name of BE to validate
 * Returns:
 *		B_TRUE - be_name is valid
 *		B_FALSE - be_name is invalid
 * Scope:
 *		Semi-private (library wide use only)
 */

boolean_t
be_valid_be_name(const char *be_name)
{
	const char	*c = NULL;

	if (be_name == NULL)
		return (B_FALSE);

	/*
	 * A BE name must not be a multi-level dataset name.  We also check
	 * that it does not contain the ' ' and '%' characters.  The ' ' is
	 * a valid character for datasets, however we don't allow that in a
	 * BE name.  The '%' is invalid, but zfs_name_valid() allows it for
	 * internal reasons, so we explicitly check for it here.
	 */
	c = be_name;
	while (*c != '\0' && *c != '/' && *c != ' ' && *c != '%')
		c++;

	if (*c != '\0')
		return (B_FALSE);

	/*
	 * The BE name must comply with a zfs dataset filesystem. We also
	 * verify its length to be < BE_NAME_MAX_LEN.
	 */
	if (!zfs_name_valid(be_name, ZFS_TYPE_FILESYSTEM) ||
	    strlen(be_name) > BE_NAME_MAX_LEN)
		return (B_FALSE);

	return (B_TRUE);
}

/*
 * Function:	be_valid_auto_snap_name
 * Description:	This function checks that a snapshot name is a valid auto
 *		generated snapshot name.  A valid auto generated snapshot
 *		name is of the form:
 *
 *			%Y-%m-%d-%H:%M:%S
 *
 *		An older form of the auto generated snapshot name also
 *		included the snapshot's BE cleanup policy and a reserved
 *		field.  Those names will also be verified by this function.
 *
 *		Examples of valid auto snapshot names are:
 *
 *			2008-03-31-18:41:30
 *			2008-03-31-22:17:24
 *			<policy>:-:2008:04-05-09:12:55
 *			<policy>:-:2008:04-06-15:34:12
 *
 * Parameters:
 *		name - name of the snapshot to be validated.
 * Returns:
 *		B_TRUE - the name is a valid auto snapshot name.
 *		B_FALSE - the name is not a valid auto snapshot name.
 * Scope:
 *		Semi-private (library wide use only)
 */
boolean_t
be_valid_auto_snap_name(char *name)
{
	struct tm gmt_tm;

	char *policy = NULL;
	char *reserved = NULL;
	char *date = NULL;
	char *c = NULL;

	/* Validate the snapshot name by converting it into utc time */
	if (strptime(name, "%Y-%m-%d-%T", &gmt_tm) != NULL &&
	    (mktime(&gmt_tm) != -1)) {
		return (B_TRUE);
	}

	/*
	 * Validate the snapshot name against the older form of an
	 * auto generated snapshot name.
	 */
	policy = strdup(name);

	/*
	 * Get the first field from the snapshot name,
	 * which is the BE policy
	 */
	c = strchr(policy, ':');
	if (c == NULL) {
		free(policy);
		return (B_FALSE);
	}
	c[0] = '\0';

	/* Validate the policy name */
	if (!valid_be_policy(policy)) {
		free(policy);
		return (B_FALSE);
	}

	/* Get the next field, which is the reserved field. */
	if (c[1] == NULL || c[1] == '\0') {
		free(policy);
		return (B_FALSE);
	}
	reserved = c+1;
	c = strchr(reserved, ':');
	if (c == NULL) {
		free(policy);
		return (B_FALSE);
	}
	c[0] = '\0';

	/* Validate the reserved field */
	if (strcmp(reserved, "-") != 0) {
		free(policy);
		return (B_FALSE);
	}

	/* The remaining string should be the date field */
	if (c[1] == NULL || c[1] == '\0') {
		free(policy);
		return (B_FALSE);
	}
	date = c+1;

	/* Validate the date string by converting it into utc time */
	if (strptime(date, "%Y-%m-%d-%T", &gmt_tm) == NULL ||
	    (mktime(&gmt_tm) == -1)) {
		be_print_err(gettext("be_valid_auto_snap_name: "
		    "invalid auto snapshot name\n"));
		free(policy);
		return (B_FALSE);
	}

	free(policy);
	return (B_TRUE);
}

/*
 * Function:	be_default_policy
 * Description:	Temporary hardcoded policy support.  This function returns
 *		the default policy type to be used to create a BE or a BE
 *		snapshot.
 * Parameters:
 *		None
 * Returns:
 *		Name of default BE policy.
 * Scope:
 *		Semi-private (library wide use only)
 */
char *
be_default_policy(void)
{
	return (BE_PLCY_STATIC);
}

/*
 * Function:	valid_be_policy
 * Description:	Temporary hardcoded policy support.  This function valids
 *		whether a policy is a valid known policy or not.
 * Paramters:
 *		policy - name of policy to validate.
 * Returns:
 *		B_TRUE - policy is a valid.
 *		B_FALSE - policy is invalid.
 * Scope:
 *		Semi-private (library wide use only)
 */
boolean_t
valid_be_policy(char *policy)
{
	if (policy == NULL)
		return (B_FALSE);

	if (strcmp(policy, BE_PLCY_STATIC) == 0 ||
	    strcmp(policy, BE_PLCY_VOLATILE) == 0) {
		return (B_TRUE);
	}

	return (B_FALSE);
}

/*
 * Function:	be_print_err
 * Description:	This function prints out error messages if do_print is
 *		set to B_TRUE or if the BE_PRINT_ERR environment variable
 *		is set to true.
 * Paramters:
 *		prnt_str - the string we wish to print and any arguments
 *		for the format of that string.
 * Returns:
 *		void
 * Scope:
 *		Semi-private (library wide use only)
 */
void
be_print_err(char *prnt_str, ...)
{
	va_list ap;
	char buf[BUFSIZ];
	char *env_buf;
	static boolean_t env_checked = B_FALSE;

	if (!env_checked) {
		if ((env_buf = getenv("BE_PRINT_ERR")) != NULL) {
			if (strcasecmp(env_buf, "true") == 0) {
				do_print = B_TRUE;
			}
		}
		env_checked = B_TRUE;
	}

	if (do_print) {
		va_start(ap, prnt_str);
		/* LINTED variable format specifier */
		(void) vsnprintf(buf, BUFSIZ, prnt_str, ap);
		(void) fputs(buf, stderr);
		va_end(ap);
	}
}

/*
 * Function:	be_find_current_be
 * Description:	Find the currently "active" BE. Fill in the
 * 		passed in be_transaction_data_t reference with the
 *		active BE's data.
 * Paramters:
 *		none
 * Returns:
 *		BE_SUCCESS - Success
 *		be_errnot_t - Failure
 * Scope:
 *		Semi-private (library wide use only)
 */
int
be_find_current_be(be_transaction_data_t *bt)
{
	int	zret, ret = BE_SUCCESS;
	char	zpool_analog[MAXPATHLEN];
	zfs_handle_t	*zhp = NULL;
	char	be_container_ds[MAXPATHLEN];
	int	zfs_init = 0;

	if (g_zfs == NULL) {
		if (!be_zfs_init())
			return (BE_ERR_INIT);
		zfs_init = 1;
	}

	if (getzoneid() == GLOBAL_ZONEID) {
		if ((zret = zpool_iter(g_zfs,
		    be_zpool_find_current_be_callback, bt)) == 0) {
			be_print_err(gettext("be_find_current_be: failed to "
			    "find current BE name\n"));
			ret = BE_ERR_BE_NOENT;
		} else if (zret < 0) {
			be_print_err(gettext("be_find_current_be: "
			    "zpool_iter failed: %s\n"),
			    libzfs_error_description(g_zfs));
			ret = zfs_err_to_be_err(g_zfs);
		}
	} else {
		if (be_zone_get_zpool_analog(zpool_analog) == BE_SUCCESS) {
			bt->obe_zpool = strdup(zpool_analog);
		} else {
			be_print_err(gettext("be_find_current_be: "
			    "failed to find zpool analog\n"));
			ret = BE_ERR_BE_NOENT;
			goto out;
		}

		be_make_container_ds(bt->obe_zpool, be_container_ds,
		    sizeof (be_container_ds));

		/*
		 * Get handle to this pool analog's BE container dataset.
		 */
		if ((zhp = zfs_open(g_zfs, be_container_ds,
		    ZFS_TYPE_FILESYSTEM)) == NULL) {
			be_print_err(gettext("be_find_current_be: "
			    "failed to open BE container dataset (%s)\n"),
			    be_container_ds);
			ret = 0;
			goto out;
		}

		/*
		 * Iterate through all potential BEs in this zpool
		 */
		if (zfs_iter_filesystems(zhp, be_zfs_find_current_be_callback,
		    bt) == 1) {
			ZFS_CLOSE(zhp);
			ret = BE_SUCCESS;
		} else {
			ZFS_CLOSE(zhp);
			ret = BE_ERR_BE_NOENT;
		}
	}

out:
	if (zfs_init)
		be_zfs_fini();

	return (ret);
}

/*
 * Function:	be_zpool_find_current_be_callback
 * Description: Callback function used to iterate through all existing pools
 *		to find the BE that is the currently booted BE.
 * Parameters:
 *		zlp - zpool_handle_t pointer to the current pool being
 *			looked at.
 *		data - be_transaction_data_t pointer.
 *			Upon successfully finding the current BE, the
 *			obe_zpool member of this parameter is set to the
 *			pool it is found in.
 * Return:
 *		1 - Found current BE in this pool.
 *		0 - Did not find current BE in this pool.
 * Scope:
 *		Semi-private (library wide use only)
 */
int
be_zpool_find_current_be_callback(zpool_handle_t *zlp, void *data)
{
	be_transaction_data_t	*bt = data;
	zfs_handle_t		*zhp = NULL;
	const char		*zpool =  zpool_get_name(zlp);
	char			be_container_ds[MAXPATHLEN];

	/*
	 * Generate string for BE container dataset
	 */
	be_make_container_ds(zpool, be_container_ds, sizeof (be_container_ds));

	/*
	 * Check if a BE container dataset exists in this pool.
	 */
	if (!zfs_dataset_exists(g_zfs, be_container_ds, ZFS_TYPE_FILESYSTEM)) {
		zpool_close(zlp);
		return (0);
	}

	/*
	 * Get handle to this zpool's BE container dataset.
	 */
	if ((zhp = zfs_open(g_zfs, be_container_ds, ZFS_TYPE_FILESYSTEM)) ==
	    NULL) {
		be_print_err(gettext("be_zpool_find_current_be_callback: "
		    "failed to open BE container dataset (%s)\n"),
		    be_container_ds);
		zpool_close(zlp);
		return (0);
	}

	/*
	 * Iterate through all potential BEs in this zpool
	 */
	if (zfs_iter_filesystems(zhp, be_zfs_find_current_be_callback, bt)) {
		/*
		 * Found current BE dataset; set obe_zpool
		 */
		if ((bt->obe_zpool = strdup(zpool)) == NULL) {
			be_print_err(gettext(
			    "be_zpool_find_current_be_callback: "
			    "memory allocation failed\n"));
			ZFS_CLOSE(zhp);
			zpool_close(zlp);
			return (0);
		}

		ZFS_CLOSE(zhp);
		zpool_close(zlp);
		return (1);
	}

	ZFS_CLOSE(zhp);
	zpool_close(zlp);

	return (0);
}

/*
 * Function:	be_zfs_find_current_be_callback
 * Description:	Callback function used to iterate through all BEs in a
 *		pool to find the BE that is the currently booted BE.
 * Parameters:
 *		zhp - zfs_handle_t pointer to current filesystem being checked.
 *		data - be_transaction-data_t pointer
 *			Upon successfully finding the current BE, the
 *			obe_name and obe_root_ds members of this parameter
 *			are set to the BE name and BE's root dataset
 *			respectively.
 * Return:
 *		1 - Found current BE.
 *		0 - Did not find current BE.
 * Scope:
 *		Semi-private (library wide use only)
 */
int
be_zfs_find_current_be_callback(zfs_handle_t *zhp, void *data)
{
	be_transaction_data_t	*bt = data;
	char			*mp = NULL;

	/*
	 * Check if dataset is mounted, and if so where.
	 */
	if (zfs_is_mounted(zhp, &mp)) {
		/*
		 * If mounted at root, set obe_root_ds and obe_name
		 */
		if (mp != NULL && strcmp(mp, "/") == 0) {
			free(mp);

			if ((bt->obe_root_ds = strdup(zfs_get_name(zhp)))
			    == NULL) {
				be_print_err(gettext(
				    "be_zfs_find_current_be_callback: "
				    "memory allocation failed\n"));
				ZFS_CLOSE(zhp);
				return (0);
			}

			if ((bt->obe_name = strdup(basename(bt->obe_root_ds)))
			    == NULL) {
				be_print_err(gettext(
				    "be_zfs_find_current_be_callback: "
				    "memory allocation failed\n"));
				ZFS_CLOSE(zhp);
				return (0);
			}

			ZFS_CLOSE(zhp);
			return (1);
		}

		free(mp);
	}
	ZFS_CLOSE(zhp);

	return (0);
}

/*
 * Function:	be_check_be_roots_callback
 * Description:	This function checks whether or not the dataset name passed
 *		is hierachically located under the BE root container dataset
 *		for this pool.
 * Parameters:
 *		zlp - zpool_handle_t pointer to current pool being processed.
 *		data - name of dataset to check
 * Returns:
 *		0 - dataset is not in this pool's BE root container dataset
 *		1 - dataset is in this pool's BE root container dataset
 * Scope:
 *		Semi-private (library wide use only)
 */
int
be_check_be_roots_callback(zpool_handle_t *zlp, void *data)
{
	const char	*zpool;
	char		*ds = data;
	char		be_container_ds[MAXPATHLEN];

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
		char	zpool_analog[MAXPATHLEN];

		if (be_zone_get_zpool_analog(zpool_analog) == BE_SUCCESS) {
			zpool = strdup(zpool_analog);
		} else {
			zpool_close(zlp);
			return (0);
		}
	} else {
		zpool = zpool_get_name(zlp);
	}

	/* Generate string for this pool's BE root container dataset */
	be_make_container_ds(zpool, be_container_ds, sizeof (be_container_ds));

	/*
	 * If dataset lives under the BE root container dataset
	 * of this pool, return failure.
	 */
	if (strncmp(be_container_ds, ds, strlen(be_container_ds)) == 0 &&
	    ds[strlen(be_container_ds)] == '/') {
		zpool_close(zlp);
		return (1);
	}

	zpool_close(zlp);
	return (0);
}

/*
 * Function:	zfs_err_to_be_err
 * Description:	This function takes the error stored in the libzfs handle
 *		and maps it to an be_errno_t. If there are no matching
 *		be_errno_t's then BE_ERR_ZFS is returned.
 * Paramters:
 *		zfsh - The libzfs handle containing the error we're looking up.
 * Returns:
 *		be_errno_t
 * Scope:
 *		Semi-private (library wide use only)
 */
int
zfs_err_to_be_err(libzfs_handle_t *zfsh)
{
	int err = libzfs_errno(zfsh);

	switch (err) {
	case 0:
		return (BE_SUCCESS);
	case EZFS_PERM:
		return (BE_ERR_PERM);
	case EZFS_INTR:
		return (BE_ERR_INTR);
	case EZFS_NOENT:
		return (BE_ERR_NOENT);
	case EZFS_NOSPC:
		return (BE_ERR_NOSPC);
	case EZFS_MOUNTFAILED:
		return (BE_ERR_MOUNT);
	case EZFS_UMOUNTFAILED:
		return (BE_ERR_UMOUNT);
	case EZFS_EXISTS:
		return (BE_ERR_BE_EXISTS);
	case EZFS_BUSY:
		return (BE_ERR_DEV_BUSY);
	case EZFS_POOLREADONLY:
		return (BE_ERR_ROFS);
	case EZFS_NAMETOOLONG:
		return (BE_ERR_NAMETOOLONG);
	case EZFS_NODEVICE:
		return (BE_ERR_NODEV);
	case EZFS_POOL_INVALARG:
		return (BE_ERR_INVAL);
	case EZFS_PROPTYPE:
		return (BE_ERR_INVALPROP);
	case EZFS_BADTYPE:
		return (BE_ERR_DSTYPE);
	case EZFS_PROPNONINHERIT:
		return (BE_ERR_NONINHERIT);
	case EZFS_PROPREADONLY:
		return (BE_ERR_READONLYPROP);
	case EZFS_RESILVERING:
	case EZFS_POOLUNAVAIL:
		return (BE_ERR_UNAVAIL);
	case EZFS_DSREADONLY:
		return (BE_ERR_READONLYDS);
	default:
		return (BE_ERR_ZFS);
	}
}

/*
 * Function:	errno_to_be_err
 * Description:	This function takes an errno and maps it to an be_errno_t.
 *		If there are no matching be_errno_t's then BE_ERR_UNKNOWN is
 *		returned.
 * Paramters:
 *		err - The errno we're compairing against.
 * Returns:
 *		be_errno_t
 * Scope:
 *		Semi-private (library wide use only)
 */
int
errno_to_be_err(int err)
{
	switch (err) {
	case EPERM:
		return (BE_ERR_PERM);
	case EACCES:
		return (BE_ERR_ACCESS);
	case ECANCELED:
		return (BE_ERR_CANCELED);
	case EINTR:
		return (BE_ERR_INTR);
	case ENOENT:
		return (BE_ERR_NOENT);
	case ENOSPC:
	case EDQUOT:
		return (BE_ERR_NOSPC);
	case EEXIST:
		return (BE_ERR_BE_EXISTS);
	case EBUSY:
		return (BE_ERR_BUSY);
	case EROFS:
		return (BE_ERR_ROFS);
	case ENAMETOOLONG:
		return (BE_ERR_NAMETOOLONG);
	case ENXIO:
		return (BE_ERR_NXIO);
	case EINVAL:
		return (BE_ERR_INVAL);
	case EFAULT:
		return (BE_ERR_FAULT);
	default:
		return (BE_ERR_UNKNOWN);
	}
}

/*
 * Function:	be_err_to_str
 * Description:	This function takes a be_errno_t and maps it to a message.
 *		If there are no matching be_errno_t's then NULL is returned.
 * Paramters:
 *		be_errno_t - The be_errno_t we're mapping.
 * Returns:
 *		string or NULL if the error code is not known.
 * Scope:
 *		Semi-private (library wide use only)
 */
char *
be_err_to_str(int err)
{
	switch (err) {
	case BE_ERR_ACCESS:
		return (gettext("Permission denied."));
	case BE_ERR_ACTIVATE_CURR:
		return (gettext("Activation of current BE failed."));
	case BE_ERR_AUTONAME:
		return (gettext("Auto naming failed."));
	case BE_ERR_BE_NOENT:
		return (gettext("No such BE."));
	case BE_ERR_BUSY:
		return (gettext("Mount busy."));
	case BE_ERR_DEV_BUSY:
		return (gettext("Device busy."));
	case BE_ERR_CANCELED:
		return (gettext("Operation canceled."));
	case BE_ERR_CLONE:
		return (gettext("BE clone failed."));
	case BE_ERR_COPY:
		return (gettext("BE copy failed."));
	case BE_ERR_CREATDS:
		return (gettext("Dataset creation failed."));
	case BE_ERR_CURR_BE_NOT_FOUND:
		return (gettext("Can't find current BE."));
	case BE_ERR_DESTROY:
		return (gettext("Failed to destroy BE or snapshot."));
	case BE_ERR_DESTROY_CURR_BE:
		return (gettext("Cannot destroy current BE."));
	case BE_ERR_DEMOTE:
		return (gettext("BE demotion failed."));
	case BE_ERR_DSTYPE:
		return (gettext("Invalid dataset type."));
	case BE_ERR_BE_EXISTS:
		return (gettext("BE exists."));
	case BE_ERR_INIT:
		return (gettext("be_zfs_init failed."));
	case BE_ERR_INTR:
		return (gettext("Interupted system call."));
	case BE_ERR_INVAL:
		return (gettext("Invalid argument."));
	case BE_ERR_INVALPROP:
		return (gettext("Invalid property for dataset."));
	case BE_ERR_INVALMOUNTPOINT:
		return (gettext("Unexpected mountpoint."));
	case BE_ERR_MOUNT:
		return (gettext("Mount failed."));
	case BE_ERR_MOUNTED:
		return (gettext("Already mounted."));
	case BE_ERR_NAMETOOLONG:
		return (gettext("name > BUFSIZ."));
	case BE_ERR_NO_RPOOLS:
		return (gettext("No root pools could be found."));
	case BE_ERR_NOENT:
		return (gettext("Doesn't exist."));
	case BE_ERR_POOL_NOENT:
		return (gettext("No such pool."));
	case BE_ERR_NODEV:
		return (gettext("No such device."));
	case BE_ERR_NOTMOUNTED:
		return (gettext("File system not mounted."));
	case BE_ERR_NOMEM:
		return (gettext("Not enough memory."));
	case BE_ERR_NONINHERIT:
		return (gettext(
		    "Property is not inheritable for the BE dataset."));
	case BE_ERR_NXIO:
		return (gettext("No such device or address."));
	case BE_ERR_NOSPC:
		return (gettext("No space on device."));
	case BE_ERR_NOTSUP:
		return (gettext("Operation not supported."));
	case BE_ERR_OPEN:
		return (gettext("Open failed."));
	case BE_ERR_PERM:
		return (gettext("Not owner."));
	case BE_ERR_UNAVAIL:
		return (gettext("The BE is currently unavailable."));
	case BE_ERR_PROMOTE:
		return (gettext("BE promotion failed."));
	case BE_ERR_ROFS:
		return (gettext("Read only file system."));
	case BE_ERR_READONLYDS:
		return (gettext("Read only dataset."));
	case BE_ERR_READONLYPROP:
		return (gettext("Read only property."));
	case BE_ERR_RENAME_ACTIVE:
		return (gettext("Renaming the active BE is not supported."));
	case BE_ERR_RENAME_ACTIVE_ON_BOOT:
		return (gettext("Renaming the active on boot BE is not " \
		    "supported.\nTo rename it, activate some other BE " \
		    "first."));
	case BE_ERR_SS_EXISTS:
		return (gettext("Snapshot exists."));
	case BE_ERR_SS_NOENT:
		return (gettext("No such snapshot."));
	case BE_ERR_UMOUNT:
		return (gettext("Unmount failed."));
	case BE_ERR_UMOUNT_CURR_BE:
		return (gettext("Can't unmount the current BE."));
	case BE_ERR_UMOUNT_SHARED:
		return (gettext("Unmount of a shared File System failed."));
	case BE_ERR_FAULT:
		return (gettext("Bad address."));
	case BE_ERR_UNKNOWN:
		return (gettext("Unknown error."));
	case BE_ERR_ZFS:
		return (gettext("ZFS returned an error."));
	case BE_ERR_GEN_UUID:
		return (gettext("Failed to generate uuid."));
	case BE_ERR_PARSE_UUID:
		return (gettext("Failed to parse uuid."));
	case BE_ERR_NO_UUID:
		return (gettext("No uuid"));
	case BE_ERR_ZONE_NO_PARENTBE:
		return (gettext("No parent uuid"));
	case BE_ERR_ZONE_MULTIPLE_ACTIVE:
		return (gettext("Multiple active zone roots"));
	case BE_ERR_ZONE_NO_ACTIVE_ROOT:
		return (gettext("No active zone root"));
	case BE_ERR_ZONE_ROOT_NOT_SLASH:
		return (gettext("Zone root not /"));
	case BE_ERR_MOUNT_ZONEROOT:
		return (gettext("Failed to mount a zone root."));
	case BE_ERR_UMOUNT_ZONEROOT:
		return (gettext("Failed to unmount a zone root."));
	case BE_ERR_NO_MOUNTED_ZONE:
		return (gettext("Zone is not mounted"));
	case BE_ERR_ZONES_UNMOUNT:
		return (gettext("Unable to unmount a zone BE."));
	case BE_ERR_ZONE_SS_EXISTS:
		return (gettext("Zone snapshot exists."));
	case BE_ERR_ADD_SPLASH_ICT:
		return (gettext("Add_spash_image ICT failed."));
	case BE_ERR_PKG_VERSION:
		return (gettext("Package versioning error."));
	case BE_ERR_PKG:
		return (gettext("Error running pkg(1M)."));
	case BE_ERR_BOOTFILE_INST:
		return (gettext("Error installing boot files."));
	case BE_ERR_EXTCMD:
		return (gettext("Error running an external command."));
	case BE_ERR_ZONE_NOTSUP:
		return (gettext("Operation not supported for zone BE"));
	case BE_ERR_ZONE_MPOOL_NOTSUP:
		return (gettext("Multiple pools not supported for zone BEs"));
	case BE_ERR_ZONE_RO_NOTSUP:
		return (gettext(
		    "Operation not supported in a read-only environment"));
	case BE_ERR_NO_MENU_ENTRY:
		return (gettext("No such entry found"));
	case BE_ERR_NESTED_PYTHON:
		return (gettext("Nested Python interpreters not allowed"));
	case BE_ERR_PYTHON:
		return (gettext("Python library call failed"));
	case BE_ERR_PYTHON_EXCEPT:
		return (gettext("Python code raised an exception"));
	default:
		return (NULL);
	}
}

/*
 * Function: be_run_cmd
 * Description:
 *	Runs a command in a separate subprocess.  Splits out stdout from stderr
 *	and sends each to its own buffer.  Buffers must be pre-allocated and
 *	passed in as arguments.  Buffer sizes are also passed in as arguments.
 *	command_status_p returns subprocess exit status.
 *
 *	Notes / caveats:
 *	- Command being run is assumed to not have any stdout or stderr
 *		redirection.
 *	- Commands which emit total stderr output of greater than PIPE_BUF
 *		bytes can hang.  For such commands, a different implementation
 *		which uses poll(2) must be used.
 *	- Only subprocess errors are appended to the stderr_buf.  Errors
 *		running the command (vs stderr output generated by the command
 *		itself) are reported through be_print_err().
 *	- Data which would overflow its respective buffer is sent to the bit
 *		bucket.
 *
 * Parameters:
 *		command: command to run.  Assumed not to have embedded stdout
 *			or stderr redirection.  May have stdin redirection,
 *			however.
 *		command_status_p: If not NULL, buffer for retrieving exit
 *			status of the command, if the command is run.  Not
 *			altered if command is not run.
 *		stderr_buf: buffer returning subprocess stderr data.
 *			Cannot be NULL.
 *		stderr_bufsize: size of stderr_buf
 *		stdout_buf: buffer returning subprocess stdout data.  If NULL,
 *			stdout_bufsize is ignored, and the stream which would
 *			have gone to it is sent to the bit bucket.
 *		stdout_bufsize: size of stdout_buf
 * Returns:
 *		BE_SUCCESS - The command ran successfully without returning
 *			errors.
 *		BE_ERR_EXTCMD
 *			- The command could not be run.
 *			- The command terminated with error status.
 *			- There were errors extracting or returning subprocess
 *				data.
 *		BE_ERR_NOMEM - The command exceeds the command buffer size.
 *		BE_ERR_INVAL - An invalid argument was specified.
 * Scope:
 *		Semi-private (library wide use only)
 */
int
be_run_cmd(char *command, int *command_status_p, char *stderr_buf,
    int stderr_bufsize, char *stdout_buf, int stdout_bufsize)
{
	char *temp_filename = strdup(tmpnam(NULL));
	FILE *stdout_str = NULL;
	FILE *stderr_str = NULL;
	char cmdline[BUFSIZ];
	char oneline[BUFSIZ];
	int exit_status;
	int rval = BE_SUCCESS;

	if ((command == NULL) || (stderr_buf == NULL) ||
	    (stderr_bufsize <= 0) || (stdout_bufsize <  0) ||
	    ((stdout_buf != NULL) ^ (stdout_bufsize != 0))) {
		return (BE_ERR_INVAL);
}

	/* Set up command so popen returns stderr, not stdout */
	if (snprintf(cmdline, BUFSIZ, "%s 2> %s", command,
	    temp_filename) >= BUFSIZ) {
		rval = BE_ERR_NOMEM;
		goto cleanup;
	}

	/* Set up the fifo that will make stderr available. */
	if (mkfifo(temp_filename, 0600) != 0) {
		(void) be_print_err(gettext("be_run_cmd: mkfifo: %s\n"),
		    strerror(errno));
		rval = BE_ERR_EXTCMD;
		goto cleanup;
	}

	if ((stdout_str = popen(cmdline, "r")) == NULL) {
		(void) be_print_err(gettext("be_run_cmd: popen: %s\n"),
		    strerror(errno));
		rval = BE_ERR_EXTCMD;
		goto cleanup;
	}

	if ((stderr_str = fopen(temp_filename, "r")) == NULL) {
		(void) be_print_err(gettext("be_run_cmd: fopen: %s\n"),
		    strerror(errno));
		(void) pclose(stdout_str);
		rval = BE_ERR_EXTCMD;
		goto cleanup;
	}

	/* Read stdout first, as it usually outputs more than stderr. */
	oneline[BUFSIZ-1] = '\0';
	while (fgets(oneline, BUFSIZ-1, stdout_str) != NULL) {
		if (stdout_str != NULL) {
			(void) strlcat(stdout_buf, oneline, stdout_bufsize);
		}
	}

	while (fgets(oneline, BUFSIZ-1, stderr_str) != NULL) {
		(void) strlcat(stderr_buf, oneline, stderr_bufsize);
	}

	/* Close pipe, get exit status. */
	if ((exit_status = pclose(stdout_str)) == -1) {
		(void) be_print_err(gettext("be_run_cmd: pclose: %s\n"),
		    strerror(errno));
		rval = BE_ERR_EXTCMD;
	} else if (WIFEXITED(exit_status)) {
		exit_status = (int)((char)WEXITSTATUS(exit_status));
		if (exit_status != 0) {
			(void) snprintf(oneline, BUFSIZ, gettext("be_run_cmd: "
			    "command terminated with error status: %d\n"),
			    exit_status);
			(void) strlcat(stderr_buf, oneline, stderr_bufsize);
			rval = BE_ERR_EXTCMD;
		}
		if (command_status_p != NULL) {
			*command_status_p = exit_status;
		}
	} else {
		(void) snprintf(oneline, BUFSIZ, gettext("be_run_cmd: command "
		    "terminated on signal: %s\n"),
		    strsignal(WTERMSIG(exit_status)));
		(void) strlcat(stderr_buf, oneline, stderr_bufsize);
		rval = BE_ERR_EXTCMD;
	}

cleanup:
	(void) unlink(temp_filename);
	(void) free(temp_filename);

	return (rval);
}

/*
 * Function:	be_check_rozr
 * Description:	Check to see if we are inside a ROZR zone by checking
 *		to see if /var/pkg is writable.  If it is not, then
 *		we are in a read-only zone.
 * Parameters:
 *		None
 * Returns:
 *		BE_SUCCESS - Not inside a ROZR zone
 *		be_errno_t - inside a ROZR zone
 * Scope:
 *		Semi-private (library wide use only)
 */
int
be_check_rozr(void)
{
	char	*dir = "/var/pkg";
	int	err;
	int	ret = BE_SUCCESS;

	errno = 0;

	if (access(dir, W_OK) != 0) {
		err = errno;
		if (err == EROFS)
			ret = (BE_ERR_ZONE_RO_NOTSUP);
	}

	return (ret);
}

/* ********************************************************************	*/
/*			Private Functions				*/
/* ******************************************************************** */

/*
 * Function:	update_dataset
 * Description:	This function takes a dataset name and replaces the zpool
 *		and be_name components of the dataset with the new be_name
 *		zpool passed in.
 * Parameters:
 *		dataset - name of dataset
 *		dataset_len - lenth of buffer in which dataset is passed in.
 *		be_name - name of new BE name to update to.
 *		old_rc_loc - dataset under which the root container dataset
 *			for the old BE lives.
 *		new_rc_loc - dataset under which the root container dataset
 *			for the new BE lives.
 * Returns:
 *		BE_SUCCESS - Success
 *		be_errno_t - Failure
 * Scope:
 *		Private
 */
static int
update_dataset(char *dataset, int dataset_len, char *be_name,
    char *old_rc_loc, char *new_rc_loc)
{
	char	*ds = NULL;
	char	*sub_ds = NULL;

	/* Tear off the BE container dataset */
	if ((ds = be_make_name_from_ds(dataset, old_rc_loc)) == NULL) {
		return (BE_ERR_INVAL);
	}

	/* Get dataset name relative to BE root, if there is one */
	sub_ds = strchr(ds, '/');

	/* Generate the BE root dataset name */
	be_make_root_ds(new_rc_loc, be_name, dataset, dataset_len);

	/* If a subordinate dataset name was found, append it */
	if (sub_ds != NULL)
		(void) strlcat(dataset, sub_ds, dataset_len);

	free(ds);
	return (BE_SUCCESS);
}

/*
 * Function:	_update_vfstab
 * Description:	This function updates a vfstab file to reflect the new
 *		root container dataset location and be_name for all
 *		entries listed in the be_fs_list_data_t structure passed in.
 * Parameters:
 *		vfstab - vfstab file to modify
 *		be_name - name of BE to update.
 *		old_rc_loc - dataset under which the root container dataset
 *			of the old BE resides in.
 *		new_rc_loc - dataset under which the root container dataset
 *			of the new BE resides in.
 *		fld - be_fs_list_data_t pointer providing the list of
 *			file systems to look for in vfstab.
 * Returns:
 *		BE_SUCCESS - Success
 *		be_errno_t - Failure
 * Scope:
 *		Private
 */
static int
_update_vfstab(char *vfstab, char *be_name, char *old_rc_loc,
    char *new_rc_loc, be_fs_list_data_t *fld)
{
	struct vfstab	vp;
	char		*tmp_vfstab = NULL;
	char		comments_buf[BUFSIZ];
	FILE		*comments = NULL;
	FILE		*vfs_ents = NULL;
	FILE		*tfile = NULL;
	struct stat	sb;
	char		dev[MAXPATHLEN];
	char		*c;
	int		fd;
	int		ret = BE_SUCCESS, err = 0;
	int		i;
	int		tmp_vfstab_len = 0;

	errno = 0;

	/*
	 * Open vfstab for reading twice.  First is for comments,
	 * second is for actual entries.
	 */
	if ((comments = fopen(vfstab, "r")) == NULL ||
	    (vfs_ents = fopen(vfstab, "r")) == NULL) {
		err = errno;
		be_print_err(gettext("_update_vfstab: "
		    "failed to open vfstab (%s): %s\n"), vfstab,
		    strerror(err));
		ret = errno_to_be_err(err);
		goto cleanup;
	}

	/* Grab the stats of the original vfstab file */
	if (stat(vfstab, &sb) != 0) {
		err = errno;
		be_print_err(gettext("_update_vfstab: "
		    "failed to stat file %s: %s\n"), vfstab,
		    strerror(err));
		ret = errno_to_be_err(err);
		goto cleanup;
	}

	/* Create tmp file for modified vfstab */
	if ((tmp_vfstab = (char *)malloc(strlen(vfstab) + 7))
	    == NULL) {
		be_print_err(gettext("_update_vfstab: "
		    "malloc failed\n"));
		ret = BE_ERR_NOMEM;
		goto cleanup;
	}
	tmp_vfstab_len = strlen(vfstab) + 7;
	(void) memset(tmp_vfstab, 0, tmp_vfstab_len);
	(void) strlcpy(tmp_vfstab, vfstab, tmp_vfstab_len);
	(void) strlcat(tmp_vfstab, "XXXXXX", tmp_vfstab_len);
	if ((fd = mkstemp(tmp_vfstab)) == -1) {
		err = errno;
		be_print_err(gettext("_update_vfstab: "
		    "mkstemp failed: %s\n"), strerror(err));
		ret = errno_to_be_err(err);
		goto cleanup;
	}
	if ((tfile = fdopen(fd, "w")) == NULL) {
		err = errno;
		be_print_err(gettext("_update_vfstab: "
		    "could not open file for write\n"));
		(void) close(fd);
		ret = errno_to_be_err(err);
		goto cleanup;
	}

	while (fgets(comments_buf, BUFSIZ, comments)) {
		for (c = comments_buf; *c != '\0' && isspace(*c); c++)
			;
		if (*c == '\0') {
			continue;
		} else if (*c == '#') {
			/*
			 * If line is a comment line, just put
			 * it through to the tmp vfstab.
			 */
			(void) fputs(comments_buf, tfile);
		} else {
			/*
			 * Else line is a vfstab entry, grab it
			 * into a vfstab struct.
			 */
			if (getvfsent(vfs_ents, &vp) != 0) {
				err = errno;
				be_print_err(gettext("_update_vfstab: "
				    "getvfsent failed: %s\n"), strerror(err));
				ret = errno_to_be_err(err);
				goto cleanup;
			}

			if (vp.vfs_special == NULL || vp.vfs_mountp == NULL) {
				(void) putvfsent(tfile, &vp);
				continue;
			}

			/*
			 * If the entry is one of the entries in the list
			 * of file systems to update, modify it's device
			 * field to be correct for this BE.
			 */
			for (i = 0; i < fld->fs_num; i++) {
				if (strcmp(vp.vfs_special, fld->fs_list[i])
				    == 0) {
					/*
					 * Found entry that needs an update.
					 * Replace the root container dataset
					 * location and be_name in the
					 * entry's device.
					 */
					(void) strlcpy(dev, vp.vfs_special,
					    sizeof (dev));

					if ((ret = update_dataset(dev,
					    sizeof (dev), be_name, old_rc_loc,
					    new_rc_loc)) != 0) {
						be_print_err(
						    gettext("_update_vfstab: "
						    "Failed to update device "
						    "field for vfstab entry "
						    "%s\n"), fld->fs_list[i]);
						goto cleanup;
					}

					vp.vfs_special = dev;
					break;
				}
			}

			/* Put entry through to tmp vfstab */
			(void) putvfsent(tfile, &vp);
		}
	}

	(void) fclose(comments);
	comments = NULL;
	(void) fclose(vfs_ents);
	vfs_ents = NULL;
	(void) fclose(tfile);
	tfile = NULL;

	/* Copy tmp vfstab into place */
	if (rename(tmp_vfstab, vfstab) != 0) {
		err = errno;
		be_print_err(gettext("_update_vfstab: "
		    "failed to rename file %s to %s: %s\n"), tmp_vfstab,
		    vfstab, strerror(err));
		ret = errno_to_be_err(err);
		goto cleanup;
	}

	/* Set the perms and ownership of the updated file */
	if (chmod(vfstab, sb.st_mode) != 0) {
		err = errno;
		be_print_err(gettext("_update_vfstab: "
		    "failed to chmod %s: %s\n"), vfstab, strerror(err));
		ret = errno_to_be_err(err);
		goto cleanup;
	}
	if (chown(vfstab, sb.st_uid, sb.st_gid) != 0) {
		err = errno;
		be_print_err(gettext("_update_vfstab: "
		    "failed to chown %s: %s\n"), vfstab, strerror(err));
		ret = errno_to_be_err(err);
		goto cleanup;
	}

cleanup:
	if (comments != NULL)
		(void) fclose(comments);
	if (vfs_ents != NULL)
		(void) fclose(vfs_ents);
	(void) unlink(tmp_vfstab);
	(void) free(tmp_vfstab);
	if (tfile != NULL)
		(void) fclose(tfile);

	return (ret);
}


/*
 * Function:	be_get_auto_name
 * Description:	Generate an auto name constructed based on the BE name
 *		of the original BE or zone BE being cloned.
 * Parameters:
 *		obe_name - name of the original BE or zone BE being cloned.
 *              container_ds - container dataset for the zone.
 *                             Note: if zone_be is false this should be
 *                                  NULL.
 *		zone_be - flag that indicates if we are operating on a zone BE.
 * Returns:
 *		Success - pointer to auto generated BE name.  The name
 *			is allocated in heap storage so the caller is
 *			responsible for free'ing the name.
 *		Failure - NULL
 * Scope:
 *		Private
 */
static char *
be_get_auto_name(char *obe_name, char *be_container_ds, boolean_t zone_be)
{
	be_node_list_t	*be_nodes = NULL;
	be_node_list_t	*cur_be = NULL;
	char		auto_be_name[MAXPATHLEN];
	char		base_be_name[MAXPATHLEN];
	char		cur_be_name[MAXPATHLEN];
	char		*num_str = NULL;
	char		*c = NULL;
	int		num = 0;
	int		cur_num = 0;

	errno = 0;

	/*
	 * Check if obe_name is already in an auto BE name format.
	 * If it is, then strip off the increment number to get the
	 * base name.
	 */
	(void) strlcpy(base_be_name, obe_name, sizeof (base_be_name));

	if ((num_str = strrchr(base_be_name, BE_AUTO_NAME_DELIM))
	    != NULL) {
		/* Make sure remaining string is all digits */
		c = num_str + 1;
		while (c[0] != '\0' && isdigit(c[0]))
			c++;
		/*
		 * If we're now at the end of the string strip off the
		 * increment number.
		 */
		if (c[0] == '\0')
			num_str[0] = '\0';
	}

	if (zone_be) {
		if (be_container_ds == NULL)
			return (NULL);
		if (be_get_zone_be_list(obe_name, be_container_ds,
		    &be_nodes) != BE_SUCCESS) {
			be_print_err(gettext("be_get_auto_name: "
			    "be_get_zone_be_list failed\n"));
			return (NULL);
		}
	} else if (_be_list(NULL, &be_nodes) != BE_SUCCESS) {
		be_print_err(gettext("be_get_auto_name: be_list failed\n"));
		return (NULL);
	}

	for (cur_be = be_nodes; cur_be != NULL; cur_be = cur_be->be_next_node) {
		(void) strlcpy(cur_be_name, cur_be->be_node_name,
		    sizeof (cur_be_name));

		/* If cur_be_name doesn't match at least base be name, skip. */
		if (strncmp(cur_be_name, base_be_name, strlen(base_be_name))
		    != 0)
			continue;

		/* Get the string following the base be name */
		num_str = cur_be_name + strlen(base_be_name);

		/*
		 * If nothing follows the base be name, this cur_be_name
		 * is the BE named with the base be name, skip.
		 */
		if (num_str == NULL || num_str[0] == '\0')
			continue;

		/*
		 * Remove the name delimiter.  If its not there,
		 * cur_be_name isn't part of this BE name stream, skip.
		 */
		if (num_str[0] == BE_AUTO_NAME_DELIM)
			num_str++;
		else
			continue;

		/* Make sure remaining string is all digits */
		c = num_str;
		while (c[0] != '\0' && isdigit(c[0]))
			c++;
		if (c[0] != '\0')
			continue;

		/* Convert the number string to an int */
		cur_num = atoi(num_str);

		/*
		 * If failed to convert the string, skip it.  If its too
		 * long to be converted to an int, we wouldn't auto generate
		 * this number anyway so there couldn't be a conflict.
		 * We treat it as a manually created BE name.
		 */
		if (cur_num == 0 && errno == EINVAL)
			continue;

		/*
		 * Compare current number to current max number,
		 * take higher of the two.
		 */
		if (cur_num > num)
			num = cur_num;
	}

	/*
	 * Store off a copy of 'num' incase we need it later.  If incrementing
	 * 'num' causes it to roll over, this means 'num' is the largest
	 * positive int possible; we'll need it later in the loop to determine
	 * if we've exhausted all possible increment numbers.  We store it in
	 * 'cur_num'.
	 */
	cur_num = num;

	/* Increment 'num' to get new auto BE name number */
	if (++num <= 0) {
		int ret = 0;

		/*
		 * Since incrementing 'num' caused it to rollover, start
		 * over at 0 and find the first available number.
		 */
		for (num = 0; num < cur_num; num++) {

			(void) snprintf(cur_be_name, sizeof (cur_be_name),
			    "%s%c%d", base_be_name, BE_AUTO_NAME_DELIM, num);

			ret = zpool_iter(g_zfs, be_exists_callback,
			    cur_be_name);

			if (ret == 0) {
				/*
				 * BE name doesn't exist, break out
				 * to use 'num'.
				 */
				break;
			} else if (ret == 1) {
				/* BE name exists, continue looking */
				continue;
			} else {
				be_print_err(gettext("be_get_auto_name: "
				    "zpool_iter failed: %s\n"),
				    libzfs_error_description(g_zfs));
				be_free_list(be_nodes);
				return (NULL);
			}
		}

		/*
		 * If 'num' equals 'cur_num', we've exhausted all possible
		 * auto BE names for this base BE name.
		 */
		if (num == cur_num) {
			be_print_err(gettext("be_get_auto_name: "
			    "No more available auto BE names for base "
			    "BE name %s\n"), base_be_name);
			be_free_list(be_nodes);
			return (NULL);
		}
	}

	be_free_list(be_nodes);

	/*
	 * Generate string for auto BE name.
	 */
	(void) snprintf(auto_be_name, sizeof (auto_be_name), "%s%c%d",
	    base_be_name, BE_AUTO_NAME_DELIM, num);

	if ((c = strdup(auto_be_name)) == NULL) {
		be_print_err(gettext("be_get_auto_name: "
		    "memory allocation failed\n"));
		return (NULL);
	}

	return (c);
}

/*
 * Function:	be_zpool_find_rpools_callback
 * Description: Callback function used to iterate through all existing pools
 *		to find root pools
 * Parameters:
 *		zlp - zpool_handle_t pointer to the current pool being
 *			looked at.
 *		data - rpool_data_t pointer.
 *			Upon successfully finding a root pool, the
 *			rpool_count is incremented and the rpool_list is
 *                      appended.
 * Return:
 *		0 - Always
 * Scope:
 *		Semi-private (library wide use only)
 */
static int
be_zpool_find_rpools_callback(zpool_handle_t *zlp, void *data)
{
	rpool_data_t	*rpd = data;
	const char	*zpool =  zpool_get_name(zlp);
	char		be_container_ds[MAXPATHLEN];

	/*
	 * Generate string for BE container dataset
	 */
	be_make_container_ds(zpool, be_container_ds, sizeof (be_container_ds));

	/*
	 * Check if a BE container dataset exists in this pool.
	 */
	if (zfs_dataset_exists(g_zfs, be_container_ds, ZFS_TYPE_FILESYSTEM)) {
		/* Add this rpool to the pool list */
		rpd->pool_count++;
		rpd->pool_list = realloc(rpd->pool_list,
		    rpd->pool_count * sizeof (rpd->pool_list[0]));
		rpd->pool_list[rpd->pool_count - 1] = strdup(zpool);
	}

	zpool_close(zlp);
	return (0);
}

/*
 * Function:	be_find_root_pools
 * Description:
 * 		Returns a list of the root pools.  Only the global zone
 *		is supported.
 * Parameters:
 *		pool_listp - pointer to a char ** in which the list of
 *			     pool name strings is returned.
 *		pool_countp - pointer to an int that holds the number of
 *			      pool name strings in *pool_listp
 * Return:
 *		BE_SUCCESS - On success
 *		BE_ERR_ZONE_NOTSUP - If attempted from a NGZ
 *              BE_ERR_* - On various other failures
 * Scope:
 *		Public
 */
/*
 * Returns a list of the root pools.  Supported only from the
 * global zone.
 */
int
be_find_root_pools(char ***pool_listp, int *pool_countp)
{
	int		zfs_init = 0, i;
	int		ret = BE_SUCCESS, zret;
	rpool_data_t	rpd;

	if (getzoneid() != GLOBAL_ZONEID) {
		/*
		 * There is no support for zones root pools here because this
		 * function is intended for use in locating the boot
		 * configuration which is only present on root pools in the
		 * global zone.
		 */
		return (BE_ERR_ZONE_NOTSUP);
	}

	if (g_zfs == NULL) {
		if (!be_zfs_init())
			return (BE_ERR_INIT);
		zfs_init = 1;
	}

	rpd.pool_list = NULL;
	rpd.pool_count = 0;

	if ((zret = zpool_iter(g_zfs, be_zpool_find_rpools_callback, &rpd))
	    == 0 && rpd.pool_count == 0) {
		be_print_err(gettext("be_find_root_pools: failed to "
		    "find any root pools\n"));
		ret = BE_ERR_NO_RPOOLS;

	} else if (zret == 0 && rpd.pool_count > 0) {
		*pool_listp = rpd.pool_list;
		*pool_countp = rpd.pool_count;
	} else if (zret < 0) {
		if (rpd.pool_count > 0 && rpd.pool_list != NULL) {
			/* Free the partial list of root pools */
			for (i = 0; i < rpd.pool_count; i++)
				free(rpd.pool_list[i]);
			free(rpd.pool_list);
		}
		be_print_err(gettext("be_find_root_pools: "
		    "zpool_iter failed: %s\n"),
		    libzfs_error_description(g_zfs));
		ret = zfs_err_to_be_err(g_zfs);
	}

	if (zfs_init)
		be_zfs_fini();

	return (ret);
}
