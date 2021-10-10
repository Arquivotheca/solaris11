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

uint64_t vpci_tx_check_interval = 2 * MILLISEC;	/* 2 seconds */
uint64_t vpci_tx_timeout = 5 * MILLISEC;	/* 5 seconds */

static boolean_t vpci_tx_watchdog_enable = B_TRUE;

/*
 * Function:
 *	vpci_get_err()
 *
 * Description:
 * 	Translate a vpci driver dring return code to a DDI return code
 *
 * Arguments:
 *	status	- Vpci driver dring return code
 *
 * Return Code:
 * 	DDI_xxx	- DDI return code
 */
static int
vpci_get_err(int status)
{
	switch (status) {
	case VPCI_RSP_OKAY:
		return (DDI_SUCCESS);
	case VPCI_RSP_ENOTSUP:
		return (DDI_ENOTSUP);
	case VPCI_RSP_ENORES:
		return (DDI_ENOMEM);
	case VPCI_RSP_ENODEV:
		return (DDI_EINVAL);
	case VPCI_RSP_ETRANSPORT:
		return (DDI_ETRANSPORT);
	case VPCI_RSP_ERROR:
	default:
		return (DDI_FAILURE);
	}
}

/*
 * Function:
 *	vpci_tx_req_clean()
 *
 * Description:
 * 	Get PCIv packet from tx request, unmap share memory, awake
 * 	sleeping sending threads which are associated with the tx req.
 *
 * Arguments:
 *	req	- Tx request pointer
 *	status	- Status defined for desc
 *
 * Return Code:
 * 	0	- SUCCESS
 * 	<0	- FAILURE
 */
static int
vpci_tx_req_clean(vpci_tx_req_t *req, uint16_t status)
{
	pciv_pkt_t	*pkt;
	int		rv;

	pkt = req->pkt;
	ASSERT(pkt != NULL);

	if ((pkt->io_err = vpci_get_err(status)) != 0)
		pkt->io_flag |= PCIV_ERROR;


	/*
	 * If the upper layer passed in a misaligned address we copied
	 * the data into an aligned buffer before sending it to LDC -
	 * we now copy it back to the original buffer.
	 */
	if (req->align_addr) {
		ASSERT(pkt->buf != NULL);

		bcopy(req->align_addr, pkt->buf, pkt->hdr.size);
		kmem_free(req->align_addr,
		    sizeof (caddr_t) *
		    P2ROUNDUP((size_t)pkt->hdr.size, 8));
		req->align_addr = NULL;
	}

	if ((rv = ldc_mem_unbind_handle(req->ldc_mhdl)) != 0) {
		pkt->io_err = DDI_FAILURE;
		pkt->io_flag |= PCIV_ERROR;
	}

	(void) ldc_mem_free_handle(req->ldc_mhdl);

	mutex_enter(&pkt->io_lock);
	ASSERT(pkt->io_flag & PCIV_BUSY);
	pkt->io_flag &= ~PCIV_BUSY;
	pkt->io_flag |= PCIV_DONE;
	cv_signal(&pkt->io_cv);
	mutex_exit(&pkt->io_lock);

	kmem_free(req, sizeof (vpci_tx_req_t));

	return (rv);
}

/*
 * Function:
 *	vpci_tx_ring_clean()
 *
 * Description:
 * 	Recycle tx descriptors which have response data, awake
 * 	sleeping sending threads which are associated with these
 * 	tx descriptors.
 *
 * Arguments:
 *	tx_ring	- Soft tx ring pointer
 *
 * Return Code:
 *	recycle_ndesc	- The number of tx descriptors recycled
 */
