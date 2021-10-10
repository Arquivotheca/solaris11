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
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <mdb/mdb_modapi.h>
#include <mdb/mdb_ctf.h>
#include <sys/zone.h>
#include <nfs/nfs_srv_inst.h>
#include <nfs/nfs.h>
#include <rpc/svc.h>
#include <nfs/rnode.h>
#include <nfs/nfs_clnt.h>

#include "nfs_mdb.h"

int nfs4_diag(uintptr_t, uint_t, int, const mdb_arg_t *);
int nfs4_mimsg(uintptr_t, uint_t, int, const mdb_arg_t *);
void nfs4_diag_help(void);

int nfs_vfs(uintptr_t, uint_t, int, const mdb_arg_t *);

int nfs_mntinfo(uintptr_t, uint_t, int, const mdb_arg_t *);
void nfs_mntinfo_help(void);

int nfs4_mntinfo(uintptr_t, uint_t, int, const mdb_arg_t *);
void nfs4_mntinfo_help(void);

int nfs_servinfo(uintptr_t, uint_t, int, const mdb_arg_t *);
void nfs_servinfo_help(void);

int nfs4_servinfo(uintptr_t, uint_t, int, const mdb_arg_t *);
void nfs4_servinfo_help(void);

int nfs4_server_info(uintptr_t, uint_t, int, const mdb_arg_t *);
void nfs4_server_info_help(void);

int nfs_stat(uintptr_t, uint_t, int, const mdb_arg_t *);

void nfs_stat_help(void);

int nlm_sysid(uintptr_t addr, uint_t, int, const mdb_arg_t *);
int nlm_vnode(uintptr_t addr, uint_t, int, const mdb_arg_t *);
int nlm_lockson(uintptr_t addr, uint_t, int, const mdb_arg_t *);
void nlm_sysid_help(void);
void nlm_vnode_help(void);
void nlm_lockson_help(void);

int nfs4_idmap(uintptr_t, uint_t, int, const mdb_arg_t *);
int nfs4_idmap_info(uintptr_t, uint_t, int, const mdb_arg_t *);
void nfs4_idmap_info_help(void);

int rfs4_server_state(uintptr_t, uint_t, int, const mdb_arg_t *);
void rfs4_server_state_help(void);

int rfs4_client(uintptr_t, uint_t, int, const mdb_arg_t *);
int rfs4_openowner(uintptr_t, uint_t, int, const mdb_arg_t *);
int rfs4_state(uintptr_t, uint_t, int, const mdb_arg_t *);
int rfs4_lo_state(uintptr_t, uint_t, int, const mdb_arg_t *);
int rfs4_lockowner(uintptr_t, uint_t, int, const mdb_arg_t *);
int rfs4_file(uintptr_t, uint_t, int, const mdb_arg_t *);
int rfs4_deleg_state(uintptr_t, uint_t, int, const mdb_arg_t *);
int rfs4_dinfo(uintptr_t, uint_t, int, const mdb_arg_t *);
int rfs4_cbinfo(uintptr_t, uint_t, int, const mdb_arg_t *);

void rfs4_client_help(void);
void rfs4_openowner_help(void);
void rfs4_state_help(void);
void rfs4_lo_state_help(void);
void rfs4_lockowner_help(void);
void rfs4_file_help(void);
void rfs4_deleg_state_help(void);
void rfs4_dinfo_help(void);
void rfs4_cbinfo_help(void);

int rfs4_clientid4(uintptr_t, uint_t, int, const mdb_arg_t *);
int rfs4_changeid4(uintptr_t, uint_t, int, const mdb_arg_t *);
int rfs4_verifier4(uintptr_t, uint_t, int, const mdb_arg_t *);
int rfs4_stateid4(uintptr_t, uint_t, int, const mdb_arg_t *);
int rfs4_open_owner4(uintptr_t, uint_t, int, const mdb_arg_t *);
int rfs4_lock_owner4(uintptr_t, uint_t, int, const mdb_arg_t *);
int rfs4_cb_client4(uintptr_t, uint_t, int, const mdb_arg_t *);

