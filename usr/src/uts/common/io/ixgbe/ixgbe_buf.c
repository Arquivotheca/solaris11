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
 * Copyright(c) 2007-2011 Intel Corporation. All rights reserved.
 */

/*
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include "ixgbe_sw.h"

/*
 * The size of ddp buffer used for FCoE LRO/DDP is 4K physical continuous,
 * which is different from the normal rx buffer. The ddp ring size is
 * 512, which is also different from that of normal rx ring, and there is
 * only one ddp ring for ixgbe. Such buffer is a new requirement for buffer
 * management framework (see RFE7034226 for more details). So currently ddp
 * buffer will be allocated with traditional way.
 */

static int ixgbe_alloc_tbd_ring(ixgbe_tx_ring_t *);
static void ixgbe_free_tbd_ring(ixgbe_tx_ring_t *);
static int ixgbe_alloc_rbd_ring(ixgbe_rx_data_t *);
static void ixgbe_free_rbd_ring(ixgbe_rx_data_t *);
static int ixgbe_alloc_dma_buffer(ixgbe_t *, dma_buffer_t *, size_t);
static int ixgbe_alloc_tcb_lists(ixgbe_tx_ring_t *);
static void ixgbe_free_tcb_lists(ixgbe_tx_ring_t *);
static int ixgbe_alloc_rcb_lists(ixgbe_rx_data_t *);
static void ixgbe_free_rcb_lists(ixgbe_rx_data_t *);
static int ixgbe_alloc_ddp_ubd_list(ixgbe_fcoe_ddp_buf_t *);
static void ixgbe_free_ddp_ubd_list(ixgbe_fcoe_ddp_buf_t *);
static int ixgbe_alloc_ddp_buf_lists(ixgbe_rx_fcoe_t *);
static void ixgbe_free_ddp_buf_lists(ixgbe_rx_fcoe_t *);

#ifdef __sparc
#define	IXGBE_DMA_ALIGNMENT	0x0000000000002000ull
#else
#define	IXGBE_DMA_ALIGNMENT	0x0000000000001000ull
#endif

/*
 * With 1500 MTU sized packets, 2K packet alignment is sufficient.
 * Packing 4 packets in a sparc 8K page economizes on DVMA space use.
 */
#define	IXGBE_DMA_ALIGNMENT_2K	0x0000000000000800ull

/*
 * DMA attributes for tx/rx descriptors.
 */
ddi_dma_attr_t ixgbe_desc_dma_attr = {
	DMA_ATTR_V0,			/* version number */
	0x0000000000000000ull,		/* low address */
	0xFFFFFFFFFFFFFFFFull,		/* high address */
	0x00000000FFFFFFFFull,		/* dma counter max */
	IXGBE_DMA_ALIGNMENT,		/* alignment */
	0x00000FFF,			/* burst sizes */
	0x00000001,			/* minimum transfer size */
	0x00000000FFFFFFFFull,		/* maximum transfer size */
	0xFFFFFFFFFFFFFFFFull,		/* maximum segment size */
	1,				/* scatter/gather list length */
	0x00000001,			/* granularity */
	DDI_DMA_FLAGERR			/* DMA flags */
};

/*
 * DMA attributes for Tx/Rx (jumbogram) buffers.
 */
ddi_dma_attr_t ixgbe_buf_dma_attr = {
	DMA_ATTR_V0,			/* version number */
	0x0000000000000000ull,		/* low address */
	0xFFFFFFFFFFFFFFFFull,		/* high address */
	0x00000000FFFFFFFFull,		/* dma counter max */
	IXGBE_DMA_ALIGNMENT,		/* alignment */
	0x00000FFF,			/* burst sizes */
	0x00000001,			/* minimum transfer size */
	0x00000000FFFFFFFFull,		/* maximum transfer size */
	0xFFFFFFFFFFFFFFFFull,		/* maximum segment size	 */
	1,				/* scatter/gather list length */
	0x00000001,			/* granularity */
	DDI_DMA_FLAGERR			/* DMA flags */
};

/*
 * DMA attributes for Tx/Rx (1500 MTU) size buffers.
 */
ddi_dma_attr_t ixgbe_buf2k_dma_attr = {
	DMA_ATTR_V0,			/* version number */
	0x0000000000000000ull,		/* low address */
	0xFFFFFFFFFFFFFFFFull,		/* high address */
	0x00000000FFFFFFFFull,		/* dma counter max */
	IXGBE_DMA_ALIGNMENT_2K,		/* alignment */
	0x00000FFF,			/* burst sizes */
	0x00000001,			/* minimum transfer size */
	0x00000000FFFFFFFFull,		/* maximum transfer size */
	0xFFFFFFFFFFFFFFFFull,		/* maximum segment size	 */
	1,				/* scatter/gather list length */
	0x00000001,			/* granularity */
	DDI_DMA_FLAGERR			/* DMA flags */
};

/*
 * DMA attributes for transmit.
 */
ddi_dma_attr_t ixgbe_tx_buf_dma_attr = {
	DMA_ATTR_V0,			/* version number */
	0x0000000000000000ull,		/* low address */
	0xFFFFFFFFFFFFFFFFull,		/* high address */
	0x00000000FFFFFFFFull,		/* dma counter max */
	1,				/* alignment */
	0x00000FFF,			/* burst sizes */
	0x00000001,			/* minimum transfer size */
	0x00000000FFFFFFFFull,		/* maximum transfer size */
	0xFFFFFFFFFFFFFFFFull,		/* maximum segment size	 */
	MAX_COOKIE,			/* scatter/gather list length */
	0x00000001,			/* granularity */
	DDI_DMA_FLAGERR			/* DMA flags */
};

