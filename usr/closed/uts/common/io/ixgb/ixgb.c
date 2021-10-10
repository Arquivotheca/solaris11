/*
 * Copyright (c) 2004, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * IXGB common functions
 */

#include "ixgb.h"

static char localmac_boolname[] = "local-mac-address?";
static char localmac_propname[] = "local-mac-address";
static char macaddr_propname[] = "mac-address";

/*
 * Describes the chip's DMA engine
 */
static ddi_dma_attr_t dma_attr = {
	DMA_ATTR_V0,			/* dma_attr version	*/
	0x0000000000000000ull,		/* dma_attr_addr_lo	*/
	0xFFFFFFFFFFFFFFFFull,		/* dma_attr_addr_hi	*/
	0x00000000FFFFFFFFull,		/* dma_attr_count_max	*/
	0x0000000000000100ull,		/* dma_attr_align	*/
	0x00000FFF,			/* dma_attr_burstsizes	*/
	0x00000001,			/* dma_attr_minxfer	*/
	0x000000000000FFFFull,		/* dma_attr_maxxfer	*/
	0xFFFFFFFFFFFFFFFFull,		/* dma_attr_seg		*/
	1,				/* dma_attr_sgllen 	*/
	0x00000001,			/* dma_attr_granular 	*/
	0				/* dma_attr_flags 	*/
};

static ddi_dma_attr_t tx_dma_attr = {
	DMA_ATTR_V0,			/* dma_attr version	*/
	0x0000000000000000ull,		/* dma_attr_addr_lo	*/
	0xFFFFFFFFFFFFFFFFull,		/* dma_attr_addr_hi	*/
	0x00000000FFFFFFFFull,		/* dma_attr_count_max	*/
	0x0000000000000010ull,		/* dma_attr_align	*/
	0x00000FFF,			/* dma_attr_burstsizes	*/
	0x00000001,			/* dma_attr_minxfer	*/
	0x000000000000FFFFull,		/* dma_attr_maxxfer	*/
	0xFFFFFFFFFFFFFFFFull,		/* dma_attr_seg		*/
	IXGB_MAP_COOKIES,		/* dma_attr_sgllen 	*/
	0x00000001,			/* dma_attr_granular 	*/
	0				/* dma_attr_flags 	*/
};


/*
 * DMA access attributes for descriptors.
 */
static ddi_device_acc_attr_t ixgb_desc_accattr = {
	DDI_DEVICE_ATTR_V0,
	DDI_STRUCTURE_LE_ACC,
	DDI_STRICTORDER_ACC
};

/*
 * DMA access attributes for data.
 */
static ddi_device_acc_attr_t ixgb_data_accattr = {
	DDI_DEVICE_ATTR_V0,
#ifdef _BIG_ENDIAN
	DDI_STRUCTURE_BE_ACC,
#else
	DDI_STRUCTURE_LE_ACC,
#endif
	DDI_STRICTORDER_ACC
};


static enum ioc_reply
ixgb_set_loop_mode(ixgb_t *ixgbp, uint32_t mode)
{
	/*
	 * If the mode isn't being changed, there's nothing to do ...
	 */
	if (mode == ixgbp->param_loop_mode)
		return (IOC_ACK);

	/*
	 * Validate the requested mode.
	 */
	switch (mode) {
	default:
		return (IOC_INVAL);

	case IXGB_LOOP_NONE:
	case IXGB_LOOP_EXTERNAL_XAUI:
	case IXGB_LOOP_EXTERNAL_XGMII:
	case IXGB_LOOP_INTERNAL_XGMII:
		break;
	}

	/*
	 * All OK; tell the caller to reprogram
	 * the PHY and/or MAC for the new mode ...
	 */
	ixgbp->param_loop_mode = mode;
	return (IOC_RESTART_ACK);
}

#undef	IXGB_DBG
#define	IXGB_DBG	IXGB_DBG_INIT	/* debug flag for this code	*/

/*
 * Utility routine to carve a slice off a chunk of allocated memory,
 * updating the chunk descriptor accordingly.  The size of the slice
 * is given by the product of the <qty> and <size> parameters.
 */
static void
ixgb_slice_chunk(dma_area_t *slice, dma_area_t *chunk,
    uint32_t qty, uint32_t size)
{
	size_t totsize;

	totsize = qty*size;
	ASSERT(size > 0);
	ASSERT(totsize <= chunk->alength);

	*slice = *chunk;
	slice->nslots = qty;
	slice->size = size;
	slice->alength = totsize;

	chunk->mem_va = (caddr_t)chunk->mem_va + totsize;
	chunk->alength -= totsize;
	chunk->offset += totsize;
	chunk->cookie.dmac_laddress += totsize;
	chunk->cookie.dmac_size -= totsize;
}

/*
 * Allocate an area of memory and a DMA handle for accessing it
 */
