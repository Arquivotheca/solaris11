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
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * General Structures Layout
 * -------------------------
 *
 * This is a simplified diagram showing the relationship between most of the
 * main structures.
 *
 * +-------------------+
 * |     SMB_INFO      |
 * +-------------------+
 *          |
 *          |
 *          v
 * +-------------------+       +-------------------+      +-------------------+
 * |     SESSION       |<----->|     SESSION       |......|      SESSION      |
 * +-------------------+       +-------------------+      +-------------------+
 *          |
 *          |
 *          v
 * +-------------------+       +-------------------+      +-------------------+
 * |       USER        |<----->|       USER        |......|       USER        |
 * +-------------------+       +-------------------+      +-------------------+
 *          |
 *          |
 *          v
 * +-------------------+       +-------------------+      +-------------------+
 * |       TREE        |<----->|       TREE        |......|       TREE        |
 * +-------------------+       +-------------------+      +-------------------+
 *      |         |
 *      |         |
 *      |         v
 *      |     +-------+       +-------+      +-------+
 *      |     | OFILE |<----->| OFILE |......| OFILE |
 *      |     +-------+       +-------+      +-------+
 *      |
 *      |
 *      v
 *  +-------+       +------+      +------+
 *  | ODIR  |<----->| ODIR |......| ODIR |
 *  +-------+       +------+      +------+
 *
 *
 * Tree State Machine
 * ------------------
 *
 *    +-----------------------------+	 T0
 *    |  SMB_TREE_STATE_CONNECTED   |<----------- Creation/Allocation
 *    +-----------------------------+
 *		    |
 *		    | T1
 *		    |
 *		    v
 *    +------------------------------+
 *    | SMB_TREE_STATE_DISCONNECTING |
 *    +------------------------------+
 *		    |
 *		    | T2
 *		    |
 *		    v
 *    +-----------------------------+    T3
 *    | SMB_TREE_STATE_DISCONNECTED |----------> Deletion/Free
 *    +-----------------------------+
 *
 * SMB_TREE_STATE_CONNECTED
 *
 *    While in this state:
 *      - The tree is queued in the list of trees of its user.
 *      - References will be given out if the tree is looked up.
 *      - Files under that tree can be accessed.
 *
 * SMB_TREE_STATE_DISCONNECTING
 *
 *    While in this state:
 *      - The tree is queued in the list of trees of its user.
 *      - References will not be given out if the tree is looked up.
 *      - The files and directories open under the tree are being closed.
 *      - The resources associated with the tree remain.
 *
 * SMB_TREE_STATE_DISCONNECTED
 *
 *    While in this state:
 *      - The tree is queued in the list of trees of its user.
 *      - References will not be given out if the tree is looked up.
 *      - The tree has no more files and directories opened.
 *      - The resources associated with the tree remain.
 *
 * Transition T0
 *
 *    This transition occurs in smb_tree_connect(). A new tree is created and
 *    added to the list of trees of a user.
 *
 * Transition T1
 *
 *    This transition occurs in smb_tree_disconnect().
 *
 * Transition T2
 *
 *    This transition occurs in smb_tree_release(). The resources associated
 *    with the tree are freed as well as the tree structure. For the transition
 *    to occur, the tree must be in the SMB_TREE_STATE_DISCONNECTED state and
 *    the reference count be zero.
 *
 * Comments
 * --------
 *
 *    The state machine of the tree structures is controlled by 3 elements:
 *      - The list of trees of the user it belongs to.
 *      - The mutex embedded in the structure itself.
 *      - The reference count.
 *
 *    There's a mutex embedded in the tree structure used to protect its fields
 *    and there's a lock embedded in the list of trees of a user. To
 *    increment or to decrement the reference count the mutex must be entered.
 *    To insert the tree into the list of trees of the user and to remove
 *    the tree from it, the lock must be entered in RW_WRITER mode.
 *
 *    Rules of access to a tree structure:
 *
 *    1) In order to avoid deadlocks, when both (mutex and lock of the user
 *       list) have to be entered, the lock must be entered first.
 *
 *    2) All actions applied to a tree require a reference count.
 *
 *    3) There are 2 ways of getting a reference count: when a tree is
 *       connected and when a tree is looked up.
 *
 *    It should be noted that the reference count of a tree registers the
 *    number of references to the tree in other structures (such as an smb
 *    request). The reference count is not incremented in these 2 instances:
 *
 *    1) The tree is connected. An tree is anchored by his state. If there's
 *       no activity involving a tree currently connected, the reference
 *       count of that tree is zero.
 *
 *    2) The tree is queued in the list of trees of the user. The fact of
 *       being queued in that list is NOT registered by incrementing the
 *       reference count.
 */

#include <sys/refstr_impl.h>
#include <smbsrv/smb_kproto.h>
#include <smbsrv/smb_ktypes.h>
#include <smbsrv/smb_fsops.h>
#include <smbsrv/smb_share.h>

int smb_tcon_mute = 0;

static smb_tree_t *smb_tree_connect_disk(smb_request_t *, const char *);
static smb_tree_t *smb_tree_connect_printq(smb_request_t *, const char *);
static smb_tree_t *smb_tree_connect_ipc(smb_request_t *, const char *);
static smb_tree_t *smb_tree_alloc(smb_user_t *, const smb_share_t *,
    smb_node_t *, uint32_t, uint32_t);
