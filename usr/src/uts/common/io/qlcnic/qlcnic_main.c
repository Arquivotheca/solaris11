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
 * Copyright 2010 QLogic Corporation. All rights reserved.
 */

/*
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/debug.h>
#include <sys/stropts.h>
#include <sys/stream.h>
#include <sys/strlog.h>
#include <sys/kmem.h>
#include <sys/stat.h>
#include <sys/kstat.h>
#include <sys/vtrace.h>
#include <sys/dlpi.h>
#include <sys/strsun.h>
#include <sys/ethernet.h>
#include <sys/modctl.h>
#include <sys/errno.h>
#include <sys/dditypes.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/sysmacros.h>
#include <sys/pci.h>

#include <sys/gld.h>
#include <netinet/in.h>
#include <inet/ip.h>
#include <inet/tcp.h>

#include <sys/rwlock.h>
#include <sys/mutex.h>
#include <sys/pattr.h>
#include <sys/strsubr.h>
#include <sys/ddi_impldefs.h>
#include <sys/task.h>
#include <sys/netlb.h>
#include <netinet/udp.h>

#include "qlcnic_hw.h"
#include "qlcnic.h"

#include "qlcnic_phan_reg.h"
#include "qlcnic_ioctl.h"
#include "qlcnic_cmn.h"
#include "qlcnic_version.h"
#include "qlcnic_brdcfg.h"

#if defined(lint)
#undef MBLKL
#define	MBLKL(_mp_)	((uintptr_t)(_mp_)->b_wptr - (uintptr_t)(_mp_)->b_rptr)
#endif /* lint */

#define	VLAN_TAGSZ		0x4

#define	index2rxbuf(_rdp_, _idx_)	((_rdp_)->rx_buf_pool + (_idx_))
#define	rxbuf2index(_rdp_, _bufp_)	((_bufp_) - (_rdp_)->rx_buf_pool)

/*
 * Receive ISR processes QLCNIC_RX_MAXBUFS incoming packets at most, then posts
 * as many buffers as packets processed. This loop repeats as required to
 * process all incoming packets delivered in a single interrupt. Higher
 * value of QLCNIC_RX_MAXBUFS improves performance by posting rx buffers less
 * frequently, but at the cost of not posting quickly enough when card is
 * running out of rx buffers.
 */
#define	QLCNIC_RX_THRESHOLD		256
#define	QLCNIC_RX_MAXBUFS		128
#define	QLCNIC_MAX_TXCOMPS		256

extern int qlcnic_create_rxtx_rings(qlcnic_adapter *adapter);
extern void qlcnic_destroy_rxtx_rings(qlcnic_adapter *adapter);
extern int qlcnic_configure_rss(qlcnic_adapter *adapter);
extern int qlcnic_configure_ip_addr(struct qlcnic_tx_ring_s *, uint32_t ip);
extern int qlcnic_config_led_blink(qlcnic_adapter *adapter, u32 flag);
extern u32 qlcnic_issue_cmd(struct qlcnic_adapter_s *adapter,
    u32 pci_fn, u32 version, u32 arg1, u32 arg2, u32 arg3, u32 cmd);
extern int qlcnic_set_ilb_mode(qlcnic_adapter *adapter);

static void qlcnic_post_rx_buffers_nodb(struct qlcnic_adapter_s *adapter,
    uint32_t ringid);
static mblk_t *qlcnic_process_rcv(qlcnic_adapter *, uint64_t,
    struct qlcnic_sds_ring_s *);
static mblk_t *qlcnic_process_rcv_ring(struct qlcnic_sds_ring_s *,
    int nbytes, int npkts);
static void qlcnic_process_cmd_ring(struct qlcnic_tx_ring_s *tx_ring);

static int qlcnic_do_ioctl(qlcnic_adapter *adapter, queue_t *q, mblk_t *mp);
static void qlcnic_ioctl(struct qlcnic_adapter_s *adapter, int cmd, queue_t *q,
    mblk_t *mp);
static void qlcnic_force_fw_reset(qlcnic_adapter *);
static void qlcnic_create_loopback_buff(unsigned char *data);
int qlcnic_check_loopback_buff(unsigned char *data);

/* GLDv3 interface functions */
static int qlcnic_m_start(void *);
static void qlcnic_m_stop(void *);
static int qlcnic_m_multicst(void *, boolean_t, const uint8_t *);
static int qlcnic_m_promisc(void *, boolean_t);
static int qlcnic_m_stat(void *arg, uint_t stat, uint64_t *val);
static void qlcnic_m_ioctl(void *arg, queue_t *wq, mblk_t *mp);
static boolean_t qlcnic_m_getcapab(void *arg, mac_capab_t cap, void *cap_data);
static int qlcnic_m_setprop(void *, const char *, mac_prop_id_t, uint_t,
    const void *);
static int qlcnic_m_getprop(void *, const char *, mac_prop_id_t, uint_t,
    void *);
static void qlcnic_m_propinfo(void *, const char *, mac_prop_id_t,
    mac_prop_info_handle_t);
extern int qlcnic_p3p_chk_start_firmware(qlcnic_adapter *adapter);
extern int qlcnic_p3p_clear_dev_state(qlcnic_adapter *adapter, int flag);
extern int qlcnic_start_firmware(qlcnic_adapter *adapter);
extern int qlcnic_api_lock(struct qlcnic_adapter_s *adapter);
extern void qlcnic_api_unlock(struct qlcnic_adapter_s *adapter);
extern int qlcnic_dev_init(qlcnic_adapter *adapter);
static void qlcnic_return_dma_handle(struct qlcnic_tx_ring_s *tx_ring,
    qlcnic_dmah_node_t *head);
extern int qlcnic_drv_start(qlcnic_adapter *adapter);
extern int qlcnic_configure_intr_coalesce(qlcnic_adapter *adapter);
int qlcnic_check_health(qlcnic_adapter *adapter);
static mblk_t *qlcnic_ring_rx(qlcnic_sds_ring_t *sds_ring,
    int poll_bytes, int poll_pkts);
extern int secpolicy_net_config(const cred_t *, boolean_t);
char *qlcnic_priv_prop[] = {
	"_tx_copy_thresh",
	"_rx_copy_thresh",
	NULL
};

/*
 * Allocates DMA handle, virtual memory and binds them
 * returns size of actual memory bound and the physical address.
 */
