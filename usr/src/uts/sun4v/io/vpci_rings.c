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

#include <sys/conf.h>
#include <sys/sunndi.h>
#include <sys/archsystm.h>
#include <sys/note.h>
#include <sys/pciev_impl.h>
#include <sys/sysmacros.h>

#include <sys/mdeg.h>
#include <sys/ldoms.h>
#include <sys/ldc.h>
#include <sys/vio_mailbox.h>
#include <sys/vpci_var.h>

/*
 * Descriptor Ring helper routines
 */

/*
 * Function:
 *	vpci_alloc_rings()
 *
 * Description:
 * 	Allocate driver rx and tx rings data structures
 *
 * Arguments:
 *	vport	- Virtual pci port pointer
 *
 * Return Code:
 * 	DDI_SUCCESS	- Success
 * 	DDI_FAILURE	- Failure
 */
int
vpci_alloc_rings(vpci_port_t *vport)
{
	vpci_tx_ring_t	*tx_ring = NULL;
	vpci_rx_ring_t	*rx_ring = NULL;

	/* TX/RX rings number is determined by PCIv pkt type */
	vport->num_tx_rings = PCIV_PKT_TYPE_MAX;
	vport->num_rx_rings = PCIV_PKT_TYPE_MAX;

	/*
	 * Allocate memory space for tx rings
	 */
	vport->tx_rings = kmem_zalloc(
	    sizeof (vpci_tx_ring_t) * vport->num_tx_rings,
	    KM_NOSLEEP);

	if (vport->tx_rings == NULL)
		return (DDI_FAILURE);

	for (int i = 0; i < vport->num_tx_rings; i++) {
		tx_ring = vport->tx_rings + i;
		/* TX ring ident must be an even number */
		tx_ring->ident = i * 2;
		mutex_init(&tx_ring->tx_lock, NULL, MUTEX_DRIVER, NULL);
		tx_ring->dring = &vport->tx_dring;
		tx_ring->vport = vport;
	}

	/*
	 * Allocate memory space for rx rings
	 */
	vport->rx_rings = kmem_zalloc(
	    sizeof (vpci_rx_ring_t) * vport->num_rx_rings,
	    KM_NOSLEEP);

	if (vport->rx_rings == NULL)
		return (DDI_FAILURE);

	for (int i = 0; i < vport->num_rx_rings; i++) {
		rx_ring = vport->rx_rings + i;
		/* RX ring ident must be an odd number */
		rx_ring->ident = i * 2 + 1;
		mutex_init(&rx_ring->rx_lock, NULL, MUTEX_DRIVER, NULL);
		rx_ring->dring = &vport->rx_dring;
		rx_ring->vport = vport;
	}

	return (DDI_SUCCESS);
}

/*
 * Function:
 *	vpci_free_rings()
 *
 * Description:
 * 	Free driver rx and tx rings data structures
 *
 * Arguments:
 *	vport	- Virtual pci port pointer
 *
 * Return Code:
 * 	DDI_SUCCESS	- Success
 * 	DDI_FAILURE	- Failure
 */
void
vpci_free_rings(vpci_port_t *vport)
{
	vpci_tx_ring_t *tx_ring = NULL;
	vpci_rx_ring_t *rx_ring = NULL;

	if (vport->tx_rings != NULL) {
		for (int i = 0; i < vport->num_tx_rings; i++) {
			tx_ring = vport->tx_rings + i;
			mutex_destroy(&tx_ring->tx_lock);
		}

		kmem_free(vport->tx_rings,
		    sizeof (vpci_tx_ring_t) * vport->num_tx_rings);
		vport->tx_rings = NULL;
	}

	if (vport->rx_rings != NULL) {
		for (int i = 0; i < vport->num_rx_rings; i++) {
			rx_ring = vport->rx_rings + i;
			mutex_destroy(&rx_ring->rx_lock);
		}

		kmem_free(vport->rx_rings,
		    sizeof (vpci_rx_ring_t) * vport->num_rx_rings);
		vport->rx_rings = NULL;
	}
}

