/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright (c) 2008 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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

#include <sys/ddi.h>

#include "arn_core.h"
#include "arn_hw.h"
#include "arn_reg.h"
#include "arn_phy.h"

/* ARGSUSED */
static void
ath9k_hw_set_txq_interrupts(struct ath_hal *ah,
    struct ath9k_tx_queue_info *qi)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	ARN_DBG((ARN_DBG_INTERRUPT,
	    "%s: tx ok 0x%x err 0x%x desc 0x%x eol 0x%x urn 0x%x\n",
	    __func__, ahp->ah_txOkInterruptMask,
	    ahp->ah_txErrInterruptMask, ahp->ah_txDescInterruptMask,
	    ahp->ah_txEolInterruptMask, ahp->ah_txUrnInterruptMask));

	REG_WRITE(ah, AR_IMR_S0,
	    SM(ahp->ah_txOkInterruptMask, AR_IMR_S0_QCU_TXOK) |
	    SM(ahp->ah_txDescInterruptMask, AR_IMR_S0_QCU_TXDESC));
	REG_WRITE(ah, AR_IMR_S1,
	    SM(ahp->ah_txErrInterruptMask, AR_IMR_S1_QCU_TXERR)|
	    SM(ahp->ah_txEolInterruptMask, AR_IMR_S1_QCU_TXEOL));
	REG_RMW_FIELD(ah, AR_IMR_S2,
	    AR_IMR_S2_QCU_TXURN, ahp->ah_txUrnInterruptMask);
}

void
ath9k_hw_dmaRegDump(struct ath_hal *ah)
{
	uint32_t val[ATH9K_NUM_DMA_DEBUG_REGS];
	int qcuOffset = 0, dcuOffset = 0;
	uint32_t *qcuBase = &val[0], *dcuBase = &val[4];
	int i;

	REG_WRITE(ah, AR_MACMISC,
	    ((AR_MACMISC_DMA_OBS_LINE_8 << AR_MACMISC_DMA_OBS_S) |
	    (AR_MACMISC_MISC_OBS_BUS_1 <<
	    AR_MACMISC_MISC_OBS_BUS_MSB_S)));

	ARN_DBG((ARN_DBG_REG_IO, "Raw DMA Debug values:\n"));

	for (i = 0; i < ATH9K_NUM_DMA_DEBUG_REGS; i++) {
		if (i % 4 == 0)
			ARN_DBG((ARN_DBG_REG_IO, "\n"));

		val[i] = REG_READ(ah, AR_DMADBG_0 + (i * sizeof (uint32_t)));
		ARN_DBG((ARN_DBG_REG_IO, "%d: %08x ", i, val[i]));
	}

	ARN_DBG((ARN_DBG_REG_IO, "\n\n"));
	ARN_DBG((ARN_DBG_REG_IO,
	    "Num QCU: chain_st fsp_ok fsp_st DCU: chain_st\n"));

	for (i = 0; i < ATH9K_NUM_QUEUES;
	    i++, qcuOffset += 4, dcuOffset += 5) {
		if (i == 8) {
			qcuOffset = 0;
			qcuBase++;
		}

		if (i == 6) {
			dcuOffset = 0;
			dcuBase++;
		}

		ARN_DBG((ARN_DBG_REG_IO,
		    "%2d          %2x      %1x     %2x           %2x\n",
		    i, (*qcuBase & (0x7 << qcuOffset)) >> qcuOffset,
		    (*qcuBase & (0x8 << qcuOffset)) >> (qcuOffset + 3),
		    val[2] & (0x7 << (i * 3)) >> (i * 3),
		    (*dcuBase & (0x1f << dcuOffset)) >> dcuOffset));
	}

	ARN_DBG((ARN_DBG_REG_IO, "\n"));
	ARN_DBG((ARN_DBG_REG_IO,
	    "qcu_stitch state:   %2x    qcu_fetch state:        %2x\n",
	    (val[3] & 0x003c0000) >> 18, (val[3] & 0x03c00000) >> 22));
	ARN_DBG((ARN_DBG_REG_IO,
	    "qcu_complete state: %2x    dcu_complete state:     %2x\n",
	    (val[3] & 0x1c000000) >> 26, (val[6] & 0x3)));
	ARN_DBG((ARN_DBG_REG_IO,
	    "dcu_arb state:      %2x    dcu_fp state:           %2x\n",
	    (val[5] & 0x06000000) >> 25, (val[5] & 0x38000000) >> 27));
	ARN_DBG((ARN_DBG_REG_IO,
	    "chan_idle_dur:     %3d    chan_idle_dur_valid:     %1d\n",
	    (val[6] & 0x000003fc) >> 2, (val[6] & 0x00000400) >> 10));
	ARN_DBG((ARN_DBG_REG_IO,
	    "txfifo_valid_0:      %1d    txfifo_valid_1:          %1d\n",
	    (val[6] & 0x00000800) >> 11, (val[6] & 0x00001000) >> 12));
	ARN_DBG((ARN_DBG_REG_IO,
	    "txfifo_dcu_num_0:   %2d    txfifo_dcu_num_1:       %2d\n",
	    (val[6] & 0x0001e000) >> 13, (val[6] & 0x001e0000) >> 17));

	ARN_DBG((ARN_DBG_REG_IO, "pcu observe 0x%x \n",
	    REG_READ(ah, AR_OBS_BUS_1)));
	ARN_DBG((ARN_DBG_REG_IO,
	    "AR_CR 0x%x \n", REG_READ(ah, AR_CR)));
}

uint32_t
ath9k_hw_gettxbuf(struct ath_hal *ah, uint32_t q)
{
	return (REG_READ(ah, AR_QTXDP(q)));
}

boolean_t
ath9k_hw_puttxbuf(struct ath_hal *ah, uint32_t q, uint32_t txdp)
{
	REG_WRITE(ah, AR_QTXDP(q), txdp);

	return (B_TRUE);
}

boolean_t
ath9k_hw_txstart(struct ath_hal *ah, uint32_t q)
{
	ARN_DBG((ARN_DBG_XMIT, "arn: ath9k_hw_txstart(): "
	    "tramist queue is %u\n", q));

	REG_WRITE(ah, AR_Q_TXE, 1 << q);

	return (B_TRUE);
}

