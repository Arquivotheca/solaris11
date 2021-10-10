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
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/* Portions Copyright 2010 Robert Milkowski */

#include <mdb/mdb_ctf.h>
#include <sys/zfs_context.h>
#include <sys/mdb_modapi.h>
#include <sys/dbuf.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_pool.h>
#include <sys/metaslab_impl.h>
#include <sys/space_map.h>
#include <sys/list.h>
#include <sys/spa_impl.h>
#include <sys/vdev_impl.h>
#include <sys/zap_leaf.h>
#include <sys/zap_impl.h>
#include <ctype.h>
#include <sys/zfs_acl.h>
#include <sys/sa_impl.h>

#ifdef _KERNEL
#define	ZFS_OBJ_NAME	"zfs"
#else
#define	ZFS_OBJ_NAME	"libzpool.so.1"
#endif

#ifndef _KERNEL
int aok;
#endif

static int
getmember(uintptr_t addr, const char *type, mdb_ctf_id_t *idp,
    const char *member, int len, void *buf)
{
	mdb_ctf_id_t id;
	ulong_t off;
	char name[64];

	if (idp == NULL) {
		if (mdb_ctf_lookup_by_name(type, &id) == -1) {
			mdb_warn("couldn't find type %s", type);
			return (DCMD_ERR);
		}
		idp = &id;
	} else {
		type = name;
		mdb_ctf_type_name(*idp, name, sizeof (name));
	}

	if (mdb_ctf_offsetof(*idp, member, &off) == -1) {
		mdb_warn("couldn't find member %s of type %s\n", member, type);
		return (DCMD_ERR);
	}
	if (off % 8 != 0) {
		mdb_warn("member %s of type %s is unsupported bitfield",
		    member, type);
		return (DCMD_ERR);
	}
	off /= 8;

	if (mdb_vread(buf, len, addr + off) == -1) {
		mdb_warn("failed to read %s from %s at %p",
		    member, type, addr + off);
		return (DCMD_ERR);
	}
	/* mdb_warn("read %s from %s at %p+%llx\n", member, type, addr, off); */

	return (0);
}

