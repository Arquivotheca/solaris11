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

#include <mdb/mdb_modapi.h>
#include <rpc/svc.h>

int stp_info(uintptr_t, uint_t, int, const mdb_arg_t *);
void stp_info_help(void);

struct stp_opts {
	int optlen;
	int *opts;
};

/*
 * Convenience routine for looking up the globals given a zone pointer and
 * name of zsd_key.
 */
struct zsd_lookup {
	zone_key_t	key;
	uintptr_t	value;
};

/* ARGSUSED */
static int
find_globals_cb(uintptr_t addr, const void *data, void *private)
{
	struct zsd_lookup *zlp = private;
	zone_key_t key = zlp->key;
	const struct zsd_entry *zep = data;

	if (zep->zsd_key != key)
		return (WALK_NEXT);
	zlp->value = (uintptr_t)zep->zsd_data;
	return (WALK_DONE);
}

static uintptr_t
find_globals_impl(uintptr_t zoneaddr, const char *keyname, zone_key_t key,
    uint_t quiet)
{
	struct zsd_lookup zl;

	zl.key = key;
	zl.value = NULL;
	if (mdb_pwalk("zsd", find_globals_cb, &zl, zoneaddr) == -1) {
		mdb_warn("couldn't walk zsd");
		return (NULL);
	}
	if (quiet == FALSE) {
		if (zl.value == NULL) {
			if (keyname != NULL)
				mdb_warn("unable to find a registered ZSD "
				    "value for keyname %s\n", keyname);
			else
				mdb_warn("unable to find a registered ZSD "
				    "value for key %d\n", key);
		}
	}
	return (zl.value);
}

static uintptr_t
find_globals(uintptr_t zoneaddr, const char *keyname, uint_t quiet)
{
	zone_key_t key;

	if (mdb_readsym(&key, sizeof (zone_key_t), keyname) !=
	    sizeof (zone_key_t)) {
		mdb_warn("unable to read %s", keyname);
		return (NULL);
	}

	return (find_globals_impl(zoneaddr, keyname, key, quiet));
}

/* ARGSUSED */
int
stp_print(uintptr_t addr, void *unknown, void *opts)
{
	SVCTASKPOOL stpool;
	struct stp_opts *options = (struct stp_opts *)opts;
	int i, *optval;

	if (mdb_vread(&stpool, sizeof (SVCTASKPOOL), addr) == -1) {
		mdb_warn("failed to read SVCTASKPOOL from %p\n", addr);
		return (WALK_ERR);
	}

	/*
	 * Pool options specified, print only if the poolid
	 * is in the options.
	 */
	if (options->optlen != 0) {
		optval = options->opts;
		for (i = 0; i < options->optlen; i++) {
			if (stpool.stp_id == *optval++)
				goto print;
		}
		return (DCMD_OK);
	}
print:
	mdb_printf("poolid = %s(%x) \n svctaskpool %p \n",
	    stpool.stp_id == NFS_SVCPOOL_ID ? "NFS" :
	    stpool.stp_id == NLM_SVCPOOL_ID ? "NLM" :
	    stpool.stp_id == NFS_CB_SVCPOOL_ID ? "NFS_CB" : "",
	    stpool.stp_id, addr);
	mdb_printf("taskq %p\n", stpool.stp_tq);
	mdb_printf("flags %x\n", stpool.stp_flags);
	mdb_printf("# of queues = %d\n", stpool.stp_qarray_len);
	mdb_printf("maxthreads = %d\n", stpool.stp_maxthreads);
	mdb_printf("\n");

	return (DCMD_OK);
}

