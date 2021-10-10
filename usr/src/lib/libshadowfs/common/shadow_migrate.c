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
 * This file contains the infrastructure to migrate files and directories.
 */

#include "shadow_impl.h"

static size_t shadow_fid_load_max = 10000;

static int
shadow_add_entry(shadow_handle_t *shp, const char *path, const char *entry,
    shadow_type_t type, uint32_t depth, struct stat64 *statbuf)
{
	shadow_entry_t *sep;
	size_t len;
	struct statvfs64 vstat;

	if ((sep = shadow_zalloc(sizeof (shadow_entry_t))) == NULL)
		return (-1);

	if (entry == NULL) {
		if ((sep->se_path = shadow_strdup(path)) == NULL) {
			free(sep);
			return (-1);
		}
	} else {
		len = strlen(path) + strlen(entry) + 2;
		if ((sep->se_path = shadow_alloc(len)) == NULL) {
			free(sep);
			return (-1);
		}

		(void) snprintf(sep->se_path, len, "%s/%s", path, entry);
	}

	/*
	 * If this directory is part of a different filesystem, then stop the
	 * traversal rather than wasting time traversing the subdirectory.  The
	 * implementation of 'f_fsid' leaves something to be desired, but since
	 * this is just a suggestion, it's harmless if we're wrong.
	 */
	if (shp->sh_fsid != NODEV &&
	    statvfs64(sep->se_path, &vstat) == 0 &&
	    vstat.f_fsid != shp->sh_fsid) {
		free(sep->se_path);
		free(sep);
		return (0);
	}

	if (statbuf != NULL)
		sep->se_timestamp = statbuf->st_atim;
	sep->se_depth = depth;
	sep->se_type = type;

	(void) pthread_mutex_lock(&shp->sh_lock);
	if (shadow_pq_enqueue(&shp->sh_queue, sep) != 0) {
		(void) pthread_mutex_unlock(&shp->sh_lock);
		free(sep->se_path);
		free(sep);
		return (-1);
	}
	shadow_status_enqueue(shp, sep);
	(void) pthread_mutex_unlock(&shp->sh_lock);

	return (0);
}

static fid_t *
shadow_read_fidlist(const char *root, int idx, size_t *count)
{
	int fd;
	size_t retlen;
	struct stat64 statbuf;
	vfs_shadow_header_t header;
	fid_t *ret;
	size_t fidlen = sizeof (ret->un._fid);
	int i;
	char path[PATH_MAX];

	(void) snprintf(path, sizeof (path), "%s/%s/%s/%d", root,
	    VFS_SHADOW_PRIVATE_DIR, VFS_SHADOW_PRIVATE_PENDING,
	    idx);

	if ((fd = open(path, O_RDONLY)) < 0)
		return (NULL);

	if (fstat64(fd, &statbuf) != 0) {
		(void) close(fd);
		return (NULL);
	}

	if (statbuf.st_size < sizeof (vfs_shadow_header_t)) {
		(void) close(fd);
		return (NULL);
	}

	if (read(fd, &header, sizeof (header)) < sizeof (header)) {
		(void) close(fd);
		return (NULL);
	}

	if (header.vsh_magic != VFS_SHADOW_ATTR_LIST_MAGIC ||
	    header.vsh_version != VFS_SHADOW_INTENT_VERSION) {
		(void) close(fd);
		return (NULL);
	}

	/* XXX verify endianness */

	retlen = statbuf.st_size - sizeof (header);
	if (retlen % fidlen != 0) {
		(void) close(fd);
		return (NULL);
	}

	*count = retlen / fidlen;

	/*
	 * If the size of the pending lists exceeds a reasonable size, then
	 * bail out.  While we try to keep the FID lists short, there are times
	 * (such as when there are a large number of errors) when the lists
	 * grow very large.  If this is the case, then it's probably not worth
	 * trying to load and resume the migration from this list, and we're
	 * better off just loading the root directory and starting from
	 * scratch.
	 */
	if (*count > shadow_fid_load_max) {
		(void) close(fd);
		return (NULL);
	}

	if ((ret = shadow_alloc(*count * sizeof (fid_t))) == NULL) {
		(void) close(fd);
		return (NULL);
	}

	for (i = 0; i < *count; i++) {
		if (pread64(fd, ret + i, fidlen,
		    sizeof (header) + fidlen * i) != fidlen) {
			free(ret);
			(void) close(fd);
			return (NULL);
		}
	}

	(void) close(fd);
	return (ret);
}