static int
vpci_tx_ring_clean(vpci_tx_ring_t *tx_ring)
{
	vpci_port_t		*vport = tx_ring->vport;
	vpci_t			*vpci = vport->vpci;
	vpci_dring_entry_t	*desc;
	vpci_tx_req_t		*req;
	uint64_t		id;
	uint16_t		status;
	uint16_t		recycle_ndesc = 0;
	int			rv;

	while (desc = vpci_tx_ring_get_rsp_entry(tx_ring)) {

		ASSERT(desc->hdr.dstate == VIO_DESC_DONE);

		id = desc->payload.id;
		status = desc->payload.status;
		desc->hdr.dstate = VIO_DESC_FREE;

		DMSG(vpci, 3, "[%d]resp: id %"PRIu64" status %d\n",
		    vpci->instance, id, status);

		req = (vpci_tx_req_t *)(uintptr_t)id;
		if ((rv = vpci_tx_req_clean(req, status)) != 0)
			DMSG(vpci, 0, "[%d]: I/O error, rv %d",
			    vpci->instance, rv);
		recycle_ndesc ++;
	}

	/* Update timestamp */
	tx_ring->last_access = ddi_get_lbolt();

	return (recycle_ndesc);
}

/*
 * Function:
 * 	vpci_drain_tx_ring()
 *
 * Description:
 * 	Drain tx descriptors, cleanup the descriptors which have
 * 	response data, pending descriptors will be cleaned too.
 * 	All sleeping sending threads which are associated with these
 * 	tx descriptors will be awoke with a proper return code.
 *
 * Arguments:
 *	tx_ring	- Soft tx ring pointer
 *
 * Return Code:
 *	recycle_ndesc	- The number of tx descriptors recycled
 */
static int
vpci_drain_tx_ring(vpci_tx_ring_t *tx_ring)
{
	vpci_port_t		*vport = tx_ring->vport;
	vpci_t			*vpci = vport->vpci;
	vpci_dring_entry_t	*desc;
	vpci_tx_req_t		*req;
	uint64_t		id;
	uint16_t		status;
	uint16_t		recycle_ndesc = 0;
	int			rv;

	ASSERT(mutex_owned(&tx_ring->tx_lock));

	for (int i = 0; i < tx_ring->nentry; i++) {

		desc = VPCI_GET_RING_ENTRY(tx_ring, i);
		ASSERT(desc != NULL);

		switch (desc->hdr.dstate) {
		case VIO_DESC_FREE:
			continue;
		case VIO_DESC_DONE:
			status = desc->payload.status;
			break;
		case VIO_DESC_READY:
		case VIO_DESC_ACCEPTED:
			status = (uint16_t)VPCI_RSP_ERROR;
			break;
		default:
			DMSG(vpci, 0,
			    "[%d]: Invalid Desc state, dstate %d, id %"PRIu64"",
			    vpci->instance, desc->hdr.dstate,
			    desc->payload.id);
			ASSERT(desc->payload.id == 0);
			continue;
		}

		id = desc->payload.id;
		desc->hdr.dstate = VIO_DESC_FREE;

		DMSG(vpci, 3, "[%d]resp: id %"PRIu64" status %d\n",
		    vpci->instance, id, status);

		req = (vpci_tx_req_t *)(uintptr_t)id;
		if ((rv = vpci_tx_req_clean(req, status)) != 0)
			DMSG(vpci, 0, "[%d]: I/O error, rv %d",
			    vpci->instance, rv);
		recycle_ndesc ++;
	}

	return (recycle_ndesc);
}

/*
 * Function:
 *	vpci_tx_req_get()
 *
 * Description:
 * 	Get a vpci_tx_req_t element which has been populated by sending data
 *
 * Arguments:
 *	tx_ring	- Soft tx ring pointer
 *
 * Return Code:
 * 	req	- Oldest initialized tx req pointer
 */
static vpci_tx_req_t *
vpci_tx_req_get(vpci_tx_ring_t *tx_ring)
{
	vpci_port_t	*vport = tx_ring->vport;
	vpci_t		*vpci = vport->vpci;
	vpci_tx_req_t	*req;

	ASSERT(MUTEX_HELD(&tx_ring->tx_lock));

	if (tx_ring->req_head == NULL) {
		return (NULL);
	} else {
		/* Check available TX descriptors */
		if (vpci_tx_ring_avail_slots(tx_ring) < 1) {
			DMSG(vpci, 2, "[%d]No descriptors available\n",
			    vpci->instance);
			return (NULL);
		}
		req = tx_ring->req_head;

		tx_ring->req_head = tx_ring->req_head->next;
		if (tx_ring->req_head == NULL)
			tx_ring->req_tail = NULL;
	}

	return (req);
}