uint32_t
ath9k_hw_numtxpending(struct ath_hal *ah, uint32_t q)
{
	uint32_t npend;

	npend = REG_READ(ah, AR_QSTS(q)) & AR_Q_STS_PEND_FR_CNT;
	if (npend == 0) {

		if (REG_READ(ah, AR_Q_TXE) & (1 << q))
			npend = 1;
	}

	return (npend);
}

boolean_t
ath9k_hw_updatetxtriglevel(struct ath_hal *ah, boolean_t bIncTrigLevel)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	uint32_t txcfg, curLevel, newLevel;
	enum ath9k_int omask;

	if (ah->ah_txTrigLevel >= MAX_TX_FIFO_THRESHOLD)
		return (B_FALSE);

	omask = ath9k_hw_set_interrupts(ah,
	    ahp->ah_maskReg & ~ATH9K_INT_GLOBAL);

	txcfg = REG_READ(ah, AR_TXCFG);
	curLevel = MS(txcfg, AR_FTRIG);
	newLevel = curLevel;
	if (bIncTrigLevel) {
		if (curLevel < MAX_TX_FIFO_THRESHOLD)
			newLevel++;
	} else if (curLevel > MIN_TX_FIFO_THRESHOLD)
		newLevel--;
	if (newLevel != curLevel)
		REG_WRITE(ah, AR_TXCFG,
		    (txcfg & ~AR_FTRIG) | SM(newLevel, AR_FTRIG));

	(void) ath9k_hw_set_interrupts(ah, omask);

	ah->ah_txTrigLevel = (uint16_t)newLevel; /* ??? */

	return (newLevel != curLevel);
}

boolean_t
ath9k_hw_stoptxdma(struct ath_hal *ah, uint32_t q)
{
	uint32_t tsfLow, j, wait;

	REG_WRITE(ah, AR_Q_TXD, 1 << q);

	for (wait = 1000; wait != 0; wait--) {
		if (ath9k_hw_numtxpending(ah, q) == 0)
			break;
		drv_usecwait(100);
	}

	if (ath9k_hw_numtxpending(ah, q)) {
		ARN_DBG((ARN_DBG_QUEUE,
		    "%s: Num of pending TX Frames %d on Q %d\n",
		    __func__, ath9k_hw_numtxpending(ah, q), q));

		for (j = 0; j < 2; j++) {
			tsfLow = REG_READ(ah, AR_TSF_L32);
			REG_WRITE(ah, AR_QUIET2, SM(10, AR_QUIET2_QUIET_DUR));
			REG_WRITE(ah, AR_QUIET_PERIOD, 100);
			REG_WRITE(ah, AR_NEXT_QUIET_TIMER, tsfLow >> 10);
			REG_SET_BIT(ah, AR_TIMER_MODE, AR_QUIET_TIMER_EN);

			if ((REG_READ(ah, AR_TSF_L32) >> 10) == (tsfLow >> 10))
				break;
			ARN_DBG((ARN_DBG_QUEUE,
			    "%s: TSF have moved while trying to set "
			    "quiet time TSF: 0x%08x\n",
			    __func__, tsfLow));
		}

		REG_SET_BIT(ah, AR_DIAG_SW, AR_DIAG_FORCE_CH_IDLE_HIGH);

		drv_usecwait(200);
		REG_CLR_BIT(ah, AR_TIMER_MODE, AR_QUIET_TIMER_EN);

		wait = 1000;

		while (ath9k_hw_numtxpending(ah, q)) {
			if ((--wait) == 0) {
				ARN_DBG((ARN_DBG_XMIT,
				    "%s: Failed to stop Tx DMA in 100 "
				    "msec after killing last frame\n",
				    __func__));
				break;
			}
			drv_usecwait(100);
		}

		REG_CLR_BIT(ah, AR_DIAG_SW, AR_DIAG_FORCE_CH_IDLE_HIGH);
	}

	REG_WRITE(ah, AR_Q_TXD, 0);

	return (wait != 0);
}

/* ARGSUSED */
boolean_t
ath9k_hw_filltxdesc(struct ath_hal *ah, struct ath_desc *ds,
    uint32_t segLen, boolean_t firstSeg,
    boolean_t lastSeg, const struct ath_desc *ds0)
{
	struct ar5416_desc *ads = AR5416DESC(ds);

	if (firstSeg) {
		ads->ds_ctl1 |= segLen | (lastSeg ? 0 : AR_TxMore);
	} else if (lastSeg) {
		ads->ds_ctl0 = 0;
		ads->ds_ctl1 = segLen;
		ads->ds_ctl2 = AR5416DESC_CONST(ds0)->ds_ctl2;
		ads->ds_ctl3 = AR5416DESC_CONST(ds0)->ds_ctl3;
	} else {
		ads->ds_ctl0 = 0;
		ads->ds_ctl1 = segLen | AR_TxMore;
		ads->ds_ctl2 = 0;
		ads->ds_ctl3 = 0;
	}
	ads->ds_txstatus0 = ads->ds_txstatus1 = 0;
	ads->ds_txstatus2 = ads->ds_txstatus3 = 0;
	ads->ds_txstatus4 = ads->ds_txstatus5 = 0;
	ads->ds_txstatus6 = ads->ds_txstatus7 = 0;
	ads->ds_txstatus8 = ads->ds_txstatus9 = 0;

	return (B_TRUE);
}

/* ARGSUSED */
void
ath9k_hw_cleartxdesc(struct ath_hal *ah, struct ath_desc *ds)
{
	struct ar5416_desc *ads = AR5416DESC(ds);

	ads->ds_txstatus0 = ads->ds_txstatus1 = 0;
	ads->ds_txstatus2 = ads->ds_txstatus3 = 0;
	ads->ds_txstatus4 = ads->ds_txstatus5 = 0;
	ads->ds_txstatus6 = ads->ds_txstatus7 = 0;
	ads->ds_txstatus8 = ads->ds_txstatus9 = 0;
}

