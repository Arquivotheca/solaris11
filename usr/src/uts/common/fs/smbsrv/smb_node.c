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
/*
 * SMB Node State Machine
 * ----------------------
 *
 *
 *		    +----------- Creation/Allocation
 *		    |
 *		    | T0
 *		    |
 *		    v
 *    +----------------------------+
 *    |  SMB_NODE_STATE_AVAILABLE  |
 *    +----------------------------+
 *		    |
 *		    | T1
 *		    |
 *		    v
 *    +-----------------------------+
 *    |  SMB_NODE_STATE_DESTROYING  |
 *    +-----------------------------+
 *		    |
 *		    |
 *		    | T2
 *		    |
 *		    +----------> Deletion/Free
 *
 * Transition T0
 *
 *    This transition occurs in smb_node_lookup(). If the node looked for is
 *    not found in the has table a new node is created. The reference count is
 *    initialized to 1 and the state initialized to SMB_NODE_STATE_AVAILABLE.
 *
 * Transition T1
 *
 *    This transition occurs in smb_node_release(). If the reference count
 *    drops to zero the state is moved to SMB_NODE_STATE_DESTROYING and no more
 *    reference count will be given out for that node.
 *
 * Transition T2
 *
 *    This transition occurs in smb_node_release(). The structure is deleted.
 *
 * Comments
 * --------
 *
 *    The reason the smb node has 2 states is the following synchronization
 *    rule:
 *
 *    There's a mutex embedded in the node used to protect its fields and
 *    there's a lock embedded in the bucket of the hash table the node belongs
 *    to. To increment or to decrement the reference count the mutex must be
 *    entered. To insert the node into the bucket and to remove it from the
 *    bucket the lock must be entered in RW_WRITER mode. When both (mutex and
 *    lock) have to be entered, the lock has always to be entered first then
 *    the mutex. This prevents a deadlock between smb_node_lookup() and
 *    smb_node_release() from occurring. However, in smb_node_release() when the
 *    reference count drops to zero and triggers the deletion of the node, the
 *    mutex has to be released before entering the lock of the bucket (to
 *    remove the node). This creates a window during which the node that is
 *    about to be freed could be given out by smb_node_lookup(). To close that
 *    window the node is moved to the state SMB_NODE_STATE_DESTROYING before
 *    releasing the mutex. That way, even if smb_node_lookup() finds it, the
 *    state will indicate that the node should be treated as non existent (of
 *    course the state of the node should be tested/updated under the
 *    protection of the mutex).
 */
#include <smbsrv/smb_kproto.h>
#include <smbsrv/smb_fsops.h>
#include <smbsrv/smb_kstat.h>
#include <sys/pathname.h>
#include <sys/sdt.h>
#include <sys/nbmlock.h>
#include <fs/fs_reparse.h>

uint32_t smb_is_executable(char *);
static void smb_node_delete_on_close(smb_node_t *);
static void smb_node_create_audit_buf(smb_node_t *, int);
static void smb_node_destroy_audit_buf(smb_node_t *);
static void smb_node_audit(smb_node_t *);
static smb_node_t *smb_node_alloc(char *, vnode_t *, smb_llist_t *, uint32_t);
static void smb_node_free(smb_node_t *);
static int smb_node_constructor(void *, void *, int);
static void smb_node_destructor(void *, void *);
static smb_llist_t *smb_node_get_hash(fsid_t *, smb_attr_t *, uint32_t *);

static void smb_node_init_cached_data(smb_node_t *);
static void smb_node_clear_cached_data(smb_node_t *);

static void smb_node_init_cached_timestamps(smb_node_t *, smb_attr_t *);
static void smb_node_clear_cached_timestamps(smb_node_t *);
static void smb_node_get_cached_timestamps(smb_node_t *, smb_attr_t *);
static void smb_node_set_cached_timestamps(smb_node_t *, smb_attr_t *);

static void smb_node_init_cached_allocsz(smb_node_t *, smb_attr_t *);
static void smb_node_clear_cached_allocsz(smb_node_t *);
static void smb_node_get_cached_allocsz(smb_node_t *, smb_attr_t *);
static void smb_node_set_cached_allocsz(smb_node_t *, smb_attr_t *);
static void smb_node_init_reparse(smb_node_t *, smb_attr_t *);
static void smb_node_init_system(smb_node_t *);

static void smb_node_notify_ofiles(smb_node_t *);

#define	VALIDATE_DIR_NODE(_dir_, _node_) \
    ASSERT((_dir_)->n_magic == SMB_NODE_MAGIC); \
    ASSERT(((_dir_)->vp->v_xattrdir) || ((_dir_)->vp->v_type == VDIR)); \
    ASSERT((_dir_)->n_dnode != (_node_));

/* round sz to DEV_BSIZE block */
#define	SMB_ALLOCSZ(sz)	(((sz) + DEV_BSIZE-1) & ~(DEV_BSIZE-1))

static kmem_cache_t	*smb_node_cache = NULL;
static boolean_t	smb_node_initialized = B_FALSE;
static smb_llist_t	smb_node_hash_table[SMBND_HASH_MASK+1];

/*
 * smb_node_init
 *
 * Initialization of the SMB node layer.
 *
 * This function is not multi-thread safe. The caller must make sure only one
 * thread makes the call.
 */
int
smb_node_init(void)
{
	int	i;

	if (smb_node_initialized)
		return (0);
	smb_node_cache = kmem_cache_create(SMBSRV_KSTAT_NODE_CACHE,
	    sizeof (smb_node_t), 8, smb_node_constructor, smb_node_destructor,
	    NULL, NULL, NULL, 0);

	for (i = 0; i <= SMBND_HASH_MASK; i++) {
		smb_llist_constructor(&smb_node_hash_table[i],
		    sizeof (smb_node_t), offsetof(smb_node_t, n_lnd));
	}
	smb_node_initialized = B_TRUE;
	return (0);
}

/*
 * smb_node_fini
 *
 * This function is not multi-thread safe. The caller must make sure only one
 * thread makes the call.
 */
void
smb_node_fini(void)
{
	int	i;

	if (!smb_node_initialized)
		return;

#ifdef DEBUG
	for (i = 0; i <= SMBND_HASH_MASK; i++) {
		smb_node_t	*node;

		/*
		 * The following sequence is just intended for sanity check.
		 * This will have to be modified when the code goes into
		 * production.
		 *
		 * The SMB node hash table should be emtpy at this point. If the
		 * hash table is not empty a panic will be triggered.
		 *
		 * The reason why SMB nodes are still remaining in the hash
		 * table is probably due to a mismatch between calls to
		 * smb_node_lookup() and smb_node_release(). You must track that
		 * down.
		 */
		node = smb_llist_head(&smb_node_hash_table[i]);
		ASSERT(node == NULL);
	}
#endif

	for (i = 0; i <= SMBND_HASH_MASK; i++) {
		smb_llist_destructor(&smb_node_hash_table[i]);
	}
	kmem_cache_destroy(smb_node_cache);
	smb_node_cache = NULL;
	smb_node_initialized = B_FALSE;
}

/*
 * smb_node_lookup()
 *
 * NOTE: This routine should only be called by the file system interface layer,
 * and not by SMB.
 *
 * smb_node_lookup() is called upon successful lookup, mkdir, and create
 * (for both non-streams and streams).  In each of these cases, a held vnode is
 * passed into this routine.  If a new smb_node is created it will take its
 * own hold on the vnode.  The caller's hold therefore still belongs to, and
 * should be released by, the caller.
 *
 * A reference is taken on the smb_node whether found in the hash table
 * or newly created.
 *
 * If an smb_node needs to be created, a reference is also taken on the
 * dnode (if passed in).
 *
 * See smb_node_release() for details on the release of these references.
 */

