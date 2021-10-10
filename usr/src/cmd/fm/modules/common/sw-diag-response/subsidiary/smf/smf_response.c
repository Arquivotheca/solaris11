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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * SMF software-response subsidiary
 */

#include <strings.h>
#include <fm/libtopo.h>
#include <libscf.h>
#include <sys/fm/protocol.h>
#include <fm/fmd_fmri.h>
#include <uuid/uuid.h>

#include "../../common/sw.h"
#include "smf.h"

static const fmd_prop_t swrp_smf_props[] = {
	{ "swrp_smf_enable", FMD_TYPE_BOOL, "true" },
	{ "swrp_smf_verify_interval", FMD_TYPE_TIME, "60sec" },
	{ NULL, 0, NULL }
};

static id_t myid;
static id_t swrp_smf_vrfy_timerid;
static hrtime_t swrp_smf_vrfy_interval;

static struct {
	fmd_stat_t swrp_smf_fmrepairs;
	fmd_stat_t swrp_smf_clears;
	fmd_stat_t swrp_smf_closed;
	fmd_stat_t swrp_smf_wrongclass;
	fmd_stat_t swrp_smf_badlist;
	fmd_stat_t swrp_smf_badresource;
	fmd_stat_t swrp_smf_badclrevent;
	fmd_stat_t swrp_smf_noloop;
	fmd_stat_t swrp_smf_suppressed;
	fmd_stat_t swrp_smf_cache_nents;
	fmd_stat_t swrp_smf_cache_full;
	fmd_stat_t swrp_smf_norepairlink;
	fmd_stat_t swrp_smf_immediaterepair;
	fmd_stat_t swrp_smf_prblm_cache_nents;
	fmd_stat_t swrp_smf_prblm_cache_bad;
	fmd_stat_t swrp_smf_lost_sync_repair;
} swrp_smf_stats = {
	{ "swrp_smf_fmrepairs", FMD_TYPE_UINT64,
	    "repair events received for propogation to SMF" },
	{ "swrp_smf_clears", FMD_TYPE_UINT64,
	    "notifications from SMF of exiting maint state" },
	{ "swrp_smf_closed", FMD_TYPE_UINT64,
	    "cases closed" },
	{ "swrp_smf_wrongclass", FMD_TYPE_UINT64,
	    "unexpected event class received" },
	{ "swrp_smf_badlist", FMD_TYPE_UINT64,
	    "list event with invalid structure" },
	{ "swrp_smf_badresource", FMD_TYPE_UINT64,
	    "list.repaired with smf fault but bad svc fmri" },
	{ "swrp_smf_badclrevent", FMD_TYPE_UINT64,
	    "maint clear event from SMF malformed" },
	{ "swrp_smf_noloop", FMD_TYPE_UINT64,
	    "avoidance of smf->fmd->smf repairs propogations" },
	{ "swrp_smf_suppressed", FMD_TYPE_UINT64,
	    "not propogated to smf because no longer in maint" },
	{ "swrp_smf_cache_nents", FMD_TYPE_UINT64,
	    "number of entries in smf problem cache" },
	{ "swrp_smf_cache_full", FMD_TYPE_UINT64,
	    "smf problem cache full" },
	{ "swrp_smf_norepairlink", FMD_TYPE_UINT64,
	    "maintenance clear events with no pointer to original event" },
	{ "swrp_smf_immediaterepair", FMD_TYPE_UINT64,
	    "suspect and maintenance clear received out-of-order" },
	{ "swrp_smf_prblm_cache_nents", FMD_TYPE_UINT64,
	    "number of entries in problem cache" },
	{ "swrp_smf_prblm_cache_bad", FMD_TYPE_UINT64,
	    "problem cache bad on checkpoint restore" },
	{ "swrp_smf_lost_sync_repair", FMD_TYPE_UINT64,
	    "repairs discovered through polling" },
};

#define	SETSTAT(stat, val)	swrp_smf_stats.stat.fmds_value.ui64 = (val)
#define	BUMPSTAT(stat)		swrp_smf_stats.stat.fmds_value.ui64++

