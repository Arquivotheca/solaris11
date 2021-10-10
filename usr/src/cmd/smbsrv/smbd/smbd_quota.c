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

#include <sys/avl.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <attr.h>
#include <unistd.h>
#include <libuutil.h>
#include <libzfs.h>
#include <assert.h>
#include <stddef.h>
#include <strings.h>
#include <errno.h>
#include <synch.h>
#include <smbsrv/smb_xdr.h>
#include <smbsrv/libntsvcs.h>
#include <smbsrv/smb_idmap.h>
#include "smbd.h"

/*
 * smbd_quota subsystem interface
 * ------------------------------
 * Management of the smbd_quota_fs_list (see below).
 * smbd_quota_init
 * smbd_quota_fini
 * smbd_quota_add_fs
 * smbd_quota_remove_fs
 *
 * smbd_quota public interface
 * --------------------------
 * Handling of requests to query and set quota data on a filesystem.
 * smbd_quota_query - query user/group quotas on a filesystem
 * smbd_quota_set - set user/group quotas ona filesystem
 * smbd_quota_free - delete the quota list created in smbd_quota_query
 */

/*
 * Querying user & group quotas - smbd_quota_query
 *
 * In order to fulfill the quota query requests that can be received
 * from clients, it is required that the quota data can be provided in
 * a well defined and consistent order, and that a request can specify
 * at which quota entry to begin the query.
 *
 * Quota Tree
 * Since the file system does not support the above, an avl tree is
 * populated with the file system's user and group quota data, and
 * then used to provide the data to respond to query requests. The
 * avl tree is indexed by the SID.
 * Each node of the avl tree is an smbd_quota_t structure.
 *
 * Quota List
 * There is a list of avl trees, one per file system.
 * Each node in the list is an smbd_quota_tree_t structure.
 * The list is created via a call to smbd_quota_init() when the library
 * is initialized, and destroyed via a call to smbd_quota_fini() when
 * the library is fini'd.
 *
 * An avl tree for a specific file system is created and added to the
 * list via a call to smbd_quota_add_fs() when the file system is shared,
 * and removed from the list via a call to smbd_quota_remove_fs() when
 * the file system is unshared.
 *
 * An avl tree is (re)populated, if required, whenever a quota request
 * (EXCLUDING a resume request) is received for its filesystem. The
 * avl tree is considered to be expired (needs to be repopulated) if
 * either of the following have occurred since it was last (re)populated:
 * - SMB_QUOTA_REFRESH seconds have elapsed OR
 * - a quota set operation has been performed on its file system
 *
 * In order to perform a smbd_quota_query/set operation on a file system
 * the appropriate quota tree must be identified and locked via a call
 * to smbd_quota_tree_lookup(), The quota tree is locked (qt_locked == B_TRUE)
 * until the caller releases it via a call to smbd_quota_tree_release().
 */

/*
 * smbd_quota_tree_t
 * Represents an avl tree of user quotas for a file system.
 *
 * qt_refcnt - a count of the number of users of the tree.
 * qt_refcnt is also incremented and decremented when the tree is
 * added to and removed from the quota list.
 * The tree cannot be deleted until this count is zero.
 *
 * qt_sharecnt - a count of the shares of the file system which the
 * tree represents.  smb_quota_remove_fs() cannot remove the tree from
 * removed from the quota list until this count is zero.
 *
 * qt_locked - B_TRUE if someone is currently using the tree, in
 * which case a lookup will wait for the tree to become available.
 */
typedef struct smbd_quota_tree {
	list_node_t	qt_node;
	char		*qt_path;
	time_t		qt_timestamp;
	uint32_t	qt_refcnt;
	uint32_t	qt_sharecnt;
	boolean_t	qt_locked;
	avl_tree_t	qt_avl;
	mutex_t		qt_mutex;
} smbd_quota_tree_t;

/*
 * smbd_quota_fs_list
 * list of quota trees; one per shared file system.
 */
static list_t smbd_quota_fs_list;
static boolean_t smbd_quota_list_init = B_FALSE;
static boolean_t smbd_quota_shutdown = B_FALSE;
static mutex_t smbd_quota_list_mutex = DEFAULTMUTEX;
static cond_t smbd_quota_list_condvar;
static uint32_t smbd_quota_tree_cnt = 0;
static int smbd_quota_fini_timeout = 1; /* seconds */

/*
 * smbd_quota_zfs_handle_t
 * handle to zfs library and dataset
 */
typedef struct smbd_quota_zfs_handle {
	libzfs_handle_t *z_lib;
	zfs_handle_t *z_fs;
} smbd_quota_zfs_handle_t;

/*
 * smbd_quota_zfs_arg_t
 * arg passed to zfs callback when querying quota properties
 */
typedef struct smbd_quota_zfs_arg {
	zfs_userquota_prop_t qa_prop;
	avl_tree_t *qa_avl;
} smbd_quota_zfs_arg_t;

static void smbd_quota_add_ctrldir(const char *);
static void smbd_quota_remove_ctrldir(const char *);

static smbd_quota_tree_t *smbd_quota_tree_create(const char *);
static void smbd_quota_tree_delete(smbd_quota_tree_t *);

static smbd_quota_tree_t *smbd_quota_tree_lookup(const char *);
static void smbd_quota_tree_release(smbd_quota_tree_t *);
static boolean_t smbd_quota_tree_match(smbd_quota_tree_t *, const char *);
static int smbd_quota_sid_cmp(const void *, const void *);
static uint32_t smbd_quota_tree_populate(smbd_quota_tree_t *);
static boolean_t smbd_quota_tree_expired(smbd_quota_tree_t *);
static void smbd_quota_tree_set_expired(smbd_quota_tree_t *);

