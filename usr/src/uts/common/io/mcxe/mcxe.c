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
 *  Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copyright (c) 2010 Mellanox Technologies. All rights reserved.
 */

#include "mcxe.h"

static char mcxe_ident[] = "Mellanox ConnectX-2 10 Gigbit Ethernet Driver";

static void *mcxe_statep;
int mcxe_verbose = 0;

#define	MCXE_DMA_RX_ALIGNMENT	0x0000000000001000ull	/* 4K */

const char *setportstr[] = {
	[MCXE_SET_PORT_GENERAL] = "port general",
	[MCXE_SET_PORT_RQP_CALC] = "rqp calc",
	[MCXE_SET_PORT_MAC_TABLE] = "mac table",
	[MCXE_SET_PORT_VLAN_TABLE] = "vlan table",
	[MCXE_SET_PORT_PRIO_MAP] = "prio map"
};

static ddi_dma_attr_t mcxe_dma_buf_attr = {
	DMA_ATTR_V0,			/* version number */
	0x0000000000000000ull,		/* low address */
	0xFFFFFFFFFFFFFFFFull,		/* high address */
	0x00000000FFFFFFFFull,		/* dma counter max */
	MCXE_DMA_RX_ALIGNMENT,		/* alignment */
	0x00000FFF,			/* burst sizes */
	0x00000001,			/* minimum transfer size */
	0x00000000FFFFFFFFull,		/* maximum transfer size */
	0xFFFFFFFFFFFFFFFFull,		/* maximum segment size */
	1,				/* scatter/gather list length */
	0x00000001,			/* granularity */
	DDI_DMA_FLAGERR			/* DMA flags */
};

static ddi_dma_attr_t mcxe_dma_tx_attr = {
	DMA_ATTR_V0,			/* version number */
	0x0000000000000000ull,		/* low address */
	0xFFFFFFFFFFFFFFFFull,		/* high address */
	0x00000000FFFFFFFFull,		/* dma counter max */
	1,				/* alignment */
	0x00000FFF,			/* burst sizes */
	0x00000001,			/* minimum transfer size */
	0x00000000FFFFFFFFull,		/* maximum transfer size */
	0xFFFFFFFFFFFFFFFFull,		/* maximum segment size  */
	MCXE_MAX_COOKIE,		/* scatter/gather list length */
	0x00000001,			/* granularity */
	DDI_DMA_FLAGERR			/* DMA flags */
};

/*
 * DMA access attributes for buffers.
 */
static ddi_device_acc_attr_t mcxe_buf_acc_attr = {
	DDI_DEVICE_ATTR_V0,
	DDI_NEVERSWAP_ACC,
	DDI_STRICTORDER_ACC
};

/*
 * Get the corresponding mcxe port pointer from its mcxnex state.
 */
static mcxe_port_t *
mcxe_state_to_port(mcxnex_state_t *mcxnex_state, int port_num)
{
	dev_info_t *dc;
	int port_inst;
	mcxe_port_t *port;

	for (dc = ddi_get_child(mcxnex_state->hs_dip); dc != NULL;
	    dc = ddi_get_next_sibling(dc)) {

		port_inst = ddi_get_instance(dc);
		port = ddi_get_soft_state(mcxe_statep, port_inst);

		if (port == NULL)
			continue;

		if (port->phys_port_num == port_num)
			return (port);
	}

	return (NULL);
}

static void
mcxe_free_dma_buffer(dma_buffer_t *buf)
{
	if (buf->dma_handle != NULL) {
		if (buf->dma_address) {
			(void) ddi_dma_unbind_handle(buf->dma_handle);
			buf->dma_address = 0;
		}
		ddi_dma_free_handle(&buf->dma_handle);
		buf->dma_handle = NULL;
	}

	if (buf->acc_handle != NULL) {
		ddi_dma_mem_free(&buf->acc_handle);
		buf->acc_handle = NULL;
	}

	buf->size = 0;
	buf->len = 0;
}

static int
mcxe_alloc_dma_buffer(mcxe_port_t *port, dma_buffer_t *buf, size_t size)
{
	dev_info_t *devinfo = port->devinfo;
	ddi_dma_cookie_t cookie;
	size_t len;
	uint_t cookie_num;
	int ret;

	ret = ddi_dma_alloc_handle(devinfo,
	    &mcxe_dma_buf_attr, DDI_DMA_DONTWAIT,
	    NULL, &buf->dma_handle);
	if (ret != DDI_SUCCESS) {
		buf->dma_handle = NULL;
		cmn_err(CE_WARN, "!failed to allocate dma handle.");
		return (ret);
	}

	ret = ddi_dma_mem_alloc(buf->dma_handle,
	    size, &mcxe_buf_acc_attr, DDI_DMA_STREAMING,
	    DDI_DMA_DONTWAIT, NULL, &buf->address,
	    &len, &buf->acc_handle);
	if (ret != DDI_SUCCESS) {
		buf->acc_handle = NULL;
		ddi_dma_free_handle(&buf->dma_handle);
		buf->dma_handle = NULL;
		cmn_err(CE_WARN, "!failed to allocate dma memory.");
		return (ret);
	}

	ret = ddi_dma_addr_bind_handle(buf->dma_handle, NULL,
	    buf->address,
	    len, DDI_DMA_RDWR | DDI_DMA_STREAMING,
	    DDI_DMA_DONTWAIT, NULL, &cookie, &cookie_num);
	if (ret != DDI_DMA_MAPPED) {
		buf->dma_address = NULL;
		ddi_dma_mem_free(&buf->acc_handle);
		buf->acc_handle = NULL;
		ddi_dma_free_handle(&buf->dma_handle);
		buf->dma_handle = NULL;
		cmn_err(CE_WARN, "!failed to bind dma handle(ret=%x).", ret);
		return (ret);
	}

	ASSERT(cookie_num == 1);

	buf->dma_address = cookie.dmac_laddress;
	buf->size = len;
	buf->len = 0;

	return (DDI_SUCCESS);
}

static int
mcxe_alloc_tx_qp(struct mcxe_tx_ring *tx_ring)
{
	mcxe_port_t *port = tx_ring->port;
	ibt_qp_alloc_attr_t qp_attr;
	mcxnex_qp_info_t qp_info;
	ib_qpn_t qpn;
	int ret;

	bzero(&qp_attr, sizeof (qp_attr));
	bzero(&qp_info, sizeof (qp_info));
	bzero(&qpn, sizeof (qpn));

	qp_info.qpi_qpn = &qpn;
	qp_info.qpi_attrp = &qp_attr;

	/* setup the qp attrs for the alloc call */
	qp_attr.qp_ibc_scq_hdl = (ibt_opaque1_t)tx_ring->tx_cq;
	qp_attr.qp_ibc_rcq_hdl = (ibt_opaque1_t)tx_ring->tx_cq;
	qp_attr.qp_pd_hdl = (ibt_pd_hdl_t)port->mcxnex_state->hs_pdhdl_internal;
	qp_attr.qp_sizes.cs_sq_sgl = port->max_tx_frags;
	qp_attr.qp_sizes.cs_rq_sgl = 1;
	qp_attr.qp_sizes.cs_sq = port->tx_ring_size;
	qp_attr.qp_sizes.cs_rq = 1;
	qp_attr.qp_flags = IBT_ALL_SIGNALED | IBT_FAST_REG_RES_LKEY;
	qp_attr.qp_alloc_flags = IBT_QP_NO_FLAGS;
	qp_info.qpi_type = IBT_RAWETHER_RQP;

	ret = mcxnex_qp_alloc(port->mcxnex_state, &qp_info, DDI_NOSLEEP);
	if (ret != DDI_SUCCESS) {
		cmn_err(CE_WARN, "!mcxe_alloc_tx_qp() failed(ret=%d).", ret);
		return (DDI_FAILURE);
	}

	tx_ring->tx_qp = qp_info.qpi_qphdl;

	if (mcxe_verbose) {
		cmn_err(CE_NOTE, "!mcxe%d: created tx qp %x\n",
		    port->instance, tx_ring->tx_qp->qp_qpnum);
	}

	return (DDI_SUCCESS);
}

static int
mcxe_alloc_rx_qp(struct mcxe_rx_ring *rx_ring)
{
	mcxe_port_t *port = rx_ring->port;
	ibt_qp_alloc_attr_t qp_attr;
	mcxnex_qp_info_t qp_info;
	ib_qpn_t qpn;
	int ret;

	bzero(&qp_attr, sizeof (qp_attr));
	bzero(&qp_info, sizeof (qp_info));
	bzero(&qpn, sizeof (qpn));

	qp_info.qpi_qpn = &qpn;
	qp_info.qpi_attrp = &qp_attr;

	/* setup the qp attrs for the alloc call */
	qp_attr.qp_ibc_scq_hdl = (ibt_opaque1_t)rx_ring->rx_cq;
	qp_attr.qp_ibc_rcq_hdl = (ibt_opaque1_t)rx_ring->rx_cq;
	qp_attr.qp_pd_hdl = (ibt_pd_hdl_t)port->mcxnex_state->hs_pdhdl_internal;
	qp_attr.qp_sizes.cs_sq_sgl = 1;
	qp_attr.qp_sizes.cs_rq_sgl = 1;
	qp_attr.qp_sizes.cs_sq = 1;
	qp_attr.qp_sizes.cs_rq = port->rx_ring_size;
	qp_attr.qp_flags = IBT_ALL_SIGNALED | IBT_FAST_REG_RES_LKEY;
	qp_attr.qp_alloc_flags = IBT_QP_LINK_TYPE_ETH;
	if (port->mcxnex_state->hs_vlan_strip_off_cap)
		qp_attr.qp_alloc_flags |= IBT_QP_VLAN_STRIP_OFF;
	qp_info.qpi_type = IBT_RAWETHER_RQP;

	ret = mcxnex_qp_alloc(port->mcxnex_state, &qp_info, DDI_NOSLEEP);
	if (ret != DDI_SUCCESS) {
		cmn_err(CE_WARN, "!mcxe_alloc_rx_qp() failed(ret=%d).", ret);
		return (DDI_FAILURE);
	}

	rx_ring->rx_qp = qp_info.qpi_qphdl;

	if (mcxe_verbose) {
		cmn_err(CE_NOTE, "!mcxe%d: created rx qp %x\n",
		    port->instance, rx_ring->rx_qp->qp_qpnum);
	}
	return (DDI_SUCCESS);
}