/* ARGSUSED */
int
stp_walk_pools(uintptr_t zoneaddr, void *unknown, void *opts)
{
	uintptr_t svc_addr;
	struct svc_globals *svcg;
	struct zone stzone;
	char zname[ZONENAME_MAX];

	svc_addr = find_globals(zoneaddr, "svc_zone_key", FALSE);
	svcg = (struct svc_globals *)svc_addr;

	if (mdb_vread(&stzone, sizeof (struct zone),
	    zoneaddr) == -1) {
		mdb_warn("failed to read the zone %p\n", zoneaddr);
		return (DCMD_ERR);
	}

	if (mdb_vread(zname, ZONENAME_MAX, (uintptr_t)stzone.zone_name) == -1) {
		mdb_warn("failed to read the zone name %p\n", zoneaddr);
		return (DCMD_ERR);
	}

	mdb_printf("==== %s zone addr %p ====\n", zname, zoneaddr);

	if (mdb_pwalk("list", (mdb_walk_cb_t)stp_print, opts,
	    (uintptr_t)(&svcg->sg_stp_list)) == -1) {
		mdb_warn("Failed to walk sg_stp_list");
		return (DCMD_ERR);
	}
	return (DCMD_OK);
}

int
stp_info(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	int aflag = 0;
	int first, i, optlen;
	int *optval;
	int status = DCMD_OK;
	uintptr_t zoneaddr;
	struct stp_opts options;

	first = mdb_getopts(argc, argv,
	    'a', MDB_OPT_SETBITS, TRUE, &aflag, NULL);

	if (!(flags & DCMD_ADDRSPEC)) {
		if (mdb_readsym(&zoneaddr, sizeof (uintptr_t),
		    "global_zone") == -1) {
			mdb_warn("unable to locate global_zone");
			return (WALK_ERR);
		}
	} else {
		zoneaddr = addr;
	}

	optlen = argc - first;
	options.opts = mdb_alloc(sizeof (int) * optlen, UM_SLEEP|UM_GC);
	optval = options.opts;
	optlen = 0;

	for (i = first; i < argc; i++) {
		if (argv[i].a_type != MDB_TYPE_IMMEDIATE) {
			if (argv[i].a_type == MDB_TYPE_STRING) {
				if (strcasecmp(argv[i].a_un.a_str,
				    "nfs") == 0) {
					*optval++ = NFS_SVCPOOL_ID;
					optlen++;
				} else if (strcasecmp(argv[i].a_un.a_str,
				    "nlm") == 0) {
					*optval++ = NLM_SVCPOOL_ID;
					optlen++;
				} else if (strcasecmp(argv[i].a_un.a_str,
				    "nfs_cb") == 0) {
					*optval++ = NFS_CB_SVCPOOL_ID;
					optlen++;
				} else {
					status = DCMD_USAGE;
					goto error;
				}
			} else {
				status = DCMD_USAGE;
				goto error;
			}
		}
	}

	options.optlen = optlen;

	if (!aflag) {
		return (stp_walk_pools(zoneaddr, NULL, (void *)&options));
	}

	if (mdb_pwalk("zone", (mdb_walk_cb_t)stp_walk_pools,
	    (void *)&options, NULL) == -1) {
		mdb_warn("Failed to walk stp_list");
		return (DCMD_ERR);
	}
error:
	return (status);
}

void
stp_info_help(void)
{
	mdb_printf(
	"<zoneaddr::stp_info [-a] [ $[poolid] | nfs | nlm | nfs_cb ]\n"
	"Given a zoneaddr prints RPC task pool information"
	" for the specified zone\n"
	"If no address is specified and option -a is specified"
	" walks all the zones\n"
	"otherwise only the global zone information is printed."
	" If the poolid list\n"
	"is specified, prints information for the specified pools."
	" This list can be\n"
	"$[numeric] or verbose: nfs or nlm or nfs_cb\n");
}


static const mdb_dcmd_t dcmds[] = {
	{ "stp_info", "[-a] [poolid...]", "RPC task pool information",
	    stp_info, stp_info_help },
	{NULL}
};

static const mdb_modinfo_t modinfo = {
	MDB_API_VERSION, dcmds, NULL
};

const mdb_modinfo_t *
_mdb_init(void)
{
	return (&modinfo);
}