static boolean_t smb_tree_is_connected_locked(smb_tree_t *);
static boolean_t smb_tree_is_disconnected(smb_tree_t *);
static const char *smb_tree_get_sharename(const char *);
static int smb_tree_getattr(const smb_share_t *, smb_node_t *, smb_tree_t *);
static void smb_tree_get_volname(vfs_t *, smb_tree_t *);
static void smb_tree_get_flags(const smb_share_t *, vfs_t *, smb_tree_t *);
static void smb_tree_log(smb_request_t *, const char *, const char *, ...);
static void smb_tree_close_odirs(smb_tree_t *, uint16_t);
static smb_ofile_t *smb_tree_get_ofile(smb_tree_t *, smb_ofile_t *);
static smb_odir_t *smb_tree_get_odir(smb_tree_t *, smb_odir_t *);
static void smb_tree_set_execinfo(smb_tree_t *, smb_shr_execinfo_t *, int);
static int smb_tree_enum_private(smb_tree_t *, smb_svcenum_t *);
static int smb_tree_netinfo_encode(smb_tree_t *, uint8_t *, size_t, uint32_t *);
static void smb_tree_netinfo_init(smb_tree_t *tree, smb_netconnectinfo_t *);
static void smb_tree_netinfo_fini(smb_netconnectinfo_t *);

/*
 * Lookup the share name dispatch the appropriate stype handler.
 * Share names are case insensitive so we map the share name to
 * lower-case as a convenience for internal processing.
 *
 * Valid service values are:
 *	A:      Disk share
 *	LPT1:   Printer
 *	IPC     Named pipe (IPC$ is reserved as the named pipe share).
 *	COMM    Communications device
 *	?????   Any type of device (wildcard)
 */
smb_tree_t *
smb_tree_connect(smb_request_t *sr)
{
	char		*unc_path = sr->sr_tcon.path;
	smb_tree_t	*tree = NULL;
	smb_share_t	*si;
	const char	*name;

	if (unc_path == NULL) {
		smb_tree_log(sr, "<null>", "invalid UNC path");
		smb_errcode_seterror(0, ERRSRV, ERRinvnetname);
		return (NULL);
	}

	(void) smb_strlwr(unc_path);

	if ((name = smb_tree_get_sharename(unc_path)) == NULL) {
		smb_tree_log(sr, unc_path, "invalid UNC path");
		smb_errcode_seterror(0, ERRSRV, ERRinvnetname);
		return (NULL);
	}

	if (strcasecmp(SMB_SHARE_PRINT, name) == 0) {
		smb_tree_log(sr, name, "access not permitted");
		smb_errcode_seterror(NT_STATUS_ACCESS_DENIED,
		    ERRSRV, ERRaccess);
		return (NULL);
	}

	if ((si = smb_shrmgr_share_lookup(name)) == NULL) {
		smb_tree_log(sr, name, "share not found");
		smb_errcode_seterror(0, ERRSRV, ERRinvnetname);
		return (NULL);
	}

	sr->sr_tcon.si = si;

	switch (si->shr_type & STYPE_MASK) {
	case STYPE_DISKTREE:
		tree = smb_tree_connect_disk(sr, name);
		break;
	case STYPE_IPC:
		tree = smb_tree_connect_ipc(sr, name);
		break;
	case STYPE_PRINTQ:
		tree = smb_tree_connect_printq(sr, name);
		break;
	default:
		smb_errcode_seterror(NT_STATUS_BAD_DEVICE_TYPE,
		    ERRDOS, ERROR_BAD_DEV_TYPE);
		break;
	}

	smb_shrmgr_share_release(si);
	return (tree);
}

/*
 * Disconnect a tree.
 */
void
smb_tree_disconnect(smb_tree_t *tree, boolean_t do_exec)
{
	smb_shr_execinfo_t execinfo;

	ASSERT(tree->t_magic == SMB_TREE_MAGIC);

	mutex_enter(&tree->t_mutex);
	ASSERT(tree->t_refcnt);

	if (smb_tree_is_connected_locked(tree)) {
		/*
		 * Indicate that the disconnect process has started.
		 */
		tree->t_state = SMB_TREE_STATE_DISCONNECTING;
		mutex_exit(&tree->t_mutex);

		if (do_exec) {
			/*
			 * The files opened under this tree are closed.
			 */
			smb_ofile_close_all(tree);
			/*
			 * The directories opened under this tree are closed.
			 */
			smb_tree_close_odirs(tree, 0);
		}

		mutex_enter(&tree->t_mutex);
		tree->t_state = SMB_TREE_STATE_DISCONNECTED;
		smb_server_dec_trees(tree->t_server);
	}

	mutex_exit(&tree->t_mutex);

	if (do_exec && (tree->t_state == SMB_TREE_STATE_DISCONNECTED) &&
	    (tree->t_execflags & SMB_EXEC_UNMAP)) {

		smb_tree_set_execinfo(tree, &execinfo, SMB_EXEC_UNMAP);
		(void) smb_kshare_exec(&execinfo);
	}
}

/*
 * Take a reference on a tree.
 */
boolean_t
smb_tree_hold(
    smb_tree_t		*tree)
{
	ASSERT(tree);
	ASSERT(tree->t_magic == SMB_TREE_MAGIC);

	mutex_enter(&tree->t_mutex);

	if (smb_tree_is_connected_locked(tree)) {
		tree->t_refcnt++;
		mutex_exit(&tree->t_mutex);
		return (B_TRUE);
	}

	mutex_exit(&tree->t_mutex);
	return (B_FALSE);
}

/*
 * Release a reference on a tree.  If the tree is disconnected and the
 * reference count falls to zero, post the object for deletion.
 * Object deletion is deferred to avoid modifying a list while an
 * iteration may be in progress.
 */
void
smb_tree_release(
    smb_tree_t		*tree)
{
	SMB_TREE_VALID(tree);

	mutex_enter(&tree->t_mutex);
	ASSERT(tree->t_refcnt);
	tree->t_refcnt--;

	/* flush the ofile and odir lists' delete queues */
	smb_llist_flush(&tree->t_ofile_list);
	smb_llist_flush(&tree->t_odir_list);

	if (smb_tree_is_disconnected(tree) && (tree->t_refcnt == 0))
		smb_user_post_tree(tree->t_user, tree);

	mutex_exit(&tree->t_mutex);
}

void
smb_tree_post_ofile(smb_tree_t *tree, smb_ofile_t *of)
{
	SMB_TREE_VALID(tree);
	SMB_OFILE_VALID(of);
	ASSERT(of->f_refcnt == 0);
	ASSERT(of->f_state == SMB_OFILE_STATE_CLOSED);
	ASSERT(of->f_tree == tree);

	smb_llist_post(&tree->t_ofile_list, of, smb_ofile_delete);
}

