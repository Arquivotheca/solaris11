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
extern int	iwp_alloc_dma_mem(iwp_sc_t *, size_t,
    ddi_dma_attr_t *, ddi_device_acc_attr_t *,
    uint_t, iwp_dma_t *);
extern void	iwp_free_dma_mem(iwp_dma_t *);
extern void	iwp_mac_access_enter(iwp_sc_t *);
extern void	iwp_mac_access_exit(iwp_sc_t *);

extern ddi_dma_attr_t fw_dma_attr;
extern ddi_device_acc_attr_t iwp_dma_accattr;

/*
 * copy ucode into dma buffers
 */
int
iwp_alloc_fw_dma(iwp_sc_t *sc)
{
	int err = DDI_FAILURE;
	iwp_dma_t *dma_p;
	char *t;

	/*
	 * firmware image layout:
	 * |HDR|<-TEXT->|<-DATA->|<-INIT_TEXT->|<-INIT_DATA->|<-BOOT->|
	 */

	/*
	 * Check firmware image size.
	 */
	if (LE_32(sc->sc_hdr->init_textsz) > RTC_INST_SIZE) {
		cmn_err(CE_WARN, "iwp_alloc_fw_dma(): "
		    "firmware init text size 0x%x is too large\n",
		    LE_32(sc->sc_hdr->init_textsz));

		goto fail;
	}

	if (LE_32(sc->sc_hdr->init_datasz) > RTC_DATA_SIZE) {
		cmn_err(CE_WARN, "iwp_alloc_fw_dma(): "
		    "firmware init data size 0x%x is too large\n",
		    LE_32(sc->sc_hdr->init_datasz));

		goto fail;
	}

	if (LE_32(sc->sc_hdr->textsz) > RTC_INST_SIZE) {
		cmn_err(CE_WARN, "iwp_alloc_fw_dma(): "
		    "firmware text size 0x%x is too large\n",
		    LE_32(sc->sc_hdr->textsz));

		goto fail;
	}

	if (LE_32(sc->sc_hdr->datasz) > RTC_DATA_SIZE) {
		cmn_err(CE_WARN, "iwp_alloc_fw_dma(): "
		    "firmware data size 0x%x is too large\n",
		    LE_32(sc->sc_hdr->datasz));

		goto fail;
	}

	/*
	 * copy text of runtime ucode
	 */
	t = (char *)(sc->sc_hdr + 1);
	err = iwp_alloc_dma_mem(sc, LE_32(sc->sc_hdr->textsz),
	    &fw_dma_attr, &iwp_dma_accattr,
	    DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
	    &sc->sc_dma_fw_text);
	if (err != DDI_SUCCESS) {
		cmn_err(CE_WARN, "iwp_alloc_fw_dma(): "
		    "failed to allocate text dma memory.\n");
		goto fail;
	}

	dma_p = &sc->sc_dma_fw_text;

	IWP_DBG((IWP_DEBUG_DMA, "iwp_alloc_fw_dma(): "
	    "text[ncookies:%d addr:%lx size:%lx]\n",
	    dma_p->ncookies, dma_p->cookie.dmac_address,
	    dma_p->cookie.dmac_size));

	bcopy(t, dma_p->mem_va, LE_32(sc->sc_hdr->textsz));

	/*
	 * copy data and bak-data of runtime ucode
	 */
	t += LE_32(sc->sc_hdr->textsz);
	err = iwp_alloc_dma_mem(sc, LE_32(sc->sc_hdr->datasz),
	    &fw_dma_attr, &iwp_dma_accattr,
	    DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
	    &sc->sc_dma_fw_data);
	if (err != DDI_SUCCESS) {
		cmn_err(CE_WARN, "iwp_alloc_fw_dma(): "
		    "failed to allocate data dma memory\n");
		goto fail;
	}

	dma_p = &sc->sc_dma_fw_data;

	IWP_DBG((IWP_DEBUG_DMA, "iwp_alloc_fw_dma(): "
	    "data[ncookies:%d addr:%lx size:%lx]\n",
	    dma_p->ncookies, dma_p->cookie.dmac_address,
	    dma_p->cookie.dmac_size));

	bcopy(t, dma_p->mem_va, LE_32(sc->sc_hdr->datasz));

	err = iwp_alloc_dma_mem(sc, LE_32(sc->sc_hdr->datasz),
	    &fw_dma_attr, &iwp_dma_accattr,
	    DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
	    &sc->sc_dma_fw_data_bak);
	if (err != DDI_SUCCESS) {
		cmn_err(CE_WARN, "iwp_alloc_fw_dma(): "
		    "failed to allocate data bakup dma memory\n");
		goto fail;
	}
	dma_p = &sc->sc_dma_fw_data_bak;

	IWP_DBG((IWP_DEBUG_DMA, "iwp_alloc_fw_dma(): "
	    "data_bak[ncookies:%d addr:%lx "
	    "size:%lx]\n",
	    dma_p->ncookies, dma_p->cookie.dmac_address,
	    dma_p->cookie.dmac_size));

	bcopy(t, dma_p->mem_va, LE_32(sc->sc_hdr->datasz));

	/*
	 * copy text of init ucode
	 */
	t += LE_32(sc->sc_hdr->datasz);
	err = iwp_alloc_dma_mem(sc, LE_32(sc->sc_hdr->init_textsz),
	    &fw_dma_attr, &iwp_dma_accattr,
	    DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
	    &sc->sc_dma_fw_init_text);
	if (err != DDI_SUCCESS) {
		cmn_err(CE_WARN, "iwp_alloc_fw_dma(): "
		    "failed to allocate init text dma memory\n");
		goto fail;
	}

	dma_p = &sc->sc_dma_fw_init_text;

	IWP_DBG((IWP_DEBUG_DMA, "iwp_alloc_fw_dma(): "
	    "init_text[ncookies:%d addr:%lx "
	    "size:%lx]\n",
	    dma_p->ncookies, dma_p->cookie.dmac_address,
	    dma_p->cookie.dmac_size));

	bcopy(t, dma_p->mem_va, LE_32(sc->sc_hdr->init_textsz));

	/*
	 * copy data of init ucode
	 */
	t += LE_32(sc->sc_hdr->init_textsz);
	err = iwp_alloc_dma_mem(sc, LE_32(sc->sc_hdr->init_datasz),
	    &fw_dma_attr, &iwp_dma_accattr,
	    DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
	    &sc->sc_dma_fw_init_data);
	if (err != DDI_SUCCESS) {
		cmn_err(CE_WARN, "iwp_alloc_fw_dma(): "
		    "failed to allocate init data dma memory\n");
		goto fail;
	}

	dma_p = &sc->sc_dma_fw_init_data;

	IWP_DBG((IWP_DEBUG_DMA, "iwp_alloc_fw_dma(): "
	    "init_data[ncookies:%d addr:%lx "
	    "size:%lx]\n",
	    dma_p->ncookies, dma_p->cookie.dmac_address,
	    dma_p->cookie.dmac_size));

	bcopy(t, dma_p->mem_va, LE_32(sc->sc_hdr->init_datasz));

	sc->sc_boot = t + LE_32(sc->sc_hdr->init_datasz);

	return (err);
fail:
	sc->sc_chip_param->free_fw_dma(sc);
	return (err);
}


