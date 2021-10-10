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
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/fm/protocol.h>
#include <uuid/uuid.h>

#include <dirent.h>
#include <limits.h>
#include <unistd.h>
#include <alloca.h>
#include <stddef.h>
#include <fm/libtopo.h>

#include <fmd_alloc.h>
#include <fmd_string.h>
#include <fmd_error.h>
#include <fmd_subr.h>
#include <fmd_protocol.h>
#include <fmd_event.h>
#include <fmd_conf.h>
#include <fmd_fmri.h>
#include <fmd_dispq.h>
#include <fmd_case.h>
#include <fmd_module.h>
#include <fmd_asru.h>

#include <fmd.h>

static const char *const _fmd_asru_events[] = {
	FMD_RSRC_CLASS "asru.ok",		/* UNUSABLE=0 FAULTED=0 */
	FMD_RSRC_CLASS "asru.degraded",		/* UNUSABLE=0 FAULTED=1 */
	FMD_RSRC_CLASS "asru.unknown",		/* UNUSABLE=1 FAULTED=0 */
	FMD_RSRC_CLASS "asru.faulted"		/* UNUSABLE=1 FAULTED=1 */
};

static const char *const _fmd_asru_snames[] = {
	"uf", "uF", "Uf", "UF"			/* same order as above */
};

volatile uint32_t fmd_asru_fake_not_present = 0;

static void fmd_asru_do_delete_entry(fmd_asru_hash_t *ahp, fmd_case_t *cp,
    fmd_asru_link_t **hash, size_t next_offset, char *name);

static uint_t
fmd_asru_strhash(fmd_asru_hash_t *ahp, const char *val)
{
	return (topo_fmri_strhash(ahp->ah_topo->ft_hdl, val) % ahp->ah_hashlen);
}

#define	HC_ONLY_FALSE	0
#define	HC_ONLY_TRUE	1

static int
fmd_asru_replacement_state(nvlist_t *event, int hc_only)
{
	int ps = -1;
	nvlist_t *fru, *ident_node;
	char *s;

	/*
	 * Check if there is evidence that this object is no longer present.
	 * fmd_fmri_presence_state() should be supported on frus, as those are
	 * the things that are physically present or not present - an asru can
	 * be spread over a number of frus some of which are present and some
	 * not, so fmd_fmri_presence_state() is not meaningful. If we have
	 * checked and we get -1 then we don't know whether it's present or not,
	 * so err on the safe side and treat it as still present.
	 *
	 * Note that if hc_only is set, then we only check status using fmris
	 * that are in hc-scheme.
	 *
	 * We may need to check ident_node as well as it may be different from
	 * the fru node.
	 */
	if (fmd_asru_fake_not_present)
		return (fmd_asru_fake_not_present);

	if (nvlist_lookup_nvlist(event, FM_FAULT_FRU, &fru) == 0) {
		if ((hc_only == HC_ONLY_FALSE ||
		    (nvlist_lookup_string(fru, FM_FMRI_SCHEME, &s) == 0 &&
		    strcmp(s, FM_FMRI_SCHEME_HC) == 0)))
			ps = fmd_fmri_presence_state(fru);
	}
	if (ps == -1)
		return (FMD_OBJ_STATE_UNKNOWN);
	if (ps == FMD_OBJ_STATE_UNKNOWN && nvlist_lookup_nvlist(event,
	    FM_FAULT_IDENT_NODE, &ident_node) == 0)
		ps = fmd_fmri_presence_state(ident_node);
	return (ps);
}

static void
fmd_asru_asru_hash_insert(fmd_asru_hash_t *ahp, fmd_asru_link_t *alp,
    char *name)
{
	uint_t h = fmd_asru_strhash(ahp, name);

	ASSERT(MUTEX_HELD(&ahp->ah_lock));
	alp->al_asru_next = ahp->ah_asru_hash[h];
	ahp->ah_asru_hash[h] = alp;
}

static void
fmd_asru_case_hash_insert(fmd_asru_hash_t *ahp, fmd_asru_link_t *alp,
    char *name)
{
	uint_t h = fmd_asru_strhash(ahp, name);

	ASSERT(MUTEX_HELD(&ahp->ah_lock));
	alp->al_case_next = ahp->ah_case_hash[h];
	ahp->ah_case_hash[h] = alp;
	ahp->ah_al_count++;
}

static void
fmd_asru_fru_hash_insert(fmd_asru_hash_t *ahp, fmd_asru_link_t *alp, char *name)
{
	uint_t h = fmd_asru_strhash(ahp, name);

	ASSERT(MUTEX_HELD(&ahp->ah_lock));
	alp->al_fru_next = ahp->ah_fru_hash[h];
	ahp->ah_fru_hash[h] = alp;
}

static void
fmd_asru_label_hash_insert(fmd_asru_hash_t *ahp, fmd_asru_link_t *alp,
    char *name)
{
	uint_t h = fmd_asru_strhash(ahp, name);

	ASSERT(MUTEX_HELD(&ahp->ah_lock));
	alp->al_label_next = ahp->ah_label_hash[h];
	ahp->ah_label_hash[h] = alp;
}

static void
fmd_asru_rsrc_hash_insert(fmd_asru_hash_t *ahp, fmd_asru_link_t *alp,
    char *name)
{
	uint_t h = fmd_asru_strhash(ahp, name);

	ASSERT(MUTEX_HELD(&ahp->ah_lock));
	alp->al_rsrc_next = ahp->ah_rsrc_hash[h];
	ahp->ah_rsrc_hash[h] = alp;
}

static void
fmd_asru_al_destroy(fmd_asru_link_t *alp)
{
	fmd_strfree(alp->al_uuid);
	fmd_strfree(alp->al_root);
	nvlist_free(alp->al_event);
	fmd_strfree(alp->al_rsrc_name);
	fmd_strfree(alp->al_case_uuid);
	fmd_strfree(alp->al_fru_name);
	fmd_strfree(alp->al_asru_name);
	fmd_strfree(alp->al_label);
	fmd_free(alp, sizeof (fmd_asru_link_t));
}

static int
fmd_asru_get_namestr(nvlist_t *nvl, char **name, ssize_t *namelen)
{
	if ((*namelen = fmd_fmri_nvl2str(nvl, NULL, 0)) == -1)
		return (EFMD_ASRU_FMRI);
	*name = fmd_alloc(*namelen + 1, FMD_SLEEP);
	if (fmd_fmri_nvl2str(nvl, *name, *namelen + 1) == -1) {
		if (*name != NULL)
			fmd_free(*name, *namelen + 1);
		return (EFMD_ASRU_FMRI);
	}
	return (0);
}

static fmd_asru_link_t *
fmd_asru_al_create(fmd_asru_hash_t *ahp, nvlist_t *nvl, fmd_case_t *cp,
    const char *al_uuid)
{
	nvlist_t *asru = NULL, *fru, *rsrc;
	int got_rsrc = 0, got_asru = 0, got_fru = 0;
	ssize_t fru_namelen, rsrc_namelen, asru_namelen;
	char *asru_name, *rsrc_name, *fru_name, *label;
	fmd_asru_link_t *alp;
	boolean_t msg;
	fmd_case_impl_t *cip = (fmd_case_impl_t *)cp;

	if (nvlist_lookup_nvlist(nvl, FM_FAULT_ASRU, &asru) == 0 &&
	    fmd_asru_get_namestr(asru, &asru_name, &asru_namelen) == 0)
		got_asru = 1;
	if (nvlist_lookup_nvlist(nvl, FM_FAULT_FRU, &fru) == 0 &&
	    fmd_asru_get_namestr(fru, &fru_name, &fru_namelen) == 0)
		got_fru = 1;
	if (nvlist_lookup_nvlist(nvl, FM_FAULT_RESOURCE, &rsrc) == 0 &&
	    fmd_asru_get_namestr(rsrc, &rsrc_name, &rsrc_namelen) == 0)
		got_rsrc = 1;
	if (nvlist_lookup_string(nvl, FM_FAULT_LOCATION, &label) != 0)
		label = "";

	/*
	 * Create and initialise the per-fault "link" structure.
	 */
	alp = fmd_zalloc(sizeof (fmd_asru_link_t), FMD_SLEEP);
	alp->al_uuid = fmd_strdup(al_uuid, FMD_SLEEP);
	alp->al_root = fmd_strdup(ahp->ah_dirpath, FMD_SLEEP);
	(void) pthread_mutex_init(&alp->al_lock, NULL);
	alp->al_asru_name = got_asru ? asru_name : fmd_strdup("", FMD_SLEEP);
	alp->al_fru_name = got_fru ? fru_name : fmd_strdup("", FMD_SLEEP);
	alp->al_rsrc_name = got_rsrc ? rsrc_name : fmd_strdup("", FMD_SLEEP);
	alp->al_label = fmd_strdup(label, FMD_SLEEP);
	alp->al_case_uuid = fmd_strdup(cip->ci_uuid, FMD_SLEEP);
	alp->al_case = cp;
	if (nvlist_lookup_boolean_value(nvl, FM_SUSPECT_MESSAGE, &msg) == 0 &&
	    msg == B_FALSE)
		alp->al_flags |= FMD_ASRU_INVISIBLE;
	alp->al_event = nvl;

	/*
	 * Put the link structure on the various hashes.
	 */
	(void) pthread_mutex_lock(&ahp->ah_lock);
	fmd_asru_asru_hash_insert(ahp, alp, alp->al_asru_name);
	fmd_asru_fru_hash_insert(ahp, alp, alp->al_fru_name);
	fmd_asru_rsrc_hash_insert(ahp, alp, alp->al_rsrc_name);
	fmd_asru_label_hash_insert(ahp, alp, label);
	fmd_asru_case_hash_insert(ahp, alp, cip->ci_uuid);
	(void) pthread_mutex_unlock(&ahp->ah_lock);
	return (alp);
}

/*
 * If we have a "fru" and it has a serial then set "serial" to that, else if we
 * have an "ident_node" and it has a serial then set "parent-serial" to that.
 */
