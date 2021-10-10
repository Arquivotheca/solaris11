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
 * User State Machine
 * ------------------
 *
 *		    |
 *		    | T0 smb_user_alloc
 *		    v
 *    +---------------------------------+
 *    |  SMB_USER_STATE_AUTHENTICATING  |
 *    +---------------------------------+
 *		    |
 *		    | T1 smb_user_logon
 *		    v
 *    +-----------------------------+
 *    |  SMB_USER_STATE_LOGGED_IN   |
 *    +-----------------------------+
 *		    |
 *		    | T2 smb_user_logoff
 *		    v
 *    +-----------------------------+
 *    |  SMB_USER_STATE_LOGGING_OFF |
 *    +-----------------------------+
 *		    |
 *		    | T3 smb_user_release
 *		    v
 *    +-----------------------------+
 *    |  SMB_USER_STATE_LOGGED_OFF  |
 *    +-----------------------------+
 *		    |
 *		    | T4 smb_user_delete
 *		    v
 *
 *
 * SMB_USER_STATE_AUTHENTICATING
 *    While in this state:
 *	- The user has been created and is being authenticated
 *	- The user is NOT queued in the list of users of this session.
 *
 * SMB_USER_STATE_LOGGED_IN
 *    While in this state:
 *      - The user is queued in the list of users of this session.
 *      - References will be given out if the user is looked up.
 *      - The user can access files and pipes.
 *
 * SMB_USER_STATE_LOGGING_OFF
 *    While in this state:
 *      - The user is queued in the list of users of this session.
 *      - References will not be given out if the user is looked up.
 *      - The trees the user connected are being disconnected.
 *      - The resources associated with the user remain.
 *
 * SMB_USER_STATE_LOGGED_OFF
 *    While in this state:
 *      - The user is queued in the list of users of this session.
 *      - References will not be given out if the user is looked up.
 *      - The user has no more trees connected.
 *      - The resources associated with the user remain.
 *
 * Transition T3
 *    This transition occurs in smb_user_release(). The resources associated
 *    with the user are deleted as well as the user. For the transition to
 *    occur, the user must be in the SMB_USER_STATE_LOGGED_OFF state and the
 *    reference count be zero.
 *
 * Comments
 * --------
 *
 *    The state machine of the user structures is controlled by 3 elements:
 *      - The list of users of the session he belongs to.
 *      - The mutex embedded in the structure itself.
 *      - The reference count.
 *
 *    There's a mutex embedded in the user structure used to protect its fields
 *    and there's a lock embedded in the list of users of a session. To
 *    increment or to decrement the reference count the mutex must be entered.
 *    To insert the user into the list of users of the session and to remove
 *    the user from it, the lock must be entered in RW_WRITER mode.
 *
 *    Rules of access to a user structure:
 *
 *    1) In order to avoid deadlocks, when both (mutex and lock of the session
 *       list) have to be entered, the lock must be entered first.
 *
 *    2) All actions applied to a user require a reference count.
 *
 *    3) There are 2 ways of getting a reference count. One is when the user
 *       logs in. The other when the user is looked up.
 *
 *    It should be noted that the reference count of a user registers the
 *    number of references to the user in other structures (such as an smb
 *    request). The reference count is not incremented in these 2 instances:
 *
 *    1) The user is logged in. An user is anchored by his state. If there's
 *       no activity involving a user currently logged in, the reference
 *       count of that user is zero.
 *
 *    2) The user is queued in the list of users of the session. The fact of
 *       being queued in that list is NOT registered by incrementing the
 *       reference count.
 */
#include <sys/types.h>
#include <sys/sid.h>
#include <sys/priv_names.h>
#include <sys/idmap.h>
#include <smbsrv/smb_kproto.h>
#include <smbsrv/smb_door.h>

#define	ADMINISTRATORS_SID	"S-1-5-32-544"

