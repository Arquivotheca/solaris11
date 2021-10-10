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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copyright Exar 2010. Copyright (c) 2002-2010 Neterion, Inc.
 * All right Reserved.
 *
 * FileName :	vxgehal-fifo.c
 *
 * Description:  fifo object implementation
 *
 * Created:	  23 January 2006
 */

#include "vxgehal.h"
#include <sys/strsun.h>


/*
 * vxge_hal_ring_rxd_size_get	- Get the size of ring descriptor.
 * @buf_mode: Buffer mode (1 , 3 or 5)
 *
 * This function returns the size of RxD for given buffer mode
 */
u32 vxge_hal_ring_rxd_size_get(
			u32 buf_mode)
{
	return ((buf_mode == 1 ? sizeof (vxge_hal_ring_rxd_1_t) : \
	    (buf_mode == 3 ? sizeof (vxge_hal_ring_rxd_3_t) : \
	    sizeof (vxge_hal_ring_rxd_5_t))));

}

/*
 * vxge_hal_ring_rxds_per_block_get - Get the number of rxds per block.
 * @buf_mode: Buffer mode (1 , 3 or 5)
 *
 * This function returns the number of RxD for RxD block for given buffer mode
 */
u32 vxge_hal_ring_rxds_per_block_get(
			u32 buf_mode)
{
	return ((u32)((VXGE_OS_HOST_PAGE_SIZE-16) /
	    ((buf_mode == 1) ? sizeof (vxge_hal_ring_rxd_1_t) :
	    ((buf_mode == 3) ? sizeof (vxge_hal_ring_rxd_3_t) :
	    sizeof (vxge_hal_ring_rxd_5_t)))));
}

/*
 * vxge_hal_ring_rxd_1b_set - Prepare 1-buffer-mode descriptor.
 * @rxdh: Descriptor handle.
 * @dma_pointer: DMA address of	a single receive buffer	this descriptor
 *				should	carry. Note that by the	time
 *				vxge_hal_ring_rxd_1b_set is called, the
 *				  receive buffer should be already mapped
 *				to the	corresponding X3100 device.
 * @size: Size of the receive @dma_pointer buffer.
 *
 * Prepare 1-buffer-mode Rx	descriptor for posting
 * (via	vxge_hal_ring_rxd_post()).
 *
 * This	inline helper-function does not	return any parameters and always
 * succeeds.
 *
 */
void vxge_hal_ring_rxd_1b_set(
			vxge_hal_rxd_h rxdh,
			dma_addr_t dma_pointer,
			u32 size)
{
	vxge_hal_ring_rxd_1_t *rxdp = (vxge_hal_ring_rxd_1_t *)rxdh;
	rxdp->buffer0_ptr = dma_pointer;
	rxdp->control_1	&= ~VXGE_HAL_RING_RXD_1_BUFFER0_SIZE_MASK;
	rxdp->control_1	|= VXGE_HAL_RING_RXD_1_BUFFER0_SIZE(size);
}

/*
 * vxge_hal_ring_rxd_3b_set - Prepare 3-buffer-mode descriptor.
 * @rxdh: Descriptor handle.
 * @dma_pointers: Array	of DMA addresses. Contains exactly 3 receive buffers
 *		  _this_ descriptor should carry. Note that by the time
 *		vxge_hal_ring_rxd_3b_set is called, the receive	buffers	should
 *		be mapped to the	corresponding X3100 device.
 * @sizes: Array of receive buffer sizes. Contains 3 sizes: one size per
 *		  buffer from @dma_pointers.
 *
 * Prepare 3-buffer-mode Rx descriptor for posting (via
 * vxge_hal_ring_rxd_post()).
 * This	inline helper-function does not	return any parameters and always
 * succeeds.
 *
 */
