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

#include <assert.h>
#include <config_admin.h>
#include <errno.h>
#include <libsysevent.h>
#include <pthread.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <fm/libtopo.h>
#include <fm/topo_hc.h>
#include <fm/topo_list.h>
#include <fm/fmd_fmri.h>
#include <sys/fm/protocol.h>
#include <sys/sunddi.h>
#include <sys/sysevent/dr.h>
#include <sys/sysevent/eventdefs.h>

#include <fm/fmd_api.h>

#include "fru_monitor.h"

/*
 * The FRU database is used to keep track of removable components on the system
 * and generate sysevents when components are added or removed.  This allows
 * generic management software to be written on top of libtopo using FMRIs, and
 * hides the implementation details about how to detect FRU changes.  This
 * operates in one of two ways:
 *
 * 	For disks, we listen to sysevents that may affect the topology.  When
 * 	we receive one of these events, we look at the most recent topo
 * 	snapshot, and generate notifications for any disks that have been
 * 	added or removed.
 *
 * 	For external components (power supplies and fans), we periodically poll
 * 	the known set of components and check for presence.
 *
 * In addition, this module is responsible for managing LEDs based on these
 * state transitions.
 */

static struct fru_mon_stats {
	fmd_stat_t replays_dropped;
	fmd_stat_t leds_on;
	fmd_stat_t leds_off;
} fru_stats = {
	{ "replays_dropped", FMD_TYPE_UINT64,
	    "replayed repair events dropped" },
	{ "leds_on", FMD_TYPE_UINT64,
	    "no of times a SERVICE LED was turned on" },
	{ "leds_off", FMD_TYPE_UINT64,
	    "no of times a SERVICE LED was turned off" },
};

#define	BUMPSTAT(stat)  fru_stats.stat.fmds_value.ui64++

static hrtime_t fru_poll_interval;
static hrtime_t fru_chassis_timeout;

static void fru_scan(fru_hash_t *);

/*
 * Strip out everything except the chassis-id from the authority, as the rest
 * may be specific to the component itself.
 */
static void
fru_strip(nvlist_t *nvl)
{
	nvlist_t *auth;

	if (nvlist_lookup_nvlist(nvl, FM_FMRI_AUTHORITY,
	    &auth) == 0)
		(void) nvlist_remove_all(auth, FM_FMRI_AUTH_PRODUCT);

	(void) nvlist_remove_all(nvl, FM_FMRI_HC_SERIAL_ID);
	(void) nvlist_remove_all(nvl, FM_FMRI_HC_PART);
	(void) nvlist_remove_all(nvl, FM_FMRI_HC_REVISION);
	(void) nvlist_remove_all(nvl, FM_FMRI_HC_DEVID);
	(void) nvlist_remove_all(nvl, FM_FMRI_HC_PARENT_SERIAL);
}

/*
 * Search the fru_hash for this FRU.  The key is the fmri string.
 * If found return a pointer the the fru structure, otherwiser return NULL.
 */
static fru_t *
fru_lookup(fru_hash_t *fhp, topo_hdl_t *thp, const char *fmristr)
{
	uint_t h = topo_fmri_strhash(thp, fmristr) % fhp->fh_hashlen;
	fru_t *fp;

	for (fp = fhp->fh_hash[h]; fp != NULL; fp = fp->fru_chain) {
		if (topo_fmri_strcmp(thp, fp->fru_fmristr, fmristr)) {
			if (fp->fru_chassis != NULL)
				fp->fru_chassis->frc_present = B_TRUE;
			return (fp);
		}
	}

	return (NULL);
}

/*
 * Insert the fru_t structure for a fru into the fru_hash.
 */
static void
fru_insert(fru_hash_t *fhp, topo_hdl_t *thp, fru_t *fp)
{
	uint_t h = topo_fmri_strhash(thp, fp->fru_fmristr) % fhp->fh_hashlen;

	assert(fru_lookup(fhp, thp, fp->fru_fmristr) == NULL);

	fp->fru_chain = fhp->fh_hash[h];
	fhp->fh_hash[h] = fp;
	fhp->fh_count++;

	fp->fru_next = fhp->fh_list;
	if (fhp->fh_list != NULL)
		fhp->fh_list->fru_prev = fp;
	fhp->fh_list = fp;
}

/*
 * Check whether the FRU indicated in the fru_hash is present in the system.
 */
static int
fru_check_present(topo_hdl_t *thp, fru_t *fp)
{
	int err = 0;
	boolean_t present = B_TRUE;
	int ret;
	nvlist_t *nvl;

	if ((ret = topo_fmri_presence_state(thp, fp->fru_fmri,
	    &err)) < 0 || err != 0)
		return (-1);

	if (ret == FMD_OBJ_STATE_NOT_PRESENT)
		present = B_FALSE;
	/*
	 * If the service processor is reset, and we take a snapshot while the
	 * service processor is down, then topo_fmri_present() will resort to
	 * the standard hc presence detection, which will return FALSE because
	 * the node isn't present in the current snapshot.  To prevent false
	 * positives (we don't know the state of the FRU), call
	 * topo_fmri_fru(), which will fail if
	 * the node isn't present, and succeed if it is (and truly is not
	 * present).
	 */
	if (!present) {
		if (topo_fmri_fru(thp, fp->fru_fmri,
		    &nvl, &err) != 0)
			return (-1);

		nvlist_free(nvl);
	}

	fp->fru_present = present;
	return (0);
}

