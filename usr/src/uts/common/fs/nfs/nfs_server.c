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
 * Copyright (c) 1990, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 *	Copyright (c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *	All rights reserved.
 *	Use is subject to license terms.
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/cred.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/buf.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/pathname.h>
#include <sys/uio.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <sys/sysmacros.h>
#include <sys/siginfo.h>
#include <sys/tiuser.h>
#include <sys/statvfs.h>
#include <sys/stream.h>
#include <sys/strsubr.h>
#include <sys/stropts.h>
#include <sys/timod.h>
#include <sys/t_kuser.h>
#include <sys/kmem.h>
#include <sys/kstat.h>
#include <sys/dirent.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/unistd.h>
#include <sys/vtrace.h>
#include <sys/mode.h>
#include <sys/acl.h>
#include <sys/sdt.h>

#include <rpc/types.h>
#include <rpc/auth.h>
#include <rpc/auth_unix.h>
#include <rpc/auth_des.h>
#include <rpc/svc.h>
#include <rpc/xdr.h>
#include <rpc/rpc_rdma.h>

#include <nfs/nfs_srv_inst.h>
#include <nfs/nfs.h>
#include <nfs/export.h>
#include <nfs/nfssys.h>
#include <nfs/nfs_clnt.h>
#include <nfs/nfs_acl.h>
#include <nfs/nfs_log.h>
#include <nfs/nfs_cmd.h>
#include <nfs/lm.h>
#include <nfs/nfs4_drc.h>
#include <nfs/nfs4_kprot.h>
#include <nfs/nfs_dispatch.h>
#include <nfs/nfs4_drc.h>
#include <nfs/nfs_srv_inst_impl.h>

#include <sys/modctl.h>
#include <sys/cladm.h>
#include <sys/clconf.h>

#include <sys/tsol/label.h>

#define	MAXHOST 32
const char *kinet_ntop6(uchar_t *, char *, size_t);
void rfs_dispbuf_free(void *, int);

/*
 * Module linkage information.
 */

static struct modlmisc modlmisc = {
	&mod_miscops, "NFS server module"
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modlmisc, NULL
};

char _depends_on[] = "misc/klmmod";

kmem_cache_t *nfs_xuio_cache;
/* enable zero copy for NFS READs by default */
int nfs_loaned_buffers = 1;

kmem_cache_t *nfs_disp_cache;

int
_init(void)
{
	int status;

	nfs_srvinit();

	if ((status = mod_install((struct modlinkage *)&modlinkage)) != 0) {
		nfs_srvfini();
		cmn_err(CE_WARN, "_init: mod_install failed");
		return (status);
	}

	/*
	 * Initialise some placeholders for nfssys() calls. These have
	 * to be declared by the nfs module, since that handles nfssys()
	 * calls - also used by NFS clients - but are provided by this
	 * nfssrv module. These also then serve as confirmation to the
	 * relevant code in nfs that nfssrv has been loaded, as they're
	 * initially NULL.
	 */
	nfs_srv_quiesce_func = rfs_inst_quiesce;
	nfs_srv_dss_func = rfs4_dss_setpaths;
	nfs_get_fsh_status_func = rfs4_get_fsh_status;

	/* initialize the copy reduction caches */
	nfs_xuio_cache = kmem_cache_create("nfs_xuio_cache",
	    sizeof (nfs_xuio_t), 0, NULL, NULL, NULL, NULL, NULL, 0);

	nfs_disp_cache = kmem_cache_create("nfs_dispatch_cache",
	    sizeof (rfs_disp_t), 0, NULL, NULL, NULL, NULL, NULL, 0);

	return (0);
}

int
_fini()
{
	return (EBUSY);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

static void	nullfree(void);
static void	rfs_dispatch(struct svc_req *, SVCXPRT *);
static void	acl_dispatch(struct svc_req *, SVCXPRT *);
static void	common_dispatch(struct svc_req *, SVCXPRT *,
		rpcvers_t, rpcvers_t, char *,
		struct rpc_disptable *);
static	int	checkauth(struct exportinfo *, struct svc_req *, cred_t *, int,
			bool_t);
static char	*client_name(struct svc_req *req);
static char	*client_addr(struct svc_req *req, char *buf);
extern	int	sec_svc_getcred(struct svc_req *, cred_t *cr, char **, int *);
extern	bool_t	sec_svc_inrootlist(int, caddr_t, int, caddr_t *);

#define	NFSLOG_COPY_NETBUF(exi, xprt, nb)	{		\
	(nb)->maxlen = (xprt)->xp_rtaddr.maxlen;		\
	(nb)->len = (xprt)->xp_rtaddr.len;			\
	(nb)->buf = kmem_alloc((nb)->len, KM_SLEEP);		\
	bcopy((xprt)->xp_rtaddr.buf, (nb)->buf, (nb)->len);	\
	}

/*
 * Public Filehandle common nfs routines
 */
static int	MCLpath(char **);
static void	URLparse(char *);

int rfs4_dispatch(struct rpcdisp *, struct svc_req *,
    SVCXPRT *, rfs_disp_t *, int *);
bool_t rfs4_minorvers_mismatch(struct svc_req *, SVCXPRT *, void *);
extern void rfs4_dispatch_free(rfs_disp_t *);

static SVC_CALLOUT_TABLE *
rfs_callout_table_alloc(void)
{
	SVC_CALLOUT_TABLE *sct;
	SVC_CALLOUT *sc;

	sct = kmem_zalloc(sizeof (SVC_CALLOUT_TABLE), KM_SLEEP);
	sct->sct_size = 2;
	sct->sct_free = TRUE;
	sc = kmem_zalloc(sct->sct_size * sizeof (SVC_CALLOUT), KM_SLEEP);
	sct->sct_sc = sc;
	sc[0].sc_prog = NFS_PROGRAM;
	sc[0].sc_versmin = NFS_VERSMIN;
	sc[0].sc_versmax = NFS_VERSMAX;
	sc[0].sc_dispatch = rfs_dispatch;
	sc[1].sc_prog = NFS_ACL_PROGRAM;
	sc[1].sc_versmin = NFS_ACL_VERSMIN;
	sc[1].sc_versmax = NFS_ACL_VERSMAX;
	sc[1].sc_dispatch = acl_dispatch;

	return (sct);
}

static void
rfs_callout_table_free(SVC_CALLOUT_TABLE *sct)
{
	ASSERT(sct->sct_free);
	kmem_free(sct->sct_sc, sct->sct_size * sizeof (SVC_CALLOUT));
	kmem_free(sct, sizeof (SVC_CALLOUT_TABLE));
}

static int
nfs_srv_set_sc_versions(struct file *fp, SVC_CALLOUT_TABLE **sctpp,
    rpcvers_t versmin, rpcvers_t versmax)
{
	struct strioctl strioc;
	struct T_info_ack tinfo;
	int error, retval;
	SVC_CALLOUT_TABLE *sctp;

	/*
	 * Find out what type of transport this is.
	 */
	strioc.ic_cmd = TI_GETINFO;
	strioc.ic_timout = -1;
	strioc.ic_len = sizeof (tinfo);
	strioc.ic_dp = (char *)&tinfo;
	tinfo.PRIM_type = T_INFO_REQ;

	error = strioctl(fp->f_vnode, I_STR, (intptr_t)&strioc, 0, K_TO_K,
	    CRED(), &retval);
	if (error || retval)
		return (error);

	/*
	 * Based on our query of the transport type...
	 *
	 * Reset the min/max versions based on the caller's request
	 * NOTE: This assumes that NFS_PROGRAM is first in the array!!
	 * And the second entry is the NFS_ACL_PROGRAM.
	 */
	switch (tinfo.SERV_type) {
	case T_CLTS:
		if (versmax == NFS_V4)
			return (EINVAL);
		sctp = rfs_callout_table_alloc();
		sctp->sct_sc[0].sc_versmin = versmin;
		sctp->sct_sc[0].sc_versmax = versmax;
		sctp->sct_sc[1].sc_versmin = versmin;
		sctp->sct_sc[1].sc_versmax = versmax;
		*sctpp = sctp;
		break;
	case T_COTS:
	case T_COTS_ORD:
		sctp = rfs_callout_table_alloc();
		sctp->sct_sc[0].sc_versmin = versmin;
		sctp->sct_sc[0].sc_versmax = versmax;
		/* For the NFS_ACL program, check the max version */
		if (versmax > NFS_ACL_VERSMAX)
			versmax = NFS_ACL_VERSMAX;
		sctp->sct_sc[1].sc_versmin = versmin;
		sctp->sct_sc[1].sc_versmax = versmax;
		*sctpp = sctp;
		break;
	default:
		error = EINVAL;
	}

	return (error);
}

/*
 * NFS Server system call.
 * Does all of the work of running a NFS server.
 * uap->fd is the fd of an open transport provider
 */
int
nfs_svc(struct nfs_svc_args *arg, model_t model)
{
	file_t *fp;
	SVCMASTERXPRT *xprt;
	int error;
	int readsize;
	char buf[KNC_STRSIZE];
	size_t len;
	STRUCT_HANDLE(nfs_svc_args, uap);
	struct netbuf addrmask;
	SVC_CALLOUT_TABLE *sctp = NULL;
	rpcvers_t nfs_versmin, nfs_versmax;
	rfs_inst_t *rip;

#ifdef lint
	model = model;		/* STRUCT macros don't always refer to it */
#endif

	STRUCT_SET_HANDLE(uap, model, arg);

	/* Check privileges in nfssys() */

	if ((fp = getf(STRUCT_FGET(uap, fd))) == NULL)
		return (EBADF);

	/*
	 * Set read buffer size to rsize
	 * and add room for RPC headers.
	 */
	readsize = nfs3tsize() + (RPC_MAXDATASIZE - NFS_MAXDATA);
	if (readsize < RPC_MAXDATASIZE)
		readsize = RPC_MAXDATASIZE;

	error = copyinstr((const char *)STRUCT_FGETP(uap, netid), buf,
	    KNC_STRSIZE, &len);
	if (error) {
		releasef(STRUCT_FGET(uap, fd));
		return (error);
	}

	addrmask.len = STRUCT_FGET(uap, addrmask.len);
	addrmask.maxlen = STRUCT_FGET(uap, addrmask.maxlen);
	addrmask.buf = kmem_alloc(addrmask.maxlen, KM_SLEEP);
	error = copyin(STRUCT_FGETP(uap, addrmask.buf), addrmask.buf,
	    addrmask.len);
	if (error) {
		releasef(STRUCT_FGET(uap, fd));
		kmem_free(addrmask.buf, addrmask.maxlen);
		return (error);
	}

	nfs_versmin = STRUCT_FGET(uap, versmin);
	nfs_versmax = STRUCT_FGET(uap, versmax);

	/* Double check the vers min/max ranges */
	if ((nfs_versmin > nfs_versmax) ||
	    (nfs_versmin < NFS_VERSMIN) ||
	    (nfs_versmax > NFS_VERSMAX)) {
		nfs_versmin = NFS_VERSMIN_DEFAULT;
		nfs_versmax = NFS_VERSMAX_DEFAULT;
	}

	/* Create / initialize nfs server instance */
	error = rfs_inst_start(nfs_versmax, STRUCT_FGET(uap, delegation));
	if (error) {
		releasef(STRUCT_FGET(uap, fd));
		kmem_free(addrmask.buf, addrmask.maxlen);
		return (error);
	}

	error = nfs_srv_set_sc_versions(fp, &sctp, nfs_versmin, nfs_versmax);
	if (error) {
		releasef(STRUCT_FGET(uap, fd));
		kmem_free(addrmask.buf, addrmask.maxlen);
		return (error);
	}

	/* Create a transport handle. */
	error = svc_tli_kcreate(fp, readsize, buf, &addrmask, &xprt,
	    sctp, NULL, NFS_SVCPOOL_ID, TRUE);
	if (error) {
		rfs_callout_table_free(sctp);
		kmem_free(addrmask.buf, addrmask.maxlen);
	}

	releasef(STRUCT_FGET(uap, fd));

	/*
	 * Iff we are booted with SUN Cluster,
	 * save the cluster nodeid
	 */
	if (cluster_bootflags & CLUSTER_BOOTED) {
		rip = rfs_inst_find(FALSE);
		if (rip == NULL)
			return (ENOENT);

		lm_global_nlmid = clconf_get_nodeid();
		rip->ri_hanfs_id = clconf_get_nodeid();
		ASSERT(rip->ri_hanfs_id != NODEID_UNKNOWN);
		ASSERT(rip->ri_hanfs_id <= NFS_MAX_NODEID);
		rfs_inst_active_rele(rip);
	}

	return (error);
}

/*
 * If RDMA device available,
 * start RDMA listener.
 */
int
rdma_start(struct rdma_svc_args *rsa)
{
	int error;
	rdma_xprt_group_t started_rdma_xprts;
	rdma_stat stat;
	int svc_state = 0;
	SVC_CALLOUT_TABLE *sctp;

	/* Double check the vers min/max ranges */
	if ((rsa->nfs_versmin > rsa->nfs_versmax) ||
	    (rsa->nfs_versmin < NFS_VERSMIN) ||
	    (rsa->nfs_versmax > NFS_VERSMAX)) {
		rsa->nfs_versmin = NFS_VERSMIN_DEFAULT;
		rsa->nfs_versmax = NFS_VERSMAX_DEFAULT;
	}

	/* Create / initialize nfs server instance */
	error = rfs_inst_start(rsa->nfs_versmax, rsa->delegation);
	if (error)
		return (error);

	started_rdma_xprts.rtg_count = 0;
	started_rdma_xprts.rtg_listhead = NULL;
	started_rdma_xprts.rtg_poolid = rsa->poolid;

restart:
	/* Set the versions in the callout table */
	sctp = rfs_callout_table_alloc();
	sctp->sct_sc[0].sc_versmin = rsa->nfs_versmin;
	sctp->sct_sc[0].sc_versmax = rsa->nfs_versmax;
	/* For the NFS_ACL program, check the max version */
	sctp->sct_sc[1].sc_versmin = rsa->nfs_versmin;
	if (rsa->nfs_versmax > NFS_ACL_VERSMAX)
		sctp->sct_sc[1].sc_versmax = NFS_ACL_VERSMAX;
	else
		sctp->sct_sc[1].sc_versmax = rsa->nfs_versmax;

	error = svc_rdma_kcreate(rsa->netid, sctp, rsa->poolid,
	    &started_rdma_xprts);
	if (error)
		rfs_callout_table_free(sctp);

	svc_state = !error;

	while (!error) {

		/*
		 * wait till either interrupted by a signal on
		 * nfs service stop/restart or signalled by a
		 * rdma plugin attach/detatch.
		 */

		stat = rdma_kwait();

		/*
		 * stop services if running -- either on a HCA detach event
		 * or if the nfs service is stopped/restarted.
		 */

		if ((stat == RDMA_HCA_DETACH || stat == RDMA_INTR) &&
		    svc_state) {
			rdma_stop(&started_rdma_xprts);
			svc_state = 0;
		}

		/*
		 * nfs service stop/restart, break out of the
		 * wait loop and return;
		 */
		if (stat == RDMA_INTR)
			return (0);

		/*
		 * restart stopped services on a HCA attach event
		 * (if not already running)
		 */

		if ((stat == RDMA_HCA_ATTACH) && (svc_state == 0))
			goto restart;

		/*
		 * loop until a nfs service stop/restart
		 */
	}

	return (error);
}

/* ARGSUSED */
void
rpc_null(caddr_t *argp, caddr_t *resp)
{
}

/* ARGSUSED */
void
rpc_null_v3(caddr_t *argp, caddr_t *resp, struct exportinfo *exi,
    struct svc_req *req, cred_t *cr)
{
	DTRACE_NFSV3_3(op__null__start, struct svc_req *, req,
	    cred_t *, cr, vnode_t *, NULL);
	DTRACE_NFSV3_3(op__null__done, struct svc_req *, req,
	    cred_t *, cr, vnode_t *, NULL);
}

/* ARGSUSED */
static void
rfs_error(caddr_t *argp, caddr_t *resp)
{
	/* return (EOPNOTSUPP); */
}

static void
nullfree(void)
{
}

static char *rfscallnames_v2[] = {
	"RFS2_NULL",
	"RFS2_GETATTR",
	"RFS2_SETATTR",
	"RFS2_ROOT",
	"RFS2_LOOKUP",
	"RFS2_READLINK",
	"RFS2_READ",
	"RFS2_WRITECACHE",
	"RFS2_WRITE",
	"RFS2_CREATE",
	"RFS2_REMOVE",
	"RFS2_RENAME",
	"RFS2_LINK",
	"RFS2_SYMLINK",
	"RFS2_MKDIR",
	"RFS2_RMDIR",
	"RFS2_READDIR",
	"RFS2_STATFS"
};

static struct rpcdisp rfsdisptab_v2[] = {
	/*
	 * NFS VERSION 2
	 */

	/* RFS_NULL = 0 */
	{rpc_null,
	    xdr_void, NULL_xdrproc_t, 0,
	    xdr_void, NULL_xdrproc_t, 0,
	    nullfree, RPC_IDEMPOTENT,
	    0},

	/* RFS_GETATTR = 1 */
	{rfs_getattr,
	    xdr_fhandle, xdr_fastfhandle, sizeof (fhandle_t),
	    xdr_attrstat, xdr_fastattrstat, sizeof (struct nfsattrstat),
	    nullfree, RPC_IDEMPOTENT|RPC_ALLOWANON|RPC_MAPRESP,
	    rfs_getattr_getfh},

	/* RFS_SETATTR = 2 */
	{rfs_setattr,
	    xdr_saargs, NULL_xdrproc_t, sizeof (struct nfssaargs),
	    xdr_attrstat, xdr_fastattrstat, sizeof (struct nfsattrstat),
	    nullfree, RPC_MAPRESP,
	    rfs_setattr_getfh},

	/* RFS_ROOT = 3 *** NO LONGER SUPPORTED *** */
	{rfs_error,
	    xdr_void, NULL_xdrproc_t, 0,
	    xdr_void, NULL_xdrproc_t, 0,
	    nullfree, RPC_IDEMPOTENT,
	    0},

	/* RFS_LOOKUP = 4 */
	{rfs_lookup,
	    xdr_diropargs, NULL_xdrproc_t, sizeof (struct nfsdiropargs),
	    xdr_diropres, xdr_fastdiropres, sizeof (struct nfsdiropres),
	    nullfree, RPC_IDEMPOTENT|RPC_MAPRESP|RPC_PUBLICFH_OK,
	    rfs_lookup_getfh},

	/* RFS_READLINK = 5 */
	{rfs_readlink,
	    xdr_fhandle, xdr_fastfhandle, sizeof (fhandle_t),
	    xdr_rdlnres, NULL_xdrproc_t, sizeof (struct nfsrdlnres),
	    rfs_rlfree, RPC_IDEMPOTENT,
	    rfs_readlink_getfh},

	/* RFS_READ = 6 */
	{rfs_read,
	    xdr_readargs, NULL_xdrproc_t, sizeof (struct nfsreadargs),
	    xdr_rdresult, NULL_xdrproc_t, sizeof (struct nfsrdresult),
	    rfs_rdfree, RPC_IDEMPOTENT,
	    rfs_read_getfh},

	/* RFS_WRITECACHE = 7 *** NO LONGER SUPPORTED *** */
	{rfs_error,
	    xdr_void, NULL_xdrproc_t, 0,
	    xdr_void, NULL_xdrproc_t, 0,
	    nullfree, RPC_IDEMPOTENT,
	    0},

	/* RFS_WRITE = 8 */
	{rfs_write,
	    xdr_writeargs, NULL_xdrproc_t, sizeof (struct nfswriteargs),
	    xdr_attrstat, xdr_fastattrstat, sizeof (struct nfsattrstat),
	    nullfree, RPC_MAPRESP,
	    rfs_write_getfh},

	/* RFS_CREATE = 9 */
	{rfs_create,
	    xdr_creatargs, NULL_xdrproc_t, sizeof (struct nfscreatargs),
	    xdr_diropres, xdr_fastdiropres, sizeof (struct nfsdiropres),
	    nullfree, RPC_MAPRESP,
	    rfs_create_getfh},

	/* RFS_REMOVE = 10 */
	{rfs_remove,
	    xdr_diropargs, NULL_xdrproc_t, sizeof (struct nfsdiropargs),
#ifdef _LITTLE_ENDIAN
	    xdr_enum, xdr_fastenum, sizeof (enum nfsstat),
#else
	    xdr_enum, NULL_xdrproc_t, sizeof (enum nfsstat),
#endif
	    nullfree, RPC_MAPRESP,
	    rfs_remove_getfh},

	/* RFS_RENAME = 11 */
	{rfs_rename,
	    xdr_rnmargs, NULL_xdrproc_t, sizeof (struct nfsrnmargs),
#ifdef _LITTLE_ENDIAN
	    xdr_enum, xdr_fastenum, sizeof (enum nfsstat),
#else
	    xdr_enum, NULL_xdrproc_t, sizeof (enum nfsstat),
#endif
	    nullfree, RPC_MAPRESP,
	    rfs_rename_getfh},

	/* RFS_LINK = 12 */
	{rfs_link,
	    xdr_linkargs, NULL_xdrproc_t, sizeof (struct nfslinkargs),
#ifdef _LITTLE_ENDIAN
	    xdr_enum, xdr_fastenum, sizeof (enum nfsstat),
#else
	    xdr_enum, NULL_xdrproc_t, sizeof (enum nfsstat),
#endif
	    nullfree, RPC_MAPRESP,
	    rfs_link_getfh},

	/* RFS_SYMLINK = 13 */
	{rfs_symlink,
	    xdr_slargs, NULL_xdrproc_t, sizeof (struct nfsslargs),
#ifdef _LITTLE_ENDIAN
	    xdr_enum, xdr_fastenum, sizeof (enum nfsstat),
#else
	    xdr_enum, NULL_xdrproc_t, sizeof (enum nfsstat),
#endif
	    nullfree, RPC_MAPRESP,
	    rfs_symlink_getfh},

	/* RFS_MKDIR = 14 */
	{rfs_mkdir,
	    xdr_creatargs, NULL_xdrproc_t, sizeof (struct nfscreatargs),
	    xdr_diropres, xdr_fastdiropres, sizeof (struct nfsdiropres),
	    nullfree, RPC_MAPRESP,
	    rfs_mkdir_getfh},

	/* RFS_RMDIR = 15 */
	{rfs_rmdir,
	    xdr_diropargs, NULL_xdrproc_t, sizeof (struct nfsdiropargs),
#ifdef _LITTLE_ENDIAN
	    xdr_enum, xdr_fastenum, sizeof (enum nfsstat),
#else
	    xdr_enum, NULL_xdrproc_t, sizeof (enum nfsstat),
#endif
	    nullfree, RPC_MAPRESP,
	    rfs_rmdir_getfh},

	/* RFS_READDIR = 16 */
	{rfs_readdir,
	    xdr_rddirargs, NULL_xdrproc_t, sizeof (struct nfsrddirargs),
	    xdr_putrddirres, NULL_xdrproc_t, sizeof (struct nfsrddirres),
	    rfs_rddirfree, RPC_IDEMPOTENT,
	    rfs_readdir_getfh},

	/* RFS_STATFS = 17 */
	{rfs_statfs,
	    xdr_fhandle, xdr_fastfhandle, sizeof (fhandle_t),
	    xdr_statfs, xdr_faststatfs, sizeof (struct nfsstatfs),
	    nullfree, RPC_IDEMPOTENT|RPC_ALLOWANON|RPC_MAPRESP,
	    rfs_statfs_getfh},
};

static char *rfscallnames_v3[] = {
	"RFS3_NULL",
	"RFS3_GETATTR",
	"RFS3_SETATTR",
	"RFS3_LOOKUP",
	"RFS3_ACCESS",
	"RFS3_READLINK",
	"RFS3_READ",
	"RFS3_WRITE",
	"RFS3_CREATE",
	"RFS3_MKDIR",
	"RFS3_SYMLINK",
	"RFS3_MKNOD",
	"RFS3_REMOVE",
	"RFS3_RMDIR",
	"RFS3_RENAME",
	"RFS3_LINK",
	"RFS3_READDIR",
	"RFS3_READDIRPLUS",
	"RFS3_FSSTAT",
	"RFS3_FSINFO",
	"RFS3_PATHCONF",
	"RFS3_COMMIT"
};

static struct rpcdisp rfsdisptab_v3[] = {
	/*
	 * NFS VERSION 3
	 */

	/* RFS_NULL = 0 */
	{rpc_null_v3,
	    xdr_void, NULL_xdrproc_t, 0,
	    xdr_void, NULL_xdrproc_t, 0,
	    nullfree, RPC_IDEMPOTENT,
	    0},

	/* RFS3_GETATTR = 1 */
	{rfs3_getattr,
	    xdr_nfs_fh3_server, NULL_xdrproc_t, sizeof (GETATTR3args),
	    xdr_GETATTR3res, NULL_xdrproc_t, sizeof (GETATTR3res),
	    nullfree, (RPC_IDEMPOTENT | RPC_ALLOWANON),
	    rfs3_getattr_getfh},

	/* RFS3_SETATTR = 2 */
	{rfs3_setattr,
	    xdr_SETATTR3args, NULL_xdrproc_t, sizeof (SETATTR3args),
	    xdr_SETATTR3res, NULL_xdrproc_t, sizeof (SETATTR3res),
	    nullfree, 0,
	    rfs3_setattr_getfh},

	/* RFS3_LOOKUP = 3 */
	{rfs3_lookup,
	    xdr_diropargs3, NULL_xdrproc_t, sizeof (LOOKUP3args),
	    xdr_LOOKUP3res, NULL_xdrproc_t, sizeof (LOOKUP3res),
	    nullfree, (RPC_IDEMPOTENT | RPC_PUBLICFH_OK),
	    rfs3_lookup_getfh},

	/* RFS3_ACCESS = 4 */
	{rfs3_access,
	    xdr_ACCESS3args, NULL_xdrproc_t, sizeof (ACCESS3args),
	    xdr_ACCESS3res, NULL_xdrproc_t, sizeof (ACCESS3res),
	    nullfree, RPC_IDEMPOTENT,
	    rfs3_access_getfh},

	/* RFS3_READLINK = 5 */
	{rfs3_readlink,
	    xdr_nfs_fh3_server, NULL_xdrproc_t, sizeof (READLINK3args),
	    xdr_READLINK3res, NULL_xdrproc_t, sizeof (READLINK3res),
	    rfs3_readlink_free, RPC_IDEMPOTENT,
	    rfs3_readlink_getfh},

	/* RFS3_READ = 6 */
	{rfs3_read,
	    xdr_READ3args, NULL_xdrproc_t, sizeof (READ3args),
	    xdr_READ3res, NULL_xdrproc_t, sizeof (READ3res),
	    rfs3_read_free, RPC_IDEMPOTENT,
	    rfs3_read_getfh},

	/* RFS3_WRITE = 7 */
	{rfs3_write,
	    xdr_WRITE3args, NULL_xdrproc_t, sizeof (WRITE3args),
	    xdr_WRITE3res, NULL_xdrproc_t, sizeof (WRITE3res),
	    nullfree, 0,
	    rfs3_write_getfh},

	/* RFS3_CREATE = 8 */
	{rfs3_create,
	    xdr_CREATE3args, NULL_xdrproc_t, sizeof (CREATE3args),
	    xdr_CREATE3res, NULL_xdrproc_t, sizeof (CREATE3res),
	    nullfree, 0,
	    rfs3_create_getfh},

	/* RFS3_MKDIR = 9 */
	{rfs3_mkdir,
	    xdr_MKDIR3args, NULL_xdrproc_t, sizeof (MKDIR3args),
	    xdr_MKDIR3res, NULL_xdrproc_t, sizeof (MKDIR3res),
	    nullfree, 0,
	    rfs3_mkdir_getfh},

	/* RFS3_SYMLINK = 10 */
	{rfs3_symlink,
	    xdr_SYMLINK3args, NULL_xdrproc_t, sizeof (SYMLINK3args),
	    xdr_SYMLINK3res, NULL_xdrproc_t, sizeof (SYMLINK3res),
	    nullfree, 0,
	    rfs3_symlink_getfh},

	/* RFS3_MKNOD = 11 */
	{rfs3_mknod,
	    xdr_MKNOD3args, NULL_xdrproc_t, sizeof (MKNOD3args),
	    xdr_MKNOD3res, NULL_xdrproc_t, sizeof (MKNOD3res),
	    nullfree, 0,
	    rfs3_mknod_getfh},

	/* RFS3_REMOVE = 12 */
	{rfs3_remove,
	    xdr_diropargs3, NULL_xdrproc_t, sizeof (REMOVE3args),
	    xdr_REMOVE3res, NULL_xdrproc_t, sizeof (REMOVE3res),
	    nullfree, 0,
	    rfs3_remove_getfh},

	/* RFS3_RMDIR = 13 */
	{rfs3_rmdir,
	    xdr_diropargs3, NULL_xdrproc_t, sizeof (RMDIR3args),
	    xdr_RMDIR3res, NULL_xdrproc_t, sizeof (RMDIR3res),
	    nullfree, 0,
	    rfs3_rmdir_getfh},

	/* RFS3_RENAME = 14 */
	{rfs3_rename,
	    xdr_RENAME3args, NULL_xdrproc_t, sizeof (RENAME3args),
	    xdr_RENAME3res, NULL_xdrproc_t, sizeof (RENAME3res),
	    nullfree, 0,
	    rfs3_rename_getfh},

	/* RFS3_LINK = 15 */
	{rfs3_link,
	    xdr_LINK3args, NULL_xdrproc_t, sizeof (LINK3args),
	    xdr_LINK3res, NULL_xdrproc_t, sizeof (LINK3res),
	    nullfree, 0,
	    rfs3_link_getfh},

	/* RFS3_READDIR = 16 */
	{rfs3_readdir,
	    xdr_READDIR3args, NULL_xdrproc_t, sizeof (READDIR3args),
	    xdr_READDIR3res, NULL_xdrproc_t, sizeof (READDIR3res),
	    rfs3_readdir_free, RPC_IDEMPOTENT,
	    rfs3_readdir_getfh},

	/* RFS3_READDIRPLUS = 17 */
	{rfs3_readdirplus,
	    xdr_READDIRPLUS3args, NULL_xdrproc_t, sizeof (READDIRPLUS3args),
	    xdr_READDIRPLUS3res, NULL_xdrproc_t, sizeof (READDIRPLUS3res),
	    rfs3_readdirplus_free, RPC_AVOIDWORK,
	    rfs3_readdirplus_getfh},

	/* RFS3_FSSTAT = 18 */
	{rfs3_fsstat,
	    xdr_nfs_fh3_server, NULL_xdrproc_t, sizeof (FSSTAT3args),
	    xdr_FSSTAT3res, NULL_xdrproc_t, sizeof (FSSTAT3res),
	    nullfree, RPC_IDEMPOTENT,
	    rfs3_fsstat_getfh},

	/* RFS3_FSINFO = 19 */
	{rfs3_fsinfo,
	    xdr_nfs_fh3_server, NULL_xdrproc_t, sizeof (FSINFO3args),
	    xdr_FSINFO3res, NULL_xdrproc_t, sizeof (FSINFO3res),
	    nullfree, RPC_IDEMPOTENT|RPC_ALLOWANON,
	    rfs3_fsinfo_getfh},

	/* RFS3_PATHCONF = 20 */
	{rfs3_pathconf,
	    xdr_nfs_fh3_server, NULL_xdrproc_t, sizeof (PATHCONF3args),
	    xdr_PATHCONF3res, NULL_xdrproc_t, sizeof (PATHCONF3res),
	    nullfree, RPC_IDEMPOTENT,
	    rfs3_pathconf_getfh},

	/* RFS3_COMMIT = 21 */
	{rfs3_commit,
	    xdr_COMMIT3args, NULL_xdrproc_t, sizeof (COMMIT3args),
	    xdr_COMMIT3res, NULL_xdrproc_t, sizeof (COMMIT3res),
	    nullfree, RPC_IDEMPOTENT,
	    rfs3_commit_getfh},
};

static char *rfscallnames_v4[] = {
	"RFS4_NULL",
	"RFS4_COMPOUND"
};

static struct rpcdisp rfsdisptab_v4[] = {
	/*
	 * NFS VERSION 4
	 */

	/* RFS_NULL = 0 */
	{rpc_null,
	    xdr_void, NULL_xdrproc_t, 0,
	    xdr_void, NULL_xdrproc_t, 0,
	    nullfree, RPC_IDEMPOTENT, 0},

	/* RFS4_compound = 1 */
	{rfs4_compound,
	    xdr_COMPOUND4args_srv, NULL_xdrproc_t, sizeof (COMPOUND4args),
	    xdr_COMPOUND4res_srv, NULL_xdrproc_t, sizeof (COMPOUND4res),
	    rfs4_compound_free, 0, 0},
};


static struct rpc_disptable rfs_disptable[] = {
	{sizeof (rfsdisptab_v2) / sizeof (rfsdisptab_v2[0]),
	    rfscallnames_v2, rfsdisptab_v2},
	{sizeof (rfsdisptab_v3) / sizeof (rfsdisptab_v3[0]),
	    rfscallnames_v3, rfsdisptab_v3},
	{sizeof (rfsdisptab_v4) / sizeof (rfsdisptab_v4[0]),
	    rfscallnames_v4, rfsdisptab_v4},
};

/*
 * If nfs_portmon is set, then clients are required to use privileged
 * ports (ports < IPPORT_RESERVED) in order to get NFS services.
 *
 * N.B.: this attempt to carry forward the already ill-conceived notion
 * of privileged ports for TCP/UDP is really quite ineffectual.  Not only
 * is it transport-dependent, it's laughably easy to spoof.  If you're
 * really interested in security, you must start with secure RPC instead.
 */
static int nfs_portmon = 0;

#ifdef DEBUG
static int cred_hits = 0;
static int cred_misses = 0;
#endif


#ifdef DEBUG
/*
 * Debug code to allow disabling of rfs_dispatch() use of
 * fastxdrargs() and fastxdrres() calls for testing purposes.
 */
static int rfs_no_fast_xdrargs = 0;
static int rfs_no_fast_xdrres = 0;
#endif


static bool_t
auth_tooweak(struct svc_req *req, char *res)
{
	if (req->rq_vers == NFS_VERSION && req->rq_proc == RFS_LOOKUP) {
		struct nfsdiropres *dr = (struct nfsdiropres *)res;
		if (dr->dr_status == WNFSERR_CLNT_FLAVOR)
			return (TRUE);
	} else if (req->rq_vers == NFS_V3 && req->rq_proc == NFSPROC3_LOOKUP) {
		LOOKUP3res *resp = (LOOKUP3res *)res;
		if (resp->status == WNFSERR_CLNT_FLAVOR)
			return (TRUE);
	}
	return (FALSE);
}

static void
migration_error(SVCXPRT *xprt, char *pgmname, struct rpcdisp *disp, int status)
{
	union {
		union rfs_res rr;
		union acl_res ar;
	} res_buf;

	GETATTR3res *resp;

	bzero(&res_buf, sizeof (res_buf));

	/*
	 * Since all ops have the status as the first field,
	 * we can cast to any of them to set the it.
	 */
	resp = (GETATTR3res *)&res_buf;

	resp->status = status;

	if (!svc_sendreply(xprt, disp->dis_xdrres, (char *)resp))
		cmn_err(CE_NOTE, "%s: bad sendreply", pgmname);

	if (disp->dis_resfree != nullfree)
		(*disp->dis_resfree)((char *)resp);
}

static void
common_dispatch(struct svc_req *req, SVCXPRT *xprt, rpcvers_t min_vers,
		rpcvers_t max_vers, char *pgmname,
		struct rpc_disptable *disptable)
{
	int which;
	rpcvers_t vers;
	char *args;
	char *res;
	struct rpcdisp *disp = NULL;
	rfs_disp_t *dispbuf = NULL;
	int dis_flags = 0;
	cred_t *cr;
	int error = 0;
	int anon_ok;
	struct exportinfo *exi = NULL;
	unsigned int nfslog_rec_id;
	int dupstat;
	struct dupreq *dr;
	int authres;
	enum_t auth_flavor;
	struct netbuf	nb;
	bool_t logging_enabled = FALSE;
	bool_t release_freeze_lock = FALSE;
	struct exportinfo *nfslog_exi = NULL;
	char **procnames;
	char cbuf[INET6_ADDRSTRLEN];	/* allows either IPv4 or IPv6 addr */
	rfs_inst_t *rip;
	int async_reply = 0;
	fsh_entry_t *fse = NULL;

	rip = rfs_inst_svcreq_to_rip(req);
	vers = req->rq_vers;
	auth_flavor = req->rq_cred.oa_flavor;

	if (vers < min_vers || vers > max_vers) {
		svcerr_progvers(req->rq_xprt, min_vers, max_vers);
		error++;
		zcmn_err(getzoneid(), CE_NOTE,
		    "%s: bad version number %u", pgmname, vers);
		goto done;
	}
	vers -= min_vers;

	which = req->rq_proc;
	if (which < 0 || which >= disptable[(int)vers].dis_nprocs) {
		svcerr_noproc(req->rq_xprt);
		error++;
		goto done;
	}

	nfsstat_inc_proc_cnt(rip, (int)req->rq_vers, which);

	dispbuf = kmem_cache_alloc(nfs_disp_cache, KM_SLEEP);
	bzero(dispbuf, sizeof (rfs_disp_t));

	dispbuf->req = *req;

	disp = &disptable[(int)vers].dis_table[which];
	dispbuf->disp = disp;
	procnames = disptable[(int)vers].dis_procnames;

	/*
	 * Deserialize into the args struct.
	 */
	args = (char *)&(dispbuf->args_buf);

#ifdef DEBUG
	if (rfs_no_fast_xdrargs || (auth_flavor == RPCSEC_GSS) ||
	    disp->dis_fastxdrargs == NULL_xdrproc_t ||
	    !SVC_GETARGS(xprt, disp->dis_fastxdrargs, (char *)&args))
#else
	if ((auth_flavor == RPCSEC_GSS) ||
	    disp->dis_fastxdrargs == NULL_xdrproc_t ||
	    !SVC_GETARGS(xprt, disp->dis_fastxdrargs, (char *)&args))
#endif
	{
		if (!SVC_GETARGS(xprt, disp->dis_xdrargs, args)) {
			error++;
			/*
			 * Check if we are outside our capabilities.
			 */
			if (rfs4_minorvers_mismatch(req, xprt, (void *)args))
				goto done;

			svcerr_decode(xprt);
			zcmn_err(getzoneid(), CE_NOTE,
			    "Failed to decode arguments for %s version %u "
			    "procedure %s client %s%s",
			    pgmname, vers + min_vers, procnames[which],
			    client_name(req), client_addr(req, cbuf));
			goto done;
		}
	}

	/*
	 * If Version 4 use that specific dispatch function.
	 */
	if (req->rq_vers == NFS_V4) {
		error += rfs4_dispatch(disp, req, xprt, dispbuf, &async_reply);
		if (!async_reply)
			goto done;
		return;
	}

	dis_flags = disp->dis_flags;

	/*
	 * Find export information and check authentication,
	 * setting the credential if everything is ok.
	 */
	if (disp->dis_getfh != NULL) {
		void *fh;
		fsid_t *fsid;
		fid_t *fid, *xfid;
		fhandle_t *fh2;
		nfs_fh3 *fh3;
		bool_t publicfh_ok = FALSE;

		fh = (*disp->dis_getfh)(args);
		switch (req->rq_vers) {
		case NFS_VERSION:
			fh2 = (fhandle_t *)fh;
			fsid = &fh2->fh_fsid;
			fid = (fid_t *)&fh2->fh_len;
			xfid = (fid_t *)&fh2->fh_xlen;
			publicfh_ok = PUBLIC_FH2(fh2) &&
			    (disp->dis_flags & RPC_PUBLICFH_OK);
			break;
		case NFS_V3:
			fh3 = (nfs_fh3 *)fh;
			fsid = &fh3->fh3_fsid;
			fid = FH3TOFIDP(fh3);
			xfid = FH3TOXFIDP(fh3);
			publicfh_ok = PUBLIC_FH3(fh3) &&
			    (disp->dis_flags & RPC_PUBLICFH_OK);
			break;
		}

		/*
		 * Fix for bug 1038302 - corbin
		 * There is a problem here if anonymous access is
		 * disallowed.  If the current request is part of the
		 * client's mount process for the requested filesystem,
		 * then it will carry root (uid 0) credentials on it, and
		 * will be denied by checkauth if that client does not
		 * have explicit root=0 permission.  This will cause the
		 * client's mount operation to fail.  As a work-around,
		 * we check here to see if the request is a getattr or
		 * statfs operation on the exported vnode itself, and
		 * pass a flag to checkauth with the result of this test.
		 *
		 * The filehandle refers to the mountpoint itself if
		 * the fh_data and fh_xdata portions of the filehandle
		 * are equal.
		 *
		 * Added anon_ok argument to checkauth().
		 */

		if ((dis_flags & RPC_ALLOWANON) && EQFID(fid, xfid))
			anon_ok = 1;
		else
			anon_ok = 0;

		cr = xprt->xp_cred;
		ASSERT(cr != NULL);
#ifdef DEBUG
		if (crgetref(cr) != 1) {
			crfree(cr);
			cr = crget();
			xprt->xp_cred = cr;
			cred_misses++;
		} else
			cred_hits++;
#else
		if (crgetref(cr) != 1) {
			crfree(cr);
			cr = crget();
			xprt->xp_cred = cr;
		}
#endif

		exi = checkexport(rip, fsid, xfid, publicfh_ok);
		dispbuf->exi = exi;
		if (exi != NULL) {

			/* Don't allow non-V4 clients access pseudo exports */
			if (PSEUDO(exi) && !publicfh_ok) {
				svcerr_weakauth(xprt);
				error++;
				goto done;
			}

			fse = fsh_get_ent(rip, exi->exi_fsid);
			DTRACE_PROBE1(nfsmig__i__common, fsh_entry_t *, fse);

			/*
			 * If the export is part of a frozen
			 * filesystem, then do not allow access.
			 */
			switch (req->rq_vers) {
			case NFS_VERSION:
				if (fse == NULL) {
					DTRACE_PROBE2(nfsmig__e__no_fse_v2,
					    fsid_t *, fsid,
					    exportinfo_t *, exi);
					migration_error(xprt, pgmname,
					    disp, NFSERR_NOENT);
					error++;
					goto done;
				}
				if (fse->fse_state == FSE_MOVED) {
					migration_error(xprt, pgmname,
					    disp, NFSERR_STALE);
					error++;
					goto done;
				} else if (rfs4_fse_is_frozen(fse) == TRUE) {
					/*
					 * Drop it.
					 */
					error++;
					goto done;
				} else
					release_freeze_lock = TRUE;
				break;
			case NFS_V3:
				if (fse == NULL) {
					DTRACE_PROBE2(nfsmig__e__no_fse_v3,
					    fsid_t *, fsid,
					    exportinfo_t *, exi);
					migration_error(xprt, pgmname,
					    disp, NFS3ERR_NOENT);
					error++;
					goto done;
				}
				if (fse->fse_state == FSE_MOVED) {
					migration_error(xprt, pgmname,
					    disp, NFS3ERR_STALE);
					error++;
					goto done;
				} else if (rfs4_fse_is_frozen(fse) == TRUE) {
					migration_error(xprt, pgmname,
					    disp, NFS3ERR_JUKEBOX);
					error++;
					goto done;
				} else
					release_freeze_lock = TRUE;
				break;
			}

			authres = checkauth(exi, req, cr, anon_ok, publicfh_ok);

			/*
			 * authres >  0: authentication OK - proceed
			 * authres == 0: authentication weak - return error
			 * authres <  0: authentication timeout - drop
			 */
			if (authres <= 0) {
				if (authres == 0) {
					svcerr_weakauth(xprt);
					error++;
				}
				goto done;
			}
		}
	} else
		cr = NULL;

	if ((dis_flags & RPC_MAPRESP) && (auth_flavor != RPCSEC_GSS)) {
		res = (char *)SVC_GETRES(xprt, disp->dis_ressz);
		if (res == NULL)
			res = (char *)&(dispbuf->res_buf);
	} else {
		res = (char *)&(dispbuf->res_buf);
	}

	if (!(dis_flags & RPC_IDEMPOTENT)) {
		dupstat = SVC_DUP_EXT(xprt, req, res, disp->dis_ressz, &dr,
		    &dispbuf->drstat);

		switch (dupstat) {
		case DUP_ERROR:
			svcerr_systemerr(xprt);
			error++;
			goto done;
			/* NOTREACHED */
		case DUP_INPROGRESS:
			if (res != (char *)&dispbuf->res_buf)
				SVC_FREERES(xprt);
			error++;
			goto done;
			/* NOTREACHED */
		case DUP_NEW:
		case DUP_DROP:
			curthread->t_flag |= T_DONTPEND;

			(*disp->dis_proc)(args, res, exi, req, cr);

			curthread->t_flag &= ~T_DONTPEND;
			if (curthread->t_flag & T_WOULDBLOCK) {
				curthread->t_flag &= ~T_WOULDBLOCK;
				SVC_DUPDONE_EXT(xprt, dr, res, NULL,
				    disp->dis_ressz, DUP_DROP);
				if (res != (char *)&dispbuf->res_buf)
					SVC_FREERES(xprt);
				error++;
				goto done;
			}
			if (dis_flags & RPC_AVOIDWORK) {
				SVC_DUPDONE_EXT(xprt, dr, res, NULL,
				    disp->dis_ressz, DUP_DROP);
			} else {
				SVC_DUPDONE_EXT(xprt, dr, res,
				    disp->dis_resfree == nullfree ? NULL :
				    disp->dis_resfree,
				    disp->dis_ressz, DUP_DONE);
				dispbuf->drstat = 1;
			}
			break;
		case DUP_DONE:
			break;
		}
	} else {
		curthread->t_flag |= T_DONTPEND;

		(*disp->dis_proc)(args, res, exi, req, cr);

		curthread->t_flag &= ~T_DONTPEND;
		if (curthread->t_flag & T_WOULDBLOCK) {
			curthread->t_flag &= ~T_WOULDBLOCK;
			if (res != (char *)&dispbuf->res_buf)
				SVC_FREERES(xprt);
			error++;
			goto done;
		}
	}

	if (auth_tooweak(req, res)) {
		svcerr_weakauth(xprt);
		error++;
		goto done;
	}

	/*
	 * Check to see if logging has been enabled on the server.
	 * If so, then obtain the export info struct to be used for
	 * the later writing of the log record.  This is done for
	 * the case that a lookup is done across a non-logged public
	 * file system.
	 */
	if (rip->ri_rzone->rz_nfslog_buffer_list != NULL) {
		nfslog_exi = nfslog_get_exi(exi, req, res, &nfslog_rec_id);
		/*
		 * Is logging enabled?
		 */
		logging_enabled = (nfslog_exi != NULL);

		/*
		 * Copy the netbuf for logging purposes, before it is
		 * freed by svc_sendreply().
		 */
		if (logging_enabled) {
			NFSLOG_COPY_NETBUF(nfslog_exi, xprt, &nb);
			/*
			 * If RPC_MAPRESP flag set (i.e. in V2 ops) the
			 * res gets copied directly into the mbuf and
			 * may be freed soon after the sendreply. So we
			 * must copy it here to a safe place...
			 */
			if (res != (char *)&dispbuf->res_buf) {
				bcopy(res, (char *)&dispbuf->res_buf,
				    disp->dis_ressz);
			}
		}
	}

	/*
	 * Serialize and send results struct
	 */
#ifdef DEBUG
	if (rfs_no_fast_xdrres == 0 && res != (char *)&(dispbuf->res_buf))
#else
	if (res != (char *)&(dispbuf->res_buf))
#endif
	{
		if (!svc_sendreply(xprt, disp->dis_fastxdrres, res)) {
			zcmn_err(getzoneid(), CE_NOTE,
			    "%s: bad sendreply", pgmname);
			error++;
		}
	} else {
		/*
		 * Only attempt an async sendreply if it is relevant
		 * for the transport type and it does not have
		 * legacy or other complications.
		 */
		async_reply = (SVC_QUEUE_SENDREPLY(xprt) &&
		    (req->rq_vers != NFS_VERSION) &&
		    !logging_enabled &&
		    (req->rq_label == NULL));

		if (async_reply &&
		    svc_async_sendreply(xprt, disp->dis_xdrres, res,
		    rfs_dispbuf_free, (void *)dispbuf)) {
			if (fse != NULL) {
				if (release_freeze_lock)
					rfs4_fse_release_reader(fse);
				fsh_ent_rele(rip, fse);
			}
			return;
		}

		/* unable to async sendreply, perform inline */
		if (!svc_sendreply(xprt, disp->dis_xdrres, res)) {
			zcmn_err(getzoneid(), CE_NOTE,
			    "%s: bad sendreply", pgmname);
			error++;
		}
	}

	/*
	 * Log if needed
	 */
	if (logging_enabled) {

		nfslog_write_record(nfslog_exi, req, args,
		    (char *)&dispbuf->res_buf, cr, &nb,
		    nfslog_rec_id, NFSLOG_ONE_BUFFER);

		exi_rele(nfslog_exi);
		kmem_free((&nb)->buf, (&nb)->len);
	}

	/*
	 * Free results struct. With the addition of NFS V4 we can
	 * have non-idempotent procedures with functions.
	 */
	if (disp->dis_resfree != nullfree && dispbuf->drstat == 0) {
		(*disp->dis_resfree)(res);
	}

done:
	/*
	 * Free arguments struct
	 */
	if (disp) {
		if (!SVC_FREEARGS(xprt, disp->dis_xdrargs, args)) {
			zcmn_err(getzoneid(), CE_NOTE,
			    "%s: bad freeargs", pgmname);
			error++;
		}
	} else {
		if (!SVC_FREEARGS(xprt, (xdrproc_t)0, (caddr_t)0)) {
			zcmn_err(getzoneid(), CE_NOTE,
			    "%s: bad freeargs", pgmname);
			error++;
		}
	}

	if (auth_flavor == RPCSEC_GSS)
		rpc_gss_cleanup(xprt);

	svc_xprt_free(xprt);

	if (req->rq_label != NULL)
		kmem_free(req->rq_label, sizeof (bslabel_t));

	if (fse != NULL) {
		if (release_freeze_lock)
			rfs4_fse_release_reader(fse);
		fsh_ent_rele(rip, fse);
	}

	if (exi != NULL)
		exi_rele(exi);

	if (dispbuf)
		kmem_cache_free(nfs_disp_cache, dispbuf);

	if (error) {
		nfsstat_inc_svstat(rip, req->rq_vers, NFS_BADCALLS);
		nfsstat_inc_svstat(rip, 0, NFS_BADCALLS);
	}
	nfsstat_inc_svstat(rip, req->rq_vers, NFS_CALLS);
	nfsstat_inc_svstat(rip, 0, NFS_CALLS);
}


void
rfs_dispbuf_free(void *arg, int error)
{
	rfs_disp_t *dispbuf = (rfs_disp_t *)arg;
	rfs_inst_t *rip = rfs_inst_svcxprt_to_rip(dispbuf->req.rq_xprt);
	struct rpcdisp *disp = dispbuf->disp;

	/*
	 * Free results struct. With the addition of NFS V4 we can
	 * have non-idempotent procedures with functions.
	 */
	switch (dispbuf->req.rq_vers) {
	case NFS_V3:
		if (disp->dis_resfree != nullfree && dispbuf->drstat == 0) {
			(*disp->dis_resfree)(&dispbuf->res_buf);
		}
		break;

	case NFS_V4:
		rfs4_dispatch_free(dispbuf);
		break;
	}

	/*
	 * Free arguments struct
	 */
	if (!SVC_FREEARGS(dispbuf->req.rq_xprt, disp->dis_xdrargs,
	    (char *)&dispbuf->args_buf)) {
		error++;
	}

	if (dispbuf->exi != NULL)
		exi_rele(dispbuf->exi);

	if (error) {
		nfsstat_inc_svstat(rip, dispbuf->req.rq_vers, NFS_BADCALLS);
		nfsstat_inc_svstat(rip, 0, NFS_BADCALLS);
	}
	nfsstat_inc_svstat(rip, dispbuf->req.rq_vers, NFS_CALLS);
	nfsstat_inc_svstat(rip, 0, NFS_CALLS);

	if (dispbuf->req.rq_cred.oa_flavor == RPCSEC_GSS)
		rpc_gss_cleanup(dispbuf->req.rq_xprt);

	svc_xprt_free(dispbuf->req.rq_xprt);

	kmem_cache_free(nfs_disp_cache, dispbuf);
}

static void
rfs_dispatch(struct svc_req *req, SVCXPRT *xprt)
{
	common_dispatch(req, xprt, NFS_VERSMIN, NFS_VERSMAX,
	    "NFS", rfs_disptable);
}

static char *aclcallnames_v2[] = {
	"ACL2_NULL",
	"ACL2_GETACL",
	"ACL2_SETACL",
	"ACL2_GETATTR",
	"ACL2_ACCESS",
	"ACL2_GETXATTRDIR"
};

static struct rpcdisp acldisptab_v2[] = {
	/*
	 * ACL VERSION 2
	 */

	/* ACL2_NULL = 0 */
	{rpc_null,
	    xdr_void, NULL_xdrproc_t, 0,
	    xdr_void, NULL_xdrproc_t, 0,
	    nullfree, RPC_IDEMPOTENT,
	    0},

	/* ACL2_GETACL = 1 */
	{acl2_getacl,
	    xdr_GETACL2args, xdr_fastGETACL2args, sizeof (GETACL2args),
	    xdr_GETACL2res, NULL_xdrproc_t, sizeof (GETACL2res),
	    acl2_getacl_free, RPC_IDEMPOTENT,
	    acl2_getacl_getfh},

	/* ACL2_SETACL = 2 */
	{acl2_setacl,
	    xdr_SETACL2args, NULL_xdrproc_t, sizeof (SETACL2args),
#ifdef _LITTLE_ENDIAN
	    xdr_SETACL2res, xdr_fastSETACL2res, sizeof (SETACL2res),
#else
	    xdr_SETACL2res, NULL_xdrproc_t, sizeof (SETACL2res),
#endif
	    nullfree, RPC_MAPRESP,
	    acl2_setacl_getfh},

	/* ACL2_GETATTR = 3 */
	{acl2_getattr,
	    xdr_GETATTR2args, xdr_fastGETATTR2args, sizeof (GETATTR2args),
#ifdef _LITTLE_ENDIAN
	    xdr_GETATTR2res, xdr_fastGETATTR2res, sizeof (GETATTR2res),
#else
	    xdr_GETATTR2res, NULL_xdrproc_t, sizeof (GETATTR2res),
#endif
	    nullfree, RPC_IDEMPOTENT|RPC_ALLOWANON|RPC_MAPRESP,
	    acl2_getattr_getfh},

	/* ACL2_ACCESS = 4 */
	{acl2_access,
	    xdr_ACCESS2args, xdr_fastACCESS2args, sizeof (ACCESS2args),
#ifdef _LITTLE_ENDIAN
	    xdr_ACCESS2res, xdr_fastACCESS2res, sizeof (ACCESS2res),
#else
	    xdr_ACCESS2res, NULL_xdrproc_t, sizeof (ACCESS2res),
#endif
	    nullfree, RPC_IDEMPOTENT|RPC_MAPRESP,
	    acl2_access_getfh},

	/* ACL2_GETXATTRDIR = 5 */
	{acl2_getxattrdir,
	    xdr_GETXATTRDIR2args, NULL_xdrproc_t, sizeof (GETXATTRDIR2args),
	    xdr_GETXATTRDIR2res, NULL_xdrproc_t, sizeof (GETXATTRDIR2res),
	    nullfree, RPC_IDEMPOTENT,
	    acl2_getxattrdir_getfh},
};

static char *aclcallnames_v3[] = {
	"ACL3_NULL",
	"ACL3_GETACL",
	"ACL3_SETACL",
	"ACL3_GETXATTRDIR"
};

static struct rpcdisp acldisptab_v3[] = {
	/*
	 * ACL VERSION 3
	 */

	/* ACL3_NULL = 0 */
	{rpc_null,
	    xdr_void, NULL_xdrproc_t, 0,
	    xdr_void, NULL_xdrproc_t, 0,
	    nullfree, RPC_IDEMPOTENT,
	    0},

	/* ACL3_GETACL = 1 */
	{acl3_getacl,
	    xdr_GETACL3args, NULL_xdrproc_t, sizeof (GETACL3args),
	    xdr_GETACL3res, NULL_xdrproc_t, sizeof (GETACL3res),
	    acl3_getacl_free, RPC_IDEMPOTENT,
	    acl3_getacl_getfh},

	/* ACL3_SETACL = 2 */
	{acl3_setacl,
	    xdr_SETACL3args, NULL_xdrproc_t, sizeof (SETACL3args),
	    xdr_SETACL3res, NULL_xdrproc_t, sizeof (SETACL3res),
	    nullfree, 0,
	    acl3_setacl_getfh},

	/* ACL3_GETXATTRDIR = 3 */
	{acl3_getxattrdir,
	    xdr_GETXATTRDIR3args, NULL_xdrproc_t, sizeof (GETXATTRDIR3args),
	    xdr_GETXATTRDIR3res, NULL_xdrproc_t, sizeof (GETXATTRDIR3res),
	    nullfree, RPC_IDEMPOTENT,
	    acl3_getxattrdir_getfh},
};

static struct rpc_disptable acl_disptable[] = {
	{sizeof (acldisptab_v2) / sizeof (acldisptab_v2[0]),
		aclcallnames_v2, acldisptab_v2},
	{sizeof (acldisptab_v3) / sizeof (acldisptab_v3[0]),
		aclcallnames_v3, acldisptab_v3},
};

static void
acl_dispatch(struct svc_req *req, SVCXPRT *xprt)
{
	common_dispatch(req, xprt, NFS_ACL_VERSMIN, NFS_ACL_VERSMAX,
	    "ACL", acl_disptable);
}

int
checkwin(int flavor, int window, struct svc_req *req)
{
	struct authdes_cred *adc;

	switch (flavor) {
	case AUTH_DES:
		adc = (struct authdes_cred *)req->rq_clntcred;
		if (adc->adc_fullname.window > window)
			return (0);
		break;

	default:
		break;
	}
	return (1);
}


/*
 * checkauth() will check the access permission against the export
 * information.  Then map root uid/gid to appropriate uid/gid.
 *
 * This routine is used by NFS V3 and V2 code.
 */
static int
checkauth(struct exportinfo *exi, struct svc_req *req, cred_t *cr, int anon_ok,
    bool_t publicfh_ok)
{
	int i, nfsflavor, rpcflavor, stat, access;
	struct secinfo *secp;
	caddr_t principal;
	char buf[INET6_ADDRSTRLEN]; /* to hold both IPv4 and IPv6 addr */
	int anon_res = 0;

	/*
	 * Check for privileged port number
	 * N.B.:  this assumes that we know the format of a netbuf.
	 */
	if (nfs_portmon) {
		struct sockaddr *ca;
		ca = (struct sockaddr *)svc_getrpccaller(req->rq_xprt)->buf;

		if (ca == NULL)
			return (0);

		if ((ca->sa_family == AF_INET &&
		    ntohs(((struct sockaddr_in *)ca)->sin_port) >=
		    IPPORT_RESERVED) ||
		    (ca->sa_family == AF_INET6 &&
		    ntohs(((struct sockaddr_in6 *)ca)->sin6_port) >=
		    IPPORT_RESERVED)) {
			zcmn_err(getzoneid(), CE_NOTE,
			    "nfs_server: client %s%ssent NFS request from "
			    "unprivileged port",
			    client_name(req), client_addr(req, buf));
			return (0);
		}
	}

	/*
	 *  return 1 on success or 0 on failure
	 */
	stat = sec_svc_getcred(req, cr, &principal, &nfsflavor);

	/*
	 * A failed AUTH_UNIX svc_get_cred() implies we couldn't set
	 * the credentials; below we map that to anonymous.
	 */
	if (!stat && nfsflavor != AUTH_UNIX) {
		zcmn_err(getzoneid(), CE_NOTE,
		    "nfs_server: couldn't get unix cred for %s",
		    client_name(req));
		return (0);
	}

	/*
	 * Short circuit checkauth() on operations that support the
	 * public filehandle, and if the request for that operation
	 * is using the public filehandle. Note that we must call
	 * sec_svc_getcred() first so that xp_cookie is set to the
	 * right value. Normally xp_cookie is just the RPC flavor
	 * of the the request, but in the case of RPCSEC_GSS it
	 * could be a pseudo flavor.
	 */
	if (publicfh_ok)
		return (1);

	rpcflavor = req->rq_cred.oa_flavor;

	/*
	 * Check if the auth flavor is valid for this export
	 */
	access = nfsauth_access(exi, req);
	if (access & NFSAUTH_DROP)
		return (-1);	/* drop the request */

	if (access & NFSAUTH_DENIED) {
		/*
		 * If anon_ok == 1 and we got NFSAUTH_DENIED, it was
		 * probably due to the flavor not matching during the
		 * the mount attempt. So map the flavor to AUTH_NONE
		 * so that the credentials get mapped to the anonymous
		 * user.
		 */
		if (anon_ok == 1)
			rpcflavor = AUTH_NONE;
		else
			return (0);	/* deny access */

	} else if (access & NFSAUTH_MAPNONE) {
		/*
		 * Access was granted even though the flavor mismatched
		 * because AUTH_NONE was one of the exported flavors.
		 */
		rpcflavor = AUTH_NONE;

	} else if (access & NFSAUTH_WRONGSEC) {
		/*
		 * NFSAUTH_WRONGSEC is used for NFSv4. If we get here,
		 * it means a client ignored the list of allowed flavors
		 * returned via the MOUNT protocol. So we just disallow it!
		 */
		return (0);
	}

	switch (rpcflavor) {
	case AUTH_NONE:
		anon_res = crsetugid(cr, exi->exi_export.ex_anon,
		    exi->exi_export.ex_anon);
		(void) crsetgroups(cr, 0, NULL);
		break;

	case AUTH_UNIX:
		if (!stat || crgetuid(cr) == 0 && !(access & NFSAUTH_ROOT)) {
			anon_res = crsetugid(cr, exi->exi_export.ex_anon,
			    exi->exi_export.ex_anon);
			(void) crsetgroups(cr, 0, NULL);
		} else if (!stat || crgetuid(cr) == 0 &&
		    access & NFSAUTH_ROOT) {
			/*
			 * It is root, so apply rootid to get real UID
			 * Find the secinfo structure.  We should be able
			 * to find it by the time we reach here.
			 * nfsauth_access() has done the checking.
			 */
			secp = NULL;
			for (i = 0; i < exi->exi_export.ex_seccnt; i++) {
				struct secinfo *sptr;
				sptr = &exi->exi_export.ex_secinfo[i];
				if (sptr->s_secinfo.sc_nfsnum == nfsflavor) {
					secp = sptr;
					break;
				}
			}
			if (secp != NULL) {
				(void) crsetugid(cr, secp->s_rootid,
				    secp->s_rootid);
				(void) crsetgroups(cr, 0, NULL);
			}
		}
		break;

	case AUTH_DES:
	case RPCSEC_GSS:
		/*
		 *  Find the secinfo structure.  We should be able
		 *  to find it by the time we reach here.
		 *  nfsauth_access() has done the checking.
		 */
		secp = NULL;
		for (i = 0; i < exi->exi_export.ex_seccnt; i++) {
			if (exi->exi_export.ex_secinfo[i].s_secinfo.sc_nfsnum ==
			    nfsflavor) {
				secp = &exi->exi_export.ex_secinfo[i];
				break;
			}
		}

		if (!secp) {
			zcmn_err(getzoneid(), CE_NOTE,
			    "nfs_server: client %s%shad "
			    "no secinfo data for flavor %d",
			    client_name(req), client_addr(req, buf),
			    nfsflavor);
			return (0);
		}

		if (!checkwin(rpcflavor, secp->s_window, req)) {
			zcmn_err(getzoneid(), CE_NOTE,
			    "nfs_server: client %s%sused invalid "
			    "auth window value",
			    client_name(req), client_addr(req, buf));
			return (0);
		}

		/*
		 * Map root principals listed in the share's root= list to root,
		 * and map any others principals that were mapped to root by RPC
		 * to anon.
		 */
		if (principal && sec_svc_inrootlist(rpcflavor, principal,
		    secp->s_rootcnt, secp->s_rootnames)) {
			if (crgetuid(cr) == 0 && secp->s_rootid == 0)
				return (1);


			(void) crsetugid(cr, secp->s_rootid, secp->s_rootid);

			/*
			 * NOTE: If and when kernel-land privilege tracing is
			 * added this may have to be replaced with code that
			 * retrieves root's supplementary groups (e.g., using
			 * kgss_get_group_info().  In the meantime principals
			 * mapped to uid 0 get all privileges, so setting cr's
			 * supplementary groups for them does nothing.
			 */
			(void) crsetgroups(cr, 0, NULL);

			return (1);
		}

		/*
		 * Not a root princ, or not in root list, map UID 0/nobody to
		 * the anon ID for the share.  (RPC sets cr's UIDs and GIDs to
		 * UID_NOBODY and GID_NOBODY, respectively.)
		 */
		if (crgetuid(cr) != 0 &&
		    (crgetuid(cr) != UID_NOBODY || crgetgid(cr) != GID_NOBODY))
			return (1);

		anon_res = crsetugid(cr, exi->exi_export.ex_anon,
		    exi->exi_export.ex_anon);
		(void) crsetgroups(cr, 0, NULL);
		break;
	default:
		return (0);
	} /* switch on rpcflavor */

	/*
	 * Even if anon access is disallowed via ex_anon == -1, we allow
	 * this access if anon_ok is set.  So set creds to the default
	 * "nobody" id.
	 */
	if (anon_res != 0) {
		if (anon_ok == 0) {
			zcmn_err(getzoneid(), CE_NOTE,
			    "nfs_server: client %s%ssent wrong "
			    "authentication for %s",
			    client_name(req), client_addr(req, buf),
			    exi->exi_export.ex_path ?
			    exi->exi_export.ex_path : "?");
			return (0);
		}

		if (crsetugid(cr, UID_NOBODY, GID_NOBODY) != 0)
			return (0);
	}

	return (1);
}

/*
 * returns 0 on failure, -1 on a drop, -2 on wrong security flavor,
 * and 1 on success
 */
int
checkauth4(struct compound_state *cs, struct svc_req *req)
{
	int i, rpcflavor, access;
	struct secinfo *secp;
	char buf[MAXHOST + 1];
	int anon_res = 0, nfsflavor;
	struct exportinfo *exi;
	cred_t	*cr;
	caddr_t	principal;

	exi = cs->exi;
	cr = cs->cr;
	principal = cs->principal;
	nfsflavor = cs->nfsflavor;

	ASSERT(cr != NULL);

	rpcflavor = req->rq_cred.oa_flavor;
	cs->access &= ~CS_ACCESS_LIMITED;

	/*
	 * Check for privileged port number
	 * N.B.:  this assumes that we know the format of a netbuf.
	 */
	if (nfs_portmon) {
		struct sockaddr *ca;
		ca = (struct sockaddr *)svc_getrpccaller(req->rq_xprt)->buf;

		if (ca == NULL)
			return (0);

		if ((ca->sa_family == AF_INET &&
		    ntohs(((struct sockaddr_in *)ca)->sin_port) >=
		    IPPORT_RESERVED) ||
		    (ca->sa_family == AF_INET6 &&
		    ntohs(((struct sockaddr_in6 *)ca)->sin6_port) >=
		    IPPORT_RESERVED)) {
			zcmn_err(getzoneid(), CE_NOTE,
			    "nfs_server: client %s%ssent NFSv4 request from "
			    "unprivileged port",
			    client_name(req), client_addr(req, buf));
			return (0);
		}
	}

	/*
	 * Check the access right per auth flavor on the vnode of
	 * this export for the given request.
	 */
	access = nfsauth4_access(cs->exi, cs->vp, req);

	if (access & NFSAUTH_WRONGSEC)
		return (-2);	/* no access for this security flavor */

	if (access & NFSAUTH_DROP)
		return (-1);	/* drop the request */

	if (access & NFSAUTH_DENIED) {

		if (exi->exi_export.ex_seccnt > 0)
			return (0);	/* deny access */

	} else if (access & NFSAUTH_LIMITED) {

		cs->access |= CS_ACCESS_LIMITED;

	} else if (access & NFSAUTH_MAPNONE) {
		/*
		 * Access was granted even though the flavor mismatched
		 * because AUTH_NONE was one of the exported flavors.
		 */
		rpcflavor = AUTH_NONE;
	}

	/*
	 * XXX probably need to redo some of it for nfsv4?
	 * return 1 on success or 0 on failure
	 */

	switch (rpcflavor) {
	case AUTH_NONE:
		anon_res = crsetugid(cr, exi->exi_export.ex_anon,
		    exi->exi_export.ex_anon);
		(void) crsetgroups(cr, 0, NULL);
		break;

	case AUTH_UNIX:
		if (crgetuid(cr) == 0 && !(access & NFSAUTH_ROOT)) {
			anon_res = crsetugid(cr, exi->exi_export.ex_anon,
			    exi->exi_export.ex_anon);
			(void) crsetgroups(cr, 0, NULL);
		} else if (crgetuid(cr) == 0 && access & NFSAUTH_ROOT) {
			/*
			 * It is root, so apply rootid to get real UID
			 * Find the secinfo structure.  We should be able
			 * to find it by the time we reach here.
			 * nfsauth_access() has done the checking.
			 */
			secp = NULL;
			for (i = 0; i < exi->exi_export.ex_seccnt; i++) {
				struct secinfo *sptr;
				sptr = &exi->exi_export.ex_secinfo[i];
				if (sptr->s_secinfo.sc_nfsnum == nfsflavor) {
					secp = &exi->exi_export.ex_secinfo[i];
					break;
				}
			}
			if (secp != NULL) {
				(void) crsetugid(cr, secp->s_rootid,
				    secp->s_rootid);
				(void) crsetgroups(cr, 0, NULL);
			}
		}
		break;

	default:
		/*
		 *  Find the secinfo structure.  We should be able
		 *  to find it by the time we reach here.
		 *  nfsauth_access() has done the checking.
		 */
		secp = NULL;
		for (i = 0; i < exi->exi_export.ex_seccnt; i++) {
			if (exi->exi_export.ex_secinfo[i].s_secinfo.sc_nfsnum ==
			    nfsflavor) {
				secp = &exi->exi_export.ex_secinfo[i];
				break;
			}
		}

		if (!secp) {
			zcmn_err(getzoneid(), CE_NOTE,
			    "nfs_server: client %s%shad "
			    "no secinfo data for flavor %d",
			    client_name(req), client_addr(req, buf),
			    nfsflavor);
			return (0);
		}

		if (!checkwin(rpcflavor, secp->s_window, req)) {
			zcmn_err(getzoneid(), CE_NOTE,
			    "nfs_server: client %s%sused invalid "
			    "auth window value",
			    client_name(req), client_addr(req, buf));
			return (0);
		}

		/*
		 * Map root principals listed in the share's root= list to root,
		 * and map any others principals that were mapped to root by RPC
		 * to anon. If not going to anon, set to rootid (root_mapping).
		 */
		if (principal && sec_svc_inrootlist(rpcflavor, principal,
		    secp->s_rootcnt, secp->s_rootnames)) {
			if (crgetuid(cr) == 0 && secp->s_rootid == 0)
				return (1);

			(void) crsetugid(cr, secp->s_rootid, secp->s_rootid);

			/*
			 * NOTE: If and when kernel-land privilege tracing is
			 * added this may have to be replaced with code that
			 * retrieves root's supplementary groups (e.g., using
			 * kgss_get_group_info().  In the meantime principals
			 * mapped to uid 0 get all privileges, so setting cr's
			 * supplementary groups for them does nothing.
			 */
			(void) crsetgroups(cr, 0, NULL);

			return (1);
		}

		/*
		 * Not a root princ, or not in root list, map UID 0/nobody to
		 * the anon ID for the share.  (RPC sets cr's UIDs and GIDs to
		 * UID_NOBODY and GID_NOBODY, respectively.)
		 */
		if (crgetuid(cr) != 0 &&
		    (crgetuid(cr) != UID_NOBODY || crgetgid(cr) != GID_NOBODY))
			return (1);

		anon_res = crsetugid(cr, exi->exi_export.ex_anon,
		    exi->exi_export.ex_anon);
		(void) crsetgroups(cr, 0, NULL);
		break;
	} /* switch on rpcflavor */

	/*
	 * Even if anon access is disallowed via ex_anon == -1, we allow
	 * this access if anon_ok is set.  So set creds to the default
	 * "nobody" id.
	 */

	if (anon_res != 0) {
		zcmn_err(getzoneid(), CE_NOTE,
		    "nfs_server: client %s%ssent wrong "
		    "authentication for %s",
		    client_name(req), client_addr(req, buf),
		    exi->exi_export.ex_path ?
		    exi->exi_export.ex_path : "?");
		return (0);
	}

	return (1);
}


static char *
client_name(struct svc_req *req)
{
	char *hostname = NULL;

	/*
	 * If it's a Unix cred then use the
	 * hostname from the credential.
	 */
	if (req->rq_cred.oa_flavor == AUTH_UNIX) {
		hostname = ((struct authunix_parms *)
		    req->rq_clntcred)->aup_machname;
	}
	if (hostname == NULL)
		hostname = "";

	return (hostname);
}

static char *
client_addr(struct svc_req *req, char *buf)
{
	struct sockaddr *ca;
	uchar_t *b;
	char *frontspace = "";

	/*
	 * We assume we are called in tandem with client_name and the
	 * format string looks like "...client %s%sblah blah..."
	 *
	 * If it's a Unix cred then client_name returned
	 * a host name, so we need insert a space between host name
	 * and IP address.
	 */
	if (req->rq_cred.oa_flavor == AUTH_UNIX)
		frontspace = " ";

	/*
	 * Convert the caller's IP address to a dotted string
	 */
	ca = (struct sockaddr *)svc_getrpccaller(req->rq_xprt)->buf;

	if (ca->sa_family == AF_INET) {
		b = (uchar_t *)&((struct sockaddr_in *)ca)->sin_addr;
		(void) sprintf(buf, "%s(%d.%d.%d.%d) ", frontspace,
		    b[0] & 0xFF, b[1] & 0xFF, b[2] & 0xFF, b[3] & 0xFF);
	} else if (ca->sa_family == AF_INET6) {
		struct sockaddr_in6 *sin6;
		sin6 = (struct sockaddr_in6 *)ca;
		(void) kinet_ntop6((uchar_t *)&sin6->sin6_addr,
		    buf, INET6_ADDRSTRLEN);

	} else {

		/*
		 * No IP address to print. If there was a host name
		 * printed, then we print a space.
		 */
		(void) sprintf(buf, frontspace);
	}

	return (buf);
}

/*
 * NFS Server initialization routine.  This routine should only be called
 * once (when the nfssrv kmod is loaded).
 */
void
nfs_srvinit(void)
{

	bzero(&rfs, sizeof (rfs));
	mutex_init(&rfs.rg_lock, NULL, MUTEX_DEFAULT, NULL);
	list_create(&rfs.rg_instances, sizeof (rfs_inst_t),
	    offsetof(rfs_inst_t, ri_gnode));
	list_create(&rfs.rg_zones, sizeof (rfs_zone_t),
	    offsetof(rfs_zone_t, rz_gnode));
	zone_key_create(&rfs.rg_rfszone_key, NULL, rfs_zone_shutdown, NULL);
	rfs_global_init();
	rfs3_global_init();
	rfs4_global_init();
}

/*
 * NFS Server finalization routine. This routine is called to cleanup the
 * initialization work previously performed if the NFS server module could
 * not be loaded correctly.
 */
void
nfs_srvfini(void)
{
	rfs4_global_fini();
	rfs3_global_fini();
	rfs_global_fini();
	list_destroy(&rfs.rg_instances);
	list_destroy(&rfs.rg_zones);
	(void) zone_key_delete(rfs.rg_rfszone_key);
	mutex_destroy(&rfs.rg_lock);
}

/*
 * Set up an iovec array of up to cnt pointers.
 */

void
mblk_to_iov(mblk_t *m, int cnt, struct iovec *iovp)
{
	while (m != NULL && cnt-- > 0) {
		iovp->iov_base = (caddr_t)m->b_rptr;
		iovp->iov_len = (m->b_wptr - m->b_rptr);
		iovp++;
		m = m->b_cont;
	}
}

/*
 * Common code between NFS Version 2 and NFS Version 3 for the public
 * filehandle multicomponent lookups.
 */

/*
 * Public filehandle evaluation of a multi-component lookup, following
 * symbolic links, if necessary. This may result in a vnode in another
 * filesystem, which is OK as long as the other filesystem is exported.
 *
 * Note that the exi will be set either to NULL or a new reference to the
 * exportinfo struct that corresponds to the vnode of the multi-component path.
 * It is the callers responsibility to release this reference.
 */
int
rfs_publicfh_mclookup(rfs_inst_t *rip, char *p, vnode_t *dvp, cred_t *cr,
    vnode_t **vpp, struct exportinfo **exi, struct sec_ol *sec)
{
	int pathflag;
	vnode_t *mc_dvp = NULL;
	vnode_t *realvp;
	int error;

	*exi = NULL;

	/*
	 * check if the given path is a url or native path. Since p is
	 * modified by MCLpath(), it may be empty after returning from
	 * there, and should be checked.
	 */
	if ((pathflag = MCLpath(&p)) == -1)
		return (EIO);

	/*
	 * If pathflag is SECURITY_QUERY, turn the SEC_QUERY bit
	 * on in sec->sec_flags. This bit will later serve as an
	 * indication in makefh_ol() or makefh3_ol() to overload the
	 * filehandle to contain the sec modes used by the server for
	 * the path.
	 */
	if (pathflag == SECURITY_QUERY) {
		if ((sec->sec_index = (uint_t)(*p)) > 0) {
			sec->sec_flags |= SEC_QUERY;
			p++;
			if ((pathflag = MCLpath(&p)) == -1)
				return (EIO);
		} else {
			zcmn_err(getzoneid(), CE_NOTE,
			    "nfs_server: invalid security index %d, "
			    "violating WebNFS SNEGO protocol.", sec->sec_index);
			return (EIO);
		}
	}

	if (p[0] == '\0') {
		error = ENOENT;
		goto publicfh_done;
	}

	error = rfs_pathname(rip, p, &mc_dvp, vpp, dvp, cr, pathflag);

	/*
	 * If name resolves to "/" we get EINVAL since we asked for
	 * the vnode of the directory that the file is in. Try again
	 * with NULL directory vnode.
	 */
	if (error == EINVAL) {
		error = rfs_pathname(rip, p, NULL, vpp, dvp, cr, pathflag);
		if (!error) {
			ASSERT(*vpp != NULL);
			if ((*vpp)->v_type == VDIR) {
				VN_HOLD(*vpp);
				mc_dvp = *vpp;
			} else {
				/*
				 * This should not happen, the filesystem is
				 * in an inconsistent state. Fail the lookup
				 * at this point.
				 */
				VN_RELE(*vpp);
				error = EINVAL;
			}
		}
	}

	if (error)
		goto publicfh_done;

	if (*vpp == NULL) {
		error = ENOENT;
		goto publicfh_done;
	}

	ASSERT(mc_dvp != NULL);
	ASSERT(*vpp != NULL);

	if ((*vpp)->v_type == VDIR) {
		do {
			/*
			 * *vpp may be an AutoFS node, so we perform
			 * a VOP_ACCESS() to trigger the mount of the intended
			 * filesystem, so we can perform the lookup in the
			 * intended filesystem.
			 */
			(void) VOP_ACCESS(*vpp, 0, 0, cr, NULL);

			/*
			 * If vnode is covered, get the
			 * the topmost vnode.
			 */
			if (vn_mountedvfs(*vpp) != NULL) {
				error = traverse(vpp);
				if (error) {
					VN_RELE(*vpp);
					goto publicfh_done;
				}
			}

			if (VOP_REALVP(*vpp, &realvp, NULL) == 0 &&
			    realvp != *vpp) {
				/*
				 * If realvp is different from *vpp
				 * then release our reference on *vpp, so that
				 * the export access check be performed on the
				 * real filesystem instead.
				 */
				VN_HOLD(realvp);
				VN_RELE(*vpp);
				*vpp = realvp;
			} else {
				break;
			}
		/* LINTED */
		} while (TRUE);

		/*
		 * Let nfs_vptexi() figure what the real parent is.
		 */
		VN_RELE(mc_dvp);
		mc_dvp = NULL;

	} else {
		/*
		 * If vnode is covered, get the
		 * the topmost vnode.
		 */
		if (vn_mountedvfs(mc_dvp) != NULL) {
			error = traverse(&mc_dvp);
			if (error) {
				VN_RELE(*vpp);
				goto publicfh_done;
			}
		}

		if (VOP_REALVP(mc_dvp, &realvp, NULL) == 0 &&
		    realvp != mc_dvp) {
			/*
			 * *vpp is a file, obtain realvp of the parent
			 * directory vnode.
			 */
			VN_HOLD(realvp);
			VN_RELE(mc_dvp);
			mc_dvp = realvp;
		}
	}

	/*
	 * The pathname may take us from the public filesystem to another.
	 * If that's the case then just set the exportinfo to the new export
	 * and build filehandle for it. Thanks to per-access checking there's
	 * no security issues with doing this. If the client is not allowed
	 * access to this new export then it will get an access error when it
	 * tries to use the filehandle
	 */
	if (error = nfs_check_vpexi(rip, mc_dvp, *vpp, kcred, exi)) {
		VN_RELE(*vpp);
		goto publicfh_done;
	}

	/*
	 * Not allowed access to pseudo exports.
	 */
	if (PSEUDO(*exi)) {
		error = ENOENT;
		VN_RELE(*vpp);
		goto publicfh_done;
	}

	/*
	 * Do a lookup for the index file. We know the index option doesn't
	 * allow paths through handling in the share command, so mc_dvp will
	 * be the parent for the index file vnode, if its present. Use
	 * temporary pointers to preserve and reuse the vnode pointers of the
	 * original directory in case there's no index file. Note that the
	 * index file is a native path, and should not be interpreted by
	 * the URL parser in rfs_pathname()
	 */
	if (((*exi)->exi_export.ex_flags & EX_INDEX) &&
	    ((*vpp)->v_type == VDIR) && (pathflag == URLPATH)) {
		vnode_t *tvp, *tmc_dvp;	/* temporary vnode pointers */

		tmc_dvp = mc_dvp;
		mc_dvp = tvp = *vpp;

		error = rfs_pathname(rip, (*exi)->exi_export.ex_index, NULL,
		    vpp, mc_dvp, cr, NATIVEPATH);

		if (error == ENOENT) {
			*vpp = tvp;
			mc_dvp = tmc_dvp;
			error = 0;
		} else {	/* ok or error other than ENOENT */
			if (tmc_dvp)
				VN_RELE(tmc_dvp);
			if (error)
				goto publicfh_done;

			/*
			 * Found a valid vp for index "filename". Sanity check
			 * for odd case where a directory is provided as index
			 * option argument and leads us to another filesystem
			 */

			/* Release the reference on the old exi value */
			ASSERT(*exi != NULL);
			exi_rele(*exi);

			error = nfs_check_vpexi(rip, mc_dvp, *vpp, kcred, exi);
			if (error) {
				VN_RELE(*vpp);
				goto publicfh_done;
			}
		}
	}

publicfh_done:
	if (mc_dvp)
		VN_RELE(mc_dvp);

	return (error);
}

/*
 * Evaluate a multi-component path
 */
int
rfs_pathname(
	rfs_inst_t *rip,		/* instance ptr */
	char *path,			/* pathname to evaluate */
	vnode_t **dirvpp,		/* ret for ptr to parent dir vnode */
	vnode_t **compvpp,		/* ret for ptr to component vnode */
	vnode_t *startdvp,		/* starting vnode */
	cred_t *cr,			/* user's credential */
	int pathflag)			/* flag to identify path, e.g. URL */
{
	char namebuf[TYPICALMAXPATHLEN];
	struct pathname pn;
	int error;
	vnode_t *zonevp = RFS_INST_ROOTVP(rip);

	/*
	 * If pathname starts with '/', then set startdvp to root.
	 */
	if (*path == '/') {
		while (*path == '/')
			path++;
		startdvp = zonevp;
	}

	error = pn_get_buf(path, UIO_SYSSPACE, &pn, namebuf, sizeof (namebuf));
	if (error == 0) {
		/*
		 * Call the URL parser for URL paths to modify the original
		 * string to handle any '%' encoded characters that exist.
		 * Done here to avoid an extra bcopy in the lookup.
		 * We need to be careful about pathlen's. We know that
		 * rfs_pathname() is called with a non-empty path. However,
		 * it could be emptied due to the path simply being all /'s,
		 * which is valid to proceed with the lookup, or due to the
		 * URL parser finding an encoded null character at the
		 * beginning of path which should not proceed with the lookup.
		 */
		if (pn.pn_pathlen != 0 && pathflag == URLPATH) {
			URLparse(pn.pn_path);
			if ((pn.pn_pathlen = strlen(pn.pn_path)) == 0)
				return (ENOENT);
		}
		if (zonevp != rootdir)
			VN_HOLD(zonevp);
		VN_HOLD(startdvp);
		error = lookuppnvp(&pn, NULL, NO_FOLLOW, dirvpp, compvpp,
		    zonevp, startdvp, cr);
	}
	if (error == ENAMETOOLONG) {
		/*
		 * This thread used a pathname > TYPICALMAXPATHLEN bytes long.
		 */
		if (error = pn_get(path, UIO_SYSSPACE, &pn))
			return (error);
		if (pn.pn_pathlen != 0 && pathflag == URLPATH) {
			URLparse(pn.pn_path);
			if ((pn.pn_pathlen = strlen(pn.pn_path)) == 0) {
				pn_free(&pn);
				return (ENOENT);
			}
		}
		if (zonevp != rootdir)
			VN_HOLD(zonevp);
		VN_HOLD(startdvp);
		error = lookuppnvp(&pn, NULL, NO_FOLLOW, dirvpp, compvpp,
		    zonevp, startdvp, cr);
		pn_free(&pn);
	}

	return (error);
}

/*
 * Adapt the multicomponent lookup path depending on the pathtype
 */
static int
MCLpath(char **path)
{
	unsigned char c = (unsigned char)**path;

	/*
	 * If the MCL path is between 0x20 and 0x7E (graphic printable
	 * character of the US-ASCII coded character set), its a URL path,
	 * per RFC 1738.
	 */
	if (c >= 0x20 && c <= 0x7E)
		return (URLPATH);

	/*
	 * If the first octet of the MCL path is not an ASCII character
	 * then it must be interpreted as a tag value that describes the
	 * format of the remaining octets of the MCL path.
	 *
	 * If the first octet of the MCL path is 0x81 it is a query
	 * for the security info.
	 */
	switch (c) {
	case 0x80:	/* native path, i.e. MCL via mount protocol */
		(*path)++;
		return (NATIVEPATH);
	case 0x81:	/* security query */
		(*path)++;
		return (SECURITY_QUERY);
	default:
		return (-1);
	}
}

#define	fromhex(c)  ((c >= '0' && c <= '9') ? (c - '0') : \
			((c >= 'A' && c <= 'F') ? (c - 'A' + 10) :\
			((c >= 'a' && c <= 'f') ? (c - 'a' + 10) : 0)))

