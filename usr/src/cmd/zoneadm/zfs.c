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
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This file contains the functions used to support the ZFS integration
 * with zones.  This includes validation (e.g. zonecfg dataset), cloning,
 * file system creation and destruction.
 */

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <locale.h>
#include <libintl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <libgen.h>
#include <libzonecfg.h>
#include <sys/mnttab.h>
#include <libzfs.h>
#include <sys/mntent.h>
#include <values.h>
#include <strings.h>
#include <assert.h>
#include <stddef.h>
#include <dirent.h>
#include <signal.h>
#include <siginfo.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "zoneadm.h"

/* Dataset that will contain boot environments.  Relative to zonepath dataset */
#define	ZONE_BE_CONTAINER_DS	"rpool/ROOT"

libzfs_handle_t *g_zfs;

typedef struct zfs_snapshot_data {
	char	*match_name;	/* zonename@SUNWzone */
	int	len;		/* strlen of match_name */
	int	max;		/* highest digit appended to snap name */
	int	num;		/* number of snapshots to rename */
	int	cntr;		/* counter for renaming snapshots */
} zfs_snapshot_data_t;

typedef struct clone_data {
	zfs_handle_t	*clone_zhp;	/* clone dataset to promote */
	time_t		origin_creation; /* snapshot creation time of clone */
	const char	*snapshot;	/* snapshot of dataset being demoted */
} clone_data_t;

/*
 * Callback data used when verifying zonepath dataset layout
 */
typedef struct verify_zpds_cb_data {
	int errs;		/* Number of errors encountered */
	const char *zpds;	/* zonepath dataset name */
	const char *cmdname;	/* zoneadm subcommand */
} verify_zpds_cb_data_t;

/*
 * Get ZFS handle for the specified mount point.
 */
static zfs_handle_t *
mount2zhandle(char *mountpoint)
{
	zfs_handle_t *zhp;
	char mpprop[MAXPATHLEN];

	if ((zhp = zfs_path_to_zhandle(g_zfs, mountpoint,
	    ZFS_TYPE_FILESYSTEM)) == NULL)
		return (NULL);

	/* Be sure it is mounted */
	if (zfs_prop_get_int(zhp, ZFS_PROP_MOUNTED) == 0) {
		zfs_close(zhp);
		return (NULL);
	}

	/*
	 * Be sure its mountpoint property property matches the one requested.
	 * Note that when mounted, the mountpoint property always matches what
	 * is found in /etc/mnttab.
	 */
	if (zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, mpprop, sizeof (mpprop),
	    NULL, NULL, 0, B_FALSE) != 0) {
		zfs_close(zhp);
		return (NULL);
	}
	if (strcmp(mountpoint, mpprop) == 0)
		return (zhp);

	/* The mountpoint argument is not actually a mountpoint. */
	zfs_close(zhp);
	return (NULL);
}

/*
 * Check if there is already a file system mounted on the path.  If fstype is
 * not NULL, the mountpoint and the file system type must match for B_TRUE to
 * be returned.  If an overlay mount is detected an error message is printed
 * and B_FALSE is returned.
 */
boolean_t
is_mountpnt(char *path, char *fstype)
{
	FILE		*fp;
	struct mnttab	entry;
	int		mp_found = 0;	/* number of times mountpoint seen */
	boolean_t	ret = B_FALSE;	/* return value */

	if ((fp = fopen(MNTTAB, "r")) == NULL)
		return (B_FALSE);

	while (getmntent(fp, &entry) == 0) {
		if (strcmp(path, entry.mnt_mountp) == 0) {
			mp_found++;
			if ((fstype == NULL) ||
			    (strcmp(fstype, entry.mnt_fstype) == 0)) {
				ret = B_TRUE;
			}
		}
	}

	(void) fclose(fp);

	if (mp_found > 1) {
		zerror(gettext("Overlay mount detected at %s"), path);
		return (B_FALSE);
	}
	return (ret);
}

/*
 * Run the brand's pre-snapshot hook before we take a ZFS snapshot of the zone.
 */
static int
pre_snapshot(char *presnapbuf)
{
	int status;

	/* No brand-specific handler */
	if (presnapbuf[0] == '\0')
		return (Z_OK);

	/* Run the hook */
	status = do_subproc(presnapbuf);
	if ((status = subproc_status(gettext("brand-specific presnapshot"),
	    status, B_FALSE)) != ZONE_SUBPROC_OK)
		return (Z_ERR);

	return (Z_OK);
}

/*
 * Run the brand's post-snapshot hook after we take a ZFS snapshot of the zone.
 */
static int
post_snapshot(char *postsnapbuf)
{
	int status;

	/* No brand-specific handler */
	if (postsnapbuf[0] == '\0')
		return (Z_OK);

	/* Run the hook */
	status = do_subproc(postsnapbuf);
	if ((status = subproc_status(gettext("brand-specific postsnapshot"),
	    status, B_FALSE)) != ZONE_SUBPROC_OK)
		return (Z_ERR);

	return (Z_OK);
}

/*
 * This is a ZFS snapshot iterator call-back function which returns the
 * highest number of SUNWzone snapshots that have been taken.
 */
static int
get_snap_max(zfs_handle_t *zhp, void *data)
{
	int			res;
	zfs_snapshot_data_t	*cbp;

	if (zfs_get_type(zhp) != ZFS_TYPE_SNAPSHOT) {
		zfs_close(zhp);
		return (0);
	}

	cbp = (zfs_snapshot_data_t *)data;

	if (strncmp(zfs_get_name(zhp), cbp->match_name, cbp->len) == 0) {
		char	*nump;
		int	num;

		cbp->num++;
		nump = (char *)(zfs_get_name(zhp) + cbp->len);
		num = atoi(nump);
		if (num > cbp->max)
			cbp->max = num;
	}

	res = zfs_iter_snapshots(zhp, get_snap_max, data);
	zfs_close(zhp);
	return (res);
}

/*
 * Take a ZFS snapshot to be used for cloning the zone.
 */
static int
take_snapshot(zfs_handle_t *zhp, char *snapshot_name, int snap_size,
    char *presnapbuf, char *postsnapbuf)
{
	int			res;
	char			template[ZFS_MAXNAMELEN];
	zfs_snapshot_data_t	cb;

	/*
	 * First we need to figure out the next available name for the
	 * zone snapshot.  Look through the list of zones snapshots for
	 * this file system to determine the maximum snapshot name.
	 */
	if (snprintf(template, sizeof (template), "%s@SUNWzone",
	    zfs_get_name(zhp)) >=  sizeof (template))
		return (Z_ERR);

	cb.match_name = template;
	cb.len = strlen(template);
	cb.max = 0;

	if (zfs_iter_snapshots(zhp, get_snap_max, &cb) != 0)
		return (Z_ERR);

	cb.max++;

	if (snprintf(snapshot_name, snap_size, "%s@SUNWzone%d",
	    zfs_get_name(zhp), cb.max) >= snap_size)
		return (Z_ERR);

	if (pre_snapshot(presnapbuf) != Z_OK)
		return (Z_ERR);
	res = zfs_snapshot(g_zfs, snapshot_name, B_FALSE, NULL);
	if (post_snapshot(postsnapbuf) != Z_OK)
		return (Z_ERR);

	if (res != 0)
		return (Z_ERR);
	return (Z_OK);
}

/*
 * We are using an explicit snapshot from some earlier point in time so
 * we need to validate it.  Run the brand specific hook.
 */
static int
validate_snapshot(char *snapshot_name, char *snap_path, char *validsnapbuf)
{
	int status;
	char cmdbuf[MAXPATHLEN];

	/* No brand-specific handler */
	if (validsnapbuf[0] == '\0')
		return (Z_OK);

	/* pass args - snapshot_name & snap_path */
	if (snprintf(cmdbuf, sizeof (cmdbuf), "%s %s %s", validsnapbuf,
	    snapshot_name, snap_path) >= sizeof (cmdbuf)) {
		zerror("Command line too long");
		return (Z_ERR);
	}

	/* Run the hook */
	status = do_subproc(cmdbuf);
	if ((status = subproc_status(gettext("brand-specific validatesnapshot"),
	    status, B_FALSE)) != ZONE_SUBPROC_OK)
		return (Z_ERR);

	return (Z_OK);
}

/*
 * Remove the sw inventory file from inside this zonepath that we picked up out
 * of the snapshot.
 */
static int
clean_out_clone()
{
	int err;
	zone_dochandle_t handle;

	if ((handle = zonecfg_init_handle()) == NULL) {
		zperror(cmd_to_str(CMD_CLONE), B_TRUE);
		return (Z_ERR);
	}

	if ((err = zonecfg_get_handle(target_zone, handle)) != Z_OK) {
		errno = err;
		zperror(cmd_to_str(CMD_CLONE), B_TRUE);
		zonecfg_fini_handle(handle);
		return (Z_ERR);
	}

	zonecfg_rm_detached(handle, B_FALSE);
	zonecfg_fini_handle(handle);

	return (Z_OK);
}

/*
 * Make a ZFS clone on zonepath from snapshot_name.
 */
static int
clone_snap(char *snapshot_name, char *zonepath)
{
	int		res = Z_OK;
	int		err;
	zfs_handle_t	*zhp;
	zfs_handle_t	*clone;
	nvlist_t	*props = NULL;

	if ((zhp = zfs_open(g_zfs, snapshot_name, ZFS_TYPE_SNAPSHOT)) == NULL)
		return (Z_NO_ENTRY);

	(void) printf(gettext("Cloning snapshot %s\n"), snapshot_name);

	/*
	 * We turn off zfs SHARENFS and SHARESMB properties on the
	 * zoneroot dataset in order to prevent the GZ from sharing
	 * NGZ data by accident.
	 */
	if ((nvlist_alloc(&props, NV_UNIQUE_NAME, 0) != 0) ||
	    (nvlist_add_string(props, zfs_prop_to_name(ZFS_PROP_SHARENFS),
	    "off") != 0) ||
	    (nvlist_add_string(props, zfs_prop_to_name(ZFS_PROP_SHARESMB),
	    "off") != 0)) {
		if (props != NULL)
			nvlist_free(props);
		(void) fprintf(stderr, gettext("could not create ZFS clone "
		    "%s: out of memory\n"), zonepath);
		return (Z_ERR);
	}

	err = zfs_clone(zhp, zonepath, props);
	zfs_close(zhp);

	nvlist_free(props);

	if (err != 0)
		return (Z_ERR);

	/* create the mountpoint if necessary */
	if ((clone = zfs_open(g_zfs, zonepath, ZFS_TYPE_DATASET)) == NULL)
		return (Z_ERR);

	/*
	 * The clone has been created so we need to print a diagnostic
	 * message if one of the following steps fails for some reason.
	 */
	if (zfs_mount(clone, NULL, 0) != 0) {
		(void) fprintf(stderr, gettext("could not mount ZFS clone "
		    "%s\n"), zfs_get_name(clone));
		res = Z_ERR;

	} else if (clean_out_clone() != Z_OK) {
		(void) fprintf(stderr, gettext("could not remove the "
		    "software inventory from ZFS clone %s\n"),
		    zfs_get_name(clone));
		res = Z_ERR;
	}

	zfs_close(clone);
	return (res);
}