static int
mcxe_disable_qp(struct mcxe_port *port, mcxnex_qphdl_t qphdl)
{
	int ret;
	ibt_cep_modify_flags_t qp_mod_flags;
	ibt_qp_info_t qp_info;

	bzero(&qp_info, sizeof (qp_info));

	qp_info.qp_transport.ud.ud_port = port->phys_port_num;
	qp_info.qp_trans = IBT_RAWETHER_SRV;
	qp_mod_flags = IBT_CEP_SET_STATE;

	/* QP state ==> Reset */
	qp_info.qp_current_state = IBT_STATE_RTS;
	qp_info.qp_state = IBT_STATE_RESET;
	ret = mcxnex_qp_modify(port->mcxnex_state, qphdl, qp_mod_flags,
	    &qp_info, NULL);
	if (ret) {
		cmn_err(CE_WARN, "!mcxe_disable_qp: failed to modify qp 0x%06x"
		    " to RESET state(ret=%d).", qphdl->qp_qpnum, ret);
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

static int
mcxe_enable_qp(mcxe_port_t *port, mcxnex_qphdl_t qphdl)
{
	int ret;
	ibt_cep_modify_flags_t qp_mod_flags;
	ibt_qp_info_t qp_info;

	bzero(&qp_info, sizeof (qp_info));
	qp_info.qp_transport.ud.ud_port = port->phys_port_num;
	qp_info.qp_trans = IBT_RAWETHER_SRV;

	/* QP state ==> INIT */
	qp_mod_flags = IBT_CEP_SET_RESET_INIT | IBT_CEP_SET_STATE |
	    IBT_CEP_SET_PORT;
	qp_info.qp_current_state = IBT_STATE_RESET;
	qp_info.qp_state = IBT_STATE_INIT;
	ret = mcxnex_qp_modify(port->mcxnex_state, qphdl, qp_mod_flags,
	    &qp_info, NULL);
	if (ret) {
		cmn_err(CE_WARN, "!mcxe_enable_qp: failed to modify qp 0x%06x"
		    " to INIT state(ret=%d).", qphdl->qp_qpnum, ret);
		return (DDI_FAILURE);
	}

	/* QP state ==> RTR */
	qp_mod_flags = IBT_CEP_SET_STATE | IBT_CEP_SET_INIT_RTR;
	qp_info.qp_current_state = IBT_STATE_INIT;
	qp_info.qp_state = IBT_STATE_RTR;
	ret = mcxnex_qp_modify(port->mcxnex_state, qphdl, qp_mod_flags,
	    &qp_info, NULL);
	if (ret) {
		cmn_err(CE_WARN, "!mcxe_enable_qp: failed to modify qp 0x%06x"
		    " to RTR state(ret=%d).", qphdl->qp_qpnum, ret);
		return (DDI_FAILURE);
	}

	/* QP state ==> RTS */
	qp_mod_flags = IBT_CEP_SET_STATE | IBT_CEP_SET_RTR_RTS;
	qp_info.qp_current_state = IBT_STATE_RTR;
	qp_info.qp_state = IBT_STATE_RTS;
	ret = mcxnex_qp_modify(port->mcxnex_state, qphdl, qp_mod_flags,
	    &qp_info, NULL);
	if (ret) {
		cmn_err(CE_WARN, "!mcxe_enable_qp: failed to modify qp 0x%06x"
		    " to RTS state(ret=%d).", qphdl->qp_qpnum, ret);
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

/*
 * Post Rx buffer free callback routine after mcxe_m_stop.
 */
void
mcxe_post_rxb_free(struct mcxe_rxbuf *rxb)
{
	struct mcxe_rx_ring *rx_ring = rxb->rx_ring;
	mcxe_port_t *port = rx_ring->port;

	rw_enter(&rx_ring->rc_rwlock, RW_WRITER);
	atomic_dec_32(&rxb->ref_cnt);
	atomic_dec_32(&rx_ring->rxb_pending);
	atomic_dec_32(&port->rxb_pending);
	if (rxb->flag & MCXE_RXB_REUSED) {
		rxb->mp = desballoc((unsigned char *)rxb->dma_buf.address,
		    rxb->dma_buf.size, 0, &rxb->rxb_free_rtn);
		if (rxb->flag & MCXE_RXB_STARTED) {
			(void) mcxe_post_recv(rxb);
			rxb->flag = 0;	/* clear the flag */
		}
		rw_exit(&rx_ring->rc_rwlock);
		return;
	}
	rxb->mp = NULL;
	mcxe_free_dma_buffer(&rxb->dma_buf);
	if (!rx_ring->rxb_pending && rx_ring->rxbuf) {
		kmem_free(rx_ring->rxbuf, port->rx_ring_size *
		    sizeof (struct mcxe_rxbuf));
		rx_ring->rxbuf = NULL;
	}

	if (mcxe_verbose) {
		cmn_err(CE_NOTE,
		    "!mcxe%d: mcxe_post_rxb_free() rxb %d, pending = %d.\n",
		    port->instance, rxb->index, rx_ring->rxb_pending);
	}
	rw_exit(&rx_ring->rc_rwlock);
}

/*
 * ====== h/w operation functions ======
 * Note: mcxe only owns one of two ports of the chip, while
 * mcxnex owns the whole chip. Below h/w functions are port-based.
 */

/*
 * dump the h/w statistics.
 */
static int
mcxe_hw_get_stats(mcxe_port_t *port, uint8_t reset)
{
	int ret;
	uint32_t in_mod;

	ASSERT(mutex_owned(&port->port_lock));

	in_mod = reset << 8 | port->phys_port_num;

	ret = mcxnex_cmn_query_cmd_post(port->mcxnex_state,
	    EN_DUMP_ETH_STATS, 0, in_mod, &port->hw_stats,
	    sizeof (struct mcxe_hw_stats), DDI_SLEEP);
	if (ret != MCXNEX_CMD_SUCCESS) {
		cmn_err(CE_WARN, "!mcxe%d: EN_DUMP_ETH_STATS (port %02d) "
		    "command failed: %08x", port->instance,
		    port->phys_port_num, ret);
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

/*
 * mcxe_set_port_gen_cmd_post()
 */
static int
mcxe_set_port_cmd_post(mcxnex_state_t *mcxnex_state, uint_t port, uint16_t op,
    uint16_t mbx_sz, int entry_sz, void *port_gen, uint_t sleepflag)
{
	mcxnex_mbox_info_t mbox_info;
	mcxnex_cmd_post_t cmd;
	uint64_t data;
	int ret, i;

	bzero(&cmd, sizeof (cmd));

	/* Get an "In" mailbox for the command */
	mbox_info.mbi_alloc_flags = MCXNEX_ALLOC_INMBOX;
	ret = mcxnex_mbox_alloc(mcxnex_state, &mbox_info, sleepflag);
	if (ret != MCXNEX_CMD_SUCCESS) {
		cmn_err(CE_WARN,
		    "!mcxe_set_port_cmd_post: alloc mbox failed(status=%d).",
		    ret);
		return (DDI_FAILURE);
	}

	/* Copy the "INIT_PORT" command into the mailbox */
	for (i = 0; i < (mbx_sz >> entry_sz); i++) {
		if (entry_sz == 2) {
			data = ((uint32_t *)port_gen)[i];
			ddi_put32(mbox_info.mbi_in->mb_acchdl,
			    ((uint32_t *)mbox_info.mbi_in->mb_addr + i), data);
		} else {
			data = ((uint64_t *)port_gen)[i];
			ddi_put64(mbox_info.mbi_in->mb_acchdl,
			    ((uint64_t *)mbox_info.mbi_in->mb_addr + i), data);
		}
	}

	(void) ddi_dma_sync(mbox_info.mbi_in->mb_rsrcptr->hr_dmahdl,
	    0, mbx_sz, DDI_DMA_SYNC_FORDEV);

	/* Setup and post the mcxnex "SET_PORT" command */
	cmd.cp_inparm = mbox_info.mbi_in->mb_mapaddr;
	cmd.cp_outparm = 0;
	cmd.cp_inmod = op << 8 | port;
	cmd.cp_opcode = SET_PORT;
	cmd.cp_opmod = 1;
	cmd.cp_flags = sleepflag;
	ret = mcxnex_cmd_post(mcxnex_state, &cmd);
	/* Free the mailbox */
	mcxnex_mbox_free(mcxnex_state, &mbox_info);
	if (ret != MCXNEX_CMD_SUCCESS) {
		cmn_err(CE_WARN,
		    "!mcxe_set_port_cmd_post: SET_PORT OP=\"%s\" "
		    "failed(status=%d).", setportstr[op], ret);
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

/*
 * mcxe_set_rqpn_calc()
 */
static int
mcxe_set_rqpn_calc(mcxe_port_t *port, uint32_t promisc)
{
	int ret;
	uint32_t base_qpn;
	mcxe_hw_set_port_rqp_calc_cx_t rqp_calc;
	int port_num = port->phys_port_num;

	/* currently only support one rx ring */
	base_qpn = port->rx_rings[0].rx_qp->qp_qpnum;

	bzero(&rqp_calc, sizeof (rqp_calc));
	rqp_calc.base_qpn = base_qpn;
	rqp_calc.promisc = (promisc << SET_PORT_PROMISC_SHIFT) | base_qpn;
	rqp_calc.mcast = (1ULL << SET_PORT_PROMISC_SHIFT) | base_qpn;

	ret = mcxe_set_port_cmd_post(port->mcxnex_state, port_num,
	    MCXE_SET_PORT_RQP_CALC, sizeof (rqp_calc), 2,
	    &rqp_calc, DDI_SLEEP);
	if (ret != DDI_SUCCESS) {
		cmn_err(CE_WARN, "!mcxe%d: mcxe_set_rqpn_calc() failed.",
		    port->instance);
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

/*
 * mcxe_set_vlan_filter()
 *	set up port VLAN filter table.
 */
static int
mcxe_set_vlan_filter(mcxe_port_t *port)
{
	mcxnex_state_t *mcxnex_state = port->mcxnex_state;
	mcxnex_mbox_info_t mbox_info;
	mcxnex_cmd_post_t cmd;
	int ret, i;

	/* Get an "In" mailbox for the command */
	mbox_info.mbi_alloc_flags = MCXNEX_ALLOC_INMBOX;
	ret = mcxnex_mbox_alloc(mcxnex_state, &mbox_info, DDI_SLEEP);
	if (ret != MCXNEX_CMD_SUCCESS) {
		cmn_err(CE_WARN,
		    "!mcxe_set_vlan_filter: alloc mbox failed(status=%d).",
		    ret);
		return (DDI_FAILURE);
	}

	/* clear all filters */
	for (i = 0; i < VLAN_FLTR_SIZE; i++) {
		ddi_put32(mbox_info.mbi_in->mb_acchdl,
		    ((uint32_t *)mbox_info.mbi_in->mb_addr + i), 0xffffffff);
	}

	(void) ddi_dma_sync(mbox_info.mbi_in->mb_rsrcptr->hr_dmahdl,
	    0, VLAN_FLTR_SIZE * 4, DDI_DMA_SYNC_FORDEV);

	/* Setup and post the mcxnex "SET_PORT" command */
	bzero(&cmd, sizeof (cmd));
	cmd.cp_inparm = mbox_info.mbi_in->mb_mapaddr;
	cmd.cp_outparm = 0;
	cmd.cp_inmod = port->phys_port_num;
	cmd.cp_opcode = EN_SET_VLAN_FLTR;
	cmd.cp_opmod = 0;
	cmd.cp_flags = DDI_SLEEP;
	ret = mcxnex_cmd_post(mcxnex_state, &cmd);
	/* Free the mailbox */
	mcxnex_mbox_free(mcxnex_state, &mbox_info);
	if (ret != MCXNEX_CMD_SUCCESS) {
		cmn_err(CE_WARN,
		    "!mcxe_set_vlan_filter: EN_SET_VLAN_FLTR cmd post "
		    "failed(status=%d).", ret);
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

/*
 * mcxe_hw_ucast_setup()
 *	set up port MAC table.
 */
static int
mcxe_hw_ucast_setup(struct mcxe_port *port)
{
	int ret;

	ret = mcxe_set_port_cmd_post(port->mcxnex_state,
	    port->phys_port_num,
	    MCXE_SET_PORT_MAC_TABLE,
	    MCXE_MAC_TABLE_SIZE, 3,
	    port->ucast_table,
	    DDI_NOSLEEP);
	if (ret != DDI_SUCCESS) {
		cmn_err(CE_WARN, "!mcxe%d: mcxe_hw_ucast_setup() failed.",
		    port->instance);
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

/*
 * mcxe_hw_ucast_cleanup()
 *	clean up port MAC table.
 */
static int
mcxe_hw_ucast_cleanup(struct mcxe_port *port)
{
	ASSERT(mutex_owned(&port->port_lock));

	bzero(port->ucast_table, sizeof (port->ucast_table));

	return (mcxe_hw_ucast_setup(port));
}

/*
 * mcxe_query_port()
 */
static int
mcxe_query_port(mcxe_port_t *port, struct mcxe_hw_query_port *port_query)
{
	int ret;

	bzero(port_query, sizeof (*port_query));
	ret = mcxnex_cmn_query_cmd_post(port->mcxnex_state, QUERY_PORT, 0,
	    port->phys_port_num, port_query, sizeof (*port_query),
	    DDI_NOSLEEP);
	if (ret != MCXNEX_CMD_SUCCESS) {
		cmn_err(CE_WARN, "!mcxe%d: mcxe_query_port() failed(ret=%d).",
		    port->instance, ret);
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

/*
 * mcxe_get_link_state()
 */
static void
mcxe_get_link_state(mcxe_port_t *port)
{
	int ret;
	struct mcxe_hw_query_port port_query;
	link_state_t link_state;

	mutex_enter(&port->port_lock);
	if (!(port->if_state & MCXE_IF_STARTED)) {
		mutex_exit(&port->port_lock);
		return;
	}

	ret = mcxe_query_port(port, &port_query);
	if (ret != DDI_SUCCESS) {
		mutex_exit(&port->port_lock);
		return;
	}

	port->link_duplex = LINK_DUPLEX_FULL;
	if ((port_query.link_speed & MCXE_HW_SPEED_MASK) == MCXE_HW_1G_SPEED)
		port->link_speed = 1000;
	else
		port->link_speed = 10000;

	if (port_query.link_up & MCXE_HW_LINK_UP_MASK)
		link_state = LINK_STATE_UP;
	else
		link_state = LINK_STATE_DOWN;

	if (port->link_state != link_state) {
		port->link_state = link_state;
		mutex_exit(&port->port_lock);
		mac_link_update(port->mac_hdl, port->link_state);
	} else
		mutex_exit(&port->port_lock);
}

/*
 * mcxe_get_primary_mac()
 */
static int
mcxe_get_primary_mac(mcxe_port_t *port)
{
	int ret;
	struct mcxnex_hw_query_port_s hs_queryport;

	bzero(&hs_queryport, sizeof (hs_queryport));
	ret = mcxnex_cmn_query_cmd_post(port->mcxnex_state, QUERY_PORT, 0,
	    port->phys_port_num, &hs_queryport, sizeof (hs_queryport),
	    DDI_NOSLEEP);
	if (ret != MCXNEX_CMD_SUCCESS) {
		cmn_err(CE_WARN,
		    "mcxe%d: mcxe_get_primary_mac() QUERY_PORT failed(ret=%d).",
		    port->instance, ret);
		return (DDI_FAILURE);
	}

	port->hw_mac = htonll(hs_queryport.def_mac);

	return (DDI_SUCCESS);
}

/*
 * mcxe_set_primary_mac()
 */
static int
mcxe_set_primary_mac(mcxe_port_t *port, uint64_t hw_mac)
{
	uint64_t entry;

	ASSERT(mutex_owned(&port->port_lock));

	entry = htonll(hw_mac) | 1ULL << MCXE_MAC_VALID_SHIFT;
	port->ucast_table[0] = entry;	/* first entry */

	return (mcxe_hw_ucast_setup(port));
}

/*
 * mcxe_set_mcast_filter()
 *	set up multicast filter table.
 */
static int
mcxe_set_mcast_filter(mcxe_port_t *port, const uint8_t *mac, uint64_t clear,
    uint8_t mode)
{
	mcxnex_cmd_post_t cmd;
	int status;
	uint64_t inparm;

	bzero(&cmd, sizeof (cmd));

	if (mac) {
		bcopy(mac, &inparm, ETHERADDRL);
		inparm = ntohll(inparm) >> 16;
	} else
		inparm = 0;

	cmd.cp_inparm = inparm | (clear << 63);
	cmd.cp_outparm = 0;
	cmd.cp_inmod = port->phys_port_num;
	cmd.cp_opcode = EN_SET_MCAST_FLTR;
	cmd.cp_opmod = mode;
	cmd.cp_flags = DDI_SLEEP;
	status = mcxnex_cmd_post(port->mcxnex_state, &cmd);
	if (status != DDI_SUCCESS) {
		cmn_err(CE_WARN, "!mcxe%d: failed to set mcast filter(%x)\n",
		    port->instance, status);
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

/*
 * mcxe_hw_mcast_setup()
 */
static int
mcxe_hw_mcast_setup(mcxe_port_t *port)
{
	int ret, i;
	uint8_t bcast[ETHERADDRL] = { 0xff, 0xff, 0xff,
	    0xff, 0xff, 0xff };

	ASSERT(mutex_owned(&port->port_lock));

	ret = mcxe_set_mcast_filter(port, NULL, 0, EN_MCAST_DISABLE);
	if (ret != DDI_SUCCESS)
		return (DDI_FAILURE);

	ret = mcxe_set_mcast_filter(port, bcast, 1, EN_MCAST_CONFIG);
	if (ret != DDI_SUCCESS)
		return (DDI_FAILURE);

	for (i = 0; i < port->mcast_count; i++) {
		ret = mcxe_set_mcast_filter(port,
		    port->mcast_table[i].ether_addr_octet, 0, EN_MCAST_CONFIG);
		if (ret != DDI_SUCCESS)
			return (DDI_FAILURE);
	}

	ret = mcxe_set_mcast_filter(port, NULL, 0, EN_MCAST_ENABLE);
	if (ret != DDI_SUCCESS)
		return (DDI_FAILURE);

	return (DDI_SUCCESS);
}

/*
 * mcxe_mcast_add()
 *	add specified multicast address
 */
static int
mcxe_mcast_add(mcxe_port_t *port, const uint8_t *mcast_addr)
{
	int ret;

	if ((mcast_addr[0] & 01) == 0) {
		return (EINVAL);
	}

	if (port->mcast_count >= MCXE_MAX_MCAST_NUM) {
		return (ENOENT);
	}

	bcopy(mcast_addr, &port->mcast_table[port->mcast_count],
	    ETHERADDRL);
	port->mcast_count++;

	ret = mcxe_hw_mcast_setup(port);
	if (ret != DDI_SUCCESS)
		return (ret);

	return (0);
}

/*
 * mcxe_mcast_del()
 *	delete specified multicast address
 */
static int
mcxe_mcast_del(mcxe_port_t *port, const uint8_t *mcast_addr)
{
	int ret, i;

	/* find the mcast entry */
	for (i = 0; i < port->mcast_count; i++) {
		if (bcmp(mcast_addr, &port->mcast_table[i],
		    ETHERADDRL) == 0) {
			for (i++; i < port->mcast_count; i++) {
				port->mcast_table[i - 1] =
				    port->mcast_table[i];
			}
			port->mcast_count--;
			break;
		}
	}

	/* failed to find */
	if (i == port->mcast_count) {
		uint64_t entry = 0;

		bcopy(mcast_addr, ((char *)&entry) + 2, ETHERADDRL);
		entry = htonll(entry) | 1ULL << MCXE_MAC_VALID_SHIFT;
		cmn_err(CE_WARN, "!mcxe%d: failed to find mcast address %lx",
		    port->instance, (unsigned long)entry);
		return (EINVAL);
	}

	ret = mcxe_hw_mcast_setup(port);
	if (ret != DDI_SUCCESS)
		return (EINVAL);

	return (0);
}

/*
 * mcxe_hw_enable_promisc()
 *	enable port promiscuous mode
 */
static int
mcxe_hw_enable_promisc(mcxe_port_t *port)
{
	return (mcxe_set_rqpn_calc(port, 1));
}

/*
 * mcxe_hw_disable_promisc()
 *	disable port promiscuous mode
 */
static int
mcxe_hw_disable_promisc(mcxe_port_t *port)
{
	return (mcxe_set_rqpn_calc(port, 0));
}

/*
 * ====== Link change notification functions ======
 */
static void
mcxe_trigger_link_softintr(mcxe_port_t *port)
{
	mutex_enter(&port->link_softintr_lock);
	if (port->link_softintr_flag == 0) {
		port->link_softintr_flag = 1;
		ddi_trigger_softintr(port->link_softintr_id);
	}
	mutex_exit(&port->link_softintr_lock);
}

static uint_t
mcxe_link_softintr_handler(caddr_t arg)
{
	mcxe_port_t *port = (void *) arg;

	mutex_enter(&port->link_softintr_lock);

	if (port->link_softintr_flag == 0) {
		mutex_exit(&port->link_softintr_lock);
		return (DDI_INTR_UNCLAIMED);
	}
	port->link_softintr_flag = 0;
	mutex_exit(&port->link_softintr_lock);

	mcxe_get_link_state(port);

	return (DDI_INTR_CLAIMED);
}

static void
mcxe_async_handler(mcxnex_state_t *mcxnex_state,
    ibt_async_code_t type, ibc_async_event_t *event)
{
	mcxe_port_t *port;

	switch (type) {
	case IBT_EVENT_PORT_UP:
	case IBT_ERROR_PORT_DOWN:
		port = mcxe_state_to_port(mcxnex_state, event->ev_port);
		if (port != NULL)
			mcxe_trigger_link_softintr(port);
		break;
	default:
		cmn_err(CE_WARN,
		    "!mcxe_async_handler: unknown async event 0x%x", type);
		break;
	}
}

/*
 * mcxe_port_timer - timer for link status detection and Tx reschedule
 */
static void
mcxe_port_timer(void *arg)
{
	mcxe_port_t *port = arg;
	struct mcxe_tx_ring *tx_ring = &port->tx_rings[0];

	/* link status detection */
	mcxe_get_link_state(port);

	/* Tx reschedule */
	if ((port->if_state & MCXE_IF_STARTED) &&
	    tx_ring->tx_resched_needed &&
	    (tx_ring->tx_free > tx_ring->tx_buffers_low)) {
		tx_ring->tx_resched_needed = 0;
		mac_tx_update(port->mac_hdl);
		tx_ring->tx_resched++;
	}
}


/*
 * ====== GLD entry points support functions ======
 */
static int
mcxe_init_port(mcxe_port_t *port)
{
	int ret;
	mcxe_hw_set_port_gen_t port_gen;
	int port_num = port->phys_port_num;

	bzero(&port_gen, sizeof (port_gen));

	port_gen.flags = SET_PORT_GEN_ALL_VALID;
	port_gen.mtu = port->max_frame_size;
	port_gen.pptx = 1;
	port_gen.pfctx = 0;
	port_gen.pprx = 1;
	port_gen.pfcrx = 0;

	ret = mcxe_set_port_cmd_post(port->mcxnex_state, port_num,
	    MCXE_SET_PORT_GENERAL, sizeof (port_gen), 2,
	    &port_gen, DDI_SLEEP);
	if (ret != DDI_SUCCESS)
		return (DDI_FAILURE);

	ret = mcxe_set_rqpn_calc(port, 0);
	if (ret != DDI_SUCCESS)
		return (DDI_FAILURE);

	ret = mcxe_hw_mcast_setup(port);
	if (ret != DDI_SUCCESS)
		return (DDI_FAILURE);

	ret = mcxe_set_vlan_filter(port);
	if (ret != DDI_SUCCESS)
		return (DDI_FAILURE);

	ret = mcxnex_init_port_cmd_post(port->mcxnex_state,
	    port_num, DDI_SLEEP);
	if (ret != MCXNEX_CMD_SUCCESS) {
		cmn_err(CE_WARN,
		    "!mcxe%d: INIT_PORT (port %02d) command failed: %08x",
		    port->instance, port_num, ret);
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

/*
 * mcxe_free_txb_table()
 *	free tx buffer table
 */
static void
mcxe_free_txb_table(struct mcxe_tx_ring *tx_ring)
{
	mcxe_port_t *port;
	struct mcxe_txbuf *txb;
	int n, w;

	if (tx_ring->txbuf == NULL)
		return;

	mutex_enter(&tx_ring->tc_lock);
	port = tx_ring->port;
	for (n = 0; n < tx_ring->tx_buffers; n++) {
		txb = &tx_ring->txbuf[n];
		if (txb->num_sge) {
			for (w = 0; w < txb->num_dma_seg; w++) {
				(void) ddi_dma_unbind_handle(
				    txb->dma_handle_table[w]);
			}
			txb->num_dma_seg = 0;
			txb->num_sge = 0;
			txb->copy_len = 0;
			if (txb->mp) {
				freemsg(txb->mp);
				txb->mp = NULL;
			}
		}

		if (txb->dma_handle_table) {
			for (w = 0; w < port->max_tx_frags; w++) {
				if (txb->dma_handle_table[w]) {
					ddi_dma_free_handle(
					    &txb->dma_handle_table[w]);
					txb->dma_handle_table[w] = NULL;
				}
			}
			kmem_free(txb->dma_handle_table,
			    sizeof (ddi_dma_handle_t) *
			    port->max_tx_frags);
			txb->dma_handle_table = NULL;
		}

		if (txb->dma_frags) {
			kmem_free(txb->dma_frags,
			    sizeof (ibt_wr_ds_t) *
			    port->max_tx_frags);
			txb->dma_frags = NULL;
		}

		mcxe_free_dma_buffer(&txb->dma_buf);
	}

	kmem_free(tx_ring->txbuf, tx_ring->tx_buffers *
	    sizeof (struct mcxe_txbuf));
	tx_ring->txbuf = NULL;
	tx_ring->tx_buffers = 0;

	mutex_exit(&tx_ring->tc_lock);
}

/*
 * mcxe_alloc_txb_table()
 *	allocate tx buffer table
 */
static int
mcxe_alloc_txb_table(struct mcxe_tx_ring *tx_ring)
{
	mcxe_port_t *port = tx_ring->port;
	struct mcxe_txbuf *txb;
	int n, w, ret;

	tx_ring->tx_buffers = port->tx_ring_size;
	tx_ring->txbuf = kmem_zalloc(tx_ring->tx_buffers *
	    sizeof (struct mcxe_txbuf), KM_NOSLEEP);
	if (tx_ring->txbuf == NULL)
		return (DDI_FAILURE);

	for (n = 0; n < tx_ring->tx_buffers; n++) {
		txb = &tx_ring->txbuf[n];

		ret = mcxe_alloc_dma_buffer(port, &txb->dma_buf,
		    port->tx_buff_size);
		if (ret != DDI_SUCCESS)
			goto alloc_txb_fail;

		txb->dma_frags = kmem_zalloc(sizeof (ibt_wr_ds_t) *
		    port->max_tx_frags, KM_NOSLEEP);
		if (txb->dma_frags == NULL)
			goto alloc_txb_fail;

		txb->dma_handle_table = kmem_zalloc(
		    sizeof (ddi_dma_handle_t) *
		    port->max_tx_frags, KM_NOSLEEP);
		if (txb->dma_frags == NULL)
			goto alloc_txb_fail;

		for (w = 0; w < port->max_tx_frags; w++) {
			ret = ddi_dma_alloc_handle(port->devinfo,
			    &mcxe_dma_tx_attr,
			    DDI_DMA_DONTWAIT, NULL,
			    &txb->dma_handle_table[w]);
			if (ret != DDI_SUCCESS)
				goto alloc_txb_fail;
		}
	}
	tx_ring->tx_buffers_low = tx_ring->tx_buffers / 8;

	return (DDI_SUCCESS);

alloc_txb_fail:
	cmn_err(CE_WARN, "!mcxe%d: mcxe_alloc_txb_table() failed.\n",
	    port->instance);
	(void) mcxe_free_txb_table(tx_ring);

	return (DDI_FAILURE);
}

/*
 * mcxe_free_rxb_table()
 *	free rx buffer table
 */
static void
mcxe_free_rxb_table(struct mcxe_rx_ring *rx_ring)
{
	mcxe_port_t *port;
	struct mcxe_rxbuf *rxb;
	dma_buffer_t *dma_buf;
	int i;

	if (rx_ring->rxbuf == NULL)
		return;

	port = rx_ring->port;
	rw_enter(&rx_ring->rc_rwlock, RW_WRITER);
	for (i = 0; i < port->rx_ring_size; i++) {
		rxb = &rx_ring->rxbuf[i];
		if (rxb->ref_cnt) {	/* the rxb is in-use */
			if (mcxe_verbose) {
				cmn_err(CE_NOTE, "!mcxe%d: rxb %d is in use.\n",
				    port->instance, i);
			}
			atomic_inc_32(&port->rxb_pending);
			atomic_inc_32(&rx_ring->rxb_pending);
			rxb->flag |= MCXE_RXB_STOPPED;
			continue;
		}
		if (rxb->mp != NULL)
			freemsg(rxb->mp);
		dma_buf = &rxb->dma_buf;
		mcxe_free_dma_buffer(dma_buf);
	}
	rw_exit(&rx_ring->rc_rwlock);

	for (i = 0; rx_ring->rxb_pending && i < 10; i++) {
		/* wait for rxb to be freed ... */
		drv_usecwait(100);
	}

	rw_enter(&rx_ring->rc_rwlock, RW_WRITER);
	if (!rx_ring->rxb_pending && rx_ring->rxbuf) {
		kmem_free(rx_ring->rxbuf, port->rx_ring_size *
		    sizeof (struct mcxe_rxbuf));
		rx_ring->rxbuf = NULL;
	}
	rw_exit(&rx_ring->rc_rwlock);
}

/*
 * mcxe_alloc_rxb_table()
 *	alloc rx buffer table
 */
static int
mcxe_alloc_rxb_table(struct mcxe_rx_ring *rx_ring)
{
	mcxe_port_t *port = rx_ring->port;
	struct mcxe_rxbuf *rxb;
	dma_buffer_t *dma_buf;
	int i, ret;

	rw_enter(&rx_ring->rc_rwlock, RW_WRITER);
	if (rx_ring->rxbuf == NULL) {
		rx_ring->rxbuf = kmem_zalloc(port->rx_ring_size *
		    sizeof (struct mcxe_rxbuf), KM_NOSLEEP);
		if (rx_ring->rxbuf == NULL) {
			cmn_err(CE_WARN,
			    "!mcxe_alloc_rxb_table(): rxbuf alloc failed.");
			rw_exit(&rx_ring->rc_rwlock);
			return (DDI_FAILURE);
		}
	}

	for (i = 0; i < port->rx_ring_size; i++) {
		rxb = &rx_ring->rxbuf[i];
		if (rxb->ref_cnt) {	/* reuse the rxb */
			rxb->flag |= MCXE_RXB_REUSED;
			continue;
		}
		rxb->rx_ring = rx_ring;
		dma_buf = &rxb->dma_buf;
		ret = mcxe_alloc_dma_buffer(port, dma_buf,
		    port->rx_buff_size);
		if (ret != DDI_SUCCESS)
			goto alloc_rxb_fail;

		dma_buf->size -= MCXE_IPHDR_ALIGN_ROOM;
		dma_buf->address += MCXE_IPHDR_ALIGN_ROOM;
		dma_buf->dma_address += MCXE_IPHDR_ALIGN_ROOM;

		rxb->rxb_free_rtn.free_arg = (caddr_t)rxb;
		rxb->rxb_free_rtn.free_func = mcxe_rxb_free_cb;
		rxb->mp = desballoc((unsigned char *)dma_buf->address,
		    dma_buf->size, 0, &rxb->rxb_free_rtn);
	}
	rw_exit(&rx_ring->rc_rwlock);
	return (DDI_SUCCESS);

alloc_rxb_fail:
	rw_exit(&rx_ring->rc_rwlock);
	cmn_err(CE_WARN, "!mcxe%d: mcxe_alloc_rxb_table() failed.\n",
	    port->instance);
	mcxe_free_rxb_table(rx_ring);

	return (DDI_FAILURE);

}

/*
 * mcxe_free_tx_rsrc()
 *	free tx required resources including CQs, QPs and tx buffer table.
 */
static int
mcxe_free_tx_rsrc(struct mcxe_tx_ring *tx_ring)
{
	mcxe_port_t *port;
	mcxnex_qphdl_t qp_handle;
	ibc_qpn_hdl_t qpn_handle;
	mcxnex_cqhdl_t cq_handle;
	int ret;

	if (tx_ring == NULL)
		return (DDI_SUCCESS);
	port = tx_ring->port;

	/* free tx buffer table */
	mcxe_free_txb_table(tx_ring);

	/* free tx QP */
	qp_handle = tx_ring->tx_qp;
	if (qp_handle) {
		ret = mcxnex_qp_free(port->mcxnex_state, &qp_handle,
		    IBC_FREE_QP_AND_QPN, &qpn_handle,
		    DDI_NOSLEEP);
		if (ret != DDI_SUCCESS) {
			cmn_err(CE_WARN, "!mcxe%d: tx_qp free failed(ret=%d).",
			    port->instance, ret);
			goto free_tx_fail;
		}
		tx_ring->tx_qp = 0;
	}

	/* free tx CQ */
	cq_handle = tx_ring->tx_cq;
	if (cq_handle) {
		ret = mcxnex_cq_free(port->mcxnex_state, &cq_handle,
		    DDI_NOSLEEP);
		if (ret != DDI_SUCCESS) {
			cmn_err(CE_WARN, "!mcxe%d: tx_cq free failed(ret=%d).",
			    port->instance, ret);
			goto free_tx_fail;
		}
		tx_ring->tx_cq = 0;
	}

	return (DDI_SUCCESS);

free_tx_fail:
	cmn_err(CE_WARN, "!mcxe%d: mcxe_free_tx_rsrc() failed.\n",
	    port->instance);
	return (DDI_FAILURE);
}

/*
 * mcxe_alloc_tx_rsrc()
 *	allocate tx required resources including CQs, QPs and tx buffer table.
 */
static int
mcxe_alloc_tx_rsrc(struct mcxe_tx_ring *tx_ring)
{
	mcxe_port_t *port = tx_ring->port;
	ibt_cq_attr_t cq_attr;
	uint_t actual_size;
	int ret;

	/* allocate tx buffer table */
	ret = mcxe_alloc_txb_table(tx_ring);
	if (ret != DDI_SUCCESS)
		goto alloc_tx_fail;

	/* allocate tx CQ */
	cq_attr.cq_flags = 0;
	cq_attr.cq_sched = 0;
	cq_attr.cq_size = port->tx_ring_size;
	tx_ring->tx_cq = 0;
	ret = mcxnex_cq_alloc(port->mcxnex_state, NULL, &cq_attr,
	    &actual_size, &tx_ring->tx_cq, DDI_NOSLEEP);
	if (ret != DDI_SUCCESS) {
		cmn_err(CE_WARN,
		    "!mcxe_alloc_tx_rsrc(): alloc tx cq failed(ret=%d).", ret);
		goto alloc_tx_fail;
	}
	(void) mcxnex_cq_modify(port->mcxnex_state, tx_ring->tx_cq,
	    8, 0, 0, DDI_NOSLEEP);
	mcxnex_priv_cq_set_handler(port->mcxnex_state, tx_ring->tx_cq,
	    mcxe_tx_intr_handler, tx_ring);

	/* allocate tx QP */
	tx_ring->tx_qp = 0;
	ret = mcxe_alloc_tx_qp(tx_ring);
	if (ret != DDI_SUCCESS)
		goto alloc_tx_fail;

	return (DDI_SUCCESS);

alloc_tx_fail:
	cmn_err(CE_WARN, "!mcxe%d: mcxe_alloc_tx_rsrc() failed.\n",
	    port->instance);
	(void) mcxe_free_tx_rsrc(tx_ring);

	return (DDI_FAILURE);
}

/*
 * mcxe_free_rx_rsrc()
 *	free rx required resources including CQs, QPs and rx buffer table.
 */
static int
mcxe_free_rx_rsrc(struct mcxe_rx_ring *rx_ring)
{
	mcxe_port_t *port;
	mcxnex_qphdl_t qp_handle;
	ibc_qpn_hdl_t qpn_handle;
	mcxnex_cqhdl_t cq_handle;
	int ret;

	if (rx_ring == NULL)
		return (DDI_SUCCESS);

	if (rx_ring->rxbuf)
		mcxe_free_rxb_table(rx_ring);

	port = rx_ring->port;
	qp_handle = rx_ring->rx_qp;
	if (qp_handle) {
		ret = mcxnex_qp_free(port->mcxnex_state, &qp_handle,
		    IBC_FREE_QP_AND_QPN, &qpn_handle,
		    DDI_NOSLEEP);
		if (ret != DDI_SUCCESS) {
			cmn_err(CE_WARN, "!mcxe%d: free rx_qp failed(ret=%d).",
			    port->instance, ret);
			goto free_rx_fail;
		}
		rx_ring->rx_qp = 0;
	}

	cq_handle = rx_ring->rx_cq;
	if (cq_handle) {
		ret = mcxnex_cq_free(port->mcxnex_state, &cq_handle,
		    DDI_NOSLEEP);
		if (ret != DDI_SUCCESS) {
			cmn_err(CE_WARN, "!mcxe%d: free rx_cq failed(ret=%d).",
			    port->instance, ret);
			goto free_rx_fail;
		}
		rx_ring->rx_cq = 0;
	}

	return (DDI_SUCCESS);

free_rx_fail:
	cmn_err(CE_WARN, "!mcxe%d: mcxe_free_rx_rsrc() failed.\n",
	    port->instance);

	return (DDI_FAILURE);
}

/*
 * mcxe_alloc_rx_rsrc()
 *	allocate rx required resources including CQs, QPs and rx buffer table.
 */
static int
mcxe_alloc_rx_rsrc(struct mcxe_rx_ring *rx_ring)
{
	mcxe_port_t *port = rx_ring->port;
	ibt_cq_attr_t cq_attr;
	uint_t actual_size;
	int ret;

	/* allocate rx buffer table */
	ret = mcxe_alloc_rxb_table(rx_ring);
	if (ret != DDI_SUCCESS)
		goto alloc_rx_fail;

	/* allocate rx CQ */
	cq_attr.cq_flags = 0;
	cq_attr.cq_sched = 0;
	cq_attr.cq_size = port->rx_ring_size;
	ret = mcxnex_cq_alloc(port->mcxnex_state, NULL, &cq_attr,
	    &actual_size, &rx_ring->rx_cq, DDI_NOSLEEP);
	if (ret != DDI_SUCCESS) {
		cmn_err(CE_WARN,
		    "!mcxe_alloc_rx_rsrc(): alloc rx cq failed(ret=%d).", ret);
		goto alloc_rx_fail;
	}
	mcxnex_priv_cq_set_handler(port->mcxnex_state, rx_ring->rx_cq,
	    mcxe_rx_intr_handler, rx_ring);

	/* allocate rx QP */
	ret = mcxe_alloc_rx_qp(rx_ring);
	if (ret != DDI_SUCCESS)
		goto alloc_rx_fail;

	return (DDI_SUCCESS);

alloc_rx_fail:
	cmn_err(CE_WARN, "!mcxe%d: mcxe_alloc_rx_rsrc() failed.\n",
	    port->instance);
	(void) mcxe_free_rx_rsrc(rx_ring);
	return (DDI_FAILURE);
}

/*
 * mcxe_free_rsrc()
 *	free port rx/tx resources.
 */
static void
mcxe_free_rsrc(mcxe_port_t *port)
{
	int i;

	for (i = 0; i < port->num_rx_rings; i++)
		(void) mcxe_free_rx_rsrc(&port->rx_rings[i]);

	for (i = 0; i < port->num_tx_rings; i++)
		(void) mcxe_free_tx_rsrc(&port->tx_rings[i]);

	port->progress &= ~MCXE_PRG_ALLOC_RSRC;
}

/*
 * mcxe_alloc_rsrc()
 *	allocate port rx/tx resources.
 */
static int
mcxe_alloc_rsrc(mcxe_port_t *port)
{
	struct mcxe_tx_ring *tx_ring;
	struct mcxe_rx_ring *rx_ring;
	int i, ret;

	if (port->progress & MCXE_PRG_ALLOC_RSRC)
		return (DDI_SUCCESS);

	/* allocate tx resource */
	for (i = 0; i < port->num_tx_rings; i++) {
		tx_ring = &port->tx_rings[i];
		ret = mcxe_alloc_tx_rsrc(tx_ring);
		if (ret != DDI_SUCCESS)
			goto alloc_rsrc_fail;
	}

	/* allocate rx resource */
	for (i = 0; i < port->num_rx_rings; i++) {
		rx_ring = &port->rx_rings[i];
		ret = mcxe_alloc_rx_rsrc(rx_ring);
		if (ret != DDI_SUCCESS)
			goto alloc_rsrc_fail;
	}

	port->progress |= MCXE_PRG_ALLOC_RSRC;

	return (DDI_SUCCESS);

alloc_rsrc_fail:
	cmn_err(CE_WARN, "!mcxe%d: mcxe_alloc_rsrc() failed.\n",
	    port->instance);
	mcxe_free_rsrc(port);

	return (DDI_FAILURE);
}

/*
 * mcxe_init_rx_ring()
 *	initialize the rx ring structure.
 */
static void
mcxe_init_rx_ring(struct mcxe_rx_ring *rx_ring)
{
	int i;
	struct mcxe_rxbuf *rxb;

	mutex_enter(&rx_ring->rx_lock);
	rw_enter(&rx_ring->rc_rwlock, RW_WRITER);

	rx_ring->rx_free = 0;
	rx_ring->rx_bind = 0;
	rx_ring->rx_bcopy = 0;
	rx_ring->rx_postfail = 0;
	rx_ring->rx_allocfail = 0;

	/* fill in Rx QP */
	for (i = 0; i < rx_ring->port->rx_ring_size; i++) {
		rxb = &rx_ring->rxbuf[i];
		rxb->index = i;
		if (rxb->ref_cnt) {
			if (rxb->flag & MCXE_RXB_REUSED)
				rxb->flag |= MCXE_RXB_STARTED;
		} else {
			rxb->flag = 0;
			(void) mcxe_post_recv(rxb);
		}
	}

	rw_exit(&rx_ring->rc_rwlock);
	mutex_exit(&rx_ring->rx_lock);
}

/*
 * mcxe_init_tx_ring()
 *	initialize the tx ring structure.
 */
static void
mcxe_init_tx_ring(struct mcxe_tx_ring *tx_ring)
{
	mcxe_queue_t *txbuf_queue;
	mcxe_queue_item_t *txbuf_head;
	mcxe_txbuf_t *txbuf;
	uint32_t slot;

	mutex_enter(&tx_ring->tc_lock);

	/*
	 * Reinitialise control variables ...
	 */
	tx_ring->tx_nobd = 0;
	tx_ring->tx_nobuf = 0;
	tx_ring->tx_bindfail = 0;
	tx_ring->tx_bindexceed = 0;
	tx_ring->tx_alloc_fail = 0;
	tx_ring->tx_pullup = 0;
	tx_ring->tx_drop = 0;
	tx_ring->tx_bcopy = 0;
	tx_ring->tx_free = tx_ring->tx_buffers;

	/*
	 * Initialize the tx buffer push queue
	 */
	mutex_enter(&tx_ring->freetxbuf_lock);
	mutex_enter(&tx_ring->txbuf_lock);
	txbuf_queue = &tx_ring->freetxbuf_queue;
	txbuf_queue->head = NULL;
	txbuf_queue->count = 0;
	txbuf_queue->lock = &tx_ring->freetxbuf_lock;
	tx_ring->txbuf_push_queue = txbuf_queue;

	/*
	 * Initialize the tx buffer pop queue
	 */
	txbuf_queue = &tx_ring->txbuf_queue;
	txbuf_queue->head = NULL;
	txbuf_queue->count = 0;
	txbuf_queue->lock = &tx_ring->txbuf_lock;
	tx_ring->txbuf_pop_queue = txbuf_queue;
	txbuf_head = tx_ring->txbuf_head;
	txbuf = tx_ring->txbuf;
	for (slot = 0; slot < tx_ring->tx_buffers; ++slot) {
		txbuf_head->item = txbuf;
		txbuf_head->next = txbuf_queue->head;
		txbuf_queue->head = txbuf_head;
		txbuf_queue->count++;
		txbuf++;
		txbuf_head++;
	}
	mutex_exit(&tx_ring->txbuf_lock);
	mutex_exit(&tx_ring->freetxbuf_lock);

	mutex_exit(&tx_ring->tc_lock);
}

/*
 * mcxe_start_rings()
 *	enable QPs for tx/rx rings.
 */
static int
mcxe_start_rings(struct mcxe_port *port)
{
	int ret, i;
	struct mcxe_rx_ring *rx_ring;
	struct mcxe_tx_ring *tx_ring;

	for (i = 0; i < port->num_tx_rings; i++) {
		tx_ring = &port->tx_rings[i];
		ret = mcxe_enable_qp(port, tx_ring->tx_qp);
		if (ret != DDI_SUCCESS) {
			cmn_err(CE_WARN,
			    "!mcxe_start_rings: enable tx ring %d failed.", i);
			return (DDI_FAILURE);
		}
		(void) mcxnex_cq_notify(port->mcxnex_state, tx_ring->tx_cq,
		    IBT_NEXT_COMPLETION);

		mcxe_init_tx_ring(tx_ring);
	}

	for (i = 0; i < port->num_rx_rings; i++) {
		rx_ring = &port->rx_rings[i];
		ret = mcxe_enable_qp(port, rx_ring->rx_qp);
		if (ret != DDI_SUCCESS) {
			cmn_err(CE_WARN,
			    "!mcxe_start_rings: enable rx ring %d failed.", i);
			return (DDI_FAILURE);
		}

		(void) mcxnex_cq_notify(port->mcxnex_state, rx_ring->rx_cq,
		    IBT_NEXT_COMPLETION);

		mcxe_init_rx_ring(rx_ring);
	}

	return (DDI_SUCCESS);
}

/*
 * mcxe_stop_rings()
 */
static int
mcxe_stop_rings(struct mcxe_port *port)
{
	struct mcxe_rx_ring *rx_ring;
	int ret, i;

	for (i = 0; i < port->num_rx_rings; i++) {
		rx_ring = &port->rx_rings[i];
		mutex_enter(&rx_ring->rx_lock);
		rw_enter(&rx_ring->rc_rwlock, RW_WRITER);
		ret = mcxe_disable_qp(port, rx_ring->rx_qp);
		rw_exit(&rx_ring->rc_rwlock);
		mutex_exit(&rx_ring->rx_lock);

		if (ret != DDI_SUCCESS)
			return (DDI_FAILURE);

	}

	return (DDI_SUCCESS);
}


/*
 * ====== GLD-required entry points ======
 */

/*
 * mcxe_m_tx() - send a chain of packets
 */
mblk_t *
mcxe_m_tx(void *arg, mblk_t *mp)
{
	mblk_t *next;
	mcxe_port_t *port = arg;

	ASSERT(mp != NULL);

	rw_enter(&port->port_rwlock, RW_READER);

	while (mp != NULL) {
		next = mp->b_next;
		mp->b_next = NULL;

		if ((mp = mcxe_tx(arg, mp)) != NULL) {
			mp->b_next = next;
			break;
		}
		mp = next;
	}

	rw_exit(&port->port_rwlock);

	return (mp);
}

static int
mcxe_m_stat(void *arg, uint_t stat, uint64_t *val)
{
	mcxe_port_t *port = arg;
	struct mcxe_hw_stats *hw_statsp;

	mutex_enter(&port->port_lock);

	if (!(port->if_state & MCXE_IF_STARTED)) {
		mutex_exit(&port->port_lock);
		return (ECANCELED);
	}

	(void) mcxe_hw_get_stats(port, 0);

	hw_statsp = &port->hw_stats;
	switch (stat) {
	case MAC_STAT_IFSPEED:
		*val = port->link_speed * 1000000ull;
		break;

	case MAC_STAT_MULTIRCV:
		*val = hw_statsp->MCAST_prio_0 + hw_statsp->MCAST_prio_1 +
		    hw_statsp->MCAST_prio_2 + hw_statsp->MCAST_prio_3 +
		    hw_statsp->MCAST_prio_4 + hw_statsp->MCAST_prio_5 +
		    hw_statsp->MCAST_prio_6 + hw_statsp->MCAST_prio_7 +
		    hw_statsp->MCAST_novlan;
		break;

	case MAC_STAT_BRDCSTRCV:
		*val = hw_statsp->RBCAST_prio_0 + hw_statsp->RBCAST_prio_1 +
		    hw_statsp->RBCAST_prio_2 + hw_statsp->RBCAST_prio_3 +
		    hw_statsp->RBCAST_prio_4 + hw_statsp->RBCAST_prio_5 +
		    hw_statsp->RBCAST_prio_6 + hw_statsp->RBCAST_prio_7 +
		    hw_statsp->RBCAST_novlan;
		break;

	case MAC_STAT_MULTIXMT:
		*val = hw_statsp->TMCAST_prio_0 + hw_statsp->TMCAST_prio_1 +
		    hw_statsp->TMCAST_prio_2 + hw_statsp->TMCAST_prio_3 +
		    hw_statsp->TMCAST_prio_4 + hw_statsp->TMCAST_prio_5 +
		    hw_statsp->TMCAST_prio_6 + hw_statsp->TMCAST_prio_7 +
		    hw_statsp->TMCAST_novlan;
		break;

	case MAC_STAT_BRDCSTXMT:
		*val = hw_statsp->TBCAST_prio_0 + hw_statsp->TBCAST_prio_1 +
		    hw_statsp->TBCAST_prio_2 + hw_statsp->TBCAST_prio_3 +
		    hw_statsp->TBCAST_prio_4 + hw_statsp->TBCAST_prio_5 +
		    hw_statsp->TBCAST_prio_6 + hw_statsp->TBCAST_prio_7 +
		    hw_statsp->TBCAST_novlan;
		break;

	case MAC_STAT_NORCVBUF:
		*val = hw_statsp->RdropOvflw;
		break;

	case MAC_STAT_IERRORS:
		*val = hw_statsp->PCS + hw_statsp->RJBBR +
		    hw_statsp->RCRC + hw_statsp->RRUNT;
		break;

	case MAC_STAT_RBYTES:
		*val = hw_statsp->ROCT_prio_0 + hw_statsp->ROCT_prio_1 +
		    hw_statsp->ROCT_prio_2 + hw_statsp->ROCT_prio_3 +
		    hw_statsp->ROCT_prio_4 + hw_statsp->ROCT_prio_5 +
		    hw_statsp->ROCT_prio_6 + hw_statsp->ROCT_prio_7 +
		    hw_statsp->ROCT_novlan;
		break;

	case MAC_STAT_OBYTES:
		*val = hw_statsp->TOCT_prio_0 + hw_statsp->TOCT_prio_1 +
		    hw_statsp->TOCT_prio_2 + hw_statsp->TOCT_prio_3 +
		    hw_statsp->TOCT_prio_4 + hw_statsp->TOCT_prio_5 +
		    hw_statsp->TOCT_prio_6 + hw_statsp->TOCT_prio_7 +
		    hw_statsp->TOCT_novlan;
		break;

	case MAC_STAT_IPACKETS:
		*val = hw_statsp->RTOT_prio_0 + hw_statsp->RTOT_prio_1 +
		    hw_statsp->RTOT_prio_2 + hw_statsp->RTOT_prio_3 +
		    hw_statsp->RTOT_prio_4 + hw_statsp->RTOT_prio_5 +
		    hw_statsp->RTOT_prio_6 + hw_statsp->RTOT_prio_7 +
		    hw_statsp->RTOT_novlan;
		break;

	case MAC_STAT_OPACKETS:
		*val = hw_statsp->TTOT_prio_0 + hw_statsp->TTOT_prio_1 +
		    hw_statsp->TTOT_prio_2 + hw_statsp->TTOT_prio_3 +
		    hw_statsp->TTOT_prio_4 + hw_statsp->TTOT_prio_5 +
		    hw_statsp->TTOT_prio_6 + hw_statsp->TTOT_prio_7 +
		    hw_statsp->TTOT_novlan;
		break;

	case ETHER_STAT_FCS_ERRORS:
		*val = hw_statsp->RCRC;
		break;

	case ETHER_STAT_TOOLONG_ERRORS:
		*val = hw_statsp->RGIANT_prio_0 + hw_statsp->RGIANT_prio_1 +
		    hw_statsp->RGIANT_prio_2 + hw_statsp->RGIANT_prio_3 +
		    hw_statsp->RGIANT_prio_4 + hw_statsp->RGIANT_prio_5 +
		    hw_statsp->RGIANT_prio_6 + hw_statsp->RGIANT_prio_7 +
		    hw_statsp->RGIANT_novlan;
		break;

	case ETHER_STAT_MACRCV_ERRORS:
		*val = hw_statsp->RDROP;
		break;

	/* MII/GMII stats */
	case ETHER_STAT_XCVR_ADDR:
		*val = 1;
		break;

	case ETHER_STAT_XCVR_ID:
		*val = 1;
		break;

	case ETHER_STAT_XCVR_INUSE:
		*val = 1;
		break;

	case ETHER_STAT_CAP_10GFDX:
		*val = 1;
		break;

	case ETHER_STAT_CAP_1000FDX:
		*val = 1;
		break;

	case ETHER_STAT_CAP_100FDX:
		*val = 0;
		break;

	case ETHER_STAT_CAP_ASMPAUSE:
		*val = port->param_asym_pause_cap;
		break;

	case ETHER_STAT_CAP_PAUSE:
		*val = port->param_pause_cap;
		break;
	case ETHER_STAT_CAP_AUTONEG:
		*val = 1;
		break;

	case ETHER_STAT_ADV_CAP_10GFDX:
		*val = port->param_adv_10000fdx_cap;
		break;

	case ETHER_STAT_ADV_CAP_1000FDX:
		*val = port->param_adv_1000fdx_cap;
		break;

	case ETHER_STAT_ADV_CAP_100FDX:
		*val = port->param_adv_100fdx_cap;
		break;

	case ETHER_STAT_ADV_CAP_ASMPAUSE:
		*val = port->param_adv_asym_pause_cap;
		break;

	case ETHER_STAT_ADV_CAP_PAUSE:
		*val = port->param_adv_pause_cap;
		break;

	case ETHER_STAT_ADV_CAP_AUTONEG:
		*val = port->param_adv_autoneg_cap;
		break;

	case ETHER_STAT_LP_CAP_10GFDX:
		*val = port->param_lp_10000fdx_cap;
		break;

	case ETHER_STAT_LP_CAP_1000FDX:
		*val = port->param_lp_1000fdx_cap;
		break;

	case ETHER_STAT_LP_CAP_100FDX:
		*val = port->param_lp_100fdx_cap;
		break;

	case ETHER_STAT_LP_CAP_ASMPAUSE:
		*val = port->param_lp_asym_pause_cap;
		break;

	case ETHER_STAT_LP_CAP_PAUSE:
		*val = port->param_lp_pause_cap;
		break;

	case ETHER_STAT_LP_CAP_AUTONEG:
		*val = port->param_lp_autoneg_cap;
		break;

	case ETHER_STAT_LINK_ASMPAUSE:
		*val = port->param_asym_pause_cap;
		break;

	case ETHER_STAT_LINK_PAUSE:
		*val = port->param_pause_cap;
		break;

	case ETHER_STAT_LINK_AUTONEG:
		*val = port->param_adv_autoneg_cap;
		break;

	case ETHER_STAT_LINK_DUPLEX:
		*val = port->link_duplex;
		break;

	case ETHER_STAT_TOOSHORT_ERRORS:
		*val = hw_statsp->RSHORT;
		break;

	case ETHER_STAT_CAP_REMFAULT:
		*val = port->param_rem_fault;
		break;

	case ETHER_STAT_ADV_REMFAULT:
		*val = port->param_adv_rem_fault;
		break;

	case ETHER_STAT_LP_REMFAULT:
		*val = port->param_lp_rem_fault;
		break;

	case ETHER_STAT_JABBER_ERRORS:
		*val = hw_statsp->RJBBR;
		break;

	default:
		mutex_exit(&port->port_lock);
		return (ENOTSUP);
	}

	mutex_exit(&port->port_lock);

	return (0);
}

static int
mcxe_m_start(void *arg)
{
	mcxe_port_t *port = arg;
	int ret;

	mutex_enter(&port->port_lock);
	rw_enter(&port->port_rwlock, RW_WRITER);

	ret = mcxe_alloc_rsrc(port);
	if (ret != DDI_SUCCESS)
		goto alloc_fail;

	ret = mcxe_start_rings(port);
	if (ret != DDI_SUCCESS)
		goto start_fail;

	/* add the primary MAC address to MAC address table */
	ret = mcxe_set_primary_mac(port, port->hw_mac);
	if (ret != DDI_SUCCESS)
		goto start_fail;

	/* init and start the port */
	ret = mcxe_init_port(port);
	if (ret != DDI_SUCCESS)
		goto start_fail;
	atomic_or_32(&port->if_state, MCXE_IF_STARTED);

	/* clear the statistics */
	(void) mcxe_hw_get_stats(port, 1);
	bzero(&port->hw_stats, sizeof (struct mcxe_hw_stats));

	rw_exit(&port->port_rwlock);
	mutex_exit(&port->port_lock);

	if (mcxe_verbose) {
		cmn_err(CE_NOTE, "!mcxe%d: mcxe_m_start() OK.\n",
		    port->instance);
	}

	return (DDI_SUCCESS);

start_fail:
	mcxe_free_rsrc(port);

alloc_fail:
	rw_exit(&port->port_rwlock);
	mutex_exit(&port->port_lock);

	cmn_err(CE_WARN, "!mcxe%d: mcxe_m_start() failed.\n",
	    port->instance);

	return (ret);
}

static void
mcxe_m_stop(void *arg)
{
	int ret;
	mcxe_port_t *port = arg;

	mutex_enter(&port->port_lock);
	atomic_and_32(&port->if_state, ~MCXE_IF_STARTED);
	port->link_state = LINK_STATE_UNKNOWN;
	mac_link_update(port->mac_hdl, port->link_state);
	ret = mcxe_hw_ucast_cleanup(port);
	if (ret != DDI_SUCCESS) {
		cmn_err(CE_WARN, "!mcxe_m_stop: ucast cleanup failed");
	}
	(void) mcxe_hw_disable_promisc(port);

	rw_enter(&port->port_rwlock, RW_WRITER);
	drv_usecwait(1500);	/* drain hw rx/tx */
	ret = mcxe_stop_rings(port);
	if (ret != DDI_SUCCESS) {
		cmn_err(CE_WARN, "!mcxe_m_stop: stop rings failed");
	}
	ret = mcxnex_close_port_cmd_post(port->mcxnex_state,
	    port->phys_port_num, DDI_SLEEP);
	if (ret != MCXNEX_CMD_SUCCESS) {
		cmn_err(CE_WARN, "!mcxe%d: failed to close port %x",
		    port->instance, port->phys_port_num);
	}
	rw_exit(&port->port_rwlock);

	mcxe_free_rsrc(port);
	mutex_exit(&port->port_lock);
}

static int
mcxe_m_promisc(void *arg, boolean_t on)
{
	int ret;
	mcxe_port_t *port = arg;

	mutex_enter(&port->port_lock);
	if (!(port->if_state & MCXE_IF_STARTED)) {
		mutex_exit(&port->port_lock);
		return (0);
	}

	if (on)
		ret = mcxe_hw_enable_promisc(port);
	else
		ret = mcxe_hw_disable_promisc(port);

	mutex_exit(&port->port_lock);

	return (ret);
}

static int
mcxe_m_multicst(void *arg, boolean_t add, const uint8_t *mcast_addr)
{
	mcxe_port_t *port = arg;
	int ret;

	mutex_enter(&port->port_lock);
	if (!(port->if_state & MCXE_IF_STARTED)) {
		mutex_exit(&port->port_lock);
		return (0);
	}

	if (add)
		ret = mcxe_mcast_add(port, mcast_addr);
	else
		ret = mcxe_mcast_del(port, mcast_addr);

	mutex_exit(&port->port_lock);

	return (ret);
}

static int
mcxe_m_unicst(void *arg, const uint8_t *mac_addr)
{
	struct mcxe_port *port = arg;
	uint64_t hw_mac = 0;
	int ret;

	mutex_enter(&port->port_lock);
	if (!(port->if_state & MCXE_IF_STARTED)) {
		mutex_exit(&port->port_lock);
		return (0);
	}

	bcopy(mac_addr, ((uint8_t *)&hw_mac) + 2, ETHERADDRL);
	ret = mcxe_set_primary_mac(port, hw_mac);

	mutex_exit(&port->port_lock);

	return (ret);
}

/*ARGSUSED*/
static boolean_t
mcxe_m_getcapab(void *arg, mac_capab_t cap, void *cap_data)
{
	switch (cap) {
	case MAC_CAPAB_HCKSUM: {
		uint32_t *txflags = cap_data;

		*txflags = HCKSUM_INET_FULL_V4 | HCKSUM_IPHDRCKSUM;
		break;
	}
	case MAC_CAPAB_LSO: {
		mac_capab_lso_t *cap_lso = cap_data;

		cap_lso->lso_flags = LSO_TX_BASIC_TCP_IPV4;
		cap_lso->lso_basic_tcp_ipv4.lso_max = MCXE_LSO_MAXLEN;
		break;
	}
	default:
		return (B_FALSE);
	}

	return (B_TRUE);
}

/*ARGSUSED*/
static int
mcxe_m_setprop(void *arg, const char *pr_name, mac_prop_id_t pr_num,
    uint_t pr_valsize, const void *pr_val)
{
	mcxe_port_t *port = arg;
	uint32_t cur_mtu, new_mtu;
	int err;

	mutex_enter(&port->port_lock);

	switch (pr_num) {
	case MAC_PROP_EN_10GFDX_CAP:
	case MAC_PROP_EN_1000FDX_CAP:
	case MAC_PROP_EN_100FDX_CAP:
	case MAC_PROP_AUTONEG:
	case MAC_PROP_FLOWCTRL:
	case MAC_PROP_ADV_10GFDX_CAP:
	case MAC_PROP_ADV_1000FDX_CAP:
	case MAC_PROP_ADV_100FDX_CAP:
	case MAC_PROP_STATUS:
	case MAC_PROP_SPEED:
	case MAC_PROP_DUPLEX:
		err = ENOTSUP; /* read-only prop. Can't set this. */
		break;

	case MAC_PROP_MTU:
		cur_mtu = port->default_mtu;
		bcopy(pr_val, &new_mtu, sizeof (new_mtu));
		if (new_mtu == cur_mtu) {
			err = 0;
			break;
		}

		if (new_mtu < MCXE_DEFAULT_MTU || new_mtu > MCXE_MAX_MTU) {
			err = EINVAL;
			break;
		}

		if (port->if_state & MCXE_IF_STARTED) {
			err = EBUSY;
			break;
		}

		err = mac_maxsdu_update(port->mac_hdl, new_mtu);
		if (err == 0) {
			/*
			 * Set rx/tx buffer size
			 */
			port->default_mtu = new_mtu;
			port->max_frame_size = port->default_mtu +
			    sizeof (struct ether_vlan_header) + ETHERFCSL;
			port->rx_buff_size = port->max_frame_size +
			    MCXE_IPHDR_ALIGN_ROOM;
			port->rx_buff_size = (port->rx_buff_size + 0x3ff) &
			    (~0x3ff); /* 1K alligned */
			port->tx_buff_size = port->rx_buff_size;
		}
		break;

	default:
		err = EINVAL;
		break;
	}

	mutex_exit(&port->port_lock);
	return (err);
}

/*ARGSUSED*/
static int
mcxe_m_getprop(void *arg, const char *pr_name, mac_prop_id_t pr_num,
    uint_t pr_valsize, void *pr_val)
{
	mcxe_port_t *port = arg;
	uint32_t flow_control;
	uint64_t tmp = 0;
	int err = 0;

	switch (pr_num) {
	case MAC_PROP_DUPLEX:
		ASSERT(pr_valsize >= sizeof (link_duplex_t));
		bcopy(&port->link_duplex, pr_val,
		    sizeof (link_duplex_t));
		break;
	case MAC_PROP_SPEED:
		ASSERT(pr_valsize >= sizeof (uint64_t));
		tmp = port->link_speed * 1000000ull;
		bcopy(&tmp, pr_val, sizeof (tmp));
		break;
	case MAC_PROP_AUTONEG:
		*(uint8_t *)pr_val = port->param_adv_autoneg_cap;
		break;
	case MAC_PROP_FLOWCTRL:
		ASSERT(pr_valsize >= sizeof (uint32_t));
		flow_control = LINK_FLOWCTRL_NONE;
		bcopy(&flow_control, pr_val, sizeof (flow_control));
		break;
	case MAC_PROP_ADV_10GFDX_CAP:
		*(uint8_t *)pr_val = port->param_adv_10000fdx_cap;
		break;
	case MAC_PROP_EN_10GFDX_CAP:
		*(uint8_t *)pr_val = port->param_en_10000fdx_cap;
		break;
	case MAC_PROP_ADV_1000FDX_CAP:
		*(uint8_t *)pr_val = port->param_adv_1000fdx_cap;
		break;
	case MAC_PROP_EN_1000FDX_CAP:
		*(uint8_t *)pr_val = port->param_en_1000fdx_cap;
		break;
	case MAC_PROP_ADV_100FDX_CAP:
		*(uint8_t *)pr_val = port->param_adv_100fdx_cap;
		break;
	case MAC_PROP_EN_100FDX_CAP:
		*(uint8_t *)pr_val = port->param_en_100fdx_cap;
		break;
	default:
		err = EINVAL;
		break;
	}
	return (err);
}

/*ARGSUSED*/
static void
mcxe_m_propinfo(void *arg, const char *pr_name, mac_prop_id_t pr_num,
    mac_prop_info_handle_t prh)
{
	switch (pr_num) {
	case MAC_PROP_DUPLEX:
	case MAC_PROP_SPEED:
	case MAC_PROP_ADV_100FDX_CAP:
	case MAC_PROP_ADV_1000FDX_CAP:
	case MAC_PROP_ADV_10GFDX_CAP:
	case MAC_PROP_AUTONEG:
	case MAC_PROP_EN_10GFDX_CAP:
	case MAC_PROP_EN_1000FDX_CAP:
	case MAC_PROP_EN_100FDX_CAP:
	case MAC_PROP_FLOWCTRL:
		mac_prop_info_set_perm(prh, MAC_PROP_PERM_READ);
		break;

	case MAC_PROP_MTU:
		mac_prop_info_set_range_uint32(prh, MCXE_DEFAULT_MTU,
		    MCXE_MAX_MTU);
		break;
	default:
		break;
	}
}

#define	MCXE_MAC_CALLBACK_FLAGS \
	(MC_GETCAPAB | MC_SETPROP | MC_GETPROP | MC_PROPINFO)

static mac_callbacks_t mcxe_mac_callbacks = {
	MCXE_MAC_CALLBACK_FLAGS,
	mcxe_m_stat,
	mcxe_m_start,
	mcxe_m_stop,
	mcxe_m_promisc,
	mcxe_m_multicst,
	mcxe_m_unicst,
	mcxe_m_tx,
	NULL,
	NULL,
	mcxe_m_getcapab,
	NULL,
	NULL,
	mcxe_m_setprop,
	mcxe_m_getprop,
	mcxe_m_propinfo
};


/*
 * ====== module initialization support functions ======
 */
static void
mcxe_init_link_params(mcxe_port_t *port)
{
	port->param_en_10000fdx_cap = 1;
	port->param_en_1000fdx_cap = 1;
	port->param_en_100fdx_cap = 0;
	port->param_adv_10000fdx_cap = 1;
	port->param_adv_1000fdx_cap = 1;
	port->param_adv_100fdx_cap = 0;

	port->param_pause_cap = 1;
	port->param_asym_pause_cap = 1;
	port->param_rem_fault = 0;

	port->param_adv_autoneg_cap = 1;
	port->param_adv_pause_cap = 1;
	port->param_adv_asym_pause_cap = 1;
	port->param_adv_rem_fault = 0;

	port->param_lp_10000fdx_cap = 0;
	port->param_lp_1000fdx_cap = 0;
	port->param_lp_100fdx_cap = 0;
	port->param_lp_autoneg_cap = 0;
	port->param_lp_pause_cap = 0;
	port->param_lp_asym_pause_cap = 0;
	port->param_lp_rem_fault = 0;
}

static int
mcxe_get_conf_prop(mcxe_port_t *port,
    char *propname,	/* name of the property */
    int minval,		/* minimum acceptable value */
    int maxval,		/* maximim acceptable value */
    int defval)		/* default value */
{
	int value;

	/*
	 * Call ddi_prop_get_int() to read the conf settings
	 */
	value = ddi_prop_get_int(DDI_DEV_T_ANY, port->devinfo,
	    DDI_PROP_DONTPASS, propname, defval);

	if (value > maxval)
		value = maxval;

	if (value < minval)
		value = minval;

	return (value);
}

/*
 * mcxe_get_drv_conf - Get driver configurations set in mcxe.conf
 */
static void
mcxe_get_drv_conf(mcxe_port_t *port)
{
	/*
	 * Jumbo frame configuration:
	 *    default_mtu
	 */
	port->default_mtu = mcxe_get_conf_prop(port, PROP_DEFAULT_MTU,
	    ETHERMIN, MCXE_MAX_MTU, MCXE_DEFAULT_MTU);
}

/*
 * mcxe_create_profile()
 */
static void
mcxe_create_profile(mcxe_port_t *port)
{
	uint32_t max_rx_size;

	port->num_tx_rings = MCXE_TX_RING_NUM;
	port->num_rx_rings = MCXE_RX_RING_NUM;
	port->rx_ring_size = MCXE_RX_SLOTS;
	port->tx_ring_size = MCXE_TX_SLOTS;
	port->max_tx_frags = MCXE_MAX_FRAGS;
	port->tx_copy_thresh = 512;
	port->rx_copy_thresh = 256;
	port->max_frame_size = port->default_mtu +
	    sizeof (struct ether_vlan_header) + ETHERFCSL;

	/*
	 * Set rx buffer size
	 */
	max_rx_size = port->max_frame_size + MCXE_IPHDR_ALIGN_ROOM;
	port->rx_buff_size = (max_rx_size + 0x3ff) & (~0x3ff); /* 1K alligned */
	port->tx_buff_size = port->rx_buff_size;

	mcxe_init_link_params(port);
}

/*
 * mcxe_free_rings()
 */
static void
mcxe_free_rings(mcxe_port_t *port)
{
	struct mcxe_tx_ring *tx_ring;
	struct mcxe_rx_ring *rx_ring;
	int i;

	if (port->tx_rings) {
		for (i = 0; i < port->num_tx_rings; i++) {
			tx_ring = &port->tx_rings[i];
			mutex_destroy(&tx_ring->tc_lock);
			mutex_destroy(&tx_ring->freetxbuf_lock);
			mutex_destroy(&tx_ring->txbuf_lock);
			if (tx_ring->txbuf_head) {
				kmem_free(tx_ring->txbuf_head,
				    MCXE_TX_SLOTS * sizeof (mcxe_queue_item_t));
				tx_ring->txbuf_head = NULL;
			}
		}

		kmem_free(port->tx_rings, MCXE_TX_RING_MAX *
		    sizeof (struct mcxe_tx_ring));
		port->tx_rings = NULL;
	}

	if (port->rx_rings) {
		for (i = 0; i < port->num_rx_rings; i++) {
			rx_ring = &port->rx_rings[i];
			rw_destroy(&rx_ring->rc_rwlock);
			mutex_destroy(&rx_ring->rx_lock);
		}

		kmem_free(port->rx_rings, MCXE_RX_RING_MAX *
		    sizeof (struct mcxe_rx_ring));
		port->rx_rings = NULL;
	}
}

/*
 * mcxe_alloc_rings()
 */
static void
mcxe_alloc_rings(mcxe_port_t *port)
{
	struct mcxe_tx_ring *tx_ring;
	struct mcxe_rx_ring *rx_ring;
	int i;

	/* Allocate tx rings */
	port->tx_rings = kmem_zalloc(MCXE_TX_RING_MAX *
	    sizeof (struct mcxe_tx_ring), KM_SLEEP);
	for (i = 0; i < port->num_tx_rings; i++) {
		tx_ring = &port->tx_rings[i];
		tx_ring->port = port;
		/* initialize required tx locks */
		mutex_init(&tx_ring->txbuf_lock, NULL, MUTEX_DRIVER,
		    DDI_INTR_PRI(port->mcxnex_state->hs_intrmsi_pri));
		mutex_init(&tx_ring->freetxbuf_lock, NULL, MUTEX_DRIVER,
		    DDI_INTR_PRI(port->mcxnex_state->hs_intrmsi_pri));
		mutex_init(&tx_ring->tc_lock, NULL, MUTEX_DRIVER,
		    DDI_INTR_PRI(port->mcxnex_state->hs_intrmsi_pri));
		/* allocate tx buf array */
		tx_ring->txbuf_head = kmem_zalloc(
		    MCXE_TX_SLOTS * sizeof (mcxe_queue_item_t), KM_SLEEP);
	}

	/* Allocate rx rings */
	port->rx_rings = kmem_zalloc(MCXE_RX_RING_MAX *
	    sizeof (struct mcxe_rx_ring), KM_SLEEP);
	for (i = 0; i < port->num_rx_rings; i++) {
		rx_ring = &port->rx_rings[i];
		rx_ring->port = port;
		/* initialize required rx locks */
		mutex_init(&rx_ring->rx_lock, NULL, MUTEX_DRIVER,
		    DDI_INTR_PRI(port->mcxnex_state->hs_intrmsi_pri));
		rw_init(&rx_ring->rc_rwlock, NULL, RW_DRIVER,
		    DDI_INTR_PRI(port->mcxnex_state->hs_intrmsi_pri));
	}
}

typedef struct {
	offset_t	index;
	char		*name;
} mcxe_ksindex_t;

static const mcxe_ksindex_t mcxe_driverinfo[] = {
	{ 0,				"tx_free"		},
	{ 1,				"tx_bcopy"		},
	{ 2,				"tx_nobd"		},
	{ 3,				"tx_nobuf"		},
	{ 4,				"tx_bindfail"		},
	{ 5,				"tx_bindexceed"		},
	{ 6,				"tx_alloc_fail"		},
	{ 7,				"tx_pullup"		},
	{ 8,				"tx_drop"		},
	{ 9,				"tx_resched_needed"	},
	{ 10,				"tx_resched"		},

	{ 11,				"num_tx_rings"		},
	{ 12,				"tx_ring_size"		},
	{ 13,				"tx_buff_size"		},
	{ 14,				"max_tx_frags"		},
	{ 15,				"tx_copy_thresh"	},

	{ 16,				"num_rx_rings"		},
	{ 17,				"rx_ring_size"		},
	{ 18,				"rx_buff_size"		},
	{ 19,				"rx_copy_thresh"	},
	{ 20,				"rx_free"		},
	{ 21,				"rx_bind"		},
	{ 22,				"rx_bcopy"		},
	{ 23,				"rx_postfail"		},
	{ 24,				"rx_allocfail"		},

	{ -1,				NULL 			}
};

static int
mcxe_driverinfo_update(kstat_t *ksp, int flag)
{
	mcxe_port_t *port;
	kstat_named_t *knp;
	struct mcxe_tx_ring *tx_ring;
	struct mcxe_rx_ring *rx_ring;

	if (flag != KSTAT_READ)
		return (EACCES);

	port = ksp->ks_private;
	tx_ring = &port->tx_rings[0];
	rx_ring = &port->rx_rings[0];
	knp = ksp->ks_data;

	(knp++)->value.ui64 = tx_ring->tx_free;
	(knp++)->value.ui64 = tx_ring->tx_bcopy;
	(knp++)->value.ui64 = tx_ring->tx_nobd;
	(knp++)->value.ui64 = tx_ring->tx_nobuf;
	(knp++)->value.ui64 = tx_ring->tx_bindfail;
	(knp++)->value.ui64 = tx_ring->tx_bindexceed;
	(knp++)->value.ui64 = tx_ring->tx_alloc_fail;
	(knp++)->value.ui64 = tx_ring->tx_pullup;
	(knp++)->value.ui64 = tx_ring->tx_drop;
	(knp++)->value.ui64 = tx_ring->tx_resched_needed;
	(knp++)->value.ui64 = tx_ring->tx_resched;
	(knp++)->value.ui64 = port->num_tx_rings;
	(knp++)->value.ui64 = port->tx_ring_size;
	(knp++)->value.ui64 = port->tx_buff_size;
	(knp++)->value.ui64 = port->max_tx_frags;
	(knp++)->value.ui64 = port->tx_copy_thresh;


	(knp++)->value.ui64 = port->num_rx_rings;
	(knp++)->value.ui64 = port->rx_ring_size;
	(knp++)->value.ui64 = port->rx_buff_size;
	(knp++)->value.ui64 = port->rx_copy_thresh;
	(knp++)->value.ui64 = rx_ring->rx_free;
	(knp++)->value.ui64 = rx_ring->rx_bind;
	(knp++)->value.ui64 = rx_ring->rx_bcopy;
	(knp++)->value.ui64 = rx_ring->rx_postfail;
	(knp++)->value.ui64 = rx_ring->rx_allocfail;

	return (0);
}

static kstat_t *
mcxe_init_kstats(mcxe_port_t *port, int instance)
{
	const mcxe_ksindex_t *ksip = mcxe_driverinfo;
	kstat_t *ksp;
	kstat_named_t *knp;
	char *np;
	size_t size;

	size = sizeof (mcxe_driverinfo) / sizeof (mcxe_ksindex_t);
	ksp = kstat_create(MCXE_MODULE_NAME, instance, "driverinfo", "net",
	    KSTAT_TYPE_NAMED, size - 1, KSTAT_FLAG_PERSISTENT);
	if (ksp == NULL)
		return (NULL);

	ksp->ks_private = port;
	ksp->ks_update = mcxe_driverinfo_update;
	for (knp = ksp->ks_data; (np = ksip->name) != NULL; ++knp, ++ksip)
		kstat_named_init(knp, np, KSTAT_DATA_UINT64);
	kstat_install(ksp);

	return (ksp);
}

static int
mcxe_mac_register(mcxe_port_t *port)
{
	mac_register_t *mac;
	int ret;

	if (mcxe_get_primary_mac(port) != DDI_SUCCESS)
		return (DDI_FAILURE);

	if ((mac = mac_alloc(MAC_VERSION)) == NULL) {
		cmn_err(CE_WARN, "!mcxe_mac_register: invalid MAC version");
		return (DDI_FAILURE);
	}

	mac->m_type_ident = MAC_PLUGIN_IDENT_ETHER;
	mac->m_driver = port;
	mac->m_dip = port->devinfo;
	mac->m_src_addr = ((uint8_t *)&port->hw_mac) + 2;
	mac->m_callbacks = &mcxe_mac_callbacks;
	mac->m_min_sdu = 0;
	mac->m_max_sdu = port->default_mtu;
	mac->m_margin = VLAN_TAGSZ;

	ret = mac_register(mac, &port->mac_hdl);
	mac_free(mac);
	if (ret) {
		cmn_err(CE_WARN, "!mcxe%d: failed to register mac (ret %d)",
		    port->instance, ret);
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

static int
mcxe_unattach(mcxe_port_t *port, int port_inst)
{
	int i, ret;

	if (mcxe_verbose)
		cmn_err(CE_NOTE, "!==>mcxe_unattach() mcxe%d.\n", port_inst);

	for (i = 0; port->rxb_pending && i < 100; i++) {
		/* wait for rxb to be freed ... */
		drv_usecwait(100);
	}
	if (port->rxb_pending) /* rxb is still in-use */
		return (DDI_FAILURE);

	if (port->progress & MCXE_PRG_SOFT_INTR) {
		mcxnex_priv_async_cb_set(port->mcxnex_state, NULL);
		ddi_remove_softintr(port->link_softintr_id);
		port->progress &= ~MCXE_PRG_SOFT_INTR;
	}

	if (port->mcxe_kstats != NULL) {
		kstat_delete(port->mcxe_kstats);
		port->mcxe_kstats = NULL;
	}

	if (port->periodic_id != NULL) {
		ddi_periodic_delete(port->periodic_id);
		port->periodic_id = NULL;
	}

	if (port->progress & MCXE_PRG_MAC_REGISTER) {
		ret = mac_unregister(port->mac_hdl);
		if (ret != DDI_SUCCESS) {
			cmn_err(CE_WARN, "!mac unregistered failed");
			return (DDI_FAILURE);
		}
		port->progress &= ~MCXE_PRG_MAC_REGISTER;
	}

	if (port->progress & MCXE_PRG_ALLOC_RSRC) {
		mcxe_free_rsrc(port);
		port->progress &= ~MCXE_PRG_ALLOC_RSRC;
	}

	if (port->progress & MCXE_PRG_ALLOC_RINGS) {
		(void) mcxe_free_rings(port);
		port->progress &= ~MCXE_PRG_ALLOC_RINGS;
	}

	if (port->progress & MCXE_PRG_MUTEX) {
		mutex_destroy(&port->link_softintr_lock);
		rw_destroy(&port->port_rwlock);
		mutex_destroy(&port->port_lock);
		port->progress &= ~MCXE_PRG_MUTEX;
	}

	if (port->progress & MCXE_PRG_ALLOC_SOFTSTAT)
		ddi_soft_state_free(mcxe_statep, port_inst);

	return (DDI_SUCCESS);
}


/*
 * ====== Module Initialization Functions ======
 */
static int
mcxe_attach(dev_info_t *devinfo, ddi_attach_cmd_t cmd)
{
	mcxnex_ppd_t *ppd;
	mcxe_port_t *port;
	int port_inst;
	int ret;

	/*
	 * Check the command and perform corresponding operations
	 */
	switch (cmd) {
	default:
	case DDI_RESUME:
		return (DDI_FAILURE);

	case DDI_ATTACH:
		break;
	}

	/* get and verify the parent data info */
	ppd = ddi_get_parent_data(devinfo);
	if (ppd == NULL) {
		cmn_err(CE_WARN, "!mcxe_attach: no parent data!\n");
		return (DDI_FAILURE);
	}
	if (ppd->cp_state == NULL) {
		cmn_err(CE_WARN, "!mcxe_attach: invalid parent data!\n");
		return (DDI_FAILURE);
	}
	if (!ppd->cp_state->hs_devlim.reserved_lkey) {
		cmn_err(CE_WARN, "!mcxe_attach: device has no private lkey");
		return (DDI_FAILURE);
	}

	/* allocat port soft structure */
	port_inst = ddi_get_instance(devinfo);
	ret = ddi_soft_state_zalloc(mcxe_statep, port_inst);
	if (ret != DDI_SUCCESS) {
		cmn_err(CE_WARN,
		    "!mcxe_attach: unable to allocate soft mcxe%d state\n",
		    port_inst);
		return (DDI_FAILURE);
	}
	port = ddi_get_soft_state(mcxe_statep, port_inst);
	port->progress |= MCXE_PRG_ALLOC_SOFTSTAT;

	port->devinfo = devinfo;
	port->instance = port_inst;
	port->mcxnex_state = ppd->cp_state;
	port->phys_port_num = ppd->cp_port + 1;
	ASSERT(port->phys_port_num <= MCXE_MAX_PORT_NUM);

	/* get driver configuration parameters */
	mcxe_get_drv_conf(port);
	/* initialize driver parameters */
	mcxe_create_profile(port);

	/* init port locks */
	mutex_init(&port->port_lock, NULL, MUTEX_DRIVER,
	    DDI_INTR_PRI(port->mcxnex_state->hs_intrmsi_pri));
	rw_init(&port->port_rwlock, NULL, RW_DRIVER,
	    DDI_INTR_PRI(port->mcxnex_state->hs_intrmsi_pri));
	mutex_init(&port->link_softintr_lock, NULL, MUTEX_DRIVER,
	    DDI_INTR_PRI(port->mcxnex_state->hs_intrmsi_pri));
	port->progress |= MCXE_PRG_MUTEX;

	/* allocate rx/tx rings array */
	mcxe_alloc_rings(port);
	port->progress |= MCXE_PRG_ALLOC_RINGS;

	/* register to MAC */
	if (mcxe_mac_register(port) != DDI_SUCCESS)
		goto attach_fail;
	port->progress |= MCXE_PRG_MAC_REGISTER;
	port->link_state = LINK_STATE_UNKNOWN;
	mac_link_update(port->mac_hdl, LINK_STATE_UNKNOWN);
	port->periodic_id = ddi_periodic_add(mcxe_port_timer, port,
	    MCXE_CYCLIC_PERIOD, DDI_IPL_0);
	if (port->periodic_id == NULL) {
		cmn_err(CE_WARN,
		    "!mcxe%d: Failed to add the link check timer.", port_inst);
		goto attach_fail;
	}

	/* create & initialise named kstats */
	port->mcxe_kstats = mcxe_init_kstats(port, port_inst);

	/* add soft interrupt handler */
	ret = ddi_add_softintr(devinfo, DDI_SOFTINT_MED,
	    &port->link_softintr_id, NULL, NULL, mcxe_link_softintr_handler,
	    (caddr_t)port);
	if (ret != DDI_SUCCESS)
		goto attach_fail;
	mcxnex_priv_async_cb_set(port->mcxnex_state, mcxe_async_handler);
	port->progress |= MCXE_PRG_SOFT_INTR;

	if (mcxe_verbose) {
		cmn_err(CE_NOTE, "!mcxe_attach() OK on instance %d, port %d\n",
		    port_inst, port->phys_port_num);
	}

	return (DDI_SUCCESS);

attach_fail:
	cmn_err(CE_WARN, "!mcxe_attach() FAIL on instance%d(progress=0x%x).\n",
	    port_inst, port->progress);
	(void) mcxe_unattach(port, port_inst);
	return (DDI_FAILURE);
}

static int
mcxe_detach(dev_info_t *devinfo, ddi_detach_cmd_t cmd)
{
	int port_inst;
	mcxe_port_t *port;

	/*
	 * Check detach command
	 */
	switch (cmd) {
	default:
	case DDI_SUSPEND:
		return (DDI_FAILURE);

	case DDI_DETACH:
		break;
	}

	port_inst = ddi_get_instance(devinfo);
	port = ddi_get_soft_state(mcxe_statep, port_inst);
	ASSERT(port != NULL);

	return (mcxe_unattach(port, port_inst));
}


DDI_DEFINE_STREAM_OPS(mcxe_dev_ops,
	nulldev,	/* identify */
	nulldev,	/* probe */
	mcxe_attach,	/* attach */
	mcxe_detach,	/* detach */
	nodev,		/* reset */
	NULL,		/* cb_ops */
	D_MP,		/* bus_ops */
	NULL,		/* power */
	ddi_quiesce_not_needed	/* quiesce */
	/* chip quiesce is done in parent mcxnex driver */
);

static struct modldrv mcxe_modldrv = {
	&mod_driverops,		/* Type of module.  This one is a driver */
	mcxe_ident,		/* Discription string */
	&mcxe_dev_ops		/* driver ops */
};

static struct modlinkage mcxe_modlinkage = {
	MODREV_1, &mcxe_modldrv, NULL
};

int
_init(void)
{
	int ret;

	ret = ddi_soft_state_init(&mcxe_statep, sizeof (mcxe_port_t), 0);
	if (ret != DDI_SUCCESS)
		return (ret);

	mac_init_ops(&mcxe_dev_ops, MCXE_MODULE_NAME);

	ret = mod_install(&mcxe_modlinkage);
	if (ret != DDI_SUCCESS) {
		mac_fini_ops(&mcxe_dev_ops);
		ddi_soft_state_fini(&mcxe_statep);
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

int
_fini(void)
{
	int ret;

	ret = mod_remove(&mcxe_modlinkage);
	if (ret != DDI_SUCCESS)
		return (DDI_FAILURE);

	mac_fini_ops(&mcxe_dev_ops);
	ddi_soft_state_fini(&mcxe_statep);

	return (DDI_SUCCESS);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&mcxe_modlinkage, modinfop));
}
