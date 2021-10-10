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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/sysmacros.h>
#include <sys/byteorder.h>
#include <sys/strsubr.h>
#include <sys/strsun.h>
#include <sys/pattr.h>
#include <inet/ip.h>
#include <inet/tcp.h>
#include <sys/atomic.h>
#include <sys/mac_provider.h>

#include "cxge_fw_exports.h"
#include "cxge_common.h"
#include "cxge_regs.h"
#include "cxge_t3_cpl.h"
#include "cxge_sge_defs.h"
#include "cxgen.h"

#if defined(__sparc)
#define	htonll(x) (x)
#else
#define	htonll(x) ((((uint64_t)htonl(x)) << 32) + htonl(((uint64_t)x) >> 32))
#endif

/* TODO: Tune. */
int copy_thresh = 256;
int flq_small_buf_sz = 1600;
int flq_big_buf_sz = 8192;

#pragma pack(1)
struct tx_desc {
	uint64_t flit[TX_DESC_FLITS];
};
struct rx_desc {
	uint32_t addr_lo;
	uint32_t len_gen;
	uint32_t gen2;
	uint32_t addr_hi;
};
struct rsp_desc {
	struct rss_header rss_hdr;
	uint32_t flags;
	uint32_t len_cq;
	uint8_t imm_data[47];
	uint8_t intr_gen;
};
#pragma pack()

#define	MAX_SGL_LEN	8
#define	USE_GTS		0

static ddi_dma_attr_t sge3_dma_attr = {
	.dma_attr_version = DMA_ATTR_V0,
	.dma_attr_addr_lo = 0,
	.dma_attr_addr_hi = 0xffffffffffffffff,
	.dma_attr_count_max = 0xffffffffffffffff,
	.dma_attr_align = 8,
	.dma_attr_burstsizes = 0xfc00fc,
	.dma_attr_minxfer = 0x1,
	.dma_attr_maxxfer = 0xffffffffffffffff,
	.dma_attr_seg = 0xffffffffffffffff,
	.dma_attr_sgllen = 1,
	.dma_attr_granular = 1,
	.dma_attr_flags = 0
};

static ddi_device_acc_attr_t sge3_device_acc_attr = {
	.devacc_attr_version = DDI_DEVICE_ATTR_V0,
	.devacc_attr_endian_flags = DDI_NEVERSWAP_ACC,
	.devacc_attr_dataorder = DDI_STRICTORDER_ACC
};

/*
 * helpers
 */

static int sge_alloc_txq(p_adapter_t, uint_t, struct sge_txq *, int);
static void sge_free_txq(struct sge_txq *);
static int sge_alloc_flq(p_adapter_t, uint_t, uint_t, struct sge_fl *);
static void sge_free_flq(struct sge_fl *);
static int sge_alloc_fl_metaq(p_adapter_t, int, int, p_fl_metaq_t);
static void sge_free_fl_metaq(p_fl_metaq_t);
static int sge_alloc_fl_meta(p_adapter_t, int, p_fl_meta_t, struct fl_stats *);
static void sge_free_fl_meta(p_fl_meta_t);
static int sge_alloc_rspq(p_adapter_t, uint_t, struct sge_rspq *);
static void sge_free_rspq(struct sge_rspq *);
static mblk_t *sge_get_msg(p_adapter_t, struct sge_qset *, int);
static uint32_t lso_info(mblk_t *);
static int sge_xmit_reclaim(struct sge_txq *, int);
static void sge_tx_ctrl(struct sge_qset *, mblk_t *);

/* TODO: Tune. */
void
t3_sge_prep(p_adapter_t adap, struct sge_params *p)
{
	int i;
	struct qset_params *q;

	for (i = 0; i < SGE_QSETS; ++i) {
		q = &p->qset[i];

		if (adap->params.nports > 2)
			q->coalesce_usecs = 50;
		else
			q->coalesce_usecs = 5;

		q->polling = 0;
		q->rspq_size = 2048;
		q->fl_size = 1024;
		q->jumbo_size = 1024;
		q->txq_size[TXQ_ETH] = 1024;
		q->txq_size[TXQ_OFLD] = 32;
		q->txq_size[TXQ_CTRL] = 32;
		q->cong_thres = 0;
	}
}

/*
 *	t3_sge_init - initialize SGE
 *	@adap: the adapter
 *	@p: the SGE parameters
 *
 *	Performs SGE initialization needed every time after a chip reset.
 *	We do not initialize any of the queue sets here, instead the driver
 *	top-level must request those individually.  We also do not enable DMA
 *	here, that should be done after the queues have been set up.
 */
/* ARGSUSED */
void
t3_sge_init(adapter_t *adap, struct sge_params *p)
{
	uint_t ctrl, ups;

	ups = 0; /* = ffs(pci_resource_len(adap->pdev, 2) >> 12); */

	ctrl = F_DROPPKT | V_PKTSHIFT(2) | F_FLMODE | F_AVOIDCQOVFL |
	    F_CQCRDTCTRL | F_CONGMODE | F_TNLFLMODE | F_FATLPERREN |
	    V_HOSTPAGESIZE(PAGESHIFT - 11) | F_BIGENDIANINGRESS |
	    V_USERSPACESIZE(ups ? ups - 1 : 0) | F_ISCSICOALESCING;
#if SGE_NUM_GENBITS == 1
	ctrl |= F_EGRGENCTRL;
#endif
	if (adap->params.rev > 0) {
		if (!(adap->flags & USING_MSIX))
			ctrl |= F_ONEINTMULTQ | F_OPTONEINTMULTQ;
	}
	t3_write_reg(adap, A_SG_CONTROL, ctrl);
	t3_write_reg(adap, A_SG_EGR_RCQ_DRB_THRSH, V_HIRCQDRBTHRSH(512) |
	    V_LORCQDRBTHRSH(512));
	t3_write_reg(adap, A_SG_TIMER_TICK, core_ticks_per_usec(adap) / 10);
	t3_write_reg(adap, A_SG_CMDQ_CREDIT_TH, V_THRESHOLD(32) |
	    V_TIMEOUT(200 * core_ticks_per_usec(adap)));
	t3_write_reg(adap, A_SG_HI_DRB_HI_THRSH,
	    adap->params.rev < T3_REV_C ? 1000 : 500);
	t3_write_reg(adap, A_SG_HI_DRB_LO_THRSH, 256);
	t3_write_reg(adap, A_SG_LO_DRB_HI_THRSH, 1000);
	t3_write_reg(adap, A_SG_LO_DRB_LO_THRSH, 256);
	t3_write_reg(adap, A_SG_OCO_BASE, V_BASE1(0xfff));
	t3_write_reg(adap, A_SG_DRB_PRI_THRESH, 63 * 1024);
}

int
t3_get_desc(struct sge_qset *qs, uint_t qnum, uint_t idx, uint64_t *data)
{
	if (qnum >= 6)
		return (-1);

	if (qnum < 3) {
		egr_hwq_t *q = &qs->txq[qnum].hwq;

		if (!q->queue || idx >= q->depth)
			return (-1);

		bcopy(&q->queue[idx], data, sizeof (struct tx_desc));

		return (sizeof (struct tx_desc));
	} else if (qnum == 3) {
		struct sge_rspq *q = &qs->rspq;

		if (!q->queue || idx >= q->depth)
			return (-1);

		bcopy(&q->queue[idx], data, sizeof (struct rsp_desc));

		return (sizeof (struct rsp_desc));
	} else {
		fl_hwq_t *q;

		qnum -= 4;
		q = &qs->fl[qnum].hwq;

		if (!q->queue || idx >= q->depth)
			return (-1);

		bcopy(&q->queue[idx], data, sizeof (struct rx_desc));

		return (sizeof (struct rx_desc));
	}
}

static void
init_qset_cntxt(struct sge_qset *qs, unsigned int id)
{
	qs->rspq.cntxt_id = id;
	qs->fl[0].cntxt_id = 2 * id;
	qs->fl[1].cntxt_id = 2 * id + 1;
	qs->txq[TXQ_ETH].cntxt_id = FW_TUNNEL_SGEEC_START + id;
	qs->txq[TXQ_ETH].token = FW_TUNNEL_TID_START + id;
	qs->txq[TXQ_OFLD].cntxt_id = FW_OFLD_SGEEC_START + id;
	qs->txq[TXQ_CTRL].cntxt_id = FW_CTRL_SGEEC_START + id;
	qs->txq[TXQ_CTRL].token = FW_CTRL_TID_START + id;
}

/*
 * Allocates resources and initalizes a qset.  This includes all the queues in
 * that qset.
 */
int
t3_sge_alloc_qset(p_adapter_t cxgenp, uint_t id, int vec, const
	struct qset_params *p, struct port_info *pi)
{
	int rc, i;
	struct sge_txq *txq;
	struct sge_fl *fl;
	struct sge_rspq *rspq;
	struct sge_qset *q = &cxgenp->sge.qs[id];

	/* qset should be all blank at this stage */
	ASSERT(q->port == NULL);
	ASSERT(q->idx == 0);

	/* qset's index and parent port */
	q->idx = id;
	q->port = pi;

	/* Allocate the two freelists */
	fl = &q->fl[0];
	rc = sge_alloc_flq(cxgenp, p->fl_size, flq_small_buf_sz, fl);
	if (rc) {
		cmn_err(CE_WARN, "%s: could not allocate free list 0 for "
		    "qset%d: 0x%x", __func__, id, rc);
		goto fail;
	}

	fl = &q->fl[1];
	rc = sge_alloc_flq(cxgenp, p->jumbo_size, flq_big_buf_sz, fl);
	if (rc) {
		cmn_err(CE_WARN, "%s: could not allocate free list 1 for "
		    "qset%d: 0x%x", __func__, id, rc);
		goto fail;
	}

	/* Allocate the response queue */
	rspq = &q->rspq;
	rc = sge_alloc_rspq(cxgenp, p->rspq_size, rspq);
	if (rc) {
		cmn_err(CE_WARN, "%s: could not allocate response queue for "
		    "qset%d: 0x%x", __func__, id, rc);
		goto fail;
	}
	rspq->holdoff_tmr = max(p->coalesce_usecs * 10, 1U);

