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
 * Handle initialization of the suspend subsystem.  This includes setting
 * platform default values for the suspend-enable property and creating
 * the cpr file.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <libgen.h>
#include <sys/cpr.h>
#include <errno.h>
#include <libnvpair.h>
#include <libuutil.h>
#include <libzfs.h>
#include <instzones_api.h>
#include <libpower.h>
#include <libpower_impl.h>
#ifdef	sparc
#include <syslog.h>
#include <sys/openpromio.h>
#endif

#define	DUMP	"/dev/zvol/dsk/rpool/dump"
#define	CPR_LOG_DOMAIN	"libpower-cprfile"

int get_cpr_config_path(char **cpr_conf);

#ifdef	sparc
static int utop(char *fs_name, char *prom_name);
static void strcpy_limit(char *dst, char *src, size_t limit, char *info);
#else

static libzfs_handle_t *g_zfs = NULL;

typedef struct dir_data {
	char *dir;
	char *ds;
} dir_data_t;

#define	ZFS_CLOSE(_zhp) \
	if (_zhp) { \
		zfs_close(_zhp); \
		_zhp = NULL; \
	}

static int be_get_ds_from_dir_callback(zfs_handle_t *zhp, void *data);
static char *be_get_ds_from_dir(char *dir);

#endif

static	uu_dprintf_t	*cpr_log;

/*
 * Initialize the suspend-enable property based on the current machines
 * capabilities and the machine whitelist.
 *
 */
pm_error_t
pm_init_suspend()
{
	pm_error_t	err;
	boolean_t	enabled;
	char		*ep;
	nvlist_t	*proplist;
	nvpair_t	*nvp;

	/*
	 * create the cprconfig file if it does not already exist
	 * silently ignore errors
	 */
	(void) update_cprconfig(UU_DPRINTF_SILENT);

	/*
	 * Retrieve all of the SMF properties to see if suspend-enabled is
	 * already configured in SMF.  If it is, then the service has been
	 * started before and no re-setting of the default is required.
	 */
	errno = 0;
	proplist = NULL;
	err = pm_smf_listprop(&proplist, PM_SVC_POWER);
	if (err != PM_SUCCESS) {
		/*
		 * An error occurred reading from SMF. Pass the error up
		 * to the caller to process.
		 */
		return (err);
	}

	nvp = NULL;
	errno = nvlist_lookup_nvpair(proplist, PM_PROP_SUSPEND_ENABLE, &nvp);
	if (errno == 0 && nvp != NULL) {
		/*
		 * The suspend-enable property has already been set. This
		 * counts as success and the value is not re-initialized to
		 * the default.
		 */
		uu_dprintf(pm_log, UU_DPRINTF_NOTICE,
		    "%s property %s found. Skipping re-initialization\n",
		    __FUNCTION__, nvpair_name(nvp));
		nvlist_free(proplist);
		return (PM_SUCCESS);
	}
	nvlist_free(proplist);

	/*
	 * The suspend-enable property is not yet initialized. Determine the
	 * proper value for this platform and update the service.
	 */
	enabled = pm_get_suspendenable();
	ep = (enabled ? PM_TRUE_STR : PM_FALSE_STR);
	if (nvlist_alloc(&proplist, NV_UNIQUE_NAME, 0) == 0 &&
	    nvlist_add_string(proplist, PM_PROP_SUSPEND_ENABLE, ep) == 0) {
		uu_dprintf(pm_log, UU_DPRINTF_DEBUG,
		    "%s setting %s to %s\n", __FUNCTION__,
		    PM_PROP_SUSPEND_ENABLE, ep);

		/* Update SMF with the value appropriate for this platform */
		err = pm_setprop(proplist);
	}

	/* Clean up and return the result of setting the property */
	if (proplist != NULL) {
		nvlist_free(proplist);
	}

	return (err);
}


