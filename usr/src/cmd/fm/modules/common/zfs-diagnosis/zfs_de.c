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
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <assert.h>
#include <stddef.h>
#include <strings.h>
#include <libuutil.h>
#include <libzfs.h>
#include <fm/fmd_api.h>
#include <fm/libtopo.h>
#include <fm/topo_hc.h>
#include <fm/fmd_fmri.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/libdevid.h>
#include <sys/fs/zfs.h>
#include <sys/fm/protocol.h>
#include <sys/fm/fs/zfs.h>

/*
 * Our serd engines are named 'zfs_<pool_guid>_<vdev_guid>_{checksum,io}'.  This
 * #define reserves enough space for two 64-bit hex values plus the length of
 * the longest string.
 */
#define	MAX_SERDLEN	(16 * 2 + sizeof ("zfs___checksum"))

/*
 * On-disk case structure.  This must maintain backwards compatibility with
 * previous versions of the DE.  By default, any members appended to the end
 * will be filled with zeros if they don't exist in a previous version.
 */
typedef struct zfs_case_data {
	uint64_t	zc_version;
	uint64_t	zc_ena;
	uint64_t	zc_pool_guid;
	uint64_t	zc_vdev_guid;
	int		zc_has_timer;		/* defunct */
	int		zc_pool_state;
	char		zc_serd_checksum[MAX_SERDLEN];
	char		zc_serd_io[MAX_SERDLEN];
	int		zc_has_remove_timer;
} zfs_case_data_t;

/*
 * Time-of-day
 */
typedef struct er_timeval {
	uint64_t	ertv_sec;
	uint64_t	ertv_nsec;
} er_timeval_t;

/*
 * In-core case structure.
 */
typedef struct zfs_case {
	boolean_t	zc_present;
	uint32_t	zc_version;
	zfs_case_data_t	zc_data;
	fmd_case_t	*zc_case;
	uu_list_node_t	zc_node;
	id_t		zc_remove_timer;
	char		*zc_fru;
	er_timeval_t	zc_when;
} zfs_case_t;

#define	CASE_DATA			"data"
#define	CASE_FRU			"fru"
#define	CASE_DATA_VERSION_INITIAL	1
#define	CASE_DATA_VERSION_SERD		2

typedef struct zfs_de_stats {
	fmd_stat_t	old_drops;
	fmd_stat_t	dev_drops;
	fmd_stat_t	vdev_drops;
	fmd_stat_t	import_drops;
	fmd_stat_t	resource_drops;
	fmd_stat_t	cksum_drops;
} zfs_de_stats_t;

zfs_de_stats_t zfs_stats = {
	{ "old_drops", FMD_TYPE_UINT64, "ereports dropped (from before load)" },
	{ "dev_drops", FMD_TYPE_UINT64, "ereports dropped (dev during open)"},
	{ "vdev_drops", FMD_TYPE_UINT64, "ereports dropped (weird vdev types)"},
	{ "import_drops", FMD_TYPE_UINT64, "ereports dropped (during import)" },
	{ "resource_drops", FMD_TYPE_UINT64, "resource related ereports" },
	{ "checksum_drops", FMD_TYPE_UINT64, "ereports dropped (checksum)" }
};

static hrtime_t zfs_remove_timeout;

uu_list_pool_t *zfs_case_pool;
uu_list_t *zfs_cases;

#define	ZFS_MAKE_RSRC(type)	\
    FM_RSRC_CLASS "." ZFS_ERROR_CLASS "." type
#define	ZFS_MAKE_EREPORT(type)	\
    FM_EREPORT_CLASS "." ZFS_ERROR_CLASS "." type

/*
 * Write out the persistent representation of an active case.
 */
static void
zfs_case_serialize(fmd_hdl_t *hdl, zfs_case_t *zcp)
{
	/*
	 * Always update cases to the latest version, even if they were the
	 * previous version when unserialized.
	 */
	zcp->zc_data.zc_version = CASE_DATA_VERSION_SERD;
	fmd_buf_write(hdl, zcp->zc_case, CASE_DATA, &zcp->zc_data,
	    sizeof (zcp->zc_data));

	if (zcp->zc_fru != NULL)
		fmd_buf_write(hdl, zcp->zc_case, CASE_FRU, zcp->zc_fru,
		    strlen(zcp->zc_fru));
}

/*
 * Read back the persistent representation of an active case.
 */
static zfs_case_t *
zfs_case_unserialize(fmd_hdl_t *hdl, fmd_case_t *cp)
{
	zfs_case_t *zcp;
	size_t frulen;

	zcp = fmd_hdl_zalloc(hdl, sizeof (zfs_case_t), FMD_SLEEP);
	zcp->zc_case = cp;

	fmd_buf_read(hdl, cp, CASE_DATA, &zcp->zc_data,
	    sizeof (zcp->zc_data));

	if (zcp->zc_data.zc_version > CASE_DATA_VERSION_SERD) {
		fmd_hdl_free(hdl, zcp, sizeof (zfs_case_t));
		return (NULL);
	}

	if ((frulen = fmd_buf_size(hdl, zcp->zc_case, CASE_FRU)) > 0) {
		zcp->zc_fru = fmd_hdl_alloc(hdl, frulen + 1, FMD_SLEEP);
		fmd_buf_read(hdl, zcp->zc_case, CASE_FRU, zcp->zc_fru,
		    frulen);
		zcp->zc_fru[frulen] = '\0';
	}

	/*
	 * fmd_buf_read() will have already zeroed out the remainder of the
	 * buffer, so we don't have to do anything special if the version
	 * doesn't include the SERD engine name.
	 */

	if (zcp->zc_data.zc_has_remove_timer)
		zcp->zc_remove_timer = fmd_timer_install(hdl, zcp,
		    NULL, zfs_remove_timeout);

	(void) uu_list_insert_before(zfs_cases, NULL, zcp);

	fmd_case_setspecific(hdl, cp, zcp);

	return (zcp);
}