	/* Allocate TXQ_ETH (aka tunnel queue) */
	txq = &q->txq[TXQ_ETH];
	rc = sge_alloc_txq(cxgenp, p->txq_size[TXQ_ETH], txq, TXQ_ETH);
	if (rc) {
		cmn_err(CE_WARN, "%s: could not allocate tunnel queue for "
		    "qset%d: 0x%x", __func__, id, rc);
		goto fail;
	}

	/* Allocate TXQ_CTRL (aka control queue) if this is qset #0 */
	if (q->idx == 0) {
		txq = &q->txq[TXQ_CTRL];
		rc = sge_alloc_txq(cxgenp, p->txq_size[TXQ_CTRL], txq,
		    TXQ_CTRL);
		if (rc) {
			cmn_err(CE_WARN, "%s: could not allocate ctrl queue "
			    "for qset%d: 0x%x", __func__, id, rc);
			goto fail;
		}
	}

	/* Intialize and program all the contexts into the chip */
	init_qset_cntxt(q, id);
	mutex_enter(&cxgenp->sge.reg_lock);

	rspq = &q->rspq;
	rc = -t3_sge_init_rspcntxt(cxgenp, rspq->cntxt_id, vec,
	    rspq->cookie.dmac_laddress, rspq->depth,
	    flq_small_buf_sz, 1, 0);
	if (rc) {
		cmn_err(CE_WARN, "%s: could not initialize response queue "
		    "context for qset%d (%d)", __func__, id, rc);
		goto sge_unlock;
	}

	for (i = 0; i < SGE_RXQ_PER_SET; i++) {
		fl = &q->fl[i];
		rc = -t3_sge_init_flcntxt(cxgenp, fl->cntxt_id, 0,
		    fl->hwq.cookie.dmac_laddress,
		    fl->hwq.depth,
		    fl->metaq[0].queue[0]->buf_size,
		    p->cong_thres, 1, 0);
		if (rc) {
			cmn_err(CE_WARN, "%s: could not initialize free list "
			    "%d context for qset%d (%d)", __func__, i, id,
			    rc);
			goto sge_unlock;
		}

		/* ring fl doorbell */
		t3_write_reg(cxgenp, A_SG_KDOORBELL, V_EGRCNTX(fl->cntxt_id));
	}

	txq = &q->txq[TXQ_ETH];
	rc = -t3_sge_init_ecntxt(cxgenp, txq->cntxt_id, USE_GTS, SGE_CNTXT_ETH,
	    id, txq->hwq.cookie.dmac_laddress,
	    txq->hwq.depth, txq->token, 1, 0);
	if (rc) {
		cmn_err(CE_WARN, "%s: could not initialize tunnel queue "
		    "context for qset%d (%d)", __func__, id, rc);
		goto sge_unlock;
	}

	if (q->idx == 0) {
		txq = &q->txq[TXQ_CTRL];
		rc = -t3_sge_init_ecntxt(cxgenp, txq->cntxt_id, USE_GTS,
		    SGE_CNTXT_CTRL, id,
		    txq->hwq.cookie.dmac_laddress,
		    txq->hwq.depth, txq->token, 1, 0);
		if (rc) {
			cmn_err(CE_WARN, "%s: could not initialize ctrl queue "
			    "context for qset%d (%d)", __func__, id, rc);
			goto sge_unlock;
		}
	}

	t3_write_reg(cxgenp, A_SG_RSPQ_CREDIT_RETURN, V_RSPQ(rspq->cntxt_id) |
	    V_CREDITS(rspq->depth - 1));

	t3_write_reg(cxgenp, A_SG_GTS, V_RSPQ(rspq->cntxt_id) |
	    V_NEWTIMER(rspq->holdoff_tmr));

sge_unlock:
	mutex_exit(&cxgenp->sge.reg_lock);
fail:
	if (rc)
		t3_sge_free_qset(cxgenp, id);
	return (rc);
}

void
t3_sge_free_qset(p_adapter_t cxgenp, uint_t id)
{
	int i;
	struct sge_qset *q = &cxgenp->sge.qs[id];
	struct port_info *pi = q->port;

	ASSERT(q->idx == id);
	ASSERT(pi);

	if (q->rspq.cleanup)
		sge_free_rspq(&q->rspq);

	for (i = 0; i < SGE_RXQ_PER_SET; i++)
		if (q->fl[i].cleanup)
			sge_free_flq(&q->fl[i]);

	for (i = 0; i < SGE_TXQ_PER_SET; i++)
		if (q->txq[i].cleanup)
			sge_free_txq(&q->txq[i]);

	/* scrub it clean */
	bzero(q, sizeof (struct sge_qset));
}

#define	TXQ_HWQ_HDL_ALLOC	0x1
#define	TXQ_HWQ_MEM_ALLOC	0x2
#define	TXQ_HWQ_HDL_BOUND	0x4
#define	TXQ_SWDESC_ALLOC	0x8
#define	TXQ_MQ_HDL_ALLOC	0x10
#define	TXQ_MQ_MEM_ALLOC	0x20
#define	TXQ_MQ_HDL_BOUND	0x40
#define	TXQ_SWDESC_HANDLES	0x80
#define	TXQ_MUTEX		0x100
static int
sge_alloc_txq(p_adapter_t cxgenp, uint_t sz, struct sge_txq *txq, int type)
{
	p_egr_hwq_t hwq = &txq->hwq;
	p_egr_metaq_t metaq = &txq->metaq;
	ddi_dma_attr_t da = sge3_dma_attr;
	ddi_device_acc_attr_t daa = sge3_device_acc_attr;
	int rc, i;
	uint_t ccount;
	size_t size, real_len, offset;
	caddr_t queue;

	/* queue should be all blank at this stage */
	ASSERT(hwq->queue == NULL);
	ASSERT(metaq->queue == NULL);
	ASSERT(metaq->bufs == NULL);

	txq->sys_page_sz = ddi_ptob(cxgenp->dip, 1); /* TODO PAGESIZE v. this */
	hwq->depth = sz;
	hwq->credits = sz;

	/* FMA */
	if (DDI_FM_DMA_ERR_CAP(cxgenp->fma_cap))
		da.dma_attr_flags |= DDI_DMA_FLAGERR;

	/* Allocate DMA handle for hwq */
	da.dma_attr_align = 0x1000;
	rc = ddi_dma_alloc_handle(cxgenp->dip, &da, DDI_DMA_DONTWAIT, 0,
	    &hwq->dma_hdl);
	if (rc != DDI_SUCCESS) {
		cmn_err(CE_WARN, "%s: failed to allocate DMA handle (%d)",
		    __func__, rc);
		goto fail;
	}
	txq->cleanup |= TXQ_HWQ_HDL_ALLOC;

	/* Allocate space for the hardware descriptor ring */
	size = hwq->depth * sizeof (struct tx_desc);
	rc = ddi_dma_mem_alloc(hwq->dma_hdl, size, &daa, DDI_DMA_CONSISTENT,
	    DDI_DMA_DONTWAIT, 0, &queue, &real_len, &hwq->acc_hdl);
	hwq->queue = (struct tx_desc *)queue;
	if (rc != DDI_SUCCESS) {
		cmn_err(CE_WARN, "%s: failed to allocate DMA memory (%d)",
		    __func__, rc);
		goto fail;
	}
	txq->cleanup |= TXQ_HWQ_MEM_ALLOC;
	bzero(hwq->queue, size);

	/* Bind the descriptor ring to the handle */
	rc = ddi_dma_addr_bind_handle(hwq->dma_hdl, NULL, queue, size,
	    DDI_DMA_WRITE | DDI_DMA_CONSISTENT, NULL,
	    NULL, &hwq->cookie, &ccount);
	if (rc != DDI_DMA_MAPPED) {
		cmn_err(CE_WARN, "%s: failed to map DMA memory (%d)", __func__,
		    rc);
		goto fail;
	}
	txq->cleanup |= TXQ_HWQ_HDL_BOUND;
	if (ccount != 1) {
		cmn_err(CE_WARN, "%s: unusable DMA mapping (%d segments)",
		    __func__, ccount);
		goto fail;
	}

	hwq->gen_bit = 1;

	/* ctrlq does not need any metaq or metas.  Go straight to the mutex */
	if (type == TXQ_CTRL)
		goto mtx;

	/* Now onto the meta q.  For TXQ_ETH and TXQ_OFLD */
	metaq->depth = MAX_SGL_LEN * hwq->depth;
	metaq->buf_size = copy_thresh * metaq->depth;

	/*
	 * Allocate space for the software descriptors.  Each of these has a
	 * small_buf_[va|pa] which will point to the corresponding 256 byte
	 * buffer in metaq->bufs
	 */
	metaq->queue = kmem_zalloc(metaq->depth * sizeof (egr_meta_t),
	    KM_NOSLEEP);
	if (metaq->queue == NULL) {
		cmn_err(CE_WARN, "%s: can't allocate memory for metaq",
		    __func__);
		goto fail;
	}
	txq->cleanup |= TXQ_SWDESC_ALLOC;

	rc = ddi_dma_alloc_handle(cxgenp->dip, &da, DDI_DMA_DONTWAIT, 0,
	    &metaq->dma_hdl);
	if (rc != DDI_SUCCESS) {
		cmn_err(CE_WARN, "%s: failed to allocate metaq DMA handle (%d)",
		    __func__, rc);
		goto fail;
	}
	txq->cleanup |= TXQ_MQ_HDL_ALLOC;

	/* Allocate space for the small_buf's */
	rc = ddi_dma_mem_alloc(metaq->dma_hdl, metaq->buf_size, &daa,
	    DDI_DMA_CONSISTENT, DDI_DMA_DONTWAIT, 0,
	    &metaq->bufs, &real_len, &metaq->acc_hdl);
	if (rc != DDI_SUCCESS) {
		cmn_err(CE_WARN, "%s: failed to allocate DMA memory for "
		    "small_bufs (%d)", __func__, rc);
		goto fail;
	}
	txq->cleanup |= TXQ_MQ_MEM_ALLOC;

	/* Bind the small bufs to metaq->dma_hdl */
	rc = ddi_dma_addr_bind_handle(metaq->dma_hdl, NULL, metaq->bufs,
	    metaq->buf_size, DDI_DMA_WRITE | DDI_DMA_CONSISTENT, NULL, NULL,
	    &metaq->cookie, &ccount);
	if (rc != DDI_DMA_MAPPED) {
		cmn_err(CE_WARN, "%s: failed to map metaq DMA memory (%d)",
		    __func__, rc);
		goto fail;
	}
	txq->cleanup |= TXQ_MQ_HDL_BOUND;

	if (ccount != 1) {
		cmn_err(CE_WARN, "%s: unusable metaq DMA mapping (%d segments)",
		    __func__, ccount);
		goto fail;
	}

	/*
	 * Setup the sw descriptors.  Each has an associated small buf and a DMA
	 * handle for use when the small buf is not sufficient.
	 */
	da.dma_attr_align = 1;
	da.dma_attr_sgllen = ~0U;
	for (i = 0, offset = 0; i < metaq->depth; i++, offset += copy_thresh) {
		p_egr_meta_t sdesc = &metaq->queue[i];
		rc = ddi_dma_alloc_handle(cxgenp->dip, &da, DDI_DMA_DONTWAIT,
		    0, &sdesc->dma_hdl);
		if (rc != DDI_SUCCESS) {
			cmn_err(CE_WARN, "%s: failed to allocate DMA handle "
			    "for sw descriptor at index %d (%d)", __func__,
			    i, rc);
			/* unwind and fail */
			while (--i >= 0) {
				sdesc = &metaq->queue[i];
				ddi_dma_free_handle(&sdesc->dma_hdl);
			}
			goto fail;
		}

		sdesc->small_buf_va = metaq->bufs + offset;
		sdesc->small_buf_pa = metaq->cookie.dmac_laddress + offset;
	}
	txq->cleanup |= TXQ_SWDESC_HANDLES;

mtx:
	/* All OK */
	mutex_init(&txq->lock, NULL, MUTEX_DRIVER,
	    DDI_INTR_PRI(cxgenp->intr_hi_priority));
	txq->cleanup |= TXQ_MUTEX;

	return (0);

fail:
	/* never return 0, try to return the cleanup mask */
	rc = txq->cleanup ? txq->cleanup : -1;
	sge_free_txq(txq);
	return (rc);
}

