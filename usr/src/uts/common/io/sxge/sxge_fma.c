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

#include "sxge.h"
#include <sys/ddifm.h>
#include <sys/fm/protocol.h>
#include <sys/fm/util.h>
#include <sys/fm/io/ddi.h>

static sxge_fm_ereport_attr_t
*sxge_fm_get_ereport_attr(sxge_fm_ereport_id_t);

static int
sxge_fm_error_cb(dev_info_t *dip, ddi_fm_error_t *err, const void *impl_data);

sxge_fm_ereport_attr_t	sxge_fm_ereport_mac[] = {
	{SXGE_FM_EREPORT_XMAC_LINK_DOWN,	"10g_link_down",
						DDI_FM_DEVICE_INVAL_STATE,
						DDI_SERVICE_LOST},
	{SXGE_FM_EREPORT_XMAC_TX_LINK_FAULT,	"10g_tx_link_fault",
						DDI_FM_DEVICE_INVAL_STATE,
						DDI_SERVICE_DEGRADED},
	{SXGE_FM_EREPORT_XMAC_RX_LINK_FAULT,	"10g_rx_link_fault",
						DDI_FM_DEVICE_INVAL_STATE,
						DDI_SERVICE_DEGRADED},
	{SXGE_FM_EREPORT_MAC_LINK_DOWN,		"1g_link_down",
						DDI_FM_DEVICE_INVAL_STATE,
						DDI_SERVICE_LOST},
	{SXGE_FM_EREPORT_MAC_REMOTE_FAULT,	"1g_remote_fault",
						DDI_FM_DEVICE_INVAL_STATE,
						DDI_SERVICE_DEGRADED},
};

sxge_fm_ereport_attr_t sxge_fm_ereport_pfc[] = {
	{SXGE_FM_EREPORT_PFC_TCAM_ERR,		"classifier_tcam_err",
						DDI_FM_DEVICE_INTERN_UNCORR,
						DDI_SERVICE_UNAFFECTED},
	{SXGE_FM_EREPORT_PFC_VLAN_ERR,		"classifier_vlan_err",
						DDI_FM_DEVICE_INTERN_UNCORR,
						DDI_SERVICE_UNAFFECTED},
	{SXGE_FM_EREPORT_PFC_HASHT_LOOKUP_ERR,	"classifier_hasht_lookup_err",
						DDI_FM_DEVICE_INTERN_UNCORR,
						DDI_SERVICE_UNAFFECTED},
	{SXGE_FM_EREPORT_PFC_ACCESS_FAIL,	"classifier_access_fail",
						DDI_FM_DEVICE_NO_RESPONSE,
						DDI_SERVICE_DEGRADED}
};

sxge_fm_ereport_attr_t sxge_fm_ereport_rdc[] = {
	{SXGE_FM_EREPORT_RDC_RBR_RING_ERR,	"rbr_ring_error",
						DDI_FM_DEVICE_INTERN_UNCORR,
						DDI_SERVICE_LOST},
	{SXGE_FM_EREPORT_RDC_RCR_RING_ERR,	"rcr_ring_error",
						DDI_FM_DEVICE_INTERN_UNCORR,
						DDI_SERVICE_LOST},
	{SXGE_FM_EREPORT_RDC_RBR_PRE_PAR,	"rbr_pre_par",
						DDI_FM_DEVICE_INTERN_UNCORR,
						DDI_SERVICE_LOST},
	{SXGE_FM_EREPORT_RDC_RCR_SHA_PAR,	"rcr_sha_par",
						DDI_FM_DEVICE_INTERN_UNCORR,
						DDI_SERVICE_LOST},
	{SXGE_FM_EREPORT_RDC_RCR_ACK_ERR,	"rcr_ack_err",
						DDI_FM_DEVICE_INTERN_UNCORR,
						DDI_SERVICE_LOST},
	{SXGE_FM_EREPORT_RDC_RSP_DAT_ERR,	"rsp_dat_err",
						DDI_FM_DEVICE_INTERN_UNCORR,
						DDI_SERVICE_LOST},
	{SXGE_FM_EREPORT_RDC_RBR_TMOUT,		"rxdma_rbr_tmout",
						DDI_FM_DEVICE_INTERN_UNCORR,
						DDI_SERVICE_LOST},
	{SXGE_FM_EREPORT_RDC_REQUEST_REJECT,	"rxdma_request_reject",
						DDI_FM_DEVICE_INTERN_UNCORR,
						DDI_SERVICE_LOST},
	{SXGE_FM_EREPORT_RDC_SHADOW_FULL,	"rxdma_shadow_full",
						DDI_FM_DEVICE_INVAL_STATE,
						DDI_SERVICE_DEGRADED},
	{SXGE_FM_EREPORT_RDC_FIFO_ERR,		"rxdma_fifo_err",
						DDI_FM_DEVICE_INTERN_UNCORR,
						DDI_SERVICE_DEGRADED},
	{SXGE_FM_EREPORT_RDC_RESET_FAIL,	"rxdma_reset_fail",
						DDI_FM_DEVICE_INTERN_UNCORR,
						DDI_SERVICE_DEGRADED}
};

