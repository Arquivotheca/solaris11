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
#include <strings.h>
#include <libuvfs_impl.h>

static umem_cache_t *libuvfs_fid_info_cache;
static umem_cache_t *libuvfs_name_dirent_cache;

static libuvfs_name_dirent_t *
libuvfs_name_dirent_alloc(libuvfs_fid_info_t *dir, libuvfs_fid_info_t *child,
    const char *name, const libuvfs_fid_t *dirfid,
    const libuvfs_fid_t *childfid)
{
	libuvfs_name_dirent_t *rc;

	rc = umem_cache_alloc(libuvfs_name_dirent_cache, UMEM_NOFAIL);

	rc->de_name = libuvfs_strdup(name);
	rc->de_dirfid = *dirfid;
	rc->de_fid = *childfid;
	rc->de_dir = dir;
	rc->de_myinfo = child;

	return (rc);
}

static void
libuvfs_name_dirent_free(libuvfs_name_dirent_t *dirent)
{
	libuvfs_fid_info_t *myinfo = dirent->de_myinfo;

	if (list_link_active(&dirent->de_allnames))
		list_remove(&myinfo->nm_allnames, dirent);
	libuvfs_strfree(dirent->de_name);

	umem_cache_free(libuvfs_name_dirent_cache, dirent);
}

static libuvfs_fid_info_t *
libuvfs_fid_info_alloc(const libuvfs_fid_t *fid)
{
	libuvfs_fid_info_t *rc;

	rc = umem_cache_alloc(libuvfs_fid_info_cache, UMEM_NOFAIL);

	rc->nm_fid = *fid;

	return (rc);
}

static void
libuvfs_fid_info_free(libuvfs_fid_info_t *info)
{
	libuvfs_name_dirent_t *de;
	void *cookie;

	cookie = NULL;
	while ((de = avl_destroy_nodes(&info->nm_dir, &cookie)) != NULL)
		libuvfs_name_dirent_free(de);
	while ((de = list_head(&info->nm_allnames)) != NULL) {
		/*
		 * If we get here, we're freeing info when it still has
		 * libuvfs_name_dirent_t objects pointing to it; i.e.,
		 * it's like freeing a file before all of its names are
		 * gone.  This can happen via libuvfs_destroy_fs, when
		 * all libuvfs_fid_info_t objects are destroyed in
		 * fid order.
		 */
		list_remove(&info->nm_allnames, de);
		de->de_myinfo = NULL;
	}

	umem_cache_free(libuvfs_fid_info_cache, info);
}

static libuvfs_fid_info_t *
libuvfs_fid_info_find(libuvfs_fs_t *fs, const libuvfs_fid_t *dirfid,
    avl_index_t *where)
{
	libuvfs_fid_info_t *rc, key;

	key.nm_fid = *dirfid;

	rc = avl_find(&fs->fs_name_info_tree, &key, where);

	return (rc);
}

static libuvfs_name_dirent_t *
libuvfs_name_dirent_find(libuvfs_fid_info_t *dir, const char *name,
    avl_index_t *where)
{
	libuvfs_name_dirent_t *rc, key;

	key.de_name = libuvfs_strdup(name);

	rc = avl_find(&dir->nm_dir, &key, where);

	libuvfs_strfree(key.de_name);

	return (rc);
}

void
libuvfs_name_fs_rdlock(libuvfs_fs_t *fs)
{
	(void) rw_rdlock(&fs->fs_name_user_lock);
}

void
libuvfs_name_fs_wrlock(libuvfs_fs_t *fs)
{
	(void) rw_wrlock(&fs->fs_name_user_lock);
}

void
libuvfs_name_fs_unlock(libuvfs_fs_t *fs)
{
	(void) rw_unlock(&fs->fs_name_user_lock);
}

int
libuvfs_name_fid_rdlock(libuvfs_fs_t *fs, const libuvfs_fid_t *fid)
{
	libuvfs_fid_info_t *info;

	info = libuvfs_fid_info_find(fs, fid, NULL);
	if (info == NULL)
		return (-1);

	(void) rw_rdlock(&info->nm_user_lock);

	return (0);
}

int
libuvfs_name_fid_wrlock(libuvfs_fs_t *fs, const libuvfs_fid_t *fid)
{
	libuvfs_fid_info_t *info;

	info = libuvfs_fid_info_find(fs, fid, NULL);
	if (info == NULL)
		return (-1);

	(void) rw_wrlock(&info->nm_user_lock);

	return (0);
}

