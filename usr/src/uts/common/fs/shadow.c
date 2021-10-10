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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Shadow data migration
 *
 * This file provides a common infrastructure for transparently migrating data
 * from one filesystem to another.  New data is written only to the active
 * filesystem, and data is pulled on demand from the shadowed filesystem.  The
 * shadowed filesystem must be static, or the results are undefined.  The
 * anticipated use is to front remote filesystems until the data is copied over
 * and then decommission the old filesystem.  The implementation is generic,
 * however, and can support shadowing any directory.
 *
 * Shadow filesystems are created by specifying the 'shadow' property in the
 * mount options to any filesystem.  This mount option associates a shadow
 * vnode with the vfs_t, and sets a boolean value in the vfs_t.  We then
 * interpose on every vnode operation to first check this boolean value.  If
 * it's set, then we jump into our shadow handling, otherwise we continue with
 * the standard operation.
 *
 * Shadow migration works by associating an extended attribute with each file
 * or directory that indicates that it has not yet been copied locally.  When a
 * filesystem is first mounted with the 'shadow' option, we we this attribute
 * for the root of the filesystem.  Whenever we interpose on a vnode operation,
 * we check for this extended attribute.  If it's not set, then we know it's
 * been copied locally and we just return the local vnode.  Otherwise, we
 * populate the file or directory from the shadow filesystem, and then clear
 * the attribute to indicate that all the data is present locally.  Files can
 * be migrated in smaller chunks by maintaining a space map as an extended
 * attribute (SUNWshadow.map).  This represents what portions of the file have
 * yet to be migrated.  Only when all the contents of a file have been migrated
 * are the attributes removed.
 *
 * When populating a directory, we iterate over the entries and get the
 * attributes of each one.   For symlinks and device nodes, we create the local
 * objects and continue.  For files or directories, we create an empty object
 * (file or directory) and set our shadow attribute to indicate that next time
 * through we need to copy the data locally.  While it is theoretically
 * possible to lazily load files as needed (bringing in only portions of the
 * data), this complicates the implementation.  In addition, we always need to
 * copy the entire file when looking up attributes, because we don't know how
 * much data is going to be used on-disk.
 *
 * Hard links present an interesting problem, as we don't want to recreate a
 * new instance of the file.  On the source, we know that there is more than
 * one link, as well as the fsid and fid of the file.  On the destination, we
 * need to map this to a local fid so that we can make hard links to the same
 * file in the future.  We also need to make sure that we recreate the file
 * should the original file get removed in the interim.  In order to accomplish
 * this, we need to have a transient hard link to the file that is named by the
 * (fsid, fid) pair on the source, which is removed once the shadow migration
 * completes.  Because extended attributes cannot be links to regular files, we
 * need to carve out a small portion of the namespace by creating a normal
 * directory in the root of the filesystem that will store the transient hard
 * links.  One wrinkle to this strategy is that the fsid is not persistent, so
 * in the case where the source spans multiple filesystems, we need to persist
 * an index that is associated with the path so that when the system reboots,
 * we can lookup the remote path and get the new fsid.  For cases where the FID
 * changes (as when the source is re-created from backup), we also store the
 * path of the original hard link, which can be used to verify that the FID is
 * still correct, as well as allowing the hard links to be updated to the new
 * values.
 *
 * Another problem is knowing when a filesystem has been successfully migrated.
 * We let userland tools control the frequency and aggressiveness of the
 * background migration, but it is impossible to know if a traversal of the
 * filesystem visits every node.  For any traversal algorithm, it is possible
 * to rename a portion of the tree at a point where the traversal will fail to
 * notice the renamed file or directory.  To compensate for this, we keep track
 * of all unvisited SUNWshadow attributes persistently with the filesystem.
 * This list must be appended to before writing any shadow attribute.  As nodes
 * are migrated, we remove the entries from our in-core list.  Periodically, we
 * atomically rewrite the persistent copy, so the on-disk version does not grow
 * infinitely.  At the end of the normal tree traversal, the userland consumer
 * can then do individual fcntl() calls to migrated individual directories or
 * files off this list until there is nothing left.  If we visit a file that no
 * longer has the attribute or doesn't exist, then we just ignore it.  This
 * allows the on-disk version to be a superset of possible vnodes left, and can
 * therefore be slightly out of date w.r.t. to the actual set of remaining
 * vnodes.
 */

#include <sys/types.h>

#include <sys/acl.h>
#include <sys/attr.h>
#include <sys/ddi.h>
#include <sys/dirent.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/filio.h>
#include <sys/fs/shadow.h>
#include <sys/pathname.h>
#include <sys/disp.h>
#include <sys/sdt.h>
#include <sys/sunddi.h>
#include <sys/sysmacros.h>

#include <c2/audit.h>

boolean_t vfs_shadow_disable;
boolean_t vfs_shadow_rotate_disable;
boolean_t vfs_shadow_spacemap_disable;

/*
 * We want to do block-size transfers to the filesystem, but we also don't want
 * to do lots of small updates when doing streaming operations, so we set a
 * minimum transfer size.  It's currently the case that this is larger than any
 * possible block size for supported local filesystems, but this is not a
 * requirement.
 */
int vfs_shadow_min_xfer_size = 128 * 1024;

/*
 * Normally all migration is done with vfs_shadow_cred set to 'kcred'.   This
 * makes it difficult to simulate permission problems, as vfs_shadow_cred
 * always has FILE_DAC_READ and friends.  We export this variable for testing
 * purposes so we can override it temporarily, but under normal operation it's
 * always 'kcred'.
 */
cred_t *vfs_shadow_cred;

/*
 * Flags to vfs_shadow_check_impl()
 */
typedef enum {
	SHADOW_CHECK_READ_HELD = 0x01,
	SHADOW_CHECK_DONTBLOCK = 0x02,
	SHADOW_CHECK_RW_HELD = 0x04,
	SHADOW_CHECK_NONCONTENT = 0x08
} shadow_check_flags_t;

static int Shadow_max_dirreclen = DIRENT64_RECLEN(MAXPATHLEN);

static int vfs_shadow_migrate_file_vp(vnode_shadow_t *, vnode_t **,
    const char *, uint64_t, uint64_t, uint64_t *);
static int vfs_shadow_migrate_dir_vp(vnode_t *, vnode_t *, const char *,
    uint64_t *);
static void vfs_shadow_timeout(void *);
static int vfs_shadow_open_space_map(vnode_t *, vnode_t **, uint64_t *,
    uint64_t *, boolean_t);
static void vfs_shadow_close_space_map(vnode_t *);
static int vfs_shadow_check_impl(vnode_t *, int *, uint64_t, uint64_t,
    uint64_t *, shadow_check_flags_t);
static v_shadowstate_t vfs_shadow_retrieve(vnode_t *, vnode_shadow_t **);
static v_shadowstate_t vfs_shadow_instantiate(vnode_t *, vnode_shadow_t **);

uint32_t vfs_shadow_timeout_freq = 10;		/* seconds */

/*
 * Vnode-specific data field keys
 */
uint_t vfs_shadow_key_state;
uint_t vfs_shadow_key_data;

kmem_cache_t *vfs_shadow_cache;

static void
shadow_new_state(vnode_shadow_t *vsdp, v_shadowstate_t state)
{
	vnode_t *vp = vsdp->vns_vnode;

	mutex_enter(&(vp->v_vsd_lock));
	VERIFY(vsd_set(vp, vfs_shadow_key_state, (void *)state) == 0);
	cv_broadcast(&(vsdp->vns_shadow_state_cv));
	mutex_exit(&(vp->v_vsd_lock));
}

static void
shadow_new_state_vp(vnode_t *vp, v_shadowstate_t state)
{
	vnode_shadow_t *vsdp;
	mutex_enter(&(vp->v_vsd_lock));
	vsdp = (vnode_shadow_t *)vsd_get(vp, vfs_shadow_key_data);
	ASSERT(vsdp != NULL);
	VERIFY(vsd_set(vp, vfs_shadow_key_state, (void *)state) == 0);
	cv_broadcast(&(vsdp->vns_shadow_state_cv));
	mutex_exit(&(vp->v_vsd_lock));
}

static void
shadow_unset_migrating_self(vnode_shadow_t *vsdp)
{
	v_shadowstate_t state;
	vnode_t *vp = vsdp->vns_vnode;

	mutex_enter(&(vp->v_vsd_lock));
	state = (v_shadowstate_t)vsd_get(vp, vfs_shadow_key_state);
	if (state == V_SHADOW_MIGRATING_SELF) {
		state = V_SHADOW_UNKNOWN;
		VERIFY(vsd_set(vp, vfs_shadow_key_state, (void *)state) == 0);
	}
	cv_broadcast(&(vsdp->vns_shadow_state_cv));
	mutex_exit(&(vp->v_vsd_lock));
}

static void
vnode_assert_not_migrating_self(vnode_t *vp)
{
	v_shadowstate_t state;
	vnode_shadow_t *vsdp;

	mutex_enter(&(vp->v_vsd_lock));
	state = (v_shadowstate_t)vsd_get(vp, vfs_shadow_key_state);
	vsdp = (vnode_shadow_t *)vsd_get(vp, vfs_shadow_key_data);
	ASSERT(state != V_SHADOW_MIGRATING_SELF ||
	    (vsdp != NULL && MUTEX_HELD(&(vsdp->vns_shadow_content_lock))));
	mutex_exit(&(vp->v_vsd_lock));
}

static boolean_t
vnode_is_migrated(vnode_t *vp)
{
	v_shadowstate_t vstate;
	mutex_enter(&vp->v_vsd_lock);
	vstate = (v_shadowstate_t)vsd_get(vp, vfs_shadow_key_state);
	mutex_exit(&vp->v_vsd_lock);
	return (vstate == V_SHADOW_MIGRATED);
}

/*
 * Wrappers around the standard read/write interfaces to make life a little
 * easier.
 */
static int
vfs_shadow_read(vnode_t *vp, void *buf, size_t buflen, offset_t offset)
{
	return (vn_rdwr(UIO_READ, vp, (caddr_t)buf, buflen, offset,
	    UIO_SYSSPACE, 0, RLIM64_INFINITY, kcred, NULL));
}

static int
vfs_shadow_append(vnode_t *vp, void *buf, size_t buflen)
{
	return (vn_rdwr(UIO_WRITE, vp, (caddr_t)buf, buflen, 0,
	    UIO_SYSSPACE, FAPPEND | FDSYNC, RLIM64_INFINITY, kcred, NULL));
}

static int
vfs_shadow_append_async(vnode_t *vp, void *buf, size_t buflen)
{
	return (vn_rdwr(UIO_WRITE, vp, (caddr_t)buf, buflen, 0,
	    UIO_SYSSPACE, FAPPEND, RLIM64_INFINITY, kcred, NULL));
}

static int
vfs_shadow_append_string(vnode_t *vp, const char *str)
{
	return (vfs_shadow_append(vp, (void *)str, strlen(str)));
}

/*
 * Create a file, write a string, and close it.
 */
static int
vfs_shadow_create_and_write(vnode_t *dirvp, const char *name,
    const char *contents)
{
	int error;
	vattr_t vattr;
	vnode_t *vp;

	vattr.va_type = VREG;
	vattr.va_mode = 0600;
	vattr.va_mask = AT_TYPE|AT_MODE;

	if ((error = VOP_CREATE(dirvp, (char *)name, &vattr, NONEXCL, VWRITE,
	    &vp, kcred, 0, NULL, NULL)) != 0)
		return (error);

	if ((error = VOP_OPEN(&vp, FWRITE, kcred, NULL)) != 0) {
		VN_RELE(vp);
		return (error);
	}

	if ((error = vfs_shadow_append_string(vp, contents)) != 0) {
		(void) VOP_CLOSE(vp, FWRITE, 1, 0, kcred, NULL);
		VN_RELE(vp);
		return (error);
	}

	(void) VOP_CLOSE(vp, FWRITE, 1, 0, kcred, NULL);
	VN_RELE(vp);

	return (0);
}

/*
 * Open a file, read the contents, and return a string.
 */
static char *
vfs_shadow_open_and_read(vnode_t *dirvp, const char *name, int *errp,
    boolean_t lookuperr)
{
	char *contents;
	vnode_t *vp;
	vattr_t vattr;
	int err;

	if ((err = VOP_LOOKUP(dirvp, (char *)name, &vp, NULL, 0, NULL,
	    kcred, NULL, NULL, NULL)) != 0) {
		if (lookuperr)
			*errp = err;
		return (NULL);
	}

	if ((*errp = VOP_OPEN(&vp, FREAD, kcred, NULL)) != 0) {
		VN_RELE(vp);
		return (NULL);
	}

	if ((*errp = VOP_GETATTR(vp, &vattr, 0, kcred, NULL)) != 0) {
		(void) VOP_CLOSE(vp, FREAD, 1, 0, kcred, NULL);
		VN_RELE(vp);
		return (NULL);
	}

	contents = kmem_alloc(vattr.va_size + 1, KM_SLEEP);

	if ((*errp = vfs_shadow_read(vp, contents, vattr.va_size, 0)) != 0) {
		kmem_free(contents, vattr.va_size + 1);
		(void) VOP_CLOSE(vp, FREAD, 1, 0, kcred, NULL);
		VN_RELE(vp);
		return (NULL);
	}

	contents[vattr.va_size] = '\0';

	/*
	 * Don't allow for embedded null characters.
	 */
	if (strlen(contents) != vattr.va_size) {
		kmem_free(contents, vattr.va_size + 1);
		(void) VOP_CLOSE(vp, FREAD, 1, 0, kcred, NULL);
		VN_RELE(vp);
		*errp = EIO;
		return (NULL);
	}

	(void) VOP_CLOSE(vp, FREAD, 1, 0, kcred, NULL);
	VN_RELE(vp);

	return (contents);
}

/*
 * Return a pointer to a directory within the private stash (.SUNWshadow in the
 * root directory).  If the directory doesn't exist, then it's created as part
 * of this process.
 */
static int
vfs_shadow_private_lookup(vfs_t *vfsp, const char *subdir, vnode_t **vpp)
{
	int error;
	vnode_t *rootdir;
	vnode_t *dirvp = NULL;
	vattr_t vattr;

	rw_enter(&vfsp->vfs_shadow_lock, RW_READER);
	if (vfsp->vfs_shadow && vfsp->vfs_shadow->vs_shadow_dir)
		VN_HOLD(dirvp = vfsp->vfs_shadow->vs_shadow_dir);
	rw_exit(&vfsp->vfs_shadow_lock);

	if (dirvp == NULL) {
		if ((error = VFS_ROOT(vfsp, &rootdir)) != 0)
			return (error);

		if ((error = VOP_LOOKUP(rootdir, VFS_SHADOW_PRIVATE_DIR,
		    &dirvp, NULL, 0, NULL, kcred, NULL, NULL, NULL)) != 0) {
			if (error != ENOENT) {
				VN_RELE(rootdir);
				return (error);
			}

			vattr.va_mask = AT_MODE | AT_UID | AT_GID | AT_TYPE;
			vattr.va_uid = vattr.va_gid = 0;
			vattr.va_mode = 0700;
			vattr.va_type = VDIR;

			if ((error = VOP_MKDIR(rootdir, VFS_SHADOW_PRIVATE_DIR,
			    &vattr, &dirvp, kcred, NULL, 0, NULL)) != 0) {
				VN_RELE(rootdir);
				return (error);
			}
		}

		/* cache the shadow_dir if we can, but don't wait */
		if (rw_tryenter(&vfsp->vfs_shadow_lock, RW_WRITER) != 0) {
			if (vfsp->vfs_shadow &&
			    vfsp->vfs_shadow->vs_shadow_dir == NULL) {
				VN_HOLD(dirvp);
				vfsp->vfs_shadow->vs_shadow_dir = dirvp;
			}
			rw_exit(&vfsp->vfs_shadow_lock);
		}
		VN_RELE(rootdir);
	}

	if ((error = VOP_LOOKUP(dirvp, (char *)subdir, vpp, NULL, 0,
	    NULL, kcred, NULL, NULL, NULL)) != 0) {
		if (error != ENOENT) {
			VN_RELE(dirvp);
			return (error);
		}

		vattr.va_mask = AT_MODE | AT_UID | AT_GID | AT_TYPE;
		vattr.va_uid = vattr.va_gid = 0;
		vattr.va_mode = 0700;
		vattr.va_type = VDIR;

		if ((error = VOP_MKDIR(dirvp, (char *)subdir, &vattr,
		    vpp, kcred, NULL, 0, NULL)) != 0) {
			VN_RELE(dirvp);
			return (error);
		}
	}

	VN_RELE(dirvp);

	return (0);
}

/*
 * The list of possible shadow vnodes is kept in-core as well as on disk, as
 * described in the introductory text.  This pending log has some interesting
 * properties:
 *
 * 	1. Attributes must be added to the pending log before the parent
 *	   directory's attribute is removed.
 *
 * 	2. Attribute removal can be lazy and done after the fact.
 *
 *	3. The full list is only needed when processing extra entries, which
 *	   only happens when the filesystem migration is presumed complete, and
 *	   therefore the list should be small.
 *
 * To accomodate these characteristics, we use a very simple on-disk format -
 * an array of fid_t structures with a header.  While FIDs are variable length,
 * they have a maximum size limit, and the fixed size makes certain operations
 * (namely pulling the last entry from the list) possible.  Since we don't
 * expect extremely long lists (userland code does depth-first traversal), and
 * the data structures are small, the wasted space is immaterial.
 *
 * To keep these data structures, we have the following data structures:
 *
 * 	vs_active	The current file to which entries are appended.  We
 * 			employ a double-buffering algorithm, so that we can
 * 			re-write one version of the file while the other is
 * 			still active.  The vs_active_idx controls which file is
 * 			active.
 *
 *
 * 	vs_removed	An AVL tree of FIDs that have been removed from the
 * 			list.  There are three lists (one active and two
 * 			buffered lists).  The format and use of these lists is
 * 			described below.
 */

static void
vfs_shadow_pending_close_vp(vnode_t *vp)
{
	(void) VOP_CLOSE(vp, FREAD|FWRITE, 1, 0, kcred, NULL);
	VN_RELE(vp);
}

/*
 * Creates a new instance of the pending log, SUNWshadow_list.<idx>, returning
 * the result in 'vpp'.
 */
static int
vfs_shadow_pending_create(vfs_t *vfsp, int idx, boolean_t temporary,
    vnode_t **vpp)
{
	vnode_t *dirvp, *vp;
	vattr_t vattr;
	int error;
	char name[6];
	vfs_shadow_header_t hdr;

	ASSERT(idx == 0 || idx == 1);
	if (temporary)
		(void) snprintf(name, sizeof (name), "%d.tmp", idx);
	else
		(void) snprintf(name, sizeof (name), "%d", idx);

	if ((error = vfs_shadow_private_lookup(vfsp,
	    VFS_SHADOW_PRIVATE_PENDING, &dirvp)) != 0)
		return (error);
	ASSERT(dirvp->v_vfsp == vfsp);

	(void) VOP_REMOVE(dirvp, name, kcred, NULL, 0);

	vattr.va_type = VREG;
	vattr.va_mode = 0600;
	vattr.va_mask = AT_TYPE|AT_MODE;

	if ((error = VOP_CREATE(dirvp, name, &vattr, NONEXCL, VWRITE,
	    &vp, kcred, 0, NULL, NULL)) != 0) {
		VN_RELE(dirvp);
		return (error);
	}
	ASSERT(vp->v_vfsp == vfsp);

	/*
	 * Open the file and create the header.
	 */
	if ((error = VOP_OPEN(&vp, FREAD|FWRITE, kcred, NULL)) != 0) {
		VN_RELE(vp);
		VN_RELE(dirvp);
		return (error);
	}
	ASSERT(vp->v_vfsp == vfsp);

	bzero(&hdr, sizeof (hdr));

	hdr.vsh_magic = VFS_SHADOW_ATTR_LIST_MAGIC;
	hdr.vsh_version = VFS_SHADOW_INTENT_VERSION;
#if defined(_BIG_ENDIAN)
	hdr.vsh_bigendian = B_TRUE;
#endif

	if ((error = vfs_shadow_append(vp, &hdr, sizeof (hdr))) != 0) {
		vfs_shadow_pending_close_vp(vp);
		(void) VOP_REMOVE(dirvp, name, kcred, NULL, 0);
		VN_RELE(dirvp);
		return (error);
	}

	VN_RELE(dirvp);
	*vpp = vp;
	return (0);
}

/*
 * Renames a temporary file to its current name.
 */
