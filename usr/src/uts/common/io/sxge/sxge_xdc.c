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

#include "sxge.h"

extern	uint32_t sxge_dbg_en;
extern	uint_t sxge_jumbo_frame_sz;
#define	sxge_tdc_maxframe	(sxge_jumbo_frame_sz)

uint_t sxge_xdc_debug 		= 1;
uint_t sxge_tdc_csum 		= 1;
uint_t sxge_tdc_kicks 		= 0;
uint_t sxge_tdc_mbx 		= 0;

uint_t sxge_tdc_reinit_en	= 1;
uint_t sxge_rdc_reinit_en	= 1;

uint_t sxge_tdc_nr		= TDC_NR_DESC;

uint_t sxge_rdc_rbr_nr		= RDC_NR_DESC;
uint_t sxge_rdc_rcr_nr		= RDC_NR_RCDESC;

uint_t sxge_rdc_buf_sz		= RDC_BUF_SIZE;
uint_t sxge_tdc_buf_sz		= TDC_BUF_SIZE;

uint_t sxge_rdc_tiny_sz		= RDC_TINY_SIZE;
uint_t sxge_tdc_tiny_sz		= TDC_TINY_SIZE;

uint_t sxge_rdc_csum 		= 1;

uint_t sxge_rdc_pthres_en	= 1;
uint_t sxge_rdc_pthres		= 1;
uint_t sxge_rdc_timer_en	= 1;
uint_t sxge_rdc_timer		= 60;
uint_t sxge_rdc_mbx_en 		= 0;
uint_t sxge_rdc_mbx_pthres 	= 8;

uint_t sxge_rdc_poll_usec	= 16;
uint_t sxge_rdc_poll_max	= 4;

uint_t sxge_rdc_fcoe_en		= 1;

uint64_t sxge_rdc_err_msk	= 0xFFULL;
uint64_t sxge_tdc_err_msk	= 0x1FFFULL;

uint_t sxge_rdc_dbg_sz		= 8;

uint_t sxge_tdc_cs_dbg		= 0;
uint_t sxge_tdc_stuff_en	= 0;
uint_t sxge_tdc_flush_en	= 0;

uint_t sxge_dbg_pkts		= 0;

uint_t sxge_rxmb_reuse 		= 1;
uint_t sxge_rxmb_nwait		= 1;
uint_t sxge_rxmb_allocb		= 1;

uint_t sxge_rxmb_lock		= 0;
uint64_t sxge_rxmb_pend 	= 0;
uint64_t sxge_rxmb_pend_max 	= 0xffff;

uint_t sxge_txmb_reuse 		= 1;

#undef SXGE_DBG
#undef SXGE_DBG0

#define	SXGEP (sxgep), (SXGE_ERR_BLK|SXGE_ERR_PRI)
#ifdef SXGE_DEBUG
#define	SXGE_DBG(params)	sxge_dbg_msg params
#define	SXGE_DBG0(params)	sxge_dbg_msg params
#else
#define	SXGE_DBG(params)
#define	SXGE_DBG0(params)	sxge_dbg_msg params
#endif

static ddi_device_acc_attr_t sxge_dev_dma_attr = {
	DDI_DEVICE_ATTR_V0,
	DDI_STRUCTURE_LE_ACC,
	DDI_STRICTORDER_ACC
};

static ddi_device_acc_attr_t sxge_buf_dma_attr = {
	DDI_DEVICE_ATTR_V0,
	DDI_STRUCTURE_BE_ACC,
	DDI_STRICTORDER_ACC
};

static ddi_dma_attr_t sxge_desc_attr = {
	DMA_ATTR_V0,		/* version number. */
	0,			/* low address */
	0xffffffffffffffff,	/* high address */
	0xffffffffffffffff,	/* address counter max */
	0x1000,			/* alignment */
	0xfc00fc,		/* dlim_burstsizes */
	0x1,			/* minimum transfer size */
	0xffffffffffffffff,	/* maximum transfer size */
	0xffffffffffffffff,	/* maximum segment size */
	1,			/* scatter/gather list length */
	(unsigned int) 1,	/* granularity */
	0			/* attribute flags */
};

static ddi_dma_attr_t sxge_tx_dma_attr = {
	DMA_ATTR_V0,		/* version number. */
	0,			/* low address */
	0xffffffffffffffff,	/* high address */
	0xffffffffffffffff,	/* address counter max */
	0x1,			/* alignment */
	0xfc00fc,		/* dlim_burstsizes */
	0x1,			/* minimum transfer size */
	0xffffffffffffffff,	/* maximum transfer size */
	0xffffffffffffffff,	/* maximum segment size */
	1,			/* scatter/gather list length */
	(unsigned int) 1,	/* granularity */
	0			/* attribute flags */
};

static ddi_dma_attr_t sxge_tx_buf_attr = {
	DMA_ATTR_V0,		/* version number. */
	0,			/* low address */
	0xffffffffffffffff,	/* high address */
	0xffffffffffffffff,	/* address counter max */
	0x1000,			/* alignment */
	0xfc00fc,		/* dlim_burstsizes */
	0x1,			/* minimum transfer size */
	0xffffffffffffffff,	/* maximum transfer size */
	0xffffffffffffffff,	/* maximum segment size */
	1,			/* scatter/gather list length */
	(unsigned int) 1,	/* granularity */
	0			/* attribute flags */
};

static ddi_dma_attr_t sxge_rx_buf_attr = {
	DMA_ATTR_V0,		/* version number. */
	0,			/* low address */
	0xffffffffffffffff,	/* high address */
	0xffffffffffffffff,	/* address counter max */
	0x1000,			/* alignment */
	0xfc00fc,		/* dlim_burstsizes */
	0x1,			/* minimum transfer size */
	0xffffffffffffffff,	/* maximum transfer size */
	0xffffffffffffffff,	/* maximum segment size */
	1,			/* scatter/gather list length */
	(unsigned int) 1,	/* granularity */
	0			/* attribute flags */
};

static ddi_dma_attr_t sxge_buf_attr = {
	DMA_ATTR_V0,		/* version number. */
	0,			/* low address */
	0xffffffffffffffff,	/* high address */
	0xffffffffffffffff,	/* address counter max */
	0x1,			/* alignment */
	0xfc00fc,		/* dlim_burstsizes */
	0x1,			/* minimum transfer size */
	0xffffffffffffffff,	/* maximum transfer size */
	0xffffffffffffffff,	/* maximum segment size */
	1,			/* scatter/gather list length */
	(unsigned int) 1,	/* granularity */
	0			/* attribute flags */
};

static ddi_dma_attr_t sxge_abuf_attr = {
	DMA_ATTR_V0,		/* version number. */
	0,			/* low address */
	0xffffffffffffffff,	/* high address */
	0xffffffffffffffff,	/* address counter max */
	0x1000,			/* alignment */
	0xfc00fc,		/* dlim_burstsizes */
	0x1,			/* minimum transfer size */
	0xffffffffffffffff,	/* maximum transfer size */
	0xffffffffffffffff,	/* maximum segment size */
	1,			/* scatter/gather list length */
	(unsigned int) 1,	/* granularity */
	0			/* attribute flags */
};

void
sxge_udelay(unsigned int usecs)
{
	drv_usecwait(usecs);
}

/*
 * sxge_allocb
 */

mblk_t *
sxge_allocb(size_t size, uint_t pri)
{
	mblk_t *mp;
	mp = allocb(size, pri);
	return (mp);
}

/*
 * sxge_desballoc
 */

mblk_t *
sxge_desballoc(uchar_t *base, size_t size, uint_t pri, frtn_t *fr_rtnp)
{
	mblk_t *mp;
	mp = desballoc(base, size, pri, fr_rtnp);
	return (mp);
}

mblk_t *
sxge_dupb(mblk_t *mp)
{
	mblk_t *nmp;
	nmp = dupb(mp);
	return (nmp);
}

/*
 * sxge_freeb
 * also see free_rtn(9S):void (*free_func)(), caddr_t free_arg
 */
void
sxge_freeb(mblk_t *mp)
{
	/* mblk_t *nmp; */	/* unused */
	freeb(mp);
}

/*
 * sxge_freemsg
 */

void
sxge_freemsg(mblk_t *mp)
{
	/* mblk_t *nmp; */	/* unused */
	freemsg(mp);
}

int
sxge_msgsize(mblk_t *mp)
{
	int cnt = 0;
	cnt = msgsize(mp);
	return (cnt);
}

/* ARGSUSED */
int
sxge_buf_unmap(sxge_t *sxgep, ddi_dma_handle_t *h, ddi_acc_handle_t *mh)
{
	int			status = SXGE_SUCCESS;
	/* size_t			real_len; */	/* unused */

	SXGE_DBG((SXGEP, "==>sxge_buf_unmap: h %llx mh %llx ", h, mh));
	/*
	 * Undo binds and mappings
	 */

	if (h) (void) ddi_dma_unbind_handle(*h);

	if (mh) ddi_dma_mem_free(mh);

	if (h) ddi_dma_free_handle(h);

	goto sxge_buf_unmap_exit;

sxge_buf_unmap_fail:

	status = SXGE_FAILURE;

sxge_buf_unmap_exit:
	SXGE_DBG((SXGEP, "<==sxge_buf_unmap: completed\n"));

	return (status);
}

int
sxge_buf_map(sxge_t *sxgep, uint_t align, ddi_dma_handle_t *h,
	ddi_acc_handle_t *mh, void **vp, ddi_dma_cookie_t *pp,
	uint_t *len, uint_t *cc)
{
	ddi_dma_handle_t	buf_h;
	ddi_acc_handle_t 	buf_mh;
	void			*buf_vp;
	ddi_dma_cookie_t 	buf_pp;
	uint_t			count;
	size_t			real_len;
	int			status = SXGE_SUCCESS;

	SXGE_DBG((SXGEP, "==>sxge_buf_map: enter\n"));

	/*
	 * Initialize descriptor and buf handles
	 */
	buf_h		= *h;
	buf_mh		= *mh;
	buf_vp		= *vp;
	/* buf_pp	= NULL; */
	real_len 	= (size_t)*len;

	if (buf_h == NULL) {
		if (align)
			status = ddi_dma_alloc_handle(sxgep->dip,
			    &sxge_abuf_attr, DDI_DMA_DONTWAIT, 0, &buf_h);
		else
			status = ddi_dma_alloc_handle(sxgep->dip,
			    &sxge_buf_attr, DDI_DMA_DONTWAIT, 0, &buf_h);

		if (status != SXGE_SUCCESS) {
			SXGE_DBG((SXGEP,
			    "sxge_buf_map: buf_h ddi_dma_alloc_handle fail"));
			goto sxge_buf_map_fail;
		}

		*h = buf_h;
	}

	if ((buf_vp == NULL) && (buf_mh == NULL)) {
		status = ddi_dma_mem_alloc(buf_h, *len,
		    &sxge_buf_dma_attr, DDI_DMA_CONSISTENT,
		    DDI_DMA_DONTWAIT, 0,
		    (caddr_t *)&buf_vp,
		    &real_len, &buf_mh);

		if (status != SXGE_SUCCESS) {
			SXGE_DBG((SXGEP,
			    "sxge_buf_map: buf_mh ddi_dma_mem_alloc fail"));
			goto sxge_buf_map_fail1;
		}

		*mh = buf_mh;
		*vp = buf_vp;
		*len = (uint_t)real_len;
	}

	status = ddi_dma_addr_bind_handle(buf_h, NULL,
	    (caddr_t)buf_vp, real_len,
	    DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
	    /* DDI_DMA_WRITE | DDI_DMA_CONSISTENT, */
	    DDI_DMA_DONTWAIT, 0,
	    &buf_pp, &count);

	if (status != DDI_DMA_MAPPED) {
		SXGE_DBG((SXGEP,
		    "sxge_buf_map: buf_pp ddi_dma_addr_bind_handle fail"));
		goto sxge_buf_map_fail2;
	}

	*pp = buf_pp;
	*cc = count;

	goto sxge_buf_map_exit;

sxge_buf_map_fail2:
	(void) ddi_dma_mem_free(&buf_mh);
sxge_buf_map_fail1:
	(void) ddi_dma_free_handle(&buf_h);
sxge_buf_map_fail:

	status = SXGE_FAILURE;

sxge_buf_map_exit:
	SXGE_DBG((SXGEP, "<==sxge_buf_map: completed\n"));

	return (status);
}

/* ARGSUSED */
int
sxge_buf_unbind(sxge_t *sxgep, ddi_dma_handle_t h)
{
	int			status = SXGE_SUCCESS;
	/* size_t			real_len; */	/* unused */

	SXGE_DBG((SXGEP, "==>sxge_buf_unbind: h %llx ", h));

	/*
	 * Undo binds and mappings
	 */

	if (h) (void) ddi_dma_unbind_handle(h);

	goto sxge_buf_unbind_exit;

sxge_buf_unbind_fail:

	status = SXGE_FAILURE;

sxge_buf_unbind_exit:
	SXGE_DBG((SXGEP, "<==sxge_buf_unbind: completed\n"));

	return (status);
}

/* ARGSUSED */
int
sxge_buf_bind(sxge_t *sxgep, uint_t align, ddi_dma_handle_t h,
	void *vp, ddi_dma_cookie_t *pp, size_t *len, uint_t *cc)
{
	ddi_dma_handle_t	buf_h;
	/* ddi_acc_handle_t 	buf_mh; */	/* unused */
	void			*buf_vp;
	ddi_dma_cookie_t 	buf_pp;
	uint_t			count;
	size_t			real_len;
	int			status = SXGE_SUCCESS;

	SXGE_DBG((SXGEP, "==>sxge_buf_bind "));
	/*
	 * Initialize descriptor and buf handles
	 */
	buf_h		= h;
	buf_vp		= vp;
	real_len 	= (size_t)*len;

	status = ddi_dma_addr_bind_handle(buf_h, NULL,
	    (caddr_t)buf_vp, real_len,
	    DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
	    /* DDI_DMA_READ| DDI_DMA_CONSISTENT, */
	    DDI_DMA_DONTWAIT, 0,
	    &buf_pp, &count);

	if (status != DDI_DMA_MAPPED) {
		SXGE_DBG((SXGEP,
		    "sxge_buf_bind: buf_pp ddi_dma_addr_bind_handle fail"));
		goto sxge_buf_bind_fail;
	}

	*pp = buf_pp;
	*cc = count;

	goto sxge_buf_bind_exit;

sxge_buf_bind_fail:

	status = SXGE_FAILURE;

sxge_buf_bind_exit:
	SXGE_DBG((SXGEP, "<==sxge_buf_bind: completed\n"));

	return (status);
}


void
sxge_freem(sxge_msg_t *sxge_mbp)
{
	/*
	 * size_t 			size;
	 * uchar_t 		*buffer = NULL;
	 */
	/* set but not used */
	sxge_t 			*sxgep = (sxge_t *)sxge_mbp->sxgep;
	if (sxge_rxmb_lock)
	MUTEX_ENTER(&sxge_mbp->lock);
	if (0 == atomic_add_32_nv(&sxge_mbp->ref_cnt, -1)) {
		/*
		 * buffer = sxge_mbp->buffer;
		 * size = sxge_mbp->asize;
		 */
		/* set but not used */
		atomic_add_64(&sxge_rxmb_pend, -1);
		if (sxge_mbp->pre_buf == 0)
		(void) sxge_buf_unmap(sxgep, &sxge_mbp->buffer_h,
		    &sxge_mbp->buffer_mh);

		if (sxge_rxmb_lock) {
		MUTEX_EXIT(&sxge_mbp->lock);
		MUTEX_DESTROY(&sxge_mbp->lock);
		}

		SXGE_FREE(sxge_mbp, sizeof (sxge_msg_t));
		return;
	}
	if (sxge_rxmb_lock)
	MUTEX_EXIT(&sxge_mbp->lock);
}

sxge_msg_t *
sxge_allocm(sxge_t *sxgep, size_t size, uint_t pri, uchar_t *buf)
{
	sxge_msg_t 		*sxge_mbp = NULL;
	uchar_t 		*buffer, *buffer_a;
	uint_t 			i, len = (uint_t)size;
	uint_t 			align = (buf == NULL)?1:0;
	size_t 			asize = size;
	int 			status = SXGE_FAILURE;

	sxge_mbp = SXGE_ZALLOC(sizeof (sxge_msg_t));
	if (sxge_mbp == NULL) {
		SXGE_DBG((SXGEP, "sxge_allocm: alloc sxge_msg_t failed\n"));
		goto sxge_allocm_exit;
	}

	if (buf != NULL) sxge_mbp->buffer_vp = (caddr_t)buf;

	status = sxge_buf_map(sxgep, align, &sxge_mbp->buffer_h,
	    &sxge_mbp->buffer_mh, &sxge_mbp->buffer_vp,
	    &sxge_mbp->buffer_pp, &len,
	    &sxge_mbp->buffer_cc);

	if (status != SXGE_SUCCESS) {
		SXGE_DBG((SXGEP, "sxge_allocm: sxge_buf_mapfailed\n"));
		goto sxge_allocm_fail2;
	}
	buffer = (uchar_t *)sxge_mbp->buffer_vp;
	buffer_a = buffer;
	asize = size;

	sxge_mbp->mp = SXGE_ALLOCD(buffer_a, size, pri, &sxge_mbp->freeb);
	if (sxge_mbp->mp == NULL) {
		SXGE_DBG((SXGEP, "sxge_allocm: allocd sxge_mbp->mp failed\n"));
		goto sxge_allocm_fail1;
	}

	sxge_mbp->sxgep = (void *)sxgep;
	if (sxge_rxmb_lock)
	MUTEX_INIT(&sxge_mbp->lock, NULL, MUTEX_DRIVER, NULL);
	sxge_mbp->buffer = buffer;
	sxge_mbp->buffer_a = buffer_a;
	sxge_mbp->pre_buf = (buf == NULL)?0:1;
	sxge_mbp->size = size;
	sxge_mbp->asize = asize;
	sxge_mbp->pri = pri;
	sxge_mbp->freeb.free_func = (void (*)())SXGE_FREEM;
	sxge_mbp->freeb.free_arg = (caddr_t)sxge_mbp;
	sxge_mbp->ref_cnt = 1;
	sxge_mbp->in_use = 0;
	sxge_mbp->in_bytes = 0;
	sxge_mbp->in_last = 0;
	sxge_mbp->in_bufsz = (uint_t)-1;
	sxge_mbp->nmp = NULL;
	for (i = 0; i < 4; i++) sxge_mbp->in_prev[i] = NULL;
	atomic_add_64(&sxge_rxmb_pend, 1);
	goto sxge_allocm_exit;

sxge_allocm_fail1:
	(void) sxge_buf_unmap(sxgep, &sxge_mbp->buffer_h, &sxge_mbp->buffer_mh);
sxge_allocm_fail2:
	SXGE_DBG0((SXGEP, "sxge_allocm: failed %p", sxgep));
	SXGE_FREE(sxge_mbp, sizeof (sxge_msg_t));
	sxge_mbp = NULL;

sxge_allocm_exit:
	return (sxge_mbp);
}