void
fmd_asru_set_serial(topo_hdl_t *thp, nvlist_t *fru, nvlist_t *ident_node)
{
	char *serial = NULL;
	int err;

	if (fru && topo_fmri_serial(thp, fru, &serial, &err) == 0)
		(void) nvlist_add_string(fru, FM_FMRI_HC_SERIAL_ID, serial);
	else if (fru && ident_node &&
	    topo_fmri_serial(thp, ident_node, &serial, &err) == 0)
		(void) nvlist_add_string(fru, FM_FMRI_HC_PARENT_SERIAL, serial);
	topo_hdl_strfree(thp, serial);
}

struct find_ident {
	nvlist_t **identp;
	char *old_identstr;
	int count;
};

/*ARGSUSED*/
static int
fmd_asru_find_ident(topo_hdl_t *thp, tnode_t *node, void *arg)
{
	struct find_ident *ffp = (struct find_ident *)arg;
	nvlist_t *rsrc = NULL;
	char *rsrcstr = NULL;
	int err;

	if (topo_node_flags(node) == TOPO_NODE_FACILITY)
		return (TOPO_WALK_NEXT);
	if (topo_node_resource(node, &rsrc, &err) != 0)
		return (TOPO_WALK_NEXT);
	(void) topo_fmri_nvl2str(thp, rsrc, &rsrcstr, &err);
	if (rsrcstr == NULL ||
	    !topo_fmri_strcmp_ident(thp, rsrcstr, ffp->old_identstr)) {
		topo_hdl_strfree(thp, rsrcstr);
		nvlist_free(rsrc);
		return (TOPO_WALK_NEXT);
	}
	topo_hdl_strfree(thp, rsrcstr);
	if (ffp->count++ == 0)
		*ffp->identp = rsrc;
	return (TOPO_WALK_NEXT);
}

void
fmd_asru_update_fault(nvlist_t *flt, char **asrustrp, char **frustrp,
    char **rsrcstrp, char **locp)
{
	topo_hdl_t *thp;
	nvlist_t *rsrc = NULL, *fru = NULL, *old_fru = NULL, *old_rsrc = NULL;
	nvlist_t *asru = NULL, *old_asru = NULL, *old_ident = NULL;
	nvlist_t *ident = NULL, *tmp = NULL, *prop = NULL, *tmp2 = NULL;
	int err;
	char *scheme = NULL, *class;
	char *asrustr = NULL, *rsrcstr = NULL, *frustr = NULL, *loc = NULL;
	char *old_asrustr = NULL, *old_rsrcpath = NULL, *old_identpath = NULL;
	char *old_identstr = NULL, *identstr = NULL;
	topo_walk_t *twp;
	struct find_ident ff;
	char *ptr;

	/*
	 * Only do update for faults with hc-scheme resource.
	 */
	if (nvlist_lookup_string(flt, FM_CLASS, &class) != 0 ||
	    strncmp(class, FM_FAULT_CLASS, strlen(FM_FAULT_CLASS)) != 0)
		return;
	if (nvlist_lookup_nvlist(flt, FM_FAULT_RESOURCE, &old_rsrc) != 0)
		return;
	(void) nvlist_lookup_string(old_rsrc, FM_FMRI_SCHEME, &scheme);
	if (!scheme || strcmp(scheme, FM_FMRI_SCHEME_HC) != 0)
		return;

	/*
	 * Look up the original fru/asru/etc, then snapshot the latest topo.
	 */
	(void) nvlist_lookup_nvlist(flt, FM_FAULT_FRU, &old_fru);
	(void) nvlist_lookup_nvlist(flt, FM_FAULT_ASRU, &old_asru);
	(void) nvlist_lookup_nvlist(flt, FM_FAULT_IDENT_NODE, &old_ident);
	thp = fmd_fmri_topo_hold(TOPO_VERSION);
	if (old_ident) {
		(void) topo_fmri_nvl2str(thp, old_ident, &old_identstr, &err);
		(void) nvlist_xdup(old_ident, &tmp, &fmd.d_nva);
		if (tmp) {
			(void) nvlist_remove(tmp, FM_FMRI_HC_SERIAL_ID,
			    DATA_TYPE_STRING);
			(void) nvlist_remove(tmp, FM_FMRI_HC_PART,
			    DATA_TYPE_STRING);
			(void) nvlist_remove(tmp, FM_FMRI_HC_REVISION,
			    DATA_TYPE_STRING);
			(void) nvlist_remove(tmp, FM_FMRI_HC_DEVID,
			    DATA_TYPE_STRING);
			(void) topo_fmri_nvl2str(thp, tmp, &old_identpath,
			    &err);
			nvlist_free(tmp);
		}
	}
	(void) nvlist_xdup(old_rsrc, &tmp, &fmd.d_nva);
	if (tmp) {
		(void) nvlist_remove(tmp, FM_FMRI_HC_SERIAL_ID,
		    DATA_TYPE_STRING);
		(void) nvlist_remove(tmp, FM_FMRI_HC_PART,
		    DATA_TYPE_STRING);
		(void) nvlist_remove(tmp, FM_FMRI_HC_REVISION,
		    DATA_TYPE_STRING);
		(void) nvlist_remove(tmp, FM_FMRI_HC_PARENT_SERIAL,
		    DATA_TYPE_STRING);
		(void) nvlist_remove(tmp, FM_FMRI_HC_DEVID,
		    DATA_TYPE_STRING);
		(void) topo_fmri_nvl2str(thp, tmp, &old_rsrcpath, &err);
		nvlist_free(tmp);
	}

	/*
	 * If the fault has a rsrc and an ident node, and the rsrc is the same
	 * as (or an extended version of) the ident node, then it is identity
	 * based.
	 */
	if (!old_identpath || !old_rsrcpath ||
	    strncmp(old_identpath, old_rsrcpath, strlen(old_identpath)) != 0) {
		/*
		 * This is a location-based fault. If the old asru did not
		 * exist or was not in hc scheme, then recalculate asru from
		 * resource if present.
		 */
		topo_hdl_strfree(thp, old_identpath);
		topo_hdl_strfree(thp, old_identstr);
		topo_hdl_strfree(thp, old_rsrcpath);
		if (old_asru && nvlist_lookup_string(old_asru, FM_FMRI_SCHEME,
		    &scheme) == 0 && strcmp(scheme, FM_FMRI_SCHEME_HC) == 0) {
			fmd_fmri_topo_rele(thp);
			return;
		}

		/*
		 * Get asru from rsrc using topo_fmri_asru() and put in fault.
		 */
		if (topo_fmri_asru(thp, old_rsrc, &asru, &err) == 0 &&
		    topo_fmri_nvl2str(thp, asru, &asrustr, &err) == 0) {
			if (old_asru)
				(void) topo_fmri_nvl2str(thp, old_asru,
				    &old_asrustr, &err);
			if (!old_asrustr || strcmp(asrustr, old_asrustr) != 0) {
				(void) nvlist_remove(flt, FM_FAULT_ASRU,
				    DATA_TYPE_NVLIST);
				(void) nvlist_add_nvlist(flt, FM_FAULT_ASRU,
				    asru);
				if (asrustrp)
					*asrustrp = fmd_strdup(asrustr,
					    FMD_SLEEP);
			}
		}

		/*
		 * Finally tidy up.
		 */
		topo_hdl_strfree(thp, asrustr);
		topo_hdl_strfree(thp, old_asrustr);
		if (asru)
			nvlist_free(asru);
		fmd_fmri_topo_rele(thp);
		return;
	}

	/*
	 * A call to topo_fmri_ident_node() will see if there is still a
	 * node with the same path as old_fru in the current topology, and
	 * return the fmri of its current ident_node if there is one.
	 * If this has the same identity as the old_identstr, the nothing
	 * has changed. This saves walking the whole tree below.
	 */
	(void) topo_fmri_ident_node(thp, old_fru, &ident, &err);
	if (ident) {
		(void) topo_fmri_nvl2str(thp, ident, &identstr, &err);
		if (identstr && strcmp(identstr, old_identstr) == 0) {
			topo_hdl_strfree(thp, identstr);
			topo_hdl_strfree(thp, old_identpath);
			topo_hdl_strfree(thp, old_identstr);
			topo_hdl_strfree(thp, old_rsrcpath);
			nvlist_free(ident);
			fmd_fmri_topo_rele(thp);
			return;
		}
		topo_hdl_strfree(thp, identstr);
		nvlist_free(ident);
		ident = NULL;
		identstr = NULL;
	}

	/*
	 * This is an identity-based fault. Find new location of the ident node.
	 */
	ff.old_identstr = old_identstr;
	ff.identp = &ident;
	ff.count = 0;
	if ((twp = topo_walk_init(thp, FM_FMRI_SCHEME_HC, fmd_asru_find_ident,
	    &ff, &err)) != NULL) {
		(void) topo_walk_step(twp, TOPO_WALK_CHILD);
		topo_walk_fini(twp);
	}
	if (ident)
		(void) topo_fmri_nvl2str(thp, ident, &identstr, &err);

	/*
	 * If no longer present in system, or ident location is unchanged,
	 * or there are multiple devices with the same ident, then just return.
	 */
	if (!identstr || strcmp(identstr, old_identstr) == 0 || ff.count > 1) {
		if (ff.count > 1)
			TRACE((FMD_DBG_ASRU,
			    "multiple resources with same ident"));
		topo_hdl_strfree(thp, identstr);
		topo_hdl_strfree(thp, old_identpath);
		topo_hdl_strfree(thp, old_identstr);
		topo_hdl_strfree(thp, old_rsrcpath);
		if (ident)
			nvlist_free(ident);
		fmd_fmri_topo_rele(thp);
		return;
	}

	/*
	 * Old rsrc was an extended version of the old ident node.
	 * Set new rsrc to be the same extension of the new ident node.
	 */
	topo_hdl_strfree(thp, old_identstr);
	ptr = old_rsrcpath + strlen(old_identpath);
	rsrcstr = topo_hdl_alloc(thp, strlen(identstr) + strlen(ptr) + 1);
	(void) strcpy(rsrcstr, identstr);
	if (strlen(ptr) != 0)
		(void) strcpy(rsrcstr + strlen(identstr), ptr);
	topo_hdl_strfree(thp, old_identpath);
	topo_hdl_strfree(thp, old_rsrcpath);
	tmp = NULL;
	if (topo_fmri_str2nvl(thp, rsrcstr, &tmp, &err) != 0 ||
	    topo_fmri_getprop(thp, tmp, TOPO_PGROUP_PROTOCOL,
	    TOPO_PROP_RESOURCE, NULL, &prop, &err) != 0 ||
	    nvlist_lookup_nvlist(prop, TOPO_PROP_VAL_VAL, &tmp2) != 0 ||
	    nvlist_xdup(tmp2, &rsrc, &fmd.d_nva) != 0) {
		if (tmp)
			nvlist_free(tmp);
		if (prop)
			nvlist_free(prop);
		topo_hdl_strfree(thp, rsrcstr);
		topo_hdl_strfree(thp, identstr);
		nvlist_free(ident);
		fmd_fmri_topo_rele(thp);
		return;
	}
	nvlist_free(tmp);
	nvlist_free(prop);

	/*
	 * Update resource in fault.
	 */
	(void) nvlist_add_nvlist(flt, FM_FAULT_RESOURCE, rsrc);
	if (rsrcstrp)
		*rsrcstrp = fmd_strdup(rsrcstr, FMD_SLEEP);
	topo_hdl_strfree(thp, rsrcstr);

	/*
	 * Update ident_node in fault.
	 */
	(void) nvlist_remove(flt, FM_FAULT_IDENT_NODE, DATA_TYPE_NVLIST);
	(void) nvlist_add_nvlist(flt, FM_FAULT_IDENT_NODE, ident);
	topo_hdl_strfree(thp, identstr);

	/*
	 * Now update fru, asru, and label, based on new rsrc.
	 */
	(void) topo_fmri_asru(thp, rsrc, &asru, &err);
	(void) topo_fmri_fru(thp, rsrc, &fru, &err);
	fmd_asru_set_serial(thp, fru, ident);
	if (fru) {
		(void) topo_fmri_label(thp, fru, &loc, &err);
		(void) topo_fmri_nvl2str(thp, fru, &frustr, &err);
	} else
		(void) topo_fmri_label(thp, rsrc, &loc, &err);
	if (asru)
		(void) topo_fmri_nvl2str(thp, asru, &asrustr, &err);
	(void) nvlist_remove(flt, FM_FAULT_FRU, DATA_TYPE_NVLIST);
	if (frustr) {
		(void) nvlist_add_nvlist(flt, FM_FAULT_FRU, fru);
		if (frustrp)
			*frustrp = fmd_strdup(frustr, FMD_SLEEP);
	}
	(void) nvlist_remove(flt, FM_FAULT_ASRU, DATA_TYPE_NVLIST);
	if (asrustr) {
		(void) nvlist_add_nvlist(flt, FM_FAULT_ASRU, asru);
		if (asrustrp)
			*asrustrp = fmd_strdup(asrustr, FMD_SLEEP);
	}
	(void) nvlist_remove(flt, FM_FAULT_LOCATION, DATA_TYPE_STRING);
	if (loc) {
		(void) nvlist_add_string(flt, FM_FAULT_LOCATION, loc);
		if (locp)
			*locp = fmd_strdup(loc, FMD_SLEEP);
	}

	/*
	 * Finally tidy up.
	 */
	topo_hdl_strfree(thp, frustr);
	topo_hdl_strfree(thp, asrustr);
	topo_hdl_strfree(thp, loc);
	if (fru)
		nvlist_free(fru);
	if (asru)
		nvlist_free(asru);
	nvlist_free(ident);
	nvlist_free(rsrc);
	fmd_fmri_topo_rele(thp);
}

