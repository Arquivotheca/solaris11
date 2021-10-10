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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include "igbvf_sw.h"

static int igbvf_alloc_tbd_ring(igbvf_tx_ring_t *);
static void igbvf_free_tbd_ring(igbvf_tx_ring_t *);
static int igbvf_alloc_rbd_ring(igbvf_rx_data_t *);
static void igbvf_free_rbd_ring(igbvf_rx_data_t *);
static int igbvf_alloc_dma_buffer(igbvf_t *, dma_buffer_t *, size_t);
static int igbvf_alloc_tcb_lists(igbvf_tx_ring_t *);
static void igbvf_free_tcb_lists(igbvf_tx_ring_t *);
static int igbvf_alloc_rcb_lists(igbvf_rx_data_t *);
static void igbvf_free_rcb_lists(igbvf_rx_data_t *);

#ifdef __sparc
#define	IGBVF_DMA_ALIGNMENT	0x0000000000002000ull
#else
#define	IGBVF_DMA_ALIGNMENT	0x0000000000001000ull
#endif

/*
 * DMA attributes for tx/rx descriptors
 */
static ddi_dma_attr_t igbvf_desc_dma_attr = {
	DMA_ATTR_V0,			/* version number */
	0x0000000000000000ull,		/* low address */
	0xFFFFFFFFFFFFFFFFull,		/* high address */
	0x00000000FFFFFFFFull,		/* dma counter max */
	IGBVF_DMA_ALIGNMENT,		/* alignment */
	0x00000FFF,			/* burst sizes */
	0x00000001,			/* minimum transfer size */
	0x00000000FFFFFFFFull,		/* maximum transfer size */
	0xFFFFFFFFFFFFFFFFull,		/* maximum segment size */
	1,				/* scatter/gather list length */
	0x00000001,			/* granularity */
	DDI_DMA_FLAGERR,		/* DMA flags */
};

/*
 * DMA attributes for tx/rx buffers
 */
static ddi_dma_attr_t igbvf_buf_dma_attr = {
	DMA_ATTR_V0,			/* version number */
	0x0000000000000000ull,		/* low address */
	0xFFFFFFFFFFFFFFFFull,		/* high address */
	0x00000000FFFFFFFFull,		/* dma counter max */
	IGBVF_DMA_ALIGNMENT,		/* alignment */
	0x00000FFF,			/* burst sizes */
	0x00000001,			/* minimum transfer size */
	0x00000000FFFFFFFFull,		/* maximum transfer size */
	0xFFFFFFFFFFFFFFFFull,		/* maximum segment size	 */
	1,				/* scatter/gather list length */
	0x00000001,			/* granularity */
	DDI_DMA_FLAGERR,		/* DMA flags */
};

/*
 * DMA attributes for transmit
 */
static ddi_dma_attr_t igbvf_tx_dma_attr = {
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
	DDI_DMA_FLAGERR,		/* DMA flags */
};

/*
 * DMA access attributes for descriptors.
 */
static ddi_device_acc_attr_t igbvf_desc_acc_attr = {
	DDI_DEVICE_ATTR_V0,
	DDI_STRUCTURE_LE_ACC,
	DDI_STRICTORDER_ACC
};

/*
 * DMA access attributes for buffers.
 */
static ddi_device_acc_attr_t igbvf_buf_acc_attr = {
	DDI_DEVICE_ATTR_V0,
	DDI_NEVERSWAP_ACC,
	DDI_STRICTORDER_ACC
};


/*
 * igbvf_alloc_dma - Allocate DMA resources for all rx/tx rings
 */
int
igbvf_alloc_dma(igbvf_t *igbvf)
{
	igbvf_rx_ring_t *rx_ring;
	igbvf_rx_data_t *rx_data;
	igbvf_tx_ring_t *tx_ring;
	int i;

	for (i = 0; i < igbvf->num_rx_rings; i++) {
		/*
		 * Allocate receive desciptor ring and control block lists
		 */
		rx_ring = &igbvf->rx_rings[i];
		rx_data = rx_ring->rx_data;

		if (igbvf_alloc_rbd_ring(rx_data) != IGBVF_SUCCESS)
			goto alloc_dma_failure;

		if (igbvf_alloc_rcb_lists(rx_data) != IGBVF_SUCCESS)
			goto alloc_dma_failure;
	}

	for (i = 0; i < igbvf->num_tx_rings; i++) {
		/*
		 * Allocate transmit desciptor ring and control block lists
		 */
		tx_ring = &igbvf->tx_rings[i];

		if (igbvf_alloc_tbd_ring(tx_ring) != IGBVF_SUCCESS)
			goto alloc_dma_failure;

		if (igbvf_alloc_tcb_lists(tx_ring) != IGBVF_SUCCESS)
			goto alloc_dma_failure;
	}

	return (IGBVF_SUCCESS);

alloc_dma_failure:
	igbvf_free_dma(igbvf);

	return (IGBVF_FAILURE);
}