static int
ixgb_alloc_dma_mem(ixgb_t *ixgbp, size_t memsize, ddi_device_acc_attr_t *attr_p,
    uint_t dma_flags, dma_area_t *dma_p)
{
	int err;
	caddr_t va;

	IXGB_TRACE(("ixgb_alloc_dma_mem($%p, %ld, $%p, 0x%x, $%p)",
	    (void *)ixgbp, memsize, attr_p, dma_flags, dma_p));

	/*
	 * Allocate handle
	 */
	err = ddi_dma_alloc_handle(ixgbp->devinfo, &dma_attr,
	    DDI_DMA_SLEEP, NULL, &dma_p->dma_hdl);
	if (err != DDI_SUCCESS) {
		dma_p->dma_hdl = NULL;
		return (DDI_FAILURE);
	}

	/*
	 * Allocate memory
	 */
	err = ddi_dma_mem_alloc(dma_p->dma_hdl, memsize, attr_p,
	    dma_flags & (DDI_DMA_CONSISTENT | DDI_DMA_STREAMING),
	    DDI_DMA_SLEEP, NULL, &va, &dma_p->alength, &dma_p->acc_hdl);
	if (err != DDI_SUCCESS) {
		dma_p->alength = 0;
		dma_p->acc_hdl = NULL;
		ddi_dma_free_handle(&dma_p->dma_hdl);
		dma_p->dma_hdl = NULL;
		return (DDI_FAILURE);
	}

	/*
	 * Bind the two together
	 */
	dma_p->mem_va = va;
	err = ddi_dma_addr_bind_handle(dma_p->dma_hdl, NULL,
	    va, dma_p->alength, dma_flags, DDI_DMA_SLEEP, NULL,
	    &dma_p->cookie, &dma_p->ncookies);

	IXGB_DEBUG(("ixgb_alloc_dma_mem(): bind %d bytes; err %d, %d cookies",
	    dma_p->alength, err, dma_p->ncookies));

	if (err != DDI_DMA_MAPPED || dma_p->ncookies != 1) {
		ddi_dma_mem_free(&dma_p->acc_hdl);
		dma_p->alength = 0;
		dma_p->acc_hdl = NULL;
		dma_p->mem_va = NULL;
		ddi_dma_free_handle(&dma_p->dma_hdl);
		dma_p->dma_hdl = NULL;
		return (DDI_FAILURE);
	}

	dma_p->nslots = ~0U;
	dma_p->size = ~0U;
	dma_p->offset = 0;

	return (DDI_SUCCESS);
}

/*
 * Free one allocated area of DMAable memory
 */
static void
ixgb_free_dma_mem(dma_area_t *dma_p)
{
	if (dma_p->dma_hdl != NULL) {
		if (dma_p->ncookies) {
			(void) ddi_dma_unbind_handle(dma_p->dma_hdl);
			dma_p->ncookies = 0;
		}
		ddi_dma_free_handle(&dma_p->dma_hdl);
		dma_p->dma_hdl = NULL;
	}

	if (dma_p->acc_hdl != NULL) {
		ddi_dma_mem_free(&dma_p->acc_hdl);
		dma_p->acc_hdl = NULL;
	}
}

int
ixgb_alloc_bufs(ixgb_t *ixgbp)
{
	size_t rxdescsize;
	size_t txdescsize;
	int err;

	IXGB_TRACE(("ixgb_alloc_bufs($%p)", (void *)ixgbp));

	/*
	 * Allocate memory & handles for RX descriptor ring
	 */
	rxdescsize = IXGB_RECV_SLOTS_USED * sizeof (ixgb_rbd_t);
	err = ixgb_alloc_dma_mem(ixgbp, rxdescsize, &ixgb_desc_accattr,
	    DDI_DMA_RDWR | DDI_DMA_CONSISTENT, &ixgbp->recv->desc);
	if (err != DDI_SUCCESS)
		return (DDI_FAILURE);

	/*
	 * Allocate memory & handles for TX descriptor ring
	 */
	txdescsize = IXGB_SEND_SLOTS_USED * sizeof (ixgb_sbd_t);
	err = ixgb_alloc_dma_mem(ixgbp, txdescsize, &ixgb_desc_accattr,
	    DDI_DMA_RDWR | DDI_DMA_CONSISTENT, &ixgbp->send->desc);
	if (err != DDI_SUCCESS)
		return (DDI_FAILURE);

	return (DDI_SUCCESS);
}

/*
 * This routine frees the transmit and receive buffers and descriptors.
 * Make sure the chip is stopped before calling it!
 */
void
ixgb_free_bufs(ixgb_t *ixgbp)
{
	IXGB_TRACE(("ixgb_free_bufs($%p)", (void *)ixgbp));

	ixgb_free_dma_mem(&ixgbp->recv->desc);
	ixgb_free_dma_mem(&ixgbp->send->desc);
}

