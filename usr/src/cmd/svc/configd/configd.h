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
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_CONFIGD_H
#define	_CONFIGD_H

#include <bsm/adt.h>
#include <door.h>
#include <pthread.h>
#include <synch.h>
#include <string.h>
#include <sys/types.h>

#include <libscf.h>
#include <repcache_protocol.h>
#include <libuutil.h>

#include <configd_exit.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Lock order:
 *
 *	client lock
 *		iter locks, in ID order
 *		entity locks, in ID order
 *
 *		(any iter/entity locks)
 *			backend locks (NORMAL, then NONPERSIST)
 *				rc_node lock
 *					children's rc_node lock
 *				cache bucket lock
 *					rc_node lock[*]
 *
 *	* only one node may be grabbed while holding a bucket lock
 *
 *	leaf locks:  (no other locks may be aquired while holding one)
 *		rc_pg_notify_lock
 *		rc_annotate_lock
 */

/*
 * Returns the minimum size for a structure of type 't' such
 * that it is safe to access field 'f'.
 */
#define	offsetofend(t, f)	(offsetof(t, f) + sizeof (((t *)0)->f))

/*
 * We want MUTEX_HELD, but we also want pthreads.  So we're stuck with this
 * for the native build, at least until the build machines can catch up
 * with the latest version of MUTEX_HELD() in <synch.h>.
 */
#if defined(NATIVE_BUILD)
#undef	MUTEX_HELD
#define	MUTEX_HELD(m)		_mutex_held((mutex_t *)(m))
#endif

/*
 * Maximum levels of composition.
 */
#define	COMPOSITION_DEPTH	2

#define	CONFIGD_CORE	"core.%f.%t.%p"

#ifndef NDEBUG
#define	bad_error(f, e)							\
	uu_warn("%s:%d: %s() returned bad error %d.  Aborting.\n",	\
	    __FILE__, __LINE__, f, e);					\
	abort()
#else
#define	bad_error(f, e)		abort()
#endif

/*
 * Decoration flags used to mark decorations of entities to specify information
 * about the entities they decorate.
 *
 * DECORATION_UNMASK : Default setting
 * DECORATION_MASK : Masked entity that still has file backing but should not be
 * 	presented to the user under normal conditions
 * DECORATION_NOFILE : There is no file backing for this entity even though the
 * 	entity may still be present in the repository at a layer that requires
 * 	file backing.  (e.g a property that is kept due to snapshot reference).
 * DECORATION_SNAP_ONLY : The row is only being kept around because of the
 * 	snapshot reference.
 * DECORATION_BUNDLE_ONLY : The row is only being kept around because of bundle
 * 	reference.
 * DECORATION_DELCUSTED : A property has been delcusted but the admin decoration
 *      that was delcusted is held by a snapshot.
 * DECORATION_CONFLICT : The entity that is decorated by this row is in
 * 	conflict.
 */
#define	DECORATION_UNMASK	0x0
#define	DECORATION_MASK		0x1
#define	DECORATION_NOFILE	0x2
#define	DECORATION_TRUE_VALUE	0x4
#define	DECORATION_SNAP_ONLY	0x10
#define	DECORATION_BUNDLE_ONLY	0x20
#define	DECORATION_DELCUSTED	0x40
#define	DECORATION_CONFLICT	0x100

/*
 * DECORATION_IN_USE should never be used to set decoration flags, only to test
 * them when you don't care *why*, but only *whether* an entity is safe to be
 * deleted.
 */
#define	DECORATION_IN_USE	(DECORATION_TRUE_VALUE | DECORATION_SNAP_ONLY)

typedef enum decoration_type {
	DECORATION_TYPE_UNSET,
	DECORATION_TYPE_SVC,
	DECORATION_TYPE_INST,
	DECORATION_TYPE_PG,
	DECORATION_TYPE_PROP
} decoration_type_t;

typedef enum backend_type {
	BACKEND_TYPE_NORMAL		= 0,
	BACKEND_TYPE_NONPERSIST,
	BACKEND_TYPE_TOTAL			/* backend use only */
} backend_type_t;

/*
 * The DELETE_NA entry requires more explanation than will fit on one
 * line.  This is used in delete_cb_func type functions that do none of
 * delete, mask or unmask.  Instead they will cause new entries to be added
 * to the delete stack where the actual deleting or muting will be done.
 */
typedef enum delete_result {
	DELETE_UNDEFINED = 0,
	DELETE_DELETED,		/* Object was removed from repository */
	DELETE_MASKED,		/* Object marked as masked in repository */
	DELETE_NA,		/* Not applicable */
	DELETE_UNMASKED		/* Previously masked object was unmasked */
} delete_result_t;

/*
 * pre-declare rc_* types
 */
typedef struct rc_node rc_node_t;
typedef struct rc_snapshot rc_snapshot_t;
typedef struct rc_snaplevel rc_snaplevel_t;

/*
 * notification layer -- protected by rc_pg_notify_lock
 */
typedef struct rc_notify_info rc_notify_info_t;
typedef struct rc_notify_delete rc_notify_delete_t;