int
ath9k_hw_txprocdesc(struct ath_hal *ah, struct ath_desc *ds)
{
	struct ar5416_desc *ads = AR5416DESC(ds);

	if ((ads->ds_txstatus9 & AR_TxDone) == 0)
		return (EINPROGRESS);

	ds->ds_txstat.ts_seqnum = MS(ads->ds_txstatus9, AR_SeqNum);
	ds->ds_txstat.ts_tstamp = ads->AR_SendTimestamp;
	ds->ds_txstat.ts_status = 0;
	ds->ds_txstat.ts_flags = 0;

	if (ads->ds_txstatus1 & AR_ExcessiveRetries) {
		ARN_DBG((ARN_DBG_INTERRUPT, "arn: ATH9K_TXERR_XRETRY\n"));
		ds->ds_txstat.ts_status |= ATH9K_TXERR_XRETRY;
	}
	if (ads->ds_txstatus1 & AR_Filtered) {
		ARN_DBG((ARN_DBG_INTERRUPT, "arn: AR_Filtered\n"));
		ds->ds_txstat.ts_status |= ATH9K_TXERR_FILT;
	}
	if (ads->ds_txstatus1 & AR_FIFOUnderrun) {
		ARN_DBG((ARN_DBG_INTERRUPT, "arn: ATH9K_TXERR_FIFO\n"));
		ds->ds_txstat.ts_status |= ATH9K_TXERR_FIFO;
		(void) ath9k_hw_updatetxtriglevel(ah, B_TRUE);
	}
	if (ads->ds_txstatus9 & AR_TxOpExceeded) {
		ARN_DBG((ARN_DBG_INTERRUPT, "arn: ATH9K_TXERR_XTXOP\n"));
		ds->ds_txstat.ts_status |= ATH9K_TXERR_XTXOP;
	}
	if (ads->ds_txstatus1 & AR_TxTimerExpired) {
		ARN_DBG((ARN_DBG_INTERRUPT,
		"arn: ATH9K_TXERR_TIMER_EXPIRED\n"));
		ds->ds_txstat.ts_status |= ATH9K_TXERR_TIMER_EXPIRED;
	}

	if (ads->ds_txstatus1 & AR_DescCfgErr) {
		ARN_DBG((ARN_DBG_INTERRUPT, "arn: ATH9K_TX_DESC_CFG_ERR\n"));
		ds->ds_txstat.ts_flags |= ATH9K_TX_DESC_CFG_ERR;
	}
	if (ads->ds_txstatus1 & AR_TxDataUnderrun) {
		ds->ds_txstat.ts_flags |= ATH9K_TX_DATA_UNDERRUN;
		(void) ath9k_hw_updatetxtriglevel(ah, B_TRUE);
	}
	if (ads->ds_txstatus1 & AR_TxDelimUnderrun) {
		ds->ds_txstat.ts_flags |= ATH9K_TX_DELIM_UNDERRUN;
		(void) ath9k_hw_updatetxtriglevel(ah, B_TRUE);
	}
	if (ads->ds_txstatus0 & AR_TxBaStatus) {
		ds->ds_txstat.ts_flags |= ATH9K_TX_BA;
		ds->ds_txstat.ba_low = ads->AR_BaBitmapLow;
		ds->ds_txstat.ba_high = ads->AR_BaBitmapHigh;
	}

	ds->ds_txstat.ts_rateindex = MS(ads->ds_txstatus9, AR_FinalTxIdx);
	switch (ds->ds_txstat.ts_rateindex) {
	case 0:
		ds->ds_txstat.ts_ratecode = MS(ads->ds_ctl3, AR_XmitRate0);
		break;
	case 1:
		ds->ds_txstat.ts_ratecode = MS(ads->ds_ctl3, AR_XmitRate1);
		break;
	case 2:
		ds->ds_txstat.ts_ratecode = MS(ads->ds_ctl3, AR_XmitRate2);
		break;
	case 3:
		ds->ds_txstat.ts_ratecode = MS(ads->ds_ctl3, AR_XmitRate3);
		break;
	}

	ds->ds_txstat.ts_rssi = MS(ads->ds_txstatus5, AR_TxRSSICombined);
	ds->ds_txstat.ts_rssi_ctl0 = MS(ads->ds_txstatus0, AR_TxRSSIAnt00);
	ds->ds_txstat.ts_rssi_ctl1 = MS(ads->ds_txstatus0, AR_TxRSSIAnt01);
	ds->ds_txstat.ts_rssi_ctl2 = MS(ads->ds_txstatus0, AR_TxRSSIAnt02);
	ds->ds_txstat.ts_rssi_ext0 = MS(ads->ds_txstatus5, AR_TxRSSIAnt10);
	ds->ds_txstat.ts_rssi_ext1 = MS(ads->ds_txstatus5, AR_TxRSSIAnt11);
	ds->ds_txstat.ts_rssi_ext2 = MS(ads->ds_txstatus5, AR_TxRSSIAnt12);
	ds->ds_txstat.evm0 = ads->AR_TxEVM0;
	ds->ds_txstat.evm1 = ads->AR_TxEVM1;
	ds->ds_txstat.evm2 = ads->AR_TxEVM2;
	ds->ds_txstat.ts_shortretry = MS(ads->ds_txstatus1, AR_RTSFailCnt);
	ds->ds_txstat.ts_longretry = MS(ads->ds_txstatus1, AR_DataFailCnt);
	ds->ds_txstat.ts_virtcol = MS(ads->ds_txstatus1, AR_VirtRetryCnt);
	ds->ds_txstat.ts_antenna = 1;

	return (0);
}

void
ath9k_hw_set11n_txdesc(struct ath_hal *ah, struct ath_desc *ds,
    uint32_t pktLen, enum ath9k_pkt_type type, uint32_t txPower,
    uint32_t keyIx, enum ath9k_key_type keyType, uint32_t flags)
{
	struct ar5416_desc *ads = AR5416DESC(ds);
	struct ath_hal_5416 *ahp = AH5416(ah);

	txPower += ahp->ah_txPowerIndexOffset;
	if (txPower > 63)
		txPower = 63;

	ads->ds_ctl0 = (pktLen & AR_FrameLen) |
	    (flags & ATH9K_TXDESC_VMF ? AR_VirtMoreFrag : 0) |
	    SM(txPower, AR_XmitPower) |
	    (flags & ATH9K_TXDESC_VEOL ? AR_VEOL : 0) |
	    (flags & ATH9K_TXDESC_CLRDMASK ? AR_ClrDestMask : 0) |
	    (flags & ATH9K_TXDESC_INTREQ ? AR_TxIntrReq : 0) |
	    (keyIx != ATH9K_TXKEYIX_INVALID ? AR_DestIdxValid : 0);

	ads->ds_ctl1 =
	    (keyIx != ATH9K_TXKEYIX_INVALID ? SM(keyIx, AR_DestIdx) : 0) |
	    SM(type, AR_FrameType) |
	    (flags & ATH9K_TXDESC_NOACK ? AR_NoAck : 0) |
	    (flags & ATH9K_TXDESC_EXT_ONLY ? AR_ExtOnly : 0) |
	    (flags & ATH9K_TXDESC_EXT_AND_CTL ? AR_ExtAndCtl : 0);

	ads->ds_ctl6 = SM(keyType, AR_EncrType);

	if (AR_SREV_9285(ah)) {
		ads->ds_ctl8 = 0;
		ads->ds_ctl9 = 0;
		ads->ds_ctl10 = 0;
		ads->ds_ctl11 = 0;
	}

}

