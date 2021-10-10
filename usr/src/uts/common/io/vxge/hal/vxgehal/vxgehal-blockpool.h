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
 * FileName :   vxgehal-blockpool.h
 *
 * Description:  HAL Block pool for OD List, NSMR and PBL Lists
 *
 * Created:       12 May 2006
 */
#ifndef	VXGE_HAL_BLOCKPOOL_H
#define	VXGE_HAL_BLOCKPOOL_H

__EXTERN_BEGIN_DECLS

/*
 * struct __vxge_hal_blockpool_entry_t - Block private data structure
 * @item: List header used to link.
 * @length: Length of the block
 * @memblock: Virtual address block
 * @dma_addr: DMA Address of the block.
 * @dma_handle: DMA handle of the block.
 * @acc_handle: DMA acc handle
 *
 * Block is allocated with a header to put the blocks into list.
 *
 */
typedef struct __vxge_hal_blockpool_entry_t {
	vxge_list_t			item;
	u32				length;
	void				*memblock;
	dma_addr_t			dma_addr;
	pci_dma_h			dma_handle;
	pci_dma_acc_h			acc_handle;
} __vxge_hal_blockpool_entry_t;

/*
 * struct __vxge_hal_blockpool_t - Block Pool
 * @hldev: HAL device
 * @block_size: size of each block.
 * @Pool_size: Number of blocks in the pool
 * @pool_incr: Number of blocks to be requested/freed at a time from OS
 * @pool_min: Minimum number of block below which to request additional blocks
 * @pool_max: Maximum number of blocks above which to free additional blocks
 * @req_out: Number of block requests with OS out standing
 * @dma_flags: DMA flags
 * @free_block_list: List of free blocks
 * @pool_lock: Spin lock for the pool
 *
 * Block pool contains the DMA blocks preallocated.
 *
 */
typedef struct __vxge_hal_blockpool_t {
	vxge_hal_device_h		hldev;
	u32				block_size;
	u32				pool_size;
	u32				pool_incr;
	u32				pool_min;
	u32				pool_max;
	u32				req_out;
	u32				dma_flags;
	vxge_list_t			free_block_list;
	vxge_list_t			free_entry_list;
	spinlock_t			pool_lock;
} __vxge_hal_blockpool_t;

vxge_hal_status_e
__vxge_hal_blockpool_create(vxge_hal_device_h hldev,
			__vxge_hal_blockpool_t *blockpool,
			u32 pool_size,
			u32 pool_incr,
			u32 pool_min,
			u32 pool_max);

void
__vxge_hal_blockpool_destroy(__vxge_hal_blockpool_t *blockpool);

__vxge_hal_blockpool_entry_t *
__vxge_hal_blockpool_block_allocate(vxge_hal_device_h hldev,
				    u32 size);

void
__vxge_hal_blockpool_block_free(vxge_hal_device_h hldev,
			__vxge_hal_blockpool_entry_t *entry);

void *
__vxge_hal_blockpool_malloc(vxge_hal_device_h hldev,
			u32 size,
			dma_addr_t *dma_addr,
			pci_dma_h *dma_handle,
			pci_dma_acc_h *acc_handle);

void
__vxge_hal_blockpool_free(vxge_hal_device_h hldev,
			void *memblock,
			u32 size,
			dma_addr_t *dma_addr,
			pci_dma_h *dma_handle,
			pci_dma_acc_h *acc_handle);

vxge_hal_status_e
__vxge_hal_blockpool_list_allocate(vxge_hal_device_h hldev,
			vxge_list_t *blocklist, u32 count);

void
__vxge_hal_blockpool_list_free(vxge_hal_device_h hldev,
			vxge_list_t *blocklist);

__EXTERN_END_DECLS

#endif /* VXGE_HAL_BLOCKPOOL_H */
