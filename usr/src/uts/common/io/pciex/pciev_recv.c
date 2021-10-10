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

#include <sys/sunndi.h>
#include <sys/pcie.h>
#include <sys/pci_impl.h>
#include <sys/pcie_impl.h>
#include <sys/pciev_impl.h>
#include <sys/modhash.h>

/* Global variables used by framework */
mod_hash_t	*i_pciv_proxy_hash;
uint_t		i_pciv_proxy_count;
krwlock_t	i_pciv_proxy_lock;
krwlock_t	i_pciv_send_lock;

static uint16_t		pciv_proxy_hashsz = 32;

static void pciv_ctrl_pkt_rx(void *arg);
static void pciv_drv_pkt_rx(void *arg);

/* Pkt handle function for each pkt type. */
static void (*i_pciv_rx_proc[PCIV_PKT_TYPE_MAX])(void *) = {
	pciv_ctrl_pkt_rx, 	/* for PCIV_PKT_CTRL */
	pciv_drv_pkt_rx,	/* for PCIV_PKT_DRV */
	pciv_drv_pkt_rx 	/* for PCIV_PKT_FABERR */
};

#define	PCIV_VERSION_NR	1
static pciv_version_t i_pciv_ver_map[PCIV_VERSION_NR] = {
	/* Proxy version 1.7 maps to PCIv version 1.0 */
	{0x1, 0x7, 0x1, 0x0}
};

static pciv_version_t *
pciv_ver_match(uint16_t proxy_major, uint16_t proxy_minor)
{
	for (int i = 0; i < PCIV_VERSION_NR; i++) {
		if ((i_pciv_ver_map[i].proxy_major == proxy_major) &&
		    (i_pciv_ver_map[i].proxy_minor == proxy_minor))
			return (&i_pciv_ver_map[i]);
	}

	return (NULL);
}

/*
 * SPARC and x86 platform have different device path
 * format for root complex device
 */
#if defined(__i386) || defined(__amd64)
#define	RC_PATH "/pci@%x,0"
#else
#define	RC_PATH "/pci@%x"
#endif

/*
 * Rx taskqs create and destroy
 */
static int
pciv_rx_taskq_create(pciv_proxy_t *proxy)
{
	char taskq_name[MAXTASKQNAME];

	for (int i = 0; i < PCIV_PKT_TYPE_MAX; i ++) {
		/*
		 * The rx taskq threads are created for dealing with the
		 * per-ring based packets receiving
		 */
		(void) snprintf(taskq_name, sizeof (taskq_name),
		    "pciv_rx_taskq_%"PRIu64"_%d", proxy->domid, i);
		if ((proxy->rx_taskq[i] =
		    ddi_taskq_create(NULL, taskq_name,
		    1, TASKQ_DEFAULTPRI, 0)) == NULL) {
			cmn_err(CE_NOTE, "pciv: rx ddi_taskq_create failed");
			return (DDI_FAILURE);
		}

	}

	return (DDI_SUCCESS);
}

static void
pciv_rx_taskq_destroy(pciv_proxy_t *proxy)
{
	for (int i = 0; i < PCIV_PKT_TYPE_MAX; i ++) {
		if (proxy->rx_taskq[i] != NULL)
			ddi_taskq_destroy(proxy->rx_taskq[i]);
	}
}

/*
 * Initialize the locks and hash tables maintained by the framework.
 */
void
pciv_proxy_init()
{
	i_pciv_proxy_count = 0;

	rw_init(&i_pciv_send_lock, NULL, RW_DEFAULT, NULL);
	rw_init(&i_pciv_proxy_lock, NULL, RW_DEFAULT, NULL);
	i_pciv_proxy_hash = mod_hash_create_idhash("pciv_proxy_hash",
	    pciv_proxy_hashsz, mod_hash_null_valdtor);
}

/*
 * Destroy locks and hash tables.
 */
void
pciv_proxy_fini()
{
	mod_hash_destroy_hash(i_pciv_proxy_hash);
	rw_destroy(&i_pciv_proxy_lock);
	rw_destroy(&i_pciv_send_lock);

	i_pciv_proxy_count = 0;
}

/*
 * Register the proxy driver instance in the pcie framework.
 * One instance represents an IO Domain who can be communicated
 * with. The pcie framework could select the a proper proxy
 * driver instance for packets transmissions.
 */