/*
 * This function takes a zonepath and attempts to determine what the ZFS
 * file system name (not mountpoint) should be for that path.  We do not
 * assume that zonepath is an existing directory or ZFS fs since we use
 * this function as part of the process of creating a new ZFS fs or clone.
 *
 * The way this works is that we look at the parent directory of the zonepath
 * to see if it is a ZFS fs.  If it is, we get the name of that ZFS fs and
 * append the last component of the zonepath to generate the ZFS name for the
 * zonepath.  This matches the algorithm that ZFS uses for automatically
 * mounting a new fs after it is created.
 *
 * Although a ZFS fs can be mounted anywhere, we don't worry about handling
 * all of the complexity that a user could possibly configure with arbitrary
 * mounts since there is no way to generate a ZFS name from a random path in
 * the file system.  We only try to handle the automatic mounts that ZFS does
 * for each file system.  ZFS restricts this so that a new fs must be created
 * in an existing parent ZFS fs.  It then automatically mounts the new fs
 * directly under the mountpoint for the parent fs using the last component
 * of the name as the mountpoint directory.
 *
 * For example:
 *    Name			Mountpoint
 *    space/eng/dev/test/zone1	/project1/eng/dev/test/zone1
 *
 * Note, however that space/eng/dev/test/zone1 may already exist with a
 * mountpoint property that points elsewhere.
 *
 * Return Z_OK if the path mapped to a ZFS file system name, otherwise return
 * Z_ERR.
 */
static int
path2name(char *zonepath, char *zfs_name, int len)
{
	int		res;
	char		*bnm, *dnm, *dname, *bname;
	zfs_handle_t	*zhp;
	struct stat	stbuf;
	char		mp[MAXPATHLEN];

	/*
	 * If the exact path already exists as the mountpoint of a zfs dataset,
	 * use it.
	 */
	if (stat(zonepath, &stbuf) == 0 &&
	    (zhp = zfs_path_to_zhandle(g_zfs, zonepath, ZFS_TYPE_FILESYSTEM))
	    != NULL) {
		if ((zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, mp, sizeof (mp),
		    NULL, NULL, 0, B_FALSE) == 0) &&
		    (strcmp(mp, zonepath) == 0)) {
			if (strlcpy(zfs_name, zfs_get_name(zhp), len) >= len) {
				zerror(gettext("dataset %s too long for "
				    "buffer"), zfs_get_name(zhp));
				zfs_close(zhp);
				return (Z_ERR);
			}
			zfs_close(zhp);
			return (Z_OK);
		}
		zfs_close(zhp);
	}

	/*
	 * We need two tmp strings to handle paths directly in / (e.g. /foo)
	 * since dirname will overwrite the first char after "/" in this case.
	 */
	if ((bnm = strdup(zonepath)) == NULL)
		return (Z_ERR);

	if ((dnm = strdup(zonepath)) == NULL) {
		free(bnm);
		return (Z_ERR);
	}

	bname = basename(bnm);
	dname = dirname(dnm);

	/*
	 * This is a quick test to save iterating over all of the zfs datasets
	 * on the system (which can be a lot).  If the parent dir is not in a
	 * ZFS fs, then we're done.
	 */
	if (stat(dname, &stbuf) != 0 || !S_ISDIR(stbuf.st_mode) ||
	    strcmp(stbuf.st_fstype, MNTTYPE_ZFS) != 0) {
		free(bnm);
		free(dnm);
		return (Z_ERR);
	}

	/* See if the parent directory is its own ZFS dataset. */
	if ((zhp = mount2zhandle(dname)) == NULL) {
		/*
		 * The parent is not a ZFS dataset so we can't automatically
		 * create a dataset on the given path.
		 */
		free(bnm);
		free(dnm);
		return (Z_ERR);
	}

	res = snprintf(zfs_name, len, "%s/%s", zfs_get_name(zhp), bname);

	free(bnm);
	free(dnm);
	zfs_close(zhp);
	if (res >= len)
		return (Z_ERR);

	return (Z_OK);
}

/*
 * A ZFS file system iterator call-back function used to determine if the
 * file system has dependents (snapshots & clones).
 */
/* ARGSUSED */
static int
has_dependent(zfs_handle_t *zhp, void *data)
{
	zfs_close(zhp);
	return (1);
}

/*
 * Given a snapshot name, get the file system path where the snapshot lives.
 * A snapshot name is of the form fs_name@snap_name.  For example, snapshot
 * pl/zones/z1@SUNWzone1 would have a path of
 * /pl/zones/z1/.zfs/snapshot/SUNWzone1.
 */
static int
snap2path(char *snap_name, char *path, int len)
{
	char		*p;
	zfs_handle_t	*zhp;
	char		mp[ZFS_MAXPROPLEN];

	if ((p = strrchr(snap_name, '@')) == NULL)
		return (Z_ERR);

	/* Get the file system name from the snap_name. */
	*p = '\0';
	zhp = zfs_open(g_zfs, snap_name, ZFS_TYPE_DATASET);
	*p = '@';
	if (zhp == NULL)
		return (Z_ERR);

	/* Get the file system mount point. */
	if (zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, mp, sizeof (mp), NULL, NULL,
	    0, B_FALSE) != 0) {
		zfs_close(zhp);
		return (Z_ERR);
	}
	zfs_close(zhp);

	p++;
	if (snprintf(path, len, "%s/.zfs/snapshot/%s", mp, p) >= len)
		return (Z_ERR);

	return (Z_OK);
}

/*
 * This callback function is used to iterate through a snapshot's dependencies
 * to find a filesystem that is a direct clone of the snapshot being iterated.
 */
static int
get_direct_clone(zfs_handle_t *zhp, void *data)
{
	clone_data_t	*cd = data;
	char		origin[ZFS_MAXNAMELEN];
	char		ds_path[ZFS_MAXNAMELEN];

	if (zfs_get_type(zhp) != ZFS_TYPE_FILESYSTEM) {
		zfs_close(zhp);
		return (0);
	}

	(void) strlcpy(ds_path, zfs_get_name(zhp), sizeof (ds_path));

	/* Make sure this is a direct clone of the snapshot we're iterating. */
	if (zfs_prop_get(zhp, ZFS_PROP_ORIGIN, origin, sizeof (origin), NULL,
	    NULL, 0, B_FALSE) != 0 || strcmp(origin, cd->snapshot) != 0) {
		zfs_close(zhp);
		return (0);
	}

	if (cd->clone_zhp != NULL)
		zfs_close(cd->clone_zhp);

	cd->clone_zhp = zhp;
	return (1);
}

/*
 * A ZFS file system iterator call-back function used to determine the clone
 * to promote.  This function finds the youngest (i.e. last one taken) snapshot
 * that has a clone.  If found, it returns a reference to that clone in the
 * callback data.
 */
static int
find_clone(zfs_handle_t *zhp, void *data)
{
	clone_data_t	*cd = data;
	time_t		snap_creation;
	int		zret = 0;

	/* If snapshot has no clones, skip it */
	if (zfs_prop_get_int(zhp, ZFS_PROP_NUMCLONES) == 0) {
		zfs_close(zhp);
		return (0);
	}

	cd->snapshot = zfs_get_name(zhp);

	/* Get the creation time of this snapshot */
	snap_creation = (time_t)zfs_prop_get_int(zhp, ZFS_PROP_CREATION);

	/*
	 * If this snapshot's creation time is greater than (i.e. younger than)
	 * the current youngest snapshot found, iterate this snapshot to
	 * get the right clone.
	 */
	if (snap_creation >= cd->origin_creation) {
		/*
		 * Iterate the dependents of this snapshot to find a clone
		 * that's a direct dependent.
		 */
		if ((zret = zfs_iter_dependents(zhp, B_FALSE, get_direct_clone,
		    cd)) == -1) {
			zfs_close(zhp);
			return (1);
		} else if (zret == 1) {
			/*
			 * Found a clone, update the origin_creation time
			 * in the callback data.
			 */
			cd->origin_creation = snap_creation;
		}
	}

	zfs_close(zhp);
	return (0);
}

/*
 * A ZFS file system iterator call-back function used to remove standalone
 * snapshots.
 */
/* ARGSUSED */
static int
rm_snap(zfs_handle_t *zhp, void *data)
{
	/* If snapshot has clones, something is wrong */
	if (zfs_prop_get_int(zhp, ZFS_PROP_NUMCLONES) != 0) {
		zfs_close(zhp);
		return (1);
	}

	if (zfs_unmount(zhp, NULL, 0) == 0) {
		(void) zfs_destroy(zhp, B_FALSE);
	}

	zfs_close(zhp);
	return (0);
}

/*
 * A ZFS snapshot iterator call-back function which renames snapshots.
 */
static int
rename_snap(zfs_handle_t *zhp, void *data)
{
	int			res;
	zfs_snapshot_data_t	*cbp;
	char			template[ZFS_MAXNAMELEN];

	cbp = (zfs_snapshot_data_t *)data;

	/*
	 * When renaming snapshots with the iterator, the iterator can see
	 * the same snapshot after we've renamed up in the namespace.  To
	 * prevent this we check the count for the number of snapshots we have
	 * to rename and stop at that point.
	 */
	if (cbp->cntr >= cbp->num) {
		zfs_close(zhp);
		return (0);
	}

	if (zfs_get_type(zhp) != ZFS_TYPE_SNAPSHOT) {
		zfs_close(zhp);
		return (0);
	}

	/* Only rename the snapshots we automatically generate when we clone. */
	if (strncmp(zfs_get_name(zhp), cbp->match_name, cbp->len) != 0) {
		zfs_close(zhp);
		return (0);
	}

	(void) snprintf(template, sizeof (template), "%s%d", cbp->match_name,
	    cbp->max++);

	res = (zfs_rename(zhp, template, B_FALSE) != 0);
	if (res != 0)
		(void) fprintf(stderr, gettext("failed to rename snapshot %s "
		    "to %s: %s\n"), zfs_get_name(zhp), template,
		    libzfs_error_description(g_zfs));

	cbp->cntr++;

	zfs_close(zhp);
	return (res);
}

/*
 * Rename the source dataset's snapshots that are automatically generated when
 * we clone a zone so that there won't be a name collision when we promote the
 * cloned dataset.  Once the snapshots have been renamed, then promote the
 * clone.
 *
 * The snapshot rename process gets the highest number on the snapshot names
 * (the format is zonename@SUNWzoneXX where XX are digits) on both the source
 * and clone datasets, then renames the source dataset snapshots starting at
 * the next number.
 */
static int
promote_clone(zfs_handle_t *src_zhp, zfs_handle_t *cln_zhp)
{
	zfs_snapshot_data_t	sd;
	char			nm[ZFS_MAXNAMELEN];
	char			template[ZFS_MAXNAMELEN];

	(void) strlcpy(nm, zfs_get_name(cln_zhp), sizeof (nm));
	/*
	 * Start by getting the clone's snapshot max which we use
	 * during the rename of the original dataset's snapshots.
	 */
	(void) snprintf(template, sizeof (template), "%s@SUNWzone", nm);
	sd.match_name = template;
	sd.len = strlen(template);
	sd.max = 0;

	if (zfs_iter_snapshots(cln_zhp, get_snap_max, &sd) != 0)
		return (Z_ERR);

	/*
	 * Now make sure the source's snapshot max is at least as high as
	 * the clone's snapshot max.
	 */
	(void) snprintf(template, sizeof (template), "%s@SUNWzone",
	    zfs_get_name(src_zhp));
	sd.match_name = template;
	sd.len = strlen(template);
	sd.num = 0;

	if (zfs_iter_snapshots(src_zhp, get_snap_max, &sd) != 0)
		return (Z_ERR);

	/*
	 * Now rename the source dataset's snapshots so there's no
	 * conflict when we promote the clone.
	 */
	sd.max++;
	sd.cntr = 0;
	if (zfs_iter_snapshots(src_zhp, rename_snap, &sd) != 0)
		return (Z_ERR);

	/* close and reopen the clone dataset to get the latest info */
	zfs_close(cln_zhp);
	if ((cln_zhp = zfs_open(g_zfs, nm, ZFS_TYPE_FILESYSTEM)) == NULL)
		return (Z_ERR);

	if (zfs_promote(cln_zhp) != 0) {
		(void) fprintf(stderr, gettext("failed to promote %s: %s\n"),
		    nm, libzfs_error_description(g_zfs));
		return (Z_ERR);
	}

	zfs_close(cln_zhp);
	return (Z_OK);
}

