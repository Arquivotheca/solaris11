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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include "nfs_mdb.h"
#include "nfs4_mdb.h"

/* should ideally get this from some header file, or maybe use CTF? */
struct sv_stats {
	int	sv_activate;
	int	sv_find;
	int	sv_match;
	int	sv_inactive;
	int	sv_exchange;
};
/* end of what should ideally be gotten from a header file */

/*
 * Convenience structure which enables us to bundle together all the various
 * kstats that we may need to print.
 */
struct agg_stats {
	struct nfs_stats nfsstats;
	struct rpcstat rpcstats;
	uintptr_t clstat;
	uintptr_t clstat4;
	uintptr_t callback_stats;
};

static int stat_clnt_rpc(struct agg_stats *);
static int stat_serv_rpc(struct agg_stats *);
static int stat_callback(struct agg_stats *);
static int stat_clnt_nfs(struct agg_stats *, int);
static int stat_serv_nfs(struct agg_stats *, int);
static int stat_clnt_acl(struct agg_stats *, int);
static int stat_serv_acl(struct agg_stats *, int);
static int stat_clnt(struct agg_stats *, int, int);
static int stat_serv(struct agg_stats *, int, int);

static int shadow_stat(void);

static int pr_stats(uintptr_t, char *, int);
static int pr4_stats(uintptr_t, char *, int);
static void printout(char *header, uint64_t *value, int n, int percentflag);

void
nfs_stat_help(void)
{
	mdb_printf(
	    "Switches similar to those of nfsstat command.\n"
	    "            ::nfs_stat [-csb][-234][-anr]\n"
	    "            ::nfs_stat -c	\t-> Client Statistics.\n"
	    "            ::nfs_stat -s	\t-> Server Statistics.\n"
	    "            ::nfs_stat -b\t\t-> Callback Stats. (V4 only)\n"
	    "            ::nfs_stat -r	\t-> RPC    Statistics.\n"
	    "            ::nfs_stat -n	\t-> NFS    Statistics.\n"
	    "            ::nfs_stat -a	\t-> ACL    Statistics.\n"
	    "            ::nfs_stat -2	\t-> Version 2.\n"
	    "            ::nfs_stat -3	\t-> Version 3.\n"
	    "            ::nfs_stat -4	\t-> Version 4.\n"
	    "If neither of -csb are specified, all three are assumed.\n"
	    "If neither of -arn are specified, all three are assumed.\n"
	    "If neither of -234 are specified, all three are assumed.\n"
	    "The -b works independently of the -234 flags\n\n"
	    "The following generic (non-NFS specific) format is supported:\n"
	    "<kstat addr>::nfs_stat $[count]\n"
	    "    -> Print Stats in a kstat_named array of `count' elements\n");
}

#define	STAT_CLIENT	0x1
#define	STAT_SERVER	0x2
#define	STAT_CB		0x4

#define	STAT_ACL	0x1
#define	STAT_NFS	0x2
#define	STAT_RPC	0x4

#define	STAT_V2		0x1
#define	STAT_V3		0x2
#define	STAT_V4		0x4