static uint32_t smbd_quota_zfs_init(const char *, smbd_quota_zfs_handle_t *);
static void smbd_quota_zfs_fini(smbd_quota_zfs_handle_t *);
static void smbd_quota_zfs_get_userquota(smbd_quota_zfs_handle_t *,
    smb_quota_sid_t *, smb_quota_t *quota);
static uint32_t smbd_quota_zfs_get_quotas(smbd_quota_tree_t *);
static int smbd_quota_zfs_callback(void *, const char *, uid_t, uint64_t);
static uint32_t smbd_quota_zfs_set_quotas(smbd_quota_tree_t *,
    smb_quota_set_t *);
static int smbd_quota_sidstr(uint32_t, zfs_userquota_prop_t, char *);
static uint32_t smbd_quota_sidtype(smbd_quota_tree_t *, char *);
static int smbd_quota_getid(char *, uint32_t, uint32_t *);

static uint32_t smbd_quota_query_all(smbd_quota_tree_t *,
    smb_quota_query_t *, smb_quota_response_t *);
static uint32_t smbd_quota_query_list(smbd_quota_tree_t *,
    smb_quota_query_t *, smb_quota_response_t *);
static uint32_t smbd_quota_query_user(smb_quota_query_t *,
    smb_quota_response_t *);

#define	SMB_QUOTA_REFRESH		2
#define	SMB_QUOTA_CMD_LENGTH		21
#define	SMB_QUOTA_CMD_STR_LENGTH	SMB_SID_STRSZ+SMB_QUOTA_CMD_LENGTH

/*
 * In order to display the quota properties tab, windows clients
 * check for the existence of the quota control file.
 */
#define	SMB_QUOTA_CNTRL_DIR		".$EXTEND"
#define	SMB_QUOTA_CNTRL_FILE		"$QUOTA"
#define	SMB_QUOTA_CNTRL_INDEX_XATTR	"SUNWsmb:$Q:$INDEX_ALLOCATION"
#define	SMB_QUOTA_CNTRL_PERM		"everyone@:rwpaARWc::allow"

/*
 * smbd_quota_init
 * Initialize the list to hold the quota trees.
 */
void
smbd_quota_init(void)
{
	(void) mutex_lock(&smbd_quota_list_mutex);
	if (!smbd_quota_list_init) {
		list_create(&smbd_quota_fs_list, sizeof (smbd_quota_tree_t),
		    offsetof(smbd_quota_tree_t, qt_node));
		smbd_quota_list_init = B_TRUE;
		smbd_quota_shutdown = B_FALSE;
	}
	(void) mutex_unlock(&smbd_quota_list_mutex);
}

/*
 * smbd_quota_fini
 *
 * Wait for each quota tree to not be in use (qt_refcnt == 1)
 * then remove it from the list and delete it.
 */
void
smbd_quota_fini(void)
{
	smbd_quota_tree_t *qtree, *qtree_next;
	boolean_t remove;
	struct timespec tswait;
	tswait.tv_sec = smbd_quota_fini_timeout;
	tswait.tv_nsec = 0;

	(void) mutex_lock(&smbd_quota_list_mutex);
	smbd_quota_shutdown = B_TRUE;

	if (!smbd_quota_list_init) {
		(void) mutex_unlock(&smbd_quota_list_mutex);
		return;
	}

	(void) cond_broadcast(&smbd_quota_list_condvar);

	while (!list_is_empty(&smbd_quota_fs_list)) {
		qtree = list_head(&smbd_quota_fs_list);
		while (qtree != NULL) {
			qtree_next = list_next(&smbd_quota_fs_list, qtree);

			(void) mutex_lock(&qtree->qt_mutex);
			remove = (qtree->qt_refcnt == 1);
			if (remove) {
				list_remove(&smbd_quota_fs_list, qtree);
				--qtree->qt_refcnt;
			}
			(void) mutex_unlock(&qtree->qt_mutex);

			if (remove)
				smbd_quota_tree_delete(qtree);

			qtree = qtree_next;
		}

		if (!list_is_empty(&smbd_quota_fs_list)) {
			if (cond_reltimedwait(&smbd_quota_list_condvar,
			    &smbd_quota_list_mutex, &tswait) == ETIME) {
				smbd_log(LOG_WARNING,
				    "quota shutdown timeout expired");
				break;
			}
		}
	}

	if (list_is_empty(&smbd_quota_fs_list)) {
		list_destroy(&smbd_quota_fs_list);
		smbd_quota_list_init = B_FALSE;
	}

	(void) mutex_unlock(&smbd_quota_list_mutex);
}

/*
 * smbd_quota_add_fs
 *
 * If there is not a quota tree representing the specified path,
 * create one and add it to the list.
 */
void
smbd_quota_add_fs(const char *path)
{
	smbd_quota_tree_t *qtree;

	(void) mutex_lock(&smbd_quota_list_mutex);

	if (!smbd_quota_list_init || smbd_quota_shutdown) {
		(void) mutex_unlock(&smbd_quota_list_mutex);
		return;
	}

	qtree = list_head(&smbd_quota_fs_list);
	while (qtree != NULL) {
		if (smbd_quota_tree_match(qtree, path)) {
			(void) mutex_lock(&qtree->qt_mutex);
			++qtree->qt_sharecnt;
			(void) mutex_unlock(&qtree->qt_mutex);
			break;
		}
		qtree = list_next(&smbd_quota_fs_list, qtree);
	}

	if (qtree == NULL) {
		qtree = smbd_quota_tree_create(path);
		if (qtree)
			list_insert_head(&smbd_quota_fs_list, (void *)qtree);
	}

	if (qtree)
		smbd_quota_add_ctrldir(path);

	(void) mutex_unlock(&smbd_quota_list_mutex);
}