/*
 * We need to record selected aspects of the events we process, and persist
 * this information across fmd restarts.
 *
 * a) When a list.suspect is received for an SMF maintenance defect we
 *    update the cache with the case UUID and the instance FMRI, and mark
 *    the entry as SMF_PRBLM_CACHE_FL_SUSPECT_SEEN.  Usually there will not
 *    already be an entry for this uuid and fmri, but it is possible that
 *    an entry created in case b) below matches in which case we must
 *    immediately proceed to take a repair action.
 *
 * b) When we see an ireport with from-state indicating an instance has
 *    left maintenance state we create or update the cache entry for
 *    this uuid and fmri with SMF_PRBLM_CACHE_FL_CLEAR_SMF.  If the entry
 *    was already marked with SMF_PRBLM_CACHE_FL_SUSPECT_SEEN then
 *    we have processed the associated list.suspect and so we now repair
 *    the asru; if not then as in a) when the suspect is processed we will
 *    action the repair then when we notice SMF_PRBLM_CACHE_FL_CLEAR_SMF
 *    already present on the entry.
 *
 * c) When a list.repaired arrives it is either the result of an fmadm
 *    commandline action (or equivalent library call), or a consequence of
 *    a repair action taken in a) or b) above.  We distinguish the latter
 *    case through observing SMF_PRBLM_CACHE_FL_CLEAR_SMF present on the
 *    entry, so we know not to propogate the clear back to SMF (which
 *    initiated it).  We add SMF_PRBLM_CACHE_FL_CLEAR_FM in both cases.
 *
 * Once a problem has been resolved we should eventually see all three
 * flags present on the entry.  We can garbage-collect the cache
 * through verifying resolution state with fmd_case_uuisresolved.
 */

#define	SMF_PRBLM_CACHE_NENT_INC		16
#define	SMF_PRBLM_CACHE_NENT_MAX		128

struct smf_prblm_cache_ent {
	char uuid[UUID_PRINTABLE_STRING_LENGTH];
	char fmristr[90];
	uint8_t flags;
};

#define	SMF_PRBLM_CACHE_FL_CLEAR_SMF		0x01 /* clear from SMF seen */
#define	SMF_PRBLM_CACHE_FL_CLEAR_FM		0x02 /* clear from FMD seen */
#define	SMF_PRBLM_CACHE_FL_SUSPECT_SEEN		0x04 /* list.suspect seen */
#define	SMF_PRBLM_CACHE_FL_MAINT_NOLONGER	0x08 /* fmri not in maint */

#define	SMF_PRBLM_CACHE_VERSION_1		0x11111111U
#define	SMF_PRBLM_CACHE_VERSION			SMF_PRBLM_CACHE_VERSION_1

struct smf_prblm_cache {
	uint32_t version;			/* Version */
	uint32_t nentries;			/* Real size of array below */
	struct smf_prblm_cache_ent entry[1];	/* Cache entries */
};

static struct smf_prblm_cache *smf_prblm_cache;

#define	SMF_PRBLM_CACHE_BUFSZ(nent) ((sizeof (struct smf_prblm_cache) + \
	((nent) - 1) * sizeof (struct smf_prblm_cache_ent)))

#define	SMF_PRBLM_CACHE_BUFNAME		"swrp_smf_prblm_cache"

static void
smf_prblm_cache_unpersist(fmd_hdl_t *hdl)
{
	fmd_buf_destroy(hdl, NULL, SMF_PRBLM_CACHE_BUFNAME);
	sw_debug(hdl, SW_DBG3, "smf response: buffer %s destroyed",
	    SMF_PRBLM_CACHE_BUFNAME);
}

