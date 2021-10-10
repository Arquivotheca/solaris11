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
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_NFS_MDB_H
#define	_NFS_MDB_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/mdb_modapi.h>
#include <sys/sysinfo.h>
#include <nfs/nfs_srv_inst.h>
#include <nfs/nfs.h>
#include <nfs/nfs_clnt.h>
#include <nfs/rnode.h>
#include <sys/vnode.h>
#include <sys/vfs.h>
#include <sys/refstr_impl.h>
#include <sys/kstat.h>
#include <netinet/in.h>

int rfs_inst_init(void);
rfs_inst_t *get_rfs_inst(void);
uintptr_t get_rfs_inst_addr(void);
int set_rfs_inst(uintptr_t);
int nfs_print_netbuf(struct netbuf *);
void nfs_print_netbuf_buf(void *, int);
int nfs_print_netconfig(struct knetconfig *);
int nfs_print_hex(char *, int);
int nfs_read_print_hex(uintptr_t, int);
void nfs_mutex_print(const kmutex_t *);
void nfs_rwlock_print(const krwlock_t *);
void nfs_bprint(uint_t, uchar_t *);
int getoffset(char *, char *, ulong_t *);

extern void nfs4_os_help(void);
extern int nlm_sysid_walk_init(mdb_walk_state_t *);
extern int nlm_sysid_walk_step(mdb_walk_state_t *);
extern void pr_vfs_mntpnts(vfs_t *);

#define	NAMMAX	64

#define	NFS_MDB_OPT_VERBOSE		0x0001
#define	NFS_MDB_OPT_WALK_IT		0x0002
#define	NFS_MDB_OPT_DBE_ADDR		0x0004
#define	NFS_MDB_OPT_SOLARIS_SRV		0x0008
#define	NFS_MDB_OPT_SOLARIS_CLNT	0x0010
#define	NFS_MDB_OPT_SHWMP		0x0020
#define	NFS_MDB_OPT_SHWMMSG		0x0040
#define	NFS_MDB_OPT_SUMMARY		0x0080
#define	NFS_MDB_OPT_FS_SET		0x0100
#define	NFS_MDB_OPT_INST_SET		0x0200

extern int nfs4_mdb_opt;

#define	NFS_OBJ_FETCH(obj_addr, obj_type, dest, err) \
	if (mdb_vread(dest, sizeof (obj_type), obj_addr) \
					!= sizeof (obj_type)) { \
		mdb_warn("error reading "#obj_type" at %p", obj_addr); \
		return (err); \
	}

#define	GETOFFSET(type, member, member_off) {                   \
	if (getoffset(#type, #member, member_off) != DCMD_OK)   \
		*member_off = offsetof(type, member);           \
}

extern uintptr_t find_globals(uintptr_t, const char *, uint_t);
extern uintptr_t find_globals_bykey(uintptr_t, zone_key_t, uint_t);

#ifdef	__cplusplus
}
#endif

#endif /* _NFS_MDB_H */