/*
 * Function:
 *	vpci_create_dring()
 *
 * Description:
 * 	Create LDC dring resources
 *
 * Arguments:
 *	vport	- Virtual pci port
 *
 * Return Code:
 *	0	- Success
 *	EINVAL	- Invalid input
 */
static int
vpci_create_dring(vpci_port_t *vport)
{
	vpci_dring_t	*dring = &vport->tx_dring;
	vpci_t		*vpci = vport->vpci;
	int		rv = 0;

	dring->ident = VPCI_DRING_IDENT;
	dring->entrysize = sizeof (vpci_dring_entry_t);
	dring->nentry = VPCI_DRING_LEN;

	DMSG(vpci, 3, "[%d] ldc_mem_dring_create\n", vpci->instance);

	rv = ldc_mem_dring_create(dring->nentry, dring->entrysize, &dring->hdl);
	if ((dring->hdl == NULL) || (rv != 0)) {
		DMSG(vpci, 0, "[%d] tx descriptor ring creation failed",
		    vpci->instance);
		return (rv);
	}

	DMSG(vpci, 3, "[%d] ldc_mem_dring_bind\n", vpci->instance);
	dring->cookie = kmem_zalloc(sizeof (ldc_mem_cookie_t), KM_SLEEP);

	rv = ldc_mem_dring_bind(vport->ldc_handle, dring->hdl,
	    LDC_SHADOW_MAP|LDC_DIRECT_MAP, LDC_MEM_RW,
	    &dring->cookie[0], &dring->ncookie);
	if (rv != 0) {
		DMSG(vpci, 0, "[%d] Failed to bind descriptor ring "
		    "(%lx) to channel (%lx) rv=%d\n",
		    vpci->instance, dring->hdl, vport->ldc_handle, rv);
		return (rv);
	}
	ASSERT(dring->ncookie == 1);

	rv = ldc_mem_dring_info(dring->hdl, &dring->mem_info);
	if (rv != 0) {
		DMSG(vpci, 0,
		    "[%d] Failed to get info for tx descriptor ring (%lx)\n",
		    vpci->instance, dring->hdl);
		return (rv);
	}

	/*
	 * Mark all DRing entries as free and initialize the private
	 * descriptor's memory handles. If any entry is initialized,
	 * we need to free it later so we set the bit in 'initialized'
	 * at the start.
	 */
	for (int i = 0; i < dring->nentry; i++) {
		VPCI_MARK_DRING_ENTRY_FREE(dring, i);
	}

	DMSG(vpci, 3, "[%d] local dring marked\n", vpci->instance);

	return (rv);
}

/*
 * Function:
 *	vpci_destroy_dring()
 *
 * Description:
 * 	Destroy LDC dring resources
 *
 * Arguments:
 *	vport	- Virtual pci port
 *
 * Return Code:
 * 	N/A
 */
static void
vpci_destroy_dring(vpci_port_t *vport)
{
	vpci_dring_t	*dring = &vport->tx_dring;
	vpci_t		*vpci = vport->vpci;
	int		rv = -1;

	DMSG(vpci, 3, "[%d] vpci_dring_destroy Entered\n", vpci->instance);

	if (dring->ncookie != 0) {
		DMSG(vpci, 0, "[%d] Unbinding DRing\n", vpci->instance);
		rv = ldc_mem_dring_unbind(dring->hdl);
		if (rv != 0) {
			DMSG(vpci, 0, "[%d] Error %d unbinding DRing %lx",
			    vpci->instance, rv, dring->hdl);
		}
		kmem_free(dring->cookie, sizeof (ldc_mem_cookie_t));
		dring->ncookie = 0;
	}

	if (dring->hdl != NULL) {
		DMSG(vpci, 0, "[%d] Destroying DRing\n", vpci->instance);
		rv = ldc_mem_dring_destroy(dring->hdl);
		if (rv == 0) {
			dring->hdl = NULL;
			bzero(&dring->mem_info, sizeof (ldc_mem_info_t));
		} else {
			DMSG(vpci, 0, "[%d] Error %d destroying DRing (%lx)",
			    vpci->instance, rv, dring->hdl);
		}
	}
}

