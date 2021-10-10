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
#include <assert.h>
#include <sys/avl.h>
#include <string.h>
#include <libuvfs_impl.h>

static umem_cache_t *libuvfs_stash_node_cache;

static libuvfs_stash_node_t *
libuvfs_stash_node_alloc(libuvfs_fid_t *fid, uint32_t key, void *value)
{
	libuvfs_stash_node_t *rc;

	rc = umem_cache_alloc(libuvfs_stash_node_cache, UMEM_NOFAIL);

	rc->st_fid.uvfid_len = fid->uvfid_len;
	if (fid->uvfid_len > 0)
		(void) memcpy(rc->st_fid.uvfid_data, fid->uvfid_data,
		    fid->uvfid_len);

	rc->st_key = key;
	rc->st_value = value;

	return (rc);
}

static void
libuvfs_stash_node_free(libuvfs_stash_node_t *node)
{
	umem_cache_free(libuvfs_stash_node_cache, node);
}

static libuvfs_stash_node_t *
libuvfs_stash_get_impl(libuvfs_fs_t *fs, libuvfs_fid_t *fid, uint32_t key,
    avl_index_t *where)
{
	libuvfs_stash_node_t nkey, *found;

	nkey.st_key = key;
	nkey.st_fid.uvfid_len = fid->uvfid_len;
	if (fid->uvfid_len > 0)
		(void) memcpy(nkey.st_fid.uvfid_data, fid->uvfid_data,
		    fid->uvfid_len);

	found = avl_find(&fs->fs_stash, &nkey, where);

	return (found);
}

void *
libuvfs_stash_fid_get(libuvfs_fs_t *fs, libuvfs_fid_t *fid, uint32_t key,
    int *found)
{
	libuvfs_stash_node_t *exist;

	(void) mutex_lock(&fs->fs_stash_lock);
	exist = libuvfs_stash_get_impl(fs, fid, key, NULL);
	(void) mutex_unlock(&fs->fs_stash_lock);

	if (found != NULL)
		*found = (exist != NULL) ? B_TRUE : B_FALSE;
	if (exist != NULL)
		return (exist->st_value);
	return (NULL);
}

void *
libuvfs_stash_fs_get(libuvfs_fs_t *fs, uint32_t key, int *found)
{
	libuvfs_fid_t fid;

	fid.uvfid_len = 0;

	return (libuvfs_stash_fid_get(fs, &fid, key, found));
}

void *
libuvfs_stash_fid_store(libuvfs_fs_t *fs, libuvfs_fid_t *fid, uint32_t key,
    int overwrite, void *value)
{
	libuvfs_stash_node_t *node;
	avl_index_t where;
	void *rc = NULL;

	(void) mutex_lock(&fs->fs_stash_lock);
	node = libuvfs_stash_get_impl(fs, fid, key, &where);
	if (node != NULL) {
		rc = node->st_value;
		if (overwrite) {
			if (value != NULL) {
				node->st_value = value;
			} else {
				avl_remove(&fs->fs_stash, node);
				libuvfs_stash_node_free(node);
			}
		}
	} else {
		node = libuvfs_stash_node_alloc(fid, key, value);
		avl_insert(&fs->fs_stash, node, where);
		node->st_value = value;
	}
	(void) mutex_unlock(&fs->fs_stash_lock);

	return (rc);
}

void *
libuvfs_stash_fid_remove(libuvfs_fs_t *fs, libuvfs_fid_t *fid, uint32_t key)
{
	return (libuvfs_stash_fid_store(fs, fid, key, B_TRUE, NULL));
}

void *
libuvfs_stash_fs_store(libuvfs_fs_t *fs, uint32_t key, int overwrite,
    void *value)
{
	libuvfs_fid_t fid;

	fid.uvfid_len = 0;

	return (libuvfs_stash_fid_store(fs, &fid, key, overwrite, value));
}

void *
libuvfs_stash_fs_remove(libuvfs_fs_t *fs, uint32_t key)
{
	return (libuvfs_stash_fs_store(fs, key, B_TRUE, NULL));
}

static int
libuvfs_stash_node_compare(const void *va, const void *vb)
{
	const libuvfs_stash_node_t *a = va;
	const libuvfs_stash_node_t *b = vb;
	int rc;

	rc = a->st_key - b->st_key;
	if (rc < 0)
		return (-1);
	if (rc > 0)
		return (1);

	return (libuvfs_fid_compare(&a->st_fid, &b->st_fid));
}

void
libuvfs_stash_fs_construct(libuvfs_fs_t *fs)
{
	avl_create(&fs->fs_stash, libuvfs_stash_node_compare,
	    sizeof (libuvfs_stash_node_t),
	    offsetof(libuvfs_stash_node_t, st_avl));
}

void
libuvfs_stash_fs_destroy(libuvfs_fs_t *fs)
{
	avl_destroy(&fs->fs_stash);
}

void
libuvfs_stash_fs_free(libuvfs_fs_t *fs)
{
	libuvfs_stash_node_t *node;
	void *cookie;

	cookie = NULL;
	while ((node = avl_destroy_nodes(&fs->fs_stash, &cookie)) != NULL)
		libuvfs_stash_node_free(node);
}

#pragma init(libuvfs_stash_init)
static void
libuvfs_stash_init(void)
{
	libuvfs_stash_node_cache = umem_cache_create("libuvfs_stash_node_cache",
	    sizeof (libuvfs_stash_node_t), 0,
	    NULL, NULL, NULL,
	    NULL, NULL, 0);
}

#pragma fini(libuvfs_stash_fini)
static void
libuvfs_stash_fini(void)
{
	umem_cache_destroy(libuvfs_stash_node_cache);
}
