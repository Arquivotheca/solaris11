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
 * Copyright 2011 Emulex.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Source file containing the Receive Path handling
 * functions
 */
#include <oce_impl.h>


void oce_rx_pool_free(char *arg);
static void oce_rqb_dtor(oce_rq_bdesc_t *rqbd);

static inline mblk_t *oce_rx(struct oce_dev *dev, struct oce_rq *rq,
    struct oce_nic_rx_cqe *cqe);
static inline mblk_t *oce_rx_bcopy(struct oce_dev *dev,
	struct oce_rq *rq, struct oce_nic_rx_cqe *cqe);
static int oce_rq_charge(struct oce_rq *rq, uint32_t nbufs, boolean_t repost);
static inline void oce_rx_insert_tag(mblk_t *mp, uint16_t vtag);
static void oce_set_rx_oflags(mblk_t *mp, struct oce_nic_rx_cqe *cqe);
static inline void oce_rx_drop_pkt(struct oce_rq *rq,
    struct oce_nic_rx_cqe *cqe);
static oce_rq_bdesc_t *oce_rqb_alloc(struct oce_rq *rq);
static void oce_rqb_free(struct oce_rq *rq, oce_rq_bdesc_t *rqbd);
static void oce_rq_post_buffer(struct oce_rq *rq, int nbufs);
static boolean_t oce_check_tagged(struct oce_dev *dev,
    struct oce_nic_rx_cqe *cqe);

#pragma	inline(oce_rx)
#pragma	inline(oce_rx_bcopy)
#pragma	inline(oce_rq_charge)
#pragma	inline(oce_rx_insert_tag)
#pragma	inline(oce_set_rx_oflags)
#pragma	inline(oce_rx_drop_pkt)
#pragma	inline(oce_rqb_alloc)
#pragma	inline(oce_rqb_free)
#pragma inline(oce_rq_post_buffer)

static ddi_dma_attr_t oce_rx_buf_attr = {
	DMA_ATTR_V0,		/* version number */
	0x0000000000000000ull,	/* low address */
	0xFFFFFFFFFFFFFFFFull,	/* high address */
	0x00000000FFFFFFFFull,	/* dma counter max */
	OCE_DMA_ALIGNMENT,	/* alignment */
	0x000007FF,		/* burst sizes */
	0x00000001,		/* minimum transfer size */
	0x00000000FFFFFFFFull,	/* maximum transfer size */
	0xFFFFFFFFFFFFFFFFull,	/* maximum segment size */
	1,			/* scatter/gather list length */
	0x00000001,		/* granularity */
	DDI_DMA_FLAGERR|DDI_DMA_RELAXED_ORDERING		/* DMA flags */
};

/*
 * function to create a DMA buffer pool for RQ
 *
 * dev - software handle to the device
 * num_items - number of buffers in the pool
 * item_size - size of each buffer
 *
 * return DDI_SUCCESS => success, DDI_FAILURE otherwise
 */
int
oce_rqb_cache_create(struct oce_rq *rq, size_t buf_size)
{
	int cnt;
	oce_rq_bdesc_t *rqbd;
	oce_dma_buf_t *dbuf;
	struct oce_dev *dev;
	uint32_t size;
	uint64_t paddr;
	caddr_t vaddr;

	rqbd = rq->rq_bdesc_array;
	size = buf_size * rq->cfg.nbufs;
	dev = rq->parent;

	dbuf = oce_alloc_dma_buffer(dev, size, &oce_rx_buf_attr,
	    (DDI_DMA_RDWR|DDI_DMA_STREAMING));
	if (dbuf == NULL) {
		return (DDI_FAILURE);
	}

	rq->rqb = dbuf;
	/* Set the starting phys and vaddr */
	paddr = dbuf->addr;
	vaddr = dbuf->base;

	for (cnt = 0; cnt < rq->cfg.nbufs; cnt++, rqbd++) {
		rqbd->mp = desballoc((uchar_t *)vaddr, buf_size, 0,
		    &rqbd->fr_rtn);
		if (rqbd->mp == NULL) {
			goto desb_fail;
		}
		/* Set the call back function parameters */
		rqbd->fr_rtn.free_func = (void (*)())oce_rx_pool_free;
		rqbd->fr_rtn.free_arg = (caddr_t)(void *)rqbd;
		/* Populate the DMA object for each buffer */
		rqbd->rqb.acc_handle = dbuf->acc_handle;
		rqbd->rqb.dma_handle = dbuf->dma_handle;
		rqbd->rqb.base = vaddr;
		rqbd->rqb.addr = paddr;
		rqbd->rqb.len  = buf_size;
		rqbd->rqb.size = buf_size;
		rqbd->rqb.off  = cnt * buf_size;
		rqbd->rq = rq;
		rqbd->frag_addr.dw.addr_lo = ADDR_LO(paddr);
		rqbd->frag_addr.dw.addr_hi = ADDR_HI(paddr);
		rq->rqb_freelist[cnt] = rqbd;
		/* increment the addresses */
		paddr += buf_size;
		vaddr += buf_size;
	}
	rq->rqb_free = rq->cfg.nbufs;
	rq->rqb_rc_head = 0;
	rq->rqb_next_free = 0;
	return (DDI_SUCCESS);

desb_fail:
	oce_rqb_cache_destroy(rq);
	return (DDI_FAILURE);
} /* oce_rqb_cache_create */