mblk_t *
sxge_dupm(sxge_msg_t *sxge_mbp, uint_t offset, size_t size)
{
	mblk_t 			*mp = NULL;
#ifdef SXGE_DEBUG
	sxge_t 			*sxgep = (sxge_t *)sxge_mbp->sxgep;

	if (sxge_mbp == NULL) {
		SXGE_DBG((SXGEP,
		    "sxge_dupm: trying to duplicate a null pointer."));
		goto sxge_dupm_exit;
	}
#endif

	mp = SXGE_ALLOCD(sxge_mbp->buffer_a, sxge_mbp->size,
	    sxge_mbp->pri, &sxge_mbp->freeb);

	if (mp != NULL) {
		mp->b_rptr += offset;
		mp->b_wptr = mp->b_rptr + size;
	} else {
		SXGE_DBG((SXGEP, "sxge_dupm: desballoc failed"));
		goto sxge_dupm_exit;
	}

	if (sxge_rxmb_lock)
	MUTEX_ENTER(&sxge_mbp->lock);

	atomic_add_32(&sxge_mbp->ref_cnt, 1);

	if (sxge_rxmb_lock)
	MUTEX_EXIT(&sxge_mbp->lock);
sxge_dupm_exit:
	return (mp);
}

int
sxge_tdc_map(tdc_state_t *tdcp)
{
	sxge_t			*sxgep = (sxge_t *)tdcp->sxgep;
	/*
	 * uint_t		cc = tdcp->tdcn;
	 * uint64_t		pbase = tdcp->pbase;
	 * sxge_pio_handle_t	phdl = tdcp->phdl;
	 */
	/* set but not used */
	uint_t 			i, len, count;
	size_t			real_len;
	sxge_msg_t		*sxge_mbp = NULL;
	int			status = SXGE_SUCCESS;

	/*
	 * Initialize descriptor and buf handles
	 */
	tdcp->desc_h		= NULL;
	tdcp->desc_mh		= NULL;
	tdcp->desc_vp		= NULL;
	/* tdcp->desc_pp	= NULL; */

	/*
	 * Allocate descriptor handles and map them
	 */
	status = ddi_dma_alloc_handle(sxgep->dip, &sxge_desc_attr,
	    DDI_DMA_DONTWAIT, 0, &tdcp->desc_h);

	if (status != SXGE_SUCCESS) {
		SXGE_DBG((SXGEP,
		    "sxge_tdc_map: desc_h ddi_dma_alloc_handle fail"));
		goto sxge_tdc_map_fail;
	}

	len = tdcp->size * sizeof (tdc_desc_t);
	status = ddi_dma_mem_alloc(tdcp->desc_h, len,
	    &sxge_dev_dma_attr, DDI_DMA_CONSISTENT,
	    DDI_DMA_DONTWAIT, 0,
	    (caddr_t *)&tdcp->desc_vp,
	    &real_len, &tdcp->desc_mh);

	if (status != SXGE_SUCCESS) {
		SXGE_DBG((SXGEP,
		    "sxge_tdc_map: desc_mh ddi_dma_mem_alloc fail"));
		goto sxge_tdc_map_fail;
	}


	status = ddi_dma_addr_bind_handle(tdcp->desc_h, NULL,
	    (caddr_t)tdcp->desc_vp, real_len,
	    DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
	    /* DDI_DMA_WRITE | DDI_DMA_CONSISTENT, */
	    DDI_DMA_DONTWAIT, 0,
	    &tdcp->desc_pp, &count);

	if (status != DDI_DMA_MAPPED) {
		SXGE_DBG((SXGEP,
		    "sxge_tdc_map: desc_pp ddi_dma_addr_bind_handle fail"));
		goto sxge_tdc_map_fail;
	}

	/*
	 * Clear descriptor memory
	 */
	bzero((caddr_t)tdcp->desc_vp, real_len);

	/*
	 * Allocate buffer handles and map them
	 */

	tdcp->txb_h		= NULL;
	tdcp->txb_mh		= NULL;
	tdcp->txb_vp		= NULL;
	/* tdcp->txb_pp		= NULL; */

	status = ddi_dma_alloc_handle(sxgep->dip, &sxge_tx_buf_attr,
	    DDI_DMA_DONTWAIT, 0, &tdcp->txb_h);

	if (status != SXGE_SUCCESS) {
		SXGE_DBG((SXGEP,
		    "sxge_tdc_map: txb_h ddi_dma_alloc_handle fail"));
		goto sxge_tdc_map_fail;
	}

	len = tdcp->size * sxge_tdc_buf_sz;
	status = ddi_dma_mem_alloc(tdcp->txb_h, len,
	    &sxge_buf_dma_attr, DDI_DMA_CONSISTENT,
	    DDI_DMA_DONTWAIT, 0,
	    (caddr_t *)&tdcp->txb_vp,
	    &real_len, &tdcp->txb_mh);

	if (status != SXGE_SUCCESS) {
		SXGE_DBG((SXGEP,
		    "sxge_tdc_map: txb_mh ddi_dma_mem_alloc fail"));
		goto sxge_tdc_map_fail;
	}

	status = ddi_dma_addr_bind_handle(tdcp->txb_h, NULL,
	    (caddr_t)tdcp->txb_vp, real_len,
	    DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
	    /* DDI_DMA_WRITE | DDI_DMA_CONSISTENT, */
	    DDI_DMA_DONTWAIT, 0,
	    &tdcp->txb_pp, &count);

	if (status != DDI_DMA_MAPPED) {
		SXGE_DBG((SXGEP,
		    "sxge_tdc_map: txb_pp ddi_dma_addr_bind_handle fail"));
		goto sxge_tdc_map_fail;
	}

	for (i = 0; i < (tdcp->size); i++) {
		sxge_mbp = SXGE_ZALLOC(sizeof (sxge_msg_t));
		if (sxge_mbp == NULL)
			SXGE_DBG0((SXGEP,
			    "sxge_tdc_map: alloc sxge_msg_t failed\n"));
		tdcp->txmb_s[i] = (void *) sxge_mbp;
		status = ddi_dma_alloc_handle(sxgep->dip,
		    &sxge_tx_dma_attr,
		    DDI_DMA_DONTWAIT, 0, &sxge_mbp->buffer_h);
		if (status != SXGE_SUCCESS)
			SXGE_DBG0((SXGEP,
			    "sxge_tdc_map: ddi_dma_alloc_handle fail"));
	}

	/*
	 * Allocate mailbox handles and map them
	 */

	tdcp->mbx_h		= NULL;
	tdcp->mbx_mh		= NULL;
	tdcp->mbx_vp		= NULL;
	/* tdcp->mbx_pp		= NULL; */

	status = ddi_dma_alloc_handle(sxgep->dip, &sxge_desc_attr,
	    DDI_DMA_DONTWAIT, 0, &tdcp->mbx_h);

	if (status != SXGE_SUCCESS) {
		SXGE_DBG((SXGEP,
		    "sxge_tdc_map: mbx_h ddi_dma_alloc_handle fail"));
		goto sxge_tdc_map_fail;
	}

	len = (sizeof (tdc_mbx_desc_t));
	status = ddi_dma_mem_alloc(tdcp->mbx_h, len,
	    &sxge_dev_dma_attr, DDI_DMA_CONSISTENT,
	    DDI_DMA_DONTWAIT, 0,
	    (caddr_t *)&tdcp->mbx_vp,
	    &real_len, &tdcp->mbx_mh);

	if (status != SXGE_SUCCESS) {
		SXGE_DBG((SXGEP,
		    "sxge_tdc_map: mbx_mh ddi_dma_mem_alloc fail"));
		goto sxge_tdc_map_fail;
	}

	status = ddi_dma_addr_bind_handle(tdcp->mbx_h, NULL,
	    (caddr_t)tdcp->mbx_vp, real_len,
	    DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
	    /* DDI_DMA_WRITE | DDI_DMA_CONSISTENT, */
	    DDI_DMA_DONTWAIT, 0,
	    &tdcp->mbx_pp, &count);

	if (status != DDI_DMA_MAPPED) {
		SXGE_DBG((SXGEP,
		    "sxge_tdc_map: mbx_pp ddi_dma_addr_bind_handle fail"));
		goto sxge_tdc_map_fail;
	}

	goto sxge_tdc_map_exit;

sxge_tdc_map_fail:

	/*
	 * Reset descriptor and buf handles
	 */

	if (tdcp->mbx_h)
		(void) ddi_dma_unbind_handle(tdcp->mbx_h);

	if (tdcp->mbx_mh)
		(void) ddi_dma_mem_free(&tdcp->mbx_mh);

	if (tdcp->mbx_h)
		(void) ddi_dma_free_handle(&tdcp->mbx_h);

	tdcp->mbx_h		= (ddi_dma_handle_t)NULL;
	tdcp->mbx_mh		= (ddi_acc_handle_t)NULL;
	tdcp->mbx_vp		= (void *)NULL;
	/* tdcp->mbx_pp		= (ddi_dma_cookie_t)NULL; */

	/*
	 * Reset descriptor and buf handles
	 */

	if (tdcp->txb_h)
		(void) ddi_dma_unbind_handle(tdcp->txb_h);

	if (tdcp->txb_mh)
		(void) ddi_dma_mem_free(&tdcp->txb_mh);

	if (tdcp->txb_h)
		(void) ddi_dma_free_handle(&tdcp->txb_h);

	tdcp->txb_h		= (ddi_dma_handle_t)NULL;
	tdcp->txb_mh		= (ddi_acc_handle_t)NULL;
	tdcp->txb_vp		= (void *)NULL;
	/* tdcp->txb_pp		= (ddi_dma_cookie_t)NULL; */

	if (tdcp->desc_h)
		(void) ddi_dma_unbind_handle(tdcp->desc_h);

	if (tdcp->desc_mh)
		(void) ddi_dma_mem_free(&tdcp->desc_mh);

	if (tdcp->desc_h)
		(void) ddi_dma_free_handle(&tdcp->desc_h);

	tdcp->desc_h		= (ddi_dma_handle_t)NULL;
	tdcp->desc_mh		= (ddi_acc_handle_t)NULL;
	tdcp->desc_vp		= (void *)NULL;
	/* tdcp->desc_pp		= (ddi_dma_cookie_t)NULL; */

	status 	= SXGE_FAILURE;

sxge_tdc_map_exit:

	return (status);
}

int
sxge_tdc_unmap(tdc_state_t *tdcp)
{
#ifdef SXGE_DEBUG
	sxge_t			*sxgep = (sxge_t *)tdcp->sxgep;
#endif
	/*
	 * uint_t			cc = tdcp->tdcn;
	 * uint64_t		pbase = tdcp->pbase;
	 * sxge_pio_handle_t	phdl = tdcp->phdl;
	 */
	/* set but not used */
	sxge_msg_t		*sxge_mbp;
	uint_t 			i;
	/* uint_t			len, real_len; */	/* unused */
	int			status = SXGE_SUCCESS;

	/*
	 * Undo binds and mappings
	 */

	if (tdcp->mbx_h) {
		(void) ddi_dma_unbind_handle(tdcp->mbx_h);
	}
	if (tdcp->mbx_mh)
		(void) ddi_dma_mem_free(&tdcp->mbx_mh);

	if (tdcp->mbx_h)
		(void) ddi_dma_free_handle(&tdcp->mbx_h);
	tdcp->mbx_h		= (ddi_dma_handle_t)NULL;
	tdcp->mbx_mh		= (ddi_acc_handle_t)NULL;
	tdcp->mbx_vp		= (void *)NULL;
	/* tdcp->mbx_pp		= (ddi_dma_cookie_t)NULL; */

	for (i = 0; i < (tdcp->size); i++) {
		sxge_mbp = (sxge_msg_t *)tdcp->txmb_s[i];
		if (sxge_mbp != NULL) {
			if (sxge_mbp->size)
			(void) ddi_dma_unbind_handle(sxge_mbp->buffer_h);
			(void) ddi_dma_free_handle(&sxge_mbp->buffer_h);
			SXGE_FREE(sxge_mbp, sizeof (sxge_msg_t));
		}
		tdcp->txmb_s[i] = NULL;
	}

	if (tdcp->txb_h) {
		(void) ddi_dma_unbind_handle(tdcp->txb_h);
	}
	if (tdcp->txb_mh)
		(void) ddi_dma_mem_free(&tdcp->txb_mh);

	if (tdcp->txb_h)
		(void) ddi_dma_free_handle(&tdcp->txb_h);

	tdcp->txb_h		= (ddi_dma_handle_t)NULL;
	tdcp->txb_mh		= (ddi_acc_handle_t)NULL;
	tdcp->txb_vp		= (void *)NULL;
	/* tdcp->txb_pp		= (ddi_dma_cookie_t)NULL; */
	if (tdcp->desc_h) {
		(void) ddi_dma_unbind_handle(tdcp->desc_h);
	}
	if (tdcp->desc_mh)
		(void) ddi_dma_mem_free(&tdcp->desc_mh);

	if (tdcp->desc_h)
		(void) ddi_dma_free_handle(&tdcp->desc_h);

	/*
	 * Reset descriptor and buf handles
	 */
	tdcp->desc_h		= (ddi_dma_handle_t)NULL;
	tdcp->desc_mh		= (ddi_acc_handle_t)NULL;
	tdcp->desc_vp		= (void *)NULL;
	/* tdcp->desc_pp		= (ddi_dma_cookie_t)NULL; */
	goto sxge_tdc_unmap_exit;

sxge_tdc_unmap_fail:
	status = SXGE_FAILURE;

sxge_tdc_unmap_exit:

	SXGE_DBG((SXGEP, "sxge_tdc_unmap: return"));

	return (status);
}

void
sxge_tdc_hdr(mblk_t *mp, boolean_t fill_len,
		boolean_t l4_cksum, uint_t l4_start, uint_t l4_stuff,
		int pkt_len, uint8_t npads, tdc_pkt_hdr_all_t *pkthdrp)
{
	tdc_pkt_hdr_t		*hdrp;
	mblk_t			*nmp;
	uint64_t		tmp;
	size_t			mblk_len;
	size_t			iph_len;
	size_t			hdrs_size;
	uint8_t			*hdrs_buf;
	uint8_t			*ip_buf;
	uint16_t		eth_type;
	uint8_t			ipproto;
	boolean_t		is_vlan = B_FALSE;
	size_t			eth_hdr_size;

	/*
	 * Caller should zero out the headers first.
	 */
	pkthdrp->resv = 0;
	hdrp = (tdc_pkt_hdr_t *)&pkthdrp->pkthdr;
	hdrp->value = 0;

	if (fill_len) {
		tmp = (uint64_t)pkt_len;
		hdrp->value |= (tmp << TDC_PKT_HEADER_TOT_XFER_LEN_SH);
	}

	tmp = (uint64_t)(npads>>1);
	hdrp->value |= (tmp << TDC_PKT_HEADER_PAD_SH);

	/*
	 * mp is the original data packet (does not include the
	 * Neptune transmit header).
	 */
	nmp = mp;
	mblk_len = MBLKL(nmp);
	ip_buf = NULL;
	hdrs_buf = (uint8_t *)nmp->b_rptr;
	eth_type = ntohs(((ether_header_t *)hdrs_buf)->ether_type);

	if (eth_type < ETHERMTU) {
		tmp = 1ull;
		hdrp->value |= (tmp << TDC_PKT_HEADER_LLC_SH);
		if (*(hdrs_buf + sizeof (struct ether_header))
		    == LLC_SNAP_SAP) {
			eth_type = ntohs(*((uint16_t *)(hdrs_buf +
			    sizeof (struct ether_header) + 6)));
		} else {
			goto fill_tx_header_done;
		}
	} else if (eth_type == VLAN_ETHERTYPE) {
		tmp = 1ull;
		hdrp->value |= (tmp << TDC_PKT_HEADER_VLAN_SH);

		eth_type = ntohs(((struct ether_vlan_header *)
		    hdrs_buf)->ether_type);
		is_vlan = B_TRUE;
	}

	if (!is_vlan) {
		eth_hdr_size = sizeof (struct ether_header);
	} else {
		eth_hdr_size = sizeof (struct ether_vlan_header);
	}

	switch (eth_type) {
	case ETHERTYPE_IP:
		if (mblk_len > eth_hdr_size + sizeof (uint8_t)) {
			ip_buf = nmp->b_rptr + eth_hdr_size;
			mblk_len -= eth_hdr_size;
			iph_len = ((*ip_buf) & 0x0f);
			if (mblk_len > (iph_len + sizeof (uint32_t))) {
				ip_buf = nmp->b_rptr;
				ip_buf += eth_hdr_size;
			} else {
				ip_buf = NULL;
			}

		}
		if (ip_buf == NULL) {
			hdrs_size = 0;
			((ether_header_t *)hdrs_buf)->ether_type = 0;
			while ((nmp) && (hdrs_size < sizeof (hdrs_buf))) {
				mblk_len = (size_t)nmp->b_wptr -
				    (size_t)nmp->b_rptr;
				if (mblk_len >=
				    (sizeof (hdrs_buf) - hdrs_size))
				mblk_len = sizeof (hdrs_buf) - hdrs_size;
				hdrs_size += mblk_len;
				nmp = nmp->b_cont;
			}
			ip_buf = hdrs_buf;
			ip_buf += eth_hdr_size;
			iph_len = ((*ip_buf) & 0x0f);
		}

		ipproto = ip_buf[9];

		tmp = (uint64_t)iph_len;
		hdrp->value |= (tmp << TDC_PKT_HEADER_IHL_SH);
		tmp = (uint64_t)(eth_hdr_size >> 1);
		hdrp->value |= (tmp << TDC_PKT_HEADER_L3START_SH);

		break;

	case ETHERTYPE_IPV6:
		hdrs_size = 0;
		((ether_header_t *)hdrs_buf)->ether_type = 0;
		while ((nmp) && (hdrs_size <
		    sizeof (hdrs_buf))) {
			mblk_len = MBLKL(nmp);
			if (mblk_len >= (sizeof (hdrs_buf) - hdrs_size))
				mblk_len = sizeof (hdrs_buf) - hdrs_size;
			hdrs_size += mblk_len;
			nmp = nmp->b_cont;
		}
		ip_buf = hdrs_buf;
		ip_buf += eth_hdr_size;

		tmp = 1ull;
		hdrp->value |= (tmp << TDC_PKT_HEADER_IP_VER_SH);

		tmp = (eth_hdr_size >> 1);
		hdrp->value |= (tmp << TDC_PKT_HEADER_L3START_SH);

		/* byte 6 is the next header protocol */
		ipproto = ip_buf[6];

		break;

	default:
		goto fill_tx_header_done;
	}

	switch (ipproto) {
	case IPPROTO_TCP:
		if (l4_cksum) {
			tmp = 1ull;
			hdrp->value |= (tmp << TDC_PKT_HEADER_PKT_TYPE_SH);

			tmp = (uint64_t)l4_start>>1;
			tmp = (tmp << TDC_PKT_HEADER_L4START_SH);
			tmp &= TDC_PKT_HEADER_L4START_MK;
			hdrp->value |= tmp;

			tmp = (uint64_t)l4_stuff>>1;
			tmp = (tmp << TDC_PKT_HEADER_L4STUFF_SH);
			tmp &= TDC_PKT_HEADER_L4STUFF_MK;
			hdrp->value |= tmp;
		}

		break;

	case IPPROTO_UDP:
		if (l4_cksum) {
			tmp = 0x2ull;
			hdrp->value |= (tmp << TDC_PKT_HEADER_PKT_TYPE_SH);

			tmp = (uint64_t)l4_start>>1;
			tmp = (tmp << TDC_PKT_HEADER_L4START_SH);
			tmp &= TDC_PKT_HEADER_L4START_MK;
			hdrp->value |= tmp;

			tmp = (uint64_t)l4_stuff>>1;
			tmp = (tmp << TDC_PKT_HEADER_L4STUFF_SH);
			tmp &= TDC_PKT_HEADER_L4STUFF_MK;
			hdrp->value |= tmp;
		}
		break;

	default:
		goto fill_tx_header_done;
	}

fill_tx_header_done:

	tmp = hdrp->value;
	hdrp->value = SXGE_SWAP64(tmp);
}


