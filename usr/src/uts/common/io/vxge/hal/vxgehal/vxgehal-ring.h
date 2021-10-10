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
/*
 * Copyright Exar 2010. Copyright (c) 2002-2010 Neterion, Inc.
 * All right Reserved.
 *
 * FileName :   vxgehal-ring.h
 *
 * Description:  HAL Rx ring object functionality
 *
 * Created:       29 December 2006
 */

#ifndef	VXGE_HAL_RING_H
#define	VXGE_HAL_RING_H

__EXTERN_BEGIN_DECLS

typedef u8 vxge_hal_ring_block_t[VXGE_OS_HOST_PAGE_SIZE];

#define	VXGE_HAL_RING_NEXT_BLOCK_POINTER_OFFSET (VXGE_OS_HOST_PAGE_SIZE-8)
#define	VXGE_HAL_RING_MEMBLOCK_IDX_OFFSET	(VXGE_OS_HOST_PAGE_SIZE-16)

/*
 * struct __vxge_hal_ring_rxd_priv_t - Receive descriptor HAL-private data.
 * @dma_addr: DMA (mapped) address of _this_ descriptor.
 * @dma_handle: DMA handle used to map the descriptor onto device.
 * @dma_offset: Descriptor's offset in the memory block. HAL allocates
 *		 descriptors in memory blocks of %VXGE_OS_HOST_PAGE_SIZE
 *		 bytes. Each memblock is contiguous DMA-able memory. Each
 *		 memblock contains 1 or more 4KB RxD blocks visible to the
 *		 X3100 hardware.
 * @dma_object: DMA address and handle of the memory block that contains
 *		 the descriptor. This member is used only in the "checked"
 *		 version of the HAL (to enforce certain assertions);
 *		 otherwise it gets compiled out.
 * @allocated: True if the descriptor is reserved, 0 otherwise. Internal usage.
 * @db_bytes: Number of doorbell bytes to be posted for this Rxd. This includes
 *		next RxD block pointers
 *
 * Per-receive decsriptor HAL-private data. HAL uses the space to keep DMA
 * information associated with the descriptor. Note that ULD can ask HAL
 * to allocate additional per-descriptor space for its own (ULD-specific)
 * purposes.
 */
typedef struct __vxge_hal_ring_rxd_priv_t {
	dma_addr_t		dma_addr;
	pci_dma_h		dma_handle;
	ptrdiff_t		dma_offset;
#ifdef	VXGE_DEBUG_ASSERT
	vxge_hal_mempool_dma_t	*dma_object;
#endif
#ifdef	VXGE_OS_MEMORY_CHECK
	u32			allocated;
#endif
	u32			db_bytes;
} __vxge_hal_ring_rxd_priv_t;


/*
 * struct __vxge_hal_ring_t - Ring channel.
 * @channel: Channel "base" of this ring, the common part of all HAL
 *	  channels.
 * @mempool: Memory pool, the pool from which descriptors get allocated.
 *	  (See vxge_hal_mm.h).
 * @config: Ring configuration, part of device configuration
 *	 (see vxge_hal_device_config_t {}).
 * @ring_length: Length of the ring
 * @buffer_mode: 1, 3, or 5. The value specifies a receive buffer mode,
 *	 as per X3100 User Guide.
 * @indicate_max_pkts: Maximum number of packets processed within a single
 *	 interrupt. Can be used to limit the time spent inside hw
 *	 interrupt.
 * @rxd_size: RxD sizes for 1-, 3- or 5- buffer modes. As per X3100 spec,
 *	   1-buffer mode descriptor is 32 byte long, etc.
 * @rxd_priv_size: Per RxD size reserved (by HAL) for ULD to keep per-descriptor
 *		data (e.g., DMA handle for Solaris)
 * @per_rxd_space: Per rxd space requested by ULD
 * @rxds_per_block: Number of descriptors per hardware-defined RxD
 *		 block. Depends on the (1-, 3-, 5-) buffer mode.
 * @rxdblock_priv_size: Reserved at the end of each RxD block. HAL internal
 *			 usage. Not to confuse with @rxd_priv_size.
 * @rxd_mem_avail: Available RxD memory
 * @db_byte_count: Number of doorbell bytes to be posted
 * @cmpl_cnt: Completion counter. Is reset to zero upon entering the ISR.
 *	   Used in conjunction with @indicate_max_pkts.
 * @active_sw_lros: List of Software LRO sessions in progess
 * @active_sw_lro_count: Number of Software LRO sessions in progess
 * @free_sw_lros: List of Software LRO sessions free
 * @free_sw_lro_count: Number of Software LRO sessions free
 * @sw_lro_lock: LRO session lists' lock
 * @callback: Channel completion callback. HAL invokes the callback when there
 *	   are new completions on that channel. In many implementations
 *	   the @callback executes in the hw interrupt context.
 * @rxd_init: Channel's descriptor-initialize callback.
 *	   See vxge_hal_ring_rxd_init_f {}.
 *	   If not NULL, HAL invokes the callback when opening
 *	   the ring.
 * @rxd_term: Channel's descriptor-terminate callback. If not NULL,
 *	 HAL invokes the callback when closing the corresponding channel.
 *	 See also vxge_hal_channel_rxd_term_f {}.
 * @stats: Statistics for ring
 * Ring channel.
 *
 * Note: The structure is cache line aligned to better utilize
 *	   CPU cache performance.
 */