/*
 * function to Destroy RQ DMA buffer cache
 *
 * rq - pointer to rq structure
 *
 * return none
 */
void
oce_rqb_cache_destroy(struct oce_rq *rq)
{
	oce_rq_bdesc_t *rqbd = NULL;
	int cnt;

	rqbd = rq->rq_bdesc_array;
	for (cnt = 0; cnt < rq->cfg.nbufs; cnt++, rqbd++) {
		oce_rqb_dtor(rqbd);
	}

	oce_free_dma_buffer(rq->parent, rq->rqb);
	rq->rqb = NULL;
} /* oce_rqb_cache_destroy */

/*
 * RQ buffer destructor function
 *
 * rqbd - pointer to rq buffer descriptor
 *
 * return none
 */
static	void
oce_rqb_dtor(oce_rq_bdesc_t *rqbd)
{
	if ((rqbd == NULL) || (rqbd->rq == NULL)) {
		return;
	}
	if (rqbd->mp != NULL) {
		rqbd->fr_rtn.free_arg = NULL;
		freemsg(rqbd->mp);
		rqbd->mp = NULL;
	}
} /* oce_rqb_dtor */

/*
 * RQ buffer allocator function
 *
 * rq - pointer to RQ structure
 *
 * return pointer to RQ buffer descriptor
 */
static inline oce_rq_bdesc_t *
oce_rqb_alloc(struct oce_rq *rq)
{
	oce_rq_bdesc_t *rqbd;
	uint32_t free_index;
	free_index = rq->rqb_next_free;
	rqbd = rq->rqb_freelist[free_index];
	rq->rqb_freelist[free_index] = NULL;
	rq->rqb_next_free = GET_Q_NEXT(free_index, 1, rq->cfg.nbufs);
	return (rqbd);
} /* oce_rqb_alloc */

/*
 * function to free the RQ buffer
 *
 * rq - pointer to RQ structure
 * rqbd - pointer to recieve buffer descriptor
 *
 * return none
 */
static inline void
oce_rqb_free(struct oce_rq *rq, oce_rq_bdesc_t *rqbd)
{
	uint32_t free_index;
	mutex_enter(&rq->rc_lock);
	free_index = rq->rqb_rc_head;
	rq->rqb_freelist[free_index] = rqbd;
	rq->rqb_rc_head = GET_Q_NEXT(free_index, 1, rq->cfg.nbufs);
	mutex_exit(&rq->rc_lock);
	atomic_add_32(&rq->rqb_free, 1);
} /* oce_rqb_free */




static void oce_rq_post_buffer(struct oce_rq *rq, int nbufs)
{
	pd_rxulp_db_t rxdb_reg;
	int count;
	struct oce_dev *dev =  rq->parent;


	rxdb_reg.dw0 = 0;
	rxdb_reg.bits.qid = rq->rq_id & DB_RQ_ID_MASK;

	for (count = nbufs/OCE_MAX_RQ_POSTS; count > 0; count--) {
		rxdb_reg.bits.num_posted = OCE_MAX_RQ_POSTS;
		OCE_DB_WRITE32(dev, PD_RXULP_DB, rxdb_reg.dw0);
		rq->buf_avail += OCE_MAX_RQ_POSTS;
		nbufs -= OCE_MAX_RQ_POSTS;
	}
	if (nbufs > 0) {
		rxdb_reg.bits.num_posted = nbufs;
		OCE_DB_WRITE32(dev, PD_RXULP_DB, rxdb_reg.dw0);
		rq->buf_avail += nbufs;
	}
}
/*
 * function to charge a given rq with buffers from a pool's free list
 *
 * dev - software handle to the device
 * rq - pointer to the RQ to charge
 * nbufs - numbers of buffers to be charged
 *
 * return number of rqe's charges.
 */