static int
vfs_shadow_pending_rename(vfs_t *vfsp, int idx)
{
	int error;
	char tmpname[6];
	char fullname[6];
	vnode_t *dirvp;

	(void) snprintf(tmpname, sizeof (tmpname), "%d.tmp", idx);
	(void) snprintf(fullname, sizeof (fullname), "%d", idx);

	if ((error = vfs_shadow_private_lookup(vfsp,
	    VFS_SHADOW_PRIVATE_PENDING, &dirvp)) != 0)
		return (error);

	error = VOP_RENAME(dirvp, tmpname, dirvp, fullname, kcred, NULL, 0);

	VN_RELE(dirvp);

	return (error);
}

/*
 * Opens an pending log, creating a new one if necessary.
 */
static int
vfs_shadow_pending_open(vfs_t *vfsp, int idx, vnode_t **vpp)
{
	vnode_t *dirvp, *lvp;
	int error;
	char name[6];

	if ((error = vfs_shadow_private_lookup(vfsp,
	    VFS_SHADOW_PRIVATE_PENDING, &dirvp)) != 0)
		return (error);

	ASSERT(idx == 0 || idx == 1);
	(void) snprintf(name, sizeof (name), "%d", idx);

	if ((error = VOP_LOOKUP(dirvp, name,
	    &lvp, NULL, 0, NULL, kcred, NULL, NULL, NULL)) != 0) {
		VN_RELE(dirvp);

		if (error != ENOENT)
			return (error);

		return (vfs_shadow_pending_create(vfsp,
		    idx, B_FALSE, vpp));
	}

	VN_RELE(dirvp);

	if ((error = VOP_OPEN(&lvp, FWRITE|FREAD, kcred, NULL)) != 0) {
		VN_RELE(lvp);
		return (error);
	}

	/* XXX validate contents */

	*vpp = lvp;

	return (0);
}

/*
 * Closes the current pending log, if we are in unmount mode (see
 * vfs_shadow_pre_unmount).
 */
static void
vfs_shadow_pending_close(vfs_shadow_t *vsp)
{
	if (vsp->vs_active != NULL) {
		vfs_shadow_pending_close_vp(vsp->vs_active);
		vsp->vs_active = NULL;
	}
}

/*
 * Adds the given vnode to the pending log.
 */
static int
vfs_shadow_pending_add(vfs_shadow_t *vsp, vnode_t *vp)
{
	fid_t fid;
	int error;

	bzero(&fid, sizeof (fid));
	fid.fid_len = MAXFIDSZ;

	if ((error = VOP_FID(vp, &fid, NULL)) != 0)
		return (error);

	mutex_enter(&vsp->vs_lock);

	if (vsp->vs_active == NULL &&
	    (error = vfs_shadow_pending_open(vp->v_vfsp,
	    vsp->vs_active_idx, &vsp->vs_active)) != 0)
		goto out;

	ASSERT(vsp->vs_active != NULL);

	/*
	 * Note that sizeof (fid) will be different between 32-bit and 64-bit
	 * kernels due to alignment, so we explicitly only write out the _fid
	 * portion (which is always fixed).
	 *
	 * Remove when 32-bit kernels go away...
	 */
	error = vfs_shadow_append(vsp->vs_active, &fid.un._fid,
	    sizeof (fid.un._fid));

	if (vsp->vs_close_on_update)
		vfs_shadow_pending_close(vsp);

out:
	mutex_exit(&vsp->vs_lock);

	return (error);
}

static void
vfs_shadow_pending_remove_fid(vfs_shadow_t *vsp, fid_t *fidp)
{
	vfs_shadow_remove_entry_t *rp;
	avl_tree_t *tree = &vsp->vs_removed[vsp->vs_removed_idx];

	ASSERT(MUTEX_HELD(&vsp->vs_lock));

	/*
	 * Clear any persistent notion of errors, as this indicates we were
	 * successfully able to migrate an entry.
	 */
	vsp->vs_error_valid[0] = vsp->vs_error_valid[1] = B_FALSE;

	rp = kmem_zalloc(sizeof (vfs_shadow_remove_entry_t), KM_SLEEP);
	bcopy(fidp, &rp->vsre_fid, sizeof (fid_t));

	if (avl_find(tree, rp, NULL) != NULL)
		kmem_free(rp, sizeof (vfs_shadow_remove_entry_t));
	else
		avl_add(tree, rp);
}

static void
vfs_shadow_pending_remove(vfs_shadow_t *vsp, vnode_t *vp)
{
	fid_t fid;

	fid.fid_len = MAXFIDSZ;
	if (VOP_FID(vp, &fid, NULL) != 0)
		return;

	mutex_enter(&vsp->vs_lock);
	vfs_shadow_pending_remove_fid(vsp, &fid);
	mutex_exit(&vsp->vs_lock);
}

static int
vfs_shadow_fid_compare(const fid_t *fa, const fid_t *fb)
{
	int i;

	if (fa->fid_len < fb->fid_len)
		return (-1);
	else if (fa->fid_len > fb->fid_len)
		return (1);

	for (i = 0; i < fa->fid_len; i++) {
		if (fa->fid_data[i] == fb->fid_data[i])
			continue;

		if (fa->fid_data[i] < fb->fid_data[i])
			return (-1);
		else
			return (1);
	}

	return (0);
}

static int
vfs_shadow_pending_rewrite(vnode_t *newvp, vnode_t *oldvp,
    avl_tree_t *primary, avl_tree_t *secondary)
{
	int error;
	vfs_shadow_t *vsp;
	vattr_t attr;
	offset_t offset;
	vfs_shadow_remove_entry_t vr;
	size_t fidlen = sizeof (vr.vsre_fid.un._fid);
	boolean_t append_last;

	vsp = newvp->v_vfsp->vfs_shadow;

	attr.va_mask = AT_SIZE;
	if ((error = VOP_GETATTR(oldvp, &attr, 0, kcred, NULL)) != 0)
		return (error);

	/*
	 * Read in all entries from the previous pending list, and write them
	 * out only if they don't exist in either of the removed AVL trees.
	 */
	append_last = B_FALSE;
	for (offset = sizeof (vfs_shadow_header_t);
	    offset < attr.va_size; offset += fidlen) {

		if ((error = vfs_shadow_read(oldvp, &vr.vsre_fid, fidlen,
		    offset)) != 0)
			return (error);

		if (avl_find(primary, &vr, NULL) != NULL ||
		    avl_find(secondary, &vr, NULL) != NULL)
			continue;

		/*
		 * The 'last_processed' fid indicates the last request pulled
		 * from the start of the pending list.  If this is set, then we
		 * want to make sure that this ends up at the end of the list,
		 * so that if there was an error we move onto the next record.
		 */
		if (vfs_shadow_fid_compare(&vr.vsre_fid,
		    &vsp->vs_last_processed) == 0) {
			append_last = B_TRUE;
			continue;
		}

		if ((error = vfs_shadow_append_async(newvp, &vr.vsre_fid,
		    fidlen)) != 0)
			return (error);
	}

	if (append_last) {
		if ((error = vfs_shadow_append_async(newvp,
		    &vsp->vs_last_processed, fidlen)) != 0)
			return (error);
	}

	(void) VOP_FSYNC(newvp, 0, kcred, NULL);

	return (0);
}

static void
vfs_shadow_pending_collapse(vfs_t *vfsp, boolean_t force)
{
	vfs_shadow_t *vsp = vfsp->vfs_shadow;
	avl_tree_t *primary, *secondary;
	int idx, err;
	vnode_t *prev, *tmp;
	vfs_shadow_remove_entry_t *vrp;
	vnode_t *rootdir;

	/*
	 * Make sure the root directory has been successfully migrated before
	 * attempting to collapse the pending lists.  Otherwise, we can
	 * deadlock when trying to migrate the directory with vs_lock held.
	 * By virtue of having a hold on the vnode and knowing that
	 * the state is not migrated, we can bypass all shadow migration code
	 * elsewhere.
	 */
	if (VFS_ROOT(vfsp, &rootdir) != 0)
		return;

	/*
	 * We specify SHADOW_CHECK_DONTBLOCK here not because we expect it to
	 * block, but because vfs_shadow_check_impl() will
	 * transparently allow access to the root directory in standby
	 * mode.  If we happen to unmount a filesystem in standby
	 * mode, then we'll blow our assertion below.  If we haven't
	 * gotten out of standby mode, there is nothing to collapse so
	 * we should bail out anyway.  If the root directory is still
	 * not fully migrated, this is not the time to work on it.
	 */
	if (vfs_shadow_check_impl(rootdir, &err, 0, -1ULL, NULL,
	    SHADOW_CHECK_RW_HELD | SHADOW_CHECK_DONTBLOCK) != 0) {
		VN_RELE(rootdir);
		return;
	}
	vnode_assert_not_migrating_self(rootdir);

	ASSERT(vnode_is_migrated(rootdir) || vfs_shadow_disable);

	/*
	 * Make sure a remount has not snuck in and NULLed out our
	 * shadow pointer
	 */
	if (vsp == NULL) {
		VN_RELE(rootdir);
		return;
	}

	mutex_enter(&vsp->vs_resync_lock);
	mutex_enter(&vsp->vs_lock);

	primary = &vsp->vs_removed[vsp->vs_removed_idx];
	secondary = &vsp->vs_removed[(vsp->vs_removed_idx + 3 - 1) % 3];

	/*
	 * If the primary and secondary lists are empty, there is nothing to
	 * do.  However, we must make sure to close and release the current
	 * active list, as this may have been called from
	 * vfs_shadow_pre_unmount(), which is relying on us to return with no
	 * vnodes open.
	 */
	if (!force && (avl_first(primary) == NULL &&
	    avl_first(secondary) == NULL)) {
		vfs_shadow_pending_close(vsp);
		VN_RELE(rootdir);
		mutex_exit(&vsp->vs_lock);
		mutex_exit(&vsp->vs_resync_lock);
		return;
	}

	/*
	 * We create this first because it is the most likely thing to fail and
	 * we don't yet have any state to unwind.
	 */
	if (vfs_shadow_pending_create(vfsp, vsp->vs_active_idx,
	    B_TRUE, &tmp) != 0) {
		VN_RELE(rootdir);
		mutex_exit(&vsp->vs_lock);
		mutex_exit(&vsp->vs_resync_lock);
		return;
	}

	/*
	 * We have to make sure that active file is currently open for
	 * vfs_shadow_pending_rewrite() to work.
	 */
	if (vsp->vs_active == NULL &&
	    vfs_shadow_pending_open(vfsp, vsp->vs_active_idx,
	    &vsp->vs_active) != 0) {
		VN_RELE(rootdir);
		mutex_exit(&vsp->vs_lock);
		mutex_exit(&vsp->vs_resync_lock);
		return;
	}

	/*
	 * Record the index of the current file and switch the active index.
	 * index.
	 */
	idx = vsp->vs_active_idx;
	vsp->vs_active_idx = !vsp->vs_active_idx;

	/*
	 * Reset the current remove index.
	 */
	vsp->vs_removed_idx = (vsp->vs_removed_idx + 1) % 3;

	ASSERT(avl_first(&vsp->vs_removed[vsp->vs_removed_idx]) == NULL);
	ASSERT(vsp->vs_active != NULL);

	prev = vsp->vs_active;
	vsp->vs_active = NULL;

	mutex_exit(&vsp->vs_lock);

	/*
	 * At this point, normal operation can continue operating on the active
	 * file and active removed list.  As long as we hold the resync lock,
	 * we have exclusive control over the previous file and the primary
	 * and secondary lists.
	 */
	(void) vfs_shadow_pending_rewrite(tmp, prev, primary, secondary);
	/* XXX what to do if this fails? */

	vfs_shadow_pending_close_vp(prev);

	(void) vfs_shadow_pending_rename(vfsp, idx);
	/* XXX what to do if this fails? */

	vfs_shadow_pending_close_vp(tmp);

	while ((vrp = avl_first(secondary)) != NULL) {
		avl_remove(secondary, vrp);
		kmem_free(vrp, sizeof (vfs_shadow_remove_entry_t));
	}

	VN_RELE(rootdir);

	mutex_exit(&vsp->vs_resync_lock);
}

typedef struct vfs_shadow_timeout_data {
	vfs_t		*vst_vfsp;
	vfs_shadow_t	*vst_vsp;
	uint64_t	vst_gen;
	kcondvar_t	vst_cv;
	kthread_t	*vst_thread;
	kt_did_t	vst_tid;
	int		vst_flags;
} vfs_shadow_timeout_data_t;

enum vstd_flags {
	VST_FLAGS_NONE	= 0,
	VST_FLAGS_GO	= 1,
	VST_FLAGS_QUIT	= 2
};

static void
vfs_shadow_timeout_worker(void *data)
{
	vfs_shadow_timeout_data_t *vtp = data;
	vfs_t *vfsp = vtp->vst_vfsp;
	vfs_shadow_t *vsp = vtp->vst_vsp;

	for (;;) {
		mutex_enter(&vsp->vs_timeout_lock);
		while (vtp->vst_flags == VST_FLAGS_NONE) {
			if (!cv_wait_sig(&vtp->vst_cv, &vsp->vs_timeout_lock)) {
				mutex_exit(&vsp->vs_timeout_lock);
				thread_exit();
			}
		}

		if (vtp->vst_flags & VST_FLAGS_QUIT) {
			mutex_exit(&vsp->vs_timeout_lock);
			thread_exit();
		}

		vtp->vst_flags = VST_FLAGS_NONE;

		/*
		 * We don't rotate the pending logs while processing
		 * items from the pending list because we rely on being able
		 * to remove the entry from the list we started with.
		 * Need to grab vs_lock for a short time here because we
		 * are checking vs_process_inprogress.
		 */
		mutex_enter(&vsp->vs_lock);
		if (vsp->vs_gen != vtp->vst_gen || vsp->vs_process_inprogress) {
			mutex_exit(&vsp->vs_lock);
			goto out;
		}
		mutex_exit(&vsp->vs_lock);
		mutex_exit(&vsp->vs_timeout_lock);

		if (!vfs_shadow_rotate_disable) {
			rw_enter(&vfsp->vfs_shadow_lock, RW_READER);
			vfs_shadow_pending_collapse(vfsp, B_FALSE);
			rw_exit(&vfsp->vfs_shadow_lock);
		}

		mutex_enter(&vsp->vs_timeout_lock);
out:
		if (vsp->vs_gen != vtp->vst_gen) {
			mutex_exit(&vsp->vs_timeout_lock);
			thread_exit();
		} else {
			clock_t udelta = vfs_shadow_timeout_freq * 1000 * 1000;
			vsp->vs_timeout = timeout(vfs_shadow_timeout, vtp,
			    drv_usectohz(udelta));
		}

		mutex_exit(&vsp->vs_timeout_lock);
	}
}

static void
vfs_shadow_timeout(void *data)
{
	vfs_shadow_timeout_data_t *vtp = data;
	vfs_shadow_t *vsp = vtp->vst_vsp;

	mutex_enter(&vsp->vs_timeout_lock);
	vtp->vst_flags |= VST_FLAGS_GO;
	cv_signal(&vtp->vst_cv);
	mutex_exit(&vsp->vs_timeout_lock);
}

/*
 * These functions are used to enable and disable the periodic rotation of
 * shadow pending logs.
 */
static timeout_id_t
vfs_shadow_suspend(vfs_shadow_t *vsp, vfs_shadow_timeout_data_t **data)
{
	timeout_id_t id;

	mutex_enter(&vsp->vs_timeout_lock);

	if (vsp->vs_timeout == NULL) {
		mutex_exit(&vsp->vs_timeout_lock);
		return (NULL);
	}

	/*
	 * We don't actually call untimeout() here because it's difficult to do
	 * with the locks currently held.  Instead, we return the timeout id so
	 * that it can be canceled (via vfs_shadow_free) when the
	 * appropriate locks are dropped.  If the timeout executes after we've
	 * torn down the shadow infrastructure but before we've had a chance to
	 * cancel it, it will notice the generation count and/or index has
	 * changed, and will do nothing.  Callers must call
	 * vfs_shadow_free() immediately before freeing the vfs_t or the
	 * timeout can access bogus memory.
	 */
	vsp->vs_gen++;

	id = vsp->vs_timeout;
	vsp->vs_timeout = NULL;

	ASSERT(vsp->vs_timeout_data != NULL);
	*data = vsp->vs_timeout_data;
	vsp->vs_timeout_data = NULL;

	mutex_exit(&vsp->vs_timeout_lock);

	return (id);
}

static void
vfs_shadow_resume(vfs_t *vfsp)
{
	vfs_shadow_t *vsp = vfsp->vfs_shadow;
	vfs_shadow_timeout_data_t *vtp;

	mutex_enter(&vsp->vs_timeout_lock);
	ASSERT(vsp->vs_timeout == NULL);
	ASSERT(vsp->vs_timeout_data == NULL);

	vtp = kmem_alloc(sizeof (vfs_shadow_timeout_data_t), KM_SLEEP);
	vtp->vst_vfsp = vfsp;
	vtp->vst_vsp = vsp;
	vtp->vst_gen = vsp->vs_gen;
	vtp->vst_flags = VST_FLAGS_NONE;
	cv_init(&vtp->vst_cv, NULL, CV_DEFAULT, NULL);
	vtp->vst_thread = thread_create(NULL, 0, vfs_shadow_timeout_worker,
	    vtp, 0, &p0, TS_RUN, minclsyspri);
	vtp->vst_tid = vtp->vst_thread->t_did;

	vsp->vs_timeout = timeout(vfs_shadow_timeout, vtp,
	    drv_usectohz(vfs_shadow_timeout_freq * 1000 * 1000));
	vsp->vs_timeout_data = vtp;

	mutex_exit(&vsp->vs_timeout_lock);
}

static int
vfs_shadow_pending_compare(const void *a, const void *b)
{
	const vfs_shadow_remove_entry_t *ra = a;
	const vfs_shadow_remove_entry_t *rb = b;

	return (vfs_shadow_fid_compare(&ra->vsre_fid, &rb->vsre_fid));
}

static int
vnode_shadow_range_compare(const void *a, const void *b)
{
	const vnode_shadow_range_t *ra = a;
	const vnode_shadow_range_t *rb = b;

	if (ra->vsr_start < rb->vsr_start)
		return (-1);
	if (ra->vsr_start > rb->vsr_start)
		return (1);
	else
		return (0);
}
/*
 * Given a range, returns the lowest overlapping (empty) space map entry within
 * the given range.  If there is no overlapping ranges, then we return NULL to
 * indicate that the range is currently valid
 */
static vnode_shadow_range_t *
vfs_shadow_lookup_range(vnode_shadow_t *vsdp, uint64_t start, uint64_t end)
{
	vnode_shadow_range_t search, *vrp;
	avl_index_t where;
	boolean_t valid;

	ASSERT(vsdp != NULL);
	ASSERT(MUTEX_HELD(&vsdp->vns_shadow_content_lock));

	search.vsr_start = start;
	vrp = avl_find(&vsdp->vns_space_map, &search, &where);
	if (vrp == NULL)
		vrp = avl_nearest(&vsdp->vns_space_map, where, AVL_BEFORE);
	if (vrp == NULL)
		vrp = avl_nearest(&vsdp->vns_space_map, where, AVL_AFTER);
	ASSERT(vrp != NULL);

	valid = B_TRUE;
	for (; vrp != NULL && end > vrp->vsr_start;
	    vrp = avl_walk(&vsdp->vns_space_map, vrp, AVL_AFTER)) {
		if (start < vrp->vsr_end &&
		    end > vrp->vsr_start) {
			valid = B_FALSE;
			break;
		}
	}

	if (valid)
		return (NULL);

	if (vrp != NULL && vrp->vsr_start == vrp->vsr_end) {
		ASSERT(avl_walk(&vsdp->vns_space_map, vrp, AVL_BEFORE) == NULL);
		ASSERT(avl_walk(&vsdp->vns_space_map, vrp, AVL_AFTER) == NULL);
		return (NULL);
	}

	return (vrp);
}

static boolean_t
vfs_shadow_range_empty(vnode_shadow_t *vsdp)
{
	vnode_shadow_range_t *vrp;

	if ((vrp = avl_first(&vsdp->vns_space_map)) == NULL)
		return (B_TRUE);

	if (vrp->vsr_start == vrp->vsr_end) {
		ASSERT(avl_walk(&vsdp->vns_space_map, vrp, AVL_AFTER) == NULL);
		return (B_TRUE);
	}

	return (B_FALSE);
}

/*
 * After migrating some portion of the file, this function is called to
 * indicate that the given range has now been migrated and doesn't need to be
 * migrated again.  This function can handle the case where the range has
 * already been migrated, in which case no changes are made.
 */
