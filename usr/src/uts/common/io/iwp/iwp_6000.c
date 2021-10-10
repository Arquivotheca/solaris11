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
#include "iwp_firmware.h"

extern int	iwp_preinit(iwp_sc_t *);
extern void	iwp_overwrite_11n_rateset(iwp_sc_t *);
extern int	iwp_alloc_fw_dma(iwp_sc_t *);
extern int	iwp_alloc_fw_dma_tlv(iwp_sc_t *);
extern void	iwp_free_fw_dma(iwp_sc_t *);

/*
 * ucode will be compiled into driver image
 */
static uint8_t iwp_fw_6000_bin [] = {
#include "fw-iw/fw_6000/iwp_6000.ucode"
};

static uint8_t iwp_fw_6050_bin [] = {
#include "fw-iw/fw_6050/iwp_6050.ucode"
};

static uint8_t iwp_fw_6205_bin [] = {
#include "fw-iw/fw_6205/iwp_6205.ucode"
};

static void
iwp_locate_fw_img_6205(iwp_sc_t *sc)
{
	sc->sc_hdr = (iwp_firmware_hdr_t *)iwp_fw_6205_bin;
}

static void
iwp_locate_fw_img_6x00(iwp_sc_t *sc)
{
	sc->sc_hdr = (iwp_firmware_hdr_t *)iwp_fw_6000_bin;
}

static void
iwp_locate_fw_img_6x50(iwp_sc_t *sc)
{
	sc->sc_hdr = (iwp_firmware_hdr_t *)iwp_fw_6050_bin;
}

static uint32_t
iwp_determine_nvm_type_6000(iwp_sc_t *sc)
{
	uint32_t otp_r;
	uint32_t nvm_t;

	otp_r = IWP_READ(sc, CSR_OTP_GP);
	if (otp_r & CSR_OTP_GP_DEVICE_SELECT) {
		nvm_t = NVM_TYPE_OTP;
	} else {
		nvm_t = NVM_TYPE_EEPROM;
	}

	return (nvm_t);
}

static void
iwp_config_rxon_chain_6000_type1(iwp_sc_t *sc)
{
	ieee80211com_t *ic = &sc->sc_ic;
	ieee80211_node_t *in = ic->ic_bss;

	sc->sc_config.rx_chain = LE_16((RXON_RX_CHAIN_A_MSK |
	    RXON_RX_CHAIN_B_MSK) << RXON_RX_CHAIN_VALID_POS);

	sc->sc_config.rx_chain |= LE_16((RXON_RX_CHAIN_A_MSK |
	    RXON_RX_CHAIN_B_MSK) << RXON_RX_CHAIN_FORCE_SEL_POS);

	sc->sc_config.rx_chain |= LE_16((RXON_RX_CHAIN_A_MSK |
	    RXON_RX_CHAIN_B_MSK) <<
	    RXON_RX_CHAIN_FORCE_MIMO_SEL_POS);

	sc->sc_config.rx_chain |= LE_16(RXON_RX_CHAIN_DRIVER_FORCE_MSK);

	if ((in != NULL) &&
	    (in->in_flags & IEEE80211_NODE_HT)) {
		sc->sc_config.rx_chain |= LE_16(2 <<
		    RXON_RX_CHAIN_CNT_POS);
		sc->sc_config.rx_chain |= LE_16(2 <<
		    RXON_RX_CHAIN_MIMO_CNT_POS);

		sc->sc_config.rx_chain |= LE_16(1 <<
		    RXON_RX_CHAIN_MIMO_FORCE_POS);
	}

	IWP_DBG((IWP_DEBUG_RXON, "iwp_config_rxon_chain_6000_type1(): "
	    "rxon->rx_chain = %x\n", sc->sc_config.rx_chain));
}