void rfs4_clientid4_help(void);
void rfs4_changeid4_help(void);
void rfs4_verifier4_help(void);
void rfs4_stateid4_help(void);
void rfs4_open_owner4_help(void);
void rfs4_lock_owner4_help(void);
void rfs4_cb_client4_help(void);

int nfs4_exp_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
int nfs4_exptbl_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
int nfs4_exptbl_path_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
int nfs4_nstree_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
int nfs4_expvis_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
void nfs4_exp_dcmd_help(void);
void nfs4_exptbl_dcmd_help(void);
void nfs4_nstree_dcmd_help(void);
void nfs4_expvis_dcmd_help(void);

int rnode4_dump_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
int rnode4_find_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
void rnode4_dump_dcmd_help(void);
void rnode4_find_dcmd_help(void);

int rnode4_info(uintptr_t, uint_t, int, const mdb_arg_t *);
int nfs4_svnode_info(uintptr_t, uint_t, int, const mdb_arg_t *);
int nfs4_fname(uintptr_t, uint_t, int, const mdb_arg_t *);

void rnode4_info_help(void);
void nfs4_svnode_info_help(void);
void nfs4_fname_help(void);

int nfs_serv_walk_init(mdb_walk_state_t *);
int nfs_serv_walk_step(mdb_walk_state_t *);
void nfs_serv_walk_fini(mdb_walk_state_t *);
int nfs4_serv_walk_init(mdb_walk_state_t *);
int nfs4_serv_walk_step(mdb_walk_state_t *);
void nfs4_serv_walk_fini(mdb_walk_state_t *);

int nfs_async_walk_init(mdb_walk_state_t *);
int nfs_async_walk_step(mdb_walk_state_t *);
void nfs_async_walk_fini(mdb_walk_state_t *);
int nfs4_async_walk_step(mdb_walk_state_t *);

int nfs_vfs_walk_init(mdb_walk_state_t *);
int nfs_vfs_walk_step(mdb_walk_state_t *);
void nfs_vfs_walk_fini(mdb_walk_state_t *);

int nfs4_server_walk_init(mdb_walk_state_t *);
int nfs4_server_walk_step(mdb_walk_state_t *);
void nfs4_server_walk_fini(mdb_walk_state_t *);

int mxprt_walk_init(mdb_walk_state_t *);
int mxprt_walk_step(mdb_walk_state_t *);
void mxprt_walk_fini(mdb_walk_state_t *);

int nlm_vnode_walk_init(mdb_walk_state_t *);
int nlm_vnode_walk_step(mdb_walk_state_t *);
void nlm_vnode_walk_fini(mdb_walk_state_t *);

int ld_walk_init(mdb_walk_state_t *);
int ld_walk_step(mdb_walk_state_t *);
void ld_walk_fini(mdb_walk_state_t *);

int lg_walk_init(mdb_walk_state_t *);
int lg_walk_step(mdb_walk_state_t *);

int ac_rnode_walk_init(mdb_walk_state_t *);
int ac_rnode_walk_step(mdb_walk_state_t *);
void ac_rnode_walk_fini(mdb_walk_state_t *);

int rtbl_walk_init(mdb_walk_state_t *);
int rtbl_walk_step(mdb_walk_state_t *);
void rtbl_walk_fini(mdb_walk_state_t *);

int acache_walk_init(mdb_walk_state_t *);
int acache_walk_step(mdb_walk_state_t *);
void acache_walk_fini(mdb_walk_state_t *);

int ac4_rnode_walk_init(mdb_walk_state_t *);
int ac4_rnode_walk_step(mdb_walk_state_t *);
void ac4_rnode_walk_fini(mdb_walk_state_t *);

int rtbl4_walk_init(mdb_walk_state_t *);
int rtbl4_walk_step(mdb_walk_state_t *);
void rtbl4_walk_fini(mdb_walk_state_t *);