/*ARGSUSED*/
smb_node_t *
smb_node_lookup(
    struct smb_request	*sr,
    struct open_param	*op,
    cred_t		*cred,
    vnode_t		*vp,
    char		*od_name,
    smb_node_t		*dnode,
    smb_node_t		*unode)
{
	smb_llist_t		*node_hdr;
	smb_node_t		*node;
	smb_attr_t		attr;
	uint32_t		hashkey = 0;
	fsid_t			fsid;
	int			error;
	krw_t			lock_mode;
	vnode_t			*unnamed_vp = NULL;

	/*
	 * smb_vop_getattr() is called here instead of smb_fsop_getattr(),
	 * because the node may not yet exist.  We also do not want to call
	 * it with the list lock held.
	 */

	if (unode)
		unnamed_vp = unode->vp;

	/*
	 * This getattr is performed on behalf of the server
	 * that's why kcred is used not the user's cred
	 */
	attr.sa_mask = SMB_AT_ALL;
	error = smb_vop_getattr(vp, unnamed_vp, &attr, 0, kcred);
	if (error)
		return (NULL);

	if (sr && sr->tid_tree) {
		/*
		 * The fsid for a file is that of the tree, even
		 * if the file resides in a different mountpoint
		 * under the share.
		 */
		fsid = SMB_TREE_FSID(sr->tid_tree);
	} else {
		/*
		 * This should be getting executed only for the
		 * tree root smb_node.
		 */
		fsid = vp->v_vfsp->vfs_fsid;
	}

	node_hdr = smb_node_get_hash(&fsid, &attr, &hashkey);
	lock_mode = RW_READER;

	smb_llist_enter(node_hdr, lock_mode);
	for (;;) {
		node = list_head(&node_hdr->ll_list);
		while (node) {
			ASSERT(node->n_magic == SMB_NODE_MAGIC);
			ASSERT(node->n_hash_bucket == node_hdr);
			if ((node->n_hashkey == hashkey) && (node->vp == vp)) {
				mutex_enter(&node->n_mutex);
				DTRACE_PROBE1(smb_node_lookup_hit,
				    smb_node_t *, node);
				switch (node->n_state) {
				case SMB_NODE_STATE_AVAILABLE:
					/* The node was found. */
					node->n_refcnt++;
					if ((node->n_dnode == NULL) &&
					    (dnode != NULL) &&
					    (node != dnode) &&
					    (strcmp(od_name, "..") != 0) &&
					    (strcmp(od_name, ".") != 0)) {
						VALIDATE_DIR_NODE(dnode, node);
						node->n_dnode = dnode;
						smb_node_ref(dnode);
					}

					smb_node_audit(node);
					mutex_exit(&node->n_mutex);
					smb_llist_exit(node_hdr);
					return (node);

				case SMB_NODE_STATE_DESTROYING:
					/*
					 * Although the node exists it is about
					 * to be destroyed. We act as it hasn't
					 * been found.
					 */
					mutex_exit(&node->n_mutex);
					break;
				default:
					/*
					 * Although the node exists it is in an
					 * unknown state. We act as it hasn't
					 * been found.
					 */
					ASSERT(0);
					mutex_exit(&node->n_mutex);
					break;
				}
			}
			node = smb_llist_next(node_hdr, node);
		}
		if ((lock_mode == RW_READER) && smb_llist_upgrade(node_hdr)) {
			lock_mode = RW_WRITER;
			continue;
		}
		break;
	}
	node = smb_node_alloc(od_name, vp, node_hdr, hashkey);
	smb_node_init_reparse(node, &attr);

	if (op)
		node->flags |= smb_is_executable(op->fqi.fq_last_comp);

	if (dnode) {
		smb_node_ref(dnode);
		node->n_dnode = dnode;
		ASSERT(dnode->n_dnode != node);
		ASSERT((dnode->vp->v_xattrdir) ||
		    (dnode->vp->v_type == VDIR));
	}

	if (unode) {
		smb_node_ref(unode);
		node->n_unode = unode;
	}

	smb_node_init_system(node);

	DTRACE_PROBE1(smb_node_lookup_miss, smb_node_t *, node);
	smb_node_audit(node);
	smb_llist_insert_head(node_hdr, node);
	smb_llist_exit(node_hdr);
	return (node);
}

/*
 * smb_stream_node_lookup()
 *
 * Note: stream_name (the name that will be stored in the "od_name" field
 * of a stream's smb_node) is the same as the on-disk name for the stream
 * except that it does not have SMB_STREAM_PREFIX prepended.
 */

smb_node_t *
smb_stream_node_lookup(smb_request_t *sr, cred_t *cr, smb_node_t *fnode,
    vnode_t *xattrdirvp, vnode_t *vp, char *stream_name)
{
	smb_node_t	*xattrdir_node;
	smb_node_t	*snode;

	xattrdir_node = smb_node_lookup(sr, NULL, cr, xattrdirvp, XATTR_DIR,
	    fnode, NULL);

	if (xattrdir_node == NULL)
		return (NULL);

	snode = smb_node_lookup(sr, NULL, cr, vp, stream_name, xattrdir_node,
	    fnode);

	(void) smb_node_release(xattrdir_node);
	return (snode);
}


/*
 * This function should be called whenever a reference is needed on an
 * smb_node pointer.  The copy of an smb_node pointer from one non-local
 * data structure to another requires a reference to be taken on the smb_node
 * (unless the usage is localized).  Each data structure deallocation routine
 * will call smb_node_release() on its smb_node pointers.
 *
 * In general, an smb_node pointer residing in a structure should never be
 * stale.  A node pointer may be NULL, however, and care should be taken
 * prior to calling smb_node_ref(), which ASSERTs that the pointer is valid.
 * Care also needs to be taken with respect to racing deallocations of a
 * structure.
 */
void
smb_node_ref(smb_node_t *node)
{
	SMB_NODE_VALID(node);

	mutex_enter(&node->n_mutex);
	switch (node->n_state) {
	case SMB_NODE_STATE_AVAILABLE:
		node->n_refcnt++;
		ASSERT(node->n_refcnt);
		DTRACE_PROBE1(smb_node_ref_exit, smb_node_t *, node);
		smb_node_audit(node);
		break;
	default:
		SMB_PANIC();
	}
	mutex_exit(&node->n_mutex);
}

/*
 * smb_node_lookup() takes a hold on an smb_node, whether found in the
 * hash table or newly created.  This hold is expected to be released
 * in the following manner.
 *
 * smb_node_lookup() takes an address of an smb_node pointer.  This should
 * be getting passed down via a lookup (whether path name or component), mkdir,
 * create.  If the original smb_node pointer resides in a data structure, then
 * the deallocation routine for the data structure is responsible for calling
 * smb_node_release() on the smb_node pointer.  Alternatively,
 * smb_node_release() can be called as soon as the smb_node pointer is no longer
 * needed.  In this case, callers are responsible for setting an embedded
 * pointer to NULL if it is known that the last reference is being released.
 *
 * If the passed-in address of the smb_node pointer belongs to a local variable,
 * then the caller with the local variable should call smb_node_release()
 * directly.
 *
 * smb_node_release() itself will call smb_node_release() on a node's n_dnode,
 * as smb_node_lookup() takes a hold on dnode.
 */