#define	TOPO_LED_STATE_SUCCESS	0x10

/*
 * Execute a topo_prop_set call with the indicated LED state.
 */
/*ARGSUSED*/
static int
fru_set_indicator_cb(topo_hdl_t *thp, tnode_t *node, void *arg)
{
	uint32_t *modep = arg;
	int err;

	if (topo_prop_set_uint32(node, TOPO_PGROUP_FACILITY, TOPO_LED_MODE,
	    TOPO_PROP_MUTABLE, *modep & ~TOPO_LED_STATE_SUCCESS, &err) == 0)
		*modep |= TOPO_LED_STATE_SUCCESS;

	return (1);
}

/*
 * Set up the call to the facility node via the topo_fmri_facility interface.
 */
static void
fru_set_indicator(fmd_hdl_t *hdl, topo_hdl_t *thp, fru_t *fp,
    topo_led_type_t type, boolean_t state)
{
	topo_led_state_t mode;
	int err;
	char typestr[32];

	if (fp->fru_ctl == NULL)
		return;

	topo_led_type_name(type, typestr, sizeof (typestr));

	mode = (state ? TOPO_LED_STATE_ON : TOPO_LED_STATE_OFF);

	(void) topo_fmri_facility(thp, fp->fru_ctl, TOPO_FAC_TYPE_INDICATOR,
	    type, fru_set_indicator_cb, &mode, &err);

	if (mode & TOPO_LED_STATE_SUCCESS) {
		fmd_hdl_debug(hdl, "set %s indicator of %s to %s", typestr,
		    fp->fru_fmristr, state ? "on" : "off");
		state ? BUMPSTAT(leds_on) : BUMPSTAT(leds_off);
	}

	else
		fmd_hdl_debug(hdl,
		    "Call to set %s indicator of %s to %s has failed",
		    typestr, fp->fru_fmristr, state ? "on" : "off");

}

/*
 * Set up and post a sysevent for disk, psu, fan or controller frus here.
 */
static void
fru_post_sysevent(fmd_hdl_t *hdl, fru_t *fp, const char *subclass)
{
	nvlist_t *nvl = NULL;
	sysevent_id_t eid;
	const char *type;

	fmd_hdl_debug(hdl, "posting %s for %s", subclass,
	    fp->fru_fmristr);

	switch (fp->fru_type) {
	case FRU_DISK:
		type = "disk";
		break;

	case FRU_PSU:
		type = "psu";
		break;

	case FRU_FAN:
		type = "fan";
		break;

	case FRU_CONTROLLER:
		type = "controller";
		break;
	}

	if ((nvl = fmd_nvl_alloc(hdl, FMD_SLEEP)) == NULL ||
	    nvlist_add_string(nvl, TOPO_EV_FMRISTR, fp->fru_fmristr) != 0 ||
	    nvlist_add_string(nvl, TOPO_EV_TYPE, type) != 0 ||
	    nvlist_add_nvlist(nvl, TOPO_EV_FMRI, fp->fru_fmri) != 0) {
		fmd_hdl_debug(hdl, "failed to construct sysevent payload");
		return;
	}

	if (sysevent_post_event(EC_TOPO, (char *)subclass, SUNW_VENDOR, "fmd",
	    nvl, &eid) != 0) {
		fmd_hdl_error(hdl, "failed to post sysevent");
	}

	nvlist_free(nvl);

}

/*
 * Construct and post sysevents for new or removed chassis detected in this
 * system.
 */
static void
fru_chassis_post_sysevent(fmd_hdl_t *hdl, fru_chassis_t *cp,
    const char *subclass)
{
	nvlist_t *nvl = NULL;
	sysevent_id_t eid;

	fmd_hdl_debug(hdl, "posting %s for %s", subclass,
	    cp->frc_fmristr);


	if ((nvl = fmd_nvl_alloc(hdl, FMD_SLEEP)) == NULL ||
	    nvlist_add_string(nvl, TOPO_EV_FMRISTR, cp->frc_fmristr) != 0 ||
	    nvlist_add_string(nvl, TOPO_EV_TYPE, "chassis") != 0 ||
	    nvlist_add_nvlist(nvl, TOPO_EV_FMRI, cp->frc_fmri) != 0) {
		fmd_hdl_debug(hdl, "failed to construct sysevent payload");
		return;
	}

	if (sysevent_post_event(EC_TOPO, (char *)subclass, SUNW_VENDOR, "fmd",
	    nvl, &eid) != 0) {
		fmd_hdl_error(hdl, "failed to post sysevent");
	}

	nvlist_free(nvl);
}