/*
 * DMA access attributes for descriptors.
 */
ddi_device_acc_attr_t ixgbe_desc_acc_attr = {
	DDI_DEVICE_ATTR_V0,
	DDI_STRUCTURE_LE_ACC,
	DDI_STRICTORDER_ACC
};

/*
 * DMA access attributes for buffers.
 */
ddi_device_acc_attr_t ixgbe_buf_acc_attr = {
	DDI_DEVICE_ATTR_V0,
	DDI_NEVERSWAP_ACC,
	DDI_STRICTORDER_ACC
};

/*
 * ixgbe_alloc_dma - Allocate DMA resources for all rx/tx rings.
 */
int
ixgbe_alloc_dma(ixgbe_t *ixgbe)
{
	ixgbe_rx_ring_t	*rx_ring;
	ixgbe_rx_data_t *rx_data;
	ixgbe_rx_fcoe_t *rx_fcoe;
	ixgbe_tx_ring_t *tx_ring;
	int i;

	for (i = 0; i < ixgbe->num_rx_rings; i++) {
		/*
		 * Allocate receive desciptor ring and control block lists
		 */
		rx_ring = &ixgbe->rx_rings[i];
		rx_data = rx_ring->rx_data;

		if (ixgbe_alloc_rbd_ring(rx_data) != IXGBE_SUCCESS) {
			ixgbe_error(ixgbe, "ixgbe_alloc_rbd_ring failed");
			goto alloc_dma_failure;
		}

		if (ixgbe_alloc_rcb_lists(rx_data) != IXGBE_SUCCESS) {
			ixgbe_error(ixgbe, "ixgbe_alloc_rcb_ring failed");
			goto alloc_dma_failure;
		}
	}

	if (ixgbe->fcoe_lro_enable) {
		rx_fcoe = ixgbe->rx_fcoe;
		if (ixgbe_alloc_ddp_buf_lists(rx_fcoe) != IXGBE_SUCCESS)
			goto alloc_dma_failure;
	}

	for (i = 0; i < ixgbe->num_tx_rings; i++) {
		/*
		 * Allocate transmit desciptor ring and control block lists
		 */
		tx_ring = &ixgbe->tx_rings[i];

		if (ixgbe_alloc_tbd_ring(tx_ring) != IXGBE_SUCCESS)
			goto alloc_dma_failure;

		if (ixgbe_alloc_tcb_lists(tx_ring) != IXGBE_SUCCESS)
			goto alloc_dma_failure;
	}

	return (IXGBE_SUCCESS);

alloc_dma_failure:
	ixgbe_free_dma(ixgbe);
	return (IXGBE_FAILURE);
}

/*
 * ixgbe_free_dma - Free all the DMA resources of all rx/tx rings.
 */
void
ixgbe_free_dma(ixgbe_t *ixgbe)
{
	ixgbe_rx_ring_t *rx_ring;
	ixgbe_rx_data_t *rx_data;
	ixgbe_rx_fcoe_t *rx_fcoe;
	ixgbe_tx_ring_t *tx_ring;
	int i;

	/*
	 * Free DMA resources of rx rings
	 */
	for (i = 0; i < ixgbe->num_rx_rings; i++) {
		rx_ring = &ixgbe->rx_rings[i];
		rx_data = rx_ring->rx_data;

		ixgbe_free_rbd_ring(rx_data);
		ixgbe_free_rcb_lists(rx_data);
	}

	if (ixgbe->fcoe_lro_enable) {
		rx_fcoe = ixgbe->rx_fcoe;
		ixgbe_free_ddp_buf_lists(rx_fcoe);
	}

	/*
	 * Free DMA resources of tx rings
	 */
	for (i = 0; i < ixgbe->num_tx_rings; i++) {
		tx_ring = &ixgbe->tx_rings[i];
		ixgbe_free_tbd_ring(tx_ring);
		ixgbe_free_tcb_lists(tx_ring);
	}
}

int
ixgbe_alloc_rx_ring_data(ixgbe_rx_ring_t *rx_ring)
{
	ixgbe_rx_data_t	*rx_data;
	ixgbe_t *ixgbe = rx_ring->ixgbe;
	uint32_t rcb_count;

	/*
	 * Allocate memory for software receive rings
	 */
	rx_data = kmem_zalloc(sizeof (ixgbe_rx_data_t), KM_NOSLEEP);
	if (rx_data == NULL) {
		ixgbe_error(ixgbe, "Allocate software receive rings failed");
		return (IXGBE_FAILURE);
	}

	rx_data->rx_ring = rx_ring;
	rx_data->ring_size = ixgbe->rx_ring_size;

	/*
	 * Allocate memory for the work list.
	 */
	rx_data->work_list = kmem_zalloc(sizeof (rx_control_block_t *) *
	    rx_data->ring_size, KM_NOSLEEP);

	if (rx_data->work_list == NULL) {
		ixgbe_error(ixgbe,
		    "Could not allocate memory for rx work list");
		goto alloc_rx_data_failure;
	}

	/*
	 * Allocate memory for the rx control blocks for work list.
	 */
	rcb_count = rx_data->ring_size;
	rx_data->rcb_area = kmem_zalloc(
	    sizeof (rx_control_block_t) * rcb_count, KM_NOSLEEP);
	if (rx_data->rcb_area == NULL) {
		ixgbe_error(ixgbe,
		    "Cound not allocate memory for rx control blocks");
		goto alloc_rx_data_failure;
	}

	rx_ring->rx_data = rx_data;
	return (IXGBE_SUCCESS);

alloc_rx_data_failure:
	ixgbe_free_rx_ring_data(rx_data);
	return (IXGBE_FAILURE);
}