static struct smf_prblm_cache_ent *
smf_prblm_cache_grow(fmd_hdl_t *hdl)
{
	struct smf_prblm_cache *newcache;
	size_t newsz;
	uint32_t oldn, n;

	oldn = smf_prblm_cache == NULL ? 0 : smf_prblm_cache->nentries;
	if (oldn == SMF_PRBLM_CACHE_NENT_MAX)
		return (NULL);

	n = oldn + SMF_PRBLM_CACHE_NENT_INC;
	newsz = SMF_PRBLM_CACHE_BUFSZ(n);
	newcache = fmd_hdl_zalloc(hdl, newsz, FMD_SLEEP);
	newcache->version = SMF_PRBLM_CACHE_VERSION;
	newcache->nentries = n;

	if (smf_prblm_cache != NULL) {
		size_t oldsz = SMF_PRBLM_CACHE_BUFSZ(oldn);

		bcopy(&smf_prblm_cache->entry[0], &newcache->entry[0], oldsz);
		smf_prblm_cache_unpersist(hdl);
		fmd_hdl_free(hdl, smf_prblm_cache, oldsz);
		smf_prblm_cache = NULL;
	}

	smf_prblm_cache = newcache;
	fmd_buf_create(hdl, NULL, SMF_PRBLM_CACHE_BUFNAME, newsz);
	fmd_buf_write(hdl, NULL, SMF_PRBLM_CACHE_BUFNAME, smf_prblm_cache,
	    newsz);

	SETSTAT(swrp_smf_prblm_cache_nents, n);
	sw_debug(hdl, SW_DBG3, "smf response: cache grown to %d entries", n);

	return (&smf_prblm_cache->entry[oldn]);	/* first added entry */
}

static void
smf_prblm_cache_persist(fmd_hdl_t *hdl)
{
	size_t sz = SMF_PRBLM_CACHE_BUFSZ(smf_prblm_cache->nentries);

	fmd_buf_write(hdl, NULL, SMF_PRBLM_CACHE_BUFNAME, smf_prblm_cache, sz);
}

/*
 * Garbage-collect the uuid cache.  Any cases that are already resolved
 * we do not need an entry for.  If a case is not resolved but the
 * service involved in that case is no longer in maintenance state
 * then we've lost sync somehow, so repair the asru (which will
 * also resolve the case).
 */
static void
smf_prblm_cache_gc(fmd_hdl_t *hdl, boolean_t candestroy)
{
	struct smf_prblm_cache_ent *entp;
	topo_hdl_t *thp = NULL;
	nvlist_t *svcfmri;
	int validcnt = 0;
	char *svcname;
	int err, i;

	for (i = 0; i < smf_prblm_cache->nentries; i++) {
		entp = &smf_prblm_cache->entry[i];

		if (entp->uuid[0] == '\0')
			continue;

		if (fmd_case_uuisresolved(hdl, entp->uuid)) {
			sw_debug(hdl, SW_DBG3, "smf response: gc "
			    "found case %s resolved", entp->uuid);
			bzero(entp, sizeof (*entp));
		} else {
			validcnt++;

			if (thp == NULL)
				thp = fmd_hdl_topo_hold(hdl, TOPO_VERSION);

			if (topo_fmri_str2nvl(thp, entp->fmristr, &svcfmri,
			    &err) != 0) {
				fmd_hdl_error(hdl, "str2nvl failed for %s\n",
				    entp->fmristr);
				continue;
			}

			if (fmd_nvl_fmri_service_state(hdl, svcfmri) !=
			    FMD_SERVICE_STATE_UNUSABLE) {
				svcname = sw_smf_svcfmri2shortstr(hdl, svcfmri);
				sw_debug(hdl, SW_DBG2, "smf response: "
				    "gc decides to repair %s", entp->fmristr);
				(void) fmd_repair_asru(hdl, entp->fmristr);
				fmd_hdl_strfree(hdl, svcname);
			}

			nvlist_free(svcfmri);
		}
	}

	if (thp)
		fmd_hdl_topo_rele(hdl, thp);

	if (!validcnt && candestroy == B_TRUE) {
		smf_prblm_cache_unpersist(hdl);
		fmd_hdl_free(hdl, smf_prblm_cache,
		    SMF_PRBLM_CACHE_BUFSZ(smf_prblm_cache->nentries));
		smf_prblm_cache = NULL;
	} else {
		smf_prblm_cache_persist(hdl);
	}
}

