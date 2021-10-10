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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/conf.h>
#include <sys/esunddi.h>
#include <sys/stream.h>
#include <sys/strsubr.h>
#include <sys/mac.h>
#include <sys/mac_client_impl.h>
#include <sys/mac_client_priv.h>
#include <sys/mac_provider.h>
#include <sys/mac_provider_priv.h>
#include <sys/mac_impl.h>
#include <sys/thread.h>
#include <sys/fm/io/ddi.h>
#include <sys/time.h>
#include <sys/sdt.h>

/*
 * MAC Layer Buffer Management
 */

/*
 * Local Data
 */
static kmem_cache_t *mac_block_cachep = NULL;
static kmem_cache_t *mac_descriptor_cachep = NULL;
static kmem_cache_t *mac_packet_cachep = NULL;
static kmem_cache_t *mac_packet_pool_cachep = NULL;
uint32_t mac_packet_recycle_threshold = MAC_PACKET_RECYCLE_THRESHOLD;

/*
 * Conserve IO resources such that we don't arbitraily double the number
 * of buffers a ring can allocated, if there are more than mac_bm_rings_adj
 * rings for the given interface.
 */
int mac_bm_rings_adj = 8;

/*
 * Local functions
 */
static void mac_packet_pool_init(mac_packet_pool_t *);
static void mac_packet_pool_fini(mac_packet_pool_t *);
static mblk_t *i_mac_packet_alloc(mac_handle_t,
    mac_ring_handle_t, size_t, mac_block_t **);
static void i_mac_packet_pool_cleanup(mac_packet_pool_t *);
static void i_mac_packet_pool_destroy(mac_packet_pool_t *);
static void i_mac_packet_pool_add(mac_packet_list_t *, mblk_t *);
static boolean_t i_mac_packet_pool_recycle_add(mac_packet_list_t *, mblk_t *);
static boolean_t i_mac_packet_destroy(mac_packet_pool_t *, mac_packet_t *);
static void i_mac_packet_unbind(mac_packet_t *);
static void i_mac_packet_pool_move(mac_packet_list_t *, mac_packet_list_t *);
static void mac_mem_ref_inc(mac_impl_t *);
static boolean_t mac_mem_ref_dec(mac_impl_t *);
static void mac_impl_free(mac_impl_t *);
static void i_mac_block_unbind(mac_block_t *);

/*
 * Define the pragma for the functions to be inlined.
 */
#if !defined(DEBUG)
#pragma inline(i_mac_packet_pool_recycle_add)
#pragma inline(i_mac_packet_pool_move)
#endif

#define	MAC_BM_CACHE_ALIGN	0x40

/*
 * Typically buffer_size is 2K for regular 1500 byte MTU. To conserve
 * DVMA space on SPARC systems, we pack as many buffers in a 8K page as
 * possible provided the alignment requirements specified by the driver
 * permit packing.
 */
#if defined(__sparc)
int	bm_do_buffer_packing = 1;
#else
int	bm_do_buffer_packing = 0;
#endif

/*
 * 'mac_bm_huge_mem' can be changed independently to a more appropriate value
 * without impacting other uses of PM_HUGE_MEM.
 */
size_t mac_bm_huge_mem = PM_HUGE_MEM;

/*
 * Initialize buffer management data structures.
 */
void
mac_buffermgmt_init()
{
	mac_block_cachep = kmem_cache_create("mac_block_cache",
	    sizeof (mac_block_t), MAC_BM_CACHE_ALIGN,
	    NULL, NULL, NULL, NULL, NULL, 0);
	ASSERT(mac_block_cachep != NULL);

	mac_descriptor_cachep = kmem_cache_create("mac_descriptor_cache",
	    sizeof (mac_descriptor_t), MAC_BM_CACHE_ALIGN,
	    NULL, NULL, NULL, NULL, NULL, 0);
	ASSERT(mac_descriptor_cachep != NULL);

	mac_packet_pool_cachep = kmem_cache_create("mac_packet_pool_cache",
	    sizeof (mac_packet_pool_t), MAC_BM_CACHE_ALIGN,
	    NULL, NULL, NULL, NULL, NULL, 0);
	ASSERT(mac_packet_pool_cachep != NULL);

	mac_packet_cachep = kmem_cache_create("mac_packet_cache",
	    sizeof (mac_packet_t), MAC_BM_CACHE_ALIGN,
	    NULL, NULL, NULL, NULL, NULL, 0);
	ASSERT(mac_packet_cachep != NULL);
}

/*
 * The following 2 functions are not in the data path. Hence we take the
 * less error prone approach of bzero'ing and initializing after allocation,
 * rather than depend on kmem constructors and destructors which requires
 * every field to have been reset to its initial value before the free.
 */
static void
mac_packet_pool_init(mac_packet_pool_t *pktpoolp)
{
	int i;

	bzero(pktpoolp, sizeof (mac_packet_pool_t));

	/*
	 * Initialize the locks for the pool.
	 */
	mutex_init(&pktpoolp->mpp_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&pktpoolp->mpp_nic_pool.mpl_lock, NULL, MUTEX_DEFAULT, NULL);
	for (i = 0; i < MAC_PACKET_RECYCLE_LISTS; i++) {
		mutex_init(&pktpoolp->mpp_recycle_pools[i].mpl_lock, NULL,
		    MUTEX_DEFAULT, NULL);
	}
}

static void
mac_packet_pool_fini(mac_packet_pool_t *pktpoolp)
{
	int	i;

	ASSERT(pktpoolp->mpp_nic_pool.mpl_cnt == 0);
	for (i = 0; i < MAC_PACKET_RECYCLE_LISTS; i++)
		ASSERT(pktpoolp->mpp_recycle_pools[i].mpl_cnt == 0);

	kmem_free(pktpoolp->mpp_list,
	    sizeof (mac_packet_t *) * pktpoolp->mpp_npackets);
	for (i = 0; i < MAC_PACKET_RECYCLE_LISTS; i++)
		mutex_destroy(&pktpoolp->mpp_recycle_pools[i].mpl_lock);

	mutex_destroy(&pktpoolp->mpp_nic_pool.mpl_lock);
	mutex_destroy(&pktpoolp->mpp_lock);
}

void
mac_buffermgmt_fini()
{
	kmem_cache_destroy(mac_block_cachep);
	kmem_cache_destroy(mac_descriptor_cachep);
	kmem_cache_destroy(mac_packet_pool_cachep);
	kmem_cache_destroy(mac_packet_cachep);
}

static int
i_mac_bm_ring_create_pools(mac_ring_t *ring)
{
	mac_impl_t	*mip = ring->mr_mip;
	mac_capab_bm_t	*bm;
	int 	err;

	/*
	 * Initalized buffers for the ring.
	 */
	if (ring->mr_type == MAC_RING_TYPE_RX) {
		if (mip->mi_bm_rx_enabled)
			bm = &mip->mi_bm_rx_cap;
		else
			return (0);
	} else if (ring->mr_type == MAC_RING_TYPE_TX) {
		if (mip->mi_bm_tx_enabled)
			bm = &mip->mi_bm_tx_cap;
		else
			return (0);
	}

	if (!(bm->mbm_flags & MAC_BM_DRIVER_MANAGES_BUFFERS)) {
		/*
		 * Create the descriptor handle for this packet pool.
		 */
		err = i_mac_descriptor_create(
		    (mac_handle_t)mip, (mac_ring_handle_t)ring,
		    bm->mbm_min_descriptors, bm->mbm_max_descriptors,
		    bm->mbm_desired_descriptors, bm->mbm_descriptor_size);
		if (err != 0)
			return (err);

		ASSERT(ring->mr_packet_descriptor != NULL);

		/*
		 * Create the packet buffer pool for the ring.
		 */
		err = i_mac_packet_pool_create(
		    (mac_handle_t)mip, (mac_ring_handle_t)ring,
		    bm->mbm_min_descriptors, bm->mbm_max_descriptors,
		    bm->mbm_desired_buffers, bm->mbm_buffer_size,
		    bm->mbm_offset);
		if (err != 0)
			return (err);

		ASSERT(ring->mr_packet_pool != NULL);
	}

	return (0);
}

