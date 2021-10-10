/*
 *
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <alloca.h>
#include <assert.h>
#include <limits.h>
#include <zone.h>
#include <fm/topo_mod.h>
#include <fm/topo_hc.h>
#include <fm/fmd_fmri.h>
#include <sys/param.h>
#include <sys/systeminfo.h>
#include <sys/fm/protocol.h>
#include <sys/stat.h>
#include <sys/systeminfo.h>
#include <sys/utsname.h>
#include <sys/libdevid.h>

#include <topo_method.h>
#include <topo_module.h>
#include <topo_subr.h>
#include <topo_prop.h>
#include <topo_tree.h>
#include <topo_fmri.h>
#include <hc.h>

static int hc_enum(topo_mod_t *, tnode_t *, const char *, topo_instance_t,
    topo_instance_t, void *, void *);
static void hc_release(topo_mod_t *, tnode_t *);
static int hc_fmri_nvl2str(topo_mod_t *, tnode_t *, topo_version_t,
    nvlist_t *, nvlist_t **);
static int hc_fmri_str2nvl(topo_mod_t *, tnode_t *, topo_version_t,
    nvlist_t *, nvlist_t **);
static int hc_fmri_presence_state(topo_mod_t *, tnode_t *, topo_version_t,
    nvlist_t *, nvlist_t **);
static int hc_fmri_retire(topo_mod_t *, tnode_t *, topo_version_t, nvlist_t *,
    nvlist_t **);
static int hc_fmri_unretire(topo_mod_t *, tnode_t *, topo_version_t, nvlist_t *,
    nvlist_t **);
static int hc_fmri_service_state(topo_mod_t *, tnode_t *, topo_version_t,
    nvlist_t *, nvlist_t **);
static int hc_fmri_create_meth(topo_mod_t *, tnode_t *, topo_version_t,
    nvlist_t *, nvlist_t **);
static int hc_fmri_prop_get(topo_mod_t *, tnode_t *, topo_version_t,
    nvlist_t *, nvlist_t **);
static int hc_fmri_prop_set(topo_mod_t *, tnode_t *, topo_version_t,
    nvlist_t *, nvlist_t **);
static int hc_fmri_pgrp_get(topo_mod_t *, tnode_t *, topo_version_t,
    nvlist_t *, nvlist_t **);
static int hc_fmri_strcmp_path(topo_mod_t *, tnode_t *, topo_version_t,
    nvlist_t *, nvlist_t **);
static int hc_fmri_strcmp_ident(topo_mod_t *, tnode_t *, topo_version_t,
    nvlist_t *, nvlist_t **);
static int hc_fmri_ident_node(topo_mod_t *, tnode_t *, topo_version_t,
    nvlist_t *, nvlist_t **);
static int hc_fmri_strhash_path(topo_mod_t *, tnode_t *, topo_version_t,
    nvlist_t *, nvlist_t **);
static int hc_fmri_facility(topo_mod_t *, tnode_t *, topo_version_t,
    nvlist_t *, nvlist_t **);

static nvlist_t *hc_fmri_create(topo_mod_t *, nvlist_t *, int, const char *,
    topo_instance_t inst, const nvlist_t *, const char *, const char *,
    const char *);

const topo_method_t hc_methods[] = {
	{ TOPO_METH_NVL2STR, TOPO_METH_NVL2STR_DESC, TOPO_METH_NVL2STR_VERSION,
	    TOPO_STABILITY_INTERNAL, hc_fmri_nvl2str },
	{ TOPO_METH_STR2NVL, TOPO_METH_STR2NVL_DESC, TOPO_METH_STR2NVL_VERSION,
	    TOPO_STABILITY_INTERNAL, hc_fmri_str2nvl },
	{ TOPO_METH_PRESENCE_STATE, TOPO_METH_PRESENCE_STATE_DESC,
	    TOPO_METH_PRESENCE_STATE_VERSION, TOPO_STABILITY_INTERNAL,
	    hc_fmri_presence_state },
	{ TOPO_METH_RETIRE, TOPO_METH_RETIRE_DESC,
	    TOPO_METH_RETIRE_VERSION, TOPO_STABILITY_INTERNAL,
	    hc_fmri_retire },
	{ TOPO_METH_UNRETIRE, TOPO_METH_UNRETIRE_DESC,
	    TOPO_METH_UNRETIRE_VERSION, TOPO_STABILITY_INTERNAL,
	    hc_fmri_unretire },
	{ TOPO_METH_SERVICE_STATE, TOPO_METH_SERVICE_STATE_DESC,
	    TOPO_METH_SERVICE_STATE_VERSION, TOPO_STABILITY_INTERNAL,
	    hc_fmri_service_state },
	{ TOPO_METH_FMRI, TOPO_METH_FMRI_DESC, TOPO_METH_FMRI_VERSION,
	    TOPO_STABILITY_INTERNAL, hc_fmri_create_meth },
	{ TOPO_METH_PROP_GET, TOPO_METH_PROP_GET_DESC,
	    TOPO_METH_PROP_GET_VERSION, TOPO_STABILITY_INTERNAL,
	    hc_fmri_prop_get },
	{ TOPO_METH_PROP_SET, TOPO_METH_PROP_SET_DESC,
	    TOPO_METH_PROP_SET_VERSION, TOPO_STABILITY_INTERNAL,
	    hc_fmri_prop_set },
	{ TOPO_METH_PGRP_GET, TOPO_METH_PGRP_GET_DESC,
	    TOPO_METH_PGRP_GET_VERSION, TOPO_STABILITY_INTERNAL,
	    hc_fmri_pgrp_get },
	{ TOPO_METH_STRCMP_PATH, TOPO_METH_STRCMP_PATH_DESC,
	    TOPO_METH_STRCMP_VERSION, TOPO_STABILITY_INTERNAL,
	    hc_fmri_strcmp_path },
	{ TOPO_METH_STRHASH_PATH, TOPO_METH_STRHASH_PATH_DESC,
	    TOPO_METH_STRHASH_VERSION, TOPO_STABILITY_INTERNAL,
	    hc_fmri_strhash_path },
	{ TOPO_METH_STRCMP_IDENT, TOPO_METH_STRCMP_IDENT_DESC,
	    TOPO_METH_STRCMP_VERSION, TOPO_STABILITY_INTERNAL,
	    hc_fmri_strcmp_ident },
	{ TOPO_METH_IDENT_NODE, TOPO_METH_IDENT_NODE_DESC,
	    TOPO_METH_IDENT_NODE_VERSION, TOPO_STABILITY_INTERNAL,
	    hc_fmri_ident_node },
	{ TOPO_METH_FACILITY, TOPO_METH_FACILITY_DESC,
	    TOPO_METH_FACILITY_VERSION, TOPO_STABILITY_INTERNAL,
	    hc_fmri_facility },
	{ NULL }
};

static const topo_modops_t hc_ops =
	{ hc_enum, hc_release };
static const topo_modinfo_t hc_info =
	{ HC, FM_FMRI_SCHEME_HC, HC_VERSION, &hc_ops };

static const hcc_t hc_canon[] = {
	{ BANK, TOPO_STABILITY_PRIVATE },
	{ BAY, TOPO_STABILITY_PRIVATE },
	{ BLADE, TOPO_STABILITY_PRIVATE },
	{ BRANCH, TOPO_STABILITY_PRIVATE },
	{ CMP, TOPO_STABILITY_PRIVATE },
	{ CENTERPLANE, TOPO_STABILITY_PRIVATE },
	{ CHASSIS, TOPO_STABILITY_PRIVATE },
	{ CHIP, TOPO_STABILITY_PRIVATE },
	{ CHIP_SELECT, TOPO_STABILITY_PRIVATE },
	{ CORE, TOPO_STABILITY_PRIVATE },
	{ CONTROLLER, TOPO_STABILITY_PRIVATE },
	{ CPU, TOPO_STABILITY_PRIVATE },
	{ CPUBOARD, TOPO_STABILITY_PRIVATE },
	{ DIMM, TOPO_STABILITY_PRIVATE },
	{ DISK, TOPO_STABILITY_PRIVATE },
	{ DRAM, TOPO_STABILITY_PRIVATE },
	{ DRAMCHANNEL, TOPO_STABILITY_PRIVATE },
	{ EXTERNALCHASSIS, TOPO_STABILITY_PRIVATE },
	{ FAN, TOPO_STABILITY_PRIVATE },
	{ FANBOARD, TOPO_STABILITY_PRIVATE },
	{ FANMODULE, TOPO_STABILITY_PRIVATE },
	{ HBA, TOPO_STABILITY_PRIVATE },
	{ HOSTBRIDGE, TOPO_STABILITY_PRIVATE },
	{ INTERCONNECT, TOPO_STABILITY_PRIVATE },
	{ IOBOARD, TOPO_STABILITY_PRIVATE },
	{ IPORT, TOPO_STABILITY_PRIVATE },
	{ MEMBOARD, TOPO_STABILITY_PRIVATE },
	{ MEMORYBUFFER, TOPO_STABILITY_PRIVATE },
	{ MEMORYCONTROL, TOPO_STABILITY_PRIVATE },
	{ MICROCORE, TOPO_STABILITY_PRIVATE },
	{ MOTHERBOARD, TOPO_STABILITY_PRIVATE },
	{ NIU, TOPO_STABILITY_PRIVATE },
	{ NIUFN, TOPO_STABILITY_PRIVATE },
	{ PCI_BUS, TOPO_STABILITY_PRIVATE },
	{ PCI_DEVICE, TOPO_STABILITY_PRIVATE },
	{ PCI_FUNCTION, TOPO_STABILITY_PRIVATE },
	{ PCIEX_BUS, TOPO_STABILITY_PRIVATE },
	{ PCIEX_DEVICE, TOPO_STABILITY_PRIVATE },
	{ PCIEX_FUNCTION, TOPO_STABILITY_PRIVATE },
	{ PCIEX_ROOT, TOPO_STABILITY_PRIVATE },
	{ PCIEX_SWUP, TOPO_STABILITY_PRIVATE },
	{ PCIEX_SWDWN, TOPO_STABILITY_PRIVATE },
	{ POWERBOARD, TOPO_STABILITY_PRIVATE },
	{ POWERMODULE, TOPO_STABILITY_PRIVATE },
	{ PSU, TOPO_STABILITY_PRIVATE },
	{ RANK, TOPO_STABILITY_PRIVATE },
	{ RECEPTACLE, TOPO_STABILITY_PRIVATE },
	{ RISER, TOPO_STABILITY_PRIVATE },
	{ SASEXPANDER, TOPO_STABILITY_PRIVATE },
	{ SCSI_DEVICE, TOPO_STABILITY_PRIVATE },
	{ SHELF, TOPO_STABILITY_PRIVATE },
	{ SES_ENCLOSURE, TOPO_STABILITY_PRIVATE },
	{ SMP_DEVICE, TOPO_STABILITY_PRIVATE },
	{ SP, TOPO_STABILITY_PRIVATE },
	{ STRAND, TOPO_STABILITY_PRIVATE },
	{ SUBCHASSIS, TOPO_STABILITY_PRIVATE },
	{ SYSTEMBOARD, TOPO_STABILITY_PRIVATE },
	{ USB_BUS, TOPO_STABILITY_PRIVATE },
	{ USB_HUB, TOPO_STABILITY_PRIVATE },
	{ USB_DEV, TOPO_STABILITY_PRIVATE },
	{ USB_IFC, TOPO_STABILITY_PRIVATE },
	{ XAUI, TOPO_STABILITY_PRIVATE },
	{ XFP, TOPO_STABILITY_PRIVATE }
};

static int hc_ncanon = sizeof (hc_canon) / sizeof (hcc_t);

int
hc_init(topo_mod_t *mod, topo_version_t version)
{
	/*
	 * Turn on module debugging output
	 */
	if (getenv("TOPOHCDEBUG"))
		topo_mod_setdebug(mod);

	topo_mod_dprintf(mod, "initializing hc builtin\n");

	if (version != HC_VERSION)
		return (topo_mod_seterrno(mod, EMOD_VER_NEW));

	if (topo_mod_register(mod, &hc_info, TOPO_VERSION) != 0) {
		topo_mod_dprintf(mod, "failed to register hc: "
		    "%s\n", topo_mod_errmsg(mod));
		return (-1); /* mod errno already set */
	}

	return (0);
}