int
libuvfs_name_fid_unlock(libuvfs_fs_t *fs, const libuvfs_fid_t *fid)
{
	libuvfs_fid_info_t *info;

	info = libuvfs_fid_info_find(fs, fid, NULL);
	if (info == NULL)
		return (-1);

	(void) rw_unlock(&info->nm_user_lock);

	return (0);
}

void
libuvfs_name_root_create(libuvfs_fs_t *fs, const libuvfs_fid_t *rootfid)
{
	libuvfs_fid_info_t *root;
	avl_index_t where;

	(void) mutex_lock(&fs->fs_name_lock);

	root = libuvfs_fid_info_find(fs, rootfid, &where);
	if (root != NULL) {
		(void) mutex_unlock(&fs->fs_name_lock);
		return;
	}

	root = libuvfs_fid_info_alloc(rootfid);
	avl_insert(&fs->fs_name_info_tree, root, where);

	(void) mutex_unlock(&fs->fs_name_lock);
}

/*
 * Store childfid in the directory specified by dirfid under the given
 * name.  If overwrite is nonzero, the previous fid (if any) is overwritten.
 * If oldfid is not NULL, return the previous fid; the uvfid_len is set
 * to NULL if there is no previous fid.
 *
 * If childfid is NULL, and overwrite is true, then this function will
 * delete (although you could also call libuvfs_name_delete).
 */

void
libuvfs_name_store(libuvfs_fs_t *fs, const libuvfs_fid_t *dirfid,
    const char *name, const libuvfs_fid_t *childfid, int overwrite,
    libuvfs_fid_t *oldfid)
{
	libuvfs_fid_info_t *dir;
	libuvfs_name_dirent_t *dirent;
	libuvfs_fid_info_t *child;
	avl_index_t where;

	(void) mutex_lock(&fs->fs_name_lock);

	dir = libuvfs_fid_info_find(fs, dirfid, &where);
	if (dir == NULL) {
		(void) mutex_unlock(&fs->fs_name_lock);
		return;
	}
	if (childfid != NULL) {
		child = libuvfs_fid_info_find(fs, childfid, &where);
		if (child == NULL) {
			child = libuvfs_fid_info_alloc(childfid);
			avl_insert(&fs->fs_name_info_tree, child, where);
		}
	} else {
		child = NULL;
	}

	dirent = libuvfs_name_dirent_find(dir, name, &where);
	if (oldfid != NULL) {
		if (dirent != NULL)
			*oldfid = dirent->de_fid;
		else
			oldfid->uvfid_len = 0;
	}

	if (dirent == NULL) {
		if (childfid != NULL) {
			dirent = libuvfs_name_dirent_alloc(dir, child, name,
			    dirfid, childfid);
			avl_insert(&dir->nm_dir, dirent, where);
			list_insert_head(&child->nm_allnames, dirent);
		}
	} else if (overwrite) {
		libuvfs_fid_info_t *oldinfo = dirent->de_myinfo;
		list_remove(&oldinfo->nm_allnames, dirent);

		if (childfid != NULL) {
			dirent->de_fid = *childfid;
			list_insert_head(&child->nm_allnames, dirent);
			dirent->de_myinfo = child;
		} else {
			avl_remove(&dir->nm_dir, dirent);
			libuvfs_name_dirent_free(dirent);
		}
	}

	(void) mutex_unlock(&fs->fs_name_lock);
}

/*
 * Delete the fid in the given dirfid for the given name.  If oldfid is
 * not NULL, then the previous fid (if any) is put there.
 */

void
libuvfs_name_delete(libuvfs_fs_t *fs, const libuvfs_fid_t *dirfid,
    const char *name, libuvfs_fid_t *oldfid)
{
	libuvfs_name_store(fs, dirfid, name, NULL, B_TRUE, oldfid);
}

/*
 * Retrieve the fid for the given dirfid and the given name.
 */

void
libuvfs_name_lookup(libuvfs_fs_t *fs, const libuvfs_fid_t *dirfid,
    const char *name, libuvfs_fid_t *found)
{
	libuvfs_fid_info_t *dir;
	libuvfs_name_dirent_t *dirent;

	(void) mutex_lock(&fs->fs_name_lock);

	found->uvfid_len = 0;
	dir = libuvfs_fid_info_find(fs, dirfid, NULL);
	if (dir && (dirent = libuvfs_name_dirent_find(dir, name, NULL)))
		*found = dirent->de_fid;

	(void) mutex_unlock(&fs->fs_name_lock);
}