static boolean_t smb_user_is_logged_in(smb_user_t *);
static int smb_user_enum_private(smb_user_t *, smb_svcenum_t *);
static smb_tree_t *smb_user_get_tree(smb_llist_t *, smb_tree_t *);
static int smb_user_setcred(smb_user_t *, smb_token_t *);
static cred_t *smb_cred_create(smb_token_t *);
static void smb_cred_set_sid(smb_id_t *, ksid_t *);
static ksidlist_t *smb_cred_set_sidlist(smb_ids_t *);
static uint32_t smb_priv_xlate(smb_token_t *);
static boolean_t smb_token_query_privilege(smb_token_t *, int);
static smb_tree_t *smb_user_lookup_share(smb_user_t *, uint64_t, smb_tree_t *);

/*
 * Create a new user.
 */
smb_user_t *
smb_user_alloc(smb_session_t *session)
{
	smb_user_t	*user;

	SMB_SESSION_VALID(session);

	user = kmem_cache_alloc(session->s_server->si_cache_user, KM_SLEEP);
	bzero(user, sizeof (smb_user_t));

	if (!smb_idpool_alloc(&session->s_uid_pool, &user->u_uid)) {
		if (!smb_idpool_constructor(&user->u_tid_pool)) {
			smb_llist_constructor(&user->u_tree_list,
			    sizeof (smb_tree_t), offsetof(smb_tree_t, t_lnd));
			mutex_init(&user->u_mutex, NULL, MUTEX_DEFAULT, NULL);

			user->u_state = SMB_USER_STATE_AUTHENTICATING;
			user->u_magic = SMB_USER_MAGIC;
			user->u_refcnt = 0;
			user->u_session = session;
			user->u_server = session->s_server;
			return (user);
		}
		smb_idpool_free(&session->s_uid_pool, user->u_uid);
	}
	kmem_cache_free(session->s_server->si_cache_user, user);
	return (NULL);
}

/*
 * Update user's authentication information from the token, and
 * transition to LOGGED_ON state.
 */
int
smb_user_logon(smb_user_t *user, smb_token_t *token)
{
	SMB_USER_VALID(user);

	ASSERT(user->u_state == SMB_USER_STATE_AUTHENTICATING);

	if (!smb_token_valid(token)) {
		smb_errcode_seterror(NT_STATUS_LOGON_FAILURE,
		    ERRDOS, ERROR_LOGON_FAILURE);
		return (-1);
	}

	if (smb_user_setcred(user, token) != 0) {
		smb_errcode_seterror(0, ERRDOS, ERROR_INVALID_HANDLE);
		return (-1);
	}

	user->u_logon_time = gethrestime_sec();
	user->u_flags = token->tkn_flags;
	user->u_name_len = strlen(token->tkn_account_name) + 1;
	user->u_domain_len = strlen(token->tkn_domain_name) + 1;
	user->u_name = smb_mem_strdup(token->tkn_account_name);
	user->u_domain = smb_mem_strdup(token->tkn_domain_name);
	if (token->tkn_posix_name)
		user->u_posix_name = smb_mem_strdup(token->tkn_posix_name);
	user->u_state = SMB_USER_STATE_LOGGED_IN;

	return (0);
}

/*
 * smb_user_logoff
 *
 * Change the user state and disconnect trees.
 * The user list must not be entered or modified here.
 */
void
smb_user_logoff(smb_user_t *user)
{
	ASSERT(user->u_magic == SMB_USER_MAGIC);

	mutex_enter(&user->u_mutex);
	ASSERT(user->u_refcnt);
	switch (user->u_state) {
	case SMB_USER_STATE_LOGGED_IN: {
		/*
		 * The user is moved into a state indicating that the log off
		 * process has started.
		 */
		user->u_state = SMB_USER_STATE_LOGGING_OFF;
		mutex_exit(&user->u_mutex);
		/*
		 * All the trees hanging off of this user are disconnected.
		 */
		smb_user_disconnect_trees(user);
		smb_shrmgr_autohome_unpublish(user->u_name);
		smb_user_logoff_upcall(user);
		mutex_enter(&user->u_mutex);
		user->u_state = SMB_USER_STATE_LOGGED_OFF;
		smb_server_dec_users(user->u_server);
		break;
	}
	case SMB_USER_STATE_LOGGED_OFF:
	case SMB_USER_STATE_LOGGING_OFF:
		break;

	case SMB_USER_STATE_AUTHENTICATING:
	default:
		ASSERT(0);
		break;
	}
	mutex_exit(&user->u_mutex);
}

/*
 * Take a reference on a user.
 */
