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
 * FileName :   vxgehal-mm.h
 *
 * Description:  memory pool object
 *
 * Created:       28 December 2006
 */

#ifndef	VXGE_HAL_MM_H
#define	VXGE_HAL_MM_H
#include <sys/strsun.h>

__EXTERN_BEGIN_DECLS

typedef void*				vxge_hal_mempool_h;

/*
 * struct vxge_hal_mempool_dma_t - Represents DMA objects passed to the
 * caller.
 */
typedef struct vxge_hal_mempool_dma_t {
	dma_addr_t			addr;
	pci_dma_h			handle;
	pci_dma_acc_h			acc_handle;
} vxge_hal_mempool_dma_t;

/*
 * vxge_hal_mempool_item_f  - Mempool item alloc/free callback
 * @mempoolh: Memory pool handle.
 * @memblock: Address of memory block
 * @memblock_index: Index of memory block
 * @item: Item that gets allocated or freed.
 * @index: Item's index in the memory pool.
 * @is_last: True, if this item is the last one in the pool; false - otherwise.
 * userdata: Per-pool user context.
 *
 * Memory pool allocation/deallocation callback.
 */
typedef vxge_hal_status_e (*vxge_hal_mempool_item_f) (
	vxge_hal_mempool_h	mempoolh,
	void			*memblock,
	u32			memblock_index,
	vxge_hal_mempool_dma_t	*dma_object,
	void			*item,
	u32			index,
	u32			is_last,
	void			*userdata);

/*
 * struct vxge_hal_mempool_t - Memory pool.
 */
typedef struct vxge_hal_mempool_t {
	vxge_hal_mempool_item_f	item_func_alloc;
	vxge_hal_mempool_item_f	item_func_free;
	void			*userdata;
	void			**memblocks_arr;
	void			**memblocks_priv_arr;
	vxge_hal_mempool_dma_t	*memblocks_dma_arr;
	vxge_hal_device_h	devh;
	u32			memblock_size;
	u32			memblocks_max;
	u32			memblocks_allocated;
	u32			item_size;
	u32			items_max;
	u32			items_initial;
	u32			items_current;
	u32			items_per_memblock;
	u32			dma_flags;
	void			**items_arr;
	void			**shadow_items_arr;
	u32			items_priv_size;
} vxge_hal_mempool_t;

/*
 * __vxge_hal_mempool_item_count - Returns number of items in the mempool
 */
u32
__vxge_hal_mempool_item_count(
	vxge_hal_mempool_t *mempool);

/*
 * __vxge_hal_mempool_item - Returns pointer to the item in the mempool
 * items array.
 */
void*
__vxge_hal_mempool_item(
	vxge_hal_mempool_t *mempool,
	u32 index);

/*
 * __vxge_hal_mempool_item_priv - will return pointer on per item private space
 */
void*
__vxge_hal_mempool_item_priv(
	vxge_hal_mempool_t *mempool,
	u32 memblock_idx,
	void *item,
	u32 *memblock_item_idx);

/*
 * __vxge_hal_mempool_items_arr - will return pointer to the items array in the
 * mempool.
 */
void*
__vxge_hal_mempool_items_arr(
	vxge_hal_mempool_t *mempool);

/*
 * __vxge_hal_mempool_memblock - will return pointer to the memblock in the
 * mempool memblocks array.
 */
void*
__vxge_hal_mempool_memblock(
	vxge_hal_mempool_t *mempool,
	u32 memblock_idx);

/*
 * __vxge_hal_mempool_memblock_dma - will return pointer to the dma block
 * corresponds to the memblock(identified by memblock_idx) in the mempool.
 */
vxge_hal_mempool_dma_t *
__vxge_hal_mempool_memblock_dma(
	vxge_hal_mempool_t *mempool,
	u32 memblock_idx);

vxge_hal_status_e
__vxge_hal_mempool_grow(
	vxge_hal_mempool_t *mempool,
	u32 num_allocate,
	u32 *num_allocated);

vxge_hal_mempool_t *
__vxge_hal_mempool_create(
	vxge_hal_device_h devh,
	u32 memblock_size,
	u32 item_size,
	u32 private_size,
	u32 items_initial,
	u32 items_max,
	vxge_hal_mempool_item_f item_func_alloc,
	vxge_hal_mempool_item_f item_func_free,
	void *userdata);

void
__vxge_hal_mempool_destroy(
	vxge_hal_mempool_t *mempool);


__EXTERN_END_DECLS

#endif /* VXGE_HAL_MM_H */