/*
 * Clean up initialisation done for Send Ring
 */
static void
ixgb_fini_send_ring(ixgb_t *ixgbp)
{
	send_ring_t *srp;
	sw_sbd_t *ssbdp;
	uint32_t slot;

	IXGB_TRACE(("ixgb_fini_send_ring($%p, %d)", (void *)ixgbp));

	srp = ixgbp->send;
	for (slot = 0; slot < IXGB_TX_HANDLES; ++slot) {
		if (srp->dma_handle_txbuf[slot]) {
			ddi_dma_free_handle(&srp->dma_handle_txbuf[slot]);
			srp->dma_handle_txbuf[slot] = NULL;
		}
	}

	ssbdp = srp->sw_sbds;
	for (slot = 0; slot < srp->desc.nslots; ++slot) {
		ixgb_free_dma_mem(&ssbdp->pbuf);
		ssbdp++;
	}
	kmem_free(srp->sw_sbds, srp->desc.nslots*sizeof (*ssbdp));
	kmem_free(srp->txhdl_head, IXGB_TX_HANDLES*sizeof (*srp->txhdl_head));
	srp->sw_sbds = NULL;
	srp->txhdl_head = NULL;
}

/*
 * Initialise the specified Send Ring, using the information in the
 * <dma_area> descriptors that it contains to set up all the other
 * fields. This routine should be called only once for each ring.
 */
static int
ixgb_init_send_ring(ixgb_t *ixgbp)
{
	uint32_t nslots;
	uint32_t slot;
	send_ring_t *srp;
	sw_sbd_t *ssbdp;
	dma_area_t desc;
	ixgb_queue_item_t *txhdl_head;
	int err;

	IXGB_TRACE(("ixgb_init_send_ring($%p)", (void *)ixgbp));

	srp = ixgbp->send;
	srp->tx_ring = DMA_VPTR(srp->desc);
	srp->desc.nslots = IXGB_SEND_SLOTS_USED;
	nslots = srp->desc.nslots;

	/*
	 * Other one-off initialisation of per-ring data
	 */
	srp->ixgbp = ixgbp;

	/*
	 * Allocate the array of s/w Send Buffer Descriptors
	 */
	ssbdp = kmem_zalloc(nslots*sizeof (*ssbdp), KM_SLEEP);
	txhdl_head =
	    kmem_zalloc(IXGB_TX_HANDLES*sizeof (*txhdl_head), KM_SLEEP);
	srp->sw_sbds = ssbdp;
	srp->txhdl_head = txhdl_head;

	desc = srp->desc;
	ssbdp = srp->sw_sbds;
	for (slot = 0; slot < nslots; ++ssbdp, ++slot) {
		ixgb_slice_chunk(&ssbdp->desc, &desc, 1,
		    sizeof (ixgb_sbd_t));
	}
	ASSERT(desc.alength == 0);

	/*
	 * Allocate memory & handles for TX buffers
	 */
	ssbdp = srp->sw_sbds;
	for (slot = 0; slot < nslots; ++ssbdp, ++slot) {
		err = ixgb_alloc_dma_mem(ixgbp, ixgbp->buf_size,
		    &ixgb_data_accattr, DDI_DMA_WRITE | IXGB_DMA_MODE,
		    &ssbdp->pbuf);
		if (err != DDI_SUCCESS) {
			ixgb_error(ixgbp, "ixgb_init_send_ring:"
			    " alloc tx buffer failed");
			ixgb_fini_send_ring(ixgbp);
			return (DDI_FAILURE);
		}
	}

	for (slot = 0; slot < IXGB_TX_HANDLES; ++slot) {
		err = ddi_dma_alloc_handle(ixgbp->devinfo,
		    &tx_dma_attr, DDI_DMA_DONTWAIT, NULL,
		    &srp->dma_handle_txbuf[slot]);
		if (err != DDI_SUCCESS) {
			ixgb_error(ixgbp, "ixgb_init_send_ring:"
		    " alloc dma handle failed");
			ixgb_fini_send_ring(ixgbp);
			return (DDI_FAILURE);
		}
	}

	return (DDI_SUCCESS);
}

/*
 * Intialize the tx recycle pointer and tx sending pointer of tx ring
 * and setting the type of tx's descriptor data descriptor by default.
 */