static void
i_mac_bm_ring_destroy_pools(mac_ring_t *ring)
{
	/*
	 * Destroy the packet pool cache, if it exists.
	 */
	if (ring->mr_packet_pool) {
		i_mac_packet_pool_cleanup(ring->mr_packet_pool);
		ring->mr_packet_pool = NULL;
	}

	/*
	 * Destroy the packet descriptor memory, if it exists.
	 */
	if (ring->mr_packet_descriptor) {
		i_mac_descriptor_destroy(ring->mr_packet_descriptor);
		ring->mr_packet_descriptor = NULL;
	}

#if defined(__sparc)
	if (ring->mr_contig_mem != NULL) {
		mac_capab_bm_t	*bm;
		mac_impl_t	*mip;

		ASSERT(ring->mr_type == MAC_RING_TYPE_TX);
		mip = ring->mr_mip;
		bm = &mip->mi_bm_tx_cap;
		ASSERT(bm->mbm_flags & MAC_BM_DRIVER_MANAGES_BUFFERS);
		ASSERT(mip->mi_bm_tx_block_cnt == 0);
		premapped_contig_mem_free(mip->mi_pmh_tx,
		    ring->mr_contig_mem, ring->mr_contig_mem_length);
		ring->mr_contig_mem = NULL;
		ring->mr_contig_mem_length = 0;
	}
#endif
}

static int
i_mac_bm_group_create_pools(mac_group_t *g)
{
	mac_ring_t	*ring;
	int		err;

	ring = g->mrg_rings;
	while (ring != NULL) {
		err = i_mac_bm_ring_create_pools(ring);
		if (err != 0)
			return (err);
		ring = ring->mr_next;
	}
	return (0);
}

static void
i_mac_bm_group_destroy_pools(mac_group_t *g)
{
	mac_ring_t	*ring;

	ring = g->mrg_rings;
	while (ring != NULL) {
		i_mac_bm_ring_destroy_pools(ring);
		ring = ring->mr_next;
	}
}

/*
 * Create Buffer Management Pools for all groups.
 */
int
mac_bm_create_pools(mac_impl_t *mip)
{
	mac_group_t	*g;
	char		cache_name[PM_MAX_NAME_LEN];
	boolean_t	phys_contig;
	mac_capab_bm_t	*bm;
	int		err;

	ASSERT(MAC_PERIM_HELD((mac_handle_t)mip));
	/*
	 * Get the devices Buffer Management capabilities.
	 */
	mip->mi_bm_rx_enabled = i_mac_capab_bm_get(mip, MAC_BUFFER_MGMT_RX);
	mip->mi_bm_tx_enabled = i_mac_capab_bm_get(mip, MAC_BUFFER_MGMT_TX);

	if (mip->mi_bm_rx_enabled) {
		/*
		 * Create the pools for the RX resources.
		 */
		g = mip->mi_rx_groups;
		while (g != NULL) {
			err = i_mac_bm_group_create_pools(g);
			if (err != 0)
				goto cleanup;
			g = g->mrg_next;
		}
	}

	/*
	 * The operation of the pre-mapped network dma buffer caches is
	 * explained in stream.c. The 'mi_pmh_tx' cache is used for
	 * packet allocations on the transmit side by the stack and it
	 * does not support packet alignment attributes.
	 * Physically contiguous memory (used by the NIU) implies a
	 * single contiguous memory cache for both stack allocated packets
	 * and the driver transmit DMA buffers.
	 */
	ASSERT(mip->mi_pmh_tx == NULL && mip->mi_pmh_tx_drv == NULL);
	if (mip->mi_bm_tx_enabled) {
		bm = &mip->mi_bm_tx_cap;
		if (bm->mbm_flags & MAC_BM_PREMAP_OK) {
			phys_contig = (bm->mbm_flags &
			    MAC_BM_PACKET_CONTIGUOUS);
			(void) snprintf(cache_name, sizeof (cache_name), "%s%s",
			    mip->mi_name, "_tx");

			(void) premapped_cache_create(cache_name,
			    phys_contig, bm->mbm_contig_mem_size, 0,
			    mip->mi_dip, bm->mbm_packet_attrp,
			    &mip->mi_pmh_tx);

			if (phys_contig)
				mip->mi_pmh_tx_drv = mip->mi_pmh_tx;
		}
		/*
		 * Create the pools for the tx resources.
		 */
		g = mip->mi_tx_groups;
		while (g != NULL) {
			err = i_mac_bm_group_create_pools(g);
			if (err != 0)
				goto cleanup;
			g = g->mrg_next;
		}
	}

	return (0);

cleanup:
	mac_bm_destroy_pools(mip);
	return (err);
}

static void
i_mac_bm_group_offline_pools_check(mac_group_t *g)
{
	mac_ring_t *ring;

	/*
	 * This is just a sanity check that the pools for the RX resources
	 * are really offline. The mpp_offline flag was set at mac_stop() time.
	 * The flag is used in i_mac_packet_freeb() to determine whether a
	 * packet that is freed by the stack or consumers should be recycled
	 * or whether the packet should really be destroyed.
	 *
	 * Since the mpp_offline flag is set at mac_stop() time and we are
	 * being called from mac_unregister() which is subsequent to mac_stop()
	 * we don't need any lock or perimeter to check the flag.
	 */
	ring = g->mrg_rings;
	while (ring != NULL) {
		ASSERT(ring->mr_packet_pool == NULL ||
		    ring->mr_packet_pool->mpp_offline == B_TRUE);
		ring = ring->mr_next;
	}
}

void
mac_bm_offline_pools_check(mac_impl_t *mip)
{
	mac_group_t	*g;

	g = mip->mi_rx_groups;
	while (g != NULL) {
		i_mac_bm_group_offline_pools_check(g);
		g = g->mrg_next;
	}

	g = mip->mi_tx_groups;
	while (g != NULL) {
		i_mac_bm_group_offline_pools_check(g);
		g = g->mrg_next;
	}
}

/*
 * Destroy Buffer Management Pools for all groups.
 */
void
mac_bm_destroy_pools(mac_impl_t *mip)
{
	list_t	*list;
	mac_group_t	*g;
	mac_capab_bm_t	*bm;
	mac_block_t	*dmap;

	ASSERT(MAC_PERIM_HELD((mac_handle_t)mip));

	bm = &mip->mi_bm_rx_cap;
	if (bm->mbm_flags & MAC_BM_DRIVER_MANAGES_BUFFERS) {
		mutex_enter(&mip->mi_bm_list_lock);

		/*
		 * We are here after the driver's m_stop() has completed.
		 * Some Rx blocks may be queued up in the protocol stack up.
		 * They will be DMA unbound now so that the mac_unregister()
		 * and the driver detach can succeed.
		 */
		list = &mip->mi_bm_rx_block_list;
		dmap = list_head(list);
		while (dmap != NULL) {
			i_mac_block_unbind(dmap);
			dmap = list_next(list, dmap);
		}

		/*
		 * All the Tx blocks must have been released by the driver
		 * in its m_stop().
		 */
		ASSERT(mip->mi_bm_tx_block_cnt == 0);

		mutex_exit(&mip->mi_bm_list_lock);
	}

	/*
	 * Destroy the pools for the RX resources.
	 */
	g = mip->mi_rx_groups;
	while (g != NULL) {
		i_mac_bm_group_destroy_pools(g);
		g = g->mrg_next;
	}

	/*
	 * Destroy the pools for the TX resources.
	 */
	g = mip->mi_tx_groups;
	while (g != NULL) {
		i_mac_bm_group_destroy_pools(g);
		g = g->mrg_next;
	}

	if (mip->mi_pmh_tx != NULL)
		premapped_cache_delete(mip->mi_pmh_tx);
	mip->mi_pmh_tx = NULL;
	mip->mi_pmh_tx_drv = NULL;

}

/* Called before freeing the mac_impl_t */
void
mac_bm_reset_values(mac_impl_t *mip)
{
	mac_capab_bm_t	*bm;

	mip->mi_bm_rx_enabled = B_FALSE;
	mip->mi_bm_tx_enabled = B_FALSE;

	bm = &mip->mi_bm_rx_cap;
	bzero(bm, sizeof (mac_capab_bm_t));

	bm = &mip->mi_bm_tx_cap;
	bzero(bm, sizeof (mac_capab_bm_t));

	list_destroy(&mip->mi_bm_tx_block_list);
	list_destroy(&mip->mi_bm_rx_block_list);
}