/*
 * Frees an allocated txq.  Inspects the cleanup member to determine what needs
 * to be done.
 */
static void
sge_free_txq(struct sge_txq *txq)
{
	p_egr_hwq_t hwq = &txq->hwq;
	p_egr_metaq_t metaq = &txq->metaq;

	ASSERT(txq->ksp == NULL);

	if (txq->cleanup & TXQ_SWDESC_ALLOC &&
	    hwq->rd_index != hwq->wr_index) {
		mutex_enter(&txq->lock);
		hwq->crd_index = hwq->wr_index;
		(void) sge_xmit_reclaim(txq, -1);
		mutex_exit(&txq->lock);
	}

	if (txq->cleanup & TXQ_MUTEX)
		mutex_destroy(&txq->lock);

	if (txq->cleanup & TXQ_SWDESC_HANDLES) {
		for (int i = 0; i < metaq->depth; i++) {
			p_egr_meta_t sdesc = &metaq->queue[i];
			ddi_dma_free_handle(&sdesc->dma_hdl);
		}
	}

	if (txq->cleanup & TXQ_MQ_HDL_BOUND)
		(void) ddi_dma_unbind_handle(metaq->dma_hdl);

	if (txq->cleanup & TXQ_MQ_MEM_ALLOC)
		ddi_dma_mem_free(&metaq->acc_hdl);

	if (txq->cleanup & TXQ_MQ_HDL_ALLOC)
		ddi_dma_free_handle(&metaq->dma_hdl);

	if (txq->cleanup & TXQ_SWDESC_ALLOC)
		kmem_free(metaq->queue, metaq->depth * sizeof (egr_meta_t));

	if (txq->cleanup & TXQ_HWQ_HDL_BOUND)
		(void) ddi_dma_unbind_handle(hwq->dma_hdl);

	if (txq->cleanup & TXQ_HWQ_MEM_ALLOC)
		ddi_dma_mem_free(&hwq->acc_hdl);

	if (txq->cleanup & TXQ_HWQ_HDL_ALLOC)
		ddi_dma_free_handle(&hwq->dma_hdl);

	/* scrub it clean */
	bzero(txq, sizeof (struct sge_txq));
}

#define	FL_HWQ_HDL_ALLOC	0x1
#define	FL_HWQ_MEM_ALLOC	0x2
#define	FL_HWQ_HDL_BOUND	0x4
#define	FL_MQ0_ALLOC		0x8
#define	FL_MQ1_ALLOC		0x10
#define	FL_MUTEX		0x20
static int
sge_alloc_flq(p_adapter_t cxgenp, uint_t sz, uint_t buf_size, struct sge_fl *fl)
{
	p_fl_hwq_t hwq = &fl->hwq;
	p_fl_metaq_t mq0 = &fl->metaq[0], mq1 = &fl->metaq[1];
	ddi_dma_attr_t da = sge3_dma_attr;
	ddi_device_acc_attr_t daa = sge3_device_acc_attr;
	int rc, i;
	uint_t ccount;
	size_t size, real_len;
	struct rx_desc *desc;
	caddr_t queue;

	/* queue should be all blank at this stage */
	ASSERT(hwq->queue == NULL);
	ASSERT(mq0->queue == NULL);
	ASSERT(mq1->queue == NULL);

	hwq->depth = sz;

	/* FMA */
	if (DDI_FM_DMA_ERR_CAP(cxgenp->fma_cap))
		da.dma_attr_flags |= DDI_DMA_FLAGERR;

	/* Allocate dma handle for hwq */
	da.dma_attr_align = 0x1000;
	rc = ddi_dma_alloc_handle(cxgenp->dip, &da, DDI_DMA_DONTWAIT, 0,
	    &hwq->dma_hdl);
	if (rc != DDI_SUCCESS) {
		cmn_err(CE_WARN, "%s: failed to allocate DMA handle (%d)",
		    __func__, rc);
		goto fail;
	}
	fl->cleanup |= FL_HWQ_HDL_ALLOC;

	/* Allocate space for the hardware descriptor ring */
	size = hwq->depth * sizeof (struct rx_desc);
	rc = ddi_dma_mem_alloc(hwq->dma_hdl, size, &daa, DDI_DMA_CONSISTENT,
	    DDI_DMA_DONTWAIT, 0, &queue, &real_len, &hwq->acc_hdl);
	hwq->queue = (struct rx_desc *)queue;
	if (rc != DDI_SUCCESS) {
		cmn_err(CE_WARN, "%s: failed to allocate DMA memory (%d)",
		    __func__, rc);
		goto fail;
	}
	fl->cleanup |= FL_HWQ_MEM_ALLOC;
	bzero(hwq->queue, size);

	/* Bind the descriptor ring to the handle */
	rc = ddi_dma_addr_bind_handle(hwq->dma_hdl, NULL, queue, size,
	    DDI_DMA_WRITE | DDI_DMA_CONSISTENT, NULL,
	    NULL, &hwq->cookie, &ccount);
	if (rc != DDI_DMA_MAPPED) {
		cmn_err(CE_WARN, "%s: failed to map DMA memory (%d)", __func__,
		    rc);
		goto fail;
	}
	fl->cleanup |= FL_HWQ_HDL_BOUND;
	if (ccount != 1) {
		cmn_err(CE_WARN, "%s: unusable DMA mapping (%d segments)",
		    __func__, ccount);
		goto fail;
	}

	hwq->gen_bit = 1;

	/* On to the meta queues */
	rc = sge_alloc_fl_metaq(cxgenp, hwq->depth, buf_size, mq0);
	if (rc != DDI_SUCCESS) {
		cmn_err(CE_WARN, "%s: failed to setup metaq", __func__);
		goto fail;
	}
	fl->cleanup |= FL_MQ0_ALLOC;

	rc = sge_alloc_fl_metaq(cxgenp, hwq->depth, buf_size, mq1);
	if (rc != DDI_SUCCESS) {
		cmn_err(CE_WARN, "%s: failed to setup spareq", __func__);
		goto fail;
	}
	fl->cleanup |= FL_MQ1_ALLOC;

	/* Initialize the hardware descriptors. */
	desc = hwq->queue;
	for (i = 0; i < hwq->depth; i++, desc++) {
		p_fl_meta_t m = fl->metaq[0].queue[i];

		desc->addr_lo = BE_32(m->cookie.dmac_laddress & 0xffffffff);
		desc->addr_hi = BE_32((m->cookie.dmac_laddress >> 32) &
		    0xffffffff);
		desc->len_gen = BE_32(V_FLD_GEN1(hwq->gen_bit));
		desc->gen2 = BE_32(V_FLD_GEN2(hwq->gen_bit));
	}

	/*
	 * Now Flip the gen bit so we can use this variable
	 * to update the gen bit at runtime.
	 */
	hwq->gen_bit ^= 1;

	/* TODO: what for? */
	mutex_init(&fl->lock, NULL, MUTEX_DRIVER,
	    DDI_INTR_PRI(cxgenp->intr_hi_priority));
	fl->cleanup |= FL_MUTEX;

	ASSERT(rc == 0);
	return (0);
fail:
	/* never return 0 from here */
	rc = fl->cleanup ? fl->cleanup : -1;
	sge_free_flq(fl);
	return (rc);
}

static int
sge_alloc_fl_metaq(p_adapter_t cxgenp, int depth, int b_sz, p_fl_metaq_t mq)
{
	int i;

	mq->queue = kmem_zalloc(depth * sizeof (p_fl_meta_t), KM_NOSLEEP);
	if (mq->queue == NULL) {
		cmn_err(CE_WARN, "%s: out of memory", __func__);
		return (ENOMEM);
	}

	mq->depth = depth;

	for (i = 0; i < mq->depth; i++) {
		mq->queue[i] = kmem_zalloc(sizeof (fl_meta_t), KM_NOSLEEP);
		if (mq->queue[i] == NULL) {
			cmn_err(CE_WARN, "%s: out of memory at index %d",
			    __func__, i);
			goto fail;
		}

		if (sge_alloc_fl_meta(cxgenp, b_sz, mq->queue[i], NULL) != 0) {
			cmn_err(CE_WARN, "%s: unable to setup meta at index %d",
			    __func__, i);
			kmem_free(mq->queue[i], sizeof (fl_meta_t));
			goto fail;
		}
	}

	return (0);
fail:
	while (--i >= 0)
		freemsg(mq->queue[i]->message);

	kmem_free(mq->queue, depth * sizeof (p_fl_meta_t));
	return (ENOMEM);
}

