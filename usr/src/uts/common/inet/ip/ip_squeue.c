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
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * IP interface to squeues.
 *
 * IP uses squeues to force serialization of packets, both incoming and
 * outgoing. Each squeue is associated with a connection instance (conn_t)
 * above, and a soft ring (if enabled) below. Each CPU will have a default
 * squeue for outbound connections, and each soft ring of an interface will
 * have an squeue to which it sends incoming packets. squeues are never
 * destroyed, and if they become unused they are kept around against future
 * needs.
 *
 * IP organizes its squeues using squeue sets (squeue_set_t). For each CPU
 * in the system there will be one squeue set, all of whose squeues will be
 * bound to that CPU, plus one additional set known as the unbound set. Sets
 * associated with CPUs will have one default squeue, for outbound
 * connections, and a linked list of squeues used by various NICs for inbound
 * packets. The unbound set also has a linked list of squeues, but no default
 * squeue.
 *
 * When a CPU goes offline its squeue set is destroyed, and all its squeues
 * are moved to the unbound set. When a CPU comes online, a new squeue set is
 * created and the default set is searched for a default squeue formerly bound
 * to this CPU. If no default squeue is found, a new one is created.
 *
 * Two fields of the squeue_t, namely sq_next and sq_set, are owned by IP
 * and not the squeue code. squeue.c will not touch them, and we can modify
 * them without holding the squeue lock because of the guarantee that squeues
 * are never destroyed. ip_squeue locks must be held, however.
 *
 * All the squeue sets are protected by a single lock, the sqset_lock. This
 * is also used to protect the sq_next and sq_set fields of an squeue_t.
 *
 * The lock order is: cpu_lock --> ill_lock --> sqset_lock --> sq_lock
 *
 * There are two modes of associating connection with squeues. The first mode
 * associates each connection with the CPU that creates the connection (either
 * during open time or during accept time). The second mode associates each
 * connection with a random CPU, effectively distributing load over all CPUs
 * and all squeues in the system. The mode is controlled by the
 * ip_squeue_fanout variable.
 *
 * NOTE: The fact that there is an association between each connection and
 * squeue and squeue and CPU does not mean that each connection is always
 * processed on this CPU and on this CPU only. Any thread calling squeue_enter()
 * may process the connection on whatever CPU it is scheduled. The squeue to CPU
 * binding is only relevant for the worker thread.
 *
 * INTERFACE:
 *
 * squeue_t *ip_squeue_get(ill_rx_ring_t)
 *
 * Returns the squeue associated with an ill receive ring. If the ring is
 * not bound to a CPU, and we're currently servicing the interrupt which
 * generated the packet, then bind the squeue to CPU.
 *
 *
 * DR Notes
 * ========
 *
 * The ip_squeue_init() registers a call-back function with the CPU DR
 * subsystem using register_cpu_setup_func(). The call-back function does two
 * things:
 *
 * o When the CPU is going off-line or unconfigured, the worker thread is
 *	unbound from the CPU. This allows the CPU unconfig code to move it to
 *	another CPU.
 *
 * o When the CPU is going online, it creates a new squeue for this CPU if
 *	necessary and binds the squeue worker thread to this CPU.
 *
 * TUNABLES:
 *
 * ip_squeue_fanout: used when TCP calls IP_SQUEUE_GET(). If 1, then
 * pick the default squeue from a random CPU, otherwise use our CPU's default
 * squeue.
 *
 * ip_squeue_fanout can be accessed and changed using ndd on /dev/tcp or
 * /dev/ip.
 *
 * ip_squeue_worker_wait: global value for the sq_wait field for all squeues *
 * created. This is the time squeue code waits before waking up the worker
 * thread after queuing a request.
 */

#include <sys/types.h>
#include <sys/debug.h>
#include <sys/kmem.h>
#include <sys/cpuvar.h>
#include <sys/cmn_err.h>