/*
 * Function:
 *	vpci_init_tx_ring()
 *
 * Description:
 * 	Initialize tx rings LDC dring resources
 *
 * Arguments:
 *	tx_ring - Tx ring pointer
 *	idx	- Ring index
 *
 * Return Code:
 *	0	- Success
 *	EINVAL	- Invalid input
 */
static int
vpci_init_tx_ring(vpci_tx_ring_t *tx_ring)
{
	vpci_port_t	*vport = tx_ring->vport;
	int		rv = 0;

	ASSERT(tx_ring != NULL);
	ASSERT(mutex_owned(&tx_ring->tx_lock));

	/* Initialize dring info, multiple tx_rings share one dring */
	tx_ring->nentry = tx_ring->dring->nentry / vport->num_tx_rings;
	tx_ring->dep = VPCI_GET_DRING_ENTRY_PTR(tx_ring);

	/* Initialize the starting index */
	tx_ring->rsp_cons = 0;
	tx_ring->req_prod = 0;
	tx_ring->req_send = 0;
	tx_ring->rsp_peer_prod = 0;

	/* Initialize the tx request */
	tx_ring->req_head = tx_ring->req_tail = NULL;

	return (rv);
}

/*
 * Function:
 *	vpci_init_tx_rings()
 *
 * Description:
 * 	Initialize multiple tx rings
 *
 * Arguments:
 *	vport	- Virtual pci port
 *
 * Return Code:
 *	0		- Success
 *	EINVAL		- Invalid input
 *	ECONNRESET	- Reset request is asserted
 */
int
vpci_init_tx_rings(vpci_port_t *vport)
{
	vpci_tx_ring_t	*tx_ring = NULL;
	int		rv = 0;

	ASSERT(RW_WRITE_HELD(&vport->hshake_lock));

	if ((rv = vpci_create_dring(vport)) != 0)
		return (rv);

	for (int i = 0; i < vport->num_tx_rings; i++) {
		tx_ring = vport->tx_rings + i;
		mutex_enter(&tx_ring->tx_lock);
		if ((rv = vpci_init_tx_ring(tx_ring)) != 0) {
			mutex_exit(&tx_ring->tx_lock);
			return (rv);
		}
		mutex_exit(&tx_ring->tx_lock);
	}
	return (rv);
}

/*
 * Function:
 *	vpci_fini_tx_rings()
 *
 * Description:
 * 	Finish multiple tx rings
 *
 * Arguments:
 *	vport	- Virtual pci port
 *
 * Return Code:
 * 	N/A
 */
void
vpci_fini_tx_rings(vpci_port_t *vport)
{
	vpci_tx_ring_t	*tx_ring = NULL;

	for (int i = 0; i < vport->num_tx_rings; i++) {
		tx_ring = vport->tx_rings + i;
		mutex_enter(&tx_ring->tx_lock);
		/* Reset the tx request */
		tx_ring->req_head = tx_ring->req_tail = NULL;
		mutex_exit(&tx_ring->tx_lock);
	}

	vpci_destroy_dring(vport);
}

/*
 * Function:
 *	vpci_tx_ring_get()
 *
 * Description:
 * 	Select a tx ring for a given pkt_type
 *
 * Arguments:
 *	vport		- Virtual pci port
 *	pkt_type	- PCIv packet
 *
 * Return Code:
 * 	vpci_tx_ring_t*	- TX ring pointer
 */
vpci_tx_ring_t *
vpci_tx_ring_get(vpci_port_t *vport, pciv_pkt_t *pkt)
{
	ASSERT(pkt->hdr.type <= vport->num_tx_rings);

	return (vport->tx_rings + pkt->hdr.type);
}

/*
 * Function:
 *	vpci_map_dring()
 *
 * Description:
 * 	Map and initialize LDC dring resources
 *
 * Arguments:
 *	vport	- Virtual pci port
 *
 * Return Code:
 *	0	- Success
 *	EINVAL	- Invalid input
 *	ENXIO	- IO error
 */
