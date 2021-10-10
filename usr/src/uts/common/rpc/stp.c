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

/*
 * RPC server task pools: RPC request processing facility used by
 * kernel RPC services.
 *
 * High level schematic representation:
 *
 *   svc_globals
 *   -------------,
 *  " sg_stp_list "
 *  `-------------"
 *         |
 *         |    SVCTASKPOOL stp_id = 1             stp_id = 2
 *         |   +-=====================+ stp_next +-===================+
 *         `-->:                      : -------> :                    :
 *             | svc_tqueue_t         |          |                    |
 *             : +---++---++---++---+ :          :+---++---++---++---+:
 *             | |---||---||---||---| |          ||   ||   ||   ||   ||
 *             +=+---++---++---++---+=+          ++---++---++---++---++
 *               |---||---||---||---|             |   ||   ||   ||   |
 *         ,---> |---||---||---||---|             |   ||   ||   ||   |
 *        /      +---++---++---++---+             +---++-+-++-+-++-+-+
 *       /           \  \   | |   |                  \   |  |  |
 * stp_queuereq()     \  |  | |  / taskq_dispatch()   \  |  |  |
 *                     `.|  | /.'                      . |  |  /
 *             +````````vv''vv`v``````````+     +``````v`v``v``v```````````+
 *    Dynamic  : +--+ +--+ +--+ +--+ +--+ :     : +--+ +--+ +--+ +--+ +--+ :
 *     taskq   : |  | |  | |  | |  | |  | :     : |  | |  | |  | |  | |  | :
 *             : |  | |  | |  | |  | |  | :     : |  | |  | |  | |  | |  | :
 *             +.+..+.+..+.+..+.+..+.+..+.+     +.+..+.+..+.+..+.+..+.+..+.+
 *                ||  |||   ||   ||                ||   |||  |||  ||
 *        taskq   \\  \\\   \\   \\                \\   \\\  \\\  \\
 *        threads  ||  |||   ||   ||                ||   |||  |||  ||
 *
 * Implementation:
 * A server task pool is composed of multiple request work queues.
 * Each task pool utilizes a dynamic Taskq pool which provides the worker
 * threads for processing of requests. Incoming requests are first queued
 * on one the queues before being dispatched to the associated taskq for
 * the pool. Each taskq dispatch can process multiple requests in it's
 * working queue per invocation. A Dynamic Taskq pool internally, is
 * composed of multiple buckets to which the worker threads are tied to.
 * There is no binding between the taskq buckets and the RPC task pool's
 * request queues.
 *
 * A RPC request is represented as a service task which is encapsulated in
 * a 'svc_task_t' structure along with the request credentials and the
 * clone SVCXPRT transport handle. The task is not bound to a particular
 * thread and can be passed to other threads for asynchronous processing.
 *
 * When a user-level daemon creates a kernel RPC service pool, each pool
 * is associated with a new kernel/system proc. The taskq for the pool
 * is created with this proc and all the worker threads are associated
 * with it. The link between the userland daemon and the kernel service
 * is maintained through the use of svc_wait() nfssys call by the daemon
 * and this waits until signaled for service closure. The daemon registers
 * all master transport with the pool and each SVCMASTERXPRT holds a
 * reference for the pool. A reference is not held for individual messages.
 *
 * Dynamic dispatch throttling:
 * At higher IOPs a taskq_dispatch() per RPC request exhibits very high
 * context-switches/csw count. Even with requests queued at RPC or taskq
 * level and with each taskq thread processing more than one request per
 * invocation. So a method to throttle the number of taskq dispatch at
 * ingress is required. Such a throttle also provides the active running
 * threads a better queue of requests to work with, improving efficiency.
 * 'stq_csw_threshold' determines the cutoff for enabling dispatch
 * throttling.
 *
 * In an ideal scenario with n-CPUs request queues, a single thread per
 * queue (a throttle value of one) is enough to work through all the
 * queued requests as long as this thread does not block during the
 * processing of a request. Processing a RPC request however requires
 * transitions through several modules and the thread may block in
 * any module. Once the request processing transitions to NFS layer,
 * the RPC server has no control or knowledge of a blocking thread and
 * more than one thread is needed for maximum performance.
 *
 * Each RPC queue starts off with a base throttle value of 'stp_base_throttle'.
 * This throttle is then dynamically tuned based on the request workload.
 * A sustained blocking workload automatically tunes the throttle up higher.
 * When dispatch throttling is enabled at ingress point stp_queuereq()
 * chooses between doing a taskq_dispatch() or the Dynamic Taskq pool or
 * to just queue the message without a dispatch and allow an active thread
 * working on the queue to process the request.
 *
 * Queue monitoring:
 * A queue monitor 'stp_queue_monitor()' runs periodically at preset
 * intervals. This interval is 'stp_queue_monitor_timeout' when throttling
 * is not enabled and 'stp_csw_monitor_timeout' when throttling is enabled.
 * The queue monitor performs these tasks:
 *  - watch for blocked threads
 *  - watch for requests with missed deadlines
 *  - watch for idle threads
 *
 * The queue monitor dispatches new threads to handle the queued requests
 * if it detects blocked threads or requests past the deadline. If this happens
 * for more than 'stp_active_underrun' threshold number of consecutive monitored
 * intervals, the queue throttle is incremented. A queue is idle if the number
 * of active threads is lower than the current queue throttle. The queue
 * throttle is decremented if it is idle for 'stp_idle_threshold' number of
 * consecutive intervals.
 *
 * Estimating blocked threads through "rollcall" accounting:
 * Each queue maintains a rollcall counter for the active threads for
 * the queue. Active threads increment the rollcall counter as they loop
 * through the requests. The rollcall counter is reset by the queue monitor
 * every monitored interval. Threads are presumed to be blocked if the
 * rollcall counter is less than the number of active threads.
 *
 * Temporary throttle increases for async sendreply()'s:
 * Multiple threads sending responses over the same TCP connection are
 * serialized in 'squeues' at the TCP/IP layer. The thread entering the
 * squeue first blocks all other threads. As the context of the send thread
 * may be utilized by the squeue to drain incoming requests, this delay may
 * be as long as 20ms. Instead of multiple threads blocking on the squeue,
 * if the RPC layer detects that a thread is active in putmsg() for a
 * particular transport connection, all other threads queue their
 * responses to be drained by the active thread sending the response.
 *
 * While this is efficient than leaving multiple threads blocked, it may
 * starve the home queue of this active thread which is delayed draining
 * responses from other queues introducing latency bubbles. To prevent this,
 * the thread pro-actively increments the throttle for its home queue before
 * starting the drain. The throttle is decremented on completion.
 *
 * Dynamic Flow control:
 * Flow control is currently implemented as a defensive mechanism against
 * excessive use of system memory by qeueued messages. A flow control monitor
 * runs at 'stp_flowctrl_timeout' monitoring the queued messages per pool,
 * comparing against the high and low water mark for the pool ('svc_qmsg_hiwat'
 * and 'svc_qmsg_lowat'). stp_queuereq() fails when flow control is enabled.
 * This triggers the underlying transport modules to flow control the
 * associated connections. A list of flow-controlled master transport handles
 * is maintened per pool, which is used to call the transport specific callback
 * routines to unblock, when the flow control is disabled.
 *
 */
#include <sys/param.h>
#include <sys/types.h>
#include <rpc/types.h>
#include <rpc/svc.h>
#include <sys/proc.h>
#include <sys/stream.h>
#include <sys/strsubr.h>
#include <sys/cmn_err.h>
#include <sys/sysmacros.h>
#include <sys/list.h>
#include <sys/zone.h>
#include <sys/taskq.h>
#include <sys/kmem.h>
#include <sys/kstat.h>
#include <sys/atomic.h>
#include <sys/ddi.h>
#include <sys/cpuvar.h>
#include <sys/sdt.h>