void
smb_tree_post_odir(smb_tree_t *tree, smb_odir_t *od)
{
	SMB_TREE_VALID(tree);
	SMB_ODIR_VALID(od);
	ASSERT(od->d_refcnt == 0);
	ASSERT(od->d_state == SMB_ODIR_STATE_CLOSED);
	ASSERT(od->d_tree == tree);

	smb_llist_post(&tree->t_odir_list, od, smb_odir_delete);
}

/*
 * Close ofiles and odirs that match pid.
 */
void
smb_tree_close_pid(
    smb_tree_t		*tree,
    uint16_t		pid)
{
	ASSERT(tree);
	ASSERT(tree->t_magic == SMB_TREE_MAGIC);

	smb_ofile_close_all_by_pid(tree, pid);
	smb_tree_close_odirs(tree, pid);
}

/*
 * Check whether or not a tree supports the features identified by flags.
 */
boolean_t
smb_tree_has_feature(smb_tree_t *tree, uint32_t flags)
{
	ASSERT(tree);
	ASSERT(tree->t_magic == SMB_TREE_MAGIC);

	return ((tree->t_flags & flags) == flags);
}

/*
 * If the enumeration request is for tree data, handle the request
 * here.  Otherwise, pass it on to the ofiles.
 *
 * This function should be called with a hold on the tree.
 */
int
smb_tree_enum(smb_tree_t *tree, smb_svcenum_t *svcenum)
{
	smb_ofile_t	*of;
	smb_ofile_t	*next;
	int		rc = 0;

	ASSERT(tree);
	ASSERT(tree->t_magic == SMB_TREE_MAGIC);

	if (svcenum->se_type == SMB_SVCENUM_TYPE_TREE)
		return (smb_tree_enum_private(tree, svcenum));

	of = smb_tree_get_ofile(tree, NULL);
	while (of) {
		ASSERT(of->f_tree == tree);

		rc = smb_ofile_enum(of, svcenum);
		if (rc != 0) {
			smb_ofile_release(of);
			break;
		}

		next = smb_tree_get_ofile(tree, of);
		smb_ofile_release(of);
		of = next;
	}

	return (rc);
}

/*
 * Close a file by its unique id.
 */
int
smb_tree_fclose(smb_tree_t *tree, uint32_t uniqid)
{
	smb_ofile_t	*of;

	ASSERT(tree);
	ASSERT(tree->t_magic == SMB_TREE_MAGIC);

	if ((of = smb_ofile_lookup_by_uniqid(tree, uniqid)) == NULL)
		return (ENOENT);

	if (smb_ofile_disallow_fclose(of)) {
		smb_ofile_release(of);
		return (EACCES);
	}

	smb_ofile_close(of, 0);
	smb_ofile_release(of);
	return (0);
}

/* *************************** Static Functions ***************************** */

#define	SHARES_DIR	".zfs/shares/"

/*
 * Calculates permissions given by the share's ACL to the
 * user in the passed request.  The default is full access.
 * If any error occurs, full access is granted.
 *
 * Using the vnode of the share path find the root directory
 * of the mounted file system. Then look to see if there is a
 * .zfs/shares directory and if there is, lookup the file with
 * the same name as the share name in it. The ACL set for this
 * file is the share's ACL which is used for access check here.
 */
static uint32_t
smb_tree_acl_access(smb_request_t *sr, const smb_share_t *si, vnode_t *pathvp)
{
	smb_user_t	*user;
	cred_t		*cred;
	int		rc;
	vfs_t		*vfsp;
	vnode_t		*root = NULL;
	vnode_t		*sharevp = NULL;
	char		*sharepath;
	struct pathname	pnp;
	size_t		size;
	uint32_t	access;

	user = sr->uid_user;
	cred = user->u_cred;
	access = ACE_ALL_PERMS;

	if (si->shr_flags & SMB_SHRF_AUTOHOME) {
		/*
		 * An autohome share owner gets full access to the share.
		 * Everyone else is denied access.
		 */
		if (si->shr_uid != crgetuid(cred))
			access = 0;

		return (access);
	}

	/*
	 * The hold on 'root' is released by the lookuppnvp() that follows
	 */
	vfsp = pathvp->v_vfsp;
	if (vfsp != NULL)
		rc = VFS_ROOT(vfsp, &root);
	else
		rc = ENOENT;

	if (rc != 0)
		return (access);


	size = sizeof (SHARES_DIR) + strlen(si->shr_name) + 1;
	sharepath = smb_srm_alloc(sr, size);
	(void) sprintf(sharepath, "%s%s", SHARES_DIR, si->shr_name);

	pn_alloc(&pnp);
	(void) pn_set(&pnp, sharepath);
	rc = lookuppnvp(&pnp, NULL, NO_FOLLOW, NULL, &sharevp, rootdir, root,
	    kcred);
	pn_free(&pnp);

	/*
	 * Now get the effective access value based on cred and ACL values.
	 */
	if (rc == 0) {
		smb_vop_eaccess(sharevp, (int *)&access, V_ACE_MASK, NULL,
		    cred);
		VN_RELE(sharevp);
	}

	return (access);
}

/*
 * Performs the following access checks for a disk share:
 *
 *  - No IPC/anonymous user is allowed
 *
 *  - If user is Guest, guestok property of the share should be
 *    enabled
 *
 *  - If this is an Admin share, the user should have administrative
 *    privileges
 *
 *  - Host based access control lists
 *
 *  - Share ACL
 *
 *  Returns the access allowed or 0 if access is denied.
 */
