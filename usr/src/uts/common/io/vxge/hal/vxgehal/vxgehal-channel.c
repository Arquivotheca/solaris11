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
 * FileName :   vxgehal-channel.c
 *
 * Description:  Base unit for all the queues
 *
 * Created:       9 January 2006
 */
#include "vxgehal.h"


/*
 * __vxge_hal_channel_allocate - Allocate memory for channel
 * @devh: Handle to the device object
 * @vph: Handle to Virtual Path
 * @type: Type of channel
 * @length: Lengths of arrays
 * @per_dtr_space: ULD requested per dtr space to be allocated in priv
 * @userdata: User data to be passed back in the callback
 *
 * This function allocates required memory for the channel and various arrays
 * in the channel
 */
__vxge_hal_channel_t *
__vxge_hal_channel_allocate(
	vxge_hal_device_h devh,
	vxge_hal_vpath_h vph,
	__vxge_hal_channel_type_e	type,
	u32 length,
	u32 per_dtr_space,
	void *userdata)
{
	vxge_hal_device_t *hldev = (vxge_hal_device_t *)devh;
	__vxge_hal_channel_t *channel;
	u32 size = 0, index;

	vxge_assert((devh != NULL) && (vph != NULL));

	vxge_hal_trace_log_channel(devh,
	    ((__vxge_hal_vpath_handle_t *)vph)->vpath->vp_id,
	    "==>  %s:%s:%d", __FILE__, __func__, __LINE__);

	vxge_hal_trace_log_channel(devh,
	    ((__vxge_hal_vpath_handle_t *)vph)->vpath->vp_id,
	    "devh = 0x"VXGE_OS_STXFMT", vph = "
	    "0x"VXGE_OS_STXFMT", type = %d, length = %d, "
	    "per_dtr_space = %d, userdata = 0x"VXGE_OS_STXFMT,
	    (ptr_t)devh, (ptr_t)vph, type, length, per_dtr_space,
	    (ptr_t)userdata);

	switch (type) {
		case VXGE_HAL_CHANNEL_TYPE_FIFO:
			size = sizeof (__vxge_hal_fifo_t);
			break;
		case VXGE_HAL_CHANNEL_TYPE_RING:
			size = sizeof (__vxge_hal_ring_t);
			break;


		default :
			vxge_assert(size);
			break;

	}

	channel = (__vxge_hal_channel_t *)vxge_os_malloc(hldev->pdev, size);
	if (channel == NULL) {
		vxge_hal_trace_log_channel(devh,
		    ((__vxge_hal_vpath_handle_t *)vph)->vpath->vp_id,
		    "<==  %s:%s:%d  Result: %d", __FILE__, __func__,
		    __LINE__, VXGE_HAL_ERR_OUT_OF_MEMORY);
		return (NULL);
	}

	vxge_os_memzero(channel, size);
	vxge_list_init(&channel->item);

	channel->pdev = hldev->pdev;
	channel->type = type;
	channel->devh = devh;
	channel->vph = vph;

	channel->userdata = userdata;
	channel->per_dtr_space = per_dtr_space;

	channel->length = length;

	channel->dtr_arr = (__vxge_hal_dtr_item_t *)vxge_os_malloc(hldev->pdev,
	    sizeof (__vxge_hal_dtr_item_t)*length);
	if (channel->dtr_arr == NULL) {
		__vxge_hal_channel_free(channel);
		vxge_hal_trace_log_channel(devh,
		    ((__vxge_hal_vpath_handle_t *)vph)->vpath->vp_id,
		    "<==  %s:%s:%d  Result: %d", __FILE__, __func__,
		    __LINE__, VXGE_HAL_ERR_OUT_OF_MEMORY);
		return (NULL);
	}

	vxge_os_memzero(channel->dtr_arr,
	    sizeof (__vxge_hal_dtr_item_t)*length);

	channel->compl_index = 0;
	channel->reserve_index = 0;

	for (index = 0; index < length; index++) {
		channel->dtr_arr[index].state =
		    VXGE_HAL_CHANNEL_DTR_FREE;
	}

	vxge_hal_trace_log_channel(devh,
	    ((__vxge_hal_vpath_handle_t *)vph)->vpath->vp_id,
	    "<==  %s:%s:%d  Result: 0", __FILE__, __func__, __LINE__);
	return (channel);
}

