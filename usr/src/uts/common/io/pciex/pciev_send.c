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
#include <sys/sunndi.h>
#include <sys/pcie.h>
#include <sys/pcie_impl.h>
#include <sys/pciev_impl.h>
#include <sys/modhash.h>
#include <sys/archsystm.h>
#include <sys/disp.h>
#include <sys/pci_cfgacc.h>
#include <sys/pci_cap.h>
#include <util/sscanf.h>

/* Global variables used by framework */
extern mod_hash_t	*i_pciv_proxy_hash;
extern uint_t		i_pciv_proxy_count;
extern krwlock_t	i_pciv_proxy_lock;
extern krwlock_t	i_pciv_send_lock;

/* Generic tx request */
typedef struct pciv_tx_req {
	uint32_t		rc_addr;
	uint32_t		src_addr;
	dom_id_t		dst_domain;
	uint32_t		dst_addr;
	pciv_pkt_type_t		pkt_type;
	caddr_t			buf;
	size_t			nbyte;
	buf_cb_t		buf_cb;
	caddr_t			cb_arg;
} pciv_tx_req_t;

/* Packet format for PF/VF local packet transmission */
typedef struct pciv_local_pkt {
	dev_info_t	*src_dip;
	dev_info_t	*dst_dip;
	pciv_pvp_req_t	req;
} pciv_local_pkt_t;

/*
 * Initialize the high PIL pkt queue.
 */
static void
pciv_high_pil_qinit(pciv_hpil_queue_t *queue)
{
	/* Clear the queue */
	bzero((void *)queue, sizeof (pciv_hpil_queue_t));
	for (int i = 0; i < PCIV_HIGH_PIL_QSIZE; i++) {
		/* Buf and nbytes will be initialized while transmitting */
		queue->pkt[i] = pciv_pkt_alloc(NULL, 0, KM_SLEEP);
	}
}

/*
 * Finish the high PIL pkt queue.
 */
static void
pciv_high_pil_qfini(pciv_hpil_queue_t *queue)
{
	for (int i = 0; i < PCIV_HIGH_PIL_QSIZE; i++) {
		/* Free the pkt of each bucket */
		pciv_pkt_free(queue->pkt[i]);
	}
	/* Reset the value */
	bzero((void *)queue, sizeof (pciv_hpil_queue_t));
}

/*
 * Get a bucket from the queue, copy it to a new allocated pkt.
 */
static pciv_pkt_t *
pciv_high_pil_getq(pciv_hpil_queue_t *queue)
{
	int		old_idx, new_idx;
	pciv_pkt_t	*pkt, *newpkt;

	/* The queue is empty if queue->qused is zero */
	if (atomic_cas_16(&queue->qused, 0, queue->qused) == 0)
		return (NULL);

	/*
	 * Grab the first bucket which contains tx request. Lockless design by
	 * using atomic operations.
	 */
	do {
		old_idx = queue->qhead;
		new_idx = old_idx + 1;
	} while (atomic_cas_16(&queue->qhead, old_idx, new_idx) != old_idx);

	pkt = queue->pkt[old_idx & (PCIV_HIGH_PIL_QSIZE - 1)];

	/* Allocate a pciv packet, copy from queue */
	newpkt = kmem_alloc(sizeof (pciv_pkt_t), KM_SLEEP);
	bcopy(pkt, newpkt, sizeof (pciv_pkt_t));

	/* Reset the bucket of pkt queue */
	bzero((void *)&pkt->hdr, sizeof (pciv_pkt_hdr_t));
	pkt->buf = NULL;
	pkt->src_domain = 0;
	pkt->dst_domain = 0;
	pkt->buf_cb = NULL;
	pkt->cb_arg = NULL;
	pkt->io_flag &= ~PCIV_BUSY;

	/* Return it as a free bucket */
	atomic_dec_16(&queue->qused);

	return (newpkt);
}

/*
 * Find a proxy driver instance from hash table.
 */
static pciv_proxy_t *
pciv_proxy_get(dom_id_t domain_id)
{
	mod_hash_val_t hv;
	pciv_proxy_t *proxy = NULL;

	rw_enter(&i_pciv_proxy_lock, RW_READER);
	if (i_pciv_proxy_count > 0) {
		if (mod_hash_find(i_pciv_proxy_hash,
		    (mod_hash_key_t)(uintptr_t)domain_id, &hv) == 0)
			proxy = (pciv_proxy_t *)hv;
	}
	rw_exit(&i_pciv_proxy_lock);

	return (proxy);
}

/*
 * Send out packet by calling into virutal proxy driver.
 * This function will select a proxy driver instance according to
 * destination address existing in pciv packet header. The packet
 * will be passed into virtual proxy driver via the entry points of
 * the proxy driver instance.
 */
static int
pciv_proxy_send(pciv_pkt_t *pkt)
{
	pciv_proxy_t	*proxy;
	int		rv;

	rw_enter(&i_pciv_send_lock, RW_READER);

	/* Select a proxy driver instance */
	if ((proxy = pciv_proxy_get(pkt->dst_domain)) == NULL) {
		rv = DDI_ENOTSUP;
		goto exit;
	}

	/*
	 * Check link availability. If the handshake is still on going,
	 * wait until get a success or fail state.
	 */
	if (proxy->check(proxy->state) != DDI_SUCCESS) {
		rv = DDI_ENOTSUP;
		goto exit;
	}

	/* Pass a packet to virtual proxy driver tx entry point */
	proxy->tx(proxy->state, pkt);
	rv = (pkt->io_flag & PCIV_ERROR) ? pkt->io_err : DDI_SUCCESS;
exit:
	rw_exit(&i_pciv_send_lock);
	return (rv);
}