/*
 * The implementation of URLparse guarantees that the final string will
 * fit in the original one. Replaces '%' occurrences followed by 2 characters
 * with its corresponding hexadecimal character.
 */
static void
URLparse(char *str)
{
	char *p, *q;

	p = q = str;
	while (*p) {
		*q = *p;
		if (*p++ == '%') {
			if (*p) {
				*q = fromhex(*p) * 16;
				p++;
				if (*p) {
					*q += fromhex(*p);
					p++;
				}
			}
		}
		q++;
	}
	*q = '\0';
}


/*
 * Get the export information for the lookup vnode, and verify its
 * useable.
 */
int
nfs_check_vpexi(rfs_inst_t *rip, vnode_t *mc_dvp, vnode_t *vp, cred_t *cr,
    struct exportinfo **exi)
{
	int walk;
	int error = 0;

	*exi = nfs_vptoexi(rip, mc_dvp, vp, cr, &walk, NULL, FALSE);
	if (*exi == NULL)
		error = EACCES;
	else {
		/*
		 * If nosub is set for this export then
		 * a lookup relative to the public fh
		 * must not terminate below the
		 * exported directory.
		 */
		if ((*exi)->exi_export.ex_flags & EX_NOSUB && walk > 0)
			error = EACCES;
	}

	return (error);
}