static void
iwp_config_rxon_chain_6000_type2(iwp_sc_t *sc)
{
	ieee80211com_t *ic = &sc->sc_ic;
	ieee80211_node_t *in = ic->ic_bss;

	sc->sc_config.rx_chain = LE_16((RXON_RX_CHAIN_B_MSK |
	    RXON_RX_CHAIN_C_MSK) << RXON_RX_CHAIN_VALID_POS);

	sc->sc_config.rx_chain |= LE_16((RXON_RX_CHAIN_B_MSK |
	    RXON_RX_CHAIN_C_MSK) << RXON_RX_CHAIN_FORCE_SEL_POS);

	sc->sc_config.rx_chain |= LE_16((RXON_RX_CHAIN_B_MSK |
	    RXON_RX_CHAIN_C_MSK) <<
	    RXON_RX_CHAIN_FORCE_MIMO_SEL_POS);

	sc->sc_config.rx_chain |= LE_16(RXON_RX_CHAIN_DRIVER_FORCE_MSK);

	if ((in != NULL) &&
	    (in->in_flags & IEEE80211_NODE_HT)) {
		sc->sc_config.rx_chain |= LE_16(2 <<
		    RXON_RX_CHAIN_CNT_POS);
		sc->sc_config.rx_chain |= LE_16(2 <<
		    RXON_RX_CHAIN_MIMO_CNT_POS);

		sc->sc_config.rx_chain |= LE_16(1 <<
		    RXON_RX_CHAIN_MIMO_FORCE_POS);
	}

	IWP_DBG((IWP_DEBUG_RXON, "iwp_config_rxon_chain_6000_type2(): "
	    "rxon->rx_chain = %x\n", sc->sc_config.rx_chain));
}

static void
iwp_config_rxon_chain_6000_type3(iwp_sc_t *sc)
{
	ieee80211com_t *ic = &sc->sc_ic;
	ieee80211_node_t *in = ic->ic_bss;

	sc->sc_config.rx_chain = LE_16((RXON_RX_CHAIN_A_MSK |
	    RXON_RX_CHAIN_B_MSK | RXON_RX_CHAIN_C_MSK) <<
	    RXON_RX_CHAIN_VALID_POS);

	sc->sc_config.rx_chain |= LE_16((RXON_RX_CHAIN_A_MSK |
	    RXON_RX_CHAIN_B_MSK | RXON_RX_CHAIN_C_MSK) <<
	    RXON_RX_CHAIN_FORCE_SEL_POS);

	sc->sc_config.rx_chain |= LE_16((RXON_RX_CHAIN_A_MSK |
	    RXON_RX_CHAIN_B_MSK | RXON_RX_CHAIN_C_MSK) <<
	    RXON_RX_CHAIN_FORCE_MIMO_SEL_POS);

	sc->sc_config.rx_chain |= LE_16(RXON_RX_CHAIN_DRIVER_FORCE_MSK);

	if ((in != NULL) &&
	    (in->in_flags & IEEE80211_NODE_HT)) {
		sc->sc_config.rx_chain |= LE_16(3 <<
		    RXON_RX_CHAIN_CNT_POS);
		sc->sc_config.rx_chain |= LE_16(3 <<
		    RXON_RX_CHAIN_MIMO_CNT_POS);

		sc->sc_config.rx_chain |= LE_16(1 <<
		    RXON_RX_CHAIN_MIMO_FORCE_POS);
	}

	IWP_DBG((IWP_DEBUG_RXON, "iwp_config_rxon_chain_6000_type3(): "
	    "rxon->rx_chain = %x\n", sc->sc_config.rx_chain));
}

static void
iwp_config_rxon_chain_6000_type4(iwp_sc_t *sc)
{
	sc->sc_config.rx_chain = LE_16((RXON_RX_CHAIN_B_MSK |
	    RXON_RX_CHAIN_C_MSK) << RXON_RX_CHAIN_VALID_POS);

	sc->sc_config.rx_chain |= LE_16((RXON_RX_CHAIN_B_MSK |
	    RXON_RX_CHAIN_C_MSK) << RXON_RX_CHAIN_FORCE_SEL_POS);

	sc->sc_config.rx_chain |= LE_16((RXON_RX_CHAIN_B_MSK |
	    RXON_RX_CHAIN_C_MSK) <<
	    RXON_RX_CHAIN_FORCE_MIMO_SEL_POS);

	sc->sc_config.rx_chain |= LE_16(RXON_RX_CHAIN_DRIVER_FORCE_MSK);

	IWP_DBG((IWP_DEBUG_RXON, "iwp_config_rxon_chain_6000_type4(): "
	    "rxon->rx_chain = %x\n", sc->sc_config.rx_chain));
}