int acache4_walk_init(mdb_walk_state_t *);
int acache4_walk_step(mdb_walk_state_t *);
void acache4_walk_fini(mdb_walk_state_t *);

int u2s_walk_init(mdb_walk_state_t *);
int g2s_walk_init(mdb_walk_state_t *);
int s2u_walk_init(mdb_walk_state_t *);
int s2g_walk_init(mdb_walk_state_t *);
int idmap_generic_step(mdb_walk_state_t *);
void idmap_generic_fini(mdb_walk_state_t *);

int rfs4_dbe_walk_init(mdb_walk_state_t *);
int rfs4_dbe_walk_step(mdb_walk_state_t *);
void rfs4_dbe_walk_fini(mdb_walk_state_t *);

int rfs4_idx_walk_init(mdb_walk_state_t *);
int rfs4_idx_walk_step(mdb_walk_state_t *);
void rfs4_idx_walk_fini(mdb_walk_state_t *);

int rfs4_tbl_walk_init(mdb_walk_state_t *);
int rfs4_tbl_walk_step(mdb_walk_state_t *);
void rfs4_tbl_walk_fini(mdb_walk_state_t *);

int list_walk_init(mdb_walk_state_t *);
int list_walk_step(mdb_walk_state_t *);
void list_walk_fini(mdb_walk_state_t *);

int nfs4_svnode_walk_init(mdb_walk_state_t *);
int nfs4_svnode_walk_step(mdb_walk_state_t *);
void nfs4_svnode_walk_fini(mdb_walk_state_t *);

int deleg_rnode4_walk_init(mdb_walk_state_t *);
int deleg_rnode4_walk_step(mdb_walk_state_t *);
void deleg_rnode4_walk_fini(mdb_walk_state_t *);

int nfs4_mnt_walk_init(mdb_walk_state_t *);
int nfs4_mnt_walk_step(mdb_walk_state_t *);

int nfs_mnt_walk_init(mdb_walk_state_t *);
int nfs_mnt_walk_step(mdb_walk_state_t *);

int nfs4_foo_walk_init(mdb_walk_state_t *);
int nfs4_foo_walk_step(mdb_walk_state_t *);

int exi_fid_walk_init(mdb_walk_state_t *);
int exi_path_walk_init(mdb_walk_state_t *);
int exi_walk_step(mdb_walk_state_t *);
void exi_walk_fini(mdb_walk_state_t *);

int vis_walk_init(mdb_walk_state_t *);
int vis_walk_step(mdb_walk_state_t *);
void vis_walk_fini(mdb_walk_state_t *);

void nfs_null_walk_fini(mdb_walk_state_t *);

int  nfs4_oo_bkt_walk_init(mdb_walk_state_t *);
int  nfs4_oo_bkt_walk_step(mdb_walk_state_t *);
void nfs4_oo_bkt_walk_fini(mdb_walk_state_t *);

int  nfs4_oob_walk_init(mdb_walk_state_t *);
int  nfs4_oob_walk_step(mdb_walk_state_t *);

int rfs4_fsh_bkt_val_walk_init(mdb_walk_state_t *);
int rfs4_fsh_bkt_val_walk_step(mdb_walk_state_t *);
void rfs4_fsh_bkt_val_walk_fini(mdb_walk_state_t *);

int rfs4_fsh_bkt_addr_walk_init(mdb_walk_state_t *);
int rfs4_fsh_bkt_addr_walk_step(mdb_walk_state_t *);
void rfs4_fsh_bkt_addr_walk_fini(mdb_walk_state_t *);

int rfs4_fsh_ent_walk_init(mdb_walk_state_t *);
int rfs4_fsh_ent_walk_step(mdb_walk_state_t *);

int rfs4_fsh_stats_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);

int rfs4_fsh_bkt_sum_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
int rfs4_fsh_ent_sum_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);

int rfs4_db_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
int rfs4_db_tbl_walk_init(mdb_walk_state_t *w);
int rfs4_db_tbl_walk_step(mdb_walk_state_t *w);

int rfs4_tbl_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);

