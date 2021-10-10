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

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/mkdev.h>
#include <sys/fcntl.h>
#include <sys/vfs.h>
#include <sys/stat.h>
#include <sys/flock.h>
#include <sys/vfs_opreg.h>
#include <sys/vnode.h>
#include <sys/mode.h>
#include <sys/uvfs_vfsops.h>
#include <sys/uvfs_uvnode.h>
#include <sys/uvfs_upcall.h>
#include <sys/cmn_err.h>
#include <sys/crc32.h>
#include <vm/pvn.h>
#include <sys/policy.h>

static kmem_cache_t *uvnode_cache = NULL;

#define	UVH_LOCKS	256
typedef struct uv_hash_table {
	uint64_t h_mask;
	uvnode_t **h_table;
	kmutex_t h_locks[UVH_LOCKS];
} uv_hash_table_t;

uv_hash_table_t uv_htable;

#define	UVH_HASH(vfsp, ino) ((uint64_t)(uintptr_t)vfsp ^ (ino))
#define	UVH_HASH_INDEX(vfsp, ino) \
	UVH_HASH(vfsp, ino) & uvnode_hash_table.uvh_mask
#define	UVH_HASH_LOCK(h, idx)	(&(h)->h_locks[idx & (UVH_LOCKS-1)])

uint64_t
uvfs_hash_lock(vfs_t *vfsp, libuvfs_fid_t *fid)
{
	uint64_t idx;
	uint64_t hv;
	uint32_t id_crc;

	CRC32(id_crc, fid->uvfid_data, fid->uvfid_len, -1U, crc32_table);
	hv = UVH_HASH(vfsp, id_crc);
	idx = hv & uv_htable.h_mask;

	mutex_enter(UVH_HASH_LOCK(&uv_htable, idx));
	return (idx);
}

void
uvfs_hash_unlock(uint64_t idx)
{
	mutex_exit(UVH_HASH_LOCK(&uv_htable, idx));
}

/*ARGSUSED*/
static int
uvfs_uvnode_cache_constructor(void *buf, void *unused, int kmflags)
{
	uvnode_t *uvp = buf;

	list_link_init(&uvp->uv_list_node);
	mutex_init(&uvp->uv_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&uvp->uv_open_lock, NULL, MUTEX_DEFAULT, NULL);
	rw_init(&uvp->uv_rwlock, NULL, RW_DEFAULT, NULL);

	return (0);
}

/*ARGSUSED*/
static void
uvfs_uvnode_cache_destructor(void *buf, void *unused)
{
	uvnode_t *uvp = buf;

	ASSERT(!list_link_active(&uvp->uv_list_node));
	mutex_destroy(&uvp->uv_lock);
	mutex_destroy(&uvp->uv_open_lock);
	rw_destroy(&uvp->uv_rwlock);
}

void
uvfs_uvnode_init(void)
{
	uint64_t hsize = 1ULL << 22;
	int i;

	uv_htable.h_table = kmem_zalloc(hsize * sizeof (void *), KM_SLEEP);
	uv_htable.h_mask = hsize - 1;
	for (i = 0; i != UVH_LOCKS; i++)
		mutex_init(&uv_htable.h_locks[i], NULL,
		    MUTEX_DEFAULT, NULL);

	uvnode_cache = kmem_cache_create("uvfs_uvnode_cache",
	    sizeof (uvnode_t), 0, uvfs_uvnode_cache_constructor,
	    uvfs_uvnode_cache_destructor, NULL, NULL, NULL, 0);
}

uvnode_t *
uvfs_uvnode_alloc(void)
{
	uvnode_t *uvp;

	uvp = kmem_cache_alloc(uvnode_cache, KM_SLEEP);

	uvp->uv_flags = 0;
	uvp->uv_vnode = vn_alloc(KM_SLEEP);
	UVTOV(uvp)->v_data = uvp;
	uvp->uv_fid.uvfid_len = 0;
	uvp->uv_pfid.uvfid_len = 0;
	uvp->uv_hash_next = NULL;
	uvp->uv_uvfsvfs = NULL;
	uvp->uv_door = NULL;
	uvp->uv_id = 0;
	uvp->uv_seq = 0;
	uvp->uv_mapcnt = 0;
	uvp->uv_opencnt = 0;
	uvp->uv_size_known = B_FALSE;

	return (uvp);
}

void
uvfs_uvnode_fini(void)
{
	int i;

	for (i = 0; i != UVH_LOCKS; i++)
		mutex_destroy(&uv_htable.h_locks[i]);

	kmem_free(uv_htable.h_table, (uv_htable.h_mask + 1) * sizeof (void *));

	if (uvnode_cache)
		kmem_cache_destroy(uvnode_cache);
	uvnode_cache = NULL;
}

