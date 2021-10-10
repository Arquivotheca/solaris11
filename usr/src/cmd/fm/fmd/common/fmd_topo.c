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
 * FMD Topology Handling
 *
 * Fault manager scheme and module plug-ins may need access to the latest
 * libtopo snapshot.  Upon fmd initialization, a snapshot is taken and
 * made available via fmd_fmri_topology() and fmd_hdl_topology().  Each
 * of these routines returns a libtopo snapshot handle back to the caller.
 * New snapshots are taken if and when a DR event causes the DR generation
 * number to increase.  The current snapshot is retained to assure consistency
 * for modules still using older snapshots and the latest snapshot handle is
 * returned to the caller.
 */

#include <fmd_alloc.h>
#include <fmd_conf.h>
#include <fmd_error.h>
#include <fmd_subr.h>
#include <fmd_topo.h>
#include <fmd.h>

#include <string.h>
#include <unistd.h>
#include <uuid/uuid.h>
#include <sys/ddi.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <dirent.h>
#include <fm/fmd_fmri.h>
#include <fm/libtopo.h>

static void
fmd_topo_rele_locked(fmd_topo_t *ftp, const char *caller)
{
	ASSERT(MUTEX_HELD(&fmd.d_topo_lock));

	fmd_dprintf(FMD_DBG_TOPO, "%s: %s: ftp=0x%p, thp=0x%p, "
	    "refcount=(%d)->(%d)\n", __func__, caller, (void *)ftp,
	    (void *)ftp->ft_hdl, ftp->ft_refcount, ftp->ft_refcount - 1);

	ASSERT((int32_t)ftp->ft_refcount > 0);

	if (--ftp->ft_refcount == 0) {
		fmd_dprintf(FMD_DBG_TOPO, "%s: freeing snapshot, ftp=0x%p, "
		    "thp=0x%p\n", __func__, (void *)ftp, (void *)ftp->ft_hdl);
		fmd_list_delete(&fmd.d_topo_list, ftp);
		topo_close(ftp->ft_hdl);
		fmd_free(ftp, sizeof (fmd_topo_t));
	}
}

fmd_topo_t *
fmd_topo_update(void)
{
	int err;
	topo_hdl_t *tp;
	fmd_topo_t *ftp, *prev;
	char *id;
	const char *name;

	(void) pthread_mutex_lock(&fmd.d_topo_lock);

	fmd_dprintf(FMD_DBG_TOPO, "%s: creating new snapshot\n", __func__);

	fmd.d_stats->ds_topo_drgen.fmds_value.ui64 = fmd_fmri_get_drgen();

	name = fmd.d_rootdir != NULL &&
	    *fmd.d_rootdir != '\0' ? fmd.d_rootdir : NULL;

	/*
	 * Create a new topology snapshot.
	 */
	if ((tp = topo_open(TOPO_VERSION, name, &err)) == NULL)
		fmd_panic("failed to open topology library: %s",
		    topo_strerror(err));
	ftp = fmd_zalloc(sizeof (fmd_topo_t), FMD_SLEEP);
	ftp->ft_hdl = tp;
	ftp->ft_time_begin = fmd_time_gethrtime();

	/*
	 * fmd(1M) snapshots have the side-effect of constructing/updating
	 * the Chassis Receptacle Occupant database (via di_cromk_begin
	 * di_cromk_recadd, di_cromk_end).
	 */
	id = topo_snap_hold_flag(tp, NULL, &err, TOPO_SNAP_HOLD_CROMK);
	if (id == NULL)
		fmd_panic("failed to get topology snapshot: %s",
		    topo_strerror(err));
	topo_hdl_strfree(tp, id);
	ftp->ft_time_end = fmd_time_gethrtime();

	fmd.d_stats->ds_topo_gen.fmds_value.ui64++;

	/*
	 * Set the reference count on the new snapshot to 1,
	 * release the previous snapshot, and add the new
	 * one to the head of the list.
	 */
	ftp->ft_refcount = 1;
	if ((prev = fmd_list_next(&fmd.d_topo_list)) != NULL)
		fmd_topo_rele_locked(prev, __func__);
	fmd_list_prepend(&fmd.d_topo_list, ftp);

	fmd_dprintf(FMD_DBG_TOPO, "%s: ftp=0x%p, thp=0x%p, uuid=%s, "
	    "time_begin=0x%llx, time_end=0x%llx, refcount=%d\n",
	    __func__, (void *)ftp, (void *)ftp->ft_hdl,
	    topo_hdl_uuid(ftp->ft_hdl), (u_longlong_t)ftp->ft_time_begin,
	    (u_longlong_t)ftp->ft_time_end, ftp->ft_refcount);

	(void) pthread_mutex_unlock(&fmd.d_topo_lock);

	/*
	 * Save the current snapshot.
	 */
	if (!FMD_DEBUG_TOPO_NO_SNAP_SAVE) {
		if (fmd_topo_save(ftp) == 0) {
			fmd_dprintf(FMD_DBG_TOPO,
			    "%s: saved snapshot\n", __func__);
		} else {
			fmd_dprintf(FMD_DBG_ERR | FMD_DBG_TOPO,
			    "%s: failed to save snapshot\n", __func__);
		}
	} else {
		fmd_dprintf(FMD_DBG_TOPO,
		    "%s: not saving snapshot\n", __func__);
	}

	return (ftp);
}