static uint32_t
smb_tree_chkaccess(smb_request_t *sr, smb_share_t *si, vnode_t *vp)
{
	smb_user_t *user = sr->uid_user;
	char *sharename = si->shr_name;
	uint32_t host_access;
	uint32_t acl_access;
	uint32_t access;

	if (user->u_flags & SMB_USER_FLAG_IPC) {
		smb_tree_log(sr, sharename, "access denied: IPC only");
		return (0);
	}

	if ((user->u_flags & SMB_USER_FLAG_GUEST) &&
	    ((si->shr_flags & SMB_SHRF_GUEST_OK) == 0)) {
		smb_tree_log(sr, sharename, "access denied: guest disabled");
		return (0);
	}

	if ((si->shr_flags & SMB_SHRF_ADMIN) && !smb_user_is_admin(user)) {
		smb_tree_log(sr, sharename, "access denied: not admin");
		return (0);
	}

	host_access = smb_kshare_hostaccess(si, &sr->session->ipaddr);
	if ((host_access & ACE_ALL_PERMS) == 0) {
		smb_tree_log(sr, sharename, "access denied: host access");
		return (0);
	}

	acl_access = smb_tree_acl_access(sr, si, vp);
	if ((acl_access & ACE_ALL_PERMS) == 0) {
		smb_tree_log(sr, sharename, "access denied: share ACL");
		return (0);
	}

	access = host_access & acl_access;
	if ((access & ACE_ALL_PERMS) == 0) {
		smb_tree_log(sr, sharename, "access denied");
		return (0);
	}

	return (access);
}

/*
 * Connect a share for use with files and directories.
 */
static smb_tree_t *
smb_tree_connect_disk(smb_request_t *sr, const char *sharename)
{
	const char		*any = "?????";
	smb_user_t		*user = sr->uid_user;
	smb_node_t		*dnode = NULL;
	smb_node_t		*snode = NULL;
	smb_share_t 		*si = sr->sr_tcon.si;
	char			*service = sr->sr_tcon.service;
	char			last_component[MAXNAMELEN];
	smb_tree_t		*tree;
	int			rc;
	uint32_t		access;
	smb_shr_execinfo_t	execinfo;

	ASSERT(user);
	ASSERT(user->u_cred);

	if ((strcmp(service, any) != 0) && (strcasecmp(service, "A:") != 0)) {
		smb_tree_log(sr, sharename, "invalid service (%s)", service);
		smb_errcode_seterror(NT_STATUS_BAD_DEVICE_TYPE,
		    ERRDOS, ERROR_BAD_DEV_TYPE);
		return (NULL);
	}

	/*
	 * Check that the shared directory exists.
	 */
	rc = smb_pathname_reduce(sr, user->u_cred, si->shr_path, 0, 0, &dnode,
	    last_component);

	if (rc == 0) {
		rc = smb_fsop_lookup(sr, user->u_cred, SMB_FOLLOW_LINKS,
		    sr->sr_server->si_root_smb_node, dnode, last_component,
		    &snode);

		smb_node_release(dnode);
	}

	if (rc) {
		if (snode)
			smb_node_release(snode);

		smb_tree_log(sr, sharename, "bad path: %s", si->shr_path);
		smb_errcode_seterror(0, ERRSRV, ERRinvnetname);
		return (NULL);
	}

	if ((access = smb_tree_chkaccess(sr, si, snode->vp)) == 0) {
		smb_errcode_seterror(NT_STATUS_ACCESS_DENIED,
		    ERRSRV, ERRaccess);
		smb_node_release(snode);
		return (NULL);
	}

	/*
	 * Set up the OptionalSupport for this share.
	 */
	sr->sr_tcon.optional_support = SMB_SUPPORT_SEARCH_BITS;

	switch (si->shr_flags & SMB_SHRF_CSC_MASK) {
	case SMB_SHRF_CSC_DISABLED:
		sr->sr_tcon.optional_support |= SMB_CSC_CACHE_NONE;
		break;
	case SMB_SHRF_CSC_AUTO:
		sr->sr_tcon.optional_support |= SMB_CSC_CACHE_AUTO_REINT;
		break;
	case SMB_SHRF_CSC_VDO:
		sr->sr_tcon.optional_support |= SMB_CSC_CACHE_VDO;
		break;
	case SMB_SHRF_CSC_MANUAL:
	default:
		/*
		 * Default to SMB_CSC_CACHE_MANUAL_REINT.
		 */
		break;
	}

	/* ABE support */
	if (si->shr_flags & SMB_SHRF_ABE)
		sr->sr_tcon.optional_support |=
		    SHI1005_FLAGS_ACCESS_BASED_DIRECTORY_ENUM;

	if (si->shr_flags & SMB_SHRF_DFSROOT)
		sr->sr_tcon.optional_support |= SMB_SHARE_IS_IN_DFS;

	/* if 'smb' zfs property: shortnames=disabled */
	if (!smb_shortnames)
		sr->arg.tcon.optional_support |= SMB_UNIQUE_FILE_NAME;

	tree = smb_tree_alloc(user, si, snode, access,
	    sr->sr_cfg->skc_execflags);

	smb_node_release(snode);

	if (tree) {
		if (tree->t_execflags & SMB_EXEC_MAP) {
			smb_tree_set_execinfo(tree, &execinfo, SMB_EXEC_MAP);

			rc = smb_kshare_exec(&execinfo);

			if ((rc != 0) && (tree->t_execflags & SMB_EXEC_TERM)) {
				smb_tree_disconnect(tree, B_FALSE);
				smb_tree_release(tree);
				smb_errcode_seterror(NT_STATUS_ACCESS_DENIED,
				    ERRSRV, ERRaccess);
				return (NULL);
			}
		}
	} else {
		smb_errcode_seterror(NT_STATUS_ACCESS_DENIED,
		    ERRSRV, ERRaccess);
	}

	return (tree);
}

/*
 * Shares have both a share and host based access control.  The access
 * granted will be minimum permissions based on both hostaccess
 * (permissions allowed by host based access) and aclaccess (from the
 * share ACL).
 */
