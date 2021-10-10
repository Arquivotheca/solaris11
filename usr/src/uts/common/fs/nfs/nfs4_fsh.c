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
#include <sys/param.h>
#include <sys/time.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/debug.h>
#include <sys/cmn_err.h>
#include <sys/sdt.h>
#include <sys/atomic.h>

#include <nfs/nfs.h>
#include <nfs/nfs4_fsh.h>
#include <nfs/export.h>
#include <nfs/nfs_srv_inst_impl.h>
#include <nfs/nfs4_mig.h>

/*
 * File System Hash routines
 */

/* ARGSUSED */
static int
rfs4_fse_constructor(void *obj, void *private, int kmflag)
{
	fsh_entry_t *fse = obj;

	mutex_init(&fse->fse_lock, NULL, MUTEX_DEFAULT, NULL);

	/* Initialize the freeze lock for migration */
	nfs_rw_init(&fse->fse_freeze_lock, NULL, RW_DEFAULT, NULL);

	return (0);
}

/* ARGSUSED */
static void
rfs4_fse_destructor(void *obj, void *private)
{
	fsh_entry_t *fse = obj;

	nfs_rw_destroy(&fse->fse_freeze_lock);
	mutex_destroy(&fse->fse_lock);
}

void
fs_hash_init(rfs_inst_t *rip)
{
	int i;
	fsh_bucket_t *fshbp;
	char name[RFS_UNIQUE_BUFLEN];

	for (i = 0; i < FSHTBLSZ; i++) {
		fshbp = &rip->ri_fsh_table[i];
		rw_init(&fshbp->fsb_lock, NULL, RW_DEFAULT, NULL);
		list_create(&fshbp->fsb_entries, sizeof (fsh_entry_t),
		    offsetof(fsh_entry_t, fse_node));
		fshbp->fsb_chainlen = 0;
	}

	/*
	 * unlike the fsh_table, we could use only one cache after the
	 * merge with server instances, however, one per instance would
	 * work, too.  If we do a cache per instance, at least I'll know
	 * where to do the cache_destroy.  The down side is, we will have
	 * to make the name unique for each cache.
	 */
	rfs_inst_uniqstr(rip, "fse_cache", name, RFS_UNIQUE_BUFLEN);
	rip->ri_fse_cache = kmem_cache_create(name, sizeof (fsh_entry_t), 0,
	    rfs4_fse_constructor, rfs4_fse_destructor, NULL, NULL, NULL, 0);
}

void
fs_hash_destroy(rfs_inst_t *rip)
{
	int i;
	fsh_bucket_t *fshbp;

	for (i = 0; i < FSHTBLSZ; i++) {
		fshbp = &rip->ri_fsh_table[i];
		rw_destroy(&fshbp->fsb_lock);
		list_destroy(&fshbp->fsb_entries);
	}
	kmem_cache_destroy(rip->ri_fse_cache);
}

/* really simple for now, we can improve on it */
static int
fs_hash(fsid_t fsid)
{
	return ((fsid.val[0] >> 8) & (FSHTBLSZ - 1));
}

static fsh_entry_t *
fsh_find(fsid_t fsid, fsh_bucket_t *fsb)
{
	fsh_entry_t *fse;

	ASSERT(RW_LOCK_HELD(&fsb->fsb_lock));

	for (fse = list_head(&fsb->fsb_entries);
	    fse != NULL;
	    fse = list_next(&fsb->fsb_entries, fse)) {
		if (EQFSID(&fsid, &fse->fse_fsid))
			break;
	}

	return (fse);
}

static fsh_entry_t *
fsh_ent_alloc(rfs_inst_t *rip)
{
	fsh_entry_t *fse;
	int x;

	fse = kmem_cache_alloc(rip->ri_fse_cache, KM_SLEEP);

	fse->fse_state = FSE_AVAILABLE;
	fse->fse_state_store = NULL;
	fse->fse_loaded_from_freeze = 0;
	fse->fse_start_time = 0;
	fse->fse_grace_period = 0;
	fse->fse_wr4verf = 0;

	x = atomic_add_32_nv(&rfs.rg_fse_uniq, 1);
	(void) sprintf(fse->fse_name, "%d", x);

	rfs4_fse_db_create(rip, fse);

	return (fse);
}

static void
fsh_ent_free(rfs_inst_t *rip, fsh_entry_t *fse)
{
	ASSERT(fse->fse_refcnt == 0);

	if (fse->fse_mntpt != NULL)
		refstr_rele(fse->fse_mntpt);

	if (fse->fse_state_store) {
		rfs4_database_shutdown(fse->fse_state_store);
		rfs4_database_destroy(fse->fse_state_store);
		fse->fse_state_store = NULL;
	}

	kmem_cache_free(rip->ri_fse_cache, fse);
}