fmd_topo_t *
fmd_topo_hold(const char const *caller)
{
	fmd_topo_t *ftp;

	(void) pthread_mutex_lock(&fmd.d_topo_lock);
	ftp = fmd_list_next(&fmd.d_topo_list);

	fmd_dprintf(FMD_DBG_TOPO, "%s: %s: ftp=0x%p, thp=0x%p, "
	    "refcount=(%d)->(%d)\n", __func__, caller, (void *)ftp,
	    (void *)ftp->ft_hdl, ftp->ft_refcount, ftp->ft_refcount + 1);

	ftp->ft_refcount++;
	(void) pthread_mutex_unlock(&fmd.d_topo_lock);

	return (ftp);
}

void
fmd_topo_addref(fmd_topo_t *ftp, const char *caller)
{

	(void) pthread_mutex_lock(&fmd.d_topo_lock);

	fmd_dprintf(FMD_DBG_TOPO, "%s: %s: ftp=0x%p, thp=0x%p, "
	    "refcount=(%d)->(%d)\n", __func__, caller, (void *)ftp,
	    (void *)ftp->ft_hdl, ftp->ft_refcount, ftp->ft_refcount + 1);

	ftp->ft_refcount++;
	(void) pthread_mutex_unlock(&fmd.d_topo_lock);
}

void
fmd_topo_rele(fmd_topo_t *ftp, const char *caller)
{
	fmd_dprintf(FMD_DBG_TOPO, "%s: %s: ftp=0x%p, thp=0x%p, "
	    "refcount=(%d)->(%d)\n", __func__, caller, (void *)ftp,
	    (void *)ftp->ft_hdl, ftp->ft_refcount, ftp->ft_refcount - 1);

	(void) pthread_mutex_lock(&fmd.d_topo_lock);
	fmd_topo_rele_locked(ftp, __func__);
	(void) pthread_mutex_unlock(&fmd.d_topo_lock);
}

void
fmd_topo_rele_hdl(topo_hdl_t *thp, const char *caller)
{
	fmd_topo_t *ftp;

	(void) pthread_mutex_lock(&fmd.d_topo_lock);
	for (ftp = fmd_list_next(&fmd.d_topo_list); ftp != NULL;
	    ftp = fmd_list_next(ftp)) {
		if (ftp->ft_hdl == thp)
			break;
	}
	ASSERT(ftp != NULL);

	fmd_dprintf(FMD_DBG_TOPO, "%s: %s: ftp=0x%p, thp=0x%p, "
	    "refcount=(%d)->(%d)\n", __func__, caller, (void *)ftp,
	    (void *)thp, ftp->ft_refcount, ftp->ft_refcount - 1);

	fmd_topo_rele_locked(ftp, __func__);
	(void) pthread_mutex_unlock(&fmd.d_topo_lock);
}