/*
 * Promote the youngest clone.  That clone will then become the origin of all
 * of the other clones that were hanging off of the source dataset.
 */
int
promote_all_clones(zfs_handle_t *zhp)
{
	clone_data_t	cd;
	char		nm[ZFS_MAXNAMELEN];

	cd.clone_zhp = NULL;
	cd.origin_creation = 0;
	cd.snapshot = NULL;

	if (zfs_iter_snapshots(zhp, find_clone, &cd) != 0) {
		zfs_close(zhp);
		return (Z_ERR);
	}

	/* Nothing to promote. */
	if (cd.clone_zhp == NULL)
		return (Z_OK);

	/* Found the youngest clone to promote.  Promote it. */
	if (promote_clone(zhp, cd.clone_zhp) != 0) {
		zfs_close(cd.clone_zhp);
		zfs_close(zhp);
		return (Z_ERR);
	}

	/* close and reopen the main dataset to get the latest info */
	(void) strlcpy(nm, zfs_get_name(zhp), sizeof (nm));
	zfs_close(zhp);
	if ((zhp = zfs_open(g_zfs, nm, ZFS_TYPE_FILESYSTEM)) == NULL)
		return (Z_ERR);

	return (Z_OK);
}

/*
 * Clone a pre-existing ZFS snapshot, either by making a direct ZFS clone, if
 * possible, or by copying the data from the snapshot to the zonepath.
 */
int
clone_snapshot_zfs(char *snap_name, char *zonepath, char *validatesnap)
{
	int	err = Z_OK;
	char	clone_name[MAXPATHLEN];
	char	snap_path[MAXPATHLEN];

	if (snap2path(snap_name, snap_path, sizeof (snap_path)) != Z_OK) {
		(void) fprintf(stderr, gettext("unable to find path for %s.\n"),
		    snap_name);
		return (Z_ERR);
	}

	if (validate_snapshot(snap_name, snap_path, validatesnap) != Z_OK)
		return (Z_NO_ENTRY);

	/*
	 * The zonepath cannot be ZFS cloned, try to copy the data from
	 * within the snapshot to the zonepath.
	 */
	if (path2name(zonepath, clone_name, sizeof (clone_name)) != Z_OK) {
		if ((err = clone_copy(snap_path, zonepath)) == Z_OK)
			if (clean_out_clone() != Z_OK)
				(void) fprintf(stderr,
				    gettext("could not remove the "
				    "software inventory from %s\n"), zonepath);

		return (err);
	}

	if ((err = clone_snap(snap_name, clone_name)) != Z_OK) {
		if (err != Z_NO_ENTRY) {
			/*
			 * Cloning the snapshot failed.  Fall back to trying
			 * to install the zone by copying from the snapshot.
			 */
			if ((err = clone_copy(snap_path, zonepath)) == Z_OK)
				if (clean_out_clone() != Z_OK)
					(void) fprintf(stderr,
					    gettext("could not remove the "
					    "software inventory from %s\n"),
					    zonepath);
		} else {
			/*
			 * The snapshot is unusable for some reason so restore
			 * the zone state to configured since we were unable to
			 * actually do anything about getting the zone
			 * installed.
			 */
			int tmp;

			if ((tmp = zone_set_state(target_zone,
			    ZONE_STATE_CONFIGURED)) != Z_OK) {
				errno = tmp;
				zperror2(target_zone,
				    gettext("could not set state"));
			}
		}
	}

	return (err);
}

/*
 * Attempt to clone a source_zone to a target zonepath by using a ZFS clone.
 */
int
clone_zfs(char *source_zonepath, char *zonepath, char *presnapbuf,
    char *postsnapbuf)
{
	zfs_handle_t	*zhp;
	char		clone_name[MAXPATHLEN];
	char		snap_name[MAXPATHLEN];

	/*
	 * Try to get a zfs handle for the source_zonepath.  If this fails
	 * the source_zonepath is not ZFS so return an error.
	 */
	if ((zhp = mount2zhandle(source_zonepath)) == NULL)
		return (Z_ERR);

	/*
	 * Check if there is a file system already mounted on zonepath.  If so,
	 * we can't clone to the path so we should fall back to copying.
	 */
	if (is_mountpnt(zonepath, NULL)) {
		zfs_close(zhp);
		(void) fprintf(stderr,
		    gettext("A file system is already mounted on %s,\n"
		    "preventing use of a ZFS clone.\n"), zonepath);
		return (Z_ERR);
	}

	/*
	 * Instead of using path2name to get the clone name from the zonepath,
	 * we could generate a name from the source zone ZFS name.  However,
	 * this would mean we would create the clone under the ZFS fs of the
	 * source instead of what the zonepath says.  For example,
	 *
	 * source_zonepath		zonepath
	 * /pl/zones/dev/z1		/pl/zones/deploy/z2
	 *
	 * We don't want the clone to be under "dev", we want it under
	 * "deploy", so that we can leverage the normal attribute inheritance
	 * that ZFS provides in the fs hierarchy.
	 */
	if (path2name(zonepath, clone_name, sizeof (clone_name)) != Z_OK) {
		zfs_close(zhp);
		return (Z_ERR);
	}

	if (take_snapshot(zhp, snap_name, sizeof (snap_name), presnapbuf,
	    postsnapbuf) != Z_OK) {
		zfs_close(zhp);
		return (Z_ERR);
	}
	zfs_close(zhp);

	if (clone_snap(snap_name, clone_name) != Z_OK) {
		/* Clean up the snapshot we just took. */
		if ((zhp = zfs_open(g_zfs, snap_name, ZFS_TYPE_SNAPSHOT))
		    != NULL) {
			if (zfs_unmount(zhp, NULL, 0) == 0)
				(void) zfs_destroy(zhp, B_FALSE);
			zfs_close(zhp);
		}

		return (Z_ERR);
	}

	(void) printf(gettext("Instead of copying, a ZFS clone has been "
	    "created for this zone.\n"));

	return (Z_OK);
}

static boolean_t
is_dataset_in_gzbe(const char *ds)
{
	zfs_handle_t	*zhp;
	const char	*rootds;
	size_t		len;
	boolean_t	ret;

	/*
	 * If "/" can't resolve to a dataset name, anything that is passed in
	 * should be OK.
	 */
	if ((zhp = zfs_path_to_zhandle(g_zfs, "/", ZFS_TYPE_FILESYSTEM)) ==
	    NULL) {
		return (B_FALSE);
	}
	rootds = zfs_get_name(zhp);
	len = strlen(rootds);

	ret = strncmp(rootds, ds, len) == 0 &&
	    (ds[len] == '\0' || ds[len] == '/');
	zfs_close(zhp);

	return (ret);
}

/*
 * Finds the closest ancestor dataset of the specified zonepath.
 *
 * On success, indicated by a return value of 0, anc will contain the name of
 * the first ancestor dataset that needs to be created.  zpds contains the name
 * of the zonepath dataset that needs to be created.  anc and zpds will hold
 * the same value if the parent of the zonepath dataset already exists.
 */
static int
find_closest_ancestor(const char *zonepath, char *anc, size_t anc_len,
    char *zpds, size_t zpds_len)
{
	zfs_handle_t *zhp = NULL;
	char path[MAXNAMELEN];		/* Mutable copy of zonepath */
	char *slash = NULL;
	DIR *dir;
	struct dirent *ent;
	struct stat stbuf;
	int tries;

	if (strlcpy(path, zonepath, sizeof (path)) >= sizeof (path)) {
		zerror(gettext("zonepath is too long"));
		return (Z_ERR);
	}

	/*
	 * Walk up the zonepath until we find an existing directory.  Once
	 * there, verify that it is a dataset or an empty top-level directory
	 * in a dataset.
	 */
	for (tries = 0; tries < 2; tries++) {

		while ((slash = strrchr(path, '/')) != NULL) {
			if (slash == path) {
				zerror(gettext("zonepath must not be in "
				    "global zone root file system."), zonepath);
				goto errout;
			}
			*slash = '\0';

			if (lstat(path, &stbuf) == 0)
				break;
		}
		if (slash == NULL) {
			zerror(gettext("zonepath must be an absolute path."));
			goto errout;
		}

		if (!S_ISDIR(stbuf.st_mode)) {
			zerror(gettext("path %s exists but is not a "
			    "directory."), path);
			goto errout;
		}

		/* The directory exists, but is it on a zfs filesystem? */
		if (strcmp(stbuf.st_fstype, MNTTYPE_ZFS) != 0) {
			zerror(gettext("%s is a %s file system, not %s."),
			    path, stbuf.st_fstype, MNTTYPE_ZFS);
			goto errout;
		}

		if ((zhp = zfs_path_to_zhandle(g_zfs, path,
		    ZFS_TYPE_FILESYSTEM)) == NULL) {
			zerror(gettext("cannot get zfs handle for %s."), path);
			goto errout;
		}

		/* Be sure it is not in the global zone BE */
		if (is_dataset_in_gzbe(zfs_get_name(zhp))) {
			zerror(gettext("zonepath must not be in global zone "
			    "root file system."));
			goto errout;
		}

		/* Is the directory the mountpoint for the file system? */
		if (is_mountpnt(path, MNTTYPE_ZFS)) {
			if (snprintf(anc, anc_len, "%s/%s", zfs_get_name(zhp),
			    slash + 1) >= anc_len) {
				zerror(gettext("ancestor dataset %s/%s too "
				    "long."), zfs_get_name(zhp), slash + 1);
				goto errout;
			}
			if (snprintf(zpds, zpds_len, "%s%s", zfs_get_name(zhp),
			    zonepath + (slash - path)) >= zpds_len) {
				zerror(gettext("zonepath dataset %s/%s too "
				    "long."), zfs_get_name(zhp),
				    zonepath + (slash - path));
				goto errout;
			}
			return (Z_OK);
		}

		/*
		 * It's not a mountpoint, but maybe its an empty directory
		 * where a filesystem could be mounted.
		 */
		if ((dir = opendir(path)) == NULL) {
			zerror(gettext("unable to open directory %s."), path);
			goto errout;
		}
		while ((ent = readdir(dir)) != NULL) {
			if (strcmp(ent->d_name, ".") == 0 ||
			    strcmp(ent->d_name, "..") == 0)
				continue;

			zerror(gettext("directory %s must be empty."), path);
			(void) closedir(dir);
			goto errout;
		}
		(void) closedir(dir);
		/*
		 * It's an empty directory.  Is the parent a mountpoint?  Jump
		 * back up to the top and try again.  Note that we only do one
		 * retry (two tries total) because we know the parent directory
		 * of this directory is not empty.
		 */
		zfs_close(zhp);
		zhp = NULL;
	}

errout:
	if (zhp != NULL)
		zfs_close(zhp);
	return (Z_ERR);
}

