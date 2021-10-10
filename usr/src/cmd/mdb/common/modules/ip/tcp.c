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

#include "common.h"

static const mdb_bitmask_t tcp_flags[] = {
	{ "SYN",	TH_SYN,		TH_SYN	},
	{ "ACK",	TH_ACK,		TH_ACK	},
	{ "FIN",	TH_FIN,		TH_FIN	},
	{ "RST",	TH_RST,		TH_RST	},
	{ "PSH",	TH_PUSH,	TH_PUSH	},
	{ "ECE",	TH_ECE,		TH_ECE	},
	{ "CWR",	TH_CWR,		TH_CWR	},
	{ NULL,		0,		0	}
};

/* TCP option length */
#define	TCPOPT_HEADER_LEN	2
#define	TCPOPT_MAXSEG_LEN	4
#define	TCPOPT_WS_LEN		3
#define	TCPOPT_TSTAMP_LEN	10
#define	TCPOPT_SACK_OK_LEN	2

/*
 * TCP network stack walker stepping function.
 */
int
tcp_stacks_walk_step(mdb_walk_state_t *wsp)
{
	return (ns_walk_step(wsp, NS_TCP));
}

/*
 * Initialization function for the per CPU TCP stats counter walker of a given
 * TCP stack.
 */
int
tcps_sc_walk_init(mdb_walk_state_t *wsp)
{
	tcp_stack_t tcps;

	if (wsp->walk_addr == NULL)
		return (WALK_ERR);

	if (mdb_vread(&tcps, sizeof (tcps), wsp->walk_addr) == -1) {
		mdb_warn("failed to read tcp_stack_t at %p", wsp->walk_addr);
		return (WALK_ERR);
	}
	if (tcps.tcps_sc_cnt == 0)
		return (WALK_DONE);

	/*
	 * Store the tcp_stack_t pointer in walk_data.  The stepping function
	 * used it to calculate if the end of the counter has reached.
	 */
	wsp->walk_data = (void *)wsp->walk_addr;
	wsp->walk_addr = (uintptr_t)tcps.tcps_sc;
	return (WALK_NEXT);
}

/*
 * Stepping function for the per CPU TCP stats counterwalker.
 */
int
tcps_sc_walk_step(mdb_walk_state_t *wsp)
{
	int status;
	tcp_stack_t tcps;
	tcp_stats_cpu_t *stats;
	char *next, *end;

	if (mdb_vread(&tcps, sizeof (tcps), (uintptr_t)wsp->walk_data) == -1) {
		mdb_warn("failed to read tcp_stack_t at %p", wsp->walk_addr);
		return (WALK_ERR);
	}
	if (mdb_vread(&stats, sizeof (stats), wsp->walk_addr) == -1) {
		mdb_warn("failed ot read tcp_stats_cpu_t at %p",
		    wsp->walk_addr);
		return (WALK_ERR);
	}
	status = wsp->walk_callback((uintptr_t)stats, &stats, wsp->walk_cbdata);
	if (status != WALK_NEXT)
		return (status);

	next = (char *)wsp->walk_addr + sizeof (tcp_stats_cpu_t *);
	end = (char *)tcps.tcps_sc + tcps.tcps_sc_cnt *
	    sizeof (tcp_stats_cpu_t *);
	if (next >= end)
		return (WALK_DONE);
	wsp->walk_addr = (uintptr_t)next;
	return (WALK_NEXT);
}