int
pciv_proxy_register(pciv_proxy_reg_t *regp, pciv_handle_t *h)
{
	pciv_proxy_t	*proxy;
	int		err;

	proxy = kmem_alloc(sizeof (pciv_proxy_t), KM_SLEEP);

	proxy->dip = regp->dip;
	proxy->state = regp->state;
	proxy->domid = regp->domid;
	proxy->check = regp->check;
	proxy->tx = regp->tx;

	err = pciv_rx_taskq_create(proxy);
	if (err != DDI_SUCCESS)
		goto fail;

	rw_enter(&i_pciv_send_lock, RW_WRITER);
	rw_enter(&i_pciv_proxy_lock, RW_WRITER);

	/*
	 * Hold the dip associated to the pciv proxy to prevent it from
	 * being detached.
	 */
	e_ddi_hold_devi(proxy->dip);

	if (mod_hash_insert(i_pciv_proxy_hash,
	    (mod_hash_key_t)(uintptr_t)proxy->domid,
	    (mod_hash_val_t)(uintptr_t)proxy) != 0) {
		err = DDI_FAILURE;
		goto fail_tx;
	}

	i_pciv_proxy_count++;

	rw_exit(&i_pciv_proxy_lock);
	rw_exit(&i_pciv_send_lock);

	*h = (pciv_handle_t)proxy;
	return (DDI_SUCCESS);

fail_tx:
	ddi_release_devi(proxy->dip);
	pciv_rx_taskq_destroy(proxy);

	rw_exit(&i_pciv_proxy_lock);
	rw_exit(&i_pciv_send_lock);
fail:

	kmem_free(proxy, sizeof (pciv_proxy_t));
	return (err);
}

/*
 * Unregister the proxy driver
 */
void
pciv_proxy_unregister(pciv_handle_t h)
{
	pciv_proxy_t	*proxy = (pciv_proxy_t *)h;
	mod_hash_val_t	val;

	rw_enter(&i_pciv_send_lock, RW_WRITER);
	rw_enter(&i_pciv_proxy_lock, RW_WRITER);
	(void) mod_hash_remove(i_pciv_proxy_hash,
	    (mod_hash_key_t)(uintptr_t)proxy->domid, &val);

	ASSERT(proxy == (pciv_proxy_t *)val);
	ASSERT(i_pciv_proxy_count > 0);
	i_pciv_proxy_count--;

	ddi_release_devi(proxy->dip);

	rw_exit(&i_pciv_proxy_lock);
	rw_exit(&i_pciv_send_lock);

	pciv_rx_taskq_destroy(proxy);

	kmem_free(proxy, sizeof (pciv_proxy_t));
}

int
pciv_set_version(pciv_handle_t h, uint16_t proxy_major, uint16_t proxy_minor)
{
	pciv_proxy_t 	*proxy = (pciv_proxy_t *)h;

	ASSERT(proxy != NULL);

	proxy->version = pciv_ver_match(proxy_major, proxy_minor);

	return (proxy->version != NULL ? DDI_SUCCESS : DDI_FAILURE);
}

void
pciv_unset_version(pciv_handle_t h)
{
	pciv_proxy_t 	*proxy = (pciv_proxy_t *)h;

	ASSERT(proxy != NULL);

	proxy->version = NULL;
}

/*
 * Find the dip by decoding the dev_addr
 */
static dev_info_t *
pciv_get_dip_by_dev_addr(uint32_t rc_addr, uint32_t dev_addr)
{
	dev_info_t	*pdip;
	char		devpath[MAXNAMELEN];

	(void) snprintf(devpath, sizeof (devpath), RC_PATH, rc_addr);

	pdip = pcie_find_dip_by_unit_addr(devpath);
	ASSERT(pdip != NULL);

	return (pcie_find_dip_by_bdf(pdip, (pcie_req_id_t)dev_addr));
}

/*
 * Find the destination dip which can receive packet.
 */
