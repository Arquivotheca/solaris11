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

#include <stdio.h>
#include <stdio_ext.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/dmu.h>
#include <sys/zap.h>
#include <sys/fs/zfs.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_sa.h>
#include <sys/sa.h>
#include <sys/sa_impl.h>
#include <sys/vdev.h>
#include <sys/vdev_impl.h>
#include <sys/metaslab_impl.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_pool.h>
#include <sys/dbuf.h>
#include <sys/zil.h>
#include <sys/zil_impl.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/dmu_traverse.h>
#include <sys/zio.h>
#include <sys/zio_checksum.h>
#include <sys/zio_compress.h>
#include <sys/zfs_fuid.h>
#include <sys/arc.h>
#include <sys/ddt.h>
#undef ZFS_MAXNAMELEN
#undef verify
#include <libzfs.h>

#define	ZDB_COMPRESS_NAME(idx) ((idx) < ZIO_COMPRESS_FUNCTIONS ? \
    zio_compress_table[(idx)].ci_name : "UNKNOWN")
#define	ZDB_CHECKSUM_NAME(idx) ((idx) < ZIO_CHECKSUM_FUNCTIONS ? \
    zio_checksum_table[(idx)].ci_name : "UNKNOWN")
#define	ZDB_OT_NAME(idx) ((idx) < DMU_OT_NUMTYPES ? \
    dmu_ot[(idx)].ot_name : "UNKNOWN")
#define	ZDB_OT_TYPE(idx) ((idx) < DMU_OT_NUMTYPES ? (idx) : DMU_OT_NUMTYPES)

#ifndef lint
extern int zfs_recover;
#else
int zfs_recover;
#endif

const char cmdname[] = "zdb";
uint8_t dump_opt[256];

typedef void object_viewer_t(objset_t *, uint64_t, void *data, size_t size);

extern void dump_intent_log(zilog_t *);
uint64_t *zopt_object = NULL;
int zopt_objects = 0;
libzfs_handle_t *g_zfs;

/*
 * These libumem hooks provide a reasonable set of defaults for the allocator's
 * debugging facilities.
 */
const char *
_umem_debug_init()
{
	return ("default,verbose"); /* $UMEM_DEBUG setting */
}

const char *
_umem_logging_init(void)
{
	return ("fail,contents"); /* $UMEM_LOGGING setting */
}

char *mos_opts[] = {
#define	MOS_OPT_OBJSET		0
	"objset",
#define	MOS_OPT_DIR		1
	"dir",
#define	MOS_OPT_POOL_PROPS	2
	DMU_POOL_PROPS,
#define	MOS_OPT_METASLAB	3
	"metaslab",
#define	MOS_OPT_SYNC_BPOBJ	4
	DMU_POOL_SYNC_BPOBJ,
#define	MOS_OPT_DTL		5
	"dtl",
#define	MOS_OPT_CONFIG		6
	DMU_POOL_CONFIG,
#define	MOS_OPT_SPARES		7
	DMU_POOL_SPARES,
#define	MOS_OPT_L2CACHE		8
	DMU_POOL_L2CACHE,
#define	MOS_OPT_HISTORY		9
	DMU_POOL_HISTORY,
#define	MOS_OPT_ERRLOG_SCRUB	10
	DMU_POOL_ERRLOG_SCRUB,
#define	MOS_OPT_ERRLOG_LAST	11
	DMU_POOL_ERRLOG_LAST,
#define	MOS_OPT_ALL		12	/* end of 'all' */
#define	MOS_OPT_RAW_CONFIG	12
	"raw_config",
#define	MOS_OPTS		13	/* must be last */
	NULL,
};

static void
usage(void)
{
	(void) fprintf(stderr,
	    "Usage: %s [-bcdhimrsuvCDL] [-M <what>] poolname [object...]\n"
	    "       %s [-div] dataset [object...]\n"
	    "       %s -m [-L] poolname [vdev [metaslab...]]\n"
	    "       %s -R poolname vdev:offset:size[:flags]\n"
	    "       %s -S poolname\n"
	    "       %s -l [-cu] device\n"
	    "       %s -C\n\n",
	    cmdname, cmdname, cmdname, cmdname, cmdname, cmdname, cmdname);

	(void) fprintf(stderr, "    Dataset name must include at least one "
	    "separator character '/' or '@'\n");
	(void) fprintf(stderr, "    Root dataset can be dumped by specifying "
	    "option '-d' explicitly\n");
	(void) fprintf(stderr, "    If dataset name is specified, only that "
	    "dataset is dumped\n");
	(void) fprintf(stderr, "    If object numbers are specified, only "
	    "those objects are dumped\n\n");
	(void) fprintf(stderr, "    Options to control amount of output:\n");
	(void) fprintf(stderr, "        -b block statistics\n");
	(void) fprintf(stderr, "        -c checksum all metadata (twice for "
	    "all data) blocks\n");
	(void) fprintf(stderr, "        -d dataset(s)\n");
	(void) fprintf(stderr, "        -h pool history\n");
	(void) fprintf(stderr, "        -i intent logs\n");
	(void) fprintf(stderr, "        -l dump label contents\n");
	(void) fprintf(stderr, "        -m metaslabs\n");
	(void) fprintf(stderr, "        -r dump datasets recursively\n");
	(void) fprintf(stderr, "        -s report stats on zdb's I/O\n");
	(void) fprintf(stderr, "        -u uberblock\n");
	(void) fprintf(stderr, "        -v verbose (applies to all others)\n");
	(void) fprintf(stderr, "        -C config (or cachefile if alone)\n");
	(void) fprintf(stderr, "        -D dedup statistics\n");
	(void) fprintf(stderr, "        -L disable leak tracking (do not "
	    "load spacemaps)\n");
	(void) fprintf(stderr, "        -M <what> -- dump MOS contents; "
	    "<what> is '%s' or 'all' or \n", mos_opts[MOS_OPT_RAW_CONFIG]);
	(void) fprintf(stderr, "           a comma-separated list of one "
	    "or more of:");
#define	WIDTH	76
#define	PADDING	"             "
	for (int len = WIDTH, i = 0; i < MOS_OPT_ALL; i++) {
		if (len + strlen(mos_opts[i]) + 2 > WIDTH) {
			(void) fprintf(stderr, "\n%s", PADDING);
			len = strlen(PADDING);
		}
		(void) fprintf(stderr, (i < MOS_OPT_ALL - 1) ? "%s, " : "%s",
		    mos_opts[i]);
		len += strlen(mos_opts[i]) + 2;
	}
	(void) fprintf(stderr, "\n");
	(void) fprintf(stderr, "        -S simulate dedup to measure effect\n");
	(void) fprintf(stderr, "        -v verbose (applies to all others)\n");
	(void) fprintf(stderr, "        -l dump label contents\n");
	(void) fprintf(stderr, "        -L disable leak tracking (do not "
	    "load spacemaps and deferred frees)\n");
	(void) fprintf(stderr, "        -R read and display block from a "
	    "device\n\n");
	(void) fprintf(stderr, "    Below options are intended for use "
	    "with other options (except -l):\n");
	(void) fprintf(stderr, "        -A ignore assertions (-A), enable "
	    "panic recovery (-AA) or both (-AAA)\n");
	(void) fprintf(stderr, "        -F attempt automatic rewind within "
	    "safe range of transaction groups\n");
	(void) fprintf(stderr, "        -I ignore errors while dumping "
	    "datasets and objects\n");
	(void) fprintf(stderr, "        -U <full path> -- use alternate "
	    "cachefile; works with -e too\n");
	(void) fprintf(stderr, "        -X attempt extreme rewind (does not "
	    "work with dataset)\n");
	(void) fprintf(stderr, "        -e pool is exported/destroyed/"
	    "has altroot/not in a default cachefile\n");
	(void) fprintf(stderr, "        -p <path> -- use one or more with "
	    "-e to specify path to vdev dir\n");
	(void) fprintf(stderr, "	-P print numbers parsable\n");
	(void) fprintf(stderr, "        -t <txg> -- highest txg to use when "
	    "searching for uberblocks\n");
	(void) fprintf(stderr, "Specify an option more than once (e.g. -bb) "
	    "to make only that option verbose\n");
	(void) fprintf(stderr, "Default is to dump everything non-verbosely\n");
	exit(1);
}

/*
 * Called for usage errors that are discovered after a call to spa_open(),
 * dmu_bonus_hold(), or pool_match().  abort() is called for other errors.
 */
static void
zdb_err(int level, const char *fmt, ...)
{
	va_list ap;
	FILE *out = (level != CE_PANIC && dump_opt['I']) ? stdout : stderr;

	if (level == CE_PANIC || !dump_opt['I'])
		(void) fprintf(out, "%s: ", cmdname);

	va_start(ap, fmt);
	(void) vfprintf(out, fmt, ap);
	va_end(ap);

	if (level == CE_NOTE || level != CE_PANIC && dump_opt['I'])
		return;

	(void) fprintf(out, "\n");
	exit(1);
}

static nvlist_t *
load_packed_nvlist(objset_t *os, uint64_t object, size_t nvsize)
{
	nvlist_t *nv;
	char *packed = umem_alloc(nvsize, UMEM_NOFAIL);
	int err = dmu_read(os, object, 0, nvsize, packed, DMU_READ_PREFETCH);

	if (err) {
		zdb_err(CE_WARN, "dmu_read(%llu) failed, errno %d: %s\n",
		    (u_longlong_t)object, err, strerror(err));
		umem_free(packed, nvsize);
		return (NULL);
	}

	if ((err = nvlist_unpack(packed, nvsize, &nv, 0)) != 0) {
		zdb_err(CE_WARN, "nvlist_unpack() failed, errno %d: %s\n",
		    err, strerror(err));
		umem_free(packed, nvsize);
		return (NULL);
	}
	umem_free(packed, nvsize);

	return (nv);
}

/* ARGSUSED */
static void
dump_packed_nvlist(objset_t *os, uint64_t object, void *data, size_t size)
{
	nvlist_t *nv = load_packed_nvlist(os, object, *(uint64_t *)data);
	dump_nvlist(nv, 8);
	nvlist_free(nv);
}

static void
zdb_nicenum(uint64_t num, char *buf)
{
	if (dump_opt['P'])
		(void) sprintf(buf, "%llu", (longlong_t)num);
	else
		nicenum(num, buf);
}

const char dump_zap_stars[] = "****************************************";
const int dump_zap_width = sizeof (dump_zap_stars) - 1;

static void
dump_zap_histogram(uint64_t histo[ZAP_HISTOGRAM_SIZE])
{
	int i;
	int minidx = ZAP_HISTOGRAM_SIZE - 1;
	int maxidx = 0;
	uint64_t max = 0;

	for (i = 0; i < ZAP_HISTOGRAM_SIZE; i++) {
		if (histo[i] > max)
			max = histo[i];
		if (histo[i] > 0 && i > maxidx)
			maxidx = i;
		if (histo[i] > 0 && i < minidx)
			minidx = i;
	}

	if (max < dump_zap_width)
		max = dump_zap_width;

	for (i = minidx; i <= maxidx; i++)
		(void) printf("\t\t\t%u: %6llu %s\n", i, (u_longlong_t)histo[i],
		    &dump_zap_stars[(max - histo[i]) * dump_zap_width / max]);
}

static void
dump_zap_stats(objset_t *os, uint64_t object)
{
	int error;
	zap_stats_t zs;

	error = zap_get_stats(os, object, &zs);
	if (error)
		return;

	if (zs.zs_ptrtbl_len == 0) {
		ASSERT(zs.zs_num_blocks == 1);
		(void) printf("\tmicrozap: %llu bytes, %llu entries\n",
		    (u_longlong_t)zs.zs_blocksize,
		    (u_longlong_t)zs.zs_num_entries);
		return;
	}

	(void) printf("\tFat ZAP stats:\n");

	(void) printf("\t\tPointer table:\n");
	(void) printf("\t\t\t%llu elements\n",
	    (u_longlong_t)zs.zs_ptrtbl_len);
	(void) printf("\t\t\tzt_blk: %llu\n",
	    (u_longlong_t)zs.zs_ptrtbl_zt_blk);
	(void) printf("\t\t\tzt_numblks: %llu\n",
	    (u_longlong_t)zs.zs_ptrtbl_zt_numblks);
	(void) printf("\t\t\tzt_shift: %llu\n",
	    (u_longlong_t)zs.zs_ptrtbl_zt_shift);
	(void) printf("\t\t\tzt_blks_copied: %llu\n",
	    (u_longlong_t)zs.zs_ptrtbl_blks_copied);
	(void) printf("\t\t\tzt_nextblk: %llu\n",
	    (u_longlong_t)zs.zs_ptrtbl_nextblk);

	(void) printf("\t\tZAP entries: %llu\n",
	    (u_longlong_t)zs.zs_num_entries);
	(void) printf("\t\tLeaf blocks: %llu\n",
	    (u_longlong_t)zs.zs_num_leafs);
	(void) printf("\t\tTotal blocks: %llu\n",
	    (u_longlong_t)zs.zs_num_blocks);
	(void) printf("\t\tzap_block_type: 0x%llx\n",
	    (u_longlong_t)zs.zs_block_type);
	(void) printf("\t\tzap_magic: 0x%llx\n",
	    (u_longlong_t)zs.zs_magic);
	(void) printf("\t\tzap_salt: 0x%llx\n",
	    (u_longlong_t)zs.zs_salt);

	(void) printf("\t\tLeafs with 2^n pointers:\n");
	dump_zap_histogram(zs.zs_leafs_with_2n_pointers);

	(void) printf("\t\tBlocks with n*5 entries:\n");
	dump_zap_histogram(zs.zs_blocks_with_n5_entries);

	(void) printf("\t\tBlocks n/10 full:\n");
	dump_zap_histogram(zs.zs_blocks_n_tenths_full);

	(void) printf("\t\tEntries with n chunks:\n");
	dump_zap_histogram(zs.zs_entries_using_n_chunks);

	(void) printf("\t\tBuckets with n entries:\n");
	dump_zap_histogram(zs.zs_buckets_with_n_entries);
}

static void
dump_zap_contents(objset_t *os, uint64_t object, const char *header)
{
	zap_cursor_t zc;
	zap_attribute_t attr;
	void *prop;

	(void) printf("%s", header);

	for (zap_cursor_init(&zc, os, object);
	    zap_cursor_retrieve(&zc, &attr) == 0;
	    zap_cursor_advance(&zc)) {
		(void) printf("\t\t%s = ", attr.za_name);
		if (attr.za_num_integers == 0) {
			(void) printf("\n");
			continue;
		}
		prop = umem_zalloc(attr.za_num_integers *
		    attr.za_integer_length, UMEM_NOFAIL);
		(void) zap_lookup(os, object, attr.za_name,
		    attr.za_integer_length, attr.za_num_integers, prop);
		if (attr.za_integer_length == 1) {
			(void) printf("%s", (char *)prop);
		} else {
			for (int i = 0; i < attr.za_num_integers; i++) {
				switch (attr.za_integer_length) {
				case 2:
					(void) printf("%u ",
					    ((uint16_t *)prop)[i]);
					break;
				case 4:
					(void) printf("%u ",
					    ((uint32_t *)prop)[i]);
					break;
				case 8:
					(void) printf("%lld ",
					    (u_longlong_t)((int64_t *)prop)[i]);
					break;
				}
			}
		}
		(void) printf("\n");
		umem_free(prop, attr.za_num_integers * attr.za_integer_length);
	}
	zap_cursor_fini(&zc);
}

/*ARGSUSED*/
static void
dump_none(objset_t *os, uint64_t object, void *data, size_t size)
{
}

/*ARGSUSED*/
static void
dump_unknown(objset_t *os, uint64_t object, void *data, size_t size)
{
	zdb_err(CE_WARN, "\tUNKNOWN OBJECT TYPE\n");
}

/*ARGSUSED*/
void
dump_uint8(objset_t *os, uint64_t object, void *data, size_t size)
{
}

/*ARGSUSED*/
static void
dump_uint64(objset_t *os, uint64_t object, void *data, size_t size)
{
}

/*ARGSUSED*/
static void
dump_zap(objset_t *os, uint64_t object, void *data, size_t size)
{
	dump_zap_stats(os, object);
	dump_zap_contents(os, object, "\n");
}

/*ARGSUSED*/
static void
dump_ddt_zap(objset_t *os, uint64_t object, void *data, size_t size)
{
	dump_zap_stats(os, object);
	/* contents are printed elsewhere, properly decoded */
}