/*
 * __vxge_hal_channel_free - Free memory allocated for channel
 * @channel: channel to be freed
 *
 * This function deallocates memory from the channel and various arrays
 * in the channel
 */
void
__vxge_hal_channel_free(
	__vxge_hal_channel_t *channel)
{
	int size = 0;
	/*LINTED*/
	u32	vp_id = 0;
	/*LINTED*/
	vxge_hal_device_t *hldev;

	vxge_assert(channel != NULL);

	hldev = (vxge_hal_device_t *)channel->devh;

	vp_id = ((__vxge_hal_vpath_handle_t *)channel->vph)->vpath->vp_id;

	vxge_hal_trace_log_channel(hldev, vp_id,
	    "==>  %s:%s:%d", __FILE__, __func__, __LINE__);

	vxge_hal_trace_log_channel(hldev, vp_id,
	    "channel = 0x"VXGE_OS_STXFMT, (ptr_t)channel);

	vxge_assert(channel->pdev);

	if (channel->dtr_arr) {
		vxge_os_free(channel->pdev, channel->dtr_arr,
		    sizeof (__vxge_hal_dtr_item_t)*channel->length);
		channel->dtr_arr = NULL;
	}

	switch (channel->type) {
		case VXGE_HAL_CHANNEL_TYPE_FIFO:
			size = sizeof (__vxge_hal_fifo_t);
			break;
		case VXGE_HAL_CHANNEL_TYPE_RING:
			size = sizeof (__vxge_hal_ring_t);
			break;
		default:
			break;
	}

	vxge_os_free(channel->pdev, channel, size);

	vxge_hal_trace_log_channel(hldev, vp_id,
	    "<==  %s:%s:%d  Result: 0", __FILE__, __func__, __LINE__);
}

/*
 * __vxge_hal_channel_initialize - Initialize a channel
 * @channel: channel to be initialized
 *
 * This function initializes a channel by properly
 *		setting the various references
 */
vxge_hal_status_e
__vxge_hal_channel_initialize(
	__vxge_hal_channel_t *channel)
{
	__vxge_hal_virtualpath_t *vpath =
	    ((__vxge_hal_vpath_handle_t *)channel->vph)->vpath;
	/* vxge_hal_device_t *hldev; */

	vxge_assert(channel != NULL);

	/* hldev = (vxge_hal_device_t *)channel->devh; */

	vxge_hal_trace_log_channel(hldev, vpath->vp_id,
	    "==>  %s:%s:%d", __FILE__, __func__, __LINE__);

	vxge_hal_trace_log_channel(hldev, vpath->vp_id,
	    "channel = 0x"VXGE_OS_STXFMT, (ptr_t)channel);

	vpath = (__vxge_hal_virtualpath_t *)
	    ((__vxge_hal_vpath_handle_t *)channel->vph)->vpath;

	vxge_assert(vpath != NULL);

	switch (channel->type) {
	case VXGE_HAL_CHANNEL_TYPE_FIFO:
		vpath->fifoh = (vxge_hal_fifo_h)channel;
		channel->stats =
		    &((__vxge_hal_fifo_t *)channel)->stats->common_stats;
		break;
	case VXGE_HAL_CHANNEL_TYPE_RING:
		vpath->ringh = (vxge_hal_ring_h)channel;
		channel->stats =
		    &((__vxge_hal_ring_t *)channel)->stats->common_stats;
		break;


	default:
		break;
	}

	vxge_hal_trace_log_channel(hldev, vpath->vp_id,
	    "<==  %s:%s:%d  Result: 0", __FILE__, __func__, __LINE__);

	return (VXGE_HAL_OK);
}