static void
smf_prblm_cache_restore(fmd_hdl_t *hdl)
{
	size_t sz;

	/*
	 * This cache was previously persisted named "uuid_cache" which
	 * is busted since it does not have any prefix for this subsidiary
	 * so is just asking for trouble down the line.  If this old
	 * buffer exists then simply ignore its content and destroy it
	 */
	if ((sz = fmd_buf_size(hdl, NULL, "uuid_cache")) != 0) {
		sw_debug(hdl, SW_DBG3, "destroying old uuid_cache buffer");
		fmd_buf_destroy(hdl, NULL, "uuid_cache");
	}

	if ((sz = fmd_buf_size(hdl, NULL, SMF_PRBLM_CACHE_BUFNAME)) == 0)
		return;

	smf_prblm_cache = fmd_hdl_alloc(hdl, sz, FMD_SLEEP);
	fmd_buf_read(hdl, NULL, SMF_PRBLM_CACHE_BUFNAME,
	    smf_prblm_cache, sz);

	if (smf_prblm_cache->version != SMF_PRBLM_CACHE_VERSION ||
	    smf_prblm_cache->nentries > SMF_PRBLM_CACHE_NENT_MAX ||
	    sz != SMF_PRBLM_CACHE_BUFSZ(smf_prblm_cache->nentries)) {
		BUMPSTAT(swrp_smf_prblm_cache_bad);
		sw_debug(hdl, SW_DBG1, "smf response: restored cache was bad");
		smf_prblm_cache_unpersist(hdl);
		fmd_hdl_free(hdl, smf_prblm_cache, sz);
		smf_prblm_cache = NULL;
		return;
	}

	/* Garbage collect now, and allow cache deletion if no valid entries */
	smf_prblm_cache_gc(hdl, B_TRUE);
}

/*
 * Update cache.
 *
 * Matching entries are those matching the specified uuid and the fmri
 * string.  If no matching entry is found then create one if creat is true.
 *
 * For any matching entry, or for one newly created, record flags
 * against the entry.  When an entry is matched and updated, return
 * in *oflagsp the flags value in effect before flags were applied;
 * otherwise set *oflagsp = 0.
 */
static void
smf_prblm_cache_add(fmd_hdl_t *hdl, char *uuid, char *fmristr, boolean_t creat,
    uint8_t flags, uint8_t *oflagsp)
{
	struct smf_prblm_cache_ent *entp;
	int dirty = 0;
	int hit = 0;
	int gced = 0;
	int i;

	sw_debug(hdl, SW_DBG3, "smf response: prblm_cache_add for case %s "
	    "and %s - set flag 0x%x", uuid, fmristr, flags);

	if (oflagsp)
		*oflagsp = 0;

	if (smf_prblm_cache == NULL) {
		if (creat == B_FALSE) {
			sw_debug(hdl, SW_DBG3, "cache empty - no create "
			    "requested");
			return;
		} else {
			entp = smf_prblm_cache_grow(hdl); /* can't fail */
			sw_debug(hdl, SW_DBG3, "cache empty - grown");
			goto fill;
		}
	}

	for (i = 0; i < smf_prblm_cache->nentries; i++) {
		entp = &smf_prblm_cache->entry[i];

		if (entp->uuid[0] == '\0')
			continue;

		if (strcmp(uuid, entp->uuid) == 0 &&
		    strcmp(fmristr, entp->fmristr) == 0) {
			hit = 1;
			sw_debug(hdl, SW_DBG3, "cache hit - flags were 0x%x",
			    entp->flags);
			if (oflagsp)
				*oflagsp = entp->flags;
			if (flags) {
				entp->flags |= flags;
				dirty = 1;
			}
			break;
		}
	}

	if (hit || creat == B_FALSE)
		goto done;

scan:
	for (i = 0, entp = NULL; i < smf_prblm_cache->nentries; i++) {
		if (smf_prblm_cache->entry[i].uuid[0] == '\0') {
			entp = &smf_prblm_cache->entry[i];
			break;
		}
	}

	if (entp == NULL) {
		/*
		 * Before growing the cache we try again after first
		 * garbage-collecting the existing cache for any cases
		 * that are confirmed as resolved.
		 */
		if (!gced) {
			smf_prblm_cache_gc(hdl, B_FALSE);
			gced = 1;
			goto scan;
		}

		if ((entp = smf_prblm_cache_grow(hdl)) == NULL) {
			BUMPSTAT(swrp_smf_cache_full);
			sw_debug(hdl, SW_DBG1, "smf response: cache full");
			goto done;
		}
	}

fill:
	sw_debug(hdl, SW_DBG3, "smf response: caching uuid %s for %s with "
	    "flags 0x%x", uuid, fmristr, flags);
	(void) strncpy(entp->uuid, uuid, sizeof (entp->uuid));
	(void) strncpy(entp->fmristr, fmristr, sizeof (entp->fmristr));
	entp->flags = flags;
	dirty = 1;

done:
	if (dirty)
		smf_prblm_cache_persist(hdl);
}