void
uvfs_hash_remove(uvnode_t *uvp, uint64_t idx)
{
	uvnode_t *hashp, **hashpp;

	ASSERT(MUTEX_HELD(UVH_HASH_LOCK(&uv_htable, idx)));
	hashpp = &uv_htable.h_table[idx];
	while ((hashpp != NULL) && ((hashp = *hashpp) != uvp)) {
		hashpp = &hashp->uv_hash_next;
	}
	if (hashpp == NULL)
		return;

	*hashpp = uvp->uv_hash_next;
	uvp->uv_hash_next = NULL;
}

void
uvfs_uvnode_free(uvnode_t *uvp)
{
	uvfsvfs_t *uvfsp = uvp->uv_uvfsvfs;

	/* Remove uvnode from the per-fs uvnode list */
	mutex_enter(&uvfsp->uvfs_uvnodes_lock);
	list_remove(&uvfsp->uvfs_uvnodes, uvp);
	mutex_exit(&uvfsp->uvfs_uvnodes_lock);

	kmem_cache_free(uvnode_cache, uvp);
}

boolean_t
uvfs_fid_match(uvnode_t *uvp, vfs_t *vfsp, libuvfs_fid_t *fid)
{
	int cmp;

	if (UVTOV(uvp)->v_vfsp != vfsp)
		return (B_FALSE);

	if (uvp->uv_fid.uvfid_len != fid->uvfid_len)
		return (B_FALSE);

	cmp = memcmp(uvp->uv_fid.uvfid_data, fid->uvfid_data, fid->uvfid_len);
	if (cmp != 0)
		return (B_FALSE);

	return (B_TRUE);
}

int
uvfs_uvget(vfs_t *vfsp, uvnode_t **uvnodep, libuvfs_fid_t *fid,
    libuvfs_stat_t *stat)
{
	uvnode_t *uvp;
	vnode_t *vp;
	uv_hash_table_t *h = &uv_htable;
	uint64_t idx;
	int error = 0;
	uvfsvfs_t *uvfsvfs = (uvfsvfs_t *)vfsp->vfs_data;

	idx = uvfs_hash_lock(vfsp, fid);
	for (uvp = h->h_table[idx]; uvp != NULL;
	    uvp = uvp->uv_hash_next) {
		if (uvfs_fid_match(uvp, vfsp, fid)) {
			VN_HOLD(UVTOV(uvp));
			uvfs_hash_unlock(idx);
			*uvnodep = uvp;
			uvfs_update_attrs(uvp, stat);
			return (0);
		}
	}

	/* Not found.  If no mode given, we fail; else, we create. */

	if (stat->l_mode == 0) {
		error = ESTALE;
		goto out;
	}

	uvp = uvfs_uvnode_alloc();

	uvp->uv_fid = *fid; /* struct assign */
	uvp->uv_uvfsvfs = vfsp->vfs_data;

	vp = UVTOV(uvp);
	vn_reinit(vp);

	vp->v_type = IFTOVT((mode_t)stat->l_mode);
	vp->v_vfsp = vfsp;

	switch (vp->v_type) {
	case VDIR:
		vn_setops(vp, uvfsvfs->uvfs_dvnodeops);
		break;
	case VBLK:
	case VCHR:
		vp->v_rdev = uvfs_cmpldev(stat->l_rdev);
		/*FALLTHROUGH*/
	case VFIFO:
	case VSOCK:
	case VDOOR:
		vn_setops(vp, uvfsvfs->uvfs_fvnodeops);
		break;
	case VREG:
		vp->v_flag |= VMODSORT;
		vn_setops(vp, uvfsvfs->uvfs_fvnodeops);
		break;
	case VLNK:
		vn_setops(vp, uvfsvfs->uvfs_symvnodeops);
		break;
	default:
		vn_setops(vp, uvfsvfs->uvfs_evnodeops);
		break;
	}
	*uvnodep = uvp;

	VFS_HOLD(vp->v_vfsp);

	uvfs_update_attrs(uvp, stat);

	uvp->uv_hash_next = h->h_table[idx];
	h->h_table[idx] = uvp;

	/* Add the uvnode to the per-fs uvnode list */
	mutex_enter(&uvfsvfs->uvfs_uvnodes_lock);
	list_insert_tail(&uvfsvfs->uvfs_uvnodes, uvp);
	mutex_exit(&uvfsvfs->uvfs_uvnodes_lock);

out:
	uvfs_hash_unlock(idx);
	return (error);
}

/*
 * Dummy interface used when truncating a file
 */
/*ARGSUSED*/
static int
uvfs_no_putpage(vnode_t *vp, page_t *pp, u_offset_t *offp, size_t *lenp,
    int flags, cred_t *cr)
{
	return (0);
}