/*ARGSUSED*/
static void
dump_keychain_zap(objset_t *os, uint64_t object, void *data, size_t size)
{
	zap_cursor_t zc;
	zap_attribute_t attr;

	dump_zap_stats(os, object);
	(void) printf("\tKeychain entries by txg:\n");

	for (zap_cursor_init(&zc, os, object);
	    zap_cursor_retrieve(&zc, &attr) == 0;
	    zap_cursor_advance(&zc)) {
		/*LINTED E_BAD_PTR_CAST_ALIGN */
		u_longlong_t txg = *(uint64_t *)attr.za_name;
		(void) printf("\t\ttxg %llu : wkeylen = %llu\n",
		    txg, (u_longlong_t)attr.za_num_integers);
	}
	zap_cursor_fini(&zc);

}

/*ARGSUSED*/
static void
dump_sa_attrs(objset_t *os, uint64_t object, void *data, size_t size)
{
	zap_cursor_t zc;
	zap_attribute_t attr;

	dump_zap_stats(os, object);
	(void) printf("\n");

	for (zap_cursor_init(&zc, os, object);
	    zap_cursor_retrieve(&zc, &attr) == 0;
	    zap_cursor_advance(&zc)) {
		(void) printf("\t\t%s = ", attr.za_name);
		if (attr.za_num_integers == 0) {
			(void) printf("\n");
			continue;
		}
		(void) printf(" %llx : [%d:%d:%d]\n",
		    (u_longlong_t)attr.za_first_integer,
		    (int)ATTR_LENGTH(attr.za_first_integer),
		    (int)ATTR_BSWAP(attr.za_first_integer),
		    (int)ATTR_NUM(attr.za_first_integer));
	}
	zap_cursor_fini(&zc);
}

/*ARGSUSED*/
static void
dump_sa_layouts(objset_t *os, uint64_t object, void *data, size_t size)
{
	zap_cursor_t zc;
	zap_attribute_t attr;
	uint16_t *layout_attrs;
	int i;

	dump_zap_stats(os, object);
	(void) printf("\n");

	for (zap_cursor_init(&zc, os, object);
	    zap_cursor_retrieve(&zc, &attr) == 0;
	    zap_cursor_advance(&zc)) {
		(void) printf("\t\t%s = [", attr.za_name);
		if (attr.za_num_integers == 0) {
			(void) printf("\n");
			continue;
		}

		VERIFY(attr.za_integer_length == 2);
		layout_attrs = umem_zalloc(attr.za_num_integers *
		    attr.za_integer_length, UMEM_NOFAIL);

		VERIFY(zap_lookup(os, object, attr.za_name,
		    attr.za_integer_length,
		    attr.za_num_integers, layout_attrs) == 0);

		for (i = 0; i != attr.za_num_integers; i++)
			(void) printf(" %d ", (int)layout_attrs[i]);
		(void) printf("]\n");
		umem_free(layout_attrs,
		    attr.za_num_integers * attr.za_integer_length);
	}
	zap_cursor_fini(&zc);
}

/*ARGSUSED*/
static void
dump_zpldir(objset_t *os, uint64_t object, void *data, size_t size)
{
	zap_cursor_t zc;
	zap_attribute_t attr;
	const char *typenames[] = {
		/* 0 */ "not specified",
		/* 1 */ "FIFO",
		/* 2 */ "Character Device",
		/* 3 */ "3 (invalid)",
		/* 4 */ "Directory",
		/* 5 */ "5 (invalid)",
		/* 6 */ "Block Device",
		/* 7 */ "7 (invalid)",
		/* 8 */ "Regular File",
		/* 9 */ "9 (invalid)",
		/* 10 */ "Symbolic Link",
		/* 11 */ "11 (invalid)",
		/* 12 */ "Socket",
		/* 13 */ "Door",
		/* 14 */ "Event Port",
		/* 15 */ "15 (invalid)",
	};

	dump_zap_stats(os, object);
	(void) printf("\n");

	for (zap_cursor_init(&zc, os, object);
	    zap_cursor_retrieve(&zc, &attr) == 0;
	    zap_cursor_advance(&zc)) {
		(void) printf("\t\t%s = %lld (type: %s)\n",
		    attr.za_name, ZFS_DIRENT_OBJ(attr.za_first_integer),
		    typenames[ZFS_DIRENT_TYPE(attr.za_first_integer)]);
	}
	zap_cursor_fini(&zc);
}

static void
dump_spacemap(objset_t *os, space_map_obj_t *smo, space_map_t *sm)
{
	uint64_t alloc, offset, entry;
	uint8_t mapshift = sm->sm_shift;
	uint64_t mapstart = sm->sm_start;
	uint64_t dumped = 0;
	char *ddata[] = { "ALLOC", "FREE", "CONDENSE", "INVALID",
			    "INVALID", "INVALID", "INVALID", "INVALID" };

	if (smo->smo_object == 0)
		return;

	/*
	 * Print out the freelist entries in both encoded and decoded form.
	 */
	alloc = 0;
	for (offset = 0; offset < smo->smo_objsize; offset += sizeof (entry)) {
		int err = dmu_read(os, smo->smo_object, offset, sizeof (entry),
		    &entry, DMU_READ_PREFETCH);
		if (err) {
			zdb_err(CE_WARN, "\t    [%6llu] ERROR: dmu_read(%llu) "
			    "failed, errno %d: %s\n",
			    (u_longlong_t)(offset / sizeof (entry)),
			    (u_longlong_t)smo->smo_object, err, strerror(err));
			continue;
		}
		if (SM_DEBUG_DECODE(entry)) {
			(void) printf("\t    [%6llu] %s: txg %llu, pass %llu\n",
			    (u_longlong_t)(offset / sizeof (entry)),
			    ddata[SM_DEBUG_ACTION_DECODE(entry)],
			    (u_longlong_t)SM_DEBUG_TXG_DECODE(entry),
			    (u_longlong_t)SM_DEBUG_SYNCPASS_DECODE(entry));
		} else {
			(void) printf("\t    [%6llu]    %c  range:"
			    " %010llx-%010llx  size: %06llx\n",
			    (u_longlong_t)(offset / sizeof (entry)),
			    SM_TYPE_DECODE(entry) == SM_ALLOC ? 'A' : 'F',
			    (u_longlong_t)((SM_OFFSET_DECODE(entry) <<
			    mapshift) + mapstart),
			    (u_longlong_t)((SM_OFFSET_DECODE(entry) <<
			    mapshift) + mapstart + (SM_RUN_DECODE(entry) <<
			    mapshift)),
			    (u_longlong_t)(SM_RUN_DECODE(entry) << mapshift));
			if (SM_TYPE_DECODE(entry) == SM_ALLOC)
				alloc += SM_RUN_DECODE(entry) << mapshift;
			else
				alloc -= SM_RUN_DECODE(entry) << mapshift;
		}
		dumped++;
	}
	if (dumped != smo->smo_objsize / sizeof (entry)) {
		zdb_err(CE_WARN, "space_map_object has %llu "
		    "CORRUPTED entries\n",
		    (u_longlong_t)(smo->smo_objsize / sizeof (entry) - dumped));
	}
	if (alloc != smo->smo_alloc) {
		zdb_err(CE_WARN, "space_map_object alloc (%llu) "
		    "INCONSISTENT with space map summary (%llu)\n",
		    (u_longlong_t)smo->smo_alloc, (u_longlong_t)alloc);
	}
}

static void
dump_metaslab_stats(metaslab_t *msp)
{
	char maxbuf[32];
	space_map_t *sm = &msp->ms_map;
	avl_tree_t *t = sm->sm_pp_root;
	int free_pct = sm->sm_space * 100 / sm->sm_size;

	zdb_nicenum(space_map_maxsize(sm), maxbuf);

	(void) printf("\t %25s %10lu   %7s  %6s   %4s %4d%%\n",
	    "segments", avl_numnodes(t), "maxsize", maxbuf,
	    "freepct", free_pct);
}

static void
dump_metaslab(metaslab_t *msp)
{
	vdev_t *vd = msp->ms_group->mg_vd;
	spa_t *spa = vd->vdev_spa;
	space_map_t *sm = &msp->ms_map;
	space_map_obj_t *smo = &msp->ms_smo;
	char freebuf[32];
	int verbose = MAX(dump_opt['M'], dump_opt['m']);

	zdb_nicenum(sm->sm_size - smo->smo_alloc, freebuf);

	(void) printf(
	    "\tmetaslab %6llu   offset %12llx   spacemap %6llu   free    %5s\n",
	    (u_longlong_t)(sm->sm_start / sm->sm_size),
	    (u_longlong_t)sm->sm_start, (u_longlong_t)smo->smo_object, freebuf);

	if (verbose > 1 && !dump_opt['L']) {
		int err = 0;

		mutex_enter(&msp->ms_lock);
		space_map_load_wait(sm);
		if (!sm->sm_loaded)
			err = space_map_load(sm, zfs_metaslab_ops,
			    SM_FREE, smo, spa->spa_meta_objset);
		if (err) {
			zdb_err(CE_WARN, "space_map_load(%llu) failed, "
			    "errno %d: %s\n", (u_longlong_t)smo->smo_object,
			    err, strerror(err));
			mutex_exit(&msp->ms_lock);
			return;
		}
		dump_metaslab_stats(msp);
		space_map_unload(sm);
		mutex_exit(&msp->ms_lock);
	}
	if (verbose > 2) {
		ASSERT(sm->sm_size == (1ULL << vd->vdev_ms_shift));

		mutex_enter(&msp->ms_lock);
		dump_spacemap(spa->spa_meta_objset, smo, sm);
		mutex_exit(&msp->ms_lock);
	}
}

static void
print_vdev_metaslab_header(vdev_t *vd)
{
	(void) printf("\tvdev %10llu   ms_array %10llu\n"
	    "\t%-10s%5llu   %-19s   %-15s   %-10s\n",
	    (u_longlong_t)vd->vdev_id, (u_longlong_t)vd->vdev_ms_array,
	    "metaslabs", (u_longlong_t)vd->vdev_ms_count,
	    "offset", "spacemap", "free");
	(void) printf("\t%15s   %19s   %15s   %10s\n",
	    "---------------", "-------------------",
	    "---------------", "-------------");
}

static void
dump_metaslabs(spa_t *spa)
{
	vdev_t *vd, *rvd = spa->spa_root_vdev;
	uint64_t m, c = 0, children = rvd->vdev_children;

	(void) printf("\nMetaslabs:\n");

	if (!dump_opt['M'] && zopt_objects > 0) {
		c = zopt_object[0];

		if (c >= children)
			zdb_err(CE_PANIC, "bad vdev id: %llu", (u_longlong_t)c);

		if (zopt_objects > 1) {
			vd = rvd->vdev_child[c];
			print_vdev_metaslab_header(vd);

			for (m = 1; m < zopt_objects; m++) {
				if (zopt_object[m] < vd->vdev_ms_count)
					dump_metaslab(
					    vd->vdev_ms[zopt_object[m]]);
				else
					zdb_err(CE_WARN, "bad metaslab "
					    "number %llu\n",
					    (u_longlong_t)zopt_object[m]);
			}
			(void) printf("\n");
			return;
		}
		children = c + 1;
	}
	for (; c < children; c++) {
		vd = rvd->vdev_child[c];
		print_vdev_metaslab_header(vd);

		for (m = 0; m < vd->vdev_ms_count; m++)
			dump_metaslab(vd->vdev_ms[m]);
		(void) printf("\n");
	}
}

static void
dump_dde(const ddt_t *ddt, const ddt_entry_t *dde, uint64_t index)
{
	const ddt_phys_t *ddp = dde->dde_phys;
	const ddt_key_t *ddk = &dde->dde_key;
	char *types[4] = { "ditto", "single", "double", "triple" };
	char blkbuf[BP_SPRINTF_LEN];
	blkptr_t blk;

	for (int p = 0; p < DDT_PHYS_TYPES; p++, ddp++) {
		if (ddp->ddp_phys_birth == 0)
			continue;
		ddt_bp_create(ddt->ddt_checksum, ddk, ddp, &blk);
		sprintf_blkptr(blkbuf, &blk);
		(void) printf("index %llx refcnt %llu %s %s\n",
		    (u_longlong_t)index, (u_longlong_t)ddp->ddp_refcnt,
		    types[p], blkbuf);
	}
}

static void
dump_dedup_ratio(const ddt_stat_t *dds)
{
	double rL, rP, rD, D, dedup, compress, copies;

	if (dds->dds_blocks == 0)
		return;

	rL = (double)dds->dds_ref_lsize;
	rP = (double)dds->dds_ref_psize;
	rD = (double)dds->dds_ref_dsize;
	D = (double)dds->dds_dsize;

	dedup = rD / D;
	compress = rL / rP;
	copies = rD / rP;

	(void) printf("dedup = %.2f, compress = %.2f, copies = %.2f, "
	    "dedup * compress / copies = %.2f\n\n",
	    dedup, compress, copies, dedup * compress / copies);
}

static void
dump_ddt(ddt_t *ddt, enum ddt_type type, enum ddt_class class)
{
	char name[DDT_NAMELEN];
	ddt_entry_t dde;
	uint64_t walk = 0;
	dmu_object_info_t doi;
	uint64_t count, dspace, mspace;
	int error;

	error = ddt_object_info(ddt, type, class, &doi);

	if (error == ENOENT)
		return;
	ASSERT(error == 0);

	if ((count = ddt_object_count(ddt, type, class)) == 0)
		return;

	dspace = doi.doi_physical_blocks_512 << 9;
	mspace = doi.doi_fill_count * doi.doi_data_block_size;

	ddt_object_name(ddt, type, class, name);

	(void) printf("%s: %llu entries, size %llu on disk, %llu in core\n",
	    name,
	    (u_longlong_t)count,
	    (u_longlong_t)(dspace / count),
	    (u_longlong_t)(mspace / count));

	if (dump_opt['D'] < 3)
		return;

	zpool_dump_ddt(NULL, &ddt->ddt_histogram[type][class]);

	if (dump_opt['D'] < 4)
		return;

	if (dump_opt['D'] < 5 && class == DDT_CLASS_UNIQUE)
		return;

	(void) printf("%s contents:\n\n", name);

	while ((error = ddt_object_walk(ddt, type, class, &walk, &dde)) == 0)
		dump_dde(ddt, &dde, walk);

	ASSERT(error == ENOENT);

	(void) printf("\n");
}

static void
dump_all_ddts(spa_t *spa)
{
	ddt_histogram_t ddh_total = { 0 };
	ddt_stat_t dds_total = { 0 };

	for (enum zio_checksum c = 0; c < ZIO_CHECKSUM_FUNCTIONS; c++) {
		ddt_t *ddt = spa->spa_ddt[c];
		for (enum ddt_type type = 0; type < DDT_TYPES; type++) {
			for (enum ddt_class class = 0; class < DDT_CLASSES;
			    class++) {
				dump_ddt(ddt, type, class);
			}
		}
	}

	ddt_get_dedup_stats(spa, &dds_total);

	if (dds_total.dds_blocks == 0) {
		(void) printf("All DDTs are empty\n");
		return;
	}

	(void) printf("\n");

	if (dump_opt['D'] > 1) {
		(void) printf("DDT histogram (aggregated over all DDTs):\n");
		ddt_get_dedup_histogram(spa, &ddh_total);
		zpool_dump_ddt(&dds_total, &ddh_total);
	}

	dump_dedup_ratio(&dds_total);
}

static void
dump_dtl_seg(space_map_t *sm, uint64_t start, uint64_t size)
{
	char *prefix = (void *)sm;

	(void) printf("%s [%llu,%llu) length %llu\n",
	    prefix,
	    (u_longlong_t)start,
	    (u_longlong_t)(start + size),
	    (u_longlong_t)(size));
}