/*
 * __vxge_hal_channel_reset - Resets a channel
 * @channel: channel to be reset
 *
 * This function resets a channel by properly setting the various references
 */
vxge_hal_status_e
__vxge_hal_channel_reset(
	__vxge_hal_channel_t *channel)
{
	u32 i;
	/*LINTED*/
	__vxge_hal_device_t *hldev;

	vxge_assert(channel != NULL);

	hldev = (__vxge_hal_device_t *)channel->devh;

	vxge_hal_trace_log_channel(hldev,
	    ((__vxge_hal_vpath_handle_t *)channel->vph)->vpath->vp_id,
	    "==>  %s:%s:%d", __FILE__, __func__, __LINE__);

	vxge_hal_trace_log_channel(hldev,
	    ((__vxge_hal_vpath_handle_t *)channel->vph)->vpath->vp_id,
	    "channel = 0x"VXGE_OS_STXFMT, (ptr_t)channel);

	vxge_assert(channel->pdev);

	channel->compl_index = 0;
	channel->reserve_index = 0;

	for (i = 0; i < channel->length; i++) {
		channel->dtr_arr[i].state =
		    VXGE_HAL_CHANNEL_DTR_FREE;
	}

	vxge_hal_trace_log_channel(hldev,
	    ((__vxge_hal_vpath_handle_t *)channel->vph)->vpath->vp_id,
	    "<==  %s:%s:%d  Result: 0", __FILE__, __func__, __LINE__);

	return (VXGE_HAL_OK);
}

/*
 * __vxge_hal_channel_terminate - Deinitializes a channel
 * @channel: channel to be deinitialized
 *
 * This function deinitializes a channel by properly
 *		setting the various references
 */
void
__vxge_hal_channel_terminate(
	__vxge_hal_channel_t *channel)
{
	__vxge_hal_virtualpath_t *vpath =
	    ((__vxge_hal_vpath_handle_t *)channel->vph)->vpath;
	/*LINTED*/
	__vxge_hal_device_t *hldev;

	vxge_assert(channel != NULL);

	hldev = (__vxge_hal_device_t *)channel->devh;

	vxge_hal_trace_log_channel(hldev, vpath->vp_id,
	    "==>  %s:%s:%d", __FILE__, __func__, __LINE__);

	vxge_hal_trace_log_channel(hldev, vpath->vp_id,
	    "channel = 0x"VXGE_OS_STXFMT, (ptr_t)channel);

	vpath = (__vxge_hal_virtualpath_t *)
	    ((__vxge_hal_vpath_handle_t *)channel->vph)->vpath;

	vxge_assert(vpath != NULL);

	switch (channel->type) {
		case VXGE_HAL_CHANNEL_TYPE_FIFO:
			vpath->fifoh = 0;
			break;
		case VXGE_HAL_CHANNEL_TYPE_RING:
			vpath->ringh = 0;
			break;
		case VXGE_HAL_CHANNEL_TYPE_SEND_QUEUE:
			vxge_list_remove(&channel->item);
			vpath->sw_stats->obj_counts.no_sqs--;
			break;
		case VXGE_HAL_CHANNEL_TYPE_RECEIVE_QUEUE:
			vxge_list_remove(&channel->item);
			vpath->sw_stats->obj_counts.no_srqs--;
			break;
		case VXGE_HAL_CHANNEL_TYPE_COMPLETION_QUEUE:
			vxge_list_remove(&channel->item);
			vpath->sw_stats->obj_counts.no_cqrqs--;
			break;
		case VXGE_HAL_CHANNEL_TYPE_UP_MESSAGE_QUEUE:
			vpath->umqh = 0;
			break;
		case VXGE_HAL_CHANNEL_TYPE_DOWN_MESSAGE_QUEUE:
			vpath->dmqh = 0;
			break;
		default:
			break;
	}

	vxge_hal_trace_log_channel(hldev, vpath->vp_id,
	    "<==  %s:%s:%d  Result: 0", __FILE__, __func__, __LINE__);
}