#include <inet/common.h>
#include <inet/ip.h>
#include <netinet/ip6.h>
#include <inet/ip_if.h>
#include <inet/ip_ire.h>
#include <inet/nd.h>
#include <inet/ipclassifier.h>
#include <sys/types.h>
#include <sys/conf.h>
#include <sys/sunddi.h>
#include <sys/dlpi.h>
#include <sys/squeue_impl.h>
#include <sys/tihdr.h>
#include <inet/udp_impl.h>
#include <sys/strsubr.h>
#include <sys/zone.h>
#include <sys/dld.h>
#include <sys/atomic.h>

/*
 * List of all created squeue sets. The list and its size are protected by
 * sqset_lock.
 */
static squeue_set_t	**sqset_global_list; /* list 0 is the unbound list */
static uint_t		sqset_global_size;
kmutex_t		sqset_lock;

static void (*ip_squeue_create_callback)(squeue_t *) = NULL;

/*
 * ip_squeue_worker_wait: global value for the sq_wait field for all squeues
 *	created. This is the time squeue code waits before waking up the worker
 *	thread after queuing a request.
 */
uint_t ip_squeue_worker_wait = 10;

static squeue_t *ip_squeue_create(pri_t);
static squeue_set_t *ip_squeue_set_create(processorid_t);
static int ip_squeue_cpu_setup(cpu_setup_t, int, void *);
static void ip_squeue_set_move(squeue_t *, squeue_set_t *);
static void ip_squeue_set_destroy(cpu_t *);

#define	CPU_ISON(c) (c != NULL && CPU_ACTIVE(c) && (c->cpu_flags & CPU_EXISTS))

static squeue_t *
ip_squeue_create(pri_t pri)
{
	squeue_t *sqp;

	sqp = squeue_create(ip_squeue_worker_wait, pri);
	ASSERT(sqp != NULL);
	if (ip_squeue_create_callback != NULL)
		ip_squeue_create_callback(sqp);
	return (sqp);
}

void
ip_gld_squeue_add(mac_ip_sqinfo_t *sqinfo)
{
	squeue_t	*sqp;

	sqp = ip_squeue_getfree(MAXCLSYSPRI);
	sqinfo->mis_sqp = sqp;
	sqinfo->mis_tid = sqp->sq_worker;
}

void
ip_gld_squeue_remove(mac_ip_sqinfo_t *sqinfo)
{
	squeue_t	*sqp = sqinfo->mis_sqp;

	mutex_enter(&sqset_lock);
	ip_squeue_set_move(sqp, sqset_global_list[0]);
	mutex_exit(&sqset_lock);

	mutex_enter(&sqp->sq_lock);
	sqp->sq_state &= ~SQS_ILL_BOUND;
	mutex_exit(&sqp->sq_lock);
	sqinfo->mis_sqp = NULL;
	sqinfo->mis_tid = NULL;
}

/*
 * Create a new squeue_set. If id == -1, then we're creating the unbound set,
 * which should only happen once when we are first initialized. Otherwise id
 * is the id of the CPU that needs a set, either because we are initializing
 * or because the CPU has come online.
 *
 * If id != -1, then we need at a minimum to provide a default squeue for the
 * new set. We search the unbound set for candidates, and if none are found we
 * create a new one.
 */