#define	RC_NOTIFY_MAX_NAMES	4	/* enough for now */

typedef struct rc_notify {
	uu_list_node_t	rcn_list_node;
	rc_node_t	*rcn_node;
	rc_notify_info_t *rcn_info;
	rc_notify_delete_t *rcn_delete;
} rc_notify_t;

struct rc_notify_delete {
	rc_notify_t rnd_notify;
	char rnd_fmri[REP_PROTOCOL_FMRI_LEN];
};

struct rc_notify_info {
	uu_list_node_t	rni_list_node;
	rc_notify_t	rni_notify;
	const char	*rni_namelist[RC_NOTIFY_MAX_NAMES];
	const char	*rni_typelist[RC_NOTIFY_MAX_NAMES];

	int		rni_flags;
	int		rni_waiters;
	pthread_cond_t	rni_cv;
};
#define	RC_NOTIFY_ACTIVE	0x00000001
#define	RC_NOTIFY_DRAIN		0x00000002
#define	RC_NOTIFY_EMPTYING	0x00000004

typedef struct rc_node_pg_notify {
	uu_list_node_t	rnpn_node;
	int		rnpn_fd;
	rc_node_t	*rnpn_pg;
} rc_node_pg_notify_t;

/*
 * Decoration Table representation
 */
typedef struct rc_bundle {
	uint32_t	bundle_id;
	const char	*bundle_name;
	time_t		bundle_timestamp;
} rc_bundle_t;

/*
 * Decoration Layers
 */

typedef struct decoration_layer_info {
	char				*layer_name;
	rep_protocol_decoration_layer_t	layer_id;
} decoration_layer_info_t;

typedef struct rc_decoration {
	uu_list_node_t	decoration_list_node;
	uint32_t	decoration_id;
	uint32_t	decoration_value_id;
	uint32_t	decoration_gen_id;
	uint32_t	decoration_layer;
	uint32_t	decoration_bundle_id;
	uint32_t	decoration_type;
	rc_bundle_t	*decoration_bundlep;
} rc_decoration_t;

/*
 * cache layer
 */

/*
 * Property value manipulation
 */
typedef struct rc_value rc_value_t;
typedef struct rc_value_set rc_value_set_t;

typedef enum rc_value_access {
	RC_VALUE_ACCESS_READ,		/* Read value list */
	RC_VALUE_ACCESS_FILL		/* Fill value list */
} rc_value_access_t;

/*
 * The 'key' for the main object hash.  main_id is the main object
 * identifier.  The rl_ids array contains:
 *
 *	TYPE		RL_IDS
 *	scope		unused
 *	service		unused
 *	instance	{service_id}
 *	snapshot	{service_id, instance_id}
 *	snaplevel	{service_id, instance_id, name_id, snapshot_id}
 *	propertygroup	{service_id, (instance_id or 0), (name_id or 0),
 *			    (snapshot_id or 0), (l_id or 0)}
 *	property	{service_id, (instance_id or 0), (name_id or 0),
 *			    (snapshot_id or 0), (l_id or 0), pg_id, gen_id}
 *	decoration	{service_id, (instance_id or 0), (name_id or 0),
 *			    (snapshot_id or 0), (l_id or 0), pg_id, prop_id}
 *
 * The rl_ids array in rc_node_lookup is used to hold keys from the
 * repository:
 *
 *	ID_SERVICE	- svc_id from service_tbl
 *	ID_INSTANCE	- instance_id from instance_tbl
 *	ID_NAME		- lnk_id from snapshot_lnk_tbl
 *	ID_SNAPSHOT	- snap_id from snaplevel_tbl
 *	ID_LEVEL	- snap_level_id from snaplevel_tbl
 *	ID_PG		- pg_id from pg_tbl
 *	ID_GEN		- pg_gen_id from pg_tbl (this is not a primary key)
 *	ID_PROPERTY	- lnk_prop_id from prop_lnk_tbl
 */
#define	ID_SERVICE	0
#define	ID_INSTANCE	1
#define	ID_NAME		2
#define	ID_SNAPSHOT	3
#define	ID_LEVEL	4
#define	ID_PG		5
#define	ID_GEN		6
#define	ID_PROPERTY	7
#define	MAX_IDS		8
typedef struct rc_node_lookup {
	uint16_t	rl_type;		/* REP_PROTOCOL_ENTITY_* */
	uint16_t	rl_backend;		/* BACKEND_TYPE_* */
	uint32_t	rl_main_id;		/* primary identifier */
	uint32_t	rl_ids[MAX_IDS];	/* context */
} rc_node_lookup_t;

struct rc_node {
	/*
	 * read-only data
	 */
	rc_node_lookup_t rn_id;			/* must be first */
	uint32_t	rn_hash;
	const char	*rn_name;

	/*
	 * type-specific state
	 * (if space becomes an issue, these can become a union)
	 */

