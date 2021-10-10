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
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Functions supporting Solaris Extended Attributes,
 * used to provide access to CIFS "named streams".
 */

#include <sys/systm.h>
#include <sys/cred.h>
#include <sys/vnode.h>
#include <sys/vfs.h>
#include <sys/filio.h>
#include <sys/uio.h>
#include <sys/dirent.h>
#include <sys/errno.h>
#include <sys/sysmacros.h>
#include <sys/kmem.h>
#include <sys/stat.h>
#include <sys/cmn_err.h>
#include <sys/u8_textprep.h>

#include <smb/smb.h>
#include <netsmb/smb_conn.h>
#include <netsmb/smb_subr.h>
#include <netsmb/smb_rq.h>

#include <smbfs/smbfs.h>
#include <smbfs/smbfs_node.h>
#include <smbfs/smbfs_subr.h>

#include <fs/fs_subr.h>

/*
 * Solaris wants there to be a directory node to contain
 * all the extended attributes.  The SMB protocol does not
 * really support a directory here, and uses very different
 * operations to list attributes, etc. so we "fake up" an
 * smbnode here to represent the attributes directory.
 *
 * We need to give this (fake) directory a unique identity,
 * and since we're using the full remote pathname as the
 * unique identity of all nodes, the easiest thing to do
 * here is append a colon (:) to the given pathname.
 *
 * There are several places where smbfs_fullpath and its
 * callers must decide what separator to use when building
 * a remote path name, and the rule is now as follows:
 * 1: When no XATTR involved, use "\\" as the separator.
 * 2: Traversal into the (fake) XATTR dir adds one ":"
 * 3: Children of the XATTR dir add nothing (sep=0)
 * The result should be _one_ colon before the attr name.
 */

/* ARGSUSED */
int
smbfs_get_xattrdir(vnode_t *pvp, vnode_t **vpp, cred_t *cr, int flags)
{
	vnode_t *xvp;
	smbnode_t *pnp, *xnp;

	pnp = VTOSMB(pvp);

	/*
	 * We don't allow recursive extended attributes
	 * (xattr under xattr dir.) so the "parent" node
	 * (pnp) must NOT be an XATTR directory or file.
	 */
	if (pnp->n_flag & N_XATTR)
		return (EINVAL);

	xnp = smbfs_node_findcreate(pnp->n_mount,
	    pnp->n_rpath, pnp->n_rplen, NULL, 0, ':',
	    &smbfs_fattr0); /* force create */
	ASSERT(xnp != NULL);
	xvp = SMBTOV(xnp);
	/* Note: xvp has a VN_HOLD, which our caller expects. */

	/* If it's a new node, initialize. */
	if (xvp->v_type == VNON) {

		mutex_enter(&xvp->v_lock);
		xvp->v_type = VDIR;
		xvp->v_flag |= V_XATTRDIR;
		mutex_exit(&xvp->v_lock);

		mutex_enter(&xnp->r_statelock);
		xnp->n_flag |= N_XATTR;
		mutex_exit(&xnp->r_statelock);
	}

	/* Success! */
	*vpp = xvp;
	return (0);
}

/*
 * Find the parent of an XATTR directory or file,
 * by trimming off the ":attrname" part of rpath.
 * Called on XATTR files to get the XATTR dir, and
 * called on the XATTR dir to get the real object
 * under which the (faked up) XATTR dir lives.
 */
int
smbfs_xa_parent(vnode_t *vp, vnode_t **vpp)
{
	smbnode_t *np = VTOSMB(vp);
	smbnode_t *pnp;
	int rplen;

	*vpp = NULL;

	if ((np->n_flag & N_XATTR) == 0)
		return (EINVAL);

	if (vp->v_flag & V_XATTRDIR) {
		/*
		 * Want the parent of the XATTR directory.
		 * That's easy: just remove trailing ":"
		 */
		rplen = np->n_rplen - 1;
		if (rplen < 1) {
			SMBVDEBUG("rplen < 1?");
			return (ENOENT);
		}
		if (np->n_rpath[rplen] != ':') {
			SMBVDEBUG("last is not colon");
			return (ENOENT);
		}
	} else {
		/*
		 * Want the XATTR directory given
		 * one of its XATTR files (children).
		 * Find the ":" and trim after it.
		 */
		for (rplen = 1; rplen < np->n_rplen; rplen++)
			if (np->n_rpath[rplen] == ':')
				break;
		/* Should have found ":stream_name" */
		if (rplen >= np->n_rplen) {
			SMBVDEBUG("colon not found");
			return (ENOENT);
		}
		rplen++; /* keep the ":" */
		if (rplen >= np->n_rplen) {
			SMBVDEBUG("no stream name");
			return (ENOENT);
		}
	}

	pnp = smbfs_node_findcreate(np->n_mount,
	    np->n_rpath, rplen, NULL, 0, 0,
	    &smbfs_fattr0); /* force create */
	ASSERT(pnp != NULL);
	/* Note: have VN_HOLD from smbfs_node_findcreate */
	*vpp = SMBTOV(pnp);
	return (0);
}