/*
 * Creates the topmost dataset of the zfs hierarchy that leads to a zonepath
 * such that it is not in a boot environment, if appropriate.
 *
 * If the topmost directory doesn't exist an attempt will be made to create
 * <root pool>/<topdir> which will be mounted at </topdir>.  For example, if
 * "/zones/z1" is passed in and /zones doesn't exist, an attempt will be made
 * to create rpool/zones and mount it at /zones.  If the creation or mount
 * fails, any dataset created is destroyed and Z_ERR is returned.  If creation
 * and mount succeed, Z_OK is returned.
 *
 * If the topmost directory already exists, Z_OK is returned an retds is not
 * changed.  Further checks are required by the caller to ensure that the
 * directory is suitable for zonepaths.
 *
 * Any time that Z_ERR has been returned, retds has not been modified.
 */
static int
create_root_pool_child(const char *zonepath, char *retds, size_t retlen) {
	char dir[MAXPATHLEN];
	char ds[ZFS_MAXNAMELEN];
	char *slash;
	struct stat stbuf;
	zfs_handle_t	*zhp;
	nvlist_t	*props = NULL;

	if (strlcpy(dir, zonepath, sizeof (dir)) >= sizeof (dir)) {
		zerror(gettext("zonepath is too long"));
		return (Z_ERR);
	}
	if (dir[0] != '/') {
		zerror(gettext("zonepath must be an absolute path"));
		return (Z_ERR);
	}
	/* Truncate path down to subdirectory of / */
	if ((slash = strchr(dir + 1, '/')) != NULL)
		*slash = '\0';

	/*
	 * Nothing done because nothing to do.  If it's not a directory
	 * or is ia directory in the root file system, that will be caught
	 * later.
	 */
	if (stat(dir, &stbuf) == 0)
		return (Z_OK);

	/*
	 * Root file system is not ZFS?  Perhaps things will still work
	 * out, but probably not.  We'll sort that out later.
	 */
	if ((zhp = zfs_path_to_zhandle(g_zfs, "/", ZFS_TYPE_FILESYSTEM)) ==
	    NULL)
		return (Z_OK);

	/* Get the name of the root pool. */
	if (strlcpy(ds, zfs_get_name(zhp), sizeof (ds)) >= sizeof (ds)) {
		zerror(gettext("root pool dataset name too long"));
		zfs_close(zhp);
		return (Z_ERR);
	}
	zfs_close(zhp);

	if ((slash = strchr(ds, '/')) == NULL) {
		zerror(gettext("global zone root file system is a zpool"));
		return (Z_ERR);
	}
	*slash = '\0';

	/* Create the name of the new dataset. */
	if (strlcat(ds, dir, sizeof (ds)) >= sizeof (ds)) {
		zerror(gettext("name of new dataset for mounting at %s too "
		    "long"), dir);
		return (Z_ERR);
	}

	/*
	 * Before creating the dataset, be sure its name will fit in the
	 * result buffer.
	 */
	if (strlen(ds) + 1 > retlen) {
		zerror(gettext("dataset name %s too long for result buffer"),
		    ds);
		return (Z_ERR);
	}

	/* Create the dataset with the mountpoint property set. */
	if ((nvlist_alloc(&props, NV_UNIQUE_NAME, 0) != 0) ||
	    (nvlist_add_string(props, zfs_prop_to_name(ZFS_PROP_MOUNTPOINT),
	    dir) != 0)) {
		zerror(gettext("out of memory while creating dataset %s."),
		    ds);
		return (Z_ERR);
	}

	if (zfs_create(g_zfs, ds, ZFS_TYPE_FILESYSTEM, props) != 0) {
		zerror(gettext("could not create dataset %s: %s"),
		    ds, libzfs_error_description(g_zfs));
		nvlist_free(props);
		return (Z_ERR);
	}
	nvlist_free(props);

	/* Mount it */
	if ((zhp = zfs_open(g_zfs, ds, ZFS_TYPE_FILESYSTEM)) == NULL) {
		zerror(gettext("could not open dataset %s: %s"),
		    ds, libzfs_error_description(g_zfs));
		(void) zfs_destroy(zhp, B_FALSE);
		return (Z_ERR);
	}

	/*
	 * Not within a zone - no need for "nodevices" mount option.
	 */
	if (zfs_mount(zhp, NULL, 0) != 0) {
		zerror(gettext("could not mount dataset %s: %s"),
		    ds, libzfs_error_description(g_zfs));
		(void) zfs_destroy(zhp, B_FALSE);
		zfs_close(zhp);
		return (Z_ERR);
	}
	zfs_close(zhp);

	/* Length checked above. */
	(void) strlcpy(retds, ds, retlen);
	return (Z_OK);
}

/*
 * Attempt to create a ZFS file system for the specified zonepath, creating
 * hierarchical zfs datasets as needed.  On success, Z_OK is returned.  If the
 * zonepath dataset cannot be created and mounted, error messages are printed
 * to stderr indicating what the problem is and Z_ERR is returned.
 */
int
create_zfs_zonepath(char *zonepath, boolean_t ancestors_only)
{
	zfs_handle_t	*zhp = NULL;
	char		zfs_name[ZFS_MAXNAMELEN]; /* zonepath dataset name */
	char		anc_name[ZFS_MAXNAMELEN]; /* ancestor dataset name */
	nvlist_t	*props = NULL;
	char		rpchild[MAXPATHLEN];	/* root pool child dataset */
	boolean_t	ancestors_created = B_FALSE;
	boolean_t	cleanup_err = B_FALSE;

	/*
	 * Check if the dataset already exists.  If so, be sure it is mounted
	 * and move on.
	 */
	if (path2name(zonepath, zfs_name, sizeof (zfs_name)) == Z_OK &&
	    (zhp = zfs_open(g_zfs, zfs_name, ZFS_TYPE_DATASET)) != NULL) {
		char mountpt[MAXPATHLEN];

		/*
		 * Verify that the default zonepath dataset doesn't already
		 * have a conflicting mountpoint.
		 */
		if (zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, mountpt,
		    sizeof (mountpt), NULL, NULL, 0, B_FALSE) != 0) {
			zerror(gettext("unable to get zfs mountpoint property "
			    "from %s"), zfs_get_name(zhp));
			zfs_close(zhp);
			return (Z_ERR);
		}
		if (strcmp(zonepath, mountpt) != 0) {
			zerror(gettext("default zonepath dataset for %s is %s"),
			    zonepath, zfs_name);
			zerror(gettext("dataset %s exists with mountpoint %s"),
			    zfs_get_name(zhp), mountpt);
			zfs_close(zhp);
			return (Z_ERR);
		}

		/*
		 * If it's not mounted, mount it.  This mount is outside of the
		 * zone so it doesn't need the "nodevices" mount option.
		 */
		if ((zfs_prop_get_int(zhp, ZFS_PROP_MOUNTED) == 0) &&
		    zfs_mount(zhp, NULL, 0) != 0) {
			zerror(gettext("mount of %s on %s failed: %s"),
			    zfs_get_name(zhp), zonepath,
			    libzfs_error_description(g_zfs));
			zfs_close(zhp);
			return (Z_ERR);
		}
		zfs_close(zhp);
		return (Z_OK);
	}
	zfs_name[0] = '\0';

	/*
	 * If the top-level directory doesn't exist, create it as a child
	 * of the root pool.  create_root_pool_child does nothing if it
	 * the top-level directory exists.
	 *
	 * If rpchild has a non-empty string in it, the dataset by that name
	 * will be destroyed on error.
	 */
	rpchild[0] = '\0';
	if (create_root_pool_child(zonepath, rpchild, sizeof (rpchild)) !=
	    Z_OK) {
		/* useful error message already displayed */
		goto errout;
	}

	/*
	 * Look for the closest existing ancestor and the first ancestor
	 * that will need to be created.
	 */
	if (find_closest_ancestor(zonepath, anc_name, sizeof (anc_name),
	    zfs_name, sizeof (zfs_name)) != Z_OK) {
		/* useful error message already displayed */
		goto errout;
	}

	/*
	 * If the first ancestor to create and the zonepath dataset are the
	 * same, don't try to create ancestors as its not needed.
	 */
	if (strcmp(anc_name, zfs_name) != 0) {
		if (zfs_create_ancestors(g_zfs, zfs_name) != 0) {
			zerror(gettext("failed to create ancestors of %s."),
			    anc_name);
			goto errout;
		}
		ancestors_created = B_TRUE;
	}

	if (ancestors_only)
		return (Z_OK);

	/*
	 * We turn off zfs SHARENFS and SHARESMB properties on the
	 * zoneroot dataset in order to prevent the GZ from sharing
	 * NGZ data by accident.
	 */
	if ((nvlist_alloc(&props, NV_UNIQUE_NAME, 0) != 0) ||
	    (nvlist_add_string(props, zfs_prop_to_name(ZFS_PROP_SHARENFS),
	    "off") != 0) ||
	    (nvlist_add_string(props, zfs_prop_to_name(ZFS_PROP_SHARESMB),
	    "off") != 0)) {
		zerror(gettext("out of memory while creating dataset %s."),
		    zfs_name);
		goto errout;
	}

	if (zfs_create(g_zfs, zfs_name, ZFS_TYPE_FILESYSTEM, props) != 0 ||
	    (zhp = zfs_open(g_zfs, zfs_name, ZFS_TYPE_DATASET)) == NULL) {
		zerror(gettext("cannot create ZFS dataset %s: %s"),
		    zfs_name, libzfs_error_description(g_zfs));
		goto errout;
	}

	nvlist_free(props);
	props = NULL;

	/*
	 * This filesystem is outside of the zone so it does not need the
	 * "nodevices" mount option.
	 */
	if (zfs_mount(zhp, NULL, 0) != 0) {
		zerror(gettext("file system %s successfully created but"),
		    zfs_name);
		zerror(gettext("mount failed: %s"),
		    libzfs_error_description(g_zfs));
		zerror(gettext("destroying %s."), zfs_name);
		(void) zfs_destroy(zhp, B_FALSE);
		goto errout;
	}

	if (chmod(zonepath, S_IRWXU) != 0) {
		zerror(gettext("file system %s successfully created but"),
		    zfs_name);
		zerror(gettext("chmod %o failed: %s"), S_IRWXU,
		    strerror(errno));
		zerror(gettext("destroying %s."), zfs_name);
		(void) zfs_unmount(zhp, zonepath, 0);
		(void) zfs_destroy(zhp, B_FALSE);
		goto errout;
	}
	(void) printf(gettext("A ZFS file system has been "
	    "created for this zone.\n"));

	zfs_close(zhp);
	return (Z_OK);