typedef struct shadow_fid_entry {
	fid_t			sfe_fid;
	shadow_hash_link_t	sfe_link;
} shadow_fid_entry_t;

static const void *
shadow_fid_hash_convert(const void *p)
{
	const shadow_fid_entry_t *fep = p;

	return (&fep->sfe_fid);
}

static ulong_t
shadow_fid_hash_compute(const void *p)
{
	const fid_t *fidp = p;
	ulong_t hash = 0;
	int i;

	for (i = 0; i < fidp->fid_len; i++)
		hash += fidp->fid_data[i];

	return (hash);
}

static int
shadow_fid_hash_compare(const void *a, const void *b)
{
	const fid_t *fa = a;
	const fid_t *fb = b;

	if (fa->fid_len != fb->fid_len)
		return (-1);

	return (bcmp(fa->fid_data, fb->fid_data, fa->fid_len));
}

/*
 * Iterate over the given pending FID list and add entries for each item in the
 * list.
 */
static int
shadow_load_fidlist(shadow_handle_t *shp, shadow_hash_t *seen, int idx)
{
	fid_t *fids;
	size_t i, count, depth;
	char *buf, *newbuf, *slash;
	size_t buflen, mountlen, mountptlen;
	shadow_ioc_t ioc;
	int fd;
	struct stat64 statbuf;
	shadow_type_t type;
	int ret = -1;
	shadow_fid_entry_t *fep;

	if ((fids = shadow_read_fidlist(shp->sh_mountpoint, idx,
	    &count)) == NULL)
		return (0);

	if ((fd = open(shp->sh_mountpoint, O_RDONLY)) < 0) {
		free(fids);
		return (0);
	}

	if ((buf = shadow_alloc(PATH_MAX)) == NULL) {
		free(fids);
		(void) close(fd);
		return (-1);
	}

	mountptlen = strlen(shp->sh_mountpoint);

	buflen = PATH_MAX;
	ioc.si_buffer = (uint64_t)(uintptr_t)buf;
	ioc.si_length = buflen;

	mountlen = strlen(shp->sh_mountpoint);

	for (i = 0; i < count; i++) {
		ioc.si_fid = fids[i];

		if (ioc.si_fid.fid_len > MAXFIDSZ)
			continue;

		if (ioctl(fd, SHADOW_IOC_FID2PATH, &ioc) != 0) {
			if (errno == ENOMEM) {
				if ((newbuf = shadow_alloc(
				    buflen * 2)) == NULL) {
					goto error;
				}

				free(buf);
				buf = newbuf;
				buflen *= 2;
				i--;
			}
			continue;
		}

		if (buf[0] == '\0')
			continue;

		/*
		 * With two pending lists and the abilty for entries to appear
		 * multiple times in a pending list, we want to make sure we
		 * don't add the same entry twice.  For efficiency, we create a
		 * hash based on FID and ignore those we've already seen.
		 * Ideally, we'd like to avoid adding children if we've already
		 * added a parent (which would visit the same child twice), but
		 * this requires a more complicated data structure and should
		 * hopefully be a rare occurrence.
		 */
		if (shadow_hash_lookup(seen, &ioc.si_fid) != NULL)
			continue;

		if ((fep = shadow_alloc(sizeof (shadow_fid_entry_t))) == NULL)
			goto error;

		fep->sfe_fid = ioc.si_fid;
		shadow_hash_insert(seen, fep);

		/*
		 * If this is a relative path, it is the remote path and we
		 * should turn it into a guess at the absolute path.
		 */
		if (buf[0] != '/') {
			if (strlen(shp->sh_mountpoint) +
			    strlen(buf) + 2 > buflen) {
				if ((newbuf = shadow_alloc(
				    buflen * 2)) == NULL)
					goto error;

				free(buf);
				buf = newbuf;
				buflen *= 2;
				i--;
				continue;
			}

			(void) memmove(buf + mountlen + 1, buf,
			    strlen(buf) + 1);
			(void) memcpy(buf, shp->sh_mountpoint, mountlen);
			buf[mountlen] = '/';
		}

		if (strncmp(buf, shp->sh_mountpoint, mountlen) != 0)
			continue;

		/*
		 * When we first start migration, we have the root directory
		 * and its contents in the pending list.  As a special case to
		 * avoid looking at the entire hierarchy twice, we never add
		 * the root directory to the pending list.  If there is some
		 * error that is keeping the root directory from being
		 * migrated, we'll discover it when we process the pending
		 * list.
		 */
		if (buf[mountptlen] == '\0')
			continue;

		if (stat64(buf, &statbuf) != 0)
			continue;

		if (S_ISREG(statbuf.st_mode)) {
			type = SHADOW_TYPE_FILE;
		} else if (S_ISDIR(statbuf.st_mode)) {
			type = SHADOW_TYPE_DIR;
		} else {
			continue;
		}

		depth = 0;
		slash = buf + mountlen - 1;
		while ((slash = strchr(slash + 1, '/')) != NULL)
			depth++;

		if (shadow_add_entry(shp, buf, NULL, type, depth,
		    &statbuf) != 0)
			goto error;
	}

	ret = 0;
error:

	free(fids);
	free(buf);
	(void) close(fd);
	return (ret);
}

