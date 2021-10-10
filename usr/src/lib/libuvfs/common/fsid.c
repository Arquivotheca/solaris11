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


#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <attr.h>
#include <libnvpair.h>
#include <sys/int_fmtio.h>
#include <string.h>
#include <atomic.h>
#include <assert.h>
#include <libuvfs_impl.h>

uint64_t
libuvfs_get_fsid(const char *path)
{
	nvlist_t *nvl = NULL;
	uint64_t fsid = 0;

	if (getattrat(AT_FDCWD, XATTR_VIEW_READONLY, path, &nvl))
		return (0);
	if (nvlist_lookup_uint64(nvl, A_FSID, &fsid))
		return (0);
	if (nvl != NULL)
		nvlist_free(nvl);

	return (fsid);
}

void
libuvfs_fsid_to_str(const uint64_t fsid, char *result, int size)
{
	(void) snprintf(result, size, "%016" PRIx64, fsid);
}

uint64_t
libuvfs_str_to_fsid(const char *fsidstr)
{
	uint64_t fsid;

	fsid = strtoll(fsidstr, NULL, 16);

	return (fsid);
}

void
libuvfs_fid_unique(libuvfs_fs_t *fs, libuvfs_fid_t *fid)
{
	libuvfs_fh_t fh;
	uint64_t seq;

	(void) memcpy(&fh.fs_fid_random, &fs->fs_fid_random,
	    sizeof (fh.fs_fid_random));
	seq = atomic_add_64_nv(&fs->fs_fid_seq, 1);
	(void) memcpy(&fh.fs_fid_seq, &seq, sizeof (fh.fs_fid_seq));

	fid->uvfid_len = sizeof (fh);
	(void) memcpy(fid->uvfid_data, &fh, sizeof (fh));
}

/*
 * NB: this must only be called on fids that are generated via
 * libuvfs_fid_unique()!  If that is not the case, then another
 * scheme to derive id numbers must be used.
 */
uint64_t
libuvfs_fid_to_id(libuvfs_fs_t *fs, const libuvfs_fid_t *fid)
{
	libuvfs_fh_t *fh = (libuvfs_fh_t *)(&fid->uvfid_data);
	uint64_t id;

	assert(fid->uvfid_len == sizeof (libuvfs_fh_t));
	assert(memcmp(&fh->fs_fid_random, &fs->fs_fid_random,
	    sizeof (fh->fs_fid_random)) == 0);

	(void) memcpy(&id, &fh->fs_fid_seq, sizeof (id));

	return (id);
}

int
libuvfs_fid_compare(const libuvfs_fid_t *a, const libuvfs_fid_t *b)
{
	int rc;

	rc = a->uvfid_len - b->uvfid_len;
	if (rc < 0)
		return (-1);
	if (rc > 0)
		return (1);

	rc = memcmp(a->uvfid_data, b->uvfid_data, a->uvfid_len);
	if (rc < 0)
		return (-1);
	if (rc > 0)
		return (1);

	return (0);
}
