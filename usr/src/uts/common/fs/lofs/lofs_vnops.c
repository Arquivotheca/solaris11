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
 * Copyright (c) 1991, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/vnode.h>
#include <sys/vfs.h>
#include <sys/vfs_opreg.h>
#include <sys/uio.h>
#include <sys/cred.h>
#include <sys/pathname.h>
#include <sys/debug.h>
#include <sys/fs/lofs_node.h>
#include <sys/fs/lofs_info.h>
#include <fs/fs_subr.h>
#include <vm/as.h>
#include <vm/seg.h>

#define	VTOLI(vp)	(vtoli(vp->v_vfsp));

/*
 * These are the vnode ops routines which implement the vnode interface to
 * the looped-back file system.  These routines just take their parameters,
 * and then calling the appropriate real vnode routine(s) to do the work.
 */

static int
lo_open(vnode_t **vpp, int flag, struct cred *cr, caller_context_t *ct)
{
	vnode_t *vp = *vpp;
	struct loinfo *li = VTOLI(vp);
	vnode_t *rvp;
	vnode_t *oldvp;
	int error;

	LOFS_ENTER(li);

	oldvp = vp;
	vp = rvp = realvp(vp);
	/*
	 * Need to hold new reference to vp since VOP_OPEN() may
	 * decide to release it.
	 */
	VN_HOLD(vp);
	error = VOP_OPEN(&rvp, flag, cr, ct);

	if (!error && rvp != vp) {
		/*
		 * the FS which we called should have released the
		 * new reference on vp
		 */
		*vpp = makelonode(rvp, li, 0);
		if ((*vpp)->v_type == VDIR) {
			/*
			 * Copy over any looping flags to the new lnode.
			 */
			(vtol(*vpp))->lo_looping |= (vtol(oldvp))->lo_looping;
		}
		if (IS_DEVVP(*vpp)) {
			vnode_t *svp;

			svp = specvp(*vpp, (*vpp)->v_rdev, (*vpp)->v_type, cr);
			VN_RELE(*vpp);
			if (svp == NULL)
				error = ENOSYS;
			else
				*vpp = svp;
		}
		VN_RELE(oldvp);
	} else {
		ASSERT(rvp->v_count > 1);
		VN_RELE(rvp);
	}

	LOFS_EXIT(li);
	return (error);
}

static int
lo_close(
	vnode_t *vp,
	int flag,
	int count,
	offset_t offset,
	struct cred *cr,
	caller_context_t *ct)
{
	struct loinfo *li = VTOLI(vp);
	int ret;

	LOFS_ENTER(li);
	vp = realvp(vp);
	ret = VOP_CLOSE(vp, flag, count, offset, cr, ct);
	LOFS_EXIT(li);

	return (ret);
}

static int
lo_read(vnode_t *vp, struct uio *uiop, int ioflag, struct cred *cr,
	caller_context_t *ct)
{
	struct loinfo *li = VTOLI(vp);
	int ret;

	LOFS_ENTER(li);
	vp = realvp(vp);
	ret = VOP_READ(vp, uiop, ioflag, cr, ct);
	LOFS_EXIT(li);

	return (ret);
}

static int
lo_write(vnode_t *vp, struct uio *uiop, int ioflag, struct cred *cr,
	caller_context_t *ct)
{
	struct loinfo *li = VTOLI(vp);
	int ret;

	LOFS_ENTER(li);
	vp = realvp(vp);
	ret = VOP_WRITE(vp, uiop, ioflag, cr, ct);
	LOFS_EXIT(li);

	return (ret);
}

static int
lo_ioctl(
	vnode_t *vp,
	int cmd,
	intptr_t arg,
	int flag,
	struct cred *cr,
	int *rvalp,
	caller_context_t *ct)
{
	struct loinfo *li = VTOLI(vp);
	int ret;

	LOFS_ENTER(li);
	vp = realvp(vp);
	ret = VOP_IOCTL(vp, cmd, arg, flag, cr, rvalp, ct);
	LOFS_EXIT(li);

	return (ret);
}

static int
lo_setfl(vnode_t *vp, int oflags, int nflags, cred_t *cr, caller_context_t *ct)
{
	struct loinfo *li = VTOLI(vp);
	int ret;

	LOFS_ENTER(li);
	vp = realvp(vp);
	ret = VOP_SETFL(vp, oflags, nflags, cr, ct);
	LOFS_EXIT(li);

	return (ret);
}

static int
lo_getattr(
	vnode_t *vp,
	struct vattr *vap,
	int flags,
	struct cred *cr,
	caller_context_t *ct)
{
	struct loinfo *li = VTOLI(vp);
	int ret;

	LOFS_ENTER(li);
	vp = realvp(vp);
	ret = VOP_GETATTR(vp, vap, flags, cr, ct);
	LOFS_EXIT(li);

	return (ret);
}

static int
lo_setattr(
	vnode_t *vp,
	struct vattr *vap,
	int flags,
	struct cred *cr,
	caller_context_t *ct)
{
	struct loinfo *li = VTOLI(vp);
	int ret;

	LOFS_ENTER(li);
	vp = realvp(vp);
	ret = VOP_SETATTR(vp, vap, flags, cr, ct);
	LOFS_EXIT(li);

	return (ret);
}

