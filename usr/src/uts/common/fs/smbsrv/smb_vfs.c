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
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/vfs.h>
#include <smbsrv/smb_ktypes.h>
#include <smbsrv/smb_kproto.h>

static smb_vfs_t *smb_vfs_find(smb_shrmgr_t *, vfs_t *);
static void smb_vfs_destroy(smb_shrmgr_t *, smb_vfs_t *);

/*
 * If a hold on the specified VFS has already been taken
 * then only increment the reference count of the corresponding
 * smb_vfs_t structure. If no smb_vfs_t structure has been created
 * yet for the specified VFS then create one and take a hold on
 * the VFS.
 */
smb_vfs_t *
smb_vfs_hold(smb_shrmgr_t *shrmgr, vfs_t *vfsp, int *err)
{
	smb_vfs_t	*smb_vfs;
	vnode_t 	*rootvp;

	ASSERT(shrmgr);
	ASSERT(vfsp);

	smb_llist_enter(&shrmgr->sm_vfs_list, RW_WRITER);

	if ((smb_vfs = smb_vfs_find(shrmgr, vfsp)) != NULL) {
		smb_vfs->sv_refcnt++;
		DTRACE_PROBE1(smb__vfs__hold__hit, smb_vfs_t *, smb_vfs);
		smb_llist_exit(&shrmgr->sm_vfs_list);
		return (smb_vfs);
	}

	if ((*err = VFS_ROOT(vfsp, &rootvp)) != 0) {
		smb_llist_exit(&shrmgr->sm_vfs_list);
		return (NULL);
	}

	smb_vfs = kmem_cache_alloc(shrmgr->sm_cache_vfs, KM_SLEEP);

	bzero(smb_vfs, sizeof (smb_vfs_t));

	smb_vfs->sv_magic = SMB_VFS_MAGIC;
	smb_vfs->sv_refcnt = 1;
	smb_vfs->sv_vfsp = vfsp;
	/*
	 * We have a hold on the root vnode of the file system
	 * from the VFS_ROOT call above.
	 */
	smb_vfs->sv_rootvp = rootvp;

	smb_llist_insert_head(&shrmgr->sm_vfs_list, smb_vfs);
	DTRACE_PROBE1(smb__vfs__hold__miss, smb_vfs_t *, smb_vfs);
	smb_llist_exit(&shrmgr->sm_vfs_list);

	return (smb_vfs);
}

/*
 * smb_vfs_rele
 *
 * Decrements the reference count of the fs passed in. If the reference count
 * drops to zero the smb_vfs_t structure associated with the fs is freed.
 */
void
smb_vfs_rele(smb_shrmgr_t *shrmgr, smb_vfs_t *smb_vfs)
{
	ASSERT(smb_vfs);
	ASSERT(smb_vfs->sv_magic == SMB_VFS_MAGIC);

	DTRACE_PROBE1(smb__vfs__release, smb_vfs_t *, smb_vfs);

	smb_llist_enter(&shrmgr->sm_vfs_list, RW_WRITER);
	ASSERT(smb_vfs->sv_refcnt > 0);
	if (--smb_vfs->sv_refcnt == 0) {
		smb_llist_remove(&shrmgr->sm_vfs_list, smb_vfs);
		smb_llist_exit(&shrmgr->sm_vfs_list);
		smb_vfs_destroy(shrmgr, smb_vfs);
		return;
	}
	smb_llist_exit(&shrmgr->sm_vfs_list);
}

/*
 * smb_vfs_rele_all()
 *
 * Release all holds on root vnodes of file systems which were taken
 * due to the existence of at least one enabled share on the file system.
 * Called at driver close time.
 */
void
smb_vfs_rele_all(smb_shrmgr_t *shrmgr)
{
	smb_vfs_t	*smb_vfs;

	smb_llist_enter(&shrmgr->sm_vfs_list, RW_WRITER);
	while ((smb_vfs = smb_llist_head(&shrmgr->sm_vfs_list)) != NULL) {

		ASSERT(smb_vfs->sv_magic == SMB_VFS_MAGIC);
		DTRACE_PROBE1(smb__vfs__rele__all__hit, smb_vfs_t *, smb_vfs);
		smb_llist_remove(&shrmgr->sm_vfs_list, smb_vfs);
		smb_vfs_destroy(shrmgr, smb_vfs);
	}
	smb_llist_exit(&shrmgr->sm_vfs_list);
}

/*
 * Goes through the list of smb_vfs_t structure and returns the one matching
 * the vnode passed in. If no match is found a NULL pointer is returned.
 *
 * The list of smb_vfs_t structures has to have been entered prior calling
 * this function.
 */
static smb_vfs_t *
smb_vfs_find(smb_shrmgr_t *shrmgr, vfs_t *vfsp)
{
	smb_vfs_t *smb_vfs;

	smb_vfs = smb_llist_head(&shrmgr->sm_vfs_list);
	while (smb_vfs) {
		ASSERT(smb_vfs->sv_magic == SMB_VFS_MAGIC);
		if (smb_vfs->sv_vfsp == vfsp)
			return (smb_vfs);
		smb_vfs = smb_llist_next(&shrmgr->sm_vfs_list, smb_vfs);
	}

	return (NULL);
}

static void
smb_vfs_destroy(smb_shrmgr_t *shrmgr, smb_vfs_t *smb_vfs)
{
	VN_RELE(smb_vfs->sv_rootvp);
	smb_vfs->sv_magic = (uint32_t)~SMB_VFS_MAGIC;
	kmem_cache_free(shrmgr->sm_cache_vfs, smb_vfs);
}