static void
fmd_asru_hash_recreate(fmd_log_t *lp, fmd_event_t *ep, fmd_asru_hash_t *ahp)
{
	nvlist_t *nvl = FMD_EVENT_NVL(ep);
	boolean_t faulty = FMD_B_FALSE, unusable = FMD_B_FALSE;
	int ps;
	boolean_t repaired = FMD_B_FALSE, replaced = FMD_B_FALSE;
	boolean_t acquitted = FMD_B_FALSE, resolved = FMD_B_FALSE;
	nvlist_t *flt, *flt_copy, *asru;
	char *case_uuid = NULL, *case_code = NULL, *case_topo_uuid = NULL;
	fmd_asru_link_t *alp;
	fmd_case_t *cp;
	int64_t *diag_time;
	nvlist_t *de_fmri, *de_fmri_dup;
	uint_t nelem;
	boolean_t injected;

	/*
	 * Extract the most recent values of 'faulty' from the event log.
	 */
	if (nvlist_lookup_boolean_value(nvl, FM_RSRC_ASRU_FAULTY,
	    &faulty) != 0) {
		fmd_error(EFMD_ASRU_EVENT, "failed to reload asru %s: "
		    "invalid event log record\n", lp->log_name);
		ahp->ah_error = EFMD_ASRU_EVENT;
		return;
	}
	if (nvlist_lookup_nvlist(nvl, FM_RSRC_ASRU_EVENT, &flt) != 0) {
		fmd_error(EFMD_ASRU_EVENT, "failed to reload asru %s: "
		    "invalid event log record\n", lp->log_name);
		ahp->ah_error = EFMD_ASRU_EVENT;
		return;
	}
	(void) nvlist_lookup_string(nvl, FM_RSRC_ASRU_UUID, &case_uuid);
	(void) nvlist_lookup_string(nvl, FM_RSRC_ASRU_TOPO_UUID,
	    &case_topo_uuid);
	(void) nvlist_lookup_string(nvl, FM_RSRC_ASRU_CODE, &case_code);
	(void) nvlist_lookup_boolean_value(nvl, FM_RSRC_ASRU_UNUSABLE,
	    &unusable);
	(void) nvlist_lookup_boolean_value(nvl, FM_RSRC_ASRU_REPAIRED,
	    &repaired);
	(void) nvlist_lookup_boolean_value(nvl, FM_RSRC_ASRU_REPLACED,
	    &replaced);
	(void) nvlist_lookup_boolean_value(nvl, FM_RSRC_ASRU_ACQUITTED,
	    &acquitted);
	(void) nvlist_lookup_boolean_value(nvl, FM_RSRC_ASRU_RESOLVED,
	    &resolved);

	fmd_dprintf(FMD_DBG_ASRU, "%s: uuid=%s, topo_uuid=%s, code=%s, "
	    "unusable=%d, repaired=%d, replaced=%d, acquitted=%d, "
	    "resolved=%d\n", __func__, case_uuid, case_topo_uuid,
	    case_code, unusable, repaired, replaced, acquitted, resolved);

	/*
	 * Attempt to recreate the case in CLOSED, REPAIRED or RESOLVED state
	 * (depending on whether the faulty/resolved bits are set).
	 * If the case is already present, fmd_case_recreate() will return it.
	 * If not, we'll create a new orphaned case. Either way,  we use the
	 * ASRU event to insert a suspect into the partially-restored case.
	 */
	fmd_module_lock(fmd.d_rmod);
	cp = fmd_case_recreate(fmd.d_rmod, NULL, faulty ? FMD_CASE_CLOSED :
	    resolved ? FMD_CASE_RESOLVED : FMD_CASE_REPAIRED, case_uuid,
	    case_code, case_topo_uuid);
	fmd_case_hold(cp);
	fmd_module_unlock(fmd.d_rmod);
	if (nvlist_lookup_boolean_value(nvl, FM_SUSPECT_INJECTED,
	    &injected) == 0 && injected)
		fmd_case_set_injected(cp);
	if (nvlist_lookup_int64_array(nvl, FM_SUSPECT_DIAG_TIME, &diag_time,
	    &nelem) == 0 && nelem >= 2)
		fmd_case_settime(cp, diag_time[0], diag_time[1]);
	else
		fmd_case_settime(cp, lp->log_stat.st_ctime, 0);
	if (nvlist_lookup_nvlist(nvl, FM_SUSPECT_DE, &de_fmri) == 0) {
		(void) nvlist_xdup(de_fmri, &de_fmri_dup, &fmd.d_nva);
		fmd_case_set_de_fmri(cp, de_fmri_dup);
	}
	(void) nvlist_xdup(flt, &flt_copy, &fmd.d_nva);
	fmd_case_recreate_suspect(cp, flt_copy);

	/*
	 * Now create the resource cache entries.
	 */
	alp = fmd_asru_al_create(ahp, flt_copy, cp,
	    fmd_strbasename(lp->log_name));

	fmd_dprintf(FMD_DBG_ASRU, "%s: alp=0x%p, flags=0x%x, rsrc=%s, "
	    "asru=%s, fru=%s\n", __func__, (void *)alp, alp->al_flags,
	    alp->al_rsrc_name, alp->al_asru_name, alp->al_fru_name);

	/*
	 * Check to see if the resource is still present in the system.
	 */
	ps = fmd_asru_replacement_state(flt_copy, HC_ONLY_FALSE);
	fmd_dprintf(FMD_DBG_ASRU, "%s: replacement_state=%d\n", __func__, ps);
	if (ps == FMD_OBJ_STATE_STILL_PRESENT ||
	    ps == FMD_OBJ_STATE_UNKNOWN) {
		alp->al_flags |= FMD_ASRU_PRESENT;
		if (nvlist_lookup_nvlist(alp->al_event, FM_FAULT_ASRU,
		    &asru) == 0) {
			switch (fmd_fmri_service_state(asru)) {
			case FMD_SERVICE_STATE_UNUSABLE:
				unusable = FMD_B_TRUE;
				break;
			case FMD_SERVICE_STATE_OK:
			case FMD_SERVICE_STATE_ISOLATE_PENDING:
			case FMD_SERVICE_STATE_DEGRADED:
				unusable = FMD_B_FALSE;
				break;
			case FMD_SERVICE_STATE_UNKNOWN:
			case -1:
				unusable = FMD_B_FALSE;
				break;
			}
		}
		fmd_dprintf(FMD_DBG_ASRU, "%s: present and unusable=%s\n",
		    __func__, (unusable == FMD_B_TRUE) ? "true" : "false");
	}

	fmd_dprintf(FMD_DBG_ASRU, "%s: unusable=%d, repaired=%d, "
	    "replaced=%d, acquitted=%d, " "resolved=%d\n",
	    __func__, unusable, repaired, replaced, acquitted, resolved);

	if (faulty)
		alp->al_flags |= FMD_ASRU_FAULTY;
	if (unusable)
		alp->al_flags |= FMD_ASRU_UNUSABLE;
	if (replaced)
		alp->al_reason = FMD_ASRU_REPLACED;
	else if (repaired)
		alp->al_reason = FMD_ASRU_REPAIRED;
	else if (acquitted)
		alp->al_reason = FMD_ASRU_ACQUITTED;
	else
		alp->al_reason = FMD_ASRU_REMOVED;

	TRACE((FMD_DBG_ASRU, "asru %s recreated as %p (%s)", alp->al_uuid,
	    (void *)alp, _fmd_asru_snames[alp->al_flags & FMD_ASRU_STATE]));
}

