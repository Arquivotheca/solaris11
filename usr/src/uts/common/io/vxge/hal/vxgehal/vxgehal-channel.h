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
 * FileName :   vxgehal-channel.h
 *
 * Description:  HAL channel object functionality
 *
 * Created:       27 December 2006
 */

#ifndef	VXGE_HAL_CHANNEL_H
#define	VXGE_HAL_CHANNEL_H

__EXTERN_BEGIN_DECLS

/*
 * __vxge_hal_dtr_h - Handle to the desriptor object used for nonoffload
 *		send or receive. Generic handle which can be with txd or rxd
 */
typedef void* __vxge_hal_dtr_h;

/*
 * enum __vxge_hal_channel_type_e - Enumerated channel types.
 * @VXGE_HAL_CHANNEL_TYPE_UNKNOWN: Unknown channel.
 * @VXGE_HAL_CHANNEL_TYPE_FIFO: fifo.
 * @VXGE_HAL_CHANNEL_TYPE_RING: ring.
 * @VXGE_HAL_CHANNEL_TYPE_SQ: Send Queue
 * @VXGE_HAL_CHANNEL_TYPE_SRQ: Receive Queue
 * @VXGE_HAL_CHANNEL_TYPE_CQRQ: Receive queue completion queue
 * @VXGE_HAL_CHANNEL_TYPE_UMQ: Up message queue
 * @VXGE_HAL_CHANNEL_TYPE_DMQ: Down message queue
 * @VXGE_HAL_CHANNEL_TYPE_MAX: Maximum number of HAL-supported
 * (and recognized) channel types. Currently: 7.
 *
 * Enumerated channel types. Currently there are only two link-layer
 * channels - X3100 fifo and X3100 ring. In the future the list will grow.
 */
typedef enum __vxge_hal_channel_type_e {
	VXGE_HAL_CHANNEL_TYPE_UNKNOWN			= 0,
	VXGE_HAL_CHANNEL_TYPE_FIFO			= 1,
	VXGE_HAL_CHANNEL_TYPE_RING			= 2,
	VXGE_HAL_CHANNEL_TYPE_SEND_QUEUE		= 3,
	VXGE_HAL_CHANNEL_TYPE_RECEIVE_QUEUE		= 4,
	VXGE_HAL_CHANNEL_TYPE_COMPLETION_QUEUE		= 5,
	VXGE_HAL_CHANNEL_TYPE_UP_MESSAGE_QUEUE		= 6,
	VXGE_HAL_CHANNEL_TYPE_DOWN_MESSAGE_QUEUE	= 7,
	VXGE_HAL_CHANNEL_TYPE_MAX			= 8
} __vxge_hal_channel_type_e;

/*
 * __vxge_hal_dtr_item_t
 * @dtr: Pointer to the descriptors that contains the dma data
 *		to/from the device.
 * @hal_priv: HAL Private data related to the dtr.
 * @uld_priv: ULD Private data related to the dtr.
 */
typedef struct __vxge_hal_dtr_item_t {
	void *dtr;
	void *hal_priv;
	void *uld_priv;
	u32  state;
#define	VXGE_HAL_CHANNEL_DTR_FREE	0
#define	VXGE_HAL_CHANNEL_DTR_RESERVED	1
#define	VXGE_HAL_CHANNEL_DTR_POSTED	2
#define	VXGE_HAL_CHANNEL_DTR_COMPLETED	3
} __vxge_hal_dtr_item_t;