static void
iwp_config_rxon_chain_6000_type5(iwp_sc_t *sc)
{
	sc->sc_config.rx_chain = LE_16((RXON_RX_CHAIN_A_MSK |
	    RXON_RX_CHAIN_B_MSK) << RXON_RX_CHAIN_VALID_POS);

	sc->sc_config.rx_chain |= LE_16((RXON_RX_CHAIN_A_MSK |
	    RXON_RX_CHAIN_B_MSK) << RXON_RX_CHAIN_FORCE_SEL_POS);

	sc->sc_config.rx_chain |= LE_16((RXON_RX_CHAIN_A_MSK |
	    RXON_RX_CHAIN_B_MSK) <<
	    RXON_RX_CHAIN_FORCE_MIMO_SEL_POS);

	sc->sc_config.rx_chain |= LE_16(RXON_RX_CHAIN_DRIVER_FORCE_MSK);

	IWP_DBG((IWP_DEBUG_RXON, "iwp_config_rxon_chain_6000_type5(): "
	    "rxon->rx_chain = %x\n", sc->sc_config.rx_chain));
}


static struct iwp_ampdu_param iwp_ampdu_param_6000 = {
	.factor = HT_RX_AMPDU_FACTOR,
	.density = HT_MPDU_DENSITY,
};

static struct iwp_ht_conf iwp_ht_conf_6000_type1 = {
	.ht_support = 1,
	.cap = HT_CAP_GRN_FLD | HT_CAP_SGI_20 |
	    HT_CAP_MAX_AMSDU | HT_CAP_MIMO_PS,
	.ampdu_p = &iwp_ampdu_param_6000,
	.tx_support_mcs = {0xff, 0xff},
	.rx_support_mcs = {0xff, 0xff},
	.valid_chains = 3,
	.tx_stream_count = 2,
	.rx_stream_count = 2,
	.ht_protection = HT_PROT_CHAN_NON_HT,
};

static struct iwp_ht_conf iwp_ht_conf_6000_type2 = {
	.ht_support = 1,
	.cap = HT_CAP_GRN_FLD | HT_CAP_SGI_20 |
	    HT_CAP_MAX_AMSDU | HT_CAP_MIMO_PS,
	.ampdu_p = &iwp_ampdu_param_6000,
	.tx_support_mcs = {0xff, 0xff},
	.rx_support_mcs = {0xff, 0xff},
	.valid_chains = 2,
	.tx_stream_count = 2,
	.rx_stream_count = 2,
	.ht_protection = HT_PROT_CHAN_NON_HT,
};

static struct iwp_ht_conf iwp_ht_conf_6000_type3 = {
	.ht_support = 0,
	.cap = 0,
	.ampdu_p = NULL,
	.tx_support_mcs = {0xff, 0xff},
	.rx_support_mcs = {0xff, 0xff},
	.valid_chains = 2,
	.tx_stream_count = 2,
	.rx_stream_count = 2,
	.ht_protection = 0,
};

