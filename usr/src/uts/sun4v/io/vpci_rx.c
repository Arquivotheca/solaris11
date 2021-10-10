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
 * Function:
 *	vpci_rx_req_get()
 *
 * Description:
 * 	Get a free vpci_rx_req_t element
 *
 * Arguments:
 *	rx_ring	- Soft rx ring pointer
 *
 * Return Code:
 * 	req	- First free rx req pointer
 */
vpci_rx_req_t *
vpci_rx_req_get(vpci_rx_ring_t *rx_ring)
{
	vpci_rx_req_t *req;

	ASSERT(MUTEX_HELD(&rx_ring->rx_lock));

	/*
	 * Last one of req->next is initialized with -1,
	 * but it is not possible we get there, since the
	 * rx ring has limited rx descriptors.
	 */
	ASSERT(rx_ring->free_req != -1);
	req = &rx_ring->req[rx_ring->free_req];
	rx_ring->free_req = req->next;
	rx_ring->latest_req = req->idx;
	req->idx = rx_ring->latest_req;

	return (req);
}

/*
 * Function:
 *	vpci_rx_req_setup()
 *
 * Description:
 *	Populate the rx req by reading data from rx descriptors
 *
 * Arguments:
 *	rx_ring		- Soft rx ring pointer
 *	desc		- Rx descriptor pointer which need copy data from
 *	dring_msg	- dring msg incoming with an rx interrupt
 *
 * Return Code:
 * 	N/A
 */
static void
vpci_rx_req_setup(vpci_rx_ring_t *rx_ring, vpci_dring_entry_t *desc,
    vio_dring_msg_t *dring_msg)
{
	vpci_rx_req_t	*req = NULL;

	ASSERT(MUTEX_HELD(&rx_ring->rx_lock));

	req = vpci_rx_req_get(rx_ring);
	ASSERT(req != NULL);

	req->id = desc->payload.id;
	req->nbytes = desc->payload.hdr.size;
	ASSERT(req->dring_msg == NULL);
	req->dring_msg = dring_msg;
	ASSERT(req->pkt == NULL);

	req->ncookie = desc->payload.ncookie;
	for (int i = 0; i < req->ncookie; i ++)
		req->ldc_cookie[i] = desc->payload.cookie[i];
}

/*
 * Function:
 *	vpci_rx_req_return()
 *
 * Description:
 *	Return the rx req to a free state
 *
 * Arguments:
 *	rx_ring	- Soft rx ring pointer
 *	req	- Rx req pointer needs to be returned
 *
 * Return Code:
 * 	N/A
 */
static void
vpci_rx_req_return(vpci_rx_ring_t *rx_ring, vpci_rx_req_t *req)
{
	ldc_mem_handle_t ldc_mhdl;

	ASSERT(MUTEX_HELD(&rx_ring->rx_lock));

	/* Free last req's dring msg, since nobody reference it */
	if (rx_ring->rsp_prod == req->dring_msg->end_idx) {
		kmem_free(req->dring_msg, sizeof (vio_dring_msg_t));
	}

	req->dring_msg = NULL;
	/*
	 * Save pre-alloced mhdl before zero out the rx req
	 */
	ASSERT(req->ldc_mhdl != NULL);
	ldc_mhdl =  req->ldc_mhdl;
	bzero(req, sizeof (vpci_rx_req_t));
	req->ldc_mhdl = ldc_mhdl;

	req->next = rx_ring->free_req;
	rx_ring->free_req = req->idx;
}

/*
 * Function:
 *	vpci_push_response()
 *
 * Description:
 *	Push response data to the rx descriptors which have been received
 *
 * Arguments:
 *	rx_ring	- Soft rx ring pointer
 *	id	- Response id which copy from orignal tx request id
 *	dmsg	- Dring msg pointer coming with rx interrupt
 *	status	- Reponse code indicates the receiving results
 *
 * Return Code:
 * 	N/A
 */