boolean_t
smb_user_hold(smb_user_t *user)
{
	ASSERT(user);
	ASSERT(user->u_magic == SMB_USER_MAGIC);

	mutex_enter(&user->u_mutex);

	if (smb_user_is_logged_in(user)) {
		user->u_refcnt++;
		mutex_exit(&user->u_mutex);
		return (B_TRUE);
	}

	mutex_exit(&user->u_mutex);
	return (B_FALSE);
}

/*
 * Release a reference on a user.  If the reference count falls to
 * zero and the user has logged off, post the object for deletion.
 * Object deletion is deferred to avoid modifying a list while an
 * iteration may be in progress.
 */
void
smb_user_release(smb_user_t *user)
{
	ASSERT(user->u_magic == SMB_USER_MAGIC);

	mutex_enter(&user->u_mutex);
	ASSERT(user->u_refcnt);
	user->u_refcnt--;

	/* flush the tree list's delete queue */
	smb_llist_flush(&user->u_tree_list);

	switch (user->u_state) {
	case SMB_USER_STATE_LOGGED_OFF:
		if (user->u_refcnt == 0)
			smb_session_post_user(user->u_session, user);
		break;

	case SMB_USER_STATE_LOGGED_IN:
	case SMB_USER_STATE_LOGGING_OFF:
		break;

	case SMB_USER_STATE_AUTHENTICATING:
	default:
		ASSERT(0);
		break;
	}
	mutex_exit(&user->u_mutex);
}

void
smb_user_post_tree(smb_user_t *user, smb_tree_t *tree)
{
	SMB_USER_VALID(user);
	SMB_TREE_VALID(tree);
	ASSERT(tree->t_refcnt == 0);
	ASSERT(tree->t_state == SMB_TREE_STATE_DISCONNECTED);
	ASSERT(tree->t_user == user);

	smb_llist_post(&user->u_tree_list, tree, smb_tree_dealloc);
}


/*
 * Find a tree by tree-id.
 */
smb_tree_t *
smb_user_lookup_tree(smb_user_t *user, uint16_t tid)
{
	smb_tree_t	*tree;

	ASSERT(user);
	ASSERT(user->u_magic == SMB_USER_MAGIC);

	smb_llist_enter(&user->u_tree_list, RW_READER);
	tree = smb_llist_head(&user->u_tree_list);

	while (tree) {
		ASSERT(tree->t_magic == SMB_TREE_MAGIC);
		ASSERT(tree->t_user == user);

		if (tree->t_tid == tid) {
			if (smb_tree_hold(tree)) {
				smb_llist_exit(&user->u_tree_list);
				return (tree);
			} else {
				smb_llist_exit(&user->u_tree_list);
				return (NULL);
			}
		}

		tree = smb_llist_next(&user->u_tree_list, tree);
	}

	smb_llist_exit(&user->u_tree_list);
	return (NULL);
}

/*
 * Find the first connected tree that matches the specified sharename.
 * If the specified tree is NULL the search starts from the beginning of
 * the user's tree list.  If a tree is provided the search starts just
 * after that tree.
 */
static smb_tree_t *
smb_user_lookup_share(
    smb_user_t		*user,
    uint64_t		share_id,
    smb_tree_t		*tree)
{
	ASSERT(user);
	ASSERT(user->u_magic == SMB_USER_MAGIC);

	smb_llist_enter(&user->u_tree_list, RW_READER);

	if (tree) {
		ASSERT(tree->t_magic == SMB_TREE_MAGIC);
		ASSERT(tree->t_user == user);
		tree = smb_llist_next(&user->u_tree_list, tree);
	} else {
		tree = smb_llist_head(&user->u_tree_list);
	}

	while (tree) {
		ASSERT(tree->t_magic == SMB_TREE_MAGIC);
		ASSERT(tree->t_user == user);
		if (tree->t_shareid == share_id) {
			if (smb_tree_hold(tree)) {
				smb_llist_exit(&user->u_tree_list);
				return (tree);
			}
		}
		tree = smb_llist_next(&user->u_tree_list, tree);
	}

	smb_llist_exit(&user->u_tree_list);
	return (NULL);
}

