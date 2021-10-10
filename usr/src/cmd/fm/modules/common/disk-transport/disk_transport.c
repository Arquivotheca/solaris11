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
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Disk error transport module
 *
 * This transport module is responsible for translating between disk errors
 * and FMA ereports.  It is a read-only transport module, and checks for the
 * following failures:
 *
 * 	- overtemp
 * 	- predictive failure
 * 	- self-test failure
 *
 * These failures are detected via libdiskstatus calls to perform the actual
 * analysis.  This transport module is in charge of the following tasks:
 *
 * 	- discovering available devices
 * 	- periodically checking devices
 */

#include <ctype.h>
#include <fm/fmd_api.h>
#include <fm/libdiskstatus.h>
#include <fm/libtopo.h>
#include <fm/topo_hc.h>
#include <fm/topo_mod.h>
#include <limits.h>
#include <string.h>
#include <sys/fm/io/scsi.h>
#include <sys/fm/protocol.h>
#include <libdevinfo.h>
#include <sys/stat.h>

static struct dt_stat {
	fmd_stat_t dropped;
} dt_stats = {
	{ "dropped", FMD_TYPE_UINT64, "number of dropped ereports" }
};

typedef struct disk_monitor {
	fmd_hdl_t	*dm_hdl;
	fmd_xprt_t	*dm_xprt;
	id_t		dm_timer;
	hrtime_t	dm_interval;
	char		*dm_sim_search;
	char		*dm_sim_file;
	boolean_t	dm_timer_istopo;
	nvlist_t	*paths;
} disk_monitor_t;

#define	DEVICES_DIR "/devices"

static void
dt_post_ereport(fmd_hdl_t *hdl, fmd_xprt_t *xprt, const char *protocol,
    const char *faultname, uint64_t ena, nvlist_t *detector, nvlist_t *payload)
{
	nvlist_t *nvl;
	int e = 0;
	char fullclass[PATH_MAX];

	(void) snprintf(fullclass, sizeof (fullclass), "%s.io.%s.disk.%s",
	    FM_EREPORT_CLASS, protocol, faultname);

	if (nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0) == 0) {
		e |= nvlist_add_string(nvl, FM_CLASS, fullclass);
		e |= nvlist_add_uint8(nvl, FM_VERSION, FM_EREPORT_VERSION);
		e |= nvlist_add_uint64(nvl, FM_EREPORT_ENA, ena);
		e |= nvlist_add_nvlist(nvl, FM_EREPORT_DETECTOR, detector);
		e |= nvlist_merge(nvl, payload, 0);

		if (e == 0) {
			fmd_xprt_post(hdl, xprt, nvl, 0);
		} else {
			nvlist_free(nvl);
			dt_stats.dropped.fmds_value.ui64++;
		}
	} else {
		dt_stats.dropped.fmds_value.ui64++;
	}
}

/*
 * Create a detector for use in the ereport based on the device path and
 * id of the target device.
 * This is analogous to what is done in fm.c fm_fmri_dev_set
 */
static nvlist_t *
dt_create_detector(char *devpath, char *devid)
{
	nvlist_t *detector;
	int err = 0;

	if (nvlist_alloc(&detector, NV_UNIQUE_NAME, KM_SLEEP) != 0) {
		return (NULL);
	}

	err |= nvlist_add_uint8(detector, FM_VERSION, FM_DEV_SCHEME_VERSION);
	err |= nvlist_add_string(detector, FM_FMRI_SCHEME, FM_FMRI_SCHEME_DEV);
	err |= nvlist_add_string(detector, FM_FMRI_DEV_PATH, devpath);

	if (devid != NULL)
		err |= nvlist_add_string(detector, FM_FMRI_DEV_ID, devid);

	if (err) {
		nvlist_free(detector);
		detector = NULL;
	}

	return (detector);
}

/*
 * This is the callback function for the di_walk_node operation.
 * If this is a disk node, check the minor nodes and create the path to open
 * from the di_devfs_path call and the minor node name.  Additionally obtain
 * the devid in case an ereport is needed.
 */
