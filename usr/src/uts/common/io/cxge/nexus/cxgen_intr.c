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
 * Copyright (c) 2010 by Chelsio Communications, Inc.
 */

/*
 * Interrupt handling routines and helpers.
 */
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/sysmacros.h>

#include "cxge_common.h"
#include "cxge_regs.h"

#include "cxgen.h"

/*
 * Routines used by others are not static, helpers are.
 */
int cxgen_alloc_intr(p_adapter_t, int);
void cxgen_free_intr(p_adapter_t);

static int cxgen_alloc_intr_onetype(p_adapter_t, int);
static uint_t t3_intr_fixed(caddr_t, caddr_t);
static uint_t t3_intr_msi(caddr_t, caddr_t);
static uint_t t3_intr_msix_ctrl(caddr_t, caddr_t);
static uint_t t3_intr_msix_data(caddr_t, caddr_t);
static inline void cxgen_data_intr(p_adapter_t, uint_t);

int
cxgen_alloc_intr(p_adapter_t cxgenp, int allowed)
{
	int itypes;

	cxgenp->intr_type = 0;
	cxgenp->intr_count = 0;
	cxgenp->intr_handle = NULL;

	/* We need this to be set at this point */
	ASSERT(cxgenp->params.nports);

	if (ddi_intr_get_supported_types(
	    cxgenp->dip, &itypes) != DDI_SUCCESS) {
		ASSERT(0);
		return (DDI_FAILURE);
	}

	/* Prune intr options based on what is allowed and what we can handle */
	itypes &= allowed;
	itypes &= DDI_INTR_TYPE_MSIX | DDI_INTR_TYPE_MSI | DDI_INTR_TYPE_FIXED;

	/* Try msi-x before anything else */
	if (itypes & DDI_INTR_TYPE_MSIX &&
	    cxgen_alloc_intr_onetype(cxgenp, DDI_INTR_TYPE_MSIX) == 0)
		return (DDI_SUCCESS);

	/* Next try msi */
	if (itypes & DDI_INTR_TYPE_MSI &&
	    cxgen_alloc_intr_onetype(cxgenp, DDI_INTR_TYPE_MSI) == 0)
		return (DDI_SUCCESS);

	/* Try INTx if all else has failed */
	if (itypes & DDI_INTR_TYPE_FIXED &&
	    cxgen_alloc_intr_onetype(cxgenp, DDI_INTR_TYPE_FIXED) == 0)
			return (DDI_SUCCESS);

	/* These should not have changed. */
	ASSERT(cxgenp->intr_type == 0);
	ASSERT(cxgenp->intr_count == 0);
	ASSERT(cxgenp->intr_handle == NULL);

	return (DDI_FAILURE);
}

void
cxgen_free_intr(p_adapter_t cxgenp)
{
	int i, rc;

	ASSERT(cxgenp->intr_type);
	ASSERT(cxgenp->intr_handle);
	ASSERT(cxgenp->intr_count);

	if (cxgenp->intr_cap & DDI_INTR_FLAG_BLOCK) {
		(void) ddi_intr_block_disable(cxgenp->intr_handle,
		    cxgenp->intr_count);
	} else {
		for (i = 0; i < cxgenp->intr_count; i++)
			(void) ddi_intr_disable(cxgenp->intr_handle[i]);
	}

	for (i = 0; i < cxgenp->intr_count; i++) {
		(void) ddi_intr_remove_handler(cxgenp->intr_handle[i]);
		rc = ddi_intr_free(cxgenp->intr_handle[i]);
		if (rc != DDI_SUCCESS)
			cmn_err(CE_WARN, "err %d during intr %d free", rc, i);
	}

	kmem_free(cxgenp->intr_handle,
	    cxgenp->intr_count * sizeof (ddi_intr_handle_t));

	cxgenp->intr_type = 0;
	cxgenp->intr_count = 0;
	cxgenp->intr_handle = NULL;
}