/*
 * Handle received sysevents here.  Specifically look for service processor
 * platform resets.  When one is recieved, reset the indicators for all of the
 * frus in the hash.
 */
/*ARGSUSED*/
static void
fru_recv_sysevent(fmd_hdl_t *hdl, nvlist_t *nvl, const char *class)
{
	fru_hash_t *fhp = fmd_hdl_getspecific(hdl);
	fru_t *fp;

	if (fmd_nvl_class_match(hdl, nvl,
	    "resource.sysevent.EC_platform.ESC_platform_sp_reset")) {
		/*
		 * For a SP reset, we need to walk over all FRUs in the system
		 * chassis and re-sync any LED state.
		 */
		for (fp = fhp->fh_list; fp != NULL; fp = fp->fru_next) {
			if (fp->fru_chassis != NULL)
				continue;

			/*
			 * Don't mess with the indicators if the present
			 * check encounters any errors.
			 */
			if (fru_check_present(fhp->fh_scanhdl, fp) < 0)
				continue;
			fru_set_indicator(hdl, fhp->fh_scanhdl, fp,
			    TOPO_LED_TYPE_PRESENT, fp->fru_present);
			fru_set_indicator(hdl, fhp->fh_scanhdl, fp,
			    TOPO_LED_TYPE_SERVICE, fp->fru_faulted);
		}
	}
}

/*
 * list.* events are processed here.  If the component is in FMD's fault list
 * then determine the fault status and set the service indicator to the
 * appropriate state.
 */
static void
fru_recv_list(fmd_hdl_t *hdl, nvlist_t *nvl)
{
	fru_hash_t *fhp = fmd_hdl_getspecific(hdl);
	fru_t *fp;
	nvlist_t **entries, *rawfru, *fru;
	uint_t nentries, n;
	char *fmristr;
	int err;
	topo_hdl_t *thp;
	boolean_t fru_faulted;

	if (nvlist_lookup_nvlist_array(nvl, FM_SUSPECT_FAULT_LIST,
	    &entries, &nentries) != 0) {
		fmd_hdl_debug(hdl, "failed to find %s member",
		    FM_SUSPECT_FAULT_LIST);
		return;
	}

	thp = fmd_hdl_topo_hold(hdl, TOPO_VERSION);

	for (n = 0; n < nentries; n++) {
		if (nvlist_lookup_nvlist(entries[n],
		    FM_FAULT_FRU, &rawfru) != 0)
			continue;

		fru = fmd_nvl_dup(hdl, rawfru, FMD_SLEEP);
		fru_strip(fru);

		if (topo_fmri_nvl2str(thp, fru, &fmristr, &err) != 0) {
			fmd_hdl_error(hdl,
			    "failed to convert FMRI to string for "
			    "entry %d: %s\n", n, topo_strerror(err));
			nvlist_free(fru);
			continue;
		}

		nvlist_free(fru);

		fp = fru_lookup(fhp, thp, fmristr);
		if (fp == NULL) {
			topo_hdl_strfree(thp, fmristr);
			continue;
		}

		fmd_hdl_debug(hdl, "received list event for %s", fmristr);

		fru_faulted = fmd_nvl_fmri_has_fault(hdl, fp->fru_fmri,
		    FMD_HAS_FAULT_FRU, NULL);

		fmd_hdl_debug(hdl, "Checking %s, for a fault.  faulted = %d",
		    fmristr, fru_faulted);

		topo_hdl_strfree(thp, fmristr);

		fru_set_indicator(hdl, thp, fp, TOPO_LED_TYPE_SERVICE,
		    fru_faulted);
		fp->fru_faulted = fru_faulted;
	}

	fmd_hdl_topo_rele(hdl, thp);
}

/*
 * Entry point for events.  Route the events to either the sysevent handler
 * or the list event handler.
 */
/*ARGSUSED*/
static void
fru_recv(fmd_hdl_t *hdl, fmd_event_t *ep, nvlist_t *nvl, const char *class)
{
	boolean_t replay;

	fmd_hdl_debug(hdl, "received %s event", class);

	if (fmd_nvl_class_match(hdl, nvl, "resource.sysevent.*"))
		fru_recv_sysevent(hdl, nvl, class);
	else if (fmd_nvl_class_match(hdl, nvl, "list.suspect") ||
	    fmd_nvl_class_match(hdl, nvl, "list.isolated"))
		fru_recv_list(hdl, nvl);
	else if (fmd_nvl_class_match(hdl, nvl, "list.repaired")) {
		if (nvlist_lookup_boolean_value(nvl, FM_SUSPECT_REPLAYED,
		    &replay) == 0 && replay) {
			BUMPSTAT(replays_dropped);
			return;
		} else
			fru_recv_list(hdl, nvl);
	}
}

/*
 * Entry point for topology change notifications. Start a scan of the system
 * which will update the fru_hash.
 */
/*ARGSUSED*/
static void
fru_topo_change(fmd_hdl_t *hdl, topo_hdl_t *thp)
{
	fru_scan(fmd_hdl_getspecific(hdl));
}

