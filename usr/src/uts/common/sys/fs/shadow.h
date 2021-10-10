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

#ifndef	_SYS_FS_SHADOW_H
#define	_SYS_FS_SHADOW_H

#include <sys/vfs.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * This is not a filesystem, but generic VFS infrastructure for creating
 * filesystems that shadow a different underlying directory and transparently
 * migrate data to the local filesystem on demand.
 */
extern int vfs_shadow_check_root(vfs_t *);

extern int vfs_shadow_check_vp(vnode_t *, boolean_t, int *);
extern int vfs_shadow_check_attr_vp(vnode_t *, int *);
extern int vfs_shadow_check_uio(vnode_t *, uio_t *, boolean_t);
extern int vfs_shadow_check_range(vnode_t *, uint64_t, uint64_t);
extern int vfs_shadow_check_ioctl(vnode_t *, int, intptr_t, int, cred_t *);

#define	VFS_SHADOW_CHECK_ALL(vp)					\
	if ((vp)->v_vfsp != NULL && (vp)->v_vfsp->vfs_shadow != NULL) {	\
		int shadow_err = vfs_shadow_check_vp((vp), B_FALSE, NULL); \
		if (shadow_err != 0)					\
			return (shadow_err);				\
	}

/*
 * This version migrates only the range of data specifies by the uio_t.
 */
#define	VFS_SHADOW_CHECK_UIO(vp, uio, readheld)				\
	if ((vp)->v_vfsp != NULL && (vp)->v_vfsp->vfs_shadow != NULL) {	\
		int shadow_err = vfs_shadow_check_uio((vp), (uio),	\
		    (readheld));					\
		if (shadow_err != 0)					\
			return (shadow_err);				\
	}

/*
 * This function migrates directories and does nothing for files.  In general
 * the operations are not supported for files, but this also has the effect of
 * allowing xattr lookups (which do not need migration because they are
 * migrated with the directory entry).
 */
#define	VFS_SHADOW_CHECK_DIR(vp)					\
	if ((vp)->v_vfsp != NULL &&					\
	    (vp)->v_vfsp->vfs_shadow != NULL &&				\
	    (vp)->v_type == VDIR) {					\
		int shadow_err = vfs_shadow_check_vp((vp), B_FALSE, NULL); \
		if (shadow_err != 0)					\
			return (shadow_err);				\
	}

/*
 * This function migrates directories and does nothing for files.  In general
 * the operations are not supported for files, but this also has the effect of
 * allowing xattr lookups (which do not need migration because they are
 * migrated with the directory entry).
 */
#define	VFS_SHADOW_CHECK_DIR_HELD(vp)					\
	if ((vp)->v_vfsp != NULL &&					\
	    (vp)->v_vfsp->vfs_shadow != NULL &&				\
	    (vp)->v_type == VDIR) {					\
		int shadow_err = vfs_shadow_check_vp((vp), B_TRUE, NULL); \
		if (shadow_err != 0)					\
			return (shadow_err);				\
	}

/*
 * Like the uio version, this checks for migration only over a particular
 * range.
 */
#define	VFS_SHADOW_CHECK_RANGE(vp, off, len)				\
	if ((vp)->v_vfsp != NULL && (vp)->v_vfsp->vfs_shadow != NULL) {	\
		int shadow_err = vfs_shadow_check_range((vp), (off),	\
		    (off) + (len));					\
		if (shadow_err != 0)					\
			return (shadow_err);				\
	}

/* XXX make these real */
#define	VFS_SHADOW_CHECK_SETATTR(vp, vap)				\
	VFS_SHADOW_CHECK_ALL(vp)
#define	VFS_SHADOW_CHECK_SPACE(vp, off, flag)				\
	VFS_SHADOW_CHECK_ALL(vp)

/*
 * We explicitly do not check for shadowed vnodes as part of queries that only
 * need to access attributes of a file or directory.  The attributes of
 * importance are migrated as part of migrating the initial directory entry.
 * The only attributes that may be inaccurate are the number of links and the
 * physical size of the file.  The former cannot be solved without migrating
 * all files, and the latter requires migrating the entire file, which yields a
 * poor user experience and prevents future optimizations (such as pulling only
 * portions of a file).  So we live with the fact that the number of blocks may
 * be incorrect for unmigrated files within a shadow filesystem.
 *
 * The exception to this rule is the root directory.  Because we rely
 * on the shadow attribute to kick off the beginning of the migration,
 * we need to make sure that we invoke vfs_shadow_check_vp() even if
 * the first operation is a getattr().
 */
#define	VFS_SHADOW_CHECK_ATTR(vp)					\
	if ((vp)->v_vfsp != NULL && (vp)->v_vfsp->vfs_shadow != NULL &&	\
	    ((vp)->v_flag & VROOT)) {					\
		int shadow_err =					\
		    vfs_shadow_check_attr_vp((vp), NULL);		\
		if (shadow_err != 0)					\
			return (shadow_err);				\
	}

typedef struct vfs_shadow_remove_entry {
	avl_node_t			vsre_link;
	fid_t				vsre_fid;
} vfs_shadow_remove_entry_t;

typedef struct vfs_shadow_fsid_entry {
	avl_node_t			vsfe_link;
	fsid_t				vsfe_fsid;
	uint32_t			vsfe_idx;
} vfs_shadow_fsid_entry_t;

typedef enum vfs_shadow_debug_flags {
	VFS_SHADOW_DBG_SPIN	= 0x01	/* spin during migration */
} vfs_shadow_debug_flags_t;