/*
 * __vxge_hal_channel_t
 * @item: List item; used to maintain a list of open channels.
 * @type: Channel type. See vxge_hal_channel_type_e {}.
 * @devh: Device handle. HAL device object that contains _this_ channel.
 * @pdev: PCI Device object
 * @vph: Virtual path handle. Virtual Path Object that contains _this_ channel.
 * @length: Channel length. Currently allocated number of descriptors.
 *	 The channel length "grows" when more descriptors get allocated.
 *	 See _hal_mempool_grow.
 * @dtr_arr: Dtr array. Contains descriptors posted to the channel and their
 *	  private data.
 *	   Note that at any point in time @dtr_arr contains 3 types of
 *	   descriptors:
 *	   1) posted but not yet consumed by X3100 device;
 *	   2) consumed but not yet completed;
 *	   3) completed.
 * @post_index: Post index. At any point in time points on the
 *		 position in the channel, which'll contain next to-be-posted
 *		 descriptor.
 * @compl_index: Completion index. At any point in time points on the
 *		  position in the channel, which will contain next
 *		  to-be-completed descriptor.
 * @reserve_index: Reserve index. At any point in time points on the
 *		  position in the channel, which will contain next
 *		  to-be-reserved descriptor.
 * @free_dtr_count: Number of dtrs free.
 * @posted_dtr_count: Number of dtrs posted
 * @post_lock: Lock to serialize multiple concurrent "posters" of descriptors
 *		on the given channel.
 * @poll_bytes: Poll bytes.
 * @per_dtr_space: Per-descriptor space (in bytes) that channel user can utilize
 *		to store per-operation control information.
 * @stats: Pointer to common statistics
 * @userdata: Per-channel opaque (void*) user-defined context, which may be
 *	   upper-layer driver object, ULP connection, etc.
 *	   Once channel is open, @userdata is passed back to user via
 *	   vxge_hal_channel_callback_f.
 *
 * HAL channel object.
 *
 * See also: vxge_hal_channel_type_e {}, vxge_hal_channel_flag_e
 */
typedef struct __vxge_hal_channel_t {
	vxge_list_t		item;
	__vxge_hal_channel_type_e	type;
	vxge_hal_device_h	devh;
	pci_dev_h		pdev;
	vxge_hal_vpath_h	vph;
	u32			length;
	__vxge_hal_dtr_item_t	*dtr_arr;
#ifdef	__VXGE_WIN__
	u32			__vxge_os_attr_cacheline_aligned compl_index;
	u32			__vxge_os_attr_cacheline_aligned reserve_index;
#else
	u32			compl_index __vxge_os_attr_cacheline_aligned;
	u32			reserve_index __vxge_os_attr_cacheline_aligned;
#endif
	spinlock_t		post_lock;
	u32			poll_bytes;
	u32			per_dtr_space;
	vxge_hal_vpath_stats_sw_common_info_t *stats;
	void			*userdata;
#ifdef	__VXGE_WIN__
} __vxge_os_attr_cacheline_aligned __vxge_hal_channel_t;
#else
} __vxge_hal_channel_t __vxge_os_attr_cacheline_aligned;
#endif

#define	__vxge_hal_channel_is_posted_dtr(channel, index) \
	    ((channel)->dtr_arr[index].state == VXGE_HAL_CHANNEL_DTR_POSTED)

#define	__vxge_hal_channel_for_each_posted_dtr(channel, dtrh, index) \
	for (index = (channel)->compl_index,\
	    dtrh = (channel)->dtr_arr[index].dtr; \
	    (index < (channel)->reserve_index) && \
	    ((channel)->dtr_arr[index].state == VXGE_HAL_CHANNEL_DTR_POSTED); \
	    index = (++index == (channel)->length)? 0 : index, \
	    dtrh = (channel)->dtr_arr[index].dtr)

#define	__vxge_hal_channel_for_each_dtr(channel, dtrh, index) \
	for (index = 0, dtrh = (channel)->dtr_arr[index].dtr; \
	    index < (channel)->length; \
	    dtrh = ((++index == (channel)->length)? 0 : \
	    (channel)->dtr_arr[index].dtr))

#define	__vxge_hal_channel_free_dtr_count(channel)			\
	(((channel)->reserve_index < (channel)->compl_index) ?	\
	((channel)->compl_index - (channel)->reserve_index) :	\
	(((channel)->length - (channel)->reserve_index) + \
	(channel)->reserve_index))

/* ========================== CHANNEL PRIVATE API ========================= */