static void
ixgb_reinit_send_ring(ixgb_t *ixgbp)
{
	uint32_t slot;
	ixgb_sbd_t *hw_sbd_p;
	send_ring_t *srp;
	ixgb_queue_t *txhdl_queue;
	ixgb_queue_item_t *txhdl_head;
	ixgbp->watchdog = 0;
	srp = ixgbp->send;

	/*
	 * Reinitialise control variables ...
	 */
	srp->ether_header_size = 0;
	srp->start_offset = 0;
	srp->stuff_offset = 0;
	srp->sum_flags = 0;
	srp->lso_flags = 0;
	srp->mss = 0;
	srp->hdr_len = 0;
	srp->tx_free = srp->desc.nslots;
	srp->tx_next = 0;
	srp->tc_next = 0;
	srp->tx_flow = 0;

	/*
	 * Zero and sync all the h/w Send Buffer Descriptors
	 */
	DMA_ZERO(srp->desc);
	hw_sbd_p = srp->tx_ring;
	for (slot = 0; slot < IXGB_SEND_SLOTS_USED; ++slot) {
		hw_sbd_p->len_cmd = IXGB_TBD_TYPE;
		hw_sbd_p++;
	}
	DMA_SYNC(srp->desc, DDI_DMA_SYNC_FORDEV);

	/*
	 * Initialize the tx dma handle push queue
	 */
	txhdl_queue = &srp->freetxhdl_queue;
	txhdl_queue->head = NULL;
	txhdl_queue->count = 0;
	txhdl_queue->lock = srp->freetxhdl_lock;
	srp->txhdl_push_queue = txhdl_queue;

	/*
	 * Initialize the tx dma handle pop queue
	 */
	txhdl_queue = &srp->txhdl_queue;
	txhdl_queue->head = NULL;
	txhdl_queue->count = 0;
	txhdl_queue->lock = srp->txhdl_lock;
	srp->txhdl_pop_queue = txhdl_queue;
	txhdl_head = srp->txhdl_head;
	for (slot = 0; slot < IXGB_TX_HANDLES; ++slot) {
		txhdl_head->item = &srp->dma_handle_txbuf[slot];
		IXGB_QUEUE_PUSH(txhdl_queue, txhdl_head);
		txhdl_head++;
	}
}

/*
 * Fintialize the rx recycle pointer and rx sending pointer of rx ring
 */
static void
ixgb_fini_recv_ring(ixgb_t *ixgbp)
{
	recv_ring_t *rrp;
	sw_rbd_t *srbdp;
	uint32_t slot;

	rrp = ixgbp->recv;
	rrp->rx_tail = 0;
	srbdp = rrp->sw_rbds;

	for (slot = 0; slot < IXGB_RECV_SLOTS_USED; ++srbdp, ++slot) {
		if (srbdp->bufp) {
			if (srbdp->bufp->mp != NULL) {
				freemsg(srbdp->bufp->mp);
				srbdp->bufp->mp = NULL;
			}
			ixgb_free_dma_mem(srbdp->bufp);
			kmem_free(srbdp->bufp, sizeof (dma_area_t));
			srbdp->bufp = NULL;
		}
	}
	kmem_free(rrp->sw_rbds, IXGB_RECV_SLOTS_USED * sizeof (*srbdp));
}

/*
 * Initialize the slot number of rx's ring
 */
static int
ixgb_init_recv_ring(ixgb_t *ixgbp)
{
	recv_ring_t *rrp;
	sw_rbd_t *srbdp;
	dma_area_t desc;
	uint32_t nslots;
	uint32_t slot;
	int err;

	IXGB_TRACE(("ixgb_init_recv_ring($%p)", (void *)ixgbp));

	rrp = ixgbp->recv;
	rrp->rx_ring = DMA_VPTR(rrp->desc);
	rrp->desc.nslots = IXGB_RECV_SLOTS_USED;
	nslots = rrp->desc.nslots;

	/*
	 * Other one-off initialisation of per-ring data
	 */
	rrp->ixgbp = ixgbp;

	/*
	 * Allocate the array of s/w Receive Buffer Descriptors
	 */
	srbdp = kmem_zalloc(nslots*sizeof (*srbdp), KM_SLEEP);
	rrp->sw_rbds = srbdp;

	/*
	 * Now initialise each array element once and for all
	 */
	desc = rrp->desc;
	for (slot = 0; slot < nslots; ++slot) {
		ixgb_slice_chunk(&srbdp->desc, &desc, 1,
		    sizeof (ixgb_rbd_t));
		srbdp->bufp =
		    kmem_zalloc(sizeof (dma_area_t), KM_SLEEP);
		err = ixgb_alloc_dma_mem(ixgbp, ixgbp->buf_size,
		    &ixgb_data_accattr,
		    DDI_DMA_READ | IXGB_DMA_MODE, srbdp->bufp);
		if (err != DDI_SUCCESS) {
			ixgb_fini_recv_ring(ixgbp);
			return (DDI_FAILURE);
		}

		srbdp->bufp->alength -= IXGB_HEADROOM;
		srbdp->bufp->offset += IXGB_HEADROOM;
		srbdp->bufp->rx_recycle.free_func = ixgb_rx_recycle;
		srbdp->bufp->rx_recycle.free_arg = (caddr_t)srbdp->bufp;
		srbdp->bufp->private = (caddr_t)ixgbp;
		srbdp->bufp->mp = desballoc(DMA_VPTR(*srbdp->bufp),
		    ixgbp->buf_size,
		    0, &srbdp->bufp->rx_recycle);
		if (srbdp->bufp->mp == NULL) {
			ixgb_fini_recv_ring(ixgbp);
			return (DDI_FAILURE);
		}
		srbdp++;
	}
	ASSERT(desc.alength == 0);

	return (DDI_SUCCESS);
}