/*
 * Return the parent fid for the given directory fid.  The index parameter
 * indicates which hard link to follow; normally, index is zero.
 */

int
libuvfs_name_parent(libuvfs_fs_t *fs, const libuvfs_fid_t *fid, int index,
    libuvfs_fid_t *parent)
{
	libuvfs_fid_info_t *me;
	libuvfs_name_dirent_t *de;
	int found = B_TRUE;

	(void) mutex_lock(&fs->fs_name_lock);

	me = libuvfs_fid_info_find(fs, fid, NULL);
	if (me == NULL) {
		found = B_FALSE;
		goto out;
	}

	for (de = list_head(&me->nm_allnames); de != NULL;
	    de = list_next(&me->nm_allnames, de))
		if (index-- <= 0)
			break;
	if (de == NULL) {
		found = B_FALSE;
		goto out;
	}

out:
	if (found)
		*parent = de->de_dirfid;
	else
		parent->uvfid_len = 0;

	(void) mutex_unlock(&fs->fs_name_lock);

	return (found);
}

/*
 * Retrieve the fid and name for the "index"th entry in the given directory.
 */

uint32_t
libuvfs_name_dirent(libuvfs_fs_t *fs, const libuvfs_fid_t *dirfid, int index,
    libuvfs_fid_t *fid, char *name, uint32_t namesize)
{
	libuvfs_fid_info_t *dir;
	libuvfs_name_dirent_t *de;
	uint32_t namelen = 0;

	dir = libuvfs_fid_info_find(fs, dirfid, NULL);
	if (dir == NULL)
		goto out;

	for (de = avl_first(&dir->nm_dir); de != NULL;
	    de = AVL_NEXT(&dir->nm_dir, de))
		if (--index < 0)
			break;
	if (de == NULL)
		goto out;

	*fid = de->de_fid;
	namelen = strlcpy(name, de->de_name, namesize);

out:

	return (namelen);
}

/*
 * Given a directory and an existing name, retrieve the next name and fid
 * from that directory.  Note that directories are sorted by name.
 */

uint32_t
libuvfs_name_dirent_next(libuvfs_fs_t *fs, const libuvfs_fid_t *dirfid,
    libuvfs_fid_t *fid, char *name, uint32_t namesize)
{
	libuvfs_fid_info_t *dir;
	libuvfs_name_dirent_t *de;
	uint32_t namelen = 0;

	dir = libuvfs_fid_info_find(fs, dirfid, NULL);
	if (dir == NULL)
		goto out;
	de = libuvfs_name_dirent_find(dir, name, NULL);
	if (de == NULL)
		goto out;
	de = AVL_NEXT(&dir->nm_dir, de);
	if (de == NULL)
		goto out;

	*fid = de->de_fid;
	namelen = strlcpy(name, de->de_name, namesize);

out:

	return (namelen);
}

/*
 * Count the number of names a fid appears in, i.e. the number of known
 * hard links.
 */

uint32_t
libuvfs_name_count(libuvfs_fs_t *fs, const libuvfs_fid_t *fid)
{
	libuvfs_fid_info_t *child;
	uint32_t count = 0;

	(void) mutex_lock(&fs->fs_name_lock);

	child = libuvfs_fid_info_find(fs, fid, NULL);
	if (child != NULL) {
		libuvfs_name_dirent_t *de;

		for (de = list_head(&child->nm_allnames); de != NULL;
		    de = list_next(&child->nm_allnames, de))
			++count;
	}

	(void) mutex_unlock(&fs->fs_name_lock);

	return (count);
}

static uint32_t
libuvfs_name_dirent_path(libuvfs_name_dirent_t *de, char *buffer,
    uint32_t size)
{
	uint32_t need = 0;

	if (de->de_dir != NULL) {
		libuvfs_name_dirent_t *first =
		    list_head(&de->de_dir->nm_allnames);
		if (first != NULL)
			need = libuvfs_name_dirent_path(first, buffer, size);
	}

	if (need < size)
		need = strlcat(buffer, "/", size);

	if (need < size)
		need = strlcat(buffer, de->de_name, size);

out:
	return (need);
}

/*
 * Determine a path for a given fid.  The index parameter specifies which
 * hard link to find the path for; it is normally zero.
 */