/*
 * This function is responsible for adding the initial directories to the list.
 * In order to allow us to resume a previous migration, we make the assumption
 * that the filesystem is largely static, and the remote paths are likely the
 * same as the local ones.  By this token, we can iterate over the pending list
 * and lookup the remote path for those FIDs that are not yet migrated.  As an
 * extra check, we also look at the vnode path information as a second source
 * of possible path information.  If everything fails, then we fall back to
 * processing the FID list individually.  While not ideal, it gets the job
 * done.  This is done asynchronously to the open, when the first migration is
 * attempted.  Because we don't want to block reading the FID list when mounted
 * in standby mode, we return an error if we're currently in standby mode.
 */
static int
shadow_begin(shadow_handle_t *shp)
{
	FILE *mnttab = NULL;
	struct mnttab mntent, search;
	char *mntopt;
	shadow_hash_t *seen;
	shadow_fid_entry_t *fep;
	int ret;

	if ((mnttab = fopen(MNTTAB, "r")) == NULL)
		return (shadow_error(ESHADOW_NOMOUNT,
		    dgettext(TEXT_DOMAIN, "failed to open /etc/mnttab")));

	bzero(&search, sizeof (search));
	search.mnt_mountp = shp->sh_mountpoint;
	if (getmntany(mnttab, &mntent, &search) != 0) {
		/* shouldn't happen */
		(void) fclose(mnttab);
		return (shadow_error(ESHADOW_NOMOUNT,
		    dgettext(TEXT_DOMAIN, "no such mountpoint %s"),
		    shp->sh_mountpoint));
	}

	if ((mntopt = hasmntopt(&mntent, "shadow")) != NULL &&
	    strncmp(mntopt, "shadow=standby", 14) == 0) {
		(void) fclose(mnttab);
		return (shadow_error(ESHADOW_STANDBY,
		    dgettext(TEXT_DOMAIN, "filesystem currently in standby")));
	}

	(void) fclose(mnttab);

	if ((seen = shadow_hash_create(offsetof(shadow_fid_entry_t, sfe_link),
	    shadow_fid_hash_convert, shadow_fid_hash_compute,
	    shadow_fid_hash_compare)) == NULL)
		return (-1);

	ret = 0;
	if (shadow_load_fidlist(shp, seen, 0) != 0 ||
	    shadow_load_fidlist(shp, seen, 1) != 0)
		ret = -1;

	while ((fep = shadow_hash_first(seen)) != NULL) {
		shadow_hash_remove(seen, fep);
		free(fep);
	}
	shadow_hash_destroy(seen);

	if (shadow_pq_peek(&shp->sh_queue) == NULL)
		(void) shadow_add_entry(shp, shp->sh_mountpoint, NULL,
		    SHADOW_TYPE_DIR, 0, NULL);

	return (ret);
}

/*
 * This function will go and load the pending FID list, if necessary.  It
 * returns with the sh_lock held on success
 */
static int
shadow_check_begin(shadow_handle_t *shp)
{
	(void) pthread_mutex_lock(&shp->sh_lock);
	if (!shp->sh_loaded) {
		if (shp->sh_loading) {
			(void) pthread_mutex_unlock(&shp->sh_lock);
			return (shadow_error(ESHADOW_MIGRATE_BUSY,
			    dgettext(TEXT_DOMAIN,
			    "pending FID list is currently being loaded")));
		}

		shp->sh_loading = B_TRUE;
		(void) pthread_mutex_unlock(&shp->sh_lock);

		if (shadow_begin(shp) != 0) {
			(void) pthread_mutex_lock(&shp->sh_lock);
			shp->sh_loading = B_FALSE;
			(void) pthread_mutex_unlock(&shp->sh_lock);
			return (-1);
		}

		(void) pthread_mutex_lock(&shp->sh_lock);
	}

	shp->sh_loaded = B_TRUE;
	shp->sh_loading = B_FALSE;

	return (0);
}