/*
 * Iterate over any active cases.  If any cases are associated with a pool or
 * vdev which is no longer present on the system, close the associated case.
 */
static void
zfs_mark_vdev(uint64_t pool_guid, nvlist_t *vd, er_timeval_t *loaded)
{
	uint64_t vdev_guid;
	uint_t c, children;
	nvlist_t **child;
	zfs_case_t *zcp;
	int ret;

	ret = nvlist_lookup_uint64(vd, ZPOOL_CONFIG_GUID, &vdev_guid);
	assert(ret == 0);

	/*
	 * Mark any cases associated with this (pool, vdev) pair.
	 */
	for (zcp = uu_list_first(zfs_cases); zcp != NULL;
	    zcp = uu_list_next(zfs_cases, zcp)) {
		if (zcp->zc_data.zc_pool_guid == pool_guid &&
		    zcp->zc_data.zc_vdev_guid == vdev_guid) {
			zcp->zc_present = B_TRUE;
			zcp->zc_when = *loaded;
		}
	}

	/*
	 * Iterate over all children.
	 */
	if (nvlist_lookup_nvlist_array(vd, ZPOOL_CONFIG_CHILDREN, &child,
	    &children) == 0) {
		for (c = 0; c < children; c++)
			zfs_mark_vdev(pool_guid, child[c], loaded);
	}

	if (nvlist_lookup_nvlist_array(vd, ZPOOL_CONFIG_L2CACHE, &child,
	    &children) == 0) {
		for (c = 0; c < children; c++)
			zfs_mark_vdev(pool_guid, child[c], loaded);
	}

	if (nvlist_lookup_nvlist_array(vd, ZPOOL_CONFIG_SPARES, &child,
	    &children) == 0) {
		for (c = 0; c < children; c++)
			zfs_mark_vdev(pool_guid, child[c], loaded);
	}
}

/*ARGSUSED*/
static int
zfs_mark_pool(zpool_handle_t *zhp, void *unused)
{
	zfs_case_t *zcp;
	uint64_t pool_guid;
	uint64_t *tod;
	er_timeval_t loaded = { 0 };
	nvlist_t *config, *vd;
	uint_t nelem = 0;
	int ret;

	pool_guid = zpool_get_prop_int(zhp, ZPOOL_PROP_GUID, NULL);
	/*
	 * Mark any cases associated with just this pool.
	 */
	for (zcp = uu_list_first(zfs_cases); zcp != NULL;
	    zcp = uu_list_next(zfs_cases, zcp)) {
		if (zcp->zc_data.zc_pool_guid == pool_guid &&
		    zcp->zc_data.zc_vdev_guid == 0)
			zcp->zc_present = B_TRUE;
	}

	if ((config = zpool_get_config(zhp, NULL)) == NULL) {
		zpool_close(zhp);
		return (-1);
	}

	(void) nvlist_lookup_uint64_array(config, ZPOOL_CONFIG_LOADED_TIME,
	    &tod, &nelem);
	if (nelem == 2) {
		loaded.ertv_sec = tod[0];
		loaded.ertv_nsec = tod[1];
		for (zcp = uu_list_first(zfs_cases); zcp != NULL;
		    zcp = uu_list_next(zfs_cases, zcp)) {
			if (zcp->zc_data.zc_pool_guid == pool_guid &&
			    zcp->zc_data.zc_vdev_guid == 0) {
				zcp->zc_when = loaded;
			}
		}
	}

	ret = nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE, &vd);
	assert(ret == 0);

	zfs_mark_vdev(pool_guid, vd, &loaded);

	zpool_close(zhp);

	return (0);
}

struct load_time_arg {
	uint64_t lt_guid;
	er_timeval_t *lt_time;
	boolean_t lt_found;
};

static int
zpool_find_load_time(zpool_handle_t *zhp, void *arg)
{
	struct load_time_arg *lta = arg;
	uint64_t pool_guid;
	uint64_t *tod;
	nvlist_t *config;
	uint_t nelem;

	if (lta->lt_found) {
		zpool_close(zhp);
		return (0);
	}

	pool_guid = zpool_get_prop_int(zhp, ZPOOL_PROP_GUID, NULL);
	if (pool_guid != lta->lt_guid) {
		zpool_close(zhp);
		return (0);
	}

	if ((config = zpool_get_config(zhp, NULL)) == NULL) {
		zpool_close(zhp);
		return (-1);
	}

	if (nvlist_lookup_uint64_array(config, ZPOOL_CONFIG_LOADED_TIME,
	    &tod, &nelem) == 0 && nelem == 2) {
		lta->lt_found = B_TRUE;
		lta->lt_time->ertv_sec = tod[0];
		lta->lt_time->ertv_nsec = tod[1];
	}

	zpool_close(zhp);
	return (0);
}