/*
 * Function:
 *	vpci_drain_fini_tx_rings()
 *
 * Description:
 * 	Drain and fini all of tx rings for specific vitual PCI port
 *
 * Arguments:
 *	vport	- Virtual pci port
 *
 * Return Code:
 * 	N/A
 */
void
vpci_drain_fini_tx_rings(vpci_port_t *vport)
{
	vpci_tx_ring_t	*tx_ring = NULL;
	vpci_tx_req_t	*req = NULL;

	for (int i = 0; i < vport->num_tx_rings; i++) {
		tx_ring = vport->tx_rings + i;
		mutex_enter(&tx_ring->tx_lock);

		/* Clean tx ring, skip if the dring is not initialzed */
		if (!VPCI_DRING_IS_INIT(tx_ring)) {
			mutex_exit(&tx_ring->tx_lock);
			continue;
		} else {
			(void) vpci_drain_tx_ring(tx_ring);
		}

		/* clean tx req queue which was not put on the ring  */
		for (;;) {
			if ((req = vpci_tx_req_get(tx_ring)) == NULL)
				break;
			(void) vpci_tx_req_clean(req, VPCI_RSP_ETRANSPORT);
		}

		mutex_exit(&tx_ring->tx_lock);
	}

	vpci_fini_tx_rings(vport);
}

/*
 * Function:
 *	vpci_tx_watchdog_timer()
 *
 * Description:
 * 	Check the ring timestamp to see if tx hangs
 *
 * Arguments:
 *	vport	- Virtual pci port
 *
 * Return Code:
 *	N/A
 */
void
vpci_tx_watchdog_timer(vpci_port_t *vport)
{
	clock_t cur = ddi_get_lbolt(); /* in ticks */
	vpci_tx_ring_t *tx_ring;

	/*
	 * Holding the read lock should be safe, because only the reset taskq
	 * which holds write lock could uninstall the timer.
	 */
	rw_enter(&vport->hshake_lock, RW_READER);
	for (int i = 0; i < vport->num_tx_rings; i++) {
		tx_ring = vport->tx_rings + i;
		mutex_enter(&tx_ring->tx_lock);
		if ((VPCI_TX_RING_HAS_INCOMP_REQ(tx_ring) ||
		    VPCI_TX_RING_HAS_UNCONSUM_RSP(tx_ring)) && cur >
		    tx_ring->last_access +
		    drv_usectohz(vpci_tx_timeout*1000)) {
			/*
			 * Reset all rings and return immediately without
			 * reinstalling watchdog handler.
			 */
			DMSG(vport->vpci, 1, "Tx hang detect: ident=%"PRIu64" "
			    "rsp_cons=%u, req_prod=%u, rsp_peer_prod=%u\n",
			    tx_ring->ident, tx_ring->rsp_cons,
			    tx_ring->req_prod, tx_ring->rsp_peer_prod);

			if (vpci_tx_watchdog_enable)
				vpci_request_reset(vport, B_TRUE);

			tx_ring->last_access = 0;
			mutex_exit(&tx_ring->tx_lock);
			vport->tx_reset_cnt++;
			rw_exit(&vport->hshake_lock);
			return;
		}
		mutex_exit(&tx_ring->tx_lock);
	}

	/* Reinstall the watchdog handler when no hang is found. */
	vport->tx_tid =
	    timeout((void (*)(void *))vpci_tx_watchdog_timer,
	    (caddr_t)vport, drv_usectohz(vpci_tx_check_interval*1000));
	rw_exit(&vport->hshake_lock);
}

/*
 * Function:
 *	vpci_tx_notify()
 *
 * Description:
 *	Send out the dring message to trigger a rx interrupt in peer side
 *
 * Arguments:
 *	tx_ring	- Soft tx ring pointer
 *	ndesc	- Number of tx descriptors has been populated
 *
 * Return Code:
 * 	N/A
 */
