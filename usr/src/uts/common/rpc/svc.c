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

/*
 * Copyright 1993 OpenVision Technologies, Inc., All Rights Reserved.
 */

/*	Copyright (c) 1983, 1984, 1985,  1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*
 * Portions of this source code were derived from Berkeley 4.3 BSD
 * under license from the Regents of the University of California.
 */

/*
 * Server-side remote procedure call interface.
 *
 */

#include <sys/param.h>
#include <sys/types.h>
#include <rpc/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/tiuser.h>
#include <sys/t_kuser.h>
#include <netinet/in.h>
#include <rpc/xdr.h>
#include <rpc/auth.h>
#include <rpc/clnt.h>
#include <rpc/rpc_msg.h>
#include <rpc/svc.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/stream.h>
#include <sys/strsubr.h>
#include <sys/tihdr.h>
#include <sys/debug.h>
#include <sys/cmn_err.h>
#include <sys/file.h>
#include <sys/systm.h>
#include <sys/callb.h>
#include <sys/vtrace.h>
#include <sys/zone.h>
#include <sys/list.h>
#include <nfs/nfs.h>
#include <sys/tsol/label_macro.h>

/*
 * Default stack size for service threads.
 */
#define	DEFAULT_SVC_RUN_STKSIZE		(0)	/* default kernel stack */

#define	SVC_ASYNC_CAPABLE(xprt)		\
	(SVC_XPRT2TPOOL(xprt)->stp_flags & STP_ASYNC_REPLY)

int    svc_default_stksize = DEFAULT_SVC_RUN_STKSIZE;

/* filled in by rpcsec_gss:_init() */
void (*rpc_gss_drain_func)(void) = NULL;

/*
 * If true, then keep quiet about version mismatch.
 * This macro is for broadcast RPC only. We have no broadcast RPC in
 * kernel now but one may define a flag in the transport structure
 * and redefine this macro.
 */
#define	version_keepquiet(xprt)	(FALSE)

svc_xprt_globals_t svc_xprt;

volatile int svc_enforce_maxreqs = 0;
volatile int svc_msgsize_max = 1024 * 1024;

volatile int svc_qmsg_lowat;
volatile int svc_qmsg_hiwat;

/*
 * ZSD key used to retrieve zone-specific svc globals
 */
zone_key_t svc_zone_key;

static void svc_callout_free(SVCMASTERXPRT *);

/* ARGSUSED */
static void *
svc_zoneinit(zoneid_t zoneid)
{
	struct svc_globals *svc;

	svc = kmem_alloc(sizeof (*svc), KM_SLEEP);
	stp_init(svc);
	return (svc);
}

/* ARGSUSED */
static void
svc_zoneshutdown(zoneid_t zoneid, void *arg)
{
	struct svc_globals *svc = arg;
	stp_free_allpools(svc);
}

/* ARGSUSED */
static void
svc_zonefini(zoneid_t zoneid, void *arg)
{
	struct svc_globals *svc = arg;

	stp_destroy(svc);
	kmem_free(svc, sizeof (*svc));
}

/*
 * Global SVC init routine.
 * Initialize global generic and transport type specific structures
 * used by the kernel RPC server side. This routine is called only
 * once when the module is being loaded.
 */
void
svc_init()
{
	zone_key_create(&svc_zone_key, svc_zoneinit, svc_zoneshutdown,
	    svc_zonefini);
	mutex_init(&svc_xprt.sxg_lock, NULL, MUTEX_DEFAULT, NULL);
	svc_cots_init();
	svc_clts_init();

	/*
	 * Consume no more than 1/4 of the memory size
	 * for queued requests. Calculated with the current
	 * max msg size for the transports, to handle the
	 * worst case scenario.
	 */

	svc_qmsg_hiwat = (physmem * PAGESIZE)/(4 * svc_msgsize_max);

	/*
	 * Unblock and open up when we are 1/4th of hiwater mark.
	 * This should allow smoother transition to the clients to
	 * catch up.
	 */
	svc_qmsg_lowat = (svc_qmsg_hiwat/4);

}

void
svc_fini(void)
{
	svc_clts_fini();
	svc_cots_fini();
	mutex_destroy(&svc_xprt.sxg_lock);
	(void) zone_key_delete(svc_zone_key);
}