void
smb_node_release(smb_node_t *node)
{
	SMB_NODE_VALID(node);

	mutex_enter(&node->n_mutex);
	ASSERT(node->n_refcnt);
	DTRACE_PROBE1(smb_node_release, smb_node_t *, node);
	if (--node->n_refcnt == 0) {
		switch (node->n_state) {

		case SMB_NODE_STATE_AVAILABLE:
			node->n_state = SMB_NODE_STATE_DESTROYING;
			mutex_exit(&node->n_mutex);

			smb_llist_enter(node->n_hash_bucket, RW_WRITER);
			smb_llist_remove(node->n_hash_bucket, node);
			smb_llist_exit(node->n_hash_bucket);

			/*
			 * Check if the file was deleted
			 */
			smb_node_delete_on_close(node);

			if (node->n_dnode) {
				ASSERT(node->n_dnode->n_magic ==
				    SMB_NODE_MAGIC);
				smb_node_release(node->n_dnode);
			}

			if (node->n_unode) {
				ASSERT(node->n_unode->n_magic ==
				    SMB_NODE_MAGIC);
				smb_node_release(node->n_unode);
			}

			smb_node_free(node);
			return;

		default:
			SMB_PANIC();
		}
	}
	smb_node_audit(node);
	mutex_exit(&node->n_mutex);
}

static void
smb_node_delete_on_close(smb_node_t *node)
{
	smb_node_t	*d_snode;
	int		rc = 0;
	uint32_t	flags = 0;
	boolean_t	isdir;

	d_snode = node->n_dnode;
	if (node->flags & NODE_FLAGS_DELETE_ON_CLOSE) {
		node->flags &= ~NODE_FLAGS_DELETE_ON_CLOSE;
		flags = node->n_delete_on_close_flags;
		ASSERT(node->od_name != NULL);

		isdir = smb_node_is_dir(node);
		if (isdir) {
			rc = smb_fsop_rmdir(0, node->delete_on_close_cred,
			    d_snode, node->od_name, flags);
		} else {
			rc = smb_fsop_remove(0, node->delete_on_close_cred,
			    d_snode, node->od_name, flags);
		}
		crfree(node->delete_on_close_cred);

		if (rc != 0) {
			cmn_err(CE_WARN, "%s %s could not be removed, rc=%d\n",
			    isdir ? "Directory" : "File", node->od_name, rc);
		}
	}
	DTRACE_PROBE2(smb_node_delete_on_close, int, rc, smb_node_t *, node);
}

boolean_t
smb_node_delete_pending(smb_node_t *node)
{
	if (node->flags & NODE_FLAGS_DELETE_ON_CLOSE)
		return (B_TRUE);

	if (SMB_IS_STREAM(node) &&
	    (node->n_unode->flags & NODE_FLAGS_DELETE_ON_CLOSE))
		return (B_TRUE);

	return (B_FALSE);
}

/*
 * smb_node_rename()
 *
 */
void
smb_node_rename(
    smb_node_t	*from_dnode,
    smb_node_t	*ret_node,
    smb_node_t	*to_dnode,
    char	*to_name)
{
	SMB_NODE_VALID(from_dnode);
	SMB_NODE_VALID(to_dnode);
	SMB_NODE_VALID(ret_node);

	smb_node_ref(to_dnode);
	mutex_enter(&ret_node->n_mutex);
	switch (ret_node->n_state) {
	case SMB_NODE_STATE_AVAILABLE:
		ret_node->n_dnode = to_dnode;
		mutex_exit(&ret_node->n_mutex);
		ASSERT(to_dnode->n_dnode != ret_node);
		ASSERT((to_dnode->vp->v_xattrdir) ||
		    (to_dnode->vp->v_type == VDIR));
		smb_node_release(from_dnode);
		(void) strcpy(ret_node->od_name, to_name);
		/*
		 * XXX Need to update attributes?
		 */
		break;
	default:
		SMB_PANIC();
	}
}

int
smb_node_root_init(vnode_t *vp, smb_server_t *sv, smb_node_t **root)
{
	smb_attr_t	attr;
	int		error;
	uint32_t	hashkey;
	smb_llist_t	*node_hdr;
	smb_node_t	*node;

	attr.sa_mask = SMB_AT_ALL;
	error = smb_vop_getattr(vp, NULL, &attr, 0, kcred);
	if (error) {
		VN_RELE(vp);
		return (error);
	}

	node_hdr = smb_node_get_hash(&vp->v_vfsp->vfs_fsid, &attr, &hashkey);

	node = smb_node_alloc(ROOTVOL, vp, node_hdr, hashkey);

	sv->si_root_smb_node = node;
	smb_node_audit(node);
	smb_llist_enter(node_hdr, RW_WRITER);
	smb_llist_insert_head(node_hdr, node);
	smb_llist_exit(node_hdr);
	*root = node;
	return (0);
}

/*
 * When DeleteOnClose is set on an smb_node, the common open code will
 * reject subsequent open requests for the file. Observation of Windows
 * 2000 indicates that subsequent opens should be allowed (assuming
 * there would be no sharing violation) until the file is closed using
 * the fid on which the DeleteOnClose was requested.
 *
 * If there are multiple opens with delete-on-close create options,
 * whichever the first file handle is closed will trigger the node to be
 * marked as delete-on-close. The credentials of that ofile will be used
 * as the delete-on-close credentials of the node.
 *
 * We do not allow delete-on-close to be set on a directory that is not
 * empty. A ZFS directory is considered not empty if it contains any
 * entries other than the "." and ".." entries, i.e. if its size is > 2.
 * This check is currently only supported for ZFS nodes.
 */
int
smb_node_set_delete_on_close(smb_node_t *node, cred_t *cr, uint32_t flags)
{
	int rc = 0;
	int fstype;
	char *fsname;
	smb_attr_t attr;

	if (node->readonly_creator)
		return (EACCES);

	bzero(&attr, sizeof (smb_attr_t));
	attr.sa_mask = SMB_AT_DOSATTR;
	rc = smb_fsop_getattr(NULL, kcred, node, &attr);
	if ((rc != 0) || (attr.sa_dosattr & FILE_ATTRIBUTE_READONLY))
		return (EACCES);

	if (smb_node_is_dir(node) && (attr.sa_vattr.va_size > 2)) {
		fstype = node->vp->v_vfsp->vfs_fstype;
		fsname = vfssw[fstype].vsw_name;
		if (strcasecmp(fsname, MNTTYPE_ZFS) == 0)
			return (ENOTEMPTY);
	}

	mutex_enter(&node->n_mutex);
	if ((node->flags & NODE_FLAGS_DELETE_ON_CLOSE) == 0) {
		crhold(cr);
		node->delete_on_close_cred = cr;
		node->n_delete_on_close_flags = flags;
		node->flags |= NODE_FLAGS_DELETE_ON_CLOSE;
	}
	mutex_exit(&node->n_mutex);
	return (0);
}

void
smb_node_reset_delete_on_close(smb_node_t *node)
{
	mutex_enter(&node->n_mutex);
	if (node->flags & NODE_FLAGS_DELETE_ON_CLOSE) {
		node->flags &= ~NODE_FLAGS_DELETE_ON_CLOSE;
		crfree(node->delete_on_close_cred);
		node->delete_on_close_cred = NULL;
		node->n_delete_on_close_flags = 0;
	}
	mutex_exit(&node->n_mutex);
}

