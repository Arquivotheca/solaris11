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

#ifndef _SYS_UVFS_UPCALL_H
#define	_SYS_UVFS_UPCALL_H

#include <sys/uvfs_uvnode.h>
#include <sys/fcntl.h>

/*
 * There are tunables for the values below; see the lowercase versions (i.e.
 * the symbols themselves) in the .c files.
 *
 * Max read size must be less than or equal to:
 * (door_max_upcall_reply - sizeof (libuvfs_cb_read_res_t))
 */
#define	UVFS_MAX_READ_SIZE	(1048576)
#define	UVFS_MAX_WRITE_SIZE	(1048576)
#define	UVFS_MAX_READDIR_SIZE	(8192)
#define	UVFS_MAX_DTHREADS	(16)

#ifdef	__cplusplus
extern "C" {
#endif

int uvfs_up_vfsroot(uvfsvfs_t *, libuvfs_stat_t *, cred_t *);
int uvfs_up_statvfs(uvfsvfs_t *, statvfs64_t *, cred_t *);
void uvfs_up_inactive(uvfsvfs_t *, libuvfs_fid_t *, cred_t *);
int uvfs_up_getattr(uvnode_t *, vattr_t *, cred_t *);
int uvfs_up_setattr(uvnode_t *, vattr_t *vap, libuvfs_stat_t *stat, cred_t *);
int uvfs_up_lookup(uvnode_t *, char *nm, libuvfs_fid_t *fidp,
    libuvfs_stat_t *stat, cred_t *);
int uvfs_up_mkdir(uvnode_t *, char *nm, libuvfs_fid_t *fidp, vattr_t *vap,
    libuvfs_stat_t *stat, cred_t *);
int uvfs_up_create(uvnode_t *, char *nm, vattr_t *vap, int mode,
    libuvfs_fid_t *fid, libuvfs_stat_t *stat, cred_t *);
int uvfs_up_read(uvnode_t *, caddr_t addr, int len, u_offset_t off,
    cred_t *, size_t *);
int uvfs_up_write(uvnode_t *, caddr_t addr, int length, u_offset_t off,
    cred_t *);
int uvfs_up_readdir(uvnode_t *, uio_t *uiop, int *eofp, cred_t *);
int uvfs_up_open(uvnode_t *, int, uint64_t, cred_t *);
int uvfs_up_close(uvnode_t *, int flag, int count, offset_t off, cred_t *);
int uvfs_up_remove(uvnode_t *, char *name, cred_t *);
int uvfs_up_rmdir(uvnode_t *, char *name, cred_t *);
int uvfs_up_link(uvnode_t *dvp, uvnode_t *svp, char *name, int flags, cred_t *);
int uvfs_up_symlink(uvnode_t *dvp, char *name, char *link, cred_t *);
int uvfs_up_readlink(uvnode_t *dvp, uio_t *uiop, cred_t *);
int uvfs_up_vget(uvfsvfs_t *uvfsvfsp, libuvfs_fid_t *fidp,
    libuvfs_stat_t *stat, cred_t *);
int uvfs_up_rename(uvnode_t *sduvp, char *snm, uvnode_t *tduvp, char *tnm,
    cred_t *);
int uvfs_up_space(uvnode_t *uvp, uint64_t offset, uint64_t len,
    int flag, cred_t *cr);
int uvfs_up_fsync(uvnode_t *uvp, int flag, cred_t *);
int uvfs_up_direct_read(vnode_t *, uio_t *, cred_t *);
int uvfs_up_direct_write(vnode_t *, uio_t *, cred_t *, int);
int uvfs_up_addmap(uvnode_t *uvp, uint64_t, cred_t *);
int uvfs_up_delmap(uvnode_t *uvp, uint64_t, cred_t *);

extern uint32_t uvfs_max_write_size;
extern uint32_t uvfs_max_read_size;
extern uint32_t uvfs_max_readdir_size;
extern uint32_t uvfs_max_dthreads;

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_UVFS_UPCALL_H */