/*
 * igbvf_free_dma - Free all the DMA resources of all rx/tx rings
 */
void
igbvf_free_dma(igbvf_t *igbvf)
{
	igbvf_rx_ring_t *rx_ring;
	igbvf_rx_data_t *rx_data;
	igbvf_tx_ring_t *tx_ring;
	int i;

	/*
	 * Free DMA resources of rx rings
	 */
	for (i = 0; i < igbvf->num_rx_rings; i++) {
		rx_ring = &igbvf->rx_rings[i];
		rx_data = rx_ring->rx_data;
		if (rx_data != NULL) {
			igbvf_free_rbd_ring(rx_data);
			igbvf_free_rcb_lists(rx_data);
		}
	}

	/*
	 * Free DMA resources of tx rings
	 */
	for (i = 0; i < igbvf->num_tx_rings; i++) {
		tx_ring = &igbvf->tx_rings[i];
		igbvf_free_tbd_ring(tx_ring);
		igbvf_free_tcb_lists(tx_ring);
	}
}

/*
 * igbvf_alloc_tbd_ring - Memory allocation for the tx descriptors of one ring.
 */
static int
igbvf_alloc_tbd_ring(igbvf_tx_ring_t *tx_ring)
{
	int ret;
	size_t size;
	size_t len;
	uint_t cookie_num;
	dev_info_t *devinfo;
	ddi_dma_cookie_t cookie;
	igbvf_t *igbvf = tx_ring->igbvf;

	if (!IGBVF_IS_STARTED(igbvf) ||
	    IGBVF_IS_SUSPENDED_DMA_FREE(igbvf)) {

		devinfo = igbvf->dip;
		size = sizeof (union e1000_adv_tx_desc) * tx_ring->ring_size;

		/*
		 * If tx head write-back is enabled, an extra tbd is allocated
		 * to save the head write-back value
		 */
		if (igbvf->tx_head_wb_enable) {
			size += sizeof (union e1000_adv_tx_desc);
		}

		/*
		 * Allocate a DMA handle for the transmit descriptor
		 * memory area.
		 */
		ret = ddi_dma_alloc_handle(devinfo, &igbvf_desc_dma_attr,
		    DDI_DMA_DONTWAIT, NULL,
		    &tx_ring->tbd_area.dma_handle);

		if (ret != DDI_SUCCESS) {
			igbvf_error(igbvf,
			    "Could not allocate tbd dma handle: %x", ret);
			tx_ring->tbd_area.dma_handle = NULL;

			return (IGBVF_FAILURE);
		}

		/*
		 * Allocate memory to DMA data to and from the transmit
		 * descriptors.
		 */
		ret = ddi_dma_mem_alloc(tx_ring->tbd_area.dma_handle,
		    size, &igbvf_desc_acc_attr, DDI_DMA_CONSISTENT,
		    DDI_DMA_DONTWAIT, NULL,
		    (caddr_t *)&tx_ring->tbd_area.address,
		    &len, &tx_ring->tbd_area.acc_handle);

		if (ret != DDI_SUCCESS) {
			igbvf_error(igbvf,
			    "Could not allocate tbd dma memory: %x", ret);
			tx_ring->tbd_area.acc_handle = NULL;
			tx_ring->tbd_area.address = NULL;
			if (tx_ring->tbd_area.dma_handle != NULL) {
				ddi_dma_free_handle(&tx_ring->
				    tbd_area.dma_handle);
				tx_ring->tbd_area.dma_handle = NULL;
			}
			return (IGBVF_FAILURE);
		}

		/*
		 * Initialize the entire transmit buffer descriptor area to zero
		 */
		bzero(tx_ring->tbd_area.address, len);

		tx_ring->tbd_area.size = len;
		tx_ring->tbd_ring = (union e1000_adv_tx_desc *)(uintptr_t)
		    tx_ring->tbd_area.address;
	}

	if (!IGBVF_IS_STARTED(igbvf) ||
	    IGBVF_IS_SUSPENDED_DMA_UNBIND(igbvf)) {
		/*
		 * Allocates DMA resources for the memory that was allocated by
		 * the ddi_dma_mem_alloc call. The DMA resources then get bound
		 * to the memory address
		 */
		len = tx_ring->tbd_area.size;
		ret = ddi_dma_addr_bind_handle(tx_ring->tbd_area.dma_handle,
		    NULL, (caddr_t)tx_ring->tbd_area.address,
		    len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
		    DDI_DMA_DONTWAIT, NULL, &cookie, &cookie_num);

		if (ret != DDI_DMA_MAPPED) {
			igbvf_error(igbvf,
			    "Could not bind tbd dma resource: %x", ret);
			tx_ring->tbd_area.dma_address = NULL;
			if (tx_ring->tbd_area.acc_handle != NULL) {
				ddi_dma_mem_free(&tx_ring->tbd_area.acc_handle);
				tx_ring->tbd_area.acc_handle = NULL;
				tx_ring->tbd_area.address = NULL;
			}
			if (tx_ring->tbd_area.dma_handle != NULL) {
				ddi_dma_free_handle(&tx_ring->
				    tbd_area.dma_handle);
				tx_ring->tbd_area.dma_handle = NULL;
			}
			return (IGBVF_FAILURE);
		}

		ASSERT(cookie_num == 1);

		tx_ring->tbd_area.dma_address = cookie.dmac_laddress;
	}

	return (IGBVF_SUCCESS);
}

