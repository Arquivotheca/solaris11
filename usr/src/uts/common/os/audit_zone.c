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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <c2/audit.h>
#include <c2/audit_kernel.h>
#include <c2/audit_record.h>
#include <sys/kmem.h>
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/taskq.h>
#include <sys/t_lock.h>
#include <sys/thread.h>
#include <sys/types.h>
#include <sys/zone.h>

zone_key_t au_zone_key;

/*ARGSUSED*/
static void *
au_zone_init(zoneid_t zone)
{
	au_kcontext_t	*kctx = kmem_zalloc(sizeof (au_kcontext_t), KM_SLEEP);
	static au_kcontext_t	*global_kctx = NULL;

	/*
	 * INGLOBALZONE(curproc) is invalid at this point, so check for
	 * zone 0
	 */

	if (zone == 0) {
		global_kctx = kctx;
		global_zone->zone_audit_kctxt = kctx;
	} else {
		kctx->auk_policy = global_kctx->auk_policy;
		curproc->p_zone->zone_audit_kctxt = kctx;
	}
	kctx->auk_zid = zone;

	kctx->auk_info.ai_termid.at_type = AU_IPv4;
	kctx->auk_info.ai_auid = AU_NOAUDITID;
	kctx->auk_auditstate = AUC_INIT_AUDIT;

	/* setup defaults for audit queue flow control */
	kctx->auk_queue.hiwater = AQ_HIWATER;
	kctx->auk_queue.lowater = AQ_LOWATER;
	kctx->auk_queue.bufsz   = AQ_BUFSZ;
	kctx->auk_queue.buflen  = AQ_BUFSZ;
	kctx->auk_queue.delay   = AQ_DELAY;

	/* statistics per zone */
	kctx->auk_statistics.as_version  = TOKEN_VERSION;
	kctx->auk_statistics.as_numevent = MAX_KEVENTS;

	/* door IO buffer: */
	kctx->auk_dbuffer =
	    kmem_alloc(AU_DBUF_HEADER + kctx->auk_queue.bufsz, KM_SLEEP);

	/* locks and cv's */

	mutex_init(&(kctx->auk_eagain_mutex), NULL, MUTEX_DEFAULT, NULL);
	cv_init(&(kctx->auk_eagain_cv), NULL, CV_DRIVER, NULL);

	mutex_init(&(kctx->auk_svc_lock), NULL, MUTEX_DEFAULT, NULL);

	mutex_init(&(kctx->auk_queue.lock), NULL, MUTEX_DEFAULT, NULL);
	cv_init(&(kctx->auk_queue.write_cv), NULL, CV_DRIVER, NULL);
	cv_init(&(kctx->auk_queue.read_cv), NULL, CV_DRIVER, NULL);

	return (kctx);
}

/*ARGSUSED*/
static void
au_zone_shutdown(zoneid_t zone, void *arg)
{
	au_kcontext_t	*kctx = arg;
	timeout_id_t	coid_stop;

	/* shutdown the output thread if it is still running */
	kctx->auk_auditstate = AUC_NOAUDIT;

	/*
	 * The auditing is turned off at this moment. If the audit output thread
	 * is still active, wake it up so it can finish its job and terminate.
	 */
	if (kctx->auk_output_active) {
		mutex_enter(&(kctx->auk_queue.lock));
		cv_broadcast(&(kctx->auk_queue.read_cv));
		mutex_exit(&(kctx->auk_queue.lock));
	}

	/*
	 * Destroy audit output taskq. Do it here, not in au_zone_destroy()
	 * since we need to kill all kernel threads running in the zone.
	 * taskq_destroy() waits for audit output thread to finish its job.
	 * That is neccessary because we need to be sure here that
	 * kctx->auk_queue.coid_stop is set before we try to untimeout() it.
	 */
	if (kctx->auk_taskq != NULL) {
		taskq_destroy(kctx->auk_taskq);
		/* Ensure that taskq_destroy() is called only once per zone. */
		kctx->auk_taskq = NULL;
	}

	/*
	 * The output thread is gone, but there still might be a callout for
	 * audit_dont_stop() active. We need to remove it because with the zone
	 * gone, kctx is freed and we do not want to let audit_dont_stop() touch
	 * freed memory.  audit_dont_stop() is scheduled only once during the
	 * run of the output thread. taskq_destroy() correctly lets the output
	 * thread finish its job, so there is a callout id which has not changed
	 * since the thread destruction. The timer might have already expired so
	 * the callout id may be obsolete at this moment, but we do not care as
	 * untimeout() is fine with that.
	 */
	coid_stop = kctx->auk_queue.coid_stop;
	if (coid_stop != 0) {
		(void) untimeout(coid_stop);
	}

}

/*ARGSUSED*/
void
au_zone_destroy(zoneid_t zone, void *arg)
{
	au_kcontext_t *kctx = arg;

	ASSERT(kctx->auk_auditstate == AUC_NOAUDIT);

	/* Release all resources kept by auditing for a given zone */

	mutex_destroy(&(kctx->auk_eagain_mutex));
	cv_destroy(&(kctx->auk_eagain_cv));

	mutex_destroy(&(kctx->auk_svc_lock));

	mutex_enter(&(kctx->auk_queue.lock));
	if (kctx->auk_queue.head != NULL) {
		au_free_rec(kctx->auk_queue.head);
	}
	mutex_exit(&(kctx->auk_queue.lock));
	mutex_destroy(&(kctx->auk_queue.lock));

	cv_destroy(&(kctx->auk_queue.write_cv));
	cv_destroy(&(kctx->auk_queue.read_cv));

	kmem_free(kctx->auk_dbuffer, AU_DBUF_HEADER + kctx->auk_queue.buflen);

	kmem_free(kctx, sizeof (au_kcontext_t));
}

void
au_zone_setup()
{
	zone_key_create(&au_zone_key, au_zone_init, au_zone_shutdown,
	    au_zone_destroy);

}

int
au_zone_getstate(const au_kcontext_t *context)
{
	au_kcontext_t *tcontext;

	if (context != NULL)
		return (context->auk_auditstate);
	tcontext = GET_KCTX_PZ;
	return (tcontext->auk_auditstate);
}

au_class_t
au_zone_getkclasses(proc_t *p)
{
	return ((audit_policy & AUDIT_PERZONE) ?
	    p->p_zone->zone_audit_kctxt->auk_classes:
	    global_zone->zone_audit_kctxt->auk_classes);
}
