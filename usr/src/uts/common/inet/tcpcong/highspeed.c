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
 * HighSpeed congestion control algorithm.
 *
 * The structure to represent the RFC 3649 (HighSpeed TCP) congestion window
 * increasing and decreasing factor.  The index to the array represents the
 * number of MSS to increase per RTT.  The cwnd_decr is the decreasing
 * factor at the same point.  cwnd_decr is left shifted by TCP_HS_DECR_FACTOR
 * so that when we calculate the cwnd reduction, we can multiply the current
 * cwnd with cwnd_decr and then right shifted the product by the same factor.
 * The cwnd_thres is the value of cwnd to determine whether we need to shift
 * to a lower or higher index point.
 */
struct hs_cwnd_chg {
	int		cwnd_decr;
	uint32_t	cwnd_thres;
};
#define	HS_DECR_FACTOR	7

/*
 * Using the calculation in RFC 3649, a size of 74 corresponds to
 * a congestion window of 94717 * MSS bytes.  With MSS = 1460 bytes,
 * this is ~132MB.
 */
#define	HS_ARR_SIZE		74

/* Pre-calculated array using the method described in RFC 3649. */
static struct hs_cwnd_chg hs_arr[HS_ARR_SIZE] = {
	{ 0, 0 }, { 64, 38 }, { 71, 118 }, { 75, 221 },
	{ 78, 347 }, { 81, 495 }, { 83, 663 }, { 84, 851 },
	{ 86, 1058 }, { 87, 1284 }, { 88, 1529 }, { 89, 1793 },
	{ 90, 2076 }, { 91, 2378 }, { 92, 2699 }, { 93, 3039 },
	{ 93, 3399 }, { 94, 3778 }, { 95, 4177 }, { 95, 4596 },
	{ 96, 5036 }, { 97, 5497 }, { 97, 5979 }, { 98, 6483 },
	{ 98, 7009 }, { 99, 7558 }, { 99, 8130 }, { 100, 8726 },
	{ 100, 9346 }, { 101, 9991 }, { 101, 10661 }, { 101, 11358 },
	{ 102, 12082 }, { 102, 12834 }, { 103, 13614 }, { 103, 14424 },
	{ 103, 15265 }, { 104, 16137 }, { 104, 17042 }, { 105, 17981 },
	{ 105, 18955 }, { 105, 19965 }, { 106, 21013 }, { 106, 22101 },
	{ 106, 23230 }, { 107, 24402 }, { 107, 25618 }, { 107, 26881 },
	{ 108, 28193 }, { 108, 29557 }, { 108, 30975 }, { 108, 32450 },
	{ 109, 33986 }, { 109, 35586 }, { 109, 37253 }, { 110, 38992 },
	{ 110, 40808 }, { 110, 42707 }, { 111, 44694 }, { 111, 46776 },
	{ 111, 48961 }, { 111, 51258 }, { 112, 53677 }, { 112, 56230 },
	{ 112, 58932 }, { 113, 61799 }, { 113, 64851 }, { 113, 68113 },
	{ 114, 71617 }, { 114, 75401 }, { 114, 79517 }, { 115, 84035 },
	{ 115, 89053 }, { 116, 94717 }
};

typedef struct highspeed_state_s {
	int	cur_aw;		/* current index to cwnd adjustment array */
} highspeed_state_t;

static size_t	highspeed_state_size(int);
static void	highspeed_state_init(tcpcong_args_t *);
static void	highspeed_state_fini(tcpcong_args_t *);
static void	highspeed_ack(tcpcong_args_t *);
static void	highspeed_loss(tcpcong_args_t *);
static void	highspeed_rto(tcpcong_args_t *);
static void	highspeed_congestion(tcpcong_args_t *);
static void	highspeed_after_idle(tcpcong_args_t *);

tcpcong_ops_t tcpcong_highspeed_ops = {
	TCPCONG_VERSION,
	"highspeed",
	TCPCONG_PROTO_TCP | TCPCONG_PROTO_SCTP,
	NULL,	/* co_prop_info_alloc */
	NULL,	/* co_prop_info_free */
	highspeed_state_size,
	highspeed_state_init,
	highspeed_state_fini,
	highspeed_ack,
	highspeed_loss,
	highspeed_rto,
	highspeed_congestion,
	highspeed_after_idle
};