/* ARGSUSED */
int
nfs_stat(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	int host_flag = 0;
	int type_flag = 0;
	int vers_flag = 0;
	struct agg_stats stats;
	uintptr_t gaddr;
	uintptr_t zoneaddr;

	if (argc == 1 && argv->a_type == MDB_TYPE_IMMEDIATE) {
		kstat_named_t ks;
		int i = argv->a_un.a_val;
		while (i--) {
			if (mdb_vread(&ks, sizeof (ks), addr) < 0) {
				mdb_warn("could not read `kstat_named_t'\n");
				return (DCMD_ERR);
			}
			mdb_printf(" %16s %7ld\n", ks.name, ks.value.ui64);
			addr += sizeof (ks);
		}
		return (DCMD_OK);
	}

	if (mdb_getopts(argc, argv,
	    's', MDB_OPT_SETBITS, STAT_SERVER, &host_flag,
	    'c', MDB_OPT_SETBITS, STAT_CLIENT, &host_flag,
	    'b', MDB_OPT_SETBITS, STAT_CB, &host_flag,
	    'a', MDB_OPT_SETBITS, STAT_ACL, &type_flag,
	    'r', MDB_OPT_SETBITS, STAT_RPC, &type_flag,
	    'n', MDB_OPT_SETBITS, STAT_NFS, &type_flag,
	    '2', MDB_OPT_SETBITS, STAT_V2, &vers_flag,
	    '3', MDB_OPT_SETBITS, STAT_V3, &vers_flag,
	    '4', MDB_OPT_SETBITS, STAT_V4, &vers_flag,
	    NULL) != argc)
		return (DCMD_USAGE);


	if (!(flags & DCMD_ADDRSPEC)) {
		if (mdb_readsym(&zoneaddr, sizeof (uintptr_t),
		    "global_zone") == -1) {
			mdb_warn("unable to locate global_zone");
			return (NULL);
		}
	} else {
		zoneaddr = addr;
	}


	gaddr = find_globals(zoneaddr, "nfsstat_zone_key", FALSE);

	if (mdb_vread(&stats.nfsstats, sizeof (struct nfs_stats),
	    gaddr) == -1) {
		mdb_warn("unable to read nfs_stats at %p", gaddr);
		return (DCMD_ERR);
	}

	gaddr = find_globals(zoneaddr, "rpcstat_zone_key", FALSE);
	if (mdb_vread(&stats.rpcstats, sizeof (struct rpcstat), gaddr) == -1) {
		mdb_warn("unable to read rpcstat at %p", gaddr);
		return (DCMD_ERR);
	}

	gaddr = find_globals(zoneaddr, "nfsclnt_zone_key", FALSE);
	stats.clstat = gaddr + offsetof(struct nfs_clnt, nfscl_stat);

	gaddr = find_globals(zoneaddr, "nfs4clnt_zone_key", FALSE);
	stats.clstat4 = gaddr + offsetof(struct nfs4_clnt, nfscl_stat);

	gaddr = find_globals(zoneaddr, "nfs4_callback_zone_key", FALSE);
	stats.callback_stats = gaddr + offsetof(struct nfs4_callback_globals,
	    nfs4_callback_stats);

	if (!host_flag)
		host_flag = STAT_SERVER | STAT_CLIENT | STAT_CB;
	if (!vers_flag)
		vers_flag = STAT_V2 | STAT_V3 | STAT_V4;
	if (!type_flag)
		type_flag = STAT_ACL | STAT_RPC | STAT_NFS;

	if (host_flag & STAT_CB)
		if (stat_callback(&stats))
			return (DCMD_ERR);
	if (host_flag & STAT_SERVER)
		if (stat_serv(&stats, type_flag, vers_flag))
			return (DCMD_ERR);
	if (host_flag & STAT_CLIENT)
		if (stat_clnt(&stats, type_flag, vers_flag))
			return (DCMD_ERR);
	return (DCMD_OK);
}

static int
stat_serv(struct agg_stats *statsp, int type_flag, int vers_flag)
{
	mdb_printf("SERVER STATISTICS:\n");
	if (type_flag & STAT_ACL)
		if (stat_serv_acl(statsp, vers_flag))
			return (-1);
	if (type_flag & STAT_NFS)
		if (stat_serv_nfs(statsp, vers_flag))
			return (-1);
	if (type_flag & STAT_RPC)
		if (stat_serv_rpc(statsp))
			return (-1);
	return (0);
}

static int
stat_clnt(struct agg_stats *statsp, int type_flag, int vers_flag)
{
	mdb_printf("CLIENT STATISTICS:\n");
	if (type_flag & STAT_ACL)
		if (stat_clnt_acl(statsp, vers_flag))
			return (-1);
	if (type_flag & STAT_NFS)
		if (stat_clnt_nfs(statsp, vers_flag))
			return (-1);
	if (type_flag & STAT_RPC)
		if (stat_clnt_rpc(statsp))
			return (-1);
	return (0);
}

