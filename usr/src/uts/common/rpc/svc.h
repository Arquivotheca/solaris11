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
 * Copyright (c) 1989, 2011, Oracle and/or its affiliates. All rights reserved.
 */
/* Copyright (c) 1983, 1984, 1985, 1986, 1987, 1988, 1989 AT&T */
/* All Rights Reserved */
/*
 * Portions of this source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 */

/*
 * svc.h, Server-side remote procedure call interface.
 */

#ifndef	_RPC_SVC_H
#define	_RPC_SVC_H

#include <rpc/rpc_com.h>
#include <rpc/rpc_msg.h>
#include <sys/tihdr.h>
#include <sys/poll.h>
#include <sys/tsol/label.h>

#ifdef	_KERNEL
#include <rpc/svc_auth.h>
#include <sys/callb.h>
#include <sys/taskq.h>
#include <sys/kmem.h>
#endif	/* _KERNEL */

/*
 * This interface must manage two items concerning remote procedure calling:
 *
 * 1) An arbitrary number of transport connections upon which rpc requests
 * are received. They are created and registered by routines in svc_generic.c,
 * svc_vc.c and svc_dg.c; they in turn call xprt_register and
 * xprt_unregister.
 *
 * 2) An arbitrary number of locally registered services.  Services are
 * described by the following four data: program number, version number,
 * "service dispatch" function, a transport handle, and a boolean that
 * indicates whether or not the exported program should be registered with a
 * local binder service;  if true the program's number and version and the
 * address from the transport handle are registered with the binder.
 * These data are registered with rpcbind via svc_reg().
 *
 * A service's dispatch function is called whenever an rpc request comes in
 * on a transport.  The request's program and version numbers must match
 * those of the registered service.  The dispatch function is passed two
 * parameters, struct svc_req * and SVCXPRT *, defined below.
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Server-side transport handles.
 * The actual type definitions are below.
 */
#ifdef	_KERNEL
typedef struct __svcmasterxprt	SVCMASTERXPRT;	/* Master transport handle */
typedef struct __svcxprt	SVCXPRT;	/* Per-thread clone handle */
typedef struct __svc_task_pool	SVCTASKPOOL;
#else	/* _KERNEL */
typedef struct __svcxprt	SVCXPRT;	/* Server transport handle */
#endif	/* _KERNEL */

/*
 *  Prototype of error handler callback
 */
#ifndef _KERNEL
typedef void (*svc_errorhandler_t)(const SVCXPRT* svc, const bool_t isAConn);
#endif

/*
 * Service request.
 *
 * PSARC 2003/523 Contract Private Interface
 * svc_req
 * Changes must be reviewed by Solaris File Sharing
 * Changes must be communicated to contract-2003-523@sun.com
 */
struct svc_req {
	rpcprog_t	rq_prog;	/* service program number */
	rpcvers_t	rq_vers;	/* service protocol version */
	rpcproc_t	rq_proc;	/* the desired procedure */
	struct opaque_auth rq_cred;	/* raw creds from the wire */
	caddr_t		rq_clntcred;	/* read only cooked cred */
	SVCXPRT		*rq_xprt;	/* associated transport */
	bslabel_t	*rq_label;	/* TSOL label of the request */
};

#ifdef _KERNEL
struct dupreq {
	uint32_t	dr_xid;
	rpcproc_t	dr_proc;
	rpcvers_t	dr_vers;
	rpcprog_t	dr_prog;
	struct netbuf	dr_addr;
	struct netbuf	dr_resp;
	void		(*dr_resfree)();
	int		dr_status;
	struct dupreq	*dr_next;
	struct dupreq	*dr_chain;
};

/*
 * States of requests for duplicate request caching.
 */
#define	DUP_NEW			0x00	/* new entry */
#define	DUP_INPROGRESS		0x01	/* request already going */
#define	DUP_DONE		0x02	/* request done */
#define	DUP_DROP		0x03	/* request dropped */
#define	DUP_ERROR		0x04	/* error in dup req cache */

/*
 * MAXDUPREQS is the number of cached items.  It should be adjusted
 * to the service load so that there is likely to be a response entry
 * when the first retransmission comes in.
 */
#define	MAXDUPREQS	8192

/*
 * This should be appropriately scaled to MAXDUPREQS.
 */
#define	DRHASHSZ	2049

#if ((DRHASHSZ & (DRHASHSZ - 1)) == 0)
#define	XIDHASH(xid)	((xid) & (DRHASHSZ - 1))
#else
#define	XIDHASH(xid)	((xid) % DRHASHSZ)
#endif
#define	DRHASH(dr)	XIDHASH((dr)->dr_xid)
#define	REQTOXID(req)	((req)->rq_xprt->xp_xid)

typedef struct drc_globals {
	int		dg_numdrq;
	int		dg_maxdrq;
	kmutex_t	dg_lock;
	struct dupreq	*dg_mru;
	struct dupreq	*dg_hashtbl[DRHASHSZ];
	int		dg_hashstat[DRHASHSZ];
} drc_globals_t;

/*
 * globals to manage zone-specific transport private data
 * initialied by svc_init, svc_clts_init, svc_cots_init
 */
typedef struct svc_xprt_globals {
	kmutex_t	sxg_lock;
	int		sxg_clts_cnt;
	int		sxg_cots_cnt;
	list_t		sxg_clts_list;
	list_t		sxg_cots_list;
	zone_key_t	sxg_clts_key;
	zone_key_t	sxg_cots_key;
} svc_xprt_globals_t;
extern svc_xprt_globals_t svc_xprt;

/*
 * Prototype for a service dispatch routine.
 */
typedef void (SVC_DISPATCH)(struct svc_req *, SVCXPRT *);

/*
 * The service provider callout.
 * Each entry identifies a dispatch routine to be called
 * for a given RPC program number and a version fitting
 * into the registered range.
 */
typedef struct {
	rpcprog_t	sc_prog;	/* RPC Program number */
	rpcvers_t	sc_versmin;	/* Min version number */
	rpcvers_t	sc_versmax;	/* Max version number */
	SVC_DISPATCH	*sc_dispatch;	/* Dispatch routine   */
} SVC_CALLOUT;