/*
 * smbd_quota_remove_fs
 *
 * If this is the last share that the quota tree represents
 * (qtree->qt_sharecnt == 0) remove the qtree from the list.
 * The qtree will be deleted if/when there is nobody using it
 * (qtree->qt_refcnt == 0).
 */
void
smbd_quota_remove_fs(const char *path)
{
	smbd_quota_tree_t *qtree;
	boolean_t delete = B_FALSE;

	(void) mutex_lock(&smbd_quota_list_mutex);

	if (!smbd_quota_list_init || smbd_quota_shutdown) {
		(void) mutex_unlock(&smbd_quota_list_mutex);
		return;
	}

	qtree = list_head(&smbd_quota_fs_list);
	while (qtree != NULL) {
		assert(qtree->qt_refcnt > 0);
		if (smbd_quota_tree_match(qtree, path)) {
			(void) mutex_lock(&qtree->qt_mutex);
			--qtree->qt_sharecnt;
			if (qtree->qt_sharecnt == 0) {
				list_remove(&smbd_quota_fs_list, (void *)qtree);
				smbd_quota_remove_ctrldir(qtree->qt_path);
				--(qtree->qt_refcnt);
				delete = (qtree->qt_refcnt == 0);
			}
			(void) mutex_unlock(&qtree->qt_mutex);
			if (delete)
				smbd_quota_tree_delete(qtree);
			break;
		}
		qtree = list_next(&smbd_quota_fs_list, qtree);
	}
	(void) mutex_unlock(&smbd_quota_list_mutex);
}

/*
 * smbd_quota_query
 *
 * Get list of user/group quotas entries.
 * Request->qq_query_op determines whether to get quota entries
 * for the specified SIDs (smbd_quota_query_list) OR to get all
 * quota entries, optionally starting at a specified SID.
 *
 * Returns NT_STATUS codes.
 */
uint32_t
smbd_quota_query(smb_quota_query_t *request, smb_quota_response_t *reply)
{
	uint32_t status;
	smbd_quota_tree_t *qtree;
	smb_quota_query_op_t query_op = request->qq_query_op;

	list_create(&reply->qr_quota_list, sizeof (smb_quota_t),
	    offsetof(smb_quota_t, q_list_node));

	/* SMB_QUOTA_QUERY_USER does not use the quota cache */
	if (query_op == SMB_QUOTA_QUERY_USER) {
		status = smbd_quota_query_user(request, reply);
		return (status);
	}

	qtree = smbd_quota_tree_lookup(request->qq_root_path);
	if (qtree == NULL)
		return (NT_STATUS_INVALID_PARAMETER);

	/* If NOT resuming a previous query all, refresh qtree if required */
	if ((query_op != SMB_QUOTA_QUERY_ALL) || (request->qq_restart)) {
		status = smbd_quota_tree_populate(qtree);
		if (status != NT_STATUS_SUCCESS) {
			smbd_quota_tree_release(qtree);
			return (status);
		}
	}

	switch (query_op) {
	case SMB_QUOTA_QUERY_SIDLIST:
		status = smbd_quota_query_list(qtree, request, reply);
		break;
	case SMB_QUOTA_QUERY_STARTSID:
	case SMB_QUOTA_QUERY_ALL:
		status = smbd_quota_query_all(qtree, request, reply);
		break;
	case SMB_QUOTA_QUERY_INVALID_OP:
	default:
		status = NT_STATUS_INVALID_PARAMETER;
		break;
	}

	smbd_quota_tree_release(qtree);

	return (status);
}

/*
 * smb_quota_set
 *
 * Set the list of quota entries.
 */
uint32_t
smbd_quota_set(smb_quota_set_t *request)
{
	uint32_t status;
	smbd_quota_tree_t *qtree;

	qtree = smbd_quota_tree_lookup(request->qs_root_path);
	if (qtree == NULL)
		return (NT_STATUS_INVALID_PARAMETER);

	status = smbd_quota_zfs_set_quotas(qtree, request);

	smbd_quota_tree_set_expired(qtree);
	smbd_quota_tree_release(qtree);

	return (status);
}

/*
 * smb_quota_free
 *
 * This method frees quota entries.
 */
void
smbd_quota_free(smb_quota_response_t *reply)
{
	list_t *list = &reply->qr_quota_list;
	smb_quota_t *quota;

	while ((quota = list_head(list)) != NULL) {
		list_remove(list, quota);
		free(quota);
	}

	list_destroy(list);
}

/*
 * smbd_quota_query_all
 *
 * Query quotas sequentially from tree, optionally starting at a
 * specified sid. If request->qq_single is TRUE only one quota
 * should be returned, otherwise up to request->qq_max_quota
 * should be returned.
 *
 * SMB_QUOTA_QUERY_STARTSID
 * The query should start at the startsid, the first sid in
 * request->qq_sid_list.
 *
 * SMQ_QUOTA_QUERY_ALL
 * If request->qq_restart the query should restart at the start
 * of the avl tree. Otherwise the first sid in request->qq_sid_list
 * is the resume sid and the query should start at the tree entry
 * after the one it refers to.
 *
 * Returns NT_STATUS codes.
 */
