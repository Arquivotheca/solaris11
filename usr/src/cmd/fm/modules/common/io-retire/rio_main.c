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

#include <sys/fm/protocol.h>
#include <fm/fmd_api.h>
#include <strings.h>
#include <libdevinfo.h>
#include <sys/libdevid.h>
#include <sys/modctl.h>
#include <sys/fs/zfs.h>
#include <sys/fm/fs/zfs.h>
#include <libzfs.h>

static int	global_disable;

struct except_list {
	char			*el_fault;
	struct except_list	*el_next;
};

static struct except_list *except_list;

static void
parse_exception_string(fmd_hdl_t *hdl, char *estr)
{
	char	*p;
	char	*next;
	size_t	len;
	struct except_list *elem;

	len = strlen(estr);

	p = estr;
	for (;;) {
		/* Remove leading ':' */
		while (*p == ':')
			p++;
		if (*p == '\0')
			break;

		next = strchr(p, ':');

		if (next)
			*next = '\0';

		elem = fmd_hdl_alloc(hdl,
		    sizeof (struct except_list), FMD_SLEEP);
		elem->el_fault = fmd_hdl_strdup(hdl, p, FMD_SLEEP);
		elem->el_next = except_list;
		except_list = elem;

		if (next) {
			*next = ':';
			p = next + 1;
		} else {
			break;
		}
	}

	if (len != strlen(estr)) {
		fmd_hdl_abort(hdl, "Error parsing exception list: %s\n", estr);
	}
}

/*
 * Returns
 *	1  if fault on exception list
 *	0  otherwise
 */
static int
fault_exception(fmd_hdl_t *hdl, nvlist_t *fault)
{
	struct except_list *elem;

	for (elem = except_list; elem; elem = elem->el_next) {
		if (fmd_nvl_class_match(hdl, fault, elem->el_fault)) {
			fmd_hdl_debug(hdl, "rio_recv: Skipping fault "
			    "on exception list (%s)\n", elem->el_fault);
			return (1);
		}
	}

	return (0);
}

static void
free_exception_list(fmd_hdl_t *hdl)
{
	struct except_list *elem;

	while (except_list) {
		elem = except_list;
		except_list = elem->el_next;
		fmd_hdl_strfree(hdl, elem->el_fault);
		fmd_hdl_free(hdl, elem, sizeof (*elem));
	}
}

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

	replacement = fmd_nvl_alloc(hdl, FMD_SLEEP);

	(void) nvlist_add_string(replacement, ZPOOL_CONFIG_TYPE,
	    VDEV_TYPE_ROOT);

	dev_name = zpool_vdev_name(NULL, zhp, vdev, B_FALSE, B_FALSE);

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

typedef struct find_cbdata {
	const char	*cb_devid;
	zpool_handle_t	*cb_zhp;
	nvlist_t	*cb_vdev;
} find_cbdata_t;

static nvlist_t *
find_vdev(libzfs_handle_t *zhdl, nvlist_t *nv, const char *search_devid)
{
	nvlist_t **child;
	uint_t c, children;
	nvlist_t *ret;
	char *devid;

	if (nvlist_lookup_string(nv, ZPOOL_CONFIG_DEVID, &devid) == 0 &&
	    devid_str_compare(devid, (char *)search_devid) == 0)
		return (nv);

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0)
		return (NULL);

	for (c = 0; c < children; c++) {
		if ((ret = find_vdev(zhdl, child[c], search_devid)) != NULL)
			return (ret);
	}

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_L2CACHE,
	    &child, &children) != 0)
		return (NULL);

	for (c = 0; c < children; c++) {
		if ((ret = find_vdev(zhdl, child[c], search_devid)) != NULL)
			return (ret);
	}

	return (NULL);
}

static int
search_pool(zpool_handle_t *zhp, void *data)
{
	find_cbdata_t *cbp = data;
	nvlist_t *config;
	nvlist_t *nvroot;

	config = zpool_get_config(zhp, NULL);
	if (nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvroot) != 0) {
		zpool_close(zhp);
		return (0);
	}

	if ((cbp->cb_vdev = find_vdev(zpool_get_handle(zhp), nvroot,
	    cbp->cb_devid)) != NULL) {
		cbp->cb_zhp = zhp;
		return (1);
	}

	zpool_close(zhp);
	return (0);
}

static zpool_handle_t *
find_by_devid(libzfs_handle_t *zhdl, const char *devid, nvlist_t **vdevp)
{
	find_cbdata_t cb;

	cb.cb_devid = devid;
	cb.cb_zhp = NULL;
	if (zpool_iter(zhdl, search_pool, &cb) != 1)
		return (NULL);

	*vdevp = cb.cb_vdev;
	return (cb.cb_zhp);
}