int
ixgbe_alloc_rx_fcoe_data(ixgbe_t *ixgbe)
{
	ixgbe_rx_fcoe_t *rx_fcoe;
	uint32_t ddp_buf_count;

	/*
	 * Allocate memory for software fcoe ring
	 */
	rx_fcoe = kmem_zalloc(sizeof (ixgbe_rx_fcoe_t), KM_NOSLEEP);

	if (rx_fcoe == NULL) {
		ixgbe_error(ixgbe, "Allocate software fcoe ring failed");
		return (IXGBE_FAILURE);
	}

	rx_fcoe->ixgbe = ixgbe;
	mutex_init(&rx_fcoe->recycle_lock, NULL,
	    MUTEX_DRIVER, DDI_INTR_PRI(ixgbe->intr_pri));

	rx_fcoe->ddp_ring_size = IXGBE_DDP_RING_SIZE;
	rx_fcoe->free_list_size = IXGBE_DDP_RING_SIZE;

	rx_fcoe->ddp_buf_head = 0;
	rx_fcoe->ddp_buf_tail = 0;
	rx_fcoe->ddp_buf_free = rx_fcoe->free_list_size;

	/*
	 * Allocate memory for the ddp buf work list.
	 */
	rx_fcoe->work_list = kmem_zalloc(sizeof (ixgbe_fcoe_ddp_buf_t *) *
	    rx_fcoe->ddp_ring_size, KM_NOSLEEP);

	if (rx_fcoe->work_list == NULL) {
		ixgbe_error(ixgbe,
		    "Could not allocate memory for ddp buf work list");
		goto alloc_rx_fcoe_data_failure;
	}

	/*
	 * Allocate memory for the ddp buf free list.
	 */
	rx_fcoe->free_list = kmem_zalloc(sizeof (ixgbe_fcoe_ddp_buf_t *) *
	    rx_fcoe->free_list_size, KM_NOSLEEP);

	if (rx_fcoe->free_list == NULL) {
		ixgbe_error(ixgbe,
		    "Cound not allocate memory for ddp buf free list");
		goto alloc_rx_fcoe_data_failure;
	}

	/*
	 * Allocate memory for the ddp buffers for work list and
	 * free list.
	 */
	ddp_buf_count = rx_fcoe->ddp_ring_size + rx_fcoe->free_list_size;
	rx_fcoe->ddp_buf_area =
	    kmem_zalloc(sizeof (ixgbe_fcoe_ddp_buf_t) * ddp_buf_count,
	    KM_NOSLEEP);

	if (rx_fcoe->ddp_buf_area == NULL) {
		ixgbe_error(ixgbe,
		    "Cound not allocate memory for ddp buffers");
		goto alloc_rx_fcoe_data_failure;
	}

	ixgbe->rx_fcoe = rx_fcoe;
	return (IXGBE_SUCCESS);

alloc_rx_fcoe_data_failure:
	ixgbe_free_rx_fcoe_data(rx_fcoe);
	return (IXGBE_FAILURE);
}

void
ixgbe_free_rx_fcoe_data(ixgbe_rx_fcoe_t *rx_fcoe)
{
	uint32_t ddp_buf_count;

	if (rx_fcoe == NULL)
		return;

	ASSERT(rx_fcoe->ddp_buf_pending == 0);

	ddp_buf_count = rx_fcoe->ddp_ring_size + rx_fcoe->free_list_size;
	if (rx_fcoe->ddp_buf_area != NULL) {
		kmem_free(rx_fcoe->ddp_buf_area,
		    sizeof (ixgbe_fcoe_ddp_buf_t) * ddp_buf_count);
		rx_fcoe->ddp_buf_area = NULL;
	}

	if (rx_fcoe->work_list != NULL) {
		kmem_free(rx_fcoe->work_list,
		    sizeof (ixgbe_fcoe_ddp_buf_t *) * rx_fcoe->ddp_ring_size);
		rx_fcoe->work_list = NULL;
	}

	if (rx_fcoe->free_list != NULL) {
		kmem_free(rx_fcoe->free_list,
		    sizeof (ixgbe_fcoe_ddp_buf_t *) * rx_fcoe->free_list_size);
		rx_fcoe->free_list = NULL;
	}

	mutex_destroy(&rx_fcoe->recycle_lock);
	kmem_free(rx_fcoe, sizeof (ixgbe_rx_fcoe_t));
}

void
ixgbe_free_rx_ring_data(ixgbe_rx_data_t *rx_data)
{
	uint32_t rcb_count;

	if (rx_data == NULL)
		return;

	rcb_count = rx_data->ring_size;
	if (rx_data->rcb_area != NULL) {
		kmem_free(rx_data->rcb_area,
		    sizeof (rx_control_block_t) * rcb_count);
		rx_data->rcb_area = NULL;
	}

	if (rx_data->work_list != NULL) {
		kmem_free(rx_data->work_list,
		    sizeof (rx_control_block_t *) * rx_data->ring_size);
		rx_data->work_list = NULL;
	}

	kmem_free(rx_data, sizeof (ixgbe_rx_data_t));
}