static int
vpci_map_dring(vpci_port_t *vport)
{
	vpci_dring_t	*dring = &vport->rx_dring;
	vpci_t		*vpci = vport->vpci;
	int		rv;

	ASSERT(RW_WRITE_HELD(&vport->hshake_lock));

	rv = ldc_mem_dring_map(vport->ldc_handle, dring->cookie,
	    dring->ncookie, dring->nentry, dring->entrysize,
	    LDC_SHADOW_MAP, &dring->hdl);
	if (rv != 0) {
		DMSG(vpci, 0, "[%d]ldc_mem_dring_map() returned errno %d",
		    vpci->instance, rv);
		return (rv);
	}

	/*
	 * To remove the need for this assertion, must call
	 * ldc_mem_dring_nextcookie() successfully ncookies-1 times after a
	 * successful call to ldc_mem_dring_map()
	 */
	ASSERT(dring->ncookie == 1);

	if ((rv = ldc_mem_dring_info(dring->hdl, &dring->mem_info)) != 0) {
		DMSG(vpci, 0, "[%d]ldc_mem_dring_info() returned errno %d",
		    vpci->instance, rv);
		if ((rv = ldc_mem_dring_unmap(dring->hdl)) != 0)
			DMSG(vpci, 0,
			    "[%d]ldc_mem_dring_unmap() returned errno %d",
			    vpci->instance, rv);
		return (rv);
	}

	if (dring->mem_info.vaddr == NULL) {
		DMSG(vpci, 0, "[%d]Descriptor ring virtual address is NULL",
		    vpci->instance);
		return (ENXIO);
	}

	return (0);
}

/*
 * Function:
 *	vpci_unmap_dring()
 *
 * Description:
 * 	Unmap LDC dring resources
 *
 * Arguments:
 *	vport	- Virtual pci port
 *
 * Return Code:
 * 	N/A
 */
static void
vpci_unmap_dring(vpci_port_t *vport)
{
	vpci_dring_t	*dring = &vport->rx_dring;
	vpci_t		*vpci = vport->vpci;
	int		rv;

	if (dring->hdl != NULL) {
		if ((rv = ldc_mem_dring_unmap(dring->hdl)) != 0)
			DMSG(vpci, 0,
			    "[%d]ldc_mem_dring_unmap() returned errno %d",
			    vpci->instance, rv);
		dring->hdl = NULL;
	}
}

/*
 * Function:
 *	vpci_init_rx_ring()
 *
 * Description:
 * 	Initialize rx_ring
 *
 * Arguments:
 *	vport	- Virtual pci port
 *
 * Return Code:
 *	0	- Success
 *	EINVAL	- Invalid input
 */
int
vpci_init_rx_ring(vpci_rx_ring_t *rx_ring)
{
	vpci_port_t	*vport = rx_ring->vport;
	vpci_t		*vpci = vport->vpci;
	int		rv;

	/* Initialize dring info, multiple rx_rings share one dring */
	rx_ring->nentry = rx_ring->dring->nentry / vport->num_rx_rings;
	rx_ring->dep = VPCI_GET_DRING_ENTRY_PTR(rx_ring);

	/* Initialize the starting index */
	rx_ring->req_cons = 0;
	rx_ring->rsp_prod = 0;
	rx_ring->req_peer_prod = 0;

	/* Allocate and initialize rx request */
	if (rx_ring->req == NULL)
		rx_ring->req = kmem_zalloc(rx_ring->nentry *
		    sizeof (vpci_rx_req_t), KM_SLEEP);

	for (int i = 0; i < rx_ring->nentry; i++) {
		rx_ring->req[i].idx = i;
		rx_ring->req[i].next = i + 1;

		if ((rv = ldc_mem_alloc_handle(vport->ldc_handle,
		    &rx_ring->req[i].ldc_mhdl)) != 0) {
			DMSG(vpci, 0, "[%d]ldc_mem_alloc_handle() error=%d",
			    vpci->instance, rv);
			return (ENXIO);
		}

	}

	rx_ring->req[rx_ring->nentry - 1].next = -1;
	rx_ring->latest_req = -1;
	rx_ring->free_req = 0;

	return (0);
}