static smb_tree_t *
smb_tree_connect_printq(smb_request_t *sr, const char *sharename)
{
	const char		*any = "?????";
	smb_user_t		*user = sr->uid_user;
	smb_node_t		*dnode = NULL;
	smb_node_t		*snode = NULL;
	smb_share_t 		*si = sr->sr_tcon.si;
	char			*service = sr->sr_tcon.service;
	char			last_component[MAXNAMELEN];
	smb_tree_t		*tree;
	int			rc;
	uint32_t		access;

	ASSERT(user);
	ASSERT(user->u_cred);

	if ((strcmp(service, any) != 0) &&
	    (strcasecmp(service, "LPT1:") != 0)) {
		smb_tree_log(sr, sharename, "invalid service (%s)", service);
		smb_errcode_seterror(NT_STATUS_BAD_DEVICE_TYPE,
		    ERRDOS, ERROR_BAD_DEV_TYPE);
		return (NULL);
	}

	/*
	 * Check that the shared directory exists.
	 */
	rc = smb_pathname_reduce(sr, user->u_cred, si->shr_path, 0, 0, &dnode,
	    last_component);
	if (rc == 0) {
		rc = smb_fsop_lookup(sr, user->u_cred, SMB_FOLLOW_LINKS,
		    sr->sr_server->si_root_smb_node, dnode, last_component,
		    &snode);

		smb_node_release(dnode);
	}

	if (rc) {
		if (snode)
			smb_node_release(snode);

		smb_tree_log(sr, sharename, "bad path: %s", si->shr_path);
		smb_errcode_seterror(0, ERRSRV, ERRinvnetname);
		return (NULL);
	}

	if ((access = smb_tree_chkaccess(sr, si, snode->vp)) == 0) {
		smb_errcode_seterror(NT_STATUS_ACCESS_DENIED,
		    ERRSRV, ERRaccess);
		smb_node_release(snode);
		return (NULL);
	}

	sr->sr_tcon.optional_support = SMB_SUPPORT_SEARCH_BITS;

	tree = smb_tree_alloc(user, si, snode, access,
	    sr->sr_cfg->skc_execflags);

	smb_node_release(snode);

	if (tree == NULL)
		smb_errcode_seterror(NT_STATUS_ACCESS_DENIED,
		    ERRSRV, ERRaccess);

	return (tree);
}

/*
 * Connect an IPC share for use with named pipes.
 */
static smb_tree_t *
smb_tree_connect_ipc(smb_request_t *sr, const char *name)
{
	const char	*any = "?????";
	smb_user_t	*user = sr->uid_user;
	smb_tree_t	*tree;
	smb_share_t	*si = sr->sr_tcon.si;
	char		*service = sr->sr_tcon.service;

	ASSERT(user);

	if ((user->u_flags & SMB_USER_FLAG_IPC) &&
	    sr->sr_cfg->skc_restrict_anon) {
		smb_tree_log(sr, name, "access denied: restrict anonymous");
		smb_errcode_seterror(NT_STATUS_ACCESS_DENIED,
		    ERRSRV, ERRaccess);
		return (NULL);
	}

	if ((strcmp(service, any) != 0) && (strcasecmp(service, "IPC") != 0)) {
		smb_tree_log(sr, name, "invalid service (%s)", service);
		smb_errcode_seterror(NT_STATUS_BAD_DEVICE_TYPE,
		    ERRDOS, ERROR_BAD_DEV_TYPE);
		return (NULL);
	}

	sr->sr_tcon.optional_support = SMB_SUPPORT_SEARCH_BITS;

	tree = smb_tree_alloc(user, si, NULL, ACE_ALL_PERMS, 0);
	if (tree == NULL) {
		smb_tree_log(sr, name, "access denied");
		smb_errcode_seterror(NT_STATUS_ACCESS_DENIED,
		    ERRSRV, ERRaccess);
	}

	return (tree);
}

/*
 * Allocate a tree.
 */
static smb_tree_t *
smb_tree_alloc(smb_user_t *user, const smb_share_t *si, smb_node_t *snode,
    uint32_t access, uint32_t execflags)
{
	smb_tree_t	*tree;
	uint32_t	stype = si->shr_type;
	uint16_t	tid;

	if (smb_idpool_alloc(&user->u_tid_pool, &tid))
		return (NULL);

	tree = kmem_cache_alloc(user->u_server->si_cache_tree, KM_SLEEP);
	bzero(tree, sizeof (smb_tree_t));

	tree->t_user = user;
	tree->t_session = user->u_session;
	tree->t_server = user->u_server;

	if (STYPE_ISDSK(stype) || STYPE_ISPRN(stype)) {
		if (smb_tree_getattr(si, snode, tree) != 0) {
			smb_idpool_free(&user->u_tid_pool, tid);
			kmem_cache_free(user->u_server->si_cache_tree, tree);
			return (NULL);
		}
	}

	if (smb_idpool_constructor(&tree->t_fid_pool)) {
		smb_idpool_free(&user->u_tid_pool, tid);
		kmem_cache_free(user->u_server->si_cache_tree, tree);
		return (NULL);
	}

	if (smb_idpool_constructor(&tree->t_odid_pool)) {
		smb_idpool_destructor(&tree->t_fid_pool);
		smb_idpool_free(&user->u_tid_pool, tid);
		kmem_cache_free(user->u_server->si_cache_tree, tree);
		return (NULL);
	}

	smb_llist_constructor(&tree->t_ofile_list, sizeof (smb_ofile_t),
	    offsetof(smb_ofile_t, f_lnd));

	smb_llist_constructor(&tree->t_odir_list, sizeof (smb_odir_t),
	    offsetof(smb_odir_t, d_lnd));

	(void) strlcpy(tree->t_sharename, si->shr_name,
	    sizeof (tree->t_sharename));
	(void) strlcpy(tree->t_resource, si->shr_path,
	    sizeof (tree->t_resource));
	tree->t_shareid = si->shr_kid;

	mutex_init(&tree->t_mutex, NULL, MUTEX_DEFAULT, NULL);

	tree->t_refcnt = 1;
	tree->t_tid = tid;
	tree->t_res_type = stype;
	tree->t_state = SMB_TREE_STATE_CONNECTED;
	tree->t_magic = SMB_TREE_MAGIC;
	tree->t_access = access;
	tree->t_connect_time = gethrestime_sec();
	tree->t_execflags = execflags;

	/* if FS is readonly, enforce that here */
	if (tree->t_flags & SMB_TREE_READONLY)
		tree->t_access &= ~ACE_ALL_WRITE_PERMS;

	if (STYPE_ISDSK(stype) || STYPE_ISPRN(stype)) {
		smb_node_ref(snode);
		tree->t_snode = snode;
		tree->t_acltype = smb_fsop_acltype(snode);
	}

	smb_llist_enter(&user->u_tree_list, RW_WRITER);
	smb_llist_insert_head(&user->u_tree_list, tree);
	smb_llist_exit(&user->u_tree_list);
	atomic_inc_32(&user->u_session->s_tree_cnt);
	smb_server_inc_trees(user->u_server);
	return (tree);
}

