/*
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copyright (c) 2010, Intel Corporation
 * All rights reserved.
 */

/*
 * Copyright (c) 2006
 * Copyright (c) 2007
 *      Damien Bergamini <damien.bergamini@free.fr>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/byteorder.h>
#include <sys/conf.h>
#include <sys/cmn_err.h>
#include <sys/stat.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/strsubr.h>
#include <sys/ethernet.h>
#include <inet/common.h>
#include <inet/nd.h>
#include <inet/mi.h>
#include <sys/note.h>
#include <sys/stream.h>
#include <sys/strsun.h>
#include <sys/modctl.h>
#include <sys/devops.h>
#include <sys/dlpi.h>
#include <sys/mac_provider.h>
#include <sys/mac_wifi.h>
#include <sys/net80211.h>
#include <sys/net80211_proto.h>
#include <sys/varargs.h>
#include <sys/policy.h>
#include <sys/pci.h>

#include "iwp_calibration.h"
#include "iwp_hardware.h"
#include "iwp_base.h"

extern int	iwp_cmd(iwp_sc_t *, int, const void *, int, int);

/*
 * Demand firmware to execute calibrations
 */
int
iwp_require_calibration(iwp_sc_t *sc)
{
	uint32_t rv;
	struct iwp_calib_cfg_cmd cmd;

	(void) memset(&cmd, 0, sizeof (cmd));

	cmd.ucd_calib_cfg.once.is_enable = IWP_CALIB_INIT_CFG_ALL;
	cmd.ucd_calib_cfg.once.start = IWP_CALIB_INIT_CFG_ALL;
	cmd.ucd_calib_cfg.once.send_res = IWP_CALIB_INIT_CFG_ALL;
	cmd.ucd_calib_cfg.flags = IWP_CALIB_INIT_CFG_ALL;

	/*
	 * require ucode execute calibration
	 */
	rv = iwp_cmd(sc, CALIBRATION_CFG_CMD, &cmd, sizeof (cmd), 1);
	if (rv != IWP_SUCCESS) {
		cmn_err(CE_WARN, "iwp_require_calibration(): "
		    "failed to send calibration configure command.\n");
	}

	return (rv);
}

/*
 * save results of calibration from ucode
 */
void
iwp_save_calib_result(iwp_sc_t *sc, iwp_rx_desc_t *desc)
{
	struct iwp_calib_results *res_p = &sc->sc_calib_results;
	struct iwp_calib_hdr *calib_hdr = (struct iwp_calib_hdr *)(desc + 1);
	int len = LE_32(desc->len);

	/*
	 * ensure the size of buffer is not too big
	 */
	len = (len & FH_RSCSR_FRAME_SIZE_MASK) - 4;

	switch (calib_hdr->op_code) {
	case PHY_CALIBRATE_LO_CMD:
		if (!(sc->sc_chip_param->calibration_msk & CALIB_LO_CMD)) {
			return;
		}

		if (NULL == res_p->lo_res) {
			res_p->lo_res = kmem_alloc(len, KM_NOSLEEP);
		}

		if (NULL == res_p->lo_res) {
			cmn_err(CE_WARN, "iwp_save_calib_result(): "
			    "failed to allocate memory.\n");
			return;
		}

		res_p->lo_res_len = len;
		bcopy(calib_hdr, res_p->lo_res, len);
		break;

	case PHY_CALIBRATE_TX_IQ_CMD:
		if (!(sc->sc_chip_param->calibration_msk & CALIB_TX_IQ_CMD)) {
			return;
		}

		if (NULL == res_p->tx_iq_res) {
			res_p->tx_iq_res = kmem_alloc(len, KM_NOSLEEP);
		}

		if (NULL == res_p->tx_iq_res) {
			cmn_err(CE_WARN, "iwp_save_calib_result(): "
			    "failed to allocate memory.\n");
			return;
		}

		res_p->tx_iq_res_len = len;
		bcopy(calib_hdr, res_p->tx_iq_res, len);
		break;

	case PHY_CALIBRATE_TX_IQ_PERD_CMD:
		if (!(sc->sc_chip_param->calibration_msk &
		    CALIB_TX_IQ_PERD_CMD)) {
			return;
		}

		if (NULL == res_p->tx_iq_perd_res) {
			res_p->tx_iq_perd_res = kmem_alloc(len, KM_NOSLEEP);
		}

		if (NULL == res_p->tx_iq_perd_res) {
			cmn_err(CE_WARN, "iwp_save_calib_result(): "
			    "failed to allocate memory.\n");
			return;
		}

		res_p->tx_iq_perd_res_len = len;
		bcopy(calib_hdr, res_p->tx_iq_perd_res, len);
		break;

	case PHY_CALIBRATE_BASE_BAND_CMD:
		if (!(sc->sc_chip_param->calibration_msk &
		    CALIB_BASE_BAND_CMD)) {
			return;
		}

		if (NULL == res_p->base_band_res) {
			res_p->base_band_res = kmem_alloc(len, KM_NOSLEEP);
		}

		if (NULL == res_p->base_band_res) {
			cmn_err(CE_WARN, "iwp_save_calib_result(): "
			    "failed to allocate memory.\n");
			return;
		}

		res_p->base_band_res_len = len;
		bcopy(calib_hdr, res_p->base_band_res, len);
		break;

	case PHY_CALIBRATE_DC_CMD:
		if (!(sc->sc_chip_param->calibration_msk & CALIB_DC_CMD)) {
			return;
		}

		if (NULL == res_p->dc_res) {
			res_p->dc_res = kmem_alloc(len, KM_NOSLEEP);
		}

		if (NULL == res_p->dc_res) {
			cmn_err(CE_WARN, "iwp_save_calib_result(): "
			    "failed to allocate memory.\n");
			return;
		}

		res_p->dc_res_len = len;
		bcopy(calib_hdr, res_p->dc_res, len);
		break;

	default:
		cmn_err(CE_WARN, "iwp_save_calib_result(): "
		    "incorrect calibration type(%d).\n",
		    calib_hdr->op_code);
		break;
	}

}