/*
 * Find the first connected tree that matches the specified volume name.
 * If the specified tree is NULL the search starts from the beginning of
 * the user's tree list.  If a tree is provided the search starts just
 * after that tree.
 */
smb_tree_t *
smb_user_lookup_volume(
    smb_user_t		*user,
    const char		*name,
    smb_tree_t		*tree)
{
	ASSERT(user);
	ASSERT(user->u_magic == SMB_USER_MAGIC);
	ASSERT(name);

	smb_llist_enter(&user->u_tree_list, RW_READER);

	if (tree) {
		ASSERT(tree->t_magic == SMB_TREE_MAGIC);
		ASSERT(tree->t_user == user);
		tree = smb_llist_next(&user->u_tree_list, tree);
	} else {
		tree = smb_llist_head(&user->u_tree_list);
	}

	while (tree) {
		ASSERT(tree->t_magic == SMB_TREE_MAGIC);
		ASSERT(tree->t_user == user);

		if (smb_strcasecmp(tree->t_volume, name, 0) == 0) {
			if (smb_tree_hold(tree)) {
				smb_llist_exit(&user->u_tree_list);
				return (tree);
			}
		}

		tree = smb_llist_next(&user->u_tree_list, tree);
	}

	smb_llist_exit(&user->u_tree_list);
	return (NULL);
}

/*
 * Disconnect all trees that match the specified client process-id.
 */
void
smb_user_close_pid(
    smb_user_t		*user,
    uint16_t		pid)
{
	smb_tree_t	*tree;

	ASSERT(user);
	ASSERT(user->u_magic == SMB_USER_MAGIC);

	tree = smb_user_get_tree(&user->u_tree_list, NULL);
	while (tree) {
		smb_tree_t *next;
		ASSERT(tree->t_user == user);
		smb_tree_close_pid(tree, pid);
		next = smb_user_get_tree(&user->u_tree_list, tree);
		smb_tree_release(tree);
		tree = next;
	}
}

/*
 * Disconnect all trees that this user has connected.
 */
void
smb_user_disconnect_trees(
    smb_user_t		*user)
{
	smb_tree_t	*tree;

	ASSERT(user);
	ASSERT(user->u_magic == SMB_USER_MAGIC);

	tree = smb_user_get_tree(&user->u_tree_list, NULL);
	while (tree) {
		ASSERT(tree->t_user == user);
		smb_tree_disconnect(tree, B_TRUE);
		smb_tree_release(tree);
		tree = smb_user_get_tree(&user->u_tree_list, NULL);
	}
}

/*
 * Disconnect all trees that match the specified share.
 */
void
smb_user_disconnect_share(smb_user_t *user, uint64_t share_id)
{
	smb_tree_t	*tree;
	smb_tree_t	*next;

	ASSERT(user);
	ASSERT(user->u_magic == SMB_USER_MAGIC);
	ASSERT(user->u_refcnt);

	tree = smb_user_lookup_share(user, share_id, NULL);
	while (tree) {
		ASSERT(tree->t_magic == SMB_TREE_MAGIC);
		smb_session_cancel_requests(user->u_session, tree, NULL);
		smb_tree_disconnect(tree, B_TRUE);
		next = smb_user_lookup_share(user, share_id, tree);
		smb_tree_release(tree);
		tree = next;
	}
}

/*
 * Close a file by its unique id.
 */
int
smb_user_fclose(smb_user_t *user, uint32_t uniqid)
{
	smb_llist_t	*tree_list;
	smb_tree_t	*tree;
	int		rc = ENOENT;

	ASSERT(user);
	ASSERT(user->u_magic == SMB_USER_MAGIC);

	tree_list = &user->u_tree_list;
	ASSERT(tree_list);

	smb_llist_enter(tree_list, RW_READER);
	tree = smb_llist_head(tree_list);

	while ((tree != NULL) && (rc == ENOENT)) {
		ASSERT(tree->t_user == user);

		if (smb_tree_hold(tree)) {
			rc = smb_tree_fclose(tree, uniqid);
			smb_tree_release(tree);
		}

		tree = smb_llist_next(tree_list, tree);
	}

	smb_llist_exit(tree_list);
	return (rc);
}

/*
 * Determine whether or not the user is an administrator.
 * Members of the administrators group have administrative rights.
 */
