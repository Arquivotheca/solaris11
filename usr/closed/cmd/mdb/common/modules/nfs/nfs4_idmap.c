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
 * Copyright (c) 2004, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include "nfs_mdb.h"
#include "nfs4_mdb.h"
#include <nfs/nfs4_idmap_impl.h>

typedef struct idmap_walk_data {
	nfsidhq_t *id_table;	/* ac_table = local copy of acache */
	int id_ndx;	   /* ac_ndx is the current+1 index into the ac_table */
	uintptr_t id_ip;   /* ac_ap is the current acache_t pointer */
	uintptr_t id_hp;   /* ac_hp is the current acache_hash_t pointer */
} idmap_walk_data_t;


static int idmap_generic_init(mdb_walk_state_t *, ptrdiff_t);
static int make_walk_call(const char *, uintptr_t);

int
u2s_walk_init(mdb_walk_state_t *wsp)
{
	return (idmap_generic_init(wsp,
	    offsetof(struct nfsidmap_globals, u2s_ci)));
}
int
g2s_walk_init(mdb_walk_state_t *wsp)
{
	return (idmap_generic_init(wsp,
	    offsetof(struct nfsidmap_globals, g2s_ci)));
}

int
s2u_walk_init(mdb_walk_state_t *wsp)
{
	return (idmap_generic_init(wsp,
	    offsetof(struct nfsidmap_globals, s2u_ci)));
}
int
s2g_walk_init(mdb_walk_state_t *wsp)
{
	return (idmap_generic_init(wsp,
	    offsetof(struct nfsidmap_globals, s2g_ci)));
}

static int
idmap_generic_init(mdb_walk_state_t *wsp, ptrdiff_t offset)
{
	idmap_walk_data_t *id;
	idmap_cache_info_t ci;
	int sz = NFSID_CACHE_ANCHORS;
	uintptr_t addr;

	if ((addr = find_globals(wsp->walk_addr, "nfsidmap_zone_key", FALSE))
	    == NULL) {
		mdb_warn("couldn't find globals\n");
		return (WALK_ERR);
	}
	addr += offset;	/* skip ahead to the idmap_cache_info */
	if (mdb_vread(&ci, sizeof (idmap_cache_info_t), addr) == -1) {
		mdb_warn("unable to read idmap_cache_info at %p", addr);
		return (WALK_ERR);
	}

	id = mdb_alloc(sizeof (idmap_walk_data_t), UM_SLEEP);
	id->id_table = mdb_alloc(sz * sizeof (nfsidhq_t), UM_SLEEP);
	if (mdb_vread(id->id_table, sz * sizeof (nfsidhq_t),
	    (uintptr_t)ci.table) == -1) {
		mdb_warn("failed to read nfsidhq_t array at %p\n",
		    (uintptr_t)ci.table);
		mdb_free(id->id_table, sz * sizeof (nfsidhq_t));
		return (WALK_ERR);
	}
	id->id_ndx = 1;
	id->id_hp = (uintptr_t)ci.table;
	id->id_ip = (uintptr_t)id->id_table[0].hq_que_forw;

	wsp->walk_data = id;
	return (WALK_NEXT);
}

int
idmap_generic_step(mdb_walk_state_t *wsp)
{
	idmap_walk_data_t *id = wsp->walk_data;
	uintptr_t addr;
	nfsidmap_t entry;

again:
	while (id->id_ip == id->id_hp && id->id_ndx < NFSID_CACHE_ANCHORS) {
		int offset = sizeof (nfsidhq_t) * id->id_ndx;
		void *p = (char *)id->id_table + offset;
		nfsidhq_t *h = (nfsidhq_t *)p;

		id->id_ip = (uintptr_t)h->hq_que_forw;
		id->id_ndx++;
		id->id_hp += sizeof (nfsidhq_t);
	}

	if (id->id_ip == id->id_hp)
		return (WALK_DONE);

	if (mdb_vread(&entry, sizeof (entry), addr = id->id_ip) == -1) {
		mdb_warn("failed to read nfsidmap entry at %p in bucket %p\n",
		    id->id_ip, id->id_hp);
		id->id_ip = id->id_hp;
		goto again;
	}

	id->id_ip = (uintptr_t)entry.id_forw;
	return (wsp->walk_callback(addr, &entry, wsp->walk_cbdata));
}