void
fmd_topo_init(void)
{
	/*
	 * Load saved topology snapshot if one exists.
	 */
	if (!FMD_DEBUG_TOPO_NO_SNAP_LOAD) {
		fmd_dprintf(FMD_DBG_TOPO, "%s: loading previous snapshot\n",
		    __func__);
		if ((fmd.d_topo_previous =
		    fmd_topo_load(TOPO_SNAPSHOT_LINK_LATEST)) == NULL) {
			fmd_dprintf(FMD_DBG_ERR | FMD_DBG_TOPO,
			    "%s: failed to load previous snapshot\n",
			    __func__);
		}
	} else {
		fmd_dprintf(FMD_DBG_TOPO, "%s: not loading snapshot\n",
		    __func__);
	}

	/*
	 * Create current topology snapshot.
	 * If debug option is set, then look for and load a debug
	 * snapshot otherwise create a new snapshot as usual.
	 */
	if (FMD_DEBUG_TOPO_UPDATE_LOAD) {
		fmd_dprintf(FMD_DBG_TOPO, "%s: loading current snapshot\n",
		    __func__);
		fmd.d_topo_current = fmd_topo_load(TOPO_SNAPSHOT_LINK_INIT);
		if (fmd.d_topo_current == NULL) {
			fmd_dprintf(FMD_DBG_ERR | FMD_DBG_TOPO,
			    "%s: failed to load current snapshot\n",
			    __func__);
		}
	}
	if (fmd.d_topo_current == NULL) {
		fmd_dprintf(FMD_DBG_TOPO, "%s: creating current snapshot\n",
		    __func__);
		/*
		 * Add an extra reference to the previous snapshot
		 * so it won't be released by fmd_topo_update().
		 */
		if (fmd.d_topo_previous != NULL)
			fmd_topo_addref(fmd.d_topo_previous, __func__);
		if ((fmd.d_topo_current = fmd_topo_update()) == NULL) {
			fmd_dprintf(FMD_DBG_ERR | FMD_DBG_TOPO,
			    "%s: failed to create current snapshot\n",
			    __func__);
		}
	}

	if (fmd.d_topo_previous) {
		fmd_dprintf(FMD_DBG_TOPO, "%s: previous ftp=0x%p, "
		    "thp=0x%p, uuid=%s, refcount=%d\n", __func__,
		    (void *)fmd.d_topo_previous,
		    (void *)fmd.d_topo_previous->ft_hdl,
		    topo_hdl_uuid(fmd.d_topo_previous->ft_hdl),
		    fmd.d_topo_previous->ft_refcount);
	}
	if (fmd.d_topo_current) {
		fmd_dprintf(FMD_DBG_TOPO, "%s: current ftp=0x%p, "
		    "thp=0x%p, uuid=%s, refcount=%d\n", __func__,
		    (void *)fmd.d_topo_current,
		    (void *)fmd.d_topo_current->ft_hdl,
		    topo_hdl_uuid(fmd.d_topo_current->ft_hdl),
		    fmd.d_topo_current->ft_refcount);
	}
}

void
fmd_topo_cleanup(void)
{
	topo_cleanup();		/* fmd is exiting */
}

void
fmd_topo_fini(void)
{
	fmd_topo_t *ftp;

	(void) pthread_mutex_lock(&fmd.d_topo_lock);
	while ((ftp = fmd_list_next(&fmd.d_topo_list)) != NULL) {
		fmd_dprintf(FMD_DBG_TOPO, "%s: deleting snapshot, "
		    "ftp=0x%p, thp=0x%p, refcount=%d\n", __func__,
		    (void *)ftp, (void *)ftp->ft_hdl, ftp->ft_refcount);
		fmd_list_delete(&fmd.d_topo_list, ftp);
		topo_close(ftp->ft_hdl);
		fmd_free(ftp, sizeof (fmd_topo_t));
	}
	(void) pthread_mutex_unlock(&fmd.d_topo_lock);
}

/*
 * This function checks for for a saved snapshot by the
 * given name, and loads it in if it exists.
 */