/*
 * This is called by smbfs_pathconf to find out
 * if some file has any extended attributes.
 * There's no short-cut way to find out, so we
 * just list the attributes the usual way and
 * check for an empty result.
 *
 * Returns 1: (exists) or 0: (none found)
 */
int
smbfs_xa_exists(vnode_t *vp, cred_t *cr)
{
	smbnode_t *xnp;
	vnode_t *xvp;
	struct smb_cred scred;
	struct smbfs_fctx ctx;
	int error, rc = 0;

	/* Get the xattr dir */
	error = smbfs_get_xattrdir(vp, &xvp, cr, LOOKUP_XATTR);
	if (error)
		return (0);
	/* NB: have VN_HOLD on xpv */
	xnp = VTOSMB(xvp);

	smb_credinit(&scred, cr);

	bzero(&ctx, sizeof (ctx));
	ctx.f_flags = SMBFS_RDD_FINDFIRST;
	ctx.f_dnp = xnp;
	ctx.f_scred = &scred;
	ctx.f_ssp = xnp->n_mount->smi_share;

	error = smbfs_xa_findopen(&ctx, xnp, "*", 1);
	if (error)
		goto out;

	error = smbfs_xa_findnext(&ctx, 1);
	if (error)
		goto out;

	/* Have at least one named stream. */
	SMBVDEBUG("ctx.f_name: %s\n", ctx.f_name);
	rc = 1;

out:
	/* NB: Always call findclose, error or not. */
	(void) smbfs_xa_findclose(&ctx);
	smb_credrele(&scred);
	VN_RELE(xvp);
	return (rc);
}


/*
 * This is called to get attributes (size, etc.) of either
 * the "faked up" XATTR directory or a named stream.
 */
int
smbfs_xa_getfattr(struct smbnode *xnp, struct smbfattr *fap,
	struct smb_cred *scrp)
{
	vnode_t *xvp;	/* xattr */
	vnode_t *pvp;	/* parent */
	smbnode_t *pnp;	/* parent */
	int error, nlen;
	const char *name, *sname;

	xvp = SMBTOV(xnp);

	/*
	 * Simulate smbfs_smb_getfattr() for a named stream.
	 * OK to leave a,c,m times zero (expected w/ XATTR).
	 * The XATTR directory is easy (all fake).
	 */
	if (xvp->v_flag & V_XATTRDIR) {
		fap->fa_attr = FILE_ATTRIBUTE_DIRECTORY;
		fap->fa_size = DEV_BSIZE;
		return (0);
	}

	/*
	 * Do a lookup in the XATTR directory,
	 * using the stream name (last part)
	 * from the xattr node.
	 */
	error = smbfs_xa_parent(xvp, &pvp);
	if (error)
		return (error);
	/* Note: pvp has a VN_HOLD */
	pnp = VTOSMB(pvp);

	/* Get stream name (ptr and length) */
	ASSERT(xnp->n_rplen > pnp->n_rplen);
	nlen = xnp->n_rplen - pnp->n_rplen;
	name = xnp->n_rpath + pnp->n_rplen;
	sname = name;

	/* Note: this can allocate a new "name" */
	error = smbfs_smb_lookup(pnp, &name, &nlen, fap, scrp);
	if (error == 0 && name != sname)
		smbfs_name_free(name, nlen);

	VN_RELE(pvp);

	return (error);
}

/*
 * Fetch the entire attribute list here in findopen.
 * Will parse the results in findnext.
 *
 * This is called on the XATTR directory, so we
 * have to get the (real) parent object first.
 */
/* ARGSUSED */
int
smbfs_xa_findopen(struct smbfs_fctx *ctx, struct smbnode *dnp,
	const char *wildcard, int wclen)
{
	vnode_t *pvp;	/* parent */
	smbnode_t *pnp;
	struct smb_t2rq *t2p;
	struct smb_vc *vcp = SSTOVC(ctx->f_ssp);
	struct mbchain *mbp;
	int error;

	ASSERT(dnp->n_flag & N_XATTR);

	ctx->f_type = ft_XA;
	ctx->f_namesz = SMB_MAXFNAMELEN + 1;
	if (SMB_UNICODE_STRINGS(SSTOVC(ctx->f_ssp)))
		ctx->f_namesz *= 2;
	ctx->f_name = kmem_alloc(ctx->f_namesz, KM_SLEEP);

	error = smbfs_xa_parent(SMBTOV(dnp), &pvp);
	if (error)
		return (error);
	ASSERT(pvp);
	/* Note: pvp has a VN_HOLD */
	pnp = VTOSMB(pvp);

	if (ctx->f_t2) {
		smb_t2_done(ctx->f_t2);
		ctx->f_t2 = NULL;
	}

	error = smb_t2_alloc(SSTOCP(ctx->f_ssp),
	    TRANS2_QUERY_PATH_INFORMATION,
	    ctx->f_scred, &t2p);
	if (error)
		goto out;
	ctx->f_t2 = t2p;

	mbp = &t2p->t2_tparam;
	(void) mb_init(mbp);
	(void) mb_put_uint16le(mbp, SMB_QUERY_FILE_STREAM_INFO);
	(void) mb_put_uint32le(mbp, 0);
	error = smbfs_fullpath(mbp, vcp, pnp, NULL, NULL, 0);
	if (error)
		goto out;
	t2p->t2_maxpcount = 2;
	t2p->t2_maxdcount = INT16_MAX;
	error = smb_t2_request(t2p);
	if (error) {
		if (t2p->t2_sr_error == NT_STATUS_INVALID_PARAMETER)
			error = ENOTSUP;
	}
	/*
	 * No returned parameters to parse.
	 * Returned data are in t2_rdata,
	 * which we'll parse in _findnext.
	 * However, save the wildcard.
	 */
	ctx->f_wildcard = wildcard;
	ctx->f_wclen = wclen;

out:
	VN_RELE(pvp);
	return (error);
}