/*
 * Deallocate a fru_t structure.
 */
static void
fru_free(fmd_hdl_t *hdl, fru_t *fp)
{
	fmd_hdl_strfree(hdl, fp->fru_fmristr);
	nvlist_free(fp->fru_fmri);
	nvlist_free(fp->fru_ctl);
	fmd_hdl_free(hdl, fp, sizeof (fru_t));
}

/*
 * Deallocate a fru_chassis_t structure.
 */
static void
fru_chassis_free(fmd_hdl_t *hdl, fru_chassis_t *cp)
{
	fmd_hdl_strfree(hdl, cp->frc_id);
	fmd_hdl_strfree(hdl, cp->frc_fmristr);
	if (cp->frc_timer)
		fmd_timer_remove(hdl, cp->frc_timer);
	nvlist_free(cp->frc_fmri);
	fmd_hdl_free(hdl, cp, sizeof (fru_chassis_t));
}

/*
 * Remove all FRUs in the hash that match the given chassis.
 */
static void
fru_remove_all(fru_hash_t *fhp, fru_chassis_t *cp)
{
	fmd_hdl_t *hdl = fhp->fh_hdl;
	uint_t h;
	fru_t **loc, *fp;

	for (h = 0; h < fhp->fh_hashlen; h++) {
		loc = &fhp->fh_hash[h];
		while ((fp = *loc) != NULL) {
			if (fp->fru_chassis == cp) {
				*loc = fp->fru_chain;
				if (fp->fru_next != NULL)
					fp->fru_next->fru_prev = fp->fru_prev;

				if (fp->fru_prev != NULL)
					fp->fru_prev->fru_next = fp->fru_next;
				else
					fhp->fh_list = fp->fru_next;

				fru_free(hdl, fp);
			} else {
				loc = &fp->fru_chain;
			}
		}
	}
}

/*
 * Given a FRU, find and set the associated chassis structure, creating it if
 * necessary.
 */
static void
fru_set_chassis(topo_hdl_t *thp, fru_hash_t *fhp, fru_t *fp)
{
	fmd_hdl_t *hdl = fhp->fh_hdl;
	nvlist_t **comp, *auth;
	uint_t ncomp;
	char *name, *csn;
	fru_chassis_t *cp;
	char *fmristr;
	int err;

	/*
	 * Look at the first element of the hc-scheme.  If it is not
	 * ses-enclosure, then this is part of the internal chassis.
	 */
	(void) nvlist_lookup_nvlist_array(fp->fru_fmri, FM_FMRI_HC_LIST,
	    &comp, &ncomp);
	assert(ncomp != 0);

	(void) nvlist_lookup_string(comp[0], FM_FMRI_HC_NAME, &name);

	if (strcmp(name, SES_ENCLOSURE) != 0)
		return;

	if (nvlist_lookup_nvlist(fp->fru_fmri, FM_FMRI_AUTHORITY,
	    &auth) != 0 ||
	    nvlist_lookup_string(auth, FM_FMRI_AUTH_CHASSIS,
	    &csn) != 0)
		return;

	for (cp = fhp->fh_chassis; cp != NULL; cp = cp->frc_next) {
		if (strcmp(cp->frc_id, csn) == 0)
			break;
	}

	if (cp == NULL) {
		cp = fmd_hdl_zalloc(hdl, sizeof (fru_chassis_t), FMD_SLEEP);

		cp->frc_id = fmd_hdl_strdup(hdl, csn, FMD_SLEEP);

		/*
		 * Create a FMRI that consists only of the ses-enclosure
		 * portion and chassis ID.
		 */
		if ((cp->frc_fmri = fmd_nvl_dup(hdl, fp->fru_fmri,
		    FMD_SLEEP)) == NULL ||
		    nvlist_add_nvlist_array(cp->frc_fmri, FM_FMRI_HC_LIST,
		    comp, 1) != 0) {
			fmd_hdl_error(hdl, "failed to allocate chassis FRU");
			nvlist_free(cp->frc_fmri);
			fmd_hdl_strfree(hdl, cp->frc_id);
			fmd_hdl_free(hdl, cp, sizeof (fru_chassis_t));
			return;
		}

		fru_strip(cp->frc_fmri);

		if (topo_fmri_nvl2str(thp, cp->frc_fmri, &fmristr,
		    &err) != 0) {
			fmd_hdl_error(hdl, "failed to convert FMRI "
			    "to string for chassis %s: %s\n",
			    topo_strerror(err), csn);
			nvlist_free(cp->frc_fmri);
			fmd_hdl_strfree(hdl, cp->frc_id);
			fmd_hdl_free(hdl, cp, sizeof (fru_chassis_t));
			return;
		}

		cp->frc_fmristr = fmd_hdl_strdup(hdl, fmristr, FMD_SLEEP);
		topo_hdl_strfree(thp, fmristr);

		cp->frc_next = fhp->fh_chassis;
		fhp->fh_chassis = cp;
	}

	cp->frc_present = B_TRUE;
	fp->fru_chassis = cp;
}