fmd_topo_t *
fmd_topo_load(char *snap_dirname)
{
	int err;
	topo_hdl_t *tp;
	fmd_topo_t *ftp;
	fmd_timeval_t tod0, tod1;
	hrtime_t hrt0, hrt1;
	struct stat statbuf;
	const char *root_dir;
	char *uuid_str;
	char snap_dname[MAXPATHLEN];
	char *topo_dir;

	ASSERT(!FMD_DEBUG_TOPO_NO_SNAP_LOAD);

	/*
	 * Generate snapshot dir name and check for existance.
	 */

	(void) fmd_conf_getprop(fmd.d_conf, "topo.dir", &topo_dir);
	(void) snprintf(snap_dname, MAXPATHLEN, "%s/%s/%s",
	    (fmd.d_rootdir != NULL && *fmd.d_rootdir != '\0' ?
	    fmd.d_rootdir : "/"), topo_dir, snap_dirname);
	if (access(snap_dname, F_OK) != 0) {
		fmd_dprintf(FMD_DBG_TOPO, "%s: snapshot dir not found: %s\n",
		    __func__, snap_dname);
		return (NULL);
	}
	if (stat(snap_dname, &statbuf) != 0) {
		fmd_dprintf(FMD_DBG_TOPO, "%s: stat(%s) failed, errno=%d\n",
		    __func__, snap_dname, errno);
		return (NULL);
	} else {
		if ((statbuf.st_mode & S_IFMT) != S_IFDIR) {
			fmd_dprintf(FMD_DBG_TOPO, "%s: %s file type 0x%x is "
			    "not a directory\n", __func__, snap_dname,
			    (uint32_t)statbuf.st_mode & S_IFMT);
			return (NULL);
		}
	}

	/*
	 * Load the topology snapshot.
	 */

	fmd_dprintf(FMD_DBG_TOPO, "%s: loading snapshot dir: %s\n",
	    __func__, snap_dname);

	(void) pthread_mutex_lock(&fmd.d_topo_lock);

	root_dir = fmd.d_rootdir != NULL &&
	    *fmd.d_rootdir != '\0' ? fmd.d_rootdir : NULL;

	if ((tp = topo_open(TOPO_VERSION, root_dir, &err)) == NULL) {
		fmd_dprintf(FMD_DBG_ERR | FMD_DBG_TOPO,
		    "%s: failed to open topology library: %s\n",
		    __func__, topo_strerror(err));
		(void) pthread_mutex_unlock(&fmd.d_topo_lock);
		return (NULL);
	}

	ftp = fmd_zalloc(sizeof (fmd_topo_t), FMD_SLEEP);
	ftp->ft_hdl = tp;

	if ((uuid_str = topo_snap_load(tp, snap_dname, &err)) == NULL) {
		fmd_dprintf(FMD_DBG_ERR | FMD_DBG_TOPO,
		    "failed to get topology snapshot: %s", topo_strerror(err));
		(void) pthread_mutex_unlock(&fmd.d_topo_lock);
		return (NULL);
	}

	/*
	 * Convert snapshot timestamp to current hrtime,
	 * and set begin/end time to that.
	 */
	fmd_time_sync(&tod0, &hrt0, 1);
	tod1.ftv_sec = topo_hdl_timestamp(tp);
	tod1.ftv_nsec = 0;
	fmd_time_tod2hrt(hrt0, &tod0, &tod1, &hrt1);
	ftp->ft_time_begin = ftp->ft_time_end = hrt1;

	/*
	 * Set the reference count on the snapshot to 1,
	 * and add it to the head of the list.
	 */
	ftp->ft_refcount = 1;
	fmd_list_prepend(&fmd.d_topo_list, ftp);

	fmd_dprintf(FMD_DBG_TOPO, "%s: ftp=0x%p, thp=0x%p, uuid=%s, "
	    "time_begin=0x%llx, time_end=0x%llx, refcount=%d\n",
	    __func__, (void *)ftp, (void *)ftp->ft_hdl, uuid_str,
	    (u_longlong_t)ftp->ft_time_begin, (u_longlong_t)ftp->ft_time_end,
	    ftp->ft_refcount);

	(void) pthread_mutex_unlock(&fmd.d_topo_lock);
	topo_hdl_strfree(tp, uuid_str);

	return (ftp);
}