/*
 * Get the next name in an XATTR directory into f_name
 */
/* ARGSUSED */
int
smbfs_xa_findnext(struct smbfs_fctx *ctx, uint16_t limit)
{
	struct mdchain *mdp;
	struct smb_t2rq *t2p;
	uint32_t size, next;
	uint64_t llongint;
	int error, skip, used, nmlen;

	t2p = ctx->f_t2;
	mdp = &t2p->t2_rdata;

	if (ctx->f_flags & SMBFS_RDD_FINDSINGLE) {
		ASSERT(ctx->f_wildcard);
		SMBVDEBUG("wildcard: %s\n", ctx->f_wildcard);
	}

again:
	if (ctx->f_flags & SMBFS_RDD_EOF)
		return (ENOENT);

	/* Parse FILE_STREAM_INFORMATION */
	if ((error = md_get_uint32le(mdp, &next)) != 0)	/* offset to */
		return (ENOENT);
	if ((error = md_get_uint32le(mdp, &size)) != 0) /* name len */
		return (ENOENT);
	(void) md_get_uint64le(mdp, &llongint); /* file size */
	ctx->f_attr.fa_size = llongint;
	(void) md_get_uint64le(mdp, NULL);	/* alloc. size */
	used = 4 + 4 + 8 + 8;	/* how much we consumed */

	/*
	 * Copy the string, but skip the first char (":")
	 * Watch out for zero-length strings here.
	 */
	if (SMB_UNICODE_STRINGS(SSTOVC(ctx->f_ssp))) {
		if (size >= 2) {
			size -= 2; used += 2;
			(void) md_get_uint16le(mdp, NULL);
		}
		nmlen = min(size, SMB_MAXFNAMELEN * 2);
	} else {
		if (size >= 1) {
			size -= 1; used += 1;
			(void) md_get_uint8(mdp, NULL);
		}
		nmlen = min(size, SMB_MAXFNAMELEN);
	}

	ASSERT(nmlen < ctx->f_namesz);
	ctx->f_nmlen = nmlen;
	error = md_get_mem(mdp, ctx->f_name, nmlen, MB_MSYSTEM);
	if (error)
		return (error);
	used += nmlen;

	/*
	 * Convert UCS-2 to UTF-8
	 */
	smbfs_fname_tolocal(ctx);
	if (nmlen)
		SMBVDEBUG("name: %s\n", ctx->f_name);
	else
		SMBVDEBUG("null name!\n");

	/*
	 * Skip padding until next offset
	 */
	if (next > used) {
		skip = next - used;
		(void) md_get_mem(mdp, NULL, skip, MB_MSYSTEM);
	}
	if (next == 0)
		ctx->f_flags |= SMBFS_RDD_EOF;

	/*
	 * Chop off the trailing ":$DATA"
	 * The 6 here is strlen(":$DATA")
	 */
	if (ctx->f_nmlen >= 6) {
		char *p = ctx->f_name + ctx->f_nmlen - 6;
		if (strncmp(p, ":$DATA", 6) == 0) {
			*p = '\0'; /* Chop! */
			ctx->f_nmlen -= 6;
		}
	}

	/*
	 * The Chop above will typically leave
	 * an empty name in the first slot,
	 * which we will skip here.
	 */
	if (ctx->f_nmlen == 0)
		goto again;

	/*
	 * If this is a lookup of a specific name,
	 * skip past any non-matching names.
	 */
	if (ctx->f_flags & SMBFS_RDD_FINDSINGLE) {
		if (ctx->f_wclen != ctx->f_nmlen)
			goto again;
		if (u8_strcmp(ctx->f_wildcard, ctx->f_name,
		    ctx->f_nmlen, U8_STRCMP_CI_LOWER,
		    U8_UNICODE_LATEST, &error) || error)
			goto again;
	}

	return (0);
}

/*
 * Find first/next/close for XATTR directories.
 * NB: also used by smbfs_smb_lookup
 */

int
smbfs_xa_findclose(struct smbfs_fctx *ctx)
{

	if (ctx->f_name)
		kmem_free(ctx->f_name, ctx->f_namesz);
	if (ctx->f_t2)
		smb_t2_done(ctx->f_t2);

	return (0);
}