struct iwp_chip_param iwp_chip_param_6205_type1 = {
	.phy_mode = PHY_MODE_G | PHY_MODE_A | PHY_MODE_N,
	.tx_ant = ANT_A | ANT_B,
	.rx_ant = ANT_A | ANT_B,
	.pa_type = PA_TYPE_SYSTEM,
	.shadowram_support = 1,
	.max_ll_items = OTP_MAX_LL_ITEMS_6x00,
	.eeprom_ver = EEPROM_6000G2_EEPROM_VERSION,
	.eeprom_calib_ver = EEPROM_6000G2_TX_POWER_VERSION,
	.calibration_msk = CALIB_LO_CMD | CALIB_TX_IQ_CMD | CALIB_DC_CMD |
	    CALIB_BASE_BAND_CMD | CALIB_CRYSTAL_FRQ_CMD | CALIB_TEMP_OFFSET_CMD,
	.rt_calibration_cfg = RT_CALIB_CFG_DC,
	.ht_conf = &iwp_ht_conf_6000_type2,
	.determine_nvm_type = iwp_determine_nvm_type_6000,
	.hw_init = iwp_preinit,
	.config_rxon_chain = iwp_config_rxon_chain_6000_type1,
	.overwrite_11n_rateset = iwp_overwrite_11n_rateset,
	.locate_firmware_img = iwp_locate_fw_img_6205,
	.alloc_fw_dma = iwp_alloc_fw_dma_tlv,
	.free_fw_dma = iwp_free_fw_dma,
};

struct iwp_chip_param iwp_chip_param_6205_type2 = {
	.phy_mode = PHY_MODE_G | PHY_MODE_A,
	.tx_ant = ANT_A | ANT_B,
	.rx_ant = ANT_A | ANT_B,
	.pa_type = PA_TYPE_SYSTEM,
	.shadowram_support = 1,
	.max_ll_items = OTP_MAX_LL_ITEMS_6x00,
	.eeprom_ver = EEPROM_6000G2_EEPROM_VERSION,
	.eeprom_calib_ver = EEPROM_6000G2_TX_POWER_VERSION,
	.calibration_msk = CALIB_LO_CMD | CALIB_TX_IQ_CMD | CALIB_DC_CMD |
	    CALIB_BASE_BAND_CMD | CALIB_CRYSTAL_FRQ_CMD | CALIB_TEMP_OFFSET_CMD,
	.rt_calibration_cfg = RT_CALIB_CFG_DC,
	.ht_conf = &iwp_ht_conf_6000_type3,
	.determine_nvm_type = iwp_determine_nvm_type_6000,
	.hw_init = iwp_preinit,
	.config_rxon_chain = iwp_config_rxon_chain_6000_type5,
	.overwrite_11n_rateset = NULL,
	.locate_firmware_img = iwp_locate_fw_img_6205,
	.alloc_fw_dma = iwp_alloc_fw_dma_tlv,
	.free_fw_dma = iwp_free_fw_dma,
};

struct iwp_chip_param iwp_chip_param_6205_type3 = {
	.phy_mode = PHY_MODE_G,
	.tx_ant = ANT_A | ANT_B,
	.rx_ant = ANT_A | ANT_B,
	.pa_type = PA_TYPE_SYSTEM,
	.shadowram_support = 1,
	.max_ll_items = OTP_MAX_LL_ITEMS_6x00,
	.eeprom_ver = EEPROM_6000G2_EEPROM_VERSION,
	.eeprom_calib_ver = EEPROM_6000G2_TX_POWER_VERSION,
	.calibration_msk = CALIB_LO_CMD | CALIB_TX_IQ_CMD | CALIB_DC_CMD |
	    CALIB_BASE_BAND_CMD | CALIB_CRYSTAL_FRQ_CMD | CALIB_TEMP_OFFSET_CMD,
	.rt_calibration_cfg = RT_CALIB_CFG_DC,
	.ht_conf = &iwp_ht_conf_6000_type3,
	.determine_nvm_type = iwp_determine_nvm_type_6000,
	.hw_init = iwp_preinit,
	.config_rxon_chain = iwp_config_rxon_chain_6000_type5,
	.overwrite_11n_rateset = NULL,
	.locate_firmware_img = iwp_locate_fw_img_6205,
	.alloc_fw_dma = iwp_alloc_fw_dma_tlv,
	.free_fw_dma = iwp_free_fw_dma,
};