void vxge_hal_ring_rxd_3b_set(
			vxge_hal_rxd_h rxdh,
			dma_addr_t dma_pointers[],
			u32 sizes[])
{
	vxge_hal_ring_rxd_3_t *rxdp = (vxge_hal_ring_rxd_3_t *)rxdh;
	rxdp->buffer0_ptr = dma_pointers[0];
	rxdp->control_1	&= (~VXGE_HAL_RING_RXD_3_BUFFER0_SIZE_MASK);
	rxdp->control_1	|= VXGE_HAL_RING_RXD_3_BUFFER0_SIZE(sizes[0]);
	rxdp->buffer1_ptr = dma_pointers[1];
	rxdp->control_1	&= (~VXGE_HAL_RING_RXD_3_BUFFER1_SIZE_MASK);
	rxdp->control_1	|= VXGE_HAL_RING_RXD_3_BUFFER1_SIZE(sizes[1]);
	rxdp->buffer2_ptr = dma_pointers[2];
	rxdp->control_1	&= (~VXGE_HAL_RING_RXD_3_BUFFER2_SIZE_MASK);
	rxdp->control_1	|= VXGE_HAL_RING_RXD_3_BUFFER2_SIZE(sizes[2]);
}

/*
 * vxge_hal_ring_rxd_5b_set - Prepare 5-buffer-mode descriptor.
 * @rxdh: Descriptor handle.
 * @dma_pointers: Array	of DMA addresses. Contains exactly 5 receive buffers
 *		_this_ descriptor should carry. Note that by the time
 *		vxge_hal_ring_rxd_5b_set is called, the receive buffers should
 *		be mapped to the	corresponding X3100 device.
 * @sizes: Array of receive buffer sizes. Contains 5 sizes: one	size per buffer
 *		from @dma_pointers.
 *
 * Prepare 5-buffer-mode Rx descriptor for posting
 * (via vxge_hal_ring_rxd_post()).
 * This	inline helper-function does not	return any
 * values and always succeeds.
 *
 * See also: vxge_hal_ring_rxd_1b_set(), vxge_hal_ring_rxd_3b_set().
 */
void vxge_hal_ring_rxd_5b_set(
			vxge_hal_rxd_h rxdh,
			dma_addr_t dma_pointers[],
			u32 sizes[])

{
	vxge_hal_ring_rxd_5_t *rxdp = (vxge_hal_ring_rxd_5_t *)rxdh;
	rxdp->buffer0_ptr = dma_pointers[0];
	rxdp->control_1	&= (~VXGE_HAL_RING_RXD_5_BUFFER0_SIZE_MASK);
	rxdp->control_1	|= VXGE_HAL_RING_RXD_5_BUFFER0_SIZE(sizes[0]);
	rxdp->buffer1_ptr = dma_pointers[1];
	rxdp->control_1	&= (~VXGE_HAL_RING_RXD_5_BUFFER1_SIZE_MASK);
	rxdp->control_1	|= VXGE_HAL_RING_RXD_5_BUFFER1_SIZE(sizes[1]);
	rxdp->buffer2_ptr = dma_pointers[2];
	rxdp->control_1	&= (~VXGE_HAL_RING_RXD_5_BUFFER2_SIZE_MASK);
	rxdp->control_1	|= VXGE_HAL_RING_RXD_5_BUFFER2_SIZE(sizes[2]);
	rxdp->buffer3_ptr = dma_pointers[3];
	rxdp->control_2	&= (~VXGE_HAL_RING_RXD_5_BUFFER3_SIZE_MASK);
	rxdp->control_2	|= VXGE_HAL_RING_RXD_5_BUFFER3_SIZE(sizes[3]);
	rxdp->buffer4_ptr = dma_pointers[4];
	rxdp->control_2	&= (~VXGE_HAL_RING_RXD_5_BUFFER4_SIZE_MASK);
	rxdp->control_2 |= VXGE_HAL_RING_RXD_5_BUFFER4_SIZE(sizes[4]);
}

/*
 * vxge_hal_ring_rxd_1b_get - Get data from the completed 1-buf
 * descriptor.
 * @vpath_handle: Virtual Path handle.
 * @rxdh: Descriptor handle.
 * @dma_pointer: DMA address of	a single receive buffer	_this_ descriptor
 *				carries. Returned by HAL.
 * @pkt_length:	Length (in bytes) of the data in the buffer pointed	by
 *				@dma_pointer. Returned by HAL.
 *
 * Retrieve protocol data from the completed 1-buffer-mode Rx descriptor.
 * This	inline helper-function uses completed descriptor to populate receive
 * buffer pointer and other "out" parameters. The function always succeeds.
 *
 */