/*
 * Function:
 *	vpci_init_rx_rings()
 *
 * Description:
 * 	Initialize multiple rx rings
 *
 * Arguments:
 *	vport	- Virtual pci port
 *
 * Return Code:
 *	0		- Success
 *	EINVAL		- Invalid input
 *	ECONNRESET	- Reset request is asserted
 */
int
vpci_init_rx_rings(vpci_port_t *vport)
{
	vpci_rx_ring_t	*rx_ring = NULL;
	int		rv = 0;

	ASSERT(RW_WRITE_HELD(&vport->hshake_lock));

	if ((rv = vpci_map_dring(vport)) != 0)
		return (rv);

	for (int i = 0; i < vport->num_rx_rings; i++) {
		rx_ring = vport->rx_rings + i;
		mutex_enter(&rx_ring->rx_lock);
		if ((rv = vpci_init_rx_ring(rx_ring)) != 0) {
			mutex_exit(&rx_ring->rx_lock);
			return (rv);
		}
		mutex_exit(&rx_ring->rx_lock);
	}
	return (rv);
}

/*
 * Function:
 *	vpci_fini_rx_ring()
 *
 * Description:
 * 	Finish rx_ring resources
 *
 * Arguments:
 *	rx_ring - Rx ring pointer
 *
 * Return Code:
 * 	N/A
 */
static void
vpci_fini_rx_ring(vpci_rx_ring_t *rx_ring)
{
	if (rx_ring->req != NULL) {
		for (int i = 0; i < rx_ring->nentry; i++) {
			if (rx_ring->req[i].ldc_mhdl) {
				(void) ldc_mem_free_handle
				    (rx_ring->req[i].ldc_mhdl);
				rx_ring->req[i].ldc_mhdl = NULL;
			}
		}

		kmem_free(rx_ring->req,
		    rx_ring->nentry * sizeof (vpci_rx_req_t));
		rx_ring->req = NULL;
	}
}

/*
 * Function:
 *	vpci_fini_rx_rings()
 *
 * Description:
 * 	Finish multiple rx rings
 *
 * Arguments:
 *	vport	- Virtual pci port
 *
 * Return Code:
 *	N/A
 */
void
vpci_fini_rx_rings(vpci_port_t *vport)
{
	vpci_rx_ring_t	*rx_ring = NULL;

	for (int i = 0; i < vport->num_rx_rings; i++) {
		rx_ring = vport->rx_rings + i;
		ASSERT(rx_ring != NULL);
		mutex_enter(&rx_ring->rx_lock);
		vpci_fini_rx_ring(rx_ring);
		mutex_exit(&rx_ring->rx_lock);
	}

	vpci_unmap_dring(vport);
}

/*
 * Function:
 *	vpci_tx_ring_get_by_ident()
 *
 * Description:
 * 	Select a tx ring according to ring ident
 *
 * Arguments:
 *	vport		- Virtual pci port
 *	ident		- Ring ident
 *
 * Return Code:
 * 	tx_ring		- TX ring pointer
 * 	NULL		- Can not find a TX ring
 */
vpci_tx_ring_t *
vpci_tx_ring_get_by_ident(vpci_port_t *vport, uint8_t ident)
{
	vpci_tx_ring_t	*tx_ring = NULL;

	for (int i = 0; i < vport->num_tx_rings; i ++) {
		tx_ring = vport->tx_rings + i;
		if (tx_ring->ident == ident)
			break;
		else
			tx_ring = NULL;
	}

	return (tx_ring);
}

/*
 * Function:
 *	vpci_rx_ring_get_by_ident()
 *
 * Description:
 * 	Select a rx ring according to ring ident
 *
 * Arguments:
 *	vport		- Virtual pci port
 *	ident		- Ring ident
 *
 * Return Code:
 * 	rx_ring		- RX ring pointer
 * 	NULL		- Can not find a RX ring
 */