/*
 * ixgbe_alloc_tbd_ring - Memory allocation for the tx descriptors of one ring.
 */
static int
ixgbe_alloc_tbd_ring(ixgbe_tx_ring_t *tx_ring)
{
	mac_descriptor_handle_t	 mdh;
	struct ixgbe		*ixgbe = tx_ring->ixgbe;

	mdh = mac_descriptors_get(ixgbe->mac_hdl, tx_ring->ring_handle,
	    &tx_ring->ring_size);
	if (mdh == NULL) {
		ixgbe_error(ixgbe,
		    "ixgbe_alloc_tbd_ring: mac_descriptors_get() failed\n");
		return (IXGBE_FAILURE);
	}

	tx_ring->tbd_mdh = mdh;
	if (ixgbe->tx_head_wb_enable)
		tx_ring->ring_size--;
	tx_ring->free_list_size = tx_ring->ring_size;
	tx_ring->tbd_area.address = mac_descriptors_address_get(mdh);
	tx_ring->tbd_area.dma_handle = mac_descriptors_dma_handle_get(mdh);
	tx_ring->tbd_area.dma_address = mac_descriptors_ioaddress_get(mdh);
	tx_ring->tbd_area.size = mac_descriptors_length_get(mdh);
	tx_ring->tbd_area.len = mac_descriptors_length_get(mdh);
	tx_ring->tbd_ring = (union ixgbe_adv_tx_desc *)(uintptr_t)
	    tx_ring->tbd_area.address;

	return (IXGBE_SUCCESS);
}

/*
 * ixgbe_free_tbd_ring - Free the tx descriptors of one ring.
 */
static void
ixgbe_free_tbd_ring(ixgbe_tx_ring_t *tx_ring)
{
	ASSERT(tx_ring != NULL);

	tx_ring->tbd_mdh = NULL;
	tx_ring->tbd_area.address = NULL;
	tx_ring->tbd_area.dma_address = NULL;
	tx_ring->tbd_area.size = 0;
	tx_ring->tbd_ring = NULL;
}

/*
 * ixgbe_alloc_rbd_ring - Memory allocation for the rx descriptors of one ring.
 */
static int
ixgbe_alloc_rbd_ring(ixgbe_rx_data_t *rx_data)
{
	mac_descriptor_handle_t	mdh;
	struct ixgbe		*ixgbe = rx_data->rx_ring->ixgbe;

	ASSERT(rx_data != NULL);
	ASSERT(rx_data->rx_ring->ring_handle != NULL);

	/*
	 * Get the descriptor memory from the MAC layer.
	 */
	mdh = mac_descriptors_get(ixgbe->mac_hdl,
	    rx_data->rx_ring->ring_handle, &rx_data->ring_size);
	if (mdh == NULL) {
		ixgbe_error(ixgbe, "mac_descriptors_get() for RX failed\n");
		return (IXGBE_FAILURE);
	}

	rx_data->rbd_mdh = mdh;
	rx_data->rbd_area.address = mac_descriptors_address_get(mdh);
	rx_data->rbd_area.dma_handle = mac_descriptors_dma_handle_get(mdh);
	rx_data->rbd_area.dma_address = mac_descriptors_ioaddress_get(mdh);
	rx_data->rbd_area.size = mac_descriptors_length_get(mdh);
	rx_data->rbd_area.len = mac_descriptors_length_get(mdh);
	rx_data->rbd_ring = (union ixgbe_adv_rx_desc *)(uintptr_t)
	    rx_data->rbd_area.address;

	return (IXGBE_SUCCESS);
}

/*
 * ixgbe_free_rbd_ring - Free the rx descriptors of one ring.
 */
static void
ixgbe_free_rbd_ring(ixgbe_rx_data_t *rx_data)
{
	ASSERT(rx_data != NULL);
	ASSERT(rx_data->rx_ring != NULL);
	ASSERT(rx_data->rbd_mdh != NULL);

	rx_data->rbd_mdh = NULL;
	rx_data->rbd_area.address = NULL;
	rx_data->rbd_area.dma_address = NULL;
	rx_data->rbd_area.size = 0;
	rx_data->rbd_ring = NULL;
}

/*
 * ixgbe_alloc_dma_buffer - Allocate DMA resources for a DMA buffer.
 */