/* ARGSUSED */
void
ath9k_hw_set11n_ratescenario(struct ath_hal *ah, struct ath_desc *ds,
    struct ath_desc *lastds,
    uint32_t durUpdateEn, uint32_t rtsctsRate,
    uint32_t rtsctsDuration,
    struct ath9k_11n_rate_series series[],
    uint32_t nseries, uint32_t flags)
{
	struct ar5416_desc *ads = AR5416DESC(ds);
	struct ar5416_desc *last_ads = AR5416DESC(lastds);
	uint32_t ds_ctl0;

	(void) nseries;
	(void) rtsctsDuration;

	if (flags & (ATH9K_TXDESC_RTSENA | ATH9K_TXDESC_CTSENA)) {
		ds_ctl0 = ads->ds_ctl0;

		if (flags & ATH9K_TXDESC_RTSENA) {
			ds_ctl0 &= ~AR_CTSEnable;
			ds_ctl0 |= AR_RTSEnable;
		} else {
			ds_ctl0 &= ~AR_RTSEnable;
			ds_ctl0 |= AR_CTSEnable;
		}

		ads->ds_ctl0 = ds_ctl0;
	} else {
		ads->ds_ctl0 =
		    (ads->ds_ctl0 & ~(AR_RTSEnable | AR_CTSEnable));
	}

	ads->ds_ctl2 = set11nTries(series, 0) |
	    set11nTries(series, 1) |
	    set11nTries(series, 2) |
	    set11nTries(series, 3) |
	    (durUpdateEn ? AR_DurUpdateEna : 0) |
	    SM(0, AR_BurstDur);

	ads->ds_ctl3 = set11nRate(series, 0) |
	    set11nRate(series, 1) |
	    set11nRate(series, 2) |
	    set11nRate(series, 3);

	ads->ds_ctl4 = set11nPktDurRTSCTS(series, 0) |
	    set11nPktDurRTSCTS(series, 1);

	ads->ds_ctl5 = set11nPktDurRTSCTS(series, 2) |
	    set11nPktDurRTSCTS(series, 3);

	ads->ds_ctl7 = set11nRateFlags(series, 0) |
	    set11nRateFlags(series, 1) |
	    set11nRateFlags(series, 2) |
	    set11nRateFlags(series, 3) |
	    SM(rtsctsRate, AR_RTSCTSRate);
	last_ads->ds_ctl2 = ads->ds_ctl2;
	last_ads->ds_ctl3 = ads->ds_ctl3;
}

/* ARGSUSED */
void
ath9k_hw_set11n_aggr_first(struct ath_hal *ah, struct ath_desc *ds,
    uint32_t aggrLen)
{
	struct ar5416_desc *ads = AR5416DESC(ds);

	ads->ds_ctl1 |= (AR_IsAggr | AR_MoreAggr);
	ads->ds_ctl6 &= ~AR_AggrLen;
	ads->ds_ctl6 |= SM(aggrLen, AR_AggrLen);
}

/* ARGSUSED */
void
ath9k_hw_set11n_aggr_middle(struct ath_hal *ah, struct ath_desc *ds,
    uint32_t numDelims)
{
	struct ar5416_desc *ads = AR5416DESC(ds);
	unsigned int ctl6;

	ads->ds_ctl1 |= (AR_IsAggr | AR_MoreAggr);

	ctl6 = ads->ds_ctl6;
	ctl6 &= ~AR_PadDelim;
	ctl6 |= SM(numDelims, AR_PadDelim);
	ads->ds_ctl6 = ctl6;
}

/* ARGSUSED */
void
ath9k_hw_set11n_aggr_last(struct ath_hal *ah, struct ath_desc *ds)
{
	struct ar5416_desc *ads = AR5416DESC(ds);

	ads->ds_ctl1 |= AR_IsAggr;
	ads->ds_ctl1 &= ~AR_MoreAggr;
	ads->ds_ctl6 &= ~AR_PadDelim;
}

/* ARGSUSED */
void
ath9k_hw_clr11n_aggr(struct ath_hal *ah, struct ath_desc *ds)
{
	struct ar5416_desc *ads = AR5416DESC(ds);

	ads->ds_ctl1 &= (~AR_IsAggr & ~AR_MoreAggr);
}

/* ARGSUSED */
void
ath9k_hw_set11n_burstduration(struct ath_hal *ah, struct ath_desc *ds,
    uint32_t burstDuration)
{
	struct ar5416_desc *ads = AR5416DESC(ds);

	ads->ds_ctl2 &= ~AR_BurstDur;
	ads->ds_ctl2 |= SM(burstDuration, AR_BurstDur);
}

/* ARGSUSED */
void
ath9k_hw_set11n_virtualmorefrag(struct ath_hal *ah, struct ath_desc *ds,
    uint32_t vmf)
{
	struct ar5416_desc *ads = AR5416DESC(ds);

	if (vmf)
		ads->ds_ctl0 |= AR_VirtMoreFrag;
	else
		ads->ds_ctl0 &= ~AR_VirtMoreFrag;
}

void
ath9k_hw_gettxintrtxqs(struct ath_hal *ah, uint32_t *txqs)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	*txqs &= ahp->ah_intrTxqs;
	ahp->ah_intrTxqs &= ~(*txqs);
}