/*
 * We will receive list events for cases we are not interested in.  Test
 * that this list has exactly one suspect and that it matches the maintenance
 * defect.  Return the defect to the caller in the second argument,
 * and the defect resource element in the third arg.
 */
static int
problem_is_maint_defect(fmd_hdl_t *hdl, nvlist_t *nvl,
    nvlist_t **defectnvl, nvlist_t **rsrcnvl)
{
	nvlist_t **faults;
	uint_t nfaults;

	if (nvlist_lookup_nvlist_array(nvl, FM_SUSPECT_FAULT_LIST,
	    &faults, &nfaults) != 0) {
		BUMPSTAT(swrp_smf_badlist);
		return (0);
	}

	if (nfaults != 1 ||
	    !fmd_nvl_class_match(hdl, faults[0], SW_SMF_MAINT_DEFECT))
		return (0);

	if (nvlist_lookup_nvlist(faults[0], FM_FAULT_RESOURCE, rsrcnvl) != 0) {
		BUMPSTAT(swrp_smf_badlist);
		return (0);
	}

	*defectnvl = faults[0];

	return (1);
}

/*
 * Received newly-diagnosed list.suspect events that are for the
 * maintenance defect we diagnose.  Close the case (the resource was already
 * isolated by SMF) after caching the case UUID.
 */
/*ARGSUSED*/
static void
swrp_smf_suspect_recv(fmd_hdl_t *hdl, fmd_event_t *ep, nvlist_t *nvl,
    const char *class, void *arg)
{
	nvlist_t *defect, *rsrc;
	char *fmristr, *uuid;
	uint8_t oflags;

	sw_debug(hdl, SW_DBG3, "smf response: received %s", class);

	if (nvlist_lookup_string(nvl, FM_SUSPECT_UUID, &uuid) != 0) {
		BUMPSTAT(swrp_smf_badlist);
		return;
	}

	/*
	 * We receive all list.suspect but are only interested
	 * in maintenance defects.
	 */
	if (!problem_is_maint_defect(hdl, nvl, &defect, &rsrc)) {
		sw_debug(hdl, SW_DBG3, "smf response: not a maint defect");
		return;
	}

	if ((fmristr = sw_smf_svcfmri2str(hdl, rsrc)) == NULL) {
		BUMPSTAT(swrp_smf_badlist);
		return;
	}

	smf_prblm_cache_add(hdl, uuid, fmristr, B_TRUE,
	    SMF_PRBLM_CACHE_FL_SUSPECT_SEEN, &oflags);

	if (!fmd_case_uuclosed(hdl, uuid)) {
		sw_debug(hdl, SW_DBG3, "smf response: closing %s", uuid);
		fmd_case_uuclose(hdl, uuid);
		BUMPSTAT(swrp_smf_closed);
	}

	/*
	 * If we processed a maintenance clear ireport before we received
	 * this list.suspect then effect the asru repair now.
	 */
	if (oflags & SMF_PRBLM_CACHE_FL_CLEAR_SMF) {
		sw_debug(hdl, SW_DBG2, "smf response: immediate repair "
		    "on %s for case %s", fmristr, uuid);
		(void) fmd_repair_asru(hdl, fmristr);
		BUMPSTAT(swrp_smf_immediaterepair);
	}

	fmd_hdl_strfree(hdl, fmristr);

	if (!swrp_smf_vrfy_timerid) {
		swrp_smf_vrfy_timerid = sw_timer_install(hdl, myid, NULL, NULL,
		    swrp_smf_vrfy_interval);
		sw_debug(hdl, SW_DBG3, "smf response: armed timerid %d "
		    "to poll case resolution", swrp_smf_vrfy_timerid);
	}
}

/*
 * Propogate a maintenance clear initiated in SMF (e.g., svcadm clear)
 * into fmd state by calling fmd_repair_asru.  Set SMF_PRBLM_CACHE_FL_CLEAR_SMF
 * in the cache entry so that when a list.repaired is received as a result
 * of the fmd_repair_asru we know not to push that back towards SMF!
 */
