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
 * Vegas congestion control algorithm.
 *
 * This implementation is based on the paper:
 * TCP Vegas: End to End Congestion Avoidance on a Global Internet
 * by Lawerence S. Brakmo and Larry L. Peterson
 *
 * Notes:
 *
 * 1. In congestion avoidance phase, Reno increases cwnd once per RTT.
 * By contrast, Vegas will either increase or decrease cwnd, based on
 * RTT samples. As segments get acked (without congestion), we record
 * the minimum RTT value we observe. This value is used to calculate
 * the expected sending rate, in segments:
 *
 *	expected = cwnd / min_RTT
 *
 * Once per RTT, a segment is chosen to calculate the actual sending rate:
 *
 *	actual = cwnd / RTT
 *
 * The difference will indicate whether we need to slow down or speed up:
 *
 *	diff = expected - actual (diff is always >=0 by definition)
 *
 *	if (diff < alpha)
 *		cwnd += 1 segment
 *	else if (diff > beta)
 *		cwnd -= 1 segment
 *
 *  alpha and beta are lower and upper bound constants.
 *
 * 2. In slow start, the paper suggests doubling cwnd every other RTT.
 * This implementation  follows the Reno algorithm instead, doubling cwnd
 * on every RTT.
 *
 * 3. We do not modify retransmission as the paper suggests, as there are other
 * optimizations in Solaris that pretty much take care of that.
 */

typedef struct vegas_state_s {
	clock_t		min_rtt;
	uint32_t	alpha;
	uint32_t	beta;
} vegas_state_t;

mod_prop_info_t vegas_tcp_propinfo_tbl[] = {
	{ "_cong_vegas_alpha", MOD_PROTO_TCP,
	    mod_set_uint32, mod_get_uint32,
	    {1, 65536, 2}, {2} },

	{ "_cong_vegas_beta", MOD_PROTO_TCP,
	    mod_set_uint32, mod_get_uint32,
	    {1, 65536, 4}, {4} },

	{ NULL, 0, NULL, NULL, {0}, {0} }
};

mod_prop_info_t vegas_sctp_propinfo_tbl[] = {
	{ "_cong_vegas_alpha", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {1, 65536, 2}, {2} },

	{ "_cong_vegas_beta", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {1, 65536, 4}, {4} },

	{ NULL, 0, NULL, NULL, {0}, {0} }
};

#define	vegas_alpha(args)		((args)->ca_propinfo)[0].prop_cur_uval
#define	vegas_beta(args)		((args)->ca_propinfo)[1].prop_cur_uval

static mod_prop_info_t *vegas_prop_info_alloc(uint_t);
static void	vegas_prop_info_free(mod_prop_info_t *, uint_t);
static size_t	vegas_state_size(int);
static void	vegas_state_init(tcpcong_args_t *);
static void	vegas_state_fini(tcpcong_args_t *);
static void	vegas_ack(tcpcong_args_t *);
static void	vegas_loss(tcpcong_args_t *);
static void	vegas_rto(tcpcong_args_t *);
static void	vegas_congestion(tcpcong_args_t *);
static void	vegas_after_idle(tcpcong_args_t *);

tcpcong_ops_t tcpcong_vegas_ops = {
	TCPCONG_VERSION,
	"vegas",
	TCPCONG_PROTO_TCP | TCPCONG_PROTO_SCTP,
	vegas_prop_info_alloc,
	vegas_prop_info_free,
	vegas_state_size,
	vegas_state_init,
	vegas_state_fini,
	vegas_ack,
	vegas_loss,
	vegas_rto,
	vegas_congestion,
	vegas_after_idle
};

extern struct mod_ops mod_tcpcongops;