/*
 * Add a bay node to the FRU database.  Even if a bay node is empty, we want to
 * create the FRU entry for the disk, knowing that it may currently be absent.
 * Bay nodes exist only for disks.  This is essentially a "dummy" entry that
 * will be replaced if we find an actual disk in topology.
 */
static void
fru_gather_bay(topo_hdl_t *thp, tnode_t *node, fru_hash_t *fhp)
{
	fmd_hdl_t *hdl = fhp->fh_hdl;
	nvlist_t *fmri, *disk = NULL, *bay;
	nvlist_t **comp, **newcomp;
	uint_t ncomp = 0;
	char *fmristr;
	int err;
	fru_t *fp;

	if (topo_node_resource(node, &fmri, &err) != 0) {
		fmd_hdl_error(hdl,
		    "failed to get resource for bay %d: %s\n",
		    topo_node_instance(node), topo_strerror(err));
		return;
	}

	fru_strip(fmri);

	bay = fmd_nvl_dup(hdl, fmri, FMD_SLEEP);

	/*
	 * Append 'disk=0' to the FMRI.
	 */
	(void) nvlist_lookup_nvlist_array(fmri, FM_FMRI_HC_LIST,
	    &comp, &ncomp);
	disk = fmd_nvl_alloc(hdl, FMD_SLEEP);
	(void) nvlist_add_string(disk, FM_FMRI_HC_NAME, DISK);
	(void) nvlist_add_string(disk, FM_FMRI_HC_ID, "0");

	newcomp = fmd_hdl_alloc(hdl, sizeof (void *) * (ncomp + 1), FMD_SLEEP);
	bcopy(comp, newcomp, ncomp * sizeof (void *));
	newcomp[ncomp] = disk;

	(void) nvlist_add_nvlist_array(fmri, FM_FMRI_HC_LIST, newcomp,
	    ncomp + 1);

	fmd_hdl_free(hdl, newcomp, sizeof (void *) * (ncomp + 1));
	nvlist_free(disk);

	if (topo_fmri_nvl2str(thp, fmri, &fmristr, &err) != 0) {
		fmd_hdl_error(hdl,
		    "failed to convert fmri to string for bay %d: %s\n",
		    topo_node_instance(node), topo_strerror(err));
		nvlist_free(fmri);
		nvlist_free(bay);
		return;
	}

	/*
	 * If this is part of a rescan, and we already know about this disk,
	 * then do nothing.
	 */
	if ((fp = fru_lookup(fhp, thp, fmristr)) != NULL) {
		nvlist_free(fmri);
		nvlist_free(bay);
		topo_hdl_strfree(thp, fmristr);
		return;
	}

	fmd_hdl_debug(hdl, "added bay node: %s", fmristr);

	fp = fmd_hdl_zalloc(hdl, sizeof (fru_t), FMD_SLEEP);
	fp->fru_fmristr = fmd_hdl_strdup(hdl, fmristr, FMD_SLEEP);

	/*
	 * Need to duplicate fmri to hold across topo handle changes.
	 */
	fp->fru_fmri = fmd_nvl_dup(hdl, fmri, FMD_SLEEP);
	fp->fru_ctl = bay;
	nvlist_free(fmri);
	fp->fru_type = FRU_DISK;
	fru_insert(fhp, thp, fp);

	fru_set_chassis(thp, fhp, fp);

	topo_hdl_strfree(thp, fmristr);
}

/*
 * Determine whether two authorities are different.  This indicates a FRU
 * replacement operation has ocurred.
 */
static boolean_t
fru_auth_changed(nvlist_t *nva, nvlist_t *nvb, const char *propname)
{
	char *stra, *strb;

	if (nvlist_lookup_string(nva, propname, &stra) != 0 ||
	    nvlist_lookup_string(nvb, propname, &strb) != 0)
		return (B_FALSE);

	if (strcmp(stra, strb) != 0)
		return (B_TRUE);
	else
		return (B_FALSE);
}

/*
 * Create an entry in the FRU hash, using data from the current node.
 */