void
hc_fini(topo_mod_t *mod)
{
	topo_mod_unregister(mod);
}


static const topo_pgroup_info_t sys_pgroup = {
	TOPO_PGROUP_SYSTEM,
	TOPO_STABILITY_PRIVATE,
	TOPO_STABILITY_PRIVATE,
	1
};

static const topo_pgroup_info_t auth_pgroup = {
	FM_FMRI_AUTHORITY,
	TOPO_STABILITY_PRIVATE,
	TOPO_STABILITY_PRIVATE,
	1
};

static void
hc_prop_set(tnode_t *node, nvlist_t *auth)
{
	int err;
	char isa[MAXNAMELEN];
	struct utsname uts;
	char *prod, *psn, *csn, *server;

	if (auth == NULL)
		return;

	if (topo_pgroup_create(node, &auth_pgroup, &err) != 0) {
		if (err != ETOPO_PROP_DEFD)
			return;
	}

	/*
	 * Inherit if we can, it saves memory
	 */
	if ((topo_prop_inherit(node, FM_FMRI_AUTHORITY, FM_FMRI_AUTH_PRODUCT,
	    &err) != 0) && (err != ETOPO_PROP_DEFD)) {
		if (nvlist_lookup_string(auth, FM_FMRI_AUTH_PRODUCT, &prod)
		    == 0)
			(void) topo_prop_set_string(node, FM_FMRI_AUTHORITY,
			    FM_FMRI_AUTH_PRODUCT, TOPO_PROP_IMMUTABLE, prod,
			    &err);
	}
	if ((topo_prop_inherit(node, FM_FMRI_AUTHORITY, FM_FMRI_AUTH_PRODUCT_SN,
	    &err) != 0) && (err != ETOPO_PROP_DEFD)) {
		if (nvlist_lookup_string(auth, FM_FMRI_AUTH_PRODUCT_SN, &psn)
		    == 0)
			(void) topo_prop_set_string(node, FM_FMRI_AUTHORITY,
			    FM_FMRI_AUTH_PRODUCT_SN, TOPO_PROP_IMMUTABLE, psn,
			    &err);
	}
	if ((topo_prop_inherit(node, FM_FMRI_AUTHORITY, FM_FMRI_AUTH_CHASSIS,
	    &err) != 0) && (err != ETOPO_PROP_DEFD)) {
		if (nvlist_lookup_string(auth, FM_FMRI_AUTH_CHASSIS, &csn) == 0)
			(void) topo_prop_set_string(node, FM_FMRI_AUTHORITY,
			    FM_FMRI_AUTH_CHASSIS, TOPO_PROP_IMMUTABLE, csn,
			    &err);
	}
	if ((topo_prop_inherit(node, FM_FMRI_AUTHORITY, FM_FMRI_AUTH_SERVER,
	    &err) != 0) && (err != ETOPO_PROP_DEFD)) {
		if (nvlist_lookup_string(auth, FM_FMRI_AUTH_SERVER, &server)
		    == 0)
			(void) topo_prop_set_string(node, FM_FMRI_AUTHORITY,
			    FM_FMRI_AUTH_SERVER, TOPO_PROP_IMMUTABLE, server,
			    &err);
	}

	if (topo_pgroup_create(node, &sys_pgroup, &err) != 0)
		return;

	isa[0] = '\0';
	(void) sysinfo(SI_ARCHITECTURE, isa, sizeof (isa));
	(void) uname(&uts);
	(void) topo_prop_set_string(node, TOPO_PGROUP_SYSTEM, TOPO_PROP_ISA,
	    TOPO_PROP_IMMUTABLE, isa, &err);
	(void) topo_prop_set_string(node, TOPO_PGROUP_SYSTEM, TOPO_PROP_MACHINE,
	    TOPO_PROP_IMMUTABLE, uts.machine, &err);
}

/*ARGSUSED*/
int
hc_enum(topo_mod_t *mod, tnode_t *pnode, const char *name, topo_instance_t min,
    topo_instance_t max, void *notused1, void *notused2)
{
	int isglobal = (getzoneid() == GLOBAL_ZONEID);
	nvlist_t *pfmri = NULL;
	nvlist_t *nvl;
	nvlist_t *auth;
	tnode_t *node;
	int err;
	/*
	 * Register root node methods
	 */
	if (strcmp(name, HC) == 0) {
		(void) topo_method_register(mod, pnode, hc_methods);
		return (0);
	}
	if (min != max) {
		topo_mod_dprintf(mod,
		    "Request to enumerate %s component with an "
		    "ambiguous instance number, min (%d) != max (%d).\n",
		    HC, min, max);
		return (topo_mod_seterrno(mod, EINVAL));
	}

	if (!isglobal)
		return (0);

	(void) topo_node_resource(pnode, &pfmri, &err);
	auth = topo_mod_auth(mod, pnode);
	nvl = hc_fmri_create(mod, pfmri, FM_HC_SCHEME_VERSION, name, min,
	    auth, NULL, NULL, NULL);
	nvlist_free(pfmri);	/* callee ignores NULLs */
	if (nvl == NULL) {
		nvlist_free(auth);
		return (-1);
	}

	if ((node = topo_node_bind(mod, pnode, name, min, nvl)) == NULL) {
		topo_mod_dprintf(mod, "topo_node_bind failed: %s\n",
		    topo_strerror(topo_mod_errno(mod)));
		nvlist_free(auth);
		nvlist_free(nvl);
		return (-1);
	}

	/*
	 * Set FRU for the motherboard node
	 */
	if (strcmp(name, MOTHERBOARD) == 0)
		(void) topo_node_fru_set(node, nvl, 0, &err);

	hc_prop_set(node, auth);
	nvlist_free(nvl);
	nvlist_free(auth);

	return (0);
}

/*ARGSUSED*/
static void
hc_release(topo_mod_t *mp, tnode_t *node)
{
	topo_method_unregister_all(mp, node);
}

static void
fmristr_hcs_build(ssize_t *sz, char *buf, size_t buflen, nvlist_t *hcs,
    char *hcsn)
{
	uint64_t hcsv;
	char decstr[30];

	if (nvlist_lookup_uint64(hcs, hcsn, &hcsv) == 0) {
		(void) snprintf(decstr, sizeof (decstr), "%llu", hcsv);
		topo_fmristr_build(sz, buf, buflen, "/", NULL, NULL);
		topo_fmristr_build(sz, buf, buflen, "=", hcsn, decstr);
	}
}