/*
 * Table of service provider `callouts' for an RPC
 * transport handle. If sct_free is TRUE then transport
 * destructor is supposed to deallocate this table.
 */
typedef struct {
	size_t		sct_size;	/* Number of entries  */
	bool_t		sct_free;	/* Deallocate if true */
	SVC_CALLOUT	*sct_sc;	/* Callout entries    */
} SVC_CALLOUT_TABLE;

struct svc_ops {
	bool_t	(*xp_recv)(SVCXPRT *, mblk_t *, struct rpc_msg *);
		/* receive incoming requests */
	bool_t	(*xp_getargs)(SVCXPRT *, xdrproc_t, caddr_t);
		/* get arguments */
	bool_t	(*xp_reply)(SVCXPRT *, struct rpc_msg *);
		/* send reply */
	bool_t	(*xp_freeargs)(SVCXPRT *, xdrproc_t, caddr_t);
		/* free mem allocated for args */
	void	(*xp_destroy)(SVCMASTERXPRT *);
		/* destroy this struct */
	int	(*xp_dup)(struct svc_req *, caddr_t, int,
				struct dupreq **, bool_t *);
		/* check for dup */
	void	(*xp_dupdone)(struct dupreq *, caddr_t, void (*)(), int, int);
		/* mark dup entry as completed */
	int32_t *(*xp_getres)(SVCXPRT *, int);
		/* get pointer to response buffer */
	void	(*xp_freeres)(SVCXPRT *);
		/* destroy pre-serialized response */
	void	(*xp_clone_destroy)(SVCXPRT *);
		/* destroy a clone xprt */
	void	(*xp_start)(SVCMASTERXPRT *);
		/* `ready-to-receive' */
	void	(*xp_clone_xprt)(SVCXPRT *, SVCXPRT *);
		/* transport specific clone function */
	void	(*xp_tattrs) (SVCXPRT *, int, void **);
};

#define	SVC_TATTR_ADDRMASK	1

#else	/* _KERNEL */
/*
 *	Service control requests
 */
#define	SVCGET_VERSQUIET	1
#define	SVCSET_VERSQUIET	2
#define	SVCGET_XID		4
#define	SVCSET_KEEPALIVE	5
#define	SVCSET_CONNMAXREC	6
#define	SVCGET_CONNMAXREC	7
#define	SVCGET_RECVERRHANDLER	8
#define	SVCSET_RECVERRHANDLER	9

enum xprt_stat {
	XPRT_DIED,
	XPRT_MOREREQS,
	XPRT_IDLE
};

struct xp_ops {
#ifdef	__STDC__
	bool_t	(*xp_recv)(SVCXPRT *, struct rpc_msg *);
		/* receive incoming requests */
	enum xprt_stat (*xp_stat)(SVCXPRT *);
		/* get transport status */
	bool_t	(*xp_getargs)(SVCXPRT *, xdrproc_t, caddr_t);
		/* get arguments */
	bool_t	(*xp_reply)(SVCXPRT *,	struct rpc_msg *);
		/* send reply */
	bool_t	(*xp_freeargs)(SVCXPRT *, xdrproc_t, caddr_t);
		/* free mem allocated for args */
	void	(*xp_destroy)(SVCXPRT *);
		/* destroy this struct */
	bool_t	(*xp_control)(SVCXPRT *, const uint_t,	void *);
		/* catch-all control function */
#else	/* __STDC__ */
	bool_t	(*xp_recv)(); /* receive incoming requests */
	enum xprt_stat (*xp_stat)(); /* get transport status */
	bool_t	(*xp_getargs)(); /* get arguments */
	bool_t	(*xp_reply)(); /* send reply */
	bool_t	(*xp_freeargs)(); /* free mem allocated for args */
	void	(*xp_destroy)(); /* destroy this struct */
	bool_t	(*xp_control)(); /* catch-all control function */
#endif	/* __STDC__ */
};
#endif	/* _KERNEL */

#ifdef	_KERNEL

/*
 * Server side transport handle (SVCMASTERXPRT).
 *
 * A kernel service provider may register a callback function "closeproc"
 * for a transport.  When the transport is closing the last exiting attached
 * thread - xp_threads goes to zero - it calls the callback function, passing
 * it a reference to the transport.  This call is made with xp_ref_lock
 * held, so any cleanup bookkeeping it does should be done quickly.
 *
 * When the transport is closing the last exiting thread is supposed
 * to destroy/free the data structure.
 */
typedef struct __svcxprt_common {
	struct file	*xpc_fp;
	struct svc_ops	*xpc_ops;
	queue_t		*xpc_wq;	/* queue to write onto		*/
	cred_t		*xpc_cred;	/* cached cred for server to use */
	int32_t		xpc_type;	/* transport type		*/
	int		xpc_msg_size;	/* TSDU or TIDU size		*/
	struct netbuf	xpc_rtaddr;	/* remote transport address	*/
	struct netbuf	xpc_lcladdr;	/* local transport address	*/
	char		*xpc_netid;	/* network token		*/
	SVC_CALLOUT_TABLE *xpc_sct;
} __SVCXPRT_COMMON;

#define	xp_fp		xp_xpc.xpc_fp
#define	xp_ops		xp_xpc.xpc_ops
#define	xp_wq		xp_xpc.xpc_wq
#define	xp_cred		xp_xpc.xpc_cred
#define	xp_type		xp_xpc.xpc_type
#define	xp_msg_size	xp_xpc.xpc_msg_size
#define	xp_rtaddr	xp_xpc.xpc_rtaddr
#define	xp_lcladdr	xp_xpc.xpc_lcladdr
#define	xp_sct		xp_xpc.xpc_sct
#define	xp_netid	xp_xpc.xpc_netid

/*
 * Async svc replies
 */

