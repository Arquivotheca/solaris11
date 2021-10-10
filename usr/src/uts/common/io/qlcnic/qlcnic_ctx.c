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

#include "qlcnic_hw.h"
#include "qlcnic.h"
#include "qlcnic_phan_reg.h"
#include "qlcnic_cmn.h"

typedef unsigned int qlcnic_rcode_t;

#include "qlcnic_errorcode.h"
#include "qlcnic_interface.h"


#define	QLCNIC_OS_CRB_RETRY_COUNT	4000

int
qlcnic_api_lock(struct qlcnic_adapter_s *adapter)
{
	u32 done = 0, timeout = 0;

	for (;;) {
		/* Acquire PCIE HW semaphore5 */
		qlcnic_read_w0(adapter,
		    QLCNIC_PCIE_REG(PCIE_SEM5_LOCK), &done);

		if (done == 1)
			break;

		if (++timeout >= QLCNIC_OS_CRB_RETRY_COUNT) {
			cmn_err(CE_WARN, "%s: lock timeout.", __func__);
			return (-1);
		}

		drv_usecwait(1000);
	}

	return (0);
}

void
qlcnic_api_unlock(struct qlcnic_adapter_s *adapter)
{
	u32 val;

	/* Release PCIE HW semaphore5 */
	qlcnic_read_w0(adapter,
	    QLCNIC_PCIE_REG(PCIE_SEM5_UNLOCK), &val);
}

static u32
qlcnic_poll_rsp(struct qlcnic_adapter_s *adapter)
{
	u32 rsp = QLCNIC_CDRP_RSP_OK;
	int timeout = 0;

	do {
		/* give at least 1ms for firmware to respond */
		drv_usecwait(1000);

		if (++timeout > QLCNIC_OS_CRB_RETRY_COUNT)
			return (QLCNIC_CDRP_RSP_TIMEOUT);

		adapter->qlcnic_hw_read_wx(adapter, QLCNIC_CDRP_CRB_OFFSET,
		    &rsp, 4);

	} while (!QLCNIC_CDRP_IS_RSP(rsp));

	return (rsp);
}

u32
qlcnic_issue_cmd(struct qlcnic_adapter_s *adapter,
    u32 pci_fn, u32 version, u32 arg1, u32 arg2, u32 arg3, u32 cmd)
{
	u32 rsp;
	u32 signature = 0;
	u32 rcode = QLCNIC_RCODE_SUCCESS;

	signature = QLCNIC_CDRP_SIGNATURE_MAKE(pci_fn, version);

	/* Acquire semaphore before accessing CRB */
	if (qlcnic_api_lock(adapter))
		return (QLCNIC_RCODE_TIMEOUT);

	qlcnic_reg_write(adapter, QLCNIC_SIGN_CRB_OFFSET, signature);

	qlcnic_reg_write(adapter, QLCNIC_ARG1_CRB_OFFSET, arg1);

	qlcnic_reg_write(adapter, QLCNIC_ARG2_CRB_OFFSET, arg2);

	qlcnic_reg_write(adapter, QLCNIC_ARG3_CRB_OFFSET, arg3);

	qlcnic_reg_write(adapter, QLCNIC_CDRP_CRB_OFFSET,
	    QLCNIC_CDRP_FORM_CMD(cmd));

	rsp = qlcnic_poll_rsp(adapter);

	if (rsp == QLCNIC_CDRP_RSP_TIMEOUT) {
		cmn_err(CE_WARN, "%s: card response timeout.",
		    qlcnic_driver_name);

		rcode = QLCNIC_RCODE_TIMEOUT;
	} else if (rsp == QLCNIC_CDRP_RSP_FAIL) {
		adapter->qlcnic_hw_read_wx(adapter, QLCNIC_ARG1_CRB_OFFSET,
		    &rcode, 4);
		cmn_err(CE_WARN, "%s: failed card response code:0x%x",
		    qlcnic_driver_name, rcode);
	}

	/* Release semaphore */
	qlcnic_api_unlock(adapter);

	return (rcode);
}