/*
 * sxge_tdc_send() - Transmit one frame
 */
int
sxge_tdc_send(tdc_state_t *tdcp, mblk_t *mp, uint_t *flags)
{
	/*
	 * unsigned char		dest[] = {0xff, 0xff, 0xff, 0xff, 0xff,
	 * 0xff};
	 */
	/* unused */
	/* unsigned int		type = ETHERTYPE_IP; */	/* Type */
	/* set but not used */
	unsigned int 		size, size_a;			/* size */
	char 			*packet, *packet_a;		/* Packet */
	uint8_t 		*packet_desc;			/* PacketDesc */
	uint8_t 		*packet_desc1;			/* PacketDesc */
	sxge_t			*sxgep = (sxge_t *)tdcp->sxgep;
	/* uint_t			cc = tdcp->tdcn; */
	/* set but not used */
	tdc_pkt_hdr_all_t	*pkthdrp;
	tdc_pkt_hdr_t		*hdrp;
	tdc_desc_t		tmd, tmd1;
	tdc_desc_t		*tmdp = NULL, *tmdp1 = NULL;
	tdc_desc_t		*ntmdp = NULL;
	uint_t			nmpln, nmpln_a, nmpln_d, l;
	uint8_t			*nmpbase, *nmpbase_a;
	uint32_t		tmhp;
	uint32_t		i, ii, nn, wrap, pkt_hdr_len;
	uint32_t		j = 0, pad_len = 0;
	uint32_t		pkt_size = 0, tot_pkt_size = 0;
	uint64_t		tdc_buf_phyaddr;
	uint64_t		tdc_buf_phyaddr1;
	uint64_t		tx_addr, tx_addr0, tx_pad = 0;
	tdc_rng_kick_t		kick;
	tdc_rng_hdl_t		hdl;
	tdc_cs_t		tdc_cs;
	/* tdc_pre_st_t		tdc_pre_st; */ /* unused */
	uint64_t		tmp = 0;
	/* uint8_t			iph_len; */	/* unused */
	uint_t			kk = 0;
	/* uint_t			ick_start, ick_stuff; */
	/* unused */
	mblk_t			*nmp;
	uint_t			nmpcnt = 0;
	uint_t			one_desc, flush = 0;
	uint_t			l2_len = 0, l2_stuff_len = 0;
	unsigned char		*gbp[SXGE_TDC_MAXGTHR+2];
	mblk_t			*gmb[SXGE_TDC_MAXGTHR+2];
	uint_t			gln[SXGE_TDC_MAXGTHR+2];
	uint_t			tdc_prsr_en = tdcp->tdc_prsr_en;
	uint_t			pkt_cnt = 0;
	uint_t			cksen = (*flags & SXGE_TX_CKENB)?1:0;
	sxge_msg_t		*sxge_mbp = NULL;
	uint64_t		pbase = tdcp->pbase;
	sxge_pio_handle_t	phdl = tdcp->phdl;
	int			status = SXGE_FAILURE;

	if (!tdcp->enable) {
		sxge_freemsg(mp);
		return (-1);
	}

	tdc_cs.value = SXGE_GET64(phdl, TDC_CS_REG);
	tdcp->tdc_cs.value = tdc_cs.value;
	if (tdc_cs.value & sxge_tdc_err_msk) {
		(void) sxge_tdc_errs(tdcp);
		return (-1);
	}

	if (sxge_tdc_flush_en) {
		flush = (mp == NULL)?1:0;
		if (flush) {
			ntmdp = tdcp->tnextp;
			nn = ntmdp - tdcp->tmdp;
			goto sxge_tdc_send_kick;
		}
	}
tdc_send_start:
	nmpcnt = 0;
	pkt_size = 0;
	size = MBLKL(mp);
	packet = (char *)mp->b_rptr;
	packet_a = (char *)((uint64_t)(packet) & ~0xfffULL);
	size_a =  size + (packet - packet_a);

	one_desc = ((mp->b_cont == NULL) && (size_a <= TDC_DESC_MAXLN))?1:0;
	if (!tdc_prsr_en) {
		if ((mp->b_rptr - mp->b_datap->db_base) < TDC_HDR_SIZE_DEFAULT)
			one_desc = 0;
		else one_desc = (mp->b_cont == NULL)?1:0;
	}

	if (!sxge_txmb_reuse && !one_desc) {

		if (!pullupmsg(mp, -1)) {
			sxge_freemsg(mp);
			return (0);
		}
		size = MBLKL(mp);
		packet = (char *)mp->b_rptr;

		packet_a = (char *)((uint64_t)(packet) & ~0xfffULL);
		size_a =  size + (packet - packet_a);
		one_desc =
		    ((mp->b_cont == NULL) && (size_a <= TDC_DESC_MAXLN))?1:0;

		SXGE_DBG((SXGEP, "sxge_tdc_send: msgpullup %llx \n", mp));
	}

	if (one_desc) {

#ifdef SXGE_DEBUG
		ASSERT(size != 0);
#endif
		if (size == 0) {
			SXGE_DBG0((SXGEP,
			    "%d: sxge_tdc_send: _zero_ len mp %p from stack",
			    sxgep->instance, mp));
			sxge_freemsg(mp);
			return (0);
		}

		if (size >= sxge_tdc_maxframe) {
			SXGE_DBG0((SXGEP,
			    "sxge_tdc_send: pkt len too large %dB!\n", size));
			sxge_freemsg(mp);
			return (0);
		}
		pkt_size = size;
	} else {

		for (nmp = mp; nmp; nmp = nmp->b_cont) {

		nmpln = MBLKL(nmp);

#ifdef SXGE_DEBUG
		ASSERT(nmpln != 0);
#endif
		if (nmpln == 0) {
			SXGE_DBG0((SXGEP,
			    "%d: sxge_tdc_send: _zero_ len mp %p %p from stack",
			    sxgep->instance, mp, nmp));
			sxge_freemsg(mp);
			return (0);
		}

		nmpbase = nmp->b_rptr;
		nmpbase_a = (uint8_t *)((uint64_t)(nmpbase) & ~0xfffULL);
		nmpln_d = (nmpbase - nmpbase_a);
		nmpln_a = nmpln + nmpln_d;
		pkt_size += nmpln;
		if (nmpln_a <= TDC_DESC_MAXLN) {
			gbp[nmpcnt] = nmpbase;
			gmb[nmpcnt] = nmp;
			gln[nmpcnt] = nmpln;
			nmpcnt++;

		} else {
			gbp[nmpcnt] = nmpbase;
			gmb[nmpcnt] = nmp;
			gln[nmpcnt] = TDC_DESC_MAXLN - nmpln_d;
			nmpcnt++;
			nmpln -= (TDC_DESC_MAXLN - nmpln_d);
			nmpbase += (TDC_DESC_MAXLN - nmpln_d);

			while (nmpln > TDC_DESC_MAXLN) {
				gbp[nmpcnt] = nmpbase;
				gmb[nmpcnt] = NULL;
				gln[nmpcnt] = TDC_DESC_MAXLN;
				nmpcnt++;
				nmpln -= TDC_DESC_MAXLN;
				nmpbase += TDC_DESC_MAXLN;
				if (nmpcnt >= SXGE_TDC_MAXGTHR) {
				SXGE_DBG((SXGEP,
				    "sxge_tdc_send: too many gathers!!\n"));

				if (!pullupmsg(mp, -1)) {
					sxge_freemsg(mp);
					return (0);
				}
				goto tdc_send_start;
				}
			}
			gbp[nmpcnt] = nmpbase;
			gmb[nmpcnt] = NULL;
			gln[nmpcnt] = nmpln;
			nmpcnt++;
		}
		if (nmpcnt >= SXGE_TDC_MAXGTHR) {
			SXGE_DBG((SXGEP,
			    "sxge_tdc_send: too many gathers!!\n"));
			if (!pullupmsg(mp, -1)) {
				sxge_freemsg(mp);
				return (0);
			}
			goto tdc_send_start;
		}

		} /* for */

		if (pkt_size >= sxge_tdc_maxframe) {
		SXGE_DBG((SXGEP, "sxge_tdc_send: pkt len too large %dB!\n",
		    pkt_size));
		sxge_freemsg(mp);
		return (0);
		}

	}

	tmdp = tdcp->tnextp;

	if (one_desc | tdc_prsr_en)
		tmdp1 = tmdp;
	else
		tmdp1 = NEXTTMD(tdcp, tmdp);

	ntmdp = NEXTTMD(tdcp, tmdp1);
	i = tmdp - tdcp->tmdp;
	ii = tmdp1 - tdcp->tmdp;
	nn = ntmdp - tdcp->tmdp;
	tdcp->tdc_idx = i;
	tmhp = tdcp->tmhp;

	/* check head */
	hdl.value = SXGE_GET64(phdl, TDC_RNG_HDL_REG);
	tdcp->tdc_hdl.value = hdl.value;
	tmhp = hdl.bits.ldw.head;
	for (l = 0; l < (TDC_NR_DESC_GAP); l++) {
		if ((nn+l)%(tdcp->size) == tmhp) {
			tdcp->ofulls++;
			return (-1);
		}
	}

	tdcp->tmhp = tmhp;

	/* reclaim and bind */
	if (one_desc) {
		sxge_mbp = (sxge_msg_t *)tdcp->txmb_s[ii];

		if (sxge_txmb_reuse) {

			if (sxge_mbp->size) {
			(void) sxge_buf_unbind(sxgep, sxge_mbp->buffer_h);
			sxge_mbp->mp = NULL;
			sxge_mbp->buffer_vp = NULL;
			sxge_mbp->size = 0;
			}
		}

		if (tdcp->txmb[ii] != NULL)
			SXGE_FREEB(tdcp->txmb[ii]);

		tdcp->txbp[ii] = (uint64_t)packet;
		tdcp->txmb[ii] = mp;

		if ((sxge_txmb_reuse) && (pkt_size > sxge_tdc_tiny_sz)) {

		sxge_mbp->buffer_vp = (void *) tdcp->txbp[ii];
		sxge_mbp->mp = tdcp->txmb[ii];
		sxge_mbp->size = pkt_size;

		status = sxge_buf_bind(sxgep, 0, sxge_mbp->buffer_h,
		    sxge_mbp->buffer_vp, &sxge_mbp->buffer_pp,
		    &sxge_mbp->size, &sxge_mbp->buffer_cc);

		if (status != SXGE_SUCCESS) {
			SXGE_DBG0((SXGEP,
			    "sxge_tdc_send: sxge_buf_bind (one_desc) failed"));
			SXGE_DBG0((SXGEP, "buf_bind vp %p size %d",
			    sxge_mbp->buffer_vp, sxge_mbp->size));

			sxge_mbp->mp = NULL;
			sxge_mbp->buffer_vp = NULL;
			sxge_mbp->size = 0;
		} else
			tdcp->txbp[ii] =
			    (uint64_t)(sxge_mbp->buffer_pp.dmac_laddress);

		} /* sxge_txmb_reuse && (pkt_size > sxge_tdc_tiny_sz) */

	/* !one_desc */
	} else {

		uint_t jj;

		if (!tdc_prsr_en) {
			if (tdcp->txmb[i])
				SXGE_FREEB(tdcp->txmb[i]);
			tdcp->txmb[i] = NULL;
		}

		for (kk = 0; kk < nmpcnt; kk++) {

		jj = (ii + kk) % (tdcp->size);

		sxge_mbp = (sxge_msg_t *)tdcp->txmb_s[jj];

		if (sxge_txmb_reuse) {

			if (sxge_mbp->size) {
			(void) sxge_buf_unbind(sxgep, sxge_mbp->buffer_h);
			sxge_mbp->mp = NULL;
			sxge_mbp->buffer_vp = NULL;
			sxge_mbp->size = 0;
			}
		}

		if (tdcp->txmb[jj] != NULL)
			SXGE_FREEB(tdcp->txmb[jj]);
		tdcp->txbp[jj] = (uint64_t)gbp[kk];
		tdcp->txmb[jj] = gmb[kk];

		if (sxge_txmb_reuse) {

			sxge_mbp->buffer_vp = (void *)tdcp->txbp[jj];
			sxge_mbp->mp = tdcp->txmb[jj];
			sxge_mbp->size = (size_t)gln[kk];

			status = sxge_buf_bind(sxgep, 0, sxge_mbp->buffer_h,
			    sxge_mbp->buffer_vp, &sxge_mbp->buffer_pp,
			    &sxge_mbp->size, &sxge_mbp->buffer_cc);

			if (status != SXGE_SUCCESS) {
				SXGE_DBG0((SXGEP,
				    "sxge_tdc_send: sxge_buf_bind failed"));
				sxge_mbp->mp = NULL;
				sxge_mbp->buffer_vp = NULL;
				sxge_mbp->size = 0;
			} else {
				tdcp->txbp[jj] =
				    (uint64_t)sxge_mbp->buffer_pp.dmac_laddress;
				gbp[kk] = (unsigned char *)tdcp->txbp[jj];
			}
		}

		} /* for (kk) */
	}

	pkt_hdr_len = TDC_HDR_SIZE_DEFAULT;

	if (one_desc) {
		tx_addr = (uint64_t)packet;
		if (!tdc_prsr_en) {
			tx_addr0 = tx_addr & (~0xFULL);
			tx_pad = tx_addr - tx_addr0;
			if (tx_pad == RDC_PAD_SIZE) pad_len = (uint32_t)tx_pad;
			else pad_len = (tx_pad>>1)<<1;
		}

		tot_pkt_size = (pkt_size & 0x1FFF); /* size */

		packet_desc = (uint8_t *)packet;
		if (!tdc_prsr_en)
		packet_desc = (uint8_t *)packet - (pkt_hdr_len+pad_len);

	} else {
		pad_len = 0x0;
		tot_pkt_size = pkt_size;

		packet_desc = (uint8_t *)packet;
		if (!tdc_prsr_en)
		packet_desc = (uint8_t *)(&tdcp->txb[i * sxge_tdc_buf_sz])
		    + SXGE_TX_CTL_OFFSET;
	}

	l2_len = tot_pkt_size;

	/* bcopy */

	if (one_desc) {

	if ((tot_pkt_size <= sxge_tdc_tiny_sz) || (sxge_mbp->size == 0)) {
		packet = (char *)(tdcp->txb_vp) + (ii * sxge_tdc_buf_sz);
		packet_desc1 = (uint8_t *)(tdcp->txb_pp.dmac_laddress);
		packet_desc1 += (ii * sxge_tdc_buf_sz);
		bcopy(mp->b_rptr, packet, l2_len);
	} else {
		packet_desc1 = (uint8_t *)packet;
		if (sxge_txmb_reuse)
		packet_desc1 = (uint8_t *)tdcp->txbp[ii];
	}

	/* !one_desc */
	} else {
		packet_desc1 = (uint8_t *)packet;
		if (sxge_txmb_reuse) {

			uint_t jj;
			packet_desc1 = (uint8_t *)tdcp->txbp[ii];
			for (kk = 0; kk < nmpcnt; kk++) {
				jj = (ii + kk) % (tdcp->size);

				sxge_mbp = (sxge_msg_t *)tdcp->txmb_s[jj];
				packet = (char *)(tdcp->txb_vp) +
				    (jj * sxge_tdc_buf_sz);
				packet_desc1 =
				    (uint8_t *)(tdcp->txb_pp.dmac_laddress);
				packet_desc1 += (jj * sxge_tdc_buf_sz);
				if (sxge_mbp->size == 0) {
					bcopy(gbp[kk], packet, gln[kk]);
					gbp[kk] = (unsigned char *)packet_desc1;
					tdcp->txbp[jj] = (uint64_t)gbp[kk];
				}
			}
		} else {
			uint_t jj;
			for (kk = 0; kk < nmpcnt; kk++) {
			jj = (ii + kk) % (tdcp->size);
			packet = (char *)(tdcp->txb_vp) +
			    (jj * sxge_tdc_buf_sz);
			packet_desc1 = (uint8_t *)(tdcp->txb_pp.dmac_laddress);
			packet_desc1 += (jj * sxge_tdc_buf_sz);
			bcopy(gbp[kk], packet, gln[kk]);
			gbp[kk] = (unsigned char *)packet_desc1;
			tdcp->txbp[jj] = (uint64_t)gbp[kk];
			}
		}
	}

	/*
	 * Stuff pkt_size to the ethernet min len on last desc
	 */
	if (sxge_tdc_stuff_en) {

		while (tot_pkt_size < SXGE_TX_MIN_LEN) {
		tot_pkt_size++;
		/* packet_desc1[pkt_hdr_len + pad_len + pkt_size] = 0x0; */
		packet[pkt_hdr_len + pad_len + tot_pkt_size] = 0x0;
		}
	}

	if (tot_pkt_size < SXGE_TX_MIN_LEN) {
		l2_stuff_len = SXGE_TX_MIN_LEN - tot_pkt_size;
		l2_len = tot_pkt_size = SXGE_TX_MIN_LEN;
	}

	if (!tdc_prsr_en) {
		/* hdr */
		pkthdrp = (tdc_pkt_hdr_all_t *)packet_desc;
		hdrp = (tdc_pkt_hdr_t *)&pkthdrp->pkthdr;

		if (one_desc) {
		tmp |= ((uint64_t)(tot_pkt_size+pad_len)<<
		    TDC_PKT_HEADER_TOT_XFER_LEN_SH);
		tmp |= ((uint64_t)(pad_len>>1)<< TDC_PKT_HEADER_PAD_SH);
		} else
		tmp |= ((uint64_t)tot_pkt_size<<TDC_PKT_HEADER_TOT_XFER_LEN_SH);

		tmp = SXGE_SWAP64(tmp);

		SXGE_MPUT64(phdl, (uint64_t)&hdrp->value, tmp);
	}

	tdc_buf_phyaddr = (uint64_t)(packet_desc);
	tdc_buf_phyaddr1 = (uint64_t)(packet_desc);
	if (!tdc_prsr_en)
		tdc_buf_phyaddr1 = (uint64_t)(packet_desc1);

	if (one_desc)
		tdc_buf_phyaddr = (uint64_t)(packet_desc1);

	if (one_desc) {

		/* tx_desc.0 */
		tmd.value = 0x0;
		tmd.bits.hdw.sop = 1;
		tmd.bits.hdw.mark = 0;
		tmd.bits.hdw.numptr = 1;
		tmd.bits.hdw.cksen = cksen;
		tmd.bits.hdw.trlen = tot_pkt_size;
		if (!tdc_prsr_en) {
		tmd.bits.hdw.trlen = pkt_hdr_len + pad_len + tot_pkt_size;
		}
		tmd.bits.hdw.saddr = (tdc_buf_phyaddr >> 32) & 0xFFF;
		tmd.bits.ldw.saddr = tdc_buf_phyaddr & 0xffffffff;

		SXGE_MPUT64(phdl, (uint64_t)&tmdp->value, tmd.value);
	} else {

		if (!tdc_prsr_en) {

			/* tx_desc.0 */
			tmd.value = 0x0;
			tmd.bits.hdw.sop = 1;
			tmd.bits.hdw.mark = 0;
			tmd.bits.hdw.numptr = nmpcnt + 1;
			tmd.bits.hdw.trlen = pkt_hdr_len;
			tmd.bits.hdw.saddr = (tdc_buf_phyaddr >> 32) & 0xFFF;
			tmd.bits.ldw.saddr = tdc_buf_phyaddr & 0xffffffff;
			SXGE_MPUT64(phdl, (uint64_t)&tmdp->value, tmd.value);
		}

		/* tx_desc.n */
		for (kk = 0; kk < nmpcnt; kk++) {

			tdc_buf_phyaddr1 = (uint64_t)(gbp[kk]);

			tmd1.value = 0x0;
			tmd1.bits.hdw.sop = 0;
			tmd1.bits.hdw.mark = 0;
			tmd1.bits.hdw.numptr = 0;
			tmd1.bits.hdw.trlen = gln[kk];
			if (kk == (nmpcnt-1))
			tmd1.bits.hdw.trlen = gln[kk] + l2_stuff_len;
			if (tdc_prsr_en && (kk == 0)) {
			tmd1.bits.hdw.cksen = cksen;
			tmd1.bits.hdw.sop = 1;
			tmd1.bits.hdw.numptr = nmpcnt;
			}
			tmd1.bits.hdw.saddr = (tdc_buf_phyaddr1 >> 32) & 0xFFF;
			tmd1.bits.ldw.saddr = tdc_buf_phyaddr1 & 0xffffffff;
			SXGE_MPUT64(phdl, (uint64_t)&tmdp1->value, tmd1.value);
			tmdp1 = NEXTTMD(tdcp, tmdp1);
		}
		ntmdp = tmdp1;
		nn = ntmdp - tdcp->tmdp;
	}

	/* next */
	tdcp->tnextp = ntmdp;

	/* wrap */
	wrap = tdcp->wrap;
	if ((nn == 0) || (nn < i)) {
		if (!wrap)	tdcp->wrap = 1;
		else	tdcp->wrap = 0;
	}

	/* pkt_cnt dbg */
	if (sxge_tdc_cs_dbg) {
	tdc_cs.value = SXGE_GET64(phdl, TDC_CS_REG);
	pkt_cnt = tdc_cs.bits.hdw.pkt_cnt;
	}

sxge_tdc_send_kick:
	/* kick */
	kick.value = 0;
	kick.bits.ldw.wrap = tdcp->wrap;
	kick.bits.ldw.tail = nn;
	tdcp->tdc_kick.value = kick.value;
	if (!sxge_tdc_kicks || flush) {
		SXGE_PUT64(phdl, TDC_RNG_KICK_REG, kick.value);
		/* tdcp->opackets += 1; */
	} else if (!(nn % sxge_tdc_kicks)) {
		SXGE_PUT64(phdl, TDC_RNG_KICK_REG, kick.value);
		/* tdcp->opackets += sxge_tdc_kicks; */
	}

	if (sxge_tdc_cs_dbg) {
		if ((!sxge_tdc_kicks || flush) ||
		    ((sxge_tdc_kicks != 0) && !(nn % sxge_tdc_kicks)))
		while ((pkt_cnt+1) != tdc_cs.bits.hdw.pkt_cnt && j++ < 10) {
		SXGE_UDELAY(sxgep, 100, sxge_delay_busy);
		tdc_cs.value = SXGE_GET64(phdl, TDC_CS_REG);
		/* tdc_pre_st.value = SXGE_GET64(phdl, TDC_PRE_ST_REG); */
		}
		pkt_cnt = tdc_cs.bits.hdw.pkt_cnt;
		hdl.value = SXGE_GET64(phdl, TDC_RNG_HDL_REG);
	}

	atomic_add_32(&tdcp->opackets, 1);
	atomic_add_32(&tdcp->obytes, l2_len + 4);

	return (0);
}