static void
fmd_asru_hash_discard(fmd_asru_hash_t *ahp, const char *uuid, int err)
{
	char src[PATH_MAX], dst[PATH_MAX];

	(void) snprintf(src, PATH_MAX, "%s/%s", ahp->ah_dirpath, uuid);
	(void) snprintf(dst, PATH_MAX, "%s/%s-", ahp->ah_dirpath, uuid);

	if (err != 0)
		err = rename(src, dst);
	else
		err = unlink(src);

	if (err != 0 && errno != ENOENT)
		fmd_error(EFMD_ASRU_EVENT, "failed to rename log %s", src);
}

/*
 * Open a saved log file and restore it into the ASRU hash.  If we can't even
 * open the log, rename the log file to <uuid>- to indicate it is corrupt.  If
 * fmd_log_replay() fails, we either delete the file (if it has reached the
 * upper limit on cache age) or rename it for debugging if it was corrupted.
 */
static void
fmd_asru_hash_logopen(fmd_asru_hash_t *ahp, const char *uuid)
{
	fmd_log_t *lp = fmd_log_tryopen(ahp->ah_dirpath, uuid, FMD_LOG_ASRU);
	uint_t n;

	if (lp == NULL) {
		fmd_asru_hash_discard(ahp, uuid, errno);
		return;
	}

	ahp->ah_error = 0;
	n = ahp->ah_al_count;

	fmd_log_replay(lp, (fmd_log_f *)fmd_asru_hash_recreate, ahp);
	fmd_log_rele(lp);

	if (ahp->ah_al_count == n)
		fmd_asru_hash_discard(ahp, uuid, ahp->ah_error);
}

void
fmd_asru_hash_refresh(fmd_asru_hash_t *ahp)
{
	struct dirent *dp;
	DIR *dirp;
	int zero;

	if ((dirp = opendir(ahp->ah_dirpath)) == NULL) {
		fmd_error(EFMD_ASRU_NODIR,
		    "failed to open asru cache directory %s", ahp->ah_dirpath);
		return;
	}

	(void) fmd_conf_getprop(fmd.d_conf, "rsrc.zero", &zero);

	while ((dp = readdir(dirp)) != NULL) {
		if (dp->d_name[0] == '.')
			continue; /* skip "." and ".." */

		if (zero)
			fmd_asru_hash_discard(ahp, dp->d_name, 0);
		else if (!fmd_strmatch(dp->d_name, "*-"))
			fmd_asru_hash_logopen(ahp, dp->d_name);
	}

	(void) closedir(dirp);
}

/*
 * If the resource is present and faulty but not unusable, replay the fault
 * event that caused it be marked faulty.  This will cause the agent
 * subscribing to this fault class to again disable the resource.
 */
/*ARGSUSED*/
static void
fmd_asru_hash_replay_asru(fmd_asru_link_t *alp, void *data)
{
	fmd_event_t *e;
	nvlist_t *nvl;
	char *class;

	fmd_dprintf(FMD_DBG_ASRU, "%s: alp=0x%p, flags=0x%x, rsrc=%s, "
	    "asru=%s, fru=%s\n", __func__, (void *)alp, alp->al_flags,
	    alp->al_rsrc_name, alp->al_asru_name, alp->al_fru_name);

	if (alp->al_event != NULL && (alp->al_flags & (FMD_ASRU_STATE |
	    FMD_ASRU_PRESENT)) == (FMD_ASRU_FAULTY | FMD_ASRU_PRESENT)) {

		fmd_dprintf(FMD_DBG_ASRU, "%s: replaying fault event for %s",
		    __func__, alp->al_asru_name);

		(void) nvlist_xdup(alp->al_event, &nvl, &fmd.d_nva);
		(void) nvlist_lookup_string(nvl, FM_CLASS, &class);

		(void) nvlist_add_string(nvl, FMD_EVN_UUID,
		    ((fmd_case_impl_t *)alp->al_case)->ci_uuid);

		e = fmd_event_create(FMD_EVT_PROTOCOL, FMD_HRT_NOW, nvl, class);
		fmd_dispq_dispatch(fmd.d_disp, e, class);
	}
}

void
fmd_asru_hash_replay(fmd_asru_hash_t *ahp)
{
	fmd_asru_al_hash_apply(ahp, fmd_asru_hash_replay_asru, NULL);
}

/*ARGSUSED*/
void
fmd_asru_handle_dr(fmd_asru_link_t *alp, void *arg)
{
	int ps;
	int err;
	fmd_asru_rep_arg_t fara;
	char *asrustr = NULL, *frustr = NULL, *rsrcstr = NULL, *loc = NULL;
	fmd_asru_hash_t *ahp = fmd.d_asrus;
	int do_update = 0;
	fmd_case_impl_t *cip = (fmd_case_impl_t *)alp->al_case;
	nvlist_t *old_flt;

	/*
	 * replace the topo snapshot with the new one.
	 */
	fmd_topo_rele(ahp->ah_topo, __func__);
	ahp->ah_topo = fmd_topo_hold(__func__);

	/*
	 * Save the old fault nvlist before updating it. If the fru has moved
	 * then we'll send one list.updated with the old location saying "not
	 * present", followed by a second list.updated with the new location -
	 * thus giving the retire agent the chance to update the state of both
	 * locations.
	 */
	if ((alp->al_flags & FMD_ASRU_FAULTY) &&
	    !(alp->al_flags & FMD_ASRU_PROXY))
		old_flt = fmd_case_mkevent((fmd_case_t *)cip,
		    FM_LIST_UPDATED_CLASS);

	/*
	 * This function is called via "hash_apply", which means that ah_refs
	 * is > 0. This will prevent fmd_asru_hash_delete_case() from deleting
	 * the case, which means that the resource cache code is guaranteed to
	 * still have a hold on the case, so it in turn cannot be deleted. Hence
	 * we can safely lock and unlock the case as necessary (we need to
	 * lock it as we may modify al_event which is also referenced from
	 * the case structure).
	 *
	 * If ah_refs is > 1, it implies there are other "hash_apply" calls
	 * still running - wait until they are complete as they may be playing
	 * with al_event etc.
	 */
	(void) pthread_mutex_lock(&cip->ci_lock);
	(void) pthread_mutex_lock(&ahp->ah_lock);
	while (ahp->ah_refs > 1) {
		(void) pthread_mutex_unlock(&cip->ci_lock);
		(void) pthread_cond_wait(&ahp->ah_cv, &ahp->ah_lock);
		(void) pthread_mutex_unlock(&ahp->ah_lock);
		(void) pthread_mutex_lock(&cip->ci_lock);
		(void) pthread_mutex_lock(&ahp->ah_lock);
	}

	/*
	 * Update the fmris in the resource cache (if needed)
	 */
	fmd_asru_update_fault(alp->al_event, &asrustr, &frustr, &rsrcstr, &loc);
	if (asrustr) {
		fmd_asru_do_delete_entry(ahp, alp->al_case, ahp->ah_asru_hash,
		    offsetof(fmd_asru_link_t, al_asru_next), alp->al_asru_name);
		fmd_strfree(alp->al_asru_name);
		alp->al_asru_name = asrustr;
		fmd_asru_asru_hash_insert(ahp, alp, alp->al_asru_name);
	}
	if (frustr) {
		fmd_asru_do_delete_entry(ahp, alp->al_case, ahp->ah_fru_hash,
		    offsetof(fmd_asru_link_t, al_fru_next), alp->al_fru_name);
		fmd_strfree(alp->al_fru_name);
		alp->al_fru_name = frustr;
		fmd_asru_fru_hash_insert(ahp, alp, alp->al_fru_name);
	}
	if (rsrcstr) {
		fmd_asru_do_delete_entry(ahp, alp->al_case, ahp->ah_rsrc_hash,
		    offsetof(fmd_asru_link_t, al_rsrc_next), alp->al_rsrc_name);
		fmd_strfree(alp->al_rsrc_name);
		alp->al_rsrc_name = rsrcstr;
		fmd_asru_rsrc_hash_insert(ahp, alp, alp->al_rsrc_name);
	}
	if (loc) {
		fmd_asru_do_delete_entry(ahp, alp->al_case, ahp->ah_label_hash,
		    offsetof(fmd_asru_link_t, al_label_next), alp->al_label);
		fmd_strfree(alp->al_label);
		alp->al_label = loc;
		fmd_asru_label_hash_insert(ahp, alp, alp->al_label);
	}
	if (frustr || asrustr || rsrcstr || loc)
		do_update = 1;
	(void) pthread_mutex_unlock(&ahp->ah_lock);
	(void) pthread_mutex_unlock(&cip->ci_lock);

	if (!(alp->al_flags & FMD_ASRU_FAULTY))
		return;

	/*
	 * Checking for replaced resources only happens on the diagnosing side
	 * not on a proxy.
	 */
	if (alp->al_flags & FMD_ASRU_PROXY)
		return;

	/*
	 * If do_update is set the resource must now be present. Send two
	 * list.suspects, one with the old location, then one with the new.
	 */
	if (do_update) {
		fmd_event_t *e;
		char *class;

		(void) nvlist_lookup_string(old_flt, FM_CLASS, &class);
		e = fmd_event_create(FMD_EVT_PROTOCOL, FMD_HRT_NOW,
		    old_flt, class);
		(void) pthread_rwlock_rdlock(&fmd.d_log_lock);
		fmd_log_append(fmd.d_fltlog, e, (fmd_case_t *)cip);
		(void) pthread_rwlock_unlock(&fmd.d_log_lock);
		fmd_dispq_dispatch(fmd.d_disp, e, class);
		fmd_case_update(alp->al_case);
		(void) pthread_mutex_lock(&alp->al_lock);
		alp->al_flags |= FMD_ASRU_PRESENT;
		(void) pthread_mutex_unlock(&alp->al_lock);
		return;
	}
	nvlist_free(old_flt);
	ps = fmd_asru_replacement_state(alp->al_event, HC_ONLY_FALSE);
	if (ps == FMD_OBJ_STATE_REPLACED) {
		fara.fara_reason = FMD_ASRU_REPLACED;
		fara.fara_bywhat = FARA_ALL;
		fara.fara_rval = &err;
		fmd_asru_repaired(alp, &fara);
		(void) pthread_mutex_lock(&alp->al_lock);
		alp->al_flags &= ~FMD_ASRU_PRESENT;
		(void) pthread_mutex_unlock(&alp->al_lock);
	} else if (ps == FMD_OBJ_STATE_NOT_PRESENT) {
		if (alp->al_flags & FMD_ASRU_PRESENT)
			fmd_case_update(alp->al_case);
		(void) pthread_mutex_lock(&alp->al_lock);
		alp->al_flags &= ~FMD_ASRU_PRESENT;
		(void) pthread_mutex_unlock(&alp->al_lock);
	} else if (ps == FMD_OBJ_STATE_STILL_PRESENT ||
	    ps == FMD_OBJ_STATE_UNKNOWN) {
		if (!(alp->al_flags & FMD_ASRU_PRESENT))
			fmd_case_update(alp->al_case);
		(void) pthread_mutex_lock(&alp->al_lock);
		alp->al_flags |= FMD_ASRU_PRESENT;
		(void) pthread_mutex_unlock(&alp->al_lock);
	}
}