int
update_cprconfig(uu_dprintf_severity_t cpr_severity)
{
	struct cprconfig cc;
	char cpr_conf_dir[MAXPATHLEN], cpr_conf_parent[MAXPATHLEN];
	int fd, createdir = 0;
	struct stat statbuf;
	char	*cpr_conf = NULL;

	cpr_log = uu_dprintf_create(CPR_LOG_DOMAIN, cpr_severity, 0);
	/*
	 * A note on the use of dirname and the strings we use to get it.
	 * dirname(3c) usually manipulates the string that is passed in,
	 * such that the original string is not usable unless it was
	 * dup'd or copied before.  The exception, is if a NULL is passed,
	 * and then we get get a new string that is ".".  So we must
	 * always check the return value, and if we care about the original
	 * string, we must dup it before calling dirname.
	 */

	if (cpr_conf == NULL && get_cpr_config_path(&cpr_conf) != 0) {
		uu_dprintf(cpr_log, UU_DPRINTF_FATAL,
		    "%s cannot find cpr_file\n", __FUNCTION__);
		uu_dprintf_destroy(cpr_log);
		return (1);
	}

	if (strlcpy(cpr_conf_dir, cpr_conf, MAXPATHLEN) >= MAXPATHLEN) {
		uu_dprintf(cpr_log, UU_DPRINTF_FATAL,
		    "%s buffer overflow error\n", __FUNCTION__);
		uu_dprintf_destroy(cpr_log);
		return (1);
	}

	if ((strcmp(dirname(cpr_conf_dir), ".") == 0) ||
	    (cpr_conf_dir[0] != '/')) {
		uu_dprintf(cpr_log, UU_DPRINTF_FATAL,
		    "%s cpr_config directory unknown or invalid\n",
		    __FUNCTION__);
		uu_dprintf_destroy(cpr_log);
		return (1);
	}

	/*
	 * If the confdir doesn't exist, it needs to be created,
	 * unless the parent doesn't exist, and then we bail.
	 */
	if (stat(cpr_conf_dir, &statbuf) == -1) {
		/*
		 * cpr_config directory doesn't exist.  If the parent does,
		 * set it up so that cpr_conf_dir can be created a little
		 * further down.  Otherwise, return an error.
		 */
		if (strlcpy(cpr_conf_parent, cpr_conf_dir, MAXPATHLEN) >=
		    MAXPATHLEN) {
			uu_dprintf(cpr_log, UU_DPRINTF_FATAL,
			    "%s buffer overflow error\n", __FUNCTION__);
			uu_dprintf_destroy(cpr_log);
			return (1);
		}
		/*
		 * We already know that cpr_conf_dir properly started
		 * with '/', so the copy should as well, and we don't
		 * need to test for it.  However, if the parent is
		 * just '/', this is an error.
		 */
		if ((strcmp(dirname(cpr_conf_parent), "/") == 0) ||
		    (stat(cpr_conf_parent, &statbuf) == -1)) {
			uu_dprintf(cpr_log, UU_DPRINTF_FATAL,
			    "%s cpr_config parent directory missing\n",
			    __FUNCTION__);
			uu_dprintf_destroy(cpr_log);
			return (1);
		} else {
			createdir = 1;
		}
	}


	/*
	 * Create the cpr_config directory if we have determined it is missing
	 */
	if (createdir) {
		uu_dprintf(cpr_log, UU_DPRINTF_DEBUG, "%s creating: %s\n",
		    __FUNCTION__, cpr_conf_dir);

		if (mkdir(cpr_conf_dir, 0755) == -1) {
			uu_dprintf(cpr_log, UU_DPRINTF_FATAL,
			    "%s conf directory %s missing\n",
			    __FUNCTION__, cpr_conf_dir);
			uu_dprintf_destroy(cpr_log);
			return (1);
		}
	}

	cc.cf_magic = CPR_CONFIG_MAGIC;
	(void) strcpy(cc.cf_path, DUMP);

#ifdef	sparc
	(void) utop(cc.cf_devfs, cc.cf_dev_prom);
#endif

	uu_dprintf(cpr_log, UU_DPRINTF_DEBUG, "%s creating file: %s\n",
	    __FUNCTION__, cpr_conf);

	if ((fd = open(cpr_conf, O_CREAT | O_TRUNC | O_WRONLY, 0644)) == -1) {
		uu_dprintf(cpr_log, UU_DPRINTF_FATAL,
		    "%s cannot open/create \"%s\", %s\\n",
		    __FUNCTION__, cpr_conf, strerror(errno));
		uu_dprintf_destroy(cpr_log);
		return (1);
	} else if (write(fd, &cc, sizeof (cc)) != sizeof (cc)) {
		uu_dprintf(cpr_log, UU_DPRINTF_FATAL,
		    "%s error writing \"%s\", %s\\n",
		    __FUNCTION__, cpr_conf, strerror(errno));
		(void) close(fd);
		uu_dprintf_destroy(cpr_log);
		return (1);
	}

	(void) close(fd);
	uu_dprintf_destroy(cpr_log);
	return (0);
}

/*
 * Find the path to the cpr_config file.
 */
int
get_cpr_config_path(char **cpr_conf)
{
	int	cpsize;
#ifdef sparc
	/* On Sparc, this is easy, as it (currently) must be CPR_CONFIG */
	if (*cpr_conf == NULL) {
		*cpr_conf = strdup(CPR_CONFIG);
		cpsize = strlen(CPR_CONFIG);
	} else {
		cpsize = snprintf(*cpr_conf, MAXPATHLEN, "%s", CPR_CONFIG);
	}
#else
	char    pool[MAXPATHLEN], *fs, *sep;
	size_t  size;

	/* On others, we need to find the pool the root is running. */

	/* Clear and add the leading '/' to the pool pathname. */
	(void) memset(pool, 0, MAXPATHLEN);
	if (*cpr_conf == NULL)
		*cpr_conf = malloc(MAXPATHLEN);
	pool[0] = '/';

	if ((fs = be_get_ds_from_dir("/")) == NULL)
		return (-1);

	if ((sep = strstr(fs, "/ROOT")) != NULL) {
		/*
		 * We have identified the mounted pool, extract everything
		 * up to "/ROOT" from it.
		 */
		size = (size_t)(sep - fs);
		(void) strncpy(pool + 1, fs, size);
	}

	cpsize = snprintf(*cpr_conf, MAXPATHLEN, "%s%s", pool, CPR_CONFIG);
#endif
	if ((cpsize <= 0) || (cpsize > MAXPATHLEN))
		return (-1);
	else
		return (0);
}