boolean_t
i_mac_capab_bm_get(mac_impl_t *mip, mac_buffer_mgmt_type_t type)
{
	mac_capab_bm_t	*bm;
	size_t	align;
	size_t	bufsize;
	size_t	albufsize;

	ASSERT(MAC_PERIM_HELD((mac_handle_t)mip));

	if (type == MAC_BUFFER_MGMT_RX)
		bm = &mip->mi_bm_rx_cap;
	else
		bm = &mip->mi_bm_tx_cap;

	bzero(bm, sizeof (*bm));

	/*
	 * Query the driver.
	 */
	bm->mbm_type = type;
	if (i_mac_capab_get((mac_handle_t)mip, MAC_CAPAB_BUFFER_MGMT, bm)) {
		ASSERT(bm->mbm_packet_attrp != NULL);
		ASSERT(bm->mbm_packet_accattrp != NULL);
		ASSERT(bm->mbm_descriptor_attrp != NULL);
		ASSERT(bm->mbm_descriptor_accattrp != NULL);
		ASSERT(bm->mbm_buffer_size != 0);
		if (!(bm->mbm_flags & MAC_BM_DRIVER_MANAGES_BUFFERS)) {
			ASSERT(bm->mbm_descriptor_size != 0);
			ASSERT(bm->mbm_min_descriptors != 0);
			ASSERT(bm->mbm_max_descriptors != 0);
			ASSERT(bm->mbm_desired_descriptors != 0);
			ASSERT(bm->mbm_desired_buffers != 0);

			/*
			 * For now, double the number of  buffers requested
			 * in order to have an active list of buffers and a
			 * free pool to replace loaned up buffers until
			 * they are returned to the mac layer.
			 *
			 * If there are more than mac_bm_ring_adj rings
			 * for the interface, then we will not double the
			 * number of buffers.
			 *
			 * A future project will address this issue in a more
			 * appropriate way in the future other than just
			 * doubling the number of buffers.
			 */
			if ((mip->mi_rx_rings_cap.mr_rnum <=
			    mac_bm_rings_adj) && (type == MAC_BUFFER_MGMT_RX)) {
				bm->mbm_desired_buffers <<= 1;
			}

			bufsize = bm->mbm_buffer_size;
			if (bm_do_buffer_packing) {
				align = bm->mbm_packet_attrp->dma_attr_align;
				albufsize = roundup(bufsize, align);

				/*
				 * Given that the life time of all the blocks
				 * is the same and from mac_start to mac_stop,
				 * it is a performance win to allocate a single
				 * large block instead of several scattered
				 * small blocks on large memory systems.
				 * Each block needs to have a single DMA cookie
				 * so this optimization can be done only on
				 * systems with DVMA. For now we limit
				 * it to sun4v systems. Platform specific code
				 * enables 'mac_bm_single_block_per_ring'.
				 */
				if (mac_bm_single_block_per_ring &&
				    physmem >= (mac_bm_huge_mem >> PAGESHIFT)) {
					bm->mbm_block_size = albufsize *
					    bm->mbm_desired_buffers;
				} else if (albufsize < PAGESIZE) {
					bm->mbm_block_size = PAGESIZE;
				} else {
					bm->mbm_block_size = bufsize;
				}
			} else {
				bm->mbm_block_size = bufsize;
			}
		}

		return (B_TRUE);
	}

	return (B_FALSE);
}

/*
 * Allocate a physical contiguous memory segment and then do the appropriate
 * DMA mappings of the allocated segment. For simplicity, we support only 1
 * contiguous allocation per ring.
 */
/* ARGSUSED */
static int
i_mac_dma_contig_alloc(mac_impl_t *mip, mac_ring_handle_t mrh, pm_handle_t pmh,
    mac_block_t *dmap, ddi_dma_attr_t *attrp, boolean_t streaming,
    size_t length)
{
#if defined(__sparc)
	int	ret;
	uint_t	flags;
	dev_info_t *dip;
	mac_ring_t *mr = (mac_ring_t *)mrh;
	void	*contig_mem;

	ASSERT(streaming);
	/*
	 * Get the dip.
	 */
	dip = mip->mi_dip;
	ASSERT(dip != NULL);

	/*
	 * Allocate a DMA handle for the DMA buffer.
	 */
	if (ddi_dma_alloc_handle(dip, attrp, DDI_DMA_DONTWAIT, NULL,
	    &dmap->mbm_dma_handle) != DDI_SUCCESS)
		return (ENOMEM);

	if (mr->mr_type == MAC_RING_TYPE_TX && pmh != NULL) {
		/*
		 * Allocate the contiguous pre-mapped memory. This is freed
		 * only at mac_stop time. We reuse the contig pre-mapped
		 * memory until then through ring start and stops.
		 */
		if (mr->mr_contig_mem == NULL) {
			contig_mem = premapped_contig_mem_alloc(pmh, length);
			mr->mr_contig_mem = contig_mem;
			mr->mr_contig_mem_length = length;
		} else {
			ASSERT(mr->mr_contig_mem_length == length &&
			    !mr->mr_contig_mem_in_use);
			contig_mem = mr->mr_contig_mem;
		}
		mr->mr_contig_mem_in_use = B_TRUE;
		dmap->mbm_mem_type = MAC_CONTIG_PREMAP_MEM;
	} else {
		contig_mem = contig_mem_alloc(length);
		dmap->mbm_mem_type = MAC_CONTIG_MEM;
	}

	dmap->mbm_kaddrp = contig_mem;
	if (dmap->mbm_kaddrp == NULL) {
		ddi_dma_free_handle(&dmap->mbm_dma_handle);
		dmap->mbm_dma_handle = NULL;
		return (ENOMEM);
	}
	dmap->mbm_length = length;

	/*
	 * Associate contiguous memory with DMA handles.
	 */
	flags =  DDI_DMA_RDWR | DDI_DMA_STREAMING;

	ret = ddi_dma_addr_bind_handle(dmap->mbm_dma_handle,
	    NULL, (caddr_t)dmap->mbm_kaddrp, length, flags, DDI_DMA_DONTWAIT,
	    0, &dmap->mbm_dma_cookie, &dmap->mbm_ncookies);
	if (ret != DDI_DMA_MAPPED) {
		ddi_dma_free_handle(&dmap->mbm_dma_handle);
		dmap->mbm_dma_handle = NULL;
		dmap->mbm_kaddrp = NULL;
		return (ENOMEM);
	}

	ASSERT(dmap->mbm_ncookies == 1);
	ASSERT(dmap->mbm_dma_cookie.dmac_laddress != NULL);

	return (0);
#else
	return (ENOTSUP);
#endif
}

/*
 * Allocate DMA mapped memory for the requestor.
 * If the contiguous flag is set, the call the contiguous memory allocator.
 */