/*
 * Packet creation by specify payload and memory allocation flag.
 */
pciv_pkt_t *
pciv_pkt_alloc(caddr_t buf, size_t nbyte, int flag)
{
	pciv_pkt_t *pkt;

	pkt = kmem_zalloc(sizeof (pciv_pkt_t), flag);
	pkt->buf = buf;
	pkt->hdr.size = (uint32_t)nbyte;
	pkt->io_flag = PCIV_INIT;
	cv_init(&pkt->io_cv, NULL, CV_DEFAULT, NULL);
	mutex_init(&pkt->io_lock, NULL, MUTEX_DEFAULT, NULL);

	return (pkt);
}

/*
 * Packet destroy
 */
void
pciv_pkt_free(pciv_pkt_t *pkt)
{
	mutex_destroy(&pkt->io_lock);
	cv_destroy(&pkt->io_cv);
	kmem_free(pkt, sizeof (pciv_pkt_t));
}

/*
 * Select a proxy instance to send the pkt, the buffer callback will
 * be called after the sending.
 */
static void
pciv_remote_pkt_post(void *arg)
{
	pciv_pkt_t	*pkt = (pciv_pkt_t *)arg;
	int		rv;

	rv = pciv_proxy_send(pkt);

	/*
	 * Invoke buf_cb which will process buffer
	 * according to transmission results
	 */
	pkt->buf_cb(rv, pkt->buf, pkt->hdr.size, pkt->cb_arg);
	pciv_pkt_free(pkt);
}

/*
 * Drain the pkts from the queue.
 *
 * It is invoked by a taskq, and working in an infinite loop. When
 * new pkts incoming, it is awaked by a soft interrupt thread. It will
 * not sleep until sending out all of pkts reading from the queue.
 */
static void
pciv_high_pil_qdrain(void *arg)
{
	pciv_tx_taskq_t		*taskq = (pciv_tx_taskq_t *)arg;
	pciv_hpil_queue_t	*queue = &taskq->hpil_queue;
	pciv_pkt_t		*pkt;

	mutex_enter(&taskq->hpil_taskq_lock);

	for (;;) {
		cv_wait(&taskq->hpil_taskq_cv, &taskq->hpil_taskq_lock);

		taskq->hpil_taskq_running = B_TRUE;
try_again:
		/* Drain tx request on the queue. */
		while ((pkt = pciv_high_pil_getq(queue)) != NULL)
			pciv_remote_pkt_post(pkt);

		if (taskq->hpil_taskq_running) {
			/*
			 * Clear the running flag, and try to read
			 * queue again, ensure no new data incoming
			 * during clearing the flag.
			 */
			taskq->hpil_taskq_running = B_FALSE;
			goto try_again;
		}

		/* Check exit flag after the thread awaked */
		if (taskq->hpil_taskq_exit)
			break;
	}

	mutex_exit(&taskq->hpil_taskq_lock);
}

/*
 * Soft interrupt handler which handles the soft interrupt
 * triggered by high PIL interrupt sending interface.
 */
static uint_t
pciv_tx_softint(caddr_t arg1, caddr_t arg2)
{
	pciv_tx_taskq_t	*taskq = (pciv_tx_taskq_t *)(void *)arg1;

	_NOTE(ARGUNUSED(arg2));

	/* If running flag set, doesn't need awake the taskq */
	if (!taskq->hpil_taskq_running) {
		/*
		 * Accquire the lock to ensure cv_signal is always after
		 * the taskq call cv_wait.
		 */
		mutex_enter(&taskq->hpil_taskq_lock);
		cv_signal(&taskq->hpil_taskq_cv);
		mutex_exit(&taskq->hpil_taskq_lock);
	}

	return (DDI_INTR_CLAIMED);
}

/*
 * Create tx taskqs and softint for a dip
 */