int
qlcnic_fw_cmd_set_mtu(struct qlcnic_adapter_s *adapter, int mtu)
{
	u32 rcode = QLCNIC_RCODE_SUCCESS;
	struct qlcnic_recv_context_s *recv_ctx = &adapter->recv_ctx[0];

	if (recv_ctx->state == QLCNIC_HOST_CTX_STATE_ACTIVE)
		rcode = qlcnic_issue_cmd(adapter,
		    adapter->ahw.pci_func,
		    NXHAL_VERSION,
		    recv_ctx->context_id,
		    mtu,
		    0,
		    QLCNIC_CDRP_CMD_SET_MTU);

	if (rcode != QLCNIC_RCODE_SUCCESS) {
		cmn_err(CE_WARN, "set mtu %d failed", mtu);
		return (-EIO);
	}
	return (0);
}

static int
qlcnic_fw_cmd_create_rx_ctx(struct qlcnic_adapter_s *adapter)
{
	qlcnic_recv_context_t	*recv_ctx = &adapter->recv_ctx[0];
	qlcnic_hostrq_rx_ctx_t	*prq;
	qlcnic_cardrsp_rx_ctx_t	*prsp;
	qlcnic_hostrq_rds_ring_t	*prq_rds;
	qlcnic_hostrq_sds_ring_t	*prq_sds;
	qlcnic_cardrsp_rds_ring_t	*prsp_rds;
	qlcnic_cardrsp_sds_ring_t	*prsp_sds;
	qlcnic_rcv_desc_ctx_t	*rcv_desc;
	ddi_dma_cookie_t	cookie;
	ddi_dma_handle_t	rqdhdl, rsdhdl;
	ddi_acc_handle_t	rqahdl, rsahdl;
	uint64_t		hostrq_phys_addr, cardrsp_phys_addr;
	u64			phys_addr;
	u32			cap, reg;
	size_t			rq_size, rsp_size;
	void			*addr;
	int			i, nrds_rings, nsds_rings, err;
	struct	qlcnic_sds_ring_s *sds_ring;

	nrds_rings = adapter->max_rds_rings;
	nsds_rings = adapter->max_sds_rings;

	rq_size =
	    SIZEOF_HOSTRQ_RX(qlcnic_hostrq_rx_ctx_t, nrds_rings, nsds_rings);
	rsp_size =
	    SIZEOF_CARDRSP_RX(qlcnic_cardrsp_rx_ctx_t, nrds_rings, nsds_rings);

	if (qlcnic_pci_alloc_consistent(adapter, rq_size, (caddr_t *)&addr,
	    &cookie, &rqdhdl, &rqahdl) != DDI_SUCCESS)
		return (-ENOMEM);
	hostrq_phys_addr = cookie.dmac_laddress;
	prq = (qlcnic_hostrq_rx_ctx_t *)addr;

	if (qlcnic_pci_alloc_consistent(adapter, rsp_size, (caddr_t *)&addr,
	    &cookie, &rsdhdl, &rsahdl) != DDI_SUCCESS) {
		err = -ENOMEM;
		goto out_free_rq;
	}
	cardrsp_phys_addr = cookie.dmac_laddress;
	prsp = (qlcnic_cardrsp_rx_ctx_t *)addr;

	prq->host_rsp_dma_addr = HOST_TO_LE_64(cardrsp_phys_addr);

	cap = (QLCNIC_CAP0_LEGACY_CONTEXT | QLCNIC_CAP0_LEGACY_MN);
	cap |= (QLCNIC_CAP0_JUMBO_CONTIGUOUS);

	prq->capabilities[0] = HOST_TO_LE_32(cap);
	prq->host_int_crb_mode =
	    HOST_TO_LE_32(QLCNIC_HOST_INT_CRB_MODE_SHARED);
	prq->host_rds_crb_mode =
	    HOST_TO_LE_32(QLCNIC_HOST_RDS_CRB_MODE_UNIQUE);

	prq->num_rds_rings = HOST_TO_LE_16(nrds_rings);
	prq->num_sds_rings = HOST_TO_LE_16(nsds_rings);
	prq->rds_ring_offset = 0;
	prq->sds_ring_offset = prq->rds_ring_offset +
	    (sizeof (qlcnic_hostrq_rds_ring_t) * nrds_rings);

	prq_rds = (qlcnic_hostrq_rds_ring_t *)(uintptr_t)((char *)prq +
	    sizeof (*prq) + prq->rds_ring_offset);

	for (i = 0; i < nrds_rings; i++) {
		rcv_desc = recv_ctx->rcv_desc[i];

		prq_rds[i].host_phys_addr = HOST_TO_LE_64(rcv_desc->phys_addr);
		prq_rds[i].ring_size = HOST_TO_LE_32(rcv_desc->MaxRxDescCount);
		prq_rds[i].ring_kind = HOST_TO_LE_32(i);
		prq_rds[i].buff_size = HOST_TO_LE_64((u64)rcv_desc->dma_size);
	}

	prq_sds = (qlcnic_hostrq_sds_ring_t *)(uintptr_t)((char *)prq +
	    sizeof (*prq) + prq->sds_ring_offset);

	for (i = 0; i < adapter->max_sds_rings; i++) {
		sds_ring = &recv_ctx->sds_ring[i];

		prq_sds[i].host_phys_addr =
		    HOST_TO_LE_64(sds_ring->rcvStatusDesc_physAddr);

		prq_sds[i].ring_size =
		    HOST_TO_LE_32(sds_ring->max_status_desc_count);
		/*
		 * Todo: fix for mismatch if interrupt count is not equal to
		 * no of rings
		 */
		prq_sds[i].msi_index = HOST_TO_LE_16(i);
	}

	/* now byteswap offsets */
	prq->rds_ring_offset = HOST_TO_LE_32(prq->rds_ring_offset);
	prq->sds_ring_offset = HOST_TO_LE_32(prq->sds_ring_offset);

	phys_addr = hostrq_phys_addr;
	err = qlcnic_issue_cmd(adapter,
	    adapter->ahw.pci_func,
	    NXHAL_VERSION,
	    (u32)(phys_addr >> 32),
	    (u32)(phys_addr & 0xffffffff),
	    rq_size,
	    QLCNIC_CDRP_CMD_CREATE_RX_CTX);
	if (err) {
		cmn_err(CE_WARN, "Failed to create rx ctx in firmware%d",
		    err);
		goto out_free_rsp;
	}


	prsp_rds = (qlcnic_cardrsp_rds_ring_t *)(uintptr_t)((char *)prsp +
	    sizeof (*prsp) + LE_TO_HOST_32(prsp->rds_ring_offset));

	for (i = 0; i < LE_TO_HOST_16(prsp->num_rds_rings); i++) {
		rcv_desc = recv_ctx->rcv_desc[i];

		reg = LE_TO_HOST_32(prsp_rds[i].host_producer_crb);
		rcv_desc->host_rx_producer_addr = qlcnic_get_ioaddr(adapter,
		    QLCNIC_REG(reg - 0x200));
	}

	prsp_sds = (qlcnic_cardrsp_sds_ring_t *)(uintptr_t)((char *)prsp +
	    sizeof (*prsp) + LE_TO_HOST_32(prsp->sds_ring_offset));

	for (i = 0; i < adapter->max_sds_rings; i++) {
		sds_ring = &recv_ctx->sds_ring[i];

		reg = LE_TO_HOST_32(prsp_sds[i].host_consumer_crb);

		sds_ring->host_sds_consumer_addr = qlcnic_get_ioaddr(adapter,
		    QLCNIC_REG(reg - 0x200));

		reg = LE_TO_HOST_32(prsp_sds[i].interrupt_crb);

		sds_ring->interrupt_crb_addr = qlcnic_get_ioaddr(adapter,
		    QLCNIC_REG(reg - 0x200));
	}

	recv_ctx->state = LE_TO_HOST_32(prsp->host_ctx_state);
	recv_ctx->context_id = LE_TO_HOST_16(prsp->context_id);
	recv_ctx->virt_port = prsp->virt_port;

out_free_rsp:
	qlcnic_pci_free_consistent(&rsdhdl, &rsahdl);
out_free_rq:
	qlcnic_pci_free_consistent(&rqdhdl, &rqahdl);
	return (err);
}