void
sxge_tdc_dump(tdc_state_t *tdcp)
{
	sxge_t			*sxgep = (sxge_t *)tdcp->sxgep;
	/* uint_t			cc = tdcp->tdcn; */
	/* set but not used */
	uint64_t		pbase = tdcp->pbase;
	sxge_pio_handle_t	phdl = tdcp->phdl;

	SXGE_DBG0((SXGEP, "sxge_tdc_dump: vni = %d tdc = %d \n",
	    tdcp->vnin, tdcp->tdcn));

	SXGE_DBG0((SXGEP, "\ttdc_rng_cfg\t\t[ %16llx ] \t= %llx\n",
	    TDC_RNG_CFG_REG, SXGE_GET64(phdl, TDC_RNG_CFG_REG)));

	SXGE_DBG0((SXGEP, "\ttdc_pg_hdl\t\t[ %16llx ] \t= %llx\n",
	    TDC_PG_HDL_REG, SXGE_GET64(phdl, TDC_PG_HDL_REG)));

	SXGE_DBG0((SXGEP, "\ttdc_rng_hdl\t\t[ %16llx ] \t= %llx\n",
	    TDC_RNG_HDL_REG, SXGE_GET64(phdl, TDC_RNG_HDL_REG)));

	SXGE_DBG0((SXGEP, "\ttdc_rng_kick \t\t[ %16llx ] \t= %llx\n",
	    TDC_RNG_KICK_REG, SXGE_GET64(phdl, TDC_RNG_KICK_REG)));

	SXGE_DBG0((SXGEP, "\ttdc_dma_ent_msk\t\t[ %16llx ] \t= %llx\n",
	    TDC_DMA_ENT_MSK_REG, SXGE_GET64(phdl, TDC_DMA_ENT_MSK_REG)));

	SXGE_DBG0((SXGEP, "\ttdc_cs \t\t[ %16llx ] \t= %llx\n",
	    TDC_CS_REG, SXGE_GET64(phdl, TDC_CS_REG)));

	SXGE_DBG0((SXGEP, "\ttdc_mbh\t\t[ %16llx ] \t= %llx\n",
	    TDC_MBH_REG, SXGE_GET64(phdl, TDC_MBH_REG)));

	SXGE_DBG0((SXGEP, "\ttdc_mbl\t\t[ %16llx ] \t= %llx\n",
	    TDC_MBL_REG, SXGE_GET64(phdl, TDC_MBL_REG)));

	SXGE_DBG0((SXGEP, "\ttdc_pre_st\t\t[ %16llx ] \t= %llx\n",
	    TDC_PRE_ST_REG, SXGE_GET64(phdl, TDC_PRE_ST_REG)));

	SXGE_DBG0((SXGEP, "\ttdc_err_logh \t\t[ %16llx ] \t= %llx\n",
	    TDC_ERR_LOGH_REG, SXGE_GET64(phdl, TDC_ERR_LOGH_REG)));

	SXGE_DBG0((SXGEP, "\ttdc_err_logl \t\t[ %16llx ] \t= %llx\n",
	    TDC_ERR_LOGL_REG, SXGE_GET64(phdl, TDC_ERR_LOGL_REG)));

	SXGE_DBG0((SXGEP, "\ttdc_intr_dbg \t\t[ %16llx ] \t= %llx\n",
	    TDC_INTR_DBG_REG, SXGE_GET64(phdl, TDC_INTR_DBG_REG)));

	SXGE_DBG0((SXGEP, "\ttdc_cs_dbg\t\t[ %16llx ] \t= %llx\n",
	    TDC_CS_DBG_REG, SXGE_GET64(phdl, TDC_CS_DBG_REG)));

	SXGE_DBG0((SXGEP, "\ttdc_max_burst\t\t[ %16llx ] \t= %llx\n",
	    TXC_DMA_MAX, SXGE_GET64(phdl, TXC_DMA_MAX)));

	SXGE_DBG0((SXGEP, "\ttxvmac_frm_cnt \t\t[ %16llx ] \t= %llx\n",
	    (TXVMAC_BASE + 0x20), SXGE_GET64(phdl, (TXVMAC_BASE + 0x20))));

	SXGE_DBG0((SXGEP, "\ttxvmac_byt_cnt \t\t[ %16llx ] \t= %llx\n",
	    (TXVMAC_BASE + 0x28), SXGE_GET64(phdl, (TXVMAC_BASE + 0x28))));

	if (sxge_dbg_pkts)
		SXGE_DBG0((SXGEP, "sxge_tdc_dump:%d: opackets %d obytes %d",
		    tdcp->tdcn, tdcp->opackets, tdcp->obytes));
	else
		SXGE_DBG0((SXGEP, "sxge_tdc_dump:%d: opackets %d obytes %d",
		    tdcp->tdcn, tdcp->opackets, tdcp->obytes));
}

int
sxge_tdc_init(tdc_state_t *tdcp)
{
	uint_t			i;
	uint_t			delay = 0;
	uint64_t		ring_phyaddr;
	/* tdc_desc_t		*tmp_tmdp; */	/* set but not used */
	tdc_cs_t		tdc_cs;
	tdc_rng_cfg_t		rng_cfg;
	txc_dma_max_burst_t	dma_max_burst;
	tdc_mbh_t		tdc_mbh;
	tdc_mbl_t		tdc_mbl;
	/* tdc_pre_st_t		tdc_pre_st; */
	sxge_t			*sxgep = (sxge_t *)tdcp->sxgep;
	uint_t			cc = tdcp->tdcn;
	uint64_t		pbase = tdcp->pbase = TDC_PBASE(tdcp);
	sxge_pio_handle_t	phdl = tdcp->phdl = sxgep->pio_hdl;
	int			status = SXGE_SUCCESS;

	tdcp->enable = 0;
	tdcp->size = sxge_tdc_nr;

	SXGE_DBG((SXGEP, "sxge_tdc_init: inside... tdc = %d\n", cc));

	/*
	 * Allocate tdc ring, mbx
	 */
	tdcp->txbp = SXGE_ZALLOC((sizeof (uint64_t)) * (tdcp->size));
	tdcp->txmb = SXGE_ZALLOC((sizeof (void *)) * (tdcp->size));
	tdcp->txmb_s = SXGE_ZALLOC((sizeof (void *)) * (tdcp->size));
	/*
	 * map ring, dma bind tmdp
	 */

	if (sxge_tdc_map(tdcp)) {
		SXGE_DBG((SXGEP, "sxge_tdc_init: sxge_tdc_map fail\n"));
		goto sxge_tdc_init_fail;
	}

	tdcp->ring = tdcp->desc_vp;
	tdcp->txb = tdcp->txb_vp;
	tdcp->mbxp = tdcp->mbx_vp;

	/* Setup tdc ring */
	ring_phyaddr = (uint64_t)(&tdcp->ring[0]);
	tdcp->tmdp = (tdc_desc_t *)(ring_phyaddr);
	tdcp->tnextp = tdcp->tmdp;
	/* tmp_tmdp = tdcp->tmdp; */	/* set but not used */
	tdcp->tmdlp = &((tdcp->tmdp)[(tdcp->size)]);
	tdcp->wrap = 0;
	tdcp->tmhp = 0;

	/* Setup tdc bufs */
	ring_phyaddr = (uint64_t)(tdcp->txb);
	tdcp->txb_a = (uint8_t *)ring_phyaddr;

	/* Setup mbx */
	ring_phyaddr = (uint64_t)(tdcp->mbxp);
	tdcp->mbxp_a = (tdc_mbx_desc_t *)ring_phyaddr;
	/* Fill tdc ring */
	for (i = 0; i < (tdcp->size); i++) {
		/* tdcp->ring[i] = 0x0ULL; */
		tdcp->tmdp[i].value = 0x0ULL;
		tdcp->txbp[i] = 0x0ULL;
		tdcp->txmb[i] = 0x0ULL;
	}

	/* Reset Kick regsister */
	SXGE_PUT64(phdl, TDC_RNG_KICK_REG, 0x0ULL);

	/* Reset Hdl register */
	SXGE_PUT64(phdl, TDC_RNG_HDL_REG, 0x0ULL);

	/* Reset TX DMA channel */
	tdc_cs.value = SXGE_GET64(phdl, TDC_CS_REG);
	tdc_cs.value = 0;
	tdc_cs.bits.ldw.rst = 1;
	SXGE_PUT64(phdl, TDC_CS_REG, tdc_cs.value);

	/* Wait till reset is done */
	do {
		SXGE_UDELAY(sxgep, 5, sxge_delay_busy);
		tdc_cs.value = SXGE_GET64(phdl, TDC_CS_REG);
		if (!tdc_cs.bits.ldw.rst) {
			break;
		}
		delay++;
	} while (delay < TDC_RESET_LOOP);

	if (delay == TDC_RESET_LOOP) {
		SXGE_DBG0((SXGEP, "sxge_tdc_init: TDC_RESET fail\n"));
		SXGE_FM_REPORT_ERROR(sxgep, sxgep->instance, (int)cc,
		    SXGE_FM_EREPORT_TDC_RESET_FAIL);
		goto sxge_tdc_init_fail;
	}

	/* Initialize the page handle */
	SXGE_PUT64(phdl, TDC_PG_HDL_REG, 0ULL); /* sxgep->page_hdl.value */

	/* Initialize the DMA channel, and enable DMA channel */
	rng_cfg.value = 0ULL;
	rng_cfg.value = (uint64_t)(tdcp->desc_pp.dmac_laddress) &
	    TDC_RNG_CFG_ADDR_MK;
	rng_cfg.value |= (uint64_t)((tdcp->size) / 8) << TDC_RNG_CFG_LEN_SH;
	SXGE_PUT64(phdl, TDC_RNG_CFG_REG, rng_cfg.value);
	rng_cfg.value = SXGE_GET64(phdl, TDC_RNG_CFG_REG);

	/* Initialize Mailbox */
	tdc_mbl.value = 0;
	tdc_mbl.value = ((uint64_t)tdcp->mbxp_a) & TDC_MBL_MK;
	SXGE_PUT64(phdl, TDC_MBL_REG, tdc_mbl.value);
	tdc_mbl.value = SXGE_GET64(phdl, TDC_MBL_REG);

	tdc_mbh.value = 0;
	tdc_mbh.value = (((uint64_t)tdcp->mbxp_a)
	    >>TDC_MBH_ADDR_SH) & TDC_MBH_MK;
	SXGE_PUT64(phdl, TDC_MBH_REG, tdc_mbh.value);
	tdc_mbh.value = SXGE_GET64(phdl, TDC_MBH_REG);

	/* Initialize TXC_DMA_MAX */
	dma_max_burst.value = 0;
	dma_max_burst.bits.dma_max_burst = TXC_DMA_MAX_BURST_DEFAULT;
	SXGE_PUT64(phdl, TXC_DMA_MAX, dma_max_burst.value);
	dma_max_burst.value = SXGE_GET64(phdl, TXC_DMA_MAX);

	/* Initialize tdc_prsr_en */
	if (dma_max_burst.value & 0x80000000) {
		tdcp->tdc_prsr_en = sxgep->tdc_prsr_en = 1;
		SXGE_DBG((SXGEP, "sxge_tdc_init: tdc=%d tdc_prsr_en=1\n", cc));
	} else {
		tdcp->tdc_prsr_en = sxgep->tdc_prsr_en = 0;
		SXGE_DBG((SXGEP, "sxge_tdc_init: tdc=%d tdc_prsr_en=0\n", cc));
	}

	/* Dump prefetch reg */
	/* tdc_pre_st.value = SXGE_GET64(phdl, TDC_PRE_ST_REG); */

	/* Start the DMA engine */
	tdc_cs.value = SXGE_GET64(phdl, TDC_CS_REG);
	tdc_cs.bits.ldw.rst_state = 0;
	if (sxge_tdc_mbx)
	tdc_cs.bits.ldw.mb = 1;
	SXGE_PUT64(phdl, TDC_CS_REG, tdc_cs.value);
	tdc_cs.value = SXGE_GET64(phdl, TDC_CS_REG);
	tdcp->enable = 1;

	goto sxge_tdc_init_exit;

sxge_tdc_init_fail:
	status = SXGE_FAILURE;

sxge_tdc_init_exit:

	return (status);
}

int
sxge_tdc_mask(tdc_state_t *tdcp, boolean_t mask)
{
	/*
	 * sxge_t			*sxgep = (sxge_t *)tdcp->sxgep;
	 * uint_t			cc = tdcp->tdcn;
	 */
	/* set but not used */
	uint64_t		pbase = tdcp->pbase;
	sxge_pio_handle_t	phdl = tdcp->phdl;
	/* int			status = SXGE_SUCCESS; */
	/* set but not used */
	tdc_dma_ent_msk_t	tdc_ent_msk;

	/* Setup tdc_ent_msk_reg */
	if (mask == B_TRUE) {

	tdc_ent_msk.value = -1;
	SXGE_PUT64(phdl, TDC_DMA_ENT_MSK_REG, tdc_ent_msk.value);
	tdcp->intr_msk = 1;

	} else {

	tdc_ent_msk.value = 0;
	tdc_ent_msk.bits.ldw.mk = 1;
	SXGE_PUT64(phdl, TDC_DMA_ENT_MSK_REG, tdc_ent_msk.value);
	tdcp->intr_msk = 0;

	}

	return (0);
}