int
pciv_tx_taskq_create(dev_info_t	*dip, uint32_t task_id, boolean_t loopback_mode)
{
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);
	char		taskq_name[MAXTASKQNAME];
	pciv_tx_taskq_t	*taskq;

	ASSERT(bus_p != NULL);
	if (bus_p == NULL)
		return (DDI_EINVAL);

	if (bus_p->taskq == NULL) {
		taskq = (pciv_tx_taskq_t *)kmem_zalloc(sizeof (pciv_tx_taskq_t),
		    KM_SLEEP);
		bus_p->taskq = taskq;

		mutex_init(&taskq->hpil_taskq_lock, NULL, MUTEX_DEFAULT, NULL);
		cv_init(&taskq->hpil_taskq_cv, NULL, CV_DEFAULT, NULL);
		pciv_high_pil_qinit(&taskq->hpil_queue);
	} else {
		taskq = bus_p->taskq;
	}

	if (loopback_mode) {
		/* Virtual fabric doesn't need to create loopback taskq */
		if (!pcie_is_physical_fabric(dip))
			return (DDI_SUCCESS);

		/*
		 * The loopback taskq thread is created for dealing with the
		 * PF/VF sending requests while both transmission and receiving
		 * sides are in the same Domain.
		 */
		(void) snprintf(taskq_name, sizeof (taskq_name),
		    "pciv_loopback_taskq_%x", task_id);
		if ((taskq->lb_taskq = ddi_taskq_create(NULL, taskq_name,
		    1, TASKQ_DEFAULTPRI, 0)) == NULL) {
			cmn_err(CE_NOTE,
			    "pciv: pciv_loopback_taskq creation failed");
			goto fail;
		}

		return (DDI_SUCCESS);
	}

	/*
	 * The high PIL int taskq thread is created for dealing with the
	 * sending request from high PIL interrupt context.
	 */
	(void) snprintf(taskq_name, sizeof (taskq_name),
	    "pciv_hpil_taskq_%x", task_id);
	if ((taskq->hpil_taskq = ddi_taskq_create(NULL, taskq_name,
	    1, TASKQ_DEFAULTPRI, 0)) == NULL) {
		cmn_err(CE_NOTE, "pciv: pciv_hpil_taskq creation failed");
		goto fail;
	}

	/*
	 * Dispatch high PIL taskq here, since taskq dispatch could fail and
	 * we can not return error code to caller under softint context.
	 */
	taskq->hpil_taskq_exit = B_FALSE;
	if ((ddi_taskq_dispatch(taskq->hpil_taskq,
	    pciv_high_pil_qdrain,
	    (void *)taskq,
	    DDI_SLEEP)) != DDI_SUCCESS) {
		cmn_err(CE_NOTE, "pciv dispatch pciv_hpil_taskq failed\n");
		taskq->hpil_taskq_exit = B_TRUE;
		goto fail;
	}

	/*
	 * Add the softint so that high PIL interrupt could do a real
	 * transmission.
	 */
	if (ddi_intr_add_softint(dip, &taskq->hpil_softint_hdl,
	    DDI_INTR_SOFTPRI_MIN, pciv_tx_softint, (void *)taskq)
	    != DDI_SUCCESS) {
		cmn_err(CE_NOTE, "pciv: ddi_intr_add_softint failed");
		goto fail;
	}

	for (int i = 0; i < PCIV_PKT_TYPE_MAX; i ++) {
		/*
		 * The tx taskq threads are created for dealing with the
		 * non-blocking transmitting for interrupt context under
		 * the LOCK level.
		 */
		(void) snprintf(taskq_name, sizeof (taskq_name),
		    "pciv_tx_taskq_%x_%d", task_id, i);
		if ((taskq->tx_taskq[i] = ddi_taskq_create(NULL, taskq_name,
		    1, TASKQ_DEFAULTPRI, 0)) == NULL) {
			cmn_err(CE_NOTE, "pciv: pciv_tx_taskq creation failed");
			goto fail;
		}
	}

	return (DDI_SUCCESS);

fail:
	pciv_tx_taskq_destroy(dip);

	return (DDI_FAILURE);
}

/*
 * Destroy tx taskqs and softint for a specific dip
 */
void
pciv_tx_taskq_destroy(dev_info_t *dip)
{
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);
	pciv_tx_taskq_t	*taskq;

	if ((taskq = bus_p->taskq) == NULL)
		return;

	for (int i = 0; i < PCIV_PKT_TYPE_MAX; i ++) {
		if (taskq->tx_taskq[i] != NULL)
			ddi_taskq_destroy(taskq->tx_taskq[i]);
	}

	/* Acquire lock ensure no race with high PIL access */
	if (taskq->hpil_softint_hdl != NULL)
		(void) ddi_intr_remove_softint(taskq->hpil_softint_hdl);

	if (taskq->hpil_taskq != NULL) {
		/* Awake the taskq and wait the thread exiting */
		taskq->hpil_taskq_exit = B_TRUE;
		mutex_enter(&taskq->hpil_taskq_lock);
		cv_signal(&taskq->hpil_taskq_cv);
		mutex_exit(&taskq->hpil_taskq_lock);
		ddi_taskq_wait(taskq->hpil_taskq);

		ddi_taskq_destroy(taskq->hpil_taskq);
	}

	if (taskq->lb_taskq != NULL)
		ddi_taskq_destroy(taskq->lb_taskq);
	/*
	 * Both high PIL interrupt and high PIL taskqs will not
	 * access pkt queue here, it's safe to free the resource.
	 */
	pciv_high_pil_qfini(&taskq->hpil_queue);
	cv_destroy(&taskq->hpil_taskq_cv);
	mutex_destroy(&taskq->hpil_taskq_lock);

	kmem_free(taskq, sizeof (pciv_tx_taskq_t));
	bus_p->taskq = NULL;
}

/*
 * Return tx taskq for the request dip.
 */
static pciv_tx_taskq_t *
pciv_get_tx_taskq(dev_info_t *dip)
{
	dev_info_t	*rcdip;
	pcie_bus_t	*bus_p;

	/* Currently we uses per Root Complex tx taskqs */
	if ((rcdip = pcie_get_rc_dip(dip)) == NULL)
		return (NULL);

	bus_p = PCIE_DIP2BUS(rcdip);

	return (bus_p != NULL ? bus_p->taskq : NULL);
}

/*
 * Putting the tx quest on the queue, and trigger a softint.
 *
 * As current layer dones't have the knowlege of driver interrupt,
 * it is difficult to initialize one SPIN mutex for various possible
 * PILs above the LOCK level. We use a per Root Complex golbal ring
 * buffer which is synchronized by atomic oprations to avoid the races
 * among the tx requests from different driver's high PIL interrupts.
 * Tx taskqs that works together with the high PIL interrupt will also
 * need this synchronization to get incoming data from the queue.
 */