void
iwp_free_fw_dma(iwp_sc_t *sc)
{
	iwp_free_dma_mem(&sc->sc_dma_fw_text);
	iwp_free_dma_mem(&sc->sc_dma_fw_data);
	iwp_free_dma_mem(&sc->sc_dma_fw_data_bak);
	iwp_free_dma_mem(&sc->sc_dma_fw_init_text);
	iwp_free_dma_mem(&sc->sc_dma_fw_init_data);
}


/*
 * loade a section of ucode into NIC
 */
static int
iwp_put_seg_fw(iwp_sc_t *sc, uint32_t addr_s, uint32_t addr_d, uint32_t len)
{

	iwp_mac_access_enter(sc);

	IWP_WRITE(sc, IWP_FH_TCSR_CHNL_TX_CONFIG_REG(IWP_FH_SRVC_CHNL),
	    IWP_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_PAUSE);

	IWP_WRITE(sc, IWP_FH_SRVC_CHNL_SRAM_ADDR_REG(IWP_FH_SRVC_CHNL), addr_d);

	IWP_WRITE(sc, IWP_FH_TFDIB_CTRL0_REG(IWP_FH_SRVC_CHNL),
	    (addr_s & FH_MEM_TFDIB_DRAM_ADDR_LSB_MASK));

	IWP_WRITE(sc, IWP_FH_TFDIB_CTRL1_REG(IWP_FH_SRVC_CHNL), len);

	IWP_WRITE(sc, IWP_FH_TCSR_CHNL_TX_BUF_STS_REG(IWP_FH_SRVC_CHNL),
	    (1 << IWP_FH_TCSR_CHNL_TX_BUF_STS_REG_POS_TB_NUM) |
	    (1 << IWP_FH_TCSR_CHNL_TX_BUF_STS_REG_POS_TB_IDX) |
	    IWP_FH_TCSR_CHNL_TX_BUF_STS_REG_VAL_TFDB_VALID);

	IWP_WRITE(sc, IWP_FH_TCSR_CHNL_TX_CONFIG_REG(IWP_FH_SRVC_CHNL),
	    IWP_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_ENABLE |
	    IWP_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_DISABLE_VAL |
	    IWP_FH_TCSR_TX_CONFIG_REG_VAL_CIRQ_HOST_ENDTFD);

	iwp_mac_access_exit(sc);

	return (IWP_SUCCESS);
}