/*
 * Deallocate a tree.  The open file and open directory lists should be
 * empty.
 *
 * Remove the tree from the user's tree list before freeing resources
 * associated with the tree.
 */
void
smb_tree_dealloc(void *arg)
{
	smb_user_t	*user;
	smb_tree_t	*tree = (smb_tree_t *)arg;

	SMB_TREE_VALID(tree);
	ASSERT(tree->t_state == SMB_TREE_STATE_DISCONNECTED);
	ASSERT(tree->t_refcnt == 0);

	user = tree->t_user;
	smb_llist_enter(&user->u_tree_list, RW_WRITER);
	smb_llist_remove(&user->u_tree_list, tree);
	smb_idpool_free(&user->u_tid_pool, tree->t_tid);
	atomic_dec_32(&tree->t_session->s_tree_cnt);
	smb_llist_exit(&user->u_tree_list);

	mutex_enter(&tree->t_mutex);
	mutex_exit(&tree->t_mutex);

	tree->t_magic = (uint32_t)~SMB_TREE_MAGIC;

	if (tree->t_snode)
		smb_node_release(tree->t_snode);

	mutex_destroy(&tree->t_mutex);
	smb_llist_destructor(&tree->t_ofile_list);
	smb_llist_destructor(&tree->t_odir_list);
	smb_idpool_destructor(&tree->t_fid_pool);
	smb_idpool_destructor(&tree->t_odid_pool);
	kmem_cache_free(tree->t_server->si_cache_tree, tree);
}

/*
 * Determine whether or not a tree is connected.
 * This function must be called with the tree mutex held.
 */
static boolean_t
smb_tree_is_connected_locked(smb_tree_t *tree)
{
	switch (tree->t_state) {
	case SMB_TREE_STATE_CONNECTED:
		return (B_TRUE);

	case SMB_TREE_STATE_DISCONNECTING:
	case SMB_TREE_STATE_DISCONNECTED:
		/*
		 * The tree exists but being diconnected or destroyed.
		 */
		return (B_FALSE);

	default:
		ASSERT(0);
		return (B_FALSE);
	}
}

/*
 * Determine whether or not a tree is disconnected.
 * This function must be called with the tree mutex held.
 */
static boolean_t
smb_tree_is_disconnected(smb_tree_t *tree)
{
	switch (tree->t_state) {
	case SMB_TREE_STATE_DISCONNECTED:
		return (B_TRUE);

	case SMB_TREE_STATE_CONNECTED:
	case SMB_TREE_STATE_DISCONNECTING:
		return (B_FALSE);

	default:
		ASSERT(0);
		return (B_FALSE);
	}
}

/*
 * Return a pointer to the share name within a share resource path.
 *
 * The share path may be a Uniform Naming Convention (UNC) string
 * (\\server\share) or simply the share name.  We validate the UNC
 * format but we don't look at the server name.
 */
static const char *
smb_tree_get_sharename(const char *unc_path)
{
	const char *sharename = unc_path;

	if (sharename[0] == '\\') {
		/*
		 * Looks like a UNC path, validate the format.
		 */
		if (sharename[1] != '\\')
			return (NULL);

		if ((sharename = strchr(sharename+2, '\\')) == NULL)
			return (NULL);

		++sharename;
	} else if (strchr(sharename, '\\') != NULL) {
		/*
		 * This should be a share name (no embedded \'s).
		 */
		return (NULL);
	}

	return (sharename);
}

/*
 * Obtain the tree attributes: volume name, typename and flags.
 */
static int
smb_tree_getattr(const smb_share_t *si, smb_node_t *node, smb_tree_t *tree)
{
	vfs_t *vfsp = SMB_NODE_VFS(node);

	ASSERT(vfsp);

	if (getvfs(&vfsp->vfs_fsid) != vfsp)
		return (ESTALE);

	smb_tree_get_volname(vfsp, tree);
	smb_tree_get_flags(si, vfsp, tree);

	VFS_RELE(vfsp);
	return (0);
}

/*
 * Extract the volume name.
 */
static void
smb_tree_get_volname(vfs_t *vfsp, smb_tree_t *tree)
{
	refstr_t *vfs_mntpoint;
	const char *s;
	char *name;

	vfs_mntpoint = vfs_getmntpoint(vfsp);

	s = vfs_mntpoint->rs_string;
	s += strspn(s, "/");
	(void) strlcpy(tree->t_volume, s, SMB_VOLNAMELEN);

	refstr_rele(vfs_mntpoint);

	name = tree->t_volume;
	(void) strsep((char **)&name, "/");
}

/*
 * Always set ACL support because the VFS will fake ACLs for file systems
 * that don't support them.
 *
 * Some flags are dependent on the typename, which is also set up here.
 * File system types are hardcoded in uts/common/os/vfs_conf.c.
 */
