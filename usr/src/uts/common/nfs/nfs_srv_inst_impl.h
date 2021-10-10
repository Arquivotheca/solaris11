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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */
#ifndef	_NFS_SRV_INST_IMPL_H
#define	_NFS_SRV_INST_IMPL_H

/*
 * nfs_srv_inst.h is always included first to provide
 * forward type declarations to includes below.
 */
#include <nfs/nfs_srv_inst.h>
#include <nfs/nfs4.h>
#include <nfs/nfs4_drc.h>
#include <nfs/export.h>
#include <sys/zone.h>
#include <sys/door.h>
#include <sys/kstat.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef _KERNEL

#define	RFS_INST_ROOTVP(rip)	((rip)->ri_rzone->rz_zonep->zone_rootvp)
#define	RFS_INST_ZONEP(rip)	((rip)->ri_rzone->rz_zonep)
#define	RFS_INST_ZONEID(rip)	((rip)->ri_rzone->rz_zonep->zone_id)

/*
 * RFS_UNIQUE_BUFLEN (KSTAT_STRLEN) includes NULL terminator, so it is
 *   actually the buf len for kstat names. (31: 30 char str + 1 NULL)
 *
 * RFS_ZONE_SUFFIX_MAXSTR: 1(_) + 4(zoneid)
 * RFS_INST_SUFFIX_MAXSTR: 1(_) + 4(zoneid) + 1(_) + 1(instid)
 * RFS_ZONE/INST_UNIQUE_BASELEN:
 *   max legth allowed for base_str in rfs_zone/inst_uniqstr()
 */
#define	RFS_ZONE_SUFFIX_MAXSTR (1 + ZONEID_WIDTH)
#define	RFS_INST_SUFFIX_MAXSTR (1 + ZONEID_WIDTH + 1 + 1)
#define	RFS_ZONE_UNIQUE_BASELEN (RFS_UNIQUE_BUFLEN - RFS_ZONE_SUFFIX_MAXSTR - 1)
#define	RFS_INST_UNIQUE_BASELEN (RFS_UNIQUE_BUFLEN - RFS_INST_SUFFIX_MAXSTR - 1)

typedef struct rfs2_inst {
	kmutex_t			r2_async_write_lock;
	struct rfs_async_write_list	*r2_async_write_head;
} rfs2_inst_t;

typedef struct rfs_namespc {
	krwlock_t	rn_export_rwlock;
	kmem_cache_t	*rn_authcache_hdl;
	exportinfo_t	*rn_exi_public;
	treenode_t	*rn_ns_root;
	exportinfo_t	**rn_exptable;
	exportinfo_t	**rn_exptable_path_hash;
	kmutex_t	rn_refreshq_lock;
	list_t		rn_refreshq_queue;
	kcondvar_t	rn_refreshq_cv;
	struct auth_cache *rn_refreshq_dead_entries;
	int		rn_refreshq_thread_state;
} rfs_namespc_t;


/*
 * r4_mc_verifier_tree:
 * An AVL tree, indexed by metacluster(mc) ids,  to track the NFS
 * service's clientid verifiers of mc nodes from which one or more
 * file systems migrate.  The verifiers are used specifically in
 * clientid to detect lost state due to service reboot.
 */
struct rfs4_inst {
	int		r4_enabled;
	krwlock_t	r4_enabled_rwlock;
	nvlist_t	*r4_dss_paths;
	nvlist_t	*r4_dss_oldpaths;
	rfs4_dss_path_t	*r4_dss_pathlist;
	char		**r4_dss_newpaths;
	uint_t		r4_dss_numnewpaths;
	rfs4_database_t	*r4_server_state;
	rfs4_table_t	*r4_openowner_tab;
	rfs4_index_t	*r4_openowner_idx;
	rfs4_table_t	*r4_lockowner_tab;
	rfs4_index_t	*r4_lockowner_idx;
	rfs4_index_t	*r4_lockowner_pid_idx;
	rfs4_table_t	*r4_client_tab;
	rfs4_index_t	*r4_clientid_idx;
	rfs4_index_t	*r4_nfsclnt_idx;
	rfs4_table_t	*r4_clntip_tab;
	rfs4_index_t	*r4_clntip_idx;
	callb_id_t	r4_cpr_id;
	krwlock_t	r4_findclient_lock;
	int		r4_seen_first_compound;
	time_t		r4_start_time;
	kmutex_t	r4_deleg_lock;
	int		r4_deleg_disabled;
	krwlock_t	r4_deleg_policy_lock;
	srv_deleg_policy_t r4_deleg_policy;
	rfs4_drc_t	*r4_drc;
	uint32_t	r4_drc_max;
	uint32_t	r4_drc_hash;
	kmutex_t	r4_grace_lock;
	rfs4_grace_t	*r4_cur_grace;
	bitmap4		r4_supported_attrs;
	avl_tree_t	r4_mc_verifier_tree;
	kmutex_t	r4_mc_verftree_lock;
};