boolean_t
smb_user_is_admin(smb_user_t *user)
{
	char		sidstr[SMB_SID_STRSZ];
	ksidlist_t	*ksidlist;
	ksid_t		ksid1;
	ksid_t		*ksid2;
	boolean_t	rc = B_FALSE;
	int		i;

	ASSERT(user);
	ASSERT(user->u_cred);

	if (SMB_USER_IS_ADMIN(user))
		return (B_TRUE);

	bzero(&ksid1, sizeof (ksid_t));
	(void) strlcpy(sidstr, ADMINISTRATORS_SID, SMB_SID_STRSZ);
	ASSERT(smb_sid_splitstr(sidstr, &ksid1.ks_rid) == 0);
	ksid1.ks_domain = ksid_lookupdomain(sidstr);

	ksidlist = crgetsidlist(user->u_cred);
	ASSERT(ksidlist);
	ASSERT(ksid1.ks_domain);
	ASSERT(ksid1.ks_domain->kd_name);

	i = 0;
	ksid2 = crgetsid(user->u_cred, KSID_USER);
	do {
		ASSERT(ksid2->ks_domain);
		ASSERT(ksid2->ks_domain->kd_name);

		if (strcmp(ksid1.ks_domain->kd_name,
		    ksid2->ks_domain->kd_name) == 0 &&
		    ksid1.ks_rid == ksid2->ks_rid) {
			user->u_flags |= SMB_USER_FLAG_ADMIN;
			rc = B_TRUE;
			break;
		}

		ksid2 = &ksidlist->ksl_sids[i];
	} while (i++ < ksidlist->ksl_nsid);

	ksid_rele(&ksid1);
	return (rc);
}

/*
 * This function should be called with a hold on the user.
 */
boolean_t
smb_user_namecmp(smb_user_t *user, const char *name)
{
	char		*fq_name;
	boolean_t	match;

	if (smb_strcasecmp(name, user->u_name, 0) == 0)
		return (B_TRUE);

	fq_name = kmem_alloc(MAXNAMELEN, KM_SLEEP);

	(void) snprintf(fq_name, MAXNAMELEN, "%s\\%s",
	    user->u_domain, user->u_name);

	match = (smb_strcasecmp(name, fq_name, 0) == 0);
	if (!match) {
		(void) snprintf(fq_name, MAXNAMELEN, "%s@%s",
		    user->u_name, user->u_domain);

		match = (smb_strcasecmp(name, fq_name, 0) == 0);
	}

	kmem_free(fq_name, MAXNAMELEN);
	return (match);
}

/*
 * If the enumeration request is for user data, handle the request
 * here.  Otherwise, pass it on to the trees.
 *
 * This function should be called with a hold on the user.
 */
int
smb_user_enum(smb_user_t *user, smb_svcenum_t *svcenum)
{
	smb_tree_t	*tree;
	smb_tree_t	*next;
	int		rc = 0;

	ASSERT(user);
	ASSERT(user->u_magic == SMB_USER_MAGIC);

	if (svcenum->se_type == SMB_SVCENUM_TYPE_USER)
		return (smb_user_enum_private(user, svcenum));

	tree = smb_user_get_tree(&user->u_tree_list, NULL);
	while (tree) {
		ASSERT(tree->t_user == user);

		rc = smb_tree_enum(tree, svcenum);
		if (rc != 0) {
			smb_tree_release(tree);
			break;
		}

		next = smb_user_get_tree(&user->u_tree_list, tree);
		smb_tree_release(tree);
		tree = next;
	}

	return (rc);
}

/*
 * Determine whether or not a user is logged in.
 * Typically, a reference can only be taken on a logged-in user.
 *
 * This is a private function and must be called with the user
 * mutex held.
 */
static boolean_t
smb_user_is_logged_in(smb_user_t *user)
{
	switch (user->u_state) {
	case SMB_USER_STATE_LOGGED_IN:
		return (B_TRUE);

	case SMB_USER_STATE_AUTHENTICATING:
	case SMB_USER_STATE_LOGGING_OFF:
	case SMB_USER_STATE_LOGGED_OFF:
		return (B_FALSE);

	default:
		ASSERT(0);
		return (B_FALSE);
	}
}

