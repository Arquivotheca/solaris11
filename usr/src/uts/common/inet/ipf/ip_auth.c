/*
 * Copyright (C) 1998-2003 by Darren Reed & Guido van Rooij.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * ip_auth.c module intercepts packets matching an auth rule and passes them to
 * userspace application. The application is supposed to decide what to do with
 * packet:
 *	drop it
 *	forward it
 *
 * The auth module creates so called auth request (ar). Among other data every
 * request keeps pointer to packet, which should be authorized. The requests
 * are kept in queue waiting for application's decision.
 *
 * There are those queues and lists used to manage requests in IPF stack
 * instance:
 *	ifs_auth_list	- keeps all auth requests
 *	ifs_auth_wq	- time out queue for requests. It keeps requests, which
 *			are waiting to be fetched by app. via ioctl().
 *	ifs_auth_rq	- time out queue for resolved requests. Every ar is
 *			moved from ifs_auth_wq to ifs_auth_rq once it is passed
 *			to userland application for processing.
 *	ifs_auth_req_htab	- hash table for auth requests. ar's are hashed
 *				by id.
 *	ifs_auth_fin_htab	- hash table for auth requests. ar's are hashed
 *				by packet data (fin - firewall information)
 *
 * The request is created by fr_newauth() function, which is called from
 * fr_check(). It is inserted into global list (ifs_auth_list), hash tables and
 * ifs_auth_wq. The packet from to request is assumed to be dropped since that
 * moment from fr_check() prespective.
 *
 * Once fr_newauth() creates request it triggers ifs_ipfauthwait event to
 * notify any pending ioctl() there is a packet ready to be passed to
 * userspace.
 *
 * The fr_authwait() function is ifs_ipfauthwait event consumer. Once event is
 * triggered and there is ioctl() operation waiting for completion, the first
 * auth request found in ifs_auth_wq, is copied to ioctl() buffer. The auth
 * request is then moved from ifs_auth_wq to ifs_auth_rq. The pending ioctl()
 * operation is completed then.
 *
 * The application 'writes' auth result through SIOCAUTHR ioctl() operation.
 * It is processed by fr_authreply() function. Function looks up a matching
 * auth request to copy result there. Once result is stored the fr_authreply()
 * reinjects packet bound to request back to inbound queue.
 *
 * The reinjected packet is intercepted by fr_check(). The fr_check() calls
 * fr_checkauth() in very early stage. fr_checkauth() tries to look up auth
 * response for packet in ifs_auth_fin_htab. If there are any extra actions to
 * be performed (keep state, use rule group, ...), then they are performed by
 * fr_checkauth() function. The fr_checkauth() returns a rule bound to auth
 * request to fr_check(). fr_check() then drops/forwards packet as requested by
 * auth result.
 *
 * Synchronization
 * There is an ifs_ipf_auth RW-lock, which protects data consistency of global
 * lists used in auth module. Any operation, which performs look up/read
 * operation must grab this lock for 'R'. Any operation, which inserts/removes
 * auth requests from any of those lists must grab this lock exclusively ('W').
 *
 * We don't need to grab the lock when we are moving entry from ifs_auth_wq to
 * ifs_auth_rq, since every time out queue uses dedicated mutex to serialize
 * access.
 *
 * Each auth request instance (frauth_priv_t) uses its owon mutex, which
 * protects data consistency (result, reference count).
 *
 * Furthermore there are two events:
 *	ifs_auth_ictlmx (protected by ifs_auth_ictlmx mutex)
 *	ifs_auth_ictl_done (protected by ifs_ipf_authmx)
 *
 * The ifs_auth_ictlmx allows fr_newauth() to notify fr_authwait() there is a
 * packet waiting for authorization.
 *
 * The ifs_auth_ictl_done synchronizes fr_authunload() with fr_authwait().
 * fr_authwait() must notify fr_authunload() function it is done, so
 * fr_authunload() can proceed with destruction of request queues.
 *
 * Reference count handling
 * Every auth request instance is using reference count. fr_auth_deref()
 * decrements reference count. Once reference count reaches zero,
 * fr_auth_deref() will release memory occupied by frauth_priv_t instance.
 * The entry must be already removed from all global lists when it is being
 * removed.
 *
 * The auth module provides fr_auth_remove() function, which unlinks
 * frauth_priv_t instance from all lists. The caller of fr_auth_remove() will
 * get a reference to particular instance just unlinked. It is fr_auth_remove()
 * caller responsibility to call fr_auth_deref() explicitly to drop a reference
 * count.
 */

#if defined(KERNEL) || defined(_KERNEL)
#undef KERNEL
#undef _KERNEL
#define	KERNEL	1
#define	_KERNEL	1
#endif

#include <sys/errno.h>
#include <sys/types.h>
#include <sys/file.h>
#include <net/if.h>
#include <netinet/in.h>

#ifndef _KERNEL
#include <stdlib.h>
#include <string.h>
#endif

#include "netinet/ip_compat.h"
#include "netinet/ipf_stack.h"
#include "netinet/ip_fil.h"
#include "netinet/ip_auth.h"
/* END OF INCLUDES */


#define	AUTH_REF_INC(ar)	do {					\
		ASSERT(MUTEX_HELD((kmutex_t *)&ar->fri_lock));		\
		(ar)->fri_ref++;					\
		DTRACE_PROBE1(auth_ref, frauth_priv_t *, (ar));		\
		_NOTE(CONSTCOND)					\
	} while (0)

#define	AUTH_REF_DEC(ar)	do {					\
		ASSERT(MUTEX_HELD((kmutex_t *)&ar->fri_lock));		\
		(ar)->fri_ref--;					\
		DTRACE_PROBE1(auth_deref, frauth_priv_t *, (ar));	\
		_NOTE(CONSTCOND)					\
	} while (0)

int fr_authgeniter __P((ipftoken_t *, ipfgeniter_t *, ipf_stack_t *));