static int
lo_access(
	vnode_t *vp,
	int mode,
	int flags,
	struct cred *cr,
	caller_context_t *ct)
{
	struct loinfo *li = VTOLI(vp);
	int ret;

	LOFS_ENTER(li);
	if (mode & VWRITE) {
		if ((vp->v_type == VREG || vp->v_type == VDIR) &&
		    vn_is_readonly(vp)) {
			LOFS_EXIT(li);
			return (EROFS);
		}
	}
	vp = realvp(vp);
	ret = VOP_ACCESS(vp, mode, flags, cr, ct);
	LOFS_EXIT(li);

	return (ret);
}

static int
lo_fsync(vnode_t *vp, int syncflag, struct cred *cr, caller_context_t *ct)
{
	struct loinfo *li = VTOLI(vp);
	int ret;

	LOFS_ENTER(li);
	vp = realvp(vp);
	ret = VOP_FSYNC(vp, syncflag, cr, ct);
	LOFS_EXIT(li);

	return (ret);
}

/*ARGSUSED*/
static void
lo_inactive(vnode_t *vp, struct cred *cr, caller_context_t *ct)
{
	freelonode(vtol(vp));
}

/* ARGSUSED */
static int
lo_fid(vnode_t *vp, struct fid *fidp, caller_context_t *ct)
{
	struct loinfo *li = VTOLI(vp);
	int ret;

	LOFS_ENTER(li);
	vp = realvp(vp);
	ret = VOP_FID(vp, fidp, ct);
	LOFS_EXIT(li);

	return (ret);
}

/*
 * Given a vnode of lofs type, lookup nm name and
 * return a shadow vnode (of lofs type) of the
 * real vnode found.
 *
 * Due to the nature of lofs, there is a potential
 * looping in path traversal.
 *
 * starting from the mount point of an lofs;
 * a loop is defined to be a traversal path
 * where the mount point or the real vnode of
 * the root of this lofs is encountered twice.
 * Once at the start of traversal and second
 * when the looping is found.
 *
 * When a loop is encountered, a shadow of the
 * covered vnode is returned to stop the looping.
 *
 * This normally works, but with the advent of
 * the new automounter, returning the shadow of the
 * covered vnode (autonode, in this case) does not
 * stop the loop.  Because further lookup on this
 * lonode will cause the autonode to call lo_lookup()
 * on the lonode covering it.
 *
 * example "/net/jurassic/net/jurassic" is a loop.
 * returning the shadow of the autonode corresponding to
 * "/net/jurassic/net/jurassic" will not terminate the
 * loop.   To solve this problem we allow the loop to go
 * through one more level component lookup.  Whichever
 * directory is then looked up in "/net/jurassic/net/jurassic"
 * the vnode returned is the vnode covered by the autonode
 * "net" and this will terminate the loop.
 *
 * Lookup for dot dot has to be dealt with separately.
 * It will be nice to have a "one size fits all" kind
 * of solution, so that we don't have so many ifs statement
 * in the lo_lookup() to handle dotdot.  But, since
 * there are so many special cases to handle different
 * kinds looping above, we need special codes to handle
 * dotdot lookup as well.
 */