typedef struct rpc_async_msg {
	SVCXPRT *ram_xprt;
	xdrproc_t ram_proc;
	caddr_t	ram_procargs;
	void (*ram_cbfunc) (void *, int);
	void *ram_cbargs;
	list_node_t ram_next;
} rpc_async_msg_t;

typedef struct __svc_async_msgs {
	list_t		msg_list;
	kmutex_t	msg_lock;
	int		msg_indrain;
} SVC_ASYNC_MSGS;

struct __svcmasterxprt {
	__SVCXPRT_COMMON xp_xpc;	/* Fields common with the clone	*/

	int		xp_flags;	/* State of the master xprt */
	SVCTASKPOOL	*xp_tpool;	/* task pool for request dispatch */

	SVC_ASYNC_MSGS	xp_async_msgs;	/* async reply msgs */

	kmutex_t	xp_ref_lock;	/* ref count lock	*/

	int		xp_ref;		/* # of references to the */
					/* master xprt by clone-xprts */

	void		(*xp_closeproc)(const SVCMASTERXPRT *);
					/* optional; see comments above	*/
	struct netbuf	xp_addrmask;	/* address mask			*/

	caddr_t		xp_p2;		/* private: for use by svc ops  */
	list_node_t	xp_next;	/* xprt in the flow ctrl list */
};

/*
 * xprt flags
 */

#define	XPRT_QUEUE_CLOSED	0x1
#define	XPRT_FLOWCTRL_ON	0x2

/*
 * Service thread `clone' transport handle (SVCXPRT)
 *
 * PSARC 2003/523 Contract Private Interface
 * SVCXPRT
 * Changes must be reviewed by Solaris File Sharing
 * Changes must be communicated to contract-2003-523@sun.com
 *
 * The xp_p2buf buffer is used as the storage for a transport type
 * specific structure. It is private for the svc ops for a given
 * transport type.
 */

#define	SVC_P2LEN   128

struct __svcxprt {
	__SVCXPRT_COMMON xp_xpc;
	SVCMASTERXPRT	*xp_master;	/* back ptr to master		*/

	/* The following fileds are on a per-thread basis */
	callb_cpr_t	*xp_cprp;	/* unused padding for Contract	*/

	/* The following fields are used on a per-request basis */
	struct opaque_auth xp_verf;	/* raw response verifier	*/
	SVCAUTH		xp_auth;	/* auth flavor of current req	*/
	void		*xp_cookie;	/* a cookie			*/
	uint32_t	xp_xid;		/* id				*/
	XDR		xp_xdrin;	/* input xdr stream		*/
	XDR		xp_xdrout;	/* output xdr stream		*/

	/* Private for svc ops */
	char		xp_p2buf[SVC_P2LEN]; /* udp_data or cots_data_t */
						/* or clone_rdma_data_t */
};



/* server task pool interfaces */

typedef struct svc_tqueue svc_tqueue_t;

#define	RQCRED_SIZE	400	/* this size is excessive */
#define	CRED_AREA_SIZE	(2 * MAX_AUTH_BYTES + RQCRED_SIZE)

#define	SVC_RQCRED_AREA_SZ	(2 * MAX_AUTH_BYTES + RQCRED_SIZE)

/* Pool flags */
#define	STP_NOSETUP		0x00010000
#define	STP_CLOSING		0x00020000
#define	STP_RUNNING		0x00040000
#define	STP_CSWCTRL_ON		0x00080000	/* csw throttle on */
#define	STP_FLOWCTRL_ON		0x00100000	/* flow controlled */
#define	STP_ASYNC_REPLY		0x00200000	/* async reply capable */

/*
 * For counumer's unregister/shutdown callbacks.
 */
typedef struct stp_callback {
	void	(*scb_func)(void *);
	void	*scb_arg;
} stp_callback_t;

/*
 * RPC tasks.  st_xprt MUST be the first element below
 * for structure overloading.
 */
typedef struct svc_task {
	SVCXPRT	st_xprt;	/* xprt handle for the task */
	mblk_t	*st_mp;		/* request payload */
	list_node_t st_next; 	/* next task on the queue */
	svc_tqueue_t *st_queue; /* assigned queue */
	clock_t st_qtime;	/* time the task was queued */
	char	st_rqcred[SVC_RQCRED_AREA_SZ];	/* request creds */
} svc_task_t;


struct svc_tqueue {
	uint32_t stq_msgcnt;		/* # of msgs in queue */
	uint32_t stq_active;		/* # of active threads */
	uint32_t stq_throttle;		/* active thread throttle */
	uint32_t stq_maxthreads;	/* max threads per queue */

	/*
	 * Parameters to monitor the request flows
	 * detected over back-to-back monitored intervals
	 */
	timeout_id_t stq_mtid;		/* monitor timeout() id */
	clock_t stq_monitor_ts;		/* timestamp when monitor last ran */
	uint32_t stq_csw;		/* # of csw per monitored interval */
	uint32_t stq_over_cswl;		/* # times over csw cutoff */
	uint32_t stq_under_cswl;	/* # times under csw cutoff */

	uint32_t stq_rollcall;		/* rollcall for active threads */
	uint32_t stq_over_throttle;	/* count of active increments */
					/* over throttle */
	uint32_t stq_under_throttle;	/* count of intervals # active */
					/* under throttle */

	uint_t	stq_disp_failed;	/* taskq dispatch failure count */
	timeout_id_t stq_rtid;		/* dispatch retry timeout id */

	kmutex_t stq_lock;
	list_t stq_task_list;
	SVCTASKPOOL *stq_tpool;
};

struct __svc_task_pool {
	list_node_t	stp_next;	/* next pool in the global list */
	taskq_t		*stp_tq;	/* taskq to process requests */
	uint32_t	stp_id;		/* pool id */
	uint32_t	stp_flags;
	char		stp_proc_name[MAXCOMLEN + 1];
	zoneid_t	stp_zoneid;

	/*
	 * Lock ordering between svc_gloabls->sg_lock and stp_mtx
	 * sg_lock -> stp_mtx.
	 */
	kmutex_t	stp_mtx;
	kcondvar_t	stp_cv;
	int		stp_ref;	/* references to the pool */
	kcondvar_t	stp_waitcv;	/* CV for svc_wait() thread */
	kcondvar_t	stp_fcwaitcv;	/* CV for flow control */