static int
pciv_high_pil_send(dev_info_t *dip, pciv_tx_req_t *req)
{
	pciv_tx_taskq_t		*taskq;
	pciv_hpil_queue_t	*queue;
	int			old_idx, new_idx;
	pciv_pkt_t		*pkt;
	int			rv;

	if ((taskq = pciv_get_tx_taskq(dip)) == NULL)
		return (DDI_ENOTSUP);

	queue = &taskq->hpil_queue;

	if (taskq->hpil_taskq_exit || (taskq->hpil_softint_hdl == NULL))
		return (DDI_ENOTSUP);

	/* Check to make sure the queue hasn't overflowed */
	if (atomic_inc_16_nv(&queue->qused) >= PCIV_HIGH_PIL_QSIZE) {
		queue->qfailed++;
		atomic_dec_16(&queue->qused);
		return (DDI_ENOMEM);
	}

	/*
	 * Grab the first bucket which contains tx request. Lockless design by
	 * using atomic operations.
	 */
	do {
		old_idx = queue->qtail;
		new_idx = old_idx + 1;
	} while (atomic_cas_16(&queue->qtail, old_idx, new_idx) != old_idx);

	pkt = queue->pkt[old_idx & (PCIV_HIGH_PIL_QSIZE - 1)];

	/* Populate the packet */
	ASSERT(pkt->io_flag == PCIV_INIT);
	pkt->hdr.type = req->pkt_type;
	pkt->hdr.rc_addr = req->rc_addr;
	pkt->hdr.src_addr = req->src_addr;
	pkt->hdr.dst_addr = req->dst_addr;
	pkt->hdr.size = req->nbyte;

	pkt->buf = req->buf;
	pkt->src_domain = 0;
	pkt->dst_domain = req->dst_domain;
	pkt->buf_cb = req->buf_cb;
	pkt->cb_arg = req->cb_arg;

	pkt->io_flag |= PCIV_BUSY;

	/* If running flag set, doesn't need trigger softint */
	if (!taskq->hpil_taskq_running) {
		rv = ddi_intr_trigger_softint(taskq->hpil_softint_hdl,
		    (caddr_t)taskq);
		switch (rv) {
		case DDI_SUCCESS:
		case DDI_EPENDING:
			/*
			 * Pending softint will be fired later, and
			 * it will drain all of tx requests on the
			 * queue.
			 */
			break;
		default:
			return (DDI_ENOMEM);
		}
	}

	return (DDI_SUCCESS);
}

/*
 * Send a buffer to another IO domain.
 * It will create a pciv packet for underlying virtual proxy driver.
 * Virtual proxy driver will put the packet on the wire.
 */
static int
pciv_generic_send(dev_info_t *dip, pciv_tx_req_t *req)
{
	pciv_tx_taskq_t	*taskq;
	pciv_pkt_t	*pkt;
	int		rv = DDI_SUCCESS;

	ASSERT((dip != NULL) && (req != NULL));

	/* Validate inputs */
	if ((req->dst_domain == 0) || (req->buf == NULL) ||
	    (req->nbyte == 0) || (req->nbyte > PCIV_MAX_BUF_SIZE))
		return (DDI_EINVAL);

	if (servicing_interrupt()) {
		/* Interrupt context must use buf_cb */
		if (req->buf_cb == NULL)
			return (DDI_EINVAL);

		if (getpil() > LOCK_LEVEL)
			return (pciv_high_pil_send(dip, req));
	}

	/* Allocate a pciv packet, buf as the packet payload */
	pkt = pciv_pkt_alloc(req->buf, req->nbyte,
	    (req->buf_cb == NULL ? KM_SLEEP : KM_NOSLEEP));

	/* Allocation might fail if KM_NOSLEEP set */
	if (pkt == NULL)
		return (DDI_ENOMEM);

	/* Populate the packet */
	pkt->hdr.type = req->pkt_type;
	pkt->hdr.rc_addr = req->rc_addr;
	pkt->hdr.src_addr = req->src_addr;
	pkt->hdr.dst_addr = req->dst_addr;

	/*
	 * The src_domain set by transmitting side will be ignored,
	 * receving side could get the src_domain by virtual proxy
	 * driver.
	 */
	pkt->src_domain = 0;
	pkt->dst_domain = req->dst_domain;
	pkt->buf_cb = req->buf_cb;
	pkt->cb_arg = req->cb_arg;

	pkt->io_flag |= PCIV_BUSY;

	if (req->buf_cb == NULL) {
		rv = pciv_proxy_send(pkt);
		pciv_pkt_free(pkt);
	} else {
		taskq = pciv_get_tx_taskq(dip);
		if (taskq == NULL || taskq->tx_taskq[req->pkt_type] == NULL)
			return (DDI_ENOTSUP);

		if ((ddi_taskq_dispatch(taskq->tx_taskq[req->pkt_type],
		    pciv_remote_pkt_post,
		    (void *)pkt,
		    DDI_NOSLEEP)) != DDI_SUCCESS) {
			cmn_err(CE_NOTE, "pciv start taskq"
			"for handle tx failed. \n");
			return (DDI_ENOMEM);
		}
	}

	return (rv);
}

/*
 * Get root complex address by giving any of a dip on the fabric
 */
