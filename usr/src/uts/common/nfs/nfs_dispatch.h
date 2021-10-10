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

/*
 *	Copyright (c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *	All rights reserved.
 *	Use is subject to license terms.
 */


#ifndef _NFS_DISPATCH_H
#define	_NFS_DISPATCH_H

#ifdef	__cplusplus
extern "C" {
#endif


/*
 * RPC dispatch table
 * Indexed by version, proc
 */

typedef struct rpcdisp {
	void	  (*dis_proc)();	/* proc to call */
	xdrproc_t dis_xdrargs;		/* xdr routine to get args */
	xdrproc_t dis_fastxdrargs;	/* `fast' xdr routine to get args */
	int	  dis_argsz;		/* sizeof args */
	xdrproc_t dis_xdrres;		/* xdr routine to put results */
	xdrproc_t dis_fastxdrres;	/* `fast' xdr routine to put results */
	int	  dis_ressz;		/* size of results */
	void	  (*dis_resfree)();	/* frees space allocated by proc */
	int	  dis_flags;		/* flags, see below */
	void	  *(*dis_getfh)();	/* returns the fhandle for the req */
} rpcdisp_t;

#define	RPC_IDEMPOTENT	0x1	/* idempotent or not */
/*
 * Be very careful about which NFS procedures get the RPC_ALLOWANON bit.
 * Right now, it this bit is on, we ignore the results of per NFS request
 * access control.
 */
#define	RPC_ALLOWANON	0x2	/* allow anonymous access */
#define	RPC_MAPRESP	0x4	/* use mapped response buffer */
#define	RPC_AVOIDWORK	0x8	/* do work avoidance for dups */
#define	RPC_PUBLICFH_OK	0x10	/* allow use of public filehandle */

typedef struct rpc_disptable {
	int dis_nprocs;
	char **dis_procnames;
	struct rpcdisp *dis_table;
} rpc_disptable_t;

void	rpc_null(caddr_t *, caddr_t *);

union rfs_args {
	/*
	 * NFS VERSION 2
	 */

	/* RFS_NULL = 0 */

	/* RFS_GETATTR = 1 */
	fhandle_t nfs2_getattr_args;

	/* RFS_SETATTR = 2 */
	struct nfssaargs nfs2_setattr_args;

	/* RFS_ROOT = 3 *** NO LONGER SUPPORTED *** */

	/* RFS_LOOKUP = 4 */
	struct nfsdiropargs nfs2_lookup_args;

	/* RFS_READLINK = 5 */
	fhandle_t nfs2_readlink_args;

	/* RFS_READ = 6 */
	struct nfsreadargs nfs2_read_args;

	/* RFS_WRITECACHE = 7 *** NO LONGER SUPPORTED *** */

	/* RFS_WRITE = 8 */
	struct nfswriteargs nfs2_write_args;

	/* RFS_CREATE = 9 */
	struct nfscreatargs nfs2_create_args;

	/* RFS_REMOVE = 10 */
	struct nfsdiropargs nfs2_remove_args;

	/* RFS_RENAME = 11 */
	struct nfsrnmargs nfs2_rename_args;

	/* RFS_LINK = 12 */
	struct nfslinkargs nfs2_link_args;

	/* RFS_SYMLINK = 13 */
	struct nfsslargs nfs2_symlink_args;

	/* RFS_MKDIR = 14 */
	struct nfscreatargs nfs2_mkdir_args;

	/* RFS_RMDIR = 15 */
	struct nfsdiropargs nfs2_rmdir_args;

	/* RFS_READDIR = 16 */
	struct nfsrddirargs nfs2_readdir_args;

	/* RFS_STATFS = 17 */
	fhandle_t nfs2_statfs_args;

	/*
	 * NFS VERSION 3
	 */

	/* RFS_NULL = 0 */

	/* RFS3_GETATTR = 1 */
	GETATTR3args nfs3_getattr_args;

	/* RFS3_SETATTR = 2 */
	SETATTR3args nfs3_setattr_args;

	/* RFS3_LOOKUP = 3 */
	LOOKUP3args nfs3_lookup_args;

	/* RFS3_ACCESS = 4 */
	ACCESS3args nfs3_access_args;

	/* RFS3_READLINK = 5 */
	READLINK3args nfs3_readlink_args;

	/* RFS3_READ = 6 */
	READ3args nfs3_read_args;

	/* RFS3_WRITE = 7 */
	WRITE3args nfs3_write_args;

	/* RFS3_CREATE = 8 */
	CREATE3args nfs3_create_args;

	/* RFS3_MKDIR = 9 */
	MKDIR3args nfs3_mkdir_args;

	/* RFS3_SYMLINK = 10 */
	SYMLINK3args nfs3_symlink_args;

	/* RFS3_MKNOD = 11 */
	MKNOD3args nfs3_mknod_args;

	/* RFS3_REMOVE = 12 */
	REMOVE3args nfs3_remove_args;

	/* RFS3_RMDIR = 13 */
	RMDIR3args nfs3_rmdir_args;

	/* RFS3_RENAME = 14 */
	RENAME3args nfs3_rename_args;

	/* RFS3_LINK = 15 */
	LINK3args nfs3_link_args;

	/* RFS3_READDIR = 16 */
	READDIR3args nfs3_readdir_args;

	/* RFS3_READDIRPLUS = 17 */
	READDIRPLUS3args nfs3_readdirplus_args;

	/* RFS3_FSSTAT = 18 */
	FSSTAT3args nfs3_fsstat_args;

	/* RFS3_FSINFO = 19 */
	FSINFO3args nfs3_fsinfo_args;

	/* RFS3_PATHCONF = 20 */
	PATHCONF3args nfs3_pathconf_args;

	/* RFS3_COMMIT = 21 */
	COMMIT3args nfs3_commit_args;

	/*
	 * NFS VERSION 4
	 */

	/* RFS_NULL = 0 */

	/* COMPOUND = 1 */
	COMPOUND4args nfs4_compound_args;
};

union rfs_res {
	/*
	 * NFS VERSION 2
	 */

	/* RFS_NULL = 0 */

	/* RFS_GETATTR = 1 */
	struct nfsattrstat nfs2_getattr_res;

	/* RFS_SETATTR = 2 */
	struct nfsattrstat nfs2_setattr_res;

	/* RFS_ROOT = 3 *** NO LONGER SUPPORTED *** */

	/* RFS_LOOKUP = 4 */
	struct nfsdiropres nfs2_lookup_res;

	/* RFS_READLINK = 5 */
	struct nfsrdlnres nfs2_readlink_res;

	/* RFS_READ = 6 */
	struct nfsrdresult nfs2_read_res;

	/* RFS_WRITECACHE = 7 *** NO LONGER SUPPORTED *** */

	/* RFS_WRITE = 8 */
	struct nfsattrstat nfs2_write_res;

	/* RFS_CREATE = 9 */
	struct nfsdiropres nfs2_create_res;

	/* RFS_REMOVE = 10 */
	enum nfsstat nfs2_remove_res;

	/* RFS_RENAME = 11 */
	enum nfsstat nfs2_rename_res;

	/* RFS_LINK = 12 */
	enum nfsstat nfs2_link_res;

	/* RFS_SYMLINK = 13 */
	enum nfsstat nfs2_symlink_res;

	/* RFS_MKDIR = 14 */
	struct nfsdiropres nfs2_mkdir_res;

	/* RFS_RMDIR = 15 */
	enum nfsstat nfs2_rmdir_res;

	/* RFS_READDIR = 16 */
	struct nfsrddirres nfs2_readdir_res;

	/* RFS_STATFS = 17 */
	struct nfsstatfs nfs2_statfs_res;

	/*
	 * NFS VERSION 3
	 */

	/* RFS_NULL = 0 */

	/* RFS3_GETATTR = 1 */
	GETATTR3res nfs3_getattr_res;

	/* RFS3_SETATTR = 2 */
	SETATTR3res nfs3_setattr_res;

	/* RFS3_LOOKUP = 3 */
	LOOKUP3res nfs3_lookup_res;

	/* RFS3_ACCESS = 4 */
	ACCESS3res nfs3_access_res;

	/* RFS3_READLINK = 5 */
	READLINK3res nfs3_readlink_res;

	/* RFS3_READ = 6 */
	READ3res nfs3_read_res;

	/* RFS3_WRITE = 7 */
	WRITE3res nfs3_write_res;

	/* RFS3_CREATE = 8 */
	CREATE3res nfs3_create_res;

	/* RFS3_MKDIR = 9 */
	MKDIR3res nfs3_mkdir_res;

	/* RFS3_SYMLINK = 10 */
	SYMLINK3res nfs3_symlink_res;

	/* RFS3_MKNOD = 11 */
	MKNOD3res nfs3_mknod_res;

	/* RFS3_REMOVE = 12 */
	REMOVE3res nfs3_remove_res;

	/* RFS3_RMDIR = 13 */
	RMDIR3res nfs3_rmdir_res;

	/* RFS3_RENAME = 14 */
	RENAME3res nfs3_rename_res;

	/* RFS3_LINK = 15 */
	LINK3res nfs3_link_res;

	/* RFS3_READDIR = 16 */
	READDIR3res nfs3_readdir_res;

	/* RFS3_READDIRPLUS = 17 */
	READDIRPLUS3res nfs3_readdirplus_res;

	/* RFS3_FSSTAT = 18 */
	FSSTAT3res nfs3_fsstat_res;

	/* RFS3_FSINFO = 19 */
	FSINFO3res nfs3_fsinfo_res;

	/* RFS3_PATHCONF = 20 */
	PATHCONF3res nfs3_pathconf_res;

	/* RFS3_COMMIT = 21 */
	COMMIT3res nfs3_commit_res;

	/*
	 * NFS VERSION 4
	 */

	/* RFS_NULL = 0 */

	/* RFS4_COMPOUND = 1 */
	COMPOUND4res nfs4_compound_res;

};

union acl_args {
	/*
	 * ACL VERSION 2
	 */

	/* ACL2_NULL = 0 */

	/* ACL2_GETACL = 1 */
	GETACL2args acl2_getacl_args;

	/* ACL2_SETACL = 2 */
	SETACL2args acl2_setacl_args;

	/* ACL2_GETATTR = 3 */
	GETATTR2args acl2_getattr_args;

	/* ACL2_ACCESS = 4 */
	ACCESS2args acl2_access_args;

	/* ACL2_GETXATTRDIR = 5 */
	GETXATTRDIR2args acl2_getxattrdir_args;

	/*
	 * ACL VERSION 3
	 */

	/* ACL3_NULL = 0 */

	/* ACL3_GETACL = 1 */
	GETACL3args acl3_getacl_args;

	/* ACL3_SETACL = 2 */
	SETACL3args acl3_setacl;

	/* ACL3_GETXATTRDIR = 3 */
	GETXATTRDIR3args acl3_getxattrdir_args;

};

union acl_res {
	/*
	 * ACL VERSION 2
	 */

	/* ACL2_NULL = 0 */

	/* ACL2_GETACL = 1 */
	GETACL2res acl2_getacl_res;

	/* ACL2_SETACL = 2 */
	SETACL2res acl2_setacl_res;

	/* ACL2_GETATTR = 3 */
	GETATTR2res acl2_getattr_res;

	/* ACL2_ACCESS = 4 */
	ACCESS2res acl2_access_res;

	/* ACL2_GETXATTRDIR = 5 */
	GETXATTRDIR2args acl2_getxattrdir_res;

	/*
	 * ACL VERSION 3
	 */

	/* ACL3_NULL = 0 */

	/* ACL3_GETACL = 1 */
	GETACL3res acl3_getacl_res;

	/* ACL3_SETACL = 2 */
	SETACL3res acl3_setacl_res;

	/* ACL3_GETXATTRDIR = 3 */
	GETXATTRDIR3res acl3_getxattrdir_res;

};

/*
 * This structure encapsulates information required
 * to send async sendreply().
 */
typedef struct rfs_disp {
	int drstat;
	struct svc_req req;
	struct rpcdisp *disp;
	struct exportinfo *exi;
	union {
		union rfs_args ra;
		union acl_args aa;
	} args_buf;
	union {
		union rfs_res rr;
		union acl_res ar;
	} res_buf;
	/* private data specific to different versions */
	void *pdata;
} rfs_disp_t;

#ifdef	__cplusplus
}
#endif

#endif /* _NFS_DISPATCH_H */