int
sxge_tdc_fini(tdc_state_t *tdcp)
{
	uint_t			i = 0, delay = 0;
	tdc_cs_t		tdc_cs;
	/*
	 * txc_dma_max_burst_t	dma_max_burst;
	 * tdc_mbh_t		tdc_mbh;
	 * tdc_mbl_t		tdc_mbl;
	 * sxge_msg_t		*sxge_mbp;
	 */
	/* unused */
	sxge_t			*sxgep = (sxge_t *)tdcp->sxgep;
	uint_t			cc = tdcp->tdcn;
	uint64_t		pbase = tdcp->pbase;
	sxge_pio_handle_t	phdl = tdcp->phdl;
	int			status = SXGE_SUCCESS;

	tdcp->enable = 0;

	/* Reset Kick regsister */
	SXGE_PUT64(phdl, TDC_RNG_KICK_REG, 0x0ULL);

	/* Reset Hdl register */
	SXGE_PUT64(phdl, TDC_RNG_HDL_REG, 0x0ULL);

	/* Reset Cfg register */
	SXGE_PUT64(phdl, TDC_RNG_CFG_REG, 0x0ULL);

	/* Reset page handle */
	SXGE_PUT64(phdl, TDC_PG_HDL_REG, 0x0ULL); /* tdcp->page_hdl.value */

	/* Reset Mailbox */
	SXGE_PUT64(phdl, TDC_MBL_REG, 0x0ULL);
	SXGE_PUT64(phdl, TDC_MBH_REG, 0x0ULL);

	/* Reset Max burst */
	SXGE_PUT64(phdl, TXC_DMA_MAX, 0x0ULL);

	/* Reset ent mask */
	SXGE_PUT64(phdl, TDC_DMA_ENT_MSK_REG, -1);

	/* Reset TX DMA channel */
	tdc_cs.value = 0;
	tdc_cs.bits.ldw.rst = 1;
	SXGE_PUT64(phdl, TDC_CS_REG, tdc_cs.value);

	/* Wait till reset is done */
	do {
		SXGE_UDELAY(sxgep, 5, sxge_delay_busy);
		tdc_cs.value = SXGE_GET64(phdl, TDC_CS_REG);
		if (!tdc_cs.bits.ldw.rst) {
			break;
		}
		delay++;
	} while (delay < TDC_RESET_LOOP);

	if (delay == TDC_RESET_LOOP) {
		SXGE_DBG((SXGEP, "sxge_tdc_fini: TDC_RESET fail\n"));
		SXGE_FM_REPORT_ERROR(sxgep, sxgep->instance, (int)cc,
		    SXGE_FM_EREPORT_TDC_RESET_FAIL);
		status = SXGE_FAILURE;
	}

	for (i = 0; i < (tdcp->size); i++) {
		if (tdcp->txmb[i] != NULL) SXGE_FREEB(tdcp->txmb[i]);
		tdcp->txmb[i] = NULL;
		/* tdcp->ring[i] = 0x0ULL; */
		tdcp->tmdp[i].value = NULL;
		tdcp->txbp[i] = NULL;
	}

	(void) sxge_tdc_unmap(tdcp);
	SXGE_FREE(tdcp->txbp, (sizeof (uint64_t)) * (tdcp->size));
	SXGE_FREE(tdcp->txmb, (sizeof (void *)) * (tdcp->size));
	SXGE_FREE(tdcp->txmb_s, (sizeof (void *)) * (tdcp->size));

	goto sxge_tdc_fini_exit;

sxge_tdc_fini_fail:
	status = SXGE_FAILURE;

sxge_tdc_fini_exit:

	SXGE_DBG((SXGEP, "sxge_tdc_fini: tdc=%d completed\n", cc));

	return (status);
}


int
sxge_tdc_reinit(tdc_state_t *tdcp)
{
	sxge_t			*sxgep = (sxge_t *)tdcp->sxgep;
	/* uint_t			cc = tdcp->tdcn; */
	/* set but not used */
	uint_t 			reset_cnt = tdcp->oerrors_rst + 1;
	int			status = SXGE_SUCCESS;

	SXGE_DBG0((SXGEP, "%d:sxge_tdc_reinit: resetting tdc %d oerrors_rst %d",
	    sxgep->instance, tdcp->tdcn, tdcp->oerrors_rst));

	if (!sxge_tdc_reinit_en)
		goto tdc_reinit_exit;

	status = sxge_tdc_fini(tdcp);
	status = sxge_tdc_init(tdcp);
	if (!tdcp->intr_msk) (void) sxge_tdc_mask(tdcp, B_FALSE);
	tdcp->oerrors_rst = reset_cnt;

	if (status == SXGE_FAILURE) {
		SXGE_DBG0((SXGEP, "%d:sxge_tdc_reinit: reinit failed tdc %d",
		    sxgep->instance, tdcp->tdcn));
		return (status);
	}

tdc_reinit_exit:
	SXGE_FM_SERVICE_RESTORED(sxgep);
	return (status);
}

int
sxge_tdc_errs(tdc_state_t *tdcp)
{
	sxge_t			*sxgep = (sxge_t *)tdcp->sxgep;
	uint64_t		pbase = tdcp->pbase;
	sxge_pio_handle_t	phdl = tdcp->phdl;
	uint_t			cc = tdcp->tdcn;
	int			channel = cc;
	tdc_cs_t		cs;
	sxge_tx_ring_stats_t	*tdc_stats;
	/*
	 * boolean_t		txchan_fatal = B_FALSE;
	 * boolean_t		txport_fatal = B_FALSE;
	 */
	/* set but not used */
	uint8_t			portn = sxgep->instance;
	int			status = SXGE_SUCCESS;

	SXGE_DBG((SXGEP, "%d:==> sxge_tdc_errs:", sxgep->instance));

	cs.value = SXGE_GET64(phdl, TDC_CS_REG);

	if (cs.value & sxge_tdc_err_msk) {

	tdc_stats = &sxgep->statsp->tdc_stats[channel];

	if (cs.bits.ldw.rej_resp_err) {
		tdc_stats->peu_resp_err++;
		SXGE_FM_REPORT_ERROR(sxgep, portn, channel,
		    SXGE_FM_EREPORT_TDC_REJECT_RESP_ERR);
		/* txchan_fatal = B_TRUE; */	/* set but not used */

		SXGE_DBG((SXGEP, "sxge_tdc_errs: rej_resp_err"));
	}
	if (cs.bits.ldw.sop_bit_err) {
		tdc_stats->invalid_sop++;
		SXGE_FM_REPORT_ERROR(sxgep, portn, channel,
		    SXGE_FM_EREPORT_TDC_SOP_BIT_ERR);
		/* txchan_fatal = B_TRUE; */	/* set but not used */

		SXGE_DBG((SXGEP, "sxge_tdc_errs: sop_bit_err"));
	}
	if (cs.bits.ldw.prem_sop_err) {
		tdc_stats->unexpected_sop++;
		SXGE_FM_REPORT_ERROR(sxgep, portn, channel,
		    SXGE_FM_EREPORT_TDC_PREMATURE_SOP_ERR);
		/* txchan_fatal = B_TRUE; */	/* set but not used */

		SXGE_DBG((SXGEP, "sxge_tdc_errs: prem_sop_err"));
	}
	if (cs.bits.ldw.desc_len_err) {
		tdc_stats->desc_len_err++;
		SXGE_FM_REPORT_ERROR(sxgep, portn, channel,
		    SXGE_FM_EREPORT_TDC_DESC_LENGTH_ERR);
		/* txchan_fatal = B_TRUE; */	/* set but not used */

		SXGE_DBG((SXGEP, "sxge_tdc_errs: desc_len_err"));
	}
	if (cs.bits.ldw.desc_nptr_err) {
		tdc_stats->desc_nptr_err++;
		SXGE_FM_REPORT_ERROR(sxgep, portn, channel,
		    SXGE_FM_EREPORT_TDC_DESC_NUM_PTR_ERR);
		/* txchan_fatal = B_TRUE; */	/* set but not used */

		SXGE_DBG((SXGEP, "sxge_tdc_errs: desc_nptr_err"));
	}

	if (cs.bits.ldw.mbox_err) {
		SXGE_FM_REPORT_ERROR(sxgep, portn, channel,
		    SXGE_FM_EREPORT_TDC_MBOX_ERR);
		/* txchan_fatal = B_TRUE; */	/* set but not used */

		SXGE_DBG((SXGEP, "sxge_tdc_errs: mbox_err"));
	}
	if (cs.bits.ldw.pkt_size_err) {
		tdc_stats->pkt_size_err++;
		SXGE_FM_REPORT_ERROR(sxgep, portn, channel,
		    SXGE_FM_EREPORT_TDC_PKT_SIZE_ERR);
		/* txchan_fatal = B_TRUE; */	/* set but not used */

		SXGE_DBG((SXGEP, "sxge_tdc_errs: rbr_timeout"));
	}
	if (cs.bits.ldw.tx_ring_oflow) {
		tdc_stats->tx_rng_oflow++;
		SXGE_FM_REPORT_ERROR(sxgep, portn, channel,
		    SXGE_FM_EREPORT_TDC_TX_RING_OFLOW);
		/* txchan_fatal = B_TRUE; */	/* set but not used */

		SXGE_DBG((SXGEP, "sxge_tdc_errs: tx_ring_oflow"));
	}
	if (cs.bits.ldw.pref_buf_par_err) {
		tdc_stats->pref_par_err++;
		SXGE_FM_REPORT_ERROR(sxgep, portn, channel,
		    SXGE_FM_EREPORT_TDC_PREF_BUF_PAR_ERR);
		/* txchan_fatal = B_TRUE; */	/* set but not used */

		SXGE_DBG((SXGEP, "sxge_tdc_errs: pref_buf_par_err"));
	}
	if (cs.bits.ldw.nack_pref) {
		tdc_stats->tdr_pref_cpl_to++;
		SXGE_FM_REPORT_ERROR(sxgep, portn, channel,
		    SXGE_FM_EREPORT_TDC_NACK_PREF);
		/* txchan_fatal = B_TRUE; */	/* set but not used */

		SXGE_DBG((SXGEP, "sxge_tdc_errs: nack_pref"));
	}
	if (cs.bits.ldw.nack_pkt_rd) {
		tdc_stats->peu_resp_err++;
		SXGE_FM_REPORT_ERROR(sxgep, portn, channel,
		    SXGE_FM_EREPORT_TDC_NACK_PKT_RD);
		/* txchan_fatal = B_TRUE; */	/* set but not used */

		SXGE_DBG((SXGEP, "sxge_tdc_errs: nack_pkt_rd"));
	}
	if (cs.bits.ldw.conf_part_err) {
		SXGE_FM_REPORT_ERROR(sxgep, portn, channel,
		    SXGE_FM_EREPORT_TDC_CONF_PART_ERR);
		/* txchan_fatal = B_TRUE; */	/* set but not used */

		SXGE_DBG((SXGEP, "sxge_tdc_errs: conf_part_err"));
	}
	if (cs.bits.ldw.pkt_prt_err) {
		tdc_stats->pkt_cpl_to++;
		SXGE_FM_REPORT_ERROR(sxgep, portn, channel,
		    SXGE_FM_EREPORT_TDC_PKT_PRT_ERR);
		/* txchan_fatal = B_TRUE; */	/* set but not used */

		SXGE_DBG((SXGEP, "sxge_tdc_errs: pkt_prt_err"));
	}

	status = sxge_tdc_reinit(tdcp);

	}

	SXGE_DBG((SXGEP, "%d:<== sxge_tdc_errs:", sxgep->instance));

	return (status);
}

/*
 * sxge_rdc_recv() - Receive one frame
 */
/* ARGSUSED */
mblk_t *
sxge_rdc_recv(rdc_state_t *rdcp, uint_t bytes, uint_t *flags)
{
	uint_t			i = 0, rxpad, rxhdr;
	uint32_t		rbrhead, rbrcurr, rbrnext;
	/* uint32_t		clscode; */	/* set but not used */
	uint32_t		rcrcurr, rcrtail;
	uint32_t		wrap, rcwrap;
	uint32_t		index, subidx, len, multi = 0, pkterrs;
	uint32_t		last, bufsz;
	uint32_t		pkttype;
	uint_t			suboff;
	/* uint_t			index_prev; */	/* set but not used */
	rdc_kick_t		rdc_kick;
	rdc_ctl_stat_t		ctl_stat;
	/* rdc_rcr_flsh_t		rcr_flsh; */	/* unused */
	rdc_rcr_desc_t		rcdesc, *rcmdp, *rcnextp, *rcmdlp;
	rdc_rbr_desc_t		rdesc, *rmdp, *rnextp, *rmdlp;
	rdc_mbx_desc_t		*mbxp;
	uint8_t			*rxbp;
	sxge_t			*sxgep = (sxge_t *)rdcp->sxgep;
	uint_t			cc = rdcp->rdcn;
	mblk_t			*fmp = NULL, *lmp = NULL, *nmp = NULL;
	uint_t			allocb_fail = 0;
	uint_t			allocb_fail_prev = 0;
	sxge_msg_t		*sxge_mbp, *sxge_mbp_s, *sxge_mbp_n;
	uint64_t		pbase = rdcp->pbase;
	sxge_pio_handle_t	phdl = rdcp->phdl;

	if (!rdcp->enable)
		return (NULL);

rdc_recv_start:
	*flags = 0;
	rxpad = rdcp->rxpad;
	rxhdr = rdcp->rxhdr;
	rmdp = rdcp->rmdp;
	rnextp = rdcp->rnextp;
	rmdlp = rdcp->rmdlp;
	rcmdp = rdcp->rcmdp;
	rcmdlp = rdcp->rcmdlp;
	rcnextp = rdcp->rcnextp;
	rcrcurr = rcnextp - rcmdp;
	rdcp->rcr_idx = rcrcurr;
	rbrcurr = rnextp - rmdp;
	rdcp->rbr_idx = rbrcurr;
	rbrnext = (rbrcurr+1)%(rdcp->size);
	mbxp = rdcp->mbxp; /* mbxp_a */
	/* index_prev = 0; */	/* set but not used */
	allocb_fail = 0;
	nmp = NULL;

	/* Check rdc_ctl_stat */
	do {

		if (sxge_rdc_mbx_en) {
		ctl_stat.value = SXGE_MGET64(phdl, &mbxp->ctl_stat.value);
		if (ctl_stat.value != rdcp->mbx_cs.value) {
			rdcp->mbx_cs.value = ctl_stat.value;
			rcrtail = ctl_stat.bits.hdw.rcrtail;
			rbrhead = ctl_stat.bits.hdw.rbrhead;
			goto rdc_ctl_stat_skip;
		}
		}

		ctl_stat.value = SXGE_GET64(phdl, RDC_CTL_STAT_REG);
		rdcp->rdc_cs.value = ctl_stat.value;
		rcrtail = ctl_stat.bits.hdw.rcrtail;
		rbrhead = ctl_stat.bits.hdw.rbrhead;

rdc_ctl_stat_skip:

		if (ctl_stat.value & sxge_rdc_err_msk) {
			(void) sxge_rdc_errs(rdcp);
			return (NULL);
		}

		if ((i++ > sxge_rdc_poll_max) && (multi == 0))
		return (NULL);

		if (rcrcurr == rcrtail)
		SXGE_UDELAY(sxgep, sxge_rdc_poll_usec, sxge_delay_busy);

	} while (rcrcurr == rcrtail);

	i = 0;

	/* Check rcdesc entry */
	rcdesc.value = SXGE_MGET64(phdl, (uint64_t)rcnextp);

	/* Check for pkt errors */
	pkterrs = rcdesc.bits.hdw.pkterrs;
	pkttype = rcdesc.bits.hdw.pkttype;
	multi = rcdesc.bits.hdw.multi;
	/* clscode = rcdesc.bits.hdw.clscode; */	/* set but not used */
	index = rcdesc.bits.ldw.index;
	subidx = rcdesc.bits.ldw.subidx;
	last = rcdesc.bits.ldw.last;
	bufsz = rcdesc.bits.hdw.bufsz;
	len = rcdesc.bits.hdw.len << 6 | rcdesc.bits.ldw.len;
	suboff = subidx * sxge_rdc_tiny_sz;

	if (fmp == NULL) {
		suboff += (rxpad + rxhdr);
		len -= (rxpad + rxhdr);
	}

	if (sxge_rdc_fcoe_en && (pkttype & 0x1)) {
		if (pkterrs == 0x3)
		SXGE_DBG0((SXGEP, "sxge_rdc_recv %d:fcoe error", cc));
	}

	if (pkterrs) {

		*flags |= (0x7 & pkterrs);
		atomic_add_32(&rdcp->ierrors, 1);
	}

	/* Check index */
	/*
	 * if (rbrnext != index) {
	 * 	index_prev = 1;
	 * }
	 */
	/* no consequent */

	if (!len) {
		SXGE_DBG0((SXGEP,
		    "sxge_rdc_recv %d: Error 0 length packet\n", cc));
	}

	if (sxge_rxmb_reuse) {

		sxge_mbp = (sxge_msg_t *)rdcp->rxmb[index]; /* rcrcurr */
		sxge_mbp_s = (sxge_msg_t *)rdcp->rxmb_s[index]; /* rcrcurr */
		sxge_mbp->mp->b_rptr = (uchar_t *)sxge_mbp->buffer_vp;
		sxge_mbp->mp->b_rptr += suboff;
		sxge_mbp->mp->b_wptr = sxge_mbp->mp->b_rptr + len;
		if (sxge_mbp->in_use) {
			if (!subidx) {
				sxge_mbp->in_bufsz = bufsz;
				sxge_mbp->in_last = last;
				rxbp = (uint8_t *)sxge_mbp->buffer_a;
				sxge_mbp->in_prev[bufsz] = sxge_mbp;
				sxge_mbp_n = sxge_mbp;
			} else {
				sxge_mbp_n = sxge_mbp->in_prev[bufsz];
				sxge_mbp_n->in_last = last;
				rxbp = (uint8_t *)sxge_mbp_n->buffer_a;
			}
		} else rxbp = (uint8_t *)rdcp->rxbp[index]; /* rcrcurr */
	} else rxbp = (uint8_t *)rdcp->rxbp[index]; /* rcrcurr */

	/* Wrap */
	rcwrap = rdcp->rcwrap;
	if ((rcnextp+1) == rcmdlp) rdcp->rcwrap = rcwrap?0:1;

	if (rbrnext != rbrhead) {
		wrap = rdcp->wrap;
		if ((rnextp+1) == rmdlp) rdcp->wrap = wrap?0:1;
	}

	/* Update rcnextp, rnextp */
	rcnextp->value = 0x0ULL;
	rdcp->rcnextp = NEXTRCMD(rdcp, rcnextp);
	rcnextp = rdcp->rcnextp;
	if (rbrnext != rbrhead) {
	rnextp = rdcp->rnextp;
	rdcp->rnextp = NEXTRMD(rdcp, rnextp);
	rnextp = rdcp->rnextp;
	}

	/* Kick rbr, rcr */
	rdc_kick.value = 0x0ULL;
	if (rbrnext != rbrhead) {
	rdc_kick.bits.ldw.rbrtail = (rnextp - rmdp);
	rdc_kick.bits.ldw.rbrtwrap = rdcp->wrap;
	rdc_kick.bits.ldw.rbrtvld = 1;
	}

	rdc_kick.bits.ldw.rcrhead = (rcnextp - rcmdp);
	rdc_kick.bits.ldw.rcrhwrap = rdcp->rcwrap;
	rdc_kick.bits.ldw.rcrhvld = 1;
	SXGE_PUT64(phdl, RDC_KICK_REG, rdc_kick.value);
	rdcp->rdc_kick.value = rdc_kick.value;

	/* Update ctl_stat */
	ctl_stat.bits.ldw.mbxen = sxge_rdc_mbx_en;
	ctl_stat.bits.ldw.mbxthres = 1;
	ctl_stat.bits.ldw.rcrthres = 1;
	ctl_stat.bits.ldw.rcrtmout = 1;
	SXGE_PUT64(phdl, RDC_CTL_STAT_REG, ctl_stat.value);

	/*
	 * Allocate/reuse and chain new mblk
	 */
	if (sxge_rxmb_reuse) {

		if (sxge_mbp->in_use) {

		if (sxge_mbp_s->ref_cnt == 1) {

			sxge_mbp_n = sxge_mbp_s;
			nmp = SXGE_DUPM(sxge_mbp, suboff, len);
			sxge_mbp->nmp = nmp;
			sxge_mbp_n->nmp = NULL;
			if (nmp == NULL) {
				sxge_mbp_n = NULL;
			}
		} else {

			if (sxge_rxmb_nwait) {

			if (sxge_rxmb_pend > sxge_rxmb_pend_max) {
				sxge_mbp_n = NULL;
				nmp = NULL;
			} else {
				sxge_mbp_n =
				    SXGE_ALLOCM(sxgep,
				    sxge_rdc_buf_sz, 1, NULL);
				if (sxge_mbp_n != NULL) {
					nmp = SXGE_DUPM(sxge_mbp, suboff, len);
					sxge_mbp->nmp = nmp;
					if (nmp == NULL) {
						SXGE_FREEB(sxge_mbp_n->mp);
						sxge_mbp_n = NULL;
					} else	SXGE_FREEB(sxge_mbp_s->mp);
				} else nmp = NULL;
			}

			/* !sxge_rxmb_nwait */
			} else {
				sxge_mbp_n = NULL;
				nmp = NULL;
			}

			if (sxge_rxmb_allocb)
			if (nmp == NULL) {
				allocb_fail = 1;
				nmp = SXGE_ALLOCB(len + suboff);
				if (nmp != NULL) {
					sxge_mbp->nmp = nmp;
					bcopy(sxge_mbp->buffer, nmp->b_rptr,
					    len + suboff);
					nmp->b_rptr += suboff;
					nmp->b_wptr = nmp->b_rptr + len;
					allocb_fail = 0;
				}
			}
		}

		sxge_mbp->in_bytes = len;
		sxge_mbp->in_use = 0;
		if (sxge_mbp_n != NULL) {
			rdcp->rxmb[index] = (void *)sxge_mbp_n;
			rdcp->rxmb_s[index] = (void *)sxge_mbp;
		}
		sxge_mbp = (sxge_msg_t *)rdcp->rxmb[index];
		sxge_mbp->in_bytes = 0;
		sxge_mbp->in_use = 1;

		/* !sxge_mbp->in_use */
		} else {

			nmp = SXGE_ALLOCB(len + suboff);
			if (nmp != NULL) {
				bcopy(rxbp, nmp->b_rptr, len + suboff);
				nmp->b_rptr += suboff;
				nmp->b_wptr = nmp->b_rptr + len;
			} else allocb_fail = 1;
		}

		/* replace page */
		rxbp = (uint8_t *)sxge_mbp->buffer_pp.dmac_laddress;
		rdesc.value = 0x0ULL;
		rdesc.bits.index = index;
		rdesc.bits.blkaddr =
		    (uint32_t)((uint64_t)rxbp>>RBR_BKADDR_SHIFT);
		SXGE_MPUT64(phdl, (uint64_t)&rmdp[index].value, rdesc.value);

	/* !sxge_rxmb_reuse */
	} else {

		nmp = SXGE_ALLOCB(len + suboff);
		if (nmp != NULL) {
			bcopy(rxbp, nmp->b_rptr, len + suboff);
			nmp->b_rptr += suboff;
			nmp->b_wptr = nmp->b_rptr + len;
		} else allocb_fail = 1;
	}

	atomic_add_32(&rdcp->ibytes, len);

	if (allocb_fail) {
		allocb_fail_prev = 1;
		atomic_add_32(&rdcp->ierrors_alloc, 1);
	}

sxge_rdc_recv_fail1:

	if (multi == 0) {
		/* last_desc */
		if (fmp == NULL) fmp = nmp;
		else {
			if (lmp != NULL) lmp->b_cont = nmp;
			if (allocb_fail_prev)
			while (fmp != NULL) {
				lmp = fmp->b_cont;
				SXGE_FREEB(fmp);
				fmp = lmp;
			}
		}
	} else {
		if (fmp == NULL) { fmp = nmp; lmp = nmp; }
		else {
			if (lmp != NULL) lmp->b_cont = nmp;
			if (nmp != NULL) lmp = nmp;
		}
		goto rdc_recv_start;
	}

	atomic_add_32(&rdcp->ipackets, 1);

	return (fmp);
}