static squeue_set_t *
ip_squeue_set_create(processorid_t id)
{
	squeue_set_t	*sqs;
	squeue_set_t	*src = sqset_global_list[0];
	squeue_t	**lastsqp, *sq;
	squeue_t	**defaultq_lastp = NULL;

	sqs = kmem_zalloc(sizeof (squeue_set_t), KM_SLEEP);
	sqs->sqs_cpuid = id;

	if (id == -1) {
		ASSERT(sqset_global_size == 0);
		sqset_global_list[0] = sqs;
		sqset_global_size = 1;
		return (sqs);
	}

	/*
	 * When we create an squeue set id != -1, we need to give it a
	 * default squeue, in order to support fanout of conns across
	 * CPUs. Try to find a former default squeue that matches this
	 * cpu id on the unbound squeue set. If no such squeue is found,
	 * find some non-default TCP squeue that is free. If still no such
	 * candidate is found, create a new squeue.
	 */

	ASSERT(MUTEX_HELD(&cpu_lock));
	ASSERT(MUTEX_HELD(&sqset_lock));
	lastsqp = &src->sqs_head;

	while (*lastsqp) {
		if ((*lastsqp)->sq_bind == id &&
		    (*lastsqp)->sq_state & SQS_DEFAULT) {
			/*
			 * Exact match. Former default squeue of cpu 'id'
			 */
			ASSERT(!((*lastsqp)->sq_state & SQS_ILL_BOUND));
			defaultq_lastp = lastsqp;
			break;
		}
		if (defaultq_lastp == NULL &&
		    !((*lastsqp)->sq_state & (SQS_ILL_BOUND | SQS_DEFAULT))) {
			/*
			 * A free non-default TCP squeue
			 */
			defaultq_lastp = lastsqp;
		}
		lastsqp = &(*lastsqp)->sq_next;
	}

	if (defaultq_lastp != NULL) {
		/* Remove from src set and set SQS_DEFAULT */
		sq = *defaultq_lastp;
		*defaultq_lastp = sq->sq_next;
		sq->sq_next = NULL;
		if (!(sq->sq_state & SQS_DEFAULT)) {
			mutex_enter(&sq->sq_lock);
			sq->sq_state |= SQS_DEFAULT;
			mutex_exit(&sq->sq_lock);
		}
	} else {
		sq = ip_squeue_create(SQUEUE_DEFAULT_PRIORITY);
		sq->sq_state |= SQS_DEFAULT;
	}

	sq->sq_set = sqs;
	sqs->sqs_default = sq;
	squeue_bind(sq, id); /* this locks squeue mutex */

	ASSERT(sqset_global_size <= NCPU);
	sqset_global_list[sqset_global_size++] = sqs;
	return (sqs);
}

/*
 * Called by ill_ring_add() to find an squeue to associate with a new ring.
 */

squeue_t *
ip_squeue_getfree(pri_t pri)
{
	squeue_set_t	*sqs = sqset_global_list[0];
	squeue_t	*sq;

	mutex_enter(&sqset_lock);
	for (sq = sqs->sqs_head; sq != NULL; sq = sq->sq_next) {
		/*
		 * Select a non-default TCP squeue that is free i.e. not
		 * bound to any ill.
		 */
		if (!(sq->sq_state & (SQS_DEFAULT | SQS_ILL_BOUND)))
			break;
	}

	if (sq == NULL) {
		sq = ip_squeue_create(pri);
		sq->sq_set = sqs;
		sq->sq_next = sqs->sqs_head;
		sqs->sqs_head = sq;
	}

	ASSERT(!(sq->sq_state & (SQS_POLL_THR_CONTROL | SQS_WORKER_THR_CONTROL |
	    SQS_POLL_CLEANUP_DONE | SQS_POLL_QUIESCE_DONE |
	    SQS_POLL_THR_QUIESCED)));

	mutex_enter(&sq->sq_lock);
	sq->sq_state |= SQS_ILL_BOUND;
	mutex_exit(&sq->sq_lock);
	mutex_exit(&sqset_lock);

	if (sq->sq_priority != pri) {
		thread_lock(sq->sq_worker);
		(void) thread_change_pri(sq->sq_worker, pri, 0);
		thread_unlock(sq->sq_worker);

		thread_lock(sq->sq_poll_thr);
		(void) thread_change_pri(sq->sq_poll_thr, pri, 0);
		thread_unlock(sq->sq_poll_thr);

		sq->sq_priority = pri;
	}
	return (sq);
}

/*
 * Initialize IP squeues.
 */