static dev_info_t *
pciv_get_dst_dip(pciv_pkt_t *pkt)
{
	dev_info_t	*shadow_dip = NULL;
	pcie_bus_t	*bus_p = NULL;
	int		src_func;
	dev_info_t	*dst_dip = NULL;

	/* Validate the packet address */
	if (pkt->hdr.dst_addr == (uint32_t)PCIV_PF) {
		/*
		 * Source dip must be a VF, get its shadow dip in
		 * local domain.
		 */
		shadow_dip = pciv_get_dip_by_dev_addr(pkt->hdr.rc_addr,
		    pkt->hdr.src_addr);
		if (shadow_dip == NULL)
			return (NULL);

		/* Src and dst dip must be in the same Domain */
		ASSERT(pcie_is_physical_fabric(shadow_dip));

		bus_p = PCIE_DIP2BUS(shadow_dip);
		if (bus_p && !PCIE_IS_VF(bus_p))
			return (NULL);

		/*
		 * If can not get right VF index, return NULL. Otherwise
		 * translate src_addr to a right VF index.
		 */
		src_func = pciv_get_vf_idx(shadow_dip);
		if (src_func == PCIV_INVAL_VF)
			return (NULL);
		pkt->hdr.src_addr = src_func;

		dst_dip = PCIE_GET_PF_DIP(bus_p);

	} else if (pkt->hdr.dst_addr != pkt->hdr.src_addr) {
		/* Source dip must be a PF */
		if (pkt->hdr.src_addr != (uint32_t)PCIV_PF)
			return (NULL);

		dst_dip = pciv_get_dip_by_dev_addr(pkt->hdr.rc_addr,
		    pkt->hdr.dst_addr);

		/* Dst dip must be a VF on a virtual fabric */
		ASSERT(!pcie_is_physical_fabric(dst_dip));

	} if (pkt->hdr.dst_addr == pkt->hdr.src_addr) {
		/*
		 * Regular function, dst and src are same but they are
		 * located in different Domains.
		 */
		dst_dip = pciv_get_dip_by_dev_addr(pkt->hdr.rc_addr,
		    pkt->hdr.dst_addr);
		if (dst_dip == NULL)
			return (NULL);

		if (pcie_is_physical_fabric(dst_dip)) {
			bus_p = PCIE_DIP2BUS(dst_dip);
			if (bus_p && !(PCIE_IS_REGULAR(bus_p)))
				return (NULL);
		}
	}

	return (dst_dip);
}

/*
 * Mark the pkt to be already handled and ack it back.
 */
static void
pciv_pkt_ack(pciv_pkt_t *pkt, int rv)
{
	switch (rv) {
	case DDI_SUCCESS:
	case DDI_ENOTSUP:
	case DDI_ENOMEM:
		break;
	case DDI_EINVAL:
	defult:
		rv = DDI_ETRANSPORT;
	}

	if (rv) {
		pkt->io_flag |= PCIV_ERROR;
		pkt->io_err = rv;
	}

	pkt->io_flag &= ~PCIV_BUSY;
	pkt->io_flag |= PCIV_DONE;

	pkt->io_cb(pkt->cb_arg, pkt);
}

/*
 * Invoke the callback registered by ddi_cb_register.
 */
static int
pciv_cb_callout(pciv_pkt_t *pkt, pciv_event_type_t type)
{
	dev_info_t		*dst_dip = NULL;
	pciv_recv_event_t	*event = NULL;
	int			rv;

	if ((dst_dip = pciv_get_dst_dip(pkt)) == NULL)
		return (DDI_EINVAL);

	event = kmem_zalloc(sizeof (pciv_recv_event_t), KM_SLEEP);

	event->type = type;
	event->src_func = pkt->hdr.src_addr;
	event->src_domain = (dom_id_t)pkt->src_domain;

	if (pkt->hdr.type == PCIV_PKT_CTRL) {
		/* CTRL packet will not expose buffers */
		event->buf = NULL;
		event->nbyte = 0;
	} else {
		event->buf = pkt->buf;
		event->nbyte = pkt->hdr.size;
	}

	rv = pcie_cb_execute(dst_dip, DDI_CB_COMM_RECV, (void *)event);

	kmem_free(event, sizeof (pciv_recv_event_t));

	return (rv);
}

/*
 * Process packet chain which needs to set err code and invoke
 * packet io callback, so that underlying virtual proxy driver
 * can notify the transmitting side.
 */
static void
pciv_pkt_chain_ack(pciv_pkt_t *pkt_chain, int rv)
{
	pciv_pkt_t	*pkt, *tmp;

	pkt = pkt_chain;
	while (pkt != NULL) {
		/*
		 * Set err code and notify underlying layer the
		 * IO is done.
		 */
		pciv_pkt_ack(pkt, rv);

		tmp = pkt;

		pkt = pkt->next;

		pciv_pkt_free(tmp);
	}
}