/*
 * igbvf_free_tbd_ring - Free the tx descriptors of one ring.
 */
static void
igbvf_free_tbd_ring(igbvf_tx_ring_t *tx_ring)
{
	igbvf_t *igbvf = tx_ring->igbvf;

	if (IGBVF_IS_STARTED(igbvf) ==
	    IGBVF_IS_SUSPENDED_DMA_UNBIND(igbvf)) {
		if (tx_ring->tbd_area.dma_handle != NULL) {
			(void) ddi_dma_unbind_handle(tx_ring->
			    tbd_area.dma_handle);
		}
		tx_ring->tbd_area.dma_address = NULL;
	}

	if (IGBVF_IS_STARTED(igbvf) ==
	    IGBVF_IS_SUSPENDED_DMA_FREE(igbvf)) {
		if (tx_ring->tbd_area.acc_handle != NULL) {
			ddi_dma_mem_free(&tx_ring->tbd_area.acc_handle);
			tx_ring->tbd_area.acc_handle = NULL;
		}
		if (tx_ring->tbd_area.dma_handle != NULL) {
			ddi_dma_free_handle(&tx_ring->tbd_area.dma_handle);
			tx_ring->tbd_area.dma_handle = NULL;
		}
		tx_ring->tbd_area.address = NULL;
		tx_ring->tbd_area.size = 0;

		tx_ring->tbd_ring = NULL;
	}
}

int
igbvf_alloc_rx_ring_data(igbvf_rx_ring_t *rx_ring)
{
	igbvf_rx_data_t *rx_data;
	igbvf_t *igbvf = rx_ring->igbvf;
	uint32_t rcb_count;

	/*
	 * Allocate memory for software receive rings
	 */
	rx_data = kmem_zalloc(sizeof (igbvf_rx_data_t), KM_NOSLEEP);

	if (rx_data == NULL) {
		igbvf_error(igbvf, "Allocate software receive rings failed");
		return (IGBVF_FAILURE);
	}

	rx_data->rx_ring = rx_ring;
	mutex_init(&rx_data->recycle_lock, NULL,
	    MUTEX_DRIVER, DDI_INTR_PRI(igbvf->intr_pri));

	rx_data->ring_size = igbvf->rx_ring_size;
	rx_data->free_list_size = igbvf->rx_ring_size;

	rx_data->rcb_head = 0;
	rx_data->rcb_tail = 0;
	rx_data->rcb_free = rx_data->free_list_size;

	/*
	 * Allocate memory for the work list.
	 */
	rx_data->work_list = kmem_zalloc(sizeof (rx_control_block_t *) *
	    rx_data->ring_size, KM_NOSLEEP);

	if (rx_data->work_list == NULL) {
		igbvf_error(igbvf,
		    "Could not allocate memory for rx work list");
		goto alloc_rx_data_failure;
	}

	/*
	 * Allocate memory for the free list.
	 */
	rx_data->free_list = kmem_zalloc(sizeof (rx_control_block_t *) *
	    rx_data->free_list_size, KM_NOSLEEP);

	if (rx_data->free_list == NULL) {
		igbvf_error(igbvf,
		    "Cound not allocate memory for rx free list");
		goto alloc_rx_data_failure;
	}

	/*
	 * Allocate memory for the rx control blocks for work list and
	 * free list.
	 */
	rcb_count = rx_data->ring_size + rx_data->free_list_size;
	rx_data->rcb_area =
	    kmem_zalloc(sizeof (rx_control_block_t) * rcb_count,
	    KM_NOSLEEP);

	if (rx_data->rcb_area == NULL) {
		igbvf_error(igbvf,
		    "Cound not allocate memory for rx control blocks");
		goto alloc_rx_data_failure;
	}

	rx_ring->rx_data = rx_data;
	return (IGBVF_SUCCESS);

alloc_rx_data_failure:
	igbvf_free_rx_ring_data(rx_data);
	return (IGBVF_FAILURE);
}

