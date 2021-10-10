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

#include <sys/types.h>
#include <rpc/types.h>
#include <sys/systm.h>
#include <sys/vfs.h>
#include <sys/errno.h>
#include <sys/cred.h>
#include <sys/policy.h>
#include <sys/siginfo.h>
#include <sys/proc.h>		/* for exit() declaration */
#include <sys/kmem.h>
#include <nfs/nfs4.h>
#include <nfs/nfs4_mig.h>
#include <nfs/nfssys.h>
#include <nfs/export.h>
#include <sys/thread.h>
#include <rpc/auth.h>
#include <rpc/rpcsys.h>
#include <rpc/svc.h>

int rfs4_do_tsm = 1;

/*ARGSUSED*/
migerr_t
rfs4_migop_freeze(vnode_t *vp, void *rp)
{
	return (rfs4_fse_freeze_fsid(vp));
}

/*ARGSUSED*/
migerr_t
rfs4_migop_grace(vnode_t *vp, void *rp)
{
	migerr_t migerr = 0;

	migerr = rfs4_fse_grace_fsid(vp);
	if (migerr == 0)
		cmn_err(CE_WARN, "Server set to return GRACE");

	return (migerr);
}

/*ARGSUSED*/
migerr_t
rfs4_migop_thaw(vnode_t *vp, void *rp)
{
	return (rfs4_fse_thaw_fsid(vp));
}

migerr_t
rfs4_migop_harvest(vnode_t *vp, void *rp)
{
	migerr_t migerr = 0;
	if (rfs4_do_tsm == 1)
		migerr = rfs4_tsm(rp, vp, TSM_HARVEST);
	else
		cmn_err(CE_WARN, "rfs4_migop_harvest: operation is a no-op");

	return (migerr);
}

migerr_t
rfs4_migop_hydrate(vnode_t *vp, void *rp)
{
	migerr_t migerr = 0;

	if (rfs4_do_tsm == 1)
		migerr = rfs4_tsm(rp, vp, TSM_HYDRATE);
	else
		cmn_err(CE_WARN, "rfs4_migop_hydrate: operation is a no-op");

	return (migerr);
}

static migerr_t
set_fs_reparse(vnode_t *vp, int sense)
{
	xvattr_t xvattr;
	xoptattr_t *xoap;
	int err = 0;

	/*
	 * Does the filesystem support reparse points?
	 */
	if (vfs_has_feature(vp->v_vfsp, VFSFT_REPARSE) == 0)
		return (ENOTSUP);

	/*
	 * Set the XAT_REPARSE extensible system attribute to
	 * a set state by arranging to pass in a 'fat' attr set
	 */
	xva_init(&xvattr);
	xoap = xva_getxoptattr(&xvattr);
	ASSERT(xoap);
	XVA_SET_REQ(&xvattr, XAT_REPARSE);
	xoap->xoa_reparse = (sense != 0);
	xvattr.xva_vattr.va_mask |= AT_MTIME;
	gethrestime(&xvattr.xva_vattr.va_mtime);

	err = VOP_SETATTR(vp, &xvattr.xva_vattr, 0, kcred, NULL);

	return (err);
}

/*ARGSUSED*/
migerr_t
rfs4_migop_convert(vnode_t *vp, void *rp)
{
	migerr_t migerr = 0;
	int err = 0;

	migerr = rfs4_fse_convert_fsid(vp, FSE_MOVED);
	if (migerr != MIG_OK)
		return (migerr);

	err = set_fs_reparse(vp, 1);
	if (err)
		return (MIGERR_REPARSE);

	return (MIG_OK);
}

/*ARGSUSED*/
migerr_t
rfs4_migop_unconvert(vnode_t *vp, void *rp)
{
	migerr_t migerr = 0;
	int err = 0;

	migerr = rfs4_fse_convert_fsid(vp, FSE_AVAILABLE);
	if (migerr != MIG_OK)
		return (migerr);

	err = set_fs_reparse(vp, 0);
	if (err)
		return (MIGERR_REPARSE);

	return (MIG_OK);
}

migerr_t
rfs4_migop_status(vnode_t *vp, void *mig_fsstat)
{
	fsh_entry_t *fse;
	uint32_t *fsstat = mig_fsstat;
	rfs_inst_t *rip = rfs_inst_find(FALSE);

	*fsstat = 0;

	if (rip == NULL) {
		return (MIGERR_NONFSINST);
	}

	fse = fsh_get_ent(rip, vp->v_vfsp->vfs_fsid);
	if (fse == NULL) {
		rfs_inst_active_rele(rip);
		return (MIG_OK);
	}

	mutex_enter(&fse->fse_lock);
	if (fse->fse_state == FSE_AVAILABLE) {
		mutex_exit(&fse->fse_lock);
		fsh_ent_rele(rip, fse);
		rfs_inst_active_rele(rip);
		return (MIG_OK);
	}

	if (fse->fse_state & FSE_FROZEN) {
		*fsstat = FS_FROZEN;
	}
	if (fse->fse_state & FSE_MOVED) {
		*fsstat |= FS_CONVERTED;
	}
	mutex_exit(&fse->fse_lock);
	fsh_ent_rele(rip, fse);
	rfs_inst_active_rele(rip);

	return (MIG_OK);

}