static int
sge_alloc_fl_meta(p_adapter_t cxgenp, int buf_size, p_fl_meta_t m,
	struct fl_stats *stats)
{
	int rc;
	uint_t ccount = 0;
	ddi_dma_attr_t da = sge3_dma_attr;
	ddi_device_acc_attr_t daa = sge3_device_acc_attr;
	size_t real_len;

	/* FMA */
	if (DDI_FM_DMA_ERR_CAP(cxgenp->fma_cap))
		da.dma_attr_flags |= DDI_DMA_FLAGERR;

	m->dma_hdl = NULL;
	rc = ddi_dma_alloc_handle(cxgenp->dip, &da, DDI_DMA_DONTWAIT, 0,
	    &m->dma_hdl);
	if (rc != 0) {
		if (stats)
			stats->nomem_meta_hdl++;
		return (ENOMEM);
	}

	m->buf = NULL;
	rc = ddi_dma_mem_alloc(m->dma_hdl, buf_size, &daa, DDI_DMA_CONSISTENT,
	    DDI_DMA_DONTWAIT, 0, &m->buf, &real_len, &m->acc_hdl);
	if (rc != DDI_SUCCESS) {
		if (stats)
			stats->nomem_meta_mem++;
		goto fail;
	}

	rc = ddi_dma_addr_bind_handle(m->dma_hdl, NULL, m->buf, buf_size,
	    DDI_DMA_READ | DDI_DMA_CONSISTENT, NULL,
	    NULL, &m->cookie, &ccount);
	if (rc != DDI_SUCCESS || ccount != 1) {
		if (stats)
			stats->nomem_meta_bind++;
		goto fail;
	}

	m->buf_size = buf_size;
	m->ref_cnt = 1;
	m->fr_rtn.free_arg = (caddr_t)m;
	m->fr_rtn.free_func = sge_free_fl_meta;

	m->message = desballoc((uchar_t *)m->buf, buf_size, BPRI_HI,
	    &m->fr_rtn);
	if (m->message == NULL) {
		if (stats)
			stats->nomem_meta_mblk++;
		goto fail;
	}

	return (0);
fail:
	if (ccount > 0) {
		(void) ddi_dma_unbind_handle(m->dma_hdl);
	}

	if (m->buf)
		ddi_dma_mem_free(&m->acc_hdl);

	if (m->dma_hdl)
		ddi_dma_free_handle(&m->dma_hdl);

	return (ENOMEM);
}

/* Never call this directly, this is called indirectly via freemsg() */
static void
sge_free_fl_meta(p_fl_meta_t m)
{
	int ref_cnt;

	ref_cnt = atomic_dec_uint_nv(&m->ref_cnt);
	ASSERT(ref_cnt >= 0);

	if (ref_cnt == 0) {
		(void) ddi_dma_unbind_handle(m->dma_hdl);
		ddi_dma_mem_free(&m->acc_hdl);
		ddi_dma_free_handle(&m->dma_hdl);
		kmem_free(m, sizeof (fl_meta_t));
	}
}

static void
sge_free_fl_metaq(p_fl_metaq_t mq)
{
	for (int i = 0; i < mq->depth; i++) {
		p_fl_meta_t m = mq->queue[i];
		freemsg(m->message);
	}

	kmem_free(mq->queue, mq->depth * sizeof (p_fl_meta_t));
}

static void
sge_free_flq(struct sge_fl *fl)
{
	p_fl_hwq_t hwq = &fl->hwq;

	ASSERT(fl->ksp == NULL);

	if (fl->cleanup & FL_MUTEX)
		mutex_destroy(&fl->lock);

	if (fl->cleanup & FL_MQ1_ALLOC)
		sge_free_fl_metaq(&fl->metaq[1]);

	if (fl->cleanup & FL_MQ0_ALLOC)
		sge_free_fl_metaq(&fl->metaq[0]);

	if (fl->cleanup & FL_HWQ_HDL_BOUND)
		(void) ddi_dma_unbind_handle(hwq->dma_hdl);

	if (fl->cleanup & FL_HWQ_MEM_ALLOC)
		ddi_dma_mem_free(&hwq->acc_hdl);

	if (fl->cleanup & FL_HWQ_HDL_ALLOC)
		ddi_dma_free_handle(&hwq->dma_hdl);

	/* scrub it clean */
	bzero(fl, sizeof (struct sge_fl));
}

#define	RSPQ_HDL_ALLOC	0x1
#define	RSPQ_MEM_ALLOC	0x2
#define	RSPQ_HDL_BOUND	0x4
#define	RSPQ_MUTEX	0x8
static int
sge_alloc_rspq(p_adapter_t cxgenp, uint_t sz, struct sge_rspq *rspq)
{
	ddi_dma_attr_t da = sge3_dma_attr;
	ddi_device_acc_attr_t daa = sge3_device_acc_attr;
	int rc;
	uint_t ccount;
	size_t size, real_len;
	caddr_t queue;

	/* should be all blank right now */
	ASSERT(rspq->queue == NULL);

	rspq->depth = sz;

	/* FMA */
	if (DDI_FM_DMA_ERR_CAP(cxgenp->fma_cap))
		da.dma_attr_flags |= DDI_DMA_FLAGERR;

	/* Allocate DMA handle */
	da.dma_attr_align = 0x1000;
	rc = ddi_dma_alloc_handle(cxgenp->dip, &da, DDI_DMA_DONTWAIT, 0,
	    &rspq->dma_hdl);
	if (rc != DDI_SUCCESS) {
		cmn_err(CE_WARN, "%s: failed to allocate DMA handle (%d)",
		    __func__, rc);
		goto fail;
	}
	rspq->cleanup |= RSPQ_HDL_ALLOC;

	/* Allocate space for the hardware descriptor ring */
	size = rspq->depth * sizeof (struct rsp_desc);
	rc = ddi_dma_mem_alloc(rspq->dma_hdl, size, &daa, DDI_DMA_CONSISTENT,
	    DDI_DMA_DONTWAIT, 0, &queue, &real_len, &rspq->acc_hdl);
	rspq->queue = (struct rsp_desc *)queue;
	if (rc != DDI_SUCCESS) {
		cmn_err(CE_WARN, "%s: failed to allocate DMA memory (%d)",
		    __func__, rc);
		goto fail;
	}
	rspq->cleanup |= RSPQ_MEM_ALLOC;
	bzero(rspq->queue, size);

	/* Bind the descriptor ring to the handle */
	rc = ddi_dma_addr_bind_handle(rspq->dma_hdl, NULL, queue, size,
	    DDI_DMA_READ | DDI_DMA_CONSISTENT, NULL,
	    NULL, &rspq->cookie, &ccount);
	if (rc != DDI_DMA_MAPPED) {
		cmn_err(CE_WARN, "%s: failed to map DMA memory (%d)", __func__,
		    rc);
		goto fail;
	}
	rspq->cleanup |= RSPQ_HDL_BOUND;
	if (ccount != 1) {
		cmn_err(CE_WARN, "%s: unusable DMA mapping (%d segments)",
		    __func__, ccount);
		goto fail;
	}

	rspq->gen_bit = 1;
	rspq->state = THRD_EXITED;

	mutex_init(&rspq->lock, NULL, MUTEX_DRIVER,
	    DDI_INTR_PRI(cxgenp->intr_hi_priority));
	rspq->cleanup |= RSPQ_MUTEX;

	rspq->budget.descs = QS_DEFAULT_DESC_BUDGET(rspq);
	rspq->budget.frames = QS_DEFAULT_FRAME_BUDGET(rspq);
	rspq->budget.bytes = 0;

	ASSERT(rc == 0);
	return (0);
fail:
	/* never return 0, try to return the cleanup mask */
	rc = rspq->cleanup ? rspq->cleanup : -1;
	sge_free_rspq(rspq);
	return (rc);
}

static void
sge_free_rspq(struct sge_rspq *rspq)
{
	ASSERT(rspq->ksp == NULL);

	if (rspq->cleanup & RSPQ_MUTEX)
		mutex_destroy(&rspq->lock);

	if (rspq->cleanup & RSPQ_HDL_BOUND)
		(void) ddi_dma_unbind_handle(rspq->dma_hdl);

	if (rspq->cleanup & RSPQ_MEM_ALLOC)
		ddi_dma_mem_free(&rspq->acc_hdl);

	if (rspq->cleanup & RSPQ_HDL_ALLOC)
		ddi_dma_free_handle(&rspq->dma_hdl);

	/* scrub it clean */
	bzero(rspq, sizeof (struct sge_rspq));
}

/*
 *	t3_sge_start - enable SGE
 *	@sc: the controller softc
 *
 *	Enables the SGE for DMAs.  This is the last step in starting packet
 *	transfers.
 */
void
t3_sge_start(adapter_t *sc)
{
	t3_set_reg_field(sc, A_SG_CONTROL, F_GLOBALENABLE, F_GLOBALENABLE);
}

/*
 *	t3_sge_stop - disable SGE operation
 *	@sc: the adapter
 *
 *	Disables the DMA engine.  This can be called in emergencies (e.g.,
 *	from error interrupts) or from normal process context.  In the latter
 *	case it also disables any pending queue restart tasklets.  Note that
 *	if it is called in interrupt context it cannot disable the restart
 *	tasklets as it cannot wait, however the tasklets will have no effect
 *	since the doorbells are disabled and the driver will call this again
 *	later from process context, at which time the tasklets will be stopped
 *	if they are still running.
 */
void
t3_sge_stop(adapter_t *sc)
{
	t3_set_reg_field(sc, A_SG_CONTROL, F_GLOBALENABLE, 0);
}

/*
 *	refill_rspq - replenish an SGE response queue
 *	@adapter: the adapter
 *	@q: the response queue to replenish
 *	@credits: how many new responses to make available
 *
 *	Replenishes a response queue by making the supplied number of responses
 *	available to HW.
 */