boolean_t
ath9k_hw_set_txq_props(struct ath_hal *ah, int q,
    const struct ath9k_tx_queue_info *qinfo)
{
	uint32_t cw;
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ath9k_hw_capabilities *pCap = &ah->ah_caps;
	struct ath9k_tx_queue_info *qi;

	if (q >= pCap->total_queues) {
		ARN_DBG((ARN_DBG_QUEUE, "%s: invalid queue num %u\n",
		    __func__, q));
		return (B_FALSE);
	}

	qi = &ahp->ah_txq[q];
	if (qi->tqi_type == ATH9K_TX_QUEUE_INACTIVE) {
		ARN_DBG((ARN_DBG_QUEUE, "%s: inactive queue\n",
		    __func__));
		return (B_FALSE);
	}

	ARN_DBG((ARN_DBG_QUEUE, "%s: queue %p\n", __func__, qi));

	qi->tqi_ver = qinfo->tqi_ver;
	qi->tqi_subtype = qinfo->tqi_subtype;
	qi->tqi_qflags = qinfo->tqi_qflags;
	qi->tqi_priority = qinfo->tqi_priority;
	if (qinfo->tqi_aifs != ATH9K_TXQ_USEDEFAULT)
		qi->tqi_aifs = min(qinfo->tqi_aifs, 255U);
	else
		qi->tqi_aifs = INIT_AIFS;
	if (qinfo->tqi_cwmin != ATH9K_TXQ_USEDEFAULT) {
		cw = min(qinfo->tqi_cwmin, 1024U);
		qi->tqi_cwmin = 1;
		while (qi->tqi_cwmin < cw)
			qi->tqi_cwmin = (qi->tqi_cwmin << 1) | 1;
	} else
		qi->tqi_cwmin = qinfo->tqi_cwmin;
	if (qinfo->tqi_cwmax != ATH9K_TXQ_USEDEFAULT) {
		cw = min(qinfo->tqi_cwmax, 1024U);
		qi->tqi_cwmax = 1;
		while (qi->tqi_cwmax < cw)
			qi->tqi_cwmax = (qi->tqi_cwmax << 1) | 1;
	} else
		qi->tqi_cwmax = INIT_CWMAX;

	if (qinfo->tqi_shretry != 0)
		qi->tqi_shretry = min((uint32_t)qinfo->tqi_shretry, 15U);
	else
		qi->tqi_shretry = INIT_SH_RETRY;
	if (qinfo->tqi_lgretry != 0)
		qi->tqi_lgretry = min((uint32_t)qinfo->tqi_lgretry, 15U);
	else
		qi->tqi_lgretry = INIT_LG_RETRY;
	qi->tqi_cbrPeriod = qinfo->tqi_cbrPeriod;
	qi->tqi_cbrOverflowLimit = qinfo->tqi_cbrOverflowLimit;
	qi->tqi_burstTime = qinfo->tqi_burstTime;
	qi->tqi_readyTime = qinfo->tqi_readyTime;

	switch (qinfo->tqi_subtype) {
	case ATH9K_WME_UPSD:
		if (qi->tqi_type == ATH9K_TX_QUEUE_DATA)
			qi->tqi_intFlags = ATH9K_TXQ_USE_LOCKOUT_BKOFF_DIS;
		break;
	default:
		break;
	}

	return (B_TRUE);
}

boolean_t
ath9k_hw_get_txq_props(struct ath_hal *ah, int q,
    struct ath9k_tx_queue_info *qinfo)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ath9k_hw_capabilities *pCap = &ah->ah_caps;
	struct ath9k_tx_queue_info *qi;

	if (q >= pCap->total_queues) {
		ARN_DBG((ARN_DBG_QUEUE, "arn: ath9k_hw_get_txq_props(): "
		    "invalid queue num %u\n", q));
		return (B_FALSE);
	}

	qi = &ahp->ah_txq[q];
	if (qi->tqi_type == ATH9K_TX_QUEUE_INACTIVE) {
		ARN_DBG((ARN_DBG_QUEUE, "arn: ath9k_hw_get_txq_props(): "
		    "inactive queue\n"));
		return (B_FALSE);
	}

	qinfo->tqi_qflags = qi->tqi_qflags;
	qinfo->tqi_ver = qi->tqi_ver;
	qinfo->tqi_subtype = qi->tqi_subtype;
	qinfo->tqi_qflags = qi->tqi_qflags;
	qinfo->tqi_priority = qi->tqi_priority;
	qinfo->tqi_aifs = qi->tqi_aifs;
	qinfo->tqi_cwmin = qi->tqi_cwmin;
	qinfo->tqi_cwmax = qi->tqi_cwmax;
	qinfo->tqi_shretry = qi->tqi_shretry;
	qinfo->tqi_lgretry = qi->tqi_lgretry;
	qinfo->tqi_cbrPeriod = qi->tqi_cbrPeriod;
	qinfo->tqi_cbrOverflowLimit = qi->tqi_cbrOverflowLimit;
	qinfo->tqi_burstTime = qi->tqi_burstTime;
	qinfo->tqi_readyTime = qi->tqi_readyTime;

	return (B_TRUE);
}

int
ath9k_hw_setuptxqueue(struct ath_hal *ah, enum ath9k_tx_queue type,
    const struct ath9k_tx_queue_info *qinfo)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ath9k_tx_queue_info *qi;
	struct ath9k_hw_capabilities *pCap = &ah->ah_caps;
	int q;

	switch (type) {
	case ATH9K_TX_QUEUE_BEACON:
		q = pCap->total_queues - 1;
		break;
	case ATH9K_TX_QUEUE_CAB:
		q = pCap->total_queues - 2;
		break;
	case ATH9K_TX_QUEUE_PSPOLL:
		q = 1;
		break;
	case ATH9K_TX_QUEUE_UAPSD:
		q = pCap->total_queues - 3;
		break;
	case ATH9K_TX_QUEUE_DATA:
		for (q = 0; q < pCap->total_queues; q++)
			if (ahp->ah_txq[q].tqi_type ==
			    ATH9K_TX_QUEUE_INACTIVE)
				break;
		if (q == pCap->total_queues) {
			ARN_DBG((ARN_DBG_QUEUE,
			    "arn: ath9k_hw_setuptxqueue(): "
			    "no available tx queue\n"));
			return (-1);
		}
		break;
	default:
		ARN_DBG((ARN_DBG_QUEUE,
		    "arn: ath9k_hw_setuptxqueue(): "
		    "bad tx queue type %u\n", type));

		return (-1);
	}

	ARN_DBG((ARN_DBG_QUEUE, "arn: ath9k_hw_setuptxqueue(): "
	    "queue %u\n", q));

	qi = &ahp->ah_txq[q];
	if (qi->tqi_type != ATH9K_TX_QUEUE_INACTIVE) {
		ARN_DBG((ARN_DBG_QUEUE, "arn: ath9k_hw_setuptxqueue(): "
		    "tx queue %u already active\n", q));

		return (-1);
	}
	(void) memset(qi, 0, sizeof (struct ath9k_tx_queue_info));
	qi->tqi_type = type;
	if (qinfo == NULL) {
		qi->tqi_qflags = TXQ_FLAG_TXOKINT_ENABLE |
		    TXQ_FLAG_TXERRINT_ENABLE |
		    TXQ_FLAG_TXDESCINT_ENABLE |
		    TXQ_FLAG_TXURNINT_ENABLE;
		qi->tqi_aifs = INIT_AIFS;
		qi->tqi_cwmin = ATH9K_TXQ_USEDEFAULT;
		qi->tqi_cwmax = INIT_CWMAX;
		qi->tqi_shretry = INIT_SH_RETRY;
		qi->tqi_lgretry = INIT_LG_RETRY;
		qi->tqi_physCompBuf = 0;
	} else {
		qi->tqi_physCompBuf = qinfo->tqi_physCompBuf;
		(void) ath9k_hw_set_txq_props(ah, q, qinfo);
	}

	return (q);
}