static int
cxgen_alloc_intr_onetype(p_adapter_t cxgenp, int type)
{
	int count, avail, rc, i;

	/* cxgenp must not be setup already */
	ASSERT(cxgenp->intr_type == 0);
	ASSERT(cxgenp->intr_count == 0);
	ASSERT(cxgenp->intr_handle == NULL);

	ASSERT(type == DDI_INTR_TYPE_MSIX || type == DDI_INTR_TYPE_MSI ||
	    type == DDI_INTR_TYPE_FIXED);

	/* Get number of available interrupts */
	rc = ddi_intr_get_navail(cxgenp->dip, type, &avail);
	if (rc != DDI_SUCCESS || avail == 0)
		return (DDI_FAILURE);

	/* type specific stuff */
	if (type == DDI_INTR_TYPE_MSIX) {
		/* one for "slow" stuff (error/mac/phy), rest for qsets */
		count = 1 + cxgenp->qsets_per_port * cxgenp->params.nports;
#if defined(__sparc)
		(void) ddi_prop_create(DDI_DEV_T_NONE, cxgenp->dip,
		    DDI_PROP_CANSLEEP, "#msix-request", NULL, 0);
#endif
	} else if (type == DDI_INTR_TYPE_MSI) {
		count = 2;
	} else {
		count = 1;
	}

	if (count > avail)
		return (DDI_FAILURE);

	cxgenp->intr_handle = kmem_zalloc(count * sizeof (ddi_intr_handle_t),
	    KM_SLEEP);
	if (cxgenp->intr_handle == NULL)
		return (DDI_FAILURE);

	rc = ddi_intr_alloc(cxgenp->dip, cxgenp->intr_handle, type, 0, count,
	    &cxgenp->intr_count, DDI_INTR_ALLOC_STRICT);
	if (rc != DDI_SUCCESS || cxgenp->intr_count == 0) {
		kmem_free(cxgenp->intr_handle,
		    count * sizeof (ddi_intr_handle_t));
		cxgenp->intr_count = 0;
		cxgenp->intr_handle = NULL;

		return (DDI_FAILURE);
	}

	ASSERT(cxgenp->intr_count);
	ASSERT(cxgenp->intr_handle);

	/* cxge_intr.c should use intr_type, others should use cxgenp->flags */
	cxgenp->intr_type = type;
	if (type == DDI_INTR_TYPE_MSIX)
		cxgenp->flags |= USING_MSIX;
	else if (type == DDI_INTR_TYPE_MSI)
		cxgenp->flags |= USING_MSI;

	(void) ddi_intr_get_cap(cxgenp->intr_handle[0], &cxgenp->intr_cap);
	(void) ddi_intr_get_pri(cxgenp->intr_handle[0],
	    &cxgenp->intr_lo_priority);
	if (cxgenp->intr_count > 1)
		(void) ddi_intr_get_pri(cxgenp->intr_handle[1],
		    &cxgenp->intr_hi_priority);
	else
		cxgenp->intr_hi_priority = cxgenp->intr_lo_priority;

	/* Add the handlers too. */
	if (cxgenp->intr_type == DDI_INTR_TYPE_FIXED) {
		(void) ddi_intr_add_handler(cxgenp->intr_handle[0],
		    t3_intr_fixed, cxgenp, NULL);
	} else if (cxgenp->intr_type == DDI_INTR_TYPE_MSI) {
		ASSERT(cxgenp->intr_count == 2);
		(void) ddi_intr_add_handler(cxgenp->intr_handle[0], t3_intr_msi,
		    cxgenp, NULL);
		(void) ddi_intr_add_handler(cxgenp->intr_handle[1], t3_intr_msi,
		    cxgenp, NULL);
	} else if (cxgenp->intr_type == DDI_INTR_TYPE_MSIX) {
		(void) ddi_intr_add_handler(cxgenp->intr_handle[0],
		    t3_intr_msix_ctrl, cxgenp, NULL);
		for (i = 1; i < cxgenp->intr_count; i++) {
			(void) ddi_intr_add_handler(cxgenp->intr_handle[i],
			    t3_intr_msix_data, cxgenp, &cxgenp->sge.qs[i - 1]);
		}
	}

	/* Enable them too */
	if (cxgenp->intr_cap & DDI_INTR_FLAG_BLOCK) {
		(void) ddi_intr_block_enable(cxgenp->intr_handle,
		    cxgenp->intr_count);
	} else {
		for (i = 0; i < cxgenp->intr_count; i++)
			(void) ddi_intr_enable(cxgenp->intr_handle[i]);
	}

	return (DDI_SUCCESS);
}