uint32_t
pciv_get_rc_addr(dev_info_t *dip)
{
	dev_info_t	*rcdip = NULL;
	char		*name = NULL;
	uint32_t	rc_addr;
	int		n = 0;

	rcdip = pcie_get_rc_dip(dip);

	name = ddi_get_name_addr(rcdip);
	ASSERT(name != NULL);

	/*
	 * RC address should be extracted from 'reg' property of
	 * RC device node. We don't want to access OBP properties
	 * frequently, now we get it from the string of device path.
	 * The name string may contains ',' on x86 platform
	 */
	n = sscanf(name, "%x,", &rc_addr);
	ASSERT(n == 1);

	return ((n == 1) ? rc_addr : PCIV_INVAL_RC_ADDR);
}

/*
 * Get uniq device address cross the different Domains.
 */
static uint32_t
pciv_get_dev_addr(dev_info_t *dip)
{
	pcie_bus_t *bus_p = PCIE_DIP2BUS(dip);

	ASSERT(bus_p != NULL);

	return (bus_p->bus_bdf);
}

/*
 * Calculate VF BDF for given PF dip and VF function index.
 */
static pcie_req_id_t
pcie_get_vf_bdf_by_idx(dev_info_t *pf_dip, int vf_idx)
{
	pcie_bus_t		*bus_p = PCIE_DIP2BUS(pf_dip);
	ddi_acc_handle_t	config_handle;
	int			first_vf_offset, vf_stride, value;
	pcie_req_id_t		vf_bdf;

	config_handle = bus_p->bus_cfg_hdl;
	value = PCI_XCAP_GET16(config_handle, NULL,
	    bus_p->sriov_cap_ptr,
	    PCIE_EXT_CAP_SRIOV_VF_OFFSET_OFFSET);
	if ((value == PCI_CAP_EINVAL32) || (value == 0))
		return (0);
	first_vf_offset = (uint16_t)value;
	value = PCI_XCAP_GET16(config_handle, NULL,
	    bus_p->sriov_cap_ptr,
	    PCIE_EXT_CAP_SRIOV_VF_STRIDE_OFFSET);
	if (value == PCI_CAP_EINVAL32)
		return (0);
	vf_stride = (uint16_t)value;

	vf_bdf = bus_p->bus_bdf + first_vf_offset + ((vf_idx -1) * vf_stride);

	return (vf_bdf);
}

/*
 * Get VF dip for a PF dip by specifying a VF function index.
 */
dev_info_t *
pciv_get_vf_dip(dev_info_t *pf_dip, int vf_idx)
{
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(pf_dip);
	pcie_req_id_t	vf_bdf;
	pcie_bus_t	*cdip_busp;
	dev_info_t	*pdip, *cdip;

	if (!PCIE_IS_PF(bus_p))
		return (NULL);

	if ((vf_idx <= 0) || (vf_idx > bus_p->num_vf))
		return (NULL);

	vf_bdf = pcie_get_vf_bdf_by_idx(pf_dip, vf_idx);

	pdip = ddi_get_parent(pf_dip);
	for (cdip = ddi_get_child(pdip);
	    cdip; cdip = ddi_get_next_sibling(cdip)) {
		cdip_busp = PCIE_DIP2UPBUS(cdip);
		if ((cdip_busp->bus_func_type == FUNC_TYPE_VF) &&
		    (cdip_busp->bus_bdf == vf_bdf))
			return (cdip);
	}
	return (NULL);
}

/*
 * Get a VF index for given VF dip.
 */
int
pciv_get_vf_idx(dev_info_t *vf_dip)
{
	pcie_bus_t		*vf_bus_p = PCIE_DIP2BUS(vf_dip);
	dev_info_t		*pf_dip = NULL;
	pcie_bus_t		*pf_bus_p = NULL;
	int			vf_idx = PCIV_INVAL_VF;
	ddi_acc_handle_t	config_handle;
	int			first_vf_offset, vf_stride, value;
	int			vf_bdf, pf_bdf;

	vf_bdf = vf_bus_p->bus_bdf;
	pf_dip = vf_bus_p->bus_pf_dip;
	if (pf_dip == NULL)
		return (PCIV_INVAL_VF);
	pf_bus_p = PCIE_DIP2BUS(pf_dip);
	pf_bdf = pf_bus_p->bus_bdf;
	config_handle = pf_bus_p->bus_cfg_hdl;
	value = PCI_XCAP_GET16(config_handle, NULL,
	    pf_bus_p->sriov_cap_ptr,
	    PCIE_EXT_CAP_SRIOV_VF_OFFSET_OFFSET);
	if ((value == PCI_CAP_EINVAL32) || (value == 0))
		return (PCIV_INVAL_VF);
	first_vf_offset = (uint16_t)value;
	value = PCI_XCAP_GET16(config_handle, NULL,
	    pf_bus_p->sriov_cap_ptr,
	    PCIE_EXT_CAP_SRIOV_VF_STRIDE_OFFSET);
	if (value == PCI_CAP_EINVAL32)
		return (PCIV_INVAL_VF);
	vf_stride = (uint16_t)value;
	vf_idx =  ((vf_bdf - (pf_bdf + first_vf_offset)) / vf_stride) + 1;

	if ((vf_idx > 0) && (vf_idx <= pf_bus_p->num_vf))
		return (vf_idx);

	return (PCIV_INVAL_VF);
}

/*
 * Send a buffer to a target domain by specified a domain id.
 * If buf_cb is NOT NULL, that means that caller can not be
 * blocked, buf_cb will handle the buf according to the
 * transmission reulst. If buf_cb is NULL, that means caller
 * will be blocked until the transmission completes.
 */