	svc_tqueue_t **stp_queue_array;
	int		stp_qarray_len;		/* # of queues */
	int		stp_maxthreads;		/* max thread limit */

	/* pool's cache */
	kmem_cache_t 	*stp_task_cache;	/* for queue's tasks */
	kmem_cache_t *stp_async_msg_cache;	/* for async replies */
	struct svc_globals *stp_svcgp;		/* svc_globlas */

	kstat_t		*stp_kstat;

	timeout_id_t	stp_fctid;	/* Flow control timeout id */
	list_t		stp_fclist;	/* Consumers to notify on unblock */

	/* external callout routines */
	stp_callback_t stp_unregister;	/* callout for unregister */
	stp_callback_t stp_shutdown;	/* callout for shutdown */
	void		*stp_handle;	/* consumer specified ptr */
};

#else	/* _KERNEL */
struct __svcxprt {
	int		xp_fd;
#define	xp_sock		xp_fd
	ushort_t	xp_port;
	/*
	 * associated port number.
	 * Obsolete, but still used to
	 * specify whether rendezvouser
	 * or normal connection
	 */
	struct	xp_ops	*xp_ops;
	int		xp_addrlen;	/* length of remote addr. Obsoleted */
	char		*xp_tp;		/* transport provider device name */
	char		*xp_netid;	/* network token */
	struct netbuf	xp_ltaddr;	/* local transport address */
	struct netbuf	xp_rtaddr;	/* remote transport address */
	char		xp_raddr[16];	/* remote address. Now obsoleted */
	struct opaque_auth xp_verf;	/* raw response verifier */
	caddr_t		xp_p1;		/* private: for use by svc ops */
	caddr_t		xp_p2;		/* private: for use by svc ops */
	caddr_t		xp_p3;		/* private: for use by svc lib */
	int		xp_type;	/* transport type */
	/*
	 * callback on client death
	 * First parameter is the current structure,
	 * Second parameter :
	 *	- FALSE for the service listener
	 *	- TRUE for a real connected socket
	 */
	svc_errorhandler_t xp_closeclnt;
};
#endif	/* _KERNEL */

/*
 *  Approved way of getting address of caller,
 *  address mask, and netid of transport.
 */
#define	svc_getrpccaller(x) (&(x)->xp_rtaddr)
#ifdef _KERNEL
#define	svc_getcaller(x) (&(x)->xp_rtaddr.buf)
#define	svc_getaddrmask(x) (&(x)->xp_master->xp_addrmask)
#define	svc_getnetid(x) ((x)->xp_netid)
#endif	/* _KERNEL */

/*
 * Operations defined on an SVCXPRT handle
 */

#ifdef	_KERNEL

#define	SVC_GETADDRMASK(clone_xprt, attrflag, tattr) \
(*(clone_xprt)->xp_ops->xp_tattrs)((clone_xprt), (attrflag), (tattr))

#define	SVC_CLONE_XPRT(src_xprt, dst_xprt) \
	if ((src_xprt)->xp_ops->xp_clone_xprt) \
		(*(src_xprt)->xp_ops->xp_clone_xprt) \
		    (src_xprt, dst_xprt)

#define	SVC_RECV(clone_xprt, mp, msg) \
	(*(clone_xprt)->xp_ops->xp_recv)((clone_xprt), (mp), (msg))

/*
 * Pointers to transport specific `rele' routines in rpcmod (set from rpcmod).
 */
extern void (*rpc_rele)(queue_t *, mblk_t *);
extern void (*mir_rele)(queue_t *, mblk_t *);
extern void (*rdma_rele)(queue_t *, mblk_t *);
extern void mir_unblock(queue_t *);

/*
 * PSARC 2003/523 Contract Private Interface
 * SVC_GETARGS
 * Changes must be reviewed by Solaris File Sharing
 * Changes must be communicated to contract-2003-523@sun.com
 */
#define	SVC_GETARGS(clone_xprt, xargs, argsp) \
	(*(clone_xprt)->xp_ops->xp_getargs)((clone_xprt), (xargs), (argsp))

#define	SVC_REPLY(clone_xprt, msg) \
	(*(clone_xprt)->xp_ops->xp_reply) ((clone_xprt), (msg))

#define	SVC_FREEARGS(clone_xprt, xargs, argsp)	\
	(*(clone_xprt)->xp_ops->xp_freeargs)((clone_xprt), (xargs), (argsp))

#define	SVC_GETRES(clone_xprt, size) \
	(*(clone_xprt)->xp_ops->xp_getres)((clone_xprt), (size))

#define	SVC_FREERES(clone_xprt)	\
	(*(clone_xprt)->xp_ops->xp_freeres)(clone_xprt)

#define	SVC_DESTROY(xprt) \
	(*(xprt)->xp_ops->xp_destroy)(xprt)

/*
 * PSARC 2003/523 Contract Private Interfaces
 * SVC_DUP, SVC_DUPDONE, SVC_DUP_EXT, SVC_DUPDONE_EXT
 * Changes must be reviewed by Solaris File Sharing
 * Changes must be communicated to contract-2003-523@sun.com
 *
 * SVC_DUP and SVC_DUPDONE are defined here for backward compatibility.
 */
#define	SVC_DUP_EXT(clone_xprt, req, res, size, drpp, dupcachedp) \
	(*(clone_xprt)->xp_ops->xp_dup)(req, res, size, drpp, dupcachedp)

#define	SVC_DUPDONE_EXT(clone_xprt, dr, res, resfree, size, status) \
	(*(clone_xprt)->xp_ops->xp_dupdone)(dr, res, resfree, size, status)

#define	SVC_DUP(clone_xprt, req, res, size, drpp) \
	(*(clone_xprt)->xp_ops->xp_dup)(req, res, size, drpp, NULL)