/* ARGSUSED */
static int
i_mac_dma_alloc(mac_handle_t mh, mac_ring_handle_t mrh, pm_handle_t pmh,
    mac_block_t *dmap, ddi_dma_attr_t *attrp, ddi_device_acc_attr_t *accattrp,
    boolean_t contiguous, boolean_t streaming, size_t size)
{
	mac_impl_t	*mip = (mac_impl_t *)mh;
	uint_t		flags = 0;
	dev_info_t	*dip;
	int		ret;
	size_t		length;

	ASSERT(mip != NULL);
	ASSERT(dmap != NULL);
	dip = mip->mi_dip;
	ASSERT(dip != NULL);

	if (contiguous) {
		return (i_mac_dma_contig_alloc(mip, mrh, pmh, dmap, attrp,
		    streaming, size));
	}

	/*
	 * Allocate a DMA handle for the DMA buffer.
	 */
	if (ddi_dma_alloc_handle(dip, attrp, DDI_DMA_DONTWAIT,
	    NULL, &dmap->mbm_dma_handle) != DDI_SUCCESS)
		return (ENOMEM);

	/*
	 * Set the flags appropriately, streaming or consistent.
	 */
	if (streaming)
		flags = DDI_DMA_RDWR | DDI_DMA_STREAMING;
	else
		flags = DDI_DMA_RDWR | DDI_DMA_CONSISTENT;

	ret = ddi_dma_mem_alloc(dmap->mbm_dma_handle, size,
	    accattrp, flags, DDI_DMA_DONTWAIT, NULL,
	    (caddr_t *)&dmap->mbm_kaddrp, &length,
	    &dmap->mbm_acc_handle);
	if (ret != DDI_SUCCESS) {
		ddi_dma_free_handle(&dmap->mbm_dma_handle);
		dmap->mbm_dma_handle = NULL;
		return (ENOMEM);
	}
	dmap->mbm_mem_type = MAC_DDI_MEM;

	/*
	 * Associate memory with DMA handles.
	 */
	ret = ddi_dma_addr_bind_handle(dmap->mbm_dma_handle,
	    NULL, (caddr_t)dmap->mbm_kaddrp, size, flags, DDI_DMA_DONTWAIT,
	    0, &dmap->mbm_dma_cookie, &dmap->mbm_ncookies);
	if (ret != DDI_DMA_MAPPED) {
		ddi_dma_mem_free(&dmap->mbm_acc_handle);
		ddi_dma_free_handle(&dmap->mbm_dma_handle);
		dmap->mbm_dma_handle = NULL;
		dmap->mbm_kaddrp = NULL;
		return (ENOMEM);
	}

	ASSERT(dmap->mbm_ncookies == 1);
	ASSERT(dmap->mbm_dma_cookie.dmac_laddress != NULL);
	ASSERT(dmap->mbm_acc_handle != NULL);
	dmap->mbm_length = size;

	return (0);
}

/*
 * Entry point invoked by driver to allocate descriptor or buffer
 * memory for a given ring.
 */
mac_block_handle_t
mac_block_alloc(mac_handle_t mh, mac_ring_handle_t mrh, uint64_t flags,
    size_t length)
{
	mac_impl_t		*mip = (mac_impl_t *)mh;
	mac_ring_t		*mr = (mac_ring_t *)mrh;
	mac_capab_bm_t		*bm;
	mac_block_t		*dmap = NULL;
	ddi_dma_attr_t		*attrp;
	ddi_device_acc_attr_t	*accattrp;
	boolean_t		streaming;
	boolean_t		contig = B_FALSE;
	pm_handle_t		pmh;
	uint_t			cnt;
	list_t			*list;

	ASSERT(mh != NULL);
	ASSERT(mrh != NULL);
	ASSERT(flags != 0);
	ASSERT(length != 0);
	ASSERT(mr->mr_type == MAC_RING_TYPE_TX ||
	    mr->mr_type == MAC_RING_TYPE_RX);

	dmap = kmem_cache_alloc(mac_block_cachep, KM_NOSLEEP);
	if (dmap == NULL)
		return (NULL);
	bzero(dmap, sizeof (mac_block_t));

	dmap->mbm_handle = mh;
	dmap->mbm_ring_handle = mrh;
	dmap->mbm_length = length;
	dmap->mbm_flags = flags;
	/*
	 * Get the necessary information for this allocation.
	 * If contiguous memory is needed, then we must use
	 * premapped memory if a premapped cache has been created.
	 * (contiguous implies we can't have 2 different ranges,
	 * a premapped range and another non-premapped range).
	 * Currently contiguous memory allocation is supported
	 * only on transmit, but not on receive, as there is no consumer
	 * for the latter.
	 */
	if (mr->mr_type == MAC_RING_TYPE_RX) {
		ASSERT(mip->mi_bm_rx_enabled == B_TRUE);
		bm = &mip->mi_bm_rx_cap;
		pmh = NULL;
	} else {
		ASSERT(mip->mi_bm_tx_enabled == B_TRUE);
		bm = &mip->mi_bm_tx_cap;
		pmh = mip->mi_pmh_tx_drv;
	}
	dmap->mbm_bm = bm;

	/*
	 * Packet memory can be one of DDI_MEM, CONTIG_MEM, CONTIG_PREMAP_MEM.
	 * Descriptor memory is always DDI_MEM. Thus 'contig' is used only
	 * for packet memory.
	 */
	if (flags & MAC_BM_BLOCK_PACKET) {
		attrp = bm->mbm_packet_attrp;
		accattrp = bm->mbm_packet_accattrp;
		contig = (bm->mbm_flags & MAC_BM_PACKET_CONTIGUOUS);
		streaming = B_TRUE;
	} else {
		ASSERT(flags & MAC_BM_BLOCK_DESCRIPTOR);
		attrp = bm->mbm_descriptor_attrp;
		accattrp = bm->mbm_descriptor_accattrp;
		streaming = B_FALSE;
	}

	if (i_mac_dma_alloc(mh, mrh, pmh, dmap, attrp, accattrp, contig,
	    streaming, length) != 0) {
		dmap->mbm_ring_handle = NULL;
		dmap->mbm_handle = NULL;
		kmem_cache_free(mac_block_cachep, dmap);
		return (NULL);
	}

	dmap->mbm_ioaddrp = dmap->mbm_dma_cookie.dmac_laddress;
	/*
	 * Keep track of the count of driver managed buffers.
	 * We check this in the mac_unregister() path.
	 */
	if (bm->mbm_flags & MAC_BM_DRIVER_MANAGES_BUFFERS) {
		mutex_enter(&mip->mi_bm_list_lock);
		if (mr->mr_type == MAC_RING_TYPE_TX) {
			mip->mi_bm_tx_block_cnt++;
			cnt = mip->mi_bm_tx_block_cnt;
			list = &mip->mi_bm_tx_block_list;
		} else {
			mip->mi_bm_rx_block_cnt++;
			cnt = mip->mi_bm_rx_block_cnt;
			list = &mip->mi_bm_rx_block_list;
		}
		list_insert_tail(list, dmap);
		mutex_exit(&mip->mi_bm_list_lock);
		if (cnt == 1) {
			/* A block pool is a memory reference to the mac */
			mac_mem_ref_inc(mip);
		}
	}

	return ((mac_block_handle_t)dmap);
}

/*
 * A block (packet) is still loaned up, unbind it's DMA mappings.
 */
static void
i_mac_block_unbind(mac_block_t *dmap)
{
	ASSERT(dmap != NULL);

	/*
	 * More than 1 packet can be packed in the same block to conserve
	 * DVMA space. We dont' need to track reference counts here,
	 * unlike the free case, since the call is single-threaded.
	 */
	if (dmap->mbm_unbound)
		return;

	ASSERT(dmap->mbm_dma_handle != NULL);
	ASSERT(dmap->mbm_length != 0);
	ASSERT(dmap->mbm_kaddrp != NULL);

	/*
	 * Cleanup the cookies.
	 */
	if (dmap->mbm_ncookies > 0) {
		ASSERT(!dmap->mbm_unbound);
		(void) ddi_dma_unbind_handle(dmap->mbm_dma_handle);
		dmap->mbm_ncookies = 0;
	}

	/*
	 * Free the DMA handle.
	 */
	ddi_dma_free_handle(&dmap->mbm_dma_handle);
	dmap->mbm_dma_handle = NULL;

	/*
	 * The packet is now unbound.
	 */
	dmap->mbm_unbound = B_TRUE;

	/* mac_unregister can complete and free the rings */
	dmap->mbm_ring_handle = NULL;
}