void
iwp_send_calib_result_cmd(iwp_sc_t *sc, void *res, uint32_t res_len)
{
	iwp_tx_ring_t *ring = &sc->sc_txq[IWP_CMD_QUEUE_NUM];
	iwp_tx_desc_t *desc;
	iwp_tx_data_t *data;
	iwp_cmd_t *cmd;
	int len;

	data = &ring->data[ring->cur];
	desc = data->desc;
	cmd = (iwp_cmd_t *)data->dma_data.mem_va;

	cmd->hdr.type = REPLY_PHY_CALIBRATION_CMD;
	cmd->hdr.flags = 0;

	cmd->hdr.qid = (ring->qid | HUGE_UCODE_CMD);

	cmd->hdr.idx = ring->cur;

	bcopy(res, cmd->data, res_len);
	len = sizeof (struct iwp_cmd_header) + res_len;

	(void) memset(desc, 0, sizeof (*desc));
	desc->val0 = 1 << 24;
	desc->pa[0].tb1_addr =
	    (uint32_t)(data->dma_data.cookie.dmac_address & 0xffffffff);
	desc->pa[0].val1 = (len << 4) & 0xfff0;

	/*
	 * maybe for cmd, filling the byte cnt table is not necessary.
	 * anyway, we fill it here.
	 */
	sc->sc_shared->queues_byte_cnt_tbls[ring->qid]
	    .tfd_offset[ring->cur].val = 8;
	if (ring->cur < IWP_MAX_WIN_SIZE) {
		sc->sc_shared->queues_byte_cnt_tbls[ring->qid].
		    tfd_offset[IWP_QUEUE_SIZE + ring->cur].val = 8;
	}

	/*
	 * kick cmd ring
	 */
	ring->cur = (ring->cur + 1) % ring->count;
	IWP_WRITE(sc, HBUS_TARG_WRPTR, ring->qid << 8 | ring->cur);
}

void
iwp_send_calib_result(iwp_sc_t *sc)
{
	struct iwp_calib_results *res_p = &sc->sc_calib_results;

	/*
	 * send the result of dc calibration to uCode.
	 */
	if (res_p->dc_res != NULL) {
		iwp_send_calib_result_cmd(sc, res_p->dc_res, res_p->dc_res_len);
		DELAY(1000);
	}


	if (res_p->lo_res != NULL) {
		iwp_send_calib_result_cmd(sc, res_p->lo_res, res_p->lo_res_len);
		DELAY(1000);
	}

	/*
	 * send the result of TX IQ calibration to uCode.
	 */
	if (res_p->tx_iq_res != NULL) {
		iwp_send_calib_result_cmd(sc, res_p->tx_iq_res,
		    res_p->tx_iq_res_len);
		DELAY(1000);
	}

	/*
	 * send the result of TX IQ perd calibration to uCode.
	 */
	if (res_p->tx_iq_perd_res != NULL) {
		iwp_send_calib_result_cmd(sc, res_p->tx_iq_perd_res,
		    res_p->tx_iq_perd_res_len);
		DELAY(1000);
	}


	/*
	 * send the result of Base Band calibration to uCode.
	 */
	if (res_p->base_band_res != NULL) {
		iwp_send_calib_result_cmd(sc, res_p->base_band_res,
		    res_p->base_band_res_len);
		DELAY(1000);
	}

	/*
	 * send the result of temp offset calibration to uCode.
	 */
	if (res_p->temp_offset_res != NULL) {
		iwp_send_calib_result_cmd(sc, res_p->temp_offset_res,
		    res_p->temp_offset_res_len);
		DELAY(1000);
	}
}