static ssize_t
fmri_nvl2str(nvlist_t *nvl, char *buf, size_t buflen)
{
	nvlist_t **hcprs = NULL;
	nvlist_t *hcsp = NULL;
	nvlist_t *anvl = NULL;
	nvpair_t *apair;
	nvlist_t *fnvl;
	uint8_t version;
	ssize_t size = 0;
	uint_t hcnprs;
	char *devid = NULL;
	char *parent_serial = NULL;
	char *serial = NULL;
	char *part = NULL;
	char *root = NULL;
	char *rev = NULL;
	char *aname, *aval;
	char *fname = NULL, *ftype = NULL;
	int err, i;

	if (nvlist_lookup_uint8(nvl, FM_VERSION, &version) != 0 ||
	    version > FM_HC_SCHEME_VERSION)
		return (0);

	/* Get authority, if present */
	err = nvlist_lookup_nvlist(nvl, FM_FMRI_AUTHORITY, &anvl);
	if (err != 0 && err != ENOENT)
		return (0);

	(void) nvlist_lookup_string(nvl, FM_FMRI_HC_ROOT, &root);

	err = nvlist_lookup_nvlist_array(nvl, FM_FMRI_HC_LIST, &hcprs, &hcnprs);
	if (err != 0 || hcprs == NULL)
		return (0);

	(void) nvlist_lookup_string(nvl, FM_FMRI_HC_SERIAL_ID, &serial);
	(void) nvlist_lookup_string(nvl, FM_FMRI_HC_DEVID, &devid);
	(void) nvlist_lookup_string(nvl, FM_FMRI_HC_PARENT_SERIAL,
	    &parent_serial);
	(void) nvlist_lookup_string(nvl, FM_FMRI_HC_PART, &part);
	(void) nvlist_lookup_string(nvl, FM_FMRI_HC_REVISION, &rev);

	/* hc:// */
	topo_fmristr_build(&size, buf, buflen, FM_FMRI_SCHEME_HC, NULL, "://");

	/* authority, if any */
	if (anvl != NULL) {
		for (apair = nvlist_next_nvpair(anvl, NULL);
		    apair != NULL; apair = nvlist_next_nvpair(anvl, apair)) {
			if (nvpair_type(apair) != DATA_TYPE_STRING ||
			    nvpair_value_string(apair, &aval) != 0)
				continue;
			aname = nvpair_name(apair);
			topo_fmristr_build(&size, buf, buflen, ":", NULL, NULL);
			topo_fmristr_build(&size, buf, buflen, "=",
			    aname, aval);
		}
	}

	/* hardware-id part */
	topo_fmristr_build(&size,
	    buf, buflen, serial, ":" FM_FMRI_HC_SERIAL_ID "=", NULL);
	topo_fmristr_build(&size,
	    buf, buflen, devid, ":" FM_FMRI_HC_DEVID "=", NULL);
	topo_fmristr_build(&size,
	    buf, buflen, parent_serial, ":" FM_FMRI_HC_PARENT_SERIAL "=", NULL);
	topo_fmristr_build(&size,
	    buf, buflen, part, ":" FM_FMRI_HC_PART "=", NULL);
	topo_fmristr_build(&size,
	    buf, buflen, rev, ":" FM_FMRI_HC_REVISION "=", NULL);

	/* separating slash */
	topo_fmristr_build(&size, buf, buflen, "/", NULL, NULL);

	/* hc-root */
	if (root)
		topo_fmristr_build(&size, buf, buflen, root, NULL, NULL);

	/* all the pairs */
	for (i = 0; i < hcnprs; i++) {
		char *nm = NULL;
		char *id = NULL;

		if (i > 0)
			topo_fmristr_build(&size,
			    buf, buflen, "/", NULL, NULL);
		(void) nvlist_lookup_string(hcprs[i], FM_FMRI_HC_NAME, &nm);
		(void) nvlist_lookup_string(hcprs[i], FM_FMRI_HC_ID, &id);
		if (nm == NULL || id == NULL)
			return (0);
		topo_fmristr_build(&size, buf, buflen, nm, NULL, "=");
		topo_fmristr_build(&size, buf, buflen, id, NULL, NULL);
	}

	if (nvlist_lookup_nvlist(nvl, FM_FMRI_HC_SPECIFIC, &hcsp) == 0) {
		char *hcsn = NULL;
		char hexstr[17];
		uint64_t val;

		/* append offset/physaddr if it exists in hc-specific */
		if (nvlist_lookup_uint64(hcsp, FM_FMRI_HC_SPECIFIC_OFFSET,
		    &val) == 0 || nvlist_lookup_uint64(hcsp,
		    "asru-" FM_FMRI_HC_SPECIFIC_OFFSET, &val) == 0)
			hcsn = FM_FMRI_HC_SPECIFIC_OFFSET;
		else if (nvlist_lookup_uint64(hcsp,
		    FM_FMRI_HC_SPECIFIC_PHYSADDR, &val) == 0 ||
		    nvlist_lookup_uint64(hcsp,
		    "asru-" FM_FMRI_HC_SPECIFIC_PHYSADDR, &val) == 0)
			hcsn = FM_FMRI_HC_SPECIFIC_PHYSADDR;

		if (hcsn != NULL) {
			(void) snprintf(hexstr, sizeof (hexstr), "%llx", val);
			topo_fmristr_build(&size, buf, buflen, "/", NULL, NULL);
			topo_fmristr_build(&size, buf, buflen, "=", hcsn,
			    hexstr);
		} else {
			/* append l2cache/l3cache/cacheindex/cacheway */
			fmristr_hcs_build(&size, buf, buflen, hcsp,
			    FM_FMRI_HC_SPECIFIC_L2CACHE);
			fmristr_hcs_build(&size, buf, buflen, hcsp,
			    FM_FMRI_HC_SPECIFIC_L3CACHE);
			fmristr_hcs_build(&size, buf, buflen, hcsp,
			    FM_FMRI_HC_SPECIFIC_CACHEINDEX);
			fmristr_hcs_build(&size, buf, buflen, hcsp,
			    FM_FMRI_HC_SPECIFIC_CACHEWAY);
		}
	}

	/*
	 * If the nvlist represents a facility node, then we append the
	 * facility type and name to the end of the string representation using
	 * the format below:
	 *
	 * ?<ftype>=<fname>
	 */
	if (nvlist_lookup_nvlist(nvl, FM_FMRI_FACILITY, &fnvl) == 0) {
		if (nvlist_lookup_string(fnvl, FM_FMRI_FACILITY_NAME,
		    &fname) != 0 || nvlist_lookup_string(fnvl,
		    FM_FMRI_FACILITY_TYPE, &ftype) != 0)
			return (0);
		topo_fmristr_build(&size, buf, buflen, "?", NULL, NULL);
		topo_fmristr_build(&size, buf, buflen, "=", ftype, fname);
	}

	return (size);
}

/*ARGSUSED*/
static int
hc_fmri_nvl2str(topo_mod_t *mod, tnode_t *node, topo_version_t version,
    nvlist_t *nvl, nvlist_t **out)
{
	ssize_t len;
	char *name = NULL;
	nvlist_t *fmristr;

	if (version > TOPO_METH_NVL2STR_VERSION)
		return (topo_mod_seterrno(mod, EMOD_VER_NEW));

	if ((len = fmri_nvl2str(nvl, NULL, 0)) == 0 ||
	    (name = topo_mod_alloc(mod, len + 1)) == NULL ||
	    fmri_nvl2str(nvl, name, len + 1) == 0) {
		if (name != NULL)
			topo_mod_free(mod, name, len + 1);
		return (topo_mod_seterrno(mod, EMOD_FMRI_NVL));
	}

	if (topo_mod_nvalloc(mod, &fmristr, NV_UNIQUE_NAME) != 0) {
		topo_mod_free(mod, name, len + 1);
		return (topo_mod_seterrno(mod, EMOD_FMRI_NVL));
	}
	if (nvlist_add_string(fmristr, "fmri-string", name) != 0) {
		topo_mod_free(mod, name, len + 1);
		nvlist_free(fmristr);
		return (topo_mod_seterrno(mod, EMOD_FMRI_NVL));
	}
	topo_mod_free(mod, name, len + 1);
	*out = fmristr;

	return (0);
}

static nvlist_t *
hc_base_fmri_create(topo_mod_t *mod, const nvlist_t *auth, const char *part,
    const char *rev, const char *serial, const char *devid,
    const char *parent_serial)
{
	nvlist_t *fmri;
	int err = 0;

	/*
	 * Create base HC nvlist
	 */
	if (topo_mod_nvalloc(mod, &fmri, NV_UNIQUE_NAME) != 0)
		return (NULL);

	err = nvlist_add_uint8(fmri, FM_VERSION, FM_HC_SCHEME_VERSION);
	err |= nvlist_add_string(fmri, FM_FMRI_SCHEME, FM_FMRI_SCHEME_HC);
	err |= nvlist_add_string(fmri, FM_FMRI_HC_ROOT, "");
	if (err != 0) {
		nvlist_free(fmri);
		return (NULL);
	}

	/*
	 * Add optional payload members
	 */
	if (serial != NULL)
		(void) nvlist_add_string(fmri, FM_FMRI_HC_SERIAL_ID, serial);
	if (devid != NULL)
		(void) nvlist_add_string(fmri, FM_FMRI_HC_DEVID, devid);
	if (parent_serial != NULL)
		(void) nvlist_add_string(fmri, FM_FMRI_HC_PARENT_SERIAL,
		    parent_serial);
	if (part != NULL)
		(void) nvlist_add_string(fmri, FM_FMRI_HC_PART, part);
	if (rev != NULL)
		(void) nvlist_add_string(fmri, FM_FMRI_HC_REVISION, rev);
	if (auth != NULL)
		(void) nvlist_add_nvlist(fmri, FM_FMRI_AUTHORITY,
		    (nvlist_t *)auth);

	return (fmri);
}

static nvlist_t **
make_hc_pairs(topo_mod_t *mod, char *fmri, int *num)
{
	nvlist_t **pa;
	char *hc, *fromstr;
	char *starti, *startn, *endi, *endi2;
	char *ne, *ns;
	char *cname = NULL;
	char *find;
	char *cid = NULL;
	int nslashes = 0;
	int npairs = 0;
	int i, hclen;

	if ((hc = topo_mod_strdup(mod, fmri + 5)) == NULL)
		return (NULL);

	hclen = strlen(hc) + 1;

	/*
	 * Count equal signs and slashes to determine how many
	 * hc-pairs will be present in the final FMRI.  There should
	 * be at least as many slashes as equal signs.  There can be
	 * more, though if the string after an = includes them.
	 */
	if ((fromstr = strchr(hc, '/')) == NULL)
		return (NULL);

	find = fromstr;
	while ((ne = strchr(find, '=')) != NULL) {
		find = ne + 1;
		npairs++;
	}

	find = fromstr;
	while ((ns = strchr(find, '/')) != NULL) {
		find = ns + 1;
		nslashes++;
	}

	/*
	 * Do we appear to have a well-formed string version of the FMRI?
	 */
	if (nslashes < npairs || npairs == 0) {
		topo_mod_free(mod, hc, hclen);
		return (NULL);
	}

	*num = npairs;

	find = fromstr;

	if ((pa = topo_mod_zalloc(mod, npairs * sizeof (nvlist_t *))) == NULL) {
		topo_mod_free(mod, hc, hclen);
		return (NULL);
	}

	/*
	 * We go through a pretty complicated procedure to find the
	 * name and id for each pair.  That's because, unfortunately,
	 * we have some ids that can have slashes within them.  So
	 * we can't just search for the next slash after the equal sign
	 * and decide that starts a new pair.  Instead we have to find
	 * an equal sign for the next pair and work our way back to the
	 * slash from there.
	 */
	for (i = 0; i < npairs; i++) {
		startn = strchr(find, '/');
		if (startn == NULL)
			break;
		startn++;
		starti = strchr(find, '=');
		if (starti == NULL)
			break;
		*starti = '\0';
		if ((cname = topo_mod_strdup(mod, startn)) == NULL)
			break;
		*starti++ = '=';
		endi = strchr(starti, '=');
		if (endi != NULL) {
			*endi = '\0';
			endi2 = strrchr(starti, '/');
			if (endi2 == NULL)
				break;
			*endi = '=';
			*endi2 = '\0';
			if ((cid = topo_mod_strdup(mod, starti)) == NULL)
				break;
			*endi2 = '/';
			find = endi2;
		} else {
			if ((cid = topo_mod_strdup(mod, starti)) == NULL)
				break;
			find = starti + strlen(starti);
		}
		if (topo_mod_nvalloc(mod, &pa[i], NV_UNIQUE_NAME) < 0)
			break;

		if (nvlist_add_string(pa[i], FM_FMRI_HC_NAME, cname) ||
		    nvlist_add_string(pa[i], FM_FMRI_HC_ID, cid))
			break;

		topo_mod_strfree(mod, cname);
		topo_mod_strfree(mod, cid);
		cname = NULL;
		cid = NULL;
	}

	topo_mod_strfree(mod, cname);
	topo_mod_strfree(mod, cid);

	if (i < npairs) {
		for (i = 0; i < npairs; i++)
			nvlist_free(pa[i]);
		topo_mod_free(mod, pa, npairs * sizeof (nvlist_t *));
		topo_mod_free(mod, hc, hclen);
		return (NULL);
	}

	topo_mod_free(mod, hc, hclen);

	return (pa);
}