#define	SVC_DUPDONE(clone_xprt, dr, res, size, status) \
	(*(clone_xprt)->xp_ops->xp_dupdone)(dr, res, NULL, size, status)

#define	SVC_CLONE_DESTROY(clone_xprt) \
	(*(clone_xprt)->xp_ops->xp_clone_destroy)(clone_xprt)


#define	SVC_START(xprt) \
	(*(xprt)->xp_ops->xp_start)(xprt)

#else	/* _KERNEL */

#define	SVC_RECV(xprt, msg) \
	(*(xprt)->xp_ops->xp_recv)((xprt), (msg))
#define	svc_recv(xprt, msg) \
	(*(xprt)->xp_ops->xp_recv)((xprt), (msg))

#define	SVC_STAT(xprt) \
	(*(xprt)->xp_ops->xp_stat)(xprt)
#define	svc_stat(xprt) \
	(*(xprt)->xp_ops->xp_stat)(xprt)

#define	SVC_GETARGS(xprt, xargs, argsp) \
	(*(xprt)->xp_ops->xp_getargs)((xprt), (xargs), (argsp))
#define	svc_getargs(xprt, xargs, argsp)	\
	(*(xprt)->xp_ops->xp_getargs)((xprt), (xargs), (argsp))

#define	SVC_REPLY(xprt, msg) \
	(*(xprt)->xp_ops->xp_reply) ((xprt), (msg))
#define	svc_reply(xprt, msg) \
	(*(xprt)->xp_ops->xp_reply) ((xprt), (msg))

#define	SVC_FREEARGS(xprt, xargs, argsp) \
	(*(xprt)->xp_ops->xp_freeargs)((xprt), (xargs), (argsp))
#define	svc_freeargs(xprt, xargs, argsp) \
	(*(xprt)->xp_ops->xp_freeargs)((xprt), (xargs), (argsp))

#define	SVC_GETRES(xprt, size) \
	(*(xprt)->xp_ops->xp_getres)((xprt), (size))
#define	svc_getres(xprt, size) \
	(*(xprt)->xp_ops->xp_getres)((xprt), (size))

#define	SVC_FREERES(xprt) \
	(*(xprt)->xp_ops->xp_freeres)(xprt)
#define	svc_freeres(xprt) \
	(*(xprt)->xp_ops->xp_freeres)(xprt)

#define	SVC_DESTROY(xprt) \
	(*(xprt)->xp_ops->xp_destroy)(xprt)
#define	svc_destroy(xprt) \
	(*(xprt)->xp_ops->xp_destroy)(xprt)

/*
 * PSARC 2003/523 Contract Private Interface
 * SVC_CONTROL
 * Changes must be reviewed by Solaris File Sharing
 * Changes must be communicated to contract-2003-523@sun.com
 */
#define	SVC_CONTROL(xprt, rq, in) \
	(*(xprt)->xp_ops->xp_control)((xprt), (rq), (in))
#endif	/* _KERNEL */

/*
 * Pool id's reserved for NFS, NLM, and the NFSv4 callback program.
 */
#define	NFS_SVCPOOL_ID		0x01
#define	NLM_SVCPOOL_ID		0x02
#define	NFS_CB_SVCPOOL_ID	0x03
#define	RDC_SVCPOOL_ID		0x05	/* SNDR, PSARC 2001/699 */

struct svcpool_args {
	uint32_t	id;		/* Pool id */
	uint32_t	maxthreads;	/* Max threads in the pool */
	uint32_t	redline;	/* `Redline' for the pool */
	uint32_t	qsize;		/* `xprt-ready' queue size */
	uint32_t	timeout;	/* svc_poll() timeout */
	uint32_t	stksize;	/* svc_run() stack size */
	uint32_t	max_same_xprt;	/* Max reqs from the same xprt */
};


#ifdef	_KERNEL
/*
 * Transport registration and thread pool creation.
 */

/*
 * Global SVC variables (private).
 */
struct svc_globals {
	/* protects the stp_list */
	kmutex_t sg_lock;
	list_t sg_stp_list;
};

extern int svc_do_run(int);
extern int svc_wait(int);
extern int svc_pool_create(struct svcpool_args *);
extern int stp_curthread_can_block(SVCXPRT *);
extern void stp_init(struct svc_globals *);
extern void stp_destroy(struct svc_globals *);
extern int stp_queuereq(queue_t *, mblk_t *);
extern int stp_xprt_register(SVCMASTERXPRT *, int);
extern void stp_xprt_unregister(SVCMASTERXPRT *);
extern int stp_pool_alloc(int, int);
extern void stp_free_allpools(struct svc_globals *);
extern int stp_wait(int);
extern void stp_async_reply(void *arg);
extern void stp_task_free(svc_task_t *);
extern int stp_pool_control(int, int, void (*)(void *), void *);
extern int svc_pool_control(int, int, void (*)(void *), void *);

#define	SVCPSET_SHUTDOWN_PROC	1
#define	SVCPSET_UNREGISTER_PROC	2
#define	SVCPSET_HANDLE		3
#define	SVCPSET_ASYNCREPLY	4

#define	SVC_XPRT2TPOOL(xprt) (xprt->xp_master->xp_tpool)
#define	SVC_XPRT2PHANDLE(xprt)  (xprt->xp_master->xp_tpool->stp_handle)
#define	SVC_REQ2PHANDLE(req)   (SVC_XPRT2PHANDLE(req->rq_xprt))

/*
 * This macro picks which `rele' routine to use, based on the transport type.
 */
#define	RELE_PROC(xprt) \
	((xprt)->xp_type == T_RDMA ? rdma_rele : \
	(((xprt)->xp_type == T_CLTS) ? rpc_rele : mir_rele))

/*
 * In case of COTS/TCP, multiple simultaneous sendreply()'s for the
 * same xprt block at TCP layer due to squeue serialization. In such
 * cases queueing the sendreply()s with async replies frees up
 * RPC threads for other work.
 */

#define	SVC_QUEUE_SENDREPLY(xprt) \
	(((xprt)->xp_type == T_COTS || (xprt)->xp_type == T_COTS_ORD) ? 1 : 0)