#define	GETMEMB(addr, type, member, dest) \
	getmember(addr, #type, NULL, #member, sizeof (dest), &(dest))

#define	GETMEMBID(addr, ctfid, member, dest) \
	getmember(addr, NULL, ctfid, #member, sizeof (dest), &(dest))

static int
getrefcount(uintptr_t addr, mdb_ctf_id_t *id,
    const char *member, uint64_t *rc)
{
	static int gotid;
	static mdb_ctf_id_t rc_id;
	ulong_t off;

	if (!gotid) {
		if (mdb_ctf_lookup_by_name("struct refcount", &rc_id) == -1) {
			mdb_warn("couldn't find struct refcount");
			return (DCMD_ERR);
		}
		gotid = TRUE;
	}

	if (mdb_ctf_offsetof(*id, member, &off) == -1) {
		char name[64];
		mdb_ctf_type_name(*id, name, sizeof (name));
		mdb_warn("couldn't find member %s of type %s\n", member, name);
		return (DCMD_ERR);
	}
	off /= 8;

	return (GETMEMBID(addr + off, &rc_id, rc_count, *rc));
}

static boolean_t
strisprint(const char *cp)
{
	for (; *cp; cp++) {
		if (!isprint(*cp))
			return (B_FALSE);
	}
	return (B_TRUE);
}

static int verbose;

static int
freelist_walk_init(mdb_walk_state_t *wsp)
{
	if (wsp->walk_addr == NULL) {
		mdb_warn("must supply starting address\n");
		return (WALK_ERR);
	}

	wsp->walk_data = 0;  /* Index into the freelist */
	return (WALK_NEXT);
}

static int
freelist_walk_step(mdb_walk_state_t *wsp)
{
	uint64_t entry;
	uintptr_t number = (uintptr_t)wsp->walk_data;
	char *ddata[] = { "ALLOC", "FREE", "CONDENSE", "INVALID",
			    "INVALID", "INVALID", "INVALID", "INVALID" };
	int mapshift = SPA_MINBLOCKSHIFT;

	if (number >= (1 << SPACE_MAP_BLOCKSHIFT) / sizeof (entry))
		return (WALK_DONE);

	if (mdb_vread(&entry, sizeof (entry), wsp->walk_addr) == -1) {
		mdb_warn("failed to read freelist entry %p", wsp->walk_addr);
		return (WALK_DONE);
	}
	wsp->walk_addr += sizeof (entry);
	wsp->walk_data = (void *)(number + 1);

	if (SM_DEBUG_DECODE(entry)) {
		mdb_printf("DEBUG: %3u  %10s: txg=%llu  pass=%llu\n",
		    number,
		    ddata[SM_DEBUG_ACTION_DECODE(entry)],
		    SM_DEBUG_TXG_DECODE(entry),
		    SM_DEBUG_SYNCPASS_DECODE(entry));
	} else {
		mdb_printf("Entry: %3u  offsets=%08llx-%08llx  type=%c  "
		    "size=%06llx", number,
		    SM_OFFSET_DECODE(entry) << mapshift,
		    (SM_OFFSET_DECODE(entry) + SM_RUN_DECODE(entry)) <<
		    mapshift,
		    SM_TYPE_DECODE(entry) == SM_ALLOC ? 'A' : 'F',
		    SM_RUN_DECODE(entry) << mapshift);
		if (verbose)
			mdb_printf("      (raw=%012llx)\n", entry);
		mdb_printf("\n");
	}
	return (WALK_NEXT);
}


static int
dataset_name(uintptr_t addr, char *buf)
{
	static int gotid;
	static mdb_ctf_id_t dd_id;
	uintptr_t dd_parent;
	char dd_myname[MAXNAMELEN];

	if (!gotid) {
		if (mdb_ctf_lookup_by_name("struct dsl_dir",
		    &dd_id) == -1) {
			mdb_warn("couldn't find struct dsl_dir");
			return (DCMD_ERR);
		}
		gotid = TRUE;
	}
	if (GETMEMBID(addr, &dd_id, dd_parent, dd_parent) ||
	    GETMEMBID(addr, &dd_id, dd_myname, dd_myname)) {
		return (DCMD_ERR);
	}

	if (dd_parent) {
		if (dataset_name(dd_parent, buf))
			return (DCMD_ERR);
		strcat(buf, "/");
	}

	if (dd_myname[0])
		strcat(buf, dd_myname);
	else
		strcat(buf, "???");

	return (0);
}

static int
objset_name(uintptr_t addr, char *buf)
{
	static int gotid;
	static mdb_ctf_id_t os_id, ds_id;
	uintptr_t os_dsl_dataset;
	char ds_snapname[MAXNAMELEN];
	uintptr_t ds_dir;

	buf[0] = '\0';

	if (!gotid) {
		if (mdb_ctf_lookup_by_name("struct objset",
		    &os_id) == -1) {
			mdb_warn("couldn't find struct objset");
			return (DCMD_ERR);
		}
		if (mdb_ctf_lookup_by_name("struct dsl_dataset",
		    &ds_id) == -1) {
			mdb_warn("couldn't find struct dsl_dataset");
			return (DCMD_ERR);
		}

		gotid = TRUE;
	}

	if (GETMEMBID(addr, &os_id, os_dsl_dataset, os_dsl_dataset))
		return (DCMD_ERR);

	if (os_dsl_dataset == 0) {
		strcat(buf, "mos");
		return (0);
	}

	if (GETMEMBID(os_dsl_dataset, &ds_id, ds_snapname, ds_snapname) ||
	    GETMEMBID(os_dsl_dataset, &ds_id, ds_dir, ds_dir)) {
		return (DCMD_ERR);
	}

	if (ds_dir && dataset_name(ds_dir, buf))
		return (DCMD_ERR);

	if (ds_snapname[0]) {
		strcat(buf, "@");
		strcat(buf, ds_snapname);
	}
	return (0);
}

static void
enum_lookup(char *out, size_t size, mdb_ctf_id_t id, int val,
    const char *prefix)
{
	const char *cp;
	size_t len = strlen(prefix);

	if ((cp = mdb_ctf_enum_name(id, val)) != NULL) {
		if (strncmp(cp, prefix, len) == 0)
			cp += len;
		(void) strncpy(out, cp, size);
	} else {
		mdb_snprintf(out, size, "? (%d)", val);
	}
}

/* ARGSUSED */
static int
zfs_params(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	/*
	 * This table can be approximately generated by running:
	 * egrep "^[a-z0-9_]+ [a-z0-9_]+( =.*)?;" *.c | cut -d ' ' -f 2
	 */
	static const char *params[] = {
		"arc_reduce_dnlc_percent",
		"zfs_arc_max",
		"zfs_arc_min",
		"arc_shrink_shift",
		"zfs_mdcomp_disable",
		"zfs_prefetch_disable",
		"zfetch_max_streams",
		"zfetch_min_sec_reap",
		"zfetch_block_cap",
		"zfetch_array_rd_sz",
		"zfs_default_bs",
		"zfs_default_ibs",
		"metaslab_aliquot",
		"reference_tracking_enable",
		"reference_history",
		"spa_max_replication_override",
		"spa_mode_global",
		"zfs_flags",
		"zfs_txg_synctime_ms",
		"zfs_txg_timeout",
		"zfs_write_limit_min",
		"zfs_write_limit_max",
		"zfs_write_limit_shift",
		"zfs_write_limit_override",
		"zfs_no_write_throttle",
		"zfs_vdev_cache_max",
		"zfs_vdev_cache_size",
		"zfs_vdev_cache_bshift",
		"vdev_mirror_shift",
		"zfs_vdev_max_pending",
		"zfs_vdev_min_pending",
		"zfs_vdev_future_pending",
		"zfs_scrub_limit",
		"zfs_no_scrub_io",
		"zfs_no_scrub_prefetch",
		"zfs_vdev_time_shift",
		"zfs_vdev_ramp_rate",
		"zfs_vdev_aggregation_limit",
		"fzap_default_block_shift",
		"zfs_immediate_write_sz",
		"zfs_read_chunk_size",
		"zfs_nocacheflush",
		"zil_replay_disable",
		"metaslab_gang_threshold",
		"metaslab_df_alloc_threshold",
		"metaslab_df_free_pct",
		"zio_injection_enabled",
		"zvol_immediate_write_sz",
	};

	for (int i = 0; i < sizeof (params) / sizeof (params[0]); i++) {
		int sz;
		uint64_t val64;
		uint32_t *val32p = (uint32_t *)&val64;

		sz = mdb_readvar(&val64, params[i]);
		if (sz == 4) {
			mdb_printf("%s = 0x%x\n", params[i], *val32p);
		} else if (sz == 8) {
			mdb_printf("%s = 0x%llx\n", params[i], val64);
		} else {
			mdb_warn("variable %s not found", params[i]);
		}
	}

	return (DCMD_OK);
}

/* ARGSUSED */
static int
blkptr(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	mdb_ctf_id_t type_enum, checksum_enum, compress_enum;
	char type[80], checksum[80], compress[80];
	blkptr_t blk, *bp = &blk;
	char buf[BP_SPRINTF_LEN];

	if (mdb_vread(&blk, sizeof (blkptr_t), addr) == -1) {
		mdb_warn("failed to read blkptr_t");
		return (DCMD_ERR);
	}

	if (mdb_ctf_lookup_by_name("enum dmu_object_type", &type_enum) == -1 ||
	    mdb_ctf_lookup_by_name("enum zio_checksum", &checksum_enum) == -1 ||
	    mdb_ctf_lookup_by_name("enum zio_compress", &compress_enum) == -1) {
		mdb_warn("Could not find blkptr enumerated types");
		return (DCMD_ERR);
	}

	enum_lookup(type, sizeof (type), type_enum,
	    BP_GET_TYPE(bp), "DMU_OT_");
	enum_lookup(checksum, sizeof (checksum), checksum_enum,
	    BP_GET_CHECKSUM(bp), "ZIO_CHECKSUM_");
	enum_lookup(compress, sizeof (compress), compress_enum,
	    BP_GET_COMPRESS(bp), "ZIO_COMPRESS_");

	SPRINTF_BLKPTR(mdb_snprintf, '\n', buf, bp, type, checksum, compress);

	mdb_printf("%s\n", buf);

	return (DCMD_OK);
}

/* ARGSUSED */
static int
dbuf(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	mdb_ctf_id_t id;
	dmu_buf_t db;
	uintptr_t objset;
	uint8_t level;
	uint64_t blkid;
	uint64_t holds;
	char objectname[32];
	char blkidname[32];
	char path[MAXNAMELEN];

	if (DCMD_HDRSPEC(flags)) {
		mdb_printf("        addr object lvl blkid holds os\n");
	}

	if (mdb_ctf_lookup_by_name("struct dmu_buf_impl", &id) == -1) {
		mdb_warn("couldn't find struct dmu_buf_impl_t");
		return (DCMD_ERR);
	}

	if (GETMEMBID(addr, &id, db_objset, objset) ||
	    GETMEMBID(addr, &id, db, db) ||
	    GETMEMBID(addr, &id, db_level, level) ||
	    GETMEMBID(addr, &id, db_blkid, blkid)) {
		return (WALK_ERR);
	}

	if (getrefcount(addr, &id, "db_holds", &holds)) {
		return (WALK_ERR);
	}

	if (db.db_object == DMU_META_DNODE_OBJECT)
		(void) strcpy(objectname, "mdn");
	else
		(void) mdb_snprintf(objectname, sizeof (objectname), "%llx",
		    (u_longlong_t)db.db_object);

	if (blkid == DMU_BONUS_BLKID)
		(void) strcpy(blkidname, "bonus");
	else
		(void) mdb_snprintf(blkidname, sizeof (blkidname), "%llx",
		    (u_longlong_t)blkid);

	if (objset_name(objset, path)) {
		return (WALK_ERR);
	}

	mdb_printf("%p %8s %1u %9s %2llu %s\n",
	    addr, objectname, level, blkidname, holds, path);

	return (DCMD_OK);
}

/* ARGSUSED */
static int
dbuf_stats(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
#define	HISTOSZ 32
	uintptr_t dbp;
	dmu_buf_impl_t db;
	dbuf_hash_table_t ht;
	uint64_t bucket, ndbufs;
	uint64_t histo[HISTOSZ];
	uint64_t histo2[HISTOSZ];
	int i, maxidx;

	if (mdb_readvar(&ht, "dbuf_hash_table") == -1) {
		mdb_warn("failed to read 'dbuf_hash_table'");
		return (DCMD_ERR);
	}

	for (i = 0; i < HISTOSZ; i++) {
		histo[i] = 0;
		histo2[i] = 0;
	}

	ndbufs = 0;
	for (bucket = 0; bucket < ht.hash_table_mask+1; bucket++) {
		int len;

		if (mdb_vread(&dbp, sizeof (void *),
		    (uintptr_t)(ht.hash_table+bucket)) == -1) {
			mdb_warn("failed to read hash bucket %u at %p",
			    bucket, ht.hash_table+bucket);
			return (DCMD_ERR);
		}

		len = 0;
		while (dbp != 0) {
			if (mdb_vread(&db, sizeof (dmu_buf_impl_t),
			    dbp) == -1) {
				mdb_warn("failed to read dbuf at %p", dbp);
				return (DCMD_ERR);
			}
			dbp = (uintptr_t)db.db_hash_next;
			for (i = MIN(len, HISTOSZ - 1); i >= 0; i--)
				histo2[i]++;
			len++;
			ndbufs++;
		}

		if (len >= HISTOSZ)
			len = HISTOSZ-1;
		histo[len]++;
	}

	mdb_printf("hash table has %llu buckets, %llu dbufs "
	    "(avg %llu buckets/dbuf)\n",
	    ht.hash_table_mask+1, ndbufs,
	    (ht.hash_table_mask+1)/ndbufs);

	mdb_printf("\n");
	maxidx = 0;
	for (i = 0; i < HISTOSZ; i++)
		if (histo[i] > 0)
			maxidx = i;
	mdb_printf("hash chain length	number of buckets\n");
	for (i = 0; i <= maxidx; i++)
		mdb_printf("%u			%llu\n", i, histo[i]);

	mdb_printf("\n");
	maxidx = 0;
	for (i = 0; i < HISTOSZ; i++)
		if (histo2[i] > 0)
			maxidx = i;
	mdb_printf("hash chain depth	number of dbufs\n");
	for (i = 0; i <= maxidx; i++)
		mdb_printf("%u or more		%llu	%llu%%\n",
		    i, histo2[i], histo2[i]*100/ndbufs);


	return (DCMD_OK);
}

#define	CHAIN_END 0xffff
/*
 * ::zap_leaf [-v]
 *
 * Print a zap_leaf_phys_t, assumed to be 16k
 */
/* ARGSUSED */
static int
zap_leaf(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	char buf[16*1024];
	int verbose = B_FALSE;
	int four = B_FALSE;
	zap_leaf_t l;
	zap_leaf_phys_t *zlp = (void *)buf;
	int i;

	if (mdb_getopts(argc, argv,
	    'v', MDB_OPT_SETBITS, TRUE, &verbose,
	    '4', MDB_OPT_SETBITS, TRUE, &four,
	    NULL) != argc)
		return (DCMD_USAGE);

	l.l_phys = zlp;
	l.l_bs = 14; /* assume 16k blocks */
	if (four)
		l.l_bs = 12;

	if (!(flags & DCMD_ADDRSPEC)) {
		return (DCMD_USAGE);
	}

	if (mdb_vread(buf, sizeof (buf), addr) == -1) {
		mdb_warn("failed to read zap_leaf_phys_t at %p", addr);
		return (DCMD_ERR);
	}

	if (zlp->l_hdr.lh_block_type != ZBT_LEAF ||
	    zlp->l_hdr.lh_magic != ZAP_LEAF_MAGIC) {
		mdb_warn("This does not appear to be a zap_leaf_phys_t");
		return (DCMD_ERR);
	}

	mdb_printf("zap_leaf_phys_t at %p:\n", addr);
	mdb_printf("    lh_prefix_len = %u\n", zlp->l_hdr.lh_prefix_len);
	mdb_printf("    lh_prefix = %llx\n", zlp->l_hdr.lh_prefix);
	mdb_printf("    lh_nentries = %u\n", zlp->l_hdr.lh_nentries);
	mdb_printf("    lh_nfree = %u\n", zlp->l_hdr.lh_nfree,
	    zlp->l_hdr.lh_nfree * 100 / (ZAP_LEAF_NUMCHUNKS(&l)));
	mdb_printf("    lh_freelist = %u\n", zlp->l_hdr.lh_freelist);
	mdb_printf("    lh_flags = %x (%s)\n", zlp->l_hdr.lh_flags,
	    zlp->l_hdr.lh_flags & ZLF_ENTRIES_CDSORTED ?
	    "ENTRIES_CDSORTED" : "");

	if (verbose) {
		mdb_printf(" hash table:\n");
		for (i = 0; i < ZAP_LEAF_HASH_NUMENTRIES(&l); i++) {
			if (zlp->l_hash[i] != CHAIN_END)
				mdb_printf("    %u: %u\n", i, zlp->l_hash[i]);
		}
	}

	mdb_printf(" chunks:\n");
	for (i = 0; i < ZAP_LEAF_NUMCHUNKS(&l); i++) {
		/* LINTED: alignment */
		zap_leaf_chunk_t *zlc = &ZAP_LEAF_CHUNK(&l, i);
		switch (zlc->l_entry.le_type) {
		case ZAP_CHUNK_FREE:
			if (verbose) {
				mdb_printf("    %u: free; lf_next = %u\n",
				    i, zlc->l_free.lf_next);
			}
			break;
		case ZAP_CHUNK_ENTRY:
			mdb_printf("    %u: entry\n", i);
			if (verbose) {
				mdb_printf("        le_next = %u\n",
				    zlc->l_entry.le_next);
			}
			mdb_printf("        le_name_chunk = %u\n",
			    zlc->l_entry.le_name_chunk);
			mdb_printf("        le_name_numints = %u\n",
			    zlc->l_entry.le_name_numints);
			mdb_printf("        le_value_chunk = %u\n",
			    zlc->l_entry.le_value_chunk);
			mdb_printf("        le_value_intlen = %u\n",
			    zlc->l_entry.le_value_intlen);
			mdb_printf("        le_value_numints = %u\n",
			    zlc->l_entry.le_value_numints);
			mdb_printf("        le_cd = %u\n",
			    zlc->l_entry.le_cd);
			mdb_printf("        le_hash = %llx\n",
			    zlc->l_entry.le_hash);
			break;
		case ZAP_CHUNK_ARRAY:
			mdb_printf("    %u: array", i);
			if (strisprint((char *)zlc->l_array.la_array))
				mdb_printf(" \"%s\"", zlc->l_array.la_array);
			mdb_printf("\n");
			if (verbose) {
				int j;
				mdb_printf("        ");
				for (j = 0; j < ZAP_LEAF_ARRAY_BYTES; j++) {
					mdb_printf("%02x ",
					    zlc->l_array.la_array[j]);
				}
				mdb_printf("\n");
			}
			if (zlc->l_array.la_next != CHAIN_END) {
				mdb_printf("        lf_next = %u\n",
				    zlc->l_array.la_next);
			}
			break;
		default:
			mdb_printf("    %u: undefined type %u\n",
			    zlc->l_entry.le_type);
		}
	}

	return (DCMD_OK);
}

typedef struct dbufs_data {
	mdb_ctf_id_t id;
	uint64_t objset;
	uint64_t object;
	uint64_t level;
	uint64_t blkid;
	char *osname;
} dbufs_data_t;

#define	DBUFS_UNSET	(0xbaddcafedeadbeefULL)

/* ARGSUSED */
static int
dbufs_cb(uintptr_t addr, const void *unknown, void *arg)
{
	dbufs_data_t *data = arg;
	uintptr_t objset;
	dmu_buf_t db;
	uint8_t level;
	uint64_t blkid;
	char osname[MAXNAMELEN];

	if (GETMEMBID(addr, &data->id, db_objset, objset) ||
	    GETMEMBID(addr, &data->id, db, db) ||
	    GETMEMBID(addr, &data->id, db_level, level) ||
	    GETMEMBID(addr, &data->id, db_blkid, blkid)) {
		return (WALK_ERR);
	}

	if ((data->objset == DBUFS_UNSET || data->objset == objset) &&
	    (data->osname == NULL || (objset_name(objset, osname) == 0 &&
	    strcmp(data->osname, osname) == 0)) &&
	    (data->object == DBUFS_UNSET || data->object == db.db_object) &&
	    (data->level == DBUFS_UNSET || data->level == level) &&
	    (data->blkid == DBUFS_UNSET || data->blkid == blkid)) {
		mdb_printf("%#lr\n", addr);
	}
	return (WALK_NEXT);
}

/* ARGSUSED */
static int
dbufs(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	dbufs_data_t data;
	char *object = NULL;
	char *blkid = NULL;

	data.objset = data.object = data.level = data.blkid = DBUFS_UNSET;
	data.osname = NULL;

	if (mdb_getopts(argc, argv,
	    'O', MDB_OPT_UINT64, &data.objset,
	    'n', MDB_OPT_STR, &data.osname,
	    'o', MDB_OPT_STR, &object,
	    'l', MDB_OPT_UINT64, &data.level,
	    'b', MDB_OPT_STR, &blkid) != argc) {
		return (DCMD_USAGE);
	}

	if (object) {
		if (strcmp(object, "mdn") == 0) {
			data.object = DMU_META_DNODE_OBJECT;
		} else {
			data.object = mdb_strtoull(object);
		}
	}

	if (blkid) {
		if (strcmp(blkid, "bonus") == 0) {
			data.blkid = DMU_BONUS_BLKID;
		} else {
			data.blkid = mdb_strtoull(blkid);
		}
	}

	if (mdb_ctf_lookup_by_name("struct dmu_buf_impl", &data.id) == -1) {
		mdb_warn("couldn't find struct dmu_buf_impl_t");
		return (DCMD_ERR);
	}

	if (mdb_walk("dmu_buf_impl_t", dbufs_cb, &data) != 0) {
		mdb_warn("can't walk dbufs");
		return (DCMD_ERR);
	}

	return (DCMD_OK);
}

typedef struct abuf_find_data {
	dva_t dva;
	mdb_ctf_id_t id;
} abuf_find_data_t;

/* ARGSUSED */
static int
abuf_find_cb(uintptr_t addr, const void *unknown, void *arg)
{
	abuf_find_data_t *data = arg;
	dva_t dva;

	if (GETMEMBID(addr, &data->id, b_dva, dva)) {
		return (WALK_ERR);
	}

	if (dva.dva_word[0] == data->dva.dva_word[0] &&
	    dva.dva_word[1] == data->dva.dva_word[1]) {
		mdb_printf("%#lr\n", addr);
	}
	return (WALK_NEXT);
}

/* ARGSUSED */
static int
abuf_find(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	abuf_find_data_t data;
	GElf_Sym sym;
	int i;
	const char *syms[] = {
		"ARC_mru",
		"ARC_mru_ghost",
		"ARC_mfu",
		"ARC_mfu_ghost",
	};

	if (argc != 2)
		return (DCMD_USAGE);

	for (i = 0; i < 2; i ++) {
		switch (argv[i].a_type) {
		case MDB_TYPE_STRING:
			data.dva.dva_word[i] = mdb_strtoull(argv[i].a_un.a_str);
			break;
		case MDB_TYPE_IMMEDIATE:
			data.dva.dva_word[i] = argv[i].a_un.a_val;
			break;
		default:
			return (DCMD_USAGE);
		}
	}

	if (mdb_ctf_lookup_by_name("struct arc_buf_hdr", &data.id) == -1) {
		mdb_warn("couldn't find struct arc_buf_hdr");
		return (DCMD_ERR);
	}

	for (i = 0; i < sizeof (syms) / sizeof (syms[0]); i++) {
		if (mdb_lookup_by_name(syms[i], &sym)) {
			mdb_warn("can't find symbol %s", syms[i]);
			return (DCMD_ERR);
		}

		if (mdb_pwalk("list", abuf_find_cb, &data, sym.st_value) != 0) {
			mdb_warn("can't walk %s", syms[i]);
			return (DCMD_ERR);
		}
	}

	return (DCMD_OK);
}

/* ARGSUSED */
static int
dbgmsg_cb(uintptr_t addr, const void *unknown, void *arg)
{
	static mdb_ctf_id_t id;
	static boolean_t gotid;
	static ulong_t off;

	int *verbosep = arg;
	time_t timestamp;
	char buf[1024];

	if (!gotid) {
		if (mdb_ctf_lookup_by_name("struct zfs_dbgmsg", &id) == -1) {
			mdb_warn("couldn't find struct zfs_dbgmsg");
			return (WALK_ERR);
		}
		gotid = TRUE;
		if (mdb_ctf_offsetof(id, "zdm_msg", &off) == -1) {
			mdb_warn("couldn't find zdm_msg");
			return (WALK_ERR);
		}
		off /= 8;
	}


	if (GETMEMBID(addr, &id, zdm_timestamp, timestamp)) {
		return (WALK_ERR);
	}

	if (mdb_readstr(buf, sizeof (buf), addr + off) == -1) {
		mdb_warn("failed to read zdm_msg at %p\n", addr + off);
		return (DCMD_ERR);
	}

	if (*verbosep)
		mdb_printf("%Y ", timestamp);

	mdb_printf("%s\n", buf);

	if (*verbosep)
		(void) mdb_call_dcmd("whatis", addr, DCMD_ADDRSPEC, 0, NULL);

	return (WALK_NEXT);
}

/* ARGSUSED */
static int
dbgmsg(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	GElf_Sym sym;
	int verbose = FALSE;

	if (mdb_getopts(argc, argv,
	    'v', MDB_OPT_SETBITS, TRUE, &verbose,
	    NULL) != argc)
		return (DCMD_USAGE);

	if (mdb_lookup_by_obj(ZFS_OBJ_NAME, "zfs_dbgmsgs", &sym)) {
		mdb_warn("can't find zfs_dbgmsgs");
		return (DCMD_ERR);
	}

	if (mdb_pwalk("list", dbgmsg_cb, &verbose, sym.st_value) != 0) {
		mdb_warn("can't walk zfs_dbgmsgs");
		return (DCMD_ERR);
	}

	return (DCMD_OK);
}

/*ARGSUSED*/
static int
arc_print(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	kstat_named_t *stats;
	GElf_Sym sym;
	int nstats, i;
	uint_t opt_a = FALSE;
	uint_t opt_b = FALSE;
	uint_t shift = 0;
	const char *suffix;

	static const char *bytestats[] = {
		"p", "c", "c_min", "c_max", "size",
		"buf_size", "data_size", "other_size",
		"meta_used", "meta_max", "meta_limit",
		"l2_read_bytes", "l2_write_bytes", "l2_hdr_size",
		NULL
	};

	static const char *extras[] = {
		"arc_no_grow", "arc_tempreserve",
		NULL
	};

	if (mdb_lookup_by_obj(ZFS_OBJ_NAME, "arc_stats", &sym)) {
		mdb_warn("can't find arc_stats");
		return (DCMD_ERR);
	}

	stats = mdb_zalloc(sym.st_size, UM_SLEEP | UM_GC);

	if (mdb_vread(stats, sym.st_size, sym.st_value) == -1) {
		mdb_warn("couldn't read 'arc_stats' at %p", sym.st_value);
		return (DCMD_ERR);
	}

	nstats = sym.st_size / sizeof (kstat_named_t);

	/* NB: -a / opt_a are ignored for backwards compatibility */
	if (mdb_getopts(argc, argv,
	    'a', MDB_OPT_SETBITS, TRUE, &opt_a,
	    'b', MDB_OPT_SETBITS, TRUE, &opt_b,
	    'k', MDB_OPT_SETBITS, 10, &shift,
	    'm', MDB_OPT_SETBITS, 20, &shift,
	    'g', MDB_OPT_SETBITS, 30, &shift,
	    NULL) != argc)
		return (DCMD_USAGE);

	if (!opt_b && !shift)
		shift = 20;

	switch (shift) {
	case 0:
		suffix = "B";
		break;
	case 10:
		suffix = "KB";
		break;
	case 20:
		suffix = "MB";
		break;
	case 30:
		suffix = "GB";
		break;
	default:
		suffix = "XX";
	}

	for (i = 0; i < nstats; i++) {
		int j;
		boolean_t bytes = B_FALSE;

		for (j = 0; bytestats[j]; j++) {
			if (strcmp(stats[i].name, bytestats[j]) == 0) {
				bytes = B_TRUE;
				break;
			}
		}

		if (bytes) {
			mdb_printf("%-25s = %9llu %s\n", stats[i].name,
			    stats[i].value.ui64 >> shift, suffix);
		} else {
			mdb_printf("%-25s = %9llu\n", stats[i].name,
			    stats[i].value.ui64);
		}
	}

	for (i = 0; extras[i]; i++) {
		uint64_t buf;

		if (mdb_lookup_by_obj(ZFS_OBJ_NAME, extras[i], &sym)) {
			mdb_warn("failed to find '%s'", extras[i]);
			continue;
		}

		if (sym.st_size != sizeof (uint64_t) &&
		    sym.st_size != sizeof (uint32_t)) {
			mdb_warn("expected scalar for variable '%s'\n",
			    extras[i]);
			continue;
		}

		if (mdb_vread(&buf, sym.st_size, sym.st_value) == -1) {
			mdb_warn("couldn't read '%s'", extras[i]);
			continue;
		}

		mdb_printf("%-25s = ", extras[i]);

		/* NB: all the 64-bit extras happen to be byte counts */
		if (sym.st_size == sizeof (uint64_t))
			mdb_printf("%9llu %s\n", buf >> shift, suffix);

		if (sym.st_size == sizeof (uint32_t))
			mdb_printf("%9d\n", *((uint32_t *)&buf));
	}
	return (DCMD_OK);
}

/*
 * ::spa
 *
 * 	-c	Print configuration information as well
 * 	-v	Print vdev state
 * 	-e	Print vdev error stats
 *
 * Print a summarized spa_t.  When given no arguments, prints out a table of all
 * active pools on the system.
 */
/* ARGSUSED */
static int
spa_print(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	spa_t spa;
	const char *statetab[] = { "ACTIVE", "EXPORTED", "DESTROYED",
		"SPARE", "L2CACHE", "UNINIT", "UNAVAIL", "POTENTIAL" };
	const char *state;
	int config = FALSE;
	int vdevs = FALSE;
	int errors = FALSE;

	if (mdb_getopts(argc, argv,
	    'c', MDB_OPT_SETBITS, TRUE, &config,
	    'v', MDB_OPT_SETBITS, TRUE, &vdevs,
	    'e', MDB_OPT_SETBITS, TRUE, &errors,
	    NULL) != argc)
		return (DCMD_USAGE);

	if (!(flags & DCMD_ADDRSPEC)) {
		if (mdb_walk_dcmd("spa", "spa", argc, argv) == -1) {
			mdb_warn("can't walk spa");
			return (DCMD_ERR);
		}

		return (DCMD_OK);
	}

	if (flags & DCMD_PIPE_OUT) {
		mdb_printf("%#lr\n", addr);
		return (DCMD_OK);
	}

	if (DCMD_HDRSPEC(flags))
		mdb_printf("%<u>%-?s %9s %-*s%</u>\n", "ADDR", "STATE",
		    sizeof (uintptr_t) == 4 ? 60 : 52, "NAME");

	if (mdb_vread(&spa, sizeof (spa), addr) == -1) {
		mdb_warn("failed to read spa_t at %p", addr);
		return (DCMD_ERR);
	}

	if (spa.spa_state < 0 || spa.spa_state > POOL_STATE_UNAVAIL)
		state = "UNKNOWN";
	else
		state = statetab[spa.spa_state];

	mdb_printf("%0?p %9s %s\n", addr, state, spa.spa_name);

	if (config) {
		mdb_printf("\n");
		mdb_inc_indent(4);
		if (mdb_call_dcmd("spa_config", addr, flags, 0,
		    NULL) != DCMD_OK)
			return (DCMD_ERR);
		mdb_dec_indent(4);
	}

	if (vdevs || errors) {
		mdb_arg_t v;

		v.a_type = MDB_TYPE_STRING;
		v.a_un.a_str = "-e";

		mdb_printf("\n");
		mdb_inc_indent(4);
		if (mdb_call_dcmd("spa_vdevs", addr, flags, errors ? 1 : 0,
		    &v) != DCMD_OK)
			return (DCMD_ERR);
		mdb_dec_indent(4);
	}

	return (DCMD_OK);
}

/*
 * ::spa_config
 *
 * Given a spa_t, print the configuration information stored in spa_config.
 * Since it's just an nvlist, format it as an indented list of name=value pairs.
 * We simply read the value of spa_config and pass off to ::nvlist.
 */
/* ARGSUSED */
static int
spa_print_config(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	spa_t spa;

	if (argc != 0 || !(flags & DCMD_ADDRSPEC))
		return (DCMD_USAGE);

	if (mdb_vread(&spa, sizeof (spa), addr) == -1) {
		mdb_warn("failed to read spa_t at %p", addr);
		return (DCMD_ERR);
	}

	if (spa.spa_config == NULL) {
		mdb_printf("(none)\n");
		return (DCMD_OK);
	}

	return (mdb_call_dcmd("nvlist", (uintptr_t)spa.spa_config, flags,
	    0, NULL));
}

/*
 * ::vdev
 *
 * Print out a summarized vdev_t, in the following form:
 *
 * ADDR             STATE	AUX            DESC
 * fffffffbcde23df0 HEALTHY	-              /dev/dsk/c0t0d0
 *
 * If '-r' is specified, recursively visit all children.
 *
 * With '-e', the statistics associated with the vdev are printed as well.
 */
static int
do_print_vdev(uintptr_t addr, int flags, int depth, int stats,
    int recursive)
{
	vdev_t vdev;
	char desc[MAXNAMELEN];
	int c, children;
	uintptr_t *child;
	const char *state, *aux;

	if (mdb_vread(&vdev, sizeof (vdev), (uintptr_t)addr) == -1) {
		mdb_warn("failed to read vdev_t at %p\n", (uintptr_t)addr);
		return (DCMD_ERR);
	}

	if (flags & DCMD_PIPE_OUT) {
		mdb_printf("%#lr", addr);
	} else {
		if (vdev.vdev_path != NULL) {
			if (mdb_readstr(desc, sizeof (desc),
			    (uintptr_t)vdev.vdev_path) == -1) {
				mdb_warn("failed to read vdev_path at %p\n",
				    vdev.vdev_path);
				return (DCMD_ERR);
			}
		} else if (vdev.vdev_ops != NULL) {
			vdev_ops_t ops;
			if (mdb_vread(&ops, sizeof (ops),
			    (uintptr_t)vdev.vdev_ops) == -1) {
				mdb_warn("failed to read vdev_ops at %p\n",
				    vdev.vdev_ops);
				return (DCMD_ERR);
			}
			(void) strcpy(desc, ops.vdev_op_type);
		} else {
			(void) strcpy(desc, "<unknown>");
		}

		if (depth == 0 && DCMD_HDRSPEC(flags))
			mdb_printf("%<u>%-?s %-9s %-12s %-*s%</u>\n",
			    "ADDR", "STATE", "AUX",
			    sizeof (uintptr_t) == 4 ? 43 : 35,
			    "DESCRIPTION");

		mdb_printf("%0?p ", addr);

		switch (vdev.vdev_state) {
		case VDEV_STATE_CLOSED:
			state = "CLOSED";
			break;
		case VDEV_STATE_OFFLINE:
			state = "OFFLINE";
			break;
		case VDEV_STATE_CANT_OPEN:
			state = "CANT_OPEN";
			break;
		case VDEV_STATE_DEGRADED:
			state = "DEGRADED";
			break;
		case VDEV_STATE_HEALTHY:
			state = "HEALTHY";
			break;
		case VDEV_STATE_REMOVED:
			state = "REMOVED";
			break;
		case VDEV_STATE_FAULTED:
			state = "FAULTED";
			break;
		default:
			state = "UNKNOWN";
			break;
		}

		switch (vdev.vdev_stat.vs_aux) {
		case VDEV_AUX_NONE:
			aux = "-";
			break;
		case VDEV_AUX_OPEN_FAILED:
			aux = "OPEN_FAILED";
			break;
		case VDEV_AUX_CORRUPT_DATA:
			aux = "CORRUPT_DATA";
			break;
		case VDEV_AUX_NO_REPLICAS:
			aux = "NO_REPLICAS";
			break;
		case VDEV_AUX_BAD_GUID_SUM:
			aux = "BAD_GUID_SUM";
			break;
		case VDEV_AUX_TOO_SMALL:
			aux = "TOO_SMALL";
			break;
		case VDEV_AUX_BAD_LABEL:
			aux = "BAD_LABEL";
			break;
		case VDEV_AUX_VERSION_NEWER:
			aux = "VERS_NEWER";
			break;
		case VDEV_AUX_VERSION_OLDER:
			aux = "VERS_OLDER";
			break;
		case VDEV_AUX_SPARED:
			aux = "SPARED";
			break;
		case VDEV_AUX_ERR_EXCEEDED:
			aux = "ERR_EXCEEDED";
			break;
		case VDEV_AUX_IO_FAILURE:
			aux = "IO_FAILURE";
			break;
		case VDEV_AUX_BAD_LOG:
			aux = "BAD_LOG";
			break;
		case VDEV_AUX_EXTERNAL:
			aux = "EXTERNAL";
			break;
		case VDEV_AUX_SPLIT_POOL:
			aux = "SPLIT_POOL";
			break;
		default:
			aux = "UNKNOWN";
			break;
		}

		mdb_printf("%-9s %-12s %*s%s\n", state, aux, depth, "", desc);

		if (stats) {
			vdev_stat_t *vs = &vdev.vdev_stat;
			int i;

			mdb_inc_indent(4);
			mdb_printf("\n");
			mdb_printf("%<u>       %12s %12s %12s %12s "
			    "%12s%</u>\n", "READ", "WRITE", "FREE", "CLAIM",
			    "IOCTL");
			mdb_printf("OPS     ");
			for (i = 1; i < ZIO_TYPES; i++)
				mdb_printf("%11#llx%s", vs->vs_ops[i],
				    i == ZIO_TYPES - 1 ? "" : "  ");
			mdb_printf("\n");
			mdb_printf("BYTES   ");
			for (i = 1; i < ZIO_TYPES; i++)
				mdb_printf("%11#llx%s", vs->vs_bytes[i],
				    i == ZIO_TYPES - 1 ? "" : "  ");


			mdb_printf("\n");
			mdb_printf("EREAD    %10#llx\n", vs->vs_read_errors);
			mdb_printf("EWRITE   %10#llx\n", vs->vs_write_errors);
			mdb_printf("ECKSUM   %10#llx\n",
			    vs->vs_checksum_errors);
			mdb_dec_indent(4);
		}

		if (stats)
			mdb_printf("\n");
	}

	children = vdev.vdev_children;

	if (children == 0 || !recursive)
		return (DCMD_OK);

	child = mdb_alloc(children * sizeof (void *), UM_SLEEP | UM_GC);
	if (mdb_vread(child, children * sizeof (void *),
	    (uintptr_t)vdev.vdev_child) == -1) {
		mdb_warn("failed to read vdev children at %p", vdev.vdev_child);
		return (DCMD_ERR);
	}

	for (c = 0; c < children; c++) {
		if (do_print_vdev(child[c], flags, depth + 2, stats,
		    recursive))
			return (DCMD_ERR);
	}

	return (DCMD_OK);
}

static int
vdev_print(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	int recursive = FALSE;
	int stats = FALSE;
	uint64_t depth = 0;

	if (mdb_getopts(argc, argv,
	    'r', MDB_OPT_SETBITS, TRUE, &recursive,
	    'e', MDB_OPT_SETBITS, TRUE, &stats,
	    'd', MDB_OPT_UINT64, &depth,
	    NULL) != argc)
		return (DCMD_USAGE);

	if (!(flags & DCMD_ADDRSPEC)) {
		mdb_warn("no vdev_t address given\n");
		return (DCMD_ERR);
	}

	return (do_print_vdev(addr, flags, (int)depth, stats, recursive));
}

typedef struct metaslab_walk_data {
	uint64_t mw_numvdevs;
	uintptr_t *mw_vdevs;
	int mw_curvdev;
	uint64_t mw_nummss;
	uintptr_t *mw_mss;
	int mw_curms;
} metaslab_walk_data_t;

static int
metaslab_walk_step(mdb_walk_state_t *wsp)
{
	metaslab_walk_data_t *mw = wsp->walk_data;
	metaslab_t ms;
	uintptr_t msp;

	if (mw->mw_curvdev >= mw->mw_numvdevs)
		return (WALK_DONE);

	if (mw->mw_mss == NULL) {
		uintptr_t mssp;
		uintptr_t vdevp;

		ASSERT(mw->mw_curms == 0);
		ASSERT(mw->mw_nummss == 0);

		vdevp = mw->mw_vdevs[mw->mw_curvdev];
		if (GETMEMB(vdevp, struct vdev, vdev_ms, mssp) ||
		    GETMEMB(vdevp, struct vdev, vdev_ms_count, mw->mw_nummss)) {
			return (WALK_ERR);
		}

		mw->mw_mss = mdb_alloc(mw->mw_nummss * sizeof (void*),
		    UM_SLEEP | UM_GC);
		if (mdb_vread(mw->mw_mss, mw->mw_nummss * sizeof (void*),
		    mssp) == -1) {
			mdb_warn("failed to read vdev_ms at %p", mssp);
			return (WALK_ERR);
		}
	}

	if (mw->mw_curms >= mw->mw_nummss) {
		mw->mw_mss = NULL;
		mw->mw_curms = 0;
		mw->mw_nummss = 0;
		mw->mw_curvdev++;
		return (WALK_NEXT);
	}

	msp = mw->mw_mss[mw->mw_curms];
	if (mdb_vread(&ms, sizeof (metaslab_t), msp) == -1) {
		mdb_warn("failed to read metaslab_t at %p", msp);
		return (WALK_ERR);
	}

	mw->mw_curms++;

	return (wsp->walk_callback(msp, &ms, wsp->walk_cbdata));
}

/* ARGSUSED */
static int
metaslab_walk_init(mdb_walk_state_t *wsp)
{
	metaslab_walk_data_t *mw;
	uintptr_t root_vdevp;
	uintptr_t childp;

	if (wsp->walk_addr == NULL) {
		mdb_warn("must supply address of spa_t\n");
		return (WALK_ERR);
	}

	mw = mdb_zalloc(sizeof (metaslab_walk_data_t), UM_SLEEP | UM_GC);

	if (GETMEMB(wsp->walk_addr, struct spa, spa_root_vdev, root_vdevp) ||
	    GETMEMB(root_vdevp, struct vdev, vdev_children, mw->mw_numvdevs) ||
	    GETMEMB(root_vdevp, struct vdev, vdev_child, childp)) {
		return (DCMD_ERR);
	}

	mw->mw_vdevs = mdb_alloc(mw->mw_numvdevs * sizeof (void *),
	    UM_SLEEP | UM_GC);
	if (mdb_vread(mw->mw_vdevs, mw->mw_numvdevs * sizeof (void *),
	    childp) == -1) {
		mdb_warn("failed to read root vdev children at %p", childp);
		return (DCMD_ERR);
	}

	wsp->walk_data = mw;

	return (WALK_NEXT);
}

typedef struct mdb_spa {
	uintptr_t spa_dsl_pool;
	uintptr_t spa_root_vdev;
} mdb_spa_t;

typedef struct mdb_dsl_dir {
	uintptr_t dd_phys;
	int64_t dd_space_towrite[TXG_SIZE];
} mdb_dsl_dir_t;

typedef struct mdb_dsl_dir_phys {
	uint64_t dd_used_bytes;
	uint64_t dd_compressed_bytes;
	uint64_t dd_uncompressed_bytes;
} mdb_dsl_dir_phys_t;

typedef struct mdb_vdev {
	uintptr_t vdev_parent;
	uintptr_t vdev_ms;
	uint64_t vdev_ms_count;
	vdev_stat_t vdev_stat;
} mdb_vdev_t;

typedef struct mdb_metaslab {
	space_map_t ms_allocmap[TXG_SIZE];
	space_map_t ms_freemap[TXG_SIZE];
	space_map_t ms_map;
	space_map_obj_t ms_smo;
	space_map_obj_t ms_smo_syncing;
} mdb_metaslab_t;

typedef struct space_data {
	uint64_t ms_allocmap[TXG_SIZE];
	uint64_t ms_freemap[TXG_SIZE];
	uint64_t ms_map;
	uint64_t avail;
	uint64_t nowavail;
} space_data_t;

/* ARGSUSED */
static int
space_cb(uintptr_t addr, const void *unknown, void *arg)
{
	space_data_t *sd = arg;
	mdb_metaslab_t ms;

	if (GETMEMB(addr, struct metaslab, ms_allocmap, ms.ms_allocmap) ||
	    GETMEMB(addr, struct metaslab, ms_freemap, ms.ms_freemap) ||
	    GETMEMB(addr, struct metaslab, ms_map, ms.ms_map) ||
	    GETMEMB(addr, struct metaslab, ms_smo, ms.ms_smo) ||
	    GETMEMB(addr, struct metaslab, ms_smo_syncing, ms.ms_smo_syncing)) {
		return (WALK_ERR);
	}

	sd->ms_allocmap[0] += ms.ms_allocmap[0].sm_space;
	sd->ms_allocmap[1] += ms.ms_allocmap[1].sm_space;
	sd->ms_allocmap[2] += ms.ms_allocmap[2].sm_space;
	sd->ms_allocmap[3] += ms.ms_allocmap[3].sm_space;
	sd->ms_freemap[0] += ms.ms_freemap[0].sm_space;
	sd->ms_freemap[1] += ms.ms_freemap[1].sm_space;
	sd->ms_freemap[2] += ms.ms_freemap[2].sm_space;
	sd->ms_freemap[3] += ms.ms_freemap[3].sm_space;
	sd->ms_map += ms.ms_map.sm_space;
	sd->avail += ms.ms_map.sm_size - ms.ms_smo.smo_alloc;
	sd->nowavail += ms.ms_map.sm_size - ms.ms_smo_syncing.smo_alloc;

	return (WALK_NEXT);
}

/*
 * ::spa_space [-b]
 *
 * Given a spa_t, print out it's on-disk space usage and in-core
 * estimates of future usage.  If -b is given, print space in bytes.
 * Otherwise print in megabytes.
 */
/* ARGSUSED */
static int
spa_space(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	mdb_spa_t spa;
	uintptr_t dp_root_dir;
	mdb_dsl_dir_t dd;
	mdb_dsl_dir_phys_t dsp;
	uint64_t children;
	uintptr_t childaddr;
	space_data_t sd;
	int shift = 20;
	char *suffix = "M";
	int bits = FALSE;

	if (mdb_getopts(argc, argv, 'b', MDB_OPT_SETBITS, TRUE, &bits, NULL) !=
	    argc)
		return (DCMD_USAGE);
	if (!(flags & DCMD_ADDRSPEC))
		return (DCMD_USAGE);

	if (bits) {
		shift = 0;
		suffix = "";
	}

	if (GETMEMB(addr, struct spa, spa_dsl_pool, spa.spa_dsl_pool) ||
	    GETMEMB(addr, struct spa, spa_root_vdev, spa.spa_root_vdev) ||
	    GETMEMB(spa.spa_root_vdev, struct vdev, vdev_children, children) ||
	    GETMEMB(spa.spa_root_vdev, struct vdev, vdev_child, childaddr) ||
	    GETMEMB(spa.spa_dsl_pool, struct dsl_pool,
	    dp_root_dir, dp_root_dir) ||
	    GETMEMB(dp_root_dir, struct dsl_dir, dd_phys, dd.dd_phys) ||
	    GETMEMB(dp_root_dir, struct dsl_dir,
	    dd_space_towrite, dd.dd_space_towrite) ||
	    GETMEMB(dd.dd_phys, struct dsl_dir_phys,
	    dd_used_bytes, dsp.dd_used_bytes) ||
	    GETMEMB(dd.dd_phys, struct dsl_dir_phys,
	    dd_compressed_bytes, dsp.dd_compressed_bytes) ||
	    GETMEMB(dd.dd_phys, struct dsl_dir_phys,
	    dd_uncompressed_bytes, dsp.dd_uncompressed_bytes)) {
		return (DCMD_ERR);
	}

	mdb_printf("dd_space_towrite = %llu%s %llu%s %llu%s %llu%s\n",
	    dd.dd_space_towrite[0] >> shift, suffix,
	    dd.dd_space_towrite[1] >> shift, suffix,
	    dd.dd_space_towrite[2] >> shift, suffix,
	    dd.dd_space_towrite[3] >> shift, suffix);

	mdb_printf("dd_phys.dd_used_bytes = %llu%s\n",
	    dsp.dd_used_bytes >> shift, suffix);
	mdb_printf("dd_phys.dd_compressed_bytes = %llu%s\n",
	    dsp.dd_compressed_bytes >> shift, suffix);
	mdb_printf("dd_phys.dd_uncompressed_bytes = %llu%s\n",
	    dsp.dd_uncompressed_bytes >> shift, suffix);

	bzero(&sd, sizeof (sd));
	if (mdb_pwalk("metaslab", space_cb, &sd, addr) != 0) {
		mdb_warn("can't walk metaslabs");
		return (DCMD_ERR);
	}

	mdb_printf("ms_allocmap = %llu%s %llu%s %llu%s %llu%s\n",
	    sd.ms_allocmap[0] >> shift, suffix,
	    sd.ms_allocmap[1] >> shift, suffix,
	    sd.ms_allocmap[2] >> shift, suffix,
	    sd.ms_allocmap[3] >> shift, suffix);
	mdb_printf("ms_freemap = %llu%s %llu%s %llu%s %llu%s\n",
	    sd.ms_freemap[0] >> shift, suffix,
	    sd.ms_freemap[1] >> shift, suffix,
	    sd.ms_freemap[2] >> shift, suffix,
	    sd.ms_freemap[3] >> shift, suffix);
	mdb_printf("ms_map = %llu%s\n", sd.ms_map >> shift, suffix);
	mdb_printf("last synced avail = %llu%s\n", sd.avail >> shift, suffix);
	mdb_printf("current syncing avail = %llu%s\n",
	    sd.nowavail >> shift, suffix);

	return (DCMD_OK);
}

/*
 * ::spa_verify
 *
 * Given a spa_t, verify that that the pool is self-consistent.
 * Currently, it only checks to make sure that the vdev tree exists.
 */
/* ARGSUSED */
static int
spa_verify(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	spa_t spa;

	if (argc != 0 || !(flags & DCMD_ADDRSPEC))
		return (DCMD_USAGE);

	if (mdb_vread(&spa, sizeof (spa), addr) == -1) {
		mdb_warn("failed to read spa_t at %p", addr);
		return (DCMD_ERR);
	}

	if (spa.spa_root_vdev == NULL) {
		mdb_printf("no vdev tree present\n");
		return (DCMD_OK);
	}

	return (DCMD_OK);
}

static int
spa_print_aux(spa_aux_vdev_t *sav, uint_t flags, mdb_arg_t *v,
    const char *name)
{
	uintptr_t *aux;
	size_t len;
	int ret, i;

	/*
	 * Iterate over aux vdevs and print those out as well.  This is a
	 * little annoying because we don't have a root vdev to pass to ::vdev.
	 * Instead, we print a single line and then call it for each child
	 * vdev.
	 */
	if (sav->sav_count != 0) {
		v[1].a_type = MDB_TYPE_STRING;
		v[1].a_un.a_str = "-d";
		v[2].a_type = MDB_TYPE_IMMEDIATE;
		v[2].a_un.a_val = 2;

		len = sav->sav_count * sizeof (uintptr_t);
		aux = mdb_alloc(len, UM_SLEEP);
		if (mdb_vread(aux, len,
		    (uintptr_t)sav->sav_vdevs) == -1) {
			mdb_free(aux, len);
			mdb_warn("failed to read l2cache vdevs at %p",
			    sav->sav_vdevs);
			return (DCMD_ERR);
		}

		mdb_printf("%-?s %-9s %-12s %s\n", "-", "-", "-", name);

		for (i = 0; i < sav->sav_count; i++) {
			ret = mdb_call_dcmd("vdev", aux[i], flags, 3, v);
			if (ret != DCMD_OK) {
				mdb_free(aux, len);
				return (ret);
			}
		}

		mdb_free(aux, len);
	}

	return (0);
}

/*
 * ::spa_vdevs
 *
 * 	-e	Include error stats
 *
 * Print out a summarized list of vdevs for the given spa_t.
 * This is accomplished by invoking "::vdev -re" on the root vdev, as well as
 * iterating over the cache devices.
 */
/* ARGSUSED */
static int
spa_vdevs(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	spa_t spa;
	mdb_arg_t v[3];
	int errors = FALSE;
	int ret;

	if (mdb_getopts(argc, argv,
	    'e', MDB_OPT_SETBITS, TRUE, &errors,
	    NULL) != argc)
		return (DCMD_USAGE);

	if (!(flags & DCMD_ADDRSPEC))
		return (DCMD_USAGE);

	if (mdb_vread(&spa, sizeof (spa), addr) == -1) {
		mdb_warn("failed to read spa_t at %p", addr);
		return (DCMD_ERR);
	}

	/*
	 * Unitialized spa_t structures can have a NULL root vdev.
	 */
	if (spa.spa_root_vdev == NULL) {
		mdb_printf("no associated vdevs\n");
		return (DCMD_OK);
	}

	v[0].a_type = MDB_TYPE_STRING;
	v[0].a_un.a_str = errors ? "-re" : "-r";

	ret = mdb_call_dcmd("vdev", (uintptr_t)spa.spa_root_vdev,
	    flags, 1, v);
	if (ret != DCMD_OK)
		return (ret);

	if (spa_print_aux(&spa.spa_l2cache, flags, v, "cache") != 0 ||
	    spa_print_aux(&spa.spa_spares, flags, v, "spares") != 0)
		return (DCMD_ERR);

	return (DCMD_OK);
}

/*
 * ::zio
 *
 * Print a summary of zio_t and all its children.  This is intended to display a
 * zio tree, and hence we only pick the most important pieces of information for
 * the main summary.  More detailed information can always be found by doing a
 * '::print zio' on the underlying zio_t.  The columns we display are:
 *
 *	ADDRESS		TYPE	STAGE		WAITER
 *
 * The 'address' column is indented by one space for each depth level as we
 * descend down the tree.
 */

#define	ZIO_MAXINDENT	24
#define	ZIO_MAXWIDTH	(sizeof (uintptr_t) * 2 + ZIO_MAXINDENT)
#define	ZIO_WALK_SELF	0
#define	ZIO_WALK_CHILD	1
#define	ZIO_WALK_PARENT	2

typedef struct zio_print_args {
	int	zpa_current_depth;
	int	zpa_min_depth;
	int	zpa_max_depth;
	int	zpa_type;
	uint_t	zpa_flags;
} zio_print_args_t;

static int zio_child_cb(uintptr_t addr, const void *unknown, void *arg);

static int
zio_print_cb(uintptr_t addr, const void *data, void *priv)
{
	const zio_t *zio = data;
	zio_print_args_t *zpa = priv;
	mdb_ctf_id_t type_enum, stage_enum;
	int indent = zpa->zpa_current_depth;
	const char *type, *stage;
	uintptr_t laddr;

	if (indent > ZIO_MAXINDENT)
		indent = ZIO_MAXINDENT;

	if (mdb_ctf_lookup_by_name("enum zio_type", &type_enum) == -1 ||
	    mdb_ctf_lookup_by_name("enum zio_stage", &stage_enum) == -1) {
		mdb_warn("failed to lookup zio enums");
		return (WALK_ERR);
	}

	if ((type = mdb_ctf_enum_name(type_enum, zio->io_type)) != NULL)
		type += sizeof ("ZIO_TYPE_") - 1;
	else
		type = "?";

	if ((stage = mdb_ctf_enum_name(stage_enum, zio->io_stage)) != NULL)
		stage += sizeof ("ZIO_STAGE_") - 1;
	else
		stage = "?";

	if (zpa->zpa_current_depth >= zpa->zpa_min_depth) {
		if (zpa->zpa_flags & DCMD_PIPE_OUT) {
			mdb_printf("%?p\n", addr);
		} else {
			mdb_printf("%*s%-*p %-5s %-16s ", indent, "",
			    ZIO_MAXWIDTH - indent, addr, type, stage);
			if (zio->io_waiter)
				mdb_printf("%?p\n", zio->io_waiter);
			else
				mdb_printf("-\n");
		}
	}

	if (zpa->zpa_current_depth >= zpa->zpa_max_depth)
		return (WALK_NEXT);

	if (zpa->zpa_type == ZIO_WALK_PARENT)
		laddr = addr + OFFSETOF(zio_t, io_parent_list);
	else
		laddr = addr + OFFSETOF(zio_t, io_child_list);

	zpa->zpa_current_depth++;
	if (mdb_pwalk("list", zio_child_cb, zpa, laddr) != 0) {
		mdb_warn("failed to walk zio_t children at %p\n", laddr);
		return (WALK_ERR);
	}
	zpa->zpa_current_depth--;

	return (WALK_NEXT);
}

/* ARGSUSED */
static int
zio_child_cb(uintptr_t addr, const void *unknown, void *arg)
{
	zio_link_t zl;
	zio_t zio;
	uintptr_t ziop;
	zio_print_args_t *zpa = arg;

	if (mdb_vread(&zl, sizeof (zl), addr) == -1) {
		mdb_warn("failed to read zio_link_t at %p", addr);
		return (WALK_ERR);
	}

	if (zpa->zpa_type == ZIO_WALK_PARENT)
		ziop = (uintptr_t)zl.zl_parent;
	else
		ziop = (uintptr_t)zl.zl_child;

	if (mdb_vread(&zio, sizeof (zio_t), ziop) == -1) {
		mdb_warn("failed to read zio_t at %p", ziop);
		return (WALK_ERR);
	}

	return (zio_print_cb(ziop, &zio, arg));
}

/* ARGSUSED */
static int
zio_print(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	zio_t zio;
	zio_print_args_t zpa = { 0 };

	if (!(flags & DCMD_ADDRSPEC))
		return (DCMD_USAGE);

	if (mdb_getopts(argc, argv,
	    'r', MDB_OPT_SETBITS, INT_MAX, &zpa.zpa_max_depth,
	    'c', MDB_OPT_SETBITS, ZIO_WALK_CHILD, &zpa.zpa_type,
	    'p', MDB_OPT_SETBITS, ZIO_WALK_PARENT, &zpa.zpa_type,
	    NULL) != argc)
		return (DCMD_USAGE);

	zpa.zpa_flags = flags;
	if (zpa.zpa_max_depth != 0) {
		if (zpa.zpa_type == ZIO_WALK_SELF)
			zpa.zpa_type = ZIO_WALK_CHILD;
	} else if (zpa.zpa_type != ZIO_WALK_SELF) {
		zpa.zpa_min_depth = 1;
		zpa.zpa_max_depth = 1;
	}

	if (mdb_vread(&zio, sizeof (zio_t), addr) == -1) {
		mdb_warn("failed to read zio_t at %p", addr);
		return (DCMD_ERR);
	}

	if (DCMD_HDRSPEC(flags))
		mdb_printf("%<u>%-*s %-5s %-16s %-?s%</u>\n", ZIO_MAXWIDTH,
		    "ADDRESS", "TYPE", "STAGE", "WAITER");

	if (zio_print_cb(addr, &zio, &zpa) != WALK_NEXT)
		return (DCMD_ERR);

	return (DCMD_OK);
}

/*
 * [addr]::zio_state
 *
 * Print a summary of all zio_t structures on the system, or for a particular
 * pool.  This is equivalent to '::walk zio_root | ::zio'.
 */
/*ARGSUSED*/
static int
zio_state(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	/*
	 * MDB will remember the last address of the pipeline, so if we don't
	 * zero this we'll end up trying to walk zio structures for a
	 * non-existent spa_t.
	 */
	if (!(flags & DCMD_ADDRSPEC))
		addr = 0;

	return (mdb_pwalk_dcmd("zio_root", "zio", argc, argv, addr));
}

typedef struct txg_list_walk_data {
	uintptr_t lw_head[TXG_SIZE];
	int	lw_txgoff;
	int	lw_maxoff;
	size_t	lw_offset;
	void	*lw_obj;
} txg_list_walk_data_t;

static int
txg_list_walk_init_common(mdb_walk_state_t *wsp, int txg, int maxoff)
{
	txg_list_walk_data_t *lwd;
	txg_list_t list;
	int i;

	lwd = mdb_alloc(sizeof (txg_list_walk_data_t), UM_SLEEP | UM_GC);
	if (mdb_vread(&list, sizeof (txg_list_t), wsp->walk_addr) == -1) {
		mdb_warn("failed to read txg_list_t at %#lx", wsp->walk_addr);
		return (WALK_ERR);
	}

	for (i = 0; i < TXG_SIZE; i++)
		lwd->lw_head[i] = (uintptr_t)list.tl_head[i];
	lwd->lw_offset = list.tl_offset;
	lwd->lw_obj = mdb_alloc(lwd->lw_offset + sizeof (txg_node_t),
	    UM_SLEEP | UM_GC);
	lwd->lw_txgoff = txg;
	lwd->lw_maxoff = maxoff;

	wsp->walk_addr = lwd->lw_head[lwd->lw_txgoff];
	wsp->walk_data = lwd;

	return (WALK_NEXT);
}

static int
txg_list_walk_init(mdb_walk_state_t *wsp)
{
	return (txg_list_walk_init_common(wsp, 0, TXG_SIZE-1));
}

static int
txg_list0_walk_init(mdb_walk_state_t *wsp)
{
	return (txg_list_walk_init_common(wsp, 0, 0));
}

static int
txg_list1_walk_init(mdb_walk_state_t *wsp)
{
	return (txg_list_walk_init_common(wsp, 1, 1));
}

static int
txg_list2_walk_init(mdb_walk_state_t *wsp)
{
	return (txg_list_walk_init_common(wsp, 2, 2));
}

static int
txg_list3_walk_init(mdb_walk_state_t *wsp)
{
	return (txg_list_walk_init_common(wsp, 3, 3));
}

static int
txg_list_walk_step(mdb_walk_state_t *wsp)
{
	txg_list_walk_data_t *lwd = wsp->walk_data;
	uintptr_t addr;
	txg_node_t *node;
	int status;

	while (wsp->walk_addr == NULL && lwd->lw_txgoff < lwd->lw_maxoff) {
		lwd->lw_txgoff++;
		wsp->walk_addr = lwd->lw_head[lwd->lw_txgoff];
	}

	if (wsp->walk_addr == NULL)
		return (WALK_DONE);

	addr = wsp->walk_addr - lwd->lw_offset;

	if (mdb_vread(lwd->lw_obj,
	    lwd->lw_offset + sizeof (txg_node_t), addr) == -1) {
		mdb_warn("failed to read list element at %#lx", addr);
		return (WALK_ERR);
	}

	status = wsp->walk_callback(addr, lwd->lw_obj, wsp->walk_cbdata);
	node = (txg_node_t *)((uintptr_t)lwd->lw_obj + lwd->lw_offset);
	wsp->walk_addr = (uintptr_t)node->tn_next[lwd->lw_txgoff];

	return (status);
}

/*
 * ::walk spa
 *
 * Walk all named spa_t structures in the namespace.  This is nothing more than
 * a layered avl walk.
 */
static int
spa_walk_init(mdb_walk_state_t *wsp)
{
	GElf_Sym sym;

	if (wsp->walk_addr != NULL) {
		mdb_warn("spa walk only supports global walks\n");
		return (WALK_ERR);
	}

	if (mdb_lookup_by_obj(ZFS_OBJ_NAME, "spa_namespace_avl", &sym) == -1) {
		mdb_warn("failed to find symbol 'spa_namespace_avl'");
		return (WALK_ERR);
	}

	wsp->walk_addr = (uintptr_t)sym.st_value;

	if (mdb_layered_walk("avl", wsp) == -1) {
		mdb_warn("failed to walk 'avl'\n");
		return (WALK_ERR);
	}

	return (WALK_NEXT);
}

static int
spa_walk_step(mdb_walk_state_t *wsp)
{
	spa_t	spa;

	if (mdb_vread(&spa, sizeof (spa), wsp->walk_addr) == -1) {
		mdb_warn("failed to read spa_t at %p", wsp->walk_addr);
		return (WALK_ERR);
	}

	return (wsp->walk_callback(wsp->walk_addr, &spa, wsp->walk_cbdata));
}

/*
 * [addr]::walk zio
 *
 * Walk all active zio_t structures on the system.  This is simply a layered
 * walk on top of ::walk zio_cache, with the optional ability to limit the
 * structures to a particular pool.
 */
static int
zio_walk_init(mdb_walk_state_t *wsp)
{
	wsp->walk_data = (void *)wsp->walk_addr;

	if (mdb_layered_walk("zio_cache", wsp) == -1) {
		mdb_warn("failed to walk 'zio_cache'\n");
		return (WALK_ERR);
	}

	return (WALK_NEXT);
}

static int
zio_walk_step(mdb_walk_state_t *wsp)
{
	zio_t zio;

	if (mdb_vread(&zio, sizeof (zio), wsp->walk_addr) == -1) {
		mdb_warn("failed to read zio_t at %p", wsp->walk_addr);
		return (WALK_ERR);
	}

	if (wsp->walk_data != NULL && wsp->walk_data != zio.io_spa)
		return (WALK_NEXT);

	return (wsp->walk_callback(wsp->walk_addr, &zio, wsp->walk_cbdata));
}

/*
 * [addr]::walk zio_root
 *
 * Walk only root zio_t structures, optionally for a particular spa_t.
 */
static int
zio_walk_root_step(mdb_walk_state_t *wsp)
{
	zio_t zio;

	if (mdb_vread(&zio, sizeof (zio), wsp->walk_addr) == -1) {
		mdb_warn("failed to read zio_t at %p", wsp->walk_addr);
		return (WALK_ERR);
	}

	if (wsp->walk_data != NULL && wsp->walk_data != zio.io_spa)
		return (WALK_NEXT);

	/* If the parent list is not empty, ignore */
	if (zio.io_parent_list.list_head.list_next !=
	    &((zio_t *)wsp->walk_addr)->io_parent_list.list_head)
		return (WALK_NEXT);

	return (wsp->walk_callback(wsp->walk_addr, &zio, wsp->walk_cbdata));
}

#define	NICENUM_BUFLEN 6

static int
snprintfrac(char *buf, int len,
    uint64_t numerator, uint64_t denom, int frac_digits)
{
	int mul = 1;
	int whole, frac, i;

	for (i = frac_digits; i; i--)
		mul *= 10;
	whole = numerator / denom;
	frac = mul * numerator / denom - mul * whole;
	return (mdb_snprintf(buf, len, "%u.%0*u", whole, frac_digits, frac));
}

static void
mdb_nicenum(uint64_t num, char *buf)
{
	uint64_t n = num;
	int index = 0;
	char *u;

	while (n >= 1024) {
		n = (n + (1024 / 2)) / 1024; /* Round up or down */
		index++;
	}

	u = &" \0K\0M\0G\0T\0P\0E\0"[index*2];

	if (index == 0) {
		(void) mdb_snprintf(buf, NICENUM_BUFLEN, "%llu",
		    (u_longlong_t)n);
	} else if (n < 10 && (num & (num - 1)) != 0) {
		(void) snprintfrac(buf, NICENUM_BUFLEN,
		    num, 1ULL << 10 * index, 2);
		strcat(buf, u);
	} else if (n < 100 && (num & (num - 1)) != 0) {
		(void) snprintfrac(buf, NICENUM_BUFLEN,
		    num, 1ULL << 10 * index, 1);
		strcat(buf, u);
	} else {
		(void) mdb_snprintf(buf, NICENUM_BUFLEN, "%llu%s",
		    (u_longlong_t)n, u);
	}
}

/*
 * ::zfs_blkstats
 *
 * 	-v	print verbose per-level information
 *
 */
static int
zfs_blkstats(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	boolean_t verbose = B_FALSE;
	zfs_all_blkstats_t stats;
	dmu_object_type_t t;
	zfs_blkstat_t *tzb;
	uint64_t ditto;
	dmu_object_type_info_t dmu_ot[DMU_OT_NUMTYPES + 10];
	/* +10 in case it grew */

	if (mdb_readvar(&dmu_ot, "dmu_ot") == -1) {
		mdb_warn("failed to read 'dmu_ot'");
		return (DCMD_ERR);
	}

	if (mdb_getopts(argc, argv,
	    'v', MDB_OPT_SETBITS, TRUE, &verbose,
	    NULL) != argc)
		return (DCMD_USAGE);

	if (!(flags & DCMD_ADDRSPEC))
		return (DCMD_USAGE);

	if (GETMEMB(addr, struct spa, spa_dsl_pool, addr) ||
	    GETMEMB(addr, struct dsl_pool, dp_blkstats, addr) ||
	    mdb_vread(&stats, sizeof (zfs_all_blkstats_t), addr) == -1) {
		mdb_warn("failed to read data at %p;", addr);
		mdb_printf("maybe no stats? run \"zpool scrub\" first.");
		return (DCMD_ERR);
	}

	tzb = &stats.zab_type[DN_MAX_LEVELS][DMU_OT_NUMTYPES];
	if (tzb->zb_gangs != 0) {
		mdb_printf("Ganged blocks: %llu\n",
		    (longlong_t)tzb->zb_gangs);
	}

	ditto = tzb->zb_ditto_2_of_2_samevdev + tzb->zb_ditto_2_of_3_samevdev +
	    tzb->zb_ditto_3_of_3_samevdev;
	if (ditto != 0) {
		mdb_printf("Dittoed blocks on same vdev: %llu\n",
		    (longlong_t)ditto);
	}

	mdb_printf("\nBlocks\tLSIZE\tPSIZE\tASIZE"
	    "\t  avg\t comp\t%%Total\tType\n");

	for (t = 0; t <= DMU_OT_NUMTYPES; t++) {
		char csize[NICENUM_BUFLEN], lsize[NICENUM_BUFLEN];
		char psize[NICENUM_BUFLEN], asize[NICENUM_BUFLEN];
		char avg[NICENUM_BUFLEN];
		char comp[NICENUM_BUFLEN], pct[NICENUM_BUFLEN];
		char typename[64];
		int l;


		if (t == DMU_OT_DEFERRED)
			strcpy(typename, "deferred free");
		else if (t == DMU_OT_TOTAL)
			strcpy(typename, "Total");
		else if (mdb_readstr(typename, sizeof (typename),
		    (uintptr_t)dmu_ot[t].ot_name) == -1) {
			mdb_warn("failed to read type name");
			return (DCMD_ERR);
		}

		if (stats.zab_type[DN_MAX_LEVELS][t].zb_asize == 0)
			continue;

		for (l = -1; l < DN_MAX_LEVELS; l++) {
			int level = (l == -1 ? DN_MAX_LEVELS : l);
			zfs_blkstat_t *zb = &stats.zab_type[level][t];

			if (zb->zb_asize == 0)
				continue;

			/*
			 * Don't print each level unless requested.
			 */
			if (!verbose && level != DN_MAX_LEVELS)
				continue;

			/*
			 * If all the space is level 0, don't print the
			 * level 0 separately.
			 */
			if (level == 0 && zb->zb_asize ==
			    stats.zab_type[DN_MAX_LEVELS][t].zb_asize)
				continue;

			mdb_nicenum(zb->zb_count, csize);
			mdb_nicenum(zb->zb_lsize, lsize);
			mdb_nicenum(zb->zb_psize, psize);
			mdb_nicenum(zb->zb_asize, asize);
			mdb_nicenum(zb->zb_asize / zb->zb_count, avg);
			(void) snprintfrac(comp, NICENUM_BUFLEN,
			    zb->zb_lsize, zb->zb_psize, 2);
			(void) snprintfrac(pct, NICENUM_BUFLEN,
			    100 * zb->zb_asize, tzb->zb_asize, 2);

			mdb_printf("%6s\t%5s\t%5s\t%5s\t%5s"
			    "\t%5s\t%6s\t",
			    csize, lsize, psize, asize, avg, comp, pct);

			if (level == DN_MAX_LEVELS)
				mdb_printf("%s\n", typename);
			else
				mdb_printf("  L%d %s\n",
				    level, typename);
		}
	}

	return (DCMD_OK);
}

/* ARGSUSED */
static int
reference_cb(uintptr_t addr, const void *ignored, void *arg)
{
	static int gotid;
	static mdb_ctf_id_t ref_id;
	uintptr_t ref_holder;
	uintptr_t ref_removed;
	uint64_t ref_number;
	boolean_t holder_is_str = B_FALSE;
	char holder_str[128];
	boolean_t removed = (boolean_t)arg;

	if (!gotid) {
		if (mdb_ctf_lookup_by_name("struct reference", &ref_id) == -1) {
			mdb_warn("couldn't find struct reference");
			return (WALK_ERR);
		}
		gotid = TRUE;
	}

	if (GETMEMBID(addr, &ref_id, ref_holder, ref_holder) ||
	    GETMEMBID(addr, &ref_id, ref_removed, ref_removed) ||
	    GETMEMBID(addr, &ref_id, ref_number, ref_number))
		return (WALK_ERR);

	if (mdb_readstr(holder_str, sizeof (holder_str), ref_holder) != -1)
		holder_is_str = strisprint(holder_str);

	if (removed)
		mdb_printf("removed ");
	mdb_printf("reference ");
	if (ref_number != 1)
		mdb_printf("with count=%llu ", ref_number);
	mdb_printf("with tag %p", (void*)ref_holder);
	if (holder_is_str)
		mdb_printf(" \"%s\"", holder_str);
	mdb_printf(", held at:\n");

	(void) mdb_call_dcmd("whatis", addr, DCMD_ADDRSPEC, 0, NULL);

	if (removed) {
		mdb_printf("removed at:\n");
		(void) mdb_call_dcmd("whatis", ref_removed,
		    DCMD_ADDRSPEC, 0, NULL);
	}

	mdb_printf("\n");

	return (WALK_NEXT);
}

/* ARGSUSED */
static int
refcount(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	uint64_t rc_count, rc_removed_count;
	uintptr_t rc_list, rc_removed;
	static int gotid;
	static mdb_ctf_id_t rc_id;
	ulong_t off;

	if (!(flags & DCMD_ADDRSPEC))
		return (DCMD_USAGE);

	if (!gotid) {
		if (mdb_ctf_lookup_by_name("struct refcount", &rc_id) == -1) {
			mdb_warn("couldn't find struct refcount");
			return (DCMD_ERR);
		}
		gotid = TRUE;
	}

	if (GETMEMBID(addr, &rc_id, rc_count, rc_count) ||
	    GETMEMBID(addr, &rc_id, rc_removed_count, rc_removed_count))
		return (DCMD_ERR);

	mdb_printf("refcount_t at %p has %llu current holds, "
	    "%llu recently released holds\n",
	    addr, (longlong_t)rc_count, (longlong_t)rc_removed_count);

	if (rc_count > 0)
		mdb_printf("current holds:\n");
	if (mdb_ctf_offsetof(rc_id, "rc_list", &off) == -1)
		return (DCMD_ERR);
	rc_list = addr + off/NBBY;
	mdb_pwalk("list", reference_cb, (void*)B_FALSE, rc_list);

	if (rc_removed_count > 0)
		mdb_printf("released holds:\n");
	if (mdb_ctf_offsetof(rc_id, "rc_removed", &off) == -1)
		return (DCMD_ERR);
	rc_removed = addr + off/NBBY;
	mdb_pwalk("list", reference_cb, (void*)B_TRUE, rc_removed);

	return (DCMD_OK);
}

/* ARGSUSED */
static int
sa_attr_table(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	sa_attr_table_t *table;
	sa_os_t sa_os;
	char *name;
	int i;

	if (mdb_vread(&sa_os, sizeof (sa_os_t), addr) == -1) {
		mdb_warn("failed to read sa_os at %p", addr);
		return (DCMD_ERR);
	}

	table = mdb_alloc(sizeof (sa_attr_table_t) * sa_os.sa_num_attrs,
	    UM_SLEEP | UM_GC);
	name = mdb_alloc(MAXPATHLEN, UM_SLEEP | UM_GC);

	if (mdb_vread(table, sizeof (sa_attr_table_t) * sa_os.sa_num_attrs,
	    (uintptr_t)sa_os.sa_attr_table) == -1) {
		mdb_warn("failed to read sa_os at %p", addr);
		return (DCMD_ERR);
	}

	mdb_printf("%<u>%-10s %-10s %-10s %-10s %s%</u>\n",
	    "ATTR ID", "REGISTERED", "LENGTH", "BSWAP", "NAME");
	for (i = 0; i != sa_os.sa_num_attrs; i++) {
		mdb_readstr(name, MAXPATHLEN, (uintptr_t)table[i].sa_name);
		mdb_printf("%5x   %8x %8x %8x          %-s\n",
		    (int)table[i].sa_attr, (int)table[i].sa_registered,
		    (int)table[i].sa_length, table[i].sa_byteswap, name);
	}

	return (DCMD_OK);
}

static int
sa_get_off_table(uintptr_t addr, uint32_t **off_tab, int attr_count)
{
	uintptr_t idx_table;

	if (GETMEMB(addr, struct sa_idx_tab, sa_idx_tab, idx_table)) {
		mdb_printf("can't find offset table in sa_idx_tab\n");
		return (-1);
	}

	*off_tab = mdb_alloc(attr_count * sizeof (uint32_t),
	    UM_SLEEP | UM_GC);

	if (mdb_vread(*off_tab,
	    attr_count * sizeof (uint32_t), idx_table) == -1) {
		mdb_warn("failed to attribute offset table %p", idx_table);
		return (-1);
	}

	return (DCMD_OK);
}

/*ARGSUSED*/
static int
sa_attr_print(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	uint32_t *offset_tab;
	int attr_count;
	uint64_t attr_id;
	uintptr_t attr_addr;
	uintptr_t bonus_tab, spill_tab;
	uintptr_t db_bonus, db_spill;
	uintptr_t os, os_sa;
	uintptr_t db_data;

	if (argc != 1)
		return (DCMD_USAGE);

	if (argv[0].a_type == MDB_TYPE_STRING)
		attr_id = mdb_strtoull(argv[0].a_un.a_str);
	else
		return (DCMD_USAGE);

	if (GETMEMB(addr, struct sa_handle, sa_bonus_tab, bonus_tab) ||
	    GETMEMB(addr, struct sa_handle, sa_spill_tab, spill_tab) ||
	    GETMEMB(addr, struct sa_handle, sa_os, os) ||
	    GETMEMB(addr, struct sa_handle, sa_bonus, db_bonus) ||
	    GETMEMB(addr, struct sa_handle, sa_spill, db_spill)) {
		mdb_printf("Can't find necessary information in sa_handle "
		    "in sa_handle\n");
		return (DCMD_ERR);
	}

	if (GETMEMB(os, struct objset, os_sa, os_sa)) {
		mdb_printf("Can't find os_sa in objset\n");
		return (DCMD_ERR);
	}

	if (GETMEMB(os_sa, struct sa_os, sa_num_attrs, attr_count)) {
		mdb_printf("Can't find sa_num_attrs\n");
		return (DCMD_ERR);
	}

	if (attr_id > attr_count) {
		mdb_printf("attribute id number is out of range\n");
		return (DCMD_ERR);
	}

	if (bonus_tab) {
		if (sa_get_off_table(bonus_tab, &offset_tab,
		    attr_count) == -1) {
			return (DCMD_ERR);
		}

		if (GETMEMB(db_bonus, struct dmu_buf, db_data, db_data)) {
			mdb_printf("can't find db_data in bonus dbuf\n");
			return (DCMD_ERR);
		}
	}

	if (bonus_tab && !TOC_ATTR_PRESENT(offset_tab[attr_id]) &&
	    spill_tab == NULL) {
		mdb_printf("Attribute does not exist\n");
		return (DCMD_ERR);
	} else if (!TOC_ATTR_PRESENT(offset_tab[attr_id]) && spill_tab) {
		if (sa_get_off_table(spill_tab, &offset_tab,
		    attr_count) == -1) {
			return (DCMD_ERR);
		}
		if (GETMEMB(db_spill, struct dmu_buf, db_data, db_data)) {
			mdb_printf("can't find db_data in spill dbuf\n");
			return (DCMD_ERR);
		}
		if (!TOC_ATTR_PRESENT(offset_tab[attr_id])) {
			mdb_printf("Attribute does not exist\n");
			return (DCMD_ERR);
		}
	}
	attr_addr = db_data + TOC_OFF(offset_tab[attr_id]);
	mdb_printf("%p\n", attr_addr);
	return (DCMD_OK);
}

/* ARGSUSED */
static int
zfs_ace_print_common(uintptr_t addr, uint_t flags,
    uint64_t id, uint32_t access_mask, uint16_t ace_flags,
    uint16_t ace_type, int verbose)
{
	if (DCMD_HDRSPEC(flags) && !verbose)
		mdb_printf("%<u>%-?s %-8s %-8s %-8s %s%</u>\n",
		    "ADDR", "FLAGS", "MASK", "TYPE", "ID");

	if (!verbose) {
		mdb_printf("%0?p %-8x %-8x %-8x %-llx\n", addr,
		    ace_flags, access_mask, ace_type, id);
		return (DCMD_OK);
	}

	switch (ace_flags & ACE_TYPE_FLAGS) {
	case ACE_OWNER:
		mdb_printf("owner@:");
		break;
	case (ACE_IDENTIFIER_GROUP | ACE_GROUP):
		mdb_printf("group@:");
		break;
	case ACE_EVERYONE:
		mdb_printf("everyone@:");
		break;
	case ACE_IDENTIFIER_GROUP:
		mdb_printf("group:%llx:", (u_longlong_t)id);
		break;
	case 0: /* User entry */
		mdb_printf("user:%llx:", (u_longlong_t)id);
		break;
	}

	/* print out permission mask */
	if (access_mask & ACE_READ_DATA)
		mdb_printf("r");
	else
		mdb_printf("-");
	if (access_mask & ACE_WRITE_DATA)
		mdb_printf("w");
	else
		mdb_printf("-");
	if (access_mask & ACE_EXECUTE)
		mdb_printf("x");
	else
		mdb_printf("-");
	if (access_mask & ACE_APPEND_DATA)
		mdb_printf("p");
	else
		mdb_printf("-");
	if (access_mask & ACE_DELETE)
		mdb_printf("d");
	else
		mdb_printf("-");
	if (access_mask & ACE_DELETE_CHILD)
		mdb_printf("D");
	else
		mdb_printf("-");
	if (access_mask & ACE_READ_ATTRIBUTES)
		mdb_printf("a");
	else
		mdb_printf("-");
	if (access_mask & ACE_WRITE_ATTRIBUTES)
		mdb_printf("A");
	else
		mdb_printf("-");
	if (access_mask & ACE_READ_NAMED_ATTRS)
		mdb_printf("R");
	else
		mdb_printf("-");
	if (access_mask & ACE_WRITE_NAMED_ATTRS)
		mdb_printf("W");
	else
		mdb_printf("-");
	if (access_mask & ACE_READ_ACL)
		mdb_printf("c");
	else
		mdb_printf("-");
	if (access_mask & ACE_WRITE_ACL)
		mdb_printf("C");
	else
		mdb_printf("-");
	if (access_mask & ACE_WRITE_OWNER)
		mdb_printf("o");
	else
		mdb_printf("-");
	if (access_mask & ACE_SYNCHRONIZE)
		mdb_printf("s");
	else
		mdb_printf("-");

	mdb_printf(":");

	/* Print out inheritance flags */
	if (ace_flags & ACE_FILE_INHERIT_ACE)
		mdb_printf("f");
	else
		mdb_printf("-");
	if (ace_flags & ACE_DIRECTORY_INHERIT_ACE)
		mdb_printf("d");
	else
		mdb_printf("-");
	if (ace_flags & ACE_INHERIT_ONLY_ACE)
		mdb_printf("i");
	else
		mdb_printf("-");
	if (ace_flags & ACE_NO_PROPAGATE_INHERIT_ACE)
		mdb_printf("n");
	else
		mdb_printf("-");
	if (ace_flags & ACE_SUCCESSFUL_ACCESS_ACE_FLAG)
		mdb_printf("S");
	else
		mdb_printf("-");
	if (ace_flags & ACE_FAILED_ACCESS_ACE_FLAG)
		mdb_printf("F");
	else
		mdb_printf("-");
	if (ace_flags & ACE_INHERITED_ACE)
		mdb_printf("I");
	else
		mdb_printf("-");

	switch (ace_type) {
	case ACE_ACCESS_ALLOWED_ACE_TYPE:
		mdb_printf(":allow\n");
		break;
	case ACE_ACCESS_DENIED_ACE_TYPE:
		mdb_printf(":deny\n");
		break;
	case ACE_SYSTEM_AUDIT_ACE_TYPE:
		mdb_printf(":audit\n");
		break;
	case ACE_SYSTEM_ALARM_ACE_TYPE:
		mdb_printf(":alarm\n");
		break;
	default:
		mdb_printf(":?\n");
	}
	return (DCMD_OK);
}

/* ARGSUSED */
static int
zfs_ace_print(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	zfs_ace_t zace;
	int verbose = FALSE;
	uint64_t id;

	if (!(flags & DCMD_ADDRSPEC))
		return (DCMD_USAGE);

	if (mdb_getopts(argc, argv,
	    'v', MDB_OPT_SETBITS, TRUE, &verbose, TRUE, NULL) != argc)
		return (DCMD_USAGE);

	if (mdb_vread(&zace, sizeof (zfs_ace_t), addr) == -1) {
		mdb_warn("failed to read zfs_ace_t");
		return (DCMD_ERR);
	}

	if ((zace.z_hdr.z_flags & ACE_TYPE_FLAGS) == 0 ||
	    (zace.z_hdr.z_flags & ACE_TYPE_FLAGS) == ACE_IDENTIFIER_GROUP)
		id = zace.z_fuid;
	else
		id = -1;

	return (zfs_ace_print_common(addr, flags, id, zace.z_hdr.z_access_mask,
	    zace.z_hdr.z_flags, zace.z_hdr.z_type, verbose));
}

/* ARGSUSED */
static int
zfs_ace0_print(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	ace_t ace;
	uint64_t id;
	int verbose = FALSE;

	if (!(flags & DCMD_ADDRSPEC))
		return (DCMD_USAGE);

	if (mdb_getopts(argc, argv,
	    'v', MDB_OPT_SETBITS, TRUE, &verbose, TRUE, NULL) != argc)
		return (DCMD_USAGE);

	if (mdb_vread(&ace, sizeof (ace_t), addr) == -1) {
		mdb_warn("failed to read ace_t");
		return (DCMD_ERR);
	}

	if ((ace.a_flags & ACE_TYPE_FLAGS) == 0 ||
	    (ace.a_flags & ACE_TYPE_FLAGS) == ACE_IDENTIFIER_GROUP)
		id = ace.a_who;
	else
		id = -1;

	return (zfs_ace_print_common(addr, flags, id, ace.a_access_mask,
	    ace.a_flags, ace.a_type, verbose));
}

typedef struct acl_dump_args {
	int a_argc;
	const mdb_arg_t *a_argv;
	uint16_t a_version;
	int a_flags;
} acl_dump_args_t;

/* ARGSUSED */
static int
acl_aces_cb(uintptr_t addr, const void *unknown, void *arg)
{
	acl_dump_args_t *acl_args = (acl_dump_args_t *)arg;

	if (acl_args->a_version == 1) {
		if (mdb_call_dcmd("zfs_ace", addr,
		    DCMD_ADDRSPEC|acl_args->a_flags, acl_args->a_argc,
		    acl_args->a_argv) != DCMD_OK) {
			return (WALK_ERR);
		}
	} else {
		if (mdb_call_dcmd("zfs_ace0", addr,
		    DCMD_ADDRSPEC|acl_args->a_flags, acl_args->a_argc,
		    acl_args->a_argv) != DCMD_OK) {
			return (WALK_ERR);
		}
	}
	acl_args->a_flags = DCMD_LOOP;
	return (WALK_NEXT);
}

/* ARGSUSED */
static int
acl_cb(uintptr_t addr, const void *unknown, void *arg)
{
	acl_dump_args_t *acl_args = (acl_dump_args_t *)arg;

	if (acl_args->a_version == 1) {
		if (mdb_pwalk("zfs_acl_node_aces", acl_aces_cb,
		    arg, addr) != 0) {
			mdb_warn("can't walk ACEs");
			return (DCMD_ERR);
		}
	} else {
		if (mdb_pwalk("zfs_acl_node_aces0", acl_aces_cb,
		    arg, addr) != 0) {
			mdb_warn("can't walk ACEs");
			return (DCMD_ERR);
		}
	}
	return (WALK_NEXT);
}

/* ARGSUSED */
static int
zfs_acl_dump(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	zfs_acl_t zacl;
	int verbose = FALSE;
	acl_dump_args_t acl_args;

	if (!(flags & DCMD_ADDRSPEC))
		return (DCMD_USAGE);

	if (mdb_getopts(argc, argv,
	    'v', MDB_OPT_SETBITS, TRUE, &verbose, TRUE, NULL) != argc)
		return (DCMD_USAGE);

	if (mdb_vread(&zacl, sizeof (zfs_acl_t), addr) == -1) {
		mdb_warn("failed to read zfs_acl_t");
		return (DCMD_ERR);
	}

	acl_args.a_argc = argc;
	acl_args.a_argv = argv;
	acl_args.a_version = zacl.z_version;
	acl_args.a_flags = DCMD_LOOPFIRST;

	if (mdb_pwalk("zfs_acl_node", acl_cb, &acl_args, addr) != 0) {
		mdb_warn("can't walk ACL");
		return (DCMD_ERR);
	}

	return (DCMD_OK);
}

/* ARGSUSED */
static int
zfs_acl_node_walk_init(mdb_walk_state_t *wsp)
{
	if (wsp->walk_addr == NULL) {
		mdb_warn("must supply address of zfs_acl_node_t\n");
		return (WALK_ERR);
	}

	wsp->walk_addr += OFFSETOF(zfs_acl_t, z_acl);

	if (mdb_layered_walk("list", wsp) == -1) {
		mdb_warn("failed to walk 'list'\n");
		return (WALK_ERR);
	}

	return (WALK_NEXT);
}

static int
zfs_acl_node_walk_step(mdb_walk_state_t *wsp)
{
	zfs_acl_node_t	aclnode;

	if (mdb_vread(&aclnode, sizeof (zfs_acl_node_t),
	    wsp->walk_addr) == -1) {
		mdb_warn("failed to read zfs_acl_node at %p", wsp->walk_addr);
		return (WALK_ERR);
	}

	return (wsp->walk_callback(wsp->walk_addr, &aclnode, wsp->walk_cbdata));
}

typedef struct ace_walk_data {
	int		ace_count;
	int		ace_version;
} ace_walk_data_t;

static int
zfs_aces_walk_init_common(mdb_walk_state_t *wsp, int version,
    int ace_count, uintptr_t ace_data)
{
	ace_walk_data_t *ace_walk_data;

	if (wsp->walk_addr == NULL) {
		mdb_warn("must supply address of zfs_acl_node_t\n");
		return (WALK_ERR);
	}

	ace_walk_data = mdb_alloc(sizeof (ace_walk_data_t), UM_SLEEP | UM_GC);

	ace_walk_data->ace_count = ace_count;
	ace_walk_data->ace_version = version;

	wsp->walk_addr = ace_data;
	wsp->walk_data = ace_walk_data;

	return (WALK_NEXT);
}

static int
zfs_acl_node_aces_walk_init_common(mdb_walk_state_t *wsp, int version)
{
	static int gotid;
	static mdb_ctf_id_t acl_id;
	int z_ace_count;
	uintptr_t z_acldata;

	if (!gotid) {
		if (mdb_ctf_lookup_by_name("struct zfs_acl_node",
		    &acl_id) == -1) {
			mdb_warn("couldn't find struct zfs_acl_node");
			return (DCMD_ERR);
		}
		gotid = TRUE;
	}

	if (GETMEMBID(wsp->walk_addr, &acl_id, z_ace_count, z_ace_count)) {
		return (DCMD_ERR);
	}
	if (GETMEMBID(wsp->walk_addr, &acl_id, z_acldata, z_acldata)) {
		return (DCMD_ERR);
	}

	return (zfs_aces_walk_init_common(wsp, version,
	    z_ace_count, z_acldata));
}

/* ARGSUSED */
static int
zfs_acl_node_aces_walk_init(mdb_walk_state_t *wsp)
{
	return (zfs_acl_node_aces_walk_init_common(wsp, 1));
}

/* ARGSUSED */
static int
zfs_acl_node_aces0_walk_init(mdb_walk_state_t *wsp)
{
	return (zfs_acl_node_aces_walk_init_common(wsp, 0));
}

static int
zfs_aces_walk_step(mdb_walk_state_t *wsp)
{
	ace_walk_data_t *ace_data = wsp->walk_data;
	zfs_ace_t zace;
	ace_t *acep;
	int status;
	int entry_type;
	int allow_type;
	uintptr_t ptr;

	if (ace_data->ace_count == 0)
		return (WALK_DONE);

	if (mdb_vread(&zace, sizeof (zfs_ace_t), wsp->walk_addr) == -1) {
		mdb_warn("failed to read zfs_ace_t at %#lx",
		    wsp->walk_addr);
		return (WALK_ERR);
	}

	switch (ace_data->ace_version) {
	case 0:
		acep = (ace_t *)&zace;
		entry_type = acep->a_flags & ACE_TYPE_FLAGS;
		allow_type = acep->a_type;
		break;
	case 1:
		entry_type = zace.z_hdr.z_flags & ACE_TYPE_FLAGS;
		allow_type = zace.z_hdr.z_type;
		break;
	default:
		return (WALK_ERR);
	}

	ptr = (uintptr_t)wsp->walk_addr;
	switch (entry_type) {
	case ACE_OWNER:
	case ACE_EVERYONE:
	case (ACE_IDENTIFIER_GROUP | ACE_GROUP):
		ptr += ace_data->ace_version == 0 ?
		    sizeof (ace_t) : sizeof (zfs_ace_hdr_t);
		break;
	case ACE_IDENTIFIER_GROUP:
	default:
		switch (allow_type) {
		case ACE_ACCESS_ALLOWED_OBJECT_ACE_TYPE:
		case ACE_ACCESS_DENIED_OBJECT_ACE_TYPE:
		case ACE_SYSTEM_AUDIT_OBJECT_ACE_TYPE:
		case ACE_SYSTEM_ALARM_OBJECT_ACE_TYPE:
			ptr += ace_data->ace_version == 0 ?
			    sizeof (ace_t) : sizeof (zfs_object_ace_t);
			break;
		default:
			ptr += ace_data->ace_version == 0 ?
			    sizeof (ace_t) : sizeof (zfs_ace_t);
			break;
		}
	}

	ace_data->ace_count--;
	status = wsp->walk_callback(wsp->walk_addr,
	    (void *)(uintptr_t)&zace, wsp->walk_cbdata);

	wsp->walk_addr = ptr;
	return (status);
}

/*
 * MDB module linkage information:
 *
 * We declare a list of structures describing our dcmds, and a function
 * named _mdb_init to return a pointer to our module information.
 */

static const mdb_dcmd_t dcmds[] = {
	{ "arc", "[-bkmg]", "print ARC variables", arc_print },
	{ "blkptr", ":", "print blkptr_t", blkptr },
	{ "dbuf", ":", "print dmu_buf_impl_t", dbuf },
	{ "dbuf_stats", ":", "dbuf stats", dbuf_stats },
	{ "dbufs",
	    "\t[-O objset_t*] [-n objset_name | \"mos\"] "
	    "[-o object | \"mdn\"] \n"
	    "\t[-l level] [-b blkid | \"bonus\"]",
	    "find dmu_buf_impl_t's that match specified criteria", dbufs },
	{ "abuf_find", "dva_word[0] dva_word[1]",
	    "find arc_buf_hdr_t of a specified DVA",
	    abuf_find },
	{ "spa", "?[-cv]", "spa_t summary", spa_print },
	{ "spa_config", ":", "print spa_t configuration", spa_print_config },
	{ "spa_verify", ":", "verify spa_t consistency", spa_verify },
	{ "spa_space", ":[-b]", "print spa_t on-disk space usage", spa_space },
	{ "spa_vdevs", ":", "given a spa_t, print vdev summary", spa_vdevs },
	{ "vdev", ":[-re]\n"
	    "\t-r display recursively\n"
	    "\t-e print statistics",
	    "vdev_t summary", vdev_print },
	{ "zio", ":[cpr]\n"
	    "\t-c display children\n"
	    "\t-p display parents\n"
	    "\t-r display recursively",
	    "zio_t summary", zio_print },
	{ "zio_state", "?", "print out all zio_t structures on system or "
	    "for a particular pool", zio_state },
	{ "zfs_blkstats", ":[-v]",
	    "given a spa_t, print block type stats from last scrub",
	    zfs_blkstats },
	{ "zfs_params", "", "print zfs tunable parameters", zfs_params },
	{ "refcount", "", "print refcount_t holders", refcount },
	{ "zap_leaf", "", "print zap_leaf_phys_t", zap_leaf },
	{ "zfs_aces", ":[-v]", "print all ACEs from a zfs_acl_t",
	    zfs_acl_dump },
	{ "zfs_ace", ":[-v]", "print zfs_ace", zfs_ace_print },
	{ "zfs_ace0", ":[-v]", "print zfs_ace0", zfs_ace0_print },
	{ "sa_attr_table", ":", "print SA attribute table from sa_os_t",
	    sa_attr_table},
	{ "sa_attr", ": attr_id",
	    "print SA attribute address when given sa_handle_t", sa_attr_print},
	{ "zfs_dbgmsg", ":[-v]",
	    "print zfs debug log", dbgmsg},
	{ NULL }
};

static const mdb_walker_t walkers[] = {
	{ "zms_freelist", "walk ZFS metaslab freelist",
		freelist_walk_init, freelist_walk_step, NULL },
	{ "txg_list", "given any txg_list_t *, walk all entries in all txgs",
		txg_list_walk_init, txg_list_walk_step, NULL },
	{ "txg_list0", "given any txg_list_t *, walk all entries in txg 0",
		txg_list0_walk_init, txg_list_walk_step, NULL },
	{ "txg_list1", "given any txg_list_t *, walk all entries in txg 1",
		txg_list1_walk_init, txg_list_walk_step, NULL },
	{ "txg_list2", "given any txg_list_t *, walk all entries in txg 2",
		txg_list2_walk_init, txg_list_walk_step, NULL },
	{ "txg_list3", "given any txg_list_t *, walk all entries in txg 3",
		txg_list3_walk_init, txg_list_walk_step, NULL },
	{ "zio", "walk all zio structures, optionally for a particular spa_t",
		zio_walk_init, zio_walk_step, NULL },
	{ "zio_root", "walk all root zio_t structures, optionally for a "
	    "particular spa_t",
		zio_walk_init, zio_walk_root_step, NULL },
	{ "spa", "walk all spa_t entries in the namespace",
		spa_walk_init, spa_walk_step, NULL },
	{ "metaslab", "given a spa_t *, walk all metaslab_t structures",
		metaslab_walk_init, metaslab_walk_step, NULL },
	{ "zfs_acl_node", "given a zfs_acl_t, walk all zfs_acl_nodes",
	    zfs_acl_node_walk_init, zfs_acl_node_walk_step, NULL },
	{ "zfs_acl_node_aces", "given a zfs_acl_node_t, walk all ACEs",
	    zfs_acl_node_aces_walk_init, zfs_aces_walk_step, NULL },
	{ "zfs_acl_node_aces0",
	    "given a zfs_acl_node_t, walk all ACEs as ace_t",
	    zfs_acl_node_aces0_walk_init, zfs_aces_walk_step, NULL },
	{ NULL }
};

static const mdb_modinfo_t modinfo = {
	MDB_API_VERSION, dcmds, walkers
};

const mdb_modinfo_t *
_mdb_init(void)
{
	return (&modinfo);
}