static void
fsh_ent_hold(fsh_entry_t *fse)
{
	atomic_add_32(&fse->fse_refcnt, 1);
}

void
fsh_ent_rele(rfs_inst_t *rip, fsh_entry_t *fse)
{
	fsh_bucket_t *fsb = fse->fse_fsb;

	ASSERT(fse->fse_refcnt != 0);

	rw_enter(&fsb->fsb_lock, RW_WRITER);
	if (atomic_add_32_nv(&fse->fse_refcnt, -1) == 0) {
		list_remove(&fsb->fsb_entries, fse);
		fsb->fsb_chainlen--;
		rw_exit(&fsb->fsb_lock);
		fsh_ent_free(rip, fse);
	} else
		rw_exit(&fsb->fsb_lock);
}

void
fsh_fsid_rele(rfs_inst_t *rip, fsid_t fsid)
{
	fsh_entry_t *fse;

	fse = fsh_get_ent(rip, fsid);
	if (fse != NULL) {
		/* For the caller.  */
		fsh_ent_rele(rip, fse);

		/* For our fsh_get_ent() */
		fsh_ent_rele(rip, fse);
	}
}

/*
 * The database doesn't have a hold on the FSE, only the shares do.
 * Since the NFSv4 server is being shutdown, shutdown all the per file
 * system databases and destroy them.
 */
void
fsh_db_cleanup(rfs_inst_t *rip)
{
	fsh_entry_t *fse;
	fsh_bucket_t *fshbp;
	int i;

	for (i = 0; i < FSHTBLSZ; i++, fshbp++) {
		fshbp = &rip->ri_fsh_table[i];
		rw_enter(&fshbp->fsb_lock, RW_WRITER);
		for (fse = list_head(&fshbp->fsb_entries);
		    fse != NULL;
		    fse = list_next(&fshbp->fsb_entries, fse)) {
			if (fse->fse_state_store) {
				rfs4_database_shutdown(fse->fse_state_store);
				rfs4_database_destroy(fse->fse_state_store);
				fse->fse_state_store = NULL;
			}
		}
		rw_exit(&fshbp->fsb_lock);
	}
}

void
fsh_db_init(rfs_inst_t *rip)
{
	fsh_entry_t *fse;
	fsh_bucket_t *fshbp;
	int i;

	for (i = 0; i < FSHTBLSZ; i++) {
		fshbp = &rip->ri_fsh_table[i];
		rw_enter(&fshbp->fsb_lock, RW_WRITER);
		for (fse = list_head(&fshbp->fsb_entries);
		    fse != NULL;
		    fse = list_next(&fshbp->fsb_entries, fse)) {
			rfs4_fse_db_create(rip, fse);
		}
		rw_exit(&fshbp->fsb_lock);
	}
}

fsh_entry_t *
fsh_get_ent(rfs_inst_t *rip, fsid_t fsid)
{
	fsh_bucket_t *fsb;
	fsh_entry_t *fse;

	ASSERT(rip != NULL);

	fsb = &rip->ri_fsh_table[fs_hash(fsid)];

	rw_enter(&fsb->fsb_lock, RW_READER);
	fse = fsh_find(fsid, fsb);
	if (fse != NULL)
		fsh_ent_hold(fse);

	rw_exit(&fsb->fsb_lock);

	return (fse);
}

/*
 * If the entry was added from a freeze command,
 * it means that we should not bump the refcnt
 * for the first access from the export.
 *
 * We only do this for the first export as we
 * want the refcnt to reflect a minimum of
 * how many entries we have for the shares in
 * the filesystem.
 */
static void
fsh_loaded_freeze_check(fsh_entry_t *fse)
{
	mutex_enter(&fse->fse_lock);
	if (fse->fse_loaded_from_freeze)
		fse->fse_loaded_from_freeze = FALSE;
	else
		fsh_ent_hold(fse);
	mutex_exit(&fse->fse_lock);
}

/*
 * An entry can either be added from an export or
 * a freeze.
 */