static inline int
oce_rq_charge(struct oce_rq *rq, uint32_t nbufs, boolean_t repost)
{
	struct oce_nic_rqe *rqe;
	oce_rq_bdesc_t *rqbd;
	oce_rq_bdesc_t **shadow_rq;
	int cnt;
	int cur_index;
	oce_ring_buffer_t *ring;

	shadow_rq = rq->shadow_ring;
	ring = rq->ring;
	cur_index = ring->cidx;

	for (cnt = 0; cnt < nbufs; cnt++) {
		if (!repost) {
			rqbd = oce_rqb_alloc(rq);
		} else {
			/* just repost the buffers from shadow ring */
			rqbd = shadow_rq[cur_index];
			cur_index = GET_Q_NEXT(cur_index, 1, ring->num_items);
		}
		/* fill the rqes */
		rqe = RING_GET_PRODUCER_ITEM_VA(rq->ring,
		    struct oce_nic_rqe);
		rqe->u0.s.frag_pa_lo = rqbd->frag_addr.dw.addr_lo;
		rqe->u0.s.frag_pa_hi = rqbd->frag_addr.dw.addr_hi;
		shadow_rq[rq->ring->pidx] = rqbd;
		DW_SWAP(u32ptr(rqe), sizeof (struct oce_nic_rqe));
		RING_PUT(rq->ring, 1);
	}

	return (cnt);
} /* oce_rq_charge */

/*
 * function to release the posted buffers
 *
 * rq - pointer to the RQ to charge
 *
 * return none
 */
void
oce_rq_discharge(struct oce_rq *rq)
{
	oce_rq_bdesc_t *rqbd;
	oce_rq_bdesc_t **shadow_rq;

	shadow_rq = rq->shadow_ring;
	/* Free the posted buffer since RQ is destroyed already */
	while ((int32_t)rq->buf_avail > 0) {
		rqbd = shadow_rq[rq->ring->cidx];
		oce_rqb_free(rq, rqbd);
		RING_GET(rq->ring, 1);
		rq->buf_avail--;
	}
}
/*
 * function to process a single packet
 *
 * dev - software handle to the device
 * rq - pointer to the RQ to charge
 * cqe - Pointer to Completion Q entry
 *
 * return mblk pointer =>  success, NULL  => error
 */
