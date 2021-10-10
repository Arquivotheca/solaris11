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

/*
 * The ZFS retire agent is responsible for managing hot spares across all pools.
 * When we see a device fault or a device removal, we try to open the associated
 * pool and look for any hot spares.  We iterate over any available hot spares
 * and attempt a 'zpool replace' for each one.
 *
 * For vdevs diagnosed as faulty, the agent is also responsible for proactively
 * marking the vdev FAULTY (for I/O errors) or DEGRADED (for checksum errors).
 */

#include <fm/fmd_api.h>
#include <sys/fs/zfs.h>
#include <sys/fm/protocol.h>
#include <sys/fm/fs/zfs.h>
#include <sys/libdevid.h>
#include <libzfs.h>
#include <fm/libtopo.h>
#include <fm/topo_hc.h>
#include <string.h>

typedef struct zfs_retire_data {
	libzfs_handle_t			*zrd_hdl;
} zfs_retire_data_t;

/*
 * Find a pool with a matching GUID.
 */
typedef struct find_cbdata {
	uint64_t	cb_guid;
	zpool_handle_t	*cb_zhp;
	nvlist_t	*cb_vdev;
} find_cbdata_t;

static int
find_pool(zpool_handle_t *zhp, void *data)
{
	find_cbdata_t *cbp = data;

	if (cbp->cb_guid ==
	    zpool_get_prop_int(zhp, ZPOOL_PROP_GUID, NULL)) {
		cbp->cb_zhp = zhp;
		return (1);
	}

	zpool_close(zhp);
	return (0);
}

/*
 * Find a vdev within a tree with a matching GUID.
 */
static nvlist_t *
find_vdev(libzfs_handle_t *zhdl, nvlist_t *nv, uint64_t search_guid)
{
	uint64_t guid;
	nvlist_t **child;
	uint_t c, children;
	nvlist_t *ret;

	if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_GUID, &guid) == 0 &&
	    guid == search_guid)
		return (nv);

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0)
		return (NULL);

	for (c = 0; c < children; c++) {
		if ((ret = find_vdev(zhdl, child[c], search_guid)) != NULL)
			return (ret);
	}

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_L2CACHE,
	    &child, &children) != 0)
		return (NULL);

	for (c = 0; c < children; c++) {
		if ((ret = find_vdev(zhdl, child[c], search_guid)) != NULL)
			return (ret);
	}

	return (NULL);
}

/*
 * Given a (pool, vdev) GUID pair, find the matching pool and vdev.
 */
static zpool_handle_t *
find_by_guid(libzfs_handle_t *zhdl, uint64_t pool_guid, uint64_t vdev_guid,
    nvlist_t **vdevp)
{
	find_cbdata_t cb;
	zpool_handle_t *zhp;
	nvlist_t *config, *nvroot;

	/*
	 * Find the corresponding pool and make sure the vdev still exists.
	 */
	cb.cb_guid = pool_guid;
	if (zpool_iter(zhdl, find_pool, &cb) != 1)
		return (NULL);

	zhp = cb.cb_zhp;
	config = zpool_get_config(zhp, NULL);
	if (nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvroot) != 0) {
		zpool_close(zhp);
		return (NULL);
	}

	if (vdev_guid != 0) {
		if ((*vdevp = find_vdev(zhdl, nvroot, vdev_guid)) == NULL) {
			zpool_close(zhp);
			return (NULL);
		}
	}

	return (zhp);
}

#define	ZR_NOT_FOUND	0	/* keep looking */
#define	ZR_VDEV_FOUND	1	/* we found the vdev in the tree */
#define	ZR_SPARE_PARENT	2	/* the vdev is a child of a spare */

/*
 * This returns true if the vdev in question has a spare vdev as an
 * ancestor. As there is no way to work "upwards" through an nvlist,
 * we must recurse downwards to determine if the child of any spare
 * contains the specified vdev.
 */