static int
lo_lookup(
	vnode_t *dvp,
	char *nm,
	vnode_t **vpp,
	struct pathname *pnp,
	int flags,
	vnode_t *rdir,
	struct cred *cr,
	caller_context_t *ct,
	int *direntflags,
	pathname_t *realpnp)
{
	vnode_t *vp = NULL, *tvp = NULL, *nonlovp;
	int error, is_indirectloop;
	vnode_t *realdvp = realvp(dvp);
	struct loinfo *li = VTOLI(dvp);
	int looping = 0;
	int autoloop = 0;
	int doingdotdot = 0;
	int nosub = 0;
	int mkflag = 0;

	LOFS_ENTER(li);

	/*
	 * If name is empty and no XATTR flags are set, then return
	 * dvp (empty name == lookup ".").  If an XATTR flag is set
	 * then we need to call VOP_LOOKUP to get the xattr dir.
	 */
	if (nm[0] == '\0' && ! (flags & (CREATE_XATTR_DIR|LOOKUP_XATTR))) {
		VN_HOLD(dvp);
		*vpp = dvp;
		goto success;
	}

	if (nm[0] == '.' && nm[1] == '.' && nm[2] == '\0') {
		doingdotdot++;
		/*
		 * Handle ".." out of mounted filesystem.
		 *
		 * vn_under() does VN_RELE & VN_HOLD as it traverses root vnodes
		 * of stacked vfs's. Therefore we need to call VN_HOLD on
		 * realdvp before calling vn_under() and VN_RELE later when
		 * realdvp isn't used anymore.
		 */
		VN_HOLD(realdvp);
		realdvp = vn_under(realdvp);
	}

	*vpp = NULL;	/* default(error) case */

	/*
	 * Do the normal lookup
	 */
	if (error = VOP_LOOKUP(realdvp, nm, &vp, pnp, flags, rdir, cr,
	    ct, direntflags, realpnp)) {
		if (doingdotdot)
			VN_RELE(realdvp);
		vp = NULL;
		goto out;
	}

	/*
	 * We do this check here to avoid returning a stale file handle to the
	 * caller.
	 */
	if (nm[0] == '.' && nm[1] == '\0') {
		ASSERT(!doingdotdot);
		ASSERT(vp == realdvp);
		VN_HOLD(dvp);
		VN_RELE(vp);
		*vpp = dvp;
		goto success;
	}

	if (doingdotdot) {
		if ((vtol(dvp))->lo_looping & LO_LOOPING) {
			vfs_t *vfsp;

			error = vn_vfsrlock_wait(realdvp);
			if (error) {
				VN_RELE(realdvp);
				goto out;
			}
			vfsp = vn_mountedvfs(realdvp);
			/*
			 * In the standard case if the looping flag is set and
			 * performing dotdot we would be returning from a
			 * covered vnode, implying vfsp could not be null. The
			 * exceptions being if we have looping and overlay
			 * mounts or looping and covered file systems.
			 */
			if (vfsp == NULL) {
				/*
				 * Overlay mount or covered file system,
				 * so just make the shadow node.
				 */
				vn_vfsunlock(realdvp);
				VN_RELE(realdvp);
				*vpp = makelonode(vp, li, 0);
				(vtol(*vpp))->lo_looping |= LO_LOOPING;
				goto success;
			}
			/*
			 * When looping get the actual found vnode
			 * instead of the vnode covered.
			 * Here we have to hold the lock for realdvp
			 * since an unmount during the traversal to the
			 * root vnode would turn *vfsp into garbage
			 * which would be fatal.
			 */
			error = VFS_ROOT(vfsp, &tvp);
			vn_vfsunlock(realdvp);
			VN_RELE(realdvp);

			if (error)
				goto out;

			if ((tvp == li->li_rootvp) && (vp == realvp(tvp))) {
				/*
				 * we're back at the real vnode
				 * of the rootvp
				 *
				 * return the rootvp
				 * Ex: /mnt/mnt/..
				 * where / has been lofs-mounted
				 * onto /mnt.  Return the lofs
				 * node mounted at /mnt.
				 */
				*vpp = tvp;
				VN_RELE(vp);
				goto success;
			} else {
				/*
				 * We are returning from a covered
				 * node whose vfs_mountedhere is
				 * not pointing to vfs of the current
				 * root vnode.
				 * This is a condn where in we
				 * returned a covered node say Zc
				 * but Zc is not the cover of current
				 * root.
				 * i.e.., if X is the root vnode
				 * lookup(Zc,"..") is taking us to
				 * X.
				 * Ex: /net/X/net/X/Y
				 *
				 * If LO_AUTOLOOP (autofs/lofs looping detected)
				 * has been set then we are encountering the
				 * cover of Y (Y being any directory vnode
				 * under /net/X/net/X/).
				 * When performing a dotdot set the
				 * returned vp to the vnode covered
				 * by the mounted lofs, ie /net/X/net/X
				 */
				VN_RELE(tvp);
				if ((vtol(dvp))->lo_looping & LO_AUTOLOOP) {
					VN_RELE(vp);
					vp = li->li_rootvp;
					vp = vp->v_vfsp->vfs_vnodecovered;
					VN_HOLD(vp);
					*vpp = makelonode(vp, li, 0);
					(vtol(*vpp))->lo_looping |= LO_LOOPING;
					goto success;
				}
			}
		} else {
			/*
			 * No frills just make the shadow node.
			 */
			*vpp = makelonode(vp, li, 0);
			VN_RELE(realdvp); /* kryl */
			goto success;	/* kryl had return(0) */
		}
	}

	nosub = (li->li_flag & LO_NOSUB);

	/*
	 * If this vnode is mounted on, then we
	 * traverse to the vnode which is the root of
	 * the mounted file system.
	 */
	if (!nosub && (error = traverse(&vp)))
		goto out;

	/*
	 * Make a lnode for the real vnode.
	 */
	if (vp->v_type != VDIR || nosub) {
		*vpp = makelonode(vp, li, 0);
		if (IS_DEVVP(*vpp)) {
			vnode_t *svp;

			svp = specvp(*vpp, (*vpp)->v_rdev, (*vpp)->v_type, cr);
			VN_RELE(*vpp);
			if (svp == NULL)
				error = ENOSYS;
			else
				*vpp = svp;
		}
		goto error;
	}

	/*
	 * if the found vnode (vp) is not of type lofs
	 * then we're just going to make a shadow of that
	 * vp and get out.
	 *
	 * If the found vnode (vp) is of lofs type, and
	 * we're not doing dotdot, check if we are
	 * looping.
	 */
	if (!doingdotdot && vfs_matchops(vp->v_vfsp, lo_vfsops)) {
		/*
		 * Check if we're looping, i.e.
		 * vp equals the root vp of the lofs, directly
		 * or indirectly, return the covered node.
		 */

		if (!((vtol(dvp))->lo_looping & LO_LOOPING)) {
			if (vp == li->li_rootvp) {
				/*
				 * Direct looping condn.
				 * Ex:- X is / mounted directory so lookup of
				 * /X/X is a direct looping condn.
				 */
				tvp = vp;
				vp = vp->v_vfsp->vfs_vnodecovered;
				VN_HOLD(vp);
				VN_RELE(tvp);
				looping++;
			} else {
				/*
				 * Indirect looping can be defined as
				 * real lookup returning rootvp of the current
				 * tree in any level of recursion.
				 *
				 * This check is useful if there are multiple
				 * levels of lofs indirections. Suppose vnode X
				 * in the current lookup has as its real vnode
				 * another lofs node. Y = realvp(X) Y should be
				 * a lofs node for the check to continue or Y
				 * is not the rootvp of X.
				 * Ex:- say X and Y are two vnodes
				 * say real(Y) is X and real(X) is Z
				 * parent vnode for X and Y is Z
				 * lookup(Y,"path") say we are looking for Y
				 * again under Y and we have to return Yc.
				 * but the lookup of Y under Y doesnot return
				 * Y the root vnode again here is why.
				 * 1. lookup(Y,"path of Y") will go to
				 * 2. lookup(real(Y),"path of Y") and then to
				 * 3. lookup(real(X),"path of Y").
				 * and now what lookup level 1 sees is the
				 * outcome of 2 but the vnode Y is due to
				 * lookup(Z,"path of Y") so we have to skip
				 * intermediate levels to find if in any level
				 * there is a looping.
				 */
				is_indirectloop = 0;
				nonlovp = vp;
				while (
				    vfs_matchops(nonlovp->v_vfsp, lo_vfsops) &&
				    !(is_indirectloop)) {
					if (li->li_rootvp  == nonlovp) {
						is_indirectloop++;
						break;
					}
					nonlovp = realvp(nonlovp);
				}

				if (is_indirectloop) {
					VN_RELE(vp);
					vp = nonlovp;
					vp = vp->v_vfsp->vfs_vnodecovered;
					VN_HOLD(vp);
					looping++;
				}
			}
		} else {
			/*
			 * come here only because of the interaction between
			 * the autofs and lofs.
			 *
			 * Lookup of "/net/X/net/X" will return a shadow of
			 * an autonode X_a which we call X_l.
			 *
			 * Lookup of anything under X_l, will trigger a call to
			 * auto_lookup(X_a,nm) which will eventually call
			 * lo_lookup(X_lr,nm) where X_lr is the root vnode of
			 * the current lofs.
			 *
			 * We come here only when we are called with X_l as dvp
			 * and look for something underneath.
			 *
			 * Now that an autofs/lofs looping condition has been
			 * identified any directory vnode contained within
			 * dvp will be set to the vnode covered by the
			 * mounted autofs. Thus all directories within dvp
			 * will appear empty hence teminating the looping.
			 * The LO_AUTOLOOP flag is set on the returned lonode
			 * to indicate the termination of the autofs/lofs
			 * looping. This is required for the correct behaviour
			 * when performing a dotdot.
			 */
			realdvp = realvp(dvp);
			while (vfs_matchops(realdvp->v_vfsp, lo_vfsops)) {
				realdvp = realvp(realdvp);
			}

			error = VFS_ROOT(realdvp->v_vfsp, &tvp);
			if (error)
				goto out;
			/*
			 * tvp now contains the rootvp of the vfs of the
			 * real vnode of dvp. The directory vnode vp is set
			 * to the covered vnode to terminate looping. No
			 * distinction is made between any vp as all directory
			 * vnodes contained in dvp are returned as the covered
			 * vnode.
			 */
			VN_RELE(vp);
			vp = tvp;	/* possibly is an autonode */

			/*
			 * Need to find the covered vnode
			 */
			if (vp->v_vfsp->vfs_vnodecovered == NULL) {
				/*
				 * We don't have a covered vnode so this isn't
				 * an autonode. To find the autonode simply
				 * find the vnode covered by the lofs rootvp.
				 */
				vp = li->li_rootvp;
				vp = vp->v_vfsp->vfs_vnodecovered;
				VN_RELE(tvp);
				error = VFS_ROOT(vp->v_vfsp, &tvp);
				if (error)
					goto out;
				vp = tvp;	/* now this is an autonode */
				if (vp->v_vfsp->vfs_vnodecovered == NULL) {
					/*
					 * Still can't find a covered vnode.
					 * Fail the lookup, or we'd loop.
					 */
					error = ENOENT;
					goto out;
				}
			}
			vp = vp->v_vfsp->vfs_vnodecovered;
			VN_HOLD(vp);
			VN_RELE(tvp);
			/*
			 * Force the creation of a new lnode even if the hash
			 * table contains a lnode that references this vnode.
			 */
			mkflag = LOF_FORCE;
			autoloop++;
		}
	}
	*vpp = makelonode(vp, li, mkflag);

	if ((looping) ||
	    (((vtol(dvp))->lo_looping & LO_LOOPING) && !doingdotdot)) {
		(vtol(*vpp))->lo_looping |= LO_LOOPING;
	}

	if (autoloop) {
		(vtol(*vpp))->lo_looping |= LO_AUTOLOOP;
	}

out:
	if (error != 0 && vp != NULL)
		VN_RELE(vp);
error:
	LOFS_EXIT(li);
	return (error);

success:
	LOFS_EXIT(li);
	return (0);
}