/*ARGSUSED*/
static void
swrp_smf2fmd(fmd_hdl_t *hdl, fmd_event_t *ep, nvlist_t *nvl,
    const char *class, void *arg)
{
	nvlist_t *attr, *fmri;
	char *linkuuid = NULL;
	char *fromstate;
	char *fmristr;


	if (!fmd_nvl_class_match(hdl, nvl, TRANCLASS("*"))) {
		BUMPSTAT(swrp_smf_wrongclass);
		return;
	}

	if (nvlist_lookup_nvlist(nvl, FM_IREPORT_ATTRIBUTES, &attr) != 0 ||
	    nvlist_lookup_string(attr, "from-state", &fromstate) != 0) {
		BUMPSTAT(swrp_smf_badclrevent);
		return;
	}

	/*
	 * Filter those not describing a transition out of maintenance.
	 */
	if (strcmp(fromstate, "maintenance") != 0)
		return;

	if (nvlist_lookup_nvlist(attr, "svc", &fmri) != 0) {
		BUMPSTAT(swrp_smf_badclrevent);
		return;
	}

	if ((fmristr = sw_smf_svcfmri2str(hdl, fmri)) == NULL) {
		BUMPSTAT(swrp_smf_badclrevent);
		return;
	}

	sw_debug(hdl, SW_DBG1, "smf response: smf2fmd sees maintenance "
	    "clear of %s", fmristr);

	(void) nvlist_lookup_string(attr, "linked-event", &linkuuid);

	if (linkuuid) {
		uint8_t oflags;

		smf_prblm_cache_add(hdl, linkuuid, fmristr, B_TRUE,
		    SMF_PRBLM_CACHE_FL_CLEAR_SMF, &oflags);

		sw_debug(hdl, SW_DBG1, "smf response: smf2fmd for maintenance "
		    "clear of %s for case uuid %s flags 0x%x", fmristr,
		    linkuuid, oflags);

		if (oflags & SMF_PRBLM_CACHE_FL_SUSPECT_SEEN) {
			if (oflags & SMF_PRBLM_CACHE_FL_CLEAR_FM) {
				sw_debug(hdl, SW_DBG3, "smf2fmd - repair "
				    "was from fmd so skipping asru repair");
			} else {
				sw_debug(hdl, SW_DBG3, "smf2fmd - repairing "
				    "asru %s", fmristr);
				(void) fmd_repair_asru(hdl, fmristr);
			}
		}

		BUMPSTAT(swrp_smf_clears);
	} else {
		BUMPSTAT(swrp_smf_norepairlink);
		sw_debug(hdl, SW_DBG3, "smf response: smf2fmd  - no linked "
		    "event");
	}


	fmd_hdl_strfree(hdl, fmristr);
}

/*
 * Propogate an instance clear request made via fmd (fmadm or other
 * libfmd_adm client) to SMF using smf_restore_instance().  Filter
 * out those list.repaired that arise from an earlier swrp_smf2fmd
 * as explained above.
 */