static inline mblk_t *
oce_rx(struct oce_dev *dev, struct oce_rq *rq, struct oce_nic_rx_cqe *cqe)
{
	mblk_t *mp;
	int pkt_len;
	int32_t frag_cnt = 0;
	mblk_t **mblk_tail;
	mblk_t	*mblk_head;
	int frag_size;
	oce_rq_bdesc_t *rqbd;
	uint16_t cur_index;
	oce_ring_buffer_t *ring;
	int i;
	uint32_t hdr_len;

	frag_cnt  = cqe->u0.s.num_fragments & 0x7;
	mblk_head = NULL;
	mblk_tail = &mblk_head;

	ring = rq->ring;
	cur_index = ring->cidx;

	/* Get the relevant Queue pointers */
	pkt_len = cqe->u0.s.pkt_size;
	for (i = 0; i < frag_cnt; i++) {
		rqbd = rq->shadow_ring[cur_index];
		if (rqbd->mp == NULL) {
			rqbd->mp = desballoc((uchar_t *)rqbd->rqb.base,
			    rqbd->rqb.size, 0, &rqbd->fr_rtn);
			if (rqbd->mp == NULL) {
				return (NULL);
			}
		}

		mp = rqbd->mp;
		frag_size  = (pkt_len > rq->cfg.frag_size) ?
		    rq->cfg.frag_size : pkt_len;
		mp->b_wptr = mp->b_rptr + frag_size;
		pkt_len   -= frag_size;
		mp->b_next = mp->b_cont = NULL;
		/* Chain the message mblks */
		*mblk_tail = mp;
		mblk_tail = &mp->b_cont;
		DBUF_SYNC(&rqbd->rqb, rqbd->rqb.off, rqbd->rqb.len,
		    DDI_DMA_SYNC_FORCPU);
		cur_index = GET_Q_NEXT(cur_index, 1, ring->num_items);
	}
	if (mblk_head == NULL) {
		oce_log(dev, CE_WARN, MOD_RX, "%s", "oce_rx:no frags?");
		return (NULL);
	}
	/* coallesce headers + Vtag  to first mblk */
	mp = allocb(OCE_HDR_LEN, BPRI_HI);
	if (mp == NULL) {
		return (NULL);
	}
	/* Align the IP header */
	mp->b_rptr += OCE_IP_ALIGN;

	if (oce_check_tagged(dev, cqe)) {
		hdr_len = min(MBLKL(mblk_head), OCE_HDR_LEN) -
		    VTAG_SIZE - OCE_IP_ALIGN;
		(void) memcpy(mp->b_rptr, mblk_head, 2 * ETHERADDRL);
		oce_rx_insert_tag(mp, cqe->u0.s.vlan_tag);
		(void) memcpy(mp->b_rptr + 16, mblk_head->b_rptr + 12,
		    hdr_len - 12);
		mp->b_wptr = mp->b_rptr + VTAG_SIZE + hdr_len;
	} else {

		hdr_len = min(MBLKL(mblk_head), OCE_HDR_LEN) - OCE_IP_ALIGN;
		(void) memcpy(mp->b_rptr, mblk_head->b_rptr, hdr_len);
		mp->b_wptr = mp->b_rptr + hdr_len;
	}
	mblk_head->b_rptr += hdr_len;
	if (MBLKL(mblk_head) > 0) {
		mp->b_cont = mblk_head;
	} else {
		mp->b_cont = mblk_head->b_cont;
		freeb(mblk_head);
	}
	/* replace the buffer with new ones */
	(void) oce_rq_charge(rq, frag_cnt, B_FALSE);
	atomic_add_32(&rq->pending, frag_cnt);
	return (mp);
} /* oce_rx */

static inline mblk_t *
oce_rx_bcopy(struct oce_dev *dev, struct oce_rq *rq, struct oce_nic_rx_cqe *cqe)
{
	mblk_t *mp;
	int pkt_len;
	int32_t frag_cnt = 0;
	int frag_size;
	oce_rq_bdesc_t *rqbd;
	uint32_t cur_index;
	oce_ring_buffer_t *ring;
	oce_rq_bdesc_t **shadow_rq;
	int cnt = 0;
	pkt_len = cqe->u0.s.pkt_size;

	mp = allocb(pkt_len + OCE_RQE_BUF_HEADROOM, BPRI_HI);
	if (mp == NULL) {
		return (NULL);
	}

	ring = rq->ring;
	shadow_rq = rq->shadow_ring;
	frag_cnt = cqe->u0.s.num_fragments & 0x7;
	cur_index = ring->cidx;
	rqbd = shadow_rq[cur_index];
	frag_size  = min(pkt_len, rq->cfg.frag_size);
	/* Align IP header */
	mp->b_rptr += OCE_IP_ALIGN;

	/* Sync the first buffer */
	DBUF_SYNC(&rqbd->rqb, rqbd->rqb.off, rqbd->rqb.len,
	    DDI_DMA_SYNC_FORCPU);

	if (oce_check_tagged(dev, cqe)) {
		(void) memcpy(mp->b_rptr, rqbd->rqb.base, 2  * ETHERADDRL);
		oce_rx_insert_tag(mp, cqe->u0.s.vlan_tag);
		(void) memcpy(mp->b_rptr + 16, rqbd->rqb.base + 12,
		    frag_size - 12);
		mp->b_wptr = mp->b_rptr + frag_size + VTAG_SIZE;
	} else {
		(void) memcpy(mp->b_rptr, rqbd->rqb.base, frag_size);
		mp->b_wptr = mp->b_rptr + frag_size;
	}

	for (cnt = 1; cnt < frag_cnt; cnt++) {
		cur_index = GET_Q_NEXT(cur_index, 1, ring->num_items);
		pkt_len   -= frag_size;
		rqbd = shadow_rq[cur_index];
		frag_size  = min(rq->cfg.frag_size, pkt_len);
		DBUF_SYNC(&rqbd->rqb, rqbd->rqb.off, rqbd->rqb.len,
		    DDI_DMA_SYNC_FORCPU);
		(void) memcpy(mp->b_wptr, rqbd->rqb.base, frag_size);
		mp->b_wptr += frag_size;
	}
	(void) oce_rq_charge(rq, frag_cnt, B_TRUE);
	return (mp);
}

