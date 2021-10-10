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

#ifndef	_NFS_NFS4_FSH_H
#define	_NFS_NFS4_FSH_H

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef	_KERNEL

#include <sys/types.h>
#include <sys/vnode.h>
#include <sys/fem.h>
#include <rpc/rpc.h>
#include <nfs/nfs.h>
#include <nfs/nfssys.h>
#include <nfs/nfs4_kprot.h>
#include <sys/list.h>
#include <sys/refstr.h>

#define	FSE_NM_SZ	15

/*
 * What state is the filesystem in wrt migration?
 */
#define	FSE_AVAILABLE	0x01
#define	FSE_FROZEN	0x02
#define	FSE_MOVED	0x04
#define	FSE_TSM		0x08

/* file system hash stuff */
typedef struct fsh_entry {
	list_node_t		fse_node;
	struct fsh_bucket	*fse_fsb;
	fsid_t			fse_fsid;
	refstr_t		*fse_mntpt;
	verifier4		fse_wr4verf;
	char			fse_name[FSE_NM_SZ];
	kmutex_t		fse_lock;

	struct rfs4_database	*fse_state_store;

	/* freeze state and lock */
	uint32_t		fse_refcnt;
	uint32_t		fse_state;
	bool_t			fse_loaded_from_freeze;
	nfs_rwlock_t		fse_freeze_lock;

	/* grace period manipulation */
	time_t			fse_start_time;
	time_t			fse_grace_period;

	/* open state table and indices */
	struct rfs4_table	*fse_state_tab;
	struct rfs4_index	*fse_state_idx;
	struct rfs4_index	*fse_state_owner_file_idx;
	struct rfs4_index	*fse_state_file_idx;

	/* lock state table and indices */
	struct rfs4_table	*fse_lo_state_tab;
	struct rfs4_index	*fse_lo_state_idx;
	struct rfs4_index	*fse_lo_state_owner_idx;

	/* delegation state table and indices */
	struct rfs4_table	*fse_deleg_state_tab;
	struct rfs4_index	*fse_deleg_idx;
	struct rfs4_index	*fse_deleg_state_idx;

	/* File table and indices */
	struct rfs4_table 	*fse_file_tab;
	struct rfs4_index 	*fse_file_idx;

	/* stateid verifier */
	time_t			fse_stateid_verifier;


} fsh_entry_t;

typedef struct fsh_bucket {
	krwlock_t	fsb_lock;
	list_t		fsb_entries;
	int		fsb_chainlen;
} fsh_bucket_t;

/* Table size needs to be a power of 2, or fix the hash function */
#define	FSHTBLSZ	64

/*
 * rfs4_do_tsm variable allows an NFSv4 client to test against a server
 * capable of transparent state migration (TSM) as well as a server that does
 * not support TSM.  The variable is used to turn TSM on or off and is turned
 * on (set to 1) by default.
 */
extern int rfs4_do_tsm;

/*
 * Functions to handle freezing the filesystem
 */
extern void	fs_hash_init(rfs_inst_t *);
extern void	fs_hash_destroy(rfs_inst_t *);
extern migerr_t	rfs4_fse_convert_fsid(vnode_t *, uint32_t);
extern migerr_t	rfs4_fse_freeze_fsid(vnode_t *);
extern migerr_t	rfs4_fse_grace_fsid(vnode_t *);
extern migerr_t	rfs4_fse_reset_fsid(vnode_t *);
extern migerr_t	rfs4_fse_thaw_fsid(vnode_t *);

extern int	rfs4_fse_is_frozen(fsh_entry_t *);
extern void	rfs4_fse_release_reader(fsh_entry_t *);

extern fsh_entry_t *fsh_get_ent(rfs_inst_t *, fsid_t);
extern void	fsh_add(rfs_inst_t *, fsid_t, bool_t);
extern void	fsh_db_init(rfs_inst_t *);
extern void	fsh_db_cleanup(rfs_inst_t *);

extern void	fsh_ent_rele(rfs_inst_t *, fsh_entry_t *);
extern void	fsh_fsid_rele(rfs_inst_t *, fsid_t);

/*
 * Determine the status of the filesystem wrt migration
 */

/* Can be called in nfs and klm modules */
extern uint32_t nfs_get_fsh_status(vnode_t *);

/* Can be called in nfssrv module */
extern uint32_t rfs4_get_fsh_status(vnode_t *);

/* Linkage to allow nfs to get the state from nfssrv */
extern uint32_t (*nfs_get_fsh_status_func)(vnode_t *);

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _NFS_NFS4_FSH_H */
