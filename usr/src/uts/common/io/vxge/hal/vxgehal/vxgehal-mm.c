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
 * FileName :   vxgehal-mm.c
 *
 * Description:  chipset memory pool object implementation
 *
 * Created:       10 May 2004
 */

#include "vxgehal.h"

/*
 * __vxge_hal_mempool_grow
 *
 * Will resize mempool up to %num_allocate value.
 */
vxge_hal_status_e
__vxge_hal_mempool_grow(
	vxge_hal_mempool_t *mempool,
	u32 num_allocate,
	u32 *num_allocated)
{
	u32 i, first_time = mempool->memblocks_allocated == 0 ? 1 : 0;
	u32 n_items = mempool->items_per_memblock;
	u32 start_block_idx = mempool->memblocks_allocated;
	u32 end_block_idx = mempool->memblocks_allocated + num_allocate;
	/*LINTED*/
	__vxge_hal_device_t *hldev;

	vxge_assert(mempool != NULL);

	hldev = (__vxge_hal_device_t *)mempool->devh;

	vxge_hal_trace_log_mm(mempool->devh, NULL_VPID,
	    "==>  %s:%s:%d", __FILE__, __func__, __LINE__);

	vxge_hal_trace_log_mm(mempool->devh, NULL_VPID,
	    "mempool = 0x"VXGE_OS_STXFMT", num_allocate = %d, "
	    "num_allocated = 0x"VXGE_OS_STXFMT, (ptr_t)mempool,
	    num_allocate, (ptr_t)num_allocated);

	*num_allocated = 0;

	if (end_block_idx > mempool->memblocks_max) {
		vxge_hal_err_log_mm(mempool->devh, NULL_VPID, "%s",
		    "__vxge_hal_mempool_grow: can grow anymore");
		vxge_hal_trace_log_mm(mempool->devh, NULL_VPID,
		    "<==  %s:%s:%d  Result: %d", __FILE__, __func__,
		    __LINE__, VXGE_HAL_ERR_OUT_OF_MEMORY);
		return (VXGE_HAL_ERR_OUT_OF_MEMORY);
	}

	for (i = start_block_idx; i < end_block_idx; i++) {
		u32 j;
		u32 is_last = ((end_block_idx-1) == i);
		vxge_hal_mempool_dma_t *dma_object =
		    mempool->memblocks_dma_arr + i;
		void *the_memblock;

		/*
		 * allocate memblock's private part. Each DMA memblock
		 * has a space allocated for item's private usage upon
		 * mempool's user request. Each time mempool grows, it will
		 * allocate new memblock and its private part at once.
		 * This helps to minimize memory usage a lot.
		 */
		mempool->memblocks_priv_arr[i] = vxge_os_malloc(
		    ((__vxge_hal_device_t *)mempool->devh)->header.pdev,
		    mempool->items_priv_size * n_items);
		if (mempool->memblocks_priv_arr[i] == NULL) {

			vxge_hal_err_log_mm(mempool->devh, NULL_VPID,
			    "memblock_priv[%d]: out of virtual memory, "
			    "requested %d(%d:%d) bytes", i,
			    mempool->items_priv_size * n_items,
			    mempool->items_priv_size, n_items);
			vxge_hal_trace_log_mm(mempool->devh, NULL_VPID,
			    "<==  %s:%s:%d  Result: %d", __FILE__, __func__,
			    __LINE__, VXGE_HAL_ERR_OUT_OF_MEMORY);
			return (VXGE_HAL_ERR_OUT_OF_MEMORY);

		}

		vxge_os_memzero(mempool->memblocks_priv_arr[i],
		    mempool->items_priv_size * n_items);

		/* allocate DMA-capable memblock */
		mempool->memblocks_arr[i] =
		    __vxge_hal_blockpool_malloc(mempool->devh,
		    mempool->memblock_size,
		    &dma_object->addr,
		    &dma_object->handle,
		    &dma_object->acc_handle);
		if (mempool->memblocks_arr[i] == NULL) {
			vxge_os_free(
			    ((__vxge_hal_device_t *)mempool->devh)->header.pdev,
			    mempool->memblocks_priv_arr[i],
			    mempool->items_priv_size * n_items);
			vxge_hal_err_log_mm(mempool->devh, NULL_VPID,
			    "memblock[%d]: out of DMA memory", i);
			vxge_hal_trace_log_mm(mempool->devh, NULL_VPID,
			    "<==  %s:%s:%d  Result: %d", __FILE__, __func__,
			    __LINE__, VXGE_HAL_ERR_OUT_OF_MEMORY);
			return (VXGE_HAL_ERR_OUT_OF_MEMORY);
		}

		(*num_allocated)++;
		mempool->memblocks_allocated++;

		vxge_os_memzero(mempool->memblocks_arr[i],
		    mempool->memblock_size);

		the_memblock = mempool->memblocks_arr[i];

		/* fill the items hash array */
		for (j = 0; j < n_items; j++) {
			u32 index = i*n_items + j;

			if (first_time && index >= mempool->items_initial) {
				break;
			}

			mempool->items_arr[index] =
			    ((char *)the_memblock + j*mempool->item_size);

			/* let caller to do more job on each item */
			if (mempool->item_func_alloc != NULL) {
				vxge_hal_status_e status;

				if ((status = mempool->item_func_alloc(
				    mempool,
				    the_memblock,
				    i,
				    dma_object,
				    mempool->items_arr[index],
				    index,
				    is_last,
				    mempool->userdata)) != VXGE_HAL_OK) {

					if (mempool->item_func_free != NULL) {
						u32 k;

						for (k = 0; k < j; k++) {

						index = i*n_items + k;

						(void) mempool->item_func_free(
						    mempool, the_memblock,
						    i, dma_object,
						    mempool->items_arr[index],
						    index, is_last,
						    mempool->userdata);
						}
					}

					vxge_os_free(((__vxge_hal_device_t *)
					    mempool->devh)->header.pdev,
					    mempool->memblocks_priv_arr[i],
					    mempool->items_priv_size *
					    n_items);

					__vxge_hal_blockpool_free(mempool->devh,
					    the_memblock,
					    mempool->memblock_size,
					    &dma_object->addr,
					    &dma_object->handle,
					    &dma_object->acc_handle);

					(*num_allocated)--;
					mempool->memblocks_allocated--;
					return (status);
				}
			}

			mempool->items_current = index + 1;
		}

		vxge_hal_info_log_mm(mempool->devh, NULL_VPID,
		    "memblock%d: allocated %dk, vaddr 0x"VXGE_OS_STXFMT", "
		    "dma_addr 0x"VXGE_OS_STXFMT,
		    i, mempool->memblock_size / 1024,
		    mempool->memblocks_arr[i], dma_object->addr);

		if (first_time && mempool->items_current ==
		    mempool->items_initial) {
			break;
		}
	}

	vxge_hal_trace_log_mm(mempool->devh, NULL_VPID,
	    "<==  %s:%s:%d  Result: 0", __FILE__, __func__, __LINE__);

	return (VXGE_HAL_OK);
}