boolean_t
i_mac_block_free(mac_block_t *dmap)
{
	mac_impl_t	*mip;
#if defined(__sparc)
	mac_ring_t	*mr = (mac_ring_t *)dmap->mbm_ring_handle;
#endif
	mac_capab_bm_t	*bm;
	uint_t		cnt;
	boolean_t	do_decref = B_FALSE;
	list_t		*list;

	ASSERT(dmap != NULL);
	ASSERT(dmap->mbm_length != 0 && dmap->mbm_kaddrp != NULL);
	ASSERT(dmap->mbm_pkt_cnt == 0);

	if (!dmap->mbm_unbound) {
		ASSERT(dmap->mbm_dma_handle != NULL && dmap->mbm_ncookies != 0);
		(void) ddi_dma_unbind_handle(dmap->mbm_dma_handle);
		ddi_dma_free_handle(&dmap->mbm_dma_handle);
		dmap->mbm_dma_handle = NULL;
	} else {
		ASSERT(dmap->mbm_unbound && dmap->mbm_dma_handle == NULL &&
		    dmap->mbm_ncookies == 0);
	}

	bm = dmap->mbm_bm;
	mip = (mac_impl_t *)dmap->mbm_handle;

#if defined(__sparc)
	if (dmap->mbm_mem_type == MAC_DDI_MEM) {
		ddi_dma_mem_free(&dmap->mbm_acc_handle);
	} else if (dmap->mbm_mem_type == MAC_CONTIG_MEM) {
		contig_mem_free(dmap->mbm_kaddrp, dmap->mbm_length);
	} else if (mr != NULL) {
		ASSERT(dmap->mbm_mem_type == MAC_CONTIG_PREMAP_MEM);
		ASSERT(bm->mbm_flags & MAC_BM_DRIVER_MANAGES_BUFFERS);
		/*
		 * We free the premapped contig memory only when we destroy
		 * the pools at mac_stop time and then destroy the pre-mapped
		 * cache. Rings may come and go between mac_start and mac_stop.
		 */
		ASSERT((mr->mr_contig_mem == dmap->mbm_kaddrp) &&
		    (mr->mr_contig_mem_length == dmap->mbm_length) &&
		    mr->mr_contig_mem_in_use);
		mr->mr_contig_mem_in_use = B_FALSE;
	}
#else
	ASSERT(dmap->mbm_mem_type == MAC_DDI_MEM);
	ddi_dma_mem_free(&dmap->mbm_acc_handle);
#endif

	dmap->mbm_acc_handle = NULL;
	dmap->mbm_kaddrp = NULL;
	dmap->mbm_ioaddrp = NULL;
	dmap->mbm_handle = NULL;
	dmap->mbm_ring_handle = NULL;
	dmap->mbm_ncookies = 0;
	dmap->mbm_bm = NULL;
	dmap->mbm_length = 0;

	/*
	 * It is possible that the Rx buffers may be held up in the
	 * stack past the driver detach. These blocks were DMA unbound at
	 * mac_stop() time in mac_bm_destroy_pools(). Eventually when
	 * the mblks are released  by the stack, the driver (which was
	 * detached, but not unloaded) does a mac_block_free() and we
	 * may end up here, after mac_unregister() has completed.
	 * In such a case, the mac_impl_t is freed below.
	 */
	if (bm->mbm_flags & MAC_BM_DRIVER_MANAGES_BUFFERS) {
		ASSERT(MUTEX_HELD(&mip->mi_bm_list_lock));
		if (bm->mbm_type == MAC_BUFFER_MGMT_TX) {
			mip->mi_bm_tx_block_cnt--;
			cnt = mip->mi_bm_tx_block_cnt;
			list = &mip->mi_bm_tx_block_list;
		} else {
			mip->mi_bm_rx_block_cnt--;
			cnt = mip->mi_bm_rx_block_cnt;
			list = &mip->mi_bm_rx_block_list;
		}
		list_remove(list, dmap);
		if (cnt == 0)
			do_decref = B_TRUE;
	}

	kmem_cache_free(mac_block_cachep, dmap);

	return (do_decref);
}

/*
 * Entry point by the driver to free previously allocated DMA memory.
 */
void
mac_block_free(mac_block_handle_t mbh)
{
	mac_block_t	*dmap = (mac_block_t *)mbh;
	mac_capab_bm_t	*bm;
	mac_impl_t	*mip;
	boolean_t	do_decref;
	boolean_t	do_free;

	mip = (mac_impl_t *)dmap->mbm_handle;
	bm = dmap->mbm_bm;
	ASSERT(bm->mbm_flags & MAC_BM_DRIVER_MANAGES_BUFFERS);

	/*
	 * Serialize concurrent i_mac_block_unbind().
	 */
	mutex_enter(&mip->mi_bm_list_lock);
	do_decref = i_mac_block_free((mac_block_t *)mbh);
	mutex_exit(&mip->mi_bm_list_lock);

	if (do_decref) {
		do_free = mac_mem_ref_dec(mip);
		if (do_free)
			mac_impl_free(mip);
	}
}

size_t
mac_block_length_get(mac_block_handle_t mbh)
{
	mac_block_t	*dmap = (mac_block_t *)mbh;

	ASSERT(mbh != NULL);
	ASSERT(dmap->mbm_length != 0);

	return (dmap->mbm_length);
}

caddr_t
mac_block_kaddr_get(mac_block_handle_t mbh)
{
	mac_block_t	*dmap = (mac_block_t *)mbh;

	ASSERT(mbh != NULL);
	ASSERT(dmap->mbm_kaddrp != NULL);

	return (dmap->mbm_kaddrp);
}

uint64_t
mac_block_ioaddr_get(mac_block_handle_t mbh)
{
	mac_block_t	*dmap = (mac_block_t *)mbh;

	ASSERT(mbh != NULL);
	ASSERT(dmap->mbm_ioaddrp != NULL);

	return (dmap->mbm_ioaddrp);
}

ddi_dma_cookie_t
mac_block_dma_cookie_get(mac_block_handle_t mbh)
{
	mac_block_t	*dmap = (mac_block_t *)mbh;

	ASSERT(mbh != NULL);

	return (dmap->mbm_dma_cookie);
}

ddi_dma_handle_t
mac_block_dma_handle_get(mac_block_handle_t mbh)
{
	mac_block_t	*dmap = (mac_block_t *)mbh;

	ASSERT(mbh != NULL);

	return (dmap->mbm_dma_handle);
}

ddi_acc_handle_t
mac_block_acc_handle_get(mac_block_handle_t mbh)
{
	mac_block_t	*dmap = (mac_block_t *)mbh;

	ASSERT(mbh != NULL);

	return (dmap->mbm_acc_handle);
}

/*
 * MAC Descriptor for buffer management
 */
int
i_mac_descriptor_create(mac_handle_t mh, mac_ring_handle_t mrh,
    uint32_t min_descriptors, uint32_t max_descriptors,
    uint32_t desired_descriptors, size_t descriptor_size)
{
	mac_descriptor_t	*mdp;
	mac_block_t		*mblockp;
	size_t			length;
	uint64_t		flags = MAC_BM_BLOCK_DESCRIPTOR;
	uint32_t		ndescriptors;
	mac_ring_t		*ring = (mac_ring_t *)mrh;

	ASSERT(mh != NULL);
	ASSERT(mrh != NULL);
	ASSERT(max_descriptors != 0);
	ASSERT(desired_descriptors != 0);
	ASSERT(descriptor_size != 0);

	ndescriptors = desired_descriptors;
	if (desired_descriptors < min_descriptors)
		ndescriptors = min_descriptors;
	else if (desired_descriptors > max_descriptors)
		ndescriptors = max_descriptors;

	/*
	 * For now, just use desired descriptor ring size.
	 */
	length = descriptor_size * ndescriptors;

	mblockp = (mac_block_t *)mac_block_alloc(mh, mrh, flags, length);
	if (mblockp == NULL)
		return (-1);

	mdp = kmem_cache_alloc(mac_descriptor_cachep, KM_NOSLEEP);
	if (mdp == NULL) {
		(void) i_mac_block_free(mblockp);
		return (-1);
	}
	bzero(mdp, sizeof (mac_descriptor_t));

	mdp->md_mblockp = mblockp;
	mdp->md_handle = mh;
	mdp->md_ring_handle = mrh;
	mdp->md_descriptors = desired_descriptors;
	mdp->md_min = min_descriptors;
	mdp->md_max = max_descriptors;
	mdp->md_size = descriptor_size;

	/*
	 * Zero the descriptor memory.
	 */
	bzero(mblockp->mbm_kaddrp, length);
	ring->mr_packet_descriptor = mdp;

	return (0);
}

void
i_mac_descriptor_destroy(mac_descriptor_t *mdp)
{
	ASSERT(mdp != NULL);
	ASSERT(mdp->md_mblockp != NULL);

	(void) i_mac_block_free(mdp->md_mblockp);
	mdp->md_mblockp = NULL;

	kmem_cache_free(mac_descriptor_cachep, mdp);
}

/*
 * Return the descriptor block of memory for the driver to use.
 *
 */