void
fsh_add(rfs_inst_t *rip, fsid_t fsid, bool_t adding_from_freeze)
{
	fsh_bucket_t *fsb;
	fsh_entry_t *fse, *new;

	fsb = &rip->ri_fsh_table[fs_hash(fsid)];

	/* before allocating a new entry, check to see if it exists */
	rw_enter(&fsb->fsb_lock, RW_READER);
	fse = fsh_find(fsid, fsb);

	if (fse != NULL) {
		fsh_loaded_freeze_check(fse);
		rw_exit(&fsb->fsb_lock);
		DTRACE_PROBE1(nfsmig__i__fsh_add, int, fse->fse_refcnt);
		return;
	}
	rw_exit(&fsb->fsb_lock);

	/* do the allocation before we grab the lock */
	new = fsh_ent_alloc(rip);

	rw_enter(&fsb->fsb_lock, RW_WRITER);

	fse = fsh_find(fsid, fsb);
	if (fse == NULL) {
		vfs_t	*vfs;
		vnode_t	*rootvp;

		fse = new;

		vfs = getvfs(&fsid);
		if (vfs != NULL) {
			if (vfs->vfs_mntpt != NULL) {
				fse->fse_mntpt = vfs->vfs_mntpt;
				refstr_hold(fse->fse_mntpt);
			}

			/*
			 * Try to determine if the filesystem
			 * has moved.
			 */
			if (VFS_ROOT(vfs, &rootvp)) {
				DTRACE_PROBE1(nfsmig__e__fsh_add__no_root,
				    vfs_t *, vfs);
			} else {
				if (vn_is_nfs_reparse(rootvp, kcred))
					fse->fse_state = FSE_MOVED;
				VN_RELE(rootvp);
			}

			VFS_RELE(vfs);
		}

		mutex_enter(&fse->fse_lock);
		fse->fse_fsid = fsid;
		fse->fse_refcnt = 1;
		fse->fse_loaded_from_freeze = adding_from_freeze;
		mutex_exit(&fse->fse_lock);

		fse->fse_fsb = fsb;
		fsb->fsb_chainlen++;

		list_insert_tail(&fsb->fsb_entries, new);

		rw_exit(&fsb->fsb_lock);
	} else {
		fsh_loaded_freeze_check(fse);

		rw_exit(&fsb->fsb_lock);

		/* just in case there was a race to create this entry */
		fsh_ent_free(rip, new);
	}

	DTRACE_PROBE1(nfsmig__i__fsh_add, int, fse->fse_refcnt);
}

/*
 * Return where we are in the migration process.
 * If we can't find a fsh_entry_t, assume it
 * is local.
 */
uint32_t
rfs4_get_fsh_status(vnode_t *vp)
{
	fsh_entry_t *fse;
	uint32_t state;
	rfs_inst_t *rip = rfs_inst_find(FALSE);

	ASSERT(rip != NULL);
	if (rip == NULL)
		return (FSE_AVAILABLE);

	fse = fsh_get_ent(rip, vp->v_vfsp->vfs_fsid);
	if (fse == NULL) {
		rfs_inst_active_rele(rip);
		return (FSE_AVAILABLE);
	}

	state = fse->fse_state;
	fsh_ent_rele(rip, fse);
	rfs_inst_active_rele(rip);

	return (state);
}

migerr_t
rfs4_fse_freeze_fsid(vnode_t *vp)
{
	fsh_entry_t *fse;
	rfs_inst_t *rip = rfs_inst_find(FALSE);

	if (rip == NULL)
		return (MIGERR_NONFSINST);

	/*
	 * As the destination might do  a freeze before
	 * a share, we have a valid state where we
	 * can not find the filesystem.
	 *
	 * We handle this by adding in the filesystem.
	 *
	 * (XXXtdh: Might be nice to have a flag denoting that
	 * this is for the destination.)
	 */
	fse = fsh_get_ent(rip, vp->v_vfsp->vfs_fsid);
	if (fse == NULL) {
		/*
		 * So add it and we better then find it!
		 *
		 * Note that after finding it, the refcnt
		 * will be at least 2, which is the minimum
		 * of what we expect below.
		 */
		fsh_add(rip, vp->v_vfsp->vfs_fsid, TRUE);
		fse = fsh_get_ent(rip, vp->v_vfsp->vfs_fsid);
		if (fse == NULL) {
			rfs_inst_active_rele(rip);
			return (MIGERR_FSNOENT);
		}
	}

	/*
	 * If already frozen, then bail out with an error instead of blocking
	 * inside nfs_rw_enter_sig and causing a hang.
	 */
	mutex_enter(&fse->fse_lock);
	if (nfs_rw_lock_held(&fse->fse_freeze_lock, RW_WRITER) == TRUE) {
		mutex_exit(&fse->fse_lock);
		fsh_ent_rele(rip, fse);
		rfs_inst_active_rele(rip);
		return (MIGERR_ALREADY);
	}
	(void) nfs_rw_enter_sig(&fse->fse_freeze_lock, RW_WRITER, FALSE);
	fse->fse_state = FSE_FROZEN;
	fse->fse_freeze_lock.remote_thread = TRUE;
	mutex_exit(&fse->fse_lock);

	fsh_ent_rele(rip, fse);
	rfs_inst_active_rele(rip);

	return (MIG_OK);
}