#define	FLAG_MASK	~(FI_DONTCACHE|FI_NATED|FI_NEWNAT)

/*
 * function will do the platform dependent mblock adjustment.
 */
static mb_t *
fr_adjust_pkt(mb_t *m, fr_info_t *fin)
{
	mb_t	*rv = *(mb_t **)fin->fin_mp;
#if defined(_KERNEL) && defined(MENTAT)
	qpktinfo_t *qpi = fin->fin_qpi;
#endif

#if defined(_KERNEL)
	m->b_rptr -= qpi->qpi_off;
	rv = m;
#endif
	return (rv);
}

/*
 * Grabs a reference to auth request entry.
 */
static frauth_priv_t *
fr_auth_ref(frauth_priv_t *auth_req)
{
	MUTEX_ENTER(&auth_req->fri_lock);
	AUTH_REF_INC(auth_req);
	MUTEX_EXIT(&auth_req->fri_lock);

	return (auth_req);
}

/*
 * Drops reference count of auth_req entry. When ref count reaches zero, it
 * will destroy entry. The function relies on auth_req entry has been
 * previously removed from all queues.
 */
static void
fr_auth_deref(frauth_priv_t *auth_req)
{
	if (auth_req != NULL) {
		MUTEX_ENTER(&auth_req->fri_lock);
		AUTH_REF_DEC(auth_req);

		if (auth_req->fri_ref == 0) {
			MUTEX_EXIT(&auth_req->fri_lock);
			MUTEX_DESTROY(&auth_req->fri_lock);

			if (auth_req->fri_pkt != NULL) {
				FREE_MB_T(auth_req->fri_pkt);
			}

			DTRACE_PROBE1(entry_destroyed,
			    frauth_priv_t *, auth_req);

			KFREE(auth_req);

		} else {
			DTRACE_PROBE1(entry_derefed,
			    frauth_priv_t *, auth_req);
			MUTEX_EXIT(&auth_req->fri_lock);
		}
	}
}

/*
 * Removes auth entry from all hash tables and time out queues.  It leaves
 * entry in global list. The entry gets removed from global list by
 * fr_auth_deref().  Assumes auth lock (ifs_ipf_auth) is held exclusively.
 *
 * Returns reference to auth_req on success, caller is supposed to call
 * fr_auth_deref then. The NULL return value means the caller attempts to
 * remove entry, which's been removed already.
 */
static frauth_priv_t *
fr_auth_remove(frauth_priv_t *auth_req, ipf_stack_t *ifs)
{
	frauth_priv_t *rv;

	MUTEX_ENTER(&auth_req->fri_lock);
	if (auth_req->fri_removed == 0) {
		fr_deletequeueentry(&auth_req->fri_tq);
		LIST_REMOVE(auth_req, fri_requests);
		LIST_REMOVE(auth_req, fri_answers);
		LIST_REMOVE(auth_req, fri_glist);
		ifs->ifs_fr_authcnt--;
		ifs->ifs_fr_authstats.fas_entries--;
		rv = auth_req;
		auth_req->fri_removed = 1;
	} else {
		rv = NULL;
	}
	MUTEX_EXIT(&auth_req->fri_lock);

	return (rv);
}

/*
 * Constructor for auth entry, the entry is returned with ref. count set to -1.
 */
static frauth_priv_t *
fr_auth_new(fr_info_t *fin, mb_t *m)
{
	frauth_priv_t *rv;
	ipf_stack_t *ifs = fin->fin_ifs;	/* needed by COPYIFNAME() */

	KMALLOCS(rv, frauth_priv_t *, sizeof (frauth_priv_t));
	if (rv != NULL) {
		bzero(rv, sizeof (frauth_priv_t));
		bcopy((char *)fin, (char *)&rv->fri_info, sizeof (fr_info_t));
		/*
		 * We need to mask out all flags, which might get set after
		 * fr_checkauth() is called. Failing to do that, may prevent
		 * fr_checkauth() to find authorized packet.
		 */
		rv->fri_info.fin_fi.fi_flx = fin->fin_flx & FLAG_MASK;
		MUTEX_INIT(&rv->fri_lock, "Auth Entry Lock");
		rv->fri_pkt = fr_adjust_pkt(m, fin);

		COPYIFNAME(fin->fin_ifp, rv->fri_ifname, fin->fin_v);
	}

	return (rv);
}

/*
 * Constructor for time-out queue. The q is pointer to memory where time-out
 * queue will be constructed.
 */
static void
fr_authinitq(ipftq_t *q, char *mx_name, ipf_stack_t *ifs)
{
	q->ifq_ttl = ifs->ifs_fr_authq_ttl;
	q->ifq_head = NULL;
	q->ifq_tail = &q->ifq_head;
	q->ifq_next = NULL;
	q->ifq_pnext = NULL;
	q->ifq_ref = 1;
	q->ifq_flags = 0;
	MUTEX_NUKE(&q->ifq_lock);
	MUTEX_INIT(&q->ifq_lock, mx_name);
}

/*
 * Flushes all auth requests. Function expects ifs_ipf_auth lock is grabbed
 * exclusively.
 */
static int
fr_authflush(ipf_stack_t *ifs)
{
	frauth_priv_t *ar;

	while (!LIST_EMPTY(&ifs->ifs_auth_list)) {
		ar = LIST_FIRST(&ifs->ifs_auth_list);
		fr_auth_deref(fr_auth_remove(ar, ifs));
	}

	return (0);
}

/*
 * Function will compute hash table size.
 *
 * It's based on the computation of square root of max limit configured by
 * user. The hash table size will be (2 * sqrt(max)).
 *
 * The algorithm is based on article by Jack W. Crenshaw. The article presented
 * methods of sqrt() computation in NASA during 60's. The details are attached
 * at bugster with CR 6914077, see intsqrt.pdf attachment.
 *
 */