	/*
	 * Used by instances, snapshots, and "composed property groups" only.
	 * These are the entities whose properties should appear composed when
	 * this entity is traversed by a composed iterator.  0 is the top-most
	 * entity, down to COMPOSITION_DEPTH - 1.
	 */
	rc_node_t	*rn_cchain[COMPOSITION_DEPTH];

	/*
	 * used by property groups, properties and decorations.
	 */
	uint32_t	rn_gen_id;

	/*
	 * used by property groups only
	 */
	const char	*rn_type;
	uint32_t	rn_pgflags;
	uu_list_t	*rn_pg_notify_list;	/* prot by rc_pg_notify_lock */
	rc_notify_t	rn_notify;		/* prot by rc_pg_notify_lock */

	/*
	 * Used by services, instances, property groups and properties.
	 */
	uint32_t	rn_decoration_id;
	uint32_t	rn_decoration_flags;

	/*
	 * used by properties and decorations only
	 */
	rep_protocol_value_type_t rn_prop_type;
	rc_value_set_t	*rn_value_set;

	/*
	 * used by decorations only
	 */
	rep_protocol_decoration_layer_t rn_layer;
	decoration_type_t rn_decoration_type;
	uint32_t	rn_bundle_id;
	const char	*rn_bundle_name;
	time_t		rn_bundle_timestamp;

	/*
	 * used by snapshots only
	 */
	uint32_t	rn_snapshot_id;
	rc_snapshot_t	*rn_snapshot;		/* protected by rn_lock */

	/*
	 * used by snaplevels only
	 */
	rc_snaplevel_t	*rn_snaplevel;

	/*
	 * mutable state
	 */
	pthread_mutex_t	rn_lock;
	pthread_cond_t	rn_cv;
	uint32_t	rn_flags;
	uint32_t	rn_refs;		/* client reference count */
	uint32_t	rn_erefs;		/* ephemeral ref count */
	uint32_t	rn_other_refs;		/* atomic refcount */
	uint32_t	rn_other_refs_held;	/* for 1->0 transitions */

	uu_list_t	*rn_children;
	uu_list_node_t	rn_sibling_node;

	rc_node_t	*rn_parent;		/* set if on child list */
	rc_node_t	*rn_former;		/* next former node */
	rc_node_t	*rn_parent_ref;		/* reference count target */
	const char	*rn_fmri;

	/*
	 * external state (protected by hash chain lock)
	 */
	rc_node_t	*rn_hash_next;

	uu_list_node_t	rn_all_node;		/* Link for all nodes list */
};

/*
 * flag ordering:
 *	RC_DYING
 *		RC_NODE_CHILDREN_CHANGING
 *		RC_NODE_CREATING_CHILD
 *		RC_NODE_USING_PARENT
 *			RC_NODE_IN_TX
 *
 * RC_NODE_USING_PARENT is special, because it lets you proceed up the tree,
 * in the reverse of the usual locking order.  Because of this, there are
 * limitations on what you can do while holding it.  While holding
 * RC_NODE_USING_PARENT, you may:
 *	bump or release your parent's reference count
 *	access fields in your parent
 *	hold RC_NODE_USING_PARENT in the parent, proceeding recursively.
 *
 * If you are only holding *one* node's RC_NODE_USING_PARENT, and:
 *	you are *not* proceeding recursively, you can hold your
 *	    immediate parent's RC_NODE_CHILDREN_CHANGING flag.
 *	you hold your parent's RC_NODE_CHILDREN_CHANGING flag, you can add
 *	    RC_NODE_IN_TX to your flags.
 *	you want to grab a flag in your parent, you must lock your parent,
 *	    lock yourself, drop RC_NODE_USING_PARENT, unlock yourself,
 *	    then proceed to manipulate the parent.
 */
#define	RC_NODE_CHILDREN_CHANGING	0x00000001 /* child list in flux */
#define	RC_NODE_HAS_CHILDREN		0x00000002 /* child list is accurate */

#define	RC_NODE_IN_PARENT		0x00000004 /* I'm in my parent's list */
#define	RC_NODE_USING_PARENT		0x00000008 /* parent ptr in use */
#define	RC_NODE_CREATING_CHILD		0x00000010 /* a create is in progress */
#define	RC_NODE_IN_TX			0x00000020 /* a tx is in progess */

#define	RC_NODE_OLD			0x00000400 /* out-of-date object */
#define	RC_NODE_ON_FORMER		0x00000800 /* on an rn_former list */

#define	RC_NODE_PARENT_REF		0x00001000 /* parent_ref in use */
#define	RC_NODE_UNREFED			0x00002000 /* unref processing active */
#define	RC_NODE_DYING			0x00004000 /* node is being deleted */
#define	RC_NODE_DEAD			0x00008000 /* node has been deleted */
#define	RC_NODE_REFRESHING		0X00010000 /* rc_node_refresh_branch */