/*
 * Check if the resource is still present. If not, and if the rsrc.age time
 * has expired, then do an implicit repair on the resource.
 */
/*ARGSUSED*/
static void
fmd_asru_repair_if_aged(fmd_asru_link_t *alp, void *arg)
{
	struct timeval tv;
	fmd_log_t *lp;
	hrtime_t hrt;
	int ps;
	int err;
	fmd_asru_rep_arg_t fara;

	fmd_dprintf(FMD_DBG_ASRU, "%s: alp=0x%p, flags=0x%x, rsrc=%s, "
	    "asru=%s, fru=%s\n", __func__, (void *)alp, alp->al_flags,
	    alp->al_rsrc_name, alp->al_asru_name, alp->al_fru_name);

	if (!(alp->al_flags & FMD_ASRU_FAULTY))
		return;

	/*
	 * Checking for aged resources only happens on the diagnosing side
	 * not on a proxy.
	 */
	if (alp->al_flags & FMD_ASRU_PROXY)
		return;

	ps = fmd_asru_replacement_state(alp->al_event, HC_ONLY_FALSE);
	fmd_dprintf(FMD_DBG_ASRU, "%s: replacement_state=%d\n", __func__, ps);
	if (ps == FMD_OBJ_STATE_REPLACED) {
		fara.fara_reason = FMD_ASRU_REPLACED;
		fara.fara_bywhat = FARA_ALL;
		fara.fara_rval = &err;
		fmd_asru_repaired(alp, &fara);
	} else if (ps == FMD_OBJ_STATE_NOT_PRESENT) {
		fmd_time_gettimeofday(&tv);
		lp = fmd_log_open(alp->al_root, alp->al_uuid, FMD_LOG_ASRU);
		if (lp == NULL)
			return;
		hrt = (hrtime_t)(tv.tv_sec - lp->log_stat.st_mtime);
		fmd_log_rele(lp);
		if (hrt * NANOSEC >= fmd.d_asrus->ah_lifetime) {
			fara.fara_reason = FMD_ASRU_REMOVED;
			fara.fara_bywhat = FARA_ALL;
			fara.fara_rval = &err;
			fmd_asru_repaired(alp, &fara);
		}
	}
}

/*ARGSUSED*/
void
fmd_asru_check_if_aged(fmd_asru_link_t *alp, void *arg)
{
	struct timeval tv;
	fmd_log_t *lp;
	hrtime_t hrt;

	/*
	 * Case must be in resolved state for this to be called. So modified
	 * time on resource cache entry should be the time the resolve occurred.
	 * Return 0 if not yet hit rsrc.aged.
	 */
	fmd_time_gettimeofday(&tv);
	lp = fmd_log_open(alp->al_root, alp->al_uuid, FMD_LOG_ASRU);
	if (lp == NULL)
		return;
	hrt = (hrtime_t)(tv.tv_sec - lp->log_stat.st_mtime);
	fmd_log_rele(lp);
	if (hrt * NANOSEC < fmd.d_asrus->ah_lifetime)
		*(int *)arg = 0;
}

/*ARGSUSED*/
void
fmd_asru_most_recent(fmd_asru_link_t *alp, void *arg)
{
	fmd_log_t *lp;
	uint64_t hrt;

	/*
	 * Find most recent modified time of a set of resource cache entries.
	 */
	lp = fmd_log_open(alp->al_root, alp->al_uuid, FMD_LOG_ASRU);
	if (lp == NULL)
		return;
	hrt = lp->log_stat.st_mtime;
	fmd_log_rele(lp);
	if (*(uint64_t *)arg < hrt)
		*(uint64_t *)arg = hrt;
}

void
fmd_asru_clear_aged_rsrcs()
{
	fmd_asru_al_hash_apply(fmd.d_asrus, fmd_asru_repair_if_aged, NULL);
	fmd_case_hash_apply(fmd.d_cases, fmd_case_discard_resolved, NULL);
}

fmd_asru_hash_t *
fmd_asru_hash_create(const char *root, const char *dir)
{
	fmd_asru_hash_t *ahp;
	char path[PATH_MAX];

	ahp = fmd_zalloc(sizeof (fmd_asru_hash_t), FMD_SLEEP);
	(void) pthread_mutex_init(&ahp->ah_lock, NULL);
	(void) pthread_cond_init(&ahp->ah_cv, NULL);
	ahp->ah_hashlen = fmd.d_str_buckets;
	ahp->ah_asru_hash = fmd_zalloc(sizeof (void *) * ahp->ah_hashlen,
	    FMD_SLEEP);
	ahp->ah_case_hash = fmd_zalloc(sizeof (void *) * ahp->ah_hashlen,
	    FMD_SLEEP);
	ahp->ah_fru_hash = fmd_zalloc(sizeof (void *) * ahp->ah_hashlen,
	    FMD_SLEEP);
	ahp->ah_label_hash = fmd_zalloc(sizeof (void *) * ahp->ah_hashlen,
	    FMD_SLEEP);
	ahp->ah_rsrc_hash = fmd_zalloc(sizeof (void *) * ahp->ah_hashlen,
	    FMD_SLEEP);
	(void) snprintf(path, sizeof (path), "%s/%s", root, dir);
	ahp->ah_dirpath = fmd_strdup(path, FMD_SLEEP);
	(void) fmd_conf_getprop(fmd.d_conf, "rsrc.age", &ahp->ah_lifetime);
	(void) fmd_conf_getprop(fmd.d_conf, "fakenotpresent",
	    (uint32_t *)&fmd_asru_fake_not_present);
	ahp->ah_al_count = 0;
	ahp->ah_error = 0;
	ahp->ah_topo = fmd_topo_hold(__func__);

	return (ahp);
}

void
fmd_asru_hash_destroy(fmd_asru_hash_t *ahp)
{
	fmd_asru_link_t *alp, *np;
	uint_t i;

	(void) pthread_mutex_lock(&ahp->ah_lock);
	while (ahp->ah_refs > 0)
		(void) pthread_cond_wait(&ahp->ah_cv, &ahp->ah_lock);
	for (i = 0; i < ahp->ah_hashlen; i++) {
		for (alp = ahp->ah_case_hash[i]; alp != NULL; alp = np) {
			np = alp->al_case_next;
			alp->al_case_next = NULL;
			fmd_case_rele(alp->al_case);
			alp->al_case = NULL;
			fmd_asru_al_destroy(alp);
		}
	}

	fmd_strfree(ahp->ah_dirpath);
	fmd_free(ahp->ah_asru_hash, sizeof (void *) * ahp->ah_hashlen);
	fmd_free(ahp->ah_case_hash, sizeof (void *) * ahp->ah_hashlen);
	fmd_free(ahp->ah_fru_hash, sizeof (void *) * ahp->ah_hashlen);
	fmd_free(ahp->ah_label_hash, sizeof (void *) * ahp->ah_hashlen);
	fmd_free(ahp->ah_rsrc_hash, sizeof (void *) * ahp->ah_hashlen);
	fmd_topo_rele(ahp->ah_topo, __func__);
	fmd_free(ahp, sizeof (fmd_asru_hash_t));
}

void
fmd_asru_al_hash_apply(fmd_asru_hash_t *ahp,
    void (*func)(fmd_asru_link_t *, void *), void *arg)
{
	fmd_asru_link_t *alp, **alps, **alpp;
	uint_t alpc, i;

	(void) pthread_mutex_lock(&ahp->ah_lock);
	ahp->ah_refs++;

	alps = alpp = fmd_alloc(ahp->ah_al_count * sizeof (fmd_asru_link_t *),
	    FMD_SLEEP);
	alpc = ahp->ah_al_count;

	for (i = 0; i < ahp->ah_hashlen; i++) {
		for (alp = ahp->ah_case_hash[i]; alp != NULL;
		    alp = alp->al_case_next)
			*alpp++ = alp;
	}

	ASSERT(alpp == alps + alpc);
	(void) pthread_mutex_unlock(&ahp->ah_lock);

	for (i = 0; i < alpc; i++)
		func(alps[i], arg);

	(void) pthread_mutex_lock(&ahp->ah_lock);
	if (--ahp->ah_refs <= 1)
		(void) pthread_cond_broadcast(&ahp->ah_cv);
	(void) pthread_mutex_unlock(&ahp->ah_lock);

	fmd_free(alps, alpc * sizeof (fmd_asru_link_t *));
}