static inline void
refill_rspq(adapter_t *adapter, const struct sge_rspq *q, unsigned int credits)
{
	t3_write_reg(adapter, A_SG_RSPQ_CREDIT_RETURN,
	    V_RSPQ(q->cntxt_id) | V_CREDITS(credits));
}

/* TODO: this really must be inlined */
static inline void
process_credits(struct sge_qset *qs, uint32_t flags)
{
	struct port_info *pi = qs->port;
	struct sge_rspq *q = &qs->rspq;
	struct sge_txq *txq;
	uint32_t credits, crd_index;

	ASSERT(mutex_owned(&q->lock));

	/*
	 * Process any tx credits that were returned.  We don't want to
	 * spend too much time reclaiming tx descriptors here in rx.
	 * We'll give it a go but we don't try too hard, and we don't
	 * reclaim too many.  Let tx do its own work.
	 */
	credits = G_RSPD_TXQ0_CR(flags);
	if (credits) {
		txq = &qs->txq[TXQ_ETH];

		crd_index = txq->hwq.crd_index + credits;
		if (crd_index >= txq->hwq.depth)
			crd_index -= txq->hwq.depth;
		txq->hwq.crd_index = crd_index;

		if (mutex_tryenter(&txq->lock)) {
			if (txq->queueing) {
				txq->queueing = B_FALSE;
				q->stats.txq_unblocked++;

				/* Let go of txq's lock before the upcall */
				mutex_exit(&txq->lock);
				(pi->tx_update)(qs);
			} else {
				/* Reclaim a few descriptors */
				(void) sge_xmit_reclaim(txq, 0);
				mutex_exit(&txq->lock);
			}
		}
	}

	/* TXQ_OFLD credits */
	credits = G_RSPD_TXQ1_CR(flags);
	if (credits) {
		t3_unimplemented("TXQ_OFLD credit reclaim");
	}

	/* Control queue credits */
	credits = G_RSPD_TXQ2_CR(flags);
	if (credits) {
		txq = &qs->txq[TXQ_CTRL];

		crd_index = txq->hwq.crd_index + credits;
		if (crd_index >= txq->hwq.depth)
			crd_index -= txq->hwq.depth;
		txq->hwq.crd_index = crd_index;

		/*
		 * That's all for ctrlq.  All data sent out this queue
		 * was immediate.  Incrementing the index is all the
		 * reclaim required.
		 */
	}

}

#define	RSPD_CRD_MASK (V_RSPD_TXQ0_CR(M_RSPD_TXQ0_CR) | \
	V_RSPD_TXQ1_CR(M_RSPD_TXQ1_CR) | \
	V_RSPD_TXQ2_CR(M_RSPD_TXQ2_CR))
#define	T3_FULL_CSUM (HCK_FULLCKSUM_OK | HCK_IPV4_HDRCKSUM_OK)
#define	NOMEM_INTR_DELAY	2500

/*
 * rx workhorse
 */
mblk_t *
sge_rx_data(struct sge_qset *qs)
{
	struct port_info *pi = qs->port;
	adapter_t *cxgenp = pi->adapter;
	struct sge_rspq *q = &qs->rspq;
	int descs = 0, frames_chained = 0, stop = 0;
	uint32_t tot_len;
	struct rsp_desc *r = &q->queue[q->index];
	mblk_t *msg = NULL;
	struct mblk_pair *frame = &q->frame;
	struct mblk_pair *chain = &q->chain;
	struct cpl_rx_pkt *cpl;

	q->next_holdoff = q->holdoff_tmr;
	q->more = 1;

	ASSERT(mutex_owned(&q->lock));
	ASSERT(chain->head == NULL);
	ASSERT(chain->tail == NULL);

	for (;;) {
		/* LINT - E_FUNC_SET_NOT_USED */
		int eth, sop = 0, eop = 0, ethpad;
		uint32_t flags, len_cq, len;

		(void) ddi_dma_sync(q->dma_hdl,
		    q->index * sizeof (struct rsp_desc),
		    sizeof (struct rsp_desc), DDI_DMA_SYNC_FORKERNEL);

		if ((cxgen_fm_check_dma_handle(q->dma_hdl) != DDI_FM_OK) ||
		    ((r->intr_gen & F_RSPD_GEN2) != q->gen_bit)) {
			q->more = 0;
			break;
		}

		msg = NULL;
		flags = ntohl(r->flags);

		/* First process credits */
		if (flags & RSPD_CRD_MASK)
			process_credits(qs, flags);

		/* Now process rx data */
		eth = r->rss_hdr.opcode == CPL_RX_PKT;
		len_cq = ntohl(r->len_cq);
		len = G_RSPD_LEN(len_cq);

		if (len) {
			int fl = len_cq & F_RSPD_FLQ ? 1 : 0;

			/* freelist buffer consumed */
			q->stats.rx_buf_data++;

			ethpad = 2;
			sop = flags & F_RSPD_SOP ? 1 : 0;
			eop = flags & F_RSPD_EOP ? 1 : 0;

			msg = sge_get_msg(cxgenp, qs, fl);
			if (msg == NULL) {
				q->stats.nomem++;
				q->drop_frame = 1;
			}

		} else if (flags & F_RSPD_IMM_DATA_VALID) {
			cpl = (void *)r->imm_data;

			/* immediate data */
			q->stats.rx_imm_data++;

			ethpad = 0;
			len = htons(cpl->len);
			eop = 1;
			ASSERT(sop == 0);

			/* So we don't bcopy from random places */
			ASSERT(len <= sizeof (r->imm_data) - sizeof (*cpl));

			msg = allocb(len, BPRI_HI);
			if (msg)
				bcopy(&cpl[1], msg->b_rptr, len);
			else {
				q->stats.nomem++;
				q->drop_frame = 1;
			}

		} else
			q->stats.pure++;

		/* As of now this is all we can deal with */
		if (msg)
			ASSERT(eth);

		if (msg && sop) {

			ASSERT(frame->head == NULL);
			ASSERT(frame->tail == NULL);

			/* Adjust for padding */
			msg->b_rptr += ethpad;

			/* Strip off the CPL header. */
			cpl = (void *)msg->b_rptr;
			msg->b_rptr += sizeof (struct cpl_rx_pkt);
			len -= sizeof (struct cpl_rx_pkt) + ethpad;
			tot_len = cpl->len;

			/*
			 * cpl->len has the length of the frame, len has the
			 * length of the portion in msg.  If we have the entire
			 * frame (sop + eop set) then the lengths must match.
			 */
			if (eop)
				ASSERT(htons(cpl->len) == len);

			/* Hardware assistance with csum */
			if (cpl->csum_valid && !cpl->fragment)
				mac_hcksum_set(msg, 0, 0, 0, 0xffff,
				    T3_FULL_CSUM);
		}

		/* This part common for all sop/eop combinations */
		if (msg) {
			/* Length of this portion of the frame. */
			msg->b_wptr = msg->b_rptr + len;

			if (frame->head)
				frame->tail->b_cont = msg;
			else
				frame->head = msg;
			frame->tail = msg;
		}

		if (eop && q->drop_frame) {

			/*
			 * At least one part of this frame was lost due to
			 * nomem.  Now that we've reached the eop, drop even
			 * those parts that were received successfully.
			 *
			 * Note that if a frame is received in many descriptors,
			 * we will try allocating msg for each, and may end up
			 * incrementing nomem multiple times for a single frame.
			 */
			if (frame->head)
				freemsg(frame->head);
			frame->head = frame->tail = NULL;

			/* Stop dropping frames */
			q->drop_frame = 0;

			q->next_holdoff = NOMEM_INTR_DELAY;
			stop = 1;

		} else if (eop) {

			/*
			 * Reached eop and we aren't dropping the current frame.
			 * So there must be something to process and pass up.
			 */
			ASSERT(frame->head);

			/* TODO: This is the correct place to attempt LRO */

			/* Add to the chain that we'll send up */
			if (chain->head) {
				chain->tail->b_next = frame->head;
				frame->head->b_prev = chain->tail;
			} else
				chain->head = frame->head;
			chain->tail = frame->head;

			/* frame now absorbed in the chain */
			frame->head = frame->tail = NULL;

			frames_chained++;
		}

		r++;
		if (++q->index == q->depth) {
			q->index = 0;
			r = q->queue;
			q->gen_bit ^= 1;
		}

		/* TODO: Tune. */
		if (++q->credits >= 0x20) {
			refill_rspq(cxgenp, q, q->credits);
			q->credits = 0;
		}

		descs++;

		if (stop || (q->polling && (tot_len >= q->budget.bytes ||
		    frames_chained >= q->budget.frames)) ||
		    (!q->polling && (descs >= q->budget.descs ||
		    frames_chained >= q->budget.frames)))
			break;
	}

	/* Update Rx ring statistics */
	q->stats.rx_frames += frames_chained;
	q->stats.rx_octets += tot_len;

	/* Anything to send up?  Copy to msg and empty the chain */
	msg = chain->head;
	chain->head = chain->tail = NULL;

	if (descs >= q->budget.descs)
		q->stats.desc_budget_met++;

	if (frames_chained >= q->budget.frames)
		q->stats.chain_budget_met++;

	if (descs > q->stats.max_descs_processed)
		q->stats.max_descs_processed = descs;

	if (frames_chained > q->stats.max_frames_chained)
		q->stats.max_frames_chained = frames_chained;

	return (msg);
}

/*
 * Data for the qset has been DMA'ed into the given freelist.  This routine
 * returns an mblk_t with the data and also replenishes the rx descriptor that
 * was used.
 */