/*
 * Intialize the rx recycle pointer and rx sending pointer of rx ring
 */
static void
ixgb_reinit_recv_ring(ixgb_t *ixgbp)
{
	recv_ring_t *rrp;
	ixgb_rbd_t *hw_rbd_p;
	sw_rbd_t *srbdp;
	uint32_t slot;

	/*
	 * Reinitialise control variables ...
	 */
	rrp = ixgbp->recv;
	rrp->rx_tail = rrp->desc.nslots - 1;
	rrp->rx_next = 0;

	/*
	 * Zero and sync all the h/w Receive Buffer Descriptors
	 */
	DMA_ZERO(rrp->desc);
	hw_rbd_p = rrp->rx_ring;
	srbdp = rrp->sw_rbds;
	for (slot = 0; slot < IXGB_RECV_SLOTS_USED; ++slot) {
		hw_rbd_p->host_buf_addr =
		    srbdp->bufp->cookie.dmac_laddress + IXGB_HEADROOM;
		srbdp++;
		hw_rbd_p++;
	}
	DMA_SYNC(rrp->desc, DDI_DMA_SYNC_FORDEV);
}

/*
 * Clean up initialisation done for buffer ring
 */
static void
ixgb_fini_buff_ring(ixgb_t *ixgbp)
{
	buff_ring_t *brp;
	sw_rbd_t *free_srbdp;
	uint32_t slot;

	IXGB_TRACE(("ixgb_fini_buff_ring($%p, %d)", (void *)ixgbp));

	brp = ixgbp->buff;
	free_srbdp =  brp->free_srbds;

	for (slot = 0; slot < IXGB_RECV_SLOTS_BUFF; ++free_srbdp, ++slot) {
		if (free_srbdp->bufp) {
			if (free_srbdp->bufp->mp != NULL) {
				freemsg(free_srbdp->bufp->mp);
				free_srbdp->bufp->mp = NULL;
			}
			ixgb_free_dma_mem(free_srbdp->bufp);
			kmem_free(free_srbdp->bufp, sizeof (dma_area_t));
			free_srbdp->bufp = NULL;
		}
	}
	kmem_free(brp->free_srbds, IXGB_RECV_SLOTS_BUFF * sizeof (sw_rbd_t));
}

/*
 * Intialize the Rx's data ring and free ring
 */
static int
ixgb_init_buff_ring(ixgb_t *ixgbp)
{
	buff_ring_t *brp;
	sw_rbd_t *free_srbdp;
	uint32_t nslots;
	uint32_t slot;
	int err;

	IXGB_TRACE(("ixgb_init_buff_ring($%p)", (void *)ixgbp));

	brp = ixgbp->buff;
	brp->nslots = IXGB_RECV_SLOTS_BUFF;
	brp->rx_free = IXGB_RECV_SLOTS_BUFF;
	brp->rx_bcopy = B_FALSE;
	brp->rc_next = 0;
	brp->rfree_next = 0;

	/*
	 * Allocate the array of s/w Free Receive Buffers
	 */
	nslots = brp->nslots;
	free_srbdp = kmem_zalloc(nslots * sizeof (*free_srbdp), KM_SLEEP);
	brp->free_srbds = free_srbdp;
	brp->ixgbp = ixgbp;

	/*
	 * Now initialise each array element once and for all
	 */
	for (slot = 0; slot < nslots; ++free_srbdp, ++slot) {
		free_srbdp->bufp =
		    kmem_zalloc(sizeof (dma_area_t), KM_SLEEP);
		err = ixgb_alloc_dma_mem(ixgbp, ixgbp->buf_size,
		    &ixgb_data_accattr,
		    DDI_DMA_READ | IXGB_DMA_MODE, free_srbdp->bufp);
		if (err != DDI_SUCCESS) {
			ixgb_fini_buff_ring(ixgbp);
			return (DDI_FAILURE);
		}

		free_srbdp->bufp->alength -= IXGB_HEADROOM;
		free_srbdp->bufp->offset += IXGB_HEADROOM;
		free_srbdp->bufp->private = (caddr_t)ixgbp;
		free_srbdp->bufp->rx_recycle.free_func =
		    ixgb_rx_recycle;
		free_srbdp->bufp->rx_recycle.free_arg =
		    (caddr_t)free_srbdp->bufp;

		free_srbdp->bufp->mp = desballoc(
		    DMA_VPTR(*free_srbdp->bufp),
		    ixgbp->buf_size,
		    0, &free_srbdp->bufp->rx_recycle);
		if (free_srbdp->bufp->mp == NULL) {
			ixgb_fini_buff_ring(ixgbp);
			return (DDI_FAILURE);
		}
	}

	return (DDI_SUCCESS);
}