static void
zfs_purge_cases(fmd_hdl_t *hdl)
{
	zfs_case_t *zcp;
	uu_list_walk_t *walk;
	libzfs_handle_t *zhdl = fmd_hdl_getspecific(hdl);

	/*
	 * There is no way to open a pool by GUID, or lookup a vdev by GUID.  No
	 * matter what we do, we're going to have to stomach a O(vdevs * cases)
	 * algorithm.  In reality, both quantities are likely so small that
	 * neither will matter. Given that iterating over pools is more
	 * expensive than iterating over the in-memory case list, we opt for a
	 * 'present' flag in each case that starts off cleared.  We then iterate
	 * over all pools, marking those that are still present, and removing
	 * those that aren't found.
	 *
	 * Note that we could also construct an FMRI and rely on
	 * fmd_nvl_fmri_presence_state(), but this would end up doing the same
	 * search.
	 */

	/*
	 * Mark the cases an not present.
	 */
	for (zcp = uu_list_first(zfs_cases); zcp != NULL;
	    zcp = uu_list_next(zfs_cases, zcp))
		zcp->zc_present = B_FALSE;

	/*
	 * Iterate over all pools and mark the pools and vdevs found.  If this
	 * fails (most probably because we're out of memory), then don't close
	 * any of the cases and we cannot be sure they are accurate.
	 */
	if (zpool_iter(zhdl, zfs_mark_pool, NULL) != 0)
		return;

	/*
	 * Remove those cases which were not found.
	 */
	walk = uu_list_walk_start(zfs_cases, UU_WALK_ROBUST);
	while ((zcp = uu_list_walk_next(walk)) != NULL) {
		if (!zcp->zc_present)
			fmd_case_close(hdl, zcp->zc_case);
	}
	uu_list_walk_end(walk);
}

/*
 * Construct the name of a serd engine given the pool/vdev GUID and type (io or
 * checksum).
 */
static void
zfs_serd_name(char *buf, uint64_t pool_guid, uint64_t vdev_guid,
    const char *type)
{
	(void) snprintf(buf, MAX_SERDLEN, "zfs_%llx_%llx_%s", pool_guid,
	    vdev_guid, type);
}

/*
 * Solve a given ZFS case.  This first checks to make sure the diagnosis is
 * still valid, as well as cleaning up any pending timer associated with the
 * case.
 */
static void
zfs_case_solve(fmd_hdl_t *hdl, zfs_case_t *zcp, const char *faultname,
    boolean_t checkunusable)
{
	libzfs_handle_t *zhdl = fmd_hdl_getspecific(hdl);
	nvlist_t *detector, *fault;
	boolean_t serialize;
	nvlist_t *fmri, *fru;
	topo_hdl_t *thp;
	int err;

	/*
	 * Construct the detector from the case data.  The detector is in the
	 * ZFS scheme, and is either the pool or the vdev, depending on whether
	 * this is a vdev or pool fault.
	 */
	detector = fmd_nvl_alloc(hdl, FMD_SLEEP);

	(void) nvlist_add_uint8(detector, FM_VERSION, ZFS_SCHEME_VERSION0);
	(void) nvlist_add_string(detector, FM_FMRI_SCHEME, FM_FMRI_SCHEME_ZFS);
	(void) nvlist_add_uint64(detector, FM_FMRI_ZFS_POOL,
	    zcp->zc_data.zc_pool_guid);
	if (zcp->zc_data.zc_vdev_guid != 0) {
		(void) nvlist_add_uint64(detector, FM_FMRI_ZFS_VDEV,
		    zcp->zc_data.zc_vdev_guid);
	}

	/*
	 * We also want to make sure that the detector (pool or vdev) properly
	 * reflects the diagnosed state, when the fault corresponds to internal
	 * ZFS state (i.e. not checksum or I/O error-induced).  Otherwise, a
	 * device which was unavailable early in boot (because the driver/file
	 * wasn't available) and is now healthy will be mis-diagnosed.
	 */
	if (fmd_nvl_fmri_presence_state(hdl, detector) ==
	    FMD_OBJ_STATE_NOT_PRESENT ||
	    (checkunusable && fmd_nvl_fmri_service_state(hdl, detector) !=
	    FMD_SERVICE_STATE_UNUSABLE)) {
		fmd_case_close(hdl, zcp->zc_case);
		nvlist_free(detector);
		return;
	}


	/*
	 * If the disk is part of the system chassis, but the
	 * FRU indicates a different chassis ID than our
	 * current system, then ignore the error.  This
	 * indicates that the device was part of another
	 * cluster head, and for obvious reasons cannot be
	 * imported on this system.
	 */
	if (zcp->zc_fru != NULL && libzfs_fru_notself(zhdl, zcp->zc_fru)) {
		fmd_case_close(hdl, zcp->zc_case);
		nvlist_free(detector);
		return;
	}

	/*
	 * Convert zc_fru string back to nvlist.
	 */
	fru = NULL;
	if (zcp->zc_fru != NULL &&
	    (thp = fmd_hdl_topo_hold(hdl, TOPO_VERSION)) != NULL) {
		if (topo_fmri_str2nvl(thp, zcp->zc_fru, &fmri, &err) == 0) {
			fru = fmd_nvl_dup(hdl, fmri, FMD_SLEEP);
			nvlist_free(fmri);
		}

		fmd_hdl_topo_rele(hdl, thp);
	}

	/*
	 * If FRU is already faulty with SMART or device-as-detector fault
	 * then ignore this zfs fault.
	 */
	if (fru != NULL &&
	    (fmd_nvl_fmri_has_fault(hdl, fru, FMD_HAS_FAULT_FRU,
	    "fault.io.scsi.cmd.disk.dev.rqs.derr") != 0 ||
	    fmd_nvl_fmri_has_fault(hdl, fru, FMD_HAS_FAULT_FRU,
	    "fault.io.disk.self-test-failure") != 0 ||
	    fmd_nvl_fmri_has_fault(hdl, fru, FMD_HAS_FAULT_FRU,
	    "fault.io.disk.over-temperature") != 0 ||
	    fmd_nvl_fmri_has_fault(hdl, fru, FMD_HAS_FAULT_FRU,
	    "fault.io.disk.predictive-failure") != 0 ||
	    fmd_nvl_fmri_has_fault(hdl, fru, FMD_HAS_FAULT_FRU,
	    "fault.io.scsi.cmd.disk.dev.rqs.merr") != 0)) {
		fmd_case_close(hdl, zcp->zc_case);
		nvlist_free(fru);
		nvlist_free(detector);
		return;
	}

	fault = fmd_nvl_create_fault(hdl, faultname, 100, detector,
	    fru, detector);
	fmd_case_add_suspect(hdl, zcp->zc_case, fault);

	if (fru != NULL)
		nvlist_free(fru);

	fmd_case_solve(hdl, zcp->zc_case);

	serialize = B_FALSE;
	if (zcp->zc_data.zc_has_remove_timer) {
		fmd_timer_remove(hdl, zcp->zc_remove_timer);
		zcp->zc_data.zc_has_remove_timer = 0;
		serialize = B_TRUE;
	}
	if (serialize)
		zfs_case_serialize(hdl, zcp);

	nvlist_free(detector);
}