static void
fru_gather_basic(topo_hdl_t *thp, tnode_t *node, fru_hash_t *fhp)
{
	fmd_hdl_t *hdl = fhp->fh_hdl;
	int err;
	nvlist_t *fmri, *fmri_noauth;
	char *fmristr;
	fru_t *fp;
	const char *name = topo_node_name(node);
	boolean_t check_present;

	if (topo_node_fru(node, &fmri, NULL, &err) != 0) {
		fmd_hdl_error(hdl,
		    "failed to get FRU for %s %d: %s\n", name,
		    topo_node_instance(node), topo_strerror(err));
		return;
	}

	/*
	 * The FRU index is recorded with the serial/part/revision information
	 * removed from the authority.  This keeps the hash index consistent
	 * independent of the contents of the bay at any given point.
	 */
	fmri_noauth = fmd_nvl_dup(hdl, fmri, FMD_SLEEP);

	fru_strip(fmri_noauth);

	if (topo_fmri_nvl2str(thp, fmri_noauth, &fmristr, &err) != 0) {
		fmd_hdl_error(hdl,
		    "failed to convert FMRI to string for %s %d: %s\n",
		    name, topo_node_instance(node), topo_strerror(err));
		nvlist_free(fmri);
		nvlist_free(fmri_noauth);
		return;
	}

	nvlist_free(fmri_noauth);

	if ((fp = fru_lookup(fhp, thp, fmristr)) != NULL) {
		/*
		 * Check to see if the authority information has changed, which
		 * indicates that a disk was removed and a new one inserted in
		 * between snapshots.  In this case, generate both a remove
		 * event for the old FRU and mark it such that we get the add
		 * event.
		 */
		if (fp->fru_last_present &&
		    (fru_auth_changed(fp->fru_fmri, fmri,
		    FM_FMRI_HC_SERIAL_ID) ||
		    fru_auth_changed(fp->fru_fmri, fmri,
		    FM_FMRI_HC_PART))) {
			fru_post_sysevent(hdl, fp, ESC_TOPO_FRU_REMOVE);
			fp->fru_last_present = B_FALSE;

		}

		if (strcmp(name, DISK) == 0) {
			fp->fru_present = B_TRUE;

			/*
			 * If this is a newly inserted FRU, or the first time
			 * we have seen it, then query the fmd state to see if
			 * it's faulted or not.
			 */
			if (!fp->fru_last_present) {
				fp->fru_faulted =
				    fmd_nvl_fmri_has_fault(hdl, fmri,
				    FMD_HAS_FAULT_FRU, NULL);
			}
		}
		fmd_hdl_debug(hdl, "Fault status of %s is %d", fmristr,
		    fp->fru_faulted);

		check_present = B_FALSE;

		/*
		 * Even if we've already seen this FRU before, we make sure to
		 * update the FRU with the full FMRI so that we can detect
		 * changes in the authority information in the future.
		 */
		nvlist_free(fp->fru_fmri);
		fp->fru_fmri = fmd_nvl_dup(hdl, fmri, FMD_SLEEP);

	} else {
		fp = fmd_hdl_zalloc(hdl, sizeof (fru_t), FMD_SLEEP);
		fp->fru_fmristr = fmd_hdl_strdup(hdl, fmristr, FMD_SLEEP);
		fp->fru_fmri = fmd_nvl_dup(hdl, fmri, FMD_SLEEP);
		if (strcmp(name, FAN) == 0)
			fp->fru_type = FRU_FAN;
		else if (strcmp(name, PSU) == 0)
			fp->fru_type = FRU_PSU;
		else if (strcmp(name, DISK) == 0)
			fp->fru_type = FRU_DISK;
		else if (strcmp(name, CONTROLLER) == 0)
			fp->fru_type = FRU_CONTROLLER;
		else
			fmd_hdl_abort(hdl, "unknown node type %s",
			    topo_node_name(node));
		fru_insert(fhp, thp, fp);

		fmd_hdl_debug(hdl, "added %s node: %s", name, fmristr);

		fru_set_chassis(thp, fhp, fp);
		check_present = B_TRUE;
	}

	topo_hdl_strfree(thp, fmristr);
	nvlist_free(fmri);

	if (check_present) {
		if (fru_check_present(thp, fp) == 0) {
			fmd_hdl_debug(hdl, "present = %d for %s",
			    fp->fru_present, fp->fru_fmristr);
			fp->fru_valid = B_TRUE;
		}
	}
}

/*
 * Called only from the fru_scan function (i.e. only on startup or on topology
 * changes.  A FRU has been detected and needs to be added to the hash.
 */
/*ARGSUSED*/
static int
fru_gather(topo_hdl_t *thp, tnode_t *node, void *data)
{
	const char *name = topo_node_name(node);

	if (strcmp(name, BAY) == 0)
		fru_gather_bay(thp, node, data);
	else if (strcmp(name, PSU) == 0 ||
	    strcmp(name, FAN) == 0 ||
	    strcmp(name, CONTROLLER) == 0 ||
	    strcmp(name, DISK) == 0)
		fru_gather_basic(thp, node, data);

	return (TOPO_WALK_NEXT);
}

/*
 * Polling thread for power supplies and fans.  We don't get any sysevents for
 * changes in these components, so we have no choice but to poll.
 */
static void
fru_poll(fru_hash_t *fhp)
{
	fmd_hdl_t *hdl = fhp->fh_hdl;
	fru_t *fp;
	topo_hdl_t *thp;

	thp = fmd_hdl_topo_hold(hdl, TOPO_VERSION);

	for (fp = fhp->fh_list; fp != NULL; fp = fp->fru_next) {
		if (fp->fru_type == FRU_DISK)
			continue;

		/*
		 * FRU presence check.  This call sets fp->fru_present to the
		 * appropriate state.  If the check fails, (returns non zero)
		 * then skip processing this FRU.
		 */
		fp->fru_last_present = fp->fru_present;
		if (fru_check_present(thp, fp) != 0)
			continue;

		if (fp->fru_last_present == fp->fru_present) {
			fp->fru_valid = B_TRUE;
			continue;
		}

		fmd_hdl_debug(hdl, "present = %d (was %d) for %s",
		    fp->fru_present, fp->fru_last_present, fp->fru_fmristr);

		if (fp->fru_valid) {
			if (fp->fru_present)
				fru_post_sysevent(hdl, fp, ESC_TOPO_FRU_ADD);
			else
				fru_post_sysevent(hdl, fp, ESC_TOPO_FRU_REMOVE);
		}
		fp->fru_valid = B_TRUE;
	}

	fmd_hdl_topo_rele(hdl, thp);
}