void __vxge_hal_channel_init_pending_list(
		vxge_hal_device_h devh)
{
	__vxge_hal_device_t *hldev = (__vxge_hal_device_t *)devh;

	vxge_assert(devh != NULL);

	vxge_hal_trace_log_channel(hldev, NULL_VPID,
	    "==>  %s:%s:%d", __FILE__, __func__, __LINE__);

	vxge_hal_trace_log_channel(hldev, NULL_VPID,
	    "devh = 0x"VXGE_OS_STXFMT, (ptr_t)devh);

	vxge_list_init(&hldev->pending_channel_list);

#if defined(VXGE_HAL_VP_CHANNELS)
	vxge_os_spin_lock_init(&hldev->pending_channel_lock, hldev->pdev);
#elif defined(VXGE_HAL_VP_CHANNELS_IRQ)
	vxge_os_spin_lock_init_irq(&hldev->pending_channel_lock, hldev->irqh);
#endif
	vxge_hal_trace_log_channel(hldev, NULL_VPID,
	    "<==  %s:%s:%d  Result: 0", __FILE__, __func__, __LINE__);
}

void __vxge_hal_channel_insert_pending_list(
		__vxge_hal_channel_t *channel)
{
	__vxge_hal_device_t *hldev = (__vxge_hal_device_t *)channel->devh;

	vxge_assert(channel != NULL);

	vxge_hal_trace_log_channel(hldev,
	    ((__vxge_hal_vpath_handle_t *)channel->vph)->vpath->vp_id,
	    "==>  %s:%s:%d", __FILE__, __func__, __LINE__);

	vxge_hal_trace_log_channel(hldev,
	    ((__vxge_hal_vpath_handle_t *)channel->vph)->vpath->vp_id,
	    "channel = 0x"VXGE_OS_STXFMT, (ptr_t)channel);

#if defined(VXGE_HAL_PENDING_CHANNELS)
	vxge_os_spin_lock(&hldev->pending_channel_lock);
#elif defined(VXGE_HAL_PENDING_CHANNELS_IRQ)
	vxge_os_spin_lock_irq(&hldev->pending_channel_lock, flags);
#endif

	vxge_list_insert_before(&channel->item, &hldev->pending_channel_list);

#if defined(VXGE_HAL_PENDING_CHANNELS)
	vxge_os_spin_unlock(&hldev->pending_channel_lock);
#elif defined(VXGE_HAL_PENDING_CHANNELS_IRQ)
	vxge_os_spin_unlock_irq(&hldev->pending_channel_lock, flags);
#endif

	__vxge_hal_channel_process_pending_list(channel->devh);

	vxge_hal_trace_log_channel(hldev,
	    ((__vxge_hal_vpath_handle_t *)channel->vph)->vpath->vp_id,
	    "<==  %s:%s:%d  Result: 0", __FILE__, __func__, __LINE__);
}