/*
 * Do the main work of handling HA-NFSv4 Resource Group failover
 * We need to detect whether any RG admin paths have been added or removed,
 * and adjust resources accordingly.
 * Currently we're using a very inefficient algorithm, ~ 2 * O(n**2). In
 * order to scale, the list and array of paths need to be held in more
 * suitable data structures.
 */
void
hanfsv4_failover(rfs_inst_t *rip)
{
	rfs4_inst_t *vip = &rip->ri_v4;
	int i, start_grace, numadded_paths = 0;
	char **added_paths = NULL;
	rfs4_dss_path_t *dss_path;

	/*
	 * Note: currently, r4_dss_pathlist cannot be NULL, since
	 * it will always include an entry for NFS4_DSS_VAR_DIR. If we
	 * make the latter dynamically specified too, the following will
	 * need to be adjusted.
	 */

	/*
	 * First, look for removed paths: RGs that have been failed-over
	 * away from this node.
	 * Walk the "currently-serving" r4_dss_pathlist and, for each
	 * path, check if it is on the "passed-in" rfs4_dss_newpaths array
	 * from nfsd. If not, that RG path has been removed.
	 *
	 * Note that nfsd has sorted rfs4_dss_newpaths for us, and removed
	 * any duplicates.
	 */
	dss_path = vip->r4_dss_pathlist;
	do {
		int found = 0;
		char *path = dss_path->ds_path;

		/* used only for non-HA so may not be removed */
		if (strcmp(path, NFS4_DSS_VAR_DIR) == 0) {
			dss_path = dss_path->ds_next;
			continue;
		}

		for (i = 0; i < vip->r4_dss_numnewpaths; i++) {
			int cmpret;
			char *newpath = vip->r4_dss_newpaths[i];

			/*
			 * Since nfsd has sorted rfs4_dss_newpaths for us,
			 * once the return from strcmp is negative we know
			 * we've passed the point where "path" should be,
			 * and can stop searching: "path" has been removed.
			 */
			cmpret = strcmp(path, newpath);
			if (cmpret < 0)
				break;
			if (cmpret == 0) {
				found = 1;
				break;
			}
		}

		if (found == 0) {
			unsigned index = dss_path->ds_index;
			rfs4_grace_t *sip = dss_path->ds_sip;
			rfs4_dss_path_t *path_next = dss_path->ds_next;

			/*
			 * This path has been removed.
			 * We must clear out the grace reference to
			 * it, since it's now owned by another
			 * node: we should not attempt to touch it.
			 */
			ASSERT(dss_path == sip->rg_dss_paths[index]);
			sip->rg_dss_paths[index] = NULL;

			/* remove from "currently-serving" list, and destroy */
			remque(dss_path);
			/* allow for NUL */
			kmem_free(dss_path->ds_path,
			    strlen(dss_path->ds_path) + 1);
			kmem_free(dss_path, sizeof (rfs4_dss_path_t));

			dss_path = path_next;
		} else {
			/* path was found; not removed */
			dss_path = dss_path->ds_next;
		}
	} while (dss_path != vip->r4_dss_pathlist);

	/*
	 * Now, look for added paths: RGs that have been failed-over
	 * to this node.
	 * Walk the "passed-in" rfs4_dss_newpaths array from nfsd and,
	 * for each path, check if it is on the "currently-serving"
	 * r4_dss_pathlist. If not, that RG path has been added.
	 *
	 * Note: we don't do duplicate detection here; nfsd does that for us.
	 *
	 * Note: numadded_paths <= vip->r4_dss_numnewpaths, which gives us
	 * an upper bound for the size needed for added_paths[numadded_paths].
	 */

	/* probably more space than we need, but guaranteed to be enough */
	if (vip->r4_dss_numnewpaths > 0) {
		size_t sz = vip->r4_dss_numnewpaths * sizeof (char *);
		added_paths = kmem_zalloc(sz, KM_SLEEP);
	}

	/* walk the "passed-in" rfs4_dss_newpaths array from nfsd */
	for (i = 0; i < vip->r4_dss_numnewpaths; i++) {
		int found = 0;
		char *newpath = vip->r4_dss_newpaths[i];

		dss_path = vip->r4_dss_pathlist;
		do {
			char *path = dss_path->ds_path;

			/* used only for non-HA */
			if (strcmp(path, NFS4_DSS_VAR_DIR) == 0) {
				dss_path = dss_path->ds_next;
				continue;
			}

			if (strncmp(path, newpath, strlen(path)) == 0) {
				found = 1;
				break;
			}

			dss_path = dss_path->ds_next;
		} while (dss_path != vip->r4_dss_pathlist);

		if (found == 0) {
			added_paths[numadded_paths] = newpath;
			numadded_paths++;
		}
	}

	/* did we find any added paths? */
	if (numadded_paths > 0) {
		/* create a new server instance, and start its grace period */
		start_grace = 1;
		rfs4_grace_create(rip, start_grace, numadded_paths,
		    added_paths);

		/* read in the stable storage state from these paths */
		rfs4_dss_readstate(rip, numadded_paths, added_paths);

		/*
		 * Multiple failovers during a grace period will cause
		 * clients of the same resource group to be partitioned
		 * into different server instances, with different
		 * grace periods.  Since clients of the same resource
		 * group must be subject to the same grace period,
		 * we need to reset all currently active grace periods.
		 */
		rfs4_inst_grace_reset(rip);
	}

	if (vip->r4_dss_numnewpaths > 0)
		kmem_free(added_paths,
		    vip->r4_dss_numnewpaths * sizeof (char *));
}

