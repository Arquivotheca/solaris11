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
 *
 * libuvfs - user-level file systems
 *
 * Linking with libuvfs allows implementation of file systems in user (not
 * kernel) space.  There is also libfuse, which supports the fuse API.
 * libuvfs is more Solaris-specific.
 *
 * To use, start with an executable linked with libuvfs.  Then mount it:
 *
 * # mount -F uvfs /path/to/executable /path/to/mount/point
 *
 * The executable must take the following steps:
 *
 * Use libuvfs_create_fs() to create a libuvfs_fs_t
 *
 * Use libuvfs_register_callbacks() to register a set of callbacks
 *
 * Call libuvfs_daemon_ready()
 *
 * CALLBACKS
 *
 * The bulk of the work is done in callbacks.  A callback is called for
 * every operation, e.g., open, write, read, ....  Callbacks receive an
 * argument structure, and return a result structure.  The structures are
 * specific for each operation.
 *
 * The signature for each callback is:
 *
 * callback(libuvfs_fs_t *fs, void *arg, size_t arglen)
 *
 * fs: a pointer to the fs structure that was created when
 * libuvfs_create_fs() was called.  It can hold user-defined data in its
 * fs_data field.
 *
 * arg: a pointer to the operation-specific structure relevant for the
 * given operation.
 *
 * argsize: the size of the arg.  Usually sizeof (specific_type), but
 * can be more if arbitrarily-sized data is following, e.g., the write
 * operation can contain a variable number of bytes.
 *
 */

#ifndef _LIBUVFS_H
#define	_LIBUVFS_H

#include <sys/libuvfs_ki.h>

#include <thread.h>
#include <synch.h>
#include <umem.h>
#include <libscf.h>
#include <sys/avl.h>
#include <ucred.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	LIBUVFS_STACK_OVERHEAD	(2 * 1024 * 1024)

#define	LIBUVFS_UMEM_ALLOC_MAX_FAILURES		(3600)
#define	LIBUVFS_UMEM_ALLOC_FAILURE_SLEEP	(1)
#define	LIBUVFS_UMEM_ALLOC_FAILURE_EXIT		(SMF_EXIT_ERR_FATAL)

#define	LIBUVFS_FSID_SVC	(0)
#define	LIBUVFS_FSID_NONE	(1)

/*
 * libuvfs types
 */

struct libuvfs_fs;

typedef void (*libuvfs_callback_t)(struct libuvfs_fs *, void *, size_t,
    ucred_t *cr);

typedef struct {
	libuvfs_optag_t lcr_optag;	/* which operation */
	libuvfs_callback_t lcr_callback; /* the callback */
} libuvfs_callback_reg_t;

typedef struct libuvfs_fs {
	uint64_t fs_fid_random; /* randomly generated at create time */
	uint64_t fs_fid_seq;

	mutex_t fs_lock;
	uint32_t fs_flags;
	uint64_t fs_fsid;
	int fs_dev;

	int fs_door;
	libuvfs_callback_reg_t fs_callback[UVFS_CB_NUM_OPS];
	cond_t fs_daemon_cv;
	pthread_attr_t fs_pthread_attr;
	uint32_t fs_io_maxread;
	uint32_t fs_io_maxwrite;
	uint32_t fs_max_dthreads;
	uint32_t fs_cur_dthreads;

	/* data stash */

	mutex_t fs_stash_lock;
	avl_tree_t fs_stash;

	/* namespace */

	rwlock_t fs_name_user_lock;
	mutex_t fs_name_lock;
	avl_tree_t fs_name_info_tree;

	/* SMF/libscf interaction */

	scf_handle_t *fs_scf_handle;
	scf_error_t fs_scf_error;
	scf_simple_app_props_t *fs_scf_props;
	int fs_daemon_fmri_size;
	char *fs_daemon_fmri;
} libuvfs_fs_t;

/* for fs_flags above */
#define	LIBUVFS_FS_FLAG_DOOR_VALID	0x01

#define	LIBUVFS_VERSION		1
typedef int libuvfs_version_t;

#define	LIBUVFS_SERVER_FMRI	"svc:/system/filesystem/uvfs-server"

#define	LIBUVFS_DAEMON_WAIT_MOUNT	(30 * 1000000)
#define	LIBUVFS_DAEMON_WAIT_OTHER	(24 * 1000000)

/*
 * libuvfs functions
 */

/* basics */

libuvfs_fs_t *libuvfs_create_fs(libuvfs_version_t, uint64_t);
void libuvfs_destroy_fs(libuvfs_fs_t *);
int libuvfs_set_fsparam(libuvfs_fs_t *);