/*
 * RC_NODE_DEAD means that the node no longer represents data in the
 * backend, and we should return _DELETED errors to clients who try to use
 * it.  Very much like a zombie process.
 *
 * RC_NODE_OLD also means that the node no longer represents data in the
 * backend, but it's ok for clients to access it because we've loaded all of
 * the children.  (This only happens for transactional objects such as
 * property groups and snapshots, where we guarantee a stable view once
 * a reference is obtained.)  When all client references are destroyed,
 * however, the node should be destroyed.
 *
 * Though RC_NODE_DEAD is set by the rc_node_delete() code, it is also set
 * by rc_node_no_client_refs() for RC_NODE_OLD nodes not long before
 * they're destroyed.
 */

#define	RC_NODE_DYING_FLAGS						\
	(RC_NODE_CHILDREN_CHANGING | RC_NODE_IN_TX | RC_NODE_DYING |	\
	    RC_NODE_CREATING_CHILD)

#define	RC_NODE_WAITING_FLAGS						\
	(RC_NODE_DYING_FLAGS | RC_NODE_USING_PARENT)


#define	NODE_LOCK(n)	(void) pthread_mutex_lock(&(n)->rn_lock)
#define	NODE_UNLOCK(n)	(void) pthread_mutex_unlock(&(n)->rn_lock)


typedef enum rc_auth_state {
	RC_AUTH_UNKNOWN = 0,		/* No checks done yet. */
	RC_AUTH_FAILED,			/* Authorization checked & failed. */
	RC_AUTH_PASSED			/* Authorization succeeded. */
} rc_auth_state_t;

/*
 * Some authorization checks are performed in rc_node_setup_tx() in
 * response to the REP_PROTOCOL_PROPERTYGRP_TX_START message.  Other checks
 * must wait until the actual transaction operations are received in the
 * REP_PROTOCOL_PROPERTYGRP_TX_COMMIT message.  This second set of checks
 * is performed in rc_tx_commit().  rnp_auth_string and rnp_authorized in
 * the following structure are used to hold the results of the
 * authorization checking done in rc_node_setup_tx() for later use by
 * rc_tx_commit().
 *
 * In client.c transactions are represented by rc_node_ptr structures which
 * point to a property group rc_node_t.  Thus, this is an appropriate place
 * to hold authorization state.
 */
typedef struct rc_node_ptr {
	rc_node_t	*rnp_node;
	const char	*rnp_auth_string;	/* authorization string */
	rc_auth_state_t	rnp_authorized;		/* transaction pre-auth rslt. */
	char		rnp_deleted;		/* object was deleted */
} rc_node_ptr_t;

#define	NODE_PTR_NOT_HELD(npp) \
	    ((npp)->rnp_node == NULL || !MUTEX_HELD(&(npp)->rnp_node->rn_lock))

typedef int rc_iter_filter_func(rc_node_t *, void *);

typedef struct rc_node_iter {
	rc_node_t	*rni_parent;
	int		rni_clevel;	/* index into rni_parent->rn_cchain[] */
	rc_node_t	*rni_iter_node;
	uu_list_walk_t	*rni_iter;
	uint32_t	rni_type;

	/*
	 * for normal walks
	 */
	rc_iter_filter_func *rni_filter;
	void		*rni_filter_arg;

	/*
	 * for value walks
	 */
	rc_value_set_t	*rni_values;
	rc_value_t	*rni_prev_value;	/* previous value */
} rc_node_iter_t;

typedef struct rc_node_tx {
	rc_node_ptr_t	rnt_ptr;
	int		rnt_authorized;		/* No need to check anymore. */
} rc_node_tx_t;


typedef struct cache_bucket {
	pthread_mutex_t	cb_lock;
	rc_node_t	*cb_head;

	char		cb_pad[64 - sizeof (pthread_mutex_t) -
			    2 * sizeof (rc_node_t *)];
} cache_bucket_t;

/*
 * tx_commit_data_tx is an opaque structure which is defined in object.c.
 * It contains the data of the transaction that is to be committed.
 * Accessor functions in object.c allow other modules to retrieve
 * information.
 */
typedef struct tx_commit_data tx_commit_data_t;

/*
 * Snapshots
 */
struct rc_snapshot {
	uint32_t	rs_snap_id;

	pthread_mutex_t	rs_lock;
	pthread_cond_t	rs_cv;

	uint32_t	rs_flags;
	uint32_t	rs_refcnt;	/* references from rc_nodes */
	uint32_t	rs_childref;	/* references to children */

	rc_snaplevel_t	*rs_levels;	/* list of levels */
	rc_snapshot_t	*rs_hash_next;
};
#define	RC_SNAPSHOT_FILLING	0x00000001	/* rs_levels changing */
#define	RC_SNAPSHOT_READY	0x00000002
#define	RC_SNAPSHOT_DEAD	0x00000004	/* no resources */

typedef struct rc_snaplevel_pgs {
	uint32_t	rsp_pg_id;
	uint32_t	rsp_gen_id;
} rc_snaplevel_pgs_t;

struct rc_snaplevel {
	rc_snapshot_t	*rsl_parent;
	uint32_t	rsl_level_num;
	uint32_t	rsl_level_id;

	uint32_t	rsl_service_id;
	uint32_t	rsl_instance_id;