static void
qlcnic_fw_cmd_destroy_rx_ctx(struct qlcnic_adapter_s *adapter)
{
	struct qlcnic_recv_context_s *recv_ctx = &adapter->recv_ctx[0];

	if (qlcnic_issue_cmd(adapter,
	    adapter->ahw.pci_func,
	    NXHAL_VERSION,
	    recv_ctx->context_id,
	    QLCNIC_DESTROY_CTX_RESET,
	    0,
	    QLCNIC_CDRP_CMD_DESTROY_RX_CTX)) {

		cmn_err(CE_WARN, "%s: Failed to destroy rx ctx in firmware",
		    qlcnic_driver_name);
	}
}

static int
qlcnic_fw_cmd_create_one_tx_ctx(struct qlcnic_adapter_s *adapter,
    struct qlcnic_tx_ring_s *tx_ring)
{
	qlcnic_hostrq_tx_ctx_t *prq;
	qlcnic_hostrq_cds_ring_t *prq_cds;
	qlcnic_cardrsp_tx_ctx_t *prsp;
	ddi_dma_cookie_t cookie;
	ddi_dma_handle_t rqdhdl, rsdhdl;
	ddi_acc_handle_t rqahdl, rsahdl;
	void *rq_addr, *rsp_addr;
	size_t rq_size, rsp_size;
	u32 temp;
	int err = 0;
	u64 offset, phys_addr;
	uint64_t rq_phys_addr, rsp_phys_addr;