/*
 * smb_node_open_check
 *
 * check file sharing rules for current open request
 * against all existing opens for a file.
 *
 * Returns NT_STATUS_SHARING_VIOLATION if there is any
 * sharing conflict, otherwise returns NT_STATUS_SUCCESS.
 */
uint32_t
smb_node_open_check(smb_node_t *node, uint32_t desired_access,
    uint32_t share_access)
{
	smb_ofile_t *of;
	uint32_t status;

	SMB_NODE_VALID(node);

	smb_llist_enter(&node->n_ofile_list, RW_READER);
	of = smb_llist_head(&node->n_ofile_list);
	while (of) {
		status = smb_ofile_open_check(of, desired_access, share_access);

		switch (status) {
		case NT_STATUS_INVALID_HANDLE:
		case NT_STATUS_SUCCESS:
			of = smb_llist_next(&node->n_ofile_list, of);
			break;
		default:
			ASSERT(status == NT_STATUS_SHARING_VIOLATION);
			smb_llist_exit(&node->n_ofile_list);
			return (status);
		}
	}

	smb_llist_exit(&node->n_ofile_list);
	return (NT_STATUS_SUCCESS);
}

uint32_t
smb_node_rename_check(smb_node_t *node)
{
	smb_ofile_t	*of;
	uint32_t	status;

	SMB_NODE_VALID(node);

	/*
	 * Intra-CIFS check
	 */
	smb_llist_enter(&node->n_ofile_list, RW_READER);
	of = smb_llist_head(&node->n_ofile_list);
	while (of) {
		status = smb_ofile_rename_check(of);

		switch (status) {
		case NT_STATUS_INVALID_HANDLE:
		case NT_STATUS_SUCCESS:
			of = smb_llist_next(&node->n_ofile_list, of);
			break;
		default:
			ASSERT(status == NT_STATUS_SHARING_VIOLATION);
			smb_llist_exit(&node->n_ofile_list);
			return (status);
		}
	}
	smb_llist_exit(&node->n_ofile_list);

	/*
	 * system-wide share check
	 */
	if (nbl_share_conflict(node->vp, NBL_RENAME, NULL))
		return (NT_STATUS_SHARING_VIOLATION);
	else
		return (NT_STATUS_SUCCESS);
}

uint32_t
smb_node_delete_check(smb_node_t *node)
{
	smb_ofile_t	*of;
	uint32_t	status;

	SMB_NODE_VALID(node);

	if (smb_node_is_dir(node))
		return (NT_STATUS_SUCCESS);

	if (smb_node_is_reparse(node))
		return (NT_STATUS_ACCESS_DENIED);

	/*
	 * intra-CIFS check
	 */
	smb_llist_enter(&node->n_ofile_list, RW_READER);
	of = smb_llist_head(&node->n_ofile_list);
	while (of) {
		status = smb_ofile_delete_check(of);

		switch (status) {
		case NT_STATUS_INVALID_HANDLE:
		case NT_STATUS_SUCCESS:
			of = smb_llist_next(&node->n_ofile_list, of);
			break;
		default:
			ASSERT(status == NT_STATUS_SHARING_VIOLATION);
			smb_llist_exit(&node->n_ofile_list);
			return (status);
		}
	}
	smb_llist_exit(&node->n_ofile_list);

	/*
	 * system-wide share check
	 */
	if (nbl_share_conflict(node->vp, NBL_REMOVE, NULL))
		return (NT_STATUS_SHARING_VIOLATION);
	else
		return (NT_STATUS_SUCCESS);
}

/*
 * smb_node_share_check
 *
 * Returns: TRUE    - ofiles have non-zero share access
 *          B_FALSE - ofile with share access NONE.
 */
boolean_t
smb_node_share_check(smb_node_t *node)
{
	smb_ofile_t	*of;
	boolean_t	status = B_TRUE;

	SMB_NODE_VALID(node);

	smb_llist_enter(&node->n_ofile_list, RW_READER);
	of = smb_llist_head(&node->n_ofile_list);
	if (of)
		status = smb_ofile_share_check(of);
	smb_llist_exit(&node->n_ofile_list);

	return (status);
}


/*
 * smb_node_fcn_inc
 *
 * Increment the count of ofiles monitoring for changes on
 * the node. If this is the first, install the fem monitor
 * on the node.
 * The node's n_mutex cannot be used to protect this operation
 * as it is entered by smb_node_ref() which is used by the
 * fem monitor. Thus n_fcn_mutex is required.
 */
void
smb_node_fcn_inc(smb_node_t *node)
{
	mutex_enter(&node->n_fcn_mutex);
	if (++node->n_fcn_count == 1)
		smb_fem_fcn_install(node);
	mutex_exit(&node->n_fcn_mutex);
}

/*
 * smb_node_fcn_dec
 *
 * Decrement the count of ofiles monitoring for changes on
 * the node. If this is the last, uninstall the fem monitor
 * on the node.
 * The node's n_mutex cannot be used to protect this operation
 * as it is entered by smb_node_release() which is used by the
 * fem monitor. Thus n_fcn_mutex is required.
 */
void
smb_node_fcn_dec(smb_node_t *node)
{
	mutex_enter(&node->n_fcn_mutex);
	ASSERT(node->n_fcn_count > 0);
	if (--node->n_fcn_count == 0)
		smb_fem_fcn_uninstall(node);
	mutex_exit(&node->n_fcn_mutex);
}

/*
 * smb_node_notify_change
 *
 * A change, of a type which a client may monitor, has occurred
 * on the node. Inform the node's open ofiles and the node's
 * parents.
 */
void
smb_node_notify_change(smb_node_t *node)
{
	SMB_NODE_VALID(node);

	smb_node_notify_ofiles(node);
	smb_node_notify_parents(node);
}

/*
 * smb_node_notify_ofiles
 *
 * Notify all ofiles on the node that something has changed.
 */
static void
smb_node_notify_ofiles(smb_node_t *node)
{
	smb_ofile_t *of;

	smb_llist_enter(&node->n_ofile_list, RW_READER);
	of = smb_llist_head(&node->n_ofile_list);
	while (of) {
		smb_ofile_notify_change(of);
		of = smb_llist_next(&node->n_ofile_list, of);
	}
	smb_llist_exit(&node->n_ofile_list);
}

/*
 * smb_node_notify_parents
 *
 * Iterate up the directory tree notifying any parent
 * directories that are being watched for changes in
 * their sub directories.
 * Stop at the root node, which has a NULL parent node.
 */
void
smb_node_notify_parents(smb_node_t *dnode)
{
	smb_node_t *pnode = dnode;

	SMB_NODE_VALID(dnode);

	while ((pnode = pnode->n_dnode) != NULL) {
		SMB_NODE_VALID(pnode);
		smb_node_notify_ofiles(pnode);
	}
}

/*
 * smb_node_start_crit()
 *
 * Enter critical region for share reservations.
 * See comments above smb_fsop_shrlock().
 */
void
smb_node_start_crit(smb_node_t *node, krw_t mode)
{
	rw_enter(&node->n_lock, mode);
	nbl_start_crit(node->vp, mode);
}

/*
 * smb_node_end_crit()
 *
 * Exit critical region for share reservations.
 */
void
smb_node_end_crit(smb_node_t *node)
{
	nbl_end_crit(node->vp);
	rw_exit(&node->n_lock);
}

