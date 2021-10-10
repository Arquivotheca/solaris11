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

#ifndef	_NFS_NFS4_MIG_H
#define	_NFS_NFS4_MIG_H

#include <nfs/nfs.h>
#include <nfs/nfs4.h>
#include <rpc/xdr.h>
#include <sys/flock.h>
#include <sys/avl.h>
#include <sys/list.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	TSM_HARVEST 	0x1
#define	TSM_HYDRATE 	0x2
#define	LAST_RECORD	1
#define	NOT_LAST_RECORD	0

/* Structure to encapsulate the state migration of any NFSv4 state table */
typedef struct state_mig {
	XDR 		sm_xdrs;
	vnode_t 	*sm_vp;
	vnode_t		*sm_rootvp;
	bool_t		sm_status;
	char 		*sm_tab_type;
	avl_tree_t 	sm_uniq_node_tree;
} state_mig_t;

/* TSM structure to migrate lockstate and record locks */
typedef struct state_mig_lock {
	state_mig_t *sml_lo_state_tab;
	locklist_t *sml_lo_list;
} state_mig_lock_t;

/*
 * A node in the AVL tree for eliminating duplicates from the client and
 * openowner XDR stream during harvest.
 */
typedef struct uniq_node {
	avl_node_t un_avl_node;
	void *un_key;
} uniq_node_t;

/*
 * mig_stateid_verifier is a file-system-specific stateid verifier that is
 * derived from the NFS service start time,  and which is embedded in the
 * stateids associated with a specific file system.
 */
typedef struct rfs4_mig_globals {
	verifier4	mig_write4verf;
	time_t		mig_stateid_verifier;
} rfs4_mig_globals_t;

/*
 * AVL tree of clientid verifiers, indexed by metacluster(mc) id. Each node in
 * the tree points to a linked list of clientid verifiers. In the event of
 * migrations of multiple filesystems from the same mc, with one or more
 * reboots of the mc in between, there will be more than one clientid erifier
 * for that mc.
 */
typedef struct {
	uint32_t	mcv_nodeid;
	list_t		mcv_list;
	avl_node_t	mcv_avl_node;
} mc_verifier_node_t;

typedef struct {
	time_t 		vl_verifier;
	list_node_t	vl_list_node;
} verifier_node_t;

extern void xdrvn_create(XDR *, vnode_t *, enum xdr_op);
extern void xdrvn_destroy(XDR *);
extern void rfs4_free_reply(nfs_resop4 *);
extern bool_t xdr_OPEN4res(XDR *, OPEN4res *);
extern bool_t xdr_LOCK4res(XDR *, LOCK4res *);
extern nfsstat4 rfs4_client_sysid(rfs4_client_t *, sysid_t *);
extern void rfs4_ss_clid(rfs4_client_t *);
extern void rfs4_state_rele_nounlock(rfs4_state_t *);
extern int rfs4_setlock(vnode_t *, struct flock64 *, int, cred_t *);

extern migerr_t rfs4_tsm(void *rp, vnode_t *, int);
extern int rfs4_do_client_hydrate(rfs_inst_t *, rfs4_client_t *);
extern int rfs4_do_openowner_hydrate(rfs_inst_t *, rfs4_openowner_t *);
extern int rfs4_do_state_hydrate(rfs_inst_t *, rfs4_state_t *, vnode_t *);
extern int rfs4_do_deleg_state_hydrate(rfs_inst_t *, rfs4_deleg_state_t *,
    vnode_t *);
extern int rfs4_do_lo_state_hydrate(rfs_inst_t *, rfs4_lo_state_t *,
    state_mig_lock_t *);
extern int rfs4_do_globals_hydrate(fsh_entry_t *, rfs4_mig_globals_t *);
extern void mc_insert_verf(rfs4_inst_t *, uint32_t, time_t,
    mc_verifier_node_t *);
extern bool_t mc_verf_exists(rfs4_inst_t *, uint32_t, time_t,
    mc_verifier_node_t **);

#ifdef	__cplusplus
}
#endif

#endif /* _NFS_NFS4_MIG_H */