static u_32_t
fr_get_htab_size(u_32_t max)
{
	u_32_t	rem = 0;
	u_32_t	root = 0;
	u_32_t	i = 0;

	for (i = 0; i < (sizeof (max) << 2); i++) {
		root <<= 1;
		rem = (rem << 2) + (max >> 30);
		max <<= 2;
		root++;

		if (root <= rem) {
			rem -= root;
			root++;
		} else {
			root--;
		}
	}

	DTRACE_PROBE2(sqrt, u_32_t, max, u_32_t, root);
	/*
	 * to get a square root of max we are supposed to perform last step:
	 * 	root >> 1.
	 * since we want to have a result in form (2 * sqrt(max)), returning
	 * root is just fine.
	 *
	 * To improve hashing, we turn even roots odd.
	 */

	root += ((root & 0x1) == 1) ? 0 : 1;

	return (root);
}

/*
 * Initializes auth module. The ipf_global lock is held exclusively, while the
 * function is being called.
 */
int
fr_authinit(ipf_stack_t *ifs)
{
	unsigned int	i;

	ifs->ifs_fr_authsize = fr_get_htab_size(ifs->ifs_fr_authmax);
	ifs->ifs_auth_unloading = 0;
	ifs->ifs_authwait_entered = 0;

	KMALLOCS(ifs->ifs_auth_req_htab, frauth_htab_t *,
	    ifs->ifs_fr_authsize * sizeof (frauth_htab_t));

	if (ifs->ifs_auth_req_htab == NULL)
		return (-1);

	KMALLOCS(ifs->ifs_auth_fin_htab, frauth_htab_t *,
	    ifs->ifs_fr_authsize * sizeof (frauth_htab_t));

	if (ifs->ifs_auth_fin_htab == NULL) {
		KFREE(ifs->ifs_auth_req_htab);
		return (-1);
	}

	/*
	 * initialize global list and hash tables
	 */
	LIST_INIT(&ifs->ifs_auth_list);
	for (i = 0; i < ifs->ifs_fr_authsize; i++) {
		LIST_INIT(&ifs->ifs_auth_req_htab[i]);
		LIST_INIT(&ifs->ifs_auth_fin_htab[i]);
	}

	/*
	 * initialize time out queues for requests and answers
	 */
	fr_authinitq(&ifs->ifs_auth_wq, "Auth wait queue mutex", ifs);
	fr_authinitq(&ifs->ifs_auth_rq, "Auth result queue mutex", ifs);

	MUTEX_INIT(&ifs->ifs_ipf_authmx, "ipf auth condvar mutex");
	MUTEX_INIT(&ifs->ifs_auth_ictlmx, "ipf auth ictl cndvar mutex");
	RWLOCK_INIT(&ifs->ifs_ipf_auth, "ipf IP User-Auth rwlock");
#if SOLARIS && defined(_KERNEL)
	cv_init(&ifs->ifs_ipfauthwait, "ipf auth condvar", CV_DRIVER, NULL);
	cv_init(&ifs->ifs_auth_ictl_done, "ipf auth ictl condvar",
	    CV_DRIVER, NULL);
#endif
	ifs->ifs_fr_auth_init = 1;
	ifs->ifs_fr_authcnt = 0;

	return (0);
}


/*
 * The function computes key from fin data, the key will be used to look up
 * auth request in fr_checkauth() function.
 */
static u_32_t
fr_get_fin_hkey(fr_info_t *fin)
{
	u_32_t rv;

	rv = fin->fin_ip->ip_id;
	rv |= fin->fin_ip->ip_ttl << 24;
	rv += fin->fin_fi.fi_src.in4.s_addr;
	rv ^= fin->fin_fi.fi_dst.in4.s_addr;
	rv = rv % fin->fin_ifs->ifs_fr_authsize;

	return (rv);
}

/*
 * The function is solely used in sdt probe. It returns the offset, where a, b
 * differ. The offset is passed to sdt probe 'candidate_diff'.
 */
static int
fr_get_diff(const char *a, const char *b, size_t len)
{
	unsigned int ret_val = 0;
	unsigned int i = 0;

	for (i = 0; i < len; i++) {
		if (a[i] != b[i]) {
			ret_val = i;
			break;
		}
	}

	return (ret_val);
}

static int
fr_chkgrp(fr_info_t *fin, frauth_priv_t *ar, u_32_t pass)
{
	frgroup_t *rg;
	ipf_stack_t *ifs = fin->fin_ifs;
	frentry_t *old_rule;

	rg = fr_findgroup(ar->fri_rgroup, (minor_t)ar->fri_unit,
	    ifs->ifs_fr_active,
	    /* we don't care where to add a next rule -> passing NULL */
	    NULL,
	    ifs);

	/*
	 * No group found or there are no rules in group
	 */
	if ((rg == NULL) || (rg->fg_head == NULL))
		return (pass);

	fin->fin_fr = rg->fg_head;
	old_rule = fin->fin_fr;
	pass = fr_scanlist(fin, pass);

	if (fin->fin_fr == NULL)
		fin->fin_fr = old_rule;

	return (pass);
}

/*
 * Function fr_auth_pkt_lookup() finds matching auth entry for given packet
 * data fin. Function grabs read lock for ifs_ipf_auth. The lock is released
 * on return. If matching entry is found a reference is returned, NULL
 * otherwise.
 */