void
sxge_rdc_dump(rdc_state_t *rdcp)
{

	sxge_t			*sxgep = (sxge_t *)rdcp->sxgep;
	/* uint_t			cc = rdcp->rdcn; */
	/* set but not used */
	uint64_t		pbase = rdcp->pbase;
	sxge_pio_handle_t	phdl = rdcp->phdl;

	SXGE_DBG0((SXGEP, "sxge_rdc_dump: vni = %d rdc = %d\n",
	    rdcp->vnin, rdcp->rdcn));

	SXGE_DBG0((SXGEP, "\trdc_pg_hdl\t\t[ %16llx ] \t= %llx\n",
	    RDC_PG_HDL_REG, SXGE_GET64(phdl, RDC_PG_HDL_REG)));

	SXGE_DBG0((SXGEP, "\trdc_cfg\t\t[ %16llx ] \t= %llx\n",
	    RDC_CFG_REG, SXGE_GET64(phdl, RDC_CFG_REG)));

	SXGE_DBG0((SXGEP, "\trdc_rbr_cfg \t\t[ %16llx ] \t= %llx\n",
	    RDC_RBR_CFG_REG, SXGE_GET64(phdl, RDC_RBR_CFG_REG)));

	SXGE_DBG0((SXGEP, "\trdc_rcr_cfg \t\t[ %16llx ] \t= %llx\n",
	    RDC_RCR_CFG_REG, SXGE_GET64(phdl, RDC_RCR_CFG_REG)));

	SXGE_DBG0((SXGEP, "\trdc_mbx_cfg \t\t[ %16llx ] \t= %llx\n",
	    RDC_MBX_CFG_REG, SXGE_GET64(phdl, RDC_MBX_CFG_REG)));

	SXGE_DBG0((SXGEP, "\trdc_rcr_tmr \t\t[ %16llx ] \t= %llx\n",
	    RDC_RCR_TMR_REG, SXGE_GET64(phdl, RDC_RCR_TMR_REG)));

	SXGE_DBG0((SXGEP, "\trdc_mbx_upd \t\t[ %16llx ] \t= %llx\n",
	    RDC_MBX_UPD_REG, SXGE_GET64(phdl, RDC_MBX_UPD_REG)));

	SXGE_DBG0((SXGEP, "\trdc_kick \t\t[ %16llx ] \t= %llx\n",
	    RDC_KICK_REG, SXGE_GET64(phdl, RDC_KICK_REG)));

	SXGE_DBG0((SXGEP, "\trdc_ent_msk \t\t[ %16llx ] \t= %llx\n",
	    RDC_ENT_MSK_REG, SXGE_GET64(phdl, RDC_ENT_MSK_REG)));

	SXGE_DBG0((SXGEP, "\trdc_pre_st\t\t[ %16llx ] \t= %llx\n",
	    RDC_PRE_ST_REG, SXGE_GET64(phdl, RDC_PRE_ST_REG)));

	SXGE_DBG0((SXGEP, "\trdc_ctl_stat\t\t[ %16llx ] \t= %llx\n",
	    RDC_CTL_STAT_REG, SXGE_GET64(phdl, RDC_CTL_STAT_REG)));

	SXGE_DBG0((SXGEP, "\trdc_ctl_stat_d\t\t[ %16llx ] \t= %llx\n",
	    RDC_CTL_STAT_DBG_REG, SXGE_GET64(phdl, RDC_CTL_STAT_DBG_REG)));

	SXGE_DBG0((SXGEP, "\trdc_pkt_cnt \t\t[ %16llx ] \t= %llx\n",
	    RDC_PKT_CNT_REG, SXGE_GET64(phdl, RDC_PKT_CNT_REG)));

	SXGE_DBG0((SXGEP, "\trdc_dis_cnt \t\t[ %16llx ] \t= %llx\n",
	    RDC_DIS_CNT_REG, SXGE_GET64(phdl, RDC_DIS_CNT_REG)));

	SXGE_DBG0((SXGEP, "\trdc_err_log \t\t[ %16llx ] \t= %llx\n",
	    RDC_ERR_LOG_REG, SXGE_GET64(phdl, RDC_ERR_LOG_REG)));

	SXGE_DBG0((SXGEP, "\trxvmac_frm_cnt \t\t[ %16llx ] \t= %llx\n",
	    (RXVMAC_BASE + 0x10), SXGE_GET64(phdl, (RXVMAC_BASE + 0x10))));

	SXGE_DBG0((SXGEP, "\trxvmac_byt_cnt \t\t[ %16llx ] \t= %llx\n",
	    (RXVMAC_BASE + 0x18), SXGE_GET64(phdl, (RXVMAC_BASE + 0x18))));

	SXGE_DBG0((SXGEP, "\trxvmac_drp_cnt \t\t[ %16llx ] \t= %llx\n",
	    (RXVMAC_BASE + 0x20), SXGE_GET64(phdl, (RXVMAC_BASE + 0x20))));

	if (sxge_dbg_pkts)
		SXGE_DBG0((SXGEP, "sxge_rdc_dump:%d: ipackets %d ibytes %d",
		    rdcp->rdcn, rdcp->ipackets, rdcp->ibytes));
	else
		SXGE_DBG0((SXGEP, "sxge_rdc_dump:%d: ipackets %d ibytes %d",
		    rdcp->rdcn, rdcp->ipackets, rdcp->ibytes));
}

int
sxge_rdc_map(rdc_state_t *rdcp)
{
	sxge_t			*sxgep = (sxge_t *)rdcp->sxgep;
	/*
	 * uint_t			cc = rdcp->rdcn;
	 * uint64_t		pbase = rdcp->pbase;
	 * sxge_pio_handle_t	phdl = rdcp->phdl;
	 */
	uint_t 			len, count;
	size_t			real_len;
	int			status = SXGE_SUCCESS;

	/*
	 * Initialize descriptor and buf handles
	 */
	rdcp->desc_h		= NULL;
	rdcp->desc_mh		= NULL;
	rdcp->desc_vp		= NULL;
	/* rdcp->desc_pp	= NULL; */

	/*
	 * Allocate descriptor handles and map them
	 */
	status = ddi_dma_alloc_handle(sxgep->dip, &sxge_desc_attr,
	    DDI_DMA_DONTWAIT, 0, &rdcp->desc_h);

	if (status != SXGE_SUCCESS) {
		SXGE_DBG((SXGEP,
		    "sxge_rdc_map: desc_h ddi_dma_alloc_handle fail"));
		goto sxge_rdc_map_fail;
	}

	len = rdcp->size * sizeof (rdc_rbr_desc_t);
	status = ddi_dma_mem_alloc(rdcp->desc_h, len,
	    &sxge_dev_dma_attr, DDI_DMA_CONSISTENT,
	    DDI_DMA_DONTWAIT, 0,
	    (caddr_t *)&rdcp->desc_vp,
	    &real_len, &rdcp->desc_mh);

	if (status != SXGE_SUCCESS) {
		SXGE_DBG((SXGEP,
		    "sxge_rdc_map: desc_mh ddi_dma_mem_alloc fail"));
		goto sxge_rdc_map_fail;
	}


	status = ddi_dma_addr_bind_handle(rdcp->desc_h, NULL,
	    (caddr_t)rdcp->desc_vp, real_len,
	    DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
	    /* DDI_DMA_WRITE | DDI_DMA_CONSISTENT, */
	    DDI_DMA_DONTWAIT, 0,
	    &rdcp->desc_pp, &count);

	if (status != DDI_DMA_MAPPED) {
		SXGE_DBG((SXGEP,
		    "sxge_rdc_map: desc_pp ddi_dma_addr_bind_handle fail"));
		goto sxge_rdc_map_fail;
	}

	/*
	 * Clear descriptor memory
	 */
	bzero((caddr_t)rdcp->desc_vp, real_len);

	/*
	 * Allocate completor descriptor handles and map them
	 */

	rdcp->cdesc_h		= NULL;
	rdcp->cdesc_mh		= NULL;
	rdcp->cdesc_vp		= NULL;
	/* rdcp->cdesc_pp	= NULL; */

	status = ddi_dma_alloc_handle(sxgep->dip, &sxge_desc_attr,
	    DDI_DMA_DONTWAIT, 0, &rdcp->cdesc_h);

	if (status != SXGE_SUCCESS) {
		SXGE_DBG((SXGEP,
		    "sxge_rdc_map: cdesc_h ddi_dma_alloc_handle fail"));
		goto sxge_rdc_map_fail;
	}

	len = rdcp->rcr_size * sizeof (rdc_rcr_desc_t);
	status = ddi_dma_mem_alloc(rdcp->cdesc_h, len,
	    &sxge_dev_dma_attr, DDI_DMA_CONSISTENT,
	    DDI_DMA_DONTWAIT, 0,
	    (caddr_t *)&rdcp->cdesc_vp,
	    &real_len, &rdcp->cdesc_mh);

	if (status != SXGE_SUCCESS) {
		SXGE_DBG((SXGEP,
		    "sxge_rdc_map: cdesc_mh ddi_dma_mem_alloc fail"));
		goto sxge_rdc_map_fail;
	}


	status = ddi_dma_addr_bind_handle(rdcp->cdesc_h, NULL,
	    (caddr_t)rdcp->cdesc_vp, real_len,
	    DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
	    /* DDI_DMA_WRITE | DDI_DMA_CONSISTENT, */
	    DDI_DMA_DONTWAIT, 0,
	    &rdcp->cdesc_pp, &count);

	if (status != DDI_DMA_MAPPED) {
		SXGE_DBG((SXGEP,
		    "sxge_rdc_map:cdesc_pp ddi_dma_addr_bind_handle fail"));
		goto sxge_rdc_map_fail;
	}

	/*
	 * Clear completor descriptor memory
	 */
	bzero((caddr_t)rdcp->cdesc_vp, real_len);

	/*
	 * Allocate buffer handles and map them
	 */

	rdcp->rxb_h		= NULL;
	rdcp->rxb_mh		= NULL;
	rdcp->rxb_vp		= NULL;
	/* rdcp->rxb_pp		= NULL; */

	status = ddi_dma_alloc_handle(sxgep->dip, &sxge_rx_buf_attr,
	    DDI_DMA_DONTWAIT, 0, &rdcp->rxb_h);

	if (status != SXGE_SUCCESS) {
		SXGE_DBG((SXGEP,
		    "sxge_rdc_map: rxb_h ddi_dma_alloc_handle fail"));
		goto sxge_rdc_map_fail;
	}

	len = rdcp->size * sxge_rdc_buf_sz;
	status = ddi_dma_mem_alloc(rdcp->rxb_h, len,
	    &sxge_buf_dma_attr, DDI_DMA_CONSISTENT,
	    DDI_DMA_DONTWAIT, 0,
	    (caddr_t *)&rdcp->rxb_vp,
	    &real_len, &rdcp->rxb_mh);

	if (status != SXGE_SUCCESS) {
		SXGE_DBG((SXGEP,
		    "sxge_rdc_map: rxb_mh ddi_dma_mem_alloc fail"));
		goto sxge_rdc_map_fail;
	}

	status = ddi_dma_addr_bind_handle(rdcp->rxb_h, NULL,
	    (caddr_t)rdcp->rxb_vp, real_len,
	    DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
	    /* DDI_DMA_WRITE | DDI_DMA_CONSISTENT, */
	    DDI_DMA_DONTWAIT, 0,
	    &rdcp->rxb_pp, &count);

	if (status != DDI_DMA_MAPPED) {
		SXGE_DBG((SXGEP,
		    "sxge_rdc_map: rxb_pp ddi_dma_addr_bind_handle fail"));
		goto sxge_rdc_map_fail;
	}

	/*
	 * Allocate mailbox handles and map them
	 */

	rdcp->mbx_h		= NULL;
	rdcp->mbx_mh		= NULL;
	rdcp->mbx_vp		= NULL;
	/* rdcp->mbx_pp		= NULL; */

	status = ddi_dma_alloc_handle(sxgep->dip, &sxge_desc_attr,
	    DDI_DMA_DONTWAIT, 0, &rdcp->mbx_h);

	if (status != SXGE_SUCCESS) {
		SXGE_DBG((SXGEP,
		    "sxge_rdc_map: mbx_h ddi_dma_alloc_handle fail"));
		goto sxge_rdc_map_fail;
	}

	len = (sizeof (rdc_mbx_desc_t));
	status = ddi_dma_mem_alloc(rdcp->mbx_h, len,
	    &sxge_dev_dma_attr, DDI_DMA_CONSISTENT,
	    DDI_DMA_DONTWAIT, 0,
	    (caddr_t *)&rdcp->mbx_vp,
	    &real_len, &rdcp->mbx_mh);

	if (status != SXGE_SUCCESS) {
		SXGE_DBG((SXGEP,
		    "sxge_rdc_map: mbx_mh ddi_dma_mem_alloc fail"));
		goto sxge_rdc_map_fail;
	}

	status = ddi_dma_addr_bind_handle(rdcp->mbx_h, NULL,
	    (caddr_t)rdcp->mbx_vp, real_len,
	    DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
	    /* DDI_DMA_WRITE | DDI_DMA_CONSISTENT, */
	    DDI_DMA_DONTWAIT, 0,
	    &rdcp->mbx_pp, &count);

	if (status != DDI_DMA_MAPPED) {
		SXGE_DBG((SXGEP,
		    "sxge_rdc_map: mbx_pp ddi_dma_addr_bind_handle fail"));
		goto sxge_rdc_map_fail;
	}

	goto sxge_rdc_map_exit;

sxge_rdc_map_fail:

	/*
	 * Undo binds and buf mappings
	 */

	if (rdcp->mbx_h) {
		(void) ddi_dma_unbind_handle(rdcp->mbx_h);
	}
	if (rdcp->mbx_mh)
		(void) ddi_dma_mem_free(&rdcp->mbx_mh);

	if (rdcp->mbx_h)
		(void) ddi_dma_free_handle(&rdcp->mbx_h);

	rdcp->mbx_h		= (ddi_dma_handle_t)NULL;
	rdcp->mbx_mh		= (ddi_acc_handle_t)NULL;
	rdcp->mbx_vp		= (void *)NULL;
	/* rdcp->mbx_pp		= (ddi_dma_cookie_t)NULL; */



	if (rdcp->rxb_h) {
		(void) ddi_dma_unbind_handle(rdcp->rxb_h);
	}
	if (rdcp->rxb_mh)
		(void) ddi_dma_mem_free(&rdcp->rxb_mh);

	if (rdcp->rxb_h)
		(void) ddi_dma_free_handle(&rdcp->rxb_h);

	rdcp->rxb_h		= (ddi_dma_handle_t)NULL;
	rdcp->rxb_mh		= (ddi_acc_handle_t)NULL;
	rdcp->rxb_vp		= (void *)NULL;
	/* rdcp->rxb_pp		= (ddi_dma_cookie_t)NULL; */

	/*
	 * Free and reset descriptor handles
	 */
	if (rdcp->desc_h) {
		(void) ddi_dma_unbind_handle(rdcp->desc_h);
	}
	if (rdcp->desc_mh)
		(void) ddi_dma_mem_free(&rdcp->desc_mh);

	if (rdcp->desc_h)
		(void) ddi_dma_free_handle(&rdcp->desc_h);

	rdcp->desc_h		= (ddi_dma_handle_t)NULL;
	rdcp->desc_mh		= (ddi_acc_handle_t)NULL;
	rdcp->desc_vp		= (void *)NULL;
	/* rdcp->desc_pp	= (ddi_dma_cookie_t)NULL; */

	/*
	 * Free and reset completor handles
	 */

	if (rdcp->cdesc_h) {
		(void) ddi_dma_unbind_handle(rdcp->cdesc_h);
	}
	if (rdcp->cdesc_mh)
		(void) ddi_dma_mem_free(&rdcp->cdesc_mh);

	if (rdcp->cdesc_h)
		(void) ddi_dma_free_handle(&rdcp->cdesc_h);

	rdcp->cdesc_h		= (ddi_dma_handle_t)NULL;
	rdcp->cdesc_mh		= (ddi_acc_handle_t)NULL;
	rdcp->cdesc_vp		= (void *)NULL;
	/* rdcp->cdesc_pp	= (ddi_dma_cookie_t)NULL; */

	status 	= SXGE_FAILURE;

sxge_rdc_map_exit:

	return (status);
}

