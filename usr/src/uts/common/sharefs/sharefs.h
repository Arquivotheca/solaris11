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
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SHAREFS_SHAREFS_H
#define	_SHAREFS_SHAREFS_H

/*
 * This header provides service for the sharefs module.
 */

#include <sys/types.h>
#include <sys/zone.h>
#include <sys/modctl.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/gfs.h>
#include <sharefs/share.h>
#include <sharefs/sharetab.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	SHAREFS_ROOT	"/etc/dfs"
#define	SHAREFS_BASE	"sharetab"

#define	SHAREFS_NAME_MAX	MAXNAMELEN

typedef struct sharefs_zone {
	kmutex_t	sz_lock;
	int		sz_refcount;
	int		sz_enabled;
	int		sz_mounted;
	zone_t		*sz_zonep;
	zone_ref_t	sz_zone_ref;
	int		sz_shnode_count;
	void		*sz_shtab_cache;
	krwlock_t	sz_sharefs_rwlock;	/* protect vnode/vfs ops */
	minor_t		sz_minordev;
	list_node_t	sz_gnode;
} sharefs_zone_t;

typedef struct sharefs_globals {
	kmutex_t	sg_lock;
	int		sg_fstype;
	major_t		sg_majordev;
	minor_t		sg_last_minordev;
	list_t		sg_zones;
	int		sg_zone_cnt;
	zone_key_t	sg_sfszone_key;
} sharefs_globals_t;

extern sharefs_globals_t sfs;

/*
 * VFS data object
 */
typedef struct sharefs_vfs {
	vnode_t	*sharefs_vfs_root;
	sharefs_zone_t	*sharefs_zsd;
} sharefs_vfs_t;

#define	SHAREFS_INO_FILE	0x80

extern vnode_t *sharefs_create_root_file(sharefs_zone_t *, vfs_t *);

/*
 * Sharetab file
 *
 * Note that even though the sharetab code does not explictly
 * use 'sharefs_file', it is required by GFS that the first
 * field of the private data be a gfs_file_t.
 */
typedef struct shnode_t {
	gfs_file_t	sharefs_file;		/* gfs file */
	char		*sharefs_snap;		/* snapshot of the share */
	size_t		sharefs_size;		/* size of the snapshot */
	uint_t		sharefs_count;		/* number of shares */
	uint_t		sharefs_real_vp;	/* Are we a real or snap */
	uint_t		sharefs_generation;	/* Which copy are we? */
	timestruc_t	sharefs_mtime;		/* When were we modded? */
} shnode_t;

typedef struct shtab_stats_s {
	size_t		sts_size;
	uint_t		sts_count;
	uint_t		sts_generation;
	timestruc_t	sts_mtime;
} shtab_stats_t;

/*
 * Some conversion macros:
 */
#define	VTOSH(vp)	((shnode_t *)((vp)->v_data))
#define	VTOZSD(vp) (((sharefs_vfs_t *)((vp)->v_vfsp->vfs_data))->sharefs_zsd)

extern const fs_operation_def_t	sharefs_tops_data[];
extern vnodeops_t		*sharefs_ops_data;

extern void sharefs_update_minor(minor_t, minor_t);
extern sharefs_zone_t *sharefs_zone_lookup();
extern sharefs_zone_t *sharefs_zone_create();
extern void sharefs_zone_rele(sharefs_zone_t *);
extern void sharefs_zone_shnode_hold(sharefs_zone_t *);
extern void sharefs_zone_shnode_rele(sharefs_zone_t *);

extern void shtab_cache_init(sharefs_zone_t *);
extern void shtab_cache_fini(sharefs_zone_t *);
extern int shtab_snap_create(sharefs_zone_t *, shnode_t *);
extern int shtab_stats(sharefs_zone_t *, shtab_stats_t *);
extern boolean_t shtab_is_empty(sharefs_zone_t *);
extern int shtab_cache_flush(sharefs_zone_t *);
extern int shtab_cache_lookup(sharefs_zone_t *, char *, char *, uint32_t,
    char *, size_t);
extern int shtab_cache_find_init(sharefs_zone_t *, char *, uint32_t,
    sharefs_find_hdl_t *);
extern int shtab_cache_find_next(sharefs_zone_t *, sharefs_find_hdl_t *,
    char *, size_t);
extern int shtab_cache_find_fini(sharefs_zone_t *, sharefs_find_hdl_t *);

#ifdef __cplusplus
}
#endif

#endif /* !_SHAREFS_SHAREFS_H */