/*ARGSUSED*/
void vxge_hal_ring_rxd_1b_get(
			vxge_hal_vpath_h vpath_handle,
			vxge_hal_rxd_h rxdh,
			dma_addr_t *dma_pointer,
			u32 *pkt_length)
{
	vxge_hal_ring_rxd_1_t *rxdp = (vxge_hal_ring_rxd_1_t *)rxdh;

	*pkt_length =
	    (u32)VXGE_HAL_RING_RXD_1_BUFFER0_SIZE_GET(rxdp->control_1);
	*dma_pointer = rxdp->buffer0_ptr;
}

/*
 * vxge_hal_ring_rxd_3b_get - Get data from the completed 3-buf
 * descriptor.
 * @vpath_handle: Virtual Path handle.
 * @rxdh: Descriptor handle.
 * @dma_pointers: DMA addresses	of the 3 receive buffers _this_	descriptor
 *			carries. The first two buffers contain ethernet and
 *			(IP + transport) headers. The 3rd buffer contains packet
 *			data.
 * @sizes: Array of receive buffer sizes. Contains 3 sizes: one	size per
 * buffer from @dma_pointers. Returned by HAL.
 *
 * Retrieve	protocol data from the completed 3-buffer-mode Rx descriptor.
 * This	inline helper-function uses completed descriptor to populate receive
 * buffer pointer and other "out" parameters. The function always succeeds.
 *
 */
/*ARGSUSED*/
void vxge_hal_ring_rxd_3b_get(
			vxge_hal_vpath_h vpath_handle,
			vxge_hal_rxd_h rxdh,
			dma_addr_t dma_pointers[],
			u32 sizes[])
{
	vxge_hal_ring_rxd_3_t *rxdp = (vxge_hal_ring_rxd_3_t *)rxdh;

	dma_pointers[0]	= rxdp->buffer0_ptr;
	sizes[0] = (u32)VXGE_HAL_RING_RXD_3_BUFFER0_SIZE_GET(rxdp->control_1);

	dma_pointers[1]	= rxdp->buffer1_ptr;
	sizes[1] = (u32)VXGE_HAL_RING_RXD_3_BUFFER1_SIZE_GET(rxdp->control_1);

	dma_pointers[2]	= rxdp->buffer2_ptr;
	sizes[2] = (u32)VXGE_HAL_RING_RXD_3_BUFFER2_SIZE_GET(rxdp->control_1);
}

/*
 * vxge_hal_ring_rxd_5b_get - Get data from the completed 5-buf descriptor.
 * @vpath_handle: Virtual Path handle.
 * @rxdh: Descriptor handle.
 * @dma_pointers: DMA addresses	of the 5 receive buffers _this_	descriptor
 *		carries. The first 4 buffers contains L2 (ethernet) through
 *		  L5 headers. The 5th buffer contain received (applicaion)
 *		  data. Returned by HAL.
 * @sizes: Array of receive buffer sizes. Contains 5 sizes: one	size per
 * buffer from @dma_pointers. Returned by HAL.
 *
 * Retrieve	protocol data from the completed 5-buffer-mode Rx descriptor.
 * This	inline helper-function uses completed descriptor to populate receive
 * buffer pointer and other "out" parameters. The function always succeeds.
 *
 * See also: vxge_hal_ring_rxd_3b_get(),	vxge_hal_ring_rxd_5b_get().
 */