static int
ixgbe_alloc_dma_buffer(ixgbe_t *ixgbe, dma_buffer_t *buf, size_t size)
{
	int ret;
	dev_info_t *devinfo = ixgbe->dip;
	ddi_dma_cookie_t cookie;
	size_t len;
	uint_t cookie_num;

	ret = ddi_dma_alloc_handle(devinfo,
	    &ixgbe_buf_dma_attr, DDI_DMA_DONTWAIT,
	    NULL, &buf->dma_handle);

	if (ret != DDI_SUCCESS) {
		buf->dma_handle = NULL;
		ixgbe_error(ixgbe,
		    "Could not allocate dma buffer handle: %x", ret);
		return (IXGBE_FAILURE);
	}

	ret = ddi_dma_mem_alloc(buf->dma_handle,
	    size, &ixgbe_buf_acc_attr, DDI_DMA_STREAMING,
	    DDI_DMA_DONTWAIT, NULL, &buf->address,
	    &len, &buf->acc_handle);

	if (ret != DDI_SUCCESS) {
		buf->acc_handle = NULL;
		buf->address = NULL;
		if (buf->dma_handle != NULL) {
			ddi_dma_free_handle(&buf->dma_handle);
			buf->dma_handle = NULL;
		}
		ixgbe_error(ixgbe,
		    "Could not allocate dma buffer memory: %x", ret);
		return (IXGBE_FAILURE);
	}

	ret = ddi_dma_addr_bind_handle(buf->dma_handle, NULL,
	    buf->address,
	    len, DDI_DMA_RDWR | DDI_DMA_STREAMING,
	    DDI_DMA_DONTWAIT, NULL, &cookie, &cookie_num);

	if (ret != DDI_DMA_MAPPED) {
		buf->dma_address = NULL;
		if (buf->acc_handle != NULL) {
			ddi_dma_mem_free(&buf->acc_handle);
			buf->acc_handle = NULL;
			buf->address = NULL;
		}
		if (buf->dma_handle != NULL) {
			ddi_dma_free_handle(&buf->dma_handle);
			buf->dma_handle = NULL;
		}
		ixgbe_error(ixgbe,
		    "Could not bind dma buffer handle: %x", ret);
		return (IXGBE_FAILURE);
	}

	ASSERT(cookie_num == 1);

	buf->dma_address = cookie.dmac_laddress;
	buf->size = len;
	buf->len = 0;

	return (IXGBE_SUCCESS);
}

/*
 * ixgbe_free_dma_buffer - Free one allocated area of dma memory and handle.
 */
void
ixgbe_free_dma_buffer(dma_buffer_t *buf)
{
	if (buf->dma_handle != NULL) {
		(void) ddi_dma_unbind_handle(buf->dma_handle);
		buf->dma_address = NULL;
	} else {
		return;
	}

	if (buf->acc_handle != NULL) {
		ddi_dma_mem_free(&buf->acc_handle);
		buf->acc_handle = NULL;
		buf->address = NULL;
	}

	if (buf->dma_handle != NULL) {
		ddi_dma_free_handle(&buf->dma_handle);
		buf->dma_handle = NULL;
	}

	buf->size = 0;
	buf->len = 0;
}

/*
 * ixgbe_alloc_tcb_lists - Memory allocation for the transmit control bolcks
 * of one ring.
 */
static int
ixgbe_alloc_tcb_lists(ixgbe_tx_ring_t *tx_ring)
{
	int			i, ret;
	tx_control_block_t	*tcb;
	dma_buffer_t		*tx_buf;
	ixgbe_t			*ixgbe = tx_ring->ixgbe;
	dev_info_t		*devinfo = ixgbe->dip;
	int	cnt = 1;
	mblk_t	*tail;

	/*
	 * Allocate memory for the work list.
	 */
	tx_ring->work_list = kmem_zalloc(sizeof (tx_control_block_t *) *
	    tx_ring->ring_size, KM_NOSLEEP);

	if (tx_ring->work_list == NULL) {
		ixgbe_error(ixgbe,
		    "Cound not allocate memory for tx work list");
		return (IXGBE_FAILURE);
	}

	/*
	 * Allocate memory for the free list.
	 */
	tx_ring->free_list = kmem_zalloc(sizeof (tx_control_block_t *) *
	    tx_ring->free_list_size, KM_NOSLEEP);

	if (tx_ring->free_list == NULL) {
		kmem_free(tx_ring->work_list,
		    sizeof (tx_control_block_t *) * tx_ring->ring_size);
		tx_ring->work_list = NULL;

		ixgbe_error(ixgbe,
		    "Cound not allocate memory for tx free list");
		return (IXGBE_FAILURE);
	}

	/*
	 * Allocate memory for the tx control blocks of free list.
	 */
	tx_ring->tcb_area =
	    kmem_zalloc(sizeof (tx_control_block_t) *
	    tx_ring->free_list_size, KM_NOSLEEP);

	if (tx_ring->tcb_area == NULL) {
		kmem_free(tx_ring->work_list,
		    sizeof (tx_control_block_t *) * tx_ring->ring_size);
		tx_ring->work_list = NULL;

		kmem_free(tx_ring->free_list,
		    sizeof (tx_control_block_t *) * tx_ring->free_list_size);
		tx_ring->free_list = NULL;

		ixgbe_error(ixgbe,
		    "Cound not allocate memory for tx control blocks");
		return (IXGBE_FAILURE);
	}

	/*
	 * Allocate dma memory for the tx control block of free list.
	 */
	tcb = tx_ring->tcb_area;
	for (i = 0; i < tx_ring->free_list_size; i++, tcb++) {
		ASSERT(tcb != NULL);

		tx_ring->free_list[i] = tcb;

		/*
		 * Pre-allocate dma handles for transmit. These dma handles
		 * will be dynamically bound to the data buffers passed down
		 * from the upper layers at the time of transmitting.
		 */

		ret = ddi_dma_alloc_handle(devinfo, &ixgbe_tx_buf_dma_attr,
		    DDI_DMA_DONTWAIT, NULL, &tcb->tx_dma_handle);
		if (ret != DDI_SUCCESS) {
			tcb->tx_dma_handle = NULL;
			ixgbe_error(ixgbe,
			    "Could not allocate tx dma handle: %x", ret);
			goto alloc_tcb_lists_fail;
		}

		/*
		 * Pre-allocate DMA buffers for bcopy packets.
		 */
		tcb->tx_mp = mac_mblk_get(ixgbe->mac_hdl,
		    tx_ring->ring_handle, &tail, &cnt);
		if (tcb->tx_mp == NULL) {
			ixgbe_error(ixgbe,
			    "mac_mblk_get() for TX bcopy packet failed\n");
			goto alloc_tcb_lists_fail;
		}

		tx_buf = &tcb->tx_buf;
		mac_mblk_info_get(ixgbe->mac_hdl,
		    tcb->tx_mp, &tx_buf->dma_handle, &tx_buf->dma_address,
		    &tx_buf->size);
		tx_buf->address = (char *)tcb->tx_mp->b_rptr;
		tx_buf->len = 0;
		tcb->last_index = MAX_TX_RING_SIZE;
		tcb->mp = NULL;
	}

	return (IXGBE_SUCCESS);

alloc_tcb_lists_fail:
	ixgbe_free_tcb_lists(tx_ring);

	return (IXGBE_FAILURE);
}

