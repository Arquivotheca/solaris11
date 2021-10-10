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


#ifndef _IWP_RATE_CONTROL_H
#define	_IWP_RATE_CONTROL_H

typedef struct iwp_amrr {
	ieee80211_node_t	in;
	uint32_t	txcnt;
	uint32_t	retrycnt;
	uint32_t	success;
	uint32_t	success_threshold;
	int		recovery;
	volatile uint32_t	ht_mcs_idx;
} iwp_amrr_t;

/*
 * Naive implementation of the Adaptive Multi Rate Retry algorithm:
 * "IEEE 802.11 Rate Adaptation: A Practical Approach"
 * Mathieu Lacage, Hossein Manshaei, Thierry Turletti
 * INRIA Sophia - Projet Planete
 * http://www-sop.inria.fr/rapports/sophia/RR-5208.html
 */
#define	is_success(amrr)	\
	((amrr)->retrycnt < (amrr)->txcnt / 10)
#define	is_failure(amrr)	\
	((amrr)->retrycnt > (amrr)->txcnt / 3)
#define	is_enough(amrr)		\
	((amrr)->txcnt > 200)
#define	not_very_few(amrr)	\
	((amrr)->txcnt > 40)
#define	is_min_rate(in)		\
	(0 == (in)->in_txrate)
#define	is_max_rate(in)		\
	((in)->in_rates.ir_nrates - 1 == (in)->in_txrate)
#define	increase_rate(in)	\
	((in)->in_txrate++)
#define	decrease_rate(in)	\
	((in)->in_txrate--)
#define	reset_cnt(amrr)		\
	{ (amrr)->txcnt = (amrr)->retrycnt = 0; }

#define	IWP_AMRR_MIN_SUCCESS_THRESHOLD   1
#define	IWP_AMRR_MAX_SUCCESS_THRESHOLD  15


#endif /* _IWP_RATE_CONTROL_H */