/*
 * steps of loading ucode:
 * load init ucode=>init alive=>calibrate=>
 * receive calibration result=>reinitialize NIC=>
 * load runtime ucode=>runtime alive=>
 * send calibration result=>running.
 */
int
iwp_load_init_firmware(iwp_sc_t *sc)
{
	int err = IWP_FAIL;
	clock_t clk;

	atomic_and_32(&sc->sc_flags, ~IWP_F_PUT_SEG);

	/*
	 * load init_text section of uCode to hardware
	 */
	err = iwp_put_seg_fw(sc, sc->sc_dma_fw_init_text.cookie.dmac_address,
	    RTC_INST_LOWER_BOUND, sc->sc_dma_fw_init_text.cookie.dmac_size);
	if (err != IWP_SUCCESS) {
		cmn_err(CE_WARN, "iwp_load_init_firmware(): "
		    "failed to write init uCode.\n");
		return (err);
	}

	clk = ddi_get_lbolt() + drv_usectohz(1000000);

	/* wait loading init_text until completed or timeout */
	while (!(sc->sc_flags & IWP_F_PUT_SEG)) {
		if (cv_timedwait(&sc->sc_put_seg_cv, &sc->sc_glock, clk) < 0) {
			break;
		}
	}

	if (!(sc->sc_flags & IWP_F_PUT_SEG)) {
		cmn_err(CE_WARN, "iwp_load_init_firmware(): "
		    "timeout waiting for init uCode load.\n");
		return (IWP_FAIL);
	}

	atomic_and_32(&sc->sc_flags, ~IWP_F_PUT_SEG);

	/*
	 * load init_data section of uCode to hardware
	 */
	err = iwp_put_seg_fw(sc, sc->sc_dma_fw_init_data.cookie.dmac_address,
	    RTC_DATA_LOWER_BOUND, sc->sc_dma_fw_init_data.cookie.dmac_size);
	if (err != IWP_SUCCESS) {
		cmn_err(CE_WARN, "iwp_load_init_firmware(): "
		    "failed to write init_data uCode.\n");
		return (err);
	}

	clk = ddi_get_lbolt() + drv_usectohz(1000000);

	/*
	 * wait loading init_data until completed or timeout
	 */
	while (!(sc->sc_flags & IWP_F_PUT_SEG)) {
		if (cv_timedwait(&sc->sc_put_seg_cv, &sc->sc_glock, clk) < 0) {
			break;
		}
	}

	if (!(sc->sc_flags & IWP_F_PUT_SEG)) {
		cmn_err(CE_WARN, "iwp_load_init_firmware(): "
		    "timeout waiting for init_data uCode load.\n");
		return (IWP_FAIL);
	}

	atomic_and_32(&sc->sc_flags, ~IWP_F_PUT_SEG);

	return (err);
}