/* ARGSUSED */
mac_descriptor_handle_t
mac_descriptors_get(mac_handle_t mh, mac_ring_handle_t mrh, uint32_t *nds)
{
	mac_ring_t	*ring = (mac_ring_t *)mrh;

	ASSERT(mh != NULL);
	ASSERT(mrh != NULL);
	ASSERT(ring->mr_packet_descriptor != NULL);

	*nds = (uint32_t)ring->mr_packet_descriptor->md_descriptors;

	return ((mac_descriptor_handle_t)ring->mr_packet_descriptor);
}

/*
 * Return length of the descriptor memory.
 */
size_t
mac_descriptors_length_get(mac_descriptor_handle_t mdh)
{
	mac_descriptor_t	*mdp = (mac_descriptor_t *)mdh;

	ASSERT(mdh != NULL);
	ASSERT(mdp->md_mblockp != NULL);

	return (mac_block_length_get((mac_block_handle_t)mdp->md_mblockp));
}

/*
 * Return the kernel address for the descriptor memory.
 */
caddr_t
mac_descriptors_address_get(mac_descriptor_handle_t mdh)
{
	mac_descriptor_t	*mdp = (mac_descriptor_t *)mdh;

	ASSERT(mdh != NULL);
	ASSERT(mdp->md_mblockp != NULL);

	return (mac_block_kaddr_get((mac_block_handle_t)mdp->md_mblockp));
}

/*
 * Return the IOaddress for the descriptor memory.
 */
uint64_t
mac_descriptors_ioaddress_get(mac_descriptor_handle_t mdh)
{
	mac_descriptor_t	*mdp = (mac_descriptor_t *)mdh;

	ASSERT(mdh != NULL);
	ASSERT(mdp->md_mblockp != NULL);

	return (mac_block_ioaddr_get((mac_block_handle_t)mdp->md_mblockp));
}

/*
 * Get the DDI DMA Handle for this descriptor block.
 */
ddi_dma_handle_t
mac_descriptors_dma_handle_get(mac_descriptor_handle_t mdh)
{
	mac_descriptor_t	*mdp = (mac_descriptor_t *)mdh;

	ASSERT(mdh != NULL);
	ASSERT(mdp->md_mblockp != NULL);

	return (mac_block_dma_handle_get((mac_block_handle_t)mdp->md_mblockp));
}


/*
 * Called when a packet pool is createed or when the first mac_block_t is
 * allocated. A packet pool is a memory reference to the MAC instance.
 * Similarly an outstanding block pool is also a memory reference.
 */
static void
mac_mem_ref_inc(mac_impl_t *mip)
{
	i_mac_perim_enter(mip);
	mip->mi_mem_ref_cnt++;
	i_mac_perim_exit(mip);
}

static boolean_t
mac_mem_ref_dec(mac_impl_t *mip)
{
	boolean_t dofree = B_FALSE;

	/*
	 * Release the reference to the MAC instance.
	 */
	i_mac_perim_enter(mip);
	mip->mi_mem_ref_cnt--;
	if ((mip->mi_mem_ref_cnt == 0) && (mip->mi_ref == 0))
		dofree = B_TRUE;
	i_mac_perim_exit(mip);

	return (dofree);
}

static void
mac_impl_free(mac_impl_t *mip)
{
	/*
	 * Reset to default values before kmem_cache_free
	 * Note that MIS_DISABLED would have been set in
	 * mi_state_flags after the mac was disabled.
	 */
	mip->mi_state_flags = 0;
	mac_bm_reset_values(mip);
	kmem_cache_free(i_mac_impl_cachep, mip);
}

/*
 * MAC Packet Pool for buffer management.
 */
int
i_mac_packet_pool_create(mac_handle_t mh, mac_ring_handle_t mrh,
    uint32_t min_buffers, uint32_t max_buffers, uint32_t desired_buffers,
    size_t buffer_size, off_t offset)
{
	mblk_t			*mp;
	mac_packet_pool_t	*pktpoolp;
	mac_packet_t		*mpkt;
	mac_impl_t		*mip = (mac_impl_t *)mh;
	int			i;
	uint32_t		npackets;
	mac_ring_t		*ring = (mac_ring_t *)mrh;
	mac_block_t		*blockp = NULL;

	/*
	 * NOTE: For now MAC layer will allocate the NIC driver's desired
	 * number of buffers.  In the future the MAC layer can and will have
	 * a different policy for allocation.
	 */
	ASSERT(mh != NULL);
	ASSERT(mrh != NULL);
	ASSERT(max_buffers != 0);
	ASSERT(desired_buffers != 0);
	ASSERT(buffer_size != 0);

	/*
	 * Limit check the number of buffers for the packet
	 * pool.
	 */
	npackets = desired_buffers;
	if (npackets < min_buffers)
		npackets = min_buffers;
	else if (npackets > max_buffers)
		npackets = max_buffers;

	/*
	 * Allocate the packet pool management structure.
	 */
	pktpoolp = kmem_cache_alloc(mac_packet_pool_cachep, KM_NOSLEEP);
	if (pktpoolp == NULL)
		return (-1);
	mac_packet_pool_init(pktpoolp);

	pktpoolp->mpp_list = (mac_packet_t **)kmem_zalloc(
	    sizeof (mac_packet_t *) * npackets, KM_NOSLEEP);
	if (pktpoolp->mpp_list == NULL) {
		kmem_cache_free(mac_packet_pool_cachep, pktpoolp);
		return (-1);
	}

	pktpoolp->mpp_mh = mh;
	pktpoolp->mpp_mrh = mrh;
	pktpoolp->mpp_min_buffers = min_buffers;
	pktpoolp->mpp_max_buffers = max_buffers;
	pktpoolp->mpp_desired_buffers = desired_buffers;
	pktpoolp->mpp_buffer_size = buffer_size;
	pktpoolp->mpp_offset = offset;
	pktpoolp->mpp_npackets = npackets;

	ring->mr_packet_pool = pktpoolp;

	/*
	 * A packet pool is a memory reference to the MAC
	 * instance
	 */
	mac_mem_ref_inc(mip);

	/*
	 * Packet pool will be created with some extra packets.
	 */
	mutex_enter(&pktpoolp->mpp_lock);
	for (i = 0; i < npackets; i++) {
		/*
		 * Create a new packet and add it the pool.
		 */
		if ((mp = i_mac_packet_alloc(mh, mrh, buffer_size,
		    &blockp)) == NULL) {
			mutex_exit(&pktpoolp->mpp_lock);
			if (i == 0) {
				/*
				 * Since not even a single packet was allocated
				 * the freeb callbacks won't happen and we need
				 * to cleanup explicitly
				 */
				ASSERT(pktpoolp->mpp_mem_ref_cnt == 0);
				pktpoolp->mpp_offline = B_TRUE;
				i_mac_packet_pool_destroy(pktpoolp);
			} else {
				ASSERT(pktpoolp->mpp_mem_ref_cnt != 0);
				i_mac_packet_pool_cleanup(pktpoolp);
			}
			ring->mr_packet_pool = NULL;
			return (-1);
		}

		mutex_enter(&pktpoolp->mpp_nic_pool.mpl_lock);
		i_mac_packet_pool_add(&pktpoolp->mpp_nic_pool, mp);
		mutex_exit(&pktpoolp->mpp_nic_pool.mpl_lock);

		mpkt = (mac_packet_t *)mp->b_datap->db_packet;
		/*
		 * Keep a list of all packets allocated to this pool.
		 */
		pktpoolp->mpp_list[i] = mpkt;
		pktpoolp->mpp_mem_ref_cnt++;
		mpkt->mp_index = i;
		mpkt->mp_pool = pktpoolp;
	}
	mutex_exit(&pktpoolp->mpp_lock);

	return (0);
}

static void
i_mac_packet_list_destroy(mac_packet_list_t *mpl)
{
	mblk_t *mp;

	ASSERT(mpl != NULL);

	mutex_enter(&mpl->mpl_lock);
	mp = mpl->mpl_head;
	mpl->mpl_head = NULL;
	mpl->mpl_tail = NULL;
	mpl->mpl_cnt = 0;
	mutex_exit(&mpl->mpl_lock);

	if (mp != NULL)
		freemsgchain(mp);
}

/*
 * This function is called when packet pools are destroyed from
 * i_mac_bm_ring_destroy_pools(). Also called on pool creation failure
 * from i_mac_packet_pool_create().
 */
