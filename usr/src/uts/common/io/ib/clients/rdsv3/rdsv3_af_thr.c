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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */
#include <sys/ib/clients/rdsv3/ib.h>
#include <sys/ib/clients/rdsv3/rdsv3_af_thr_impl.h>
#include <sys/ib/clients/rdsv3/rdsv3_debug.h>

extern pri_t maxclsyspri;

int rdsv3_enable_snd_cq = 0;
int rdsv3_intr_line_up_mode = 1;

void
rdsv3_af_init(dev_info_t *dip)
{
	rdsv3_enable_snd_cq = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, "EnableSendCQ", rdsv3_enable_snd_cq);
	rdsv3_intr_line_up_mode = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, "IntrLineUpMode", rdsv3_intr_line_up_mode);
}

rdsv3_af_grp_t *
rdsv3_af_grp_create(ibt_hca_hdl_t hca, uint64_t id)
{
	char name[128];
	ibt_cq_sched_attr_t cq_sched_attr;
	ibt_status_t status;
	rdsv3_af_grp_t *hcagp;
	uint64_t l_id = id;
	ibt_hca_attr_t dev_attr;

	hcagp = kmem_zalloc(sizeof (*hcagp), KM_NOSLEEP);
	if (!hcagp)
		return (NULL);
	hcagp->g_hca_hdl = hca;

	cq_sched_attr.cqs_flags = IBT_CQS_SCHED_GROUP;
	cq_sched_attr.cqs_pool_name = "rdsv3";
	status = ibt_alloc_cq_sched(hca,
	    &cq_sched_attr, &hcagp->g_sched_hdl);
	if (status != IBT_SUCCESS) {
		RDSV3_DPRINTF2("rdsv3_af_grp_create",
		    "ibt_alloc_cq_sched failed for did: %llx", (longlong_t)id);
		kmem_free(hcagp, sizeof (*hcagp));
		return (NULL);
	}
	if (ibt_query_hca(hca, &dev_attr) != IBT_SUCCESS) {
		(void) ibt_free_cq_sched(hca, hcagp->g_sched_hdl);
		kmem_free(hcagp, sizeof (*hcagp));
		return (NULL);
	}
	hcagp->g_hca_dip = dev_attr.hca_dip;

	/* create HCA affinity group */
	(void) snprintf(name, 64, "RDSV3_IB_HCAGP_%llx", (longlong_t)id);
	hcagp->g_aff_hcagp = numaio_group_create(name);
	if (hcagp->g_aff_hcagp == NULL) {
		RDSV3_DPRINTF2("rdsv3_af_grp_create",
		    "numaio_group_create failed for did: %llx", (longlong_t)id);
		(void) ibt_free_cq_sched(hca, hcagp->g_sched_hdl);
		kmem_free(hcagp, sizeof (*hcagp));
		return (NULL);
	}

	/* create HCA affinity object and add to the HCA group */
	(void) snprintf(name, 64, "RDSV3_IB_HCA_%llx", (longlong_t)id);

	hcagp->g_aff_hca = numaio_object_create_dev_info(hcagp->g_hca_dip,
	    name, 0);

	if (hcagp->g_aff_hca == NULL) {
		RDSV3_DPRINTF2("rdsv3_af_grp_create",
		    "numaio_object_create_dev_info failed for did: %llx", id);
		numaio_group_destroy(hcagp->g_aff_hcagp);

		(void) ibt_free_cq_sched(hca, hcagp->g_sched_hdl);

		kmem_free(hcagp, sizeof (*hcagp));
		return (NULL);
	}
	numaio_group_add_object(hcagp->g_aff_hcagp,
	    hcagp->g_aff_hca, NUMAIO_FLAG_BIND_DEFER);
	return (hcagp);
}

void
rdsv3_af_grp_destroy(rdsv3_af_grp_t *hcagp)
{
	if (hcagp == NULL)
		return;

	if (hcagp->g_sched_hdl != NULL) {
		(void) ibt_free_cq_sched(hcagp->g_hca_hdl,
		    hcagp->g_sched_hdl);
	}

	/*
	 * cleanup the HCA affinity group and its HCA object which
	 * should be the last remaining object
	 */
	if (hcagp->g_aff_hcagp) {
		numaio_group_remove_object(hcagp->g_aff_hcagp,
		    hcagp->g_aff_hca);
		numaio_object_destroy(hcagp->g_aff_hca);
		numaio_group_destroy(hcagp->g_aff_hcagp);
	}
	kmem_free(hcagp, sizeof (*hcagp));
}