static int
vfs_shadow_remove_range(vnode_shadow_t *vsdp, uint64_t start, uint64_t end,
    boolean_t addrec)
{
	vnode_shadow_range_t *vrp, *newrange, *next;
	int error;
	vnode_t *map;
	vfs_shadow_range_record_t rec;

	if (start == end)
		return (0);

	ASSERT(end > start);

	if ((vrp = vfs_shadow_lookup_range(vsdp, start, end)) == NULL)
		return (0);

	if (addrec) {
		if ((error = vfs_shadow_open_space_map(vsdp->vns_vnode, &map,
		    NULL, NULL, B_FALSE)) != 0)
			return (error);

		rec.vsrr_type = VFS_SHADOW_RANGE_LOCAL;
		rec.vsrr_start = start;
		rec.vsrr_end = end;
		ASSERT(end != start);

		if ((error = vfs_shadow_append(map, &rec,
		    sizeof (rec))) != 0) {
			vfs_shadow_close_space_map(map);
			return (error);
		}

		vfs_shadow_close_space_map(map);
	}

	/*
	 * We must deal with the following possibilities:
	 *
	 * Overlaps the end of a range
	 * 	- Truncate the end of the range
	 *
	 * Overlaps the start of a range
	 * 	- Truncate the beginning of the range
	 *
	 * Encompasses an entire range
	 * 	- Remove the range, unless it's the last range.
	 *
	 * Pokes a hole in an existing range
	 * 	- Truncate the range and add a new one.
	 */

	/*
	 * First check to see if this pokes a hole in the current range.  If
	 * so, add a new range and return, as we know it cannot overlap with
	 * any other range.
	 */
	if (start > vrp->vsr_start && end < vrp->vsr_end) {
		newrange = kmem_alloc(sizeof (vnode_shadow_range_t),
		    KM_SLEEP);
		newrange->vsr_end = vrp->vsr_end;
		newrange->vsr_start = end;

		vrp->vsr_end = start;
		ASSERT(newrange->vsr_end > newrange->vsr_start);
		ASSERT(vrp->vsr_end > vrp->vsr_start);

		avl_add(&vsdp->vns_space_map, newrange);

		ASSERT(vfs_shadow_lookup_range(vsdp, start, end) == NULL);

		return (0);
	}

	/*
	 * Otherwise, walk through the ranges and truncate or remove them as
	 * necessary.
	 */
	while (vrp != NULL) {
		next = avl_walk(&vsdp->vns_space_map, vrp, AVL_AFTER);

		if (start <= vrp->vsr_start && end >= vrp->vsr_end) {
			/*
			 * This entire range is encompassed by this removal.
			 * If this is the last range in the tree, simply set it
			 * to be the empty range.  Otherwise, remove it.
			 */
			if (next == NULL && avl_walk(&vsdp->vns_space_map,
			    vrp, AVL_BEFORE) == NULL) {
				vrp->vsr_end = vrp->vsr_start;
				ASSERT(vfs_shadow_range_empty(vsdp));
				break;
			} else {
				avl_remove(&vsdp->vns_space_map, vrp);
				kmem_free(vrp, sizeof (vnode_shadow_range_t));
			}
		} else if (end > vrp->vsr_start && start <= vrp->vsr_start) {
			/*
			 * This truncates the beginning of the range.
			 */
			vrp->vsr_start = end;
			ASSERT(vrp->vsr_end > vrp->vsr_start);
		} else if (start < vrp->vsr_end && end >= vrp->vsr_end) {
			/*
			 * This truncates the end of the range.
			 */
			vrp->vsr_end = start;
			ASSERT(vrp->vsr_end > vrp->vsr_start);
		} else {
			/*
			 * This doesn't overlap the current range - we're done.
			 */
			break;
		}

		vrp = next;
	}

	ASSERT(vfs_shadow_lookup_range(vsdp, start, end) == NULL);

	return (0);
}

static void
vfs_shadow_alloc_space_map(vnode_shadow_t *vsdp)
{
	vnode_shadow_free(vsdp);

	avl_create(&vsdp->vns_space_map, vnode_shadow_range_compare,
	    sizeof (vnode_shadow_range_t),
	    offsetof(vnode_shadow_range_t, vsr_link));

	vsdp->vns_have_map = B_TRUE;
}

/*
 * Creates a new (empty) space map for the given source vnode.  This will
 * only create an in-core version.  The on-disk version is managed separately.
 */
static int
vfs_shadow_create_space_map(vnode_shadow_t *vsdp)
{
	vnode_shadow_range_t *vrp;
	vattr_t vattr;
	int error;

	bzero(&vattr, sizeof (vattr));
	vattr.va_mask = AT_SIZE | AT_BLKSIZE;
	error = VOP_GETATTR(vsdp->vns_vnode, &vattr, 0, kcred, NULL);
	if (error != 0)
		return (error);

	vfs_shadow_alloc_space_map(vsdp);

	vrp = kmem_alloc(sizeof (vnode_shadow_range_t), KM_SLEEP);
	vrp->vsr_start = 0;
	vrp->vsr_end = vattr.va_size;

	avl_add(&vsdp->vns_space_map, vrp);

	return (0);
}

static int
vfs_shadow_open_space_map(vnode_t *vp, vnode_t **ret, uint64_t *lenp,
    uint64_t *filelen, boolean_t temp)
{
	vnode_t *xdir, *map;
	vfs_shadow_header_t hdr;
	vfs_shadow_range_record_t rec;
	vattr_t vattr;
	int error;
	uint64_t len;
	char *name;

	/*
	 * Lookup the entry in the extended attribute directory.
	 */
	if ((error = VOP_LOOKUP(vp, "", &xdir, NULL, LOOKUP_XATTR, NULL, kcred,
	    NULL, NULL, NULL)) != 0)
		return (error);

	if (temp) {
		name = VFS_SHADOW_MAP ".tmp";
		(void) VOP_REMOVE(xdir, name, kcred, NULL, 0);
	} else {
		name = VFS_SHADOW_MAP;
	}

	if ((error = VOP_LOOKUP(xdir, name, &map, NULL, 0, NULL,
	    kcred, NULL, NULL, NULL)) != 0) {
		/*
		 * Lookup failed.  If this was due to anything but ENOENT, then
		 * propagate the error upwards.
		 */
		if (error != ENOENT) {
			VN_RELE(xdir);
			return (error);
		}

		map = NULL;
	} else {
		if ((error = VOP_OPEN(&map, FREAD|FWRITE, kcred, NULL)) != 0) {
			VN_RELE(xdir);
			return (error);
		}

		/*
		 * Successfully opened the file.  If we can't validate the
		 * contents (likely because we created the file but died before
		 * we could write the initial header), then remove the file and
		 * create a new one.
		 */
		(void) VOP_RWLOCK(map, V_WRITELOCK_FALSE, NULL);
		if ((error = vfs_shadow_read(map, &hdr,
		    sizeof (hdr), 0)) == 0) {
			if (hdr.vsh_magic != VFS_SHADOW_SPACE_MAP_MAGIC ||
			    hdr.vsh_version != VFS_SHADOW_SPACE_MAP_VERSION) {
				error = ENOTSUP;
			}
		}

		VOP_RWUNLOCK(map, V_WRITELOCK_FALSE, NULL);

		if (error != 0) {
			(void) VOP_CLOSE(map, FREAD, 1, 0, kcred, NULL);
			VN_RELE(map);
			map = NULL;
			if (error == ENOTSUP) {
				VN_RELE(xdir);
				return (error);
			}
		}
	}

	/*
	 * If it didn't exist, then create a new space map.
	 */
	if (map == NULL) {
		if (lenp == NULL)
			return (ENOENT);

		vattr.va_type = VREG;
		vattr.va_mode = 0600;
		vattr.va_mask = AT_TYPE|AT_MODE;

		(void) VOP_REMOVE(xdir, name, kcred, NULL, 0);
		if ((error = VOP_CREATE(xdir, name, &vattr,
		    NONEXCL, VWRITE, &map, kcred, 0, NULL, NULL)) != 0) {
			VN_RELE(xdir);
			return (error);
		}

		VN_RELE(xdir);

		if ((error = VOP_OPEN(&map, FREAD|FWRITE, kcred, NULL)) != 0) {
			VN_RELE(map);
			return (error);
		}

		bzero(&hdr, sizeof (hdr));
		hdr.vsh_magic = VFS_SHADOW_SPACE_MAP_MAGIC;
		hdr.vsh_version = VFS_SHADOW_SPACE_MAP_VERSION;

		if ((error = vfs_shadow_append(map, &hdr,
		    sizeof (hdr))) != 0) {
			vfs_shadow_close_space_map(map);
			return (error);
		}
	} else {
		VN_RELE(xdir);
	}

	vattr.va_mask = AT_SIZE;
	if ((error = VOP_GETATTR(vp, &vattr, 0, kcred, NULL)) != 0) {
		vfs_shadow_close_space_map(map);
		return (error);
	}
	len = vattr.va_size;
	if (filelen != NULL)
		*filelen = len;

	/*
	 * Check the size of the file.  If this only has a header, either
	 * because we just created it above, or because we died after creating
	 * the file but before writing the first record, write the record for
	 * the whole file.
	 */
	vattr.va_mask = AT_SIZE;
	if ((error = VOP_GETATTR(map, &vattr, 0, kcred, NULL)) != 0) {
		vfs_shadow_close_space_map(map);
		return (error);
	}

	if (vattr.va_size > sizeof (vfs_shadow_header_t) || temp) {
		if (lenp != NULL)
			*lenp = vattr.va_size;
		*ret = map;
		return (0);
	}

	if (lenp == NULL) {
		vfs_shadow_close_space_map(map);
		return (ENOENT);
	}

	rec.vsrr_type = VFS_SHADOW_RANGE_REMOTE;
	rec.vsrr_start = 0;
	rec.vsrr_end = len;
	ASSERT(len != 0);

	if ((error = vfs_shadow_append(map, &rec, sizeof (rec))) != 0) {
		vfs_shadow_close_space_map(map);
		return (error);
	}

	*lenp = vattr.va_size + sizeof (rec);
	*ret = map;
	return (0);
}

static int
vfs_shadow_rename_space_map(vnode_t *vp)
{
	int error;
	vnode_t *xdir;

	if ((error = VOP_LOOKUP(vp, "", &xdir, NULL, LOOKUP_XATTR, NULL, kcred,
	    NULL, NULL, NULL)) != 0)
		return (error);

	error = VOP_RENAME(xdir, VFS_SHADOW_MAP ".tmp", xdir, VFS_SHADOW_MAP,
	    kcred, NULL, 0);

	VN_RELE(xdir);

	return (error);
}

static void
vfs_shadow_close_space_map(vnode_t *vp)
{
	(void) VOP_CLOSE(vp, FREAD|FWRITE, 1, 0, kcred, NULL);
	VN_RELE(vp);
}

/*
 * Read the contents of the on-disk space map and create an in-core
 * representation of the file.
 */
static int
vfs_shadow_read_space_map(vnode_shadow_t *vsdp)
{
	int error;
	vnode_t *map;
	vnode_t *vp;
	uint64_t len, filelen;
	offset_t offset;
	vfs_shadow_range_record_t rec;
	vnode_shadow_range_t *vrp;
	boolean_t rewrite;

	if (vsdp->vns_have_map)
		return (0);

	vp = vsdp->vns_vnode;

	if ((error = vfs_shadow_open_space_map(vp, &map, &len,
	    &filelen, B_FALSE)) != 0) {
		vnode_shadow_free(vsdp);
		return (error);
	}

	vfs_shadow_alloc_space_map(vsdp);

	rewrite = B_FALSE;
	for (offset = sizeof (vfs_shadow_header_t); offset < len;
	    offset += sizeof (vfs_shadow_range_record_t)) {
		if ((error = vfs_shadow_read(map, &rec, sizeof (rec),
		    offset)) != 0) {
			vfs_shadow_close_space_map(map);
			vnode_shadow_free(vsdp);
			return (error);
		}

		/*
		 * We should never see records with matching start and end
		 * ranges, or ranges where the end is before the start.
		 */
		if (rec.vsrr_start >= rec.vsrr_end) {
			vfs_shadow_close_space_map(map);
			vnode_shadow_free(vsdp);
			return (EINVAL);
		}

		if (rec.vsrr_type == VFS_SHADOW_RANGE_REMOTE) {
			/*
			 * Add a new record to the AVL tree, making sure we
			 * don't bomb out on an invalid record.
			 */
			if (avl_first(&vsdp->vns_space_map) != NULL &&
			    vfs_shadow_lookup_range(vsdp, rec.vsrr_start,
			    rec.vsrr_end) != NULL) {
				vfs_shadow_close_space_map(map);
				vnode_shadow_free(vsdp);
				return (EINVAL);
			}

			vrp = kmem_alloc(sizeof (vnode_shadow_range_t),
			    KM_SLEEP);
			vrp->vsr_start = rec.vsrr_start;
			vrp->vsr_end = rec.vsrr_end;

			avl_add(&vsdp->vns_space_map, vrp);
		} else if (rec.vsrr_type == VFS_SHADOW_RANGE_LOCAL) {
			/*
			 * Remove a record from the AVL tree.
			 */
			if (avl_first(&vsdp->vns_space_map) == NULL) {
				vfs_shadow_close_space_map(map);
				vnode_shadow_free(vsdp);
				return (EINVAL);
			}

			if (!vfs_shadow_spacemap_disable)
				rewrite = B_TRUE;
			(void) vfs_shadow_remove_range(vsdp, rec.vsrr_start,
			    rec.vsrr_end, B_FALSE);
		} else {
			vfs_shadow_close_space_map(map);
			vnode_shadow_free(vsdp);
			return (EINVAL);
		}
	}

	vfs_shadow_close_space_map(map);

	if (avl_first(&vsdp->vns_space_map) == NULL) {
		vnode_shadow_free(vsdp);
		return (EINVAL);
	}

	/*
	 * If there were local records, we should take this opportunity to
	 * write the space map in a more compressed form (only remote records).
	 * If this fails for some reason, we just leave the file as-is and
	 * drive on.  If the space map is now empty, we leave it be and let it
	 * be removed as part of the normal process.
	 */
	if (rewrite && !vfs_shadow_range_empty(vsdp)) {
		if ((error = vfs_shadow_open_space_map(vp, &map, &len,
		    NULL, B_TRUE)) != 0)
			return (0);

		for (vrp = avl_first(&vsdp->vns_space_map);
		    vrp != NULL;
		    vrp = avl_walk(&vsdp->vns_space_map, vrp, AVL_AFTER)) {
			rec.vsrr_type = VFS_SHADOW_RANGE_REMOTE;
			rec.vsrr_start = vrp->vsr_start;
			rec.vsrr_end = vrp->vsr_end;
			ASSERT3U(vrp->vsr_start, !=, vrp->vsr_end);

			if (vfs_shadow_append_async(map, &rec,
			    sizeof (rec)) != 0) {
				vfs_shadow_close_space_map(map);
				return (0);
			}
		}

		(void) VOP_FSYNC(map, 0, kcred, NULL);

		vfs_shadow_close_space_map(map);

		(void) vfs_shadow_rename_space_map(vp);
	}

	return (0);
}

/*
 * Given a vnode, return the extended attribute containing the path of the file
 * or directory relative to the shadow root.  The empty string indicates that
 * it is the root.
 */
static char *
vfs_shadow_attr_read(vnode_t *vp, int *errp)
{
	vnode_t *xdir;
	char *path;

	*errp = 0;

	/*
	 * Nothing to do for xattr directories.
	 */
	if (IS_XATTRDIR(vp))
		return (NULL);

	/*
	 * Lookup the entry in the extended attribute directory.
	 */
	if (VOP_LOOKUP(vp, "", &xdir, NULL, LOOKUP_XATTR, NULL, kcred,
	    NULL, NULL, NULL) != 0)
		return (NULL);

	path = vfs_shadow_open_and_read(xdir, VFS_SHADOW_ATTR, errp, B_FALSE);
	VN_RELE(xdir);

	if (*errp == ENOENT)
		*errp = 0;

	return (path);
}

/*
 * Given a vnode, remove the shadow extended attribute.  This function cannot
 * fail; if the extended attribute is not there or cannot be accessed, then we
 * know we cannot read it when we come back up.
 */
static void
vfs_shadow_attr_remove(vfs_shadow_t *vsp, vnode_t *vp, boolean_t skippending)
{
	vnode_t *xdir;

	if (VOP_LOOKUP(vp, "", &xdir, NULL, LOOKUP_XATTR, NULL, kcred,
	    NULL, NULL, NULL) != 0)
		return;

	(void) VOP_REMOVE(xdir, VFS_SHADOW_ATTR, kcred, NULL, 0);
	(void) VOP_REMOVE(xdir, VFS_SHADOW_MAP, kcred, NULL, 0);
	(void) VOP_REMOVE(xdir, VFS_SHADOW_MAP ".tmp", kcred, NULL, 0);

	VN_RELE(xdir);

	if (!skippending)
		vfs_shadow_pending_remove(vsp, vp);
}

/*
 * Given a vnode, write the given path to the shadow extended attribute.  This
 * path is expressed in two parts: the parent's shadow path, and the relative
 * path of a directory entry.
 */
static int
vfs_shadow_attr_write(vfs_shadow_t *vsp, vnode_t *vp, const char *parent,
    const char *name)
{
	vnode_t *xdir, *shadow;
	vattr_t vattr;
	int error;

	if ((error = vfs_shadow_pending_add(vsp, vp)) != 0)
		return (error);

	/*
	 * For simplicity, always remove the entry if there was an aborted
	 * previous attempt.  We could also handle this by truncating the file,
	 * but this not performance critical and this simplifies the rest of
	 * this function.
	 */
	vfs_shadow_attr_remove(vsp, vp, B_TRUE);

	if (VOP_LOOKUP(vp, "", &xdir, NULL, LOOKUP_XATTR | CREATE_XATTR_DIR,
	    NULL, kcred, NULL, NULL, NULL) != 0)
		return (-1);

	/*
	 * Ideally, we'd leverage vn_openat() to do most of the work, but that
	 * requires taking some locks that are not valid in all contexts where
	 * we can be called.
	 */
	vattr.va_type = VREG;
	vattr.va_mode = 0600;
	vattr.va_mask = AT_TYPE|AT_MODE;

	if ((error = VOP_CREATE(xdir, VFS_SHADOW_ATTR, &vattr, NONEXCL, VWRITE,
	    &shadow, kcred, 0, NULL, NULL)) != 0) {
		VN_RELE(xdir);
		return (error);
	}

	VN_RELE(xdir);

	error = 0;
	if (parent[0] != '\0') {
		if ((error = vfs_shadow_append_string(shadow, parent)) != 0)
			goto out;
	}

	if (name[0] != '\0') {
		if (parent[0] != '\0') {
			if ((error = vfs_shadow_append_string(shadow,
			    "/")) != 0)
				goto out;
		}

		if ((error = vfs_shadow_append_string(shadow, name)) != 0)
			goto out;
	}

	/*
	 * XXX reset mtime of xattr directory.
	 */

out:
	VN_RELE(shadow);
	if (error != 0)
		vfs_shadow_attr_remove(vsp, vp, B_TRUE);
	return (error);
}

/*
 * Migrate ACLs.  We either do NFSv4 or POSIX draft ACLs, but don't convert
 * between the two.
 */
static int
vfs_shadow_migrate_acl(vnode_t *destvp, vnode_t *sourcevp)
{
	vfs_shadow_t *vsp = destvp->v_vfsp->vfs_shadow;
	vsecattr_t vsec;
	ulong_t sourcetype, mask;

	bzero(&vsec, sizeof (vsec));

	if (VOP_PATHCONF(sourcevp, _PC_ACL_ENABLED, &sourcetype,
	    vfs_shadow_cred, NULL) != 0)
		return (0);

	mask = sourcetype & vsp->vs_aclflags;

	switch (mask) {
	case _ACL_ACE_ENABLED | _ACL_ACLENT_ENABLED:
	case _ACL_ACE_ENABLED:
		vsec.vsa_mask = VSA_ACE | VSA_ACECNT | VSA_ACE_ALLTYPES |
		    VSA_ACE_ACLFLAGS;
		break;

	case _ACL_ACLENT_ENABLED:
		vsec.vsa_mask = VSA_ACL | VSA_ACLCNT | VSA_DFACL |
		    VSA_DFACLCNT;
		break;

	default:
		return (0);
	}

	/*
	 * For unknown reasons, ufs_setattr() will acquire the rwlock for us,
	 * but ufs_setsecattr() requires it to be held.
	 */

	(void) VOP_RWLOCK(destvp, V_WRITELOCK_TRUE, NULL);

	if (VOP_GETSECATTR(sourcevp, &vsec, 0, vfs_shadow_cred, NULL) == 0) {
		(void) VOP_SETSECATTR(destvp, &vsec, 0, kcred, NULL);
		if (vsec.vsa_aclcnt != 0)
			kmem_free(vsec.vsa_aclentp,
			    vsec.vsa_aclcnt * sizeof (aclent_t));
		if (vsec.vsa_dfaclcnt != 0)
			kmem_free(vsec.vsa_dfaclentp,
			    vsec.vsa_dfaclcnt * sizeof (aclent_t));
	}

	VOP_RWUNLOCK(destvp, V_WRITELOCK_TRUE, NULL);

	return (0);
}