void
igbvf_free_rx_ring_data(igbvf_rx_data_t *rx_data)
{
	rx_control_block_t *rcb;
	uint32_t i;
	uint32_t rcb_count;

	if (rx_data == NULL)
		return;

	ASSERT(rx_data->rcb_pending == 0);

	if (rx_data->rcb_area != NULL) {
		rcb_count = rx_data->ring_size + rx_data->free_list_size;
		rcb = rx_data->rcb_area;
		for (i = 0; i < rcb_count; i++, rcb++) {
			if (rcb->ref_cnt != 0)
				igbvf_free_dma_buffer(&rcb->rx_buf);
		}
		kmem_free(rx_data->rcb_area,
		    sizeof (rx_control_block_t) * rcb_count);
		rx_data->rcb_area = NULL;
	}

	if (rx_data->work_list != NULL) {
		kmem_free(rx_data->work_list,
		    sizeof (rx_control_block_t *) * rx_data->ring_size);
		rx_data->work_list = NULL;
	}

	if (rx_data->free_list != NULL) {
		kmem_free(rx_data->free_list,
		    sizeof (rx_control_block_t *) * rx_data->free_list_size);
		rx_data->free_list = NULL;
	}

	mutex_destroy(&rx_data->recycle_lock);
	kmem_free(rx_data, sizeof (igbvf_rx_data_t));
}

/*
 * igbvf_free_pending_rx_data - Free rx_data in the pending list
 */
void
igbvf_free_pending_rx_data(igbvf_t *igbvf)
{
	igbvf_rx_data_t *rx_data;

	mutex_enter(&igbvf->rx_pending_lock);

	while ((rx_data = igbvf->pending_rx_data) != NULL) {
		igbvf->pending_rx_data = rx_data->next;
		igbvf_free_rx_ring_data(rx_data);
	}

	mutex_exit(&igbvf->rx_pending_lock);
}

/*
 * igbvf_alloc_rbd_ring - Memory allocation for the rx descriptors of one ring.
 */