typedef struct __vxge_hal_ring_t {
	__vxge_hal_channel_t				channel;
	vxge_hal_mempool_t			*mempool;
	vxge_hal_ring_config_t			*config;
	u32					ring_length;
	u32					buffer_mode;
	u32					indicate_max_pkts;
	u32					rxd_size;
	u32					rxd_priv_size;
	u32					per_rxd_space;
	u32					rxds_per_block;
	u32					rxdblock_priv_size;
	u32					rxd_mem_avail;
	u32					db_byte_count;
	u32					cmpl_cnt;
#if defined(VXGE_HAL_USE_SW_LRO)
	vxge_list_t				active_sw_lros;
	u32					active_sw_lro_count;
	vxge_list_t				free_sw_lros;
	u32					free_sw_lro_count;
	spinlock_t				sw_lro_lock;
#endif
	vxge_hal_ring_callback_f		callback;
	vxge_hal_ring_rxd_init_f		rxd_init;
	vxge_hal_ring_rxd_term_f		rxd_term;
	vxge_hal_vpath_stats_sw_ring_info_t	*stats;
} __vxge_os_attr_cacheline_aligned __vxge_hal_ring_t;

#define	VXGE_HAL_RING_ULD_PRIV(ring, rxdh)				\
	ring->channel.dtr_arr[						\
	    ((vxge_hal_ring_rxd_5_t *)(rxdh))->host_control].uld_priv

#define	VXGE_HAL_RING_HAL_PRIV(ring, rxdh)				\
	((__vxge_hal_ring_rxd_priv_t *)(ring->channel.dtr_arr[		\
	    ((vxge_hal_ring_rxd_5_t *)(rxdh))->host_control].hal_priv))

#define	VXGE_HAL_RING_POST_DOORBELL(vph, ringh)				\
{									\
	if (((__vxge_hal_ring_t *)(ringh))->config->post_mode ==	\
	    VXGE_HAL_RING_POST_MODE_DOORBELL) {				\
		vxge_hal_ring_rxd_post_post_db(vph);			\
	}								\
}

#define	VXGE_HAL_RING_RXD_INDEX(rxdp)	\
	(u32)((vxge_hal_ring_rxd_5_t *)rxdp)->host_control
dma_addr_t __vxge_hal_ring_block_next_pointer(
	vxge_hal_ring_block_t *block);
#pragma inline(__vxge_hal_ring_block_next_pointer)

/* ========================== RING PRIVATE API ============================ */

u64
__vxge_hal_ring_first_block_address_get(
	vxge_hal_ring_h ringh);

vxge_hal_status_e
__vxge_hal_ring_create(
	vxge_hal_vpath_h vpath_handle,
	vxge_hal_ring_attr_t *attr);

vxge_hal_status_e
__vxge_hal_ring_abort(
	vxge_hal_ring_h ringh,
	vxge_hal_reopen_e reopen);

vxge_hal_status_e
__vxge_hal_ring_reset(
	vxge_hal_ring_h ringh);

vxge_hal_status_e
__vxge_hal_ring_delete(
	vxge_hal_vpath_h vpath_handle);

vxge_hal_status_e
__vxge_hal_ring_initial_replenish(
	__vxge_hal_ring_t *ring,
	vxge_hal_reopen_e reopen);

vxge_hal_status_e
__vxge_hal_ring_frame_length_set(
	__vxge_hal_virtualpath_t *vpath,
	u32 new_frmlen);

__EXTERN_END_DECLS

#endif /* VXGE_HAL_RING_H */