static void
i_mac_packet_pool_cleanup(mac_packet_pool_t *pktpoolp)
{
	int i;
	mac_packet_list_t *mpl;

	ASSERT(pktpoolp != NULL);

	/*
	 * Grab all the list locks to make sure that every thread doing
	 * an i_mac_packet_freeb gets to see the mpp_offline correctly.
	 */
	for (i = 0; i < MAC_PACKET_RECYCLE_LISTS; i++) {
		mpl = &pktpoolp->mpp_recycle_pools[i];
		mutex_enter(&mpl->mpl_lock);
	}

	/*
	 * Set the pool to be offline, and release all the list locks.
	 */
	pktpoolp->mpp_offline = B_TRUE;

	for (i = MAC_PACKET_RECYCLE_LISTS - 1; i >= 0; i--) {
		mpl = &pktpoolp->mpp_recycle_pools[i];
		mutex_exit(&mpl->mpl_lock);
	}

	/*
	 * Find the packets that are loaned out to the driver or system
	 * that need to be unbound.
	 */
	mutex_enter(&pktpoolp->mpp_lock);
	for (i = 0; i < pktpoolp->mpp_npackets; i++) {
		if (pktpoolp->mpp_list[i] != NULL)
			i_mac_packet_unbind(pktpoolp->mpp_list[i]);
	}

	/*
	 * Let go of the pool lock. Note that each individual packet list
	 * destroyed below is protected by its own list lock (mpl_lock).
	 */
	mutex_exit(&pktpoolp->mpp_lock);

	/*
	 * Destroy the packets that have been recycled but not yet
	 * moved to the NIC pool.
	 */
	for (i = 0; i < MAC_PACKET_RECYCLE_LISTS; i++)
		i_mac_packet_list_destroy(&pktpoolp->mpp_recycle_pools[i]);

	/*
	 * These packets are not loaned out, so destroy them.
	 */
	i_mac_packet_list_destroy(&pktpoolp->mpp_nic_pool);
}

static void
i_mac_packet_pool_destroy(mac_packet_pool_t *pktpoolp)
{
	mac_impl_t *mip;
	boolean_t dofree = B_FALSE;

	ASSERT(pktpoolp != NULL);
	mip = (mac_impl_t *)pktpoolp->mpp_mh;
	ASSERT(mip != NULL);

	dofree = mac_mem_ref_dec(mip);

	mac_packet_pool_fini(pktpoolp);
	/*
	 * Free the packet pool instance.
	 */
	kmem_cache_free(mac_packet_pool_cachep, pktpoolp);

	if (dofree)
		mac_impl_free(mip);
}

/*
 * Add/return a packet to the packet pool.
 */
static void
i_mac_packet_pool_add(mac_packet_list_t *mpl, mblk_t *mp)
{
	ASSERT(mpl != NULL);
	ASSERT(mutex_owned(&mpl->mpl_lock));
	ASSERT(mp != NULL);

	if (mpl->mpl_head == NULL) {
		ASSERT(mpl->mpl_tail == NULL);
		mpl->mpl_head = mp;
	} else
		mpl->mpl_tail->b_next = mp;
	mpl->mpl_tail = mp;
	mpl->mpl_cnt++;
}

static boolean_t
i_mac_packet_pool_recycle_add(mac_packet_list_t *mpl, mblk_t *mp)
{
	ASSERT(mpl != NULL);
	ASSERT(mutex_owned(&mpl->mpl_lock));
	ASSERT(mp != NULL);

	DTRACE_PROBE2(i_mac_packet_pool_recycle_add, mac_packet_list_t *, mpl,
	    mblk_t *, mp);
	if (mpl->mpl_head == NULL) {
		ASSERT(mpl->mpl_tail == NULL);
		mpl->mpl_head = mp;
	} else
		mpl->mpl_tail->b_next = mp;
	mpl->mpl_tail = mp;
	mpl->mpl_cnt++;

	return (mpl->mpl_cnt >= mac_packet_recycle_threshold);
}

/*
 * The packet is still loaned up to the	upper layers, but we need to unbind the
 * packet since the interface is going away.
 */
static void
i_mac_packet_unbind(mac_packet_t *pktp)
{
	ASSERT(pktp != NULL);

	/*
	 * Undo the DMA bindings. The call is single threaded in the
	 * mac_stop path and error cases in creation.
	 */
	i_mac_block_unbind(pktp->mp_dm_p);
	pktp->mp_unbound = B_TRUE;
}

/*
 * Destroy a previously allocated packet.
 */
static boolean_t
i_mac_packet_destroy(mac_packet_pool_t *pktpoolp, mac_packet_t *pktp)
{
	mac_block_t	*dmap;

	ASSERT(pktp != NULL);
	ASSERT(pktpoolp != NULL);

	ASSERT(MUTEX_HELD(&pktpoolp->mpp_lock));

	/*
	 * Free the packet buffer.
	 */
	dmap = pktp->mp_dm_p;
	if (atomic_add_32_nv(&dmap->mbm_pkt_cnt, -1) == 0)
		(void) i_mac_block_free(dmap);
	pktp->mp_dm_p = NULL;

	kmem_cache_free(mac_packet_cachep, pktp);

	/*
	 * Update the packet pool reference count. If the memory reference
	 * count is now zero, then all mac_packet_t's allocated from this
	 * pool have been freed and the pool can be destroyed
	 */
	pktpoolp->mpp_mem_ref_cnt--;

	return (pktpoolp->mpp_mem_ref_cnt == 0);
}

/*
 * Move packets from one packet list to	another.
 */
static void
i_mac_packet_pool_move(mac_packet_list_t *from, mac_packet_list_t *to)
{
	ASSERT(from != NULL);
	ASSERT(to != NULL);
	ASSERT(mutex_owned(&from->mpl_lock));

	DTRACE_PROBE2(i_mac_packet_pool_move, mac_packet_list_t *, from,
	    mac_packet_list_t *, to);

	mutex_enter(&to->mpl_lock);

	/*
	 * Update the "to" mac_packet_list.
	 */
	if (to->mpl_head == NULL)
		to->mpl_head = from->mpl_head;
	else
		to->mpl_tail->b_next = from->mpl_head;
	to->mpl_tail = from->mpl_tail;
	to->mpl_cnt += from->mpl_cnt;

	/*
	 * Update the "from" mac_packet_list.
	 */
	from->mpl_cnt = 0;
	from->mpl_head = NULL;
	from->mpl_tail = NULL;

	mutex_exit(&to->mpl_lock);
}

/*
 * Recycle or destroy the packet returned from the upper layers.
 *
 * The dblk associated with the 'mp' is normally recycled by STREAMS
 * on the last freeb(). However STREAMS may allocate a new dblk on
 * debug kernels. Please see the block comment above the mac_packet_t
 * definition in the header file for more information.
 */