static frauth_priv_t *
fr_auth_pkt_lookup(fr_info_t *fin, ipf_stack_t *ifs)
{
	u_32_t hkey;
	frauth_priv_t *ret_val = NULL;

	READ_ENTER(&ifs->ifs_ipf_auth);

	if (ifs->ifs_fr_authcnt == 0) {
		RWLOCK_EXIT(&ifs->ifs_ipf_auth);
		return (NULL);
	}

	/*
	 * look up auth request, if found bump a ref count
	 */
	hkey = fr_get_fin_hkey(fin);
	ret_val = LIST_FIRST(&ifs->ifs_auth_fin_htab[hkey]);
	while (ret_val != NULL) {
		DTRACE_PROBE2(candidate_found, frauth_priv_t *, ret_val,
		    fr_info_t *, fin);
		MUTEX_ENTER(&ret_val->fri_lock);
		if ((ret_val->fri_resolved) &&
		    (bcmp((char *)fin, (char *)&ret_val->fri_info,
		    FI_CSIZE) == 0)) {
			/*
			 * Get reference for successful look up. count here, we
			 * already have mutex locked.  Reference comes handy
			 * here, because we are going to drop lock ifs_ipf_auth
			 * in few moments.
			 */
			AUTH_REF_INC(ret_val);
			MUTEX_EXIT(&ret_val->fri_lock);
			break;
		} else {
			DTRACE_PROBE3(candidate_diff,
			    fr_info_t *, fin,
			    fr_info_t *, &ret_val->fri_info,
			    int, fr_get_diff((char *)fin,
			    (char *)&ret_val->fri_info, FI_CSIZE));
			MUTEX_EXIT(&ret_val->fri_lock);
			ret_val = LIST_NEXT(ret_val, fri_requests);
		}
	}

	RWLOCK_EXIT(&ifs->ifs_ipf_auth);

	if (ret_val == NULL) {
		DTRACE_PROBE2(result_nomatch, u_32_t, hkey, fr_info_t *, fin);
	}

	return (ret_val);
}

/*
 * Check if a packet is authorized.
 */
frentry_t *
fr_checkauth(fr_info_t *fin, u_32_t *passp)
{
	ipstate_t *is;
	fr_info_t *fin_a;
	frentry_t *fr = NULL;
	frauth_priv_t *ar = NULL;
	u_32_t pass;
	int out;
	ipf_stack_t *ifs = fin->fin_ifs;

	if ((ar = fr_auth_pkt_lookup(fin, ifs)) == NULL)
		return (NULL);

	/*
	 * We must grab auth lock exclusively here, since we are going to
	 * remove resolved auth request from all lists and queues.
	 */
	WRITE_ENTER(&ifs->ifs_ipf_auth);
	if (fr_auth_remove(ar, ifs) == NULL) {
		/*
		 * We attempted to remove already removed request.  It could
		 * expire between look up and moment we've grabed ifs_ipf_auth
		 * lock exclusively
		 *
		 * Anyway we have to give up further processing now.  We will
		 * just drop reference count we got from successful look up.
		 */
		fr_auth_deref(ar);
		RWLOCK_EXIT(&ifs->ifs_ipf_auth);
		return (NULL);
	}

	/*
	 * At this point we have two references to ar:
	 *	the first one is for successful look up
	 *	second one is for successful remove from global lists
	 *
	 * we will drop one of them.
	 */
	fr_auth_deref(ar);
	/*
	 * We are done with auth module lists, so we can drop auth module lock
	 * here.
	 */
	RWLOCK_EXIT(&ifs->ifs_ipf_auth);

	fin_a = &ar->fri_info;
	DTRACE_PROBE1(result_found, frauth_priv_t *, ar);

	if (ar->fri_rgroup[0] != '\0') {
		DTRACE_PROBE1(use_rgroup, frauth_priv_t *, ar);
		pass = fr_chkgrp(fin, ar, ar->fri_pass);
		/*
		 * The fr_scanlist() called in fr_chkgrp() function adds
		 * state/fragentry if matching rule has a FR_QUICK flag. In
		 * such case we have to check presence of FR_QUICK. If FR_QUICK
		 * flag is present, we must reset FR_KEEPSTATE, FR_KEEPFRAG to
		 * avoid attempt of adding state entry again.
		 */

		if (pass & FR_QUICK)
			pass &= ~(FR_KEEPSTATE | FR_KEEPFRAG);

		/*
		 * Use rule found in fr_scanlist() called by fr_chkgrp()
		 */
		fr = fin->fin_fr;
	}

	/*
	 * If we failed to find rule for whatever reason (no group found, group
	 * does not contain rule, ...), then we have to use rule bound to auth
	 * request.
	 */
	if (fr == NULL) {
		/*
		 * It is O.K. to return a pointer to FW rule, since we are sure
		 * the rule exists (see fr_authflush_rule()).
		 */
		fr = fin_a->fin_fr;
		pass = ar->fri_pass;
	} else {
		/*
		 * Set matching rule to fin bound to auth request
		 */
		fin_a->fin_fr = fr;
	}

	/*
	 * The FR_AUTH action has to be turned into FR_BLOCK here, to avoid a
	 * feeback loop.
	 */
	if ((pass == 0) || (FR_ISAUTH(pass)))
		pass = FR_BLOCK;

	if ((pass & FR_KEEPSTATE) || ((pass & FR_KEEPFRAG) &&
	    (ar->fri_info.fin_flx & FI_FRAG))) {
		is = fr_addstate(fin_a, NULL, 0);
		out = fin_a->fin_out;
		if (is != NULL) {
			ifs->ifs_frstats[out].fr_ads++;
			/*
			 * This will override the action which might came from
			 * rule bound to auth request (ar).  We need action
			 * coming either from user app, or rule found in group
			 * here.
			 */
			is->is_pass = pass;
		} else
			ifs->ifs_frstats[out].fr_bads++;
	}

	ifs->ifs_fr_authstats.fas_hits++;

	/*
	 * Application can pass any flags. we have to sanitize them before we
	 * return to fr_check
	 */
	pass &= ~(FR_FASTROUTE|FR_DUP|FR_KEEPSTATE|FR_KEEPFRAG);
	if (passp != NULL)
		*passp = pass;

	DTRACE_PROBE1(use_rule, frentry_t *, fr);

	fr_auth_deref(ar);

	return (fr);
}