errout:
	if (ancestors_created) {
		char *slash = NULL;
		size_t anc_len = strlen(anc_name);
		char *first_slash;

		/*
		 * Be sure no manipulations above modified anc_name or
		 * zfs_name such that anc_name is not an ancestor of or the
		 * same as zfs_name.  If we this assertion were not true
		 * and we didn't bail out, it could result in data loss.
		 */
		assert(((anc_len > 0) &&
		    (strncmp(anc_name, zfs_name, anc_len) == 0) &&
		    (zfs_name[anc_len] == '\0' || zfs_name[anc_len] == '/')));

		/*
		 * If this becomes a pointer to the null character it means
		 * there is nothing more that can be deleted.
		 */
		first_slash = strchr(zfs_name, '/');

		/*
		 * Perform dataset deletion.  If the zonepath dataset was
		 * created above, it should have also been deleted above.
		 */
		for (slash = strrchr(zfs_name, '/');
		    strcmp(anc_name, zfs_name) <= 0;
		    slash = strrchr(zfs_name, '/')) {

			if (slash != NULL)
				*slash = '\0';

			/* make sure were not operating on a pool name */
			if (first_slash == NULL || *first_slash == '\0')
				break;

			if (zhp != NULL)
				zfs_close(zhp);

			zerror("destroying %s.", zfs_name);

			if ((zhp = zfs_open(g_zfs, zfs_name,
			    ZFS_TYPE_FILESYSTEM)) == NULL) {
				zerror(gettext("cannot open %s for cleanup: "
				    "%s"), zfs_name,
				    libzfs_error_description(g_zfs));
				cleanup_err = B_TRUE;
				continue;
			}

			if (zfs_unmount(zhp, zfs_name, 0) != 0) {
				zerror(gettext("cannot unmount %s: %s"),
				    zfs_name, libzfs_error_description(g_zfs));
				cleanup_err = B_TRUE;
				continue;
			}

			if (zfs_destroy(zhp, B_FALSE) != 0) {
				zerror(gettext("cannot destroy %s: %s"),
				    zfs_name, libzfs_error_description(g_zfs));
				cleanup_err = B_TRUE;
				continue;
			}
		}
	}
	/*
	 * Clean up the root pool child if it was created.
	 */
	if (rpchild[0] != '\0') {
		char mountpoint[MAXPATHLEN];

		if (zhp != NULL)
			zfs_close(zhp);
		if ((zhp = zfs_open(g_zfs, rpchild, ZFS_TYPE_FILESYSTEM)) ==
		    NULL) {
			zerror(gettext("cannot open %s for cleanup: %s"),
			    rpchild, libzfs_error_description(g_zfs));
			cleanup_err = B_TRUE;
			goto errout2;
		}
		if (zfs_unmount(zhp, rpchild, 0) != 0) {
			zerror(gettext("cannot unmount %s: %s"),
			    rpchild, libzfs_error_description(g_zfs));
			cleanup_err = B_TRUE;
			goto errout2;
		}

		if (zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, mountpoint,
		    sizeof (mountpoint), NULL, NULL, 0, B_FALSE) != 0) {
			zerror(gettext("cannot get mountpoint from %s: %s"),
			    rpchild, libzfs_error_description(g_zfs));
			cleanup_err = B_TRUE;
			/* no goto */
		} else {
			if (rmdir(mountpoint) != 0) {
				zerror(gettext("cannot remove mountpoint %d: "
				    "%s"), mountpoint, strerror(errno));
				cleanup_err = B_TRUE;
			}
			/* no goto */
		}

		if (zfs_destroy(zhp, B_FALSE) != 0) {
			zerror(gettext("cannot destroy %s: %s"),
			    rpchild, libzfs_error_description(g_zfs));
			cleanup_err = B_TRUE;
		}
	}
errout2:
	if (cleanup_err)
		zerror(gettext("one ore more datasets may require manual "
		    "cleanup"));
	if (zhp != NULL)
		zfs_close(zhp);
	if (props != NULL)
		nvlist_free(props);
	return (Z_ERR);
}

/*
 * Checks to see if the zhp is for the topmost dataset in a zpool.
 */
boolean_t
is_zpool(zfs_handle_t *zhp)
{
	return (strcmp(zpool_get_name(zfs_get_pool_handle(zhp)),
	    zfs_get_name(zhp)) == 0 ? B_TRUE : B_FALSE);
}

/*
 * Recursively destroy datasets - suitable for use with zfs_iter_children().
 * If a dataset has been cloned, the clone(s) are first promoted.  If the
 * dataset is the root of a zpool, its descendents are destroyed but it is
 * not destroyed nor is it unmounted.
 *
 * zhp		The top-level zfs handle.  It is closed before return.
 * data		Unused.  Should be set to NULL.
 */
static int
destroy_datasets_cb(zfs_handle_t *zhp, void *data)
{
	/* Promote clones before deleting any child datasets.  */
	if (promote_all_clones(zhp) != 0) {
		zfs_close(zhp);
		return (Z_ERR);
	}

	/* Get rid of all the children. */
	if (zfs_iter_children(zhp, destroy_datasets_cb, data) != 0) {
		zfs_close(zhp);
		return (Z_ERR);
	}

	/* Do not mess with the topmost dataset in a zpool. */
	if (is_zpool(zhp)) {
		zfs_close(zhp);
		return (Z_OK);
	}

	/* Get rid of this dataset. */
	(void) zfs_unmount(zhp, NULL, 0);
	if (zfs_destroy(zhp, B_FALSE) != 0) {
		zerror(gettext("could not destroy %s: %s"), zfs_get_name(zhp),
		    libzfs_error_description(g_zfs));
		zfs_close(zhp);
		return (Z_ERR);
	}

	zfs_close(zhp);
	return (Z_OK);
}

/*
 * Recursively destroy datasets.  If a dataset has been cloned, the clone(s)
 * are first promoted.  If the dataset is the root of a zpool, its descendents
 * are destroyed but it is not destroyed nor is it unmounted.
 *
 * name		The name of the top-level zfs dataset to destroy.
 */
static int
destroy_datasets(const char *name) {
	zfs_handle_t *zhp;

	if ((zhp = zfs_open(g_zfs, name, ZFS_TYPE_DATASET)) == NULL) {
		/* Dataset doesn't exist: Nothing to do. */
		return (Z_OK);
	}

	/* destroy_datasets_cb() closes zhp. */
	return (destroy_datasets_cb(zhp, NULL));
}

/*
 * Promotes the clones of the given dataset and its descendants.  Upon
 * successful completion, if the specified dataset or any of its descendants
 * have a non-null value for the origin property, that property will refer to
 * the specified dataset's descendants.
 *
 * zhp		The top-level zfs handle.  It is closed before return.
 * data		Unused.  Should be set to NULL.
 */
static int
demote_datasets_cb(zfs_handle_t *zhp, void *data)
{
	/* Promote clones of this dataset. */
	if (promote_all_clones(zhp) != 0) {
		zfs_close(zhp);
		return (Z_ERR);
	}

	/* Promote clones of descendants. */
	if (zfs_iter_children(zhp, demote_datasets_cb, data) != 0) {
		zfs_close(zhp);
		return (Z_ERR);
	}

	zfs_close(zhp);
	return (Z_OK);
}

/*
 * Promotes the clones of the given dataset and its descendants.  Upon
 * successful completion, if the specified dataset or any of its descendants
 * have a non-null value for the origin property, that property will refer to
 * the specified dataset's descendants.
 *
 * name		The name of the top-level dataset to demote.
 */
static int
demote_datasets(const char *name)
{
	zfs_handle_t *zhp;

	if ((zhp = zfs_open(g_zfs, name, ZFS_TYPE_DATASET)) == NULL)
		return (Z_ERR);
	/* demote_datasets_cb() closes zhp. */
	return (demote_datasets_cb(zhp, NULL));
}

/*
 * Unmounts the zfs file system hierarchy at and below the given dataset.
 *
 * zhp		ZFS handle for the top-level filesystem to unmount.
 * data		Unused.  Should be set to NULL.
 */
static int
unmount_filesystems_cb(zfs_handle_t *zhp, void *data)
{
	/* Unmount children first */
	if (zfs_iter_filesystems(zhp, unmount_filesystems_cb, data) != 0) {
		zfs_close(zhp);
		return (Z_ERR);
	}

	/* Unmount this filesystem. */
	if (zfs_prop_get_int(zhp, ZFS_PROP_MOUNTED) != 0) {
		if (zfs_unmount(zhp, NULL, 0) != 0) {
			zerror(gettext("unable to unmount %s: %s"),
			    zfs_get_name(zhp), libzfs_error_description(g_zfs));
			zfs_close(zhp);
			return (Z_ERR);
		}
	}

	zfs_close(zhp);
	return (Z_OK);
}

/*
 * Umounts the zfs file system hierarchy at and below the zoneroot.
 *
 * zonepath	The parent directory of the hierarchy to unmount.  That is,
 * 		the hierarchy that will be unmounted starts at
 * 		<zonepath>/root - <zonepath> remains mounted.
 */
static int
unmount_zoneroot(const char *zonepath)
{
	char		zoneroot[MAXPATHLEN];
	zfs_handle_t	*zhp;

	(void) snprintf(zoneroot, sizeof (zoneroot), "%s/root", zonepath);
	if ((zhp = mount2zhandle(zoneroot)) == NULL) {
		/* Nothing mounted on zoneroot. */
		return (Z_OK);
	}

	/* closes zhp */
	return (unmount_filesystems_cb(zhp, NULL));
}

/*
 * If the zonepath is a ZFS file system, attempt to destroy it.  If the
 * zonepath dataset is the root dataset of a zpool, the only way to destroy the
 * dataset would be to destroy the zpool.  The zpool is not destroyed - rather,
 * all of the files and directories that remain in the dataset are removed.
 *
 * Returns Z_OK if we were able to zfs_destroy the zonepath, otherwise we
 * return Z_ERR which means manual cleanup is required.
 */
