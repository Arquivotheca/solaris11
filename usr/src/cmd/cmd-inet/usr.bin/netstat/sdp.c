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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/socket.h>
#include <kstat.h>
#include <assert.h>
#include "../../../../uts/common/inet/sdp_itf.h"
#include "pr_val.h"

extern char *pr_ap();
extern void fail(int, char *, ...);
extern boolean_t family_selected(int);

#define	SDP_STAT(sdp_stat, field) \
    sdp_stat->field = sdp_stat_get_value(ksp, #field)

#define	PRINT_SDP_STAT_DIFFS(field) \
	val = sdp_new_stat->field; \
	if (sdp_prev_stat != NULL) \
		val = sdp_new_stat->field - sdp_prev_stat->field; \
	prval64(#field, val)

typedef struct sdp_netstat_s
{
	uint64_t sdpActiveOpens;
	uint64_t sdpCurrEstab;
	uint64_t sdpInDataBytes;
	uint64_t sdpInSegs;
	uint64_t sdpOutDataBytes;
	uint64_t sdpOutSegs;
	uint64_t sdpPrFails;
	uint64_t sdpRejects;
} sdp_netstat_t;

static uint64_t
sdp_stat_get_value(kstat_t *ksp, char *name)
{
	kstat_named_t *ks;

	ks = kstat_data_lookup(ksp, name);
	if (ks != NULL)
		return (ks->value.ui64);
	return (0);
}

static void *
print_sdp_proto_stats(kstat_t *ksp, sdp_netstat_t *sdp_prev_stat)
{
	sdp_netstat_t *sdp_new_stat;
	uint64_t val;

	sdp_new_stat = (sdp_netstat_t *)malloc(sizeof (sdp_netstat_t));

	if (sdp_new_stat == NULL) {
		fail(0, "sdp stats no memory\n");
	}

	(void) fputs("\nSDP", stdout);

	SDP_STAT(sdp_new_stat, sdpActiveOpens);
	SDP_STAT(sdp_new_stat, sdpCurrEstab);
	SDP_STAT(sdp_new_stat, sdpInDataBytes);
	SDP_STAT(sdp_new_stat, sdpInSegs);
	SDP_STAT(sdp_new_stat, sdpOutDataBytes);
	SDP_STAT(sdp_new_stat, sdpOutSegs);
	SDP_STAT(sdp_new_stat, sdpPrFails);
	SDP_STAT(sdp_new_stat, sdpRejects);

	prval_init();
	PRINT_SDP_STAT_DIFFS(sdpActiveOpens);
	PRINT_SDP_STAT_DIFFS(sdpCurrEstab);
	PRINT_SDP_STAT_DIFFS(sdpPrFails);
	PRINT_SDP_STAT_DIFFS(sdpRejects);
	PRINT_SDP_STAT_DIFFS(sdpInSegs);
	prval_end();
	prval_init();
	PRINT_SDP_STAT_DIFFS(sdpOutSegs);
	prval_end();
	prval_init();
	PRINT_SDP_STAT_DIFFS(sdpInDataBytes);
	prval_end();
	prval_init();
	PRINT_SDP_STAT_DIFFS(sdpOutDataBytes);
	prval_end();
	prval_init();
	prval_end();

	if (sdp_prev_stat != NULL)
		free(sdp_prev_stat);
	return (sdp_new_stat);
}

static void *
print_sdp_conn_stats(kstat_t *ksp, boolean_t Aflag)
{
	int		i;
	sdp_connect_info_t  *infop;

	infop = ksp->ks_data;
	(void) printf("\nActive SDP sockets\n");

	/* for each sdp_sockinfo structure, display what we need: */
	(void) printf("%*s%*s%*s%*s%*s%*s%*s%*s%*s%*s%*s\n",
	    -28, "Local Address", -28, "Remote Address", -8, "State",
	    -11, "RxBPending", -10, "TxBQueued", -10, "TxBPosted",
	    -11, "LAdvtSz", -11, "RAdvtSz", -11, "LAdvtBuff", -11, "RAdvtBuff",
	    -10, "LPostBuff");
	for (i = 0; i < ksp->ks_ndata; i++) {
		char buf[1024];
		extern char *pr_ap6();

		if (!Aflag && strcmp("LST", infop->sci_state) == 0)
			goto skip;
		if ((family_selected(AF_INET) ||
		    family_selected(AF_INET_SDP)) &&
		    (infop->sci_family == AF_INET ||
		    infop->sci_family == AF_INET_SDP)) {
			(void) printf("%-28s", pr_ap(infop->sci_laddr[0],
			    (uint_t)ntohs(infop->sci_lport), "tcp", buf, 1024));
			(void) printf("%-28s", pr_ap(infop->sci_faddr[0],
			    (uint_t)ntohs(infop->sci_fport), "tcp", buf, 1024));
		} else if ((family_selected(AF_INET6) &&
		    infop->sci_family == AF_INET6)) {
			(void) printf("%-28s",
			    pr_ap6((const in6_addr_t *)&infop->sci_laddr,
			    (uint_t)ntohs(infop->sci_lport), "tcp", buf, 1024));
			(void) printf("%-28s",
			    pr_ap6((const in6_addr_t *)&infop->sci_faddr,
			    (uint_t)ntohs(infop->sci_fport), "tcp", buf, 1024));
		} else {
			goto skip;
		}
		(void) printf("%-8s", infop->sci_state);
		(void) printf("%-10d ", infop->sci_recv_bytes_pending);
		(void) printf("%-9d ", infop->sci_tx_bytes_queued);
		(void) printf("%-9d ",
		    infop->sci_tx_bytes_queued -infop->sci_tx_bytes_unposted);
		(void) printf("%-10d ", infop->sci_lbuff_size);
		(void) printf("%-10d ", infop->sci_rbuff_size);
		(void) printf("%-10d ", infop->sci_lbuff_advt);
		(void) printf("%-10d ", infop->sci_rbuff_advt);
		(void) printf("%-19d ", infop->sci_lbuff_posted);

		(void) printf("\n");
skip:
		/*
		 * increment pointer based on the current size of the struct
		 * This will allow automatic size changes
		 */
		/* LINTED */
		infop = (sdp_connect_info_t *)
		    ((char *)infop + infop->sci_size);
	}
	return (NULL);
}

void *
sdp_pr_stats(kstat_ctl_t *kc, boolean_t proto_stats, void *sdp_prev_stat,
    boolean_t Aflag)
{
	kstat_t	*ksp;

	if (kc == NULL) {
		fail(0, "sdp_pr_stats: No kstat");
	}

	/* find the sdp kstat: */
	if ((ksp = kstat_lookup(kc, SDPIB_STR_NAME, 0, proto_stats ?
	    "sdpstat" : "sdp_conn_stats")) == (kstat_t *)NULL) {
		/*
		 * sdpib module has been unloaded
		 */
		return (sdp_prev_stat);
	}

	if (kstat_read(kc, ksp, NULL) == -1) {
		fail(0, "kstat_read failed for sdpconn \n");
	}

	if (ksp->ks_ndata == 0) {
		return (sdp_prev_stat);	/* no SDP sockets found	*/
	}

	assert(ksp->ks_data != NULL);

	return (proto_stats ? print_sdp_proto_stats(ksp, sdp_prev_stat):
	    print_sdp_conn_stats(ksp, Aflag));
}