/*
 * Remove oldest snapshot in specified directory.
 */
static int
fmd_topo_snap_delete_oldest(char *topo_dname)
{
	DIR		*dirp;
	struct dirent	*dp;
	struct stat	statbuf;
	time_t		oldest_dir_mtime;
	char		*oldest_dir_name = NULL;
	char		dir_path[MAXPATHLEN];
	char		file_path[MAXPATHLEN];

	fmd_dprintf(FMD_DBG_TOPO, "%s: topo_dname=%s\n", __func__, topo_dname);

	if ((dirp = opendir(topo_dname)) == NULL) {
		fmd_dprintf(FMD_DBG_TOPO, "%s: couldn't open directory %s\n",
		    __func__, topo_dname);
		return (-1);
	}

	while ((dp = readdir(dirp)) != NULL) {
		if (dp->d_name[0] == '.')
			continue; /* skip "." and ".." and hidden files */
		(void) snprintf(dir_path, MAXPATHLEN, "%s/%s",
		    topo_dname, dp->d_name);
		if (lstat(dir_path, &statbuf) != 0) {
			fmd_dprintf(FMD_DBG_TOPO, "%s: stat failed\n",
			    __func__);
			continue;
		}
		if ((statbuf.st_mode & S_IFMT) == S_IFDIR) {
			if (oldest_dir_name == NULL) {
				oldest_dir_name = fmd_alloc(MAXNAMELEN,
				    FMD_SLEEP);
				(void) strncpy(oldest_dir_name, dp->d_name,
				    MAXNAMELEN);
				oldest_dir_mtime = statbuf.st_mtime;
			} else {
				if (statbuf.st_mtime < oldest_dir_mtime) {
					(void) strncpy(oldest_dir_name,
					    dp->d_name, MAXNAMELEN);
					oldest_dir_mtime = statbuf.st_mtime;
				}
			}
		}
	}

	(void) closedir(dirp);

	if (oldest_dir_name == NULL) {
		fmd_dprintf(FMD_DBG_TOPO, "%s: oldest dir not found\n",
		    __func__);
		return (-1);
	}

	/*
	 * Delete all files in the snapshot directory.
	 * Assume there are no subdirectories so we don't need to recurse.
	 */
	(void) snprintf(dir_path, MAXPATHLEN, "%s/%s",
	    topo_dname, oldest_dir_name);
	fmd_dprintf(FMD_DBG_TOPO, "%s: deleting dir=%s, mtime=0x%x(%s)\n",
	    __func__, dir_path, (uint32_t)oldest_dir_mtime,
	    ctime(&oldest_dir_mtime));
	fmd_free(oldest_dir_name, MAXNAMELEN);
	if ((dirp = opendir(dir_path)) == NULL) {
		fmd_dprintf(FMD_DBG_TOPO, "%s: couldn't open directory %s\n",
		    __func__, dir_path);
		return (-1);
	}
	while ((dp = readdir(dirp)) != NULL) {
		fmd_dprintf(FMD_DBG_TOPO, "%s: dir entry %s\n",
		    __func__, dp->d_name);
		if ((strcmp(dp->d_name, ".") == 0) ||
		    (strcmp(dp->d_name, "..") == 0))
			continue; /* skip "." and ".." */
		(void) snprintf(file_path, MAXPATHLEN, "%s/%s",
		    dir_path, dp->d_name);
		if (stat(file_path, &statbuf) != 0) {
			fmd_dprintf(FMD_DBG_TOPO, "%s: stat of %s failed\n",
			    __func__, file_path);
			(void) closedir(dirp);
			return (-1);
		}
		if ((statbuf.st_mode & S_IFMT) == S_IFREG) {
			fmd_dprintf(FMD_DBG_TOPO, "%s: unlinking file=%s\n",
			    __func__, file_path);
			if (unlink(file_path) != 0) {
				fmd_dprintf(FMD_DBG_ERR | FMD_DBG_TOPO,
				    "%s: failed to unlink file %s\n",
				    __func__, file_path);
				(void) closedir(dirp);
				return (-1);
			}
		} else {
			fmd_dprintf(FMD_DBG_TOPO, "%s: unexpected file "
			    "type, file=%s, type=0x%x\n", __func__, file_path,
			    (uint32_t)(statbuf.st_mode & S_IFMT));
			(void) closedir(dirp);
			return (-1);
		}
	}
	(void) closedir(dirp);

	/*
	 * Delete the snapshot directory itself.
	 */
	fmd_dprintf(FMD_DBG_TOPO, "%s: removing dir=%s\n", __func__, dir_path);
	if (rmdir(dir_path) != 0) {
		fmd_dprintf(FMD_DBG_ERR | FMD_DBG_TOPO,
		    "%s: failed to remove dir=%s errno=%s\n",
		    __func__, dir_path, strerror(errno));
		return (-1);
	}

	return (0);
}