static uint32_t
smbd_quota_query_all(smbd_quota_tree_t *qtree, smb_quota_query_t *request,
    smb_quota_response_t *reply)
{
	avl_tree_t *avl_tree = &qtree->qt_avl;
	avl_index_t where;
	list_t *sid_list, *quota_list;
	smb_quota_sid_t *sid;
	smb_quota_t *quota, *quotal, key;
	uint32_t count;

	/* find starting sid */
	if (request->qq_query_op == SMB_QUOTA_QUERY_STARTSID) {
		sid_list = &request->qq_sid_list;
		sid = list_head(sid_list);
		(void) strlcpy(key.q_sidstr, sid->qs_sidstr, SMB_SID_STRSZ);
		quota = avl_find(avl_tree, &key, &where);
		if (quota == NULL)
			return (NT_STATUS_INVALID_PARAMETER);
	} else if (request->qq_restart) {
		quota = avl_first(avl_tree);
		if (quota == NULL)
			return (NT_STATUS_NO_MORE_ENTRIES);
	} else {
		sid_list = &request->qq_sid_list;
		sid = list_head(sid_list);
		(void) strlcpy(key.q_sidstr, sid->qs_sidstr, SMB_SID_STRSZ);
		quota = avl_find(avl_tree, &key, &where);
		if (quota == NULL)
			return (NT_STATUS_INVALID_PARAMETER);
		quota = AVL_NEXT(avl_tree, quota);
		if (quota == NULL)
			return (NT_STATUS_NO_MORE_ENTRIES);
	}

	if ((request->qq_single) && (request->qq_max_quota > 1))
		request->qq_max_quota = 1;

	quota_list = &reply->qr_quota_list;
	count = 0;
	while (quota) {
		if (count >= request->qq_max_quota)
			break;

		quotal = malloc(sizeof (smb_quota_t));
		if (quotal == NULL)
			return (NT_STATUS_NO_MEMORY);
		bcopy(quota, quotal, sizeof (smb_quota_t));

		list_insert_tail(quota_list, quotal);
		++count;

		quota = AVL_NEXT(avl_tree, quota);
	}

	return (NT_STATUS_SUCCESS);
}

/*
 * smbd_quota_query_list
 *
 * Iterate through request sid list querying the avl tree for each.
 * Insert an entry in the reply quota list for each sid.
 * For any sid that cannot be found in the avl tree, the reply
 * quota list entry should contain zeros.
 */
static uint32_t
smbd_quota_query_list(smbd_quota_tree_t *qtree, smb_quota_query_t *request,
    smb_quota_response_t *reply)
{
	avl_tree_t *avl_tree = &qtree->qt_avl;
	avl_index_t where;
	list_t *sid_list, *quota_list;
	smb_quota_sid_t *sid;
	smb_quota_t *quota, *quotal, key;

	quota_list = &reply->qr_quota_list;
	sid_list = &request->qq_sid_list;
	sid = list_head(sid_list);
	while (sid) {
		quotal = malloc(sizeof (smb_quota_t));
		if (quotal == NULL)
			return (NT_STATUS_NO_MEMORY);

		(void) strlcpy(key.q_sidstr, sid->qs_sidstr, SMB_SID_STRSZ);
		quota = avl_find(avl_tree, &key, &where);
		if (quota) {
			bcopy(quota, quotal, sizeof (smb_quota_t));
		} else {
			bzero(quotal, sizeof (smb_quota_t));
			(void) strlcpy(quotal->q_sidstr, sid->qs_sidstr,
			    SMB_SID_STRSZ);
		}

		list_insert_tail(quota_list, quotal);
		sid = list_next(sid_list, sid);
	}

	return (NT_STATUS_SUCCESS);
}

/*
 * smbd_quota_query_user
 *
 * This request is used to query the quota and usage information by
 * UID or GID. It is intended to obtain a small number of quota entries,
 * for example when querying file system usage for a single user. It is
 * therefore more efficient to bypass the quota cashe (avl tree) and
 * go directly to zfs to obtain the data per uid/gid.
 */
static uint32_t
smbd_quota_query_user(smb_quota_query_t *request, smb_quota_response_t *reply)
{
	list_t *sid_list, *quota_list;
	smb_quota_sid_t *sid;
	smb_quota_t *quota;
	smbd_quota_zfs_handle_t zfs_hdl;
	uint32_t status;

	status = smbd_quota_zfs_init(request->qq_root_path, &zfs_hdl);
	if (status != NT_STATUS_SUCCESS)
		return (status);

	quota_list = &reply->qr_quota_list;
	sid_list = &request->qq_sid_list;
	sid = list_head(sid_list);

	while (sid != NULL) {
		quota = malloc(sizeof (smb_quota_t));
		if (quota == NULL) {
			status = NT_STATUS_NO_MEMORY;
			break;
		}

		smbd_quota_zfs_get_userquota(&zfs_hdl, sid, quota);

		list_insert_tail(quota_list, quota);
		sid = list_next(sid_list, sid);
	}

	smbd_quota_zfs_fini(&zfs_hdl);
	return (status);
}

/*
 * smbd_quota_zfs_set_quotas
 *
 * This method sets the list of quota entries.
 *
 * A quota list or threshold value of SMB_QUOTA_UNLIMITED means that
 * the user / group does not have a quota limit. In ZFS this maps to
 * 0 (none).
 * A quota list or threshold value of (SMB_QUOTA_UNLIMITED - 1) means
 * that the user / group quota should be removed. In ZFS this maps to
 * 0 (none).
 */
