/*
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * BSD 3 Clause License
 *
 * Copyright (c) 2007, The Storage Networking Industry Association.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 	- Redistributions of source code must retain the above copyright
 *	  notice, this list of conditions and the following disclaimer.
 *
 * 	- Redistributions in binary form must reproduce the above copyright
 *	  notice, this list of conditions and the following disclaimer in
 *	  the documentation and/or other materials provided with the
 *	  distribution.
 *
 *	- Neither the name of The Storage Networking Industry Association (SNIA)
 *	  nor the names of its contributors may be used to endorse or promote
 *	  products derived from this software without specific prior written
 *	  permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <string.h>
#include "ndmpd.h"
#include <libzfs.h>

typedef struct snap_param {
	char *snp_name;
	boolean_t snp_found;
} snap_param_t;

static int snapshot_delete(char *snapname, char *jname, boolean_t recursive);

/*
 * ndmp_has_backup
 *
 * Call backup function which looks for backup snapshot.
 * This is a callback function used with zfs_iter_snapshots.
 *
 * Parameters:
 *   zhp (input) - ZFS handle pointer
 *   data (output) - 0 - no backup snapshot
 *		     1 - has backup snapshot
 *
 * Returns:
 *   0: on success
 *  -1: otherwise
 */
static int
ndmp_has_backup(zfs_handle_t *zhp, void *data)
{
	const char *name;
	snap_param_t *chp = (snap_param_t *)data;

	name = zfs_get_name(zhp);
	if (name == NULL ||
	    strcmp(name, chp->snp_name) != 0) {
		zfs_close(zhp);
		return (-1);
	}

	chp->snp_found = 1;
	zfs_close(zhp);

	return (0);
}

/*
 * ndmp_has_backup_snapshot
 *
 * Returns TRUE if the volume has an active backup snapshot, otherwise,
 * returns FALSE.
 *
 * Parameters:
 *   volname (input) - name of the volume
 *   jobname (input) - name associated with backup job
 *
 * Returns:
 *   0: on success
 *  -1: otherwise
 */
static int
ndmp_has_backup_snapshot(char *volname, char *jobname)
{
	zfs_handle_t *zhp;
	snap_param_t snp;
	char chname[ZFS_MAXNAMELEN];

	(void) mutex_lock(&zlib_mtx);
	if ((zhp = zfs_open(zlibh, volname, ZFS_TYPE_DATASET)) == 0) {
		NDMP_LOG(LOG_ERR, "Cannot open snapshot %s.", volname);
		(void) mutex_unlock(&zlib_mtx);
		return (-1);
	}

	snp.snp_found = 0;
	(void) snprintf(chname, ZFS_MAXNAMELEN, "%s@%s", volname, jobname);
	snp.snp_name = chname;

	(void) zfs_iter_snapshots(zhp, ndmp_has_backup, &snp);
	zfs_close(zhp);
	(void) mutex_unlock(&zlib_mtx);

	return (snp.snp_found);
}

/*
 * ndmp_create_snapshot
 *
 * This function will parse the path to get the real volume name.
 * It will then create a snapshot based on volume and job name.
 * This function should be called before the NDMP backup is started.
 *
 * Parameters:
 *   session (input) - pointer to the session
 *   vol_name (input) - name of the volume
 *   jname (input) - name associated with backup job
 *
 * Returns:
 *   0: on success
 *   -1: otherwise
 */
int
ndmp_create_snapshot(ndmpd_session_t *session, char *vol_name, char *jname)
{
	char vol[ZFS_MAXNAMELEN];

	if (vol_name == 0 ||
	    get_zfsvolname(vol, sizeof (vol), vol_name) == -1)
		return (0);

	/*
	 * If there is an old snapshot left from the previous
	 * backup it could be stale one and it must be
	 * removed before using it.
	 */
	if (ndmp_has_backup_snapshot(vol, jname))
		(void) snapshot_destroy(vol, jname, B_FALSE, B_TRUE,
		    &session->ns_clnup_fd, NULL);

	return (snapshot_create(vol, jname, B_FALSE, B_TRUE,
	    &session->ns_clnup_fd));
}

/*
 * ndmp_remove_snapshot
 *
 * This function will parse the path to get the real volume name.
 * It will then remove the snapshot for that volume and job name.
 * This function should be called after NDMP backup is finished.
 *
 * Parameters:
 *   session (input) - pointer to the session
 *   vol_name (input) - name of the volume
 *   jname (input) - name associated with backup job
 *
 * Returns:
 *   0: on success
 *   -1: otherwise
 */
int
ndmp_remove_snapshot(ndmpd_session_t *session, char *vol_name, char *jname)
{
	char vol[ZFS_MAXNAMELEN];

	if (vol_name == 0 ||
	    get_zfsvolname(vol, sizeof (vol), vol_name) == -1)
		return (0);

	return (snapshot_destroy(vol, jname, B_FALSE, B_TRUE,
	    &session->ns_clnup_fd, NULL));
}