static inline void
oce_set_rx_oflags(mblk_t *mp, struct oce_nic_rx_cqe *cqe)
{
	int csum_flags = 0;

	/* set flags */
	if (cqe->u0.s.ip_cksum_pass) {
		csum_flags |= HCK_IPV4_HDRCKSUM_OK;
	}

	if (cqe->u0.s.l4_cksum_pass) {
		csum_flags |= (HCK_FULLCKSUM | HCK_FULLCKSUM_OK);
	}

	if (csum_flags) {
		(void) mac_hcksum_set(mp, 0, 0, 0, 0, csum_flags);
	}
}

static inline void
oce_rx_insert_tag(mblk_t *mp, uint16_t vtag)
{
	struct ether_vlan_header *ehp;
	ehp = (struct ether_vlan_header *)voidptr(mp->b_rptr);
	ehp->ether_tpid = htons(ETHERTYPE_VLAN);
	ehp->ether_tci = LE_16(vtag);
}

static inline void
oce_rx_drop_pkt(struct oce_rq *rq, struct oce_nic_rx_cqe *cqe)
{
	int frag_cnt;
	oce_rq_bdesc_t *rqbd;
	oce_rq_bdesc_t  **shadow_rq;
	shadow_rq = rq->shadow_ring;
	for (frag_cnt = 0; frag_cnt < cqe->u0.s.num_fragments; frag_cnt++) {
		rqbd = shadow_rq[rq->ring->cidx];
		oce_rqb_free(rq, rqbd);
		RING_GET(rq->ring, 1);
	}
}


/*
 * function to process a Recieve queue
 *
 * arg - pointer to the RQ to charge
 *
 * return number of cqes processed
 */
uint16_t
oce_drain_rq_cq(void *arg)
{
	struct oce_nic_rx_cqe *cqe;
	struct oce_rq *rq;
	mblk_t *mp = NULL;
	mblk_t *mblk_head;
	mblk_t **mblk_tail;
	uint16_t num_cqe = 0;
	struct oce_cq  *cq;
	struct oce_dev *dev;
	int32_t frag_cnt;
	uint32_t nbufs = 0;

	rq = (struct oce_rq *)arg;
	dev = rq->parent;
	cq = rq->cq;
	mblk_head = NULL;
	mblk_tail = &mblk_head;

	cqe = RING_GET_CONSUMER_ITEM_VA(cq->ring, struct oce_nic_rx_cqe);

	(void) DBUF_SYNC(cq->ring->dbuf, 0, 0, DDI_DMA_SYNC_FORKERNEL);
	/* dequeue till you reach an invalid cqe */
	while (RQ_CQE_VALID(cqe)) {
		DW_SWAP(u32ptr(cqe), sizeof (struct oce_nic_rx_cqe));
		frag_cnt = cqe->u0.s.num_fragments & 0x7;
		/* if insufficient buffers to charge then do copy */
		if ((cqe->u0.s.pkt_size < dev->rx_bcopy_limit) ||
		    (oce_atomic_reserve(&rq->rqb_free, frag_cnt) < 0)) {
			mp = oce_rx_bcopy(dev, rq, cqe);
		} else {
			mp = oce_rx(dev, rq, cqe);
			if (mp == NULL) {
				atomic_add_32(&rq->rqb_free, frag_cnt);
				mp = oce_rx_bcopy(dev, rq, cqe);
			}
		}
		if (mp != NULL) {
			oce_set_rx_oflags(mp, cqe);
			*mblk_tail = mp;
			mblk_tail = &mp->b_next;

		} else {
			(void) oce_rq_charge(rq, frag_cnt, B_TRUE);
		}
		RING_GET(rq->ring, frag_cnt);
		rq->buf_avail -= frag_cnt;
		nbufs += frag_cnt;
		oce_rq_post_buffer(rq, frag_cnt);
		RQ_CQE_INVALIDATE(cqe);
		RING_GET(cq->ring, 1);
		cqe = RING_GET_CONSUMER_ITEM_VA(cq->ring,
		    struct oce_nic_rx_cqe);
		num_cqe++;
		/* process max ring size */
		if (num_cqe > dev->rx_pkt_per_intr) {
			break;
		}
	} /* for all valid CQEs */

	if (mblk_head) {
		mac_rx(dev->mac_handle, NULL, mblk_head);
	}
	DBUF_SYNC(cq->ring->dbuf, 0, 0, DDI_DMA_SYNC_FORDEV);
	oce_arm_cq(dev, cq->cq_id, num_cqe, B_TRUE);
	return (num_cqe);
} /* oce_drain_rq_cq */