/*
 * State of the rfs_inst_t.
 * The following transitions are allowed.
 *   COLD -> OFFLINE		startup
 *   COLD -> STOPPED		startup race detected or zone shutdown
 *   OFFLINE -> RUNNING		nfsd starts
 *   RUNNING -> OFFLINE		nfsd exits
 *   OFFLINE -> STOPPED		no more exports, zone shutdown
 */
typedef enum rfs_inst_state {
	RFS_INST_COLD = 1,	/* partially initialized */
	RFS_INST_OFFLINE,	/* no network connection, state is preserved */
	RFS_INST_RUNNING,	/* in use */
	RFS_INST_STOPPED	/* partially destroyed */
} rfs_inst_state_t;

typedef uint32_t rfs_instid_t;

struct rfs_inst {
	kmutex_t		ri_lock;
	int			ri_refcount;
	int			ri_unregistered;
	rfs_inst_state_t	ri_state;
	kcondvar_t		ri_state_cv;
	kthread_t		*ri_state_locked;
	int			ri_state_active;
	list_node_t		ri_gnode;
	list_node_t		ri_znode;
	rfs_zone_t		*ri_rzone;
	char			*ri_name;
	int			ri_name_len;
	nodeid_t		ri_hanfs_id;
	int			ri_quiesce;
	int			ri_export_cnt;
	rfs_namespc_t		ri_nm;
	rfs2_inst_t		ri_v2;
	rfs4_inst_t		ri_v4;
	rfs_instid_t		ri_id;
	fsh_bucket_t		ri_fsh_table[FSHTBLSZ];
	kmem_cache_t		*ri_fse_cache;
	char			ri_unique_suf[RFS_UNIQUE_BUFLEN];
};

struct nfslog_mem;

struct rfs_zone {
	kmutex_t		rz_lock;
	int			rz_refcount;
	int			rz_enabled;
	int			rz_shutting_down;
	zone_t			*rz_zonep;
	zone_ref_t		rz_zone_ref;
	char			*rz_name;
	int			rz_name_len;
	int			rz_inst_cnt;
	list_t			rz_instances;
	list_node_t		rz_gnode;
	kmutex_t		rz_mountd_lock;
	door_handle_t		rz_mountd_dh;
	krwlock_t		rz_nfslog_buffer_list_lock;
	struct log_buffer	*rz_nfslog_buffer_list;
	struct nfslog_mem	*rz_nfslog_mem_alloc;
	rfs_instid_t		rz_next_rfsinst_id;
	char			rz_unique_suf[RFS_UNIQUE_BUFLEN];
	struct nfs_stats	*rz_nfsstats;
};

typedef struct rfs_globals_v2 {
	u_longlong_t	rg2_caller_id;
} rfs_globals_v2_t;

typedef struct rfs_globals_v3 {
	writeverf3	rg3_write3verf;
	u_longlong_t	rg3_caller_id;
} rfs_globals_v3_t;

typedef struct rfs_globals_v4 {
	fem_t		*rg4_deleg_rdops;
	fem_t		*rg4_deleg_wrops;
	u_longlong_t	rg4_caller_id;
	uint_t		rg4_vkey;
	sysid_t		rg4_lockt_sysid;
	verifier4	rg4_write4verf;
	verifier4	rg4_readdir4verf;
} rfs_globals_v4_t;

typedef struct rfs_globals {
	kmutex_t		rg_lock;
	int			rg_inst_cnt;
	int			rg_zone_cnt;
	list_t			rg_instances;
	list_t			rg_zones;
	zone_key_t		rg_rfszone_key;
	uint32_t		rg_fse_uniq;
	rfs_globals_v2_t	rg_v2;
	rfs_globals_v3_t	rg_v3;
	rfs_globals_v4_t	rg_v4;
} rfs_globals_t;

extern rfs_globals_t rfs;

extern int (*nfs_set_nodeid)(nodeid_t);

void hanfsv4_failover(rfs_inst_t *);
void rfs_zone_shutdown(zoneid_t, void *);
#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif
#endif	/* _NFS_SRV_INST_IMPL_H */