sxge_fm_ereport_attr_t sxge_fm_ereport_rxvmac[] = {
	{SXGE_FM_EREPORT_RXVMAC_LINKDOWN,	"rxvmac_linkfault",
						DDI_FM_DEVICE_INTERN_UNCORR,
						DDI_SERVICE_UNAFFECTED},
	{SXGE_FM_EREPORT_RXVMAC_RESET_FAIL,	"rxvmac_reset_fail",
						DDI_FM_DEVICE_NO_RESPONSE,
						DDI_SERVICE_UNAFFECTED},
};

sxge_fm_ereport_attr_t sxge_fm_ereport_tdc[] = {
	{SXGE_FM_EREPORT_TDC_PKT_PRT_ERR,	"txdma_pkt_prt_err",
						DDI_FM_DEVICE_INTERN_UNCORR,
						DDI_SERVICE_DEGRADED},
	{SXGE_FM_EREPORT_TDC_CONF_PART_ERR,	"txdma_conf_part_err",
						DDI_FM_DEVICE_INTERN_UNCORR,
						DDI_SERVICE_DEGRADED},
	{SXGE_FM_EREPORT_TDC_NACK_PKT_RD,	"txdma_nack_pkt_rd",
						DDI_FM_DEVICE_NO_RESPONSE,
						DDI_SERVICE_DEGRADED},
	{SXGE_FM_EREPORT_TDC_NACK_PREF,		"txdma_nack_pref",
						DDI_FM_DEVICE_NO_RESPONSE,
						DDI_SERVICE_DEGRADED},
	{SXGE_FM_EREPORT_TDC_PREF_BUF_PAR_ERR,	"txdma_pref_buf_par_err",
						DDI_FM_DEVICE_INTERN_UNCORR,
						DDI_SERVICE_DEGRADED},
	{SXGE_FM_EREPORT_TDC_TX_RING_OFLOW,	"txdma_tx_ring_oflow",
						DDI_FM_DEVICE_INVAL_STATE,
						DDI_SERVICE_DEGRADED},
	{SXGE_FM_EREPORT_TDC_PKT_SIZE_ERR,	"txdma_pkt_size_err",
						DDI_FM_DEVICE_INVAL_STATE,
						DDI_SERVICE_DEGRADED},
	{SXGE_FM_EREPORT_TDC_MBOX_ERR,		"txdma_mbox_err",
						DDI_FM_DEVICE_NO_RESPONSE,
						DDI_SERVICE_DEGRADED},
	{SXGE_FM_EREPORT_TDC_DESC_NUM_PTR_ERR,	"txdma_desc_num_ptr_err",
						DDI_FM_DEVICE_NO_RESPONSE,
						DDI_SERVICE_DEGRADED},
	{SXGE_FM_EREPORT_TDC_DESC_LENGTH_ERR,	"txdma_desc_length_err",
						DDI_FM_DEVICE_NO_RESPONSE,
						DDI_SERVICE_DEGRADED},
	{SXGE_FM_EREPORT_TDC_PREMATURE_SOP_ERR,	"txdma_premature_sop_err",
						DDI_FM_DEVICE_NO_RESPONSE,
						DDI_SERVICE_DEGRADED},
	{SXGE_FM_EREPORT_TDC_SOP_BIT_ERR,	"txdma_sop_bit_err",
						DDI_FM_DEVICE_NO_RESPONSE,
						DDI_SERVICE_DEGRADED},
	{SXGE_FM_EREPORT_TDC_REJECT_RESP_ERR,	"txdma_reject_resp_err",
						DDI_FM_DEVICE_INVAL_STATE,
						DDI_SERVICE_DEGRADED},
	{SXGE_FM_EREPORT_TDC_RESET_FAIL,	"txdma_reset_fail",
						DDI_FM_DEVICE_NO_RESPONSE,
						DDI_SERVICE_LOST},
};