static int
vdev_spare_parent(zpool_handle_t *zhp, char *vdev, nvlist_t *tree)
{
	nvlist_t **child;
	uint_t children, c;

	if (nvlist_lookup_nvlist_array(tree, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0) {
		char *devname =
		    zpool_vdev_name(NULL, zhp, tree, B_FALSE, B_FALSE);
		boolean_t match = (strcmp(devname, vdev) == 0);

		free(devname);

		/*
		 * If the vdev names match, we found it. If we return
		 * ZR_VDEV_FOUND, we'll unwind the stack.
		 */
		return (match ? ZR_VDEV_FOUND : ZR_NOT_FOUND);
	}

	/*
	 * This is an inner vdev, not a leaf vdev. We need to search
	 * all of its children for the leaf we're interested in.
	 */
	for (c = 0; c < children; c++) {
		char *type;
		int result;

		/* ignore config errors */
		if (nvlist_lookup_string(child[c], ZPOOL_CONFIG_TYPE,
		    &type) != 0)
			continue;

		/* see if the disk vdev is a child of this vdev */
		result = vdev_spare_parent(zhp, vdev, child[c]);

		/*
		 * If we found the vdev, then determine what to return. If
		 * we're a spare, then the vdev is a child of a spare.
		 * Otherwise, return the result we got from searching below us.
		 */
		if (result != ZR_NOT_FOUND)
			return (strcmp(type, VDEV_TYPE_SPARE) == 0 ?
			    ZR_SPARE_PARENT : result);
	}

	/* The vdev couldn't be found at this level of the tree */
	return (ZR_NOT_FOUND);
}

/*
 * Given a vdev, attempt to replace it with every known spare until one
 * succeeds.
 */
static void
replace_with_spare(fmd_hdl_t *hdl, zpool_handle_t *zhp, nvlist_t *vdev)
{
	nvlist_t *config, *nvroot, *replacement;
	nvlist_t **spares;
	uint_t s, nspares;
	char *dev_name;

	config = zpool_get_config(zhp, NULL);
	if (nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvroot) != 0)
		return;

	/*
	 * Find out if there are any hot spares available in the pool.
	 */
	if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_SPARES,
	    &spares, &nspares) != 0)
		return;

	dev_name = zpool_vdev_name(NULL, zhp, vdev, B_FALSE, B_FALSE);

	/*
	 * If this device already has a spare as a parent, we don't
	 * want to make matters worse by attaching a second spare.
	 *
	 */
	if (vdev_spare_parent(zhp, dev_name, nvroot) == ZR_SPARE_PARENT) {
		free(dev_name);
		return;
	}

	replacement = fmd_nvl_alloc(hdl, FMD_SLEEP);

	(void) nvlist_add_string(replacement, ZPOOL_CONFIG_TYPE,
	    VDEV_TYPE_ROOT);

	/*
	 * Try to replace each spare, ending when we successfully
	 * replace it.
	 */
	for (s = 0; s < nspares; s++) {
		char *spare_name;

		if (nvlist_lookup_string(spares[s], ZPOOL_CONFIG_PATH,
		    &spare_name) != 0)
			continue;

		(void) nvlist_add_nvlist_array(replacement,
		    ZPOOL_CONFIG_CHILDREN, &spares[s], 1);

		if (zpool_vdev_attach(zhp, dev_name, spare_name,
		    replacement, B_TRUE) == 0)
			break;
	}

	free(dev_name);
	nvlist_free(replacement);
}

typedef struct {
	nvlist_t **frup;
	char *devid;
} zr_ff_t;

/*ARGSUSED*/
static int
zr_find_fru(topo_hdl_t *thp, tnode_t *node, void *arg)
{
	zr_ff_t *ffp = (zr_ff_t *)arg;
	char *nodename = topo_node_name(node);
	nvlist_t *asru;
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
	(void) topo_node_fru(node, ffp->frup, NULL, &err);
	return (TOPO_WALK_TERMINATE);
}