/*
 * Used by NFSv3 and NFSv4 server to query label of
 * a pathname component during lookup/access ops.
 */
ts_label_t *
nfs_getflabel(vnode_t *vp, struct exportinfo *exi)
{
	zone_t *zone;
	ts_label_t *zone_label;
	char *path;

	mutex_enter(&vp->v_lock);
	if (vp->v_path != NULL) {
		zone = zone_find_by_any_path(vp->v_path, B_FALSE);
		mutex_exit(&vp->v_lock);
	} else {
		/*
		 * v_path not cached. Fall back on pathname of exported
		 * file system as we rely on pathname from which we can
		 * derive a label. The exported file system portion of
		 * path is sufficient to obtain a label.
		 */
		path = exi->exi_export.ex_path;
		if (path == NULL) {
			mutex_exit(&vp->v_lock);
			return (NULL);
		}
		zone = zone_find_by_any_path(path, B_FALSE);
		mutex_exit(&vp->v_lock);
	}
	/*
	 * Caller has verified that the file is either
	 * exported or visible. So if the path falls in
	 * global zone, admin_low is returned; otherwise
	 * the zone's label is returned.
	 */
	zone_label = zone->zone_slabel;
	label_hold(zone_label);
	zone_rele(zone);
	return (zone_label);
}