static void
tcphdr_print_options(uint8_t *opts, uint32_t opts_len)
{
	uint8_t *endp;
	uint32_t len, val;

	mdb_printf("%<b>Options:%</b>");
	endp = opts + opts_len;
	while (opts < endp) {
		len = endp - opts;
		switch (*opts) {
		case TCPOPT_EOL:
			mdb_printf(" EOL");
			opts++;
			break;

		case TCPOPT_NOP:
			mdb_printf(" NOP");
			opts++;
			break;

		case TCPOPT_MAXSEG: {
			uint16_t mss;

			if (len < TCPOPT_MAXSEG_LEN ||
			    opts[1] != TCPOPT_MAXSEG_LEN) {
				mdb_printf(" <Truncated MSS>\n");
				return;
			}
			mdb_nhconvert(&mss, opts + TCPOPT_HEADER_LEN,
			    sizeof (mss));
			mdb_printf(" MSS=%u", mss);
			opts += TCPOPT_MAXSEG_LEN;
			break;
		}

		case TCPOPT_WSCALE:
			if (len < TCPOPT_WS_LEN || opts[1] != TCPOPT_WS_LEN) {
				mdb_printf(" <Truncated WS>\n");
				return;
			}
			mdb_printf(" WS=%u", opts[2]);
			opts += TCPOPT_WS_LEN;
			break;

		case TCPOPT_TSTAMP: {
			if (len < TCPOPT_TSTAMP_LEN ||
			    opts[1] != TCPOPT_TSTAMP_LEN) {
				mdb_printf(" <Truncated TS>\n");
				return;
			}

			opts += TCPOPT_HEADER_LEN;
			mdb_nhconvert(&val, opts, sizeof (val));
			mdb_printf(" TS_VAL=%u,", val);

			opts += sizeof (val);
			mdb_nhconvert(&val, opts, sizeof (val));
			mdb_printf("TS_ECHO=%u", val);

			opts += sizeof (val);
			break;
		}

		case TCPOPT_SACK_PERMITTED:
			if (len < TCPOPT_SACK_OK_LEN ||
			    opts[1] != TCPOPT_SACK_OK_LEN) {
				mdb_printf(" <Truncated SACK_OK>\n");
				return;
			}
			mdb_printf(" SACK_OK");
			opts += TCPOPT_SACK_OK_LEN;
			break;

		case TCPOPT_SACK: {
			uint32_t sack_len;

			if (len <= TCPOPT_HEADER_LEN || len < opts[1] ||
			    opts[1] <= TCPOPT_HEADER_LEN) {
				mdb_printf(" <Truncated SACK>\n");
				return;
			}
			sack_len = opts[1] - TCPOPT_HEADER_LEN;
			opts += TCPOPT_HEADER_LEN;

			mdb_printf(" SACK=");
			while (sack_len > 0) {
				if (opts + 2 * sizeof (val) > endp) {
					mdb_printf("<Truncated SACK>\n");
					opts = endp;
					break;
				}

				mdb_nhconvert(&val, opts, sizeof (val));
				mdb_printf("<%u,", val);
				opts += sizeof (val);
				mdb_nhconvert(&val, opts, sizeof (val));
				mdb_printf("%u>", val);
				opts += sizeof (val);

				sack_len -= 2 * sizeof (val);
			}
			break;
		}

		default:
			mdb_printf(" Opts=<val=%u,len=%u>", *opts,
			    opts[1]);
			opts += opts[1];
			break;
		}
	}
	mdb_printf("\n");
}

int
tcphdr_print(struct tcphdr *tcph, uintptr_t addr)
{
	in_port_t	sport, dport;
	tcp_seq		seq, ack;
	uint16_t	win, urp;
	uint32_t	opt_len;

	mdb_printf("%<b>TCP header%</b>\n");

	mdb_nhconvert(&sport, &tcph->th_sport, sizeof (sport));
	mdb_nhconvert(&dport, &tcph->th_dport, sizeof (dport));
	mdb_nhconvert(&seq, &tcph->th_seq, sizeof (seq));
	mdb_nhconvert(&ack, &tcph->th_ack, sizeof (ack));
	mdb_nhconvert(&win, &tcph->th_win, sizeof (win));
	mdb_nhconvert(&urp, &tcph->th_urp, sizeof (urp));

	mdb_printf("%<u>%6s %6s %10s %10s %4s %5s %5s %5s %-15s%</u>\n",
	    "SPORT", "DPORT", "SEQ", "ACK", "HLEN", "WIN", "CSUM", "URP",
	    "FLAGS");
	mdb_printf("%6hu %6hu %10u %10u %4d %5hu %5hu %5hu <%b>\n",
	    sport, dport, seq, ack, tcph->th_off << 2, win,
	    tcph->th_sum, urp, tcph->th_flags, tcp_flags);
	mdb_printf("0x%04x 0x%04x 0x%08x 0x%08x\n\n",
	    sport, dport, seq, ack);

	/* If there are options, print them out also. */
	opt_len = (tcph->th_off << 2) - TCP_MIN_HEADER_LENGTH;
	if (opt_len > 0) {
		uint8_t *opt_buf;

		opt_buf = mdb_alloc(opt_len, UM_SLEEP);
		if (mdb_vread(opt_buf, opt_len, addr + sizeof (*tcph)) == -1) {
			mdb_warn("failed to read TCP options at %p", addr +
			    sizeof (*tcph));
			return (DCMD_ERR);
		}
		tcphdr_print_options(opt_buf, opt_len);
		mdb_free(opt_buf, opt_len);
	}

	return (DCMD_OK);
}

/* ARGSUSED */
int
tcphdr(uintptr_t addr, uint_t flags, int ac, const mdb_arg_t *av)
{
	struct tcphdr	tcph;

	if (!(flags & DCMD_ADDRSPEC))
		return (DCMD_USAGE);

	if (mdb_vread(&tcph, sizeof (tcph), addr) == -1) {
		mdb_warn("failed to read TCP header at %p", addr);
		return (DCMD_ERR);
	}
	return (tcphdr_print(&tcph, addr));
}