extern pri_t minclsyspri;

static struct stp_kstats {
	kstat_named_t stp_dispatch_failed;
	kstat_named_t stp_active_threads;
	kstat_named_t stp_msgs_queued;
	kstat_named_t stp_csw_control;
	kstat_named_t stp_flow_control;
	kstat_named_t stp_throttle;
} stp_kstats = {
	{"dispatch_failed", KSTAT_DATA_UINT64},
	{"active_threads", KSTAT_DATA_UINT64},
	{"requests_inq", KSTAT_DATA_UINT64},
	{"csw_control", KSTAT_DATA_UINT64},
	{"flow_control", KSTAT_DATA_UINT64},
	{"avg_throttle", KSTAT_DATA_UINT64},
};

/* csw control */
#define	STP_IDLE_THREADS(stqp) (stqp->stq_active < stqp->stq_throttle)

#define	STP_DECR_ACTIVE_THR(stqp) {	\
	stqp->stq_active--;		\
	if (stqp->stq_rollcall)		\
		stqp->stq_rollcall--;	\
}

#define	STP_INCR_ACTIVE_THR(stqp) {	\
	stqp->stq_active++;		\
	stqp->stq_rollcall++;		\
}

#define	STP_ACTIVE_THR_UNDER_MAX(stqp) (stqp->stq_active < stqp->stq_maxthreads)


#define	STP_RESET_CSWCNTS(stqp) {	\
	stqp->stq_over_cswl = 0;	\
	stqp->stq_under_cswl = 0;	\
}

#define	STP_ENABLE_FLAG(tpool, flag) {		\
	DTRACE_PROBE2(krpc__i__stp_enable,	\
	    SVCTASKPOOL *, tpool, int, flag);	\
	mutex_enter(&tpool->stp_mtx);		\
	tpool->stp_flags |= flag;		\
	mutex_exit(&tpool->stp_mtx);		\
}

#define	STP_DISABLE_FLAG(tpool, flag) {		\
	DTRACE_PROBE2(krpc__i__stp_disable,	\
	    SVCTASKPOOL *, tpool, int, flag);	\
	mutex_enter(&tpool->stp_mtx);		\
	tpool->stp_flags &= ~flag;		\
	mutex_exit(&tpool->stp_mtx);		\
}

#define	STP_ENABLE_CSWCTRL(stqp)	\
    STP_ENABLE_FLAG(stqp->stq_tpool, STP_CSWCTRL_ON)

#define	STP_DISABLE_CSWCTRL(stqp)	\
    STP_DISABLE_FLAG(stqp->stq_tpool, STP_CSWCTRL_ON)

#define	STP_CHECK_FLOWCTRL(tpool)	\
	((tpool->stp_flags & STP_FLOWCTRL_ON) ? 1 : 0)

#define	STP_CHECK_CSWCTRL(stqp)		\
	((stqp->stq_tpool->stp_flags & STP_CSWCTRL_ON) ? 1 : 0)

extern zone_key_t svc_zone_key;
extern int svc_qmsg_hiwat;
extern int svc_qmsg_lowat;
extern int svc_enforce_maxreqs;
extern size_t strlcpy(char *dst, const char *src, size_t dstsize);

void stp_task_free(svc_task_t *);

static void stp_proc(void *);
static int stp_can_dispatch(svc_tqueue_t *);
static void stp_dispatch(void *);
static void stp_retry_dispatch(void *);
static void stp_msg_free(svc_task_t *, SVCMASTERXPRT *);
static int stp_taskq_dispatch(svc_tqueue_t *);
static void stp_pool_close(SVCTASKPOOL *);
static void stp_pool_destroy(SVCTASKPOOL *);
static void stp_task_destr(void *, void *);
static int stp_task_constr(void *, void *, int);
static int stp_kstat_update(kstat_t *, int);

static void stp_queue_monitor(void *);
static int stp_incr_active(svc_tqueue_t *, int);
static void stp_decr_throttle(svc_tqueue_t *);
static int stp_deadline_check(svc_tqueue_t *);
static void stp_flow_control(void *arg);
static void stp_disable_flowctrl(SVCTASKPOOL *);
static void stp_enable_flowctrl(SVCTASKPOOL *);
static void stp_flowctrl_insert(SVCMASTERXPRT *);
static void stp_free_flowctrl(SVCTASKPOOL *);

/*
 * taskq parameters
 */
#define	DEFAULT_STP_MAXTHREADS 1024
static int stp_default_maxthreads = DEFAULT_STP_MAXTHREADS;

#define	STP_MAXTHREADS_LIMIT	4096
volatile int stp_maxthreads_limit = STP_MAXTHREADS_LIMIT;

/* Based on taskq_maxbuckets value */
#define	STP_MAX_TQBUCKETS	128

/*
 * This defines the minalloc entries for taskq's backing queue.
 * We always dispatch with TQ_NOQUEUE, so this is only relevant in
 * in taskq bucket extensions.
 */
#define	STP_MIN_ENTRIES	1024
volatile int stp_min_entries =  STP_MIN_ENTRIES;

#define	STP_HASH(x) ((x) ^ ((x) >> 11) ^ ((x) >> 17) ^ ((x) ^ 27))

volatile int stp_retry_timeout = 100;	/* 100usec */
volatile int stp_retry_backoff = 1000;	/* 1msec */

static kmutex_t stp_kstat_mutex;

/*
 * Caution: Change these values with extreme care,
 * adversely impacts queue monitoring and performance.
 */

#define	STP_DEFAULT_MONITOR_TIMEOUT	1000000		/* 1 sec */

volatile int stp_queue_scale = 4;
volatile int stp_min_queues = 2;

/* Default queue monitor timeout in usec */
volatile int stp_queue_monitor_timeout = STP_DEFAULT_MONITOR_TIMEOUT;

volatile int stp_flowctrl_timeout = STP_DEFAULT_MONITOR_TIMEOUT;

/* csw monitor timeout in usec */
volatile int stp_csw_monitor_timeout = 5000; /* 5ms */

/*
 * Dynamic csw control under high load.
 */

/* setting to 0 disables csw control */
volatile int stp_throttle = 1;

/* If set disables dynamic csw control, ie. throttle always */
volatile int stp_force_throttle = 0;

volatile int stp_task_deadline_timer = 5; /* 5ms */

/*
 * Going beyond this per-queue threshold enables
 * csw control for the pool.
 */
volatile int stq_csw_threshold = 25000;

/*
 * sustatined high-csw of 5s
 * Calculated as a multiple of stp_queue_monitor_timeout
 */
volatile int stq_highcsw_intrvl = 5;

/*
 * sustatined low-csw of 10s
 * Calculated as a multiple of stq_csw_monitor_timeout
 */
volatile int stq_lowcsw_intrvl = 2000;

/*
 * Dynamic throttle adjustments under csw control
 */
volatile int stp_throttle_base = 4; /* base throttle */

/*
 * Thresholds calculated as multiples of csw_monitor_timeout
 */
volatile int stp_active_underrun = 1000; /* 5s */
volatile int stp_idle_threshold = 1000; /* 5s */

/* Zone init/destroy routines */

void
stp_init(struct svc_globals *svc)
{
	mutex_init(&svc->sg_lock, NULL, MUTEX_DEFAULT, NULL);
	list_create(&svc->sg_stp_list, sizeof (struct __svc_task_pool),
	    offsetof(struct __svc_task_pool, stp_next));
}

void
stp_destroy(struct svc_globals *svc)
{
	mutex_destroy(&svc->sg_lock);
	list_destroy(&svc->sg_stp_list);
}

/*
 * Expects sg_lock be held.
 */