/*
 * vxge_hal_mempool_create
 * @memblock_size:
 * @items_initial:
 * @items_max:
 * @item_size:
 * @item_func:
 *
 * This function will create memory pool object. Pool may grow but will
 * never shrink. Pool consists of number of dynamically allocated blocks
 * with size enough to hold %items_initial number of items. Memory is
 * DMA-able but client must map/unmap before interoperating with the device.
 * See also: vxge_os_dma_map(), vxge_hal_dma_unmap(), vxge_hal_status_e {}.
 */
vxge_hal_mempool_t *
__vxge_hal_mempool_create(
	vxge_hal_device_h devh,
	u32 memblock_size,
	u32 item_size,
	u32 items_priv_size,
	u32 items_initial,
	u32 items_max,
	vxge_hal_mempool_item_f item_func_alloc,
	vxge_hal_mempool_item_f item_func_free,
	void *userdata)
{
	vxge_hal_status_e status;
	u32 memblocks_to_allocate;
	vxge_hal_mempool_t *mempool;
	/*LINTED*/
	__vxge_hal_device_t *hldev;
	u32 allocated;

	vxge_assert(devh != NULL);

	hldev = (__vxge_hal_device_t *)devh;

	vxge_hal_trace_log_mm(devh, NULL_VPID,
	    "==>  %s:%s:%d", __FILE__, __func__, __LINE__);

	vxge_hal_trace_log_mm(devh, NULL_VPID,
	    "devh = 0x"VXGE_OS_STXFMT", memblock_size = %d, item_size = %d, "
	    "items_priv_size = %d, items_initial = %d, items_max = %d, "
	    "item_func_alloc = 0x"VXGE_OS_STXFMT", "
	    "item_func_free = 0x"VXGE_OS_STXFMT", "
	    "userdata = 0x"VXGE_OS_STXFMT, (ptr_t)devh,
	    memblock_size, item_size, items_priv_size,
	    items_initial, items_max, (ptr_t)item_func_alloc,
	    (ptr_t)item_func_free, (ptr_t)userdata);

	if (memblock_size < item_size) {
		vxge_hal_err_log_mm(devh, NULL_VPID,
		    "memblock_size %d < item_size %d: misconfiguration",
		    memblock_size, item_size);
		vxge_hal_trace_log_mm(devh, NULL_VPID,
		    "<==  %s:%s:%d  Result: %d", __FILE__, __func__,
		    __LINE__, VXGE_HAL_FAIL);
		return (NULL);
	}

	mempool = (vxge_hal_mempool_t *)vxge_os_malloc(
	    ((__vxge_hal_device_t *)devh)->header.pdev,
	    sizeof (vxge_hal_mempool_t));
	if (mempool == NULL) {
		vxge_hal_trace_log_mm(devh, NULL_VPID,
		    "<==  %s:%s:%d  Result: %d", __FILE__, __func__,
		    __LINE__, VXGE_HAL_ERR_OUT_OF_MEMORY);
		return (NULL);
	}
	vxge_os_memzero(mempool, sizeof (vxge_hal_mempool_t));

	mempool->devh			= devh;
	mempool->memblock_size		= memblock_size;
	mempool->items_max		= items_max;
	mempool->items_initial		= items_initial;
	mempool->item_size		= item_size;
	mempool->items_priv_size	= items_priv_size;
	mempool->item_func_alloc	= item_func_alloc;
	mempool->item_func_free		= item_func_free;
	mempool->userdata		= userdata;

	mempool->memblocks_allocated = 0;

	if (memblock_size != VXGE_OS_HOST_PAGE_SIZE)
		mempool->dma_flags = VXGE_OS_DMA_CACHELINE_ALIGNED;

#ifdef	VXGE_HAL_DMA_CONSISTENT
	mempool->dma_flags |= VXGE_OS_DMA_CONSISTENT;
#else
	mempool->dma_flags |= VXGE_OS_DMA_STREAMING;
#endif

	mempool->items_per_memblock = memblock_size / item_size;

	mempool->memblocks_max = (items_max + mempool->items_per_memblock - 1) /
	    mempool->items_per_memblock;

	/* allocate array of memblocks */
	mempool->memblocks_arr = (void **)vxge_os_malloc(
	    ((__vxge_hal_device_t *)mempool->devh)->header.pdev,
	    sizeof (void*) * mempool->memblocks_max);
	if (mempool->memblocks_arr == NULL) {
		__vxge_hal_mempool_destroy(mempool);
		vxge_hal_trace_log_mm(devh, NULL_VPID,
		    "<==  %s:%s:%d  Result: %d", __FILE__, __func__,
		    __LINE__, VXGE_HAL_ERR_OUT_OF_MEMORY);
		return (NULL);
	}
	vxge_os_memzero(mempool->memblocks_arr,
	    sizeof (void*) * mempool->memblocks_max);

	/* allocate array of private parts of items per memblocks */
	mempool->memblocks_priv_arr = (void **)vxge_os_malloc(
	    ((__vxge_hal_device_t *)mempool->devh)->header.pdev,
	    sizeof (void*) * mempool->memblocks_max);
	if (mempool->memblocks_priv_arr == NULL) {
		__vxge_hal_mempool_destroy(mempool);
		vxge_hal_trace_log_mm(devh, NULL_VPID,
		    "<==  %s:%s:%d  Result: %d", __FILE__, __func__,
		    __LINE__, VXGE_HAL_ERR_OUT_OF_MEMORY);
		return (NULL);
	}
	vxge_os_memzero(mempool->memblocks_priv_arr,
	    sizeof (void*) * mempool->memblocks_max);

	/* allocate array of memblocks DMA objects */
	mempool->memblocks_dma_arr =
	    (vxge_hal_mempool_dma_t *)vxge_os_malloc(
	    ((__vxge_hal_device_t *)mempool->devh)->header.pdev,
	    sizeof (vxge_hal_mempool_dma_t) * mempool->memblocks_max);

	if (mempool->memblocks_dma_arr == NULL) {
		__vxge_hal_mempool_destroy(mempool);
		vxge_hal_trace_log_mm(devh, NULL_VPID,
		    "<==  %s:%s:%d  Result: %d", __FILE__, __func__,
		    __LINE__, VXGE_HAL_ERR_OUT_OF_MEMORY);
		return (NULL);
	}
	vxge_os_memzero(mempool->memblocks_dma_arr,
	    sizeof (vxge_hal_mempool_dma_t) * mempool->memblocks_max);

	/* allocate hash array of items */
	mempool->items_arr = (void **)vxge_os_malloc(
	    ((__vxge_hal_device_t *)mempool->devh)->header.pdev,
	    sizeof (void*) * mempool->items_max);
	if (mempool->items_arr == NULL) {
		__vxge_hal_mempool_destroy(mempool);
		vxge_hal_trace_log_mm(devh, NULL_VPID,
		    "<==  %s:%s:%d  Result: %d", __FILE__, __func__,
		    __LINE__, VXGE_HAL_ERR_OUT_OF_MEMORY);
		return (NULL);
	}
	vxge_os_memzero(mempool->items_arr,
	    sizeof (void *) * mempool->items_max);

	mempool->shadow_items_arr = (void **)vxge_os_malloc(
	    ((__vxge_hal_device_t *)mempool->devh)->header.pdev,
	    sizeof (void*) *  mempool->items_max);
	if (mempool->shadow_items_arr == NULL) {
		__vxge_hal_mempool_destroy(mempool);
		vxge_hal_trace_log_mm(devh, NULL_VPID,
		    "<==  %s:%s:%d  Result: %d", __FILE__, __func__,
		    __LINE__, VXGE_HAL_ERR_OUT_OF_MEMORY);
		return (NULL);
	}
	vxge_os_memzero(mempool->shadow_items_arr,
	    sizeof (void *) * mempool->items_max);

	/* calculate initial number of memblocks */
	memblocks_to_allocate = (mempool->items_initial +
	    mempool->items_per_memblock - 1) /
	    mempool->items_per_memblock;

	vxge_hal_info_log_mm(mempool->devh,
	    NULL_VPID, "allocating %d memblocks, "
	    "%d items per memblock", memblocks_to_allocate,
	    mempool->items_per_memblock);

	/* pre-allocate the mempool */
	status =
	    __vxge_hal_mempool_grow(mempool, memblocks_to_allocate, &allocated);
	vxge_os_memcpy(mempool->shadow_items_arr, mempool->items_arr,
	    sizeof (void*) * mempool->items_max);
	if (status != VXGE_HAL_OK) {
		__vxge_hal_mempool_destroy(mempool);
		vxge_hal_trace_log_mm(devh, NULL_VPID,
		    "<==  %s:%s:%d  Result: %d", __FILE__, __func__,
		    __LINE__, VXGE_HAL_ERR_OUT_OF_MEMORY);
		return (NULL);
	}

	vxge_hal_info_log_mm(mempool->devh, NULL_VPID,
	    "total: allocated %dk of DMA-capable memory",
	    mempool->memblock_size * allocated / 1024);

	vxge_hal_trace_log_mm(devh, NULL_VPID,
	    "<==  %s:%s:%d  Result: 0", __FILE__, __func__, __LINE__);

	return (mempool);
}