/*
 * Migrate attributes of the root directory.  This is normally done by
 * vfs_shadow_migrate_entry(), but that function is not invoked for the root
 * directory itself.
 */
static int
vfs_shadow_migrate_root_attr(vnode_t *destvp, vnode_t *sourcevp)
{
	xvattr_t xvattr;
	int error;

	ASSERT(destvp->v_type == VDIR);

	xva_init(&xvattr);
	xvattr.xva_vattr.va_mask = AT_ALL | AT_XVATTR;
	xvattr.xva_vattr.va_mask &= ~(AT_NOSET | AT_SIZE);
	XVA_SET_REQ(&xvattr, XAT_CREATETIME);
	XVA_SET_REQ(&xvattr, XAT_ARCHIVE);
	XVA_SET_REQ(&xvattr, XAT_SYSTEM);
	XVA_SET_REQ(&xvattr, XAT_HIDDEN);
	XVA_SET_REQ(&xvattr, XAT_NODUMP);
	XVA_SET_REQ(&xvattr, XAT_APPENDONLY);
	XVA_SET_REQ(&xvattr, XAT_NOUNLINK);
	XVA_SET_REQ(&xvattr, XAT_READONLY);
	XVA_SET_REQ(&xvattr, XAT_IMMUTABLE);
	XVA_SET_REQ(&xvattr, XAT_AV_MODIFIED);
	XVA_SET_REQ(&xvattr, XAT_AV_QUARANTINED);
	XVA_SET_REQ(&xvattr, XAT_AV_SCANSTAMP);

	if ((error = VOP_GETATTR(sourcevp, (vattr_t *)&xvattr, 0,
	    vfs_shadow_cred, NULL)) != 0)
		return (error);

	bcopy(xvattr.xva_rtnattrmap, xvattr.xva_reqattrmap,
	    sizeof (xvattr.xva_reqattrmap));

	xvattr.xva_vattr.va_mask &= ~(AT_NOSET | AT_SIZE);

	if ((error = VOP_SETATTR(destvp, (vattr_t *)&xvattr, 0,
	    kcred, NULL)) != 0)
		return (error);

	if ((error = vfs_shadow_migrate_acl(destvp, sourcevp)) != 0)
		return (error);

	shadow_new_state_vp(destvp, V_SHADOW_UNKNOWN);

	return (0);
}

/*
 * This function disables the attributes that can prevent us from migrating
 * data (IMMUTABLE, READONLY, and APPENDONLY).   These are then restored
 * afterwards if set.
 */
static int
vfs_shadow_attr_save(vnode_t *destvp, xvattr_t *xva)
{
	int err;
	xvattr_t setattr;

	xva_init(xva);
	xva->xva_vattr.va_mask = AT_XVATTR | AT_MTIME;
	XVA_SET_REQ(xva, XAT_IMMUTABLE);
	XVA_SET_REQ(xva, XAT_READONLY);
	XVA_SET_REQ(xva, XAT_APPENDONLY);
	XVA_SET_REQ(xva, XAT_ARCHIVE);
	XVA_SET_REQ(xva, XAT_AV_MODIFIED);

	if ((err = VOP_GETATTR(destvp, (vattr_t *)xva, 0,
	    kcred, NULL)) != 0)
		return (err);

	if ((XVA_ISSET_RTN(xva, XAT_IMMUTABLE) &&
	    xva->xva_xoptattrs.xoa_immutable) ||
	    (XVA_ISSET_RTN(xva, XAT_READONLY) &&
	    xva->xva_xoptattrs.xoa_readonly) ||
	    (XVA_ISSET_RTN(xva, XAT_APPENDONLY) &&
	    xva->xva_xoptattrs.xoa_appendonly)) {
		xva_init(&setattr);
		XVA_SET_REQ(&setattr, XAT_IMMUTABLE);
		XVA_SET_REQ(&setattr, XAT_READONLY);
		XVA_SET_REQ(&setattr, XAT_APPENDONLY);
		setattr.xva_xoptattrs.xoa_immutable = B_FALSE;
		setattr.xva_xoptattrs.xoa_readonly = B_FALSE;
		setattr.xva_xoptattrs.xoa_appendonly = B_FALSE;
		err = VOP_SETATTR(destvp, (vattr_t *)&setattr, 0,
		    kcred, NULL);
	}

	return (err);
}

/*
 * This function reverses the work done by vfs_shadow_attr_save().  If the
 * mtime of the destination was the same as the source before the migration,
 * then we reset this mtime after performing the migration.
 */
static int
vfs_shadow_attr_restore(vnode_t *destvp, vnode_t *sourcevp, xvattr_t *xva)
{
	int err;
	xvattr_t setattr;
	vattr_t getattr;

	xva_init(&setattr);

	/*
	 * Restore any necessary system attributes.
	 */
	if (XVA_ISSET_RTN(xva, XAT_IMMUTABLE) &&
	    xva->xva_xoptattrs.xoa_immutable) {
		XVA_SET_REQ(&setattr, XAT_IMMUTABLE);
		setattr.xva_xoptattrs.xoa_immutable = B_TRUE;
	}

	if (XVA_ISSET_RTN(xva, XAT_READONLY) &&
	    xva->xva_xoptattrs.xoa_readonly) {
		XVA_SET_REQ(&setattr, XAT_READONLY);
		setattr.xva_xoptattrs.xoa_readonly = B_TRUE;
	}

	if (XVA_ISSET_RTN(xva, XAT_APPENDONLY) &&
	    xva->xva_xoptattrs.xoa_appendonly) {
		XVA_SET_REQ(&setattr, XAT_APPENDONLY);
		setattr.xva_xoptattrs.xoa_appendonly = B_TRUE;
	}

	/*
	 * We also need to make sure the archive and av_modified attributes are
	 * set to the same values as they were originally, because the process
	 * of modifying the file will turn on these attributes.
	 */
	if (XVA_ISSET_RTN(xva, XAT_ARCHIVE) &&
	    !xva->xva_xoptattrs.xoa_archive) {
		XVA_SET_REQ(&setattr, XAT_ARCHIVE);
		setattr.xva_xoptattrs.xoa_archive = B_FALSE;
	}

	if (XVA_ISSET_RTN(xva, XAT_AV_MODIFIED) &&
	    !xva->xva_xoptattrs.xoa_av_modified) {
		XVA_SET_REQ(&setattr, XAT_AV_MODIFIED);
		setattr.xva_xoptattrs.xoa_av_modified = B_FALSE;
	}

	/*
	 * Check the mtime of the original file.
	 */
	getattr.va_mask = AT_MTIME;
	if ((err = VOP_GETATTR(sourcevp, &getattr, 0, vfs_shadow_cred,
	    NULL)) != 0)
		return (err);

	if (getattr.va_mtime.tv_sec == xva->xva_vattr.va_mtime.tv_sec &&
	    getattr.va_mtime.tv_nsec == xva->xva_vattr.va_mtime.tv_nsec) {
		setattr.xva_vattr.va_mtime = xva->xva_vattr.va_mtime;
		setattr.xva_vattr.va_mask |= AT_MTIME;
	}

	return (VOP_SETATTR(destvp, (vattr_t *)&setattr, 0, kcred,
	    NULL));
}

/*
 * This is a replacement lookupnnameat(), as we can't be guaranteed to take
 * curproc's lock in order to lookup up the root.  Because we are specifying
 * NO_FOLLOW, and we have a relative path, we know we won't need the zone-local
 * root, and can always specify rootvp for this.
 */
static int
vfs_shadow_lookup(vnode_t *startvp, const char *path, vnode_t **vpp)
{
	char namebuf[TYPICALMAXPATHLEN];
	struct pathname pn;
	int error;
	vnode_t *vp;

	if (path[0] == '\0')
		return (ENOENT);
	if (path[0] == '/')
		return (EINVAL);

	error = pn_get_buf((char *)path, UIO_SYSSPACE, &pn, namebuf,
	    sizeof (namebuf));
	if (error == 0) {
		VN_HOLD(startvp);
		error = lookuppnvp(&pn, NULL, NO_FOLLOW | NONSYSCALL, NULL,
		    &vp, rootdir, startvp, vfs_shadow_cred);
	}
	if (error == ENAMETOOLONG) {
		if (error = pn_get((char *)path, UIO_SYSSPACE, &pn))
			return (error);
		VN_HOLD(startvp);
		error = lookuppnvp(&pn, NULL, NO_FOLLOW | NONSYSCALL, NULL,
		    &vp, rootdir, startvp, vfs_shadow_cred);
		pn_free(&pn);
	}

	if (error == 0)
		*vpp = vp;

	return (error);
}

/*
 * Given an fsid index and a vnode, constuct the name of the file that will be
 * used to to either link to the local file or track the remote path name.
 * This file is named "<index>.<fid>.<suffix>", where 'suffix' is one of "ref"
 * or "path".
 */
static char *
vfs_shadow_link_name_common(uint32_t idx, vnode_t *vp, const char *suffix)
{
	fid_t fid;
	int i;
	size_t len;
	char *fidp, *bufp;

	bzero(&fid, sizeof (fid));
	fid.fid_len = MAXFIDSZ;

	if (VOP_FID(vp, &fid, NULL) != 0)
		return (NULL);

	len = snprintf(NULL, 0, "%u..%s", idx, suffix) + fid.fid_len * 2 + 1;
	bufp = kmem_alloc(len, KM_SLEEP);
	(void) snprintf(bufp, len, "%u.", idx);

	fidp = bufp + strlen(bufp);
	for (i = 0; i < fid.fid_len; i++, fidp += 2) {
		(void) snprintf(fidp, bufp + len - fidp, "%02x",
		    (uint8_t)fid.fid_data[i]);
	}
	(void) snprintf(fidp, len, ".%s", suffix);
	ASSERT(strlen(bufp) + 1 == len);

	return (bufp);
}

static char *
vfs_shadow_link_name_ref(uint32_t idx, vnode_t *vp)
{
	return (vfs_shadow_link_name_common(idx, vp, "ref"));
}

static char *
vfs_shadow_link_name_path(uint32_t idx, vnode_t *vp)
{
	return (vfs_shadow_link_name_common(idx, vp, "path"));
}


/*
 * Given the name of a link file, return the vnode associated with the link, if
 * present.
 */
static int
vfs_shadow_link_lookup(vfs_t *vfsp, const char *name, vnode_t **vpp)
{
	int error;
	vnode_t *dirvp;

	if ((error = vfs_shadow_private_lookup(vfsp, VFS_SHADOW_PRIVATE_LINK,
	    &dirvp)) != 0)
		return (error);

	if ((error = VOP_LOOKUP(dirvp, (char *)name, vpp, NULL, 0,
	    NULL, kcred, NULL, NULL, NULL)) != 0) {
		if (error != ENOENT) {
			VN_RELE(dirvp);
			return (error);
		}
		*vpp = NULL;
	}

	VN_RELE(dirvp);
	return (0);
}

static void
vfs_shadow_link_remove(vfs_t *vfsp, const char *name)
{
	vnode_t *dirvp;

	if (vfs_shadow_private_lookup(vfsp, VFS_SHADOW_PRIVATE_LINK,
	    &dirvp) != 0)
		return;

	(void) VOP_REMOVE(dirvp, (char *)name, kcred, NULL, 0);
	VN_RELE(dirvp);
}

/*
 * This function is called the first time we need to lookup a fsid.  We iterate
 * over all entries in the private 'fsid' directory, and read the contents to
 * get the source pathname.  From here, we open the source path and get the
 * associated fsid.
 */
static int
vfs_shadow_load_fsid(vfs_t *vfsp)
{
	vfs_shadow_t *vsp = vfsp->vfs_shadow;
	vnode_t *dirvp, *vp;
	int error;
	char name[12];
	char *path = NULL;
	size_t pathlen = 0;
	vattr_t vattr;
	vfs_shadow_fsid_entry_t *vfp;

	if ((error = vfs_shadow_private_lookup(vfsp, VFS_SHADOW_PRIVATE_FSID,
	    &dirvp)) != 0)
		return (error);

	for (; ; vsp->vs_fsid_idx++) {
		(void) snprintf(name, sizeof (name), "%u", vsp->vs_fsid_idx);

		if ((error = VOP_LOOKUP(dirvp, name, &vp, NULL, 0,
		    NULL, kcred, NULL, NULL, NULL)) != 0) {
			if (error != ENOENT) {
				VN_RELE(dirvp);
				return (error);
			}

			break;
		}

		vattr.va_mask = AT_SIZE;
		if ((error = VOP_GETATTR(vp, &vattr, 0, kcred, NULL)) != 0) {
			VN_RELE(vp);
			VN_RELE(dirvp);
			return (error);
		}

		/*
		 * We may have crashed after creating the file but before
		 * populating it.  In this case, we just continue on as if this
		 * didn't exist.
		 */
		if (vattr.va_size == 0) {
			VN_RELE(vp);
			continue;
		}

		if (vattr.va_size > pathlen) {
			if (pathlen != 0)
				kmem_free(path, pathlen + 1);
			path = kmem_alloc(vattr.va_size + 1, KM_SLEEP);
			pathlen = vattr.va_size;
		}

		if ((error = vfs_shadow_read(vp, path,
		    vattr.va_size, 0)) != 0) {
			VN_RELE(vp);
			VN_RELE(dirvp);
			return (error);
		}

		path[vattr.va_size] = '\0';
		VN_RELE(vp);

		/*
		 * If the source is changing, we simply ignore failure here
		 * rather than propagating failure upwards for all consumers.
		 */
		if ((error = vfs_shadow_lookup(vsp->vs_root, path,
		    &vp)) != 0)
			continue;

		vfp = kmem_alloc(sizeof (vfs_shadow_fsid_entry_t), KM_SLEEP);
		bcopy(&vp->v_vfsp->vfs_fsid, &vfp->vsfe_fsid,
		    sizeof (fsid_t));
		vfp->vsfe_idx = vsp->vs_fsid_idx;
		avl_add(&vsp->vs_fsid_table, vfp);

		VN_RELE(vp);
	}

	if (pathlen != 0)
		kmem_free(path, pathlen + 1);
	VN_RELE(dirvp);
	return (0);
}

/*
 * Verifies that the file linked to by 'vpp' is actually the correct one.
 * Normally this is always the case, but we want to double-check our work when
 * it comes to data integrity.  We also want to be able to support someone
 * restoring the source from backup and resuming migration, after which point
 * all the remote FIDs may have changed (but the paths and contents are the
 * same).
 *
 * To do this, we keep track of the remote path we used when creating the link
 * in the first place.  We can then go back to the source and lookup the remote
 * fid and compare it to that of 'sourcevp'.  If it matches, we're OK.
 * Otherwise, we need to remove the current bogus link and reset 'vpp' so that
 * we re-create the link.  Ideally, it would be nice to retroactively correct
 * the links, but this is lossy (we may have created the link without knowing
 * it conflicted).  The only way to do it would be to linearly scan the link
 * table when the shadow filesystem is first mounted.
 *
 * This function returns 0 on success or -1 on error.  On success, it may
 * change 'vpp' to point to NULL, to masquerade as if the link had never been
 * there in the first place.
 */
static int
vfs_shadow_check_link(vfs_t *vfsp, const char *linkname,
    uint32_t idx, vnode_t *sourcevp, vnode_t **vpp)
{
	vfs_shadow_t *vsp = vfsp->vfs_shadow;
	vnode_t *remotevp = NULL;
	vnode_t *dirvp;
	char *filename, *path = NULL;
	int error = 0;
	fid_t sourcefid, remotefid;

	if (*vpp == NULL)
		return (0);

	if ((filename = vfs_shadow_link_name_path(idx, sourcevp)) == NULL)
		goto invalid;

	if ((error = vfs_shadow_private_lookup(vfsp, VFS_SHADOW_PRIVATE_LINK,
	    &dirvp)) != 0)
		goto invalid;

	if ((path = vfs_shadow_open_and_read(dirvp, filename, &error,
	    B_TRUE)) == NULL) {
		VN_RELE(dirvp);
		goto invalid;
	}

	VN_RELE(dirvp);

	/*
	 * We now have the path, so lookup the remote vnode at that location.
	 */
	if ((error = vfs_shadow_lookup(vsp->vs_root, path, &remotevp)) != 0) {
		/*
		 * If the remote vnode no longer exists, just continue without
		 * the hard link but don't propagate the error.
		 */
		if (error == ENOENT)
			error = 0;
		goto invalid;
	}

	/*
	 * If the remote vp and source vp are not part of the same filesystem,
	 * it's invalid.
	 */
	if (remotevp->v_vfsp != sourcevp->v_vfsp)
		goto invalid;

	/*
	 * If the FIDs don't match, then it's also invalid.
	 */
	sourcefid.fid_len = remotefid.fid_len = MAXFIDSZ;
	if ((error = VOP_FID(sourcevp, &sourcefid, NULL)) != 0 ||
	    (error = VOP_FID(remotevp, &remotefid, NULL)) != 0)
		goto invalid;

	VN_RELE(remotevp);
	remotevp = NULL;

	if (vfs_shadow_fid_compare(&sourcefid, &remotefid) != 0)
		goto invalid;

	/*
	 * The FIDs match.  There's nothing else to do.
	 */
	strfree(path);
	strfree(filename);
	return (0);

invalid:
	/*
	 * Something went wrong.  If error is non-zero, it indicates a local or
	 * transient error, and we should pass it up to the caller.  If error
	 * is zero, then it indicates that we could read the link information,
	 * but it didn't match.  In this case, we blow away the info.
	 */
	if (error == 0) {
		vfs_shadow_link_remove(vfsp, linkname);
		vfs_shadow_link_remove(vfsp, filename);
	}
	VN_RELE(*vpp);
	*vpp = NULL;

	if (filename != NULL)
		strfree(filename);
	if (path != NULL)
		strfree(path);
	if (remotevp != NULL)
		VN_RELE(remotevp);

	return (error);
}

/*
 * Migrate an entry with a linkcount greater than one.  Logically, what we need
 * is a hash table mapping (fsid, fid) -> vnode.  While the fid is persistent,
 * the fsid is not.  To handle this, we have an in-core mapping of fsid to
 * index, where the latter is a monotonically increasing number.  Rather than
 * keeping all of this in core, we leverage the on-disk filesystem to do
 * the hashing for us.  We keep extended attributes of the form:
 *
 * 	Name					Contents
 *
 * 	.SUNWshadow/fsid/<idx>			Remote path to filesystem
 * 	.SUNWshadow/link/<idx>.<fid>.ref	Link to local file
 * 	.SUNWshadow/link/<idx>.<fid>.path	A valid remote path to file
 *
 * The special index '0' is reserved for the source root.
 */
static int
vfs_shadow_migrate_link(vnode_t *destdir, vnode_t *sourcevp,
    const char *name, boolean_t *needlink)
{
	vfs_t *vfsp = destdir->v_vfsp;
	vfs_shadow_t *vsp = vfsp->vfs_shadow;
	vfs_shadow_fsid_entry_t search, *vfp;
	int error;
	char *linkname;
	vnode_t *vp;

	bcopy(&sourcevp->v_vfsp->vfs_fsid, &search.vsfe_fsid,
	    sizeof (fsid_t));
	mutex_enter(&vsp->vs_fsid_lock);

	if (!vsp->vs_fsid_loaded) {
		if ((error = vfs_shadow_load_fsid(vfsp)) != 0) {
			mutex_exit(&vsp->vs_fsid_lock);
			return (error);
		}
		vsp->vs_fsid_loaded = B_TRUE;
	}

	if ((vfp = avl_find(&vsp->vs_fsid_table, &search, NULL)) == NULL) {
		*needlink = B_TRUE;
		return (0);
	}

	/*
	 * If the source filesystem doesn't support VOP_FID(), then we proceed
	 * to migrate the file and abandon any notion of preserving hard links.
	 */
	if ((linkname = vfs_shadow_link_name_ref(vfp->vsfe_idx,
	    sourcevp)) == NULL) {
		*needlink = B_TRUE;
		return (0);
	}

	if ((error = vfs_shadow_link_lookup(destdir->v_vfsp, linkname,
	    &vp)) != 0) {
		strfree(linkname);
		mutex_exit(&vsp->vs_fsid_lock);
		return (error);
	}

	if ((error = vfs_shadow_check_link(destdir->v_vfsp, linkname,
	    vfp->vsfe_idx, sourcevp, &vp)) != 0) {
		strfree(linkname);
		mutex_exit(&vsp->vs_fsid_lock);
		return (error);
	}

	strfree(linkname);

	if (vp == NULL) {
		/*
		 * Leave link table locked until we finish creating the link.
		 */
		*needlink = B_TRUE;
		return (0);
	}

	error = VOP_LINK(destdir, vp, (char *)name, kcred, NULL, 0);
	VN_RELE(vp);
	mutex_exit(&vsp->vs_fsid_lock);

	return (error);
}