/*
 * This function is called during shadow_close() and is responsible for
 * removing all items from the work queue and freeing up any errors seen.
 */
void
shadow_end(shadow_handle_t *shp)
{
	shadow_entry_t *sep;
	shadow_error_t *srp;

	while ((sep = shadow_pq_dequeue(&shp->sh_queue)) != NULL) {
		free(sep->se_path);
		free(sep);
	}

	while ((srp = shp->sh_errors) != NULL) {
		shp->sh_errors = srp->se_next;
		free(srp);
	}
}

/*
 * Record an error against the given path.  We first check to see if it's a
 * known error, returning if it is.  Otherwise, we create an entry in the error
 * list and record the relevant information.
 */
static int
shadow_error_record(shadow_handle_t *shp, const char *path, int err)
{
	shadow_error_t *sep;

	(void) pthread_mutex_lock(&shp->sh_errlock);
	for (sep = shp->sh_errors; sep != NULL; sep = sep->se_next) {
		if (strcmp(sep->se_path, path) == 0) {
			sep->se_error = err;
			break;
		}
	}

	if (sep == NULL) {
		if ((sep = shadow_zalloc(sizeof (shadow_error_t))) == NULL) {
			(void) pthread_mutex_unlock(&shp->sh_errlock);
			return (-1);
		}

		if ((sep->se_path = shadow_strdup(path)) == NULL) {
			(void) pthread_mutex_unlock(&shp->sh_errlock);
			free(sep);
			return (-1);
		}

		sep->se_error = err;
		sep->se_next = shp->sh_errors;
		shp->sh_errors = sep;
		shp->sh_errcount++;
	}

	(void) pthread_mutex_unlock(&shp->sh_errlock);
	return (0);
}

/*
 * Called when migration fails for a file or directory.  In this case, we
 * consult the kernel to get the remote path for the object.  If this fails,
 * then we assume it's a local error and don't record the failure.  If it
 * succeeds, it indicates there was a problem with the remote side, and we do
 * record the error.
 */
static int
shadow_error_check(shadow_handle_t *shp, const char *localpath, int err)
{
	char path[PATH_MAX];
	shadow_ioc_t ioc;
	int fd;

	if (err == 0 || err == EINTR)
		return (0);

	if ((fd = open(localpath, O_RDONLY)) < 0)
		return (0);

	bzero(&ioc, sizeof (ioc));
	ioc.si_buffer = (uint64_t)(uintptr_t)path;
	ioc.si_length = sizeof (path);

	if (ioctl(fd, SHADOW_IOC_GETPATH, &ioc) == 0 && ioc.si_processed) {
		path[PATH_MAX - 1] = '\0';
		if (shadow_error_record(shp, path, err) != 0) {
			(void) close(fd);
			return (-1);
		}
	}

	(void) close(fd);
	return (0);
}

/*
 * Internal function to calculate priority within the pending queue.  This is
 * based primarily on the the directory depth, as we want to proceed
 * depth-first in order to minimize the size of our pending list.  We also bias
 * towards the most recently accessed entries, under the assumption that they
 * are more likely to be accessed again.
 */
uint64_t
shadow_priority(const void *data)
{
	uint64_t depth, priority;
	const shadow_entry_t *sep = data;

	/*
	 * We have only 64 bits of identifiers, and a complete timestamp could
	 * potentially take up this entire value.  Instead, we carve 16 high
	 * order bits for the depth, and then squeeze the timestamp into the
	 * remaining bits.  This may lose some nanosecond accuracy, but this
	 * won't make a significant difference in the overall functioning of
	 * the algorithm.
	 */
	depth = MIN(sep->se_depth, 0xFFFF);

	priority = (uint64_t)sep->se_timestamp.tv_sec * NANOSEC +
	    sep->se_timestamp.tv_nsec;
	priority = (priority >> 16) | (depth << 48);

	/*
	 * At this point the highest value represents the highest priority, but
	 * priority queues are based on the lowest value being the highest
	 * priority.  We invert the value here to achieve this.
	 */
	return (~priority);
}