/*
 * This #define and function access a private interface of the FMA
 * framework.  Ereports include a time-of-day upper bound.
 * We want to look at that so we can compare it to when pools get
 * loaded.
 */
#define	FMD_EVN_TOD	"__tod"

static boolean_t
timeval_earlier(er_timeval_t *a, er_timeval_t *b)
{
	return (a->ertv_sec < b->ertv_sec ||
	    (a->ertv_sec == b->ertv_sec && a->ertv_nsec < b->ertv_nsec));
}

/*ARGSUSED*/
static void
zfs_ereport_when(fmd_hdl_t *hdl, nvlist_t *nvl, er_timeval_t *when)
{
	uint64_t *tod;
	uint_t	nelem;

	if (nvlist_lookup_uint64_array(nvl, FMD_EVN_TOD, &tod, &nelem) == 0 &&
	    nelem == 2) {
		when->ertv_sec = tod[0];
		when->ertv_nsec = tod[1];
	} else {
		when->ertv_sec = when->ertv_nsec = UINT64_MAX;
	}
}

typedef struct {
	char **frup;
	char *devid;
} zd_ff_t;

/*ARGSUSED*/
static int
zd_find_fru(topo_hdl_t *thp, tnode_t *node, void *arg)
{
	zd_ff_t *ffp = (zd_ff_t *)arg;
	char *nodename = topo_node_name(node);
	nvlist_t *asru, *fru;
	int err;
	char *scheme, *devid;

	if (strcmp(DISK, nodename) != 0)
		return (TOPO_WALK_NEXT);
	if (topo_node_asru(node, &asru, NULL, &err) != 0)
		return (TOPO_WALK_NEXT);
	if (nvlist_lookup_string(asru, FM_FMRI_SCHEME, &scheme) != 0) {
		nvlist_free(asru);
		return (TOPO_WALK_NEXT);
	}
	if (strcmp(scheme, FM_FMRI_SCHEME_DEV) != 0) {
		nvlist_free(asru);
		return (TOPO_WALK_NEXT);
	}
	if (nvlist_lookup_string(asru, FM_FMRI_DEV_ID, &devid) != 0) {
		nvlist_free(asru);
		return (TOPO_WALK_NEXT);
	}
	if (devid_str_compare(devid, ffp->devid) != 0) {
		nvlist_free(asru);
		return (TOPO_WALK_NEXT);
	}
	nvlist_free(asru);
	if (topo_node_fru(node, &fru, NULL, &err) != 0)
		return (TOPO_WALK_NEXT);
	(void) topo_fmri_nvl2str(thp, fru, ffp->frup, &err);
	nvlist_free(fru);
	return (TOPO_WALK_TERMINATE);
}

/*
 * Main fmd entry point.
 */