static int
igbvf_alloc_rbd_ring(igbvf_rx_data_t *rx_data)
{
	int ret;
	size_t size;
	size_t len;
	uint_t cookie_num;
	dev_info_t *devinfo;
	ddi_dma_cookie_t cookie;
	igbvf_t *igbvf = rx_data->rx_ring->igbvf;

	if (!IGBVF_IS_STARTED(igbvf) ||
	    IGBVF_IS_SUSPENDED_DMA_FREE(igbvf)) {

		devinfo = igbvf->dip;
		size = sizeof (union e1000_adv_rx_desc) * rx_data->ring_size;

		/*
		 * Allocate a new DMA handle for the receive descriptor
		 * memory area.
		 */
		ret = ddi_dma_alloc_handle(devinfo, &igbvf_desc_dma_attr,
		    DDI_DMA_DONTWAIT, NULL,
		    &rx_data->rbd_area.dma_handle);

		if (ret != DDI_SUCCESS) {
			igbvf_error(igbvf,
			    "Could not allocate rbd dma handle: %x", ret);
			rx_data->rbd_area.dma_handle = NULL;
			return (IGBVF_FAILURE);
		}

		/*
		 * Allocate memory to DMA data to and from the receive
		 * descriptors.
		 */
		ret = ddi_dma_mem_alloc(rx_data->rbd_area.dma_handle,
		    size, &igbvf_desc_acc_attr, DDI_DMA_CONSISTENT,
		    DDI_DMA_DONTWAIT, NULL,
		    (caddr_t *)&rx_data->rbd_area.address,
		    &len, &rx_data->rbd_area.acc_handle);

		if (ret != DDI_SUCCESS) {
			igbvf_error(igbvf,
			    "Could not allocate rbd dma memory: %x", ret);
			rx_data->rbd_area.acc_handle = NULL;
			rx_data->rbd_area.address = NULL;
			if (rx_data->rbd_area.dma_handle != NULL) {
				ddi_dma_free_handle(&rx_data->
				    rbd_area.dma_handle);
				rx_data->rbd_area.dma_handle = NULL;
			}
			return (IGBVF_FAILURE);
		}

		/*
		 * Initialize the entire transmit buffer descriptor area to zero
		 */
		bzero(rx_data->rbd_area.address, len);

		rx_data->rbd_area.size = len;
		rx_data->rbd_ring = (union e1000_adv_rx_desc *)(uintptr_t)
		    rx_data->rbd_area.address;
	}

	if (!IGBVF_IS_STARTED(igbvf) ||
	    IGBVF_IS_SUSPENDED_DMA_UNBIND(igbvf)) {
		/*
		 * Allocates DMA resources for the memory that was allocated by
		 * the ddi_dma_mem_alloc call.
		 */
		len = rx_data->rbd_area.size;
		ret = ddi_dma_addr_bind_handle(rx_data->rbd_area.dma_handle,
		    NULL, (caddr_t)rx_data->rbd_area.address,
		    len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
		    DDI_DMA_DONTWAIT, NULL, &cookie, &cookie_num);

		if (ret != DDI_DMA_MAPPED) {
			igbvf_error(igbvf,
			    "Could not bind rbd dma resource: %x", ret);
			rx_data->rbd_area.dma_address = NULL;
			if (rx_data->rbd_area.acc_handle != NULL) {
				ddi_dma_mem_free(&rx_data->rbd_area.acc_handle);
				rx_data->rbd_area.acc_handle = NULL;
				rx_data->rbd_area.address = NULL;
			}
			if (rx_data->rbd_area.dma_handle != NULL) {
				ddi_dma_free_handle(&rx_data->
				    rbd_area.dma_handle);
				rx_data->rbd_area.dma_handle = NULL;
			}
			return (IGBVF_FAILURE);
		}

		ASSERT(cookie_num == 1);

		rx_data->rbd_area.dma_address = cookie.dmac_laddress;
	}

	return (IGBVF_SUCCESS);
}

/*
 * igbvf_free_rbd_ring - Free the rx descriptors of one ring.
 */
static void
igbvf_free_rbd_ring(igbvf_rx_data_t *rx_data)
{
	igbvf_t *igbvf = rx_data->rx_ring->igbvf;

	if (IGBVF_IS_STARTED(igbvf) ==
	    IGBVF_IS_SUSPENDED_DMA_UNBIND(igbvf)) {
		if (rx_data->rbd_area.dma_handle != NULL) {
			(void) ddi_dma_unbind_handle(rx_data->
			    rbd_area.dma_handle);
		}
		rx_data->rbd_area.dma_address = NULL;
	}

	if (IGBVF_IS_STARTED(igbvf) ==
	    IGBVF_IS_SUSPENDED_DMA_FREE(igbvf)) {
		if (rx_data->rbd_area.acc_handle != NULL) {
			ddi_dma_mem_free(&rx_data->rbd_area.acc_handle);
			rx_data->rbd_area.acc_handle = NULL;
		}
		if (rx_data->rbd_area.dma_handle != NULL) {
			ddi_dma_free_handle(&rx_data->rbd_area.dma_handle);
			rx_data->rbd_area.dma_handle = NULL;
		}
		rx_data->rbd_area.address = NULL;
		rx_data->rbd_area.size = 0;

		rx_data->rbd_ring = NULL;
	}
}


/*
 * igbvf_alloc_dma_buffer - Allocate DMA resources for a DMA buffer
 */