/*ARGSUSED*/
static void
swrp_fmd2smf(fmd_hdl_t *hdl, fmd_event_t *ep, nvlist_t *nvl,
    const char *class, void *arg)
{
	char *fmristr, *shrtfmristr;
	nvlist_t *defect, *rsrc;
	uint8_t oflags;
	char *uuid;

	if (strcmp(class, FM_LIST_REPAIRED_CLASS) != 0) {
		BUMPSTAT(swrp_smf_wrongclass);
		return;
	}

	if (nvlist_lookup_string(nvl, FM_SUSPECT_UUID, &uuid) != 0) {
		BUMPSTAT(swrp_smf_badlist);
		return;
	}

	if (!problem_is_maint_defect(hdl, nvl, &defect, &rsrc))
		return;

	if ((fmristr = sw_smf_svcfmri2str(hdl, rsrc)) == NULL) {
		BUMPSTAT(swrp_smf_badresource);
		return;
	}

	sw_debug(hdl, SW_DBG3, "smf response: fmd2smf for %s case %s",
	    fmristr, uuid);

	smf_prblm_cache_add(hdl, uuid, fmristr, B_FALSE,
	    SMF_PRBLM_CACHE_FL_CLEAR_FM, &oflags);

	fmd_hdl_strfree(hdl, fmristr);

	/*
	 * If the cache already had a marked entry for this UUID then
	 * this is a list.repaired arising from a SMF-initiated maintenance
	 * clear (propogated with fmd_repair_asru above which then results
	 * in a list.repaired) and so we should not propogate the repair
	 * back towards SMF.  But do still force the case to RESOLVED state in
	 * case fmd is unable to confirm the service no longer in maintenance
	 * state (it may have failed again) so that a new case can be opened.
	 */
	fmd_case_uuresolved(hdl, uuid);
	if (oflags & (SMF_PRBLM_CACHE_FL_CLEAR_SMF |
	    SMF_PRBLM_CACHE_FL_MAINT_NOLONGER)) {
		BUMPSTAT(swrp_smf_noloop);
		sw_debug(hdl, SW_DBG3, "smf response: saw CLEAR_SMF or "
		    "MAINT_NOLONGER and not propogating to SMF");
		return;
	}

	/*
	 * Only propogate to SMF if we can see that service still
	 * in maintenance state.  We're not synchronized with SMF
	 * and this state could change at any time, but if we can
	 * see it's not in maintenance state then things are obviously
	 * moving (e.g., external svcadm active) so we don't poke
	 * at SMF otherwise we confuse things or duplicate operations.
	 */
	if (fmd_nvl_fmri_service_state(hdl, rsrc) ==
	    FMD_SERVICE_STATE_UNUSABLE) {
		shrtfmristr = sw_smf_svcfmri2shortstr(hdl, rsrc);

		if (shrtfmristr != NULL) {
			sw_debug(hdl, SW_DBG3, "smf response: restoring "
			    "instance %s", shrtfmristr);
			(void) smf_restore_instance(shrtfmristr);
			fmd_hdl_strfree(hdl, shrtfmristr);
			BUMPSTAT(swrp_smf_fmrepairs);
		} else {
			BUMPSTAT(swrp_smf_badresource);
		}
	} else {
		BUMPSTAT(swrp_smf_suppressed);
	}
}

/*
 * This timeout is armed when we process a list.suspect.  We will walk
 * through the problem cache and verify all entries for which we have
 * not received a clear notification - the associated instance should
 * be in the maintenance state.  If we observe anything other than
 * maintenance state then there are two possibilities: 1) an event is on
 * its way and if we check again a short time later it will have been processed,
 * or 2) we've lost sync somehow.  So the first time we see state other
 * than maintenance we mark the entry, and if we see a marked entry
 * not in maintenance state we effect a repair.
 */
/*ARGSUSED*/
void
swrp_smf_timeout(fmd_hdl_t *hdl, id_t timerid, void *arg)
{
	struct smf_prblm_cache_ent *entp;
	topo_hdl_t *thp = NULL;
	nvlist_t *fmri;
	int remain = 0;
	int state;
	int err;
	int i;

	for (i = 0; i < smf_prblm_cache->nentries; i++) {
		entp = &smf_prblm_cache->entry[i];

		if (entp->uuid[0] == '\0')
			continue;

		/*
		 * If we've not seen a suspect or if we have seen a clear event
		 * then we skip the entry.
		 */
		if (!(entp->flags & SMF_PRBLM_CACHE_FL_SUSPECT_SEEN) ||
		    (entp-> flags & (SMF_PRBLM_CACHE_FL_CLEAR_SMF |
		    SMF_PRBLM_CACHE_FL_CLEAR_FM)))
			continue;

		if (thp == NULL)
			thp = fmd_hdl_topo_hold(hdl, TOPO_VERSION);

		if (topo_fmri_str2nvl(thp, entp->fmristr, &fmri, &err) != 0)
			continue;

		state = fmd_nvl_fmri_service_state(hdl, fmri);
		nvlist_free(fmri);

		/*
		 * This should be the common case - the instance is still
		 * unusable (in maintenance state).  We'll continue to check
		 * up on it.
		 */
		if (state == FMD_SERVICE_STATE_UNUSABLE) {
			sw_debug(hdl, SW_DBG3, "%s for %s still in "
			    "maintenance - will check again",
			    entp->fmristr, entp->uuid);
			remain++;
			continue;
		}

		/*
		 * If we have not previously noted this entry appears to be
		 * out-of-sync then mark it now and continue, arranging to
		 * re-arm the timeout.
		 */
		if (!(entp->flags & SMF_PRBLM_CACHE_FL_MAINT_NOLONGER)) {
			entp->flags |= SMF_PRBLM_CACHE_FL_MAINT_NOLONGER;
			sw_debug(hdl, SW_DBG3, "noted %s for %s no longer in "
			    "maintenance - will check resolution");
			remain++;
			continue;
		}

		/*
		 * We have twice observed this entry not to reflect reality,
		 * so repair the asru.  When we handle the resulting
		 * list.repaired we will observe the flag we set above
		 * SMF_PRBLM_CACHE_FL_MAINT_NOLONGER and avoid forwarding the
		 * repair to SMF.
		 */
		sw_debug(hdl, SW_DBG3, "smf response: timeout decided to "
		    "repair %s for case %s", entp->fmristr, entp->uuid);
		(void) fmd_repair_asru(hdl, entp->fmristr);
		BUMPSTAT(swrp_smf_lost_sync_repair);
	}

	if (thp)
		fmd_hdl_topo_rele(hdl, thp);

	if (remain) {
		swrp_smf_vrfy_timerid = sw_timer_install(hdl, myid, NULL, NULL,
		    swrp_smf_vrfy_interval);
		sw_debug(hdl, SW_DBG3, "smf response: rearmed verify "
		    "timerid %d", swrp_smf_vrfy_timerid);
	} else {
		swrp_smf_vrfy_timerid = 0;
	}
}

