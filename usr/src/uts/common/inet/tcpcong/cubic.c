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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <inet/common.h>
#include <inet/ip.h>
#include <inet/tcp.h>
#include <inet/tcpcong.h>
#include <inet/tunables.h>

/*
 * CUBIC congestion control algorithm
 *
 * Per RFC draft-rhee-tcpm-cubic-02.
 *
 * cwnd growth is a cubic function:
 *
 *         cwnd ^
 *              |                          .
 *              |                         .
 *              |                        .
 *              |                      .
 *   W_last_max +           . . + . .
 *              |        .
 *              |      .
 *              |     .
 *              |    .
 * origin_point +    .
 *              |
 *              |
 *              -----+----------+-------------------->
 *           epoch_start        K                time
 *
 * W_last_max is the maximum cwnd size we've seen in previous loss episodes.
 * It represents the algorithm's notion of the available bandwidth and
 * that's where the cubic function will plateau.
 *
 * Upon next loss event, we save current cwnd as origin_point and reset
 * epoch_start to 0. We calculate K - the time it would take the concave
 * region to go from origin_point to W_last_max, provided no further loss.
 *
 * If cwnd outgrows W_last_max, that means the network conditions might have
 * changed, e.g. some flows have departed. We enter the convex region, its
 * purpose is to probe for more available bandwidth.
 *
 * Two additional features:
 *
 * 1. TCP friendliness: if within the same time period, NewReno would grow cwnd
 * faster than CUBIC, the NewReno increment is used.
 *
 * 2. Fast convergence: if we notice W_last_max reduction from previous loss
 * event, we interpret that as an indication of new flows joining the network,
 * and allow this flow to release more bandwidth by reducing W_last_max further.
 */

typedef struct cubic_state_s {
	tcpcong_handle_t newreno_hdl;
	tcpcong_ops_t	*newreno_ops;
	uint64_t	epoch_start;
	uint64_t	dMin;
	int32_t		cwnd_cnt;
	uint32_t	ack_cnt;
	uint32_t	W_last_max;
	uint32_t	W_tcp;
	uint32_t	origin_point;
	uint32_t	K;
	uint64_t	last_update;
	uint32_t	last_cwnd;
	uint32_t	beta;
	uint32_t	one_minus_beta;
	uint32_t	C;
	uint32_t	max_burst;
	uint32_t	update_interval;
	boolean_t	tcp_friendliness;
	uint32_t	tf_factor_a;
	uint32_t	tf_factor_b;
	boolean_t	fast_convergence;
	uint32_t	fc_factor;
} cubic_state_t;

/*
 * Right shift factor for cubic parameters beta and C.
 * 10 means that values need to be divided by 1024
 */
#define	CUBIC_SHIFT	10

/*
 * beta and C are parameters from the CUBIC paper.
 * Default values are 0.8 and 0.4, respectively.
 *
 * max_burst is the max number of segments cwnd can be increased per ACK
 *
 * update_interval is cwnd update interval in milliseconds
 *
 * tcp_friendliness enables/disables TCP friendliness feature.
 * reno_beta used in TCP friendliness is 0.5 by default.
 *
 * fast_convergence enables/disables fast convergence feature.
 * f/c factor will be calculated as (1+beta)/2, ~0.9 by default.
 */
mod_prop_info_t cubic_tcp_propinfo_tbl[] = {
	{ "_cong_cubic_beta", MOD_PROTO_TCP,
	    mod_set_uint32, mod_get_uint32,
	    {1, 65536, 820}, {820} },

	{ "_cong_cubic_C", MOD_PROTO_TCP,
	    mod_set_uint32, mod_get_uint32,
	    {1, 65536, 410}, {410} },

	{ "_cong_cubic_max_burst", MOD_PROTO_TCP,
	    mod_set_uint32, mod_get_uint32,
	    {1, 65536, 16}, {16} },

	{ "_cong_cubic_update_interval", MOD_PROTO_TCP,
	    mod_set_uint32, mod_get_uint32,
	    {1, 65536, 20}, {20} },

	{ "_cong_cubic_tcp_friendliness", MOD_PROTO_TCP,
	    mod_set_uint32, mod_get_uint32,
	    {0, 1, 1}, {1} },

	{ "_cong_cubic_reno_beta", MOD_PROTO_TCP,
	    mod_set_uint32, mod_get_uint32,
	    {1, 65536, 512}, {512} },

	{ "_cong_cubic_fast_convergence", MOD_PROTO_TCP,
	    mod_set_uint32, mod_get_uint32,
	    {0, 1, 1}, {1} },

	{ NULL, 0, NULL, NULL, {0}, {0} }
};