/*ARGSUSED*/
void vxge_hal_ring_rxd_5b_get(
			vxge_hal_vpath_h vpath_handle,
			vxge_hal_rxd_h rxdh,
			dma_addr_t dma_pointers[],
			int sizes[])
{
	vxge_hal_ring_rxd_5_t *rxdp = (vxge_hal_ring_rxd_5_t *)rxdh;

	dma_pointers[0]	= rxdp->buffer0_ptr;
	sizes[0] = (u32)VXGE_HAL_RING_RXD_5_BUFFER0_SIZE_GET(rxdp->control_1);

	dma_pointers[1]	= rxdp->buffer1_ptr;
	sizes[1] = (u32)VXGE_HAL_RING_RXD_5_BUFFER1_SIZE_GET(rxdp->control_1);

	dma_pointers[2]	= rxdp->buffer2_ptr;
	sizes[2] = (u32)VXGE_HAL_RING_RXD_5_BUFFER2_SIZE_GET(rxdp->control_1);

	dma_pointers[3]	= rxdp->buffer3_ptr;
	sizes[3] = (u32)VXGE_HAL_RING_RXD_5_BUFFER3_SIZE_GET(rxdp->control_2);

	dma_pointers[4]	= rxdp->buffer4_ptr;
	sizes[4] = (u32)VXGE_HAL_RING_RXD_5_BUFFER3_SIZE_GET(rxdp->control_2);
}

/*
 * vxge_hal_ring_rxd_1b_info_get - Get extended information associated with
 *				  a completed receive descriptor for 1b mode.
 * @vpath_handle: Virtual Path handle.
 * @rxdh: Descriptor handle.
 * @rxd_info: Descriptor information
 *
 * Retrieve extended information associated with a completed receive descriptor.
 *
 */
/*ARGSUSED*/
void vxge_hal_ring_rxd_1b_info_get(
			vxge_hal_vpath_h vpath_handle,
			vxge_hal_rxd_h rxdh,
			vxge_hal_ring_rxd_info_t *rxd_info)
{
	vxge_hal_ring_rxd_1_t *rxdp = (vxge_hal_ring_rxd_1_t *)rxdh;
	rxd_info->syn_flag =
	    (u32)VXGE_HAL_RING_RXD_SYN_GET(rxdp->control_0);
	rxd_info->is_icmp =
	    (u32)VXGE_HAL_RING_RXD_IS_ICMP_GET(rxdp->control_0);
	rxd_info->fast_path_eligible =
	    (u32)VXGE_HAL_RING_RXD_FAST_PATH_ELIGIBLE_GET(rxdp->control_0);
	rxd_info->l3_cksum_valid =
	    (u32)VXGE_HAL_RING_RXD_L3_CKSUM_CORRECT_GET(rxdp->control_0);
	rxd_info->l3_cksum =
	    (u32)VXGE_HAL_RING_RXD_L3_CKSUM_GET(rxdp->control_0);
	rxd_info->l4_cksum_valid =
	    (u32)VXGE_HAL_RING_RXD_L4_CKSUM_CORRECT_GET(rxdp->control_0);
	rxd_info->l4_cksum =
	    (u32)VXGE_HAL_RING_RXD_L4_CKSUM_GET(rxdp->control_0);
	rxd_info->frame =
	    (u32)VXGE_HAL_RING_RXD_ETHER_ENCAP_GET(rxdp->control_0);
	rxd_info->proto =
	    (u32)VXGE_HAL_RING_RXD_FRAME_PROTO_GET(rxdp->control_0);
	rxd_info->is_vlan =
	    (u32)VXGE_HAL_RING_RXD_IS_VLAN_GET(rxdp->control_0);
	rxd_info->vlan =
	    (u32)VXGE_HAL_RING_RXD_VLAN_TAG_GET(rxdp->control_1);
	rxd_info->rth_bucket =
	    (u32)VXGE_HAL_RING_RXD_RTH_BUCKET_GET(rxdp->control_0);
	rxd_info->rth_it_hit =
	    (u32)VXGE_HAL_RING_RXD_RTH_IT_HIT_GET(rxdp->control_0);
	rxd_info->rth_spdm_hit =
	    (u32)VXGE_HAL_RING_RXD_RTH_SPDM_HIT_GET(rxdp->control_0);
	rxd_info->rth_hash_type =
	    (u32)VXGE_HAL_RING_RXD_RTH_HASH_TYPE_GET(rxdp->control_0);
	rxd_info->rth_value =
	    (u32)VXGE_HAL_RING_RXD_1_RTH_HASH_VAL_GET(rxdp->control_1);
}

/*
 * vxge_hal_ring_rxd_3b_5b_info_get - Get extended information associated with
 *			    a completed receive descriptor for 3b & 5b mode.
 * @vpath_handle: Virtual Path handle.
 * @rxdh: Descriptor handle.
 * @rxd_info: Descriptor information
 *
 * Retrieve extended information associated with a completed receive descriptor.
 *
 */