void __vxge_hal_channel_process_pending_list(
	vxge_hal_device_h devh)
{
	vxge_hal_status_e status;
	__vxge_hal_channel_t *channel;
	__vxge_hal_device_t *hldev = (__vxge_hal_device_t *)devh;

	vxge_assert(devh != NULL);

	vxge_hal_trace_log_channel(hldev, NULL_VPID,
	    "==>  %s:%s:%d", __FILE__, __func__, __LINE__);

	vxge_hal_trace_log_channel(hldev, NULL_VPID,
	    "devh = 0x"VXGE_OS_STXFMT, (ptr_t)devh);

	for (;;) {
#if defined(VXGE_HAL_PENDING_CHANNELS)
		vxge_os_spin_lock(&hldev->pending_channel_lock);
#elif defined(VXGE_HAL_PENDING_CHANNELS_IRQ)
		vxge_os_spin_lock_irq(&hldev->pending_channel_lock, flags);
#endif

		channel = (__vxge_hal_channel_t *)
		    vxge_list_first_get(&hldev->pending_channel_list);

		if (channel != NULL)
			vxge_list_remove(&channel->item);

#if defined(VXGE_HAL_PENDING_CHANNELS)
		vxge_os_spin_unlock(&hldev->pending_channel_lock);
#elif defined(VXGE_HAL_PENDING_CHANNELS_IRQ)
		vxge_os_spin_unlock_irq(&hldev->pending_channel_lock, flags);
#endif

		if (channel == NULL) {
			vxge_hal_trace_log_channel(hldev, NULL_VPID,
			    "<==  %s:%s:%d  Result: 0",
			    __FILE__, __func__, __LINE__);
			return;
		}

		switch (channel->type) {
		default:
			status = VXGE_HAL_OK;
			break;
		}

		if (status == VXGE_HAL_ERR_OUT_OF_MEMORY) {
#if defined(VXGE_HAL_PENDING_CHANNELS)
		vxge_os_spin_lock(&hldev->pending_channel_lock);
#elif defined(VXGE_HAL_PENDING_CHANNELS_IRQ)
		vxge_os_spin_lock_irq(&hldev->pending_channel_lock, flags);
#endif

			vxge_list_insert(&channel->item,
			    &hldev->pending_channel_list);

#if defined(VXGE_HAL_PENDING_CHANNELS)
		vxge_os_spin_unlock(&hldev->pending_channel_lock);
#elif defined(VXGE_HAL_PENDING_CHANNELS_IRQ)
		vxge_os_spin_unlock_irq(&hldev->pending_channel_lock, flags);
#endif
			vxge_hal_trace_log_channel(hldev, NULL_VPID,
			    "<==  %s:%s:%d  Result: 0",
			    __FILE__, __func__, __LINE__);

			return;
		}

	}
}

void __vxge_hal_channel_destroy_pending_list(
	vxge_hal_device_h devh)
{
	vxge_list_t *p, *n;
	__vxge_hal_device_t *hldev = (__vxge_hal_device_t *)devh;

	vxge_assert(devh != NULL);

	vxge_hal_trace_log_channel(hldev, NULL_VPID,
	    "==>  %s:%s:%d", __FILE__, __func__, __LINE__);

	vxge_hal_trace_log_channel(hldev, NULL_VPID,
	    "devh = 0x"VXGE_OS_STXFMT, (ptr_t)devh);

	vxge_list_for_each_safe(p, n, &hldev->pending_channel_list) {

		vxge_list_remove(p);

		switch (((__vxge_hal_channel_t *)p)->type) {
		default:
			break;
		}

	}

#if defined(VXGE_HAL_PENDING_CHANNELS)
	vxge_os_spin_lock_destroy(&hldev->pending_channel_lock,
	    hldev->header.pdev);
#elif defined(VXGE_HAL_PENDING_CHANNELS_IRQ)
	vxge_os_spin_lock_destroy_irq(&hldev->pending_channel_lock,
	    hldev->header.pdev);
#endif
	vxge_hal_trace_log_channel(hldev, NULL_VPID,
	    "<==  %s:%s:%d  Result: 0", __FILE__, __func__, __LINE__);
}
/*
 * __vxge_hal_channel_dtr_free - Frees a dtr
 * @channelh: Channel
 * @index:  Index of DTR
 *
 * Returns the dtr to free array
 *
 */

void
__vxge_hal_channel_dtr_free(__vxge_hal_channel_t *channel, u32 index)
{
	channel->dtr_arr[index].state =
	    VXGE_HAL_CHANNEL_DTR_FREE;
}
/*
 * __vxge_hal_channel_dtr_complete - Removes next completed dtr from
 * the work array
 * @channelh: Channel
 *
 * Removes the next completed dtr from work array
 *
 */
void
__vxge_hal_channel_dtr_complete(__vxge_hal_channel_t *channel)
{
	channel->dtr_arr[channel->compl_index].state =
	    VXGE_HAL_CHANNEL_DTR_COMPLETED;

	if (++channel->compl_index == channel->length)
		channel->compl_index = 0;

	channel->stats->total_compl_cnt++;
}
/*
 * __vxge_hal_channel_dtr_try_complete - Returns next completed dtr
 * @channelh: Channel
 * @dtr: Buffer to return the next completed DTR pointer
 *
 * Returns the next completed dtr with out removing it from work array
 *
 */