/*
 * vxge_hal_mempool_destroy
 */
void
__vxge_hal_mempool_destroy(
	vxge_hal_mempool_t *mempool)
{
	u32 i, j;
	__vxge_hal_device_t *hldev;

	vxge_assert(mempool != NULL);

	hldev = (__vxge_hal_device_t *)mempool->devh;

	vxge_hal_trace_log_mm(hldev, NULL_VPID,
	    "==>  %s:%s:%d", __FILE__, __func__, __LINE__);

	vxge_hal_trace_log_mm(hldev, NULL_VPID,
	    "mempool = 0x"VXGE_OS_STXFMT, (ptr_t)mempool);

	for (i = 0; i < mempool->memblocks_allocated; i++) {
		vxge_hal_mempool_dma_t *dma_object;

		vxge_assert(mempool->memblocks_arr[i]);
		vxge_assert(mempool->memblocks_dma_arr + i);

		dma_object = mempool->memblocks_dma_arr + i;

		for (j = 0; j < mempool->items_per_memblock; j++) {
			u32 index = i*mempool->items_per_memblock + j;

			/* to skip last partially filled(if any) memblock */
			if (index >= mempool->items_current) {
				break;
			}

			/* let caller to do more job on each item */
			if (mempool->item_func_free != NULL) {

				mempool->item_func_free(mempool,
				    mempool->memblocks_arr[i],
				    i, dma_object,
				    mempool->shadow_items_arr[index],
				    index, /* unused */ -1,
				    mempool->userdata);
			}
		}

		vxge_os_free(hldev->header.pdev,
		    mempool->memblocks_priv_arr[i],
		    mempool->items_priv_size * mempool->items_per_memblock);

		__vxge_hal_blockpool_free(hldev,
		    mempool->memblocks_arr[i],
		    mempool->memblock_size,
		    &dma_object->addr,
		    &dma_object->handle,
		    &dma_object->acc_handle);
	}

	if (mempool->items_arr) {
		vxge_os_free(hldev->header.pdev,
		    mempool->items_arr, sizeof (void*) * mempool->items_max);
	}

	if (mempool->shadow_items_arr) {
		vxge_os_free(hldev->header.pdev,
		    mempool->shadow_items_arr,
		    sizeof (void*) * mempool->items_max);
	}

	if (mempool->memblocks_dma_arr) {
		vxge_os_free(hldev->header.pdev,
		    mempool->memblocks_dma_arr,
		    sizeof (vxge_hal_mempool_dma_t) *
		    mempool->memblocks_max);
	}

	if (mempool->memblocks_priv_arr) {
		vxge_os_free(hldev->header.pdev,
		    mempool->memblocks_priv_arr,
		    sizeof (void*) * mempool->memblocks_max);
	}

	if (mempool->memblocks_arr) {
		vxge_os_free(hldev->header.pdev,
		    mempool->memblocks_arr,
		    sizeof (void*) * mempool->memblocks_max);
	}

	vxge_os_free(hldev->header.pdev,
	    mempool, sizeof (vxge_hal_mempool_t));

	vxge_hal_trace_log_mm(hldev, NULL_VPID,
	    "<==  %s:%s:%d  Result: 0", __FILE__, __func__, __LINE__);
}

