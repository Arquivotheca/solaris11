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
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 *	Copyright (c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *	All Rights Reserved
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/cred.h>
#include <sys/buf.h>
#include <sys/vfs.h>
#include <sys/vfs_opreg.h>
#include <sys/vnode.h>
#include <sys/uio.h>
#include <sys/errno.h>
#include <sys/sysmacros.h>
#include <sys/statvfs.h>
#include <sys/kmem.h>
#include <sys/dirent.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/systeminfo.h>
#include <sys/flock.h>
#include <sys/pathname.h>
#include <sys/nbmlock.h>
#include <sys/share.h>
#include <sys/atomic.h>
#include <sys/policy.h>
#include <sys/fem.h>
#include <sys/sdt.h>
#include <sys/ddi.h>
#include <sys/zone.h>
#include <sys/clconf.h>

#include <fs/fs_reparse.h>

#include <rpc/types.h>
#include <rpc/auth.h>
#include <rpc/rpcsec_gss.h>
#include <rpc/svc.h>

#include <nfs/nfs.h>
#include <nfs/export.h>
#include <nfs/nfs_cmd.h>
#include <nfs/lm.h>
#include <nfs/nfs4.h>
#include <nfs/nfs4_fsh.h>
#include <nfs/nfssys.h>
#include <nfs/nfs_srv_inst_impl.h>

#include <sys/strsubr.h>
#include <sys/strsun.h>

#include <inet/common.h>
#include <inet/ip.h>
#include <inet/ip6.h>

#include <sys/tsol/label.h>
#include <sys/tsol/tndb.h>

#define	RFS4_MAXLOCK_TRIES 4	/* Try to get the lock this many times */
static int rfs4_maxlock_tries = RFS4_MAXLOCK_TRIES;
#define	RFS4_LOCK_DELAY 10	/* Milliseconds */
static clock_t  rfs4_lock_delay = RFS4_LOCK_DELAY;
extern struct svc_ops rdma_svc_ops;
extern int nfs_loaned_buffers;
/* End of Tunables */

static int rdma_setup_read_data4(READ4args *, READ4res *);
static nfsstat4	fse_op_check(struct compound_state *, uint_t);

/*
 * Used to bump the stateid4.seqid value and show changes in the stateid
 */
#define	next_stateid(sp) (++(sp)->bits.chgseq)

/*
 * RFS4_MINLEN_ENTRY4: XDR-encoded size of smallest possible dirent.
 *	This is used to return NFS4ERR_TOOSMALL when clients specify
 *	maxcount that isn't large enough to hold the smallest possible
 *	XDR encoded dirent.
 *
 *	    sizeof cookie (8 bytes) +
 *	    sizeof name_len (4 bytes) +
 *	    sizeof smallest (padded) name (4 bytes) +
 *	    sizeof bitmap4_len (12 bytes) +   NOTE: we always encode len=2 bm4
 *	    sizeof attrlist4_len (4 bytes) +
 *	    sizeof next boolean (4 bytes)
 *
 * RFS4_MINLEN_RDDIR4: XDR-encoded size of READDIR op reply containing
 * the smallest possible entry4 (assumes no attrs requested).
 *	sizeof nfsstat4 (4 bytes) +
 *	sizeof verifier4 (8 bytes) +
 *	sizeof entry4list bool (4 bytes) +
 *	sizeof entry4 	(36 bytes) +
 *	sizeof eof bool  (4 bytes)
 *
 * RFS4_MINLEN_RDDIR_BUF: minimum length of buffer server will provide to
 *	VOP_READDIR.  Its value is the size of the maximum possible dirent
 *	for solaris.  The DIRENT64_RECLEN macro returns	the size of dirent
 *	required for a given name length.  MAXNAMELEN is the maximum
 *	filename length allowed in Solaris.  The first two DIRENT64_RECLEN()
 *	macros are to allow for . and .. entries -- just a minor tweak to try
 *	and guarantee that buffer we give to VOP_READDIR will be large enough
 *	to hold ., .., and the largest possible solaris dirent64.
 */
#define	RFS4_MINLEN_ENTRY4 36
#define	RFS4_MINLEN_RDDIR4 (4 + NFS4_VERIFIER_SIZE + 4 + RFS4_MINLEN_ENTRY4 + 4)
#define	RFS4_MINLEN_RDDIR_BUF \
	(DIRENT64_RECLEN(1) + DIRENT64_RECLEN(2) + DIRENT64_RECLEN(MAXNAMELEN))

/*
 * It would be better to pad to 4 bytes since that's what XDR would do,
 * but the dirents UFS gives us are already padded to 8, so just take
 * what we're given.  Dircount is only a hint anyway.  Currently the
 * solaris kernel is ASCII only, so there's no point in calling the
 * UTF8 functions.
 *
 * dirent64: named padded to provide 8 byte struct alignment
 *	d_ino(8) + d_off(8) + d_reclen(2) + d_name(namelen + null(1) + pad)
 *
 * cookie: uint64_t   +  utf8namelen: uint_t  +   utf8name padded to 8 bytes
 *
 */
#define	DIRENT64_TO_DIRCOUNT(dp) \
	(3 * BYTES_PER_XDR_UNIT + DIRENT64_NAMELEN((dp)->d_reclen))

void	rfs4_init_compound_state(struct compound_state *);

static void	nullfree(caddr_t);
static void	rfs4_op_inval(nfs_argop4 *, nfs_resop4 *, struct svc_req *,
			struct compound_state *);
static void	rfs4_op_access(nfs_argop4 *, nfs_resop4 *, struct svc_req *,
			struct compound_state *);
static void	rfs4_op_close(nfs_argop4 *, nfs_resop4 *, struct svc_req *,
			struct compound_state *);
static void	rfs4_op_commit(nfs_argop4 *, nfs_resop4 *, struct svc_req *,
			struct compound_state *);
static void	rfs4_op_create(nfs_argop4 *, nfs_resop4 *, struct svc_req *,
			struct compound_state *);
static void	rfs4_op_create_free(nfs_resop4 *resop);
static void	rfs4_op_delegreturn(nfs_argop4 *, nfs_resop4 *,
			struct svc_req *, struct compound_state *);
static void	rfs4_op_delegpurge(nfs_argop4 *, nfs_resop4 *,
			struct svc_req *, struct compound_state *);
static void	rfs4_op_getattr(nfs_argop4 *, nfs_resop4 *, struct svc_req *,
			struct compound_state *);
static void	rfs4_op_getattr_free(nfs_resop4 *);
static void	rfs4_op_getfh(nfs_argop4 *, nfs_resop4 *, struct svc_req *,
			struct compound_state *);
static void	rfs4_op_getfh_free(nfs_resop4 *);
static void	rfs4_op_illegal(nfs_argop4 *, nfs_resop4 *, struct svc_req *,
			struct compound_state *);
static void	rfs4_op_link(nfs_argop4 *, nfs_resop4 *, struct svc_req *,
			struct compound_state *);
static void	rfs4_op_lock(nfs_argop4 *, nfs_resop4 *, struct svc_req *,
			struct compound_state *);
static void	lock_denied_free(nfs_resop4 *);
static void	rfs4_op_locku(nfs_argop4 *, nfs_resop4 *, struct svc_req *,
			struct compound_state *);
static void	rfs4_op_lockt(nfs_argop4 *, nfs_resop4 *, struct svc_req *,
			struct compound_state *);
static void	rfs4_op_lookup(nfs_argop4 *, nfs_resop4 *, struct svc_req *,
			struct compound_state *);
static void	rfs4_op_lookupp(nfs_argop4 *, nfs_resop4 *, struct svc_req *,
			struct compound_state *);
static void	rfs4_op_openattr(nfs_argop4 *argop, nfs_resop4 *resop,
				struct svc_req *req, struct compound_state *cs);
static void	rfs4_op_nverify(nfs_argop4 *, nfs_resop4 *, struct svc_req *,
			struct compound_state *);
static void	rfs4_op_open(nfs_argop4 *, nfs_resop4 *, struct svc_req *,
			struct compound_state *);
static void	rfs4_op_open_confirm(nfs_argop4 *, nfs_resop4 *,
			struct svc_req *, struct compound_state *);
static void	rfs4_op_open_downgrade(nfs_argop4 *, nfs_resop4 *,
			struct svc_req *, struct compound_state *);
static void	rfs4_op_putfh(nfs_argop4 *, nfs_resop4 *, struct svc_req *,
			struct compound_state *);
static void	rfs4_op_putpubfh(nfs_argop4 *, nfs_resop4 *, struct svc_req *,
			struct compound_state *);
static void	rfs4_op_putrootfh(nfs_argop4 *, nfs_resop4 *, struct svc_req *,
			struct compound_state *);
static void	rfs4_op_read(nfs_argop4 *, nfs_resop4 *, struct svc_req *,
			struct compound_state *);
static void	rfs4_op_read_free(nfs_resop4 *);
static void	rfs4_op_readdir_free(nfs_resop4 *resop);
static void	rfs4_op_readlink(nfs_argop4 *, nfs_resop4 *, struct svc_req *,
			struct compound_state *);
static void	rfs4_op_readlink_free(nfs_resop4 *);
static void	rfs4_op_release_lockowner(nfs_argop4 *, nfs_resop4 *,
			struct svc_req *, struct compound_state *);
static void	rfs4_op_remove(nfs_argop4 *, nfs_resop4 *, struct svc_req *,
			struct compound_state *);
static void	rfs4_op_rename(nfs_argop4 *, nfs_resop4 *, struct svc_req *,
			struct compound_state *);
static void	rfs4_op_renew(nfs_argop4 *, nfs_resop4 *, struct svc_req *,
			struct compound_state *);
static void	rfs4_op_restorefh(nfs_argop4 *, nfs_resop4 *, struct svc_req *,
			struct compound_state *);
static void	rfs4_op_savefh(nfs_argop4 *, nfs_resop4 *, struct svc_req *,
			struct compound_state *);
static void	rfs4_op_setattr(nfs_argop4 *, nfs_resop4 *, struct svc_req *,
			struct compound_state *);
static void	rfs4_op_verify(nfs_argop4 *, nfs_resop4 *, struct svc_req *,
			struct compound_state *);
static void	rfs4_op_write(nfs_argop4 *, nfs_resop4 *, struct svc_req *,
			struct compound_state *);
static void	rfs4_op_setclientid(nfs_argop4 *, nfs_resop4 *,
			struct svc_req *, struct compound_state *);
static void	rfs4_op_setclientid_confirm(nfs_argop4 *, nfs_resop4 *,
			struct svc_req *req, struct compound_state *);
static void	rfs4_op_secinfo(nfs_argop4 *, nfs_resop4 *, struct svc_req *,
			struct compound_state *);
static void	rfs4_op_secinfo_free(nfs_resop4 *);

static nfsstat4 check_open_access(uint32_t,
				struct compound_state *, struct svc_req *);
nfsstat4 rfs4_client_sysid(rfs4_client_t *, sysid_t *);
void rfs4_ss_clid(rfs4_client_t *);

/*
 * translation table for attrs
 */
struct nfs4_ntov_table {
	union nfs4_attr_u *na;
	uint8_t amap[NFS4_MAXNUM_ATTRS];
	int attrcnt;
	bool_t vfsstat;
};

static void	nfs4_ntov_table_init(struct nfs4_ntov_table *ntovp);
static void	nfs4_ntov_table_free(struct nfs4_ntov_table *ntovp,
				    struct nfs4_svgetit_arg *sargp);

static nfsstat4	do_rfs4_set_attrs(bitmap4 *resp, fattr4 *fattrp,
		    struct compound_state *cs, struct nfs4_svgetit_arg *sargp,
		    struct nfs4_ntov_table *ntovp, nfs4_attr_cmd_t cmd);

/*
 * NFS4 op dispatch table
 */

struct rfsv4disp {
	void	(*dis_proc)();		/* proc to call */
	void	(*dis_resfree)();	/* frees space allocated by proc */
	int	dis_flags;		/* RPC_IDEMPOTENT, etc... */
};

static struct rfsv4disp rfsv4disptab[] = {
	/*
	 * NFS VERSION 4
	 */

	/* RFS_NULL = 0 */
	{rfs4_op_illegal, nullfree, 0},

	/* UNUSED = 1 */
	{rfs4_op_illegal, nullfree, 0},

	/* UNUSED = 2 */
	{rfs4_op_illegal, nullfree, 0},

	/* OP_ACCESS = 3 */
	{rfs4_op_access, nullfree, RPC_IDEMPOTENT},

	/* OP_CLOSE = 4 */
	{rfs4_op_close, nullfree, 0},

	/* OP_COMMIT = 5 */
	{rfs4_op_commit, nullfree, RPC_IDEMPOTENT},

	/* OP_CREATE = 6 */
	{rfs4_op_create, nullfree, 0},

	/* OP_DELEGPURGE = 7 */
	{rfs4_op_delegpurge, nullfree, 0},

	/* OP_DELEGRETURN = 8 */
	{rfs4_op_delegreturn, nullfree, 0},

	/* OP_GETATTR = 9 */
	{rfs4_op_getattr, rfs4_op_getattr_free, RPC_IDEMPOTENT},

	/* OP_GETFH = 10 */
	{rfs4_op_getfh, rfs4_op_getfh_free, RPC_ALL},

	/* OP_LINK = 11 */
	{rfs4_op_link, nullfree, 0},

	/* OP_LOCK = 12 */
	{rfs4_op_lock, lock_denied_free, 0},

	/* OP_LOCKT = 13 */
	{rfs4_op_lockt, lock_denied_free, 0},

	/* OP_LOCKU = 14 */
	{rfs4_op_locku, nullfree, 0},

	/* OP_LOOKUP = 15 */
	{rfs4_op_lookup, nullfree, (RPC_IDEMPOTENT | RPC_PUBLICFH_OK)},

	/* OP_LOOKUPP = 16 */
	{rfs4_op_lookupp, nullfree, (RPC_IDEMPOTENT | RPC_PUBLICFH_OK)},

	/* OP_NVERIFY = 17 */
	{rfs4_op_nverify, nullfree, RPC_IDEMPOTENT},

	/* OP_OPEN = 18 */
	{rfs4_op_open, rfs4_free_reply, 0},

	/* OP_OPENATTR = 19 */
	{rfs4_op_openattr, nullfree, 0},

	/* OP_OPEN_CONFIRM = 20 */
	{rfs4_op_open_confirm, nullfree, 0},

	/* OP_OPEN_DOWNGRADE = 21 */
	{rfs4_op_open_downgrade, nullfree, 0},

	/* OP_OPEN_PUTFH = 22 */
	{rfs4_op_putfh, nullfree, RPC_ALL},

	/* OP_PUTPUBFH = 23 */
	{rfs4_op_putpubfh, nullfree, RPC_ALL},

	/* OP_PUTROOTFH = 24 */
	{rfs4_op_putrootfh, nullfree, RPC_ALL},

	/* OP_READ = 25 */
	{rfs4_op_read, rfs4_op_read_free, RPC_IDEMPOTENT},

	/* OP_READDIR = 26 */
	{rfs4_op_readdir, rfs4_op_readdir_free, RPC_IDEMPOTENT},

	/* OP_READLINK = 27 */
	{rfs4_op_readlink, rfs4_op_readlink_free, RPC_IDEMPOTENT},

	/* OP_REMOVE = 28 */
	{rfs4_op_remove, nullfree, 0},

	/* OP_RENAME = 29 */
	{rfs4_op_rename, nullfree, 0},

	/* OP_RENEW = 30 */
	{rfs4_op_renew, nullfree, 0},

	/* OP_RESTOREFH = 31 */
	{rfs4_op_restorefh, nullfree, RPC_ALL},

	/* OP_SAVEFH = 32 */
	{rfs4_op_savefh, nullfree, RPC_ALL},

	/* OP_SECINFO = 33 */
	{rfs4_op_secinfo, rfs4_op_secinfo_free, 0},

	/* OP_SETATTR = 34 */
	{rfs4_op_setattr, nullfree, 0},

	/* OP_SETCLIENTID = 35 */
	{rfs4_op_setclientid, nullfree, 0},

	/* OP_SETCLIENTID_CONFIRM = 36 */
	{rfs4_op_setclientid_confirm, nullfree, 0},

	/* OP_VERIFY = 37 */
	{rfs4_op_verify, nullfree, RPC_IDEMPOTENT},

	/* OP_WRITE = 38 */
	{rfs4_op_write, nullfree, 0},

	/* OP_RELEASE_LOCKOWNER = 39 */
	{rfs4_op_release_lockowner, nullfree, 0},
};

static uint_t rfsv4disp_cnt = sizeof (rfsv4disptab) / sizeof (rfsv4disptab[0]);

#define	OP_ILLEGAL_IDX (rfsv4disp_cnt)

#ifdef DEBUG

int		rfs4_fillone_debug = 0;
int		rfs4_no_stub_access = 1;
int		rfs4_rddir_debug = 0;

static char    *rfs4_op_string[] = {
	"rfs4_op_null",
	"rfs4_op_1 unused",
	"rfs4_op_2 unused",
	"rfs4_op_access",
	"rfs4_op_close",
	"rfs4_op_commit",
	"rfs4_op_create",
	"rfs4_op_delegpurge",
	"rfs4_op_delegreturn",
	"rfs4_op_getattr",
	"rfs4_op_getfh",
	"rfs4_op_link",
	"rfs4_op_lock",
	"rfs4_op_lockt",
	"rfs4_op_locku",
	"rfs4_op_lookup",
	"rfs4_op_lookupp",
	"rfs4_op_nverify",
	"rfs4_op_open",
	"rfs4_op_openattr",
	"rfs4_op_open_confirm",
	"rfs4_op_open_downgrade",
	"rfs4_op_putfh",
	"rfs4_op_putpubfh",
	"rfs4_op_putrootfh",
	"rfs4_op_read",
	"rfs4_op_readdir",
	"rfs4_op_readlink",
	"rfs4_op_remove",
	"rfs4_op_rename",
	"rfs4_op_renew",
	"rfs4_op_restorefh",
	"rfs4_op_savefh",
	"rfs4_op_secinfo",
	"rfs4_op_setattr",
	"rfs4_op_setclientid",
	"rfs4_op_setclient_confirm",
	"rfs4_op_verify",
	"rfs4_op_write",
	"rfs4_op_release_lockowner",
	"rfs4_op_illegal"
};
#endif

void	rfs4_ss_chkclid(rfs4_client_t *);

extern size_t   strlcpy(char *dst, const char *src, size_t dstsize);

extern void	rfs4_free_fs_locations4(fs_locations4 *);

#ifdef	nextdp
#undef nextdp
#endif
#define	nextdp(dp)	((struct dirent64 *)((char *)(dp) + (dp)->d_reclen))

static const fs_operation_def_t nfs4_rd_deleg_tmpl[] = {
	VOPNAME_OPEN,		{ .femop_open = deleg_rd_open },
	VOPNAME_WRITE,		{ .femop_write = deleg_rd_write },
	VOPNAME_SETATTR,	{ .femop_setattr = deleg_rd_setattr },
	VOPNAME_RWLOCK,		{ .femop_rwlock = deleg_rd_rwlock },
	VOPNAME_SPACE,		{ .femop_space = deleg_rd_space },
	VOPNAME_SETSECATTR,	{ .femop_setsecattr = deleg_rd_setsecattr },
	VOPNAME_VNEVENT,	{ .femop_vnevent = deleg_rd_vnevent },
	NULL,			NULL
};
static const fs_operation_def_t nfs4_wr_deleg_tmpl[] = {
	VOPNAME_OPEN,		{ .femop_open = deleg_wr_open },
	VOPNAME_READ,		{ .femop_read = deleg_wr_read },
	VOPNAME_WRITE,		{ .femop_write = deleg_wr_write },
	VOPNAME_SETATTR,	{ .femop_setattr = deleg_wr_setattr },
	VOPNAME_RWLOCK,		{ .femop_rwlock = deleg_wr_rwlock },
	VOPNAME_SPACE,		{ .femop_space = deleg_wr_space },
	VOPNAME_SETSECATTR,	{ .femop_setsecattr = deleg_wr_setsecattr },
	VOPNAME_VNEVENT,	{ .femop_vnevent = deleg_wr_vnevent },
	NULL,			NULL
};

#define	INVALID_MOVED_ATTR(attr) \
	((attr) & ~(FATTR4_FSID_MASK | \
	FATTR4_FS_LOCATIONS_MASK | FATTR4_MOUNTED_ON_FILEID_MASK))
#define	VALID_MOVED_ATTR(attr) \
	((attr) & (FATTR4_FSID_MASK | \
	FATTR4_FS_LOCATIONS_MASK | FATTR4_MOUNTED_ON_FILEID_MASK))

int
rfs4_set_nodeid(nodeid_t nodeid)
{
	rfs_inst_t *rip;

	rip = rfs_inst_find(TRUE);
	if (rip == NULL)
		return (ENOENT);
	if (rip->ri_state != RFS_INST_OFFLINE) {
		rfs_inst_active_rele(rip);
		return (EINVAL);
	}
	ASSERT(nodeid >= NODEID_UNKNOWN && nodeid <= NFS_MAX_NODEID);
	rip->ri_hanfs_id = nodeid;
	rfs_inst_active_rele(rip);
	return (0);
}

void
rfs4_global_init(void)
{
	fem_t *rdops = NULL;
	fem_t *wrops = NULL;

	if (fem_create("deleg_rdops", nfs4_rd_deleg_tmpl, &rdops) ||
	    fem_create("deleg_wrops", nfs4_wr_deleg_tmpl, &wrops)) {
		if (rdops) {
			fem_free(rdops);
			rdops = NULL;
		}
		if (wrops) {
			fem_free(wrops);
			wrops = NULL;
		}
	}
	rfs.rg_v4.rg4_deleg_rdops = rdops;
	rfs.rg_v4.rg4_deleg_wrops = wrops;

	rfs.rg_v4.rg4_caller_id = fs_new_caller_id();

	vsd_create(&rfs.rg_v4.rg4_vkey, NULL);

	rfs.rg_v4.rg4_lockt_sysid = lm_alloc_sysidt();
	rfs.rg_v4.rg4_readdir4verf = 0;
	rfs.rg_v4.rg4_write4verf = 0;

	rfs4_ntov_init();
	rfs4_client_clrst = rfs4_clear_client_state;
	nfs_set_nodeid = rfs4_set_nodeid;

	/*
	 * Initialize the table in the client kmod to point to the
	 * server's functions.  This avoids direct client to server calls.
	 */
	nfs4_mig_tab[NFS4_MIG_FREEZE-NFS4_MIG_FIRST] = rfs4_migop_freeze;
	nfs4_mig_tab[NFS4_MIG_GRACE-NFS4_MIG_FIRST] = rfs4_migop_grace;
	nfs4_mig_tab[NFS4_MIG_THAW-NFS4_MIG_FIRST] = rfs4_migop_thaw;
	nfs4_mig_tab[NFS4_MIG_HARVEST-NFS4_MIG_FIRST] = rfs4_migop_harvest;
	nfs4_mig_tab[NFS4_MIG_HYDRATE-NFS4_MIG_FIRST] = rfs4_migop_hydrate;
	nfs4_mig_tab[NFS4_MIG_CONVERT-NFS4_MIG_FIRST] = rfs4_migop_convert;
	nfs4_mig_tab[NFS4_MIG_UNCONVERT-NFS4_MIG_FIRST] = rfs4_migop_unconvert;
	nfs4_mig_tab[NFS4_MIG_STATUS-NFS4_MIG_FIRST] = rfs4_migop_status;

}

void
rfs4_global_fini(void)
{
	rfs4_client_clrst = NULL;

	lm_free_sysidt(rfs.rg_v4.rg4_lockt_sysid);

	vsd_destroy(&rfs.rg_v4.rg4_vkey);

	rfs.rg_v4.rg4_caller_id = 0;

	if (rfs.rg_v4.rg4_deleg_rdops) {
		fem_free(rfs.rg_v4.rg4_deleg_rdops);
		rfs.rg_v4.rg4_deleg_rdops = NULL;
	}
	if (rfs.rg_v4.rg4_deleg_wrops) {
		fem_free(rfs.rg_v4.rg4_deleg_wrops);
		rfs.rg_v4.rg4_deleg_wrops = NULL;
	}
}

void
rfs4_srvrinit(rfs_inst_t *rip)
{
	rfs4_attr_init(rip);
	mutex_init(&rip->ri_v4.r4_deleg_lock, NULL, MUTEX_DEFAULT, NULL);

	rip->ri_v4.r4_deleg_policy = SRV_NEVER_DELEGATE;

	/* Used to manage access to grace instance linked list */
	mutex_init(&rip->ri_v4.r4_grace_lock, NULL, MUTEX_DEFAULT, NULL);

	/* Used to manage access to rfs4_deleg_policy */
	rw_init(&rip->ri_v4.r4_deleg_policy_lock, NULL, RW_DEFAULT, NULL);

	if ((rfs.rg_v4.rg4_deleg_rdops == NULL) ||
	    (rfs.rg_v4.rg4_deleg_wrops == NULL))
		rfs4_disable_delegation(rip);

	rip->ri_v4.r4_drc_max = 8 * 1024;
	rip->ri_v4.r4_drc_hash = 541;
}

void
rfs4_srvrfini(rfs_inst_t *rip)
{
	mutex_destroy(&rip->ri_v4.r4_grace_lock);
	mutex_destroy(&rip->ri_v4.r4_deleg_lock);
	rw_destroy(&rip->ri_v4.r4_deleg_policy_lock);
}

void
rfs4_init_compound_state(struct compound_state *cs)
{
	bzero(cs, sizeof (*cs));
	cs->cont = TRUE;
	cs->access = CS_ACCESS_DENIED;
	cs->deleg = FALSE;
	cs->mandlock = FALSE;
	cs->fh.nfs_fh4_val = cs->fhbuf;
}

void
rfs4_grace_start(rfs4_grace_t *sip)
{
	rw_enter(&sip->rg_rwlock, RW_WRITER);
	sip->rg_start_time = (time_t)TICK_TO_SEC(ddi_get_lbolt());
	sip->rg_grace_period = rfs4_grace_period;
	rw_exit(&sip->rg_rwlock);
}

/*
 * returns true if the instance's grace period has never been started
 */
int
rfs4_grace_new(rfs4_grace_t *sip)
{
	time_t start_time;

	rw_enter(&sip->rg_rwlock, RW_READER);
	start_time = sip->rg_start_time;
	rw_exit(&sip->rg_rwlock);

	return (start_time == 0);
}

/*
 * Indicates if server instance is within the
 * grace period.
 */
int
rfs4_grace_in(rfs4_grace_t *sip)
{
	time_t grace_expiry;

	rw_enter(&sip->rg_rwlock, RW_READER);
	grace_expiry = sip->rg_start_time + sip->rg_grace_period;
	rw_exit(&sip->rg_rwlock);

	return (((time_t)TICK_TO_SEC(ddi_get_lbolt())) < grace_expiry);
}

/*
 * Mark an individual filesystem as being in a grace period.
 */
void
rfs4_fse_grace_start(rfs_inst_t *rip, fsh_entry_t *fse)
{
	mutex_enter(&fse->fse_lock);
	fse->fse_start_time = (time_t)TICK_TO_SEC(ddi_get_lbolt());
	fse->fse_grace_period = rip->ri_v4.r4_cur_grace->rg_grace_period;
	mutex_exit(&fse->fse_lock);
}

/*
 * Indicates if an individual filesystem is within the
 * grace period.
 */
int
rfs4_fse_in_grace(fsh_entry_t *fse)
{
	time_t grace_expiry;

	ASSERT(fse);
	mutex_enter(&fse->fse_lock);
	grace_expiry = fse->fse_start_time + fse->fse_grace_period;
	mutex_exit(&fse->fse_lock);

	return (((time_t)TICK_TO_SEC(ddi_get_lbolt())) < grace_expiry);
}

int
rfs4_in_grace(rfs4_client_t *cp, struct compound_state *cs)
{
	ASSERT(rfs4_dbe_refcnt(cp->rc_dbe) > 0);

	if (rfs4_do_tsm == 0)
		return (rfs4_fse_in_grace(cs->fse) ||
		    rfs4_grace_in(cp->rc_grace));
	else
		return (rfs4_grace_in(cp->rc_grace));
}

/*
 * reset all currently active grace periods
 */
void
rfs4_inst_grace_reset(rfs_inst_t *rip)
{
	rfs4_grace_t *sip;

	mutex_enter(&rip->ri_v4.r4_grace_lock);
	for (sip = rip->ri_v4.r4_cur_grace; sip != NULL; sip = sip->rg_prev)
		if (rfs4_grace_in(sip))
			rfs4_grace_start(sip);
	mutex_exit(&rip->ri_v4.r4_grace_lock);
}

/*
 * start any new instances' grace periods
 */
void
rfs4_inst_grace_start(rfs_inst_t *rip)
{
	rfs4_grace_t *sip;

	mutex_enter(&rip->ri_v4.r4_grace_lock);
	for (sip = rip->ri_v4.r4_cur_grace; sip != NULL; sip = sip->rg_prev)
		if (rfs4_grace_new(sip))
			rfs4_grace_start(sip);
	mutex_exit(&rip->ri_v4.r4_grace_lock);
}

static rfs4_dss_path_t *
rfs4_dss_newpath(rfs_inst_t *rip, rfs4_grace_t *sip, char *path,
    unsigned index)
{
	rfs4_inst_t *vip = &rip->ri_v4;
	size_t len;
	rfs4_dss_path_t *dss_path;

	dss_path = kmem_alloc(sizeof (rfs4_dss_path_t), KM_SLEEP);

	/*
	 * Take a copy of the string, since the original may be overwritten.
	 * Sadly, no strdup() in the kernel.
	 */
	/* allow for NUL */
	len = strlen(path) + 1;
	dss_path->ds_path = kmem_alloc(len, KM_SLEEP);
	(void) strlcpy(dss_path->ds_path, path, len);

	/* associate with grace */
	dss_path->ds_sip = sip;
	dss_path->ds_index = index;

	/*
	 * Add to list of served paths.
	 * No locking required, as we're only ever called at startup.
	 */
	if (vip->r4_dss_pathlist == NULL) {
		/* this is the first dss_path_t */

		/* needed for insque/remque */
		dss_path->ds_next = dss_path->ds_prev = dss_path;

		vip->r4_dss_pathlist = dss_path;
	} else {
		insque(dss_path, vip->r4_dss_pathlist);
	}

	return (dss_path);
}

/*
 * Create a new server instance, and make it the currently active instance.
 * Note that starting the grace period too early will reduce the clients'
 * recovery window.
 */
void
rfs4_grace_create(rfs_inst_t *rip, int start_grace, int dss_npaths,
    char **dss_paths)
{
	unsigned i;
	rfs4_grace_t *sip;
	rfs4_oldstate_t *oldstate;

	sip = kmem_alloc(sizeof (rfs4_grace_t), KM_SLEEP);
	rw_init(&sip->rg_rwlock, NULL, RW_DEFAULT, NULL);

	sip->rg_start_time = (time_t)0;
	sip->rg_grace_period = (time_t)0;
	sip->rg_next = NULL;
	sip->rg_prev = NULL;

	rw_init(&sip->rg_oldstate_lock, NULL, RW_DEFAULT, NULL);
	/*
	 * This initial dummy entry is required to setup for insque/remque.
	 * It must be skipped over whenever the list is traversed.
	 * insque/remque require initial list entry to be self-terminated.
	 */
	oldstate = &sip->rg_oldstate;
	oldstate->next = oldstate;
	oldstate->prev = oldstate;

	sip->rg_dss_npaths = dss_npaths;
	sip->rg_dss_paths = kmem_alloc(dss_npaths *
	    sizeof (rfs4_dss_path_t *), KM_SLEEP);

	for (i = 0; i < dss_npaths; i++) {
		sip->rg_dss_paths[i] = rfs4_dss_newpath(rip, sip,
		    dss_paths[i], i);
	}

	mutex_enter(&rip->ri_v4.r4_grace_lock);
	if (rip->ri_v4.r4_cur_grace != NULL) {
		/* add to linked list */
		sip->rg_prev = rip->ri_v4.r4_cur_grace;
		rip->ri_v4.r4_cur_grace->rg_next = sip;
	}
	if (start_grace)
		rfs4_grace_start(sip);
	/* make the new instance "current" */
	rip->ri_v4.r4_cur_grace = sip;

	mutex_exit(&rip->ri_v4.r4_grace_lock);
}

/*
 * In future, we might add a rfs4_grace_destroy(sip) but, for now, destroy
 * all instances directly.
 */
void
rfs4_grace_destroy_all(rfs_inst_t *rip)
{
	rfs4_grace_t *sip, *prev, *current;
	rfs4_dss_path_t *dss_path;
	int xx;

	mutex_enter(&rip->ri_v4.r4_grace_lock);
	ASSERT(rip->ri_v4.r4_cur_grace != NULL);
	current = rip->ri_v4.r4_cur_grace;
	rip->ri_v4.r4_cur_grace = NULL;
	for (sip = current; sip != NULL; sip = prev) {
		prev = sip->rg_prev;
		rw_destroy(&sip->rg_rwlock);
		rw_destroy(&sip->rg_oldstate_lock);
		for (xx = 0; xx < sip->rg_dss_npaths; xx++) {
			dss_path = sip->rg_dss_paths[xx];
			if (dss_path == NULL)
				continue;
			kmem_free(dss_path->ds_path,
			    strlen(dss_path->ds_path) + 1);
			kmem_free(dss_path, sizeof (rfs4_dss_path_t));
		}
		if (sip->rg_dss_paths)
			kmem_free(sip->rg_dss_paths,
			    sip->rg_dss_npaths * sizeof (rfs4_dss_path_t *));
		kmem_free(sip, sizeof (rfs4_grace_t));
	}
	mutex_exit(&rip->ri_v4.r4_grace_lock);
}

/*
 * Assign the current server instance to a client_t.
 * Should be called with cp->rc_dbe held.
 */
void
rfs4_clnt_grace_assign(rfs4_client_t *cp, rfs4_grace_t *sip)
{
	rfs_inst_t *rip = rfs4_dbe_rip(cp->rc_dbe);
	ASSERT(rfs4_dbe_refcnt(cp->rc_dbe) > 0);

	/*
	 * The lock ensures that if the current instance is in the process
	 * of changing, we will see the new one.
	 */
	mutex_enter(&rip->ri_v4.r4_grace_lock);
	cp->rc_grace = sip;
	mutex_exit(&rip->ri_v4.r4_grace_lock);
}

rfs4_grace_t *
rfs4_clnt_grace(rfs4_client_t *cp)
{
	ASSERT(rfs4_dbe_refcnt(cp->rc_dbe) > 0);

	return (cp->rc_grace);
}

/* ARGSUSED */
static void
nullfree(caddr_t resop)
{
}

/*
 * This is a fall-through for invalid or not implemented (yet) ops
 */
/* ARGSUSED */
static void
rfs4_op_inval(nfs_argop4 *argop, nfs_resop4 *resop, struct svc_req *req,
	struct compound_state *cs)
{
	*cs->statusp = *((nfsstat4 *)&(resop)->nfs_resop4_u) = NFS4ERR_INVAL;
}

/*
 * Check if the security flavor, nfsnum, is in the flavor_list.
 */
bool_t
in_flavor_list(int nfsnum, int *flavor_list, int count)
{
	int i;

	for (i = 0; i < count; i++) {
		if (nfsnum == flavor_list[i])
			return (TRUE);
	}
	return (FALSE);
}

/*
 * Used by rfs4_op_secinfo to get the security information from the
 * export structure associated with the component.
 */
/* ARGSUSED */
static nfsstat4
do_rfs4_op_secinfo(struct compound_state *cs, char *nm, SECINFO4res *resp)
{
	int error = 0, different_export = 0;
	vnode_t *dvp, *vp = NULL;
	struct exportinfo *exi = NULL;
	fid_t fid;
	uint_t count, i;
	secinfo4 *resok_val;
	struct secinfo *secp;
	seconfig_t *si;
	bool_t did_traverse = FALSE;
	int dotdot, walk;

	dvp = cs->vp;
	VN_HOLD(dvp);
	dotdot = (nm[0] == '.' && nm[1] == '.' && nm[2] == '\0');

	/*
	 * If dotdotting, then need to check whether it's above the
	 * root of a filesystem, or above an export point.
	 */
	if (dotdot) {

		/*
		 * If dotdotting at the root of a filesystem, then
		 * need to traverse back to the mounted-on filesystem
		 * and do the dotdot lookup there.
		 */
		if (cs->vp->v_flag & VROOT) {

			/*
			 * If at the zone/instance root, then
			 * go up no further.
			 */
			if (VN_CMP(dvp, RFS_INST_ROOTVP(cs->rip))) {
				error = ENOENT;
				goto out;
			}

			/*
			 * Traverse back to the mounted-on filesystem
			 */
			dvp = vn_under(dvp);

			/*
			 * Set the different_export flag so we remember
			 * to pick up a new exportinfo entry for
			 * this new filesystem.
			 */
			different_export = 1;
		} else {

			/*
			 * If dotdotting above an export point then set
			 * the different_export to get new export info.
			 */
			different_export = nfs_exported(cs->exi, dvp);
		}
	}

	/*
	 * Get the vnode for the component "nm".
	 */
	error = VOP_LOOKUP(dvp, nm, &vp, NULL, 0, NULL, cs->cr,
	    NULL, NULL, NULL);
	if (error)
		goto out;

	/*
	 * If the vnode is in a pseudo filesystem, or if the security flavor
	 * used in the request is valid but not an explicitly shared flavor,
	 * or the access bit indicates that this is a limited access,
	 * check whether this vnode is visible.
	 */
	if (!different_export &&
	    (PSEUDO(cs->exi) || ! is_exported_sec(cs->nfsflavor, cs->exi) ||
	    cs->access & CS_ACCESS_LIMITED)) {
		if (! nfs_visible(cs->exi, vp, &different_export)) {
			error = ENOENT;
			goto out;
		}
	}

	/*
	 * If it's a mountpoint, then traverse it.
	 */
	if (vn_ismntpt(vp)) {
		if ((error = traverse(&vp)) != 0)
			goto out;

		/* remember that we had to traverse mountpoint */
		did_traverse = TRUE;
		different_export = 1;
	} else if (vp->v_vfsp != dvp->v_vfsp) {
		/*
		 * If vp isn't a mountpoint and the vfs ptrs aren't the same,
		 * then vp is probably an LOFS object.  We don't need the
		 * realvp, we just need to know that we might have crossed
		 * a server fs boundary and need to call checkexport4.
		 * (LOFS lookup hides server fs mountpoints, and actually calls
		 * traverse)
		 */
		different_export = 1;
	}

	/*
	 * Get the export information for it.
	 */
	if (different_export) {

		bzero(&fid, sizeof (fid));
		fid.fid_len = MAXFIDSZ;
		if ((error = vop_fid_pseudo(vp, &fid)) != 0)
			goto out;

		if (dotdot)
			exi = nfs_vptoexi(cs->rip, NULL, vp, cs->cr, &walk,
			    NULL, TRUE);
		else
			exi = checkexport4(cs->rip, &vp->v_vfsp->vfs_fsid,
			    &fid, vp);

		if (exi == NULL) {
			if (!did_traverse) {
				error = EACCES;
				goto out;
			}
			/*
			 * If this vnode is a mounted-on vnode,
			 * but the mounted-on file system is not
			 * exported, send back the secinfo for
			 * the exported node that the mounted-on
			 * vnode lives in.
			 */
			exi = cs->exi;
		}
	} else {
		exi = cs->exi;
	}
	ASSERT(exi != NULL);


	/*
	 * Create the secinfo result based on the security information
	 * from the exportinfo structure (exi).
	 *
	 * Return all flavors for a pseudo node.
	 * For a real export node, return the flavor that the client
	 * has access with.
	 */
	ASSERT(RW_LOCK_HELD(&exi->exi_rip->ri_nm.rn_export_rwlock));
	if (PSEUDO(exi)) {
		count = exi->exi_export.ex_seccnt; /* total sec count */
		resok_val = kmem_alloc(count * sizeof (secinfo4), KM_SLEEP);
		secp = exi->exi_export.ex_secinfo;

		for (i = 0; i < count; i++) {
			si = &secp[i].s_secinfo;
			resok_val[i].flavor = si->sc_rpcnum;
			if (resok_val[i].flavor == RPCSEC_GSS) {
				rpcsec_gss_info *info;

				info = &resok_val[i].flavor_info;
				info->qop = si->sc_qop;
				info->service = (rpc_gss_svc_t)si->sc_service;

				/* get oid opaque data */
				info->oid.sec_oid4_len =
				    si->sc_gss_mech_type->length;
				info->oid.sec_oid4_val = kmem_alloc(
				    si->sc_gss_mech_type->length, KM_SLEEP);
				bcopy(
				    si->sc_gss_mech_type->elements,
				    info->oid.sec_oid4_val,
				    info->oid.sec_oid4_len);
			}
		}
		resp->SECINFO4resok_len = count;
		resp->SECINFO4resok_val = resok_val;
	} else {
		int ret_cnt = 0, k = 0;
		int *flavor_list;

		count = exi->exi_export.ex_seccnt; /* total sec count */
		secp = exi->exi_export.ex_secinfo;

		flavor_list = kmem_alloc(count * sizeof (int), KM_SLEEP);
		/* find out which flavors to return */
		for (i = 0; i < count; i ++) {
			int access, flavor, perm;

			flavor = secp[i].s_secinfo.sc_nfsnum;
			perm = secp[i].s_flags;

			access = nfsauth4_secinfo_access(exi, cs->req,
			    flavor, perm);

			if (! (access & NFSAUTH_DENIED) &&
			    ! (access & NFSAUTH_WRONGSEC)) {
				flavor_list[ret_cnt] = flavor;
				ret_cnt++;
			}
		}

		/* Create the returning SECINFO value */
		resok_val = kmem_alloc(ret_cnt * sizeof (secinfo4), KM_SLEEP);

		for (i = 0; i < count; i++) {
			/*
			 * If the flavor is in the flavor list,
			 * fill in resok_val.
			 */
			si = &secp[i].s_secinfo;
			if (in_flavor_list(si->sc_nfsnum,
			    flavor_list, ret_cnt)) {
				resok_val[k].flavor = si->sc_rpcnum;
				if (resok_val[k].flavor == RPCSEC_GSS) {
					rpcsec_gss_info *info;

					info = &resok_val[k].flavor_info;
					info->qop = si->sc_qop;
					info->service = (rpc_gss_svc_t)
					    si->sc_service;

					/* get oid opaque data */
					info->oid.sec_oid4_len =
					    si->sc_gss_mech_type->length;
					info->oid.sec_oid4_val = kmem_alloc(
					    si->sc_gss_mech_type->length,
					    KM_SLEEP);
					bcopy(si->sc_gss_mech_type->elements,
					    info->oid.sec_oid4_val,
					    info->oid.sec_oid4_len);
				}
				k++;
			}
			if (k >= ret_cnt)
				break;
		}
		resp->SECINFO4resok_len = ret_cnt;
		resp->SECINFO4resok_val = resok_val;
		kmem_free(flavor_list, count * sizeof (int));
	}

out:
	if (vp)
		VN_RELE(vp);
	VN_RELE(dvp);
	return (puterrno4(error));
}

/*
 * SECINFO (Operation 33): Obtain required security information on
 * the component name in the format of (security-mechanism-oid, qop, service)
 * triplets.
 */
/* ARGSUSED */
static void
rfs4_op_secinfo(nfs_argop4 *argop, nfs_resop4 *resop, struct svc_req *req,
    struct compound_state *cs)
{
	SECINFO4args *args = &argop->nfs_argop4_u.opsecinfo;
	SECINFO4res *resp = &resop->nfs_resop4_u.opsecinfo;
	utf8string *utfnm = &args->name;
	uint_t len;
	char *nm;
	struct sockaddr *ca;
	char *name = NULL;

	DTRACE_NFSV4_2(op__secinfo__start, struct compound_state *, cs,
	    SECINFO4args *, args);

	/*
	 * Current file handle (cfh) should have been set before getting
	 * into this function. If not, return error.
	 */
	if (cs->vp == NULL) {
		*cs->statusp = resp->status = NFS4ERR_NOFILEHANDLE;
		goto out;
	}

	if (cs->vp->v_type != VDIR) {
		*cs->statusp = resp->status = NFS4ERR_NOTDIR;
		goto out;
	}

	/*
	 * Verify the component name. If failed, error out, but
	 * do not error out if the component name is a "..".
	 * SECINFO will return its parents secinfo data for SECINFO "..".
	 */
	if (!utf8_dir_verify(utfnm)) {
		if (utfnm->utf8string_len != 2 ||
		    utfnm->utf8string_val[0] != '.' ||
		    utfnm->utf8string_val[1] != '.') {
			*cs->statusp = resp->status = NFS4ERR_INVAL;
			goto out;
		}
	}

	nm = utf8_to_str(utfnm, &len, NULL);
	if (nm == NULL) {
		*cs->statusp = resp->status = NFS4ERR_INVAL;
		goto out;
	}

	if (len > MAXNAMELEN) {
		*cs->statusp = resp->status = NFS4ERR_NAMETOOLONG;
		kmem_free(nm, len);
		goto out;
	}

	ca = (struct sockaddr *)svc_getrpccaller(req->rq_xprt)->buf;
	name = nfscmd_convname(ca, cs->exi, nm, NFSCMD_CONV_INBOUND,
	    MAXPATHLEN  + 1);

	if (name == NULL) {
		*cs->statusp = resp->status = NFS4ERR_INVAL;
		kmem_free(nm, len);
		goto out;
	}


	*cs->statusp = resp->status = do_rfs4_op_secinfo(cs, name, resp);

	if (name != nm)
		kmem_free(name, MAXPATHLEN + 1);
	kmem_free(nm, len);

out:
	DTRACE_NFSV4_2(op__secinfo__done, struct compound_state *, cs,
	    SECINFO4res *, resp);
}

/*
 * Free SECINFO result.
 */
/* ARGSUSED */
static void
rfs4_op_secinfo_free(nfs_resop4 *resop)
{
	SECINFO4res *resp = &resop->nfs_resop4_u.opsecinfo;
	int count, i;
	secinfo4 *resok_val;

	/* If this is not an Ok result, nothing to free. */
	if (resp->status != NFS4_OK) {
		return;
	}

	count = resp->SECINFO4resok_len;
	resok_val = resp->SECINFO4resok_val;

	for (i = 0; i < count; i++) {
		if (resok_val[i].flavor == RPCSEC_GSS) {
			rpcsec_gss_info *info;

			info = &resok_val[i].flavor_info;
			kmem_free(info->oid.sec_oid4_val,
			    info->oid.sec_oid4_len);
		}
	}
	kmem_free(resok_val, count * sizeof (secinfo4));
	resp->SECINFO4resok_len = 0;
	resp->SECINFO4resok_val = NULL;
}

/* ARGSUSED */
static void
rfs4_op_access(nfs_argop4 *argop, nfs_resop4 *resop, struct svc_req *req,
    struct compound_state *cs)
{
	ACCESS4args *args = &argop->nfs_argop4_u.opaccess;
	ACCESS4res *resp = &resop->nfs_resop4_u.opaccess;
	int error;
	vnode_t *vp;
	struct vattr va;
	int checkwriteperm;
	cred_t *cr = cs->cr;
	bslabel_t *clabel, *slabel;
	ts_label_t *tslabel;
	boolean_t admin_low_client;

	DTRACE_NFSV4_2(op__access__start, struct compound_state *, cs,
	    ACCESS4args *, args);

#if 0	/* XXX allow access even if !cs->access. Eventually only pseudo fs */
	if (cs->access == CS_ACCESS_DENIED) {
		*cs->statusp = resp->status = NFS4ERR_ACCESS;
		goto out;
	}
#endif
	if (cs->vp == NULL) {
		*cs->statusp = resp->status = NFS4ERR_NOFILEHANDLE;
		goto out;
	}

	ASSERT(cr != NULL);

	vp = cs->vp;

	/*
	 * If the file system is exported read only, it is not appropriate
	 * to check write permissions for regular files and directories.
	 * Special files are interpreted by the client, so the underlying
	 * permissions are sent back to the client for interpretation.
	 */
	if (rdonly4(cs->exi, cs->vp, req) &&
	    (vp->v_type == VREG || vp->v_type == VDIR))
		checkwriteperm = 0;
	else
		checkwriteperm = 1;

	/*
	 * XXX
	 * We need the mode so that we can correctly determine access
	 * permissions relative to a mandatory lock file.  Access to
	 * mandatory lock files is denied on the server, so it might
	 * as well be reflected to the server during the open.
	 */
	va.va_mask = AT_MODE;
	error = VOP_GETATTR(vp, &va, 0, cr, NULL);
	if (error) {
		*cs->statusp = resp->status = puterrno4(error);
		goto out;
	}
	resp->access = 0;
	resp->supported = 0;

	if (is_system_labeled()) {
		ASSERT(req->rq_label != NULL);
		clabel = req->rq_label;
		DTRACE_PROBE2(tx__rfs4__log__info__opaccess__clabel, char *,
		    "got client label from request(1)",
		    struct svc_req *, req);
		if (!blequal(&l_admin_low->tsl_label, clabel)) {
			if ((tslabel = nfs_getflabel(vp, cs->exi)) == NULL) {
				*cs->statusp = resp->status = puterrno4(EACCES);
				goto out;
			}
			slabel = label2bslabel(tslabel);
			DTRACE_PROBE3(tx__rfs4__log__info__opaccess__slabel,
			    char *, "got server label(1) for vp(2)",
			    bslabel_t *, slabel, vnode_t *, vp);

			admin_low_client = B_FALSE;
		} else
			admin_low_client = B_TRUE;
	}

	if (args->access & ACCESS4_READ) {
		error = VOP_ACCESS(vp, VREAD, 0, cr, NULL);
		if (!error && !MANDLOCK(vp, va.va_mode) &&
		    (!is_system_labeled() || admin_low_client ||
		    bldominates(clabel, slabel)))
			resp->access |= ACCESS4_READ;
		resp->supported |= ACCESS4_READ;
	}
	if ((args->access & ACCESS4_LOOKUP) && vp->v_type == VDIR) {
		error = VOP_ACCESS(vp, VEXEC, 0, cr, NULL);
		if (!error && (!is_system_labeled() || admin_low_client ||
		    bldominates(clabel, slabel)))
			resp->access |= ACCESS4_LOOKUP;
		resp->supported |= ACCESS4_LOOKUP;
	}
	if (checkwriteperm &&
	    (args->access & (ACCESS4_MODIFY|ACCESS4_EXTEND))) {
		error = VOP_ACCESS(vp, VWRITE, 0, cr, NULL);
		if (!error && !MANDLOCK(vp, va.va_mode) &&
		    (!is_system_labeled() || admin_low_client ||
		    blequal(clabel, slabel)))
			resp->access |=
			    (args->access & (ACCESS4_MODIFY | ACCESS4_EXTEND));
		resp->supported |= (ACCESS4_MODIFY | ACCESS4_EXTEND);
	}

	if (checkwriteperm &&
	    (args->access & ACCESS4_DELETE) && vp->v_type == VDIR) {
		error = VOP_ACCESS(vp, VWRITE, 0, cr, NULL);
		if (!error && (!is_system_labeled() || admin_low_client ||
		    blequal(clabel, slabel)))
			resp->access |= ACCESS4_DELETE;
		resp->supported |= ACCESS4_DELETE;
	}
	if (args->access & ACCESS4_EXECUTE && vp->v_type != VDIR) {
		error = VOP_ACCESS(vp, VEXEC, 0, cr, NULL);
		if (!error && !MANDLOCK(vp, va.va_mode) &&
		    (!is_system_labeled() || admin_low_client ||
		    bldominates(clabel, slabel)))
			resp->access |= ACCESS4_EXECUTE;
		resp->supported |= ACCESS4_EXECUTE;
	}

	if (is_system_labeled() && !admin_low_client)
		label_rele(tslabel);

	*cs->statusp = resp->status = NFS4_OK;
out:
	DTRACE_NFSV4_2(op__access__done, struct compound_state *, cs,
	    ACCESS4res *, resp);
}

/* ARGSUSED */
static void
rfs4_op_commit(nfs_argop4 *argop, nfs_resop4 *resop, struct svc_req *req,
    struct compound_state *cs)
{
	COMMIT4args *args = &argop->nfs_argop4_u.opcommit;
	COMMIT4res *resp = &resop->nfs_resop4_u.opcommit;
	int error;
	vnode_t *vp = cs->vp;
	cred_t *cr = cs->cr;
	vattr_t va;

	DTRACE_NFSV4_2(op__commit__start, struct compound_state *, cs,
	    COMMIT4args *, args);

	if (vp == NULL) {
		*cs->statusp = resp->status = NFS4ERR_NOFILEHANDLE;
		goto out;
	}
	if (cs->access == CS_ACCESS_DENIED) {
		*cs->statusp = resp->status = NFS4ERR_ACCESS;
		goto out;
	}

	if (args->offset + args->count < args->offset) {
		*cs->statusp = resp->status = NFS4ERR_INVAL;
		goto out;
	}

	va.va_mask = AT_UID;
	error = VOP_GETATTR(vp, &va, 0, cr, NULL);

	/*
	 * If we can't get the attributes, then we can't do the
	 * right access checking.  So, we'll fail the request.
	 */
	if (error) {
		*cs->statusp = resp->status = puterrno4(error);
		goto out;
	}
	if (rdonly4(cs->exi, cs->vp, req)) {
		*cs->statusp = resp->status = NFS4ERR_ROFS;
		goto out;
	}

	if (vp->v_type != VREG) {
		if (vp->v_type == VDIR)
			resp->status = NFS4ERR_ISDIR;
		else
			resp->status = NFS4ERR_INVAL;
		*cs->statusp = resp->status;
		goto out;
	}

	if (crgetuid(cr) != va.va_uid &&
	    (error = VOP_ACCESS(vp, VWRITE, 0, cs->cr, NULL))) {
		*cs->statusp = resp->status = puterrno4(error);
		goto out;
	}

	error = VOP_FSYNC(vp, FSYNC, cr, NULL);

	if (error) {
		*cs->statusp = resp->status = puterrno4(error);
		goto out;
	}

	*cs->statusp = resp->status = NFS4_OK;
	if (cs->fse->fse_wr4verf == 0)
		resp->writeverf = rfs.rg_v4.rg4_write4verf;
	else
		resp->writeverf = cs->fse->fse_wr4verf;
out:
	DTRACE_NFSV4_2(op__commit__done, struct compound_state *, cs,
	    COMMIT4res *, resp);
}

/*
 * rfs4_op_mknod is called from rfs4_op_create after all initial verification
 * was completed. It does the nfsv4 create for special files.
 */
/* ARGSUSED */
static vnode_t *
do_rfs4_op_mknod(CREATE4args *args, CREATE4res *resp, struct svc_req *req,
    struct compound_state *cs, vattr_t *vap, char *nm)
{
	int error;
	cred_t *cr = cs->cr;
	vnode_t *dvp = cs->vp;
	vnode_t *vp = NULL;
	int mode;
	enum vcexcl excl;

	switch (args->type) {
	case NF4CHR:
	case NF4BLK:
		if (secpolicy_sys_devices(cr) != 0) {
			*cs->statusp = resp->status = NFS4ERR_PERM;
			return (NULL);
		}
		if (args->type == NF4CHR)
			vap->va_type = VCHR;
		else
			vap->va_type = VBLK;
		vap->va_rdev = makedevice(args->ftype4_u.devdata.specdata1,
		    args->ftype4_u.devdata.specdata2);
		vap->va_mask |= AT_RDEV;
		break;
	case NF4SOCK:
		vap->va_type = VSOCK;
		break;
	case NF4FIFO:
		vap->va_type = VFIFO;
		break;
	default:
		*cs->statusp = resp->status = NFS4ERR_BADTYPE;
		return (NULL);
	}

	/*
	 * Must specify the mode.
	 */
	if (!(vap->va_mask & AT_MODE)) {
		*cs->statusp = resp->status = NFS4ERR_INVAL;
		return (NULL);
	}

	excl = EXCL;

	mode = 0;

	error = VOP_CREATE(dvp, nm, vap, excl, mode, &vp, cr, 0, NULL, NULL);
	if (error) {
		*cs->statusp = resp->status = puterrno4(error);
		return (NULL);
	}
	return (vp);
}

/*
 * nfsv4 create is used to create non-regular files. For regular files,
 * use nfsv4 open.
 */
/* ARGSUSED */
static void
rfs4_op_create(nfs_argop4 *argop, nfs_resop4 *resop, struct svc_req *req,
    struct compound_state *cs)
{
	CREATE4args *args = &argop->nfs_argop4_u.opcreate;
	CREATE4res *resp = &resop->nfs_resop4_u.opcreate;
	int error;
	struct vattr bva, iva, iva2, ava, *vap;
	cred_t *cr = cs->cr;
	vnode_t *dvp = cs->vp;
	vnode_t *vp = NULL;
	vnode_t *realvp;
	char *nm, *lnm;
	uint_t len, llen;
	int syncval = 0;
	struct nfs4_svgetit_arg sarg;
	struct nfs4_ntov_table ntov;
	struct statvfs64 sb;
	nfsstat4 status;
	struct sockaddr *ca;
	char *name = NULL;
	char *lname = NULL;

	DTRACE_NFSV4_2(op__create__start, struct compound_state *, cs,
	    CREATE4args *, args);

	resp->attrset = 0;

	if (dvp == NULL) {
		*cs->statusp = resp->status = NFS4ERR_NOFILEHANDLE;
		goto out;
	}

	/*
	 * If there is an unshared filesystem mounted on this vnode,
	 * do not allow to create an object in this directory.
	 */
	if (vn_ismntpt(dvp)) {
		*cs->statusp = resp->status = NFS4ERR_ACCESS;
		goto out;
	}

	/* Verify that type is correct */
	switch (args->type) {
	case NF4LNK:
	case NF4BLK:
	case NF4CHR:
	case NF4SOCK:
	case NF4FIFO:
	case NF4DIR:
		break;
	default:
		*cs->statusp = resp->status = NFS4ERR_BADTYPE;
		goto out;
	};

	if (cs->access == CS_ACCESS_DENIED) {
		*cs->statusp = resp->status = NFS4ERR_ACCESS;
		goto out;
	}
	if (dvp->v_type != VDIR) {
		*cs->statusp = resp->status = NFS4ERR_NOTDIR;
		goto out;
	}
	if (!utf8_dir_verify(&args->objname)) {
		*cs->statusp = resp->status = NFS4ERR_INVAL;
		goto out;
	}

	if (rdonly4(cs->exi, cs->vp, req)) {
		*cs->statusp = resp->status = NFS4ERR_ROFS;
		goto out;
	}

	/*
	 * Name of newly created object
	 */
	nm = utf8_to_fn(&args->objname, &len, NULL);
	if (nm == NULL) {
		*cs->statusp = resp->status = NFS4ERR_INVAL;
		goto out;
	}

	if (len > MAXNAMELEN) {
		*cs->statusp = resp->status = NFS4ERR_NAMETOOLONG;
		kmem_free(nm, len);
		goto out;
	}

	ca = (struct sockaddr *)svc_getrpccaller(req->rq_xprt)->buf;
	name = nfscmd_convname(ca, cs->exi, nm, NFSCMD_CONV_INBOUND,
	    MAXPATHLEN  + 1);

	if (name == NULL) {
		*cs->statusp = resp->status = NFS4ERR_INVAL;
		kmem_free(nm, len);
		goto out;
	}

	resp->attrset = 0;

	sarg.sbp = &sb;
	sarg.is_referral = B_FALSE;
	nfs4_ntov_table_init(&ntov);

	status = do_rfs4_set_attrs(&resp->attrset,
	    &args->createattrs, cs, &sarg, &ntov, NFS4ATTR_SETIT);

	if (sarg.vap->va_mask == 0 && status == NFS4_OK)
		status = NFS4ERR_INVAL;

	if (status != NFS4_OK) {
		*cs->statusp = resp->status = status;
		if (name != nm)
			kmem_free(name, MAXPATHLEN + 1);
		kmem_free(nm, len);
		nfs4_ntov_table_free(&ntov, &sarg);
		resp->attrset = 0;
		goto out;
	}

	/* Get "before" change value */
	bva.va_mask = AT_CTIME|AT_SEQ|AT_MODE;
	error = VOP_GETATTR(dvp, &bva, 0, cr, NULL);
	if (error) {
		*cs->statusp = resp->status = puterrno4(error);
		if (name != nm)
			kmem_free(name, MAXPATHLEN + 1);
		kmem_free(nm, len);
		nfs4_ntov_table_free(&ntov, &sarg);
		resp->attrset = 0;
		goto out;
	}
	NFS4_SET_FATTR4_CHANGE(resp->cinfo.before, bva.va_ctime)

	vap = sarg.vap;

	/*
	 * Set the default initial values for attributes when the parent
	 * directory does not have the VSUID/VSGID bit set and they have
	 * not been specified in createattrs.
	 */
	if (!(bva.va_mode & VSUID) && (vap->va_mask & AT_UID) == 0) {
		vap->va_uid = crgetuid(cr);
		vap->va_mask |= AT_UID;
	}
	if (!(bva.va_mode & VSGID) && (vap->va_mask & AT_GID) == 0) {
		vap->va_gid = crgetgid(cr);
		vap->va_mask |= AT_GID;
	}

	vap->va_mask |= AT_TYPE;
	switch (args->type) {
	case NF4DIR:
		vap->va_type = VDIR;
		if ((vap->va_mask & AT_MODE) == 0) {
			vap->va_mode = 0700;	/* default: owner rwx only */
			vap->va_mask |= AT_MODE;
		}
		error = VOP_MKDIR(dvp, name, vap, &vp, cr, NULL, 0, NULL);
		if (error)
			break;

		/*
		 * Get the initial "after" sequence number, if it fails,
		 * set to zero
		 */
		iva.va_mask = AT_SEQ;
		if (VOP_GETATTR(dvp, &iva, 0, cs->cr, NULL))
			iva.va_seq = 0;
		break;
	case NF4LNK:
		vap->va_type = VLNK;
		if ((vap->va_mask & AT_MODE) == 0) {
			vap->va_mode = 0700;	/* default: owner rwx only */
			vap->va_mask |= AT_MODE;
		}

		/*
		 * symlink names must be treated as data
		 */
		lnm = utf8_to_str(&args->ftype4_u.linkdata, &llen, NULL);

		if (lnm == NULL) {
			*cs->statusp = resp->status = NFS4ERR_INVAL;
			if (name != nm)
				kmem_free(name, MAXPATHLEN + 1);
			kmem_free(nm, len);
			nfs4_ntov_table_free(&ntov, &sarg);
			resp->attrset = 0;
			goto out;
		}

		if (llen > MAXPATHLEN) {
			*cs->statusp = resp->status = NFS4ERR_NAMETOOLONG;
			if (name != nm)
				kmem_free(name, MAXPATHLEN + 1);
			kmem_free(nm, len);
			kmem_free(lnm, llen);
			nfs4_ntov_table_free(&ntov, &sarg);
			resp->attrset = 0;
			goto out;
		}

		lname = nfscmd_convname(ca, cs->exi, lnm,
		    NFSCMD_CONV_INBOUND, MAXPATHLEN  + 1);

		if (lname == NULL) {
			*cs->statusp = resp->status = NFS4ERR_SERVERFAULT;
			if (name != nm)
				kmem_free(name, MAXPATHLEN + 1);
			kmem_free(nm, len);
			kmem_free(lnm, llen);
			nfs4_ntov_table_free(&ntov, &sarg);
			resp->attrset = 0;
			goto out;
		}

		error = VOP_SYMLINK(dvp, name, vap, lname, cr, NULL, 0);
		if (lname != lnm)
			kmem_free(lname, MAXPATHLEN + 1);
		kmem_free(lnm, llen);
		if (error)
			break;

		/*
		 * Get the initial "after" sequence number, if it fails,
		 * set to zero
		 */
		iva.va_mask = AT_SEQ;
		if (VOP_GETATTR(dvp, &iva, 0, cs->cr, NULL))
			iva.va_seq = 0;

		error = VOP_LOOKUP(dvp, name, &vp, NULL, 0, NULL, cr,
		    NULL, NULL, NULL);
		if (error)
			break;

		/*
		 * va_seq is not safe over VOP calls, check it again
		 * if it has changed zero out iva to force atomic = FALSE.
		 */
		iva2.va_mask = AT_SEQ;
		if (VOP_GETATTR(dvp, &iva2, 0, cs->cr, NULL) ||
		    iva2.va_seq != iva.va_seq)
			iva.va_seq = 0;
		break;
	default:
		/*
		 * probably a special file.
		 */
		if ((vap->va_mask & AT_MODE) == 0) {
			vap->va_mode = 0600;	/* default: owner rw only */
			vap->va_mask |= AT_MODE;
		}
		syncval = FNODSYNC;
		/*
		 * We know this will only generate one VOP call
		 */
		vp = do_rfs4_op_mknod(args, resp, req, cs, vap, name);

		if (vp == NULL) {
			if (name != nm)
				kmem_free(name, MAXPATHLEN + 1);
			kmem_free(nm, len);
			nfs4_ntov_table_free(&ntov, &sarg);
			resp->attrset = 0;
			goto out;
		}

		/*
		 * Get the initial "after" sequence number, if it fails,
		 * set to zero
		 */
		iva.va_mask = AT_SEQ;
		if (VOP_GETATTR(dvp, &iva, 0, cs->cr, NULL))
			iva.va_seq = 0;

		break;
	}
	if (name != nm)
		kmem_free(name, MAXPATHLEN + 1);
	kmem_free(nm, len);

	if (error) {
		*cs->statusp = resp->status = puterrno4(error);
	}

	/*
	 * Force modified data and metadata out to stable storage.
	 */
	(void) VOP_FSYNC(dvp, 0, cr, NULL);

	if (resp->status != NFS4_OK) {
		if (vp != NULL)
			VN_RELE(vp);
		nfs4_ntov_table_free(&ntov, &sarg);
		resp->attrset = 0;
		goto out;
	}

	/*
	 * Finish setup of cinfo response, "before" value already set.
	 * Get "after" change value, if it fails, simply return the
	 * before value.
	 */
	ava.va_mask = AT_CTIME|AT_SEQ;
	if (VOP_GETATTR(dvp, &ava, 0, cr, NULL)) {
		ava.va_ctime = bva.va_ctime;
		ava.va_seq = 0;
	}
	NFS4_SET_FATTR4_CHANGE(resp->cinfo.after, ava.va_ctime);

	/*
	 * True verification that object was created with correct
	 * attrs is impossible.  The attrs could have been changed
	 * immediately after object creation.  If attributes did
	 * not verify, the only recourse for the server is to
	 * destroy the object.  Maybe if some attrs (like gid)
	 * are set incorrectly, the object should be destroyed;
	 * however, seems bad as a default policy.  Do we really
	 * want to destroy an object over one of the times not
	 * verifying correctly?  For these reasons, the server
	 * currently sets bits in attrset for createattrs
	 * that were set; however, no verification is done.
	 *
	 * vmask_to_nmask accounts for vattr bits set on create
	 *	[do_rfs4_set_attrs() only sets resp bits for
	 *	 non-vattr/vfs bits.]
	 * Mask off any bits set by default so as not to return
	 * more attrset bits than were requested in createattrs
	 */
	nfs4_vmask_to_nmask(sarg.vap->va_mask, &resp->attrset);
	resp->attrset &= args->createattrs.attrmask;
	nfs4_ntov_table_free(&ntov, &sarg);

	error = makefh4(&cs->fh, vp, cs->exi);
	if (error) {
		*cs->statusp = resp->status = puterrno4(error);
	}

	/*
	 * The cinfo.atomic = TRUE only if we got no errors, we have
	 * non-zero va_seq's, and it has incremented by exactly one
	 * during the creation and it didn't change during the VOP_LOOKUP
	 * or VOP_FSYNC.
	 */
	if (!error && bva.va_seq && iva.va_seq && ava.va_seq &&
	    iva.va_seq == (bva.va_seq + 1) && iva.va_seq == ava.va_seq)
		resp->cinfo.atomic = TRUE;
	else
		resp->cinfo.atomic = FALSE;

	/*
	 * Force modified metadata out to stable storage.
	 *
	 * if a underlying vp exists, pass it to VOP_FSYNC
	 */
	if (VOP_REALVP(vp, &realvp, NULL) == 0)
		(void) VOP_FSYNC(realvp, syncval, cr, NULL);
	else
		(void) VOP_FSYNC(vp, syncval, cr, NULL);

	if (resp->status != NFS4_OK) {
		VN_RELE(vp);
		goto out;
	}
	if (cs->vp)
		VN_RELE(cs->vp);

	cs->vp = vp;
	*cs->statusp = resp->status = NFS4_OK;
out:
	DTRACE_NFSV4_2(op__create__done, struct compound_state *, cs,
	    CREATE4res *, resp);
}

/*ARGSUSED*/
static void
rfs4_op_delegpurge(nfs_argop4 *argop, nfs_resop4 *resop, struct svc_req *req,
    struct compound_state *cs)
{
	DTRACE_NFSV4_2(op__delegpurge__start, struct compound_state *, cs,
	    DELEGPURGE4args *, &argop->nfs_argop4_u.opdelegpurge);

	rfs4_op_inval(argop, resop, req, cs);

	DTRACE_NFSV4_2(op__delegpurge__done, struct compound_state *, cs,
	    DELEGPURGE4res *, &resop->nfs_resop4_u.opdelegpurge);
}

/*ARGSUSED*/
static void
rfs4_op_delegreturn(nfs_argop4 *argop, nfs_resop4 *resop, struct svc_req *req,
    struct compound_state *cs)
{
	DELEGRETURN4args *args = &argop->nfs_argop4_u.opdelegreturn;
	DELEGRETURN4res *resp = &resop->nfs_resop4_u.opdelegreturn;
	rfs4_deleg_state_t *dsp;
	nfsstat4 status;

	DTRACE_NFSV4_2(op__delegreturn__start, struct compound_state *, cs,
	    DELEGRETURN4args *, args);

	/*
	 * No op_putfh is made, so there is no vp, exi, or fse.
	 * This is an illegal compound and shouldn't happen.
	 */
	if (cs->vp == NULL) {
		*cs->statusp = resp->status = NFS4ERR_NOFILEHANDLE;
		goto out;
	}

	status = rfs4_get_deleg_state(cs, &args->deleg_stateid, &dsp);
	resp->status = *cs->statusp = status;
	if (status != NFS4_OK)
		goto out;
	if (cs->lease_moved) {
		status = NFS4ERR_LEASE_MOVED;
		goto done;
	}

	/* Ensure specified filehandle matches */
	if (cs->vp != dsp->rds_finfo->rf_vp) {
		resp->status = *cs->statusp = NFS4ERR_BAD_STATEID;
	} else
		rfs4_return_deleg(dsp, FALSE);

	rfs4_update_lease(dsp->rds_client);
done:
	rfs4_deleg_state_rele(dsp);
out:
	DTRACE_NFSV4_2(op__delegreturn__done, struct compound_state *, cs,
	    DELEGRETURN4res *, resp);
}

/*
 * Check to see if a given "flavor" is an explicitly shared flavor.
 * The assumption of this routine is the "flavor" is already a valid
 * flavor in the secinfo list of "exi".
 *
 *	e.g.
 *		# share -o sec=flavor1 /export
 *		# share -o sec=flavor2 /export/home
 *
 *		flavor2 is not an explicitly shared flavor for /export,
 *		however it is in the secinfo list for /export thru the
 *		server namespace setup.
 */
int
is_exported_sec(int flavor, struct exportinfo *exi)
{
	int	i;
	struct secinfo *sp;

	sp = exi->exi_export.ex_secinfo;
	for (i = 0; i < exi->exi_export.ex_seccnt; i++) {
		if (flavor == sp[i].s_secinfo.sc_nfsnum ||
		    sp[i].s_secinfo.sc_nfsnum == AUTH_NONE) {
			return (SEC_REF_EXPORTED(&sp[i]));
		}
	}

	/* Should not reach this point based on the assumption */
	return (0);
}

/*
 * Check if the security flavor used in the request matches what is
 * required at the export point.
 *
 * returns 1 if there's a match or if exported with AUTH_NONE; 0 otherwise.
 */
static int
secinfo_match_or_authnone(struct compound_state *cs)
{
	int	i;
	struct secinfo *sp;

	/*
	 * Check cs->nfsflavor (from the request) against
	 * the current export data in cs->exi.
	 */
	sp = cs->exi->exi_export.ex_secinfo;
	for (i = 0; i < cs->exi->exi_export.ex_seccnt; i++) {
		if (cs->nfsflavor == sp[i].s_secinfo.sc_nfsnum ||
		    sp[i].s_secinfo.sc_nfsnum == AUTH_NONE)
			return (1);
	}

	return (0);
}

/*
 * Check the access authority for the client and return the correct error.
 */
nfsstat4
call_checkauth4(struct compound_state *cs, struct svc_req *req)
{
	int	authres;

	/*
	 * First, check if the security flavor used in the request
	 * are among the flavors set in the server namespace.
	 */
	if (!secinfo_match_or_authnone(cs)) {
		*cs->statusp = NFS4ERR_WRONGSEC;
		return (*cs->statusp);
	}

	authres = checkauth4(cs, req);

	if (authres > 0) {
		*cs->statusp = NFS4_OK;
		if (! (cs->access & CS_ACCESS_LIMITED))
			cs->access = CS_ACCESS_OK;
	} else if (authres == 0) {
		*cs->statusp = NFS4ERR_ACCESS;
	} else if (authres == -2) {
		*cs->statusp = NFS4ERR_WRONGSEC;
	} else {
		*cs->statusp = NFS4ERR_DELAY;
	}
	return (*cs->statusp);
}

/*
 * bitmap4_to_attrmask is called by getattr and readdir.
 * It sets up the vattr mask and determines whether vfsstat call is needed
 * based on the input bitmap.
 * Returns nfsv4 status.
 */
static nfsstat4
bitmap4_to_attrmask(bitmap4 breq, struct nfs4_svgetit_arg *sargp)
{
	int i;
	uint_t	va_mask;
	struct statvfs64 *sbp = sargp->sbp;

	sargp->sbp = NULL;
	sargp->flag = 0;
	sargp->rdattr_error = NFS4_OK;
	sargp->mntdfid_set = FALSE;
	if (sargp->cs->vp)
		sargp->xattr = get_fh4_flag(&sargp->cs->fh,
		    FH4_ATTRDIR | FH4_NAMEDATTR);
	else
		sargp->xattr = 0;

	/*
	 * Set rdattr_error_req to true if return error per
	 * failed entry rather than fail the readdir.
	 */
	if (breq & FATTR4_RDATTR_ERROR_MASK)
		sargp->rdattr_error_req = 1;
	else
		sargp->rdattr_error_req = 0;

	/*
	 * generate the va_mask
	 * Handle the easy cases first
	 */
	switch (breq) {
	case NFS4_NTOV_ATTR_MASK:
		sargp->vap->va_mask = NFS4_NTOV_ATTR_AT_MASK;
		return (NFS4_OK);

	case NFS4_FS_ATTR_MASK:
		sargp->vap->va_mask = NFS4_FS_ATTR_AT_MASK;
		sargp->sbp = sbp;
		return (NFS4_OK);

	case NFS4_NTOV_ATTR_CACHE_MASK:
		sargp->vap->va_mask = NFS4_NTOV_ATTR_CACHE_AT_MASK;
		return (NFS4_OK);

	case FATTR4_LEASE_TIME_MASK:
		sargp->vap->va_mask = 0;
		return (NFS4_OK);

	default:
		va_mask = 0;
		for (i = 0; i < nfs4_ntov_map_size; i++) {
			if ((breq & nfs4_ntov_map[i].fbit) &&
			    nfs4_ntov_map[i].vbit)
				va_mask |= nfs4_ntov_map[i].vbit;
		}

		/*
		 * Check is vfsstat is needed
		 */
		if (breq & NFS4_FS_ATTR_MASK)
			sargp->sbp = sbp;

		sargp->vap->va_mask = va_mask;
		return (NFS4_OK);
	}
	/* NOTREACHED */
}

/*
 * bitmap4_get_sysattrs is called by getattr and readdir.
 * It calls both VOP_GETATTR and VFS_STATVFS calls to get the attrs.
 * Returns nfsv4 status.
 */
static nfsstat4
bitmap4_get_sysattrs(struct nfs4_svgetit_arg *sargp)
{
	int error;
	struct compound_state *cs = sargp->cs;
	vnode_t *vp = cs->vp;

	if (sargp->sbp != NULL) {
		if (error = VFS_STATVFS(vp->v_vfsp, sargp->sbp)) {
			sargp->sbp = NULL;	/* to identify error */
			return (puterrno4(error));
		}
	}

	return (rfs4_vop_getattr(vp, sargp->vap, 0, cs->cr));
}

static void
nfs4_ntov_table_init(struct nfs4_ntov_table *ntovp)
{
	ntovp->na = kmem_zalloc(sizeof (union nfs4_attr_u) * nfs4_ntov_map_size,
	    KM_SLEEP);
	ntovp->attrcnt = 0;
	ntovp->vfsstat = FALSE;
}

static void
nfs4_ntov_table_free(struct nfs4_ntov_table *ntovp,
    struct nfs4_svgetit_arg *sargp)
{
	int i;
	union nfs4_attr_u *na;
	uint8_t *amap;

	/*
	 * XXX Should do the same checks for whether the bit is set
	 */
	for (i = 0, na = ntovp->na, amap = ntovp->amap;
	    i < ntovp->attrcnt; i++, na++, amap++) {
		(void) (*nfs4_ntov_map[*amap].sv_getit)(
		    NFS4ATTR_FREEIT, sargp, na);
	}
	if ((sargp->op == NFS4ATTR_SETIT) || (sargp->op == NFS4ATTR_VERIT)) {
		/*
		 * xdr_free for getattr will be done later
		 */
		for (i = 0, na = ntovp->na, amap = ntovp->amap;
		    i < ntovp->attrcnt; i++, na++, amap++) {
			xdr_free(nfs4_ntov_map[*amap].xfunc, (caddr_t)na);
		}
	}
	kmem_free(ntovp->na, sizeof (union nfs4_attr_u) * nfs4_ntov_map_size);
}

/*
 * do_rfs4_op_getattr gets the system attrs and converts into fattr4.
 */
static nfsstat4
do_rfs4_op_getattr(bitmap4 breq, fattr4 *fattrp,
    struct nfs4_svgetit_arg *sargp, bool_t is_migrated)
{
	int error = 0;
	int i, k;
	struct nfs4_ntov_table ntov;
	XDR xdr;
	ulong_t xdr_size;
	char *xdr_attrs;
	nfsstat4 status = NFS4_OK;
	nfsstat4 prev_rdattr_error = sargp->rdattr_error;
	union nfs4_attr_u *na;
	uint8_t *amap;

	sargp->op = NFS4ATTR_GETIT;
	sargp->flag = 0;

	fattrp->attrmask = 0;
	/* if no bits requested, then return empty fattr4 */
	if (breq == 0) {
		fattrp->attrlist4_len = 0;
		fattrp->attrlist4 = NULL;
		return (NFS4_OK);
	}

	/*
	 * return NFS4ERR_INVAL when client requests write-only attrs
	 */
	if (breq & (FATTR4_TIME_ACCESS_SET_MASK | FATTR4_TIME_MODIFY_SET_MASK))
		return (NFS4ERR_INVAL);

	nfs4_ntov_table_init(&ntov);
	na = ntov.na;
	amap = ntov.amap;

	/*
	 * Now loop to get or verify the attrs
	 */
	for (i = 0; i < nfs4_ntov_map_size; i++) {
		if (breq & nfs4_ntov_map[i].fbit) {
			if (is_migrated == TRUE &&
			    VALID_MOVED_ATTR(breq) == FALSE)
				continue;
			if ((*nfs4_ntov_map[i].sv_getit)(
			    NFS4ATTR_SUPPORTED, sargp, NULL) == 0) {

				error = (*nfs4_ntov_map[i].sv_getit)(
				    NFS4ATTR_GETIT, sargp, na);

				/*
				 * Possible error values:
				 * >0:
				 *    if sv_getit failed to
				 *    get the attr;
				 * 0:
				 *    if succeeded;
				 * NFS4_ATTR_ERR_RDATTR_OKAY:
				 *    if rdattr_error and the
				 *    attribute cannot be returned.
				 * NFS4_ATTR_ERR_SILENT_IGNORE:
				 *    if the attribute cannot
				 *    be returned and that is okay.
				 */
				DTRACE_PROBE2(rfs4__i__getattr_check_error,
				    int, error, bool_t,
				    sargp->rdattr_error_req);
				if (error &&
				    error != NFS4_ATTR_ERR_SILENT_IGNORE &&
				    !(sargp->rdattr_error_req)) {
					/*
					 * Only return valid errors across
					 * the wire!
					 */
					if (error < 0)
						error = EINVAL;
					goto done;
				}

				/*
				 * If error then just for entry
				 */
				if (error == 0) {
					fattrp->attrmask |=
					    nfs4_ntov_map[i].fbit;
					*amap++ =
					    (uint8_t)nfs4_ntov_map[i].nval;
					na++;
					(ntov.attrcnt)++;
				} else if ((error > 0) &&
				    (sargp->rdattr_error == NFS4_OK)) {
					sargp->rdattr_error = puterrno4(error);
				}
				error = 0;
			}
		}
	}

	/*
	 * If rdattr_error was set after the return value for it was assigned,
	 * update it.
	 */
	if (prev_rdattr_error != sargp->rdattr_error) {
		na = ntov.na;
		amap = ntov.amap;
		for (i = 0; i < ntov.attrcnt; i++, na++, amap++) {
			k = *amap;
			if (k < FATTR4_RDATTR_ERROR) {
				continue;
			}
			if ((k == FATTR4_RDATTR_ERROR) &&
			    ((*nfs4_ntov_map[k].sv_getit)(
			    NFS4ATTR_SUPPORTED, sargp, NULL) == 0)) {

				(void) (*nfs4_ntov_map[k].sv_getit)(
				    NFS4ATTR_GETIT, sargp, na);
			}
			break;
		}
	}

	xdr_size = 0;
	na = ntov.na;
	amap = ntov.amap;
	for (i = 0; i < ntov.attrcnt; i++, na++, amap++) {
		xdr_size += xdr_sizeof(nfs4_ntov_map[*amap].xfunc, na);
	}

	fattrp->attrlist4_len = xdr_size;
	if (xdr_size) {
		/* freed by rfs4_op_getattr_free() */
		fattrp->attrlist4 = xdr_attrs = kmem_zalloc(xdr_size, KM_SLEEP);

		xdrmem_create(&xdr, xdr_attrs, xdr_size, XDR_ENCODE);

		na = ntov.na;
		amap = ntov.amap;
		for (i = 0; i < ntov.attrcnt; i++, na++, amap++) {
			if (!(*nfs4_ntov_map[*amap].xfunc)(&xdr, na)) {
				DTRACE_PROBE1(nfss__e__getattr4_encfail,
				    int, *amap);
				status = NFS4ERR_SERVERFAULT;
				break;
			}
		}
		/* xdrmem_destroy(&xdrs); */	/* NO-OP */
	} else {
		fattrp->attrlist4 = NULL;
	}
done:

	nfs4_ntov_table_free(&ntov, sargp);

	if (error != 0)
		status = puterrno4(error);

	return (status);
}

/* ARGSUSED */
static void
rfs4_op_getattr(nfs_argop4 *argop, nfs_resop4 *resop, struct svc_req *req,
    struct compound_state *cs)
{
	GETATTR4args *args = &argop->nfs_argop4_u.opgetattr;
	GETATTR4res *resp = &resop->nfs_resop4_u.opgetattr;
	struct nfs4_svgetit_arg sarg;
	struct statvfs64 sb;
	nfsstat4 status;

	DTRACE_NFSV4_2(op__getattr__start, struct compound_state *, cs,
	    GETATTR4args *, args);

	if (cs->vp == NULL) {
		*cs->statusp = resp->status = NFS4ERR_NOFILEHANDLE;
		goto out;
	}

	if (cs->access == CS_ACCESS_DENIED) {
		*cs->statusp = resp->status = NFS4ERR_ACCESS;
		goto out;
	}

	sarg.sbp = &sb;
	sarg.cs = cs;
	sarg.is_referral = B_FALSE;

	status = bitmap4_to_attrmask(args->attr_request, &sarg);
	if (status == NFS4_OK) {

		status = bitmap4_get_sysattrs(&sarg);
		if (status == NFS4_OK) {

			/*
			 * Check to see if we need to test for a migration.
			 */
			if (cs->fse->fse_state == FSE_MOVED) {
				/*
				 * If one of these is not set, then
				 * we return NFS4ERR_MOVED.
				 */
				if (VALID_MOVED_ATTR(args->attr_request) ==
				    FALSE) {
					status = NFS4ERR_MOVED;
					goto done;
				}
			}

			/* Is this a referral? */
			if (vn_is_nfs_reparse(cs->vp, cs->cr)) {
				/* Older V4 Solaris client sees a link */
				if (client_is_downrev(req))
					sarg.vap->va_type = VLNK;
				else
					sarg.is_referral = B_TRUE;
			}

			status = do_rfs4_op_getattr(args->attr_request,
			    &resp->obj_attributes, &sarg,
			    cs->fse->fse_state == FSE_MOVED);
		}
	}

done:
	*cs->statusp = resp->status = status;
out:
	DTRACE_NFSV4_2(op__getattr__done, struct compound_state *, cs,
	    GETATTR4res *, resp);
}

static void
rfs4_op_getattr_free(nfs_resop4 *resop)
{
	GETATTR4res *resp = &resop->nfs_resop4_u.opgetattr;

	nfs4_fattr4_free(&resp->obj_attributes);
}

/* ARGSUSED */
static void
rfs4_op_getfh(nfs_argop4 *argop, nfs_resop4 *resop, struct svc_req *req,
    struct compound_state *cs)
{
	GETFH4res *resp = &resop->nfs_resop4_u.opgetfh;

	DTRACE_NFSV4_1(op__getfh__start, struct compound_state *, cs);

	if (cs->vp == NULL) {
		*cs->statusp = resp->status = NFS4ERR_NOFILEHANDLE;
		goto out;
	}
	if (cs->access == CS_ACCESS_DENIED) {
		*cs->statusp = resp->status = NFS4ERR_ACCESS;
		goto out;
	}

	/*
	 * we only want to return moved if this is a referral.  check for
	 * reparse point at vp, but only if it is not the root of the FS.
	 */
	if (!(cs->vp->v_flag & VROOT) &&
	    vn_is_nfs_reparse(cs->vp, cs->cr) && !client_is_downrev(req)) {
		/* it's not all bad */
		*cs->statusp = resp->status = NFS4ERR_MOVED;
		DTRACE_PROBE2(nfs4serv__func__referral__moved,
		    vnode_t *, cs->vp, char *, "rfs4_op_getfh");
		return;
	}

	resp->object.nfs_fh4_val =
	    kmem_alloc(cs->fh.nfs_fh4_len, KM_SLEEP);
	nfs_fh4_copy(&cs->fh, &resp->object);
	*cs->statusp = resp->status = NFS4_OK;
out:
	DTRACE_NFSV4_2(op__getfh__done, struct compound_state *, cs,
	    GETFH4res *, resp);
}

static void
rfs4_op_getfh_free(nfs_resop4 *resop)
{
	GETFH4res *resp = &resop->nfs_resop4_u.opgetfh;

	if (resp->status == NFS4_OK &&
	    resp->object.nfs_fh4_val != NULL) {
		kmem_free(resp->object.nfs_fh4_val, resp->object.nfs_fh4_len);
		resp->object.nfs_fh4_val = NULL;
		resp->object.nfs_fh4_len = 0;
	}
}

/*
 * XXXtdh: ???
 */
/* ARGSUSED */
static void
rfs4_op_error(nfs_argop4 *argop, nfs_resop4 *resop,
	struct svc_req *req, struct compound_state *cs, nfsstat4 stat)
{
	ILLEGAL4res *resp = &resop->nfs_resop4_u.opillegal;
	*cs->statusp = resp->status = stat;
}

/*
 * illegal: args: void
 *	    res : status (NFS4ERR_OP_ILLEGAL)
 */
/* ARGSUSED */
static void
rfs4_op_illegal(nfs_argop4 *argop, nfs_resop4 *resop,
    struct svc_req *req, struct compound_state *cs)
{
	ILLEGAL4res *resp = &resop->nfs_resop4_u.opillegal;

	resop->resop = OP_ILLEGAL;
	*cs->statusp = resp->status = NFS4ERR_OP_ILLEGAL;
}

/*
 * link: args: SAVED_FH: file, CURRENT_FH: target directory
 *	 res: status. If success - CURRENT_FH unchanged, return change_info
 */
/* ARGSUSED */
static void
rfs4_op_link(nfs_argop4 *argop, nfs_resop4 *resop, struct svc_req *req,
    struct compound_state *cs)
{
	LINK4args *args = &argop->nfs_argop4_u.oplink;
	LINK4res *resp = &resop->nfs_resop4_u.oplink;
	int error;
	vnode_t *vp;
	vnode_t *dvp;
	struct vattr bdva, idva, adva;
	char *nm;
	uint_t  len;
	struct sockaddr *ca;
	char *name = NULL;

	DTRACE_NFSV4_2(op__link__start, struct compound_state *, cs,
	    LINK4args *, args);

	/* SAVED_FH: source object */
	vp = cs->saved_vp;
	if (vp == NULL) {
		*cs->statusp = resp->status = NFS4ERR_NOFILEHANDLE;
		goto out;
	}

	/* CURRENT_FH: target directory */
	dvp = cs->vp;
	if (dvp == NULL) {
		*cs->statusp = resp->status = NFS4ERR_NOFILEHANDLE;
		goto out;
	}

	/*
	 * If there is a non-shared filesystem mounted on this vnode,
	 * do not allow to link any file in this directory.
	 */
	if (vn_ismntpt(dvp)) {
		*cs->statusp = resp->status = NFS4ERR_ACCESS;
		goto out;
	}

	if (cs->access == CS_ACCESS_DENIED) {
		*cs->statusp = resp->status = NFS4ERR_ACCESS;
		goto out;
	}

	/* Check source object's type validity */
	if (vp->v_type == VDIR) {
		*cs->statusp = resp->status = NFS4ERR_ISDIR;
		goto out;
	}

	/* Check target directory's type */
	if (dvp->v_type != VDIR) {
		*cs->statusp = resp->status = NFS4ERR_NOTDIR;
		goto out;
	}

	if (cs->saved_exi != cs->exi) {
		*cs->statusp = resp->status = NFS4ERR_XDEV;
		goto out;
	}

	if (!utf8_dir_verify(&args->newname)) {
		*cs->statusp = resp->status = NFS4ERR_INVAL;
		goto out;
	}

	nm = utf8_to_fn(&args->newname, &len, NULL);
	if (nm == NULL) {
		*cs->statusp = resp->status = NFS4ERR_INVAL;
		goto out;
	}

	if (len > MAXNAMELEN) {
		*cs->statusp = resp->status = NFS4ERR_NAMETOOLONG;
		kmem_free(nm, len);
		goto out;
	}

	if (rdonly4(cs->exi, cs->vp, req)) {
		*cs->statusp = resp->status = NFS4ERR_ROFS;
		kmem_free(nm, len);
		goto out;
	}

	/* Get "before" change value */
	bdva.va_mask = AT_CTIME|AT_SEQ;
	error = VOP_GETATTR(dvp, &bdva, 0, cs->cr, NULL);
	if (error) {
		*cs->statusp = resp->status = puterrno4(error);
		kmem_free(nm, len);
		goto out;
	}

	ca = (struct sockaddr *)svc_getrpccaller(req->rq_xprt)->buf;
	name = nfscmd_convname(ca, cs->exi, nm, NFSCMD_CONV_INBOUND,
	    MAXPATHLEN  + 1);

	if (name == NULL) {
		*cs->statusp = resp->status = NFS4ERR_INVAL;
		kmem_free(nm, len);
		goto out;
	}

	NFS4_SET_FATTR4_CHANGE(resp->cinfo.before, bdva.va_ctime)

	error = VOP_LINK(dvp, vp, name, cs->cr, NULL, 0);

	if (nm != name)
		kmem_free(name, MAXPATHLEN + 1);
	kmem_free(nm, len);

	/*
	 * Get the initial "after" sequence number, if it fails, set to zero
	 */
	idva.va_mask = AT_SEQ;
	if (VOP_GETATTR(dvp, &idva, 0, cs->cr, NULL))
		idva.va_seq = 0;

	/*
	 * Force modified data and metadata out to stable storage.
	 */
	(void) VOP_FSYNC(vp, FNODSYNC, cs->cr, NULL);
	(void) VOP_FSYNC(dvp, 0, cs->cr, NULL);

	if (error) {
		*cs->statusp = resp->status = puterrno4(error);
		goto out;
	}

	/*
	 * Get "after" change value, if it fails, simply return the
	 * before value.
	 */
	adva.va_mask = AT_CTIME|AT_SEQ;
	if (VOP_GETATTR(dvp, &adva, 0, cs->cr, NULL)) {
		adva.va_ctime = bdva.va_ctime;
		adva.va_seq = 0;
	}

	NFS4_SET_FATTR4_CHANGE(resp->cinfo.after, adva.va_ctime)

	/*
	 * The cinfo.atomic = TRUE only if we have
	 * non-zero va_seq's, and it has incremented by exactly one
	 * during the VOP_LINK and it didn't change during the VOP_FSYNC.
	 */
	if (bdva.va_seq && idva.va_seq && adva.va_seq &&
	    idva.va_seq == (bdva.va_seq + 1) && idva.va_seq == adva.va_seq)
		resp->cinfo.atomic = TRUE;
	else
		resp->cinfo.atomic = FALSE;

	*cs->statusp = resp->status = NFS4_OK;
out:
	DTRACE_NFSV4_2(op__link__done, struct compound_state *, cs,
	    LINK4res *, resp);
}

/*
 * Used by rfs4_op_lookup and rfs4_op_lookupp to do the actual work.
 */

/* ARGSUSED */
static nfsstat4
do_rfs4_op_lookup(char *nm, struct svc_req *req, struct compound_state *cs)
{
	int error;
	int different_export = 0;
	vnode_t *vp, *pre_tvp = NULL, *oldvp = NULL;
	struct exportinfo *exi = NULL, *pre_exi = NULL;
	nfsstat4 stat;
	fid_t fid;
	int attrdir, dotdot, walk;

	if (cs->vp->v_flag & V_XATTRDIR) {
		attrdir = 1;
		ASSERT(get_fh4_flag(&cs->fh, FH4_ATTRDIR));
	} else {
		attrdir = 0;
		ASSERT(! get_fh4_flag(&cs->fh, FH4_ATTRDIR));
	}

	dotdot = (nm[0] == '.' && nm[1] == '.' && nm[2] == '\0');

	/*
	 * If dotdotting, then need to check whether it's
	 * above the root of a filesystem, or above an
	 * export point.
	 */
	if (dotdot) {

		/*
		 * If dotdotting at the root of a filesystem, then
		 * need to traverse back to the mounted-on filesystem
		 * and do the dotdot lookup there.
		 */
		if (cs->vp->v_flag & VROOT) {

			/*
			 * If at the zone/instance root, then
			 * go up no further.
			 */
			if (VN_CMP(cs->vp, RFS_INST_ROOTVP(cs->rip)))
				return (puterrno4(ENOENT));

			/*
			 * Traverse back to the mounted-on filesystem
			 */
			cs->vp = vn_under(cs->vp);

			/*
			 * Set the different_export flag so we remember
			 * to pick up a new exportinfo entry for
			 * this new filesystem.
			 */
			different_export = 1;
		} else {

			/*
			 * If dotdotting above an export point then set
			 * the different_export to get new export info.
			 */
			different_export = nfs_exported(cs->exi, cs->vp);
		}
	}

	error = VOP_LOOKUP(cs->vp, nm, &vp, NULL, 0, NULL, cs->cr,
	    NULL, NULL, NULL);
	if (error)
		return (puterrno4(error));

	/*
	 * If the vnode is in a pseudo filesystem, check whether it is visible.
	 *
	 * XXX if the vnode is a symlink and it is not visible in
	 * a pseudo filesystem, return ENOENT (not following symlink).
	 * V4 client can not mount such symlink. This is a regression
	 * from V2/V3.
	 *
	 * In the same exported filesystem, if the security flavor used
	 * is not an explicitly shared flavor, limit the view to the visible
	 * list entries only. This is not a WRONGSEC case because it's already
	 * checked via PUTROOTFH/PUTPUBFH or PUTFH.
	 */
	if (!different_export &&
	    (PSEUDO(cs->exi) || ! is_exported_sec(cs->nfsflavor, cs->exi) ||
	    cs->access & CS_ACCESS_LIMITED)) {
		if (! nfs_visible(cs->exi, vp, &different_export)) {
			VN_RELE(vp);
			return (puterrno4(ENOENT));
		}
	}

	/*
	 * If it's a mountpoint, then traverse it.
	 */
	if (vn_ismntpt(vp)) {
		pre_exi = cs->exi;	/* save pre-traversed exportinfo */
		pre_tvp = vp;		/* save pre-traversed vnode	*/

		/*
		 * hold pre_tvp to counteract rele by traverse.  We will
		 * need pre_tvp below if checkexport4 fails
		 */
		VN_HOLD(pre_tvp);
		if ((error = traverse(&vp)) != 0) {
			VN_RELE(vp);
			VN_RELE(pre_tvp);
			return (puterrno4(error));
		}
		different_export = 1;
	} else if (vp->v_vfsp != cs->vp->v_vfsp) {
		/*
		 * The vfsp comparison is to handle the case where
		 * a LOFS mount is shared.  lo_lookup traverses mount points,
		 * and NFS is unaware of local fs transistions because
		 * v_vfsmountedhere isn't set.  For this special LOFS case,
		 * the dir and the obj returned by lookup will have different
		 * vfs ptrs.
		 */
		different_export = 1;
	}

	if (different_export) {

		bzero(&fid, sizeof (fid));
		fid.fid_len = MAXFIDSZ;
		error = vop_fid_pseudo(vp, &fid);
		if (error) {
			VN_RELE(vp);
			if (pre_tvp)
				VN_RELE(pre_tvp);
			return (puterrno4(error));
		}

		if (dotdot)
			exi = nfs_vptoexi(cs->rip, NULL, vp, cs->cr, &walk,
			    NULL, TRUE);
		else
			exi = checkexport4(cs->rip, &vp->v_vfsp->vfs_fsid,
			    &fid, vp);

		if (exi == NULL) {
			if (pre_tvp) {
				/*
				 * If this vnode is a mounted-on vnode,
				 * but the mounted-on file system is not
				 * exported, send back the filehandle for
				 * the mounted-on vnode, not the root of
				 * the mounted-on file system.
				 */
				VN_RELE(vp);
				vp = pre_tvp;
				exi = pre_exi;
			} else {
				VN_RELE(vp);
				return (puterrno4(EACCES));
			}
		} else if (pre_tvp) {
			/* we're done with pre_tvp now. release extra hold */
			VN_RELE(pre_tvp);
		}

		cs->exi = exi;

		/*
		 * Now we do a checkauth4. The reason is that
		 * this client/user may not have access to the new
		 * exported file system, and if he does,
		 * the client/user may be mapped to a different uid.
		 *
		 * We start with a new cr, because the checkauth4 done
		 * in the PUT*FH operation over wrote the cred's uid,
		 * gid, etc, and we want the real thing before calling
		 * checkauth4()
		 */
		crfree(cs->cr);
		cs->cr = crdup(cs->basecr);

		oldvp = cs->vp;
		cs->vp = vp;

		stat = call_checkauth4(cs, req);
		if (stat != NFS4_OK) {
			cs->vp = oldvp;
			VN_RELE(vp);
			return (stat);
		}
	} else {
		oldvp = cs->vp;
		cs->vp = vp;
	}

	/*
	 * After various NFS checks, do a label check on the path
	 * component. The label on this path should either be the
	 * global zone's label or a zone's label. We are only
	 * interested in the zone's label because exported files
	 * in global zone is accessible (though read-only) to
	 * clients. The exportability/visibility check is already
	 * done before reaching this code.
	 */
	if (is_system_labeled()) {
		bslabel_t *clabel;

		ASSERT(req->rq_label != NULL);
		clabel = req->rq_label;
		DTRACE_PROBE2(tx__rfs4__log__info__oplookup__clabel, char *,
		    "got client label from request(1)", struct svc_req *, req);

		if (!blequal(&l_admin_low->tsl_label, clabel)) {
			if (!do_rfs_label_check(clabel, vp, DOMINANCE_CHECK,
			    cs->exi)) {
				error = EACCES;
				goto err_out;
			}
		} else {
			/*
			 * We grant access to admin_low label clients
			 * only if the client is trusted, i.e. also
			 * running Solaris Trusted Extension.
			 */
			struct sockaddr	*ca;
			int		addr_type;
			void		*ipaddr;
			tsol_tpc_t	*tp;

			ca = (struct sockaddr *)svc_getrpccaller(
			    req->rq_xprt)->buf;
			if (ca->sa_family == AF_INET) {
				addr_type = IPV4_VERSION;
				ipaddr = &((struct sockaddr_in *)ca)->sin_addr;
			} else if (ca->sa_family == AF_INET6) {
				addr_type = IPV6_VERSION;
				ipaddr = &((struct sockaddr_in6 *)
				    ca)->sin6_addr;
			}
			tp = find_tpc(ipaddr, addr_type, B_FALSE);
			if (tp == NULL || tp->tpc_tp.tp_doi !=
			    l_admin_low->tsl_doi || tp->tpc_tp.host_type !=
			    SUN_CIPSO) {
				if (tp != NULL)
					TPC_RELE(tp);
				error = EACCES;
				goto err_out;
			}
			TPC_RELE(tp);
		}
	}

	error = makefh4(&cs->fh, vp, cs->exi);

err_out:
	if (error) {
		cs->vp = oldvp;
		VN_RELE(vp);
		return (puterrno4(error));
	}

	if (oldvp)
		VN_RELE(oldvp);

	/*
	 * if did lookup on attrdir and didn't lookup .., set named
	 * attr fh flag
	 */
	if (attrdir && ! dotdot)
		set_fh4_flag(&cs->fh, FH4_NAMEDATTR);

	/* Assume false for now, open proc will set this */
	cs->mandlock = FALSE;

	return (NFS4_OK);
}

/* ARGSUSED */
static void
rfs4_op_lookup(nfs_argop4 *argop, nfs_resop4 *resop, struct svc_req *req,
    struct compound_state *cs)
{
	LOOKUP4args *args = &argop->nfs_argop4_u.oplookup;
	LOOKUP4res *resp = &resop->nfs_resop4_u.oplookup;
	char *nm;
	uint_t len;
	struct sockaddr *ca;
	char *name = NULL;

	DTRACE_NFSV4_2(op__lookup__start, struct compound_state *, cs,
	    LOOKUP4args *, args);

	if (cs->vp == NULL) {
		*cs->statusp = resp->status = NFS4ERR_NOFILEHANDLE;
		goto out;
	}

	if (cs->vp->v_type == VLNK) {
		*cs->statusp = resp->status = NFS4ERR_SYMLINK;
		goto out;
	}

	if (cs->vp->v_type != VDIR) {
		*cs->statusp = resp->status = NFS4ERR_NOTDIR;
		goto out;
	}

	if (!utf8_dir_verify(&args->objname)) {
		*cs->statusp = resp->status = NFS4ERR_INVAL;
		goto out;
	}

	nm = utf8_to_str(&args->objname, &len, NULL);
	if (nm == NULL) {
		*cs->statusp = resp->status = NFS4ERR_INVAL;
		goto out;
	}

	if (len > MAXNAMELEN) {
		*cs->statusp = resp->status = NFS4ERR_NAMETOOLONG;
		kmem_free(nm, len);
		goto out;
	}

	ca = (struct sockaddr *)svc_getrpccaller(req->rq_xprt)->buf;
	name = nfscmd_convname(ca, cs->exi, nm, NFSCMD_CONV_INBOUND,
	    MAXPATHLEN  + 1);

	if (name == NULL) {
		*cs->statusp = resp->status = NFS4ERR_INVAL;
		kmem_free(nm, len);
		goto out;
	}

	*cs->statusp = resp->status = do_rfs4_op_lookup(name, req, cs);

	if (name != nm)
		kmem_free(name, MAXPATHLEN + 1);
	kmem_free(nm, len);

out:
	DTRACE_NFSV4_2(op__lookup__done, struct compound_state *, cs,
	    LOOKUP4res *, resp);
}

/* ARGSUSED */
static void
rfs4_op_lookupp(nfs_argop4 *args, nfs_resop4 *resop, struct svc_req *req,
    struct compound_state *cs)
{
	LOOKUPP4res *resp = &resop->nfs_resop4_u.oplookupp;

	DTRACE_NFSV4_1(op__lookupp__start, struct compound_state *, cs);

	if (cs->vp == NULL) {
		*cs->statusp = resp->status = NFS4ERR_NOFILEHANDLE;
		goto out;
	}

	if (cs->vp->v_type != VDIR) {
		*cs->statusp = resp->status = NFS4ERR_NOTDIR;
		goto out;
	}

	*cs->statusp = resp->status = do_rfs4_op_lookup("..", req, cs);

	/*
	 * From NFSV4 Specification, LOOKUPP should not check for
	 * NFS4ERR_WRONGSEC. Retrun NFS4_OK instead.
	 */
	if (resp->status == NFS4ERR_WRONGSEC) {
		*cs->statusp = resp->status = NFS4_OK;
	}

out:
	DTRACE_NFSV4_2(op__lookupp__done, struct compound_state *, cs,
	    LOOKUPP4res *, resp);
}


/*ARGSUSED2*/
static void
rfs4_op_openattr(nfs_argop4 *argop, nfs_resop4 *resop, struct svc_req *req,
    struct compound_state *cs)
{
	OPENATTR4args	*args = &argop->nfs_argop4_u.opopenattr;
	OPENATTR4res	*resp = &resop->nfs_resop4_u.opopenattr;
	vnode_t		*avp = NULL;
	int		lookup_flags = LOOKUP_XATTR, error;
	int		exp_ro = 0;

	DTRACE_NFSV4_2(op__openattr__start, struct compound_state *, cs,
	    OPENATTR4args *, args);

	if (cs->vp == NULL) {
		*cs->statusp = resp->status = NFS4ERR_NOFILEHANDLE;
		goto out;
	}

	if ((cs->vp->v_vfsp->vfs_flag & VFS_XATTR) == 0 &&
	    !vfs_has_feature(cs->vp->v_vfsp, VFSFT_SYSATTR_VIEWS)) {
		*cs->statusp = resp->status = puterrno4(ENOTSUP);
		goto out;
	}

	/*
	 * If file system supports passing ACE mask to VOP_ACCESS then
	 * check for ACE_READ_NAMED_ATTRS, otherwise do legacy checks
	 */

	if (vfs_has_feature(cs->vp->v_vfsp, VFSFT_ACEMASKONACCESS))
		error = VOP_ACCESS(cs->vp, ACE_READ_NAMED_ATTRS,
		    V_ACE_MASK, cs->cr, NULL);
	else
		error = ((VOP_ACCESS(cs->vp, VREAD, 0, cs->cr, NULL) != 0) &&
		    (VOP_ACCESS(cs->vp, VWRITE, 0, cs->cr, NULL) != 0) &&
		    (VOP_ACCESS(cs->vp, VEXEC, 0, cs->cr, NULL) != 0));

	if (error) {
		*cs->statusp = resp->status = puterrno4(EACCES);
		goto out;
	}

	/*
	 * The CREATE_XATTR_DIR VOP flag cannot be specified if
	 * the file system is exported read-only -- regardless of
	 * createdir flag.  Otherwise the attrdir would be created
	 * (assuming server fs isn't mounted readonly locally).  If
	 * VOP_LOOKUP returns ENOENT in this case, the error will
	 * be translated into EROFS.  ENOSYS is mapped to ENOTSUP
	 * because specfs has no VOP_LOOKUP op, so the macro would
	 * return ENOSYS.  EINVAL is returned by all (current)
	 * Solaris file system implementations when any of their
	 * restrictions are violated (xattr(dir) can't have xattrdir).
	 * Returning NOTSUPP is more appropriate in this case
	 * because the object will never be able to have an attrdir.
	 */
	if (args->createdir && ! (exp_ro = rdonly4(cs->exi, cs->vp, req)))
		lookup_flags |= CREATE_XATTR_DIR;

	error = VOP_LOOKUP(cs->vp, "", &avp, NULL, lookup_flags, NULL, cs->cr,
	    NULL, NULL, NULL);

	if (error) {
		if (error == ENOENT && args->createdir && exp_ro)
			*cs->statusp = resp->status = puterrno4(EROFS);
		else if (error == EINVAL || error == ENOSYS)
			*cs->statusp = resp->status = puterrno4(ENOTSUP);
		else
			*cs->statusp = resp->status = puterrno4(error);
		goto out;
	}

	ASSERT(avp->v_flag & V_XATTRDIR);

	error = makefh4(&cs->fh, avp, cs->exi);

	if (error) {
		VN_RELE(avp);
		*cs->statusp = resp->status = puterrno4(error);
		goto out;
	}

	VN_RELE(cs->vp);
	cs->vp = avp;

	/*
	 * There is no requirement for an attrdir fh flag
	 * because the attrdir has a vnode flag to distinguish
	 * it from regular (non-xattr) directories.  The
	 * FH4_ATTRDIR flag is set for future sanity checks.
	 */
	set_fh4_flag(&cs->fh, FH4_ATTRDIR);
	*cs->statusp = resp->status = NFS4_OK;

out:
	DTRACE_NFSV4_2(op__openattr__done, struct compound_state *, cs,
	    OPENATTR4res *, resp);
}

static int
do_io(int direction, vnode_t *vp, struct uio *uio, int ioflag, cred_t *cred,
    caller_context_t *ct)
{
	int error;
	int i;
	clock_t delaytime;

	delaytime = MSEC_TO_TICK_ROUNDUP(rfs4_lock_delay);

	/*
	 * Don't block on mandatory locks. If this routine returns
	 * EAGAIN, the caller should return NFS4ERR_LOCKED.
	 */
	uio->uio_fmode = FNONBLOCK;

	for (i = 0; i < rfs4_maxlock_tries; i++) {


		if (direction == FREAD) {
			(void) VOP_RWLOCK(vp, V_WRITELOCK_FALSE, ct);
			error = VOP_READ(vp, uio, ioflag, cred, ct);
			VOP_RWUNLOCK(vp, V_WRITELOCK_FALSE, ct);
		} else {
			(void) VOP_RWLOCK(vp, V_WRITELOCK_TRUE, ct);
			error = VOP_WRITE(vp, uio, ioflag, cred, ct);
			VOP_RWUNLOCK(vp, V_WRITELOCK_TRUE, ct);
		}

		if (error != EAGAIN)
			break;

		if (i < rfs4_maxlock_tries - 1) {
			delay(delaytime);
			delaytime *= 2;
		}
	}

	return (error);
}

/* ARGSUSED */
static void
rfs4_op_read(nfs_argop4 *argop, nfs_resop4 *resop, struct svc_req *req,
    struct compound_state *cs)
{
	READ4args *args = &argop->nfs_argop4_u.opread;
	READ4res *resp = &resop->nfs_resop4_u.opread;
	int error;
	int verror;
	vnode_t *vp;
	struct vattr va;
	struct iovec iov;
	struct uio uio;
	u_offset_t offset;
	bool_t *deleg = &cs->deleg;
	nfsstat4 stat;
	int in_crit = 0;
	mblk_t *mp = NULL;
	int alloc_err = 0;
	int rdma_used = 0;
	int loaned_buffers;
	caller_context_t ct;
	struct uio *uiop;

	DTRACE_NFSV4_2(op__read__start, struct compound_state *, cs,
	    READ4args, args);

	vp = cs->vp;
	if (vp == NULL) {
		*cs->statusp = resp->status = NFS4ERR_NOFILEHANDLE;
		goto out;
	}
	if (cs->access == CS_ACCESS_DENIED) {
		*cs->statusp = resp->status = NFS4ERR_ACCESS;
		goto out;
	}

	if ((stat = rfs4_check_stateid(FREAD, cs, vp, &args->stateid, FALSE,
	    deleg, TRUE, &ct)) != NFS4_OK) {
		*cs->statusp = resp->status = stat;
		goto out;
	}
	if (cs->lease_moved) {
		*cs->statusp = resp->status = NFS4ERR_LEASE_MOVED;
		goto out;
	}

	/*
	 * Enter the critical region before calling VOP_RWLOCK
	 * to avoid a deadlock with write requests.
	 */
	if (nbl_need_check(vp)) {
		nbl_start_crit(vp, RW_READER);
		in_crit = 1;
		if (nbl_conflict(vp, NBL_READ, args->offset, args->count, 0,
		    &ct)) {
			*cs->statusp = resp->status = NFS4ERR_LOCKED;
			goto out;
		}
	}

	if ((stat = rfs4_check_stateid(FREAD, cs, vp, &args->stateid, FALSE,
	    deleg, TRUE, &ct)) != NFS4_OK) {
		*cs->statusp = resp->status = stat;
		goto out;
	}

	if (req->rq_xprt->xp_type == T_RDMA)
		rdma_used = 1;

	if (rdma_used) {
		if ((args->wlist != NULL &&
		    args->count > clist_len(args->wlist))) {
			*cs->statusp = resp->status = NFS4ERR_INVAL;
			goto out;
		}
	}

	/* use loaned buffers for TCP */
	loaned_buffers = (nfs_loaned_buffers && !rdma_used) ? 1 : 0;

	va.va_mask = AT_MODE|AT_SIZE|AT_UID;
	verror = VOP_GETATTR(vp, &va, 0, cs->cr, &ct);

	/*
	 * If we can't get the attributes, then we can't do the
	 * right access checking.  So, we'll fail the request.
	 */
	if (verror) {
		*cs->statusp = resp->status = puterrno4(verror);
		goto out;
	}

	if (vp->v_type != VREG) {
		*cs->statusp = resp->status =
		    ((vp->v_type == VDIR) ? NFS4ERR_ISDIR : NFS4ERR_INVAL);
		goto out;
	}

	if (crgetuid(cs->cr) != va.va_uid &&
	    (error = VOP_ACCESS(vp, VREAD, 0, cs->cr, &ct)) &&
	    (error = VOP_ACCESS(vp, VEXEC, 0, cs->cr, &ct))) {
		*cs->statusp = resp->status = puterrno4(error);
		goto out;
	}

	if (MANDLOCK(vp, va.va_mode)) { /* XXX - V4 supports mand locking */
		*cs->statusp = resp->status = NFS4ERR_ACCESS;
		goto out;
	}

	offset = args->offset;
	if (offset >= va.va_size) {
		*cs->statusp = resp->status = NFS4_OK;
		resp->eof = TRUE;
		resp->data_len = 0;
		resp->data_val = NULL;
		resp->mblk = NULL;
		/* RDMA */
		resp->wlist = args->wlist;
		resp->wlist_len = resp->data_len;
		*cs->statusp = resp->status = NFS4_OK;
		if (resp->wlist)
			clist_zero_len(resp->wlist);
		goto out;
	}

	if (args->count == 0) {
		*cs->statusp = resp->status = NFS4_OK;
		resp->eof = FALSE;
		resp->data_len = 0;
		resp->data_val = NULL;
		resp->mblk = NULL;
		/* RDMA */
		resp->wlist = args->wlist;
		resp->wlist_len = resp->data_len;
		if (resp->wlist)
			clist_zero_len(resp->wlist);
		goto out;
	}

	/*
	 * Do not allocate memory more than maximum allowed
	 * transfer size
	 */
	if (args->count > rfs4_tsize(req))
		args->count = rfs4_tsize(req);

	if (loaned_buffers) {
		uiop = (uio_t *)rfs_setup_xuio(vp);
		ASSERT(uiop != NULL);
		uiop->uio_segflg = UIO_SYSSPACE;
		uiop->uio_loffset = args->offset;
		uiop->uio_resid = args->count;

		/* Jump to do the read if successful */
		if (!VOP_REQZCBUF(vp, UIO_READ, (xuio_t *)uiop, cs->cr, &ct)) {
			/*
			 * Need to hold the vnode until after VOP_RETZCBUF()
			 * is called.
			 */
			VN_HOLD(vp);
			goto doio_read;
		}

		DTRACE_PROBE2(nfss__i__reqzcbuf_failed, int,
		    uiop->uio_loffset, int, uiop->uio_resid);

		uiop->uio_extflg = 0;

		/* failure to setup for zero copy */
		rfs_free_xuio((void *)uiop);
		loaned_buffers = 0;
	}

	/*
	 * If returning data via RDMA Write, then grab the chunk list. If we
	 * aren't returning READ data w/RDMA_WRITE, then grab a mblk.
	 */
	if (rdma_used && args->wlist != NULL) {
		mp = NULL;
		(void) rdma_get_wchunk(req, &iov, args->wlist);
	} else {
		/*
		 * mp will contain the data to be sent out in the read reply.
		 * It will be freed after the reply has been sent. Let's
		 * roundup the data to a BYTES_PER_XDR_UNIT multiple, so that
		 * the call to xdrmblk_putmblk() never fails. If the first
		 * alloc of the requested size fails, then decrease the size to
		 * something more reasonable and wait for the allocation to
		 * occur.
		 */
		mp = allocb(RNDUP(args->count), BPRI_MED);
		if (mp == NULL) {
			if (args->count > MAXBSIZE)
				args->count = MAXBSIZE;
			mp = allocb_wait(RNDUP(args->count), BPRI_MED,
			    STR_NOSIG, &alloc_err);
		}
		ASSERT(mp != NULL);
		ASSERT(alloc_err == 0);

		iov.iov_base = (caddr_t)mp->b_datap->db_base;
		iov.iov_len = args->count;
	}

	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_extflg = UIO_COPY_CACHED;
	uio.uio_loffset = args->offset;
	uio.uio_resid = args->count;
	uiop = &uio;

doio_read:
	error = do_io(FREAD, vp, uiop, 0, cs->cr, &ct);

	va.va_mask = AT_SIZE;
	verror = VOP_GETATTR(vp, &va, 0, cs->cr, &ct);

	if (error) {
		if (mp)
			freemsg(mp);
		*cs->statusp = resp->status = puterrno4(error);
		goto out;
	}

	/* make mblk using zc buffers */
	if (loaned_buffers) {
		mp = uio_to_mblk(uiop);
		ASSERT(mp != NULL);
	}

	*cs->statusp = resp->status = NFS4_OK;

	ASSERT(uiop->uio_resid >= 0);
	resp->data_len = args->count - uiop->uio_resid;
	if (mp) {
		resp->data_val = (char *)mp->b_datap->db_base;
		rfs_rndup_mblks(mp, resp->data_len, loaned_buffers);
	} else {
		resp->data_val = (caddr_t)iov.iov_base;
	}

	resp->mblk = mp;

	if (!verror && offset + resp->data_len == va.va_size)
		resp->eof = TRUE;
	else
		resp->eof = FALSE;

	if (rdma_used && args->wlist != NULL) {
		if (!rdma_setup_read_data4(args, resp)) {
			*cs->statusp = resp->status = NFS4ERR_INVAL;
		}
	} else {
		resp->wlist = NULL;
	}

out:
	if (in_crit)
		nbl_end_crit(vp);

	DTRACE_NFSV4_2(op__read__done, struct compound_state *, cs,
	    READ4res *, resp);
}

static void
rfs4_op_read_free(nfs_resop4 *resop)
{
	READ4res	*resp = &resop->nfs_resop4_u.opread;

	if (resp->status == NFS4_OK && resp->mblk != NULL) {
		freemsg(resp->mblk);
		resp->mblk = NULL;
		resp->data_val = NULL;
		resp->data_len = 0;
	}
}

static void
rfs4_op_readdir_free(nfs_resop4 * resop)
{
	READDIR4res    *resp = &resop->nfs_resop4_u.opreaddir;

	if (resp->status == NFS4_OK && resp->mblk != NULL) {
		freeb(resp->mblk);
		resp->mblk = NULL;
		resp->data_len = 0;
	}
}


/* ARGSUSED */
static void
rfs4_op_putpubfh(nfs_argop4 *args, nfs_resop4 *resop, struct svc_req *req,
    struct compound_state *cs)
{
	PUTPUBFH4res	*resp = &resop->nfs_resop4_u.opputpubfh;
	vnode_t		*vp;
	struct exportinfo *exi, *sav_exi;

	DTRACE_NFSV4_1(op__putpubfh__start, struct compound_state *, cs);

	if (cs->vp) {
		VN_RELE(cs->vp);
		cs->vp = NULL;
	}

	if (cs->cr)
		crfree(cs->cr);

	cs->cr = crdup(cs->basecr);

	exi = cs->rip->ri_nm.rn_exi_public;
	if (exi == NULL) {
		*cs->statusp = resp->status = NFS4ERR_SERVERFAULT;
		goto out;
	}

	vp = exi->exi_vp;
	if (makefh4(&cs->fh, vp, exi) != 0) {
		*cs->statusp = resp->status = NFS4ERR_SERVERFAULT;
		goto out;
	}

	sav_exi = cs->exi;
	cs->exi = exi;

	if (is_system_labeled()) {
		bslabel_t *clabel;

		ASSERT(req->rq_label != NULL);
		clabel = req->rq_label;
		DTRACE_PROBE2(tx__rfs4__log__info__opputpubfh__clabel, char *,
		    "got client label from request(1)",
		    struct svc_req *, req);
		if (!blequal(&l_admin_low->tsl_label, clabel)) {
			if (!do_rfs_label_check(clabel, vp, DOMINANCE_CHECK,
			    cs->exi)) {
				*cs->statusp = resp->status =
				    NFS4ERR_SERVERFAULT;
				goto out;
			}
		}
	}

	VN_HOLD(vp);
	cs->vp = vp;

	if ((resp->status = call_checkauth4(cs, req)) != NFS4_OK) {
		VN_RELE(cs->vp);
		cs->vp = NULL;
		cs->exi = sav_exi;
		goto out;
	}

	*cs->statusp = resp->status = NFS4_OK;
out:
	DTRACE_NFSV4_2(op__putpubfh__done, struct compound_state *, cs,
	    PUTPUBFH4res *, resp);
}

/*
 * XXX - issue with put*fh operations. Suppose /export/home is exported.
 * Suppose an NFS client goes to mount /export/home/joe. If /export, home,
 * or joe have restrictive search permissions, then we shouldn't let
 * the client get a file handle. This is easy to enforce. However, we
 * don't know what security flavor should be used until we resolve the
 * path name. Another complication is uid mapping. If root is
 * the user, then it will be mapped to the anonymous user by default,
 * but we won't know that till we've resolved the path name. And we won't
 * know what the anonymous user is.
 * Luckily, SECINFO is specified to take a full filename.
 * So what we will have to in rfs4_op_lookup is check that flavor of
 * the target object matches that of the request, and if root was the
 * caller, check for the root= and anon= options, and if necessary,
 * repeat the lookup using the right cred_t. But that's not done yet.
 */
/* ARGSUSED */
static void
rfs4_op_putfh(nfs_argop4 *argop, nfs_resop4 *resop, struct svc_req *req,
    struct compound_state *cs)
{
	PUTFH4args *args = &argop->nfs_argop4_u.opputfh;
	PUTFH4res *resp = &resop->nfs_resop4_u.opputfh;
	nfs_fh4_fmt_t *fh_fmtp;

	DTRACE_NFSV4_2(op__putfh__start, struct compound_state *, cs,
	    PUTFH4args *, args);

	if (cs->vp) {
		VN_RELE(cs->vp);
		cs->vp = NULL;
	}

	if (cs->cr) {
		crfree(cs->cr);
		cs->cr = NULL;
	}


	if (args->object.nfs_fh4_len < NFS_FH4_LEN) {
		*cs->statusp = resp->status = NFS4ERR_BADHANDLE;
		goto out;
	}

	fh_fmtp = (nfs_fh4_fmt_t *)args->object.nfs_fh4_val;
	cs->exi = checkexport4(cs->rip, &fh_fmtp->fh4_fsid,
	    (fid_t *)&fh_fmtp->fh4_xlen, NULL);

	/*
	 * Note that cs->exi can now be out of sync with
	 * cs->fse. That is okay as we will sync them up
	 * once the next op, if any, resets the fse.
	 */
	if (cs->exi == NULL) {
		*cs->statusp = resp->status = NFS4ERR_STALE;
		goto out;
	}

	cs->cr = crdup(cs->basecr);

	ASSERT(cs->cr != NULL);

	if (! (cs->vp = nfs4_fhtovp(&args->object, cs->exi, &resp->status))) {
		*cs->statusp = resp->status;
		goto out;
	}

	if ((resp->status = call_checkauth4(cs, req)) != NFS4_OK) {
		VN_RELE(cs->vp);
		cs->vp = NULL;
		goto out;
	}

	nfs_fh4_copy(&args->object, &cs->fh);
	*cs->statusp = resp->status = NFS4_OK;
	cs->deleg = FALSE;

out:
	DTRACE_NFSV4_2(op__putfh__done, struct compound_state *, cs,
	    PUTFH4res *, resp);
}

/* ARGSUSED */
static void
rfs4_op_putrootfh(nfs_argop4 *argop, nfs_resop4 *resop, struct svc_req *req,
    struct compound_state *cs)
{
	PUTROOTFH4res *resp = &resop->nfs_resop4_u.opputrootfh;
	struct exportinfo *exi, *sav_exi;
	vnode_t *rootvp;
	rfs_namespc_t *rnp = &cs->rip->ri_nm;

	DTRACE_NFSV4_1(op__putrootfh__start, struct compound_state *, cs);

	if (cs->vp) {
		VN_RELE(cs->vp);
		cs->vp = NULL;
	}

	if (cs->cr)
		crfree(cs->cr);

	cs->cr = crdup(cs->basecr);

	exi = rnp->rn_ns_root ? rnp->rn_ns_root->tree_exi : NULL;
	if (exi == NULL) {
		*cs->statusp = resp->status = NFS4ERR_SERVERFAULT;
		goto out;
	}

	rootvp = exi->exi_vp;
	/* Make a filehandle based on the root export and root vnode. */
	if (makefh4(&cs->fh, rootvp, exi) != 0) {
		*cs->statusp = resp->status = NFS4ERR_SERVERFAULT;
		goto out;
	}

	sav_exi = cs->exi;
	cs->exi = exi;

	VN_HOLD(rootvp);
	cs->vp = rootvp;

	if ((resp->status = call_checkauth4(cs, req)) != NFS4_OK) {
		VN_RELE(rootvp);
		cs->vp = NULL;
		cs->exi = sav_exi;
		goto out;
	}

	*cs->statusp = resp->status = NFS4_OK;
	cs->deleg = FALSE;
out:
	DTRACE_NFSV4_2(op__putrootfh__done, struct compound_state *, cs,
	    PUTROOTFH4res *, resp);
}

/*
 * A directory entry is a valid nfsv4 entry if
 * - it has a non-zero ino
 * - it is not a dot or dotdot name
 * - it is visible in a pseudo export or in a real export that can
 *   only have a limited view.
 */
static bool_t
valid_nfs4_entry(struct exportinfo *exi, struct dirent64 *dp,
    int *expseudo, int check_visible)
{
	if (dp->d_ino == 0 || NFS_IS_DOTNAME(dp->d_name)) {
		*expseudo = 0;
		return (FALSE);
	}

	if (! check_visible) {
		*expseudo = 0;
		return (TRUE);
	}

	return (nfs_visible_inode(exi, dp->d_ino, expseudo));
}

/*
 * set_rdattr_params sets up the variables used to manage what information
 * to get for each directory entry.
 */
static nfsstat4
set_rdattr_params(struct nfs4_svgetit_arg *sargp,
    bitmap4 attrs, bool_t *need_to_lookup)
{
	uint_t	va_mask;
	nfsstat4 status;
	bitmap4 objbits;

	status = bitmap4_to_attrmask(attrs, sargp);
	if (status != NFS4_OK) {
		/*
		 * could not even figure attr mask
		 */
		return (status);
	}
	va_mask = sargp->vap->va_mask;

	/*
	 * dirent's d_ino is always correct value for mounted_on_fileid.
	 * mntdfid_set is set once here, but mounted_on_fileid is
	 * set in main dirent processing loop for each dirent.
	 * The mntdfid_set is a simple optimization that lets the
	 * server attr code avoid work when caller is readdir.
	 */
	sargp->mntdfid_set = TRUE;

	/*
	 * Lookup entry only if client asked for any of the following:
	 * a) vattr attrs
	 * b) vfs attrs
	 * c) attrs w/per-object scope requested (change, filehandle, etc)
	 *    other than mounted_on_fileid (which we can take from dirent)
	 */
	objbits = attrs ? attrs & NFS4_VP_ATTR_MASK : 0;

	if (va_mask || sargp->sbp || (objbits & ~FATTR4_MOUNTED_ON_FILEID_MASK))
		*need_to_lookup = TRUE;
	else
		*need_to_lookup = FALSE;

	if (sargp->sbp == NULL)
		return (NFS4_OK);

	/*
	 * If filesystem attrs are requested, get them now from the
	 * directory vp, as most entries will have same filesystem. The only
	 * exception are mounted over entries but we handle
	 * those as we go (XXX mounted over detection not yet implemented).
	 */
	sargp->vap->va_mask = 0;	/* to avoid VOP_GETATTR */
	status = bitmap4_get_sysattrs(sargp);
	sargp->vap->va_mask = va_mask;

	if ((status != NFS4_OK) && sargp->rdattr_error_req) {
		/*
		 * Failed to get filesystem attributes.
		 * Return a rdattr_error for each entry, but don't fail.
		 * However, don't get any obj-dependent attrs.
		 */
		sargp->rdattr_error = status;	/* for rdattr_error */
		*need_to_lookup = FALSE;
		/*
		 * At least get fileid for regular readdir output
		 */
		sargp->vap->va_mask &= AT_NODEID;
		status = NFS4_OK;
	}

	return (status);
}

/*
 * readlink: args: CURRENT_FH.
 *	res: status. If success - CURRENT_FH unchanged, return linktext.
 */

/* ARGSUSED */
static void
rfs4_op_readlink(nfs_argop4 *argop, nfs_resop4 *resop, struct svc_req *req,
    struct compound_state *cs)
{
	READLINK4res *resp = &resop->nfs_resop4_u.opreadlink;
	int error;
	vnode_t *vp;
	struct iovec iov;
	struct vattr va;
	struct uio uio;
	char *data;
	struct sockaddr *ca;
	char *name = NULL;
	int is_referral;

	DTRACE_NFSV4_1(op__readlink__start, struct compound_state *, cs);

	/* CURRENT_FH: directory */
	vp = cs->vp;
	if (vp == NULL) {
		*cs->statusp = resp->status = NFS4ERR_NOFILEHANDLE;
		goto out;
	}

	if (cs->access == CS_ACCESS_DENIED) {
		*cs->statusp = resp->status = NFS4ERR_ACCESS;
		goto out;
	}

	/* Is it a referral? */
	if (vn_is_nfs_reparse(vp, cs->cr) && client_is_downrev(req)) {

		is_referral = 1;

	} else {

		is_referral = 0;

		if (vp->v_type == VDIR) {
			*cs->statusp = resp->status = NFS4ERR_ISDIR;
			goto out;
		}

		if (vp->v_type != VLNK) {
			*cs->statusp = resp->status = NFS4ERR_INVAL;
			goto out;
		}

	}

	va.va_mask = AT_MODE;
	error = VOP_GETATTR(vp, &va, 0, cs->cr, NULL);
	if (error) {
		*cs->statusp = resp->status = puterrno4(error);
		goto out;
	}

	if (MANDLOCK(vp, va.va_mode)) {
		*cs->statusp = resp->status = NFS4ERR_ACCESS;
		goto out;
	}

	data = kmem_alloc(MAXPATHLEN + 1, KM_SLEEP);

	if (is_referral) {
		char *s;
		size_t strsz;

		/* Get an artificial symlink based on a referral */
		s = build_symlink(cs->rip, vp, cs->cr, &strsz);

		nfsstat_inc_svstat(cs->rip, NFS_V4, NFS_REFERLINKS);
		nfsstat_inc_svstat(cs->rip, 0, NFS_REFERLINKS);
		DTRACE_PROBE2(nfs4serv__func__referral__reflink,
		    vnode_t *, vp, char *, s);
		if (s == NULL)
			error = EINVAL;
		else {
			error = 0;
			(void) strlcpy(data, s, MAXPATHLEN + 1);
			kmem_free(s, strsz);
		}

	} else {

		iov.iov_base = data;
		iov.iov_len = MAXPATHLEN;
		uio.uio_iov = &iov;
		uio.uio_iovcnt = 1;
		uio.uio_segflg = UIO_SYSSPACE;
		uio.uio_extflg = UIO_COPY_CACHED;
		uio.uio_loffset = 0;
		uio.uio_resid = MAXPATHLEN;

		error = VOP_READLINK(vp, &uio, cs->cr, NULL);

		if (!error)
			*(data + MAXPATHLEN - uio.uio_resid) = '\0';
	}

	if (error) {
		kmem_free((caddr_t)data, (uint_t)MAXPATHLEN + 1);
		*cs->statusp = resp->status = puterrno4(error);
		goto out;
	}

	ca = (struct sockaddr *)svc_getrpccaller(req->rq_xprt)->buf;
	name = nfscmd_convname(ca, cs->exi, data, NFSCMD_CONV_OUTBOUND,
	    MAXPATHLEN  + 1);

	if (name == NULL) {
		/*
		 * Even though the conversion failed, we return
		 * something. We just don't translate it.
		 */
		name = data;
	}

	/*
	 * treat link name as data
	 */
	(void) str_to_utf8(name, &resp->link);

	if (name != data)
		kmem_free(name, MAXPATHLEN + 1);
	kmem_free((caddr_t)data, (uint_t)MAXPATHLEN + 1);
	*cs->statusp = resp->status = NFS4_OK;

out:
	DTRACE_NFSV4_2(op__readlink__done, struct compound_state *, cs,
	    READLINK4res *, resp);
}

static void
rfs4_op_readlink_free(nfs_resop4 *resop)
{
	READLINK4res *resp = &resop->nfs_resop4_u.opreadlink;
	utf8string *symlink = &resp->link;

	if (symlink->utf8string_val) {
		UTF8STRING_FREE(*symlink)
	}
}

/*
 * release_lockowner:
 *	Release any state associated with the supplied
 *	lockowner. Note if any lo_state is holding locks we will not
 *	rele that lo_state and thus the lockowner will not be destroyed.
 *	A client using lock after the lock owner stateid has been released
 *	will suffer the consequence of NFS4ERR_BAD_STATEID and would have
 *	to reissue the lock with new_lock_owner set to TRUE.
 *	args: lock_owner
 *	res:  status
 */
/* ARGSUSED */
static void
rfs4_op_release_lockowner(nfs_argop4 *argop, nfs_resop4 *resop,
    struct svc_req *req, struct compound_state *cs)
{
	RELEASE_LOCKOWNER4args *ap = &argop->nfs_argop4_u.oprelease_lockowner;
	RELEASE_LOCKOWNER4res *resp = &resop->nfs_resop4_u.oprelease_lockowner;
	rfs4_lockowner_t *lo;
	rfs4_openowner_t *oo;
	rfs4_state_t *sp;
	rfs4_lo_state_t *lsp;
	rfs4_client_t *cp;
	bool_t create = FALSE;
	locklist_t *llist;
	sysid_t sysid;

	DTRACE_NFSV4_2(op__release__lockowner__start, struct compound_state *,
	    cs, RELEASE_LOCKOWNER4args *, ap);

	/* Make sure there is a clientid around for this request */
	cp = rfs4_findclient_by_id(cs->rip, ap->lock_owner.clientid, FALSE);

	if (cp == NULL) {
		*cs->statusp = resp->status =
		    rfs4_check_clientid(cs->rip, &ap->lock_owner.clientid, 0);
		goto out;
	}
	rfs4_client_rele(cp);

	lo = rfs4_findlockowner(cs->rip, &ap->lock_owner, &create);
	if (lo == NULL) {
		*cs->statusp = resp->status = NFS4_OK;
		goto out;
	}
	ASSERT(lo->rl_client != NULL);

	/*
	 * Check for EXPIRED client. If so will reap state with in a lease
	 * period or on next set_clientid_confirm step
	 */
	if (rfs4_lease_expired(lo->rl_client)) {
		rfs4_lockowner_rele(lo);
		*cs->statusp = resp->status = NFS4ERR_EXPIRED;
		goto out;
	}

	if (rfs4_lease_moved(lo->rl_client, NULL)) {
		rfs4_lockowner_rele(lo);
		*cs->statusp = resp->status = NFS4ERR_LEASE_MOVED;
		goto out;
	}

	/*
	 * If no sysid has been assigned, then no locks exist; just return.
	 */
	rfs4_dbe_lock(lo->rl_client->rc_dbe);
	if (lo->rl_client->rc_sysidt == LM_NOSYSID) {
		rfs4_lockowner_rele(lo);
		rfs4_dbe_unlock(lo->rl_client->rc_dbe);
		goto out;
	}

	sysid = lo->rl_client->rc_sysidt;
	rfs4_dbe_unlock(lo->rl_client->rc_dbe);

	/*
	 * Mark the lockowner invalid.
	 */
	rfs4_dbe_hide(lo->rl_dbe);

	/*
	 * sysid-pid pair should now not be used since the lockowner is
	 * invalid. If the client were to instantiate the lockowner again
	 * it would be assigned a new pid. Thus we can get the list of
	 * current locks.
	 */

	llist = flk_get_active_locks(sysid, lo->rl_pid);
	/* If we are still holding locks fail */
	if (llist != NULL) {

		*cs->statusp = resp->status = NFS4ERR_LOCKS_HELD;

		flk_free_locklist(llist);
		/*
		 * We need to unhide the lockowner so the client can
		 * try it again. The bad thing here is if the client
		 * has a logic error that took it here in the first place
		 * he probably has lost accounting of the locks that it
		 * is holding. So we may have dangling state until the
		 * open owner state is reaped via close. One scenario
		 * that could possibly occur is that the client has
		 * sent the unlock request(s) in separate threads
		 * and has not waited for the replies before sending the
		 * RELEASE_LOCKOWNER request. Presumably, it would expect
		 * and deal appropriately with NFS4ERR_LOCKS_HELD, by
		 * reissuing the request.
		 */
		rfs4_dbe_unhide(lo->rl_dbe);
		rfs4_lockowner_rele(lo);
		goto out;
	}

	/*
	 * For the corresponding client we need to check each open
	 * owner for any opens that have lockowner state associated
	 * with this lockowner.
	 */

	rfs4_dbe_lock(lo->rl_client->rc_dbe);
	for (oo = list_head(&lo->rl_client->rc_openownerlist); oo != NULL;
	    oo = list_next(&lo->rl_client->rc_openownerlist, oo)) {

		rfs4_dbe_lock(oo->ro_dbe);
		for (sp = list_head(&oo->ro_statelist); sp != NULL;
		    sp = list_next(&oo->ro_statelist, sp)) {

			rfs4_dbe_lock(sp->rs_dbe);
			for (lsp = list_head(&sp->rs_lostatelist);
			    lsp != NULL;
			    lsp = list_next(&sp->rs_lostatelist, lsp)) {
				if (lsp->rls_locker == lo) {
					rfs4_dbe_lock(lsp->rls_dbe);
					rfs4_dbe_invalidate(lsp->rls_dbe);
					rfs4_dbe_unlock(lsp->rls_dbe);
				}
			}
			rfs4_dbe_unlock(sp->rs_dbe);
		}
		rfs4_dbe_unlock(oo->ro_dbe);
	}
	rfs4_dbe_unlock(lo->rl_client->rc_dbe);

	rfs4_lockowner_rele(lo);

	*cs->statusp = resp->status = NFS4_OK;

out:
	DTRACE_NFSV4_2(op__release__lockowner__done, struct compound_state *,
	    cs, RELEASE_LOCKOWNER4res *, resp);
}

/*
 * short utility function to lookup a file and recall the delegation
 */
static rfs4_file_t *
rfs4_lookup_and_findfile(fsh_entry_t *fse, vnode_t *dvp, char *nm,
    vnode_t **vpp, int *lkup_error, cred_t *cr)
{
	vnode_t *vp;
	rfs4_file_t *fp = NULL;
	bool_t fcreate = FALSE;
	int error;

	if (vpp)
		*vpp = NULL;

	if ((error = VOP_LOOKUP(dvp, nm, &vp, NULL, 0, NULL, cr, NULL, NULL,
	    NULL)) == 0) {
		if (vp->v_type == VREG)
			fp = rfs4_findfile(fse, vp, NULL, &fcreate);
		if (vpp)
			*vpp = vp;
		else
			VN_RELE(vp);
	}

	if (lkup_error)
		*lkup_error = error;

	return (fp);
}

/*
 * remove: args: CURRENT_FH: directory; name.
 *	res: status. If success - CURRENT_FH unchanged, return change_info
 *		for directory.
 */
/* ARGSUSED */
static void
rfs4_op_remove(nfs_argop4 *argop, nfs_resop4 *resop, struct svc_req *req,
    struct compound_state *cs)
{
	REMOVE4args *args = &argop->nfs_argop4_u.opremove;
	REMOVE4res *resp = &resop->nfs_resop4_u.opremove;
	int error;
	vnode_t *dvp, *vp;
	struct vattr bdva, idva, adva;
	char *nm;
	uint_t len;
	rfs4_file_t *fp;
	int in_crit = 0;
	bslabel_t *clabel;
	struct sockaddr *ca;
	char *name = NULL;

	DTRACE_NFSV4_2(op__remove__start, struct compound_state *, cs,
	    REMOVE4args *, args);

	/* CURRENT_FH: directory */
	dvp = cs->vp;
	if (dvp == NULL) {
		*cs->statusp = resp->status = NFS4ERR_NOFILEHANDLE;
		goto out;
	}

	if (cs->access == CS_ACCESS_DENIED) {
		*cs->statusp = resp->status = NFS4ERR_ACCESS;
		goto out;
	}

	/*
	 * If there is an unshared filesystem mounted on this vnode,
	 * Do not allow to remove anything in this directory.
	 */
	if (vn_ismntpt(dvp)) {
		*cs->statusp = resp->status = NFS4ERR_ACCESS;
		goto out;
	}

	if (dvp->v_type != VDIR) {
		*cs->statusp = resp->status = NFS4ERR_NOTDIR;
		goto out;
	}

	if (!utf8_dir_verify(&args->target)) {
		*cs->statusp = resp->status = NFS4ERR_INVAL;
		goto out;
	}

	/*
	 * Lookup the file so that we can check if it's a directory
	 */
	nm = utf8_to_fn(&args->target, &len, NULL);
	if (nm == NULL) {
		*cs->statusp = resp->status = NFS4ERR_INVAL;
		goto out;
	}

	if (len > MAXNAMELEN) {
		*cs->statusp = resp->status = NFS4ERR_NAMETOOLONG;
		kmem_free(nm, len);
		goto out;
	}

	if (rdonly4(cs->exi, cs->vp, req)) {
		*cs->statusp = resp->status = NFS4ERR_ROFS;
		kmem_free(nm, len);
		goto out;
	}

	ca = (struct sockaddr *)svc_getrpccaller(req->rq_xprt)->buf;
	name = nfscmd_convname(ca, cs->exi, nm, NFSCMD_CONV_INBOUND,
	    MAXPATHLEN  + 1);

	if (name == NULL) {
		*cs->statusp = resp->status = NFS4ERR_INVAL;
		kmem_free(nm, len);
		goto out;
	}

	/*
	 * Lookup the file to determine type and while we are see if
	 * there is a file struct around and check for delegation.
	 * We don't need to acquire va_seq before this lookup, if
	 * it causes an update, cinfo.before will not match, which will
	 * trigger a cache flush even if atomic is TRUE.
	 */
	if (fp = rfs4_lookup_and_findfile(cs->fse, dvp, name, &vp, &error,
	    cs->cr)) {
		if (rfs4_check_delegated_byfp(FWRITE, fp, TRUE, TRUE, TRUE,
		    NULL)) {
			VN_RELE(vp);
			rfs4_file_rele(fp);
			*cs->statusp = resp->status = NFS4ERR_DELAY;
			if (nm != name)
				kmem_free(name, MAXPATHLEN + 1);
			kmem_free(nm, len);
			goto out;
		}
	}

	/* Didn't find anything to remove */
	if (vp == NULL) {
		*cs->statusp = resp->status = error;
		if (nm != name)
			kmem_free(name, MAXPATHLEN + 1);
		kmem_free(nm, len);
		goto out;
	}

	if (nbl_need_check(vp)) {
		nbl_start_crit(vp, RW_READER);
		in_crit = 1;
		if (nbl_conflict(vp, NBL_REMOVE, 0, 0, 0, NULL)) {
			*cs->statusp = resp->status = NFS4ERR_FILE_OPEN;
			if (nm != name)
				kmem_free(name, MAXPATHLEN + 1);
			kmem_free(nm, len);
			nbl_end_crit(vp);
			VN_RELE(vp);
			if (fp) {
				rfs4_clear_dont_grant(fp);
				rfs4_file_rele(fp);
			}
			goto out;
		}
	}

	/* check label before allowing removal */
	if (is_system_labeled()) {
		ASSERT(req->rq_label != NULL);
		clabel = req->rq_label;
		DTRACE_PROBE2(tx__rfs4__log__info__opremove__clabel, char *,
		    "got client label from request(1)",
		    struct svc_req *, req);
		if (!blequal(&l_admin_low->tsl_label, clabel)) {
			if (!do_rfs_label_check(clabel, vp, EQUALITY_CHECK,
			    cs->exi)) {
				*cs->statusp = resp->status = NFS4ERR_ACCESS;
				if (name != nm)
					kmem_free(name, MAXPATHLEN + 1);
				kmem_free(nm, len);
				if (in_crit)
					nbl_end_crit(vp);
				VN_RELE(vp);
				if (fp) {
					rfs4_clear_dont_grant(fp);
					rfs4_file_rele(fp);
				}
				goto out;
			}
		}
	}

	/* Get dir "before" change value */
	bdva.va_mask = AT_CTIME|AT_SEQ;
	error = VOP_GETATTR(dvp, &bdva, 0, cs->cr, NULL);
	if (error) {
		*cs->statusp = resp->status = puterrno4(error);
		if (nm != name)
			kmem_free(name, MAXPATHLEN + 1);
		kmem_free(nm, len);
		if (in_crit)
			nbl_end_crit(vp);
		VN_RELE(vp);
		if (fp) {
			rfs4_clear_dont_grant(fp);
			rfs4_file_rele(fp);
		}
		goto out;
	}
	NFS4_SET_FATTR4_CHANGE(resp->cinfo.before, bdva.va_ctime)

	/* Actually do the REMOVE operation */
	if (vp->v_type == VDIR) {
		/*
		 * Can't remove a directory that has a mounted-on filesystem.
		 */
		if (vn_ismntpt(vp)) {
			error = EACCES;
		} else {
			/*
			 * System V defines rmdir to return EEXIST,
			 * not ENOTEMPTY, if the directory is not
			 * empty.  A System V NFS server needs to map
			 * NFS4ERR_EXIST to NFS4ERR_NOTEMPTY to
			 * transmit over the wire.
			 */
			if ((error = VOP_RMDIR(dvp, name,
			    RFS_INST_ROOTVP(cs->rip), cs->cr, NULL, 0)) ==
			    EEXIST)
				error = ENOTEMPTY;
		}
	} else {
		if ((error = VOP_REMOVE(dvp, name, cs->cr, NULL, 0)) == 0 &&
		    fp != NULL) {
			struct vattr va;
			vnode_t *tvp;

			rfs4_dbe_lock(fp->rf_dbe);
			tvp = fp->rf_vp;
			if (tvp)
				VN_HOLD(tvp);
			rfs4_dbe_unlock(fp->rf_dbe);

			if (tvp) {
				/*
				 * This is va_seq safe because we are not
				 * manipulating dvp.
				 */
				va.va_mask = AT_NLINK;
				if (!VOP_GETATTR(tvp, &va, 0, cs->cr, NULL) &&
				    va.va_nlink == 0) {
					/* Remove state on file remove */
					if (in_crit) {
						nbl_end_crit(vp);
						in_crit = 0;
					}
					rfs4_close_all_state(cs, fp);
				}
				VN_RELE(tvp);
			}
		}
	}

	if (in_crit)
		nbl_end_crit(vp);
	VN_RELE(vp);

	if (fp) {
		rfs4_clear_dont_grant(fp);
		rfs4_file_rele(fp);
	}
	if (nm != name)
		kmem_free(name, MAXPATHLEN + 1);
	kmem_free(nm, len);

	if (error) {
		*cs->statusp = resp->status = puterrno4(error);
		goto out;
	}

	/*
	 * Get the initial "after" sequence number, if it fails, set to zero
	 */
	idva.va_mask = AT_SEQ;
	if (VOP_GETATTR(dvp, &idva, 0, cs->cr, NULL))
		idva.va_seq = 0;

	/*
	 * Force modified data and metadata out to stable storage.
	 */
	(void) VOP_FSYNC(dvp, 0, cs->cr, NULL);

	/*
	 * Get "after" change value, if it fails, simply return the
	 * before value.
	 */
	adva.va_mask = AT_CTIME|AT_SEQ;
	if (VOP_GETATTR(dvp, &adva, 0, cs->cr, NULL)) {
		adva.va_ctime = bdva.va_ctime;
		adva.va_seq = 0;
	}

	NFS4_SET_FATTR4_CHANGE(resp->cinfo.after, adva.va_ctime)

	/*
	 * The cinfo.atomic = TRUE only if we have
	 * non-zero va_seq's, and it has incremented by exactly one
	 * during the VOP_REMOVE/RMDIR and it didn't change during
	 * the VOP_FSYNC.
	 */
	if (bdva.va_seq && idva.va_seq && adva.va_seq &&
	    idva.va_seq == (bdva.va_seq + 1) && idva.va_seq == adva.va_seq)
		resp->cinfo.atomic = TRUE;
	else
		resp->cinfo.atomic = FALSE;

	*cs->statusp = resp->status = NFS4_OK;

out:
	DTRACE_NFSV4_2(op__remove__done, struct compound_state *, cs,
	    REMOVE4res *, resp);
}

/*
 * rename: args: SAVED_FH: from directory, CURRENT_FH: target directory,
 *		oldname and newname.
 *	res: status. If success - CURRENT_FH unchanged, return change_info
 *		for both from and target directories.
 */
/* ARGSUSED */
static void
rfs4_op_rename(nfs_argop4 *argop, nfs_resop4 *resop, struct svc_req *req,
    struct compound_state *cs)
{
	RENAME4args *args = &argop->nfs_argop4_u.oprename;
	RENAME4res *resp = &resop->nfs_resop4_u.oprename;
	int error;
	vnode_t *odvp;
	vnode_t *ndvp;
	vnode_t *srcvp, *targvp;
	struct vattr obdva, oidva, oadva;
	struct vattr nbdva, nidva, nadva;
	char *onm, *nnm;
	uint_t olen, nlen;
	rfs4_file_t *fp, *sfp;
	int in_crit_src, in_crit_targ;
	int fp_rele_grant_hold, sfp_rele_grant_hold;
	bslabel_t *clabel;
	struct sockaddr *ca;
	char *converted_onm = NULL;
	char *converted_nnm = NULL;

	DTRACE_NFSV4_2(op__rename__start, struct compound_state *, cs,
	    RENAME4args *, args);

	fp = sfp = NULL;
	srcvp = targvp = NULL;
	in_crit_src = in_crit_targ = 0;
	fp_rele_grant_hold = sfp_rele_grant_hold = 0;

	/* CURRENT_FH: target directory */
	ndvp = cs->vp;
	if (ndvp == NULL) {
		*cs->statusp = resp->status = NFS4ERR_NOFILEHANDLE;
		goto out;
	}

	/* SAVED_FH: from directory */
	odvp = cs->saved_vp;
	if (odvp == NULL) {
		*cs->statusp = resp->status = NFS4ERR_NOFILEHANDLE;
		goto out;
	}

	if (cs->access == CS_ACCESS_DENIED) {
		*cs->statusp = resp->status = NFS4ERR_ACCESS;
		goto out;
	}

	/*
	 * If there is an unshared filesystem mounted on this vnode,
	 * do not allow to rename objects in this directory.
	 */
	if (vn_ismntpt(odvp)) {
		*cs->statusp = resp->status = NFS4ERR_ACCESS;
		goto out;
	}

	/*
	 * If there is an unshared filesystem mounted on this vnode,
	 * do not allow to rename to this directory.
	 */
	if (vn_ismntpt(ndvp)) {
		*cs->statusp = resp->status = NFS4ERR_ACCESS;
		goto out;
	}

	if (odvp->v_type != VDIR || ndvp->v_type != VDIR) {
		*cs->statusp = resp->status = NFS4ERR_NOTDIR;
		goto out;
	}

	if (cs->saved_exi != cs->exi) {
		*cs->statusp = resp->status = NFS4ERR_XDEV;
		goto out;
	}

	if (!utf8_dir_verify(&args->oldname)) {
		*cs->statusp = resp->status = NFS4ERR_INVAL;
		goto out;
	}

	if (!utf8_dir_verify(&args->newname)) {
		*cs->statusp = resp->status = NFS4ERR_INVAL;
		goto out;
	}

	onm = utf8_to_fn(&args->oldname, &olen, NULL);
	if (onm == NULL) {
		*cs->statusp = resp->status = NFS4ERR_INVAL;
		goto out;
	}
	ca = (struct sockaddr *)svc_getrpccaller(req->rq_xprt)->buf;
	nlen = MAXPATHLEN + 1;
	converted_onm = nfscmd_convname(ca, cs->exi, onm, NFSCMD_CONV_INBOUND,
	    nlen);

	if (converted_onm == NULL) {
		*cs->statusp = resp->status = NFS4ERR_INVAL;
		kmem_free(onm, olen);
		goto out;
	}

	nnm = utf8_to_fn(&args->newname, &nlen, NULL);
	if (nnm == NULL) {
		*cs->statusp = resp->status = NFS4ERR_INVAL;
		if (onm != converted_onm)
			kmem_free(converted_onm, MAXPATHLEN + 1);
		kmem_free(onm, olen);
		goto out;
	}
	converted_nnm = nfscmd_convname(ca, cs->exi, nnm, NFSCMD_CONV_INBOUND,
	    MAXPATHLEN  + 1);

	if (converted_nnm == NULL) {
		*cs->statusp = resp->status = NFS4ERR_INVAL;
		kmem_free(nnm, nlen);
		nnm = NULL;
		if (onm != converted_onm)
			kmem_free(converted_onm, MAXPATHLEN + 1);
		kmem_free(onm, olen);
		goto out;
	}


	if (olen > MAXNAMELEN || nlen > MAXNAMELEN) {
		*cs->statusp = resp->status = NFS4ERR_NAMETOOLONG;
		kmem_free(onm, olen);
		kmem_free(nnm, nlen);
		goto out;
	}


	if (rdonly4(cs->exi, cs->vp, req)) {
		*cs->statusp = resp->status = NFS4ERR_ROFS;
		if (onm != converted_onm)
			kmem_free(converted_onm, MAXPATHLEN + 1);
		kmem_free(onm, olen);
		if (nnm != converted_nnm)
			kmem_free(converted_nnm, MAXPATHLEN + 1);
		kmem_free(nnm, nlen);
		goto out;
	}

	/* check label of the target dir */
	if (is_system_labeled()) {
		ASSERT(req->rq_label != NULL);
		clabel = req->rq_label;
		DTRACE_PROBE2(tx__rfs4__log__info__oprename__clabel, char *,
		    "got client label from request(1)",
		    struct svc_req *, req);
		if (!blequal(&l_admin_low->tsl_label, clabel)) {
			if (!do_rfs_label_check(clabel, ndvp,
			    EQUALITY_CHECK, cs->exi)) {
				*cs->statusp = resp->status = NFS4ERR_ACCESS;
				goto err_out;
			}
		}
	}

	/*
	 * Is the source a file and have a delegation?
	 * We don't need to acquire va_seq before these lookups, if
	 * it causes an update, cinfo.before will not match, which will
	 * trigger a cache flush even if atomic is TRUE.
	 */
	if (sfp = rfs4_lookup_and_findfile(cs->fse, odvp, converted_onm, &srcvp,
	    &error, cs->cr)) {
		if (rfs4_check_delegated_byfp(FWRITE, sfp, TRUE, TRUE, TRUE,
		    NULL)) {
			*cs->statusp = resp->status = NFS4ERR_DELAY;
			goto err_out;
		}
	}

	if (srcvp == NULL) {
		*cs->statusp = resp->status = puterrno4(error);
		if (onm != converted_onm)
			kmem_free(converted_onm, MAXPATHLEN + 1);
		kmem_free(onm, olen);
		if (nnm != converted_nnm)
			kmem_free(converted_nnm, MAXPATHLEN + 1);
		kmem_free(nnm, nlen);
		goto out;
	}

	sfp_rele_grant_hold = 1;

	/* Does the destination exist and a file and have a delegation? */
	if (fp = rfs4_lookup_and_findfile(cs->fse, ndvp, converted_nnm, &targvp,
	    NULL, cs->cr)) {
		if (rfs4_check_delegated_byfp(FWRITE, fp, TRUE, TRUE, TRUE,
		    NULL)) {
			*cs->statusp = resp->status = NFS4ERR_DELAY;
			goto err_out;
		}
	}
	fp_rele_grant_hold = 1;


	/* Check for NBMAND lock on both source and target */
	if (nbl_need_check(srcvp)) {
		nbl_start_crit(srcvp, RW_READER);
		in_crit_src = 1;
		if (nbl_conflict(srcvp, NBL_RENAME, 0, 0, 0, NULL)) {
			*cs->statusp = resp->status = NFS4ERR_FILE_OPEN;
			goto err_out;
		}
	}

	if (targvp && nbl_need_check(targvp)) {
		nbl_start_crit(targvp, RW_READER);
		in_crit_targ = 1;
		if (nbl_conflict(targvp, NBL_REMOVE, 0, 0, 0, NULL)) {
			*cs->statusp = resp->status = NFS4ERR_FILE_OPEN;
			goto err_out;
		}
	}

	/* Get source "before" change value */
	obdva.va_mask = AT_CTIME|AT_SEQ;
	error = VOP_GETATTR(odvp, &obdva, 0, cs->cr, NULL);
	if (!error) {
		nbdva.va_mask = AT_CTIME|AT_SEQ;
		error = VOP_GETATTR(ndvp, &nbdva, 0, cs->cr, NULL);
	}
	if (error) {
		*cs->statusp = resp->status = puterrno4(error);
		goto err_out;
	}

	NFS4_SET_FATTR4_CHANGE(resp->source_cinfo.before, obdva.va_ctime)
	NFS4_SET_FATTR4_CHANGE(resp->target_cinfo.before, nbdva.va_ctime)

	if ((error = VOP_RENAME(odvp, converted_onm, ndvp, converted_nnm,
	    cs->cr, NULL, 0)) == 0 && fp != NULL) {
		struct vattr va;
		vnode_t *tvp;

		rfs4_dbe_lock(fp->rf_dbe);
		tvp = fp->rf_vp;
		if (tvp)
			VN_HOLD(tvp);
		rfs4_dbe_unlock(fp->rf_dbe);

		if (tvp) {
			va.va_mask = AT_NLINK;
			if (!VOP_GETATTR(tvp, &va, 0, cs->cr, NULL) &&
			    va.va_nlink == 0) {
				/* The file is gone and so should the state */
				if (in_crit_targ) {
					nbl_end_crit(targvp);
					in_crit_targ = 0;
				}
				rfs4_close_all_state(cs, fp);
			}
			VN_RELE(tvp);
		}
	}
	if (error == 0)
		vn_renamepath(ndvp, srcvp, nnm, nlen - 1);

	if (in_crit_src)
		nbl_end_crit(srcvp);
	if (srcvp)
		VN_RELE(srcvp);
	if (in_crit_targ)
		nbl_end_crit(targvp);
	if (targvp)
		VN_RELE(targvp);

	if (sfp) {
		rfs4_clear_dont_grant(sfp);
		rfs4_file_rele(sfp);
	}
	if (fp) {
		rfs4_clear_dont_grant(fp);
		rfs4_file_rele(fp);
	}

	if (converted_onm != onm)
		kmem_free(converted_onm, MAXPATHLEN + 1);
	kmem_free(onm, olen);
	if (converted_nnm != nnm)
		kmem_free(converted_nnm, MAXPATHLEN + 1);
	kmem_free(nnm, nlen);

	/*
	 * Get the initial "after" sequence number, if it fails, set to zero
	 */
	oidva.va_mask = AT_SEQ;
	if (VOP_GETATTR(odvp, &oidva, 0, cs->cr, NULL))
		oidva.va_seq = 0;

	nidva.va_mask = AT_SEQ;
	if (VOP_GETATTR(ndvp, &nidva, 0, cs->cr, NULL))
		nidva.va_seq = 0;

	/*
	 * Force modified data and metadata out to stable storage.
	 */
	(void) VOP_FSYNC(odvp, 0, cs->cr, NULL);
	(void) VOP_FSYNC(ndvp, 0, cs->cr, NULL);

	if (error) {
		*cs->statusp = resp->status = puterrno4(error);
		goto out;
	}

	/*
	 * Get "after" change values, if it fails, simply return the
	 * before value.
	 */
	oadva.va_mask = AT_CTIME|AT_SEQ;
	if (VOP_GETATTR(odvp, &oadva, 0, cs->cr, NULL)) {
		oadva.va_ctime = obdva.va_ctime;
		oadva.va_seq = 0;
	}

	nadva.va_mask = AT_CTIME|AT_SEQ;
	if (VOP_GETATTR(odvp, &nadva, 0, cs->cr, NULL)) {
		nadva.va_ctime = nbdva.va_ctime;
		nadva.va_seq = 0;
	}

	NFS4_SET_FATTR4_CHANGE(resp->source_cinfo.after, oadva.va_ctime)
	NFS4_SET_FATTR4_CHANGE(resp->target_cinfo.after, nadva.va_ctime)

	/*
	 * The cinfo.atomic = TRUE only if we have
	 * non-zero va_seq's, and it has incremented by exactly one
	 * during the VOP_RENAME and it didn't change during the VOP_FSYNC.
	 */
	if (obdva.va_seq && oidva.va_seq && oadva.va_seq &&
	    oidva.va_seq == (obdva.va_seq + 1) && oidva.va_seq == oadva.va_seq)
		resp->source_cinfo.atomic = TRUE;
	else
		resp->source_cinfo.atomic = FALSE;

	if (nbdva.va_seq && nidva.va_seq && nadva.va_seq &&
	    nidva.va_seq == (nbdva.va_seq + 1) && nidva.va_seq == nadva.va_seq)
		resp->target_cinfo.atomic = TRUE;
	else
		resp->target_cinfo.atomic = FALSE;

#ifdef	VOLATILE_FH_TEST
	{
	extern void add_volrnm_fh(struct exportinfo *, vnode_t *);

	/*
	 * Add the renamed file handle to the volatile rename list
	 */
	if (cs->exi->exi_export.ex_flags & EX_VOLRNM) {
		/* file handles may expire on rename */
		vnode_t *vp;

		nnm = utf8_to_fn(&args->newname, &nlen, NULL);
		/*
		 * Already know that nnm will be a valid string
		 */
		error = VOP_LOOKUP(ndvp, nnm, &vp, NULL, 0, NULL, cs->cr,
		    NULL, NULL, NULL);
		kmem_free(nnm, nlen);
		if (!error) {
			add_volrnm_fh(cs->exi, vp);
			VN_RELE(vp);
		}
	}
	}
#endif	/* VOLATILE_FH_TEST */

	*cs->statusp = resp->status = NFS4_OK;
out:
	DTRACE_NFSV4_2(op__rename__done, struct compound_state *, cs,
	    RENAME4res *, resp);
	return;

err_out:
	if (onm != converted_onm)
		kmem_free(converted_onm, MAXPATHLEN + 1);
	if (onm != NULL)
		kmem_free(onm, olen);
	if (nnm != converted_nnm)
		kmem_free(converted_nnm, MAXPATHLEN + 1);
	if (nnm != NULL)
		kmem_free(nnm, nlen);

	if (in_crit_src) nbl_end_crit(srcvp);
	if (in_crit_targ) nbl_end_crit(targvp);
	if (targvp) VN_RELE(targvp);
	if (srcvp) VN_RELE(srcvp);
	if (sfp) {
		if (sfp_rele_grant_hold) rfs4_clear_dont_grant(sfp);
		rfs4_file_rele(sfp);
	}
	if (fp) {
		if (fp_rele_grant_hold) rfs4_clear_dont_grant(fp);
		rfs4_file_rele(fp);
	}

	DTRACE_NFSV4_2(op__rename__done, struct compound_state *, cs,
	    RENAME4res *, resp);
}

/* ARGSUSED */
static void
rfs4_op_renew(nfs_argop4 *argop, nfs_resop4 *resop, struct svc_req *req,
    struct compound_state *cs)
{
	RENEW4args *args = &argop->nfs_argop4_u.oprenew;
	RENEW4res *resp = &resop->nfs_resop4_u.oprenew;
	rfs4_client_t *cp;

	DTRACE_NFSV4_2(op__renew__start, struct compound_state *, cs,
	    RENEW4args *, args);

	cp = rfs4_findclient_by_id(cs->rip, args->clientid, FALSE);
	if (cp == NULL) {
		*cs->statusp = resp->status =
		    rfs4_check_clientid(cs->rip, &args->clientid, 0);
		goto out;
	}

	if (rfs4_lease_expired(cp)) {
		rfs4_client_rele(cp);
		*cs->statusp = resp->status = NFS4ERR_EXPIRED;
		goto out;
	}

	if (rfs4_lease_moved(cp, cs)) {
		rfs4_client_rele(cp);
		*cs->statusp = resp->status = NFS4ERR_LEASE_MOVED;
		goto out;
	}
	rfs4_update_lease(cp);

	mutex_enter(cp->rc_cbinfo.cb_lock);
	if (cp->rc_cbinfo.cb_notified_of_cb_path_down == FALSE) {
		cp->rc_cbinfo.cb_notified_of_cb_path_down = TRUE;
		*cs->statusp = resp->status = NFS4ERR_CB_PATH_DOWN;
	} else {
		*cs->statusp = resp->status = NFS4_OK;
		cs->clientid = args->clientid;
		cs->renew_seen = TRUE;
	}
	mutex_exit(cp->rc_cbinfo.cb_lock);

	rfs4_client_rele(cp);

out:
	DTRACE_NFSV4_2(op__renew__done, struct compound_state *, cs,
	    RENEW4res *, resp);
}

/* ARGSUSED */
static void
rfs4_op_restorefh(nfs_argop4 *args, nfs_resop4 *resop, struct svc_req *req,
    struct compound_state *cs)
{
	RESTOREFH4res *resp = &resop->nfs_resop4_u.oprestorefh;

	DTRACE_NFSV4_1(op__restorefh__start, struct compound_state *, cs);

	/* No need to check cs->access - we are not accessing any object */
	if ((cs->saved_vp == NULL) || (cs->saved_fh.nfs_fh4_val == NULL)) {
		*cs->statusp = resp->status = NFS4ERR_RESTOREFH;
		goto out;
	}
	if (cs->vp != NULL) {
		VN_RELE(cs->vp);
	}
	cs->vp = cs->saved_vp;
	cs->saved_vp = NULL;
	cs->exi = cs->saved_exi;
	nfs_fh4_copy(&cs->saved_fh, &cs->fh);
	*cs->statusp = resp->status = NFS4_OK;
	cs->deleg = FALSE;

out:
	DTRACE_NFSV4_2(op__restorefh__done, struct compound_state *, cs,
	    RESTOREFH4res *, resp);
}

/* ARGSUSED */
static void
rfs4_op_savefh(nfs_argop4 *argop, nfs_resop4 *resop, struct svc_req *req,
    struct compound_state *cs)
{
	SAVEFH4res *resp = &resop->nfs_resop4_u.opsavefh;

	DTRACE_NFSV4_1(op__savefh__start, struct compound_state *, cs);

	/* No need to check cs->access - we are not accessing any object */
	if (cs->vp == NULL) {
		*cs->statusp = resp->status = NFS4ERR_NOFILEHANDLE;
		goto out;
	}
	if (cs->saved_vp != NULL) {
		VN_RELE(cs->saved_vp);
	}
	cs->saved_vp = cs->vp;
	VN_HOLD(cs->saved_vp);
	cs->saved_exi = cs->exi;
	/*
	 * since SAVEFH is fairly rare, don't alloc space for its fh
	 * unless necessary.
	 */
	if (cs->saved_fh.nfs_fh4_val == NULL) {
		cs->saved_fh.nfs_fh4_val = kmem_alloc(NFS4_FHSIZE, KM_SLEEP);
	}
	nfs_fh4_copy(&cs->fh, &cs->saved_fh);
	*cs->statusp = resp->status = NFS4_OK;

out:
	DTRACE_NFSV4_2(op__savefh__done, struct compound_state *, cs,
	    SAVEFH4res *, resp);
}

/*
 * rfs4_verify_attr is called when nfsv4 Setattr failed, but we wish to
 * return the bitmap of attrs that were set successfully. It is also
 * called by Verify/Nverify to test the vattr/vfsstat attrs. It should
 * always be called only after rfs4_do_set_attrs().
 *
 * Verify that the attributes are same as the expected ones. sargp->vap
 * and sargp->sbp contain the input attributes as translated from fattr4.
 *
 * This function verifies only the attrs that correspond to a vattr or
 * vfsstat struct. That is because of the extra step needed to get the
 * corresponding system structs. Other attributes have already been set or
 * verified by do_rfs4_set_attrs.
 *
 * Return 0 if all attrs match, -1 if some don't, error if error processing.
 */
static int
rfs4_verify_attr(struct nfs4_svgetit_arg *sargp,
    bitmap4 *resp, struct nfs4_ntov_table *ntovp)
{
	int error, ret_error = 0;
	int i, k;
	uint_t sva_mask = sargp->vap->va_mask;
	uint_t vbit;
	union nfs4_attr_u *na;
	uint8_t *amap;
	bool_t getsb = ntovp->vfsstat;

	if (sva_mask != 0) {
		/*
		 * Okay to overwrite sargp->vap because we verify based
		 * on the incoming values.
		 */
		ret_error = VOP_GETATTR(sargp->cs->vp, sargp->vap, 0,
		    sargp->cs->cr, NULL);
		if (ret_error) {
			if (resp == NULL)
				return (ret_error);
			/*
			 * Must return bitmap of successful attrs
			 */
			sva_mask = 0;	/* to prevent checking vap later */
		} else {
			/*
			 * Some file systems clobber va_mask. it is probably
			 * wrong of them to do so, nonethless we practice
			 * defensive coding.
			 * See bug id 4276830.
			 */
			sargp->vap->va_mask = sva_mask;
		}
	}

	if (getsb) {
		/*
		 * Now get the superblock and loop on the bitmap, as there is
		 * no simple way of translating from superblock to bitmap4.
		 */
		ret_error = VFS_STATVFS(sargp->cs->vp->v_vfsp, sargp->sbp);
		if (ret_error) {
			if (resp == NULL)
				goto errout;
			getsb = FALSE;
		}
	}

	/*
	 * Now loop and verify each attribute which getattr returned
	 * whether it's the same as the input.
	 */
	if (resp == NULL && !getsb && (sva_mask == 0))
		goto errout;

	na = ntovp->na;
	amap = ntovp->amap;
	k = 0;
	for (i = 0; i < ntovp->attrcnt; i++, na++, amap++) {
		k = *amap;
		ASSERT(nfs4_ntov_map[k].nval == k);
		vbit = nfs4_ntov_map[k].vbit;

		/*
		 * If vattr attribute but VOP_GETATTR failed, or it's
		 * superblock attribute but VFS_STATVFS failed, skip
		 */
		if (vbit) {
			if ((vbit & sva_mask) == 0)
				continue;
		} else if (!(getsb && nfs4_ntov_map[k].vfsstat)) {
			continue;
		}
		error = (*nfs4_ntov_map[k].sv_getit)(NFS4ATTR_VERIT, sargp, na);
		if (resp != NULL) {
			if (error)
				ret_error = -1;	/* not all match */
			else	/* update response bitmap */
				*resp |= nfs4_ntov_map[k].fbit;
			continue;
		}
		if (error) {
			ret_error = -1;	/* not all match */
			break;
		}
	}
errout:
	return (ret_error);
}

/*
 * Decode the attribute to be set/verified. If the attr requires a sys op
 * (VOP_GETATTR, VFS_VFSSTAT), and the request is to verify, then don't
 * call the sv_getit function for it, because the sys op hasn't yet been done.
 * Return 0 for success, error code if failed.
 *
 * Note: the decoded arg is not freed here but in nfs4_ntov_table_free.
 */
static int
decode_fattr4_attr(nfs4_attr_cmd_t cmd, struct nfs4_svgetit_arg *sargp,
    int k, XDR *xdrp, bitmap4 *resp_bval, union nfs4_attr_u *nap)
{
	int error = 0;
	bool_t set_later;

	sargp->vap->va_mask |= nfs4_ntov_map[k].vbit;

	if ((*nfs4_ntov_map[k].xfunc)(xdrp, nap)) {
		set_later = nfs4_ntov_map[k].vbit || nfs4_ntov_map[k].vfsstat;
		/*
		 * don't verify yet if a vattr or sb dependent attr,
		 * because we don't have their sys values yet.
		 * Will be done later.
		 */
		if (! (set_later && (cmd == NFS4ATTR_VERIT))) {
			/*
			 * ACLs are a special case, since setting the MODE
			 * conflicts with setting the ACL.  We delay setting
			 * the ACL until all other attributes have been set.
			 * The ACL gets set in do_rfs4_op_setattr().
			 */
			if (nfs4_ntov_map[k].fbit != FATTR4_ACL_MASK) {
				error = (*nfs4_ntov_map[k].sv_getit)(cmd,
				    sargp, nap);
				if (error) {
					xdr_free(nfs4_ntov_map[k].xfunc,
					    (caddr_t)nap);
				}
			}
		}
	} else {
#ifdef  DEBUG
		zcmn_err(getzoneid(), CE_NOTE, "decode_fattr4_attr: error "
		    "decoding attribute %d\n", k);
#endif
		error = EINVAL;
	}
	if (!error && resp_bval && !set_later) {
		*resp_bval |= nfs4_ntov_map[k].fbit;
	}

	return (error);
}

/*
 * Set vattr based on incoming fattr4 attrs - used by setattr.
 * Set response mask. Ignore any values that are not writable vattr attrs.
 */
static nfsstat4
do_rfs4_set_attrs(bitmap4 *resp, fattr4 *fattrp, struct compound_state *cs,
    struct nfs4_svgetit_arg *sargp, struct nfs4_ntov_table *ntovp,
    nfs4_attr_cmd_t cmd)
{
	int error = 0;
	int i;
	char *attrs = fattrp->attrlist4;
	uint32_t attrslen = fattrp->attrlist4_len;
	XDR xdr;
	nfsstat4 status = NFS4_OK;
	vnode_t *vp = cs->vp;
	union nfs4_attr_u *na;
	uint8_t *amap;

#ifndef lint
	/*
	 * Make sure that maximum attribute number can be expressed as an
	 * 8 bit quantity.
	 */
	ASSERT(NFS4_MAXNUM_ATTRS <= (UINT8_MAX + 1));
#endif

	if (vp == NULL) {
		if (resp)
			*resp = 0;
		return (NFS4ERR_NOFILEHANDLE);
	}

	if (cs->access == CS_ACCESS_DENIED) {
		if (resp)
			*resp = 0;
		return (NFS4ERR_ACCESS);
	}

	/*
	 * If this is either nverify or verify, we need to
	 * see if the filesystem is frozen and the
	 * attribute has anything set other than the
	 * valid moved bits.
	 */
	DTRACE_PROBE3(nfsmig__i__verify_check, nfs4_attr_cmd_t, cmd,
	    fsh_entry_t *, cs->fse, fattr4 *, fattrp);

	if (cmd == NFS4ATTR_VERIT && (cs->fse->fse_state & FSE_MOVED) &&
	    INVALID_MOVED_ATTR(fattrp->attrmask)) {
		if (resp)
			*resp = 0;
		return (NFS4ERR_MOVED);
	}

	sargp->op = cmd;
	sargp->cs = cs;
	sargp->flag = 0;	/* may be set later */
	sargp->vap->va_mask = 0;
	sargp->rdattr_error = NFS4_OK;
	sargp->rdattr_error_req = FALSE;
	/* sargp->sbp is set by the caller */

	xdrmem_create(&xdr, attrs, attrslen, XDR_DECODE);

	na = ntovp->na;
	amap = ntovp->amap;

	/*
	 * The following loop iterates on the nfs4_ntov_map checking
	 * if the fbit is set in the requested bitmap.
	 * If set then we process the arguments using the
	 * rfs4_fattr4 conversion functions to populate the setattr
	 * vattr and va_mask. Any settable attrs that are not using vattr
	 * will be set in this loop.
	 */
	for (i = 0; i < nfs4_ntov_map_size; i++) {
		if (!(fattrp->attrmask & nfs4_ntov_map[i].fbit)) {
			continue;
		}
		/*
		 * If setattr, must be a writable attr.
		 * If verify/nverify, must be a readable attr.
		 */
		if ((error = (*nfs4_ntov_map[i].sv_getit)(
		    NFS4ATTR_SUPPORTED, sargp, NULL)) != 0) {
			/*
			 * Client tries to set/verify an
			 * unsupported attribute, tries to set
			 * a read only attr or verify a write
			 * only one - error!
			 */
			break;
		}
		/*
		 * Decode the attribute to set/verify
		 */
		error = decode_fattr4_attr(cmd, sargp, nfs4_ntov_map[i].nval,
		    &xdr, resp ? resp : NULL, na);
		if (error)
			break;
		*amap++ = (uint8_t)nfs4_ntov_map[i].nval;
		na++;
		(ntovp->attrcnt)++;
		if (nfs4_ntov_map[i].vfsstat)
			ntovp->vfsstat = TRUE;
	}

	if (error != 0)
		status = (error == ENOTSUP ? NFS4ERR_ATTRNOTSUPP :
		    puterrno4(error));
	/* xdrmem_destroy(&xdrs); */	/* NO-OP */
	return (status);
}

static nfsstat4
do_rfs4_op_setattr(bitmap4 *resp, fattr4 *fattrp, struct compound_state *cs,
    stateid4 *stateid)
{
	int error = 0;
	struct nfs4_svgetit_arg sarg;
	bool_t trunc;

	nfsstat4 status = NFS4_OK;
	cred_t *cr = cs->cr;
	vnode_t *vp = cs->vp;
	struct nfs4_ntov_table ntov;
	struct statvfs64 sb;
	struct vattr bva;
	struct flock64 bf;
	int in_crit = 0;
	uint_t saved_mask = 0;
	caller_context_t ct;

	*resp = 0;
	sarg.sbp = &sb;
	sarg.is_referral = B_FALSE;
	nfs4_ntov_table_init(&ntov);
	status = do_rfs4_set_attrs(resp, fattrp, cs, &sarg, &ntov,
	    NFS4ATTR_SETIT);
	if (status != NFS4_OK) {
		/*
		 * failed set attrs
		 */
		goto done;
	}
	if ((sarg.vap->va_mask == 0) &&
	    (! (fattrp->attrmask & FATTR4_ACL_MASK))) {
		/*
		 * no further work to be done
		 */
		goto done;
	}

	/*
	 * If we got a request to set the ACL and the MODE, only
	 * allow changing VSUID, VSGID, and VSVTX.  Attempting
	 * to change any other bits, along with setting an ACL,
	 * gives NFS4ERR_INVAL.
	 */
	if ((fattrp->attrmask & FATTR4_ACL_MASK) &&
	    (fattrp->attrmask & FATTR4_MODE_MASK)) {
		vattr_t va;

		va.va_mask = AT_MODE;
		error = VOP_GETATTR(vp, &va, 0, cs->cr, NULL);
		if (error) {
			status = puterrno4(error);
			goto done;
		}
		if ((sarg.vap->va_mode ^ va.va_mode) &
		    ~(VSUID | VSGID | VSVTX)) {
			status = NFS4ERR_INVAL;
			goto done;
		}
	}

	/* Check stateid only if size has been set */
	if (sarg.vap->va_mask & AT_SIZE) {
		trunc = (sarg.vap->va_size == 0);
		status = rfs4_check_stateid(FWRITE, cs, cs->vp, stateid,
		    trunc, &cs->deleg, sarg.vap->va_mask & AT_SIZE, &ct);
		if (status != NFS4_OK)
			goto done;
		if (cs->lease_moved) {
			status = NFS4ERR_LEASE_MOVED;
			goto done;
		}
	} else {
		ct.cc_sysid = 0;
		ct.cc_pid = 0;
		ct.cc_caller_id = rfs.rg_v4.rg4_caller_id;
		ct.cc_flags = CC_DONTBLOCK;
	}

	/* XXX start of possible race with delegations */

	/*
	 * We need to specially handle size changes because it is
	 * possible for the client to create a file with read-only
	 * modes, but with the file opened for writing. If the client
	 * then tries to set the file size, e.g. ftruncate(3C),
	 * fcntl(F_FREESP), the normal access checking done in
	 * VOP_SETATTR would prevent the client from doing it even though
	 * it should be allowed to do so.  To get around this, we do the
	 * access checking for ourselves and use VOP_SPACE which doesn't
	 * do the access checking.
	 * Also the client should not be allowed to change the file
	 * size if there is a conflicting non-blocking mandatory lock in
	 * the region of the change.
	 */
	if (vp->v_type == VREG && (sarg.vap->va_mask & AT_SIZE)) {
		u_offset_t offset;
		ssize_t length;

		/*
		 * ufs_setattr clears AT_SIZE from vap->va_mask, but
		 * before returning, sarg.vap->va_mask is used to
		 * generate the setattr reply bitmap.  We also clear
		 * AT_SIZE below before calling VOP_SPACE.  For both
		 * of these cases, the va_mask needs to be saved here
		 * and restored after calling VOP_SETATTR.
		 */
		saved_mask = sarg.vap->va_mask;

		/*
		 * Check any possible conflict due to NBMAND locks.
		 * Get into critical region before VOP_GETATTR, so the
		 * size attribute is valid when checking conflicts.
		 */
		if (nbl_need_check(vp)) {
			nbl_start_crit(vp, RW_READER);
			in_crit = 1;
		}

		bva.va_mask = AT_UID|AT_SIZE;
		if (error = VOP_GETATTR(vp, &bva, 0, cr, &ct)) {
			status = puterrno4(error);
			goto done;
		}

		if (in_crit) {
			if (sarg.vap->va_size < bva.va_size) {
				offset = sarg.vap->va_size;
				length = bva.va_size - sarg.vap->va_size;
			} else {
				offset = bva.va_size;
				length = sarg.vap->va_size - bva.va_size;
			}
			if (nbl_conflict(vp, NBL_WRITE, offset, length, 0,
			    &ct)) {
				status = NFS4ERR_LOCKED;
				goto done;
			}
		}

		if (crgetuid(cr) == bva.va_uid) {
			sarg.vap->va_mask &= ~AT_SIZE;
			bf.l_type = F_WRLCK;
			bf.l_whence = 0;
			bf.l_start = (off64_t)sarg.vap->va_size;
			bf.l_len = 0;
			bf.l_sysid = 0;
			bf.l_pid = 0;
			error = VOP_SPACE(vp, F_FREESP, &bf, FWRITE,
			    (offset_t)sarg.vap->va_size, cr, &ct);
		}
	}

	if (!error && sarg.vap->va_mask != 0)
		error = VOP_SETATTR(vp, sarg.vap, sarg.flag, cr, &ct);

	/* restore va_mask -- ufs_setattr clears AT_SIZE */
	if (saved_mask & AT_SIZE)
		sarg.vap->va_mask |= AT_SIZE;

	/*
	 * If an ACL was being set, it has been delayed until now,
	 * in order to set the mode (via the VOP_SETATTR() above) first.
	 */
	if ((! error) && (fattrp->attrmask & FATTR4_ACL_MASK)) {
		int i;

		for (i = 0; i < NFS4_MAXNUM_ATTRS; i++)
			if (ntov.amap[i] == FATTR4_ACL)
				break;
		if (i < NFS4_MAXNUM_ATTRS) {
			error = (*nfs4_ntov_map[FATTR4_ACL].sv_getit)(
			    NFS4ATTR_SETIT, &sarg, &ntov.na[i]);
			if (error == 0) {
				*resp |= FATTR4_ACL_MASK;
			} else if (error == ENOTSUP) {
				(void) rfs4_verify_attr(&sarg, resp, &ntov);
				status = NFS4ERR_ATTRNOTSUPP;
				goto done;
			}
		} else {
			NFS4_DEBUG(rfs4_debug,
			    (CE_NOTE, "do_rfs4_op_setattr: "
			    "unable to find ACL in fattr4"));
			error = EINVAL;
		}
	}

	if (error) {
		/* check if a monitor detected a delegation conflict */
		if (error == EAGAIN && (ct.cc_flags & CC_WOULDBLOCK))
			status = NFS4ERR_DELAY;
		else
			status = puterrno4(error);

		/*
		 * Set the response bitmap when setattr failed.
		 * If VOP_SETATTR partially succeeded, test by doing a
		 * VOP_GETATTR on the object and comparing the data
		 * to the setattr arguments.
		 */
		(void) rfs4_verify_attr(&sarg, resp, &ntov);
	} else {
		/*
		 * Force modified metadata out to stable storage.
		 */
		(void) VOP_FSYNC(vp, FNODSYNC, cr, &ct);
		/*
		 * Set response bitmap
		 */
		nfs4_vmask_to_nmask_set(sarg.vap->va_mask, resp);
	}

/* Return early and already have a NFSv4 error */
done:
	/*
	 * Except for nfs4_vmask_to_nmask_set(), vattr --> fattr
	 * conversion sets both readable and writeable NFS4 attrs
	 * for AT_MTIME and AT_ATIME.  The line below masks out
	 * unrequested attrs from the setattr result bitmap.  This
	 * is placed after the done: label to catch the ATTRNOTSUP
	 * case.
	 */
	*resp &= fattrp->attrmask;

	if (in_crit)
		nbl_end_crit(vp);

	nfs4_ntov_table_free(&ntov, &sarg);

	return (status);
}

/* ARGSUSED */
static void
rfs4_op_setattr(nfs_argop4 *argop, nfs_resop4 *resop, struct svc_req *req,
    struct compound_state *cs)
{
	SETATTR4args *args = &argop->nfs_argop4_u.opsetattr;
	SETATTR4res *resp = &resop->nfs_resop4_u.opsetattr;
	bslabel_t *clabel;

	DTRACE_NFSV4_2(op__setattr__start, struct compound_state *, cs,
	    SETATTR4args *, args);

	if (cs->vp == NULL) {
		*cs->statusp = resp->status = NFS4ERR_NOFILEHANDLE;
		goto out;
	}

	/*
	 * If there is an unshared filesystem mounted on this vnode,
	 * do not allow to setattr on this vnode.
	 */
	if (vn_ismntpt(cs->vp)) {
		*cs->statusp = resp->status = NFS4ERR_ACCESS;
		goto out;
	}

	resp->attrsset = 0;

	if (rdonly4(cs->exi, cs->vp, req)) {
		*cs->statusp = resp->status = NFS4ERR_ROFS;
		goto out;
	}

	/* check label before setting attributes */
	if (is_system_labeled()) {
		ASSERT(req->rq_label != NULL);
		clabel = req->rq_label;
		DTRACE_PROBE2(tx__rfs4__log__info__opsetattr__clabel, char *,
		    "got client label from request(1)",
		    struct svc_req *, req);
		if (!blequal(&l_admin_low->tsl_label, clabel)) {
			if (!do_rfs_label_check(clabel, cs->vp,
			    EQUALITY_CHECK, cs->exi)) {
				*cs->statusp = resp->status = NFS4ERR_ACCESS;
				goto out;
			}
		}
	}

	*cs->statusp = resp->status =
	    do_rfs4_op_setattr(&resp->attrsset, &args->obj_attributes, cs,
	    &args->stateid);

out:
	DTRACE_NFSV4_2(op__setattr__done, struct compound_state *, cs,
	    SETATTR4res *, resp);
}

/* ARGSUSED */
static void
rfs4_op_verify(nfs_argop4 *argop, nfs_resop4 *resop, struct svc_req *req,
    struct compound_state *cs)
{
	/*
	 * verify and nverify are exactly the same, except that nverify
	 * succeeds when some argument changed, and verify succeeds when
	 * when none changed.
	 */

	VERIFY4args  *args = &argop->nfs_argop4_u.opverify;
	VERIFY4res *resp = &resop->nfs_resop4_u.opverify;

	int error;
	struct nfs4_svgetit_arg sarg;
	struct statvfs64 sb;
	struct nfs4_ntov_table ntov;

	DTRACE_NFSV4_2(op__verify__start, struct compound_state *, cs,
	    VERIFY4args *, args);

	if (cs->vp == NULL) {
		*cs->statusp = resp->status = NFS4ERR_NOFILEHANDLE;
		goto out;
	}

	sarg.sbp = &sb;
	sarg.is_referral = B_FALSE;
	nfs4_ntov_table_init(&ntov);
	resp->status = do_rfs4_set_attrs(NULL, &args->obj_attributes, cs,
	    &sarg, &ntov, NFS4ATTR_VERIT);
	if (resp->status != NFS4_OK) {
		/*
		 * do_rfs4_set_attrs will try to verify systemwide attrs,
		 * so could return -1 for "no match".
		 */
		if (resp->status == -1)
			resp->status = NFS4ERR_NOT_SAME;
		goto done;
	}
	error = rfs4_verify_attr(&sarg, NULL, &ntov);
	switch (error) {
	case 0:
		resp->status = NFS4_OK;
		break;
	case -1:
		resp->status = NFS4ERR_NOT_SAME;
		break;
	default:
		resp->status = puterrno4(error);
		break;
	}
done:
	*cs->statusp = resp->status;
	nfs4_ntov_table_free(&ntov, &sarg);
out:
	DTRACE_NFSV4_2(op__verify__done, struct compound_state *, cs,
	    VERIFY4res *, resp);
}

/* ARGSUSED */
static void
rfs4_op_nverify(nfs_argop4 *argop, nfs_resop4 *resop, struct svc_req *req,
    struct compound_state *cs)
{
	/*
	 * verify and nverify are exactly the same, except that nverify
	 * succeeds when some argument changed, and verify succeeds when
	 * when none changed.
	 */

	NVERIFY4args  *args = &argop->nfs_argop4_u.opnverify;
	NVERIFY4res *resp = &resop->nfs_resop4_u.opnverify;

	int error;
	struct nfs4_svgetit_arg sarg;
	struct statvfs64 sb;
	struct nfs4_ntov_table ntov;

	DTRACE_NFSV4_2(op__nverify__start, struct compound_state *, cs,
	    NVERIFY4args *, args);

	if (cs->vp == NULL) {
		*cs->statusp = resp->status = NFS4ERR_NOFILEHANDLE;
		DTRACE_NFSV4_2(op__nverify__done, struct compound_state *, cs,
		    NVERIFY4res *, resp);
		return;
	}
	sarg.sbp = &sb;
	sarg.is_referral = B_FALSE;
	nfs4_ntov_table_init(&ntov);
	resp->status = do_rfs4_set_attrs(NULL, &args->obj_attributes, cs,
	    &sarg, &ntov, NFS4ATTR_VERIT);
	if (resp->status != NFS4_OK) {
		/*
		 * do_rfs4_set_attrs will try to verify systemwide attrs,
		 * so could return -1 for "no match".
		 */
		if (resp->status == -1)
			resp->status = NFS4_OK;
		goto done;
	}
	error = rfs4_verify_attr(&sarg, NULL, &ntov);
	switch (error) {
	case 0:
		resp->status = NFS4ERR_SAME;
		break;
	case -1:
		resp->status = NFS4_OK;
		break;
	default:
		resp->status = puterrno4(error);
		break;
	}
done:
	*cs->statusp = resp->status;
	nfs4_ntov_table_free(&ntov, &sarg);

	DTRACE_NFSV4_2(op__nverify__done, struct compound_state *, cs,
	    NVERIFY4res *, resp);
}

/*
 * XXX - This should live in an NFS header file.
 */
#define	MAX_IOVECS	12

/* ARGSUSED */
static void
rfs4_op_write(nfs_argop4 *argop, nfs_resop4 *resop, struct svc_req *req,
    struct compound_state *cs)
{
	WRITE4args *args = &argop->nfs_argop4_u.opwrite;
	WRITE4res *resp = &resop->nfs_resop4_u.opwrite;
	int error;
	vnode_t *vp;
	struct vattr bva;
	u_offset_t rlimit;
	struct uio uio;
	struct iovec iov[MAX_IOVECS];
	struct iovec *iovp;
	int iovcnt;
	int ioflag;
	cred_t *savecred, *cr;
	bool_t *deleg = &cs->deleg;
	nfsstat4 stat;
	int in_crit = 0;
	caller_context_t ct;
	verifier4 write_verf;

	DTRACE_NFSV4_2(op__write__start, struct compound_state *, cs,
	    WRITE4args *, args);

	vp = cs->vp;
	if (vp == NULL) {
		*cs->statusp = resp->status = NFS4ERR_NOFILEHANDLE;
		goto out;
	}
	if (cs->access == CS_ACCESS_DENIED) {
		*cs->statusp = resp->status = NFS4ERR_ACCESS;
		goto out;
	}

	cr = cs->cr;

	if ((stat = rfs4_check_stateid(FWRITE, cs, vp, &args->stateid, FALSE,
	    deleg, TRUE, &ct)) != NFS4_OK) {
		*cs->statusp = resp->status = stat;
		goto out;
	}
	if (cs->lease_moved) {
		*cs->statusp = resp->status = NFS4ERR_LEASE_MOVED;
		goto out;
	}

	/*
	 * We have to enter the critical region before calling VOP_RWLOCK
	 * to avoid a deadlock with ufs.
	 */
	if (nbl_need_check(vp)) {
		nbl_start_crit(vp, RW_READER);
		in_crit = 1;
		if (nbl_conflict(vp, NBL_WRITE,
		    args->offset, args->data_len, 0, &ct)) {
			*cs->statusp = resp->status = NFS4ERR_LOCKED;
			goto out;
		}
	}

	bva.va_mask = AT_MODE | AT_UID;
	error = VOP_GETATTR(vp, &bva, 0, cr, &ct);

	/*
	 * If we can't get the attributes, then we can't do the
	 * right access checking.  So, we'll fail the request.
	 */
	if (error) {
		*cs->statusp = resp->status = puterrno4(error);
		goto out;
	}

	if (rdonly4(cs->exi, cs->vp, req)) {
		*cs->statusp = resp->status = NFS4ERR_ROFS;
		goto out;
	}

	if (vp->v_type != VREG) {
		*cs->statusp = resp->status =
		    ((vp->v_type == VDIR) ? NFS4ERR_ISDIR : NFS4ERR_INVAL);
		goto out;
	}

	if (crgetuid(cr) != bva.va_uid &&
	    (error = VOP_ACCESS(vp, VWRITE, 0, cr, &ct))) {
		*cs->statusp = resp->status = puterrno4(error);
		goto out;
	}

	if (MANDLOCK(vp, bva.va_mode)) {
		*cs->statusp = resp->status = NFS4ERR_ACCESS;
		goto out;
	}

	if (cs->fse->fse_wr4verf == 0)
		write_verf = rfs.rg_v4.rg4_write4verf;
	else
		write_verf = cs->fse->fse_wr4verf;

	if (args->data_len == 0) {
		*cs->statusp = resp->status = NFS4_OK;
		resp->count = 0;
		resp->committed = args->stable;
		resp->writeverf = write_verf;
		goto out;
	}

	if (args->mblk != NULL) {
		mblk_t *m;
		uint_t bytes, round_len;

		iovcnt = 0;
		bytes = 0;
		round_len = roundup(args->data_len, BYTES_PER_XDR_UNIT);
		for (m = args->mblk;
		    m != NULL && bytes < round_len;
		    m = m->b_cont) {
			iovcnt++;
			bytes += MBLKL(m);
		}
#ifdef DEBUG
		/* should have ended on an mblk boundary */
		if (bytes != round_len) {
			printf("bytes=0x%x, round_len=0x%x, req len=0x%x\n",
			    bytes, round_len, args->data_len);
			printf("args=%p, args->mblk=%p, m=%p", (void *)args,
			    (void *)args->mblk, (void *)m);
			ASSERT(bytes == round_len);
		}
#endif
		if (iovcnt <= MAX_IOVECS) {
			iovp = iov;
		} else {
			iovp = kmem_alloc(sizeof (*iovp) * iovcnt, KM_SLEEP);
		}
		mblk_to_iov(args->mblk, iovcnt, iovp);
	} else if (args->rlist != NULL) {
		iovcnt = 1;
		iovp = iov;
		iovp->iov_base = (char *)((args->rlist)->u.c_daddr3);
		iovp->iov_len = args->data_len;
	} else {
		iovcnt = 1;
		iovp = iov;
		iovp->iov_base = args->data_val;
		iovp->iov_len = args->data_len;
	}

	uio.uio_iov = iovp;
	uio.uio_iovcnt = iovcnt;

	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_extflg = UIO_COPY_DEFAULT;
	uio.uio_loffset = args->offset;
	uio.uio_resid = args->data_len;
	uio.uio_llimit = curproc->p_fsz_ctl;
	rlimit = uio.uio_llimit - args->offset;
	if (rlimit < (u_offset_t)uio.uio_resid)
		uio.uio_resid = (int)rlimit;

	if (args->stable == UNSTABLE4)
		ioflag = 0;
	else if (args->stable == FILE_SYNC4)
		ioflag = FSYNC;
	else if (args->stable == DATA_SYNC4)
		ioflag = FDSYNC;
	else {
		if (iovp != iov)
			kmem_free(iovp, sizeof (*iovp) * iovcnt);
		*cs->statusp = resp->status = NFS4ERR_INVAL;
		goto out;
	}

	/*
	 * We're changing creds because VM may fault and we need
	 * the cred of the current thread to be used if quota
	 * checking is enabled.
	 */
	savecred = curthread->t_cred;
	curthread->t_cred = cr;
	error = do_io(FWRITE, vp, &uio, ioflag, cr, &ct);
	curthread->t_cred = savecred;

	if (iovp != iov)
		kmem_free(iovp, sizeof (*iovp) * iovcnt);

	if (error) {
		*cs->statusp = resp->status = puterrno4(error);
		goto out;
	}

	*cs->statusp = resp->status = NFS4_OK;
	resp->count = args->data_len - uio.uio_resid;

	if (ioflag == 0)
		resp->committed = UNSTABLE4;
	else
		resp->committed = FILE_SYNC4;

	resp->writeverf = write_verf;

out:
	if (in_crit)
		nbl_end_crit(vp);

	DTRACE_NFSV4_2(op__write__done, struct compound_state *, cs,
	    WRITE4res *, resp);
}


/* XXX put in a header file */
extern int	sec_svc_getcred(struct svc_req *, cred_t *,  caddr_t *, int *);

void
rfs4_compound(COMPOUND4args *args, COMPOUND4res *resp, struct exportinfo *exi,
    struct svc_req *req, cred_t *cr, int *rv)
{
	uint_t i;
	struct compound_state cs;
	bool_t is_frozen = TRUE;
	rfs_inst_t *rip;

	if (rv != NULL)
		*rv = 0;

	rip = rfs_inst_svcreq_to_rip(req);

	rfs4_init_compound_state(&cs);
	cs.rip = rip;

	/*
	 * Form a reply tag by copying over the reqeuest tag.
	 */
	resp->tag.utf8string_val =
	    kmem_alloc(args->tag.utf8string_len, KM_SLEEP);
	resp->tag.utf8string_len = args->tag.utf8string_len;
	bcopy(args->tag.utf8string_val, resp->tag.utf8string_val,
	    resp->tag.utf8string_len);

	cs.statusp = &resp->status;
	cs.req = req;

	/*
	 * XXX for now, minorversion should be zero
	 */
	if (args->minorversion != NFS4_MINORVERSION) {
		DTRACE_NFSV4_2(compound__start, struct compound_state *,
		    &cs, COMPOUND4args *, args);
		resp->array_len = 0;
		resp->array = NULL;
		resp->status = NFS4ERR_MINOR_VERS_MISMATCH;
		DTRACE_NFSV4_2(compound__done, struct compound_state *,
		    &cs, COMPOUND4res *, resp);
		return;
	}

	ASSERT(exi == NULL);
	ASSERT(cr == NULL);

	cr = crget();
	ASSERT(cr != NULL);

	if (sec_svc_getcred(req, cr, &cs.principal, &cs.nfsflavor) == 0) {
		DTRACE_NFSV4_2(compound__start, struct compound_state *,
		    &cs, COMPOUND4args *, args);
		crfree(cr);
		DTRACE_NFSV4_2(compound__done, struct compound_state *,
		    &cs, COMPOUND4res *, resp);
		svcerr_badcred(req->rq_xprt);
		if (rv != NULL)
			*rv = 1;
		return;
	}
	resp->array_len = args->array_len;
	resp->array = kmem_zalloc(args->array_len * sizeof (nfs_resop4),
	    KM_SLEEP);

	cs.basecr = cr;

	DTRACE_NFSV4_2(compound__start, struct compound_state *, &cs,
	    COMPOUND4args *, args);

	/*
	 * For now, NFS4 compound processing must be protected by
	 * rn_export_rwlock because it can access more than one exportinfo
	 * per compound and share/unshare can now change multiple
	 * exinfo structs.  The NFS2/3 code only refs 1 exportinfo
	 * per proc (excluding public exinfo), and exi_count design
	 * is sufficient to protect concurrent execution of NFS2/3
	 * ops along with unexport.  This lock will be removed as
	 * part of the NFSv4 phase 2 namespace redesign work.
	 */
	rw_enter(&rip->ri_nm.rn_export_rwlock, RW_READER);

	/*
	 * If this is the first compound we've seen, we need to start all
	 * new instances' grace periods.
	 */
	if (rip->ri_v4.r4_seen_first_compound == 0) {
		rfs4_inst_grace_start(rip);
		/*
		 * This must be set after rfs4_inst_grace_start(), otherwise
		 * another thread could proceed past here before the former
		 * is finished.
		 */
		rip->ri_v4.r4_seen_first_compound = 1;
	}

	cs.fse = NULL;
	for (i = 0; i < args->array_len && cs.cont; i++) {
		nfs_argop4 *argop;
		nfs_resop4 *resop;
		uint_t op;
		struct exportinfo *old_exi;
		nfsstat4 s = NFS4_OK;

		argop = &args->array[i];
		resop = &resp->array[i];
		resop->resop = argop->argop;
		op = (uint_t)resop->resop;

		/*
		 * Check to see if we can get a READER lock.
		 */
		old_exi = cs.exi;
		if (old_exi != NULL) {
			if (cs.fse == NULL || !EQFSID(&old_exi->exi_fsid,
			    &cs.fse->fse_fsid)) {
				if (cs.fse) {
					/*
					 * If frozen, then cs.cont must be
					 * FALSE, and we should never be here.
					 */
					ASSERT(is_frozen != TRUE);
					if (is_frozen == FALSE)
						rfs4_fse_release_reader(cs.fse);

					fsh_ent_rele(rip, cs.fse);
				}
				cs.fse = fsh_get_ent(rip, old_exi->exi_fsid);

				/*
				 * We have an exi, so we should
				 * have a fse. It must be the case that
				 * the entry was removed.
				 */
				if (cs.fse == NULL) {
					fsid_t	*fsid = &old_exi->exi_fsid;

					DTRACE_PROBE2(nfsmig__e__no_fse_v4,
					    fsid_t *, fsid,
					    struct exportinfo *, old_exi);

					cs.cont = FALSE;
					rfs4_op_error(argop, resop, req, &cs,
					    NFS4ERR_SERVERFAULT);
					goto errors;
				}

				is_frozen = rfs4_fse_is_frozen(cs.fse);
			}

			DTRACE_PROBE4(nfsmig__i__v4__state,
			    int, is_frozen,
			    uint_t, op,
			    fsh_entry_t, cs.fse,
			    struct exportinfo *, old_exi);

			switch (s = fse_op_check(&cs, op)) {
			case NFS4ERR_MOVED:
			case NFS4ERR_DELAY:
			case NFS4ERR_RESOURCE:
				cs.cont = FALSE;
				rfs4_op_error(argop, resop, req, &cs, s);
				goto errors;

			case NFS4_OK:
			default:
				break;
			}
		}

		if (op < rfsv4disp_cnt) {
			/*
			 * Count the individual ops here; NULL and COMPOUND
			 * are counted in common_dispatch()
			 */
			nfsstat_inc_proc_cnt(rip, NFS_V4, op);

			NFS4_DEBUG(rfs4_debug > 1,
			    (CE_NOTE, "Executing %s", rfs4_op_string[op]));
			(*rfsv4disptab[op].dis_proc)(argop, resop, req, &cs);
			NFS4_DEBUG(rfs4_debug > 1, (CE_NOTE, "%s returned %d",
			    rfs4_op_string[op], *cs.statusp));
			if (*cs.statusp != NFS4_OK)
				cs.cont = FALSE;
		} else {
			/*
			 * This is effectively dead code since XDR code
			 * will have already returned BADXDR if op doesn't
			 * decode to legal value.  This only done for a
			 * day when XDR code doesn't verify v4 opcodes.
			 */
			op = OP_ILLEGAL;
			nfsstat_inc_proc_cnt(rip, NFS_V4, OP_ILLEGAL_IDX);

			rfs4_op_illegal(argop, resop, req, &cs);
			cs.cont = FALSE;
		}

errors:
		/*
		 * If not at last op, and if we are to stop, then
		 * compact the results array.
		 */
		if ((i + 1) < args->array_len && !cs.cont) {
			nfs_resop4 *new_res = kmem_alloc(
			    (i+1) * sizeof (nfs_resop4), KM_SLEEP);
			bcopy(resp->array,
			    new_res, (i+1) * sizeof (nfs_resop4));
			kmem_free(resp->array,
			    args->array_len * sizeof (nfs_resop4));

			resp->array_len =  i + 1;
			resp->array = new_res;
		}
	}

	if (cs.fse != NULL) {
		if (is_frozen == FALSE)
			rfs4_fse_release_reader(cs.fse);
		fsh_ent_rele(rip, cs.fse);
		cs.fse = NULL;
	}

	rw_exit(&rip->ri_nm.rn_export_rwlock);

	DTRACE_NFSV4_2(compound__done, struct compound_state *, &cs,
	    COMPOUND4res *, resp);

	if (cs.vp)
		VN_RELE(cs.vp);
	if (cs.saved_vp)
		VN_RELE(cs.saved_vp);
	if (cs.saved_fh.nfs_fh4_val)
		kmem_free(cs.saved_fh.nfs_fh4_val, NFS4_FHSIZE);

	if (cs.basecr)
		crfree(cs.basecr);
	if (cs.cr)
		crfree(cs.cr);
	/*
	 * done with this compound request, free the label
	 */

	if (req->rq_label != NULL) {
		kmem_free(req->rq_label, sizeof (bslabel_t));
		req->rq_label = NULL;
	}
}

/*
 * XXX because of what appears to be duplicate calls to rfs4_compound_free
 * XXX zero out the tag and array values. Need to investigate why the
 * XXX calls occur, but at least prevent the panic for now.
 */
void
rfs4_compound_free(COMPOUND4res *resp)
{
	uint_t i;

	if (resp->tag.utf8string_val) {
		UTF8STRING_FREE(resp->tag)
	}

	for (i = 0; i < resp->array_len; i++) {
		nfs_resop4 *resop;
		uint_t op;

		resop = &resp->array[i];
		op = (uint_t)resop->resop;
		if (op < rfsv4disp_cnt) {
			(*rfsv4disptab[op].dis_resfree)(resop);
		}
	}
	if (resp->array != NULL) {
		kmem_free(resp->array, resp->array_len * sizeof (nfs_resop4));
	}
}

/*
 * Process the value of the compound request rpc flags, as a bit-AND
 * of the individual per-op flags (idempotent, allowork, publicfh_ok)
 */
void
rfs4_compound_flagproc(COMPOUND4args *args, int *flagp)
{
	int i;
	int flag = RPC_ALL;

	for (i = 0; flag && i < args->array_len; i++) {
		uint_t op;

		op = (uint_t)args->array[i].argop;

		if (op < rfsv4disp_cnt)
			flag &= rfsv4disptab[op].dis_flags;
		else
			flag = 0;
	}
	*flagp = flag;
}

nfsstat4
rfs4_client_sysid(rfs4_client_t *cp, sysid_t *sp)
{
	nfsstat4 e;

	rfs4_dbe_lock(cp->rc_dbe);

	if (cp->rc_sysidt != LM_NOSYSID) {
		*sp = cp->rc_sysidt;
		e = NFS4_OK;

	} else if ((cp->rc_sysidt = lm_alloc_sysidt()) != LM_NOSYSID) {
		*sp = cp->rc_sysidt;
		e = NFS4_OK;

		NFS4_DEBUG(rfs4_debug, (CE_NOTE,
		    "rfs4_client_sysid: allocated 0x%x\n", *sp));
	} else
		e = NFS4ERR_DELAY;

	rfs4_dbe_unlock(cp->rc_dbe);
	return (e);
}

#if defined(DEBUG) && ! defined(lint)
static void lock_print(char *str, int operation, struct flock64 *flk)
{
	char *op, *type;

	switch (operation) {
	case F_GETLK: op = "F_GETLK";
		break;
	case F_SETLK: op = "F_SETLK";
		break;
	case F_SETLK_NBMAND: op = "F_SETLK_NBMAND";
		break;
	default: op = "F_UNKNOWN";
		break;
	}
	switch (flk->l_type) {
	case F_UNLCK: type = "F_UNLCK";
		break;
	case F_RDLCK: type = "F_RDLCK";
		break;
	case F_WRLCK: type = "F_WRLCK";
		break;
	default: type = "F_UNKNOWN";
		break;
	}

	ASSERT(flk->l_whence == 0);
	zcmn_err(getzoneid(), CE_NOTE,
	    "%s:  %s, type = %s, off = %llx len = %llx pid = %d",
	    str, op, type, (longlong_t)flk->l_start,
	    flk->l_len ? (longlong_t)flk->l_len : ~0LL, flk->l_pid);
}

#define	LOCK_PRINT(d, s, t, f) if (d) lock_print(s, t, f)
#else
#define	LOCK_PRINT(d, s, t, f)
#endif

/*ARGSUSED*/
static bool_t
creds_ok(cred_set_t cr_set, struct svc_req *req, struct compound_state *cs)
{
	return (TRUE);
}

/*
 * Look up the pathname using the vp in cs as the directory vnode.
 * cs->vp will be the vnode for the file on success
 */

static nfsstat4
rfs4_lookup(component4 *component, struct svc_req *req,
    struct compound_state *cs)
{
	char *nm;
	uint32_t len;
	nfsstat4 status;
	struct sockaddr *ca;
	char *name;

	if (cs->vp == NULL) {
		return (NFS4ERR_NOFILEHANDLE);
	}
	if (cs->vp->v_type != VDIR) {
		return (NFS4ERR_NOTDIR);
	}

	if (!utf8_dir_verify(component))
		return (NFS4ERR_INVAL);

	nm = utf8_to_fn(component, &len, NULL);
	if (nm == NULL) {
		return (NFS4ERR_INVAL);
	}

	if (len > MAXNAMELEN) {
		kmem_free(nm, len);
		return (NFS4ERR_NAMETOOLONG);
	}

	ca = (struct sockaddr *)svc_getrpccaller(req->rq_xprt)->buf;
	name = nfscmd_convname(ca, cs->exi, nm, NFSCMD_CONV_INBOUND,
	    MAXPATHLEN + 1);

	if (name == NULL) {
		kmem_free(nm, len);
		return (NFS4ERR_INVAL);
	}

	status = do_rfs4_op_lookup(name, req, cs);

	if (name != nm)
		kmem_free(name, MAXPATHLEN + 1);

	kmem_free(nm, len);

	return (status);
}

static nfsstat4
rfs4_lookupfile(component4 *component, struct svc_req *req,
    struct compound_state *cs, uint32_t access, change_info4 *cinfo)
{
	nfsstat4 status;
	vnode_t *dvp = cs->vp;
	vattr_t bva, ava, fva;
	int error;

	/* Get "before" change value */
	bva.va_mask = AT_CTIME|AT_SEQ;
	error = VOP_GETATTR(dvp, &bva, 0, cs->cr, NULL);
	if (error)
		return (puterrno4(error));

	/* rfs4_lookup may VN_RELE directory */
	VN_HOLD(dvp);

	status = rfs4_lookup(component, req, cs);
	if (status != NFS4_OK) {
		VN_RELE(dvp);
		return (status);
	}

	/*
	 * Get "after" change value, if it fails, simply return the
	 * before value.
	 */
	ava.va_mask = AT_CTIME|AT_SEQ;
	if (VOP_GETATTR(dvp, &ava, 0, cs->cr, NULL)) {
		ava.va_ctime = bva.va_ctime;
		ava.va_seq = 0;
	}
	VN_RELE(dvp);

	/*
	 * Validate the file is a file
	 */
	fva.va_mask = AT_TYPE|AT_MODE;
	error = VOP_GETATTR(cs->vp, &fva, 0, cs->cr, NULL);
	if (error)
		return (puterrno4(error));

	if (fva.va_type != VREG) {
		if (fva.va_type == VDIR)
			return (NFS4ERR_ISDIR);
		if (fva.va_type == VLNK)
			return (NFS4ERR_SYMLINK);
		return (NFS4ERR_INVAL);
	}

	NFS4_SET_FATTR4_CHANGE(cinfo->before, bva.va_ctime);
	NFS4_SET_FATTR4_CHANGE(cinfo->after, ava.va_ctime);

	/*
	 * It is undefined if VOP_LOOKUP will change va_seq, so
	 * cinfo.atomic = TRUE only if we have
	 * non-zero va_seq's, and they have not changed.
	 */
	if (bva.va_seq && ava.va_seq && ava.va_seq == bva.va_seq)
		cinfo->atomic = TRUE;
	else
		cinfo->atomic = FALSE;

	/* Check for mandatory locking */
	cs->mandlock = MANDLOCK(cs->vp, fva.va_mode);
	return (check_open_access(access, cs, req));
}

static nfsstat4
create_vnode(vnode_t *dvp, char *nm,  vattr_t *vap, createmode4 mode,
    timespec32_t *mtime, cred_t *cr, vnode_t **vpp, bool_t *created)
{
	int error;
	nfsstat4 status = NFS4_OK;
	vattr_t va;

tryagain:

	/*
	 * The file open mode used is VWRITE.  If the client needs
	 * some other semantic, then it should do the access checking
	 * itself.  It would have been nice to have the file open mode
	 * passed as part of the arguments.
	 */

	*created = TRUE;
	error = VOP_CREATE(dvp, nm, vap, EXCL, VWRITE, vpp, cr, 0, NULL, NULL);

	if (error) {
		*created = FALSE;

		/*
		 * If we got something other than file already exists
		 * then just return this error.  Otherwise, we got
		 * EEXIST.  If we were doing a GUARDED create, then
		 * just return this error.  Otherwise, we need to
		 * make sure that this wasn't a duplicate of an
		 * exclusive create request.
		 *
		 * The assumption is made that a non-exclusive create
		 * request will never return EEXIST.
		 */

		if (error != EEXIST || mode == GUARDED4) {
			status = puterrno4(error);
			return (status);
		}
		error = VOP_LOOKUP(dvp, nm, vpp, NULL, 0, NULL, cr,
		    NULL, NULL, NULL);

		if (error) {
			/*
			 * We couldn't find the file that we thought that
			 * we just created.  So, we'll just try creating
			 * it again.
			 */
			if (error == ENOENT)
				goto tryagain;

			status = puterrno4(error);
			return (status);
		}

		if (mode == UNCHECKED4) {
			/* existing object must be regular file */
			if ((*vpp)->v_type != VREG) {
				if ((*vpp)->v_type == VDIR)
					status = NFS4ERR_ISDIR;
				else if ((*vpp)->v_type == VLNK)
					status = NFS4ERR_SYMLINK;
				else
					status = NFS4ERR_INVAL;
				VN_RELE(*vpp);
				return (status);
			}

			return (NFS4_OK);
		}

		/* Check for duplicate request */
		ASSERT(mtime != 0);
		va.va_mask = AT_MTIME;
		error = VOP_GETATTR(*vpp, &va, 0, cr, NULL);
		if (!error) {
			/* We found the file */
			if (va.va_mtime.tv_sec != mtime->tv_sec ||
			    va.va_mtime.tv_nsec != mtime->tv_nsec) {
				/* but its not our creation */
				VN_RELE(*vpp);
				return (NFS4ERR_EXIST);
			}
			*created = TRUE; /* retrans of create == created */
			return (NFS4_OK);
		}
		VN_RELE(*vpp);
		return (NFS4ERR_EXIST);
	}

	return (NFS4_OK);
}

static nfsstat4
check_open_access(uint32_t access, struct compound_state *cs,
    struct svc_req *req)
{
	int error;
	vnode_t *vp;
	bool_t readonly;
	cred_t *cr = cs->cr;

	/* For now we don't allow mandatory locking as per V2/V3 */
	if (cs->access == CS_ACCESS_DENIED || cs->mandlock) {
		return (NFS4ERR_ACCESS);
	}

	vp = cs->vp;
	ASSERT(cr != NULL && vp->v_type == VREG);

	/*
	 * If the file system is exported read only and we are trying
	 * to open for write, then return NFS4ERR_ROFS
	 */

	readonly = rdonly4(cs->exi, cs->vp, req);

	if ((access & OPEN4_SHARE_ACCESS_WRITE) && readonly)
		return (NFS4ERR_ROFS);

	if (access & OPEN4_SHARE_ACCESS_READ) {
		if ((VOP_ACCESS(vp, VREAD, 0, cr, NULL) != 0) &&
		    (VOP_ACCESS(vp, VEXEC, 0, cr, NULL) != 0)) {
			return (NFS4ERR_ACCESS);
		}
	}

	if (access & OPEN4_SHARE_ACCESS_WRITE) {
		error = VOP_ACCESS(vp, VWRITE, 0, cr, NULL);
		if (error)
			return (NFS4ERR_ACCESS);
	}

	return (NFS4_OK);
}

static nfsstat4
rfs4_createfile(OPEN4args *args, struct svc_req *req, struct compound_state *cs,
    change_info4 *cinfo, bitmap4 *attrset, clientid4 clientid)
{
	struct nfs4_svgetit_arg sarg;
	struct nfs4_ntov_table ntov;

	bool_t ntov_table_init = FALSE;
	struct statvfs64 sb;
	nfsstat4 status;
	vnode_t *vp;
	vattr_t bva, ava, iva, cva, *vap;
	vnode_t *dvp;
	timespec32_t *mtime;
	char *nm = NULL;
	uint_t buflen;
	bool_t created;
	bool_t setsize = FALSE;
	len_t reqsize;
	int error;
	bool_t trunc;
	caller_context_t ct;
	component4 *component;
	bslabel_t *clabel;
	struct sockaddr *ca;
	char *name = NULL;

	sarg.sbp = &sb;
	sarg.is_referral = B_FALSE;

	dvp = cs->vp;

	/* Check if the file system is read only */
	if (rdonly4(cs->exi, dvp, req))
		return (NFS4ERR_ROFS);

	/* check the label of including directory */
	if (is_system_labeled()) {
		ASSERT(req->rq_label != NULL);
		clabel = req->rq_label;
		DTRACE_PROBE2(tx__rfs4__log__info__opremove__clabel, char *,
		    "got client label from request(1)",
		    struct svc_req *, req);
		if (!blequal(&l_admin_low->tsl_label, clabel)) {
			if (!do_rfs_label_check(clabel, dvp, EQUALITY_CHECK,
			    cs->exi)) {
				return (NFS4ERR_ACCESS);
			}
		}
	}

	/*
	 * Get the last component of path name in nm. cs will reference
	 * the including directory on success.
	 */
	component = &args->open_claim4_u.file;
	if (!utf8_dir_verify(component))
		return (NFS4ERR_INVAL);

	nm = utf8_to_fn(component, &buflen, NULL);

	if (nm == NULL)
		return (NFS4ERR_RESOURCE);

	if (buflen > MAXNAMELEN) {
		kmem_free(nm, buflen);
		return (NFS4ERR_NAMETOOLONG);
	}

	bva.va_mask = AT_TYPE|AT_CTIME|AT_SEQ;
	error = VOP_GETATTR(dvp, &bva, 0, cs->cr, NULL);
	if (error) {
		kmem_free(nm, buflen);
		return (puterrno4(error));
	}

	if (bva.va_type != VDIR) {
		kmem_free(nm, buflen);
		return (NFS4ERR_NOTDIR);
	}

	NFS4_SET_FATTR4_CHANGE(cinfo->before, bva.va_ctime)

	switch (args->mode) {
	case GUARDED4:
		/*FALLTHROUGH*/
	case UNCHECKED4:
		nfs4_ntov_table_init(&ntov);
		ntov_table_init = TRUE;

		*attrset = 0;
		status = do_rfs4_set_attrs(attrset,
		    &args->createhow4_u.createattrs,
		    cs, &sarg, &ntov, NFS4ATTR_SETIT);

		if (status == NFS4_OK && (sarg.vap->va_mask & AT_TYPE) &&
		    sarg.vap->va_type != VREG) {
			if (sarg.vap->va_type == VDIR)
				status = NFS4ERR_ISDIR;
			else if (sarg.vap->va_type == VLNK)
				status = NFS4ERR_SYMLINK;
			else
				status = NFS4ERR_INVAL;
		}

		if (status != NFS4_OK) {
			kmem_free(nm, buflen);
			nfs4_ntov_table_free(&ntov, &sarg);
			*attrset = 0;
			return (status);
		}

		vap = sarg.vap;
		vap->va_type = VREG;
		vap->va_mask |= AT_TYPE;

		if ((vap->va_mask & AT_MODE) == 0) {
			vap->va_mask |= AT_MODE;
			vap->va_mode = (mode_t)0600;
		}

		if (vap->va_mask & AT_SIZE) {

			/* Disallow create with a non-zero size */

			if ((reqsize = sarg.vap->va_size) != 0) {
				kmem_free(nm, buflen);
				nfs4_ntov_table_free(&ntov, &sarg);
				*attrset = 0;
				return (NFS4ERR_INVAL);
			}
			setsize = TRUE;
		}
		break;

	case EXCLUSIVE4:
		/* prohibit EXCL create of named attributes */
		if (dvp->v_flag & V_XATTRDIR) {
			kmem_free(nm, buflen);
			*attrset = 0;
			return (NFS4ERR_INVAL);
		}

		cva.va_mask = AT_TYPE | AT_MTIME | AT_MODE;
		cva.va_type = VREG;
		/*
		 * Ensure no time overflows. Assumes underlying
		 * filesystem supports at least 32 bits.
		 * Truncate nsec to usec resolution to allow valid
		 * compares even if the underlying filesystem truncates.
		 */
		mtime = (timespec32_t *)&args->createhow4_u.createverf;
		cva.va_mtime.tv_sec = mtime->tv_sec % TIME32_MAX;
		cva.va_mtime.tv_nsec = (mtime->tv_nsec / 1000) * 1000;
		cva.va_mode = (mode_t)0;
		vap = &cva;

		/*
		 * For EXCL create, attrset is set to the server attr
		 * used to cache the client's verifier.
		 */
		*attrset = FATTR4_TIME_MODIFY_MASK;
		break;
	}

	ca = (struct sockaddr *)svc_getrpccaller(req->rq_xprt)->buf;
	name = nfscmd_convname(ca, cs->exi, nm, NFSCMD_CONV_INBOUND,
	    MAXPATHLEN  + 1);

	if (name == NULL) {
		kmem_free(nm, buflen);
		return (NFS4ERR_SERVERFAULT);
	}

	status = create_vnode(dvp, name, vap, args->mode, mtime,
	    cs->cr, &vp, &created);
	if (nm != name)
		kmem_free(name, MAXPATHLEN + 1);
	kmem_free(nm, buflen);

	if (status != NFS4_OK) {
		if (ntov_table_init)
			nfs4_ntov_table_free(&ntov, &sarg);
		*attrset = 0;
		return (status);
	}

	trunc = (setsize && !created);

	if (args->mode != EXCLUSIVE4) {
		bitmap4 createmask = args->createhow4_u.createattrs.attrmask;

		/*
		 * True verification that object was created with correct
		 * attrs is impossible.  The attrs could have been changed
		 * immediately after object creation.  If attributes did
		 * not verify, the only recourse for the server is to
		 * destroy the object.  Maybe if some attrs (like gid)
		 * are set incorrectly, the object should be destroyed;
		 * however, seems bad as a default policy.  Do we really
		 * want to destroy an object over one of the times not
		 * verifying correctly?  For these reasons, the server
		 * currently sets bits in attrset for createattrs
		 * that were set; however, no verification is done.
		 *
		 * vmask_to_nmask accounts for vattr bits set on create
		 *	[do_rfs4_set_attrs() only sets resp bits for
		 *	 non-vattr/vfs bits.]
		 * Mask off any bits we set by default so as not to return
		 * more attrset bits than were requested in createattrs
		 */
		if (created) {
			nfs4_vmask_to_nmask(sarg.vap->va_mask, attrset);
			*attrset &= createmask;
		} else {
			/*
			 * We did not create the vnode (we tried but it
			 * already existed).  In this case, the only createattr
			 * that the spec allows the server to set is size,
			 * and even then, it can only be set if it is 0.
			 */
			*attrset = 0;
			if (trunc)
				*attrset = FATTR4_SIZE_MASK;
		}
	}
	if (ntov_table_init)
		nfs4_ntov_table_free(&ntov, &sarg);

	/*
	 * Get the initial "after" sequence number, if it fails,
	 * set to zero, time to before.
	 */
	iva.va_mask = AT_CTIME|AT_SEQ;
	if (VOP_GETATTR(dvp, &iva, 0, cs->cr, NULL)) {
		iva.va_seq = 0;
		iva.va_ctime = bva.va_ctime;
	}

	/*
	 * create_vnode attempts to create the file exclusive,
	 * if it already exists the VOP_CREATE will fail and
	 * may not increase va_seq. It is atomic if
	 * we haven't changed the directory, but if it has changed
	 * we don't know what changed it.
	 */
	if (!created) {
		if (bva.va_seq && iva.va_seq &&
		    bva.va_seq == iva.va_seq)
			cinfo->atomic = TRUE;
		else
			cinfo->atomic = FALSE;
		NFS4_SET_FATTR4_CHANGE(cinfo->after, iva.va_ctime);
	} else {
		/*
		 * The entry was created, we need to sync the
		 * directory metadata.
		 */
		(void) VOP_FSYNC(dvp, 0, cs->cr, NULL);

		/*
		 * Get "after" change value, if it fails, simply return the
		 * before value.
		 */
		ava.va_mask = AT_CTIME|AT_SEQ;
		if (VOP_GETATTR(dvp, &ava, 0, cs->cr, NULL)) {
			ava.va_ctime = bva.va_ctime;
			ava.va_seq = 0;
		}

		NFS4_SET_FATTR4_CHANGE(cinfo->after, ava.va_ctime);

		/*
		 * The cinfo->atomic = TRUE only if we have
		 * non-zero va_seq's, and it has incremented by exactly one
		 * during the create_vnode and it didn't
		 * change during the VOP_FSYNC.
		 */
		if (bva.va_seq && iva.va_seq && ava.va_seq &&
		    iva.va_seq == (bva.va_seq + 1) && iva.va_seq == ava.va_seq)
			cinfo->atomic = TRUE;
		else
			cinfo->atomic = FALSE;
	}

	/* Check for mandatory locking and that the size gets set. */
	cva.va_mask = AT_MODE;
	if (setsize)
		cva.va_mask |= AT_SIZE;

	/* Assume the worst */
	cs->mandlock = TRUE;

	if (VOP_GETATTR(vp, &cva, 0, cs->cr, NULL) == 0) {
		cs->mandlock = MANDLOCK(cs->vp, cva.va_mode);

		/*
		 * Truncate the file if necessary; this would be
		 * the case for create over an existing file.
		 */

		if (trunc) {
			int in_crit = 0;
			rfs4_file_t *fp;
			bool_t create = FALSE;

			/*
			 * We are writing over an existing file.
			 * Check to see if we need to recall a delegation.
			 */
			rfs4_hold_deleg_policy(cs->rip);
			if ((fp = rfs4_findfile(cs->fse, vp, NULL, &create)) !=
			    NULL) {
				if (rfs4_check_delegated_byfp(FWRITE, fp,
				    (reqsize == 0), FALSE, FALSE, &clientid)) {
					rfs4_file_rele(fp);
					rfs4_rele_deleg_policy(cs->rip);
					VN_RELE(vp);
					*attrset = 0;
					return (NFS4ERR_DELAY);
				}
				rfs4_file_rele(fp);
			}
			rfs4_rele_deleg_policy(cs->rip);

			if (nbl_need_check(vp)) {
				in_crit = 1;

				ASSERT(reqsize == 0);

				nbl_start_crit(vp, RW_READER);
				if (nbl_conflict(vp, NBL_WRITE, 0,
				    cva.va_size, 0, NULL)) {
					in_crit = 0;
					nbl_end_crit(vp);
					VN_RELE(vp);
					*attrset = 0;
					return (NFS4ERR_ACCESS);
				}
			}
			ct.cc_sysid = 0;
			ct.cc_pid = 0;
			ct.cc_caller_id = rfs.rg_v4.rg4_caller_id;
			ct.cc_flags = CC_DONTBLOCK;

			cva.va_mask = AT_SIZE;
			cva.va_size = reqsize;
			(void) VOP_SETATTR(vp, &cva, 0, cs->cr, &ct);
			if (in_crit)
				nbl_end_crit(vp);
		}
	}

	error = makefh4(&cs->fh, vp, cs->exi);

	/*
	 * Force modified data and metadata out to stable storage.
	 */
	(void) VOP_FSYNC(vp, FNODSYNC, cs->cr, NULL);

	if (error) {
		VN_RELE(vp);
		*attrset = 0;
		return (puterrno4(error));
	}

	/* if parent dir is attrdir, set namedattr fh flag */
	if (dvp->v_flag & V_XATTRDIR)
		set_fh4_flag(&cs->fh, FH4_NAMEDATTR);

	if (cs->vp)
		VN_RELE(cs->vp);

	cs->vp = vp;

	/*
	 * if we did not create the file, we will need to check
	 * the access bits on the file
	 */

	if (!created) {
		if (setsize)
			args->share_access |= OPEN4_SHARE_ACCESS_WRITE;
		status = check_open_access(args->share_access, cs, req);
		if (status != NFS4_OK)
			*attrset = 0;
	}
	return (status);
}

/*ARGSUSED*/
static void
rfs4_do_open(struct compound_state *cs, struct svc_req *req,
    rfs4_openowner_t *oo, delegreq_t deleg,
    uint32_t access, uint32_t deny,
    OPEN4res *resp, int deleg_cur)
{
	/* XXX Currently not using req  */
	rfs4_state_t *sp;
	rfs4_file_t *fp;
	bool_t screate = TRUE;
	bool_t fcreate = TRUE;
	uint32_t open_a, share_a;
	uint32_t open_d, share_d;
	rfs4_deleg_state_t *dsp;
	sysid_t sysid;
	nfsstat4 status;
	caller_context_t ct;
	int fflags = 0;
	int recall = 0;
	int err;
	int first_open;

	/*
	 * Get the file struct and hold a write lock on it during initial
	 * open. In fact we serialize all OPENs of the same file here.
	 */
	fp = rfs4_findfile_withlock(cs->fse, cs->vp, &cs->fh, &fcreate);
	if (fp == NULL) {
		resp->status = NFS4ERR_RESOURCE;
		DTRACE_PROBE1(nfss__e__do__open1, nfsstat4, resp->status);
		return;
	}

	sp = rfs4_findstate_by_owner_file(cs, oo, fp, &screate);
	if (sp == NULL) {
		resp->status = NFS4ERR_RESOURCE;
		DTRACE_PROBE1(nfss__e__do__open2, nfsstat4, resp->status);
		/* No need to keep any reference */
		rw_exit(&fp->rf_file_rwlock);
		rfs4_file_rele(fp);
		return;
	}

	/* try to get the sysid before continuing */
	if ((status = rfs4_client_sysid(oo->ro_client, &sysid)) != NFS4_OK) {
		resp->status = status;
		rfs4_file_rele(fp);
		/* Not a fully formed open; "close" it */
		if (screate == TRUE)
			rfs4_state_close(sp, FALSE, FALSE, cs->cr);
		rfs4_state_rele(sp);
		return;
	}

	/* Calculate the fflags for this OPEN. */
	if (access & OPEN4_SHARE_ACCESS_READ)
		fflags |= FREAD;
	if (access & OPEN4_SHARE_ACCESS_WRITE)
		fflags |= FWRITE;

	rfs4_dbe_lock(sp->rs_dbe);

	/*
	 * Calculate the new deny and access mode that this open is adding to
	 * the file for this open owner;
	 */
	open_d = (deny & ~sp->rs_open_deny);
	open_a = (access & ~sp->rs_open_access);

	/*
	 * Calculate the new share access and share deny modes that this open
	 * is adding to the file for this open owner;
	 */
	share_a = (access & ~sp->rs_share_access);
	share_d = (deny & ~sp->rs_share_deny);

	first_open = (sp->rs_open_access & OPEN4_SHARE_ACCESS_BOTH) == 0;

	/*
	 * Check to see the client has already sent an open for this
	 * open owner on this file with the same share/deny modes.
	 * If so, we don't need to check for a conflict and we don't
	 * need to add another shrlock.  If not, then we need to
	 * check for conflicts in deny and access before checking for
	 * conflicts in delegation.  We don't want to recall a
	 * delegation based on an open that will eventually fail based
	 * on shares modes.
	 */

	if (share_a || share_d) {
		if ((err = rfs4_share(sp, access, deny)) != 0) {
			rfs4_dbe_unlock(sp->rs_dbe);
			resp->status = err;

			rfs4_file_rele(fp);
			/* Not a fully formed open; "close" it */
			if (screate == TRUE)
				rfs4_state_close(sp, FALSE, FALSE, cs->cr);
			rfs4_state_rele(sp);
			return;
		}
	}

	rfs4_dbe_lock(fp->rf_dbe);

	/*
	 * Check to see if this file is delegated and if so, if a
	 * recall needs to be done.
	 */
	if (rfs4_check_recall(sp, access)) {
		rfs4_dbe_unlock(fp->rf_dbe);
		rfs4_dbe_unlock(sp->rs_dbe);
		rfs4_recall_deleg(fp, FALSE, sp->rs_owner->ro_client);
		delay(NFS4_DELEGATION_CONFLICT_DELAY);
		rfs4_dbe_lock(sp->rs_dbe);

		/* if state closed while lock was dropped */
		if (sp->rs_closed) {
			resp->status = NFS4ERR_OLD_STATEID;
			goto clean_state;
		}
		rfs4_dbe_lock(fp->rf_dbe);
		/* Let's see if the delegation was returned */
		if (rfs4_check_recall(sp, access)) {
			rfs4_dbe_unlock(fp->rf_dbe);
			rfs4_update_lease(sp->rs_owner->ro_client);
			resp->status = NFS4ERR_DELAY;
			goto clean_state;
		}
	}

	/*
	 * The share check passed and any delegation conflict has been
	 * taken care of, so we can call to VFS layer.
	 * If this is the first open then call vop_open with fflags.
	 * If not, call vn_open_upgrade with just the upgrade flags.
	 *
	 * if the file has been opened already, it will have the current
	 * access mode in the state struct.  if it has no share access, then
	 * this is a new open.
	 *
	 * However, if this is open with CLAIM_DELEGATE_CUR, then don't
	 * call VOP_OPEN(), just do the open upgrade.
	 */
	if (first_open && !deleg_cur) {

		/*
		 * Release mutexes before calling VOP_OPEN. VOP call could be
		 * hijacked (i.e. by vscan kernel module), end up in NFS kernel
		 * module again (because of NFSv4 FEM callback) and cause a
		 * deadlock. Releasing locks is safe considering that:
		 *
		 *	*) New NFS delegations are granted only in this
		 *	function and all OPENs of the same file are serialized
		 *	by 'file_rwlock'. So new conflicting delegation cannot
		 *	occur while the locks are dropped.
		 *
		 *	*) State struct is checked for validity after VFS call
		 *	when the locks are reacquired again.
		 */
		rfs4_dbe_unlock(fp->rf_dbe);
		rfs4_dbe_unlock(sp->rs_dbe);

		ct.cc_sysid = sysid;
		ct.cc_pid = rfs4_dbe_getid(sp->rs_owner->ro_dbe);
		ct.cc_caller_id = rfs.rg_v4.rg4_caller_id;
		ct.cc_flags = CC_DONTBLOCK;
		err = VOP_OPEN(&cs->vp, fflags, cs->cr, &ct);
		rfs4_dbe_lock(sp->rs_dbe);
		if (err) {
			/* check if a monitor detected a delegation conflict */
			if (err == EAGAIN && (ct.cc_flags & CC_WOULDBLOCK))
				resp->status = NFS4ERR_DELAY;
			else
				resp->status = NFS4ERR_SERVERFAULT;
			goto clean_state;
		}
		/*
		 * If state was closed while the lock was dropped, then undo
		 * VOP_OPEN and return error.
		 */
		if (sp->rs_closed) {
			rfs4_dbe_unlock(sp->rs_dbe);
			(void) VOP_CLOSE(cs->vp, fflags, 1, (offset_t)0,
			    cs->cr, &ct);
			resp->status = NFS4ERR_OLD_STATEID;
			rfs4_dbe_lock(sp->rs_dbe);
			goto clean_state;
		}
		rfs4_dbe_lock(fp->rf_dbe);
	} else { /* open upgrade */
		/*
		 * calculate the fflags for the new mode that is being added
		 * by this upgrade.
		 */
		fflags = 0;
		if (open_a & OPEN4_SHARE_ACCESS_READ)
			fflags |= FREAD;
		if (open_a & OPEN4_SHARE_ACCESS_WRITE)
			fflags |= FWRITE;
		vn_open_upgrade(cs->vp, fflags);
	}
	sp->rs_open_access |= access;
	sp->rs_open_deny |= deny;

	if (open_d & OPEN4_SHARE_DENY_READ)
		fp->rf_deny_read++;
	if (open_d & OPEN4_SHARE_DENY_WRITE)
		fp->rf_deny_write++;
	fp->rf_share_deny |= deny;

	if (open_a & OPEN4_SHARE_ACCESS_READ)
		fp->rf_access_read++;
	if (open_a & OPEN4_SHARE_ACCESS_WRITE)
		fp->rf_access_write++;
	fp->rf_share_access |= access;

	/*
	 * Check for delegation here. if the deleg argument is not
	 * DELEG_ANY, then this is a reclaim from a client and
	 * we must honor the delegation requested. If necessary we can
	 * set the recall flag.
	 */

	dsp = rfs4_grant_delegation(cs, deleg, sp, &recall);

	cs->deleg = (fp->rf_dinfo.rd_dtype == OPEN_DELEGATE_WRITE);

	next_stateid(&sp->rs_stateid);

	resp->stateid = sp->rs_stateid.stateid;

	rfs4_dbe_unlock(fp->rf_dbe);
	rfs4_dbe_unlock(sp->rs_dbe);

	if (dsp) {
		rfs4_set_deleg_response(dsp, &resp->delegation, NULL, recall);
		rfs4_deleg_state_rele(dsp);
	}

	rfs4_file_rele(fp);
	rfs4_state_rele(sp);

	resp->status = NFS4_OK;
	return;

clean_state:
	if (share_a || share_d)
		(void) rfs4_unshare(sp);
	rfs4_dbe_unlock(sp->rs_dbe);
	rfs4_file_rele(fp);
	/* Not a fully formed open; "close" it */
	if (screate == TRUE)
		rfs4_state_close(sp, FALSE, FALSE, cs->cr);
	rfs4_state_rele(sp);
}

/*ARGSUSED*/
static void
rfs4_do_opennull(struct compound_state *cs, struct svc_req *req,
    OPEN4args *args, rfs4_openowner_t *oo, OPEN4res *resp)
{
	change_info4 *cinfo = &resp->cinfo;
	bitmap4 *attrset = &resp->attrset;

	if (args->opentype == OPEN4_NOCREATE)
		resp->status = rfs4_lookupfile(&args->open_claim4_u.file,
		    req, cs, args->share_access, cinfo);
	else {
		/* inhibit delegation grants during exclusive create */

		if (args->mode == EXCLUSIVE4)
			rfs4_disable_delegation(cs->rip);

		resp->status = rfs4_createfile(args, req, cs, cinfo, attrset,
		    oo->ro_client->rc_clientid);
	}

	if (resp->status == NFS4_OK) {

		/* cs->vp cs->fh now reference the desired file */

		rfs4_do_open(cs, req, oo,
		    oo->ro_need_confirm ? DELEG_NONE : DELEG_ANY,
		    args->share_access, args->share_deny, resp, 0);

		/*
		 * If rfs4_createfile set attrset, we must
		 * clear this attrset before the response is copied.
		 */
		if (resp->status != NFS4_OK && resp->attrset) {
			resp->attrset = 0;
		}
	}
	else
		*cs->statusp = resp->status;

	if (args->mode == EXCLUSIVE4)
		rfs4_enable_delegation(cs->rip);
}

/*ARGSUSED*/
static void
rfs4_do_openprev(struct compound_state *cs, struct svc_req *req,
    OPEN4args *args, rfs4_openowner_t *oo, OPEN4res *resp)
{
	change_info4 *cinfo = &resp->cinfo;
	vattr_t va;
	vtype_t v_type = cs->vp->v_type;
	int error = 0;

	/* Verify that we have a regular file */
	if (v_type != VREG) {
		if (v_type == VDIR)
			resp->status = NFS4ERR_ISDIR;
		else if (v_type == VLNK)
			resp->status = NFS4ERR_SYMLINK;
		else
			resp->status = NFS4ERR_INVAL;
		return;
	}

	va.va_mask = AT_MODE|AT_UID;
	error = VOP_GETATTR(cs->vp, &va, 0, cs->cr, NULL);
	if (error) {
		resp->status = puterrno4(error);
		return;
	}

	cs->mandlock = MANDLOCK(cs->vp, va.va_mode);

	/*
	 * Check if we have access to the file, Note the the file
	 * could have originally been open UNCHECKED or GUARDED
	 * with mode bits that will now fail, but there is nothing
	 * we can really do about that except in the case that the
	 * owner of the file is the one requesting the open.
	 */
	if (crgetuid(cs->cr) != va.va_uid) {
		resp->status = check_open_access(args->share_access, cs, req);
		if (resp->status != NFS4_OK) {
			return;
		}
	}

	/*
	 * cinfo on a CLAIM_PREVIOUS is undefined, initialize to zero
	 */
	cinfo->before = 0;
	cinfo->after = 0;
	cinfo->atomic = FALSE;

	rfs4_do_open(cs, req, oo,
	    NFS4_DELEG4TYPE2REQTYPE(args->open_claim4_u.delegate_type),
	    args->share_access, args->share_deny, resp, 0);
}

static void
rfs4_do_opendelcur(struct compound_state *cs, struct svc_req *req,
    OPEN4args *args, rfs4_openowner_t *oo, OPEN4res *resp)
{
	int error;
	nfsstat4 status;
	stateid4 stateid =
	    args->open_claim4_u.delegate_cur_info.delegate_stateid;
	rfs4_deleg_state_t *dsp;

	/*
	 * Find the state info from the stateid and confirm that the
	 * file is delegated.  If the state openowner is the same as
	 * the supplied openowner we're done. If not, get the file
	 * info from the found state info. Use that file info to
	 * create the state for this lock owner. Note solaris doen't
	 * really need the pathname to find the file. We may want to
	 * lookup the pathname and make sure that the vp exist and
	 * matches the vp in the file structure. However it is
	 * possible that the pathname nolonger exists (local process
	 * unlinks the file), so this may not be that useful.
	 */

	status = rfs4_get_deleg_state(cs, &stateid, &dsp);
	if (status != NFS4_OK) {
		resp->status = status;
		return;
	}

	ASSERT(dsp->rds_finfo->rf_dinfo.rd_dtype != OPEN_DELEGATE_NONE);

	/*
	 * New lock owner, create state. Since this was probably called
	 * in response to a CB_RECALL we set deleg to DELEG_NONE
	 */

	ASSERT(cs->vp != NULL);
	VN_RELE(cs->vp);
	VN_HOLD(dsp->rds_finfo->rf_vp);
	cs->vp = dsp->rds_finfo->rf_vp;

	if (error = makefh4(&cs->fh, cs->vp, cs->exi)) {
		rfs4_deleg_state_rele(dsp);
		*cs->statusp = resp->status = puterrno4(error);
		return;
	}

	/* Mark progress for delegation returns */
	dsp->rds_finfo->rf_dinfo.rd_time_lastwrite = gethrestime_sec();
	rfs4_deleg_state_rele(dsp);
	rfs4_do_open(cs, req, oo, DELEG_NONE,
	    args->share_access, args->share_deny, resp, 1);
}

/*ARGSUSED*/
static void
rfs4_do_opendelprev(struct compound_state *cs, struct svc_req *req,
    OPEN4args *args, rfs4_openowner_t *oo, OPEN4res *resp)
{
	/*
	 * Lookup the pathname, it must already exist since this file
	 * was delegated.
	 *
	 * Find the file and state info for this vp and open owner pair.
	 *	check that they are in fact delegated.
	 *	check that the state access and deny modes are the same.
	 *
	 * Return the delgation possibly seting the recall flag.
	 */
	rfs4_file_t *fp;
	rfs4_state_t *sp;
	bool_t create = FALSE;
	bool_t dcreate = FALSE;
	rfs4_deleg_state_t *dsp;
	nfsace4 *ace;

	/* Note we ignore oflags */
	resp->status = rfs4_lookupfile(&args->open_claim4_u.file_delegate_prev,
	    req, cs, args->share_access, &resp->cinfo);

	if (resp->status != NFS4_OK) {
		return;
	}

	/* get the file struct and hold a lock on it during initial open */
	fp = rfs4_findfile_withlock(cs->fse, cs->vp, NULL, &create);
	if (fp == NULL) {
		resp->status = NFS4ERR_RESOURCE;
		DTRACE_PROBE1(nfss__e__do_opendelprev1, nfsstat4, resp->status);
		return;
	}

	sp = rfs4_findstate_by_owner_file(cs, oo, fp, &create);
	if (sp == NULL) {
		resp->status = NFS4ERR_SERVERFAULT;
		DTRACE_PROBE1(nfss__e__do_opendelprev2, nfsstat4, resp->status);
		rw_exit(&fp->rf_file_rwlock);
		rfs4_file_rele(fp);
		return;
	}

	rfs4_dbe_lock(sp->rs_dbe);
	rfs4_dbe_lock(fp->rf_dbe);
	if (args->share_access != sp->rs_share_access ||
	    args->share_deny != sp->rs_share_deny ||
	    sp->rs_finfo->rf_dinfo.rd_dtype == OPEN_DELEGATE_NONE) {
		NFS4_DEBUG(rfs4_debug,
		    (CE_NOTE, "rfs4_do_opendelprev: state mixup"));
		rfs4_dbe_unlock(fp->rf_dbe);
		rfs4_dbe_unlock(sp->rs_dbe);
		rfs4_file_rele(fp);
		rfs4_state_rele(sp);
		resp->status = NFS4ERR_SERVERFAULT;
		return;
	}
	rfs4_dbe_unlock(fp->rf_dbe);
	rfs4_dbe_unlock(sp->rs_dbe);

	dsp = rfs4_finddeleg(cs, sp, &dcreate);
	if (dsp == NULL) {
		rfs4_state_rele(sp);
		rfs4_file_rele(fp);
		resp->status = NFS4ERR_SERVERFAULT;
		return;
	}

	next_stateid(&sp->rs_stateid);

	resp->stateid = sp->rs_stateid.stateid;

	resp->delegation.delegation_type = dsp->rds_dtype;

	if (dsp->rds_dtype == OPEN_DELEGATE_READ) {
		open_read_delegation4 *rv =
		    &resp->delegation.open_delegation4_u.read;

		rv->stateid = dsp->rds_delegid.stateid;
		rv->recall = FALSE; /* no policy in place to set to TRUE */
		ace = &rv->permissions;
	} else {
		open_write_delegation4 *rv =
		    &resp->delegation.open_delegation4_u.write;

		rv->stateid = dsp->rds_delegid.stateid;
		rv->recall = FALSE;  /* no policy in place to set to TRUE */
		ace = &rv->permissions;
		rv->space_limit.limitby = NFS_LIMIT_SIZE;
		rv->space_limit.nfs_space_limit4_u.filesize = UINT64_MAX;
	}

	/* XXX For now */
	ace->type = ACE4_ACCESS_ALLOWED_ACE_TYPE;
	ace->flag = 0;
	ace->access_mask = 0;
	ace->who.utf8string_len = 0;
	ace->who.utf8string_val = 0;

	rfs4_deleg_state_rele(dsp);
	rfs4_state_rele(sp);
	rfs4_file_rele(fp);
}

typedef enum {
	NFS4_CHKSEQ_OKAY = 0,
	NFS4_CHKSEQ_REPLAY = 1,
	NFS4_CHKSEQ_BAD = 2
} rfs4_chkseq_t;

/*
 * Generic function for sequence number checks.
 */
static rfs4_chkseq_t
rfs4_check_seqid(seqid4 seqid, nfs_resop4 *lastop,
    seqid4 rqst_seq, nfs_resop4 *resop, bool_t copyres)
{
	/* Same sequence ids and matching operations? */
	if (seqid == rqst_seq && resop->resop == lastop->resop) {
		if (copyres == TRUE) {
			rfs4_free_reply(resop);
			rfs4_copy_reply(resop, lastop);
		}
		NFS4_DEBUG(rfs4_debug, (CE_NOTE,
		    "Replayed SEQID %d\n", seqid));
		return (NFS4_CHKSEQ_REPLAY);
	}

	/* If the incoming sequence is not the next expected then it is bad */
	if (rqst_seq != seqid + 1) {
		if (rqst_seq == seqid) {
			NFS4_DEBUG(rfs4_debug,
			    (CE_NOTE, "BAD SEQID: Replayed sequence id "
			    "but last op was %d current op is %d\n",
			    lastop->resop, resop->resop));
			return (NFS4_CHKSEQ_BAD);
		}
		NFS4_DEBUG(rfs4_debug,
		    (CE_NOTE, "BAD SEQID: got %u expecting %u\n",
		    rqst_seq, seqid));
		return (NFS4_CHKSEQ_BAD);
	}

	/* Everything okay -- next expected */
	return (NFS4_CHKSEQ_OKAY);
}


static rfs4_chkseq_t
rfs4_check_open_seqid(seqid4 seqid, rfs4_openowner_t *op, nfs_resop4 *resop)
{
	rfs4_chkseq_t rc;

	rfs4_dbe_lock(op->ro_dbe);
	rc = rfs4_check_seqid(op->ro_open_seqid, &op->ro_reply, seqid, resop,
	    TRUE);
	rfs4_dbe_unlock(op->ro_dbe);

	if (rc == NFS4_CHKSEQ_OKAY)
		rfs4_update_lease(op->ro_client);

	return (rc);
}

static rfs4_chkseq_t
rfs4_check_olo_seqid(seqid4 olo_seqid, rfs4_openowner_t *op, nfs_resop4 *resop)
{
	rfs4_chkseq_t rc;

	rfs4_dbe_lock(op->ro_dbe);
	rc = rfs4_check_seqid(op->ro_open_seqid, &op->ro_reply,
	    olo_seqid, resop, FALSE);
	rfs4_dbe_unlock(op->ro_dbe);

	return (rc);
}

static rfs4_chkseq_t
rfs4_check_lock_seqid(seqid4 seqid, rfs4_lo_state_t *lsp, nfs_resop4 *resop)
{
	rfs4_chkseq_t rc = NFS4_CHKSEQ_OKAY;

	rfs4_dbe_lock(lsp->rls_dbe);
	if (!lsp->rls_skip_seqid_check)
		rc = rfs4_check_seqid(lsp->rls_seqid, &lsp->rls_reply, seqid,
		    resop, TRUE);
	rfs4_dbe_unlock(lsp->rls_dbe);

	return (rc);
}

static void
rfs4_op_open(nfs_argop4 *argop, nfs_resop4 *resop,
    struct svc_req *req, struct compound_state *cs)
{
	OPEN4args *args = &argop->nfs_argop4_u.opopen;
	OPEN4res *resp = &resop->nfs_resop4_u.opopen;
	open_owner4 *owner = &args->owner;
	open_claim_type4 claim = args->claim;
	rfs4_client_t *cp;
	rfs4_openowner_t *oo;
	bool_t create;
	bool_t replay = FALSE;
	int can_reclaim;

	DTRACE_NFSV4_2(op__open__start, struct compound_state *, cs,
	    OPEN4args *, args);

	if (cs->vp == NULL) {
		resp->status = NFS4ERR_NOFILEHANDLE;
		goto end;
	}

	/*
	 * Need to check clientid and lease expiration first based on
	 * error ordering and incrementing sequence id.
	 */
	cp = rfs4_findclient_by_id(cs->rip, owner->clientid, FALSE);
	if (cp == NULL) {
		resp->status =
		    rfs4_check_clientid(cs->rip, &owner->clientid, 0);
		goto end;
	}

	if (rfs4_lease_expired(cp)) {
		rfs4_client_close(cp);
		resp->status = NFS4ERR_EXPIRED;
		goto end;
	}

	can_reclaim = cp->rc_can_reclaim;
	/* only do this when not using tsm */
	if (!rfs4_do_tsm && rfs4_in_grace(cp, cs))
		can_reclaim = 1;

	/*
	 * Find the open_owner for use from this point forward.  Take
	 * care in updating the sequence id based on the type of error
	 * being returned.
	 */
retry:
	create = TRUE;
	oo = rfs4_findopenowner(cs->rip, owner, &create, args->seqid);
	if (oo == NULL) {
		resp->status = NFS4ERR_STALE_CLIENTID;
		rfs4_client_rele(cp);
		goto end;
	}

	/* Hold off access to the sequence space while the open is done */
	rfs4_sw_enter(&oo->ro_sw);

	/*
	 * If the open_owner existed before at the server, then check
	 * the sequence id.
	 */
	if (!create && !oo->ro_postpone_confirm) {
		switch (rfs4_check_open_seqid(args->seqid, oo, resop)) {
		case NFS4_CHKSEQ_BAD:
			if ((args->seqid > oo->ro_open_seqid) &&
			    oo->ro_need_confirm) {
				rfs4_free_opens(oo, TRUE, FALSE);
				rfs4_sw_exit(&oo->ro_sw);
				rfs4_openowner_rele(oo);
				goto retry;
			}
			resp->status = NFS4ERR_BAD_SEQID;
			goto out;
		case NFS4_CHKSEQ_REPLAY: /* replay of previous request */
			replay = TRUE;
			goto out;
		default:
			break;
		}

		/*
		 * Sequence was ok and open owner exists
		 * check to see if we have yet to see an
		 * open_confirm.
		 */
		if (oo->ro_need_confirm) {
			rfs4_free_opens(oo, TRUE, FALSE);
			rfs4_sw_exit(&oo->ro_sw);
			rfs4_openowner_rele(oo);
			goto retry;
		}
	}
	/* Grace only applies to regular-type OPENs */
	if (rfs4_in_grace(cp, cs) &&
	    (claim == CLAIM_NULL || claim == CLAIM_DELEGATE_CUR)) {
		resp->status = NFS4ERR_GRACE;
		goto out;
	}

	/*
	 * If previous state at the server existed then can_reclaim
	 * will be set. If not reply NFS4ERR_NO_GRACE to the
	 * client.
	 */
	if (rfs4_in_grace(cp, cs) && claim == CLAIM_PREVIOUS && !can_reclaim) {
		resp->status = NFS4ERR_NO_GRACE;
		goto out;
	}

	/*
	 * Reject the open if the client has missed the grace period
	 */
	if (!rfs4_in_grace(cp, cs) && claim == CLAIM_PREVIOUS) {
		resp->status = NFS4ERR_NO_GRACE;
		goto out;
	}

	/*
	 * Now that sequence ID maintenance is done, check for
	 * lease moved and fail the open, if necessary.
	 */
	if (rfs4_lease_moved(cp, NULL)) {
		resp->status = NFS4ERR_LEASE_MOVED;
		goto out;
	}

	/* Couple of up-front bookkeeping items */
	if (oo->ro_need_confirm) {
		/*
		 * If this is a reclaim OPEN then we should not ask
		 * for a confirmation of the open_owner per the
		 * protocol specification.
		 */
		if (claim == CLAIM_PREVIOUS)
			oo->ro_need_confirm = FALSE;
		else
			resp->rflags |= OPEN4_RESULT_CONFIRM;
	}
	resp->rflags |= OPEN4_RESULT_LOCKTYPE_POSIX;

	/*
	 * If there is an unshared filesystem mounted on this vnode,
	 * do not allow to open/create in this directory.
	 */
	if (vn_ismntpt(cs->vp)) {
		resp->status = NFS4ERR_ACCESS;
		goto out;
	}

	/*
	 * access must READ, WRITE, or BOTH.  No access is invalid.
	 * deny can be READ, WRITE, BOTH, or NONE.
	 * bits not defined for access/deny are invalid.
	 */
	if (! (args->share_access & OPEN4_SHARE_ACCESS_BOTH) ||
	    (args->share_access & ~OPEN4_SHARE_ACCESS_BOTH) ||
	    (args->share_deny & ~OPEN4_SHARE_DENY_BOTH)) {
		resp->status = NFS4ERR_INVAL;
		goto out;
	}


	/*
	 * make sure attrset is zero before response is built.
	 */
	resp->attrset = 0;

	switch (claim) {
	case CLAIM_NULL:
		rfs4_do_opennull(cs, req, args, oo, resp);
		break;
	case CLAIM_PREVIOUS:
		rfs4_do_openprev(cs, req, args, oo, resp);
		break;
	case CLAIM_DELEGATE_CUR:
		rfs4_do_opendelcur(cs, req, args, oo, resp);
		break;
	case CLAIM_DELEGATE_PREV:
		rfs4_do_opendelprev(cs, req, args, oo, resp);
		break;
	default:
		resp->status = NFS4ERR_INVAL;
		break;
	}

out:
	rfs4_client_rele(cp);

	/* Catch sequence id handling here to make it a little easier */
	switch (resp->status) {
	case NFS4ERR_BADXDR:
	case NFS4ERR_BAD_SEQID:
	case NFS4ERR_BAD_STATEID:
	case NFS4ERR_NOFILEHANDLE:
	case NFS4ERR_RESOURCE:
	case NFS4ERR_STALE_CLIENTID:
	case NFS4ERR_STALE_STATEID:
		/*
		 * The protocol states that if any of these errors are
		 * being returned, the sequence id should not be
		 * incremented.  Any other return requires an
		 * increment.
		 */
		break;
	default:
		/* Always update the lease in this case */
		rfs4_update_lease(oo->ro_client);

		/* Regular response - copy the result */
		if (!replay)
			rfs4_update_open_resp(oo, resop, &cs->fh);

		/*
		 * REPLAY case: Only if the previous response was OK
		 * do we copy the filehandle.  If not OK, no
		 * filehandle to copy.
		 */
		if (replay == TRUE &&
		    resp->status == NFS4_OK &&
		    oo->ro_reply_fh.nfs_fh4_val) {
			/*
			 * If this is a replay, we must restore the
			 * current filehandle/vp to that of what was
			 * returned originally.  Try our best to do
			 * it.
			 */
			nfs_fh4_fmt_t *fh_fmtp =
			    (nfs_fh4_fmt_t *)oo->ro_reply_fh.nfs_fh4_val;

			cs->exi = checkexport4(cs->rip, &fh_fmtp->fh4_fsid,
			    (fid_t *)&fh_fmtp->fh4_xlen, NULL);

			if (cs->exi == NULL) {
				resp->status = NFS4ERR_STALE;
				goto finish;
			}

			VN_RELE(cs->vp);

			cs->vp = nfs4_fhtovp(&oo->ro_reply_fh, cs->exi,
			    &resp->status);

			if (cs->vp == NULL)
				goto finish;

			nfs_fh4_copy(&oo->ro_reply_fh, &cs->fh);
		}

		/*
		 * If this was a replay, no need to update the
		 * sequence id. If the open_owner was not created on
		 * this pass, then update.  The first use of an
		 * open_owner will not bump the sequence id.
		 */
		if (replay == FALSE && !create)
			rfs4_update_open_sequence(oo);
		/*
		 * If the client is receiving an error and the
		 * open_owner needs to be confirmed, there is no way
		 * to notify the client of this fact ignoring the fact
		 * that the server has no method of returning a
		 * stateid to confirm.  Therefore, the server needs to
		 * mark this open_owner in a way as to avoid the
		 * sequence id checking the next time the client uses
		 * this open_owner.
		 */
		if (resp->status != NFS4_OK && oo->ro_need_confirm)
			oo->ro_postpone_confirm = TRUE;
		/*
		 * If OK response then clear the postpone flag and
		 * reset the sequence id to keep in sync with the
		 * client.
		 */
		if (resp->status == NFS4_OK && oo->ro_postpone_confirm) {
			oo->ro_postpone_confirm = FALSE;
			oo->ro_open_seqid = args->seqid;
		}
		break;
	}

finish:
	rfs4_sw_exit(&oo->ro_sw);
	rfs4_openowner_rele(oo);

end:
	*cs->statusp = resp->status;

	DTRACE_NFSV4_2(op__open__done, struct compound_state *, cs,
	    OPEN4res *, resp);
}

/*ARGSUSED*/
void
rfs4_op_open_confirm(nfs_argop4 *argop, nfs_resop4 *resop,
    struct svc_req *req, struct compound_state *cs)
{
	OPEN_CONFIRM4args *args = &argop->nfs_argop4_u.opopen_confirm;
	OPEN_CONFIRM4res *resp = &resop->nfs_resop4_u.opopen_confirm;
	rfs4_state_t *sp;
	nfsstat4 status;

	DTRACE_NFSV4_2(op__open__confirm__start, struct compound_state *, cs,
	    OPEN_CONFIRM4args *, args);

	if (cs->vp == NULL) {
		*cs->statusp = resp->status = NFS4ERR_NOFILEHANDLE;
		goto out;
	}

	status = rfs4_get_state(cs, &args->open_stateid, &sp, RFS4_DBS_VALID);
	if (status != NFS4_OK) {
		*cs->statusp = resp->status = status;
		goto out;
	}

	/* Ensure specified filehandle matches */
	if (cs->vp != sp->rs_finfo->rf_vp) {
		rfs4_state_rele(sp);
		*cs->statusp = resp->status = NFS4ERR_BAD_STATEID;
		goto out;
	}

	/* hold off other access to open_owner while we tinker */
	rfs4_sw_enter(&sp->rs_owner->ro_sw);

	switch (rfs4_check_stateid_seqid(sp, &args->open_stateid)) {
	case NFS4_CHECK_STATEID_OKAY:
		if (rfs4_check_open_seqid(args->seqid, sp->rs_owner,
		    resop) != 0) {
			*cs->statusp = resp->status = NFS4ERR_BAD_SEQID;
			break;
		}
		/*
		 * If it is the appropriate stateid and determined to
		 * be "OKAY" then this means that the stateid does not
		 * need to be confirmed and the client is in error for
		 * sending an OPEN_CONFIRM.
		 */
		*cs->statusp = resp->status = NFS4ERR_BAD_STATEID;
		break;
	case NFS4_CHECK_STATEID_OLD:
		*cs->statusp = resp->status = NFS4ERR_OLD_STATEID;
		break;
	case NFS4_CHECK_STATEID_BAD:
		*cs->statusp = resp->status = NFS4ERR_BAD_STATEID;
		break;
	case NFS4_CHECK_STATEID_EXPIRED:
		*cs->statusp = resp->status = NFS4ERR_EXPIRED;
		break;
	case NFS4_CHECK_STATEID_CLOSED:
		*cs->statusp = resp->status = NFS4ERR_OLD_STATEID;
		break;
	case NFS4_CHECK_STATEID_REPLAY:
		switch (rfs4_check_open_seqid(args->seqid, sp->rs_owner,
		    resop)) {
		case NFS4_CHKSEQ_OKAY:
			/*
			 * This is replayed stateid; if seqid matches
			 * next expected, then client is using wrong seqid.
			 */
			/* fall through */
		case NFS4_CHKSEQ_BAD:
			*cs->statusp = resp->status = NFS4ERR_BAD_SEQID;
			break;
		case NFS4_CHKSEQ_REPLAY:
			/*
			 * Note this case is the duplicate case so
			 * resp->status is already set.
			 */
			*cs->statusp = resp->status;
			rfs4_update_lease(sp->rs_owner->ro_client);
			break;
		}
		break;
	case NFS4_CHECK_STATEID_UNCONFIRMED:
		if (rfs4_check_open_seqid(args->seqid, sp->rs_owner,
		    resop) != NFS4_CHKSEQ_OKAY) {
			*cs->statusp = resp->status = NFS4ERR_BAD_SEQID;
			break;
		}
		if (cs->lease_moved) {
			/*
			 * Update the OO sequence ID, but fail
			 * the operation, as per the spec.
			 */
			rfs4_update_open_sequence(sp->rs_owner);
			*cs->statusp = resp->status = NFS4ERR_LEASE_MOVED;
			break;
		}
		*cs->statusp = resp->status = NFS4_OK;

		next_stateid(&sp->rs_stateid);
		resp->open_stateid = sp->rs_stateid.stateid;
		sp->rs_owner->ro_need_confirm = FALSE;
		rfs4_update_lease(sp->rs_owner->ro_client);
		rfs4_update_open_sequence(sp->rs_owner);
		rfs4_update_open_resp(sp->rs_owner, resop, NULL);
		break;
	default:
		ASSERT(FALSE);
		*cs->statusp = resp->status = NFS4ERR_SERVERFAULT;
		break;
	}
	rfs4_sw_exit(&sp->rs_owner->ro_sw);
	rfs4_state_rele(sp);

out:
	DTRACE_NFSV4_2(op__open__confirm__done, struct compound_state *, cs,
	    OPEN_CONFIRM4res *, resp);
}

/*ARGSUSED*/
void
rfs4_op_open_downgrade(nfs_argop4 *argop, nfs_resop4 *resop,
    struct svc_req *req, struct compound_state *cs)
{
	OPEN_DOWNGRADE4args *args = &argop->nfs_argop4_u.opopen_downgrade;
	OPEN_DOWNGRADE4res *resp = &resop->nfs_resop4_u.opopen_downgrade;
	uint32_t access = args->share_access;
	uint32_t deny = args->share_deny;
	nfsstat4 status;
	rfs4_state_t *sp;
	rfs4_file_t *fp;
	int fflags = 0;

	DTRACE_NFSV4_2(op__open__downgrade__start, struct compound_state *, cs,
	    OPEN_DOWNGRADE4args *, args);

	if (cs->vp == NULL) {
		*cs->statusp = resp->status = NFS4ERR_NOFILEHANDLE;
		goto out;
	}

	status = rfs4_get_state(cs, &args->open_stateid, &sp, RFS4_DBS_VALID);
	if (status != NFS4_OK) {
		*cs->statusp = resp->status = status;
		goto out;
	}

	/* Ensure specified filehandle matches */
	if (cs->vp != sp->rs_finfo->rf_vp) {
		rfs4_state_rele(sp);
		*cs->statusp = resp->status = NFS4ERR_BAD_STATEID;
		goto out;
	}

	/* hold off other access to open_owner while we tinker */
	rfs4_sw_enter(&sp->rs_owner->ro_sw);

	switch (rfs4_check_stateid_seqid(sp, &args->open_stateid)) {
	case NFS4_CHECK_STATEID_OKAY:
		if (rfs4_check_open_seqid(args->seqid, sp->rs_owner,
		    resop) != NFS4_CHKSEQ_OKAY) {
			*cs->statusp = resp->status = NFS4ERR_BAD_SEQID;
			goto end;
		}
		break;
	case NFS4_CHECK_STATEID_OLD:
		*cs->statusp = resp->status = NFS4ERR_OLD_STATEID;
		goto end;
	case NFS4_CHECK_STATEID_BAD:
		*cs->statusp = resp->status = NFS4ERR_BAD_STATEID;
		goto end;
	case NFS4_CHECK_STATEID_EXPIRED:
		*cs->statusp = resp->status = NFS4ERR_EXPIRED;
		goto end;
	case NFS4_CHECK_STATEID_CLOSED:
		*cs->statusp = resp->status = NFS4ERR_OLD_STATEID;
		goto end;
	case NFS4_CHECK_STATEID_UNCONFIRMED:
		*cs->statusp = resp->status = NFS4ERR_BAD_STATEID;
		goto end;
	case NFS4_CHECK_STATEID_REPLAY:
		/* Check the sequence id for the open owner */
		switch (rfs4_check_open_seqid(args->seqid, sp->rs_owner,
		    resop)) {
		case NFS4_CHKSEQ_OKAY:
			/*
			 * This is replayed stateid; if seqid matches
			 * next expected, then client is using wrong seqid.
			 */
			/* fall through */
		case NFS4_CHKSEQ_BAD:
			*cs->statusp = resp->status = NFS4ERR_BAD_SEQID;
			goto end;
		case NFS4_CHKSEQ_REPLAY:
			/*
			 * Note this case is the duplicate case so
			 * resp->status is already set.
			 */
			*cs->statusp = resp->status;
			rfs4_update_lease(sp->rs_owner->ro_client);
			goto end;
		}
		break;
	default:
		ASSERT(FALSE);
		break;
	}

	rfs4_dbe_lock(sp->rs_dbe);

	if (cs->lease_moved) {
		*cs->statusp = resp->status = NFS4ERR_LEASE_MOVED;
		rfs4_update_open_sequence(sp->rs_owner);
		rfs4_dbe_unlock(sp->rs_dbe);
		goto end;
	}

	/*
	 * Check that the new access modes and deny modes are valid.
	 * Check that no invalid bits are set.
	 */
	if ((access & ~(OPEN4_SHARE_ACCESS_READ | OPEN4_SHARE_ACCESS_WRITE)) ||
	    (deny & ~(OPEN4_SHARE_DENY_READ | OPEN4_SHARE_DENY_WRITE))) {
		*cs->statusp = resp->status = NFS4ERR_INVAL;
		rfs4_update_open_sequence(sp->rs_owner);
		rfs4_dbe_unlock(sp->rs_dbe);
		goto end;
	}

	/*
	 * The new modes must be a subset of the current modes and
	 * the access must specify at least one mode. To test that
	 * the new mode is a subset of the current modes we bitwise
	 * AND them together and check that the result equals the new
	 * mode. For example:
	 * New mode, access == R and current mode, sp->rs_open_access  == RW
	 * access & sp->rs_open_access == R == access, so the new access mode
	 * is valid. Consider access == RW, sp->rs_open_access = R
	 * access & sp->rs_open_access == R != access, so the new access mode
	 * is invalid.
	 */
	if ((access & sp->rs_open_access) != access ||
	    (deny & sp->rs_open_deny) != deny ||
	    (access &
	    (OPEN4_SHARE_ACCESS_READ | OPEN4_SHARE_ACCESS_WRITE)) == 0) {
		*cs->statusp = resp->status = NFS4ERR_INVAL;
		rfs4_update_open_sequence(sp->rs_owner);
		rfs4_dbe_unlock(sp->rs_dbe);
		goto end;
	}

	/*
	 * Release any share locks associated with this stateID.
	 * Strictly speaking, this violates the spec because the
	 * spec effectively requires that open downgrade be atomic.
	 * At present, fs_shrlock does not have this capability.
	 */
	(void) rfs4_unshare(sp);

	status = rfs4_share(sp, access, deny);
	if (status != NFS4_OK) {
		*cs->statusp = resp->status = NFS4ERR_SERVERFAULT;
		rfs4_update_open_sequence(sp->rs_owner);
		rfs4_dbe_unlock(sp->rs_dbe);
		goto end;
	}

	fp = sp->rs_finfo;
	rfs4_dbe_lock(fp->rf_dbe);

	/*
	 * If the current mode has deny read and the new mode
	 * does not, decrement the number of deny read mode bits
	 * and if it goes to zero turn off the deny read bit
	 * on the file.
	 */
	if ((sp->rs_open_deny & OPEN4_SHARE_DENY_READ) &&
	    (deny & OPEN4_SHARE_DENY_READ) == 0) {
		fp->rf_deny_read--;
		if (fp->rf_deny_read == 0)
			fp->rf_share_deny &= ~OPEN4_SHARE_DENY_READ;
	}

	/*
	 * If the current mode has deny write and the new mode
	 * does not, decrement the number of deny write mode bits
	 * and if it goes to zero turn off the deny write bit
	 * on the file.
	 */
	if ((sp->rs_open_deny & OPEN4_SHARE_DENY_WRITE) &&
	    (deny & OPEN4_SHARE_DENY_WRITE) == 0) {
		fp->rf_deny_write--;
		if (fp->rf_deny_write == 0)
			fp->rf_share_deny &= ~OPEN4_SHARE_DENY_WRITE;
	}

	/*
	 * If the current mode has access read and the new mode
	 * does not, decrement the number of access read mode bits
	 * and if it goes to zero turn off the access read bit
	 * on the file.  set fflags to FREAD for the call to
	 * vn_open_downgrade().
	 */
	if ((sp->rs_open_access & OPEN4_SHARE_ACCESS_READ) &&
	    (access & OPEN4_SHARE_ACCESS_READ) == 0) {
		fp->rf_access_read--;
		if (fp->rf_access_read == 0)
			fp->rf_share_access &= ~OPEN4_SHARE_ACCESS_READ;
		fflags |= FREAD;
	}

	/*
	 * If the current mode has access write and the new mode
	 * does not, decrement the number of access write mode bits
	 * and if it goes to zero turn off the access write bit
	 * on the file.  set fflags to FWRITE for the call to
	 * vn_open_downgrade().
	 */
	if ((sp->rs_open_access & OPEN4_SHARE_ACCESS_WRITE) &&
	    (access & OPEN4_SHARE_ACCESS_WRITE) == 0) {
		fp->rf_access_write--;
		if (fp->rf_access_write == 0)
			fp->rf_share_deny &= ~OPEN4_SHARE_ACCESS_WRITE;
		fflags |= FWRITE;
	}

	/* Check that the file is still accessible */
	ASSERT(fp->rf_share_access);

	rfs4_dbe_unlock(fp->rf_dbe);

	/* now set the new open access and deny modes */
	sp->rs_open_access = access;
	sp->rs_open_deny = deny;

	/*
	 * we successfully downgraded the share lock, now we need to downgrade
	 * the open. it is possible that the downgrade was only for a deny
	 * mode and we have nothing else to do.
	 */
	if ((fflags & (FREAD|FWRITE)) != 0)
		vn_open_downgrade(cs->vp, fflags);

	/* Update the stateid */
	next_stateid(&sp->rs_stateid);
	resp->open_stateid = sp->rs_stateid.stateid;

	rfs4_dbe_unlock(sp->rs_dbe);

	*cs->statusp = resp->status = NFS4_OK;
	/* Update the lease */
	rfs4_update_lease(sp->rs_owner->ro_client);
	/* And the sequence */
	rfs4_update_open_sequence(sp->rs_owner);
	rfs4_update_open_resp(sp->rs_owner, resop, NULL);

end:
	rfs4_sw_exit(&sp->rs_owner->ro_sw);
	rfs4_state_rele(sp);
out:
	DTRACE_NFSV4_2(op__open__downgrade__done, struct compound_state *, cs,
	    OPEN_DOWNGRADE4res *, resp);
}

static void
rfs4_update_callback(rfs4_client_t *cp, struct compound_state *cs,
    SETCLIENTID4args *args, SETCLIENTID4res *res)
{
	/* Setup callback information */
	rfs4_client_setcb(cp, &args->callback, args->callback_ident);

	/* everything okay -- move ahead */
	*cs->statusp = res->status = NFS4_OK;
	res->SETCLIENTID4res_u.resok4.clientid = cp->rc_clientid;

	/* update the confirm_verifier and return it */
	rfs4_client_scv_next(cp);
	res->SETCLIENTID4res_u.resok4.setclientid_confirm =
	    cp->rc_confirm_verf;
}

static bool_t
rfs4_chk_principal_owner(rfs4_client_t *cp, struct compound_state *cs,
    struct svc_req *req, SETCLIENTID4res *res)
{
	int len;
	char *addr, *netid;
	rfs4_cbinfo_t *cbp;

	/* If creds don't match then client identifier is inuse */
	if (creds_ok(cp->rc_cr_set, req, cs) == TRUE)
		return (TRUE);

	/*
	 * Some one else has established this client
	 * id. Try and say * who they are. We will use
	 * the call back address supplied by * the
	 * first client.
	 */
	*cs->statusp = res->status = NFS4ERR_CLID_INUSE;

	addr = netid = NULL;

	cbp = &cp->rc_cbinfo;
	if (cbp->cb_callback.cb_location.r_addr &&
	    cbp->cb_callback.cb_location.r_netid) {
		cb_client4 *cbcp = &cbp->cb_callback;

		len = strlen(cbcp->cb_location.r_addr)+1;
		addr = kmem_alloc(len, KM_SLEEP);
		bcopy(cbcp->cb_location.r_addr, addr, len);
		len = strlen(cbcp->cb_location.r_netid)+1;
		netid = kmem_alloc(len, KM_SLEEP);
		bcopy(cbcp->cb_location.r_netid, netid, len);
	}

	res->SETCLIENTID4res_u.client_using.r_addr = addr;
	res->SETCLIENTID4res_u.client_using.r_netid = netid;

	return (FALSE);
}

/*
 * The logic behind this function is detailed in the NFSv4 RFC in the
 * SETCLIENTID operation description under IMPLEMENTATION.  Refer to
 * that section for explicit guidance to server behavior for
 * SETCLIENTID.
 */
void
rfs4_op_setclientid(nfs_argop4 *argop, nfs_resop4 *resop,
    struct svc_req *req, struct compound_state *cs)
{
	SETCLIENTID4args *args = &argop->nfs_argop4_u.opsetclientid;
	SETCLIENTID4res *res = &resop->nfs_resop4_u.opsetclientid;
	rfs4_client_t *cp, *newcp, *cp_confirmed, *cp_unconfirmed;
	rfs4_clntip_t *ci;
	bool_t create;

	DTRACE_NFSV4_2(op__setclientid__start, struct compound_state *, cs,
	    SETCLIENTID4args *, args);
retry:
	cp = newcp = cp_confirmed = cp_unconfirmed = NULL;
	create = FALSE;
	args->client.mig_clientid = 0;		/* not hydrating */
	args->client.mig_create = 0;

	/*
	 * Save the caller's IP address
	 */
	args->client.cl_addr =
	    (struct sockaddr *)svc_getrpccaller(req->rq_xprt)->buf;

	/*
	 * Record if it is a Solaris client that cannot handle referrals.
	 */
	if (strstr(args->client.id_val, "Solaris") &&
	    !strstr(args->client.id_val, "+referrals")) {
		/* Add a "yes, it's downrev" record */
		create = TRUE;
		ci = rfs4_find_clntip(cs->rip, args->client.cl_addr, &create);
		ASSERT(ci != NULL);
		rfs4_dbe_rele(ci->ri_dbe);
	} else {
		/* Remove any previous record */
		rfs4_invalidate_clntip(cs->rip, args->client.cl_addr);
	}

	/*
	 * Deal with the callback update case first. The convention is that a
	 * RENEW will preceed SETCLIENTID in the compound if it is a callback
	 * update, so that we can look up the client record using the short
	 * form that comes along with RENEW. The lookup based on the short form
	 * is primarily to find the right client record if there are  confirmed
	 * client records with identical long forms, but different short forms
	 * (as a result of multiple file system migrations from source to the
	 * destination with source reboots in between).
	 */
	if (cs->renew_seen) {
		cp = rfs4_findclient_by_id(cs->rip, cs->clientid, FALSE);
	}

	if (cp == NULL) {

		/*
		 * In search of an EXISTING client matching the incoming
		 * request to establish a new client identifier at the server
		 */
		create = TRUE;
		cp = rfs4_findclient(cs->rip, &args->client, &create, NULL);
	}

	/* Should never happen */
	ASSERT(cp != NULL);

	if (cp == NULL) {
		*cs->statusp = res->status = NFS4ERR_SERVERFAULT;
		goto out;
	}

	/*
	 * Easiest case. Client identifier is newly created and is
	 * unconfirmed.  Also note that for this case, no other
	 * entries exist for the client identifier.  Nothing else to
	 * check.  Just setup the response and respond.
	 */
	if (create) {
		*cs->statusp = res->status = NFS4_OK;
		res->SETCLIENTID4res_u.resok4.clientid = cp->rc_clientid;
		res->SETCLIENTID4res_u.resok4.setclientid_confirm =
		    cp->rc_confirm_verf;
		/* Setup callback information; CB_NULL confirmation later */
		rfs4_client_setcb(cp, &args->callback, args->callback_ident);

		rfs4_client_rele(cp);
		goto out;
	}

	/*
	 * An existing, confirmed client may exist but it may not have
	 * been active for at least one lease period.  If so, then
	 * "close" the client and create a new client identifier
	 */
	if (rfs4_lease_expired(cp)) {
		rfs4_client_close(cp);
		goto retry;
	}

	if (cp->rc_need_confirm == TRUE)
		cp_unconfirmed = cp;
	else
		cp_confirmed = cp;

	cp = NULL;

	/*
	 * We have a confirmed client, now check for an
	 * unconfimred entry
	 */
	if (cp_confirmed) {
		if (rfs4_chk_principal_owner(cp_confirmed, cs, req,
		    res) == FALSE) {
			rfs4_client_rele(cp_confirmed);
			goto out;
		}

		/*
		 * Confirmed, creds match, and verifier matches; must
		 * be an update of the callback info
		 */
		if (cp_confirmed->rc_nfs_client.verifier ==
		    args->client.verifier) {
			rfs4_update_callback(cp_confirmed, cs, args, res);
			rfs4_client_rele(cp_confirmed);
			goto out;
		}

		/*
		 * Creds match but the verifier doesn't.  Must search
		 * for an unconfirmed client that would be replaced by
		 * this request.
		 */
		create = FALSE;
		cp_unconfirmed = rfs4_findclient(cs->rip, &args->client,
		    &create, cp_confirmed);
	}

	/*
	 * At this point, we have taken care of the brand new client
	 * struct, INUSE case, update of an existing, and confirmed
	 * client struct.
	 */

	/*
	 * check to see if things have changed while we originally
	 * picked up the client struct.  If they have, then return and
	 * retry the processing of this SETCLIENTID request.
	 */
	if (cp_unconfirmed) {
		rfs4_dbe_lock(cp_unconfirmed->rc_dbe);
		if (!cp_unconfirmed->rc_need_confirm) {
			rfs4_dbe_unlock(cp_unconfirmed->rc_dbe);
			rfs4_client_rele(cp_unconfirmed);
			if (cp_confirmed)
				rfs4_client_rele(cp_confirmed);
			goto retry;
		}
		/* do away with the old unconfirmed one */
		rfs4_dbe_invalidate(cp_unconfirmed->rc_dbe);
		rfs4_dbe_unlock(cp_unconfirmed->rc_dbe);
		rfs4_client_rele(cp_unconfirmed);
		cp_unconfirmed = NULL;
	}

	/*
	 * This search will temporarily hide the confirmed client
	 * struct while a new client struct is created as the
	 * unconfirmed one.
	 */
	create = TRUE;
	newcp = rfs4_findclient(cs->rip, &args->client, &create, cp_confirmed);

	ASSERT(newcp != NULL);

	if (newcp == NULL) {
		*cs->statusp = res->status = NFS4ERR_SERVERFAULT;
		rfs4_client_rele(cp_confirmed);
		goto out;
	}

	/*
	 * If one was not created, then a similar request must be in
	 * process so release and start over with this one
	 */
	if (create != TRUE) {
		rfs4_client_rele(newcp);
		if (cp_confirmed)
			rfs4_client_rele(cp_confirmed);
		goto retry;
	}

	*cs->statusp = res->status = NFS4_OK;
	res->SETCLIENTID4res_u.resok4.clientid = newcp->rc_clientid;
	res->SETCLIENTID4res_u.resok4.setclientid_confirm =
	    newcp->rc_confirm_verf;
	/* Setup callback information; CB_NULL confirmation later */
	rfs4_client_setcb(newcp, &args->callback, args->callback_ident);

	newcp->rc_cp_confirmed = cp_confirmed;

	rfs4_client_rele(newcp);

out:
	DTRACE_NFSV4_2(op__setclientid__done, struct compound_state *, cs,
	    SETCLIENTID4res *, res);
}

/*ARGSUSED*/
void
rfs4_op_setclientid_confirm(nfs_argop4 *argop, nfs_resop4 *resop,
    struct svc_req *req, struct compound_state *cs)
{
	SETCLIENTID_CONFIRM4args *args =
	    &argop->nfs_argop4_u.opsetclientid_confirm;
	SETCLIENTID_CONFIRM4res *res =
	    &resop->nfs_resop4_u.opsetclientid_confirm;
	rfs4_client_t *cp, *cptoclose = NULL;
	rfs_inst_t *rip = cs->rip;

	DTRACE_NFSV4_2(op__setclientid__confirm__start,
	    struct compound_state *, cs,
	    SETCLIENTID_CONFIRM4args *, args);

	*cs->statusp = res->status = NFS4_OK;

	cp = rfs4_findclient_by_id(rip, args->clientid, TRUE);

	if (cp == NULL) {
		*cs->statusp = res->status =
		    rfs4_check_clientid(rip, &args->clientid, 1);
		goto out;
	}

	if (!creds_ok(cp, req, cs)) {
		*cs->statusp = res->status = NFS4ERR_CLID_INUSE;
		rfs4_client_rele(cp);
		goto out;
	}

	/* If the verifier doesn't match, the record doesn't match */
	if (cp->rc_confirm_verf != args->setclientid_confirm) {
		*cs->statusp = res->status = NFS4ERR_STALE_CLIENTID;
		rfs4_client_rele(cp);
		goto out;
	}

	rfs4_dbe_lock(cp->rc_dbe);
	cp->rc_need_confirm = FALSE;
	if (cp->rc_cp_confirmed) {
		cptoclose = cp->rc_cp_confirmed;
		cptoclose->rc_ss_remove = 1;
		cp->rc_cp_confirmed = NULL;
	}

	/*
	 * Update the client's associated server instance, if it's changed
	 * since the client was created.
	 */
	if (rfs4_clnt_grace(cp) != rip->ri_v4.r4_cur_grace)
		rfs4_clnt_grace_assign(cp, rip->ri_v4.r4_cur_grace);

	/*
	 * Record clientid in stable storage.
	 * Must be done after server instance has been assigned.
	 */
	rfs4_ss_clid(cp);

	rfs4_dbe_unlock(cp->rc_dbe);

	if (cptoclose)
		/* don't need to rele, client_close does it */
		rfs4_client_close(cptoclose);

	/* If needed, initiate CB_NULL call for callback path */
	rfs4_deleg_cb_check(cp);
	rfs4_update_lease(cp);

	/*
	 * Check to see if client can perform reclaims
	 */
	rfs4_ss_chkclid(cp);

	rfs4_client_rele(cp);

out:
	DTRACE_NFSV4_2(op__setclientid__confirm__done,
	    struct compound_state *, cs,
	    SETCLIENTID_CONFIRM4 *, res);
}


/*ARGSUSED*/
void
rfs4_op_close(nfs_argop4 *argop, nfs_resop4 *resop,
    struct svc_req *req, struct compound_state *cs)
{
	CLOSE4args *args = &argop->nfs_argop4_u.opclose;
	CLOSE4res *resp = &resop->nfs_resop4_u.opclose;
	rfs4_state_t *sp;
	nfsstat4 status;

	DTRACE_NFSV4_2(op__close__start, struct compound_state *, cs,
	    CLOSE4args *, args);

	if (cs->vp == NULL) {
		*cs->statusp = resp->status = NFS4ERR_NOFILEHANDLE;
		goto out;
	}

	status = rfs4_get_state(cs, &args->open_stateid, &sp, RFS4_DBS_INVALID);
	if (status != NFS4_OK) {
		*cs->statusp = resp->status = status;
		goto out;
	}

	/* Ensure specified filehandle matches */
	if (cs->vp != sp->rs_finfo->rf_vp) {
		rfs4_state_rele(sp);
		*cs->statusp = resp->status = NFS4ERR_BAD_STATEID;
		goto out;
	}

	/* hold off other access to open_owner while we tinker */
	rfs4_sw_enter(&sp->rs_owner->ro_sw);

	switch (rfs4_check_stateid_seqid(sp, &args->open_stateid)) {
	case NFS4_CHECK_STATEID_OKAY:
		if (rfs4_check_open_seqid(args->seqid, sp->rs_owner,
		    resop) != NFS4_CHKSEQ_OKAY) {
			*cs->statusp = resp->status = NFS4ERR_BAD_SEQID;
			goto end;
		}
		break;
	case NFS4_CHECK_STATEID_OLD:
		*cs->statusp = resp->status = NFS4ERR_OLD_STATEID;
		goto end;
	case NFS4_CHECK_STATEID_BAD:
		*cs->statusp = resp->status = NFS4ERR_BAD_STATEID;
		goto end;
	case NFS4_CHECK_STATEID_EXPIRED:
		*cs->statusp = resp->status = NFS4ERR_EXPIRED;
		goto end;
	case NFS4_CHECK_STATEID_CLOSED:
		*cs->statusp = resp->status = NFS4ERR_OLD_STATEID;
		goto end;
	case NFS4_CHECK_STATEID_UNCONFIRMED:
		*cs->statusp = resp->status = NFS4ERR_BAD_STATEID;
		goto end;
	case NFS4_CHECK_STATEID_REPLAY:
		/* Check the sequence id for the open owner */
		switch (rfs4_check_open_seqid(args->seqid, sp->rs_owner,
		    resop)) {
		case NFS4_CHKSEQ_OKAY:
			/*
			 * This is replayed stateid; if seqid matches
			 * next expected, then client is using wrong seqid.
			 */
			/* FALL THROUGH */
		case NFS4_CHKSEQ_BAD:
			*cs->statusp = resp->status = NFS4ERR_BAD_SEQID;
			goto end;
		case NFS4_CHKSEQ_REPLAY:
			/*
			 * Note this case is the duplicate case so
			 * resp->status is already set.
			 */
			*cs->statusp = resp->status;
			rfs4_update_lease(sp->rs_owner->ro_client);
			goto end;
		}
		break;
	default:
		ASSERT(FALSE);
		break;
	}
	if (cs->lease_moved) {
		rfs4_update_open_sequence(sp->rs_owner);
		*cs->statusp = resp->status = NFS4ERR_LEASE_MOVED;
		goto end;
	}

	rfs4_dbe_lock(sp->rs_dbe);

	/* Update the stateid. */
	next_stateid(&sp->rs_stateid);
	resp->open_stateid = sp->rs_stateid.stateid;

	rfs4_dbe_unlock(sp->rs_dbe);

	rfs4_update_lease(sp->rs_owner->ro_client);
	rfs4_update_open_sequence(sp->rs_owner);
	rfs4_update_open_resp(sp->rs_owner, resop, NULL);

	rfs4_state_close(sp, FALSE, FALSE, cs->cr);

	*cs->statusp = resp->status = status;

end:
	rfs4_sw_exit(&sp->rs_owner->ro_sw);
	rfs4_state_rele(sp);
out:
	DTRACE_NFSV4_2(op__close__done, struct compound_state *, cs,
	    CLOSE4res *, resp);
}

/*
 * Manage the counts on the file struct and close all file locks
 */
/*ARGSUSED*/
void
rfs4_release_share_lock_state(rfs4_state_t *sp, cred_t *cr,
    bool_t close_of_client)
{
	rfs4_file_t *fp = sp->rs_finfo;
	rfs4_lo_state_t *lsp;
	int fflags = 0;

	/*
	 * If this call is part of the larger closing down of client
	 * state then it is just easier to release all locks
	 * associated with this client instead of going through each
	 * individual file and cleaning locks there.
	 */
	if (close_of_client) {
		if (sp->rs_owner->ro_client->rc_unlksys_completed == FALSE &&
		    !list_is_empty(&sp->rs_lostatelist) &&
		    sp->rs_owner->ro_client->rc_sysidt != LM_NOSYSID) {
			/* Is the PxFS kernel module loaded? */
			if (lm_remove_file_locks != NULL) {
				int new_sysid;

				/* Encode the cluster nodeid in new sysid */
				new_sysid = sp->rs_owner->ro_client->rc_sysidt;
				lm_set_nlmid_flk(&new_sysid);

				/*
				 * This PxFS routine removes file locks for a
				 * client over all nodes of a cluster.
				 */
				NFS4_DEBUG(rfs4_debug, (CE_NOTE,
				    "lm_remove_file_locks(sysid=0x%x)\n",
				    new_sysid));
				(*lm_remove_file_locks)(new_sysid);
			} else {
				struct flock64 flk;

				/* Release all locks for this client */
				flk.l_type = F_UNLKSYS;
				flk.l_whence = 0;
				flk.l_start = 0;
				flk.l_len = 0;
				flk.l_sysid =
				    sp->rs_owner->ro_client->rc_sysidt;
				flk.l_pid = 0;
				(void) VOP_FRLOCK(sp->rs_finfo->rf_vp, F_SETLK,
				    &flk, F_REMOTELOCK | FREAD | FWRITE,
				    (u_offset_t)0, NULL, CRED(), NULL);
			}

			sp->rs_owner->ro_client->rc_unlksys_completed = TRUE;
		}
	}

	/*
	 * Release all locks on this file by this lock owner or at
	 * least mark the locks as having been released
	 */
	for (lsp = list_head(&sp->rs_lostatelist); lsp != NULL;
	    lsp = list_next(&sp->rs_lostatelist, lsp)) {
		lsp->rls_locks_cleaned = TRUE;

		/* Was this already taken care of above? */
		if (!close_of_client &&
		    sp->rs_owner->ro_client->rc_sysidt != LM_NOSYSID)
			(void) cleanlocks(sp->rs_finfo->rf_vp,
			    lsp->rls_locker->rl_pid,
			    lsp->rls_locker->rl_client->rc_sysidt);
	}

	/*
	 * Release any shrlocks associated with this open state ID.
	 * This must be done before the rfs4_state gets marked closed.
	 */
	if (sp->rs_owner->ro_client->rc_sysidt != LM_NOSYSID)
		(void) rfs4_unshare(sp);

	if (sp->rs_open_access) {
		rfs4_dbe_lock(fp->rf_dbe);

		/*
		 * Decrement the count for each access and deny bit that this
		 * state has contributed to the file.
		 * If the file counts go to zero
		 * clear the appropriate bit in the appropriate mask.
		 */
		if (sp->rs_open_access & OPEN4_SHARE_ACCESS_READ) {
			fp->rf_access_read--;
			fflags |= FREAD;
			if (fp->rf_access_read == 0)
				fp->rf_share_access &= ~OPEN4_SHARE_ACCESS_READ;
		}
		if (sp->rs_open_access & OPEN4_SHARE_ACCESS_WRITE) {
			fp->rf_access_write--;
			fflags |= FWRITE;
			if (fp->rf_access_write == 0)
				fp->rf_share_access &=
				    ~OPEN4_SHARE_ACCESS_WRITE;
		}
		if (sp->rs_open_deny & OPEN4_SHARE_DENY_READ) {
			fp->rf_deny_read--;
			if (fp->rf_deny_read == 0)
				fp->rf_share_deny &= ~OPEN4_SHARE_DENY_READ;
		}
		if (sp->rs_open_deny & OPEN4_SHARE_DENY_WRITE) {
			fp->rf_deny_write--;
			if (fp->rf_deny_write == 0)
				fp->rf_share_deny &= ~OPEN4_SHARE_DENY_WRITE;
		}

		(void) VOP_CLOSE(fp->rf_vp, fflags, 1, (offset_t)0, cr, NULL);

		rfs4_dbe_unlock(fp->rf_dbe);

		sp->rs_open_access = 0;
		sp->rs_open_deny = 0;
	}
}

/*
 * lock_denied: Fill in a LOCK4deneid structure given an flock64 structure.
 */
static nfsstat4
lock_denied(rfs_inst_t *rip, LOCK4denied *dp, struct flock64 *flk)
{
	rfs4_lockowner_t *lo;
	rfs4_client_t *cp;
	uint32_t len;

	lo = rfs4_findlockowner_by_pid(rip, flk->l_pid);
	if (lo != NULL) {
		cp = lo->rl_client;
		if (rfs4_lease_expired(cp)) {
			rfs4_lockowner_rele(lo);
			rfs4_dbe_hold(cp->rc_dbe);
			rfs4_client_close(cp);
			return (NFS4ERR_EXPIRED);
		}
		dp->owner.clientid = lo->rl_owner.clientid;
		len = lo->rl_owner.owner_len;
		dp->owner.owner_val = kmem_alloc(len, KM_SLEEP);
		bcopy(lo->rl_owner.owner_val, dp->owner.owner_val, len);
		dp->owner.owner_len = len;
		rfs4_lockowner_rele(lo);
		goto finish;
	}

	/*
	 * Its not a NFS4 lock. We take advantage that the upper 32 bits
	 * of the client id contain the boot time for a NFS4 lock. So we
	 * fabricate and identity by setting clientid to the sysid, and
	 * the lock owner to the pid.
	 */
	dp->owner.clientid = flk->l_sysid;
	len = sizeof (pid_t);
	dp->owner.owner_len = len;
	dp->owner.owner_val = kmem_alloc(len, KM_SLEEP);
	bcopy(&flk->l_pid, dp->owner.owner_val, len);
finish:
	dp->offset = flk->l_start;
	dp->length = flk->l_len;

	if (flk->l_type == F_RDLCK)
		dp->locktype = READ_LT;
	else if (flk->l_type == F_WRLCK)
		dp->locktype = WRITE_LT;
	else
		return (NFS4ERR_INVAL);	/* no mapping from POSIX ltype to v4 */

	return (NFS4_OK);
}

int
rfs4_setlock(vnode_t *vp, struct flock64 *flock, int flag, cred_t *cred)
{
	int error;
	struct flock64 flk;
	int i;
	clock_t delaytime;
	int cmd;

	cmd = nbl_need_check(vp) ? F_SETLK_NBMAND : F_SETLK;
retry:
	delaytime = MSEC_TO_TICK_ROUNDUP(rfs4_lock_delay);

	for (i = 0; i < rfs4_maxlock_tries; i++) {
		LOCK_PRINT(rfs4_debug, "setlock", cmd, flock);
		error = VOP_FRLOCK(vp, cmd,
		    flock, flag, (u_offset_t)0, NULL, cred, NULL);

		if (error != EAGAIN && error != EACCES)
			break;

		if (i < rfs4_maxlock_tries - 1) {
			delay(delaytime);
			delaytime *= 2;
		}
	}

	if (error == EAGAIN || error == EACCES) {
		/* Get the owner of the lock */
		flk = *flock;
		LOCK_PRINT(rfs4_debug, "setlock", F_GETLK, &flk);
		if (VOP_FRLOCK(vp, F_GETLK, &flk, flag,
		    (u_offset_t)0, NULL, cred, NULL) == 0) {
			if (flk.l_type == F_UNLCK) {
				/* No longer locked, retry */
				goto retry;
			}
			*flock = flk;
			LOCK_PRINT(rfs4_debug, "setlock(blocking lock)",
			    F_GETLK, &flk);
		}
	}

	return (error);
}

/*ARGSUSED*/
static nfsstat4
rfs4_do_lock(rfs_inst_t *rip, rfs4_lo_state_t *lsp, nfs_lock_type4 locktype,
    offset4 offset, length4 length, cred_t *cred, nfs_resop4 *resop)
{
	nfsstat4 status;
	rfs4_lockowner_t *lo = lsp->rls_locker;
	rfs4_state_t *sp = lsp->rls_state;
	struct flock64 flock;
	int16_t ltype;
	int flag;
	int error;
	sysid_t sysid;
	LOCK4res *lres;

	if (rfs4_lease_expired(lo->rl_client)) {
		return (NFS4ERR_EXPIRED);
	}

	if ((status = rfs4_client_sysid(lo->rl_client, &sysid)) != NFS4_OK)
		return (status);

	/* Check for zero length. To lock to end of file use all ones for V4 */
	if (length == 0)
		return (NFS4ERR_INVAL);
	else if (length == (length4)(~0))
		length = 0;		/* Posix to end of file  */

retry:
	rfs4_dbe_lock(sp->rs_dbe);
	if (sp->rs_closed) {
		rfs4_dbe_unlock(sp->rs_dbe);
		return (NFS4ERR_OLD_STATEID);
	}

	if (resop->resop != OP_LOCKU) {
		switch (locktype) {
		case READ_LT:
		case READW_LT:
			if ((sp->rs_share_access
			    & OPEN4_SHARE_ACCESS_READ) == 0) {
				rfs4_dbe_unlock(sp->rs_dbe);

				return (NFS4ERR_OPENMODE);
			}
			ltype = F_RDLCK;
			break;
		case WRITE_LT:
		case WRITEW_LT:
			if ((sp->rs_share_access
			    & OPEN4_SHARE_ACCESS_WRITE) == 0) {
				rfs4_dbe_unlock(sp->rs_dbe);

				return (NFS4ERR_OPENMODE);
			}
			ltype = F_WRLCK;
			break;
		default:
			rfs4_dbe_unlock(sp->rs_dbe);
			return (NFS4ERR_INVAL);
		}
	} else
		ltype = F_UNLCK;

	flock.l_type = ltype;
	flock.l_whence = 0;		/* SEEK_SET */
	flock.l_start = offset;
	flock.l_len = length;
	flock.l_sysid = sysid;
	flock.l_pid = lsp->rls_locker->rl_pid;

	/* Note that length4 is uint64_t but l_len and l_start are off64_t */
	if (flock.l_len < 0 || flock.l_start < 0) {
		rfs4_dbe_unlock(sp->rs_dbe);
		return (NFS4ERR_INVAL);
	}

	/*
	 * N.B. FREAD has the same value as OPEN4_SHARE_ACCESS_READ and
	 * FWRITE has the same value as OPEN4_SHARE_ACCESS_WRITE.
	 */
	flag = (int)sp->rs_share_access | F_REMOTELOCK;

	error = rfs4_setlock(sp->rs_finfo->rf_vp, &flock, flag, cred);
	if (error == 0) {
		rfs4_dbe_lock(lsp->rls_dbe);
		next_stateid(&lsp->rls_lockid);
		rfs4_dbe_unlock(lsp->rls_dbe);
	}

	rfs4_dbe_unlock(sp->rs_dbe);

	/*
	 * N.B. We map error values to nfsv4 errors. This is differrent
	 * than puterrno4 routine.
	 */
	switch (error) {
	case 0:
		status = NFS4_OK;
		break;
	case EAGAIN:
	case EACCES:		/* Old value */
		/* Can only get here if op is OP_LOCK */
		ASSERT(resop->resop == OP_LOCK);
		lres = &resop->nfs_resop4_u.oplock;
		status = NFS4ERR_DENIED;
		if (lock_denied(rip, &lres->LOCK4res_u.denied, &flock)
		    == NFS4ERR_EXPIRED)
			goto retry;
		break;
	case ENOLCK:
		status = NFS4ERR_DELAY;
		break;
	case EOVERFLOW:
		status = NFS4ERR_INVAL;
		break;
	case EINVAL:
		status = NFS4ERR_NOTSUPP;
		break;
	default:
		status = NFS4ERR_SERVERFAULT;
		break;
	}

	return (status);
}

/*ARGSUSED*/
void
rfs4_op_lock(nfs_argop4 *argop, nfs_resop4 *resop,
    struct svc_req *req, struct compound_state *cs)
{
	LOCK4args *args = &argop->nfs_argop4_u.oplock;
	LOCK4res *resp = &resop->nfs_resop4_u.oplock;
	nfsstat4 status;
	stateid4 *stateid;
	rfs4_lockowner_t *lo;
	rfs4_client_t *cp;
	rfs4_state_t *sp = NULL;
	rfs4_lo_state_t *lsp = NULL;
	bool_t ls_sw_held = FALSE;
	bool_t create = TRUE;
	bool_t lcreate = TRUE;
	bool_t dup_lock = FALSE;
	int rc, can_reclaim;

	DTRACE_NFSV4_2(op__lock__start, struct compound_state *, cs,
	    LOCK4args *, args);

	if (cs->vp == NULL) {
		*cs->statusp = resp->status = NFS4ERR_NOFILEHANDLE;
		DTRACE_NFSV4_2(op__lock__done, struct compound_state *,
		    cs, LOCK4res *, resp);
		return;
	}

	if (args->locker.new_lock_owner) {
		/* Create a new lockowner for this instance */
		open_to_lock_owner4 *olo = &args->locker.locker4_u.open_owner;

		NFS4_DEBUG(rfs4_debug, (CE_NOTE, "Creating new lock owner"));

		stateid = &olo->open_stateid;
		status = rfs4_get_state(cs, stateid, &sp, RFS4_DBS_VALID);
		if (status != NFS4_OK) {
			NFS4_DEBUG(rfs4_debug,
			    (CE_NOTE, "Get state failed in lock %d", status));
			*cs->statusp = resp->status = status;
			DTRACE_NFSV4_2(op__lock__done, struct compound_state *,
			    cs, LOCK4res *, resp);
			return;
		}

		/* Ensure specified filehandle matches */
		if (cs->vp != sp->rs_finfo->rf_vp) {
			rfs4_state_rele(sp);
			*cs->statusp = resp->status = NFS4ERR_BAD_STATEID;
			DTRACE_NFSV4_2(op__lock__done, struct compound_state *,
			    cs, LOCK4res *, resp);
			return;
		}

		/* hold off other access to open_owner while we tinker */
		rfs4_sw_enter(&sp->rs_owner->ro_sw);

		switch (rc = rfs4_check_stateid_seqid(sp, stateid)) {
		case NFS4_CHECK_STATEID_OLD:
			*cs->statusp = resp->status = NFS4ERR_OLD_STATEID;
			goto end;
		case NFS4_CHECK_STATEID_BAD:
			*cs->statusp = resp->status = NFS4ERR_BAD_STATEID;
			goto end;
		case NFS4_CHECK_STATEID_EXPIRED:
			*cs->statusp = resp->status = NFS4ERR_EXPIRED;
			goto end;
		case NFS4_CHECK_STATEID_UNCONFIRMED:
			*cs->statusp = resp->status = NFS4ERR_BAD_STATEID;
			goto end;
		case NFS4_CHECK_STATEID_CLOSED:
			*cs->statusp = resp->status = NFS4ERR_OLD_STATEID;
			goto end;
		case NFS4_CHECK_STATEID_OKAY:
		case NFS4_CHECK_STATEID_REPLAY:
			switch (rfs4_check_olo_seqid(olo->open_seqid,
			    sp->rs_owner, resop)) {
			case NFS4_CHKSEQ_OKAY:
				if (rc == NFS4_CHECK_STATEID_OKAY)
					break;
				/*
				 * This is replayed stateid; if seqid
				 * matches next expected, then client
				 * is using wrong seqid.
				 */
				/* FALLTHROUGH */
			case NFS4_CHKSEQ_BAD:
				*cs->statusp = resp->status = NFS4ERR_BAD_SEQID;
				goto end;
			case NFS4_CHKSEQ_REPLAY:
				/* This is a duplicate LOCK request */
				dup_lock = TRUE;

				/*
				 * For a duplicate we do not want to
				 * create a new lockowner as it should
				 * already exist.
				 * Turn off the lockowner create flag.
				 */
				lcreate = FALSE;
			}
			break;
		}

		lo = rfs4_findlockowner(cs->rip, &olo->lock_owner, &lcreate);
		if (lo == NULL) {
			NFS4_DEBUG(rfs4_debug,
			    (CE_NOTE, "rfs4_op_lock: no lock owner"));
			*cs->statusp = resp->status = NFS4ERR_RESOURCE;
			goto end;
		}

		lsp = rfs4_findlo_state_by_owner(cs, lo, sp, &create);
		if (lsp == NULL) {
			rfs4_update_lease(sp->rs_owner->ro_client);
			/*
			 * Only update theh open_seqid if this is not
			 * a duplicate request
			 */
			if (dup_lock == FALSE) {
				rfs4_update_open_sequence(sp->rs_owner);
			}

			NFS4_DEBUG(rfs4_debug,
			    (CE_NOTE, "rfs4_op_lock: no state"));
			*cs->statusp = resp->status = NFS4ERR_SERVERFAULT;
			rfs4_update_open_resp(sp->rs_owner, resop, NULL);
			rfs4_lockowner_rele(lo);
			goto end;
		}

		/*
		 * This is the new_lock_owner branch and the client is
		 * supposed to be associating a new lock_owner with
		 * the open file at this point.  If we find that a
		 * lock_owner/state association already exists and a
		 * successful LOCK request was returned to the client,
		 * an error is returned to the client since this is
		 * not appropriate.  The client should be using the
		 * existing lock_owner branch.
		 */
		if (dup_lock == FALSE && create == FALSE) {
			if (lsp->rls_lock_completed == TRUE) {
				*cs->statusp =
				    resp->status = NFS4ERR_BAD_SEQID;
				rfs4_lockowner_rele(lo);
				goto end;
			}
		}

		rfs4_update_lease(sp->rs_owner->ro_client);

		/*
		 * Only update theh open_seqid if this is not
		 * a duplicate request
		 */
		if (dup_lock == FALSE) {
			rfs4_update_open_sequence(sp->rs_owner);
		}

		/*
		 * If this is a duplicate lock request, just copy the
		 * previously saved reply and return.
		 */
		if (dup_lock == TRUE) {
			/* verify that lock_seqid's match */
			if (lsp->rls_seqid != olo->lock_seqid) {
				NFS4_DEBUG(rfs4_debug,
				    (CE_NOTE, "rfs4_op_lock: Dup-Lock seqid bad"
				    "lsp->seqid=%d old->seqid=%d",
				    lsp->rls_seqid, olo->lock_seqid));
				*cs->statusp = resp->status = NFS4ERR_BAD_SEQID;
			} else {
				rfs4_copy_reply(resop, &lsp->rls_reply);
				/*
				 * Make sure to copy the just
				 * retrieved reply status into the
				 * overall compound status
				 */
				*cs->statusp = resp->status;
			}
			rfs4_lockowner_rele(lo);
			goto end;
		}

		rfs4_dbe_lock(lsp->rls_dbe);

		/* Make sure to update the lock sequence id */
		lsp->rls_seqid = olo->lock_seqid;

		NFS4_DEBUG(rfs4_debug,
		    (CE_NOTE, "Lock seqid established as %d", lsp->rls_seqid));

		/*
		 * This is used to signify the newly created lockowner
		 * stateid and its sequence number.  The checks for
		 * sequence number and increment don't occur on the
		 * very first lock request for a lockowner.
		 */
		lsp->rls_skip_seqid_check = TRUE;

		/* hold off other access to lsp while we tinker */
		rfs4_sw_enter(&lsp->rls_sw);
		ls_sw_held = TRUE;

		rfs4_dbe_unlock(lsp->rls_dbe);

		rfs4_lockowner_rele(lo);
	} else {
		stateid = &args->locker.locker4_u.lock_owner.lock_stateid;
		/* get lsp and hold the lock on the underlying file struct */
		if ((status = rfs4_get_lo_state(cs, stateid, &lsp, TRUE))
		    != NFS4_OK) {
			*cs->statusp = resp->status = status;
			DTRACE_NFSV4_2(op__lock__done, struct compound_state *,
			    cs, LOCK4res *, resp);
			return;
		}
		create = FALSE;	/* We didn't create lsp */

		/* Ensure specified filehandle matches */
		if (cs->vp != lsp->rls_state->rs_finfo->rf_vp) {
			rfs4_lo_state_rele(lsp, TRUE);
			*cs->statusp = resp->status = NFS4ERR_BAD_STATEID;
			DTRACE_NFSV4_2(op__lock__done, struct compound_state *,
			    cs, LOCK4res *, resp);
			return;
		}

		/* hold off other access to lsp while we tinker */
		rfs4_sw_enter(&lsp->rls_sw);
		ls_sw_held = TRUE;

		switch (rfs4_check_lo_stateid_seqid(lsp, stateid)) {
		/*
		 * The stateid looks like it was okay (expected to be
		 * the next one)
		 */
		case NFS4_CHECK_STATEID_OKAY:
			/*
			 * The sequence id is now checked.  Determine
			 * if this is a replay or if it is in the
			 * expected (next) sequence.  In the case of a
			 * replay, there are two replay conditions
			 * that may occur.  The first is the normal
			 * condition where a LOCK is done with a
			 * NFS4_OK response and the stateid is
			 * updated.  That case is handled below when
			 * the stateid is identified as a REPLAY.  The
			 * second is the case where an error is
			 * returned, like NFS4ERR_DENIED, and the
			 * sequence number is updated but the stateid
			 * is not updated.  This second case is dealt
			 * with here.  So it may seem odd that the
			 * stateid is okay but the sequence id is a
			 * replay but it is okay.
			 */
			switch (rfs4_check_lock_seqid(
			    args->locker.locker4_u.lock_owner.lock_seqid,
			    lsp, resop)) {
			case NFS4_CHKSEQ_REPLAY:
				if (resp->status != NFS4_OK) {
					/*
					 * Here is our replay and need
					 * to verify that the last
					 * response was an error.
					 */
					*cs->statusp = resp->status;
					goto end;
				}
				/*
				 * This is done since the sequence id
				 * looked like a replay but it didn't
				 * pass our check so a BAD_SEQID is
				 * returned as a result.
				 */
				/*FALLTHROUGH*/
			case NFS4_CHKSEQ_BAD:
				*cs->statusp = resp->status = NFS4ERR_BAD_SEQID;
				goto end;
			case NFS4_CHKSEQ_OKAY:
				/* Everything looks okay move ahead */
				break;
			}
			break;
		case NFS4_CHECK_STATEID_OLD:
			*cs->statusp = resp->status = NFS4ERR_OLD_STATEID;
			goto end;
		case NFS4_CHECK_STATEID_BAD:
			*cs->statusp = resp->status = NFS4ERR_BAD_STATEID;
			goto end;
		case NFS4_CHECK_STATEID_EXPIRED:
			*cs->statusp = resp->status = NFS4ERR_EXPIRED;
			goto end;
		case NFS4_CHECK_STATEID_CLOSED:
			*cs->statusp = resp->status = NFS4ERR_OLD_STATEID;
			goto end;
		case NFS4_CHECK_STATEID_REPLAY:
			switch (rfs4_check_lock_seqid(
			    args->locker.locker4_u.lock_owner.lock_seqid,
			    lsp, resop)) {
			case NFS4_CHKSEQ_OKAY:
				/*
				 * This is a replayed stateid; if
				 * seqid matches the next expected,
				 * then client is using wrong seqid.
				 */
			case NFS4_CHKSEQ_BAD:
				*cs->statusp = resp->status = NFS4ERR_BAD_SEQID;
				goto end;
			case NFS4_CHKSEQ_REPLAY:
				rfs4_update_lease(lsp->rls_locker->rl_client);
				*cs->statusp = status = resp->status;
				goto end;
			}
			break;
		default:
			ASSERT(FALSE);
			break;
		}

		rfs4_update_lock_sequence(lsp);
		rfs4_update_lease(lsp->rls_locker->rl_client);
	}

	/*
	 * NFS4 only allows locking on regular files, so
	 * verify type of object.
	 */
	if (cs->vp->v_type != VREG) {
		if (cs->vp->v_type == VDIR)
			status = NFS4ERR_ISDIR;
		else
			status = NFS4ERR_INVAL;
		goto out;
	}

	cp = lsp->rls_state->rs_owner->ro_client;

	if (rfs4_in_grace(cp, cs) && !args->reclaim) {
		status = NFS4ERR_GRACE;
		goto out;
	}

	can_reclaim = cp->rc_can_reclaim;
	/* only do this when not using tsm */
	if (!rfs4_do_tsm && rfs4_in_grace(cp, cs))
		can_reclaim = 1;

	if (rfs4_in_grace(cp, cs) && args->reclaim && !can_reclaim) {
		status = NFS4ERR_NO_GRACE;
		goto out;
	}

	if (!rfs4_in_grace(cp, cs) && args->reclaim) {
		status = NFS4ERR_NO_GRACE;
		goto out;
	}

	if (cs->lease_moved) {
		status = NFS4ERR_LEASE_MOVED;
		goto out;
	}

	if (lsp->rls_state->rs_finfo->rf_dinfo.rd_dtype == OPEN_DELEGATE_WRITE)
		cs->deleg = TRUE;

	status = rfs4_do_lock(cs->rip, lsp, args->locktype,
	    args->offset, args->length, cs->cr, resop);

out:
	lsp->rls_skip_seqid_check = FALSE;

	*cs->statusp = resp->status = status;

	if (status == NFS4_OK) {
		resp->LOCK4res_u.lock_stateid = lsp->rls_lockid.stateid;
		lsp->rls_lock_completed = TRUE;
	}
	/*
	 * Only update the "OPEN" response here if this was a new
	 * lock_owner
	 */
	if (sp)
		rfs4_update_open_resp(sp->rs_owner, resop, NULL);

	rfs4_update_lock_resp(lsp, resop);

end:
	if (lsp) {
		if (ls_sw_held)
			rfs4_sw_exit(&lsp->rls_sw);
		/*
		 * If an sp obtained, then the lsp does not represent
		 * a lock on the file struct.
		 */
		if (sp != NULL)
			rfs4_lo_state_rele(lsp, FALSE);
		else
			rfs4_lo_state_rele(lsp, TRUE);
	}
	if (sp) {
		rfs4_sw_exit(&sp->rs_owner->ro_sw);
		rfs4_state_rele(sp);
	}

	DTRACE_NFSV4_2(op__lock__done, struct compound_state *, cs,
	    LOCK4res *, resp);
}

/* free function for LOCK/LOCKT */
static void
lock_denied_free(nfs_resop4 *resop)
{
	LOCK4denied *dp = NULL;

	switch (resop->resop) {
	case OP_LOCK:
		if (resop->nfs_resop4_u.oplock.status == NFS4ERR_DENIED)
			dp = &resop->nfs_resop4_u.oplock.LOCK4res_u.denied;
		break;
	case OP_LOCKT:
		if (resop->nfs_resop4_u.oplockt.status == NFS4ERR_DENIED)
			dp = &resop->nfs_resop4_u.oplockt.denied;
		break;
	default:
		break;
	}

	if (dp)
		kmem_free(dp->owner.owner_val, dp->owner.owner_len);
}

/*ARGSUSED*/
void
rfs4_op_locku(nfs_argop4 *argop, nfs_resop4 *resop,
    struct svc_req *req, struct compound_state *cs)
{
	LOCKU4args *args = &argop->nfs_argop4_u.oplocku;
	LOCKU4res *resp = &resop->nfs_resop4_u.oplocku;
	nfsstat4 status;
	stateid4 *stateid = &args->lock_stateid;
	rfs4_lo_state_t *lsp;

	DTRACE_NFSV4_2(op__locku__start, struct compound_state *, cs,
	    LOCKU4args *, args);

	if (cs->vp == NULL) {
		*cs->statusp = resp->status = NFS4ERR_NOFILEHANDLE;
		DTRACE_NFSV4_2(op__locku__done, struct compound_state *, cs,
		    LOCKU4res *, resp);
		return;
	}

	if ((status = rfs4_get_lo_state(cs, stateid, &lsp, TRUE)) != NFS4_OK) {
		*cs->statusp = resp->status = status;
		DTRACE_NFSV4_2(op__locku__done, struct compound_state *, cs,
		    LOCKU4res *, resp);
		return;
	}

	/* Ensure specified filehandle matches */
	if (cs->vp != lsp->rls_state->rs_finfo->rf_vp) {
		rfs4_lo_state_rele(lsp, TRUE);
		*cs->statusp = resp->status = NFS4ERR_BAD_STATEID;
		DTRACE_NFSV4_2(op__locku__done, struct compound_state *, cs,
		    LOCKU4res *, resp);
		return;
	}

	/* hold off other access to lsp while we tinker */
	rfs4_sw_enter(&lsp->rls_sw);

	switch (rfs4_check_lo_stateid_seqid(lsp, stateid)) {
	case NFS4_CHECK_STATEID_OKAY:
		if (rfs4_check_lock_seqid(args->seqid, lsp, resop)
		    != NFS4_CHKSEQ_OKAY) {
			*cs->statusp = resp->status = NFS4ERR_BAD_SEQID;
			goto end;
		}
		break;
	case NFS4_CHECK_STATEID_OLD:
		*cs->statusp = resp->status = NFS4ERR_OLD_STATEID;
		goto end;
	case NFS4_CHECK_STATEID_BAD:
		*cs->statusp = resp->status = NFS4ERR_BAD_STATEID;
		goto end;
	case NFS4_CHECK_STATEID_EXPIRED:
		*cs->statusp = resp->status = NFS4ERR_EXPIRED;
		goto end;
	case NFS4_CHECK_STATEID_CLOSED:
		*cs->statusp = resp->status = NFS4ERR_OLD_STATEID;
		goto end;
	case NFS4_CHECK_STATEID_REPLAY:
		switch (rfs4_check_lock_seqid(args->seqid, lsp, resop)) {
		case NFS4_CHKSEQ_OKAY:
				/*
				 * This is a replayed stateid; if
				 * seqid matches the next expected,
				 * then client is using wrong seqid.
				 */
		case NFS4_CHKSEQ_BAD:
			*cs->statusp = resp->status = NFS4ERR_BAD_SEQID;
			goto end;
		case NFS4_CHKSEQ_REPLAY:
			rfs4_update_lease(lsp->rls_locker->rl_client);
			*cs->statusp = status = resp->status;
			goto end;
		}
		break;
	default:
		ASSERT(FALSE);
		break;
	}

	rfs4_update_lock_sequence(lsp);
	rfs4_update_lease(lsp->rls_locker->rl_client);

	/*
	 * NFS4 only allows locking on regular files, so
	 * verify type of object.
	 */
	if (cs->vp->v_type != VREG) {
		if (cs->vp->v_type == VDIR)
			status = NFS4ERR_ISDIR;
		else
			status = NFS4ERR_INVAL;
		goto out;
	}

	if (rfs4_in_grace(lsp->rls_state->rs_owner->ro_client, cs)) {
		status = NFS4ERR_GRACE;
		goto out;
	}

	if (cs->lease_moved) {
		status = NFS4ERR_LEASE_MOVED;
		goto out;
	}

	status = rfs4_do_lock(cs->rip, lsp, args->locktype,
	    args->offset, args->length, cs->cr, resop);

out:
	*cs->statusp = resp->status = status;

	if (status == NFS4_OK)
		resp->lock_stateid = lsp->rls_lockid.stateid;

	rfs4_update_lock_resp(lsp, resop);

end:
	rfs4_sw_exit(&lsp->rls_sw);
	rfs4_lo_state_rele(lsp, TRUE);

	DTRACE_NFSV4_2(op__locku__done, struct compound_state *, cs,
	    LOCKU4res *, resp);
}

/*
 * LOCKT is a best effort routine, the client can not be guaranteed that
 * the status return is still in effect by the time the reply is received.
 * They are numerous race conditions in this routine, but we are not required
 * and can not be accurate.
 */
/*ARGSUSED*/
void
rfs4_op_lockt(nfs_argop4 *argop, nfs_resop4 *resop,
    struct svc_req *req, struct compound_state *cs)
{
	LOCKT4args *args = &argop->nfs_argop4_u.oplockt;
	LOCKT4res *resp = &resop->nfs_resop4_u.oplockt;
	rfs4_lockowner_t *lo;
	rfs4_client_t *cp;
	bool_t create = FALSE;
	struct flock64 flk;
	int error;
	int flag = FREAD | FWRITE;
	int ltype, can_reclaim;
	length4 posix_length;
	sysid_t sysid;
	pid_t pid;

	DTRACE_NFSV4_2(op__lockt__start, struct compound_state *, cs,
	    LOCKT4args *, args);

	if (cs->vp == NULL) {
		*cs->statusp = resp->status = NFS4ERR_NOFILEHANDLE;
		goto out;
	}

	/*
	 * NFS4 only allows locking on regular files, so
	 * verify type of object.
	 */
	if (cs->vp->v_type != VREG) {
		if (cs->vp->v_type == VDIR)
			*cs->statusp = resp->status = NFS4ERR_ISDIR;
		else
			*cs->statusp = resp->status =  NFS4ERR_INVAL;
		goto out;
	}

	/*
	 * Check out the clientid to ensure the server knows about it
	 * so that we correctly inform the client of a server reboot.
	 */
	cp = rfs4_findclient_by_id(cs->rip, args->owner.clientid, FALSE);
	if (cp == NULL) {
		*cs->statusp = resp->status =
		    rfs4_check_clientid(cs->rip, &args->owner.clientid, 0);
		goto out;
	}
	if (rfs4_lease_expired(cp)) {
		rfs4_client_close(cp);
		/*
		 * Protocol doesn't allow returning NFS4ERR_EXPIRED as
		 * other operations do on this check so STALE_CLIENTID
		 * is returned instead
		 */
		*cs->statusp = resp->status = NFS4ERR_STALE_CLIENTID;
		goto out;
	}

	can_reclaim = cp->rc_can_reclaim;
	/* only do this when not using tsm */
	if (!rfs4_do_tsm && rfs4_in_grace(cp, cs))
		can_reclaim = 1;

	if (rfs4_in_grace(cp, cs) && !can_reclaim) {
		*cs->statusp = resp->status = NFS4ERR_GRACE;
		rfs4_client_rele(cp);
		goto out;
	}
	if (rfs4_lease_moved(cp, NULL)) {
		rfs4_client_rele(cp);
		*cs->statusp = resp->status = NFS4ERR_LEASE_MOVED;
		goto out;
	}
	rfs4_client_rele(cp);

	resp->status = NFS4_OK;

	switch (args->locktype) {
	case READ_LT:
	case READW_LT:
		ltype = F_RDLCK;
		break;
	case WRITE_LT:
	case WRITEW_LT:
		ltype = F_WRLCK;
		break;
	}

	posix_length = args->length;
	/* Check for zero length. To lock to end of file use all ones for V4 */
	if (posix_length == 0) {
		*cs->statusp = resp->status = NFS4ERR_INVAL;
		goto out;
	} else if (posix_length == (length4)(~0)) {
		posix_length = 0;	/* Posix to end of file  */
	}

	/* Find or create a lockowner */
	lo = rfs4_findlockowner(cs->rip, &args->owner, &create);

	if (lo) {
		pid = lo->rl_pid;
		if ((resp->status =
		    rfs4_client_sysid(lo->rl_client, &sysid)) != NFS4_OK)
			goto err;
	} else {
		pid = 0;
		sysid = rfs.rg_v4.rg4_lockt_sysid;
	}
retry:
	flk.l_type = ltype;
	flk.l_whence = 0;		/* SEEK_SET */
	flk.l_start = args->offset;
	flk.l_len = posix_length;
	flk.l_sysid = sysid;
	flk.l_pid = pid;
	flag |= F_REMOTELOCK;

	LOCK_PRINT(rfs4_debug, "rfs4_op_lockt", F_GETLK, &flk);

	/* Note that length4 is uint64_t but l_len and l_start are off64_t */
	if (flk.l_len < 0 || flk.l_start < 0) {
		resp->status = NFS4ERR_INVAL;
		goto err;
	}
	error = VOP_FRLOCK(cs->vp, F_GETLK, &flk, flag, (u_offset_t)0,
	    NULL, cs->cr, NULL);

	/*
	 * N.B. We map error values to nfsv4 errors. This is differrent
	 * than puterrno4 routine.
	 */
	switch (error) {
	case 0:
		if (flk.l_type == F_UNLCK)
			resp->status = NFS4_OK;
		else {
			if (lock_denied(cs->rip, &resp->denied, &flk) ==
			    NFS4ERR_EXPIRED)
				goto retry;
			resp->status = NFS4ERR_DENIED;
		}
		break;
	case EOVERFLOW:
		resp->status = NFS4ERR_INVAL;
		break;
	case EINVAL:
		resp->status = NFS4ERR_NOTSUPP;
		break;
	default:
		zcmn_err(getzoneid(), CE_WARN,
		    "rfs4_op_lockt: unexpected errno (%d)", error);
		resp->status = NFS4ERR_SERVERFAULT;
		break;
	}

err:
	if (lo)
		rfs4_lockowner_rele(lo);
	*cs->statusp = resp->status;
out:
	DTRACE_NFSV4_2(op__lockt__done, struct compound_state *, cs,
	    LOCKT4res *, resp);
}

int
rfs4_share(rfs4_state_t *sp, uint32_t access, uint32_t deny)
{
	int err;
	int cmd;
	vnode_t *vp;
	struct shrlock shr;
	struct shr_locowner shr_loco;
	int fflags = 0;

	ASSERT(rfs4_dbe_islocked(sp->rs_dbe));
	ASSERT(sp->rs_owner->ro_client->rc_sysidt != LM_NOSYSID);

	if (sp->rs_closed)
		return (NFS4ERR_OLD_STATEID);

	vp = sp->rs_finfo->rf_vp;
	ASSERT(vp);

	shr.s_access = shr.s_deny = 0;

	if (access & OPEN4_SHARE_ACCESS_READ) {
		fflags |= FREAD;
		shr.s_access |= F_RDACC;
	}
	if (access & OPEN4_SHARE_ACCESS_WRITE) {
		fflags |= FWRITE;
		shr.s_access |= F_WRACC;
	}
	ASSERT(shr.s_access);

	if (deny & OPEN4_SHARE_DENY_READ)
		shr.s_deny |= F_RDDNY;
	if (deny & OPEN4_SHARE_DENY_WRITE)
		shr.s_deny |= F_WRDNY;

	shr.s_pid = rfs4_dbe_getid(sp->rs_owner->ro_dbe);
	shr.s_sysid = sp->rs_owner->ro_client->rc_sysidt;
	shr_loco.sl_pid = shr.s_pid;
	shr_loco.sl_id = shr.s_sysid;
	shr.s_owner = (caddr_t)&shr_loco;
	shr.s_own_len = sizeof (shr_loco);

	cmd = nbl_need_check(vp) ? F_SHARE_NBMAND : F_SHARE;

	err = VOP_SHRLOCK(vp, cmd, &shr, fflags, CRED(), NULL);
	if (err != 0) {
		if (err == EAGAIN)
			err = NFS4ERR_SHARE_DENIED;
		else
			err = puterrno4(err);
		return (err);
	}

	sp->rs_share_access |= access;
	sp->rs_share_deny |= deny;

	return (0);
}

int
rfs4_unshare(rfs4_state_t *sp)
{
	int err;
	struct shrlock shr;
	struct shr_locowner shr_loco;

	ASSERT(rfs4_dbe_islocked(sp->rs_dbe));

	if (sp->rs_closed || sp->rs_share_access == 0)
		return (0);

	ASSERT(sp->rs_owner->ro_client->rc_sysidt != LM_NOSYSID);
	ASSERT(sp->rs_finfo->rf_vp);

	shr.s_access = shr.s_deny = 0;
	shr.s_pid = rfs4_dbe_getid(sp->rs_owner->ro_dbe);
	shr.s_sysid = sp->rs_owner->ro_client->rc_sysidt;
	shr_loco.sl_pid = shr.s_pid;
	shr_loco.sl_id = shr.s_sysid;
	shr.s_owner = (caddr_t)&shr_loco;
	shr.s_own_len = sizeof (shr_loco);

	err = VOP_SHRLOCK(sp->rs_finfo->rf_vp, F_UNSHARE, &shr, 0, CRED(),
	    NULL);
	if (err != 0) {
		err = puterrno4(err);
		return (err);
	}

	sp->rs_share_access = 0;
	sp->rs_share_deny = 0;

	return (0);

}

static int
rdma_setup_read_data4(READ4args *args, READ4res *rok)
{
	struct clist	*wcl;
	count4		count = rok->data_len;
	int		wlist_len;

	wcl = args->wlist;
	if (rdma_setup_read_chunks(wcl, count, &wlist_len) == FALSE) {
		return (FALSE);
	}
	wcl = args->wlist;
	rok->wlist_len = wlist_len;
	rok->wlist = wcl;
	return (TRUE);
}

/* tunable to disable server referrals */
int rfs4_no_referrals = 0;

/*
 * Find an NFS record in reparse point data.
 * Returns 0 for success and <0 or an errno value on failure.
 */
int
vn_find_nfs_record(vnode_t *vp, nvlist_t **nvlp, char **svcp, char **datap)
{
	int err;
	char *stype, *val;
	nvlist_t *nvl;
	nvpair_t *curr;

	if ((nvl = reparse_init()) == NULL)
		return (-1);

	if ((err = reparse_vnode_parse(vp, nvl)) != 0) {
		reparse_free(nvl);
		return (err);
	}

	curr = NULL;
	while ((curr = nvlist_next_nvpair(nvl, curr)) != NULL) {
		if ((stype = nvpair_name(curr)) == NULL) {
			reparse_free(nvl);
			return (-2);
		}
		if (strncasecmp(stype, "NFS", 3) == 0)
			break;
	}

	if ((curr == NULL) ||
	    (nvpair_value_string(curr, &val))) {
		reparse_free(nvl);
		return (-3);
	}

	*nvlp = nvl;
	*svcp = stype;
	*datap = val;
	return (0);
}

/*
 * Check to see if this object has some kind of referral data
 * available (which also means a client will see NFS4ERR_MOVED).
 */
int
vn_is_nfs_reparse(vnode_t *vp, cred_t *cr)
{
	nvlist_t *nvl;
	char *s, *d;

	if (rfs4_no_referrals != 0)
		return (B_FALSE);

	if (vfs_is_migrated(vp->v_vfsp, cr) == B_FALSE &&
	    vn_is_reparse(vp, cr, NULL) == B_FALSE)
		return (B_FALSE);

	if (vn_find_nfs_record(vp, &nvl, &s, &d) != 0)
		return (B_FALSE);

	reparse_free(nvl);

	return (B_TRUE);
}

/*
 * There is a user-level copy of this routine in ref_subr.c.
 * Changes should be kept in sync.
 */
static int
nfs4_create_components(char *path, component4 *comp4)
{
	int slen, plen, ncomp;
	char *ori_path, *nxtc, buf[MAXNAMELEN];

	if (path == NULL)
		return (0);

	plen = strlen(path) + 1;	/* include the terminator */
	ori_path = path;
	ncomp = 0;

	/* count number of components in the path */
	for (nxtc = path; nxtc < ori_path + plen; nxtc++) {
		if (*nxtc == '/' || *nxtc == '\0' || *nxtc == '\n') {
			if ((slen = nxtc - path) == 0) {
				path = nxtc + 1;
				continue;
			}

			if (comp4 != NULL) {
				bcopy(path, buf, slen);
				buf[slen] = '\0';
				(void) str_to_utf8(buf, &comp4[ncomp]);
			}

			ncomp++;	/* 1 valid component */
			path = nxtc + 1;
		}
		if (*nxtc == '\0' || *nxtc == '\n')
			break;
	}

	return (ncomp);
}

/*
 * There is a user-level copy of this routine in ref_subr.c.
 * Changes should be kept in sync.
 */
static int
make_pathname4(char *path, pathname4 *pathname)
{
	int ncomp;
	component4 *comp4;

	if (pathname == NULL)
		return (0);

	if (path == NULL) {
		pathname->pathname4_val = NULL;
		pathname->pathname4_len = 0;
		return (0);
	}

	/* count number of components to alloc buffer */
	if ((ncomp = nfs4_create_components(path, NULL)) == 0) {
		pathname->pathname4_val = NULL;
		pathname->pathname4_len = 0;
		return (0);
	}
	comp4 = kmem_zalloc(ncomp * sizeof (component4), KM_SLEEP);

	/* copy components into allocated buffer */
	ncomp = nfs4_create_components(path, comp4);

	pathname->pathname4_val = comp4;
	pathname->pathname4_len = ncomp;

	return (ncomp);
}

#define	xdr_fs_locations4 xdr_fattr4_fs_locations

fs_locations4 *
do_nfs_upcall(char *stype, char *sdata)
{
	int err;
	char *buf;
	size_t bufsize;
	XDR xdr;
	fs_locations4 *result;

	bufsize = 4 * MAXPATHLEN;
	buf = kmem_zalloc(bufsize, KM_SLEEP);
	err = reparse_kderef((const char *)stype, (const char *)sdata,
	    buf, &bufsize);

	DTRACE_PROBE4(nfs4serv__func__referral__upcall,
	    char *, stype, char *, sdata, char *, buf, int, err);
	if (err) {
		zcmn_err(getzoneid(), CE_NOTE,
		    "reparsed daemon not running: unable to get referral (%d)",
		    err);
		kmem_free(buf, 4 * MAXPATHLEN);
		return (NULL);
	}

	/*
	 * We get an XDR'ed record back from the kderef call
	 */
	xdrmem_create(&xdr, buf, bufsize, XDR_DECODE);
	result = kmem_alloc(sizeof (fs_locations4), KM_SLEEP);
	err = xdr_fs_locations4(&xdr, result);
	XDR_DESTROY(&xdr);
	kmem_free(buf, 4 * MAXPATHLEN);
	if (err != TRUE) {
		DTRACE_PROBE1(nfs4serv__func__referral__upcall__xdrfail,
		    int, err);
		kmem_free(result, sizeof (fs_locations4));
		return (NULL);
	}
	return (result);
}

fs_locations4 *
fetch_referral(rfs_inst_t *rip, vnode_t *vp, cred_t *cr)
{
	nvlist_t *nvl;
	char *stype, *sdata;
	fs_locations4 *result;
	int err;
	vnode_t *rvp;
	char *path;

	/*
	 * Check root vnode for migration
	 */
	if (vfs_is_migrated(vp->v_vfsp, cr) == B_TRUE &&
	    VFS_ROOT(vp->v_vfsp, &rvp) == 0) {
		vp = rvp;
	} else {
		VN_HOLD(vp);
	}

	/*
	 * Check attrs to ensure it's a reparse point
	 */
	if (vn_is_nfs_reparse(vp, cr) == B_FALSE) {
		VN_RELE(vp);
		return (NULL);
	}

	/*
	 * Look for an NFS record and get the type and data
	 */
	if (vn_find_nfs_record(vp, &nvl, &stype, &sdata) != 0) {
		VN_RELE(vp);
		return (NULL);
	}

	/*
	 * With the type and data, upcall to get the referral
	 */
	result = do_nfs_upcall(stype, sdata);
	reparse_free(nvl);

	if (result == NULL) {
		VN_RELE(vp);
		return (NULL);
	}

	/* get the zone relative path of the vnode */
	path = kmem_alloc(MAXPATHLEN, KM_SLEEP);
	err = vnodetopath(RFS_INST_ROOTVP(rip), vp, path, MAXPATHLEN,
	    cr);
	if (err) {
		result->fs_root.pathname4_val = NULL;
		result->fs_root.pathname4_len = 0;
	} else {
		/*
		 * Use path to generate fs_root, ignoring the leading '/'
		 */
		(void) make_pathname4(path, &result->fs_root);
	}
	VN_RELE(vp);
	kmem_free(path, MAXPATHLEN);

	return (result);
}

char *
build_symlink(rfs_inst_t *rip, vnode_t *vp, cred_t *cr, size_t *strsz)
{
	fs_locations4 *fsl;
	fs_location4 *fs;
	char *server, *path, *symbuf;
	static char *prefix = "/net/";
	int i, size, npaths;
	uint_t len;

	/* Get the referral */
	if ((fsl = fetch_referral(rip, vp, cr)) == NULL)
		return (NULL);

	/* Deal with only the first location and first server */
	fs = &fsl->locations_val[0];
	server = utf8_to_str(&fs->server_val[0], &len, NULL);
	if (server == NULL) {
		rfs4_free_fs_locations4(fsl);
		kmem_free(fsl, sizeof (fs_locations4));
		return (NULL);
	}

	/* Figure out size for "/net/" + host + /path/path/path + NULL */
	size = strlen(prefix) + len;
	for (i = 0; i < fs->rootpath.pathname4_len; i++)
		size += fs->rootpath.pathname4_val[i].utf8string_len + 1;

	/* Allocate the symlink buffer and fill it */
	symbuf = kmem_zalloc(size, KM_SLEEP);
	(void) strcat(symbuf, prefix);
	(void) strcat(symbuf, server);
	kmem_free(server, len);

	npaths = 0;
	for (i = 0; i < fs->rootpath.pathname4_len; i++) {
		path = utf8_to_str(&fs->rootpath.pathname4_val[i], &len, NULL);
		if (path == NULL)
			continue;
		(void) strcat(symbuf, "/");
		(void) strcat(symbuf, path);
		npaths++;
		kmem_free(path, len);
	}

	rfs4_free_fs_locations4(fsl);
	kmem_free(fsl, sizeof (fs_locations4));

	if (strsz != NULL)
		*strsz = size;
	return (symbuf);
}

/* ARGSUSED */
fs_locations4 *
fetch_replicas(rfs_inst_t *rip, vfs_t *vfsp, cred_t *cr)
{
	nvlist_t *nvl;
	nvpair_t *curr;
	char *stype, *sdata;
	fs_locations4 *result;

	if ((nvl = reparse_init()) == NULL)
		return (NULL);

	/*
	 * Look up replica information
	 */
	if (vfs_replica_parse(vfsp, nvl) != 0) {
		reparse_free(nvl);
		return (NULL);
	}

	/*
	 * Look for NFS data
	 */
	curr = NULL;
	while ((curr = nvlist_next_nvpair(nvl, curr)) != NULL) {
		if ((stype = nvpair_name(curr)) == NULL) {
			reparse_free(nvl);
			return (NULL);
		}
		if (strncasecmp(stype, "NFS", 3) == 0)
			break;
	}

	if ((curr == NULL) ||
	    (nvpair_value_string(curr, &sdata))) {
		reparse_free(nvl);
		return (NULL);
	}

	/*
	 * With the type and data, upcall to get the replicas
	 */
	result = do_nfs_upcall(stype, sdata);
	reparse_free(nvl);
	return (result);
}

/*
 * Check to see if we have a downrev Solaris client, so that we
 * can send it a symlink instead of a referral.
 */
int
client_is_downrev(struct svc_req *req)
{
	struct sockaddr *ca;
	rfs4_clntip_t *ci;
	bool_t create = FALSE;
	int is_downrev;
	rfs_inst_t *rip;

	rip = rfs_inst_svcreq_to_rip(req);
	ASSERT(rip->ri_v4.r4_enabled);

	ca = (struct sockaddr *)svc_getrpccaller(req->rq_xprt)->buf;
	ASSERT(ca);
	ci = rfs4_find_clntip(rip, ca, &create);
	if (ci == NULL)
		return (0);
	is_downrev = ci->ri_no_referrals;
	rfs4_dbe_rele(ci->ri_dbe);
	return (is_downrev);
}

static nfsstat4
fse_moved(uint_t op)
{
	nfsstat4	 rs = NFS4_OK;

	/*
	 * MOVED takes precedence over LEMO
	 */
	switch (op) {
	case OP_ACCESS:
	case OP_CLOSE:
	case OP_COMMIT:
	case OP_CREATE:
	case OP_DELEGRETURN:
	case OP_GETFH:
	case OP_LINK:
	case OP_LOCK:
	case OP_LOCKT:
	case OP_LOCKU:
	case OP_LOOKUP:
	case OP_LOOKUPP:
	case OP_NVERIFY:
	case OP_OPEN:
	case OP_OPENATTR:
	case OP_OPEN_CONFIRM:
	case OP_OPEN_DOWNGRADE:
	case OP_READ:
	case OP_READDIR:
	case OP_READLINK:
	case OP_REMOVE:
	case OP_RENAME:
	case OP_RESTOREFH:
	case OP_SAVEFH:
	case OP_SECINFO:
	case OP_SETATTR:
	case OP_VERIFY:
	case OP_WRITE:
		rs = NFS4ERR_MOVED;
		break;

	case OP_DELEGPURGE:
	case OP_GETATTR:
	case OP_RENEW:
	case OP_RELEASE_LOCKOWNER:
	default:
		/*
		 * The rest, drop thru to op
		 */
		break;
	}
	return (rs);
}

static nfsstat4
fse_frozen(uint_t op)
{
	nfsstat4	 rs = NFS4_OK;

	switch (op) {
	case OP_CLOSE:
	case OP_COMMIT:
	case OP_DELEGRETURN:
	case OP_LOCK:
	case OP_LOCKU:
	case OP_LOOKUP:
	case OP_OPEN:
	case OP_OPEN_CONFIRM:
	case OP_OPEN_DOWNGRADE:
	case OP_RELEASE_LOCKOWNER:
		/*
		 * OpenOwner/LockOwner bearing ops _or_
		 * server-state changing ops, get RESOURCE.
		 * NB - Lookup is tricky as it could cause
		 *	secondary effects.
		 */
		rs = NFS4ERR_RESOURCE;
		break;

	case OP_ACCESS:
	case OP_CREATE:
	case OP_GETATTR:
	case OP_LINK:
	case OP_LOCKT:
	case OP_LOOKUPP:
	case OP_NVERIFY:
	case OP_OPENATTR:
	case OP_READ:
	case OP_READDIR:
	case OP_READLINK:
	case OP_REMOVE:
	case OP_RENAME:
	case OP_SECINFO:
	case OP_SETATTR:
	case OP_VERIFY:
	case OP_WRITE:
		/*
		 * Ops that _can_ return DELAY as
		 * per rfc3530bis-10, do so now.
		 */
		rs = NFS4ERR_DELAY;
		break;

	case OP_DELEGPURGE:
	case OP_GETFH:
	case OP_PUTFH:
	case OP_PUTPUBFH:
	case OP_PUTROOTFH:
	case OP_RENEW:
	case OP_RESTOREFH:
	case OP_SAVEFH:
	case OP_SETCLIENTID:
	case OP_SETCLIENTID_CONFIRM:
	default:
		/*
		 * If the op can't return DELAY
		 * _or_ its deemed harmless, let
		 * it fall thru to actual op.
		 */
		break;
	}

	return (rs);
}

static nfsstat4
fse_op_check(struct compound_state *cs, uint_t op)
{
	fsh_entry_t	*fse;

	ASSERT(cs->fse != NULL);
	fse = cs->fse;

	if (fse->fse_state & FSE_FROZEN)
		return (fse_frozen(op));
	if (fse->fse_state & FSE_MOVED)
		return (fse_moved(op));

	return (NFS4_OK);
}
