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

/*
 * NewReno congestion control algorithm.
 */

size_t	newreno_state_size(int);
void	newreno_state_init(tcpcong_args_t *);
void	newreno_state_fini(tcpcong_args_t *);
void	newreno_ack(tcpcong_args_t *);
void	newreno_loss(tcpcong_args_t *);
void	newreno_rto(tcpcong_args_t *);
void	newreno_congestion(tcpcong_args_t *);
void	newreno_after_idle(tcpcong_args_t *);

tcpcong_ops_t tcpcong_newreno_ops = {
	TCPCONG_VERSION,
	"newreno",
	TCPCONG_PROTO_TCP | TCPCONG_PROTO_SCTP,
	NULL,	/* co_prop_info_alloc */
	NULL,	/* co_prop_info_free */
	newreno_state_size,
	newreno_state_init,
	newreno_state_fini,
	newreno_ack,
	newreno_loss,
	newreno_rto,
	newreno_congestion,
	newreno_after_idle
};

/*
 * newreno is different from other tcpcong modules in that
 * it is built into the kernel, rather than being a loadable module.
 * newreno_ddi_g_init/destroy are here instead of the usual _init/_fini.
 */
void
newreno_ddi_g_init()
{
	int	ret;

	ret = tcpcong_mod_register(&tcpcong_newreno_ops);
	ASSERT(ret == 0);
}

void
newreno_ddi_g_destroy()
{
	int	ret;

	ret = tcpcong_mod_unregister(&tcpcong_newreno_ops);
	ASSERT(ret == 0);
}

/* ARGSUSED */
size_t
newreno_state_size(int flags)
{
	return (0);
}

/* ARGSUSED */
void
newreno_state_init(tcpcong_args_t *args)
{
}

/* ARGSUSED */
void
newreno_state_fini(tcpcong_args_t *args)
{
}

void
newreno_ack(tcpcong_args_t *args)
{
	uint32_t	cwnd = *args->ca_cwnd;
	uint32_t	mss = *args->ca_mss;

	if (cwnd >= *args->ca_ssthresh) {
		/*
		 * Congestion avoidance: linear increase, 1 MSS per RTT.
		 *
		 * cwnd_cnt is used to prevent an increase of less than 1 MSS.
		 * With partial increase, we may send out tinygrams
		 * in order to preserve mblk boundaries.
		 */
		if (*args->ca_cwnd_cnt > 0) {
			if (args->ca_flags & TCPCONG_PROTO_SCTP)
				*args->ca_cwnd_cnt -= *args->ca_bytes_acked;
			else
				*args->ca_cwnd_cnt -= mss;
		} else {
			cwnd += mss;
			*args->ca_cwnd = MIN(cwnd, *args->ca_cwnd_max);
			*args->ca_cwnd_cnt = *args->ca_cwnd;
		}
	} else {
		/* Slow start: exponential increase, 1 MSS per ACK. */
		if (args->ca_flags & TCPCONG_PROTO_SCTP)
			cwnd += MIN(mss, *args->ca_bytes_acked);
		else
			cwnd += mss;
		*args->ca_cwnd = MIN(cwnd, *args->ca_cwnd_max);
	}
}

void
newreno_loss(tcpcong_args_t *args)
{
	uint32_t	mss = *args->ca_mss;
	uint32_t	npkt;

	if (args->ca_flags & TCPCONG_PROTO_TCP) {
		npkt = (*args->ca_flight_size >> 1) / mss;
		*args->ca_ssthresh = MAX(npkt, 2) * mss;
		*args->ca_cwnd = (npkt + *args->ca_dupack_cnt) * mss;
	} else {
		npkt = (*args->ca_cwnd >> 1) / mss;
		*args->ca_ssthresh = MAX(npkt, 2) * mss;
		*args->ca_cwnd = *args->ca_ssthresh;
	}
	*args->ca_cwnd_cnt = 0;
}

void
newreno_rto(tcpcong_args_t *args)
{
	uint32_t	mss = *args->ca_mss;
	uint32_t	npkt;

	if (args->ca_flags & TCPCONG_PROTO_TCP)
		npkt = (*args->ca_flight_size >> 1) / mss;
	else
		npkt = (*args->ca_cwnd >> 1) / mss;
	*args->ca_ssthresh = MAX(npkt, 2) * mss;
	*args->ca_cwnd = mss;
	*args->ca_cwnd_cnt = 0;
}

void
newreno_congestion(tcpcong_args_t *args)
{
	newreno_loss(args);
}

/* ARGSUSED */
void
newreno_after_idle(tcpcong_args_t *args)
{
}