int
qlcnic_pci_alloc_consistent(qlcnic_adapter *adapter,
    int size, caddr_t *address, ddi_dma_cookie_t *cookie,
    ddi_dma_handle_t *dma_handle, ddi_acc_handle_t *handlep)
{
	int err;
	uint32_t ncookies;
	size_t ring_len;
	uint_t dma_flags = DDI_DMA_RDWR | DDI_DMA_CONSISTENT;

	*dma_handle = NULL;

	if (size <= 0)
		return (DDI_ENOMEM);

	err = ddi_dma_alloc_handle(adapter->dip,
	    &adapter->gc_dma_attr_desc,
	    DDI_DMA_DONTWAIT, NULL, dma_handle);
	if (err != DDI_SUCCESS) {
		cmn_err(CE_WARN, "!qlcnic(%d): pci_alloc_consistent: "
		    "ddi_dma_alloc_handle FAILED: %d", adapter->instance, err);
		*dma_handle = NULL;
		return (DDI_ENOMEM);
	}

	err = ddi_dma_mem_alloc(*dma_handle,
	    size, &adapter->gc_attr_desc,
	    dma_flags & (DDI_DMA_STREAMING | DDI_DMA_CONSISTENT),
	    DDI_DMA_DONTWAIT, NULL, address, &ring_len,
	    handlep);
	if (err != DDI_SUCCESS) {
		cmn_err(CE_WARN, "!qlcnic(%d): pci_alloc_consistent: "
		    "ddi_dma_mem_alloc FAILED: %d, request size: %d",
		    adapter->instance, err, size);
		ddi_dma_free_handle(dma_handle);
		*dma_handle = NULL;
		*handlep = NULL;
		return (DDI_ENOMEM);
	}

	if (ring_len < size) {
		cmn_err(CE_WARN, "!qlcnic(%d): pci_alloc_consistent: "
		    "could not allocate required: %d, request size: %d",
		    adapter->instance, err, size);
		ddi_dma_mem_free(handlep);
		ddi_dma_free_handle(dma_handle);
		*dma_handle = NULL;
		*handlep = NULL;
		return (DDI_FAILURE);
	}

	(void) memset(*address, 0, size);

	if (((err = ddi_dma_addr_bind_handle(*dma_handle,
	    NULL, *address, ring_len,
	    dma_flags,
	    DDI_DMA_DONTWAIT, NULL,
	    cookie, &ncookies)) != DDI_DMA_MAPPED) ||
	    (ncookies != 1)) {
		cmn_err(CE_WARN, "!qlcnic(%d): pci_alloc_consistent: "
		    "ddi_dma_addr_bind_handle Failed: %d",
		    adapter->instance, err);
		ddi_dma_mem_free(handlep);
		ddi_dma_free_handle(dma_handle);
		*dma_handle = NULL;
		*handlep = NULL;
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

/*
 * Unbinds the memory, frees the DMA handle and at the end, frees the memory
 */
void
qlcnic_pci_free_consistent(ddi_dma_handle_t *dma_handle,
    ddi_acc_handle_t *acc_handle)
{
	int err;

	if (*dma_handle != NULL) {
		err = ddi_dma_unbind_handle(*dma_handle);
		if (err != DDI_SUCCESS) {
			cmn_err(CE_WARN, "pci_free_consistent: "
			    "Error unbinding memory, err %d", err);
			return;
		}
	} else {
		goto exit;
	}
	ddi_dma_mem_free(acc_handle);
	ddi_dma_free_handle(dma_handle);
exit:
	*dma_handle = NULL;
	*acc_handle = NULL;
}

static uint32_t msi_tgt_status[] = {
    ISR_INT_TARGET_STATUS, ISR_INT_TARGET_STATUS_F1,
    ISR_INT_TARGET_STATUS_F2, ISR_INT_TARGET_STATUS_F3,
    ISR_INT_TARGET_STATUS_F4, ISR_INT_TARGET_STATUS_F5,
    ISR_INT_TARGET_STATUS_F6, ISR_INT_TARGET_STATUS_F7
};

static void
qlcnic_disable_int(struct qlcnic_sds_ring_s *sds_ring)
{
	/* adapter is neeeded by the MACRO */
	qlcnic_adapter *adapter = sds_ring->adapter;

	QLCNIC_PCI_WRITE_32(0, (unsigned long)sds_ring->interrupt_crb_addr);
	if (qlcnic_check_acc_handle(adapter,
	    adapter->regs_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(adapter->dip,
		    DDI_SERVICE_DEGRADED);
		qlcnic_force_fw_reset(adapter);
	}
}

static inline int
qlcnic_clear_int(qlcnic_adapter *adapter)
{
	uint32_t mask, temp, status;

	QLCNIC_READ_LOCK(&adapter->adapter_lock);

	/* check whether it's our interrupt */
	if (!QLCNIC_IS_MSI_FAMILY(adapter)) {

		/* Legacy Interrupt case */
		adapter->qlcnic_pci_read_immediate(adapter, ISR_INT_VECTOR,
		    &status);

		if (!(status & adapter->legacy_intr.int_vec_bit)) {
			QLCNIC_READ_UNLOCK(&adapter->adapter_lock);
			return (-1);
		}

		if (adapter->ahw.revision_id >= QLCNIC_P3_B1) {
			adapter->qlcnic_pci_read_immediate(adapter,
			    ISR_INT_STATE_REG, &temp);
			if (!ISR_IS_LEGACY_INTR_TRIGGERED(temp)) {
				QLCNIC_READ_UNLOCK(&adapter->adapter_lock);
				return (-1);
			}
		}
		/* claim interrupt */
		temp = 0xffffffff;
		adapter->qlcnic_pci_write_immediate(adapter,
		    adapter->legacy_intr.tgt_status_reg, &temp);

		adapter->qlcnic_pci_read_immediate(adapter, ISR_INT_VECTOR,
		    &mask);

		/*
		 * Read again to make sure the legacy interrupt message got
		 * flushed out
		 */
		adapter->qlcnic_pci_read_immediate(adapter, ISR_INT_VECTOR,
		    &mask);
	} else if (adapter->flags & QLCNIC_MSI_ENABLED) {
		/* clear interrupt */
		temp = 0xffffffff;
		adapter->qlcnic_pci_write_immediate(adapter,
		    msi_tgt_status[adapter->ahw.pci_func], &temp);
	}

	QLCNIC_READ_UNLOCK(&adapter->adapter_lock);

	return (0);
}

static void
qlcnic_enable_int(struct qlcnic_sds_ring_s *sds_ring)
{
	qlcnic_adapter *adapter = sds_ring->adapter;

	QLCNIC_PCI_WRITE_32(1, (unsigned long)sds_ring->interrupt_crb_addr);

	if (!QLCNIC_IS_MSI_FAMILY(sds_ring->adapter)) {
		uint32_t mask = 0xfbff;

		sds_ring->adapter->qlcnic_pci_write_immediate(sds_ring->adapter,
		    sds_ring->adapter->legacy_intr.tgt_mask_reg, &mask);
	}
	if (qlcnic_check_acc_handle(adapter,
	    adapter->regs_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(adapter->dip,
		    DDI_SERVICE_DEGRADED);
		qlcnic_force_fw_reset(adapter);
	}
}

static int
qlcnic_alloc_rx_freelist(
	qlcnic_adapter *adapter)
{
	int rc = 0;
	struct qlcnic_sds_ring_s *sds_ring;
	int i, ctx;
	qlcnic_rds_buf_recycle_list_t *rx_recycle_list;
	int free_list_size[2];
	free_list_size[0] = adapter->MaxRxDescCount *
	    sizeof (qlcnic_rx_buffer_t *);
	free_list_size[1] = adapter->MaxJumboRxDescCount *
	    sizeof (qlcnic_rx_buffer_t *);
	int rds_index;

	for (ctx = 0; ctx < MAX_RCV_CTX; ctx++) {
		for (i = 0; i < adapter->max_sds_rings; i++) {
			sds_ring = &adapter->recv_ctx[ctx].sds_ring[i];

			for (rds_index = 0; rds_index < NUM_RCV_DESC_RINGS;
			    rds_index++) {
				rx_recycle_list =
				    &sds_ring->rds_buf_recycle_list[rds_index];
				/* size free list size */
				rx_recycle_list->free_list_size =
				    free_list_size[rds_index];
				if ((rx_recycle_list->free_list =
				    kmem_zalloc(rx_recycle_list->free_list_size,
				    KM_SLEEP)) == NULL) {
					rc = 1;
					rx_recycle_list->free_list_size = 0;
					return (rc);
				}
				rx_recycle_list->max_free_entries =
				    rx_recycle_list->free_list_size /
				    sizeof (qlcnic_rx_buffer_t *);
				DPRINTF(DBG_RX, (CE_WARN,
				    "%s%d: rds ring %d max free entries %d\n",
				    adapter->name, adapter->instance, rds_index,
				    rx_recycle_list->max_free_entries));
			}
		}
	}

	return (rc);
}

static void
qlcnic_dealloc_rx_freelist(
	qlcnic_adapter *adapter)
{
	struct qlcnic_sds_ring_s *sds_ring;
	int i, ctx;
	int rds_index;
	qlcnic_rds_buf_recycle_list_t *rx_recycle_list;

	for (ctx = 0; ctx < MAX_RCV_CTX; ctx++) {
		for (i = 0; i < adapter->max_sds_rings; i++) {
			sds_ring = &adapter->recv_ctx[ctx].sds_ring[i];

			for (rds_index = 0; rds_index < NUM_RCV_DESC_RINGS;
			    rds_index++) {
				rx_recycle_list =
				    &sds_ring->rds_buf_recycle_list[rds_index];

				DPRINTF(DBG_RX, (CE_WARN,
				    "%s%d: Free list %p at index %d\n",
				    adapter->name, adapter->instance,
				    rx_recycle_list->free_list, rds_index));
				if (rx_recycle_list->free_list) {
					kmem_free(
					    rx_recycle_list->free_list,
					    rx_recycle_list->free_list_size);
					rx_recycle_list->free_list = NULL;
				}
			}
		}
	}
}

static void
qlcnic_free_hw_resources(qlcnic_adapter *adapter)
{
	qlcnic_recv_context_t *recv_ctx;
	qlcnic_rcv_desc_ctx_t *rcv_desc;
	struct qlcnic_sds_ring_s *sds_ring;
	struct qlcnic_tx_ring_s *tx_ring;
	int ctx, ring, i;
	qlcnic_rds_buf_recycle_list_t *rx_recycle_list;

	if (adapter->context_allocated == 1) {
		qlcnic_destroy_rxtx_ctx(adapter);
		adapter->context_allocated = 0;
	}

	if (adapter->ctxDesc != NULL) {
		qlcnic_pci_free_consistent(&adapter->ctxDesc_dma_handle,
		    &adapter->ctxDesc_acc_handle);
		adapter->ctxDesc = NULL;
	}

	/* free tx cmd descriptor ring */
	for (i = 0; i < adapter->max_tx_rings; i++) {
		tx_ring = &(adapter->tx_ring[i]);

		if (tx_ring->cmdDescHead != NULL) {
			qlcnic_pci_free_consistent(
			    &tx_ring->cmd_desc_dma_handle,
			    &tx_ring->cmd_desc_acc_handle);
			tx_ring->cmdDesc_physAddr = NULL;
			tx_ring->cmdDescHead = NULL;
		}
	}

	for (ctx = 0; ctx < MAX_RCV_CTX; ++ctx) {
		recv_ctx = &adapter->recv_ctx[ctx];
		for (ring = 0; ring < adapter->max_rds_rings; ring++) {
			rcv_desc = recv_ctx->rcv_desc[ring];

			if (rcv_desc->desc_head != NULL) {
				qlcnic_pci_free_consistent(
				    &rcv_desc->rx_desc_dma_handle,
				    &rcv_desc->rx_desc_acc_handle);
				rcv_desc->desc_head = NULL;
				rcv_desc->phys_addr = NULL;
			}
		}

		for (ring = 0; ring < adapter->max_sds_rings; ring++) {
			sds_ring = &recv_ctx->sds_ring[ring];

			if (sds_ring->rcvStatusDescHead != NULL) {
				qlcnic_pci_free_consistent(
				    &sds_ring->status_desc_dma_handle,
				    &sds_ring->status_desc_acc_handle);
				sds_ring->rcvStatusDesc_physAddr = NULL;
				sds_ring->rcvStatusDescHead = NULL;
				sds_ring->adapter = NULL;
				sds_ring->status_desc_dma_handle = NULL;
				sds_ring->status_desc_acc_handle = NULL;

				/* Free lock resources */
				mutex_destroy(&sds_ring->sds_lock);
				for (i = 0; i < adapter->max_rds_rings; i ++) {
					rx_recycle_list =
					    &sds_ring->rds_buf_recycle_list[i];
					mutex_destroy(&rx_recycle_list->lock);
				}
			}

			sds_ring->adapter = NULL;
		}

		qlcnic_dealloc_rx_freelist(adapter);
	}
}

void
cleanup_adapter(struct qlcnic_adapter_s *adapter)
{
	ddi_regs_map_free(&(adapter->regs_handle));
	kmem_free(adapter, sizeof (qlcnic_adapter));
}

void
qlcnic_remove(qlcnic_adapter *adapter, int wflag)
{
	uint32_t last_producer, last_consumer;
	int count = 0;
	struct qlcnic_cmd_buffer *buffer;
	qlcnic_dmah_node_t *dmah, *head = NULL, *tail = NULL;
	uint32_t free_hdls = 0;
	qlcnic_recv_context_t *recv_ctx = &(adapter->recv_ctx[0]);
	uint32_t ctx, ring;
	struct qlcnic_sds_ring_s *sds_ring;
	qlcnic_rcv_desc_ctx_t *rcv_desc;
	struct qlcnic_tx_ring_s *tx_ring;
	int i;
	qlcnic_rds_buf_recycle_list_t *rx_recycle_list;

	DPRINTF(DBG_GLD, (CE_NOTE, "qlcnic_remove(%d) entered, drv_state %x, ",
	    adapter->instance, adapter->drv_state));

	/*
	 * If we call this as part of firmware recovery, stop DMAs from
	 * the adapter
	 */
	if (!wflag) {
		uint16_t pci_cmd_word;

		pci_cmd_word = pci_config_get16(adapter->pci_cfg_handle,
		    PCI_CONF_COMM);
		pci_cmd_word &= ~(PCI_COMM_ME);
		pci_config_put16(adapter->pci_cfg_handle, PCI_CONF_COMM,
		    pci_cmd_word);
	}

	/* Wait for isr to complete */
	qlcnic_msleep(1000);

	/* Destroy fw rx/tx context */
	if (adapter->context_allocated == 1) {
		if (wflag) {
			DPRINTF(DBG_INIT, (CE_NOTE, "%s%d: Deleting rx/tx "
			"contexts ....\n", adapter->name, adapter->instance));
				qlcnic_destroy_rxtx_ctx(adapter);
			adapter->context_allocated = 0;
		}
	}

	qlcnic_msleep(1000);

	DPRINTF(DBG_INIT, (CE_NOTE, "%s%d Disable RX interrupts...\n",
	    adapter->name, adapter->instance));

	for (count = 0; count < adapter->max_sds_rings; count++) {
		sds_ring = &adapter->recv_ctx[0].sds_ring[count];

		if (sds_ring->interrupt_crb_addr) {
			mutex_enter(&sds_ring->sds_lock);
			qlcnic_disable_int(sds_ring);
			mutex_exit(&sds_ring->sds_lock);
		}
	}
	if (adapter->rx_ring_created == B_FALSE) {
		cmn_err(CE_NOTE, "qlcnic_remove(%d) skip rx ring clearing \n",
		    adapter->instance);
		goto tx_cleaning;
	}
	/* Wait for rx buffers to be returned from upper layers */
	for (ctx = 0; ctx < MAX_RCV_CTX; ++ctx) {
		recv_ctx = &adapter->recv_ctx[ctx];

		/* rds rings */
		for (ring = 0; ring < adapter->max_rds_rings; ring++) {
			rcv_desc = recv_ctx->rcv_desc[ring];

			if (rcv_desc->rx_buf_indicated) {
#ifdef LOAD_UNLOAD
				cmn_err(CE_WARN,
				    "!%s%d: rds ring %p waiting (upto 10 sec) "
				    "for %d buffers"
				    " from OS",
				    adapter->name, adapter->instance, rcv_desc,
				    rcv_desc->rx_buf_indicated);
#endif
				count = 0;
				while (rcv_desc->rx_buf_indicated &&
				    (count < 100)) {
					qlcnic_delay(100);
					count++;
				}
				if (rcv_desc->rx_buf_indicated) {
					/* EMPTY */
#ifdef LOAD_UNLOAD
					cmn_err(CE_WARN,
					    "!%s%d: Timedout waiting"
					    " for %d buffers from OS !!!",
					    adapter->name,
					    adapter->instance,
					    rcv_desc->rx_buf_indicated);
#endif
				}
			} /* buffers pending with stack */
		} /* All RDS rings */
	}
tx_cleaning:
	DPRINTF(DBG_INIT, (CE_NOTE, "%s%d Clear TX queue ...\n",
	    adapter->name, adapter->instance));

	if (adapter->tx_ring_created == B_FALSE) {
		cmn_err(CE_NOTE, "qlcnic_remove(%d) skip tx ring clearing \n",
		    adapter->instance);
		goto stop_watchdog;
	}
	/* Free any unprocessed transmit complete and free buffer */
	if (wflag) {
		for (i = 0; i < adapter->max_tx_rings; i++) {
			tx_ring = &(adapter->tx_ring[i]);

			mutex_enter(&tx_ring->tx_lock);
			last_consumer = tx_ring->lastCmdConsumer;
			last_producer = tx_ring->cmdProducer;
			while (last_consumer != last_producer) {
				buffer = &tx_ring->cmd_buf_arr[last_consumer];
				if (buffer->head != NULL) {
					dmah = buffer->head;
					while (dmah != NULL) {
						(void) ddi_dma_unbind_handle(
						    dmah->dmahdl);
						dmah = dmah->next;
						free_hdls++;
					}

					if (head == NULL) {
						head = buffer->head;
						tail = buffer->tail;
					} else {
						tail->next = buffer->head;
						tail = buffer->tail;
					}

					buffer->head = NULL;
					buffer->tail = NULL;
					if (buffer->msg != NULL) {
						freemsg(buffer->msg);
						buffer->msg = NULL;
					}
				}
				last_consumer = get_next_index(last_consumer,
				    adapter->MaxTxDescCount);
				count++;
			}
			if (count) {
				tx_ring->freecmds += count;
			}
			tx_ring->lastCmdConsumer = 0;
			tx_ring->cmdProducer = 0;
			mutex_exit(&tx_ring->tx_lock);
			if (head != NULL)
				qlcnic_return_dma_handle(tx_ring, head);
		}
	} else {
		/*
		 * If we are doing firmware recovery, unmap all the buffers
		 * from the transmit queue, including ones in flight
		 */
		for (i = 0; i < adapter->max_tx_rings; i++) {
			tx_ring = &(adapter->tx_ring[i]);

			mutex_enter(&tx_ring->tx_lock);
			last_consumer = 0;
			for (; last_consumer < adapter->MaxTxDescCount;
			    last_consumer++) {
				buffer = &tx_ring->cmd_buf_arr[last_consumer];
				if (buffer->head != NULL) {
					dmah = buffer->head;
					while (dmah != NULL) {
						(void) ddi_dma_unbind_handle(
						    dmah->dmahdl);
						dmah = dmah->next;
						free_hdls++;
					}

					if (head == NULL) {
						head = buffer->head;
						tail = buffer->tail;
					} else {
						tail->next = buffer->head;
						tail = buffer->tail;
					}

					buffer->head = NULL;
					buffer->tail = NULL;
					if (buffer->msg != NULL) {
						freemsg(buffer->msg);
						buffer->msg = NULL;
					}
				}
			}

			mutex_exit(&tx_ring->tx_lock);
		}
	}

stop_watchdog:
	if (wflag) {
		if (adapter->watchdog_running) {
			(void) untimeout(adapter->watchdog_timer);
			adapter->watchdog_running = 0;
		}
	}

	if (adapter->is_up == QLCNIC_ADAPTER_UP_MAGIC) {
		if (wflag) {
			qlcnic_free_hw_resources(adapter);
			qlcnic_destroy_rxtx_rings(adapter);
		}
	}

	if (QLCNIC_IS_REVISION_P3PLUS(adapter->ahw.revision_id)) {
		/* Remove driver active bit only on detach */
		if (wflag)
			(void) qlcnic_p3p_clear_dev_state(adapter, 0x1);
	}

	mac_link_update(adapter->mach, LINK_STATE_UNKNOWN);

	qlcnic_free_dummy_dma(adapter);

	adapter->is_up = 0;

	for (i = 0; i < adapter->max_tx_rings; i++) {
		tx_ring = &(adapter->tx_ring[i]);

		tx_ring->cmdProducer = 0;
		tx_ring->lastCmdConsumer = 0;
	}

	adapter->ahw.linkup = 0;

	for (count = 0; count < adapter->max_sds_rings; count++) {
		sds_ring = &recv_ctx->sds_ring[count];

		sds_ring->statusRxConsumer = 0;
		sds_ring->host_sds_consumer_addr = 0;
		sds_ring->interrupt_crb_addr = 0;
		sds_ring->no_rcv = 0;
		sds_ring->rxbytes = 0;

		for (ring = 0; ring < NUM_RCV_DESC_RINGS; ring++) {
			rx_recycle_list =
			    &sds_ring->rds_buf_recycle_list[ring];

			if (!wflag)
				bzero(rx_recycle_list->free_list,
				    rx_recycle_list->free_list_size);
			rx_recycle_list->head_index = 0;
			rx_recycle_list->tail_index = 0;
			rx_recycle_list->count = 0;
		}
	}
}

static int
init_firmware(qlcnic_adapter *adapter)
{
	uint32_t state = 0;
	uint32_t loops = 0;
	uint32_t tempout;

	/* Window 1 call */
	QLCNIC_READ_LOCK(&adapter->adapter_lock);
	state = adapter->qlcnic_pci_read_normalize(adapter, CRB_CMDPEG_STATE);
	QLCNIC_READ_UNLOCK(&adapter->adapter_lock);

	if (state == PHAN_INITIALIZE_ACK)
		return (0);

	while (state != PHAN_INITIALIZE_COMPLETE && loops < 200000) {
		drv_usecwait(100);
		/* Window 1 call */
		QLCNIC_READ_LOCK(&adapter->adapter_lock);
		state = adapter->qlcnic_pci_read_normalize(adapter,
		    CRB_CMDPEG_STATE);
		QLCNIC_READ_UNLOCK(&adapter->adapter_lock);
		loops++;
	}

	if (loops >= 200000) {
		cmn_err(CE_WARN, "%s%d: CmdPeg init incomplete:%x",
		    adapter->name, adapter->instance, state);
		return (-EIO);
	}

	/* Window 1 call */
	QLCNIC_READ_LOCK(&adapter->adapter_lock);
	tempout = INTR_SCHEME_PERPORT;
	adapter->qlcnic_hw_write_wx(adapter, CRB_NIC_CAPABILITIES_HOST,
	    &tempout, 4);
	tempout = MPORT_MULTI_FUNCTION_MODE;
	adapter->qlcnic_hw_write_wx(adapter, CRB_MPORT_MODE, &tempout, 4);
	tempout = PHAN_INITIALIZE_ACK;
	adapter->qlcnic_hw_write_wx(adapter, CRB_CMDPEG_STATE, &tempout, 4);
	QLCNIC_READ_UNLOCK(&adapter->adapter_lock);

	return (0);
}

/*
 * Utility to synchronize with receive peg.
 *  Returns   0 on sucess
 *         -EIO on error
 */
int
qlcnic_receive_peg_ready(struct qlcnic_adapter_s *adapter)
{
	uint32_t state = 0;
	int loops = 0, err = 0;

	/* Window 1 call */
	QLCNIC_READ_LOCK(&adapter->adapter_lock);
	state = adapter->qlcnic_pci_read_normalize(adapter, CRB_RCVPEG_STATE);
	QLCNIC_READ_UNLOCK(&adapter->adapter_lock);

	while ((state != PHAN_PEG_RCV_INITIALIZED) && (loops < 20000)) {
		drv_usecwait(100);
		/* Window 1 call */

		QLCNIC_READ_LOCK(&adapter->adapter_lock);
		state = adapter->qlcnic_pci_read_normalize(adapter,
		    CRB_RCVPEG_STATE);
		QLCNIC_READ_UNLOCK(&adapter->adapter_lock);

		loops++;
	}

	if (loops >= 20000) {
		cmn_err(CE_WARN, "Receive Peg initialization incomplete 0x%x",
		    state);
		err = -EIO;
	}

	return (err);
}

/*
 * check if the firmware has been downloaded and ready to run  and
 * setup the address for the descriptors in the adapter
 */
static int
qlcnic_hw_resources(qlcnic_adapter *adapter)
{
	void *addr;
	int err;
	int ctx, ring;
	qlcnic_recv_context_t *recv_ctx;
	qlcnic_rcv_desc_ctx_t *rcv_desc;
	ddi_dma_cookie_t cookie;
	int size;
	struct qlcnic_sds_ring_s *sds_ring;
	struct qlcnic_tx_ring_s *tx_ring;
	int i;
	qlcnic_rds_buf_recycle_list_t *rx_recycle_list;

	if (err = qlcnic_receive_peg_ready(adapter))
		return (err);

	/*
	 * allocate RingContext and cmd consumer index shadow registers of
	 * all tx rings.
	 */
	size = sizeof (RingContext) + sizeof (uint32_t) * adapter->max_tx_rings;

	err = qlcnic_pci_alloc_consistent(adapter,
	    size, (caddr_t *)&addr, &cookie,
	    &adapter->ctxDesc_dma_handle,
	    &adapter->ctxDesc_acc_handle);
	if (err != DDI_SUCCESS) {
		cmn_err(CE_WARN, "Failed to allocate HW context");
		return (err);
	}

	adapter->ctxDesc_physAddr = cookie.dmac_laddress;

	(void) memset(addr, 0, sizeof (RingContext));

	adapter->ctxDesc = (RingContext *) addr;
	adapter->ctxDesc->CtxId = HOST_TO_LE_32(adapter->portnum);

	for (ring = 0; ring < adapter->max_tx_rings; ring ++) {
		tx_ring = &(adapter->tx_ring[ring]);
		tx_ring->index = ring;
		addr = (void *)adapter->ctxDesc;
		tx_ring->cmd_consumer_offset =
		    sizeof (RingContext) + (sizeof (uint32_t)) * ring;
		tx_ring->cmdConsumer = (uint32_t *)(uintptr_t)
		    (((char *)addr) + tx_ring->cmd_consumer_offset);
		tx_ring->cmd_consumer_phys =
		    adapter->ctxDesc_physAddr + tx_ring->cmd_consumer_offset;
		/*
		 * Allocate Tx command descriptor ring.
		 */
		size = (TX_DESC_LEN * adapter->MaxTxDescCount);
		err = qlcnic_pci_alloc_consistent(adapter,
		    size, (caddr_t *)&addr, &cookie,
		    &tx_ring->cmd_desc_dma_handle,
		    &tx_ring->cmd_desc_acc_handle);
		if (err != DDI_SUCCESS) {
			cmn_err(CE_WARN, "Failed to allocate cmd desc ring");
			return (err);
		}

		tx_ring->cmdDesc_physAddr = cookie.dmac_laddress;
		tx_ring->cmdDescHead = (cmdDescType0_t *)addr;

		tx_ring->ring_handle =
		    adapter->tx_reserved_attr[ring].ring_handle;

	}

	ASSERT(!((unsigned long)adapter->ctxDesc_physAddr & 0x3f));


	for (ctx = 0; ctx < MAX_RCV_CTX; ++ctx) {
		recv_ctx = &adapter->recv_ctx[ctx];

		/* sds rings */
		for (ring = 0; ring < adapter->max_sds_rings; ring++) {
			sds_ring = &recv_ctx->sds_ring[ring];
			sds_ring->index = ring;
			sds_ring->max_status_desc_count =
			    adapter->MaxStatusDescCount;
			size = (sizeof (statusDesc_t) *
			    sds_ring->max_status_desc_count);
			err = qlcnic_pci_alloc_consistent(adapter,
			    size, (caddr_t *)&addr,
			    &sds_ring->status_desc_dma_cookie,
			    &sds_ring->status_desc_dma_handle,
			    &sds_ring->status_desc_acc_handle);
			if (err != DDI_SUCCESS) {
				cmn_err(CE_WARN, "Failed to allocate sts desc "
				    "ring for index %d", ring);
				goto free_cmd_desc;
			}

			(void) memset(addr, 0, size);
			sds_ring->rcvStatusDesc_physAddr =
			    sds_ring->status_desc_dma_cookie.dmac_laddress;
			sds_ring->rcvStatusDescHead = (statusDesc_t *)addr;
			sds_ring->adapter = adapter;
			mutex_init(&sds_ring->sds_lock, NULL, MUTEX_DRIVER,
			    DDI_INTR_PRI(adapter->intr_pri));

			sds_ring->ring_gen_num =
			    adapter->rx_reserved_attr[ring].ring_gen_num;
			sds_ring->ring_handle =
			    adapter->rx_reserved_attr[ring].ring_handle;

			for (i = 0; i < adapter->max_rds_rings; i ++) {
				rx_recycle_list =
				    &sds_ring->rds_buf_recycle_list[i];
				mutex_init(&rx_recycle_list->lock, NULL,
				    MUTEX_DRIVER,
				    DDI_INTR_PRI(adapter->intr_pri));
				rx_recycle_list->count = 0;
			}
		}

		/* rds rings */
		for (ring = 0; ring < adapter->max_rds_rings; ring++) {
			rcv_desc = recv_ctx->rcv_desc[ring];

			size = (sizeof (rcvDesc_t) * rcv_desc->MaxRxDescCount);
			err = qlcnic_pci_alloc_consistent(adapter,
			    size, (caddr_t *)&addr,
			    &rcv_desc->rx_desc_dma_cookie,
			    &rcv_desc->rx_desc_dma_handle,
			    &rcv_desc->rx_desc_acc_handle);
			if (err != DDI_SUCCESS) {
				cmn_err(CE_WARN, "Failed to allocate "
				    "rx desc ring %d", ring);
				goto free_status_desc;
			}

			rcv_desc->phys_addr =
			    rcv_desc->rx_desc_dma_cookie.dmac_laddress;
			rcv_desc->desc_head = (rcvDesc_t *)addr;
		}
	}

	if ((err = qlcnic_alloc_rx_freelist(adapter)))
		goto free_statusrx_desc;

	if (err = qlcnic_create_rxtx_ctx(adapter))
		goto free_statusrx_desc;
	adapter->context_allocated = 1;

	return (DDI_SUCCESS);

free_statusrx_desc:
free_status_desc:
free_cmd_desc:
	qlcnic_free_hw_resources(adapter);

	return (err);
}

void
qlcnic_desc_dma_sync(ddi_dma_handle_t handle, uint_t start, uint_t count,
    uint_t range, uint_t unit_size, uint_t direction)
{
	if ((start + count) < range) {
		(void) ddi_dma_sync(handle, start * unit_size,
		    count * unit_size, direction);
	} else {
		(void) ddi_dma_sync(handle, start * unit_size, 0, direction);
		(void) ddi_dma_sync(handle, 0,
		    (start + count - range) * unit_size, direction);
	}
}

void
qlcnic_update_cmd_producer(struct qlcnic_tx_ring_s *tx_ring,
    uint32_t crb_producer)
{
	struct qlcnic_adapter_s *adapter = tx_ring->adapter;
	int data = crb_producer;

	if (tx_ring->crb_addr_cmd_producer) {
		QLCNIC_PCI_WRITE_32(data,
		    (unsigned long)tx_ring->crb_addr_cmd_producer);
	}
	if (qlcnic_check_acc_handle(adapter,
	    adapter->regs_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(adapter->dip,
		    DDI_SERVICE_DEGRADED);
		qlcnic_force_fw_reset(adapter);
	}
}

/*
 * Looks for type of packet and sets opcode accordingly
 * so that LSO/TSO offload or checksum offload can be used.
 * LSO/TSO has higher priority.
 */
static void
qlcnic_tx_offload_check(cmdDescType0_t *desc, pktinfo_t *pktinfo)
{
	uint16_t opcode = TX_ETHER_PKT;
	uint16_t flags = 0;

	if (pktinfo->mac_hlen == ETHER_VALAN_HEADER_LEN)
		flags = FLAGS_VLAN_TAGGED;

	/* if TSO is needed */
	if ((pktinfo->l4_proto == IPPROTO_TCP) && pktinfo->use_lso &&
	    (pktinfo->mss != 0) && (pktinfo->l4_hlen != 0)) {
		desc->ip_hdr_offset = pktinfo->mac_hlen;
		desc->tcp_hdr_offset = pktinfo->mac_hlen + pktinfo->ip_hlen;
		desc->total_hdr_length = desc->tcp_hdr_offset +
		    pktinfo->l4_hlen;
		desc->mss = HOST_TO_LE_16(pktinfo->mss);

		opcode = (pktinfo->etype == ETHERTYPE_IP) ?
		    TX_TCP_LSO :TX_TCP_LSO6;

	} else if ((pktinfo->etype == ETHERTYPE_IP) && pktinfo->use_cksum) {

		/*
		 * For TCP/UDP, ask hardware to do both IP header and
		 * full checksum, even if stack has already done one or
		 * the other. Hardware will always get it correct even
		 * if stack has already done it.
		 */
		switch (pktinfo->l4_proto) {
			case IPPROTO_TCP:
				opcode = TX_TCP_PKT;
				break;
			case IPPROTO_UDP:
				opcode = TX_UDP_PKT;
				break;
			default:
				/* Must be here with HCK_IPV4_HDRCKSUM */
				opcode = TX_IP_PKT;
				return;
		}

		desc->ip_hdr_offset = pktinfo->mac_hlen;
		desc->tcp_hdr_offset = pktinfo->mac_hlen + pktinfo->ip_hlen;
	}
exit:
	qlcnic_set_tx_flags_opcode(desc, flags, opcode);
}

/*
 * check if OS requires us to do checksum or LSO / TSO offload
 */
static void
qlcnic_get_pkt_offload_info(struct qlcnic_adapter_s *adapter, mblk_t *mp,
    boolean_t *use_cksum, boolean_t *use_lso, uint16_t *mss)
{
	uint32_t pflags;

	mac_hcksum_get(mp, NULL, NULL, NULL, NULL, &pflags);
	if (pflags & (HCK_FULLCKSUM | HCK_IPV4_HDRCKSUM))
		*use_cksum = B_TRUE;

	if (adapter->flags & QLCNIC_LSO_ENABLED) {
		uint32_t pkt_mss = 0;
		uint32_t lso_flags = 0;
		mac_lso_get(mp, &pkt_mss, &lso_flags);
		*use_lso = (lso_flags == HW_LSO);
		*mss = (uint16_t)pkt_mss;
	}

}
/*
 * For IP/UDP/TCP checksum offload, this checks for MAC+IP header in one
 * contiguous block ending at 8 byte aligned address as required by hardware.
 * Caller assumes pktinfo->total_len will be updated by this function and
 * if pktinfo->etype is set to 0, it will need to linearize the mblk and
 * invoke qlcnic_update_pkt_info() to determine ethertype, IP header len and
 * protocol.
 */
static int
qlcnic_get_pkt_info(mblk_t *mp, pktinfo_t *pktinfo)
{
	mblk_t *bp;
	ushort_t type;
	uint32_t size;
	boolean_t ret;

	for (bp = mp; bp != NULL; bp = bp->b_cont) {
		if (MBLKL(bp) == 0)
			continue;
		size = MBLKL(bp);
		pktinfo->total_len += size;
		pktinfo->mblk_no++;
	}
	/* if MAC+IP header not in one contiguous block, use copy mode */
	if (MBLKL(mp) < (ETHER_HEADER_LEN + IP_HEADER_LEN)) {
		return (B_FALSE);
	}

	/*
	 * We just need non 1 byte aligned address, since ether_type is
	 * ushort.
	 */
	if ((uintptr_t)mp->b_rptr & 1) {
		cmn_err(CE_WARN, "qlcnic_get_pkt_info: b_rptr addr not even");
		return (B_FALSE);
	}

	type = ((struct ether_header *)(uintptr_t)mp->b_rptr)->ether_type;
	if (type == htons(ETHERTYPE_VLAN)) {
		if (MBLKL(mp) < (sizeof (struct ether_vlan_header) +
		    sizeof (ipha_t))) {
			return (B_FALSE);
		}
		type = ((struct ether_vlan_header *) \
		    (uintptr_t)mp->b_rptr)->ether_type;
		pktinfo->mac_hlen = ETHER_VALAN_HEADER_LEN;
	} else {
		pktinfo->mac_hlen = ETHER_HEADER_LEN;
	}
	pktinfo->etype = htons(type);

	if (pktinfo->etype == ETHERTYPE_IP) {
		uchar_t *ip_off = mp->b_rptr + pktinfo->mac_hlen;
		pktinfo->ip_hlen = IPH_HDR_LENGTH((uintptr_t)ip_off);
		pktinfo->l4_proto =
		    ((ipha_t *)(uintptr_t)ip_off)->ipha_protocol;

		/*
		 * if TCP packet and LSO/TSO is needed, mss is valid, we
		 * calculate TCP hdr length
		 */
		if ((pktinfo->l4_proto == IPPROTO_TCP) && pktinfo->use_lso &&
		    (pktinfo->mss != 0)) {
			uchar_t *l4_off =
			    mp->b_rptr + pktinfo->mac_hlen + pktinfo->ip_hlen;
			uint16_t l4_hlen = TCP_HDR_LENGTH((tcph_t *)l4_off);
			if (MBLKL(mp) < (pktinfo->mac_hlen+pktinfo->ip_hlen)) {
				cmn_err(CE_NOTE,
				    "qlcnic_get_pkt_info: tcp packet "
				    "header not on a contiguous buffer\n");
				ret = B_FALSE;
				goto exit;
			}
			pktinfo->l4_hlen = l4_hlen;
		}
	}
	ret = B_TRUE;
exit:
	return (ret);
}

static void
qlcnic_update_pkt_info(char *ptr, pktinfo_t *pktinfo)
{
	ushort_t type;

	type = ((struct ether_header *)(uintptr_t)ptr)->ether_type;
	if (type == htons(ETHERTYPE_VLAN)) {
		type = ((struct ether_vlan_header *)(uintptr_t)ptr)->ether_type;
		pktinfo->mac_hlen = ETHER_VALAN_HEADER_LEN;
	} else {
		pktinfo->mac_hlen = ETHER_HEADER_LEN;
	}
	pktinfo->etype = ntohs(type);
	if (pktinfo->etype == ETHERTYPE_IP) {
		char *ipp = ptr + pktinfo->mac_hlen;

		pktinfo->ip_hlen = IPH_HDR_LENGTH((uintptr_t)ipp);
		pktinfo->l4_proto = ((ipha_t *)(uintptr_t)ipp)->ipha_protocol;

		if ((pktinfo->l4_proto == IPPROTO_TCP) && pktinfo->use_lso &&
		    (pktinfo->mss != 0)) {
			char *l4_off =
			    ptr + pktinfo->mac_hlen + pktinfo->ip_hlen;
			pktinfo->l4_hlen = TCP_HDR_LENGTH((tcph_t *)l4_off);
		}
	}
}

/*
 * Copy MAC/IP/TCP headers to descriptors starting from current "producer"
 * index
 */
static void
qlcnic_copy_pkt_hdrs_to_desc(struct qlcnic_tx_ring_s *tx_ring,
    uint32_t producer, uint8_t *txb, uint32_t total_hdr_len)
{
	struct qlcnic_adapter_s *adapter = tx_ring->adapter;
	uint8_t copied, copy_len;
	cmdDescType0_t *hwdesc;
	uint8_t desc_size = TX_DESC_LEN;
	uint32_t MaxTxDescCount = adapter->MaxTxDescCount;
	uint8_t offset = IP_ALIGNMENT_BYTES;

	copied = 0;

	while (copied < total_hdr_len) {
		hwdesc = &tx_ring->cmdDescHead[producer];
		bzero(hwdesc, desc_size);
		copy_len = min((desc_size - offset), (total_hdr_len - copied));
		bcopy(txb, (char *)hwdesc + offset, copy_len);
		txb += copy_len;
		copied += copy_len;
		offset = 0;
		producer = get_next_index(producer, MaxTxDescCount);
	}
}
static void
qlcnic_return_tx_buf(struct qlcnic_tx_ring_s *tx_ring,
    qlcnic_buf_node_t *buf)
{
	uint32_t index;
	int num = 0;
	qlcnic_buf_node_t *next;
	struct qlcnic_adapter_s *adapter = tx_ring->adapter;

	ASSERT(buf != NULL);

	mutex_enter(&tx_ring->buf_tail_lock);
	index = tx_ring->buf_tail;
	while (buf != NULL) {
		next = buf->next;
		buf->next = NULL;
		tx_ring->buf_free_list[index] = buf;
		num++;
		index = get_next_index(index, adapter->MaxTxDescCount);
		buf = next;
	}
	tx_ring->buf_tail = index;
	atomic_add_32(&tx_ring->buf_free, num);
	mutex_exit(&tx_ring->buf_tail_lock);
}

static qlcnic_buf_node_t *
qlcnic_reserve_tx_buf(struct qlcnic_tx_ring_s *tx_ring)
{
	qlcnic_buf_node_t *buf = NULL;
	struct qlcnic_adapter_s *adapter = tx_ring->adapter;
	/*
	 * check if there is any in the free list
	 */
	if (qlcnic_atomic_reserve(&tx_ring->buf_free, 1) < 0)
		return (NULL);

	mutex_enter(&tx_ring->buf_head_lock);
	buf = tx_ring->buf_free_list[tx_ring->buf_head];
	if (buf == NULL) {
		mutex_exit(&tx_ring->buf_head_lock);
		cmn_err(CE_WARN, "!tx buf NULL buf_free %d buf_head %d \n",
		    tx_ring->buf_free, tx_ring->buf_head);
		return (NULL);
	}
	tx_ring->buf_free_list[tx_ring->buf_head] = NULL;
	tx_ring->buf_head =
	    get_next_index(tx_ring->buf_head, adapter->MaxTxDescCount);
	mutex_exit(&tx_ring->buf_head_lock);

	return (buf);
}
static boolean_t
qlcnic_send_copy(struct qlcnic_tx_ring_s *tx_ring, mblk_t *mp,
    pktinfo_t *pktinfo)
{
	struct qlcnic_adapter_s *adapter = tx_ring->adapter;
	uint32_t producer, saved_producer;
	cmdDescType0_t *hwdesc, *swdesc;
	struct qlcnic_cmd_buffer *pbuf = NULL;
	uint32_t mblen;
	int no_of_desc = 1;
	int MaxTxDescCount = adapter->MaxTxDescCount;
	mblk_t *bp;
	uint8_t *txb;
	uint8_t total_hdr_len = 0;
	boolean_t use_lso = B_FALSE;
	uint8_t desc_size = TX_DESC_LEN;
	boolean_t config_ip_addr = B_FALSE;
	uint32_t ip_addr;
	int ret;
	cmdDescType0_t temp_desc;
	qlcnic_buf_node_t *tx_buffer = qlcnic_reserve_tx_buf(tx_ring);

	swdesc = &temp_desc;
	bzero(&temp_desc, desc_size);


	if (tx_buffer == NULL) {
		tx_ring->stats.outoftxbuffer++;
		tx_ring->resched_needed = 1;
		return (B_FALSE);
	}

	txb = tx_buffer->dma_area.vaddr;

	for (bp = mp; bp != NULL; bp = bp->b_cont) {
		if ((mblen = MBLKL(bp)) == 0)
			continue;
		bcopy(bp->b_rptr, txb, mblen);
		txb += mblen;
	}
	/*
	 * Determine metadata if not previously done due to fragmented mblk.
	 */
	if (pktinfo->etype == 0)
		qlcnic_update_pkt_info(tx_buffer->dma_area.vaddr, pktinfo);

	(void) ddi_dma_sync(tx_buffer->dma_area.dma_hdl,
	    tx_buffer->dma_area.offset, pktinfo->total_len,
	    DDI_DMA_SYNC_FORDEV);

	/*
	 * check if we should do TSO/LSO on this packet. If needed, we
	 * need to copy MAC+IP+TCP headers to end of previous descriptors.
	 */
	if ((pktinfo->l4_proto == IPPROTO_TCP) && pktinfo->use_lso &&
	    (pktinfo->mss != 0)) {
		total_hdr_len = pktinfo->mac_hlen + pktinfo->ip_hlen +
		    pktinfo->l4_hlen;
		/* how many additional descriptors needed ? */
		no_of_desc += (total_hdr_len < (desc_size-IP_ALIGNMENT_BYTES)) ?
		    1 : (1 + (total_hdr_len - (desc_size - IP_ALIGNMENT_BYTES) +
		    desc_size -1) / desc_size);
		use_lso = B_TRUE;
	}

	qlcnic_tx_offload_check(swdesc, pktinfo); /* update u1 only */
	qlcnic_set_tx_frags_len(swdesc, no_of_desc, pktinfo->total_len);
	qlcnic_set_tx_port(swdesc, adapter->portnum);
	swdesc->buffer_length[0] = HOST_TO_LE_16(pktinfo->total_len);
	swdesc->addr_buffer1 = HOST_TO_LE_64(tx_buffer->dma_area.dma_addr);

	mutex_enter(&tx_ring->tx_lock);

	if (find_diff_among(tx_ring->cmdProducer, tx_ring->lastCmdConsumer,
	    MaxTxDescCount) <= 4) {
		tx_ring->stats.outofcmddesc++;
		tx_ring->resched_needed = 1;
		mutex_exit(&tx_ring->tx_lock);
		qlcnic_return_tx_buf(tx_ring, tx_buffer);
		return (B_FALSE);
	}

	saved_producer = producer = tx_ring->cmdProducer;
	hwdesc = &tx_ring->cmdDescHead[producer];
	bcopy(swdesc, hwdesc, desc_size);

	pbuf = &tx_ring->cmd_buf_arr[producer];
	pbuf->msg = NULL;
	pbuf->head = NULL;
	pbuf->tail = NULL;
	pbuf->buf = tx_buffer;

	/* copy MAC/IP/TCP headers to following descriptors */
	producer = get_next_index(producer, MaxTxDescCount);
	if (use_lso) {
		txb = tx_buffer->dma_area.vaddr;
		qlcnic_copy_pkt_hdrs_to_desc(tx_ring, producer, txb,
		    total_hdr_len);
	}

	qlcnic_desc_dma_sync(tx_ring->cmd_desc_dma_handle,
	    saved_producer,
	    no_of_desc,
	    MaxTxDescCount,
	    TX_DESC_LEN,
	    DDI_DMA_SYNC_FORDEV);

	/* increment to next produceer */
	tx_ring->cmdProducer =
	    get_index_range(saved_producer, MaxTxDescCount, no_of_desc);

	qlcnic_update_cmd_producer(tx_ring, tx_ring->cmdProducer);

	tx_ring->stats.txbytes += pktinfo->total_len;
	tx_ring->stats.xmitfinished++;
	tx_ring->stats.txcopyed++;

	tx_ring->freecmds -= no_of_desc;

	mutex_exit(&tx_ring->tx_lock);

	/*
	 * Add current ip address to firmware for RSS to work SJP
	 * TODO: Check if there is another way to get our ip address
	 */
	if (pktinfo->etype == ETHERTYPE_ARP) {
		/* Get current IP address if it is an ARP reply */
		arp_hdr_t *arp_hdr =
		    (arp_hdr_t *)((uint8_t *)tx_buffer->dma_area.vaddr +
		    pktinfo->mac_hlen);
		uint16_t opcode;

		ip_addr = HOST_TO_LE_32(arp_hdr->sender_ip);

		if (ip_addr && ip_addr != adapter->current_ip_addr) {
			opcode = ntohs(arp_hdr->opcode);
			if ((opcode == ARP_REPLY) || (opcode == ARP_REQUEST)) {
				config_ip_addr = B_TRUE;
			}
		}
	}

	freemsg(mp);

	if (config_ip_addr) {
		mutex_enter(&tx_ring->tx_lock);
		ret = qlcnic_configure_ip_addr(tx_ring, ip_addr);
		mutex_exit(&tx_ring->tx_lock);
		if (ret) {
			cmn_err(CE_WARN, "!%s%d: Failed to set "
			    "current ip address to 0x%x",
			    adapter->name, adapter->instance,
			    ip_addr);
		} else {
			mutex_enter(&adapter->lock);
			adapter->current_ip_addr = ip_addr;
			mutex_exit(&adapter->lock);
		}
	}
	return (B_TRUE);
}

/*
 * qlcnic_atomic_reserve - Atomic decrease operation.
 */
int
qlcnic_atomic_reserve(uint32_t *count_p, uint32_t n)
{
	uint32_t oldval;
	uint32_t newval;

	/*
	 * ATOMICALLY
	 */
	do {
		oldval = *count_p;
		if (oldval < n)
			return (-1);
		newval = oldval - n;
	} while (atomic_cas_32(count_p, oldval, newval) != oldval);

	return (newval);
}

static void
qlcnic_return_dma_handle(struct qlcnic_tx_ring_s *tx_ring,
    qlcnic_dmah_node_t *dmah)
{
	uint32_t index;
	int num = 0;
	qlcnic_dmah_node_t *next;
	struct qlcnic_adapter_s *adapter = tx_ring->adapter;

	ASSERT(dmah != NULL);

	mutex_enter(&tx_ring->dmah_tail_lock);
	index = tx_ring->dmah_tail;
	while (dmah != NULL) {
		next = dmah->next;
		dmah->next = NULL;
		tx_ring->dmah_free_list[index] = dmah;
		num++;
		index = get_next_index(index, adapter->max_tx_dma_hdls);
		dmah = next;
	}
	tx_ring->dmah_tail = index;
	atomic_add_32(&tx_ring->dmah_free, num);
	mutex_exit(&tx_ring->dmah_tail_lock);

}

static qlcnic_dmah_node_t *
qlcnic_reserve_dma_handle(struct qlcnic_tx_ring_s *tx_ring)
{
	qlcnic_dmah_node_t *dmah = NULL;
	struct qlcnic_adapter_s *adapter = tx_ring->adapter;

	/*
	 * check if there is any in the free list
	 */
	if (qlcnic_atomic_reserve(&tx_ring->dmah_free, 1) < 0)
		return (NULL);

	mutex_enter(&tx_ring->dmah_head_lock);
	dmah = tx_ring->dmah_free_list[tx_ring->dmah_head];
	ASSERT(dmah != NULL);
	tx_ring->dmah_free_list[tx_ring->dmah_head] = NULL;
	tx_ring->dmah_head =
	    get_next_index(tx_ring->dmah_head, adapter->max_tx_dma_hdls);
	mutex_exit(&tx_ring->dmah_head_lock);
	return (dmah);
}

enum {
	TX_FAILURE = 0,
	TX_SUCCESS = 1,
	TX_PULLUP = 2};

static int
qlcnic_send_mapped(struct qlcnic_tx_ring_s *tx_ring,
    mblk_t *mp, pktinfo_t *pktinfo)
{
	struct qlcnic_adapter_s *adapter = tx_ring->adapter;
	uint32_t producer = 0;
	uint32_t saved_producer = 0;
	cmdDescType0_t *hwdesc, *swdesc;
	cmdDescType0_t temp_desc[MAX_CMD_DESC_PER_TX];
	struct qlcnic_cmd_buffer *pbuf = NULL;
	int no_of_desc;
	int MaxTxDescCount = adapter->MaxTxDescCount;
	mblk_t *bp;
	qlcnic_dmah_node_t *dmah, *head = NULL, *tail = NULL, *hdlp;
	ddi_dma_cookie_t cookie[MAX_COOKIES_PER_CMD + 1];
	int ret;
	uint32_t i, j, k;
	uint32_t hdl_reserved = 0;
	uint32_t mblen;
	uint32_t ncookies, index = 0, total_cookies = 0;
	uint8_t temp_hdr_buf[MAX_HDR_SIZE_PER_TX_PKT];
	uint8_t *txb;
	uint8_t total_hdr_len = 0;
	uint8_t copied, copy_len;
	boolean_t use_lso = B_FALSE;
	uint8_t desc_size = TX_DESC_LEN;
	int status = TX_FAILURE;
	boolean_t force_pullup = B_FALSE;

	/* bind all the mblks of the packet first */
	for (bp = mp; bp != NULL; bp = bp->b_cont) {
		mblen = MBLKL(bp);
		if (mblen == 0)
			continue;

		dmah = qlcnic_reserve_dma_handle(tx_ring);
		if (dmah == NULL) {
			mutex_enter(&tx_ring->tx_lock);
			tx_ring->stats.outoftxdmahdl++;
			mutex_exit(&tx_ring->tx_lock);
			goto err_map;
		}

		ret = ddi_dma_addr_bind_handle(dmah->dmahdl,
		    NULL, (caddr_t)bp->b_rptr, mblen,
		    DDI_DMA_STREAMING | DDI_DMA_WRITE,
		    DDI_DMA_DONTWAIT, NULL, &cookie[index], &ncookies);

		if (ret != DDI_DMA_MAPPED) {
			mutex_enter(&tx_ring->tx_lock);
			tx_ring->stats.dmabindfailures++;
			tx_ring->stats.lastdmabindfailsize = mblen;
			tx_ring->stats.lastdmabinderror = (uint64_t)(-1 * ret);
			mutex_exit(&tx_ring->tx_lock);
			force_pullup = B_TRUE;
			goto err_map;
		}

		if (tail == NULL) {
			head = tail = dmah;
		} else {
			tail->next = dmah;
			tail = dmah;
		}
		hdl_reserved++;

		total_cookies += ncookies;
		if (total_cookies > MAX_COOKIES_PER_CMD) {
			mutex_enter(&tx_ring->tx_lock);
			dmah = NULL;
			tx_ring->stats.lastfailedcookiecount = total_cookies;
			tx_ring->stats.exceedcookiesfailures++;
			mutex_exit(&tx_ring->tx_lock);
			/*
			 * if too many cookie needed but pkt size is less than
			 * tx buffer size then try copy mode.
			 */
			if ((pktinfo->mblk_no >1) && (pktinfo->total_len >
			    adapter->tx_buf_size)) {
				force_pullup = B_TRUE;
			}
			goto err_map;
		}

		if (index == 0) {
			size_t	hsize = cookie[0].dmac_size;

			/*
			 * For TCP/UDP packets with checksum offload,
			 * MAC/IP headers need to be contiguous. Otherwise,
			 * there must be at least 16 bytes in the first
			 * descriptor.
			 */
			if ((pktinfo->l4_proto == IPPROTO_TCP) ||
			    (pktinfo->l4_proto == IPPROTO_UDP)) {
				if (hsize < (pktinfo->mac_hlen +
				    pktinfo->ip_hlen)) {
					mutex_enter(&tx_ring->tx_lock);
					dmah = NULL;
					tx_ring->stats.lastfailedhdrsize =
					    hsize;
					tx_ring->stats.hdrsizefailures++;
					mutex_exit(&tx_ring->tx_lock);
					force_pullup = B_TRUE;
					goto err_map;
				}
			} else {
				if (hsize < 16) {
					mutex_enter(&tx_ring->tx_lock);
					dmah = NULL;
					tx_ring->stats.hdrtoosmallfailures++;
					mutex_exit(&tx_ring->tx_lock);
					force_pullup = B_TRUE;
					goto err_map;
				}
			}
		}

		index++;
		ncookies--;
		for (i = 0; i < ncookies; i++, index++)
			ddi_dma_nextcookie(dmah->dmahdl, &cookie[index]);
	}

	no_of_desc = (total_cookies + 3) >> 2;
	/*
	 * check if we should do TSO/LSO on this packet. If needed, we
	 * need to copy MAC+IP+TCP headers to end of previous descriptors.
	 */
	if ((pktinfo->l4_proto == IPPROTO_TCP) && pktinfo->use_lso &&
	    (pktinfo->mss != 0)) {
		total_hdr_len = pktinfo->mac_hlen + pktinfo->ip_hlen +
		    pktinfo->l4_hlen;
		/* how many additional descriptors needed ? */
		no_of_desc += (total_hdr_len < (desc_size-IP_ALIGNMENT_BYTES)) ?
		    1 : (1 + (total_hdr_len - (desc_size - IP_ALIGNMENT_BYTES) +
		    desc_size -1) / desc_size);
		use_lso = B_TRUE;
		/* copy packet headers to a contiguous temporary buffer */
		txb = temp_hdr_buf;
		copied = 0;
		bzero(txb, MAX_HDR_SIZE_PER_TX_PKT);
		for (bp = mp; bp != NULL; bp = bp->b_cont) {
			if ((mblen = MBLKL(bp)) == 0)
				continue;
			copy_len = (uint8_t)min(mblen,
			    (total_hdr_len - copied));
			bcopy(bp->b_rptr, txb, copy_len);
			txb += copy_len;
			copied += copy_len;
			if (copied >= total_hdr_len)
				break;
		}
	}

	j = 0;
	swdesc = &temp_desc[j];
	bzero(&temp_desc[j], desc_size);

	qlcnic_set_tx_frags_len(swdesc, total_cookies, pktinfo->total_len);
	qlcnic_set_tx_port(swdesc, adapter->portnum);

	qlcnic_tx_offload_check(swdesc, pktinfo); /* update uword1 only */

	/* add fragments list to temp descriptor list */
	for (i = k = 0; i < total_cookies; i++) {
		if (k == 4) {
			/* Move to the next descriptor */
			k = 0;
			j++;
			swdesc = &temp_desc[j];
			bzero(&temp_desc[j], desc_size);
		}

		swdesc->buffer_length[k] = HOST_TO_LE_16(cookie[i].dmac_size);

		switch (k) {
		case 0:
			swdesc->addr_buffer1 =
			    HOST_TO_LE_64(cookie[i].dmac_laddress);
			break;
		case 1:
			swdesc->addr_buffer2 =
			    HOST_TO_LE_64(cookie[i].dmac_laddress);
			break;
		case 2:
			swdesc->addr_buffer3 =
			    HOST_TO_LE_64(cookie[i].dmac_laddress);
			break;
		case 3:
			swdesc->addr_buffer4 =
			    HOST_TO_LE_64(cookie[i].dmac_laddress);
			break;
		}
		k++;
	}
	j++;

	mutex_enter(&tx_ring->tx_lock);
	dmah = NULL;
	if (find_diff_among(tx_ring->cmdProducer, tx_ring->lastCmdConsumer,
	    MaxTxDescCount) < no_of_desc+2) {
		/*
		 * If we are going to be trying the copy path, no point
		 * scheduling an upcall when Tx resources are freed.
		 */
		if (pktinfo->total_len > adapter->tx_buf_size) {
			tx_ring->stats.outofcmddesc++;
			tx_ring->resched_needed = 1;
		}
		mutex_exit(&tx_ring->tx_lock);
		goto err_alloc_desc;
	}
	tx_ring->freecmds -= no_of_desc;

	/* Copy the descriptors into the hardware    */
	saved_producer = producer = tx_ring->cmdProducer;

	pbuf = &tx_ring->cmd_buf_arr[producer];

	pbuf->msg = mp;
	pbuf->head = head;
	pbuf->tail = tail;
	/* copy tx descriptor from dummy to real tx descriptor list */
	for (i = 0; i < j; i ++) {
		hwdesc = &tx_ring->cmdDescHead[producer];
		bcopy(&(temp_desc[i]), hwdesc, desc_size);
		producer = get_next_index(producer, MaxTxDescCount);
	}

	/* copy MAC/IP/TCP headers to following descriptors */
	if (use_lso) {
		qlcnic_copy_pkt_hdrs_to_desc(tx_ring, producer, temp_hdr_buf,
		    total_hdr_len);
	}

	qlcnic_desc_dma_sync(tx_ring->cmd_desc_dma_handle, saved_producer,
	    no_of_desc,
	    MaxTxDescCount, desc_size, DDI_DMA_SYNC_FORDEV);

	tx_ring->cmdProducer =
	    get_index_range(tx_ring->cmdProducer, MaxTxDescCount, no_of_desc);

	tx_ring->stats.txbytes += pktinfo->total_len;
	tx_ring->stats.xmitfinished++;
	tx_ring->stats.txmapped++;

	qlcnic_update_cmd_producer(tx_ring, tx_ring->cmdProducer);

	mutex_exit(&tx_ring->tx_lock);

	status = TX_SUCCESS;
	return (status);

err_alloc_desc:
err_map:

	hdlp = head;
	while (hdlp != NULL) {
		(void) ddi_dma_unbind_handle(hdlp->dmahdl);
		hdlp = hdlp->next;
	}

	/*
	 * add the reserved but bind failed one to the list to be returned
	 */
	if (dmah != NULL) {
		if (tail == NULL)
			head = tail = dmah;
		else {
			tail->next = dmah;
			tail = dmah;
		}
		hdl_reserved++;
	}

	if (head != NULL)
		qlcnic_return_dma_handle(tx_ring, head);

	if (force_pullup)
		status = TX_PULLUP;

	return (status);
}

static boolean_t
qlcnic_xmit_frame(qlcnic_adapter *adapter, struct qlcnic_tx_ring_s *tx_ring,
    mblk_t *mp)
{
	pktinfo_t pktinfo;
	boolean_t status = B_FALSE;
	boolean_t send_mapped = B_FALSE;
	boolean_t pkt_parse_ok;
	boolean_t use_cksum = B_FALSE;
	boolean_t use_lso = B_FALSE;
	uint16_t mss = 0;
	boolean_t force_pullup = B_FALSE;
	mblk_t *pullup_mp = NULL;
	int send_mapped_status;

	tx_ring->stats.xmitcalled++;

	{
		uint32_t temp = 1;

		temp = atomic_swap_32(&tx_ring->tx_comp, temp);
		if (temp == 0) {
			qlcnic_process_cmd_ring(tx_ring);
			temp = atomic_swap_32(&tx_ring->tx_comp, temp);
		}
	}

	/* check if we need to do checksum offload or TSO offload */
	qlcnic_get_pkt_offload_info(adapter, mp, &use_cksum, &use_lso, &mss);

do_pullup:
	bzero(&pktinfo, sizeof (pktinfo_t));

	/* concatenate all frags into one large packet if too fragmented */
	if (force_pullup) {

		if ((pullup_mp = msgpullup(mp, -1)) != NULL) {
			/* save the original mblk */
			freemsg(mp);
			mp = pullup_mp;
			tx_ring->stats.msgpulluped++;
		} else {
			cmn_err(CE_WARN, "!qlcnic(%d) pkt pullup failed",
			    adapter->instance);
			goto exit;
		}
	}

	pktinfo.use_cksum = use_cksum;
	pktinfo.use_lso = use_lso;
	pktinfo.mss = mss;

	pkt_parse_ok = qlcnic_get_pkt_info(mp, &pktinfo);

	if (pktinfo.total_len > QLCNIC_LSO_MAXLEN)
		goto too_large;


	if (pktinfo.total_len > adapter->tx_bcopy_threshold)
		send_mapped = B_TRUE;

	/*
	 * If the packet can not be parsed due to fragmentation but its size is
	 * less than tx buffer, then use copy mode to transmit.
	 * If dma binding is needed to transmit this packet, but the sending
	 * pkt is too fragmented, or unable to parse and never pulled up before
	 * then concatinate all fragments into one large pkt.
	 */
	if ((!pkt_parse_ok || (pktinfo.mblk_no > 10)) &&
	    (pktinfo.total_len <= adapter->tx_buf_size))
		send_mapped = B_FALSE;

	if (send_mapped) {
		send_mapped_status = qlcnic_send_mapped(tx_ring, mp, &pktinfo);
		if (send_mapped_status == TX_SUCCESS)
			status = B_TRUE;
		else if ((send_mapped_status == TX_PULLUP) && !force_pullup) {
			force_pullup = B_TRUE;
			goto do_pullup;
		} else
			status = B_FALSE;
	}

	if (status != B_TRUE) {
		if (pktinfo.total_len <= adapter->tx_buf_size) {
			if (pktinfo.total_len > adapter->tx_bcopy_threshold)
				tx_ring->stats.sendcopybigpkt++;
			status = qlcnic_send_copy(tx_ring, mp, &pktinfo);
			goto exit;
		}

		goto too_large;
	}

exit:
	return (status);

too_large:
	/* message too large */
/*	freemsg(mp); */
	tx_ring->stats.txdropped++;
	status = B_TRUE;
	return (status);
}

static int
qlcnic_check_temp(struct qlcnic_adapter_s *adapter)
{
	uint32_t temp, temp_state, temp_val;
	int rv = 0;

	if ((adapter->ahw.revision_id == QLCNIC_P3_A2) ||
	    (adapter->ahw.revision_id == QLCNIC_P3_A0))
		return (0);

	temp = adapter->qlcnic_pci_read_normalize(adapter, CRB_TEMP_STATE);

	temp_state = qlcnic_get_temp_state(temp);
	temp_val = qlcnic_get_temp_val(temp);

	if (temp_state == QLCNIC_TEMP_PANIC) {
		cmn_err(CE_WARN, "%s: Device temperature %d C exceeds "
		    "maximum allowed, device has been shut down",
		    qlcnic_driver_name, temp_val);
		rv = 1;
	} else if (temp_state == QLCNIC_TEMP_WARN) {
		if (adapter->temp == QLCNIC_TEMP_NORMAL) {
		cmn_err(CE_WARN, "%s: Device temperature %d C exceeds"
		    "operating range. Immediate action needed.",
		    qlcnic_driver_name, temp_val);
		}
	} else {
		if (adapter->temp == QLCNIC_TEMP_WARN) {
			cmn_err(CE_WARN, "%s: Device temperature is now %d "
			    "degrees C in normal range.",
			    qlcnic_driver_name, temp_val);
		}
	}

	adapter->temp = temp_state;
	return (rv);
}

static void
qlcnic_watchdog(unsigned long v)
{
	qlcnic_adapter *adapter = (qlcnic_adapter *)v;
	struct qlcnic_tx_ring_s *tx_ring;

	if ((adapter->drv_state == QLCNIC_DRV_DETACH) ||
	    (adapter->drv_state == QLCNIC_DRV_SUSPEND)) {
		/*
		 * If detach is in progress, just return
		 */
		return;
	}

	if (QLCNIC_IS_REVISION_P3PLUS(adapter->ahw.revision_id)) {
		(void) qlcnic_check_health(adapter);
	}

	if ((adapter->portnum == 0) && qlcnic_check_temp(adapter)) {
		/*
		 * We return without turning on the netdev queue as there
		 * was an overheated device
		 */
		cmn_err(CE_NOTE, "check_temp\n");
		return;
	}

	if (adapter->drv_state == QLCNIC_DRV_OPERATIONAL)
		qlcnic_handle_phy_intr(adapter);

	if (adapter->is_up == QLCNIC_ADAPTER_UP_MAGIC) {
		uint32_t temp = 1;

		for (int i = 0; i < adapter->max_tx_rings; i ++) {
			tx_ring = &(adapter->tx_ring[i]);

			temp = atomic_swap_32(&tx_ring->tx_comp, temp);
			if (temp == 0) {
				qlcnic_process_cmd_ring(tx_ring);
				(void) atomic_swap_32(&tx_ring->tx_comp, temp);
			}
		}
	}

	/*
	 * This function schedules a call for itself.
	 */
	adapter->watchdog_timer = timeout((void (*)(void *))&qlcnic_watchdog,
	    (void *)adapter, 2 * drv_usectohz(1000000));

}

static void qlcnic_clear_stats(qlcnic_adapter *adapter)
{
	(void) memset(&adapter->stats, 0, sizeof (adapter->stats));
}

static mblk_t *
qlcnic_ring_rx_poll(void *arg, int poll_bytes, int poll_pkts)
{
	qlcnic_sds_ring_t *sds_ring = (qlcnic_sds_ring_t *)arg;
	qlcnic_adapter *adapter = sds_ring->adapter;
	mblk_t *mp;

	adapter->stats.polled++;

	mp = qlcnic_ring_rx(sds_ring, poll_bytes, poll_pkts);

	return (mp);
}

/* ARGSUSED */
uint_t
qlcnic_intr(caddr_t data, caddr_t arg)
{
	qlcnic_adapter *adapter = (qlcnic_adapter *)(uintptr_t)data;
	struct qlcnic_sds_ring_s *sds_ring = &adapter->recv_ctx[0].sds_ring[0];
	mblk_t *mp;

	if (qlcnic_clear_int(adapter))
		return (DDI_INTR_UNCLAIMED);

	mp = qlcnic_ring_rx_poll(sds_ring, QLCNIC_POLL_ALL, QLCNIC_POLL_ALL);
	if (mp)
		RX_UPSTREAM(sds_ring, mp);
	return (DDI_INTR_CLAIMED);
}

static mblk_t *
qlcnic_ring_rx(struct qlcnic_sds_ring_s *sds_ring,
    int poll_bytes, int poll_pkts)
{
	mblk_t *mp;

	mutex_enter(&sds_ring->sds_lock);
	mp = qlcnic_process_rcv_ring(sds_ring, poll_bytes, poll_pkts);
	mutex_exit(&sds_ring->sds_lock);

	return (mp);
}

/* ARGSUSED */
uint_t
qlcnic_msix_rx_intr(caddr_t data, caddr_t arg)
{
	struct qlcnic_sds_ring_s *sds_ring =
	    (struct qlcnic_sds_ring_s *)(uintptr_t)data;
	qlcnic_adapter *adapter = sds_ring->adapter;
	mblk_t *mp;
	struct qlcnic_tx_ring_s *tx_ring;
	int i;

	if (adapter->drv_state == QLCNIC_DRV_OPERATIONAL) {
		mp = qlcnic_ring_rx(sds_ring, QLCNIC_POLL_ALL, QLCNIC_POLL_ALL);

		if (adapter->diag_test == QLCNIC_INTERRUPT_TEST) {
			adapter->diag_cnt++;
		}

		if (mp) {
			RX_UPSTREAM(sds_ring, mp);
		}

		/* do tx completion if reschedule is needed */
		if (!sds_ring->index) {
			for (i = 0; i < adapter->max_tx_rings; i++) {
				tx_ring = &adapter->tx_ring[i];
				if (!tx_ring->resched_needed)
					continue;
				uint32_t temp = 1;

				temp = atomic_swap_32(&tx_ring->tx_comp,
				    temp);
				if (temp == 0) {
					qlcnic_process_cmd_ring(tx_ring);
					temp = atomic_swap_32(
					    &tx_ring->tx_comp, temp);
				}
			}
		}
	}

	/* Fix for 32-bit HCTS hang */
	mutex_enter(&sds_ring->sds_lock);
	qlcnic_enable_int(sds_ring);
	mutex_exit(&sds_ring->sds_lock);

	return (DDI_INTR_CLAIMED);
}

/*
 * This is invoked from receive isr.
 */
void
qlcnic_free_rx_buffer(struct qlcnic_sds_ring_s *sds_ring,
    qlcnic_rx_buffer_t *rx_buffer)
{
	int rds_ring_id = rx_buffer->rds_index;
	qlcnic_rds_buf_recycle_list_t *rx_recycle_list;
	int free_index;
	qlcnic_adapter *adapter;

	rx_recycle_list = &sds_ring->rds_buf_recycle_list[rds_ring_id];
	rx_buffer->sds_ring = NULL;
	/* put the rx buffer back to recycle list */
	mutex_enter(&rx_recycle_list->lock);
	free_index = rx_recycle_list->tail_index;

	DPRINTF(DBG_RX, (CE_WARN, "free buf %p at index %d, old value %p\n",
	    rx_buffer, free_index, rx_recycle_list->free_list[free_index]));
	if (rx_recycle_list->free_list[free_index]) {
		adapter = sds_ring->adapter;

		cmn_err(CE_WARN, "%s%d: Error entry at %d = %p != NULL\n",
		    adapter->name, adapter->instance, free_index,
		    (void *)rx_recycle_list->free_list[free_index]);
		mutex_exit(&rx_recycle_list->lock);
	} else {
		rx_recycle_list->free_list[free_index] = rx_buffer;
		rx_recycle_list->tail_index = get_next_index(free_index,
		    rx_recycle_list->max_free_entries);

		mutex_exit(&rx_recycle_list->lock);
		atomic_inc_32(&rx_recycle_list->count);

		if (rx_recycle_list->count >
		    rx_recycle_list->max_free_entries) {
			adapter = sds_ring->adapter;

			cmn_err(CE_WARN,
			    "%s%d: Error recycle count %d exceeds %d\n",
			    adapter->name, adapter->instance,
			    rx_recycle_list->count,
			    rx_recycle_list->max_free_entries);
			rx_recycle_list->count =
			    rx_recycle_list->max_free_entries;
		}
	}
}

/*
 * Update packet infor and check if receive checksum has been calculated
 */
static boolean_t
qlcnic_get_rx_cksum(qlcnic_adapter *adapter, uint64_t sts_data0)
{
	int cksum_flags;
	int cksum = qlcnic_get_sts_status(sts_data0);

	if (cksum == STATUS_CKSUM_OK) {
		adapter->stats.csummed++;
		cksum_flags = HCK_FULLCKSUM_OK;
	} else {
		cksum_flags = 0;
	}
	return (cksum_flags);
}

/*
 * qlcnic_process_rcv() send the received packet to the protocol stack.
 */
static mblk_t *
qlcnic_process_rcv(qlcnic_adapter *adapter, uint64_t sts_data0,
    struct qlcnic_sds_ring_s *sds_ring)
{
	qlcnic_recv_context_t *recv_ctx = &(adapter->recv_ctx[0]);
	qlcnic_rx_buffer_t *rx_buffer;
	mblk_t *mp;
	uint32_t desc_ctx = qlcnic_get_sts_type(sts_data0);
	qlcnic_rcv_desc_ctx_t *rcv_desc = recv_ctx->rcv_desc[desc_ctx];
	uint32_t pkt_length = qlcnic_get_sts_totallength(sts_data0);
	int poff = qlcnic_get_sts_pkt_offset(sts_data0);
	int index = qlcnic_get_sts_refhandle(sts_data0);
	int cksum_flags, docopy;
	char *vaddr;

	rx_buffer = index2rxbuf(rcv_desc, index);

	if (rx_buffer == NULL) {
		cmn_err(CE_WARN, "\r\nNULL rx_buffer idx=%d", index);
		return (NULL);
	}
	vaddr = (char *)rx_buffer->dma_info.vaddr;
	if (vaddr == NULL) {
		cmn_err(CE_WARN, "\r\nNULL vaddr");
		return (NULL);
	}
	/* which sds ring processed this rx buffer */
	rx_buffer->sds_ring = sds_ring;

	atomic_inc_32(&rcv_desc->rx_desc_handled);
	atomic_dec_32(&rcv_desc->rx_buf_card);

	(void) ddi_dma_sync(rx_buffer->dma_info.dma_hdl,
	    rx_buffer->dma_info.offset,
	    pkt_length + poff + (adapter->ahw.cut_through ? 0 :
	    IP_ALIGNMENT_BYTES), DDI_DMA_SYNC_FORKERNEL);

	/*
	 * If card is running out of rx buffers, then attempt to allocate
	 * new mblk so we can feed this rx buffer back to card (we
	 * _could_ look at what's pending on free and recycle lists).
	 */
	if (rcv_desc->rx_buf_card < QLCNIC_RX_THRESHOLD) {
		docopy = 1;
		adapter->stats.rxbufshort++;
	}

	if (docopy == 1) {
		if ((mp = allocb(pkt_length + IP_ALIGNMENT_BYTES, 0)) == NULL) {
			adapter->stats.allocbfailed++;
			goto freebuf;
		}

		mp->b_rptr += IP_ALIGNMENT_BYTES;
		vaddr += poff;
		bcopy(vaddr, mp->b_rptr, pkt_length);
		adapter->stats.rxcopyed++;
		if (adapter->diag_test == QLCNIC_LOOPBACK_TEST) {
			if (!qlcnic_check_loopback_buff(mp->b_rptr)) {
				adapter->diag_cnt++;
				cmn_err(CE_NOTE, "diag_count is  : %d\n",
				    adapter->diag_cnt);
				mp->b_wptr = (uchar_t *)
				    ((unsigned long)mp->b_rptr + pkt_length);
				mp->b_next = mp->b_cont = NULL;
			}
			adapter->stats.no_rcv++;
			adapter->stats.rxbytes += pkt_length;
			adapter->stats.uphappy++;
			goto freebuf;
		}
		qlcnic_free_rx_buffer(sds_ring, rx_buffer);
	} else {
		mp = (mblk_t *)rx_buffer->mp;
		if (mp == NULL) {
			mp = desballoc(rx_buffer->dma_info.vaddr,
			    rcv_desc->dma_size, 0, &rx_buffer->rx_recycle);
			if (mp == NULL) {
				adapter->stats.desballocfailed++;
				goto freebuf;
			}
			rx_buffer->mp = mp;
		}
		atomic_inc_32(&rcv_desc->rx_buf_indicated);
		atomic_inc_32(&rx_buffer->ref_cnt);
		mp->b_rptr += poff;
		adapter->stats.rxmapped++;
	}

	mp->b_wptr = (uchar_t *)((unsigned long)mp->b_rptr + pkt_length);
	mp->b_next = mp->b_cont = NULL;

	cksum_flags = qlcnic_get_rx_cksum(adapter, sts_data0);
	mac_hcksum_set(mp, 0, 0, 0, 0, cksum_flags);
	adapter->stats.no_rcv++;
	adapter->stats.rxbytes += pkt_length;
	adapter->stats.uphappy++;

	return (mp);

freebuf:
	qlcnic_free_rx_buffer(sds_ring, rx_buffer);
	return (NULL);
}


/* Process Receive status ring */
static mblk_t *
qlcnic_process_rcv_ring(struct qlcnic_sds_ring_s *sds_ring,
    int poll_bytes, int poll_pkts)
{
	qlcnic_adapter *adapter = sds_ring->adapter;
	qlcnic_recv_context_t *recv_ctx = &(adapter->recv_ctx[0]);
	statusDesc_t *desc_head = sds_ring->rcvStatusDescHead;
	statusDesc_t *desc = NULL;
	uint32_t consumer, start;
	int count = 0, ring;
	mblk_t *mp, *first_mp = NULL, *last_mp = NULL;
	volatile uint64_t sts_data0;
	uint32_t pkt_length;
	uint32_t curr;
	uint32_t received_bytes = 0;
	uint32_t received_pkts = 0;

	start = consumer = sds_ring->statusRxConsumer;

	curr = start * sizeof (statusDesc_t);
	while (count < QLCNIC_RX_MAXBUFS) {
		desc = &desc_head[consumer];
		(void) ddi_dma_sync(sds_ring->status_desc_dma_handle, curr,
		    sizeof (statusDesc_t), DDI_DMA_SYNC_FORKERNEL);
		sts_data0 = LE_TO_HOST_64(desc->status_desc_data[0]);

		if (!(sts_data0 & STATUS_OWNER_HOST))
			break;

		/*
		 * The extra dma sync and check for length are
		 * required to make sure the
		 * descriptor is completely flushed.
		 */
		(void) ddi_dma_sync(sds_ring->status_desc_dma_handle, curr,
		    sizeof (statusDesc_t), DDI_DMA_SYNC_FORKERNEL);
		sts_data0 = LE_TO_HOST_64(desc->status_desc_data[0]);

		pkt_length = qlcnic_get_sts_totallength(sts_data0);
		if (!pkt_length) {
			cmn_err(CE_WARN, "qlcnic:%d Status_own updated"
			    "but pktlength is zero\n", adapter->instance);
			break;

		}
		if (((received_bytes + pkt_length) > poll_bytes) ||
		    (received_pkts >= poll_pkts))
			break;

		sds_ring->no_rcv++;
		sds_ring->rxbytes += pkt_length;
		received_bytes += pkt_length;
		received_pkts++;

		mp = qlcnic_process_rcv(adapter, sts_data0, sds_ring);
		QLCNIC_DUMP(DBG_DATA, "\t Mac data dump:\n",
		    (uint8_t *)mp->b_rptr, 8, pkt_length);

		/* owner is in first u64 data */
		desc->status_desc_data[0] =
		    HOST_TO_LE_64(STATUS_OWNER_PHANTOM);

		consumer = (consumer + 1) % sds_ring->max_status_desc_count;
		curr = consumer * sizeof (statusDesc_t);
		count++;
		if (mp != NULL) {
			if (first_mp) {
				last_mp->b_next = mp;
			} else {
				first_mp = mp;
			}
			last_mp = mp;
		}
	}

	if (qlcnic_check_dma_handle(adapter,
	    sds_ring->status_desc_dma_handle) != DDI_FM_OK) {
		cmn_err(CE_WARN, "Invalid dma handle, trying to"
		    " reset the firmware");
		qlcnic_force_fw_reset(adapter);
		return (NULL);
	}

	if (first_mp) {
		last_mp->b_next = NULL;
	}
	for (ring = 0; ring < adapter->max_rds_rings; ring++) {
		if (recv_ctx->rcv_desc[ring]->rx_desc_handled > 32) {
			if (mutex_tryenter(
			    &recv_ctx->rcv_desc[ring]->pool_lock[0])) {
				qlcnic_post_rx_buffers_nodb(adapter, ring);
				mutex_exit(
				    &recv_ctx->rcv_desc[ring]->pool_lock[0]);
			}
		}
	}

	if (count) {
		qlcnic_desc_dma_sync(sds_ring->status_desc_dma_handle, start,
		    count, sds_ring->max_status_desc_count,
		    STATUS_DESC_LEN,
		    DDI_DMA_SYNC_FORDEV);

		/* update the consumer index in phantom */
		sds_ring->statusRxConsumer = consumer;

		QLCNIC_PCI_WRITE_32(consumer,
		    (unsigned long)sds_ring->host_sds_consumer_addr);
	}

	return (first_mp);
}

/* Process Command status ring */
static void
qlcnic_process_cmd_ring(struct qlcnic_tx_ring_s *tx_ring)
{
	uint32_t last_consumer;
	uint32_t consumer;
	int count = 0;
	struct qlcnic_cmd_buffer *buffer;
	qlcnic_dmah_node_t *dmah, *head = NULL, *tail = NULL;
	uint32_t free_hdls = 0;
	boolean_t doresched = B_FALSE;
	struct qlcnic_adapter_s *adapter = tx_ring->adapter;
	qlcnic_buf_node_t *buf_head = NULL, *buf_tail = NULL;
	uint32_t free_bufs = 0;

	/* synchronize with command consumer index in card */
	(void) ddi_dma_sync(adapter->ctxDesc_dma_handle,
	    tx_ring->cmd_consumer_offset,
	    sizeof (uint32_t), DDI_DMA_SYNC_FORKERNEL);

	mutex_enter(&tx_ring->tx_lock);

	last_consumer = tx_ring->lastCmdConsumer;
	consumer = HOST_TO_LE_32(*tx_ring->cmdConsumer);

	if (last_consumer == consumer) {
		mutex_exit(&tx_ring->tx_lock);
		return;
	}

	while (last_consumer != consumer) {
		buffer = &tx_ring->cmd_buf_arr[last_consumer];
		/* recycle tx copy buffer */
		if (buffer->buf != NULL) {
			free_bufs ++;
			if (buf_head == NULL) {
				buf_head = buf_tail = buffer->buf;
			} else {
				buf_tail->next = buffer->buf;
				buf_tail = buffer->buf;
			}
			buffer->buf = NULL;
		}
		/* recycle tx dma handles */
		if (buffer->head != NULL) {
			dmah = buffer->head;
			while (dmah != NULL) {
				(void) ddi_dma_unbind_handle(dmah->dmahdl);
				dmah = dmah->next;
				free_hdls++;
			}

			if (head == NULL) {
				head = buffer->head;
				tail = buffer->tail;
			} else {
				tail->next = buffer->head;
				tail = buffer->tail;
			}

			buffer->head = NULL;
			buffer->tail = NULL;

			if (buffer->msg != NULL) {
				freemsg(buffer->msg);
				buffer->msg = NULL;
			}
		}

		last_consumer = get_next_index(last_consumer,
		    adapter->MaxTxDescCount);
		if (++count > QLCNIC_MAX_TXCOMPS)
			break;
	}

	if (count) {
		tx_ring->lastCmdConsumer = last_consumer;
		tx_ring->freecmds += count;

		doresched = tx_ring->resched_needed;
		if (doresched)
			tx_ring->resched_needed = 0;
	}

	mutex_exit(&tx_ring->tx_lock);

	if (head != NULL) {
		qlcnic_return_dma_handle(tx_ring, head);
	}
	if (buf_head != NULL) {
		qlcnic_return_tx_buf(tx_ring, buf_head);
	}
	if (doresched)
		RESUME_TX(tx_ring);
}

/*
 * This is invoked from receive isr, and at initialization time when no
 * rx buffers have been posted to card. Due to the single threaded nature
 * of the invocation, pool_lock acquisition is not neccesary to protect
 * pool_list.
 */
static qlcnic_rx_buffer_t *
qlcnic_reserve_rx_buffer(qlcnic_rcv_desc_ctx_t *rcv_desc,
    struct qlcnic_adapter_s *adapter)
{
	qlcnic_rx_buffer_t *rx_buffer = NULL;
	qlcnic_recv_context_t *recv_ctx;
	struct	qlcnic_sds_ring_s *sds_ring;
	int i;
	int rds_ring_id = rcv_desc->rds_index;
	qlcnic_rds_buf_recycle_list_t *rx_recycle_list;
	int head_index;

	recv_ctx = &adapter->recv_ctx[0];
	for (i = 0; i < adapter->max_sds_rings; i++) {
		sds_ring = &recv_ctx->sds_ring[i];
		rx_recycle_list =
		    &sds_ring->rds_buf_recycle_list[rds_ring_id];

		if (qlcnic_atomic_reserve(&rx_recycle_list->count, 1) >= 0) {
			head_index = rx_recycle_list->head_index;
			rx_buffer = rx_recycle_list->free_list[head_index];

			DPRINTF(DBG_RX, (CE_WARN,
			    "%s%d: getting buffer %p from index %d\n",
			    adapter->name, adapter->instance, rx_buffer,
			    head_index));
			if (!rx_buffer) {
				cmn_err(CE_WARN,
				    "%s%d: Error NULL buf at head index %d\n",
				    adapter->name, adapter->instance,
				    head_index);
			} else {
				/* ASSERT(rx_buffer != NULL); */
				rx_recycle_list->free_list[head_index] = NULL;
				rx_recycle_list->head_index = get_next_index(
				    head_index,
				    rx_recycle_list->max_free_entries);
				break;
			}
		}
	}

	return (rx_buffer);
}

static int
qlcnic_post_rx_buffers(struct qlcnic_adapter_s *adapter, uint32_t ringid)
{
	qlcnic_recv_context_t *recv_ctx = &(adapter->recv_ctx[0]);
	qlcnic_rcv_desc_ctx_t *rcv_desc = recv_ctx->rcv_desc[ringid];
	qlcnic_rx_buffer_t *rx_buffer;
	rcvDesc_t *pdesc;
	int count;

	for (count = 0; count < rcv_desc->MaxRxDescCount; count++) {
		rx_buffer = &rcv_desc->rx_buf_pool[count];

		pdesc = &rcv_desc->desc_head[count];
		pdesc->referenceHandle =
		    HOST_TO_LE_16(rxbuf2index(rcv_desc, rx_buffer));
		pdesc->flags = HOST_TO_LE_16(ringid);
		pdesc->bufferLength = HOST_TO_LE_32(rcv_desc->dma_size);
		pdesc->AddrBuffer =
		    HOST_TO_LE_64(rx_buffer->dma_info.dma_addr);
	}

	rcv_desc->producer = count % rcv_desc->MaxRxDescCount;
	count--;
	qlcnic_desc_dma_sync(rcv_desc->rx_desc_dma_handle,
	    0,		/* start */
	    count,	/* count */
	    count,	/* range */
	    sizeof (rcvDesc_t),	/* unit_size */
	    DDI_DMA_SYNC_FORDEV);	/* direction */

	rcv_desc->rx_buf_card = rcv_desc->MaxRxDescCount;
	QLCNIC_PCI_WRITE_32(count,
	    (unsigned long)rcv_desc->host_rx_producer_addr);
	return (DDI_SUCCESS);
}

static void
qlcnic_post_rx_buffers_nodb(struct qlcnic_adapter_s *adapter,
    uint32_t ringid)
{
	qlcnic_recv_context_t *recv_ctx = &(adapter->recv_ctx[0]);
	qlcnic_rcv_desc_ctx_t *rcv_desc = recv_ctx->rcv_desc[ringid];
	struct qlcnic_rx_buffer *rx_buffer;
	rcvDesc_t *pdesc;
	int count, producer = rcv_desc->producer;
	int last_producer = producer;
	uint32_t rx_desc_handled;

	rx_desc_handled = rcv_desc->rx_desc_handled;
	for (count = 0; count < rx_desc_handled; count++) {
		rx_buffer = qlcnic_reserve_rx_buffer(rcv_desc, adapter);
		if (rx_buffer != NULL) {
			pdesc = &rcv_desc->desc_head[producer];
			pdesc->referenceHandle =
			    HOST_TO_LE_16(rxbuf2index(rcv_desc, rx_buffer));
			pdesc->flags = HOST_TO_LE_16(ringid);
			pdesc->bufferLength = HOST_TO_LE_32(rcv_desc->dma_size);
			pdesc->AddrBuffer =
			    HOST_TO_LE_64(rx_buffer->dma_info.dma_addr);
			DPRINTF(DBG_RX, (CE_WARN, "%s%d: arming %p\n",
			    adapter->name, adapter->instance,
			    rx_buffer));
		} else {
			adapter->stats.outofrxbuf ++;
			break;
		}
		producer = get_next_index(producer, rcv_desc->MaxRxDescCount);
	}

	/* if we did allocate buffers, then write the count to Phantom */
	if (count) {
		/* Sync rx ring, considering case for wrap around */
		qlcnic_desc_dma_sync(rcv_desc->rx_desc_dma_handle,
		    last_producer,
		    count, rcv_desc->MaxRxDescCount, sizeof (rcvDesc_t),
		    DDI_DMA_SYNC_FORDEV);

		rcv_desc->producer = producer;
		atomic_add_32(&rcv_desc->rx_desc_handled,  -1*count);
		atomic_add_32(&rcv_desc->rx_buf_card, count);

		producer = (producer - 1) % rcv_desc->MaxRxDescCount;
		QLCNIC_PCI_WRITE_32(producer,
		    (unsigned long)rcv_desc->host_rx_producer_addr);
	}
}

int
qlcnic_fill_statistics_128M(struct qlcnic_adapter_s *adapter,
    struct qlcnic_statistics *qlcnic_stats)
{
	void *addr;
	if (adapter->ahw.board_type == QLCNIC_XGBE) {
		QLCNIC_WRITE_LOCK(&adapter->adapter_lock);
		qlcnic_pci_change_crbwindow_128M(adapter, 0);

		QLCNIC_LOCKED_READ_REG(QLCNIC_NIU_XGE_TX_BYTE_CNT,
		    &(qlcnic_stats->tx_bytes));
		QLCNIC_LOCKED_READ_REG(QLCNIC_NIU_XGE_TX_FRAME_CNT,
		    &(qlcnic_stats->tx_packets));
		QLCNIC_LOCKED_READ_REG(QLCNIC_NIU_XGE_RX_BYTE_CNT,
		    &(qlcnic_stats->rx_bytes));
		QLCNIC_LOCKED_READ_REG(QLCNIC_NIU_XGE_RX_FRAME_CNT,
		    &(qlcnic_stats->rx_packets));
		QLCNIC_LOCKED_READ_REG(QLCNIC_NIU_XGE_AGGR_ERROR_CNT,
		    &(qlcnic_stats->rx_errors));
		QLCNIC_LOCKED_READ_REG(QLCNIC_NIU_XGE_CRC_ERROR_CNT,
		    &(qlcnic_stats->rx_CRC_errors));
		QLCNIC_LOCKED_READ_REG(QLCNIC_NIU_XGE_OVERSIZE_FRAME_ERR,
		    &(qlcnic_stats->rx_long_length_error));
		QLCNIC_LOCKED_READ_REG(QLCNIC_NIU_XGE_UNDERSIZE_FRAME_ERR,
		    &(qlcnic_stats->rx_short_length_error));

		/*
		 * For reading rx_MAC_error bit different procedure
		 * QLCNIC_LOCKED_WRITE_REG(QLCNIC_NIU_TEST_MUX_CTL, 0x15);
		 * QLCNIC_LOCKED_READ_REG((QLCNIC_CRB_NIU + 0xC0), &temp);
		 * qlcnic_stats->rx_MAC_errors = temp & 0xff;
		 */

		qlcnic_pci_change_crbwindow_128M(adapter, QLCNIC_WINDOW_ONE);
		QLCNIC_WRITE_UNLOCK(&adapter->adapter_lock);
	} else {
		qlcnic_stats->rx_bytes = adapter->stats.rxbytes;
		qlcnic_stats->rx_packets = adapter->stats.no_rcv;
		qlcnic_stats->rx_errors = adapter->stats.rcvdbadmsg;
		qlcnic_stats->rx_short_length_error = adapter->stats.uplcong;
		qlcnic_stats->rx_long_length_error = adapter->stats.uphcong;
		qlcnic_stats->rx_CRC_errors = 0;
		qlcnic_stats->rx_MAC_errors = 0;
	}
	return (0);
}

int
qlcnic_fill_statistics_2M(struct qlcnic_adapter_s *adapter,
    struct qlcnic_statistics *qlcnic_stats)
{
	if (adapter->ahw.board_type == QLCNIC_XGBE) {
		(void) qlcnic_hw_read_wx_2M(adapter, QLCNIC_NIU_XGE_TX_BYTE_CNT,
		    &(qlcnic_stats->tx_bytes), 4);
		(void) qlcnic_hw_read_wx_2M(adapter,
		    QLCNIC_NIU_XGE_TX_FRAME_CNT,
		    &(qlcnic_stats->tx_packets), 4);
		(void) qlcnic_hw_read_wx_2M(adapter, QLCNIC_NIU_XGE_RX_BYTE_CNT,
		    &(qlcnic_stats->rx_bytes), 4);
		(void) qlcnic_hw_read_wx_2M(adapter,
		    QLCNIC_NIU_XGE_RX_FRAME_CNT,
		    &(qlcnic_stats->rx_packets), 4);
		(void) qlcnic_hw_read_wx_2M(adapter,
		    QLCNIC_NIU_XGE_AGGR_ERROR_CNT,
		    &(qlcnic_stats->rx_errors),
		    4);
		(void) qlcnic_hw_read_wx_2M(adapter,
		    QLCNIC_NIU_XGE_CRC_ERROR_CNT,
		    &(qlcnic_stats->rx_CRC_errors), 4);
		(void) qlcnic_hw_read_wx_2M(adapter,
		    QLCNIC_NIU_XGE_OVERSIZE_FRAME_ERR,
		    &(qlcnic_stats->rx_long_length_error), 4);
		(void) qlcnic_hw_read_wx_2M(adapter,
		    QLCNIC_NIU_XGE_UNDERSIZE_FRAME_ERR,
		    &(qlcnic_stats->rx_short_length_error), 4);
	} else {
		qlcnic_stats->rx_bytes = adapter->stats.rxbytes;
		qlcnic_stats->rx_packets = adapter->stats.no_rcv;
		qlcnic_stats->rx_errors = adapter->stats.rcvdbadmsg;
		qlcnic_stats->rx_short_length_error = adapter->stats.uplcong;
		qlcnic_stats->rx_long_length_error = adapter->stats.uphcong;
		qlcnic_stats->rx_CRC_errors = 0;
		qlcnic_stats->rx_MAC_errors = 0;
	}
	return (0);
}

int
qlcnic_clear_statistics_128M(struct qlcnic_adapter_s *adapter)
{
	void *addr;
	int data = 0;

	QLCNIC_WRITE_LOCK(&adapter->adapter_lock);
	qlcnic_pci_change_crbwindow_128M(adapter, 0);

	QLCNIC_LOCKED_WRITE_REG(QLCNIC_NIU_XGE_TX_BYTE_CNT, &data);
	QLCNIC_LOCKED_WRITE_REG(QLCNIC_NIU_XGE_TX_FRAME_CNT, &data);
	QLCNIC_LOCKED_WRITE_REG(QLCNIC_NIU_XGE_RX_BYTE_CNT, &data);
	QLCNIC_LOCKED_WRITE_REG(QLCNIC_NIU_XGE_RX_FRAME_CNT, &data);
	QLCNIC_LOCKED_WRITE_REG(QLCNIC_NIU_XGE_AGGR_ERROR_CNT, &data);
	QLCNIC_LOCKED_WRITE_REG(QLCNIC_NIU_XGE_CRC_ERROR_CNT, &data);
	QLCNIC_LOCKED_WRITE_REG(QLCNIC_NIU_XGE_OVERSIZE_FRAME_ERR, &data);
	QLCNIC_LOCKED_WRITE_REG(QLCNIC_NIU_XGE_UNDERSIZE_FRAME_ERR, &data);

	qlcnic_pci_change_crbwindow_128M(adapter, QLCNIC_WINDOW_ONE);
	QLCNIC_WRITE_UNLOCK(&adapter->adapter_lock);
	qlcnic_clear_stats(adapter);
	return (0);
}

int
qlcnic_clear_statistics_2M(struct qlcnic_adapter_s *adapter)
{
	int data = 0;

	(void) qlcnic_hw_write_wx_2M(adapter, QLCNIC_NIU_XGE_TX_BYTE_CNT,
	    &data, 4);
	(void) qlcnic_hw_write_wx_2M(adapter, QLCNIC_NIU_XGE_TX_FRAME_CNT,
	    &data, 4);
	(void) qlcnic_hw_write_wx_2M(adapter, QLCNIC_NIU_XGE_RX_BYTE_CNT,
	    &data, 4);
	(void) qlcnic_hw_write_wx_2M(adapter, QLCNIC_NIU_XGE_RX_FRAME_CNT,
	    &data, 4);
	(void) qlcnic_hw_write_wx_2M(adapter, QLCNIC_NIU_XGE_AGGR_ERROR_CNT,
	    &data, 4);
	(void) qlcnic_hw_write_wx_2M(adapter, QLCNIC_NIU_XGE_CRC_ERROR_CNT,
	    &data, 4);
	(void) qlcnic_hw_write_wx_2M(adapter, QLCNIC_NIU_XGE_OVERSIZE_FRAME_ERR,
	    &data, 4);
	(void) qlcnic_hw_write_wx_2M(adapter,
	    QLCNIC_NIU_XGE_UNDERSIZE_FRAME_ERR,
	    &data, 4);
	qlcnic_clear_stats(adapter);
	return (0);
}

int
qlcnic_get_deviceinfo_2M(struct qlcnic_adapter_s *adapter,
    struct qlcnic_devinfo *qlcnic_devinfo)
{
	int ret;
	uint_t noptions;
	int *reg_options;
	uchar_t bus, dev, func;

	bzero(qlcnic_devinfo->link_speed, 10);
	bzero(qlcnic_devinfo->link_status, 10);
	bzero(qlcnic_devinfo->link_duplex, 20);
	qlcnic_devinfo->name = ddi_driver_name(adapter->dip);
	qlcnic_devinfo->instance = adapter->instance;
	switch (adapter->ahw.linkup) {
	case 0:
		(void) memcpy(qlcnic_devinfo->link_status, "LINK_DOWN", 10);
		break;
	case 1:
		(void) memcpy(qlcnic_devinfo->link_status, "LINK_UP", 10);
		break;
	default:
		(void) memcpy(qlcnic_devinfo->link_status, "UNKNOWN", 10);
		break;
	}
	qlcnic_devinfo->mtu = adapter->mtu;
	qlcnic_devinfo->max_sds_rings = adapter->max_sds_rings;
	qlcnic_devinfo->MaxTxDescCount = adapter->MaxTxDescCount;
	qlcnic_devinfo->MaxRxDescCount = adapter->MaxRxDescCount;
	qlcnic_devinfo->MaxJumboRxDescCount = adapter->MaxJumboRxDescCount;
	qlcnic_devinfo->tx_recycle_threshold = adapter->tx_recycle_threshold;
	qlcnic_devinfo->max_tx_dma_hdls = adapter->max_tx_dma_hdls;
	qlcnic_devinfo->MaxStatusDescCount =
	    adapter->MaxStatusDescCount;
	qlcnic_devinfo->rx_bcopy_threshold =
	    adapter->rx_bcopy_threshold;
	qlcnic_devinfo->tx_bcopy_threshold =
	    adapter->tx_bcopy_threshold;
	qlcnic_devinfo->lso_enable =
	    (adapter->flags & QLCNIC_LSO_ENABLED) ? 1 : 0;

	switch (adapter->link_speed) {
	case 10:
		(void) memcpy(qlcnic_devinfo->link_speed, "10MBPS", 10);
		break;
	case 100:
		(void) memcpy(qlcnic_devinfo->link_speed, "100MBPS", 10);
		break;
	case 1000:
		(void) memcpy(qlcnic_devinfo->link_speed, "1000MBPS", 10);
		break;
	case 10000:
		(void) memcpy(qlcnic_devinfo->link_speed, "10GBPS", 10);
		break;
	default:
		(void) memcpy(qlcnic_devinfo->link_speed, "UNKNOWN", 10);
		break;
	}
	switch (adapter->link_duplex) {
	case 1:
		(void) memcpy(qlcnic_devinfo->link_duplex, "DUPLEX_HALF", 20);
		break;
	case 2:
		(void) memcpy(qlcnic_devinfo->link_duplex, "DUPLEX_FULL", 20);
		break;
	default:
		(void) memcpy(qlcnic_devinfo->link_duplex, "DUPLEX_UNKNOWN",
		    20);
		break;
	}

	/* Get  bus/device/func number */
	ret = ddi_prop_lookup_int_array(DDI_DEV_T_ANY,
	    adapter->dip, 0, "reg", &reg_options, &noptions);
	if (ret != DDI_PROP_SUCCESS) {
		cmn_err(CE_WARN, "%s%d: Could not determine reg property",
		    adapter->name, adapter->instance);
		return (DDI_FAILURE);
	}
	func = (uchar_t)PCI_REG_FUNC_G(reg_options[0]);
	dev = (uchar_t)PCI_REG_DEV_G(reg_options[0]);
	bus = (uchar_t)PCI_REG_BUS_G(reg_options[0]);
	(void) snprintf(qlcnic_devinfo->bdf, 8, "%x:%x:%x", bus, dev, func);

	cmn_err(CE_NOTE, "Device  BDF                  : %s\n",
	    qlcnic_devinfo->bdf);
	return (0);
}


static void
qlcnic_create_loopback_buff(unsigned char *data)
{
	unsigned char random_data[] = {0x08, 0x06, 0x45, 0x00};
	(void) memset(data, 0x4e, QLC_ILB_PKT_SIZE);
	(void) memset(data, 0xff, 12);
	(void) memcpy(data + 12, random_data, sizeof (random_data));
}

int
qlcnic_check_loopback_buff(unsigned char *data)
{
	unsigned char buff[QLC_ILB_PKT_SIZE];
	qlcnic_create_loopback_buff(buff);
	return (memcmp(data, buff, QLC_ILB_PKT_SIZE));
}

static void
qlcnic_tx_offload_check_diag(cmdDescType0_t *desc)
{
	uint16_t opcode = TX_ETHER_PKT;
	uint16_t flags = 0;

	qlcnic_set_tx_flags_opcode(desc, flags, opcode);
}

static boolean_t
qlcnic_send_copy_diag(struct qlcnic_tx_ring_s *tx_ring, mblk_t *mp,
    pktinfo_t *pktinfo)
{
	struct qlcnic_adapter_s *adapter = tx_ring->adapter;
	uint32_t producer, saved_producer;
	cmdDescType0_t *hwdesc, *swdesc;
	struct qlcnic_cmd_buffer *pbuf = NULL;
	uint32_t mblen;
	int no_of_desc = 1;
	int MaxTxDescCount = adapter->MaxTxDescCount;
	mblk_t *bp;
	uint8_t *txb;
	uint8_t total_hdr_len = 0;
	boolean_t use_lso = B_FALSE;
	uint8_t desc_size = TX_DESC_LEN;
	cmdDescType0_t temp_desc;
	qlcnic_buf_node_t *tx_buffer = qlcnic_reserve_tx_buf(tx_ring);

	swdesc = &temp_desc;
	bzero(&temp_desc, desc_size);

	if (tx_buffer == NULL) {
		tx_ring->stats.outoftxbuffer++;
		return (B_FALSE);
	}

	txb = tx_buffer->dma_area.vaddr;

	for (bp = mp; bp != NULL; bp = bp->b_cont) {
		if ((mblen = MBLKL(bp)) == 0)
			continue;
		bcopy(bp->b_rptr, txb, mblen);
		txb += mblen;
	}

	(void) ddi_dma_sync(tx_buffer->dma_area.dma_hdl,
	    tx_buffer->dma_area.offset, pktinfo->total_len,
	    DDI_DMA_SYNC_FORDEV);

	qlcnic_tx_offload_check_diag(swdesc); /* update u1 only */
	qlcnic_set_tx_frags_len(swdesc, no_of_desc, pktinfo->total_len);
	qlcnic_set_tx_port(swdesc, adapter->portnum);
	swdesc->buffer_length[0] = HOST_TO_LE_16(pktinfo->total_len);
	swdesc->addr_buffer1 = HOST_TO_LE_64(tx_buffer->dma_area.dma_addr);

	mutex_enter(&tx_ring->tx_lock);

	if (find_diff_among(tx_ring->cmdProducer, tx_ring->lastCmdConsumer,
	    MaxTxDescCount) <= 4) {
		tx_ring->stats.outofcmddesc++;
		tx_ring->resched_needed = 1;
		mutex_exit(&tx_ring->tx_lock);
		qlcnic_return_tx_buf(tx_ring, tx_buffer);
		return (B_FALSE);
	}

	saved_producer = producer = tx_ring->cmdProducer;
	hwdesc = &tx_ring->cmdDescHead[producer];
	bcopy(swdesc, hwdesc, desc_size);

	pbuf = &tx_ring->cmd_buf_arr[producer];
	pbuf->msg = NULL;
	pbuf->head = NULL;
	pbuf->tail = NULL;
	pbuf->buf = tx_buffer;

	/* copy MAC/IP/TCP headers to following descriptors */
	producer = get_next_index(producer, MaxTxDescCount);
	if (use_lso) {
		txb = tx_buffer->dma_area.vaddr;
		qlcnic_copy_pkt_hdrs_to_desc(tx_ring, producer, txb,
		    total_hdr_len);
	}

	qlcnic_desc_dma_sync(tx_ring->cmd_desc_dma_handle,
	    saved_producer,
	    no_of_desc,
	    MaxTxDescCount,
	    TX_DESC_LEN,
	    DDI_DMA_SYNC_FORDEV);

	/* increment to next produceer */
	tx_ring->cmdProducer =
	    get_index_range(saved_producer, MaxTxDescCount, no_of_desc);

	qlcnic_update_cmd_producer(tx_ring, tx_ring->cmdProducer);

	tx_ring->stats.txbytes += pktinfo->total_len;
	tx_ring->stats.xmitfinished++;
	tx_ring->stats.txcopyed++;

	tx_ring->freecmds -= no_of_desc;

	mutex_exit(&tx_ring->tx_lock);

	freemsg(mp);

	return (B_TRUE);

}


static int
qlcnic_get_pkt_info_diag(mblk_t *mp, pktinfo_t *pktinfo)
{
	mblk_t *bp;
	uint32_t size;
	boolean_t ret;

	for (bp = mp; bp != NULL; bp = bp->b_cont) {
		if (MBLKL(bp) == 0)
			continue;
		size = MBLKL(bp);
		pktinfo->total_len += size;
		pktinfo->mblk_no++;
	}
	/*
	 * We just need non 1 byte aligned address, since ether_type is
	 * ushort.
	 */
	if ((uintptr_t)mp->b_rptr & 1) {
		cmn_err(CE_WARN, "qlcnic_get_pkt_info: b_rptr addr not even");
		return (B_FALSE);
	}
	pktinfo->mac_hlen = sizeof (struct ether_header);
	ret = B_TRUE;
exit:
	return (ret);

}

static boolean_t
qlcnic_xmit_frame_diag(struct qlcnic_tx_ring_s *tx_ring, mblk_t *mp)
{
	pktinfo_t pktinfo;
	boolean_t status = B_FALSE;

	tx_ring->stats.xmitcalled++;

	{
		uint32_t temp = 1;

		temp = atomic_swap_32(&tx_ring->tx_comp, temp);
		if (temp == 0) {
			qlcnic_process_cmd_ring(tx_ring);
			temp = atomic_swap_32(&tx_ring->tx_comp, temp);
		}
	}
	(void) memset(&pktinfo, 0, sizeof (pktinfo_t));

	(void) qlcnic_get_pkt_info_diag(mp, &pktinfo);

	status = qlcnic_send_copy_diag(tx_ring, mp, &pktinfo);
	return (status);

}

static int qlcnic_do_ilb_test(qlcnic_adapter *adapter)
{
	struct qlcnic_tx_ring_s *tx_ring = &(adapter->tx_ring[0]);
	mblk_t *mp;
	int i;

	adapter->diag_cnt = 0;
	for (i = 0; i < 16; i++) {
		mp = allocb(QLC_ILB_PKT_SIZE, 0);
		if (mp == NULL) {
			cmn_err(CE_NOTE, "allocb failed \n");
			return (-1);
		}
		qlcnic_create_loopback_buff(mp->b_rptr);
		mp->b_wptr += QLC_ILB_PKT_SIZE;

		if (qlcnic_xmit_frame_diag(tx_ring, mp) != B_TRUE) {
			cmn_err(CE_WARN,
			    "qlcnic(%d) xmit_frame failed for %dth pkt\n",
			    adapter->instance, i);
			return (-1);
		}

		qlcnic_delay(50);
		{
			uint32_t temp = 1;

			for (int i = 0; i < adapter->max_tx_rings; i ++) {
				tx_ring = &(adapter->tx_ring[i]);

				temp = atomic_swap_32(&tx_ring->tx_comp, temp);
				if (temp == 0) {
					qlcnic_process_cmd_ring(tx_ring);
					(void) atomic_swap_32(&tx_ring->tx_comp,
					    temp);
				}
			}
		}

		if (!adapter->diag_cnt) {
			return (-1);
		}
	}
	cmn_err(CE_NOTE, "%s%d: Loopback test diag_cnt is : %d\n",
	    adapter->name, adapter->instance, adapter->diag_cnt);

	return (0);
}

static int qlcnic_loopback_test(qlcnic_adapter *adapter)
{
	struct qlcnic_tx_ring_s *tx_ring;
	int ret = 0, temp_state;

	if (adapter->is_up != QLCNIC_ADAPTER_UP_MAGIC) {
		cmn_err(CE_WARN, "device is not in UP state: %x\n",
		    adapter->is_up);
		return (-1);
	}
	temp_state = adapter->ahw.linkup;
	/* send a fake LINK_DOWN to proceed with loopback test */
	mac_link_update(adapter->mach, LINK_STATE_DOWN);

	adapter->diag_test = QLCNIC_LOOPBACK_TEST;
	{
		uint32_t temp = 1;

		for (int i = 0; i < adapter->max_tx_rings; i ++) {
			tx_ring = &(adapter->tx_ring[i]);

			temp = atomic_swap_32(&tx_ring->tx_comp, temp);
			if (temp == 0) {
				qlcnic_process_cmd_ring(tx_ring);
				temp = atomic_swap_32(&tx_ring->tx_comp, temp);
			}
		}
		cmn_err(CE_NOTE, "!tx drain done \n");
	}

	ret = qlcnic_set_ilb_mode(adapter);
	if (ret)
		goto clear_it;

	ret = qlcnic_do_ilb_test(adapter);

	qlcnic_clear_ilb_mode(adapter);

clear_it:
	adapter->diag_test = 0;
	adapter->diag_cnt = 0;
	mac_link_update(adapter->mach, temp_state);
	return (ret);
}


static int qlcnic_irq_test(qlcnic_adapter *adapter)
{
	int ret, temp_state;
	struct qlcnic_tx_ring_s *tx_ring;

	if (adapter->is_up != QLCNIC_ADAPTER_UP_MAGIC) {
		cmn_err(CE_WARN, "device is not in UP state: %x\n",
		    adapter->is_up);
		return (-1);
	}

	adapter->diag_test = QLCNIC_INTERRUPT_TEST;
	temp_state = adapter->ahw.linkup;
	/* send a fake LINK_DOWN to proceed with loopback test */
	mac_link_update(adapter->mach, LINK_STATE_DOWN);
	{
		uint32_t temp = 1;

		for (int i = 0; i < adapter->max_tx_rings; i ++) {
			tx_ring = &(adapter->tx_ring[i]);

			temp = atomic_swap_32(&tx_ring->tx_comp, temp);
			if (temp == 0) {
				qlcnic_process_cmd_ring(tx_ring);
				temp = atomic_swap_32(&tx_ring->tx_comp, temp);
			}
		}
	}

	adapter->diag_cnt = 0;
	ret = qlcnic_issue_cmd(adapter, adapter->ahw.pci_func,
	    NXHAL_VERSION, adapter->portnum,
	    0, 0, 0x00000011);

	if (ret)
		goto clear_it;

	qlcnic_delay(100);

	cmn_err(CE_NOTE, "%s%d: Interrupt count is : %d\n",
	    adapter->name, adapter->instance, adapter->diag_cnt);

	ret = !adapter->diag_cnt;

clear_it:
	mac_link_update(adapter->mach, temp_state);
	adapter->diag_test = 0;
	adapter->diag_cnt = 0;
	return (ret);
}

/*
 * qlcnic_ioctl ()    We provide the tcl/phanmon support
 * through these ioctls.
 */
static void
qlcnic_ioctl(struct qlcnic_adapter_s *adapter, int cmd, queue_t *q, mblk_t *mp)
{
	void *ptr;

	switch (cmd) {
	case QLCNIC_CMD:
		(void) qlcnic_do_ioctl(adapter, q, mp);
		break;

	case QLCNIC_NAME:
		ptr = (void *) mp->b_cont->b_rptr;

		/*
		 * Phanmon checks for "UNM-UNM" string
		 * Replace the hardcoded value with appropriate macro
		 */
		(void) memcpy(ptr, "UNM-UNM", 10);
		miocack(q, mp, 10, 0);
		break;

	default:
		cmn_err(CE_WARN, "qlcnic ioctl cmd %x not supported", cmd);

		miocnak(q, mp, 0, EINVAL);
		break;
	}
}

/*
 * Loopback ioctl code
 */
static lb_property_t loopmodes[] = {
	{ normal,	"normal",	QLCNIC_LOOP_NONE		},
	{ internal,	"internal",	QLCNIC_LOOP_INTERNAL		},
};

/*
 * Set Loopback mode
 */
static enum ioc_reply
qlcnic_set_loop_mode(qlcnic_adapter *adapter, uint32_t mode)
{
	/*
	 * If the mode is same as current mode ...
	 */
	if (mode == adapter->loop_back_mode)
		return (IOC_ACK);

	switch (mode) {
	default:
		return (IOC_INVAL);

	case QLCNIC_LOOP_NONE:
		qlcnic_clear_ilb_mode(adapter);
		break;
	case QLCNIC_LOOP_INTERNAL:
		(void) qlcnic_set_ilb_mode(adapter);
		break;
	}

	return (IOC_REPLY);
}
/*
 * Loopback ioctl
 */
/* ARGSUSED */
enum ioc_reply
qlcnic_loop_ioctl(qlcnic_adapter *adapter, queue_t *wq, mblk_t *mp,
    struct iocblk *iocp)
{
	lb_info_sz_t *lbsp;
	lb_property_t *lbpp;
	uint32_t *lbmp;
	int cmd;

	_NOTE(ARGUNUSED(wq))
	/*
	 * Validate format of ioctl
	 */
	if (mp->b_cont == NULL)
		return (IOC_INVAL);

	cmd = iocp->ioc_cmd;
	switch (cmd) {
	default:
		/* NOTREACHED */
		return (IOC_INVAL);

	case LB_GET_INFO_SIZE:
		if (iocp->ioc_count != sizeof (lb_info_sz_t))
			return (IOC_INVAL);
		lbsp = (void *)mp->b_cont->b_rptr;
		*lbsp = sizeof (loopmodes);
		return (IOC_REPLY);

	case LB_GET_INFO:
		if (iocp->ioc_count != sizeof (loopmodes))
			return (IOC_INVAL);
		lbpp = (void *)mp->b_cont->b_rptr;
		bcopy(loopmodes, lbpp, sizeof (loopmodes));
		return (IOC_REPLY);

	case LB_GET_MODE:
		if (iocp->ioc_count != sizeof (uint32_t))
			return (IOC_INVAL);
		lbmp = (void *)mp->b_cont->b_rptr;
		*lbmp = adapter->loop_back_mode;
		return (IOC_REPLY);

	case LB_SET_MODE:
		if (iocp->ioc_count != sizeof (uint32_t))
			return (IOC_INVAL);
		lbmp = (void *)mp->b_cont->b_rptr;
		return (qlcnic_set_loop_mode(adapter, *lbmp));
	}
}

static int
qlcnic_do_ioctl(qlcnic_adapter *adapter, queue_t *wq, mblk_t *mp)
{
	qlcnic_ioctl_data_t data;
	struct qlcnic_ioctl_data *up_data;
	ddi_acc_handle_t conf_handle;
	int retval = 0;
	uint64_t efuse_chip_id = 0;
	char *ptr1;
	short *ptr2;
	int *ptr4;
	int ret;

	up_data = (struct qlcnic_ioctl_data *)(mp->b_cont->b_rptr);
	(void) memcpy(&data, (void **)(uintptr_t)(mp->b_cont->b_rptr),
	    sizeof (data));

	/* Shouldn't access beyond legal limits of  "char u[64];" member */
	if (data.size > sizeof (data.uabc)) {
		/* evil user tried to crash the kernel */
		cmn_err(CE_WARN, "bad size: %d", data.size);
		retval = EINVAL;
		goto error_out;
	}

	switch (data.cmd) {
	case qlcnic_cmd_pci_read:

		if ((retval = adapter->qlcnic_hw_read_ioctl(adapter,
		    data.off, up_data, data.size))) {
			retval = data.rv;
			goto error_out;
		}

		data.rv = 0;
		break;

	case qlcnic_cmd_pci_write:
		if ((data.rv = adapter->qlcnic_hw_write_ioctl(adapter,
		    data.off, &(data.uabc), data.size))) {
			retval = data.rv;
			goto error_out;
		}
		data.size = 0;
		break;

	case qlcnic_cmd_pci_mem_read:
		if ((data.rv = adapter->qlcnic_pci_mem_read(adapter,
		    data.off, (u64 *)up_data))) {
			retval = data.rv;
			goto error_out;
		}
		data.rv = 0;
		break;

	case qlcnic_cmd_pci_mem_write:
		if ((data.rv = adapter->qlcnic_pci_mem_write(adapter,
		    data.off, (uint64_t)(uptr_t)data.uabc))) {
			retval = data.rv;
			goto error_out;
		}

		data.size = 0;
		data.rv = 0;
		break;

	case qlcnic_cmd_pci_config_read:

		if (adapter->pci_cfg_handle != NULL) {
			conf_handle = adapter->pci_cfg_handle;

		} else if ((retval = pci_config_setup(adapter->dip,
		    &conf_handle)) != DDI_SUCCESS) {
			goto error_out;
		} else
			adapter->pci_cfg_handle = conf_handle;

		switch (data.size) {
		case 1:
			ptr1 = (char *)up_data;
			*ptr1 = (char)pci_config_get8(conf_handle, data.off);
			break;
		case 2:
			ptr2 = (short *)up_data;
			*ptr2 = (short)pci_config_get16(conf_handle, data.off);
			break;
		case 4:
			ptr4 = (int *)up_data;
			*ptr4 = (int)pci_config_get32(conf_handle, data.off);
			break;
		}

		break;

	case qlcnic_cmd_pci_config_write:

		if (adapter->pci_cfg_handle != NULL) {
			conf_handle = adapter->pci_cfg_handle;
		} else if ((retval = pci_config_setup(adapter->dip,
		    &conf_handle)) != DDI_SUCCESS) {
			goto error_out;
		} else {
			adapter->pci_cfg_handle = conf_handle;
		}

		switch (data.size) {
		case 1:
			pci_config_put8(conf_handle,
			    data.off, *(char *)&(data.uabc));
			break;
		case 2:
			pci_config_put16(conf_handle,
			    data.off, *(short *)(uintptr_t)&(data.uabc));
			break;
		case 4:
			pci_config_put32(conf_handle,
			    data.off, *(uint32_t *)(uintptr_t)&(data.uabc));
			break;
		}
		data.size = 0;
		break;

	case qlcnic_cmd_get_stats:
		data.rv = adapter->qlcnic_fill_statistics(adapter,
		    (struct qlcnic_statistics *)up_data);
		data.size = sizeof (struct qlcnic_statistics);

		break;

	case qlcnic_cmd_clear_stats:
		data.rv = adapter->qlcnic_clear_statistics(adapter);
		break;

	case qlcnic_cmd_get_version:
		(void) memcpy(up_data, QLCNIC_VERSIONID,
		    sizeof (QLCNIC_VERSIONID));
		data.size = sizeof (QLCNIC_VERSIONID);

		break;

	case qlcnic_cmd_get_phy_type:
		cmn_err(CE_WARN, "qlcnic_cmd_get_phy_type unimplemented");
		break;

	case qlcnic_cmd_efuse_chip_id:
		efuse_chip_id = adapter->qlcnic_pci_read_normalize(adapter,
		    QLCNIC_EFUSE_CHIP_ID_HIGH);
		efuse_chip_id <<= 32;
		efuse_chip_id |= adapter->qlcnic_pci_read_normalize(adapter,
		    QLCNIC_EFUSE_CHIP_ID_LOW);
		(void) memcpy(up_data, &efuse_chip_id, sizeof (uint64_t));
		data.rv = 0;
		break;
	case qlcnic_cmd_get_port_num:
		(void) memcpy(up_data, &(adapter->portnum), sizeof (uint16_t));
		data.rv = 0;
		break;
	case qlcnic_cmd_get_pci_func_num:
		(void) memcpy(up_data, &(adapter->ahw.pci_func), sizeof (int));
		data.rv = 0;
		break;

	case qlcnic_cmd_port_led_blink:
		ret = qlcnic_config_led_blink(adapter, 1);
		(void) memcpy(up_data, &ret, sizeof (int));
		/* wait for few secs for  LED to blink */
		qlcnic_delay(30000);
		ret = qlcnic_config_led_blink(adapter, 0);
		(void) memcpy(up_data, &ret, sizeof (int));
		data.rv = 0;
		break;

	case qlcnic_cmd_loopback_test:

		ret = qlcnic_loopback_test(adapter);
		if (ret) {
			cmn_err(CE_WARN, "qlcnic_cmd_loopback_test failed\n");
			goto error_out;
		} else {
			(void) memcpy(up_data, &ret, sizeof (int));
		}
		data.rv = 0;
		break;

	case qlcnic_cmd_irq_test:

		ret = qlcnic_irq_test(adapter);
		if (ret) {
			cmn_err(CE_WARN, "qlcnic_cmd_irq_test failed\n");
			goto error_out;
		} else {
			(void) memcpy(up_data, &ret, sizeof (int));
		}
		data.rv = 0;
		break;

	case qlcnic_cmd_interface_info:
		data.rv = adapter->qlcnic_get_deviceinfo(adapter,
		    (struct qlcnic_devinfo *)up_data);
		data.size = sizeof (struct qlcnic_devinfo);
		break;

	case qlcnic_cmd_set_dbg_level:
		adapter->dbg_level = (uint32_t)data.uabc[0];
		data.rv = 0;
		data.size = 0;
		cmn_err(CE_NOTE, "new dbg level %x\n", data.uabc[0]);
		break;
	default:
		cmn_err(CE_WARN, "%s%d: bad command %d", adapter->name,
		    adapter->instance, data.cmd);
		data.rv = GLD_NOTSUPPORTED;
		data.size = 0;
		goto error_out;
	}

work_done:
	miocack(wq, mp, data.size, data.rv);
	return (DDI_SUCCESS);

error_out:
	cmn_err(CE_WARN, "%s(%d) ioctl error", __FUNCTION__, data.cmd);
	miocnak(wq, mp, 0, EINVAL);
	return (retval);
}

/*
 * Local datatype for defining tables of (Offset, Name) pairs
 */
typedef struct {
	offset_t	index;
	char		*name;
} qlcnic_ksindex_t;

static const qlcnic_ksindex_t qlcnic_kstat[] = {
	{ 0,		"polled"		},
	{ 1,		"uphappy"		},
	{ 2,		"updropped"		},
	{ 3,		"csummed"		},
	{ 4,		"no_rcv"		},
	{ 5,		"rxbytes"		},
	{ 6,		"rxcopyed"		},
	{ 7,		"rxmapped"		},
	{ 8,		"desballocfailed"	},
	{ 9,		"outofrxbuf"		},
	{ 10,		"promiscmode"		},
	{ 11,		"rxbufshort"		},
	{ 12,		"allocbfailed"		},
	{ 13,		"fw major"		},
	{ 14,		"fw minor"		},
	{ 15,		"fw sub"		},
	{ 16,		"number rx rings"	},
	{ 17,		"number tx rings"	},
	{ 18,		"number of interrupts"	},

	{ 19,		"intr on rx ring0"	},
	{ 20,		"intr on rx ring1"	},
	{ 21,		"intr on rx ring2"	},
	{ 22,		"intr on rx ring3"	},
	{ 23,		"rbytes on rx ring0"	},
	{ 24,		"rbytes on rx ring1"	},
	{ 25,		"rbytes on rx ring2"	},
	{ 26,		"rbytes on rx ring3"	},
	{ 27,		"rx rds0 rx_buf_card"	},
	{ 28,		"rx rds0 rx_buf_free"	},
	{ 29,		"rx rds1 rx_buf_card"	},
	{ 30,		"rx rds1 rx_buf_free"	},

	{ 31,		"tx_bcopy_threshold"	},
	{ 32,		"tx ring0 xmitcalled"	},
	{ 33,		"tx ring1 xmitcalled"	},
	{ 34,		"tx ring2 xmitcalled"	},
	{ 35,		"tx ring3 xmitcalled"	},
	{ 36,		"tx ring0 xmitfinished"	}, /* indiv */
	{ 37,		"tx ring1 xmitfinished"	},
	{ 38,		"tx ring2 xmitfinished"	},
	{ 39,		"tx ring3 xmitfinished"	},
	{ 40,		"tx ring0 outofcmddesc"	}, /* indiv */
	{ 41,		"tx ring1 outofcmddesc"	},
	{ 42,		"tx ring2 outofcmddesc"	},
	{ 43,		"tx ring3 outofcmddesc"	},
	{ 44,		"tx xmitedframes"	},
	{ 45,		"tx bytes"		},
	{ 46,		"tx freehdls"		},
	{ 47,		"tx freecmds"		},
	{ 48,		"tx copyed"		},
	{ 49,		"tx mapped"		},
	{ 50,		"tx outoftxdmahdl"	},
	{ 51,		"tx outoftxbuffer"	},
	{ 52,		"tx dropped"		},
	{ 53,		"tx dmabindfailures"	},
	{ 54,		"tx lastfailedcookiecount"},
	{ 55,		"tx exceedcookiesfailures"},
	{ 56,		"tx lastfailedhdrsize"	},
	{ 57,		"tx hdrsizefailures"	},
	{ 58,		"tx hdrtoosmallfailures"},
	{ 59,		"tx lastdmabinderror"	},
	{ 60,		"tx lastdmabindfailsize"},
	{ 61,		"tx sendcopybigpkt"	},
	{ 62,		"tx msgpulluped"},

	{ -1,		NULL			}
};

static int
qlcnic_kstat_update(kstat_t *ksp, int flag)
{
	qlcnic_adapter *adapter;
	kstat_named_t *knp;
	qlcnic_recv_context_t *recv_ctx;
	qlcnic_rcv_desc_ctx_t *rcv_desc;
	struct qlcnic_tx_ring_s *tx_ring;
	qlcnic_tx_stats_t tx_stats;
	int i;

	if (flag != KSTAT_READ)
		return (EACCES);

	adapter = ksp->ks_private;
	knp = ksp->ks_data;

	bzero(&tx_stats, sizeof (qlcnic_tx_stats_t));

	/* Global */
	(knp++)->value.ui64 = adapter->stats.polled;
	(knp++)->value.ui64 = adapter->stats.uphappy;
	(knp++)->value.ui64 = adapter->stats.updropped;
	(knp++)->value.ui64 = adapter->stats.csummed;
	(knp++)->value.ui64 = adapter->stats.no_rcv;
	(knp++)->value.ui64 = adapter->stats.rxbytes;
	(knp++)->value.ui64 = adapter->stats.rxcopyed;
	(knp++)->value.ui64 = adapter->stats.rxmapped;
	(knp++)->value.ui64 = adapter->stats.desballocfailed;
	(knp++)->value.ui64 = adapter->stats.outofrxbuf;
	(knp++)->value.ui64 = adapter->stats.promiscmode;
	(knp++)->value.ui64 = adapter->stats.rxbufshort;
	(knp++)->value.ui64 = adapter->stats.allocbfailed;
	(knp++)->value.ui64 = adapter->fw_major;
	(knp++)->value.ui64 = adapter->fw_minor;
	(knp++)->value.ui64 = adapter->fw_sub;
	(knp++)->value.ui64 = adapter->max_sds_rings;
	(knp++)->value.ui64 = adapter->max_tx_rings;
	(knp++)->value.ui64 = adapter->intr_count;

	/* rx statistics */
	(knp++)->value.ui64 = adapter->recv_ctx->sds_ring[0].no_rcv;
	(knp++)->value.ui64 = adapter->recv_ctx->sds_ring[1].no_rcv;
	(knp++)->value.ui64 = adapter->recv_ctx->sds_ring[2].no_rcv;
	(knp++)->value.ui64 = adapter->recv_ctx->sds_ring[3].no_rcv;
	(knp++)->value.ui64 = adapter->recv_ctx->sds_ring[0].rxbytes;
	(knp++)->value.ui64 = adapter->recv_ctx->sds_ring[1].rxbytes;
	(knp++)->value.ui64 = adapter->recv_ctx->sds_ring[2].rxbytes;
	(knp++)->value.ui64 = adapter->recv_ctx->sds_ring[3].rxbytes;
	/* rds ring0 - mtu1500 */
	recv_ctx = &(adapter->recv_ctx[0]);
	rcv_desc = recv_ctx->rcv_desc[0];
	if (rcv_desc) {
		(knp++)->value.ui64 = rcv_desc->rx_buf_card;
		(knp++)->value.ui64 = rcv_desc->rx_buf_free;
	} else {
		(knp++)->value.ui64 = 0;
		(knp++)->value.ui64 = 0;
	}
	/* rds ring0 - mtu9000 */
	rcv_desc = recv_ctx->rcv_desc[1];
	if (rcv_desc) {
		(knp++)->value.ui64 = rcv_desc->rx_buf_card;
		(knp++)->value.ui64 = rcv_desc->rx_buf_free;
	} else {
		(knp++)->value.ui64 = 0;
		(knp++)->value.ui64 = 0;
	}
	/* tx statics */
	for (i = 0; i < adapter->max_tx_rings; i++) {
		tx_ring = &(adapter->tx_ring[i]);
		/* total tx frames and bytes */
		tx_stats.xmitedframes += tx_ring->stats.xmitedframes;
		tx_stats.txbytes += tx_ring->stats.txbytes;
	}

	(knp++)->value.ui64 = adapter->tx_bcopy_threshold;
	(knp++)->value.ui64 = adapter->tx_ring[0].stats.xmitcalled;
	(knp++)->value.ui64 = adapter->tx_ring[1].stats.xmitcalled;
	(knp++)->value.ui64 = adapter->tx_ring[2].stats.xmitcalled;
	(knp++)->value.ui64 = adapter->tx_ring[3].stats.xmitcalled;
	(knp++)->value.ui64 = adapter->tx_ring[0].stats.xmitfinished;
	(knp++)->value.ui64 = adapter->tx_ring[1].stats.xmitfinished;
	(knp++)->value.ui64 = adapter->tx_ring[2].stats.xmitfinished;
	(knp++)->value.ui64 = adapter->tx_ring[3].stats.xmitfinished;
	(knp++)->value.ui64 = adapter->tx_ring[0].stats.outofcmddesc;
	(knp++)->value.ui64 = adapter->tx_ring[1].stats.outofcmddesc;
	(knp++)->value.ui64 = adapter->tx_ring[2].stats.outofcmddesc;
	(knp++)->value.ui64 = adapter->tx_ring[3].stats.outofcmddesc;

	(knp++)->value.ui64 = tx_stats.xmitedframes;
	(knp++)->value.ui64 = tx_stats.txbytes;
	(knp++)->value.ui64 = adapter->tx_ring[0].dmah_free;
	(knp++)->value.ui64 = adapter->tx_ring[0].freecmds;
	(knp++)->value.ui64 = adapter->tx_ring[0].stats.txcopyed;
	(knp++)->value.ui64 = adapter->tx_ring[0].stats.txmapped;
	(knp++)->value.ui64 = adapter->tx_ring[0].stats.outoftxdmahdl;
	(knp++)->value.ui64 = adapter->tx_ring[0].stats.outoftxbuffer;
	(knp++)->value.ui64 = adapter->tx_ring[0].stats.txdropped;
	(knp++)->value.ui64 = adapter->tx_ring[0].stats.dmabindfailures;
	(knp++)->value.ui64 = adapter->tx_ring[0].stats.lastfailedcookiecount;
	(knp++)->value.ui64 = adapter->tx_ring[0].stats.exceedcookiesfailures;
	(knp++)->value.ui64 = adapter->tx_ring[0].stats.lastfailedhdrsize;
	(knp++)->value.ui64 = adapter->tx_ring[0].stats.hdrsizefailures;
	(knp++)->value.ui64 = adapter->tx_ring[0].stats.hdrtoosmallfailures;
	(knp++)->value.ui64 = adapter->tx_ring[0].stats.lastdmabinderror;
	(knp++)->value.ui64 = adapter->tx_ring[0].stats.lastdmabindfailsize;
	(knp++)->value.ui64 = adapter->tx_ring[0].stats.sendcopybigpkt;
	(knp++)->value.ui64 = adapter->tx_ring[0].stats.msgpulluped;

	return (0);
}

static kstat_t *
qlcnic_setup_named_kstat(qlcnic_adapter *adapter, int instance, char *name,
    const qlcnic_ksindex_t *ksip, size_t size, int (*update)(kstat_t *, int))
{
	kstat_t *ksp;
	kstat_named_t *knp;
	char *np;
	int type;
	int count = 0;

	size /= sizeof (qlcnic_ksindex_t);
	ksp = kstat_create(qlcnic_driver_name, instance, name, "net",
	    KSTAT_TYPE_NAMED, size-1, KSTAT_FLAG_PERSISTENT);
	if (ksp == NULL)
		return (NULL);

	ksp->ks_private = adapter;
	ksp->ks_update = update;
	for (knp = ksp->ks_data; (np = ksip->name) != NULL; ++knp, ++ksip) {
		count++;
		switch (*np) {
		default:
			type = KSTAT_DATA_UINT64;
			break;
		case '%':
			np += 1;
			type = KSTAT_DATA_UINT32;
			break;
		case '$':
			np += 1;
			type = KSTAT_DATA_STRING;
			break;
		case '&':
			np += 1;
			type = KSTAT_DATA_CHAR;
			break;
		}
		kstat_named_init(knp, np, type);
	}
	kstat_install(ksp);

	return (ksp);
}

void
qlcnic_init_kstats(qlcnic_adapter* adapter, int instance)
{
	adapter->kstats[0] = qlcnic_setup_named_kstat(adapter,
	    instance, "status", qlcnic_kstat,
	    sizeof (qlcnic_kstat), qlcnic_kstat_update);
}

void
qlcnic_fini_kstats(qlcnic_adapter* adapter)
{

	if (adapter->kstats[0] != NULL) {
			kstat_delete(adapter->kstats[0]);
			adapter->kstats[0] = NULL;
		}
}

static int
qlcnic_set_pauseparam(qlcnic_adapter *adapter, qlcnic_pauseparam_t *pause)
{
	int ret = 0;

	if (QLCNIC_IS_REVISION_P3(adapter->ahw.revision_id))
		return (0);

	if (adapter->ahw.board_type == QLCNIC_GBE) {
		if (qlcnic_niu_gbe_set_rx_flow_ctl(adapter, pause->rx_pause))
			ret = -EIO;

		if (qlcnic_niu_gbe_set_tx_flow_ctl(adapter, pause->tx_pause))
			ret = -EIO;

	} else if (adapter->ahw.board_type == QLCNIC_XGBE) {
		if (qlcnic_niu_xg_set_tx_flow_ctl(adapter, pause->tx_pause))
			ret =  -EIO;
	} else
		ret = -EIO;

	return (ret);
}


int
qlcnic_driver_start(qlcnic_adapter* adapter, int fw_flag, int fw_recovery)
{
	int ring;
	int i, ctx;
	struct qlcnic_sds_ring_s *sds_ring;
	qlcnic_recv_context_t *recv_ctx;
	qlcnic_rcv_desc_ctx_t *rcv_desc;
	uint16_t pci_cmd_word;

	DPRINTF(DBG_GLD, (CE_NOTE, "qlcnic_driver_start(%d) entered \n",
	    adapter->instance));

	if (adapter->drv_state == QLCNIC_DRV_DETACH) {
		return (DDI_SUCCESS);
	}

	/*
	 * Turn ON bus master ability for this function, was
	 * turned off during firmware recovery
	 */
	pci_cmd_word = pci_config_get16(adapter->pci_cfg_handle, PCI_CONF_COMM);
	if (!(pci_cmd_word & PCI_COMM_ME)) {
		pci_cmd_word |= (PCI_COMM_ME);
		pci_config_put16(adapter->pci_cfg_handle, PCI_CONF_COMM,
		    pci_cmd_word);
	}

	if (fw_flag) {
		if (adapter->dev_init_in_progress) {
			cmn_err(CE_WARN, "%s%d: Device init in progress ...",
			    adapter->name, adapter->instance);
			return (DDI_FAILURE);
		}

		adapter->dev_init_in_progress = 1;
		if (qlcnic_dev_init(adapter) != DDI_SUCCESS) {
			adapter->dev_init_in_progress = 0;
			cmn_err(CE_WARN, "%s%d: Failed to init firmware",
			    adapter->name, adapter->instance);
			return (DDI_FAILURE);
		}
		adapter->dev_init_in_progress = 0;
	}

	if (!fw_recovery) {
		if (qlcnic_create_rxtx_rings(adapter) != DDI_SUCCESS) {
			cmn_err(CE_WARN, "%s%d: qlcnic_create_rxtx_rings "
			    "failed",
			    adapter->name, adapter->instance);
			return (DDI_FAILURE);
		}
	}

	if (init_firmware(adapter) != DDI_SUCCESS) {
		cmn_err(CE_WARN, "%s%d: Failed to init firmware",
		    adapter->name, adapter->instance);
		goto dest_rings;
	}

	qlcnic_clear_stats(adapter);

	if (fw_recovery) {
		if (qlcnic_receive_peg_ready(adapter)) {
			cmn_err(CE_WARN, "%s%d: Receive peg not ready",
			    adapter->name, adapter->instance);
			goto dest_rings;
		} else if (qlcnic_create_rxtx_ctx(adapter)) {
			cmn_err(CE_WARN,
			    "%s%d: Unable to create rx/tx contexts",
			    adapter->name, adapter->instance);
			goto dest_rings;
		}

		for (ctx = 0; ctx < MAX_RCV_CTX; ++ctx) {
			recv_ctx = &adapter->recv_ctx[ctx];

			for (ring = 0; ring < adapter->max_rds_rings; ring++) {
				rcv_desc = recv_ctx->rcv_desc[ring];
				rcv_desc->quit_time = 0;
				rcv_desc->pool_list = rcv_desc->rx_buf_pool;
				rcv_desc->rx_buf_free = rcv_desc->rx_buf_total;
				rcv_desc->rx_buf_indicated = 0;
			} /* For all rings */
		} /* For all receive contexts */
	} else {
		if ((qlcnic_hw_resources(adapter) != 0)) {
			cmn_err(CE_WARN, "%s%d: Error setting hw resources",
			    adapter->name, adapter->instance);
			goto dest_rings;
		}
	}

	for (ring = 0; ring < adapter->max_rds_rings; ring++) {
		if (qlcnic_post_rx_buffers(adapter, ring) != DDI_SUCCESS) {
			cmn_err(CE_WARN,
			    "%s%d: Unable to post buffers to firmware",
			    adapter->name, adapter->instance);
			goto free_hw_res;
		}
	}

	if (qlcnic_macaddr_set(adapter, adapter->mac_addr) != 0) {
		cmn_err(CE_WARN, "%s%d: Could not set mac address",
		    adapter->name, adapter->instance);
		goto free_hw_res;
	}
	qlcnic_set_link_parameters(adapter);

	mac_link_update(adapter->mach, LINK_STATE_UNKNOWN);

	qlcnic_p3_nic_set_multi(adapter);
	adapter->stats.promiscmode = 1;

	if (qlcnic_set_mtu(adapter, adapter->mtu) != 0) {
		cmn_err(CE_WARN, "%s%d: Could not set mtu",
		    adapter->name, adapter->instance);
		goto stop_and_free;
	}

	if (!adapter->watchdog_running) {
		adapter->watchdog_timer = timeout(
		    (void (*)(void *))&qlcnic_watchdog, (void *)adapter, 0);
		adapter->watchdog_running = 1;
	}

	adapter->is_up = QLCNIC_ADAPTER_UP_MAGIC;
	adapter->fw_fail_cnt = 0;
	adapter->force_fw_reset = 0;

	/* If we have enabled multiple receive rings, configure RSS */
	if (adapter->max_sds_rings > 1) {
		if (qlcnic_configure_rss(adapter) != 0) {
			cmn_err(CE_WARN, "%s%d: Could not configure RSS",
			    adapter->name, adapter->instance);
		} else {
			/*
			 * If we are recovering from a firmware reset program
			 * the IP address as well
			 */
			if (adapter->current_ip_addr) {
				struct qlcnic_tx_ring_s *tx_ring =
				    &adapter->tx_ring[0];

				mutex_enter(&tx_ring->tx_lock);
				if (qlcnic_configure_ip_addr(tx_ring,
				    adapter->current_ip_addr)) {
					cmn_err(CE_WARN, "!%s%d: Failed to set "
					    "current ip address to 0x%x",
					    adapter->name, adapter->instance,
					    adapter->current_ip_addr);
				}
				mutex_exit(&tx_ring->tx_lock);
			}
		}
	}

	/*
	 * Enable interrupt coalescing parameters once it is fixed,
	 * currently any value of > 0 in rx_time_us starts reducing performance
	 */
	/* qlcnic_configure_intr_coalesce(adapter); */
	for (i = 0; i < adapter->max_sds_rings; i++) {
		sds_ring = &adapter->recv_ctx[0].sds_ring[i];

		mutex_enter(&sds_ring->sds_lock);
		qlcnic_enable_int(sds_ring);
		mutex_exit(&sds_ring->sds_lock);
	}

	adapter->drv_state = QLCNIC_DRV_OPERATIONAL;
	DPRINTF(DBG_GLD, (CE_NOTE, "%s done \n", __func__));

	return (DDI_SUCCESS);

stop_and_free:
	/* qlcnic_stop_port(adapter); */
free_hw_res:
	if (!fw_recovery)
		qlcnic_free_hw_resources(adapter);
dest_rings:
	if (QLCNIC_IS_REVISION_P3PLUS(adapter->ahw.revision_id))
		(void) qlcnic_p3p_clear_dev_state(adapter, 0x0);
	if (!fw_recovery)
		qlcnic_destroy_rxtx_rings(adapter);
	return (DDI_FAILURE);
}

/*
 * GLD/MAC interfaces
 */
static int
qlcnic_m_start(void *arg)
{
	qlcnic_adapter *adapter = arg;
	int fw_flag;

	DPRINTF(DBG_INIT, (CE_NOTE, "%s%d: qlcnic_m_start enter\n",
	    adapter->name, adapter->instance));

	mutex_enter(&adapter->lock);

	if (adapter->drv_state == QLCNIC_DRV_SUSPEND) {
		mutex_exit(&adapter->lock);
		return (ECANCELED);
	}

	if ((adapter->is_up == QLCNIC_ADAPTER_UP_MAGIC)) {
		if (QLCNIC_IS_REVISION_P3PLUS(adapter->ahw.revision_id)) {
			adapter->drv_state = QLCNIC_DRV_OPERATIONAL;
		}
		mutex_exit(&adapter->lock);
		DPRINTF(DBG_INIT, (CE_NOTE, "%s: %s%d: adapter instance "
		    "already up. exiting\n",
		    __func__, adapter->name, adapter->instance));
		return (DDI_SUCCESS);
	}

	if (QLCNIC_IS_REVISION_P3PLUS(adapter->ahw.revision_id))
		fw_flag = 1;
	else
		fw_flag = 0;
	if (qlcnic_driver_start(adapter, fw_flag, 0) != DDI_SUCCESS) {
		cmn_err(CE_WARN, "%s%d: Failed to start driver",
		    adapter->name, adapter->instance);
		mutex_exit(&adapter->lock);
		ddi_fm_service_impact(adapter->dip,
		    DDI_SERVICE_LOST);
		return (DDI_FAILURE);
	}
	mutex_exit(&adapter->lock);
	if (qlcnic_check_acc_handle(adapter,
	    adapter->regs_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(adapter->dip,
		    DDI_SERVICE_LOST);
		return (DDI_FAILURE);
	}
	DPRINTF(DBG_INIT, (CE_NOTE, "%s: %s%d: exiting\n",
	    __func__, adapter->name, adapter->instance));
	cmn_err(CE_NOTE, "!qlcnic(%d) started. qlcnic version %s\n",
	    adapter->instance, QLCNIC_VERSIONID);
	return (0);
}


/*
 * Needed mainly for P3P
 */
/* ARGSUSED */
static void
qlcnic_m_stop(void *arg)
{
	qlcnic_adapter *adapter = arg;
	int 		count = 0;

	DPRINTF(DBG_GLD, (CE_NOTE, "%s%d: qlcnic_m_stop enter\n",
	    adapter->name, adapter->instance));

	count = 0;
	while (count < 1000) {
		mutex_enter(&adapter->lock);
		if (adapter->remove_entered != 1) {
			adapter->remove_entered = 1;
			mutex_exit(&adapter->lock);
			break;
		} /* if */
		mutex_exit(&adapter->lock);
		qlcnic_delay(100);
		count++;
	} /* while */

	if (count >= 1000) {
		cmn_err(CE_WARN, "%s%d: adapter lock held too long,"
		    " stop failed!", adapter->name, adapter->instance);
		return;
	}

	mutex_enter(&adapter->lock);

	if (adapter->drv_state == QLCNIC_DRV_SUSPEND) {
		mutex_exit(&adapter->lock);
		return;
	}
	mutex_exit(&adapter->lock);

	/* Stop the watchdog timer */
	qlcnic_remove(adapter, 0x1);

	mutex_enter(&adapter->lock);
	adapter->drv_state = QLCNIC_DRV_DOWN;
	adapter->remove_entered = 0;
	mutex_exit(&adapter->lock);

	DPRINTF(DBG_GLD, (CE_NOTE, "%s%d: qlcnic_m_stop exiting\n",
	    adapter->name, adapter->instance));
}

/*ARGSUSED*/
static int
qlcnic_m_multicst(void *arg, boolean_t add, const uint8_t *mcst_addr)
{
	/*
	 * When we correctly implement this, invoke qlcnic_p3_nic_set_multi()
	 * here.
	 */
	DPRINTF(DBG_GLD, (CE_NOTE, "qlcnic_m_multicst entered add"));
	DPRINTF(DBG_GLD, (CE_NOTE, "mcst_addr %02x %02x %02x %02x %02x %02x \n",
	    add, mcst_addr[0], mcst_addr[1], mcst_addr[2],
	    mcst_addr[3], mcst_addr[4], mcst_addr[5]));

	return (0);
}

/*ARGSUSED*/
static int
qlcnic_m_promisc(void *arg, boolean_t on)
{
	return (0);
}

static int
qlcnic_m_stat(void *arg, uint_t stat, uint64_t *val)
{
	struct qlcnic_adapter_s *adapter = arg;
	struct qlcnic_adapter_stats *portstat = &adapter->stats;
	struct qlcnic_tx_ring_s *tx_ring;
	int i;
	uint64_t temp = 0;

	mutex_enter(&adapter->lock);

	if (adapter->drv_state == QLCNIC_DRV_SUSPEND) {
		mutex_exit(&adapter->lock);
		return (ECANCELED);
	}

	switch (stat) {
	case MAC_STAT_IFSPEED:
		if (adapter->ahw.board_type == QLCNIC_XGBE) {
			/* 10 Gigs */
			*val = 10000000000ULL;
		} else {
			/* 1 Gig */
			*val = 1000000000;
		}
		break;

	case MAC_STAT_MULTIRCV:
		*val = 0;
		break;

	case MAC_STAT_BRDCSTRCV:
	case MAC_STAT_BRDCSTXMT:
		*val = 0;
		break;

	case MAC_STAT_NORCVBUF:
		*val = portstat->updropped;
		break;

	case MAC_STAT_NOXMTBUF:
		temp = 0;
		for (i = 0; i < adapter->max_tx_rings; i++) {
			tx_ring = &(adapter->tx_ring[0]);
			temp += tx_ring->stats.txdropped;
		}
		*val = temp;
		break;

	case MAC_STAT_RBYTES:
		*val = portstat->rxbytes;
		break;

	case MAC_STAT_OBYTES:
		temp = 0;
		for (i = 0; i < adapter->max_tx_rings; i++) {
			tx_ring = &(adapter->tx_ring[0]);
			temp += tx_ring->stats.txbytes;
		}
		*val = temp;
		break;

	case MAC_STAT_OPACKETS:
		temp = 0;
		for (i = 0; i < adapter->max_tx_rings; i++) {
			tx_ring = &(adapter->tx_ring[0]);
			temp += tx_ring->stats.xmitedframes;
		}
		*val = temp;
		break;

	case MAC_STAT_IPACKETS:
		*val = portstat->uphappy;
		break;

	case MAC_STAT_OERRORS:
		temp = 0;
		for (i = 0; i < adapter->max_tx_rings; i++) {
			tx_ring = &(adapter->tx_ring[0]);
			temp += tx_ring->stats.txdropped;
		}
		*val = temp;
		break;

	case ETHER_STAT_LINK_DUPLEX:
		*val = LINK_DUPLEX_FULL;
		break;

	default:
		mutex_exit(&adapter->lock);
		return (ENOTSUP);
	}

	mutex_exit(&adapter->lock);

	return (0);
}
mblk_t *
qlcnic_ring_tx(void *arg, mblk_t *mp)
{
	qlcnic_tx_ring_t *tx_ring = (qlcnic_tx_ring_t *)arg;
	qlcnic_adapter *adapter = tx_ring->adapter;
	mblk_t *next;

	DPRINTF(DBG_TX,
	    (CE_NOTE, "qlcnic(%d)qlcnic_ring_tx entered", adapter->instance));

	if ((adapter->drv_state != QLCNIC_DRV_OPERATIONAL) ||
	    (adapter->drv_state == QLCNIC_DRV_SUSPEND)) {
		goto tx_exit;
	}
	if ((adapter->diag_test == QLCNIC_LOOPBACK_TEST) ||
	    (adapter->diag_test == QLCNIC_INTERRUPT_TEST)) {
		cmn_err(CE_WARN,
		    "%s%d: Diagnostic test in progress, drop pkt\n",
		    adapter->name, adapter->instance);
		goto tx_exit;
	}
	/* link is down! */
	if (adapter->ahw.linkup == 0) {
		goto tx_exit;
	}

	while (mp != NULL) {
		next = mp->b_next;
		mp->b_next = NULL;

		if (qlcnic_xmit_frame(adapter, tx_ring, mp) != B_TRUE) {
			mp->b_next = next;
			break;
		}
		mp = next;
		tx_ring->stats.xmitedframes++;
	}

	return (mp);
tx_exit:
	freemsgchain(mp);
	mp = NULL;
	return (mp);
}

/*
 * Find the slot for the specified unicast address
 */
static int
qlcnic_unicst_find(qlcnic_adapter *adapter, const uint8_t *mac_addr)
{
	int slot;

	for (slot = 0; slot < adapter->unicst_total; slot++) {
		if (bcmp(adapter->unicst_addr[slot].addr.ether_addr_octet,
		    mac_addr, ETHERADDRL) == 0)
			return (slot);
	}

	return (-1);
}

static int
qlcnic_addmac(void *arg, const uint8_t *mac_addr, uint64_t flags)
{
	_NOTE(ARGUNUSED(flags))

	qlcnic_rx_group_t *rx_group = (qlcnic_rx_group_t *)arg;
	qlcnic_adapter *adapter = rx_group->adapter;
	int i;
	int ret;

	DPRINTF(DBG_GLD, (CE_NOTE, "qlcnic_addmac(%d) entered to add ",
	    adapter->instance));
	DPRINTF(DBG_GLD, (CE_NOTE, "mac_addr %02x %02x %02x %02x %02x %02x \n",
	    mac_addr[0], mac_addr[1], mac_addr[2],
	    mac_addr[3], mac_addr[4], mac_addr[5]));

	mutex_enter(&adapter->lock);

	if (adapter->drv_state == QLCNIC_DRV_SUSPEND) {
		mutex_exit(&adapter->lock);
		return (ECANCELED);
	}

	i = qlcnic_unicst_find(adapter, mac_addr);
	if (i != -1) {
		cmn_err(CE_NOTE,
		    "!mac_address already added, available %d\n",
		    adapter->unicst_avail);
		mutex_exit(&adapter->lock);
		return (0);
	}

	if (adapter->unicst_avail == 0) {
		/* no slots available */
		mutex_exit(&adapter->lock);
		cmn_err(CE_NOTE, "!qlcnic_addmac(%d) ignored, no space\n",
		    adapter->instance);
		return (ENOSPC);
	}

	/* find the available slot */
	for (i = 0; i < adapter->unicst_total; i++) {
		if (adapter->unicst_addr[i].set == 0)
			break;
	}
	if (i >= adapter->unicst_total) {
		/* no slots available */
		mutex_exit(&adapter->lock);
		cmn_err(CE_NOTE, "!qlcnic_addmac(%d) no space! available %d\n",
		    adapter->instance, adapter->unicst_avail);
		return (ENOSPC);
	}

	/* Set new Mac Address to the slot */
	ret = qlcnic_p3_sre_macaddr_change(adapter, (uint8_t *)mac_addr,
	    QLCNIC_MAC_ADD);
	if (ret == 0) {
		bcopy(mac_addr, adapter->unicst_addr[i].addr.ether_addr_octet,
		    ETHERADDRL);
		adapter->unicst_addr[i].set = 1;
		adapter->unicst_avail--;
	} else {
		cmn_err(CE_WARN, "!qlcnic_addmac(%d) failed",
		    adapter->instance);
	}
	mutex_exit(&adapter->lock);

	DPRINTF(DBG_GLD, (CE_NOTE,
	    "qlcnic_addmac(%d) done, ret %d, added to slot %d, available %d",
	    adapter->instance, ret, i, adapter->unicst_avail));
	return (0);
}

static int
qlcnic_remmac(void *arg, const uint8_t *mac_addr)
{
	qlcnic_rx_group_t *rx_group = (qlcnic_rx_group_t *)arg;
	qlcnic_adapter *adapter = rx_group->adapter;
	uint8_t null_mac_addr[ETHERADDRL];
	int ret;
	int i;

	DPRINTF(DBG_GLD, (CE_NOTE, "qlcnic_remmac(%d) entered to remove ",
	    adapter->instance));
	DPRINTF(DBG_GLD, (CE_NOTE, "mac_addr %02x %02x %02x %02x %02x %02x \n",
	    mac_addr[0], mac_addr[1], mac_addr[2],
	    mac_addr[3], mac_addr[4], mac_addr[5]));

	bzero(null_mac_addr, sizeof (ETHERADDRL));

	mutex_enter(&adapter->lock);

	if (adapter->drv_state == QLCNIC_DRV_SUSPEND) {
		mutex_exit(&adapter->lock);
		return (ECANCELED);
	}

	i = qlcnic_unicst_find(adapter, mac_addr);
	if (i == -1) {
		cmn_err(CE_NOTE, "!remove none listed mac_addr "
		    "%02x %02x %02x %02x %02x %02x \n",
		    mac_addr[0], mac_addr[1], mac_addr[2],
		    mac_addr[3], mac_addr[4], mac_addr[5]);
		mutex_exit(&adapter->lock);
		return (EINVAL);
	}

	if (adapter->unicst_addr[i].set == 0) {
		mutex_exit(&adapter->lock);
		return (EINVAL);
	}

	bcopy(null_mac_addr, adapter->unicst_addr[i].addr.ether_addr_octet,
	    ETHERADDRL);

	ret = qlcnic_p3_sre_macaddr_change(adapter, (uint8_t *)mac_addr,
	    QLCNIC_MAC_DEL);
	if (ret == 0) {
		adapter->unicst_addr[i].set = 0;
		adapter->unicst_avail++;
		ret = DDI_SUCCESS;
	} else {
		cmn_err(CE_WARN,
		    "!qlcnic%d: fail to rmv mac %02x %02x %02x %02x %02x %02x",
		    adapter->instance,
		    mac_addr[0], mac_addr[1], mac_addr[2],
		    mac_addr[3], mac_addr[4], mac_addr[5]);
	}

	mutex_exit(&adapter->lock);

	DPRINTF(DBG_GLD, (CE_NOTE,
	    "qlcnic_remmac(%d) done: %d, available: %d \n",
	    adapter->instance, ret, adapter->unicst_avail));
	return (ret);
}

static void
qlcnic_m_ioctl(void *arg, queue_t *wq, mblk_t *mp)
{
	int err, cmd;
	struct iocblk *iocp = (struct iocblk *)(uintptr_t)mp->b_rptr;
	qlcnic_adapter *adapter = (qlcnic_adapter *)arg;
	enum ioc_reply status = IOC_DONE;
	boolean_t need_privilege = B_TRUE;

	iocp->ioc_error = 0;
	cmd = iocp->ioc_cmd;

	mutex_enter(&adapter->lock);
	if (adapter->drv_state == QLCNIC_DRV_SUSPEND) {
		mutex_exit(&adapter->lock);
		miocnak(wq, mp, 0, EINVAL);
		return;
	}

	switch (cmd) {
		default:
			/* unknown ioctl command */
			DPRINTF(DBG_GLD, (CE_NOTE,
			    "qlcnic_m_ioctl unknown ioctl cmd %x\n", cmd));
			miocnak(wq, mp, 0, EINVAL);
			mutex_exit(&adapter->lock);
			return;
		case QLCNIC_CMD_START:
		case QLCNIC_CMD:
		case QLCNIC_NAME:
			break;

		case LB_GET_INFO_SIZE:
		case LB_GET_INFO:
		case LB_GET_MODE:
			need_privilege = B_FALSE;
			/* FALLTHRU */
		case LB_SET_MODE:
			break;
	}

	if (need_privilege) {
		/*
		 * Check for specific net_config privilege
		 */
		err = secpolicy_net_config(iocp->ioc_cr, B_FALSE);
		if (err != 0) {
			miocnak(wq, mp, 0, err);
			mutex_exit(&adapter->lock);
			return;
		}
	}
	/*
	 * Implement ioctl
	 */
	switch (cmd) {
		case QLCNIC_CMD_START:
		case QLCNIC_CMD:
		case QLCNIC_NAME:
			qlcnic_ioctl(adapter, cmd, wq, mp);
			status = IOC_DONE;
			break;
		case LB_GET_INFO_SIZE:
		case LB_GET_INFO:
		case LB_GET_MODE:
		case LB_SET_MODE:
			status = qlcnic_loop_ioctl(adapter, wq, mp, iocp);
			break;
		default:
			status = IOC_INVAL;
			break;
	}
	/*
	 * Decide how to reply
	 */
	switch (status) {
		default:
		case IOC_INVAL:
			/*
			 * Error, reply with a NAK and EINVAL or the specified
			 * error
			 */
			miocnak(wq, mp, 0, iocp->ioc_error == 0 ?
			    EINVAL : iocp->ioc_error);
			break;

		case IOC_DONE:
			/*
			 * OK, reply already sent
			 */
			break;

		case IOC_RESTART_ACK:
		case IOC_ACK:
			/*
			 * OK, reply with an ACK
			 */
			miocack(wq, mp, 0, 0);
			break;

		case IOC_RESTART_REPLY:
		case IOC_REPLY:
			/*
			 * OK, send prepared reply as ACK or NAK
			 */
			mp->b_datap->db_type = iocp->ioc_error == 0 ?
			    M_IOCACK : M_IOCNAK;
			qreply(wq, mp);
			break;
	}
	mutex_exit(&adapter->lock);
}

/*
 * Enable interrupt on the specificed rx ring.
 */
int
qlcnic_rx_ring_intr_enable(mac_ring_driver_t rh)
{
	qlcnic_sds_ring_t *sds_ring = (qlcnic_sds_ring_t *)rh;

	mutex_enter(&sds_ring->sds_lock);
	qlcnic_enable_int(sds_ring);
	mutex_exit(&sds_ring->sds_lock);

	return (0);
}

int
qlcnic_rx_ring_intr_disable(mac_ring_driver_t rh)
{
	qlcnic_sds_ring_t *sds_ring = (qlcnic_sds_ring_t *)rh;

	mutex_enter(&sds_ring->sds_lock);
	qlcnic_disable_int(sds_ring);
	mutex_exit(&sds_ring->sds_lock);

	return (0);
}

static int
qlcnic_ring_start(mac_ring_driver_t rh, uint64_t mr_gen_num)
{
	qlcnic_sds_ring_t *sds_ring = (qlcnic_sds_ring_t *)rh;
	qlcnic_adapter *adapter = sds_ring->adapter;

	mutex_enter(&sds_ring->sds_lock);
	sds_ring->ring_gen_num = mr_gen_num;
	adapter->rx_reserved_attr[sds_ring->index].ring_gen_num = mr_gen_num;
	mutex_exit(&sds_ring->sds_lock);
	return (0);
}

/*
 * Retrieve a value for one of the statistics for a particular rx ring
 */
int
qlcnic_rx_ring_stat(mac_ring_driver_t rh, uint_t stat, uint64_t *val)
{
	qlcnic_sds_ring_t *sds_ring = (qlcnic_sds_ring_t *)rh;
	qlcnic_adapter *adapter = sds_ring->adapter;

	/* adapter coule be NULL if instance is not yet plumbed */
	if (adapter && adapter->drv_state == QLCNIC_DRV_SUSPEND) {
		return (ECANCELED);
	}

	switch (stat) {
	case MAC_STAT_RBYTES:
		*val = sds_ring->rxbytes;
		break;

	case MAC_STAT_IPACKETS:
		*val = sds_ring->no_rcv;
		break;

	default:
		*val = 0;
		return (ENOTSUP);
	}

	return (0);
}

/*
 * Retrieve a value for one of the statistics for a particular tx ring
 */
int
qlcnic_tx_ring_stat(mac_ring_driver_t rh, uint_t stat, uint64_t *val)
{
	qlcnic_tx_ring_t *tx_ring = (qlcnic_tx_ring_t *)rh;
	qlcnic_adapter *adapter = tx_ring->adapter;

	if (adapter && adapter->drv_state == QLCNIC_DRV_SUSPEND) {
		return (ECANCELED);
	}

	switch (stat) {
	case MAC_STAT_OBYTES:
		*val = tx_ring->stats.txbytes;
		break;

	case MAC_STAT_OPACKETS:
		*val = tx_ring->stats.xmitedframes;
		break;

	default:
		*val = 0;
		return (ENOTSUP);
	}

	return (0);
}

/*
 * Get the global ring index.
 */
static int
qlcnic_get_rx_ring_index(qlcnic_adapter *adapter, int gindex, int rindex)
{
	qlcnic_recv_context_t *recv_ctx = &adapter->recv_ctx[0];
	qlcnic_sds_ring_t *sds_ring;
	int i;

	for (i = 0; i < adapter->max_sds_rings; i++) {
		sds_ring = &recv_ctx->sds_ring[i];

		if (sds_ring->group_index == gindex)
			rindex--;
		if (rindex < 0)
			return (i);
	}

	return (-1);
}

/*
 * Callback funtion for MAC layer to register all rings.
 */
/* ARGSUSED */
void
qlcnic_fill_ring(void *arg, mac_ring_type_t rtype, const int group_index,
    const int ring_index, mac_ring_info_t *infop, mac_ring_handle_t rh)
{
	qlcnic_adapter *adapter = (qlcnic_adapter *)arg;
	mac_intr_t *mintr = &infop->mri_intr;

	switch (rtype) {
	case MAC_RING_TYPE_RX: {
		/*
		 * 'index' is the ring index within the group.
		 * Need to get the global ring index by searching in groups.
		 */
		DPRINTF(DBG_GLD,
		    (CE_NOTE, "qlcnic(%d) fill_ring entered to set group"
		    " index %d, ring index %d \n",
		    adapter->instance, group_index, ring_index));

		int global_ring_index = qlcnic_get_rx_ring_index(
		    adapter, group_index, ring_index);

		ASSERT(global_ring_index >= 0);
		DPRINTF(DBG_GLD,
		    (CE_NOTE, "fill_ring(%d) global_ring_index %d \n",
		    adapter->instance, global_ring_index));

		qlcnic_recv_context_t *recv_ctx = &adapter->recv_ctx[0];
		struct qlcnic_sds_ring_s *sds_ring =
		    &recv_ctx->sds_ring[global_ring_index];
		if (rh == NULL)
			cmn_err(CE_WARN, "rx ring(%d) ring handle NULL",
			    global_ring_index);
		sds_ring->ring_handle = rh;
		adapter->rx_reserved_attr[global_ring_index].ring_handle =
		    rh;

		infop->mri_driver = (mac_ring_driver_t)sds_ring;
		infop->mri_start = qlcnic_ring_start;
		infop->mri_stop = NULL;
		infop->mri_poll = qlcnic_ring_rx_poll;
		infop->mri_stat = qlcnic_rx_ring_stat;

		mintr->mi_enable = qlcnic_rx_ring_intr_enable;
		mintr->mi_disable = qlcnic_rx_ring_intr_disable;

		break;
	}
	case MAC_RING_TYPE_TX: {
		ASSERT(group_index == -1);
		ASSERT(ring_index < adapter->max_tx_rings);
		DPRINTF(DBG_GLD,
		    (CE_NOTE, "fill_ring(%d) entered to set tx ring %d\n",
		    adapter->instance, ring_index));

		qlcnic_tx_ring_t *tx_ring = &adapter->tx_ring[ring_index];
		tx_ring->ring_handle = rh;
		adapter->tx_reserved_attr[ring_index].ring_handle =
		    rh;

		infop->mri_driver = (mac_ring_driver_t)tx_ring;
		infop->mri_start = NULL;
		infop->mri_stop = NULL;
		infop->mri_tx = qlcnic_ring_tx;
		infop->mri_stat = qlcnic_tx_ring_stat;
		break;
	}
	default:
		break;
	}
}

/*
 * Callback funtion for MAC layer to register all groups.
 */
void
qlcnic_fill_group(void *arg, mac_ring_type_t rtype, const int index,
    mac_group_info_t *infop, mac_group_handle_t gh)
{
	qlcnic_adapter *adapter = (qlcnic_adapter *)arg;

	switch (rtype) {
	case MAC_RING_TYPE_RX: {
		qlcnic_rx_group_t *rx_group;

		DPRINTF(DBG_GLD,
		    (CE_NOTE,
		    "qlcnic(%d)qlcnic_fill_group entered to fill group %d\n",
		    adapter->instance, index));

		rx_group = &adapter->rx_groups[index];
		rx_group->group_handle = gh;

		infop->mgi_driver = (mac_group_driver_t)rx_group;
		infop->mgi_start = NULL;
		infop->mgi_stop = NULL;
		infop->mgi_addmac = qlcnic_addmac;
		infop->mgi_remmac = qlcnic_remmac;
		infop->mgi_count =
		    (adapter->max_sds_rings / adapter->num_rx_groups);
		if (index == 0)
			infop->mgi_flags = MAC_GROUP_DEFAULT;
		break;
	}
	case MAC_RING_TYPE_TX:
		break;
	default:
		break;
	}
}

/* ARGSUSED */
static boolean_t
qlcnic_m_getcapab(void *arg, mac_capab_t cap, void *cap_data)
{
	struct qlcnic_adapter_s *adapter = (struct qlcnic_adapter_s *)arg;
	int ret = B_FALSE;

	switch (cap) {
	case MAC_CAPAB_HCKSUM:
		{
			uint32_t *txflags = cap_data;

			*txflags = HCKSUM_INET_FULL_V4;
			ret = B_TRUE;
		}
		break;
	case MAC_CAPAB_LSO: {
		mac_capab_lso_t *cap_lso = (mac_capab_lso_t *)cap_data;
		uint32_t page_size;
		uint32_t lso_max;

		if (adapter->flags & QLCNIC_LSO_ENABLED) {
			cap_lso->lso_flags = LSO_TX_BASIC_TCP_IPV4;
			page_size = ddi_ptob(adapter->dip, (ulong_t)1);
			lso_max = page_size * (MAX_COOKIES_PER_CMD - 1);
			cap_lso->lso_basic_tcp_ipv4.lso_max =
			    min(lso_max, QLCNIC_LSO_MAXLEN);
			ret = B_TRUE;
		}
		break;
	}
	case MAC_CAPAB_RINGS: {
		mac_capab_rings_t *cap_rings = cap_data;

		switch (cap_rings->mr_type) {
		case MAC_RING_TYPE_RX:
			cap_rings->mr_version = MAC_RINGS_VERSION_1;
			cap_rings->mr_group_type = MAC_GROUP_TYPE_STATIC;
			cap_rings->mr_rnum = adapter->max_sds_rings;
			cap_rings->mr_gnum = adapter->num_rx_groups;
			cap_rings->mr_rget = qlcnic_fill_ring;
			cap_rings->mr_gget = qlcnic_fill_group;
			cap_rings->mr_gaddring = NULL;
			cap_rings->mr_gremring = NULL;
			ret = B_TRUE;
			break;
		case MAC_RING_TYPE_TX:
			cap_rings->mr_version = MAC_RINGS_VERSION_1;
			cap_rings->mr_group_type = MAC_GROUP_TYPE_STATIC;
			cap_rings->mr_rnum = adapter->max_tx_rings;
			cap_rings->mr_gnum = 1;
			cap_rings->mr_rget = qlcnic_fill_ring;
			cap_rings->mr_gget = NULL;
			cap_rings->mr_gaddring = NULL;
			cap_rings->mr_gremring = NULL;
			ret = B_TRUE;
			break;
		default:
			break;
		}
		break;
	}
	default:
		break;
	}
	return (ret);
}

/* ARGSUSED */
static int
qlcnic_set_priv_prop(qlcnic_adapter *adapter, const char *pr_name,
    uint_t pr_valsize, const void *pr_val)
{
	int err = 0;
	long result;

	if (strcmp(pr_name, "_tx_copy_thresh") == 0) {
		(void) ddi_strtol(pr_val, (char **)NULL, 0, &result);
		if (result < 0) {
			err = EINVAL;
		} else {
			adapter->tx_bcopy_threshold = (uint32_t)result;
		}
		return (err);
	} else if (strcmp(pr_name, "_rx_copy_thresh") == 0) {
		(void) ddi_strtol(pr_val, (char **)NULL, 0, &result);
		if (result < 0) {
			err = EINVAL;
		} else {
			adapter->rx_bcopy_threshold = (uint32_t)result;
		}
		return (err);
	}

	return (ENOTSUP);
}

/*
 * callback functions for set/get of properties
 */
/* ARGSUSED */
static int
qlcnic_m_setprop(void *barg, const char *pr_name, mac_prop_id_t pr_num,
    uint_t pr_valsize, const void *pr_val)
{
	qlcnic_adapter *adapter = barg;
	int err = 0;
	uint32_t cur_mtu, new_mtu;

	mutex_enter(&adapter->lock);
	if (adapter->drv_state == QLCNIC_DRV_SUSPEND) {
		mutex_exit(&adapter->lock);
		return (ECANCELED);
	}

	switch (pr_num) {
	case MAC_PROP_MTU:
		cur_mtu = adapter->mtu;
		bcopy(pr_val, &new_mtu, sizeof (new_mtu));

		DPRINTF(DBG_GLD, (CE_NOTE, "qlcnic_m_setprop(%d) new mtu %d \n",
		    adapter->instance, new_mtu));
		if (new_mtu == cur_mtu) {
			err = 0;
			break;
		}
		if ((new_mtu < ETHERMTU) || (new_mtu > P3_MAX_MTU)) {
			err = EINVAL;
			break;
		}
		/*
		 * do not change on the fly, allow only before
		 * driver is started or stopped
		 */
		if ((adapter->drv_state == QLCNIC_DRV_OPERATIONAL) ||
		    (adapter->drv_state == QLCNIC_DRV_DETACH) ||
		    (adapter->drv_state == QLCNIC_DRV_QUIESCENT)) {
			err = EBUSY;
			cmn_err(CE_WARN,
			    "qlcnic_m_setprop(%d) new mtu %d ignored, "
			    "driver busy, mac_flags %d",
			    adapter->instance, new_mtu, adapter->drv_state);
			break;
		}
		cmn_err(CE_NOTE, "!qlcnic(%d) new mtu %d\n",
		    adapter->instance, new_mtu);
		adapter->mtu = new_mtu;

		err = mac_maxsdu_update(adapter->mach, adapter->mtu);
		if (err == 0) {
			/* EMPTY */
			DPRINTF(DBG_GLD,
			    (CE_NOTE, "qlcnic_m_setprop(%d) new mtu %d set "
			    "success\n", adapter->instance, new_mtu));
		}
		break;
	case MAC_PROP_PRIVATE:
		err = qlcnic_set_priv_prop(adapter, pr_name, pr_valsize,
		    pr_val);
		break;
	default:
		err = ENOTSUP;
		break;
	}
	mutex_exit(&adapter->lock);
	return (err);
}

static int
qlcnic_get_priv_prop(qlcnic_adapter *adapter, const char *pr_name,
    uint_t pr_valsize, void *pr_val)
{
	int err = ENOTSUP;
	uint32_t value;

	if (strcmp(pr_name, "_tx_copy_thresh") == 0) {
		value = adapter->tx_bcopy_threshold;
		err = 0;
	} else if (strcmp(pr_name, "_rx_copy_thresh") == 0) {
		value = adapter->rx_bcopy_threshold;
		err = 0;
	}

	if (err == 0) {
		(void) snprintf(pr_val, pr_valsize, "%d", value);
	}
	return (err);
}

/* ARGSUSED */
static int
qlcnic_m_getprop(void *barg, const char *pr_name, mac_prop_id_t pr_num,
    uint_t pr_valsize, void *pr_val)
{
	qlcnic_adapter *adapter = barg;
	uint64_t speed;
	link_state_t link_state;
	link_duplex_t link_duplex;
	link_flowctrl_t fl;
	int err = 0;

	switch (pr_num) {
	case MAC_PROP_DUPLEX:
		ASSERT(pr_valsize >= sizeof (link_duplex_t));
		link_duplex = adapter->link_duplex;
		bcopy(&link_duplex, pr_val, sizeof (link_duplex_t));
		break;
	case MAC_PROP_SPEED:
		ASSERT(pr_valsize >= sizeof (speed));
		speed = adapter->link_speed * 1000000ull;
		bcopy(&speed, pr_val, sizeof (speed));
		break;
	case MAC_PROP_STATUS:
		ASSERT(pr_valsize >= sizeof (link_state_t));
		if (adapter->ahw.linkup == 1)
			link_state = LINK_STATE_UP;
		else
			link_state = LINK_STATE_DOWN;
		bcopy(&link_state, pr_val, sizeof (link_state_t));
		break;
	case MAC_PROP_FLOWCTRL:
		ASSERT(pr_valsize >= sizeof (fl));
		if (adapter->rx_pause &&
		    !adapter->tx_pause)
			fl = LINK_FLOWCTRL_RX;

		if (!adapter->rx_pause &&
		    !adapter->tx_pause)
			fl = LINK_FLOWCTRL_NONE;

		if (!adapter->rx_pause &&
		    adapter->tx_pause)
			fl = LINK_FLOWCTRL_TX;

		if (adapter->rx_pause &&
		    adapter->tx_pause)
			fl = LINK_FLOWCTRL_BI;
		bcopy(&fl, pr_val, sizeof (fl));
		break;
	case MAC_PROP_PRIVATE:
		err = qlcnic_get_priv_prop(adapter, pr_name, pr_valsize,
		    pr_val);
		break;

	default:
		err = ENOTSUP;
	}
out:
	return (err);
}

/* ARGSUSED */
static void
qlcnic_m_propinfo(void *barg, const char *pr_name, mac_prop_id_t pr_num,
    mac_prop_info_handle_t prh)
{
	_NOTE(ARGUNUSED(barg));

	switch (pr_num) {
	case MAC_PROP_DUPLEX:
	case MAC_PROP_SPEED:
	case MAC_PROP_STATUS:
		mac_prop_info_set_perm(prh, MAC_PROP_PERM_READ);
		break;
	case MAC_PROP_FLOWCTRL:
		mac_prop_info_set_default_link_flowctrl(prh, LINK_FLOWCTRL_BI);
		break;
	case MAC_PROP_MTU:
		mac_prop_info_set_range_uint32(prh, ETHERMTU, P3_MAX_MTU);
		break;

	case MAC_PROP_PRIVATE: {
		char val_str[64];
		int default_val;

		bzero(val_str, sizeof (val_str));
		if (strcmp(pr_name, "_rx_copy_thresh") == 0)
			default_val = QLCNIC_RX_BCOPY_THRESHOLD;
		else if (strcmp(pr_name, "_tx_copy_thresh") == 0)
			default_val = QLCNIC_TX_BCOPY_THRESHOLD;
		else
			return;

		(void) snprintf(val_str, sizeof (val_str), "%d", default_val);
		mac_prop_info_set_default_str(prh, val_str);
		break;
		}
	}
}


#define	QLCNIC_M_CALLBACK_FLAGS	(MC_IOCTL | MC_GETCAPAB | MC_SETPROP | \
    MC_GETPROP | MC_PROPINFO)

static mac_callbacks_t qlcnic_m_callbacks = {
	QLCNIC_M_CALLBACK_FLAGS,
	qlcnic_m_stat,
	qlcnic_m_start,
	qlcnic_m_stop,
	qlcnic_m_promisc,
	qlcnic_m_multicst,
	NULL,
	NULL,
	NULL,			/* mc_resources */
	qlcnic_m_ioctl,
	qlcnic_m_getcapab,
	NULL,			/* mc_open */
	NULL,			/* mc_close */
	qlcnic_m_setprop,
	qlcnic_m_getprop,
	qlcnic_m_propinfo
};

int
qlcnic_register_mac(qlcnic_adapter *adapter)
{
	int ret;
	mac_register_t *macp;
	qlcnic_pauseparam_t pause;

	dev_info_t *dip = adapter->dip;

	if ((macp = mac_alloc(MAC_VERSION)) == NULL) {
		cmn_err(CE_WARN, "Memory not available");
		return (DDI_FAILURE);
	}

	macp->m_type_ident = MAC_PLUGIN_IDENT_ETHER;
	macp->m_driver = adapter;
	macp->m_dip = dip;
	macp->m_instance = adapter->instance;
	macp->m_src_addr = adapter->mac_addr;
	macp->m_callbacks = &qlcnic_m_callbacks;
	macp->m_min_sdu = 0;
	macp->m_max_sdu = adapter->mtu;
	macp->m_margin = VLAN_TAGSZ;
	macp->m_priv_props = qlcnic_priv_prop;

	ret = mac_register(macp, &adapter->mach);
	mac_free(macp);
	if (ret != 0) {
		cmn_err(CE_WARN, "mac_register failed for port %d",
		    adapter->portnum);
		return (DDI_FAILURE);
	}

	qlcnic_init_kstats(adapter, adapter->instance);
	pause.rx_pause = adapter->rx_pause = 1;
	pause.tx_pause = adapter->tx_pause = 1;

	if (qlcnic_set_pauseparam(adapter, &pause)) {
		cmn_err(CE_WARN, "Bad Pause settings RX %d, Tx %d",
		    pause.rx_pause, pause.tx_pause);
	}

	return (DDI_SUCCESS);
}

int
qlcnic_check_health(qlcnic_adapter *adapter)
{
	uint32_t heartbeat;
	uint32_t dev_state;
	uint32_t status, val;
	uint32_t device_init;
	uint32_t drv_active, drv_state;

	if (qlcnic_api_lock(adapter)) {
		cmn_err(CE_WARN, "%s%d: Wait time for sem5 exceeded",
		    adapter->name, adapter->instance);
		return (1);
	}

	adapter->qlcnic_hw_read_wx(adapter,
	    QLCNIC_CAM_RAM(QLCNIC_CRB_DEV_STATE),
	    &dev_state, 4);

	qlcnic_api_unlock(adapter);

	/* handle multiple resets when driver is down */
	if (adapter->drv_state == QLCNIC_DRV_QUIESCENT) {
		/* check if all driver instances have acknowledged the reset */
		device_init = 0;

		if (adapter->force_fw_reset) {
			device_init = 1;
		}
		if (adapter->own_reset && !device_init) {
			if (qlcnic_api_lock(adapter)) {
				cmn_err(CE_WARN,
				    "%s%d: Wait time for sem5 exceeded 2",
				    adapter->name, adapter->instance);
				return (1);
			}

			adapter->qlcnic_hw_read_wx(adapter,
			    QLCNIC_CAM_RAM(QLCNIC_CRB_DRV_STATE),
			    &drv_state, 4);

			adapter->qlcnic_hw_read_wx(adapter,
			    QLCNIC_CAM_RAM(QLCNIC_CRB_DRV_ACTIVE),
			    &drv_active, 4);

			qlcnic_api_unlock(adapter);

			DPRINTF(DBG_INIT, (CE_NOTE,
			    "%s %s%d: Funcs Active 0x%08x, "
			    "Driver State 0x%08x\n",
			    __func__, adapter->name, adapter->instance,
			    drv_active, drv_state));
			if ((drv_state != QLCNIC_INVALID_REG_VALUE) &&
			    (drv_active != QLCNIC_INVALID_REG_VALUE)) {
				drv_state |= ((uint32_t)0x1 <<
				    (adapter->portnum * 4));

				if (((drv_state & 0x11111111) ==
				    (drv_active & 0x11111111)) ||
				    ((drv_active & 0x11111111) ==
				    ((drv_state >> 1) & 0x11111111))) {
					adapter->reset_ack_timeout = 0;
					device_init = 1;
					DPRINTF(DBG_INIT, (CE_NOTE,
					    "%s %s%d: All functions ready "
					    "for reset!\n",
					    __func__, adapter->name,
					    adapter->instance));
				}
			}

			/*
			 * Be defensive, if some other function has taken over
			 * the reset recovery, let it go through
			 */
			if ((dev_state != QLCNIC_INVALID_REG_VALUE) &&
			    (dev_state != QLCNIC_DEV_NEED_RESET)) {
				DPRINTF(DBG_INIT, (CE_NOTE,
				    "%s %s%d: Some other function overrode "
				    "init ...!\n",
				    __func__, adapter->name,
				    adapter->instance));
				device_init = 1;
			}
		} else {
			/*
			 * Some other device is responsible for reset, wait
			 * for change of state to other than DEV_NEED_RESET
			 */
			if ((dev_state != QLCNIC_INVALID_REG_VALUE) &&
			    (dev_state != QLCNIC_DEV_NEED_RESET)) {
				DPRINTF(DBG_INIT, (CE_NOTE,
				    "%s %s%d: Some other function has started "
				    "init ...!\n",
				    __func__, adapter->name,
				    adapter->instance));
				device_init = 1;
			}
		}

		/* check reset ack timeout, force initialization */
		if (++adapter->reset_ack_timeout >= MAX_RESET_ACK_TIMEOUT) {
			cmn_err(CE_WARN,
			    "%s%d: Reset Ack timeout, Forcing init..",
			    adapter->name, adapter->instance);
			device_init = 1;
		}

		if (device_init) {
			/* check dev_state and restart driver */
			mutex_enter(&adapter->lock);
			status = qlcnic_driver_start(adapter, 0x1, 1);
			if (status != DDI_SUCCESS) {
				cmn_err(CE_WARN, "%s%d: Failed to start driver",
				    adapter->name, adapter->instance);
				mutex_exit(&adapter->lock);
				return (1);
			}
			mutex_exit(&adapter->lock);
			mac_tx_update(adapter->mach);
		}
		return (0);
	} else if ((dev_state == QLCNIC_DEV_NEED_QUISCENT) ||
	    (dev_state == QLCNIC_DEV_NEED_RESET)) {
		DPRINTF(DBG_INIT, (CE_NOTE,
		    "%s%d: Dev state changed to %d, need_fw_reset %d\n",
		    adapter->name, adapter->instance, dev_state,
		    adapter->need_fw_reset));

		/*
		 * If qlflash forces a firrmware reset by forcing the
		 * device state to NEED RESET, handle it
		 */
		if ((!adapter->need_fw_reset) &&
		    (dev_state == QLCNIC_DEV_NEED_RESET)) {
			cmn_err(CE_WARN, "%s%d: Dev state set to need Reset"
			    " Force f/w reset!\n",
			    adapter->name, adapter->instance);
			DPRINTF(DBG_INIT, (CE_NOTE,
			    "%s %s%d: Dev state set to need Reset,"
			    " Force f/w reset!\n",
			    __func__, adapter->name, adapter->instance));
			adapter->force_fw_reset = 1;
		}
		DPRINTF(DBG_INIT, (CE_NOTE,
		    "%s %s%d: Preparing for reset!\n",
		    __func__, adapter->name, adapter->instance));
		if (adapter->drv_state != QLCNIC_DRV_DOWN)
			adapter->drv_state = QLCNIC_DRV_QUIESCENT;
		/* Clear reset ack timeout */
		adapter->reset_ack_timeout = 0;

		/* don't stop the watchdog timer */
		qlcnic_remove(adapter, 0x0);

		DPRINTF(DBG_INIT, (CE_NOTE,
		    "%s %s%d: Ready for reset!\n",
		    __func__, adapter->name, adapter->instance));

		/*
		 * Set our readiness to do ahead with the reset, if we are
		 * not responsible for COMPLETING the reset. else wait for
		 * everyone to acknowledge the reset.
		 */
		if (!adapter->own_reset) {
			if (qlcnic_api_lock(adapter)) {
				cmn_err(CE_WARN, "%s%d: Wait time for sem5 "
				    "exceeded",
				    adapter->name, adapter->instance);
				return (1);
			}

			/*
			 * Set our bit in the DRV_STATE register
			 * indicating we are ready for reset or quiescent
			 */
			adapter->qlcnic_hw_read_wx(adapter,
			    QLCNIC_CAM_RAM(QLCNIC_CRB_DRV_STATE), &val, 4);

			if (dev_state == QLCNIC_DEV_NEED_RESET)
				val |= ((uint32_t)0x1 <<
				    (adapter->portnum * 4));
			else
				val |= ((uint32_t)0x2 <<
				    (adapter->portnum * 4));

			adapter->qlcnic_hw_write_wx(adapter,
			    QLCNIC_CAM_RAM(QLCNIC_CRB_DRV_STATE), &val, 4);

			qlcnic_api_unlock(adapter);
		}
		return (0);
	}

	if (qlcnic_api_lock(adapter)) {
		cmn_err(CE_WARN, "%s%d: Wait time for sem5 exceeded",
		    adapter->name, adapter->instance);
		return (1);
	}

	if (adapter->drv_state != QLCNIC_DRV_DOWN) {
		if (adapter->need_fw_reset) {
			/* Read the most current device state */
			adapter->qlcnic_hw_read_wx(adapter,
			    QLCNIC_CAM_RAM(QLCNIC_CRB_DEV_STATE),
			    &dev_state, 4);

			if (dev_state == QLCNIC_INVALID_REG_VALUE) {
				cmn_err(CE_WARN,
				    "%s%d: Invalid device state value %x",
				    adapter->name, adapter->instance,
				    dev_state);
				qlcnic_api_unlock(adapter);
				return (1);
			}

			/*
			 * If some other function has not initiated the
			 * reset process do it now
			 */
			if ((dev_state != QLCNIC_DEV_NEED_RESET) &&
			    (dev_state != QLCNIC_DEV_INITALIZING)) {
				val = QLCNIC_DEV_NEED_RESET;
				adapter->qlcnic_hw_write_wx(adapter,
				    QLCNIC_CAM_RAM(QLCNIC_CRB_DEV_STATE),
				    &val, 4);

				/*
				 * If FCoE functions are around, let them take
				 * over the reset recovery, we wait for the
				 * firmware to get reset
				 */
				adapter->qlcnic_hw_read_wx(adapter,
				    QLCNIC_CAM_RAM(QLCNIC_CRB_DRV_ACTIVE),
				    &drv_active, 4);
				cmn_err(CE_WARN, "%s%d funcs active 0x%08x",
				    adapter->name, adapter->instance,
				    drv_active);
				if ((
				    (drv_active != QLCNIC_INVALID_REG_VALUE)) &&
				    (drv_active & 0x11000000)) {
					cmn_err(CE_WARN, "%s%d Other funcs "
					    "active 0x%08x, wait for reset "
					    "recovery",
					    adapter->name, adapter->instance,
					    drv_active);
					adapter->own_reset = 0;
				} else {
					cmn_err(CE_WARN,
					    "qlcnic%d need reset, state %d, "
					    "failcnt %d, own reset",
					    adapter->instance, dev_state,
					    adapter->fw_fail_cnt);
					adapter->own_reset = 1;
				}
			} else {
				cmn_err(CE_WARN,
				    "qlcnic%d wait for reset handling, "
				    "state %d",
				    adapter->instance, dev_state);
				adapter->own_reset = 0;
			}
			qlcnic_api_unlock(adapter);
			return (0);
		}

		adapter->qlcnic_hw_read_wx(adapter,
		    QLCNIC_CAM_RAM(QLCNIC_PEG_ALIVE_COUNTER), &heartbeat, 4);

		if (heartbeat != adapter->heartbeat) {
			adapter->heartbeat = heartbeat;
			adapter->fw_fail_cnt = 0;
			qlcnic_api_unlock(adapter);
			return (0);
		}

		if (++adapter->fw_fail_cnt < FW_FAIL_THRESH) {
			qlcnic_api_unlock(adapter);
			return (0);
		}

		cmn_err(CE_WARN, "qlcnic%d fw hang detected",
		    adapter->instance);
		adapter->need_fw_reset = 1;
		qlcnic_fm_ereport(adapter, DDI_FM_DEVICE_NO_RESPONSE);
		ddi_fm_service_impact(adapter->dip, DDI_SERVICE_DEGRADED);
	}
	qlcnic_api_unlock(adapter);
	return (1);
}
/*
 * FMA functions
 */
void
qlcnic_fm_ereport(qlcnic_adapter *adapter, char *detail)
{
	uint64_t ena;
	char buf[FM_MAX_CLASS];

	(void) snprintf(buf, FM_MAX_CLASS, "%s.%s", DDI_FM_DEVICE, detail);
	ena = fm_ena_generate(0, FM_ENA_FMT1);
	if (DDI_FM_EREPORT_CAP(adapter->fm_cap)) {
		ddi_fm_ereport_post(adapter->dip, buf, ena, DDI_NOSLEEP,
		    FM_VERSION, DATA_TYPE_UINT8, FM_EREPORT_VERS0, NULL);
	}
}
/* ARGSUSED */
int
qlcnic_check_acc_handle(qlcnic_adapter *adapter,
    ddi_acc_handle_t acc_handle)
{
	ddi_fm_error_t	acc_err;

	ddi_fm_acc_err_get(acc_handle, &acc_err, DDI_FME_VERSION);
	ddi_fm_acc_err_clear(acc_handle, DDI_FME_VERSION);

	return (acc_err.fme_status);
}
/* ARGSUSED */
int
qlcnic_check_dma_handle(qlcnic_adapter *adapter,
    ddi_dma_handle_t dma_handle)
{
	ddi_fm_error_t	dma_err;

	ddi_fm_dma_err_get(dma_handle, &dma_err, DDI_FME_VERSION);

	return (dma_err.fme_status);
}

/*
 * Print a formated string
 */
void
qlcnic_printf(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vcmn_err(CE_CONT, fmt, ap);
	va_end(ap);

}
/*
 * Print as 8bit bytes
 */
static uint32_t
qlcnic_dump_buf_8(uint8_t *bp, uint32_t count, uint32_t offset)
{
	switch (count) {
	case 1:
		qlcnic_printf("0x%016x : %02x\n",
		    offset,
		    *bp);
		break;

	case 2:
		qlcnic_printf("0x%016x : %02x %02x\n",
		    offset,
		    *bp, *(bp+1));
		break;

	case 3:
		qlcnic_printf("0x%016x : %02x %02x %02x\n",
		    offset,
		    *bp, *(bp+1), *(bp+2));
		break;

	case 4:
		qlcnic_printf("0x%016x : %02x %02x %02x %02x\n",
		    offset,
		    *bp, *(bp+1), *(bp+2), *(bp+3));
		break;

	case 5:
		qlcnic_printf("0x%016x : %02x %02x %02x %02x %02x\n",
		    offset,
		    *bp, *(bp+1), *(bp+2), *(bp+3), *(bp+4));
		break;

	case 6:
		qlcnic_printf("0x%016x : %02x %02x %02x %02x %02x %02x\n",
		    offset,
		    *bp, *(bp+1), *(bp+2), *(bp+3), *(bp+4), *(bp+5));
		break;

	case 7:
		qlcnic_printf("0x%016x : %02x %02x %02x %02x %02x %02x %02x\n",
		    offset,
		    *bp, *(bp+1), *(bp+2), *(bp+3), *(bp+4), *(bp+5), *(bp+6));
		break;

	default:
		qlcnic_printf(
		    "0x%016x : %02x %02x %02x %02x %02x %02x %02x %02x\n",
		    offset, *bp, *(bp+1), *(bp+2), *(bp+3), *(bp+4), *(bp+5),
		    *(bp+6), *(bp+7));
		break;

	}

	if (count < 8) {
		count = 0;
	} else {
		count -= 8;
	}

	return (count);
}

/*
 * Prints string plus buffer.
 */
void
qlcnic_dump_buf(char *string, uint8_t *buffer, uint8_t wd_size,
    uint32_t count)
{
	uint32_t offset = 0;

	if (strcmp(string, "") != 0)
		qlcnic_printf(string);

	if ((buffer == NULL) || (count == 0))
		return;

	switch (wd_size) {
	case 8:
		while (count) {
			count = qlcnic_dump_buf_8(buffer, count, offset);
			offset += 8;
			buffer += 8;
		}
		break;
	}

}
/*
 * Note: The caller must ensure that it is not holding
 * qlcnic_api_lock before calling this function.
 */
static void
qlcnic_force_fw_reset(qlcnic_adapter *adapter)
{
	uint32_t	val = 0;

	if (qlcnic_api_lock(adapter)) {
		cmn_err(CE_WARN, "%s%d: Wait time for sem5 "
		    "exceeded in qlcnic_force_fw_reset",
		    adapter->name, adapter->instance);
		return;
	}

	/* Read current dev state */
	adapter->qlcnic_hw_read_wx(adapter,
	    QLCNIC_CAM_RAM(QLCNIC_CRB_DEV_STATE), &val, 4);
	/* Force fw reset only if the dev_state is DEV_READY */
	if (val != QLCNIC_DEV_READY) {
		cmn_err(CE_WARN, "%s%d: dev state is not DEV_READY "
		    "current dev_state = %x", adapter->name,
		    adapter->instance, val);
		qlcnic_api_unlock(adapter);
		return;
	}

	val = QLCNIC_DEV_NEED_RESET;
	adapter->qlcnic_hw_write_wx(adapter,
	    QLCNIC_CAM_RAM(QLCNIC_CRB_DEV_STATE),
	    &val, 4);
	qlcnic_api_unlock(adapter);
}