/*
 * The actual migration is done through the SHADOW_IOC_MIGRATE ioctl().
 * Normally, all migration errors are converted into the generic EIO error so
 * as not to confuse consumers.  For data reporting purposes, however, we want
 * to get the real error.
 */
static int
shadow_migrate_fd(int fd, uint64_t *size)
{
	shadow_ioc_t ioc;

	bzero(&ioc, sizeof (ioc));

	if (ioctl(fd, SHADOW_IOC_MIGRATE, &ioc) != 0)
		return (errno);

	if (size != NULL)
		*size = ioc.si_size;

	return (ioc.si_error);
}

/*
 * Migrate a directory.
 */
static int
shadow_migrate_dir(shadow_handle_t *shp, shadow_entry_t *sep, int *errp)
{
	DIR *dirp;
	struct dirent *dp;
	struct stat64 statbuf;
	int fd;
	shadow_type_t type;
	uint64_t subdirs, size;

	if ((fd = open(sep->se_path, O_RDONLY)) < 0)
		return (0);

	if ((*errp = shadow_migrate_fd(fd, &size)) != 0) {
		(void) close(fd);
		return (0);
	}

	if ((dirp = fdopendir(fd)) == NULL) {
		(void) close(fd);
		return (0);
	}

	subdirs = 0;
	errno = 0;
	while ((dp = readdir(dirp)) != NULL) {
		if (strcmp(dp->d_name, ".") == 0 ||
		    strcmp(dp->d_name, "..") == 0)
			continue;

		if (strcmp(sep->se_path, shp->sh_mountpoint) == 0) {
			/*
			 * Skip the .SUNWshadow private directory.
			 */
			if (strcmp(dp->d_name, VFS_SHADOW_PRIVATE_DIR) == 0)
				continue;

			/*
			 * Skip .zfs if this is a ZFS filesystem and it's
			 * visible.
			 */
			if (shp->sh_dataset != NULL &&
			    strcmp(dp->d_name, ".zfs") == 0)
				continue;
		}

		if (fstatat64(fd, dp->d_name, &statbuf,
		    AT_SYMLINK_NOFOLLOW) != 0) {
			errno = 0;
			continue;
		}

		if (S_ISREG(statbuf.st_mode)) {
			type = SHADOW_TYPE_FILE;
		} else if (S_ISDIR(statbuf.st_mode)) {
			type = SHADOW_TYPE_DIR;
			subdirs++;
		} else {
			continue;
		}

		if (shadow_add_entry(shp, sep->se_path, dp->d_name,
		    type, sep->se_depth + 1, &statbuf) != 0) {
			(void) closedir(dirp);
			return (-1);
		}
	}

	(void) closedir(dirp);

	if (errno == 0) {
		(void) pthread_mutex_lock(&shp->sh_lock);
		shadow_status_update(shp, sep, size, subdirs);
		(void) pthread_mutex_unlock(&shp->sh_lock);
	}

	return (0);
}

/*
 * Migrate a file.
 */
/*ARGSUSED*/
static int
shadow_migrate_file(shadow_handle_t *shp, shadow_entry_t *sep, int *errp)
{
	int fd;
	uint64_t size;

	if ((fd = open64(sep->se_path, O_RDONLY)) < 0)
		return (0);

	if ((*errp = shadow_migrate_fd(fd, &size)) != 0) {
		(void) close(fd);
		return (0);
	}

	(void) close(fd);

	(void) pthread_mutex_lock(&shp->sh_lock);
	shadow_status_update(shp, sep, size, 0);
	(void) pthread_mutex_unlock(&shp->sh_lock);

	return (0);
}

/*
 * This function processes one entry from the on-disk pending list.  This
 * function can fail with ESHADOW_MIGRATE_DONE if there are no entries left to
 * process.  This is called with the lock held.
 */