/*
 * This function is called when we fail to find an entry in the in-core fsid
 * table.  It is responsible for writing out a file with a name of the given
 * index, and the contents as the given path.
 */
static int
vfs_shadow_allocate_fsid(vfs_t *vfsp, uint32_t idx, const char *path)
{
	int error;
	vnode_t *dirvp;
	char name[12];

	if ((error = vfs_shadow_private_lookup(vfsp, VFS_SHADOW_PRIVATE_FSID,
	    &dirvp)) != 0)
		return (error);

	(void) snprintf(name, sizeof (name), "%u", idx);

	error = vfs_shadow_create_and_write(dirvp, name, path);

	VN_RELE(dirvp);

	return (error);
}

/*
 * Called after creating a file that has a hard link greater than one.  This
 * creates the link in our link directory.
 */
static int
vfs_shadow_add_link(vnode_t *destvp, vnode_t *sourcevp, const char *path,
    const char *name)
{
	vfs_t *vfsp = destvp->v_vfsp;
	vfs_shadow_t *vsp = vfsp->vfs_shadow;
	int error;
	vnode_t *dirvp;
	vfs_shadow_fsid_entry_t search, *vfp;
	char *filename, *fullpath;
	avl_index_t index;
	size_t len;

	ASSERT(MUTEX_HELD(&vsp->vs_fsid_lock));

	bcopy(&sourcevp->v_vfsp->vfs_fsid, &search.vsfe_fsid,
	    sizeof (fsid_t));
	if ((vfp = avl_find(&vsp->vs_fsid_table, &search, &index)) == NULL) {

		if ((error = vfs_shadow_allocate_fsid(vfsp,
		    vsp->vs_fsid_idx, path)) != 0)
			return (error);

		vfp = kmem_alloc(sizeof (vfs_shadow_fsid_entry_t), KM_SLEEP);
		bcopy(&sourcevp->v_vfsp->vfs_fsid, &vfp->vsfe_fsid,
		    sizeof (fsid_t));
		vfp->vsfe_idx = vsp->vs_fsid_idx;
		avl_insert(&vsp->vs_fsid_table, vfp, index);

		vsp->vs_fsid_idx++;
	}

	/*
	 * First create the path information before we try creating the link
	 * itself.
	 */
	if ((filename = vfs_shadow_link_name_path(vfp->vsfe_idx,
	    sourcevp)) == NULL)
		return (0);

	if ((error = vfs_shadow_private_lookup(vfsp, VFS_SHADOW_PRIVATE_LINK,
	    &dirvp)) != 0) {
		strfree(filename);
		return (error);
	}

	(void) VOP_REMOVE(dirvp, filename, kcred, NULL, 0);

	if (path[0] == '\0') {
		fullpath = (char *)name;
	} else {
		len = strlen(path) + strlen(name) + 2;
		fullpath = kmem_alloc(len, KM_SLEEP);
		(void) snprintf(fullpath, len, "%s/%s", path, name);
		ASSERT(strlen(fullpath) == len - 1);
	}

	if ((error = vfs_shadow_create_and_write(dirvp, filename,
	    fullpath)) != 0) {
		strfree(filename);
		if (fullpath != name)
			strfree(fullpath);
		VN_RELE(dirvp);
		return (error);
	}

	strfree(filename);
	if (fullpath != name)
		strfree(fullpath);

	if ((filename = vfs_shadow_link_name_ref(vfp->vsfe_idx,
	    sourcevp)) == NULL) {
		VN_RELE(dirvp);
		return (0);
	}

	(void) VOP_REMOVE(dirvp, filename, kcred, NULL, 0);

	error = VOP_LINK(dirvp, destvp, (char *)filename, kcred, NULL, 0);
	VN_RELE(dirvp);

	strfree(filename);

	return (error);
}

/*
 * Determine if this is a .zfs directory.  We want to give a change for
 * legitimate .zfs directories on non-ZFS filesystems, but we can't check the
 * vfs type (it will appears as lofs or nfs, for example).  So we just apply a
 * simple heuristic of checking for the 'snapshot' directory.
 */
static boolean_t
vfs_shadow_is_dotzfs(vnode_t *sourcevp, const char *name)
{
	vnode_t *snapdir;

	if (strcmp(name, ".zfs") != 0)
		return (B_FALSE);

	if (VOP_LOOKUP(sourcevp, "snapshot", &snapdir, NULL, 0,
	    NULL, vfs_shadow_cred, NULL, NULL, NULL) != 0)
		return (B_FALSE);

	if (snapdir->v_type != VDIR) {
		VN_RELE(snapdir);
		return (B_FALSE);
	}

	VN_RELE(snapdir);
	return (B_TRUE);
}

static int
vfs_shadow_migrate_xattr(vnode_t *destvp, vnode_t *sourcevp, uint64_t *size)
{
	int error;
	ulong_t xattrs;
	vnode_t *sourcex, *destx;

	ASSERT(!IS_XATTRDIR(sourcevp));
	ASSERT(!IS_XATTRDIR(destvp));

	/*
	 * First determine if there are any xattrs to migrate.  We
	 * will ignore any error.
	 */
	if (VOP_PATHCONF(sourcevp, _PC_XATTR_EXISTS, &xattrs,
	    vfs_shadow_cred, NULL) != 0 || xattrs == 0)
		return (0);

	if (VOP_LOOKUP(sourcevp, "", &sourcex, NULL, LOOKUP_XATTR,
	    NULL, vfs_shadow_cred, NULL, NULL, NULL) != 0)
		return (0);

	if ((error = VOP_LOOKUP(destvp, "", &destx, NULL,
	    LOOKUP_XATTR | CREATE_XATTR_DIR, NULL, kcred, NULL, NULL,
	    NULL)) != 0) {
		VN_RELE(sourcex);
		return (error);
	}

	error = vfs_shadow_migrate_dir_vp(destx, sourcex, NULL, size);

	VN_RELE(sourcex);
	VN_RELE(destx);

	return (error);
}

/*
 * Migrate a single directory entry.
 */
static int
vfs_shadow_migrate_entry(vnode_t *destdir, vnode_t *sourcedir,
    const char *path, const char *name, uint64_t *size)
{
	vfs_shadow_t *vsp = destdir->v_vfsp->vfs_shadow;
	vnode_t *sourcevp, *destvp;
	xvattr_t xvattr;
	int error;
	uio_t uio;
	struct iovec iov;
	char *buf;
	boolean_t needlink;
	boolean_t needattr;
	xvattr_t setattr;

	if ((error = VOP_LOOKUP(sourcedir, (char *)name, &sourcevp, NULL, 0,
	    NULL, vfs_shadow_cred, NULL, NULL, NULL)) != 0)
		return (error);

	/*
	 * Ignore .zfs directories if they happen to be visible.  This will
	 * create problems if we try to migrate it to the root of our
	 * directory, and will also attempt to migrate snapshots if there is a
	 * nested ZFS filesystem.
	 */
	if (vfs_shadow_is_dotzfs(sourcevp, name)) {
		VN_RELE(sourcevp);
		return (0);
	}

	/*
	 * Get the current attributes for use when creating the new entry.
	 * This includes extended system attributes as well.
	 */
again:
	xva_init(&xvattr);
	xvattr.xva_vattr.va_mask = AT_ALL | AT_XVATTR;
	XVA_SET_REQ(&xvattr, XAT_CREATETIME);
	XVA_SET_REQ(&xvattr, XAT_ARCHIVE);
	XVA_SET_REQ(&xvattr, XAT_SYSTEM);
	XVA_SET_REQ(&xvattr, XAT_HIDDEN);
	XVA_SET_REQ(&xvattr, XAT_NODUMP);
	XVA_SET_REQ(&xvattr, XAT_APPENDONLY);
	XVA_SET_REQ(&xvattr, XAT_NOUNLINK);
	XVA_SET_REQ(&xvattr, XAT_READONLY);
	XVA_SET_REQ(&xvattr, XAT_IMMUTABLE);
	XVA_SET_REQ(&xvattr, XAT_AV_MODIFIED);
	XVA_SET_REQ(&xvattr, XAT_AV_QUARANTINED);
	XVA_SET_REQ(&xvattr, XAT_AV_SCANSTAMP);

	if ((error = VOP_GETATTR(sourcevp, (vattr_t *)&xvattr, 0,
	    vfs_shadow_cred, NULL)) != 0) {
		VN_RELE(sourcevp);
		return (error);
	}

	bcopy(xvattr.xva_rtnattrmap, xvattr.xva_reqattrmap,
	    sizeof (xvattr.xva_reqattrmap));

	/*
	 * We cannot migrate the entry while some of the attributes are set, so
	 * we exclude them from the list of attributes and set them afterwards.
	 * We also do this with the mtime of the file so that it's set to the
	 * same as the source.
	 */
	xva_init(&setattr);
	setattr.xva_vattr.va_mask |= AT_MTIME;
	setattr.xva_vattr.va_mtime = xvattr.xva_vattr.va_mtime;
	if (XVA_ISSET_RTN(&xvattr, XAT_READONLY)) {
		XVA_SET_REQ(&setattr, XAT_READONLY);
		setattr.xva_xoptattrs.xoa_readonly =
		    xvattr.xva_xoptattrs.xoa_readonly;
		XVA_CLR_REQ(&xvattr, XAT_READONLY);
	}
	if (XVA_ISSET_RTN(&xvattr, XAT_IMMUTABLE)) {
		XVA_SET_REQ(&setattr, XAT_IMMUTABLE);
		setattr.xva_xoptattrs.xoa_immutable =
		    xvattr.xva_xoptattrs.xoa_immutable;
		XVA_CLR_REQ(&xvattr, XAT_IMMUTABLE);
	}
	if (XVA_ISSET_RTN(&xvattr, XAT_APPENDONLY)) {
		XVA_SET_REQ(&setattr, XAT_APPENDONLY);
		setattr.xva_xoptattrs.xoa_appendonly =
		    xvattr.xva_xoptattrs.xoa_appendonly;
		XVA_CLR_REQ(&xvattr, XAT_APPENDONLY);
	}

	/*
	 * If either the archive bit or av_modified bit are off on the source,
	 * we need to explicitly disable them after making our modifications to
	 * the file.
	 */
	if (XVA_ISSET_RTN(&xvattr, XAT_ARCHIVE)) {
		XVA_SET_REQ(&setattr, XAT_ARCHIVE);
		setattr.xva_xoptattrs.xoa_archive =
		    xvattr.xva_xoptattrs.xoa_archive;
	}
	if (XVA_ISSET_RTN(&xvattr, XAT_AV_MODIFIED)) {
		XVA_SET_REQ(&setattr, XAT_AV_MODIFIED);
		setattr.xva_xoptattrs.xoa_av_modified =
		    xvattr.xva_xoptattrs.xoa_av_modified;
	}

	destvp = NULL;
	needlink = B_FALSE;

	switch (sourcevp->v_type) {
	case VDIR:
		if ((error = VOP_MKDIR(destdir, (char *)name,
		    (vattr_t *)&xvattr, &destvp, kcred, NULL, 0, NULL)) != 0)
			break;

		/*
		 * We need to explicitly set the owner.  The group is
		 * handled by the attributes we passed to VOP_MKDIR(), but
		 * ownership requires a separate step.
		 */
		xvattr.xva_vattr.va_mask = AT_UID;
		if ((error = VOP_SETATTR(destvp, (vattr_t *)&xvattr, 0,
		    kcred, NULL)) != 0)
			break;

		ASSERT(path != NULL);
		error = vfs_shadow_attr_write(vsp, destvp, path, name);
		break;

	case VFIFO:
	case VCHR:
	case VBLK:
	case VREG:
		if (xvattr.xva_vattr.va_nlink > 1) {
			error = vfs_shadow_migrate_link(destdir, sourcevp,
			    name, &needlink);
			if (!needlink || error != 0)
				break;
		}

		/*
		 * Create and open the file.  For non-regular files, our work
		 * is done at this point because VOP_CREATE() will set the type
		 * as appropriate and pull the device information from the
		 * attributes.
		 */
		if ((error = VOP_CREATE(destdir, (char *)name,
		    (vattr_t *)&xvattr, EXCL, 0, &destvp, kcred,
		    0, NULL, NULL)) != 0)
			break;

		/*
		 * We need to explicitly set the owner and (for regular files)
		 * size.  The group is handled by the attributes we passed to
		 * VOP_CREATE(), but ownership requires a separate step.
		 */
		xvattr.xva_vattr.va_mask = AT_UID;
		if (sourcevp->v_type == VREG)
			xvattr.xva_vattr.va_mask |= AT_SIZE;

		if ((error = VOP_SETATTR(destvp, (vattr_t *)&xvattr, 0,
		    kcred, NULL)) != 0)
			break;

		if (sourcevp->v_type == VREG) {
			if ((error = VOP_OPEN(&destvp, FWRITE, kcred,
			    NULL)) != 0)
				break;

			needattr = B_TRUE;
			if (path == NULL) {
				/*
				 * If 'path' is NULL, it is because this is
				 * actually an entry in an extended attribute
				 * directory, and we want to immediately
				 * migrate the contents at the same time.
				 */
				vnode_shadow_t *vsdp;
				v_shadowstate_t vstate;

				mutex_enter(&destvp->v_vsd_lock);
				vstate = vfs_shadow_retrieve(destvp, &vsdp);
				mutex_exit(&destvp->v_vsd_lock);
				ASSERT(vstate != V_SHADOW_UNINITIALIZED);

				mutex_enter(&vsdp->vns_shadow_content_lock);
				error = vfs_shadow_create_space_map(vsdp);
				if (error == 0)
					error = vfs_shadow_migrate_file_vp(
					    vsdp, &sourcevp, NULL, 0, -1ULL,
					    size);
				mutex_exit(&vsdp->vns_shadow_content_lock);
				needattr = B_FALSE;
			} else {
				error = vfs_shadow_migrate_xattr(destvp,
				    sourcevp, size);
			}

			if (XVA_ISSET_RTN(&xvattr,
			    XAT_AV_QUARANTINED) &&
			    xvattr.xva_xoptattrs.xoa_av_quarantined) {
				/*
				 * If this file is quarantined, return success.
				 * There is no way we can successfully migrate
				 * the data, and this may be a request (such as
				 * reading the system attributes) that would
				 * succeed if we let it pass to the underlying
				 * filesystem.
				 */
				needattr = B_FALSE;
			} else if (xvattr.xva_vattr.va_size == 0) {
				/*
				 * If this is a zero-length file, there is
				 * nothing to do.
				 */
				needattr = B_FALSE;
			}

			if (error == 0 && needattr)
				error = vfs_shadow_attr_write(vsp,
				    destvp, path, name);

			(void) VOP_CLOSE(destvp, FWRITE, 1, 0, kcred, NULL);

			if (error != 0)
				break;
		}

		if (needlink) {
			error = vfs_shadow_add_link(destvp, sourcevp,
			    path, name);
		}

		break;

	case VLNK:
		/*
		 * For symbolic links, we read the existing link and recreate
		 * it here.
		 */
		uio.uio_iov = &iov;
		uio.uio_iovcnt = 1;
		uio.uio_segflg = UIO_SYSSPACE;
		uio.uio_fmode = 0;
		uio.uio_extflg = UIO_COPY_CACHED;
		uio.uio_loffset = 0;
		uio.uio_llimit = MAXOFFSET_T;
		uio.uio_resid = xvattr.xva_vattr.va_size;

		buf = kmem_alloc(xvattr.xva_vattr.va_size + 1, KM_SLEEP);

		iov.iov_base = buf;
		iov.iov_len = xvattr.xva_vattr.va_size;

		if ((error = VOP_READLINK(sourcevp, &uio, vfs_shadow_cred,
		    NULL)) != 0) {
			kmem_free(buf, xvattr.xva_vattr.va_size + 1);
			break;
		}

		buf[xvattr.xva_vattr.va_size] = '\0';
		error = VOP_SYMLINK(destdir, (char *)name, (vattr_t *)&xvattr,
		    buf, kcred, NULL, 0);

		kmem_free(buf, xvattr.xva_vattr.va_size + 1);
		break;

	case VPROC:
	case VDOOR:
	case VSOCK:
	case VPORT:
	case VBAD:
	case VNON:
		/*
		 * These vnode types cannot be recreated in any meaningful
		 * fashion.  However, returning an error here would prevent
		 * migration of the entire directory, which is overkill.
		 * Instead, we migrate an empty file and leave it so that
		 * vfs_shadow_migrate_file_vp() will return ENOTSUP instead.
		 */
		xvattr.xva_vattr.va_type = VREG;
		error = VOP_CREATE(destdir, (char *)name,
		    (vattr_t *)&xvattr, EXCL, 0, &destvp, kcred,
		    0, NULL, NULL);
		if (error == 0)
			error = vfs_shadow_attr_write(vsp, destvp, path, name);
		if (error == 0) {
			mutex_enter(&destvp->v_vsd_lock);
			(void) vfs_shadow_instantiate(destvp, NULL);
			mutex_exit(&destvp->v_vsd_lock);
		}
		break;

	default:
		cmn_err(CE_PANIC, "unknown vnode type 0x%x", sourcevp->v_type);
	}

	if (error == 0 && destvp != NULL)
		error = vfs_shadow_migrate_acl(destvp, sourcevp);

	if (error == EEXIST) {
		/*
		 * XXX Need to handle XAT_NOUNLINK
		 */

		if (sourcevp->v_type == VDIR)
			error = VOP_RMDIR(destdir, (char *)name,
			    rootdir, kcred, NULL, 0);
		else
			error = VOP_REMOVE(destdir, (char *)name, kcred,
			    NULL, 0);

		if (error == 0) {
			if (destvp != NULL)
				VN_RELE(destvp);
			if (needlink)
				mutex_exit(&vsp->vs_fsid_lock);
			goto again;
		}
	}

	VN_RELE(sourcevp);

	/*
	 * Always attempt to reset the mtime of the new entry to that of the
	 * source, as well as any migration-inhibiting attributes.
	 */
	if (destvp != NULL) {
		vnode_shadow_t *vsdp;
		v_shadowstate_t vstate;

		mutex_enter(&destvp->v_vsd_lock);
		vstate = vfs_shadow_retrieve(destvp, &vsdp);
		mutex_exit(&destvp->v_vsd_lock);
		ASSERT(vstate != V_SHADOW_UNINITIALIZED);

		mutex_enter(&vsdp->vns_shadow_content_lock);
		(void) VOP_SETATTR(destvp, (vattr_t *)&setattr, 0, kcred, NULL);
		mutex_exit(&vsdp->vns_shadow_content_lock);

		if (error == 0 && destvp->v_type == VREG)
			error = VOP_FSYNC(destvp, 0, kcred, NULL);

		if (error == 0) {
			if (destvp->v_type == VDIR)
				shadow_new_state(vsdp, V_SHADOW_UNKNOWN);
			else
				shadow_new_state(vsdp, V_SHADOW_MIGRATING_DATA);
		}

		VN_RELE(destvp);
	}

	if (needlink)
		mutex_exit(&vsp->vs_fsid_lock);

	return (error);
}