/*
 * Function wakes up fr_authwait ioctl()
 */
static void
fr_wake_authwait(ipf_stack_t *ifs)
{
#ifdef _KERNEL
	cv_signal(&ifs->ifs_ipfauthwait);
#endif
}

/*
 * Inserts entry into hash table and time out queue.  Function assumes
 * auth lock is held exclusively.
 */
static void
fr_insert_auth_request(frauth_priv_t *ar, ipf_stack_t *ifs)
{
	u_32_t	fin_hkey, req_hkey;

	LIST_INSERT_HEAD(&ifs->ifs_auth_list, ar, fri_glist);
	/*
	 * Since auth lock is held exclusively, we don't need to bother
	 * with auth_req->fri_lock mutex
	 */
	ar->fri_ref = 1;

	/*
	 * insert it into time queue
	 */
	fr_queueappend(&ar->fri_tq, &ifs->ifs_auth_wq, ar, ifs);

	/*
	 * insert it into hash table for requests
	 */
	req_hkey = ar->fri_key % ifs->ifs_fr_authsize;
	LIST_INSERT_HEAD(&ifs->ifs_auth_req_htab[req_hkey], ar, fri_requests);

	/*
	 * insert it into hash table for answers
	 */
	fin_hkey = fr_get_fin_hkey(&ar->fri_info);
	LIST_INSERT_HEAD(&ifs->ifs_auth_fin_htab[fin_hkey], ar, fri_answers);

	DTRACE_PROBE3(req_added, frauth_priv_t *, ar, u_32_t, req_hkey,
	    u_32_t, fin_hkey);
}

/*
 * Add a new auth request for a packet. Once auth request for packet is queued,
 * the function will wake up fr_authwait(), which will pass request to
 * userspace app.
 */
int
fr_newauth(mb_t *m, fr_info_t *fin)
{
	frauth_priv_t *ar;
	ipf_stack_t *ifs = fin->fin_ifs;
	int rv;

	WRITE_ENTER(&ifs->ifs_ipf_auth);
	if (ifs->ifs_fr_authcnt >= ifs->ifs_fr_authmax) {
		ifs->ifs_fr_authstats.fas_nospace++;
		RWLOCK_EXIT(&ifs->ifs_ipf_auth);
		DTRACE_PROBE1(queue_full, size_t, ifs->ifs_fr_authcnt);
		return (0);	/* limit exceeded */
	}
	ifs->ifs_fr_authcnt++;
	RWLOCK_EXIT(&ifs->ifs_ipf_auth);

	/*
	 * at this point we are sure we will fit into queue
	 */
	ar = fr_auth_new(fin, m);

	if (ar != NULL) {
		/*
		 * Grab again auth lock exclusively, since we are going to add
		 * entry into lists
		 */
		WRITE_ENTER(&ifs->ifs_ipf_auth);
		ifs->ifs_fr_authstats.fas_added++;
		ifs->ifs_fr_authstats.fas_entries++;
		ar->fri_key = ifs->ifs_fr_authstats.fas_added;
		fr_insert_auth_request(ar, ifs);
		fr_wake_authwait(ifs);
		RWLOCK_EXIT(&ifs->ifs_ipf_auth);
		rv = 1;
	} else {
		DTRACE_PROBE(no_mem);
		WRITE_ENTER(&ifs->ifs_ipf_auth);
		ifs->ifs_fr_authcnt--;
		RWLOCK_EXIT(&ifs->ifs_ipf_auth);
		rv = 0;
	}

	return (rv);
}

#ifdef _KERNEL
static int
fr_wait_sig(kcondvar_t *cv, ipfmutex_t *m)
{
	int	error = 0;

	MUTEX_ENTER(m);
	error = cv_wait_sig(cv, &m->ipf_lk);
	switch (error) {
		case 0	:
			error = EINTR;
			break;
		case -1 :
			error = EFAULT;
			break;
		default:
			error = 0;
			break;
	}
	MUTEX_EXIT(m);

	return (error);
}
#endif

/*
 * Waits for signal. It put's AUTHW ioctl() to sleep, while waiting for packet
 * to be authorized.
 */
static int
fr_wait_for_areq(ipf_stack_t *ifs)
{
#ifdef	_KERNEL
	return (fr_wait_sig(&ifs->ifs_ipfauthwait, &ifs->ifs_ipf_authmx));
#else
	return (0);
#endif
}


/*
 * Function waits for signal from AUTHW ioctl() (fr_authwait() function).
 * The fr_authwait() will emit signal anytime it returns.
 */
static int
fr_wait_for_ioctl(ipf_stack_t *ifs)
{
#ifdef	_KERNEL
	return (fr_wait_sig(&ifs->ifs_auth_ictl_done, &ifs->ifs_auth_ictlmx));
#else
	return (0);
#endif
}

/*
 * Function sends signal to notify the fr_authwait() is done
 */
static void
fr_notify_ioctl_done(ipf_stack_t *ifs)
{
#if defined(_KERNEL)
	cv_signal(&ifs->ifs_auth_ictl_done);
#endif
}
/*
 * The function process ioctl(2) request from auth app. The workflow is as
 * follows:
 *	function will read request from userspace
 *
 *	it will check if there is any packet requesting authorization
 *
 *	if there is such packet, then its data is copied into ioctl
 *	response and function returns -> ioctl(2) is handled (a)
 *
 *	if there is no such packet, then function will wait for signal from
 *	fr_newauth().  Once packet requesting authorization will arrive, the
 *	function will be woken up by signal. It will perform step (a) and
 *	ioctl(2) call will return. (b)
 *
 * Function assumes ipf_global lock is held non-exclusively.
 *
 * Also fr_authwait() must be synchronized with fr_authunload(), the
 * synchronisation is described there.
 */