static SVCTASKPOOL *
stp_pool_find(struct svc_globals *svc, int poolid)
{
	SVCTASKPOOL *tmpool;

	ASSERT(MUTEX_HELD(&svc->sg_lock));

	for (tmpool = list_head(&svc->sg_stp_list); tmpool != NULL;
	    tmpool = list_next(&svc->sg_stp_list, tmpool)) {
		if (tmpool->stp_id == poolid)
			return (tmpool);
	}
	return (NULL);
}

/*
 * kproc for the task pool
 * Initializes the pool and waits until signalled to destroy.
 */

static void
stp_proc(void *args)
{
	char name[31];
	SVCTASKPOOL *stpool = (SVCTASKPOOL *)args;
	svc_tqueue_t *stqp;
	struct svc_globals *svc;
	uint32_t maxthreads_perq;
	user_t *pu = PTOU(curproc);
	zone_t *zone;
	int qsize = 0;
	int i;

	stpool->stp_ref = 1;

	(void) snprintf(name, sizeof (name), "stp_%d_%d_%d", stpool->stp_id,
	    stpool->stp_zoneid, curproc->p_pid);

	/* change our name to <daemon-exec-name>_kproc */
	(void) snprintf(pu->u_psargs, sizeof (pu->u_psargs),
	    "%s_kproc", stpool->stp_proc_name);
	(void) strlcpy(pu->u_comm, pu->u_psargs, sizeof (pu->u_comm));

	if (stpool->stp_zoneid != GLOBAL_ZONEID) {
		zone = zone_find_by_id(stpool->stp_zoneid);
		zproc_enter(zone);
		zone_rele(zone);
	}

	if (stpool->stp_maxthreads == 0) {
		stpool->stp_maxthreads = stp_default_maxthreads;
	} else if (stpool->stp_maxthreads > stp_maxthreads_limit) {
		stpool->stp_maxthreads = stp_maxthreads_limit;
	}

	/*
	 * Warn if an administrator explicitly set the max threads
	 * to a suboptimal value.
	 */
	if (stpool->stp_maxthreads < MIN(max_ncpus, STP_MAX_TQBUCKETS)) {
		cmn_err(CE_WARN, "Configuration for %s server threads"
		    " is suboptimal. Default value is %d, configured"
		    " value is %d", stpool->stp_proc_name,
		    stp_default_maxthreads, stpool->stp_maxthreads);
	}

	stpool->stp_tq = taskq_create_proc(name, stpool->stp_maxthreads,
	    minclsyspri, stp_min_entries, INT_MAX, curproc,
	    TASKQ_DYNAMIC | TASKQ_PREPOPULATE);

	stpool->stp_task_cache = kmem_cache_create(name,
	    sizeof (svc_task_t), 0, stp_task_constr, stp_task_destr,
	    NULL, NULL, NULL, 0);

	if ((stpool->stp_kstat = kstat_create("rpcmod", 0,
	    name, "rpcmod", KSTAT_TYPE_NAMED,
	    sizeof (stp_kstats) / sizeof (kstat_named_t),
	    KSTAT_FLAG_VIRTUAL)) != NULL) {
		stpool->stp_kstat->ks_lock = &stp_kstat_mutex;
		stpool->stp_kstat->ks_data = &stp_kstats;
		stpool->stp_kstat->ks_update = stp_kstat_update;
		stpool->stp_kstat->ks_private = stpool;
		kstat_install(stpool->stp_kstat);
	}

	if (ncpus > stp_queue_scale)
		qsize = (ncpus/stp_queue_scale);

	if (qsize < stp_min_queues)
		qsize = stp_min_queues;

	stpool->stp_qarray_len = qsize;

	stpool->stp_queue_array = kmem_alloc(
	    sizeof (svc_tqueue_t *) * stpool->stp_qarray_len, KM_SLEEP);

	maxthreads_perq = (stpool->stp_maxthreads / stpool->stp_qarray_len);

	for (i = 0; i < stpool->stp_qarray_len; i++) {
		stqp = kmem_zalloc(sizeof (svc_tqueue_t), KM_SLEEP);
		mutex_init(&stqp->stq_lock, NULL, MUTEX_DEFAULT, NULL);
		list_create(&stqp->stq_task_list, sizeof (svc_task_t),
		    offsetof(svc_task_t, st_next));
		stqp->stq_throttle = stp_throttle_base;
		stqp->stq_maxthreads = maxthreads_perq;
		stqp->stq_tpool = stpool;
		stpool->stp_queue_array[i] = stqp;
	}

	(void) snprintf(name, sizeof (name), "stp_m%d_%d_%d", stpool->stp_id,
	    stpool->stp_zoneid, curproc->p_pid);

	stpool->stp_async_msg_cache = kmem_cache_create(name,
	    sizeof (rpc_async_msg_t), 0, NULL, NULL, NULL, NULL, NULL, 0);

	list_create(&stpool->stp_fclist, sizeof (SVCMASTERXPRT),
	    offsetof(SVCMASTERXPRT, xp_next));

	stpool->stp_flags &= ~STP_NOSETUP;

	svc = zone_getspecific(svc_zone_key, curproc->p_zone);
	mutex_enter(&svc->sg_lock);

	mutex_enter(&stpool->stp_mtx);

	/*
	 * safety check: the userland daemons should not race
	 * in creating conflicting pools with same poolid.
	 */
	if (stp_pool_find(svc, stpool->stp_id) != NULL) {
		mutex_exit(&svc->sg_lock);
		goto err_exit;
	}

	/*
	 * The alloc thread already quit, destroy the
	 * pool
	 */
	if (stpool->stp_flags & STP_CLOSING) {
		mutex_exit(&svc->sg_lock);
		goto err_exit;
	}

	list_insert_head(&svc->sg_stp_list, stpool);

	mutex_exit(&svc->sg_lock);

	stpool->stp_flags |= STP_RUNNING;

	stpool->stp_ref++;	/* ref for flow control timeout */
	stpool->stp_fctid = timeout(stp_flow_control, (void *)stpool,
	    drv_usectohz(stp_flowctrl_timeout));

	/* Signal the pool_alloc thread */
	cv_signal(&stpool->stp_waitcv);

	/* wait for service closure */

	cv_wait(&stpool->stp_cv, &stpool->stp_mtx);

	ASSERT(stpool->stp_flags & STP_CLOSING);

	if (stpool->stp_fctid) {

		mutex_exit(&stpool->stp_mtx);
		(void) untimeout(stpool->stp_fctid);

		/*
		 * stp_flow_control() may have raced with
		 * untimeout() above, completing the cleanup.
		 */
		if (stpool->stp_fctid == NULL) {
			mutex_enter(&stpool->stp_mtx);
			goto err_exit;
		}
		stp_free_flowctrl(stpool);
		mutex_enter(&stpool->stp_mtx);
		stpool->stp_fctid = NULL;
		/* ref for flow control timeout */
		stpool->stp_ref--;
	}

err_exit:
	stpool->stp_ref--;
	mutex_exit(&stpool->stp_mtx);
	stp_pool_destroy(stpool);
	mutex_enter(&curproc->p_lock);
	lwp_exit();
}

/*
 * newproc and friends are not exactly zone-aware.
 * This is a workaround to create the proc from global zone
 * by utilizing the system taskq.
 */
static void
stp_create_proc(void *arg)
{
	SVCTASKPOOL *stpool = (SVCTASKPOOL *)arg;

	if (newproc(stp_proc, (caddr_t)stpool, syscid, minclsyspri,
	    NULL, 0) == 0) {
		return;
	}

	/*
	 * newproc failed. notify the pool_alloc thread and
	 * destroy the pool.
	 */
	cv_signal(&stpool->stp_waitcv);
	stp_pool_destroy(stpool);
}