/* ARGSUSED */
static uint_t
t3_intr_fixed(caddr_t arg1, caddr_t arg2)
{
	p_adapter_t cxgenp = (void *)arg1;
	uint_t map, qsets, err;

	t3_write_reg(cxgenp, A_PL_CLI, 0);
	map = t3_read_reg(cxgenp, A_SG_DATA_INTR);

	if ((qsets = G_DATAINTR(map)) != 0)
		cxgen_data_intr(cxgenp, qsets);

	if ((err = (map & F_ERRINTR)) != 0)
		(void) t3_slow_intr_handler(cxgenp);

	return ((!err && !qsets) ? DDI_INTR_UNCLAIMED : DDI_INTR_CLAIMED);
}

/* ARGSUSED */
static uint_t
t3_intr_msi(caddr_t arg1, caddr_t arg2)
{
	p_adapter_t cxgenp = (void *)arg1;
	uint_t map, qsets;

	t3_write_reg(cxgenp, A_PL_CLI, 0);
	map = t3_read_reg(cxgenp, A_SG_DATA_INTR);

	if ((qsets = G_DATAINTR(map)) != 0)
		cxgen_data_intr(cxgenp, qsets);

	if (map & F_ERRINTR)
		(void) t3_slow_intr_handler(cxgenp);

#ifdef DEBUG
	if (!map && !qsets)
		cmn_err(CE_NOTE, "intr with no cause");
#endif

	return (DDI_INTR_CLAIMED);
}

/* ARGSUSED */
static uint_t
t3_intr_msix_ctrl(caddr_t arg1, caddr_t arg2)
{
	p_adapter_t cxgenp = (void *)arg1;

	(void) t3_slow_intr_handler(cxgenp);

	return (DDI_INTR_CLAIMED);
}

static uint_t
t3_intr_msix_data(caddr_t arg1, caddr_t arg2)
{
	p_adapter_t cxgenp = (void *)arg1;
	struct sge_qset *qs = (void *)arg2;
	struct sge_rspq *q = &qs->rspq;
	mblk_t *m;

	mutex_enter(&q->lock);
	if (q->polling)
		goto done;

	do {
		m = sge_rx_data(qs);

		if (m) {
			mutex_exit(&q->lock);

			/* Pass up with no locks held */
			(qs->port->rx)(qs, m);

			mutex_enter(&q->lock);
		}
	} while (q->more);

	t3_write_reg(cxgenp, A_SG_GTS, V_RSPQ(q->cntxt_id) |
	    V_NEWTIMER(q->next_holdoff) | V_NEWINDEX(q->index));

	if (cxgen_fm_check_acc_handle(cxgenp->regh) != DDI_FM_OK)
		cxgen_fm_err_report(cxgenp, DDI_FM_DEVICE_INVAL_STATE,
		    DDI_SERVICE_DEGRADED);

done:
	mutex_exit(&q->lock);
	return (DDI_INTR_CLAIMED);
}

/* qsets is a bitmap indicating which qsets need to be serviced */
static inline void
cxgen_data_intr(p_adapter_t cxgenp, uint_t qsets)
{
	struct sge_qset *qs;
	struct sge_rspq *q;
	int i, got_mutex, passes;

	/*
	 * We'll make these many passes using tryenter before we decide to wait
	 * for the mutex.
	 */
	passes = 3;

	while (qsets) {
		for (i = 0; i < SGE_QSETS; i++) {
			uint_t qbit = 1 << i;

			if ((qsets & qbit) == 0)
				continue;

			qs = &cxgenp->sge.qs[i];
			q = &qs->rspq;

			if (q->polling) {
				qsets &= ~qbit;
				continue;
			}

			/*
			 * Decide whether to wait for the mutex or not.  We wait
			 * if we have made many passes already or if this is the
			 * only qset that we need to service.
			 */
			if (passes == 0 || ISP2(qsets)) {
				mutex_enter(&q->lock);
				got_mutex = 1;
			} else
				got_mutex = mutex_tryenter(&q->lock);

			if (got_mutex) {
				if (q->state == THRD_IDLE) {
					q->state = THRD_BUSY;
					cv_signal(&q->cv);
				}
				mutex_exit(&q->lock);
				qsets &= ~qbit;
			}
		}
		passes--;
	}
}