static int
vfs_shadow_migrate_dir_vp(vnode_t *destvp, vnode_t *sourcevp,
    const char *path, uint64_t *size)
{
	vfs_shadow_t *vsp = destvp->v_vfsp->vfs_shadow;
	int eof;
	struct uio uio;
	struct dirent64 *dp;
	struct iovec iov;
	size_t dlen = Shadow_max_dirreclen;
	size_t dbuflen;
	void *buf;
	int error, rerror;
	xvattr_t saveattr;

	ASSERT(destvp->v_type == VDIR);

	if ((destvp->v_flag & VROOT) &&
	    (error = vfs_shadow_migrate_root_attr(destvp, sourcevp)) != 0)
		return (error);

	if ((error = vfs_shadow_attr_save(destvp, &saveattr)) != 0)
		return (error);

	eof = 0;

	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_fmode = 0;
	uio.uio_extflg = UIO_COPY_CACHED;
	uio.uio_loffset = 0;
	uio.uio_llimit = MAXOFFSET_T;

	buf = kmem_alloc(dlen, KM_SLEEP);

	/*
	 * See if there are any entries in the directory.  If we see more than
	 * one entry, bail out.
	 */
	while (!eof) {
		uio.uio_resid = dlen;
		iov.iov_base = buf;
		iov.iov_len = dlen;

		DTRACE_SHADOWFS3(transfer__start, vnode_t *, destvp,
		    char *, path, ulong_t, 0);
		(void) VOP_RWLOCK(sourcevp, V_WRITELOCK_FALSE, NULL);
		error = VOP_READDIR(sourcevp, &uio, vfs_shadow_cred,
		    &eof, NULL, 0);
		VOP_RWUNLOCK(sourcevp, V_WRITELOCK_FALSE, NULL);

		dbuflen = dlen - uio.uio_resid;

		if (error || dbuflen == 0) {
			if (error)
				DTRACE_SHADOWFS3(transfer__error, vnode_t *,
				    destvp, char *, path, int, error);
			break;
		}

		DTRACE_SHADOWFS4(transfer__done, vnode_t *, destvp, char *,
		    path, ulong_t, 0, ulong_t, dbuflen);

		for (dp = (dirent64_t *)buf;
		    (intptr_t)dp < (intptr_t)buf + dbuflen;
		    dp = (dirent64_t *)((intptr_t)dp + dp->d_reclen)) {
			if (strcmp(dp->d_name, ".") == 0 ||
			    strcmp(dp->d_name, "..") == 0)
				continue;

			/*
			 * Skip special extended attributes.
			 */
			if (path == NULL &&
			    (strcmp(dp->d_name, VIEW_READONLY) == 0 ||
			    strcmp(dp->d_name, VIEW_READWRITE) == 0 ||
			    strncmp(dp->d_name, "SUNWshadow", 10) == 0))
				continue;

again:
			if (ttolwp(curthread) != NULL &&
			    issig(JUSTLOOKING) && issig(FORREAL)) {
				error = EINTR;
				break;
			}

			if (vsp->vs_dbg_flags & VFS_SHADOW_DBG_SPIN) {
				delay(10);
				goto again;
			}

			DTRACE_SHADOWFS3(discover__start, vnode_t *, destvp,
			    char *, path, char *, dp->d_name);

			if ((error = vfs_shadow_migrate_entry(destvp,
			    sourcevp, path, dp->d_name, size)) != 0) {
				DTRACE_SHADOWFS4(discover__error, vnode_t *,
				    destvp, char *, path, char *, dp->d_name,
				    int, error);
				eof = B_TRUE;
				break;
			}

			DTRACE_SHADOWFS3(discover__done, vnode_t *, destvp,
			    char *, path, char *, dp->d_name);
		}
	}

	kmem_free(buf, dlen);

	rerror = vfs_shadow_attr_restore(destvp, sourcevp, &saveattr);
	if (error == 0)
		error = rerror;

	return (error);

}

static int
vfs_shadow_migrate_dir(vnode_shadow_t *vsdp, const char *path, uint64_t *size)
{
	vfs_shadow_t *vsp;
	vnode_t *destvp = vsdp->vns_vnode;
	vnode_t *sourcevp;
	int error;

	vsp = destvp->v_vfsp->vfs_shadow;

	/*
	 * Get the vnode of the source directory of the target.
	 */
	if (path[0] != '\0') {
		if ((error = vfs_shadow_lookup(vsp->vs_root, path,
		    &sourcevp)) != 0)
			return (error);

		if (sourcevp->v_type != VDIR) {
			VN_RELE(sourcevp);
			return (ENOTDIR);
		}
	} else {
		sourcevp = vsp->vs_root;
		VN_HOLD(sourcevp);
	}

	error = VOP_ACCESS(sourcevp, VREAD, 0, vfs_shadow_cred, NULL);
	if (error == 0)
		error = vfs_shadow_migrate_xattr(destvp, sourcevp, size);
	if (error == 0)
		error = vfs_shadow_migrate_dir_vp(destvp, sourcevp, path,
		    size);
	VN_RELE(sourcevp);

	return (error);
}

/*
 * Core function for migrating a file.
 */
static int
vfs_shadow_migrate_file_vp(vnode_shadow_t *vsdp, vnode_t **sourcevp,
    const char *path, uint64_t start_offset, uint64_t end_offset,
    uint64_t *size)
{
	vfs_shadow_t *vsp;
	xvattr_t xvattr;
	vattr_t *vattr = &xvattr.xva_vattr;
	int error;
	char *buf;
	struct uio uio;
	struct iovec iov;
	ssize_t len, offset;
	uint64_t start, end, tmp;
	vnode_shadow_range_t *vrp;
	vnode_t *destvp;
	offset_t chunk_end;
	int blocksize;
	xvattr_t saveattr;
	boolean_t dospacemap;

	destvp = vsdp->vns_vnode;
	vsp = destvp->v_vfsp->vfs_shadow;

	ASSERT(destvp->v_type == VREG);
	ASSERT(MUTEX_HELD(&vsdp->vns_shadow_content_lock));

	if ((*sourcevp)->v_type != VREG)
		return (ENOTSUP);

	/*
	 * If 'path' is NULL, this is an extended attribute and we can't update
	 * any on-disk state.
	 */
	dospacemap = (path != NULL);

	if ((error = vfs_shadow_attr_save(destvp, &saveattr)) != 0)
		return (error);

	if ((error = vfs_shadow_read_space_map(vsdp)) != 0) {
		(void) vfs_shadow_attr_restore(destvp, *sourcevp, &saveattr);
		return (error);
	}

	if ((error = vfs_shadow_attr_restore(destvp, *sourcevp,
	    &saveattr)) != 0)
		return (error);

	if ((vrp = vfs_shadow_lookup_range(vsdp, start_offset,
	    end_offset)) == NULL)
		return (0);

	if ((error = VOP_ACCESS(*sourcevp, VREAD, 0, vfs_shadow_cred,
	    NULL)) != 0)
		return (error);

	xva_init(&xvattr);
	vattr->va_mask = AT_SIZE | AT_BLKSIZE;

	if ((error = VOP_GETATTR(destvp, (vattr_t *)&xvattr, 0,
	    kcred, NULL)) != 0)
		return (error);

	if ((error = VOP_OPEN(sourcevp, FREAD, vfs_shadow_cred, NULL)) != 0)
		return (error);

	blocksize = MAX(vattr->va_blksize, vfs_shadow_min_xfer_size);

	start = P2ROUNDUP(start_offset, blocksize);
	if (start != start_offset)
		start -= blocksize;
	end = MIN(P2ROUNDUP(MIN(end_offset, vattr->va_size), blocksize),
	    vattr->va_size);

	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_fmode = 0;
	uio.uio_extflg = UIO_COPY_CACHED;
	uio.uio_loffset = 0;
	uio.uio_llimit = MAXOFFSET_T;

	buf = kmem_alloc(blocksize, KM_SLEEP);

	chunk_end = 0;
	while (start < end) {
		/*
		 * Start by looking up the next empty range that is contained
		 * within our remaining range.
		 */
		if ((vrp = vfs_shadow_lookup_range(vsdp, start, end)) == NULL)
			break;

		/*
		 * Check to see if there is a hole on the source.  If so, skip
		 * past it, recording the removed range, and then try again.
		 */
		start = MAX(start, vrp->vsr_start);
		if (start >= chunk_end) {
			chunk_end = start;
			if (VOP_IOCTL(*sourcevp, _FIO_SEEK_DATA,
			    (intptr_t)&chunk_end, FKIOCTL, vfs_shadow_cred,
			    NULL, NULL) != 0) {
				chunk_end = vattr->va_size;
			} else {
				if ((error = vfs_shadow_remove_range(vsdp,
				    start, chunk_end, dospacemap)) != 0)
					break;
				start = chunk_end;
				if (VOP_IOCTL(*sourcevp, _FIO_SEEK_HOLE,
				    (intptr_t)&chunk_end, FKIOCTL,
				    vfs_shadow_cred, NULL, NULL) != 0)
					chunk_end = vattr->va_size;
				continue;
			}
		}

		/*
		 * We cap our search at the end of the remote range, the end of
		 * this data chunk in the file, or the end of our requested
		 * range.
		 */
		tmp = end;
		if (vrp->vsr_end < tmp)
			tmp = vrp->vsr_end;
		if (chunk_end < tmp)
			tmp = chunk_end;
		len = tmp - start;
		ASSERT(len != 0);

		/*
		 * We can only transfer up to 'blocksize' chunks.
		 */
		if (len > blocksize)
			len = blocksize;

		ASSERT(start >= vrp->vsr_start && start < vrp->vsr_end &&
		    start + len > vrp->vsr_start &&
		    start + len <= vrp->vsr_end);

		/*
		 * Prepare to do the read.  At this point, we can drop the
		 * shadow lock to allow other threads to come in and read
		 * non-overlapping portions of the file.  If we have two
		 * threads come in and attempt to migrate the same region,
		 * we'll re-check after doing the read (but before the local
		 * write) and discard it if there is nothing to do.
		 */
		mutex_exit(&vsdp->vns_shadow_content_lock);

		uio.uio_loffset = start;
		iov.iov_base = buf;
		iov.iov_len = uio.uio_resid = len;

		/*
		 * Give the user a chance to interrupt the process, in the case
		 * that we are doing a large operation.
		 */
again:
		if (ttolwp(curthread) != NULL &&
		    issig(JUSTLOOKING) && issig(FORREAL)) {
			error = EINTR;
			mutex_enter(&vsdp->vns_shadow_content_lock);
			break;
		}

		/*
		 * This is a debugging tool used by the test harness to test
		 * interrupting an in-progress operation.
		 */
		if (vsp->vs_dbg_flags & VFS_SHADOW_DBG_SPIN) {
			delay(10);
			goto again;
		}

		/*
		 * Do the actual read from the source.
		 */
		DTRACE_SHADOWFS3(transfer__start, vnode_t *, destvp, char *,
		    path, ulong_t, uio.uio_loffset);

		(void) VOP_RWLOCK(*sourcevp, V_WRITELOCK_FALSE, NULL);
		if ((error = VOP_READ(*sourcevp, &uio, FREAD, vfs_shadow_cred,
		    NULL)) != 0) {
			DTRACE_SHADOWFS3(transfer__error, vnode_t *,
			    destvp, char *, path, int, error);
			VOP_RWUNLOCK(*sourcevp, V_WRITELOCK_FALSE, NULL);
			mutex_enter(&vsdp->vns_shadow_content_lock);
			break;
		}
		VOP_RWUNLOCK(*sourcevp, V_WRITELOCK_FALSE, NULL);

		len -= uio.uio_resid;

		DTRACE_SHADOWFS4(transfer__done, vnode_t *, destvp, char *,
		    path, ulong_t, uio.uio_loffset - len, ulong_t, len);

		mutex_enter(&vsdp->vns_shadow_content_lock);
		if (len == 0) {
			error = EINVAL;
			break;
		}

		/*
		 * Now that we have completed the read, grab the shadow lock
		 * and check the space map.  If another thread beat us to the
		 * punch, then continue with the next chunk.
		 */
		if ((vrp = vfs_shadow_lookup_range(vsdp, start,
		    start + len)) == NULL)
			continue;

		/*
		 * We also have to check to see if our current block is
		 * entirely contained by the resulting range.  If not, then
		 * another consumer has read part of the range and we should go
		 * back and try again.
		 */
		if (!(start >= vrp->vsr_start && start < vrp->vsr_end &&
		    start + len > vrp->vsr_start &&
		    start + len <= vrp->vsr_end))
			continue;

		/*
		 * Unfortunately, the sparse file interface only works for
		 * local Solaris filesystems.  Since the primary purpose of
		 * data migration is to migrate remote data, it would be nice
		 * if we could preserve holes.  To this end, we check to see if
		 * the buffer is entirely zeros and skip the write.  Most of
		 * the time we will immediately bail out of this check, and
		 * even if we have a pathological case of blocks that start
		 * with mostly zeros, the incremental cost of scanning the data
		 * is small.
		 *
		 * Note that this only works with holes of at least
		 * vfs_shadow_min_xfer_size block-aligned holes.  It would be
		 * possible to work through the data in smaller chunks and
		 * detect small holes, but the tradeoff is neglible.
		 */
		for (offset = 0; offset < len; offset++) {
			if (buf[offset] != '\0')
				break;
		}

		if (offset == len) {
			if ((error = vfs_shadow_remove_range(vsdp, start,
			    start + len, dospacemap)) != 0)
				break;
			start += len;
			continue;
		}

		/*
		 * Now go through and write the data to the local filesystem.
		 * This is currently done with the file-wide lock held under
		 * the assumption that it will be relatively quick, but we
		 * could also invent a mechanism to remove a range in-core
		 * without writing it out to disk (with the ability to undo it
		 * if it fails) if this proves to be a bottleneck.  This would
		 * also require synchronization of attribute state.
		 */
		iov.iov_len = uio.uio_resid = len;
		iov.iov_base = buf;
		uio.uio_loffset -= len;

		/*
		 * Turn off any attributes that may interfere with the write.
		 */
		if ((error = vfs_shadow_attr_save(destvp,
		    &saveattr)) != 0)
			break;

		(void) VOP_RWLOCK(destvp, V_WRITELOCK_TRUE, NULL);
		if ((error = VOP_WRITE(destvp, &uio, FWRITE, kcred,
		    NULL)) != 0) {
			VOP_RWUNLOCK(destvp, V_WRITELOCK_TRUE, NULL);
			(void) vfs_shadow_attr_restore(destvp, *sourcevp,
			    &saveattr);
			break;
		}
		VOP_RWUNLOCK(destvp, V_WRITELOCK_TRUE, NULL);

		/*
		 * We need to make sure the file is on-disk before removing the
		 * range indicating that it's now on-disk.
		 */
		if ((error = VOP_FSYNC(destvp, 0, kcred, NULL)) != 0) {
			(void) vfs_shadow_attr_restore(destvp, *sourcevp,
			    &saveattr);
			break;
		}

		/*
		 * If 'path' is NULL, this is an extended attribute, and we
		 * know we won't be able to update the on-disk state.
		 */
		if ((error = vfs_shadow_remove_range(vsdp, start,
		    start + len, dospacemap)) != 0) {
			(void) vfs_shadow_attr_restore(destvp, *sourcevp,
			    &saveattr);
			break;
		}

		/*
		 * Now that we're done updating extended attributes, we can
		 * restore any attributes.
		 */
		if ((error = vfs_shadow_attr_restore(destvp, *sourcevp,
		    &saveattr)) != 0)
			break;

		start += len;
		if (size != NULL)
			*size += len;
	}

	if (error == 0) {
		/*
		 * If we're done, then check to see if there is any empty space
		 * left in the AVL tree.  If not, then we are done migrating
		 * the file and can remove any attributes.
		 */
		if (vfs_shadow_range_empty(vsdp)) {
			if (error == 0) {
				vfs_shadow_attr_remove(
				    destvp->v_vfsp->vfs_shadow, destvp,
				    B_FALSE);
				shadow_new_state(vsdp, V_SHADOW_MIGRATED);
				DTRACE_SHADOWFS2(complete, vnode_t *,
				    destvp, char *, path);
			}
		}
	}

	(void) VOP_CLOSE(*sourcevp, FREAD, 1, 0, vfs_shadow_cred, NULL);
	kmem_free(buf, blocksize);
	return (error);
}

static int
vfs_shadow_migrate_file(vnode_shadow_t *vsdp, const char *path,
    uint64_t start, uint64_t end, uint64_t *size)
{
	vfs_shadow_t *vsp = vsdp->vns_vnode->v_vfsp->vfs_shadow;
	vnode_t *sourcevp;
	int error;

	if ((error = vfs_shadow_lookup(vsp->vs_root, path, &sourcevp)) != 0)
		return (error);

	error = vfs_shadow_migrate_file_vp(vsdp, &sourcevp, path,
	    start, end, size);

	VN_RELE(sourcevp);

	return (error);
}

/*
 * Called when the 'shadow' mount option is first enabled on a filesystem.
 * This function checks the root of the filesystem to see if it's an empty
 * directory.  If so, then we add our shadow attribute to the directory to kick
 * off the process, as well as propagating attributes of the root directory
 * from the shadow filesystem.  If the directory is not empty, then we assume
 * that this is resuming an in-progress shadow copy, and that any necessary
 * shadow attributes have already been set.
 */
int
vfs_shadow_check_root(vfs_t *vfsp)
{
	vfs_shadow_t *vsp = vfsp->vfs_shadow;
	vnode_shadow_t *vsdp;
	char *fsname = vfssw[vfsp->vfs_fstype].vsw_name;
	vnode_t *rootdir;
	int eof;
	struct uio uio;
	struct dirent64 *dp;
	struct iovec iov;
	size_t dlen = Shadow_max_dirreclen;
	size_t dbuflen;
	void *buf;
	int error;
	boolean_t empty;
	boolean_t standby;

	if ((error = VFS_ROOT(vfsp, &rootdir)) != 0)
		return (error);

	/*
	 * Setting 'shadow' on something like a file lofs mount is not
	 * supported.
	 */
	if (rootdir->v_type != VDIR) {
		VN_RELE(rootdir);
		return (ENOTSUP);
	}

	mutex_enter(&rootdir->v_vsd_lock);
	(void) vfs_shadow_instantiate(rootdir, &vsdp);
	mutex_exit(&rootdir->v_vsd_lock);

	/*
	 * When checking the root directory to see if we need to start shadow
	 * migration, we want to bypass any shadow checks.
	 */
	mutex_enter(&vsdp->vns_shadow_content_lock);

	/*
	 * As the support ACL types cannot change for this particular
	 * filesystem, we fetch it once and use it in any future comparisons.
	 */
	if (VOP_PATHCONF(rootdir, _PC_ACL_ENABLED, &vsp->vs_aclflags,
	    kcred, NULL) != 0)
		vsp->vs_aclflags = 0;
	else
		vsp->vs_aclflags &= (_ACL_ACE_ENABLED | _ACL_ACLENT_ENABLED);

	eof = 0;
	empty = B_TRUE;

	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_fmode = 0;
	uio.uio_extflg = UIO_COPY_CACHED;
	uio.uio_loffset = 0;
	uio.uio_llimit = MAXOFFSET_T;

	buf = kmem_alloc(dlen, KM_SLEEP);

	/*
	 * See if there are any entries in the directory.  If we see more than
	 * one entry, bail out.
	 */
	while (!eof) {
		uio.uio_resid = dlen;
		iov.iov_base = buf;
		iov.iov_len = dlen;

		(void) VOP_RWLOCK(rootdir, V_WRITELOCK_FALSE, NULL);
		error = VOP_READDIR(rootdir, &uio, kcred, &eof, NULL, 0);
		VOP_RWUNLOCK(rootdir, V_WRITELOCK_FALSE, NULL);

		dbuflen = dlen - uio.uio_resid;

		if (error || dbuflen == 0)
			break;

		for (dp = (dirent64_t *)buf;
		    (intptr_t)dp < (intptr_t)buf + dbuflen;
		    dp = (dirent64_t *)((intptr_t)dp + dp->d_reclen)) {
			if (strcmp(dp->d_name, ".") == 0 ||
			    strcmp(dp->d_name, "..") == 0)
				continue;

			if (strcmp(dp->d_name, "lost+found") == 0 &&
			    strcmp(fsname, "ufs") == 0)
				continue;

			if (strcmp(dp->d_name, ".zfs") == 0 &&
			    strcmp(fsname, "zfs") == 0)
				continue;

			empty = B_FALSE;
			break;
		}
	}

	kmem_free(buf, dlen);

	if (empty) {
		/*
		 * We need to temporarily turn off standby if it's set.
		 * Otherwise, our attempts to create the pending list will
		 * hang.
		 */
		standby = vsp->vs_standby;
		vsp->vs_standby = B_FALSE;
		(void) vfs_shadow_attr_write(vsp, rootdir, "", "");
		vsp->vs_standby = standby;
	}

	mutex_exit(&vsdp->vns_shadow_content_lock);
	VN_RELE(rootdir);

	/*
	 * At this point, we could process the pending lists and check for any
	 * removed entries.  This would purely to be keep these lists short,
	 * which is not required for correctness.  When we come back up, we'll
	 * just start appending new entries to pending list 0, eventually
	 * collapse it, and then start appending entries to pending list 1.
	 */

	if (!vsp->vs_standby)
		vfs_shadow_resume(vfsp);

	return (0);
}