/*
 * ixgbe_free_tcb_lists - Release the memory allocated for
 * the transmit control bolcks of one ring.
 */
static void
ixgbe_free_tcb_lists(ixgbe_tx_ring_t *tx_ring)
{
	int			i;
	tx_control_block_t	*tcb;

	tcb = tx_ring->tcb_area;
	if (tcb == NULL)
		return;

	for (i = 0; i < tx_ring->free_list_size; i++, tcb++) {
		ASSERT(tcb != NULL);

		/* Free the tx dma handle for dynamical binding */
		if (tcb->tx_dma_handle != NULL) {
			ddi_dma_free_handle(&tcb->tx_dma_handle);
			tcb->tx_dma_handle = NULL;
		} else {
			/*
			 * If the dma handle is NULL, then we don't
			 * have to check the remaining.
			 */
			break;
		}

		/*
		 * Free the bcopy mblk_t
		 */
		freemsg(tcb->tx_mp);
	}

	if (tx_ring->tcb_area != NULL) {
		kmem_free(tx_ring->tcb_area,
		    sizeof (tx_control_block_t) * tx_ring->free_list_size);
		tx_ring->tcb_area = NULL;
	}

	if (tx_ring->work_list != NULL) {
		kmem_free(tx_ring->work_list,
		    sizeof (tx_control_block_t *) * tx_ring->ring_size);
		tx_ring->work_list = NULL;
	}

	if (tx_ring->free_list != NULL) {
		kmem_free(tx_ring->free_list,
		    sizeof (tx_control_block_t *) * tx_ring->free_list_size);
		tx_ring->free_list = NULL;
	}
}

static void
ixgbe_rcb_fill(ixgbe_rx_data_t *rx_data, rx_control_block_t *rcb, mblk_t *mp)
{
	ixgbe_t			*ixgbe;
	dma_buffer_t		*rx_buf;

	ixgbe = rx_data->rx_ring->ixgbe;

	rcb->mp = mp;
	rx_buf = &rcb->rx_buf;
	rx_buf->address = (caddr_t)mp->b_rptr;
	mac_mblk_info_get(ixgbe->mac_hdl, mp, &rx_buf->dma_handle,
	    &rx_buf->dma_address, &rx_buf->size);
	if (ixgbe->lro_enable)
		rx_buf->size = ixgbe->rx_buf_size;
	rx_buf->len = 0;

	/*
	 * Fill in rest of the the header.
	 */
	rcb->rx_data = (ixgbe_rx_data_t *)rx_data;
	rcb->lro_prev = -1;
	rcb->lro_next = -1;
	rcb->lro_pkt = B_FALSE;
}

/*
 * ixgbe_alloc_rcb_lists - Memory allocation for the receive control blocks
 * of one ring.
 */
static int
ixgbe_alloc_rcb_lists(ixgbe_rx_data_t *rx_data)
{
	int			i;
	rx_control_block_t	*rcb;
	ixgbe_t			*ixgbe = rx_data->rx_ring->ixgbe;
	uint32_t		rcb_count;
	mblk_t			*mp;
	mblk_t			*tail;
	int			cnt = 1;

	/*
	 * Allocate memory for the rx control blocks for work list and
	 * free list.
	 */
	rcb_count = rx_data->ring_size;
	rcb = rx_data->rcb_area;

	/*
	 * Allocate dma memory for the rx control blocks
	 */
	for (i = 0; i < rcb_count; i++, rcb++) {
		ASSERT(rcb != NULL);

		/* Attach the rx control block to the work list */
		rx_data->work_list[i] = rcb;

		mp = mac_mblk_get(ixgbe->mac_hdl,
		    rx_data->rx_ring->ring_handle, &tail, &cnt);
		if (mp == NULL) {
			ixgbe_error(ixgbe,
			    "mac_packget_get() for RX packet failed\n");
			goto alloc_rcb_lists_fail;
		}

		ixgbe_rcb_fill(rx_data, rcb, mp);
	}

	return (IXGBE_SUCCESS);

alloc_rcb_lists_fail:
	ixgbe_free_rcb_lists(rx_data);
	return (IXGBE_FAILURE);
}

/*
 * ixgbe_free_rcb_lists - Free the receive control blocks of one ring.
 */