/*
 * Filling the host address of data in rx' descriptor
 * and Initialize free pointers of rx free ring.
 */
static void
ixgb_reinit_buff_ring(ixgb_t *ixgbp)
{
	buff_ring_t *brp;

	brp = ixgbp->buff;
	if (brp->rx_free != IXGB_RECV_SLOTS_BUFF)
		brp->rx_bcopy = B_TRUE;
}

int
ixgb_init_rings(ixgb_t *ixgbp)
{
	uint32_t err;

	err = ixgb_init_send_ring(ixgbp);
	if (err != DDI_SUCCESS) {
		return (IXGB_FAILURE);
	}

	err = ixgb_init_recv_ring(ixgbp);
	if (err != DDI_SUCCESS) {
		ixgb_fini_send_ring(ixgbp);
		return (IXGB_FAILURE);
	}

	err = ixgb_init_buff_ring(ixgbp);
	if (err != DDI_SUCCESS) {
		ixgb_fini_recv_ring(ixgbp);
		ixgb_fini_send_ring(ixgbp);
		return (IXGB_FAILURE);
	}

	return (IXGB_SUCCESS);
}

void
ixgb_reinit_rings(ixgb_t *ixgbp)
{
	ixgb_reinit_recv_ring(ixgbp);
	ixgb_reinit_buff_ring(ixgbp);
	ixgb_reinit_send_ring(ixgbp);
}

void
ixgb_fini_rings(ixgb_t *ixgbp)
{
	ixgb_fini_buff_ring(ixgbp);
	ixgb_fini_recv_ring(ixgbp);
	ixgb_fini_send_ring(ixgbp);
}

/*
 * Determine (initial) MAC address ("BIA") to use for this interface
 */
int
ixgb_find_mac_address(ixgb_t *ixgbp, chip_info_t *infop)
{
	char propbuf[8];		/* "true" or "false", plus NUL	*/
	uchar_t *bytes;
	int *ints;
	uint_t nelts;
	int err;
	struct ether_addr sysaddr;

	IXGB_TRACE(("ixgb_find_mac_address($%p)", (void *)ixgbp));

	IXGB_DEBUG(("ixgb_find_mac_address: "
	    "hw_mac_addr %012llx, => %s (%sset)",
	    infop->hw_mac_addr,
	    ether_sprintf((void *)infop->vendor_addr.addr),
	    infop->vendor_addr.set ? "" : "not "));

	/*
	 * The "vendor's factory-set address" may already have
	 * been extracted from the chip, but if the property
	 * "local-mac-address" is set we use that instead.  It
	 * will normally be set by OBP, but it could also be
	 * specified in a .conf file(!)
	 *
	 * There doesn't seem to be a way to define byte-array
	 * properties in a .conf, so we check whether it looks
	 * like an array of 6 ints instead.
	 *
	 * Then, we check whether it looks like an array of 6
	 * bytes (which it should, if OBP set it).  If we can't
	 * make sense of it either way, we'll ignore it.
	 */
	err = ddi_prop_lookup_int_array(DDI_DEV_T_ANY, ixgbp->devinfo,
	    DDI_PROP_DONTPASS, localmac_propname, &ints, &nelts);
	if (err == DDI_PROP_SUCCESS) {
		if (nelts == ETHERADDRL) {
			while (nelts--)
				infop->vendor_addr.addr[nelts] = ints[nelts];
			infop->vendor_addr.set = 1;
		}
		ddi_prop_free(ints);
	}

	err = ddi_prop_lookup_byte_array(DDI_DEV_T_ANY, ixgbp->devinfo,
	    DDI_PROP_DONTPASS, localmac_propname, &bytes, &nelts);
	if (err == DDI_PROP_SUCCESS) {
		if (nelts == ETHERADDRL) {
			while (nelts--)
				infop->vendor_addr.addr[nelts] = bytes[nelts];
			infop->vendor_addr.set = 1;
		}
		ddi_prop_free(bytes);
	}

	IXGB_DEBUG(("ixgb_find_mac_address: +local %s (%sset)",
	    ether_sprintf((void *)infop->vendor_addr.addr),
	    infop->vendor_addr.set ? "" : "not "));

	/*
	 * Look up the OBP property "local-mac-address?".  Note that even
	 * though its value is a string (which should be "true" or "false"),
	 * it can't be decoded by ddi_prop_lookup_string(9F).  So, we zero
	 * the buffer first and then fetch the property as an untyped array;
	 * this may or may not include a final NUL, but since there will
	 * always be one left at the end of the buffer we can now treat it
	 * as a string anyway.
	 */
	nelts = sizeof (propbuf);
	bzero(propbuf, nelts--);

	err = ddi_getlongprop_buf(DDI_DEV_T_ANY, ixgbp->devinfo,
	    DDI_PROP_CANSLEEP, localmac_boolname, propbuf, (int *)&nelts);

	/*
	 * Now, if the address still isn't set from the hardware (SEEPROM)
	 * or the OBP or .conf property, OR if the user has foolishly set
	 * 'local-mac-address? = false', use "the system address" instead
	 * (but only if it's non-null i.e. has been set from the IDPROM).
	 */
	if (err == DDI_PROP_SUCCESS) {
		if (infop->vendor_addr.set == 0 ||
		    strcmp(propbuf, "false") == 0) {
			if (localetheraddr(NULL, &sysaddr) != 0) {
				ethaddr_copy(&sysaddr, infop->vendor_addr.addr);
				infop->vendor_addr.set = 1;
			}
		}
	}
	IXGB_DEBUG(("ixgb_find_mac_address: +system %s (%sset)",
	    ether_sprintf((void *)infop->vendor_addr.addr),
	    infop->vendor_addr.set ? "" : "not "));

	/*
	 * Finally(!), if there's a valid "mac-address" property (created
	 * if we netbooted from this interface), we must use this instead
	 * of any of the above to ensure that the NFS/install server doesn't
	 * get confused by the address changing as Solaris takes over!
	 */
	err = ddi_prop_lookup_byte_array(DDI_DEV_T_ANY, ixgbp->devinfo,
	    DDI_PROP_DONTPASS, macaddr_propname, &bytes, &nelts);
	if (err == DDI_PROP_SUCCESS) {
		if (nelts == ETHERADDRL) {
			while (nelts--)
				infop->vendor_addr.addr[nelts] = bytes[nelts];
			infop->vendor_addr.set = 1;
		}
		ddi_prop_free(bytes);
	}

	IXGB_DEBUG(("ixgb_find_mac_address: =final %s (%sset)",
	    ether_sprintf((void *)infop->vendor_addr.addr),
	    infop->vendor_addr.set ? "" : "not "));

	return (IXGB_SUCCESS);
}