void
iwp_release_calib_buffer(iwp_sc_t *sc)
{
	if (sc->sc_calib_results.lo_res != NULL) {
		kmem_free(sc->sc_calib_results.lo_res,
		    sc->sc_calib_results.lo_res_len);
		sc->sc_calib_results.lo_res = NULL;
	}

	if (sc->sc_calib_results.tx_iq_res != NULL) {
		kmem_free(sc->sc_calib_results.tx_iq_res,
		    sc->sc_calib_results.tx_iq_res_len);
		sc->sc_calib_results.tx_iq_res = NULL;
	}

	if (sc->sc_calib_results.tx_iq_perd_res != NULL) {
		kmem_free(sc->sc_calib_results.tx_iq_perd_res,
		    sc->sc_calib_results.tx_iq_perd_res_len);
		sc->sc_calib_results.tx_iq_perd_res = NULL;
	}

	if (sc->sc_calib_results.base_band_res != NULL) {
		kmem_free(sc->sc_calib_results.base_band_res,
		    sc->sc_calib_results.base_band_res_len);
		sc->sc_calib_results.base_band_res = NULL;
	}

	if (sc->sc_calib_results.dc_res != NULL) {
		kmem_free(sc->sc_calib_results.dc_res,
		    sc->sc_calib_results.dc_res_len);
		sc->sc_calib_results.dc_res = NULL;
	}

	if (sc->sc_calib_results.temp_offset_res != NULL) {
		kmem_free(sc->sc_calib_results.temp_offset_res,
		    sc->sc_calib_results.temp_offset_res_len);
		sc->sc_calib_results.temp_offset_res = NULL;
	}

}

int
iwp_set_calib_temp_offset_cmd(iwp_sc_t *sc)
{
	iwp_calibration_temp_offset_cmd_t c_cmd;
	struct iwp_calib_results *res_p = &sc->sc_calib_results;

	c_cmd.opcode = PHY_CALIBRATE_TEMP_OFFSET_CMD;
	c_cmd.first_group = 0;
	c_cmd.num_group = 1;
	c_cmd.all_data_valid = 1;
	c_cmd.radio_sensor_offset = LE_16(sc->sc_eep_calib->temp_calib_volt);
	c_cmd.reserved = 0;

	if (0 == c_cmd.radio_sensor_offset) {
		c_cmd.radio_sensor_offset = DEFAULT_RADIO_SENSOR_OFFSET;
	}

	if (NULL == res_p->temp_offset_res) {
		res_p->temp_offset_res = kmem_alloc(sizeof (c_cmd), KM_NOSLEEP);
	}

	if (NULL == res_p->temp_offset_res) {
		cmn_err(CE_WARN, "iwp_set_calib_temp_offset_cmd(): "
		    "failed to allocate memory.\n");
		return (IWP_FAIL);
	}

	res_p->temp_offset_res_len = sizeof (c_cmd);
	bcopy((uint8_t *)&c_cmd, res_p->temp_offset_res, sizeof (c_cmd));

	return (IWP_SUCCESS);
}

int
iwp_set_rt_calib_config(iwp_sc_t *sc)
{
	struct iwp_calib_cfg_cmd rt_calib_cfg;
	int rv;

	(void) memset(&rt_calib_cfg, 0, sizeof (rt_calib_cfg));
	rt_calib_cfg.ucd_calib_cfg.once.is_enable = IWP_CALIB_INIT_CFG_ALL;
	rt_calib_cfg.ucd_calib_cfg.once.start =
	    LE_32(sc->sc_chip_param->rt_calibration_cfg);

	rv = iwp_cmd(sc, CALIBRATION_CFG_CMD, &rt_calib_cfg,
	    sizeof (rt_calib_cfg), 1);
	if (rv != IWP_SUCCESS) {
		cmn_err(CE_WARN, "iwp_set_rt_calib_config(): "
		    "failed to set runtime calibration configuration\n");
		return (rv);
	}

	return (IWP_SUCCESS);
}