static int
igbvf_alloc_dma_buffer(igbvf_t *igbvf,
    dma_buffer_t *buf, size_t size)
{
	int ret;
	dev_info_t *devinfo = igbvf->dip;

	ret = ddi_dma_alloc_handle(devinfo,
	    &igbvf_buf_dma_attr, DDI_DMA_DONTWAIT,
	    NULL, &buf->dma_handle);

	if (ret != DDI_SUCCESS) {
		buf->dma_handle = NULL;
		igbvf_error(igbvf,
		    "Could not allocate dma buffer handle: %x", ret);
		return (IGBVF_FAILURE);
	}

	ret = ddi_dma_mem_alloc(buf->dma_handle,
	    size, &igbvf_buf_acc_attr, DDI_DMA_STREAMING,
	    DDI_DMA_DONTWAIT, NULL, &buf->address,
	    &buf->size, &buf->acc_handle);

	if (ret != DDI_SUCCESS) {
		buf->acc_handle = NULL;
		buf->address = NULL;
		buf->size = 0;
		if (buf->dma_handle != NULL) {
			ddi_dma_free_handle(&buf->dma_handle);
			buf->dma_handle = NULL;
		}
		igbvf_error(igbvf,
		    "Could not allocate dma buffer memory: %x", ret);
		return (IGBVF_FAILURE);
	}

	buf->len = 0;

	return (IGBVF_SUCCESS);
}

/*
 * igbvf_bind_dma_buffer - Bind DMA resources for a DMA buffer
 */
static int
igbvf_bind_dma_buffer(dma_buffer_t *buf)
{
	int ret;
	ddi_dma_cookie_t cookie;
	uint_t cookie_num;

	ret = ddi_dma_addr_bind_handle(buf->dma_handle, NULL,
	    buf->address,
	    buf->size, DDI_DMA_RDWR | DDI_DMA_STREAMING,
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
		return (IGBVF_FAILURE);
	}

	ASSERT(cookie_num == 1);

	buf->dma_address = cookie.dmac_laddress;

	return (IGBVF_SUCCESS);
}

/*
 * igbvf_unbind_dma_buffer - Un-bind one allocated area of dma memory and handle
 */
void
igbvf_unbind_dma_buffer(dma_buffer_t *buf)
{
	if (buf->dma_handle == NULL)
		return;

	(void) ddi_dma_unbind_handle(buf->dma_handle);
	buf->dma_address = NULL;
}

/*
 * igbvf_free_dma_buffer - Free one allocated area of dma memory and handle
 */