int
iwp_load_run_firmware(iwp_sc_t *sc)
{
	int err = IWP_FAIL;
	clock_t clk;

	atomic_and_32(&sc->sc_flags, ~IWP_F_PUT_SEG);

	/*
	 * load init_text section of uCode to hardware
	 */
	err = iwp_put_seg_fw(sc, sc->sc_dma_fw_text.cookie.dmac_address,
	    RTC_INST_LOWER_BOUND, sc->sc_dma_fw_text.cookie.dmac_size);
	if (err != IWP_SUCCESS) {
		cmn_err(CE_WARN, "iwp_load_run_firmware(): "
		    "failed to write run uCode.\n");
		return (err);
	}

	clk = ddi_get_lbolt() + drv_usectohz(1000000);

	/* wait loading run_text until completed or timeout */
	while (!(sc->sc_flags & IWP_F_PUT_SEG)) {
		if (cv_timedwait(&sc->sc_put_seg_cv, &sc->sc_glock, clk) < 0) {
			break;
		}
	}

	if (!(sc->sc_flags & IWP_F_PUT_SEG)) {
		cmn_err(CE_WARN, "iwp_load_run_firmware(): "
		    "timeout waiting for run uCode load.\n");
		return (IWP_FAIL);
	}

	atomic_and_32(&sc->sc_flags, ~IWP_F_PUT_SEG);

	/*
	 * load run_data section of uCode to hardware
	 */
	err = iwp_put_seg_fw(sc, sc->sc_dma_fw_data_bak.cookie.dmac_address,
	    RTC_DATA_LOWER_BOUND, sc->sc_dma_fw_data.cookie.dmac_size);
	if (err != IWP_SUCCESS) {
		cmn_err(CE_WARN, "iwp_load_run_firmware(): "
		    "failed to write run_data uCode.\n");
		return (err);
	}

	clk = ddi_get_lbolt() + drv_usectohz(1000000);

	/*
	 * wait loading run_data until completed or timeout
	 */
	while (!(sc->sc_flags & IWP_F_PUT_SEG)) {
		if (cv_timedwait(&sc->sc_put_seg_cv, &sc->sc_glock, clk) < 0) {
			break;
		}
	}

	if (!(sc->sc_flags & IWP_F_PUT_SEG)) {
		cmn_err(CE_WARN, "iwp_load_run_firmware(): "
		    "timeout waiting for run_data uCode load.\n");
		return (IWP_FAIL);
	}

	atomic_and_32(&sc->sc_flags, ~IWP_F_PUT_SEG);

	return (err);
}