static int
dt_find_paths(di_node_t node, void *arg)
{
	disk_monitor_t	*dmp = arg;
	fmd_hdl_t	*hdl = dmp->dm_hdl;
	di_minor_t	minor;
	char		pathbuf[MAXPATHLEN];
	char		*devid;
	char		*devfspath;
	nvlist_t	*path_data;

	/* If the module is being unloaded, then end the walk operation. */
	if (fmd_hdl_unloading(dmp->dm_hdl) == B_TRUE)
		return (DI_WALK_TERMINATE);

	/* Only process disk nodes */
	if (strncmp("disk", di_node_name(node), 4) != 0)
		return (DI_WALK_CONTINUE);

	devfspath = di_devfs_path(node);

	minor = DI_MINOR_NIL;
	while ((minor = di_minor_next(node, minor)) != DI_MINOR_NIL) {
		if (di_minor_spectype(minor) != S_IFCHR)
			continue;
		else
			break;
	}

	if (minor == DI_MINOR_NIL) {
		fmd_hdl_debug(hdl, "No minor node found for %s", devfspath);
		di_devfs_path_free(devfspath);
		return (DI_WALK_CONTINUE);
	} else {
		(void) snprintf(pathbuf, MAXPATHLEN, "%s%s:%s",
		    DEVICES_DIR, devfspath, di_minor_name(minor));
	}

	/* Just a pointer into the data so no need to release the space. */
	if (di_prop_lookup_strings(DDI_DEV_T_ANY, node, "devid", &devid) <= 0)
			devid = NULL;

	if (nvlist_alloc(&path_data, NV_UNIQUE_NAME, 0) == 0) {
		(void) nvlist_add_string(path_data, "devid", devid);
		(void) nvlist_add_string(path_data, "devfspath", devfspath);
		(void) nvlist_add_nvlist(dmp->paths, pathbuf, path_data);
		nvlist_free(path_data);
	} else {
		/*
		 * Memory allocation error.  Any subsequent nodes are likely
		 * are likely to fail so terminate now.
		 */
		fmd_hdl_debug(hdl, "Unable to allocate path_data nvlist");
		di_devfs_path_free(devfspath);
		return (DI_WALK_TERMINATE);
	}

	di_devfs_path_free(devfspath);

	return (DI_WALK_CONTINUE);

}

/*
 * Execute the libdiskstatus tests on the disk indicated by the disk_path
 * parameter.  If the test indicates any faults detected, then generate an
 * ereport.
 */
static void
dt_test_disk(disk_monitor_t *dmp, nvpair_t *disk_path)
{
	fmd_hdl_t	*hdl = dmp->dm_hdl;
	disk_status_t	*disk_status;
	nvlist_t	*status;    /* This contains the status result */
	nvpair_t	*elem = NULL;
	nvlist_t	*details;
	nvlist_t	*faults;
	boolean_t	fault;
	char		*protocol;
	nvlist_t	*detector = NULL;
	uint64_t	ena;
	int		err = 0;
	char		*path = nvpair_name(disk_path);

	if ((disk_status = disk_status_open(path, &err)) == NULL) {
		fmd_hdl_debug(hdl, "Error opening %s, status: %d\n",
		    path, err);
		return;
	}

	if ((status = disk_status_get(disk_status)) == NULL) {
		err = (disk_status_errno(disk_status) == EDS_NOMEM ?
		    EMOD_NOMEM : EMOD_METHOD_NOTSUP);
		disk_status_close(disk_status);
		fmd_hdl_debug(hdl, "disk_status_get failed: %d\n", err);
		return;
	}

	/*
	 * If there were any faults found, then generate an ereport
	 * based on the status data.
	 */
	if (nvlist_lookup_nvlist(status, "faults", &faults) == 0 &&
	    nvlist_lookup_string(status, "protocol", &protocol) == 0) {

		char *devfspath;
		char *devid;
		nvlist_t *path_data;

		if (nvpair_value_nvlist(disk_path, &path_data) != 0) {
			fmd_hdl_debug(hdl, "Unable to retrieve path data\n");
			goto finally;
		}

		err |= nvlist_lookup_string(path_data, "devfspath", &devfspath);
		err |= nvlist_lookup_string(path_data, "devid", &devid);
		if (err != 0) {
			fmd_hdl_debug(hdl,
			    "Unable to retrieve either devid or devfspath\n");
			goto finally;
		}

		if ((detector = dt_create_detector(devfspath, devid)) == NULL) {
			fmd_hdl_debug(hdl, "Detector creation failed\n");
			goto finally;
		}
		ena = fmd_event_ena_create(dmp->dm_hdl);

		/* elem contains the fault information */
		while ((elem = nvlist_next_nvpair(faults, elem)) != NULL) {
			if (nvpair_type(elem) != DATA_TYPE_BOOLEAN_VALUE)
				continue;

			(void) nvpair_value_boolean_value(elem, &fault);
			if (!fault ||
			    nvlist_lookup_nvlist(status, nvpair_name(elem),
			    &details) != 0)
				continue;
			dt_post_ereport(dmp->dm_hdl, dmp->dm_xprt, protocol,
			    nvpair_name(elem), ena, detector, details);
		}
	}

finally:
	nvlist_free(status);
	nvlist_free(detector);
	disk_status_close(disk_status);
}