#define	PR_STATS(addr, name)	\
	pr_stats((uintptr_t)addr, name, 1)
#define	PR_STATS_NOSUM(addr, name)	\
	pr_stats((uintptr_t)addr, name, 0)
#define	PR4_STATS(addr, name)	\
	pr4_stats((uintptr_t)addr, name, 1)

static int
stat_clnt_acl(struct agg_stats *statsp, int vers_flag)
{
	mdb_printf("ACL Statistics:\n");
	if (vers_flag & STAT_V2) {
		mdb_printf("Version 2:\n");
		if (PR_STATS(statsp->nfsstats.nfs_stats[NFS_V2].aclreqcnt_ptr,
		    "aclreqcnt_v2_tmpl"))
			return (-1);
	}
	if (vers_flag & STAT_V3) {
		mdb_printf("Version 3:\n");
		if (PR_STATS(statsp->nfsstats.nfs_stats[NFS_V3].aclreqcnt_ptr,
		    "aclreqcnt_v3_tmpl"))
			return (-1);
	}
	if (vers_flag & STAT_V4) {
		mdb_printf("Version 4:\n");
		if (PR_STATS(statsp->nfsstats.nfs_stats[NFS_V4].aclreqcnt_ptr,
		    "aclreqcnt_v4_tmpl"))
			return (-1);
	}
	return (0);
}

static int
stat_clnt_nfs(struct agg_stats *statsp, int vers_flag)
{
	mdb_printf("NFS Statistics:\n");
	if (PR_STATS_NOSUM(statsp->clstat, "clstat_tmpl"))
		return (-1);
	if (vers_flag & STAT_V2) {
		mdb_printf("Version 2:\n");
		if (PR_STATS(statsp->nfsstats.nfs_stats[NFS_V2].rfsreqcnt_ptr,
		    "rfsreqcnt_v2_tmpl"))
			return (-1);
	}
	if (vers_flag & STAT_V3) {
		mdb_printf("Version 3:\n");
		if (PR_STATS(statsp->nfsstats.nfs_stats[NFS_V3].rfsreqcnt_ptr,
		    "rfsreqcnt_v3_tmpl"))
			return (-1);
	}
	if (vers_flag & STAT_V4) {
		mdb_printf("V4 Client:\n");
		if (PR_STATS_NOSUM(statsp->clstat4, "clstat4_tmpl"))
			return (-1);
		mdb_printf("Version 4:\n");
		if (PR4_STATS(statsp->nfsstats.nfs_stats[NFS_V4].rfsreqcnt_ptr,
		    "rfsreqcnt_v4_tmpl"))
			return (-1);
	}
	return (0);
}

static int
stat_clnt_rpc(struct agg_stats *statsp)
{
	mdb_printf("RPC Statistics:\n");
	mdb_printf("Connection-oriented:\n");
	if (PR_STATS_NOSUM(statsp->rpcstats.rpc_cots_client,
	    "cots_rcstat_tmpl"))
		return (-1);
	mdb_printf("Connection-less:\n");
	if (PR_STATS_NOSUM(statsp->rpcstats.rpc_clts_client,
	    "clts_rcstat_tmpl"))
		return (-1);
	return (0);
}

static int
stat_serv_acl(struct agg_stats *statsp, int vers_flag)
{
	mdb_printf("ACL Statistics:\n");
	if (vers_flag & STAT_V2) {
		mdb_printf("Version 2:\n");
		if (PR_STATS(statsp->nfsstats.nfs_stats[NFS_V2].aclproccnt_ptr,
		    "aclproccnt_v2_tmpl"))
			return (-1);
	}
	if (vers_flag & STAT_V3) {
		mdb_printf("Version 3:\n");
		if (PR_STATS(statsp->nfsstats.nfs_stats[NFS_V3].aclproccnt_ptr,
		    "aclproccnt_v3_tmpl"))
			return (-1);
	}
	if (vers_flag & STAT_V4) {
		mdb_printf("Version 4:\n");
		if (PR_STATS(statsp->nfsstats.nfs_stats[NFS_V4].aclproccnt_ptr,
		    "aclproccnt_v4_tmpl"))
			return (-1);
	}
	return (0);
}