static v_shadowstate_t
vfs_shadow_retrieve(vnode_t *vp, vnode_shadow_t **vsdpp)
{
	v_shadowstate_t cur_state;
	vnode_shadow_t *cur_vsdp;

	ASSERT(MUTEX_HELD(&vp->v_vsd_lock));
	cur_state = (v_shadowstate_t)vsd_get(vp, vfs_shadow_key_state);
	cur_vsdp = (vnode_shadow_t *)vsd_get(vp, vfs_shadow_key_data);

	ASSERT(cur_state == V_SHADOW_UNINITIALIZED || cur_vsdp != NULL);

	if (vsdpp)
		*vsdpp = cur_vsdp;
	return (cur_state);
}

/*ARGSUSED*/
static int
vfs_shadow_cache_constructor(void *buf, void *arg, int flags)
{
	vnode_shadow_t *vsd = buf;

	mutex_init(&vsd->vns_shadow_content_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&vsd->vns_shadow_state_cv, NULL, CV_DEFAULT, NULL);
	return (0);
}

/*ARGSUSED*/
static void
vfs_shadow_cache_destructor(void *buf, void *arg)
{
	vnode_shadow_t *vsd = buf;

	cv_destroy(&vsd->vns_shadow_state_cv);
	mutex_destroy(&vsd->vns_shadow_content_lock);
}

void
vfs_shadow_create_cache(void)
{
	vfs_shadow_cache = kmem_cache_create("shadow_cache",
	    sizeof (struct vnode_shadow), 0,
	    vfs_shadow_cache_constructor, vfs_shadow_cache_destructor,
	    NULL, NULL, NULL, 0);
}

static 	v_shadowstate_t
vfs_shadow_instantiate(vnode_t *vp, vnode_shadow_t **vsdpp)
{
	v_shadowstate_t vstate;
	vnode_shadow_t *vsd;

	ASSERT(MUTEX_HELD(&vp->v_vsd_lock));

	vstate = vfs_shadow_retrieve(vp, vsdpp);
	if (vstate != V_SHADOW_UNINITIALIZED)
		return (vstate);

	vstate = V_SHADOW_UNKNOWN;

	vsd = kmem_cache_alloc(vfs_shadow_cache, KM_SLEEP);
	vsd->vns_have_map = B_FALSE;
	vsd->vns_vnode = vp;

	VERIFY(vsd_set(vp, vfs_shadow_key_state, (void *)vstate) == 0);
	VERIFY(vsd_set(vp, vfs_shadow_key_data, (void *)vsd) == 0);

	if (vsdpp)
		*vsdpp = vsd;

	return (vstate);
}

void
vfs_shadow_vsd_destructor(void *arg)
{
	vnode_shadow_t *vsp = (vnode_shadow_t *)arg;

	vnode_shadow_free(vsp);
	kmem_cache_free(vfs_shadow_cache, vsp);
}

static int
vfs_shadow_check_impl(vnode_t *vp, int *realerr, uint64_t start, uint64_t end,
    uint64_t *size, shadow_check_flags_t flags)
{
	vfs_t *vfsp = vp->v_vfsp;
	v_shadowstate_t vstate;
	vnode_shadow_t *vsdp;
	vfs_shadow_t *vsp;
	char *path;
	int ret;
	boolean_t rw_held, dest_held;
	vnode_t *parent;

again:
	path = NULL;
	ret = 0;
	rw_held = dest_held = B_FALSE;

	/*
	 * There is never anything migration required for extended attribute
	 * directories.
	 */
	if (IS_XATTRDIR(vp))
		return (0);

	/*
	 * Establish or pull state data out of vnode-specific info.
	 */
	mutex_enter(&vp->v_vsd_lock);
	vstate = vfs_shadow_instantiate(vp, &vsdp);
	ASSERT(vstate != V_SHADOW_UNINITIALIZED);

	/*
	 * If we have already checked this vnode and verified that it is
	 * migrated, then there is nothing to do.
	 */
	if (vstate == V_SHADOW_MIGRATED || vfs_shadow_disable) {
		mutex_exit(&vp->v_vsd_lock);
		return (0);
	}

	/*
	 * If the per-vnode shadow content mutex is already held by our
	 * current thread, we are manipulating the extended attributes
	 * of the underlying filesystem, and we should let the request
	 * go through.  We need to do this before grabbing the
	 * vfs-wide rwlock or checking migration state, because if we are
	 * migrating self we'll just hang, or else we have a recursive
	 * read lock that could deadlock if an intervening write request
	 * were seen.
	 */
	if (MUTEX_HELD(&vsdp->vns_shadow_content_lock)) {
		mutex_exit(&vp->v_vsd_lock);
		return (0);
	}

	/*
	 * Determine where we are in migrating this vnode.  One lucky
	 * winner gets to do the initial phase of migration.  Everyone
	 * else has to wait until that is finished, but once it is done
	 * those just getting attributes don't have to wait any
	 * longer, and others coordinate nicely using the content
	 * lock.
	 */
	if (vstate == V_SHADOW_MIGRATING_SELF) {
		if (flags & SHADOW_CHECK_DONTBLOCK) {
			mutex_exit(&vp->v_vsd_lock);
			return (EWOULDBLOCK);
		} else if (flags & SHADOW_CHECK_NONCONTENT) {
			/* getattr gets a fastpass */
			mutex_exit(&vp->v_vsd_lock);
			return (0);
		}
	}

	while (vstate == V_SHADOW_MIGRATING_SELF) {
		if (!cv_wait_sig(&vsdp->vns_shadow_state_cv,
		    &vp->v_vsd_lock)) {
			mutex_exit(&vp->v_vsd_lock);
			return (EINTR);
		}
		vstate = (v_shadowstate_t)vsd_get(vp, vfs_shadow_key_state);
		ASSERT(vsdp ==
		    (vnode_shadow_t *)vsd_get(vp, vfs_shadow_key_data));
	}

	if (vstate == V_SHADOW_MIGRATED) {
		mutex_exit(&vp->v_vsd_lock);
		return (0);
	}

	ASSERT(vstate == V_SHADOW_UNKNOWN || vstate == V_SHADOW_MIGRATING_DATA);

	if (vstate == V_SHADOW_UNKNOWN) {
		vstate = V_SHADOW_MIGRATING_SELF;
		VERIFY(vsd_set(vp, vfs_shadow_key_state, (void *)vstate) == 0);
	}

	mutex_exit(&vp->v_vsd_lock);

	/*
	 * We need to lock all vnode content during a shadow operation, so that
	 * we only have one thread migrating data at any given time.
	 */
	mutex_enter(&vsdp->vns_shadow_content_lock);

	if (vnode_is_migrated(vp))
		goto out;

	DTRACE_SHADOWFS1(request__start, vnode_t *, vp);

	/*
	 * We check to see whether the extended attribute exists before
	 * grabbing the lock and double-checking our shadow state.  This allows
	 * us to manipulate the root directory's extended attributes with the
	 * vfs-wide lock held.
	 */
	if ((path = vfs_shadow_attr_read(vp, &ret)) == NULL) {
		shadow_new_state(vsdp, V_SHADOW_MIGRATED);
		goto out;
	}

	/*
	 * Before we can migrate the current directory, make sure that the
	 * parent is also not a shadow filesystem.  This can occur if a
	 * migration was previously interrupted, and we're processing entries
	 * from the FID list not in hierarchical order.  If we don't process
	 * the parent first, then we will populate the subdirectory, and when
	 * we try to resume the partially migrated parent, we won't be able
	 * rmdir this partial entry because it will be non-empty.
	 *
	 * We need to do this without the content lock held, or we can
	 * end up in a deadlock.  We do need to hold the shadow lock
	 * during the lookup of "..", however, or we'll go into
	 * infinite recursion.
	 */
	if (!(vp->v_flag & VROOT) && vp->v_type == VDIR) {
		if ((ret = VOP_LOOKUP(vp, "..", &parent, NULL,
		    0, NULL, kcred, NULL, NULL, NULL)) != 0)
			goto out;

		mutex_exit(&vsdp->vns_shadow_content_lock);

		if (vfs_shadow_check_impl(parent, &ret, 0, -1ULL, NULL,
		    SHADOW_CHECK_DONTBLOCK | SHADOW_CHECK_RW_HELD) != 0) {
			VN_RELE(parent);
			mutex_enter(&vsdp->vns_shadow_content_lock);
			goto out;
		}

		vnode_assert_not_migrating_self(parent);
		VN_RELE(parent);
		mutex_enter(&vsdp->vns_shadow_content_lock);

		if (vnode_is_migrated(vp))
			goto out;

		/*
		 * Technically, we shouldn't need to read the path again as
		 * anyone that migrated the file should have set the state
		 * to migrated but it's harmless to re-check the current path.
		 */
		strfree(path);

		if ((path = vfs_shadow_attr_read(vp, &ret)) == NULL) {
			shadow_new_state(vsdp, V_SHADOW_MIGRATED);
			goto out;
		}
	}

	if (!(flags & SHADOW_CHECK_RW_HELD)) {
		rw_enter(&vfsp->vfs_shadow_lock, RW_READER);
		rw_held = B_TRUE;
	}

	if ((vsp = vfsp->vfs_shadow) == NULL) {
		/*
		 * If the shadow mount is no longer active, then someone
		 * changed the mount option in between our fast-path check and
		 * this stricter check under the rwlock.
		 */
		goto out;
	} else if (vsp->vs_standby) {
		/*
		 * If 'dontblock' is set, this is a request from the background
		 * management task.  In this case, we don't want to block our
		 * worker threads and instead return EWOULDBLOCK, allowing the
		 * management process to continue with other shadow
		 * filesystems.
		 */
		if (flags & SHADOW_CHECK_DONTBLOCK) {
			ret = EWOULDBLOCK;
			goto out;
		}

		/*
		 * If the shadow property is set to 'pending', we wait for a
		 * change in shadow status before continuing.  The exception is
		 * the root directory, because we need the root directory to be
		 * accessible in order for mount (and other adminsitrative
		 * operations) to succeed.  This presents an annoying catch-22:
		 * we cannot differentiate between access that we want to block
		 * and those that are required for the mount to succeed.  We
		 * make the assumption that users generally do not create
		 * filesystems in standby mode - it is a technique to resume a
		 * previous migration.  If we do end up in this state, our only
		 * option is to have a period where the root directory appears
		 * empty before the shadow mount is activated.
		 */
		if (vp->v_flag & VROOT)
			goto out;

		if (rw_held) {
			rw_exit(&vfsp->vfs_shadow_lock);
			rw_held = B_FALSE;
		}
		mutex_exit(&vsdp->vns_shadow_content_lock);

		strfree(path);

		mutex_enter(&vfsp->vfs_shadow_cv_lock);

		if (!cv_wait_sig(&vfsp->vfs_shadow_cv,
		    &vfsp->vfs_shadow_cv_lock)) {
			mutex_exit(&vfsp->vfs_shadow_cv_lock);
			shadow_unset_migrating_self(vsdp);
			return (EINTR);
		}

		mutex_exit(&vfsp->vfs_shadow_cv_lock);
		shadow_unset_migrating_self(vsdp);
		goto again;
	}

	/*
	 * We cannot migrate files if the target filesystem is read-only.
	 */
	if (vn_is_readonly(vp)) {
		ret = EROFS;
		goto out;
	}

	/*
	 * We also need to check if the file/directory itself is writeable,
	 * because ZFS snapshots will return B_FALSE from vn_is_readonly(), but
	 * then panic if you attempt a write.  However, we may also be here
	 * because we're trying to turn off the IMMUTABLE or READONLY system
	 * attribute.  In this case, we want to proceed.  To distinguish these
	 * cases, we check for EPERM explicitly, and continue if that is the
	 * case.
	 */
	if ((ret = VOP_ACCESS(vp, VWRITE, 0, kcred, NULL)) != 0 &&
	    ret != EPERM)
		goto out;

	/*
	 * This is a little ugly.  If we're called with the read lock held, we
	 * may need to acquire it for writing in order to write the shadow data
	 * to the filesystem.  In this case, we have to drop the read lock and
	 * acquire it as a writer.
	 */
	if (flags & SHADOW_CHECK_READ_HELD) {
		VOP_RWUNLOCK(vp, V_WRITELOCK_FALSE, NULL);
		dest_held = B_TRUE;
	}

	switch (vp->v_type) {
	case VDIR:
		ASSERT(vstate == V_SHADOW_MIGRATING_SELF ||
		    (vp->v_flag & VROOT));
		if ((ret = vfs_shadow_migrate_dir(vsdp, path, size)) != 0)
			goto out;
		vfs_shadow_attr_remove(vsp, vp, B_FALSE);
		DTRACE_SHADOWFS2(complete, vnode_t *,
		    vp, char *, path);
		shadow_new_state(vsdp, V_SHADOW_MIGRATED);
		break;

	case VREG:
		if ((ret = vfs_shadow_migrate_file(vsdp, path, start, end,
		    size)) != 0)
			goto out;
		break;

	default:
		/*
		 * Technically, this should never happen.  But if the user
		 * happens to create an attribute with the same name, we don't
		 * want to panic.
		 */
		goto out;
	}

out:
	DTRACE_SHADOWFS2(request__done, vnode_t *, vp, char *, path);

	if (ret != 0 && ret != EWOULDBLOCK)
		DTRACE_SHADOWFS3(request__error, vnode_t *, vp, char *, path,
		    int, ret);
	if (path != NULL)
		strfree(path);
	mutex_exit(&vsdp->vns_shadow_content_lock);
	if (rw_held)
		rw_exit(&vfsp->vfs_shadow_lock);
	if (dest_held)
		(void) VOP_RWLOCK(vp, V_WRITELOCK_FALSE, NULL);

	/*
	 * If for whatever reason we didn't get past migrating self,
	 * we must reset the state so someone else can try, otherwise
	 * further attempts to check this vnode will hang.
	 */
	shadow_unset_migrating_self(vsdp);

	if (realerr != NULL)
		*realerr = ret;

	if (ret != 0 && ret != EINTR)
		ret = EIO;

	return (ret);
}

/*
 * Called to check whether a file or directory needs to be copied from the
 * shadow filesystem.  If the shadow attribute is not set, then there is
 * nothing to do and we immediately return.  If the shadow attribute is set,
 * then we branch to the appropriate function for populating the local
 * filesystem data before returning.
 */
int
vfs_shadow_check_vp(vnode_t *vp, boolean_t read_held, int *realerr)
{
	int rv;

	rv = vfs_shadow_check_impl(vp, realerr, 0, -1ULL, NULL,
	    read_held ? SHADOW_CHECK_READ_HELD : 0);
	if (rv == 0)
		vnode_assert_not_migrating_self(vp);
	return (rv);
}

/*
 * Called when we are trying a simple getattr, where we don't need to
 * view content.
 */
int
vfs_shadow_check_attr_vp(vnode_t *vp, int *realerr)
{
	int rv;
	rv = vfs_shadow_check_impl(vp, realerr, 0, -1ULL, NULL,
	    SHADOW_CHECK_NONCONTENT);
	return (rv);
}

/*
 * Called for read or write, where we have a uio_t structure.  In this case we
 * only need to migrate the affected region of the file.
 */
int
vfs_shadow_check_uio(vnode_t *vp, uio_t *uiop, boolean_t read_held)
{
	int rv;
	rv = vfs_shadow_check_impl(vp, NULL, uiop->uio_offset,
	    uiop->uio_offset + uiop->uio_resid, NULL,
	    read_held ? SHADOW_CHECK_READ_HELD : 0);
	if (rv == 0)
		vnode_assert_not_migrating_self(vp);
	return (rv);
}

/*
 * Like the uio version, but for an explicit range.
 */
int
vfs_shadow_check_range(vnode_t *vp, uint64_t start, uint64_t end)
{
	int rv;
	rv = vfs_shadow_check_impl(vp, NULL, start, end, NULL, 0);
	if (rv == 0)
		vnode_assert_not_migrating_self(vp);
	return (rv);
}

/*
 * Reads the first entry from the given pending list.
 */
int
vfs_shadow_read_first_entry(vfs_t *vfsp, int idx, fid_t *fidp)
{
	vattr_t attr;
	vnode_t *source;
	size_t fidlen;

	fidlen = sizeof (vfsp->vfs_shadow->vs_last_processed.un._fid);

	if (vfs_shadow_pending_open(vfsp, idx, &source) != 0)
		return (-1);

	attr.va_mask = AT_SIZE;
	if (VOP_GETATTR(source, &attr, 0, vfs_shadow_cred,
	    NULL) != 0) {
		vfs_shadow_pending_close_vp(source);
		return (-1);
	}

	if (attr.va_size <= sizeof (vfs_shadow_header_t)) {
		vfs_shadow_pending_close_vp(source);
		return (-1);
	}

	if (vfs_shadow_read(source,
	    fidp, fidlen, sizeof (vfs_shadow_header_t)) != 0) {
		vfs_shadow_pending_close_vp(source);
		return (-1);
	}

	vfs_shadow_pending_close_vp(source);

	return (0);
}

/*
 * Process one item from the pending list.  This is actually comprised of two
 * lists of FIDs, so we switch between them.  Each list may contain files with
 * errors, and we want to know when we've finished processing everything except
 * those files with errors.  To support this, we keep track of the first error
 * seen for each list (by FID).  This is cleared whenever we successfully
 * migrate an entry.  We always make sure to place the last processed entry at
 * the end of the list, so if we see the same error twice, then we know that
 * there is nothing but errors left in the list.  If both lists have this flag
 * set, then we know the filesystem contains nothing but errors, and this can
 * be communicated to the user to act appropriately.
 */