int
smb_node_in_crit(smb_node_t *node)
{
	return (nbl_in_crit(node->vp) && RW_LOCK_HELD(&node->n_lock));
}

void
smb_node_rdlock(smb_node_t *node)
{
	rw_enter(&node->n_lock, RW_READER);
}

void
smb_node_wrlock(smb_node_t *node)
{
	rw_enter(&node->n_lock, RW_WRITER);
}

void
smb_node_unlock(smb_node_t *node)
{
	rw_exit(&node->n_lock);
}

void
smb_node_add_ofile(smb_node_t *node, smb_ofile_t *of)
{
	SMB_NODE_VALID(node);

	smb_llist_enter(&node->n_ofile_list, RW_WRITER);
	smb_llist_insert_tail(&node->n_ofile_list, of);
	smb_llist_exit(&node->n_ofile_list);
}

void
smb_node_rem_ofile(smb_node_t *node, smb_ofile_t *of)
{
	SMB_NODE_VALID(node);

	smb_llist_enter(&node->n_ofile_list, RW_WRITER);
	smb_llist_remove(&node->n_ofile_list, of);
	smb_llist_exit(&node->n_ofile_list);
}

/*
 * smb_node_inc_open_ofiles
 */
void
smb_node_inc_open_ofiles(smb_node_t *node)
{
	SMB_NODE_VALID(node);

	mutex_enter(&node->n_mutex);
	node->n_open_count++;
	mutex_exit(&node->n_mutex);

	smb_node_init_cached_data(node);
}

/*
 * smb_node_dec_open_ofiles
 */
void
smb_node_dec_open_ofiles(smb_node_t *node)
{
	SMB_NODE_VALID(node);

	mutex_enter(&node->n_mutex);
	node->n_open_count--;
	mutex_exit(&node->n_mutex);

	smb_node_clear_cached_data(node);
}

/*
 * smb_node_inc_opening_count
 */
void
smb_node_inc_opening_count(smb_node_t *node)
{
	SMB_NODE_VALID(node);
	mutex_enter(&node->n_mutex);
	node->n_opening_count++;
	mutex_exit(&node->n_mutex);
}

/*
 * smb_node_dec_opening_count
 */
void
smb_node_dec_opening_count(smb_node_t *node)
{
	SMB_NODE_VALID(node);
	mutex_enter(&node->n_mutex);
	ASSERT(node->n_opening_count > 0);
	node->n_opening_count--;
	mutex_exit(&node->n_mutex);
}

/*
 * smb_node_getmntpath
 */
int
smb_node_getmntpath(smb_node_t *node, char *buf, uint32_t buflen)
{
	vnode_t *vp, *root_vp;
	vfs_t *vfsp;
	int err;

	ASSERT(node);
	ASSERT(node->vp);
	ASSERT(node->vp->v_vfsp);

	vp = node->vp;
	vfsp = vp->v_vfsp;

	if (VFS_ROOT(vfsp, &root_vp))
		return (ENOENT);

	VN_HOLD(vp);

	/* NULL is passed in as we want to start at "/" */
	err = vnodetopath(NULL, root_vp, buf, buflen, kcred);

	VN_RELE(vp);
	VN_RELE(root_vp);
	return (err);
}

/*
 * smb_node_getshrpath
 *
 * Determine the absolute pathname of 'node' within the share (tree).
 * For example if the node represents file "test1.txt" in directory
 * "dir1" the pathname would be: \dir1\test1.txt
 */
int
smb_node_getshrpath(smb_node_t *node, smb_tree_t *tree,
    char *buf, uint32_t buflen)
{
	int rc;

	ASSERT(node);
	ASSERT(tree);
	ASSERT(tree->t_snode);

	rc = smb_node_getpath(node, tree->t_snode->vp, buf, buflen);
	(void) strsubst(buf, '/', '\\');
	return (rc);
}

/*
 * smb_node_getpath
 *
 * Determine the absolute pathname of 'node' from 'rootvp'.
 *
 * Using vnodetopath is only reliable for directory nodes (due to
 * its reliance on the DNLC for non-directory nodes). Thus, if node
 * represents a file, construct the pathname for the parent dnode
 * and append filename.
 * If node represents a named stream, construct the pathname for the
 * associated unnamed stream and append the stream name.
 *
 * The pathname returned in buf will be '/' separated.
 */
int
smb_node_getpath(smb_node_t *node, vnode_t *rootvp, char *buf, uint32_t buflen)
{
	int rc;
	vnode_t *vp;
	smb_node_t *unode, *dnode;

	unode = (SMB_IS_STREAM(node)) ? node->n_unode : node;
	dnode = (smb_node_is_dir(unode)) ? unode : unode->n_dnode;

	/* find path to directory node */
	vp = dnode->vp;
	VN_HOLD(vp);
	if (rootvp) {
		VN_HOLD(rootvp);
		rc = vnodetopath(rootvp, vp, buf, buflen, kcred);
		VN_RELE(rootvp);
	} else {
		rc = vnodetopath(NULL, vp, buf, buflen, kcred);
	}
	VN_RELE(vp);

	if (rc != 0)
		return (rc);

	/* append filename if necessary */
	if (!smb_node_is_dir(unode)) {
		if (buf[strlen(buf) - 1] != '/')
			(void) strlcat(buf, "/", buflen);
		(void) strlcat(buf, unode->od_name, buflen);
	}

	/* append named stream name if necessary */
	if (SMB_IS_STREAM(node))
		(void) strlcat(buf, node->od_name, buflen);

	return (rc);
}

/*
 * smb_node_alloc
 */
static smb_node_t *
smb_node_alloc(
    char	*od_name,
    vnode_t	*vp,
    smb_llist_t	*bucket,
    uint32_t	hashkey)
{
	smb_node_t	*node;
	vnode_t		*root_vp;

	node = kmem_cache_alloc(smb_node_cache, KM_SLEEP);

	if (node->n_audit_buf != NULL)
		node->n_audit_buf->anb_index = 0;

	node->flags = 0;
	VN_HOLD(vp);
	node->vp = vp;
	node->n_refcnt = 1;
	node->n_hash_bucket = bucket;
	node->n_hashkey = hashkey;
	node->readonly_creator = NULL;
	node->n_fcn_count = 0;
	node->n_open_count = 0;
	node->n_opening_count = 0;
	node->n_dnode = NULL;
	node->n_unode = NULL;
	node->delete_on_close_cred = NULL;
	node->n_delete_on_close_flags = 0;
	node->n_oplock.ol_fem = B_FALSE;
	node->n_oplock.ol_xthread = NULL;
	node->n_oplock.ol_count = 0;
	node->n_oplock.ol_break = SMB_OPLOCK_NO_BREAK;

	(void) strlcpy(node->od_name, od_name, sizeof (node->od_name));
	if (strcmp(od_name, XATTR_DIR) == 0)
		node->flags |= NODE_XATTR_DIR;

	if (VFS_ROOT(vp->v_vfsp, &root_vp) == 0) {
		if (vp == root_vp)
			node->flags |= NODE_FLAGS_VFSROOT;
		VN_RELE(root_vp);
	}

	node->n_state = SMB_NODE_STATE_AVAILABLE;
	node->n_magic = SMB_NODE_MAGIC;

	return (node);
}

/*
 * smb_node_free
 */