/*ARGSUSED*/
static int
lo_create(
	vnode_t *dvp,
	char *nm,
	struct vattr *va,
	enum vcexcl exclusive,
	int mode,
	vnode_t **vpp,
	struct cred *cr,
	int flag,
	caller_context_t *ct,
	vsecattr_t *vsecp)
{
	int error;
	struct loinfo *li = VTOLI(dvp);
	vnode_t *vp = NULL;

	LOFS_ENTER(li);
	if (*nm == '\0') {
		ASSERT(vpp && dvp == *vpp);
		vp = realvp(*vpp);
	}

	error = VOP_CREATE(realvp(dvp), nm, va, exclusive, mode, &vp, cr, flag,
	    ct, vsecp);
	if (!error) {
		*vpp = makelonode(vp, li, 0);
		if (IS_DEVVP(*vpp)) {
			vnode_t *svp;

			svp = specvp(*vpp, (*vpp)->v_rdev, (*vpp)->v_type, cr);
			VN_RELE(*vpp);
			if (svp == NULL)
				error = ENOSYS;
			else
				*vpp = svp;
		}
	}
	LOFS_EXIT(li);

	return (error);
}

static int
lo_remove(
	vnode_t *dvp,
	char *nm,
	struct cred *cr,
	caller_context_t *ct,
	int flags)
{
	struct loinfo *li = VTOLI(dvp);
	int ret;

	LOFS_ENTER(li);
	dvp = realvp(dvp);
	ret = VOP_REMOVE(dvp, nm, cr, ct, flags);
	LOFS_EXIT(li);

	return (ret);
}