/*
 * These routines provide all the functionality required by the
 * corresponding MAC entry points, but don't update the MAC state
 * so they can be called internally without disturbing our record
 * of what GLD thinks we should be doing ...
 */

/*
 *	ixgb_reset() -- reset h/w & rings to initial state
 */
int
ixgb_reset(ixgb_t *ixgbp)
{
	buff_ring_t *brp;
	int i;

	ixgb_tx_recycle_all(ixgbp);

	/*
	 * Wait for posted buffer to be freed...
	 */
	brp = ixgbp->buff;
	if (!brp->rx_bcopy) {
		for (i = 0; i < 1000; i++) {
			if (brp->rx_free == IXGB_RECV_SLOTS_BUFF)
				break;
			drv_usecwait(1000);
			IXGB_DEBUG(("ixgb_reset: waiting for rx buf free."));
		}
	}

	return (ixgb_chip_reset(ixgbp));
}

/*
 *	ixgb_stop() -- stop processing, don't reset h/w or rings
 */
void
ixgb_stop(ixgb_t *ixgbp)
{
	ASSERT(mutex_owned(ixgbp->genlock));

	ixgb_chip_stop(ixgbp, B_FALSE);

	IXGB_DEBUG(("ixgb_stop($%p) done", (void *)ixgbp));
}

/*
 *	ixgb_start() -- start transmitting/receiving
 */
void
ixgb_start(ixgb_t *ixgbp)
{
	ASSERT(mutex_owned(ixgbp->genlock));

	ixgb_reinit_rings(ixgbp);

	/*
	 * Start chip processing, including enabling interrupts
	 */
	ixgb_chip_start(ixgbp);

	IXGB_DEBUG(("ixgb_start($%p, %d) done", (void *)ixgbp));
}

/*
 * ixgb_restart - restart transmitting/receiving after error
 */
void
ixgb_restart(ixgb_t *ixgbp)
{
	ASSERT(mutex_owned(ixgbp->genlock));
	mutex_enter(ixgbp->recv->rx_lock);
	rw_enter(ixgbp->errlock, RW_WRITER);
	mutex_enter(ixgbp->send->tc_lock);

	(void) ixgb_reset(ixgbp);

	if (ixgbp->ixgb_mac_state == IXGB_MAC_STARTED) {
		ixgb_start(ixgbp);
		ddi_trigger_softintr(ixgbp->resched_id);
	}
	ixgbp->chip_reset++;

	mutex_exit(ixgbp->send->tc_lock);
	rw_exit(ixgbp->errlock);
	mutex_exit(ixgbp->recv->rx_lock);
	IXGB_DEBUG(("ixgb_restart($%p, %d) done", (void *)ixgbp));
}