static void
smb_node_free(smb_node_t *node)
{
	SMB_NODE_VALID(node);

	node->n_magic = 0;
	VERIFY(!list_link_active(&node->n_lnd));
	VERIFY(node->n_lock_list.ll_count == 0);
	VERIFY(node->n_ofile_list.ll_count == 0);
	VERIFY(node->n_oplock.ol_count == 0);
	VERIFY(node->n_oplock.ol_xthread == NULL);
	VERIFY(node->n_oplock.ol_fem == B_FALSE);
	VERIFY(mutex_owner(&node->n_mutex) == NULL);
	VERIFY(!RW_LOCK_HELD(&node->n_lock));
	VN_RELE(node->vp);
	kmem_cache_free(smb_node_cache, node);
}

/*
 * smb_node_constructor
 */
static int
smb_node_constructor(void *buf, void *un, int kmflags)
{
	_NOTE(ARGUNUSED(kmflags, un))

	smb_node_t	*node = (smb_node_t *)buf;

	bzero(node, sizeof (smb_node_t));

	smb_llist_constructor(&node->n_ofile_list, sizeof (smb_ofile_t),
	    offsetof(smb_ofile_t, f_nnd));
	smb_llist_constructor(&node->n_lock_list, sizeof (smb_lock_t),
	    offsetof(smb_lock_t, l_lnd));
	cv_init(&node->n_oplock.ol_cv, NULL, CV_DEFAULT, NULL);
	mutex_init(&node->n_oplock.ol_mutex, NULL, MUTEX_DEFAULT, NULL);
	list_create(&node->n_oplock.ol_grants, sizeof (smb_oplock_grant_t),
	    offsetof(smb_oplock_grant_t, og_lnd));
	rw_init(&node->n_lock, NULL, RW_DEFAULT, NULL);
	mutex_init(&node->n_mutex, NULL, MUTEX_DEFAULT, NULL);
	smb_node_create_audit_buf(node, kmflags);
	return (0);
}

/*
 * smb_node_destructor
 */
static void
smb_node_destructor(void *buf, void *un)
{
	_NOTE(ARGUNUSED(un))

	smb_node_t	*node = (smb_node_t *)buf;

	smb_node_destroy_audit_buf(node);
	mutex_destroy(&node->n_mutex);
	rw_destroy(&node->n_lock);
	cv_destroy(&node->n_oplock.ol_cv);
	mutex_destroy(&node->n_oplock.ol_mutex);
	smb_llist_destructor(&node->n_lock_list);
	smb_llist_destructor(&node->n_ofile_list);
	list_destroy(&node->n_oplock.ol_grants);
}

/*
 * smb_node_create_audit_buf
 */
static void
smb_node_create_audit_buf(smb_node_t *node, int kmflags)
{
	smb_audit_buf_node_t	*abn;

	if (smb_audit_flags & SMB_AUDIT_NODE) {
		abn = kmem_zalloc(sizeof (smb_audit_buf_node_t), kmflags);
		abn->anb_max_index = SMB_AUDIT_BUF_MAX_REC - 1;
		node->n_audit_buf = abn;
	}
}

/*
 * smb_node_destroy_audit_buf
 */
static void
smb_node_destroy_audit_buf(smb_node_t *node)
{
	if (node->n_audit_buf != NULL) {
		kmem_free(node->n_audit_buf, sizeof (smb_audit_buf_node_t));
		node->n_audit_buf = NULL;
	}
}

/*
 * smb_node_audit
 *
 * This function saves the calling stack in the audit buffer of the node passed
 * in.
 */
static void
smb_node_audit(smb_node_t *node)
{
	smb_audit_buf_node_t	*abn;
	smb_audit_record_node_t	*anr;

	if (node->n_audit_buf) {
		abn = node->n_audit_buf;
		anr = abn->anb_records;
		anr += abn->anb_index;
		abn->anb_index++;
		abn->anb_index &= abn->anb_max_index;
		anr->anr_refcnt = node->n_refcnt;
		anr->anr_depth = getpcstack(anr->anr_stack,
		    SMB_AUDIT_STACK_DEPTH);
	}
}

static smb_llist_t *
smb_node_get_hash(fsid_t *fsid, smb_attr_t *attr, uint32_t *phashkey)
{
	uint32_t	hashkey;

	hashkey = fsid->val[0] + attr->sa_vattr.va_nodeid;
	hashkey += (hashkey >> 24) + (hashkey >> 16) + (hashkey >> 8);
	*phashkey = hashkey;
	return (&smb_node_hash_table[(hashkey & SMBND_HASH_MASK)]);
}

boolean_t
smb_node_is_file(smb_node_t *node)
{
	SMB_NODE_VALID(node);
	return (node->vp->v_type == VREG);
}

boolean_t
smb_node_is_dir(smb_node_t *node)
{
	SMB_NODE_VALID(node);
	return ((node->vp->v_type == VDIR) ||
	    (node->flags & NODE_FLAGS_DFSLINK));
}

boolean_t
smb_node_is_symlink(smb_node_t *node)
{
	SMB_NODE_VALID(node);
	return ((node->vp->v_type == VLNK) &&
	    ((node->flags & NODE_FLAGS_REPARSE) == 0));
}

boolean_t
smb_node_is_dfslink(smb_node_t *node)
{
	SMB_NODE_VALID(node);
	return ((node->vp->v_type == VLNK) &&
	    (node->flags & NODE_FLAGS_DFSLINK));
}

boolean_t
smb_node_is_reparse(smb_node_t *node)
{
	SMB_NODE_VALID(node);
	return ((node->vp->v_type == VLNK) &&
	    (node->flags & NODE_FLAGS_REPARSE));
}

boolean_t
smb_node_is_vfsroot(smb_node_t *node)
{
	SMB_NODE_VALID(node);
	return ((node->flags & NODE_FLAGS_VFSROOT) == NODE_FLAGS_VFSROOT);
}

boolean_t
smb_node_is_system(smb_node_t *node)
{
	SMB_NODE_VALID(node);
	return ((node->flags & NODE_FLAGS_SYSTEM) == NODE_FLAGS_SYSTEM);
}

/*
 * smb_node_file_is_readonly
 *
 * Checks if the file (which node represents) is marked readonly
 * in the filesystem. No account is taken of any pending readonly
 * in the node, which must be handled by the callers.
 * (See SMB_OFILE_IS_READONLY and SMB_PATHFILE_IS_READONLY)
 */
boolean_t
smb_node_file_is_readonly(smb_node_t *node)
{
	smb_attr_t attr;

	if (node == NULL)
		return (B_FALSE);

	bzero(&attr, sizeof (smb_attr_t));
	attr.sa_mask = SMB_AT_DOSATTR;
	(void) smb_fsop_getattr(NULL, kcred, node, &attr);
	return ((attr.sa_dosattr & FILE_ATTRIBUTE_READONLY) != 0);
}

/*
 * smb_node_setattr
 *
 * The sr may be NULL, for example when closing an ofile.
 * The ofile may be NULL, for example when a client request
 * specifies the file by pathname.
 *
 * Timestamps
 * When attributes are set on an ofile, any pending timestamps
 * from a write request on the ofile are implicitly set to "now".
 * For compatibility with windows the following timestamps are
 * also implicitly set to now:
 * - if any attribute is being explicitly set, set ctime to now
 * - if file size is being explicitly set, set atime & ctime to now
 *
 * Any timestamp that is being explicitly set, or has previously
 * been explicitly set on the ofile, is excluded from implicit
 * (now) setting.
 *
 * Updates the node's cached timestamp values.
 * Updates the ofile's explicit times flag.
 *
 * File allocation size
 * When the file allocation size is set it is first rounded up
 * to block size. If the file size is smaller than the allocation
 * size the file is truncated by setting the filesize to allocsz.
 * If there are open ofiles, the allocsz is cached on the node.
 *
 * Updates the node's cached allocsz value.
 *
 * Returns: errno
 */
