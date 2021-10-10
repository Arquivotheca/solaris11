/*
 * Copyright (c) 1994, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copyright 1991 NCR Corporation - Dayton, Ohio, USA
 */

/*	Copyright (c) 1983, 1984, 1985, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

#ifndef _NFS_LM_SERVER_H
#define	_NFS_LM_SERVER_H

#include <sys/types.h>
#include <nfs/rnode.h>
#include <nfs/nfs.h>
#include <sys/note.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The lock manager server code is divided into three files:
 *	lm_server.c ----------- generic server code
 *	lm_nlm_server.c ------- NLMv1-3 protocol specific code
 *	lm_nlm4_server.c ------ NLMv4 protcol specific code
 *
 * N.B. the code in lm_nlm_server.c and lm_nlm4_server.c is nearly
 * identical.  Any changes made to one file should also be made to
 * the corresponding code in the other file.  The main reason these
 * haven't been combined is that the responses made to the client
 * must be done at the same level of the protocol that was used
 * by the client when making the request.  The easiest way to make
 * this information available at the point where the server is
 * responding is to call a different routine for NLMv4.
 *
 * There is also a header file lm_server.h which holds the common
 * definitions used by the "server" files.
 *
 * The file lm_subr.c contains two protocol specific routines.  These
 * are only used for debugging purposes, so they have been left in
 * lm_subr.c.
 */

/*
 * The list is used for keeping vnodes active as long as NFS-locks or NFS-shares
 * exist on them. Each vnode in the list is missing one VN_RELE.
 * If an lm_vnode is free, it should have both a zero reference count and a
 * null vnode pointer.  (A non-free lm_vnode can have a zero reference
 * count, e.g., if there is an active lock for the file but no lock manager
 * threads are using the lm_vnode for the file.)  When an lm_vnode is
 * marked free, its memory is not immediately freed, but free lm_vnode's
 * can be garbage collected later.
 *
 * To obtain both lm_vnodes_lock and lm_lock, they must be acquired
 * in the order:
 *
 *		lm_globals::lm_vnodes_lock > lm_globals::lm_lock
 *
 * and released in the opposite order to prevent deadlocking.
 */
struct lm_vnode {
	struct vnode *vp;
	int count;
	struct lm_block *blocked;
	struct lm_vnode *next;
	nfs_fhandle fh2;		/* storage for v2 file handle */
	nfs_fh3 fh3;		/* storage for v3 file handle */
};

/*
 * For the benefit of warlock.  The "scheme" that protects lv->vp is
 * that the calling thread must obtain a refcount (lv->count) before
 * sampling lv->vp; the vp is only written when lm_lock is held.
 */
#ifndef __lint
_NOTE(SCHEME_PROTECTS_DATA("LM lv count", lm_vnode::vp))
#endif

/*
 * Argument passed to local blocking lock callback routine
 * (lm_block_callback).  This structure allows calling thread
 * to communicate enough info to callback routine to answer
 * a lock request or transmit a lock_res with a status of blocked
 * backed to the client, as appropriate.
 */
typedef struct lm_blockinfo {
	vnode_t *vp;
	int blocked;
	struct lm_sysid *ls;
	struct lm_nlm_disp *disp;
	struct lm_block *lmbp;
	union {
		struct nlm_res *nr;
		struct nlm4_res *nr4;
	} unr;
	struct lm_xprt	*xprt;	/* clustering hook */
	lm_globals_t *lm;
} lm_blockinfo_t;

/*
 * The `blocked' field is only sampled and modified by a single
 * thread and therefore needs no protection.  warlock sees a
 * potential corruption threat here because it knows that a callback
 * is being done to modify this field, but doesn't know the identify
 * of the caller (us).  So we do this just to appease warlock.
 */
#ifndef __lint
_NOTE(SCHEME_PROTECTS_DATA("LM ignore", lm_blockinfo::blocked))
#endif