int
destroy_zfs_zonepath(char *zonepath)
{
	zfs_handle_t	*zhp;
	boolean_t	is_clone = B_FALSE;
	char		origin[ZFS_MAXPROPLEN];

	if ((zhp = mount2zhandle(zonepath)) == NULL) {
		/* No file system mounted here - nothing to do. */
		return (Z_OK);
	}

	if (promote_all_clones(zhp) != 0) {
		zerror(gettext("could not promote clones of %s: manual "
		    "cleanup required"), zfs_get_name(zhp));
		zfs_close(zhp);
		return (Z_ERR);
	}

	/* Now cleanup any snapshots remaining. */
	if (zfs_iter_snapshots(zhp, rm_snap, NULL) != 0) {
		zerror(gettext("could not remove snapshots of %s: manual "
		    "cleanup required"), zfs_get_name(zhp));
		zfs_close(zhp);
		return (Z_ERR);
	}

	/*
	 * We can't destroy the file system if it has still has dependents.
	 * There shouldn't be any at this point, but we'll double check.
	 */
	if (zfs_iter_dependents(zhp, B_TRUE, has_dependent, NULL) != 0) {
		(void) fprintf(stderr, gettext("zfs destroy %s failed: the "
		    "dataset still has dependents\n"), zfs_get_name(zhp));
		zfs_close(zhp);
		return (Z_ERR);
	}

	/*
	 * This might be a clone.  Try to get the snapshot so we can attempt
	 * to destroy that as well.
	 */
	if (zfs_prop_get(zhp, ZFS_PROP_ORIGIN, origin, sizeof (origin), NULL,
	    NULL, 0, B_FALSE) == 0)
		is_clone = B_TRUE;

	if (is_zpool(zhp)) {
		DIR *dir;
		char cmd[sizeof (RMCOMMAND) + 2 * MAXPATHLEN + sizeof (" ''/")];
		struct dirent *ent;

		if ((dir = opendir(zonepath)) == NULL) {
			zerror(gettext("can not read directory %s: %s"),
			    zonepath, strerror(errno));
		} else {
			while ((ent = readdir(dir)) != NULL) {
				if (strcmp(ent->d_name, ".") == 0 ||
				    strcmp(ent->d_name, "..") == 0 ||
				    strcmp(ent->d_name, ".zfs") == 0) {
					continue;
				}
				(void) snprintf(cmd, sizeof (cmd),
				    RMCOMMAND " '%s/%s'", zonepath,
				    ent->d_name);
				if (do_subproc(cmd) != 0) {
					zerror(gettext("unable to remove "
					    "%s/%s: manual cleanup required"),
					    zonepath, ent->d_name);
				}
			}
		}
	} else {
		if (zfs_unmount(zhp, NULL, 0) != 0) {
			(void) fprintf(stderr, gettext("zfs unmount %s "
			    "failed: %s\n"), zfs_get_name(zhp),
			    libzfs_error_description(g_zfs));
			zfs_close(zhp);
			return (Z_ERR);
		}

		if (zfs_destroy(zhp, B_FALSE) != 0) {
			/*
			 * If the destroy fails for some reason, try to remount
			 * the file system so that we can use "rm -rf" to clean
			 * up instead.  Since it is in the destroy path, the
			 * zone should never be rebooted with this dataset and
			 * as such there is no need to mount with the
			 * "nodevices" mount option.
			 */
			(void) fprintf(stderr, gettext("zfs destroy %s "
			    "failed: %s\n"), zfs_get_name(zhp),
			    libzfs_error_description(g_zfs));
			(void) zfs_mount(zhp, NULL, 0);
			zfs_close(zhp);
			return (Z_ERR);
		}
		/*
		 * There are various circumstances where destroying a dataset
		 * will not clean up its mountpoint.  See remove_mountpoint()
		 * in libzfs_mount.c for details.
		 */
		(void) rmdir(zonepath);
	}


	if (is_clone) {
		zfs_handle_t	*ohp;

		/*
		 * Try to clean up the snapshot that the clone was taken from.
		 */
		if ((ohp = zfs_open(g_zfs, origin,
		    ZFS_TYPE_SNAPSHOT)) != NULL) {
			if (zfs_iter_dependents(ohp, B_TRUE, has_dependent,
			    NULL) == 0 && zfs_unmount(ohp, NULL, 0) == 0)
				(void) zfs_destroy(ohp, B_FALSE);
			zfs_close(ohp);
		}
	}

	zfs_close(zhp);
	return (Z_OK);
}

/*
 * Return true if the path is its own zfs file system.  We determine this
 * by stat-ing the path to see if it is zfs and stat-ing the parent to see
 * if it is a different fs.
 */
boolean_t
is_zonepath_zfs(char *zonepath)
{
	int res;
	char *path;
	char *parent;
	struct statvfs64 buf1, buf2;

	if (statvfs64(zonepath, &buf1) != 0)
		return (B_FALSE);

	if (strcmp(buf1.f_basetype, "zfs") != 0)
		return (B_FALSE);

	if ((path = strdup(zonepath)) == NULL)
		return (B_FALSE);

	parent = dirname(path);
	res = statvfs64(parent, &buf2);
	free(path);

	if (res != 0)
		return (B_FALSE);

	if (buf1.f_fsid == buf2.f_fsid)
		return (B_FALSE);

	return (B_TRUE);
}

/*
 * Ensures that the mountpoint property of the specified dataset is set to
 * the desired path.  If possible, this is done via an inheriting the
 * mountpoint property from the parent dataset.  If the dataset is mounted,
 * it may be unmounted and remounted.
 *
 * ds		The name of the dataset to verify/fix.
 * path		The directory where the dataset should be mounted.  This
 * 		directory should already exist.
 */
static int
fix_mountpoint(const char *ds, const char *path)
{
	int		ret = Z_ERR;
	zfs_handle_t	*zhp = NULL;
	zfs_handle_t	*pzhp = NULL;		/* parent of zhp */
	char		mp[MAXPATHLEN];
	char		parentds[ZFS_MAXNAMELEN];
	char		canmount[ZFS_MAXPROPLEN];
	zprop_source_t	propsrc;
	char		*slash;

	if ((zhp = zfs_open(g_zfs, ds, ZFS_TYPE_FILESYSTEM)) == NULL) {
		zerror(gettext("unable to open zfs file system %s"), ds);
		return (Z_ERR);
	}

	if (zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, mp, sizeof (mp), &propsrc,
	    NULL, 0, B_TRUE) != 0) {
		zerror(gettext("unable to get %s property for %s: %s"),
		    zfs_prop_to_name(ZFS_PROP_MOUNTPOINT), ds,
		    libzfs_error_description(g_zfs));
		goto errout;
	}

	/*
	 * If the mountpoint property is already set to the proper value
	 * and it is not temporary (i.e. zfs mount -o mountpoint=... <ds>),
	 * just be sure it is mounted.  If the mountpoint property is a
	 * temporary property, in all likelihood the persistent value is
	 * wrong.
	 */
	if (strcmp(path, mp) == 0 && propsrc != ZPROP_SRC_TEMPORARY)
		goto verify_mounted;

	/*
	 * Mountpoint is not OK.  Be sure it is unmounted, then try to fix it.
	 */
	if (zfs_prop_get_int(zhp, ZFS_PROP_MOUNTED) != 0) {
		if (zfs_unmount(zhp, NULL, 0) != 0) {
			zerror(gettext("unable to umount %s from wrong "
			    "mountpoint: %s"), zfs_get_name(zhp),
			    libzfs_error_description(g_zfs));
			goto errout;
		}
	}

	/*
	 * First, check to see if inheriting the mountpoint makes sense.  If
	 * it doesn't, then try settig it locally.
	 */

	/* Get the mountpoint of the parent of ds. */
	if (zfs_parent_name(ds, parentds, sizeof (parentds)) != 0 ||
	    (pzhp = zfs_open(g_zfs, parentds, ZFS_TYPE_FILESYSTEM)) == NULL ||
	    zfs_prop_get(pzhp, ZFS_PROP_MOUNTPOINT, mp, sizeof (mp), NULL,
	    NULL, 0, B_TRUE) != 0) {
		zerror(gettext("unable to get %s property for parent of %s: "
		    "%s"), zfs_prop_to_name(ZFS_PROP_MOUNTPOINT), ds,
		    libzfs_error_description(g_zfs));
		goto set_local;
	}

	/*
	 * If the parent of ds is not mounted at the parent of path, set
	 * locally.
	 */
	assert((slash = strrchr(path, '/')) != NULL);
	if (strncmp(mp, path, slash - path) != 0)
		goto set_local;

	/* If inheriting the mountpoint succeeds, proceed to mounting it. */
	if (zfs_prop_inherit(zhp, zfs_prop_to_name(ZFS_PROP_MOUNTPOINT),
	    B_FALSE) == 0)
		goto verify_mounted;

	zerror(gettext("failed to inherit %s property on %s: %s"),
	    zfs_prop_to_name(ZFS_PROP_MOUNTPOINT), zfs_get_name(zhp),
	    libzfs_error_description(g_zfs));

set_local:
	if (zfs_prop_set(zhp, zfs_prop_to_name(ZFS_PROP_MOUNTPOINT), path) !=
	    0) {
		zerror(gettext("unable to set %s property to %s on %s: %s"),
		    zfs_prop_to_name(ZFS_PROP_MOUNTPOINT), path,
		    zfs_get_name(zhp), libzfs_error_description(g_zfs));
		goto errout;
	}

verify_mounted:
	/* Be sure canmount=on */
	if (zfs_prop_get(zhp, ZFS_PROP_CANMOUNT, canmount, sizeof (canmount),
	    NULL, NULL, 0, B_TRUE) != 0) {
		zerror(gettext("unable to get %s property of %s: %s"),
		    zfs_prop_to_name(ZFS_PROP_CANMOUNT), zfs_get_name(zhp),
		    libzfs_error_description(g_zfs));
		goto errout;
	} else if (strcmp(canmount, "on") != 0 && zfs_prop_set(zhp,
	    zfs_prop_to_name(ZFS_PROP_CANMOUNT), "on") != 0) {
		zerror(gettext("unable to set %s property to %s on %s: %s"),
		    zfs_prop_to_name(ZFS_PROP_CANMOUNT), path,
		    zfs_get_name(zhp), libzfs_error_description(g_zfs));
		goto errout;
	}

	/* Mount it if needed. */
	if (zfs_prop_get_int(zhp, ZFS_PROP_MOUNTED) == 0) {
		if (zfs_mount(zhp, NULL, 0) != 0) {
			zerror(gettext("unable to mount %s on zonepath %s: %s"),
			    ds, path, libzfs_error_description(g_zfs));
			goto errout;
		}
	}

	ret = Z_OK;
errout:
	if (zhp != NULL)
		zfs_close(zhp);
	if (pzhp != NULL)
		zfs_close(pzhp);
	return (ret);
}

/*
 * Copies datasets using zfs_send | zfs_recv.
 *
 * fromname	dataset name being sent.
 * toname	target dataset name.
 *
 * Returns Z_OK if copy is successful, else Z_ERR.
 */