/*ARGSUSED*/
static void
zfs_fm_recv(fmd_hdl_t *hdl, fmd_event_t *ep, nvlist_t *nvl, const char *class)
{
	zfs_case_t *zcp, *dcp;
	int32_t pool_state;
	uint64_t ena, pool_guid, vdev_guid;
	er_timeval_t pool_load;
	er_timeval_t er_when;
	boolean_t pool_found = B_FALSE;
	boolean_t isresource;
	char *fru, *devid, *type;
	zd_ff_t ff;
	topo_walk_t *twp;
	int err;
	char *frustr = NULL;

	/*
	 * We subscribe to notifications for vdev or pool removal.  In these
	 * cases, there may be cases that no longer apply.  Purge any cases
	 * that no longer apply.
	 */
	if (fmd_nvl_class_match(hdl, nvl, "resource.sysevent.EC_zfs.*")) {
		zfs_purge_cases(hdl);
		zfs_stats.resource_drops.fmds_value.ui64++;
		return;
	}

	isresource = fmd_nvl_class_match(hdl, nvl, "resource.fs.zfs.*");

	if (isresource) {
		/*
		 * For resources, we don't have a normal payload.
		 */
		if (nvlist_lookup_uint64(nvl, FM_EREPORT_PAYLOAD_ZFS_VDEV_GUID,
		    &vdev_guid) != 0)
			pool_state = SPA_LOAD_OPEN;
		else
			pool_state = SPA_LOAD_NONE;
	} else {
		(void) nvlist_lookup_int32(nvl,
		    FM_EREPORT_PAYLOAD_ZFS_POOL_CONTEXT, &pool_state);
	}

	/*
	 * We also ignore all ereports generated during an import of a pool,
	 * since the only possible fault (.pool) would result in import failure,
	 * and hence no persistent fault.  Some day we may want to do something
	 * with these ereports, so we continue generating them internally.
	 */
	if (pool_state == SPA_LOAD_IMPORT) {
		zfs_stats.import_drops.fmds_value.ui64++;
		return;
	}

	/*
	 * Device I/O errors are ignored during pool open.
	 */
	if (pool_state == SPA_LOAD_OPEN &&
	    (fmd_nvl_class_match(hdl, nvl,
	    ZFS_MAKE_EREPORT(FM_EREPORT_ZFS_CHECKSUM)) ||
	    fmd_nvl_class_match(hdl, nvl,
	    ZFS_MAKE_EREPORT(FM_EREPORT_ZFS_IO)) ||
	    fmd_nvl_class_match(hdl, nvl,
	    ZFS_MAKE_EREPORT(FM_EREPORT_ZFS_PROBE_FAILURE)))) {
		zfs_stats.dev_drops.fmds_value.ui64++;
		return;
	}

	/*
	 * We ignore ereports for anything except disks and files.
	 */
	if (nvlist_lookup_string(nvl, FM_EREPORT_PAYLOAD_ZFS_VDEV_TYPE,
	    &type) == 0) {
		if (strcmp(type, VDEV_TYPE_DISK) != 0 &&
		    strcmp(type, VDEV_TYPE_FILE) != 0) {
			zfs_stats.vdev_drops.fmds_value.ui64++;
			return;
		}
	}
	/*
	 * If this is an ereport for a case with an associated vdev DEVID,
	 * find the associated FRU from the latest topology.
	 */
	if (nvlist_lookup_string(nvl, FM_EREPORT_PAYLOAD_ZFS_VDEV_DEVID,
	    &devid) == 0) {
		topo_hdl_t *thp = fmd_hdl_topo_hold(hdl, TOPO_VERSION);

		fru = NULL;
		ff.devid = devid;
		ff.frup = &fru;
		if ((twp = topo_walk_init(thp, FM_FMRI_SCHEME_HC, zd_find_fru,
		    &ff, &err)) != NULL) {
			(void) topo_walk_step(twp, TOPO_WALK_CHILD);
			topo_walk_fini(twp);
		}
		if (fru != NULL) {
			frustr = fmd_hdl_strdup(hdl, fru, FMD_SLEEP);
			topo_hdl_strfree(thp, fru);
			/*
			 * We ignore checksum ereports with a DEVID payload,
			 * and fru exists in the topology. This will be handled
			 * by eversholt rules.
			 */
			if (fmd_nvl_class_match(hdl, nvl,
			    ZFS_MAKE_EREPORT(FM_EREPORT_ZFS_CHECKSUM))) {
				zfs_stats.cksum_drops.fmds_value.ui64++;
				fmd_hdl_strfree(hdl, frustr);
				fmd_hdl_topo_rele(hdl, thp);
				return;
			}
		}
		fmd_hdl_topo_rele(hdl, thp);
	}

	/*
	 * Determine if this ereport corresponds to an open case.  Previous
	 * incarnations of this DE used the ENA to chain events together as
	 * part of the same case.  The problem with this is that we rely on
	 * global uniqueness of cases based on (pool_guid, vdev_guid) pair when
	 * generating SERD engines.  Instead, we have a case for each vdev or
	 * pool, regardless of the ENA.
	 */
	(void) nvlist_lookup_uint64(nvl,
	    FM_EREPORT_PAYLOAD_ZFS_POOL_GUID, &pool_guid);
	if (nvlist_lookup_uint64(nvl,
	    FM_EREPORT_PAYLOAD_ZFS_VDEV_GUID, &vdev_guid) != 0)
		vdev_guid = 0;
	if (nvlist_lookup_uint64(nvl, FM_EREPORT_ENA, &ena) != 0)
		ena = 0;

	zfs_ereport_when(hdl, nvl, &er_when);

	for (zcp = uu_list_first(zfs_cases); zcp != NULL;
	    zcp = uu_list_next(zfs_cases, zcp)) {
		if (zcp->zc_data.zc_pool_guid == pool_guid) {
			pool_found = B_TRUE;
			pool_load = zcp->zc_when;
		}
		if (zcp->zc_data.zc_vdev_guid == vdev_guid)
			break;
	}

	if (pool_found) {
		fmd_hdl_debug(hdl, "pool %llx, "
		    "ereport time %lld.%lld, pool load time = %lld.%lld\n",
		    pool_guid, er_when.ertv_sec, er_when.ertv_nsec,
		    pool_load.ertv_sec, pool_load.ertv_nsec);
	}

	/*
	 * Avoid falsely accusing a pool of being faulty.  Do so by
	 * not replaying ereports that were generated prior to the
	 * current import.  If the failure that generated them was
	 * transient because the device was actually removed but we
	 * didn't receive the normal asynchronous notification, we
	 * don't want to mark it as faulted and potentially panic. If
	 * there is still a problem we'd expect not to be able to
	 * import the pool, or that new ereports will be generated
	 * once the pool is used.
	 */
	if (pool_found && timeval_earlier(&er_when, &pool_load)) {
		zfs_stats.old_drops.fmds_value.ui64++;
		return;
	}

	if (!pool_found) {
		/*
		 * Haven't yet seen this pool, but same situation
		 * may apply.
		 */
		libzfs_handle_t *zhdl = fmd_hdl_getspecific(hdl);
		struct load_time_arg la;

		la.lt_guid = pool_guid;
		la.lt_time = &pool_load;
		la.lt_found = B_FALSE;

		if (zhdl != NULL &&
		    zpool_iter(zhdl, zpool_find_load_time, &la) == 0 &&
		    la.lt_found == B_TRUE) {
			pool_found = B_TRUE;
			fmd_hdl_debug(hdl, "pool %llx, "
			    "ereport time %lld.%lld, "
			    "pool load time = %lld.%lld\n",
			    pool_guid, er_when.ertv_sec, er_when.ertv_nsec,
			    pool_load.ertv_sec, pool_load.ertv_nsec);
			if (timeval_earlier(&er_when, &pool_load)) {
				zfs_stats.old_drops.fmds_value.ui64++;
				return;
			}
		}
	}

	if (zcp == NULL) {
		fmd_case_t *cs;
		zfs_case_data_t data = { 0 };

		/*
		 * If this is one of our 'fake' resource ereports, and there is
		 * no case open, simply discard it.
		 */
		if (isresource) {
			zfs_stats.resource_drops.fmds_value.ui64++;
			return;
		}

		/*
		 * Open a new case.
		 */
		cs = fmd_case_open(hdl, NULL);

		/*
		 * Initialize the case buffer.  To commonize code, we actually
		 * create the buffer with existing data, and then call
		 * zfs_case_unserialize() to instantiate the in-core structure.
		 */
		fmd_buf_create(hdl, cs, CASE_DATA,
		    sizeof (zfs_case_data_t));

		data.zc_version = CASE_DATA_VERSION_SERD;
		data.zc_ena = ena;
		data.zc_pool_guid = pool_guid;
		data.zc_vdev_guid = vdev_guid;
		data.zc_pool_state = (int)pool_state;

		fmd_buf_write(hdl, cs, CASE_DATA, &data, sizeof (data));

		zcp = zfs_case_unserialize(hdl, cs);
		assert(zcp != NULL);
		if (pool_found)
			zcp->zc_when = pool_load;
	}

	if (frustr == NULL && nvlist_lookup_string(nvl,
	    FM_EREPORT_PAYLOAD_ZFS_VDEV_FRU, &frustr) == 0)
		frustr = fmd_hdl_strdup(hdl, frustr, FMD_SLEEP);

	if (frustr != NULL) {
		if (zcp->zc_fru == NULL) {
			zcp->zc_fru = fmd_hdl_strdup(hdl, frustr,
			    FMD_SLEEP);
			zfs_case_serialize(hdl, zcp);
		}
		fmd_hdl_strfree(hdl, frustr);
	}

	if (isresource) {
		if (fmd_nvl_class_match(hdl, nvl,
		    ZFS_MAKE_RSRC(FM_RESOURCE_AUTOREPLACE))) {
			/*
			 * The 'resource.fs.zfs.autoreplace' event indicates
			 * that the pool was loaded with the 'autoreplace'
			 * property set.  In this case, any pending device
			 * failures should be ignored, as the asynchronous
			 * autoreplace handling will take care of them.
			 */
			fmd_case_close(hdl, zcp->zc_case);
		} else if (fmd_nvl_class_match(hdl, nvl,
		    ZFS_MAKE_RSRC(FM_RESOURCE_REMOVED))) {
			/*
			 * The 'resource.fs.zfs.removed' event indicates that
			 * device removal was detected, and the device was
			 * closed asynchronously.  If this is the case, we
			 * assume that any recent I/O errors were due to the
			 * device removal, not any fault of the device itself.
			 * We reset the SERD engine, and cancel any pending
			 * timers.
			 */
			if (zcp->zc_data.zc_has_remove_timer) {
				fmd_timer_remove(hdl, zcp->zc_remove_timer);
				zcp->zc_data.zc_has_remove_timer = 0;
				zfs_case_serialize(hdl, zcp);
			}
			if (zcp->zc_data.zc_serd_io[0] != '\0')
				fmd_serd_reset(hdl,
				    zcp->zc_data.zc_serd_io);
			if (zcp->zc_data.zc_serd_checksum[0] != '\0')
				fmd_serd_reset(hdl,
				    zcp->zc_data.zc_serd_checksum);
		}
		zfs_stats.resource_drops.fmds_value.ui64++;
		return;
	}

	/*
	 * Associate the ereport with this case.
	 */
	fmd_case_add_ereport(hdl, zcp->zc_case, ep);

	/*
	 * Don't do anything else if this case is already solved.
	 */
	if (fmd_case_solved(hdl, zcp->zc_case))
		return;

	/*
	 * Determine if we should solve the case and generate a fault.  We solve
	 * a case if:
	 *
	 * 	a. A pool failed to open (ereport.fs.zfs.pool)
	 * 	b. A device failed to open (ereport.fs.zfs.pool) while a pool
	 *	   was up and running.
	 *
	 * We may see a series of ereports associated with a pool open, all
	 * chained together by the same ENA.  If the pool open succeeds, then
	 * we'll see no further ereports.  To detect when a pool open has
	 * succeeded, we associate a timer with the event.  When it expires, we
	 * close the case.
	 */
	if (fmd_nvl_class_match(hdl, nvl,
	    ZFS_MAKE_EREPORT(FM_EREPORT_ZFS_POOL))) {
		/*
		 * Pool level fault.  Before solving the case, go through and
		 * close any open device cases that may be pending.
		 */
		for (dcp = uu_list_first(zfs_cases); dcp != NULL;
		    dcp = uu_list_next(zfs_cases, dcp)) {
			if (dcp->zc_data.zc_pool_guid ==
			    zcp->zc_data.zc_pool_guid &&
			    dcp->zc_data.zc_vdev_guid != 0)
				fmd_case_close(hdl, dcp->zc_case);
		}

		zfs_case_solve(hdl, zcp, "fault.fs.zfs.pool", B_TRUE);
	} else if (fmd_nvl_class_match(hdl, nvl,
	    ZFS_MAKE_EREPORT(FM_EREPORT_ZFS_LOG_REPLAY))) {
		/*
		 * Pool level fault for reading the intent logs.
		 */
		zfs_case_solve(hdl, zcp, "fault.fs.zfs.log_replay", B_TRUE);
	} else if (fmd_nvl_class_match(hdl, nvl, "ereport.fs.zfs.vdev.*")) {
		/*
		 * Device fault.
		 */
		zfs_case_solve(hdl, zcp, "fault.fs.zfs.device",  B_TRUE);
	} else if (fmd_nvl_class_match(hdl, nvl,
	    ZFS_MAKE_EREPORT(FM_EREPORT_ZFS_IO)) ||
	    fmd_nvl_class_match(hdl, nvl,
	    ZFS_MAKE_EREPORT(FM_EREPORT_ZFS_CHECKSUM)) ||
	    fmd_nvl_class_match(hdl, nvl,
	    ZFS_MAKE_EREPORT(FM_EREPORT_ZFS_IO_FAILURE)) ||
	    fmd_nvl_class_match(hdl, nvl,
	    ZFS_MAKE_EREPORT(FM_EREPORT_ZFS_PROBE_FAILURE))) {
		char *failmode = NULL;
		boolean_t checkremove = B_FALSE;

		/*
		 * If this is a checksum or I/O error, then toss it into the
		 * appropriate SERD engine and check to see if it has fired.
		 * Ideally, we want to do something more sophisticated,
		 * (persistent errors for a single data block, etc).  For now,
		 * a single SERD engine is sufficient.
		 */
		if (fmd_nvl_class_match(hdl, nvl,
		    ZFS_MAKE_EREPORT(FM_EREPORT_ZFS_IO))) {
			if (zcp->zc_data.zc_serd_io[0] == '\0') {
				zfs_serd_name(zcp->zc_data.zc_serd_io,
				    pool_guid, vdev_guid, "io");
				fmd_serd_create(hdl, zcp->zc_data.zc_serd_io,
				    fmd_prop_get_int32(hdl, "io_N"),
				    fmd_prop_get_int64(hdl, "io_T"));
				zfs_case_serialize(hdl, zcp);
			}
			if (fmd_serd_record(hdl, zcp->zc_data.zc_serd_io, ep))
				checkremove = B_TRUE;
		} else if (fmd_nvl_class_match(hdl, nvl,
		    ZFS_MAKE_EREPORT(FM_EREPORT_ZFS_CHECKSUM))) {
			if (zcp->zc_data.zc_serd_checksum[0] == '\0') {
				zfs_serd_name(zcp->zc_data.zc_serd_checksum,
				    pool_guid, vdev_guid, "checksum");
				fmd_serd_create(hdl,
				    zcp->zc_data.zc_serd_checksum,
				    fmd_prop_get_int32(hdl, "checksum_N"),
				    fmd_prop_get_int64(hdl, "checksum_T"));
				zfs_case_serialize(hdl, zcp);
			}
			if (fmd_serd_record(hdl,
			    zcp->zc_data.zc_serd_checksum, ep)) {
				zfs_case_solve(hdl, zcp,
				    "fault.fs.zfs.vdev.checksum", B_FALSE);
			}
		} else if (fmd_nvl_class_match(hdl, nvl,
		    ZFS_MAKE_EREPORT(FM_EREPORT_ZFS_IO_FAILURE)) &&
		    (nvlist_lookup_string(nvl,
		    FM_EREPORT_PAYLOAD_ZFS_POOL_FAILMODE, &failmode) == 0) &&
		    failmode != NULL) {
			if (strncmp(failmode, FM_EREPORT_FAILMODE_CONTINUE,
			    strlen(FM_EREPORT_FAILMODE_CONTINUE)) == 0) {
				zfs_case_solve(hdl, zcp,
				    "fault.fs.zfs.io_failure_continue",
				    B_FALSE);
			} else if (strncmp(failmode, FM_EREPORT_FAILMODE_WAIT,
			    strlen(FM_EREPORT_FAILMODE_WAIT)) == 0) {
				zfs_case_solve(hdl, zcp,
				    "fault.fs.zfs.io_failure_wait", B_FALSE);
			}
		} else if (fmd_nvl_class_match(hdl, nvl,
		    ZFS_MAKE_EREPORT(FM_EREPORT_ZFS_PROBE_FAILURE))) {
			checkremove = B_TRUE;
		}

		/*
		 * Because I/O errors may be due to device removal, we postpone
		 * any diagnosis until we're sure that we aren't about to
		 * receive a 'resource.fs.zfs.removed' event.
		 */
		if (checkremove) {
			if (zcp->zc_data.zc_has_remove_timer)
				fmd_timer_remove(hdl, zcp->zc_remove_timer);
			zcp->zc_remove_timer = fmd_timer_install(hdl, zcp, NULL,
			    zfs_remove_timeout);
			if (!zcp->zc_data.zc_has_remove_timer) {
				zcp->zc_data.zc_has_remove_timer = 1;
				zfs_case_serialize(hdl, zcp);
			}
		}
	}
}