static int
vfs_shadow_ioc_process(vnode_t *vp, vfs_shadow_t *vsp, shadow_ioc_t *sip,
    int flag)
{
	vfs_t *vfsp = vp->v_vfsp;
	int err = 0;
	int idx, ret, i;
	vnode_t *target = NULL;
	char *path;
	boolean_t empty;
	uint64_t gen;
	boolean_t allerrors;
	boolean_t firstfailed;

	mutex_enter(&vsp->vs_resync_lock);
	mutex_enter(&vsp->vs_timeout_lock);
	mutex_enter(&vsp->vs_lock);

	/*
	 * Only one thread is allowed to be processing entries at any given
	 * time.  Otherwise, we'll just end up with a bunch of threads all
	 * trying to process the same entry.
	 */
	if (vsp->vs_process_inprogress) {
		mutex_exit(&vsp->vs_lock);
		mutex_exit(&vsp->vs_timeout_lock);
		mutex_exit(&vsp->vs_resync_lock);
		rw_exit(&vfsp->vfs_shadow_lock);
		return (EBUSY);
	}

	/*
	 * In addition to preventing consumers from attempting multiple
	 * processes at once, this also stops periodic rotation of the pending
	 * logs, which is necessary to ensure that the FID we're looking at
	 * ends up at the end of the current list (when tracking errors).
	 */
	vsp->vs_process_inprogress = B_TRUE;
	gen = vsp->vs_gen;
	mutex_exit(&vsp->vs_timeout_lock);

	sip->si_size = 0;

	/*
	 * Pick the starting list.  We prefer the current active list, and skip
	 * those that have only persistent errors (unless both lists only have
	 * errors).
	 */
	idx = vsp->vs_active_idx;
	if (vsp->vs_error_seen[idx])
		idx = !idx;

	firstfailed = B_FALSE;
	if ((ret = vfs_shadow_read_first_entry(vfsp, idx,
	    &vsp->vs_last_processed)) != 0) {
		idx = !idx;
		firstfailed = B_TRUE;
		ret = vfs_shadow_read_first_entry(vfsp, idx,
		    &vsp->vs_last_processed);
	}

	/*
	 * If the pending list is obviously corrupt, then don't process
	 * anything as doing so can cause the shadow migration to prematurely
	 * declare success even though there is an underlying problem that
	 * needs to be addressed.
	 */
	if (vsp->vs_last_processed.fid_len > MAXFIDSZ) {
		vsp->vs_process_inprogress = B_FALSE;
		mutex_exit(&vsp->vs_lock);
		mutex_exit(&vsp->vs_resync_lock);
		rw_exit(&vfsp->vfs_shadow_lock);
		return (EIO);
	}

	/*
	 * If the vnode no longer exists or otherwise could not be
	 * retrieved locally, then we want to remove it next time we
	 * collapse the list.
	 */
	empty = (ret != 0);
	if (ret == 0 && VFS_VGET(vfsp, &target, &vsp->vs_last_processed) != 0)
		vfs_shadow_pending_remove_fid(vsp, &vsp->vs_last_processed);

	mutex_exit(&vsp->vs_lock);
	mutex_exit(&vsp->vs_resync_lock);

	rw_exit(&vfsp->vfs_shadow_lock);

	if (target != NULL) {
		ret = vfs_shadow_check_impl(target, &err, 0, -1ULL,
		    &sip->si_size, SHADOW_CHECK_DONTBLOCK);
		if (ret == 0)
			vnode_assert_not_migrating_self(target);
	} else {
		ret = 0;
	}

	rw_enter(&vfsp->vfs_shadow_lock, RW_READER);

	if ((vsp = vfsp->vfs_shadow) == NULL) {
		rw_exit(&vfsp->vfs_shadow_lock);
		return (0);
	}

	mutex_enter(&vsp->vs_timeout_lock);
	/*
	 * If the shadow filesystem has changed underneath us, don't do
	 * anything else.
	 */
	if (gen != vsp->vs_gen) {
		mutex_exit(&vsp->vs_timeout_lock);
		rw_exit(&vfsp->vfs_shadow_lock);
		return (0);
	}
	mutex_exit(&vsp->vs_timeout_lock);

	allerrors = B_FALSE;
	if (ret != 0) {
		/*
		 * If the migration failed, then try to get the path
		 * information.  If this fails, then it is a local
		 * error.  Otherwise, record the failure mode and the
		 * affected path in the ioctl structure.
		 */
		vnode_shadow_t *vsdp;
		sip->si_error = err;
		mutex_enter(&target->v_vsd_lock);
		(void) vfs_shadow_retrieve(target, &vsdp);
		mutex_exit(&target->v_vsd_lock);
		mutex_enter(&vsdp->vns_shadow_content_lock);
		path = vfs_shadow_attr_read(target, &err);
		mutex_exit(&vsdp->vns_shadow_content_lock);

		/*
		 * If this is the first time we've seen an error, record it in
		 * the active FID.
		 */
		if (ret != EINTR) {
			mutex_enter(&vsp->vs_lock);
			if (!vsp->vs_error_valid[idx]) {
				bcopy(&vsp->vs_last_processed,
				    &vsp->vs_error_fid[idx], sizeof (fid_t));
				vsp->vs_error_valid[idx] = B_TRUE;
				vsp->vs_error_seen[idx] = B_FALSE;
			} else if (bcmp(&vsp->vs_last_processed,
			    &vsp->vs_error_fid[idx], sizeof (fid_t)) == 0) {
				vsp->vs_error_seen[idx] = B_TRUE;
			}

			allerrors = B_TRUE;
			for (i = 0; i < 2; i++) {
				if ((!vsp->vs_error_valid[i] ||
				    !vsp->vs_error_seen[i]) &&
				    (i == idx || !firstfailed))
					allerrors = B_FALSE;
			}
			mutex_exit(&vsp->vs_lock);
		}

		if (path == NULL) {
			sip->si_error = 0;
			err = 0;
		} else if (sip->si_buffer != 0) {
			err = ddi_copyout(path,
			    (void *)(uintptr_t)sip->si_buffer,
			    MIN(strlen(path) + 1, sip->si_length),
			    flag);
			strfree(path);
		}
	} else if (target != NULL) {
		/*
		 * Normally when migration suceeds we rely on the
		 * removal of the attribute to remove the entry from
		 * the pending list.  But if we end up with a file on
		 * our pending list that is present but already
		 * migrated, this will succeed without doing anything.
		 * So we always remove the FID from our pending list, which is
		 * harmless if it's already present.
		 */
		vfs_shadow_pending_remove(vsp, target);
	}

	if (target != NULL)
		VN_RELE(target);

	/*
	 * Only if went through both pending lists and didn't find anything do
	 * we want to indicate there was nothing processed.
	 */
	if (!empty) {
		sip->si_processed = B_TRUE;
		sip->si_onlyerrors = allerrors;
		vfs_shadow_pending_collapse(vfsp, B_TRUE);
	} else {
		sip->si_processed = B_FALSE;
	}

	/*
	 * Indicate that we're no longer processing an entry.  This will allow
	 * periodic rotation of the pending list to continue.
	 */
	mutex_enter(&vsp->vs_lock);
	vsp->vs_process_inprogress = B_FALSE;
	mutex_exit(&vsp->vs_lock);

	rw_exit(&vfsp->vfs_shadow_lock);

	return (err);
}

static int
vfs_shadow_ioc_getpath(vnode_t *vp, shadow_ioc_t *sip, int flag)
{
	vfs_t *vfsp = vp->v_vfsp;
	vnode_shadow_t *vsdp;
	int err;
	char *path;

	mutex_enter(&vp->v_vsd_lock);
	(void) vfs_shadow_retrieve(vp, &vsdp);
	ASSERT(vsdp != NULL);
	mutex_exit(&vp->v_vsd_lock);

	mutex_enter(&vsdp->vns_shadow_content_lock);
	path = vfs_shadow_attr_read(vp, &err);
	mutex_exit(&vsdp->vns_shadow_content_lock);

	rw_exit(&vfsp->vfs_shadow_lock);

	if (path == NULL)
		return (0);

	err = ddi_copyout(path, (void *)(uintptr_t)sip->si_buffer,
	    MIN(strlen(path) + 1, sip->si_length), flag);
	strfree(path);
	sip->si_processed = B_TRUE;

	return (err);
}

/*
 * This is used by the background daemon for error reporting.  Normal
 * operations under vfs_shadow_check_vp() quashes all errors into EIO, rather
 * than propagating that up to consumers.  For error reporting, we want to know
 * the real error, so we have a special ioctl() that does the migration and
 * returns any error in the shadow_ioc_t structure.
 */
static int
vfs_shadow_ioc_migrate(vnode_t *vp, shadow_ioc_t *sip)
{
	vfs_t *vfsp = vp->v_vfsp;
	int err = 0;
	int rv;

	rw_exit(&vfsp->vfs_shadow_lock);

	sip->si_size = 0;

	rv = vfs_shadow_check_impl(vp, &err, 0, -1ULL, &sip->si_size,
	    SHADOW_CHECK_DONTBLOCK);
	if (rv == 0)
		vnode_assert_not_migrating_self(vp);

	sip->si_error = err;

	return (0);
}

/*
 * This is used to convert the pending FID list into a set of paths to
 * initially populate the migration process.  We first try to get the path from
 * the vnode path information, which is hopefully correct.  If this fails, then
 * we try to get the remote path and hope that it is still in the same
 * location.  If we can't look up the file itself, then simply return an error.
 */
/*ARGSUSED*/
static int
vfs_shadow_ioc_fid2path(vnode_t *rootvp, shadow_ioc_t *sip, int flag,
    cred_t *cr)
{
	vfs_t *vfsp = rootvp->v_vfsp;
	vnode_shadow_t *vsdp;
	vnode_t *vp;
	int error;
	char *buf;

	rw_exit(&vfsp->vfs_shadow_lock);

	if (sip->si_fid.fid_len > MAXFIDSZ)
		return (EINVAL);

	if ((error = VFS_VGET(rootvp->v_vfsp, &vp, &sip->si_fid)) != 0)
		return (error);

	/* XXX zones and full paths? */

	/*
	 * First try the vnode path information.  We'd like to call
	 * vnodetopath() here, but this requires doing lookups on the
	 * filesystem.  If we're currently in 'standby' mode, then we will
	 * hang.  If we had a per-thread notion of work done on behalf of
	 * shadow migration we could skip this, but that doesn't exist.
	 */
	mutex_enter(&vp->v_lock);
	if (vp->v_path != NULL) {
		if (strlen(vp->v_path) + 1 > sip->si_length)
			error = ENOMEM;
		else
			error = ddi_copyout(vp->v_path,
			    (void *)(uintptr_t)(sip->si_buffer),
			    strlen(vp->v_path) + 1, flag);
		mutex_exit(&vp->v_lock);
		VN_RELE(vp);
		return (error);
	}
	mutex_exit(&vp->v_lock);

	/*
	 * If that didn't succeed, return the remote path information.
	 * The caller is responsible for turning this into an
	 * absolute path.
	 *
	 * XXX validation of cred?
	 */
	mutex_enter(&vp->v_vsd_lock);
	(void) vfs_shadow_instantiate(vp, &vsdp);
	mutex_exit(&vp->v_vsd_lock);

	mutex_enter(&vsdp->vns_shadow_content_lock);
	buf = vfs_shadow_attr_read(vp, &error);
	mutex_exit(&vsdp->vns_shadow_content_lock);

	VN_RELE(vp);

	if (buf == NULL) {
		buf = "";
		return (ddi_copyout(buf, (void *)(uintptr_t)sip->si_buffer,
		    1, flag));
	}

	if (strlen(buf) >= sip->si_length)
		error = ENOMEM;
	else
		error = ddi_copyout(buf, (void *)(uintptr_t)sip->si_buffer,
		    strlen(buf) + 1, flag);

	strfree(buf);
	return (0);
}

/*ARGSUSED*/
int
vfs_shadow_check_ioctl(vnode_t *vp, int cmd, intptr_t arg, int flag, cred_t *cr)
{
	shadow_ioc_t sip;
	vfs_t *vfsp = vp->v_vfsp;
	vfs_shadow_t *vsp;
	int err;

	if (cmd < SHADOW_IOC_PROCESS || cmd > SHADOW_IOC_FID2PATH)
		return (ENOTSUP);

	rw_enter(&vfsp->vfs_shadow_lock, RW_READER);

	if ((vsp = vfsp->vfs_shadow) == NULL) {
		rw_exit(&vfsp->vfs_shadow_lock);
		return (0);
	}

	if ((err = ddi_copyin((void *)arg, &sip, sizeof (shadow_ioc_t),
	    flag)) != 0) {
		rw_exit(&vfsp->vfs_shadow_lock);
		return (err);
	}

	switch (cmd) {
	case SHADOW_IOC_PROCESS:
		err = vfs_shadow_ioc_process(vp, vsp, &sip, flag);
		break;

	case SHADOW_IOC_GETPATH:
		err = vfs_shadow_ioc_getpath(vp, &sip, flag);
		break;

	case SHADOW_IOC_MIGRATE:
		err = vfs_shadow_ioc_migrate(vp, &sip);
		break;

	case SHADOW_IOC_FID2PATH:
		err = vfs_shadow_ioc_fid2path(vp, &sip, flag, cr);
		break;
	}

	if (err == 0)
		err = ddi_copyout(&sip, (void *)arg, sizeof (sip), flag);

	return (err);
}

static int
vfs_shadow_fsid_compare(const void *a, const void *b)
{
	const vfs_shadow_fsid_entry_t *fa = a;
	const vfs_shadow_fsid_entry_t *fb = b;
	int i;

	for (i = 0; i < 2; i++) {
		if (fa->vsfe_fsid.val[i] < fb->vsfe_fsid.val[i])
			return (-1);
		if (fb->vsfe_fsid.val[i] < fa->vsfe_fsid.val[i])
			return (1);
	}

	return (0);
}

void
vfs_shadow_setup(vfs_t *vfsp, vnode_t *root, boolean_t standby)
{
	vfs_shadow_t *vsp;
	vfs_shadow_fsid_entry_t *vfp;
	int i;

	ASSERT(vfsp->vfs_shadow == NULL);

	if (root == NULL && !standby)
		return;

	vfsp->vfs_shadow = vsp = kmem_zalloc(sizeof (vfs_shadow_t),
	    KM_SLEEP);
	if (standby) {
		ASSERT(root == NULL);
		vsp->vs_standby = B_TRUE;
	} else {
		ASSERT(root != NULL);
		vsp->vs_root = root;
	}

	mutex_init(&vsp->vs_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&vsp->vs_resync_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&vsp->vs_fsid_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&vsp->vs_timeout_lock, NULL, MUTEX_DEFAULT, NULL);

	for (i = 0; i < 3; i++)
		avl_create(&vsp->vs_removed[i], vfs_shadow_pending_compare,
		    sizeof (vfs_shadow_remove_entry_t),
		    offsetof(vfs_shadow_remove_entry_t, vsre_link));
	avl_create(&vsp->vs_fsid_table, vfs_shadow_fsid_compare,
	    sizeof (vfs_shadow_fsid_entry_t),
	    offsetof(vfs_shadow_fsid_entry_t, vsfe_link));

	/*
	 * Setup default (0) fsid index.
	 */
	if (root != NULL) {
		vfp = kmem_zalloc(sizeof (vfs_shadow_fsid_entry_t), KM_SLEEP);
		bcopy(&root->v_vfsp->vfs_fsid, &vfp->vsfe_fsid,
		    sizeof (fsid_t));
		avl_add(&vsp->vs_fsid_table, vfp);
		vsp->vs_fsid_idx = 1;
	}
}

vfs_shadow_t *
vfs_shadow_teardown(vfs_t *vfsp)
{
	vfs_shadow_t *vsp;
	vfs_shadow_remove_entry_t *vrp;
	vfs_shadow_fsid_entry_t *vfp;
	int i;
	timeout_id_t id;
	vfs_shadow_timeout_data_t *vtp;

	if ((vsp = vfsp->vfs_shadow) == NULL)
		return (NULL);

	id = vfs_shadow_suspend(vsp, &vtp);

	if (vsp->vs_active != NULL)
		vfs_shadow_pending_close_vp(vsp->vs_active);

	if (vsp->vs_shadow_dir) {
		VN_RELE(vsp->vs_shadow_dir);
		vsp->vs_shadow_dir = NULL;
	}

	for (i = 0; i < 3; i++) {
		while ((vrp = avl_first(&vsp->vs_removed[i])) != NULL) {
			avl_remove(&vsp->vs_removed[i], vrp);
			kmem_free(vrp, sizeof (vfs_shadow_remove_entry_t));
		}
	}

	while ((vfp = avl_first(&vsp->vs_fsid_table)) != NULL) {
		avl_remove(&vsp->vs_fsid_table, vfp);
		kmem_free(vfp, sizeof (vfs_shadow_fsid_entry_t));
	}
	vsp->vs_fsid_loaded = B_FALSE;

	if (vsp->vs_root != NULL) {
		ASSERT(!vsp->vs_standby);
		VN_RELE(vsp->vs_root);
	}

	vfsp->vfs_shadow = NULL;

	mutex_enter(&vfsp->vfs_shadow_cv_lock);
	cv_broadcast(&vfsp->vfs_shadow_cv);
	mutex_exit(&vfsp->vfs_shadow_cv_lock);

	ASSERT(vsp->vs_timeout == NULL);
	ASSERT(vsp->vs_timeout_data == NULL);
	vsp->vs_timeout = id;
	vsp->vs_timeout_data = vtp;

	return (vsp);
}

void
vfs_shadow_free(vfs_shadow_t *vsp)
{
	if (vsp == NULL)
		return;

	mutex_enter(&vsp->vs_timeout_lock);
	if (vsp->vs_timeout != NULL) {
		vfs_shadow_timeout_data_t *vtp = vsp->vs_timeout_data;
		ASSERT(vtp != NULL);
		(void) untimeout(vsp->vs_timeout);
		vtp->vst_flags |= VST_FLAGS_QUIT;
		cv_signal(&vtp->vst_cv);
		mutex_exit(&vsp->vs_timeout_lock);
		thread_join(vtp->vst_tid);
		cv_destroy(&vtp->vst_cv);
		kmem_free(vtp, sizeof (vfs_shadow_timeout_data_t));
	}
	mutex_destroy(&vsp->vs_timeout_lock);
	mutex_destroy(&vsp->vs_fsid_lock);
	mutex_destroy(&vsp->vs_resync_lock);
	mutex_destroy(&vsp->vs_lock);
	kmem_free(vsp, sizeof (vfs_shadow_t));
}

void
vnode_shadow_free(vnode_shadow_t *vsdp)
{
	vnode_shadow_range_t *vrp;

	if (!(vsdp->vns_have_map))
		return;

	while ((vrp = avl_first(&vsdp->vns_space_map)) != NULL) {
		avl_remove(&vsdp->vns_space_map, vrp);
		kmem_free(vrp, sizeof (vnode_shadow_range_t));
	}

	vsdp->vns_have_map = B_FALSE;
}

/*
 * Unmounting a filesystem is a little annoying when it comes to our pending log
 * files.  Normally, we keep these files open to allow for easy appends, but
 * this would prevent the filesystem from being unmounted by virtue of the
 * vnode holds.  We don't have or want visbility into the internal unmount
 * machinations of each filesystem, so before we do the unmount, we switch to a
 * mode where the file is only open long enough to write the record and is
 * otherwise generally kept closed.  We then collapse the pending log, which
 * should leave the files closed.  We restore the previous mode after an
 * unmount, so that if it failed we go back to the normal operating mode.
 */
void
vfs_shadow_pre_unmount(vfs_t *vfsp)
{
	vfs_shadow_t *vsp;
	timeout_id_t id;
	vfs_shadow_timeout_data_t *vtp;

	if (vfsp->vfs_shadow == NULL)
		return;

	rw_enter(&vfsp->vfs_shadow_lock, RW_READER);

	if ((vsp = vfsp->vfs_shadow) != NULL) {
		mutex_enter(&vsp->vs_lock);
		vsp->vs_close_on_update = B_TRUE;
		mutex_exit(&vsp->vs_lock);

		/*
		 * This is called twice so both pending lists reflect recent
		 * removals.
		 */
		vfs_shadow_pending_collapse(vfsp, B_FALSE);
		vfs_shadow_pending_collapse(vfsp, B_FALSE);

		id = vfs_shadow_suspend(vsp, &vtp);
	} else {
		id = NULL;
	}

	if (vsp->vs_shadow_dir != NULL) {
		VN_RELE(vsp->vs_shadow_dir);
		vsp->vs_shadow_dir = NULL;
	}

	rw_exit(&vfsp->vfs_shadow_lock);

	if (id != NULL) {
		ASSERT(vtp != NULL);
		(void) untimeout(id);
		mutex_enter(&vsp->vs_lock);
		vtp->vst_flags |= VST_FLAGS_QUIT;
		cv_signal(&vtp->vst_cv);
		mutex_exit(&vsp->vs_lock);
		thread_join(vtp->vst_tid);
		cv_destroy(&vtp->vst_cv);
		kmem_free(vtp, sizeof (vfs_shadow_timeout_data_t));
	}
}

void
vfs_shadow_post_unmount(vfs_t *vfsp)
{
	vfs_shadow_t *vsp;

	if (vfsp->vfs_shadow == NULL)
		return;

	rw_enter(&vfsp->vfs_shadow_lock, RW_READER);

	if ((vsp = vfsp->vfs_shadow) != NULL) {
		mutex_enter(&vsp->vs_lock);
		vsp->vs_close_on_update = B_FALSE;
		mutex_exit(&vsp->vs_lock);

		vfs_shadow_resume(vfsp);
	}

	mutex_enter(&vfsp->vfs_shadow_cv_lock);
	cv_broadcast(&vfsp->vfs_shadow_cv);
	mutex_exit(&vfsp->vfs_shadow_cv_lock);

	rw_exit(&vfsp->vfs_shadow_lock);
}

/*
 * The following are all debug functions used by the test kernel driver.  They
 * are not declared in a header file, but are also not static.  Do not change
 * these functions without also updating the shadowtest kernel driver.
 */

/*
 * Explicitly rotate the pending log.
 */
int
vfs_shadow_dbg_rotate(vfs_t *vfsp)
{
	rw_enter(&vfsp->vfs_shadow_lock, RW_READER);

	if (vfsp->vfs_shadow == NULL) {
		rw_exit(&vfsp->vfs_shadow_lock);
		return (EINVAL);
	}

	vfs_shadow_pending_collapse(vfsp, B_FALSE);

	rw_exit(&vfsp->vfs_shadow_lock);
	return (0);
}

/*
 * Sets the vfs_t into a state where all calls to migration sping waiting for a
 * signal.  This can be used to test that EINTR is supported properly.
 */
int
vfs_shadow_dbg_spin(vfs_t *vfsp, boolean_t state)
{
	vfs_shadow_t *vsp;

	rw_enter(&vfsp->vfs_shadow_lock, RW_READER);

	if ((vsp = vfsp->vfs_shadow) == NULL) {
		rw_exit(&vfsp->vfs_shadow_lock);
		return (EINVAL);
	}

	vfs_shadow_pending_collapse(vfsp, B_FALSE);

	mutex_enter(&vsp->vs_lock);
	if (state)
		vsp->vs_dbg_flags |= VFS_SHADOW_DBG_SPIN;
	else
		vsp->vs_dbg_flags &= ~VFS_SHADOW_DBG_SPIN;
	mutex_exit(&vsp->vs_lock);

	rw_exit(&vfsp->vfs_shadow_lock);
	return (0);
}

/*
 * This function is used by the NFS server to avoid granting delegations that
 * currently shadowed.  This may return false positives, and cannot be relied
 * upon to be correct in the presence of ongoing concurrency or other
 * factories.  A negative response is always guaranteed to be correct, in the
 * absence of a filesystem remount.
 */
boolean_t
vn_is_shadow(vnode_t *vp)
{
	return (vp->v_vfsp != NULL && vp->v_vfsp->vfs_shadow != NULL &&
	    !vnode_is_migrated(vp));
}