/*
 * This struct is used to keep state about blocking lock requests.  It has
 * two primary purposes: detecting retransmissions and handling potential
 * race conditions.
 *
 * Retransmissions:
 *
 * It is necessary to detect retransmissions so that we avoid using up
 * multiple service threads to handle what is logically a single request.
 * Also, the local locking code may not be able to detect the
 * retransmission, which can cause the retransmission to fail with, e.g.,
 * EDEADLK.
 *
 * Potential race conditions:
 *
 * Several race conditions can occur with the NLM protocol, particularly if
 * some packets are lost.  For example:
 *   1. Client submits LOCK request, which blocks.
 *   2. Server sends GRANTED call.
 *   3. Client process unblocks, and client sends GRANTED response.
 *   4. GRANTED response is lost.
 *   5. Client does some processing and releases the lock.
 *   6. Some other process obtains the lock.
 *   7. Client decides it needs to get the same lock.  It submits a new LOCK
 *      request, which blocks.
 *   8. Server retransmits GRANTED call.
 *   9. Client fails to recognize that the GRANTED call is a retransmission,
 *      so it incorrectly thinks it has the lock.
 * This struct allows the server to keep track of pending granted calls.
 * In the above scenario step 5, the server is able to search through
 * a list, find the relevant entry, and cancel the retransmission of
 * granted messages.
 *
 * Once a blocked lock request is granted, the server sends the granted
 * call on to the client.  In the normal case, the client will respond to
 * this call and the server will remove the request's entry from its list.
 * However, if the response is lost for some reason, recovery will be
 * necessary.  Incoming requests are compared against this list.  If a
 * match is found, the following table describes what happens depending on
 * the NLM command received.  A match is determined by having the same
 * sysid, pid and at least some part of the blocked lock region in common.
 *
 * Command		Action
 * unlock (same or	assume the client got the granted call and the
 * overlapping region)	response was lost.  Cancel the retransmission
 *			loop and process the unlock request.
 *
 * cancel (same or	assume the client did not get the granted call and
 * overlapping region)	is bailing out.  Cancel the retransmission
 *			loop and process the cancel request.
 *
 * lock (same region	assume the client never got the granted call,
 * non blocking)	gave up, and came back later with a non blocking
 *			request.  Cancel the retransmission loop and
 *			process this request.
 *
 * lock (same region	Assume the client never got the granted call, and
 * blocking)		retransmitted its request.  Cancel the retransmisson
 *			loop.  If we're sure the request is a
 *			retransmission, just grant the lock.  Otherwise,
 *			resubmit the request.
 *
 * lock (for over-	There can be several things going on here:
 * lapping region)	1. the client could be multi-threaded,  2. it
 *			could have received the granted call and the
 *			response got lost, or 3. the granted call could
 *			have been lost and the client gave up and is
 *			trying a new lock.  There is no way to distinguish
 *			between these cases.  The retransmission loop
 *			will be cancelled and this request will be processed.
 *
 * The general solution is that if a match is found on the lm_block
 * list, cancel the retransmission loop and process the request.
 *
 * State table:
 * (rexmit = retransmission comes in while in the corresponding state)
 * (match = request comes in that matches the lm_block, but it can't be
 *	determined for sure whether it's a retransmission)
 *
 *	initial request: ------->[pending]  (rexmit: drop on floor)
 *	put struct on list	   |   |    (match: drop on floor)
 *				   |   |
 *				   |   |
 *	initial req	       <----   |
 *	succeeds w/o blocking:         |
 *	send "granted" and remove      |
 *	struct			       |
 *				       |
 *		initial req	       |
 *		blocks: send "blocked" |
 *		and wait for lock      |
 *				       |
 *				       v
 *				 [blocked] (rexmit: send "blocked"
 *				   |   |    response)
 *				   |   |   (match: send "blocked" response)
 *	request is cancelled:  <----   |
 *	remove struct		       |
 *				       |
 *		request is granted:    |
 *		initiate "granted"     |
 *		callback	       |
 *				       |
 *				       v
 *				 [granted] (rexmit: respond as described
 *				   |   |    above)
 *	"granted" callback	   |   |   (match: submit as new request)
 *	sent: remove struct    <----   |
 *				       |
 *		request is cancelled   |
 *		or unlocked: cancel    |
 *		callback, remove       |
 *		struct		       |
 *				       v
 *
 */

typedef enum {LMB_PENDING, LMB_BLOCKED, LMB_GRANTED} lmb_state_t;

struct lm_block {
	lmb_state_t	lmb_state;
	bool_t		lmb_no_callback; /* cancel GRANTED callback */
	struct flock64	*lmb_flk;	/* offset, etc. for lock req */
	netobj		*lmb_id;	/* for detecting retransmission */
	struct lm_vnode	*lmb_vn;	/* back ptr to lm_vnode */
	struct lm_block *lmb_next;	/* NULL if last entry in list */
};
typedef struct lm_block lm_block_t;

/*
 * Return codes when trying to match a new request with an lm_block list.
 * See lm_find_block().
 */
typedef enum {LMM_NONE, LMM_REXMIT, LMM_FULL, LMM_PARTIAL} lm_match_t;

#ifdef DEBUG