static void
dump_dtl(vdev_t *vd, int indent)
{
	spa_t *spa = vd->vdev_spa;
	boolean_t required;
	char *name[DTL_TYPES] = { "missing", "partial", "scrub", "outage" };
	char prefix[256];

	spa_vdev_state_enter(spa, SCL_NONE);
	required = vdev_dtl_required(vd);
	(void) spa_vdev_state_exit(spa, NULL, 0);

	if (indent == 0)
		(void) printf("\nDirty time logs:\n");

	(void) printf("\t%*s%s [%s]\n", indent, "",
	    vd->vdev_path ? vd->vdev_path :
	    vd->vdev_parent ? vd->vdev_ops->vdev_op_type : spa_name(spa),
	    required ? "DTL-required" : "DTL-expendable");

	for (int t = 0; t < DTL_TYPES; t++) {
		space_map_t *sm = &vd->vdev_dtl[t];
		if (sm->sm_space == 0)
			continue;
		(void) snprintf(prefix, sizeof (prefix), "\t%*s%s",
		    indent + 2, "", name[t]);
		mutex_enter(sm->sm_lock);
		space_map_walk(sm, dump_dtl_seg, (void *)prefix);
		mutex_exit(sm->sm_lock);
		if (t == DTL_MISSING && dump_opt['M'] > 5 &&
		    vd->vdev_children == 0)
			dump_spacemap(spa->spa_meta_objset,
			    &vd->vdev_dtl_smo, sm);
	}
	for (int c = 0; c < vd->vdev_children; c++)
		dump_dtl(vd->vdev_child[c], indent + 4);

	if (indent == 0)
		(void) printf("\n");
}

static void
dump_history(spa_t *spa)
{
	nvlist_t **events = NULL;
	char buf[SPA_MAXBLOCKSIZE];
	uint64_t resid, len, off = 0;
	uint_t num = 0;
	int error;
	time_t tsec;
	struct tm t;
	char tbuf[30];
	char internalstr[MAXPATHLEN];

	do {
		len = sizeof (buf);

		if ((error = spa_history_get(spa, &off, &len, buf)) != 0) {
			zdb_err(CE_WARN, "Unable to read history: "
			    "error %d\n", error);
			return;
		}

		if (zpool_history_unpack(buf, len, &resid, &events, &num) != 0)
			break;

		off -= resid;
	} while (len != 0);

	(void) printf("\nHistory:\n");
	for (int i = 0; i < num; i++) {
		uint64_t time, txg, ievent;
		char *cmd, *intstr;

		if (nvlist_lookup_uint64(events[i], ZPOOL_HIST_TIME,
		    &time) != 0)
			continue;
		if (nvlist_lookup_string(events[i], ZPOOL_HIST_CMD,
		    &cmd) != 0) {
			if (nvlist_lookup_uint64(events[i],
			    ZPOOL_HIST_INT_EVENT, &ievent) != 0)
				continue;
			verify(nvlist_lookup_uint64(events[i],
			    ZPOOL_HIST_TXG, &txg) == 0);
			verify(nvlist_lookup_string(events[i],
			    ZPOOL_HIST_INT_STR, &intstr) == 0);
			if (ievent >= LOG_END)
				continue;

			(void) snprintf(internalstr,
			    sizeof (internalstr),
			    "[internal %s txg:%lld] %s",
			    zfs_history_event_names[ievent], txg,
			    intstr);
			cmd = internalstr;
		}
		tsec = time;
		(void) localtime_r(&tsec, &t);
		(void) strftime(tbuf, sizeof (tbuf), "%F.%T", &t);
		(void) printf("%s %s\n", tbuf, cmd);
	}
}

/*ARGSUSED*/
static void
dump_dnode(objset_t *os, uint64_t object, void *data, size_t size)
{
}

static uint64_t
blkid2offset(const dnode_phys_t *dnp, const blkptr_t *bp, const zbookmark_t *zb)
{
	if (dnp == NULL) {
		ASSERT(zb->zb_level < 0);
		if (zb->zb_object == 0)
			return (zb->zb_blkid);
		return (zb->zb_blkid * BP_GET_LSIZE(bp));
	}

	ASSERT(zb->zb_level >= 0);

	return ((zb->zb_blkid <<
	    (zb->zb_level * (dnp->dn_indblkshift - SPA_BLKPTRSHIFT))) *
	    dnp->dn_datablkszsec << SPA_MINBLOCKSHIFT);
}

static void
sprintf_blkptr_compact(char *blkbuf, const blkptr_t *bp)
{
	const dva_t *dva = bp->blk_dva;
	int ndvas = (dump_opt['d'] > 5 || dump_opt['M'] > 5) ?
	    BP_GET_NDVAS(bp) : 1;

	if (dump_opt['b'] >= 5) {
		sprintf_blkptr(blkbuf, bp);
		return;
	}

	blkbuf[0] = '\0';

	for (int i = 0; i < ndvas; i++)
		(void) sprintf(blkbuf + strlen(blkbuf), "%llu:%llx:%llx ",
		    (u_longlong_t)DVA_GET_VDEV(&dva[i]),
		    (u_longlong_t)DVA_GET_OFFSET(&dva[i]),
		    (u_longlong_t)DVA_GET_ASIZE(&dva[i]));

	(void) sprintf(blkbuf + strlen(blkbuf),
	    "%llxL/%llxP F=%llu B=%llu/%llu",
	    (u_longlong_t)BP_GET_LSIZE(bp),
	    (u_longlong_t)BP_GET_PSIZE(bp),
	    (u_longlong_t)bp->blk_fill,
	    (u_longlong_t)bp->blk_birth,
	    (u_longlong_t)BP_PHYSICAL_BIRTH(bp));
}

static void
print_indirect(blkptr_t *bp, const zbookmark_t *zb,
    const dnode_phys_t *dnp)
{
	char blkbuf[BP_SPRINTF_LEN];
	int l;

	ASSERT3U(BP_GET_TYPE(bp), ==, dnp->dn_type);
	ASSERT3U(BP_GET_LEVEL(bp), ==, zb->zb_level);

	(void) printf("%16llx ", (u_longlong_t)blkid2offset(dnp, bp, zb));

	ASSERT(zb->zb_level >= 0);

	for (l = dnp->dn_nlevels - 1; l >= -1; l--) {
		if (l == zb->zb_level) {
			(void) printf("L%llx", (u_longlong_t)zb->zb_level);
		} else {
			(void) printf(" ");
		}
	}

	sprintf_blkptr_compact(blkbuf, bp);
	(void) printf("%s\n", blkbuf);
}

static int
visit_indirect(spa_t *spa, const dnode_phys_t *dnp,
    blkptr_t *bp, const zbookmark_t *zb)
{
	int err = 0;

	if (bp->blk_birth == 0)
		return (0);

	print_indirect(bp, zb, dnp);

	if (BP_GET_LEVEL(bp) > 0) {
		int i;
		blkptr_t *cbp;
		int epb = BP_GET_LSIZE(bp) >> SPA_BLKPTRSHIFT;
		arc_ref_t *buf;
		uint64_t fill = 0;

		err = arc_read(NULL, spa, bp, BP_GET_LSIZE(bp),
		    arc_getref_func, &buf, ZIO_PRIORITY_ASYNC_READ,
		    ZIO_FLAG_CANFAIL, ARC_OPT_METADATA, zb);
		if (err)
			return (err);
		ASSERT(buf->r_data);

		/* recursively visit blocks below this */
		cbp = buf->r_data;
		for (i = 0; i < epb; i++, cbp++) {
			zbookmark_t czb;

			SET_BOOKMARK(&czb, zb->zb_objset, zb->zb_object,
			    zb->zb_level - 1,
			    zb->zb_blkid * epb + i);
			err = visit_indirect(spa, dnp, cbp, &czb);
			if (err)
				break;
			fill += cbp->blk_fill;
		}
		if (!err)
			ASSERT3U(fill, ==, bp->blk_fill);
		arc_free_ref(buf);
	}

	return (err);
}

/*ARGSUSED*/
static void
dump_indirect(dnode_t *dn)
{
	dnode_phys_t *dnp = dn->dn_phys;
	int j;
	zbookmark_t czb;

	(void) printf("Indirect blocks:\n");

	SET_BOOKMARK(&czb, dmu_objset_id(dn->dn_objset),
	    dn->dn_object, dnp->dn_nlevels - 1, 0);
	for (j = 0; j < dnp->dn_nblkptr; j++) {
		czb.zb_blkid = j;
		(void) visit_indirect(dmu_objset_spa(dn->dn_objset), dnp,
		    &dnp->dn_blkptr[j], &czb);
	}

	(void) printf("\n");
}