/*
 * vxge_hal_check_alignment - Check buffer alignment	and	calculate the
 * "misaligned"	portion.
 * @dma_pointer: DMA address of	the	buffer.
 * @size: Buffer size, in bytes.
 * @alignment: Alignment "granularity" (see	below),	in bytes.
 * @copy_size: Maximum number of bytes to "extract"	from the buffer
 * (in order to	spost it as	a separate scatter-gather entry). See below.
 *
 * Check buffer	alignment and calculate	"misaligned" portion, if exists.
 * The buffer is considered	aligned	if its address is multiple of
 * the specified @alignment. If	this is	the	case,
 * vxge_hal_check_alignment() returns zero.
 * Otherwise, vxge_hal_check_alignment()	uses the last argument,
 * @copy_size,
 * to calculate	the	size to	"extract" from the buffer. The @copy_size
 * may or may not be equal @alignment. The difference between these	two
 * arguments is	that the @alignment is used to make the decision: aligned
 * or not aligned. While the @copy_size	is used	to calculate the portion
 * of the buffer to "extract", i.e.	to post	as a separate entry in the
 * transmit descriptor.	For example, the combination
 * @alignment = 8 and @copy_size = 64 will work	okay on	AMD Opteron boxes.
 *
 * Note: @copy_size should be a	multiple of @alignment.	In many	practical
 * cases @copy_size and	@alignment will	probably be equal.
 *
 * See also: vxge_hal_fifo_txdl_buffer_set_aligned().
 */