/*
 * Walk a topology snapshot of the system.  Add any found FRUs into the hash,
 * and remove any FRUs in the hash that are not in topology.  If a chassis is
 * found, only post a sysevent for the chassis and not all of the hosted FRUs.
 */
static void
fru_scan(fru_hash_t *fhp)
{
	fmd_hdl_t *hdl = fhp->fh_hdl;
	topo_hdl_t *thp;
	topo_walk_t *twp;
	int err;
	fru_chassis_t **loc, *cp;
	fru_t *fp;

	/*
	 * Iterate over libtopo searching for nodes that we know how to manage.
	 */
	thp = fmd_hdl_topo_hold(hdl, TOPO_VERSION);

	/*
	 * If the topo snapshot hasn't changed, don't bother rescanning
	 * anything.
	 */
	if (thp == fhp->fh_scanhdl) {
		fmd_hdl_topo_rele(hdl, thp);
		return;
	}

	if (fhp->fh_scanhdl != NULL)
		fmd_hdl_topo_rele(hdl, fhp->fh_scanhdl);
	fhp->fh_scanhdl = thp;

	fmd_hdl_debug(hdl, "scanning FRU topology");

	/*
	 * Walk the chassis and FRUs and put them into a fresh state.
	 */
	for (cp = fhp->fh_chassis; cp != NULL; cp = cp->frc_next) {
		cp->frc_last_present = B_TRUE;
		cp->frc_present = B_FALSE;
	}
	for (fp = fhp->fh_list; fp != NULL; fp = fp->fru_next)  {
		fp->fru_last_present = fp->fru_present;

		fp->fru_faulted = fmd_nvl_fmri_has_fault(hdl, fp->fru_fmri,
		    FMD_HAS_FAULT_FRU, NULL);

		if (fp->fru_type == FRU_DISK)
			fp->fru_present = B_FALSE;
	}

	/*
	 * Walk over the topology and gather all fru information.
	 */
	if ((twp = topo_walk_init(thp, FM_FMRI_SCHEME_HC, fru_gather,
	    fhp, &err)) == NULL)
		return;

	if (topo_walk_step(twp, TOPO_WALK_CHILD) == TOPO_WALK_ERR) {
		topo_walk_fini(twp);
		return;
	}

	topo_walk_fini(twp);

	/*
	 * Go through and check for any new or removed chassis, posting the
	 * appropriate sysevent and committing and FRU changes so that we don't
	 * post individual FRU events as well.
	 */
	loc = &fhp->fh_chassis;
	while ((cp = *loc) != NULL) {
		if (!cp->frc_present) {
			*loc = cp->frc_next;
			fru_chassis_post_sysevent(hdl, cp, ESC_TOPO_FRU_REMOVE);
			fru_remove_all(fhp, cp);
			fru_chassis_free(hdl, cp);
		} else if (cp->frc_present != cp->frc_last_present) {
			if (!fhp->fh_initial) {
				fru_chassis_post_sysevent(hdl, cp,
				    ESC_TOPO_FRU_ADD);
				/*
				 * When a chassis is added, it may take a while
				 * for all the disks to appear.  To avoid false
				 * positives, we wait a defined amount of time
				 * before treating new disk additions as unique
				 * events.
				 */
				cp->frc_adding = B_TRUE;
				cp->frc_timer = fmd_timer_install(hdl, cp, NULL,
				    fru_chassis_timeout);
			}

			loc = &(*loc)->frc_next;
		} else {
			loc = &(*loc)->frc_next;
		}
	}

	for (fp = fhp->fh_list; fp != NULL; fp = fp->fru_next)  {
		cp = fp->fru_chassis;
		if ((cp != NULL && cp->frc_adding) || fhp->fh_initial) {
			/*
			 * For newly added devices, or during the initial pass,
			 * we need to sync the LED state with our currently
			 * known state.
			 */
			fmd_hdl_debug(hdl, "Synching the PRESENT LED on %s",
			    fp->fru_fmristr);

			fru_set_indicator(hdl, thp, fp,
			    TOPO_LED_TYPE_PRESENT, fp->fru_present);

			fmd_hdl_debug(hdl, "Synching the FAULTED LED on %s",
			    fp->fru_fmristr);

			fru_set_indicator(hdl, thp, fp,
			    TOPO_LED_TYPE_SERVICE, fp->fru_faulted);
			continue;
		}

		/*
		 * Set the fru_present flag for the FRU. Skip if there is an
		 * error checking it.
		 */
		if (fru_check_present(thp, fp) < 0)
			continue;
		if (fp->fru_last_present == fp->fru_present)
			continue;

		if (fp->fru_present) {
			fru_post_sysevent(hdl, fp, ESC_TOPO_FRU_ADD);
		} else {
			fru_post_sysevent(hdl, fp, ESC_TOPO_FRU_REMOVE);
			fp->fru_faulted = B_FALSE;
		}

		fmd_hdl_debug(hdl, "Setting the PRESENT LED");
		fru_set_indicator(hdl, thp, fp,
		    TOPO_LED_TYPE_PRESENT, fp->fru_present);
		fmd_hdl_debug(hdl, "Setting the SERVICE LED");
		fru_set_indicator(hdl, thp, fp,
		    TOPO_LED_TYPE_SERVICE, fp->fru_faulted);
	}
}