sxge_fm_ereport_attr_t sxge_fm_ereport_txvmac[] = {
	{SXGE_FM_EREPORT_TXVMAC_UNDERFLOW,	"txvmac_underflow",
						DDI_FM_DEVICE_INVAL_STATE,
						DDI_SERVICE_UNAFFECTED},
	{SXGE_FM_EREPORT_TXVMAC_OVERFLOW,	"txvmac_overflow",
						DDI_FM_DEVICE_INVAL_STATE,
						DDI_SERVICE_UNAFFECTED},
	{SXGE_FM_EREPORT_TXVMAC_TXFIFO_XFR_ERR,	"txvmac_txfifo_xfr_err",
						DDI_FM_DEVICE_INVAL_STATE,
						DDI_SERVICE_UNAFFECTED},
	{SXGE_FM_EREPORT_TXVMAC_MAX_PKT_ERR,	"txvmac_max_pkt_err",
						DDI_FM_DEVICE_INTERN_UNCORR,
						DDI_SERVICE_UNAFFECTED},
	{SXGE_FM_EREPORT_TXVMAC_RESET_FAIL,	"txvmac_reset_fail",
						DDI_FM_DEVICE_NO_RESPONSE,
						DDI_SERVICE_UNAFFECTED},
};

sxge_fm_ereport_attr_t sxge_fm_ereport_sw[] = {
	{SXGE_FM_EREPORT_SW_INVALID_PORT_NUM,	"invalid_port_num",
						DDI_FM_DEVICE_INVAL_STATE,
						DDI_SERVICE_LOST},
	{SXGE_FM_EREPORT_SW_INVALID_CHAN_NUM,	"invalid_chan_num",
						DDI_FM_DEVICE_INVAL_STATE,
						DDI_SERVICE_LOST},
	{SXGE_FM_EREPORT_SW_INVALID_PARAM,	"invalid_param",
						DDI_FM_DEVICE_INVAL_STATE,
						DDI_SERVICE_LOST},
};

void
sxge_fm_init(sxge_t *sxgep, ddi_device_acc_attr_t *reg_attr,
	ddi_dma_attr_t *dma_attr)
{
	ddi_iblock_cookie_t iblk;

	/*
	 * fm-capable in sxge.conf can be used to set fm_capabilities.
	 * If fm-capable is not defined, then the last argument passed to
	 * ddi_prop_get_int will be returned as the capabilities.
	 */
	sxgep->fm_capabilities = ddi_prop_get_int(DDI_DEV_T_ANY, sxgep->dip,
	    DDI_PROP_DONTPASS | DDI_PROP_NOTPROM, "fm-capable",
	    DDI_FM_EREPORT_CAPABLE | DDI_FM_ERRCB_CAPABLE);

	/*
	 * Register capabilities with IO Fault Services. The capabilities
	 * set above may not be supported by the parent nexus, in that case
	 * some capability bits may be cleared.
	 */
	if (sxgep->fm_capabilities)
		ddi_fm_init(sxgep->dip, &sxgep->fm_capabilities, &iblk);

	/*
	 * Initialize pci ereport capabilities if ereport capable
	 */
	if (DDI_FM_EREPORT_CAP(sxgep->fm_capabilities) ||
	    DDI_FM_ERRCB_CAP(sxgep->fm_capabilities)) {
		pci_ereport_setup(sxgep->dip);
	}

	/* Register error callback if error callback capable */
	if (DDI_FM_ERRCB_CAP(sxgep->fm_capabilities)) {
		ddi_fm_handler_register(sxgep->dip,
		    sxge_fm_error_cb, (void*) sxgep);
	}

	/*
	 * DDI_FLGERR_ACC indicates:
	 * o Driver will check its access handle(s) for faults on
	 *   a regular basis by calling ddi_fm_acc_err_get
	 * o Driver is able to cope with incorrect results of I/O
	 *   operations resulted from an I/O fault
	 */
	if (DDI_FM_ACC_ERR_CAP(sxgep->fm_capabilities)) {
		reg_attr->devacc_attr_access  = DDI_FLAGERR_ACC;
	} else {
		reg_attr->devacc_attr_access  = DDI_DEFAULT_ACC;
	}

	/*
	 * DDI_DMA_FLAGERR indicates:
	 * o Driver will check its DMA handle(s) for faults on a
	 *   regular basis using ddi_fm_dma_err_get
	 * o Driver is able to cope with incorrect results of DMA
	 *   operations resulted from an I/O fault
	 */
	if (DDI_FM_DMA_ERR_CAP(sxgep->fm_capabilities))
		dma_attr->dma_attr_flags |= DDI_DMA_FLAGERR;
	else
		dma_attr->dma_attr_flags &= ~DDI_DMA_FLAGERR;

}