	const char	*rsl_scope;
	const char	*rsl_service;
	const char	*rsl_instance;

	rc_snaplevel_t	*rsl_next;
};

/*
 * Client layer -- the IDs fields must be first, in order for the search
 * routines to work correctly.
 */
enum repcache_txstate {
	REPCACHE_TX_INIT,
	REPCACHE_TX_SETUP,
	REPCACHE_TX_COMMITTED
};

typedef struct repcache_entity {
	uint32_t	re_id;
	uu_avl_node_t	re_link;
	uint32_t	re_changeid;

	pthread_mutex_t	re_lock;
	uint32_t	re_type;
	rc_node_ptr_t	re_node;
	enum repcache_txstate re_txstate;	/* property groups only */
	uint32_t	re_show_masked;		/* client type identifier */
} repcache_entity_t;

typedef struct repcache_iter {
	uint32_t	ri_id;
	uu_avl_node_t	ri_link;

	uint32_t	ri_type;	/* result type */

	pthread_mutex_t	ri_lock;
	uint32_t	ri_sequence;
	rc_node_iter_t	*ri_iter;
	uint32_t	ri_show_masked;		/* client type identifier */
} repcache_iter_t;

typedef struct repcache_client {
	/*
	 * constants
	 */
	uint32_t	rc_id;		/* must be first */
	int		rc_all_auths;	/* bypass auth checks */
	uint32_t	rc_debug;	/* debug flags */
	pid_t		rc_pid;		/* pid of opening process */
	door_id_t	rc_doorid;	/* a globally unique identifier */
	int		rc_doorfd;	/* our door's FD */
	uint32_t	rc_show_masked;	/* naive/advanced client identifier */

	/*
	 * Constants used for security auditing
	 *
	 * rc_adt_session points to the audit session data that is used for
	 * the life of the client.  rc_adt_sessionid is the session ID that
	 * is initially assigned when the audit session is started.  See
	 * start_audit_session() in client.c.  This session id is used for
	 * audit events except when we are processing a set of annotated
	 * events.  Annotated events use a separate session id so that they
	 * can be grouped.  See set_annotation() in client.c.
	 */
	adt_session_data_t *rc_adt_session;	/* Session data. */
	au_asid_t	rc_adt_sessionid;	/* Main session ID for */
						/* auditing */

	/*
	 * client list linkage, protected by hash chain lock
	 */
	uu_list_node_t	rc_link;

	/*
	 * notification information, protected by rc_node layer
	 */
	rc_node_pg_notify_t	rc_pg_notify;
	rc_notify_info_t	rc_notify_info;

	/*
	 * client_wait output, only usable by rc_notify_thr
	 */
	rc_node_ptr_t	rc_notify_ptr;

	/*
	 * register sets, protected by rc_lock
	 */
	uu_avl_t	*rc_entities;
	uu_avl_t	*rc_iters;

	/*
	 * Variables, protected by rc_lock
	 */
	int		rc_refcnt;	/* in-progress door calls */
	int		rc_flags;	/* see RC_CLIENT_* symbols below */
	uint32_t	rc_changeid;	/* used to make backups idempotent */
	pthread_t	rc_insert_thr;	/* single thread trying to insert */
	pthread_t	rc_notify_thr;	/* single thread waiting for notify */
	pthread_cond_t	rc_cv;
	pthread_mutex_t	rc_lock;

	/*
	 * Per-client audit and decoration information.  These fields must be
	 * protected by rc_annotate_lock separately from rc_lock because they
	 * may need to be accessed from rc_node.c with an entity or iterator
	 * lock held, and those must be taken after rc_lock.
	 */
	int		rc_annotate;	/* generate annotation event if set */
	const char	*rc_operation;	/* operation for audit annotation */

	const char	*rc_file;	/* file name for property value */
					/* decoration and audit annotation */

	rep_protocol_decoration_layer_t	rc_layer_id;	/* layer for property */
							/* value decoration */

	pthread_mutex_t	rc_annotate_lock;
} repcache_client_t;

/* Bit definitions for rc_flags. */
#define	RC_CLIENT_DEAD			0x00000001

typedef struct client_bucket {
	pthread_mutex_t	cb_lock;
	uu_list_t	*cb_list;
	char ch_pad[64 - sizeof (pthread_mutex_t) - sizeof (uu_list_t *)];
} client_bucket_t;

enum rc_ptr_type {
	RC_PTR_TYPE_ENTITY = 1,
	RC_PTR_TYPE_ITER
};

typedef struct request_log_ptr {
	enum rc_ptr_type	rlp_type;
	uint32_t		rlp_id;
	void			*rlp_ptr; /* repcache_{entity,iter}_t */
	void			*rlp_data;	/* rc_node, for ENTITY only */
} request_log_ptr_t;

#define	MAX_PTRS	3

/*
 * rl_start through rl_client cannot move without changing start_log()
 */