/*
 * See comments in stp_wait()
 */

int
svc_wait(int id)
{
	return (stp_wait(id));
}

void
svc_queuereq(queue_t *q, mblk_t *mp)
{
	(void) stp_queuereq(q, mp);
}

/*
 * The previous scheduler had a notion of reserving
 * threads for blocking calls -- the principle was to avoid
 * consuming all the threads in a pool on blocking calls.
 * The behaior is simulated for the new task pool. If
 * we are at max thread limits for the particular queue
 * we return failure.
 */
int
svc_reserve_thread(SVCXPRT *clone_xprt)
{
	int canblock;
	canblock = stp_curthread_can_block(clone_xprt);
	return (canblock);
}

/*
 * Dummy calls to provide backward compatibility
 */

/* ARGSUSED */
void
svc_unreserve_thread(SVCXPRT *clone_xprt)
{
}

/* ARGSUSED */
callb_cpr_t *
svc_detach_thread(SVCXPRT *clone_xprt)
{
	return (NULL);
}

/* ARGSUSED */
int
svc_do_run(int id)
{
	return (0);
}

/*
 * RPC async sendreply()
 */

volatile int rpc_async_sends = 1;

/*
 * The caller supplies a call back function cbfunc for cleanup of
 * args/results after the reply is sent.
 */

int
svc_async_sendreply(SVCXPRT *xprt, xdrproc_t res_proc, caddr_t res_args,
    void (*cbfunc)(void *, int), void *args)
{
	rpc_async_msg_t *rply;
	SVCTASKPOOL *tpool = SVC_XPRT2TPOOL(xprt);
	SVC_ASYNC_MSGS *xp_msgs = &(xprt->xp_master->xp_async_msgs);

	if (!rpc_async_sends || !SVC_ASYNC_CAPABLE(xprt))
		return (0);

	ASSERT(SVC_QUEUE_SENDREPLY(xprt));

	rply = kmem_cache_alloc(tpool->stp_async_msg_cache, KM_SLEEP);
	rply->ram_xprt = xprt;
	rply->ram_proc = res_proc;
	rply->ram_procargs = res_args;
	rply->ram_cbfunc = cbfunc;
	rply->ram_cbargs = args;
	mutex_enter(&xp_msgs->msg_lock);

	list_insert_tail(&xp_msgs->msg_list, rply);

	/* async dispatch */
	if (xp_msgs->msg_indrain) {
		mutex_exit(&xp_msgs->msg_lock);
		return (1);
	}

	xp_msgs->msg_indrain = 1;
	mutex_exit(&xp_msgs->msg_lock);

	stp_async_reply((void *)rply);

	return (1);
}

void
svc_xprt_free(SVCXPRT *xprt)
{

	/* release the hold on the transport queue */
	(*RELE_PROC(xprt)) (xprt->xp_wq, NULL);

	svc_clone_unlink(xprt);

	/*
	 * structure overloading: the xprt is actually
	 * the first element of svc_task_t strcuture.
	 */

	stp_task_free((svc_task_t *)xprt);

	/*
	 * The cred (xp_cred) is normally cached and
	 * freed only by the kmem stp_task_destr()
	 * destructor.
	 */
}

/*
 * PSARC 2003/523 Contract Private Interface
 * svc_pool_create
 * Changes must be reviewed by Solaris File Sharing
 * Changes must be communicated to contract-2003-523@sun.com
 *
 * Create an kernel RPC server-side thread/transport pool.
 *
 * This is public interface for creation of a server RPC thread pool
 * for a given service provider. Transports registered with the pool's id
 * will be served by a pool's threads. This function is called from the
 * nfssys() system call.
 */
int
svc_pool_create(struct svcpool_args *args)
{
	return (stp_pool_alloc(args->id, args->maxthreads));
}

int
svc_pool_control(int id, int cmd, void (*cb_fn)(void *), void *cb_fn_arg)
{
	int err;
	err = stp_pool_control(id, cmd, cb_fn, cb_fn_arg);
	return (err);
}

/*
 * Destructor for a master server transport handle.
 */