/*
 * function to free mblk databuffer to the RQ pool
 *
 * arg - pointer to the receive buffer descriptor
 *
 * return none
 */
void
oce_rx_pool_free(char *arg)
{
	oce_rq_bdesc_t *rqbd;
	struct oce_rq  *rq;

	/* During destroy, arg will be NULL */
	if (arg == NULL) {
		return;
	}

	/* retrieve the pointers from arg */
	rqbd = (oce_rq_bdesc_t *)(void *)arg;
	rq = rqbd->rq;
	rqbd->mp = desballoc((uchar_t *)rqbd->rqb.base,
	    rqbd->rqb.size, 0, &rqbd->fr_rtn);
	oce_rqb_free(rq, rqbd);
	(void) atomic_add_32(&rq->pending, -1);
} /* rx_pool_free */

/*
 * function to stop the RX
 *
 * rq - pointer to RQ structure
 *
 * return none
 */
void
oce_clean_rq(struct oce_rq *rq)
{
	uint16_t num_cqe = 0;
	struct oce_cq  *cq;
	struct oce_dev *dev;
	struct oce_nic_rx_cqe *cqe;
	int32_t ti = 0;

	dev = rq->parent;
	cq = rq->cq;
	cqe = RING_GET_CONSUMER_ITEM_VA(cq->ring, struct oce_nic_rx_cqe);
	/* dequeue till you reach an invalid cqe */
	for (ti = 0; ti < DEFAULT_DRAIN_TIME; ti++) {

		while (RQ_CQE_VALID(cqe)) {
			DW_SWAP(u32ptr(cqe), sizeof (struct oce_nic_rx_cqe));
			oce_rx_drop_pkt(rq, cqe);
			atomic_add_32(&rq->buf_avail,
			    -(cqe->u0.s.num_fragments & 0x7));
			oce_arm_cq(dev, cq->cq_id, 1, B_TRUE);
			RQ_CQE_INVALIDATE(cqe);
			RING_GET(cq->ring, 1);
			cqe = RING_GET_CONSUMER_ITEM_VA(cq->ring,
			    struct oce_nic_rx_cqe);
			num_cqe++;
		}
		OCE_MSDELAY(1);
	}
} /* oce_clean_rq */

/*
 * function to start  the RX
 *
 * rq - pointer to RQ structure
 *
 * return number of rqe's charges.
 */
int
oce_start_rq(struct oce_rq *rq)
{
	int ret = 0;
	int to_charge = 0;
	struct oce_dev *dev = rq->parent;
	to_charge = rq->cfg.q_len - rq->buf_avail;
	to_charge = min(to_charge, rq->rqb_free);
	atomic_add_32(&rq->rqb_free, -to_charge);
	(void) oce_rq_charge(rq, to_charge, B_FALSE);
	/* ok to do it here since Rx has not even started */
	oce_rq_post_buffer(rq, to_charge);
	oce_arm_cq(dev, rq->cq->cq_id, 0, B_TRUE);
	return (ret);
} /* oce_start_rq */

/* Checks for pending rx buffers with Stack */
int
oce_rx_pending(struct oce_dev *dev, struct oce_rq *rq, int32_t timeout)
{
	int ti;
	_NOTE(ARGUNUSED(dev));

	for (ti = 0; ti < timeout; ti++) {
		if (rq->pending > 0) {
			OCE_MSDELAY(10);
			continue;
		} else {
			rq->pending = 0;
			break;
		}
	}
	return (rq->pending);
}

static boolean_t
oce_check_tagged(struct oce_dev *dev, struct oce_nic_rx_cqe *cqe)
{
	boolean_t tagged = B_FALSE;
	if (dev->function_mode & FLEX10_MODE) {
		if (cqe->u0.s.vlan_tag_present &&
		    cqe->u0.s.qnq) {
			tagged = B_TRUE;
		}
	} else if (cqe->u0.s.vlan_tag_present) {
		tagged = B_TRUE;
	}
	return (tagged);
}