#else	/* _KERNEL */
#ifdef	__STDC__
extern bool_t	rpc_reg(const rpcprog_t, const rpcvers_t, const rpcproc_t,
			char *(*)(char *), const xdrproc_t, const xdrproc_t,
			const char *);

/*
 * Service registration
 *
 * svc_reg(xprt, prog, vers, dispatch, nconf)
 *	const SVCXPRT *xprt;
 *	const rpcprog_t prog;
 *	const rpcvers_t vers;
 *	const void (*dispatch)();
 *	const struct netconfig *nconf;
 */
extern bool_t	svc_reg(const SVCXPRT *, const rpcprog_t, const rpcvers_t,
			void (*)(struct svc_req *, SVCXPRT *),
			const struct netconfig *);

/*
 * Service authentication registration
 *
 * svc_auth_reg(cred_flavor, handler)
 *    int cred_flavor;
 *    enum auth_stat (*handler)();
 */
extern int	svc_auth_reg(int, enum auth_stat (*)());

/*
 * Service un-registration
 *
 * svc_unreg(prog, vers)
 *	const rpcprog_t prog;
 *	const rpcvers_t vers;
 */
extern void	svc_unreg(const rpcprog_t, const rpcvers_t);

/*
 * Transport registration/unregistration.
 *
 * xprt_register(xprt)
 *	const SVCXPRT *xprt;
 *
 * xprt_unregister(xprt)
 *	const SVCXPRT *xprt;
 */
extern void	xprt_register(const SVCXPRT *);
extern void	xprt_unregister(const SVCXPRT *);
#else	/* __STDC__ */
extern bool_t	rpc_reg();
extern bool_t	svc_reg();
extern bool_t	svc_auth_reg();
extern void	svc_unreg();
extern void	xprt_register();
extern void	xprt_unregister();
#endif /* __STDC__ */
#endif	/* _KERNEL */


/*
 * When the service routine is called, it must first check to see if it
 * knows about the procedure;  if not, it should call svcerr_noproc
 * and return.  If so, it should deserialize its arguments via
 * SVC_GETARGS (defined above).  If the deserialization does not work,
 * svcerr_decode should be called followed by a return.  Successful
 * decoding of the arguments should be followed the execution of the
 * procedure's code and a call to svc_sendreply.
 *
 * Also, if the service refuses to execute the procedure due to too-
 * weak authentication parameters, svcerr_weakauth should be called.
 * Note: do not confuse access-control failure with weak authentication!
 *
 * NB: In pure implementations of rpc, the caller always waits for a reply
 * msg.  This message is sent when svc_sendreply is called.
 * Therefore pure service implementations should always call
 * svc_sendreply even if the function logically returns void;  use
 * xdr.h - xdr_void for the xdr routine.  HOWEVER, connectionful rpc allows
 * for the abuse of pure rpc via batched calling or pipelining.  In the
 * case of a batched call, svc_sendreply should NOT be called since
 * this would send a return message, which is what batching tries to avoid.
 * It is the service/protocol writer's responsibility to know which calls are
 * batched and which are not.  Warning: responding to batch calls may
 * deadlock the caller and server processes!
 */
#ifdef	__STDC__
extern bool_t	svc_sendreply(const SVCXPRT *, const xdrproc_t,	const caddr_t);
extern void	svcerr_decode(const SVCXPRT *);
extern void	svcerr_weakauth(const SVCXPRT *);
extern void	svcerr_noproc(const SVCXPRT *);
extern void	svcerr_progvers(const SVCXPRT *, const rpcvers_t,
    const rpcvers_t);
extern void	svcerr_auth(const SVCXPRT *, const enum auth_stat);
extern void	svcerr_noprog(const SVCXPRT *);
extern void	svcerr_systemerr(const SVCXPRT *);
extern void	svcerr_badcred(const SVCXPRT *);
#else	/* __STDC__ */
extern bool_t	svc_sendreply();
extern void	svcerr_decode();
extern void	svcerr_weakauth();
extern void	svcerr_noproc();
extern void	svcerr_progvers();
extern void	svcerr_auth();
extern void	svcerr_noprog();
extern void	svcerr_systemerr();
extern void	svcerr_badcred();
#endif	/* __STDC__ */

#ifdef	_KERNEL
/*
 * Kernel RPC functions.
 */
extern void	svc_init(void);
extern void	svc_cots_init(void);
extern void	svc_clts_init(void);
extern void	svc_fini(void);
extern void	svc_cots_fini(void);
extern void	svc_clts_fini(void);
extern void	svc_purge_dupcache(drc_globals_t *);

extern void	mt_kstat_init(void);
extern void	mt_kstat_fini(void);
extern int	svc_tli_kcreate(struct file *, uint_t, char *,
				struct netbuf *, SVCMASTERXPRT **,
				SVC_CALLOUT_TABLE *,
				void (*closeproc)(const SVCMASTERXPRT *),
				int, bool_t);
extern int	svc_clts_kcreate(struct file *, uint_t, struct T_info_ack *,
				SVCMASTERXPRT **);
extern int	svc_cots_kcreate(struct file *, uint_t, struct T_info_ack *,
				SVCMASTERXPRT **);
extern void	svc_queuereq(queue_t *, mblk_t *);
extern void	svc_queueclean(queue_t *);
extern void	svc_queueclose(queue_t *);
extern int	svc_reserve_thread(SVCXPRT *);
extern void	svc_unreserve_thread(SVCXPRT *);
extern callb_cpr_t *svc_detach_thread(SVCXPRT *);
extern void	svc_process_request(SVCXPRT *, mblk_t *, char *);
extern int	svc_async_sendreply(SVCXPRT *, xdrproc_t,
				caddr_t, void (*)(void *, int), void *);
extern void	svc_xprt_free(SVCXPRT *);
extern void 	svc_unblock(SVCMASTERXPRT *);