	rq_size = SIZEOF_HOSTRQ_TX(qlcnic_hostrq_tx_ctx_t);
	if (qlcnic_pci_alloc_consistent(adapter, rq_size, (caddr_t *)&rq_addr,
	    &cookie, &rqdhdl, &rqahdl) != DDI_SUCCESS)
		return (-ENOMEM);
	rq_phys_addr = cookie.dmac_laddress;

	rsp_size = SIZEOF_CARDRSP_TX(qlcnic_cardrsp_tx_ctx_t);
	if (qlcnic_pci_alloc_consistent(adapter, rsp_size, (caddr_t *)&rsp_addr,
	    &cookie, &rsdhdl, &rsahdl) != DDI_SUCCESS) {
		err = -ENOMEM;
		goto out_free_rq;
	}
	rsp_phys_addr = cookie.dmac_laddress;

	(void) memset(rq_addr, 0, rq_size);
	prq = (qlcnic_hostrq_tx_ctx_t *)rq_addr;

	(void) memset(rsp_addr, 0, rsp_size);
	prsp = (qlcnic_cardrsp_tx_ctx_t *)rsp_addr;

	prq->host_rsp_dma_addr = HOST_TO_LE_64(rsp_phys_addr);

	temp = (QLCNIC_CAP0_LEGACY_CONTEXT | QLCNIC_CAP0_LEGACY_MN);
	prq->capabilities[0] = HOST_TO_LE_32(temp);

	prq->host_int_crb_mode =
	    HOST_TO_LE_32(QLCNIC_HOST_INT_CRB_MODE_SHARED);

	prq->interrupt_ctl = 0;
	prq->msi_index = tx_ring->index;

	prq->dummy_dma_addr = HOST_TO_LE_64(adapter->dummy_dma.phys_addr);

	offset = tx_ring->cmd_consumer_phys;
	prq->cmd_cons_dma_addr = HOST_TO_LE_64(offset);

	prq_cds = &prq->cds_ring;

	prq_cds->host_phys_addr =
	    HOST_TO_LE_64(tx_ring->cmdDesc_physAddr);