void
zfs_vdev_check_resolved(fmd_hdl_t *hdl, nvlist_t *nvl)
{
	zfs_retire_data_t *zdp = fmd_hdl_getspecific(hdl);
	uint64_t pool_guid, vdev_guid;
	nvlist_t *asru, *fru = NULL;
	libzfs_handle_t *zhdl = zdp->zrd_hdl;
	zpool_handle_t *zhp;
	nvlist_t *vdev = NULL;

	if (nvlist_lookup_uint64(nvl, FM_EREPORT_PAYLOAD_ZFS_POOL_GUID,
	    &pool_guid) != 0 || nvlist_lookup_uint64(nvl,
	    FM_EREPORT_PAYLOAD_ZFS_VDEV_GUID, &vdev_guid) != 0)
		return;

	asru = fmd_nvl_alloc(hdl, FMD_SLEEP);

	(void) nvlist_add_uint8(asru, FM_VERSION, ZFS_SCHEME_VERSION0);
	(void) nvlist_add_string(asru, FM_FMRI_SCHEME, FM_FMRI_SCHEME_ZFS);
	(void) nvlist_add_uint64(asru, FM_FMRI_ZFS_POOL, pool_guid);
	(void) nvlist_add_uint64(asru, FM_FMRI_ZFS_VDEV, vdev_guid);

	if ((zhp = find_by_guid(zhdl, pool_guid, vdev_guid, &vdev)) != NULL) {
		if (vdev != NULL) {
			char *devid;

			/*
			 * If we have a vdev, lookup the devid, and from that
			 * find the associated fru in the topology. We can then
			 * do a resolve on that rather than just on the asru,
			 * so this will work for io faults as well as zfs ones.
			 */
			if (nvlist_lookup_string(vdev, ZPOOL_CONFIG_DEVID,
			    &devid) == 0) {
				topo_hdl_t *thp = fmd_hdl_topo_hold(hdl,
				    TOPO_VERSION);
				zr_ff_t ff;
				topo_walk_t *twp;
				int err;

				fru = NULL;
				ff.devid = devid;
				ff.frup = &fru;
				if ((twp = topo_walk_init(thp,
				    FM_FMRI_SCHEME_HC, zr_find_fru,
				    &ff, &err)) != NULL) {
					(void) topo_walk_step(twp,
					    TOPO_WALK_CHILD);
					topo_walk_fini(twp);
				}
				fmd_hdl_topo_rele(hdl, thp);
			}
		}
		zpool_close(zhp);
	}

	/*
	 * Let fmd check if any cases involving the fru/asru are now resolved.
	 */
	if (fru != NULL)
		fmd_resolved_fru(hdl, fru);
	else
		fmd_resolved_asru(hdl, asru);
	nvlist_free(asru);
	if (fru != NULL)
		nvlist_free(fru);
}

