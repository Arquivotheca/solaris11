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

#ifndef _SYS_UVFS_UVNODE_H
#define	_SYS_UVFS_UVNODE_H

#include <sys/isa_defs.h>
#include <sys/types32.h>
#include <sys/uvfs_vfsops.h>
#include <vm/pvn.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct uvnode {
	kmutex_t	uv_lock;
	kmutex_t	uv_open_lock;
	krwlock_t	uv_parent_lock;
	uint32_t	uv_flags;
	krwlock_t	uv_rwlock;	/* for VOP_RWLOCK */
	vnode_t		*uv_vnode;
	libuvfs_fid_t	uv_fid;
	libuvfs_fid_t	uv_pfid;
	struct		uvnode *uv_hash_next;
	list_node_t	uv_list_node;   /* entry in uvfs_uvnodes list */

	uvfsvfs_t	*uv_uvfsvfs;
	door_handle_t	uv_door;

	uint64_t	uv_atime[2];
	uint64_t	uv_mtime[2];
	uint64_t	uv_ctime[2];
	uint64_t	uv_size;
	uint64_t	uv_blocks;
	uint64_t	uv_blksize;
	uint64_t	uv_links;
	uint64_t	uv_uid;
	uint64_t	uv_gid;
	uint64_t	uv_mode;
	uint64_t	uv_id;		/* inode number, might be fabricated */
	uint64_t	uv_seq;
	uint64_t	uv_gen;
	uint64_t	uv_mapcnt;
	uint64_t	uv_opencnt;
	uint64_t	uv_rdev;
	boolean_t	uv_size_known;
} uvnode_t;

/* Values for uv_flags */
#define	UVNODE_FLAG_DIRECTIO	0x01

extern void uvfs_uvnode_init(void);
extern void uvfs_uvnode_fini(void);
extern void uvfs_uvnode_free(uvnode_t *);
extern boolean_t uvfs_fid_match(uvnode_t *uvp, vfs_t *vfsp, libuvfs_fid_t *fid);
extern void uvfs_update_attrs(uvnode_t *uvp, libuvfs_stat_t *stat);
extern int uvfs_freesp(uvnode_t *uvp, uint64_t off, uint64_t len,
    int flag, cred_t *cr);
extern uint64_t uvfs_hash_lock(vfs_t *vfsp, libuvfs_fid_t *fid);
extern void uvfs_hash_unlock(uint64_t idx);
extern void uvfs_hash_remove(uvnode_t *uvp, uint64_t idx);

extern int uvfs_uvget(vfs_t *, uvnode_t **, libuvfs_fid_t *, libuvfs_stat_t *);
extern int uvfs_access_check(uvnode_t *, int mode, cred_t *);
extern int uvfs_directio(vnode_t *, int, cred_t *);
extern uint64_t uvfs_expldev(dev_t dev);
extern dev_t uvfs_cmpldev(uint64_t dev);
extern int uvfs_putapage(vnode_t *, page_t *, u_offset_t *, size_t *,
    int, cred_t *);

#define	UVTOV(UV)	((UV)->uv_vnode)
#define	VTOUV(VP)	((uvnode_t *)(VP)->v_data)

#define	UVFS_TIME_ENCODE(tp, stmp) 		\
{						\
	(stmp)[0] = (uint64_t)(tp)->tv_sec;     \
	(stmp)[1] = (uint64_t)(tp)->tv_nsec;    \
}

#define	UVFS_TIME_DECODE(tp, stmp)		\
{                                               \
	(tp)->tv_sec = (time_t)(stmp)[0];	\
	(tp)->tv_nsec = (long)(stmp)[1];	\
}


#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_UVFS_UVNODE_H */
