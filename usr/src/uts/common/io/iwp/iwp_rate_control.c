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
#include "iwp_rate_control.h"


void
iwp_amrr_init(iwp_amrr_t *amrr)
{
	amrr->success = 0;
	amrr->recovery = 0;
	amrr->txcnt = amrr->retrycnt = 0;
	amrr->success_threshold = IWP_AMRR_MIN_SUCCESS_THRESHOLD;
	amrr->ht_mcs_idx = 0;   /* 6Mbps */
}


void
iwp_start_rate_control(iwp_sc_t *sc)
{
	ieee80211com_t *ic = &sc->sc_ic;
	ieee80211_node_t *in = ic->ic_bss;
	iwp_amrr_t *amrr;
	int i;
	uint8_t r;

	if ((in->in_flags & IEEE80211_NODE_HT) &&
	    (sc->sc_chip_param->ht_conf->ht_support) &&
	    (in->in_htrates.rs_nrates > 0) &&
	    (in->in_htrates.rs_nrates <= IEEE80211_HTRATE_MAXSIZE)) {
		amrr = (iwp_amrr_t *)in;

		for (i = in->in_htrates.rs_nrates - 1; i > 0; i--) {

			r = in->in_htrates.rs_rates[i] &
			    IEEE80211_RATE_VAL;
			if ((r != 0) && (r <= 0xd) &&
			    (sc->sc_chip_param->ht_conf->tx_support_mcs[r/8] &
			    (1 << (r%8)))) {
				amrr->ht_mcs_idx = r;
				atomic_or_32(&sc->sc_flags,
				    IWP_F_RATE_AUTO_CTL);
				break;
			}
		}
	} else {
		if (IEEE80211_FIXED_RATE_NONE == ic->ic_fixed_rate) {
			atomic_or_32(&sc->sc_flags,
			    IWP_F_RATE_AUTO_CTL);
			/*
			 * set rate to some reasonable initial value
			 */
			i = in->in_rates.ir_nrates - 1;
			while (i > 0 && IEEE80211_RATE(i) > 72) {
				i--;
			}
			in->in_txrate = i;
		} else {
			atomic_and_32(&sc->sc_flags,
			    ~IWP_F_RATE_AUTO_CTL);
		}
	}

}

static int
iwp_is_max_rate(ieee80211_node_t *in)
{
	int i;
	iwp_amrr_t *amrr = (iwp_amrr_t *)in;
	uint8_t r = (uint8_t)amrr->ht_mcs_idx;
	ieee80211com_t *ic = in->in_ic;
	iwp_sc_t *sc = (iwp_sc_t *)ic;

	if ((sc->sc_chip_param->ht_conf->ht_support) &&
	    (in->in_flags & IEEE80211_NODE_HT)) {
		for (i = in->in_htrates.rs_nrates - 1; i >= 0; i--) {
			r = in->in_htrates.rs_rates[i] &
			    IEEE80211_RATE_VAL;
			if (sc->sc_chip_param->ht_conf->tx_support_mcs[r/8] &
			    (1 << (r%8))) {
				break;
			}
		}
		return (r == (uint8_t)amrr->ht_mcs_idx);
	} else {
		return (is_max_rate(in));
	}
}

static int
iwp_is_min_rate(ieee80211_node_t *in)
{
	int i;
	uint8_t r = 0;
	iwp_amrr_t *amrr = (iwp_amrr_t *)in;
	ieee80211com_t *ic = in->in_ic;
	iwp_sc_t *sc = (iwp_sc_t *)ic;

	if ((sc->sc_chip_param->ht_conf->ht_support) &&
	    (in->in_flags & IEEE80211_NODE_HT)) {
		for (i = 0; i < in->in_htrates.rs_nrates; i++) {
			r = in->in_htrates.rs_rates[i] &
			    IEEE80211_RATE_VAL;
			if (sc->sc_chip_param->ht_conf->tx_support_mcs[r/8] &
			    (1 << (r%8))) {
				break;
			}
		}
		return (r == (uint8_t)amrr->ht_mcs_idx);
	} else {
		return (is_min_rate(in));
	}
}