int
smb_node_setattr(smb_request_t *sr, smb_node_t *node,
    cred_t *cr, smb_ofile_t *of, smb_attr_t *attr)
{
	int rc;
	uint32_t pending_times = 0;
	uint32_t explicit_times = 0;
	timestruc_t now;
	smb_attr_t tmp_attr;

	ASSERT(attr);
	SMB_NODE_VALID(node);

	/* set attributes specified in attr */
	if (attr->sa_mask != 0) {
		/* if allocation size is < file size, truncate the file */
		if (attr->sa_mask & SMB_AT_ALLOCSZ) {
			attr->sa_allocsz = SMB_ALLOCSZ(attr->sa_allocsz);

			bzero(&tmp_attr, sizeof (smb_attr_t));
			tmp_attr.sa_mask = SMB_AT_SIZE;
			(void) smb_fsop_getattr(NULL, kcred, node, &tmp_attr);

			if (tmp_attr.sa_vattr.va_size > attr->sa_allocsz) {
				attr->sa_vattr.va_size = attr->sa_allocsz;
				attr->sa_mask |= SMB_AT_SIZE;
			}
		}

		rc = smb_fsop_setattr(sr, cr, node, attr);
		if (rc != 0)
			return (rc);

		smb_node_set_cached_allocsz(node, attr);
		smb_node_set_cached_timestamps(node, attr);
		if (of) {
			smb_ofile_set_explicit_times(of,
			    (attr->sa_mask & SMB_AT_TIMES));
		}
	}

	/*
	 * Determine which timestamps to implicitly set to "now".
	 * Don't overwrite timestamps already explicitly set.
	 */
	bzero(&tmp_attr, sizeof (smb_attr_t));
	gethrestime(&now);
	tmp_attr.sa_vattr.va_atime = now;
	tmp_attr.sa_vattr.va_mtime = now;
	tmp_attr.sa_vattr.va_ctime = now;

	/* pending write timestamps */
	if (of) {
		if (smb_ofile_write_time_pending(of)) {
			pending_times |=
			    (SMB_AT_MTIME | SMB_AT_CTIME | SMB_AT_ATIME);
		}
		explicit_times |= (smb_ofile_explicit_times(of));
	}
	explicit_times |= (attr->sa_mask & SMB_AT_TIMES);
	pending_times &= ~explicit_times;

	if (pending_times) {
		tmp_attr.sa_mask = pending_times;
		(void) smb_fsop_setattr(NULL, kcred, node, &tmp_attr);
	}

	/* additional timestamps to update in cache */
	if (attr->sa_mask)
		tmp_attr.sa_mask |= SMB_AT_CTIME;
	if (attr->sa_mask & (SMB_AT_SIZE | SMB_AT_ALLOCSZ))
		tmp_attr.sa_mask |= SMB_AT_MTIME;
	tmp_attr.sa_mask &= ~explicit_times;

	if (tmp_attr.sa_mask)
		smb_node_set_cached_timestamps(node, &tmp_attr);

	if (tmp_attr.sa_mask & SMB_AT_MTIME || explicit_times & SMB_AT_MTIME) {
		if (node->n_dnode != NULL)
			smb_node_notify_change(node->n_dnode);
	}

	return (0);
}

/*
 * smb_node_getattr
 *
 * Get attributes from the file system and apply any smb-specific
 * overrides for size, dos attributes and timestamps
 *
 * node->readonly_creator reflects whether a readonly set is pending
 * from a readonly create. The readonly attribute should be visible to
 * all clients even though the readonly creator fid is immune to the
 * readonly bit until close.
 *
 * Returns: errno
 */
int
smb_node_getattr(smb_request_t *sr, smb_node_t *node, smb_attr_t *attr)
{
	int rc;

	SMB_NODE_VALID(node);

	bzero(attr, sizeof (smb_attr_t));
	attr->sa_mask = SMB_AT_ALL;
	rc = smb_fsop_getattr(sr, kcred, node, attr);
	if (rc != 0)
		return (rc);

	mutex_enter(&node->n_mutex);

	if (smb_node_is_dir(node)) {
		attr->sa_vattr.va_size = 0;
		attr->sa_allocsz = 0;
		attr->sa_vattr.va_nlink = 1;
	}

	if (node->readonly_creator)
		attr->sa_dosattr |= FILE_ATTRIBUTE_READONLY;
	if (attr->sa_dosattr == 0)
		attr->sa_dosattr = FILE_ATTRIBUTE_NORMAL;


	mutex_exit(&node->n_mutex);

	smb_node_get_cached_allocsz(node, attr);
	smb_node_get_cached_timestamps(node, attr);

	return (0);
}

/*
 * smb_node_init_cached_data
 */
static void
smb_node_init_cached_data(smb_node_t *node)
{
	smb_attr_t attr;

	bzero(&attr, sizeof (smb_attr_t));
	attr.sa_mask = SMB_AT_ALL;
	(void) smb_fsop_getattr(NULL, kcred, node, &attr);

	smb_node_init_cached_allocsz(node, &attr);
	smb_node_init_cached_timestamps(node, &attr);
}

/*
 * smb_node_clear_cached_data
 */
static void
smb_node_clear_cached_data(smb_node_t *node)
{
	smb_node_clear_cached_allocsz(node);
	smb_node_clear_cached_timestamps(node);
}

/*
 * File allocation size (allocsz) caching
 *
 * When there are open ofiles on the node, the file allocsz is cached.
 * The cached value (n_allocsz) is initialized when the first ofile is
 * opened and cleared when the last is closed. Allocsz calculated from
 * the filesize (rounded up to block size).
 * When the allocation size is queried, if the cached allocsz is less
 * than the filesize, it is recalculated from the filesize.
 */

/*
 * smb_node_init_cached_allocsz
 *
 * If there are open ofiles, cache the allocsz in the node.
 * Calculate the allocsz from the filesizes.
 * block size).
 */
static void
smb_node_init_cached_allocsz(smb_node_t *node, smb_attr_t *attr)
{
	mutex_enter(&node->n_mutex);
	if (node->n_open_count == 1)
		node->n_allocsz = SMB_ALLOCSZ(attr->sa_vattr.va_size);
	mutex_exit(&node->n_mutex);
}

/*
 * smb_node_clear_cached_allocsz
 */
static void
smb_node_clear_cached_allocsz(smb_node_t *node)
{
	mutex_enter(&node->n_mutex);
	if (node->n_open_count == 0)
		node->n_allocsz = 0;
	mutex_exit(&node->n_mutex);
}

/*
 * smb_node_get_cached_allocsz
 *
 * If there is no cached allocsz (no open files), calculate
 * allocsz from the filesize.
 * If the allocsz is cached but is smaller than the filesize
 * recalculate the cached allocsz from the filesize.
 *
 * Return allocs in attr->sa_allocsz.
 */
static void
smb_node_get_cached_allocsz(smb_node_t *node, smb_attr_t *attr)
{
	if (smb_node_is_dir(node))
		return;

	mutex_enter(&node->n_mutex);
	if (node->n_open_count == 0) {
		attr->sa_allocsz = SMB_ALLOCSZ(attr->sa_vattr.va_size);
	} else {
		if (node->n_allocsz < attr->sa_vattr.va_size)
			node->n_allocsz = SMB_ALLOCSZ(attr->sa_vattr.va_size);
		attr->sa_allocsz = node->n_allocsz;
	}
	mutex_exit(&node->n_mutex);
}