boolean_t
ath9k_hw_releasetxqueue(struct ath_hal *ah, uint32_t q)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ath9k_hw_capabilities *pCap = &ah->ah_caps;
	struct ath9k_tx_queue_info *qi;

	if (q >= pCap->total_queues) {
		ARN_DBG((ARN_DBG_QUEUE, "arn: arn_txq_setup(): "
		    "invalid queue num %u\n", q));
		return (B_FALSE);
	}
	qi = &ahp->ah_txq[q];
	if (qi->tqi_type == ATH9K_TX_QUEUE_INACTIVE) {
		ARN_DBG((ARN_DBG_QUEUE, "arn: arn_txq_setup(): "
		    "inactive queue %u\n", q));
		return (B_FALSE);
	}

	ARN_DBG((ARN_DBG_QUEUE, "arn: arn_txq_setup(): "
	    "release queue %u\n", q));


	qi->tqi_type = ATH9K_TX_QUEUE_INACTIVE;
	ahp->ah_txOkInterruptMask &= ~(1 << q);
	ahp->ah_txErrInterruptMask &= ~(1 << q);
	ahp->ah_txDescInterruptMask &= ~(1 << q);
	ahp->ah_txEolInterruptMask &= ~(1 << q);
	ahp->ah_txUrnInterruptMask &= ~(1 << q);
	ath9k_hw_set_txq_interrupts(ah, qi);

	return (B_TRUE);
}