typedef struct request_log_entry {
	hrtime_t		rl_start;
	hrtime_t		rl_end;
	pthread_t		rl_tid;
	uint32_t		rl_clientid;
	repcache_client_t	*rl_client;
	enum rep_protocol_requestid rl_request;
	rep_protocol_responseid_t rl_response;
	int			rl_num_ptrs;
	request_log_ptr_t	rl_ptrs[MAX_PTRS];
} request_log_entry_t;

/*
 * thread information
 */
typedef enum thread_state {
	TI_CREATED,
	TI_DOOR_RETURN,
	TI_SIGNAL_WAIT,
	TI_MAIN_DOOR_CALL,
	TI_CLIENT_CALL
} thread_state_t;

typedef struct thread_info {
	pthread_t	ti_thread;
	uu_list_node_t	ti_node;		/* for list of all thread */

	/*
	 * per-thread globals
	 */
	ucred_t		*ti_ucred;		/* for credential lookups */
	int		ti_ucred_read;		/* ucred holds current creds */

	/*
	 * per-thread state information, for debuggers
	 */
	hrtime_t	ti_lastchange;

	thread_state_t	ti_state;
	thread_state_t	ti_prev_state;

	repcache_client_t *ti_active_client;
	request_log_entry_t	ti_log;

	struct rep_protocol_request *ti_client_request;
	repository_door_request_t *ti_main_door_request;

} thread_info_t;

/*
 * Backend layer
 */
typedef struct backend_query backend_query_t;
typedef struct backend_tx backend_tx_t;

typedef struct conflict {
	backend_query_t		*q;		/* query to add conflict */
	tx_commit_data_t	*data;
	struct tx_cmd		*e;
	int			in_conflict;	/* entity already in conflict */
} conflict_t;

/*
 * Bundle removal
 */
typedef struct pg_update_bundle_info {
	backend_tx_t	*pub_tx;
	uint32_t	pub_pg_id;
	uint32_t	pub_gen_id;
	uint32_t	pub_pg_inst;
	uint32_t	pub_bundleid;
	uint32_t	pub_pg_svc;
	uint32_t	pub_setmask;
	uint32_t	pub_delcust;
	uint32_t	*pub_dprop_ids;
	uint32_t	*pub_rback_ids;
	int		pub_dprop_idx;
	int		pub_rback_idx;
	int		pub_geninsnapshot;
	struct pg_update_bundle_info *pub_next;
} pg_update_bundle_info_t;

int tx_reset_mask(void *, int, char **, char **);

/*
 * transaction and query to reset admin decorations on delete or delcust
 */
typedef struct tx_reset_mask_data {
	backend_query_t *rmd_q;
	backend_tx_t    *rmd_tx;
} tx_reset_mask_data_t;

/*
 * configd.c
 */
int create_connection(ucred_t *cred, repository_door_request_t *rp,
    size_t rp_size, int *out_fd);

thread_info_t *thread_self(void);
void thread_newstate(thread_info_t *, thread_state_t);
ucred_t *get_ucred(void);
int ucred_is_privileged(ucred_t *);

adt_session_data_t *get_audit_session(void);

void configd_critical(const char *, ...);
void configd_vcritical(const char *, va_list);

extern int is_main_repository;
extern int max_repository_backups;

/*
 * maindoor.c
 */
int setup_main_door(const char *);

/*
 * client.c
 */
int client_annotation_needed(char *, size_t, char *, size_t);
void client_annotation_finished(void);
int create_client(pid_t, uint32_t, uint32_t, int, int *);
int client_init(void);
int client_is_privileged(void);
repcache_client_t *get_active_client(void);
void log_enter(request_log_entry_t *);

/*
 * rc_node.c, backend/cache interfaces (rc_node_t)
 */
int rc_node_init();
int rc_check_type_name(uint32_t, const char *);

void rc_node_ptr_free_mem(rc_node_ptr_t *);
void rc_node_rele(rc_node_t *);
void rc_node_set_gen_id(rc_node_t *, uint32_t);
void rc_node_set_decoration_flags(rc_node_t *, uint32_t);
rc_node_t *rc_node_setup(rc_node_t *, rc_node_lookup_t *,
    const char *, rc_node_t *, uint32_t, uint32_t);
rc_node_t *rc_node_setup_pg(rc_node_t *, rc_node_lookup_t *, const char *,
    const char *, uint32_t, uint32_t, uint32_t, uint32_t, rc_node_t *);
rc_node_t *rc_node_setup_snapshot(rc_node_t *, rc_node_lookup_t *, const char *,
    uint32_t, rc_node_t *);
rc_node_t *rc_node_setup_snaplevel(rc_node_t *, rc_node_lookup_t *,
    rc_snaplevel_t *, rc_node_t *);
rc_node_t *rc_node_setup_decoration(rc_node_t *, rc_node_lookup_t *,
    uint32_t, uint32_t, rep_protocol_value_type_t, rc_value_set_t *, uint32_t,
    rep_protocol_decoration_layer_t, rc_bundle_t *, decoration_type_t);
int rc_node_create_property(rc_node_t *, rc_node_lookup_t *,
    const char *, rep_protocol_value_type_t, rc_value_set_t *, uint32_t,
    uint32_t, uint32_t);