/*
 * Called from svc_pool_create(). Allocates the pool, does part
 * initialization, creates a new kproc and waits for setup completion.
 * The new kproc signals this thread upon setup completion.
 */

int
stp_pool_alloc(int poolid, int maxthreads)
{
	user_t *pu = PTOU(curproc);
	struct svc_globals *svc;
	SVCTASKPOOL *stpool;
	int error = 0;

	svc = zone_getspecific(svc_zone_key, curproc->p_zone);
	mutex_enter(&svc->sg_lock);

	/*
	 * If a pool with a same id already exists close it,
	 * before creating a new one.
	 */
	if ((stpool = stp_pool_find(svc, poolid)) != NULL) {
		list_remove(&svc->sg_stp_list, stpool);
		mutex_exit(&svc->sg_lock);
		stp_pool_close(stpool);
	} else {
		mutex_exit(&svc->sg_lock);
	}

	stpool = kmem_zalloc(sizeof (SVCTASKPOOL), KM_SLEEP);
	stpool->stp_id = poolid;
	(void) strncpy(stpool->stp_proc_name, pu->u_comm, sizeof (pu->u_comm));
	stpool->stp_zoneid = getzoneid();
	stpool->stp_maxthreads = maxthreads;

	mutex_init(&stpool->stp_mtx, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&stpool->stp_cv, NULL, CV_DEFAULT, NULL);
	cv_init(&stpool->stp_waitcv, NULL, CV_DEFAULT, NULL);
	cv_init(&stpool->stp_fcwaitcv, NULL, CV_DEFAULT, NULL);

	stpool->stp_flags |= STP_NOSETUP;

	mutex_enter(&stpool->stp_mtx);

	/*
	 * Create the newproc through system taskq
	 */
	if (taskq_dispatch(system_taskq, stp_create_proc,
	    (void *)stpool, TQ_SLEEP) == NULL) {
		mutex_exit(&stpool->stp_mtx);
		stp_pool_destroy(stpool);
		return (-1);
	}

	error = cv_wait_sig(&stpool->stp_waitcv, &stpool->stp_mtx);

	/*
	 * If we are signalled to exit before the pool is setup
	 * mark the closing flag. stp_proc notices this and
	 * destroys the pool
	 */
	if (error == 0) {
		error = EINTR;
		stpool->stp_flags |= STP_CLOSING;
		cv_signal(&stpool->stp_cv);
	} else {
		error = 0;
	}

	mutex_exit(&stpool->stp_mtx);
	return (error);
}

/*
 * The wait mechanism previously was used to wait for thread
 * creation. This is no longer needed with the use of taskqs.
 * svc_wait() is now used to catch service stop/restarts.
 * On signal the pool's proc is signalled for service closure.
 */
int
stp_wait(int id)
{
	SVCTASKPOOL *tpool;
	struct svc_globals *svc;
	int error = 0;

	svc = zone_getspecific(svc_zone_key, curproc->p_zone);

	mutex_enter(&svc->sg_lock);
	tpool = stp_pool_find(svc, id);

	if (tpool == NULL) {
		mutex_exit(&svc->sg_lock);
		return (ENOENT);
	}

	mutex_enter(&tpool->stp_mtx);
	mutex_exit(&svc->sg_lock);

	tpool->stp_ref++;

	if (cv_wait_sig(&tpool->stp_waitcv, &tpool->stp_mtx) == 0)
		error = EINTR;

	tpool->stp_ref--;

	mutex_exit(&tpool->stp_mtx);

	if (error == EINTR) {
		/*
		 * Service daemon's thread was signaled to exit.
		 * Mark the pool for closure.
		 */
		mutex_enter(&svc->sg_lock);
		list_remove(&svc->sg_stp_list, tpool);
		mutex_exit(&svc->sg_lock);

		stp_pool_close(tpool);
	}
	return (error);
}

/*
 * Given a pool-id, find the pool to hold a
 * reference for the master transport.
 */
int
stp_xprt_register(SVCMASTERXPRT *mxprt, int id)
{
	SVCTASKPOOL *tpool;
	struct svc_globals *svc;

	svc = zone_getspecific(svc_zone_key, curproc->p_zone);

	mutex_enter(&svc->sg_lock);
	tpool = stp_pool_find(svc, id);

	if (tpool == NULL) {
		mutex_exit(&svc->sg_lock);
		mxprt->xp_tpool = NULL;
		return (ENOENT);
	}

	mutex_enter(&tpool->stp_mtx);
	tpool->stp_ref++;
	mutex_exit(&tpool->stp_mtx);
	mutex_exit(&svc->sg_lock);

	mxprt->xp_tpool = tpool;
	return (0);
}

/*
 * Called during a transport closure. Decrements pool's reference
 * count. If this is the pool's last transport and pool is being
 * closed, signals the pool destroy thread.
 */
void
stp_xprt_unregister(SVCMASTERXPRT *mxprt)
{
	SVCTASKPOOL *tpool = mxprt->xp_tpool;

	if (tpool == NULL)
		return;

	mutex_enter(&tpool->stp_mtx);
	tpool->stp_ref--;

	/*
	 * This is called on last reference to the xprt,
	 * no need for xp_ref_lock since we are the last
	 * thread to get to this xprt.
	 */
	if (mxprt->xp_flags & XPRT_FLOWCTRL_ON) {
		list_remove(&tpool->stp_fclist, mxprt);
		mxprt->xp_flags &= ~XPRT_FLOWCTRL_ON;
	}

	if ((tpool->stp_ref == 0) &&
	    (tpool->stp_flags & STP_CLOSING)) {
		cv_signal(&tpool->stp_cv);
	}
	mutex_exit(&tpool->stp_mtx);
}

/*
 * Marks the pool for closure and signals
 * the stp_proc() thread to exit.
 */
static void
stp_pool_close(SVCTASKPOOL *tpool)
{

	/* call any registered callbacks */

	mutex_enter(&tpool->stp_mtx);

	if (tpool->stp_unregister.scb_func != NULL)
		(tpool->stp_unregister.scb_func)(tpool->stp_unregister.scb_arg);

	/*
	 * Mark for closure and signal the proc
	 */
	tpool->stp_flags |=  STP_CLOSING;
	cv_signal(&tpool->stp_cv);
	mutex_exit(&tpool->stp_mtx);
}

/*
 * Called from zone_shutdown(). It is possible that
 * the RPC service pools have already been destroyed when the
 * services were stopped.
 */
void
stp_free_allpools(struct svc_globals *svc)
{
	SVCTASKPOOL *tpool;

	mutex_enter(&svc->sg_lock);
	while ((tpool = list_remove_head(&svc->sg_stp_list))) {
		mutex_exit(&svc->sg_lock);
		stp_pool_close(tpool);
		mutex_enter(&svc->sg_lock);
	}
	mutex_exit(&svc->sg_lock);
}

/*
 * Final destroy of the pool, waits for all references
 * to drop to 0, before proceeding on destruction.
 */
