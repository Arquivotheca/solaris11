/*
 * Copyright 1991 NCR Corporation - Dayton, Ohio, USA
 *
 * Copyright (c) 1994, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_NFS_LM_IMPL_H
#define	_NFS_LM_IMPL_H

/* NCR OS2.00.00 1.2 */

/*
 * This file contains implementation details for the network lock manager.
 */

#include <nfs/lm_nlm.h>
#include <sys/mutex.h>
#include <sys/ksynch.h>
#include <sys/thread.h>
#include <sys/note.h>		/* needed for warlock */
#include <sys/flock.h>
#include <sys/avl.h>
#include <nfs/nfs.h>
#include <nfs/nfssys.h>
#include <nfs/nfs_clnt.h>
#include <sys/zone.h>
#include <sys/list.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * LM statistics.  This struct defines counters and miscellaneous stats
 * that are protected by lm_stat::lock.
 *
 * The RPC counters (tot_in, ... proc_out) are not maintained in a
 * straightforward manner.  For example, the "bad_in" count can be
 * incremented by more than 1 if there are multiple errors associated with
 * the incoming RPC.  Check the actual implementation for details.
 */
struct lm_stat {
	kmutex_t lock;		/* protects contents */
	int	cookie;		/* Cookie for Granted calls. */
	int	tot_in;		/* Total number of calls. */
	int	tot_out;
	int	bad_in;		/* Total number of calls that fails. */
	int	bad_out;
	int	proc_in[NLM_NUMRPCS];	/* Count of messages. */
	int	proc_out[NLM_NUMRPCS];
#ifdef LM_DEBUG_COTS
	struct knetconfig cots_config;	/* Force COTS client calls. */
#endif
};

/*
 * Sysid cache.  The strings in the knetconfig are dynamically allocated.
 * - The "config" and "addr" fields contain the information needed to
 *   contact the host.  "addr" is read-only.  "config" is protected by
 *   "lock".
 * - The "name" is the hostname.  It is used for finding lm_sysid records
 *   after a client or server reboots.  It is read-only.  In some cases the
 *   name is provided by the remote node and may be incorrect.
 * - The "sysid" field is a value used to identify the host to the
 *   local locking code.  Each lm_sysid has its own sysid value, but a
 *   given host can have multiple lm_sysid's (even with the same name).
 *   Because the sysid value is recorded by the local locking code, it is
 *   important not to free an lm_sysid if any locks exist for that sysid.
 *   Once the lm_sysid is freed, its sysid value can be reused.  This field
 *   is read-only.
 * - The "next" field is protected by the lock for the lm_sysid list.
 * - The "lock" mutex protects the reference count ("refcnt"), "config",
 *   and "in_recovery", and it is used with "cv".  Each lm_sysid has its
 *   own lock to avoid possible deadlock or recursive mutex panics that
 *   might happen if lm_lock were used.
 * - The "sm_client" and "sm_server" fields indicate whether we have
 *   registered interest in "name" with statd.  sm_client is used when we
 *   are a client of "name", and sm_server is used when we are a server for
 *   "name".  These fields are not MT-protected.  The only correctness
 *   requirement is that the field be FALSE when the lock manager needs to
 *   register the hostname with statd.
 * - The "in_recovery" field is used to prevent client processes from
 *   making requests while the lock manager is going through recovery with
 *   a server.  It is protected by "lock".
 * - The "cv" field is used by threads that are synchronizing on
 *   "in_recovery".
 *
 * Note that an lm_sysid is not freed when its reference count goes to
 * zero.  It is kept around in case it is needed later.  This avoids
 * potentially expensive queries to see if there are any locks or share
 * reservations associated with the lm_sysid.  If memory gets tight, it can
 * be garbage-collected.
 *
 * lm_sysids_lock is a readers/writer lock that protects the integrity
 * of the lm_sysids list itself.  To add an entry to the list, you must
 * acquire the lock as a writer.  To prevent modifications to the list
 * while traversing it, you must acquire it as a reader.
 *
 * To obtain any combination of lm_sysids_lock, lm_lock and lm_sysid->lock
 * (the individual lock), they must be acquired in the order:
 *
 *     lm_globals->lm_sysids_lock > lm_globals->lm_lock > lm_sysid->lock
 *
 * and released in the opposite order to prevent deadlocking.
 */