/*
 * The timeout is fired when we diagnosed an I/O error, and it was not due to
 * device removal (which would cause the timeout to be cancelled).
 */
/* ARGSUSED */
static void
zfs_fm_timeout(fmd_hdl_t *hdl, id_t id, void *data)
{
	zfs_case_t *zcp = data;

	if (id == zcp->zc_remove_timer)
		zfs_case_solve(hdl, zcp, "fault.fs.zfs.vdev.io", B_FALSE);
}

static void
zfs_fm_close(fmd_hdl_t *hdl, fmd_case_t *cs)
{
	zfs_case_t *zcp = fmd_case_getspecific(hdl, cs);

	if (zcp->zc_data.zc_serd_checksum[0] != '\0')
		fmd_serd_destroy(hdl, zcp->zc_data.zc_serd_checksum);
	if (zcp->zc_data.zc_serd_io[0] != '\0')
		fmd_serd_destroy(hdl, zcp->zc_data.zc_serd_io);
	if (zcp->zc_data.zc_has_remove_timer)
		fmd_timer_remove(hdl, zcp->zc_remove_timer);
	uu_list_remove(zfs_cases, zcp);
	if (zcp->zc_fru != NULL) {
		fmd_hdl_strfree(hdl, zcp->zc_fru);
		fmd_buf_destroy(hdl, zcp->zc_case, CASE_FRU);
	}
	fmd_hdl_free(hdl, zcp, sizeof (zfs_case_t));
}