/* ARGSUSED */
static void
svc_xprt_cleanup(SVCMASTERXPRT *xprt, bool_t detached)
{
	ASSERT(MUTEX_HELD(&xprt->xp_ref_lock));
	ASSERT(xprt->xp_wq == NULL);

	/*
	 * If called for the last reference
	 * call the closeproc for this transport.
	 */
	if (xprt->xp_ref == 0 && xprt->xp_closeproc) {
		(*(xprt->xp_closeproc)) (xprt);
	}

	if (xprt->xp_ref > 0) {
		mutex_exit(&xprt->xp_ref_lock);
		return;
	}
	mutex_exit(&xprt->xp_ref_lock);
	stp_xprt_unregister(xprt);
	svc_callout_free(xprt);
	SVC_DESTROY(xprt);
}

/*
 * Find a dispatch routine for a given prog/vers pair.
 * This function is called from svc_getreq() to search the callout
 * table for an entry with a matching RPC program number `prog'
 * and a version range that covers `vers'.
 * - if it finds a matching entry it returns pointer to the dispatch routine
 * - otherwise it returns NULL and, if `minp' or `maxp' are not NULL,
 *   fills them with, respectively, lowest version and highest version
 *   supported for the program `prog'
 */
static SVC_DISPATCH *
svc_callout_find(SVCXPRT *xprt, rpcprog_t prog, rpcvers_t vers,
    rpcvers_t *vers_min, rpcvers_t *vers_max)
{
	SVC_CALLOUT_TABLE *sct = xprt->xp_sct;
	int i;

	*vers_min = ~(rpcvers_t)0;
	*vers_max = 0;

	for (i = 0; i < sct->sct_size; i++) {
		SVC_CALLOUT *sc = &sct->sct_sc[i];

		if (prog == sc->sc_prog) {
			if (vers >= sc->sc_versmin && vers <= sc->sc_versmax)
				return (sc->sc_dispatch);

			if (*vers_max < sc->sc_versmax)
				*vers_max = sc->sc_versmax;
			if (*vers_min > sc->sc_versmin)
				*vers_min = sc->sc_versmin;
		}
	}

	return (NULL);
}

/*
 * Optionally free callout table allocated for this transport by
 * the service provider.
 */
static void
svc_callout_free(SVCMASTERXPRT *xprt)
{
	SVC_CALLOUT_TABLE *sct = xprt->xp_sct;

	if (sct->sct_free) {
		kmem_free(sct->sct_sc, sct->sct_size * sizeof (SVC_CALLOUT));
		kmem_free(sct, sizeof (SVC_CALLOUT_TABLE));
	}
}

/*
 * Send a reply to an RPC request
 *
 * PSARC 2003/523 Contract Private Interface
 * svc_sendreply
 * Changes must be reviewed by Solaris File Sharing
 * Changes must be communicated to contract-2003-523@sun.com
 */
bool_t
svc_sendreply(const SVCXPRT *clone_xprt, const xdrproc_t xdr_results,
    const caddr_t xdr_location)
{
	struct rpc_msg rply;

	rply.rm_direction = REPLY;
	rply.rm_reply.rp_stat = MSG_ACCEPTED;
	rply.acpted_rply.ar_verf = clone_xprt->xp_verf;
	rply.acpted_rply.ar_stat = SUCCESS;
	rply.acpted_rply.ar_results.where = xdr_location;
	rply.acpted_rply.ar_results.proc = xdr_results;

	return (SVC_REPLY((SVCXPRT *)clone_xprt, &rply));
}

/*
 * No procedure error reply
 *
 * PSARC 2003/523 Contract Private Interface
 * svcerr_noproc
 * Changes must be reviewed by Solaris File Sharing
 * Changes must be communicated to contract-2003-523@sun.com
 */
void
svcerr_noproc(const SVCXPRT *clone_xprt)
{
	struct rpc_msg rply;

	rply.rm_direction = REPLY;
	rply.rm_reply.rp_stat = MSG_ACCEPTED;
	rply.acpted_rply.ar_verf = clone_xprt->xp_verf;
	rply.acpted_rply.ar_stat = PROC_UNAVAIL;
	SVC_FREERES((SVCXPRT *)clone_xprt);
	SVC_REPLY((SVCXPRT *)clone_xprt, &rply);
}