static void
vpci_tx_notify(vpci_tx_ring_t *tx_ring, int ndesc)
{
	vpci_port_t	*vport = tx_ring->vport;
	vpci_t		*vpci = vport->vpci;
	vio_dring_msg_t	dmsg;
	size_t		msglen = sizeof (dmsg);
	int		rv;
	/*
	 * Send a msg with the DRing details to service side
	 */
	VIO_INIT_DRING_DATA_TAG(dmsg);
	VPCI_INIT_DRING_DATA_MSG_IDS(dmsg, vport);
	dmsg.dring_ident = tx_ring->ident;
	dmsg.start_idx = tx_ring->req_send;
	dmsg.end_idx = tx_ring->req_prod;
	/* Will get ndesc of ACKs from rx side */
	vport->local_seq += ndesc;

	DMSG(vpci, 2, "ident=0x%lx, st=%u, end=%u, seq=%ld\n",
	    tx_ring->ident, dmsg.start_idx, dmsg.end_idx, dmsg.seq_num);

	/*
	 * note we're still holding the tx lock here to make sure the
	 * the message goes out in order !!!...
	 */
	ASSERT(mutex_owned(&tx_ring->tx_lock));
	ASSERT(RW_READ_HELD(&vport->hshake_lock));
	rv = vpci_send(vport, (caddr_t)&dmsg, msglen);

	switch (rv) {
	case 0: /* EOK */
		/* Update req_send pointer */
		tx_ring->req_send = tx_ring->req_prod;
		DMSG(vpci, 1, "sent via LDC: rv=%d\n", rv);
		break;
	case ECONNRESET:
		/*
		 * vpci_send initiates the reset on failure.
		 * Since the transaction has already been put
		 * on the dring, will not update req_send pointer
		 * so that it can be retried when the channel is
		 * reset. Given that, it is ok to just return
		 * success even though the send failed.
		 */
		DMSG(vpci, 1, "LDC reset happened: rv=%d\n", rv);
		break;
	default:
		/*
		 * LDC reset will happen, will not update req_send
		 * pointer, so that it can be retried later.
		 */
		DMSG(vpci, 0, "unexpected error, rv=%d\n", rv);
	}

}

/*
 * Function:
 *	vpci_tx_fill_desc()
 *
 * Description:
 *	Populate tx descriptors by copying data from tx req
 *
 * Arguments:
 *	tx_ring	- Soft tx ring pointer
 *	req	- Tx req pointer
 *
 * Return Code:
 * 	N/A
 */
static void
vpci_tx_fill_desc(vpci_tx_ring_t *tx_ring, vpci_tx_req_t *req)
{
	vpci_port_t		*vport = tx_ring->vport;
	vpci_t			*vpci = vport->vpci;
	vpci_dring_entry_t	*desc;
	pciv_pkt_t		*pkt = req->pkt;

	ASSERT(MUTEX_HELD(&tx_ring->tx_lock));
	ASSERT(vpci_tx_ring_avail_slots(tx_ring) > 0);

	/* Get a TX descriptor and populate it by using TX req */

	desc = vpci_tx_ring_get_req_entry(tx_ring);
	ASSERT(desc != NULL);

	DMSG(vpci, 2, "[%d]Get new descriptors:%p\n",
	    vpci->instance, (void *)desc);

	bzero(desc, sizeof (vpci_dring_entry_t));

	desc->payload.id = (uint64_t)(uintptr_t)req;
	bcopy(&pkt->hdr, &desc->payload.hdr, sizeof (pciv_pkt_hdr_t));

	desc->payload.ncookie = req->ncookie;
	for (int i = 0; i < req->ncookie; i ++)
		desc->payload.cookie[i] = req->ldc_cookie[i];

	desc->hdr.dstate = VIO_DESC_READY;
	desc->hdr.ack = 1;
}

/*
 * Function:
 *	vpci_fill_tx_ring()
 *
 * Description:
 * 	Get initialzed tx reqs to fill them on the tx ring, and a rx interrupt
 * 	will be triggered in peer side before exit
 *
 * Arguments:
 *	tx_ring	- Soft tx ring pointer
 *
 * Return Code:
 * 	N/A
 */
static void
vpci_fill_tx_ring(vpci_tx_ring_t *tx_ring)
{
	vpci_port_t	*vport = tx_ring->vport;
	vpci_tx_req_t	*req = NULL;
	int		ndesc = 0;

	ASSERT(mutex_owned(&tx_ring->tx_lock));
	ASSERT(RW_READ_HELD(&vport->hshake_lock));

	for (;;) {
		if ((req = vpci_tx_req_get(tx_ring)) == NULL)
			break;

		vpci_tx_fill_desc(tx_ring, req);
		ndesc ++;
	}

	if (ndesc)
		vpci_tx_notify(tx_ring, ndesc);
}