static uint32_t
smbd_quota_zfs_set_quotas(smbd_quota_tree_t *qtree, smb_quota_set_t *request)
{
	smbd_quota_zfs_handle_t zfs_hdl;
	char *typestr, qsetstr[SMB_QUOTA_CMD_STR_LENGTH];
	char qlimit[SMB_QUOTA_CMD_LENGTH];
	list_t *quota_list;
	smb_quota_t *quota;
	uint32_t id;
	uint32_t status = NT_STATUS_SUCCESS;
	uint32_t sidtype;

	status = smbd_quota_zfs_init(request->qs_root_path, &zfs_hdl);
	if (status != NT_STATUS_SUCCESS)
		return (status);

	quota_list = &request->qs_quota_list;
	quota = list_head(quota_list);

	while (quota) {
		if ((quota->q_limit == SMB_QUOTA_UNLIMITED) ||
		    (quota->q_limit == (SMB_QUOTA_UNLIMITED - 1))) {
			quota->q_limit = 0;
		}
		(void) snprintf(qlimit, SMB_QUOTA_CMD_LENGTH, "%llu",
		    quota->q_limit);

		sidtype = smbd_quota_sidtype(qtree, quota->q_sidstr);
		switch (sidtype) {
		case SidTypeUser:
			typestr = "userquota";
			break;
		case SidTypeWellKnownGroup:
		case SidTypeGroup:
		case SidTypeAlias:
			typestr = "groupquota";
			break;
		default:
			smbd_log(LOG_WARNING, "Failed to set quota for %s: "
			    "%s (%d) not valid for quotas", quota->q_sidstr,
			    smb_sid_type2str(sidtype), sidtype);
			quota = list_next(quota_list, quota);
			continue;
		}

		if ((smbd_quota_getid(quota->q_sidstr, sidtype, &id) == 0) &&
		    !(IDMAP_ID_IS_EPHEMERAL(id))) {
			(void) snprintf(qsetstr, SMB_QUOTA_CMD_STR_LENGTH,
			    "%s@%d", typestr, id);
		} else {
			(void) snprintf(qsetstr, SMB_QUOTA_CMD_STR_LENGTH,
			    "%s@%s", typestr, quota->q_sidstr);
		}

		errno = 0;
		if (zfs_prop_set(zfs_hdl.z_fs, qsetstr, qlimit) != 0) {
			smbd_log(LOG_WARNING, "Failed to set quota for %s: %s",
			    quota->q_sidstr, strerror(errno));
			status = NT_STATUS_INVALID_PARAMETER;
			break;
		}

		quota = list_next(quota_list, quota);
	}

	smbd_quota_zfs_fini(&zfs_hdl);
	return (status);
}

/*
 * smbd_quota_sidtype
 *
 * Determine the type of the sid. If the sid exists in
 * the qtree get its type from there, otherwise do an
 * lsa_lookup_sid().
 */
static uint32_t
smbd_quota_sidtype(smbd_quota_tree_t *qtree, char *sidstr)
{
	smb_quota_t key, *quota;
	avl_index_t where;
	smb_sid_t *sid = NULL;
	smb_account_t ainfo;
	uint32_t sidtype = SidTypeUnknown;

	(void) strlcpy(key.q_sidstr, sidstr, SMB_SID_STRSZ);
	quota = avl_find(&qtree->qt_avl, &key, &where);
	if (quota)
		return (quota->q_sidtype);

	sid = smb_sid_fromstr(sidstr);
	if (sid != NULL) {
		if (lsa_lookup_sid(sid, &ainfo) == NT_STATUS_SUCCESS) {
			sidtype = ainfo.a_type;
			smb_account_free(&ainfo);
		}
		smb_sid_free(sid);
	}
	return (sidtype);
}

/*
 * smbd_quota_getid
 *
 * Get the user/group id for the sid.
 */
static int
smbd_quota_getid(char *sidstr, uint32_t sidtype, uint32_t *id)
{
	int rc = 0;
	smb_sid_t *sid = NULL;
	int idtype;

	sid = smb_sid_fromstr(sidstr);
	if (sid == NULL)
		return (-1);

	switch (sidtype) {
	case SidTypeUser:
		idtype = SMB_IDMAP_USER;
		break;
	case SidTypeWellKnownGroup:
	case SidTypeGroup:
	case SidTypeAlias:
		idtype = SMB_IDMAP_GROUP;
		break;
	default:
		rc = -1;
		break;
	}

	if (rc == 0)
		rc = smb_idmap_getid(sid, id, &idtype);

	smb_sid_free(sid);

	return (rc);
}

/*
 * smbd_quota_tree_lookup
 *
 * Find the quota tree in smbd_quota_fs_list.
 *
 * If the tree is found but is locked, waits for it to become available.
 * If the tree is available, locks it and returns it.
 * Otherwise, returns NULL.
 */
static smbd_quota_tree_t *
smbd_quota_tree_lookup(const char *path)
{
	smbd_quota_tree_t *qtree = NULL;

	assert(path);
	(void) mutex_lock(&smbd_quota_list_mutex);

	qtree = list_head(&smbd_quota_fs_list);
	while (qtree != NULL) {
		if (!smbd_quota_list_init || smbd_quota_shutdown) {
			(void) mutex_unlock(&smbd_quota_list_mutex);
			return (NULL);
		}

		(void) mutex_lock(&qtree->qt_mutex);
		assert(qtree->qt_refcnt > 0);

		if (!smbd_quota_tree_match(qtree, path)) {
			(void) mutex_unlock(&qtree->qt_mutex);
			qtree = list_next(&smbd_quota_fs_list, qtree);
			continue;
		}

		if (qtree->qt_locked) {
			(void) mutex_unlock(&qtree->qt_mutex);
			(void) cond_wait(&smbd_quota_list_condvar,
			    &smbd_quota_list_mutex);
			qtree = list_head(&smbd_quota_fs_list);
			continue;
		}

		++(qtree->qt_refcnt);
		qtree->qt_locked = B_TRUE;
		(void) mutex_unlock(&qtree->qt_mutex);
		break;
	};

	(void) mutex_unlock(&smbd_quota_list_mutex);
	return (qtree);
}