/*ARGSUSED*/
void vxge_hal_ring_rxd_3b_5b_info_get(
			vxge_hal_vpath_h vpath_handle,
			vxge_hal_rxd_h rxdh,
			vxge_hal_ring_rxd_info_t *rxd_info)
{
	vxge_hal_ring_rxd_3_t *rxdp = (vxge_hal_ring_rxd_3_t *)rxdh;
	rxd_info->syn_flag =
	    (u32)VXGE_HAL_RING_RXD_SYN_GET(rxdp->control_0);
	rxd_info->is_icmp =
	    (u32)VXGE_HAL_RING_RXD_IS_ICMP_GET(rxdp->control_0);
	rxd_info->fast_path_eligible =
	    (u32)VXGE_HAL_RING_RXD_FAST_PATH_ELIGIBLE_GET(rxdp->control_0);
	rxd_info->l3_cksum_valid =
	    (u32)VXGE_HAL_RING_RXD_L3_CKSUM_CORRECT_GET(rxdp->control_0);
	rxd_info->l3_cksum =
	    (u32)VXGE_HAL_RING_RXD_L3_CKSUM_GET(rxdp->control_0);
	rxd_info->l4_cksum_valid =
	    (u32)VXGE_HAL_RING_RXD_L4_CKSUM_CORRECT_GET(rxdp->control_0);
	rxd_info->l4_cksum =
	    (u32)VXGE_HAL_RING_RXD_L4_CKSUM_GET(rxdp->control_0);
	rxd_info->frame =
	    (u32)VXGE_HAL_RING_RXD_ETHER_ENCAP_GET(rxdp->control_0);
	rxd_info->proto =
	    (u32)VXGE_HAL_RING_RXD_FRAME_PROTO_GET(rxdp->control_0);
	rxd_info->is_vlan =
	    (u32)VXGE_HAL_RING_RXD_IS_VLAN_GET(rxdp->control_0);
	rxd_info->vlan =
	    (u32)VXGE_HAL_RING_RXD_VLAN_TAG_GET(rxdp->control_1);
	rxd_info->rth_bucket =
	    (u32)VXGE_HAL_RING_RXD_RTH_BUCKET_GET(rxdp->control_0);
	rxd_info->rth_it_hit =
	    (u32)VXGE_HAL_RING_RXD_RTH_IT_HIT_GET(rxdp->control_0);
	rxd_info->rth_spdm_hit =
	    (u32)VXGE_HAL_RING_RXD_RTH_SPDM_HIT_GET(rxdp->control_0);
	rxd_info->rth_hash_type =
	    (u32)VXGE_HAL_RING_RXD_RTH_HASH_TYPE_GET(rxdp->control_0);
	rxd_info->rth_value = (u32)VXGE_HAL_RING_RXD_3_RTH_HASH_VALUE_GET(
	    rxdp->buffer0_ptr);
}

/*
 * vxge_hal_fifo_txdl_cksum_set_bits - Offload checksum.
 * @txdlh: Descriptor handle.
 * @cksum_bits: Specifies which checksums are to be offloaded: IPv4,
 *		 and/or TCP and/or UDP.
 *
 * Ask X3100 to calculate IPv4 & transport checksums for _this_ transmit
 * descriptor.
 * This API is part of the preparation of the transmit descriptor for posting
 * (via vxge_hal_fifo_txdl_post()). The related "preparation" APIs include
 * vxge_hal_fifo_txdl_mss_set(), vxge_hal_fifo_txdl_buffer_set_aligned(),
 * and vxge_hal_fifo_txdl_buffer_set().
 * All these APIs fill in the fields of the fifo descriptor,
 * in accordance with the X3100 specification.
 *
 */
void vxge_hal_fifo_txdl_cksum_set_bits(
			vxge_hal_txdl_h txdlh,
			u64 cksum_bits)
{
	vxge_hal_fifo_txd_t *txdp = (vxge_hal_fifo_txd_t *)txdlh;

	txdp->control_1 |= cksum_bits;

}