/*ARGSUSED*/
static void
rio_recv(fmd_hdl_t *hdl, fmd_event_t *ep, nvlist_t *nvl, const char *class)
{
	nvlist_t	**faults = NULL;
	nvlist_t	*asru, *fru = NULL;
	uint_t		nfaults = 0;
	int		f;
	char		*path;
	char		*scheme;
	di_retire_t	drt = {0};
	int		retire = 0;
	int		unretire = 0;
	int		error;
	char		*snglfault = FM_FAULT_CLASS"."FM_ERROR_IO".";
	boolean_t	rtr;
	char		*devid;
	zpool_handle_t	*zhp;
	nvlist_t	*vdev;
	uint8_t		*status;
	uint_t		nstatus;
	uint64_t	vdev_guid;
	libzfs_handle_t *zhdl = fmd_hdl_getspecific(hdl);

	/*
	 * If disabled, we don't do retire. We still do unretires though
	 */
	if (global_disable && strcmp(class, FM_LIST_REPAIRED_CLASS) != 0) {
		fmd_hdl_debug(hdl, "rio_recv: retire disabled\n");
		return;
	}

	drt.rt_abort = (void (*)(void *, const char *, ...))fmd_hdl_abort;
	drt.rt_debug = (void (*)(void *, const char *, ...))fmd_hdl_debug;
	drt.rt_hdl = hdl;

	if (strcmp(class, FM_LIST_SUSPECT_CLASS) == 0) {
		retire = 1;
	} else if (strcmp(class, FM_LIST_REPAIRED_CLASS) == 0) {
		unretire = 1;
	} else if (strcmp(class, FM_LIST_UPDATED_CLASS) == 0) {
		retire = 1;
		unretire = 1;
	} else if (strcmp(class, FM_LIST_RESOLVED_CLASS) == 0) {
		return;
	} else if (strncmp(class, snglfault, strlen(snglfault)) == 0) {
		/*
		 * Don't try and retire on restart. If we successfully retired
		 * the device before then it would have been in the retire
		 * store and will already be retired again now. If not, then
		 * it may sometimes be a required device so leave it alone.
		 */
		return;
	} else {
		fmd_hdl_debug(hdl, "rio_recv: not list.* class: %s\n", class);
		return;
	}

	if (nvlist_lookup_uint8_array(nvl,
	    FM_SUSPECT_FAULT_STATUS, &status, &nstatus) != 0) {
		fmd_hdl_debug(hdl, "rio_recv: no fault status");
		return;
	}
	if (nvlist_lookup_nvlist_array(nvl,
	    FM_SUSPECT_FAULT_LIST, &faults, &nfaults) != 0) {
		fmd_hdl_debug(hdl, "rio_recv: no fault list");
		return;
	}

	for (f = 0; f < nfaults; f++) {
		if (nvlist_lookup_boolean_value(faults[f], FM_SUSPECT_RETIRE,
		    &rtr) == 0 && !rtr) {
			fmd_hdl_debug(hdl, "rio_recv: retire suppressed");
			continue;
		}

		if (nvlist_lookup_nvlist(faults[f], FM_FAULT_ASRU,
		    &asru) != 0) {
			fmd_hdl_debug(hdl, "rio_recv: no asru in fault");
			continue;
		}

		scheme = NULL;
		if (nvlist_lookup_string(asru, FM_FMRI_SCHEME, &scheme) != 0 ||
		    strcmp(scheme, FM_FMRI_SCHEME_DEV) != 0) {
			fmd_hdl_debug(hdl, "rio_recv: not \"dev\" scheme: %s",
			    scheme ? scheme : "<NULL>");
			continue;
		}

		if (fault_exception(hdl, faults[f]))
			continue;

		if (nvlist_lookup_string(asru, FM_FMRI_DEV_PATH,
		    &path) != 0 || path[0] == '\0') {
			fmd_hdl_debug(hdl, "rio_recv: no dev path in asru");
			continue;
		}

		if (retire && fmd_nvl_fmri_has_fault(hdl, asru,
		    FMD_HAS_FAULT_ASRU | FMD_HAS_FAULT_RETIRE, NULL) == 1) {
			if (zhdl != NULL && nvlist_lookup_string(asru,
			    FM_FMRI_DEV_ID, &devid) == 0 && (zhp =
			    find_by_devid(zhdl, devid, &vdev)) != NULL) {
				/*
				 * This is a disk fault with a devid in the
				 * ASRU, and we've found a matching zfs vdev.
				 * Mark vdev as faulted, which will stop zfs
				 * from using it.
				 */
				(void) nvlist_lookup_uint64(vdev,
				    ZPOOL_CONFIG_GUID, &vdev_guid);
				(void) zpool_vdev_fault(zhp, vdev_guid,
				    VDEV_AUX_EXTERNAL);
				replace_with_spare(hdl, zhp, vdev);
				zpool_close(zhp);
			}
			/*
			 * Now we can retire the device.
			 */
			if ((error = di_retire_device(path, &drt, 0)) != 0)
				fmd_hdl_debug(hdl, "rio_recv:"
				    " di_retire_device failed:"
				    " error: %d %s", error, path);
			else
				fmd_isolated_asru(hdl, asru);
		}
		if (unretire && fmd_nvl_fmri_has_fault(hdl, asru,
		    FMD_HAS_FAULT_ASRU | FMD_HAS_FAULT_RETIRE, NULL) == 0) {
			/*
			 * First unretire the device.
			 */
			if ((error = di_unretire_device(path, &drt)) != 0) {
				fmd_hdl_debug(hdl, "rio_recv:"
				    " di_unretire_device failed:"
				    " error: %d %s", error, path);
				continue;
			}
			if (zhdl == NULL || nvlist_lookup_string(asru,
			    FM_FMRI_DEV_ID, &devid) != 0 || (zhp =
			    find_by_devid(zhdl, devid, &vdev)) == NULL) {
				if (nvlist_lookup_nvlist(faults[f],
				    FM_FAULT_FRU, &fru) != 0)
					fmd_resolved_asru(hdl, asru);
				else
					fmd_resolved_fru(hdl, fru);
				continue;
			}
			/*
			 * This is a disk fault with a devid in the
			 * ASRU, and we've found a matching zfs vdev.
			 * Clear the vdev, allowing zfs to use it again, but
			 * not if there are other (zfs) faults afflicting the
			 * same FRU, and not if the disk is not present.
			 */
			(void) nvlist_lookup_uint64(vdev, ZPOOL_CONFIG_GUID,
			    &vdev_guid);
			if (nvlist_lookup_nvlist(faults[f], FM_FAULT_FRU,
			    &fru) != 0) {
				(void) zpool_vdev_clear(zhp, vdev_guid);
				fmd_resolved_asru(hdl, asru);
			} else {
				if (!(status[f] & FM_SUSPECT_REPLACED) &&
				    !(status[f] & FM_SUSPECT_NOT_PRESENT) &&
				    fmd_nvl_fmri_has_fault(hdl, fru,
				    FMD_HAS_FAULT_FRU | FMD_HAS_FAULT_RETIRE,
				    NULL) == 0)
					(void) zpool_vdev_clear(zhp, vdev_guid);
				fmd_resolved_fru(hdl, fru);
			}
			zpool_close(zhp);
		}
	}
}