vpci_rx_ring_t *
vpci_rx_ring_get_by_ident(vpci_port_t *vport, uint8_t ident)
{
	vpci_rx_ring_t	*rx_ring = NULL;

	for (int i = 0; i < vport->num_rx_rings; i ++) {
		rx_ring = vport->rx_rings + i;
		if (rx_ring->ident == ident)
			break;
		else
			rx_ring = NULL;
	}

	return (rx_ring);
}

/*
 * Function:
 *	vpci_rx_ring_acquire()
 *
 * Description:
 * 	Acquire descriptors of rx ring by start end index
 *
 * Arguments:
 *	vport		- Virtual pci port
 *	otd		- trap protection data structure poniter
 *	start_idx	- start descriptor index
 *	end_idx		- end descriptor index
 *
 * Return Code:
 *	0		- Success
 *	EINVAL		- Invalid input
 *	ECONNRESET	- LDC channel is not UP
 */
int
vpci_rx_ring_acquire(vpci_rx_ring_t *rx_ring, on_trap_data_t *otd,
    uint32_t start_idx, uint32_t end_idx)
{
	vpci_port_t	*vport = rx_ring->vport;
	vpci_t		*vpci = vport->vpci;
	int		rv;

	_NOTE(ARGUNUSED(otd));

	DMSG(vpci, 3, "[%d]Dring acquire start=%u end=%u", vpci->instance,
	    (uint32_t)VPCI_GET_RING_GLOBAL_IDX(rx_ring, start_idx),
	    (uint32_t)VPCI_GET_RING_GLOBAL_IDX(rx_ring, end_idx));

	rv = VIO_DRING_ACQUIRE(otd, LDC_SHADOW_MAP,
	    rx_ring->dring->hdl, VPCI_GET_RING_GLOBAL_IDX(rx_ring, start_idx),
	    VPCI_GET_RING_GLOBAL_IDX(rx_ring, end_idx));

	if (rv != 0)
		DMSG(vpci, 0, "[%d]vpci_rx_ring_acquire failed(errno=%d)",
		    vpci->instance, rv);

	return (rv);
}

/*
 * Function:
 *	vpci_rx_ring_release()
 *
 * Description:
 * 	Release descriptors of rx ring by start end index
 *
 * Arguments:
 *	vport		- Virtual pci port
 *	start_idxi	- start descriptor index
 *	end_idx		- end descriptor index
 *
 * Return Code:
 *	0		- Success
 *	EINVAL		- Invalid input
 *	ECONNRESET	- LDC channel is not UP
 */
int
vpci_rx_ring_release(vpci_rx_ring_t *rx_ring, uint32_t start_idx,
    uint32_t end_idx)
{
	vpci_port_t	*vport = rx_ring->vport;
	vpci_t		*vpci = vport->vpci;
	int		rv;

	DMSG(vpci, 3, "[%d]Dring release start=%u end=%u", vpci->instance,
	    (uint32_t)VPCI_GET_RING_GLOBAL_IDX(rx_ring, start_idx),
	    (uint32_t)VPCI_GET_RING_GLOBAL_IDX(rx_ring, end_idx));

	rv = VIO_DRING_RELEASE(LDC_SHADOW_MAP,
	    rx_ring->dring->hdl, VPCI_GET_RING_GLOBAL_IDX(rx_ring, start_idx),
	    VPCI_GET_RING_GLOBAL_IDX(rx_ring, end_idx));

	if (rv != 0)
		DMSG(vpci, 0, "[%d]vpci_rx_ring_release failed(errno=%d)",
		    vpci->instance, rv);

	return (rv);
}

/*
 * Function:
 *	vpci_rx_ring_get_req_entry()
 *
 * Description:
 * 	Get a new rx descriptor which contains new receving data request
 *
 * Arguments:
 *	rx_ring	- Soft rx ring pointer
 *
 * Return Code:
 *	desc	- Pointer to a LDC dring entry
 *	NULL	- No available entry
 */