struct lm_sysid {
	avl_node_t	lsnode;
	int		refcnt;
	struct knetconfig  config;
	struct netbuf   addr;
	char		*name;
	sysid_t		sysid;
	bool_t		sm_client;
	bool_t		sm_server;
	bool_t	in_recovery; /* True while reclaiming locks from `name' */
	int		sm_state; /* State of server statd */
	kmutex_t	lock;
	kcondvar_t	cv;
};

/*
 * For the benefit of warlock.
 * The `LM ignore' protection scheme declared here simply means warlock
 * does not check these fields for any type of locking protection.
 */
#ifndef __lint
_NOTE(MUTEX_PROTECTS_DATA(lm_sysid::lock, lm_sysid::refcnt))
_NOTE(MUTEX_PROTECTS_DATA(lm_sysid::lock, lm_sysid::config))
_NOTE(MUTEX_PROTECTS_DATA(lm_sysid::lock, lm_sysid::in_recovery))
_NOTE(SCHEME_PROTECTS_DATA("LM ignore", lm_sysid::sm_client))
_NOTE(SCHEME_PROTECTS_DATA("LM ignore", lm_sysid::sm_server))
#endif

/*
 * Lock ordering:
 *  lm_globals::lm_sysids_lock
 *  lm_globals::lm_lock
 *  lm_sysid::lock
 *
 * Lock ordering:
 *  lm_globals::lm_vnodes_lock
 *  lm_globals::lm_lock
 */

/*
 * Global declarations.
 */
#define	LM_STATD_DELAY	10	/* Timeout when first contacting local statd. */
#define	LM_NO_TIMOUT	 0	/* Timeout == 0. */
#define	LM_GR_TIMOUT	20	/* Timeout on NLM_GRANTED. Must be big! */
#define	LM_SM_TIMOUT	 5	/* Timeout when talking with SM. */
#define	LM_CR_TIMOUT    20	/* Timeout for SM_SIMU_CRASH. Must be big. */
#define	LM_RLOCK_SLP	20	/* Seconds to sleep before relock_server(). */
#define	LM_ERROR_SLP	30	/* Seconds to sleep after an RPC error. */
#define	LM_GRACE_SLP	10	/* Seconds to sleep if server in grace period */
#define	LM_BLOCK_SLP	60	/* Seconds to sleep if client gets blocked. */
#define	LM_RETRY	 5	/* Retry count in lm_callrpc(). */
/*
 * Retry count for lm_nlm_reclaim()/lm_nlm4_reclaim. This
 * is needed because LM_RETRY was not sufficient enough
 * to have the server setup it's lockd after a crash such that client could
 * obtain the file handle.  The value was made big enough in order
 * to accommodate varying times that it may take different servers to set up
 * its' lockd.
 */
#define	LM_RECLAIM_RETRY	100

/*
 * LM_ASYN_RETRY is not used in the current scheme to deal with
 * retransmission of callbacks to the client after granting a blocked
 * lock.  If we come up with a better scheme we may want to go back
 * to using LM_ASYN_RETRY.
 */
#define	LM_ASYN_RETRY	 3	/* Retry count in lm_asynrpc(). */
#define	LM_PMAP_TIMEOUT	15	/* timeout for talking to portmapper */
#define	LM_GR_RETRY	 1	/* Retry count for GRANTED callback */

/*
 * Owner handle information.  We use the process ID followed by
 * LM_OH_SYS_LEN bytes of system identifier.  See lm_svc() and
 * lm_set_oh().
 */
#define	LM_OH_SYS_LEN	(sizeof (int))
#define	LM_OH_LEN	(sizeof (pid_t) + LM_OH_SYS_LEN)

/*
 * Map for fp -> knetconfig.  The strings in the knetconfig are statically
 * allocated.
 */
struct lm_config {
	struct lm_config *next;		/* link in null-terminated list */
	struct file *fp;		/* fp originally paired w/this config */
	struct knetconfig config;	/* knetconfig info from userland */
};

/*
 * Map for thread -> xprt.
 */
struct lm_xprt {
	struct lm_xprt *next;
	SVCXPRT *xprt;			/* xprt for this service thread */
	kthread_t *thread;
	int valid;			/* 0 ==> invalid entry, 1 ==> valid */
};

/*
 * For the benefit of warlock.  The "scheme" that protects the other
 * lm_xprt fields is that they are only sampled when the lm_server_status
 * state machine is LM_UP, and only written when being created (holding
 * lm_lock) or when being destroyed (state LM_SHUTTING_DOWN or LM_DOWN,
 * at which time LM is essentially running single-threaded).
 */