int
make_hc_auth(topo_mod_t *mod, char *fmri, char **serial, char **part,
char **rev, nvlist_t **auth, char **devid, char **parent_serial)
{
	char *starti, *startn, *endi, *copy;
	char *aname = NULL, *aid = NULL, *fs;
	nvlist_t *na = NULL;
	size_t len;

	if ((copy = topo_mod_strdup(mod, fmri + 5)) == NULL)
		return (-1);

	len = strlen(copy);

	/*
	 * Make sure there are a valid authority members
	 */
	startn = strchr(copy, ':');
	fs = strchr(copy, '/');

	if (startn == NULL || fs == NULL) {
		topo_mod_strfree(mod, copy);
		return (0);
	}

	/*
	 * The first colon we encounter must occur before the
	 * first slash
	 */
	if (startn > fs)
		goto hcabail;

	do {
		if (++startn >= copy + len)
			break;

		if ((starti = strchr(startn, '=')) == NULL)
			goto hcabail;

		*starti = '\0';
		if (++starti > copy + len)
			goto hcabail;

		if ((aname = topo_mod_strdup(mod, startn)) == NULL)
			goto hcabail;

		startn = endi = strchr(starti, ':');
		if (endi == NULL)
			if ((endi = strchr(starti, '/')) == NULL)
				break;

		*endi = '\0';
		if ((aid = topo_mod_strdup(mod, starti)) == NULL)
			goto hcabail;

		/*
		 * Return possible devid, serial, part and revision
		 */
		if (strcmp(aname, FM_FMRI_HC_SERIAL_ID) == 0) {
			*serial = topo_mod_strdup(mod, aid);
		} else if (strcmp(aname, FM_FMRI_HC_DEVID) == 0) {
			*devid = topo_mod_strdup(mod, aid);
		} else if (strcmp(aname, FM_FMRI_HC_PARENT_SERIAL) == 0) {
			*parent_serial = topo_mod_strdup(mod, aid);
		} else if (strcmp(aname, FM_FMRI_HC_PART) == 0) {
			*part = topo_mod_strdup(mod, aid);
		} else if (strcmp(aname, FM_FMRI_HC_REVISION) == 0) {
			*rev = topo_mod_strdup(mod, aid);
		} else {
			if (na == NULL) {
				if (topo_mod_nvalloc(mod, &na,
				    NV_UNIQUE_NAME) == 0) {
					(void) nvlist_add_string(na, aname,
					    aid);
				}
			} else {
				(void) nvlist_add_string(na, aname, aid);
			}
		}
		topo_mod_strfree(mod, aname);
		topo_mod_strfree(mod, aid);
		aname = aid = NULL;

	} while (startn != NULL);

	*auth = na;

	topo_mod_free(mod, copy, len + 1);
	return (0);

hcabail:
	topo_mod_free(mod, copy, len + 1);
	topo_mod_strfree(mod, aname);
	topo_mod_strfree(mod, aid);
	nvlist_free(na);
	return (-1);
}


/*
 * This function creates an nvlist to represent the facility portion of an
 * hc-scheme node, given a string representation of the fmri.  This is called by
 * hc_fmri_str2nvl.  If the string does not contain a facility component
 * (e.g. ?<ftype>=<fname>) then it bails early and returns 0.
 *
 * On failure it returns -1 and sets the topo mod errno
 */
int
make_facility(topo_mod_t *mod, char *str, nvlist_t **nvl)
{
	char *fac, *copy, *fname, *ftype;
	nvlist_t *nf = NULL;
	size_t len;

	if ((fac = strchr(str, '?')) == NULL)
		return (0);

	++fac;
	if ((copy = topo_mod_strdup(mod, fac)) == NULL)
		return (topo_mod_seterrno(mod, EMOD_NOMEM));

	fac = copy;
	len = strlen(fac);

	if ((fname = strchr(fac, '=')) == NULL) {
		topo_mod_free(mod, copy, len + 1);
		return (topo_mod_seterrno(mod, EMOD_FMRI_MALFORM));
	}

	fname[0] = '\0';
	++fname;
	ftype = fac;

	if (topo_mod_nvalloc(mod, &nf, NV_UNIQUE_NAME) != 0) {
		topo_mod_free(mod, copy, len + 1);
		return (topo_mod_seterrno(mod, EMOD_NOMEM));
	}

	if (nvlist_add_string(nf, FM_FMRI_FACILITY_NAME, fname) != 0 ||
	    nvlist_add_string(nf, FM_FMRI_FACILITY_TYPE, ftype) != 0) {
		topo_mod_free(mod, copy, len + 1);
		return (topo_mod_seterrno(mod, EMOD_FMRI_NVL));
	}

	topo_mod_free(mod, copy, len + 1);

	*nvl = nf;

	return (0);
}

/* hc-specific member names */
static struct hcs_member {
	char *name;
	int base;
} hcs_members[] = {
	FM_FMRI_HC_SPECIFIC_OFFSET, 16,
	FM_FMRI_HC_SPECIFIC_PHYSADDR, 16,
	FM_FMRI_HC_SPECIFIC_L2CACHE, 10,
	FM_FMRI_HC_SPECIFIC_L3CACHE, 10,
	FM_FMRI_HC_SPECIFIC_CACHEINDEX, 10,
	FM_FMRI_HC_SPECIFIC_CACHEWAY, 10,
	NULL, 0
};

/*ARGSUSED*/
static int
hc_fmri_str2nvl(topo_mod_t *mod, tnode_t *node, topo_version_t version,
    nvlist_t *in, nvlist_t **out)
{
	nvlist_t **pa = NULL;
	nvlist_t *nf = NULL, *auth = NULL, *fac = NULL, *hcsp = NULL;
	char *str, *hcsn, *hcsv;
	char *serial = NULL, *part = NULL, *rev = NULL;
	int npairs, n, i, e, ret;
	struct hcs_member *hcsmp;
	char *devid = NULL, *parent_serial = NULL;
	uint64_t val;

	if (version > TOPO_METH_STR2NVL_VERSION)
		return (topo_mod_seterrno(mod, EMOD_VER_NEW));

	if (nvlist_lookup_string(in, "fmri-string", &str) != 0)
		return (topo_mod_seterrno(mod, EMOD_METHOD_INVAL));

	/* We're expecting a string version of an hc scheme FMRI */
	if (strncmp(str, "hc://", 5) != 0)
		return (topo_mod_seterrno(mod, EMOD_FMRI_MALFORM));

	if ((pa = make_hc_pairs(mod, str, &npairs)) == NULL)
		return (topo_mod_seterrno(mod, EMOD_FMRI_MALFORM));

	ret = EMOD_FMRI_MALFORM;

	if (make_hc_auth(mod, str, &serial, &part, &rev, &auth, &devid,
	    &parent_serial) < 0)
		goto hcfmbail;

	if ((nf = hc_base_fmri_create(mod, auth, part, rev, serial,
	    devid, parent_serial)) == NULL)
		goto hcfmbail;

	/*
	 * Walk from the end of the string and add components to "hc-specific"
	 * if the names match the known list.
	 */
	for (n = npairs - 1; n >= 0; n--) {
		(void) nvlist_lookup_string(pa[n], FM_FMRI_HC_NAME, &hcsn);
		for (hcsmp = hcs_members; hcsmp->name; hcsmp++)
			if (strcmp(hcsmp->name, hcsn) == 0)
				break;

		/* The rest should belong to "hc-list" */
		if (hcsmp->name == NULL)
			break;

		/* found a "hc-specific" member */
		if (hcsp == NULL &&
		    topo_mod_nvalloc(mod, &hcsp, NV_UNIQUE_NAME) != 0)
			goto hcfmbail;

		(void) nvlist_lookup_string(pa[n], FM_FMRI_HC_ID, &hcsv);
		val = strtoull(hcsv, NULL, hcsmp->base);
		if (nvlist_add_uint64(hcsp, hcsn, val) != 0)
			goto hcfmbail;
	}

	if (hcsp != NULL &&
	    nvlist_add_nvlist(nf, FM_FMRI_HC_SPECIFIC, hcsp) != 0)
		goto hcfmbail;

	if ((e = nvlist_add_uint32(nf, FM_FMRI_HC_LIST_SZ, n + 1)) == 0)
		e = nvlist_add_nvlist_array(nf, FM_FMRI_HC_LIST, pa, n + 1);
	if (e != 0) {
		topo_mod_dprintf(mod, "construction of new hc nvl failed");
		goto hcfmbail;
	}

	if (make_facility(mod, str, &fac) == -1)
		goto hcfmbail;

	if (fac != NULL) {
		if (nvlist_add_nvlist(nf, FM_FMRI_FACILITY, fac) != 0)
			goto hcfmbail;
	}

	*out = nf;
	ret = 0;

hcfmbail:
	/*
	 * Clean-up
	 */
	nvlist_free(fac);
	nvlist_free(hcsp);
	for (i = 0; i < npairs; i++)
		nvlist_free(pa[i]);
	topo_mod_free(mod, pa, npairs * sizeof (nvlist_t *));
	topo_mod_strfree(mod, serial);
	topo_mod_strfree(mod, devid);
	topo_mod_strfree(mod, parent_serial);
	topo_mod_strfree(mod, part);
	topo_mod_strfree(mod, rev);
	nvlist_free(auth);
	if (ret != 0) {
		nvlist_free(nf);
		return (topo_mod_seterrno(mod, ret));
	}

	return (0);
}

static nvlist_t *
hc_list_create(topo_mod_t *mod, const char *name, char *inst)
{
	int err;
	nvlist_t *hc;

	if (topo_mod_nvalloc(mod, &hc, NV_UNIQUE_NAME) != 0)
		return (NULL);

	err = nvlist_add_string(hc, FM_FMRI_HC_NAME, name);
	err |= nvlist_add_string(hc, FM_FMRI_HC_ID, inst);
	if (err != 0) {
		nvlist_free(hc);
		return (NULL);
	}

	return (hc);
}

static nvlist_t *
hc_create_seterror(topo_mod_t *mod, nvlist_t **hcl, int n, nvlist_t *fmri,
    int err)
{
	int i;

	if (hcl != NULL) {
		for (i = 0; i < n + 1; ++i)
			nvlist_free(hcl[i]);

		topo_mod_free(mod, hcl, sizeof (nvlist_t *) * (n + 1));
	}

	nvlist_free(fmri);

	(void) topo_mod_seterrno(mod, err);

	topo_mod_dprintf(mod, "unable to create hc FMRI: %s\n",
	    topo_mod_errmsg(mod));

	return (NULL);
}

