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

#ifndef _SYS_LIBUVFS_IMPL_H
#define	_SYS_LIBUVFS_IMPL_H

#include <libuvfs.h>

#include <sys/types.h>
#include <door.h>
#include <libscf.h>
#include <thread.h>
#include <synch.h>
#include <umem.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct {
	char		fs_fid_seq[8];
	char		fs_fid_random[2];
} libuvfs_fh_t;

typedef struct {
	uint32_t st_key;
	void *st_value;
	libuvfs_fid_t st_fid;
	avl_node_t st_avl;
} libuvfs_stash_node_t;

typedef struct {
	libuvfs_fid_t nm_fid;
	rwlock_t nm_user_lock;
	avl_tree_t nm_dir;
	avl_node_t nm_fs_avl;
	list_t nm_allnames;
} libuvfs_fid_info_t;
typedef struct {
	char *de_name;
	libuvfs_fid_t de_fid;
	libuvfs_fid_t de_dirfid;
	libuvfs_fid_info_t *de_myinfo;
	libuvfs_fid_info_t *de_dir;
	avl_node_t de_dirnode;
	list_node_t de_allnames;
} libuvfs_name_dirent_t;

void libuvfs_stash_fs_construct(libuvfs_fs_t *);
void libuvfs_stash_fs_destroy(libuvfs_fs_t *);
void libuvfs_stash_fs_free(libuvfs_fs_t *);

void libuvfs_name_fs_construct(libuvfs_fs_t *);
void libuvfs_name_fs_destroy(libuvfs_fs_t *);
void libuvfs_name_fs_free(libuvfs_fs_t *);

void libuvfs_node_fs_construct(libuvfs_fs_t *);
void libuvfs_node_fs_destroy(libuvfs_fs_t *);

void libuvfs_fid_copy(const libuvfs_fid_t *, libuvfs_fid_t *);
int libuvfs_fid_compare(const libuvfs_fid_t *, const libuvfs_fid_t *);

char *libuvfs_strdup(const char *);
char *libuvfs_hexdump(const void *, int);
void libuvfs_strfree(char *);

void libuvfs_get_daemon_fsid(libuvfs_fs_t *);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_LIBUVFS_IMPL_H */