/*ARGSUSED*/
static void
dump_dsl_dir(objset_t *os, uint64_t object, void *data, size_t size)
{
	dsl_dir_phys_t *dd = data;
	time_t crtime;
	char nice[32];

	if (dd == NULL)
		return;

	ASSERT3U(size, >=, sizeof (dsl_dir_phys_t));

	crtime = dd->dd_creation_time;
	(void) printf("\t\tcreation_time = %s", ctime(&crtime));
	(void) printf("\t\thead_dataset_obj = %llu\n",
	    (u_longlong_t)dd->dd_head_dataset_obj);
	(void) printf("\t\tparent_dir_obj = %llu\n",
	    (u_longlong_t)dd->dd_parent_obj);
	(void) printf("\t\torigin_obj = %llu\n",
	    (u_longlong_t)dd->dd_origin_obj);
	(void) printf("\t\tchild_dir_zapobj = %llu\n",
	    (u_longlong_t)dd->dd_child_dir_zapobj);
	zdb_nicenum(dd->dd_used_bytes, nice);
	(void) printf("\t\tused_bytes = %s\n", nice);
	zdb_nicenum(dd->dd_compressed_bytes, nice);
	(void) printf("\t\tcompressed_bytes = %s\n", nice);
	zdb_nicenum(dd->dd_uncompressed_bytes, nice);
	(void) printf("\t\tuncompressed_bytes = %s\n", nice);
	zdb_nicenum(dd->dd_quota, nice);
	(void) printf("\t\tquota = %s\n", nice);
	zdb_nicenum(dd->dd_reserved, nice);
	(void) printf("\t\treserved = %s\n", nice);
	(void) printf("\t\tprops_zapobj = %llu\n",
	    (u_longlong_t)dd->dd_props_zapobj);
	(void) printf("\t\tdeleg_zapobj = %llu\n",
	    (u_longlong_t)dd->dd_deleg_zapobj);
	(void) printf("\t\tflags = %llx\n",
	    (u_longlong_t)dd->dd_flags);

#define	DO(which) \
	zdb_nicenum(dd->dd_used_breakdown[DD_USED_ ## which], nice); \
	(void) printf("\t\tused_breakdown[" #which "] = %s\n", nice)
	DO(HEAD);
	DO(SNAP);
	DO(CHILD);
	DO(CHILD_RSRV);
	DO(REFRSRV);
#undef DO
}

/*ARGSUSED*/
static void
dump_dsl_dataset(objset_t *os, uint64_t object, void *data, size_t size)
{
	dsl_dataset_phys_t *ds = data;
	time_t crtime;
	char used[32], compressed[32], uncompressed[32], unique[32];
	char blkbuf[BP_SPRINTF_LEN];

	if (ds == NULL)
		return;

	ASSERT(size == sizeof (*ds));
	crtime = ds->ds_creation_time;
	zdb_nicenum(ds->ds_used_bytes, used);
	zdb_nicenum(ds->ds_compressed_bytes, compressed);
	zdb_nicenum(ds->ds_uncompressed_bytes, uncompressed);
	zdb_nicenum(ds->ds_unique_bytes, unique);
	sprintf_blkptr(blkbuf, &ds->ds_bp);

	(void) printf("\t\tdir_obj = %llu\n",
	    (u_longlong_t)ds->ds_dir_obj);
	(void) printf("\t\tprev_snap_obj = %llu\n",
	    (u_longlong_t)ds->ds_prev_snap_obj);
	(void) printf("\t\tprev_snap_txg = %llu\n",
	    (u_longlong_t)ds->ds_prev_snap_txg);
	(void) printf("\t\tnext_snap_obj = %llu\n",
	    (u_longlong_t)ds->ds_next_snap_obj);
	(void) printf("\t\tsnapnames_zapobj = %llu\n",
	    (u_longlong_t)ds->ds_snapnames_zapobj);
	(void) printf("\t\tnum_children = %llu\n",
	    (u_longlong_t)ds->ds_num_children);
	(void) printf("\t\tuserrefs_obj = %llu\n",
	    (u_longlong_t)ds->ds_userrefs_obj);
	(void) printf("\t\tcreation_time = %s", ctime(&crtime));
	(void) printf("\t\tcreation_txg = %llu\n",
	    (u_longlong_t)ds->ds_creation_txg);
	(void) printf("\t\tdeadlist_obj = %llu\n",
	    (u_longlong_t)ds->ds_deadlist_obj);
	(void) printf("\t\tused_bytes = %s\n", used);
	(void) printf("\t\tcompressed_bytes = %s\n", compressed);
	(void) printf("\t\tuncompressed_bytes = %s\n", uncompressed);
	(void) printf("\t\tunique = %s\n", unique);
	(void) printf("\t\tfsid_guid = %llu\n",
	    (u_longlong_t)ds->ds_fsid_guid);
	(void) printf("\t\tguid = %llu\n",
	    (u_longlong_t)ds->ds_guid);
	(void) printf("\t\tflags = %llx\n",
	    (u_longlong_t)ds->ds_flags);
	(void) printf("\t\tnext_clones_obj = %llu\n",
	    (u_longlong_t)ds->ds_next_clones_obj);
	(void) printf("\t\tprops_obj = %llu\n",
	    (u_longlong_t)ds->ds_props_obj);
	(void) printf("\t\tbp = %s\n", blkbuf);
}

/* ARGSUSED */
static int
dump_bpobj_cb(void *arg, const blkptr_t *bp, dmu_tx_t *tx)
{
	char blkbuf[BP_SPRINTF_LEN];

	ASSERT(bp->blk_birth != 0);
	sprintf_blkptr_compact(blkbuf, bp);
	(void) printf("\t%s\n", blkbuf);
	return (0);
}

static void
dump_bpobj(bpobj_t *bpo, char *name)
{
	char bytes[32];
	char comp[32];
	char uncomp[32];
	int verbosity = MAX(dump_opt['d'], dump_opt['M']);

	if (verbosity < 3)
		return;

	zdb_nicenum(bpo->bpo_phys->bpo_bytes, bytes);
	if (bpo->bpo_havesubobj) {
		zdb_nicenum(bpo->bpo_phys->bpo_comp, comp);
		zdb_nicenum(bpo->bpo_phys->bpo_uncomp, uncomp);
		(void) printf("\n    %s: %llu local blkptrs, %llu subobjs, "
		    "%s (%s/%s comp)\n",
		    name, (u_longlong_t)bpo->bpo_phys->bpo_num_blkptrs,
		    (u_longlong_t)bpo->bpo_phys->bpo_num_subobjs,
		    bytes, comp, uncomp);
	} else {
		(void) printf("\n    %s: %llu blkptrs, %s\n",
		    name, (u_longlong_t)bpo->bpo_phys->bpo_num_blkptrs, bytes);
	}

	if (verbosity < 5)
		return;

	(void) printf("\n");

	(void) bpobj_iterate_nofree(bpo, dump_bpobj_cb, NULL, NULL);
}

static void
dump_deadlist(dsl_deadlist_t *dl)
{
	dsl_deadlist_entry_t *dle;
	char bytes[32];
	char comp[32];
	char uncomp[32];
	int verbosity = MAX(dump_opt['d'], dump_opt['M']);

	if (verbosity < 3)
		return;

	zdb_nicenum(dl->dl_phys->dl_used, bytes);
	zdb_nicenum(dl->dl_phys->dl_comp, comp);
	zdb_nicenum(dl->dl_phys->dl_uncomp, uncomp);
	(void) printf("\n    Deadlist: %s (%s/%s comp)\n",
	    bytes, comp, uncomp);

	if (verbosity < 4)
		return;

	(void) printf("\n");

	for (dle = avl_first(&dl->dl_tree); dle;
	    dle = AVL_NEXT(&dl->dl_tree, dle)) {
		(void) printf("      mintxg %llu -> obj %llu\n",
		    (longlong_t)dle->dle_mintxg,
		    (longlong_t)dle->dle_bpobj.bpo_object);

		if (verbosity >= 5)
			dump_bpobj(&dle->dle_bpobj, "");
	}
}

static avl_tree_t idx_tree;
static avl_tree_t domain_tree;
static boolean_t fuid_table_loaded;
static boolean_t sa_loaded;
sa_attr_type_t *sa_attr_table;

static void
fuid_table_destroy()
{
	if (fuid_table_loaded) {
		zfs_fuid_table_destroy(&idx_tree, &domain_tree);
		fuid_table_loaded = B_FALSE;
	}
}

/*
 * print uid or gid information.
 * For normal POSIX id just the id is printed in decimal format.
 * For CIFS files with FUID the fuid is printed in hex followed by
 * the doman-rid string.
 */
static void
print_idstr(uint64_t id, const char *id_type)
{
	if (FUID_INDEX(id)) {
		char *domain;

		domain = zfs_fuid_idx_domain(&idx_tree, FUID_INDEX(id));
		(void) printf("\t%s     %llx [%s-%d]\n", id_type,
		    (u_longlong_t)id, domain, (int)FUID_RID(id));
	} else {
		(void) printf("\t%s     %llu\n", id_type, (u_longlong_t)id);
	}

}

static void
dump_uidgid(objset_t *os, uint64_t uid, uint64_t gid)
{
	uint32_t uid_idx, gid_idx;

	uid_idx = FUID_INDEX(uid);
	gid_idx = FUID_INDEX(gid);

	/* Load domain table, if not already loaded */
	if (!fuid_table_loaded && (uid_idx || gid_idx)) {
		uint64_t fuid_obj;

		/* first find the fuid object.  It lives in the master node */
		VERIFY(zap_lookup(os, MASTER_NODE_OBJ, ZFS_FUID_TABLES,
		    8, 1, &fuid_obj) == 0);
		zfs_fuid_avl_tree_create(&idx_tree, &domain_tree);
		(void) zfs_fuid_table_load(os, fuid_obj,
		    &idx_tree, &domain_tree);
		fuid_table_loaded = B_TRUE;
	}

	print_idstr(uid, "uid");
	print_idstr(gid, "gid");
}

/*ARGSUSED*/
static void
dump_znode(objset_t *os, uint64_t object, void *data, size_t size)
{
	char path[MAXPATHLEN * 2];	/* allow for xattr and failure prefix */
	sa_handle_t *hdl;
	uint64_t xattr, rdev, gen;
	uint64_t uid, gid, mode, fsize, parent, links;
	uint64_t pflags;
	uint64_t acctm[2], modtm[2], chgtm[2], crtm[2];
	time_t z_crtime, z_atime, z_mtime, z_ctime;
	sa_bulk_attr_t bulk[12];
	int idx = 0;
	int error;

	if (os->os_crypt != ZIO_CRYPT_OFF) {
		if (dump_opt['d'] < 3) {
			(void) printf("\tznode is encrypted\n");
		}
		return;
	}
	if (!sa_loaded) {
		uint64_t sa_attrs = 0;
		uint64_t version;

		VERIFY(zap_lookup(os, MASTER_NODE_OBJ, ZPL_VERSION_STR,
		    8, 1, &version) == 0);
		if (version >= ZPL_VERSION_SA) {
			VERIFY(zap_lookup(os, MASTER_NODE_OBJ, ZFS_SA_ATTRS,
			    8, 1, &sa_attrs) == 0);
		}
		if ((error = sa_setup(os, sa_attrs, zfs_attr_table,
		    ZPL_END, &sa_attr_table)) != 0) {
			zdb_err(CE_WARN, "sa_setup failed errno %d, can't "
			    "display znode contents\n", error);
			return;
		}
		sa_loaded = B_TRUE;
	}

	if (sa_handle_get(os, object, NULL, SA_HDL_PRIVATE, &hdl)) {
		zdb_err(CE_WARN, "Failed to get handle for SA znode\n");
		return;
	}

	SA_ADD_BULK_ATTR(bulk, idx, sa_attr_table[ZPL_UID], NULL, &uid, 8);
	SA_ADD_BULK_ATTR(bulk, idx, sa_attr_table[ZPL_GID], NULL, &gid, 8);
	SA_ADD_BULK_ATTR(bulk, idx, sa_attr_table[ZPL_LINKS], NULL,
	    &links, 8);
	SA_ADD_BULK_ATTR(bulk, idx, sa_attr_table[ZPL_GEN], NULL, &gen, 8);
	SA_ADD_BULK_ATTR(bulk, idx, sa_attr_table[ZPL_MODE], NULL,
	    &mode, 8);
	SA_ADD_BULK_ATTR(bulk, idx, sa_attr_table[ZPL_PARENT],
	    NULL, &parent, 8);
	SA_ADD_BULK_ATTR(bulk, idx, sa_attr_table[ZPL_SIZE], NULL,
	    &fsize, 8);
	SA_ADD_BULK_ATTR(bulk, idx, sa_attr_table[ZPL_ATIME], NULL,
	    acctm, 16);
	SA_ADD_BULK_ATTR(bulk, idx, sa_attr_table[ZPL_MTIME], NULL,
	    modtm, 16);
	SA_ADD_BULK_ATTR(bulk, idx, sa_attr_table[ZPL_CRTIME], NULL,
	    crtm, 16);
	SA_ADD_BULK_ATTR(bulk, idx, sa_attr_table[ZPL_CTIME], NULL,
	    chgtm, 16);
	SA_ADD_BULK_ATTR(bulk, idx, sa_attr_table[ZPL_FLAGS], NULL,
	    &pflags, 8);

	if (sa_bulk_lookup(hdl, bulk, idx)) {
		(void) sa_handle_destroy(hdl);
		return;
	}

	error = zfs_obj_to_path(os, object, path, sizeof (path));
	if (error != 0) {
		(void) snprintf(path, sizeof (path), "\?\?\?<object#%llu>",
		    (u_longlong_t)object);
	}
	if (dump_opt['d'] < 3) {
		(void) printf("\t%s\n", path);
		(void) sa_handle_destroy(hdl);
		return;
	}

	z_crtime = (time_t)crtm[0];
	z_atime = (time_t)acctm[0];
	z_mtime = (time_t)modtm[0];
	z_ctime = (time_t)chgtm[0];

	(void) printf("\tpath	%s\n", path);
	dump_uidgid(os, uid, gid);
	(void) printf("\tatime	%s", ctime(&z_atime));
	(void) printf("\tmtime	%s", ctime(&z_mtime));
	(void) printf("\tctime	%s", ctime(&z_ctime));
	(void) printf("\tcrtime	%s", ctime(&z_crtime));
	(void) printf("\tgen	%llu\n", (u_longlong_t)gen);
	(void) printf("\tmode	%llo\n", (u_longlong_t)mode);
	(void) printf("\tsize	%llu\n", (u_longlong_t)fsize);
	(void) printf("\tparent	%llu\n", (u_longlong_t)parent);
	(void) printf("\tlinks	%llu\n", (u_longlong_t)links);
	(void) printf("\tpflags	%llx\n", (u_longlong_t)pflags);
	if (sa_lookup(hdl, sa_attr_table[ZPL_XATTR], &xattr,
	    sizeof (uint64_t)) == 0)
		(void) printf("\txattr	%llu\n", (u_longlong_t)xattr);
	if (sa_lookup(hdl, sa_attr_table[ZPL_RDEV], &rdev,
	    sizeof (uint64_t)) == 0)
		(void) printf("\trdev	0x%016llx\n", (u_longlong_t)rdev);
	sa_handle_destroy(hdl);
}

/*ARGSUSED*/
static void
dump_acl(objset_t *os, uint64_t object, void *data, size_t size)
{
}

/*ARGSUSED*/
static void
dump_dmu_objset(objset_t *os, uint64_t object, void *data, size_t size)
{
}

static object_viewer_t *object_viewer[DMU_OT_NUMTYPES + 1] = {
	dump_none,		/* unallocated			*/
	dump_zap,		/* object directory		*/
	dump_uint64,		/* object array			*/
	dump_none,		/* packed nvlist		*/
	dump_packed_nvlist,	/* packed nvlist size		*/
	dump_none,		/* bplist			*/
	dump_none,		/* bplist header		*/
	dump_none,		/* SPA space map header		*/
	dump_none,		/* SPA space map		*/
	dump_none,		/* ZIL intent log		*/
	dump_dnode,		/* DMU dnode			*/
	dump_dmu_objset,	/* DMU objset			*/
	dump_dsl_dir,		/* DSL directory		*/
	dump_zap,		/* DSL directory child map	*/
	dump_zap,		/* DSL dataset snap map		*/
	dump_zap,		/* DSL props			*/
	dump_dsl_dataset,	/* DSL dataset			*/
	dump_znode,		/* ZFS znode			*/
	dump_acl,		/* ZFS V0 ACL			*/
	dump_uint8,		/* ZFS plain file		*/
	dump_zpldir,		/* ZFS directory		*/
	dump_zap,		/* ZFS master node		*/
	dump_zap,		/* ZFS delete queue		*/
	dump_uint8,		/* zvol object			*/
	dump_zap,		/* zvol prop			*/
	dump_uint8,		/* other uint8[]		*/
	dump_uint64,		/* other uint64[]		*/
	dump_zap,		/* other ZAP			*/
	dump_zap,		/* persistent error log		*/
	dump_uint8,		/* SPA history			*/
	dump_uint64,		/* SPA history offsets		*/
	dump_zap,		/* Pool properties		*/
	dump_zap,		/* DSL permissions		*/
	dump_acl,		/* ZFS ACL			*/
	dump_uint8,		/* ZFS SYSACL			*/
	dump_none,		/* FUID nvlist			*/
	dump_packed_nvlist,	/* FUID nvlist size		*/
	dump_zap,		/* DSL dataset next clones	*/
	dump_zap,		/* DSL scrub queue		*/
	dump_zap,		/* ZFS user/group used		*/
	dump_zap,		/* ZFS user/group quota		*/
	dump_zap,		/* snapshot refcount tags	*/
	dump_ddt_zap,		/* DDT ZAP object		*/
	dump_zap,		/* DDT statistics		*/
	dump_znode,		/* SA object			*/
	dump_zap,		/* SA Master Node		*/
	dump_sa_attrs,		/* SA attribute registration	*/
	dump_sa_layouts,	/* SA attribute layouts		*/
	dump_zap,		/* DSL scrub translations	*/
	dump_none,		/* fake dedup BP		*/
	dump_zap,		/* deadlist			*/
	dump_none,		/* deadlist hdr			*/
	dump_zap,		/* dsl clones			*/
	dump_none,		/* bpobj subobjs		*/
	dump_keychain_zap,	/* DSL keychain			*/
	dump_unknown,		/* Unknown type, must be last	*/
};

static void
dump_object(objset_t *os, uint64_t object, int verbosity, int *print_header)
{
	dmu_buf_t *db = NULL;
	dmu_object_info_t doi;
	dnode_t *dn;
	void *bonus = NULL;
	size_t bsize = 0;
	char iblk[32], dblk[32], lsize[32], asize[32], fill[32];
	char bonus_size[32];
	char aux[50];
	int error;

	if (*print_header) {
		(void) printf("\n%10s  %3s  %5s  %5s  %5s  %5s  %6s  %s\n",
		    "Object", "lvl", "iblk", "dblk", "dsize", "lsize",
		    "%full", "type");
		*print_header = 0;
	}

	if (object == 0) {
		dn = DMU_META_DNODE(os);
	} else {
		error = dmu_bonus_hold(os, object, FTAG, &db);
		if (error) {
			zdb_err(CE_WARN, "%10lld: dmu_bonus_hold() "
			    "failed: %s\n",
			    (u_longlong_t)object, strerror(error));
			return;
		}
		bonus = db->db_data;
		bsize = db->db_size;
		dn = DB_DNODE((dmu_buf_impl_t *)db);
	}
	dmu_object_info_from_dnode(dn, &doi);

	zdb_nicenum(doi.doi_metadata_block_size, iblk);
	zdb_nicenum(doi.doi_data_block_size, dblk);
	zdb_nicenum(doi.doi_max_offset, lsize);
	zdb_nicenum(doi.doi_physical_blocks_512 << 9, asize);
	zdb_nicenum(doi.doi_bonus_size, bonus_size);
	(void) sprintf(fill, "%6.2f", 100.0 * doi.doi_fill_count *
	    doi.doi_data_block_size / (object == 0 ? DNODES_PER_BLOCK : 1) /
	    doi.doi_max_offset);

	aux[0] = '\0';

	if (doi.doi_checksum != ZIO_CHECKSUM_INHERIT || verbosity >= 6) {
		(void) snprintf(aux + strlen(aux), sizeof (aux), " (K=%s)",
		    ZDB_CHECKSUM_NAME(doi.doi_checksum));
	}

	if (doi.doi_compress != ZIO_COMPRESS_INHERIT || verbosity >= 6) {
		(void) snprintf(aux + strlen(aux), sizeof (aux), " (Z=%s)",
		    ZDB_COMPRESS_NAME(doi.doi_compress));
	}

	(void) printf("%10lld  %3u  %5s  %5s  %5s  %5s  %6s  %s%s\n",
	    (u_longlong_t)object, doi.doi_indirection, iblk, dblk,
	    asize, lsize, fill, ZDB_OT_NAME(doi.doi_type), aux);

	if (doi.doi_bonus_type != DMU_OT_NONE && verbosity > 3) {
		(void) printf("%10s  %3s  %5s  %5s  %5s  %5s  %6s  %s\n",
		    "", "", "", "", "", bonus_size, "bonus",
		    ZDB_OT_NAME(doi.doi_bonus_type));
	}

	if (verbosity >= 4) {
		(void) printf("\tdnode flags: %s%s%s\n",
		    (dn->dn_phys->dn_flags & DNODE_FLAG_USED_BYTES) ?
		    "USED_BYTES " : "",
		    (dn->dn_phys->dn_flags & DNODE_FLAG_USERUSED_ACCOUNTED) ?
		    "USERUSED_ACCOUNTED " : "",
		    (dn->dn_phys->dn_flags & DNODE_FLAG_SPILL_BLKPTR) ?
		    "SPILL_BLKPTR" : "");
		(void) printf("\tdnode maxblkid: %llu\n",
		    (longlong_t)dn->dn_phys->dn_maxblkid);

		object_viewer[ZDB_OT_TYPE(doi.doi_bonus_type)](os, object,
		    bonus, bsize);
		object_viewer[ZDB_OT_TYPE(doi.doi_type)](os, object, NULL, 0);
		*print_header = 1;
	}

	if (verbosity >= 5)
		dump_indirect(dn);

	if (verbosity >= 5) {
		/*
		 * Report the list of segments that comprise the object.
		 */
		uint64_t start = 0;
		uint64_t end;
		uint64_t blkfill = 1;
		int minlvl = 1;

		if (dn->dn_type == DMU_OT_DNODE) {
			minlvl = 0;
			blkfill = DNODES_PER_BLOCK;
		}

		for (;;) {
			char segsize[32];
			error = dnode_next_offset(dn,
			    0, &start, minlvl, blkfill, 0);
			if (error)
				break;
			end = start;
			error = dnode_next_offset(dn,
			    DNODE_FIND_HOLE, &end, minlvl, blkfill, 0);
			zdb_nicenum(end - start, segsize);
			(void) printf("\t\tsegment [%016llx, %016llx)"
			    " size %5s\n", (u_longlong_t)start,
			    (u_longlong_t)end, segsize);
			if (error)
				break;
			start = end;
		}
	}

	if (db != NULL)
		dmu_buf_rele(db, FTAG);
}

static char *objset_types[DMU_OST_NUMTYPES] = {
	"NONE", "META", "ZPL", "ZVOL", "OTHER", "ANY" };

static void
dump_dir(objset_t *os)
{
	dmu_objset_stats_t dds;
	blkptr_t *bp;
	uint64_t object, object_count;
	uint64_t refdbytes, usedobjs, scratch;
	char numbuf[32];
	char blkbuf[BP_SPRINTF_LEN + 20];
	char osname[MAXNAMELEN];
	char *type = "UNKNOWN";
	int verbosity = dump_opt['d'];
	int print_header = 1;
	int i, error;

	dmu_objset_fast_stat(os, &dds);

	if (dds.dds_type < DMU_OST_NUMTYPES)
		type = objset_types[dds.dds_type];

	if (dds.dds_type == DMU_OST_META) {
		verbosity = dump_opt['M'];
		dds.dds_inconsistent = 0;
		dsl_pool_t *dp = os->os_spa->spa_dsl_pool;
		dds.dds_creation_txg = TXG_INITIAL;
		bp = &dp->dp_meta_rootbp;
		usedobjs = bp->blk_fill;
		refdbytes = dp->dp_mos_dir->dd_phys->dd_used_bytes;
	} else {
		bp = &os->os_dsl_dataset->ds_phys->ds_bp;
		dmu_objset_space(os, &refdbytes, &scratch, &usedobjs, &scratch);
		ASSERT3U(usedobjs, ==, bp->blk_fill);
	}

	zdb_nicenum(refdbytes, numbuf);

	if (verbosity >= 4) {
		(void) sprintf(blkbuf, ", rootbp ");
		(void) sprintf_blkptr(blkbuf + strlen(blkbuf), bp);
	} else {
		blkbuf[0] = '\0';
	}

	dmu_objset_name(os, osname);

	(void) printf("Dataset %s [%s], ID %llu, cr_txg %llu, "
	    "%s, %llu objects%s%s\n",
	    osname, type, (u_longlong_t)dmu_objset_id(os),
	    (u_longlong_t)dds.dds_creation_txg,
	    numbuf, (u_longlong_t)usedobjs,
	    dds.dds_inconsistent ? ", inconsistent" : "", blkbuf);

	if (zopt_objects != 0) {
		for (i = 0; i < zopt_objects; i++)
			dump_object(os, zopt_object[i], verbosity,
			    &print_header);
		(void) printf("\n");
		return;
	}

	if (dump_opt['i'] != 0 || verbosity >= 2)
		dump_intent_log(dmu_objset_zil(os));

	if (dmu_objset_ds(os) != NULL &&
	    spa_version(os->os_spa) >= SPA_VERSION_DEADLISTS)
		dump_deadlist(&dmu_objset_ds(os)->ds_deadlist);

	if (verbosity < 2)
		return;

	if (bp->blk_birth == 0)
		return;

	dump_object(os, 0, verbosity, &print_header);
	object_count = 0;
	if (DMU_USERUSED_DNODE(os) != NULL &&
	    DMU_USERUSED_DNODE(os)->dn_type != 0) {
		dump_object(os, DMU_USERUSED_OBJECT, verbosity, &print_header);
		dump_object(os, DMU_GROUPUSED_OBJECT, verbosity, &print_header);
	}

	object = 0;
	while ((error = dmu_object_next(os, &object, B_FALSE, 0)) == 0 ||
	    error != ESRCH && dump_opt['I']) {
		dump_object(os, object, verbosity, &print_header);
		object_count++;
	}

	if (object_count != usedobjs) {
		if (object_count + 1 == usedobjs && error == ESRCH)
			zdb_err(CE_NOTE, "dataset might be last updated "
			    "when running ZFS prior to build 114:\n"
			    "\tobject_count (%llu) + 1 == usedobjs (%llu)\n",
			    (u_longlong_t)object_count, (u_longlong_t)usedobjs);
		else if (!dump_opt['I'])
			ASSERT3U(object_count, ==, usedobjs);
	}

	(void) printf("\n");

	if (error != ESRCH) {
		zdb_err(CE_NOTE, "dmu_object_next() = %d\n", error);
		if (dump_opt['I'])
			return;
		abort();
	}
}

static void
dump_uberblock(uberblock_t *ub, const char *header, const char *footer)
{
	time_t timestamp = ub->ub_timestamp;

	(void) printf("%s", header ? header : "");
	(void) printf("\tmagic = %016llx\n", (u_longlong_t)ub->ub_magic);
	(void) printf("\tversion = %llu\n", (u_longlong_t)ub->ub_version);
	(void) printf("\ttxg = %llu\n", (u_longlong_t)ub->ub_txg);
	(void) printf("\tguid_sum = %llu\n", (u_longlong_t)ub->ub_guid_sum);
	(void) printf("\ttimestamp = %llu UTC = %s",
	    (u_longlong_t)ub->ub_timestamp, asctime(localtime(&timestamp)));
	if (dump_opt['u'] >= 3) {
		char blkbuf[BP_SPRINTF_LEN];
		sprintf_blkptr(blkbuf, &ub->ub_rootbp);
		(void) printf("\trootbp = %s\n", blkbuf);
	}
	(void) printf("%s", footer ? footer : "");
}

static nvlist_t *
load_nvlist_object(objset_t *os, uint64_t obj)
{
	dmu_buf_t *db;
	int error;
	nvlist_t *nv = NULL;

	error = dmu_bonus_hold(os, obj, FTAG, &db);

	if (error == 0) {
		size_t nvsize = *(uint64_t *)db->db_data;
		dmu_buf_rele(db, FTAG);

		if ((nv = load_packed_nvlist(os, obj, nvsize)) == NULL)
			zdb_err(CE_WARN, "load_packed_nvlist(%llu) "
			    "failed\n", (u_longlong_t)obj);
	} else {
		zdb_err(CE_WARN, "dmu_bonus_hold(%llu) failed, errno %d\n",
		    (u_longlong_t)obj, error);
	}
	return (nv);
}

static void
dump_raw_config(const char *name, nvlist_t *nv)
{
	nvlist_t *pool;
	char *packed = NULL;
	size_t size;
	int error;

	(void) nvlist_remove(nv, ZPOOL_LOAD_POLICY, DATA_TYPE_NVLIST);

	if ((error = nvlist_alloc(&pool, NV_UNIQUE_NAME_TYPE, 0)) != 0 ||
	    (error = nvlist_add_nvlist(pool, name, nv)) != 0)
		zdb_err(CE_PANIC, "internal error: errno %d", error);

	if ((error = nvlist_pack(pool, &packed, &size, NV_ENCODE_XDR,
	    UMEM_NOFAIL)) != 0)
		zdb_err(CE_PANIC, "nvlist_pack() failed, errno: %s", error);

	(void) write(1, packed, size);
	nvlist_free(pool);
	free(packed);
}

static void
dump_nvlist_object(objset_t *os, uint64_t obj, const char *header)
{
	nvlist_t *nv;

	(void) printf("%s", header);
	nv = load_nvlist_object(os, obj);

	dump_nvlist(nv, 8);
	nvlist_free(nv);
}

static int
dump_cachefile(const char *cachefile)
{
	nvlist_t *config;

	if ((config = zpool_read_cachefile(g_zfs, cachefile)) == NULL)
		return (1);

	dump_nvlist(config, 0);

	nvlist_free(config);

	return (0);
}

static int
zdb_label_cksum(zio_cksum_t *actual_cksum,
    void *data, uint64_t size, uint64_t offset)
{
	zio_eck_t *eck = (zio_eck_t *)(void *)((char *)data + size) - 1;
	zio_checksum_info_t *ci = &zio_checksum_table[ZIO_CHECKSUM_LABEL];
	zio_cksum_t expected_cksum, verifier;
	int byteswap = (eck->zec_magic == BSWAP_64(ZEC_MAGIC));

	ZIO_SET_CHECKSUM(&verifier, offset, 0, 0, 0);

	if (byteswap)
		byteswap_uint64_array(&verifier, sizeof (zio_cksum_t));

	expected_cksum = eck->zec_cksum;
	eck->zec_cksum = verifier;
	ci->ci_func[byteswap](data, size, actual_cksum);
	eck->zec_cksum = expected_cksum;

	if (byteswap)
		byteswap_uint64_array(&expected_cksum, sizeof (zio_cksum_t));

	return (ZIO_CHECKSUM_EQUAL(*actual_cksum, expected_cksum) ? 0 : ECKSUM);
}

static void
dump_cksum(zio_cksum_t *zcp, const char *header, const char *footer)
{
	(void) printf("%s%#llx:%#llx:%#llx:%#llx%s", header ? header : "",
	    (u_longlong_t)zcp->zc_word[0], (u_longlong_t)zcp->zc_word[1],
	    (u_longlong_t)zcp->zc_word[2], (u_longlong_t)zcp->zc_word[3],
	    footer ? footer : "\n");
}


#define	ZDB_MAX_UB_HEADER_SIZE 32

static void
dump_label_uberblocks(vdev_label_t *lbl, uint64_t ashift, uint64_t psize, int l)
{
	vdev_t vd;
	vdev_t *vdp = &vd;
	char header[ZDB_MAX_UB_HEADER_SIZE];
	int unique = 0;

	vd.vdev_ashift = ashift;
	vdp->vdev_top = vdp;

	for (int i = 0; i < VDEV_UBERBLOCK_COUNT(vdp); i++) {
		uint64_t uoff = VDEV_UBERBLOCK_OFFSET(vdp, i);
		uint64_t off = vdev_label_offset(psize, l, uoff);
		uberblock_t *ub = (void *)((char *)(&lbl[l]) + uoff);
		int ublen = VDEV_UBERBLOCK_SIZE(vdp);
		zio_cksum_t zc;
		int error = -1;
		int match = -1;

		if (dump_opt['c'])
			error = zdb_label_cksum(&zc, (char *)ub, ublen, off);

		if (uberblock_verify(ub) && dump_opt['u'] < 5)
			continue;

		for (int x = 0; x < l; x++) {
			char *xub = (void *)((char *)(&lbl[x]) + uoff);

			if (bcmp(ub, xub, ublen - sizeof (zio_eck_t)) == 0) {
				match = x;
				break;
			}
		}
		(void) snprintf(header, ZDB_MAX_UB_HEADER_SIZE,
		    "Uberblock[%d]%s%s\n", i, (error == -1) ? "" : " - ",
		    (error == -1) ? "" : (error == 0)  ? "VALID" : "INVALID");

		if (match < 0 || dump_opt['u'] > 1) {
			unique++;
			dump_uberblock(ub, header, "");

			if (dump_opt['c'] > 1) {
				zio_cksum_t *rcp = &((zio_eck_t *)(void *)
				    ((char *)ub + ublen) - 1)->zec_cksum;
				dump_cksum(rcp, "\texpected checksum = ", "\n");
				dump_cksum(&zc, "\tcomputed checksum = ", "\n");
			}
		}
	}
	if (dump_opt['u'] < 2 && l > 0)
		(void) printf("%s uberblocks match previous label(s)\n",
		    unique ? "Other" : "All");
}

static void
dump_label(const char *dev)
{
	int fd;
	vdev_label_t labels[VDEV_LABELS] = { 0 };
	char *path;
	struct stat64 statbuf;
	uint64_t psize, ashift;
	int len = strlen(dev) + 1;

	if (strncmp(dev, "/dev/dsk/", 9) == 0) {
		len++;
		path = malloc(len);
		(void) snprintf(path, len, "%s%s", "/dev/rdsk/", dev + 9);
	} else {
		path = strdup(dev);
	}

	if ((fd = open64(path, O_RDONLY)) < 0) {
		(void) printf("cannot open '%s': %s\n", path, strerror(errno));
		free(path);
		exit(1);
	}

	if (fstat64(fd, &statbuf) != 0) {
		(void) printf("failed to stat '%s': %s\n", path,
		    strerror(errno));
		free(path);
		(void) close(fd);
		exit(1);
	}

	if (S_ISBLK(statbuf.st_mode) && dump_opt['l'] < 2) {
		(void) printf("cannot use '%s': character device required\n",
		    path);
		free(path);
		(void) close(fd);
		exit(1);
	}

	psize = statbuf.st_size;
	psize = P2ALIGN(psize, (uint64_t)sizeof (vdev_label_t));

	for (int l = 0; l < VDEV_LABELS; l++) {
		nvlist_t *config = NULL;
		char *buf = labels[l].vl_vdev_phys.vp_nvlist;
		size_t buflen = sizeof (labels[l].vl_vdev_phys.vp_nvlist);
		size_t physlen = sizeof (labels[l].vl_vdev_phys);
		int match = -1;

		if (pread64(fd, &labels[l], sizeof (labels[l]),
		    vdev_label_offset(psize, l, 0)) != sizeof (labels[l])) {
			(void) printf("failed to read label %d\n", l);
			continue;
		}

		(void) printf("------------------------------------------\n");
		(void) printf("LABEL %d", l);

		if (dump_opt['c']) {
			zio_cksum_t zc;
			uint64_t offset = vdev_label_offset(psize, l,
			    offsetof(vdev_label_t, vl_vdev_phys.vp_nvlist));
			int error = zdb_label_cksum(&zc, buf, physlen, offset);

			(void) printf(" - %sVALID", error ? "IN" : "");
		}

		for (int x = 0; x < l; x++) {
			char *xbuf = (void *)&labels[x].vl_vdev_phys.vp_nvlist;

			if (bcmp(xbuf, buf, buflen) == 0) {
				match = x;
				break;
			}
		}

		if (match >= 0 && dump_opt['l'] < 2)
			(void) printf(" - CONFIG MATCHES LABEL %d", match);

		(void) printf("\n------------------------------------------\n");

		if (nvlist_unpack(buf, buflen, &config, 0) != 0) {
			(void) printf("failed to unpack label %d\n", l);
			ashift = SPA_MINBLOCKSHIFT;
		} else {
			nvlist_t *vdev_tree = NULL;
			uint64_t timestamp;

			if ((match < 0 || dump_opt['l'] > 1) &&
			    nvlist_lookup_uint64(config,
			    ZPOOL_CONFIG_TIMESTAMP, &timestamp) == 0) {
				time_t t = timestamp;

				(void) printf("    timestamp: %llu UTC: %s",
				    (u_longlong_t)timestamp,
				    asctime(localtime(&t)));
				(void) nvlist_remove(config,
				    ZPOOL_CONFIG_TIMESTAMP, DATA_TYPE_UINT64);
			}
			if (match < 0 || dump_opt['l'] > 1)
				dump_nvlist(config, 4);

			if ((nvlist_lookup_nvlist(config,
			    ZPOOL_CONFIG_VDEV_TREE, &vdev_tree) != 0) ||
			    (nvlist_lookup_uint64(vdev_tree,
			    ZPOOL_CONFIG_ASHIFT, &ashift) != 0))
				ashift = SPA_MINBLOCKSHIFT;
			nvlist_free(config);
		}
		if (dump_opt['u'])
			dump_label_uberblocks(&labels[0], ashift, psize, l);
	}

	free(path);
	(void) close(fd);
}

/*ARGSUSED*/
static int
dump_one_dir(const char *dsname, void *arg)
{
	int error;
	objset_t *os;

	error = dmu_objset_own(dsname, DMU_OST_ANY, B_TRUE, FTAG, &os);
	if (error) {
		zdb_err(CE_WARN, "Could not open %s, error %d\n",
		    dsname, error);
		return (0);
	}
	dump_dir(os);
	dmu_objset_disown(os, FTAG);
	fuid_table_destroy();
	sa_loaded = B_FALSE;
	return (0);
}

/*
 * Block statistics.
 */
typedef struct zdb_blkstats {
	uint64_t	zb_asize;
	uint64_t	zb_lsize;
	uint64_t	zb_psize;
	uint64_t	zb_count;
} zdb_blkstats_t;

/*
 * Extended object types to report deferred frees and dedup auto-ditto blocks.
 */
#define	ZDB_OT_DEFERRED	(DMU_OT_NUMTYPES + 0)
#define	ZDB_OT_DITTO	(DMU_OT_NUMTYPES + 1)
#define	ZDB_OT_TOTAL	(DMU_OT_NUMTYPES + 2)

static char *zdb_ot_extname[] = {
	"deferred free",
	"dedup ditto",
	"Total",
};

#define	ZB_TOTAL	DN_MAX_LEVELS

typedef struct zdb_cb {
	zdb_blkstats_t	zcb_type[ZB_TOTAL + 1][ZDB_OT_TOTAL + 1];
	uint64_t	zcb_dedup_asize;
	uint64_t	zcb_dedup_blocks;
	uint64_t	zcb_errors[256];
	int		zcb_readfails;
	int		zcb_haderrors;
	spa_t		*zcb_spa;
} zdb_cb_t;

static void
zdb_count_block(zdb_cb_t *zcb, zilog_t *zilog, const blkptr_t *bp,
    dmu_object_type_t type)
{
	uint64_t refcnt = 0;

	ASSERT(type < ZDB_OT_TOTAL);

	if (zilog && zil_bp_tree_add(zilog, bp) != 0)
		return;

	for (int i = 0; i < 4; i++) {
		int l = (i < 2) ? BP_GET_LEVEL(bp) : ZB_TOTAL;
		int t = (i & 1) ? type : ZDB_OT_TOTAL;
		zdb_blkstats_t *zb = &zcb->zcb_type[l][t];

		zb->zb_asize += BP_GET_ASIZE(bp);
		zb->zb_lsize += BP_GET_LSIZE(bp);
		zb->zb_psize += BP_GET_PSIZE(bp);
		zb->zb_count++;
	}

	if (dump_opt['L'])
		return;

	if (BP_GET_DEDUP(bp)) {
		ddt_t *ddt;
		ddt_entry_t *dde;

		ddt = ddt_select(zcb->zcb_spa, bp);
		ddt_enter(ddt);
		dde = ddt_lookup(ddt, bp, B_FALSE);

		if (dde == NULL) {
			refcnt = 0;
		} else {
			ddt_phys_t *ddp = ddt_phys_select(dde, bp);
			ddt_phys_decref(ddp);
			refcnt = ddp->ddp_refcnt;
			if (ddt_phys_total_refcnt(dde) == 0)
				ddt_remove(ddt, dde);
		}
		ddt_exit(ddt);
	}

	VERIFY3U(zio_wait(zio_claim(NULL, zcb->zcb_spa,
	    refcnt ? 0 : spa_first_txg(zcb->zcb_spa),
	    bp, NULL, NULL, ZIO_FLAG_CANFAIL)), ==, 0);
}

/* ARGSUSED */
static int
zdb_blkptr_cb(spa_t *spa, zilog_t *zilog, const blkptr_t *bp,
    const zbookmark_t *zb, const dnode_phys_t *dnp, void *arg)
{
	zdb_cb_t *zcb = arg;
	char blkbuf[BP_SPRINTF_LEN];
	dmu_object_type_t type;
	boolean_t is_metadata;

	if (bp == NULL)
		return (0);

	type = BP_GET_TYPE(bp);

	zdb_count_block(zcb, zilog, bp, type);

	is_metadata = (BP_GET_LEVEL(bp) != 0 ||
	    zb->zb_blkid == DMU_SPILL_BLKID ||
	    dmu_ot[type].ot_metadata);

	if (dump_opt['c'] > 1 || (dump_opt['c'] && is_metadata)) {
		int ioerr;
		size_t size = BP_GET_PSIZE(bp);
		void *data = malloc(size);
		int flags = ZIO_FLAG_CANFAIL | ZIO_FLAG_SCRUB | ZIO_FLAG_RAW;

		/* If it's an intent log block, failure is expected. */
		if (zb->zb_level == ZB_ZIL_LEVEL)
			flags |= ZIO_FLAG_SPECULATIVE;

		ioerr = zio_wait(zio_read(NULL, spa, bp, data, size,
		    NULL, NULL, ZIO_PRIORITY_ASYNC_READ, flags, zb));

		free(data);

		if (ioerr && !(flags & ZIO_FLAG_SPECULATIVE)) {
			zcb->zcb_haderrors = 1;
			zcb->zcb_errors[ioerr]++;

			if (dump_opt['b'] >= 2)
				sprintf_blkptr(blkbuf, bp);
			else
				blkbuf[0] = '\0';

			(void) printf("zdb_blkptr_cb: "
			    "Got error %d reading "
			    "<%llu, %llu, %lld, %llx> %s -- skipping\n",
			    ioerr,
			    (u_longlong_t)zb->zb_objset,
			    (u_longlong_t)zb->zb_object,
			    (u_longlong_t)zb->zb_level,
			    (u_longlong_t)zb->zb_blkid,
			    blkbuf);
		}
	}

	zcb->zcb_readfails = 0;

	if (dump_opt['b'] >= 4) {
		sprintf_blkptr(blkbuf, bp);
		(void) printf("objset %llu object %llu "
		    "level %lld offset 0x%llx %s\n",
		    (u_longlong_t)zb->zb_objset,
		    (u_longlong_t)zb->zb_object,
		    (longlong_t)zb->zb_level,
		    (u_longlong_t)blkid2offset(dnp, bp, zb),
		    blkbuf);
	}

	return (0);
}

static void
zdb_leak(space_map_t *sm, uint64_t start, uint64_t size)
{
	vdev_t *vd = sm->sm_ppd;

	(void) printf("leaked space: vdev %llu, offset 0x%llx, size %llu\n",
	    (u_longlong_t)vd->vdev_id, (u_longlong_t)start, (u_longlong_t)size);
}

/* ARGSUSED */
static void
zdb_space_map_load(space_map_t *sm)
{
}

static void
zdb_space_map_unload(space_map_t *sm)
{
	space_map_vacate(sm, zdb_leak, sm);
}

/* ARGSUSED */
static void
zdb_space_map_claim(space_map_t *sm, uint64_t start, uint64_t size)
{
}

static space_map_ops_t zdb_space_map_ops = {
	zdb_space_map_load,
	zdb_space_map_unload,
	NULL,	/* alloc */
	zdb_space_map_claim,
	NULL,	/* free */
	NULL	/* maxsize */
};

static void
zdb_ddt_leak_init(spa_t *spa, zdb_cb_t *zcb)
{
	ddt_bookmark_t ddb = { 0 };
	ddt_entry_t dde;
	int error;

	while ((error = ddt_walk(spa, &ddb, &dde)) == 0) {
		blkptr_t blk;
		ddt_phys_t *ddp = dde.dde_phys;

		if (ddb.ddb_class == DDT_CLASS_UNIQUE)
			return;

		ASSERT(ddt_phys_total_refcnt(&dde) > 1);

		for (int p = 0; p < DDT_PHYS_TYPES; p++, ddp++) {
			if (ddp->ddp_phys_birth == 0)
				continue;
			ddt_bp_create(ddb.ddb_checksum,
			    &dde.dde_key, ddp, &blk);
			if (p == DDT_PHYS_DITTO) {
				zdb_count_block(zcb, NULL, &blk, ZDB_OT_DITTO);
			} else {
				zcb->zcb_dedup_asize +=
				    BP_GET_ASIZE(&blk) * (ddp->ddp_refcnt - 1);
				zcb->zcb_dedup_blocks++;
			}
		}
		if (!dump_opt['L']) {
			ddt_t *ddt = spa->spa_ddt[ddb.ddb_checksum];
			ddt_enter(ddt);
			VERIFY(ddt_lookup(ddt, &blk, B_TRUE) != NULL);
			ddt_exit(ddt);
		}
	}

	ASSERT(error == ENOENT);
}

static void
zdb_leak_init(spa_t *spa, zdb_cb_t *zcb)
{
	zcb->zcb_spa = spa;

	if (!dump_opt['L']) {
		vdev_t *rvd = spa->spa_root_vdev;
		for (int c = 0; c < rvd->vdev_children; c++) {
			vdev_t *vd = rvd->vdev_child[c];
			for (int m = 0; m < vd->vdev_ms_count; m++) {
				metaslab_t *msp = vd->vdev_ms[m];
				mutex_enter(&msp->ms_lock);
				space_map_unload(&msp->ms_map);
				VERIFY(space_map_load(&msp->ms_map,
				    &zdb_space_map_ops, SM_ALLOC, &msp->ms_smo,
				    spa->spa_meta_objset) == 0);
				msp->ms_map.sm_ppd = vd;
				mutex_exit(&msp->ms_lock);
			}
		}
	}

	spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);

	zdb_ddt_leak_init(spa, zcb);

	spa_config_exit(spa, SCL_CONFIG, FTAG);
}

static void
zdb_leak_fini(spa_t *spa)
{
	if (!dump_opt['L']) {
		vdev_t *rvd = spa->spa_root_vdev;
		for (int c = 0; c < rvd->vdev_children; c++) {
			vdev_t *vd = rvd->vdev_child[c];
			for (int m = 0; m < vd->vdev_ms_count; m++) {
				metaslab_t *msp = vd->vdev_ms[m];
				mutex_enter(&msp->ms_lock);
				space_map_unload(&msp->ms_map);
				mutex_exit(&msp->ms_lock);
			}
		}
	}
}

/* ARGSUSED */
static int
count_block_cb(void *arg, const blkptr_t *bp, dmu_tx_t *tx)
{
	zdb_cb_t *zcb = arg;

	if (dump_opt['b'] >= 4) {
		char blkbuf[BP_SPRINTF_LEN];
		sprintf_blkptr(blkbuf, bp);
		(void) printf("[%s] %s\n",
		    "deferred free", blkbuf);
	}
	zdb_count_block(zcb, NULL, bp, ZDB_OT_DEFERRED);
	return (0);
}

static int
dump_block_stats(spa_t *spa)
{
	zdb_cb_t zcb = { 0 };
	zdb_blkstats_t *zb, *tzb;
	uint64_t norm_alloc, norm_space, total_alloc, total_found;
	int flags = TRAVERSE_PRE | TRAVERSE_PREFETCH_METADATA | TRAVERSE_HARD;
	int leaks = 0;

	(void) printf("\nTraversing all blocks %s%s%s%s%s...\n",
	    (dump_opt['c'] || !dump_opt['L']) ? "to verify " : "",
	    (dump_opt['c'] == 1) ? "metadata " : "",
	    dump_opt['c'] ? "checksums " : "",
	    (dump_opt['c'] && !dump_opt['L']) ? "and verify " : "",
	    !dump_opt['L'] ? "nothing leaked " : "");

	/*
	 * Load all space maps as SM_ALLOC maps, then traverse the pool
	 * claiming each block we discover.  If the pool is perfectly
	 * consistent, the space maps will be empty when we're done.
	 * Anything left over is a leak; any block we can't claim (because
	 * it's not part of any space map) is a double allocation,
	 * reference to a freed block, or an unclaimed log block.
	 */
	zdb_leak_init(spa, &zcb);

	/*
	 * If there's a deferred-free bplist, process that first.
	 */
	(void) bpobj_iterate_nofree(&spa->spa_deferred_bpobj,
	    count_block_cb, &zcb, NULL);
	if (spa_version(spa) >= SPA_VERSION_DEADLISTS)
		(void) bpobj_iterate_nofree(&spa->spa_dsl_pool->dp_free_bpobj,
		    count_block_cb, &zcb, NULL);

	if (dump_opt['c'] > 1)
		flags |= TRAVERSE_PREFETCH_DATA;

	zcb.zcb_haderrors |= traverse_pool(spa, 0, flags, zdb_blkptr_cb, &zcb);

	if (zcb.zcb_haderrors) {
		(void) printf("\nError counts:\n\n");
		(void) printf("\t%5s  %s\n", "errno", "count");
		for (int e = 0; e < 256; e++) {
			if (zcb.zcb_errors[e] != 0) {
				(void) printf("\t%5d  %llu\n",
				    e, (u_longlong_t)zcb.zcb_errors[e]);
			}
		}
	}

	/*
	 * Report any leaked segments.
	 */
	zdb_leak_fini(spa);

	tzb = &zcb.zcb_type[ZB_TOTAL][ZDB_OT_TOTAL];

	norm_alloc = metaslab_class_get_alloc(spa_normal_class(spa));
	norm_space = metaslab_class_get_space(spa_normal_class(spa));

	total_alloc = norm_alloc + metaslab_class_get_alloc(spa_log_class(spa));
	total_found = tzb->zb_asize - zcb.zcb_dedup_asize;

	if (total_found == total_alloc) {
		if (!dump_opt['L'])
			(void) printf("\n\tNo leaks (block sum matches space"
			    " maps exactly)\n");
	} else {
		(void) printf("block traversal size %llu != alloc %llu "
		    "(%s %lld)\n",
		    (u_longlong_t)total_found,
		    (u_longlong_t)total_alloc,
		    (dump_opt['L']) ? "unreachable" : "leaked",
		    (longlong_t)(total_alloc - total_found));
		leaks = 1;
	}

	if (tzb->zb_count == 0)
		return (2);

	(void) printf("\n");
	(void) printf("\tbp count:      %10llu\n",
	    (u_longlong_t)tzb->zb_count);
	(void) printf("\tbp logical:    %10llu      avg: %6llu\n",
	    (u_longlong_t)tzb->zb_lsize,
	    (u_longlong_t)(tzb->zb_lsize / tzb->zb_count));
	(void) printf("\tbp physical:   %10llu      avg:"
	    " %6llu     compression: %6.2f\n",
	    (u_longlong_t)tzb->zb_psize,
	    (u_longlong_t)(tzb->zb_psize / tzb->zb_count),
	    (double)tzb->zb_lsize / tzb->zb_psize);
	(void) printf("\tbp allocated:  %10llu      avg:"
	    " %6llu     compression: %6.2f\n",
	    (u_longlong_t)tzb->zb_asize,
	    (u_longlong_t)(tzb->zb_asize / tzb->zb_count),
	    (double)tzb->zb_lsize / tzb->zb_asize);
	(void) printf("\tbp deduped:    %10llu    ref>1:"
	    " %6llu   deduplication: %6.2f\n",
	    (u_longlong_t)zcb.zcb_dedup_asize,
	    (u_longlong_t)zcb.zcb_dedup_blocks,
	    (double)zcb.zcb_dedup_asize / tzb->zb_asize + 1.0);
	(void) printf("\tSPA allocated: %10llu     used: %5.2f%%\n",
	    (u_longlong_t)norm_alloc, 100.0 * norm_alloc / norm_space);

	if (dump_opt['b'] >= 2) {
		int l, t, level;
		(void) printf("\nBlocks\tLSIZE\tPSIZE\tASIZE"
		    "\t  avg\t comp\t%%Total\tType\n");

		for (t = 0; t <= ZDB_OT_TOTAL; t++) {
			char csize[32], lsize[32], psize[32], asize[32];
			char avg[32];
			char *typename;

			if (t < DMU_OT_NUMTYPES)
				typename = dmu_ot[t].ot_name;
			else
				typename = zdb_ot_extname[t - DMU_OT_NUMTYPES];

			if (zcb.zcb_type[ZB_TOTAL][t].zb_asize == 0) {
				(void) printf("%6s\t%5s\t%5s\t%5s"
				    "\t%5s\t%5s\t%6s\t%s\n",
				    "-",
				    "-",
				    "-",
				    "-",
				    "-",
				    "-",
				    "-",
				    typename);
				continue;
			}

			for (l = ZB_TOTAL - 1; l >= -1; l--) {
				level = (l == -1 ? ZB_TOTAL : l);
				zb = &zcb.zcb_type[level][t];

				if (zb->zb_asize == 0)
					continue;

				if (dump_opt['b'] < 3 && level != ZB_TOTAL)
					continue;

				if (level == 0 && zb->zb_asize ==
				    zcb.zcb_type[ZB_TOTAL][t].zb_asize)
					continue;

				zdb_nicenum(zb->zb_count, csize);
				zdb_nicenum(zb->zb_lsize, lsize);
				zdb_nicenum(zb->zb_psize, psize);
				zdb_nicenum(zb->zb_asize, asize);
				zdb_nicenum(zb->zb_asize / zb->zb_count, avg);

				(void) printf("%6s\t%5s\t%5s\t%5s\t%5s"
				    "\t%5.2f\t%6.2f\t",
				    csize, lsize, psize, asize, avg,
				    (double)zb->zb_lsize / zb->zb_psize,
				    100.0 * zb->zb_asize / tzb->zb_asize);

				if (level == ZB_TOTAL)
					(void) printf("%s\n", typename);
				else
					(void) printf("    L%d %s\n",
					    level, typename);
			}
		}
	}

	(void) printf("\n");

	if (leaks)
		return (2);

	if (zcb.zcb_haderrors)
		return (3);

	return (0);
}

typedef struct zdb_ddt_entry {
	ddt_key_t	zdde_key;
	uint64_t	zdde_ref_blocks;
	uint64_t	zdde_ref_lsize;
	uint64_t	zdde_ref_psize;
	uint64_t	zdde_ref_dsize;
	avl_node_t	zdde_node;
} zdb_ddt_entry_t;

/* ARGSUSED */
static int
zdb_ddt_add_cb(spa_t *spa, zilog_t *zilog, const blkptr_t *bp,
    const zbookmark_t *zb, const dnode_phys_t *dnp, void *arg)
{
	avl_tree_t *t = arg;
	avl_index_t where;
	zdb_ddt_entry_t *zdde, zdde_search;

	if (bp == NULL)
		return (0);

	if (dump_opt['S'] > 1 && zb->zb_level == ZB_ROOT_LEVEL) {
		(void) printf("traversing objset %llu, %llu objects, "
		    "%lu blocks so far\n",
		    (u_longlong_t)zb->zb_objset,
		    (u_longlong_t)bp->blk_fill,
		    avl_numnodes(t));
	}

	if (BP_IS_HOLE(bp) || BP_GET_CHECKSUM(bp) == ZIO_CHECKSUM_OFF ||
	    BP_GET_LEVEL(bp) > 0 || dmu_ot[BP_GET_TYPE(bp)].ot_metadata)
		return (0);

	ddt_key_fill(&zdde_search.zdde_key, bp);

	zdde = avl_find(t, &zdde_search, &where);

	if (zdde == NULL) {
		zdde = umem_zalloc(sizeof (*zdde), UMEM_NOFAIL);
		zdde->zdde_key = zdde_search.zdde_key;
		avl_insert(t, zdde, where);
	}

	zdde->zdde_ref_blocks += 1;
	zdde->zdde_ref_lsize += BP_GET_LSIZE(bp);
	zdde->zdde_ref_psize += BP_GET_PSIZE(bp);
	zdde->zdde_ref_dsize += bp_get_dsize_sync(spa, bp);

	return (0);
}

static void
dump_simulated_ddt(spa_t *spa)
{
	avl_tree_t t;
	void *cookie = NULL;
	zdb_ddt_entry_t *zdde;
	ddt_histogram_t ddh_total = { 0 };
	ddt_stat_t dds_total = { 0 };

	avl_create(&t, ddt_entry_compare,
	    sizeof (zdb_ddt_entry_t), offsetof(zdb_ddt_entry_t, zdde_node));

	spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);

	(void) traverse_pool(spa, 0, TRAVERSE_PRE | TRAVERSE_PREFETCH_METADATA,
	    zdb_ddt_add_cb, &t);

	spa_config_exit(spa, SCL_CONFIG, FTAG);

	while ((zdde = avl_destroy_nodes(&t, &cookie)) != NULL) {
		ddt_stat_t dds;
		uint64_t refcnt = zdde->zdde_ref_blocks;
		ASSERT(refcnt != 0);

		dds.dds_blocks = zdde->zdde_ref_blocks / refcnt;
		dds.dds_lsize = zdde->zdde_ref_lsize / refcnt;
		dds.dds_psize = zdde->zdde_ref_psize / refcnt;
		dds.dds_dsize = zdde->zdde_ref_dsize / refcnt;

		dds.dds_ref_blocks = zdde->zdde_ref_blocks;
		dds.dds_ref_lsize = zdde->zdde_ref_lsize;
		dds.dds_ref_psize = zdde->zdde_ref_psize;
		dds.dds_ref_dsize = zdde->zdde_ref_dsize;

		ddt_stat_add(&ddh_total.ddh_stat[highbit(refcnt) - 1], &dds, 0);

		umem_free(zdde, sizeof (*zdde));
	}

	avl_destroy(&t);

	ddt_histogram_stat(&dds_total, &ddh_total);

	(void) printf("Simulated DDT histogram:\n");

	zpool_dump_ddt(&dds_total, &ddh_total);

	dump_dedup_ratio(&dds_total);
}

static void
dump_mos(spa_t *spa, char *opts)
{
	dsl_pool_t *dp = spa_get_dsl(spa);
	char *value = NULL;

	while (*opts != '\0') {
		switch (getsubopt(&opts, mos_opts, &value)) {
		case MOS_OPT_OBJSET:
			dump_dir(dp->dp_meta_objset);
			break;
		case MOS_OPT_DIR:
			dump_zap_contents(dp->dp_meta_objset,
			    DMU_POOL_DIRECTORY_OBJECT, "\nMOS Directory:\n");
			break;
		case MOS_OPT_POOL_PROPS:
			if (spa->spa_pool_props_object)
				dump_zap_contents(dp->dp_meta_objset,
				    spa->spa_pool_props_object,
				    "\nPool properties:\n");
			break;
		case MOS_OPT_METASLAB:
			dump_metaslabs(spa);
			dump_opt['m'] = 0;
			break;
		case MOS_OPT_DTL:
			dump_dtl(spa->spa_root_vdev, 0);
			break;
		case MOS_OPT_SYNC_BPOBJ:
			dump_bpobj(&spa->spa_deferred_bpobj, "Deferred frees");
			if (spa_version(spa) >= SPA_VERSION_DEADLISTS) {
				dump_bpobj(&spa->spa_dsl_pool->dp_free_bpobj,
				    "Pool frees");
			}
			break;
		case MOS_OPT_CONFIG:
			if (dump_opt['C'] < 1)
				dump_nvlist_object(spa->spa_meta_objset,
				    spa->spa_config_object,
				    "\nMOS Configuration:\n");
			break;
		case MOS_OPT_SPARES:
			if (dump_opt['C'] < 3 && spa->spa_spares.sav_object)
				dump_nvlist_object(spa->spa_meta_objset,
				    spa->spa_spares.sav_object,
				    "\nMOS Spares:\n");
			break;
		case MOS_OPT_L2CACHE:
			if (dump_opt['C'] < 3 && spa->spa_l2cache.sav_object)
				dump_nvlist_object(spa->spa_meta_objset,
				    spa->spa_l2cache.sav_object,
				    "\nMOS L2 Caches:\n");
			break;
		case MOS_OPT_HISTORY:
			if (dump_opt['h'] < 1 && spa->spa_history)
				dump_history(spa);
			break;
		case MOS_OPT_ERRLOG_SCRUB:
			if (spa->spa_errlog_scrub)
				dump_zap_contents(dp->dp_meta_objset,
				    spa->spa_errlog_scrub,
				    "\nMOS Scrub Error Log:\n");
			break;
		case MOS_OPT_ERRLOG_LAST:
			if (spa->spa_errlog_last)
				dump_zap_contents(dp->dp_meta_objset,
				    spa->spa_errlog_last,
				    "\nMOS Last Scrub Error Log:\n");
			break;
		case MOS_OPT_RAW_CONFIG:
			if (dump_opt['M'] > 1) {
				(void) fprintf(stderr, "Dumping cached "
				    "configuration in raw form\n");
				dump_raw_config(spa_name(spa),
				    spa->spa_config);
			} else {
				nvlist_t *nv =
				    load_nvlist_object(spa->spa_meta_objset,
				    spa->spa_config_object);
				(void) fprintf(stderr, "Dumping MOS "
				    "configuration in raw form\n");
				dump_raw_config(spa_name(spa), nv);
				nvlist_free(nv);
			}
			break;
		default:
			zdb_err(CE_PANIC, "Unknown -M argument: %s", value);
			break;
		}
	}
}

static void
dump_zpool(spa_t *spa, char *mos_opt)
{
	int rc = 0;

	if (dump_opt['S']) {
		dump_simulated_ddt(spa);
		return;
	}

	if (!dump_opt['e'] && dump_opt['C'] > 1) {
		(void) printf("\nCached configuration:\n");
		dump_nvlist(spa->spa_config, 8);
	}

	if (dump_opt['C'])
		dump_nvlist_object(spa->spa_meta_objset,
		    spa->spa_config_object, "\nMOS Configuration:\n");

	if (dump_opt['C'] > 2 && spa->spa_spares.sav_object)
		dump_nvlist_object(spa->spa_meta_objset,
		    spa->spa_spares.sav_object, "\nMOS Spares:\n");

	if (dump_opt['C'] > 2 && spa->spa_l2cache.sav_object)
		dump_nvlist_object(spa->spa_meta_objset,
		    spa->spa_l2cache.sav_object, "\nMOS L2 Caches:\n");


	if (dump_opt['u'])
		dump_uberblock(&spa->spa_uberblock, "\nUberblock:\n", "\n");

	if (dump_opt['D'])
		dump_all_ddts(spa);

	if (dump_opt['M']) {
		if (mos_opt == NULL) {
			/* verbose dumping - emulate older output */
			if (dump_opt['M'] < 3 || dump_opt['m'])
				dump_mos(spa, mos_opts[MOS_OPT_METASLAB]);
			dump_mos(spa, mos_opts[MOS_OPT_OBJSET]);
			if (dump_opt['M'] >= 3) {
				dump_mos(spa, mos_opts[MOS_OPT_SYNC_BPOBJ]);
				dump_mos(spa, mos_opts[MOS_OPT_DTL]);
			}
		} else if (strstr(mos_opt, "all")) {
			for (int i = 0; i < MOS_OPT_ALL; i++)
				dump_mos(spa, mos_opts[i]);
		} else {
			dump_mos(spa, mos_opt);
		}
	} else if (dump_opt['m']) {
		dump_metaslabs(spa);
	}

	if (dump_opt['d'] || dump_opt['i'])
		(void) dmu_objset_find(spa_name(spa), dump_one_dir, NULL,
		    dump_opt['r'] ? DS_FIND_SNAPSHOTS | DS_FIND_CHILDREN : 0);

	if (dump_opt['b'] || dump_opt['c'])
		rc = dump_block_stats(spa);

	if (dump_opt['s'])
		show_pool_stats(spa);

	if (dump_opt['h'])
		dump_history(spa);

	if (rc != 0)
		exit(rc);
}

#define	ZDB_FLAG_BYTESWAP	0x0001
#define	ZDB_FLAG_DECOMPRESS	0x0002
#define	ZDB_FLAG_FLETCHER4	0x0004
#define	ZDB_FLAG_GANG		0x0008
#define	ZDB_FLAG_INDIRECT	0x0010
#define	ZDB_FLAG_MIRROR		0x0020
#define	ZDB_FLAG_RAW		0x0040
#define	ZDB_FLAG_SHA256		0x0080

int flagbits[256];

static void
zdb_print_blkptr(blkptr_t *bp, int flags)
{
	char blkbuf[BP_SPRINTF_LEN];

	if (flags & ZDB_FLAG_BYTESWAP)
		byteswap_uint64_array((void *)bp, sizeof (blkptr_t));

	sprintf_blkptr(blkbuf, bp);
	(void) printf("%s\n", blkbuf);
}

static void
zdb_dump_indirect(blkptr_t *bp, int nbps, int flags)
{
	for (int i = 0; i < nbps; i++) {
		(void) printf("bp[%2x] = ", i);
		zdb_print_blkptr(&bp[i], flags);
	}
}

static void
zdb_dump_block_raw(void *buf, uint64_t size, int flags)
{
	if (flags & ZDB_FLAG_BYTESWAP)
		byteswap_uint64_array(buf, size);
	(void) write(1, buf, size);
}

static void
zdb_dump_block(void *buf, uint64_t size, int flags)
{
	uint64_t *d = (uint64_t *)buf;
	int nwords = size / sizeof (uint64_t);
	int bswap = !!(flags & ZDB_FLAG_BYTESWAP);
	int i, j;
	char *hdr, *c;
	boolean_t star = B_FALSE;

	if (bswap)
		hdr = " 7 6 5 4 3 2 1 0   f e d c b a 9 8";
	else
		hdr = " 0 1 2 3 4 5 6 7   8 9 a b c d e f";

	(void) printf("%6s   %s  0123456789abcdef\n", "", hdr);

	for (i = 0; i < nwords; i += 2) {
		if (star && d[i] == 0 && d[i + 1] == 0)
			continue;

		(void) printf("%06llx:  %016llx  %016llx  ",
		    (u_longlong_t)(i * sizeof (uint64_t)),
		    (u_longlong_t)(bswap ? BSWAP_64(d[i]) : d[i]),
		    (u_longlong_t)(bswap ? BSWAP_64(d[i + 1]) : d[i + 1]));

		c = (char *)&d[i];
		for (j = 0; j < 2 * sizeof (uint64_t); j++)
			(void) printf("%c", isprint(c[j]) ? c[j] : '.');
		(void) printf("\n");

		star = (d[i] == 0 && d[i + 1] == 0);
		if (star)
			(void) printf("*\n");
	}
}

/*
 * There are two acceptable formats:
 *	leaf_name	  - For example: c1t0d0 or /tmp/ztest.0a
 *	child[.child]*    - For example: 0.1.1
 *
 * The second form can be used to specify arbitrary vdevs anywhere
 * in the hierarchy.  For example, in a pool with a mirror of
 * RAID-Zs, you can specify either RAID-Z vdev with 0.0 or 0.1 .
 */
static vdev_t *
zdb_vdev_lookup(vdev_t *vdev, char *path)
{
	char *s, *p, *q;
	int i;

	if (vdev == NULL)
		return (NULL);

	/* First, assume the x.x.x.x format */
	i = (int)strtoul(path, &s, 10);
	if (s == path || (s && *s != '.' && *s != '\0'))
		goto name;
	if (i < 0 || i >= vdev->vdev_children)
		return (NULL);

	vdev = vdev->vdev_child[i];
	if (*s == '\0')
		return (vdev);
	return (zdb_vdev_lookup(vdev, s+1));

name:
	for (i = 0; i < vdev->vdev_children; i++) {
		vdev_t *vc = vdev->vdev_child[i];

		if (vc->vdev_path == NULL) {
			vc = zdb_vdev_lookup(vc, path);
			if (vc == NULL)
				continue;
			else
				return (vc);
		}

		p = strrchr(vc->vdev_path, '/');
		p = p ? p + 1 : vc->vdev_path;
		q = &vc->vdev_path[strlen(vc->vdev_path) - 2];

		if (strcmp(vc->vdev_path, path) == 0)
			return (vc);
		if (strcmp(p, path) == 0)
			return (vc);
		if (strcmp(q, "s0") == 0 && strncmp(p, path, q - p) == 0)
			return (vc);
	}

	return (NULL);
}

/*
 * Read a block from a pool and print it out.  The syntax of the
 * block descriptor is:
 *
 *	pool vdev_specifier:offset:psize[:flags]
 *
 *	pool           - The name of the pool you wish to read from
 *	vdev_specifier - Which vdev (see comment for zdb_vdev_lookup)
 *	offset         - offset, in hex, in bytes
 *	psize          - Amount of physical data to read, in hex, in bytes
 *	flags          - A string of characters specifying options
 *		 b: Byteswap data before dumping
 *		 d: Decompress data before dumping
 *		 f: Compute fletcher4 checksum for bp
 *		 g: interpret DVA as gang block header
 *		 i: Display as an indirect block
 *		 r: Dump raw data to stdout
 *		 m: mirrored layout on RAID-Z device
 *		 s: Compute SHA256 checksum for bp
 */
static void
zdb_read_block(char *thing, spa_t *spa)
{
	blkptr_t blk, *bp = &blk;
	dva_t *dva = bp->blk_dva;
	int flags = 0;
	uint64_t offset = 0, size = 0;
	uint64_t psize, lsize, asize;
	uint64_t txg = TXG_INITIAL;
	dva_layout_t layout = DVA_LAYOUT_STANDARD;
	enum zio_checksum checksum = ZIO_CHECKSUM_OFF;
	int copies;
	zio_t *zio;
	vdev_t *vd;
	void *pbuf, *lbuf, *buf;
	char *s, *dup, *vdev, *flagstr;
	int i, error;
	int gang = 0;

	dup = strdup(thing);
	s = strtok(dup, ":");
	vdev = s ? s : "";
	s = strtok(NULL, ":");
	offset = strtoull(s ? s : "", NULL, 16);
	s = strtok(NULL, ":");
	size = strtoull(s ? s : "", NULL, 16);
	s = strtok(NULL, ":");
	flagstr = s ? s : "";

	s = NULL;
	if (size == 0)
		s = "size must not be zero";
	if (!IS_P2ALIGNED(size, DEV_BSIZE))
		s = "size must be a multiple of sector size";
	if (!IS_P2ALIGNED(offset, DEV_BSIZE))
		s = "offset must be a multiple of sector size";
	if (s) {
		(void) fprintf(stderr, "Invalid block specifier: %s  - %s\n",
		    thing, s);
		free(dup);
		return;
	}

	for (i = 0; flagstr[i]; i++) {
		int bit = flagbits[(uchar_t)flagstr[i]];

		if (bit == 0) {
			(void) fprintf(stderr, "***Invalid flag: %c\n",
			    flagstr[i]);
			free(dup);
			return;
		}
		flags |= bit;
	}

	vd = zdb_vdev_lookup(spa->spa_root_vdev, vdev);
	if (vd == NULL) {
		(void) fprintf(stderr, "***Invalid vdev: %s\n", vdev);
		free(dup);
		return;
	} else {
		if (vd->vdev_path)
			(void) fprintf(stderr, "Found vdev: %s\n",
			    vd->vdev_path);
		else
			(void) fprintf(stderr, "Found vdev type: %s\n",
			    vd->vdev_ops->vdev_op_type);
	}

	pbuf = umem_alloc(SPA_MAXBLOCKSIZE, UMEM_NOFAIL);
	lbuf = umem_alloc(SPA_MAXBLOCKSIZE, UMEM_NOFAIL);

again:
	psize = size;
	lsize = size;

	copies = vd->vdev_nparity + 1;	/* copies == 1 for non-RAID-Z */

	if (flags & ZDB_FLAG_MIRROR) {
		layout = DVA_LAYOUT_RAIDZ_MIRROR;
		if (psize != (1ULL << vd->vdev_top->vdev_ashift))
			copies = vd->vdev_children;
	}

	asize = vdev_psize_to_asize(vd->vdev_top, psize, layout, copies);

	BP_ZERO(bp);

	DVA_SET_VDEV(&dva[0], vd->vdev_id);
	DVA_SET_OFFSET(&dva[0], offset);
	DVA_SET_GANG(&dva[0], gang);
	DVA_SET_ASIZE(&dva[0], asize);
	DVA_SET_LAYOUT(&dva[0], layout);
	DVA_SET_COPIES(&dva[0], copies);

	BP_SET_BIRTH(bp, txg, txg);

	BP_SET_LSIZE(bp, lsize);
	BP_SET_PSIZE(bp, psize);
	BP_SET_COMPRESS(bp, ZIO_COMPRESS_OFF);
	BP_SET_CHECKSUM(bp, ZIO_CHECKSUM_OFF);
	BP_SET_TYPE(bp, DMU_OT_NONE);
	BP_SET_LEVEL(bp, 0);
	BP_SET_DEDUP(bp, 0);
	BP_SET_BYTEORDER(bp, ZFS_HOST_BYTEORDER);

	spa_config_enter(spa, SCL_STATE, FTAG, RW_READER);
	zio = zio_root(spa, NULL, NULL, ZIO_FLAG_CANFAIL);

	if (vd == vd->vdev_top) {
		/*
		 * Treat this as a normal block read.
		 */
		zio_nowait(zio_read(zio, spa, bp, pbuf, psize, NULL, NULL,
		    ZIO_PRIORITY_SYNC_READ,
		    ZIO_FLAG_CANFAIL | ZIO_FLAG_RAW, NULL));
	} else {
		/*
		 * Treat this as a vdev child I/O.
		 */
		zio->io_vd = vd->vdev_parent;
		zio_nowait(zio_vdev_child_io(zio, bp, vd, vd->vdev_ops,
		    offset, pbuf,
		    P2ROUNDUP(psize, 1ULL << vd->vdev_top->vdev_ashift),
		    ZIO_TYPE_READ, ZIO_PRIORITY_SYNC_READ,
		    ZIO_FLAG_DONT_CACHE | ZIO_FLAG_DONT_QUEUE |
		    ZIO_FLAG_DONT_PROPAGATE | ZIO_FLAG_DONT_RETRY |
		    ZIO_FLAG_CANFAIL | ZIO_FLAG_RAW, NULL, NULL));
	}

	error = zio_wait(zio);
	spa_config_exit(spa, SCL_STATE, FTAG);

	if (error) {
		(void) fprintf(stderr, "Read of %s failed, error: %d\n",
		    thing, error);
		zdb_print_blkptr(bp, flags);
		goto out;
	}

	/*
	 * If this is a gang block, and all we've done so far is read the
	 * gang header, use its contents to figure out the correct birth time
	 * (so we can generate a valid gang verifier) and correct size,
	 * and then read the block again with the DVA's gang bit set
	 * to get the actual contents.
	 */
	if ((flags & ZDB_FLAG_GANG) && gang == 0) {
		zio_gbh_phys_t *gbh = pbuf;
		size = 0;
		gang = 1;
		for (int i = 0; i < SPA_GBH_NBLKPTRS; i++) {
			blkptr_t *gbp = &gbh->zg_blkptr[i];
			if (gbp->blk_birth == 0)
				continue;
			txg = BP_PHYSICAL_BIRTH(gbp);
			size += BP_GET_PSIZE(gbp);
		}
		goto again;
	}

	if (flags & ZDB_FLAG_DECOMPRESS) {
		/*
		 * We don't know how the data was compressed, so just try
		 * every decompress function at every inflated blocksize.
		 */
		enum zio_compress c;
		void *pbuf2 = umem_alloc(SPA_MAXBLOCKSIZE, UMEM_NOFAIL);

		for (lsize = SPA_MAXBLOCKSIZE; lsize > psize;
		    lsize -= SPA_MINBLOCKSIZE) {
			for (c = 0; c < ZIO_COMPRESS_FUNCTIONS; c++) {
				if (zio_decompress_data(c, pbuf, lbuf,
				    psize, lsize) == 0 &&
				    zio_compress_data(c, lbuf, pbuf2,
				    lsize) == psize &&
				    bcmp(pbuf, pbuf2, psize) == 0) {
					BP_SET_COMPRESS(bp, c);
					BP_SET_LSIZE(bp, lsize);
					break;
				}
			}
			if (c != ZIO_COMPRESS_FUNCTIONS)
				break;
			lsize -= SPA_MINBLOCKSIZE;
		}

		umem_free(pbuf2, SPA_MAXBLOCKSIZE);

		if (lsize <= psize) {
			(void) fprintf(stderr, "Decompress of %s failed\n",
			    thing);
			goto out;
		}
		buf = lbuf;
		size = lsize;
	} else {
		buf = pbuf;
		size = psize;
	}

	if (flags & ZDB_FLAG_FLETCHER4)
		checksum = ZIO_CHECKSUM_FLETCHER_4;

	if (flags & ZDB_FLAG_SHA256)
		checksum = ZIO_CHECKSUM_SHA256;

	if (checksum != ZIO_CHECKSUM_OFF) {
		BP_SET_CHECKSUM(bp, checksum);
		zio_checksum_table[checksum].ci_func[
		    !!(flags & ZDB_FLAG_BYTESWAP)](pbuf, psize, &bp->blk_cksum);
	}

	if (!(flags & ZDB_FLAG_RAW))
		zdb_print_blkptr(bp, flags);

	if (flags & ZDB_FLAG_RAW)
		zdb_dump_block_raw(buf, size, flags);
	else if (flags & ZDB_FLAG_INDIRECT)
		zdb_dump_indirect(buf, size / sizeof (blkptr_t), flags);
	else
		zdb_dump_block(buf, size, flags);

out:
	umem_free(pbuf, SPA_MAXBLOCKSIZE);
	umem_free(lbuf, SPA_MAXBLOCKSIZE);
	free(dup);
}

static boolean_t
pool_match(nvlist_t *cfg, char *tgt)
{
	uint64_t v, guid = strtoull(tgt, NULL, 0);
	char *s;

	if (guid != 0) {
		if (nvlist_lookup_uint64(cfg, ZPOOL_CONFIG_POOL_GUID, &v) == 0)
			return (v == guid);
	} else {
		if (nvlist_lookup_string(cfg, ZPOOL_CONFIG_POOL_NAME, &s) == 0)
			return (strcmp(s, tgt) == 0);
	}
	return (B_FALSE);
}

static char *
find_zpool(char **target, nvlist_t **configp, importargs_t *args)
{
	nvlist_t *pools;
	nvlist_t *match = NULL;
	char *name = NULL;
	char *sepp = NULL;
	char sep;
	int count = 0;

	if ((sepp = strpbrk(*target, "/@")) != NULL) {
		sep = *sepp;
		*sepp = '\0';
	}

	args->can_be_active = B_TRUE;
	args->trust_cache = B_TRUE;

	pools = zpool_search_import(g_zfs, args);

	if (pools != NULL) {
		nvpair_t *elem = NULL;

		while ((elem = nvlist_next_nvpair(pools, elem)) != NULL) {
			verify(nvpair_value_nvlist(elem, configp) == 0);
			if (pool_match(*configp, *target)) {
				count++;
				if (match != NULL) {
					/* print previously found config */
					if (name != NULL) {
						(void) printf("%s\n", name);
						dump_nvlist(match, 8);
						name = NULL;
					}
					(void) printf("%s\n",
					    nvpair_name(elem));
					dump_nvlist(*configp, 8);
				} else {
					match = *configp;
					name = nvpair_name(elem);
				}
			}
		}
	}
	if (count > 1)
		zdb_err(CE_PANIC, "\tMatched %d pools - use pool GUID "
		    "instead of pool name or \n"
		    "\tpool name part of a dataset name to select pool", count);

	if (sepp)
		*sepp = sep;
	/*
	 * If pool GUID was specified for pool id, replace it with pool name
	 */
	if (name && (strstr(*target, name) != *target)) {
		int sz = 1 + strlen(name) + ((sepp) ? strlen(sepp) : 0);

		*target = umem_alloc(sz, UMEM_NOFAIL);
		(void) snprintf(*target, sz, "%s%s", name, sepp ? sepp : "");
	}

	*configp = name ? match : NULL;

	return (name);
}

int
main(int argc, char **argv)
{
	int i, c;
	struct rlimit rl = { 1024, 1024 };
	spa_t *spa = NULL;
	int dump_all = 1;
	int verbose = 0;
	int error = 0;
	importargs_t args = { 0 };
	char *target;
	nvlist_t *policy = NULL;
	uint64_t max_txg = UINT64_MAX;
	int rewind = ZPOOL_NEVER_REWIND | ZPOOL_RETRY_LOAD;
	char *mos_opt = NULL;
	boolean_t target_is_pool;
	boolean_t target_is_snap;

	(void) setrlimit(RLIMIT_NOFILE, &rl);
	(void) enable_extended_FILE_stdio(-1, -1);

	dprintf_setup(&argc, argv);

	while ((c = getopt(argc, argv,
	    "bcdhilmrsuCDRSAFILXevp:t:M:U:P")) != -1) {
		switch (c) {
		case 'b':
		case 'c':
		case 'd':
		case 'h':
		case 'i':
		case 'l':
		case 'm':
		case 's':
		case 'u':
		case 'C':
		case 'D':
		case 'R':
		case 'S':
			dump_opt[c]++;
			dump_all = 0;
			break;
		case 'A':
		case 'F':
		case 'I':
		case 'L':
		case 'X':
		case 'e':
		case 'P':
		case 'r':
			dump_opt[c]++;
			break;
		case 'v':
			verbose++;
			break;
		case 'p':
			if (args.path == NULL) {
				args.path = umem_alloc(sizeof (char *),
				    UMEM_NOFAIL);
			} else {
				char **tmp = umem_alloc((args.paths + 1) *
				    sizeof (char *), UMEM_NOFAIL);
				bcopy(args.path, tmp, args.paths *
				    sizeof (char *));
				umem_free(args.path,
				    args.paths * sizeof (char *));
				args.path = tmp;
			}
			args.path[args.paths++] = optarg;
			break;
		case 't':
			max_txg = strtoull(optarg, NULL, 0);
			if (max_txg < TXG_INITIAL) {
				(void) fprintf(stderr, "incorrect txg "
				    "specified: %s\n", optarg);
				usage();
			}
			break;
		case 'M':
			dump_opt[c]++;
			dump_all = 0;
			while (*optarg == c) {
				dump_opt[c]++;
				optarg++;
			}
			mos_opt = (*optarg == '\0') ? argv[optind++] : optarg;
			if (mos_opt && mos_opt[0] == '-') {
				(void) fprintf(stderr, "argument for -M "
				    "is missing\n");
				usage();
			}
			break;
		case 'U':
			args.cachefile = optarg;
			break;
		default:
			usage();
			break;
		}
	}

	if (dump_opt['e'])
		spa_config_path = "/dev/null";
	else if (args.cachefile)
		spa_config_path = args.cachefile;

	if (!dump_opt['e'] && args.path)
		zdb_err(CE_PANIC, "option -p requires use of -e");

	kernel_init(FREAD);
	g_zfs = libzfs_init();
	ASSERT(g_zfs != NULL);
	libzfs_print_on_error(g_zfs, B_TRUE);

	for (c = 0; c < 256; c++) {
		if (dump_all && !strchr("elrAFILPRSX", c))
			dump_opt[c] = 1;
		if (dump_opt[c])
			dump_opt[c] += verbose;
	}

	aok = (dump_opt['A'] == 1) || (dump_opt['A'] > 2);
	zfs_recover = (dump_opt['A'] > 1);

	argc -= optind;
	argv += optind;

	if (argc < 2 && dump_opt['R'])
		usage();
	if (argc < 1) {
		if (!dump_opt['e'] && dump_opt['C']) {
			return (dump_cachefile(spa_config_path));
		}
		usage();
	}

	if (dump_opt['l']) {
		dump_label(argv[0]);
		return (0);
	}

	if (dump_opt['X'] || dump_opt['F'])
		rewind = ZPOOL_DO_REWIND |
		    (dump_opt['X'] ? ZPOOL_EXTREME_REWIND : 0);

	if (nvlist_alloc(&policy, NV_UNIQUE_NAME_TYPE, 0) != 0 ||
	    nvlist_add_uint64(policy, ZPOOL_LOAD_REWIND_TXG, max_txg) != 0 ||
	    nvlist_add_uint32(policy, ZPOOL_LOAD_REWIND, rewind) != 0)
		zdb_err(CE_PANIC, "internal error: %s", strerror(ENOMEM));

	target = argv[0];

	target_is_snap = (strchr(target, '@') != NULL);
	target_is_pool = !target_is_snap && (strchr(target, '/') == NULL);

	if (dump_opt['e']) {
		nvlist_t *cfg = NULL;
		char *name = find_zpool(&target, &cfg, &args);

		error = ENOENT;
		if (name) {
			if (dump_opt['C'] > 1) {
				(void) printf("\nConfiguration for import:\n");
				dump_nvlist(cfg, 8);
			}
			if (!dump_opt['C'] && dump_opt['M'] && (0 ==
			    strcmp(mos_opt, mos_opts[MOS_OPT_RAW_CONFIG]))) {
				(void) fprintf(stderr, "Dumping configuration "
				    "for import in raw form\n");
				dump_raw_config(name, cfg);
				return (0);
			}
			if (nvlist_add_nvlist(cfg,
			    ZPOOL_LOAD_POLICY, policy) != 0) {
				zdb_err(CE_PANIC, "can't open '%s': %s",
				    target, strerror(ENOMEM));
			}
			if ((error = spa_import(name, cfg, NULL,
			    ZFS_IMPORT_MISSING_LOG)) != 0) {
				error = spa_import(name, cfg, NULL,
				    ZFS_IMPORT_VERBATIM);
			}
		}
	}

	if (error == 0) {
		error = spa_open_policy(target, &spa, FTAG, policy, NULL);
		if (error) {
			/*
			 * If we're missing the log device or ignoring
			 * errors then try opening the pool after clearing
			 * the log state.
			 */
			mutex_enter(&spa_namespace_lock);
			if ((spa = spa_lookup(target)) != NULL &&
			    (spa_get_log_state(spa) == SPA_LOG_MISSING ||
			    dump_opt['I'])) {
				spa_set_log_state(spa, SPA_LOG_CLEAR);
				error = 0;
			}
			mutex_exit(&spa_namespace_lock);

			if (!error) {
				error = spa_open_policy(target, &spa,
				    FTAG, policy, NULL);
			}
		}
	}
	nvlist_free(policy);

	if (error)
		zdb_err(CE_PANIC, "can't open '%s': %s", target,
		    strerror(error));

	argv++;
	argc--;
	if (dump_opt['M'] && mos_opt &&
	    strstr(mos_opt, mos_opts[MOS_OPT_RAW_CONFIG])) {
		dump_mos(spa, mos_opts[MOS_OPT_RAW_CONFIG]);
	} else if (!dump_opt['R']) {
		if (argc > 0) {
			zopt_objects = argc;
			zopt_object = calloc(zopt_objects, sizeof (uint64_t));
			for (i = 0; i < zopt_objects; i++) {
				errno = 0;
				zopt_object[i] = strtoull(argv[i], NULL, 0);
				if (zopt_object[i] == 0 && errno != 0)
					zdb_err(CE_PANIC, "bad number %s: %s",
					    argv[i], strerror(errno));
			}
		}
		if (target_is_pool) {
			dump_zpool(spa, mos_opt);
		} else if (target_is_snap) {
			(void) dump_one_dir(target, NULL);
		} else {
			int flg = dump_opt['r'] ?
			    DS_FIND_SNAPSHOTS | DS_FIND_CHILDREN : 0;
			error = dmu_objset_find(target,
			    dump_one_dir, NULL, flg);
			if (error) {
				zdb_err(CE_PANIC, "can't find '%s': %s", target,
				    strerror(error));
			}
		}
	} else {
		flagbits['b'] = ZDB_FLAG_BYTESWAP;
		flagbits['d'] = ZDB_FLAG_DECOMPRESS;
		flagbits['f'] = ZDB_FLAG_FLETCHER4;
		flagbits['g'] = ZDB_FLAG_GANG;
		flagbits['i'] = ZDB_FLAG_INDIRECT;
		flagbits['m'] = ZDB_FLAG_MIRROR;
		flagbits['r'] = ZDB_FLAG_RAW;
		flagbits['s'] = ZDB_FLAG_SHA256;

		for (i = 0; i < argc; i++)
			zdb_read_block(argv[i], spa);
	}

	spa_close(spa, FTAG);

	fuid_table_destroy();
	sa_loaded = B_FALSE;

	libzfs_fini(g_zfs);
	kernel_fini();

	return (0);
}