/*
 * Put a hold on snapshot
 */
static int
snapshot_hold(char *volname, char *snapname, char *jname, boolean_t recursive,
    int *cleanup_fd)
{
	zfs_handle_t *zhp;
	char *p;

	if ((zhp = zfs_open(zlibh, volname, ZFS_TYPE_DATASET)) == 0) {
		NDMP_LOG(LOG_ERR, "Cannot open volume %s.", volname);
		return (-1);
	}

	if (*cleanup_fd == -1 &&
	    (*cleanup_fd = open(ZFS_DEV, O_RDWR|O_EXCL)) < 0) {
		NDMP_LOG(LOG_ERR, "Cannot open dev %d", errno);
		zfs_close(zhp);
		return (-1);
	}

	p = strchr(snapname, '@') + 1;
	if (zfs_hold(zhp, p, jname, recursive, B_TRUE, B_FALSE,
	    *cleanup_fd, 0, 0) != 0) {
		NDMP_LOG(LOG_ERR, "Cannot hold snapshot %s", p);
		zfs_close(zhp);
		return (-1);
	}
	zfs_close(zhp);
	return (0);
}

/*
 * Create a snapshot on the volume
 */
int
snapshot_create(char *volname, char *jname, boolean_t recursive,
    boolean_t hold, int *cleanup_fd)
{
	char snapname[ZFS_MAXNAMELEN];
	int rv;

	if (!volname || !*volname)
		return (-1);

	(void) snprintf(snapname, ZFS_MAXNAMELEN, "%s@%s", volname, jname);

	(void) mutex_lock(&zlib_mtx);
	if ((rv = zfs_snapshot(zlibh, snapname, recursive, NULL))
	    == -1) {
		if (errno == EEXIST) {
			(void) mutex_unlock(&zlib_mtx);
			return (0);
		}
		NDMP_LOG(LOG_ERR,
		    "snapshot_create: %s failed (err=%d): %s",
		    snapname, errno, libzfs_error_description(zlibh));
		(void) mutex_unlock(&zlib_mtx);
		return (rv);
	}
	if (hold && snapshot_hold(volname, snapname, jname, recursive,
	    cleanup_fd) != 0) {
		NDMP_LOG(LOG_ERR,
		    "snapshot_create: %s hold failed (err=%d): %s",
		    snapname, errno, libzfs_error_description(zlibh));
		(void) snapshot_delete(snapname, jname, recursive);
		(void) mutex_unlock(&zlib_mtx);
		return (-1);
	}

	(void) mutex_unlock(&zlib_mtx);
	return (0);
}

/*
 * Remove the backup snapshot
 */
static int
snapshot_delete(char *snapname, char *jname, boolean_t recursive)
{
	zfs_handle_t *zhp;
	zfs_type_t ztype;
	int rv;

	ztype = recursive ? (ZFS_TYPE_VOLUME | ZFS_TYPE_FILESYSTEM) :
	    ZFS_TYPE_SNAPSHOT;

	if ((zhp = zfs_open(zlibh, snapname, ztype)) == NULL) {
		NDMP_LOG(LOG_DEBUG, "snapshot_delete: open %s failed",
		    snapname);
		return (-1);
	}

	if (recursive)
		rv = zfs_destroy_snaps(zhp, jname, B_TRUE);
	else
		rv = zfs_destroy(zhp, B_TRUE);

	zfs_close(zhp);
	return (rv);
}

/*
 * Remove and release the backup snapshot
 */
int
snapshot_destroy(char *volname, char *jname, boolean_t recursive,
    boolean_t hold, int *cleanup_fd, int *zfs_err)
{
	char snapname[ZFS_MAXNAMELEN];
	char *namep;
	int err;

	if (zfs_err)
		*zfs_err = 0;

	if (!volname || !*volname)
		return (-1);

	if (recursive) {
		namep = volname;
	} else {
		(void) snprintf(snapname, ZFS_MAXNAMELEN, "%s@%s", volname,
		    jname);
		namep = snapname;
	}

	(void) mutex_lock(&zlib_mtx);
	if (hold && *cleanup_fd != -1) {
		if (close(*cleanup_fd) != 0) {
			NDMP_LOG(LOG_ERR,
			    "snapshot_destroy: %s release failed (err=%d): %s",
			    namep, errno, libzfs_error_description(zlibh));
		}
		*cleanup_fd = -1;
	}

	if ((err = snapshot_delete(namep, jname, recursive)) == -1) {
		(void) mutex_unlock(&zlib_mtx);
		return (-1);
	}

	if (err) {
		NDMP_LOG(LOG_ERR, "%s (recursive destroy: %d): %d; %s; %s",
		    namep,
		    recursive,
		    libzfs_errno(zlibh),
		    libzfs_error_action(zlibh),
		    libzfs_error_description(zlibh));

		if (zfs_err)
			*zfs_err = err;
	}
	(void) mutex_unlock(&zlib_mtx);

	return (0);
}