void
sxge_fm_fini(sxge_t *sxgep)
{
	/* Only unregister FMA capabilities if we registered some */
	if (sxgep->fm_capabilities) {

		/*
		 * Release any resources allocated by pci_ereport_setup()
		 */
		if (DDI_FM_EREPORT_CAP(sxgep->fm_capabilities) ||
		    DDI_FM_ERRCB_CAP(sxgep->fm_capabilities))
			pci_ereport_teardown(sxgep->dip);

		/*
		 * Un-register error callback if error callback capable
		 */
		if (DDI_FM_ERRCB_CAP(sxgep->fm_capabilities))
			ddi_fm_handler_unregister(sxgep->dip);

		/* Unregister from IO Fault Services */
		ddi_fm_fini(sxgep->dip);
	}
}

/*ARGSUSED*/
/*
 * Simply call pci_ereport_post which generates ereports for errors
 * that occur in the PCI local bus configuration status registers.
 */
static int
sxge_fm_error_cb(dev_info_t *dip, ddi_fm_error_t *err,
	const void *impl_data)
{
	pci_ereport_post(dip, err, NULL);
	return (err->fme_status);
}

static sxge_fm_ereport_attr_t *
sxge_fm_get_ereport_attr(sxge_fm_ereport_id_t ereport_id)
{
	sxge_fm_ereport_attr_t *attr;
	uint8_t	blk_id = (ereport_id >> EREPORT_FM_ID_SHIFT) &
	    EREPORT_FM_ID_MASK;
	uint8_t index = ereport_id & EREPORT_INDEX_MASK;

	switch (blk_id) {
	case FM_SW_ID:
		attr = &sxge_fm_ereport_sw[index];
		break;
	case FM_MAC_ID:
		attr = &sxge_fm_ereport_mac[index];
		break;
	case FM_TXVMAC_ID:
		attr = &sxge_fm_ereport_txvmac[index];
		break;
	case FM_RXVMAC_ID:
		attr = &sxge_fm_ereport_rxvmac[index];
		break;
	case FM_PFC_ID:
		attr = &sxge_fm_ereport_pfc[index];
		break;
	case FM_RDC_ID:
		attr = &sxge_fm_ereport_rdc[index];
		break;
	case FM_TDC_ID:
		attr = &sxge_fm_ereport_tdc[index];
		break;
	default:
		attr = NULL;
	}

	return (attr);
}