static int
shadow_process_pending(shadow_handle_t *shp)
{
	shadow_ioc_t ioc;
	int fd;
	char path[PATH_MAX];

	if (shp->sh_complete)
		return (shadow_set_errno(ESHADOW_MIGRATE_DONE));

	shp->sh_active++;
	(void) pthread_mutex_unlock(&shp->sh_lock);

	/*
	 * This should never fail, but if it does just ignore the error and let
	 * the client try again.
	 */
	if ((fd = open(shp->sh_mountpoint, O_RDONLY)) < 0)
		goto error;

	bzero(&ioc, sizeof (ioc));
	ioc.si_buffer = (uint64_t)(uintptr_t)path;
	ioc.si_length = sizeof (path);
	if (ioctl(fd, SHADOW_IOC_PROCESS, &ioc) != 0) {
		(void) close(fd);
		goto error;
	}
	(void) close(fd);

	(void) pthread_mutex_lock(&shp->sh_lock);
	shp->sh_active--;
	shp->sh_onlyerrors = (boolean_t)ioc.si_onlyerrors;
	if (!ioc.si_processed) {
		shp->sh_complete = B_TRUE;
		return (shadow_set_errno(ESHADOW_MIGRATE_DONE));
	} else if (!ioc.si_error) {
		shadow_status_update(shp, NULL, ioc.si_size, 0);
		return (0);
	} else if (ioc.si_error == EINTR) {
		return (shadow_set_errno(ESHADOW_MIGRATE_INTR));
	} else {
		path[PATH_MAX - 1] = '\0';
		return (shadow_error_record(shp, path, ioc.si_error));
	}

error:
	(void) pthread_mutex_lock(&shp->sh_lock);
	shp->sh_active--;
	return (shadow_error(ESHADOW_CORRUPT,
	    dgettext(TEXT_DOMAIN, "unable to process pending list")));
}

typedef struct shadow_cleanup_arg {
	shadow_handle_t		*sca_hdl;
	shadow_entry_t		*sca_entry;
	boolean_t		sca_cleanup;
} shadow_cleanup_arg_t;

static void
shadow_migrate_cleanup(void *arg)
{
	shadow_cleanup_arg_t *sca = arg;
	shadow_handle_t *shp = sca->sca_hdl;
	shadow_entry_t *sep = sca->sca_entry;

	(void) pthread_mutex_lock(&shp->sh_lock);
	if (sca->sca_cleanup) {
		/*
		 * If the enqueue itself fails, we'll still be safe because of
		 * the on-disk pending list.  This can theoretically stomp on
		 * the previous error, but the only way either operation can
		 * fail is with ENOMEM.
		 */
		if (shadow_pq_enqueue(&shp->sh_queue, sep) == 0)
			shadow_status_enqueue(shp, sep);
	}
	shp->sh_active--;
	(void) pthread_mutex_unlock(&shp->sh_lock);
}

/*
 * Primary entry point for migrating a file or directory.  The caller is
 * responsible for controlling how often this function is called and by how
 * many threads.  This pulls an entry of the pending list, and processes it
 * appropriately.
 *
 * This function can return ESHADOW_MIGRATE_BUSY if all possible threads are
 * busy processing data, or ESHADOW_MIGRATE_DONE if the filesystem is done
 * being migrated.
 */