/*
 * Periodic timeout.  Iterates over all dev info nodes, finding the disks and
 * executing the tests on each one.
 */
/*ARGSUSED*/
static void
dt_timeout(fmd_hdl_t *hdl, id_t id, void *data)
{
	di_node_t root_node;
	disk_monitor_t *dmp = fmd_hdl_getspecific(hdl);
	dmp->dm_hdl = hdl;
	topo_hdl_t *thp;
	nvpair_t *path;

	if (nvlist_alloc(&(dmp->paths), NV_UNIQUE_NAME, 0) == 0) {

		thp = fmd_hdl_topo_hold(hdl, TOPO_VERSION);
		root_node = topo_hdl_devinfo(thp);
		(void) di_walk_node(root_node, DI_WALK_CLDFIRST, dmp,
		    dt_find_paths);
		fmd_hdl_topo_rele(hdl, thp);

		for (path = nvlist_next_nvpair(dmp->paths, NULL); path != NULL;
		    path = nvlist_next_nvpair(dmp->paths, path)) {

			/* Exit if FMD is attempting to unload the transport. */
			if (fmd_hdl_unloading(dmp->dm_hdl) == B_TRUE)
				break;

			dt_test_disk(dmp, path);
		}
		nvlist_free(dmp->paths);
		dmp->paths = NULL;
	} else
		fmd_hdl_debug(hdl, "Unable to allocate an nvlist for paths\n");

	dmp->dm_timer = fmd_timer_install(hdl, NULL, NULL, dmp->dm_interval);
	dmp->dm_timer_istopo = B_FALSE;
}

/*
 * Called when the topology may have changed.  We want to examine all disks in
 * case a new one has been inserted, but we don't want to overwhelm the system
 * in the event of a flurry of topology changes, as most likely only a small
 * number of disks are changing.  To avoid this, we set the timer for a small
 * but non-trivial interval (by default 1 minute), and ignore intervening
 * changes during this period.  This still gives us a reasonable response time
 * to newly inserted devices without overwhelming the system if lots of hotplug
 * activity is going on.
 */
/*ARGSUSED*/
static void
dt_topo_change(fmd_hdl_t *hdl, topo_hdl_t *thp)
{
	disk_monitor_t *dmp = fmd_hdl_getspecific(hdl);

	fmd_hdl_debug(hdl, "dt_topo_change: thp=0x%p\n", (void *)thp);

	if (dmp->dm_timer_istopo)
		return;

	fmd_timer_remove(hdl, dmp->dm_timer);
	dmp->dm_timer = fmd_timer_install(hdl, NULL, NULL,
	    fmd_prop_get_int64(hdl, "min-interval"));
	dmp->dm_timer_istopo = B_TRUE;
}