static void
smb_tree_get_flags(const smb_share_t *si, vfs_t *vfsp, smb_tree_t *tree)
{
	typedef struct smb_mtype {
		char		*mt_name;
		size_t		mt_namelen;
		uint32_t	mt_flags;
	} smb_mtype_t;

	static smb_mtype_t smb_mtype[] = {
		{ "zfs",    3,	SMB_TREE_UNICODE_ON_DISK |
		    SMB_TREE_QUOTA | SMB_TREE_SPARSE},
		{ "ufs",    3,	SMB_TREE_UNICODE_ON_DISK },
		{ "nfs",    3,	SMB_TREE_NFS_MOUNTED },
		{ "tmpfs",  5,	SMB_TREE_NO_EXPORT }
	};
	smb_mtype_t	*mtype;
	char		*name;
	uint32_t	flags = SMB_TREE_SUPPORTS_ACLS;
	int		i;

	if (si->shr_flags & SMB_SHRF_DFSROOT)
		flags |= SMB_TREE_DFSROOT;

	if (si->shr_flags & SMB_SHRF_CATIA)
		flags |= SMB_TREE_CATIA;

	if (si->shr_flags & SMB_SHRF_ABE)
		flags |= SMB_TREE_ABE;

	if (smb_session_oplocks_enable(tree->t_session)) {
		/* if 'smb' zfs property: oplocks=enabled */
		flags |= SMB_TREE_OPLOCKS;
	}

	/* if 'smb' zfs property: shortnames=enabled */
	if (smb_shortnames)
		flags |= SMB_TREE_SHORTNAMES;

	if (vfsp->vfs_flag & VFS_RDONLY)
		flags |= SMB_TREE_READONLY;

	if (vfsp->vfs_flag & VFS_XATTR)
		flags |= SMB_TREE_STREAMS;

	name = vfssw[vfsp->vfs_fstype].vsw_name;

	for (i = 0; i < sizeof (smb_mtype) / sizeof (smb_mtype[0]); ++i) {
		mtype = &smb_mtype[i];
		if (strncasecmp(name, mtype->mt_name, mtype->mt_namelen) == 0)
			flags |= mtype->mt_flags;
	}

	(void) strlcpy(tree->t_typename, name, SMB_TYPENAMELEN);
	(void) smb_strupr((char *)tree->t_typename);

	if (vfs_has_feature(vfsp, VFSFT_XVATTR))
		flags |= SMB_TREE_XVATTR;

	if (vfs_has_feature(vfsp, VFSFT_CASEINSENSITIVE))
		flags |= SMB_TREE_CASEINSENSITIVE;

	if (vfs_has_feature(vfsp, VFSFT_NOCASESENSITIVE))
		flags |= SMB_TREE_NO_CASESENSITIVE;

	if (vfs_has_feature(vfsp, VFSFT_DIRENTFLAGS))
		flags |= SMB_TREE_DIRENTFLAGS;

	if (vfs_has_feature(vfsp, VFSFT_ACLONCREATE))
		flags |= SMB_TREE_ACLONCREATE;

	if (vfs_has_feature(vfsp, VFSFT_ACEMASKONACCESS))
		flags |= SMB_TREE_ACEMASKONACCESS;

	DTRACE_PROBE2(smb__tree__flags, uint32_t, flags, char *, name);


	tree->t_flags = flags;
}

/*
 * Report share access result to syslog.
 */
static void
smb_tree_log(smb_request_t *sr, const char *sharename, const char *fmt, ...)
{
	va_list ap;
	char buf[128];
	smb_user_t *user = sr->uid_user;

	ASSERT(user);

	if (smb_tcon_mute)
		return;

	if ((user->u_name) && (strcasecmp(sharename, "IPC$") == 0)) {
		/*
		 * Only report normal users, i.e. ignore W2K misuse
		 * of the IPC connection by filtering out internal
		 * names such as nobody and root.
		 */
		if ((strcmp(user->u_name, "root") == 0) ||
		    (strcmp(user->u_name, "nobody") == 0)) {
			return;
		}
	}

	va_start(ap, fmt);
	(void) vsnprintf(buf, 128, fmt, ap);
	va_end(ap);

	cmn_err(CE_NOTE, "[%s\\%s]: %s %s",
	    user->u_domain, user->u_name, sharename, buf);
}

/*
 * smb_tree_lookup_odir
 *
 * Find the specified odir in the tree's list of odirs, and
 * attempt to obtain a hold on the odir.
 *
 * Returns NULL if odir not found or a hold cannot be obtained.
 */
smb_odir_t *
smb_tree_lookup_odir(smb_tree_t *tree, uint16_t odid)
{
	smb_odir_t	*od;
	smb_llist_t	*od_list;

	ASSERT(tree);
	ASSERT(tree->t_magic == SMB_TREE_MAGIC);

	od_list = &tree->t_odir_list;
	smb_llist_enter(od_list, RW_READER);

	od = smb_llist_head(od_list);
	while (od) {
		if (od->d_odid == odid) {
			if (!smb_odir_hold(od))
				od = NULL;
			break;
		}
		od = smb_llist_next(od_list, od);
	}

	smb_llist_exit(od_list);
	return (od);
}

boolean_t
smb_tree_is_connected(smb_tree_t *tree)
{
	boolean_t	rb;

	mutex_enter(&tree->t_mutex);
	rb = smb_tree_is_connected_locked(tree);
	mutex_exit(&tree->t_mutex);
	return (rb);
}

/*
 * Get the next open ofile in the list.  A reference is taken on
 * the ofile, which can be released later with smb_ofile_release().
 *
 * If the specified ofile is NULL, search from the beginning of the
 * list.  Otherwise, the search starts just after that ofile.
 *
 * Returns NULL if there are no open files in the list.
 */
static smb_ofile_t *
smb_tree_get_ofile(smb_tree_t *tree, smb_ofile_t *of)
{
	smb_llist_t *ofile_list;

	ASSERT(tree);
	ASSERT(tree->t_magic == SMB_TREE_MAGIC);

	ofile_list = &tree->t_ofile_list;
	smb_llist_enter(ofile_list, RW_READER);

	if (of) {
		ASSERT(of->f_magic == SMB_OFILE_MAGIC);
		of = smb_llist_next(ofile_list, of);
	} else {
		of = smb_llist_head(ofile_list);
	}

	while (of) {
		if (smb_ofile_hold(of))
			break;

		of = smb_llist_next(ofile_list, of);
	}

	smb_llist_exit(ofile_list);
	return (of);
}