int
shadow_migrate_one(shadow_handle_t *shp)
{
	shadow_entry_t *sep;
	int ret, err;
	struct timespec ts;
	shadow_cleanup_arg_t arg;

	if (shadow_check_begin(shp) != 0)
		return (-1);

	sep = shadow_pq_dequeue(&shp->sh_queue);
	if (sep == NULL) {
		if (shp->sh_active != 0) {
			(void) pthread_mutex_unlock(&shp->sh_lock);
			return (shadow_error(ESHADOW_MIGRATE_BUSY,
			    dgettext(TEXT_DOMAIN,
			    "all entries are actively being processed")));
		} else {
			ret = shadow_process_pending(shp);
			(void) pthread_mutex_unlock(&shp->sh_lock);
			return (ret);
		}
	}
	shp->sh_active++;
	(void) pthread_mutex_unlock(&shp->sh_lock);

	arg.sca_hdl = shp;
	arg.sca_entry = sep;
	arg.sca_cleanup = B_TRUE;
	pthread_cleanup_push(shadow_migrate_cleanup, &arg);

	/*
	 * Debugging tool to allow simulation of ESHADOW_MIGRATE_BUSY.  The
	 * delay is specified in milliseconds.
	 */
	if (shp->sh_delay != 0) {
		ts.tv_sec = shp->sh_delay / 1000;
		ts.tv_sec = (shp->sh_delay % 1000) * 1000 * 1000;

		(void) nanosleep(&ts, NULL);
	}

	err = 0;
	switch (sep->se_type) {
	case SHADOW_TYPE_DIR:
		ret = shadow_migrate_dir(shp, sep, &err);
		break;

	case SHADOW_TYPE_FILE:
		ret = shadow_migrate_file(shp, sep, &err);
		break;

	default:
		assert(0);
	}

	(void) pthread_mutex_lock(&shp->sh_lock);

	if (err == EWOULDBLOCK) {
		/*
		 * This indicates that the filesystem is mounted in standby
		 * mode.  If this is the case, return an error, which will
		 * cause the consumer to retry at a later point (or move onto
		 * other filesystems).
		 */
		(void) shadow_pq_enqueue(&shp->sh_queue, sep);
		(void) pthread_mutex_unlock(&shp->sh_lock);

		return (shadow_error(ESHADOW_STANDBY,
		    dgettext(TEXT_DOMAIN,
		    "filesystem currently mounted in standby mode")));
	}

	shadow_status_dequeue(shp, sep);
	(void) pthread_mutex_unlock(&shp->sh_lock);

	/*
	 * The above functions can only fail if there is a library error (such
	 * as out-of-memory conditions).  In this case we should put it back in
	 * our queue.  If there was an I/O error or kernel level problem, we'll
	 * rely on the shadow pending queue to pick up the file later as part
	 * of the cleanup phase.  The exception is EINTR, where we know we
	 * should retry the migration.
	 */
	if (ret == 0) {
		ret = shadow_error_check(shp, sep->se_path, err);

		if (err != EINTR) {
			arg.sca_cleanup = B_FALSE;
			free(sep->se_path);
			free(sep);
		}
	}

	pthread_cleanup_pop(B_TRUE);

	return (ret);
}

/*
 * Returns true if this filesystem has finished being migrated.
 */
boolean_t
shadow_migrate_done(shadow_handle_t *shp)
{
	return (shp->sh_complete);
}

/*
 * Returns true if there are only files with persistent errors left to migrate.
 * These errors may still be fixed by the user, so consumers should use this
 * information to process entries less aggressively.
 */
boolean_t
shadow_migrate_only_errors(shadow_handle_t *shp)
{
	return (shp->sh_onlyerrors);
}

/*
 * This is a debugging tool that allows applications to dump out the current
 * pending list or otherwise manipulate it.  Because it's only for debugging
 * purposes, it can leave the pending list in an arbitrary invalid state is
 * something fails (i.e. memory allocation).
 */
int
shadow_migrate_iter(shadow_handle_t *shp, void (*func)(const char *, void *),
    void *data)
{
	shadow_entry_t *sep;
	shadow_pq_t copy;

	if (shadow_check_begin(shp) != 0)
		return (-1);

	if (shadow_pq_init(&copy, shadow_priority) != 0) {
		(void) pthread_mutex_unlock(&shp->sh_lock);
		return (-1);
	}

	while ((sep = shadow_pq_dequeue(&shp->sh_queue)) != NULL) {
		if (shadow_pq_enqueue(&copy, sep) != 0) {
			free(sep->se_path);
			free(sep);
			goto error;
		}

		func(sep->se_path, data);
	}

error:
	while ((sep = shadow_pq_dequeue(&copy)) != NULL) {
		if (shadow_pq_enqueue(&shp->sh_queue, sep) != 0) {
			free(sep->se_path);
			free(sep);
		}
	}
	shadow_pq_fini(&copy);
	(void) pthread_mutex_unlock(&shp->sh_lock);

	return (0);
}

/*
 * Cleanup after a completed shadow migration.  This is identical to
 * shadow_cancel() except that it verifies that the migration is complete.
 */
int
shadow_migrate_finalize(shadow_handle_t *shp)
{
	if (!shadow_migrate_done(shp))
		return (shadow_error(ESHADOW_MIGRATE_BUSY,
		    dgettext(TEXT_DOMAIN, "migration is not complete")));

	return (shadow_cancel(shp));
}

/*
 * This is a debugging-only tool that makes it easier to simulate
 * ESHADOW_MIGRATE_BUSY by suspending shadow_migrate_one() before migrating the
 * file or directory.  This should not be used by production software - if
 * there needs to be throttling done, it should be implemented by the caller
 * invoking shadow_migrate_one() on a less frequent basis.  The delay is
 * specified in milliseconds.
 */
void
shadow_migrate_delay(shadow_handle_t *shp, uint32_t delay)
{
	shp->sh_delay = delay;
}