/*
 * smbd_quota_tree_release
 */
static void
smbd_quota_tree_release(smbd_quota_tree_t *qtree)
{
	boolean_t delete;

	(void) mutex_lock(&qtree->qt_mutex);
	assert(qtree->qt_locked);
	assert(qtree->qt_refcnt > 0);

	--(qtree->qt_refcnt);
	qtree->qt_locked = B_FALSE;
	delete = (qtree->qt_refcnt == 0);
	(void) mutex_unlock(&qtree->qt_mutex);

	(void) mutex_lock(&smbd_quota_list_mutex);
	if (delete)
		smbd_quota_tree_delete(qtree);
	(void) cond_broadcast(&smbd_quota_list_condvar);
	(void) mutex_unlock(&smbd_quota_list_mutex);
}

/*
 * smbd_quota_tree_match
 *
 * Determine if qtree represents the file system identified by path
 */
static boolean_t
smbd_quota_tree_match(smbd_quota_tree_t *qtree, const char *path)
{
	return (strncmp(qtree->qt_path, path, MAXPATHLEN) == 0);
}

/*
 * smb_quota_tree_create
 *
 * Create and initialize an smbd_quota_tree_t structure
 */
static smbd_quota_tree_t *
smbd_quota_tree_create(const char *path)
{
	smbd_quota_tree_t *qtree;

	assert(MUTEX_HELD(&smbd_quota_list_mutex));

	qtree = calloc(sizeof (smbd_quota_tree_t), 1);
	if (qtree == NULL)
		return (NULL);

	qtree->qt_path = strdup(path);
	if (qtree->qt_path == NULL) {
		free(qtree);
		return (NULL);
	}

	qtree->qt_timestamp = 0;
	qtree->qt_locked = B_FALSE;
	qtree->qt_refcnt = 1;
	qtree->qt_sharecnt = 1;

	avl_create(&qtree->qt_avl, smbd_quota_sid_cmp,
	    sizeof (smb_quota_t), offsetof(smb_quota_t, q_avl_node));

	++smbd_quota_tree_cnt;
	return (qtree);
}

/*
 * smbd_quota_tree_delete
 *
 * Free and delete the smbd_quota_tree_t structure.
 * qtree must have no users (refcnt == 0).
 */
static void
smbd_quota_tree_delete(smbd_quota_tree_t *qtree)
{
	void *cookie = NULL;
	smb_quota_t *node;

	assert(MUTEX_HELD(&smbd_quota_list_mutex));
	assert(qtree->qt_refcnt == 0);

	while ((node = avl_destroy_nodes(&qtree->qt_avl, &cookie)) != NULL)
		free(node);
	avl_destroy(&qtree->qt_avl);

	free(qtree->qt_path);
	free(qtree);

	--smbd_quota_tree_cnt;
}

/*
 * smb_quota_sid_cmp
 *
 * Comparision function for nodes in an AVL tree which holds quota
 * entries indexed by SID.
 */
static int
smbd_quota_sid_cmp(const void *l_arg, const void *r_arg)
{
	const char *l_sid = ((smb_quota_t *)l_arg)->q_sidstr;
	const char *r_sid = ((smb_quota_t *)r_arg)->q_sidstr;
	int ret;

	ret = strncasecmp(l_sid, r_sid, SMB_SID_STRSZ);

	if (ret > 0)
		return (1);
	if (ret < 0)
		return (-1);
	return (0);
}

/*
 * smbd_quota_tree_populate
 *
 * If the quota tree needs to be (re)populated:
 * - delete the qtree's contents
 * - repopulate the qtree from zfs
 * - set the qtree's timestamp.
 */
static uint32_t
smbd_quota_tree_populate(smbd_quota_tree_t *qtree)
{
	void *cookie = NULL;
	void *node;
	uint32_t status;

	assert(qtree->qt_locked);

	if (!smbd_quota_tree_expired(qtree))
		return (NT_STATUS_SUCCESS);

	while ((node = avl_destroy_nodes(&qtree->qt_avl, &cookie)) != NULL)
		free(node);

	status = smbd_quota_zfs_get_quotas(qtree);
	if (status != NT_STATUS_SUCCESS)
		return (status);

	qtree->qt_timestamp = time(NULL);

	return (NT_STATUS_SUCCESS);
}

static boolean_t
smbd_quota_tree_expired(smbd_quota_tree_t *qtree)
{
	time_t tnow = time(NULL);
	return ((tnow - qtree->qt_timestamp) > SMB_QUOTA_REFRESH);
}

static void
smbd_quota_tree_set_expired(smbd_quota_tree_t *qtree)
{
	qtree->qt_timestamp = 0;
}

/*
 * smbd_quota_zfs_get_userquota
 *
 * Query the user or group quota and usage for the specified user or group.
 * If the specified id is ephemeral, determine the sidstr and use that,
 * instead of the id, to get the quota
 * If either usage or quota can't be found, quota, limit and threshold are
 * all returned as zero, as if there's no quota set for the user.
 */