/*
 * This function creates a snapshot directory based on UUID,
 * and saves a topo snapshot in it.
 */
int
fmd_topo_save(fmd_topo_t *ftp)
{
	FILE		*fp;
	topo_hdl_t	*thp;
	mode_t		topo_dirmode;
	DIR		*dirp;
	struct dirent	*dp;
	struct stat	statbuf;
	int		i, j, err = 0;
	int		topo_dirlim, topo_dircount = 0;
	char		*topo_dir;
	char		topo_dname[MAXPATHLEN];	/* snapshot base dir */
	char		snap_dname[MAXPATHLEN];	/* new snapshot subdir */
	char		snap_fname[MAXPATHLEN];	/* snapshot xml file(s) */
	char		snap_link[MAXPATHLEN];	/* symbolic link to new snap */
	char		snap_link_temp[MAXPATHLEN];
	char		path[MAXPATHLEN];
	const char	*rootdir;

	thp = ftp->ft_hdl;

	/*
	 * Create topo snapshot directory base name.
	 */
	rootdir = fmd.d_rootdir != NULL &&
	    *fmd.d_rootdir != '\0' ? fmd.d_rootdir : "/";
	(void) fmd_conf_getprop(fmd.d_conf, "topo.dir", &topo_dir);
	(void) snprintf(topo_dname, MAXPATHLEN, "%s/%s", rootdir, topo_dir);

	/* Get topo snapshot limit.  */
	(void) fmd_conf_getprop(fmd.d_conf, "topo.dirlim", &topo_dirlim);

	/* Create latest snapshot symbolic links names. */
	(void) snprintf(snap_link, MAXPATHLEN, "%s/%s", topo_dname,
	    TOPO_SNAPSHOT_LINK_LATEST);
	(void) snprintf(snap_link_temp, MAXPATHLEN, "%s/%s", topo_dname,
	    TOPO_SNAPSHOT_LINK_TEMP);

	/*
	 * If topo_dirlim is <= 0 don't save or delete any snapshots,
	 * but delete the sym link since it is no longer valid.
	 */
	if (topo_dirlim <= 0) {
		fmd_dprintf(FMD_DBG_TOPO, "%s: not saving snapshot, "
		    "topo_dirlim=0\n", __func__);
		(void) unlink(snap_link);
		return (0);
	}

	/*
	 * Create new topo snapshot.
	 * Only HC scheme is currently saved.
	 */
	(void) fmd_conf_getprop(fmd.d_conf, "topo.dirmode", &topo_dirmode);
	(void) snprintf(snap_dname, MAXPATHLEN, "%s/%s", topo_dname,
	    topo_hdl_uuid(thp));
	if (mkdir(snap_dname, topo_dirmode) < 0) {
		fmd_dprintf(FMD_DBG_ERR | FMD_DBG_TOPO, "%s: failed to "
		    "create snapshot directory: %s", __func__, snap_dname);
		return (-1);
	}
	(void) snprintf(snap_fname, MAXPATHLEN, "%s/%s-topology.xml",
	    snap_dname, FM_FMRI_SCHEME_HC);
	fmd_dprintf(FMD_DBG_TOPO, "%s: saving hc topo snapshot to %s\n",
	    __func__, snap_fname);
	if ((fp = fopen(snap_fname, "w")) == NULL) {
		fmd_dprintf(FMD_DBG_ERR | FMD_DBG_TOPO, "%s: failed to open "
		    "topology snapshot file: %s",
		    __func__, topo_strerror(err));
		return (-1);
	}
	if (topo_xml_print(thp, fp, FM_FMRI_SCHEME_HC, &err) < 0) {
		fmd_dprintf(FMD_DBG_TOPO, "%s: failed to save topology "
		    "snapshot: %s", __func__, topo_strerror(err));
		(void) fclose(fp);
		return (-1);
	}
	(void) fclose(fp);

	/*
	 * Remove the latest symbolic link and create
	 * one pointing to the new snapshot.
	 */
	if (symlink(topo_hdl_uuid(thp), snap_link_temp) != 0) {
		fmd_dprintf(FMD_DBG_ERR | FMD_DBG_TOPO,
		    "%s: failed to create symbolic link %s -> %s\n",
		    __func__, snap_link_temp, topo_hdl_uuid(thp));
		return (-1);
	}
	err = unlink(snap_link);
	if ((err != 0) && (errno != ENOENT)) {
		fmd_dprintf(FMD_DBG_ERR | FMD_DBG_TOPO,
		    "%s: failed to remove existing link=%s, errno=%d\n",
		    __func__, snap_link, err);
		return (-1);
	}
	if (rename(snap_link_temp, snap_link) != 0) {
		fmd_dprintf(FMD_DBG_ERR | FMD_DBG_TOPO,
		    "%s: failed to rename symbolic link %s to %s\n",
		    __func__, snap_link_temp, snap_link);
		return (-1);
	}
	fmd_dprintf(FMD_DBG_TOPO, "%s: created symlink %s -> %s\n",
	    __func__, snap_link, topo_hdl_uuid(thp));

	/*
	 * Count the number of existing snapshots.
	 */
	if ((dirp = opendir(topo_dname)) == NULL) {
		fmd_dprintf(FMD_DBG_TOPO, "%s: couldn't open directory %s\n",
		    __func__, topo_dname);
		return (0);
	}
	while ((dp = readdir(dirp)) != NULL) {
		if (dp->d_name[0] == '.')
			continue; /* skip "." and ".." and hidden files */
		(void) snprintf(path, MAXPATHLEN, "%s/%s",
		    topo_dname, dp->d_name);
		if (lstat(path, &statbuf) != 0) {
			fmd_dprintf(FMD_DBG_TOPO, "%s: lstat of %s failed\n",
			    __func__, path);
			continue;
		}
		fmd_dprintf(FMD_DBG_TOPO, "%s: file=%s, type=0x%x\n",
		    __func__, dp->d_name, (uint32_t)(statbuf.st_mode & S_IFMT));
		if ((statbuf.st_mode & S_IFMT) == S_IFDIR) {
			topo_dircount++;
			fmd_dprintf(FMD_DBG_TOPO, "%s: dir_name=%s\n",
			    __func__, dp->d_name);
		}
	}
	(void) closedir(dirp);

	fmd_dprintf(FMD_DBG_TOPO, "%s: topo_dircount=%d, topo_dirlim=%d\n",
	    __func__, topo_dircount, topo_dirlim);

	/*
	 * If we've exceeded the saved topo snapshot limit,
	 * remove snapshots until we are within the limit.
	 */
	if (topo_dircount > topo_dirlim) {
		j = topo_dircount - topo_dirlim;
		fmd_dprintf(FMD_DBG_TOPO, "%s: dir count exceeds limit, "
		    "removing oldest %d snapshots\n", __func__, j);
		for (i = 0; i < j; i++) {
			if (fmd_topo_snap_delete_oldest(topo_dname) != 0) {
				fmd_dprintf(FMD_DBG_ERR | FMD_DBG_TOPO,
				    "%s: failed to remove snapshot "
				    "directory(ies)", __func__);
				return (0);
			}
		}
	}

	return (0);
}