static int
copy_datasets(const char *fromname, const char *toname)
{
	int		ret = Z_ERR;
	int		pipefd[2] = {-1, -1};
	char		fromsnap[sizeof ("YYYY-MM-DD-HH:MM:SS")];
	pid_t		pid;
	void		(*saveint)(int);
	void		(*saveterm)(int);
	void		(*savequit)(int);
	void		(*savehup)(int);
	boolean_t 	restore_sigs = B_FALSE;
	boolean_t 	recv_ok = B_FALSE;
	recvflags_t	recvflags = { 0 };
	int		child_status;
	char		snap_dsname[ZFS_MAXNAMELEN];
	time_t		now;

	/*
	 * Create a temporary snapshot.
	 */
	(void) time(&now);
	assert(strftime(fromsnap, sizeof (fromsnap), "%Y-%m-%d-%H:%M:%S",
	    localtime(&now)) < sizeof (fromsnap));

	if (snprintf(snap_dsname, sizeof (snap_dsname), "%s@%s", fromname,
	    fromsnap) >= sizeof (snap_dsname)) {
		zerror(gettext("dataset name %s@%s too long"), fromname,
		    fromsnap);
		snap_dsname[0] = '\0';
		goto out;
	}

	if (zfs_snapshot(g_zfs, snap_dsname, B_TRUE, NULL) != 0) {
		zerror(gettext("could not create snapshot %s: %s"), fromsnap,
		    libzfs_error_description(g_zfs));
		snap_dsname[0] = '\0';
		goto out;
	}

	/*
	 * Set up a pipe that can be used between zfs_send() and zfs_receive().
	 * Run zfs_send() in the child process.
	 */

	if (pipe(pipefd) != 0) {
		zperror(gettext("cannot create pipe"), B_FALSE);
		goto out;
	}

	(void) printf(gettext("Copying from %s to %s: please be patient\n"),
	    fromname, toname);

	if ((pid = fork()) < 0) {
		zperror(gettext("cannot create sending process"), B_FALSE);
		goto out;
	} else if (pid == 0) {		/* child, sends the stream */
		sendflags_t	flags = { 0 };
		zfs_handle_t	*zhp;

		(void) close(pipefd[0]);
		pipefd[0] = -1;

		/*
		 * If admin loses connection during long running copy, do
		 * not abort the copy.
		 */
		(void) sigset(SIGHUP, SIG_IGN);

		/*
		 * Be sure it quits on SIGPIPE, SIGTERM, and SIGINT.  The
		 * parent will take care of any required cleanup.
		 */
		(void) sigset(SIGPIPE, SIG_DFL);
		(void) sigset(SIGINT, SIG_DFL);
		(void) sigset(SIGTERM, SIG_DFL);

		/* Equivalent to zfs send -rc */
		flags.replicate = B_TRUE;
		flags.selfcont = B_TRUE;

		if ((zhp = zfs_open(g_zfs, fromname, ZFS_TYPE_DATASET)) ==
		    NULL) {
			zerror(gettext("unable to open dataset %s: %s"),
			    fromname, libzfs_error_description(g_zfs));
			_exit(1);
		}

		if (zfs_send(zhp, NULL, fromsnap, flags, pipefd[1], NULL,
		    NULL, NULL) != 0) {
			zerror(gettext("unable to send snapshot %s: %s"),
			    fromname, libzfs_error_description(g_zfs));
			zfs_close(zhp);
			_exit(1);
		}
		zfs_close(zhp);

		_exit(0);
	}
	/* parent, receives the stream */
	(void) close(pipefd[1]);
	pipefd[1] = -1;

	/*
	 * Ignore common termination signals.  A ^C or similar on the terminal
	 * will cause the child to be signaled, allowing it to die and leaving
	 * the parent to clean up.  Ignore SIGHUP so that a lost network
	 * connection doesn't leave the zone in a half copied state.
	 */
	saveint = sigset(SIGINT, SIG_IGN);
	saveterm = sigset(SIGTERM, SIG_IGN);
	savequit = sigset(SIGQUIT, SIG_IGN);
	savehup = sigset(SIGHUP, SIG_IGN);
	restore_sigs = B_TRUE;

	/* Equivalent to zfs receive -Fu */
	recvflags.force = B_TRUE;
	recvflags.nomount = B_TRUE;

	if (zfs_receive(g_zfs, toname, recvflags, NULL, pipefd[0], NULL) != 0) {
		/*
		 * Close the file descriptor so that the child gets EPIPE on
		 * the next write and exits.  Do not print an error yet, as
		 * handling of the dead child process may provide more
		 * relevant messages than "invalid backup stream" that is
		 * most likely to come from zfs_receive()'s failure.
		 */
		(void) close(pipefd[0]);
		pipefd[0] = -1;
	} else {
		recv_ok = B_TRUE;
	}

	while (waitpid(pid, &child_status, 0) != pid) {
		if (errno == EINTR)
			continue;
		zperror(gettext("wait for sending process failed"), B_FALSE);
		recv_ok = B_FALSE;
		goto out;
	}

	if (WIFSIGNALED(child_status)) {
		char signame[SIG2STR_MAX] = "unknown signal";

		(void) sig2str(WTERMSIG(child_status), signame);
		zerror(gettext("sending process received signal %s"), signame);
		recv_ok = B_FALSE;
		goto out;
	}
	if (WEXITSTATUS(child_status) != 0) {
		zerror(gettext("sending process failed to fully send %s"),
		    fromsnap);
		recv_ok = B_FALSE;
		goto out;
	}

	if (recv_ok) {
		ret = Z_OK;
	} else {
		zerror(gettext("unable to receive dataset %s: %s"), toname,
		    libzfs_error_description(g_zfs));
	}

out:
	if (pipefd[0] != -1)
		(void) close(pipefd[0]);
	if (pipefd[1] != -1)
		(void) close(pipefd[1]);

	if (restore_sigs) {
		(void) sigset(SIGINT, saveint);
		(void) sigset(SIGTERM, saveterm);
		(void) sigset(SIGQUIT, savequit);
		(void) sigset(SIGHUP, savehup);
	}

	if (!recv_ok) {
		zerror(gettext("cleaning up partially copied datasets: "
		    "please be patient"));
		if (destroy_datasets(toname) != Z_OK) {
			zerror(gettext("cleanup failed: manual removal of %s "
			    "required"), toname);
		}
	}

	/* Clean up temporary snapshot, if created */
	if (snap_dsname[0] != '\0') {
		zfs_handle_t *zhp;

		/* Clean up source snapshot */
		if ((zhp = zfs_open(g_zfs, fromname, ZFS_TYPE_DATASET)) ==
		    NULL || zfs_destroy_snaps(zhp, fromsnap, B_FALSE) != 0) {
			zerror(gettext("could not remove temporary recursive "
			    "snapshot %s@%s"), fromname, fromsnap);
		}
		if (zhp != NULL)
			zfs_close(zhp);

		/*
		 * If the recv succeeded, clean up the destination temporary
		 * snapshot.  If the recv did not succed, the temporary
		 * snapshot will have been cleaned up with the rest of the
		 * datasets above.
		 */
		if (recv_ok) {
			if ((zhp = zfs_open(g_zfs, toname, ZFS_TYPE_DATASET)) ==
			    NULL || zfs_destroy_snaps(zhp, fromsnap,
			    B_FALSE) != 0) {
				zerror(gettext("could not remove temporary "
				    "recursive snapshot %s@%s"), toname,
				    fromsnap);
			}
			if (zhp != NULL)
				zfs_close(zhp);
		}
	}

	return (ret);
}

/*
 * Implement the fast move of a ZFS file system by renaming it.  If it is
 * part of a different pool, it is copied using zfs replication.
 *
 * Upon success, zonepath does not exist.  In the case of moving to another
 * pool, the zonepath dataset and its descendants have been destroyed.
 *
 * Upon failure, the dataset that wass mounted on new_zonepath, if any,
 * may be destroyed and the new_zonepath directory may be removed.
 */
int
move_zfs(char *zonepath, char *new_zonepath)
{
	int		ret = Z_ERR;
	char		mp[MAXPATHLEN];	/* mount point */
	char		nzpparent[MAXPATHLEN];
	zfs_handle_t	*zhp = NULL;	/* handle for zonepath */
	zfs_handle_t	*nzhp = NULL;	/* handle for new_zonepath */
	char		*slash;
	size_t		len;
	char		zpds[ZFS_MAXNAMELEN];
	char		nzpds[ZFS_MAXNAMELEN];
	char		*cleanup_zpds, *cleanup_zonepath;
	boolean_t	move_done = B_FALSE;

	zpds[0] = '\0';
	nzpds[0] = '\0';

	/* Be sure there are not hierarchy problems. */
	len = strlen(zonepath);
	if (strncmp(zonepath, new_zonepath, len) == 0 &&
	    new_zonepath[len] == '/') {
		zerror(gettext("new zonepath must not be a descendant of "
		    "current zonepath"));
		return (Z_ERR);
	}

	len = strlen(new_zonepath);
	if (strncmp(zonepath, new_zonepath, len) == 0 &&
	    zonepath[len] == '/') {
		zerror(gettext("current zonepath must not be a descendant of "
		    "new zonepath"));
		return (Z_ERR);
	}


	/* Get zfs handles for current and new zonepaths. */
	if ((zhp = mount2zhandle(zonepath)) == NULL) {
		zerror(gettext("zonepath %s is not the mountpoint of a ZFS "
		    "file system"), zonepath);
		goto out;
	}
	(void) strncpy(zpds, zfs_get_name(zhp), sizeof (zpds));

	if ((nzhp = zfs_path_to_zhandle(g_zfs, new_zonepath,
	    ZFS_TYPE_FILESYSTEM)) == NULL) {
		zerror(gettext("new zonepath %s is not part of a ZFS file "
		    "system"), new_zonepath);
		goto out;
	}

	/* Figure out where the mountpoint is relative to the new_zonepath. */
	if (zfs_prop_get(nzhp, ZFS_PROP_MOUNTPOINT, mp, sizeof (mp), NULL,
	    NULL, 0, B_TRUE) != 0) {
		zerror(gettext("unable to get mountpoint for %s: %s"),
		    zfs_get_name(nzhp), libzfs_error_description(g_zfs));
		goto out;
	}

	/* If they are in different pools, copy the data to the new pool */
	if (strcmp(zpool_get_name(zfs_get_pool_handle(zhp)),
	    zpool_get_name(zfs_get_pool_handle(nzhp))) != 0) {

		/* If the new zonepath already exists, don't create it again. */
		if (strcmp(mp, new_zonepath) != 0) {
			if (create_zfs_zonepath(new_zonepath, B_FALSE) !=
			    Z_OK) {
				zerror(gettext("failed to create new "
				    "zonepath"));
				goto out;
			}
			zfs_close(nzhp);
			if ((nzhp = mount2zhandle(new_zonepath)) == NULL) {
				zerror(gettext("unable to find newly created "
				    "zonepath dataset"));
				goto out;
			}
		}

		/* Copy the datasets */
		(void) strncpy(nzpds, zfs_get_name(nzhp), sizeof (nzpds));
		if (copy_datasets(zfs_get_name(zhp), zfs_get_name(nzhp)) !=
		    Z_OK) {
			zerror(gettext("unable to copy datasets from %s to "
			    "%s"), zfs_get_name(zhp), zfs_get_name(nzhp));
			goto out;
		}

		/* Copy is good.  Do not delete new zonepath. */
		move_done = B_TRUE;

	} else {
		/* It is the same pool.  We just need to rename.  */

		/*
		 * If a dataset is already mounted on the new zonepath,
		 * something is wrong.
		 */
		if (strcmp(mp, new_zonepath) == 0) {
			zerror(gettext("new zonepath %s is busy: %s is "
			    "mounted on it"), new_zonepath, zfs_get_name(nzhp));
			goto out;
		}

		if (strlcpy(nzpparent, new_zonepath, sizeof (nzpparent)) >=
		    sizeof (nzpparent)) {
			zerror(gettext("new zonepath is too long"));
			goto out;
		}
		/*
		 * We've already determined that the new zonepath and the old
		 * zonepath are in the same pool and that the old and new are
		 * not not within the same hierarchy.  Thus, the parent of
		 * new_zonepath can't be a top-level dataset.
		 */
		assert((slash = strrchr(nzpparent, '/')) != NULL);
		*slash = '\0';

		if (strcmp(nzpparent, mp) != 0) {
			zerror(gettext("parent directory of new zonepath %s "
			    "is not a ZFS mountpoint"), new_zonepath);
			goto out;
		}

		assert((slash = strrchr(new_zonepath, '/')) != NULL);

		if (snprintf(nzpds, sizeof (nzpds), "%s%s", zfs_get_name(nzhp),
		    slash) >= sizeof (nzpds)) {
			zerror(("new zonepath dataset name %s%s is too long"),
			    zfs_get_name(nzhp), slash);
			goto out;
		}

		if (unmount_zoneroot(zonepath) != 0) {
			zerror(gettext("unable to unmount the active zone boot "
			    "environment"));
			goto out;
		}
		if (demote_datasets(zfs_get_name(zhp)) != 0) {
			zerror(gettext("unable to promote clones of %s and "
			    "its descendants"), zfs_get_name(zhp));
			/*
			 * Fall through - maybe the rename will still work.
			 * Even if it doesn't no data was harmed, we just
			 * get another error message.
			 */
		}
		if (zfs_rename(zhp, nzpds, B_FALSE) != 0) {
			zerror(gettext("unable to rename %s to %s: %s"),
			    zfs_get_name(zhp), nzpds,
			    libzfs_error_description(g_zfs));
			goto out;
		}

		move_done = B_TRUE;
	}

	/*
	 * Since the rename has happened, from here on we always return Z_OK.
	 * If things go wrong, a detailed error message needs to be provided so
	 * that the user has a chance of fixing it.  We don't want to return
	 * Z_ERR and cause the zone configuration update to fail.
	 */
	ret = Z_OK;

	/* Verify that the mountpoint property is set correctly. */
	if (fix_mountpoint(nzpds, new_zonepath) != Z_OK) {
		zerror(gettext("unable to verify mountpoint=%s for new "
		    "zonepath dataset %s"), new_zonepath, nzpds);
	}

out:
	if (zhp != NULL)
		zfs_close(zhp);
	if (nzhp != NULL)
		zfs_close(nzhp);
	/* Select zonepath and zpds OR new_zonepath and nzpds for cleanup */
	if (move_done) {
		cleanup_zonepath = zonepath;
		cleanup_zpds = zpds;
	} else {
		cleanup_zonepath = new_zonepath;
		cleanup_zpds = nzpds;
	}

	/*
	 * Destroy the zone's datasets.  If the zonepath dataset is the
	 * root dataset of a zpool, it will not be destroyed and we need
	 * to clean up whatever is in it.
	 */
	if (cleanup_zpds[0] != '\0' && destroy_datasets(cleanup_zpds) != Z_OK) {
		zerror(gettext("failed to clean up dataset %s: manual "
		    "cleanup required"), cleanup_zpds);
	} else if (cleanup_zonepath[0] != '\0' &&
	    strchr(cleanup_zpds, '/') == NULL) {
		/*
		 * Just purge the contents - do not destroy the zpool.  Since
		 * the zonepath dataset is mounted at the mountpoint for the
		 * zpool's root dataset, the zonepath itself cannot be removed.
		 */
		(void) destroy_zfs_zonepath(cleanup_zonepath);
	} else if (rmdir(cleanup_zonepath) != 0 && errno != ENOENT) {
		zerror(gettext("could not remove directory %s: %s"),
		    cleanup_zonepath, strerror(errno));
	}

	return (ret);
}