int
pciv_domain_send(dev_info_t *dip, dom_id_t dst_domain,
    pciv_pkt_type_t pkt_type, caddr_t buf, size_t nbyte,
    buf_cb_t buf_cb, caddr_t cb_arg)
{
	pciv_tx_req_t	req;

	/* Validate inputs */
	if ((dip == NULL) || (dst_domain == 0) || (buf == NULL) ||
	    (nbyte == 0) || (nbyte > PCIV_MAX_BUF_SIZE))
		return (DDI_EINVAL);
	if (servicing_interrupt()) {
		/* Interrupt context must use buf_cb */
		if (buf_cb == NULL)
			return (DDI_EINVAL);
	}

	req.rc_addr = pciv_get_rc_addr(dip);
	req.src_addr = pciv_get_dev_addr(dip);
	req.dst_domain = dst_domain;
	req.dst_addr = req.src_addr;
	req.pkt_type = pkt_type;
	req.buf = buf;
	req.nbyte = nbyte;
	req.buf_cb = buf_cb;
	req.cb_arg = cb_arg;

	return (pciv_generic_send(dip, &req));
}

/*
 * Invoke PF/VF driver callback which is registered by ddi_cb_register.
 * This function only can be used when PF and VF are in the same Domain.
 */
static int
pciv_pfvf_local_cb_callout(pciv_local_pkt_t *pkt, pciv_event_type_t type)
{
	pciv_pvp_req_t		*req = &pkt->req;
	uint32_t		src_func;
	pciv_recv_event_t	*event;
	int			rv = DDI_SUCCESS;

	src_func = (req->pvp_dstfunc == PCIV_PF) ?
	    pciv_get_vf_idx(pkt->src_dip) : PCIV_PF;
	if (src_func == (uint32_t)PCIV_INVAL_VF)
		return (DDI_EINVAL);

	event = kmem_zalloc(sizeof (pciv_recv_event_t), KM_SLEEP);

	/*
	 * Populate pciv_recv_event_t, only PCIV_EVT_DRV_DATA needs
	 * populate the buf address.
	 */
	event->type = type;
	event->src_func = src_func;
	event->src_domain = 0;
	if (type == PCIV_EVT_DRV_DATA) {
		event->buf = req->pvp_buf;
		event->nbyte = req->pvp_nbyte;
	} else {
		event->buf = NULL;
		event->nbyte = 0;
	}

	rv = pcie_cb_execute(pkt->dst_dip, DDI_CB_COMM_RECV, (void *)event);

	kmem_free(event, sizeof (pciv_recv_event_t));

	return (rv == DDI_EINVAL ? DDI_ETRANSPORT : rv);
}

static void
pciv_ctrl_msg_cb(int rc, caddr_t buf, size_t size, caddr_t cb_arg)
{
	pciv_ctrl_op_t	*ctrl_op = (pciv_ctrl_op_t *)(uintptr_t)buf;
	dev_info_t	*dip = (dev_info_t *)(uintptr_t)cb_arg;

	/* Status code is only valid when rc is DDI_SUCCESS */
	if (rc == DDI_SUCCESS)
		PCIE_DBG("control message notify failed %s%d: status=%d\n",
		    ddi_driver_name(dip), ddi_get_instance(dip),
		    ctrl_op->status);
	else
		PCIE_DBG("control message notify failed %s%d: rc=%d\n",
		    ddi_driver_name(dip), ddi_get_instance(dip), rc);

	kmem_free(buf, size);
}

/*
 * Send out the control packet.
 */
static int
pciv_ctrl_msg_send(dev_info_t *dip, uint16_t cmd)
{
	pcie_bus_t		*bus_p = PCIE_DIP2BUS(dip);
	pciv_event_type_t	type;
	pciv_tx_req_t		*tx_req;
	pciv_local_pkt_t	*pkt;
	pciv_ctrl_op_t		*ctrl_op;
	int			rv;

	switch (cmd) {
	case PCIV_CTRL_NOTIFY_READY:
		type = PCIV_EVT_READY;
		break;
	case PCIV_CTRL_NOTIFY_NOT_READY:
		type = PCIV_EVT_NOT_READY;
		break;
	default:
		return (DDI_EINVAL);
	}

	if (pcie_is_physical_fabric(dip)) {
		if (!PCIE_IS_VF(bus_p)) {
			/*
			 * Return success if PF or regular dip are
			 * owned by a physical fabric. Since PF driver
			 * is always ready before VF driver attach.
			 * And a regular device driver owned by a physical
			 * fabric is always available before its peer
			 * driver is ready on a another Domain.
			 */
			return (DDI_SUCCESS);
		} else {
			/*
			 * VF and PF are in the same PCIE Fabric, invoke
			 * the driver callback directly
			 */
			pkt = kmem_zalloc(sizeof (pciv_local_pkt_t), KM_SLEEP);
			pkt->src_dip = dip;
			pkt->dst_dip = PCIE_GET_PF_DIP(bus_p);
			pkt->req.pvp_dstfunc = PCIV_PF;
			pkt->req.pvp_buf = NULL;
			pkt->req.pvp_nbyte = 0;
			pkt->req.pvp_cb = NULL;
			pkt->req.pvp_cb_arg = NULL;
			pkt->req.pvp_flag = PCIV_WAIT;

			rv = pciv_pfvf_local_cb_callout(pkt, type);

			kmem_free(pkt, sizeof (pciv_local_pkt_t));

			return (rv);
		}
	}

	/* Send the notification cross the Domains */
	ctrl_op = kmem_zalloc(sizeof (pciv_ctrl_op_t), KM_SLEEP);
	ctrl_op->cmd = cmd;

	tx_req = kmem_alloc(sizeof (pciv_tx_req_t), KM_SLEEP);
	tx_req->rc_addr = pciv_get_rc_addr(dip);
	tx_req->src_addr = pciv_get_dev_addr(dip);
	tx_req->dst_domain = pcie_get_domain_id(dip);
	tx_req->dst_addr = PCIE_IS_VF(bus_p) ? PCIV_PF : tx_req->src_addr;
	tx_req->pkt_type = PCIV_PKT_CTRL;
	tx_req->buf = (caddr_t)ctrl_op;
	tx_req->nbyte = sizeof (pciv_ctrl_op_t);
	tx_req->buf_cb = (buf_cb_t)pciv_ctrl_msg_cb;
	tx_req->cb_arg = (caddr_t)dip;

	if ((rv = pciv_generic_send(dip, tx_req)) != DDI_SUCCESS) {
		PCIE_DBG("control message notify failed %s%d: rv %d\n",
		    ddi_driver_name(dip), ddi_get_instance(dip), rv);
		kmem_free(ctrl_op, sizeof (pciv_ctrl_op_t));
	}

	kmem_free(tx_req, sizeof (pciv_tx_req_t));

	return (rv);
}
/*
 * Called from ddi_cb_register
 */