static const fmd_prop_t fmd_props[] = {
	{ "interval", FMD_TYPE_TIME, "1h" },
	{ "min-interval", FMD_TYPE_TIME, "1min" },
	{ "simulate", FMD_TYPE_STRING, "" },
	{ NULL, 0, NULL }
};

static const fmd_hdl_ops_t fmd_ops = {
	NULL,			/* fmdo_recv */
	dt_timeout,		/* fmdo_timeout */
	NULL, 			/* fmdo_close */
	NULL,			/* fmdo_stats */
	NULL,			/* fmdo_gc */
	NULL,			/* fmdo_send */
	dt_topo_change,		/* fmdo_topo_change */
};

static const fmd_hdl_info_t fmd_info = {
	"Disk Transport Agent", "2.0", &fmd_ops, fmd_props
};

void
_fmd_init(fmd_hdl_t *hdl)
{
	disk_monitor_t *dmp;
	char *simulate;

	if (fmd_hdl_register(hdl, FMD_API_VERSION, &fmd_info) != 0)
		return;

	(void) fmd_stat_create(hdl, FMD_STAT_NOALLOC,
	    sizeof (dt_stats) / sizeof (fmd_stat_t),
	    (fmd_stat_t *)&dt_stats);

	dmp = fmd_hdl_zalloc(hdl, sizeof (disk_monitor_t), FMD_SLEEP);
	fmd_hdl_setspecific(hdl, dmp);

	dmp->dm_xprt = fmd_xprt_open(hdl, FMD_XPRT_RDONLY, NULL, NULL);
	dmp->dm_interval = fmd_prop_get_int64(hdl, "interval");

	/*
	 * Determine if we have the simulate property set.  This property allows
	 * the developer to substitute a faulty device based off all or part of
	 * an FMRI string.  For example, one could do:
	 *
	 * 	setprop simulate "bay=4/disk=4	/path/to/sim.so"
	 *
	 * When the transport module encounters an FMRI containing the given
	 * string, then it will open the simulator file instead of the
	 * corresponding device.  This can be any file, but is intended to be a
	 * libdiskstatus simulator shared object, capable of faking up SCSI
	 * responses.
	 *
	 * The property consists of two strings, an FMRI fragment and an
	 * absolute path, separated by whitespace.
	 */
	simulate = fmd_prop_get_string(hdl, "simulate");
	if (simulate[0] != '\0') {
		const char *sep;
		size_t len;

		for (sep = simulate; *sep != '\0'; sep++) {
			if (isspace(*sep))
				break;
		}

		if (*sep != '\0') {
			len = sep - simulate;

			dmp->dm_sim_search = fmd_hdl_alloc(hdl,
			    len + 1, FMD_SLEEP);
			(void) memcpy(dmp->dm_sim_search, simulate, len);
			dmp->dm_sim_search[len] = '\0';
		}

		for (; *sep != '\0'; sep++) {
			if (!isspace(*sep))
				break;
		}

		if (*sep != '\0') {
			dmp->dm_sim_file = fmd_hdl_strdup(hdl, sep, FMD_SLEEP);
		} else if (dmp->dm_sim_search) {
			fmd_hdl_strfree(hdl, dmp->dm_sim_search);
			dmp->dm_sim_search = NULL;
		}
	}
	fmd_prop_free_string(hdl, simulate);

	/*
	 * Call our initial timer routine.  This will do an initial check of all
	 * the disks, and then start the periodic timeout.
	 */
	dmp->dm_timer = fmd_timer_install(hdl, NULL, NULL, 0);
}

void
_fmd_fini(fmd_hdl_t *hdl)
{
	disk_monitor_t *dmp;

	dmp = fmd_hdl_getspecific(hdl);
	if (dmp) {
		fmd_xprt_close(hdl, dmp->dm_xprt);
		fmd_hdl_strfree(hdl, dmp->dm_sim_search);
		fmd_hdl_strfree(hdl, dmp->dm_sim_file);
		fmd_hdl_free(hdl, dmp, sizeof (*dmp));
	}
}
