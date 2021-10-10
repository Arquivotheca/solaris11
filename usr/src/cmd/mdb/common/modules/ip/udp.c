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

/*
 * UDP network stack walker stepping function.
 */
int
udp_stacks_walk_step(mdb_walk_state_t *wsp)
{
	return (ns_walk_step(wsp, NS_UDP));
}

void
udphdr_print(struct udphdr *udph)
{
	in_port_t	sport, dport;
	uint16_t	hlen;

	mdb_printf("%<b>UDP header%</b>\n");

	mdb_nhconvert(&sport, &udph->uh_sport, sizeof (sport));
	mdb_nhconvert(&dport, &udph->uh_dport, sizeof (dport));
	mdb_nhconvert(&hlen, &udph->uh_ulen, sizeof (hlen));

	mdb_printf("%<u>%14s %14s %5s %6s%</u>\n",
	    "SPORT", "DPORT", "LEN", "CSUM");
	mdb_printf("%5hu (0x%04x) %5hu (0x%04x) %5hu 0x%04hx\n\n", sport, sport,
	    dport, dport, hlen, udph->uh_sum);
}

/* ARGSUSED */
int
udphdr(uintptr_t addr, uint_t flags, int ac, const mdb_arg_t *av)
{
	struct udphdr	udph;

	if (!(flags & DCMD_ADDRSPEC))
		return (DCMD_USAGE);

	if (mdb_vread(&udph, sizeof (udph), addr) == -1) {
		mdb_warn("failed to read UDP header at %p", addr);
		return (DCMD_ERR);
	}
	udphdr_print(&udph);
	return (DCMD_OK);
}