mod_prop_info_t cubic_sctp_propinfo_tbl[] = {
	{ "_cong_cubic_beta", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {1, 65536, 820}, {820} },

	{ "_cong_cubic_C", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {1, 65536, 410}, {410} },

	{ "_cong_cubic_max_burst", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {1, 65536, 16}, {16} },

	{ "_cong_cubic_update_interval", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {1, 65536, 20}, {20} },

	{ "_cong_cubic_tcp_friendliness", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {0, 1, 1}, {1} },

	{ "_cong_cubic_reno_beta", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {1, 65536, 512}, {512} },

	{ "_cong_cubic_fast_convergence", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {0, 1, 1}, {1} },

	{ NULL, 0, NULL, NULL, {0}, {0} }
};

#define	cubic_beta(args)		((args)->ca_propinfo)[0].prop_cur_uval
#define	cubic_C(args)			((args)->ca_propinfo)[1].prop_cur_uval
#define	cubic_max_burst(args)		((args)->ca_propinfo)[2].prop_cur_uval
#define	cubic_update_interval(args)	((args)->ca_propinfo)[3].prop_cur_uval
#define	cubic_tcp_friendliness(args)	((args)->ca_propinfo)[4].prop_cur_uval
#define	cubic_reno_beta(args)		((args)->ca_propinfo)[5].prop_cur_uval
#define	cubic_fast_convergence(args)	((args)->ca_propinfo)[6].prop_cur_uval

static mod_prop_info_t *cubic_prop_info_alloc(uint_t);
static void	cubic_prop_info_free(mod_prop_info_t *, uint_t);
static size_t	cubic_state_size(int);
static void	cubic_state_init(tcpcong_args_t *);
static void	cubic_state_fini(tcpcong_args_t *);
static void	cubic_ack(tcpcong_args_t *);
static void	cubic_loss(tcpcong_args_t *);
static void	cubic_rto(tcpcong_args_t *);
static void	cubic_congestion(tcpcong_args_t *);
static void	cubic_after_idle(tcpcong_args_t *);

tcpcong_ops_t tcpcong_cubic_ops = {
	TCPCONG_VERSION,
	"cubic",
	TCPCONG_PROTO_TCP | TCPCONG_PROTO_SCTP,
	cubic_prop_info_alloc,
	cubic_prop_info_free,
	cubic_state_size,
	cubic_state_init,
	cubic_state_fini,
	cubic_ack,
	cubic_loss,
	cubic_rto,
	cubic_congestion,
	cubic_after_idle
};

extern struct mod_ops mod_tcpcongops;

static struct modltcpcong modltcpcong = {
	&mod_tcpcongops,
	"cubic congestion control module",
	&tcpcong_cubic_ops
};

static struct modlinkage modlinkage = {
	MODREV_1,
	(void *)&modltcpcong,
	NULL
};

int
_init()
{
	return (mod_install(&modlinkage));
}