#ifdef sparc
/*
 * Convert a Unix device to a prom device and save on success,
 * log any ioctl/conversion error.
 */
static int
utop(char *fs_name, char *prom_name)
{
	union obpbuf {
		char	buf[OBP_MAXPATHLEN + sizeof (uint_t)];
		struct	openpromio oppio;
	};
	union obpbuf oppbuf;
	struct openpromio *opp;
	char *promdev = "/dev/openprom";
	int fd, upval;

	if ((fd = open(promdev, O_RDONLY)) == -1) {
		uu_dprintf(cpr_log, UU_DPRINTF_FATAL,
		    "%s cannot open \"%s\", %s\\n",
		    __FUNCTION__, promdev, strerror(errno));
		return (-1);
	}

	opp = &oppbuf.oppio;
	opp->oprom_size = OBP_MAXPATHLEN;
	strcpy_limit(opp->oprom_array, fs_name,
	    OBP_MAXPATHLEN, "statefile device");
	upval = ioctl(fd, OPROMDEV2PROMNAME, opp);
	(void) close(fd);
	if (upval == 0) {
		strcpy_limit(prom_name, opp->oprom_array, OBP_MAXPATHLEN,
		    "prom device");
	} else {
		openlog("poweradm", 0, LOG_DAEMON);
		syslog(LOG_NOTICE, "cannot convert \"%s\" to prom device",
		    fs_name);
		closelog();
	}

	return (upval);
}

static void
strcpy_limit(char *dst, char *src, size_t limit, char *info)
{
	if (strlcpy(dst, src, limit) >= limit)
		uu_dprintf(cpr_log, UU_DPRINTF_FATAL,
		    "%s: %s is too long  (%s)\n",
		    __FUNCTION__, info, src);
}

#else

/*
 * Function:	be_get_ds_from_dir_callback
 * Description:	This is a callback function used to iterate all datasets
 *		to find the one that is currently mounted at the directory
 *		being searched for.  If matched, the name of the dataset is
 *		returned in heap storage, so the caller is responsible for
 *		freeing it.
 * Parameters:
 *		zhp - zfs_handle_t pointer to current dataset being processed.
 *		data - dir_data_t pointer providing name of directory being
 *			searched for.
 * Returns:
 *		1 - This dataset is mounted at directory being searched for.
 *		0 - This dataset is not mounted at directory being searched for.
 * Scope:
 *		Private
 */
static int
be_get_ds_from_dir_callback(zfs_handle_t *zhp, void *data)
{
	dir_data_t	*dd = data;
	char		*mp = NULL;
	int		zret = 0;

	if (zfs_get_type(zhp) != ZFS_TYPE_FILESYSTEM) {
		ZFS_CLOSE(zhp);
		return (0);
	}

	if (zfs_is_mounted(zhp, &mp) && mp != NULL &&
	    strcmp(mp, dd->dir) == 0) {
		if ((dd->ds = strdup(zfs_get_name(zhp))) == NULL) {
			uu_dprintf(cpr_log, UU_DPRINTF_FATAL,
			    "%s memory allocation failed\n", __FUNCTION__);
			ZFS_CLOSE(zhp);
			return (0);
		}
		ZFS_CLOSE(zhp);
		return (1);
	}

	zret = zfs_iter_filesystems(zhp, be_get_ds_from_dir_callback, dd);

	ZFS_CLOSE(zhp);

	return (zret);
}

/*
 * Function:    be_get_ds_from_dir(char *dir)
 * Description: Given a directory path, find the underlying dataset mounted
 *              at that directory path if there is one.   The returned name
 *              is allocated in heap storage, so the caller is responsible
 *              for freeing it.
 * Parameters:
 *              dir - char pointer of directory to find.
 * Returns:
 *              NULL - if directory is not mounted from a dataset.
 *              name of dataset mounted at dir.
 * Scope:
 *              Semi-private (library wide use only)
 *
 * This is what I need to find the root pool from a mounted root dir.
 */
static char *
be_get_ds_from_dir(char *dir)
{
	dir_data_t	dd = { 0 };
	char		resolved_dir[MAXPATHLEN];

	if (g_zfs == NULL && (g_zfs = libzfs_init()) == NULL) {
		return (NULL);
	}

	/* Make sure length of dir is within the max length */
	if (dir == NULL || strlen(dir) >= MAXPATHLEN)
		return (NULL);

	/* Resolve dir in case its lofs mounted */
	(void) strlcpy(resolved_dir, dir, sizeof (resolved_dir));
	z_resolve_lofs(resolved_dir, sizeof (resolved_dir));

	dd.dir = resolved_dir;

	(void) zfs_iter_root(g_zfs, be_get_ds_from_dir_callback, &dd);

	return (dd.ds);
}
#endif