/*
 * Process packet chain which contains control packet.
 */
static void
pciv_ctrl_pkt_rx(void *arg)
{
	pciv_pkt_t		*pkt_chain = (pciv_pkt_t *)arg;
	pciv_pkt_t		*pkt, *tmp;
	pciv_ctrl_op_t		*ctrl_op;
	int			rv = 0;

	pkt = pkt_chain;
	while (pkt != NULL) {
		ctrl_op = (pciv_ctrl_op_t *)pkt->buf;

		/*
		 * check pkt type and payload to prevent invalid pkt from
		 * peer domain.
		 */
		if ((pkt->hdr.type != PCIV_PKT_CTRL) || (ctrl_op == NULL)) {
			rv = DDI_EINVAL;
			goto ack_pkt;
		}

		/* Pass pkt to receive callback */
		switch (ctrl_op->cmd) {
		case PCIV_CTRL_NOTIFY_READY:
			rv = pciv_cb_callout(pkt, PCIV_EVT_READY);
			break;
		case PCIV_CTRL_NOTIFY_NOT_READY:
			rv = pciv_cb_callout(pkt, PCIV_EVT_NOT_READY);
			break;
		default:
			rv = DDI_EINVAL;
		}

		/*
		 * Driver callback only returns DDI_SUCCESS, DDI_ENOTSUP,
		 * and DDI_EINVAL, which are all aligned to ctrl_op->status
		 */
		ctrl_op->status = (uint16_t)rv;
ack_pkt:
		/* Notify underlying layer the IO is done */
		pciv_pkt_ack(pkt, rv);

		tmp = pkt;

		pkt = pkt->next;

		pciv_pkt_free(tmp);
	}
}

/*
 * Process packet chain for device driver who registered
 * call back by using ddi_cb_register.
 */
static void
pciv_drv_pkt_rx(void *arg)
{
	pciv_pkt_t	*pkt_chain = (pciv_pkt_t *)arg;
	pciv_pkt_t	*pkt, *tmp;
	int		rv;

	pkt = pkt_chain;
	while (pkt != NULL) {
		/*
		 * check pkt type and payload to prevent invalid pkt from
		 * peer domain.
		 */
		if ((pkt->hdr.type != PCIV_PKT_DRV) &&
		    (pkt->hdr.type != PCIV_PKT_FABERR)) {
			rv = DDI_EINVAL;
			goto ack_pkt;
		}

		/* Pass pkt to receive callback */
		rv = pciv_cb_callout(pkt, PCIV_EVT_DRV_DATA);
ack_pkt:
		pciv_pkt_ack(pkt, rv);

		tmp = pkt;

		pkt = pkt->next;

		pciv_pkt_free(tmp);
	}
}

/*
 * This function is the rx entry point called by virtual proxy
 * driver. When new packets sending from another IO domain are
 * received by virtual proxy driver, this function will be called
 * and a packet chain will be passed in.
 */
void
pciv_proxy_rx(pciv_handle_t h, pciv_pkt_t *pkt_chain)
{
	pciv_proxy_t	*proxy = (pciv_proxy_t *)h;
	int 		pkt_type;

	/*
	 * Since the protocol will ensure the packet type of
	 * all packets on the chain are same, we just get the
	 * packet type from first packet.
	 */
	pkt_type = pkt_chain->hdr.type;

	/*
	 * Check pkt type here as we cannot trust peer domain
	 * will always send valid pkt.
	 */
	if (pkt_type >= PCIV_PKT_TYPE_MAX) {
		pciv_pkt_chain_ack(pkt_chain, DDI_EINVAL);
		return;
	}

	/* Dispatch the pkt chain according to pkt type */
	if ((ddi_taskq_dispatch(proxy->rx_taskq[pkt_type],
	    i_pciv_rx_proc[pkt_type], pkt_chain,
	    DDI_NOSLEEP)) != DDI_SUCCESS) {
		cmn_err(CE_NOTE, "pciv start taskq"
		    "for handle pkt type %d failed.\n", pkt_type);
		pciv_pkt_chain_ack(pkt_chain, DDI_ENOMEM);
	}
}