/*
 * Always succeeds, even when it detects problems.  If the given dataset is a
 * volume, increment the error counter in the callback data.
 *
 * zfs_iter_dependents() iterates through filesystems, volumes, snapshots, and
 * clones.  verify_zfs_type_not_volume_cb() is sometimes called as callback
 * from zfs_iter_dependents().  We need to provide a way for the callback to
 * know which clones (and snapshots of clones and ...) that it should not pay
 * attention to.  In such a case, data->zpds refers to the zonepath dataset of
 * the zone that is being checked.
 *
 * If data->zpds is not NULL, the volume check is only done if zhp refers to
 * a dataset that is a hierarchical descendant of the zone path dataset.
 *
 * Must have return type of int to match zfs_iter_f.
 */
static int
verify_zfs_type_not_volume_cb(zfs_handle_t *zhp, void *data)
{
	verify_zpds_cb_data_t *dp = data;
	const char *ds = zfs_get_name(zhp);

	if (dp->zpds != NULL) {
		size_t len;
		len = strlen(dp->zpds);

		/*
		 * If it's not a hierarchical descendant of the zonepath
		 * dataset, don't worry about it.
		 */
		if (!(strncmp(dp->zpds, ds, len) == 0 &&
		    (ds[len] == '/' || ds[len] == '\0')))
			return (0);
	}

	if (zfs_get_type(zhp) == ZFS_TYPE_VOLUME) {
		zerror(gettext("ZFS volume %s not allowed in zone boot "
		    "environment"), ds);
		dp->errs++;
	}

	return (0);
}

/*
 * Check the direct descendants of the zonepath dataset.
 */
static int
verify_zpds_cb(zfs_handle_t *zhp, void *data)
{
	verify_zpds_cb_data_t *dp = data;
	char *name = NULL;			/* copy of dataset name */
	char *bname;				/* basename of name */

	if ((name = strdup(zfs_get_name(zhp))) == NULL) {
		zerror(gettext("Cannot verify %s: out of memory"),
		    zfs_get_name(zhp));
		dp->errs++;
		return (-1);
	}
	bname = basename(name);

	/*
	 * rpool is OK only if it is a file system.  Checks within rpool
	 * are done elsewhere.
	 */
	if (strcmp(bname, "rpool") == 0) {
		free(name);
		return (verify_zfs_type_not_volume_cb(zhp, data));
	}

	/*
	 * Any other dataset that is found is an error.
	 */
	if (strcmp(bname, "ROOT") == 0) {
		(void) fprintf(stderr, gettext("Error:\tCannot %s zone %s "
		    "without upgrading zone boot\n\tenvironment layout.\n\n "
		    "Note:\tConverting the zone boot environment layout is a "
		    "one-way operation.\n\tOnce converted, the zone will not "
		    "be bootable under old boot\n\tenvironments.  See "
		    "/usr/lib/brand/shared/README.dsconvert for the\n\t"
		    "conversion process.\n\n"), dp->cmdname, target_zone);
		free(name);
		dp->errs++;
		return (0);
	}

	zerror(gettext("Invalid dataset %s exists in zonepath dataset."),
	    zfs_get_name(zhp));
	free(name);
	dp->errs++;
	return (0);
}

/*
 * Verify that the zonepath does not contain unexpected datasets.  The
 * expected dataset layout is:
 *
 * .../zonename			Zonepath dataset
 * .../zonename/rpool		May or may not exist depending on history of
 *				the zonepath.  If it exists it must be a
 *				filesystem.
 * .../zonename/rpool/ROOT	If it exists, it and all of its children must
 * 				not be zvols.
 * .../zonename/rpool/ *	Anything else is OK to be a fileystem or zvol.
 * .../zonename/ROOT		Triggers old dataset layout error
 * .../zonename/ *		Anything else is invalid.
 */
int
verify_zonepath_datasets(char *path, const char *cmdname)
{
	zfs_handle_t *zhp = NULL;		/* handle for zonepath ds */
	zfs_handle_t *child_zhp = NULL;		/* handle for descendant ds */
	verify_zpds_cb_data_t data = {0};
	char dsname[ZFS_MAXNAMELEN];
	int ret = Z_OK;

	if (path2name(path, dsname, sizeof (dsname)) != Z_OK ||
	    is_mountpnt(path, MNTTYPE_ZFS) == B_FALSE) {
		zerror(gettext("zonepath %s is not root of a zfs dataset"),
		    path);
		return (Z_ERR);
	}

	/* Used for some error reporting in callbacks */
	data.cmdname = cmdname;

	/*
	 * Verify direct descendants.
	 */
	if ((zhp = zfs_open(g_zfs, dsname, ZFS_TYPE_FILESYSTEM)) == NULL) {
		zerror(gettext("unable to open dataset %s: %s"),
		    dsname, libzfs_error_description(g_zfs));
		return (Z_ERR);
	}
	if (zfs_iter_filesystems(zhp, verify_zpds_cb, &data) != 0) {
		/* Error message already printed */
		ret = Z_ERR;
		goto out;
	}

	/*
	 * Verify the dataset that contains the boot environments.
	 */
	if (snprintf(dsname, sizeof (dsname), "%s/" ZONE_BE_CONTAINER_DS,
	    zfs_get_name(zhp)) >= sizeof (dsname)) {
		zerror(gettext("dataset name '%s/" ZONE_BE_CONTAINER_DS
		    "' too long."), dsname);
		ret = Z_ERR;
		goto out;
	}

	/*
	 * If this fails it likely means it hasn't been created yet.  That's
	 * not an error.
	 */
	if ((child_zhp = zfs_open(g_zfs, dsname, ZFS_TYPE_DATASET)) == NULL)
		goto out;
	(void) verify_zfs_type_not_volume_cb(child_zhp, &data);

	/*
	 * zfs_iter_dependents() iterates through filesystems, volumes,
	 * snapshots, and clones.  We need to provide a way for the callback to
	 * know which clones (and snapshots of clones and ...) that it should
	 * not pay attention to.  The zonepath dataset name is provided for
	 * this purpose.  Note that if data.zpds is NULL, such as in the call
	 * above, this filtering is bypassed and all datasets are check.  Above
	 * it is OK to be NULL because it's checking a specific dataset, not
	 * iterating through a potentially large graph.
	 */
	data.zpds = zfs_get_name(zhp);
	if (zfs_iter_dependents(child_zhp, B_TRUE,
	    verify_zfs_type_not_volume_cb, &data) != 0)
		ret = Z_ERR;

out:
	if (zhp != NULL)
		zfs_close(zhp);
	if (child_zhp != NULL)
		zfs_close(child_zhp);
	if (data.errs != 0)
		return (Z_ERR);
	return (ret);
}

/*
 * Validate that the given datasets exist on the system.
 *
 * Note that we don't do anything with the 'zoned' property here.  All
 * management is done in zoneadmd when the zone is actually rebooted.  This
 * allows us to automatically set the zoned property even when a zone is
 * rebooted by the administrator.
 */
int
verify_datasets(zone_dochandle_t handle)
{
	int return_code = Z_OK;
	struct zone_dstab dstab;
	zfs_handle_t *zhp;

	if (zonecfg_setdsent(handle) != Z_OK) {
		/*
		 * TRANSLATION_NOTE
		 * zfs and dataset are literals that should not be translated.
		 */
		(void) fprintf(stderr, gettext("could not verify zfs datasets: "
		    "unable to enumerate datasets\n"));
		return (Z_ERR);
	}

	while (zonecfg_getdsent(handle, &dstab) == Z_OK) {
		if ((zhp = zfs_open(g_zfs, dstab.zone_dataset_name,
		    ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME)) == NULL) {
			(void) fprintf(stderr, gettext("could not verify zfs "
			    "dataset %s: %s\n"), dstab.zone_dataset_name,
			    libzfs_error_description(g_zfs));
			return_code = Z_ERR;
			continue;
		}
		zfs_close(zhp);
	}
	(void) zonecfg_enddsent(handle);

	return (return_code);
}

/*
 * Verify that the ZFS dataset exists, and its mountpoint
 * property is set to "legacy".
 */
int
verify_fs_zfs(struct zone_fstab *fstab)
{
	zfs_handle_t *zhp;
	char propbuf[ZFS_MAXPROPLEN];

	if ((zhp = zfs_open(g_zfs, fstab->zone_fs_special,
	    ZFS_TYPE_DATASET)) == NULL) {
		(void) fprintf(stderr, gettext("could not verify fs %s: "
		    "could not access zfs dataset '%s'\n"),
		    fstab->zone_fs_dir, fstab->zone_fs_special);
		return (Z_ERR);
	}

	if (zfs_get_type(zhp) != ZFS_TYPE_FILESYSTEM) {
		(void) fprintf(stderr, gettext("cannot verify fs %s: "
		    "'%s' is not a file system\n"),
		    fstab->zone_fs_dir, fstab->zone_fs_special);
		zfs_close(zhp);
		return (Z_ERR);
	}

	if (zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, propbuf, sizeof (propbuf),
	    NULL, NULL, 0, 0) != 0 || strcmp(propbuf, "legacy") != 0) {
		(void) fprintf(stderr, gettext("could not verify fs %s: "
		    "zfs '%s' mountpoint is not \"legacy\"\n"),
		    fstab->zone_fs_dir, fstab->zone_fs_special);
		zfs_close(zhp);
		return (Z_ERR);
	}

	zfs_close(zhp);
	return (Z_OK);
}

int
init_zfs(void)
{
	if ((g_zfs = libzfs_init()) == NULL) {
		(void) fprintf(stderr, gettext("failed to initialize ZFS "
		    "library\n"));
		return (Z_ERR);
	}

	return (Z_OK);
}
