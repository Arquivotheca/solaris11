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

#ifndef _SYS_UVFS_VFSOPS_H
#define	_SYS_UVFS_VFSOPS_H

#include <sys/door.h>
#include <sys/libuvfs_ki.h>
#include <sys/taskq.h>
#include <sys/disp.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	UVFS_MIN_MINOR	(12)

extern vfsops_t *uvfs_vfsops;

/*
 * The list of uvfs file systems is used from uvfs_sync.  It allows us
 * to traverse all of the uvfs file systems on the system and sync all
 * of the dirty data.  Items are added to this list in uvfs_mount and
 * removed from the list in uvfs_unmount.  uvfs_fs_list_lock protects
 * the list.
 */
extern list_t uvfs_fs_list;
extern kmutex_t uvfs_fs_list_lock;

typedef struct uvfsvfs {
	uint32_t	uvfs_flags;
	kmutex_t	uvfs_lock;
	door_handle_t	uvfs_door;
	kcondvar_t	uvfs_daemon_cv;

	taskq_t		*uvfs_taskq;
	kmem_cache_t	*uvfs_write_args_cache;
	uint32_t	uvfs_write_size;
	kmem_cache_t	*uvfs_read_res_cache;
	uint32_t	uvfs_read_size;
	kmem_cache_t	*uvfs_readdir_res_cache;
	uint32_t	uvfs_readdir_size;

	uint32_t	uvfs_max_dthreads;

	libuvfs_fid_t	uvfs_root_fid;

	cred_t		*uvfs_mount_cred;
	boolean_t	uvfs_allow_other;

	vfs_t		*uvfs_vfsp;
	vnode_t		*uvfs_rootvp;
	list_t		uvfs_uvnodes;		/* this file system's uvnodes */
	kmutex_t	uvfs_uvnodes_lock;	/* protects uvfs_uvnodes */
	list_node_t	uvfs_fs_list_node;	/* entry in uvfs_fs_list */
	vnodeops_t	*uvfs_dvnodeops;
	vnodeops_t	*uvfs_fvnodeops;
	vnodeops_t	*uvfs_symvnodeops;
	vnodeops_t	*uvfs_evnodeops;
} uvfsvfs_t;

#define	UVFS_VFS_ENTER(uvfsvfs) { \
	if (uvfsvfs->uvfs_flags & (UVFS_UNMOUNTED|UVFS_SHUTDOWN)) \
		return (EIO); \
}
#define	UVFS_VFS_EXIT(uvfsvfs)

/*
 * values for uvfs_flags
 */
#define	UVFS_FLAG_MOUNT_COMPLETE	(0x01)
#define	UVFS_FLAG_ROOTFID		(0x02)
#define	UVFS_FLAG_ROOTVP		(0x04)
#define	UVFS_SHUTDOWN			(0x08)
#define	UVFS_UNMOUNTED			(0x10)
#define	UVFS_FORCEDIRECT		(0x20)

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_UVFS_VFSOPS_H */