/*
 * vxge_hal_fifo_txdl_interrupt_type_set - Set the interrupt type for the txdl
 * @txdlh: Descriptor handle.
 * @interrupt_type: utiliz based interrupt or List interrupt
 *
 * vxge_hal_fifo_txdl_interrupt_type_set is used to set the interrupt type for
 * each xmit txdl dynamically
 */
void vxge_hal_fifo_txdl_interrupt_type_set(
			vxge_hal_txdl_h txdlh,
			u64 interrupt_type)
{
	vxge_hal_fifo_txd_t *txdp = (vxge_hal_fifo_txd_t *)txdlh;

	txdp->control_1 |= interrupt_type;
}

/*
 * vxge_hal_fifo_txdl_mss_set - Set MSS.
 * @txdlh: Descriptor handle.
 * @mss: MSS size for _this_ TCP connection. Passed by TCP stack down to the
 *	   ULD, which in turn inserts the MSS into the @txdlh.
 *
 * This API is part of the preparation of the transmit descriptor for posting
 * (via vxge_hal_fifo_txdl_post()). The related "preparation" APIs include
 * vxge_hal_fifo_txdl_buffer_set(), vxge_hal_fifo_txdl_buffer_set_aligned(),
 * and vxge_hal_fifo_txdl_cksum_set_bits().
 * All these APIs fill in the fields of the fifo descriptor,
 * in accordance with the X3100 specification.
 *
 */
void vxge_hal_fifo_txdl_mss_set(
			vxge_hal_txdl_h txdlh,
			int mss)
{
	vxge_hal_fifo_txd_t *txdp = (vxge_hal_fifo_txd_t *)txdlh;

	txdp->control_0 |= VXGE_HAL_FIFO_TXD_LSO_FLAG |
	    VXGE_HAL_FIFO_TXD_LSO_MSS(mss);
}

/*
 * vxge_hal_fifo_txdl_vlan_set - Set VLAN tag.
 * @txdlh: Descriptor handle.
 * @vlan_tag: 16bit VLAN tag.
 *
 * Insert VLAN tag into specified transmit descriptor.
 * The actual insertion of the tag into outgoing frame is done by the hardware.
 */
void vxge_hal_fifo_txdl_vlan_set(
			vxge_hal_txdl_h txdlh,
			u16 vlan_tag)
{
	vxge_hal_fifo_txd_t *txdp = (vxge_hal_fifo_txd_t *)txdlh;

	txdp->control_1 |= VXGE_HAL_FIFO_TXD_VLAN_ENABLE;
	txdp->control_1 |= VXGE_HAL_FIFO_TXD_VLAN_TAG(vlan_tag);
}
/*
 * vxge_hal_device_check_id - Verify device ID.
 * @devh: HAL device handle.
 *
 * Verify device ID.
 * Returns: one of the vxge_hal_card_e {} enumerated types.
 * See also: vxge_hal_card_e {}.
 */
vxge_hal_card_e vxge_hal_device_check_id(
			vxge_hal_device_h devh)
{
	vxge_hal_device_t *hldev = (vxge_hal_device_t *)devh;
	switch (hldev->device_id) {
	case VXGE_PCI_DEVICE_ID_TITAN_1:
		if (hldev->revision == VXGE_PCI_REVISION_TITAN_1)
			return (VXGE_HAL_CARD_TITAN_1);
		else if (hldev->revision == VXGE_PCI_REVISION_TITAN_1A)
			return (VXGE_HAL_CARD_TITAN_1A);
		else
			break;

	case VXGE_PCI_DEVICE_ID_TITAN_2:
		if (hldev->revision == VXGE_PCI_REVISION_TITAN_2)
			return (VXGE_HAL_CARD_TITAN_2);
		else
			break;
	default:
		break;
	}

	return (VXGE_HAL_CARD_UNKNOWN);
}

/*
 * vxge_hal_device_revision_get - Get Device revision number.
 * @devh: HAL device handle.
 *
 * Returns:	Device revision	number
 */
u32 vxge_hal_device_revision_get(
			vxge_hal_device_h devh)
{
	return (((vxge_hal_device_t *)devh)->revision);
}