int rfs4_idx_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
int rfs4_db_idx_walk_init(mdb_walk_state_t *w);
int rfs4_db_idx_walk_step(mdb_walk_state_t *w);

int rfs4_bkt_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
int rfs4_db_bkt_walk_init(mdb_walk_state_t *w);
int rfs4_db_bkt_walk_step(mdb_walk_state_t *w);
void rfs4_db_bkt_walk_fini(mdb_walk_state_t *w);

int rfs_inst_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
int rfs_inst_walk_init(mdb_walk_state_t *w);
int rfs_inst_walk_step(mdb_walk_state_t *w);
void rfs_inst_walk_fini(mdb_walk_state_t *w);

int rfs_zone_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
int rfs_zone_walk_init(mdb_walk_state_t *w);
int rfs_zone_walk_step(mdb_walk_state_t *w);
void rfs_zone_walk_fini(mdb_walk_state_t *w);

int rfs4_clnt_kc_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
int rfs4_delegState_kc_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
int rfs4_file_kc_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
int rfs4_loSid_kc_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
int rfs4_lo_kc_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
int rfs4_oo_kc_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
int rfs4_osid_kc_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);

int nfs4_foo_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
int nfs4_oob_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
int nfs4_os_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
int nfs_help(uintptr_t, uint_t, int, const mdb_arg_t *);
int nfs_fid_hashdist(uintptr_t, uint_t, int, const mdb_arg_t *);
int nfs_path_hashdist(uintptr_t, uint_t, int, const mdb_arg_t *);
int nfs_setopt_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
int nfs_getopt_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);