/*ARGSUSED*/
static void
zfs_retire_recv(fmd_hdl_t *hdl, fmd_event_t *ep, nvlist_t *nvl,
    const char *class)
{
	uint64_t pool_guid, vdev_guid;
	zpool_handle_t *zhp;
	nvlist_t *fault, *asru, *fru = NULL;
	nvlist_t **faults;
	uint8_t *status, faulty_status = FM_SUSPECT_FAULTY;
	uint_t f, nfaults, nstatus;
	zfs_retire_data_t *zdp = fmd_hdl_getspecific(hdl);
	libzfs_handle_t *zhdl = zdp->zrd_hdl;
	boolean_t fault_device, degrade_device;
	boolean_t is_repair;
	char *scheme;
	nvlist_t *vdev;
	boolean_t retire;
	vdev_aux_t aux;

	/*
	 * If this is a resource notifying us of device removal, then simply
	 * check for an available spare and continue.
	 */
	if (strcmp(class, "resource.fs.zfs.removed") == 0) {
		if (nvlist_lookup_uint64(nvl, FM_EREPORT_PAYLOAD_ZFS_POOL_GUID,
		    &pool_guid) != 0 ||
		    nvlist_lookup_uint64(nvl, FM_EREPORT_PAYLOAD_ZFS_VDEV_GUID,
		    &vdev_guid) != 0)
			return;

		if ((zhp = find_by_guid(zhdl, pool_guid, vdev_guid,
		    &vdev)) == NULL)
			return;

		if (fmd_prop_get_int32(hdl, "spare_on_remove"))
			replace_with_spare(hdl, zhp, vdev);
		zpool_close(zhp);
		return;
	}

	if (strcmp(class, "resource.fs.zfs.statechange") == 0 ||
	    strcmp(class,
	    "resource.sysevent.EC_zfs.ESC_ZFS_vdev_remove") == 0) {
		/*
		 * Zfs vdev is healthy again or no longer exists (eg resilver
		 * to a replacement has completed and the original has been
		 * deconfigured). Notify fmd to resolve any
		 * repaired-but-not-resolved cases.
		 */
		zfs_vdev_check_resolved(hdl, nvl);
		return;
	}

	/*
	 * We subscribe to zfs and io faults as well as repair events.
	 */
	if (strcmp(class, FM_LIST_RESOLVED_CLASS) == 0 ||
	    strcmp(class, FM_LIST_ISOLATED_CLASS) == 0)
		return;
	else if (strcmp(class, FM_LIST_SUSPECT_CLASS) == 0 ||
	    strcmp(class, FM_LIST_REPAIRED_CLASS) == 0 ||
	    strcmp(class, FM_LIST_UPDATED_CLASS) == 0) {
		if (nvlist_lookup_nvlist_array(nvl, FM_SUSPECT_FAULT_LIST,
		    &faults, &nfaults) != 0)
			return;
		if (nvlist_lookup_uint8_array(nvl, FM_SUSPECT_FAULT_STATUS,
		    &status, &nstatus) != 0)
			return;
	} else {
		/*
		 * could be a single fault from fmd startup.
		 */
		nfaults = nstatus = 1;
		faults = &nvl;
		status = &faulty_status;
	}

	for (f = 0; f < nfaults; f++) {
		fault = faults[f];

		fault_device = B_FALSE;
		degrade_device = B_FALSE;

		if (nvlist_lookup_boolean_value(fault, FM_SUSPECT_RETIRE,
		    &retire) == 0 && retire == 0)
			continue;

		/*
		 * While we subscribe to fault.fs.zfs.*, we only take action
		 * for faults targeting a specific vdev (open failure or SERD
		 * failure).
		 */
		if (fmd_nvl_class_match(hdl, fault, "fault.fs.zfs.vdev.io")) {
			fault_device = B_TRUE;
		} else if (fmd_nvl_class_match(hdl, fault,
		    "fault.fs.zfs.vdev.checksum")) {
			degrade_device = B_TRUE;
		} else if (fmd_nvl_class_match(hdl, fault,
		    "fault.fs.zfs.device")) {
			fault_device = B_FALSE;
		} else {
			continue;
		}

		if (nvlist_lookup_nvlist(fault, FM_FAULT_ASRU, &asru) != 0 ||
		    nvlist_lookup_string(asru, FM_FMRI_SCHEME, &scheme) != 0 ||
		    strcmp(scheme, FM_FMRI_SCHEME_ZFS) != 0 ||
		    nvlist_lookup_uint64(asru, FM_FMRI_ZFS_POOL,
		    &pool_guid) != 0)
			continue;

		/*
		 * Use has_fault here rather than just checking the status of
		 * the suspect in this suspect list. This is because there
		 * could be multiple faults afflicting this FRU/ASRU and we
		 * don't want to bring it back online until they are all
		 * repaired.
		 */
		is_repair = !fmd_nvl_fmri_has_fault(hdl, asru,
		    FMD_HAS_FAULT_ASRU | FMD_HAS_FAULT_RETIRE, NULL);
		if (nvlist_lookup_uint64(asru, FM_FMRI_ZFS_VDEV,
		    &vdev_guid) != 0) {
			/*
			 * For pool-level repair events, clear the entire pool.
			 */
			if (is_repair) {
				if ((zhp = find_by_guid(zhdl, pool_guid, 0,
				    &vdev)) != NULL) {
					nvlist_t *policy;

					if (nvlist_alloc(&policy,
					    NV_UNIQUE_NAME, 0) == 0 &&
					    nvlist_add_uint32(policy,
					    ZPOOL_LOAD_REWIND,
					    ZPOOL_NEVER_REWIND) == 0)
						(void) zpool_clear(zhp, NULL,
						    policy);
					zpool_close(zhp);
				}
				fmd_resolved_asru(hdl, asru);
			}
			continue;
		}

		if ((zhp = find_by_guid(zhdl, pool_guid, vdev_guid,
		    &vdev)) == NULL) {
			/*
			 * No longer a valid vdev. If this a repair event
			 * then it can be resolved.
			 */
			if (is_repair) {
				if (nvlist_lookup_nvlist(fault, FM_FAULT_FRU,
				    &fru) != 0)
					fmd_resolved_asru(hdl, asru);
				else
					fmd_resolved_fru(hdl, fru);
			}
			continue;
		}

		if (is_repair) {
			/*
			 * If we know the fru, then check that in addition to
			 * the asru as we don't want to clear the vdev if there
			 * are any io faults on the fru or if it is not present.
			 */
			if (nvlist_lookup_nvlist(fault, FM_FAULT_FRU,
			    &fru) != 0) {
				(void) zpool_vdev_clear(zhp, vdev_guid);
				fmd_resolved_asru(hdl, asru);
			} else if (!(status[f] & FM_SUSPECT_NOT_PRESENT) &&
			    !(status[f] & FM_SUSPECT_REPLACED) &&
			    !fmd_nvl_fmri_has_fault(hdl, fru,
			    FMD_HAS_FAULT_FRU | FMD_HAS_FAULT_RETIRE, NULL)) {
				(void) zpool_vdev_clear(zhp, vdev_guid);
				fmd_resolved_fru(hdl, fru);
			}
			zpool_close(zhp);
			continue;
		}

		/*
		 * Actively fault the device if needed.
		 */
		aux = VDEV_AUX_ERR_EXCEEDED;
		if (fault_device) {
			(void) zpool_vdev_fault(zhp, vdev_guid, aux);
			fmd_isolated_asru(hdl, asru);
		}
		if (degrade_device)
			(void) zpool_vdev_degrade(zhp, vdev_guid, aux);

		/*
		 * Attempt to substitute a hot spare.
		 */
		replace_with_spare(hdl, zhp, vdev);
		zpool_close(zhp);
	}
}