/*
 * vxge_hal_device_bar0_get - Get BAR0 mapped address.
 * @devh: HAL device handle.
 *
 * Returns: BAR0 address of the	specified device.
 */
u8 *vxge_hal_device_bar0_get(
			vxge_hal_device_h devh)
{
	return (((vxge_hal_device_t *)devh)->bar0);
}

/*
 * vxge_hal_device_bar1_get - Get BAR1 mapped address.
 * @devh: HAL device handle.
 *
 * Returns:	BAR1 address of	the	specified device.
 */
u8 *vxge_hal_device_bar1_get(
			vxge_hal_device_h devh)
{
	return (((vxge_hal_device_t *)devh)->bar1);
}

/*
 * vxge_hal_device_bar2_get - Get BAR2 mapped address.
 * @devh: HAL device handle.
 *
 * Returns: BAR2 address of the	specified device.
 */
u8 *vxge_hal_device_bar2_get(
			vxge_hal_device_h devh)
{
	return (((vxge_hal_device_t *)devh)->bar2);
}

/*
 * vxge_hal_device_bar0_set - Set BAR0 mapped address.
 * @devh: HAL device handle.
 * @bar0: BAR0 mapped address.
 * * Set BAR0 address in the HAL device	object.
 */
void vxge_hal_device_bar0_set(
			vxge_hal_device_h devh,
			u8 *bar0)
{
	((vxge_hal_device_t *)devh)->bar0 = bar0;
}

/*
 * vxge_hal_device_bar1_set - Set BAR1 mapped address.
 * @devh: HAL device handle.
 * @bar1: BAR1 mapped address.
 *
 * Set BAR1 address in	the HAL Device Object.
 */
void vxge_hal_device_bar1_set(
			vxge_hal_device_h devh,
			u8 *bar1)
{
	((vxge_hal_device_t *)devh)->bar1 = bar1;
}

/*
 * vxge_hal_device_bar2_set - Set BAR2 mapped address.
 * @devh: HAL device handle.
 * @bar2: BAR2 mapped address.
 *
 * Set BAR2 address in	the HAL Device Object.
 */
void vxge_hal_device_bar2_set(
			vxge_hal_device_h devh,
			u8 *bar2)
{
	((vxge_hal_device_t *)devh)->bar2 = bar2;
}

/*
 * vxge_hal_device_pci_info_get - Get PCI bus informations such as width,
 *			frequency, and mode from previously stored values.
 * @devh:		HAL device handle.
 * @signalling_rate:	pointer to a variable of enumerated type
 *			vxge_hal_pci_e_signalling_rate_e {}.
 * @link_width:		pointer to a variable of enumerated type
 *			vxge_hal_pci_e_link_width_e {}.
 *
 * Get pci-e signalling rate and link width.
 *
 * Returns: one of the vxge_hal_status_e {} enumerated types.
 * VXGE_HAL_OK			- for success.
 * VXGE_HAL_ERR_INVALID_DEVICE	- for invalid device handle.
 */
vxge_hal_device_link_state_e vxge_hal_device_link_state_get(
			vxge_hal_device_h devh)
{
	return (((vxge_hal_device_t *)devh)->link_state);
}

/*
 * vxge_hal_device_data_rate_get - Get data rate.
 * @devh: HAL device handle.
 *
 * Get data rate.
 * Returns: data rate(1G or 10G).
 */
vxge_hal_device_data_rate_e
vxge_hal_device_data_rate_get(
			vxge_hal_device_h devh)
{
	return (((vxge_hal_device_t *)devh)->data_rate);
}

/*
 * vxge_hal_device_terminating - Mark the device as 'terminating'.
 * @devh: HAL device handle.
 *
 * Mark the device as 'terminating', going to terminate. Can be used
 * to serialize termination with other running processes/contexts.
 *
 * See also: vxge_hal_device_terminate().
 */
void
vxge_hal_device_terminating(vxge_hal_device_h devh)
{
	((vxge_hal_device_t *)devh)->terminating = 1;
}

/*
 * vxge_hal_device_private_set - Set ULD context.
 * @devh: HAL device handle.
 * @data: pointer to ULD context
 *
 * Use HAL device to set upper-layer driver (ULD) context.
 *
 * See also: vxge_hal_device_private_get()
 */