static const mdb_dcmd_t dcmds[] = {
	{ "nfs_help", "", "Show nfs commands",
		nfs_help, NULL },
	/* NFS */
	{ "nfs_fid_hashdist", "[-v]", "Show fid hash distribution"
	" (-v displays individual bucket lengths).", nfs_fid_hashdist, NULL},
	{ "nfs_path_hashdist", "[-v]", "Show path hash distribution"
	" (-v displays individual bucket lengths).", nfs_path_hashdist, NULL},
	{ "nfs_vfs", "[-v]", "print all nfs vfs struct (-v for mntinfo) ",
		nfs_vfs, NULL },
	{ "nfs_mntinfo", "[-v]", "print mntinfo_t information",
		nfs_mntinfo, nfs_mntinfo_help },
	{ "nfs4_mntinfo", "[-mv]", "print mntinfo4_t information",
		nfs4_mntinfo, nfs4_mntinfo_help },
	{ "nfs_servinfo", ":[-v]", "print servinfo_t information",
		nfs_servinfo, nfs_servinfo_help },
	{ "nfs4_servinfo", ":[-v]", "print servinfo4_t information",
		nfs4_servinfo, nfs4_servinfo_help },

	/* NLM */
	{ "nlm_sysid", "[-v]", "print lm_sysid information",
		nlm_sysid, nlm_sysid_help },
	{ "nlm_vnode", "[-v]", "print lm_vnode information",
		nlm_vnode, nlm_vnode_help },
	{ "nlm_lockson", "[-v] [ $[sysid] | server ]",
		"dump locks held on host",
		nlm_lockson, nlm_lockson_help },

	/* statistics */
	{ "nfs_stat", "[-csb][-234][-anr] | $[count]", "print NFS statistics",
		nfs_stat, nfs_stat_help },

	/* NFSv4 */
	{ "nfs4_foo", "[-v]", "Dump NFSv4 freed open owners",
		nfs4_foo_dcmd, NULL},
	{ "nfs4_oob", "[-v]", "Dump NFSv4 open owner buckets",
		nfs4_oob_dcmd, NULL},
	{ "nfs4_os", "[-v]", "Dump NFSv4 open streams",
		nfs4_os_dcmd, nfs4_os_help},
	{ "nfs4_idmap", ":", "dump nfsidmap_t",
		nfs4_idmap, NULL },
	{ "nfs4_idmap_info", ":", "print nfs id-mapping information",
		nfs4_idmap_info, nfs4_idmap_info_help },
	{ "nfs4_server_info", NULL, "print nfs4_server_t information",
		nfs4_server_info, nfs4_server_info_help },
	{ "nfs4_diag", "[-s]",
		"print queued recovery messages for NFSv4 client",
		nfs4_diag, nfs4_diag_help },
	{ "nfs4_mimsg", "[-s]",
		"print queued messages, expects address of mi_msg_head",
		nfs4_mimsg, NULL },

	/* NFSv4 Server File System Hash Table */
	{ "rfs4_fsh_stats", "?",
		"dump stats for the NFSv4 server file system hash table",
		rfs4_fsh_stats_dcmd, NULL },
	{ "rfs4_fsh_ent_sum", "?",
	    "dump summary for the NFSv4 server file system entry",
	    rfs4_fsh_ent_sum_dcmd, NULL },
	{ "rfs4_fsh_bkt_sum", "?",
	    "dump summary for all of the NFSv4 server file system entries",
	    rfs4_fsh_bkt_sum_dcmd, NULL },

	/* NFSv4 Namespace */
	{ "nfs_expinfo", "?", "dump exportinfo struct",
		nfs4_exp_dcmd, nfs4_exp_dcmd_help },
	{ "nfs_exptable", "?", "dump exportinfo table",
		nfs4_exptbl_dcmd, nfs4_exptbl_dcmd_help },
	{ "nfs_exptable_path", "?", "dump exportinfo path table",
		nfs4_exptbl_path_dcmd, nfs4_exptbl_dcmd_help },
	{ "nfs_nstree", "[-v]", "dump nfs pseudo namespace tree",
		nfs4_nstree_dcmd, nfs4_nstree_dcmd_help},
	{ "nfs_expvis", "?", "dump exportinfo visible list",
		nfs4_expvis_dcmd, nfs4_expvis_dcmd_help },

	/* rnode4 */
	{ "nfs_rnode4", "?", "dump NFSv4 rnodes ",
		rnode4_dump_dcmd, rnode4_dump_dcmd_help },
	{ "nfs_rnode4find", "?", "find NFSv4 rnodes for given vfsp ",
		rnode4_find_dcmd, rnode4_find_dcmd_help },

	{ "nfs4_fname", NULL, "print path name of nfs4_fname_t specified",
		nfs4_fname, nfs4_fname_help },
	{ "nfs4_svnode", NULL, "print svnode_t info at specified address",
		nfs4_svnode_info, nfs4_svnode_info_help },

	{ "rfs4_db", "?",	"dump NFSv4 server state",
		rfs4_db_dcmd,	NULL	},
	{ "rfs4_tbl", "?",	"dump NFSv4 server table",
		rfs4_tbl_dcmd,	NULL	},
	{ "rfs4_idx", "?",	"dump NFSv4 server index",
		rfs4_idx_dcmd,	NULL	},
	{ "rfs4_bkt", "?",	"dump NFSv4 server index buckets",
		rfs4_bkt_dcmd,	NULL	},
	{ "rfs4_client", "-c <clientid>", "dump NFSv4 rfs4_client_t structures",
		rfs4_clnt_kc_dcmd,	NULL	},
	{ "rfs4_deleg", "?", "dump NFSv4 rfs4_deleg_state_t structures",
		rfs4_delegState_kc_dcmd,	NULL	},
	{ "rfs4_file", "?", "dump NFSv4 rfs4_file_t structures",
		rfs4_file_kc_dcmd,	NULL	},
	{ "rfs4_lsid", "?", "dump NFSv4 rfs4_lo_state_t structures",
		rfs4_loSid_kc_dcmd,	NULL	},
	{ "rfs4_lo", "?", "dump NFSv4 rfs4_lockowner_t structures",
		rfs4_lo_kc_dcmd,	NULL	},
	{ "rfs4_oo", "?", "dump NFSv4 rfs4_OpenOwner_t structures",
		rfs4_oo_kc_dcmd,	NULL	},
	{ "rfs4_osid", "?", "dump NFSv4 rfs4_state_t structures",
		rfs4_osid_kc_dcmd,	NULL	},
	{ "nfs_set", "-[v|w|s|c|f|i]", "set options for NFS MDB",
		nfs_setopt_dcmd,	NULL	},
	{ "nfs_get", "?", "get options for NFS MDB",
		nfs_getopt_dcmd,	NULL	},

	/* NFS Server Instances */
	{ "rfs_inst", "?",	"dump NFS Server Instance rfs_inst_t",
		rfs_inst_dcmd,	NULL	},
	{ "rfs_zone", "?",	"dump NFS Server Instance rfs_zone_t",
		rfs_zone_dcmd,	NULL	},

	{NULL}
};