rc_node_t *rc_node_alloc(void);
void rc_node_destroy(rc_node_t *);
rep_protocol_responseid_t rc_vs_add_value(rc_value_set_t *, const char *);
void rc_vs_filled(rc_value_set_t *);
void rc_vs_fill_failed(rc_value_set_t *);
rep_protocol_responseid_t rc_vs_get(uint32_t, backend_type_t, rc_value_access_t,
    rc_value_set_t **);
void rc_vs_hold(rc_value_set_t *);
void rc_vs_release(rc_value_set_t *);

extern const decoration_layer_info_t layer_info[];
extern size_t layer_info_count;

/*
 * rc_node.c, client interface (rc_node_ptr_t, rc_node_iter_t)
 */
void rc_node_ptr_init(rc_node_ptr_t *);
int rc_local_scope(uint32_t, rc_node_ptr_t *);

void rc_node_clear(rc_node_ptr_t *, int);
void rc_node_ptr_assign(rc_node_ptr_t *, const rc_node_ptr_t *);
int rc_node_name(rc_node_ptr_t *, char *, size_t, uint32_t, size_t *);
int rc_node_fmri(rc_node_ptr_t *, char *, size_t, size_t *);
int rc_node_parent_type(rc_node_ptr_t *, uint32_t *);
int rc_node_get_bundle_name(rc_node_ptr_t *, char *, size_t *);
int rc_node_get_child(rc_node_ptr_t *, const char *, uint32_t, rc_node_ptr_t *);
int rc_node_get_parent(rc_node_ptr_t *, uint32_t, rc_node_ptr_t *);
int rc_node_get_property_type(rc_node_ptr_t *, rep_protocol_value_type_t *);
int rc_node_get_property_value(rc_node_ptr_t *,
    struct rep_protocol_value_response *, size_t *);

int rc_node_get_decoration_layer(rc_node_ptr_t *,
    rep_protocol_decoration_layer_t *);

int rc_node_create_child(rc_node_ptr_t *, uint32_t, const char *,
    rc_node_ptr_t *);
int rc_node_create_child_pg(rc_node_ptr_t *, uint32_t, const char *,
    const char *, uint32_t, rc_node_ptr_t *);
int rc_node_update(rc_node_ptr_t *);
int rc_node_delete_undelete(rc_node_ptr_t *, enum rep_protocol_requestid);
int rc_node_next_snaplevel(rc_node_ptr_t *, rc_node_ptr_t *);

int rc_bundle_remove(rc_node_ptr_t *, const char *, const void *, size_t,
    uint32_t);
int rc_tx_prop_bundle_remove(pg_update_bundle_info_t *, rc_node_t *);
int rc_node_find_named_child(rc_node_t *, const char *, uint32_t,
    rc_node_t **, int);

int rc_node_setup_iter(rc_node_ptr_t *, rc_node_iter_t **, uint32_t,
    size_t, const char *);

int rc_iter_next(rc_node_iter_t *, rc_node_ptr_t *, uint32_t);
int rc_iter_next_value(rc_node_iter_t *, struct rep_protocol_value_response *,
    size_t *, int);
void rc_iter_destroy(rc_node_iter_t **);

int rc_node_setup_tx(rc_node_ptr_t *, rc_node_ptr_t *);
int rc_tx_commit(rc_node_ptr_t *, const char *, uint32_t, const void *, size_t);

void rc_pg_notify_init(rc_node_pg_notify_t *);
int rc_pg_notify_setup(rc_node_pg_notify_t *, rc_node_ptr_t *, int);
void rc_pg_notify_fini(rc_node_pg_notify_t *);

void rc_notify_info_init(rc_notify_info_t *);
int rc_notify_info_add_name(rc_notify_info_t *, const char *);
int rc_notify_info_add_type(rc_notify_info_t *, const char *);
int rc_notify_info_wait(rc_notify_info_t *, rc_node_ptr_t *, char *, size_t);
void rc_notify_info_fini(rc_notify_info_t *);

int rc_snapshot_take_new(rc_node_ptr_t *, const char *,
    const char *, const char *, rc_node_ptr_t *);
int rc_snapshot_take_attach(rc_node_ptr_t *, rc_node_ptr_t *);
int rc_snapshot_attach(rc_node_ptr_t *, rc_node_ptr_t *);

int rc_node_check_decoration_flag(rc_node_ptr_t *, uint32_t, uint32_t *);
int rc_is_naive_client();

/*
 * file_object.c
 */
int object_fill_children(rc_node_t *);
int object_check_node(rc_node_t *);
int object_create(rc_node_t *, uint32_t, const char *, rc_node_t **);
int object_create_pg(rc_node_t *, uint32_t, const char *, const char *,
    uint32_t, rc_node_t **);

int object_delete(rc_node_t *, int, delete_result_t *);
int object_fill_snapshot(rc_snapshot_t *);

int object_snapshot_take_new(rc_node_t *, const char *, const char *,
    const char *, rc_node_t **);
int object_snapshot_attach(rc_node_lookup_t *, uint32_t *, int);