int
pciv_register_notify(dev_info_t *dip)
{
	return (pciv_ctrl_msg_send(dip, PCIV_CTRL_NOTIFY_READY));
}

/*
 * Called from ddi_cb_unregister
 */
int
pciv_unregister_notify(dev_info_t *dip)
{
	return (pciv_ctrl_msg_send(dip, PCIV_CTRL_NOTIFY_NOT_READY));
}

/*
 * PF and VF driver are in different Domains.
 * In this case, the packet will be sent out via the virtual wire.
 */
static int
pciv_pfvf_remote_send(dev_info_t *dip, dev_info_t *dst_dip,
    dom_id_t dst_domain, pciv_pvp_req_t *pvp_req)
{
	pcie_bus_t	*bus_p = NULL;
	pciv_tx_req_t	tx_req;

	ASSERT(dst_domain > 0);

	/*
	 * Use reserved special address PCIV_PF as the PF address,
	 * but VF ddress must be uniq dev address cross the IO domains.
	 */
	if (pvp_req->pvp_dstfunc == PCIV_PF) {
		/*
		 * Request dip is VF, PF and VF are in diffrent Domain.
		 * Current IO domain can not verify whether a given dip
		 * is a VF, this work will be done by the Domain who owns
		 * a PF device.
		 */
		ASSERT(dst_dip == NULL);

		tx_req.dst_addr = (uint32_t)PCIV_PF;
		tx_req.rc_addr = pciv_get_rc_addr(dip);
		tx_req.src_addr = pciv_get_dev_addr(dip);
	} else {
		/*
		 * Request dip is PF, has been verified in pciv_send.
		 * Dst dip must be a VF, validate it here.
		 */
		ASSERT(dst_dip != NULL);
		bus_p = PCIE_DIP2BUS(dst_dip);
		if (!PCIE_IS_VF(bus_p))
			return (DDI_EINVAL);

		tx_req.dst_addr = pciv_get_dev_addr(dst_dip);
		tx_req.rc_addr = pciv_get_rc_addr(dst_dip);
		tx_req.src_addr = (uint32_t)PCIV_PF;
	}

	tx_req.dst_domain = dst_domain;
	tx_req.pkt_type = PCIV_PKT_DRV;
	tx_req.buf = pvp_req->pvp_buf;
	tx_req.nbyte = pvp_req->pvp_nbyte;
	tx_req.buf_cb = pvp_req->pvp_cb;
	tx_req.cb_arg = pvp_req->pvp_cb_arg;

	return (pciv_generic_send(dip, &tx_req));
}

/*
 * Function invoked by taskq thread.
 */
static void
pciv_pfvf_local_pkt_post(void *arg)
{
	pciv_local_pkt_t	*pkt = (pciv_local_pkt_t *)arg;
	pciv_pvp_req_t		*req = &pkt->req;
	int			rv = DDI_SUCCESS;

	ASSERT(req->pvp_buf != NULL);
	ASSERT(req->pvp_nbyte > 0);

	/* Invoke driver callback directly */
	rv = pciv_pfvf_local_cb_callout(pkt, PCIV_EVT_DRV_DATA);

	/*
	 * DDI users buffer callback will process buffer according
	 * to return value.
	 */
	req->pvp_cb(rv, (caddr_t)req->pvp_buf, req->pvp_nbyte, req->pvp_cb_arg);

	/* Free the pkt in callback thread */
	kmem_free(pkt, sizeof (pciv_local_pkt_t));
}

/*
 * PF and VF driver are in same Domain.
 * In this case, there is no need put the packet on the virtual wire.
 * We could find the target dip, and invoke the driver callback directly.
 */