static void
vpci_push_response(vpci_rx_ring_t *rx_ring, uint64_t id,
    vio_dring_msg_t *dmsg, uint8_t status)
{
	vpci_port_t		*vport = rx_ring->vport;
	vpci_t			*vpci = vport->vpci;
	vpci_dring_entry_t	*desc;
	on_trap_data_t		otd;
	uint32_t		start = rx_ring->rsp_prod;
	uint32_t		end = rx_ring->rsp_prod + 1;

	ASSERT(MUTEX_HELD(&rx_ring->rx_lock));

	/* acquire ring before operate on a desc */
	if (vpci_rx_ring_acquire(rx_ring, &otd, start, end - 1) != 0)
		return;

	desc = vpci_rx_ring_get_rsp_entry(rx_ring);
	ASSERT(desc != NULL);

	desc->payload.id = id;
	desc->payload.status = status;

	if (vpci_rx_ring_release(rx_ring, start, end - 1) != 0)
		return;

	ASSERT(rx_ring->rsp_prod <= dmsg->end_idx);
	dmsg->dring_ident ++;
	dmsg->start_idx = start;
	dmsg->end_idx = end;

	DMSG(vpci, 2, "[%d]ident=0x%lx, st=%u, end=%u, seq=%ld\n",
	    vpci->instance, rx_ring->ident,
	    dmsg->start_idx, dmsg->end_idx, dmsg->seq_num);

	/*
	 * Note we're holding the rx lock here to make sure
	 * the message goes out in order !!!...
	 */
	vpci_ack_msg(vport, (vio_msg_t *)dmsg, B_TRUE);
}

/*
 * Function:
 *	vpci_get_response_status()
 *
 * Description:
 * 	Translate DDI return code to virtual pci driver dring return code
 *
 * Arguments:
 *	rc	- DDI return code
 *
 * Return Code:
 * 	VPCI_RSP_xxx	- Virtual pci driver dring return code
 */
static int8_t
vpci_get_response_status(int rc)
{
	switch (rc) {
	case DDI_SUCCESS:
		return (VPCI_RSP_OKAY);
	case DDI_ENOTSUP:
		return (VPCI_RSP_ENOTSUP);
	case DDI_ENOMEM:
		return (VPCI_RSP_ENORES);
	case DDI_EINVAL:
		return (VPCI_RSP_ENODEV);
	case DDI_ETRANSPORT:
		return (VPCI_RSP_ETRANSPORT);
	case DDI_FAILURE:
	default:
		return (VPCI_RSP_ERROR);
	}
}

/*
 * Function:
 *	vpci_io_done()
 *
 * Description:
 * 	PCIV packet callback which pushes response to rx descriptors associated
 * 	with this packet.
 *
 * Arguments:
 *	arg	- Soft rx ring pointer
 *	pkt	- PCIV packet pointer
 *
 * Return Code:
 * 	N/A
 */
void
vpci_io_done(caddr_t arg, pciv_pkt_t *pkt)
{
	vpci_rx_ring_t	*rx_ring = (vpci_rx_ring_t *)(uintptr_t)arg;
	vpci_port_t	*vport = rx_ring->vport;
	vpci_t		*vpci = vport->vpci;
	vpci_rx_req_t	*req = PKT2RXREQ(pkt);
	size_t		buflen;
	int		rv;

	/*
	 * The buffer size has to be 8-byte aligned, so the client should have
	 * sent a buffer which size is roundup to the next 8-byte aligned value.
	 */
	buflen = P2ROUNDUP(req->nbytes, 8);

	/* release share memory buffer */
	rv = ldc_mem_release(req->ldc_mhdl, 0, buflen);
	if (rv) {
		DMSG(vpci, 0, "[%d]ldc_mem_release() returned err %d ",
		    vpci->instance, rv);
		rv = DDI_FAILURE;
	}

	rv = ldc_mem_unmap(req->ldc_mhdl);
	if (rv) {
		DMSG(vpci, 0, "[%d]ldc_mem_unmap() returned err %d ",
		    vpci->instance, rv);
		rv = DDI_FAILURE;
	}

	if (pkt->io_flag & PCIV_ERROR)
		rv = pkt->io_err;

	mutex_enter(&rx_ring->rx_lock);
	if (!VPCI_DRING_IS_INIT(rx_ring)) {
		mutex_exit(&rx_ring->rx_lock);
		DMSG(vpci, 0, "[%d]Rx rings is not initialized\n",
		    vpci->instance);
		return;
	}
	/* Send response back to peer side */
	vpci_push_response(rx_ring, req->id, req->dring_msg,
	    vpci_get_response_status(rv));

	vpci_rx_req_return(rx_ring, req);

	mutex_exit(&rx_ring->rx_lock);
}