/*
 * TX NFS routine used by NFSv3 and NFSv4 to do label check
 * on client label and server's file object lable.
 */
boolean_t
do_rfs_label_check(bslabel_t *clabel, vnode_t *vp, int flag,
    struct exportinfo *exi)
{
	bslabel_t *slabel;
	ts_label_t *tslabel;
	boolean_t result;

	if ((tslabel = nfs_getflabel(vp, exi)) == NULL) {
		return (B_FALSE);
	}
	slabel = label2bslabel(tslabel);
	DTRACE_PROBE4(tx__rfs__log__info__labelcheck, char *,
	    "comparing server's file label(1) with client label(2) (vp(3))",
	    bslabel_t *, slabel, bslabel_t *, clabel, vnode_t *, vp);

	if (flag == EQUALITY_CHECK)
		result = blequal(clabel, slabel);
	else
		result = bldominates(clabel, slabel);
	label_rele(tslabel);
	return (result);
}

/*
 * Callback function to return the loaned buffers.
 * Calls VOP_RETZCBUF() only after all uio_iov[]
 * buffers are returned. nu_ref maintains the count.
 */
void
rfs_free_xuio(void *free_arg)
{
	uint_t ref;
	nfs_xuio_t *nfsuiop = (nfs_xuio_t *)free_arg;

	ref = atomic_dec_uint_nv(&nfsuiop->nu_ref);

	/*
	 * Call VOP_RETZCBUF() only when all the iov buffers
	 * are sent OTW.
	 */
	if (ref != 0)
		return;

	if (((uio_t *)nfsuiop)->uio_extflg & UIO_XUIO) {
		(void) VOP_RETZCBUF(nfsuiop->nu_vp, (xuio_t *)free_arg, NULL,
		    NULL);
		VN_RELE(nfsuiop->nu_vp);
	}

	kmem_cache_free(nfs_xuio_cache, free_arg);
}