/* callbacks and event loops */

int libuvfs_register_callback(libuvfs_fs_t *, const libuvfs_callback_reg_t *);
int libuvfs_register_callbacks(libuvfs_fs_t *, const libuvfs_callback_reg_t *);
void libuvfs_return(void *, size_t);

int libuvfs_daemon_launch(libuvfs_fs_t *, const char *, const char *, uint64_t,
    const char *);
int libuvfs_is_daemon(void);
int libuvfs_daemon_ready(libuvfs_fs_t *);
void libuvfs_daemon_exit(libuvfs_fs_t *);

/* smf helpers */

const char *libuvfs_scf_error(const libuvfs_fs_t *, scf_error_t *);
const char *libuvfs_get_daemon_executable(libuvfs_fs_t *);
int libuvfs_remove_disabled_instances(libuvfs_fs_t *);
void libuvfs_daemon_atexit(void);

/* callback helpers */

void libuvfs_fid_unique(libuvfs_fs_t *, libuvfs_fid_t *);
uint64_t libuvfs_fid_to_id(libuvfs_fs_t *, const libuvfs_fid_t *);
size_t libuvfs_add_direntry(void *buf, size_t buflen, const char *,
    ino64_t, off64_t off, void **);
char *libuvfs_strdup(const char *);
void libuvfs_strfree(char *);
int libuvfs_fid_compare(const libuvfs_fid_t *, const libuvfs_fid_t *);

/* data stash */

void *libuvfs_stash_fid_get(libuvfs_fs_t *, libuvfs_fid_t *, uint32_t, int *);
void *libuvfs_stash_fs_get(libuvfs_fs_t *, uint32_t, int *);
void *libuvfs_stash_fid_store(libuvfs_fs_t *, libuvfs_fid_t *, uint32_t, int,
    void *);
void *libuvfs_stash_fid_remove(libuvfs_fs_t *, libuvfs_fid_t *, uint32_t);
void *libuvfs_stash_fs_store(libuvfs_fs_t *, uint32_t, int, void *);
void *libuvfs_stash_fs_remove(libuvfs_fs_t *, uint32_t);

/* namespace helpers */

void libuvfs_name_fs_rdlock(libuvfs_fs_t *);
void libuvfs_name_fs_wrlock(libuvfs_fs_t *);
void libuvfs_name_fs_unlock(libuvfs_fs_t *);
int libuvfs_name_fid_rdlock(libuvfs_fs_t *, const libuvfs_fid_t *);
int libuvfs_name_fid_wrlock(libuvfs_fs_t *, const libuvfs_fid_t *);
int libuvfs_name_fid_unlock(libuvfs_fs_t *, const libuvfs_fid_t *);
void libuvfs_name_root_create(libuvfs_fs_t *, const libuvfs_fid_t *);
void libuvfs_name_store(libuvfs_fs_t *, const libuvfs_fid_t *, const char *,
    const libuvfs_fid_t *, int, libuvfs_fid_t *);
void libuvfs_name_delete(libuvfs_fs_t *, const libuvfs_fid_t *, const char *,
    libuvfs_fid_t *);
void libuvfs_name_lookup(libuvfs_fs_t *, const libuvfs_fid_t *, const char *,
    libuvfs_fid_t *);
int libuvfs_name_parent(libuvfs_fs_t *, const libuvfs_fid_t *, int,
    libuvfs_fid_t *);
uint32_t libuvfs_name_dirent(libuvfs_fs_t *, const libuvfs_fid_t *, int,
    libuvfs_fid_t *, char *, uint32_t);
uint32_t libuvfs_name_dirent_next(libuvfs_fs_t *, const libuvfs_fid_t *,
    libuvfs_fid_t *, char *, uint32_t);
uint32_t libuvfs_name_count(libuvfs_fs_t *, const libuvfs_fid_t *);
uint32_t libuvfs_name_path(libuvfs_fs_t *, const libuvfs_fid_t *, uint32_t,
    const char *, char *, uint32_t);

/* fsid helpers */

void libuvfs_fsid_to_str(const uint64_t, char *, int);
uint64_t libuvfs_str_to_fsid(const char *);
uint64_t libuvfs_get_fsid(const char *);

/* device helpers */
uint64_t libuvfs_expldev(dev_t);
dev_t libuvfs_cmpldev(uint64_t);

/* functions for the mount command */

int libuvfs_daemon_start_wait(libuvfs_fs_t *, uint32_t);
int libuvfs_daemon_register(libuvfs_fs_t *);

#ifdef	__cplusplus
}
#endif

#endif	/* _LIBUVFS_H */