static int
lo_link(
	vnode_t *tdvp,
	vnode_t *vp,
	char *tnm,
	struct cred *cr,
	caller_context_t *ct,
	int flags)
{
	struct loinfo *li = VTOLI(tdvp);
	vnode_t *realvp;
	int ret;

	LOFS_ENTER(li);

	/*
	 * The source and destination vnodes may be in different lofs
	 * filesystems sharing the same underlying filesystem, so we need to
	 * make sure that the filesystem containing the source vnode is not
	 * mounted read-only (vn_link() has already checked the target vnode).
	 *
	 * In a situation such as:
	 *
	 * /data	- regular filesystem
	 * /foo		- lofs mount of /data/foo
	 * /bar		- read-only lofs mount of /data/bar
	 *
	 * This disallows a link from /bar/somefile to /foo/somefile,
	 * which would otherwise allow changes to somefile on the read-only
	 * mounted /bar.
	 */

	if (vn_is_readonly(vp)) {
		LOFS_EXIT(li);
		return (EROFS);
	}
	while (vn_matchops(vp, lo_vnodeops)) {
		vp = realvp(vp);
	}

	/*
	 * In the case where the source vnode is on another stacking
	 * filesystem (such as specfs), the loop above will
	 * terminate before finding the true underlying vnode.
	 *
	 * We use VOP_REALVP here to continue the search.
	 */
	if (VOP_REALVP(vp, &realvp, ct) == 0)
		vp = realvp;

	while (vn_matchops(tdvp, lo_vnodeops)) {
		tdvp = realvp(tdvp);
	}
	if (vp->v_vfsp != tdvp->v_vfsp)
		ret = EXDEV;
	else
		ret = VOP_LINK(tdvp, vp, tnm, cr, ct, flags);
	LOFS_EXIT(li);

	return (ret);
}

static int
lo_rename(
	vnode_t *odvp,
	char *onm,
	vnode_t *ndvp,
	char *nnm,
	struct cred *cr,
	caller_context_t *ct,
	int flags)
{
	vnode_t *tnvp;
	struct loinfo *li = VTOLI(odvp);
	int error;

	LOFS_ENTER(li);

	/*
	 * If we are coming from a loop back mounted fs, that has been
	 * mounted in the same filesystem as where we want to move to,
	 * and that filesystem is read/write, but the lofs filesystem is
	 * read only, we don't want to allow a rename of the file. The
	 * vn_rename code checks to be sure the target is read/write already
	 * so that is not necessary here. However, consider the following
	 * example:
	 *		/ - regular root fs
	 *		/foo - directory in root
	 *		/foo/bar - file in foo directory(in root fs)
	 *		/baz - directory in root
	 *		mount -F lofs -o ro /foo /baz - all still in root
	 *			directory
	 * The fact that we mounted /foo on /baz read only should stop us
	 * from renaming the file /foo/bar /bar, but it doesn't since
	 * / is read/write. We are still renaming here since we are still
	 * in the same filesystem, it is just that we do not check to see
	 * if the filesystem we are coming from in this case is read only.
	 */
	if (odvp->v_vfsp->vfs_flag & VFS_RDONLY) {
		error = EROFS;
		goto error;
	}
	/*
	 * We need to make sure we're not trying to remove a mount point for a
	 * filesystem mounted on top of lofs, which only we know about.
	 */
	if (vn_matchops(ndvp, lo_vnodeops))	/* Not our problem. */
		goto rename;

	/*
	 * XXXci - Once case-insensitive behavior is implemented, it should
	 * be added here.
	 */
	if (VOP_LOOKUP(ndvp, nnm, &tnvp, NULL, 0, NULL, cr,
	    ct, NULL, NULL) != 0)
		goto rename;
	if (tnvp->v_type != VDIR) {
		VN_RELE(tnvp);
		goto rename;
	}
	if (vn_mountedvfs(tnvp)) {
		VN_RELE(tnvp);
		error = EBUSY;
		goto error;
	}
	VN_RELE(tnvp);
rename:
	/*
	 * Since the case we're dealing with above can happen at any layer in
	 * the stack of lofs filesystems, we need to recurse down the stack,
	 * checking to see if there are any instances of a filesystem mounted on
	 * top of lofs. In order to keep on using the lofs version of
	 * VOP_RENAME(), we make sure that while the target directory is of type
	 * lofs, the source directory (the one used for getting the fs-specific
	 * version of VOP_RENAME()) is also of type lofs.
	 */
	if (vn_matchops(ndvp, lo_vnodeops)) {
		ndvp = realvp(ndvp);	/* Check the next layer */
	} else {
		/*
		 * We can go fast here
		 */
		while (vn_matchops(odvp, lo_vnodeops)) {
			odvp = realvp(odvp);
		}
		if (odvp->v_vfsp != ndvp->v_vfsp) {
			error = EXDEV;
			goto error;
		}
	}
	error = VOP_RENAME(odvp, onm, ndvp, nnm, cr, ct, flags);

error:
	LOFS_EXIT(li);
	return (error);
}