static const fmd_hdl_ops_t fmd_ops = {
	zfs_retire_recv,	/* fmdo_recv */
	NULL,			/* fmdo_timeout */
	NULL,			/* fmdo_close */
	NULL,			/* fmdo_stats */
	NULL,			/* fmdo_gc */
};

static const fmd_prop_t fmd_props[] = {
	{ "spare_on_remove", FMD_TYPE_BOOL, "true" },
	{ NULL, 0, NULL }
};

static const fmd_hdl_info_t fmd_info = {
	"ZFS Retire Agent", "1.0", &fmd_ops, fmd_props
};

void
_fmd_init(fmd_hdl_t *hdl)
{
	zfs_retire_data_t *zdp;
	libzfs_handle_t *zhdl;

	if ((zhdl = libzfs_init()) == NULL)
		return;

	if (fmd_hdl_register(hdl, FMD_API_VERSION, &fmd_info) != 0) {
		libzfs_fini(zhdl);
		return;
	}

	zdp = fmd_hdl_zalloc(hdl, sizeof (zfs_retire_data_t), FMD_SLEEP);
	zdp->zrd_hdl = zhdl;

	fmd_hdl_setspecific(hdl, zdp);
}

void
_fmd_fini(fmd_hdl_t *hdl)
{
	zfs_retire_data_t *zdp = fmd_hdl_getspecific(hdl);

	if (zdp != NULL) {
		libzfs_fini(zdp->zrd_hdl);
		fmd_hdl_free(hdl, zdp, sizeof (zfs_retire_data_t));
	}
}