static mblk_t *
sge_get_msg(p_adapter_t cxgenp, struct sge_qset *qs, int fl_idx)
{
	int idx;
	mblk_t *mp = NULL, *drop = NULL;
	struct sge_fl *fl = &qs->fl[fl_idx];
	fl_hwq_t *hwq = &fl->hwq;
	p_fl_meta_t *metaq, *spareq, meta, spare;
	struct rx_desc *rxd;

	ASSERT(fl_idx == 0 || fl_idx == 1);

	idx = hwq->index;
	metaq = fl->metaq[0].queue;
	spareq = fl->metaq[1].queue;
	meta = metaq[idx];

	if (spareq[idx]->ref_cnt > 1) {
		fl->stats.spareq_miss++;

		/* Spare is still in use, allocate another one */
		spare = kmem_alloc(sizeof (fl_meta_t), KM_NOSLEEP);
		if (spare == NULL) {
			fl->stats.nomem_kalloc++;
			goto out;
		}

		if (sge_alloc_fl_meta(cxgenp, meta->buf_size, spare,
		    &fl->stats)) {
			kmem_free(spare, sizeof (fl_meta_t));
			goto out;
		}

		/* We have a replacement, we'll remove our ref on this */
		drop = spareq[idx]->message;
		spareq[idx] = spare;
	} else {
		ASSERT(spareq[idx]->ref_cnt == 1);
		fl->stats.spareq_hit++;
	}

	/*
	 * Found a meta in the spareq or managed to alloc a new one to replace
	 * it (if it was still tied up).  Either way, whatever is in spareq[idx]
	 * is available and is going to end up in the main metaq.
	 */

	mp = desballoc((uchar_t *)meta->buf, meta->buf_size, BPRI_HI,
	    &meta->fr_rtn);
	if (mp == NULL) {
		fl->stats.nomem_mblk++;
		goto out;
	}

	meta->ref_cnt++;
	metaq[idx] = spareq[idx];
	spareq[idx] = meta;
	(void) ddi_dma_sync(meta->dma_hdl, 0, meta->buf_size,
	    DDI_DMA_SYNC_FORCPU);

out:
	/* Update the hardware rx descriptor. */
	rxd = &hwq->queue[idx];
	meta = metaq[idx];
	rxd->addr_lo = BE_32(meta->cookie.dmac_laddress & 0xffffffff);
	rxd->addr_hi = BE_32((meta->cookie.dmac_laddress >> 32) & 0xffffffff);
	rxd->len_gen = BE_32(V_FLD_GEN1(hwq->gen_bit));
	rxd->gen2 = BE_32(V_FLD_GEN2(hwq->gen_bit));
	(void) ddi_dma_sync(hwq->dma_hdl, idx * sizeof (*rxd), sizeof (*rxd),
	    DDI_DMA_SYNC_FORDEV);

	if (++hwq->index == hwq->depth) {
		hwq->index = 0;
		hwq->gen_bit ^= 1;
	}

	/* TODO: Tune. */
	if (++hwq->pending >= 0x20) {
		hwq->pending = 0;
		t3_write_reg(cxgenp, A_SG_KDOORBELL, V_EGRCNTX(fl->cntxt_id));
	}

	if (drop)
		freemsg(drop); /* this frees the meta too */

	return (mp);
}

#define	PTRDIFF(x, y) (((caddr_t)x) - ((caddr_t)y))

static uint32_t
lso_info(mblk_t *mp)
{
	uint32_t rc, flags;
	unsigned char *p = mp->b_rptr;

	mac_lso_get(mp, &rc, &flags);

	/* LINTED - E_BAD_PTR_CAST_ALIGN */
	if (((struct ether_header *)p)->ether_type == htons(ETHERTYPE_VLAN)) {
		rc |= V_LSO_ETH_TYPE(CPL_ETH_II_VLAN);
		p += sizeof (struct ether_vlan_header);
	} else {
		rc |= V_LSO_ETH_TYPE(CPL_ETH_II);
		p += sizeof (struct ether_header);
	}

	if (p >= mp->b_wptr) {
		p = mp->b_cont->b_rptr + _PTRDIFF(p, mp->b_wptr);
		mp = mp->b_cont;
	}
	/* LINTED - E_BAD_PTR_CAST_ALIGN */
	rc |= V_LSO_IPHDR_WORDS(IPH_HDR_LENGTH(p) >> 2);
	/* LINTED - E_BAD_PTR_CAST_ALIGN */
	p += IPH_HDR_LENGTH(p);

	if (p >= mp->b_wptr)
		p = mp->b_cont->b_rptr + _PTRDIFF(p, mp->b_wptr);
	rc |= V_LSO_TCPHDR_WORDS(((tcph_t *)p)->th_offset_and_rsrvd[0] >> 4);

	return (htonl(rc));
}

/*
 * tx workhorse
 */
