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

/*
 * This code has been largely lifted from the 2.8 code for the NFSv2/3 acache.
 * acache was improved in Bugfix 4774462, but corresponding change was not made
 * to acache4 of NFSv4. So, I leave here the Bugfix comments and the lookup of
 * nacache_ent, so that this code continues to work even after the 5.10 version
 * of this Bug is fixed. However, ideally, after this bug is fixed, this file
 * should ideally be made to look somewhat like the 5.10 atable.c with
 * (s/acache/acache4)
 *
 * (We can use CTF here, but 2.8 doesn't have CTF and who wants to code twice.)
 */

typedef struct acache4_walk_data
{
	/* ac_table = local copy of acache4 table */
	acache4_hash_t *ac_table;
	int ac_sizeof_acache4_hash;	/* sizeof(acache4_hash_t) */
	/*
	 * This has changed from long to int with Bugfix 4774462, but since
	 * sizeof (long) >= sizeof(int), we ignore this. Changing this to int
	 * will break the module for cores with this bugfix not installed
	 */
	long ac_table_sz;  /* ac_table_sz = `acache4size' */
	int ac_ndx;	   /* ac_ndx is the current+1 index into the ac_table */
	uintptr_t ac_ap;   /* ac_ap is the current acache4_t pointer */
	uintptr_t ac_hp;   /* ac_hp is the current acache4_hash_t pointer */
} acache4_walk_data_t;

int
acache4_walk_init(mdb_walk_state_t *wsp)
{
	acache4_walk_data_t *ac;
	uintptr_t acache4;
	int sizeof_acache4_hash = 2 * sizeof (void *);
	GElf_Sym sym;

	/* see comment above regarding the long/int problem */
	long sz;

	/*
	 * Bug 4774462 changes the data type of acache4size and adds a rwlock to
	 * the acache4_hash_t structure. The increase in size of the
	 * acache4_hash_t is important, because this code needs to read an array
	 * of acache4_hash_t. This bug also removes a symbol nacache4_ent, so we
	 * use the lookup this symbol to determine whether the core has the
	 * patch with this bug fix installed.
	 *
	 * So, inside this piece of code, strictly avoid using sizeof operator
	 * to find the size of acache4_hash_t and the [ ] operator to reference
	 * elements of acache4_hash_t array;
	 *
	 */
	if (mdb_lookup_by_name("nacache4_ent", &sym) == -1) {
		/* The bug fix has been installed in the core */
		sizeof_acache4_hash += sizeof (krwlock_t);
	}

	if (mdb_readvar(&sz, "acache4size") == -1) {
		mdb_warn("failed to read acache4size\n");
		return (WALK_ERR);
	}
	if (mdb_readvar(&acache4, "acache4") == -1) {
		mdb_warn("failed to read acache4\n");
		return (WALK_ERR);
	}

	ac = mdb_alloc(sizeof (acache4_walk_data_t), UM_SLEEP);
	ac->ac_table = mdb_alloc(sz * sizeof_acache4_hash, UM_SLEEP);
	if (mdb_vread(ac->ac_table, sz * sizeof_acache4_hash, acache4) == -1) {
		mdb_warn("failed to read acache4_hash_t array at %p\n",
				acache4);
		return (WALK_ERR);
	}

	ac->ac_sizeof_acache4_hash = sizeof_acache4_hash;
	ac->ac_table_sz = sz;
	ac->ac_ndx = 1;
	ac->ac_hp = acache4;
	ac->ac_ap = (uintptr_t)ac->ac_table[0].next;

	wsp->walk_data = ac;
	return (WALK_NEXT);
}

int
acache4_walk_step(mdb_walk_state_t *wsp)
{
	acache4_walk_data_t *ac = wsp->walk_data;
	uintptr_t addr;
	acache4_t entry;

again:
	while (ac->ac_ap == ac->ac_hp && ac->ac_ndx < ac->ac_table_sz) {
		int offset = ac->ac_sizeof_acache4_hash * ac->ac_ndx;
		void *p = (char *)ac->ac_table + offset;
		acache4_hash_t *h = (acache4_hash_t *)p;

		ac->ac_ap = (uintptr_t)h->next;
		ac->ac_ndx++;
		ac->ac_hp += ac->ac_sizeof_acache4_hash;
	}

	if (ac->ac_ap == ac->ac_hp)
		return (WALK_DONE);

	if (mdb_vread(&entry, sizeof (entry), addr = ac->ac_ap) == -1) {
		mdb_warn("failed to read acache4 entry at %p in bucket %p\n",
				ac->ac_ap, ac->ac_hp);
		ac->ac_ap = ac->ac_hp;
		goto again;
	}

	ac->ac_ap = (uintptr_t)entry.next;
	return (wsp->walk_callback(addr, &entry, wsp->walk_cbdata));
}

void
acache4_walk_fini(mdb_walk_state_t *wsp)
{
	acache4_walk_data_t *ac = wsp->walk_data;

	mdb_free(ac->ac_table, ac->ac_table_sz * ac->ac_sizeof_acache4_hash);
	mdb_free(ac, sizeof (acache4_walk_data_t));
}

int
ac4_rnode_walk_init(mdb_walk_state_t *wsp)
{
	rnode4_t rn;
	if (mdb_vread(&rn, sizeof (rn), wsp->walk_addr) == -1) {
		mdb_warn("failed to read rnode4 at %p\n", wsp->walk_addr);
		return (DCMD_ERR);
	}
	wsp->walk_addr = (uintptr_t)rn.r_acache;
	return (WALK_NEXT);
}
int
ac4_rnode_walk_step(mdb_walk_state_t *wsp)
{
	acache4_t ac;
	int status;

	if (wsp->walk_addr == NULL)
		return (WALK_DONE);
	if (mdb_vread(&ac, sizeof (ac), wsp->walk_addr) == -1) {
		mdb_warn("failed to read acache4_t at %p\n", wsp->walk_addr);
		return (WALK_ERR);
	}
	status = wsp->walk_callback(wsp->walk_addr, wsp->walk_data,
				wsp->walk_cbdata);
	wsp->walk_addr = (uintptr_t)ac.list;
	return (status);
}
/* ARGSUSED */
void
ac4_rnode_walk_fini(mdb_walk_state_t *wsp)
{
}