/*
 * Can't decode arguments error reply
 *
 * PSARC 2003/523 Contract Private Interface
 * svcerr_decode
 * Changes must be reviewed by Solaris File Sharing
 * Changes must be communicated to contract-2003-523@sun.com
 */
void
svcerr_decode(const SVCXPRT *clone_xprt)
{
	struct rpc_msg rply;

	rply.rm_direction = REPLY;
	rply.rm_reply.rp_stat = MSG_ACCEPTED;
	rply.acpted_rply.ar_verf = clone_xprt->xp_verf;
	rply.acpted_rply.ar_stat = GARBAGE_ARGS;
	SVC_FREERES((SVCXPRT *)clone_xprt);
	SVC_REPLY((SVCXPRT *)clone_xprt, &rply);
}

/*
 * Some system error
 */
void
svcerr_systemerr(const SVCXPRT *clone_xprt)
{
	struct rpc_msg rply;

	rply.rm_direction = REPLY;
	rply.rm_reply.rp_stat = MSG_ACCEPTED;
	rply.acpted_rply.ar_verf = clone_xprt->xp_verf;
	rply.acpted_rply.ar_stat = SYSTEM_ERR;
	SVC_FREERES((SVCXPRT *)clone_xprt);
	SVC_REPLY((SVCXPRT *)clone_xprt, &rply);
}

/*
 * Authentication error reply
 */
void
svcerr_auth(const SVCXPRT *clone_xprt, const enum auth_stat why)
{
	struct rpc_msg rply;

	rply.rm_direction = REPLY;
	rply.rm_reply.rp_stat = MSG_DENIED;
	rply.rjcted_rply.rj_stat = AUTH_ERROR;
	rply.rjcted_rply.rj_why = why;
	SVC_FREERES((SVCXPRT *)clone_xprt);
	SVC_REPLY((SVCXPRT *)clone_xprt, &rply);
}

/*
 * Authentication too weak error reply
 */
void
svcerr_weakauth(const SVCXPRT *clone_xprt)
{
	svcerr_auth((SVCXPRT *)clone_xprt, AUTH_TOOWEAK);
}

/*
 * Authentication error; bad credentials
 */
void
svcerr_badcred(const SVCXPRT *clone_xprt)
{
	struct rpc_msg rply;

	rply.rm_direction = REPLY;
	rply.rm_reply.rp_stat = MSG_DENIED;
	rply.rjcted_rply.rj_stat = AUTH_ERROR;
	rply.rjcted_rply.rj_why = AUTH_BADCRED;
	SVC_FREERES((SVCXPRT *)clone_xprt);
	SVC_REPLY((SVCXPRT *)clone_xprt, &rply);
}

/*
 * Program unavailable error reply
 *
 * PSARC 2003/523 Contract Private Interface
 * svcerr_noprog
 * Changes must be reviewed by Solaris File Sharing
 * Changes must be communicated to contract-2003-523@sun.com
 */
void
svcerr_noprog(const SVCXPRT *clone_xprt)
{
	struct rpc_msg rply;

	rply.rm_direction = REPLY;
	rply.rm_reply.rp_stat = MSG_ACCEPTED;
	rply.acpted_rply.ar_verf = clone_xprt->xp_verf;
	rply.acpted_rply.ar_stat = PROG_UNAVAIL;
	SVC_FREERES((SVCXPRT *)clone_xprt);
	SVC_REPLY((SVCXPRT *)clone_xprt, &rply);
}

/*
 * Program version mismatch error reply
 *
 * PSARC 2003/523 Contract Private Interface
 * svcerr_progvers
 * Changes must be reviewed by Solaris File Sharing
 * Changes must be communicated to contract-2003-523@sun.com
 */
void
svcerr_progvers(const SVCXPRT *clone_xprt,
    const rpcvers_t low_vers, const rpcvers_t high_vers)
{
	struct rpc_msg rply;

	rply.rm_direction = REPLY;
	rply.rm_reply.rp_stat = MSG_ACCEPTED;
	rply.acpted_rply.ar_verf = clone_xprt->xp_verf;
	rply.acpted_rply.ar_stat = PROG_MISMATCH;
	rply.acpted_rply.ar_vers.low = low_vers;
	rply.acpted_rply.ar_vers.high = high_vers;
	SVC_FREERES((SVCXPRT *)clone_xprt);
	SVC_REPLY((SVCXPRT *)clone_xprt, &rply);
}

