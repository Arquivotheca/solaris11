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

#ifndef _SYS_LIBUVFS_KI_H
#define	_SYS_LIBUVFS_KI_H

#include <sys/vfs.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/mount.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	LIBUVFS_MAX_FID_LEN	MAXFIDSZ

typedef struct {
	uint64_t uvfid_len;
	uint8_t uvfid_data[LIBUVFS_MAX_FID_LEN];
} libuvfs_fid_t;

typedef enum {
	UVFS_CB_VOP_OPEN,
	UVFS_CB_VOP_CLOSE,
	UVFS_CB_VOP_READ,
	UVFS_CB_VOP_WRITE,
	UVFS_CB_VOP_GETATTR,
	UVFS_CB_VOP_SETATTR,
	UVFS_CB_VOP_LOOKUP,
	UVFS_CB_VOP_CREATE,
	UVFS_CB_VOP_REMOVE,
	UVFS_CB_VOP_LINK,
	UVFS_CB_VOP_RENAME,
	UVFS_CB_VOP_MKDIR,
	UVFS_CB_VOP_RMDIR,
	UVFS_CB_VOP_READDIR,
	UVFS_CB_VOP_SYMLINK,
	UVFS_CB_VOP_READLINK,
	UVFS_CB_VOP_FSYNC,
	UVFS_CB_VOP_SPACE,
	UVFS_CB_VOP_ADDMAP,
	UVFS_CB_VOP_DELMAP,

	UVFS_CB_MAX_VOP = UVFS_CB_VOP_DELMAP,

	UVFS_CB_VFS_MOUNT,
	UVFS_CB_VFS_UNMOUNT,
	UVFS_CB_VFS_ROOT,
	UVFS_CB_VFS_STATVFS,
	UVFS_CB_VFS_SYNC,
	UVFS_CB_VFS_VGET,
	UVFS_CB_VFS_MOUNTROOT,
	UVFS_CB_NUM_OPS
} libuvfs_optag_t;

typedef struct {
	libuvfs_fid_t	l_fid;
	libuvfs_fid_t	l_pfid;
	uint64_t	l_atime[2];
	uint64_t	l_mtime[2];
	uint64_t	l_ctime[2];
	uint64_t	l_size;
	uint64_t	l_blksize;
	uint64_t	l_blocks;
	uint64_t	l_links;
	uint64_t	l_uid;
	uint64_t	l_gid;
	uint64_t	l_mode;
	uint64_t	l_id;
	uint64_t	l_gen;
	uint64_t	l_rdev;
} libuvfs_stat_t;

#define	UVFS_ARGHEAD(optag) \
	uint64_t optag

#define	UVFS_VOP_ARGHEAD(optag, fid) \
	UVFS_ARGHEAD(optag); \
	libuvfs_fid_t fid

#define	UVFS_RESHEAD(errno) \
	uint64_t errno

typedef struct {
	UVFS_ARGHEAD(lca_optag);
} libuvfs_common_arg_t;

typedef struct {
	UVFS_VOP_ARGHEAD(lca_optag, lca_fid);
} libuvfs_common_vop_arg_t;

typedef struct {
	UVFS_RESHEAD(lcr_error);
} libuvfs_common_res_t;

typedef struct {
	UVFS_RESHEAD(root_error);
	libuvfs_fid_t root_fid;
	libuvfs_stat_t root_stat;
} libuvfs_cb_vfsroot_res_t;

typedef struct {
	UVFS_RESHEAD(lcsa_error);

	uint64_t lcsa_bsize;
	uint64_t lcsa_frsize;
	uint64_t lcsa_blocks;
	uint64_t lcsa_bfree;
	uint64_t lcsa_bavail;
	uint64_t lcsa_files;
	uint64_t lcsa_ffree;
	uint64_t lcsa_favail;
	uint64_t lcsa_namemax;
} libuvfs_cb_statvfs_res_t;

