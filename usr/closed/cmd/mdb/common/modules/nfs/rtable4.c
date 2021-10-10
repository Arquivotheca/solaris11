/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2004 Sun Microsystems, Inc.
 * All rights reserved.  Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "nfs_mdb.h"
#include "nfs4_mdb.h"

typedef struct rtable4_walk_data
{
	r4hashq_t *rt_table;		/* rt_table = local copy of rtable4 */
	int rt_table_sz;  /* rt_table_sz = `rtable4size' */
	int rt_ndx;	   /* rt_ndx is the current+1 index into the rt_table */
	uintptr_t rt_rp;   /* rt_rp is the current rnode4_t pointer */
	uintptr_t rt_hp;   /* rt_hp is the current r4hashq_t pointer */
} rtable4_walk_data_t;

int
rtbl4_walk_init(mdb_walk_state_t *wsp)
{
	rtable4_walk_data_t *rt;
	uintptr_t rtable4;
	int sz;

	if (mdb_readvar(&sz, "rtable4size") == -1) {
		mdb_warn("failed to read rtable4size\n");
		return (WALK_ERR);
	}
	if (mdb_readvar(&rtable4, "rtable4") == -1) {
		mdb_warn("failed to read rtable4\n");
		return (WALK_ERR);
	}

	rt = mdb_alloc(sizeof (rtable4_walk_data_t), UM_SLEEP);
	rt->rt_table = mdb_alloc(sz * sizeof (r4hashq_t), UM_SLEEP);
	if (mdb_vread(rt->rt_table, sz * sizeof (r4hashq_t), rtable4) == -1) {
		mdb_warn("failed to read r4hashq_t array at %p\n",
				rtable4);
		return (WALK_ERR);
	}

	rt->rt_table_sz = sz;
	rt->rt_ndx = 1;
	rt->rt_hp = rtable4;
	rt->rt_rp = (uintptr_t)rt->rt_table[0].r_hashf;

	wsp->walk_data = rt;
	return (WALK_NEXT);
}

int
rtbl4_walk_step(mdb_walk_state_t *wsp)
{
	rtable4_walk_data_t *rt = wsp->walk_data;
	uintptr_t addr;
	rnode4_t entry;

again:
	while (rt->rt_rp == rt->rt_hp && rt->rt_ndx < rt->rt_table_sz) {
		int offset = sizeof (r4hashq_t) * rt->rt_ndx;
		void *p = (char *)rt->rt_table + offset;
		r4hashq_t *h = (r4hashq_t *)p;

		rt->rt_rp = (uintptr_t)h->r_hashf;
		rt->rt_ndx++;
		rt->rt_hp += sizeof (r4hashq_t);
	}

	if (rt->rt_rp == rt->rt_hp)
		return (WALK_DONE);

	if (mdb_vread(&entry, sizeof (entry), addr = rt->rt_rp) == -1) {
		mdb_warn("failed to read rnode4 entry at %p in bucket %p\n",
				rt->rt_rp, rt->rt_hp);
		rt->rt_rp = rt->rt_hp;
		goto again;
	}

	rt->rt_rp = (uintptr_t)entry.r_hashf;
	return (wsp->walk_callback(addr, &entry, wsp->walk_cbdata));
}

void
rtbl4_walk_fini(mdb_walk_state_t *wsp)
{
	rtable4_walk_data_t *rt = wsp->walk_data;

	mdb_free(rt->rt_table, rt->rt_table_sz * sizeof (r4hashq_t));
	mdb_free(rt, sizeof (rtable4_walk_data_t));
}