int
sxge_rdc_unmap(rdc_state_t *rdcp)
{
#ifdef SXGE_DEBUG
	sxge_t			*sxgep = (sxge_t *)rdcp->sxgep;
#endif
	/*
	 * uint_t			cc = rdcp->rdcn;
	 * uint64_t		pbase = rdcp->pbase;
	 * sxge_pio_handle_t	phdl = rdcp->phdl;
	 */
	int			status = SXGE_SUCCESS;

	/*
	 * Undo binds and buf mappings
	 */

	if (rdcp->mbx_h) {
		(void) ddi_dma_unbind_handle(rdcp->mbx_h);
	}

	if (rdcp->mbx_mh)
		(void) ddi_dma_mem_free(&rdcp->mbx_mh);

	if (rdcp->mbx_h)
		(void) ddi_dma_free_handle(&rdcp->mbx_h);
	rdcp->mbx_h		= (ddi_dma_handle_t)NULL;
	rdcp->mbx_mh		= (ddi_acc_handle_t)NULL;
	rdcp->mbx_vp		= (void *)NULL;
	/* rdcp->mbx_pp		= (ddi_dma_cookie_t)NULL; */

	if (rdcp->rxb_h) {
		(void) ddi_dma_unbind_handle(rdcp->rxb_h);
	}
	if (rdcp->rxb_mh)
		(void) ddi_dma_mem_free(&rdcp->rxb_mh);

	if (rdcp->rxb_h)
		(void) ddi_dma_free_handle(&rdcp->rxb_h);

	rdcp->rxb_h		= (ddi_dma_handle_t)NULL;
	rdcp->rxb_mh		= (ddi_acc_handle_t)NULL;
	rdcp->rxb_vp		= (void *)NULL;
	/* rdcp->rxb_pp		= (ddi_dma_cookie_t)NULL; */

	/*
	 * Free and reset descriptor handles
	 */
	if (rdcp->desc_h) {
		(void) ddi_dma_unbind_handle(rdcp->desc_h);
	}
	if (rdcp->desc_mh)
		(void) ddi_dma_mem_free(&rdcp->desc_mh);

	if (rdcp->desc_h)
		(void) ddi_dma_free_handle(&rdcp->desc_h);

	rdcp->desc_h		= (ddi_dma_handle_t)NULL;
	rdcp->desc_mh		= (ddi_acc_handle_t)NULL;
	rdcp->desc_vp		= (void *)NULL;
	/* rdcp->desc_pp	= (ddi_dma_cookie_t)NULL; */

	/*
	 * Free and reset completor handles
	 */

	if (rdcp->cdesc_h) {
		(void) ddi_dma_unbind_handle(rdcp->cdesc_h);
	}
	if (rdcp->cdesc_mh)
		(void) ddi_dma_mem_free(&rdcp->cdesc_mh);

	if (rdcp->cdesc_h)
		(void) ddi_dma_free_handle(&rdcp->cdesc_h);

	rdcp->cdesc_h		= (ddi_dma_handle_t)NULL;
	rdcp->cdesc_mh		= (ddi_acc_handle_t)NULL;
	rdcp->cdesc_vp		= (void *)NULL;
	/* rdcp->cdesc_pp	= (ddi_dma_cookie_t)NULL; */

	goto sxge_rdc_unmap_exit;

sxge_rdc_unmap_fail:
	status = SXGE_FAILURE;

sxge_rdc_unmap_exit:

	SXGE_DBG((SXGEP, "sxge_rdc_unmap: return"));

	return (status);
}

int
sxge_rdc_init(rdc_state_t *rdcp)
{
	uint_t			i, rxpad, rxhdr;
	/* uint_t			delay = 0; */	/* set but not used */
	uint64_t		ring_phyaddr;
	uchar_t			*rxb;
	/* uint8_t			*rxbp; */	/* set but not used */
	rdc_rbr_desc_t		*rmdp, rmd;
	/* rdc_rcr_desc_t		*rcmdp; */	/* set but not used */
	/* rdc_rcr_desc_t		rcmd; */	/* unused */
	/* rdc_mbx_desc_t		*mbxp; */	/* unused */
	uint64_t		addr;
	int			cnt = 6;
	rdc_ctl_stat_t		rdc_ctl_stat;
	/* rdc_ent_msk_t		rdc_ent_msk; */	/* unused */
	rdc_cfg_t		rdc_cfg;
	rdc_rbr_cfg_t		rdc_rbr_cfg;
	rdc_rcr_cfg_t		rdc_rcr_cfg;
	rdc_mbx_cfg_t		rdc_mbx_cfg;
	rdc_mbx_upd_t		rdc_mbx_upd;
	rdc_rcr_tmr_t		rdc_rcr_tmr;
	rdc_kick_t		rdc_kick;
	sxge_t			*sxgep = (sxge_t *)rdcp->sxgep;
	uint_t			cc = rdcp->rdcn;
	sxge_msg_t		*sxge_mbp = NULL;
	uint64_t		pbase = rdcp->pbase = RDC_PBASE(rdcp);
	sxge_pio_handle_t	phdl = rdcp->phdl = sxgep->pio_hdl;
	int			status = SXGE_SUCCESS;

	rdcp->enable = 0;
	rdcp->size = sxge_rdc_rbr_nr;
	rdcp->rcr_size = sxge_rdc_rcr_nr;

	rxpad = rdcp->rxpad = RDC_PAD_SIZE;
	rxhdr = rdcp->rxhdr = RDC_HDR_SIZE;

	/*
	 * Allocate rbr, rcr, mbx
	 */
	/*
	 * map ring, dma bind rmdp
	 */

	if (sxge_rdc_map(rdcp)) {
		SXGE_DBG((SXGEP, "sxge_rdc_init: sxge_rdc_map fail\n"));
		goto sxge_rdc_init_fail;
	}

	rdcp->ring = rdcp->desc_vp;
	rdcp->rcr_ring = rdcp->cdesc_vp;
	rdcp->rxb = rdcp->rxb_vp;
	rdcp->mbxp = rdcp->mbx_vp;
	rdcp->rxbp = (uint64_t *)
	    SXGE_ZALLOC((sizeof (uint64_t))*((rdcp->rcr_size)));
	rdcp->rxmb = (void **)
	    SXGE_ZALLOC((sizeof (void *))*((rdcp->rcr_size)));
	rdcp->rxmb_s = (void **)
	    SXGE_ZALLOC((sizeof (void *))*((rdcp->rcr_size)));

	/* Setup rbr ring */
	ring_phyaddr = (uint64_t)(&rdcp->ring[0]);
	rdcp->rmdp = (rdc_rbr_desc_t *)(ring_phyaddr);
	rdcp->rnextp = &((rdcp->rmdp)[(rdcp->size)-1]);
	rdcp->rmdlp = &((rdcp->rmdp)[(rdcp->size)]);
	rdcp->wrap = 0;

	/* Setup rcr ring */
	ring_phyaddr = (uint64_t)(&rdcp->rcr_ring[0]);
	rdcp->rcmdp = (rdc_rcr_desc_t *)(ring_phyaddr);
	rdcp->rcnextp = rdcp->rcmdp;
	rdcp->rcmdlp = &((rdcp->rcmdp)[(rdcp->rcr_size)]);
	rdcp->rcwrap = 0;

	/* Setup rxb bufs */
	ring_phyaddr = (uint64_t)(rdcp->rxb);
	rdcp->rxb_a = (uint8_t *)rdcp->rxb;

	/* Fill rbr ring */
	rmdp = rdcp->rmdp;
	rxb = rdcp->rxb_a;
	for (i = 0; i < (rdcp->size); i++) {

		rdcp->rxbp[i] = (uint64_t)rxb;
		rdcp->rxmb[i] =
		    (void *)SXGE_ALLOCM(sxgep, sxge_rdc_buf_sz, 1, NULL);
		rdcp->rxmb_s[i] =
		    (void*)SXGE_ALLOCM(sxgep, sxge_rdc_buf_sz, 1, NULL);
		sxge_mbp = (sxge_msg_t *)rdcp->rxmb[i];

		if (sxge_rxmb_reuse) {
			sxge_mbp->in_use = 1;
		}

		rmd.value = 0x0ULL;
		rmd.bits.index = i;
		if (sxge_rxmb_reuse) {
			addr = sxge_mbp->buffer_pp.dmac_laddress;
		} else {
			addr = (uint64_t)rdcp->rxb_pp.dmac_laddress;
			addr += (i * sxge_rdc_buf_sz);
		}

		rmd.bits.blkaddr =
		    (uint32_t)((uint64_t)addr>>RBR_BKADDR_SHIFT);

		SXGE_MPUT64(phdl, (uint64_t)&rmdp[i].value, rmd.value);

		rxb += sxge_rdc_buf_sz;
	}

	/* Fill rcr ring */
	for (i = 0; i < (rdcp->rcr_size); i++) {
		rdcp->rcmdp[i].value = 0x0ULL;
		SXGE_MPUT64(phdl, (uint64_t)&rdcp->rcmdp[i], 0x0ULL);
	}

	/* Fill mbx desc */
	ring_phyaddr = (uint64_t)(rdcp->mbxp);
	rdcp->mbxp_a = (rdc_mbx_desc_t *)ring_phyaddr;

	/*
	 * Reset rdc dma channel
	 */

	/* Reset rdc_ctl_stat */
	rdc_ctl_stat.value = 0;
	rdc_ctl_stat.bits.ldw.rst = 1;
	SXGE_PUT64(phdl, RDC_CTL_STAT_REG, rdc_ctl_stat.value);

	SXGE_UDELAY(sxgep, 1, sxge_delay_busy);
	rdc_ctl_stat.value = SXGE_GET64(phdl, RDC_CTL_STAT_REG);
	while ((cnt--) && (rdc_ctl_stat.bits.ldw.rststate == (uint32_t)0)) {
		SXGE_UDELAY(sxgep, 1, sxge_delay_busy);
		rdc_ctl_stat.value = SXGE_GET64(phdl, RDC_CTL_STAT_REG);
	}
	if (cnt < 0) {
		SXGE_DBG0((SXGEP, "sxge_rdc_init: RDC_RESET fail\n"));
		SXGE_FM_REPORT_ERROR(sxgep, sxgep->instance, (int)cc,
		    SXGE_FM_EREPORT_RDC_RESET_FAIL);
		goto sxge_rdc_init_fail;
	}

	/* Setup rdc_cfg_reg, bufsz, blksz, pad, offset */
	rdc_cfg.value = 0;
	rdc_cfg.bits.ldw.pad = rxpad>>1; /* 2B rxpad */
	rdc_cfg.bits.ldw.hdrsz = rxhdr>>1; /* 0B 0x2:4B RSS */
	if (sxgep->tdc_prsr_en)
		rdc_cfg.bits.ldw.off = 0x0;	/* 0B */
	else rdc_cfg.bits.ldw.off = 0x1; /* 64B */
	rdc_cfg.bits.ldw.blksz = 0x0;	/* 4K */

	rdc_cfg.bits.ldw.bufsz2 = 0x1;	/* 4K 0x3:16K jumbo */
	/* rdc_cfg.bits.ldw.vld2 = 1; */ /* 4K for all pkts */

	SXGE_PUT64(phdl, RDC_CFG_REG, rdc_cfg.value);
	rdc_cfg.value = SXGE_GET64(phdl, RDC_CFG_REG);

	/* Setup rdc_rbr_cfg_reg */
	rdc_rbr_cfg.value = 0;
	rmdp = rdcp->rmdp;
	rdc_rbr_cfg.value = ((uint64_t)
	    (rdcp->desc_pp.dmac_laddress)) & RDC_RBR_CFG_START_MK;
	rdc_rbr_cfg.bits.hdw.len = (rdcp->size)/8;
	SXGE_PUT64(phdl, RDC_RBR_CFG_REG, rdc_rbr_cfg.value);
	rdc_rbr_cfg.value = SXGE_GET64(phdl, RDC_RBR_CFG_REG);


	/* Setup rdc_rcr_cfg_reg */
	rdc_rcr_cfg.value = 0;
	/* rcmdp = rdcp->rcmdp; */	/* set but not used */
	rdc_rcr_cfg.value = ((uint64_t)
	    (rdcp->cdesc_pp.dmac_laddress));
	rdc_rcr_cfg.bits.hdw.len = (rdcp->rcr_size);
	SXGE_PUT64(phdl, RDC_RCR_CFG_REG, rdc_rcr_cfg.value);
	rdc_rcr_cfg.value = SXGE_GET64(phdl, RDC_RCR_CFG_REG);

	/* Setup rdc_mbx_cfg */
	rdc_mbx_cfg.value = 0;
	rdc_mbx_cfg.value = (uint64_t)(rdcp->mbx_pp.dmac_laddress);
	SXGE_PUT64(phdl, RDC_MBX_CFG_REG, rdc_mbx_cfg.value);
	rdc_mbx_cfg.value = SXGE_GET64(phdl, RDC_MBX_CFG_REG);

	/* Setup rdc_mbx_upd_reg */
	rdc_mbx_upd.value = 0;
	rdc_mbx_upd.bits.ldw.pthres = sxge_rdc_mbx_pthres;
	rdc_mbx_upd.bits.ldw.enable = sxge_rdc_mbx_en;
	SXGE_PUT64(phdl, RDC_MBX_UPD_REG, rdc_mbx_upd.value);
	rdc_mbx_upd.value = SXGE_GET64(phdl, RDC_MBX_UPD_REG);

	/* Setup rdc_rcr_tmr_reg */
	rdc_rcr_tmr.value = 0;
	rdc_rcr_tmr.bits.ldw.pthres = sxge_rdc_pthres;
	rdc_rcr_tmr.bits.ldw.enpthres = sxge_rdc_pthres_en;
	rdc_rcr_tmr.bits.ldw.entimeout = sxge_rdc_timer_en;
	rdc_rcr_tmr.bits.ldw.timeout = sxge_rdc_timer; /* x RX_DMA_CK_DIV */
	SXGE_PUT64(phdl, RDC_RCR_TMR_REG, rdc_rcr_tmr.value);
	rdc_rcr_tmr.value = SXGE_GET64(phdl, RDC_RCR_TMR_REG);

	/* Setup rdc_ctl_stat, enable */
	rdc_ctl_stat.value = SXGE_GET64(phdl, RDC_CTL_STAT_REG);
	rdc_ctl_stat.bits.ldw.rststate = 0;
	rdc_ctl_stat.bits.ldw.mbxen = sxge_rdc_mbx_en;
	SXGE_PUT64(phdl, RDC_CTL_STAT_REG, rdc_ctl_stat.value);
	rdc_ctl_stat.value = SXGE_GET64(phdl, RDC_CTL_STAT_REG);

	/* Setup rdc_kick_reg, post bufs */
	rdc_kick.value = 0;
	rdc_kick.bits.ldw.rcrhead = 0;
	rdc_kick.bits.ldw.rcrhvld = 1;
	rdc_kick.bits.ldw.rbrtail = (rdcp->size) - sxge_rdc_dbg_sz;
	rdc_kick.bits.ldw.rbrtvld = 1;
	SXGE_PUT64(phdl, RDC_KICK_REG, rdc_kick.value);
	rdc_kick.value = SXGE_GET64(phdl, RDC_KICK_REG);

	/* Check rdc_ctl_stat */
	rdc_ctl_stat.value = SXGE_GET64(phdl, RDC_CTL_STAT_REG);
	rdcp->enable = 1;

	goto sxge_rdc_init_exit;

sxge_rdc_init_fail:
	status = SXGE_FAILURE;

sxge_rdc_init_exit:

	return (status);
}

int
sxge_rdc_mask(rdc_state_t *rdcp, boolean_t mask)
{
	/*
	 * sxge_t			*sxgep = (sxge_t *)rdcp->sxgep;
	 * uint_t			cc = rdcp->rdcn;
	 */
	/* set but not used */
	uint64_t		pbase = rdcp->pbase;
	sxge_pio_handle_t	phdl = rdcp->phdl;
	/* int			status = SXGE_SUCCESS; */
	/* set but not used */
	rdc_ent_msk_t		rdc_ent_msk;

	/* Setup rdc_ent_msk_reg */

	if (mask == B_TRUE) {

	rdc_ent_msk.value = 0;
	rdc_ent_msk.bits.ldw.rcrtmout = 1; /* 1 is disable */
	rdc_ent_msk.bits.ldw.rcrthres = 1;
	rdc_ent_msk.bits.ldw.mbxthres = 1;
	rdc_ent_msk.bits.ldw.rdcfifoerr = 1;
	rdc_ent_msk.bits.ldw.rcrshafull = 1;
	rdc_ent_msk.bits.ldw.resv0 = 0;
	SXGE_PUT64(phdl, RDC_ENT_MSK_REG, rdc_ent_msk.value);

	rdcp->intr_msk = 1;

	} else {

	rdc_ent_msk.value = 0;
	rdc_ent_msk.bits.ldw.rcrtmout = 0; /* 0 is enable */
	rdc_ent_msk.bits.ldw.rcrthres = 0;
	rdc_ent_msk.bits.ldw.mbxthres = 0;
	rdc_ent_msk.bits.ldw.rdcfifoerr = 1;
	rdc_ent_msk.bits.ldw.rcrshafull = 1;
	rdc_ent_msk.bits.ldw.resv0 = 0; /* pcnt_flow, drop_oflow, rbr_empty */
	SXGE_PUT64(phdl, RDC_ENT_MSK_REG, rdc_ent_msk.value);

	rdcp->intr_msk = 0;
	}

	return (0);
}