/*
 * Function:
 *	vpci_dring_msg_tx()
 *
 * Description:
 * 	Process tx_ring according information in the dring message.
 *
 * Arguments:
 *	tx_ring		- Soft tx ring pointer
 *	dring_msg	- Dring message pointer
 *
 * Return Code:
 * 	0	- Success
 * 	EIO	- IO error
 */
int
vpci_dring_msg_tx(vpci_tx_ring_t *tx_ring, vio_dring_msg_t *dring_msg)
{
	vpci_port_t	*vport = tx_ring->vport;
	vpci_t		*vpci = vport->vpci;
	int		recycle_ndesc = 0;

	rw_enter(&vport->hshake_lock, RW_READER);
	mutex_enter(&tx_ring->tx_lock);
	if (!VPCI_DRING_IS_INIT(tx_ring)) {
		mutex_exit(&tx_ring->tx_lock);
		DMSG(vpci, 0, "[%d]Tx rings is not initialized\n",
		    vpci->instance);
		return (EIO);
	}
	ASSERT(tx_ring->rsp_cons <= dring_msg->start_idx);

	/* Always point to next free entry */
	tx_ring->rsp_peer_prod = dring_msg->end_idx;

	recycle_ndesc = vpci_tx_ring_clean(tx_ring);

	if (recycle_ndesc && (tx_ring->req_head != NULL)) {
		/* new descs available, resubmit tx reqs */
		vpci_fill_tx_ring(tx_ring);
	}

	mutex_exit(&tx_ring->tx_lock);
	rw_exit(&vport->hshake_lock);

	kmem_free(dring_msg, sizeof (vio_dring_msg_t));

	return (0);
}

/*
 * Function:
 *	vpci_tx_req_setup()
 *
 * Description:
 * 	Populate tx req by using LDC bind interfaces
 *
 * Arguments:
 *	tx_ring	- Soft tx ring pointer
 *	pkt	- PCIV packet pointer
 *
 * Return Code:
 * 	DDI_SUCCESS	- Success
 * 	DDI_FAILURE	- Failure
 */