struct iwp_chip_param iwp_chip_param_6x00_type2 = {
	.phy_mode = PHY_MODE_G | PHY_MODE_A | PHY_MODE_N,
	.tx_ant = ANT_B | ANT_C,
	.rx_ant = ANT_B | ANT_C,
	.pa_type = PA_TYPE_INTER,
	.shadowram_support = 1,
	.max_ll_items = OTP_MAX_LL_ITEMS_6x00,
	.eeprom_ver = EEPROM_6000_EEPROM_VERSION,
	.eeprom_calib_ver = EEPROM_6000_TX_POWER_VERSION,
	.calibration_msk = CALIB_LO_CMD | CALIB_TX_IQ_CMD |
	    CALIB_TX_IQ_PERD_CMD | CALIB_BASE_BAND_CMD | CALIB_CRYSTAL_FRQ_CMD,
	.rt_calibration_cfg = 0,
	.ht_conf = &iwp_ht_conf_6000_type2,
	.determine_nvm_type = iwp_determine_nvm_type_6000,
	.hw_init = iwp_preinit,
	.config_rxon_chain = iwp_config_rxon_chain_6000_type2,
	.overwrite_11n_rateset = iwp_overwrite_11n_rateset,
	.locate_firmware_img = iwp_locate_fw_img_6x00,
	.alloc_fw_dma = iwp_alloc_fw_dma,
	.free_fw_dma = iwp_free_fw_dma,
};

struct iwp_chip_param iwp_chip_param_6x00_type3 = {
	.phy_mode = PHY_MODE_G | PHY_MODE_A,
	.tx_ant = ANT_B | ANT_C,
	.rx_ant = ANT_B | ANT_C,
	.pa_type = PA_TYPE_INTER,
	.shadowram_support = 1,
	.max_ll_items = OTP_MAX_LL_ITEMS_6x00,
	.eeprom_ver = EEPROM_6000_EEPROM_VERSION,
	.eeprom_calib_ver = EEPROM_6000_TX_POWER_VERSION,
	.calibration_msk = CALIB_LO_CMD | CALIB_TX_IQ_CMD |
	    CALIB_TX_IQ_PERD_CMD | CALIB_BASE_BAND_CMD | CALIB_CRYSTAL_FRQ_CMD,
	.rt_calibration_cfg = 0,
	.ht_conf = &iwp_ht_conf_6000_type3,
	.determine_nvm_type = iwp_determine_nvm_type_6000,
	.hw_init = iwp_preinit,
	.config_rxon_chain = iwp_config_rxon_chain_6000_type4,
	.overwrite_11n_rateset = NULL,
	.locate_firmware_img = iwp_locate_fw_img_6x00,
	.alloc_fw_dma = iwp_alloc_fw_dma,
	.free_fw_dma = iwp_free_fw_dma,
};

struct iwp_chip_param iwp_chip_param_6x00_type4 = {
	.phy_mode = PHY_MODE_G,
	.tx_ant = ANT_B | ANT_C,
	.rx_ant = ANT_B | ANT_C,
	.pa_type = PA_TYPE_INTER,
	.shadowram_support = 1,
	.max_ll_items = OTP_MAX_LL_ITEMS_6x00,
	.eeprom_ver = EEPROM_6000_EEPROM_VERSION,
	.eeprom_calib_ver = EEPROM_6000_TX_POWER_VERSION,
	.calibration_msk = CALIB_LO_CMD | CALIB_TX_IQ_CMD |
	    CALIB_TX_IQ_PERD_CMD | CALIB_BASE_BAND_CMD | CALIB_CRYSTAL_FRQ_CMD,
	.rt_calibration_cfg = 0,
	.ht_conf = &iwp_ht_conf_6000_type3,
	.determine_nvm_type = iwp_determine_nvm_type_6000,
	.hw_init = iwp_preinit,
	.config_rxon_chain = iwp_config_rxon_chain_6000_type4,
	.overwrite_11n_rateset = NULL,
	.locate_firmware_img = iwp_locate_fw_img_6x00,
	.alloc_fw_dma = iwp_alloc_fw_dma,
	.free_fw_dma = iwp_free_fw_dma,
};