static void
fmd_asru_do_hash_apply(fmd_asru_hash_t *ahp, const char *name,
    void (*func)(fmd_asru_link_t *, void *), void *arg,
    fmd_asru_link_t **hash, size_t match_offset, size_t next_offset,
    boolean_t (*strcmp_func)(topo_hdl_t *, const char *, const char *))
{
	fmd_asru_link_t *alp, **alps, **alpp;
	uint_t alpc = 0, i;
	uint_t h;

	(void) pthread_mutex_lock(&ahp->ah_lock);
	ahp->ah_refs++;

	h = fmd_asru_strhash(ahp, name);

	for (alp = hash[h]; alp != NULL; alp =
	    /* LINTED pointer alignment */
	    FMD_ASRU_AL_HASH_NEXT(alp, next_offset))
		if (strcmp_func(ahp->ah_topo->ft_hdl,
		    /* LINTED pointer alignment */
		    FMD_ASRU_AL_HASH_NAME(alp, match_offset), name))
			alpc++;

	alps = alpp = fmd_alloc(alpc * sizeof (fmd_asru_link_t *), FMD_SLEEP);

	for (alp = hash[h]; alp != NULL; alp =
	    /* LINTED pointer alignment */
	    FMD_ASRU_AL_HASH_NEXT(alp, next_offset))
		if (strcmp_func(ahp->ah_topo->ft_hdl,
		    /* LINTED pointer alignment */
		    FMD_ASRU_AL_HASH_NAME(alp, match_offset), name))
			*alpp++ = alp;

	ASSERT(alpp == alps + alpc);
	(void) pthread_mutex_unlock(&ahp->ah_lock);

	for (i = 0; i < alpc; i++)
		func(alps[i], arg);

	(void) pthread_mutex_lock(&ahp->ah_lock);
	if (--ahp->ah_refs <= 1)
		(void) pthread_cond_broadcast(&ahp->ah_cv);
	(void) pthread_mutex_unlock(&ahp->ah_lock);

	fmd_free(alps, alpc * sizeof (fmd_asru_link_t *));
}

/*ARGSUSED*/
static boolean_t
string_strcmp(topo_hdl_t *thp, const char *a, const char *b)
{
	return (strcmp(a, b) == 0);
}

void
fmd_asru_hash_apply_by_asru(fmd_asru_hash_t *ahp, const char *name,
    void (*func)(fmd_asru_link_t *, void *), void *arg, boolean_t with_ident)
{
	fmd_asru_do_hash_apply(ahp, name, func, arg, ahp->ah_asru_hash,
	    offsetof(fmd_asru_link_t, al_asru_name),
	    offsetof(fmd_asru_link_t, al_asru_next), with_ident ?
	    topo_fmri_strcmp : topo_fmri_strcmp_noauth);
}

void
fmd_asru_hash_apply_by_case(fmd_asru_hash_t *ahp, fmd_case_t *cp,
	void (*func)(fmd_asru_link_t *, void *), void *arg)
{
	fmd_asru_do_hash_apply(ahp, ((fmd_case_impl_t *)cp)->ci_uuid, func, arg,
	    ahp->ah_case_hash, offsetof(fmd_asru_link_t, al_case_uuid),
	    offsetof(fmd_asru_link_t, al_case_next), string_strcmp);
}

void
fmd_asru_hash_apply_by_fru(fmd_asru_hash_t *ahp, const char *name,
    void (*func)(fmd_asru_link_t *, void *), void *arg, boolean_t with_ident)
{
	fmd_asru_do_hash_apply(ahp, name, func, arg, ahp->ah_fru_hash,
	    offsetof(fmd_asru_link_t, al_fru_name),
	    offsetof(fmd_asru_link_t, al_fru_next), with_ident ?
	    topo_fmri_strcmp : topo_fmri_strcmp_noauth);
}

void
fmd_asru_hash_apply_by_rsrc(fmd_asru_hash_t *ahp, const char *name,
    void (*func)(fmd_asru_link_t *, void *), void *arg, boolean_t with_ident)
{
	fmd_asru_do_hash_apply(ahp, name, func, arg, ahp->ah_rsrc_hash,
	    offsetof(fmd_asru_link_t, al_rsrc_name),
	    offsetof(fmd_asru_link_t, al_rsrc_next), with_ident ?
	    topo_fmri_strcmp : topo_fmri_strcmp_noauth);
}

void
fmd_asru_hash_apply_by_label(fmd_asru_hash_t *ahp, const char *name,
    void (*func)(fmd_asru_link_t *, void *), void *arg)
{
	fmd_asru_do_hash_apply(ahp, name, func, arg, ahp->ah_label_hash,
	    offsetof(fmd_asru_link_t, al_label),
	    offsetof(fmd_asru_link_t, al_label_next), string_strcmp);
}

/*
 * Create a resource cache entry using the fault event "nvl" for one of the
 * suspects from the case "cp".
 *
 * The fault event can have the following components :  FM_FAULT_ASRU,
 * FM_FAULT_FRU, FM_FAULT_RESOURCE. These should be set by the Diagnosis Engine
 * when calling fmd_nvl_create_fault(). In the general case, these are all
 * optional and an entry will always be added into the cache even if one or all
 * of these fields is missing.
 *
 * However, for hardware faults the recommended practice is that the fault
 * event should always have the FM_FAULT_RESOURCE field present and that this
 * should be represented in hc-scheme.
 *
 * Currently the DE should also add the FM_FAULT_ASRU and FM_FAULT_FRU fields
 * where known, though at some future stage fmd might be able to fill these
 * in automatically from the topology.
 */
fmd_asru_link_t *
fmd_asru_hash_create_entry(fmd_asru_hash_t *ahp, fmd_case_t *cp, nvlist_t *nvl)
{
	char *parsed_uuid;
	uuid_t uuid;
	int uuidlen;
	fmd_asru_link_t *alp;

	/*
	 * Generate a UUID for the ASRU.  libuuid cleverly gives us no
	 * interface for specifying or learning the buffer size.  Sigh.
	 * The spec says 36 bytes but we use a tunable just to be safe.
	 */
	(void) fmd_conf_getprop(fmd.d_conf, "uuidlen", &uuidlen);
	parsed_uuid = fmd_zalloc(uuidlen + 1, FMD_SLEEP);
	uuid_generate(uuid);
	uuid_unparse(uuid, parsed_uuid);

	/*
	 * Now create the resource cache entries.
	 */
	fmd_case_hold_locked(cp);
	alp = fmd_asru_al_create(ahp, nvl, cp, parsed_uuid);
	TRACE((FMD_DBG_ASRU, "asru %s created as %p",
	    parsed_uuid, (void *)alp));

	fmd_free(parsed_uuid, uuidlen + 1);
	return (alp);

}

static void
fmd_asru_do_delete_entry(fmd_asru_hash_t *ahp, fmd_case_t *cp,
    fmd_asru_link_t **hash, size_t next_offset, char *name)
{
	uint_t h;
	fmd_asru_link_t *alp, **pp, *alpnext, **alpnextp;

	h = fmd_asru_strhash(ahp, name);
	pp = &hash[h];
	for (alp = *pp; alp != NULL; alp = alpnext) {
		/* LINTED pointer alignment */
		alpnextp = FMD_ASRU_AL_HASH_NEXTP(alp, next_offset);
		alpnext = *alpnextp;
		if (alp->al_case == cp) {
			*pp = *alpnextp;
			*alpnextp = NULL;
		} else
			pp = alpnextp;
	}
}

static void
fmd_asru_do_hash_delete(fmd_asru_hash_t *ahp, fmd_case_susp_t *cis,
    fmd_case_t *cp, fmd_asru_link_t **hash, size_t next_offset, char *nvname)
{
	nvlist_t *nvl;
	char *name = NULL;
	ssize_t namelen;

	if (nvlist_lookup_nvlist(cis->cis_nvl, nvname, &nvl) == 0 &&
	    (namelen = fmd_fmri_nvl2str(nvl, NULL, 0)) != -1 &&
	    (name = fmd_alloc(namelen + 1, FMD_SLEEP)) != NULL) {
		if (fmd_fmri_nvl2str(nvl, name, namelen + 1) != -1)
			fmd_asru_do_delete_entry(ahp, cp, hash, next_offset,
			    name);
		fmd_free(name, namelen + 1);
	} else
		fmd_asru_do_delete_entry(ahp, cp, hash, next_offset, "");
}

void
fmd_asru_hash_delete_case(fmd_asru_hash_t *ahp, fmd_case_t *cp)
{
	fmd_case_impl_t *cip = (fmd_case_impl_t *)cp;
	fmd_case_susp_t *cis;
	fmd_asru_link_t *alp, **plp, *alpnext;
	char path[PATH_MAX];
	char *label;
	uint_t h;

	/*
	 * If ah_refs is > 0, it implies there are "hash_apply" calls
	 * still running - wait until they are complete as they may be playing
	 * with al_event etc.
	 */
	(void) pthread_mutex_lock(&ahp->ah_lock);
	while (ahp->ah_refs > 0) {
		(void) pthread_mutex_unlock(&cip->ci_lock);
		(void) pthread_cond_wait(&ahp->ah_cv, &ahp->ah_lock);
		(void) pthread_mutex_unlock(&ahp->ah_lock);
		(void) pthread_mutex_lock(&cip->ci_lock);
		(void) pthread_mutex_lock(&ahp->ah_lock);
	}

	/*
	 * first delete hash entries for each suspect
	 */
	for (cis = cip->ci_suspects; cis != NULL; cis = cis->cis_next) {
		fmd_asru_do_hash_delete(ahp, cis, cp, ahp->ah_fru_hash,
		    offsetof(fmd_asru_link_t, al_fru_next), FM_FAULT_FRU);
		fmd_asru_do_hash_delete(ahp, cis, cp, ahp->ah_rsrc_hash,
		    offsetof(fmd_asru_link_t, al_rsrc_next), FM_FAULT_RESOURCE);
		if (nvlist_lookup_string(cis->cis_nvl, FM_FAULT_LOCATION,
		    &label) != 0)
			label = "";
		fmd_asru_do_delete_entry(ahp, cp, ahp->ah_label_hash,
		    offsetof(fmd_asru_link_t, al_label_next), label);
		fmd_asru_do_hash_delete(ahp, cis, cp, ahp->ah_asru_hash,
		    offsetof(fmd_asru_link_t, al_asru_next), FM_FAULT_ASRU);
	}

	/*
	 * then delete associated case hash entries
	 */
	h = fmd_asru_strhash(ahp, cip->ci_uuid);
	plp = &ahp->ah_case_hash[h];
	for (alp = *plp; alp != NULL; alp = alpnext) {
		alpnext = alp->al_case_next;
		if (alp->al_case == cp) {
			*plp = alp->al_case_next;
			alp->al_case_next = NULL;
			ASSERT(ahp->ah_al_count != 0);
			ahp->ah_al_count--;

			/*
			 * decrement case ref.
			 */
			fmd_case_rele_locked(cp);
			alp->al_case = NULL;

			/*
			 * If we found a matching ASRU, unlink its log file and
			 * then release the hash entry. Note that it may still
			 * be referenced if another thread is manipulating it;
			 * this is ok because once we unlink, the log file will
			 * not be restored, and the log data will be freed when
			 * all of the referencing threads release their
			 * respective references.
			 */
			(void) snprintf(path, sizeof (path), "%s/%s",
			    ahp->ah_dirpath, alp->al_uuid);
			if (cip->ci_xprt == NULL && unlink(path) != 0)
				fmd_error(EFMD_ASRU_UNLINK,
				    "failed to unlink asru %s", path);

			fmd_asru_al_destroy(alp);
		} else
			plp = &alp->al_case_next;
	}
	(void) pthread_mutex_unlock(&ahp->ah_lock);
}

