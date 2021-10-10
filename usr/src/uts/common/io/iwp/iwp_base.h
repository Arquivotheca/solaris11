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
 *	Damien Bergamini <damien.bergamini@free.fr>
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

#ifndef _IWP_BASE_H
#define	_IWP_BASE_H

#ifdef __cplusplus
extern "C" {
#endif

#define	IWP_DMA_SYNC(area, flag) \
	(void) ddi_dma_sync((area).dma_hdl, (area).offset, \
	(area).alength, (flag))

#define	IWP_CHK_FAST_RECOVER(sc) \
	(sc->sc_ic.ic_state == IEEE80211_S_RUN && \
	sc->sc_ic.ic_opmode == IEEE80211_M_STA)

typedef struct iwp_dma_area {
	ddi_acc_handle_t	acc_hdl; /* handle for memory */
	caddr_t			mem_va; /* CPU VA of memory */
	uint32_t		nslots; /* number of slots */
	uint32_t		size;   /* size per slot */
	size_t			alength; /* allocated size */
					/* >= product of above */
	ddi_dma_handle_t	dma_hdl; /* DMA handle */
	offset_t		offset;  /* relative to handle */
	ddi_dma_cookie_t	cookie; /* associated cookie */
	uint32_t		ncookies;
	uint32_t		token; /* arbitrary identifier */
} iwp_dma_t;

typedef struct iwp_tx_data {
	iwp_dma_t		dma_data;	/* for sending frames */
	iwp_tx_desc_t		*desc;
	uint32_t		paddr_desc;
	iwp_cmd_t		*cmd;
	uint32_t		paddr_cmd;
} iwp_tx_data_t;

typedef struct iwp_tx_ring {
	iwp_dma_t		dma_desc;	/* for descriptor itself */
	iwp_dma_t		dma_cmd;	/* for command to ucode */
	iwp_tx_data_t	*data;
	int			qid;		/* ID of queue */
	int			count;
	int			window;
	int			queued;
	int			cur;
	int			desc_cur;
} iwp_tx_ring_t;

typedef struct iwp_rx_data {
	iwp_dma_t		dma_data;
} iwp_rx_data_t;

typedef struct iwp_rx_ring {
	iwp_dma_t		dma_desc;
	uint32_t 		*desc;
	iwp_rx_data_t	data[RX_QUEUE_SIZE];
	int			cur;
} iwp_rx_ring_t;


struct	iwp_phy_rx {
	uint8_t	flag;
	uint8_t	reserved[3];
	uint8_t	buf[128];
};

struct iwp_beacon_missed {
	uint32_t	consecutive;
	uint32_t	total;
	uint32_t	expected;
	uint32_t	received;
};

#define	PHY_MODE_G	(0x1)
#define	PHY_MODE_A	(0x2)
#define	PHY_MODE_N	(0x4)

#define	ANT_A		(0x1)
#define	ANT_B		(0x2)
#define	ANT_C		(0x4)

#define	PA_TYPE_SYSTEM	(0)
#define	PA_TYPE_MIX	(1)
#define	PA_TYPE_INTER	(2)

typedef struct iwp_softc {
	struct ieee80211com	sc_ic;
	dev_info_t		*sc_dip;
	int			(*sc_newstate)(struct ieee80211com *,
	    enum ieee80211_state, int);
	void			(*sc_recv_action)(ieee80211_node_t *,
				    const uint8_t *, const uint8_t *);
	int			(*sc_send_action)(ieee80211_node_t *,
				    int, int, uint16_t[4]);
	volatile uint32_t	sc_cmd_flag;
	volatile uint32_t	sc_cmd_accum;

	volatile uint32_t	sc_ostate;
	kmutex_t		sc_glock;
	kmutex_t		sc_mt_lock;
	kmutex_t		sc_tx_lock;
	kcondvar_t		sc_mt_cv;
	kcondvar_t		sc_cmd_cv;
	kcondvar_t		sc_put_seg_cv;
	kcondvar_t		sc_ucode_cv;

	kthread_t		*sc_mf_thread;
	volatile uint32_t	sc_mf_thread_switch;

	volatile uint32_t	sc_flags;
	uint32_t		sc_dmabuf_sz;
	uint16_t		sc_clsz;
	uint8_t			sc_rev;
	uint8_t			sc_resv;
	uint16_t		sc_assoc_id;
	uint16_t		sc_reserved0;

	/* shared area */
	iwp_dma_t		sc_dma_sh;
	iwp_shared_t		*sc_shared;
	/* keep warm area */
	iwp_dma_t		sc_dma_kw;
	/* tx scheduler base address */
	uint32_t		sc_scd_base_addr;

	struct iwp_phy_rx	sc_rx_phy_res;

	iwp_tx_ring_t		sc_txq[IWP_NUM_QUEUES];
	iwp_rx_ring_t		sc_rxq;

	/* firmware dma */
	iwp_firmware_hdr_t	*sc_hdr;
	char			*sc_boot;
	iwp_dma_t		sc_dma_fw_text;
	iwp_dma_t		sc_dma_fw_init_text;
	iwp_dma_t		sc_dma_fw_data;
	iwp_dma_t		sc_dma_fw_data_bak;
	iwp_dma_t		sc_dma_fw_init_data;

	ddi_acc_handle_t	sc_cfg_handle;
	caddr_t			sc_cfg_base;
	ddi_acc_handle_t	sc_handle;
	caddr_t			sc_base;
	ddi_intr_handle_t	*sc_intr_htable;
	uint_t			sc_intr_pri;

	iwp_rxon_cmd_t		sc_config;
	iwp_rxon_cmd_t		sc_config_save;

	uint8_t			sc_eep_map[OTP_LOW_IMAGE_SIZE];
	struct	iwp_eep_calibration *sc_eep_calib;
	struct	iwp_calib_results	sc_calib_results;
	uint32_t		sc_scd_base;
	uint32_t		sc_nvm_type;

	struct iwp_alive_resp	sc_card_alive_run;
	struct iwp_init_alive_resp	sc_card_alive_init;
	uint16_t		sc_dev_id;

	uint32_t		sc_tx_timer;
	uint32_t		sc_scan_pending;
	uint8_t			*sc_fw_bin;

	ddi_softint_handle_t    sc_soft_hdl;

	uint32_t		sc_rx_softint_pending;
	uint32_t		sc_need_reschedule;

	clock_t			sc_clk;

	struct iwp_chip_param	*sc_chip_param;

	/* kstats */
	uint32_t		sc_tx_nobuf;
	uint32_t		sc_rx_nobuf;
	uint32_t		sc_tx_err;
	uint32_t		sc_rx_err;
	uint32_t		sc_tx_retries;
} iwp_sc_t;

#define	SC_CMD_FLG_NONE		(0)
#define	SC_CMD_FLG_PENDING	(1)
#define	SC_CMD_FLG_DONE		(2)

#define	IWP_F_ATTACHED		(1 << 0)
#define	IWP_F_CMD_DONE		(1 << 1)
#define	IWP_F_FW_INIT		(1 << 2)
#define	IWP_F_HW_ERR_RECOVER	(1 << 3)
#define	IWP_F_RATE_AUTO_CTL	(1 << 4)
#define	IWP_F_RUNNING		(1 << 5)
#define	IWP_F_SCANNING		(1 << 6)
#define	IWP_F_SUSPEND		(1 << 7)
#define	IWP_F_RADIO_OFF		(1 << 8)
#define	IWP_F_STATISTICS	(1 << 9)
#define	IWP_F_READY		(1 << 10)
#define	IWP_F_PUT_SEG		(1 << 11)
#define	IWP_F_QUIESCED		(1 << 12)
#define	IWP_F_LAZY_RESUME	(1 << 13)

#define	IWP_SUCCESS		0
#define	IWP_FAIL		EIO

#ifdef DEBUG
#define	IWP_DEBUG_80211		(1 << 0)
#define	IWP_DEBUG_CMD		(1 << 1)
#define	IWP_DEBUG_DMA		(1 << 2)
#define	IWP_DEBUG_EEPROM	(1 << 3)
#define	IWP_DEBUG_FW		(1 << 4)
#define	IWP_DEBUG_HW		(1 << 5)
#define	IWP_DEBUG_INTR		(1 << 6)
#define	IWP_DEBUG_MRR		(1 << 7)
#define	IWP_DEBUG_PIO		(1 << 8)
#define	IWP_DEBUG_RX		(1 << 9)
#define	IWP_DEBUG_SCAN		(1 << 10)
#define	IWP_DEBUG_TX		(1 << 11)
#define	IWP_DEBUG_RATECTL	(1 << 12)
#define	IWP_DEBUG_RADIO		(1 << 13)
#define	IWP_DEBUG_RESUME	(1 << 14)
#define	IWP_DEBUG_CALIBRATION	(1 << 15)
#define	IWP_DEBUG_BA		(1 << 16)
#define	IWP_DEBUG_RXON		(1 << 17)
#define	IWP_DEBUG_HWRATE	(1 << 18)
#define	IWP_DEBUG_HTRATE	(1 << 19)
#define	IWP_DEBUG_QOS		(1 << 20)

void iwp_dbg(uint32_t flags, const char *fmt, ...);
#define	IWP_DBG(x) \
	iwp_dbg x
#else
#define	IWP_DBG(x)
#endif


/*
 * Interaction steps for 802.11e/n between net80211 module
 * and iwp driver:
 * -- setup link with 802.11n AP: net80211 module is responsible
 *    for setup link with 802.11n AP. iwp driver monitors current
 *    state and make relevant configurations according work mode.
 * -- QoS parameter updating: net80211 module is responsible for
 *    extract EDCA parameters from the fram of AP, iwp driver get
 *    these parameters and make relevant configuration to HW.
 * -- TX queue management: iwp driver place a frame into suitable
 *    TX queue according to frame type and user priority extracted
 *    from frame head.
 * -- MIMO: iwp driver make relevant configurations in TX and RX
 *    direction according to AP mode from net80211 module.
 * -- Link aggregation: AMSDU is implemented by net80211 module and
 *    AMPDU is implemented by both iwp driver and net80211 module.
 *    iwp driver distinguish frames in one AMPDU frame and net80211
 *    module is responsible reordering every frame.
 * -- Block ACK: net80211 module is responsible for setup agreement
 *    with AP and iwp driver is responsible for realistic ACK.
 * -- Rate scaling: This function is implemented independently by
 *    iwp driver.
 * -- HT protection: This feature is also implemented by iwp driver
 *    no interaction with net80211 module.
 */

#ifdef __cplusplus
}
#endif

#endif /* _IWP_BASE_H */