/*
 * Freeing resources associated with the user.
 * The tree list should be empty.
 */
void
smb_user_delete(void *arg)
{
	smb_user_t	*user = (smb_user_t *)arg;

	SMB_USER_VALID(user);
	ASSERT(user->u_refcnt == 0);
	ASSERT((user->u_state == SMB_USER_STATE_LOGGED_OFF) ||
	    (user->u_state == SMB_USER_STATE_AUTHENTICATING));

	mutex_enter(&user->u_mutex);
	mutex_exit(&user->u_mutex);

	smb_idpool_free(&user->u_session->s_uid_pool, user->u_uid);
	user->u_magic = (uint32_t)~SMB_USER_MAGIC;
	mutex_destroy(&user->u_mutex);
	smb_llist_destructor(&user->u_tree_list);
	smb_idpool_destructor(&user->u_tid_pool);
	if (user->u_cred)
		crfree(user->u_cred);
	if (user->u_privcred)
		crfree(user->u_privcred);
	smb_mem_free(user->u_name);
	smb_mem_free(user->u_domain);
	smb_mem_free(user->u_posix_name);
	kmem_cache_free(user->u_server->si_cache_user, user);
}

/*
 * Get the next connected tree in the list.  A reference is taken on
 * the tree, which can be released later with smb_tree_release().
 *
 * If the specified tree is NULL the search starts from the beginning of
 * the tree list.  If a tree is provided the search starts just after
 * that tree.
 *
 * Returns NULL if there are no connected trees in the list.
 */
static smb_tree_t *
smb_user_get_tree(smb_llist_t *tree_list, smb_tree_t *tree)
{
	ASSERT(tree_list);

	smb_llist_enter(tree_list, RW_READER);

	if (tree) {
		ASSERT(tree->t_magic == SMB_TREE_MAGIC);
		tree = smb_llist_next(tree_list, tree);
	} else {
		tree = smb_llist_head(tree_list);
	}

	while (tree) {
		if (smb_tree_hold(tree))
			break;

		tree = smb_llist_next(tree_list, tree);
	}

	smb_llist_exit(tree_list);
	return (tree);
}

cred_t *
smb_user_getcred(smb_user_t *user)
{
	return (user->u_cred);
}

cred_t *
smb_user_getprivcred(smb_user_t *user)
{
	return ((user->u_privcred)? user->u_privcred : user->u_cred);
}

/*
 * Assign the user cred and privileges determined from the token.
 *
 * If the user has backup and/or restore privleges, dup the cred
 * and add those privileges to this new privileged cred.
 */
static int
smb_user_setcred(smb_user_t *user, smb_token_t *token)
{
	cred_t		*cr, *privcred = NULL;
	uint32_t	privileges;

	if ((cr = smb_cred_create(token)) == NULL)
		return (-1);

	privileges = smb_priv_xlate(token);

	if (privileges & (SMB_USER_PRIV_BACKUP | SMB_USER_PRIV_RESTORE))
		privcred = crdup(cr);

	if (privcred != NULL) {
		if (privileges & SMB_USER_PRIV_BACKUP) {
			(void) crsetpriv(privcred, PRIV_FILE_DAC_READ,
			    PRIV_FILE_DAC_SEARCH, PRIV_SYS_MOUNT, NULL);
		}

		if (privileges & SMB_USER_PRIV_RESTORE) {
			(void) crsetpriv(privcred, PRIV_FILE_DAC_WRITE,
			    PRIV_FILE_CHOWN, PRIV_FILE_CHOWN_SELF,
			    PRIV_FILE_DAC_SEARCH, PRIV_FILE_LINK_ANY,
			    PRIV_FILE_OWNER, PRIV_FILE_SETID,
			    PRIV_SYS_LINKDIR, PRIV_SYS_MOUNT, NULL);
		}
	}

	user->u_cred = cr;
	user->u_privcred = privcred;
	user->u_privileges = privileges;
	return (0);
}


/*
 * Allocate a Solaris cred and initialize it based on the access token.
 *
 * If the user can be mapped to a non-ephemeral ID, the cred gid is set
 * to the Solaris user's primary group.
 *
 * If the mapped UID is ephemeral, or the primary group could not be
 * obtained, the cred gid is set to whatever Solaris group is mapped
 * to the token's primary group.
 */