static void
sxge_fm_ereport(sxge_t *sxgep, uint8_t err_portn, uint8_t err_chan,
					sxge_fm_ereport_attr_t *ereport)
{
	uint64_t		ena;
	char			eclass[FM_MAX_CLASS];
	char			*err_str;
	sxge_stats_t		*statsp;

	(void) snprintf(eclass, FM_MAX_CLASS, "%s.%s", DDI_FM_DEVICE,
	    ereport->eclass);
	err_str = ereport->str;
	ena = fm_ena_generate(0, FM_ENA_FMT1);
	statsp = sxgep->statsp;

	switch (ereport->index) {
		case SXGE_FM_EREPORT_PFC_TCAM_ERR:
			ddi_fm_ereport_post(sxgep->dip, eclass, ena,
			    DDI_NOSLEEP,
			    FM_VERSION, DATA_TYPE_UINT8, FM_EREPORT_VERS0,
			    ERNAME_DETAILED_ERR_TYPE, DATA_TYPE_STRING, err_str,
			    ERNAME_TCAM_ERR_LOG, DATA_TYPE_UINT32,
			    statsp->pfc_stats.errlog,
			    NULL);
			break;
		case SXGE_FM_EREPORT_PFC_VLAN_ERR:
			ddi_fm_ereport_post(sxgep->dip, eclass, ena,
			    DDI_NOSLEEP,
			    FM_VERSION, DATA_TYPE_UINT8, FM_EREPORT_VERS0,
			    ERNAME_DETAILED_ERR_TYPE, DATA_TYPE_STRING, err_str,
			    ERNAME_VLANTAB_ERR_LOG, DATA_TYPE_UINT32,
			    statsp->pfc_stats.errlog,
			    NULL);
			break;
		case SXGE_FM_EREPORT_PFC_HASHT_LOOKUP_ERR:
			ddi_fm_ereport_post(sxgep->dip, eclass, ena,
			    DDI_NOSLEEP,
			    FM_VERSION, DATA_TYPE_UINT8, FM_EREPORT_VERS0,
			    ERNAME_DETAILED_ERR_TYPE, DATA_TYPE_STRING, err_str,
			    ERNAME_HASHT_LOOKUP_ERR_LOG0, DATA_TYPE_UINT32,
			    statsp->pfc_stats.errlog,
			    ERNAME_HASHT_LOOKUP_ERR_LOG1, DATA_TYPE_UINT32,
			    statsp->pfc_stats.errlog,
			    NULL);
			break;
		case SXGE_FM_EREPORT_RDC_RBR_RING_ERR:
		case SXGE_FM_EREPORT_RDC_RCR_RING_ERR:
		case SXGE_FM_EREPORT_RDC_RBR_TMOUT:
		case SXGE_FM_EREPORT_RDC_RSP_DAT_ERR:
		case SXGE_FM_EREPORT_RDC_RCR_ACK_ERR:
		case SXGE_FM_EREPORT_RDC_FIFO_ERR:
		case SXGE_FM_EREPORT_RDC_REQUEST_REJECT:
		case SXGE_FM_EREPORT_RDC_SHADOW_FULL:
		case SXGE_FM_EREPORT_RDC_RESET_FAIL:
			ddi_fm_ereport_post(sxgep->dip, eclass, ena,
			    DDI_NOSLEEP,
			    FM_VERSION, DATA_TYPE_UINT8, FM_EREPORT_VERS0,
			    ERNAME_DETAILED_ERR_TYPE, DATA_TYPE_STRING, err_str,
			    ERNAME_ERR_PORTN, DATA_TYPE_UINT8, err_portn,
			    ERNAME_ERR_DCHAN, DATA_TYPE_UINT8, err_chan,
			    NULL);
			break;
		case SXGE_FM_EREPORT_RDC_RBR_PRE_PAR:
		case SXGE_FM_EREPORT_RDC_RCR_SHA_PAR:
			{
			uint32_t err_log;
			if (ereport->index == SXGE_FM_EREPORT_RDC_RBR_PRE_PAR)
				err_log = (uint32_t)statsp->
				    rdc_stats[err_chan].errlog;
			else
				err_log = (uint32_t)statsp->
				    rdc_stats[err_chan].errlog;
			ddi_fm_ereport_post(sxgep->dip, eclass, ena,
			    DDI_NOSLEEP,
			    FM_VERSION, DATA_TYPE_UINT8, FM_EREPORT_VERS0,
			    ERNAME_DETAILED_ERR_TYPE, DATA_TYPE_STRING, err_str,
			    ERNAME_ERR_PORTN, DATA_TYPE_UINT8, err_portn,
			    ERNAME_ERR_DCHAN, DATA_TYPE_UINT8, err_chan,
			    ERNAME_RDC_PAR_ERR_LOG, DATA_TYPE_UINT8, err_log,
			    NULL);
			}
			break;

		case SXGE_FM_EREPORT_RXVMAC_LINKDOWN:
			ddi_fm_ereport_post(sxgep->dip, eclass, ena,
			    DDI_NOSLEEP,
			    FM_VERSION, DATA_TYPE_UINT8, FM_EREPORT_VERS0,
			    ERNAME_DETAILED_ERR_TYPE, DATA_TYPE_STRING, err_str,
			    ERNAME_ERR_PORTN, DATA_TYPE_UINT8, err_portn,
			    NULL);
			break;

		case SXGE_FM_EREPORT_TDC_MBOX_ERR:
		case SXGE_FM_EREPORT_TDC_TX_RING_OFLOW:
			ddi_fm_ereport_post(sxgep->dip, eclass, ena,
			    DDI_NOSLEEP,
			    FM_VERSION, DATA_TYPE_UINT8, FM_EREPORT_VERS0,
			    ERNAME_DETAILED_ERR_TYPE, DATA_TYPE_STRING, err_str,
			    ERNAME_ERR_PORTN, DATA_TYPE_UINT8, err_portn,
			    ERNAME_ERR_DCHAN, DATA_TYPE_UINT8, err_chan,
			    NULL);
			break;
		case SXGE_FM_EREPORT_TDC_PREF_BUF_PAR_ERR:
		case SXGE_FM_EREPORT_TDC_NACK_PREF:
		case SXGE_FM_EREPORT_TDC_NACK_PKT_RD:
		case SXGE_FM_EREPORT_TDC_PKT_SIZE_ERR:
		case SXGE_FM_EREPORT_TDC_CONF_PART_ERR:
		case SXGE_FM_EREPORT_TDC_PKT_PRT_ERR:
		case SXGE_FM_EREPORT_TDC_DESC_NUM_PTR_ERR:
		case SXGE_FM_EREPORT_TDC_DESC_LENGTH_ERR:
		case SXGE_FM_EREPORT_TDC_SOP_BIT_ERR:
		case SXGE_FM_EREPORT_TDC_REJECT_RESP_ERR:
		case SXGE_FM_EREPORT_TDC_RESET_FAIL:
			ddi_fm_ereport_post(sxgep->dip, eclass, ena,
			    DDI_NOSLEEP,
			    FM_VERSION, DATA_TYPE_UINT8, FM_EREPORT_VERS0,
			    ERNAME_DETAILED_ERR_TYPE, DATA_TYPE_STRING, err_str,
			    ERNAME_ERR_PORTN, DATA_TYPE_UINT8, err_portn,
			    ERNAME_ERR_DCHAN, DATA_TYPE_UINT8, err_chan,
			    ERNAME_TDC_ERR_LOG1, DATA_TYPE_UINT32,
			    statsp->tdc_stats[err_chan].errlog,
			    ERNAME_TDC_ERR_LOG1, DATA_TYPE_UINT32,
			    statsp->tdc_stats[err_chan].errlog,
			    DATA_TYPE_UINT32,
			    NULL);
			break;
		case SXGE_FM_EREPORT_TXVMAC_UNDERFLOW:
		case SXGE_FM_EREPORT_TXVMAC_OVERFLOW:
		case SXGE_FM_EREPORT_TXVMAC_TXFIFO_XFR_ERR:
		case SXGE_FM_EREPORT_TXVMAC_MAX_PKT_ERR:
		case SXGE_FM_EREPORT_SW_INVALID_PORT_NUM:
		case SXGE_FM_EREPORT_SW_INVALID_CHAN_NUM:
		case SXGE_FM_EREPORT_SW_INVALID_PARAM:
			ddi_fm_ereport_post(sxgep->dip, eclass, ena,
			    DDI_NOSLEEP,
			    FM_VERSION, DATA_TYPE_UINT8, FM_EREPORT_VERS0,
			    ERNAME_DETAILED_ERR_TYPE, DATA_TYPE_STRING, err_str,
			    ERNAME_ERR_PORTN, DATA_TYPE_UINT8, err_portn,
			    NULL);
			break;
	}
}