const struct sw_disp swrp_smf_disp[] = {
	{ TRANCLASS("*"), swrp_smf2fmd, NULL },
	{ FM_LIST_SUSPECT_CLASS, swrp_smf_suspect_recv, NULL },
	{ FM_LIST_REPAIRED_CLASS, swrp_fmd2smf, NULL },
	{ NULL, NULL, NULL }
};

int
swrp_smf_init(fmd_hdl_t *hdl, id_t id, const struct sw_disp **dpp, int *nelemp)
{
	if (fmd_prop_get_int32(hdl, "swrp_smf_enable") != B_TRUE)
		return (SW_SUB_INIT_FAIL_VOLUNTARY);

	myid = id;

	(void) fmd_stat_create(hdl, FMD_STAT_NOALLOC, sizeof (swrp_smf_stats) /
	    sizeof (fmd_stat_t), (fmd_stat_t *)&swrp_smf_stats);

	smf_prblm_cache_restore(hdl);

	swrp_smf_vrfy_interval = fmd_prop_get_int64(hdl,
	    "swrp_smf_verify_interval");

	/*
	 * We need to subscribe to all SMF transition class events because
	 * we need to look inside the payload to see which events indicate
	 * a transition out of maintenance state.
	 */
	fmd_hdl_subscribe(hdl, TRANCLASS("*"));

	/*
	 * Subscribe to the defect class diagnosed for maintenance events.
	 * The module will then receive list.suspect events including
	 * these defects, and in our dispatch table above we list routing
	 * for list.suspect.
	 */
	fmd_hdl_subscribe(hdl, SW_SMF_MAINT_DEFECT);

	*dpp = &swrp_smf_disp[0];
	*nelemp = sizeof (swrp_smf_disp) / sizeof (swrp_smf_disp[0]);
	return (SW_SUB_INIT_SUCCESS);
}

/*ARGSUSED*/
void
swrp_smf_fini(fmd_hdl_t *hdl)
{
	if (swrp_smf_vrfy_timerid) {
		sw_timer_remove(hdl, myid, swrp_smf_vrfy_timerid);
		swrp_smf_vrfy_timerid = 0;
	}

	if (smf_prblm_cache != NULL) {
		size_t sz = SMF_PRBLM_CACHE_BUFSZ(smf_prblm_cache->nentries);

		fmd_hdl_free(hdl, smf_prblm_cache, sz);
		smf_prblm_cache = NULL;
	}
}

const struct sw_subinfo smf_response_info = {
	"smf repair",			/* swsub_name */
	swrp_smf_props,			/* swsub_props */
	SW_CASE_NONE,			/* swsub_casetype */
	swrp_smf_init,			/* swsub_init */
	swrp_smf_fini,			/* swsub_fini */
	swrp_smf_timeout,		/* swsub_timeout */
	NULL,				/* swsub_case_close */
	NULL,				/* swsub_case_vrfy */
};