static cred_t *
smb_cred_create(smb_token_t *token)
{
	ksid_t			ksid;
	ksidlist_t		*ksidlist = NULL;
	smb_posix_grps_t	*posix_grps;
	cred_t			*cr;
	gid_t			gid;

	ASSERT(token);
	ASSERT(token->tkn_posix_grps);
	posix_grps = token->tkn_posix_grps;

	cr = crget();
	ASSERT(cr != NULL);

	if (!IDMAP_ID_IS_EPHEMERAL(token->tkn_user.i_id) &&
	    (posix_grps->pg_ngrps != 0)) {
		gid = posix_grps->pg_grps[0];
	} else {
		gid = token->tkn_primary_grp.i_id;
	}

	if (crsetugid(cr, token->tkn_user.i_id, gid) != 0) {
		crfree(cr);
		return (NULL);
	}

	if (crsetgroups(cr, posix_grps->pg_ngrps, posix_grps->pg_grps) != 0) {
		crfree(cr);
		return (NULL);
	}

	smb_cred_set_sid(&token->tkn_user, &ksid);
	crsetsid(cr, &ksid, KSID_USER);
	smb_cred_set_sid(&token->tkn_primary_grp, &ksid);
	crsetsid(cr, &ksid, KSID_GROUP);
	smb_cred_set_sid(&token->tkn_owner, &ksid);
	crsetsid(cr, &ksid, KSID_OWNER);
	ksidlist = smb_cred_set_sidlist(&token->tkn_win_grps);
	crsetsidlist(cr, ksidlist);

	if (smb_token_query_privilege(token, SE_TAKE_OWNERSHIP_LUID))
		(void) crsetpriv(cr, PRIV_FILE_CHOWN, NULL);

	return (cr);
}

/*
 * Initialize the ksid based on the given smb_id_t.
 */
static void
smb_cred_set_sid(smb_id_t *id, ksid_t *ksid)
{
	char sidstr[SMB_SID_STRSZ];
	int rc;

	ASSERT(id);
	ASSERT(id->i_sid);

	ksid->ks_id = id->i_id;
	smb_sid_tostr(id->i_sid, sidstr);
	rc = smb_sid_splitstr(sidstr, &ksid->ks_rid);
	ASSERT(rc == 0);

	ksid->ks_attr = id->i_attrs;
	ksid->ks_domain = ksid_lookupdomain(sidstr);
}

/*
 * Allocate and initialize the ksidlist based on the access token group list.
 */
static ksidlist_t *
smb_cred_set_sidlist(smb_ids_t *token_grps)
{
	int i;
	ksidlist_t *lp;

	lp = kmem_zalloc(KSIDLIST_MEM(token_grps->i_cnt), KM_SLEEP);
	lp->ksl_ref = 1;
	lp->ksl_nsid = token_grps->i_cnt;
	lp->ksl_neid = 0;

	for (i = 0; i < lp->ksl_nsid; i++) {
		smb_cred_set_sid(&token_grps->i_ids[i], &lp->ksl_sids[i]);
		if (lp->ksl_sids[i].ks_id > IDMAP_WK__MAX_GID)
			lp->ksl_neid++;
	}

	return (lp);
}

/*
 * Convert access token privileges to local definitions.
 */
static uint32_t
smb_priv_xlate(smb_token_t *token)
{
	uint32_t	privileges = 0;

	if (smb_token_query_privilege(token, SE_BACKUP_LUID))
		privileges |= SMB_USER_PRIV_BACKUP;

	if (smb_token_query_privilege(token, SE_RESTORE_LUID))
		privileges |= SMB_USER_PRIV_RESTORE;

	if (smb_token_query_privilege(token, SE_TAKE_OWNERSHIP_LUID))
		privileges |= SMB_USER_PRIV_TAKE_OWNERSHIP;

	if (smb_token_query_privilege(token, SE_SECURITY_LUID))
		privileges |= SMB_USER_PRIV_SECURITY;

	return (privileges);
}

/*
 * smb_token_query_privilege
 *
 * Find out if the specified privilege is enable in the given
 * access token.
 */