static int
pciv_pfvf_local_send(dev_info_t *dip, dev_info_t *dst_dip, pciv_pvp_req_t *req)
{
	pcie_bus_t		*bus_p = PCIE_DIP2BUS(dst_dip);
	pciv_local_pkt_t	*pkt = NULL;
	pciv_tx_taskq_t		*taskq;
	int			rv;

	ASSERT(dst_dip != NULL);

	/* Request dip has been verified in pciv_send */
	if (req->pvp_dstfunc == PCIV_PF) {
		/* Dst dip must be a PF */
		if (!PCIE_IS_PF(bus_p))
			return (DDI_EINVAL);
	} else {
		/* Dst dip must be a VF */
		if (!PCIE_IS_VF(bus_p))
			return (DDI_EINVAL);
	}

	/* Packet will be freed in callback later */
	pkt = kmem_zalloc(sizeof (pciv_local_pkt_t),
	    (req->pvp_flag == PCIV_WAIT) ? KM_SLEEP : KM_NOSLEEP);

	/* It's possible return NULL when PCIV_NOWAIT set */
	if (pkt == NULL)
		return (DDI_ENOMEM);

	pkt->src_dip = dip;
	pkt->dst_dip = dst_dip;
	bcopy(req, &pkt->req, sizeof (pciv_pvp_req_t));

	if (req->pvp_flag == PCIV_WAIT) {
		/* Invoke the driver callback */
		rv = pciv_pfvf_local_cb_callout(pkt, PCIV_EVT_DRV_DATA);

		/* Free the pkt in current thread */
		kmem_free(pkt, sizeof (pciv_local_pkt_t));
	} else {

		/* Use loopback taskq for local send */
		taskq = pciv_get_tx_taskq(dip);
		if (taskq == NULL || taskq->lb_taskq == NULL)
			return (DDI_ENOTSUP);

		/*
		 * Taskq dispatch can not sleep since the PCIV_NOWAIT
		 * is used.
		 */
		if ((ddi_taskq_dispatch(taskq->lb_taskq,
		    pciv_pfvf_local_pkt_post,
		    (void *)pkt,
		    DDI_NOSLEEP)) != DDI_SUCCESS) {
			cmn_err(CE_NOTE, "pciv start taskq"
			"for handle tx failed. \n");
			return (DDI_ENOMEM);
		}
	}

	return (rv);
}

/*
 * Public DDI interface. It used by PF/VF driver to communicate
 * with each other at the same Domain or in different Domains.
 * When PCIV_NOWAIT set in pciv_pvp_req_t, this DDI could work
 * under interrupt context under LOCK_LEVEL.
 */
int
pciv_send(dev_info_t *dip, pciv_pvp_req_t *req)
{
	dev_info_t	*dst_dip = NULL;
	pcie_bus_t	*bus_p;
	dom_id_t	domain_id;
	boolean_t	remote = B_FALSE;

	/* Validate input parameters */
	if ((dip == NULL) || (req == NULL) || (req->pvp_buf == NULL) ||
	    (req->pvp_nbyte == 0) || (req->pvp_nbyte > PCIV_MAX_BUF_SIZE))
		return (DDI_EINVAL);
	bus_p = PCIE_DIP2BUS(dip);

	if (req->pvp_flag == PCIV_WAIT) {
		/*
		 * Sanity check for the context, PCIV_WAIT
		 * doesn't work under interrpt context
		 */
		if (servicing_interrupt())
			return (DDI_EINVAL);

		/* Callback only works with PCIV_NOWAIT */
		if ((req->pvp_cb != NULL) || (req->pvp_cb_arg != NULL))
			return (DDI_EINVAL);
	} else if (req->pvp_flag == PCIV_NOWAIT) {
		/* PCIV_NOWAIT must use callback */
		if (req->pvp_cb == NULL)
			return (DDI_EINVAL);

		if (servicing_interrupt()) {
			/* Don't support high PIL access */
			if (getpil() > LOCK_LEVEL)
				return (DDI_EINVAL);
		}
	} else {
		/* Other value of pvp_flag is not allowed */
		return (DDI_EINVAL);
	}

	if (req->pvp_dstfunc == PCIV_PF) {
		/*
		 * Request dip is VF. A NULL dst dip means that the dst dip
		 * is NOT in local Domain. Need set remote flag when dst
		 * dip is in remote domain.
		 */
		if ((dst_dip = PCIE_GET_PF_DIP(bus_p)) == NULL) {
			/* The VF dip must have a non-zero Domain ID */
			domain_id = pcie_get_domain_id(dip);
			if (domain_id == 0)
				return (DDI_EINVAL);
			/*
			 * VF validation will be delayed, since IO Domain
			 * dones't have the enough knowledge.
			 */
			remote = B_TRUE;
		} else {
			/*
			 * Validate VF here, as PF and VF are in same Domain
			 * We don't care domain_id in a local send.
			 */
			if (!PCIE_IS_VF(bus_p))
				return (DDI_EINVAL);
			remote = B_FALSE;
		}
	} else {
		/* Request dip is PF, Validate it here */
		if (!PCIE_IS_PF(bus_p))
			return (DDI_EINVAL);
		/*
		 * Giving a PF dip can always get a VF dip. If we can not get
		 * the VF dip, the pvp_dstfunc must be an invalid value.
		 */
		if ((dst_dip = pciv_get_vf_dip(dip, req->pvp_dstfunc)) == NULL)
			return (DDI_EINVAL);

		/*
		 * A non-zero domain id means that device has been assigned to
		 * another Domain, the remote flag should be set.
		 */
		domain_id = pcie_get_domain_id(dst_dip);
		remote = domain_id > 0 ? B_TRUE : B_FALSE;
	}

	return (PCIV_SEND(dip, dst_dip, domain_id, req, remote));
}