/*
 * Statement of authentication parameters management:
 * This function owns and manages all authentication parameters, specifically
 * the "raw" parameters (msg.rm_call.cb_cred and msg.rm_call.cb_verf) and
 * the "cooked" credentials (rqst->rq_clntcred).
 * However, this function does not know the structure of the cooked
 * credentials, so it make the following assumptions:
 *   a) the structure is contiguous (no pointers), and
 *   b) the cred structure size does not exceed RQCRED_SIZE bytes.
 * In all events, all three parameters are freed upon exit from this routine.
 * The storage is trivially managed on the call stack in user land, but
 * is malloced in kernel land.
 */

void
svc_process_request(SVCXPRT *clone_xprt, mblk_t *mp, char *cred_area)
{
	struct rpc_msg msg;
	struct svc_req r;
	int async_reply = 0;
	int gss_cleanup = 0;


	msg.rm_call.cb_cred.oa_base = cred_area;
	msg.rm_call.cb_verf.oa_base = &(cred_area[MAX_AUTH_BYTES]);
	r.rq_clntcred = &(cred_area[2 * MAX_AUTH_BYTES]);

	/*
	 * underlying transport recv routine may modify mblk data
	 * and make it difficult to extract label afterwards. So
	 * get the label from the raw mblk data now.
	 */
	if (is_system_labeled()) {
		cred_t *cr;

		r.rq_label = kmem_alloc(sizeof (bslabel_t), KM_SLEEP);
		cr = msg_getcred(mp, NULL);
		ASSERT(cr != NULL);

		bcopy(label2bslabel(crgetlabel(cr)), r.rq_label,
		    sizeof (bslabel_t));
	} else {
		r.rq_label = NULL;
	}

	/*
	 * Now receive a message from the transport.
	 */
	if (SVC_RECV(clone_xprt, mp, &msg)) {
		void (*dispatchroutine) (struct svc_req *, SVCXPRT *);
		rpcvers_t vers_min;
		rpcvers_t vers_max;
		bool_t no_dispatch;
		enum auth_stat why;

		/*
		 * Find the registered program and call its
		 * dispatch routine.
		 */
		r.rq_xprt = clone_xprt;
		r.rq_prog = msg.rm_call.cb_prog;
		r.rq_vers = msg.rm_call.cb_vers;
		r.rq_proc = msg.rm_call.cb_proc;
		r.rq_cred = msg.rm_call.cb_cred;

		/*
		 * First authenticate the message.
		 */
		TRACE_0(TR_FAC_KRPC, TR_SVC_GETREQ_AUTH_START,
		    "svc_getreq_auth_start:");
		if ((why = sec_svc_msg(&r, &msg, &no_dispatch)) != AUTH_OK) {
			TRACE_1(TR_FAC_KRPC, TR_SVC_GETREQ_AUTH_END,
			    "svc_getreq_auth_end:(%S)", "failed");
			svcerr_auth(clone_xprt, why);
			/*
			 * Free the arguments.
			 */
			(void) SVC_FREEARGS(clone_xprt, NULL, NULL);
		} else if (no_dispatch) {
			(void) SVC_FREEARGS(clone_xprt, NULL, NULL);
		} else {
			TRACE_1(TR_FAC_KRPC, TR_SVC_GETREQ_AUTH_END,
			    "svc_getreq_auth_end:(%S)", "good");

			dispatchroutine = svc_callout_find(clone_xprt,
			    r.rq_prog, r.rq_vers, &vers_min, &vers_max);

			if (dispatchroutine) {

				if (SVC_ASYNC_CAPABLE(clone_xprt))
					async_reply = 1;

				(*dispatchroutine) (&r, clone_xprt);

			} else {
				/*
				 * If we got here, the program or version
				 * is not served ...
				 */
				if (vers_max == 0 ||
				    version_keepquiet(clone_xprt))
					svcerr_noprog(clone_xprt);
				else
					svcerr_progvers(clone_xprt, vers_min,
					    vers_max);

				/*
				 * Free the arguments. For successful calls
				 * this is done by the dispatch routine.
				 */
				(void) SVC_FREEARGS(clone_xprt, NULL, NULL);
				/* Fall through to ... */
			}

			/*
			 * Call cleanup procedure for RPCSEC_GSS.
			 * This is a hack since there is currently no
			 * op, such as SVC_CLEANAUTH. rpc_gss_cleanup
			 * should only be called for a non null proc.
			 * Null procs in RPC GSS are overloaded to
			 * provide context setup and control. The main
			 * purpose of rpc_gss_cleanup is to decrement the
			 * reference count associated with the cached
			 * GSS security context. We should never get here
			 * for an RPCSEC_GSS null proc since *no_dispatch
			 * would have been set to true from sec_svc_msg above.
			 */
			if (r.rq_cred.oa_flavor == RPCSEC_GSS)
				gss_cleanup = 1;
		}
	}

	/*
	 * clone_xprt released asynchronously
	 * for async reply capable services
	 */

	if (async_reply)
		return;

	if (gss_cleanup)
		rpc_gss_cleanup(clone_xprt);

	svc_xprt_free(clone_xprt);

	if (r.rq_label != NULL)
		kmem_free(r.rq_label, sizeof (bslabel_t));
}