int
iwp_alloc_fw_dma_tlv(iwp_sc_t *sc)
{
	iwp_firmware_hdr_tlv_t *hdr;
	iwp_fw_sub_hdr_tlv_t *sub_hdr;
	int want_bit = 1;
	int err, complete = 0;
	uint8_t *data;
	iwp_dma_t *dma_p;

	hdr = (iwp_firmware_hdr_tlv_t *)sc->sc_hdr;

	if (hdr->magic != IWP_FW_TLV_MAGIC) {
		cmn_err(CE_WARN, "iwp_alloc_fw_dma_tlv(): "
		    "invalid firmware TLV magic\n");
		return (IWP_FAIL);
	}

	while (want_bit && !(hdr->valid_bits & want_bit)) {
		want_bit--;
	}

	data = (uint8_t *)(hdr + 1);

	while (complete < 4) {
		sub_hdr = (iwp_fw_sub_hdr_tlv_t *)data;

		data += sizeof (*sub_hdr);

		if ((sub_hdr->alt != 0) && (sub_hdr->alt != want_bit)) {
			data += roundup(sub_hdr->len, 4);
			continue;
		}



		switch (sub_hdr->type) {
		case IWP_FW_TLV_INST:
			if (LE_32(sub_hdr->len) > RTC_INST_SIZE) {
				cmn_err(CE_WARN, "iwp_alloc_fw_dma_tlv(): "
				    "firmware text size 0x%x is too large\n",
				    LE_32(sub_hdr->len));

				goto fail;
			}

			err = iwp_alloc_dma_mem(sc, LE_32(sub_hdr->len),
			    &fw_dma_attr, &iwp_dma_accattr,
			    DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
			    &sc->sc_dma_fw_text);
			if (err != DDI_SUCCESS) {
				cmn_err(CE_WARN, "iwp_alloc_fw_dma_tlv(): "
				    "failed to allocate text dma memory.\n");
				goto fail;
			}

			dma_p = &sc->sc_dma_fw_text;

			IWP_DBG((IWP_DEBUG_DMA, "iwp_alloc_fw_dma_tlv(): "
			    "text[ncookies:%d addr:%lx size:%lx]\n",
			    dma_p->ncookies, dma_p->cookie.dmac_address,
			    dma_p->cookie.dmac_size));

			bcopy(data, dma_p->mem_va, LE_32(sub_hdr->len));
			data += roundup(sub_hdr->len, 4);

			complete++;
		break;

		case IWP_FW_TLV_DATA:
			if (LE_32(sub_hdr->len) > RTC_DATA_SIZE) {
				cmn_err(CE_WARN, "iwp_alloc_fw_dma_tlv(): "
				    "firmware data size 0x%x is too large\n",
				    LE_32(sub_hdr->len));

				goto fail;
			}

			err = iwp_alloc_dma_mem(sc, LE_32(sub_hdr->len),
			    &fw_dma_attr, &iwp_dma_accattr,
			    DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
			    &sc->sc_dma_fw_data);
			if (err != DDI_SUCCESS) {
				cmn_err(CE_WARN, "iwp_alloc_fw_dma_tlv(): "
				    "failed to allocate data dma memory\n");
				goto fail;
			}

			dma_p = &sc->sc_dma_fw_data;

			IWP_DBG((IWP_DEBUG_DMA, "iwp_alloc_fw_dma_tlv(): "
			    "data[ncookies:%d addr:%lx size:%lx]\n",
			    dma_p->ncookies, dma_p->cookie.dmac_address,
			    dma_p->cookie.dmac_size));

			bcopy(data, dma_p->mem_va, LE_32(sub_hdr->len));

			err = iwp_alloc_dma_mem(sc, LE_32(sub_hdr->len),
			    &fw_dma_attr, &iwp_dma_accattr,
			    DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
			    &sc->sc_dma_fw_data_bak);
			if (err != DDI_SUCCESS) {
				cmn_err(CE_WARN, "iwp_alloc_fw_dma_tlv(): "
				    "failed to allocate data "
				    "bakup dma memory\n");
				goto fail;
			}
			dma_p = &sc->sc_dma_fw_data_bak;

			IWP_DBG((IWP_DEBUG_DMA, "iwp_alloc_fw_dma_tlv(): "
			    "data_bak[ncookies:%d addr:%lx "
			    "size:%lx]\n",
			    dma_p->ncookies, dma_p->cookie.dmac_address,
			    dma_p->cookie.dmac_size));

			bcopy(data, dma_p->mem_va, LE_32(sub_hdr->len));
			data += roundup(sub_hdr->len, 4);

			complete++;
		break;

		case IWP_FW_TLV_INIT:
			if (LE_32(sub_hdr->len) > RTC_INST_SIZE) {
				cmn_err(CE_WARN, "iwp_alloc_fw_dma_tlv(): "
				    "firmware init text 0x%x is too large\n",
				    LE_32(sub_hdr->len));

				goto fail;
			}

			err = iwp_alloc_dma_mem(sc, LE_32(sub_hdr->len),
			    &fw_dma_attr, &iwp_dma_accattr,
			    DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
			    &sc->sc_dma_fw_init_text);
			if (err != DDI_SUCCESS) {
				cmn_err(CE_WARN, "iwp_alloc_fw_dma_tlv(): "
				    "failed to allocate init "
				    "text dma memory\n");
				goto fail;
			}

			dma_p = &sc->sc_dma_fw_init_text;

			IWP_DBG((IWP_DEBUG_DMA, "iwp_alloc_fw_dma_tlv(): "
			    "init_text[ncookies:%d addr:%lx "
			    "size:%lx]\n",
			    dma_p->ncookies, dma_p->cookie.dmac_address,
			    dma_p->cookie.dmac_size));

			bcopy(data, dma_p->mem_va, LE_32(sub_hdr->len));
			data += roundup(sub_hdr->len, 4);

			complete++;
			break;

		case IWP_FW_TLV_INIT_DATA:
			if (LE_32(sub_hdr->len) > RTC_DATA_SIZE) {
				cmn_err(CE_WARN, "iwp_alloc_fw_dma_tlv(): "
				    "firmware init data 0x%x is too large\n",
				    LE_32(sub_hdr->len));

				goto fail;
			}

			err = iwp_alloc_dma_mem(sc, LE_32(sub_hdr->len),
			    &fw_dma_attr, &iwp_dma_accattr,
			    DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
			    &sc->sc_dma_fw_init_data);
			if (err != DDI_SUCCESS) {
				cmn_err(CE_WARN, "iwp_alloc_fw_dma_tlv(): "
				    "failed to allocate init "
				    "data dma memory\n");
				goto fail;
			}

			dma_p = &sc->sc_dma_fw_init_data;

			IWP_DBG((IWP_DEBUG_DMA, "iwp_alloc_fw_dma_tlv(): "
			    "init_data[ncookies:%d addr:%lx "
			    "size:%lx]\n",
			    dma_p->ncookies, dma_p->cookie.dmac_address,
			    dma_p->cookie.dmac_size));

			bcopy(data, dma_p->mem_va, LE_32(sub_hdr->len));
			data += roundup(sub_hdr->len, 4);

			complete++;
		break;

		default:
			data += roundup(sub_hdr->len, 4);
			break;
		}
	}

	return (IWP_SUCCESS);

fail:
	sc->sc_chip_param->free_fw_dma(sc);
	return (IWP_FAIL);
}