int
sge_tx_data(struct sge_qset *qs, mblk_t *mp, int db)
{
	struct port_info *pi = qs->port;
	struct sge_txq *txq = &qs->txq[TXQ_ETH];
	egr_hwq_t *hwq = &txq->hwq;
	egr_metaq_t *metaq = &txq->metaq;
	p_egr_meta_t meta, meta0;
	int meta_cnt, status, flits, buf_len, hw_cnt, sgl_indx, needed, nsegs;
	int i, idx, mblks;
	uint32_t flags, wr_hi, wr_lo, txd0_gen, lsomss, lso_flag;
	uint_t tot_len, remaining, cntrl, segments, compl = 0;
	struct tx_desc *txd, *txd0;
	struct work_request_hdr *wr;
	struct sg_ent *sgp;
	ddi_dma_cookie_t cookie;
	mblk_t *nmp;
	uint64_t buf_pa;

	/* Can't deal with b_next linked chains */
	ASSERT(mp->b_next == NULL);

	/*
	 * Count the total number of bytes in the payload, and estimate whether
	 * we'll be able to accomodate it as far as SGL entries go.  Pullup the
	 * msg if there is a risk of running out of SGL entries.
	 */
	tot_len = 0;
	needed = 0;
	mblks = 0;
	for (nmp = mp; nmp; nmp = nmp->b_cont) {
		buf_len = MBLKL(nmp);
		tot_len += buf_len;
		mblks++;

		/* Estimate max # of DMA segments assuming 4K sized pages. */
		needed += buf_len / 4096;
		needed += buf_len % 4096 > 1 ? 2 : 1;
	}
	remaining = tot_len; /* no. of bytes left */

	if (tot_len > txq->stats.max_frame_len)
		txq->stats.max_frame_len = tot_len;
	if (mblks > txq->stats.max_mblks_in_frame)
		txq->stats.max_mblks_in_frame = mblks;

	/* Convert 'needed' from number of SG entries to number of tx descs */
	needed = (needed + MAX_SGL_LEN - 1) / MAX_SGL_LEN;

	/* The hardware requires eop to be within 4 descs of sop */
	if (needed > 4) {
		txq->stats.pullup++;

		/* Save csum fields, pullup, restore csum fields */

		mac_lso_get(mp, &lsomss, &lso_flag);
		mac_hcksum_get(mp, NULL, NULL, NULL, NULL, &flags);
		flags |= lso_flag;

		if (pullupmsg(mp, -1) == 0) {
			txq->stats.pullup_failed++;
			return (1);
		}

		needed = tot_len / 4096;
		needed += tot_len % 4096 > 1 ? 2 : 1;
		needed = (needed + MAX_SGL_LEN - 1) / MAX_SGL_LEN;
		ASSERT(needed <= 4);

		/* Restore csum flags and MSS */
		mac_hcksum_set(mp, 0, 0, 0, 0, flags);
		lso_info_set(mp, lsomss, flags);
	}

	mutex_enter(&txq->lock);

	/*
	 * We've made a worst possible estimate of the number of descriptors
	 * that we may need.  Reclaim and make sure at least those many are
	 * available.
	 */
	status = sge_xmit_reclaim(txq, needed);
	if (status != 0) {
		mutex_exit(&txq->lock);
		return (status);
	}

	/* txd0 and meta0 mark the begining of our request */
	txd = txd0 = &hwq->queue[hwq->wr_index];
	meta = meta0 = &metaq->queue[metaq->wr_index];
	txd0_gen = hwq->gen_bit;

	/* TODO: Tune */
	if (txq->unacked >= 0x20) {
		compl = F_WR_COMPL;
		txq->unacked = 0;
	}

	/* The last sge_tx_data run should have left this clean */
	ASSERT(meta->message == NULL);
	ASSERT(meta->hw_cnt == 0);
	ASSERT(meta->meta_cnt == 0);
	ASSERT(meta->state == unused);

	/*
	 * Build a CPL_TX_PKT or CPL_TX_PKT_LSO header in the tx descriptor.
	 */
	mac_hcksum_get(mp, NULL, NULL, NULL, NULL, &flags);
	mac_lso_get(mp, &lsomss, &lso_flag);
	flags |= lso_flag;
	cntrl = V_TXPKT_INTF(pi->txpkt_intf);

	/* Don't compute checksums unless explicitly told to do so. */
	if (!(flags & HCK_IPV4_HDRCKSUM))
		cntrl |= F_TXPKT_IPCSUM_DIS;
	if (!(flags & HCK_FULLCKSUM))
		cntrl |= F_TXPKT_L4CSUM_DIS;

	if (flags & HW_LSO) {
		struct cpl_tx_pkt_lso *cpl = (void *)txd;

		cntrl |= V_TXPKT_OPCODE(CPL_TX_PKT_LSO);
		cpl->cntrl = htonl(cntrl);
		cpl->len = htonl(tot_len | 0x80000000);
		cpl->lso_info = lso_info(mp);
		flits = 3;

		txq->stats.tx_lso++;
	} else {
		struct cpl_tx_pkt *cpl = (void *)txd;

		cntrl |= V_TXPKT_OPCODE(CPL_TX_PKT);
		cpl->cntrl = htonl(cntrl);
		cpl->len = htonl(tot_len | 0x80000000);

		/* Immediate data takes a simpler path */
		if (tot_len <= (WR_FLITS * 8) - sizeof (*cpl)) {
			caddr_t data;
			txq->stats.tx_imm_data++;

			/* Setup the descriptor for immediate data tx */
			flits = (tot_len + 7) / 8 + 2;
			cpl->wr.wr_hi = htonl(V_WR_BCNTLFLT(tot_len & 7) |
			    V_WR_OP(FW_WROPCODE_TUNNEL_TX_PKT)
			    | F_WR_SOP | F_WR_EOP | compl);
			cpl->wr.wr_lo = htonl(V_WR_LEN(flits) |
			    V_WR_GEN(txd0_gen ^ 1) | V_WR_TID(txq->token));
#if SGE_NUM_GENBITS == 2
			txd->flit[TX_DESC_FLITS - 1] = BE_64(txd0_gen);
#endif
			/* Copy the data and throw away the mblk */
			nmp = mp;
			data = (caddr_t)&txd->flit[2];
			do {
				buf_len = MBLKL(nmp);
				bcopy(nmp->b_rptr, data, buf_len);
				data += buf_len;
				nmp = nmp->b_cont;
			} while (nmp);
			ASSERT(_PTRDIFF(data, &txd->flit[2]) == tot_len);
			freemsg(mp);
			mp = NULL; /* Required so that meta0->message is null */

			/* We know we'll use exactly 1 hw and 1 sw descriptor */
			hw_cnt = 1;
			meta_cnt = 1;
			meta0->state = immediate_data;

			/* Advance the hw and sw queue */
			txd++;
			if (++hwq->wr_index == hwq->depth) {
				hwq->wr_index = 0;
				hwq->gen_bit ^= 1;
				txd = hwq->queue;
			}
			meta++;
			if (++metaq->wr_index == metaq->depth) {
				metaq->wr_index = 0;
				meta = metaq->queue;
			}
			/* Scrub the next meta clean (reqd by debug code) */
			meta->message = NULL;
			meta->hw_cnt = 0;
			meta->meta_cnt = 0;
			meta->state = unused;

			/* Hand off to the hardware */
			goto tx_send;
		}

		/* "normal" tx - not immediate data, not LSO */
		txq->stats.tx_pkt++;

		flits = 2;
	}

	/* sgl starts from here in the tx descriptor */
	sgp = (struct sg_ent *)&txd->flit[flits];

	hw_cnt = 0;	/* number of hw tx desc we've used */
	sgl_indx = 0;	/* index of the current sg entry in the tx desc's sgl */
	meta_cnt = 0;	/* number of sw descs we've used */

	/*
	 * wr_hi/lo track what should be written to the current tx desc (once
	 * we're ready to write its header).  Initially setup for the first tx
	 * desc.
	 */
	wr_hi = htonl(F_WR_SOP | V_WR_DATATYPE(1) | V_WR_SGLSFLT(flits) |
	    V_WR_OP(FW_WROPCODE_TUNNEL_TX_PKT) | compl);

	/* Incorrect gen bit, this is deliberate.  We'll flip it at the end. */
	wr_lo = htonl(V_WR_TID(txq->token) | V_WR_GEN(txd0_gen ^ 1));

	nsegs = 0;
	for (nmp = mp; nmp; nmp = nmp->b_cont) {
		buf_len = MBLKL(nmp);
		if (buf_len == 0)
			continue;

		/* Use copy buffer or not? */
		if (buf_len < copy_thresh) {
			txq->stats.used_small_buf++;

			/*
			 * Copy buffer.  We already have the DMA mapping for it
			 */
			meta->state = use_small;
			bcopy(nmp->b_rptr, meta->small_buf_va, buf_len);
			buf_pa = meta->small_buf_pa;
			segments = 1;
			nsegs++;

		} else {
			txq->stats.used_big_buf++;

			/*
			 * No copy buffer.  We'll do the DMA mapping ourself.
			 */
			status = ddi_dma_addr_bind_handle(meta->dma_hdl, NULL,
			    (caddr_t)nmp->b_rptr,
			    buf_len, DDI_DMA_WRITE
			    | DDI_DMA_STREAMING,
			    NULL, NULL, &cookie,
			    &segments);
			if (status != DDI_DMA_MAPPED) {
				txq->stats.dma_map_failed++;
				goto tx_abort;
			}

			ASSERT(segments > 0);
			nsegs += segments;
			if (segments > txq->stats.max_dma_segs_in_mblk)
				txq->stats.max_dma_segs_in_mblk = segments;

			meta->state = use_dma;

add_next_cookie:
			buf_pa = cookie.dmac_laddress;
			buf_len = (int)cookie.dmac_size;

		}

		/* Fill in the sg entry for this buf */
		sgp->len[sgl_indx & 1] = BE_32(buf_len);
		sgp->addr[sgl_indx & 1] = BE_64(buf_pa);
		remaining -= buf_len;
		segments--;

		/* One sg entry used, flits will go up by 1 or 2 */
		sgl_indx++;
		if ((sgl_indx & 1) == 0) {
			flits++;
			sgp++;
		}
		flits++;

		if (remaining == 0 || sgl_indx == MAX_SGL_LEN) {

			/*
			 * We are here because one or both of these occurred:
			 * a) all segments of this mblk chain (the whole chain,
			 *    not just this mblk) are in the sgl.
			 * b) we've run out of sg entries in the sgl of the
			 *    current tx descriptor.
			 *
			 * Either way, it is time to send the current tx
			 * descriptor on its way and move on to the next one.
			 */

			if (!remaining)
				ASSERT(segments == 0);

			/* Update the number of flits in the wr */
			wr_lo |= htonl(V_WR_LEN(flits + (sgl_indx & 1)));

			/* Add an EOP if this is the last tx desc */
			if (remaining == 0)
				wr_hi |= htonl(F_WR_EOP);

			/* Write the work request header for this descriptor */
			wr = (void *)txd;
			wr->wr_hi = wr_hi;
			wr->wr_lo = wr_lo;
#if SGE_NUM_GENBITS == 2
			txd->flit[TX_DESC_FLITS - 1] = BE_64(hwq->gen_bit);
#endif

			/* We've used up a tx descriptor */
			hw_cnt++;
			txd++;
			if (++hwq->wr_index == hwq->depth) {
				hwq->wr_index = 0;
				txd = hwq->queue;
				hwq->gen_bit ^= 1;
			}

			/*
			 * Rest is for a tx desc that's NOT the first one.
			 * Don't bother if there's no data left.
			 */
			if (remaining) {
				wr_hi = htonl(V_WR_DATATYPE(1) |
				    V_WR_SGLSFLT(1) |
				    V_WR_OP(FW_WROPCODE_TUNNEL_TX_PKT));

				wr_lo = htonl(V_WR_TID(txq->token) |
				    V_WR_GEN(hwq->gen_bit));

				sgl_indx = 0;
				flits = 1;
				sgp = (struct sg_ent *)&txd->flit[flits];
			}
		}

		/*
		 * Take care of any remaining segments in the current cookie.
		 */
		if (segments) {
			ddi_dma_nextcookie(meta->dma_hdl, &cookie);
			goto add_next_cookie;
		}

		/* One software descriptor has been consumed */
		meta_cnt++;
		meta++;
		if (++metaq->wr_index == metaq->depth) {
			metaq->wr_index = 0;
			meta = metaq->queue;
		}
		/*
		 * The next desc will NOT be the head of the chain (meta0 is the
		 * head).  We'll try and catch sw descriptor ring corruption in
		 * sge_xmit_reclaim by making these ASSERTions:
		 *
		 * a) A sw desc at the head of the chain must have a message
		 *    (except for immediate_data), hw_cnt, and meta_cnt
		 * b) A sw desc not at the head must NOT have any of the 3
		 *    listed in (a) above.
		 * c) Any sw desc up for reclaim, head or not, must not have an
		 *    unused state.
		 */
		meta->message = NULL;
		meta->hw_cnt = 0;
		meta->meta_cnt = 0;
		meta->state = unused;
	}

	if (nsegs > txq->stats.max_dma_segs_in_frame)
		txq->stats.max_dma_segs_in_frame = nsegs;

tx_send:

	ASSERT(hw_cnt);
	ASSERT(hw_cnt <= needed); /* Wrong estimate can trash the tx ring */
	ASSERT(hw_cnt <= 4); /* This is the upper limit in the hardware */
	ASSERT(meta_cnt);

	hwq->credits -= hw_cnt;
	txq->unacked += hw_cnt;
	ASSERT(hwq->credits > 0);

	/* sge_xmit_reclaim will use these to reclaim descriptors */
	meta0->hw_cnt = hw_cnt;
	meta0->meta_cnt = meta_cnt;
	meta0->message = mp;

	/*
	 * All set. About to hand off to hardware.
	 */

	/* Flip the gen bit on the first descriptor. */
	wr = (void *)txd0;
	wr->wr_lo ^= htonl(F_WR_GEN);

	/* Make sure all our work is visible to the hardware */
	if (txd > txd0) {
		(void) ddi_dma_sync(hwq->dma_hdl, _PTRDIFF(txd0, hwq->queue),
		    sizeof (*txd) * hw_cnt, DDI_DMA_SYNC_FORDEV);
	} else {
		ASSERT(txd != txd0); /* can't happen when hw_cnt > 0 */

		/* Wrapped around, two syncs may be needed.  Do in reverse */
		if (hwq->wr_index > 0) {
			(void) ddi_dma_sync(hwq->dma_hdl, 0, sizeof (*txd) *
			    hwq->wr_index, DDI_DMA_SYNC_FORDEV);
		}

		(void) ddi_dma_sync(hwq->dma_hdl, _PTRDIFF(txd0, hwq->queue),
		    _PTRDIFF(&hwq->queue[hwq->depth], txd0),
		    DDI_DMA_SYNC_FORDEV);
	}

	/* Update Tx ring statistics */
	txq->stats.tx_frames++;
	txq->stats.tx_octets += tot_len;

	mutex_exit(&txq->lock);

	/* Ring the doorbell if asked to */
	if (db)
		t3_write_reg(pi->adapter, A_SG_KDOORBELL, F_SELEGRCNTX |
		    V_EGRCNTX(txq->cntxt_id));

	return (0);

tx_abort:

	/*
	 * Back out the changes to the hardware queue.
	 */
	if (hwq->wr_index < hw_cnt) {
		hwq->wr_index = hwq->depth + hwq->wr_index - hw_cnt;
		hwq->gen_bit ^= 1;
	} else
		hwq->wr_index -= hw_cnt;
	/* Our location and gen bit must match what we had at the start. */
	ASSERT(txd0 == &hwq->queue[hwq->wr_index]);
	ASSERT(txd0_gen == hwq->gen_bit);

	/* Invalidate the gen bits of all tx descriptors that we filled */
	idx = hwq->wr_index;
	for (i = 0; i < hw_cnt; i++) {
		txd = &hwq->queue[idx];
		wr = (void *)txd;
		wr_lo = htonl(wr->wr_lo);

		/*
		 * gen bit in work request should be invalid for first, valid
		 * for the rest.  We'll invalidate all.
		 */
		if (i == 0) {
			ASSERT((wr_lo & F_WR_GEN) != V_WR_GEN(txd0_gen));
		} else {
			ASSERT((wr_lo & F_WR_GEN) == V_WR_GEN(txd0_gen));
			wr_lo ^= F_WR_GEN;
			wr->wr_lo = htonl(wr_lo);
		}

#if SGE_NUM_GENBITS == 2
		/* 2nd gen bit should be valid, we'll invalidate it */
		ASSERT(txd->flit[TX_DESC_FLITS - 1] == BE_64(txd0_gen));
		txd->flit[TX_DESC_FLITS - 1] = BE_64(txd0_gen ^ 1);
#endif

		if (++idx == hwq->depth) {
			txd0_gen ^= 1;
			idx = 0;
		}
	}

	/*
	 * Back out the changes to the software queue.
	 */
	if (metaq->wr_index < meta_cnt)
		metaq->wr_index = metaq->depth + metaq->wr_index - meta_cnt;
	else
		metaq->wr_index -= meta_cnt;
	/* Our location must match what we had at the start */
	ASSERT(meta0 == &metaq->queue[metaq->wr_index]);

	/* Free any DMA resources. */
	idx = metaq->wr_index;
	for (i = 0; i < meta_cnt; i++) {

		meta = &metaq->queue[idx];

		/* Set only for successful tx, must not be set here. */
		ASSERT(meta->hw_cnt == 0);
		ASSERT(meta->meta_cnt == 0);
		ASSERT(meta->message == 0);

		/* meta_cnt indicates we used this, so state must be valid. */
		ASSERT(meta->state != unused);
		if (meta->state == use_dma)
			(void) ddi_dma_unbind_handle(meta->dma_hdl);
		meta->state = unused;

		if (++idx == metaq->depth)
			idx = 0;
	}

	/*
	 * Return a tx error and let the stack retry.  This is based on the hope
	 * that the tx error was due to transient problems.  Reclaiming all
	 * descriptors may help free some DMA resources.
	 */
	(void) sge_xmit_reclaim(txq, -1);
	/* So that tx_update gets called eventually */
	txq->stats.txq_blocked++;
	txq->queueing = B_TRUE;
	mutex_exit(&txq->lock);

	return (1);
}