static int
lo_mkdir(
	vnode_t *dvp,
	char *nm,
	struct vattr *va,
	vnode_t **vpp,
	struct cred *cr,
	caller_context_t *ct,
	int flags,
	vsecattr_t *vsecp)
{
	struct loinfo *li = VTOLI(dvp);
	int error;

	LOFS_ENTER(li);

	error = VOP_MKDIR(realvp(dvp), nm, va, vpp, cr, ct, flags, vsecp);
	if (!error)
		*vpp = makelonode(*vpp, li, 0);

	LOFS_EXIT(li);
	return (error);
}

static int
lo_realvp(vnode_t *vp, vnode_t **vpp, caller_context_t *ct)
{
	struct loinfo *li = VTOLI(vp);

	LOFS_ENTER(li);
	while (vn_matchops(vp, lo_vnodeops))
		vp = realvp(vp);

	if (VOP_REALVP(vp, vpp, ct) != 0)
		*vpp = vp;

	LOFS_EXIT(li);
	return (0);
}

static int
lo_rmdir(
	vnode_t *dvp,
	char *nm,
	vnode_t *cdir,
	struct cred *cr,
	caller_context_t *ct,
	int flags)
{
	struct loinfo *li = VTOLI(dvp);
	vnode_t *rvp = cdir;
	int ret;

	LOFS_ENTER(li);

	/* if cdir is lofs vnode ptr get its real vnode ptr */
	if (vn_matchops(dvp, vn_getops(rvp)))
		(void) lo_realvp(cdir, &rvp, ct);
	dvp = realvp(dvp);
	ret = VOP_RMDIR(dvp, nm, rvp, cr, ct, flags);
	LOFS_EXIT(li);

	return (ret);
}

static int
lo_symlink(
	vnode_t *dvp,
	char *lnm,
	struct vattr *tva,
	char *tnm,
	struct cred *cr,
	caller_context_t *ct,
	int flags)
{
	struct loinfo *li = VTOLI(dvp);
	int ret;

	LOFS_ENTER(li);
	dvp = realvp(dvp);
	ret = VOP_SYMLINK(dvp, lnm, tva, tnm, cr, ct, flags);
	LOFS_EXIT(li);

	return (ret);
}

static int
lo_readlink(
	vnode_t *vp,
	struct uio *uiop,
	struct cred *cr,
	caller_context_t *ct)
{
	struct loinfo *li = VTOLI(vp);
	int ret;

	LOFS_ENTER(li);
	vp = realvp(vp);
	ret = VOP_READLINK(vp, uiop, cr, ct);
	LOFS_EXIT(li);

	return (ret);
}

static int
lo_readdir(
	vnode_t *vp,
	struct uio *uiop,
	struct cred *cr,
	int *eofp,
	caller_context_t *ct,
	int flags)
{
	struct loinfo *li = VTOLI(vp);
	int ret;

	LOFS_ENTER(li);
	vp = realvp(vp);
	ret = VOP_READDIR(vp, uiop, cr, eofp, ct, flags);
	LOFS_EXIT(li);

	return (ret);
}

static int
lo_rwlock(vnode_t *vp, int write_lock, caller_context_t *ct)
{
	struct loinfo *li = VTOLI(vp);
	int ret;

	LOFS_ENTER(li);
	vp = realvp(vp);
	ret = VOP_RWLOCK(vp, write_lock, ct);
	LOFS_EXIT(li);

	return (ret);
}

static void
lo_rwunlock(vnode_t *vp, int write_lock, caller_context_t *ct)
{
	struct loinfo *li = VTOLI(vp);

	LOFS_ENTER_VOID(li);
	vp = realvp(vp);
	VOP_RWUNLOCK(vp, write_lock, ct);
	LOFS_EXIT(li);
}

static int
lo_seek(vnode_t *vp, offset_t ooff, offset_t *noffp, caller_context_t *ct)
{
	struct loinfo *li = VTOLI(vp);
	int ret;

	LOFS_ENTER(li);
	vp = realvp(vp);
	ret = VOP_SEEK(vp, ooff, noffp, ct);
	LOFS_EXIT(li);

	return (ret);
}

static int
lo_cmp(vnode_t *vp1, vnode_t *vp2, caller_context_t *ct)
{
	struct loinfo *li = VTOLI(vp1);
	int ret;

	LOFS_ENTER(li);
	while (vn_matchops(vp1, lo_vnodeops))
		vp1 = realvp(vp1);
	while (vn_matchops(vp2, lo_vnodeops))
		vp2 = realvp(vp2);
	ret = VOP_CMP(vp1, vp2, ct);
	LOFS_EXIT(li);

	return (ret);
}

static int
lo_frlock(
	vnode_t *vp,
	int cmd,
	struct flock64 *bfp,
	int flag,
	offset_t offset,
	struct flk_callback *flk_cbp,
	cred_t *cr,
	caller_context_t *ct)
{
	struct loinfo *li = VTOLI(vp);
	int ret;

	LOFS_ENTER(li);
	vp = realvp(vp);
	ret = VOP_FRLOCK(vp, cmd, bfp, flag, offset, flk_cbp, cr, ct);
	LOFS_EXIT(li);

	return (ret);
}