/*
 * Function:
 *	vpci_rx_get_pkt()
 *
 * Description:
 * 	Create a PCIV packet based on a rx req by mapping the memory into
 * 	local IO domain.
 *
 * Arguments:
 *	rx_ring	- Soft rx ring pointer
 *
 * Return Code:
 * 	pkt	- PCIV packet pointer
 * 	NULL	- Fail to get the packets
 */
static pciv_pkt_t *
vpci_rx_get_pkt(vpci_rx_ring_t *rx_ring)
{
	vpci_port_t	*vport = rx_ring->vport;
	vpci_t		*vpci = vport->vpci;
	pciv_pkt_t	*pkt;
	vpci_rx_req_t	*req;
	caddr_t		buf = NULL;
	size_t		buflen;
	int		rv;

	req = &rx_ring->req[rx_ring->latest_req];

	ASSERT(req->ldc_mhdl != NULL);

	if (req->nbytes > vport->max_xfer_sz) {
		DMSG(vpci, 0, "[%d]Exceed max_xfer_sz, req->nbytes=%"PRIu64"\n",
		    vpci->instance, req->nbytes);
		return (NULL);
	}

	/* Map memory exported by client */
	rv = ldc_mem_map(req->ldc_mhdl, req->ldc_cookie, req->ncookie,
	    LDC_SHADOW_MAP, LDC_MEM_RW, &buf, NULL);

	if (rv != 0) {
		DMSG(vpci, 0, "[%d]ldc_mem_map() error(%d)",
		    vpci->instance, rv);
		return (NULL);
	}

	/*
	 * The buffer size has to be 8-byte aligned, so the client should have
	 * sent a buffer which size is roundup to the next 8-byte aligned value.
	 */
	buflen = P2ROUNDUP(req->nbytes, 8);

	rv = ldc_mem_acquire(req->ldc_mhdl, 0, buflen);
	if (rv != 0) {
		(void) ldc_mem_unmap(req->ldc_mhdl);
		DMSG(vpci, 0, "[%d]ldc_mem_acquire() returned err %d ",
		    vpci->instance, rv);
		return (NULL);
	}

	DMSG(vpci, 3, "[%d]ldc_mem_map() buf %p\n", vpci->instance,
	    (void *)buf);

	pkt = pciv_pkt_alloc((caddr_t)buf, req->nbytes, KM_SLEEP);

	/* Expose vport->domain_id as domain handle */
	pkt->src_domain = (uint64_t)vport->domain_id;
	pkt->priv_data = req;
	pkt->io_flag |= PCIV_BUSY;
	pkt->io_cb = vpci_io_done;
	pkt->cb_arg = (caddr_t)rx_ring;
	req->pkt = pkt;

	return (pkt);
}

/*
 * Function:
 *	vpci_response()
 *
 * Description:
 *	Push response data to a rx descriptor which can not be handled
 *	correctly. This routine is only used under error code path
 *
 * Arguments:
 *	rx_ring	- Soft rx ring pointer
 *	desc	- Pointer to a rx descriptor which need response
 *	dmsg	- Dring msg pointer coming with rx interrupt
 *	err	- Reponse code indicates the receiving results
 *
 * Return Code:
 * 	N/A
 */
static void
vpci_response(vpci_rx_ring_t *rx_ring, vpci_dring_entry_t *desc,
    vio_dring_msg_t *dmsg, int err)
{
	vpci_push_response(rx_ring, desc->payload.id, dmsg,
	    vpci_get_response_status(err));
}