typedef struct {
	UVFS_RESHEAD(lcgr_error);
	libuvfs_stat_t lcgr_stat;
} libuvfs_cb_getattr_res_t;

typedef struct {
	UVFS_VOP_ARGHEAD(lcmd_optag, lcmd_dirfid);
	char		lcmd_name[MAXPATHLEN];
	libuvfs_stat_t	lcmd_creation_attrs;
} libuvfs_cb_mkdir_arg_t;

typedef struct {
	UVFS_RESHEAD(lcmd_error);

	libuvfs_fid_t	lcmd_fid;
	libuvfs_stat_t	lcmd_stat;
} libuvfs_cb_mkdir_res_t;

typedef struct {
	UVFS_VOP_ARGHEAD(lccf_optag, lccf_dirfid);
	char		lccf_name[MAXPATHLEN];
	uint64_t	lccf_mode;	/* creation mode */
	libuvfs_stat_t	lccf_creation_attrs;
} libuvfs_cb_create_arg_t;

typedef struct {
	UVFS_RESHEAD(lccf_error);
	libuvfs_fid_t	lccf_fid;
	libuvfs_stat_t	lccf_stat;	/* returned attrs */
} libuvfs_cb_create_res_t;

typedef struct {
	UVFS_VOP_ARGHEAD(lcla_optag, lcla_dirfid);
	char 		lcla_nm[MAXPATHLEN];
} libuvfs_cb_lookup_arg_t;

typedef struct {
	UVFS_RESHEAD(lclr_error);

	libuvfs_fid_t	lclr_fid;
	libuvfs_stat_t	lclr_stat;
} libuvfs_cb_lookup_res_t;

typedef struct {
	UVFS_VOP_ARGHEAD(lcvg_optag, lcvg_fid);
} libuvfs_cb_vget_arg_t;

typedef struct {
	UVFS_RESHEAD(lcvg_error);
	libuvfs_stat_t	lcvg_stat;
} libuvfs_cb_vget_res_t;

typedef struct {
	UVFS_VOP_ARGHEAD(lcwa_optag, lcwa_fid);
	uint64_t	lcwa_length;
	uint64_t	lcwa_offset;
} libuvfs_cb_write_arg_t;

typedef struct {
	UVFS_RESHEAD(lcwr_error);
	libuvfs_stat_t	lcwr_stat;
	uint64_t	lcwr_bytes_written;
} libuvfs_cb_write_res_t;

typedef struct {
	UVFS_VOP_ARGHEAD(lcra_optag, lcra_fid);
	uint64_t	lcra_offset;
	uint64_t	lcra_len;
} libuvfs_cb_read_arg_t;

typedef struct {
	UVFS_RESHEAD(lcrr_error);
	uint64_t	lcrr_length;
	libuvfs_stat_t	lcrr_stat;
} libuvfs_cb_read_res_t;

typedef struct {
	UVFS_VOP_ARGHEAD(lcrda_optag, lcrda_fid);
	uint64_t	lcrda_flags;
	uint64_t	lcrda_offset;
	uint64_t	lcrda_length;
} libuvfs_cb_readdir_arg_t;

typedef struct {
	UVFS_RESHEAD(lcrdr_error);
	uint64_t	lcrdr_offset;
	uint64_t	lcrdr_length;
	uint64_t	lcrdr_eof;
} libuvfs_cb_readdir_res_t;

typedef struct {
	UVFS_VOP_ARGHEAD(lcuf_optag, lcuf_dirfid);
	char	lcuf_file[MAXPATHLEN];
} libuvfs_cb_unlink_arg_t;

typedef struct {
	UVFS_VOP_ARGHEAD(lcsa_optag, lcsa_fid);
	uint64_t lcsa_mask;
	libuvfs_stat_t lcsa_attributes;
} libuvfs_cb_setattr_arg_t;