/*
 * We use the fmd gc entry point to look for old cases that no longer apply.
 * This allows us to keep our set of case data small in a long running system.
 */
static void
zfs_fm_gc(fmd_hdl_t *hdl)
{
	zfs_purge_cases(hdl);
}

static const fmd_hdl_ops_t fmd_ops = {
	zfs_fm_recv,	/* fmdo_recv */
	zfs_fm_timeout,	/* fmdo_timeout */
	zfs_fm_close,	/* fmdo_close */
	NULL,		/* fmdo_stats */
	zfs_fm_gc,	/* fmdo_gc */
};

static const fmd_prop_t fmd_props[] = {
	{ "checksum_N", FMD_TYPE_UINT32, "10" },
	{ "checksum_T", FMD_TYPE_TIME, "10min" },
	{ "io_N", FMD_TYPE_UINT32, "10" },
	{ "io_T", FMD_TYPE_TIME, "10min" },
	{ "remove_timeout", FMD_TYPE_TIME, "15sec" },
	{ NULL, 0, NULL }
};

static const fmd_hdl_info_t fmd_info = {
	"ZFS Diagnosis Engine", "1.0", &fmd_ops, fmd_props
};

void
_fmd_init(fmd_hdl_t *hdl)
{
	fmd_case_t *cp;
	libzfs_handle_t *zhdl;

	if ((zhdl = libzfs_init()) == NULL)
		return;

	if ((zfs_case_pool = uu_list_pool_create("zfs_case_pool",
	    sizeof (zfs_case_t), offsetof(zfs_case_t, zc_node),
	    NULL, 0)) == NULL) {
		libzfs_fini(zhdl);
		return;
	}

	if ((zfs_cases = uu_list_create(zfs_case_pool, NULL, 0)) == NULL) {
		uu_list_pool_destroy(zfs_case_pool);
		libzfs_fini(zhdl);
		return;
	}

	if (fmd_hdl_register(hdl, FMD_API_VERSION, &fmd_info) != 0) {
		uu_list_destroy(zfs_cases);
		uu_list_pool_destroy(zfs_case_pool);
		libzfs_fini(zhdl);
		return;
	}

	fmd_hdl_setspecific(hdl, zhdl);

	(void) fmd_stat_create(hdl, FMD_STAT_NOALLOC, sizeof (zfs_stats) /
	    sizeof (fmd_stat_t), (fmd_stat_t *)&zfs_stats);

	/*
	 * Iterate over all active cases and unserialize the associated buffers,
	 * adding them to our list of open cases.
	 */
	for (cp = fmd_case_next(hdl, NULL);
	    cp != NULL; cp = fmd_case_next(hdl, cp))
		(void) zfs_case_unserialize(hdl, cp);

	/*
	 * Clear out any old cases that are no longer valid.
	 */
	zfs_purge_cases(hdl);

	zfs_remove_timeout = fmd_prop_get_int64(hdl, "remove_timeout");
}

void
_fmd_fini(fmd_hdl_t *hdl)
{
	zfs_case_t *zcp;
	uu_list_walk_t *walk;
	libzfs_handle_t *zhdl;

	/*
	 * Remove all active cases.
	 */
	walk = uu_list_walk_start(zfs_cases, UU_WALK_ROBUST);
	while ((zcp = uu_list_walk_next(walk)) != NULL) {
		uu_list_remove(zfs_cases, zcp);
		fmd_hdl_free(hdl, zcp, sizeof (zfs_case_t));
	}
	uu_list_walk_end(walk);

	uu_list_destroy(zfs_cases);
	uu_list_pool_destroy(zfs_case_pool);

	zhdl = fmd_hdl_getspecific(hdl);
	libzfs_fini(zhdl);
}