void
stp_pool_destroy(SVCTASKPOOL *tpool)
{
	int retval;
	svc_tqueue_t *stqp;
	int i;

	/* Wait for the transports to close */
	mutex_enter(&tpool->stp_mtx);

	if (tpool->stp_flags & STP_NOSETUP)
		goto nosetup;

	/*
	 * We wait for the references from all the master transports
	 * to be released. This also implies that all the messages/tasks
	 * in the queues are freed by now.
	 */
	while (tpool->stp_ref) {
		retval = cv_wait_sig(&tpool->stp_cv, &tpool->stp_mtx);
		if (retval == 0) {
			/* state still STP_CLOSING */
			mutex_exit(&tpool->stp_mtx);
			return;
		}
	}

	/*
	 * Call the user supplied shutdown function.  This is done
	 * here so the user of the pool will be able to cleanup
	 * service related resources.
	 */
	if (tpool->stp_shutdown.scb_func != NULL)
		(tpool->stp_shutdown.scb_func)(tpool->stp_shutdown.scb_arg);

	/*
	 * All transports now closed, begin destruction.
	 */

	/* clean up the queues */
	for (i = 0; i < tpool->stp_qarray_len; i++) {
		stqp = tpool->stp_queue_array[i];
		if (stqp->stq_mtid)
			(void) untimeout(stqp->stq_mtid);
		if (stqp->stq_rtid)
			(void) untimeout(stqp->stq_rtid);
		ASSERT(list_is_empty(&stqp->stq_task_list));
		list_destroy(&stqp->stq_task_list);
		mutex_destroy(&stqp->stq_lock);
		kmem_free(stqp, sizeof (svc_tqueue_t));
	}
	kmem_free(tpool->stp_queue_array,
	    sizeof (svc_tqueue_t *) * tpool->stp_qarray_len);

	taskq_destroy(tpool->stp_tq);

	if (tpool->stp_kstat != NULL)
		kstat_delete(tpool->stp_kstat);

	kmem_cache_destroy(tpool->stp_task_cache);
	kmem_cache_destroy(tpool->stp_async_msg_cache);
	list_destroy(&tpool->stp_fclist);

nosetup:
	cv_destroy(&tpool->stp_waitcv);
	cv_destroy(&tpool->stp_fcwaitcv);
	cv_destroy(&tpool->stp_cv);
	mutex_destroy(&tpool->stp_mtx);
	kmem_free(tpool, sizeof (SVCTASKPOOL));
}

/* Pool control */

int
stp_pool_control(int id, int cmd, void (*cb_fn)(void *), void *cb_fn_arg)
{
	SVCTASKPOOL *pool;
	struct svc_globals *svc;

	svc = zone_getspecific(svc_zone_key, curproc->p_zone);

	switch (cmd) {
	case SVCPSET_SHUTDOWN_PROC:
		/*
		 * Search the list for a pool with a matching id
		 * and register the transport handle with that pool.
		 */
		mutex_enter(&svc->sg_lock);

		if ((pool = stp_pool_find(svc, id)) == NULL) {
			mutex_exit(&svc->sg_lock);
			return (ENOENT);
		}
		/*
		 * Grab the pool lock before releasing the
		 * pool list lock
		 */
		mutex_enter(&pool->stp_mtx);
		mutex_exit(&svc->sg_lock);

		pool->stp_shutdown.scb_func = cb_fn;
		pool->stp_shutdown.scb_arg = cb_fn_arg;

		mutex_exit(&pool->stp_mtx);

		return (0);

	case SVCPSET_UNREGISTER_PROC:
		/*
		 * Search the list for a pool with a matching id
		 * and register the unregister callback handle with that pool.
		 */
		mutex_enter(&svc->sg_lock);

		if ((pool = stp_pool_find(svc, id)) == NULL) {
			mutex_exit(&svc->sg_lock);
			return (ENOENT);
		}
		/*
		 * Grab the pool lock before releasing the
		 * pool list lock
		 */
		mutex_enter(&pool->stp_mtx);
		mutex_exit(&svc->sg_lock);

		pool->stp_unregister.scb_func = cb_fn;
		pool->stp_unregister.scb_arg = cb_fn_arg;

		mutex_exit(&pool->stp_mtx);

		return (0);

	case SVCPSET_HANDLE:
		/*
		 * Search the list for a pool with a matching id
		 * and register the unregister callback handle with that pool.
		 */
		mutex_enter(&svc->sg_lock);

		if ((pool = stp_pool_find(svc, id)) == NULL) {
			mutex_exit(&svc->sg_lock);
			return (ENOENT);
		}

		/*
		 * Grab the pool lock before releasing the
		 * pool list lock
		 */
		mutex_enter(&pool->stp_mtx);
		mutex_exit(&svc->sg_lock);

		pool->stp_handle = cb_fn_arg;

		mutex_exit(&pool->stp_mtx);
		return (0);

	case SVCPSET_ASYNCREPLY:
		/*
		 * Used by services that are capable of
		 * async replies.
		 */
		mutex_enter(&svc->sg_lock);
		if ((pool = stp_pool_find(svc, id)) == NULL) {
			mutex_exit(&svc->sg_lock);
			return (ENOENT);
		}
		/*
		 * Grab the pool lock before releasing the
		 * pool list lock
		 */
		mutex_enter(&pool->stp_mtx);
		mutex_exit(&svc->sg_lock);
		pool->stp_flags |= STP_ASYNC_REPLY;
		mutex_exit(&pool->stp_mtx);
		return (0);

	default:
		return (EINVAL);
	}
}

/*
 * If under csw control and pool is throttled, allow a
 * dispatch only if active threads are less than
 * current throttle limit.
 */
static int
stp_can_dispatch(svc_tqueue_t *stqp)
{
	if (!stp_throttle)
		return (1);

	if ((stp_force_throttle || STP_CHECK_CSWCTRL(stqp)) &&
	    (stqp->stq_active >= stqp->stq_throttle))
		return (0);

	return (1);
}

/*
 * Returns 0 on successful dispatch or 1 on failure.
 */
int
stp_queuereq(queue_t *q, mblk_t *mp)
{
	SVCMASTERXPRT *mxprt = ((void **) q->q_ptr)[0];
	SVCTASKPOOL *tpool = mxprt->xp_tpool;
	int qarrayindex, dispnow;
	svc_tqueue_t *stqp;
	svc_task_t *staskp = NULL;
	uintptr_t hash = ((uintptr_t)mp + CPU_PSEUDO_RANDOM())>> 5;

	/*
	 * If blocked due to flow control, skip the queue.
	 */
	if (STP_CHECK_FLOWCTRL(tpool))
		goto retrydisp;

	ASSERT(!is_system_labeled() || msg_getcred(mp, NULL) != NULL ||
	    mp->b_datap->db_type != M_DATA);

	mp->b_queue = q;

	qarrayindex = (STP_HASH(hash) % tpool->stp_qarray_len);

	stqp = tpool->stp_queue_array[qarrayindex];

	DTRACE_PROBE2(krpc__i__stp_queuereq, svc_tqueue_t *, stqp,
	    int, qarrayindex);

	staskp = kmem_cache_alloc(tpool->stp_task_cache, KM_NOSLEEP);

	if (staskp == NULL)
		goto retrydisp;

	staskp->st_mp = mp;
	staskp->st_queue = stqp;
	staskp->st_qtime = ddi_get_lbolt();

	mutex_enter(&stqp->stq_lock);

	list_insert_tail(&stqp->stq_task_list, staskp);
	stqp->stq_msgcnt++;

	/*
	 * Start a monitor thread for the queue if not started.
	 */
	if (stqp->stq_mtid == 0) {
		stqp->stq_mtid = timeout(stp_queue_monitor, (void *)stqp,
		    drv_usectohz(stp_queue_monitor_timeout));
	}

	/*
	 * We account for the csw here with the assumption that
	 * if we did a taskq_dispatch() it would result in one.
	 */
	stqp->stq_csw++;

	/*
	 * Request throttling is enabled under high csw.
	 * If enabled, dispatch if below the throttle limit or if
	 * the top of the queue has missed the deadline.
	 */
	dispnow = (stp_can_dispatch(stqp) || stp_deadline_check(stqp));

	if (dispnow && stp_taskq_dispatch(stqp)) {
		mutex_exit(&stqp->stq_lock);
		return (0);
	}

	/*
	 * We are here either due to input throttling or
	 * taskq dispatch failure. If latter, retry the call asap.
	 */

	if (dispnow) {
		/* taskq dispatch failure */
		stqp->stq_disp_failed++;
		if (stqp->stq_rtid == 0) {
			stqp->stq_rtid = timeout(stp_retry_dispatch,
			    (void *)stqp,
			    drv_usectohz(stp_retry_timeout));
		}
	}
	mutex_exit(&stqp->stq_lock);
	return (0);

retrydisp:
	/*
	 * This is the case where we could not even allocate a
	 * svc_task. Return failure in such a case, which turns on
	 * flow control at lower module.
	 */
	if (!STP_CHECK_FLOWCTRL(tpool))
		stp_enable_flowctrl(tpool);

	/*
	 * Insert the transport in flow controlled list.
	 */
	if (!(mxprt->xp_flags & XPRT_FLOWCTRL_ON))
		stp_flowctrl_insert(mxprt);

	return (1);
}