static void
iwp_increase_rate(ieee80211_node_t *in)
{
	int i;
	uint8_t r;
	iwp_amrr_t *amrr = (iwp_amrr_t *)in;
	ieee80211com_t *ic = in->in_ic;
	iwp_sc_t *sc = (iwp_sc_t *)ic;

	if ((sc->sc_chip_param->ht_conf->ht_support) &&
	    (in->in_flags & IEEE80211_NODE_HT)) {
again:
		amrr->ht_mcs_idx++;

		for (i = 0; i < in->in_htrates.rs_nrates; i++) {
			r = in->in_htrates.rs_rates[i] &
			    IEEE80211_RATE_VAL;
			if ((r == (uint8_t)amrr->ht_mcs_idx) &&
			    (sc->sc_chip_param->ht_conf->tx_support_mcs[r/8] &
			    (1 << (r%8)))) {
				break;
			}
		}

		if (i >= in->in_htrates.rs_nrates) {
			goto again;
		}
	} else {
		increase_rate(in);
	}
}

static void
iwp_decrease_rate(ieee80211_node_t *in)
{
	int i;
	uint8_t r;
	iwp_amrr_t *amrr = (iwp_amrr_t *)in;
	ieee80211com_t *ic = in->in_ic;
	iwp_sc_t *sc = (iwp_sc_t *)ic;

	if ((sc->sc_chip_param->ht_conf->ht_support) &&
	    (in->in_flags & IEEE80211_NODE_HT)) {
again:
		amrr->ht_mcs_idx--;

		for (i = 0; i < in->in_htrates.rs_nrates; i++) {
			r = in->in_htrates.rs_rates[i] &
			    IEEE80211_RATE_VAL;
			if ((r == (uint8_t)amrr->ht_mcs_idx) &&
			    (sc->sc_chip_param->ht_conf->tx_support_mcs[r/8] &
			    (1 << (r%8)))) {
				break;
			}
		}

		if (i >= in->in_htrates.rs_nrates) {
			goto again;
		}
	} else {
		decrease_rate(in);
	}
}

/* ARGSUSED */
static void
iwp_amrr_ratectl(void *arg, ieee80211_node_t *in)
{
	iwp_amrr_t *amrr = (iwp_amrr_t *)in;
	int need_change = 0;

	if (is_success(amrr) && is_enough(amrr)) {
		amrr->success++;
		if (amrr->success >= amrr->success_threshold &&
		    !iwp_is_max_rate(in)) {
			amrr->recovery = 1;
			amrr->success = 0;
			iwp_increase_rate(in);
			IWP_DBG((IWP_DEBUG_RATECTL, "iwp_amrr_ratectl(): "
			    "AMRR increasing rate %d "
			    "(txcnt=%d retrycnt=%d)\n",
			    in->in_txrate, amrr->txcnt,
			    amrr->retrycnt));
			need_change = 1;
		} else {
			amrr->recovery = 0;
		}
	} else if (not_very_few(amrr) && is_failure(amrr)) {
		amrr->success = 0;
		if (!iwp_is_min_rate(in)) {
			if (amrr->recovery) {
				amrr->success_threshold++;
				if (amrr->success_threshold >
				    IWP_AMRR_MAX_SUCCESS_THRESHOLD) {
					amrr->success_threshold =
					    IWP_AMRR_MAX_SUCCESS_THRESHOLD;
				}
			} else {
				amrr->success_threshold =
				    IWP_AMRR_MIN_SUCCESS_THRESHOLD;
			}
			iwp_decrease_rate(in);
			IWP_DBG((IWP_DEBUG_RATECTL, "iwp_amrr_ratectl(): "
			    "AMRR decreasing rate %d "
			    "(txcnt=%d retrycnt=%d)\n",
			    in->in_txrate, amrr->txcnt,
			    amrr->retrycnt));
			need_change = 1;
		}
		amrr->recovery = 0;	/* paper is incorrect */
	}

	if (is_enough(amrr) || need_change) {
		reset_cnt(amrr);
	}
}

void
iwp_amrr_timeout(iwp_sc_t *sc)
{
	ieee80211com_t *ic = &sc->sc_ic;

	IWP_DBG((IWP_DEBUG_RATECTL, "iwp_amrr_timeout(): "
	    "enter\n"));

	if (IEEE80211_M_STA == ic->ic_opmode) {
		iwp_amrr_ratectl(NULL, ic->ic_bss);
	} else {
		ieee80211_iterate_nodes(&ic->ic_sta, iwp_amrr_ratectl, NULL);
	}

	sc->sc_clk = ddi_get_lbolt();
}