static const mdb_walker_t walkers[] = {
	/* NFS */
	{ "nfs_serv", "walk linkedlist of servinfo structs",
		nfs_serv_walk_init, nfs_serv_walk_step, nfs_null_walk_fini },
	{ "nfs4_serv", "walk linkedlist of servinfo4 structs",
		nfs4_serv_walk_init, nfs4_serv_walk_step, nfs_null_walk_fini },
	{ "nfs_async", "walk list of async requests",
		nfs_async_walk_init, nfs_async_walk_step, nfs_null_walk_fini },
	{ "nfs4_async", "walk list of NFSv4 async requests",
		nfs_async_walk_init, nfs4_async_walk_step, nfs_null_walk_fini },
	{ "nfs_vfs", "walk all NFS-mounted vfs structs",
		nfs_vfs_walk_init, nfs_vfs_walk_step, nfs_null_walk_fini },
	{ "nfs4_mnt", "walk NFSv4-mounted vfs structs, pass mntinfo4",
		nfs4_mnt_walk_init, nfs4_mnt_walk_step, nfs_null_walk_fini },
	{ "nfs_mnt", "walk NFS-mounted vfs structs, pass mntinfo",
		nfs_mnt_walk_init, nfs_mnt_walk_step, nfs_null_walk_fini },
	{ "nfs4_server", "walk nfs4_server_t structs",
		nfs4_server_walk_init, nfs4_server_walk_step,
		nfs4_server_walk_fini },

	/* NLM */
	{ "nlm_sysid", "lm_sysid Walker",
		nlm_sysid_walk_init, nlm_sysid_walk_step, NULL },
	{ "nlm_vnode", "lm_vnode Walker",
		nlm_vnode_walk_init, nlm_vnode_walk_step, nlm_vnode_walk_fini },

	/* Caches */
	{ "nfs_acache", "walk entire nfs_access_cache",
		acache_walk_init, acache_walk_step, acache_walk_fini },
	{ "nfs_acache_rnode", "walk over acache entries for a specified rnode",
		ac_rnode_walk_init, ac_rnode_walk_step, ac_rnode_walk_fini },
	{ "nfs_rtable", "walk rnodes in rtable cache",
		rtbl_walk_init, rtbl_walk_step, rtbl_walk_fini },

	/* NFSv4 */
	{ "nfs_acache4", "walk entire nfs4_access_cache",
		acache4_walk_init, acache4_walk_step, acache4_walk_fini },
	{ "nfs_acache4_rnode", "walk over acache4 entries for a given rnode",
		ac4_rnode_walk_init, ac4_rnode_walk_step, ac4_rnode_walk_fini },
	{ "nfs_rtable4", "walk rnode4s in rtable4 cache",
		rtbl4_walk_init, rtbl4_walk_step, rtbl4_walk_fini },
	/* idmap caches */
	{ "nfs4_u2s", "walk uid-to-string idmap cache",
		u2s_walk_init, idmap_generic_step, idmap_generic_fini },
	{ "nfs4_g2s", "walk gid-to-string idmap cache",
		g2s_walk_init, idmap_generic_step, idmap_generic_fini },
	{ "nfs4_s2u", "walk string-to-uid idmap cache",
		s2u_walk_init, idmap_generic_step, idmap_generic_fini },
	{ "nfs4_s2g", "walk string-to-gid idmap cache",
		s2g_walk_init, idmap_generic_step, idmap_generic_fini },

	/* Export Info */
	{ "nfs_expinfo", "walk exportinfo table",
		exi_fid_walk_init, exi_walk_step, exi_walk_fini },
	{ "nfs_expinfo_path", "walk exportinfo path hash table",
		exi_path_walk_init, exi_walk_step, exi_walk_fini },
	{ "nfs_expvis", "walk exportinfo visible list",
		vis_walk_init, vis_walk_step, vis_walk_fini },

	/* Client */
	{ "nfs_deleg_rnode4", "walk deleg_list of an nfs4_server_t",
		deleg_rnode4_walk_init, deleg_rnode4_walk_step,
		deleg_rnode4_walk_fini },
	{ "nfs4_svnode", "walk svnode list at given svnode address",
		nfs4_svnode_walk_init, nfs4_svnode_walk_step,
		nfs4_svnode_walk_fini },
	{ "rfs4_db_tbl", "walk NFSv4 Server rfs4_table_t",
		rfs4_db_tbl_walk_init, rfs4_db_tbl_walk_step, NULL, NULL},

	{ "rfs4_db_idx", "walk NFSv4 Server rfs4_index_t",
		rfs4_db_idx_walk_init, rfs4_db_idx_walk_step, NULL, NULL},

	{ "rfs4_db_bkt", "walk NFSv4 Server rfs4_bucket_t",
		rfs4_db_bkt_walk_init, rfs4_db_bkt_walk_step,
		rfs4_db_bkt_walk_fini, NULL},

	/* NFSv4 Server File System Hash Table */
	{ "rfs4_fsh_bkt_val", "walk NFSv4 Server fsh_bucket_t",
		rfs4_fsh_bkt_val_walk_init, rfs4_fsh_bkt_val_walk_step,
		rfs4_fsh_bkt_val_walk_fini, NULL },

	/* Server Instance */
	{ "rfs_inst", "walk NFS Server Instance rfs_inst_t",
		rfs_inst_walk_init, rfs_inst_walk_step,
		rfs_inst_walk_fini, NULL },

	{ "rfs4_fsh_bkt", "walk NFSv4 Server fs_bucket_t (print addrs)",
		rfs4_fsh_bkt_addr_walk_init, rfs4_fsh_bkt_addr_walk_step,
		rfs4_fsh_bkt_addr_walk_fini, NULL },

	{ "rfs_zone", "walk NFS Server Instance rfs_zone_t",
		rfs_zone_walk_init, rfs_zone_walk_step,
		rfs_zone_walk_fini, NULL },

	{ "rfs4_fsh_ent", "walk NFSv4 fsh_entry_t",
		rfs4_fsh_ent_walk_init, rfs4_fsh_ent_walk_step,
		NULL, NULL },

	{NULL}
};