static int
hc_name_canonical(topo_mod_t *mod, const char *name)
{
	int i;

	if (getenv("NOHCCHECK") != NULL)
		return (1);

	/*
	 * Only enumerate elements with correct canonical names
	 */
	for (i = 0; i < hc_ncanon; i++) {
		if (strcmp(name, hc_canon[i].hcc_name) == 0)
			break;
	}
	if (i >= hc_ncanon) {
		topo_mod_dprintf(mod, "non-canonical name %s\n",
		    name);
		return (0);
	} else {
		return (1);
	}
}

static nvlist_t *
hc_fmri_create(topo_mod_t *mod, nvlist_t *pfmri, int version, const char *name,
    topo_instance_t inst, const nvlist_t *auth, const char *part,
    const char *rev, const char *serial)
{
	int i;
	char str[21]; /* sizeof (UINT64_MAX) + '\0' */
	uint_t pelems = 0;
	nvlist_t **phcl = NULL;
	nvlist_t **hcl = NULL;
	nvlist_t *fmri = NULL;

	if (version > FM_HC_SCHEME_VERSION)
		return (hc_create_seterror(mod,
		    hcl, pelems, fmri, EMOD_VER_OLD));
	else if (version < FM_HC_SCHEME_VERSION)
		return (hc_create_seterror(mod,
		    hcl, pelems, fmri, EMOD_VER_NEW));

	/*
	 * Check that the requested name is in our canonical list
	 */
	if (hc_name_canonical(mod, name) == 0)
		return (hc_create_seterror(mod,
		    hcl, pelems, fmri, EMOD_NONCANON));
	/*
	 * Copy the parent's HC_LIST
	 */
	if (pfmri != NULL) {
		if (nvlist_lookup_nvlist_array(pfmri, FM_FMRI_HC_LIST,
		    &phcl, &pelems) != 0)
			return (hc_create_seterror(mod,
			    hcl, pelems, fmri, EMOD_FMRI_MALFORM));
	}

	hcl = topo_mod_zalloc(mod, sizeof (nvlist_t *) * (pelems + 1));
	if (hcl == NULL)
		return (hc_create_seterror(mod,  hcl, pelems, fmri,
		    EMOD_NOMEM));

	for (i = 0; i < pelems; ++i)
		if (topo_mod_nvdup(mod, phcl[i], &hcl[i]) != 0)
			return (hc_create_seterror(mod,
			    hcl, pelems, fmri, EMOD_FMRI_NVL));

	(void) snprintf(str, sizeof (str), "%d", inst);
	if ((hcl[i] = hc_list_create(mod, name, str)) == NULL)
		return (hc_create_seterror(mod,
		    hcl, pelems, fmri, EMOD_FMRI_NVL));

	if ((fmri = hc_base_fmri_create(mod, auth, part, rev, serial,
	    NULL, NULL)) == NULL)
		return (hc_create_seterror(mod,
		    hcl, pelems, fmri, EMOD_FMRI_NVL));

	if (nvlist_add_nvlist_array(fmri, FM_FMRI_HC_LIST, hcl, pelems + 1)
	    != 0)
		return (hc_create_seterror(mod,
		    hcl, pelems, fmri, EMOD_FMRI_NVL));

	if (nvlist_add_uint32(fmri, FM_FMRI_HC_LIST_SZ, pelems + 1) != 0)
		return (hc_create_seterror(mod,
		    hcl, pelems, fmri, EMOD_FMRI_NVL));

	if (hcl != NULL) {
		for (i = 0; i < pelems + 1; ++i) {
			if (hcl[i] != NULL)
				nvlist_free(hcl[i]);
		}
		topo_mod_free(mod, hcl, sizeof (nvlist_t *) * (pelems + 1));
	}

	return (fmri);
}

/*ARGSUSED*/
static int
hc_fmri_create_meth(topo_mod_t *mod, tnode_t *node, topo_version_t version,
    nvlist_t *in, nvlist_t **out)
{
	int ret;
	nvlist_t *args, *pfmri = NULL;
	nvlist_t *auth;
	uint32_t inst;
	char *name, *serial, *rev, *part;

	if (version > TOPO_METH_FMRI_VERSION)
		return (topo_mod_seterrno(mod, EMOD_VER_NEW));

	/* First the must-have fields */
	if (nvlist_lookup_string(in, TOPO_METH_FMRI_ARG_NAME, &name) != 0)
		return (topo_mod_seterrno(mod, EMOD_METHOD_INVAL));
	if (nvlist_lookup_uint32(in, TOPO_METH_FMRI_ARG_INST, &inst) != 0)
		return (topo_mod_seterrno(mod, EMOD_METHOD_INVAL));

	/*
	 * args is optional
	 */
	pfmri = NULL;
	auth = NULL;
	serial = rev = part = NULL;
	if ((ret = nvlist_lookup_nvlist(in, TOPO_METH_FMRI_ARG_NVL, &args))
	    != 0) {
		if (ret != ENOENT)
			return (topo_mod_seterrno(mod, EMOD_METHOD_INVAL));
	} else {

		/* And then optional arguments */
		(void) nvlist_lookup_nvlist(args, TOPO_METH_FMRI_ARG_PARENT,
		    &pfmri);
		(void) nvlist_lookup_nvlist(args, TOPO_METH_FMRI_ARG_AUTH,
		    &auth);
		(void) nvlist_lookup_string(args, TOPO_METH_FMRI_ARG_PART,
		    &part);
		(void) nvlist_lookup_string(args, TOPO_METH_FMRI_ARG_REV, &rev);
		(void) nvlist_lookup_string(args, TOPO_METH_FMRI_ARG_SER,
		    &serial);
	}

	*out = hc_fmri_create(mod, pfmri, version, name, inst, auth, part,
	    rev, serial);
	if (*out == NULL)
		return (-1);
	return (0);
}

struct hc_walk {
	topo_mod_walk_cb_t hcw_cb;
	void *hcw_priv;
	topo_walk_t *hcw_wp;
	nvlist_t **hcw_list;
	nvlist_t *hcw_fmri;
	nvlist_t *hcw_fac;
	uint_t hcw_index;
	uint_t hcw_end;
};

/*ARGSUSED*/
static int
hc_match(topo_mod_t *mod, tnode_t *node, nvlist_t *fmri, const char *name,
    topo_instance_t inst, boolean_t *result)
{
	nvlist_t *rsrc = NULL, *a1, *a2;
	char *c1, *c2;
	int err;

	if (strcmp(name, topo_node_name(node)) != 0)
		*result = B_FALSE;
	else if (strcmp(name, SES_ENCLOSURE) == 0 &&
	    topo_node_resource(node, &rsrc, &err) == 0 &&
	    nvlist_lookup_nvlist(rsrc, FM_FMRI_AUTHORITY, &a1) == 0 &&
	    nvlist_lookup_nvlist(fmri, FM_FMRI_AUTHORITY, &a2) == 0 &&
	    nvlist_lookup_string(a1, FM_FMRI_AUTH_CHASSIS, &c1) == 0 &&
	    nvlist_lookup_string(a2, FM_FMRI_AUTH_CHASSIS, &c2) == 0)
		/*
		 * Still need to check chassis serial in case of proxied
		 * ereports.
		 */
		*result = (strcmp(c1, c2) == 0);
	else
		*result = (inst == topo_node_instance(node));
	if (rsrc)
		nvlist_free(rsrc);
	return (0);
}

/*
 * Ideally, we should just be able to call topo_walk_bysibling().  But that
 * code assumes that the name/instance pair will match, so we need to
 * explicitly iterate over children of the parent looking for a matching value.
 */
static int
hc_walk_sibling(topo_mod_t *mod, tnode_t *node, struct hc_walk *hwp,
    const char *name, topo_instance_t inst)
{
	tnode_t *pnp = topo_node_parent(node);
	topo_walk_t *wp = hwp->hcw_wp;
	tnode_t *np;
	boolean_t matched;
	int status;

	for (np = topo_child_first(pnp); np != NULL;
	    np = topo_child_next(pnp, np)) {
		topo_node_hold(np);
		if (hc_match(mod, np, hwp->hcw_fmri, name, inst,
		    &matched) == 0 && matched) {
			wp->tw_node = np;
			if (wp->tw_mod != NULL)
				status = wp->tw_cb(mod, np, hwp);
			else
				status = wp->tw_cb(wp->tw_thp, np, hwp);
			topo_node_rele(np);
			wp->tw_node = node;
			return (status);
		}

		topo_node_rele(np);
	}

	return (TOPO_WALK_TERMINATE);
}

/*
 * Generic walker for the hc-scheme topo tree.  This function uses the
 * hierachical nature of the hc-scheme to efficiently step through
 * the topo hc tree.  Node lookups are done by topo_walk_byid() and
 * topo_walk_bysibling()  at each component level to avoid unnecessary
 * traversal of the tree.  hc_walker() never returns TOPO_WALK_NEXT, so
 * whether TOPO_WALK_CHILD or TOPO_WALK_SIBLING is specified by
 * topo_walk_step() doesn't affect the traversal.
 */