struct iwp_chip_param iwp_chip_param_6x00_type5 = {
	.phy_mode = PHY_MODE_G | PHY_MODE_A | PHY_MODE_N,
	.tx_ant = ANT_A | ANT_B | ANT_C,
	.rx_ant = ANT_A | ANT_B | ANT_C,
	.pa_type = PA_TYPE_SYSTEM,
	.shadowram_support = 1,
	.max_ll_items = OTP_MAX_LL_ITEMS_6x00,
	.eeprom_ver = EEPROM_6000_EEPROM_VERSION,
	.eeprom_calib_ver = EEPROM_6000_TX_POWER_VERSION,
	.calibration_msk = CALIB_LO_CMD | CALIB_TX_IQ_CMD |
	    CALIB_TX_IQ_PERD_CMD | CALIB_BASE_BAND_CMD | CALIB_CRYSTAL_FRQ_CMD,
	.rt_calibration_cfg = 0,
	.ht_conf = &iwp_ht_conf_6000_type1,
	.determine_nvm_type = iwp_determine_nvm_type_6000,
	.hw_init = iwp_preinit,
	.config_rxon_chain = iwp_config_rxon_chain_6000_type3,
	.overwrite_11n_rateset = iwp_overwrite_11n_rateset,
	.locate_firmware_img = iwp_locate_fw_img_6x00,
	.alloc_fw_dma = iwp_alloc_fw_dma,
	.free_fw_dma = iwp_free_fw_dma,
};

struct iwp_chip_param iwp_chip_param_6x50_type1 = {
	.phy_mode = PHY_MODE_G | PHY_MODE_A | PHY_MODE_N,
	.tx_ant = ANT_A | ANT_B,
	.rx_ant = ANT_A | ANT_B,
	.pa_type = PA_TYPE_SYSTEM,
	.shadowram_support = 1,
	.max_ll_items = OTP_MAX_LL_ITEMS_6x50,
	.eeprom_ver = EEPROM_6050_EEPROM_VERSION,
	.eeprom_calib_ver = EEPROM_6050_TX_POWER_VERSION,
	.calibration_msk = CALIB_LO_CMD | CALIB_TX_IQ_CMD |
	    CALIB_TX_IQ_PERD_CMD | CALIB_BASE_BAND_CMD | CALIB_CRYSTAL_FRQ_CMD,
	.rt_calibration_cfg = 0,
	.ht_conf = &iwp_ht_conf_6000_type2,
	.determine_nvm_type = iwp_determine_nvm_type_6000,
	.hw_init = iwp_preinit,
	.config_rxon_chain = iwp_config_rxon_chain_6000_type1,
	.overwrite_11n_rateset = iwp_overwrite_11n_rateset,
	.locate_firmware_img = iwp_locate_fw_img_6x50,
	.alloc_fw_dma = iwp_alloc_fw_dma,
	.free_fw_dma = iwp_free_fw_dma,
};

struct iwp_chip_param iwp_chip_param_6x50_type2 = {
	.phy_mode = PHY_MODE_G | PHY_MODE_A,
	.tx_ant = ANT_A | ANT_B,
	.rx_ant = ANT_A | ANT_B,
	.pa_type = PA_TYPE_SYSTEM,
	.shadowram_support = 1,
	.max_ll_items = OTP_MAX_LL_ITEMS_6x50,
	.eeprom_ver = EEPROM_6050_EEPROM_VERSION,
	.eeprom_calib_ver = EEPROM_6050_TX_POWER_VERSION,
	.calibration_msk = CALIB_LO_CMD | CALIB_TX_IQ_CMD |
	    CALIB_TX_IQ_PERD_CMD | CALIB_BASE_BAND_CMD | CALIB_CRYSTAL_FRQ_CMD,
	.rt_calibration_cfg = 0,
	.ht_conf = &iwp_ht_conf_6000_type3,
	.determine_nvm_type = iwp_determine_nvm_type_6000,
	.hw_init = iwp_preinit,
	.config_rxon_chain = iwp_config_rxon_chain_6000_type5,
	.overwrite_11n_rateset = NULL,
	.locate_firmware_img = iwp_locate_fw_img_6x50,
	.alloc_fw_dma = iwp_alloc_fw_dma,
	.free_fw_dma = iwp_free_fw_dma,
};