uintptr_t vfs_op2, vfs_op3, vfs_op4;

static const mdb_modinfo_t modinfo = { MDB_API_VERSION, dcmds, walkers };

const mdb_modinfo_t *
_mdb_init(void)
{
	if (mdb_readvar(&vfs_op2, "nfs_vfsops") == -1) {
		mdb_warn("failed to find 'nfs_vfsops'\n");
	}

	if (mdb_readvar(&vfs_op3, "nfs3_vfsops") == -1) {
		mdb_warn("failed to find 'nfs3_vfsops'\n");
	}

	if (mdb_readvar(&vfs_op4, "nfs4_vfsops") == -1) {
		mdb_warn("failed to find 'nfs4_vfsops'\n");
	}

	rfs_inst_init();
	return (&modinfo);
}

#define	NFS_HELP_W  0x01
#define	NFS_HELP_D  0x02

/*ARGSUSED*/
int
nfs_help(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	int opts = 0;
	const mdb_dcmd_t *cmd;
	const mdb_walker_t *walk;

	if (flags && DCMD_ADDRSPEC)
		return (DCMD_USAGE);

	if (mdb_getopts(argc, argv,
	    'w', MDB_OPT_SETBITS, NFS_HELP_W, &opts,
	    'd', MDB_OPT_SETBITS, NFS_HELP_D, &opts,
	    NULL) != argc) {
		return (DCMD_USAGE);
	}

	if (opts == 0) {
		mdb_printf("::nfs_help -w -d\n"
		    "\t -w\t Will show nfs specific walkers\n"
		    "\t -d\t Will show nfs specific dcmds\n");
		return (DCMD_OK);
	}

	if (opts & NFS_HELP_D) {
		cmd = dcmds;

		while (cmd->dc_name) {
			mdb_printf("%-20s\t%s\n", cmd->dc_name, cmd->dc_descr);
			cmd++;
		}
	}

	if (opts & NFS_HELP_W) {
		walk = walkers;

		while (walk->walk_name) {
			mdb_printf("%-20s\t%s\n", walk->walk_name,
			    walk->walk_descr);
			walk++;
		}
	}
	return (DCMD_OK);
}