typedef struct {
	UVFS_RESHEAD(set_error);
	libuvfs_stat_t set_attributes;
} libuvfs_cb_setattr_res_t;

typedef struct {
	UVFS_VOP_ARGHEAD(lcof_optag, lcof_fid);
	uint64_t lcof_mode;
	uint64_t lcof_open_count;
} libuvfs_cb_open_arg_t;

typedef struct {
	UVFS_VOP_ARGHEAD(lccf_optag, lccf_fid);
	uint64_t lccf_mode;
	uint64_t lccf_count;
	uint64_t lccf_offset;
} libuvfs_cb_close_arg_t;

typedef struct {
	UVFS_VOP_ARGHEAD(lcma_optag, lcma_fid);
	uint64_t lcma_count;
} libuvfs_cb_map_arg_t;

typedef struct {
	UVFS_VOP_ARGHEAD(lcrf_optag, lcrf_fid);
	char		lcrf_name[MAXPATHLEN];
	uint64_t	lcrf_flags;
} libuvfs_cb_remove_arg_t;

typedef struct {
	UVFS_VOP_ARGHEAD(lcrd_optag, lcrd_fid);
	char		lcrd_name[MAXPATHLEN];
	uint64_t	lcrd_flags;
} libuvfs_cb_rmdir_arg_t;

typedef struct {
	UVFS_VOP_ARGHEAD(lclf_optag, lclf_dirfid);
	libuvfs_fid_t lclf_sfid;
	char lclf_name[MAXPATHLEN];
	uint64_t lclf_flags;
} libuvfs_cb_link_arg_t;

typedef struct {
	UVFS_VOP_ARGHEAD(lcsl_optag, lcsl_dirfid);
	char lcsl_name[MAXPATHLEN];
	char lcsl_link[MAXPATHLEN];
} libuvfs_cb_symlink_arg_t;

typedef struct {
	UVFS_VOP_ARGHEAD(lcrl_optag, lcrl_dirfid);
} libuvfs_cb_readlink_arg_t;

typedef struct {
	UVFS_RESHEAD(lcrl_error);
	uint64_t	lcrl_length;
} libuvfs_cb_readlink_res_t;

typedef struct {
	UVFS_VOP_ARGHEAD(lcia_optag, lcia_fid);
} libuvfs_cb_inactive_arg_t;

typedef struct {
	UVFS_VOP_ARGHEAD(lcrn_optag, lcrn_sdfid);
	libuvfs_fid_t lcrn_tdfid;
	char lcrn_sname[MAXPATHLEN];
	char lcrn_tname[MAXPATHLEN];
} libuvfs_cb_rename_arg_t;

typedef struct {
	UVFS_VOP_ARGHEAD(lcfs_optag, lcfs_fid);
	uint64_t lcfs_offset;
	uint64_t lcfs_len;
	uint64_t lcfs_flag;
} libuvfs_cb_space_arg_t;

typedef struct {
	UVFS_VOP_ARGHEAD(lcfs_optag, lcfs_fid);
	uint64_t lcfs_syncflag;
} libuvfs_cb_fsync_arg_t;

#define	MNTOPT_ALLOW_OTHER	"allow_other"	  /* access also to others */
#define	MNTOPT_NOALLOW_OTHER	"noallow_other"	  /* access to mount user */
#define	MNTOPT_MAX_READ		"max_read"	  /* max bytes per read */
#define	MNTOPT_MAX_WRITE	"max_write"	  /* max bytes per write */
#define	MNTOPT_MAX_DTHREADS	"max_dthreads"	  /* max daemon threads */
#define	MNTOPT_DIRECTIO		"direct_io"	  /* convert to forcedirectio */
#define	MNTOPT_DEFAULT_PERMS	"default_permissions" /* cvt to access_upcall */

typedef struct {
	ulong_t max_read;
	ulong_t max_write;
	ulong_t max_dthreads;
} uvfs_mount_opts_t;

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_LIBUVFS_KI_H */