void
sxge_fm_report_error(sxge_t *sxgep, uint8_t err_portn, uint8_t err_chan,
					sxge_fm_ereport_id_t fm_ereport_id)
{
	sxge_fm_ereport_attr_t		*fm_ereport_attr;

	fm_ereport_attr = sxge_fm_get_ereport_attr(fm_ereport_id);
	if (fm_ereport_attr != NULL &&
	    (DDI_FM_EREPORT_CAP(sxgep->fm_capabilities))) {
		sxge_fm_ereport(sxgep, err_portn, err_chan, fm_ereport_attr);
		ddi_fm_service_impact(sxgep->dip, fm_ereport_attr->impact);
	}
}

int
sxge_fm_check_acc_handle(ddi_acc_handle_t handle)
{
	ddi_fm_error_t err;

	ddi_fm_acc_err_get(handle, &err, DDI_FME_VERSION);
#ifndef	SXGE_FM_S10
	ddi_fm_acc_err_clear(handle, DDI_FME_VERSION);
#endif
	return (err.fme_status);
}

int
sxge_fm_check_dma_handle(ddi_dma_handle_t handle)
{
	ddi_fm_error_t err;

	ddi_fm_dma_err_get(handle, &err, DDI_FME_VERSION);
	return (err.fme_status);
}