static boolean_t
smb_token_query_privilege(smb_token_t *token, int priv_id)
{
	smb_privset_t *privset;
	int i;

	if ((token == NULL) || (token->tkn_privileges == NULL))
		return (B_FALSE);

	privset = token->tkn_privileges;
	for (i = 0; privset->priv_cnt; i++) {
		if (privset->priv[i].luid.lo_part == priv_id)
			return (privset->priv[i].attrs == SE_PRIVILEGE_ENABLED);
	}

	return (B_FALSE);
}

/*
 * Private function to support smb_user_enum.
 */
static int
smb_user_enum_private(smb_user_t *user, smb_svcenum_t *svcenum)
{
	uint8_t *pb;
	uint_t nbytes;
	int rc;

	if (svcenum->se_nskip > 0) {
		svcenum->se_nskip--;
		return (0);
	}

	if (svcenum->se_nitems >= svcenum->se_nlimit) {
		svcenum->se_nitems = svcenum->se_nlimit;
		return (0);
	}

	pb = &svcenum->se_buf[svcenum->se_bused];
	rc = smb_user_netinfo_encode(user, pb, svcenum->se_bavail, &nbytes);
	if (rc == 0) {
		svcenum->se_bavail -= nbytes;
		svcenum->se_bused += nbytes;
		svcenum->se_nitems++;
	}

	return (rc);
}

/*
 * Encode the NetInfo for a user into a buffer.  NetInfo contains
 * information that is often needed in user space to support RPC
 * requests.
 */
int
smb_user_netinfo_encode(smb_user_t *user, uint8_t *buf, size_t buflen,
    uint32_t *nbytes)
{
	smb_netuserinfo_t	info;
	int			rc;

	smb_user_netinfo_init(user, &info);
	rc = smb_netuserinfo_encode(&info, buf, buflen, nbytes);
	smb_user_netinfo_fini(&info);

	return (rc);
}

void
smb_user_netinfo_init(smb_user_t *user, smb_netuserinfo_t *info)
{
	smb_session_t *session;

	ASSERT(user);
	ASSERT(user->u_domain);
	ASSERT(user->u_name);

	session = user->u_session;
	ASSERT(session);
	ASSERT(session->workstation);

	info->ui_session_id = session->s_kid;
	info->ui_native_os = session->native_os;
	info->ui_ipaddr = session->ipaddr;
	info->ui_numopens = session->s_file_cnt;
	info->ui_smb_uid = user->u_uid;
	info->ui_logon_time = user->u_logon_time;
	info->ui_flags = user->u_flags;

	info->ui_domain = smb_mem_strdup(user->u_domain);
	info->ui_account = smb_mem_strdup(user->u_name);
	if (user->u_posix_name != NULL)
		info->ui_posix_name = smb_mem_strdup(user->u_posix_name);
	else
		info->ui_posix_name = NULL;

	info->ui_workstation = smb_mem_alloc(MAXNAMELEN);
	smb_session_getclient(session, info->ui_workstation, MAXNAMELEN);
}

void
smb_user_netinfo_fini(smb_netuserinfo_t *info)
{
	if (info == NULL)
		return;

	if (info->ui_domain)
		smb_mem_free(info->ui_domain);
	if (info->ui_account)
		smb_mem_free(info->ui_account);
	if (info->ui_workstation)
		smb_mem_free(info->ui_workstation);
	if (info->ui_posix_name)
		smb_mem_free(info->ui_posix_name);

	bzero(info, sizeof (smb_netuserinfo_t));
}

uint32_t
smb_user_auth_upcall(smb_authreq_t *authreq, smb_authrsp_t *authrsp)
{
	uint32_t	status;

	status = smb_kdoor_upcall(SMB_DR_USER_AUTH,
	    authreq, smb_authreq_xdr, authrsp, smb_authrsp_xdr);

	return (status);
}

void
smb_user_logoff_upcall(smb_user_t *user)
{
	smb_logoff_t	logoff;

	bzero(&logoff, sizeof (smb_logoff_t));
	logoff.lo_session_id = user->u_session->s_kid;
	logoff.lo_user_id = user->u_uid;

	(void) smb_kdoor_upcall(SMB_DR_USER_LOGOFF,
	    &logoff, smb_logoff_xdr, NULL, NULL);
}