xuio_t *
rfs_setup_xuio(vnode_t *vp)
{
	nfs_xuio_t *nfsuiop;

	nfsuiop = kmem_cache_alloc(nfs_xuio_cache, KM_SLEEP);

	bzero(nfsuiop, sizeof (nfs_xuio_t));
	nfsuiop->nu_vp = vp;

	/*
	 * ref count set to 1. more may be added
	 * if multiple mblks refer to multiple iov's.
	 * This is done in uio_to_mblk().
	 */

	nfsuiop->nu_ref = 1;

	nfsuiop->nu_frtn.free_func = rfs_free_xuio;
	nfsuiop->nu_frtn.free_arg = (char *)nfsuiop;

	nfsuiop->nu_uio.xu_type = UIOTYPE_ZEROCOPY;

	return (&nfsuiop->nu_uio);
}

mblk_t *
uio_to_mblk(uio_t *uiop)
{
	struct iovec *iovp;
	int i;
	mblk_t *mp, *mp1;
	nfs_xuio_t *nfsuiop = (nfs_xuio_t *)uiop;

	if (uiop->uio_iovcnt == 0)
		return (NULL);

	iovp = uiop->uio_iov;
	mp = mp1 = esballoca((uchar_t *)iovp->iov_base, iovp->iov_len,
	    BPRI_MED, &nfsuiop->nu_frtn);
	ASSERT(mp != NULL);

	mp->b_wptr += iovp->iov_len;
	mp->b_datap->db_type = M_DATA;

	for (i = 1; i < uiop->uio_iovcnt; i++) {
		iovp = (uiop->uio_iov + i);

		mp1->b_cont = esballoca(
		    (uchar_t *)iovp->iov_base, iovp->iov_len, BPRI_MED,
		    &nfsuiop->nu_frtn);

		mp1 = mp1->b_cont;
		ASSERT(mp1 != NULL);
		mp1->b_wptr += iovp->iov_len;
		mp1->b_datap->db_type = M_DATA;
	}

	nfsuiop->nu_ref = uiop->uio_iovcnt;

	return (mp);
}