void
rdsv3_af_grp_draw(rdsv3_af_grp_t *hcagp)
{
	numaio_group_map(hcagp->g_aff_hcagp);
}

rdsv3_af_procspec_t
rdsv3_af_procspec()
{
	return ((rdsv3_af_procspec_t)(numaio_get_proc_cookie(curproc)));
}

ibt_sched_hdl_t
rdsv3_af_grp_get_sched(rdsv3_af_grp_t *hcagp)
{
	return (hcagp->g_sched_hdl);
}

static void
rdsv3_af_thr_get_intr(rdsv3_af_grp_t *hcagp, ibt_cq_hdl_t ibt_cq_hdl,
    rdsv3_af_thr_t *ringp)
{
	ibt_cq_handler_attr_t cq_hdl_attr;
	uint_t entries;
	uint_t count;
	uint_t usec;
	ibt_cq_handler_id_t hid;

	(void) ibt_query_cq(ibt_cq_hdl, &entries, &count, &usec, &hid);
	(void) ibt_query_cq_handler_id(hcagp->g_hca_hdl, hid, &cq_hdl_attr);

	ringp->aft_intr = cq_hdl_attr.cha_ih;
}

rdsv3_af_thr_t *
rdsv3_af_intr_thr_create(rdsv3_af_thr_drain_func_t fn, void *data, uint_t flag,
    rdsv3_af_grp_t *hcagp, ibt_cq_hdl_t ibt_cq_hdl,
    rdsv3_af_procspec_t procspec, char *name)
{
	rdsv3_af_thr_t *ringp;
	numaio_proc_cookie_t id = (numaio_proc_cookie_t)procspec;

	if (ibt_cq_hdl == NULL)
		return (NULL);
	ringp = rdsv3_af_thr_create(fn, data, flag, hcagp, procspec, name);
	if (ringp == NULL)
		return (NULL);

	rdsv3_af_thr_get_intr(hcagp, ibt_cq_hdl, ringp);
	ringp->aft_aff_proc = numaio_object_create_proc(id, "rdsv3_proc", 0);
	numaio_group_add_object(ringp->aft_hcagp,
	    ringp->aft_aff_proc, NUMAIO_FLAG_BIND_DEFER);
	numaio_set_affinity(ringp->aft_aff_kthr, ringp->aft_aff_proc,
	    NUMAIO_AFF_STRENGTH_SOCKET, NUMAIO_FLAG_BIND_DEFER);

	ringp->aft_aff_intr = numaio_object_create_interrupt(
	    ringp->aft_intr, "hermon_intr", 0);
	numaio_group_add_object(ringp->aft_hcagp,
	    ringp->aft_aff_intr, NUMAIO_FLAG_BIND_DEFER);
	if (rdsv3_intr_line_up_mode) {
		/* line-up mode */
		numaio_set_affinity(ringp->aft_aff_kthr, ringp->aft_aff_intr,
		    NUMAIO_AFF_STRENGTH_CPU, NUMAIO_FLAG_BIND_DEFER);
	} else {
		/* exclusive mode */
		numaio_set_affinity(ringp->aft_aff_kthr, ringp->aft_aff_intr,
		    NUMAIO_AFF_STRENGTH_SOCKET, NUMAIO_FLAG_BIND_DEFER);
	}
	return (ringp);
}