static void
i_mac_packet_freeb(void *arg, mblk_t *mp)
{
	mac_packet_t		*mpkt = (mac_packet_t *)arg;
	mac_packet_pool_t	*pktpoolp;
	mac_packet_list_t	*mpl;
	boolean_t		do_free;

	ASSERT(mpkt != NULL && mp != NULL);

	ASSERT(mpkt->mp_dm_p != NULL);
	ASSERT(mpkt->mp_dm_p->mbm_handle != NULL);

	pktpoolp = mpkt->mp_pool;
	ASSERT(pktpoolp != NULL);
	/*
	 * If the pool is offlined, then destroy the packet.
	 * Otherwise, recycle the packet.
	 */
	if (!pktpoolp->mpp_offline) {
		/*
		 * Recycle the packet.
		 */
		mp->b_rptr += pktpoolp->mpp_offset;
		mp->b_wptr += pktpoolp->mpp_offset;
		VERIFY((caddr_t)mp->b_rptr == mpkt->mp_kaddr);
		VERIFY(mp->b_datap->db_packet == (intptr_t)mpkt);
		/*
		 * Grab the packet pool list to which the packet will be
		 * recycled.
		 */
		mpl = &pktpoolp->mpp_recycle_pools[
		    MAC_PACKET_LIST(CPU->cpu_id)];

		/*
		 * If we have returned enough packets to the mac layer
		 * via this recycle pool, then move them to the NIC
		 * driver pool for use by the NIC.
		 */
		mutex_enter(&mpl->mpl_lock);
		/*
		 * Check for mpp_offline once again, now under the lock
		 * To set mpp_offline, a thread needs to grab all the mpl_locks.
		 */
		if (pktpoolp->mpp_offline) {
			mutex_exit(&mpl->mpl_lock);
			goto out;
		}
		if (i_mac_packet_pool_recycle_add(mpl, mp)) {
			i_mac_packet_pool_move(mpl, &pktpoolp->mpp_nic_pool);
		}
		mutex_exit(&mpl->mpl_lock);
		return;
	}

out:
	/*
	 * Grab the pool lock.
	 */
	mutex_enter(&pktpoolp->mpp_lock);

	/*
	 * Destroy the reference to the packet in pool list.
	 */
	pktpoolp->mpp_list[mpkt->mp_index] = NULL;

	/*
	 * Destroy the packet. If all packets of the pool have been
	 * destroyed, then 'do_free' will be B_TRUE and we destroy
	 * the pool itself.
	 */
	do_free = i_mac_packet_destroy(pktpoolp, mpkt);
	mutex_exit(&pktpoolp->mpp_lock);

	if (do_free)
		i_mac_packet_pool_destroy(pktpoolp);

	/*
	 * We want STREAMS to really free this mblk. Convert this
	 * reusable mblk to a normal mblk, so that STREAMS will free it.
	 */
	mp->b_datap->db_frtnp = NULL;
	freeb(mp);
}

/*
 * Allocate a memory buffer, mac_packet_t and mblk
 */
static mblk_t *
i_mac_packet_alloc(mac_handle_t mh, mac_ring_handle_t mrh, size_t length,
    mac_block_t **blockpp)
{
	mac_block_t	*dmap = *blockpp;
	mac_packet_t	*mpkt;
	uint64_t	flags = MAC_BM_BLOCK_PACKET;
	mac_impl_t	*mip = (mac_impl_t *)mh;
	mac_ring_t	*mr = (mac_ring_t *)mrh;
	mac_packet_pool_t	*pktpoolp = mr->mr_packet_pool;
	mblk_t		*mp;
	mac_capab_bm_t	*bm;
	boolean_t	block_allocated = B_FALSE;
	off_t		offset;
	size_t		albufsize, align;

	ASSERT(mh != NULL);
	ASSERT(mr != NULL);
	ASSERT(length != 0);
	ASSERT(mr->mr_type == MAC_RING_TYPE_TX ||
	    mr->mr_type == MAC_RING_TYPE_RX);

	if ((mpkt = kmem_cache_alloc(mac_packet_cachep, KM_NOSLEEP)) == NULL)
		return (NULL);

	bzero(mpkt, sizeof (mac_packet_t));

	mpkt->mp_free_rtn.free_func = i_mac_packet_freeb;
	mpkt->mp_free_rtn.free_arg = (void *)mpkt;

	/*
	 * Get the necessary information for this allocation.
	 */
	if (mr->mr_type == MAC_RING_TYPE_RX) {
		ASSERT(mip->mi_bm_rx_enabled == B_TRUE);
		bm = &mip->mi_bm_rx_cap;
	} else {
		ASSERT(mip->mi_bm_tx_enabled == B_TRUE);
		bm = &mip->mi_bm_tx_cap;
	}

	align = bm->mbm_packet_attrp->dma_attr_align;
	albufsize = roundup(bm->mbm_buffer_size, align);

	/*
	 * Get a memory buffer if needed
	 */
	if (dmap == NULL ||
	    (dmap->mbm_cur_offset + length > dmap->mbm_length)) {
		dmap = (mac_block_t *)mac_block_alloc(mh, mrh, flags,
		    bm->mbm_block_size);
		if (dmap == NULL) {
			kmem_cache_free(mac_packet_cachep, mpkt);
			return (NULL);
		}
		block_allocated = B_TRUE;
	}

	offset = dmap->mbm_cur_offset;
	mp = desballoc_reusable((uchar_t *)dmap->mbm_kaddrp + offset,
	    length, &mpkt->mp_free_rtn);

	if (mp == NULL) {
		if (block_allocated)
			(void) i_mac_block_free(dmap);
		kmem_cache_free(mac_packet_cachep, mpkt);
		return (NULL);
	}

	mp->b_rptr += pktpoolp->mpp_offset;
	mp->b_wptr += pktpoolp->mpp_offset;

	mpkt->mp_dm_p = dmap;

	mpkt->mp_dma_cookie = dmap->mbm_dma_cookie;
	mpkt->mp_dma_cookie.dmac_laddress += pktpoolp->mpp_offset + offset;
	mpkt->mp_dma_cookie.dmac_size = length - pktpoolp->mpp_offset;
	mpkt->mp_dma_handle = dmap->mbm_dma_handle;

	mpkt->mp_length = length - pktpoolp->mpp_offset;
	mpkt->mp_kaddr = dmap->mbm_kaddrp + pktpoolp->mpp_offset + offset;
	mpkt->mp_ioaddr = dmap->mbm_ioaddrp + pktpoolp->mpp_offset + offset;

	mp->b_datap->db_packet = (intptr_t)mpkt;

	dmap->mbm_cur_offset += albufsize;
	dmap->mbm_pkt_cnt++;

	*blockpp = dmap;

	return (mp);
}

mblk_t *
mac_mblk_get(mac_handle_t mh, mac_ring_handle_t mrh, mblk_t **tail,
    int *npackets)
{
	mac_ring_t		*ring = (mac_ring_t *)mrh;
	mblk_t			*mp = NULL;
	mac_packet_pool_t	*pktpoolp;
	mac_packet_list_t	*mpl;

	ASSERT(mh != NULL);
	ASSERT(mrh != NULL);

	/*
	 * If the packet pool does not exist, return
	 * no packets.
	 */
	if ((pktpoolp = ring->mr_packet_pool) == NULL)
		goto error;

	/*
	 * Get the packet list.
	 */
	mpl = &pktpoolp->mpp_nic_pool;

	/*
	 * Are there n mblks available?  If no, do not any futher and
	 * return a NULL mblk chain.
	 */
	if (mpl->mpl_cnt < *npackets)
		goto error;

	/*
	 * Grab the lock to the the packet pool sice we are about
	 * grab the mblk chain to return to the NIC.
	 */
	mutex_enter(&mpl->mpl_lock);

	mp = mpl->mpl_head;
	ASSERT(mp != NULL);
	if (*npackets == 1) {
		mpl->mpl_head = mp->b_next;
		if (mpl->mpl_head == NULL)
			mpl->mpl_tail = NULL;
		mpl->mpl_cnt--;
		*tail = mp;
		mp->b_next = NULL;
	} else {
		*tail = mpl->mpl_tail;
		*npackets = mpl->mpl_cnt;
		mpl->mpl_head = NULL;
		mpl->mpl_tail = NULL;
		mpl->mpl_cnt = 0;
	}

	/*
	 * Let the packet pool lock go and return the mblk
	 */
	mutex_exit(&mpl->mpl_lock);
	return (mp);

error:
	*tail = NULL;
	*npackets = 0;

	return (NULL);
}

/* ARGSUSED */
void
mac_mblk_info_get(mac_handle_t mh, mblk_t *mp,
    ddi_dma_handle_t *handlep, uint64_t *ioaddrp, size_t *sizep)
{
	mac_packet_t	*mpkt;
	dblk_t	*db = mp->b_datap;

	mpkt = (mac_packet_t *)db->db_packet;
	*handlep = mpkt->mp_dma_handle;
	*ioaddrp = mpkt->mp_ioaddr;
	*sizep = mpkt->mp_length;
}

/* ARGSUSED */
boolean_t
mac_mblk_unbound(mac_handle_t mh, mac_ring_handle_t mrh, mblk_t *mp)
{
	mac_packet_t	*mpkt;

	ASSERT(mh != NULL);

	if (mp->b_datap->db_flags & DBLK_DMA_PREMAP) {
		return (B_FALSE);
	} else {
		mpkt = (mac_packet_t *)mp->b_datap->db_packet;
		ASSERT(mpkt != NULL);
		return (mpkt->mp_unbound);
	}
}