void
__vxge_hal_channel_dtr_try_complete(__vxge_hal_channel_t *channel,
	__vxge_hal_dtr_h *dtrh)
{
	vxge_assert(channel->dtr_arr);
	vxge_assert(channel->compl_index < channel->length);

	if (channel->dtr_arr[channel->compl_index].state ==
	    VXGE_HAL_CHANNEL_DTR_POSTED)
		*dtrh = channel->dtr_arr[channel->compl_index].dtr;
	else
		*dtrh = NULL;
}
/*
 * __vxge_hal_channel_dtr_post - Post a dtr to the channel
 * @channelh: Channel
 * @dtr: DTR pointer
 *
 * Posts a dtr to work array.
 *
 */
void
__vxge_hal_channel_dtr_post(__vxge_hal_channel_t *channel, u32 index)
{
	channel->dtr_arr[index].state =
	    VXGE_HAL_CHANNEL_DTR_POSTED;
}
/*
 * __vxge_hal_channel_dtr_restore - Restores a dtr to the channel
 * @channelh: Channel
 * @dtr: DTR pointer
 *
 * Returns a dtr back to reserve array.
 *
 */
void
__vxge_hal_channel_dtr_restore(__vxge_hal_channel_t *channel,
	__vxge_hal_dtr_h dtrh)
{
	/*LINTED*/
	__vxge_hal_device_t *hldev = (__vxge_hal_device_t *)channel->devh;
	u32 index;

	/*
	 * restore a previously allocated dtrh at current offset and update
	 * the available reserve length accordingly. If dtrh is null just
	 * update the reserve length, only
	 */

	if (channel->reserve_index == 0)
		index = channel->length;
	else
		index = channel->reserve_index - 1;

	if ((channel->dtr_arr[index].dtr == dtrh)) {

		channel->reserve_index = index;
		channel->dtr_arr[index].state = VXGE_HAL_CHANNEL_DTR_FREE;

		vxge_hal_info_log_channel(hldev,
		    ((__vxge_hal_vpath_handle_t *)channel->vph)->vpath->vp_id,
		    "dtrh 0x"VXGE_OS_STXFMT" restored for "
		    "channel %d at reserve index %d, ",
		    (ptr_t)dtrh, channel->type,
		    channel->reserve_index);
	}
}

/*
 * __vxge_hal_channel_dtr_reserve    - Reserve a dtr from the channel
 * @channelh: Channel
 * @dtrh: Buffer to return the DTR pointer
 *
 * Reserve a dtr from the reserve array.
 *
 */
vxge_hal_status_e
__vxge_hal_channel_dtr_reserve(__vxge_hal_channel_t *channel,
	__vxge_hal_dtr_h *dtrh)
{
	/* __vxge_hal_device_t *hldev = (__vxge_hal_device_t *)channel->devh; */
	vxge_hal_status_e status = VXGE_HAL_INF_OUT_OF_DESCRIPTORS;

	*dtrh = NULL;

	if (channel->dtr_arr[channel->reserve_index].state ==
	    VXGE_HAL_CHANNEL_DTR_FREE) {

		*dtrh = channel->dtr_arr[channel->reserve_index].dtr;

		channel->dtr_arr[channel->reserve_index].state =
		    VXGE_HAL_CHANNEL_DTR_RESERVED;

		if (++channel->reserve_index == channel->length)
			channel->reserve_index = 0;

			status = VXGE_HAL_OK;

	} else {


		vxge_hal_info_log_channel(hldev,
		    ((__vxge_hal_vpath_handle_t *)channel->vph)->vpath->vp_id,
		    "channel %d is full!", channel->type);

		channel->stats->full_cnt++;

	}

	return (status);
}