static void
ixgbe_free_rcb_lists(ixgbe_rx_data_t *rx_data)
{
	rx_control_block_t	*rcb;
	uint32_t		rcb_count;
	int			i;

	rcb = rx_data->rcb_area;
	rcb_count = rx_data->ring_size;

	for (i = 0; i < rcb_count; i++, rcb++) {
		ASSERT(rcb != NULL);

		/*
		 * NOTE: mac layer will handle inflight packets.
		 */
		if (rcb->mp != NULL) {
			freemsg(rcb->mp);
			rcb->mp = NULL;
		}
	}

	if (rx_data->mblk_head != NULL) {
		freemsgchain(rx_data->mblk_head);
		rx_data->mblk_head = NULL;
		rx_data->mblk_tail = NULL;
		rx_data->mblk_cnt = 0;
	}
}

/*
 * ixgbe_set_fma_flags - Set the attribute for fma support.
 */
void
ixgbe_set_fma_flags(int dma_flag)
{
	if (dma_flag) {
		ixgbe_tx_buf_dma_attr.dma_attr_flags = DDI_DMA_FLAGERR;
		ixgbe_buf_dma_attr.dma_attr_flags = DDI_DMA_FLAGERR;
		ixgbe_buf2k_dma_attr.dma_attr_flags = DDI_DMA_FLAGERR;
		ixgbe_desc_dma_attr.dma_attr_flags = DDI_DMA_FLAGERR;
	} else {
		ixgbe_tx_buf_dma_attr.dma_attr_flags = 0;
		ixgbe_buf_dma_attr.dma_attr_flags = 0;
		ixgbe_buf2k_dma_attr.dma_attr_flags = 0;
		ixgbe_desc_dma_attr.dma_attr_flags = 0;
	}
}

/*
 * ixgbe_alloc_ddp_ubd_list - Memory allocation for one ddp user
 * descriptors list.
 */
static int
ixgbe_alloc_ddp_ubd_list(ixgbe_fcoe_ddp_buf_t *ddp_buf)
{
	int ret;
	size_t size;
	size_t len;
	uint_t cookie_num;
	dev_info_t *devinfo;
	ddi_dma_cookie_t cookie;
	ixgbe_t *ixgbe = ddp_buf->rx_fcoe->ixgbe;

	devinfo = ixgbe->dip;
	size = sizeof (ixgbe_ddp_ubd_t) * IXGBE_DDP_UBD_COUNT;

	/*
	 * Allocate a new DMA handle for the ddp buffer descriptor
	 * memory area.
	 */
	ret = ddi_dma_alloc_handle(devinfo, &ixgbe_desc_dma_attr,
	    DDI_DMA_DONTWAIT, NULL,
	    &ddp_buf->ubd_area.dma_handle);

	if (ret != DDI_SUCCESS) {
		ixgbe_error(ixgbe,
		    "Could not allocate ubd dma handle: %x", ret);
		ddp_buf->ubd_area.dma_handle = NULL;
		return (IXGBE_FAILURE);
	}

	/*
	 * Allocate memory to DMA data to and from the ddp buffer
	 * descriptors.
	 */
	ret = ddi_dma_mem_alloc(ddp_buf->ubd_area.dma_handle,
	    size, &ixgbe_desc_acc_attr, DDI_DMA_CONSISTENT,
	    DDI_DMA_DONTWAIT, NULL,
	    (caddr_t *)&ddp_buf->ubd_area.address,
	    &len, &ddp_buf->ubd_area.acc_handle);

	if (ret != DDI_SUCCESS) {
		ixgbe_error(ixgbe,
		    "Could not allocate ubd dma memory: %x", ret);
		ddp_buf->ubd_area.acc_handle = NULL;
		ddp_buf->ubd_area.address = NULL;
		if (ddp_buf->ubd_area.dma_handle != NULL) {
			ddi_dma_free_handle(&ddp_buf->ubd_area.dma_handle);
			ddp_buf->ubd_area.dma_handle = NULL;
		}
		return (IXGBE_FAILURE);
	}

	/*
	 * Initialize the entire ddp buffer descriptor area to zero
	 */
	bzero(ddp_buf->ubd_area.address, len);

	/*
	 * Allocates DMA resources for the memory that was allocated by
	 * the ddi_dma_mem_alloc call.
	 */
	ret = ddi_dma_addr_bind_handle(ddp_buf->ubd_area.dma_handle,
	    NULL, (caddr_t)ddp_buf->ubd_area.address,
	    len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
	    DDI_DMA_DONTWAIT, NULL, &cookie, &cookie_num);

	if (ret != DDI_DMA_MAPPED) {
		ixgbe_error(ixgbe,
		    "Could not bind ubd dma resource: %x", ret);
		ddp_buf->ubd_area.dma_address = NULL;
		if (ddp_buf->ubd_area.acc_handle != NULL) {
			ddi_dma_mem_free(&ddp_buf->ubd_area.acc_handle);
			ddp_buf->ubd_area.acc_handle = NULL;
			ddp_buf->ubd_area.address = NULL;
		}
		if (ddp_buf->ubd_area.dma_handle != NULL) {
			ddi_dma_free_handle(&ddp_buf->ubd_area.dma_handle);
			ddp_buf->ubd_area.dma_handle = NULL;
		}
		return (IXGBE_FAILURE);
	}

	ASSERT(cookie_num == 1);

	ddp_buf->ubd_area.dma_address = cookie.dmac_laddress;
	ddp_buf->ubd_area.size = len;

	ddp_buf->ubd_list = (ixgbe_ddp_ubd_t *)(uintptr_t)
	    ddp_buf->ubd_area.address;

	return (IXGBE_SUCCESS);
}

/*
 * ixgbe_free_ddp_ubd_list - Free one ddp buf descriptors list.
 */