void
igbvf_free_dma_buffer(dma_buffer_t *buf)
{
	if (buf->dma_handle == NULL)
		return;

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
 * igbvf_alloc_tx_ring_mem - Memory allocation for the transmit control blocks
 * of one ring.
 */
int
igbvf_alloc_tx_ring_mem(igbvf_tx_ring_t *tx_ring)
{
	igbvf_t *igbvf = tx_ring->igbvf;

	/*
	 * Allocate memory for the work list.
	 */
	tx_ring->work_list = kmem_zalloc(sizeof (tx_control_block_t *) *
	    tx_ring->ring_size, KM_NOSLEEP);

	if (tx_ring->work_list == NULL) {
		igbvf_error(igbvf,
		    "Cound not allocate memory for tx work list");
		return (IGBVF_FAILURE);
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

		igbvf_error(igbvf,
		    "Cound not allocate memory for tx free list");
		return (IGBVF_FAILURE);
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

		igbvf_error(igbvf,
		    "Cound not allocate memory for tx control blocks");
		return (IGBVF_FAILURE);
	}

	return (IGBVF_SUCCESS);
}


/*
 * igbvf_alloc_tcb_lists - Memory allocation for the transmit control blocks
 * of one ring.
 */
static int
igbvf_alloc_tcb_lists(igbvf_tx_ring_t *tx_ring)
{
	int i;
	int ret;
	tx_control_block_t *tcb;
	dma_buffer_t *tx_buf;
	igbvf_t *igbvf = tx_ring->igbvf;
	dev_info_t *devinfo = igbvf->dip;

	/*
	 * Allocate dma memory for the tx control block of free list.
	 */
	tcb = tx_ring->tcb_area;
	for (i = 0; i < tx_ring->free_list_size; i++, tcb++) {
		ASSERT(tcb != NULL);

		tx_ring->free_list[i] = tcb;

		if (!IGBVF_IS_STARTED(igbvf) ||
		    IGBVF_IS_SUSPENDED_DMA_FREE(igbvf)) {
			/*
			 * Pre-allocate dma handles for transmit. These dma
			 * handles will be dynamically bound to the data buffers
			 * passed down from the upper layers at the time of
			 * transmitting.
			 */
			ret = ddi_dma_alloc_handle(devinfo,
			    &igbvf_tx_dma_attr,
			    DDI_DMA_DONTWAIT, NULL,
			    &tcb->tx_dma_handle);
			if (ret != DDI_SUCCESS) {
				tcb->tx_dma_handle = NULL;
				igbvf_error(igbvf,
				    "Could not allocate tx dma handle: %x",
				    ret);
				goto alloc_tcb_lists_fail;
			}
		}

		/*
		 * Pre-allocate transmit buffers for packets that the
		 * size is less than bcopy_thresh.
		 */
		tx_buf = &tcb->tx_buf;

		if (!IGBVF_IS_STARTED(igbvf) ||
		    IGBVF_IS_SUSPENDED_DMA_FREE(igbvf)) {
			ret = igbvf_alloc_dma_buffer(igbvf,
			    tx_buf, igbvf->tx_buf_size);

			if (ret != IGBVF_SUCCESS) {
				ASSERT(tcb->tx_dma_handle != NULL);
				ddi_dma_free_handle(&tcb->tx_dma_handle);
				tcb->tx_dma_handle = NULL;
				igbvf_error(igbvf,
				    "Allocate tx dma buffer failed");
				goto alloc_tcb_lists_fail;
			}
		}

		if (!IGBVF_IS_STARTED(igbvf) ||
		    IGBVF_IS_SUSPENDED_DMA_UNBIND(igbvf)) {
			ret = igbvf_bind_dma_buffer(tx_buf);

			if (ret != IGBVF_SUCCESS) {
				igbvf_error(igbvf, "Bind tx dma buffer failed");
				goto alloc_tcb_lists_fail;
			}
		}

		tcb->last_index = MAX_TX_RING_SIZE;
	}

	return (IGBVF_SUCCESS);

alloc_tcb_lists_fail:
	igbvf_free_tcb_lists(tx_ring);

	return (IGBVF_FAILURE);
}

/*
 * igbvf_free_tcb_lists - Release the memory allocated for
 * the transmit control blocks of one ring.
 */
static void
igbvf_free_tcb_lists(igbvf_tx_ring_t *tx_ring)
{
	int i;
	tx_control_block_t *tcb;
	igbvf_t *igbvf = tx_ring->igbvf;

	tcb = tx_ring->tcb_area;
	if (tcb == NULL)
		return;

	for (i = 0; i < tx_ring->free_list_size; i++, tcb++) {
		/*
		 * If the dma handle is NULL, then we don't
		 * have to check the remaining.
		 */
		if (tcb->tx_dma_handle == NULL)
			break;

		if (IGBVF_IS_STARTED(igbvf) ==
		    IGBVF_IS_SUSPENDED_DMA_FREE(igbvf)) {
			/* Free the tx dma handle for dynamical binding */
			ddi_dma_free_handle(&tcb->tx_dma_handle);
			tcb->tx_dma_handle = NULL;
		}

		if (IGBVF_IS_STARTED(igbvf) ==
		    IGBVF_IS_SUSPENDED_DMA_UNBIND(igbvf))
			igbvf_unbind_dma_buffer(&tcb->tx_buf);

		if (IGBVF_IS_STARTED(igbvf) ==
		    IGBVF_IS_SUSPENDED_DMA_FREE(igbvf))
			igbvf_free_dma_buffer(&tcb->tx_buf);
	}
}

/*
 * igbvf_free_tx_ring_mem - Release the memory allocated for
 * the transmit control blocks of one ring.
 */
void
igbvf_free_tx_ring_mem(igbvf_tx_ring_t *tx_ring)
{
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

/*
 * igbvf_alloc_rcb_lists - Memory allocation for the receive control blocks
 * of one ring.
 */
static int
igbvf_alloc_rcb_lists(igbvf_rx_data_t *rx_data)
{
	int i;
	int ret;
	rx_control_block_t *rcb;
	igbvf_t *igbvf = rx_data->rx_ring->igbvf;
	dma_buffer_t *rx_buf;
	uint32_t rcb_count;

	/*
	 * Allocate memory for the rx control blocks for work list and
	 * free list.
	 */
	rcb_count = rx_data->ring_size + rx_data->free_list_size;
	rcb = rx_data->rcb_area;

	for (i = 0; i < rcb_count; i++, rcb++) {
		ASSERT(rcb != NULL);

		rx_buf = &rcb->rx_buf;

		if (!IGBVF_IS_STARTED(igbvf) ||
		    IGBVF_IS_SUSPENDED_DMA_FREE(igbvf)) {

			if (i < rx_data->ring_size) {
				/* Attach rx control block to the work list */
				rx_data->work_list[i] = rcb;
			} else {
				/* Attach rx control block to the free list */
				rx_data->free_list[i -
				    rx_data->ring_size] = rcb;
			}

			ret = igbvf_alloc_dma_buffer(igbvf,
			    rx_buf, igbvf->rx_buf_size);

			if (ret != IGBVF_SUCCESS) {
				igbvf_error(igbvf,
				    "Allocate rx dma buffer failed");
				goto alloc_rcb_lists_fail;
			}

			rx_buf->size -= IPHDR_ALIGN_ROOM;
			rx_buf->address += IPHDR_ALIGN_ROOM;

			rcb->ref_cnt = 1;
			rcb->rx_data = (igbvf_rx_data_t *)rx_data;
			rcb->free_rtn.free_func = igbvf_rx_recycle;
			rcb->free_rtn.free_arg = (char *)rcb;

			rcb->mp = desballoc((unsigned char *)
			    rx_buf->address,
			    rx_buf->size,
			    0, &rcb->free_rtn);
		}

		if (!IGBVF_IS_STARTED(igbvf) ||
		    IGBVF_IS_SUSPENDED_DMA_UNBIND(igbvf)) {

			if (igbvf_bind_dma_buffer(rx_buf) != IGBVF_SUCCESS) {
				igbvf_error(igbvf, "Bind rx dma buffer failed");
				goto alloc_rcb_lists_fail;
			}
		}
	}

	return (IGBVF_SUCCESS);

alloc_rcb_lists_fail:
	igbvf_free_rcb_lists(rx_data);

	return (IGBVF_FAILURE);
}

/*
 * igbvf_free_rcb_lists - Free the receive control blocks of one ring.
 */
static void
igbvf_free_rcb_lists(igbvf_rx_data_t *rx_data)
{
	igbvf_t *igbvf;
	rx_control_block_t *rcb;
	dma_buffer_t *rx_buf;
	uint32_t rcb_count;
	uint32_t ref_cnt;
	int i;

	igbvf = rx_data->rx_ring->igbvf;

	mutex_enter(&igbvf->rx_pending_lock);

	rcb_count = rx_data->ring_size + rx_data->free_list_size;
	rcb = rx_data->rcb_area;

	for (i = 0; i < rcb_count; i++, rcb++) {
		ASSERT(rcb != NULL);

		rx_buf = &rcb->rx_buf;

		if (IGBVF_IS_STARTED(igbvf) ==
		    IGBVF_IS_SUSPENDED_DMA_UNBIND(igbvf)) {
			igbvf_unbind_dma_buffer(rx_buf);
		}

		if (IGBVF_IS_STARTED(igbvf) ==
		    IGBVF_IS_SUSPENDED_DMA_FREE(igbvf)) {
			ref_cnt = atomic_dec_32_nv(&rcb->ref_cnt);
			if (ref_cnt == 0) {
				if (rcb->mp != NULL) {
					freemsg(rcb->mp);
					rcb->mp = NULL;
				}
				igbvf_free_dma_buffer(rx_buf);
			} else {
				atomic_inc_32(&rx_data->rcb_pending);
				atomic_inc_32(&igbvf->rcb_pending);
			}
		}
	}

	mutex_exit(&igbvf->rx_pending_lock);
}

void
igbvf_set_fma_flags(int dma_flag)
{
	if (dma_flag) {
		igbvf_tx_dma_attr.dma_attr_flags = DDI_DMA_FLAGERR;
		igbvf_buf_dma_attr.dma_attr_flags = DDI_DMA_FLAGERR;
		igbvf_desc_dma_attr.dma_attr_flags = DDI_DMA_FLAGERR;
	} else {
		igbvf_tx_dma_attr.dma_attr_flags = 0;
		igbvf_buf_dma_attr.dma_attr_flags = 0;
		igbvf_desc_dma_attr.dma_attr_flags = 0;
	}
}