static int
stat_serv_nfs(struct agg_stats *statsp, int vers_flag)
{
	mdb_printf("NFS Statistics:\n");
	if (vers_flag & STAT_V2) {
		mdb_printf("Version 2:\n");
		if (PR_STATS_NOSUM(statsp->nfsstats.svstat_ptr[2],
		    "svstat_tmpl"))
			return (-1);
		if (PR_STATS(statsp->nfsstats.nfs_stats[NFS_V2].rfsproccnt_ptr,
		    "rfsproccnt_v2_tmpl"))
			return (-1);
	}
	if (vers_flag & STAT_V3) {
		mdb_printf("Version 3:\n");
		if (PR_STATS_NOSUM(statsp->nfsstats.svstat_ptr[3],
		    "svstat_tmpl"))
			return (-1);
		if (PR_STATS(statsp->nfsstats.nfs_stats[NFS_V3].rfsproccnt_ptr,
		    "rfsproccnt_v3_tmpl"))
			return (-1);
	}
	if (vers_flag & STAT_V4) {
		mdb_printf("Version 4:\n");
		if (PR_STATS_NOSUM(statsp->nfsstats.svstat_ptr[4],
		    "svstat_tmpl"))
			return (-1);
		if (PR4_STATS(statsp->nfsstats.nfs_stats[NFS_V4].rfsproccnt_ptr,
		    "rfsproccnt_v4_tmpl"))
			return (-1);
		if (shadow_stat())
			return (-1);
	}
	return (0);
}

static int
stat_serv_rpc(struct agg_stats *statsp)
{
	mdb_printf("RPC Statistics:\n");
	mdb_printf("Connection-oriented:\n");
	if (PR_STATS_NOSUM(statsp->rpcstats.rpc_cots_server,
	    "cots_rsstat_tmpl"))
		return (-1);
	mdb_printf("Connection-less:\n");
	if (PR_STATS_NOSUM(statsp->rpcstats.rpc_clts_server,
	    "clts_rsstat_tmpl"))
		return (-1);
	return (0);
}

static int
stat_callback(struct agg_stats *statsp)
{
	mdb_printf("CALLBACK STATISTICS:\n");
	if (PR_STATS_NOSUM(statsp->callback_stats, "nfs4_callback_stats_tmpl"))
		return (-1);
	return (0);
}

/*
 * These values are currently global across all zones.
 */
static int
shadow_stat(void)
{
	struct sv_stats ss;

	mdb_printf("Shadow Statistics:\n");
	if (mdb_readvar(&ss, "sv_stats") == -1) {
		mdb_warn("couldn't read `sv_stats'\n");
		return (-1);
	}
	mdb_printf("%-16s%-16s%-16s%-16s%-16s\n",
	    "activate", "find", "match", "inactive", "exchange");
	mdb_printf("%-16d%-16d%-16d%-16d%-16d\n",
	    ss.sv_activate, ss.sv_find, ss.sv_match,
	    ss.sv_inactive, ss.sv_exchange);
	return (0);
}