typedef enum v_shadowstate {
	V_SHADOW_UNINITIALIZED = 0,
	V_SHADOW_UNKNOWN,
	V_SHADOW_MIGRATING_SELF,
	V_SHADOW_MIGRATING_DATA,
	V_SHADOW_MIGRATED
} v_shadowstate_t;

/*
 * Locking notes:
 *	vs_fsid_lock should be held to change/examine
 *		the fsid table
 *
 *	vs_resync_lock should be held to change/examine
 *		vs_removed
 *
 *	vs_timeout_lock should be held to change/examine
 *		vs_timeout, vs_timeout_data, and vs_gen, or vtp->*
 *
 *	vs_lock should be held to change/examine
 *		everything else vs_*
 *
 * If multiple locks are needed, we grab vs_lock last.  There is one
 * instance where we need all three of the resync, timeout and vs_lock,
 * and that is the order in which we acquire them.
 */
typedef struct vfs_shadow {
	vnode_t				*vs_root;
	vnode_t				*vs_active;
	vnode_t				*vs_shadow_dir;
	int				vs_active_idx;
	avl_tree_t			vs_removed[3];
	int				vs_removed_idx;
	kmutex_t			vs_lock;
	kmutex_t			vs_resync_lock;
	kmutex_t			vs_timeout_lock;
	boolean_t			vs_close_on_update;
	int				vs_dbg_flags;
	avl_tree_t			vs_fsid_table;
	kmutex_t			vs_fsid_lock;
	uint32_t			vs_fsid_idx;
	boolean_t			vs_fsid_loaded;
	uint64_t			vs_gen;
	timeout_id_t			vs_timeout;
	void				*vs_timeout_data;
	boolean_t			vs_standby;
	ulong_t				vs_aclflags;
	fid_t				vs_last_processed;
	fid_t				vs_error_fid[2];
	boolean_t			vs_error_valid[2];
	boolean_t			vs_error_seen[2];
	boolean_t			vs_process_inprogress;
} vfs_shadow_t;

typedef struct vnode_shadow_range {
	avl_node_t			vsr_link;
	uint64_t			vsr_start;
	uint64_t			vsr_end;
} vnode_shadow_range_t;

typedef struct vnode_shadow {
	vnode_t		*vns_vnode;	/* back pointer */
	kcondvar_t	vns_shadow_state_cv; /* for waiters checking state */
	kmutex_t	vns_shadow_content_lock;  /* data manipulation lock */
	boolean_t	vns_have_map;	/* whether or not map was read */
	avl_tree_t	vns_space_map;	/* map of migrated data */
} vnode_shadow_t;

extern vfs_shadow_t *vfs_shadow_teardown(vfs_t *);
extern void vfs_shadow_free(vfs_shadow_t *);
extern void vfs_shadow_setup(vfs_t *, vnode_t *, boolean_t);
extern void vfs_shadow_vnode_init(vnode_t *);
extern void vfs_shadow_create_cache(void);

extern void vfs_shadow_pre_unmount(vfs_t *);
extern void vfs_shadow_post_unmount(vfs_t *);

extern void vnode_shadow_free(vnode_shadow_t *);

extern boolean_t vn_is_shadow(vnode_t *);

/*
 * The following are private on-disk data structures, but are exposed through
 * this header for testing purposes.
 */
#define	VFS_SHADOW_ATTR			"SUNWshadow"
#define	VFS_SHADOW_MAP			"SUNWshadow.map"

#define	VFS_SHADOW_PRIVATE_DIR		".SUNWshadow"
#define	VFS_SHADOW_PRIVATE_LINK		"link"
#define	VFS_SHADOW_PRIVATE_FSID		"fsid"
#define	VFS_SHADOW_PRIVATE_PENDING	"pending"

#define	VFS_SHADOW_ATTR_LIST_MAGIC	0x592A
#define	VFS_SHADOW_SPACE_MAP_MAGIC	0x592B

#define	VFS_SHADOW_INTENT_VERSION	1
#define	VFS_SHADOW_SPACE_MAP_VERSION	1

typedef struct vfs_shadow_header {
	uint16_t	vsh_magic;
	uint8_t		vsh_version;
	uint8_t		vsh_bigendian;
} vfs_shadow_header_t;

typedef struct vfs_shadow_range_record {
	uint64_t	vsrr_type;
	uint64_t	vsrr_start;
	uint64_t	vsrr_end;
} vfs_shadow_range_record_t;

#define	VFS_SHADOW_RANGE_REMOTE		0
#define	VFS_SHADOW_RANGE_LOCAL		1

/*
 * Private libshadowfs ioctls.
 */
#define	SHADOW_IOC		('S' << 24 | 'h' << 16 | 'W' << 8)
#define	SHADOW_IOC_PROCESS	(SHADOW_IOC | 0x01)
#define	SHADOW_IOC_GETPATH	(SHADOW_IOC | 0x02)
#define	SHADOW_IOC_MIGRATE	(SHADOW_IOC | 0x03)
#define	SHADOW_IOC_FID2PATH	(SHADOW_IOC | 0x04)

typedef struct shadow_ioc {
	uint64_t	si_buffer;
	uint64_t	si_length;
	uint64_t	si_processed;
	uint64_t	si_error;
	uint64_t	si_size;
	uint64_t	si_onlyerrors;
	fid_t		si_fid;
} shadow_ioc_t;

extern cred_t *vfs_shadow_cred;

/* vnode-specific data lookup key storage */
extern uint_t vfs_shadow_key_state;
extern uint_t vfs_shadow_key_data;
extern void vfs_shadow_vsd_destructor(void *);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FS_SHADOW_H */