static int
hc_walker(topo_mod_t *mod, tnode_t *node, void *pdata)
{
	int i, err;
	struct hc_walk *hwp = (struct hc_walk *)pdata;
	char *name, *id;
	char *fname, *ftype;
	topo_instance_t inst;
	boolean_t match;

	i = hwp->hcw_index;
	if (i > hwp->hcw_end) {
		if (hwp->hcw_fac != NULL) {
			if ((err = hwp->hcw_cb(mod, node, hwp->hcw_priv))
			    != 0) {
				(void) topo_mod_seterrno(mod, err);
				topo_mod_dprintf(mod, "hc_walker: callback "
				    "failed: %s\n ", topo_mod_errmsg(mod));
				return (TOPO_WALK_ERR);
			}
			topo_mod_dprintf(mod, "hc_walker: callback "
			    "complete: terminate walk\n");
			return (TOPO_WALK_TERMINATE);
		} else {
			topo_mod_dprintf(mod, "hc_walker: node not found\n");
			return (TOPO_WALK_TERMINATE);
		}
	}

	err = nvlist_lookup_string(hwp->hcw_list[i], FM_FMRI_HC_NAME, &name);
	err |= nvlist_lookup_string(hwp->hcw_list[i], FM_FMRI_HC_ID, &id);

	if (err != 0) {
		(void) topo_mod_seterrno(mod, EMOD_NVL_INVAL);
		return (TOPO_WALK_ERR);
	}

	inst = atoi(id);

	/*
	 * Check to see if our node matches the requested FMRI.  If it doesn't
	 * (because the enumerator determines matching based on something other
	 * than name/instance, or because we're at the first level below the
	 * root), then iterate over siblings to find the matching node.
	 */
	if (hc_match(mod, node, hwp->hcw_fmri, name, inst, &match) != 0)
		return (TOPO_WALK_ERR);

	if (!match)
		return (hc_walk_sibling(mod, node, hwp, name, inst));

	topo_mod_dprintf(mod, "hc_walker: walking node:%s=%d for hc:"
	    "%s=%d at %d, end at %d \n", topo_node_name(node),
	    topo_node_instance(node), name, inst, i, hwp->hcw_end);

	if (i == hwp->hcw_end) {

		/*
		 * We are at the end of the hc-list.  Now, check for
		 * a facility leaf and walk one more time.
		 */
		if (hwp->hcw_fac != NULL) {
			err = nvlist_lookup_string(hwp->hcw_fac,
			    FM_FMRI_FACILITY_NAME, &fname);
			err |= nvlist_lookup_string(hwp->hcw_fac,
			    FM_FMRI_FACILITY_TYPE, &ftype);
			if (err != 0) {
				(void) topo_mod_seterrno(mod, EMOD_NVL_INVAL);
				return (TOPO_WALK_ERR);
			}
			hwp->hcw_index++;
			topo_mod_dprintf(mod, "hc_walker: walk to facility "
			    "node:%s=%s\n", fname, ftype);
			return (topo_walk_byid(hwp->hcw_wp, fname, 0));
		}

		/*
		 * Otherwise, this is the node we're looking for.
		 */
		if ((err = hwp->hcw_cb(mod, node, hwp->hcw_priv)) != 0) {
			(void) topo_mod_seterrno(mod, err);
			topo_mod_dprintf(mod, "hc_walker: callback "
			    "failed: %s\n ", topo_mod_errmsg(mod));
			return (TOPO_WALK_ERR);
		} else {
			topo_mod_dprintf(mod, "hc_walker: callback "
			    "complete: terminate walk\n");
			return (TOPO_WALK_TERMINATE);
		}
	}

	/*
	 * Move on to the next component in the hc-list
	 */
	hwp->hcw_index = ++i;
	err = nvlist_lookup_string(hwp->hcw_list[i], FM_FMRI_HC_NAME, &name);
	err |= nvlist_lookup_string(hwp->hcw_list[i], FM_FMRI_HC_ID, &id);
	if (err != 0) {
		(void) topo_mod_seterrno(mod, err);
		return (TOPO_WALK_ERR);
	}
	inst = atoi(id);

	return (topo_walk_byid(hwp->hcw_wp, name, inst));

}

/*
 * A call to hc_walk_init() followed by a call to topo_walk_step() will simply
 * find the specified hc-scheme node "rsrc" and run the callback function "cb"
 * on it with argument "pdata".
 *
 * Note that this sequence does not walk the whole tree but simply matches
 * each element in the hc-scheme name "rsrc" one level at a time until it
 * finds the "rsrc" node.
 */
static struct hc_walk *
hc_walk_init(topo_mod_t *mod, tnode_t *node, nvlist_t *rsrc,
    topo_mod_walk_cb_t cb, void *pdata)
{
	int err, ret;
	uint_t sz;
	struct hc_walk *hwp;
	topo_walk_t *wp;

	if ((hwp = topo_mod_alloc(mod, sizeof (struct hc_walk))) == NULL) {
		(void) topo_mod_seterrno(mod, EMOD_NOMEM);
		return (NULL);
	}

	if (nvlist_lookup_nvlist_array(rsrc, FM_FMRI_HC_LIST, &hwp->hcw_list,
	    &sz) != 0) {
		topo_mod_dprintf(mod, "hc_walk_init: failed to lookup %s "
		    "nvlist\n", FM_FMRI_HC_LIST);
		topo_mod_free(mod, hwp, sizeof (struct hc_walk));
		(void) topo_mod_seterrno(mod, EMOD_METHOD_INVAL);
		return (NULL);
	}
	if ((ret = nvlist_lookup_nvlist(rsrc, FM_FMRI_FACILITY, &hwp->hcw_fac))
	    != 0) {
		if (ret != ENOENT) {
			topo_mod_dprintf(mod, "hc_walk_init: unexpected error "
			    "looking up %s nvlist", FM_FMRI_FACILITY);
			topo_mod_free(mod, hwp, sizeof (struct hc_walk));
			(void) topo_mod_seterrno(mod, EMOD_METHOD_INVAL);
			return (NULL);
		} else {
			hwp->hcw_fac = NULL;
		}
	}

	hwp->hcw_fmri = rsrc;
	hwp->hcw_end = sz - 1;
	hwp->hcw_index = 0;
	hwp->hcw_priv = pdata;
	hwp->hcw_cb = cb;
	if ((wp = topo_mod_walk_init(mod, node, hc_walker, (void *)hwp, &err))
	    == NULL) {
		topo_mod_dprintf(mod, "hc_walk_init: topo_mod_walk_init failed "
		    "(%s)\n", topo_strerror(err));
		topo_mod_free(mod, hwp, sizeof (struct hc_walk));
		(void) topo_mod_seterrno(mod, err);
		return (NULL);
	}

	hwp->hcw_wp = wp;

	return (hwp);
}

struct prop_lookup {
	const char *pl_pgroup;
	const char *pl_pname;
	int pl_flag;
	nvlist_t *pl_args;
	nvlist_t *pl_rsrc;
	nvlist_t *pl_prop;
};

/*ARGSUSED*/
static int
hc_prop_get(topo_mod_t *mod, tnode_t *node, void *pdata)
{
	int err = 0;

	struct prop_lookup *plp = (struct prop_lookup *)pdata;

	(void) topo_prop_getprop(node, plp->pl_pgroup, plp->pl_pname,
	    plp->pl_args, &plp->pl_prop, &err);

	return (err);
}

static int
hc_fmri_prop_get(topo_mod_t *mod, tnode_t *node, topo_version_t version,
    nvlist_t *in, nvlist_t **out)
{
	int err;
	struct hc_walk *hwp;
	struct prop_lookup *plp;

	if (version > TOPO_METH_PROP_GET_VERSION)
		return (topo_mod_seterrno(mod, ETOPO_METHOD_VERNEW));

	if ((plp = topo_mod_alloc(mod, sizeof (struct prop_lookup))) == NULL)
		return (topo_mod_seterrno(mod, EMOD_NOMEM));

	err = nvlist_lookup_string(in, TOPO_PROP_GROUP,
	    (char **)&plp->pl_pgroup);
	err |= nvlist_lookup_string(in, TOPO_PROP_VAL_NAME,
	    (char **)&plp->pl_pname);
	err |= nvlist_lookup_nvlist(in, TOPO_PROP_RESOURCE, &plp->pl_rsrc);
	if (err != 0) {
		topo_mod_free(mod, plp, sizeof (struct prop_lookup));
		return (topo_mod_seterrno(mod, EMOD_METHOD_INVAL));
	}

	/*
	 * Private args to prop method are optional
	 */
	if ((err = nvlist_lookup_nvlist(in, TOPO_PROP_PARGS, &plp->pl_args))
	    != 0) {
		if (err != ENOENT) {
			topo_mod_free(mod, plp, sizeof (struct prop_lookup));
			return (topo_mod_seterrno(mod, EMOD_METHOD_INVAL));
		} else {
			plp->pl_args = NULL;
		}
	}

	plp->pl_prop = NULL;
	if ((hwp = hc_walk_init(mod, node, plp->pl_rsrc, hc_prop_get,
	    (void *)plp)) != NULL) {
		if (topo_walk_step(hwp->hcw_wp, TOPO_WALK_CHILD) ==
		    TOPO_WALK_ERR)
			err = -1;
		else
			err = 0;
		topo_walk_fini(hwp->hcw_wp);
		topo_mod_free(mod, hwp, sizeof (struct hc_walk));
	} else {
		err = -1;
	}

	if (plp->pl_prop != NULL)
		*out = plp->pl_prop;

	topo_mod_free(mod, plp, sizeof (struct prop_lookup));

	return (err);
}

/*ARGSUSED*/
static int
hc_pgrp_get(topo_mod_t *mod, tnode_t *node, void *pdata)
{
	int err = 0;

	struct prop_lookup *plp = (struct prop_lookup *)pdata;

	(void) topo_prop_getpgrp(node, plp->pl_pgroup, &plp->pl_prop, &err);

	return (err);
}

static int
hc_fmri_pgrp_get(topo_mod_t *mod, tnode_t *node, topo_version_t version,
    nvlist_t *in, nvlist_t **out)
{
	int err;
	struct hc_walk *hwp;
	struct prop_lookup *plp;

	if (version > TOPO_METH_PGRP_GET_VERSION)
		return (topo_mod_seterrno(mod, ETOPO_METHOD_VERNEW));

	if ((plp = topo_mod_alloc(mod, sizeof (struct prop_lookup))) == NULL)
		return (topo_mod_seterrno(mod, EMOD_NOMEM));

	err = nvlist_lookup_string(in, TOPO_PROP_GROUP,
	    (char **)&plp->pl_pgroup);
	err |= nvlist_lookup_nvlist(in, TOPO_PROP_RESOURCE, &plp->pl_rsrc);
	if (err != 0) {
		topo_mod_free(mod, plp, sizeof (struct prop_lookup));
		return (topo_mod_seterrno(mod, EMOD_METHOD_INVAL));
	}

	plp->pl_prop = NULL;
	if ((hwp = hc_walk_init(mod, node, plp->pl_rsrc, hc_pgrp_get,
	    (void *)plp)) != NULL) {
		if (topo_walk_step(hwp->hcw_wp, TOPO_WALK_CHILD) ==
		    TOPO_WALK_ERR)
			err = -1;
		else
			err = 0;
		topo_walk_fini(hwp->hcw_wp);
		topo_mod_free(mod, hwp, sizeof (struct hc_walk));
	} else {
		err = -1;
	}

	if (plp->pl_prop != NULL)
		*out = plp->pl_prop;

	topo_mod_free(mod, plp, sizeof (struct prop_lookup));

	return (err);
}

/*ARGSUSED*/
static int
hc_prop_setprop(topo_mod_t *mod, tnode_t *node, void *pdata)
{
	int err = 0;

	struct prop_lookup *plp = (struct prop_lookup *)pdata;

	(void) topo_prop_setprop(node, plp->pl_pgroup, plp->pl_prop,
	    plp->pl_flag, plp->pl_args, &err);

	return (err);
}