u32
vxge_hal_check_alignment(
	dma_addr_t dma_pointer,
	u32 size,
	u32 alignment,
	u32 copy_size)
{
	u32	misaligned_size;

	misaligned_size	= (int)(dma_pointer & (alignment - 1));
	if (!misaligned_size) {
		return (0);
	}

	if (size > copy_size) {
		misaligned_size	= (int)(dma_pointer & (copy_size - 1));
		misaligned_size	= copy_size - misaligned_size;
	} else {
		misaligned_size	= size;
	}

	return (misaligned_size);
}
/*
 * __vxge_hal_mempool_item_count - Returns number of items in the mempool
 */
u32
__vxge_hal_mempool_item_count(
	vxge_hal_mempool_t *mempool)
{
	return (mempool->items_current);
}
/*
 * __vxge_hal_mempool_item - Returns pointer to the item in the mempool
 * items array.
 */
void*
__vxge_hal_mempool_item(
	vxge_hal_mempool_t *mempool,
	u32 index)
{
	return (mempool->items_arr[index]);
}
/*
 * __vxge_hal_mempool_item_priv - will return pointer on per item private space
 */
void*
__vxge_hal_mempool_item_priv(
	vxge_hal_mempool_t *mempool,
	u32 memblock_idx,
	void *item,
	u32 *memblock_item_idx)
{
	ptrdiff_t offset;
	void *memblock = mempool->memblocks_arr[memblock_idx];

	vxge_assert(memblock);
	offset = (u32) _PTRDIFF(item, memblock);
	vxge_assert(offset >= 0 && (u32)offset < mempool->memblock_size);

	(*memblock_item_idx) = (u32) offset / mempool->item_size;
	vxge_assert((*memblock_item_idx) < mempool->items_per_memblock);

	return ((u8 *)mempool->memblocks_priv_arr[memblock_idx] +
	    (*memblock_item_idx) * mempool->items_priv_size);
}

/*
 * __vxge_hal_mempool_items_arr - will return pointer to the items array in the
 * mempool.
 */
void*
__vxge_hal_mempool_items_arr(
	vxge_hal_mempool_t *mempool)
{
	return (mempool->items_arr);
}

/*
 * __vxge_hal_mempool_memblock - will return pointer to the memblock in the
 * mempool memblocks array.
 */
void*
__vxge_hal_mempool_memblock(
	vxge_hal_mempool_t *mempool,
	u32 memblock_idx)
{
	vxge_assert(mempool->memblocks_arr[memblock_idx]);
	return (mempool->memblocks_arr[memblock_idx]);
}

/*
 * __vxge_hal_mempool_memblock_dma - will return pointer to the dma block
 * corresponds to the memblock(identified by memblock_idx) in the mempool.
 */
vxge_hal_mempool_dma_t *
__vxge_hal_mempool_memblock_dma(
	vxge_hal_mempool_t *mempool,
	u32 memblock_idx)
{
	return (mempool->memblocks_dma_arr + memblock_idx);
}
