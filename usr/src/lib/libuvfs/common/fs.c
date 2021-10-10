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

#include <libuvfs_impl.h>

#include <umem.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/types32.h>
#include <sys/mkdev.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <strings.h>
#include <pthread.h>
#include <atomic.h>

static umem_cache_t *libuvfs_fs_cache;

uint32_t libuvfs_umem_alloc_failures = 0;
uint32_t libuvfs_umem_alloc_max_failures = LIBUVFS_UMEM_ALLOC_MAX_FAILURES;
uint32_t libuvfs_umem_alloc_failure_sleep = LIBUVFS_UMEM_ALLOC_FAILURE_SLEEP;

static void
libuvfs_fs_rand(libuvfs_fs_t *fs)
{
	int f;

	fs->fs_fid_random = rand();
	f = open("/dev/random", O_RDONLY);
	if (f >= 0) {
		uint64_t better;

		(void) read(f, &better, sizeof (better));
		(void) close(f);

		fs->fs_fid_random ^= better;
	}
}

libuvfs_fs_t *
libuvfs_create_fs(libuvfs_version_t version, uint64_t fsid)
{
	libuvfs_fs_t *fs;
	int i;

	if (version != LIBUVFS_VERSION)
		return (NULL);

	fs = umem_cache_alloc(libuvfs_fs_cache, UMEM_NOFAIL);

	fs->fs_dev = -1;
	fs->fs_fsid = fsid;

	fs->fs_io_maxread = 0;
	fs->fs_io_maxwrite = 0;
	fs->fs_max_dthreads = 0;
	fs->fs_cur_dthreads = 0;

	fs->fs_door = -1;
	for (i = 0; i < UVFS_CB_NUM_OPS; i++)
		fs->fs_callback[i].lcr_callback = NULL;

	fs->fs_scf_handle = NULL;
	fs->fs_scf_error = 0;
	fs->fs_scf_props = NULL;
	fs->fs_daemon_fmri_size = 0;
	fs->fs_daemon_fmri = NULL;

	if (fs->fs_fsid == LIBUVFS_FSID_SVC)
		libuvfs_get_daemon_fsid(fs);

	libuvfs_fs_rand(fs);
	fs->fs_fid_seq = 1;

	return (fs);
}

void
libuvfs_destroy_fs(libuvfs_fs_t *fs)
{
	if (fs->fs_dev >= 0)
		(void) close(fs->fs_dev);
	if (fs->fs_door >= 0)
		(void) close(fs->fs_door);

	if (fs->fs_scf_props != NULL)
		scf_simple_app_props_free(fs->fs_scf_props);
	if (fs->fs_scf_handle != NULL)
		scf_handle_destroy(fs->fs_scf_handle);
	if (fs->fs_daemon_fmri != NULL)
		umem_free(fs->fs_daemon_fmri, fs->fs_daemon_fmri_size);

	umem_cache_free(libuvfs_fs_cache, fs);
}

/*ARGSUSED*/
static int
libuvfs_fs_construct(void *vfs, void *foo, int bar)
{
	libuvfs_fs_t *fs = vfs;

	(void) mutex_init(&fs->fs_lock, USYNC_THREAD, NULL);
	(void) mutex_init(&fs->fs_stash_lock, USYNC_THREAD, NULL);
	(void) mutex_init(&fs->fs_name_lock, USYNC_THREAD, NULL);
	(void) cond_init(&fs->fs_daemon_cv, USYNC_THREAD, NULL);
	(void) pthread_attr_init(&fs->fs_pthread_attr);
	libuvfs_stash_fs_construct(fs);
	libuvfs_name_fs_construct(fs);

	return (0);
}

/*ARGSUSED*/
static void
libuvfs_fs_destroy(void *vfs, void *foo)
{
	libuvfs_fs_t *fs = vfs;

	libuvfs_name_fs_destroy(fs);
	libuvfs_stash_fs_destroy(fs);
	(void) pthread_attr_destroy(&fs->fs_pthread_attr);
	(void) cond_destroy(&fs->fs_daemon_cv);
	(void) mutex_destroy(&fs->fs_stash_lock);
	(void) mutex_destroy(&fs->fs_lock);
}

static int
libuvfs_umem_alloc_fail(void)
{
	if (atomic_inc_32_nv(&libuvfs_umem_alloc_failures) >
	    libuvfs_umem_alloc_max_failures)
		return (UMEM_CALLBACK_EXIT(255));

	(void) sleep(libuvfs_umem_alloc_failure_sleep);

	return (UMEM_CALLBACK_RETRY);
}

char *
libuvfs_strdup(const char *str)
{
	char *rc;

	rc = umem_alloc(strlen(str) + 1, UMEM_NOFAIL);

	return (strcpy(rc, str));
}

void
libuvfs_strfree(char *str)
{
	umem_free(str, strlen(str) + 1);
}

char *
libuvfs_hexdump(const void *stuff, int howlong)
{
	static char *hex = "0123456789abcdef";
	char *rc = umem_alloc(howlong * 2 + 1, UMEM_NOFAIL);
	uint8_t *bytes = (uint8_t *)stuff;
	int i;

	rc[howlong * 2] = '\0';

	for (i = 0; i < howlong; i++) {
		rc[2*i] = hex[bytes[i] >> 4];
		rc[2*i+1] = hex[bytes[i] & 0x0f];
	}

	return (rc);
}

#ifndef NBITSMINOR64
#define	NBITSMINOR64	32
#endif
#ifndef	MAXMAJ64
#define	MAXMAJ64	0xffffffffUL
#endif
#ifndef	MAXMIN64
#define	MAXMIN64	0xffffffffUL
#endif

uint64_t
libuvfs_expldev(dev_t dev)
{
#ifndef _LP64
	major_t major =  (major_t)dev >> NBITSMINOR32 & MAXMAJ32;
	return (((uint64_t)major << NBITSMINOR64) |
	    ((minor_t)dev & MAXMIN32));
#else
	return (dev);
#endif
}

dev_t
libuvfs_cmpldev(uint64_t dev)
{
#ifndef _LP64
	minor_t minor = (minor_t)dev & MAXMIN64;
	major_t major = (major_t)(dev >> NBITSMINOR64) & MAXMAJ64;

	if (major > MAXMAJ32 || minor > MAXMIN32)
		return ((dev_t)-1);

	return (((dev32_t)major << NBITSMINOR32) | minor);
#else
	return (dev);
#endif
}

#pragma init(libuvfs_fs_init)
static void
libuvfs_fs_init(void)
{
	umem_nofail_callback(libuvfs_umem_alloc_fail);
	libuvfs_fs_cache = umem_cache_create("libuvfs_fs_cache",
	    sizeof (libuvfs_fs_t), 0, libuvfs_fs_construct, libuvfs_fs_destroy,
	    NULL, NULL, NULL, 0);

	srand(getpid() + time(0));
}

#pragma fini(libuvfs_fs_fini)
static void
libuvfs_fs_fini(void)
{
	umem_cache_destroy(libuvfs_fs_cache);
}