/*ARGSUSED*/
static int
hc_fmri_prop_set(topo_mod_t *mod, tnode_t *node, topo_version_t version,
    nvlist_t *in, nvlist_t **out)
{
	int err;
	struct hc_walk *hwp;
	struct prop_lookup *plp;

	if (version > TOPO_METH_PROP_SET_VERSION)
		return (topo_mod_seterrno(mod, ETOPO_METHOD_VERNEW));

	if ((plp = topo_mod_alloc(mod, sizeof (struct prop_lookup))) == NULL)
		return (topo_mod_seterrno(mod, EMOD_NOMEM));

	err = nvlist_lookup_string(in, TOPO_PROP_GROUP,
	    (char **)&plp->pl_pgroup);
	err |= nvlist_lookup_nvlist(in, TOPO_PROP_RESOURCE, &plp->pl_rsrc);
	err |= nvlist_lookup_nvlist(in, TOPO_PROP_VAL, &plp->pl_prop);
	err |= nvlist_lookup_int32(in, TOPO_PROP_FLAG, &plp->pl_flag);
	if (err != 0) {
		topo_mod_free(mod, plp, sizeof (struct prop_lookup));
		return (topo_mod_seterrno(mod, EMOD_METHOD_INVAL));
	}

	/*
	 * Private args to prop method are optional
	 */
	if ((err = nvlist_lookup_nvlist(in, TOPO_PROP_PARGS, &plp->pl_args))
	    != 0) {
		if (err != ENOENT)
			return (topo_mod_seterrno(mod, EMOD_METHOD_INVAL));
		else
			plp->pl_args = NULL;
	}

	if ((hwp = hc_walk_init(mod, node, plp->pl_rsrc, hc_prop_setprop,
	    (void *)plp)) != NULL) {
		if (topo_walk_step(hwp->hcw_wp, TOPO_WALK_CHILD) ==
		    TOPO_WALK_ERR)
			err = -1;
		else
			err = 0;
		topo_walk_fini(hwp->hcw_wp);
		topo_mod_free(mod, hwp, sizeof (struct hc_walk));
	} else {
		err = -1;
	}

	topo_mod_free(mod, plp, sizeof (struct prop_lookup));

	return (err);
}

struct hc_args {
	nvlist_t *ha_fmri;
	nvlist_t *ha_nvl;
	char *ha_method_name;
	topo_version_t ha_method_ver;
};

static int
hc_auth_changed(nvlist_t *nva, nvlist_t *nvb, const char *propname)
{
	char *stra, *strb;

	if (nvlist_lookup_string(nva, propname, &stra) != 0 ||
	    nvlist_lookup_string(nvb, propname, &strb) != 0)
		return (FMD_OBJ_STATE_UNKNOWN);

	if (strcmp(stra, strb) != 0)
		return (FMD_OBJ_STATE_REPLACED);
	else
		return (FMD_OBJ_STATE_STILL_PRESENT);
}

static int
hc_presence_state(topo_mod_t *mod, tnode_t *node, void *pdata)
{
	int err;
	struct hc_args *hap = (struct hc_args *)pdata;
	nvlist_t *rsrc;
	uint32_t rval = FMD_OBJ_STATE_UNKNOWN;

	/*
	 * check with the enumerator that created this FMRI
	 * (topo node)
	 */
	if (topo_method_invoke(node, TOPO_METH_PRESENCE_STATE,
	    TOPO_METH_PRESENCE_STATE_VERSION, hap->ha_fmri, &hap->ha_nvl,
	    &err) < 0) {
		char *stra, *strb;

		/*
		 * If the method exists but failed for some other
		 * reason, propagate the error as making any decision
		 * over presence is impossible.
		 */
		if (err != ETOPO_METHOD_NOTSUP)
			return (err);

		/*
		 * Enumerator didn't provide "presence_state" method -
		 * so check the authority information.  If there is a devid
		 * then compare that. Otherwise, if the part id
		 * or serial number doesn't match, then it isn't the
		 * same FMRI. Otherwise, if we have a serial number and
		 * it hasn't changed, then assume it is the same FMRI.
		 */
		if (topo_node_resource(node, &rsrc, &err) != 0)
			return (err);

		if (nvlist_lookup_string(hap->ha_fmri, FM_FMRI_HC_DEVID,
		    &stra) == 0 && nvlist_lookup_string(rsrc, FM_FMRI_HC_DEVID,
		    &strb) == 0) {
			if (devid_str_compare(stra, strb) != 0)
				rval = FMD_OBJ_STATE_REPLACED;
			else
				rval = FMD_OBJ_STATE_STILL_PRESENT;
		} else {
			rval = hc_auth_changed(hap->ha_fmri, rsrc,
			    FM_FMRI_HC_PART);
			if (rval != FMD_OBJ_STATE_REPLACED)
				rval = hc_auth_changed(hap->ha_fmri, rsrc,
				    FM_FMRI_HC_SERIAL_ID);
		}
		nvlist_free(rsrc);
		if (topo_mod_nvalloc(mod, &hap->ha_nvl,
		    NV_UNIQUE_NAME) != 0)
			return (EMOD_NOMEM);
		if (nvlist_add_uint32(hap->ha_nvl,
		    TOPO_METH_PRESENCE_STATE_RET, rval) != 0) {
			nvlist_free(hap->ha_nvl);
			hap->ha_nvl = NULL;
			return (ETOPO_PROP_NVL);
		}
	}
	return (0);
}

static int
hc_fmri_presence_state(topo_mod_t *mod, tnode_t *node, topo_version_t version,
    nvlist_t *in, nvlist_t **out)
{
	int err;
	struct hc_walk *hwp;
	struct hc_args *hap;
	uint8_t fmversion;

	if (nvlist_lookup_uint8(in, FM_VERSION, &fmversion) != 0 ||
	    fmversion > FM_HC_SCHEME_VERSION)
		return (topo_mod_seterrno(mod, EMOD_FMRI_MALFORM));

	if (version > TOPO_METH_PRESENCE_STATE_VERSION)
		return (topo_mod_seterrno(mod, ETOPO_METHOD_VERNEW));

	if ((hap = topo_mod_alloc(mod, sizeof (struct hc_args))) == NULL)
		return (topo_mod_seterrno(mod, EMOD_NOMEM));

	hap->ha_fmri = in;
	hap->ha_nvl = NULL;
	if ((hwp = hc_walk_init(mod, node, hap->ha_fmri, hc_presence_state,
	    (void *)hap)) != NULL) {
		if (topo_walk_step(hwp->hcw_wp, TOPO_WALK_CHILD) ==
		    TOPO_WALK_ERR)
			err = -1;
		else
			err = 0;
		topo_walk_fini(hwp->hcw_wp);
		topo_mod_free(mod, hwp, sizeof (struct hc_walk));
	} else {
		err = -1;
	}

	if (hap->ha_nvl != NULL)
		*out = hap->ha_nvl;

	topo_mod_free(mod, hap, sizeof (struct hc_args));

	return (err);
}

struct fac_lookup {
	const char *fl_fac_type;
	uint32_t fl_fac_subtype;
#ifdef _LP64
	uint64_t fl_callback;
	uint64_t fl_callback_args;
#else
	uint32_t fl_callback;
	uint32_t fl_callback_args;
#endif
	nvlist_t *fl_rsrc;
	nvlist_t *fl_fac_rsrc;
};

static int
hc_fac_get(topo_mod_t *mod, tnode_t *node, void *pdata)
{
	struct fac_lookup *flp = (struct fac_lookup *)pdata;
	topo_walk_cb_t cb = (topo_walk_cb_t)flp->fl_callback;
	topo_faclist_t faclist, *tmp;
	int err, ret = 0;

	/*
	 * Lookup the specified facility node.  Return with an error if we can't
	 * find it.
	 */
	if (topo_node_facility(mod->tm_hdl, node, flp->fl_fac_type,
	    flp->fl_fac_subtype, &faclist, &err) != 0) {
		topo_mod_dprintf(mod, "hc_fac_get: topo_node_facility "
		    "failed\n");
		return (TOPO_WALK_ERR);
	}

	/*
	 * Invoke user's callback for each facility node in the topo list,
	 * passing in a pointer to the facility node
	 */
	for (tmp = topo_list_next(&faclist.tf_list); tmp != NULL;
	    tmp = topo_list_next(tmp)) {

		if ((err = cb(mod->tm_hdl, tmp->tf_node,
		    (void *)flp->fl_callback_args)) != 0) {
			(void) topo_mod_seterrno(mod, err);
			topo_mod_dprintf(mod, "hc_fac_get: callback failed: "
			    "%s\n ", topo_mod_errmsg(mod));
			ret = TOPO_WALK_ERR;
			break;
		}
	}

	while ((tmp = topo_list_next(&faclist.tf_list)) != NULL) {
		topo_list_delete(&faclist.tf_list, tmp);
		topo_mod_free(mod, tmp, sizeof (topo_faclist_t));
	}
	return (ret);
}

static int
hc_fmri_facility(topo_mod_t *mod, tnode_t *node, topo_version_t version,
    nvlist_t *in, nvlist_t **out)
{
	int err = 0;
	struct hc_walk *hwp;
	struct fac_lookup *flp;

	if (version > TOPO_METH_FACILITY_VERSION)
		return (topo_mod_seterrno(mod, ETOPO_METHOD_VERNEW));

	if ((flp = topo_mod_alloc(mod, sizeof (struct fac_lookup))) == NULL)
		return (topo_mod_seterrno(mod, EMOD_NOMEM));

	/*
	 * lookup arguments: hw resource, facility type, facility subtype,
	 *  callback and callback args
	 */
	err = nvlist_lookup_nvlist(in, TOPO_PROP_RESOURCE, &flp->fl_rsrc);
	err |= nvlist_lookup_string(in, FM_FMRI_FACILITY_TYPE,
	    (char **)&flp->fl_fac_type);
	err |= nvlist_lookup_uint32(in, "type", &flp->fl_fac_subtype);
#ifdef _LP64
	err |= nvlist_lookup_uint64(in, "callback", &flp->fl_callback);
	err |= nvlist_lookup_uint64(in, "callback-args",
	    &flp->fl_callback_args);
#else
	err |= nvlist_lookup_uint32(in, "callback", &flp->fl_callback);
	err |= nvlist_lookup_uint32(in, "callback-args",
	    &flp->fl_callback_args);
#endif
	if (err != 0) {
		topo_mod_dprintf(mod, "hc_fmri_facility: failed to construct "
		    "walker arg nvlist\n");
		topo_mod_free(mod, flp, sizeof (struct fac_lookup));
		return (topo_mod_seterrno(mod, EMOD_METHOD_INVAL));
	}

	flp->fl_fac_rsrc = NULL;
	if ((hwp = hc_walk_init(mod, node, flp->fl_rsrc, hc_fac_get,
	    (void *)flp)) != NULL) {
		if (topo_walk_step(hwp->hcw_wp, TOPO_WALK_CHILD) ==
		    TOPO_WALK_ERR)
			err = -1;
		else
			err = 0;
		topo_walk_fini(hwp->hcw_wp);
		topo_mod_free(mod, hwp, sizeof (struct hc_walk));
	} else {
		topo_mod_dprintf(mod, "hc_fmri_facility: failed to initialize "
		    "hc walker\n");
		err = -1;
	}

	if (flp->fl_fac_rsrc != NULL)
		*out = flp->fl_fac_rsrc;

	topo_mod_free(mod, flp, sizeof (struct fac_lookup));

	return (err);
}