void
ixgb_wake_factotum(ixgb_t *ixgbp)
{
	mutex_enter(ixgbp->softintr_lock);
	if (ixgbp->factotum_flag == 0) {
		ixgbp->factotum_flag = 1;
		ddi_trigger_softintr(ixgbp->factotum_id);
	}
	mutex_exit(ixgbp->softintr_lock);
}

/*
 * High-level cyclic handler
 *
 * This routine schedules a (low-level) softint callback to the
 * factotum.
 */

void
ixgb_chip_cyclic(void *arg)
{
	ixgb_t *ixgbp;

	ixgbp = (ixgb_t *)arg;


	switch (ixgbp->ixgb_chip_state) {
	default:
		return;

	case IXGB_CHIP_RUNNING:
		break;

	case IXGB_CHIP_FAULT:
	case IXGB_CHIP_ERROR:
		break;
	}

	ixgb_wake_factotum(ixgbp);
}

#undef	IXGB_DBG
#define	IXGB_DBG	IXGB_DBG_IOCTL	/* debug flag for this code	*/

static lb_property_t loopmodes[] = {
	{ normal,	"normal",	IXGB_LOOP_NONE		},
	{ external,	"XAUI",		IXGB_LOOP_EXTERNAL_XAUI	},
	{ external,	"XGMII",	IXGB_LOOP_EXTERNAL_XGMII},
	{ internal,	"XGMII",	IXGB_LOOP_INTERNAL_XGMII}
};

enum ioc_reply
ixgb_loop_ioctl(ixgb_t *ixgbp, queue_t *wq, mblk_t *mp, struct iocblk *iocp)
{
	lb_info_sz_t	*lbsp;
	lb_property_t	*lbpp;
	uint32_t	*lbmp;
	int		cmd;

	_NOTE(ARGUNUSED(wq))

	/*
	 * Validate format of ioctl
	 */
	if (mp->b_cont == NULL)
		return (IOC_INVAL);

	cmd = iocp->ioc_cmd;

	switch (cmd) {
	default:
		/* NOTREACHED */
		ixgb_error(ixgbp, "ixgb_loop_ioctl: invalid cmd 0x%x", cmd);
		return (IOC_INVAL);

	case LB_GET_INFO_SIZE:
		if (iocp->ioc_count != sizeof (lb_info_sz_t))
			return (IOC_INVAL);
		lbsp = (lb_info_sz_t *)mp->b_cont->b_rptr;
		*lbsp = sizeof (loopmodes);
		return (IOC_REPLY);

	case LB_GET_INFO:
		if (iocp->ioc_count != sizeof (loopmodes))
			return (IOC_INVAL);
		lbpp = (lb_property_t *)mp->b_cont->b_rptr;
		bcopy(loopmodes, lbpp, sizeof (loopmodes));
		return (IOC_REPLY);

	case LB_GET_MODE:
		if (iocp->ioc_count != sizeof (uint32_t))
			return (IOC_INVAL);
		lbmp = (uint32_t *)mp->b_cont->b_rptr;
		*lbmp = ixgbp->param_loop_mode;
		return (IOC_REPLY);

	case LB_SET_MODE:
		if (iocp->ioc_count != sizeof (uint32_t))
			return (IOC_INVAL);
		lbmp = (uint32_t *)mp->b_cont->b_rptr;
		return (ixgb_set_loop_mode(ixgbp, *lbmp));
	}
}

/*
 * Compute the index of the required bit in the multicast hash map.
 * This must mirror the way the hardware actually does it!
 * Please refer to intel's 82507ex PDM page 134 and page 147
 */
uint16_t
ixgb_mca_hash_index(ixgb_t *ixgbp, const uint8_t *mca)
{
	uint16_t hash;
	uint32_t filter = ixgbp->mcast_filter;

	hash = 0;

	switch (filter) {
	default:
		IXGB_DEBUG(("errored fileter type0x%x", filter));
		break;

	case FILTER_TYPE0:
		hash = ((mca[4] >> 4) | (((uint16_t)mca[5]) << 4));
		break;

	case FILTER_TYPE1:
		hash = ((mca[4] >> 3) | (((uint16_t)mca[5] & 0x7f) << 5));
		break;

	case FILTER_TYPE2:
		hash = ((mca[4] >> 2) | (((uint16_t)mca[5] & 0x3f) << 6));
		break;

	case FILTER_TYPE3:
		hash = ((mca[4]) | (((uint16_t)mca[5] & 0xf) << 8));
		break;
	}

	return (hash);
}