/*
 * Called on taskq_dispatch() failure, retries dispatch.
 */
static void
stp_retry_dispatch(void *arg)
{
	svc_tqueue_t *stqp = (svc_tqueue_t *)arg;
	int dispatch;

	mutex_enter(&stqp->stq_lock);

	dispatch = stp_taskq_dispatch(stqp);

	/*
	 * failed to dispatch again, re-try
	 */
	if (!dispatch) {
		stqp->stq_rtid = timeout(stp_retry_dispatch, (void *)stqp,
		    drv_usectohz(stp_retry_backoff));
	} else {
		stqp->stq_rtid = 0;
	}
	mutex_exit(&stqp->stq_lock);
}

/*
 * Main dispatch routine working the task queues.
 * loops through the tasks in a queue.
 */
static void
stp_dispatch(void *arg)
{
	mblk_t *mp;
	char *credp;
	SVCXPRT *xprtp;
	svc_task_t *staskp;
	SVCMASTERXPRT *mxprt;
	clock_t last_monitored_ts;
	svc_tqueue_t *stqp = (svc_tqueue_t *)arg;

	mutex_enter(&stqp->stq_lock);

	staskp = list_remove_head(&stqp->stq_task_list);

	last_monitored_ts = stqp->stq_monitor_ts;

	while (staskp) {

		stqp->stq_msgcnt--;

		/*
		 * roll call accounting after completion of each
		 * task.
		 */
		if (stqp->stq_monitor_ts != last_monitored_ts) {
			stqp->stq_rollcall++;
			last_monitored_ts = stqp->stq_monitor_ts;
		}
		mutex_exit(&stqp->stq_lock);

		mp = staskp->st_mp;
		mp->b_next = (mblk_t *)NULL;
		mxprt = ((void **)(mp->b_queue->q_ptr))[0];
		mp->b_queue = NULL;

		xprtp = &staskp->st_xprt;

		/*
		 * If the connection's closing, then free the
		 * message and the task. No locks held in the
		 * xp_flags check below for performance reasons.
		 * Delaying of closure due to requests in-progress
		 * should be fine.
		 */
		if (mxprt->xp_flags & XPRT_QUEUE_CLOSED) {
			stp_msg_free(staskp, mxprt);
		} else {
			/* Request processing */

			/* clone the master transport */
			if (xprtp->xp_cred == NULL)
				xprtp->xp_cred = crget();

			svc_clone_link(mxprt, xprtp, NULL);

			credp = (char *)(staskp->st_rqcred);
			svc_process_request(xprtp, staskp->st_mp, credp);
		}
		mutex_enter(&stqp->stq_lock);
		staskp = list_remove_head(&stqp->stq_task_list);
	}
	STP_DECR_ACTIVE_THR(stqp);
	mutex_exit(&stqp->stq_lock);
}

/*
 * Performs the actual dispatch to the taskq
 * with queue accounting. Expects stq_lock be held.
 * Return's 1 on successful taskq dispatch,
 * 0 on failure to dispatch.
 */
static int
stp_taskq_dispatch(svc_tqueue_t *stqp)
{
	SVCTASKPOOL *stpool = stqp->stq_tpool;
	int dispatched;

	/*
	 * We count the thread for this request and
	 * account for the roll-call before we dispatch.
	 * The count is corrected if we fail to dispatch.
	 */

	STP_INCR_ACTIVE_THR(stqp);
	mutex_exit(&stqp->stq_lock);

	dispatched = taskq_dispatch(stpool->stp_tq, stp_dispatch, stqp,
	    TQ_NOSLEEP | TQ_NOQUEUE);

	mutex_enter(&stqp->stq_lock);

	if (!dispatched) {
		STP_DECR_ACTIVE_THR(stqp);
		return (0);
	}
	return (1);
}

/*
 * Returns 0 if already at max threads for the pool.
 * 1 if thread can block.
 */
int
stp_curthread_can_block(SVCXPRT *xprt)
{
	svc_tqueue_t *stqp = ((svc_task_t *)xprt)->st_queue;
	SVCTASKPOOL *tpool = stqp->stq_tpool;
	int qindex = 0;
	int thractive = 0;
	int canblock = 0;

	/*
	 * tricky logic: if we are not under csw control,
	 * max active threads per queue does not matter since
	 * requests are dispatched anyway. As long as total
	 * number of active threads for the pool are below
	 * max threads per pool, we can block. Once it goes
	 * over max threads, taskq_dispatch(TQ_NOQUEUE)
	 * can fail.
	 */
	if (!STP_CHECK_CSWCTRL(stqp)) {
		while (qindex < tpool->stp_qarray_len) {
			stqp = tpool->stp_queue_array[qindex++];
			thractive += stqp->stq_active;
		}
		if (thractive < tpool->stp_maxthreads)
			canblock = 1;
	} else {
		/*
		 * In csw control, per queue active threads matter.
		 */
		if (STP_ACTIVE_THR_UNDER_MAX(stqp))
			canblock = 1;
	}
	return (canblock);
}

/*
 * Free a message without processing.
 */
static void
stp_msg_free(svc_task_t *stask, SVCMASTERXPRT *mxprt)
{
	SVCTASKPOOL *tpool = mxprt->xp_tpool;
	/*
	 * The transport specific release takes care
	 * of freeing the message with any associated private
	 * data along with decrementing any counts.
	 */
	(*RELE_PROC(mxprt)) (mxprt->xp_wq, stask->st_mp);
	kmem_cache_free(tpool->stp_task_cache, stask);
}

/*
 * Called from svc_xprt_free()
 */
void
stp_task_free(svc_task_t *taskp)
{
	SVCTASKPOOL *tpool = (taskp->st_queue->stq_tpool);
	kmem_cache_free(tpool->stp_task_cache, taskp);
}

/*
 * kmem cache constructor
 */
/* ARGSUSED */
static int
stp_task_constr(void *task, void *arg, int arg2)
{
	SVCXPRT *xprtp = (SVCXPRT *)task;
	xprtp->xp_cred = NULL;
	return (0);
}

/*
 * kmem cache destructor.
 * Creds are cached until task destruction
 */
/* ARGSUSED */
static void
stp_task_destr(void *task, void *arg)
{
	SVCXPRT *xprtp = (SVCXPRT *)task;
	if (xprtp->xp_cred)
		crfree(xprtp->xp_cred);
}

/*
 * kstat update callback
 * No locks held while reading the queue counts.
 */