static void
smbd_quota_zfs_get_userquota(smbd_quota_zfs_handle_t *zfs_hdl,
    smb_quota_sid_t *qsid, smb_quota_t *quota)
{
	char *quotastr, *usedstr;
	char idstr[SMB_QUOTA_CMD_STR_LENGTH];
	char querystr[SMB_QUOTA_CMD_STR_LENGTH];
	uint64_t qused, qlimit;
	smb_sid_t *sid;
	uint32_t idmap_type;
	int rc;

	bzero(quota, sizeof (smb_quota_t));

	switch (qsid->qs_idtype) {
	case SMB_QUOTA_UID:
		quotastr = "userquota@";
		usedstr = "userused@";
		idmap_type = SMB_IDMAP_USER;
		break;
	case SMB_QUOTA_GID:
		quotastr = "groupquota@";
		usedstr = "groupused@";
		idmap_type = SMB_IDMAP_GROUP;
		break;
	default:
		smbd_log(LOG_ERR, "Invalid quota type: %d", qsid->qs_idtype);
		assert(0);
		return;
	}

	if (IDMAP_ID_IS_EPHEMERAL(qsid->qs_id)) {
		rc = smb_idmap_getsid(qsid->qs_id, idmap_type, &sid);
		if (rc != IDMAP_SUCCESS) {
			smbd_log(LOG_WARNING, "Failed to get sid for %d: "
			    "error %d", qsid->qs_id, rc);
			return;
		}
		smb_sid_tostr(sid, idstr);
		smb_sid_free(sid);
	} else {
		(void) snprintf(idstr, SMB_QUOTA_CMD_STR_LENGTH,
		    "%u", qsid->qs_id);
	}

	(void) snprintf(querystr, SMB_QUOTA_CMD_STR_LENGTH, "%s%s",
	    quotastr, idstr);
	rc = zfs_prop_get_userquota_int(zfs_hdl->z_fs, querystr, &qlimit);
	if (rc != 0) {
		smbd_log(LOG_WARNING, "Failed to get quota for %d: %s",
		    qsid->qs_id, strerror(errno));
		return;
	}

	(void) snprintf(querystr, SMB_QUOTA_CMD_STR_LENGTH, "%s%s",
	    usedstr, idstr);
	rc = zfs_prop_get_userquota_int(zfs_hdl->z_fs, querystr, &qused);
	if (rc != 0) {
		smbd_log(LOG_WARNING, "Failed to get quota for %d: %s",
		    qsid->qs_id, strerror(errno));
		return;
	}

	quota->q_limit = qlimit;
	quota->q_thresh = qlimit;
	quota->q_used = qused;
}

/*
 * smbd_quota_zfs_get_quotas
 *
 * Get user and group quotas from ZFS and use them to
 * populate the quota tree.
 */
static uint32_t
smbd_quota_zfs_get_quotas(smbd_quota_tree_t *qtree)
{
	smbd_quota_zfs_handle_t zfs_hdl;
	smbd_quota_zfs_arg_t arg;
	zfs_userquota_prop_t p;
	uint32_t status = NT_STATUS_SUCCESS;

	status = smbd_quota_zfs_init(qtree->qt_path, &zfs_hdl);
	if (status != NT_STATUS_SUCCESS)
		return (status);

	arg.qa_avl = &qtree->qt_avl;
	for (p = 0; p < ZFS_NUM_USERQUOTA_PROPS; p++) {
		arg.qa_prop = p;
		if (zfs_userspace(zfs_hdl.z_fs, p,
		    smbd_quota_zfs_callback, &arg) != 0) {
			status = NT_STATUS_INTERNAL_ERROR;
			break;
		}
	}

	smbd_quota_zfs_fini(&zfs_hdl);
	return (status);
}

/*
 * smbd_quota_zfs_callback
 *
 * Find or create a node in the avl tree (arg->qa_avl) that matches
 * the SID derived from domain and rid. If no domain is specified,
 * lookup the sid (smbd_quota_sidstr()).
 * Populate the node.
 * The property type (arg->qa_prop) determines which property 'space'
 * refers to.
 */
static int
smbd_quota_zfs_callback(void *arg, const char *domain, uid_t rid,
    uint64_t space)
{
	smbd_quota_zfs_arg_t *qarg = (smbd_quota_zfs_arg_t *)arg;
	zfs_userquota_prop_t qprop = qarg->qa_prop;
	avl_tree_t *avl_tree = qarg->qa_avl;
	avl_index_t where;
	smb_quota_t *quota, key;

	if (domain == NULL || domain[0] == '\0') {
		if (smbd_quota_sidstr(rid, qprop, key.q_sidstr) != 0)
			return (0);
	} else {
		(void) snprintf(key.q_sidstr, SMB_SID_STRSZ, "%s-%u",
		    domain, (uint32_t)rid);
	}

	quota = avl_find(avl_tree, &key, &where);
	if (quota == NULL) {
		quota = malloc(sizeof (smb_quota_t));
		if (quota == NULL)
			return (NT_STATUS_NO_MEMORY);
		bzero(quota, sizeof (smb_quota_t));
		quota->q_thresh = SMB_QUOTA_UNLIMITED;
		quota->q_limit = SMB_QUOTA_UNLIMITED;
		avl_insert(avl_tree, (void *)quota, where);
		(void) strlcpy(quota->q_sidstr, key.q_sidstr, SMB_SID_STRSZ);
	}

	switch (qprop) {
	case ZFS_PROP_USERUSED:
		quota->q_sidtype = SidTypeUser;
		quota->q_used = space;
		break;
	case ZFS_PROP_GROUPUSED:
		quota->q_sidtype = SidTypeGroup;
		quota->q_used = space;
		break;
	case ZFS_PROP_USERQUOTA:
		quota->q_sidtype = SidTypeUser;
		quota->q_limit = space;
		break;
	case ZFS_PROP_GROUPQUOTA:
		quota->q_sidtype = SidTypeGroup;
		quota->q_limit = space;
		break;
	default:
		break;
	}

	quota->q_thresh = quota->q_limit;

	return (0);
}