/*
 * smb_node_set_cached_allocsz
 *
 * attr->sa_allocsz has already been rounded to block size by
 * the caller.
 */
static void
smb_node_set_cached_allocsz(smb_node_t *node, smb_attr_t *attr)
{
	mutex_enter(&node->n_mutex);
	if (attr->sa_mask & SMB_AT_ALLOCSZ) {
		if (node->n_open_count > 0)
			node->n_allocsz = attr->sa_allocsz;
	}
	mutex_exit(&node->n_mutex);
}


/*
 * Timestamp caching
 *
 * Solaris file systems handle timestamps different from NTFS. For
 * example when file data is written NTFS doesn't update the timestamps
 * until the file is closed, and then only if they haven't been explicitly
 * set via a set attribute request. In order to provide a more similar
 * view of an open file's timestamps, we cache the timestamps in the
 * node and manipulate them in a manner more consistent with windows.
 * (See handling of explicit times and pending timestamps from a write
 * request in smb_node_getattr and smb_node_setattr above.)
 * Timestamps remain cached while there are open ofiles for the node.
 * This includes open ofiles for named streams.
 * n_ofile_list cannot be used as this doesn't include ofiles opened
 * for the node's named streams. Thus n_timestamps contains a count
 * of open ofiles (t_open_ofiles), including named streams' ofiles,
 * to be used to control timestamp caching.
 *
 * If a node represents a named stream the associated unnamed streams
 * cached timestamps are used instead.
 */

/*
 * smb_node_init_cached_timestamps
 *
 * Increment count of open ofiles which are using the cached timestamps.
 * If this is the first open ofile, init the cached timestamps from the
 * file system values.
 */
static void
smb_node_init_cached_timestamps(smb_node_t *node, smb_attr_t *attr)
{
	smb_node_t *unode;

	if ((unode = SMB_IS_STREAM(node)) != NULL)
		node = unode;

	mutex_enter(&node->n_mutex);
	++(node->n_timestamps.t_open_ofiles);
	if (node->n_timestamps.t_open_ofiles == 1) {
		node->n_timestamps.t_mtime = attr->sa_vattr.va_mtime;
		node->n_timestamps.t_atime = attr->sa_vattr.va_atime;
		node->n_timestamps.t_ctime = attr->sa_vattr.va_ctime;
		node->n_timestamps.t_crtime = attr->sa_crtime;
		node->n_timestamps.t_cached = B_TRUE;
	}
	mutex_exit(&node->n_mutex);
}

/*
 * smb_node_clear_cached_timestamps
 *
 * Decrement count of open ofiles using the cached timestamps.
 * If the decremented count is zero, clear the cached timestamps.
 */
static void
smb_node_clear_cached_timestamps(smb_node_t *node)
{
	smb_node_t *unode;

	if ((unode = SMB_IS_STREAM(node)) != NULL)
		node = unode;

	mutex_enter(&node->n_mutex);
	ASSERT(node->n_timestamps.t_open_ofiles > 0);
	--(node->n_timestamps.t_open_ofiles);
	if (node->n_timestamps.t_open_ofiles == 0)
		bzero(&node->n_timestamps, sizeof (smb_times_t));
	mutex_exit(&node->n_mutex);
}

/*
 * smb_node_get_cached_timestamps
 *
 * Overwrite timestamps in attr with those cached in node.
 */
static void
smb_node_get_cached_timestamps(smb_node_t *node, smb_attr_t *attr)
{
	smb_node_t *unode;

	if ((unode = SMB_IS_STREAM(node)) != NULL)
		node = unode;

	mutex_enter(&node->n_mutex);
	if (node->n_timestamps.t_cached) {
		attr->sa_vattr.va_mtime = node->n_timestamps.t_mtime;
		attr->sa_vattr.va_atime = node->n_timestamps.t_atime;
		attr->sa_vattr.va_ctime = node->n_timestamps.t_ctime;
		attr->sa_crtime = node->n_timestamps.t_crtime;
	}
	mutex_exit(&node->n_mutex);
}

/*
 * smb_node_set_cached_timestamps
 *
 * Update the node's cached timestamps with values from attr.
 */
static void
smb_node_set_cached_timestamps(smb_node_t *node, smb_attr_t *attr)
{
	smb_node_t *unode;

	if ((unode = SMB_IS_STREAM(node)) != NULL)
		node = unode;

	mutex_enter(&node->n_mutex);
	if (node->n_timestamps.t_cached) {
		if (attr->sa_mask & SMB_AT_MTIME)
			node->n_timestamps.t_mtime = attr->sa_vattr.va_mtime;
		if (attr->sa_mask & SMB_AT_ATIME)
			node->n_timestamps.t_atime = attr->sa_vattr.va_atime;
		if (attr->sa_mask & SMB_AT_CTIME)
			node->n_timestamps.t_ctime = attr->sa_vattr.va_ctime;
		if (attr->sa_mask & SMB_AT_CRTIME)
			node->n_timestamps.t_crtime = attr->sa_crtime;
	}
	mutex_exit(&node->n_mutex);
}

/*
 * Check to see if the node represents a reparse point.
 * If yes, whether the reparse point contains a DFS link.
 */
static void
smb_node_init_reparse(smb_node_t *node, smb_attr_t *attr)
{
	nvlist_t *nvl;
	nvpair_t *rec;
	char *rec_type;

	if ((attr->sa_dosattr & FILE_ATTRIBUTE_REPARSE_POINT) == 0)
		return;

	if ((nvl = reparse_init()) == NULL)
		return;

	if (reparse_vnode_parse(node->vp, nvl) != 0) {
		reparse_free(nvl);
		return;
	}

	node->flags |= NODE_FLAGS_REPARSE;

	rec = nvlist_next_nvpair(nvl, NULL);
	while (rec != NULL) {
		rec_type = nvpair_name(rec);
		if ((rec_type != NULL) &&
		    (strcasecmp(rec_type, DFS_REPARSE_SVCTYPE) == 0)) {
			node->flags |= NODE_FLAGS_DFSLINK;
			break;
		}
		rec = nvlist_next_nvpair(nvl, rec);
	}

	reparse_free(nvl);
}

/*
 * smb_node_init_system
 *
 * If the node represents a special system file set NODE_FLAG_SYSTEM.
 * System files:
 * - any node whose parent dnode has NODE_FLAG_SYSTEM set
 * - any node whose associated unnamed stream node (unode) has
 *   NODE_FLAG_SYSTEM set
 * - .$EXTEND at root of share (quota management)
 */
static void
smb_node_init_system(smb_node_t *node)
{
	smb_node_t *dnode = node->n_dnode;
	smb_node_t *unode = node->n_unode;

	if ((dnode) && (dnode->flags & NODE_FLAGS_SYSTEM)) {
		node->flags |= NODE_FLAGS_SYSTEM;
		return;
	}

	if ((unode) && (unode->flags & NODE_FLAGS_SYSTEM)) {
		node->flags |= NODE_FLAGS_SYSTEM;
		return;
	}

	if ((dnode) && (smb_node_is_vfsroot(node->n_dnode) &&
	    (strcasecmp(node->od_name, ".$EXTEND") == 0))) {
		node->flags |= NODE_FLAGS_SYSTEM;
	}
}