/*
 * Allocate new clone transport handle.
 */
SVCXPRT *
svc_clone_init(void)
{
	SVCXPRT *clone_xprt;

	clone_xprt = kmem_zalloc(sizeof (SVCXPRT), KM_SLEEP);
	clone_xprt->xp_cred = crget();
	return (clone_xprt);
}

/*
 * Free memory allocated by svc_clone_init.
 */
void
svc_clone_free(SVCXPRT *clone_xprt)
{
	/* Fre credentials from crget() */
	if (clone_xprt->xp_cred)
		crfree(clone_xprt->xp_cred);
	kmem_free(clone_xprt, sizeof (SVCXPRT));
}

/*
 * Link a per-thread clone transport handle to a master
 * - increment a thread reference count on the master
 * - copy some of the master's fields to the clone
 * - call a transport specific clone routine.
 */
void
svc_clone_link(SVCMASTERXPRT *xprt, SVCXPRT *clone_xprt, SVCXPRT *clone_xprt2)
{
	cred_t *cred = clone_xprt->xp_cred;

	ASSERT(cred);

	/*
	 * Bump up master's thread count.
	 * Linking a per-thread clone transport handle to a master
	 * associates a service thread with the master.
	 */
	mutex_enter(&xprt->xp_ref_lock);
	xprt->xp_ref++;
	mutex_exit(&xprt->xp_ref_lock);

	/* Clear everything */
	bzero(clone_xprt, sizeof (SVCXPRT));

	/* Set pointer to the master transport stucture */
	clone_xprt->xp_master = xprt;

	/* Structure copy of all the common fields */
	clone_xprt->xp_xpc = xprt->xp_xpc;

	/* Restore per-thread fields (xp_cred) */
	clone_xprt->xp_cred = cred;

	if (clone_xprt2)
		SVC_CLONE_XPRT(clone_xprt2, clone_xprt);
}

/*
 * Unlink a non-detached clone transport handle from a master
 * - decrement a thread reference count on the master
 * - if the transport is closing (xp_wq is NULL) call svc_xprt_cleanup();
 *   if this is the last non-detached/absolute thread on this transport
 *   then it will close/destroy the transport
 * - call transport specific function to destroy the clone handle
 * - clear xp_master to avoid recursion.
 */
void
svc_clone_unlink(SVCXPRT *clone_xprt)
{
	SVCMASTERXPRT *xprt = clone_xprt->xp_master;

	ASSERT(xprt->xp_ref > 0);

	/* Decrement a reference count on the transport */
	mutex_enter(&xprt->xp_ref_lock);
	xprt->xp_ref--;

	/* svc_xprt_cleanup() destroys xprt */
	if (xprt->xp_wq)
		mutex_exit(&xprt->xp_ref_lock);
	else
		svc_xprt_cleanup(xprt, FALSE);

	/* Call a transport specific clone `destroy' function */
	SVC_CLONE_DESTROY(clone_xprt);

	/* Clear xp_master */
	clone_xprt->xp_master = NULL;
}