static void
fmd_asru_unrepaired_visible_faults(fmd_asru_link_t *alp, void *arg)
{
	int *flagsp = (int *)arg;

	if (!(alp->al_flags & FMD_ASRU_INVISIBLE))
		*flagsp |= (alp->al_flags & FMD_ASRU_FAULTY);
}

static void
fmd_asru_repair_invisible(fmd_asru_link_t *alp, void *arg)
{
	uint8_t *reasonp = (uint8_t *)arg;

	if (alp->al_flags & FMD_ASRU_INVISIBLE) {
		if (fmd_asru_clrflags(alp, FMD_ASRU_FAULTY, *reasonp)) {
			if (alp->al_flags & FMD_ASRU_PROXY)
				fmd_case_xprt_updated(alp->al_case);
			else
				fmd_case_update(alp->al_case);
		}
	}
}

static void
fmd_asru_do_repair_containees(fmd_asru_link_t *alp, uint8_t reason)
{
	int unrepaired = 0;

	/*
	 * If we have had visible faults associated with this fru, and they
	 * have all been repaired, then we should also repair any invisible
	 * ones.
	 */
	if (alp->al_fru_name == NULL || (alp->al_flags & FMD_ASRU_INVISIBLE))
		return;
	fmd_asru_hash_apply_by_fru(fmd.d_asrus, alp->al_fru_name,
	    fmd_asru_unrepaired_visible_faults, &unrepaired, B_TRUE);
	if (unrepaired)
		return;
	fmd_asru_hash_apply_by_fru(fmd.d_asrus, alp->al_fru_name,
	    fmd_asru_repair_invisible, &reason, B_TRUE);
}

void
fmd_asru_repaired(fmd_asru_link_t *alp, void *arg)
{
	int cleared;
	fmd_asru_rep_arg_t *farap = (fmd_asru_rep_arg_t *)arg;

	/*
	 * don't allow remote repair over readonly transport
	 */
	if (alp->al_flags & FMD_ASRU_PROXY_RDONLY)
		return;

	/*
	 * don't allow repair etc by asru on proxy unless asru is local
	 */
	if (farap->fara_bywhat == FARA_BY_ASRU &&
	    (alp->al_flags & FMD_ASRU_PROXY) &&
	    !(alp->al_flags & FMD_ASRU_PROXY_WITH_ASRU))
		return;
	/*
	 * For acquit, need to check both name and uuid if specified
	 */
	if (farap->fara_reason == FMD_ASRU_ACQUITTED &&
	    farap->fara_rval != NULL && strcmp(farap->fara_uuid, "") != 0 &&
	    strcmp(farap->fara_uuid, alp->al_case_uuid) != 0)
		return;

	/*
	 * For replaced, verify it has been replaced if we have serial number.
	 * If not set *farap->fara_rval to FARA_ERR_RSRCNOTR.
	 */
	if (farap->fara_reason == FMD_ASRU_REPLACED &&
	    !(alp->al_flags & FMD_ASRU_PROXY_PRESENCE) &&
	    fmd_asru_replacement_state(alp->al_event,
	    (alp->al_flags & FMD_ASRU_PROXY) ? HC_ONLY_TRUE : HC_ONLY_FALSE) ==
	    FMD_OBJ_STATE_STILL_PRESENT) {
		if (farap->fara_rval)
			*farap->fara_rval = FARA_ERR_RSRCNOTR;
		return;
	}

	cleared = fmd_asru_clrflags(alp, FMD_ASRU_FAULTY, farap->fara_reason);
	if (cleared)
		fmd_asru_do_repair_containees(alp, farap->fara_reason);

	/*
	 * if called from fmd_adm_*() and we really did clear the bit then
	 * we need to do a case update to see if the associated case can be
	 * repaired. No need to do this if called from fmd_case_*() (ie
	 * when arg is NULL) as the case will be explicitly repaired anyway.
	 */
	if (farap->fara_rval) {
		/*
		 * *farap->fara_rval defaults to FARA_ERR_RSRCNOTF (not found).
		 * If we find a valid cache entry which we repair then we
		 * set it to FARA_OK. However we don't want to do this if
		 * we have already set it to FARA_ERR_RSRCNOTR (not replaced)
		 * in a previous iteration (see above). So only set it to
		 * FARA_OK if the current value is still FARA_ERR_RSRCNOTF.
		 */
		if (*farap->fara_rval == FARA_ERR_RSRCNOTF)
			*farap->fara_rval = FARA_OK;
		if (cleared) {
			if (alp->al_flags & FMD_ASRU_PROXY)
				fmd_case_xprt_updated(alp->al_case);
			else
				fmd_case_update(alp->al_case);
		}
	}
}

/*ARGSUSED*/
void
fmd_asru_isolated(fmd_asru_link_t *alp, void *arg)
{
	if (alp->al_case)
		fmd_case_check_isolated(alp->al_case);
}

/*ARGSUSED*/
void
fmd_asru_resolved(fmd_asru_link_t *alp, void *arg)
{
	if (alp->al_case)
		fmd_case_check_resolved(alp->al_case);
}

/*
 * Discard the case associated with this alp if it is in resolved state.
 * Called on "fmadm flush".
 */
/*ARGSUSED*/
void
fmd_asru_flush(fmd_asru_link_t *alp, void *arg)
{
	int *rval = (int *)arg;

	if (alp->al_case)
		fmd_case_set_flush(alp->al_case, NULL);
	*rval = 0;
}

/*
 * This is only called for proxied faults. Set various flags so we can
 * find the nature of the transport from the resource cache code.
 */
/*ARGSUSED*/
void
fmd_asru_set_on_proxy(fmd_asru_link_t *alp, void *arg)
{
	fmd_asru_set_on_proxy_t *entryp = (fmd_asru_set_on_proxy_t *)arg;

	if (*entryp->fasp_countp >= entryp->fasp_maxcount)
		return;

	/*
	 * Note that this is a proxy fault and save whether transport is
	 * RDONLY or presence state detected remotely.
	 */
	(void) pthread_mutex_lock(&alp->al_lock);
	alp->al_flags |= FMD_ASRU_PROXY;

	if (entryp->fasp_proxy_external)
		alp->al_flags |= FMD_ASRU_PROXY_PRESENCE;

	if (entryp->fasp_proxy_rdonly)
		alp->al_flags |= FMD_ASRU_PROXY_RDONLY;

	/*
	 * Save whether asru is accessible in local domain
	 */
	if (entryp->fasp_proxy_asru[*entryp->fasp_countp])
		alp->al_flags |= FMD_ASRU_PROXY_WITH_ASRU;
	(void) pthread_mutex_unlock(&alp->al_lock);
	(*entryp->fasp_countp)++;
}

/*ARGSUSED*/
void
fmd_asru_update_containees(fmd_asru_link_t *alp, void *arg)
{
	fmd_asru_do_repair_containees(alp, alp->al_reason);
}

/*
 * This function is used for fault proxying. It updates the resource status in
 * the resource cache based on information that has come from the other side of
 * the transport. This can be called on either the proxy side or the
 * diagnosing side.
 */
void
fmd_asru_update_status(fmd_asru_link_t *alp, void *arg)
{
	fmd_asru_update_status_t *entryp = (fmd_asru_update_status_t *)arg;
	uint8_t status;

	if (*entryp->faus_countp >= entryp->faus_maxcount)
		return;

	status = entryp->faus_ba[*entryp->faus_countp];

	/*
	 * For proxy, if there is no asru on the proxy side, but there is on
	 * the diag side, then take the diag side asru status.
	 * For diag, if there is an asru on the proxy side, then take the proxy
	 * side asru status.
	 */
	if (entryp->faus_is_proxy ?
	    (entryp->faus_diag_asru[*entryp->faus_countp] &&
	    !entryp->faus_proxy_asru[*entryp->faus_countp]) :
	    entryp->faus_proxy_asru[*entryp->faus_countp]) {
		(void) pthread_mutex_lock(&alp->al_lock);
		if (status & FM_SUSPECT_DEGRADED)
			alp->al_flags |= FMD_ASRU_DEGRADED;
		else
			alp->al_flags &= ~FMD_ASRU_DEGRADED;
		(void) pthread_mutex_unlock(&alp->al_lock);
		if (status & FM_SUSPECT_UNUSABLE)
			(void) fmd_asru_setflags(alp, FMD_ASRU_UNUSABLE);
		else
			(void) fmd_asru_clrflags(alp, FMD_ASRU_UNUSABLE, 0);
	}

	/*
	 * Update the faulty status too.
	 */
	if (!(status & FM_SUSPECT_FAULTY))
		entryp->faus_repaired |= fmd_asru_clrflags(alp, FMD_ASRU_FAULTY,
		    (status & FM_SUSPECT_REPAIRED) ? FMD_ASRU_REPAIRED :
		    (status & FM_SUSPECT_REPLACED) ? FMD_ASRU_REPLACED :
		    (status & FM_SUSPECT_ACQUITTED) ? FMD_ASRU_ACQUITTED :
		    FMD_ASRU_REMOVED);
	else if (entryp->faus_is_proxy)
		(void) fmd_asru_setflags(alp, FMD_ASRU_FAULTY);

	/*
	 * for proxy only, update the present status too.
	 */
	if (entryp->faus_is_proxy) {
		(void) pthread_mutex_lock(&alp->al_lock);
		if (!(status & FM_SUSPECT_NOT_PRESENT))
			alp->al_flags |= FMD_ASRU_PRESENT;
		else
			alp->al_flags &= ~FMD_ASRU_PRESENT;
		(void) pthread_mutex_unlock(&alp->al_lock);
	}
	(*entryp->faus_countp)++;
}