int
fr_authwait(caddr_t data, ipf_stack_t *ifs)
{
	frauth_priv_t	*ar;
	frauth_t	ioctl_buf;
	frauth_t	*io_data = &ioctl_buf;
	int		error, len;
	ipftq_t		*wait_q = &ifs->ifs_auth_wq;
	ipftq_t		*result_q = &ifs->ifs_auth_rq;

	if (ifs->ifs_fr_auth_init == 0)
		return (EFAULT);

	error = fr_inobj(data, io_data, IPFOBJ_FRAUTH);
	if (error != 0)
		return (EFAULT);

	/*
	 * The ioctl(2) request is copied into local buf. If there was no error
	 * we can try to process ioctl(2)
	 */
	WRITE_ENTER(&ifs->ifs_ipf_auth);

	/*
	 * recheck if we are still running
	 */
	if (ifs->ifs_fr_auth_init == 0) {
		RWLOCK_EXIT(&ifs->ifs_ipf_auth);
		return (EFAULT);
	}
	/*
	 * We need to mark the authwait is entered, this flag will be checked
	 * in fr_authunload()
	 */
	ifs->ifs_authwait_entered = 1;
	/*
	 * Since that point it is O.K. to hold ifs_ipf_auth non-exclusively.
	 */
	MUTEX_DOWNGRADE(&ifs->ifs_ipf_auth);

	while (error == 0) {
		/*
		 * Check whether the IPF is about to be exited, if so notify
		 * fr_authunload() the ioctl is done and return.
		 */
		if (ifs->ifs_auth_unloading == 1) {
			RWLOCK_EXIT(&ifs->ifs_ipf_auth);
			WRITE_ENTER(&ifs->ifs_ipf_auth);
			ifs->ifs_authwait_entered = 0;
			/* Also notify fr_authunload() we are done */
			fr_notify_ioctl_done(ifs);
			DTRACE_PROBE(notify_ioctl_done);
			RWLOCK_EXIT(&ifs->ifs_ipf_auth);
			return (EFAULT);
		}

		if (wait_q->ifq_head != NULL) {
			/* there is some auth request follow branch (a) */
			ar = (frauth_priv_t *)wait_q->ifq_head->tqe_parent;
			error = fr_outobj(
			    data, &ar->fri_auth, IPFOBJ_FRAUTH);

			if (io_data->fra_len != 0 &&
			    io_data->fra_buf != NULL && error == 0) {
				/*
				 * Copy packet contents out to user space if
				 * requested.  Bail on an error.
				 */
				len = MSGDSIZE(ar->fri_pkt);
				if (len > io_data->fra_len)
					len = io_data->fra_len;
				io_data->fra_len = len;
				/*
				 * there used to be for loop. I believe the
				 * purpose of the forloop was to copy out
				 * entire packet gathered from mblock chain.
				 * unfortunately the for loop failed to that.
				 *
				 * I'm going to turn things into more simple
				 * form here.
				 */
				error = copyoutptr(MTOD(ar->fri_pkt, char *),
				    io_data->fra_buf,
				    MIN(M_LEN(ar->fri_pkt), len));
			}

			/*
			 * If we've copied out auth/packet data to userbuf
			 * successfuly, then we must move auth request 'fri'
			 * from wait queue to ioctl queue
			 */
			if (error == 0) {
				fr_movequeue(&ar->fri_tq, wait_q, result_q,
				    ifs);
				DTRACE_PROBE1(req_pending, frauth_priv_t *, ar);
			} else {
				DTRACE_PROBE1(req_qfail, frauth_priv_t *, ar)
			}

			RWLOCK_EXIT(&ifs->ifs_ipf_auth);

			/*
			 * Quit loop and return
			 */
			break;
		} else {
			/*
			 * We are going to sleep, we have to drop all locks
			 * including a ipf_global, which is held
			 * non-exclusively. Once fr_wait_for_areq() resumes, we
			 * must grab these locks again.
			 */
			RWLOCK_EXIT(&ifs->ifs_ipf_auth);
			RWLOCK_EXIT(&ifs->ifs_ipf_global);
			error = fr_wait_for_areq(ifs);
			READ_ENTER(&ifs->ifs_ipf_global);

			/*
			 * Don't grab auth lock, when you are going to bail out
			 * from loop.
			 */
			if (error == 0)
				READ_ENTER(&ifs->ifs_ipf_auth);
			else
				break;
		}
	}

	/*
	 * We have to mark, we are leaving fr_authwait() function.
	 */
	WRITE_ENTER(&ifs->ifs_ipf_auth);
	ifs->ifs_authwait_entered = 0;
	/*
	 * If fr_authunload() is in progress, we have to wake it up here, since
	 * it was waiting for us to complete.
	 */
	if (ifs->ifs_auth_unloading == 1) {
		fr_notify_ioctl_done(ifs);
		DTRACE_PROBE(notify_ioctl_at_exit);
	}
	RWLOCK_EXIT(&ifs->ifs_ipf_auth);

	return (error);
}


/*
 * Processes response from application. If matching request is found for the
 * response, then packet bound to request will be send, so IPF will be able to
 * intercept such packet again and find the response in fr_checkauth()
 * function.
 */