void
rfs_rndup_mblks(mblk_t *mp, uint_t len, int buf_loaned)
{
	int i, rndup;
	int alloc_err = 0;
	mblk_t *rmp;

	rndup = BYTES_PER_XDR_UNIT - (len % BYTES_PER_XDR_UNIT);

	/* single mblk_t non copy-reduction case */
	if (!buf_loaned) {
		mp->b_wptr += len;
		if (rndup != BYTES_PER_XDR_UNIT) {
			for (i = 0; i < rndup; i++)
				*mp->b_wptr++ = '\0';
		}
		return;
	}

	/* no need for extra rndup */
	if (rndup == BYTES_PER_XDR_UNIT)
		return;

	while (mp->b_cont)
		mp = mp->b_cont;

	/*
	 * In case of copy-reduction mblks, the size of the mblks
	 * are fixed and are of the size of the loaned buffers.
	 * Allocate a roundup mblk and chain it to the data
	 * buffers. This is sub-optimal, but not expected to
	 * happen in regular common workloads.
	 */

	rmp = allocb_wait(rndup, BPRI_MED, STR_NOSIG, &alloc_err);
	ASSERT(rmp != NULL);
	ASSERT(alloc_err == 0);

	for (i = 0; i < rndup; i++)
		*rmp->b_wptr++ = '\0';

	rmp->b_datap->db_type = M_DATA;
	mp->b_cont = rmp;
}