static int
stp_kstat_update(kstat_t *ksp, int rw)
{
	SVCTASKPOOL *tpool = ksp->ks_private;
	struct stp_kstats *stpks = &stp_kstats;
	svc_tqueue_t *stqp;
	int qindex = 0;

	if (rw == KSTAT_WRITE)
		return (EACCES);

	stpks->stp_dispatch_failed.value.ui64 = 0;
	stpks->stp_active_threads.value.ui64 = 0;
	stpks->stp_msgs_queued.value.ui64 = 0;
	stpks->stp_csw_control.value.ui64 = 0;
	stpks->stp_flow_control.value.ui64 = 0;
	stpks->stp_throttle.value.ui64 = 0;

	while (qindex < tpool->stp_qarray_len) {
		stqp = tpool->stp_queue_array[qindex++];
		stpks->stp_dispatch_failed.value.ui64 += stqp->stq_disp_failed;
		stpks->stp_active_threads.value.ui64 += stqp->stq_active;
		stpks->stp_msgs_queued.value.ui64 += stqp->stq_msgcnt;
		stpks->stp_throttle.value.ui64 += stqp->stq_throttle;
	}

	stpks->stp_throttle.value.ui64 =
	    (stpks->stp_throttle.value.ui64/qindex);
	stpks->stp_csw_control.value.ui64 = STP_CHECK_CSWCTRL(stqp);
	stpks->stp_flow_control.value.ui64 = STP_CHECK_FLOWCTRL(tpool);

	return (0);
}

/*
 * Dynamic flow control for the queue.
 */
static void
stp_enable_flowctrl(SVCTASKPOOL *tpool)
{
	STP_ENABLE_FLAG(tpool, STP_FLOWCTRL_ON);
}

/*
 * adds a master xprt to the flow controlled
 * xprt list for the pool
 */
static void
stp_flowctrl_insert(SVCMASTERXPRT *mxprt)
{
	SVCTASKPOOL *tpool = mxprt->xp_tpool;
	mutex_enter(&mxprt->xp_ref_lock);
	if (mxprt->xp_flags & XPRT_FLOWCTRL_ON) {
		mutex_exit(&mxprt->xp_ref_lock);
		return;
	}
	mxprt->xp_flags |= XPRT_FLOWCTRL_ON;
	mutex_exit(&mxprt->xp_ref_lock);

	mutex_enter(&tpool->stp_mtx);
	list_insert_head(&tpool->stp_fclist, mxprt);
	mutex_exit(&tpool->stp_mtx);
}

/*
 * Unblocks all the flow controlled xprts.
 */
static void
stp_disable_flowctrl(SVCTASKPOOL *tpool)
{
	SVCMASTERXPRT *mxprt;

	STP_DISABLE_FLAG(tpool, STP_FLOWCTRL_ON);
	mutex_enter(&tpool->stp_mtx);
	while ((mxprt = list_remove_head(&tpool->stp_fclist))) {
		mutex_exit(&tpool->stp_mtx);
		svc_unblock(mxprt);
		mutex_enter(&tpool->stp_mtx);
	}
	mutex_exit(&tpool->stp_mtx);
}

/*
 * The transports are just removed from the list
 * any queued messages at the lower layer are flushed
 * on close of the transport. This is called on pool
 * closure.
 */
static void
stp_free_flowctrl(SVCTASKPOOL *tpool)
{
	SVCMASTERXPRT *mxprt;
	while ((mxprt = list_head(&tpool->stp_fclist))) {
		list_remove(&tpool->stp_fclist, mxprt);
		mutex_enter(&mxprt->xp_ref_lock);
		mxprt->xp_flags &= ~XPRT_FLOWCTRL_ON;
		mutex_exit(&mxprt->xp_ref_lock);
	}
}

/*
 * Flow-control monitor per pool that runs at
 * stp_flowctrl_timeout interval.
 */
void
stp_flow_control(void *arg)
{
	SVCTASKPOOL *tpool = (SVCTASKPOOL *)arg;
	svc_tqueue_t *qp;
	int msgtotal = 0;
	int qindex = 0;
	int flow_controlled;

	flow_controlled = STP_CHECK_FLOWCTRL(tpool);

	/*
	 * Even if flow control is explictly disabled we
	 * could be here due to failure to queuereq on low
	 * memory conditions.
	 */

	if (!svc_enforce_maxreqs && !flow_controlled) {
		goto out;
	}

	while (qindex < tpool->stp_qarray_len) {
		qp = tpool->stp_queue_array[qindex++];
		msgtotal += qp->stq_msgcnt;
	}

	switch (flow_controlled) {
	case TRUE:
		if (msgtotal < svc_qmsg_lowat)
			stp_disable_flowctrl(tpool);
		break;
	case FALSE:
		if (msgtotal > svc_qmsg_hiwat)
			stp_enable_flowctrl(tpool);
		break;
	}
out:
	mutex_enter(&tpool->stp_mtx);
	if (tpool->stp_flags & STP_CLOSING) {
		tpool->stp_ref--;
		tpool->stp_fctid = NULL;
		mutex_exit(&tpool->stp_mtx);
		stp_free_flowctrl(tpool);
		return;
	}

	tpool->stp_fctid = timeout(stp_flow_control, (void *)tpool,
	    drv_usectohz(stp_flowctrl_timeout));

	mutex_exit(&tpool->stp_mtx);
}

/*
 * Dynamically manages turning on/off of CSW control.
 * The counters are maintained per queue. The csw control flags
 * however are maintained at pool level and if set the change
 * propagates to all queues.
 */
static void
stp_csw_control(svc_tqueue_t *stqp)
{
	int csw_control, csw_scale, high_csw;

	csw_control = STP_CHECK_CSWCTRL(stqp);

	/*
	 * Not under csw_control. Enable csw control
	 * if above the csw cutoff threshold.
	 */
	if (!csw_control) {

		high_csw = (stqp->stq_csw > stq_csw_threshold);

		/*
		 * Measured across back-to-back monitored intervals.
		 * reset any previously set counters.
		 */
		if (!high_csw) {
			STP_RESET_CSWCNTS(stqp);
			return;
		}

		stqp->stq_over_cswl++;
		stqp->stq_under_cswl = 0;
		if (stqp->stq_over_cswl < stq_highcsw_intrvl) {
			return;
		}

		/*
		 * Over the highcsw interval threshold,
		 * enable csw control.
		 */

		mutex_exit(&stqp->stq_lock);
		STP_ENABLE_CSWCTRL(stqp);
		mutex_enter(&stqp->stq_lock);

		STP_RESET_CSWCNTS(stqp);
		return;
	}

	/* In csw control */

	/*
	 * The csw counts are reset on every run of the queue monitor.
	 * When under csw control, since the queue monitor runs at
	 * csw_monitor_timeout intervals scale the thresholds
	 * appropriately.
	 */
	csw_scale = (STP_DEFAULT_MONITOR_TIMEOUT/stp_csw_monitor_timeout);
	high_csw = (stqp->stq_csw > (stq_csw_threshold/csw_scale));

	/*
	 * Measured across back-to-back monitored intervals.
	 * reset any previously set counters.
	 */
	if (high_csw) {
		STP_RESET_CSWCNTS(stqp);
		return;
	}

	/* monitor for csw drops below the cutoff. */
	stqp->stq_under_cswl++;
	stqp->stq_over_cswl = 0;

	if (stqp->stq_under_cswl < stq_lowcsw_intrvl) {
		return;
	}

	/*
	 * Over the lowcsw interval threshold
	 * disable csw control.
	 */
	mutex_exit(&stqp->stq_lock);
	STP_DISABLE_CSWCTRL(stqp);
	mutex_enter(&stqp->stq_lock);

	STP_RESET_CSWCNTS(stqp);
}

/*
 * Per-queue monitor.
 * When not under csw control, work is done mainly by
 * stp_csw_control(). When under csw control, monitors for
 * missed deadlines and blocked threads. Dynamically manages
 * per-queue throttle values.
 */