boolean_t
ath9k_hw_resettxqueue(struct ath_hal *ah, uint32_t q)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ath9k_hw_capabilities *pCap = &ah->ah_caps;
	struct ath9k_channel *chan = ah->ah_curchan;
	struct ath9k_tx_queue_info *qi;
	uint32_t cwMin, chanCwMin, value;

	if (q >= pCap->total_queues) {
		ARN_DBG((ARN_DBG_QUEUE, "%s: invalid queue num %u\n",
		    __func__, q));

		return (B_FALSE);
	}

	qi = &ahp->ah_txq[q];
	if (qi->tqi_type == ATH9K_TX_QUEUE_INACTIVE) {
		ARN_DBG((ARN_DBG_QUEUE, "%s: inactive queue %u\n",
		    __func__, q));

		return (B_TRUE);
	}

	ARN_DBG((ARN_DBG_QUEUE,
	    "%s: reset queue %u\n", __func__, q));

	if (qi->tqi_cwmin == ATH9K_TXQ_USEDEFAULT) {
		if (chan && IS_CHAN_B(chan))
			chanCwMin = INIT_CWMIN_11B;
		else
			chanCwMin = INIT_CWMIN;

		for (cwMin = 1; cwMin < chanCwMin; cwMin = (cwMin << 1) | 1) {
			/* Nothing to do */
		}
	} else
		cwMin = qi->tqi_cwmin;

	REG_WRITE(ah, AR_DLCL_IFS(q),
	    SM(cwMin, AR_D_LCL_IFS_CWMIN) |
	    SM(qi->tqi_cwmax, AR_D_LCL_IFS_CWMAX) |
	    SM(qi->tqi_aifs, AR_D_LCL_IFS_AIFS));

	REG_WRITE(ah, AR_DRETRY_LIMIT(q),
	    SM(INIT_SSH_RETRY, AR_D_RETRY_LIMIT_STA_SH) |
	    SM(INIT_SLG_RETRY, AR_D_RETRY_LIMIT_STA_LG) |
	    SM(qi->tqi_shretry, AR_D_RETRY_LIMIT_FR_SH));

	REG_WRITE(ah, AR_QMISC(q), AR_Q_MISC_DCU_EARLY_TERM_REQ);
	REG_WRITE(ah, AR_DMISC(q),
	    AR_D_MISC_CW_BKOFF_EN | AR_D_MISC_FRAG_WAIT_EN | 0x2);

	if (qi->tqi_cbrPeriod) {
		REG_WRITE(ah, AR_QCBRCFG(q),
		    SM(qi->tqi_cbrPeriod, AR_Q_CBRCFG_INTERVAL) |
		    SM(qi->tqi_cbrOverflowLimit, AR_Q_CBRCFG_OVF_THRESH));
		REG_WRITE(ah, AR_QMISC(q),
		    REG_READ(ah, AR_QMISC(q)) | AR_Q_MISC_FSP_CBR |
		    (qi->tqi_cbrOverflowLimit ?
		    AR_Q_MISC_CBR_EXP_CNTR_LIMIT_EN : 0));
	}
	if (qi->tqi_readyTime && (qi->tqi_type != ATH9K_TX_QUEUE_CAB)) {
		REG_WRITE(ah, AR_QRDYTIMECFG(q),
		    SM(qi->tqi_readyTime, AR_Q_RDYTIMECFG_DURATION) |
		    AR_Q_RDYTIMECFG_EN);
	}

	REG_WRITE(ah, AR_DCHNTIME(q),
	    SM(qi->tqi_burstTime, AR_D_CHNTIME_DUR) |
	    (qi->tqi_burstTime ? AR_D_CHNTIME_EN : 0));

	if (qi->tqi_burstTime &&
	    (qi->tqi_qflags & TXQ_FLAG_RDYTIME_EXP_POLICY_ENABLE)) {
		REG_WRITE(ah, AR_QMISC(q),
		    REG_READ(ah, AR_QMISC(q)) |
		    AR_Q_MISC_RDYTIME_EXP_POLICY);

	}

	if (qi->tqi_qflags & TXQ_FLAG_BACKOFF_DISABLE) {
		REG_WRITE(ah, AR_DMISC(q),
		    REG_READ(ah, AR_DMISC(q)) |
		    AR_D_MISC_POST_FR_BKOFF_DIS);
	}
	if (qi->tqi_qflags & TXQ_FLAG_FRAG_BURST_BACKOFF_ENABLE) {
		REG_WRITE(ah, AR_DMISC(q),
		    REG_READ(ah, AR_DMISC(q)) |
		    AR_D_MISC_FRAG_BKOFF_EN);
	}
	switch (qi->tqi_type) {
	case ATH9K_TX_QUEUE_BEACON:
		REG_WRITE(ah, AR_QMISC(q), REG_READ(ah, AR_QMISC(q)) |
		    AR_Q_MISC_FSP_DBA_GATED |
		    AR_Q_MISC_BEACON_USE |
		    AR_Q_MISC_CBR_INCR_DIS1);

		REG_WRITE(ah, AR_DMISC(q), REG_READ(ah, AR_DMISC(q)) |
		    (AR_D_MISC_ARB_LOCKOUT_CNTRL_GLOBAL <<
		    AR_D_MISC_ARB_LOCKOUT_CNTRL_S) |
		    AR_D_MISC_BEACON_USE |
		    AR_D_MISC_POST_FR_BKOFF_DIS);
		break;
	case ATH9K_TX_QUEUE_CAB:
		REG_WRITE(ah, AR_QMISC(q), REG_READ(ah, AR_QMISC(q)) |
		    AR_Q_MISC_FSP_DBA_GATED |
		    AR_Q_MISC_CBR_INCR_DIS1 |
		    AR_Q_MISC_CBR_INCR_DIS0);
		value = (qi->tqi_readyTime -
		    (ah->ah_config.sw_beacon_response_time -
		    ah->ah_config.dma_beacon_response_time) -
		    ah->ah_config.additional_swba_backoff) * 1024;
		REG_WRITE(ah, AR_QRDYTIMECFG(q),
		    value | AR_Q_RDYTIMECFG_EN);
		REG_WRITE(ah, AR_DMISC(q), REG_READ(ah, AR_DMISC(q)) |
		    (AR_D_MISC_ARB_LOCKOUT_CNTRL_GLOBAL <<
		    AR_D_MISC_ARB_LOCKOUT_CNTRL_S));
		break;
	case ATH9K_TX_QUEUE_PSPOLL:
		REG_WRITE(ah, AR_QMISC(q),
		    REG_READ(ah, AR_QMISC(q)) | AR_Q_MISC_CBR_INCR_DIS1);
		break;
	case ATH9K_TX_QUEUE_UAPSD:
		REG_WRITE(ah, AR_DMISC(q), REG_READ(ah, AR_DMISC(q)) |
		    AR_D_MISC_POST_FR_BKOFF_DIS);
		break;
	default:
		break;
	}

	if (qi->tqi_intFlags & ATH9K_TXQ_USE_LOCKOUT_BKOFF_DIS) {
		REG_WRITE(ah, AR_DMISC(q), REG_READ(ah, AR_DMISC(q)) |
		    SM(AR_D_MISC_ARB_LOCKOUT_CNTRL_GLOBAL,
		    AR_D_MISC_ARB_LOCKOUT_CNTRL) |
		    AR_D_MISC_POST_FR_BKOFF_DIS);
	}

	if (qi->tqi_qflags & TXQ_FLAG_TXOKINT_ENABLE)
		ahp->ah_txOkInterruptMask |= 1 << q;
	else
		ahp->ah_txOkInterruptMask &= ~(1 << q);
	if (qi->tqi_qflags & TXQ_FLAG_TXERRINT_ENABLE)
		ahp->ah_txErrInterruptMask |= 1 << q;
	else
		ahp->ah_txErrInterruptMask &= ~(1 << q);
	if (qi->tqi_qflags & TXQ_FLAG_TXDESCINT_ENABLE)
		ahp->ah_txDescInterruptMask |= 1 << q;
	else
		ahp->ah_txDescInterruptMask &= ~(1 << q);
	if (qi->tqi_qflags & TXQ_FLAG_TXEOLINT_ENABLE)
		ahp->ah_txEolInterruptMask |= 1 << q;
	else
		ahp->ah_txEolInterruptMask &= ~(1 << q);
	if (qi->tqi_qflags & TXQ_FLAG_TXURNINT_ENABLE)
		ahp->ah_txUrnInterruptMask |= 1 << q;
	else
		ahp->ah_txUrnInterruptMask &= ~(1 << q);
	ath9k_hw_set_txq_interrupts(ah, qi);

	return (B_TRUE);
}