/*
 * For RDMA based kRPC.
 * "rdma_xprt_record" is a reference to master transport handles
 * in kRPC thread pools. This is an easy way of tracking and shuting
 * down rdma based kRPC transports on demand.
 * "rdma_xprt_group" is a list of RDMA based mster transport handles
 * or records in a kRPC thread pool.
 */
typedef struct rdma_xprt_record		rdma_xprt_record_t;
struct rdma_xprt_record {
	int			rtr_type;	/* Type of rdma; IB/VI/RDDP */
	SVCMASTERXPRT		*rtr_xprt_ptr;	/* Ptr to master xprt handle */
	rdma_xprt_record_t	*rtr_next;	/* Ptr to next record */
};

typedef struct {
	int			rtg_count;	/* Number transport records */
	int			rtg_poolid;	/* Pool Id for this group */
	rdma_xprt_record_t	*rtg_listhead;	/* Head of the records list */
} rdma_xprt_group_t;

extern int	svc_rdma_kcreate(char *, SVC_CALLOUT_TABLE *, int,
			rdma_xprt_group_t *);
extern void	svc_rdma_kstop(SVCMASTERXPRT *);
extern void	svc_rdma_kdestroy(SVCMASTERXPRT *);
extern void	rdma_stop(rdma_xprt_group_t *);

/*
 * GSS cleanup method.
 */
extern void	rpc_gss_cleanup(SVCXPRT *);
extern void	(*rpc_gss_drain_func)(void);
#else	/* _KERNEL */
/*
 * Lowest level dispatching -OR- who owns this process anyway.
 * Somebody has to wait for incoming requests and then call the correct
 * service routine.  The routine svc_run does infinite waiting; i.e.,
 * svc_run never returns.
 * Since another (co-existant) package may wish to selectively wait for
 * incoming calls or other events outside of the rpc architecture, the
 * routine svc_getreq_poll is provided.  It must be passed pollfds, the
 * "in-place" results of a poll call (see poll, section 2).
 */

/*
 * Global keeper of rpc service descriptors in use
 * dynamic; must be inspected before each call to select or poll
 */
extern pollfd_t	*svc_pollfd;
extern int	svc_max_pollfd;
extern fd_set	svc_fdset;
#if !defined(_LP64) && FD_SETSIZE > 1024
extern fd_set	_new_svc_fdset;
#ifdef __PRAGMA_REDEFINE_EXTNAME
#pragma redefine_extname	svc_fdset	_new_svc_fdset
#else   /* __PRAGMA_REDEFINE_EXTNAME */
#define	svc_fdset	_new_svc_fdset
#endif  /* __PRAGMA_REDEFINE_EXTNAME */
#endif	/* LP64 && FD_SETSIZE > 1024 */
#define	svc_fds svc_fdset.fds_bits[0]	/* compatibility */

/*
 * A small program implemented by the svc_rpc implementation itself.
 * Also see clnt.h for protocol numbers.
 */
#ifdef __STDC__
extern void	svc_getreq(int);
extern void	svc_getreq_common(const int);
extern void	svc_getreqset(fd_set *); /* takes fdset instead of int */
extern void	svc_getreq_poll(struct pollfd *, const int);
extern void	svc_run(void);
extern void	svc_exit(void);
#else	/* __STDC__ */
extern void	rpctest_service();
extern void	svc_getreqset();
extern void	svc_getreq();
extern void	svc_getreq_common();
extern void	svc_getreqset();	 /* takes fdset instead of int */
extern void	svc_getreq_poll();
extern void	svc_run();
extern void	svc_exit();
#endif	/* __STDC__ */

/*
 *  Functions used to manage user file descriptors
 */
typedef int svc_input_id_t;
typedef void (*svc_callback_t)(svc_input_id_t id, int fd,
				unsigned int events, void* cookie);

#ifdef __STDC__
extern svc_input_id_t svc_add_input(int fd, unsigned int events,
				svc_callback_t user_callback,
				void* cookie);
extern int svc_remove_input(svc_input_id_t id);
#else	/* __STDC__ */
extern svc_input_id_t svc_add_input();
extern int	svc_remove_input();
#endif

/*
 * These are the existing service side transport implementations.
 *
 * Transport independent svc_create routine.
 */
#ifdef __STDC__
extern int	svc_create(void (*)(struct svc_req *, SVCXPRT *),
				const rpcprog_t, const rpcvers_t,
				const char *);
	/*
	 * 	void (*dispatch)();		-- dispatch routine
	 *	const rpcprog_t prognum;	-- program number
	 *	const rpcvers_t versnum;	-- version number
	 *	const char *nettype;		-- network type
	 */

/*
 * Generic server creation routine. It takes a netconfig structure
 * instead of a nettype.
 */
extern SVCXPRT	*svc_tp_create(void (*)(struct svc_req *, SVCXPRT *),
				const rpcprog_t, const rpcvers_t,
				const struct netconfig *);
	/*
	 * void (*dispatch)();			-- dispatch routine
	 * const rpcprog_t prognum;		-- program number
	 * const rpcvers_t versnum;		-- version number
	 * const struct netconfig *nconf;	-- netconfig structure
	 */

/*
 * Generic TLI create routine
 */
extern  SVCXPRT	*svc_tli_create(const int, const struct netconfig *,
				const struct t_bind *, const uint_t,
				const uint_t);
	/*
	 *	const int fd;			-- connection end point
	 *	const struct netconfig *nconf;	-- netconfig structure
	 *	const struct t_bind *bindaddr;	-- local bind address
	 *	const uint_t sendsz;		-- max sendsize
	 *	const uint_t recvsz;		-- max recvsize
	 */

/*
 * Connectionless and connectionful create routines.
 */
extern SVCXPRT	*svc_vc_create(const int, const uint_t, const uint_t);
	/*
	 *	const int fd;			-- open connection end point
	 *	const uint_t sendsize;		-- max send size
	 *	const uint_t recvsize;		-- max recv size
	 */

extern SVCXPRT	*svc_dg_create(const int, const uint_t, const uint_t);
	/*
	 * const int fd;			-- open connection
	 * const uint_t sendsize;		-- max send size
	 * const uint_t recvsize;		-- max recv size
	 */