static int
lo_space(
	vnode_t *vp,
	int cmd,
	struct flock64 *bfp,
	int flag,
	offset_t offset,
	struct cred *cr,
	caller_context_t *ct)
{
	struct loinfo *li = VTOLI(vp);
	int ret;

	LOFS_ENTER(li);
	vp = realvp(vp);
	ret = VOP_SPACE(vp, cmd, bfp, flag, offset, cr, ct);
	LOFS_EXIT(li);

	return (ret);
}

static int
lo_getpage(
	vnode_t *vp,
	offset_t off,
	size_t len,
	uint_t *prot,
	struct page *parr[],
	size_t psz,
	struct seg *seg,
	caddr_t addr,
	enum seg_rw rw,
	struct cred *cr,
	caller_context_t *ct)
{
	struct loinfo *li = VTOLI(vp);
	int ret;

	LOFS_ENTER(li);
	vp = realvp(vp);
	ret = VOP_GETPAGE(vp, off, len, prot, parr, psz, seg, addr, rw, cr,
	    ct);
	LOFS_EXIT(li);

	return (ret);
}

static int
lo_putpage(
	vnode_t *vp,
	offset_t off,
	size_t len,
	int flags,
	struct cred *cr,
	caller_context_t *ct)
{
	struct loinfo *li = VTOLI(vp);
	int ret;

	LOFS_ENTER(li);
	vp = realvp(vp);
	ret = VOP_PUTPAGE(vp, off, len, flags, cr, ct);
	LOFS_EXIT(li);

	return (ret);
}

static int
lo_map(
	vnode_t *vp,
	offset_t off,
	struct as *as,
	caddr_t *addrp,
	size_t len,
	uchar_t prot,
	uchar_t maxprot,
	uint_t flags,
	struct cred *cr,
	caller_context_t *ct)
{
	struct loinfo *li = VTOLI(vp);
	int ret;

	LOFS_ENTER(li);
	vp = realvp(vp);
	ret = VOP_MAP(vp, off, as, addrp, len, prot, maxprot, flags, cr, ct);
	LOFS_EXIT(li);

	return (ret);
}

static int
lo_addmap(
	vnode_t *vp,
	offset_t off,
	struct as *as,
	caddr_t addr,
	size_t len,
	uchar_t prot,
	uchar_t maxprot,
	uint_t flags,
	struct cred *cr,
	caller_context_t *ct)
{
	struct loinfo *li = VTOLI(vp);
	int ret;

	LOFS_ENTER(li);
	vp = realvp(vp);
	ret = VOP_ADDMAP(vp, off, as, addr, len, prot, maxprot, flags, cr,
	    ct);
	LOFS_EXIT(li);

	return (ret);
}

static int
lo_delmap(
	vnode_t *vp,
	offset_t off,
	struct as *as,
	caddr_t addr,
	size_t len,
	uint_t prot,
	uint_t maxprot,
	uint_t flags,
	struct cred *cr,
	caller_context_t *ct)
{
	struct loinfo *li = VTOLI(vp);
	int ret;

	LOFS_ENTER(li);
	vp = realvp(vp);
	ret = VOP_DELMAP(vp, off, as, addr, len, prot, maxprot, flags, cr,
	    ct);
	LOFS_EXIT(li);

	return (ret);
}

static int
lo_poll(
	vnode_t *vp,
	short events,
	int anyyet,
	short *reventsp,
	struct pollhead **phpp,
	caller_context_t *ct)
{
	struct loinfo *li = VTOLI(vp);
	int ret;

	LOFS_ENTER(li);
	vp = realvp(vp);
	ret = VOP_POLL(vp, events, anyyet, reventsp, phpp, ct);
	LOFS_EXIT(li);

	return (ret);
}

static int
lo_dump(vnode_t *vp, caddr_t addr, offset_t bn, offset_t count,
    caller_context_t *ct)
{
	struct loinfo *li = VTOLI(vp);
	int ret;

	LOFS_ENTER(li);
	vp = realvp(vp);
	ret = VOP_DUMP(vp, addr, bn, count, ct);
	LOFS_EXIT(li);

	return (ret);
}

static int
lo_pathconf(
	vnode_t *vp,
	int cmd,
	ulong_t *valp,
	struct cred *cr,
	caller_context_t *ct)
{
	struct loinfo *li = VTOLI(vp);
	int ret;

	LOFS_ENTER(li);
	vp = realvp(vp);
	ret = VOP_PATHCONF(vp, cmd, valp, cr, ct);
	LOFS_EXIT(li);

	return (ret);
}

static int
lo_pageio(
	vnode_t *vp,
	struct page *pp,
	u_offset_t io_off,
	size_t io_len,
	int flags,
	cred_t *cr,
	caller_context_t *ct)
{
	struct loinfo *li = VTOLI(vp);
	int ret;

	LOFS_ENTER(li);
	vp = realvp(vp);
	ret = VOP_PAGEIO(vp, pp, io_off, io_len, flags, cr, ct);
	LOFS_EXIT(li);

	return (ret);
}