rdsv3_af_thr_t *
rdsv3_af_thr_create(rdsv3_af_thr_drain_func_t fn, void *data, uint_t flag,
    rdsv3_af_grp_t *hcagp, rdsv3_af_procspec_t procspec, char *name)
{
	rdsv3_af_thr_t *ringp;
	pri_t pri = maxclsyspri;
	uint_t l_flags = flag;
	rdsv3_af_grp_t *l_hcagp = hcagp;
	uint_t object_create_flags = 0;

	ringp = kmem_zalloc(sizeof (rdsv3_af_thr_t), KM_NOSLEEP);
	if (ringp == NULL)
		return (NULL);

	ringp->aft_grp = hcagp;
	mutex_init(&ringp->aft_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&ringp->aft_async, NULL, CV_DEFAULT, NULL);
	ringp->aft_worker = thread_create(NULL, 0,
	    rdsv3_af_thr_worker, ringp, 0, &p0, TS_RUN, pri);
	ringp->aft_data = data;
	ringp->aft_drain_func = (rdsv3_af_thr_drain_func_t)fn;

	ringp->aft_state = 0;
	ringp->aft_cflag = flag;

	if ((flag & SCQ_HCA_BIND_CPU && hcagp->g_aff_hcathr == NULL) ||
	    (flag & SCQ_INTR_BIND_CPU && rdsv3_intr_line_up_mode))
		object_create_flags = NUMAIO_OBJECT_FLAG_DEDICATED_CPU;
	ringp->aft_aff_kthr = numaio_object_create_thread(ringp->aft_worker,
	    name, object_create_flags);
	numaio_group_add_object(hcagp->g_aff_hcagp,
	    ringp->aft_aff_kthr, NUMAIO_FLAG_BIND_DEFER);

	ringp->aft_hcagp = hcagp->g_aff_hcagp;
	if (flag & SCQ_BIND_CPU) {
		if (flag & SCQ_HCA_BIND_CPU) {
			if (hcagp->g_aff_hcathr != NULL) {
				numaio_set_affinity(hcagp->g_aff_hcathr,
				    ringp->aft_aff_kthr,
				    NUMAIO_AFF_STRENGTH_CPU,
				    NUMAIO_FLAG_BIND_DEFER);
			} else {
				hcagp->g_aff_hcathr = ringp->aft_aff_kthr;
			}
		} else if (flag & SCQ_WRK_BIND_CPU) {
			numaio_proc_cookie_t id;

			id = (numaio_proc_cookie_t)(procspec);
			ringp->aft_aff_proc =
			    numaio_object_create_proc(id, "rdsv3_proc", 0);
			numaio_group_add_object(ringp->aft_hcagp,
			    ringp->aft_aff_proc, NUMAIO_FLAG_BIND_DEFER);
			numaio_set_affinity(ringp->aft_aff_proc,
			    ringp->aft_aff_kthr, NUMAIO_AFF_STRENGTH_SOCKET,
			    NUMAIO_FLAG_BIND_DEFER);

			/*
			 * We want the drain threads to go on the same
			 * socket as the first connection. But the
			 * drain threads are created on a per HCA
			 * basis and not per connection.
			 */
			if (hcagp->g_aff_hcathr != NULL &&
			    hcagp->g_aff_proc == NULL) {
				hcagp->g_aff_proc =
				    numaio_object_create_proc(id,
				    "HCA_proc", 0);
				numaio_group_add_object(hcagp->g_aff_hcagp,
				    hcagp->g_aff_proc, NUMAIO_FLAG_BIND_DEFER);
				numaio_set_affinity(hcagp->g_aff_proc,
				    hcagp->g_aff_hcathr,
				    NUMAIO_AFF_STRENGTH_SOCKET,
				    NUMAIO_FLAG_BIND_DEFER);
			}
		}
	}

	RDSV3_DPRINTF4("rdsv3_af_thr_create", "af_thr %p ic %p", ringp, data);
	return (ringp);
}

void
rdsv3_af_thr_destroy(rdsv3_af_thr_t *ringp)
{
	kt_did_t tid = ringp->aft_worker->t_did;

	RDSV3_DPRINTF4("rdsv3_af_thr_destroy", "af_thr %p tid %d", ringp, tid);

	/* wait until the af_thr has gone to sleep */
	mutex_enter(&ringp->aft_lock);
	while (ringp->aft_state & AFT_PROC) {
		mutex_exit(&ringp->aft_lock);
		delay(drv_usectohz(1000));
		mutex_enter(&ringp->aft_lock);
	}
	ringp->aft_state |= AFT_CONDEMNED;
	cv_signal(&ringp->aft_async);
	mutex_exit(&ringp->aft_lock);
	thread_join(tid);
}

void
rdsv3_af_thr_fire(rdsv3_af_thr_t *ringp)
{
	mutex_enter(&ringp->aft_lock);
	ringp->aft_state |= AFT_ARMED;
	if (!(ringp->aft_state & AFT_PROC)) {
		cv_signal(&ringp->aft_async);
	}
	mutex_exit(&ringp->aft_lock);
}