#define	FEW_DESCS	32
static int
sge_xmit_reclaim(struct sge_txq *txq, int descs_needed)
{
	egr_hwq_t *hwq = &txq->hwq;
	egr_metaq_t *mq = &txq->metaq;
	p_egr_meta_t meta;
	int crd_index, reclaimable, target, i, reclaimed, hw_cnt, meta_cnt;

	ASSERT(mutex_owned(&txq->lock));

	crd_index = hwq->crd_index;

	/* LINTED - E_SUSPICIOUS_COMPARISON */
	ASSERT(hwq->wr_index >= 0 && hwq->wr_index < hwq->depth);
	ASSERT(crd_index >= 0 && crd_index < hwq->depth);
	/* LINTED - E_SUSPICIOUS_COMPARISON */
	ASSERT(hwq->rd_index >= 0 && hwq->rd_index < hwq->depth);

	/* crd_index must be "between" rd and wr indexes */
	if (hwq->wr_index > hwq->rd_index)
		ASSERT(crd_index <= hwq->wr_index &&
		    crd_index >= hwq->rd_index);
	else
		ASSERT(crd_index <= hwq->wr_index ||
		    crd_index >= hwq->rd_index);

	/* Total descriptors released by the hardware but yet to be reclaimed */
	reclaimable = crd_index - hwq->rd_index;
	if (reclaimable < 0)
		reclaimable += hwq->depth;

	/*
	 *  Establish the target number of descs we'll reclaim.  If we're called
	 *  from rx, we won't reclaim more than a few descriptors.  Otherwise
	 *  we'll reclaim everything we can.
	 */
	if (descs_needed == 0)
		target = min(reclaimable, FEW_DESCS);
	else
		target = reclaimable;

	meta = &mq->queue[mq->rd_index];
	reclaimed = 0;
	while (reclaimed < target) {
		hw_cnt = meta->hw_cnt;
		meta_cnt = meta->meta_cnt;
		mblk_t *msg;

		/*
		 * This meta has the details of the original sge_tx_data
		 * request that's about to be reclaimed.
		 */
		ASSERT(hw_cnt > 0);	/* # of hw descs that we used */
		ASSERT(meta_cnt > 0);	/* # of sw descs that we used */

		/* Anything except immediate data must have an mblk */
		if (meta->state != immediate_data)
			ASSERT(meta->message);

		/* Reclaim only when the entire tx request is complete */
		if (reclaimable < hw_cnt)
			break;

		msg = meta->message;

		for (i = 0; i < meta_cnt; i++) {

			/* Only the head meta has this info */
			if (i > 0) {
				ASSERT(meta->hw_cnt == 0);
				ASSERT(meta->meta_cnt == 0);
				ASSERT(meta->message == NULL);
			}

			ASSERT(meta->state != unused);
			if (meta->state == use_dma)
				(void) ddi_dma_unbind_handle(meta->dma_hdl);

			/*
			 * Don't reset meta->state to unused, see the comment
			 * above freemsg
			 */

			meta++;
			if (++mq->rd_index == mq->depth) {
				mq->rd_index = 0;
				meta = mq->queue;
			}
		}

		/*
		 * Free the mblks that have been tx'ed.  Do this after DMA
		 * unbind.  Do NOT scrub the meta clean.  sge_tx_data will do
		 * that after it has cycled through the metas and comes back to
		 * this one.  It helps in debugging to have historical/stale
		 * values available in the sw descriptor.
		 */
		if (msg)
			freemsg(msg);

		reclaimed += hw_cnt;
		reclaimable -= hw_cnt;
		ASSERT(reclaimable >= 0);
	}

	/* Advance the hardware queue's read index and credits */
	hwq->rd_index += reclaimed;
	if (hwq->rd_index >= hwq->depth)
		hwq->rd_index -= hwq->depth;

	hwq->credits += reclaimed;
	ASSERT(hwq->credits <= hwq->depth);

	if (descs_needed > 0 && hwq->credits <= descs_needed) {
		/* tx jammed due to lack of descriptors */
		txq->stats.txq_blocked++;
		txq->queueing = B_TRUE;
		return (1);
	}

	return (0);
}

static void
sge_tx_ctrl(struct sge_qset *qs, mblk_t *mp)
{
	struct sge_txq *txq = &qs->txq[TXQ_CTRL];
	egr_hwq_t *hwq = &txq->hwq;
	struct work_request_hdr *wr_to, *wr_from;
	struct tx_desc *txd;
	int len;

	/* For the time being we allow/activate only qs0's ctrlq */
	ASSERT(qs->idx == 0);

	len = MBLKL(mp);

	/* Must be a simple mblk that can be sent as immediate data */
	ASSERT(mp->b_cont == NULL);
	ASSERT(len <= (WR_FLITS * 8));

	mutex_enter(&txq->lock);

	/* reclaim */
	hwq->rd_index = hwq->crd_index;

	txd = &hwq->queue[hwq->wr_index];
	wr_to = (void *)txd;
	wr_from = (void *)mp->b_rptr;

	bcopy(&wr_from[1], &wr_to[1], len - sizeof (struct work_request_hdr));
	wr_to->wr_hi = wr_from->wr_hi | htonl(F_WR_EOP | F_WR_SOP |
	    V_WR_BCNTLFLT(len & 7));
	wr_to->wr_lo = wr_from->wr_lo | htonl(V_WR_GEN(hwq->gen_bit) |
	    V_WR_LEN((len + 7) / 8));
#if SGE_NUM_GENBITS == 2
	txd->flit[TX_DESC_FLITS - 1] = BE_64(hwq->gen_bit);
#endif
	(void) ddi_dma_sync(hwq->dma_hdl, hwq->wr_index * sizeof (*txd),
	    sizeof (*txd), DDI_DMA_SYNC_FORDEV);

	txq->stats.tx_imm_data++;
	if (++hwq->wr_index == hwq->depth) {
		hwq->wr_index = 0;
		hwq->gen_bit ^= 1;
	}
	mutex_exit(&txq->lock);

	t3_write_reg(qs->port->adapter, A_SG_KDOORBELL, F_SELEGRCNTX |
	    V_EGRCNTX(txq->cntxt_id));
}

void
bind_qsets(p_adapter_t cxgenp)
{
	struct mngt_pktsched_wr *req;
	int i, j;
	mblk_t *m;
	struct sge_qset *qs = &cxgenp->sge.qs[0];

	m = allocb(sizeof (*req), BPRI_HI);
	if (!m)
		return;

	req = (void *)m->b_rptr;
	m->b_wptr = m->b_rptr + sizeof (*req);
	for (i = 0; i < cxgenp->params.nports; i++) {
		const struct port_info *pi = &cxgenp->port[i];

		for (j = 0; j < pi->nqsets; j++) {

			bzero(req, sizeof (*req));
			req->wr_hi = htonl(V_WR_OP(FW_WROPCODE_MNGT));
			req->mngt_opcode = FW_MNGTOPCODE_PKTSCHED_SET;
			req->sched = 1;
			req->idx = pi->first_qset + j;
			req->min = (uint8_t)-1;
			req->max = (uint8_t)-1;
			req->binding = pi->tx_chan;

			sge_tx_ctrl(qs, m);
		}
	}

	freeb(m);
}

int
sge_qs_intr_enable(mac_ring_driver_t arg)
{
	struct sge_qset *qs = (struct sge_qset *)arg;
	struct sge_rspq *q = &qs->rspq;

	mutex_enter(&q->lock);
	q->polling = 0;
	t3_write_reg(qs->port->adapter, A_SG_GTS, V_RSPQ(q->cntxt_id) |
	    V_NEWTIMER(q->holdoff_tmr) | V_NEWINDEX(q->index));
	mutex_exit(&q->lock);

	return (0);
}

int
sge_qs_intr_disable(mac_ring_driver_t arg)
{
	struct sge_qset *qs = (struct sge_qset *)arg;
	struct sge_rspq *q = &qs->rspq;

	mutex_enter(&q->lock);
	q->polling = 1;
	t3_write_reg(qs->port->adapter, A_SG_GTS, V_RSPQ(q->cntxt_id) |
	    V_NEWTIMER(0) | V_NEWINDEX(q->index));
	mutex_exit(&q->lock);

	return (0);
}