/*
 * This function is called on the diagnosing side when fault proxying is
 * in use and the proxy has sent a uuclose. It updates the status of the
 * resource cache entries.
 */
void
fmd_asru_close_status(fmd_asru_link_t *alp, void *arg)
{
	fmd_asru_close_status_t *entryp = (fmd_asru_close_status_t *)arg;

	if (*entryp->facs_countp >= entryp->facs_maxcount)
		return;
	(void) pthread_mutex_lock(&alp->al_lock);
	alp->al_flags &= ~FMD_ASRU_DEGRADED;
	(void) pthread_mutex_unlock(&alp->al_lock);
	(void) fmd_asru_setflags(alp, FMD_ASRU_UNUSABLE);
	(*entryp->facs_countp)++;
}

static void
fmd_asru_logevent(fmd_asru_link_t *alp)
{
	boolean_t faulty = (alp->al_flags & FMD_ASRU_FAULTY) != 0;
	boolean_t unusable = (alp->al_flags & FMD_ASRU_UNUSABLE) != 0;
	boolean_t message = (alp->al_flags & FMD_ASRU_INVISIBLE) == 0;
	boolean_t repaired = (alp->al_reason == FMD_ASRU_REPAIRED);
	boolean_t replaced = (alp->al_reason == FMD_ASRU_REPLACED);
	boolean_t acquitted = (alp->al_reason == FMD_ASRU_ACQUITTED);
	fmd_case_impl_t *cip;
	fmd_event_t *e;
	fmd_log_t *lp;
	nvlist_t *nvl;
	char *class;

	ASSERT(MUTEX_HELD(&alp->al_lock));
	cip = (fmd_case_impl_t *)alp->al_case;
	ASSERT(cip != NULL);

	/*
	 * Don't log to disk on proxy side
	 */
	if (cip->ci_xprt != NULL)
		return;

	lp = fmd_log_open(alp->al_root, alp->al_uuid, FMD_LOG_ASRU);

	if (lp == NULL)
		return; /* can't log events if we can't open the log */

	nvl = fmd_protocol_rsrc_asru(_fmd_asru_events[faulty | (unusable << 1)],
	    cip->ci_uuid, cip->ci_code, faulty, unusable,
	    message, alp->al_event, &cip->ci_tv, repaired, replaced, acquitted,
	    cip->ci_state == FMD_CASE_RESOLVED, cip->ci_diag_de == NULL ?
	    cip->ci_mod->mod_fmri : cip->ci_diag_de, cip->ci_injected == 1,
	    cip->ci_topo_uuid);

	(void) nvlist_lookup_string(nvl, FM_CLASS, &class);
	e = fmd_event_create(FMD_EVT_PROTOCOL, FMD_HRT_NOW, nvl, class);

	fmd_event_hold(e);
	fmd_log_append(lp, e, NULL);
	fmd_event_rele(e);

	/*
	 * For now, we close the log file after every update to conserve file
	 * descriptors and daemon overhead.  If this becomes a performance
	 * issue this code can change to keep a fixed-size LRU cache of logs.
	 */
	fmd_log_rele(lp);
}

int
fmd_asru_setflags(fmd_asru_link_t *alp, uint_t sflag)
{
	uint_t nstate, ostate;

	ASSERT(!(sflag & ~FMD_ASRU_STATE));
	ASSERT(sflag != FMD_ASRU_STATE);

	(void) pthread_mutex_lock(&alp->al_lock);

	ostate = alp->al_flags & FMD_ASRU_STATE;
	alp->al_flags |= sflag;
	nstate = alp->al_flags & FMD_ASRU_STATE;

	if (nstate == ostate) {
		(void) pthread_mutex_unlock(&alp->al_lock);
		return (0);
	}

	TRACE((FMD_DBG_ASRU, "asru %s %s->%s", alp->al_uuid,
	    _fmd_asru_snames[ostate], _fmd_asru_snames[nstate]));

	fmd_asru_logevent(alp);
	(void) pthread_mutex_unlock(&alp->al_lock);
	return (1);
}

int
fmd_asru_clrflags(fmd_asru_link_t *alp, uint_t sflag, uint8_t reason)
{
	uint_t nstate, ostate;

	ASSERT(!(sflag & ~FMD_ASRU_STATE));
	ASSERT(sflag != FMD_ASRU_STATE);

	(void) pthread_mutex_lock(&alp->al_lock);

	ostate = alp->al_flags & FMD_ASRU_STATE;
	alp->al_flags &= ~sflag;
	nstate = alp->al_flags & FMD_ASRU_STATE;

	if (nstate == ostate) {
		if (reason > alp->al_reason &&
		    ((fmd_case_impl_t *)alp->al_case)->ci_state <
		    FMD_CASE_REPAIRED) {
			alp->al_reason = reason;
			fmd_asru_logevent(alp);
		}
		(void) pthread_mutex_unlock(&alp->al_lock);
		return (0);
	}
	if (reason > alp->al_reason)
		alp->al_reason = reason;

	TRACE((FMD_DBG_ASRU, "asru %s %s->%s", alp->al_uuid,
	    _fmd_asru_snames[ostate], _fmd_asru_snames[nstate]));

	fmd_asru_logevent(alp);

	(void) pthread_mutex_unlock(&alp->al_lock);

	return (1);
}

/*ARGSUSED*/
void
fmd_asru_log_resolved(fmd_asru_link_t *alp, void *unused)
{
	(void) pthread_mutex_lock(&alp->al_lock);
	fmd_asru_logevent(alp);
	(void) pthread_mutex_unlock(&alp->al_lock);
}

/*
 * Report the current known state of the link entry (ie this particular fault
 * affecting this particular ASRU).
 */
int
fmd_asru_al_getstate(fmd_asru_link_t *alp)
{
	int us, st = (alp->al_flags & (FMD_ASRU_FAULTY | FMD_ASRU_UNUSABLE));
	nvlist_t *asru;
	int ps = FMD_OBJ_STATE_UNKNOWN;

	/*
	 * For fault proxying with FMD_ASRU_PROXY_PRESENCE, believe the presence
	 * state as sent by the diagnosing side. Otherwise find the presence
	 * state here. Note for fault proxying without FMD_ASRU_PROXY_PRESENCE,
	 * we can only trust the presence state where we are using hc-scheme
	 * fmris which should be consistant across domains in the same system -
	 * other schemes can refer to different devices in different domains.
	 */
	if (!(alp->al_flags & FMD_ASRU_PROXY_PRESENCE)) {
		ps = fmd_asru_replacement_state(alp->al_event, (alp->al_flags &
		    FMD_ASRU_PROXY)? HC_ONLY_TRUE : HC_ONLY_FALSE);
	}
	if (ps == FMD_OBJ_STATE_UNKNOWN && (alp->al_flags & FMD_ASRU_PROXY))
		st |= (alp->al_flags & (FMD_ASRU_DEGRADED | FMD_ASRU_PRESENT));
	else if (ps == FMD_OBJ_STATE_NOT_PRESENT)
		st |= (alp->al_flags & FMD_ASRU_DEGRADED) | FMD_ASRU_UNUSABLE;
	else if (ps == FMD_OBJ_STATE_REPLACED) {
		(void) pthread_mutex_lock(&alp->al_lock);
		if (alp->al_reason < FMD_ASRU_REPLACED &&
		    !(alp->al_flags & FMD_ASRU_FAULTY))
			alp->al_reason = FMD_ASRU_REPLACED;
		(void) pthread_mutex_unlock(&alp->al_lock);
		st |= (alp->al_flags & FMD_ASRU_DEGRADED);
	} else
		st |= (alp->al_flags & (FMD_ASRU_DEGRADED)) | FMD_ASRU_PRESENT;

	/*
	 * For fault proxying, unless we have a local ASRU, then believe the
	 * service state sent by the diagnosing side. Otherwise find the service
	 * state here.
	 */
	if ((!(alp->al_flags & FMD_ASRU_PROXY) ||
	    (alp->al_flags & FMD_ASRU_PROXY_WITH_ASRU)) &&
	    nvlist_lookup_nvlist(alp->al_event, FM_FAULT_ASRU, &asru) == 0) {
		us = fmd_fmri_service_state(asru);
		if (us == -1 || us == FMD_SERVICE_STATE_UNKNOWN) {
			st &= ~FMD_ASRU_UNUSABLE;
		} else {
			if (us == FMD_SERVICE_STATE_UNUSABLE) {
				st &= ~FMD_ASRU_DEGRADED;
				st |= FMD_ASRU_UNUSABLE;
			} else if (us == FMD_SERVICE_STATE_OK) {
				st &= ~(FMD_ASRU_DEGRADED | FMD_ASRU_UNUSABLE);
			} else if (us == FMD_SERVICE_STATE_ISOLATE_PENDING) {
				st &= ~(FMD_ASRU_DEGRADED | FMD_ASRU_UNUSABLE);
			} else if (us == FMD_SERVICE_STATE_DEGRADED) {
				st &= ~FMD_ASRU_UNUSABLE;
				st |= FMD_ASRU_DEGRADED;
			}
		}
	}
	return (st);
}