/*
 * Function:
 *	vpci_rx_ring_clean()
 *
 * Description:
 * 	Copy receiving data from rx descriptors to a PCIV packet chain
 *
 * Arguments:
 *	rx_ring	- Soft rx ring pointer
 *	dring_msg - Dring msg pointer coming with rx interrupt
 *
 * Return Code:
 * 	pkt_head	- PCIV packet chain head pointer
 * 	NULL		- Fail to get the packets
 */
static pciv_pkt_t *
vpci_rx_ring_clean(vpci_rx_ring_t *rx_ring, vio_dring_msg_t *dring_msg)
{
	vpci_dring_entry_t	*desc;
	on_trap_data_t		otd;
	uint32_t		start = rx_ring->req_cons;
	uint32_t		end = rx_ring->req_peer_prod - 1;
	vpci_rx_req_t		*req;
	pciv_pkt_t		*pkt;
	pciv_pkt_t		*pkt_head = NULL;
	pciv_pkt_t		*pkt_tail = NULL;

	/* acquire ring before operate on a desc */
	if (vpci_rx_ring_acquire(rx_ring, &otd, start, end) != 0)
		return (NULL);

	while (desc = vpci_rx_ring_get_req_entry(rx_ring)) {

		vpci_rx_req_setup(rx_ring, desc, dring_msg);

		if (vpci_rx_ring_release(rx_ring, start, end) != 0)
			return (NULL);

		/*
		 * It might cause the sleep, need release ring before
		 * calling pkt alloc.
		 */
		pkt = vpci_rx_get_pkt(rx_ring);

		/* Acquire ring before operate on a desc */
		if (vpci_rx_ring_acquire(rx_ring, &otd, start, end) != 0)
			return (NULL);

		if (pkt == NULL) {
			req = &rx_ring->req[rx_ring->latest_req];
			ASSERT(req != NULL);
			vpci_response(rx_ring, desc, req->dring_msg,
			    DDI_FAILURE);
			vpci_rx_req_return(rx_ring, req);
		}

		/* copy the pkt hdr from the desc */
		bcopy(&desc->payload.hdr, &pkt->hdr, sizeof (pciv_pkt_hdr_t));

		if (pkt_head == NULL) {
			pkt_head = pkt_tail = pkt;
		} else {
			pkt_tail->next = pkt;
			pkt_tail = pkt;
		}

	}

	/* Abandon the pkts if release fails */
	if (vpci_rx_ring_release(rx_ring, start, end) != 0)
		return (NULL);

	return (pkt_head);
}


/*
 * Function:
 *	vpci_dring_msg_rx()
 *
 * Description:
 * 	Process rx_ring according information in the dring message.
 *
 * Arguments:
 *	rx_ring		- Soft rx ring pointer
 *	dring_msg	- Dring message pointer
 *
 * Return Code:
 * 	0	- Success
 * 	EIO	- IO error
 */
int
vpci_dring_msg_rx(vpci_rx_ring_t *rx_ring, vio_dring_msg_t *dring_msg)
{
	vpci_port_t	*vport = rx_ring->vport;
	vpci_t		*vpci = vport->vpci;
	pciv_pkt_t	*pkt_chain = NULL;

	mutex_enter(&rx_ring->rx_lock);

	if (!VPCI_DRING_IS_INIT(rx_ring)) {
		mutex_exit(&rx_ring->rx_lock);
		DMSG(vpci, 0, "[%d]Rx rings is not initialized\n",
		    vpci->instance);
		return (EIO);
	}

	ASSERT(rx_ring->req_cons <= dring_msg->start_idx);

	/* Always point to next free entry */
	rx_ring->req_peer_prod = dring_msg->end_idx;

	pkt_chain = vpci_rx_ring_clean(rx_ring, dring_msg);

	mutex_exit(&rx_ring->rx_lock);

	if (pkt_chain != NULL)
		pciv_proxy_rx(vport->pciv_handle, pkt_chain);

	return (0);
}