	prq_cds->ring_size = HOST_TO_LE_32(adapter->MaxTxDescCount);

	phys_addr = rq_phys_addr;
	err = qlcnic_issue_cmd(adapter,
	    adapter->ahw.pci_func,
	    NXHAL_VERSION,
	    (u32)(phys_addr >> 32),
	    ((u32)phys_addr & 0xffffffff),
	    rq_size,
	    QLCNIC_CDRP_CMD_CREATE_TX_CTX);

	if (err == QLCNIC_RCODE_SUCCESS) {
		temp = LE_TO_HOST_32(prsp->cds_ring.host_producer_crb);
		tx_ring->crb_addr_cmd_producer = qlcnic_get_ioaddr(adapter,
		    QLCNIC_REG(temp - 0x200));
		tx_ring->tx_context_id =
		    LE_TO_HOST_16(prsp->context_id);
	} else {
		err = -EIO;
	}
	qlcnic_pci_free_consistent(&rsdhdl, &rsahdl);

out_free_rq:
	qlcnic_pci_free_consistent(&rqdhdl, &rqahdl);

	return (err);
}

static int
qlcnic_fw_cmd_create_tx_ctx(struct qlcnic_adapter_s *adapter)
{
	struct qlcnic_tx_ring_s *tx_ring;
	int i;
	int ret = DDI_SUCCESS;

	for (i = 0; i < adapter->max_tx_rings; i++) {
		tx_ring = &adapter->tx_ring[i];
		ret = qlcnic_fw_cmd_create_one_tx_ctx(adapter, tx_ring);
		if (ret != DDI_SUCCESS) {
			/*
			 * If failed to create a tx ctx, but at least one tx ctx
			 * has been successfully created, then continue.
			 */
			if (i) {
				adapter->max_tx_rings = i;
				cmn_err(CE_NOTE,
				    "qlcnic(%d) fw forces tx ring number to "
				    "%d\n",
				    adapter->instance, i);
				return (DDI_SUCCESS);
			} else {
				cmn_err(CE_WARN,
				    "qlcnic(%d) Failed to create tx ring%d ctx "
				    "in firmware %d",
				    adapter->instance, i, ret);
				goto error;
			}
		}
	}

	return (ret);
error:
	/* code to free previously allocated tx ctx */
	return (ret);
}

static void
qlcnic_fw_cmd_destroy_tx_ctx(struct qlcnic_adapter_s *adapter)
{
	struct qlcnic_tx_ring_s *tx_ring;
	int i, err;

	for (i = 0; i < adapter->max_tx_rings; i++) {
		tx_ring = &adapter->tx_ring[i];
		err = qlcnic_issue_cmd(adapter,
		    adapter->ahw.pci_func,
		    NXHAL_VERSION,
		    tx_ring->tx_context_id,
		    QLCNIC_DESTROY_CTX_RESET,
		    0,
		    QLCNIC_CDRP_CMD_DESTROY_TX_CTX);
		if (err) {
			cmn_err(CE_WARN,
			    "%s: Failed to destroy tx(%d) ctx in firmware,"
			    " tx_context_id %x, err %x",
			    qlcnic_driver_name, i, tx_ring->tx_context_id, err);
		}
	}
}

void
qlcnic_destroy_rxtx_ctx(struct qlcnic_adapter_s *adapter)
{
	if (adapter->fw_major >= 4) {
		qlcnic_fw_cmd_destroy_tx_ctx(adapter);
		qlcnic_fw_cmd_destroy_rx_ctx(adapter);
	}
}

int
qlcnic_create_rxtx_ctx(struct qlcnic_adapter_s *adapter)
{
	int err;

		err = qlcnic_fw_cmd_create_rx_ctx(adapter);
		if (err)
			return (err);
		err = qlcnic_fw_cmd_create_tx_ctx(adapter);
		if (err)
			qlcnic_fw_cmd_destroy_rx_ctx(adapter);
		return (err);

}