__vxge_hal_channel_t *
__vxge_hal_channel_allocate(
	vxge_hal_device_h devh,
	vxge_hal_vpath_h vph,
	__vxge_hal_channel_type_e type,
	u32 length,
	u32 per_dtr_space,
	void *userdata);

void
__vxge_hal_channel_free(
	__vxge_hal_channel_t *channel);

vxge_hal_status_e
__vxge_hal_channel_initialize(
	__vxge_hal_channel_t *channel);

vxge_hal_status_e
__vxge_hal_channel_reset(
	__vxge_hal_channel_t *channel);

void
__vxge_hal_channel_terminate(
	__vxge_hal_channel_t *channel);

void __vxge_hal_channel_init_pending_list(
	vxge_hal_device_h devh);

void __vxge_hal_channel_insert_pending_list(
	__vxge_hal_channel_t *channel);

void __vxge_hal_channel_process_pending_list(
	vxge_hal_device_h devhv);

void __vxge_hal_channel_destroy_pending_list(
	vxge_hal_device_h devh);

#if defined(VXGE_DEBUG_FP) && (VXGE_DEBUG_FP & VXGE_DEBUG_FP_CHANNEL)
#define	__HAL_STATIC_CHANNEL
#define	__HAL_INLINE_CHANNEL
#else /* VXGE_FASTPATH_EXTERN */
#define	__HAL_STATIC_CHANNEL static
#define	__HAL_INLINE_CHANNEL inline
#endif /* VXGE_FASTPATH_INLINE */

/* ========================== CHANNEL Fast Path API ========================= */
/*
 * __vxge_hal_channel_dtr_reserve	- Reserve a dtr from the channel
 * @channelh: Channel
 * @dtrh: Buffer to return the DTR pointer
 *
 * Reserve a dtr from the reserve array.
 *
 */
vxge_hal_status_e
__vxge_hal_channel_dtr_reserve(__vxge_hal_channel_t *channel,
    __vxge_hal_dtr_h *dtrh);
#pragma inline(__vxge_hal_channel_dtr_reserve)

/*
 * __vxge_hal_channel_dtr_restore - Restores a dtr to the channel
 * @channelh: Channel
 * @dtr: DTR pointer
 *
 * Returns a dtr back to reserve array.
 *
 */
void __vxge_hal_channel_dtr_restore(__vxge_hal_channel_t *channel,
    __vxge_hal_dtr_h dtrh);
#pragma inline(__vxge_hal_channel_dtr_restore)

/*
 * __vxge_hal_channel_dtr_post - Post a dtr to the channel
 * @channelh: Channel
 * @dtr: DTR pointer
 *
 * Posts a dtr to work array.
 *
 */
void __vxge_hal_channel_dtr_post(__vxge_hal_channel_t *channel, u32 index);
#pragma inline(__vxge_hal_channel_dtr_post)

/*
 * __vxge_hal_channel_dtr_try_complete - Returns next completed dtr
 * @channelh: Channel
 * @dtr: Buffer to return the next completed DTR pointer
 *
 * Returns the next completed dtr with out removing it from work array
 *
 */
void __vxge_hal_channel_dtr_try_complete(__vxge_hal_channel_t *channel,
    __vxge_hal_dtr_h *dtrh);
#pragma inline(__vxge_hal_channel_dtr_try_complete)

/*
 * __vxge_hal_channel_dtr_complete - Removes next completed dtr from
 * the work array
 * @channelh: Channel
 *
 * Removes the next completed dtr from work array
 *
 */
void __vxge_hal_channel_dtr_complete(__vxge_hal_channel_t *channel);
#pragma inline(__vxge_hal_channel_dtr_complete)

/*
 * __vxge_hal_channel_dtr_free - Frees a dtr
 * @channelh: Channel
 * @index:  Index of DTR
 *
 * Returns the dtr to free array
 *
 */
void __vxge_hal_channel_dtr_free(__vxge_hal_channel_t *channel, u32 index);
#pragma inline(__vxge_hal_channel_dtr_free)

__EXTERN_END_DECLS

#endif /* VXGE_HAL_CHANNEL_H */