void
idmap_generic_fini(mdb_walk_state_t *wsp)
{
	idmap_walk_data_t *id = wsp->walk_data;

	mdb_free(id->id_table, NFSID_CACHE_ANCHORS * sizeof (nfsidhq_t));
	mdb_free(id, sizeof (idmap_walk_data_t));
}

/* ARGSUSED */
int
nfs4_idmap(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	nfsidmap_t id;
	char s[48];
	int len = sizeof (s) - 1;

	if (!(flags & DCMD_ADDRSPEC)) {
		mdb_warn("must specify address of nfsidmap\n");
		return (DCMD_USAGE);
	}
	if (argc != 0)
		return (DCMD_USAGE);
	if (DCMD_HDRSPEC(flags)) {
		mdb_printf("%-25s %9s %s\n",
		    "TimeStamp", "Number", "String");
	}
	if (mdb_vread(&id, sizeof (id), addr) == -1) {
		mdb_warn("couldn't read nfsidmap_t at %p\n", addr);
		return (DCMD_ERR);
	}
	if (id.id_len < sizeof (s) - 1)
		len = id.id_len;
	if (mdb_vread(&s, len, (uintptr_t)id.id_val) == -1) {
		mdb_warn("couldn't read string %d bytes at %p\n",
		    len, id.id_val);
		return (DCMD_ERR);
	}
	s[len] = '\0';
	mdb_printf("%-25Y %9ld %s\n", id.id_time, (ulong_t)id.id_no, s);
	return (DCMD_OK);
}

void
nfs4_idmap_info_help(void)
{
	mdb_printf(
	    "<addr>::nfs4_idmap_info\n"
	    "\t-> print info about all entries in specified zone\n"
	    "<addr>::nfs4_idmap_info { u2s | g2s | s2u | s2g }\n"
	    "\t-> print info about all entries in the specified cache "
	    "for the zone\n");
}

int
nfs4_idmap_info(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	int u2sflag = 0;
	int g2sflag = 0;
	int s2uflag = 0;
	int s2gflag = 0;
	int i;

	if (!(flags & DCMD_ADDRSPEC))
		addr = NULL;	/* look at the global zone */
	if (argc == 0)
		u2sflag = g2sflag = s2uflag = s2gflag = 1;
	else {
		for (i = 0; i < argc; i++) {
			const char *s;
			if (argv[i].a_type != MDB_TYPE_STRING)
				return (DCMD_USAGE);
			s = argv[i].a_un.a_str;
			if (strcmp(s, "u2s") == 0)
				u2sflag = 1;
			else if (strcmp(s, "g2s") == 0)
				g2sflag = 1;
			else if (strcmp(s, "s2u") == 0)
				s2uflag = 1;
			else if (strcmp(s, "s2g") == 0)
				s2gflag = 1;
			else
				return (DCMD_USAGE);
		}
	}
	if (u2sflag) {
		if (make_walk_call("nfs4_u2s", addr))
			return (DCMD_ERR);
	}
	if (g2sflag)
		if (make_walk_call("nfs4_g2s", addr))
			return (DCMD_ERR);
	if (s2uflag)
		if (make_walk_call("nfs4_s2u", addr))
			return (DCMD_ERR);
	if (s2gflag)
		if (make_walk_call("nfs4_s2g", addr))
			return (DCMD_ERR);
	return (DCMD_OK);
}

static int
make_walk_call(const char *name, uintptr_t addr)
{
	mdb_printf("%s:\n", name);
	if (mdb_pwalk_dcmd(name, "nfs4_idmap", 0, NULL, addr) == -1) {
		mdb_warn("couldn't ::walk %s|::nfs4_idmap\n", name);
		return (-1);
	}
	return (0);
}