void
ip_squeue_init(void (*callback)(squeue_t *))
{
	int i;
	squeue_set_t	*sqs;

	ASSERT(sqset_global_list == NULL);

	ip_squeue_create_callback = callback;
	squeue_init();
	mutex_init(&sqset_lock, NULL, MUTEX_DEFAULT, NULL);
	sqset_global_list =
	    kmem_zalloc(sizeof (squeue_set_t *) * (NCPU+1), KM_SLEEP);
	sqset_global_size = 0;
	/*
	 * We are called at system boot time and we don't
	 * expect memory allocation failure.
	 */
	mutex_enter(&sqset_lock);
	sqs = ip_squeue_set_create(-1);
	mutex_exit(&sqset_lock);
	ASSERT(sqs != NULL);

	mutex_enter(&cpu_lock);
	/* Create squeue for each active CPU available */
	for (i = 0; i < NCPU; i++) {
		cpu_t *cp = cpu_get(i);
		if (CPU_ISON(cp) && cp->cpu_squeue_set == NULL) {
			/*
			 * We are called at system boot time and we don't
			 * expect memory allocation failure then
			 */
			mutex_enter(&sqset_lock);
			cp->cpu_squeue_set = ip_squeue_set_create(cp->cpu_id);
			mutex_exit(&sqset_lock);
			ASSERT(cp->cpu_squeue_set != NULL);
		}
	}

	register_cpu_setup_func(ip_squeue_cpu_setup, NULL);
	mutex_exit(&cpu_lock);
}

/*
 * Get a default squeue, either from the current CPU or a CPU derived by hash
 * from the index argument, depending upon the setting of ip_squeue_fanout.
 */
squeue_t *
ip_squeue_random(uint_t index)
{
	squeue_set_t *sqs = NULL;
	squeue_t *sq;

	/*
	 * The minimum value of sqset_global_size is typically 2, one
	 * for the unbound squeue set and another for the squeue set of
	 * the zeroth CPU. However, there is a small possibility that
	 * the sqset_global_size can be 1 on a single-cpu system if, e.g.,
	 * cpupart_attach_cpu() executes concurrently with an
	 * ip_squeue_random() invocation due to an interrupt from an incoming
	 * packet. Even though the value could be changing, it can
	 * never go below 1, so the assert does not need the lock protection.
	 */
	ASSERT(sqset_global_size >= 1);

	/* Protect against changes to sqset_global_list */
	mutex_enter(&sqset_lock);

	if (!ip_squeue_fanout)
		sqs = CPU->cpu_squeue_set;

	/*
	 * sqset_global_list[0] corresponds to the unbound squeue set.
	 * The computation below picks a set other than the unbound set
	 * when sqset_global_size > 1.
	 */
	if (sqs == NULL) {
		sqs = (sqset_global_size < 2) ? sqset_global_list[0] :
		    sqset_global_list[(index % (sqset_global_size - 1)) + 1];
	}
	sq = sqs->sqs_default;

	mutex_exit(&sqset_lock);
	ASSERT(sq);
	return (sq);
}

/*
 * Move squeue from its current set to newset. Not used for default squeues.
 * Bind or unbind the worker thread as appropriate.
 */

static void
ip_squeue_set_move(squeue_t *sq, squeue_set_t *newset)
{
	squeue_set_t	*set;
	squeue_t	**lastsqp;
	processorid_t	cpuid = newset->sqs_cpuid;

	ASSERT(!(sq->sq_state & SQS_DEFAULT));
	ASSERT(!MUTEX_HELD(&sq->sq_lock));
	ASSERT(MUTEX_HELD(&sqset_lock));

	set = sq->sq_set;
	if (set == newset)
		return;

	lastsqp = &set->sqs_head;
	while (*lastsqp != sq)
		lastsqp = &(*lastsqp)->sq_next;

	*lastsqp = sq->sq_next;
	sq->sq_next = newset->sqs_head;
	newset->sqs_head = sq;
	sq->sq_set = newset;
	if (cpuid == -1)
		squeue_unbind(sq);
	else
		squeue_bind(sq, cpuid);
}

