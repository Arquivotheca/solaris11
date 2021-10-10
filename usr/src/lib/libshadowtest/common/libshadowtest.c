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

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/fs/shadow.h>
#include <sys/mnttab.h>
#include <sys/stat.h>
#include <unistd.h>

#include <shadowtest.h>

static int g_ctlfd;

static int
st_error(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);

	(void) vfprintf(stderr, fmt, ap);

	if (strchr(fmt, '\n') == NULL)
		(void) fprintf(stderr, ": %s\n", strerror(errno));

	va_end(ap);

	return (-1);
}

/*
 * Given the root of the filesystem, open the pending FID list and return an
 * array of fid_t structures.
 */
static fid_t *
st_read_fidlist(const char *root, int idx, size_t *count)
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

	if ((fd = open(path, O_RDONLY)) < 0) {
		(void) st_error("failed to open %s",  path);
		return (NULL);
	}

	if (fstat64(fd, &statbuf) != 0) {
		(void) st_error("failed to stat %s", path);
		(void) close(fd);
		return (NULL);
	}

	if (statbuf.st_size < sizeof (vfs_shadow_header_t)) {
		(void) st_error("header of %s too short\n", path);
		(void) close(fd);
		return (NULL);
	}

	if (read(fd, &header, sizeof (header)) < sizeof (header)) {
		(void) st_error("failed to read header from %s\n", path);
		(void) close(fd);
		return (NULL);
	}

	if (header.vsh_magic != VFS_SHADOW_ATTR_LIST_MAGIC ||
	    header.vsh_version != VFS_SHADOW_INTENT_VERSION) {
		(void) st_error("invalid header from %s\n", path);
		(void) close(fd);
		return (NULL);
	}

	/* XXX verify endianness */

	retlen = statbuf.st_size - sizeof (header);
	if (retlen % fidlen != 0) {
		(void) st_error("misaligned fid list (%d %% %d) in %s\n",
		    retlen, fidlen, path);
		(void) close(fd);
		return (NULL);
	}

	*count = retlen / fidlen;

	if ((ret = malloc(*count * sizeof (fid_t))) == NULL) {
		(void) st_error("allocation failed");
		(void) close(fd);
		return (NULL);
	}

	for (i = 0; i < *count; i++) {
		if (pread64(fd, ret + i, fidlen,
		    sizeof (header) + fidlen * i) != fidlen) {
			(void) st_error("failed to read fid list from %s\n",
			    path);
			free(ret);
			(void) close(fd);
			return (NULL);
		}
	}

	(void) close(fd);
	return (ret);
}

/*
 * Given a path to file or directory, return the root of the filesystem
 * containing the given object.
 */
static int
st_get_root(const char *path, char *root, size_t rootlen)
{
	char fullpath[PATH_MAX];
	struct mnttab mntent;
	size_t len, maxlen;
	int ret;
	FILE *fp;

	if ((ret = resolvepath(path, fullpath, sizeof (fullpath))) < 0)
		return (st_error("failed to resolve %s", path));
	fullpath[ret] = '\0';

	if ((fp = fopen(MNTTAB, "r")) == NULL)
		return (st_error("failed to open /etc/mnttab"));

	maxlen = 0;
	while (getmntent(fp, &mntent) == 0) {
		len = strlen(mntent.mnt_mountp);

		if (len <= maxlen)
			continue;

		if (strcmp(mntent.mnt_mountp, "/") == 0 ||
		    (strncmp(mntent.mnt_mountp, fullpath, len) == 0 &&
		    (fullpath[len] == '/' || fullpath[len] == '\0'))) {
			(void) strlcpy(root, mntent.mnt_mountp,
			    rootlen);
			maxlen = len;
		}
	}

	(void) fclose(fp);

	if (maxlen == 0)
		return (st_error("failed to find fw root for %s", fullpath));

	return (0);
}