static void
stp_queue_monitor(void *arg)
{
	int thrblocked, queue_delay;
	int msgdeadline = 0;
	svc_task_t *stask;
	svc_tqueue_t *stqp = (svc_tqueue_t *)arg;
	int timeo, csw_ctrl;

	mutex_enter(&stqp->stq_lock);

	stp_csw_control(stqp);

	csw_ctrl = STP_CHECK_CSWCTRL(stqp);

	timeo = csw_ctrl ? stp_csw_monitor_timeout : stp_queue_monitor_timeout;

	/*
	 * queue is empty and at base throttle, nothing to do.
	 */
	if (stqp->stq_msgcnt == 0 &&
	    (stqp->stq_throttle == stp_throttle_base)) {
		goto out;
	}

	/* Reset from previous controls */
	if (!csw_ctrl) {
		stqp->stq_throttle = stp_throttle_base;
		goto out;
	}

	/*
	 * Check for blocked threads
	 */
	thrblocked = (stqp->stq_rollcall < stqp->stq_active);

	/*
	 * Deadline monitoring. Check if head of the queue is past
	 * deadline.
	 */
	stask = list_head(&stqp->stq_task_list);
	if (stask) {
		queue_delay = TICK_TO_MSEC(ddi_get_lbolt() - stask->st_qtime);
		if (queue_delay > stp_task_deadline_timer)
			msgdeadline = 1;

		DTRACE_PROBE2(rpc__i__stp_queue_monitor, svc_tqueue_t *, stqp,
		    int, queue_delay);
	}

	/*
	 * Dispatch a new worker if we have any
	 * blocked threads or messages past deadline.
	 */
	if (thrblocked || msgdeadline) {
		(void) stp_incr_active(stqp, 0);
		goto out;
	}

	if (!STP_IDLE_THREADS(stqp)) {
		/* Not idle. Reset under throttle count */
		stqp->stq_under_throttle = 0;
		goto out;
	}

	/*
	 * Idle queue, reset over throttle count
	 */
	stqp->stq_over_throttle = 0;

	/*
	 * Queue throttle greater than base throttle.
	 * Adjust the throttle if above the idle threshold.
	 */
	if (stqp->stq_throttle > stp_throttle_base) {
		stqp->stq_under_throttle++;
		if (stqp->stq_under_throttle > stp_idle_threshold)
			stqp->stq_throttle--;
	}

out:
	if (stqp->stq_tpool->stp_flags & STP_CLOSING) {
		stqp->stq_mtid = 0;
	} else {
		stqp->stq_monitor_ts = ddi_get_lbolt();
		stqp->stq_rollcall = 0;
		stqp->stq_csw = 0;
		stqp->stq_mtid = timeout(stp_queue_monitor, (void *)stqp,
		    drv_usectohz(timeo));
	}
	mutex_exit(&stqp->stq_lock);
}

/*
 * Expects caller to hold stq_lock
 */
static int
stp_deadline_check(svc_tqueue_t *stqp)
{
	int qdelay;
	svc_task_t *head = list_head(&stqp->stq_task_list);

	ASSERT(MUTEX_HELD(&stqp->stq_lock));

	if (head == NULL)
		return (0);

	if (!STP_ACTIVE_THR_UNDER_MAX(stqp))
		return (0);

	qdelay = TICK_TO_MSEC(ddi_get_lbolt() - head->st_qtime);

	if (qdelay < stp_task_deadline_timer) {
		return (0);
	}

	return (1);
}

void
stp_decr_throttle(svc_tqueue_t *stqp)
{
	ASSERT(MUTEX_HELD(&stqp->stq_lock));
	DTRACE_PROBE1(krpc__i__stp_decr_throttle, svc_tqueue_t *, stqp);
	if (stqp->stq_throttle > stp_throttle_base)
		stqp->stq_throttle--;
}

/*
 * Dispatch a request adding to the active threads. Used when
 * under csw control. Performs per-queue throttle management
 * if the active increments are above the threshold limit.
 * Returns 1 if queue's throttle is incremented.
 */
int
stp_incr_active(svc_tqueue_t *stqp, int async_incr)
{

	ASSERT(MUTEX_HELD(&stqp->stq_lock));

	/*
	 * Already at max, nothing to do.
	 */
	if ((stqp->stq_throttle >= stqp->stq_maxthreads) ||
	    !STP_ACTIVE_THR_UNDER_MAX(stqp)) {
		return (0);
	}

	/*
	 * Dispatch a new thread for the queue
	 */
	if (!stp_taskq_dispatch(stqp)) {
		return (0);
	}

	/*
	 * Case of async_incr. If the queue has idle threads
	 * that is if thr_active < throttle, no point in
	 * incrementing the throttle.
	 */

	if (STP_IDLE_THREADS(stqp))
		return (0);

	/*
	 * async_incr increments are not counted towards
	 * per-queue throttle management.
	 */
	if (async_incr) {
		stqp->stq_throttle++;
		return (1);
	}

	/*
	 * Monitors over the throttle increment thresholds to
	 * dynamically adjust base throttle.
	 */
	stqp->stq_over_throttle++;
	if (stqp->stq_over_throttle > stp_active_underrun) {
		DTRACE_PROBE1(krpc__i__stp_incr_throttle, svc_tqueue_t *, stqp);
		stqp->stq_throttle++;
		stqp->stq_over_throttle = 0;
	}
	return (1);
}

/*
 * Async send-reply drain routine.
 */
void
stp_async_reply(void *args)
{
	int error = 0;
	rpc_async_msg_t *msg = (rpc_async_msg_t *)args;
	rpc_async_msg_t *tofree;
	SVCXPRT *xprt = msg->ram_xprt;
	SVC_ASYNC_MSGS *xp_msgs;
	svc_tqueue_t *stqp = ((svc_task_t *)xprt)->st_queue;
	int qdrain = 0;
	int incr_thr = 0;

	xp_msgs = &xprt->xp_master->xp_async_msgs;

	mutex_enter(&xp_msgs->msg_lock);
	msg = list_remove_head(&xp_msgs->msg_list);
	mutex_exit(&xp_msgs->msg_lock);

	while (msg) {

		/*
		 * If csw control is on and if we enter drain of replies
		 * for this xprt, arrange for another thread to process
		 * our queue if active thread count is at base throttle.
		 * This prevents latency bubbles for this queue.
		 */

		if (STP_CHECK_CSWCTRL(stqp) && (qdrain == 1)) {
			mutex_enter(&stqp->stq_lock);
			if ((stqp->stq_msgcnt > 0) &&
			    (stqp->stq_active == stp_throttle_base)) {
				incr_thr = stp_incr_active(stqp, 1);
			}
			mutex_exit(&stqp->stq_lock);
		}

		if (!svc_sendreply(msg->ram_xprt, msg->ram_proc,
		    msg->ram_procargs)) {
			svcerr_systemerr(msg->ram_xprt);
			DTRACE_PROBE3(rpc__e__async_sendfail, SVCXPRT *,
			    msg->ram_xprt, xdr_proc_t, msg->ram_proc,
			    caddr_t, msg->ram_procargs);
			error++;
		}

		/*
		 * The callback function below calls svc_clone_unlink()
		 * on the clone xprt, which could free the master xprt (on last
		 * reference). Avoid referencing mxprt's elements after the
		 * call.
		 */

		tofree = msg;

		mutex_enter(&xp_msgs->msg_lock);
		if ((msg = list_remove_head(&xp_msgs->msg_list)) == NULL)
			xp_msgs->msg_indrain = 0;
		mutex_exit(&xp_msgs->msg_lock);

		/* cleanup callback */
		tofree->ram_cbfunc(tofree->ram_cbargs, error);

		kmem_cache_free(stqp->stq_tpool->stp_async_msg_cache, tofree);

		qdrain++;
	}

	DTRACE_PROBE2(rpc__i__async_sendreply, svc_tqueue_t *,
	    stqp, int, qdrain);

	/*
	 * Decrement the throttle increased while
	 * qdrain above.
	 */
	if (incr_thr) {
		mutex_enter(&stqp->stq_lock);
		stp_decr_throttle(stqp);
		mutex_exit(&stqp->stq_lock);
	}
}