/* ARGSUSED */
int
ath9k_hw_rxprocdesc(struct ath_hal *ah, struct ath_desc *ds,
    uint32_t pa,
    struct ath_desc *nds,
    uint64_t tsf)
{
	struct ar5416_desc ads;
	struct ar5416_desc *adsp = AR5416DESC(ds);
	uint32_t phyerr;

	if ((adsp->ds_rxstatus8 & AR_RxDone) == 0)
		return (EINPROGRESS);

	ads.u.rx = adsp->u.rx;

	ds->ds_rxstat.rs_status = 0;
	ds->ds_rxstat.rs_flags = 0;

	ds->ds_rxstat.rs_datalen = ads.ds_rxstatus1 & AR_DataLen;
	ds->ds_rxstat.rs_tstamp = ads.AR_RcvTimestamp;

	ds->ds_rxstat.rs_rssi = MS(ads.ds_rxstatus4, AR_RxRSSICombined);
	ds->ds_rxstat.rs_rssi_ctl0 = MS(ads.ds_rxstatus0, AR_RxRSSIAnt00);
	ds->ds_rxstat.rs_rssi_ctl1 = MS(ads.ds_rxstatus0, AR_RxRSSIAnt01);
	ds->ds_rxstat.rs_rssi_ctl2 = MS(ads.ds_rxstatus0, AR_RxRSSIAnt02);
	ds->ds_rxstat.rs_rssi_ext0 = MS(ads.ds_rxstatus4, AR_RxRSSIAnt10);
	ds->ds_rxstat.rs_rssi_ext1 = MS(ads.ds_rxstatus4, AR_RxRSSIAnt11);
	ds->ds_rxstat.rs_rssi_ext2 = MS(ads.ds_rxstatus4, AR_RxRSSIAnt12);
	if (ads.ds_rxstatus8 & AR_RxKeyIdxValid)
		ds->ds_rxstat.rs_keyix = MS(ads.ds_rxstatus8, AR_KeyIdx);
	else
		ds->ds_rxstat.rs_keyix = ATH9K_RXKEYIX_INVALID;

	ds->ds_rxstat.rs_rate = RXSTATUS_RATE(ah, (&ads));
	ds->ds_rxstat.rs_more = (ads.ds_rxstatus1 & AR_RxMore) ? 1 : 0;

	ds->ds_rxstat.rs_isaggr = (ads.ds_rxstatus8 & AR_RxAggr) ? 1 : 0;
	ds->ds_rxstat.rs_moreaggr =
	    (ads.ds_rxstatus8 & AR_RxMoreAggr) ? 1 : 0;
	ds->ds_rxstat.rs_antenna = MS(ads.ds_rxstatus3, AR_RxAntenna);
	ds->ds_rxstat.rs_flags =
	    (ads.ds_rxstatus3 & AR_GI) ? ATH9K_RX_GI : 0;
	ds->ds_rxstat.rs_flags |=
	    (ads.ds_rxstatus3 & AR_2040) ? ATH9K_RX_2040 : 0;

	if (ads.ds_rxstatus8 & AR_PreDelimCRCErr)
		ds->ds_rxstat.rs_flags |= ATH9K_RX_DELIM_CRC_PRE;
	if (ads.ds_rxstatus8 & AR_PostDelimCRCErr)
		ds->ds_rxstat.rs_flags |= ATH9K_RX_DELIM_CRC_POST;
	if (ads.ds_rxstatus8 & AR_DecryptBusyErr)
		ds->ds_rxstat.rs_flags |= ATH9K_RX_DECRYPT_BUSY;

	if ((ads.ds_rxstatus8 & AR_RxFrameOK) == 0) {
		if (ads.ds_rxstatus8 & AR_CRCErr)
			ds->ds_rxstat.rs_status |= ATH9K_RXERR_CRC;
		else if (ads.ds_rxstatus8 & AR_PHYErr) {
			ds->ds_rxstat.rs_status |= ATH9K_RXERR_PHY;
			phyerr = MS(ads.ds_rxstatus8, AR_PHYErrCode);
			ds->ds_rxstat.rs_phyerr = (uint8_t)phyerr; /* LINT */
		} else if (ads.ds_rxstatus8 & AR_DecryptCRCErr)
			ds->ds_rxstat.rs_status |= ATH9K_RXERR_DECRYPT;
		else if (ads.ds_rxstatus8 & AR_MichaelErr)
			ds->ds_rxstat.rs_status |= ATH9K_RXERR_MIC;
	}

	return (0);
}

boolean_t
ath9k_hw_setuprxdesc(struct ath_hal *ah, struct ath_desc *ds,
    uint32_t size, uint32_t flags)
{
	struct ar5416_desc *ads = AR5416DESC(ds);
	struct ath9k_hw_capabilities *pCap = &ah->ah_caps;

	ads->ds_ctl1 = size & AR_BufLen;
	if (flags & ATH9K_RXDESC_INTREQ)
		ads->ds_ctl1 |= AR_RxIntrReq;

	ads->ds_rxstatus8 &= ~AR_RxDone;
	if (!(pCap->hw_caps & ATH9K_HW_CAP_AUTOSLEEP))
		(void) memset(&(ads->u), 0, sizeof (ads->u));

	return (B_TRUE);
}

boolean_t
ath9k_hw_setrxabort(struct ath_hal *ah, boolean_t set)
{
	uint32_t reg;

	if (set) {
		REG_SET_BIT(ah, AR_DIAG_SW,
		    (AR_DIAG_RX_DIS | AR_DIAG_RX_ABORT));

		if (!ath9k_hw_wait(ah, AR_OBS_BUS_1,
		    AR_OBS_BUS_1_RX_STATE, 0)) {
			REG_CLR_BIT(ah, AR_DIAG_SW,
			    (AR_DIAG_RX_DIS | AR_DIAG_RX_ABORT));

			reg = REG_READ(ah, AR_OBS_BUS_1);

			ARN_DBG((ARN_DBG_FATAL,
			    "%s: rx failed to go idle in 10 ms RXSM=0x%x\n",
			    __func__, reg));

			return (B_FALSE);
		}
	} else {
		REG_CLR_BIT(ah, AR_DIAG_SW,
		    (AR_DIAG_RX_DIS | AR_DIAG_RX_ABORT));
	}

	return (B_TRUE);
}

void
ath9k_hw_putrxbuf(struct ath_hal *ah, uint32_t rxdp)
{
	REG_WRITE(ah, AR_RXDP, rxdp);
}

void
ath9k_hw_rxena(struct ath_hal *ah)
{
	REG_WRITE(ah, AR_CR, AR_CR_RXE);
}

void
ath9k_hw_startpcureceive(struct ath_hal *ah)
{
	ath9k_enable_mib_counters(ah);

	ath9k_ani_reset(ah);

	REG_CLR_BIT(ah, AR_DIAG_SW, (AR_DIAG_RX_DIS | AR_DIAG_RX_ABORT));
}

void
ath9k_hw_stoppcurecv(struct ath_hal *ah)
{
	REG_SET_BIT(ah, AR_DIAG_SW, AR_DIAG_RX_DIS);

	ath9k_hw_disable_mib_counters(ah);
}

boolean_t
ath9k_hw_stopdmarecv(struct ath_hal *ah)
{
	REG_WRITE(ah, AR_CR, AR_CR_RXD);

	if (!ath9k_hw_wait(ah, AR_CR, AR_CR_RXE, 0)) {
		ARN_DBG((ARN_DBG_QUEUE, "arn: ath9k_hw_stopdmarecv(): "
		    "dma failed to stop in 10ms\n"
		    "AR_CR=0x%08x\nAR_DIAG_SW=0x%08x\n",
		    REG_READ(ah, AR_CR), REG_READ(ah, AR_DIAG_SW)));
		return (B_FALSE);
	} else {
		return (B_TRUE);
	}
}