int
_fini()
{
	return (mod_remove(&modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

static mod_prop_info_t *
cubic_prop_info_alloc(uint_t proto)
{
	mod_prop_info_t	*pinfo = NULL;

	if (proto == MOD_PROTO_TCP) {
		pinfo = kmem_zalloc(sizeof (cubic_tcp_propinfo_tbl), KM_SLEEP);
		bcopy(cubic_tcp_propinfo_tbl, pinfo,
		    sizeof (cubic_tcp_propinfo_tbl));
	} else if (proto == MOD_PROTO_SCTP) {
		pinfo = kmem_zalloc(sizeof (cubic_sctp_propinfo_tbl), KM_SLEEP);
		bcopy(cubic_sctp_propinfo_tbl, pinfo,
		    sizeof (cubic_sctp_propinfo_tbl));
	}
	return (pinfo);
}

static void
cubic_prop_info_free(mod_prop_info_t *pinfo, uint_t proto)
{
	if (proto == MOD_PROTO_TCP)
		kmem_free(pinfo, sizeof (cubic_tcp_propinfo_tbl));
	else if (proto == MOD_PROTO_SCTP)
		kmem_free(pinfo, sizeof (cubic_sctp_propinfo_tbl));
}

/* ARGSUSED */
static size_t
cubic_state_size(int flags)
{
	return (sizeof (cubic_state_t));
}

static void
cubic_reset(tcpcong_args_t *args)
{
	cubic_state_t	*sp = args->ca_state;
	uint32_t	reno_beta;
	tcpcong_handle_t newreno_hdl;
	tcpcong_ops_t	*newreno_ops;

	newreno_hdl = sp->newreno_hdl;
	newreno_ops = sp->newreno_ops;

	bzero(sp, sizeof (cubic_state_t));

	sp->last_update = ddi_get_lbolt64();
	sp->beta = cubic_beta(args);
	sp->one_minus_beta = (1 << CUBIC_SHIFT) - sp->beta;
	sp->C = cubic_C(args);
	sp->max_burst = cubic_max_burst(args);
	sp->update_interval = MIN(1, MSEC_TO_TICK(cubic_update_interval(args)));
	sp->tcp_friendliness = (cubic_tcp_friendliness(args) != 0);
	reno_beta = cubic_reno_beta(args);
	sp->tf_factor_a = (1 << CUBIC_SHIFT) - reno_beta;
	sp->tf_factor_b = 3 * reno_beta / ((2 << CUBIC_SHIFT) - reno_beta);
	sp->fast_convergence = (cubic_fast_convergence(args) != 0);
	sp->fc_factor = ((1 << CUBIC_SHIFT) + sp->beta) / 2;

	sp->newreno_hdl = newreno_hdl;
	sp->newreno_ops = newreno_ops;
}

static void
cubic_state_init(tcpcong_args_t *args)
{
	cubic_state_t	*sp = args->ca_state;
	int		flags = args->ca_flags;

	cubic_reset(args);

	sp->newreno_hdl = tcpcong_lookup("newreno", &sp->newreno_ops);
	ASSERT(sp->newreno_hdl != NULL);
	ASSERT(sp->newreno_ops != NULL);
	ASSERT(sp->newreno_ops->co_state_size(flags) == 0);
}

/* ARGSUSED */
static void
cubic_state_fini(tcpcong_args_t *args)
{
	cubic_state_t	*sp = args->ca_state;

	if (sp->newreno_hdl != NULL) {
		tcpcong_unref(sp->newreno_hdl);
		sp->newreno_hdl = NULL;
		sp->newreno_ops = NULL;
	}
}

/*
 * Cube root calculation using Newton-Ralphson method.
 * We start with an initial guess from a precalculated table,
 * followed by N-R iterations:
 *
 * x_next = x - f(x) / f'(x),
 * where f(x) = x^3 - v, f'(x) = 3x^2
 * x_next = x - (x^3 - v) / 3x^2 = (2x + v / x^2) / 3
 */

/* table[i] = cbrt(1 << i) */
static uint32_t cbrt_newton_table[] = { 0,
	1,	2,	2,	2,	3,	4,	4,	6,
	7,	8,	11,	13,	16,	21,	26,	32,
	41,	51,	64,	81,	102,	128,	162,	204,
	256,	323,	407,	512,	646,	813,	1024,	1291,
	1626,	2048,	2581,	3251,	4096,	5161,	6502,	8192,
	10322,	13004,	16384,	20643,	26008,	32768,	41286,	52016,
	65536,	82571,	104032,	131072,	165141,	208064,	262144,	330281,
	416128,	524288,	660562,	832256,	1048576, 1321123, 1664511, 2097152
};

static uint32_t
cbrt(uint64_t v)
{
	uint64_t	x;

	if (v == 0)
		return (0);

	/* initial guess */
	x = (uint64_t)cbrt_newton_table[highbit((ulong_t)v)];

	/*
	 * Two Newton-Raphson iterations will give sufficient accuracy
	 * for this application (average error <= 0.31%).
	 */
	x = (2 * x + v / (x * x)) / 3;
	x = (2 * x + v / (x * x)) / 3;

	return ((uint32_t)x);
}

static uint32_t
cubic_update(tcpcong_args_t *args, uint64_t time_stamp)
{
	cubic_state_t	*sp = args->ca_state;
	uint32_t	mss = *args->ca_mss;
	uint32_t	cwnd = *args->ca_cwnd / mss;
	uint32_t	target, reno_target;
	uint64_t	t;

	sp->last_cwnd = *args->ca_cwnd;
	sp->last_update = time_stamp;
	sp->ack_cnt++;

	if (sp->epoch_start == 0) {
		sp->epoch_start = time_stamp;
		if (cwnd < sp->W_last_max) {
			sp->K = cbrt(
			    ((uint64_t)(sp->W_last_max - cwnd) <<
			    CUBIC_SHIFT) / sp->C) * 1000;
			sp->origin_point = sp->W_last_max;
		} else {
			sp->K = 0;
			sp->origin_point = cwnd;
		}
		sp->ack_cnt = 1;
		sp->W_tcp = cwnd;
	}

	t = TICK_TO_MSEC(time_stamp - sp->epoch_start) + sp->dMin;

	/*
	 * target = origin_point + C(t - K)^3
	 */
	if (t < sp->K) {
		t = sp->K - t;
		target = sp->origin_point -
		    ((t * t * t / 1000000000 * sp->C) >> CUBIC_SHIFT);
	} else {
		t = t - sp->K;
		target = sp->origin_point +
		    ((t * t * t / 1000000000 * sp->C) >> CUBIC_SHIFT);
	}

	/*
	 * TCP friendliness: if, within the same time period,
	 * (New)Reno would grow cwnd faster than CUBIC, then we are
	 * in the TCP mode and choose the faster growth factor.
	 *
	 * target = W_tcp*beta + 3*beta/(2-beta)*t/RTT
	 */
	if (sp->tcp_friendliness) {
		reno_target = ((sp->W_tcp * sp->tf_factor_a) >> CUBIC_SHIFT) +
		    ((sp->tf_factor_b * t / sp->dMin) >> CUBIC_SHIFT);
		if (reno_target > target)
			target = reno_target;
	}

	return (target);
}

static void
cubic_ack(tcpcong_args_t *args)
{
	cubic_state_t	*sp = args->ca_state;
	uint64_t	rtt = (*args->ca_srtt) >> 3;
	uint32_t	mss = *args->ca_mss;
	uint32_t	cwnd = *args->ca_cwnd;
	uint32_t	cwnd_seg = cwnd / mss;
	uint32_t	target, diff;
	uint64_t	now;

	sp->dMin = (sp->dMin > 0) ? MIN(sp->dMin, rtt) : rtt;
	if (sp->dMin == 0)
		sp->dMin = 1;

	/* fall back to NewReno for slow start or if RTT is too small */
	if (cwnd < *args->ca_ssthresh || rtt == 0) {
		sp->newreno_ops->co_ack(args);
		goto done;
	}

	now = ddi_get_lbolt64();
	if (sp->epoch_start == 0 ||
	    cwnd >= sp->cwnd_cnt ||
	    now - sp->last_update > sp->update_interval) {
		target = cubic_update(args, now);
		if (target > cwnd_seg) {
			diff = MAX(target - cwnd_seg, 1);
			if (diff > sp->max_burst)
				diff = sp->max_burst;
			diff *= mss;
			sp->cwnd_cnt = cwnd + diff;
			*args->ca_cwnd += diff;
		} else {
			sp->cwnd_cnt = cwnd;
		}
	} else {
		diff = sp->cwnd_cnt - cwnd;
		if (diff > sp->max_burst * mss)
			diff = sp->max_burst * mss;
		else
			diff = MSS_ROUNDUP(diff, mss);
		*args->ca_cwnd += diff;
	}

done:
	ASSERT(*args->ca_cwnd != 0);
}

static void
cubic_loss(tcpcong_args_t *args)
{
	cubic_state_t	*sp = args->ca_state;
	uint32_t	mss = *args->ca_mss;
	uint32_t	cwnd_segs = *args->ca_cwnd / mss;
	uint32_t	cwnd;

	sp->epoch_start = 0;

	if (cwnd_segs < sp->W_last_max && sp->fast_convergence)
		sp->W_last_max = (cwnd_segs * sp->fc_factor) >> CUBIC_SHIFT;
	else
		sp->W_last_max = cwnd_segs;

	cwnd = (*args->ca_flight_size * sp->one_minus_beta) >> CUBIC_SHIFT;
	*args->ca_ssthresh = *args->ca_cwnd = MAX(cwnd, 2 * mss);
}

static void
cubic_rto(tcpcong_args_t *args)
{
	cubic_state_t	*sp = args->ca_state;
	uint32_t	mss = *args->ca_mss;
	uint32_t	cwnd = *args->ca_cwnd;

	*args->ca_ssthresh = (cwnd * sp->beta) >> CUBIC_SHIFT;
	*args->ca_cwnd = mss;
	*args->ca_cwnd_cnt = 0;
}

static void
cubic_congestion(tcpcong_args_t *args)
{
	cubic_state_t	*sp = args->ca_state;
	uint32_t	mss = *args->ca_mss;
	uint32_t	cwnd = *args->ca_cwnd;
	uint32_t	npkt = (*args->ca_flight_size >> 1) / mss;

	*args->ca_ssthresh = (cwnd * sp->beta) >> CUBIC_SHIFT;
	*args->ca_cwnd = npkt * mss;
	*args->ca_cwnd_cnt = 0;
}

/* ARGSUSED */
static void
cubic_after_idle(tcpcong_args_t *args)
{
	cubic_reset(args);
}