#ifndef __lint
_NOTE(SCHEME_PROTECTS_DATA("LM xprt", lm_xprt::{xprt thread valid}))
#endif

/*
 * lm_sleep
 *
 * List of client processes sleeping on a blocked NLM_LOCK call.  The lock
 * information is used to make sure we can match a GRANTED call with the
 * right process.  The sysid information is to assist with debugging.  An
 * entry in the list is free iff in_use == FALSE.  The condition variable
 * cv is used by the incoming GRANTED thread to wake us.
 */
struct lm_sleep {
	struct lm_sleep *next;
	struct lm_sysid *sysid;
	pid_t pid;
	struct netobj fh;
	struct netobj oh;
	u_offset_t offset;
	len_t length;
	bool_t in_use;
	bool_t waiting;
	kcondvar_t cv;
	vnode_t *vp;
};

#ifndef __lint
_NOTE(DATA_READABLE_WITHOUT_LOCK(lm_sleep::vp))
#endif

#ifdef _KERNEL
/*
 * Globals and functions used by the lock manager.  Except for
 * functions exported in modstubs.s, these should all be treated as
 * private to the lock manager.
 */

/*
 * The lock manager server code can shut down and then restart (e.g., if
 * lockd is killed and restarted).  While the server is down or shutting
 * down, no requests will be responded to.  Once the server is down, it can
 * be restarted, but not while it is still shutting down.  If a thread
 * needs to wait for the server status to change, it can wait on
 * lm_status_cv.  This status information is protected by lm_lock.
 *
 * lm_num_outstanding - number of outstanding NLM requests
 * lm_numconfigs - number of entries in the lm_config table
 * lm_start_time - time of last start of LM daemons
 * lm_nservers - maximum number of calls that can be handled at once
 * lm_grace_started - grace period has started
 *
 * Although each zone has its own lm_sysids list, the integers are
 * allocated out of a single global integer space, ensuring that
 * different zones will not collide in the local locking code.
 *
 * The lm_sysids list is a cache of remote hosts, either client or server.
 * It is used for building RPC client handles and for mapping a remote host
 * to the integer sysid used by local locking.
 */
typedef struct lm_globals {
	list_node_t		lm_node;
	kmutex_t		lm_lock;
	kmutex_t		lm_vnodes_lock;
	krwlock_t		lm_sysids_lock;
	kcondvar_t		lm_status_cv;

	int			lm_owner_handle_sys;
	pid_t			lm_lockd_pid;
	enum {LM_UP, LM_SHUTTING_DOWN, LM_DOWN}	lm_server_status;
	struct lm_svc_args	lm_sa;
	uint_t			lm_num_outstanding;
	uint_t			lm_numconfigs;
	time_t			lm_start_time;
	bool_t			lm_grace_started;
	zoneid_t		lm_zoneid;

	struct lm_xprt		*lm_xprts;
	struct lm_vnode		*lm_vnodes;
	avl_tree_t		lm_sysids;
	struct lm_client	*lm_clients;
	struct lm_async		*lm_asyncs;
	struct lm_sleep		*lm_sleeps;
	struct lm_config	*lm_configs;

	struct kmem_cache	*lm_xprt_cache;
	struct kmem_cache	*lm_vnode_cache;
	struct kmem_cache	*lm_sysid_cache;
	struct kmem_cache	*lm_client_cache;
	struct kmem_cache	*lm_async_cache;
	struct kmem_cache	*lm_sleep_cache;
	struct kmem_cache	*lm_config_cache;

	unsigned int		lm_vnode_len;
	unsigned int		lm_sysid_len;
	int			lm_async_len;
	int			lm_client_len;
	int			lm_sleep_len;
} lm_globals_t;

#ifndef __lint
_NOTE(DATA_READABLE_WITHOUT_LOCK(lm_globals::start_time))
#endif


extern zone_key_t lm_zone_key;
extern list_t lm_global_list;
extern kmutex_t lm_global_list_lock;

extern time_t		   time;
extern struct lm_stat	   lm_stat;

/* tunable delay for reclaim */
extern int lm_reclaim_factor;

/* Debugging flag for PC file shares. */
extern int share_debug;

#ifdef DEBUG
extern int		   lm_gc_sysids;
#endif