static int
hc_retire_subr(topo_mod_t *mod, tnode_t *node, void *pdata)
{
	int err, rc;
	struct hc_args *hap = (struct hc_args *)pdata;

	topo_mod_dprintf(mod, "hc_retire_subr: invoking method %s\n",
	    hap->ha_method_name);
	/*
	 * check with the enumerator that created this FMRI
	 * (topo node)
	 */
	rc = topo_method_invoke(node, hap->ha_method_name,
	    hap->ha_method_ver, hap->ha_fmri, &hap->ha_nvl, &err);

	topo_mod_dprintf(mod, "hc_retire_subr: invoking method %s "
	    "returned %d\n", hap->ha_method_name, rc);

	return (rc < 0 ? err : 0);
}

static int
hc_fmri_retire_subr(topo_mod_t *mod, tnode_t *node, char *method_name,
    topo_version_t builtin_version, topo_version_t version, nvlist_t *in,
    nvlist_t **out)
{
	int err;
	struct hc_walk *hwp;
	struct hc_args *hap;

	if (version > builtin_version)
		return (topo_mod_seterrno(mod, ETOPO_METHOD_VERNEW));

	if ((hap = topo_mod_alloc(mod, sizeof (struct hc_args))) == NULL)
		return (topo_mod_seterrno(mod, EMOD_NOMEM));

	hap->ha_fmri = in;
	hap->ha_nvl = NULL;
	hap->ha_method_name = method_name;
	hap->ha_method_ver = version;
	if ((hwp = hc_walk_init(mod, node, hap->ha_fmri, hc_retire_subr,
	    (void *)hap)) != NULL) {
		if (topo_walk_step(hwp->hcw_wp, TOPO_WALK_CHILD) ==
		    TOPO_WALK_ERR)
			err = -1;
		else
			err = 0;
		topo_walk_fini(hwp->hcw_wp);
	} else {
		err = -1;
	}

	topo_mod_free(mod, hwp, sizeof (struct hc_walk));

	if (hap->ha_nvl != NULL)
		*out = hap->ha_nvl;

	topo_mod_free(mod, hap, sizeof (struct hc_args));

	return (err);
}

static int
hc_fmri_retire(topo_mod_t *mod, tnode_t *node, topo_version_t version,
    nvlist_t *in, nvlist_t **out)
{
	return (hc_fmri_retire_subr(mod, node, TOPO_METH_RETIRE,
	    TOPO_METH_RETIRE_VERSION, version, in, out));
}

static int
hc_fmri_unretire(topo_mod_t *mod, tnode_t *node, topo_version_t version,
    nvlist_t *in, nvlist_t **out)
{
	return (hc_fmri_retire_subr(mod, node, TOPO_METH_UNRETIRE,
	    TOPO_METH_UNRETIRE_VERSION, version, in, out));
}

static int
hc_fmri_service_state(topo_mod_t *mod, tnode_t *node, topo_version_t version,
    nvlist_t *in, nvlist_t **out)
{
	uint8_t fmversion;

	if (nvlist_lookup_uint8(in, FM_VERSION, &fmversion) != 0 ||
	    fmversion > FM_HC_SCHEME_VERSION)
		return (topo_mod_seterrno(mod, EMOD_FMRI_MALFORM));

	return (hc_fmri_retire_subr(mod, node, TOPO_METH_SERVICE_STATE,
	    TOPO_METH_SERVICE_STATE_VERSION, version, in, out));
}

/*ARGSUSED*/
static int
hc_fmri_strhash_path(topo_mod_t *mod, tnode_t *node, topo_version_t version,
    nvlist_t *in, nvlist_t **out)
{
	char *next;
	char *fmri = (char *)in;
	uint64_t h;
	uint64_t *rval = (uint64_t *)(void *)out;

	h = topo_fmri_strhash_one(fmri, 5);
	next = strchr(fmri + 5, '/');
	if (next)
		h += topo_fmri_strhash_one(next, strlen(next));
	*rval = h;
	return (0);
}

/*ARGSUSED*/
static int
hc_fmri_strcmp_path(topo_mod_t *mod, tnode_t *node, topo_version_t version,
    nvlist_t *in, nvlist_t **out)
{
	char *a = (char *)in, *b = (char *)out;
	char *fmria, *fmrib;

	/*
	 * Get the portion of the FMRI independent of the authority
	 * information.
	 */
	fmria = strchr(a, '/');
	fmrib = strchr(b, '/');
	if (fmria == NULL || fmrib == NULL)
		return (strcmp(a, b) == 0);
	else
		return (strcmp(fmria, fmrib) == 0);
}

/*ARGSUSED*/
static int
hc_fmri_strcmp_ident(topo_mod_t *mod, tnode_t *node, topo_version_t version,
    nvlist_t *in, nvlist_t **out)
{
	char *a = (char *)in, *b = (char *)out;
	char *fa, *fb, *ffa, *ffb, bufa[PATH_MAX], bufb[PATH_MAX];
	int lena = -1, lenb = -1;

	fa = strstr(a, ":" FM_FMRI_HC_DEVID);
	fb = strstr(b, ":" FM_FMRI_HC_DEVID);
	if (fa != NULL && fb != NULL) {
		/* if we have devid, check that in preference */
		fa += strlen(":" FM_FMRI_HC_DEVID "=");
		fb += strlen(":" FM_FMRI_HC_DEVID "=");
		(void) strlcpy(bufa, fa, sizeof (bufa));
		(void) strlcpy(bufb, fb, sizeof (bufb));
		if ((fa = strchr(bufa, ':')) != NULL ||
		    (fa = strchr(bufa, '/')) != NULL)
			*fa++ = '\0';
		if ((fb = strchr(bufb, ':')) != NULL ||
		    (fb = strchr(bufb, '/')) != NULL)
			*fb++ = '\0';
		return (devid_str_compare(bufa, bufb) == 0);
	}
	fa = strstr(a, ":" FM_FMRI_HC_PART);
	fb = strstr(b, ":" FM_FMRI_HC_PART);
	if (fa != NULL && fb != NULL) {
		ffa = ++fa;
		ffb = ++fb;
		if ((fa = strchr(ffa, ':')) != NULL ||
		    (fa = strchr(ffa, '/')) != NULL)
			lena = fa++ - ffa;
		if ((fb = strchr(ffb, ':')) != NULL ||
		    (fb = strchr(ffb, '/')) != NULL)
			lenb = fb++ - ffb;
		if (lena != lenb)
			return (B_FALSE);
		if (strncmp(ffa, ffb, lena) != 0)
			return (B_FALSE);
	} else if (fa != NULL || fb != NULL) {
		return (B_FALSE);
	}

	fa = strstr(a, ":" FM_FMRI_HC_SERIAL_ID);
	fb = strstr(b, ":" FM_FMRI_HC_SERIAL_ID);
	if (fa != NULL && fb != NULL) {
		ffa = ++fa;
		ffb = ++fb;
		if ((fa = strchr(ffa, ':')) != NULL ||
		    (fa = strchr(ffa, '/')) != NULL)
			lena = fa++ - ffa;
		if ((fb = strchr(ffb, ':')) != NULL ||
		    (fb = strchr(ffb, '/')) != NULL)
			lenb = fb++ - ffb;
		if (lena != lenb)
			return (B_FALSE);
		return (strncmp(ffa, ffb, lena) == 0);
	} else if (fa != NULL || fb != NULL) {
		return (B_FALSE);
	}

	fa = strstr(a, ":" FM_FMRI_HC_PARENT_SERIAL);
	fb = strstr(b, ":" FM_FMRI_HC_PARENT_SERIAL);
	if (fa != NULL && fb != NULL) {
		ffa = ++fa;
		ffb = ++fb;
		if ((fa = strchr(ffa, ':')) != NULL ||
		    (fa = strchr(ffa, '/')) != NULL)
			lena = fa++ - ffa;
		if ((fb = strchr(ffb, ':')) != NULL ||
		    (fb = strchr(ffb, '/')) != NULL)
			lenb = fb++ - ffb;
		if (lena != lenb)
			return (B_FALSE);
		return (strncmp(ffa, ffb, lena) == 0);
	} else if (fa != NULL || fb != NULL) {
		return (B_FALSE);
	}
	return (B_TRUE);
}

/*ARGSUSED*/
static int
hc_ident_node(topo_mod_t *mod, tnode_t *node, void *pdata)
{
	int err;
	tnode_t *pnp = node;
	struct hc_args *hap = (struct hc_args *)pdata;
	nvlist_t *rsrc;
	char *ptr;

	while (pnp != NULL && topo_node_resource(pnp, &rsrc, &err) == 0) {
		if (nvlist_lookup_string(rsrc, FM_FMRI_HC_SERIAL_ID,
		    &ptr) == 0 ||
		    nvlist_lookup_string(rsrc, FM_FMRI_HC_DEVID, &ptr) == 0) {
			hap->ha_nvl = rsrc;
			return (0);
		} else
			nvlist_free(rsrc);
		pnp = topo_node_parent(pnp);
	}
	return (0);
}

/*ARGSUSED*/
static int
hc_fmri_ident_node(topo_mod_t *mod, tnode_t *node, topo_version_t version,
    nvlist_t *in, nvlist_t **out)
{
	int err;
	struct hc_walk *hwp;
	struct hc_args *hap;
	nvlist_t *rsrc;

	if (version > TOPO_METH_IDENT_NODE_VERSION)
		return (topo_mod_seterrno(mod, ETOPO_METHOD_VERNEW));

	err = nvlist_lookup_nvlist(in, TOPO_PROP_RESOURCE, &rsrc);
	if (err != 0)
		return (topo_mod_seterrno(mod, EMOD_METHOD_INVAL));

	if ((hap = topo_mod_alloc(mod, sizeof (struct hc_args))) == NULL)
		return (topo_mod_seterrno(mod, EMOD_NOMEM));

	hap->ha_nvl = NULL;
	if ((hwp = hc_walk_init(mod, node, rsrc, hc_ident_node,
	    (void *)hap)) != NULL) {
		if (topo_walk_step(hwp->hcw_wp, TOPO_WALK_CHILD) ==
		    TOPO_WALK_ERR)
			err = -1;
		else
			err = 0;
		topo_walk_fini(hwp->hcw_wp);
	} else {
		err = -1;
	}

	topo_mod_free(mod, hwp, sizeof (struct hc_walk));

	if (hap->ha_nvl != NULL)
		*out = hap->ha_nvl;

	topo_mod_free(mod, hap, sizeof (struct hc_args));

	return (err);
}