int
st_get_fid(const char *path, fid_t *fidp)
{
	int fd;
	st_cmd_t cmd;

	if ((fd = open(path, O_RDONLY)) < 0)
		return (st_error("failed to open %s", path));

	cmd.stc_fd = fd;

	if (ioctl(g_ctlfd, ST_IOC_GETFID, &cmd) != 0) {
		(void) close(fd);
		return (st_error("failed to get fid for %s", path));
	}

	(void) close(fd);
	bcopy(&cmd.stc_fid, fidp, sizeof (fid_t));
	return (0);
}

int
st_migrate_kthread(const char *path)
{
	int fd;
	st_cmd_t cmd;

	if ((fd = open(path, O_RDONLY)) < 0)
		return (st_error("failed to open %s", path));

	cmd.stc_fd = fd;

	if (ioctl(g_ctlfd, ST_IOC_KTHREAD, &cmd) != 0) {
		(void) close(fd);
		return (st_error("failed to migrate %s", path));
	}

	(void) close(fd);
	return (0);
}

/*
 * Verify that the pending FID list matches the list of files passed as
 * parameters.  The 'idx' parameter controls which list should be checked (0 or
 * 1).
 */
int
st_verify_pending(int idx, int argc, char **argv)
{
	char root[PATH_MAX];
	int i, j;
	fid_t *fids, fid;
	size_t nfid;
	boolean_t *matched;

	if (argc == 0)
		return (st_error("no arguments passed to "
		    "st_verify_pending()\n"));

	/*
	 * Determine the root of the filesystem from the first argument.  If
	 * the user specifies files from multiple filesystems - it's their own
	 * fault.  The VOP_FID() call will fail in this case.
	 */
	if (st_get_root(argv[0], root, sizeof (root)) != 0)
		return (-1);

	if ((fids = st_read_fidlist(root, idx, &nfid)) == NULL)
		return (-1);

	if (nfid != argc) {
		free(fids);
		return (st_error("pending list has %d entries, expecting %d\n",
		    nfid, argc));
	}

	if (nfid == 0) {
		free(fids);
		return (0);
	}

	/*
	 * The order in which entries are stored in the fidlist are arbitrary,
	 * so we need to have a separate array to track matches, and make sure
	 * we see every FID exactly once.
	 */
	if ((matched = calloc(nfid * sizeof (boolean_t), 1)) == NULL) {
		free(fids);
		return (st_error("allocation failed"));
	}

	for (i = 0; i < argc; i++) {
		if (st_get_fid(argv[i], &fid) != 0) {
			free(fids);
			free(matched);
			return (-1);
		}

		for (j = 0; j < argc; j++) {
			if (fid.fid_len == fids[j].fid_len &&
			    bcmp(fid.fid_data, fids[j].fid_data,
			    fid.fid_len) == 0) {
				matched[j] = B_TRUE;
				break;
			}
		}

		if (j == argc) {
			free(fids);
			free(matched);
			return (st_error("failed to find %s on fid list\n",
			    argv[i]));
		}

	}

	free(fids);

	for (j = 0; j < argc; j++) {
		if (!matched[j]) {
			free(matched);
			return (st_error("fid %d not found, multiple entries "
			    "for same object present\n"));
		}
	}

	free(matched);

	return (0);
}

/*
 * Verify that the pending list is empty.
 */
int
st_verify_pending_empty(int idx, const char *path)
{
	char root[PATH_MAX];
	fid_t *fids;
	size_t nfid;

	if (st_get_root(path, root, sizeof (root)) != 0)
		return (-1);

	if ((fids = st_read_fidlist(root, idx, &nfid)) == NULL)
		return (-1);

	if (nfid != 0) {
		free(fids);
		return (st_error("pending list has %d entries, expecting 0\n",
		    nfid));
	}

	return (0);
}

/*
 * Suspend or resume execution of any asynchronous rotation of pending FID
 * lists.
 */