vpci_dring_entry_t *
vpci_rx_ring_get_req_entry(vpci_rx_ring_t *rx_ring)
{
	vpci_port_t		*vport = rx_ring->vport;
	vpci_t			*vpci = vport->vpci;
	vpci_dring_entry_t	*desc = NULL;
	boolean_t		ready;

	if (VPCI_RX_RING_HAS_UNCONSUM_REQ(rx_ring))
		desc = VPCI_GET_RING_ENTRY(rx_ring, rx_ring->req_cons++);
	else
		return (NULL);

	/* Accept the updated dring desc */
	ready = (desc->hdr.dstate == VIO_DESC_READY);
	if (ready) {
		desc->hdr.dstate = VIO_DESC_ACCEPTED;
	} else {
		DMSG(vpci, 0, "[%d]descriptor %u not ready",
		    vpci->instance, rx_ring->req_cons - 1);
		VPCI_DUMP_DRING_ENTRY(vpci, desc);
		return (NULL);
	}
	return (desc);
}

/*
 * Function:
 *	vpci_rx_ring_get_rsp_entry()
 *
 * Description:
 * 	Get a new rx descriptor which will store rx response to peer side
 *
 * Arguments:
 *	rx_ring	- Soft rx ring pointer
 *
 * Return Code:
 *	desc	- Pointer to a LDC dring entry
 *	NULL	- No available entry
 */
vpci_dring_entry_t *
vpci_rx_ring_get_rsp_entry(vpci_rx_ring_t *rx_ring)
{
	vpci_port_t		*vport = rx_ring->vport;
	vpci_t			*vpci = vport->vpci;
	vpci_dring_entry_t	*desc = NULL;
	boolean_t		accepted;

	desc = VPCI_GET_RING_ENTRY(rx_ring, rx_ring->rsp_prod++);

	ASSERT(rx_ring->rsp_prod <= rx_ring->req_cons);

	/* Accept the updated dring desc */
	accepted = (desc->hdr.dstate == VIO_DESC_ACCEPTED);
	if (accepted) {
		desc->hdr.dstate = VIO_DESC_DONE;
	} else {
		DMSG(vpci, 0, "[%d]descriptor %u not accepted",
		    vpci->instance, rx_ring->rsp_prod - 1);
		VPCI_DUMP_DRING_ENTRY(vpci, desc);
		return (NULL);
	}
	return (desc);
}

/*
 * Function:
 *	vpci_tx_ring_avail_slots()
 *
 * Description:
 * 	Get free tx descriptors number
 *
 * Arguments:
 *	tx_ring	- Soft tx ring pointer
 *
 * Return Code:
 *	>0	- Free descriptors number
 *	0	- No available descriptors
 */
uint32_t
vpci_tx_ring_avail_slots(vpci_tx_ring_t *tx_ring)
{
	return (VPCI_TX_RING_AVAIL_SLOTS(tx_ring));
}

/*
 * Function:
 *	vpci_tx_ring_get_req_entry()
 *
 * Description:
 * 	Get a new tx descriptor which will store new sending data request
 *
 * Arguments:
 *	tx_ring	- Soft tx ring pointer
 *
 * Return Code:
 *	desc	- Pointer to a LDC dring entry
 *	NULL	- No available entry
 */
vpci_dring_entry_t *
vpci_tx_ring_get_req_entry(vpci_tx_ring_t *tx_ring)
{
	if (VPCI_TX_RING_AVAIL_SLOTS(tx_ring) != 0)
		return (VPCI_GET_RING_ENTRY(tx_ring, tx_ring->req_prod++));

	return (NULL);
}

/*
 * Function:
 *	vpci_tx_ring_get_rsp_entry()
 *
 * Description:
 * 	Get a new tx descriptor which contains tx response from peer side
 *
 * Arguments:
 *	tx_ring	- Soft tx ring pointer
 *
 * Return Code:
 *	desc	- Pointer to a LDC dring entry
 *	NULL	- No available entry
 */
vpci_dring_entry_t *
vpci_tx_ring_get_rsp_entry(vpci_tx_ring_t *tx_ring)
{
	if (VPCI_TX_RING_HAS_UNCONSUM_RSP(tx_ring))
		return (VPCI_GET_RING_ENTRY(tx_ring, tx_ring->rsp_cons++));

	return (NULL);
}