int
fr_authreply(caddr_t data, ipf_stack_t *ifs)
{
	frauth_priv_t	*ar = NULL;
	frauth_t	ioctl_buf;
	frauth_t	*io_data = &ioctl_buf;
	mb_t		*pkt = NULL;
	int		error;
	u_32_t		hkey;

	if (ifs->ifs_fr_auth_init == 0)
		return (EINVAL);

	error = fr_inobj(data, io_data, IPFOBJ_FRAUTH);
	if (error != 0)
		return (error);

	/*
	 * Again, we will be just moving auth request between queues, we are
	 * not going to remove it from hash tables, nor from global list.
	 */
	READ_ENTER(&ifs->ifs_ipf_auth);

	/*
	 * Check module is still alive
	 */
	if (ifs->ifs_fr_auth_init == 0) {
		RWLOCK_EXIT(&ifs->ifs_ipf_auth);
		return (EINVAL);
	}

	/*
	 * lookup request
	 */
	hkey = io_data->fra_key % ifs->ifs_fr_authsize;
	ar = LIST_FIRST(&ifs->ifs_auth_req_htab[hkey]);
	while ((ar != NULL) && (ar->fri_key != io_data->fra_key))
		ar = LIST_NEXT(ar, fri_requests);

	if (ar != NULL) {
		/* mark request as resolved. */
		MUTEX_ENTER(&ar->fri_lock);
		ar->fri_pass = io_data->fra_pass;
		pkt = ar->fri_pkt;
		ar->fri_pkt = NULL;
		ar->fri_resolved = 1;
		(void) strncpy(ar->fri_rgroup, io_data->fra_rgroup,
		    FR_GROUPLEN);
		MUTEX_EXIT(&ar->fri_lock);

		DTRACE_PROBE1(req_resolved, frauth_priv_t *, ar);
	} else {
		RWLOCK_EXIT(&ifs->ifs_ipf_auth);
		DTRACE_PROBE1(req_notfound, frauth_priv_t *, ar);
		return (ESRCH);
	}

	RWLOCK_EXIT(&ifs->ifs_ipf_auth);

	/*
	 * Re-insert the packet back into the packet stream flowing through the
	 * kernel in a manner that will mean IPFilter sees the packet again.
	 * This is not the same as is done with fastroute, deliberately, as we
	 * want to resume the normal packet processing path for it.
	 */
#ifdef	_KERNEL
	if ((pkt != NULL) && (ar->fri_info.fin_out != 0)) {
		error = ipf_inject(&ar->fri_info, pkt);
		if (error != 0) {
			error = ENOBUFS;
			ifs->ifs_fr_authstats.fas_sendfail++;
		} else {
			ifs->ifs_fr_authstats.fas_sendok++;
		}
	} else if (pkt != NULL) {
		error = ipf_inject(&ar->fri_info, pkt);
		if (error != 0) {
			error = ENOBUFS;
			ifs->ifs_fr_authstats.fas_quefail++;
		} else {
			ifs->ifs_fr_authstats.fas_queok++;
		}
	} else {
		error = EINVAL;
	}

	if (error != 0) {
		DTRACE_PROBE1(pkt_sendfail, frauth_priv_t *, ar);
	}

#endif /* _KERNEL */

	return (error);
}


int
fr_auth_ioctl(caddr_t data, ioctlcmd_t cmd, int mode, int uid,
    void *ctx, ipf_stack_t *ifs)
{
	int i, error = 0;

	switch (cmd) {
		case SIOCGENITER : {
			ipftoken_t *token;
			ipfgeniter_t iter;

			error = fr_inobj(data, &iter, IPFOBJ_GENITER);
			if (error != 0)
				break;

			token = ipf_findtoken(IPFGENITER_AUTH, uid, ctx, ifs);
			if (token != NULL)
				error = fr_authgeniter(token, &iter, ifs);
			else
				error = ESRCH;
			RWLOCK_EXIT(&ifs->ifs_ipf_tokens);
		}
		break;
		case SIOCATHST:
			ifs->ifs_fr_authstats.fas_faelist = NULL;
			error = fr_outobj(data, &ifs->ifs_fr_authstats,
			    IPFOBJ_AUTHSTAT);
			break;

		case SIOCIPFFL:
			WRITE_ENTER(&ifs->ifs_ipf_auth);
			i = fr_authflush(ifs);
			error = copyoutptr((char *)&i, data, sizeof (i));
			RWLOCK_EXIT(&ifs->ifs_ipf_auth);
			break;

		case SIOCAUTHW:
			error = (mode & FWRITE) ?
			    fr_authwait(data, ifs) : EPERM;
			break;

		case SIOCAUTHR:
			error = (mode & FWRITE) ?
			    fr_authreply(data, ifs) : EPERM;
			break;
		default :
			error = EINVAL;
			break;
	}

	return (error);
}


/*
 * It frees all resources held by ip_auth module. The function must be
 * synchronized with fr_authwait(), which might be sleeping waiting for packet.
 * We are using the synchronisation protocol as follows:
 *	Once fr_authwait() is entered it sets flag ifs_authwait_entered to 1.
 *	The flag is set while ifs_ipf_auth is held exclusively. Whenever
 *	fr_authwait() exits it sets ifs_ipf_auth to zero, while holding
 *	ifs_ipf_auth exclusively.
 *
 *	There is a similar flag ifs_auth_unloading used in fr_authunload().
 *	The flag is altered while ifs_ipf_auth is held exclusively.
 *
 *	Whenever fr_authunload() finds out the fr_authwait() is entered,
 *	it will:
 *		raise ifs_auth_unloading to 1
 *		wake up fr_authwait() by sending a signal
 *		and wait until ifs_authwait_entered will be 0
 */