void string_to_id(const char *, uint32_t *, const char *);

int object_bundle_remove(rc_node_t *, const char *);
int object_prop_bundle_remove(rc_node_t *, const char *, const void *, size_t,
    int);
int object_prop_check_conflict(backend_tx_t *, uint32_t, uint32_t,
    backend_query_t *, uint32_t, const char *);

/*
 * object.c
 */
int object_tx_commit(rc_node_lookup_t *, tx_commit_data_t *, uint32_t *,
    char *, int, int *);
uint32_t get_bundle_id(backend_tx_t *, const char *);
int object_pg_check_conflict(backend_tx_t *, uint32_t, uint32_t, uint32_t,
    uint32_t);

int object_pg_bundle_finish(uint32_t, pg_update_bundle_info_t *pup,
    uint32_t *, backend_tx_t *);

/* Functions to access transaction commands. */
int tx_cmd_action(tx_commit_data_t *, size_t,
    enum rep_protocol_transaction_action *);
size_t tx_cmd_count(tx_commit_data_t *);
int tx_cmd_nvalues(tx_commit_data_t *, size_t, uint32_t *);
int tx_cmd_prop(tx_commit_data_t *, size_t, const char **);
int tx_cmd_prop_type(tx_commit_data_t *, size_t, uint32_t *);
int tx_cmd_value(tx_commit_data_t *, size_t, uint32_t, const char **);
void tx_commit_data_free(tx_commit_data_t *);
int tx_commit_data_new(const char *, uint32_t, const void *, size_t,
    tx_commit_data_t **);

/*
 * snapshot.c
 */
int rc_snapshot_get(uint32_t, rc_snapshot_t **);
void rc_snapshot_rele(rc_snapshot_t *);
void rc_snaplevel_hold(rc_snaplevel_t *);
void rc_snaplevel_rele(rc_snaplevel_t *);

/*
 * backend.c
 */
int backend_init(const char *, const char *, int);
void backend_fini(void);
void check_upgrade(const char *);

rep_protocol_responseid_t backend_create_backup(const char *);
rep_protocol_responseid_t backend_switch(int);

void backend_mark_pg_conflict(backend_tx_t *, uint32_t, uint32_t);
void backend_mark_pg_parent(backend_tx_t *, uint32_t, backend_query_t *);
void backend_clear_pg_conflict(backend_tx_t *, uint32_t, uint32_t);
void backend_clear_pg_parent(backend_tx_t *, uint32_t, backend_query_t *);

void backend_carry_delcust_flag(backend_tx_t *, uint32_t, uint32_t, uint32_t *);

/*
 * call on any database inconsistency -- cleans up state as best it can,
 * and exits with a "Database Bad" error code.
 */
void backend_panic(const char *, ...) __NORETURN;
#pragma rarely_called(backend_panic)

backend_query_t *backend_query_alloc(void);
void backend_query_append(backend_query_t *, const char *);
void backend_query_add(backend_query_t *, const char *, ...);
void backend_query_reset(backend_query_t *);
void backend_query_free(backend_query_t *);

typedef int backend_run_callback_f(void *data, int columns, char **vals,
    char **names);
#define	BACKEND_CALLBACK_CONTINUE	0
#define	BACKEND_CALLBACK_ABORT		1

backend_run_callback_f backend_fail_if_seen;	/* aborts TX if called */

int backend_run(backend_type_t, backend_query_t *,
    backend_run_callback_f *, void *);

int backend_tx_begin(backend_type_t, backend_tx_t **);
int backend_tx_begin_ro(backend_type_t, backend_tx_t **);
void backend_tx_end_ro(backend_tx_t *);

enum id_space {
	BACKEND_ID_SERVICE_INSTANCE,
	BACKEND_ID_PROPERTYGRP,
	BACKEND_ID_GENERATION,
	BACKEND_ID_PROPERTY,
	BACKEND_ID_VALUE,
	BACKEND_ID_SNAPNAME,
	BACKEND_ID_SNAPSHOT,
	BACKEND_ID_SNAPLEVEL,
	BACKEND_ID_BUNDLE,
	BACKEND_ID_DECORATION,
	BACKEND_KEY_DECORATION,
	BACKEND_ID_INVALID	/* always illegal */
};

const char *id_space_to_name(enum id_space);

uint32_t backend_new_id(backend_tx_t *, enum id_space);
int backend_tx_run_update(backend_tx_t *, const char *, ...);
int backend_tx_run_update_changed(backend_tx_t *, const char *, ...);
int backend_tx_run_single_str(backend_tx_t *tx, backend_query_t *q, char **);
int backend_tx_run_single_int(backend_tx_t *tx, backend_query_t *q,
    uint32_t *buf);
int backend_tx_run(backend_tx_t *, backend_query_t *,
    backend_run_callback_f *, void *);

int backend_tx_commit(backend_tx_t *);
void backend_tx_rollback(backend_tx_t *);

#ifdef	__cplusplus
}
#endif

#endif	/* _CONFIGD_H */