static void
lo_dispose(
	vnode_t *vp,
	page_t *pp,
	int fl,
	int dn,
	cred_t *cr,
	caller_context_t *ct)
{
	struct loinfo *li = VTOLI(vp);

	LOFS_ENTER_VOID(li);
	vp = realvp(vp);
	if (vp != NULL && !VN_ISKAS(vp))
		VOP_DISPOSE(vp, pp, fl, dn, cr, ct);
	LOFS_EXIT(li);
}

static int
lo_setsecattr(
	vnode_t *vp,
	vsecattr_t *secattr,
	int flags,
	struct cred *cr,
	caller_context_t *ct)
{
	struct loinfo *li = VTOLI(vp);
	int ret;

	LOFS_ENTER(li);
	if (vn_is_readonly(vp)) {
		LOFS_EXIT(li);
		return (EROFS);
	}
	vp = realvp(vp);
	ret = VOP_SETSECATTR(vp, secattr, flags, cr, ct);
	LOFS_EXIT(li);

	return (ret);
}

static int
lo_getsecattr(
	vnode_t *vp,
	vsecattr_t *secattr,
	int flags,
	struct cred *cr,
	caller_context_t *ct)
{
	struct loinfo *li = VTOLI(vp);
	int ret;

	LOFS_ENTER(li);
	vp = realvp(vp);
	ret = VOP_GETSECATTR(vp, secattr, flags, cr, ct);
	LOFS_EXIT(li);

	return (ret);
}

static int
lo_shrlock(
	vnode_t *vp,
	int cmd,
	struct shrlock *shr,
	int flag,
	cred_t *cr,
	caller_context_t *ct)
{
	struct loinfo *li = VTOLI(vp);
	int ret;

	LOFS_ENTER(li);
	vp = realvp(vp);
	ret = VOP_SHRLOCK(vp, cmd, shr, flag, cr, ct);
	LOFS_EXIT(li);

	return (ret);
}

/*
 * Loopback vnode operations vector.
 */

struct vnodeops *lo_vnodeops;

const fs_operation_def_t lo_vnodeops_template[] = {
	VOPNAME_OPEN,		{ .vop_open = lo_open },
	VOPNAME_CLOSE,		{ .vop_close = lo_close },
	VOPNAME_READ,		{ .vop_read = lo_read },
	VOPNAME_WRITE,		{ .vop_write = lo_write },
	VOPNAME_IOCTL,		{ .vop_ioctl = lo_ioctl },
	VOPNAME_SETFL,		{ .vop_setfl = lo_setfl },
	VOPNAME_GETATTR,	{ .vop_getattr = lo_getattr },
	VOPNAME_SETATTR,	{ .vop_setattr = lo_setattr },
	VOPNAME_ACCESS,		{ .vop_access = lo_access },
	VOPNAME_LOOKUP,		{ .vop_lookup = lo_lookup },
	VOPNAME_CREATE,		{ .vop_create = lo_create },
	VOPNAME_REMOVE,		{ .vop_remove = lo_remove },
	VOPNAME_LINK,		{ .vop_link = lo_link },
	VOPNAME_RENAME,		{ .vop_rename = lo_rename },
	VOPNAME_MKDIR,		{ .vop_mkdir = lo_mkdir },
	VOPNAME_RMDIR,		{ .vop_rmdir = lo_rmdir },
	VOPNAME_READDIR,	{ .vop_readdir = lo_readdir },
	VOPNAME_SYMLINK,	{ .vop_symlink = lo_symlink },
	VOPNAME_READLINK,	{ .vop_readlink = lo_readlink },
	VOPNAME_FSYNC,		{ .vop_fsync = lo_fsync },
	VOPNAME_INACTIVE,	{ .vop_inactive = lo_inactive },
	VOPNAME_FID,		{ .vop_fid = lo_fid },
	VOPNAME_RWLOCK,		{ .vop_rwlock = lo_rwlock },
	VOPNAME_RWUNLOCK,	{ .vop_rwunlock = lo_rwunlock },
	VOPNAME_SEEK,		{ .vop_seek = lo_seek },
	VOPNAME_CMP,		{ .vop_cmp = lo_cmp },
	VOPNAME_FRLOCK,		{ .vop_frlock = lo_frlock },
	VOPNAME_SPACE,		{ .vop_space = lo_space },
	VOPNAME_REALVP,		{ .vop_realvp = lo_realvp },
	VOPNAME_GETPAGE,	{ .vop_getpage = lo_getpage },
	VOPNAME_PUTPAGE,	{ .vop_putpage = lo_putpage },
	VOPNAME_MAP,		{ .vop_map = lo_map },
	VOPNAME_ADDMAP,		{ .vop_addmap = lo_addmap },
	VOPNAME_DELMAP,		{ .vop_delmap = lo_delmap },
	VOPNAME_POLL,		{ .vop_poll = lo_poll },
	VOPNAME_DUMP,		{ .vop_dump = lo_dump },
	VOPNAME_DUMPCTL,	{ .error = fs_error },	/* XXX - why? */
	VOPNAME_PATHCONF,	{ .vop_pathconf = lo_pathconf },
	VOPNAME_PAGEIO,		{ .vop_pageio = lo_pageio },
	VOPNAME_DISPOSE,	{ .vop_dispose = lo_dispose },
	VOPNAME_SETSECATTR,	{ .vop_setsecattr = lo_setsecattr },
	VOPNAME_GETSECATTR,	{ .vop_getsecattr = lo_getsecattr },
	VOPNAME_SHRLOCK,	{ .vop_shrlock = lo_shrlock },
	NULL,			NULL
};