migerr_t
rfs4_fse_grace_fsid(vnode_t *vp)
{
	fsh_entry_t *fse;
	rfs_inst_t *rip = rfs_inst_find(FALSE);

	if (rip == NULL)
		return (MIGERR_NONFSINST);

	if (!rip->ri_v4.r4_enabled) {
		rfs_inst_active_rele(rip);
		return (MIGERR_NOSTATE);
	}

	fse = fsh_get_ent(rip, vp->v_vfsp->vfs_fsid);
	if (fse == NULL) {
		rfs_inst_active_rele(rip);
		return (MIGERR_FSNOENT);
	}

	rfs4_fse_grace_start(rip, fse);
	fsh_ent_rele(rip, fse);
	rfs_inst_active_rele(rip);

	return (MIG_OK);
}

migerr_t
rfs4_fse_convert_fsid(vnode_t *vp, uint32_t state)
{
	fsh_entry_t *fse;
	rfs_inst_t *rip = rfs_inst_find(FALSE);
	migerr_t status = MIG_OK;

	if (rip == NULL)
		return (MIGERR_NONFSINST);

	fse = fsh_get_ent(rip, vp->v_vfsp->vfs_fsid);
	if (fse == NULL) {
		rfs_inst_active_rele(rip);
		return (MIGERR_FSNOENT);
	}
	mutex_enter(&fse->fse_lock);
	if (fse->fse_state & state) {
		status = MIGERR_ALREADY;
		goto out;
	}

	/*
	 * Ensure that the file system is not frozen (frozen bit not set) if
	 * we are about to mark the file system as AVAILABLE. This can happen
	 * in a single stepping mode with a sequence that misses thaw, e.g.,
	 * freeze (source), convert (source), unconvert (source).
	 */
	if (state == FSE_AVAILABLE) {
		if (fse->fse_state & FSE_FROZEN) {
			status =  MIGERR_FSFROZEN;
			goto out;
		}
		fse->fse_state = state;
	} else if (state == FSE_MOVED) {
		if (!(fse->fse_state & FSE_FROZEN)) {
			status =  MIGERR_FSNOTFROZEN;
			goto out;
		}
		/* turn off TSM, turn on MOVED */
		fse->fse_state &= ~FSE_TSM;
		fse->fse_state |= state;
	}

out:
	mutex_exit(&fse->fse_lock);
	fsh_ent_rele(rip, fse);
	rfs_inst_active_rele(rip);

	return (status);
}

migerr_t
rfs4_fse_thaw_fsid(vnode_t *vp)
{
	fsh_entry_t *fse;
	rfs_inst_t *rip = rfs_inst_find(FALSE);

	if (rip == NULL)
		return (MIGERR_NONFSINST);

	fse = fsh_get_ent(rip, vp->v_vfsp->vfs_fsid);
	if (fse == NULL) {
		DTRACE_PROBE1(nfsmig__e__fse_thaw_fsid, fsid_t *,
		    &vp->v_vfsp->vfs_fsid);
		rfs_inst_active_rele(rip);
		return (MIGERR_FSNOENT);
	}

	mutex_enter(&fse->fse_lock);
	if (nfs_rw_lock_held(&fse->fse_freeze_lock, RW_WRITER) == TRUE) {
		nfs_rw_exit(&fse->fse_freeze_lock);

		/* Clean out old state on the source. */
		if (fse->fse_state & FSE_MOVED) {
			/* remove FROZEN bit */
			fse->fse_state = FSE_MOVED;
			mutex_exit(&fse->fse_lock);
			rfs4_clean_state_fse(fse);
		} else {
			fse->fse_state = FSE_AVAILABLE;
			mutex_exit(&fse->fse_lock);
		}
	} else {
		mutex_exit(&fse->fse_lock);
		fsh_ent_rele(rip, fse);
		rfs_inst_active_rele(rip);
		return (MIGERR_ALREADY);
	}
	fsh_ent_rele(rip, fse);
	rfs_inst_active_rele(rip);

	return (MIG_OK);
}


/*
 * If we can grab the read/write lock as a reader, then
 * the export is not frozen.
 *
 * The caller is responsible for releasing the reader lock.
 */
int
rfs4_fse_is_frozen(fsh_entry_t *fse)
{
	/*
	 * Need to hold the fse_lock before calling nfs_rw_tryenter to
	 * synchronize with another thread that may call nfs_rw_lock_held.
	 */
	mutex_enter(&fse->fse_lock);
	if (!nfs_rw_tryenter(&fse->fse_freeze_lock, RW_READER)) {
		mutex_exit(&fse->fse_lock);
		return (TRUE);
	}
	mutex_exit(&fse->fse_lock);

	return (FALSE);
}

void
rfs4_fse_release_reader(fsh_entry_t *fse)
{
	ASSERT(fse != NULL);

	nfs_rw_exit(&fse->fse_freeze_lock);
}