static void
ixgbe_free_ddp_ubd_list(ixgbe_fcoe_ddp_buf_t *ddp_buf)
{
	if (ddp_buf->ubd_area.dma_handle != NULL) {
		(void) ddi_dma_unbind_handle(ddp_buf->ubd_area.dma_handle);
	}
	if (ddp_buf->ubd_area.acc_handle != NULL) {
		ddi_dma_mem_free(&ddp_buf->ubd_area.acc_handle);
		ddp_buf->ubd_area.acc_handle = NULL;
	}
	if (ddp_buf->ubd_area.dma_handle != NULL) {
		ddi_dma_free_handle(&ddp_buf->ubd_area.dma_handle);
		ddp_buf->ubd_area.dma_handle = NULL;
	}
	ddp_buf->ubd_area.address = NULL;
	ddp_buf->ubd_area.dma_address = NULL;
	ddp_buf->ubd_area.size = 0;

	ddp_buf->ubd_list = NULL;
}

/*
 * ixgbe_alloc_ddp_buf_lists - Memory allocation for the fcoe ddp buffer
 * of fcoe ring.
 */
static int
ixgbe_alloc_ddp_buf_lists(ixgbe_rx_fcoe_t *rx_fcoe)
{
	int i, j;
	int ret;
	ixgbe_fcoe_ddp_buf_t *ddp_buf;
	ixgbe_t *ixgbe = rx_fcoe->ixgbe;
	dma_buffer_t *rx_buf;
	uint32_t ddp_buf_count;

	/*
	 * Allocate memory for the ddp buf for work list and
	 * free list.
	 */
	ddp_buf_count = rx_fcoe->ddp_ring_size + rx_fcoe->free_list_size;
	ddp_buf = rx_fcoe->ddp_buf_area;

	for (i = 0; i < ddp_buf_count; i++, ddp_buf++) {
		ASSERT(ddp_buf != NULL);

		if (i < rx_fcoe->ddp_ring_size) {
			/* Attach the ddp buffer to the work list */
			rx_fcoe->work_list[i] = ddp_buf;
		} else {
			/* Attach the ddp buffer to the free list */
			rx_fcoe->free_list[i - rx_fcoe->ddp_ring_size] =
			    ddp_buf;
		}

		ddp_buf->rx_fcoe = rx_fcoe;
		ret = ixgbe_alloc_ddp_ubd_list(ddp_buf);
		if (ret != IXGBE_SUCCESS) {
			ixgbe_error(ixgbe, "Allocate ddp ubd list failed");
			goto alloc_ddp_buf_lists_fail;
		}

		ddp_buf->free_rtn.free_func = ixgbe_ddp_buf_recycle;
		ddp_buf->free_rtn.free_arg = (char *)ddp_buf;
		for (j = 0; j < IXGBE_DDP_UBD_COUNT; j++) {
			rx_buf = &ddp_buf->rx_buf[j];
			ret = ixgbe_alloc_dma_buffer(ixgbe,
			    rx_buf, IXGBE_DDP_BUF_SIZE);

			if (ret != IXGBE_SUCCESS) {
				ixgbe_error(ixgbe,
				    "Allocate ddp dma buffer failed");
				goto alloc_ddp_buf_lists_fail;
			}

			ddp_buf->mp[j] = desballoc((unsigned char *)
			    rx_buf->address,
			    rx_buf->size,
			    0, &ddp_buf->free_rtn);

			ddp_buf->ubd_list[j].dma_address = rx_buf->dma_address;
		}

		ddp_buf->ref_cnt = 1;
		ddp_buf->rx_fcoe = (ixgbe_rx_fcoe_t *)rx_fcoe;
	}

	return (IXGBE_SUCCESS);

alloc_ddp_buf_lists_fail:
	ixgbe_free_ddp_buf_lists(rx_fcoe);

	return (IXGBE_FAILURE);
}

/*
 * ixgbe_free_ddp_buf_lists - Free the ddp buffer of fcoe ring.
 */
static void
ixgbe_free_ddp_buf_lists(ixgbe_rx_fcoe_t *rx_fcoe)
{
	ixgbe_t *ixgbe;
	ixgbe_fcoe_ddp_buf_t *ddp_buf;
	uint32_t ddp_buf_count;
	uint32_t ref_cnt;
	int i, j;

	ixgbe = rx_fcoe->ixgbe;

	mutex_enter(&ixgbe->rx_pending_lock);

	ddp_buf = rx_fcoe->ddp_buf_area;
	ddp_buf_count = rx_fcoe->ddp_ring_size + rx_fcoe->free_list_size;

	for (i = 0; i < ddp_buf_count; i++, ddp_buf++) {
		ASSERT(ddp_buf != NULL);
		ixgbe_free_ddp_ubd_list(ddp_buf);

		ref_cnt = atomic_dec_32_nv(&ddp_buf->ref_cnt);
		if (ref_cnt == 0) {
			for (j = 0; j < IXGBE_DDP_UBD_COUNT; j++) {
				if (ddp_buf->mp[j] != NULL) {
					freemsg(ddp_buf->mp[j]);
					ddp_buf->mp[j] = NULL;
				}
				ixgbe_free_dma_buffer(&ddp_buf->rx_buf[j]);
			}
		} else {
			atomic_inc_32(&rx_fcoe->ddp_buf_pending);
			atomic_inc_32(&ixgbe->ddp_buf_pending);
		}
	}

	mutex_exit(&ixgbe->rx_pending_lock);
}