int
st_suspend(void)
{
	st_cmd_t cmd;

	if (ioctl(g_ctlfd, ST_IOC_SUSPEND, &cmd) != 0)
		return (st_error("failed to suspend rotation"));

	return (0);
}

int
st_resume(void)
{
	st_cmd_t cmd;

	if (ioctl(g_ctlfd, ST_IOC_RESUME, &cmd) != 0)
		return (st_error("failed to suspend rotation"));

	return (0);
}

/*
 * Manually rotate the pending FID list.  This is only guaranteed to work if
 * st_suspend() has been called.
 */
int
st_rotate(const char *path)
{
	int fd;
	st_cmd_t cmd;

	if ((fd = open(path, O_RDONLY)) < 0)
		return (st_error("failed to open %s", path));

	cmd.stc_fd = fd;

	if (ioctl(g_ctlfd, ST_IOC_ROTATE, &cmd) != 0) {
		(void) close(fd);
		return (st_error("failed to rotate log"));
	}

	(void) close(fd);
	return (0);
}

/*
 * Manually set a shadow mount to spin waiting for a signal.  Can be used to
 * test EINTR handling.
 */
int
st_spin(const char *path, boolean_t enable)
{
	int fd;
	st_cmd_t cmd;

	if ((fd = open(path, O_RDONLY)) < 0)
		return (st_error("failed to open %s", path));

	cmd.stc_fd = fd;
	cmd.stc_op = enable;

	if (ioctl(g_ctlfd, ST_IOC_SPIN, &cmd) != 0) {
		(void) close(fd);
		return (st_error("failed to set spin mode"));
	}

	(void) close(fd);
	return (0);
}

/*
 * Overrides the credentials used when migrating file contents so that we can
 * test various failure modes.
 */
int
st_cred_set(void)
{
	st_cmd_t cmd;

	if (ioctl(g_ctlfd, ST_IOC_CRED_SET, &cmd) != 0)
		return (st_error("failed to set cred"));

	return (0);
}

int
st_cred_clear(void)
{
	st_cmd_t cmd;

	if (ioctl(g_ctlfd, ST_IOC_CRED_CLEAR, &cmd) != 0)
		return (st_error("failed to set cred"));

	return (0);
}

/*
 * Iterate over all entries in the space map and invoke the callback for each
 * one.
 */
int
st_iter_space_map(const char *path,
    void (*callback)(int, uint64_t, uint64_t, void *), void *data)
{
	int fd, map;
	vfs_shadow_header_t hdr;
	vfs_shadow_range_record_t rec;

	if ((fd = open(path, O_RDONLY)) < 0) {
		(void) st_error("failed to open %s", path);
		return (-1);
	}

	if ((map = openat(fd, "SUNWshadow.map", O_XATTR | O_RDONLY)) < 0) {
		(void) st_error("failed to open SUNWshadow.map "
		    "attribute on %s", path);
		(void) close(fd);
		return (-1);
	}

	(void) close(fd);

	if (read(map, &hdr, sizeof (hdr)) != sizeof (hdr)) {
		(void) st_error("failed to read space map header");
		(void) close(map);
		return (-1);
	}

	if (hdr.vsh_magic != VFS_SHADOW_SPACE_MAP_MAGIC ||
	    hdr.vsh_version != VFS_SHADOW_SPACE_MAP_VERSION) {
		(void) st_error("invalid space map header");
		(void) close(map);
		return (-1);
	}

	while (read(map, &rec, sizeof (rec)) == sizeof (rec))
		callback(rec.vsrr_type, rec.vsrr_start, rec.vsrr_end, data);

	(void) close(map);
	return (0);
}

/*
 * Initialize and tear down global library state.
 */
int
st_init(void)
{
	if ((g_ctlfd = open(SHADOWTEST_DEV, O_RDWR)) < 0)
		return (st_error("failed to open %s", SHADOWTEST_DEV));

	return (0);
}

void
st_fini(void)
{
	(void) close(g_ctlfd);
}