/*
 * smbd_quota_sidstr
 *
 * Use idmap to get the sid for the specified id and return
 * the string version of the sid in sidstr.
 * sidstr must be a buffer of at least SMB_SID_STRSZ.
 */
static int
smbd_quota_sidstr(uint32_t id, zfs_userquota_prop_t qprop, char *sidstr)
{
	int idtype;
	smb_sid_t *sid;

	switch (qprop) {
	case ZFS_PROP_USERUSED:
	case ZFS_PROP_USERQUOTA:
		idtype = SMB_IDMAP_USER;
		break;
	case ZFS_PROP_GROUPUSED:
	case ZFS_PROP_GROUPQUOTA:
		idtype = SMB_IDMAP_GROUP;
		break;
	default:
		return (-1);
	}

	if (smb_idmap_getsid(id, idtype, &sid) != IDMAP_SUCCESS)
		return (-1);

	smb_sid_tostr(sid, sidstr);
	smb_sid_free(sid);

	return (0);
}

/*
 * smbd_quota_zfs_init
 *
 * Initialize zfs library and dataset handles
 */
static uint32_t
smbd_quota_zfs_init(const char *path, smbd_quota_zfs_handle_t *zfs_hdl)
{
	char dataset[MAXPATHLEN];

	if (smb_getdataset(path, dataset, MAXPATHLEN) != 0)
		return (NT_STATUS_INVALID_PARAMETER);

	if ((zfs_hdl->z_lib = libzfs_init()) == NULL)
		return (NT_STATUS_INTERNAL_ERROR);

	zfs_hdl->z_fs = zfs_open(zfs_hdl->z_lib, dataset, ZFS_TYPE_DATASET);
	if (zfs_hdl->z_fs == NULL) {
		libzfs_fini(zfs_hdl->z_lib);
		return (NT_STATUS_ACCESS_DENIED);
	}

	return (NT_STATUS_SUCCESS);
}

/*
 * smbd_quota_zfs_fini
 *
 * Close zfs library and dataset handles
 */
static void
smbd_quota_zfs_fini(smbd_quota_zfs_handle_t *zfs_hdl)
{
	zfs_close(zfs_hdl->z_fs);
	libzfs_fini(zfs_hdl->z_lib);
}

/*
 * smbd_quota_add_ctrldir
 *
 * In order to display the quota properties tab, windows clients
 * check for the existence of the quota control file, created
 * here as follows:
 * - Create SMB_QUOTA_CNTRL_DIR directory (with A_HIDDEN & A_SYSTEM
 *   attributes).
 * - Create the SMB_QUOTA_CNTRL_FILE file (with extended attribute
 *   SMB_QUOTA_CNTRL_INDEX_XATTR) in the SMB_QUOTA_CNTRL_DIR directory.
 * - Set the acl of SMB_QUOTA_CNTRL_FILE file to SMB_QUOTA_CNTRL_PERM.
 */
static void
smbd_quota_add_ctrldir(const char *path)
{
	int newfd, dirfd, afd;
	nvlist_t *request;
	char dir[MAXPATHLEN], file[MAXPATHLEN];
	acl_t *aclp;
	struct stat statbuf;

	assert(path != NULL);

	(void) snprintf(dir, MAXPATHLEN, ".%s/%s", path, SMB_QUOTA_CNTRL_DIR);
	(void) snprintf(file, MAXPATHLEN, "%s/%s", dir, SMB_QUOTA_CNTRL_FILE);
	if ((mkdir(dir, 0750) < 0) && (errno != EEXIST))
		return;

	if ((dirfd = open(dir, O_RDONLY)) < 0) {
		(void) remove(dir);
		return;
	}

	if (nvlist_alloc(&request, NV_UNIQUE_NAME, 0) == 0) {
		if ((nvlist_add_boolean_value(request, A_HIDDEN, 1) != 0) ||
		    (nvlist_add_boolean_value(request, A_SYSTEM, 1) != 0) ||
		    (fsetattr(dirfd, XATTR_VIEW_READWRITE, request))) {
			nvlist_free(request);
			(void) close(dirfd);
			(void) remove(dir);
			return;
		}
	}
	nvlist_free(request);
	(void) close(dirfd);

	if (stat(file, &statbuf) != 0) {
		if ((newfd = creat(file, 0640)) < 0) {
			(void) remove(dir);
			return;
		}
		(void) close(newfd);
	}

	afd = attropen(file, SMB_QUOTA_CNTRL_INDEX_XATTR, O_RDWR | O_CREAT,
	    0640);
	if (afd == -1) {
		(void) unlink(file);
		(void) remove(dir);
		return;
	}
	(void) close(afd);

	if (acl_fromtext(SMB_QUOTA_CNTRL_PERM, &aclp) != 0) {
		(void) unlink(file);
		(void) remove(dir);
		return;
	}

	if (acl_set(file, aclp) == -1) {
		(void) unlink(file);
		(void) remove(dir);
		acl_free(aclp);
		return;
	}
	acl_free(aclp);
}

/*
 * smbd_quota_remove_ctrldir
 *
 * Remove SMB_QUOTA_CNTRL_FILE and SMB_QUOTA_CNTRL_DIR.
 */
static void
smbd_quota_remove_ctrldir(const char *path)
{
	char dir[MAXPATHLEN], file[MAXPATHLEN];
	assert(path);

	(void) snprintf(dir, MAXPATHLEN, ".%s/%s", path, SMB_QUOTA_CNTRL_DIR);
	(void) snprintf(file, MAXPATHLEN, "%s/%s", dir, SMB_QUOTA_CNTRL_FILE);
	(void) unlink(file);
	(void) remove(dir);
}