void
vxge_hal_device_private_set(vxge_hal_device_h devh, void *data)
{
	((vxge_hal_device_t *)devh)->upper_layer_data = data;
}

/*
 * vxge_hal_device_private_get - Get ULD context.
 * @devh: HAL device handle.
 *
 * Use HAL device to set upper-layer driver (ULD) context.
 *
 * See also: vxge_hal_device_private_get()
 */
void
*vxge_hal_device_private_get(
			vxge_hal_device_h devh)
{
	return (((vxge_hal_device_t *)devh)->upper_layer_data);
}

/*
 * vxge_hal_device_msix_mode - Is MSIX enabled?
 * @devh: HAL device handle.
 *
 * Returns 0 if MSIX is enabled for the specified device,
 * non-zero otherwise.
 */
int
vxge_hal_device_msix_mode(vxge_hal_device_h devh)
{
	return (((vxge_hal_device_t *)devh)->msix_enabled);
}

/*
 * vxge_hal_device_is_traffic_interrupt
 * @reason: The reason returned by the vxge)hal_device_begin_irq
 * @vp_id: Id of vpath for which to check the interrupt
 *
 * Returns non-zero if traffic interrupt raised, 0 otherwise
 */
u64
vxge_hal_device_is_traffic_interrupt(u64 reason, u32 vp_id)
{
	return (reason & mBIT(vp_id+3));
}

/*
 * vxge_hal_fifo_txdl_lso_bytes_sent - Get the lso bytes sent.
 * @txdlh: Descriptor handle.
 *
 * Returns the lso bytes sent
 */
u32 vxge_hal_fifo_txdl_lso_bytes_sent(
		vxge_hal_txdl_h txdlh)
{
	vxge_hal_fifo_txd_t *txdp = (vxge_hal_fifo_txd_t *)txdlh;

	return (u32)VXGE_HAL_FIFO_TXD_LSO_BYTES_SENT_GET(txdp->control_0);
}


/*
 * vxge_hal_fifo_txdl_mss_set - Set MSS.
 * @txdlh: Descriptor handle.
 * @mss: MSS size for _this_ TCP connection. Passed by TCP stack down to the
 *         ULD, which in turn inserts the MSS into the @txdlh.
 *
 * This API is part of the preparation of the transmit descriptor for posting
 * (via vxge_hal_fifo_txdl_post()). The related "preparation" APIs include
 * vxge_hal_fifo_txdl_buffer_set(), vxge_hal_fifo_txdl_buffer_set_aligned(),
 * and vxge_hal_fifo_txdl_cksum_set_bits().
 * All these APIs fill in the fields of the fifo descriptor,
 * in accordance with the X3100 specification.
 *
 */

/*
 * void vxge_hal_fifo_txdl_mss_set(
 *                       vxge_hal_txdl_h txdlh,
 *                       int mss);
 * #pragma inline(vxge_hal_fifo_txdl_mss_set)
 */

/*
 * vxge_hal_fifo_txdl_lso_set - Set LSO Parameters.
 * @txdlh: Descriptor handle.
 * @encap: LSO Encapsulation
 * @mss: MSS size for LSO.
 *
 * This API is part of the preparation of the transmit descriptor for posting
 * (via vxge_hal_fifo_txdl_post()). The related "preparation" APIs include
 * vxge_hal_fifo_txdl_buffer_set(), vxge_hal_fifo_txdl_buffer_set_aligned(),
 * and vxge_hal_fifo_txdl_cksum_set_bits().
 * All these APIs fill in the fields of the fifo descriptor,
 * in accordance with the X3100 specification.
 *
 */
void vxge_hal_fifo_txdl_lso_set(
		vxge_hal_txdl_h txdlh,
		u32 encap,
		u32 mss)
{
	vxge_hal_fifo_txd_t *txdp = (vxge_hal_fifo_txd_t *)txdlh;

	txdp->control_0 |= VXGE_HAL_FIFO_TXD_LSO_FRM_ENCAP(encap) |
	    VXGE_HAL_FIFO_TXD_LSO_FLAG | VXGE_HAL_FIFO_TXD_LSO_MSS(mss);
}