static struct modltcpcong modltcpcong = {
	&mod_tcpcongops,
	"vegas congestion control module",
	&tcpcong_vegas_ops
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
vegas_prop_info_alloc(uint_t proto)
{
	mod_prop_info_t	*pinfo = NULL;

	if (proto == MOD_PROTO_TCP) {
		pinfo = kmem_zalloc(sizeof (vegas_tcp_propinfo_tbl), KM_SLEEP);
		bcopy(vegas_tcp_propinfo_tbl, pinfo,
		    sizeof (vegas_tcp_propinfo_tbl));
	} else if (proto == MOD_PROTO_SCTP) {
		pinfo = kmem_zalloc(sizeof (vegas_sctp_propinfo_tbl), KM_SLEEP);
		bcopy(vegas_sctp_propinfo_tbl, pinfo,
		    sizeof (vegas_sctp_propinfo_tbl));
	}
	return (pinfo);
}

static void
vegas_prop_info_free(mod_prop_info_t *pinfo, uint_t proto)
{
	if (proto == MOD_PROTO_TCP)
		kmem_free(pinfo, sizeof (vegas_tcp_propinfo_tbl));
	else if (proto == MOD_PROTO_SCTP)
		kmem_free(pinfo, sizeof (vegas_sctp_propinfo_tbl));
}

/* ARGSUSED */
static size_t
vegas_state_size(int flags)
{
	return (sizeof (vegas_state_t));
}

/* ARGSUSED */
static void
vegas_state_init(tcpcong_args_t *args)
{
	vegas_state_t	*sp = args->ca_state;

	sp->min_rtt = LONG_MAX;
	sp->alpha = vegas_alpha(args);
	sp->beta = vegas_beta(args);
}

/* ARGSUSED */
static void
vegas_state_fini(tcpcong_args_t *args)
{
}

static void
vegas_ack(tcpcong_args_t *args)
{
	vegas_state_t	*sp = args->ca_state;
	uint32_t	cwnd = *args->ca_cwnd;
	uint32_t	mss = *args->ca_mss;
	uint32_t	cwnd_segs = cwnd / mss;
	uint32_t	rtt = (*args->ca_srtt) >> 3;
	int32_t		bytes_acked = *args->ca_bytes_acked;
	uint32_t	expected, actual, diff;

	if (sp->min_rtt > rtt)
		sp->min_rtt = max(rtt, 1);

	/*
	 * Taking delayed ACK into account, but should not increase by
	 * more than 2*MSS even if more than 2*MSS bytes are acked.
	 */
	bytes_acked = MIN(MSS_ROUNDUP(bytes_acked, mss), mss << 1);

	/* Check if we are in slow start or congestion avoidance. */
	if (cwnd >= *args->ca_ssthresh) {
		/*
		 * Congestion avoidance.
		 */
		if (*args->ca_cwnd_cnt > 0) {
			*args->ca_cwnd_cnt -= bytes_acked;
			return;
		}
		/* RTT has elapsed, time to reevaluate cwnd */
		expected = cwnd_segs / sp->min_rtt;
		actual = cwnd_segs / max(rtt, 1);
		diff = expected - actual;
		if (diff < sp->alpha)
			*args->ca_cwnd = MIN(cwnd + mss, *args->ca_cwnd_max);
		else if (diff > sp->beta)
			*args->ca_cwnd = MIN(cwnd - mss, *args->ca_cwnd_max);
		*args->ca_cwnd_cnt = *args->ca_cwnd;
	} else {
		/* Slow start, increase cwnd by bytes_acked for every ACKs. */
		*args->ca_cwnd = MIN(cwnd + bytes_acked, *args->ca_cwnd_max);
	}
}

static void
vegas_loss(tcpcong_args_t *args)
{
	vegas_state_t	*sp = args->ca_state;
	uint32_t	mss = *args->ca_mss;
	uint32_t	npkt = *args->ca_flight_size / mss;

	*args->ca_ssthresh = MAX(npkt, 2) * mss;
	*args->ca_cwnd = MAX(npkt + *args->ca_dupack_cnt, 2) * mss;
	*args->ca_cwnd_cnt = 0;
	sp->min_rtt = LONG_MAX;
}

static void
vegas_rto(tcpcong_args_t *args)
{
	vegas_state_t	*sp = args->ca_state;
	uint32_t	mss = *args->ca_mss;
	uint32_t	new_cwnd = *args->ca_cwnd / 2;

	*args->ca_ssthresh = MAX(2 * mss, MSS_ROUNDUP(new_cwnd, mss));
	*args->ca_cwnd = mss;
	*args->ca_cwnd_cnt = 0;
	sp->min_rtt = LONG_MAX;
}

static void
vegas_congestion(tcpcong_args_t *args)
{
	vegas_state_t	*sp = args->ca_state;
	uint32_t	mss = *args->ca_mss;
	uint32_t	new_cwnd = *args->ca_cwnd / 2;
	uint32_t	npkt = (*args->ca_flight_size >> 1) / mss;

	*args->ca_ssthresh = MAX(2 * mss, MSS_ROUNDUP(new_cwnd, mss));
	*args->ca_cwnd = npkt * mss;
	*args->ca_cwnd_cnt = 0;
	sp->min_rtt = LONG_MAX;
}

/* ARGSUSED */
static void
vegas_after_idle(tcpcong_args_t *args)
{
	vegas_state_t	*sp = args->ca_state;

	sp->min_rtt = LONG_MAX;
}