extern struct mod_ops mod_tcpcongops;

static struct modltcpcong modltcpcong = {
	&mod_tcpcongops,
	"highspeed congestion control module",
	&tcpcong_highspeed_ops
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

/* ARGSUSED */
static size_t
highspeed_state_size(int flags)
{
	return (sizeof (highspeed_state_t));
}

static void
highspeed_state_init(tcpcong_args_t *args)
{
	highspeed_state_t	*sp = args->ca_state;

	sp->cur_aw = 1;
}

/* ARGSUSED */
static void
highspeed_state_fini(tcpcong_args_t *args)
{
}

static void
highspeed_ack(tcpcong_args_t *args)
{
	highspeed_state_t *sp = args->ca_state;
	uint32_t	cwnd = *args->ca_cwnd;
	uint32_t	new_cwnd;
	uint32_t	mss = *args->ca_mss;
	int32_t		bytes_acked = *args->ca_bytes_acked;
	int		i;

	/*
	 * Taking delayed ACK into account, but should not increase by
	 * more than 2*MSS even if more than 2*MSS bytes are acked.
	 */
	bytes_acked = MIN(MSS_ROUNDUP(bytes_acked, mss), 2 * mss);

	if (cwnd >= *args->ca_ssthresh) {
		/*
		 * Congestion avoidance.
		 *
		 * cwnd_cnt is used to prevent an increase of less than 1 MSS.
		 * With partial increase, we may send out tinygrams
		 * in order to preserve mblk boundaries.
		 *
		 * By setting cwnd_cnt to new cwnd/i and decrementing it
		 * by bytes_acked (rounded up and cap at 2*MSS) for every ACK,
		 * cwnd is increased by i*MSS per RTT.
		 */
		if (*args->ca_cwnd_cnt > 0) {
			*args->ca_cwnd_cnt -= bytes_acked;
		} else {
			i = sp->cur_aw;
			new_cwnd = MIN(cwnd + mss, *args->ca_cwnd_max);
			*args->ca_cwnd = new_cwnd;
			*args->ca_cwnd_cnt = new_cwnd / i;
			if (new_cwnd > hs_arr[i].cwnd_thres * mss)
				sp->cur_aw = MIN(i + 1, HS_ARR_SIZE - 1);
		}
	} else {
		/* Slow start, increase cwnd by bytes_acked for every ACK. */
		*args->ca_cwnd = MIN(cwnd + bytes_acked, *args->ca_cwnd_max);
	}
}

static void
highspeed_loss_common(tcpcong_args_t *args)
{
	highspeed_state_t *sp = args->ca_state;
	uint32_t	mss = *args->ca_mss;
	uint32_t	new_cwnd;
	int		i = sp->cur_aw;

	new_cwnd = (*args->ca_flight_size * hs_arr[i].cwnd_decr) >>
	    HS_DECR_FACTOR;

	/* Adjust the current increment index */
	for (; i > 1; i--) {
		if (new_cwnd > hs_arr[i].cwnd_thres * mss) {
			break;
		}
	}
	sp->cur_aw = MAX(i, 1);

	*args->ca_cwnd = MAX(new_cwnd, 2 * mss);
	*args->ca_cwnd_cnt = 0;
}

static void
highspeed_loss(tcpcong_args_t *args)
{
	uint32_t	mss = *args->ca_mss;

	highspeed_loss_common(args);
	*args->ca_ssthresh = MAX(*args->ca_flight_size / mss, 2) * mss;
}

static void
highspeed_rto(tcpcong_args_t *args)
{
	uint32_t	mss = *args->ca_mss;

	highspeed_loss_common(args);
	*args->ca_ssthresh = MAX(2 * mss, MSS_ROUNDUP(*args->ca_cwnd, mss));
}

static void
highspeed_congestion(tcpcong_args_t *args)
{
	highspeed_rto(args);
}

/* ARGSUSED */
static void
highspeed_after_idle(tcpcong_args_t *args)
{
}