/*
 * Convenience routine for looking up the globals given a zone pointer and
 * name of zsd_key.
 */
struct zsd_lookup {
	zone_key_t	key;
	uintptr_t	value;
};

/* ARGSUSED */
static int
find_globals_cb(uintptr_t addr, const void *data, void *private)
{
	struct zsd_lookup *zlp = private;
	zone_key_t key = zlp->key;
	const struct zsd_entry *zep = data;

	if (zep->zsd_key != key)
		return (WALK_NEXT);
	zlp->value = (uintptr_t)zep->zsd_data;
	return (WALK_DONE);
}

static uintptr_t
find_globals_impl(uintptr_t zoneaddr, const char *keyname, zone_key_t key,
    uint_t quiet)
{
	struct zsd_lookup zl;

	zl.key = key;
	zl.value = NULL;
	if (mdb_pwalk("zsd", find_globals_cb, &zl, zoneaddr) == -1) {
		mdb_warn("couldn't walk zsd");
		return (NULL);
	}
	if (quiet == FALSE) {
		if (zl.value == NULL) {
			if (keyname != NULL)
				mdb_warn("unable to find a registered ZSD "
				    "value for keyname %s\n", keyname);
			else
				mdb_warn("unable to find a registered ZSD "
				    "value for key %d\n", key);
		}
	}
	return (zl.value);
}

uintptr_t
find_globals_bykey(uintptr_t zoneaddr, zone_key_t key, uint_t quiet)
{
	return (find_globals_impl(zoneaddr, NULL, key, quiet));
}

uintptr_t
find_globals(uintptr_t zoneaddr, const char *keyname, uint_t quiet)
{
	zone_key_t key;

	if (mdb_readsym(&key, sizeof (zone_key_t), keyname) !=
	    sizeof (zone_key_t)) {
		mdb_warn("unable to read %s", keyname);
		return (NULL);
	}

	return (find_globals_impl(zoneaddr, keyname, key, quiet));
}

/*
 * Convenience routine for finding the offset of a 'member' of a given
 * structure, 'type'.
 */
int
getoffset(char *type, char *member, ulong_t *member_off)
{
	mdb_ctf_id_t id;

	/*
	 * Using CTF get the offset of 'member' in 'type'.
	 *
	 * If the following fails the caller will have to default
	 * to the compile time offset() and hope it's okay.  (See
	 * GETOFFSET() macro.)
	 */
	if (mdb_ctf_lookup_by_name(type, &id) != 0)
		return (DCMD_ERR);

	if (mdb_ctf_offsetof(id, member, member_off) != 0)
		return (DCMD_ERR);

	/*
	 * Determine if 'member' is word-aligned
	 */
	if (*member_off % (sizeof (uintptr_t) * NBBY) != 0)
		return (DCMD_ERR);

	*member_off /= NBBY;

	return (DCMD_OK);
}