/*
 * Entry point for polling timeouts.  If this is the first call at startup,
 * scan the entire system.  Otherwise simply check the power supplies and fans.
 */
/*ARGSUSED*/
static void
fru_timeout(fmd_hdl_t *hdl, id_t id, void *data)
{
	fru_hash_t *fhp = fmd_hdl_getspecific(hdl);
	fru_chassis_t *cp;

	if (data == FRU_TIMER_INITIAL) {
		fhp->fh_initial = B_TRUE;
		fru_scan(fhp);
		fhp->fh_initial = B_FALSE;

		(void) fmd_timer_install(hdl, FRU_TIMER_POLL, 0,
		    fru_poll_interval);
	} else if (data == FRU_TIMER_POLL) {

		fru_poll(fhp);
		(void) fmd_timer_install(hdl, FRU_TIMER_POLL, 0,
		    fru_poll_interval);
	} else {
		cp = data;
		cp->frc_adding = B_FALSE;
		cp->frc_timer = 0;
	}
}

static const fmd_hdl_ops_t fmd_ops = {
	fru_recv,	/* fmdo_recv */
	fru_timeout,	/* fmdo_timeout */
	NULL, 		/* fmdo_close */
	NULL, 		/* fmdo_stats */
	NULL, 		/* fmdo_gc */
	NULL,		/* fmdo_send */
	fru_topo_change, /* fmdo_topo */
};

static const fmd_prop_t fmd_props[] = {
	{ "chassis_timeout", FMD_TYPE_TIME, "10s" },
	{ "hash_buckets", FMD_TYPE_UINT32, "211" },
	{ "poll_interval", FMD_TYPE_TIME, "30s" },
	{ NULL }
};

static const fmd_hdl_info_t fmd_info = {
	"FRU Monitor", "1.0", &fmd_ops, fmd_props
};

void
_fmd_init(fmd_hdl_t *hdl)
{
	fru_hash_t *fhp;

	if (fmd_hdl_register(hdl, FMD_API_VERSION, &fmd_info) != 0)
		return;

	(void) fmd_stat_create(hdl, FMD_STAT_NOALLOC, sizeof (fru_stats) /
	    sizeof (fmd_stat_t), (fmd_stat_t *)&fru_stats);

	fru_poll_interval = fmd_prop_get_int64(hdl, "poll_interval");
	fru_chassis_timeout = fmd_prop_get_int64(hdl, "chassis_timeout");

	/* create and populate the FRU hash */
	fhp = fmd_hdl_zalloc(hdl, sizeof (fru_hash_t), FMD_SLEEP);
	fhp->fh_hashlen = fmd_prop_get_int32(hdl, "hash_buckets");
	fhp->fh_hash = fmd_hdl_zalloc(hdl, sizeof (void *) * fhp->fh_hashlen,
	    FMD_SLEEP);
	fhp->fh_count = 0;
	fhp->fh_hdl = hdl;

	fmd_hdl_setspecific(hdl, fhp);

	(void) fmd_timer_install(hdl, FRU_TIMER_INITIAL, NULL, 0);
}

void
_fmd_fini(fmd_hdl_t *hdl)
{
	fru_hash_t *fhp = fmd_hdl_getspecific(hdl);
	fru_chassis_t *cp;
	fru_t *fp, *np;
	uint_t i;

	if (fhp->fh_scanhdl != NULL)
		fmd_hdl_topo_rele(hdl, fhp->fh_scanhdl);

	for (i = 0; i < fhp->fh_hashlen; i++) {
		for (fp = fhp->fh_hash[i]; fp != NULL; fp = np) {
			np = fp->fru_chain;
			fru_free(hdl, fp);
		}
	}

	while ((cp = fhp->fh_chassis) != NULL) {
		fhp->fh_chassis = cp->frc_next;
		fru_chassis_free(hdl, cp);
	}

	fmd_hdl_free(hdl, fhp->fh_hash, sizeof (void *) * fhp->fh_hashlen);
	fmd_hdl_free(hdl, fhp, sizeof (fru_hash_t));
}