/*
 * the routine takes any *open* TLI file
 * descriptor as its first input and is used for open connections.
 */
extern  SVCXPRT	*svc_fd_create(const int, const uint_t, const uint_t);
	/*
	 * 	const int fd;			-- open connection end point
	 * 	const uint_t sendsize;		-- max send size
	 * 	const uint_t recvsize;		-- max recv size
	 */

/*
 * Memory based rpc (for speed check and testing)
 */
extern SVCXPRT	*svc_raw_create(void);

/*
 * Creation of service over doors transport.
 */
extern SVCXPRT	*svc_door_create(void (*)(struct svc_req *, SVCXPRT *),
				const rpcprog_t, const rpcvers_t,
				const uint_t);
	/*
	 * 	void (*dispatch)();		-- dispatch routine
	 *	const rpcprog_t prognum;	-- program number
	 *	const rpcvers_t versnum;	-- version number
	 *	const uint_t sendsize;		-- send buffer size
	 */

/*
 * Service control interface
 */
extern	bool_t	svc_control(SVCXPRT *, const uint_t, void *);
	/*
	 *	SVCXPRT *svc;			-- service to manipulate
	 *	const uint_t req;		-- request
	 *	void *info;			-- argument to request
	 */

/*
 * svc_dg_enable_cache() enables the cache on dg transports.
 */
extern int svc_dg_enablecache(SVCXPRT *, const uint_t);
#else	/* __STDC__ */
extern int	svc_create();
extern SVCXPRT	*svc_tp_create();
extern SVCXPRT	*svc_tli_create();
extern SVCXPRT	*svc_vc_create();
extern SVCXPRT	*svc_dg_create();
extern SVCXPRT	*svc_fd_create();
extern SVCXPRT	*svc_raw_create();
extern SVCXPRT	*svc_door_create();
extern int svc_dg_enablecache();
#endif	/* __STDC__ */

extern boolean_t is_multilevel(rpcprog_t);

#ifdef	PORTMAP
/* For backward compatibility */
#include <rpc/svc_soc.h>
#endif	/* PORTMAP */

/*
 * For user level MT hot server functions
 */

/*
 * Different MT modes
 */
#define	RPC_SVC_MT_NONE		0	/* default, single-threaded */
#define	RPC_SVC_MT_AUTO		1	/* automatic MT mode */
#define	RPC_SVC_MT_USER		2	/* user MT mode */

#ifdef	__STDC__
extern void	svc_done(SVCXPRT *);
#else
extern void	svc_done();
#endif	/* __STDC__ */

/*
 * Obtaining local credentials.
 */
typedef struct __svc_local_cred_t {
	uid_t	euid;	/* effective uid */
	gid_t	egid;	/* effective gid */
	uid_t	ruid;	/* real uid */
	gid_t	rgid;	/* real gid */
	pid_t	pid;	/* caller's pid, or -1 if not available */
} svc_local_cred_t;

#ifdef __STDC__
struct ucred_s;
extern void	svc_fd_negotiate_ucred(int);
extern int	svc_getcallerucred(const SVCXPRT *, struct ucred_s **);
extern bool_t	svc_get_local_cred(SVCXPRT *, svc_local_cred_t *);
#else
extern void	svc_fd_negotiate_ucred();
extern int	svc_getcallerucred();
extern bool_t	svc_get_local_cred();
#endif	/* __STDC__ */

/*
 * Private interfaces and structures for user level duplicate request caching.
 * The interfaces and data structures are not committed and subject to
 * change in future releases. Currently only intended for use by automountd.
 */
struct dupreq {
	uint32_t	dr_xid;
	rpcproc_t	dr_proc;
	rpcvers_t	dr_vers;
	rpcprog_t	dr_prog;
	struct netbuf	dr_addr;
	struct netbuf	dr_resp;
	int		dr_status;
	time_t		dr_time;
	uint_t		dr_hash;
	struct dupreq	*dr_next;
	struct dupreq	*dr_prev;
	struct dupreq	*dr_chain;
	struct dupreq	*dr_prevchain;
};

/*
 * The fixedtime state is defined if we want to expand the routines to
 * handle and encompass fixed size caches.
 */
#define	DUPCACHE_FIXEDTIME	0

/*
 * States of requests for duplicate request caching.
 * These are the same as defined for the kernel.
 */
#define	DUP_NEW			0x00	/* new entry */
#define	DUP_INPROGRESS		0x01	/* request already going */
#define	DUP_DONE		0x02	/* request done */
#define	DUP_DROP		0x03	/* request dropped */
#define	DUP_ERROR		0x04	/* error in dup req cache */

#ifdef __STDC__
extern bool_t	__svc_dupcache_init(void *, int, char **);
extern int	__svc_dup(struct svc_req *, caddr_t *, uint_t *, char *);
extern int	__svc_dupdone(struct svc_req *, caddr_t, uint_t, int, char *);
extern bool_t	__svc_vc_dupcache_init(SVCXPRT *, void *, int);
extern int	__svc_vc_dup(struct svc_req *, caddr_t *, uint_t *);
extern int	__svc_vc_dupdone(struct svc_req *, caddr_t, uint_t, int);
#else
extern bool_t	__svc_dupcache_init();
extern int	__svc_dup();
extern int	__svc_dupdone();
extern bool_t	__svc_vc_dupcache_init();
extern int	__svc_vc_dup();
extern int	__svc_vc_dupdone();
#endif	/* __STDC__ */
#endif	/* _KERNEL */

#ifdef	_KERNEL
/*
 * Private interfaces and structures for SVCXPRT cloning.
 * The interfaces and data structures are not committed and subject to
 * change in future releases.
 */
extern SVCXPRT *svc_clone_init(void);
extern void svc_clone_free(SVCXPRT *);
extern void svc_clone_link(SVCMASTERXPRT *, SVCXPRT *, SVCXPRT *);
extern void svc_clone_unlink(SVCXPRT *);
#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* !_RPC_SVC_H */