static int
vpci_tx_req_setup(vpci_tx_ring_t *tx_ring, pciv_pkt_t *pkt)
{
	vpci_port_t		*vport = tx_ring->vport;
	vpci_t			*vpci = vport->vpci;
	caddr_t			vaddr = pkt->buf;
	size_t			nbytes = pkt->hdr.size;
	caddr_t			align_addr = NULL;
	ldc_mem_handle_t	mhdl;
	uint8_t			maptype;
	ldc_mem_cookie_t	ldc_cookie;
	uint32_t		ncookie;
	vpci_tx_req_t		*req = NULL;
	int			rv = 0;

	/*
	 * LDC expects any addresses passed in to be 8-byte aligned. We need
	 * to copy the contents of any misaligned buffers to a newly allocated
	 * buffer and bind it instead (and copy the the contents back to the
	 * original buffer passed in when depopulating the descriptor)
	 */
	if (((uint64_t)vaddr & 0x7) != 0) {
		align_addr = kmem_alloc(sizeof (caddr_t) *
		    P2ROUNDUP(nbytes, 8), KM_SLEEP);
		DMSG(vpci, 0, "[%d] Misaligned address %p reallocating "
		    "(buf=%p nb=%ld)\n",
		    vpci->instance, (void *)vaddr, (void *)align_addr,
		    nbytes);

		bcopy(vaddr, align_addr, nbytes);
		vaddr = align_addr;
	}

	if ((rv = ldc_mem_alloc_handle(vport->ldc_handle,
	    &mhdl)) != 0) {
		DMSG(vpci, 0, "[%d]ldc_mem_alloc_handle() error=%d",
		    vpci->instance, rv);
		goto fail;
	}

	maptype = LDC_IO_MAP|LDC_SHADOW_MAP|LDC_DIRECT_MAP;
	rv = ldc_mem_bind_handle(mhdl, vaddr, P2ROUNDUP(nbytes, 8),
	    maptype, LDC_MEM_RW, &ldc_cookie, &ncookie);
	DMSG(vpci, 2, "[%d] bound mem handle; ncookie=%d\n",
	    vpci->instance, ncookie);
	if (rv != 0) {
		DMSG(vpci, 0, "[%d] Failed to bind LDC memory handle "
		    "(mhdl=%p, buf=%p, err=%d)\n",
		    vpci->instance, (void *)mhdl, (void *)vaddr, rv);
		goto fail;
	}

	ASSERT(ncookie <= VPCI_MAX_COOKIES);

	req = kmem_zalloc(sizeof (vpci_tx_req_t), KM_SLEEP);
	if (align_addr)
		req->align_addr = align_addr;
	req->ldc_cookie[0] = ldc_cookie;
	req->ncookie = ncookie;
	/*
	 * Get the other cookies (if any).
	 */
	for (int i = 1; i < ncookie; i++) {
		rv = ldc_mem_nextcookie(mhdl, &req->ldc_cookie[i]);
		if (rv != 0) {
			DMSG(vpci, 0, "?[%d] Failed to get next cookie "
			    "(mhdl=%lx cnum=%d), err=%d",
			    vpci->instance, mhdl, i, rv);
			(void) ldc_mem_unbind_handle(mhdl);
			(void) ldc_mem_free_handle(mhdl);
			goto fail;
		}
	}

	req->ldc_mhdl = mhdl;
	req->pkt = pkt;

	rw_enter(&vport->hshake_lock, RW_READER);

	if (vpci_hshake_check(vport) != DDI_SUCCESS) {
		rw_exit(&vport->hshake_lock);
		(void) ldc_mem_unbind_handle(mhdl);
		(void) ldc_mem_free_handle(mhdl);
		goto fail;
	}

	mutex_enter(&tx_ring->tx_lock);
	if (tx_ring->req_head == NULL) {
		tx_ring->req_head = tx_ring->req_tail = req;
	} else {
		tx_ring->req_tail->next = req;
		tx_ring->req_tail = req;
	}
	mutex_exit(&tx_ring->tx_lock);

	rw_exit(&vport->hshake_lock);

	return (DDI_SUCCESS);

fail:
	if (align_addr)
		kmem_free(align_addr, sizeof (caddr_t) * P2ROUNDUP(nbytes, 8));

	if (req)
		kmem_free(req, sizeof (vpci_tx_req_t));

	return (DDI_FAILURE);
}

/*
 * Function:
 *	vpci_ring_tx()
 *
 * Description:
 * 	Per tx ring based transmitting interface
 *
 * Arguments:
 *	tx_ring	- Soft tx ring pointer
 *	pkt	- PCIV packet pointer
 *
 * Return Code:
 * 	N/A
 */
void
vpci_ring_tx(vpci_tx_ring_t *tx_ring, pciv_pkt_t *pkt)
{
	vpci_port_t	*vport = tx_ring->vport;
	vpci_t		*vpci = vport->vpci;
	int		rv = 0;

	ASSERT(tx_ring != NULL);

	if ((rv = vpci_tx_req_setup(tx_ring, pkt)) != DDI_SUCCESS) {
		pkt->io_flag |= PCIV_ERROR;
		pkt->io_err = rv;
		DMSG(vpci, 0, "[%d]tx req setup failed, rv=%d\n",
		    vpci->instance, rv);
		return;
	}

	rw_enter(&vport->hshake_lock, RW_READER);

	if (vpci_hshake_check(vport) != DDI_SUCCESS) {
		rw_exit(&vport->hshake_lock);
		return;
	}

	mutex_enter(&tx_ring->tx_lock);
	vpci_fill_tx_ring(tx_ring);
	mutex_exit(&tx_ring->tx_lock);

	rw_exit(&vport->hshake_lock);

	mutex_enter(&pkt->io_lock);
	while (pkt->io_flag & PCIV_BUSY) {
		cv_wait(&pkt->io_cv, &pkt->io_lock);
	}
	mutex_exit(&pkt->io_lock);

	ASSERT(pkt->io_flag & PCIV_DONE);

	DMSG(vpci, 2, "[%d] ring tx done, pkt->io_flag=%d\n",
	    vpci->instance, pkt->io_flag);
}