/*
 * Move squeue from its current set to cpuid's set and bind to cpuid.
 */

int
ip_squeue_cpu_move(squeue_t *sq, processorid_t cpuid)
{
	cpu_t *cpu;
	squeue_set_t *set;

	if (sq->sq_state & SQS_DEFAULT)
		return (-1);

	ASSERT(MUTEX_HELD(&cpu_lock));

	cpu = cpu_get(cpuid);
	if (!CPU_ISON(cpu))
		return (-1);

	mutex_enter(&sqset_lock);
	set = cpu->cpu_squeue_set;
	if (set != NULL)
		ip_squeue_set_move(sq, set);
	mutex_exit(&sqset_lock);
	return ((set == NULL) ? -1 : 0);
}

/*
 * Used by IP to get the squeue associated with a ring. If the squeue isn't
 * yet bound to a CPU, and we're being called directly from the NIC's
 * interrupt, then we know what CPU we want to assign the squeue to, so
 * dispatch that task to a taskq.
 */

/* ARGSUSED */
squeue_t *
ip_squeue_get(void *arg)
{
	return (IP_SQUEUE_GET(CPU_PSEUDO_RANDOM()));
}

/*
 * Called when a CPU goes offline. It's squeue_set_t is destroyed, and all
 * squeues are unboudn and moved to the unbound set.
 */
static void
ip_squeue_set_destroy(cpu_t *cpu)
{
	int i;
	squeue_t *sqp, *lastsqp = NULL;
	squeue_set_t *sqs, *unbound = sqset_global_list[0];

	if ((sqs = cpu->cpu_squeue_set) == NULL)
		return;

	/* Move all squeues to unbound set */

	for (sqp = sqs->sqs_head; sqp; lastsqp = sqp, sqp = sqp->sq_next) {
		squeue_unbind(sqp);
		sqp->sq_set = unbound;
	}
	if (sqs->sqs_head) {
		lastsqp->sq_next = unbound->sqs_head;
		unbound->sqs_head = sqs->sqs_head;
	}

	/* Also move default squeue to unbound set */

	sqp = sqs->sqs_default;
	ASSERT(sqp != NULL);
	ASSERT((sqp->sq_state & (SQS_DEFAULT|SQS_ILL_BOUND)) == SQS_DEFAULT);

	sqp->sq_next = unbound->sqs_head;
	unbound->sqs_head = sqp;
	squeue_unbind(sqp);
	sqp->sq_set = unbound;

	for (i = 1; i < sqset_global_size; i++)
		if (sqset_global_list[i] == sqs)
			break;

	ASSERT(i < sqset_global_size);
	sqset_global_list[i] = sqset_global_list[sqset_global_size - 1];
	sqset_global_list[sqset_global_size - 1] = NULL;
	sqset_global_size--;

	kmem_free(sqs, sizeof (*sqs));
}

/*
 * Reconfiguration callback
 */
/* ARGSUSED */
static int
ip_squeue_cpu_setup(cpu_setup_t what, int id, void *arg)
{
	cpu_t *cp = cpu_get(id);

	ASSERT(MUTEX_HELD(&cpu_lock));
	mutex_enter(&sqset_lock);
	switch (what) {
	case CPU_CONFIG:
	case CPU_ON:
	case CPU_INIT:
	case CPU_CPUPART_IN:
		if (CPU_ISON(cp) && cp->cpu_squeue_set == NULL)
			cp->cpu_squeue_set = ip_squeue_set_create(cp->cpu_id);
		break;
	case CPU_UNCONFIG:
	case CPU_OFF:
	case CPU_CPUPART_OUT:
		if (cp->cpu_squeue_set != NULL) {
			ip_squeue_set_destroy(cp);
			cp->cpu_squeue_set = NULL;
		}
		break;
	default:
		break;
	}
	mutex_exit(&sqset_lock);
	return (0);
}