int
sxge_rdc_fini(rdc_state_t *rdcp)
{
	uint_t			i;
	/* uint_t			delay = 0; */	/* set but not used */
	/* uint64_t		ring_phyaddr; */	/* unused */
	/* uchar_t			*rxb; */	/* set but not used */
	rdc_rbr_desc_t		*rmdp, rmd;
	/* rdc_rcr_desc_t		*rcmdp, rcmd; */	/* unused */
	/* rdc_mbx_desc_t		*mbxp; */	/* unused */
	int			cnt = 6;
	rdc_ctl_stat_t		rdc_ctl_stat;
	rdc_ent_msk_t		rdc_ent_msk;
	rdc_cfg_t		rdc_cfg;
	rdc_rbr_cfg_t		rdc_rbr_cfg;
	rdc_rcr_cfg_t		rdc_rcr_cfg;
	rdc_mbx_cfg_t		rdc_mbx_cfg;
	rdc_mbx_upd_t		rdc_mbx_upd;
	rdc_rcr_tmr_t		rdc_rcr_tmr;
	rdc_kick_t		rdc_kick;
	sxge_t			*sxgep = (sxge_t *)rdcp->sxgep;
	uint_t			cc = rdcp->rdcn;
	uint64_t		pbase = rdcp->pbase;
	sxge_pio_handle_t	phdl = rdcp->phdl;
	int			status = SXGE_SUCCESS;

	rdcp->enable = 0;

	/*
	 * Reset rdc dma channel
	 */

	/* Reset rdc_ctl_stat */
	rdc_ctl_stat.value = 0;
	rdc_ctl_stat.bits.ldw.rst = 1;
	SXGE_PUT64(phdl, RDC_CTL_STAT_REG, rdc_ctl_stat.value);

	SXGE_UDELAY(sxgep, 1, sxge_delay_busy);
	rdc_ctl_stat.value = SXGE_GET64(phdl, RDC_CTL_STAT_REG);
	while ((cnt--) && (rdc_ctl_stat.bits.ldw.rststate == 0)) {
		SXGE_UDELAY(sxgep, 1, sxge_delay_busy);
		rdc_ctl_stat.value = SXGE_GET64(phdl, RDC_CTL_STAT_REG);
	}
	if (cnt < 0) {
		SXGE_DBG((SXGEP, "sxge_rdc_fini: RDC_RESET fail\n"));
		SXGE_FM_REPORT_ERROR(sxgep, sxgep->instance, (int)cc,
		    SXGE_FM_EREPORT_RDC_RESET_FAIL);
		status = SXGE_FAILURE;
	}

	SXGE_DBG((SXGEP,
	    "sxge_rdc_fini: rdc_ctl_stat 0x%llx\n", rdc_ctl_stat.value));

	/* Reset rdc_cfg_reg, bufsz, blksz, pad, offset */
	rdc_cfg.value = 0;
	SXGE_PUT64(phdl, RDC_CFG_REG, rdc_cfg.value);

	/* Reset rdc_rbr_cfg_reg */
	rdc_rbr_cfg.value = 0;
	SXGE_PUT64(phdl, RDC_RBR_CFG_REG, rdc_rbr_cfg.value);

	/* Reset rdc_rcr_cfg_reg */
	rdc_rcr_cfg.value = 0;
	SXGE_PUT64(phdl, RDC_RCR_CFG_REG, rdc_rcr_cfg.value);

	/* Reset rdc_mbx_cfg */
	rdc_mbx_cfg.value = 0;
	SXGE_PUT64(phdl, RDC_MBX_CFG_REG, rdc_mbx_cfg.value);

	/* Reset rdc_mbx_upd_reg */
	rdc_mbx_upd.value = 0;
	SXGE_PUT64(phdl, RDC_MBX_UPD_REG, rdc_mbx_upd.value);

	/* Reset rdc_rcr_tmr_reg */
	rdc_rcr_tmr.value = 0;
	SXGE_PUT64(phdl, RDC_RCR_TMR_REG, rdc_rcr_tmr.value);

	/* Reset rdc_ent_msk_reg */
	rdc_ent_msk.value = -1;
	SXGE_PUT64(phdl, RDC_ENT_MSK_REG, rdc_ent_msk.value);

	/* Reset rdc_ctl_stat, enable */
	rdc_ctl_stat.value = SXGE_GET64(phdl, RDC_CTL_STAT_REG);
	SXGE_PUT64(phdl, RDC_CTL_STAT_REG, rdc_ctl_stat.value);

	/* Reset rdc_kick_reg */
	rdc_kick.value = 0;
	rdc_kick.bits.ldw.rcrhvld = 1;
	rdc_kick.bits.ldw.rbrtvld = 1;
	SXGE_PUT64(phdl, RDC_KICK_REG, rdc_kick.value);

	/*
	 * Free rbr, rcr, mbx
	 */

	/* Free rbr ring */
	rmdp = rdcp->rmdp;
	/* rxb = rdcp->rxb; */	/* set but not used */
	for (i = 0; i < (rdcp->size); i++) {
		rmd.value = 0x0ULL;
		rmdp[i].value = rmd.value;
		SXGE_MPUT64(phdl, (uint64_t)&rmdp[i].value, rmd.value);
		if (rdcp->rxmb_s[i] != NULL) {
			SXGE_FREEB(((sxge_msg_t *)rdcp->rxmb_s[i])->mp);
			rdcp->rxmb_s[i] = NULL;
		}

		if (rdcp->rxmb[i] != NULL) {
			SXGE_FREEB(((sxge_msg_t *)rdcp->rxmb[i])->mp);
			rdcp->rxmb[i] = NULL;
		}
		rdcp->rxbp[i] = NULL;
	}

	/* Free rcr ring */
	for (i = 0; i < (rdcp->rcr_size); i++) {
		rdcp->rcmdp[i].value = 0x0ULL;
	}

	(void) sxge_rdc_unmap(rdcp);
	SXGE_FREE(rdcp->rxbp, (sizeof (uint64_t))*((rdcp->rcr_size)));
	SXGE_FREE(rdcp->rxmb, (sizeof (void *))*((rdcp->rcr_size)));
	SXGE_FREE(rdcp->rxmb_s, (sizeof (void *))*((rdcp->rcr_size)));

	goto sxge_rdc_fini_exit;

sxge_rdc_fini_fail:
	status = SXGE_FAILURE;

sxge_rdc_fini_exit:

	SXGE_DBG((SXGEP, "sxge_rdc_fini: rdc=%d completed\n", cc));

	return (status);
}

int
sxge_rdc_reinit(rdc_state_t *rdcp)
{
	sxge_t			*sxgep = (sxge_t *)rdcp->sxgep;
	/* uint_t			cc = rdcp->rdcn; */	/* unused */
	uint_t 			reset_cnt = rdcp->ierrors_rst + 1;
	int			status = SXGE_SUCCESS;

	SXGE_DBG((SXGEP, "%d:sxge_rdc_reinit: resetting rdc %d ierrors_rst %d",
	    sxgep->instance, rdcp->rdcn, rdcp->ierrors_rst));

	if (!sxge_rdc_reinit_en)
		goto rdc_reinit_exit;

	status = sxge_rdc_fini(rdcp);
	status = sxge_rdc_init(rdcp);
	if (!rdcp->intr_msk) (void) sxge_rdc_mask(rdcp, B_FALSE);
	rdcp->ierrors_rst = reset_cnt;

	if (status == SXGE_FAILURE) {
		SXGE_DBG((SXGEP, "%d:sxge_rdc_reinit: reinit failed rdc %d",
		    sxgep->instance, rdcp->rdcn));
		return (status);
	}

rdc_reinit_exit:
	SXGE_FM_SERVICE_RESTORED(sxgep);
	return (status);
}

int
sxge_rdc_errs(rdc_state_t *rdcp)
{
	sxge_t			*sxgep = (sxge_t *)rdcp->sxgep;
	uint64_t		pbase = rdcp->pbase;
	sxge_pio_handle_t	phdl = rdcp->phdl;
	uint_t			cc = rdcp->rdcn;
	int			channel = cc;
	rdc_ctl_stat_t		cs;
	sxge_rx_ring_stats_t	*rdc_stats;
	/* boolean_t		rxchan_fatal = B_FALSE; */
	/* set but not used */
	/* boolean_t		rxport_fatal = B_FALSE; */
	/* set but not used */
	uint8_t			portn = sxgep->instance;
	int			status = SXGE_SUCCESS;

	SXGE_DBG((SXGEP, "%d:==> sxge_rdc_errs:", sxgep->instance));

	cs.value = SXGE_GET64(phdl, RDC_CTL_STAT_REG);

	if (cs.value & sxge_rdc_err_msk) {

	rdc_stats = &sxgep->statsp->rdc_stats[channel];

	if (cs.bits.ldw.rdcfifoerr) {
		rdc_stats->ctrl_fifo_ecc_err++;
		SXGE_FM_REPORT_ERROR(sxgep, portn, channel,
		    SXGE_FM_EREPORT_RDC_FIFO_ERR);

		SXGE_DBG((SXGEP, "sxge_rdc_errs: fifo_err"));
	}
	if (cs.bits.ldw.rcrshafull) {
		rdc_stats->rcr_shadow_full++;
		SXGE_FM_REPORT_ERROR(sxgep, portn, channel,
		    SXGE_FM_EREPORT_RDC_SHADOW_FULL);

		SXGE_DBG((SXGEP, "sxge_rdc_errs: rcr_shadow_full"));
	}
	if (cs.bits.ldw.rbrreqrej) {
		rdc_stats->rbr_tmout++;
		SXGE_FM_REPORT_ERROR(sxgep, portn, channel,
		    SXGE_FM_EREPORT_RDC_REQUEST_REJECT);
		/* rxchan_fatal = B_TRUE; */	/* set but not used */

		SXGE_DBG((SXGEP, "sxge_rdc_errs: rbr_request_reject"));
	}
	if (cs.bits.ldw.rbrtmout) {
		rdc_stats->rbr_tmout++;
		SXGE_FM_REPORT_ERROR(sxgep, portn, channel,
		    SXGE_FM_EREPORT_RDC_RBR_TMOUT);
		/* rxchan_fatal = B_TRUE; */	/* set but not used */

		SXGE_DBG((SXGEP, "sxge_rdc_errs: rbr_timeout"));
	}
	if (cs.bits.ldw.rspdaterr) {
		rdc_stats->peu_resp_err++;
		SXGE_FM_REPORT_ERROR(sxgep, portn, channel,
		    SXGE_FM_EREPORT_RDC_RSP_DAT_ERR);
		/* rxchan_fatal = B_TRUE; */	/* set but not used */

		SXGE_DBG((SXGEP, "sxge_rdc_errs: resp_data_err"));
	}
	if (cs.bits.ldw.rcrackerr) {
		rdc_stats->rcr_unknown_err++;
		SXGE_FM_REPORT_ERROR(sxgep, portn, channel,
		    SXGE_FM_EREPORT_RDC_RCR_ACK_ERR);
		/* rxchan_fatal = B_TRUE; */	/* set but not used */

		SXGE_DBG((SXGEP, "sxge_rdc_errs: rcr_ack_err"));
	}
	if (cs.bits.ldw.rcrshapar) {
		rdc_stats->rcr_sha_par++;
		SXGE_FM_REPORT_ERROR(sxgep, portn, channel,
		    SXGE_FM_EREPORT_RDC_RCR_SHA_PAR);
		/* rxchan_fatal = B_TRUE; */	/* set but not used */

		SXGE_DBG((SXGEP, "sxge_rdc_errs: rcr_sha_par"));
	}
	if (cs.bits.ldw.rcrprepar) {
		rdc_stats->rbr_pre_par++;
		SXGE_FM_REPORT_ERROR(sxgep, portn, channel,
		    SXGE_FM_EREPORT_RDC_RBR_PRE_PAR);
		/* rxchan_fatal = B_TRUE; */	/* set but not used */

		SXGE_DBG((SXGEP, "sxge_rdc_errs: rbr_pre_par"));
	}
	if (cs.bits.ldw.rcrundflw) {
		rdc_stats->rcrfull++;
		SXGE_FM_REPORT_ERROR(sxgep, portn, channel,
		    SXGE_FM_EREPORT_RDC_RCR_RING_ERR);
		/* rxchan_fatal = B_TRUE; */	/* set but not used */

		SXGE_DBG((SXGEP, "sxge_rdc_errs: rcr_ring_err"));
	}
	if (cs.bits.ldw.rbrovrflw) {
		rdc_stats->rbr_empty++;
		SXGE_FM_REPORT_ERROR(sxgep, portn, channel,
		    SXGE_FM_EREPORT_RDC_RBR_RING_ERR);
		/* rxchan_fatal = B_TRUE; */	/* set but not used */

		SXGE_DBG((SXGEP, "sxge_rdc_errs: rbr_ring_err"));
	}

	status = sxge_rdc_reinit(rdcp);

	}
sxge_rdc_errs_exit:
	SXGE_DBG((SXGEP, "%d:<== sxge_rdc_errs:", sxgep->instance));

	return (status);
}

/*
 * Error Injection Utility: sxge_tdc_inject_err
 *
 * 	Inject an error into a TDC.
 *
 * Arguments:
 *	sxge_t* 	sxge
 *      uint32_t 	err_id	The error to inject
 *      uint8_t  	chan	The channel to inject error
 *
 * Notes:
 *	This is called from sxge_main.c:sxge_err_inject()
 *
 */
void
sxge_tdc_inject_err(sxge_t *sxgep, uint32_t err_id, uint8_t chan)
{
	tdc_intr_dbg_t 		cs;
	tdc_state_t 		*ring;
	sxge_pio_handle_t 	phdl;
	uint64_t 		pbase;

	ring = &sxgep->tdc[chan];
	phdl = ring->phdl;
	pbase = ring->pbase;

	SXGE_DBG((SXGEP, "%d:==> sxge_tdc_inject_err:", sxgep->instance));

	cs.value = SXGE_GET64(phdl, TDC_INTR_DBG_REG);

	switch (err_id) {
	case SXGE_FM_EREPORT_TDC_PKT_PRT_ERR:
		cs.bits.ldw.pkt_part_err = 1;
		break;
	case SXGE_FM_EREPORT_TDC_CONF_PART_ERR:
		cs.bits.ldw.conf_part_err = 1;
		break;
	case SXGE_FM_EREPORT_TDC_NACK_PKT_RD:
		cs.bits.ldw.nack_pkt_rd = 1;
		break;
	case SXGE_FM_EREPORT_TDC_NACK_PREF:
		cs.bits.ldw.nack_pref = 1;
		break;
	case SXGE_FM_EREPORT_TDC_PREF_BUF_PAR_ERR:
		cs.bits.ldw.pref_buf_par_err = 1;
		break;
	case SXGE_FM_EREPORT_TDC_TX_RING_OFLOW:
		cs.bits.ldw.tx_ring_oflow = 1;
		break;
	case SXGE_FM_EREPORT_TDC_PKT_SIZE_ERR:
		cs.bits.ldw.pkt_size_err = 1;
		break;
	case SXGE_FM_EREPORT_TDC_MBOX_ERR:
		cs.bits.ldw.mbox_err = 1;
		break;
	case SXGE_FM_EREPORT_TDC_DESC_NUM_PTR_ERR:
		cs.bits.ldw.desc_nptr_err = 1;
		break;
	case SXGE_FM_EREPORT_TDC_DESC_LENGTH_ERR:
		cs.bits.ldw.desc_len_err = 1;
		break;
	case SXGE_FM_EREPORT_TDC_PREMATURE_SOP_ERR:
		cs.bits.ldw.prem_sop_err = 1;
		break;
	case SXGE_FM_EREPORT_TDC_SOP_BIT_ERR:
		cs.bits.ldw.sop_bit_err = 1;
		break;
	case SXGE_FM_EREPORT_TDC_REJECT_RESP_ERR:
		cs.bits.ldw.rej_resp_err = 1;
		break;
	case SXGE_FM_EREPORT_TDC_RESET_FAIL:
		break;
	}

	SXGE_PUT64(phdl, TDC_INTR_DBG_REG, cs.value);

	cmn_err(CE_NOTE, "Write 0x%lx to TDC_INTR_DBG_REG\n", cs.value);

	SXGE_DBG((SXGEP, "%d:<== sxge_tdc_inject_err:", sxgep->instance));
}

/*
 * Error Injection Utility: sxge_rdc_inject_err
 *
 * 	Inject an error into a RDC.
 *
 * Arguments:
 *	sxge_t* 	sxge
 *      uint32_t 	err_id	The error to inject
 *      uint8_t  	chan	The channel to inject error
 *
 * Notes:
 *	This is called from sxge_main.c:sxge_err_inject()
 *
 */
void
sxge_rdc_inject_err(sxge_t *sxgep, uint32_t err_id, uint8_t chan)
{
	rdc_ctl_stat_dbg_t 	cs;
	rdc_state_t 		*ring;
	sxge_pio_handle_t 	phdl;
	uint64_t 		pbase;

	ring = &sxgep->rdc[chan];
	phdl = ring->phdl;
	pbase = ring->pbase;

	SXGE_DBG((SXGEP, "%d:==> sxge_rdc_inject_err:", sxgep->instance));

	cs.value = SXGE_GET64(phdl, RDC_CTL_STAT_DBG_REG);

	switch (err_id) {
	case SXGE_FM_EREPORT_RDC_RBR_RING_ERR:
		cs.bits.ldw.rbrovrflw = 1;
		break;
	case SXGE_FM_EREPORT_RDC_RCR_RING_ERR:
		cs.bits.ldw.rcrundflw = 1;
		break;
	case SXGE_FM_EREPORT_RDC_RBR_PRE_PAR:
		cs.bits.ldw.rcrprepar = 1;
		break;
	case SXGE_FM_EREPORT_RDC_RCR_SHA_PAR:
		cs.bits.ldw.rcrshapar = 1;
		break;
	case SXGE_FM_EREPORT_RDC_RCR_ACK_ERR:
		cs.bits.ldw.rcrackerr = 1;
		break;
	case SXGE_FM_EREPORT_RDC_RSP_DAT_ERR:
		cs.bits.ldw.rspdaterr = 1;
		break;
	case SXGE_FM_EREPORT_RDC_RBR_TMOUT:
		cs.bits.ldw.rbrtmout = 1;
		break;
	case SXGE_FM_EREPORT_RDC_REQUEST_REJECT:
		cs.bits.ldw.rbrreqrej = 1;
		break;
	case SXGE_FM_EREPORT_RDC_SHADOW_FULL:
		cs.bits.ldw.rcrshafull = 1;
		break;
	case SXGE_FM_EREPORT_RDC_FIFO_ERR:
		cs.bits.ldw.rdcfifoerr = 1;
		break;
	case SXGE_FM_EREPORT_RDC_RESET_FAIL:
		break;
	}

	SXGE_PUT64(phdl, RDC_CTL_STAT_DBG_REG, cs.value);

	cmn_err(CE_NOTE, "Write 0x%lx to RDC_CTL_STAT_DBG_REGG\n", cs.value);

	SXGE_DBG((SXGEP, "%d:<== sxge_rdc_inject_err:", sxgep->instance));
}