struct pmap;
extern bool_t		   xdr_pmap(XDR *, struct pmap *);
extern void		  *lm_zone_init(zoneid_t);
extern void		   lm_zone_fini(zoneid_t, void *);
extern void		   lm_sysid_init(void);
extern void		   lm_sysid_fini(void);
extern char		  *lm_dup(char *, size_t);
extern int		   lm_delay(clock_t);
extern struct lm_sysid	  *lm_get_me(void);
extern struct lm_sysid	  *lm_get_sysid_locked(lm_globals_t *,
				struct knetconfig *, struct netbuf *, char *,
				bool_t, bool_t *, bool_t *);
extern void		   lm_flush_clients(lm_globals_t *, struct lm_sysid *);
extern int		   lm_netobj_eq(struct netobj *, struct netobj *);
extern int		   lm_callrpc(lm_globals_t *, struct lm_sysid *,
				rpcprog_t, rpcvers_t, rpcproc_t, xdrproc_t,
				caddr_t, xdrproc_t, caddr_t, int, int, bool_t);
extern int		   lm_asynrpc(lm_globals_t *, struct lm_sysid *,
				rpcprog_t, rpcvers_t, rpcproc_t, xdrproc_t,
				caddr_t, int, enum nlm_stats *, int, int);
extern void		   lm_asynrply(lm_globals_t *, int, enum nlm_stats);
extern void		   lm_async_free(lm_globals_t *);
extern struct lm_sleep	  *lm_get_sleep(lm_globals_t *, struct lm_sysid *,
				struct netobj *, struct netobj *, u_offset_t,
				len_t, struct vnode *);
extern void		   lm_rel_sleep(lm_globals_t *, struct lm_sleep *);
extern int		   lm_waitfor_granted(lm_globals_t *, struct lm_sleep *,
				callb_cpr_t *);
extern int		   lm_signal_granted(lm_globals_t *, pid_t,
				struct netobj *, struct netobj *, u_offset_t,
				len_t);
extern void		   lm_sm_client(lm_globals_t *, struct lm_sysid *,
				struct lm_sysid *);
extern struct lm_config	  *lm_saveconfig(lm_globals_t *, struct file *,
				struct knetconfig *);
extern struct lm_config	  *lm_getconfig(lm_globals_t *, struct file *);
extern void		   lm_rmconfig(lm_globals_t *, struct file *);
extern struct lm_xprt	  *lm_savexprt(lm_globals_t *lm, SVCXPRT *xprt);
extern struct lm_xprt	  *lm_getxprt(lm_globals_t *lm);
extern void		   lm_relxprt(lm_globals_t *lm, SVCXPRT *xprt);
extern void		   lm_flush_clients_mem(lm_globals_t *);
extern void		   lm_free_sysid_table(lm_globals_t *);
extern void		   lm_ref_sysid(struct lm_sysid *);
extern int		   lm_sigispending(void);
extern void		   lm_send_siglost(lm_globals_t *, struct flock64 *,
				struct lm_sysid *);
extern void		   lm_set_oh(lm_globals_t *, char *, size_t, pid_t);
extern void		   lm_dup_config(const struct knetconfig *,
					mntinfo_t *);
extern void		   lm_copy_config(struct knetconfig *,
					const struct knetconfig *);
extern void		   lm_free_vnode(void *);
extern void		   lm_free_xprt_map(lm_globals_t *);
extern void		   lm_free_all_vnodes(lm_globals_t *);

#endif /* _KERNEL */

/*
 * Definitions for handling LM debug.
 */
#define	LM_DEBUG_SUPPORT		/* enable runtime tracing */

#ifdef  LM_DEBUG_SUPPORT

/*PRINTFLIKE3*/
extern void lm_debug(int level, char *function, const char *fmt, ...)
	__KPRINTFLIKE(3);

#define	LM_DEBUG(a)			lm_debug a

extern void lm_alock(int, char *, nlm_lock *);
extern void lm_d_nsa(int, char *, nlm_shareargs *);
extern void lm_n_buf(int, char *, char *, struct netbuf *);
extern void lm_alock4(int, char *, nlm4_lock *);
extern void lm_d_nsa4(int, char *, nlm4_shareargs *);

#else /* LM_DEBUG_SUPPORT */

#define	LM_DEBUG(a)

#define	lm_alock(a, b, c)
#define	lm_d_nsa(a, b, c)
#define	lm_n_buf(a, b, c, d)

#endif /* LM_DEBUG_SUPPORT */

#ifdef	__cplusplus
}
#endif

#endif	/* _NFS_LM_IMPL_H */