/*
 * This routine is called by rpcmod to inform kernel RPC that a
 * queue is closing. It is also guaranteed that no more
 * request will be delivered on this transport.
 *
 * - clear xp_wq to mark the master server transport handle as closing
 * - if there are no more threads on this transport close/destroy it
 * - otherwise, the last thread referencing this will
 *   close/destroy the transport.
 */
void
svc_queueclose(queue_t *q)
{
	SVCMASTERXPRT *xprt = ((void **) q->q_ptr)[0];

	/* wait for any gss tasks that might use the queue to exit */
	if (rpc_gss_drain_func)
		rpc_gss_drain_func();

	if (xprt == NULL) {
		/*
		 * If there is no master xprt associated with this stream,
		 * then there is nothing to do.  This happens regularly
		 * with connection-oriented listening streams created by
		 * nfsd.
		 */
		return;
	}

	mutex_enter(&xprt->xp_ref_lock);

	ASSERT(xprt->xp_wq != NULL);

	xprt->xp_wq = NULL;

	if (xprt->xp_ref != 0) {
		mutex_exit(&xprt->xp_ref_lock);
		return;
	}

	/*
	 * svc_xprt_cleanup() destroys the transport
	 * or releases the transport thread lock
	 */

	svc_xprt_cleanup(xprt, FALSE);
	/*
	 * From here on the xp_wq stream will be destroyed by
	 * the caller. If there are any references the master
	 * xprt will be freed on the last clone unlink.
	 */
}

/*
 * Let the users of the clone know that the queue is closed.
 * This is called from rpcmod-streams when the queue is
 * going away but still has references to it.
 */

void
svc_queueclean(queue_t *q)
{
	SVCMASTERXPRT *mxprt = ((void **) q->q_ptr)[0];
	mutex_enter(&mxprt->xp_ref_lock);
	mxprt->xp_flags |= XPRT_QUEUE_CLOSED;
	mutex_exit(&mxprt->xp_ref_lock);
}

/*
 * Ease flow control
 * Transport specific callbacks do the work
 * of clearing any queued messages and unblocking the
 * transport connnections.
 */
void
svc_unblock(SVCMASTERXPRT *mxprt)
{

	mutex_enter(&mxprt->xp_ref_lock);
	mxprt->xp_flags &= ~XPRT_FLOWCTRL_ON;
	mutex_exit(&mxprt->xp_ref_lock);

	switch (mxprt->xp_type) {
	case T_RDMA:
		rdma_svc_unblock(mxprt);
		break;
	case T_CLTS:
		return;
	default:
		/* cots or cots-ord */
		mir_unblock(mxprt->xp_wq);
	}
}

/*
 * This routine is responsible for extracting RDMA plugin master XPRT,
 * unregister from the SVCPOOL and initiate plugin specific cleanup.
 * It is passed a list/group of rdma transports as records which are
 * active in a given registered or unregistered kRPC thread pool. Its shuts
 * all active rdma transports in that pool. If the thread active on the trasport
 * happens to be last thread for that pool, it will signal the creater thread
 * to cleanup the pool and destroy the xprt in svc_queueclose()
 */
void
rdma_stop(rdma_xprt_group_t *rdma_xprts)
{
	SVCMASTERXPRT *xprt;
	rdma_xprt_record_t *curr_rec;
	queue_t *q;
	int i, rtg_count;

	if (rdma_xprts->rtg_count == 0)
		return;

	rtg_count = rdma_xprts->rtg_count;

	for (i = 0; i < rtg_count; i++) {
		curr_rec = rdma_xprts->rtg_listhead;
		rdma_xprts->rtg_listhead = curr_rec->rtr_next;
		rdma_xprts->rtg_count--;
		curr_rec->rtr_next = NULL;
		xprt = curr_rec->rtr_xprt_ptr;
		q = xprt->xp_wq;

		svc_queueclean(q);

		/*
		 * Waits in rpcib for closure of all
		 * connections.
		 */
		svc_rdma_kstop(xprt);

		svc_queueclose(q);

		/*
		 * Free the rdma transport record for the expunged rdma
		 * based master transport handle.
		 */
		kmem_free(curr_rec, sizeof (rdma_xprt_record_t));
		if (!rdma_xprts->rtg_listhead)
			break;
	}
}