uint32_t
libuvfs_name_path(libuvfs_fs_t *fs, const libuvfs_fid_t *fid,
    uint32_t index, const char *prefix, char *buffer, uint32_t size)
{
	libuvfs_fid_info_t *fnode;
	libuvfs_name_dirent_t *de;
	uint32_t need = 0;

	(void) mutex_lock(&fs->fs_name_lock);

	fnode = libuvfs_fid_info_find(fs, fid, NULL);
	if (fnode == NULL)
		goto out;
	de = list_head(&fnode->nm_allnames);
	if (de != NULL)
		while (index-- > 0)
			if ((de = list_next(&fnode->nm_allnames, de)) == NULL)
				goto out;

	if (prefix == NULL)
		prefix = (de != NULL) ? "" : "/";
	need = strlcpy(buffer, prefix, size);
	if (need >= size)
		goto out;

	if (de != NULL)
		need = libuvfs_name_dirent_path(de, buffer, size);

out:
	(void) mutex_unlock(&fs->fs_name_lock);

	return (need);
}

static int
libuvfs_name_dirent_compare(const void *va, const void *vb)
{
	const libuvfs_name_dirent_t *a = va;
	const libuvfs_name_dirent_t *b = vb;
	int rc;

	rc = strcmp(a->de_name, b->de_name);
	if (rc < 0)
		return (-1);
	if (rc > 0)
		return (1);
	return (0);
}

/*ARGSUSED*/
static int
libuvfs_fid_info_construct(void *vdir, void *foo, int bar)
{
	libuvfs_fid_info_t *dir = vdir;

	(void) rwlock_init(&dir->nm_user_lock, USYNC_THREAD, NULL);
	list_create(&dir->nm_allnames, sizeof (libuvfs_name_dirent_t),
	    offsetof(libuvfs_name_dirent_t, de_allnames));
	avl_create(&dir->nm_dir, libuvfs_name_dirent_compare,
	    sizeof (libuvfs_name_dirent_t), offsetof(libuvfs_name_dirent_t,
	    de_dirnode));

	return (0);
}

/*ARGSUSED*/
static void
libuvfs_fid_info_destroy(void *vdir, void *foo)
{
	libuvfs_fid_info_t *dir = vdir;

	avl_destroy(&dir->nm_dir);
	list_destroy(&dir->nm_allnames);
	(void) rwlock_destroy(&dir->nm_user_lock);
}

static int
libuvfs_fid_info_compare(const void *va, const void *vb)
{
	const libuvfs_fid_info_t *a = va;
	const libuvfs_fid_info_t *b = vb;

	return (libuvfs_fid_compare(&a->nm_fid, &b->nm_fid));
}

void
libuvfs_name_fs_construct(libuvfs_fs_t *fs)
{
	(void) rwlock_init(&fs->fs_name_user_lock, USYNC_THREAD, NULL);
	avl_create(&fs->fs_name_info_tree, libuvfs_fid_info_compare,
	    sizeof (libuvfs_fid_info_t), offsetof(libuvfs_fid_info_t,
	    nm_fs_avl));
}

void
libuvfs_name_fs_destroy(libuvfs_fs_t *fs)
{
	avl_destroy(&fs->fs_name_info_tree);
	(void) rwlock_destroy(&fs->fs_name_user_lock);
}

void
libuvfs_name_fs_free(libuvfs_fs_t *fs)
{
	libuvfs_fid_info_t *dir;
	void *cookie;

	cookie = NULL;
	while ((dir = avl_destroy_nodes(&fs->fs_name_info_tree, &cookie))
	    != NULL)
		libuvfs_fid_info_free(dir);
}

#pragma init(libuvfs_name_init)
static void
libuvfs_name_init(void)
{
	libuvfs_fid_info_cache = umem_cache_create("libuvfs_fid_info_cache",
	    sizeof (libuvfs_fid_info_t), 0,
	    libuvfs_fid_info_construct, libuvfs_fid_info_destroy, NULL,
	    NULL, NULL, 0);
	libuvfs_name_dirent_cache =
	    umem_cache_create("libuvfs_name_dirent_cache",
	    sizeof (libuvfs_name_dirent_t), 0,
	    NULL, NULL, NULL,
	    NULL, NULL, 0);
}

#pragma fini(libuvfs_name_fini)
static void
libuvfs_name_fini(void)
{
	umem_cache_destroy(libuvfs_fid_info_cache);
	umem_cache_destroy(libuvfs_name_dirent_cache);
}