#define	NFS_STAT_COLUMNS 16
static int
pr_stats(uintptr_t addr, char *tmpl_name, int percentflag)
{
	GElf_Sym sym;
	kstat_named_t ks;
	int i, status = 0;
	char *header;
	uint64_t *value;
	uint_t cnt;

	if (mdb_lookup_by_name(tmpl_name, &sym)) {
		mdb_warn("failed to lookup `%s'", tmpl_name);
		return (-1);
	}
	cnt = sym.st_size / sizeof (kstat_named_t);

	header = mdb_alloc(cnt * NFS_STAT_COLUMNS, UM_SLEEP);
	value = mdb_alloc(cnt * sizeof (uint64_t), UM_SLEEP);
	for (i = 0; i < cnt; i++) {
		if (mdb_vread(&ks, sizeof (ks), addr) < 0) {
			status = -1;
			goto die;
		}
		mdb_snprintf(&header[NFS_STAT_COLUMNS*i],
		    NFS_STAT_COLUMNS, "%s", ks.name);
		value[i] = ks.value.ui64;
		addr += sizeof (ks);
	}
	printout(header, value, cnt, percentflag);
die:
	mdb_free(header, cnt * NFS_STAT_COLUMNS);
	mdb_free(value, cnt * sizeof (uint64_t));
	return (status);
}

static int
pr4_stats(uintptr_t addr, char *tmpl_name, int percentflag)
{
	GElf_Sym sym;
	kstat_named_t ks;
	int i, status = 0;
	char *header;
	uint64_t *value;
	uint_t cnt;

	if (mdb_lookup_by_name(tmpl_name, &sym)) {
		mdb_warn("failed to lookup `%s'", tmpl_name);
		return (-1);
	}
	cnt = sym.st_size / sizeof (kstat_named_t);

	/*
	 * Print "null" and "compound" counts.
	 */
	header = mdb_alloc((cnt - 2) * NFS_STAT_COLUMNS, UM_SLEEP);
	value = mdb_alloc((cnt - 2) * sizeof (uint64_t), UM_SLEEP);
	for (i = 0; i < 2; i++) {
		if (mdb_vread(&ks, sizeof (ks), addr) < 0) {
			status = -1;
			goto die;
		}
		mdb_snprintf(&header[NFS_STAT_COLUMNS*i],
		    NFS_STAT_COLUMNS, "%s", ks.name);
		value[i] = ks.value.ui64;
		addr += sizeof (ks);
	}
	printout(header, value, 2, percentflag);

	/*
	 * Print out everything else.
	 */
	for (i = 0; i < cnt-2; i++) {
		if (mdb_vread(&ks, sizeof (ks), addr) < 0) {
			status = -1;
			goto die;
		}
		mdb_snprintf(&header[NFS_STAT_COLUMNS*i],
		    NFS_STAT_COLUMNS, "%s", ks.name);
		value[i] = ks.value.ui64;
		addr += sizeof (ks);
	}
	printout(header, value, cnt - 2, percentflag);
die:
	mdb_free(header, (cnt - 2) * NFS_STAT_COLUMNS);
	mdb_free(value, (cnt - 2) * sizeof (uint64_t));
	return (status);
}

static void
printout(char *header, uint64_t *value, int n, int percentflag)
{
	int i, sum = 0;
	int num = 0;
	char str[32];

	if (percentflag) {
		for (i = 0; i < n; i++)
			sum += value[i];
		mdb_printf("(%d calls)\n", sum);
	}

	/* to avoid divide-by-zero error when calculating percentage */
	if (sum == 0) sum = 1;

	for (i = 0; i < n; i++) {
		mdb_printf("%-*s", NFS_STAT_COLUMNS,
		    &header[NFS_STAT_COLUMNS*i]);
		num++;
		if (num == 79 / NFS_STAT_COLUMNS) {
back:			mdb_printf("\n");
			if (percentflag) {
				do {
					mdb_snprintf(str, 32, "%ld %d%% ",
					    value[i+1-num],
					    (value[i+1-num]*100)/sum);
					mdb_printf("%-*s", NFS_STAT_COLUMNS,
					    str);
				} while (--num);
			} else {
				do {
					mdb_snprintf(str, 32, "%ld ",
					    value[i+1-num]);
					mdb_printf("%-*s", NFS_STAT_COLUMNS,
					    str);
				} while (--num);
			}
			mdb_printf("\n");
		}
	}
	if (num) {
		i = n - 1;
		goto back;
	}
	mdb_printf("\n");
}