void
fr_authunload(ipf_stack_t *ifs)
{
	if (ifs->ifs_fr_auth_init != 0) {
		WRITE_ENTER(&ifs->ifs_ipf_auth);
		ifs->ifs_auth_unloading = 1;

		/*
		 * Wait for fr_authwait() to finish
		 */
		while (ifs->ifs_authwait_entered) {
			fr_wake_authwait(ifs);
			/*
			 * Release all locks, while waiting for signal. Note we
			 * are holding ipf_global exclusively this time. The
			 * ipf_global is grabbed exclusively at ipldetach(),
			 * which calls fr_deinitialize(), which in turn calls
			 * fr_authunload().
			 */
			RWLOCK_EXIT(&ifs->ifs_ipf_auth);
			RWLOCK_EXIT(&ifs->ifs_ipf_global);
			DTRACE_PROBE(waiting_for_ioctl);
			(void) fr_wait_for_ioctl(ifs);
			WRITE_ENTER(&ifs->ifs_ipf_global);
			WRITE_ENTER(&ifs->ifs_ipf_auth);
			DTRACE_PROBE(ioctl_done);
		}

		(void) fr_authflush(ifs);
		KFREES(ifs->ifs_auth_req_htab,
		    ifs->ifs_fr_authsize * sizeof (frauth_htab_t));
		KFREES(ifs->ifs_auth_fin_htab,
		    ifs->ifs_fr_authsize * sizeof (frauth_htab_t));

		ifs->ifs_auth_req_htab = NULL;
		ifs->ifs_auth_fin_htab = NULL;

#if SOLARIS && defined(_KERNEL)
		cv_destroy(&ifs->ifs_ipfauthwait);
		cv_destroy(&ifs->ifs_auth_ictl_done);
#endif
		MUTEX_DESTROY(&ifs->ifs_ipf_authmx);
		MUTEX_DESTROY(&ifs->ifs_auth_ictlmx);

		ifs->ifs_fr_auth_init = 0;

		RWLOCK_EXIT(&ifs->ifs_ipf_auth);
		RW_DESTROY(&ifs->ifs_ipf_auth);
	}
}

/*
 * Expires a given time-out que 'q'
 * ifs_ipf_auth is held exclusively.
 */
static void
fr_queue_expire(ipftq_t *q, ipf_stack_t *ifs)
{
	ipftqent_t *tqe, *tqn;

	for (tqn = q->ifq_head; ((tqe = tqn) != NULL); ) {
		if (tqe->tqe_die > ifs->ifs_fr_ticks)
			break;
		DTRACE_PROBE1(entry_expired,
		    frauth_priv_t *, (frauth_priv_t *)tqe->tqe_parent);
		tqn = tqe->tqe_next;
		fr_auth_deref(
		    fr_auth_remove((frauth_priv_t *)tqe->tqe_parent, ifs));
		ifs->ifs_fr_authstats.fas_expire++;
	}
}


/*
 * Slowly expire held auth records.  Timeouts are set
 * in expectation of this being called twice per second.
 */
void
fr_authexpire(ipf_stack_t *ifs)
{
	if (ifs->ifs_fr_auth_init == 1) {
		WRITE_ENTER(&ifs->ifs_ipf_auth);

		DTRACE_PROBE(expiring_wq);
		fr_queue_expire(&ifs->ifs_auth_wq, ifs);
		DTRACE_PROBE(expiring_rq);
		fr_queue_expire(&ifs->ifs_auth_rq, ifs);

		RWLOCK_EXIT(&ifs->ifs_ipf_auth);
	}
}

/*
 * Flush all auth entries, which are bound to particular
 * rule fr.
 */
int
fr_authflush_rule(frentry_t *fr, ipf_stack_t *ifs)
{
	frauth_priv_t *ar, *remove;
	int rv = 0;

	if (ifs->ifs_fr_auth_init == 1) {
		WRITE_ENTER(&ifs->ifs_ipf_auth);

		ar = LIST_FIRST(&ifs->ifs_auth_list);
		while (ar != NULL) {
			if (ar->fri_info.fin_fr == fr) {
				ar->fri_info.fin_fr = NULL;
				remove = ar;
				ar = LIST_NEXT(ar, fri_glist);
				fr_auth_deref(fr_auth_remove(remove, ifs));
				rv++;
			} else {
				ar = LIST_NEXT(ar, fri_glist);
			}
		}

		RWLOCK_EXIT(&ifs->ifs_ipf_auth);
	}

	return (rv);
}

void
fr_auth_free_token(ipftoken_t *token)
{
	frauth_priv_t	*ar = token->ipt_data;

	if (ar != NULL)
		fr_auth_deref(ar);

	token->ipt_data = NULL;
}

/*
 * Function:	fr_authgeniter
 * Returns:	int - 0 == success, else error
 * Parameters:	token(I) - pointer to ipftoken structure
 *		itp(I)   - pointer to ipfgeniter structure
 */
int
fr_authgeniter(ipftoken_t *token, ipfgeniter_t *itp, ipf_stack_t *ifs)
{
	frauth_priv_t *ar, *next, zero;
	int error;
	char *dst;

	if (itp->igi_data == NULL)
		return (EFAULT);

	if (itp->igi_nitems != 1)
		return (EINVAL);

	if (itp->igi_type != IPFGENITER_STATE)
		return (EINVAL);

	error = 0;
	dst = itp->igi_data;
	ar = token->ipt_data;
	READ_ENTER(&ifs->ifs_ipf_auth);

	/*
	 * Get "previous" entry from the token, and find the next entry
	 * to be processed.
	 */
	if (ar == NULL) {
		/*
		 * The ioctl() retrives the first auth entry.
		 */
		ar = fr_auth_ref(LIST_FIRST(&ifs->ifs_auth_list));
		next = fr_auth_ref(LIST_NEXT(ar, fri_glist));
	} else {
		/*
		 * The ioctl() retrives second, thrid, ... auth entry.
		 */
		next = LIST_NEXT(ar, fri_glist);
	}

	if (ar == NULL) {
		/*
		 * There are no more entries, copy the empty one.
		 */
		bzero(&zero, sizeof (zero));
		token->ipt_data = NULL;
		RWLOCK_EXIT(&ifs->ifs_ipf_auth);
		error = (COPYOUT(&zero, dst, sizeof (*next)) == 0) ?
		    0 : EFAULT;
	} else {
		/*
		 * We will remember position in a list, where we will start when
		 * following ioctl() read operation will come for our iterator.
		 */
		token->ipt_data = next;
		RWLOCK_EXIT(&ifs->ifs_ipf_auth);
		error = (COPYOUT(ar, dst, sizeof (*ar)) == 0) ?  0 : EFAULT;
		/*
		 * Drop reference to entry, we've just copied out.
		 */
		fr_auth_deref(ar);
	}

	return (error);
}