/*
 * rpc_msg_dup/rpc_msg_free
 * Currently only used by svc_rpcsec_gss.c but put in this file as it
 * may be useful to others in the future.
 * But future consumers should be careful cuz so far
 *   - only tested/used for call msgs (not reply)
 *   - only tested/used with call verf oa_length==0
 */
struct rpc_msg *
rpc_msg_dup(struct rpc_msg *src)
{
	struct rpc_msg *dst;
	struct opaque_auth oa_src, oa_dst;

	dst = kmem_alloc(sizeof (*dst), KM_SLEEP);

	dst->rm_xid = src->rm_xid;
	dst->rm_direction = src->rm_direction;

	dst->rm_call.cb_rpcvers = src->rm_call.cb_rpcvers;
	dst->rm_call.cb_prog = src->rm_call.cb_prog;
	dst->rm_call.cb_vers = src->rm_call.cb_vers;
	dst->rm_call.cb_proc = src->rm_call.cb_proc;

	/* dup opaque auth call body cred */
	oa_src = src->rm_call.cb_cred;

	oa_dst.oa_flavor = oa_src.oa_flavor;
	oa_dst.oa_base = kmem_alloc(oa_src.oa_length, KM_SLEEP);

	bcopy(oa_src.oa_base, oa_dst.oa_base, oa_src.oa_length);
	oa_dst.oa_length = oa_src.oa_length;

	dst->rm_call.cb_cred = oa_dst;

	/* dup or just alloc opaque auth call body verifier */
	if (src->rm_call.cb_verf.oa_length > 0) {
		oa_src = src->rm_call.cb_verf;

		oa_dst.oa_flavor = oa_src.oa_flavor;
		oa_dst.oa_base = kmem_alloc(oa_src.oa_length, KM_SLEEP);

		bcopy(oa_src.oa_base, oa_dst.oa_base, oa_src.oa_length);
		oa_dst.oa_length = oa_src.oa_length;

		dst->rm_call.cb_verf = oa_dst;
	} else {
		oa_dst.oa_flavor = -1;  /* will be set later */
		oa_dst.oa_base = kmem_alloc(MAX_AUTH_BYTES, KM_SLEEP);

		oa_dst.oa_length = 0;   /* will be set later */

		dst->rm_call.cb_verf = oa_dst;
	}
	return (dst);

error:
	kmem_free(dst->rm_call.cb_cred.oa_base,	dst->rm_call.cb_cred.oa_length);
	kmem_free(dst, sizeof (*dst));
	return (NULL);
}

void
rpc_msg_free(struct rpc_msg **msg, int cb_verf_oa_length)
{
	struct rpc_msg *m = *msg;

	kmem_free(m->rm_call.cb_cred.oa_base, m->rm_call.cb_cred.oa_length);
	m->rm_call.cb_cred.oa_base = NULL;
	m->rm_call.cb_cred.oa_length = 0;

	kmem_free(m->rm_call.cb_verf.oa_base, cb_verf_oa_length);
	m->rm_call.cb_verf.oa_base = NULL;
	m->rm_call.cb_verf.oa_length = 0;

	kmem_free(m, sizeof (*m));
	m = NULL;
}

void
svc_purge_dupcache(drc_globals_t *drc)
{
	int i;
	struct dupreq *dr, *drnext;

	ASSERT(MUTEX_HELD(&drc->dg_lock));
	for (i = 0; i < DRHASHSZ; i++) {
		dr = drc->dg_hashtbl[i];
		while (dr != NULL) {
			drnext = dr->dr_chain;
			if (dr->dr_resfree != NULL)
				(*dr->dr_resfree)(dr->dr_resp.buf);
			if (dr->dr_resp.buf != NULL)
				kmem_free(dr->dr_resp.buf, dr->dr_resp.maxlen);
			if (dr->dr_addr.buf != NULL)
				kmem_free(dr->dr_addr.buf, dr->dr_addr.maxlen);
			kmem_free(dr, sizeof (*dr));
			drc->dg_numdrq--;
			dr = drnext;
		}
	}
	ASSERT(drc->dg_numdrq == 0);
}