static void
rdsv3_af_thr_worker(rdsv3_af_thr_t *ringp)
{
	kmutex_t *lock = &ringp->aft_lock;
	kcondvar_t *async = &ringp->aft_async;
	callb_cpr_t cprinfo;

	RDSV3_DPRINTF4("rdsv3_af_thr_worker", "Enter af_thr %p", ringp);

	CALLB_CPR_INIT(&cprinfo, lock, callb_generic_cpr, "rdsv3_af_thr");
	mutex_enter(lock);
	for (;;) {
		while (!(ringp->aft_state & (AFT_ARMED | AFT_CONDEMNED))) {
			CALLB_CPR_SAFE_BEGIN(&cprinfo);
			cv_wait(async, lock);
			CALLB_CPR_SAFE_END(&cprinfo, lock);
		}
		ringp->aft_state &= ~AFT_ARMED;

		/*
		 * Either we have work to do, or we have been asked to
		 * shutdown
		 */
		if (ringp->aft_state & AFT_CONDEMNED)
			goto done;
		ASSERT(!(ringp->aft_state & AFT_PROC));
		ringp->aft_state |= AFT_PROC;
		mutex_exit(lock);

		ringp->aft_drain_func(ringp->aft_data);

		mutex_enter(lock);
		ringp->aft_state &= ~AFT_PROC;
	}
done:
	CALLB_CPR_EXIT(&cprinfo);
	RDSV3_DPRINTF2("rdsv3_af_thr_worker", "Exit af_thr %p", ringp);
	cv_destroy(&ringp->aft_async);
	mutex_destroy(&ringp->aft_lock);
	if (ringp->aft_hcagp) {
		if (ringp->aft_aff_intr != NULL) {
			numaio_clear_affinity(ringp->aft_aff_kthr,
			    ringp->aft_aff_proc, NUMAIO_FLAG_BIND_DEFER);
			numaio_group_remove_object(ringp->aft_hcagp,
			    ringp->aft_aff_proc);
			numaio_object_destroy(ringp->aft_aff_proc);

			numaio_clear_affinity(ringp->aft_aff_kthr,
			    ringp->aft_aff_intr, NUMAIO_FLAG_BIND_DEFER);
			numaio_group_remove_object(ringp->aft_hcagp,
			    ringp->aft_aff_intr);
			numaio_object_destroy(ringp->aft_aff_intr);
		} else if (ringp->aft_cflag & SCQ_HCA_BIND_CPU) {
			rdsv3_af_grp_t *hcagp = ringp->aft_grp;

			if (hcagp->g_aff_hcathr != NULL) {
				if (hcagp->g_aff_hcathr !=
				    ringp->aft_aff_kthr) {
					numaio_clear_affinity(
					    hcagp->g_aff_hcathr,
					    ringp->aft_aff_kthr,
					    NUMAIO_FLAG_BIND_DEFER);
				} else {
					if (hcagp->g_aff_proc != NULL) {
						numaio_clear_affinity(
						    hcagp->g_aff_proc,
						    hcagp->g_aff_hcathr,
						    NUMAIO_FLAG_BIND_DEFER);
						numaio_group_remove_object(
						    hcagp->g_aff_hcagp,
						    hcagp->g_aff_proc);
						numaio_object_destroy(
						    hcagp->g_aff_proc);
						hcagp->g_aff_proc = NULL;
					}
					hcagp->g_aff_hcathr = NULL;
				}
			}
		} else if (ringp->aft_cflag & SCQ_WRK_BIND_CPU) {
			numaio_clear_affinity(ringp->aft_aff_proc,
			    ringp->aft_aff_kthr, NUMAIO_FLAG_BIND_DEFER);
			numaio_group_remove_object(ringp->aft_hcagp,
			    ringp->aft_aff_proc);
			numaio_object_destroy(ringp->aft_aff_proc);
		}
		numaio_group_remove_object(ringp->aft_hcagp,
		    ringp->aft_aff_kthr);
		numaio_object_destroy(ringp->aft_aff_kthr);
	}
	kmem_free(ringp, sizeof (rdsv3_af_thr_t));
	thread_exit();
}