static const fmd_hdl_ops_t fmd_ops = {
	rio_recv,	/* fmdo_recv */
	NULL,		/* fmdo_timeout */
	NULL,		/* fmdo_close */
	NULL,		/* fmdo_stats */
	NULL,		/* fmdo_gc */
};

static const fmd_prop_t rio_props[] = {
	{ "global-disable", FMD_TYPE_BOOL, "false" },
	{ "fault-exceptions", FMD_TYPE_STRING, NULL },
	{ NULL, 0, NULL }
};

static const fmd_hdl_info_t fmd_info = {
	"I/O Retire Agent", "2.0", &fmd_ops, rio_props
};

void
_fmd_init(fmd_hdl_t *hdl)
{
	char	*estr;
	char	*estrdup;

	if (fmd_hdl_register(hdl, FMD_API_VERSION, &fmd_info) != 0) {
		fmd_hdl_debug(hdl, "failed to register handle\n");
		return;
	}

	global_disable = fmd_prop_get_int32(hdl, "global-disable");

	estrdup = NULL;
	if (estr = fmd_prop_get_string(hdl, "fault-exceptions")) {
		estrdup = fmd_hdl_strdup(hdl, estr, FMD_SLEEP);
		fmd_prop_free_string(hdl, estr);
		parse_exception_string(hdl, estrdup);
		fmd_hdl_strfree(hdl, estrdup);
	}
	fmd_hdl_setspecific(hdl, libzfs_init());
}

void
_fmd_fini(fmd_hdl_t *hdl)
{
	libzfs_handle_t *zhdl = fmd_hdl_getspecific(hdl);
	free_exception_list(hdl);
	if (zhdl)
		libzfs_fini(zhdl);
}