/*
 * Testing hooks:
 *
 * lm_gc_sysids		if enabled, free as many lm_sysid's as possible before
 * 			every outgoing and incoming request.
 *
 * Debugging hooks:
 *
 * lm_dump_block()	dump (to the console) the lm_block list for the
 *			given vnode.
 */

#ifdef _KERNEL
extern int lm_gc_sysids;

extern void lm_dump_block(lm_globals_t *, struct lm_vnode *);
#endif /* _KERNEL */

#endif /* DEBUG */

/* function prototypes for functions found in lm_server.c */
#ifdef _KERNEL
/*
 * Globals and functions used by the lock manager.  Except for
 * functions exported in modstubs.s, these should all be treated as
 * private to the lock manager.
 */

extern void lm_sm_server(lm_globals_t *, struct lm_sysid *, struct lm_sysid *);
extern void lm_rel_vnode(lm_globals_t *, struct lm_vnode *);
extern void lm_unlock_client(lm_globals_t *, struct lm_sysid *);
extern void lm_relock_server(lm_globals_t *, char *);
extern void lm_fail_clients(lm_globals_t *);
extern int lm_shr_sysid_has_locks(lm_globals_t *, int32_t);
extern void lm_reclaim_lock(lm_globals_t *, struct vnode *, struct flock64 *);
extern int nlm_dispatch_enter(lm_globals_t *, SVCXPRT *);
extern void nlm_dispatch_exit(lm_globals_t *);
extern bool_t lm_crash(void *, void *, struct lm_nlm_disp *, struct lm_sysid *,
    uint_t);
extern void lm_log_free_all(const char *, const struct lm_sysid *);

/*
 * the dispatch routine for versions 1-3 of the NLM protocol
 */
void lm_nlm_dispatch(register struct svc_req *, register SVCXPRT *);

/*
 * the lock reclaim routine for versions 1-3 of the NLM protocol
 */
void lm_nlm_reclaim(lm_globals_t *, struct vnode *vp, struct flock64 *flkp);

/*
 * the dispatch routine for version 4 of the NLM protocol
 */
void lm_nlm4_dispatch(register struct svc_req *, register SVCXPRT *);

/*
 * the lock reclaim routine for version 4 of the NLM protocol
 */
void lm_nlm4_reclaim(lm_globals_t *, struct vnode *vp, struct flock64 *flkp);

/*
 * Routines to operate on the lm_block list
 */
extern void lm_add_block(lm_globals_t *, lm_block_t *);
extern lm_match_t lm_find_block(lm_globals_t *, struct flock64 *,
    struct lm_vnode *, netobj *, lm_block_t **);
extern void lm_init_block(lm_globals_t *, lm_block_t *, struct flock64 *,
    struct lm_vnode *, netobj *);
extern void lm_remove_block(lm_globals_t *, lm_block_t *);
extern void lm_release_blocks(lm_globals_t *, sysid_t);
extern void lm_cancel_granted_rxmit(lm_globals_t *, struct flock64 *,
    struct lm_vnode *);
#endif /* _KERNEL */

/*
 * Server NLM dispatcher table.
 * Indexed by procedure number.
 *
 * Most of the procedures ignore the RPC xid that is passed to them.
 * The exception is the code for handling blocking locks, which needs to
 * detect retransmissions.
 *
 * The three reply fields are defined as follows:
 *	do_disp_reply: reply from the dispatch routine
 *	do_block_reply: reply from the the lm_block_lock routine
 *	callback_reply: indicates if reply is callback or sendreply
 *
 * The procedure returns a boolean; if FALSE then the call should be
 * dropped on the floor, forcing the caller to retransmit.
 */

/* values for do_disp_reply, do_block_reply, and callback_reply */
enum lm_disp_reply { LM_REPLY = 10, LM_DONT = 11};
enum lm_disp_how { LM_CALLBACK = 20, LM_SENDREPLY = 21};

struct lm_nlm_disp {
	bool_t (*proc)(void *gen_args, void *gen_res, 	/* proc to call */
		struct lm_nlm_disp *disp, struct lm_sysid *ls, uint_t xid);
	enum lm_disp_reply do_disp_reply;
	enum lm_disp_reply do_block_reply;
	enum lm_disp_how callback_reply;
	unsigned char callback_proc;	/* rpc callback proc number */
	xdrproc_t xdrargs;	/* xdr routine to get args */
	xdrproc_t xdrres;	/* xdr routine to put results */
};

#ifndef __lint
_NOTE(READ_ONLY_DATA(lm_nlm_disp))
#endif

#define	LM_IGNORED	0xff	/* field contents are ignored */

#ifdef	__cplusplus
}
#endif

#endif /* _NFS_LM_SERVER_H */
