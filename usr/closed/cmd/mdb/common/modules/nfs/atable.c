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

typedef struct acache_walk_data
{
	acache_hash_t *ac_table;	/* ac_table = local copy of acache */
	long ac_table_sz;  /* ac_table_sz = `acachesize' */
	int ac_ndx;	   /* ac_ndx is the current+1 index into the ac_table */
	uintptr_t ac_ap;   /* ac_ap is the current acache_t pointer */
	uintptr_t ac_hp;   /* ac_hp is the current acache_hash_t pointer */
} acache_walk_data_t;

int
acache_walk_init(mdb_walk_state_t *wsp)
{
	acache_walk_data_t *ac;
	uintptr_t acache;
	int sz;

	if (mdb_readvar(&sz, "acachesize") == -1) {
		mdb_warn("failed to read acachesize\n");
		return (WALK_ERR);
	}
	if (mdb_readvar(&acache, "acache") == -1) {
		mdb_warn("failed to read acache\n");
		return (WALK_ERR);
	}

	ac = mdb_alloc(sizeof (acache_walk_data_t), UM_SLEEP);
	ac->ac_table = mdb_alloc(sz * sizeof (acache_hash_t), UM_SLEEP);
	if (mdb_vread(ac->ac_table, sz * sizeof (acache_hash_t),
				acache)	== -1) {
		mdb_warn("failed to read acache_hash_t array at %p\n",
				acache);
		return (WALK_ERR);
	}

	ac->ac_table_sz = sz;
	ac->ac_ndx = 1;
	ac->ac_hp = acache;
	ac->ac_ap = (uintptr_t)ac->ac_table[0].next;

	wsp->walk_data = ac;
	return (WALK_NEXT);
}

int
acache_walk_step(mdb_walk_state_t *wsp)
{
	acache_walk_data_t *ac = wsp->walk_data;
	uintptr_t addr;
	acache_t entry;

again:
	while (ac->ac_ap == ac->ac_hp && ac->ac_ndx < ac->ac_table_sz) {
		int offset = sizeof (acache_hash_t) * ac->ac_ndx;
		void *p = (char *)ac->ac_table + offset;
		acache_hash_t *h = (acache_hash_t *)p;

		ac->ac_ap = (uintptr_t)h->next;
		ac->ac_ndx++;
		ac->ac_hp += sizeof (acache_hash_t);
	}

	if (ac->ac_ap == ac->ac_hp)
		return (WALK_DONE);

	if (mdb_vread(&entry, sizeof (entry), addr = ac->ac_ap) == -1) {
		mdb_warn("failed to read acache entry at %p in bucket %p\n",
				ac->ac_ap, ac->ac_hp);
		ac->ac_ap = ac->ac_hp;
		goto again;
	}

	ac->ac_ap = (uintptr_t)entry.next;
	return (wsp->walk_callback(addr, &entry, wsp->walk_cbdata));
}

void
acache_walk_fini(mdb_walk_state_t *wsp)
{
	acache_walk_data_t *ac = wsp->walk_data;

	mdb_free(ac->ac_table, ac->ac_table_sz * sizeof (acache_hash_t));
	mdb_free(ac, sizeof (acache_walk_data_t));
}

int
ac_rnode_walk_init(mdb_walk_state_t *wsp)
{
	rnode_t rn;
	if (mdb_vread(&rn, sizeof (rnode_t), wsp->walk_addr) == -1) {
		mdb_warn("failed to read rnode at %p\n", wsp->walk_addr);
		return (DCMD_ERR);
	}
	wsp->walk_addr = (uintptr_t)rn.r_acache;
	return (WALK_NEXT);
}
int
ac_rnode_walk_step(mdb_walk_state_t *wsp)
{
	acache_t ac;
	int status;

	if (wsp->walk_addr == NULL)
		return (WALK_DONE);
	if (mdb_vread(&ac, sizeof (ac), wsp->walk_addr) == -1) {
		mdb_warn("failed to read acache_t at %p\n", wsp->walk_addr);
		return (WALK_ERR);
	}
	status = wsp->walk_callback(wsp->walk_addr, wsp->walk_data,
				wsp->walk_cbdata);
	wsp->walk_addr = (uintptr_t)ac.list;
	return (status);
}
/*ARGSUSED*/
void
ac_rnode_walk_fini(mdb_walk_state_t *wsp)
{
}