/*
 * smb_tree_get_odir
 *
 * Find the next odir in the tree's list of odirs, and obtain a
 * hold on it.
 * If the specified odir is NULL the search starts at the beginning
 * of the tree's odir list, otherwise the search starts after the
 * specified odir.
 */
static smb_odir_t *
smb_tree_get_odir(smb_tree_t *tree, smb_odir_t *od)
{
	smb_llist_t *od_list;

	ASSERT(tree);
	ASSERT(tree->t_magic == SMB_TREE_MAGIC);

	od_list = &tree->t_odir_list;
	smb_llist_enter(od_list, RW_READER);

	if (od) {
		ASSERT(od->d_magic == SMB_ODIR_MAGIC);
		od = smb_llist_next(od_list, od);
	} else {
		od = smb_llist_head(od_list);
	}

	while (od) {
		ASSERT(od->d_magic == SMB_ODIR_MAGIC);

		if (smb_odir_hold(od))
			break;
		od = smb_llist_next(od_list, od);
	}

	smb_llist_exit(od_list);
	return (od);
}

/*
 * smb_tree_close_odirs
 *
 * Close all open odirs in the tree's list which were opened by
 * the process identified by pid.
 * If pid is zero, close all open odirs in the tree's list.
 */
static void
smb_tree_close_odirs(smb_tree_t *tree, uint16_t pid)
{
	smb_odir_t *od, *next_od;

	ASSERT(tree);
	ASSERT(tree->t_magic == SMB_TREE_MAGIC);

	od = smb_tree_get_odir(tree, NULL);
	while (od) {
		ASSERT(od->d_magic == SMB_ODIR_MAGIC);
		ASSERT(od->d_tree == tree);

		next_od = smb_tree_get_odir(tree, od);
		if ((pid == 0) || (od->d_opened_by_pid == pid))
			smb_odir_close(od);
		smb_odir_release(od);

		od = next_od;
	}
}

static void
smb_tree_set_execinfo(smb_tree_t *tree, smb_shr_execinfo_t *exec, int exec_type)
{
	exec->e_sharename = tree->t_sharename;
	exec->e_sharepath = tree->t_resource;
	exec->e_winname = tree->t_user->u_name;
	exec->e_userdom = tree->t_user->u_domain;
	exec->e_srv_ipaddr = tree->t_session->local_ipaddr;
	exec->e_cli_ipaddr = tree->t_session->ipaddr;
	exec->e_cli_netbiosname = tree->t_session->workstation;
	exec->e_uid = crgetuid(tree->t_user->u_cred);
	exec->e_type = exec_type;
}

/*
 * Private function to support smb_tree_enum.
 */
static int
smb_tree_enum_private(smb_tree_t *tree, smb_svcenum_t *svcenum)
{
	uint8_t *pb;
	uint_t nbytes;
	int rc;
	smb_svcenum_qualifier_t *seq;

	if (svcenum->se_nskip > 0) {
		svcenum->se_nskip--;
		return (0);
	}

	seq = &svcenum->se_qualifier;
	if (seq->seq_mode == SMB_SVCENUM_CONNECT_SHARE) {
		if (smb_strcasecmp(seq->seq_qualstr, tree->t_sharename,
		    sizeof (seq->seq_qualstr)) != 0)
			return (0);
	}

	if (svcenum->se_nitems >= svcenum->se_nlimit) {
		svcenum->se_nitems = svcenum->se_nlimit;
		return (0);
	}

	pb = &svcenum->se_buf[svcenum->se_bused];
	rc = smb_tree_netinfo_encode(tree, pb, svcenum->se_bavail, &nbytes);
	if (rc == 0) {
		svcenum->se_bavail -= nbytes;
		svcenum->se_bused += nbytes;
		svcenum->se_nitems++;
	}

	return (rc);
}

/*
 * Encode connection information into a buffer: connection information
 * needed in user space to support RPC requests.
 */
static int
smb_tree_netinfo_encode(smb_tree_t *tree, uint8_t *buf, size_t buflen,
    uint32_t *nbytes)
{
	smb_netconnectinfo_t	info;
	int			rc;

	smb_tree_netinfo_init(tree, &info);
	rc = smb_netconnectinfo_encode(&info, buf, buflen, nbytes);
	smb_tree_netinfo_fini(&info);

	return (rc);
}

/*
 * Note: ci_numusers should be the number of users connected to
 * the share rather than the number of references on the tree but
 * we don't have a mechanism to track users/share in smbsrv yet.
 */
static void
smb_tree_netinfo_init(smb_tree_t *tree, smb_netconnectinfo_t *info)
{
	smb_user_t	*user;

	ASSERT(tree);

	info->ci_id = tree->t_tid;
	info->ci_type = tree->t_res_type;
	info->ci_numopens = tree->t_open_files;
	info->ci_numusers = tree->t_refcnt;
	info->ci_time = gethrestime_sec() - tree->t_connect_time;

	info->ci_sharelen = strlen(tree->t_sharename) + 1;
	info->ci_share = smb_mem_strdup(tree->t_sharename);

	user = tree->t_user;
	ASSERT(user);

	info->ci_namelen = user->u_domain_len + user->u_name_len + 2;
	info->ci_username = kmem_alloc(info->ci_namelen, KM_SLEEP);
	(void) snprintf(info->ci_username, info->ci_namelen, "%s\\%s",
	    user->u_domain, user->u_name);
}

static void
smb_tree_netinfo_fini(smb_netconnectinfo_t *info)
{
	if (info == NULL)
		return;

	if (info->ci_username)
		kmem_free(info->ci_username, info->ci_namelen);
	if (info->ci_share)
		smb_mem_free(info->ci_share);

	bzero(info, sizeof (smb_netconnectinfo_t));
}