int
uvfs_freesp(uvnode_t *uvp, uint64_t off, uint64_t len, int flag, cred_t *cr)
{
	vnode_t *vp = UVTOV(uvp);
	int error;

	/* Only allow freeing to end of file */
	if (len != 0)
		return (EINVAL);

	if (MANDLOCK(vp, uvp->uv_mode)) {
		uint64_t length = (len ? len : uvp->uv_size - off);

		/*
		 * chklock assumes VOP_RWLOCK is held and may drop/reacquire
		 * lock.
		 */
		rw_enter(&uvp->uv_rwlock, RW_READER);
		error = chklock(vp, FWRITE, off, length, flag, NULL);
		rw_exit(&uvp->uv_rwlock);
		if (error)
			return (error);
	}

	/* do upcall to truncate file */
	error = uvfs_up_space(uvp, off, len, flag, cr);

	if (error == 0 && vn_has_cached_data(vp)) {
		page_t *pp;
		uint64_t start = off & PAGEMASK;
		int poff = off & PAGEOFFSET;

		if (poff != 0 && (pp = page_lookup(vp, start, SE_SHARED))) {
			/*
			 * We need to zero a partial page.
			 */
			pagezero(pp, poff, PAGESIZE - poff);
			start += PAGESIZE;
			page_unlock(pp);
		}
		error = pvn_vplist_dirty(vp, start, uvfs_no_putpage,
		    B_INVAL | B_TRUNC, NULL);
		ASSERT(error == 0);
	}

	uvp->uv_size = off;
	return (error);
}

int
uvfs_access_check(uvnode_t *uvp, int mode, cred_t *cr)
{
	int error;
	int shift = 0;
	uvfsvfs_t *uvfsvfsp = uvp->uv_uvfsvfs;
	vnode_t *vp = UVTOV(uvp);

	if ((uvfsvfsp->uvfs_allow_other == 0) &&
	    crgetuid(cr) != crgetuid(uvfsvfsp->uvfs_mount_cred))
		return (EACCES);

	if ((mode & VWRITE) && vn_is_readonly(vp) && IS_DEVVP(vp))
		return (EROFS);

	if (crgetuid(cr) != uvp->uv_uid) {
		shift += 3;
		if (!groupmember((gid_t)uvp->uv_gid, cr))
			shift += 3;
	}

	error = secpolicy_vnode_access2(cr, vp, (uid_t)uvp->uv_uid,
	    (mode_t)uvp->uv_mode << shift, mode);

	return (error);
}

int
uvfs_directio(vnode_t *vp, int cmd, cred_t *cr)
{
	uvnode_t *uvp = VTOUV(vp);
	int error;

	if (cmd == DIRECTIO_ON) {
		/* Drop out if there is no work to do */
		if (uvp->uv_flags & UVNODE_FLAG_DIRECTIO)
			return (0);

		/* Flush the page cache. */
		(void) VOP_RWLOCK(vp, V_WRITELOCK_TRUE, NULL);

		/* See if another thread beat us */
		if (uvp->uv_flags & UVNODE_FLAG_DIRECTIO) {
			VOP_RWUNLOCK(vp, V_WRITELOCK_TRUE, NULL);
			return (0);
		}

		if (vn_has_cached_data(vp)) {
			error = VOP_PUTPAGE(vp, (offset_t)0, (uint_t)0,
			    B_INVAL, cr, NULL);
			if (error) {
				VOP_RWUNLOCK(vp, V_WRITELOCK_TRUE, NULL);
				return (error);
			}
		}

		mutex_enter(&uvp->uv_lock);
		uvp->uv_flags |= UVNODE_FLAG_DIRECTIO;
		mutex_exit(&uvp->uv_lock);
		VOP_RWUNLOCK(vp, V_WRITELOCK_TRUE, NULL);
		return (0);
	}

	if (cmd == DIRECTIO_OFF) {
		mutex_enter(&uvp->uv_lock);
		uvp->uv_flags &= ~UVNODE_FLAG_DIRECTIO;
		mutex_exit(&uvp->uv_lock);
		return (0);
	}

	return (EINVAL);
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
uvfs_expldev(dev_t dev)
{
#ifndef _LP64
	major_t major = (major_t)dev >> NBITSMINOR32 & MAXMAJ32;
	return (((uint64_t)major << NBITSMINOR64) |
	    ((minor_t)dev & MAXMIN32));
#else
	return (dev);
#endif
}

dev_t
uvfs_cmpldev(uint64_t dev)
{
#ifndef _LP64
	minor_t minor = (minor_t)dev & MAXMIN64;
	major_t major = (major_t)(dev >> NBITSMINOR64) & MAXMAJ64;

	if (major > MAXMAJ32 || minor > MAXMIN32)
		return (NODEV32);

	return (((dev32_t)major << NBITSMINOR32) | minor);
#else
	return (dev);
#endif
}
